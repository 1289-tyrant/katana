/** Residual based Page Rank -*- C++ -*-
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
 * Compute pageRank using residual on distributed Galois.
 *
 * @author Gurbinder Gill <gurbinder533@gmail.com>
 */

#include <iostream>
#include <limits>
#include <algorithm>
#include <vector>
#include "Galois/Galois.h"
#include "Lonestar/BoilerPlate.h"
#include "Galois/gstl.h"

#include "Galois/Runtime/CompilerHelperFunctions.h"
#include "Galois/Runtime/Tracer.h"

#include "Galois/Runtime/dGraph_edgeCut.h"
#include "Galois/Runtime/dGraph_vertexCut.h"

#ifdef __GALOIS_HET_CUDA__
#include "Galois/DistBag.h"
#include "Galois/Runtime/Cuda/cuda_device.h"
#include "gen_cuda.h"
struct CUDA_Context *cuda_ctx;
struct CUDA_Worklist cuda_wl;

enum Personality {
   CPU, GPU_CUDA, GPU_OPENCL
};
std::string personality_str(Personality p) {
   switch (p) {
   case CPU:
      return "CPU";
   case GPU_CUDA:
      return "GPU_CUDA";
   case GPU_OPENCL:
      return "GPU_OPENCL";
   }
   assert(false&& "Invalid personality");
   return "";
}
#endif

static const char* const name = "PageRank - Compiler Generated Distributed Heterogeneous";
static const char* const desc = "Residual PageRank on Distributed Galois.";
static const char* const url = 0;

namespace cll = llvm::cl;
static cll::opt<std::string> inputFile(cll::Positional, cll::desc("<input file>"), cll::Required);
static cll::opt<std::string> partFolder("partFolder", cll::desc("path to partitionFolder"), cll::init(""));
static cll::opt<float> tolerance("tolerance", cll::desc("tolerance"), cll::init(0.000001));
static cll::opt<unsigned int> maxIterations("maxIterations", cll::desc("Maximum iterations: Default 10000"), cll::init(10000));
static cll::opt<bool> verify("verify", cll::desc("Verify ranks by printing to 'page_ranks.#hid.csv' file"), cll::init(false));

static cll::opt<bool> enableVCut("enableVertexCut", cll::desc("Use vertex cut for graph partitioning."), cll::init(false));

#ifdef __GALOIS_HET_CUDA__
static cll::opt<int> gpudevice("gpu", cll::desc("Select GPU to run on, default is to choose automatically"), cll::init(-1));
static cll::opt<Personality> personality("personality", cll::desc("Personality"),
      cll::values(clEnumValN(CPU, "cpu", "Galois CPU"), clEnumValN(GPU_CUDA, "gpu/cuda", "GPU/CUDA"), clEnumValN(GPU_OPENCL, "gpu/opencl", "GPU/OpenCL"), clEnumValEnd),
      cll::init(CPU));
static cll::opt<std::string> personality_set("pset", cll::desc("String specifying personality for each host. 'c'=CPU,'g'=GPU/CUDA and 'o'=GPU/OpenCL"), cll::init(""));
static cll::opt<unsigned> scalegpu("scalegpu", cll::desc("Scale GPU workload w.r.t. CPU, default is proportionally equal workload to CPU and GPU (1)"), cll::init(1));
static cll::opt<unsigned> scalecpu("scalecpu", cll::desc("Scale CPU workload w.r.t. GPU, default is proportionally equal workload to CPU and GPU (1)"), cll::init(1));
static cll::opt<int> num_nodes("num_nodes", cll::desc("Num of physical nodes with devices (default = num of hosts): detect GPU to use for each host automatically"), cll::init(-1));
static cll::opt<double> cuda_wl_dup_factor("cuda_wl_dup_factor", cll::desc("Upper bound for duplication factor in CUDA worklist (1): worklist size = factor * graph.nnodes"), cll::init(1));
#endif


static const float alpha = (1.0 - 0.85);
//static const float TOLERANCE = 0.01;
struct PR_NodeData {
  float value;
  std::atomic<float> residual;
  unsigned int nout;

};

typedef hGraph<PR_NodeData, void> Graph;
typedef hGraph_edgeCut<PR_NodeData, void> Graph_edgeCut;
typedef hGraph_vertexCut<PR_NodeData, void> Graph_vertexCut;

typedef typename Graph::GraphNode GNode;

typedef typename Graph::GraphNode GNode;

typedef GNode WorkItem;

struct ResetGraph {
  Graph* graph;

  ResetGraph(Graph* _graph) : graph(_graph){}
  void static go(Graph& _graph) {
    	struct SyncerPull_0 {
    		static float extract(uint32_t node_id, const struct PR_NodeData & node) {
    		#ifdef __GALOIS_HET_CUDA__
    			if (personality == GPU_CUDA) return get_node_residual_cuda(cuda_ctx, node_id);
    			assert (personality == CPU);
    		#endif
    			return node.residual;
    		}
    		static bool extract_batch(unsigned from_id, float *y) {
    		#ifdef __GALOIS_HET_CUDA__
    			if (personality == GPU_CUDA) { batch_get_node_residual_cuda(cuda_ctx, from_id, y); return true; }
    			assert (personality == CPU);
    		#endif
    			return false;
    		}
    		static void setVal (uint32_t node_id, struct PR_NodeData & node, float y) {
    		#ifdef __GALOIS_HET_CUDA__
    			if (personality == GPU_CUDA) set_node_residual_cuda(cuda_ctx, node_id, y);
    			else if (personality == CPU)
    		#endif
    				node.residual = y;
    		}
    		static bool setVal_batch(unsigned from_id, float *y) {
    		#ifdef __GALOIS_HET_CUDA__
    			if (personality == GPU_CUDA) { batch_set_node_residual_cuda(cuda_ctx, from_id, y); return true; }
    			assert (personality == CPU);
    		#endif
    			return false;
    		}
    		typedef float ValTy;
    	};
    	struct Syncer_vertexCut_0 {
    		static float extract(uint32_t node_id, const struct PR_NodeData & node) {
    		#ifdef __GALOIS_HET_CUDA__
    			if (personality == GPU_CUDA) return get_node_residual_cuda(cuda_ctx, node_id);
    			assert (personality == CPU);
    		#endif
    			return node.residual;
    		}
    		static bool extract_reset_batch(unsigned from_id, float *y) {
    		#ifdef __GALOIS_HET_CUDA__
    			if (personality == GPU_CUDA) { batch_get_slave_node_residual_cuda(cuda_ctx, from_id, y); return true; }
    			assert (personality == CPU);
    		#endif
    			return false;
    		}
    		static void reduce (uint32_t node_id, struct PR_NodeData & node, float y) {
    		#ifdef __GALOIS_HET_CUDA__
    			if (personality == GPU_CUDA) set_node_residual_cuda(cuda_ctx, node_id, y);
    			else if (personality == CPU)
    		#endif
    				{ Galois::set(node.residual, y); }
    		}
    		static bool reduce_batch(unsigned from_id, float *y) {
    		#ifdef __GALOIS_HET_CUDA__
    			if (personality == GPU_CUDA) { batch_set_node_residual_cuda(cuda_ctx, from_id, y); return true; }
    			assert (personality == CPU);
    		#endif
    			return false;
    		}
    		static void reset (uint32_t node_id, struct PR_NodeData & node ) {
    		}
    		typedef float ValTy;
    	};
    #ifdef __GALOIS_HET_CUDA__
    	if (personality == GPU_CUDA) {
    		std::string impl_str("CUDA_DO_ALL_IMPL_ResetGraph_" + std::to_string(_graph.get_run_num()));
    		Galois::StatTimer StatTimer_cuda(impl_str.c_str());
    		StatTimer_cuda.start();
    		ResetGraph_cuda(cuda_ctx);
    		StatTimer_cuda.stop();
    	} else if (personality == CPU)
    #endif
    Galois::do_all(_graph.begin(), _graph.end(), ResetGraph{ &_graph }, Galois::loopname("ResetGraph"), Galois::numrun(_graph.get_run_num()), Galois::write_set("sync_pull", "this->graph", "struct PR_NodeData &", "struct PR_NodeData &", "residual" , "float" , "set",  ""));
    if(_graph.is_vertex_cut()) {
    	_graph.sync_push<Syncer_vertexCut_0>("ResetGraph");
    }
    
    _graph.sync_pull<SyncerPull_0>("ResetGraph");
    
  }

  void operator()(GNode src) const {
    PR_NodeData& sdata = graph->getData(src);
    sdata.value = 0;
    sdata.nout = 0;
    sdata.residual = 0;
  }
};

struct InitializeGraph {
  const float &local_alpha;
  Graph* graph;

  InitializeGraph(const float &_alpha, Graph* _graph) : local_alpha(_alpha), graph(_graph){}
  void static go(Graph& _graph) {
    	struct Syncer_0 {
    		static float extract(uint32_t node_id, const struct PR_NodeData & node) {
    		#ifdef __GALOIS_HET_CUDA__
    			if (personality == GPU_CUDA) return get_node_residual_cuda(cuda_ctx, node_id);
    			assert (personality == CPU);
    		#endif
    			return node.residual;
    		}
    		static bool extract_reset_batch(unsigned from_id, float *y) {
    		#ifdef __GALOIS_HET_CUDA__
    			if (personality == GPU_CUDA) { batch_get_reset_node_residual_cuda(cuda_ctx, from_id, y, 0); return true; }
    			assert (personality == CPU);
    		#endif
    			return false;
    		}
    		static void reduce (uint32_t node_id, struct PR_NodeData & node, float y) {
    		#ifdef __GALOIS_HET_CUDA__
    			if (personality == GPU_CUDA) add_node_residual_cuda(cuda_ctx, node_id, y);
    			else if (personality == CPU)
    		#endif
    				{ Galois::add(node.residual, y); }
    		}
    		static bool reduce_batch(unsigned from_id, float *y) {
    		#ifdef __GALOIS_HET_CUDA__
    			if (personality == GPU_CUDA) { batch_add_node_residual_cuda(cuda_ctx, from_id, y); return true; }
    			assert (personality == CPU);
    		#endif
    			return false;
    		}
    		static void reset (uint32_t node_id, struct PR_NodeData & node ) {
    		#ifdef __GALOIS_HET_CUDA__
    			if (personality == GPU_CUDA) set_node_residual_cuda(cuda_ctx, node_id, 0);
    			else if (personality == CPU)
    		#endif
    				{ node.residual = 0; }
    		}
    		typedef float ValTy;
    	};
    	struct SyncerPull_vertexCut_0 {
    		static float extract(uint32_t node_id, const struct PR_NodeData & node) {
    		#ifdef __GALOIS_HET_CUDA__
    			if (personality == GPU_CUDA) return get_node_residual_cuda(cuda_ctx, node_id);
    			assert (personality == CPU);
    		#endif
    			return node.residual;
    		}
    		static bool extract_batch(unsigned from_id, float *y) {
    		#ifdef __GALOIS_HET_CUDA__
    			if (personality == GPU_CUDA) { batch_get_node_residual_cuda(cuda_ctx, from_id, y); return true; }
    			assert (personality == CPU);
    		#endif
    			return false;
    		}
    		static void setVal (uint32_t node_id, struct PR_NodeData & node, float y) {
    		#ifdef __GALOIS_HET_CUDA__
    			if (personality == GPU_CUDA) set_node_residual_cuda(cuda_ctx, node_id, y);
    			else if (personality == CPU)
    		#endif
    				node.residual = y;
    		}
    		static bool setVal_batch(unsigned from_id, float *y) {
    		#ifdef __GALOIS_HET_CUDA__
    			if (personality == GPU_CUDA) { batch_set_node_residual_cuda(cuda_ctx, from_id, y); return true; }
    			assert (personality == CPU);
    		#endif
    			return false;
    		}
    		typedef float ValTy;
    	};
    #ifdef __GALOIS_HET_CUDA__
    	if (personality == GPU_CUDA) {
    		std::string impl_str("CUDA_DO_ALL_IMPL_InitializeGraph_" + std::to_string(_graph.get_run_num()));
    		Galois::StatTimer StatTimer_cuda(impl_str.c_str());
    		StatTimer_cuda.start();
    		InitializeGraph_cuda(alpha, cuda_ctx);
    		StatTimer_cuda.stop();
    	} else if (personality == CPU)
    #endif
    Galois::do_all(_graph.begin(), _graph.end(), InitializeGraph{ alpha, &_graph }, Galois::loopname("InitializeGraph"), Galois::numrun(_graph.get_run_num()), Galois::write_set("sync_push", "this->graph", "struct PR_NodeData &", "struct PR_NodeData &" , "residual", "float" , "add",  "0"));
    _graph.sync_push<Syncer_0>("InitializeGraph");
    
    if(_graph.is_vertex_cut()) {
    	_graph.sync_pull<SyncerPull_vertexCut_0>("InitializeGraph");
    }
    
  }

  void operator()(GNode src) const {
    PR_NodeData& sdata = graph->getData(src);
    sdata.value = local_alpha;
    sdata.nout = std::distance(graph->edge_begin(src), graph->edge_end(src));

    if(sdata.nout > 0 ){
      float delta = sdata.value*(1-local_alpha)/sdata.nout;
      for(auto nbr = graph->edge_begin(src), ee = graph->edge_end(src); nbr != ee; ++nbr){
        GNode dst = graph->getEdgeDst(nbr);
        PR_NodeData& ddata = graph->getData(dst);
        Galois::atomicAdd(ddata.residual, delta);
      }
    }
  }
};

template <typename GraphTy>
struct Get_info_functor : public Galois::op_tag {
	GraphTy &graph;
	struct Syncer_0 {
		static float extract(uint32_t node_id, const struct PR_NodeData & node) {
		#ifdef __GALOIS_HET_CUDA__
			if (personality == GPU_CUDA) return get_node_residual_cuda(cuda_ctx, node_id);
			assert (personality == CPU);
		#endif
			return node.residual;
		}
		static bool extract_reset_batch(unsigned from_id, float *y) {
		#ifdef __GALOIS_HET_CUDA__
			if (personality == GPU_CUDA) { batch_get_reset_node_residual_cuda(cuda_ctx, from_id, y, 0); return true; }
			assert (personality == CPU);
		#endif
			return false;
		}
		static void reduce (uint32_t node_id, struct PR_NodeData & node, float y) {
		#ifdef __GALOIS_HET_CUDA__
			if (personality == GPU_CUDA) add_node_residual_cuda(cuda_ctx, node_id, y);
			else if (personality == CPU)
		#endif
				{ Galois::add(node.residual, y); }
		}
		static bool reduce_batch(unsigned from_id, float *y) {
		#ifdef __GALOIS_HET_CUDA__
			if (personality == GPU_CUDA) { batch_add_node_residual_cuda(cuda_ctx, from_id, y); return true; }
			assert (personality == CPU);
		#endif
			return false;
		}
		static void reset (uint32_t node_id, struct PR_NodeData & node ) {
		#ifdef __GALOIS_HET_CUDA__
			if (personality == GPU_CUDA) set_node_residual_cuda(cuda_ctx, node_id, 0);
			else if (personality == CPU)
		#endif
				{ node.residual = 0; }
		}
		typedef float ValTy;
	};
	struct SyncerPull_vertexCut_0 {
		static float extract(uint32_t node_id, const struct PR_NodeData & node) {
		#ifdef __GALOIS_HET_CUDA__
			if (personality == GPU_CUDA) return get_node_residual_cuda(cuda_ctx, node_id);
			assert (personality == CPU);
		#endif
			return node.residual;
		}
		static bool extract_batch(unsigned from_id, float *y) {
		#ifdef __GALOIS_HET_CUDA__
			if (personality == GPU_CUDA) { batch_get_node_residual_cuda(cuda_ctx, from_id, y); return true; }
			assert (personality == CPU);
		#endif
			return false;
		}
		static void setVal (uint32_t node_id, struct PR_NodeData & node, float y) {
		#ifdef __GALOIS_HET_CUDA__
			if (personality == GPU_CUDA) set_node_residual_cuda(cuda_ctx, node_id, y);
			else if (personality == CPU)
		#endif
				node.residual = y;
		}
		static bool setVal_batch(unsigned from_id, float *y) {
		#ifdef __GALOIS_HET_CUDA__
			if (personality == GPU_CUDA) { batch_set_node_residual_cuda(cuda_ctx, from_id, y); return true; }
			assert (personality == CPU);
		#endif
			return false;
		}
		typedef float ValTy;
	};
	Get_info_functor(GraphTy& _g): graph(_g){}
	unsigned operator()(GNode n) const {
		return graph.getHostID(n);
	}
	GNode getGNode(uint32_t local_id) const {
		return GNode(graph.getGID(local_id));
	}
	uint32_t getLocalID(GNode n) const {
		return graph.getLID(n);
	}
	void sync_graph(){
		sync_graph_static(graph);
	}
	uint32_t get_run_num() const {
		return graph.get_run_num();
	}
	void static sync_graph_static(Graph& _graph) {

		_graph.sync_push<Syncer_0>("PageRank");

if(_graph.is_vertex_cut()) {
	_graph.sync_pull<SyncerPull_vertexCut_0>("PageRank");
}
	}
};

struct PageRank {
  const float &local_alpha;
  cll::opt<float> &local_tolerance;
  Graph* graph;
  typedef int tt_does_not_need_aborts;

  PageRank(cll::opt<float> &_tolerance, const float &_alpha, Graph* _g): local_tolerance(_tolerance), local_alpha(_alpha), graph(_g){}
  void static go(Graph& _graph) {
    #ifdef __GALOIS_HET_CUDA__
    	if (personality == GPU_CUDA) {
    		unsigned num_iter = 0;
    		auto __sync_functor = Get_info_functor<Graph>(_graph);
    		typedef Galois::DGBag<GNode, Get_info_functor<Graph> > DBag;
    		DBag dbag(__sync_functor, "PageRank");
    		auto &local_wl = DBag::get();
    		std::string impl_str("CUDA_FOR_EACH_IMPL_PageRank_" + std::to_string(_graph.get_run_num()));
    		Galois::StatTimer StatTimer_cuda(impl_str.c_str());
    		StatTimer_cuda.start();
    		cuda_wl.num_in_items = (*(_graph.end())-*(_graph.begin()));
    		for (int __i = *(_graph.begin()); __i < *(_graph.end()); ++__i) cuda_wl.in_items[__i] = __i;
    		cuda_wl.num_out_items = 0;
    		if (cuda_wl.num_in_items > 0)
    			PageRank_cuda(alpha, tolerance, cuda_ctx);
    		StatTimer_cuda.stop();
    		__sync_functor.sync_graph();
    		dbag.set_local(cuda_wl.out_items, cuda_wl.num_out_items);
    		#ifdef __GALOIS_DEBUG_WORKLIST__
    		std::cout << "[" << Galois::Runtime::getSystemNetworkInterface().ID << "] worklist size : " << cuda_wl.num_out_items << " duplication factor : " << (double)cuda_wl.num_out_items/_graph.size() << "\n";
    		#endif
    		dbag.sync();
    		while (!dbag.canTerminate()) {
    		++num_iter;
    		StatTimer_cuda.start();
    		cuda_wl.num_in_items = local_wl.size();
    		if (cuda_wl.num_in_items > cuda_wl.max_size) {
    			std::cout << "[" << Galois::Runtime::getSystemNetworkInterface().ID << "] ERROR - worklist size insufficient; size : " << cuda_wl.max_size << " , expected : " << cuda_wl.num_in_items << "\n";
    			exit(1);
    		}
    		//std::cout << "[" << Galois::Runtime::getSystemNetworkInterface().ID << "] Iter : " << num_iter << " Total items to work on : " << cuda_wl.num_in_items << "\n";
    		std::copy(local_wl.begin(), local_wl.end(), cuda_wl.in_items);
    		cuda_wl.num_out_items = 0;
    		if (cuda_wl.num_in_items > 0)
    			PageRank_cuda(alpha, tolerance, cuda_ctx);
    		StatTimer_cuda.stop();
    		__sync_functor.sync_graph();
    		dbag.set_local(cuda_wl.out_items, cuda_wl.num_out_items);
    		#ifdef __GALOIS_DEBUG_WORKLIST__
    		std::cout << "[" << Galois::Runtime::getSystemNetworkInterface().ID << "] worklist size : " << cuda_wl.num_out_items << " duplication factor : " << (double)cuda_wl.num_out_items/_graph.size() << "\n";
    		#endif
    		dbag.sync();
    		}
    	} else if (personality == CPU)
    #endif
    Galois::for_each(_graph.begin(), _graph.end(), PageRank{ tolerance, alpha, &_graph }, Galois::workList_version(), Galois::does_not_need_aborts<>(), Galois::loopname("PageRank"), Galois::write_set("sync_push", "this->graph", "struct PR_NodeData &", "struct PR_NodeData &" , "residual", "float" , "add",  "0"), Get_info_functor<Graph>(_graph));
  }

  void operator()(WorkItem src, Galois::UserContext<WorkItem>& ctx) const {
    PR_NodeData& sdata = graph->getData(src);
    float residual_old = sdata.residual.exchange(0.0);
    sdata.value += residual_old;
    //sdata.residual = residual_old;
    if (sdata.nout > 0){
      float delta = residual_old*(1-local_alpha)/sdata.nout;
      for(auto nbr = graph->edge_begin(src), ee = graph->edge_end(src); nbr != ee; ++nbr){
        GNode dst = graph->getEdgeDst(nbr);
        PR_NodeData& ddata = graph->getData(dst);
        auto dst_residual_old = Galois::atomicAdd(ddata.residual, delta);

        //Schedule TOLERANCE threshold crossed.
        if((dst_residual_old <= local_tolerance) && ((dst_residual_old + delta) >= local_tolerance)) {
          ctx.push(WorkItem(graph->getGID(dst)));
        }
      }
    }
  }
};

int main(int argc, char** argv) {
  try {

    LonestarStart(argc, argv, name, desc, url);
    Galois::Runtime::reportStat("(NULL)", "Max Iterations", (unsigned long)maxIterations, 0);
    std::ostringstream ss;
    ss << tolerance;
    Galois::Runtime::reportStat("(NULL)", "Tolerance", ss.str(), 0);
    Galois::StatManager statManager;
    auto& net = Galois::Runtime::getSystemNetworkInterface();
    Galois::StatTimer StatTimer_init("TIMER_GRAPH_INIT"), StatTimer_total("TIMER_TOTAL"), StatTimer_hg_init("TIMER_HG_INIT");

    StatTimer_total.start();

    std::vector<unsigned> scalefactor;
#ifdef __GALOIS_HET_CUDA__
    const unsigned my_host_id = Galois::Runtime::getHostID();
    int gpu_device = gpudevice;
    //Parse arg string when running on multiple hosts and update/override personality
    //with corresponding value.
    if (personality_set.length() == Galois::Runtime::NetworkInterface::Num) {
      switch (personality_set.c_str()[my_host_id]) {
      case 'g':
        personality = GPU_CUDA;
        break;
      case 'o':
        assert(0);
        personality = GPU_OPENCL;
        break;
      case 'c':
      default:
        personality = CPU;
        break;
      }
      if ((personality == GPU_CUDA) && (gpu_device == -1)) {
        gpu_device = get_gpu_device_id(personality_set, num_nodes);
      }
      for (unsigned i=0; i<personality_set.length(); ++i) {
        if (personality_set.c_str()[i] == 'c') 
          scalefactor.push_back(scalecpu);
        else
          scalefactor.push_back(scalegpu);
      }
    }
#endif

    StatTimer_hg_init.start();
    Graph* hg;
    if(enableVCut){
      hg = new Graph_vertexCut(inputFile,partFolder, net.ID, net.Num, scalefactor);
    }
    else {
      hg = new Graph_edgeCut(inputFile,partFolder, net.ID, net.Num, scalefactor);
    }

#ifdef __GALOIS_HET_CUDA__
    if (personality == GPU_CUDA) {
      cuda_ctx = get_CUDA_context(my_host_id);
      if (!init_CUDA_context(cuda_ctx, gpu_device))
        return -1;
      MarshalGraph m = (*hg).getMarshalGraph(my_host_id);
      load_graph_CUDA(cuda_ctx, &cuda_wl, cuda_wl_dup_factor, m, net.Num);
    } else if (personality == GPU_OPENCL) {
      //Galois::OpenCL::cl_env.init(cldevice.Value);
    }
#endif
    StatTimer_hg_init.stop();

    std::cout << "[" << net.ID << "] InitializeGraph::go called\n";
    StatTimer_init.start();
      InitializeGraph::go((*hg));
    StatTimer_init.stop();

    for(auto run = 0; run < numRuns; ++run){
      std::cout << "[" << net.ID << "] PageRank::go run " << run << " called\n";
      std::string timer_str("TIMER_" + std::to_string(run));
      Galois::StatTimer StatTimer_main(timer_str.c_str());

      StatTimer_main.start();
        PageRank::go((*hg));
      StatTimer_main.stop();

      if((run + 1) != numRuns){
        Galois::Runtime::getHostBarrier().wait();
        (*hg).reset_num_iter(run+1);
        ResetGraph::go((*hg));
        InitializeGraph::go((*hg));
      }
    }

   StatTimer_total.stop();

    // Verify
    if(verify){
#ifdef __GALOIS_HET_CUDA__
      if (personality == CPU) { 
#endif
        for(auto ii = (*hg).begin(); ii != (*hg).end(); ++ii) {
          Galois::Runtime::printOutput("% %\n", (*hg).getGID(*ii), (*hg).getData(*ii).value);
        }
#ifdef __GALOIS_HET_CUDA__
      } else if(personality == GPU_CUDA)  {
        for(auto ii = (*hg).begin(); ii != (*hg).end(); ++ii) {
          Galois::Runtime::printOutput("% %\n", (*hg).getGID(*ii), get_node_value_cuda(cuda_ctx, *ii));
        }
      }
#endif
    }

    return 0;
  } catch (const char* c) {
      std::cerr << "Error: " << c << "\n";
      return 1;
  }
}
