#include "Galois/CilkInit.h"
#include "Galois/Threads.h"
#include "Galois/Runtime/PerThreadStorage.h"
#include "Galois/Runtime/ThreadPool.h"
#include "Galois/Runtime/ll/gio.h"
#include "Galois/Runtime/ll/HWTopo.h"
#include "Galois/Runtime/ll/TID.h"
#include "Galois/Runtime/ll/EnvCheck.h"

#ifdef HAVE_CILK
#include <cilk/cilk.h>
#include <cilk/cilk_api.h>
#endif

#ifdef HAVE_CILK 
namespace detail {

static bool initialized = false;

struct BusyBarrier {
  volatile int entered;

  void check () const { assert (entered > 0); }

  BusyBarrier (unsigned val) : entered (val) 
  {
    check ();
  }

  void wait () {
    check ();
    __sync_fetch_and_sub (&entered, 1);
    while (entered > 0) {}
  }

  void reinit (unsigned val) {
    entered = val;
    check ();
  }
};

static void initOne (BusyBarrier& busybarrier, unsigned tid) {
  Galois::Runtime::LL::initTID(tid % Galois::Runtime::LL::getMaxThreads());
  Galois::Runtime::initPTS_cilk ();

  unsigned id = Galois::Runtime::LL::getTID ();
  pthread_t self = pthread_self ();

  std::printf ("CILK: Thread %ld assigned id=%d\n", self, id);

  if (id != 0 || !Galois::Runtime::LL::EnvCheck("GALOIS_DO_NOT_BIND_MAIN_THREAD")) {
    Galois::Runtime::LL::bindThreadToProcessor(id);
  }


  busybarrier.wait (); 
}

} // end namespace detail

void Galois::CilkInit (void) {

  if (detail::initialized) { 
    return ;
  } else {

    detail::initialized = true;

    unsigned numT = getActiveThreads ();

    // unsigned tot = __cilkrts_get_total_workers ();
    // std::printf ("CILK: total cilk workers = %d\n", tot);

    if (!Galois::Runtime::LL::EnvCheck("GALOIS_DO_NOT_BIND_MAIN_THREAD")) {
      GALOIS_DIE("Run program as: GALOIS_DO_NOT_BIND_MAIN_THREAD=1 prog args");
    }

    char nw_str[128];
    std::sprintf (nw_str, "%d", numT);

    std::printf ("CILK: Trying to set worker count to: %s\n", nw_str);
    if (0 != __cilkrts_set_param ("nworkers", nw_str)) {
      GALOIS_DIE("CILK: Failed to set Cilk worker count\n");
    } else {
      std::printf ("CILK: successfully set nworkers=%s\n", nw_str);
    }

    // if (nw != numT) {
      // std::printf ("CILK: cilk nworkers=%d != galois threads=%d\n", nw, numT); 
      // unsigned tot = __cilkrts_get_total_workers ();
      // std::printf ("CILK: total cilk workers = %d\n", tot);
      // std::abort ();
    // }

    detail::BusyBarrier busybarrier (numT);

    for (unsigned i = 0; i < numT; ++i) {
      cilk_spawn initOne (busybarrier, i);
    } // end for
  }
}
#else 
void Galois::CilkInit (void) {
  GALOIS_DIE("Cilk not found\n");
}
#endif // HAVE_CILK
