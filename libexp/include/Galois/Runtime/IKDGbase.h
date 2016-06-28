#ifndef GALOIS_RUNTIME_IKDG_BASE_H
#define GALOIS_RUNTIME_IKDG_BASE_H


#include "Galois/AltBag.h"
#include "Galois/OrderedTraits.h"

#include "Galois/Runtime/OrderedLockable.h"

#include <boost/iterator/filter_iterator.hpp>
#include <boost/iterator/filter_iterator.hpp>

#include <utility>
#include <functional>

namespace Galois {
namespace Runtime {

namespace cll = llvm::cl;

static cll::opt<double> commitRatioArg("cratio", cll::desc("target commit ratio for two phase executor, 0.0 to disable windowing"), cll::init(0.80));

// TODO: figure out when to call startIteration

template <typename Ctxt, typename S>
class SafetyTestLoop {

  using T = typename Ctxt::value_type;

  struct GetActive: public std::unary_function<Ctxt, const T&> {
    const T& operator () (const Ctxt* c) const {
      assert (c != nullptr);
      return c->getActive ();
    }
  };

  struct GetLesserThan: public std::unary_function<const Ctxt*, bool> {

    const Ctxt* curr;
    typename Ctxt::PtrComparator cmp = typename Ctxt::PtrComparator ();

    bool operator () (const Ctxt* that) const { 
      return cmp (that, curr); 
    }
  };

  S safetyTest;

  static const unsigned DEFAULT_CHUNK_SIZE = 2;

public:

  explicit SafetyTestLoop (const S& safetyTest): safetyTest (safetyTest) {}

  template <typename R>
  void run (const R& range) const {

    Galois::do_all_choice (range,
        [this, &range] (const Ctxt* c) {

          auto beg_lesser = boost::make_filter_iterator (
            range.begin (), range.end (), GetLesserThan {c});

          auto end_lesser = boost::make_filter_iterator (
            range.end (), range.end (), GetLesserThan {c});


          auto bt = boost::make_transform_iterator (beg_lesser, GetActive ());
          auto et = boost::make_transform_iterator (end_lesser, GetActive ());


          if (!safetyTest (c->getActive (), bt, et)) {
            c->disableSrc ();
          }
        },
        "safety_test_loop",
        Galois::chunk_size<DEFAULT_CHUNK_SIZE> ());
  }
};

template <typename Ctxt>
struct SafetyTestLoop<Ctxt, int> {

  SafetyTestLoop (int) {}

  template <typename R>
  void run (const R& range) const { 
  }
};


template <typename F, typename Ctxt, typename UserCtxt, typename... Args>
void runCatching (F& func, Ctxt* c, UserCtxt& uhand, Args&&... args) {
  Galois::Runtime::setThreadContext (c);

  int result = 0;

#ifdef GALOIS_USE_LONGJMP
  if ((result = setjmp(hackjmp)) == 0) {
#else
    try {
#endif
      func (c->getActive (), uhand, std::forward<Args> (args)...);

#ifdef GALOIS_USE_LONGJMP
    } else {
      // TODO
    }
#else 
  } catch (ConflictFlag f) {
    result = f;
  }
#endif

  switch (result) {
    case 0:
      break;
    case CONFLICT: 
      c->disableSrc ();
      break;
    default:
      GALOIS_DIE ("can't handle conflict flag type");
      break;
  }


  Galois::Runtime::setThreadContext (NULL);
}

template <typename T, typename Cmp, typename NhFunc, typename ExFunc, typename OpFunc, typename ArgsTuple, typename Ctxt>
class IKDGbase: public OrderedExecutorBase<T, Cmp, NhFunc, ExFunc, OpFunc, ArgsTuple, Ctxt> {

protected:

  using Base = OrderedExecutorBase<T, Cmp, NhFunc, ExFunc, OpFunc, ArgsTuple, Ctxt>;
  using CtxtWL = typename Base::CtxtWL;


  std::unique_ptr<CtxtWL> currWL;
  std::unique_ptr<CtxtWL> nextWL;


  size_t windowSize;
  size_t rounds;
  size_t totalTasks;
  size_t totalCommits;
  double targetCommitRatio;

  GAccumulator<size_t> roundTasks;;
  GAccumulator<size_t> roundCommits;

  IKDGbase (const Cmp& cmp, const NhFunc& nhFunc, const ExFunc& exFunc, const OpFunc& opFunc, const ArgsTuple& argsTuple)
    : 
      Base (cmp, nhFunc, exFunc, opFunc, argsTuple),
      currWL (new CtxtWL),
      nextWL (new CtxtWL),
      windowSize (0),
      rounds (0),
      totalTasks (0),
      totalCommits (0),
      targetCommitRatio (commitRatioArg)
  {

    if (targetCommitRatio < 0.0) {
      targetCommitRatio = 0.0;
    }
    if (targetCommitRatio > 1.0) {
      targetCommitRatio = 1.0;
    }

    if (Base::ENABLE_PARAMETER) {
      assert (targetCommitRatio == 0.0);
    }

  }

  ~IKDGbase (void) {
    dumpStats ();
  }


  CtxtWL& getCurrWL (void) { 
    assert (currWL);
    return *currWL; 
  }

  CtxtWL& getNextWL (void) {
    assert (nextWL);
    return *nextWL;
  }

  void dumpStats (void) {
    reportStat (Base::loopname, "rounds", rounds,0);
    reportStat (Base::loopname, "committed", totalCommits,0);
    reportStat (Base::loopname, "total", totalTasks,0);
    // reportStat (loopname, "efficiency", double (totalRetires.reduce ()) / totalTasks);
    // reportStat (loopname, "avg. parallelism", double (totalRetires.reduce ()) / rounds);

  }

  template <typename WinWL>
  GALOIS_ATTRIBUTE_PROF_NOINLINE void spillAll (WinWL& winWL, CtxtWL& wl) {

    dbg::print("Spilling to winWL");

    // TODO: fix this loop, change to do_all_choice
    assert (targetCommitRatio != 0.0);
    on_each(
        [this, &wl, &winWL] (const unsigned tid, const unsigned numT) {
          while (!wl.get ().empty ()) {
            Ctxt* c = wl.get ().back ();
            wl.get ().pop_back ();

            dbg::print("Spilling: ", c, " with active: ", c->getActive ());

            winWL.push (c);
          }
        });

    assert (wl.empty_all ());
    assert (!winWL.empty ());
  }

  template <typename WinWL>
  GALOIS_ATTRIBUTE_PROF_NOINLINE void refill (WinWL& winWL, CtxtWL& wl, size_t currCommits, size_t prevWindowSize) {

    assert (targetCommitRatio != 0.0);

    const size_t INIT_MAX_ROUNDS = 500;
    const size_t THREAD_MULT_FACTOR = 4;
    const double TARGET_COMMIT_RATIO = targetCommitRatio;
    const size_t MIN_WIN_SIZE = OpFunc::CHUNK_SIZE * getActiveThreads ();
    // const size_t MIN_WIN_SIZE = 2000000; // OpFunc::CHUNK_SIZE * getActiveThreads ();
    const size_t WIN_OVER_SIZE_FACTOR = 2;

    if (prevWindowSize == 0) {
      assert (currCommits == 0);

      // initial settings
      if (Base::NEEDS_PUSH) {
        windowSize = std::min (
            (winWL.initSize ()),
            (THREAD_MULT_FACTOR * MIN_WIN_SIZE));

      } else {
        windowSize = std::min (
            (winWL.initSize () / INIT_MAX_ROUNDS),
            (THREAD_MULT_FACTOR * MIN_WIN_SIZE));
      }
    } else {

      assert (windowSize > 0);

      double commitRatio = double (currCommits) / double (prevWindowSize);

      if (commitRatio >= TARGET_COMMIT_RATIO) {
        windowSize *= 2;
        // windowSize = int (windowSize * commitRatio/TARGET_COMMIT_RATIO); 
        // windowSize = windowSize + windowSize / 2;

      } else {
        windowSize = int (windowSize * commitRatio/TARGET_COMMIT_RATIO); 

        // if (commitRatio / TARGET_COMMIT_RATIO < 0.90) {
          // windowSize = windowSize - (windowSize / 10);
// 
        // } else {
          // windowSize = int (windowSize * commitRatio/TARGET_COMMIT_RATIO); 
        // }
      }
    }

    if (windowSize < MIN_WIN_SIZE) { 
      windowSize = MIN_WIN_SIZE;
    }

    assert (windowSize > 0);


    if (Base::NEEDS_PUSH) {
      if (winWL.empty () && (wl.size_all () > windowSize)) {
        // a case where winWL is empty and all the new elements were going into 
        // nextWL. When nextWL becomes bigger than windowSize, we must do something
        // to control efficiency. One solution is to spill all elements into winWL
        // and refill
        //

        spillAll (winWL, wl);

      } else if (wl.size_all () > (WIN_OVER_SIZE_FACTOR * windowSize)) {
        // too many adds. spill to control efficiency
        spillAll (winWL, wl);
      }
    }

    winWL.poll (wl, windowSize, wl.size_all ());
    // std::cout << "Calculated Window size: " << windowSize << ", Actual: " << wl->size_all () << std::endl;
  }

  template <typename WinWL>
  GALOIS_ATTRIBUTE_PROF_NOINLINE void beginRound (WinWL& winWL) {
    std::swap (currWL, nextWL);

    if (targetCommitRatio != 0.0) {
      size_t currCommits = roundCommits.reduceRO (); 

      size_t prevWindowSize = roundTasks.reduceRO ();
      refill (winWL, *currWL, currCommits, prevWindowSize);
    }


    roundCommits.reset ();
    roundTasks.reset ();
    nextWL->clear_all_parallel ();
  }

  GALOIS_ATTRIBUTE_PROF_NOINLINE void endRound () {
    ++rounds;
    totalCommits += roundCommits.reduceRO ();
    totalTasks += roundTasks.reduceRO ();
  }

  // TODO: for debugging only

#ifndef NDEBUG
  const Ctxt* getMinCurrWL (void) const {

    Substrate::PerThreadStorage<Galois::optional<const Ctxt*> > perThrdMin;

    Galois::do_all_choice (makeLocalRange (*currWL),
        [this, &perThrdMin] (const Ctxt* c) {
          Galois::optional<const Ctxt*>& m = *(perThrdMin.getLocal ());

          if (!m || Base::ctxtCmp (c, *m)) { // c < *m
            m = c;
          }
        },
        std::make_tuple (
            Galois::loopname ("safety_test_loop"),
            Galois::chunk_size<8> ()));

    const Ctxt* ret = nullptr;

    for (unsigned i = 0; i < perThrdMin.size (); ++i) {
      const Galois::optional<const Ctxt*>& m = *(perThrdMin.getRemote (i));

      if (m) {
        if (!ret || Base::ctxtCmp (*m, ret)) { // ret < *m
          ret = *m;
        }
      }
    }

    return ret;
  }


  const Ctxt* getMaxCurrWL (void) const {

    Substrate::PerThreadStorage<Galois::optional<const Ctxt*> > perThrdMax;

    Galois::do_all_choice (makeLocalRange (*currWL),
        [this, &perThrdMax] (const Ctxt* c) {
          Galois::optional<const Ctxt*>& m = *(perThrdMax.getLocal ());

          if (!m || Base::ctxtCmp (*m, c)) { // *m < c
            m = c;
          } 
        },
        std::make_tuple (
            Galois::loopname ("safety_test_loop"),
            Galois::chunk_size<8> ()));

    const Ctxt* ret = nullptr;

    for (unsigned i = 0; i < perThrdMax.size (); ++i) {
      const Galois::optional<const Ctxt*>& m = *(perThrdMax.getRemote (i));

      if (m) {
        if (!ret || Base::ctxtCmp (ret, *m)) { // ret < *m
          ret = *m;
        }
      }
    }

    return ret;
  }
#endif

};



} // end namespace Runtime
} // end namespace Galois

#endif // GALOIS_RUNTIME_IKDG_BASE_H


