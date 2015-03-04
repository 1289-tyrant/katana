#ifndef GALOIS_DET_SCHEDULES_H
#define GALOIS_DET_SCHEDULES_H

#include "Galois/Runtime/Executor_Deterministic.h"
#include "Galois/Runtime/DAGexec.h"
#include "Galois/Runtime/DetChromatic.h"
#include "Galois/Runtime/DetKDGexecutor.h"

#include "llvm/Support/CommandLine.h"

namespace Galois {

namespace Runtime {

enum DetExecType {
  non_det,
  det_i,
  det_ar,
  kdg_i,
  kdg_ar,
  kdg_r,
  chromatic,
  topo,
  edge_flip
};

static cll::opt<DetExecType> detExecTypeArg("detexec", cll::desc("Choose schedule for asynchronous algorithm"),
  cll::values(
    clEnumValN(non_det, "non_det", "non deterministic using for_each"),
    clEnumValN(det_i, "det_i", "deterministic using implicit kdg"),
    clEnumValN(det_ar, "det_ar", "deterministic add-remove"),
    clEnumValN(kdg_r, "kdg_r", "kdg_r"),
    clEnumValN(chromatic, "chromatic", "chromatic"),
    clEnumValN(topo, "topo", "topo"),
    clEnumValN(edge_flip, "edge_flip", "edge_flip"),
  clEnumValEnd),
  cll::init(det_i));


#if 0
template <typename R, typename F>
void for_each_det_choice (const R& range, const F& func, const char* loopname, const DetExecType& detExec=detExecTypeArg) {

  switch (detExec) {
    case non_det:
      {
        const unsigned CHUNK_SIZE = 32;
        typedef Galois::WorkList::dChunkedFIFO<CHUNK_SIZE, typename R::value_type> WL_ty;
        Galois::Runtime::for_each_impl<WL_ty> (range, func, loopname);
        break;
      }

    case det_i:
      Galois::Runtime::for_each_det_impl (range, func, loopname);
      break;

    case det_ar:
      Galois::Runtime::for_each_det_impl (range, func, loopname);
      break;

    default:
      GALOIS_DIE ("not implemented");
      break;
  }
}
#endif

template <typename R, typename C, typename F, typename N>
void for_each_det_choice (const R&  range, const C& cmp, const N& nhoodVisitor, const F& func, const char* loopname, const DetExecType& detExec=detExecTypeArg) {

  switch (detExec) {
    case kdg_i: 
      Galois::Runtime::for_each_ordered_2p_win (range, cmp, nhoodVisitor, func, loopname);
      break;

    case kdg_ar:
      GALOIS_DIE ("not implemented yet");
      break;

    default:
      GALOIS_DIE ("not implemented");
      break;

  }

}

template <typename R, typename F, typename G>
void for_each_det_choice (const R& range, const F& func, G& graph, const char* loopname, const DetExecType& detExec=detExecTypeArg) {

  switch (detExec) {
    case chromatic:
      Galois::Runtime::for_each_det_chromatic (range, func, graph, loopname);
      break;

    case edge_flip:
      Galois::Runtime::for_each_det_edge_flip_ar (range, func, graph, loopname);
      break;

    case topo:
      Galois::Runtime::for_each_det_edge_flip_topo (range, func, graph, loopname);
      break;

    default:
      GALOIS_DIE ("not implemented");
      break;
  }
}

template <typename R, typename F, typename G, typename M>
void for_each_det_choice (const R& range, const F& func, G& graph, M& dagManager, const char* loopname, const DetExecType& detExec=detExecTypeArg) {

  switch (detExec) {
    case chromatic:
      Galois::Runtime::for_each_det_chromatic (range, func, graph, dagManager, loopname);
      break;

    case edge_flip:
      Galois::Runtime::for_each_det_edge_flip_ar (range, func, graph, dagManager, loopname);
      break;

    case topo:
      Galois::Runtime::for_each_det_edge_flip_topo (range, func, graph, dagManager, loopname);
      break;

    default:
      GALOIS_DIE ("not implemented");
      break;

  }

}


template <typename T, typename G, typename M, typename F, typename N, typename C> 
struct ReuseableExecutorWrapper {

  typedef Galois::Runtime::ChromaticReuseExecutor<G, M, F> Chrom;
  typedef Galois::Runtime::InputGraphDAGreuseExecutor<G, M, F> InputDAG;
  typedef Galois::Runtime::DAGexecutorRW<T, C, F, N> TaskDAG;


  DetExecType detExec;

  Chrom chromExec;
  InputDAG inputDAGexec;
  TaskDAG taskDAGexec;

  ReuseableExecutorWrapper (
      DetExecType detExec,
      G& graph,
      M& dagManager,
      const F& func,
      const N& nhVisitor,
      const C& cmp, 
      const char* loopname)
    :
      detExec {detExec},
      chromExec {graph, dagManager, func, loopname},
      inputDAGexec {graph, dagManager, func, loopname},
      taskDAGexec {cmp, nhVisitor, func, loopname}
  {}


  template <typename R>
  void initialize (const R& range) {

    switch (detExec) {

      case chromatic:
        chromExec.initialize (range);
        break;

      case edge_flip:
        inputDAGexec.initialize (range);
        break;

      case kdg_r:
        taskDAGexec.initialize (range);
        break;

      default:
        GALOIS_DIE ("det exec type not supported");
        break;
    }
  }


  void execute (void) {
    switch (detExec) {

      case chromatic:
        chromExec.execute ();
        break;

      case edge_flip:
        inputDAGexec.execute ();
        break;

      case kdg_r:
        taskDAGexec.execute ();
        break;

      default:
        GALOIS_DIE ("det exec type not supported");
        break;
    }
    
  }

  void reinitDAG (void) {
    switch (detExec) {

      case chromatic:
        chromExec.reinitDAG ();
        break;

      case edge_flip:
        inputDAGexec.reinitDAG ();
        break;

      case kdg_r:
        taskDAGexec.reinitDAG ();
        break;

      default:
        GALOIS_DIE ("det exec type not supported");
        break;
    }
  }

};



template <typename R, typename G, typename M, typename F, typename N, typename C>

ReuseableExecutorWrapper<typename R::value_type, G, M, F, N, C>* make_reusable_dag_exec (const R& range, G& graph, M& dagManager, const F& func, const N& nhVisitor, const C& cmp, const char* loopname, DetExecType detExec=detExecTypeArg) {

  return new ReuseableExecutorWrapper<typename R::value_type, G, M, F, N, C> {
    detExec,
    graph,
    dagManager,
    func,
    nhVisitor,
    cmp,
    loopname
  };
}



} // end namespace Runtime


} // end namespace Galois


#endif //  GALOIS_DET_SCHEDULES_H
