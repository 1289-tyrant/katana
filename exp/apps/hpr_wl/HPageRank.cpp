/** Page rank application -*- C++ -*-
 * @file
 * @section License
 *
 * Galois, a framework to exploit amorphous data-parallelism in irregular
 * programs.
 *
 * Copyright (C) 2011, The University of Texas at Austin. All rights reserved.
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
 * @author Andrew Lenharth <andrew@lenharth.org>
 * @author Jorge Silva <up201007483@alunos.dcc.fc.up.pt>
 **/

#include "Galois/Galois.h"
#include "Galois/gstl.h"
#include "Galois/Graph/FileGraph.h"
#include "Galois/Graph/LC_CSR_Graph.h"
#include "Galois/Graph/Util.h"
#include "Lonestar/BoilerPlate.h"

#include "cuda/hpr_cuda.h"
#include "cuda/cuda_mtypes.h"
#include "hpr.h"

#include "opencl/OpenCLPrBackend.h"

#include <iostream>
#include <typeinfo>
#include <algorithm>
#include <queue>
#include <list>
#include <time.h>
#include <chrono>

#define _HETERO_DEBUG_ 1
#define _MICRO_CONV_ 1000000.0 //to convert microsec to sec


static const char* const name = "Page Rank - Distributed Heterogeneous";
static const char* const desc = "Computes PageRank on Distributed Galois.  Uses pull algorithm, takes the pre-transposed graph.";
static const char* const url = 0;

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
    assert(false && "Invalid personality");
    return "";
}


namespace cll = llvm::cl;
static cll::opt<Personality> personality("personality", cll::desc("Personality"),
        cll::values(clEnumValN(CPU, "cpu", "Galois CPU"), clEnumValN(GPU_CUDA, "gpu/cuda", "GPU/CUDA"), clEnumValN(GPU_OPENCL, "gpu/opencl", "GPU/OpenCL"), clEnumValEnd),
        cll::init(CPU));
static cll::opt<std::string> inputFile(cll::Positional, cll::desc("<input file (transpose)>"), cll::Required);
static cll::opt<unsigned int> maxIterations("maxIterations", cll::desc("Maximum iterations"), cll::init(4));
static cll::opt<bool> verify("verify", cll::desc("Verify ranks by printing to 'page_ranks.#hid.csv' file"), cll::init(false));
static cll::opt<int> gpudevice("gpu", cll::desc("Select GPU to run on, default is to choose automatically"), cll::init(-1));
static cll::opt<float> cldevice("cldevice", cll::desc("Select OpenCL device to run on , default is 0.0 (OpenCL backend)"), cll::init(0.0));
static cll::opt<std::string> personality_set("pset", cll::desc("String specifying personality for each host. 'c'=CPU,'g'=GPU/CUDA and 'o'=GPU/OpenCL"), cll::init(""));

struct LNode {
    float value;
    unsigned int nout;
};

//to control the work
struct WorkManager {
    int active_workers;
    bool first_time;
    int my_amount_of_work;
    int barrier_counter; // to test a different barrier
};

typedef Galois::Graph::LC_CSR_Graph<LNode, void> Graph;
typedef typename Graph::GraphNode GNode;
std::map<GNode, float> buffered_updates;

WorkManager MWork;

//////////////////////////////////////////////////////////////////////////////////////
typedef Galois::OpenCL::LC_LinearArray_Graph<Galois::OpenCL::Array, LNode, void> DeviceGraph;

struct CUDA_Context *cuda_ctx;
struct OPENCL_Context<DeviceGraph> cl_ctx;


/*********************************************************************************
 * Partitioned graph structure.
 **********************************************************************************/
struct pGraph {
    Graph& g;
    unsigned g_offset; // LID + g_offset = GID
    unsigned numOwned; // [0, numOwned) = global nodes owned, thus [numOwned, numNodes) are replicas
    unsigned numNodes; // number of nodes (may differ from g.size() to simplify loading)
    unsigned numEdges;
    
    std::list<unsigned> myWork;
    std::list<unsigned> myFutureWork;
    // [numNodes, g.size()) should be ignored
    std::vector<unsigned> L2G; // GID = L2G[LID - numOwned]
    unsigned id; // my hostid
    std::vector<unsigned> lastNodes; //[ [i - 1], [i]) -> Node owned by host i

    unsigned getHost(unsigned node) { // node is GID
        return std::distance(lastNodes.begin(), std::upper_bound(lastNodes.begin(), lastNodes.end(), node));
    }

    unsigned G2L(unsigned GID) {
        auto ii = std::find(L2G.begin(), L2G.end(), GID);
        assert(ii != L2G.end());
        return std::distance(L2G.begin(), ii) + numOwned;
    }

    pGraph(Graph& _g) :
    g(_g), g_offset(0), numOwned(0), numNodes(0), id(0), numEdges(0) {
    }
};

/*********************************************************************************
 * Given a partitioned graph  .
 * lastNodes maintains indices of nodes for each co-host. This is computed by
 * determining the number of nodes for each partition in 'pernum', and going over
 * all the nodes assigning the next 'pernum' nodes to the ith partition.
 * The lastNodes is used to find the host by a binary search.
 **********************************************************************************/

void loadLastNodes(pGraph &g, size_t size, unsigned numHosts) {
    if (numHosts == 1)
        return;

    auto p = Galois::block_range(0UL, size, 0, numHosts);
    unsigned pernum = p.second - p.first;
    unsigned pos = pernum;

    while (pos < size) {
        g.lastNodes.push_back(pos);
        pos += pernum;
    }
#if _HETERO_DEBUG_
    for (int i = 0; size < 10 && i < size; i++) {
        printf("node %d owned by %d\n", i, g.getHost(i));
    }
#endif
}

/*********************************************************************************
 * Load a partitioned graph from a file.
 * @param file the graph filename to be loaded.
 * @param hostID the identifier of the current host.
 * @param numHosts the total number of hosts.
 * @param out A graph instance that stores the actual graph.
 * @return a partitioned graph backed by the #out instance.
 **********************************************************************************/
pGraph loadGraph(std::string file, unsigned hostID, unsigned numHosts, Graph& out) {
    pGraph retval{out};
    Galois::Graph::FileGraph fg;
    fg.fromFile(file);
    auto p = Galois::block_range(0UL, fg.size(), hostID, numHosts);
    retval.g_offset = p.first;
    retval.numOwned = p.second - p.first;
    retval.id = hostID;
    std::vector<unsigned> perm(fg.size(), ~0); //[i (orig)] -> j (final)
    unsigned nextSlot = 0;
    //   std::cout << fg.size() << " " << p.first << " " << p.second << "\n";
    //Fill our partition
    for (unsigned i = p.first; i < p.second; ++i) {
        //retval.myWork.push_back(nextSlot); //initially define every node has potential work
        //printf("%d: owned: %d local: %d\n", hostID, i, nextSlot);
        perm[i] = nextSlot++;
    }
    //find ghost cells
    for (auto ii = fg.begin() + p.first; ii != fg.begin() + p.second; ++ii) {
        for (auto jj = fg.edge_begin(*ii); jj != fg.edge_end(*ii); ++jj) {
            //std::cout << *ii << " " << *jj << " " << nextSlot << " " << perm.size() << "\n";
            //      assert(*jj < perm.size());
            auto dst = fg.getEdgeDst(jj);
            if (perm.at(dst) == ~0) {
                //printf("%d: ghost: %d local: %d\n", hostID, dst, nextSlot);
                perm[dst] = nextSlot++;
                retval.L2G.push_back(dst);
            }
        }
    }
    retval.numNodes = nextSlot;

    //Fill remainder of graph since permute doesn't support truncating
    for (auto ii = fg.begin(); ii != fg.end(); ++ii)
        if (perm[*ii] == ~0)
            perm[*ii] = nextSlot++;
    //   std::cout << nextSlot << " " << fg.size() << "\n";
    assert(nextSlot == fg.size());
    //permute graph
    Galois::Graph::FileGraph fg2;
    Galois::Graph::permute<void>(fg, perm, fg2);
    Galois::Graph::readGraph(retval.g, fg2);

    loadLastNodes(retval, fg.size(), numHosts);

    /* TODO: This still counts edges from ghosts to remote nodes,
     ideally we only want edges from ghosts to local nodes.

     See pGraphToMarshalGraph for one implementation.
     */

    retval.numEdges = std::distance(retval.g.edge_begin(*retval.g.begin()), retval.g.edge_end(*(retval.g.begin() + retval.numNodes - 1)));

    return retval;
}

/*********************************************************************************
 *
 **********************************************************************************/
struct InitializeGraph {
    Graph* g;

    void static go(Graph& _g, unsigned num) {

        Galois::do_all(_g.begin(), _g.begin() + num, InitializeGraph {
            &_g}, Galois::loopname("init"));
    }

    void operator()(GNode src) const {
        LNode& sdata = g->getData(src);
        //sdata.value = (alpha / g->size()) + ((1.0 - alpha)/g->size());
        sdata.value = 1.0 - alpha;
        buffered_updates[src] = 0;
        for (auto nbr = g->edge_begin(src); nbr != g->edge_end(src); ++nbr) {
            __sync_fetch_and_add(&g->getData(g->getEdgeDst(*nbr)).nout, 1);
        }
    }
};

/*********************************************************************************
 * CPU PageRank operator implementation.
 **********************************************************************************/
struct WriteBack {
    Graph * g;

    void static go(Graph& _g, unsigned num) {
        Galois::do_all(_g.begin(), _g.begin() + num, WriteBack {
            &_g}, Galois::loopname("Writeback"));
    }

    void operator()(GNode src) const {
        LNode& sdata = g->getData(src);
        sdata.value = buffered_updates[src];
        buffered_updates[src] = 0;
    }
};

struct PageRank {
    Graph* g;

    //void static go(Graph& _g, unsigned num) {
    void static go(pGraph& pg) {
        //std::cout << pg.id << " List has " << pg.myWork.size() << " elements\n";
        while (!pg.myWork.empty()) { //process all nodes on the list
            unsigned node = pg.myWork.front(); //get the first element
            pg.myWork.pop_front(); //remove element
            LNode& sdata = pg.g.getData(node);
            double sum = 0;
            for (auto jj = pg.g.edge_begin(node); jj != pg.g.edge_end(node); ++jj) {
                GNode dst = pg.g.getEdgeDst(jj);
                LNode& ddata = pg.g.getData(dst);
                sum += ddata.value / ddata.nout;
            }
            double old_value = sdata.value;
            //sdata.value = (1.0 - alpha) * sum + (alpha / pg.g.size());
            sdata.value = (1.0 - alpha) * sum + alpha;
            //update the value right way, instead of using auxiliary vector. Need to check if this has some kind of strange effect
            
            double diff = std::fabs(sdata.value - old_value);
            //std::cout << "node: " << node << " score " << old_value << " changed to: " << sdata.value << " diff: " << diff << " tolerance " << ERROR_THRESHOLD <<"\n";
            if (diff > ERROR_THRESHOLD) {//&& (std::find(pg.myFutureWork.begin(), pg.myFutureWork.end(), node) != node)) { 
                //if strategy is changed that you may have to take into account if a node is already on the second list 
                pg.myFutureWork.push_back(node);
            }
        }
        //std::cout << "size old work " << pg.myWork.size() << " new work " << pg.myFutureWork.size() << "\n"; 
        pg.myWork.swap(pg.myFutureWork); //swap lists 
    }
};
/*********************************************************************************
 *
 **********************************************************************************/

// [hostid] -> vector of GID that host has replicas of
std::vector<std::vector<unsigned> > remoteReplicas;
// [hostid] -> remote pGraph Structure (locally invalid)
std::vector<pGraph*> magicPointer;

/*********************************************************************************
 *
 **********************************************************************************/

//An active work just ended its work
void decreaseActiveWorkers(uint32_t hostID) {
    //std::cout <<  Galois::Runtime::NetworkInterface::ID << " received a notice that " << hostID << " stopped working, alive workers: " << MWork.active_workers << "\n";
    MWork.active_workers --;
}

void decreaseBarrier(uint32_t hostID){ //creating my own barrier
    MWork.barrier_counter --;
    std::cout <<  Galois::Runtime::NetworkInterface::ID << " received a notice that " << hostID << " reached barrier " << MWork.barrier_counter << "\n";
}


void setRemotePtr(uint32_t hostID, pGraph* p) {
    if (hostID >= magicPointer.size())
        magicPointer.resize(hostID + 1);
    magicPointer[hostID] = p;
}

/*********************************************************************************
 *
 **********************************************************************************/

void recvNodeStatic(unsigned GID, uint32_t hostID) {
    if (hostID >= remoteReplicas.size())
        remoteReplicas.resize(hostID + 1);
    remoteReplicas[hostID].push_back(GID);
}

/*********************************************************************************
 *
 **********************************************************************************/

void setNodeValue(pGraph* p, unsigned GID, float v) {
    switch (personality) {
        case CPU:
            p->g.getData(p->G2L(GID)).value = v;
            break;
        case GPU_CUDA:
            setNodeValue_CUDA(cuda_ctx, p->G2L(GID), v);
            break;
        case GPU_OPENCL:
            cl_ctx.getData(p->G2L(GID)).value = v;
            break;
        default:
            break;
    }
}
/*********************************************************************************
 *
 **********************************************************************************/
// could be merged with setNodeValue, but this is one-time only ...

void setNodeAttr(pGraph *p, unsigned GID, unsigned nout) {
    switch (personality) {
        case CPU:
            p->g.getData(p->G2L(GID)).nout = nout;
            break;
        case GPU_CUDA:
            setNodeAttr_CUDA(cuda_ctx, p->G2L(GID), nout);
            break;
        case GPU_OPENCL:
            cl_ctx.getData(p->G2L(GID)).nout = nout;
            break;
        default:
            break;
    }
}
/*********************************************************************************
 *
 **********************************************************************************/
// send values for nout calculated on my node

void setNodeAttr2(pGraph *p, unsigned GID, unsigned nout) {
    auto LID = GID - p->g_offset;
    //printf("%d setNodeAttrs2 GID: %u nout: %u LID: %u\n", p->id, GID, nout, LID);
    switch (personality) {
        case CPU:
            p->g.getData(LID).nout += nout;
            break;
        case GPU_CUDA:
            setNodeAttr2_CUDA(cuda_ctx, LID, nout);
            break;
        case GPU_OPENCL:
            cl_ctx.getData(LID).nout += nout;
            break;
        default:
            break;
    }
}

/*********************************************************************************
 *
 **********************************************************************************/
void sendGhostCellAttrs2(Galois::Runtime::NetworkInterface& net, pGraph& g) {
    for (auto n = g.g.begin() + g.numOwned; n != g.g.begin() + g.numNodes; ++n) {
        auto l2g_ndx = std::distance(g.g.begin(), n) - g.numOwned;
        auto x = g.getHost(g.L2G[l2g_ndx]);
        //printf("%d: sendAttr2 GID: %d own: %d\n", g.id, g.L2G[l2g_ndx], x);
        switch (personality) {
            case CPU:
                net.sendAlt(x, setNodeAttr2, magicPointer[x], g.L2G[l2g_ndx], g.g.getData(*n).nout);
                break;
            case GPU_CUDA:
                net.sendAlt(x, setNodeAttr2, magicPointer[x], g.L2G[l2g_ndx], getNodeAttr2_CUDA(cuda_ctx, *n));
                break;
            case GPU_OPENCL:
                net.sendAlt(x, setNodeAttr2, magicPointer[x], g.L2G[l2g_ndx], cl_ctx.getData(*n).nout);
                break;
            default:
                assert(false);
                break;
        }
    }
}

/*********************************************************************************
 *
 * 
 * 
 * 
 **********************************************************************************/

void sendGhostCellAttrs(Galois::Runtime::NetworkInterface& net, pGraph& g) {
    for (unsigned x = 0; x < remoteReplicas.size(); ++x) {
        for (auto n : remoteReplicas[x]) {
            /* no per-personality needed but until nout is
             fixed for CPU and OpenCL ... */

            switch (personality) {
                case CPU:
                    net.sendAlt(x, setNodeAttr, magicPointer[x], n, g.g.getData(n - g.g_offset).nout);
                    break;
                case GPU_CUDA:
                    net.sendAlt(x, setNodeAttr, magicPointer[x], n, getNodeAttr_CUDA(cuda_ctx, n - g.g_offset));
                    break;
                case GPU_OPENCL:
                    net.sendAlt(x, setNodeAttr, magicPointer[x], n, cl_ctx.getData(n - g.g_offset).nout);
                    break;
                default:
                    assert(false);
                    break;
            }
        }
    }
}

/*********************************************************************************
 * Send ghost-cell updates to all hosts that require it. Go over all the remotereplica
 * arrays, and for each array, go over all the elements and send it to the host 'x'.
 * Note that we use the magicPointer array to obtain the reference of the graph object
 * where the node data is to be set.
 **********************************************************************************/
void sendGhostCells(Galois::Runtime::NetworkInterface& net, pGraph& g) {
    for (unsigned x = 0; x < remoteReplicas.size(); ++x) {
        for (auto n : remoteReplicas[x]) {
            switch (personality) {
                case CPU:
                    net.sendAlt(x, setNodeValue, magicPointer[x], n, g.g.getData(n - g.g_offset).value);
                    break;
                case GPU_CUDA:
                    net.sendAlt(x, setNodeValue, magicPointer[x], n, getNodeValue_CUDA(cuda_ctx, n - g.g_offset));
                    break;
                case GPU_OPENCL:
                    net.sendAlt(x, setNodeValue, magicPointer[x], n, cl_ctx.getData((n - g.g_offset)).value);
                    break;
                default:
                    assert(false);
                    break;
            }
        }
    }
}



/*********************************************************************************
 *
 **********************************************************************************/

MarshalGraph pGraph2MGraph(pGraph &g) {
    MarshalGraph m;

    m.nnodes = g.numNodes;
    m.nedges = g.numEdges;
    m.nowned = g.numOwned;
    m.g_offset = g.g_offset;
    m.id = g.id;
    m.row_start = (index_type *) calloc(m.nnodes + 1, sizeof (index_type));
    m.edge_dst = (index_type *) calloc(m.nedges, sizeof (index_type));

    // TODO: initialize node_data and edge_data
    m.node_data = NULL;
    m.edge_data = NULL;

    // pinched from Rashid's LC_LinearArray_Graph.h

    size_t edge_counter = 0, node_counter = 0;
    for (auto n = g.g.begin(); n != g.g.end() && *n != m.nnodes; n++, node_counter++) {
        m.row_start[node_counter] = edge_counter;
        if (*n < g.numOwned) {
            for (auto e = g.g.edge_begin(*n); e != g.g.edge_end(*n); e++) {
                if (g.g.getEdgeDst(e) < g.numNodes)
                    m.edge_dst[edge_counter++] = g.g.getEdgeDst(e);
            }
        }
    }

    m.row_start[node_counter] = edge_counter;
    m.nedges = edge_counter;
    //   printf("dropped %u edges (g->r, g->l, g->g)\n", g.numEdges - m.nedges);
    // for(int i = 0; i < node_counter; i++) {
    //   printf("%u ", m.row_start[i]);
    // }

    // for(int i = 0; i < edge_counter; i++) {
    //   printf("%u ", m.edge_dst[i]);
    // }

    // printf("nc: %zu ec: %zu\n", node_counter, edge_counter);
    return m;
}

/*********************************************************************************
 *
 **********************************************************************************/

void loadGraphNonCPU(pGraph &g) {
    MarshalGraph m;
    assert(personality != CPU);
    switch (personality) {
        case GPU_CUDA:
            m = pGraph2MGraph(g);
            load_graph_CUDA(cuda_ctx, m);
            break;
        case GPU_OPENCL:
            cl_ctx.loadGraphNonCPU(g.g, g.numOwned, g.numEdges, g.numNodes - g.numOwned);
            break;
        default:
            assert(false);
            break;
    }
    // TODO cleanup marshalgraph, leaks memory!
}


void myBarrier(int myID) { //very lazy implementation of a barrier just to compare against the one that is cause massive negative impact on parallel performance
    //Sending messages is the problem, this barrier didn't improve nothing at all!!
    /*MWork.barrier_counter--; //decrease own
    for (uint32_t x = 0; x < Galois::Runtime::NetworkInterface::Num; ++x) { //Notify everyone that it has reached the barrier
        if (x==myID){ //avoid sending to itself
            continue;
        }
        std::cout << myID << " - " << Galois::Runtime::NetworkInterface::Num << " - " << 
                Galois::Runtime::NetworkInterface::ID << " - " << x << "\n";
        net.sendAlt(x, decreaseBarrier, Galois::Runtime::NetworkInterface::ID);
    }
    //std::cout << myID << "reached barrier after decreasing its own " << MWork.barrier_counter << "\n";
    while (MWork.barrier_counter > 0) { //while others don't reach the barrie
     * //should change this to a fast sleep or something
        myID = myID;
    }
    MWork.barrier_counter = Galois::Runtime::NetworkInterface::Num; //setup barrier for the next time*/
    return;
}

/*********************************************************************************
 *
 **********************************************************************************/
void inner_main() { 
    auto& net = Galois::Runtime::getSystemNetworkInterface();
    Galois::StatManager statManager;
    auto& barrier = Galois::Runtime::getSystemBarrier();
    const unsigned my_host_id = Galois::Runtime::NetworkInterface::ID;
    //Parse arg string when running on multiple hosts and update/override personality
    //with corresponding value.
    if (personality_set.length() == Galois::Runtime::NetworkInterface::Num) {
        switch (personality_set.c_str()[Galois::Runtime::NetworkInterface::ID]) {
            case 'g':
                personality = GPU_CUDA;
                break;
            case 'o':
                personality = GPU_OPENCL;
                break;
            case 'c':
            default:
                personality = CPU;
                break;
        }
    }
    
    
    fprintf(stderr, "Pre-barrier - Host: %d, Personality %s\n", Galois::Runtime::NetworkInterface::ID, personality_str(personality).c_str());
    barrier.wait();
    fprintf(stderr, "Post-barrier - Host: %d, Personality %s\n", Galois::Runtime::NetworkInterface::ID, personality_str(personality).c_str());
    Graph rg;
    pGraph g = loadGraph(inputFile, Galois::Runtime::NetworkInterface::ID, Galois::Runtime::NetworkInterface::Num, rg);

    if (personality == GPU_CUDA) {
        cuda_ctx = get_CUDA_context(Galois::Runtime::NetworkInterface::ID);
        if (!init_CUDA_context(cuda_ctx, gpudevice))
            return;
    } else if (personality == GPU_OPENCL) {
        Galois::OpenCL::cl_env.init(cldevice);
    }
    if (personality != CPU)
        loadGraphNonCPU(g);
#if _HETERO_DEBUG_
    std::cout << g.id << " graph loaded\n";
#endif

    //local initialization
    if (personality == CPU) {
        InitializeGraph::go(g.g, g.numOwned);
    } else if (personality == GPU_CUDA) {
        initialize_graph_cuda(cuda_ctx);
    } else if (personality == GPU_OPENCL) {
        cl_ctx.init(g.numOwned, g.numNodes);
    }
#if _HETERO_DEBUG_
    std::cout << g.id << " initialized\n";
#endif
    barrier.wait();
    //send pGraph pointers
    for (uint32_t x = 0; x < Galois::Runtime::NetworkInterface::Num; ++x)
        net.sendAlt(x, setRemotePtr, Galois::Runtime::NetworkInterface::ID, &g);
    //Ask for cells
    for (auto GID : g.L2G)
        net.sendAlt(g.getHost(GID), recvNodeStatic, GID, Galois::Runtime::NetworkInterface::ID);
#if _HETERO_DEBUG_
    std::cout << "[" << my_host_id << "]:ask for remote replicas\n";
#endif
    barrier.wait();

    // send out partial contributions for nout from local -> ghost
    sendGhostCellAttrs2(net, g);
    barrier.wait();
    
    //initialize active workers
    MWork.active_workers = Galois::Runtime::NetworkInterface::Num;
    MWork.barrier_counter = Galois::Runtime::NetworkInterface::Num;
    MWork.first_time = true;
    
    
     //initialize work list in case its a CPU
    if (personality == CPU) {
        for (auto i = g.g.begin(); i != g.g.begin() + g.numOwned; ++i) {
            g.myWork.push_back(*i);
        }
    }

    // send final nout values to remote replicas
#if _HETERO_DEBUG_
    std::cout << "[" << my_host_id << "]:ask for ghost cell attrs\n";
#endif
    sendGhostCellAttrs(net, g);
    barrier.wait();
    int counter = 0, total_nodes = g.numOwned;
    MWork.my_amount_of_work = g.numOwned;
    
    std::chrono::high_resolution_clock::time_point tStart = std::chrono::high_resolution_clock::now();
    long int duration;
    #if _HETERO_DEBUG_
        std::chrono::high_resolution_clock::time_point bStart, bEnd, cStart, cEnd, compStart, compEnd;
        double comp_time = 0.0;//computation time
        double b_time = 0.0; //time wasted on barriers
        double c_time = 0.0; //time wasted on communication
    #endif
    
    //Even after the work has being done on this device, it will still have to go thourgh the barrier.wait otherwise the other processes won't finish.
    while (MWork.active_workers > 0) {
        #if _HETERO_DEBUG_
            std::cout << "debug," << g.id << "," << counter << "," << MWork.my_amount_of_work << "," << MWork.active_workers << "\n";
        #endif
        counter++;
        if (counter > 50) { //in case something goes wrong it won't cycle forever
            break;
        }
        
        //send the updates
        //maybe not all nodes should be updated. I could change the Node struct to store the value of previous iteration and check if it changed. If it didn't, then sending a ghost cell is useless.
        #if _HETERO_DEBUG_
            cStart = std::chrono::high_resolution_clock::now();
            //its pointless to measure this one, this part only request the messagen sent and then waits on the barrier
        #endif
        sendGhostCells(net, g);
        //only nodes that actually changed should be sent. 
        //Also, wouldn't be more efficient if you go for each device, collect all nodes that you have to send and then send them all together
        
        #if _HETERO_DEBUG_
            cEnd = std::chrono::high_resolution_clock::now();
            duration = std::chrono::duration_cast<std::chrono::microseconds>( cEnd - cStart ).count();
            c_time += duration/_MICRO_CONV_;
            bStart = std::chrono::high_resolution_clock::now();
        #endif
        //myBarrier(g.id);
        barrier.wait(); 
         #if _HETERO_DEBUG_
            bEnd = std::chrono::high_resolution_clock::now();
            duration = std::chrono::duration_cast<std::chrono::microseconds>( bEnd - bStart ).count();
            b_time += duration/_MICRO_CONV_;
            compStart = std::chrono::high_resolution_clock::now();
        #endif
        switch (personality) {
            case CPU:
                PageRank::go(g);
                MWork.my_amount_of_work = g.myWork.size();
                total_nodes += MWork.my_amount_of_work;
                break;
            case GPU_OPENCL:
                MWork.my_amount_of_work = cl_ctx(g.numOwned);
                //std::cout << "test_work " << MWork.my_amount_of_work;
                break;
            case GPU_CUDA:
                //std::cout << MWork.my_amount_of_work << " TESTING\n";
                MWork.my_amount_of_work = pagerank_cuda(cuda_ctx);
                total_nodes += MWork.my_amount_of_work;
                break;
            default:
                std::cout << "Wrong choice\n";
                break;                
        }
        #if _HETERO_DEBUG_
            //std::cout << g.myWork.size() << " " << MWork.active_workers << "\n";
        #endif   
        if (MWork.my_amount_of_work == 0 && MWork.first_time) { //Check if node ran out of work. Need the first_time otherwise at a certain point we are always decreasing the number of workrers
            #if _HETERO_DEBUG
                std::cout << g.id << " out of work\n";
            #endif
            MWork.first_time = false;
            MWork.active_workers--;
            for (uint32_t x = 0; x < Galois::Runtime::NetworkInterface::Num; x++) { //Notify everyone that this device work has ended
                if (x==g.id){ //this must be added otherwise some processes will be stuck because of the barrier and the net.sendAlt to itself
                    continue;
                }
                net.sendAlt(x, decreaseActiveWorkers, g.id);   
            }
            
        } 
        #if _HETERO_DEBUG_
            compEnd = std::chrono::high_resolution_clock::now();
            duration = std::chrono::duration_cast<std::chrono::microseconds>( compEnd - compStart ).count();
            comp_time += duration/_MICRO_CONV_;
            bStart = std::chrono::high_resolution_clock::now();
        #endif
        //wait for all computations
        //myBarrier(g.id);
        barrier.wait();
         #if _HETERO_DEBUG_
            bEnd = std::chrono::high_resolution_clock::now();
            duration = std::chrono::duration_cast<std::chrono::microseconds>( bEnd - bStart ).count();
            b_time += duration/_MICRO_CONV_;
        #endif
       
    }
        
    //Final synchronization to ensure that all the nodes are updated.
    sendGhostCells(net, g);
    barrier.wait();
    //myBarrier(g.id);
    std::chrono::high_resolution_clock::time_point tEnd = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::microseconds>( tEnd - tStart ).count();
    float t_time = duration / _MICRO_CONV_; //cast to second
    std::cout << g.id <<" duration:" << t_time << " iterations: " << counter << " nodes: " << total_nodes << "\n";
    // w_time, c_time,comp_time, counter, total_nodes
    #if _HETERO_DEBUG_
            std::cout << g.id <<" barrier_time: " << b_time << " comm_time " << c_time << " computations_time " << comp_time << "\n";
    #endif
    
    if (verify) {
        //printing final results
        //this debug won't work when using GPU's
        /*std::cout << my_host_id <<" PRINTING FINAL PR AFTER " << counter << " ITERATIONS\n";
        for (auto i = g.g.begin(); i != g.g.begin() + g.numOwned; ++i) {
            //LNode n = g.g.getData(i);
            std::cout << "NODE " << *i + g.g_offset << " has PR: " << g.g.getData(*i).value << "\n";
        }*/
        std::stringstream ss;
        ss << personality_str(personality) << "_" << my_host_id << "_of_" << Galois::Runtime::NetworkInterface::Num << "_page_ranks.csv";
        std::ofstream out_file(ss.str());
        switch (personality) {
            case CPU:
            {
                for (auto n = g.g.begin(); n != g.g.begin() + g.numOwned; ++n) {
                    out_file << *n + g.g_offset << ", " << g.g.getData(*n).value << ", " << g.g.getData(*n).nout << "\n";
                }
                break;
            }
            case GPU_OPENCL:
            {
                for (int n = 0; n < g.numOwned; ++n) {
                    out_file << n + g.g_offset << ", " << cl_ctx.getData(n).value << ", " << cl_ctx.getData(n).nout << "\n";
                }
                break;
            }
            case GPU_CUDA:
                for (int n = 0; n < g.numOwned; n++) {
                    out_file << n + g.g_offset << ", " << getNodeValue_CUDA(cuda_ctx, n) << ", " << getNodeAttr_CUDA(cuda_ctx, n) << "\n";
                }
                break;
        }
        out_file.close();
    }
    std::cout.flush();

}

int main(int argc, char** argv) {
    LonestarStart(argc, argv, name, desc, url);
    auto& net = Galois::Runtime::getSystemNetworkInterface();
    inner_main();
    return 0;
}
