/** Basic morph graphs -*- C++ -*-
 * @file
 * @section License
 *
 * This file is part of Galois.  Galoisis a framework to exploit
 * amorphous data-parallelism in irregular programs.
 *
 * Galois is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, version 2.1 of the
 * License.
 *
 * Galois is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Galois.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * @section Copyright
 *
 * Copyright (C) 2015, The University of Texas at Austin. All rights
 * reserved.
 *
 * @section Description
 *
 * @author Andrew Lenharth <andrewl@lenharth.org>
 */

#ifndef GALOIS_GRAPH_FIRSTGRAPH_H
#define GALOIS_GRAPH_FIRSTGRAPH_H

#include "Galois/Bag.h"
#include "Galois/Graphs/FileGraph.h"
#include "Galois/Graphs/Details.h"

#include "llvm/ADT/SmallVector.h"

#include <boost/functional.hpp>
#include <boost/iterator/transform_iterator.hpp>
#include <boost/iterator/filter_iterator.hpp>

#include <algorithm>
#include <map>
#include <set>
#include <type_traits>
#include <vector>

namespace Galois {
//! Parallel graph data structures.
namespace Graph {

namespace FirstGraphImpl {
/**
 * Wrapper class to have a valid type on void edges
 */
template<typename NTy, typename ETy, bool Directed>
struct UEdgeInfoBase;

template<typename NTy, typename ETy>
struct UEdgeInfoBase<NTy, ETy, true> {
  typedef ETy& reference;

  NTy* N;
  ETy Ea;

  inline NTy*&       first()       { assert(N); return N; }
  inline NTy* const& first() const { assert(N); return N; }
  inline ETy*       second()       { return &Ea; }
  inline const ETy* second() const { return &Ea; }

  template<typename... Args>
  UEdgeInfoBase(NTy* n, ETy* v, Args&&... args) : N(n), Ea(std::forward<Args>(args)...) {}

  template<typename... Args>
  UEdgeInfoBase(ETy* v, Args&&... args) :Ea(std::forward<Args>(args)...) {}
  
  template<typename... Args>
  UEdgeInfoBase(NTy* n, ETy &v, Args&&... args): N(n) { Ea = v; }

  static size_t sizeOfSecond()     { return sizeof(ETy); }
};

template<typename NTy, typename ETy>
struct UEdgeInfoBase<NTy, ETy, false> {
  typedef ETy& reference;
  
  NTy* N;
  ETy* Ea;

  inline NTy*&       first()       { assert(N); return N; }
  inline NTy* const& first() const { assert(N); return N; }
  inline ETy*       second()       { return Ea; }
  inline const ETy* second() const { return Ea; }
  template<typename... Args>
  UEdgeInfoBase(NTy* n, ETy* v, Args&&... args) : N(n), Ea(v) {}
  static size_t sizeOfSecond()     { return sizeof(ETy); }
};

template<typename NTy>
struct UEdgeInfoBase<NTy, void, true> {
  typedef char& reference;

  NTy* N;
  inline NTy*&       first()        { return N; }
  inline NTy* const& first()  const { return N; }
  inline char*       second() const { return static_cast<char*>(NULL); }
  inline char*       addr()   const { return second(); }
  template<typename... Args>
  UEdgeInfoBase(NTy* n, void* v, Args&&... args) : N(n) {}
  static size_t sizeOfSecond()      { return 0; }
};

template<typename NTy>
struct UEdgeInfoBase<NTy, void, false> {
  typedef char& reference;

  NTy* N;
  inline NTy*&       first()        { return N; }
  inline NTy* const& first()  const { return N; }
  inline char*       second() const { return static_cast<char*>(NULL); }
  inline char*       addr()   const { return second(); }
  template<typename... Args>
  UEdgeInfoBase(NTy* n, void* v, Args&&... args) : N(n) {}
  static size_t sizeOfSecond()      { return 0; }
};

template<typename ETy>
struct EdgeFactory {
  Galois::Runtime::FixedSizeAllocator<ETy> mem;
  template<typename... Args>
  ETy* mkEdge(Args&&... args) {
    ETy* e = mem.allocate(1);
    mem.construct(e, std::forward<Args>(args)...);
    return e;
  }
  void delEdge(ETy* e) {
    mem.destroy(e);
    mem.deallocate(e, 1);
  }
  bool mustDel() const { return true; }
};

template<>
struct EdgeFactory<void> {
  template<typename... Args>
  void* mkEdge(Args&&... args) { return static_cast<void*>(NULL); }
  void delEdge(void*) {}
  bool mustDel() const { return false; }
};

} // end namespace impl

/**
 * A Graph.
 *
 * An example of use:
 * 
 * \code
 * struct Node {
 *   ... // Definition of node data
 * };
 *
 * typedef Galois::Graph::FirstGraph<Node,int,true> Graph;
 * 
 * // Create graph
 * Graph g;
 * Node n1, n2;
 * Graph::GraphNode a, b;
 * a = g.createNode(n1);
 * g.addNode(a);
 * b = g.createNode(n2);
 * g.addNode(b);
 * g.getEdgeData(g.addEdge(a, b)) = 5;
 *
 * // Traverse graph
 * for (Graph::iterator ii = g.begin(), ei = g.end(); ii != ei; ++ii) {
 *   Graph::GraphNode src = *ii;
 *   for (Graph::edge_iterator jj = g.edge_begin(src), ej = g.edge_end(src); ++jj) {
 *     Graph::GraphNode dst = graph.getEdgeDst(jj);
 *     int edgeData = g.getEdgeData(jj);
 *     assert(edgeData == 5);
 *   }
 * }
 * \endcode
 *
 * And in C++11:
 *
 * \code
 * // Traverse graph
 * for (Graph::GraphNode src : g) {
 *   for (Graph::edge_iterator edge : g.out_edges(src)) {
 *     Graph::GraphNode dst = g.getEdgeDst(edge);
 *     int edgeData = g.getEdgeData(edge);
 *     assert(edgeData == 5);
 *   }
 * }
 * \endcode
 *
 * @tparam NodeTy Type of node data
 * @tparam EdgeTy Type of edge data
 * @tparam Directional true if graph is directed
 * @tparam SortedNeighbors Keep neighbors sorted (for faster findEdge)
 */
template<typename NodeTy, typename EdgeTy, bool Directional,
  bool HasNoLockable=false,
  bool SortedNeighbors=false,
  typename FileEdgeTy=EdgeTy
  >
class FirstGraph : private boost::noncopyable {
public:
  //! If true, do not use abstract locks in graph
  template<bool _has_no_lockable>
  struct with_no_lockable { typedef FirstGraph<NodeTy,EdgeTy,Directional,_has_no_lockable,SortedNeighbors,FileEdgeTy> type; };

  template<typename _node_data>
  struct with_node_data { typedef FirstGraph<_node_data,EdgeTy,Directional,HasNoLockable,SortedNeighbors,FileEdgeTy> type; };

  template<typename _edge_data>
  struct with_edge_data { typedef FirstGraph<NodeTy,_edge_data,Directional,HasNoLockable,SortedNeighbors,FileEdgeTy> type; };

  template<typename _file_edge_data>
  struct with_file_edge_data { typedef FirstGraph<NodeTy,EdgeTy,Directional,HasNoLockable,SortedNeighbors,_file_edge_data> type; };

  template<bool _directional>
  struct with_directional { typedef FirstGraph<NodeTy,EdgeTy,_directional,HasNoLockable,SortedNeighbors,FileEdgeTy> type; };

  template<bool _sorted_neighbors>
  struct with_sorted_neighbors { typedef FirstGraph<NodeTy,EdgeTy,Directional,HasNoLockable,_sorted_neighbors,FileEdgeTy> type; };

  typedef read_with_aux_graph_tag read_tag;

private:
  template<typename T>
  struct first_eq_and_valid {
    T N2;
    first_eq_and_valid(T& n) :N2(n) {}
    template <typename T2>
    bool operator()(const T2& ii) const { 
      return ii.first() == N2 && ii.first() && ii.first()->active;
    }
  };

  struct first_not_valid {
    template <typename T2>
    bool operator()(const T2& ii) const { return !ii.first() || !ii.first()->active; }
  };

  template<typename T>
  struct first_lt {
    template <typename T2>
    bool operator()(const T& N2, const T2& ii) const {
      assert(ii.first() && "UNEXPECTED: invalid item in edgelist");
      return N2 < ii.first();
    }
    template <typename T2>
    bool operator()(const T2& ii, const T& N2) const {
      assert(ii.first() && "UNEXPECTED: invalid item in edgelist");
      return ii.first() < N2;
    }
  };
  
  class gNode;
  struct gNodeTypes: public detail::NodeInfoBaseTypes<NodeTy, !HasNoLockable> {
    //! The storage type for an edge
    typedef FirstGraphImpl::UEdgeInfoBase<gNode, EdgeTy, Directional> EdgeInfo;
    
    //! The storage type for edges
    typedef llvm::SmallVector<EdgeInfo, 3> EdgesTy;
    
    typedef typename EdgesTy::iterator iterator;
  };

  class gNode:
    public detail::NodeInfoBase<NodeTy, !HasNoLockable>,
    public gNodeTypes
  {
    friend class FirstGraph;
    typedef detail::NodeInfoBase<NodeTy, !HasNoLockable> NodeInfo;
    typename gNode::EdgesTy edges;
    typedef typename gNode::iterator iterator;
    typedef typename gNode::EdgeInfo EdgeInfo;

    bool active;
    
    iterator begin() { return edges.begin(); }
    iterator end() { return edges.end();  }
    
    void erase(iterator ii) {
      if (SortedNeighbors) {
        // For sorted case remove the element, moving following
        // elements back to fill the space.
        edges.erase(ii);
      } else {
        // We don't need to preserve the order, so move the last edge
        // into this place and then remove last edge.
        *ii = edges.back();
        edges.pop_back();
      }
    }

    void erase(gNode* N) { 
      iterator ii = find(N);
      if (ii != end())
        edges.erase(ii); 
    }

    iterator find(gNode* N) {
      if (SortedNeighbors) {
        iterator ei = edges.end();
        iterator ii = std::lower_bound(edges.begin(), ei, N,
                                       first_lt<gNode*>());
        first_eq_and_valid<gNode*> checker(N);
        return (ii == ei || checker(*ii)) ? ii : ei;
      }
      else
        return std::find_if(begin(), end(), first_eq_and_valid<gNode*>(N));
    }

    void resizeEdges(size_t size) {
      edges.resize(size, EdgeInfo(new gNode(), 0));
    }

    template<typename... Args>
    iterator createEdge(gNode* N, EdgeTy* v, Args&&... args) {
      iterator ii;
      if (SortedNeighbors) {
        // If neighbors are sorted, find appropriate insertion point.
        // Insert before first neighbor that is too far.
        ii = std::upper_bound(edges.begin(), edges.end(), N,
                              first_lt<gNode*>());
      }
      else
        ii = edges.end();
      return edges.insert(ii, EdgeInfo(N, v, std::forward<Args>(args)...));
    }

    template<typename... Args>
    iterator createEdgeWithReuse(gNode* N, EdgeTy* v, Args&&... args) {
      // First check for holes
      iterator ii, ei;
      if (SortedNeighbors) {
        // If neighbors are sorted, find acceptable range for insertion.
        ii = std::lower_bound(edges.begin(), edges.end(), N,
                              first_lt<gNode*>());
        ei = std::upper_bound(ii, edges.end(), N,
                              first_lt<gNode*>());
      }
      else {
        // If not sorted, we can insert anywhere in the list.
        ii = edges.begin();
        ei = edges.end();
      }
      ii = std::find_if(ii, ei, first_not_valid());
      if (ii != ei) {
        // FIXME: We could move elements around (short distances).
	*ii = EdgeInfo(N, v, std::forward<Args>(args)...);
	return ii;
      }
      return edges.insert(ei, EdgeInfo(N, v, std::forward<Args>(args)...));
    }

    template<bool _A1 = HasNoLockable>
    void acquire(MethodFlag mflag, typename std::enable_if<!_A1>::type* = 0) {
      Galois::Runtime::acquire(this, mflag);
    }

    template<bool _A1 = HasNoLockable>
    void acquire(MethodFlag mflag, typename std::enable_if<_A1>::type* = 0) { }

  public:
    template<typename... Args>
    gNode(Args&&... args): NodeInfo(std::forward<Args>(args)...), active(false) { }
  };

  //The graph manages the lifetimes of the data in the nodes and edges
  typedef Galois::InsertBag<gNode> NodeListTy;
  NodeListTy nodes;

  FirstGraphImpl::EdgeFactory<EdgeTy> edgesF;

  //Helpers for iterator classes
  struct is_node : public std::unary_function<gNode&, bool>{
    bool operator() (const gNode& g) const { return g.active; }
  };
  struct is_edge : public std::unary_function<typename gNodeTypes::EdgeInfo&, bool> {
    bool operator()(typename gNodeTypes::EdgeInfo& e) const { return e.first()->active; }
  };
  struct makeGraphNode: public std::unary_function<gNode&, gNode*> {
    gNode* operator()(gNode& data) const { return &data; }
  };

public:
  //! Graph node handle
  typedef gNode* GraphNode;
  //! Edge data type
  typedef EdgeTy edge_data_type;
  //! Edge data type of file we are loading this graph from
  typedef FileEdgeTy file_edge_data_type;
  //! Node data type
  typedef NodeTy node_data_type;
  //! Edge iterator
  typedef typename boost::filter_iterator<is_edge, typename gNodeTypes::iterator> edge_iterator;
  //! Reference to edge data
  typedef typename gNodeTypes::EdgeInfo::reference edge_data_reference;
  //! Reference to node data
  typedef typename gNodeTypes::reference node_data_reference;
  //! Node iterator
  typedef boost::transform_iterator<makeGraphNode,
          boost::filter_iterator<is_node,
                   typename NodeListTy::iterator> > iterator;
  typedef LargeArray<GraphNode> ReadGraphAuxData;

private:
  template<typename... Args>
  edge_iterator createEdgeWithReuse(GraphNode src, GraphNode dst, Galois::MethodFlag mflag, Args&&... args) {
    assert(src);
    assert(dst);
    // Galois::Runtime::checkWrite(mflag, true);
    src->acquire(mflag);
    typename gNode::iterator ii = src->find(dst);
    if (ii == src->end()) {
      if (Directional) {
	ii = src->createEdgeWithReuse(dst, 0, std::forward<Args>(args)...);
      } else {
        dst->acquire(mflag);
	EdgeTy* e = edgesF.mkEdge(std::forward<Args>(args)...);
	ii = dst->createEdgeWithReuse(src, e, std::forward<Args>(args)...);
	ii = src->createEdgeWithReuse(dst, e, std::forward<Args>(args)...);
      }
    }
    return boost::make_filter_iterator(is_edge(), ii, src->end());
  }

  template<typename... Args>
  edge_iterator createEdge(GraphNode src, GraphNode dst, Galois::MethodFlag mflag, Args&&... args) {
    assert(src);
    assert(dst);
    // Galois::Runtime::checkWrite(mflag, true);
    src->acquire(mflag);
    typename gNode::iterator ii = src->end();
    if (ii == src->end()) {
      if (Directional) {
	ii = src->createEdge(dst, 0, std::forward<Args>(args)...);
      } else {
        dst->acquire(mflag);
	EdgeTy* e = edgesF.mkEdge(std::forward<Args>(args)...);
	ii = dst->createEdge(src, e, std::forward<Args>(args)...);
	ii = src->createEdge(dst, e, std::forward<Args>(args)...);
      }
    }
    return boost::make_filter_iterator(is_edge(), ii, src->end());
  }

  template<bool _A1 = LargeArray<EdgeTy>::has_value, bool _A2 = LargeArray<FileEdgeTy>::has_value>
  void constructEdgeValue(FileGraph& graph, typename FileGraph::edge_iterator nn,
      GraphNode src, GraphNode dst, typename std::enable_if<!_A1 || _A2>::type* = 0) {
    typedef typename LargeArray<FileEdgeTy>::value_type FEDV;
    typedef LargeArray<EdgeTy> ED;
    if (ED::has_value) {
      addMultiEdge(src, dst, Galois::MethodFlag::UNPROTECTED, graph.getEdgeData<FEDV>(nn));
    } else {
      addMultiEdge(src, dst, Galois::MethodFlag::UNPROTECTED);
    }
  }

  template<bool _A1 = LargeArray<EdgeTy>::has_value, bool _A2 = LargeArray<FileEdgeTy>::has_value>
  void constructEdgeValue(FileGraph& graph, typename FileGraph::edge_iterator nn,
      GraphNode src, GraphNode dst, typename std::enable_if<_A1 && !_A2>::type* = 0) {
    addMultiEdge(src, dst, Galois::MethodFlag::UNPROTECTED);
  }

public:
  /**
   * Creates a new node holding the indicated data. Usually you should call
   * {@link addNode()} afterwards.
   */
  template<typename... Args>
  GraphNode createNode(Args&&... args) {
    gNode* N = &(nodes.emplace(std::forward<Args>(args)...));
    N->active = false;
    return GraphNode(N);
  }

  /**
   * Adds a node to the graph.
   */
  void addNode(const GraphNode& n, Galois::MethodFlag mflag = MethodFlag::WRITE) {
    // Galois::Runtime::checkWrite(mflag, true);
    n->acquire(mflag);
    n->active = true;
  }

  //! Gets the node data for a node.
  node_data_reference getData(const GraphNode& n, Galois::MethodFlag mflag = MethodFlag::WRITE) const {
    assert(n);
    // Galois::Runtime::checkWrite(mflag, false);
    n->acquire(mflag);
    return n->getData();
  }

  //! Checks if a node is in the graph
  bool containsNode(const GraphNode& n, Galois::MethodFlag mflag = MethodFlag::WRITE) const {
    assert(n);
    n->acquire(mflag);
    return n->active;
  }

  /**
   * Removes a node from the graph along with all its outgoing/incoming edges
   * for undirected graphs or outgoing edges for directed graphs.
   */
  //FIXME: handle edge memory
  void removeNode(GraphNode n, Galois::MethodFlag mflag = MethodFlag::WRITE) {
    assert(n);
    // Galois::Runtime::checkWrite(mflag, true);
    n->acquire(mflag);
    gNode* N = n;
    if (N->active) {
      N->active = false;
      if (!Directional && edgesF.mustDel())
	for (edge_iterator ii = edge_begin(n, MethodFlag::UNPROTECTED), ee = edge_end(n, MethodFlag::UNPROTECTED); ii != ee; ++ii)
	  edgesF.delEdge(ii->second());
      N->edges.clear();
    }
  }

  /**
   * Resize the edges of the node. For best performance, should be done serially.
   */
  void resizeEdges(GraphNode src, size_t size, Galois::MethodFlag mflag = MethodFlag::WRITE) {
    assert(src);
    // Galois::Runtime::checkWrite(mflag, false);
    src->acquire(mflag);
    src->resizeEdges(size);
   }

  /** 
   * Adds an edge to graph, replacing existing value if edge already exists. 
   *
   * Ignore the edge data, let the caller use the returned iterator to set the
   * value if desired.  This frees us from dealing with the void edge data
   * problem in this API
   */
  edge_iterator addEdge(GraphNode src, GraphNode dst, Galois::MethodFlag mflag = MethodFlag::WRITE) {
    return createEdgeWithReuse(src, dst, mflag);
  }

  //! Adds and initializes an edge to graph but does not check for duplicate edges
  template<typename... Args>
  edge_iterator addMultiEdge(GraphNode src, GraphNode dst, Galois::MethodFlag mflag, Args&&... args) {
    return createEdge(src, dst, mflag, std::forward<Args>(args)...);
  }

  //! Removes an edge from the graph
  void removeEdge(GraphNode src, edge_iterator dst, Galois::MethodFlag mflag = MethodFlag::WRITE) {
    assert(src);
    // Galois::Runtime::checkWrite(mflag, true);
    src->acquire(mflag);
    if (Directional) {
      src->erase(dst.base());
    } else {
      dst->first()->acquire(mflag);
      EdgeTy* e = dst->second();
      edgesF.delEdge(e);
      src->erase(dst.base());
      dst->first()->erase(src);
    }
  }

  //! Finds if an edge between src and dst exists
  edge_iterator findEdge(GraphNode src, GraphNode dst, Galois::MethodFlag mflag = MethodFlag::WRITE) {
    assert(src);
    assert(dst);
    src->acquire(mflag);
    typename gNodeTypes::iterator ii = src->find(dst), ei = src->end();
    is_edge edge_predicate;
    if ( ii != ei && edge_predicate(*ii) ) {
      // After finding edge, lock dst and verify still active
      dst->acquire(mflag);
      if ( !edge_predicate(*ii) )
        // I think we need this too, else we'll return some random iterator.
        ii = ei;
    }
    else
      ii = ei;
    return boost::make_filter_iterator(edge_predicate, ii, ei);
  }

  /**
   * Returns the edge data associated with the edge. It is an error to
   * get the edge data for a non-existent edge.  It is an error to get
   * edge data for inactive edges. By default, the mflag is Galois::MethodFlag::UNPROTECTED
   * because edge_begin() dominates this call and should perform the
   * appropriate locking.
   */
  edge_data_reference getEdgeData(edge_iterator ii, Galois::MethodFlag mflag = MethodFlag::UNPROTECTED) const {
    assert(ii->first()->active);
    // Galois::Runtime::checkWrite(mflag, false);
    ii->first()->acquire(mflag);
    return *ii->second();
  }

  //! Returns the destination of an edge
  GraphNode getEdgeDst(edge_iterator ii) {
    assert(ii->first()->active);
    return GraphNode(ii->first());
  }

  //// General Things ////

  //! Returns an iterator to the neighbors of a node 
  edge_iterator edge_begin(GraphNode N, Galois::MethodFlag mflag = MethodFlag::WRITE) {
    assert(N);
    N->acquire(mflag);

    if (Galois::Runtime::shouldLock(mflag)) {
      for (typename gNode::iterator ii = N->begin(), ee = N->end(); ii != ee; ++ii) {
	if (ii->first()->active)
	  ii->first()->acquire(mflag);
      }
    }
    return boost::make_filter_iterator(is_edge(), N->begin(), N->end());
  }

  //! Returns the end of the neighbor iterator 
  edge_iterator edge_end(GraphNode N, Galois::MethodFlag mflag = MethodFlag::WRITE) {
    assert(N);
    // Acquiring lock is not necessary: no valid use for an end pointer should
    // ever require it
    // N->acquire(mflag);
    return boost::make_filter_iterator(is_edge(), N->end(), N->end());
  }

  Runtime::iterable<NoDerefIterator<edge_iterator>> edges(GraphNode N, Galois::MethodFlag mflag = MethodFlag::WRITE) {
    return detail::make_no_deref_range(edge_begin(N, mflag), edge_end(N, mflag));
  }

  /**
   * An object with begin() and end() methods to iterate over the outgoing
   * edges of N.
   */
  detail::EdgesIterator<FirstGraph> out_edges(GraphNode N, MethodFlag mflag = MethodFlag::WRITE) {
    return detail::EdgesIterator<FirstGraph>(*this, N, mflag);
  }

  /**
   * Returns an iterator to all the nodes in the graph. Not thread-safe.
   */
  iterator begin() {
    return boost::make_transform_iterator(
           boost::make_filter_iterator(is_node(),
				       nodes.begin(), nodes.end()),
	   makeGraphNode());
  }

  //! Returns the end of the node iterator. Not thread-safe.
  iterator end() {
    return boost::make_transform_iterator(
           boost::make_filter_iterator(is_node(),
				       nodes.end(), nodes.end()), 
	   makeGraphNode());
  }

  typedef iterator local_iterator;

  local_iterator local_begin() {
    return boost::make_transform_iterator(
           boost::make_filter_iterator(is_node(),
				       nodes.local_begin(), nodes.local_end()),
	   makeGraphNode());
  }

  local_iterator local_end() {
    return boost::make_transform_iterator(
           boost::make_filter_iterator(is_node(),
				       nodes.local_end(), nodes.local_end()), 
	   makeGraphNode());
  }

  /**
   * Returns the number of nodes in the graph. Not thread-safe.
   */
  unsigned int size() {
    return std::distance(begin(), end());
  }

  //! Returns the size of edge data.
  size_t sizeOfEdgeData() const {
    return gNode::EdgeInfo::sizeOfSecond();
  }

  void allocateFrom(FileGraph& graph, ReadGraphAuxData& aux) { 
    size_t numNodes = graph.size();
    aux.allocateInterleaved(numNodes);
  }

  void constructNodesFrom(FileGraph& graph, unsigned tid, unsigned total, ReadGraphAuxData& aux) {
    auto r = graph.divideByNode(sizeof(gNode), sizeof(typename gNode::EdgeInfo), tid, total).first;
    for (FileGraph::iterator ii = r.first, ei = r.second; ii != ei; ++ii) {
      aux[*ii] = createNode();
      addNode(aux[*ii], Galois::MethodFlag::UNPROTECTED);
    }
  }

  void constructEdgesFrom(FileGraph& graph, unsigned tid, unsigned total, const ReadGraphAuxData& aux) {
    typedef typename std::decay<typename gNode::EdgeInfo::reference>::type value_type;
    auto r = graph.divideByNode(sizeof(gNode), sizeof(typename gNode::EdgeInfo), tid, total).first;

    for (FileGraph::iterator ii = r.first, ei = r.second; ii != ei; ++ii) {
      for (FileGraph::edge_iterator nn = graph.edge_begin(*ii), en = graph.edge_end(*ii); nn != en; ++nn) {
        constructEdgeValue(graph, nn, aux[*ii], aux[graph.getEdgeDst(nn)]);
      }
    }
  }
};

}
}
#endif
