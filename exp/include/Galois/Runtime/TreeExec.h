
#include "Galois/config.h"
#include "Galois/Accumulator.h"
#include "Galois/Atomic.h"
#include "Galois/gdeque.h"
#include "Galois/PriorityQueue.h"
#include "Galois/Timer.h"

#include "Galois/optional.h"
// #include "Galois/GaloisUnsafe.h"
#include "Galois/Runtime/Context.h"
#include "Galois/Runtime/DoAll.h"
#include "Galois/Runtime/ForEachTraits.h"
#include "Galois/Runtime/LCordered.h"
#include "Galois/Runtime/ParallelWork.h"
#include "Galois/Runtime/PerThreadWorkList.h"
#include "Galois/Runtime/Support.h"
#include "Galois/Runtime/Termination.h"
#include "Galois/Runtime/ll/gio.h"
#include "Galois/Runtime/ll/ThreadRWlock.h"
#include "Galois/Runtime/mm/Mem.h"

#include "llvm/Support/CommandLine.h"


#include <atomic>

namespace Galois {
namespace Runtime {

template <typename T, typename DivFunc, typename ConqFunc, bool NEEDS_CHILDREN>
class TreeExecutorTwoFunc {

protected:

  class Task {
  public:
    enum Mode { DIVIDE, CONQUER };

  protected:

    GALOIS_ATTRIBUTE_ALIGN_CACHE_LINE Mode mode;
    T elem;
    Task* parent;
    std::atomic<unsigned> numChild;


    // std::atomic<unsigned> numChild;


  public:
    Task (const T& a, Task* p, const Mode& m)
      : mode (m), elem (a), parent (p), numChild (0)    
    {}

    void setNumChildren (unsigned c) {
      assert (c > 0);
      numChild = c;
    }

    bool processedLastChild (void) {
      assert (numChild > 0);
      return (--numChild == 0);
    }

    void incNumChild (void) { ++numChild; }

    unsigned getNumChild (void) const { return numChild; }

    Task* getParent () { return parent; }

    const T& getElem () const { return elem; }
    T& getElem () { return elem; }

    const Mode& getMode () const { return mode; }
    bool hasMode (const Mode& m) const { return mode == m; }
    void setMode (const Mode& m) { mode = m; }

  };



  static const unsigned CHUNK_SIZE = 2;
  typedef Galois::WorkList::AltChunkedLIFO<CHUNK_SIZE, Task*> WL_ty;
  typedef MM::FixedSizeAllocator<Task> TaskAlloc;
  typedef UserContextAccess<T> UserCtx;
  typedef PerThreadStorage<UserCtx> PerThreadUserCtx;



  template <typename C>
  class CtxWrapper: boost::noncopyable {
    TreeExecutorTwoFunc* executor;
    C& ctx;
    Task* parent;
    size_t numChild;

  public:
    CtxWrapper (TreeExecutorTwoFunc* executor, C& ctx, Task* parent):
      boost::noncopyable (),
      executor (executor),
      ctx (ctx),
      parent (parent),
      numChild (0)
    {}

    void spawn (const T& elem) {
      Task* child = executor->spawn (elem, parent);
      ctx.push (child);
      ++numChild;
    }

    size_t getNumChild (void) const { return numChild; }


    void sync (void) const {}
  };

  // class CtxWrapper: boost::noncopyable {
    // TreeExecutorTwoFunc* executor;
    // Task* parent;
// 
  // public:
    // CtxWrapper (TreeExecutorTwoFunc* executor, Task* parent)
      // : boost::noncopyable (), executor (executor), parent (parent)
    // {}
// 
    // void spawn (const T& elem) {
      // executor->spawn (elem, parent);
    // }
    // 
  // };

  struct ApplyOperatorSinglePhase {
    typedef int tt_does_not_need_aborts;
    // typedef double tt_does_not_need_push;

    TreeExecutorTwoFunc* executor;
    TaskAlloc& taskAlloc;
    DivFunc& divFunc;
    ConqFunc& conqFunc;

    template <typename C>
    void operator () (Task* t, C& ctx) {

      if (t->hasMode (Task::DIVIDE)) {
        CtxWrapper<C> uctx {executor, ctx, t};
        // CtxWrapper uctx {executor, t};
        divFunc (t->getElem (), uctx);

        if (uctx.getNumChild () == 0) {
          t->setMode (Task::CONQUER);

        } else {
          t->setNumChildren (uctx.getNumChild ());
        }

        // if (t->getNumChild () == 0) {
          // t->setMode (Task::CONQUER);
        // }
      } // end outer if

      if (t->hasMode (Task::CONQUER)) {
        conqFunc (t->getElem());

        Task* parent = t->getParent ();
        if (parent != nullptr && parent->processedLastChild()) {
          parent->setMode (Task::CONQUER);
          ctx.push (parent);
          // executor->push (parent);
        }

        // task can be deallocated now
        taskAlloc.destroy (t);
        taskAlloc.deallocate (t, 1);
      }

    }
  };

  // void push (Task* t) {
    // workList.push (t);
  // }

  Task* spawn (const T& elem, Task* parent) {
    // parent->incNumChild ();
    Task* child = taskAlloc.allocate (1);
    assert (child != nullptr);
    taskAlloc.construct (child, elem, parent, Task::DIVIDE);
    // workList.push (child);
    return child;
  }



  DivFunc divFunc;
  ConqFunc conqFunc;
  std::string loopname;
  TaskAlloc taskAlloc;
  // WL_ty workList;


public:

  TreeExecutorTwoFunc (const DivFunc& divFunc, const ConqFunc& conqFunc, const char* loopname)
    : 
      divFunc (divFunc),
      conqFunc (conqFunc),
      loopname (loopname)
  {}

  void execute (const T& initItem) {

    Task* initTask = taskAlloc.allocate (1); 
    taskAlloc.construct (initTask, initItem, nullptr, Task::DIVIDE);

    // workList.push (initTask);
// 
    // Galois::for_each_wl (workList,
        // ApplyOperatorSinglePhase {this, taskAlloc, divFunc, conqFunc},
        // loopname.c_str ());

    Task* a[] = {initTask};

    Galois::Runtime::for_each_impl<WL_ty> (
        makeStandardRange (&a[0], &a[1]), 
        ApplyOperatorSinglePhase {this, taskAlloc, divFunc, conqFunc},
        loopname.c_str ());

    // initTask deleted in ApplyOperatorSinglePhase,
  }


};
template <typename T, typename DivFunc, typename ConqFunc>
void for_each_ordered_tree (const T& initItem, const DivFunc& divFunc, const ConqFunc& conqFunc, const char* loopname=nullptr) {

  TreeExecutorTwoFunc<T, DivFunc, ConqFunc, false> executor (divFunc, conqFunc, loopname);
  executor.execute (initItem);
}



// template <typename E>
// struct CtxWrapper {
  // TreeExecStack<
  // Task* parent;
// };


template <typename F>
class TreeExecStack {

protected:

  struct Task {
    GALOIS_ATTRIBUTE_ALIGN_CACHE_LINE std::atomic<unsigned> numChild;
    Task* parent;

    explicit Task (Task* parent): numChild (0), parent (parent)
    {}
  };

  typedef std::pair<Task*, F*> WorkItem;
  static const unsigned CHUNK_SIZE = 2;
  typedef WorkList::AltChunkedLIFO<CHUNK_SIZE, WorkItem> WL_ty;
  // typedef WorkList::AltChunkedFIFO<CHUNK_SIZE, WorkItem> WL_ty;

public:

  class CtxWrapper : private boost::noncopyable {
    TreeExecStack* executor;
    Task* parent;

  public:
    CtxWrapper (TreeExecStack* executor, Task* parent)
      : boost::noncopyable (), executor (executor), parent (parent)
    {}

    void spawn (F& f) {
      executor->spawn (f, parent);
    }

    void sync () {
      executor->syncLoop (*this);
    }

    unsigned getNumChild (void) const { 
      return parent->numChild;
    }
  };

protected:
  struct PerThreadData {
    // most frequently accessed members first
    size_t stat_iterations;
    size_t stat_pushes;
    bool didWork;
    const char* loopname;

    PerThreadData (const char* loopname): 
      stat_iterations (0), 
      stat_pushes (0) ,
      didWork (false), 
      loopname (loopname)

    {}

    void reportStats (void) {
      reportStat(loopname, "Pushes", stat_pushes);
      reportStat(loopname, "Iterations", stat_iterations);
    }
  };

  void spawn (F& f, Task* parent) {
    ++(parent->numChild);
    push (WorkItem (parent, &f));
  }

  void push (const WorkItem& p) {
    workList.push (p);
    PerThreadData& ptd = *(perThreadData.getLocal ());
    ++(ptd.stat_pushes);
  }

  void syncLoop (CtxWrapper& ctx) {
    while (ctx.getNumChild () != 0) {
      applyOperatorRecursive ();
    }
  }

  void applyOperatorRecursive () {
    Galois::optional<WorkItem> funcNparent = workList.pop ();

    if (funcNparent) {
      PerThreadData& ptd = *(perThreadData.getLocal ());
      ++(ptd.stat_iterations);

      if (!ptd.didWork) {
        ptd.didWork = true;
      }

      Task task {funcNparent->first};

      CtxWrapper ctx {this, &task};

      funcNparent->second->operator () (ctx);

      Task* parent = funcNparent->first;

      if (parent != nullptr) {
        --(parent->numChild);
      }
    }
  }

  const char* loopname;  
  PerThreadStorage<PerThreadData> perThreadData;
  TerminationDetection& term;
  WL_ty workList;

public:
  TreeExecStack (const char* loopname): 
    loopname (loopname), 
    perThreadData (loopname), 
    term (getSystemTermination ()) 
  {}

  void initThread (void) {
    term.initializeThread ();
  }

  void initWork (F& initTask) {
    push (WorkItem (nullptr, &initTask));
  }

  void operator () (void) {
    PerThreadData& ptd = *(perThreadData.getLocal ());
    do {
      ptd.didWork = false;

      applyOperatorRecursive ();

      term.localTermination (ptd.didWork);
      LL::asmPause (); // Take a breath, let the token propagate
    } while (!term.globalTermination ());

    ptd.reportStats ();
  }

};

template <typename F> 
void for_each_ordered_tree_impl (F& initTask, const char* loopname=nullptr) {
  assert (initTask != nullptr);

  TreeExecStack<F> e (loopname);

  e.initWork (initTask);

  getSystemThreadPool ().run (Galois::getActiveThreads (),
      [&e] (void) { e.initThread (); },
      std::ref (e));
}
class TreeTaskBase;

typedef TreeExecStack<TreeTaskBase>::CtxWrapper TreeTaskContext;

class TreeTaskBase {
public:
  virtual void operator () (TreeTaskContext& ctx) = 0;
};

template <typename F>
void for_each_ordered_tree (F& initTask, const char* loopname=nullptr) {
  for_each_ordered_tree_impl<F> (initTask, loopname);
}

void for_each_ordered_tree_generic (TreeTaskBase& initTask, const char* loopname=nullptr); 
} // end namespace Runtime
} // end namespace Galois
