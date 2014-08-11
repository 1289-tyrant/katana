#ifndef GALOIS_RUNTIME_COUPLED_EXECUTOR_H
#define GALOIS_RUNTIME_COUPLED_EXECUTOR_H

#include "Galois/GaloisUnsafe.h"

#include "Galois/Runtime/Executor_DoAll.h"
#include "Galois/Runtime/PerThreadWorkList.h"
#include "Galois/WorkList/WorkListWrapper.h"
#include "Galois/WorkList/ExternRef.h"

namespace Galois {
namespace Runtime {

enum ExecType {
  DOALL_WAKEUP=0,
  DOALL_EXPLICIT,
};

static const bool DO_STEAL = true;

static const char* EXEC_NAMES[] = { "DOALL_WAKEUP", "DOALL_EXPLICIT" };

static cll::opt<ExecType> execType (
    cll::desc ("Executor type"),
    cll::values (
      clEnumVal (DOALL_WAKEUP, "Wake up thread pool"),
      clEnumVal (DOALL_EXPLICIT, "Explicit parallel loop"),
      clEnumValEnd),
    cll::init (DOALL_WAKEUP));


template <typename R, typename F>
void do_all_coupled_explicit (const R& initRange, const F& func, const char* loopname=nullptr) {

  typedef typename R::value_type T;
  typedef PerThreadVector<T> WL_ty;

  WL_ty* curr = new WL_ty ();
  WL_ty* next = new WL_ty ();

  F func_cpy (func);

  auto funcWrapper = 
    [&func_cpy, &next] (const T& x) {
      func_cpy (x, next->get ());
    };

  auto dummyRange = makeLocalRange (*curr);
  detail::DoAllWork<decltype(funcWrapper), decltype(dummyRange)> exec (funcWrapper, dummyRange, nullptr);

  Galois::Runtime::Barrier& barrier = Galois::Runtime::getSystemBarrier ();

  volatile bool done = false;

  auto loop = [&] (void) {

    auto rp = initRange.local_pair ();
    for (auto i = rp.first, i_end = rp.second; i != i_end; ++i) {
      next->get ().push_back (*i);
    }

    barrier ();

    while (true) {

      if (LL::getTID () == 0) {
        std::swap (curr, next);
        if (curr->empty_all ()) {
          done = true;
        }
      }

      barrier ();

      if (done) { break; }

      exec.reinit (makeLocalRange (*curr));

      barrier ();

      next->get ().clear ();

      exec ();

      barrier ();

    }
  };

  getSystemThreadPool ().run (Galois::getActiveThreads (), loop);

  delete curr;
  delete next;
}

template <typename R, typename F>
void do_all_coupled_wake (const R& initRange, const F& func, const char* loopname=nullptr) {

  typedef typename R::value_type T;
  typedef PerThreadVector<T> WL_ty;

  WL_ty* curr = new WL_ty ();
  WL_ty* next = new WL_ty ();

  getSystemThreadPool ().burnPower (Galois::getActiveThreads ());

  Galois::Runtime::on_each_impl (
      [&initRange, &next] (const unsigned tid, const unsigned numT) {
        auto rp = initRange.local_pair ();
        for (auto i = rp.first, i_end = rp.second; i != i_end; ++i) {
          next->get ().push_back (*i);
        }
      });

  // std::printf ("Initial size: %zd\n", next->size_all ());
  F func_cpy (func);

  while (!next->empty_all ()) {
    std::swap (curr, next);

    Galois::Runtime::on_each_impl (
        [&next] (const unsigned tid, const unsigned numT) {
          next->get ().clear ();
        });

    // std::printf ("Current size: %zd\n", curr->size_all ());

    Galois::Runtime::do_all_impl (
        makeLocalRange (*curr),
        [&func_cpy, &next] (const T& t) {
          func_cpy (t, next->get ());
        },
        "do_all_bs",
        DO_STEAL);


  }

  getSystemThreadPool ().beKind ();

  delete curr;
  delete next;
}

template <typename R, typename F>
void do_all_coupled_bs (const R& initRange, const F& func, const char* loopname=nullptr) {

  std::printf ("Running do_all_coupled_bs with executor: %s\n", EXEC_NAMES[execType]);

  switch (execType) {
    case DOALL_WAKEUP:
      do_all_coupled_wake (initRange, func, loopname);
      break;

    case DOALL_EXPLICIT:
      do_all_coupled_explicit (initRange, func, loopname);
      break;

    default:
      std::abort ();

  } 

}

namespace impl {

template <typename F, typename WL>
struct FunctorWrapper {

  typedef int tt_does_not_need_abort;
  typedef char tt_does_not_need_push;

  F& func;
  WL*& wl;

  typedef typename WL::value_type T;

  explicit FunctorWrapper (F& func, WL*& wl): func (func), wl (wl) {}

  template <typename C>
  void operator () (const T& x, C&) {
    func (x, *wl);
  }

};
} // end namespace impl

template <typename R, typename F>
void for_each_coupled_wake (const R& initRange, const F& func, const char* loopname=nullptr) {

  const unsigned CHUNK_SIZE = 64;

  typedef typename R::value_type T;

  typedef WorkList::WLsizeWrapper<typename Galois::WorkList::dChunkedFIFO<CHUNK_SIZE>::template retype<T>::type> WL_ty;

  WL_ty* curr = new WL_ty ();
  WL_ty* next = new WL_ty ();

  F func_cpy (func);

  getSystemThreadPool ().burnPower (Galois::getActiveThreads ());

  Galois::Runtime::on_each_impl (
      [&next, &initRange] (const unsigned tid, const unsigned numT) {
        next->push_initial (initRange);
      });


  while (next->size () != 0) {
    std::swap (curr, next);
    next->reset_all ();

    Galois::for_each_wl ( *curr,
        impl::FunctorWrapper<F, WL_ty> (func_cpy, next),
        "for_each_coupled");
  }

  getSystemThreadPool ().beKind ();

  delete curr; 
  delete next;
}

template <typename R, typename F>
void for_each_coupled_explicit (const R& initRange, const F& func, const char* loopname=nullptr) {

  const unsigned CHUNK_SIZE = 128;

  typedef typename R::value_type T;

  typedef WorkList::WLsizeWrapper<typename Galois::WorkList::dChunkedFIFO<CHUNK_SIZE>::template retype<T>::type> WL_ty;

  WL_ty* curr = new WL_ty ();
  WL_ty* next = new WL_ty ();

  F func_cpy (func);

  typedef impl::FunctorWrapper<F, WL_ty> FWrap;

  typedef Galois::Runtime::ForEachWork<WorkList::ExternPtr<WL_ty>, T, FWrap> ForEachExec_ty;

  ForEachExec_ty exec (curr, FWrap (func_cpy, next), loopname);

  Galois::Runtime::Barrier& barrier = Galois::Runtime::getSystemBarrier ();

  std::atomic<bool> done(false);

  auto loop = [&] (void) {

    next->push_initial (initRange);

    barrier ();

    while (true) {

      if (LL::getTID () == 0) {
        std::swap (curr, next);
        exec.reinit (curr);

        if (curr->size () == 0) { 
          done = true;
        }
      }

      exec.initThread ();

      barrier ();

      if (done) { break; }

      next->reset ();

      exec ();

      barrier ();

    }

  };
  

  getSystemThreadPool ().run (Galois::getActiveThreads (), loop);
  
  delete curr;
  delete next;

}


template <typename R, typename F>
void for_each_coupled_bs (const R& initRange, const F& func, const char* loopname=nullptr) {

  std::printf ("Running for_each_coupled_bs with executor: %s\n", EXEC_NAMES[execType]);

  switch (execType) {
    case DOALL_WAKEUP:
      for_each_coupled_wake (initRange, func, loopname);
      break;

    case DOALL_EXPLICIT:
      for_each_coupled_explicit (initRange, func, loopname);
      break;

    default:
      std::abort ();

  } 

}

} // end namespace Runtime
} // end namespace Galois


#endif // GALOIS_RUNTIME_COUPLED_EXECUTOR_H
