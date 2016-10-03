/*  -*- mode: c++ -*-  */
#include "gg.h"
#include "ggcuda.h"

void kernel_sizing(CSRGraph &, dim3 &, dim3 &);
#define TB_SIZE 256
const char *GGC_OPTIONS = "coop_conv=False $ outline_iterate_gb=False $ backoff_blocking_factor=4 $ parcomb=True $ np_schedulers=set(['fg', 'tb', 'wp']) $ cc_disable=set([]) $ hacks=set([]) $ np_factor=8 $ instrument=set([]) $ unroll=[] $ instrument_mode=None $ read_props=None $ outline_iterate=True $ ignore_nested_errors=False $ np=True $ write_props=None $ quiet_cgen=True $ retry_backoff=True $ cuda.graph_type=basic $ cuda.use_worklist_slots=True $ cuda.worklist_type=basic";
unsigned int * P_COMP_CURRENT;
unsigned int * P_COMP_OLD;
#include "kernels/reduce.cuh"
#include "gen_cuda.cuh"
static const int __tb_ConnectedComp = TB_SIZE;
static const int __tb_FirstItr_ConnectedComp = TB_SIZE;
__global__ void InitializeGraph(CSRGraph graph, unsigned int nowned, unsigned int * p_comp_current, unsigned int * p_comp_old)
{
  unsigned tid = TID_1D;
  unsigned nthreads = TOTAL_THREADS_1D;

  const unsigned __kernel_tb_size = TB_SIZE;
  index_type src_end;
  // FP: "1 -> 2;
  src_end = nowned;
  for (index_type src = 0 + tid; src < src_end; src += nthreads)
  {
    bool pop  = src < nowned;
    if (pop)
    {
      p_comp_current[src] = graph.node_data[src];
      p_comp_old[src] = graph.node_data[src];
    }
  }
  // FP: "8 -> 9;
}
__global__ void FirstItr_ConnectedComp(CSRGraph graph, unsigned int nowned, unsigned int * p_comp_current, unsigned int * p_comp_old)
{
  unsigned tid = TID_1D;
  unsigned nthreads = TOTAL_THREADS_1D;

  const unsigned __kernel_tb_size = __tb_FirstItr_ConnectedComp;
  index_type src_end;
  index_type src_rup;
  // FP: "1 -> 2;
  const int _NP_CROSSOVER_WP = 32;
  const int _NP_CROSSOVER_TB = __kernel_tb_size;
  // FP: "2 -> 3;
  const int BLKSIZE = __kernel_tb_size;
  const int ITSIZE = BLKSIZE * 8;
  // FP: "3 -> 4;

  typedef cub::BlockScan<multiple_sum<2, index_type>, BLKSIZE> BlockScan;
  typedef union np_shared<BlockScan::TempStorage, index_type, struct tb_np, struct warp_np<__kernel_tb_size/32>, struct fg_np<ITSIZE> > npsTy;

  // FP: "4 -> 5;
  __shared__ npsTy nps ;
  // FP: "5 -> 6;
  src_end = nowned;
  src_rup = (roundup((nowned), (blockDim.x)));
  for (index_type src = 0 + tid; src < src_rup; src += nthreads)
  {
    multiple_sum<2, index_type> _np_mps;
    multiple_sum<2, index_type> _np_mps_total;
    // FP: "6 -> 7;
    bool pop  = src < nowned;
    // FP: "7 -> 8;
    if (pop)
    {
      p_comp_old[src] = p_comp_current[src];
    }
    // FP: "10 -> 11;
    // FP: "13 -> 14;
    struct NPInspector1 _np = {0,0,0,0,0,0};
    // FP: "14 -> 15;
    __shared__ struct { index_type src; } _np_closure [TB_SIZE];
    // FP: "15 -> 16;
    _np_closure[threadIdx.x].src = src;
    // FP: "16 -> 17;
    if (pop)
    {
      _np.size = (graph).getOutDegree(src);
      _np.start = (graph).getFirstEdge(src);
    }
    // FP: "19 -> 20;
    // FP: "20 -> 21;
    _np_mps.el[0] = _np.size >= _NP_CROSSOVER_WP ? _np.size : 0;
    _np_mps.el[1] = _np.size < _NP_CROSSOVER_WP ? _np.size : 0;
    // FP: "21 -> 22;
    BlockScan(nps.temp_storage).ExclusiveSum(_np_mps, _np_mps, _np_mps_total);
    // FP: "22 -> 23;
    if (threadIdx.x == 0)
    {
      nps.tb.owner = MAX_TB_SIZE + 1;
    }
    // FP: "25 -> 26;
    __syncthreads();
    // FP: "26 -> 27;
    while (true)
    {
      if (_np.size >= _NP_CROSSOVER_TB)
      {
        nps.tb.owner = threadIdx.x;
      }
      __syncthreads();
      if (nps.tb.owner == MAX_TB_SIZE + 1)
      {
        __syncthreads();
        break;
      }
      if (nps.tb.owner == threadIdx.x)
      {
        nps.tb.start = _np.start;
        nps.tb.size = _np.size;
        nps.tb.src = threadIdx.x;
        _np.start = 0;
        _np.size = 0;
      }
      __syncthreads();
      int ns = nps.tb.start;
      int ne = nps.tb.size;
      if (nps.tb.src == threadIdx.x)
      {
        nps.tb.owner = MAX_TB_SIZE + 1;
      }
      assert(nps.tb.src < __kernel_tb_size);
      src = _np_closure[nps.tb.src].src;
      for (int _np_j = threadIdx.x; _np_j < ne; _np_j += BLKSIZE)
      {
        index_type jj;
        jj = ns +_np_j;
        {
          index_type dst;
          unsigned int new_dist;
          dst = graph.getAbsDestination(jj);
          new_dist = p_comp_current[src];
          atomicMin(&p_comp_current[dst], new_dist);
        }
      }
      __syncthreads();
      // FP: "54 -> 27;
    }
    // FP: "55 -> 56;

    // FP: "56 -> 57;
    {
      const int warpid = threadIdx.x / 32;
      // FP: "57 -> 58;
      const int _np_laneid = cub::LaneId();
      // FP: "58 -> 59;
      while (__any(_np.size >= _NP_CROSSOVER_WP && _np.size < _NP_CROSSOVER_TB))
      {
        if (_np.size >= _NP_CROSSOVER_WP && _np.size < _NP_CROSSOVER_TB)
        {
          nps.warp.owner[warpid] = _np_laneid;
        }
        if (nps.warp.owner[warpid] == _np_laneid)
        {
          nps.warp.start[warpid] = _np.start;
          nps.warp.size[warpid] = _np.size;
          nps.warp.src[warpid] = threadIdx.x;
          _np.start = 0;
          _np.size = 0;
        }
        index_type _np_w_start = nps.warp.start[warpid];
        index_type _np_w_size = nps.warp.size[warpid];
        assert(nps.warp.src[warpid] < __kernel_tb_size);
        src = _np_closure[nps.warp.src[warpid]].src;
        for (int _np_ii = _np_laneid; _np_ii < _np_w_size; _np_ii += 32)
        {
          index_type jj;
          jj = _np_w_start +_np_ii;
          {
            index_type dst;
            unsigned int new_dist;
            dst = graph.getAbsDestination(jj);
            new_dist = p_comp_current[src];
            atomicMin(&p_comp_current[dst], new_dist);
          }
        }
      }
      // FP: "77 -> 78;
      __syncthreads();
      // FP: "78 -> 79;
    }

    // FP: "79 -> 80;
    __syncthreads();
    // FP: "80 -> 81;
    _np.total = _np_mps_total.el[1];
    _np.offset = _np_mps.el[1];
    // FP: "81 -> 82;
    while (_np.work())
    {
      // FP: "82 -> 83;
      int _np_i =0;
      // FP: "83 -> 84;
      _np.inspect2(nps.fg.itvalue, nps.fg.src, ITSIZE, threadIdx.x);
      // FP: "84 -> 85;
      __syncthreads();
      // FP: "85 -> 86;

      // FP: "86 -> 87;
      for (_np_i = threadIdx.x; _np_i < ITSIZE && _np.valid(_np_i); _np_i += BLKSIZE)
      {
        index_type jj;
        assert(nps.fg.src[_np_i] < __kernel_tb_size);
        src = _np_closure[nps.fg.src[_np_i]].src;
        jj= nps.fg.itvalue[_np_i];
        {
          index_type dst;
          unsigned int new_dist;
          dst = graph.getAbsDestination(jj);
          new_dist = p_comp_current[src];
          atomicMin(&p_comp_current[dst], new_dist);
        }
      }
      // FP: "96 -> 97;
      _np.execute_round_done(ITSIZE);
      // FP: "97 -> 98;
      __syncthreads();
      // FP: "98 -> 82;
    }
    // FP: "99 -> 100;
    assert(threadIdx.x < __kernel_tb_size);
    src = _np_closure[threadIdx.x].src;
    // FP: "100 -> 6;
  }
  // FP: "101 -> 102;
}
__global__ void ConnectedComp(CSRGraph graph, unsigned int nowned, unsigned int * p_comp_current, unsigned int * p_comp_old, Any any_retval)
{
  unsigned tid = TID_1D;
  unsigned nthreads = TOTAL_THREADS_1D;

  const unsigned __kernel_tb_size = __tb_ConnectedComp;
  index_type src_end;
  index_type src_rup;
  // FP: "1 -> 2;
  const int _NP_CROSSOVER_WP = 32;
  const int _NP_CROSSOVER_TB = __kernel_tb_size;
  // FP: "2 -> 3;
  const int BLKSIZE = __kernel_tb_size;
  const int ITSIZE = BLKSIZE * 8;
  // FP: "3 -> 4;

  typedef cub::BlockScan<multiple_sum<2, index_type>, BLKSIZE> BlockScan;
  typedef union np_shared<BlockScan::TempStorage, index_type, struct tb_np, struct warp_np<__kernel_tb_size/32>, struct fg_np<ITSIZE> > npsTy;

  // FP: "4 -> 5;
  __shared__ npsTy nps ;
  // FP: "5 -> 6;
  src_end = nowned;
  src_rup = (roundup((nowned), (blockDim.x)));
  for (index_type src = 0 + tid; src < src_rup; src += nthreads)
  {
    multiple_sum<2, index_type> _np_mps;
    multiple_sum<2, index_type> _np_mps_total;
    // FP: "6 -> 7;
    bool pop  = src < nowned;
    // FP: "7 -> 8;
    if (pop)
    {
      if (p_comp_old[src] > p_comp_current[src])
      {
        p_comp_old[src] = p_comp_current[src];
        any_retval.return_( 1);
      }
      else
      {
        pop = false;
      }
    }
    // FP: "13 -> 14;
    // FP: "16 -> 17;
    struct NPInspector1 _np = {0,0,0,0,0,0};
    // FP: "17 -> 18;
    __shared__ struct { index_type src; } _np_closure [TB_SIZE];
    // FP: "18 -> 19;
    _np_closure[threadIdx.x].src = src;
    // FP: "19 -> 20;
    if (pop)
    {
      _np.size = (graph).getOutDegree(src);
      _np.start = (graph).getFirstEdge(src);
    }
    // FP: "22 -> 23;
    // FP: "23 -> 24;
    _np_mps.el[0] = _np.size >= _NP_CROSSOVER_WP ? _np.size : 0;
    _np_mps.el[1] = _np.size < _NP_CROSSOVER_WP ? _np.size : 0;
    // FP: "24 -> 25;
    BlockScan(nps.temp_storage).ExclusiveSum(_np_mps, _np_mps, _np_mps_total);
    // FP: "25 -> 26;
    if (threadIdx.x == 0)
    {
      nps.tb.owner = MAX_TB_SIZE + 1;
    }
    // FP: "28 -> 29;
    __syncthreads();
    // FP: "29 -> 30;
    while (true)
    {
      if (_np.size >= _NP_CROSSOVER_TB)
      {
        nps.tb.owner = threadIdx.x;
      }
      __syncthreads();
      if (nps.tb.owner == MAX_TB_SIZE + 1)
      {
        __syncthreads();
        break;
      }
      if (nps.tb.owner == threadIdx.x)
      {
        nps.tb.start = _np.start;
        nps.tb.size = _np.size;
        nps.tb.src = threadIdx.x;
        _np.start = 0;
        _np.size = 0;
      }
      __syncthreads();
      int ns = nps.tb.start;
      int ne = nps.tb.size;
      if (nps.tb.src == threadIdx.x)
      {
        nps.tb.owner = MAX_TB_SIZE + 1;
      }
      assert(nps.tb.src < __kernel_tb_size);
      src = _np_closure[nps.tb.src].src;
      for (int _np_j = threadIdx.x; _np_j < ne; _np_j += BLKSIZE)
      {
        index_type jj;
        jj = ns +_np_j;
        {
          index_type dst;
          unsigned int new_dist;
          dst = graph.getAbsDestination(jj);
          new_dist = p_comp_current[src];
          atomicMin(&p_comp_current[dst], new_dist);
        }
      }
      __syncthreads();
      // FP: "57 -> 30;
    }
    // FP: "58 -> 59;

    // FP: "59 -> 60;
    {
      const int warpid = threadIdx.x / 32;
      // FP: "60 -> 61;
      const int _np_laneid = cub::LaneId();
      // FP: "61 -> 62;
      while (__any(_np.size >= _NP_CROSSOVER_WP && _np.size < _NP_CROSSOVER_TB))
      {
        if (_np.size >= _NP_CROSSOVER_WP && _np.size < _NP_CROSSOVER_TB)
        {
          nps.warp.owner[warpid] = _np_laneid;
        }
        if (nps.warp.owner[warpid] == _np_laneid)
        {
          nps.warp.start[warpid] = _np.start;
          nps.warp.size[warpid] = _np.size;
          nps.warp.src[warpid] = threadIdx.x;
          _np.start = 0;
          _np.size = 0;
        }
        index_type _np_w_start = nps.warp.start[warpid];
        index_type _np_w_size = nps.warp.size[warpid];
        assert(nps.warp.src[warpid] < __kernel_tb_size);
        src = _np_closure[nps.warp.src[warpid]].src;
        for (int _np_ii = _np_laneid; _np_ii < _np_w_size; _np_ii += 32)
        {
          index_type jj;
          jj = _np_w_start +_np_ii;
          {
            index_type dst;
            unsigned int new_dist;
            dst = graph.getAbsDestination(jj);
            new_dist = p_comp_current[src];
            atomicMin(&p_comp_current[dst], new_dist);
          }
        }
      }
      // FP: "80 -> 81;
      __syncthreads();
      // FP: "81 -> 82;
    }

    // FP: "82 -> 83;
    __syncthreads();
    // FP: "83 -> 84;
    _np.total = _np_mps_total.el[1];
    _np.offset = _np_mps.el[1];
    // FP: "84 -> 85;
    while (_np.work())
    {
      // FP: "85 -> 86;
      int _np_i =0;
      // FP: "86 -> 87;
      _np.inspect2(nps.fg.itvalue, nps.fg.src, ITSIZE, threadIdx.x);
      // FP: "87 -> 88;
      __syncthreads();
      // FP: "88 -> 89;

      // FP: "89 -> 90;
      for (_np_i = threadIdx.x; _np_i < ITSIZE && _np.valid(_np_i); _np_i += BLKSIZE)
      {
        index_type jj;
        assert(nps.fg.src[_np_i] < __kernel_tb_size);
        src = _np_closure[nps.fg.src[_np_i]].src;
        jj= nps.fg.itvalue[_np_i];
        {
          index_type dst;
          unsigned int new_dist;
          dst = graph.getAbsDestination(jj);
          new_dist = p_comp_current[src];
          atomicMin(&p_comp_current[dst], new_dist);
        }
      }
      // FP: "99 -> 100;
      _np.execute_round_done(ITSIZE);
      // FP: "100 -> 101;
      __syncthreads();
      // FP: "101 -> 85;
    }
    // FP: "102 -> 103;
    assert(threadIdx.x < __kernel_tb_size);
    src = _np_closure[threadIdx.x].src;
    // FP: "103 -> 6;
  }
  // FP: "105 -> 106;
}
void InitializeGraph_cuda(struct CUDA_Context * ctx)
{
  dim3 blocks;
  dim3 threads;
  // FP: "1 -> 2;
  // FP: "2 -> 3;
  // FP: "3 -> 4;
  kernel_sizing(ctx->gg, blocks, threads);
  // FP: "4 -> 5;
  InitializeGraph <<<blocks, threads>>>(ctx->gg, ctx->nowned, ctx->comp_current.gpu_wr_ptr(), ctx->comp_old.gpu_wr_ptr());
  // FP: "5 -> 6;
  check_cuda_kernel;
  // FP: "6 -> 7;
}
void FirstItr_ConnectedComp_cuda(struct CUDA_Context * ctx)
{
  dim3 blocks;
  dim3 threads;
  // FP: "1 -> 2;
  // FP: "2 -> 3;
  // FP: "3 -> 4;
  kernel_sizing(ctx->gg, blocks, threads);
  // FP: "4 -> 5;
  FirstItr_ConnectedComp <<<blocks, __tb_FirstItr_ConnectedComp>>>(ctx->gg, ctx->nowned, ctx->comp_current.gpu_wr_ptr(), ctx->comp_old.gpu_wr_ptr());
  // FP: "5 -> 6;
  check_cuda_kernel;
  // FP: "6 -> 7;
}
void ConnectedComp_cuda(int & __retval, struct CUDA_Context * ctx)
{
  dim3 blocks;
  dim3 threads;
  // FP: "1 -> 2;
  // FP: "2 -> 3;
  // FP: "3 -> 4;
  kernel_sizing(ctx->gg, blocks, threads);
  // FP: "4 -> 5;
  *(ctx->p_retval.cpu_wr_ptr()) = __retval;
  // FP: "5 -> 6;
  ctx->any_retval.rv = ctx->p_retval.gpu_wr_ptr();
  // FP: "6 -> 7;
  ConnectedComp <<<blocks, __tb_ConnectedComp>>>(ctx->gg, ctx->nowned, ctx->comp_current.gpu_wr_ptr(), ctx->comp_old.gpu_wr_ptr(), ctx->any_retval);
  // FP: "7 -> 8;
  check_cuda_kernel;
  // FP: "8 -> 9;
  __retval = *(ctx->p_retval.cpu_rd_ptr());
  // FP: "9 -> 10;
}
