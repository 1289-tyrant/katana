/** DES ordered version -*- C++ -*-
 * @file
 * @section License
 *
 * This file is part of Galois.  Galoisis a framework to exploit
 * amorphous data-parallelism in irregular programs.
 *
 * Galois is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
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
 * @author M. Amber Hassaan <ahassaan@ices.utexas.edu>
 */

#ifndef DES_ORDERED_EXP_H
#define DES_ORDERED_EXP_H

#include "Galois/Accumulator.h"
#include "Galois/Timer.h"
#include "Galois/PerThreadContainer.h"

#include "Galois/Substrate/PaddedLock.h"
#include "Galois/Substrate/CompilerSpecific.h"

#include <deque>
#include <functional>
#include <queue>
#include <set>

#include <cassert>


#include "abstractMain.h"
#include "SimInit.h"
#include "TypeHelper.h"


namespace des_ord {

typedef Galois::GAccumulator<size_t> Accumulator_ty;

typedef des::EventRecvTimeLocalTieBrkCmp<TypeHelper::Event_ty> Cmp_ty;

typedef Galois::PerThreadVector<TypeHelper::Event_ty> AddList_ty;

struct SimObjInfo: public TypeHelper {

  typedef Galois::Substrate::SimpleLock Lock_ty;
  typedef des::AbstractMain<SimInit_ty>::GNode GNode;
  typedef std::set<Event_ty, Cmp_ty
    , Galois::FixedSizeAllocator<Event_ty> > PQ;

  Lock_ty mutex;
  PQ pendingEvents;

  GNode node;
  size_t numInputs;
  size_t numOutputs;
  std::vector<des::SimTime> inputTimes;

  SimObjInfo () {}

  SimObjInfo (const GNode& node, SimObj_ty* sobj): node (node) {
    SimGate_ty* sg = static_cast<SimGate_ty*> (sobj);
    assert (sg != NULL);

    numInputs = sg->getImpl ().getNumInputs ();
    numOutputs = sg->getImpl ().getNumOutputs ();

    inputTimes.resize (numInputs, des::SimTime ());
  }

  void recv (const Event_ty& event) {

    SimGate_ty* dstGate = static_cast<SimGate_ty*> (event.getRecvObj ());
    assert (dstGate != NULL);

    const std::string& outNet = event.getAction ().getNetName ();
    size_t dstIn = dstGate->getImpl ().getInputIndex (outNet); // get the input index of the net to which my output is connected

    assert (dstIn < inputTimes.size ());
    inputTimes[dstIn] = event.getRecvTime ();

    mutex.lock ();
      pendingEvents.insert (event);
    mutex.unlock ();
  }

  bool hasPending () const {
    mutex.lock ();
      bool ret = !pendingEvents.empty ();
    mutex.unlock ();
    return ret;
  }

  bool hasReady () const {
    mutex.lock ();
      bool ret = false;
      if (!pendingEvents.empty ()) {
        ret = isReady (*pendingEvents.begin ());
      }
    mutex.unlock ();

    return ret;
  }

  Event_ty getMin () const { 
    mutex.lock ();
       Event_ty ret = *pendingEvents.begin ();
    mutex.unlock ();

    return ret;
  }

  bool isMin (const Event_ty& event) const {
    mutex.lock ();
      bool ret = !pendingEvents.empty ()
        && (*pendingEvents.begin () == event);
    mutex.unlock ();

    return ret;
  }

  Event_ty removeMin () {
    mutex.lock ();
      assert (!pendingEvents.empty ());
      Event_ty event = *pendingEvents.begin ();
      pendingEvents.erase (pendingEvents.begin ());
    mutex.unlock ();

    return event;
  }

  void remove (const Event_ty& event) {
    mutex.lock ();
      assert (pendingEvents.find (event) != pendingEvents.end ());
      pendingEvents.erase (event);
    mutex.unlock ();
  }


  bool isReady (const Event_ty& event) const {
    // not ready if event has a timestamp greater than the latest event received
    // on any input. 
    // an input with INFINITY_SIM_TIME is dead and will not receive more non-null events
    // in the future
    bool notReady = false;
    for (std::vector<des::SimTime>::const_iterator i = inputTimes.begin ()
        , endi = inputTimes.end (); i != endi; ++i) {

      if ((*i < des::INFINITY_SIM_TIME) && (event.getRecvTime () > *i)) {
        notReady = true;
        break;
      }
    }

    return !notReady;
  }

};


std::vector<SimObjInfo>::iterator
getGlobalMin (std::vector<SimObjInfo>& sobjInfoVec) {

  std::vector<SimObjInfo>::iterator minPos = sobjInfoVec.end ();

  for (std::vector<SimObjInfo>::iterator i = sobjInfoVec.begin ()
      , endi = sobjInfoVec.end (); i != endi; ++i) {

    if (i->hasPending ()) {

      if (minPos == sobjInfoVec.end ()) {
        minPos = i;

      } else if (Cmp_ty::compare (i->getMin (), minPos->getMin ()) < 0) {
        minPos = i;
      }
    }
  }

  return minPos;
}

class DESorderedHand: 
  public des::AbstractMain<TypeHelper::SimInit_ty>, public TypeHelper {

    typedef Galois::PerThreadVector<Event_ty> WL_ty;



  struct FindReady {
    WL_ty& readyEvents;
    Accumulator_ty& findIter;

    FindReady (
        WL_ty& readyEvents,
        Accumulator_ty& findIter)
      :
        readyEvents (readyEvents),
        findIter (findIter)
    {}

    GALOIS_ATTRIBUTE_PROF_NOINLINE void operator () (SimObjInfo& sinfo) const {
      findIter += 1;

      if (sinfo.hasReady ()) {
        readyEvents.get ().push_back (sinfo.removeMin ());
      }
    }
  };

  struct ProcessEvents {

    typedef int tt_does_not_need_aborts;
    typedef char tt_does_not_need_push;
    
    Graph& graph;
    std::vector<SimObjInfo>& sobjInfoVec;
    AddList_ty& newEvents;
    Accumulator_ty& nevents;


    ProcessEvents (
        Graph& graph,
        std::vector<SimObjInfo>& sobjInfoVec,
        AddList_ty& newEvents,
        Accumulator_ty& nevents)
      :
        graph (graph),
        sobjInfoVec (sobjInfoVec),
        newEvents (newEvents),
        nevents (nevents)
    {}


    GALOIS_ATTRIBUTE_PROF_NOINLINE void operator () (const Event_ty& event) const {
      nevents += 1;


      newEvents.get ().clear ();
      SimObj_ty* recvObj = static_cast<SimObj_ty*> (event.getRecvObj ());
      GNode recvNode = sobjInfoVec[recvObj->getID ()].node;

      recvObj->execEvent (event, graph, recvNode, newEvents.get ());

      for (AddList_ty::local_iterator a = newEvents.get ().begin ()
          , enda = newEvents.get ().end (); a != enda; ++a) {

        sobjInfoVec[a->getRecvObj ()->getID ()].recv (*a);
      }
    }
  };



  std::vector<SimObjInfo> sobjInfoVec;

protected:
  virtual std::string getVersion () const { return "Handwritten Ordered ODG based"; }

  virtual void initRemaining (const SimInit_ty& simInit, Graph& graph) {
    sobjInfoVec.clear ();
    sobjInfoVec.resize (graph.size ());

    for (Graph::iterator n = graph.begin ()
        , endn = graph.end (); n != endn; ++n) {

      SimObj_ty* so = static_cast<SimObj_ty*> (graph.getData (*n, Galois::MethodFlag::UNPROTECTED));
      sobjInfoVec[so->getID ()] = SimObjInfo (*n, so);
    }
  }


  virtual void runLoop (const SimInit_ty& simInit, Graph& graph) {

    for (std::vector<Event_ty>::const_iterator i = simInit.getInitEvents ().begin ()
        , endi = simInit.getInitEvents ().end (); i != endi; ++i) {

      SimObj_ty* recvObj = static_cast<SimObj_ty*> (i->getRecvObj ());
      sobjInfoVec[recvObj->getID ()].recv (*i);

    }

    WL_ty readyEvents;
    AddList_ty newEvents;

    Accumulator_ty findIter;
    Accumulator_ty nevents;
    size_t round = 0;
    size_t gmin_calls = 0;

    Galois::TimeAccumulator t_find;
    Galois::TimeAccumulator t_gmin;
    Galois::TimeAccumulator t_simulate;

    while (true) {
      ++round;
      readyEvents.clear_all ();

      assert (readyEvents.empty_all ());

      t_find.start ();
      Galois::do_all (
      // Galois::Runtime::do_all_coupled (
          sobjInfoVec.begin (), sobjInfoVec.end (),
          FindReady (readyEvents, findIter), Galois::loopname("find_ready_events"));
      t_find.stop ();

      // std::cout << "Number of ready events found: " << readyEvents.size_all () << std::endl;

      if (readyEvents.empty_all ()) {
        t_gmin.start ();

        ++gmin_calls;

        std::vector<SimObjInfo>::iterator minPos = getGlobalMin (sobjInfoVec);

        if (minPos == sobjInfoVec.end ()) {
          break;

        } else {
          readyEvents.get ().push_back (minPos->removeMin ());
        }

        t_gmin.stop ();
      }

      t_simulate.start ();
      Galois::do_all (
      // Galois::Runtime::do_all_coupled (
          readyEvents.begin_all (), readyEvents.end_all (),
          ProcessEvents (graph, sobjInfoVec, newEvents, nevents), Galois::loopname("process_ready_events"));
      t_simulate.stop ();
    }

    std::cout << "Number of rounds = " << round << std::endl;
    std::cout << "Number of iterations spent in finding ready events = " << 
      findIter.reduce () << std::endl;
    std::cout << "Number of events processed = " << nevents.reduce () << std::endl;
    std::cout << "Average parallelism: " << double (nevents.reduce ())/ double (round) << std::endl;
    std::cout << "Number of times global min computed = " << gmin_calls << std::endl;
    std::cout << "Time spent in finding ready events = " << t_find.get () << std::endl;
    std::cout << "Time spent in computing global min = " << t_gmin.get () << std::endl;
    std::cout << "Time spent in simulating events = " << t_simulate.get () << std::endl;

  }

};


class DESorderedHandNB: 
  public des::AbstractMain<TypeHelper::SimInit_ty>, public TypeHelper {

  struct OpFuncEagerAdd {

    typedef int tt_does_not_need_aborts;

    Graph& graph;
    std::vector<SimObjInfo>& sobjInfoVec;
    AddList_ty& newEvents;
    Accumulator_ty& niter;
    Accumulator_ty& nevents;

    OpFuncEagerAdd (
        Graph& graph,
        std::vector<SimObjInfo>& sobjInfoVec,
        AddList_ty& newEvents,
        Accumulator_ty& niter,
        Accumulator_ty& nevents)
      :
        graph (graph),
        sobjInfoVec (sobjInfoVec),
        newEvents (newEvents),
        niter (niter),
        nevents (nevents)
    {}

    template <typename C>
    void operator () (const Event_ty& event, C& lwl) {

      niter += 1;

      SimObj_ty* recvObj = static_cast<SimObj_ty*> (event.getRecvObj ());
      SimObjInfo& recvInfo = sobjInfoVec[recvObj->getID ()];

      graph.getData (recvInfo.node, Galois::MethodFlag::WRITE); 

      if (recvInfo.isReady (event) 
          && recvInfo.isMin (event)) {
        nevents += 1;
        newEvents.get ().clear ();

        GNode& recvNode = sobjInfoVec[recvObj->getID ()].node;

        recvObj->execEvent (event, graph, recvNode, newEvents.get ());

        for (AddList_ty::local_iterator a = newEvents.get ().begin ()
            , enda = newEvents.get ().end (); a != enda; ++a) {

          SimObjInfo& sinfo = sobjInfoVec[a->getRecvObj ()->getID ()];

          sinfo.recv (*a);

          // if (sinfo.getMin () == *a) {
            // lwl.push (*a);
          // }
          lwl.push (sinfo.getMin ());

        }


        assert (recvInfo.isReady (event));


        recvInfo.remove (event);
        if (recvInfo.hasReady ()) {
          lwl.push (recvInfo.getMin ());
        }

      }

    }

  };

  std::vector<SimObjInfo> sobjInfoVec;

protected:
  virtual std::string getVersion () const { return "Handwritten Ordered ODG, no barrier"; }

  virtual void initRemaining (const SimInit_ty& simInit, Graph& graph) {
    sobjInfoVec.clear ();
    sobjInfoVec.resize (graph.size ());

    for (Graph::iterator n = graph.begin ()
        , endn = graph.end (); n != endn; ++n) {

      SimObj_ty* so = static_cast<SimObj_ty*> (graph.getData (*n, Galois::MethodFlag::UNPROTECTED));
      sobjInfoVec[so->getID ()] = SimObjInfo (*n, so);
    }
  }


  virtual void runLoop (const SimInit_ty& simInit, Graph& graph) {

    std::vector<Event_ty> initWL;

    for (std::vector<Event_ty>::const_iterator i = simInit.getInitEvents ().begin ()
        , endi = simInit.getInitEvents ().end (); i != endi; ++i) {

      SimObj_ty* recvObj = static_cast<SimObj_ty*> (i->getRecvObj ());
      SimObjInfo& sinfo = sobjInfoVec[recvObj->getID ()];
      sinfo.recv (*i);

      if (sinfo.isMin (*i)) { // for eager add
        initWL.push_back (*i);
      }

      // std::cout << "initial event: " << i->detailedString () << std::endl
        // << "is_ready: " << sinfo.isReady (*i) << ", is_min: " << (sinfo.getMin () == *i) << std::endl;
    }

    AddList_ty newEvents;
    Accumulator_ty niter;
    Accumulator_ty nevents;
    size_t round = 0;

    while (true) {
      ++round;

      typedef Galois::WorkList::dChunkedFIFO<16> WL_ty;

      Galois::for_each(initWL.begin (), initWL.end (), 
                       OpFuncEagerAdd (graph, sobjInfoVec, newEvents, niter, nevents),
                       Galois::wl<WL_ty>());

      initWL.clear ();

      std::vector<SimObjInfo>::iterator minPos = getGlobalMin (sobjInfoVec);

      if (minPos == sobjInfoVec.end ()) {
        break;

      } else {
        initWL.push_back (minPos->getMin ());
      }
          
    }

    std::cout << "Number of rounds = " << round << std::endl;
    std::cout << "Number of iterations or attempts = " << 
      niter.reduce () << std::endl;
    std::cout << "Number of events processed= " << 
      nevents.reduce () << std::endl;
  }

};


} // namespace des_ord
#endif // DES_ORDERED_EXP_H
