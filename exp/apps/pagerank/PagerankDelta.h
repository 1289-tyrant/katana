/** Page rank application -*- C++ -*-
 * @file
 * @section License
 *
 * Galois, a framework to exploit amorphous data-parallelism in irregular
 * programs.
 *
 * Copyright (C) 2011, The University of Texas at Austin. All rights reserved.
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
 */

// Basic Pagerank algorithm that uses a worklist (without priorities for now).

struct PagerankDelta {
  struct LNode {
    float value;
    unsigned int nout;
    LNode(): value(1.0), nout(0) {}
    float getPageRank() { return value; }
  };

  typedef typename Galois::Graph::LC_CSR_Graph<LNode,void>
    ::with_numa_alloc<true>::type
//    ::with_no_lockable<true>::type
    InnerGraph;
  typedef Galois::Graph::LC_InOut_Graph<InnerGraph> Graph;
  typedef typename Graph::GraphNode GNode;

  std::string name() const { return "PagerankDelta"; }

  void readGraph(Graph& graph) {
    Galois::Graph::readGraph(graph, filename); 
  }

  struct Initialize {
    Graph& g;
    Initialize(Graph& g): g(g) { }
    void operator()(Graph::GraphNode n) const {
      LNode& data = g.getData(n, Galois::MethodFlag::UNPROTECTED);
      data.value = 1.0;
      int outs = std::distance(g.edge_begin(n, Galois::MethodFlag::UNPROTECTED), g.edge_end(n, Galois::MethodFlag::UNPROTECTED));
      data.nout = outs;
    }
  };
  
  struct Process {
    PagerankDelta* self;
    Graph& graph;
     
    Process(PagerankDelta* s, Graph& g): self(s), graph(g) { }

    void operator()(const GNode& src, Galois::UserContext<GNode>& ctx) {
      double sum = 0;
      for (auto jj = graph.in_edge_begin(src, Galois::MethodFlag::UNPROTECTED), ej = graph.in_edge_end(src, Galois::MethodFlag::UNPROTECTED); jj != ej; ++jj) {
        GNode dst = graph.getInEdgeDst(jj);
        LNode& ddata = graph.getData(dst, Galois::MethodFlag::WRITE_INTENT);
        sum += ddata.value / ddata.nout;
      }
      float value = (1.0 - alpha) * sum + alpha;
      LNode& sdata = graph.getData(src, Galois::MethodFlag::WRITE_INTENT);
      float diff = std::fabs(value - sdata.value);
      if (diff > tolerance) {
        sdata.value = value;
        for (auto jj = graph.edge_begin(src, Galois::MethodFlag::UNPROTECTED), ej = graph.edge_end(src, Galois::MethodFlag::UNPROTECTED); jj != ej; ++jj) {
          GNode dst = graph.getEdgeDst(jj);
          ctx.push(dst);
        }
      } 
    }
  };

  void operator()(Graph& graph) {
    typedef Galois::WorkList::dChunkedFIFO<512> WL;
    Galois::for_each_local(graph, Process(this, graph), Galois::wl<WL>());
  }
};


