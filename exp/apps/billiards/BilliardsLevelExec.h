/** Billiards Simulation using level-by-level executor-*- C++ -*-
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
 * @section Description
 *
 * @author <ahassaan@ices.utexas.edu>
 */

#ifndef BILLIARDS_LEVEL_EXEC_H
#define BILLIARDS_LEVEL_EXEC_H

#include "Galois/Graphs/Graph.h"
#include "Galois/PerThreadContainer.h"

#include "Galois/Runtime/LevelExecutor.h"

#include "Billiards.h"

class BilliardsLevelExec: public Billiards {

  using Graph = Galois::Graph::FirstGraph<void*, void, true>;
  using GNode = Graph::GraphNode;
  using VecNodes = std::vector<GNode>;
  using AddListTy = Galois::PerThreadVector<Event>;

  struct GetEventTime {
    double operator () (const Event& e) const { 
      return e.getTime ();
    }
  };

  struct VisitNhood {
    Graph& graph;
    VecNodes& nodes;

    VisitNhood (Graph& graph, VecNodes& nodes): graph (graph), nodes (nodes) {}

    template <typename C>
    void operator () (const Event& e, C& ctx) const {

      const Ball& b1 = e.getBall ();
      assert (b1.getID () < nodes.size ());
      graph.getData (nodes[b1.getID ()], Galois::MethodFlag::WRITE);

      if (e.getKind () == Event::BALL_COLLISION) {
        const Ball& b2 = e.getOtherBall ();
        assert (b2.getID () < nodes.size ());
        graph.getData (nodes[b2.getID ()], Galois::MethodFlag::WRITE);
      }

    }
  };

  struct OpFunc {

    static const unsigned CHUNK_SIZE = 1;

    Table& table;
    const double endtime;
    AddListTy& addList;
    Accumulator& iter;

    OpFunc (
        Table& table,
        double endtime,
        AddListTy& addList,
        Accumulator& iter)
      :
        table (table),
        endtime (endtime),
        addList (addList),
        iter (iter)
    {}


    template <typename C>
    void operator () (const Event& e, C& ctx) const {

      addList.get ().clear ();

      // TODO: use locks to update balls' state atomically 
      // and read atomically
      const_cast<Event&>(e).simulate ();
      table.addNextEvents (e, addList.get (), endtime);

      for (auto i = addList.get ().begin ()
          , endi = addList.get ().end (); i != endi; ++i) {

        ctx.push (*i);
      }

      iter += 1;
    }
  };

  void createLocks (const Table& table, Graph& graph, VecNodes& nodes) {
    nodes.reserve (table.getNumBalls ());

    for (unsigned i = 0; i < table.getNumBalls (); ++i) {
      nodes.push_back (graph.createNode (nullptr));
    }

  };

public:

  virtual const std::string version () const { return "using Level-by-Level Executor"; }

  virtual size_t runSim (Table& table, std::vector<Event>& initEvents, const double endtime, bool enablePrints=false) {

    Graph graph;
    VecNodes nodes;

    AddListTy addList;
    Accumulator iter;

    createLocks (table, graph, nodes);

    Galois::Runtime::for_each_ordered_level (
        Galois::Runtime::makeStandardRange (initEvents.begin (), initEvents.end ()),
        GetEventTime (), std::less<double> (),
        VisitNhood (graph, nodes),
        OpFunc (table, endtime, addList, iter));

    return iter.reduce ();

  }

};


#endif // BILLIARDS_LEVEL_EXEC_H
