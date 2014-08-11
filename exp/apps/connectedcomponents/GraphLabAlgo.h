#ifndef APPS_CONNECTEDCOMPONENTS_GRAPHLABALGO_H
#define APPS_CONNECTEDCOMPONENTS_GRAPHLABALGO_H

#include "Galois/DomainSpecificExecutors.h"
#include "Galois/Graph/OCGraph.h"
#include "Galois/Graph/LCGraph.h"
#include "Galois/Graph/GraphNodeBag.h"

#include <boost/mpl/if.hpp>

template<typename Graph>
void readInOutGraph(Graph& graph);

struct GraphLabAlgo {
  struct LNode {
    typedef size_t component_type;
    unsigned int id;
    component_type labelid;
    
    component_type component() { return labelid; }
    bool isRep() { return id == labelid; }
  };

  typedef Galois::Graph::LC_CSR_Graph<LNode,void>
    ::with_no_lockable<true>::type 
    ::with_numa_alloc<true>::type InnerGraph;
  typedef Galois::Graph::LC_InOut_Graph<InnerGraph> Graph;
  typedef Graph::GraphNode GNode;

  struct Initialize {
    Graph& graph;

    Initialize(Graph& g): graph(g) { }
    void operator()(GNode n) const {
      LNode& data = graph.getData(n, Galois::MethodFlag::NONE);
      data.labelid = data.id;
    }
  };

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
    typedef int tt_needs_scatter_in_edges;

  private:
    size_t received_labelid;
    bool perform_scatter;

  public:
    Program(): received_labelid(std::numeric_limits<size_t>::max()), perform_scatter(false) { }

    void init(Graph& graph, GNode node, const message_type& msg) {
      received_labelid = msg.value;
    }

    void apply(Graph& graph, GNode node, const gather_type&) {
      if (received_labelid == std::numeric_limits<size_t>::max()) {
        perform_scatter = true;
      } else if (graph.getData(node, Galois::MethodFlag::NONE).labelid > received_labelid) {
        perform_scatter = true;
        graph.getData(node, Galois::MethodFlag::NONE).labelid = received_labelid;
      }
    }

    bool needsScatter(Graph& graph, GNode node) {
      return perform_scatter;
    }

    void gather(Graph& graph, GNode node, GNode src, GNode dst, gather_type&, typename Graph::edge_data_reference) { }

    void scatter(Graph& graph, GNode node, GNode src, GNode dst,
        Galois::GraphLab::Context<Graph,Program>& ctx, typename Graph::edge_data_reference) {
      LNode& data = graph.getData(node, Galois::MethodFlag::NONE);

      if (node == src && graph.getData(dst, Galois::MethodFlag::NONE).labelid > data.labelid) {
        ctx.push(dst, message_type(data.labelid));
      } else if (node == dst && graph.getData(src, Galois::MethodFlag::NONE).labelid > data.labelid) {
        ctx.push(src, message_type(data.labelid));
      }
    }
  };

  template<typename G>
  void readGraph(G& graph) {
    readInOutGraph(graph);
  }

  void operator()(Graph& graph) {
    Galois::do_all_local(graph, Initialize(graph));

    Galois::GraphLab::SyncEngine<Graph,Program> engine(graph, Program());
    engine.execute();
  }
};

#endif
