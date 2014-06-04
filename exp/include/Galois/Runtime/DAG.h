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
 
#ifndef GALOIS_RUNTIME_DAG_H
#define GALOIS_RUNTIME_DAG_H

#include "Galois/config.h"
#include "Galois/Accumulator.h"
#include "Galois/Atomic.h"
#include "Galois/gdeque.h"
#include "Galois/PriorityQueue.h"
#include "Galois/Timer.h"

#include "Galois/Runtime/Context.h"
#include "Galois/Runtime/DoAll.h"
#include "Galois/Runtime/LCordered.h"
#include "Galois/Runtime/ParallelWork.h"
#include "Galois/Runtime/PerThreadWorkList.h"
#include "Galois/Runtime/ll/gio.h"
#include "Galois/Runtime/ll/ThreadRWlock.h"
#include "Galois/Runtime/mm/Mem.h"


#include <atomic>

namespace Galois {
namespace Runtime {


template <typename Ctxt>
struct DAGnhoodItem: public LockManagerBase {

public:
  typedef LockManagerBase Base;
  typedef Galois::ThreadSafeOrderedSet<Ctxt*, std::less<Ctxt*> > SharerSet;

  Lockable* lockable;
  SharerSet sharers;

public:
  explicit DAGnhoodItem (Lockable* l): lockable (l) {}

  void addSharer (Ctxt* ctx) {
    sharers.push (ctx);
  }

  bool tryMappingTo (Lockable* l) {
    return Base::CASowner (l, NULL);
  }

  void clearMapping () {
    // release requires having owned the lock
    bool r = Base::tryLock (lockable);
    assert (r);
    Base::release (lockable);
  }

  // just for debugging
  const Lockable* getMapping () const {
    return lockable;
  }

  static DAGnhoodItem* getOwner (Lockable* l) {
    LockManagerBase* o = LockManagerBase::getOwner (l);
    // assert (dynamic_cast<DAGnhoodItem*> (o) != nullptr);
    return static_cast<DAGnhoodItem*> (o);
  }

  struct Factory {

    typedef DAGnhoodItem<Ctxt> NItem;
    typedef Galois::Runtime::MM::FSBGaloisAllocator<NItem> NItemAlloc;

    NItemAlloc niAlloc;

    NItem* create (Lockable* l) {
      NItem* ni = niAlloc.allocAndConstruct (ni, l);
      assert (ni != nullptr);
      return ni;
    }

    void destroy (NItem* ni) {
      // delete ni; ni = NULL;
      niAlloc.destroyAndFree (ni);
    }
  };
  
};

template <typename T>
struct DAGcontext: public SimpleRuntimeContext {

  typedef DAGnhoodItem<DAGcontext> NItem;
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

  GALOIS_ATTRIBUTE_PROF_NOINLINE virtual void subAcquire (Lockable* l) {
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


template <typename T, typename Cmp, typename OpFunc, typename NhoodFunc>
class DAGexecutor {

protected:
  typedef DAGcontext<T>  Ctxt;
  typedef typename Ctxt::NhoodMgr NhoodMgr;
  typedef typename Ctxt::NItem NItem;

  typedef Galois::Runtime::MM::FSBGaloisAllocator<Ctxt> CtxtAlloc;
  typedef Galois::Runtime::PerThreadVector<Ctxt*> CtxtWL;
  typedef Galois::Runtime::UserContextAccess<T> UserCtx;
  typedef Galois::Runtime::PerThreadStorage<UserCtx> PerThreadUserCtx;


  struct ApplyOperator {

    typedef int tt_does_not_need_aborts;

    OpFunc& opFunc;
    PerThreadUserCtx& userCtxts;

    explicit ApplyOperator (OpFunc& opFunc, PerThreadUserCtx& userCtxts)
      : opFunc (opFunc), userCtxts (userCtxts)
    {}

    template <typename W>
    void operator () (Ctxt* src, W& wl) {
      assert (src->isSrc ());

      UserCtx& uctx = *(userCtxts.getLocal ());
      opFunc (src->getElem (), uctx);

      for (auto i = src->neighbor_begin (), i_end = src->neighbor_end ();
          i != i_end; ++i) {

        if ((*i)->removeLastInNeigh (src)) {
          wl.push (*i);
        }
      }
    }
  };



  Cmp cmp;
  NhoodFunc nhVisitor;
  OpFunc opFunc;
  NhoodMgr nhmgr;

  CtxtAlloc ctxtAlloc;
  CtxtWL allCtxts;
  CtxtWL initSources;
  PerThreadUserCtx userCtxts;

public:

  DAGexecutor (
      const Cmp& cmp, 
      const NhoodFunc& nhVisitor, 
      const OpFunc& opFunc)
    :
      cmp (cmp),
      nhVisitor (nhVisitor),
      opFunc (opFunc),
      nhmgr (typename NItem::Factory ())
  {}

  ~DAGexecutor (void) {
    Galois::Runtime::do_all_impl (makeLocalRange (allCtxts),
        [this] (Ctxt* ctx) {
          ctxtAlloc.destroyAndFree (ctx);
        }, "free_ctx");
  }

  void createEdge (Ctxt* a, Ctxt* b) {
    assert (a != nullptr);
    assert (b != nullptr);

    // a < b ? a : b
    Ctxt* src = cmp (a->getElem () , b->getElem ()) ? a : b;
    Ctxt* dst = (src == a) ? b : a;

    // avoid adding same edge multiple times
    if (src->addOutNeigh (dst)) {
      dst->addInNeigh (src);
    }
  }


  template <typename R>
  void initialize (const R& range) {


    // 
    // 1. create contexts and expand neighborhoods and create graph nodes
    // 2. go over nhood items and create edges
    // 3. Find initial sources and run for_each
    //

    Galois::StatTimer t_init ("Time to create the DAG: ");

    t_init.start ();
    Galois::Runtime::do_all_impl (range,
        [this] (const T& x) {
          Ctxt* ctx = ctxtAlloc.allocAndConstruct (x, nhmgr);
          assert (ctx != NULL);

          allCtxts.get ().push_back (ctx);

          Galois::Runtime::setThreadContext (ctx);

          UserCtx& uctx = *(userCtxts.getLocal ());
          nhVisitor (ctx->getElem (), uctx);
          Galois::Runtime::setThreadContext (NULL);
        }, "create_ctxt");


    Galois::Runtime::do_all_impl(nhmgr.getAllRange(),
        [this] (NItem* nitem) {
          for (auto i = nitem->sharers.begin ()
            , i_end = nitem->sharers.end (); i != i_end; ++i) {

            auto j = i;
            ++j;
            for (; j != i_end; ++j) {
              createEdge (*i, *j);
            }
          }
        }, "create_ctxt_edges", true);

    Galois::Runtime::do_all_impl (makeLocalRange (allCtxts),
        [this] (Ctxt* ctx) {
          ctx->finalizeAdj ();
          if (ctx->isSrc ()) {
            initSources.get ().push_back (ctx);
          }
        }, "finalize", true);

    t_init.stop ();
  }

  void execute (void) {

    StatTimer t_exec ("Time to execute the DAG: ");

    const unsigned CHUNK_SIZE = OpFunc::CHUNK_SIZE;
    typedef Galois::WorkList::dChunkedFIFO<CHUNK_SIZE, Ctxt*> SrcWL_ty;


    t_exec.start ();

    Galois::Runtime::for_each_impl<SrcWL_ty> ( 
        Galois::Runtime::makeLocalRange (initSources),
        ApplyOperator (opFunc, userCtxts), "apply_operator");

    t_exec.stop ();
  }

  void resetDAG (void) {
    Galois::StatTimer t_reset ("Time to reset the DAG: ");

    t_reset.start ();
    Galois::Runtime::do_all_impl (makeLocalRange (allCtxts),
        [] (Ctxt* ctx) {
          ctx->reset();
        },
       "reset_dag", true);
    t_reset.stop ();
  }

};

template <typename R, typename Cmp, typename OpFunc, typename NhoodFunc>
DAGexecutor<typename R::value_type, Cmp, OpFunc, NhoodFunc> make_dag_executor (const R& range, const Cmp& cmp, const NhoodFunc& nhVisitor, const OpFunc& opFunc, const char* loopname=nullptr) {

  return new DAGexecutor<typename R::value_type, Cmp, OpFunc, NhoodFunc> (cmp, nhVisitor, opFunc);
}

template <typename R, typename Cmp, typename OpFunc, typename NhoodFunc>
void destroy_dag_executor (DAGexecutor<typename R::value_type, Cmp, OpFunc, NhoodFunc>*& exec_ptr) {
  delete exec_ptr; exec_ptr = nullptr;
}

template <typename R, typename Cmp, typename OpFunc, typename NhoodFunc>
void for_each_ordered_dag (const R& range, const Cmp& cmp, const NhoodFunc& nhVisitor, const OpFunc& opFunc, const char* loopname=nullptr) {

  typedef typename R::value_type T;
  typedef DAGexecutor<T, Cmp, OpFunc, NhoodFunc> Exec_ty;
  
  Exec_ty exec (cmp, nhVisitor, opFunc);

  exec.initialize (range);

  exec.execute ();

}

template <typename T, typename DivFunc, typename ConqFunc>
class DivideAndConquerExecutor {

protected:


  class Task {

  protected:
    // std::atomic<unsigned> numChild;
    GALOIS_ATTRIBUTE_ALIGN_CACHE_LINE std::atomic<unsigned> numChild;
    T elem;
    Task* parent;

  public:
    Task (const T& a, Task* p)
      : numChild (0), elem (a), parent (p)
    {}

    void setNumChildren (unsigned c) {
      assert (c > 0);
      numChild = c;
    }

    bool removedLastChild (void) {
      assert (numChild > 0);
      return (--numChild == 0);
    }

    Task* getParent () { 
      return parent;
    }

    const T& getElem () const { return elem; }
    T& getElem () { return elem; }

  };

  struct BiModalTask: public Task {
    enum Mode { DIVIDE, CONQUER };
    Mode mode;

    BiModalTask (const T& e, BiModalTask* p, const Mode& m)
      : Task (e, p), mode (m)
    {}

    BiModalTask* getParent () {
      return static_cast<BiModalTask*> (Task::getParent ());
    }

  };


  typedef Galois::Runtime::MM::FSBGaloisAllocator<Task> TaskAlloc;
  typedef Galois::Runtime::MM::FSBGaloisAllocator<BiModalTask> BiModalTaskAlloc;
  typedef Galois::Runtime::UserContextAccess<T> UserCtx;
  typedef Galois::Runtime::PerThreadStorage<UserCtx> PerThreadUserCtx;

  struct ApplyOperatorSinglePhase {
    typedef int tt_does_not_need_aborts;

    BiModalTaskAlloc& taskAlloc;
    PerThreadUserCtx& userCtxts;
    DivFunc& divFunc;
    ConqFunc& conqFunc;

#if defined(__INTEL_COMPILER) && __INTEL_COMPILER <= 1310
    ApplyOperatorSinglePhase (
        BiModalTaskAlloc& taskAlloc,
        PerThreadUserCtx& userCtxts,
        DivFunc& divFunc,
        ConqFunc& conqFunc)
      :
        taskAlloc (taskAlloc),
        userCtxts (userCtxts),
        divFunc (divFunc),
        conqFunc (conqFunc)
    {}
#endif

    template <typename C>
    void operator () (BiModalTask* t, C& ctx) {

      if (t->mode == BiModalTask::DIVIDE) {
        UserCtx& uctx = *userCtxts.getLocal ();
        uctx.reset ();
        divFunc (t->getElem (), uctx);

        bool hasChildren = uctx.getPushBuffer().begin () != uctx.getPushBuffer ().end ();

        if (hasChildren) {
          ptrdiff_t numChild = std::distance (uctx.getPushBuffer ().begin (), uctx.getPushBuffer ().end ());

          t->setNumChildren (numChild);

          unsigned i = 0;
          for (auto c = uctx.getPushBuffer ().begin (), c_end = uctx.getPushBuffer ().end (); 
              c != c_end; ++c, ++i) {

            BiModalTask* child = taskAlloc.allocAndConstruct (*c, t, BiModalTask::DIVIDE);
            ctx.push (child);
          }
        } else { 
          // no children, so t is a leaf task
          t->mode = BiModalTask::CONQUER;
        }
      } // end outer if

      if (t->mode == BiModalTask::CONQUER) {
        conqFunc (t->getElem());

        BiModalTask* parent = t->getParent ();
        if (parent != nullptr && parent->removedLastChild()) {
          parent->mode = BiModalTask::CONQUER;
          ctx.push (parent);
        }

        // task can be deallocated now
        taskAlloc.destroyAndFree (t);
      }

    }
  };


  template <typename CWL>
  struct ApplyDivide {
    typedef int tt_does_not_need_aborts;

    TaskAlloc& taskAlloc;
    PerThreadUserCtx& userCtxts;
    CWL& conqWL;
    DivFunc& divFunc;
    
#if defined(__INTEL_COMPILER) && __INTEL_COMPILER <= 1310
    ApplyDivide (
        TaskAlloc& taskAlloc,
        PerThreadUserCtx& userCtxts,
        CWL& conqWL,
        DivFunc& divFunc)
      :
        taskAlloc (taskAlloc),
        userCtxts (userCtxts),
        conqWL (conqWL),
        divFunc (divFunc)
    {}
#endif


    template <typename C>
    void operator () (Task* t, C& ctx) {

      UserCtx& uctx = *userCtxts.getLocal ();
      uctx.reset ();
      divFunc (t->getElem(), uctx);

      bool hasChildren = uctx.getPushBuffer().begin () != uctx.getPushBuffer ().end ();

      if (hasChildren) {
        ptrdiff_t numChild = std::distance (uctx.getPushBuffer ().begin (), uctx.getPushBuffer ().end ());

        t->setNumChildren (numChild);

        unsigned i = 0;
        for (auto c = uctx.getPushBuffer ().begin (), c_end = uctx.getPushBuffer ().end (); 
            c != c_end; ++c, ++i) {

          Task* child = taskAlloc.allocAndConstruct (*c, t);
          ctx.push (child);
        }
      } else { 
        conqWL.push (t);
      }
    }


  };

  struct ApplyConquer {
    typedef int tt_does_not_need_aborts;

    PerThreadUserCtx& userCtxts;
    ConqFunc& conqFunc;

#if defined(__INTEL_COMPILER) && __INTEL_COMPILER <= 1310
    ApplyConquer (
        PerThreadUserCtx& userCtxts,
        ConqFunc& conqFunc)
      :
        userCtxts (userCtxts),
        conqFunc (conqFunc)
    {}
#endif


    template <typename C>
    void operator () (Task* t, C& ctx) {
      UserCtx& uctx = *userCtxts.getLocal ();
      uctx.reset ();
      conqFunc (t->getElem());

      Task* parent = t->getParent ();
      if (parent != nullptr && parent->removedLastChild()) {
        ctx.push (parent);
      }
    }
  };



  DivFunc divFunc;
  ConqFunc conqFunc;
  std::string loopname;
  PerThreadUserCtx userCtxts;

  static const unsigned CHUNK_SIZE = 4;
public:

  DivideAndConquerExecutor (const DivFunc& divFunc, const ConqFunc& conqFunc, const char* loopname)
    : 
      divFunc (divFunc),
      conqFunc (conqFunc),
      loopname (loopname)
  {}

  void execute_1p (const T& initItem) {

    typedef Galois::gdeque<BiModalTask*, 64> TaskWL;
    typedef Galois::WorkList::dChunkedFIFO<CHUNK_SIZE, BiModalTask*> WL_ty;

    BiModalTaskAlloc taskAlloc;

    BiModalTask* t = taskAlloc.allocAndConstruct (initItem, nullptr, BiModalTask::DIVIDE);

    BiModalTask* init[] = { t };

    Galois::Runtime::for_each_impl<WL_ty> (
        makeStandardRange (init, init + 1), 
#if defined(__INTEL_COMPILER) && __INTEL_COMPILER <= 1310
        ApplyOperatorSinglePhase (taskAlloc, userCtxts, divFunc, conqFunc),
#else
        ApplyOperatorSinglePhase {taskAlloc, userCtxts, divFunc, conqFunc},
#endif
        loopname.c_str ());

    taskAlloc.destroyAndFree (t);
  }

  void execute_2p (const T& initItem) {
    typedef Galois::WorkList::dChunkedFIFO<CHUNK_SIZE, Task*> WL_ty;

    TaskAlloc taskAlloc;

    // Galois::Runtime::do_all_impl (range, 
        // [&] (const T& a) {
          // Task* t = taskAlloc.allocAndConstruct (a, nullptr);
          // initialTasks.get ().push_back (t);
// 
        // },
        // "initial_tasks_gen");

    Task* initTask = taskAlloc.allocAndConstruct (initItem, nullptr);

    WL_ty conqWL;

    std::string div_loop_name = loopname + ":divide_phase";

    Task* init[] = { initTask };

    Galois::Runtime::for_each_impl<WL_ty> (makeStandardRange (init, init + 1),
#if defined(__INTEL_COMPILER) && __INTEL_COMPILER <= 1310
        ApplyDivide<WL_ty> (taskAlloc, userCtxts, conqWL, divFunc),
#else
        ApplyDivide<WL_ty> {taskAlloc, userCtxts, conqWL, divFunc}, 
#endif

        div_loop_name.c_str ());

    std::string conq_loop_name = loopname + ":conquer_phase";

    Galois::for_each_wl (conqWL, 
#if defined(__INTEL_COMPILER) && __INTEL_COMPILER <= 1310
        ApplyConquer (userCtxts, conqFunc), 
#else
        ApplyConquer {userCtxts, conqFunc}, 
#endif
        conq_loop_name.c_str ());

    taskAlloc.destroyAndFree (initTask);

  }

};

template <typename T, typename DivFunc, typename ConqFunc>
void for_each_ordered_tree_1p (const T& initItem, const DivFunc& divFunc, const ConqFunc& conqFunc, const char* loopname=nullptr) {

  DivideAndConquerExecutor<T, DivFunc, ConqFunc> executor (divFunc, conqFunc, loopname);
  executor.execute_1p (initItem);
}

template <typename T, typename DivFunc, typename ConqFunc>
void for_each_ordered_tree_2p (const T& initItem, const DivFunc& divFunc, const ConqFunc& conqFunc, const char* loopname=nullptr) {

  DivideAndConquerExecutor<T, DivFunc, ConqFunc> executor (divFunc, conqFunc, loopname);

  executor.execute_2p (initItem);

}

} // end namespace Runtime
} // end namespace Galois


#endif // GALOIS_RUNTIME_DAG_H
