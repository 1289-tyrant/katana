/** Billiards Simulation using speculative executor-*- C++ -*-
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

#ifndef BILLIARDS_SPEC_H
#define BILLIARDS_SPEC_H

#include "Galois/Graph/Graph.h"
#include "Galois/Runtime/PerThreadWorkList.h"
#include "Galois/Runtime/ROBexecutor.h"

#include "Billiards.h"

class BilliardsSpec: public Billiards {
  using Graph = Galois::Graph::FirstGraph<void*, void, true>;
  using GNode = Graph::GraphNode;
  using VecNodes = std::vector<GNode>;
  using AddListTy = Galois::Runtime::PerThreadVector<Event>;

  struct VisitNhood {
    Graph& graph;
    VecNodes& nodes;

    VisitNhood (Graph& graph, VecNodes& nodes): graph (graph), nodes (nodes) {}

    template <typename C>
    void operator () (const Event& e, C& ctx) {

      const Ball& b1 = e.getBall ();
      assert (b1.getID () < nodes.size ());
      graph.getData (nodes[b1.getID ()], Galois::CHECK_CONFLICT);

      if (e.getKind () == Event::BALL_COLLISION) {
        const Ball& b2 = e.getOtherBall ();
        assert (b2.getID () < nodes.size ());
        graph.getData (nodes[b2.getID ()], Galois::CHECK_CONFLICT);
      }

    }
  };

  struct OpFunc {

    Table& table;
    const double endtime;
    Accumulator& iter;

    OpFunc (
        Table& table,
        double endtime,
        Accumulator& iter)
      :
        table (table),
        endtime (endtime),
        iter (iter)
    {}


    template <typename C>
    void operator () (Event e, C& ctx) {
      // using const Event& in above param list gives error in the lambda (despite
      // using mutable), why???

      // std::cout << "Processing event: " << e.str () << std::endl;


      const bool notStale = e.notStale ();

      Ball* b1 = nullptr;
      Ball* b2 = nullptr;

      if (notStale) {
        auto alloc = ctx.getPerIterAlloc ();

        using BallAlloc = Galois::PerIterAllocTy::rebind<Ball>::other;
        BallAlloc ballAlloc (alloc);

        b1 = ballAlloc.allocate (1);
        ballAlloc.construct (b1, e.getBall ());


        if (e.getKind () == Event::BALL_COLLISION) {
          b2 = ballAlloc.allocate (1);
          ballAlloc.construct (b2, e.getOtherBall ());

          Event copyEvent = Event::makeBallCollision (*b1, *b2, e.getTime ());
          copyEvent.simulate ();

        } else if (e.getKind () == Event::CUSHION_COLLISION) {
          const Cushion& c = e.getCushion ();
          Event copyEvent = Event::makeCushionCollision (*b1, c, e.getTime ());
          copyEvent.simulate ();

        }
      }

      auto oncommit = [this, &ctx, e, notStale, b1, b2] (void) mutable {


        if (notStale) {
          // update the state of the balls
          assert (b1 != nullptr);
          e.updateFirstBall (*b1);

          if (e.getKind () == Event::BALL_COLLISION) {
            assert (b2 != nullptr);
            e.updateOtherBall (*b2);
          }
        }

        using AddListTy = std::vector<Event, Galois::PerIterAllocTy::rebind<Event>::other>;
        auto alloc = ctx.getPerIterAlloc ();
        AddListTy addList (alloc);
        table.addNextEvents (e, addList, endtime);

        for (auto i = addList.begin ()
            , endi = addList.end (); i != endi; ++i) {

          ctx.push (*i);
        }

        iter += 1;

      };

      ctx.addCommitAction (oncommit);
      
    }
  };

  void createLocks (const Table& table, Graph& graph, VecNodes& nodes) {
    nodes.reserve (table.getNumBalls ());

    for (unsigned i = 0; i < table.getNumBalls (); ++i) {
      nodes.push_back (graph.createNode (nullptr));
    }

  };

public:

  virtual const std::string version () const { return "using Speculative Executor"; }

  virtual size_t runSim (Table& table, std::vector<Event>& initEvents, const double endtime, bool enablePrints=false) {

    Graph graph;
    VecNodes nodes;

    Accumulator iter;

    createLocks (table, graph, nodes);

    Galois::Runtime::for_each_ordered_rob (
        initEvents.begin (), initEvents.end (),
        Event::Comparator (),
        VisitNhood (graph, nodes),
        OpFunc (table, endtime, iter));

    return iter.reduce ();

  }

};

#endif // BILLIARDS_SPEC_H
