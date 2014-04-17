/** Graph converter -*- C++ -*-
 * @file
 * @section License
 *
 * Galois, a framework to exploit amorphous data-parallelism in irregular
 * programs.
 *
 * Copyright (C) 2013, The University of Texas at Austin. All rights reserved.
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
 * @author Dimitrios Prountzos <dprountz@cs.utexas.edu>
 * @author Donald Nguyen <ddn@cs.utexas.edu>
 */
#include "Galois/config.h"
#include "Galois/LargeArray.h"
#include "Galois/Graph/FileGraph.h"

#include "llvm/Support/CommandLine.h"

#include <boost/mpl/if.hpp>
#include <algorithm>
#include <deque>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdint.h>
#include <vector>
#include GALOIS_CXX11_STD_HEADER(random)

#include <fcntl.h>
#include <cstdlib>

namespace cll = llvm::cl;

enum ConvertMode {
  bipartitegr2bigpetsc,
  bipartitegr2littlepetsc,
  bipartitegr2sorteddegreegr,
  dimacs2gr,
  edgelist2gr,
  gr2binarypbbs32,
  gr2binarypbbs64,
  gr2bsml,
  gr2cgr,
  gr2dimacs,
  gr2edgelist,
  gr2lowdegreegr,
  gr2mtx,
  gr2partdstgr,
  gr2partsrcgr,
  gr2pbbs,
  gr2pbbsedges,
  gr2randgr,
  gr2randomweightgr,
  gr2ringgr,
  gr2rmat,
  gr2sgr,
  gr2sorteddegreegr,
  gr2sorteddstgr,
  gr2sortedparentdegreegr,
  gr2sortedweightgr,
  gr2tgr,
  gr2treegr,
  gr2trigr,
  mtx2gr,
  nodelist2gr,
  pbbs2gr
};

enum EdgeType {
  float32,
  float64,
  int32,
  int64,
  uint32,
  uint64,
  void_
};

static cll::opt<std::string> inputFilename(cll::Positional, 
    cll::desc("<input file>"), cll::Required);
static cll::opt<std::string> outputFilename(cll::Positional,
    cll::desc("<output file>"), cll::Required);
static cll::opt<std::string> transposeFilename("graphTranspose",
    cll::desc("[transpose graph file]"), cll::init(""));
static cll::opt<std::string> outputPermutationFilename("outputNodePermutation",
    cll::desc("[output node permutation file]"), cll::init(""));
static cll::opt<EdgeType> edgeType("edgeType", cll::desc("Input/Output edge type:"),
    cll::values(
      clEnumValN(EdgeType::float32, "float32", "32 bit floating point edge values"),
      clEnumValN(EdgeType::float64, "float64", "64 bit floating point edge values"),
      clEnumValN(EdgeType::int32, "int32", "32 bit int edge values"),
      clEnumValN(EdgeType::int64, "int64", "64 bit int edge values"),
      clEnumValN(EdgeType::uint32, "uint32", "32 bit unsigned int edge values"),
      clEnumValN(EdgeType::uint64, "uint64", "64 bit unsigned int edge values"),
      clEnumValN(EdgeType::void_, "void", "no edge values"),
      clEnumValEnd), cll::init(EdgeType::void_));
static cll::opt<ConvertMode> convertMode(cll::desc("Conversion mode:"),
    cll::values(
      clEnumVal(bipartitegr2bigpetsc, "Convert bipartite binary gr to big-endian PETSc format"),
      clEnumVal(bipartitegr2littlepetsc, "Convert bipartite binary gr to little-endian PETSc format"),
      clEnumVal(bipartitegr2sorteddegreegr, "Sort nodes of bipartite binary gr by degree"),
      clEnumVal(dimacs2gr, "Convert dimacs to binary gr"),
      clEnumVal(edgelist2gr, "Convert edge list to binary gr"),
      clEnumVal(gr2binarypbbs32, "Convert binary gr to unweighted binary pbbs graph"),
      clEnumVal(gr2binarypbbs64, "Convert binary gr to unweighted binary pbbs graph"),
      clEnumVal(gr2bsml, "Convert binary gr to binary sparse MATLAB matrix"),
      clEnumVal(gr2bsml, "Convert binary void gr to binary sparse MATLAB matrix"),
      clEnumVal(gr2cgr, "Clean up binary gr: remove self edges and multi-edges"),
      clEnumVal(gr2dimacs, "Convert binary gr to dimacs"),
      clEnumVal(gr2edgelist, "Convert binary gr to edgelist"),
      clEnumVal(gr2lowdegreegr, "Remove high degree nodes from binary gr"),
      clEnumVal(gr2mtx, "Convert binary gr to matrix market format"),
      clEnumVal(gr2partdstgr, "Partition binary gr in N pieces by destination nodes"),
      clEnumVal(gr2partsrcgr, "Partition binary gr in N pieces by source nodes"),
      clEnumVal(gr2pbbs, "Convert binary gr to pbbs graph"),
      clEnumVal(gr2pbbsedges, "Convert binary gr to pbbs edge list"),
      clEnumVal(gr2randgr, "Randomly permute nodes of binary gr"),
      clEnumVal(gr2randomweightgr, "Add or Randomize edge weights"),
      clEnumVal(gr2ringgr, "Convert binary gr to strongly connected graph by adding ring overlay"),
      clEnumVal(gr2rmat, "Convert binary gr to RMAT graph"),
      clEnumVal(gr2sgr, "Convert binary gr to symmetric graph by adding reverse edges"),
      clEnumVal(gr2sorteddegreegr, "Sort nodes by degree"),
      clEnumVal(gr2sorteddstgr, "Sort outgoing edges of binary gr by edge destination"),
      clEnumVal(gr2sortedparentdegreegr, "Sort nodes by degree of parent"),
      clEnumVal(gr2sortedweightgr, "Sort outgoing edges of binary gr by edge weight"),
      clEnumVal(gr2tgr, "Transpose binary gr"),
      clEnumVal(gr2treegr, "Convert binary gr to strongly connected graph by adding tree overlay"),
      clEnumVal(gr2trigr, "Convert symmetric binary gr to triangular form by removing reverse edges"),
      clEnumVal(mtx2gr, "Convert matrix market format to binary gr"),
      clEnumVal(nodelist2gr, "Convert node list to binary gr"),
      clEnumVal(pbbs2gr, "Convert pbbs graph to binary gr"),
      clEnumValEnd), cll::Required);
static cll::opt<int> numParts("numParts", 
    cll::desc("number of parts to partition graph into"), cll::init(64));
static cll::opt<int> maxValue("maxValue",
    cll::desc("maximum weight to add for tree, ring and random weight conversions"), cll::init(100));
static cll::opt<int> minValue("minValue",
    cll::desc("minimum weight to add for random weight conversions"), cll::init(1));
static cll::opt<int> maxDegree("maxDegree",
    cll::desc("maximum degree to keep"), cll::init(2*1024));

struct Conversion { };
struct HasOnlyVoidSpecialization { };
struct HasNoVoidSpecialization { };

template<typename EdgeTy, typename C>
void convert(C& c, Conversion) {
  c.template convert<EdgeTy>(inputFilename, outputFilename);
}

template<typename EdgeTy, typename C>
void convert(C& c, HasOnlyVoidSpecialization, typename std::enable_if<std::is_same<EdgeTy,void>::value>::type* = 0) {
  c.template convert<EdgeTy>(inputFilename, outputFilename);
}

template<typename EdgeTy, typename C>
void convert(C& c, HasOnlyVoidSpecialization, typename std::enable_if<!std::is_same<EdgeTy,void>::value>::type* = 0) {
  GALOIS_DIE("conversion undefined for non-void graphs");
}

template<typename EdgeTy, typename C>
void convert(C& c, HasNoVoidSpecialization, typename std::enable_if<!std::is_same<EdgeTy,void>::value>::type* = 0) {
  c.template convert<EdgeTy>(inputFilename, outputFilename);
}

template<typename EdgeTy, typename C>
void convert(C& c, HasNoVoidSpecialization, typename std::enable_if<std::is_same<EdgeTy,void>::value>::type* = 0) {
  GALOIS_DIE("conversion undefined for void graphs");
}

static std::string edgeTypeToName(EdgeType e) {
  switch (e) {
    case EdgeType::float32: return "float32";
    case EdgeType::float64: return "float64";
    case EdgeType::int32: return "int32";
    case EdgeType::int64: return "int64";
    case EdgeType::uint32: return "uint32";
    case EdgeType::uint64: return "uint64";
    case EdgeType::void_: return "void";
    default: abort();
  }
}

template<typename C>
void convert() {
  C c;
  std::cout << "Graph type: " << edgeTypeToName(edgeType) << "\n";
  switch (edgeType) {
    case EdgeType::float32: convert<float>(c, c); break;
    case EdgeType::float64: convert<double>(c, c); break;
    case EdgeType::int32: convert<int32_t>(c, c); break;
    case EdgeType::int64: convert<int64_t>(c, c); break;
    case EdgeType::uint32: convert<uint32_t>(c, c); break;
    case EdgeType::uint64: convert<uint64_t>(c, c); break;
    case EdgeType::void_: convert<void>(c, c); break;
    default: abort();
  };
}

static void printStatus(size_t inNodes, size_t inEdges, size_t outNodes, size_t outEdges) {
  std::cout << "InGraph : |V| = " << inNodes << ", |E| = " << inEdges << "\n";
  std::cout << "OutGraph: |V| = " << outNodes << ", |E| = " << outEdges << "\n";
}

static void printStatus(size_t inNodes, size_t inEdges) {
  printStatus(inNodes, inEdges, inNodes, inEdges);
}

template<typename EdgeValues,bool Enable>
void setEdgeValue(EdgeValues& edgeValues, int value, typename std::enable_if<Enable>::type* = 0) {
  edgeValues.set(0, static_cast<typename EdgeValues::value_type>(value));
}

template<typename EdgeValues,bool Enable>
void setEdgeValue(EdgeValues& edgeValues, int value, typename std::enable_if<!Enable>::type* = 0) { }

template<typename EdgeTy,bool Enable>
EdgeTy getEdgeValue(Galois::Graph::FileGraph& g, Galois::Graph::FileGraph::edge_iterator ii, typename std::enable_if<Enable>::type* = 0) {
  return g.getEdgeData<EdgeTy>(ii);
}

template<typename EdgeTy,bool Enable>
int getEdgeValue(Galois::Graph::FileGraph& g, Galois::Graph::FileGraph::edge_iterator ii, typename std::enable_if<!Enable>::type* = 0) {
  return 1;
}

template<typename T>
void outputPermutation(const T& perm) {
  size_t oid = 0;
  std::ofstream out(outputPermutationFilename);
  for (auto ii = perm.begin(), ei = perm.end(); ii != ei; ++ii, ++oid) {
    out << oid << "," << *ii << "\n";
  }
}

/**
 * Just a bunch of pairs or triples:
 * src dst weight?
 */
struct Edgelist2Gr: public Conversion {
  template<typename EdgeTy>
  void convert(const std::string& infilename, const std::string& outfilename) {
    typedef Galois::Graph::FileGraphWriter Writer;
    typedef Galois::LargeArray<EdgeTy> EdgeData;
    typedef typename EdgeData::value_type edge_value_type;

    Writer p;
    EdgeData edgeData;
    std::ifstream infile(infilename.c_str());

    size_t numNodes = 0;
    size_t numEdges = 0;

    while (infile) {
      size_t src;
      size_t dst;
      edge_value_type data;

      infile >> src >> dst;

      if (EdgeData::has_value)
        infile >> data;

      if (infile) {
        ++numEdges;
        if (src > numNodes)
          numNodes = src;
        if (dst > numNodes)
          numNodes = dst;
      }
    }

    numNodes++;
    p.setNumNodes(numNodes);
    p.setNumEdges(numEdges);
    p.setSizeofEdgeData(EdgeData::size_of::value);
    edgeData.create(numEdges);

    infile.clear();
    infile.seekg(0, std::ios::beg);
    p.phase1();
    while (infile) {
      size_t src;
      size_t dst;
      edge_value_type data;

      infile >> src >> dst;

      if (EdgeData::has_value)
        infile >> data;

      if (infile) {
        p.incrementDegree(src);
      }
    }

    infile.clear();
    infile.seekg(0, std::ios::beg);
    p.phase2();
    while (infile) {
      size_t src;
      size_t dst;
      edge_value_type data{};

      infile >> src >> dst;

      if (EdgeData::has_value)
        infile >> data;
      
      if (infile) {
        edgeData.set(p.addNeighbor(src, dst), data);
      }
    }

    edge_value_type* rawEdgeData = p.finish<edge_value_type>();
    if (EdgeData::has_value)
      std::copy(edgeData.begin(), edgeData.end(), rawEdgeData);

    p.structureToFile(outfilename);
    printStatus(numNodes, numEdges);
  }
};

/**
 * Convert matrix market matrix to binary graph.
 *
 * %% comments
 * % ...
 * <num nodes> <num nodes> <num edges>
 * <src> <dst> <float>
 *
 * src and dst start at 1.
 */
struct Mtx2Gr: public HasNoVoidSpecialization {
  template<typename EdgeTy>
  void convert(const std::string& infilename, const std::string& outfilename) {
    typedef Galois::Graph::FileGraphWriter Writer;
    typedef Galois::LargeArray<EdgeTy> EdgeData;
    typedef typename EdgeData::value_type edge_value_type;

    Writer p;
    EdgeData edgeData;
    uint32_t nnodes;
    size_t nedges;

    for (int phase = 0; phase < 2; ++phase) {
      std::ifstream infile(infilename.c_str());

      // Skip comments
      while (infile) {
        if (infile.peek() != '%') {
          break;
        }
        infile.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
      }

      // Read header
      char header[256];
      infile.getline(header, 256, '\n');
      std::istringstream line(header, std::istringstream::in);
      std::vector<std::string> tokens;
      while (line) {
        std::string tmp;
        line >> tmp;
        if (line) {
          tokens.push_back(tmp);
        }
      }
      if (tokens.size() != 3) {
        GALOIS_DIE("Unknown problem specification line: ", line.str());
      }
      // Prefer C functions for maximum compatibility
      //nnodes = std::stoull(tokens[0]);
      //nedges = std::stoull(tokens[2]);
      nnodes = strtoull(tokens[0].c_str(), NULL, 0);
      nedges = strtoull(tokens[2].c_str(), NULL, 0);

      // Parse edges
      if (phase == 0) {
        p.setNumNodes(nnodes);
        p.setNumEdges(nedges);
        p.setSizeofEdgeData(EdgeData::size_of::value);
        edgeData.create(nedges);
        p.phase1();
      } else {
        p.phase2();
      }

      for (size_t edge_num = 0; edge_num < nedges; ++edge_num) {
        uint32_t cur_id, neighbor_id;
        double weight = 1;

        infile >> cur_id >> neighbor_id >> weight;
        if (cur_id == 0 || cur_id > nnodes) {
          GALOIS_DIE("Error: node id out of range: ", cur_id);
        }
        if (neighbor_id == 0 || neighbor_id > nnodes) {
          GALOIS_DIE("Error: neighbor id out of range: ", neighbor_id);
        }

        // 1 indexed
        if (phase == 0) {
          p.incrementDegree(cur_id - 1);
        } else {
          edgeData.set(p.addNeighbor(cur_id - 1, neighbor_id - 1), static_cast<edge_value_type>(weight));
        }

        infile.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
      }

      infile.peek();
      if (!infile.eof()) {
        GALOIS_DIE("Error: additional lines in file");
      }
    }

    edge_value_type* rawEdgeData = p.finish<edge_value_type>();
    if (EdgeData::has_value)
      std::copy(edgeData.begin(), edgeData.end(), rawEdgeData);

    p.structureToFile(outfilename);
    printStatus(p.size(), p.sizeEdges());
  }
};

struct Gr2Mtx: public HasNoVoidSpecialization {
  template<typename EdgeTy>
  void convert(const std::string& infilename, const std::string& outfilename) { 
    typedef Galois::Graph::FileGraph Graph;
    typedef Graph::GraphNode GNode;

    Graph graph;
    graph.structureFromFile(infilename);

    std::ofstream file(outfilename.c_str());
    file << graph.size() << " " << graph.size() << " " << graph.sizeEdges() << "\n";
    for (Graph::iterator ii = graph.begin(), ei = graph.end(); ii != ei; ++ii) {
      GNode src = *ii;
      for (Graph::edge_iterator jj = graph.edge_begin(src), ej = graph.edge_end(src); jj != ej; ++jj) {
        GNode dst = graph.getEdgeDst(jj);
        double v = static_cast<double>(graph.getEdgeData<EdgeTy>(jj));
        file << src + 1 << " " << dst + 1 << " " << v << "\n";
      }
    }
    file.close();

    printStatus(graph.size(), graph.sizeEdges());
  }
};

/**
 * List of node adjacencies:
 *
 * <node id> <num neighbors> <neighbor id>*
 * ...
 */
struct Nodelist2Gr: public HasOnlyVoidSpecialization {
  template<typename EdgeTy>
  void convert(const std::string& infilename, const std::string& outfilename) {
    static_assert(std::is_same<EdgeTy,void>::value, "conversion undefined for non-void graphs");
    typedef Galois::Graph::FileGraphWriter Writer;

    Writer p;
    std::ifstream infile(infilename.c_str());

    size_t numNodes = 0;
    size_t numEdges = 0;

    while (infile) {
      size_t src;
      size_t numNeighbors;

      infile >> src >> numNeighbors;

      if (infile) {
        if (src > numNodes)
          numNodes = src;
        numEdges += numNeighbors;
      }
      infile.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }

    numNodes++;
    p.setNumNodes(numNodes);
    p.setNumEdges(numEdges);

    infile.clear();
    infile.seekg(0, std::ios::beg);
    p.phase1();
    while (infile) {
      size_t src;
      size_t numNeighbors;

      infile >> src >> numNeighbors;

      if (infile) {
        p.incrementDegree(src, numNeighbors);
      }
      infile.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }

    infile.clear();
    infile.seekg(0, std::ios::beg);
    p.phase2();
    while (infile) {
      size_t src;
      size_t numNeighbors;

      infile >> src >> numNeighbors;
      
      for (; infile && numNeighbors > 0; --numNeighbors) {
        size_t dst;
        infile >> dst;
        if (infile)
          p.addNeighbor(src, dst);
      }

      infile.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }

    p.finish<void>();

    p.structureToFile(outfilename);
    printStatus(numNodes, numEdges);
  }
};

struct Gr2Edgelist: public Conversion {
  template<typename EdgeTy>
  void convert(const std::string& infilename, const std::string& outfilename) {
    typedef Galois::Graph::FileGraph Graph;
    typedef Graph::GraphNode GNode;
    typedef Galois::LargeArray<EdgeTy> EdgeData;
    typedef typename EdgeData::value_type edge_value_type;

    Graph graph;
    graph.structureFromFile(infilename);

    std::ofstream file(outfilename.c_str());
    for (Graph::iterator ii = graph.begin(), ei = graph.end(); ii != ei; ++ii) {
      GNode src = *ii;
      for (Graph::edge_iterator jj = graph.edge_begin(src), ej = graph.edge_end(src); jj != ej; ++jj) {
        GNode dst = graph.getEdgeDst(jj);
        if (EdgeData::has_value) {
          file << src << " " << dst << " " << graph.getEdgeData<edge_value_type>(jj) << "\n";
        } else {
          file << src << " " << dst << "\n";
        }
      }
    }
    file.close();

    printStatus(graph.size(), graph.sizeEdges());
  }
};

template<bool LittleEndian, typename T>
void writePetsc(std::ofstream& out, T value) {
  static_assert(sizeof(T) == 4 || sizeof(T) == 8, "unknown data size");
  switch ((sizeof(T) == 4 ? 0 : 2) + (LittleEndian ? 0 : 1)) {
    case 3: value = Galois::convert_htobe64(value); break;
    case 2: value = Galois::convert_htole64(value); break;
    case 1: value = Galois::convert_htobe32(value); break;
    case 0: value = Galois::convert_htole32(value); break;
    default: abort();
  }

  out.write(reinterpret_cast<char *>(&value), sizeof(value));
}

template<typename OutEdgeTy, bool LittleEndian>
struct Bipartitegr2Petsc: public HasNoVoidSpecialization {
  template<typename InEdgeTy>
  void convert(const std::string& infilename, const std::string& outfilename) {
    typedef Galois::Graph::FileGraph Graph;
    typedef Graph::GraphNode GNode;
    typedef Galois::LargeArray<InEdgeTy> EdgeData;
    typedef typename EdgeData::value_type edge_value_type;

    Graph graph;
    graph.structureFromFile(infilename);

    size_t partition = 0;
    for (Graph::iterator ii = graph.begin(), ei = graph.end(); ii != ei; ++ii, ++partition) {
      GNode src = *ii;
      if (graph.edge_begin(src) == graph.edge_end(src)) {
        break;
      }
    }

    std::ofstream file(outfilename.c_str());
    writePetsc<LittleEndian, int32_t>(file, 1211216);
    writePetsc<LittleEndian, int32_t>(file, partition); // rows
    writePetsc<LittleEndian, int32_t>(file, graph.size() - partition); // columns
    writePetsc<LittleEndian, int32_t>(file, graph.sizeEdges());

    // number of nonzeros in each row
    for (Graph::iterator ii = graph.begin(), ei = ii + partition; ii != ei; ++ii) {
      GNode src = *ii;
      writePetsc<LittleEndian, int32_t>(file, std::distance(graph.edge_begin(src), graph.edge_end(src)));
    }
    
    // column indices 
    for (Graph::iterator ii = graph.begin(), ei = ii + partition; ii != ei; ++ii) {
      GNode src = *ii;
      for (Graph::edge_iterator jj = graph.edge_begin(src), ej = graph.edge_end(src); jj != ej; ++jj) {
        GNode dst = graph.getEdgeDst(jj);
        writePetsc<LittleEndian, int32_t>(file, dst - partition);
      }
    }

    // values
    for (Graph::iterator ii = graph.begin(), ei = ii + partition; ii != ei; ++ii) {
      GNode src = *ii;
      for (Graph::edge_iterator jj = graph.edge_begin(src), ej = graph.edge_end(src); jj != ej; ++jj) {
        writePetsc<LittleEndian, OutEdgeTy>(file, graph.getEdgeData<InEdgeTy>(jj));
      }
    }
    file.close();

    printStatus(graph.size(), graph.sizeEdges());
  }
};

//! Wrap generator into form form std::random_shuffle
template<typename T,typename Gen,template<typename> class Dist>
struct UniformDist {
  Gen& gen;
  
  UniformDist(Gen& g): gen(g) { }
  T operator()(T m) {
    Dist<T> r(0, m - 1);
    return r(gen);
  }
};

struct RandomizeNodes: public Conversion {
  template<typename EdgeTy>
  void convert(const std::string& infilename, const std::string& outfilename) {
    typedef Galois::Graph::FileGraph Graph;
    typedef Graph::GraphNode GNode;
    typedef Galois::LargeArray<GNode> Permutation;
    typedef typename std::iterator_traits<typename Permutation::iterator>::difference_type difference_type;

    Graph graph;
    graph.structureFromFile(infilename);

    Permutation perm;
    perm.create(graph.size());
    std::copy(boost::counting_iterator<GNode>(0), boost::counting_iterator<GNode>(graph.size()), perm.begin());
    std::mt19937 gen;
#if __cplusplus >= 201103L || defined(HAVE_CXX11_UNIFORM_INT_DISTRIBUTION)
    UniformDist<difference_type,std::mt19937,std::uniform_int_distribution> dist(gen);
#else
    UniformDist<difference_type,std::mt19937,std::uniform_int> dist(gen);
#endif
    std::random_shuffle(perm.begin(), perm.end(), dist);

    Graph out;
    Galois::Graph::permute<EdgeTy>(graph, perm, out);
    outputPermutation(perm);

    out.structureToFile(outfilename);
    printStatus(graph.size(), graph.sizeEdges());
  }
};


template<typename T, bool IsInteger = std::numeric_limits<T>::is_integer>
struct UniformDistribution { };

template<typename T>
struct UniformDistribution<T, true> {
#if __cplusplus >= 201103L || defined(HAVE_CXX11_UNIFORM_INT_DISTRIBUTION)
    std::uniform_int_distribution<T> dist;
#else
    std::uniform_int<T> dist;
#endif

  UniformDistribution(int a, int b): dist(a, b) { }
  template<typename Gen> T operator()(Gen& g) { return dist(g); }
};

template<typename T>
struct UniformDistribution<T, false> {
#if __cplusplus >= 201103L || defined(HAVE_CXX11_UNIFORM_REAL_DISTRIBUTION)
    std::uniform_real_distribution<T> dist;
#else
    std::uniform_real<T> dist;
#endif

  UniformDistribution(int a, int b): dist(a, b) { }
  template<typename Gen> T operator()(Gen& g) { return dist(g); }
};

struct RandomizeEdgeWeights: public HasNoVoidSpecialization {
  template<typename OutEdgeTy>
  void convert(const std::string& infilename, const std::string& outfilename) {
    typedef Galois::Graph::FileGraph Graph;
    typedef Graph::GraphNode GNode;
    
    Graph graph, outgraph;

    graph.structureFromFile(infilename);
    OutEdgeTy* edgeData = outgraph.structureFromGraph<OutEdgeTy>(graph);
    OutEdgeTy* edgeDataEnd = edgeData + graph.sizeEdges();

    std::mt19937 gen;
    UniformDistribution<OutEdgeTy> dist(minValue, maxValue);
    for (; edgeData != edgeDataEnd; ++edgeData) {
      *edgeData = dist(gen);
    }
    
    outgraph.structureToFile(outfilename);
    printStatus(graph.size(), graph.sizeEdges(), outgraph.size(), outgraph.sizeEdges());
  }
};

/**
 * Add edges (i, i-1) for all i \in V.
 */
struct AddRing: public Conversion {
  template<typename EdgeTy>
  void convert(const std::string& infilename, const std::string& outfilename) {
    typedef Galois::Graph::FileGraph Graph;
    typedef Galois::Graph::FileGraphWriter Writer;
    typedef Graph::GraphNode GNode;
    typedef Galois::LargeArray<EdgeTy> EdgeData;
    typedef typename EdgeData::value_type edge_value_type;
    
    Graph graph;
    graph.structureFromFile(infilename);

    Writer p;
    EdgeData edgeData;
    EdgeData edgeValue;

    uint64_t size = graph.size();

    p.setNumNodes(size);
    p.setNumEdges(graph.sizeEdges() + size);
    p.setSizeofEdgeData(EdgeData::size_of::value);
    edgeData.create(graph.sizeEdges() + size);
    edgeValue.create(1);
    //edgeValue.set(0, maxValue + 1);
    setEdgeValue<EdgeData,EdgeData::has_value>(edgeValue, maxValue + 1);

    p.phase1();
    for (Graph::iterator ii = graph.begin(), ei = graph.end(); ii != ei; ++ii) {
      GNode src = *ii;
      p.incrementDegree(src, std::distance(graph.edge_begin(src), graph.edge_end(src)) + 1);
    }

    p.phase2();
    for (Graph::iterator ii = graph.begin(), ei = graph.end(); ii != ei; ++ii) {
      GNode src = *ii;

      for (Graph::edge_iterator jj = graph.edge_begin(src), ej = graph.edge_end(src); jj != ej; ++jj) {
        GNode dst = graph.getEdgeDst(jj);
        if (EdgeData::has_value) {
          edgeData.set(p.addNeighbor(src, dst), graph.getEdgeData<edge_value_type>(jj));
        } else {
          p.addNeighbor(src, dst);
        }
      }

      GNode dst = src == 0 ? size - 1 : src - 1;
      if (EdgeData::has_value) {
        edgeData.set(p.addNeighbor(src, dst), edgeValue.at(0));
      } else {
        p.addNeighbor(src, dst);
      }
    }

    edge_value_type* rawEdgeData = p.finish<edge_value_type>();
    if (EdgeData::has_value)
      std::copy(edgeData.begin(), edgeData.end(), rawEdgeData);
    p.structureToFile(outfilename);
    printStatus(graph.size(), graph.sizeEdges(), p.size(), p.sizeEdges());
  }
};

/**
 * Add edges (i, i*2+1), (i, i*2+2) and their complement. 
 */
struct AddTree: public Conversion {
  template<typename EdgeTy>
  void convert(const std::string& infilename, const std::string& outfilename) {
    typedef Galois::Graph::FileGraph Graph;
    typedef Galois::Graph::FileGraphWriter Writer;
    typedef Graph::GraphNode GNode;
    typedef Galois::LargeArray<EdgeTy> EdgeData;
    typedef typename EdgeData::value_type edge_value_type;
    
    Graph graph;
    graph.structureFromFile(infilename);

    Writer p;
    EdgeData edgeData;
    EdgeData edgeValue;

    uint64_t size = graph.size();
    uint64_t newEdges = 0;
    if (size >= 2) {
      // Closed form counts for the loop below 
      newEdges =  (size - 1 + (2 - 1)) / 2; // (1) rounded up
      newEdges += (size - 2 + (2 - 1)) / 2; // (2) rounded up
    } else if (size >= 1)
      newEdges = 1;
    newEdges *= 2; // reverse edges

    p.setNumNodes(size);
    p.setNumEdges(graph.sizeEdges() + newEdges);
    p.setSizeofEdgeData(EdgeData::size_of::value);
    edgeData.create(graph.sizeEdges() + newEdges);
    edgeValue.create(1);
    //edgeValue.set(0, maxValue + 1);
    setEdgeValue<EdgeData,EdgeData::has_value>(edgeValue, maxValue + 1);

    p.phase1();
    for (Graph::iterator ii = graph.begin(), ei = graph.end(); ii != ei; ++ii) {
      GNode src = *ii;
      p.incrementDegree(src, std::distance(graph.edge_begin(src), graph.edge_end(src)));
      if (src * 2 + 1 < size) { // (1)
        p.incrementDegree(src);
        p.incrementDegree(src * 2 + 1);
      }
      if (src * 2 + 2 < size) { // (2)
        p.incrementDegree(src);
        p.incrementDegree(src * 2 + 2);
      }
    }

    p.phase2();
    for (Graph::iterator ii = graph.begin(), ei = graph.end(); ii != ei; ++ii) {
      GNode src = *ii;

      for (Graph::edge_iterator jj = graph.edge_begin(src), ej = graph.edge_end(src); jj != ej; ++jj) {
        GNode dst = graph.getEdgeDst(jj);
        if (EdgeData::has_value) {
          edgeData.set(p.addNeighbor(src, dst), graph.getEdgeData<edge_value_type>(jj));
        } else {
          p.addNeighbor(src, dst);
        }
      }
      if (src * 2 + 1 < size) {
        if (EdgeData::has_value) {
          edgeData.set(p.addNeighbor(src, src * 2 + 1), edgeValue.at(0));
          edgeData.set(p.addNeighbor(src * 2 + 1, src), edgeValue.at(0));
        } else {
          p.addNeighbor(src, src * 2 + 1);
          p.addNeighbor(src * 2 + 1, src);
        }
      }
      if (src * 2 + 2 < size) {
        if (EdgeData::has_value) {
          edgeData.set(p.addNeighbor(src, src * 2 + 2), edgeValue.at(0));
          edgeData.set(p.addNeighbor(src * 2 + 2, src), edgeValue.at(0));
        } else {
          p.addNeighbor(src, src * 2 + 2);
          p.addNeighbor(src * 2 + 2, src);
        }
      }
    }

    edge_value_type* rawEdgeData = p.finish<edge_value_type>();
    if (EdgeData::has_value)
      std::copy(edgeData.begin(), edgeData.end(), rawEdgeData);
    p.structureToFile(outfilename);
    printStatus(graph.size(), graph.sizeEdges(), p.size(), p.sizeEdges());
  }
};

//! Make graph symmetric by blindly adding reverse entries
struct MakeSymmetric: public Conversion {
  template<typename EdgeTy>
  void convert(const std::string& infilename, const std::string& outfilename) {
    typedef Galois::Graph::FileGraph Graph;

    Graph ingraph;
    Graph outgraph;
    ingraph.structureFromFile(infilename);
    Galois::Graph::makeSymmetric<EdgeTy>(ingraph, outgraph);

    outgraph.structureToFile(outfilename);
    printStatus(ingraph.size(), ingraph.sizeEdges(), outgraph.size(), outgraph.sizeEdges());
  }
};

/**
 * Like SortByDegree but (1) take into account bipartite representation splits
 * symmetric relation over two graphs (a graph and its transpose) and (2)
 * normalize representation by placing all nodes from bipartite graph set A
 * before set B.
 */
struct BipartiteSortByDegree: public Conversion {
  template<typename EdgeTy>
  void convert(const std::string& infilename, const std::string& outfilename) {
    typedef Galois::Graph::FileGraph Graph;
    typedef Graph::GraphNode GNode;
    typedef Galois::LargeArray<GNode> Permutation;

    Graph ingraph, outgraph, transposegraph;
    ingraph.structureFromFile(infilename);
    transposegraph.structureFromFile(transposeFilename);

    Permutation perm;
    perm.create(ingraph.size());

    auto hasOutEdge = [&](GNode x) {
      return ingraph.edge_begin(x) != ingraph.edge_end(x);
    };
    ptrdiff_t numSetA = std::count_if(ingraph.begin(), ingraph.end(), hasOutEdge);
    auto getDistance = [&](GNode x) -> ptrdiff_t {
      if (ingraph.edge_begin(x) == ingraph.edge_end(x))
        return numSetA + std::distance(transposegraph.edge_begin(x), transposegraph.edge_end(x));
      else
        return std::distance(ingraph.edge_begin(x), ingraph.edge_end(x));
    };

    std::copy(ingraph.begin(), ingraph.end(), perm.begin());
    std::sort(perm.begin(), perm.end(), [&](GNode lhs, GNode rhs) -> bool {
      return getDistance(lhs) < getDistance(rhs);
    });

    // Finalize by taking the transpose/inverse
    Permutation inverse;
    inverse.create(ingraph.size());
    size_t idx = 0;
    for (auto n : perm) {
      inverse[n] = idx++;
    }

    Galois::Graph::permute<EdgeTy>(ingraph, inverse, outgraph);
    outputPermutation(inverse);
    outgraph.structureToFile(outfilename);
    printStatus(ingraph.size(), ingraph.sizeEdges());
  }
};


struct SortByDegree: public Conversion {
  template<typename EdgeTy>
  void convert(const std::string& infilename, const std::string& outfilename) {
    typedef Galois::Graph::FileGraph Graph;
    typedef Graph::GraphNode GNode;
    typedef Galois::LargeArray<GNode> Permutation;

    Graph ingraph, outgraph;
    ingraph.structureFromFile(infilename);

    Permutation perm;
    perm.create(ingraph.size());

    std::copy(ingraph.begin(), ingraph.end(), perm.begin());
    std::sort(perm.begin(), perm.end(), [&](GNode lhs, GNode rhs) -> bool {
      return std::distance(ingraph.edge_begin(lhs), ingraph.edge_end(lhs)) 
        < std::distance(ingraph.edge_begin(rhs), ingraph.edge_end(rhs));
    });

    // Finalize by taking the transpose/inverse
    Permutation inverse;
    inverse.create(ingraph.size());
    size_t idx = 0;
    for (auto n : perm) {
      inverse[n] = idx++;
    }

    Galois::Graph::permute<EdgeTy>(ingraph, inverse, outgraph);
    outputPermutation(inverse);
    outgraph.structureToFile(outfilename);
    printStatus(ingraph.size(), ingraph.sizeEdges());
  }
};

struct SortByHighDegreeParent: public Conversion {
  template<typename EdgeTy>
  void convert(const std::string& infilename, const std::string& outfilename) {
    typedef Galois::Graph::FileGraph Graph;
    typedef Graph::GraphNode GNode;
    typedef Galois::LargeArray<GNode> Permutation;

    Graph graph;
    graph.structureFromFile(infilename);

    auto sz = graph.size();

    Permutation perm;
    perm.create(sz);
    std::copy(boost::counting_iterator<GNode>(0), boost::counting_iterator<GNode>(sz), perm.begin());

    std::cout << "Done setting up perm\n";

    std::deque<std::deque<std::pair<unsigned, GNode> > > inv(sz);
    unsigned count = 0;
    for (auto ii = graph.begin(), ee = graph.end(); ii != ee; ++ii) {
      if (!(++count % 1024)) std::cerr << static_cast<double>(count * 100) / sz << "\r";
      unsigned dist = std::distance(graph.edge_begin(*ii), graph.edge_end(*ii));
      for (auto dsti = graph.edge_begin(*ii), dste = graph.edge_end(*ii); dsti != dste; ++dsti)
        inv[graph.getEdgeDst(dsti)].push_back(std::make_pair(dist,*ii));
    }

    std::cout << "Found inverse\n";

    count = 0;
    for (auto ii = inv.begin(), ee = inv.end(); ii != ee; ++ii) {
      if (!(++count % 1024)) std::cerr << count << " of " << sz << "\r";
      std::sort(ii->begin(), ii->end(), std::greater<std::pair<unsigned, GNode>>());
    }

    std::sort(perm.begin(), perm.end(), [&inv, &graph](GNode lhs, GNode rhs) -> bool {
        const auto& ll = inv[lhs].begin();
        const auto& el = inv[lhs].end();
        const auto& rr = inv[rhs].begin();
        const auto& er = inv[rhs].begin();
        // not less-than and not equal => greater-than
        return !std::lexicographical_compare(ll, el, rr, er)
          && !(std::distance(ll, el) == std::distance(rr, er) && std::equal(ll, el, rr));
    });

    std::cout << "Done sorting\n";

    Permutation perm2;
    perm2.create(sz);
    for (unsigned x = 0; x < perm.size(); ++x) 
      perm2[perm[x]] = x;

    std::cout << "Done inverting\n";

    for (unsigned x = 0; x < perm2.size(); ++x) {
      if (perm[x] == 0) {
        std::cout << "Zero is at " << x << "\n";
        break;
      }
    }
    std::cout << "Zero is at " << perm2[0] << "\n";

    Graph out;
    Galois::Graph::permute<EdgeTy>(graph, perm2, out);
    outputPermutation(perm2);

    // std::cout << "Biggest was " << first << " now " << perm2[first] << " with "
    //           << std::distance(out.edge_begin(perm2[first]), out.edge_end(perm2[first]))
    //           << "\n";

    out.structureToFile(outfilename);
    printStatus(graph.size(), graph.sizeEdges());
  }
};

struct RemoveHighDegree: public Conversion {
  template<typename EdgeTy>
  void convert(const std::string& infilename, const std::string& outfilename) {
    typedef Galois::Graph::FileGraph Graph;
    typedef Graph::GraphNode GNode;
    typedef Galois::Graph::FileGraphWriter Writer;
    typedef Galois::LargeArray<EdgeTy> EdgeData;
    typedef typename EdgeData::value_type edge_value_type;
    
    Graph graph;
    graph.structureFromFile(infilename);

    Writer p;
    EdgeData edgeData;

    std::vector<GNode> nodeTable;
    nodeTable.resize(graph.size());
    uint64_t numNodes = 0;
    uint64_t numEdges = 0;
    for (Graph::iterator ii = graph.begin(), ei = graph.end(); ii != ei; ++ii) {
      GNode src = *ii;
      Graph::edge_iterator jj = graph.edge_begin(src), ej = graph.edge_end(src);
      if (std::distance(jj, ej) > maxDegree)
        continue;
      nodeTable[src] = numNodes++;
      for (; jj != ej; ++jj) {
        GNode dst = graph.getEdgeDst(jj);
        if (std::distance(graph.edge_begin(dst), graph.edge_end(dst)) > maxDegree)
          continue;
        ++numEdges;
      }
    }

    if (numEdges == graph.sizeEdges() && numNodes == graph.size()) {
      std::cout << "Graph already simplified; copy input to output\n";
      printStatus(graph.size(), graph.sizeEdges());
      graph.structureToFile(outfilename);
      return;
    }

    p.setNumNodes(numNodes);
    p.setNumEdges(numEdges);
    p.setSizeofEdgeData(EdgeData::size_of::value);
    edgeData.create(numEdges);

    p.phase1();
    for (Graph::iterator ii = graph.begin(), ei = graph.end(); ii != ei; ++ii) {
      GNode src = *ii;
      Graph::edge_iterator jj = graph.edge_begin(src), ej = graph.edge_end(src);
      if (std::distance(jj, ej) > maxDegree)
        continue;
      for (; jj != ej; ++jj) {
        GNode dst = graph.getEdgeDst(jj);
        if (std::distance(graph.edge_begin(dst), graph.edge_end(dst)) > maxDegree)
          continue;
        p.incrementDegree(nodeTable[src]);
      }
    }

    p.phase2();
    for (Graph::iterator ii = graph.begin(), ei = graph.end(); ii != ei; ++ii) {
      GNode src = *ii;
      Graph::edge_iterator jj = graph.edge_begin(src), ej = graph.edge_end(src);
      if (std::distance(jj, ej) > maxDegree)
        continue;
      for (; jj != ej; ++jj) {
        GNode dst = graph.getEdgeDst(jj);
        if (std::distance(graph.edge_begin(dst), graph.edge_end(dst)) > maxDegree)
          continue;
        if (EdgeData::has_value) {
          edgeData.set(p.addNeighbor(nodeTable[src], nodeTable[dst]), graph.getEdgeData<edge_value_type>(jj));
        } else {
          p.addNeighbor(nodeTable[src], nodeTable[dst]);
        }
      }
    }

    edge_value_type* rawEdgeData = p.finish<edge_value_type>();
    if (EdgeData::has_value)
      std::copy(edgeData.begin(), edgeData.end(), rawEdgeData);
    
    p.structureToFile(outfilename);
    printStatus(graph.size(), graph.sizeEdges(), p.size(), p.sizeEdges());
  }
};

//! Partition graph into balanced number of edges by source node
struct PartitionBySource: public Conversion {
  template<typename EdgeTy>
  void convert(const std::string& infilename, const std::string& outfilename) {
    typedef Galois::Graph::FileGraph Graph;
    typedef Graph::GraphNode GNode;
    typedef Galois::Graph::FileGraphWriter Writer;
    typedef Galois::LargeArray<EdgeTy> EdgeData;
    typedef typename EdgeData::value_type edge_value_type;
    
    Graph graph;
    graph.structureFromFile(infilename);

    for (int i = 0; i < numParts; ++i) {
      Writer p;
      EdgeData edgeData;

      auto r = graph.divideBy(0, 1, i, numParts);

      size_t numEdges = 0;
      if (r.first != r.second)
        numEdges = std::distance(graph.edge_begin(*r.first), graph.edge_end(*(r.second - 1)));

      p.setNumNodes(graph.size());
      p.setNumEdges(numEdges);
      p.setSizeofEdgeData(EdgeData::size_of::value);
      edgeData.create(numEdges);

      p.phase1();
      for (Graph::iterator ii = r.first, ei = r.second; ii != ei; ++ii) {
        GNode src = *ii;
        p.incrementDegree(src, std::distance(graph.edge_begin(src), graph.edge_end(src)));
      }
      
      p.phase2();
      for (Graph::iterator ii = r.first, ei = r.second; ii != ei; ++ii) {
        GNode src = *ii;
        for (Graph::edge_iterator jj = graph.edge_begin(src), ej = graph.edge_end(src); jj != ej; ++jj) {
          GNode dst = graph.getEdgeDst(jj);
          if (EdgeData::has_value)
            edgeData.set(p.addNeighbor(src, dst), graph.getEdgeData<edge_value_type>(jj));
          else
            p.addNeighbor(src, dst);
        }
      }

      edge_value_type* rawEdgeData = p.finish<edge_value_type>();
      if (EdgeData::has_value)
        std::copy(edgeData.begin(), edgeData.end(), rawEdgeData);

      std::ostringstream partname;
      partname << outfilename << "." << i << ".of." << numParts;

      p.structureToFile(partname.str());
      printStatus(graph.size(), graph.sizeEdges(), p.size(), p.sizeEdges());
    }
  }
};

template<typename InDegree, typename It = typename InDegree::iterator>
static std::pair<It,It> divide_by_destination(InDegree& inDegree, int id, int total) 
{
  if (inDegree.begin() == inDegree.end())
    return std::make_pair(inDegree.begin(), inDegree.end());

  size_t size = inDegree[inDegree.size() - 1];
  size_t block = (size + total - 1) / total;

  It bb = std::lower_bound(inDegree.begin(), inDegree.end(), id * block);
  It eb;
  if (id + 1 == total)
    eb = inDegree.end();
  else 
    eb = std::upper_bound(bb, inDegree.end(), (id + 1) * block);
  return std::make_pair(bb, eb);
}

template<typename GraphTy, typename InDegree>
static void compute_indegree(GraphTy& graph, InDegree& inDegree) {
  inDegree.create(graph.size());

  for (auto nn = graph.begin(), en = graph.end(); nn != en; ++nn) {
    for (auto jj = graph.edge_begin(*nn), ej = graph.edge_end(*nn); jj != ej; ++jj) {
      auto dst = graph.getEdgeDst(jj);
      inDegree[dst] += 1;
    }
  }

  for (size_t i = 1; i < inDegree.size(); ++i)
    inDegree[i] = inDegree[i-1] + inDegree[i];
}

//! Partition graph into balanced number of edges by destination node
struct PartitionByDestination: public Conversion {
  template<typename EdgeTy>
  void convert(const std::string& infilename, const std::string& outfilename) {
    typedef Galois::Graph::FileGraph Graph;
    typedef Graph::GraphNode GNode;
    typedef Galois::Graph::FileGraphWriter Writer;
    typedef Galois::LargeArray<EdgeTy> EdgeData;
    typedef Galois::LargeArray<size_t> InDegree;
    typedef typename EdgeData::value_type edge_value_type;
    
    Graph graph;
    graph.structureFromFile(infilename);
    InDegree inDegree;
    compute_indegree(graph, inDegree);

    for (int i = 0; i < numParts; ++i) {
      Writer p;
      EdgeData edgeData;

      auto r = divide_by_destination(inDegree, i, numParts);
      size_t bb = std::distance(inDegree.begin(), r.first);
      size_t eb = std::distance(inDegree.begin(), r.second);

      size_t numEdges = 0;
      if (bb != eb) {
        size_t begin = bb == 0 ? 0 : inDegree[bb - 1];
        size_t end = eb == 0 ? 0 : inDegree[eb - 1];
        numEdges = end - begin;
      }

      p.setNumNodes(graph.size());
      p.setNumEdges(numEdges);
      p.setSizeofEdgeData(EdgeData::size_of::value);
      edgeData.create(numEdges);

      p.phase1();
      for (Graph::iterator ii = graph.begin(), ei = graph.end(); ii != ei; ++ii) {
        GNode src = *ii;
        for (Graph::edge_iterator jj = graph.edge_begin(src), ej = graph.edge_end(src); jj != ej; ++jj) {
          GNode dst = graph.getEdgeDst(jj);
          if (dst < bb)
            continue;
          if (dst >= eb)
            continue;
          p.incrementDegree(src);
        }
      }
      
      p.phase2();
      for (Graph::iterator ii = graph.begin(), ei = graph.end(); ii != ei; ++ii) {
        GNode src = *ii;
        for (Graph::edge_iterator jj = graph.edge_begin(src), ej = graph.edge_end(src); jj != ej; ++jj) {
          GNode dst = graph.getEdgeDst(jj);
          if (dst < bb)
            continue;
          if (dst >= eb)
            continue;
          if (EdgeData::has_value)
            edgeData.set(p.addNeighbor(src, dst), graph.getEdgeData<edge_value_type>(jj));
          else
            p.addNeighbor(src, dst);
        }
      }

      edge_value_type* rawEdgeData = p.finish<edge_value_type>();
      if (EdgeData::has_value)
        std::copy(edgeData.begin(), edgeData.end(), rawEdgeData);

      std::ostringstream partname;
      partname << outfilename << "." << i << ".of." << numParts;

      p.structureToFile(partname.str());
      printStatus(graph.size(), graph.sizeEdges(), p.size(), p.sizeEdges());
    }
  }
};

//! Transpose graph
struct Transpose: public Conversion {
  template<typename EdgeTy>
  void convert(const std::string& infilename, const std::string& outfilename) {
    typedef Galois::Graph::FileGraph Graph;
    typedef Graph::GraphNode GNode;
    typedef Galois::Graph::FileGraphWriter Writer;
    typedef Galois::LargeArray<EdgeTy> EdgeData;
    typedef typename EdgeData::value_type edge_value_type;
    
    Graph graph;
    graph.structureFromFile(infilename);

    Writer p;
    EdgeData edgeData;

    p.setNumNodes(graph.size());
    p.setNumEdges(graph.sizeEdges());
    p.setSizeofEdgeData(EdgeData::size_of::value);
    edgeData.create(graph.sizeEdges());

    p.phase1();
    for (Graph::iterator ii = graph.begin(), ei = graph.end(); ii != ei; ++ii) {
      GNode src = *ii;

      for (Graph::edge_iterator jj = graph.edge_begin(src), ej = graph.edge_end(src); jj != ej; ++jj) {
        GNode dst = graph.getEdgeDst(jj);
        p.incrementDegree(dst);
      }
    }

    p.phase2();
    for (Graph::iterator ii = graph.begin(), ei = graph.end(); ii != ei; ++ii) {
      GNode src = *ii;

      for (Graph::edge_iterator jj = graph.edge_begin(src), ej = graph.edge_end(src); jj != ej; ++jj) {
        GNode dst = graph.getEdgeDst(jj);
        if (EdgeData::has_value) {
          edgeData.set(p.addNeighbor(dst, src), graph.getEdgeData<edge_value_type>(jj));
        } else {
          p.addNeighbor(dst, src);
        }
      }
    }

    edge_value_type* rawEdgeData = p.finish<edge_value_type>();
    if (EdgeData::has_value)
      std::copy(edgeData.begin(), edgeData.end(), rawEdgeData);
    
    p.structureToFile(outfilename);
    printStatus(graph.size(), graph.sizeEdges(), p.size(), p.sizeEdges());
  }
};

template<typename GraphNode,typename EdgeTy>
struct IdLess {
  bool operator()(const Galois::Graph::EdgeSortValue<GraphNode,EdgeTy>& e1, const Galois::Graph::EdgeSortValue<GraphNode,EdgeTy>& e2) const {
    return e1.dst < e2.dst;
  }
};

template<typename GraphNode,typename EdgeTy>
struct WeightLess {
  bool operator()(const Galois::Graph::EdgeSortValue<GraphNode,EdgeTy>& e1, const Galois::Graph::EdgeSortValue<GraphNode,EdgeTy>& e2) const {
    return e1.get() < e2.get();
  }
};

/**
 * Removes self and multi-edges from a graph.
 */
struct Cleanup: public Conversion {
  template<typename EdgeTy>
  void convert(const std::string& infilename, const std::string& outfilename) {
    typedef Galois::Graph::FileGraph Graph;
    typedef Graph::GraphNode GNode;
    
    Graph orig, graph;
    {
      // Original FileGraph is immutable because it is backed by a file
      orig.structureFromFile(infilename);
      graph.cloneFrom(orig);
    }

    size_t numEdges = 0;

    for (Graph::iterator ii = graph.begin(), ei = graph.end(); ii != ei; ++ii) {
      GNode src = *ii;

      graph.sortEdges<EdgeTy>(src, IdLess<GNode,EdgeTy>());

      Graph::edge_iterator prev = graph.edge_end(src);
      for (Graph::edge_iterator jj = graph.edge_begin(src), ej = graph.edge_end(src); jj != ej; ++jj) {
        GNode dst = graph.getEdgeDst(jj);
        if (src == dst) {
        } else if (prev != ej && graph.getEdgeDst(prev) == dst) {
        } else {
          numEdges += 1;
        }
        prev = jj;
      }
    }

    if (numEdges == graph.sizeEdges()) {
      std::cout << "Graph already simplified; copy input to output\n";
      printStatus(graph.size(), graph.sizeEdges());
      graph.structureToFile(outfilename);
      return;
    }

    typedef Galois::Graph::FileGraphWriter Writer;
    typedef Galois::LargeArray<EdgeTy> EdgeData;
    typedef typename EdgeData::value_type edge_value_type;
    
    Writer p;
    EdgeData edgeData;

    p.setNumNodes(graph.size());
    p.setNumEdges(numEdges);
    p.setSizeofEdgeData(EdgeData::size_of::value);
    edgeData.create(numEdges);

    p.phase1();
    for (Graph::iterator ii = graph.begin(), ei = graph.end(); ii != ei; ++ii) {
      GNode src = *ii;

      Graph::edge_iterator prev = graph.edge_end(src);
      for (Graph::edge_iterator jj = graph.edge_begin(src), ej = graph.edge_end(src); jj != ej; ++jj) {
        GNode dst = graph.getEdgeDst(jj);
        if (src == dst) {
        } else if (prev != ej && graph.getEdgeDst(prev) == dst) {
        } else {
          p.incrementDegree(src);
        }
        prev = jj;
      }
    }

    p.phase2();
    for (Graph::iterator ii = graph.begin(), ei = graph.end(); ii != ei; ++ii) {
      GNode src = *ii;

      Graph::edge_iterator prev = graph.edge_end(src);
      for (Graph::edge_iterator jj = graph.edge_begin(src), ej = graph.edge_end(src); jj != ej; ++jj) {
        GNode dst = graph.getEdgeDst(jj);
        if (src == dst) {
        } else if (prev != ej && graph.getEdgeDst(prev) == dst) {
        } else if (EdgeData::has_value) {
          edgeData.set(p.addNeighbor(src, dst), graph.getEdgeData<edge_value_type>(jj));
        } else {
          p.addNeighbor(src, dst);
        }
        prev = jj;
      }
    }

    edge_value_type* rawEdgeData = p.finish<edge_value_type>();
    if (EdgeData::has_value)
      std::copy(edgeData.begin(), edgeData.end(), rawEdgeData);
    
    p.structureToFile(outfilename);
    printStatus(graph.size(), graph.sizeEdges(), p.size(), p.sizeEdges());
  }
};

template<template<typename,typename> class SortBy, bool NeedsEdgeData>
struct SortEdges: public boost::mpl::if_c<NeedsEdgeData, HasNoVoidSpecialization, Conversion>::type {
  template<typename EdgeTy>
  void convert(const std::string& infilename, const std::string& outfilename) {
    typedef Galois::Graph::FileGraph Graph;
    typedef Graph::GraphNode GNode;
    typedef Galois::Graph::FileGraphWriter Writer;
    typedef Galois::LargeArray<EdgeTy> EdgeData;
    typedef typename EdgeData::value_type edge_value_type;
    
    Graph orig, graph;
    {
      // Original FileGraph is immutable because it is backed by a file
      orig.structureFromFile(infilename);
      graph.cloneFrom(orig);
    }

    for (typename Graph::iterator ii = graph.begin(), ei = graph.end(); ii != ei; ++ii) {
      GNode src = *ii;

      graph.sortEdges<EdgeTy>(src, SortBy<GNode,EdgeTy>());
    }

    graph.structureToFile(outfilename);
    printStatus(graph.size(), graph.sizeEdges());
  }
};

/**
 * Removes edges such that src > dst
 */
struct MakeUnsymmetric: public Conversion {
  template<typename EdgeTy>
  void convert(const std::string& infilename, const std::string& outfilename) {
    typedef Galois::Graph::FileGraph Graph;
    typedef Graph::GraphNode GNode;
    
    Graph graph;
    graph.structureFromFile(infilename);

    size_t numEdges = 0;

    for (Graph::iterator ii = graph.begin(), ei = graph.end(); ii != ei; ++ii) {
      GNode src = *ii;

      for (Graph::edge_iterator jj = graph.edge_begin(src), ej = graph.edge_end(src); jj != ej; ++jj) {
        GNode dst = graph.getEdgeDst(jj);
        if (src > dst) {
        } else {
          numEdges += 1;
        }
      }
    }

    if (numEdges == graph.sizeEdges()) {
      std::cout << "Graph already simplified; copy input to output\n";
      printStatus(graph.size(), graph.sizeEdges());
      graph.structureToFile(outfilename);
      return;
    }

    typedef Galois::Graph::FileGraphWriter Writer;
    typedef Galois::LargeArray<EdgeTy> EdgeData;
    typedef typename EdgeData::value_type edge_value_type;
    
    Writer p;
    EdgeData edgeData;

    p.setNumNodes(graph.size());
    p.setNumEdges(numEdges);
    p.setSizeofEdgeData(EdgeData::size_of::value);
    edgeData.create(numEdges);

    p.phase1();
    for (Graph::iterator ii = graph.begin(), ei = graph.end(); ii != ei; ++ii) {
      GNode src = *ii;

      for (Graph::edge_iterator jj = graph.edge_begin(src), ej = graph.edge_end(src); jj != ej; ++jj) {
        GNode dst = graph.getEdgeDst(jj);
        if (src > dst) {
        } else {
          p.incrementDegree(src);
        }
      }
    }

    p.phase2();
    for (Graph::iterator ii = graph.begin(), ei = graph.end(); ii != ei; ++ii) {
      GNode src = *ii;

      for (Graph::edge_iterator jj = graph.edge_begin(src), ej = graph.edge_end(src); jj != ej; ++jj) {
        GNode dst = graph.getEdgeDst(jj);
        if (src > dst) {
        } else if (EdgeData::has_value) {
          edgeData.set(p.addNeighbor(src, dst), graph.getEdgeData<edge_value_type>(jj));
        } else {
          p.addNeighbor(src, dst);
        }
      }
    }

    edge_value_type* rawEdgeData = p.finish<edge_value_type>();
    if (EdgeData::has_value)
      std::copy(edgeData.begin(), edgeData.end(), rawEdgeData);
    
    p.structureToFile(outfilename);
    printStatus(graph.size(), graph.sizeEdges(), p.size(), p.sizeEdges());
  }
};

// Example:
//  c Some file
//  c Comments
//  p XXX* <num nodes> <num edges>
//  a <src id> <dst id> <weight>
//  ....
struct Dimacs2Gr: public HasNoVoidSpecialization {
  template<typename EdgeTy>
  void convert(const std::string& infilename, const std::string& outfilename) {
    typedef Galois::Graph::FileGraphWriter Writer;
    typedef Galois::LargeArray<EdgeTy> EdgeData;
    typedef typename EdgeData::value_type edge_value_type;

    Writer p;
    EdgeData edgeData;
    uint32_t nnodes;
    size_t nedges;

    for (int phase = 0; phase < 2; ++phase) {
      std::ifstream infile(infilename.c_str());

      // Skip comments
      while (infile) {
        if (infile.peek() == 'p') {
          break;
        }
        infile.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
      }

      // Read header
      char header[256];
      infile.getline(header, 256, '\n');
      std::istringstream line(header, std::istringstream::in);
      std::vector<std::string> tokens;
      while (line) {
        std::string tmp;
        line >> tmp;
        if (line) {
          tokens.push_back(tmp);
        }
      }
      if (tokens.size() < 3 || tokens[0].compare("p") != 0) {
        GALOIS_DIE("Unknown problem specification line: ", line.str());
      }
      // Prefer C functions for maximum compatibility
      //nnodes = std::stoull(tokens[tokens.size() - 2]);
      //nedges = std::stoull(tokens[tokens.size() - 1]);
      nnodes = strtoull(tokens[tokens.size() - 2].c_str(), NULL, 0);
      nedges = strtoull(tokens[tokens.size() - 1].c_str(), NULL, 0);

      // Parse edges
      if (phase == 0) {
        p.setNumNodes(nnodes);
        p.setNumEdges(nedges);
        p.setSizeofEdgeData(EdgeData::size_of::value);
        edgeData.create(nedges);
        p.phase1();
      } else {
        p.phase2();
      }

      for (size_t edge_num = 0; edge_num < nedges; ++edge_num) {
        uint32_t cur_id, neighbor_id;
        int32_t weight;
        std::string tmp;
        infile >> tmp;

        if (tmp.compare("a") != 0) {
          --edge_num;
          infile.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
          continue;
        }

        infile >> cur_id >> neighbor_id >> weight;
        if (cur_id == 0 || cur_id > nnodes) {
          GALOIS_DIE("Error: node id out of range: ", cur_id);
        }
        if (neighbor_id == 0 || neighbor_id > nnodes) {
          GALOIS_DIE("Error: neighbor id out of range: ", neighbor_id);
        }

        // 1 indexed
        if (phase == 0) {
          p.incrementDegree(cur_id - 1);
        } else {
          edgeData.set(p.addNeighbor(cur_id - 1, neighbor_id - 1), weight);
        }

        infile.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
      }

      infile.peek();
      if (!infile.eof()) {
        GALOIS_DIE("Error: additional lines in file");
      }
    }

    edge_value_type* rawEdgeData = p.finish<edge_value_type>();
    if (EdgeData::has_value)
      std::copy(edgeData.begin(), edgeData.end(), rawEdgeData);

    p.structureToFile(outfilename);
    printStatus(p.size(), p.sizeEdges());
  }
};

/**
 * PBBS input is an ASCII file of tokens that serialize a CSR graph. I.e., 
 * elements in brackets are non-literals:
 * 
 * AdjacencyGraph
 * <num nodes>
 * <num edges>
 * <offset node 0>
 * <offset node 1>
 * ...
 * <edge 0>
 * <edge 1>
 * ...
 */
struct Pbbs2Gr: public HasOnlyVoidSpecialization {
  template<typename EdgeTy>
  void convert(const std::string& infilename, const std::string& outfilename) {
    static_assert(std::is_same<EdgeTy,void>::value, "conversion undefined for non-void graphs");
    typedef Galois::Graph::FileGraphWriter Writer;
    
    Writer p;

    std::ifstream infile(infilename.c_str());
    std::string header;
    uint32_t nnodes;
    size_t nedges;

    infile >> header >> nnodes >> nedges;
    if (header != "AdjacencyGraph") {
      GALOIS_DIE("Error: unknown file format");
    }

    p.setNumNodes(nnodes);
    p.setNumEdges(nedges);

    size_t* offsets = new size_t[nnodes];
    for (size_t i = 0; i < nnodes; ++i) {
      infile >> offsets[i];
    }

    size_t* edges = new size_t[nedges];
    for (size_t i = 0; i < nedges; ++i) {
      infile >> edges[i];
    }

    p.phase1();
    for (uint32_t i = 0; i < nnodes; ++i) {
      size_t begin = offsets[i];
      size_t end = (i == nnodes - 1) ? nedges : offsets[i+1];
      p.incrementDegree(i, end - begin);
    }

    p.phase2();
    for (uint32_t i = 0; i < nnodes; ++i) {
      size_t begin = offsets[i];
      size_t end = (i == nnodes - 1) ? nedges : offsets[i+1];
      for (size_t j = begin; j < end; ++j) {
        size_t dst = edges[j];
        p.addNeighbor(i, dst);
      }
    }

    p.finish<void>();

    p.structureToFile(outfilename);
    printStatus(p.size(), p.sizeEdges());
  }
};

struct Gr2Pbbsedges: public HasNoVoidSpecialization {
  template<typename EdgeTy>
  void convert(const std::string& infilename, const std::string& outfilename) {
    // Use FileGraph because it is basically in CSR format needed for pbbs
    typedef Galois::Graph::FileGraph Graph;
    typedef Graph::GraphNode GNode;

    Graph graph;
    graph.structureFromFile(infilename);

    std::ofstream file(outfilename.c_str());
    file << "WeightedEdgeArray\n";
    for (Graph::iterator ii = graph.begin(), ei = graph.end(); ii != ei; ++ii) {
      GNode src = *ii;
      for (Graph::edge_iterator jj = graph.edge_begin(src), ej = graph.edge_end(src); jj != ej; ++jj) {
        GNode dst = graph.getEdgeDst(jj);
        EdgeTy& weight = graph.getEdgeData<EdgeTy>(jj);
        file << src << " " << dst << " " << weight << "\n";
      }
    }
    file.close();

    printStatus(graph.size(), graph.sizeEdges());
  }
};

/**
 * PBBS input is an ASCII file of tokens that serialize a CSR graph. I.e., 
 * elements in brackets are non-literals:
 * 
 * [Weighted]AdjacencyGraph
 * <num nodes>
 * <num edges>
 * <offset node 0>
 * <offset node 1>
 * ...
 * <edge 0>
 * <edge 1>
 * ...
 * [
 * <edge weight 0>
 * <edge weight 1>
 * ...
 * ]
 */
struct Gr2Pbbs: public Conversion {
  template<typename EdgeTy>
  void convert(const std::string& infilename, const std::string& outfilename) {
    typedef Galois::Graph::FileGraph Graph;
    typedef Galois::LargeArray<EdgeTy> EdgeData;
    typedef typename EdgeData::value_type edge_value_type;

    Graph graph;
    graph.structureFromFile(infilename);

    std::ofstream file(outfilename.c_str());
    if (EdgeData::has_value)
      file << "Weighted";
    file << "AdjacencyGraph\n" << graph.size() << "\n" << graph.sizeEdges() << "\n";
    // edgeid[i] is the end of i in FileGraph while it is the beginning of i in pbbs graph
    size_t last = std::distance(graph.edge_id_begin(), graph.edge_id_end());
    size_t count = 0;
    file << "0\n";
    for (Graph::edge_id_iterator ii = graph.edge_id_begin(), ei = graph.edge_id_end();
        ii != ei; ++ii, ++count) {
      if (count < last - 1)
        file << *ii << "\n";
    }
    for (Graph::node_id_iterator ii = graph.node_id_begin(), ei = graph.node_id_end(); ii != ei; ++ii) {
      file << *ii << "\n";
    }
    if (EdgeData::has_value) {
      for (edge_value_type* ii = graph.edge_data_begin<edge_value_type>(), *ei = graph.edge_data_end<edge_value_type>();
          ii != ei; ++ii) {
        file << *ii << "\n";
      }
    }
    file.close();

    printStatus(graph.size(), graph.sizeEdges());
  }
};

/**
 * Binary PBBS format is three files.
 *
 * <base>.config - ASCII file with number of vertices
 * <base>.adj - Binary adjacencies
 * <base>.idx - Binary offsets for adjacencies
 */
template<typename NodeIdx,typename Offset>
struct Gr2BinaryPbbs: public HasOnlyVoidSpecialization {
  template<typename EdgeTy>
  void convert(const std::string& infilename, const std::string& outfilename) {
    static_assert(std::is_same<EdgeTy,void>::value, "conversion undefined for non-void graphs");
    typedef Galois::Graph::FileGraph Graph;
    typedef Graph::GraphNode GNode;

    Graph graph;
    graph.structureFromFile(infilename);

    {
      std::string configName = outfilename + ".config";
      std::ofstream configFile(configName.c_str());
      configFile << graph.size() << "\n";
    }

    {
      std::string idxName = outfilename + ".idx";
      std::ofstream idxFile(idxName.c_str());
      // edgeid[i] is the end of i in FileGraph while it is the beginning of i in pbbs graph
      size_t last = std::distance(graph.edge_id_begin(), graph.edge_id_end());
      size_t count = 0;
      Offset offset = 0;
      idxFile.write(reinterpret_cast<char*>(&offset), sizeof(offset));
      for (Graph::edge_id_iterator ii = graph.edge_id_begin(), ei = graph.edge_id_end(); ii != ei; ++ii, ++count) {
        offset = *ii;
        if (count < last - 1)
          idxFile.write(reinterpret_cast<char*>(&offset), sizeof(offset));
      }
      idxFile.close();
    }

    {
      std::string adjName = outfilename + ".adj";
      std::ofstream adjFile(adjName.c_str());
      for (Graph::node_id_iterator ii = graph.node_id_begin(), ei = graph.node_id_end(); ii != ei; ++ii) {
        NodeIdx nodeIdx = *ii;
        adjFile.write(reinterpret_cast<char*>(&nodeIdx), sizeof(nodeIdx));
      }
      adjFile.close();
    }

    printStatus(graph.size(), graph.sizeEdges());
  }
};

struct Gr2Dimacs: public HasNoVoidSpecialization {
  template<typename EdgeTy>
  void convert(const std::string& infilename, const std::string& outfilename) {
    typedef Galois::Graph::FileGraph Graph;
    typedef Graph::GraphNode GNode;

    Graph graph;
    graph.structureFromFile(infilename);

    std::ofstream file(outfilename.c_str());
    file << "p sp " << graph.size() << " " << graph.sizeEdges() << "\n";
    for (Graph::iterator ii = graph.begin(), ei = graph.end(); ii != ei; ++ii) {
      GNode src = *ii;
      for (Graph::edge_iterator jj = graph.edge_begin(src), ej = graph.edge_end(src); jj != ej; ++jj) {
        GNode dst = graph.getEdgeDst(jj);
        EdgeTy& weight = graph.getEdgeData<EdgeTy>(jj);
        file << "a " << src + 1 << " " << dst + 1 << " " << weight << "\n";
      }
    }
    file.close();

    printStatus(graph.size(), graph.sizeEdges());
  }
};

/**
 * RMAT format (zero indexed):
 *  %%% Comment1
 *  %%% Comment2
 *  %%% Comment3
 *  <num nodes> <num edges>
 *  <node id> <num edges> [<neighbor id> <neighbor weight>]*
 *  ...
 */
template<typename OutEdgeTy>
struct Gr2Rmat: public HasNoVoidSpecialization {
  template<typename InEdgeTy>
  void convert(const std::string& infilename, const std::string& outfilename) { 
    typedef Galois::Graph::FileGraph Graph;
    typedef Graph::GraphNode GNode;

    Graph graph;
    graph.structureFromFile(infilename);

    std::ofstream file(outfilename.c_str());
    file << "%%%\n";
    file << "%%%\n";
    file << "%%%\n";
    file << graph.size() << " " << graph.sizeEdges() << "\n";
    for (Graph::iterator ii = graph.begin(), ei = graph.end(); ii != ei; ++ii) {
      GNode src = *ii;
      file << *ii << " " << std::distance(graph.edge_begin(src), graph.edge_end(src));
      for (Graph::edge_iterator jj = graph.edge_begin(src), ej = graph.edge_end(src); jj != ej; ++jj) {
        GNode dst = graph.getEdgeDst(jj);
        OutEdgeTy weight = graph.getEdgeData<InEdgeTy>(jj);
        file << " " << dst << " " << weight;
      }
      file << "\n";
    }
    file.close();

    printStatus(graph.size(), graph.sizeEdges());
  }
};

/**
 * GR to Binary Sparse MATLAB matrix.
 * [i, j, v] = find(A); 
 * fwrite(f, size(A,1), 'uint32');
 * fwrite(f, size(A,2), 'uint32');
 * fwrite(f, nnz(A), 'uint32');
 * fwrite(f, (i-1), 'uint32');     % zero-indexed
 * fwrite(f, (j-1), 'uint32'); 
 * fwrite(f, v, 'double');
 */
struct Gr2Bsml: public Conversion {
  template<typename EdgeTy>
  void convert(const std::string& infilename, const std::string& outfilename) { 
    typedef Galois::Graph::FileGraph Graph;
    typedef typename Graph::GraphNode GNode;
    typedef typename Galois::LargeArray<EdgeTy> EdgeData;

    Graph graph;
    graph.structureFromFile(infilename);

    uint32_t nnodes = graph.size();
    uint32_t nedges = graph.sizeEdges(); 

    std::ofstream file(outfilename.c_str());

    // Write header
    file.write(reinterpret_cast<char*>(&nnodes), sizeof(nnodes));
    file.write(reinterpret_cast<char*>(&nnodes), sizeof(nnodes));
    file.write(reinterpret_cast<char*>(&nedges), sizeof(nedges));

    // Write row adjacency
    for (typename Graph::iterator ii = graph.begin(), ei = graph.end(); ii != ei; ++ii) {
      GNode src = *ii;
      uint32_t sid = src;
      for (typename Graph::edge_iterator jj = graph.edge_begin(src), ej = graph.edge_end(src); jj != ej; ++jj) {
        file.write(reinterpret_cast<char *>(&sid), sizeof(sid));
      }
    }

    // Write column adjacency
    for (typename Graph::iterator ii = graph.begin(), ei = graph.end(); ii != ei; ++ii) {
      GNode src = *ii;
      for (typename Graph::edge_iterator jj = graph.edge_begin(src), ej = graph.edge_end(src); jj != ej; ++jj) {
        GNode dst = graph.getEdgeDst(jj);
        uint32_t did = dst;
        file.write(reinterpret_cast<char *>(&did), sizeof(did));
      }
    }

    // Write data
    for (typename Graph::iterator ii = graph.begin(), ei = graph.end(); ii != ei; ++ii) {
      GNode src = *ii;
      for (typename Graph::edge_iterator jj = graph.edge_begin(src), ej = graph.edge_end(src); jj != ej; ++jj) {
        double weight = static_cast<double>(getEdgeValue<EdgeTy,EdgeData::has_value>(graph, jj));
        file.write(reinterpret_cast<char *>(&weight), sizeof(weight));
      }
    }

    file.close();
    printStatus(nnodes, nedges);
  }
};

// TODO: retest which conversions don't work with xlc
#if !defined(__IBMCPP__) || __IBMCPP__ > 1210
#endif

int main(int argc, char** argv) {
  llvm::cl::ParseCommandLineOptions(argc, argv);
  std::ios_base::sync_with_stdio(false);
  switch (convertMode) {
    case bipartitegr2bigpetsc: convert<Bipartitegr2Petsc<double,false> >(); break;
    case bipartitegr2littlepetsc: convert<Bipartitegr2Petsc<double,true> >(); break;
    case bipartitegr2sorteddegreegr: convert<BipartiteSortByDegree>(); break;
    case dimacs2gr: convert<Dimacs2Gr>(); break;
    case edgelist2gr: convert<Edgelist2Gr>(); break;
    case gr2binarypbbs32: convert<Gr2BinaryPbbs<uint32_t,uint32_t> >(); break;
    case gr2binarypbbs64: convert<Gr2BinaryPbbs<uint32_t,uint64_t> >(); break;
    case gr2bsml: convert<Gr2Bsml>(); break;
    case gr2cgr: convert<Cleanup>(); break;
    case gr2dimacs: convert<Gr2Dimacs>(); break;
    case gr2edgelist: convert<Gr2Edgelist>(); break;
    case gr2lowdegreegr: convert<RemoveHighDegree>(); break;
    case gr2mtx: convert<Gr2Mtx>(); break;
    case gr2partdstgr: convert<PartitionByDestination>(); break;
    case gr2partsrcgr: convert<PartitionBySource>(); break;
    case gr2pbbs: convert<Gr2Pbbs>(); break;
    case gr2pbbsedges: convert<Gr2Pbbsedges>(); break;
    case gr2randgr: convert<RandomizeNodes>(); break;
    case gr2randomweightgr: convert<RandomizeEdgeWeights>(); break;
    case gr2ringgr: convert<AddRing>(); break;
    case gr2rmat: convert<Gr2Rmat<int32_t> >(); break;
    case gr2sgr: convert<MakeSymmetric>(); break;
    case gr2sorteddegreegr: convert<SortByDegree>(); break;
    case gr2sorteddstgr: convert<SortEdges<IdLess, false> >(); break; 
    case gr2sortedparentdegreegr: convert<SortByHighDegreeParent>(); break;
    case gr2sortedweightgr: convert<SortEdges<WeightLess, true> >(); break;
    case gr2tgr: convert<Transpose>(); break;
    case gr2treegr: convert<AddTree>(); break; 
    case gr2trigr: convert<MakeUnsymmetric>(); break;
    case mtx2gr: convert<Mtx2Gr>(); break;
    case nodelist2gr: convert<Nodelist2Gr>(); break;
    case pbbs2gr: convert<Pbbs2Gr>(); break;
    default: abort();
  }
  return 0;
}
