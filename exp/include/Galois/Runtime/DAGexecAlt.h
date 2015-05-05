/** TODO -*- C++ -*-
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
 * @section Description
 *
 * TODO 
 *
 * @author <ahassaan@ices.utexas.edu>
 */
 
#ifndef GALOIS_RUNTIME_DAGEXEC_ALT_H
#define GALOIS_RUNTIME_DAGEXEC_ALT_H

#include "Galois/config.h"
#include "Galois/GaloisForwardDecl.h"
#include "Galois/Accumulator.h"
#include "Galois/Atomic.h"
#include "Galois/gdeque.h"
#include "Galois/PriorityQueue.h"
#include "Galois/Timer.h"
#include "Galois/PerThreadContainer.h"

#include "Galois/Runtime/Context.h"
#include "Galois/Runtime/Executor_DoAll.h"
#include "Galois/Runtime/OrderedLockable.h"
#include "Galois/Runtime/ll/gio.h"
#include "Galois/Runtime/ll/ThreadRWlock.h"
#include "Galois/Runtime/mm/Mem.h"

#include "llvm/Support/CommandLine.h"

#include <atomic>

namespace Galois {
namespace Runtime {

namespace Exp {

template <typename Ctxt>
struct SharerSet {

public:
  using CtxtCmp = std::less<Ctxt*>;
  using Cont = Galois::ThreadSafeOrderedSet<Ctxt*, CtxtCmp>;

  Cont sharers;

  inline void addSharer (Ctxt* ctxt) {
    assert (std::find (sharers.begin (), sharers.end (), ctxt) == sharers.end ());
    sharers.push (ctxt);
  }

};

template <typename Ctxt>
struct SharerVec {

public:
  using Cont = typename ContainersWithGAlloc::Vector<Ctxt*>::type;

  LL::SimpleLock mutex;
  Cont sharers;

  inline void addSharer (Ctxt* ctxt) {
    mutex.lock ();
      assert (std::find (sharers.begin (), sharers.end (), ctxt) == sharers.end ());
      sharers.push_back (ctxt);
    mutex.unlock();
  }

};

template <typename Ctxt>
struct SharerList {
public:
  using Cont =  Galois::concurrent_gslist<Ctxt*, 16>;
  using FSHeap =  Galois::Runtime::MM::FixedSizeHeap;

  FSHeap heap;
  Cont sharers;

  void addSharer (Ctxt* ctxt) {
    assert (std::find (sharers.begin (), sharers.end (), ctxt) == sharers.end ());
    sharers.push_front (heap, ctxt);
  }
};

template <typename Ctxt, typename SharerWrapper>
struct DAGnhoodItem: public OrdLocBase<DAGnhoodItem<Ctxt, SharerWrapper>, Ctxt, std::less<Ctxt*> > {

  using CtxtCmp = std::less<Ctxt*>;
  using Base = OrdLocBase<DAGnhoodItem, Ctxt, CtxtCmp >;
  using HeadIter = typename SharerWrapper::Cont::iterator;
  using SharerCont = typename SharerWrapper::Cont;

  SharerWrapper wrapper;
  HeadIter head;
  HeadIter end;
  Lockable* lockable;

  explicit DAGnhoodItem (Lockable* l, const CtxtCmp& ctxtCmp): Base (l) {}

  void addSharer (Ctxt* ctxt) {
    wrapper.addSharer (ctxt);
  }

  void removeMin (Ctxt* ctxt) {
    if (head != end) {
      assert (*head == ctxt);
      ++head;
      assert (std::find (head, wrapper.sharers.end (), ctxt) == wrapper.sharers.end ());
    }
  }

  template <typename CtxtCmp> 
  void sortSharerSet (const CtxtCmp& cmp) {
    std::sort (wrapper.sharers.begin (), wrapper.sharers.end (), cmp);
    head = wrapper.sharers.begin ();
    end = wrapper.sharers.end ();
  }

  bool isMin (Ctxt* ctxt) const {
    if (head != end) {
      return ctxt == (*head);

    } else {
      return false;
    }
  }

  Ctxt* getMin (void) const {
    if (head == end) {
      return nullptr;

    } else {
      return *head;
    }
  }

  void reset (void) {
    head = wrapper.sharers.begin ();
    assert (end == wrapper.sharers.end ());
  }

};

template <typename T>
struct DAGcontext: public SimpleRuntimeContext {

  // typedef DAGnhoodItem<DAGcontext> NItem;
  using NItem = DAGnhoodItem<DAGcontext, SharerVec<DAGcontext> >;
  using NhoodMgr = PtrBasedNhoodMgr<NItem>;
  using NhoodSet = Galois::gdeque<NItem*, 8>;
  using AtomicBool = Galois::GAtomic<bool>;


  AtomicBool onWL;
  T elem;
  NhoodMgr& nhmgr;
  NhoodSet nhood;


  explicit DAGcontext (const T& t, NhoodMgr& nhmgr): 
    SimpleRuntimeContext {true},
    onWL {false},
    elem {t}, 
    nhmgr {nhmgr}
  {}

  GALOIS_ATTRIBUTE_PROF_NOINLINE
  virtual void subAcquire (Lockable* l, Galois::MethodFlag) {
    NItem& nitem = nhmgr.getNhoodItem (l);

    assert (NItem::getOwner (l) == &nitem);

    // enforcing set semantics
    if (std::find (nhood.begin (), nhood.end (), &nitem) == nhood.end ()) {
      nhood.push_back (&nitem);
      nitem.addSharer (this);
    }
  }

  void removeFromNhood (void) {
    assert (isSrc());
    for (auto i = nhood.begin (), end_i = nhood.end (); i != end_i; ++i) {
      assert ((*i)->getMin () == this);
      (*i)->removeMin (this);
    }
  }

  bool isSrc (void) const {
    
    if (nhood.empty ()) {
      // std::fprintf(stderr, "WARNING: isSrc() called with empty nhood\n");
      return true;
    }

    for (auto i = nhood.begin (), end_i = nhood.end (); i != end_i; ++i) {
      if ((*i)->getMin () != this) {
        return false;
      }
    }

    return true;
  }


  template <typename WL>
  size_t findNewSources (WL& workList) {

    size_t num = 0;
    for (auto i = nhood.begin (), end_i = nhood.end (); i != end_i; ++i) {

      DAGcontext* min = (*i)->getMin ();

      if (min != nullptr && 
          !bool (min->onWL) &&
          min->isSrc () &&
          min->onWL.cas (false, true)) {

        // GALOIS_DEBUG ("Adding found source: %s\n", min->str ().c_str ());
        workList.push (min);
        ++num;
      }
    }

    return num;
  }

  void reset (void) {}

  template <typename Cmp>
  struct Comparator {
    Cmp cmp;

    bool operator () (const DAGcontext* left, const DAGcontext* right) const {
      return cmp (left->elem, right->elem);
    }
  };

};

/*

template <typename T>
struct DAGcontext: public SimpleRuntimeContext {

  // typedef DAGnhoodItem<DAGcontext> NItem;
  typedef DAGnhoodItem<SharerList<DAGcontext> > NItem;
  typedef PtrBasedNhoodMgr<NItem> NhoodMgr;

protected:
  typedef Galois::ThreadSafeOrderedSet<DAGcontext*, std::less<DAGcontext*> > AdjSet;
  // TODO: change AdjList to array for quicker iteration
  typedef Galois::gdeque<DAGcontext*, 8> AdjList;
  typedef std::atomic<int> ParCounter;

  GALOIS_ATTRIBUTE_ALIGN_CACHE_LINE ParCounter inDeg;
  // ParCounter inDeg;
  const int origInDeg;
  NhoodMgr& nhmgr;
  T elem;

  AdjSet adjSet;
  AdjList outNeighbors;

public:
  explicit DAGcontext (const T& t, NhoodMgr& nhmgr): 
    SimpleRuntimeContext (true), // true to call subAcquire
    inDeg (0),
    origInDeg (0), 
    nhmgr (nhmgr),
    elem (t)
  {}

  const T& getElem () const { return elem; }

  GALOIS_ATTRIBUTE_PROF_NOINLINE
  virtual void subAcquire (Lockable* l, Galois::MethodFlag) {
    NItem& nitem = nhmgr.getNhoodItem (l);

    assert (NItem::getOwner (l) == &nitem);

    nitem.addSharer (this);
    
  }

  //! returns true on success
  bool addOutNeigh (DAGcontext* that) {
    return adjSet.push (that);
  }

  void addInNeigh (DAGcontext* that) {
    int* x = const_cast<int*> (&origInDeg);
    ++(*x);
    ++inDeg;
  }

  void finalizeAdj (void) {
    for (auto i = adjSet.begin (), i_end = adjSet.end (); 
        i != i_end; ++i) {

      outNeighbors.push_back (*i);
    }
  }

  bool removeLastInNeigh (DAGcontext* that) {
    assert (inDeg >= 0);
    return ((--inDeg) == 0);
  }

  bool isSrc (void) const {
    return inDeg == 0;
  }

  void reset (void) {
    inDeg = origInDeg;
  }

  typename AdjList::iterator neighbor_begin (void) {
    return outNeighbors.begin ();
  }

  typename AdjList::iterator neighbor_end (void) {
    return outNeighbors.end ();
  }

};
*/


template <typename T, typename Cmp, typename OpFunc, typename NhoodFunc>
class DAGexecutor {

protected:
  using Ctxt = DAGcontext<T>;
  using NhoodMgr = typename Ctxt::NhoodMgr;
  using NItem = typename Ctxt::NItem;
  using CtxtCmp = typename NItem::CtxtCmp;
  using NItemFactory = typename NItem::Factory;

  typedef MM::FixedSizeAllocator<Ctxt> CtxtAlloc;
  typedef PerThreadBag<Ctxt*> CtxtWL;
  typedef UserContextAccess<T> UserCtx;
  typedef PerThreadStorage<UserCtx> PerThreadUserCtx;


  struct ApplyOperator {

    typedef int tt_does_not_need_aborts;

    DAGexecutor& outer;

    template <typename W>
    void operator () (Ctxt* src, W& wl) {
      assert (src->isSrc ());
      // printf ("processing source: %p, item: %d\n", src, src->elem);

      UserCtx& uctx = *(outer.userCtxts.getLocal ());
      outer.opFunc (src->elem, uctx);

      src->removeFromNhood ();

      outer.numPush += src->findNewSources (wl);
    }
  };

  static const unsigned DEFAULT_CHUNK_SIZE = 16;

  Cmp cmp;
  NhoodFunc nhVisitor;
  OpFunc opFunc;
  NItemFactory nitemFactory;
  NhoodMgr nhmgr;

  CtxtAlloc ctxtAlloc;
  CtxtWL allCtxts;
  CtxtWL initSources;
  PerThreadUserCtx userCtxts;
  Galois::GAccumulator<size_t> numPush;

public:

  DAGexecutor (
      const Cmp& cmp, 
      const NhoodFunc& nhVisitor, 
      const OpFunc& opFunc)
    :
      cmp (cmp),
      nhVisitor (nhVisitor),
      opFunc (opFunc),
      nitemFactory (CtxtCmp ()),
      nhmgr (nitemFactory)
  {}

  ~DAGexecutor (void) {

    Galois::do_all_choice (Galois::Runtime::makeLocalRange (allCtxts),
        [this] (Ctxt* ctxt) {
          ctxtAlloc.destroy (ctxt);
          ctxtAlloc.deallocate (ctxt, 1);
        }, 
        "free_ctx", Galois::chunk_size<DEFAULT_CHUNK_SIZE> ());
  }


  template <typename R>
  void initialize (const R& range) {
    // 
    // 1. create contexts and expand neighborhoods and create graph nodes
    // 2. go over nhood items and create edges
    // 3. Find initial sources and run for_each
    //

    Galois::StatTimer t_init ("Time to create the DAG: ");

    std::printf ("total number of tasks: %ld\n", std::distance (range.begin (), range.end ()));

    t_init.start ();
    Galois::do_all_choice (range,
        [this] (const T& x) {
          Ctxt* ctxt = ctxtAlloc.allocate (1);
          assert (ctxt != NULL);
          ctxtAlloc.construct (ctxt, x, nhmgr);

          allCtxts.get ().push_back (ctxt);

          Galois::Runtime::setThreadContext (ctxt);

          UserCtx& uctx = *(userCtxts.getLocal ());
          nhVisitor (ctxt->elem, uctx);
          Galois::Runtime::setThreadContext (NULL);

          // printf ("Created context:%p for item: %d\n", ctxt, x);
        }, "create_ctxt", Galois::chunk_size<DEFAULT_CHUNK_SIZE> ());


    Galois::do_all_choice (nhmgr.getAllRange(),
        [this] (NItem* nitem) {
          nitem->sortSharerSet (typename Ctxt::template Comparator<Cmp> {cmp});
          // std::printf ("Nitem: %p, num sharers: %ld\n", nitem, nitem->sharers.size ());
        }, 
        "sort_sharers", Galois::chunk_size<DEFAULT_CHUNK_SIZE>());

    Galois::do_all_choice (Galois::Runtime::makeLocalRange (allCtxts),
        [this] (Ctxt* ctxt) {
          if (ctxt->isSrc ()) {
            ctxt->onWL = true;
            initSources.get ().push_back (ctxt);
          }
        }, 
        "find-init-sources", Galois::chunk_size<DEFAULT_CHUNK_SIZE>());

    std::printf ("Number of initial sources: %ld\n", std::distance (initSources.begin () , initSources.end ()));

    t_init.stop ();
  }

  void execute (void) {

    StatTimer t_exec ("Time to execute the DAG: ");

    const unsigned CHUNK_SIZE = OpFunc::CHUNK_SIZE;
    typedef Galois::WorkList::dChunkedFIFO<CHUNK_SIZE, Ctxt*> SrcWL_ty;



    t_exec.start ();

    Galois::for_each_local (initSources,
        ApplyOperator {*this}, Galois::loopname {"apply_operator"}, Galois::wl<SrcWL_ty>());

    std::printf ("Number of pushes: %zd\n, (#pushes + #init) = %zd\n", 
        numPush.reduceRO (), numPush.reduceRO () + initSources.size_all  ());
    
    t_exec.stop ();
  }

  void resetDAG (void) {
    Galois::StatTimer t_reset ("Time to reset the DAG: ");

    t_reset.start ();
    // Galois::do_all_choice (allCtxts,
        // [] (Ctxt* ctxt) {
          // ctxt->reset();
        // },
        // Galois::loopname("reset_dag"), Galois::do_all_steal<true>());
    Galois::do_all_choice (nhmgr.getAllRange (),
        [] (NItem* nitem) {
          nitem->reset();
        },
        "reset_dag", Galois::chunk_size<DEFAULT_CHUNK_SIZE> ());
        
    t_reset.stop ();
  }

};

} // end namespace Exp

template <typename R, typename Cmp, typename OpFunc, typename NhoodFunc>
Exp::DAGexecutor<typename R::value_type, Cmp, OpFunc, NhoodFunc> make_dag_executor_alt (const R& range, const Cmp& cmp, const NhoodFunc& nhVisitor, const OpFunc& opFunc, const char* loopname=nullptr) {

  return new Exp::DAGexecutor<typename R::value_type, Cmp, OpFunc, NhoodFunc> (cmp, nhVisitor, opFunc);
}

template <typename R, typename Cmp, typename OpFunc, typename NhoodFunc>
void destroy_dag_executor_alt (Exp::DAGexecutor<typename R::value_type, Cmp, OpFunc, NhoodFunc>*& exec_ptr) {
  delete exec_ptr; exec_ptr = nullptr;
}

template <typename R, typename Cmp, typename OpFunc, typename NhoodFunc>
void for_each_ordered_dag_alt (const R& range, const Cmp& cmp, const NhoodFunc& nhVisitor, const OpFunc& opFunc, const char* loopname=nullptr) {

  typedef typename R::value_type T;
  typedef Exp::DAGexecutor<T, Cmp, OpFunc, NhoodFunc> Exec_ty;
  
  Exec_ty exec (cmp, nhVisitor, opFunc);

  exec.initialize (range);

  exec.execute ();

}



} // end namespace Runtime
} // end namespace Galois


#endif // GALOIS_RUNTIME_DAGEXEC_ALT_H
