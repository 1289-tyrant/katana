/* 
 * License:
 *
 * Copyright (C) 2014, The University of Texas at Austin. All rights reserved.
 * UNIVERSITY EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES CONCERNING THIS
 * SOFTWARE AND DOCUMENTATION, INCLUDING ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR ANY PARTICULAR PURPOSE, NON-INFRINGEMENT AND WARRANTIES OF
 * PERFORMANCE, AND ANY WARRANTY THAT MIGHT OTHERWISE ARISE FROM COURSE OF
 * DEALING OR USAGE OF TRADE.  NO WARRANTY IS EITHER EXPRESS OR IMPLIED WITH
 * RESPECT TO THE USE OF THE SOFTWARE OR DOCUMENTATION. Under no circumstances
 * shall University be liable for incidental, special, indirect, direct or
 * consequential damages or loss of profits, interruption of business, or
 * related expenses which may arise from use of Software or Documentation,
 * including but not limited to those resulting from defects in Software and/or
 * Documentation, or loss or inaccuracy of data of any kind.
 *
 * Stochastic gradient descent for matrix factorization, implemented with Distributerd Galois.
 *
 * Author: Prad Nelluru <pradn@cs.utexas.edu>
 * Author: Andrew Lenharth <andrew@lenharth.org>
 */

#include "Galois/Galois.h"
#include "Galois/gstl.h"
#include "Galois/Graphs/LC_Dist_Graph.h"
#include "Galois/Graph/FileGraph.h"
#include "Galois/Bag.h"

#include "Lonestar/BoilerPlate.h"

#include <iostream>

class BoxIterator :public std::iterator<std::random_access_iterator_tag, 
                                        std::pair<unsigned,unsigned>, int64_t> {
  typedef std::pair<unsigned, unsigned> Ty;
  uint64_t off;
  unsigned width; //of x
  unsigned adjx, adjy;
public:
  BoxIterator(uint64_t _off, unsigned _w, unsigned _x, unsigned _y)
    :off(_off), width(_w), adjx(_x), adjy(_y) {}
  BoxIterator() = default;

  bool operator==(const BoxIterator& rhs) const { return off == rhs.off; }
  bool operator!=(const BoxIterator& rhs) const { return off != rhs.off; }
  bool operator< (const BoxIterator& rhs) const { return off  < rhs.off; }
  bool operator> (const BoxIterator& rhs) const { return off  > rhs.off; }
  bool operator<=(const BoxIterator& rhs) const { return off <= rhs.off; }
  bool operator>=(const BoxIterator& rhs) const { return off >= rhs.off; }
  
  BoxIterator& operator++() { ++off; return *this; }
  BoxIterator& operator--() { --off; return *this; }
  BoxIterator operator++(int) { auto tmp = *this; ++off; return tmp; }
  BoxIterator operator--(int) { auto tmp = *this; --off; return tmp; }

  BoxIterator& operator+=(unsigned x) { off += x; return *this; }
  BoxIterator operator+(unsigned x) { auto tmp = *this; tmp.off += x; return tmp; }

  int64_t operator-(const BoxIterator& rhs) const { return off - rhs.off; }

  value_type operator*() const {
    unsigned y_off = off / width;
    unsigned x_off = off % width;
    return std::make_pair(adjy + y_off, adjx + x_off);
  }

  // const value_type* operator->() const {
  //   return &c;
  // }

};

template<typename PreFn, typename RngTy, typename FunctionTy>
class BlockedExecuter {
  PreFn p;
  RngTy r;
  FunctionTy f;
  unsigned x1, x2, y1, y2;

public:
  BlockedExecuter(const PreFn& _p, const RngTy& _r, const FunctionTy& _f,
                  unsigned _x1, unsigned _x2, unsigned _y1, unsigned _y2)
    :p(_p), r(_r), f(_f), x1(_x1), x2(_x2), y1(_y1), y2(_y2) {}

  void operator()() {
    unsigned num = Galois::Runtime::NetworkInterface::Num;
    unsigned id = Galois::Runtime::NetworkInterface::ID;
    unsigned x1Local, x2Local;
    std::tie(x1Local,x2Local) = Galois::block_range(x1, x2, id, num);
    unsigned tPrefetch = 0, tFind = 0, tDo = 0;
    for (unsigned i = 0; i < num; ++i) {
      unsigned idLocal = (id + i) % num;
      unsigned y1Local, y2Local;
      std::tie(y1Local, y2Local) = Galois::block_range(y1,y2,idLocal,num);
      std::cout << id
                << " Movies: " << x1Local << " - " << x2Local
                << " Users: " << y1Local << " - " << y2Local
                << "\n";
      Galois::InsertBag<typename RngTy::value_type> items;
      RngTy& rL = r;
      Galois::Timer T;

      T.start();
      // Galois::Runtime::do_all_impl(Galois::Runtime::makeStandardRange(boost::make_counting_iterator(y1Local), boost::make_counting_iterator(y2Local)), p, "BlockedExecutor::prefetch", true);
      // Galois::Runtime::getSystemNetworkInterface().flush();
      // Galois::Runtime::getSystemNetworkInterface().handleReceives();
      T.stop();
      tPrefetch += T.get();
      //      std::cout << Galois::Runtime::NetworkInterface::ID << " prefetch: " << T.get() << "\n";

      T.start();
      Galois::Runtime::for_each_impl<Galois::WorkList::StableIterator<> >(Galois::Runtime::makeStandardRange(boost::make_counting_iterator(x1Local), boost::make_counting_iterator(x2Local)), [&items, &rL, y1Local, y2Local] (unsigned z, Galois::UserContext<unsigned>& ctx) { rL(z, std::make_pair(y1Local, y2Local), items); }, "BlockedExecutor::find");
      T.stop();
      tFind += T.get();
      //      std::cout << Galois::Runtime::NetworkInterface::ID << " find: " << T.get() << "\n";

      T.start();
      Galois::Runtime::for_each_impl<Galois::WorkList::StableIterator<> >(
        Galois::Runtime::makeStandardRange(items.begin(), items.end()),
        f, "BlockedExecutor::do");
      T.stop();
      tDo += T.get();
      //      std::cout << Galois::Runtime::NetworkInterface::ID << " do: " << T.get() << "\n";
      //barrier before execution in foreach should be sufficient
    }
    std::cout << Galois::Runtime::NetworkInterface::ID << " ALL p: " << tPrefetch << " f: " << tFind << " d: " << tDo << "\n";
  }
};

template<typename PreFn, typename RngTy, typename FnTy>
void for_each_blocked_pad(Galois::Runtime::RecvBuffer& buf) {
  PreFn p;
  RngTy r;
  FnTy f;
  unsigned x1, x2, y1, y2;
  Galois::Runtime::gDeserialize(buf, p, r, f, x1, x2, y1, y2);
  BlockedExecuter<PreFn, RngTy, FnTy> ex(p, r, f, x1,x2,y1,y2);
  ex();
}

template<typename PreFn, typename RngTy, typename FnTy>
void for_each_blocked(unsigned x1, unsigned x2, unsigned y1, unsigned y2, 
                      PreFn p, RngTy r, FnTy f) {
  Galois::Runtime::NetworkInterface& net = Galois::Runtime::getSystemNetworkInterface();
  for (unsigned i = 1; i < Galois::Runtime::NetworkInterface::Num; i++) {
    Galois::Runtime::SendBuffer buf;
    // serialize function and data
    Galois::Runtime::gSerialize(buf, p, r, f, x1,x2,y1,y2);
    //send data
    net.sendLoop (i, &for_each_blocked_pad<PreFn, RngTy, FnTy>, buf);
  }
  net.flush();
  net.handleReceives();
  //Start locally
  BlockedExecuter<PreFn, RngTy,FnTy> ex(p, r, f, x1,x2,y1,y2);
  ex();
}

////////////////////////////////////////////////////////////////////////////////
// User code
////////////////////////////////////////////////////////////////////////////////
static const char* const name = "Matrix Completion";
static const char* const desc = "Computes Matrix Decomposition using Stochastic Gradient Descent";
static const char* const url = 0;

static const int LATENT_VECTOR_SIZE = 20; //Prad's default: 100, Intel: 20

static const double LEARNING_RATE = 0.001; // GAMMA, Purdue: 0.01 Intel: 0.001
static const double DECAY_RATE = 0.9; // STEP_DEC, Purdue: 0.1 Intel: 0.9
static const double LAMBDA = 0.001; // Purdue: 1.0 Intel: 0.001
static const double BottouInit = 0.1;

enum Learn {
  Intel,
  Purdue,
  Bottou,
  Inv
};

namespace cll = llvm::cl;
static cll::opt<std::string> inputFile (cll::Positional, cll::desc("<input file>"), cll::Required);
static cll::opt<Learn> learn(cll::desc("Choose a learning function:"),
                             cll::values(
                                         clEnumVal(Intel, "Intel"),
                                         clEnumVal(Purdue, "Perdue"),
                                         clEnumVal(Bottou, "Bottou"),
                                         clEnumVal(Inv, "Simple Inverse"),
                                         clEnumValEnd), 
                             cll::init(Intel));

struct Node {
  double latent_vector[LATENT_VECTOR_SIZE]; //latent vector to be learned
  void dump(std::ostream& os) {
    os << "{" << latent_vector[0];
    for (int i = 1; i < LATENT_VECTOR_SIZE; ++i)
      os << ", " << latent_vector[i];
    os << "}";
  }
};

struct LearnFN {
  virtual double step_size(unsigned int round) const = 0;
};

struct PurdueLearnFN : public LearnFN {
  virtual double step_size(unsigned int round) const {
    return LEARNING_RATE * 1.5 / (1.0 + DECAY_RATE * pow(round + 1, 1.5));
  }
};

struct IntelLearnFN : public LearnFN {
  virtual double step_size(unsigned int round) const {
    return LEARNING_RATE * pow (DECAY_RATE, round);
  }
};

struct BottouLearnFN : public LearnFN {
  virtual double step_size(unsigned int round) const {
    return BottouInit / (1 + BottouInit*LAMBDA*round);
  }
};

struct InvLearnFN : public LearnFN {
  virtual double step_size(unsigned int round) const {
    return (double)1 / (double)(round + 1);
  }
};

typedef typename Galois::Graph::LC_Dist<Node, int> Graph;
typedef Graph::GraphNode GNode;

//possibly over-typed
double vector_dot(const Node& movie_data, const Node& user_data) {
  //Could just specify restrict on parameters since vector is built in
  const double* __restrict__ movie_latent = movie_data.latent_vector;
  const double* __restrict__ user_latent = user_data.latent_vector;

  double dp = 0.0;
  for (int i = 0; i < LATENT_VECTOR_SIZE; ++i)
    dp += user_latent[i] * movie_latent[i];
  assert(std::isnormal(dp));
  return dp;
}

double doGradientUpdate(Node& movie_data, Node& user_data, int edge_rating, double step_size)
{
  double* __restrict__ movie_latent = movie_data.latent_vector;
  double* __restrict__ user_latent = user_data.latent_vector;
  
  //calculate error
  double old_dp = vector_dot(movie_data, user_data);
  double cur_error = edge_rating - old_dp;
  assert(cur_error < 1000 && cur_error > -1000);
  
  //take gradient step
  for(unsigned int i = 0; i < LATENT_VECTOR_SIZE; i++) {
    double prev_movie_val = movie_latent[i];
    double prev_user_val = user_latent[i];
    movie_latent[i] += step_size * (cur_error * prev_user_val  - LAMBDA * prev_movie_val);
    assert(std::isnormal(movie_latent[i]));
    user_latent[i]  += step_size * (cur_error * prev_movie_val - LAMBDA * prev_user_val);
    assert(std::isnormal(user_latent[i]));
  }
  return cur_error;
}

//Edge doer
struct sgd_edge_pair {
  Graph::pointer g;
  double step_size;
  sgd_edge_pair(Graph::pointer g, double ss) :g(g), step_size(ss) {}
  sgd_edge_pair() = default;

  template<typename Context>
  void operator()(std::pair<unsigned, unsigned> edge, Context& cnx) {
    auto src = *(g->begin() + edge.first);
    auto dst = *(g->begin() + edge.second);
    auto ii = g->edge_begin(src, Galois::MethodFlag::SRC_ONLY);
    auto ee = g->edge_end(src, Galois::MethodFlag::SRC_ONLY);
    ii = std::lower_bound(ii,ee, dst, [] (const decltype(*ii)& edg, GNode n) { return edg.dst < n; });
    if (ii != ee && g->dst(ii) == dst) {
      doGradientUpdate(g->at(src), g->at(dst), g->at(ii), step_size);
    }
  }

  template<typename Context>
  void operator()(std::pair<GNode, unsigned> item, Context& cnx) {
    auto src = item.first;
    auto ii = g->edge_begin(src, Galois::MethodFlag::SRC_ONLY);
    ii += item.second;
    auto dst = g->dst(ii);
    doGradientUpdate(g->at(src), g->at(dst), g->at(ii), step_size);
  }

};

struct sgd_edge_finder {
  Graph::pointer g;
  sgd_edge_finder(Graph::pointer g) : g(g) {}
  sgd_edge_finder() = default;

  typedef std::pair<GNode, unsigned> value_type;

  //  template<typename Context>
  void operator()(unsigned movie, std::pair<unsigned, unsigned> edge_range, 
                  Galois::InsertBag<value_type>& bag) { //, Context& cnx) {
    auto src = *(g->begin() + movie);
    auto dst1 = *(g->begin() + edge_range.first);
    auto dst2 = *(g->begin() + (edge_range.second - 1));
    auto ii = g->edge_begin(src, Galois::MethodFlag::SRC_ONLY);
    auto ee = g->edge_end(src, Galois::MethodFlag::SRC_ONLY);
    auto ii2 = std::lower_bound(ii, ee, dst1, [] (const decltype(*ii)& edg, GNode n) { return edg.dst < n; });
    auto ee2 = std::upper_bound(ii, ee, dst2, [] (GNode n, const decltype(*ii)& edg) { return n < edg.dst; });
    while (ii2 != ee2) {
      bag.push_back(std::make_pair(src, std::distance(ii, ii2)));
      ++ii2;
    }
  }
};

struct node_prefetch {
  Graph::pointer g;
  node_prefetch(Graph::pointer g) : g(g) {}
  node_prefetch() = default;

  void operator()(unsigned user) {
    Galois::Runtime::prefetch( *(g->begin() + user) );
  }
};

void go(Graph::pointer g, unsigned int numMovieNodes, unsigned int numUserNodes, const LearnFN* lf) {
  for (int i = 0; i < 20; ++i) {
    double step_size = lf->step_size(i);
    std::cout << "Step Size: " << step_size << "\n";
    Galois::Timer timer;
    timer.start();
    for_each_blocked(0, numMovieNodes, numMovieNodes, numMovieNodes + numUserNodes,
                     node_prefetch{g},
                     sgd_edge_finder{g},
                     sgd_edge_pair{g, step_size});
    timer.stop();
    std::cout << "Time: " << timer.get() << "ms\n";
  }
}

static double genRand () {
  // generate a random double in (-1,1)
  return 2.0 * ((double)std::rand () / (double)RAND_MAX) - 1.0;
}

// Initializes latent vector and id for each node
struct initializeGraphData {
  struct stats : public Galois::Runtime::Lockable {
    std::atomic<unsigned int> numMovieNodes;
    std::atomic<unsigned int> numUserNodes;
    std::atomic<unsigned int> numRatings;
    stats() : numMovieNodes(0), numUserNodes(0), numRatings(0) {}
    stats(Galois::Runtime::PerHost<stats>) : numMovieNodes(0), numUserNodes(0), numRatings(0) {}
    stats(Galois::Runtime::DeSerializeBuffer& s) {
      deserialize(s);
    }
    //serialize
    typedef int tt_has_serialize;
    void serialize(Galois::Runtime::SerializeBuffer& s) const {
      gSerialize(s,(unsigned int)numMovieNodes,(unsigned int)numUserNodes,(unsigned int)numRatings);
    }
    void deserialize(Galois::Runtime::DeSerializeBuffer& s) {
      unsigned int mn,un,r;
      gDeserialize(s,mn,un,r);
      numMovieNodes = mn;
      numUserNodes = un;
      numRatings = r;
    }

  };

  Graph::pointer g;
  Galois::Runtime::PerHost<stats> s;

  std::tuple<unsigned int, unsigned int, unsigned int>
  static go(Graph::pointer g)
  {
    const unsigned SEED = 4562727;
    std::srand (SEED);

    Galois::Runtime::PerHost<stats> s = Galois::Runtime::PerHost<stats>::allocate();
    
    Galois::for_each_local(g, initializeGraphData{g,s}, Galois::loopname("init"));

    unsigned int numMovieNodes = 0;
    unsigned int numUserNodes = 0;
    unsigned int numRatings = 0;
    for (unsigned x = 0; x < Galois::Runtime::NetworkInterface::Num; ++x) {
      auto rv = s.remote(x);
      numMovieNodes += rv->numMovieNodes;
      numUserNodes += rv->numUserNodes;
      numRatings += rv->numRatings;
    }

    return std::make_tuple(numMovieNodes, numUserNodes, numRatings);
  }

  void operator()(GNode gnode, Galois::UserContext<GNode>& ctx) {
    Node& data = g->at(gnode);
    
    //fill latent vectors with random values
    for(int i = 0; i < LATENT_VECTOR_SIZE; i++)
      data.latent_vector[i] = genRand();
    
    g->sort_edges(gnode, [] (GNode e1_dst, const int& e1_data,
                             GNode e2_dst, const int& e2_data) {
                    return e1_dst < e2_dst;
                  }, Galois::MethodFlag::NONE);
    
    //count number of movies we've seen; only movies nodes have edges
    unsigned int num_edges = 
      std::distance(g->edge_begin(gnode, Galois::NONE), g->edge_end(gnode, Galois::NONE));
    s->numRatings += num_edges;
    if(num_edges > 0) {
      s->numMovieNodes++;
      //std::cout << "M";
    } else {
      s->numUserNodes++;
      //std::cout <<"U";
    }
  }
};

int main(int argc, char** argv) {	
  LonestarStart (argc, argv, name, desc, url);
  Galois::StatManager statManager;
  
  // BoxIterator ii(0,5,0,0), ee(30,5,0,0);
  // while (ii != ee) {
  //   auto p = *ii++;
  //   std::cout << p.first << "," << p.second << "  *  " << ee - ii << "\n";
  //   assert(p < *ii);
  // }

  Galois::Timer ltimer;
  ltimer.start();
  //allocate local computation graph
  Graph::pointer g;
  {
    Galois::Graph::FileGraph fg;
    fg.fromFile(inputFile);
    std::vector<unsigned> counts;
    for(auto& N : fg)
      counts.push_back(std::distance(fg.edge_begin(N), fg.edge_end(N)));
    g = Graph::allocate(counts);
    for (unsigned x = 0; x < counts.size(); ++x) {
      auto fgn = *(fg.begin() + x);
      auto gn = *(g->begin() + x);
      for (auto ii = fg.edge_begin(fgn), ee = fg.edge_end(fgn);
           ii != ee; ++ii) {
        unsigned dst = fg.getEdgeDst(ii);
        int val = fg.getEdgeData<int>(ii);
        g->addEdge(gn, *(g->begin() + dst), val);
      }
    }
  }
  ltimer.stop();
  std::cout << "Graph Loading: " << ltimer.get() << "ms\n";

  //fill each node's id & initialize the latent vectors
  unsigned int numMovieNodes, numUserNodes, numRatings;
  Galois::Timer itimer;
  itimer.start();
  std::tie(numMovieNodes, numUserNodes,numRatings) = initializeGraphData::go(g);
  itimer.stop();
  std::cout << "Graph Init: " << itimer.get() << "ms\n";

  std::cout << "Input initialized, num users = " << numUserNodes 
            << ", num movies = " << numMovieNodes
            << ", num ratings = " << numRatings
            << std::endl;

  std::unique_ptr<LearnFN> lf;
  switch (learn) {
  case Intel:
    lf.reset(new IntelLearnFN);
    break;
  case Purdue:
    lf.reset(new PurdueLearnFN);
    break;
  case Bottou:
    lf.reset(new BottouLearnFN);
    break;
  case Inv:
    lf.reset(new InvLearnFN);
    break;
  }

  Galois::StatTimer timer;
  timer.start();
  go(g, numMovieNodes, numUserNodes, lf.get());
  timer.stop();

  Galois::Runtime::getSystemNetworkInterface().terminate();

  return 0;
}
