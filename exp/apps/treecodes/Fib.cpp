#include <limits>
#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>
#include <sstream>


#include "Galois/Galois.h"
#include "Galois/CilkInit.h"
#include "Galois/GaloisUnsafe.h"
#include "Galois/Atomic.h"
#include "Galois/Statistic.h"
#include "Galois/Runtime/DoAllCoupled.h"
#include "Galois/Runtime/Sampling.h"
#include "Galois/Runtime/ll/CompilerSpecific.h"
#include "Galois/Runtime/TreeExec.h"

#include "llvm/Support/CommandLine.h"
#include "Lonestar/BoilerPlate.h"

namespace cll = llvm::cl;
static cll::opt<unsigned> N("n", cll::desc("n-th fibonacci number"), cll::init(39));

enum ExecType {
  SERIAL, CILK, GALOIS, GALOIS_ALT, GALOIS_STACK, GALOIS_GENERIC, HAND
};

static cll::opt<ExecType> execType (
    cll::desc ("executor type"),
    cll::values (
      clEnumVal (SERIAL, "serial recursive"),
      clEnumVal (CILK, "CILK divide and conquer implementation"),
      clEnumVal (GALOIS, "galois divide and conquer implementation"),
      clEnumVal (GALOIS_ALT, "galois alternate divide and conquer implementation"),
      clEnumVal (GALOIS_STACK, "galois using thread stack"),
      clEnumVal (GALOIS_GENERIC, "galois std::function version"),
      clEnumVal (HAND, "Andrew's Handwritten version"),
      clEnumValEnd),

    cll::init (SERIAL));

const char* name = "fib";
const char* desc = "compute n-th fibonacci number";
const char* url = "fib";

unsigned fib(unsigned n)
{
  if (n <= 2)
    return n;
  unsigned x = cilk_spawn fib(n-1);
  // unsigned y = fib(n-2);
  unsigned y = cilk_spawn fib(n-2);
  cilk_sync;
  return x + y;
}

unsigned serialFib (unsigned n) {
  if (n <= 2) { 
    return n;
  }

  return serialFib (n-1) + serialFib (n-2);
}


struct FibEntry {
  unsigned n;
  unsigned result;
};

struct GaloisDivide {

  template <typename C>
  void operator () (FibEntry& x, C& wl) {
    if (x.n <= 2) {
      x.result = x.n;
      return;
    }

    FibEntry y; y.n = x.n - 1;
    FibEntry z; z.n = x.n - 2;

    wl.push (y);
    wl.push (z);
  }
};

struct GaloisConquer {

  template <typename I>
  void operator () (FibEntry& x, I beg, I end) {
    if (beg != end) {
      unsigned sum = 0;
      for (I i = beg; i != end; ++i) {
        sum += i->result;
      }

      x.result = sum;
    }
  }
};

unsigned galoisFib (unsigned n) {
  FibEntry initial {n, 0};

  FibEntry final = Galois::Runtime::for_each_ordered_tree (
      initial,
      GaloisDivide (),
      GaloisConquer (),
      Galois::Runtime::TreeExecNeedsChildren (),
      "fib-galois");

  return final.result;
}

struct FibRecord {
  unsigned n;
  unsigned* result;
  unsigned term_n_1;
  unsigned term_n_2;
};

struct GaloisDivideAlt {
  template <typename C>
  void operator () (FibRecord& r, C& wl) {
    if (r.n <= 2) {
      r.term_n_1 = r.n;
      r.term_n_2 = 0;
      return;
    }

    FibRecord left {r.n-1, &(r.term_n_1), 0, 0 };

    FibRecord rigt {r.n-2, &(r.term_n_2), 0, 0 };

    wl.push (left);
    wl.push (rigt);
  }
};

struct GaloisConquerAlt {
  void operator () (FibRecord& r) {
    *(r.result) = r.term_n_1 + r.term_n_2;
  };
};

unsigned galoisFibAlt (unsigned n) {

  unsigned result = 0;

  FibRecord init { n, &result, 0, 0};

  Galois::Runtime::for_each_ordered_tree (
      init,
      GaloisDivideAlt (),
      GaloisConquerAlt (),
      "fib-galois-alt");

  return result;

}


struct GaloisFibStack {
  unsigned n;
  unsigned result;

  template <typename C>
  void operator () (C& ctx) {
    if (n <= 2) {
      result = n;
      return;
    }

    GaloisFibStack left {n-1, 0};
    ctx.spawn (left);

    GaloisFibStack right {n-2, 0};
    ctx.spawn (right);

    ctx.sync ();

    result = left.result + right.result;
  }
};

unsigned galoisFibStack (unsigned n) {
  GaloisFibStack init {n, 0};

  Galois::Runtime::for_each_ordered_tree (init, "fib");

  return init.result;
}

struct GaloisFibGeneric: public Galois::Runtime::TreeTaskBase {
  unsigned n;
  unsigned result;

  GaloisFibGeneric (unsigned _n, unsigned _result): 
    Galois::Runtime::TreeTaskBase (),
    n (_n),
    result (_result)
  {}

  virtual void operator () (void) {
    if (n <= 2) {
      result = n;
      return;
    }

    GaloisFibGeneric left {n-1, 0};
    Galois::Runtime::spawn (left);

    GaloisFibGeneric right {n-2, 0};
    Galois::Runtime::spawn (right);

    Galois::Runtime::sync ();

    result = left.result + right.result;
  }
};

unsigned galoisFibGeneric (unsigned n) {
  GaloisFibGeneric init {n, 0};

  Galois::Runtime::for_each_ordered_tree_generic (init, "fib-gen");
  return init.result;
}


struct FibHandFrame {
  std::atomic<int> sum;
  std::atomic<int> done;
  FibHandFrame* parent;
};

Galois::InsertBag<FibHandFrame> B;

struct FibHandOp {
  typedef int tt_does_not_need_aborts;
  typedef int tt_does_not_need_stats;

  void notify_parent(FibHandFrame* r, int val) {
    if (!r) return;
    //fastpath
    if (r->done == 1) {
      notify_parent(r->parent, val + r->sum);
      return;
    }
    r->sum += val;
    if (++r->done == 2) {
      notify_parent(r->parent, r->sum);
      return;
    } //else, someone else will clean up
  }

  template<typename ContextTy>
  void operator() (std::pair<int, FibHandFrame*> wi, ContextTy& ctx) {
    int n = wi.first;
    FibHandFrame* r = wi.second;
    if (n <= 2) {
      notify_parent(r, n);
      return;
    }
    FibHandFrame& foo = B.emplace();
    foo.sum = 0;
    foo.done = 0;
    foo.parent = r;
    ctx.push(std::make_pair(n-1, &foo));
    ctx.push(std::make_pair(n-2, &foo));
    return;
  }
};

unsigned fibHand (int n) {

  typedef Galois::WorkList::AltChunkedFIFO<64> Chunked;
  // typedef Galois::WorkList::AltChunkedLIFO<4> Chunked;

  FibHandFrame init;
  init.sum = 0;
  init.done = 0;
  init.parent = 0;

  Galois::for_each(std::make_pair(n, &init), 
      FibHandOp(), 
      Galois::loopname ("fib-hand"),
      Galois::wl<Chunked>());

  return init.sum;
}

int main (int argc, char* argv[]) {

  Galois::StatManager sm;
  LonestarStart (argc, argv, name, desc, url);

  unsigned result = -1;

  Galois::StatTimer t;

  t.start ();
  switch (execType) {
    case SERIAL:
      result = serialFib (N);
      break;

    case CILK:
      Galois::CilkInit ();
      result = fib (N);
      break;

    case GALOIS:
      result = galoisFib (N);
      break;

    case GALOIS_ALT:
      result = galoisFibAlt (N);
      break;

    case GALOIS_STACK:
      result = galoisFibStack (N);
      break;

    case GALOIS_GENERIC:
      result = galoisFibGeneric (N);
      break;

    case HAND:
      result = fibHand (N);
      break;

    default:
      std::abort ();

  }
  t.stop ();

  std::printf ("%dth Fibonacci number is: %d\n", unsigned(N), result);

  if (!skipVerify) {
    unsigned ser = serialFib (N);
    if (result != ser) {
      GALOIS_DIE("Result doesn't match with serial: ", ser);
    }
    else {
      std::printf ("OK... Result verifed ...\n");
    }
  }

  return 0;
}
