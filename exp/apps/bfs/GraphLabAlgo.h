#ifndef APPS_BFS_GRAPHLABALGO_H
#define APPS_BFS_GRAPHLABALGO_H

#include "Galois/DomainSpecificExecutors.h"
#include "Galois/Graph/OCGraph.h"
#include "Galois/Graph/LCGraph.h"
#include "Galois/Graph/GraphNodeBag.h"

#include <random>
#include <boost/mpl/if.hpp>

#include "BFS.h"

struct GraphLabBFS {
  typedef typename Galois::Graph::LC_CSR_Graph<SNode,void>
    ::with_no_lockable<true>::type
    ::with_numa_alloc<true>::type InnerGraph;
  typedef Galois::Graph::LC_InOut_Graph<InnerGraph> Graph;
  typedef Graph::GraphNode GNode;

  void readGraph(Graph& graph) {
    readInOutGraph(graph);
  }

  std::string name() const { return "GraphLab"; }

  struct Program {
    typedef size_t gather_type;

    struct message_type {
      size_t value;
      message_type(): value(std::numeric_limits<size_t>::max()) { }
      explicit message_type(size_t v): value(v) { }
      message_type& operator+=(const message_type& other) {
        value = std::min<size_t>(value, other.value);
        return *this;
      }
    };

    typedef int tt_needs_scatter_out_edges;

  private:
    size_t received_dist;
    bool changed;

  public:
    Program(): received_dist(DIST_INFINITY), changed(false) { }

    void init(Graph& graph, GNode node, const message_type& msg) {
      received_dist = msg.value;
    }

    void apply(Graph& graph, GNode node, const gather_type&) {
      changed = false;
      SNode& sdata = graph.getData(node, Galois::MethodFlag::NONE);
      if (sdata.dist > received_dist) {
        changed = true;
        sdata.dist = received_dist;
      }
    }

    bool needsScatter(Graph& graph, GNode node) {
      return changed;
    }

    void gather(Graph& graph, GNode node, GNode src, GNode dst, gather_type&, typename Graph::edge_data_reference) { }

    void scatter(Graph& graph, GNode node, GNode src, GNode dst,
        Galois::GraphLab::Context<Graph,Program>& ctx, typename Graph::edge_data_reference) {
      SNode& sdata = graph.getData(node, Galois::MethodFlag::NONE);
      Dist newDist = sdata.dist + 1;

      if (graph.getData(dst, Galois::MethodFlag::NONE).dist > newDist) {
        ctx.push(dst, message_type(newDist));
      }
    }
  };

  void operator()(Graph& graph, const GNode& source) {
    Galois::GraphLab::SyncEngine<Graph,Program> engine(graph, Program());
    engine.signal(source, Program::message_type(0));
    engine.execute();
  }
};

static void bitwise_or(std::vector<std::vector<bool> >& v1, const std::vector<std::vector<bool> >& v2) {
  while (v1.size() < v2.size())
    v1.emplace_back();

  for (size_t a = 0; a < v1.size(); ++a) {
    while (v1[a].size() < v2[a].size()) {
      v1[a].push_back(false);
    }
    for (size_t i = 0; i < v2[a].size(); ++i) {
      v1[a][i] = v1[a][i] || v2[a][i];
    }
  }
}

template<bool UseHashed>
struct GraphLabDiameter {
  struct LNode {
    std::vector<std::vector<bool> > bitmask1;
    std::vector<std::vector<bool> > bitmask2;
    bool odd_iteration;

    LNode(): odd_iteration(false) { }
  };

  typedef typename Galois::Graph::LC_CSR_Graph<LNode,void>
    ::template with_no_lockable<true>::type
    ::template with_numa_alloc<true>::type InnerGraph;
  typedef Galois::Graph::LC_InOut_Graph<InnerGraph> Graph;
  typedef typename Graph::GraphNode GNode;

  void readGraph(Graph& graph) { readInOutGraph(graph); }

  struct Initialize {
    Graph& graph;
    //FIXME: don't use mutable
    mutable Galois::optional<std::mt19937> gen;
#if __cplusplus >= 201103L || defined(HAVE_CXX11_UNIFORM_REAL_DISTRIBUTION)
    mutable std::uniform_real_distribution<float> dist;
#else
    mutable std::uniform_real<float> dist;
#endif

    Initialize(Graph& g): graph(g) { }

    size_t hash_value() const {
      if (!gen) {
        gen = std::mt19937();
        gen->seed(Galois::Runtime::LL::getTID());
      }
      size_t ret = 0;
      while (dist(*gen) < 0.5) {
        ret++;
      }
      return ret;
    }

    void initHashed(LNode& data) const {
      for (size_t i = 0; i < 10; ++i) {
        size_t hash_val = hash_value();

        std::vector<bool> mask1(hash_val + 2, 0);
        mask1[hash_val] = 1;
        data.bitmask1.push_back(mask1);
        std::vector<bool> mask2(hash_val + 2, 0);
        mask2[hash_val] = 1;
        data.bitmask2.push_back(mask2);
      }
    }

    void initExact(LNode& data, size_t id) const {
      std::vector<bool> mask1(id + 2, 0);
      mask1[id] = 1;
      data.bitmask1.push_back(mask1);
      std::vector<bool> mask2(id + 2, 0);
      mask2[id] = 1;
      data.bitmask2.push_back(mask2);
    }

    void operator()(GNode n) const {
      LNode& data = graph.getData(n, Galois::MethodFlag::NONE);
      if (UseHashed)
        initHashed(data);
      else
        initExact(data, n);
    }
  };

  struct Program {
    struct gather_type {
      std::vector<std::vector<bool> > bitmask;
      gather_type() { }
      explicit gather_type(const std::vector<std::vector<bool> > & in_b) {
        for(size_t i=0;i<in_b.size();++i){
          bitmask.push_back(in_b[i]);
        }
      }

      gather_type& operator+=(const gather_type& other) {
        bitwise_or(bitmask, other.bitmask);
        return *this;
      }
    };
    typedef Galois::GraphLab::EmptyMessage message_type;

    typedef std::pair<GNode,message_type> WorkItem;
    typedef int tt_needs_gather_out_edges;
    
    void gather(Graph& graph, GNode node, GNode src, GNode dst, gather_type& gather, typename Graph::edge_data_reference) {
      LNode& sdata = graph.getData(node, Galois::MethodFlag::NONE);
      LNode& ddata = graph.getData(dst, Galois::MethodFlag::NONE);
      if (sdata.odd_iteration) {
        bitwise_or(gather.bitmask, ddata.bitmask2);
        //gather += gather_type(ddata.bitmask2);
      } else {
        bitwise_or(gather.bitmask, ddata.bitmask1);
        //gather += gather_type(ddata.bitmask1);
      }
    }

    void apply(Graph& graph, GNode node, const gather_type& total) {
      LNode& data = graph.getData(node, Galois::MethodFlag::NONE);
      if (data.odd_iteration) {
        if (total.bitmask.size() > 0)
          bitwise_or(data.bitmask1, total.bitmask);
        data.odd_iteration = false;
      } else {
        if (total.bitmask.size() > 0)
          bitwise_or(data.bitmask2, total.bitmask);
        data.odd_iteration = true;
      }
    }

    void init(Graph& graph, GNode node, const message_type& msg) { }
    bool needsScatter(Graph& graph, GNode node) { return false; }
    void scatter(Graph& graph, GNode node, GNode src, GNode dst,
        Galois::GraphLab::Context<Graph,Program>& ctx, typename Graph::edge_data_reference) { }
  };

  struct count_exact_visited {
    Graph& graph;
    count_exact_visited(Graph& g): graph(g) { }
    size_t operator()(GNode n) const {
      LNode& data = graph.getData(n);
      size_t count = 0;
      for (size_t i = 0; i < data.bitmask1[0].size(); ++i)
        if (data.bitmask1[0][i])
          count++;
      return count;
    }
  };

  struct count_hashed_visited {
    Graph& graph;
    count_hashed_visited(Graph& g): graph(g) { }

    size_t approximate_pair_number(const std::vector<std::vector<bool> >& bitmask) const {
      float sum = 0.0;
      for (size_t a = 0; a < bitmask.size(); ++a) {
        for (size_t i = 0; i < bitmask[a].size(); ++i) {
          if (bitmask[a][i] == 0) {
            sum += (float) i;
            break;
          }
        }
      }
      return (size_t) (pow(2.0, sum / (float) (bitmask.size())) / 0.77351);
    }

    size_t operator()(GNode n) const {
      LNode& data = graph.getData(n);
      return approximate_pair_number(data.bitmask1);
    }
  };

  size_t operator()(Graph& graph, const GNode& source) {
    size_t previous_count = 0;
    size_t diameter = 0;
    for (size_t iter = 0; iter < 100; ++iter) {
      //Galois::GraphLab::executeSync(graph, graph, Program());
      Galois::GraphLab::SyncEngine<Graph,Program> engine(graph, Program());
      engine.execute();

      Galois::do_all(graph.begin(), graph.end(), [&](GNode n) {
        LNode& data = graph.getData(n);
        if (data.odd_iteration == false) {
          data.bitmask2 = data.bitmask1;
        } else {
          data.bitmask1 = data.bitmask2;
        }
      });

      size_t current_count;
      if (UseHashed)
        current_count = Galois::ParallelSTL::map_reduce(graph.begin(), graph.end(),
            count_hashed_visited(graph), (size_t) 0, std::plus<size_t>());
      else
        current_count = Galois::ParallelSTL::map_reduce(graph.begin(), graph.end(),
            count_exact_visited(graph), (size_t) 0, std::plus<size_t>());

      std::cout << iter + 1 << "-th hop: " << current_count << " vertex pairs are reached\n";
      if (iter > 0 && (float) current_count < (float) previous_count * (1.0 + 0.0001)) {
        diameter = iter;
        std::cout << "Converged.\n";
        break;
      }
      previous_count = current_count;
    }

    return diameter;
  }
};

#endif
