/** partitioned graph wrapper -*- C++ -*-
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
 * @author Andrew Lenharth <andrewl@lenharth.org>
 * @author Gurbinder Gill <gurbinder533@gmail.com>
 */

#include "Galois/gstl.h"
#include "Galois/Graphs/LC_CSR_Graph.h"
#include "Galois/Runtime/Substrate.h"
#include "Galois/Runtime/Network.h"

//#include "Galois/Runtime/Barrier.h"
#include "Galois/Runtime/Serialize.h"
#include "Galois/Statistic.h"

#include "Galois/Runtime/GlobalObj.h"
#include "Galois/Runtime/OfflineGraph.h"

#ifdef __GALOIS_HET_CUDA__
#include "Galois/Runtime/Cuda/cuda_mtypes.h"
#endif

#ifdef __GALOIS_HET_OPENCL__
#include "Galois/OpenCL/CL_Header.h"
#endif

#ifdef __GALOIS_SIMULATE_COMMUNICATION__
#ifdef __GALOIS_SIMULATE_BARE_MPI_COMMUNICATION__
#include "mpi.h"
#endif
#endif

#ifndef _GALOIS_DIST_HGRAPH_H
#define _GALOIS_DIST_HGRAPH_H

template<typename NodeTy, typename EdgeTy, bool BSPNode = false, bool BSPEdge = false>
class hGraph: public GlobalObject {

   typedef typename std::conditional<BSPNode, std::pair<NodeTy, NodeTy>, NodeTy>::type realNodeTy;
   typedef typename std::conditional<BSPEdge, std::pair<EdgeTy, EdgeTy>, EdgeTy>::type realEdgeTy;

   typedef Galois::Graph::LC_CSR_Graph<realNodeTy, realEdgeTy> GraphTy;

   GraphTy graph;bool round;
   uint64_t totalNodes; // Total nodes in the complete graph.
   uint32_t numOwned; // [0, numOwned) = global nodes owned, thus [numOwned, numNodes are replicas
   uint64_t globalOffset; // [numOwned, end) + globalOffset = GID
   const unsigned id; // my hostid // FIXME: isn't this just Network::ID?
   //ghost cell ID translation
   std::vector<uint64_t> ghostMap; // GID = ghostMap[LID - numOwned]
   std::vector<std::pair<uint32_t, uint32_t> > hostNodes; //LID Node owned by host i. Stores ghost nodes from each host.
   //pointer for each host
   std::vector<uintptr_t> hostPtrs;

  //memoization optimization
  std::vector<std::vector<size_t>> slaveNodes; // slave nodes from different hosts. For sync_push
  std::vector<std::vector<size_t>> masterNodes; // master nodes on different hosts. For sync_pull

   //GID to owner
   std::vector<std::pair<uint64_t, uint64_t>> gid2host;

   uint32_t num_recv_expected; // Number of receives expected for local completion.
   uint32_t num_iter_push; //Keep track of number of iterations.
   uint32_t num_iter_pull; //Keep track of number of iterations.
   uint32_t num_run; //Keep track of number of iterations.

  //Stats: for rough estimate of sendBytes.
   Galois::Statistic statGhostNodes;

   //host -> (lid, lid]
   std::pair<uint32_t, uint32_t> nodes_by_host(uint32_t host) const {
      return hostNodes[host];
   }

   std::pair<uint64_t, uint64_t> nodes_by_host_G(uint32_t host) const {
      return gid2host[host];
   }

   uint64_t L2G(uint32_t lid) const {
      assert(lid < graph.size());
      if (lid < numOwned)
         return lid + globalOffset;
      return ghostMap[lid - numOwned];
   }

   uint32_t G2L(uint64_t gid) const {
      if (gid >= globalOffset && gid < globalOffset + numOwned)
         return gid - globalOffset;
      auto ii = std::lower_bound(ghostMap.begin(), ghostMap.end(), gid);
      assert(*ii == gid);
      return std::distance(ghostMap.begin(), ii) + numOwned;
   }

   uint32_t L2H(uint32_t lid) const {
      assert(lid < graph.size());
      if (lid < numOwned)
         return id;
      for (int i = 0; i < hostNodes.size(); ++i)
         if (hostNodes[i].first >= lid && lid < hostNodes[i].second)
            return i;
      abort();
   }

   bool isOwned(uint64_t gid) const {
      return gid >= globalOffset && gid < globalOffset + numOwned;
   }

   template<bool en, typename std::enable_if<en>::type* = nullptr>
   NodeTy& getDataImpl(typename GraphTy::GraphNode N, Galois::MethodFlag mflag = Galois::MethodFlag::WRITE) {
      auto& r = graph.getData(N, mflag);
      return round ? r.first : r.second;
   }

   template<bool en, typename std::enable_if<!en>::type* = nullptr>
   NodeTy& getDataImpl(typename GraphTy::GraphNode N, Galois::MethodFlag mflag = Galois::MethodFlag::WRITE) {
      auto& r = graph.getData(N, mflag);
      return r;
   }

   template<bool en, typename std::enable_if<en>::type* = nullptr>
   const NodeTy& getDataImpl(typename GraphTy::GraphNode N, Galois::MethodFlag mflag = Galois::MethodFlag::WRITE) const {
      auto& r = graph.getData(N, mflag);
      return round ? r.first : r.second;
   }

   template<bool en, typename std::enable_if<!en>::type* = nullptr>
   const NodeTy& getDataImpl(typename GraphTy::GraphNode N, Galois::MethodFlag mflag = Galois::MethodFlag::WRITE) const {
      auto& r = graph.getData(N, mflag);
      return r;
   }
   template<bool en, typename std::enable_if<en>::type* = nullptr>
   typename GraphTy::edge_data_reference getEdgeDataImpl(typename GraphTy::edge_iterator ni, Galois::MethodFlag mflag = Galois::MethodFlag::WRITE) {
      auto& r = graph.getEdgeData(ni, mflag);
      return round ? r.first : r.second;
   }

   template<bool en, typename std::enable_if<!en>::type* = nullptr>
   typename GraphTy::edge_data_reference getEdgeDataImpl(typename GraphTy::edge_iterator ni, Galois::MethodFlag mflag = Galois::MethodFlag::WRITE) {
      auto& r = graph.getEdgeData(ni, mflag);
      return r;
   }

public:
   GraphTy & getGraph() {
      return graph;
   }
  static void syncRecv(uint32_t src, Galois::Runtime::RecvBuffer& buf) {
      uint32_t oid;
      void (hGraph::*fn)(Galois::Runtime::RecvBuffer&);
      Galois::Runtime::gDeserialize(buf, oid, fn);
      hGraph* obj = reinterpret_cast<hGraph*>(ptrForObj(oid));
      (obj->*fn)(buf);
      //--(obj->num_recv_expected);
      //std::cout << "[ " << Galois::Runtime::getSystemNetworkInterface().ID << "] " << " NUM RECV EXPECTED : " << (obj->num_recv_expected) << "\n";
   }

   void exchange_info_landingPad(Galois::Runtime::RecvBuffer& buf){
     uint32_t hostID;
     uint64_t numItems;
     std::vector<uint64_t> items;
     Galois::Runtime::gDeserialize(buf, hostID, numItems);

     Galois::Runtime::gDeserialize(buf, masterNodes[hostID]);
     std::cout << "from : " << hostID << " -> " << numItems << " --> " << masterNodes[hostID].size() << "\n";
   }



   template<typename FnTy>
     void syncRecvApply(uint32_t from_id, Galois::Runtime::RecvBuffer& buf, uint32_t num, std::string loopName) {
       auto& net = Galois::Runtime::getSystemNetworkInterface();
       std::string set_timer_str("SYNC_SET_" + loopName + "_" + std::to_string(num_run));
       std::string doall_str("LAMBDA::SYNC_PUSH_RECV_APPLY_" + loopName + "_" + std::to_string(num_run));
       Galois::StatTimer StatTimer_set(set_timer_str.c_str());
       StatTimer_set.start();

       assert(num == masterNodes[from_id].size());
       if(num > 0){
         std::vector<typename FnTy::ValTy> val_vec(num);
         Galois::Runtime::gDeserialize(buf, val_vec);
         if (!FnTy::reduce_batch(from_id, &val_vec[0])) {
           Galois::do_all(boost::counting_iterator<uint32_t>(0), boost::counting_iterator<uint32_t>(num),
               [&](uint32_t n){
               uint32_t lid = masterNodes[from_id][n];
#ifdef __GALOIS_HET_OPENCL__
           CLNodeDataWrapper d = clGraph.getDataW(lid);
           FnTy::reduce(lid, d, val_vec[n]);
#else
           FnTy::reduce(lid, getData(lid), val_vec[n]);
#endif
               }, Galois::loopname(doall_str.c_str()));
         }
       }
       StatTimer_set.stop();
   }

   template<typename FnTy>
   void syncPullRecvReply(Galois::Runtime::RecvBuffer& buf) {
      void (hGraph::*fn)(Galois::Runtime::RecvBuffer&) = &hGraph::syncPullRecvApply<FnTy>;
      auto& net = Galois::Runtime::getSystemNetworkInterface();
      uint32_t num;
      unsigned from_id;
      std::string loopName;
      uint32_t num_iter_pull;
      Galois::Runtime::gDeserialize(buf, loopName, num_iter_pull, from_id, num);
      std::string extract_timer_str("SYNC_EXTRACT_" + loopName + "_" + std::to_string(num_run) + "_" + std::to_string(num_iter_pull));
      Galois::StatTimer StatTimer_extract(extract_timer_str.c_str());
      std::string statSendBytes_str("SEND_BYTES_SYNC_PULL_REPLY_" + loopName + "_" + std::to_string(num_run) + "_" + std::to_string(num_iter_pull));
      Galois::Statistic SyncPullReply_send_bytes(statSendBytes_str);
      std::string doall_str("LAMBDA::SYNC_PULL_RECV_REPLY_" + loopName + "_" + std::to_string(num_run) + "_" + std::to_string(num_iter_pull));
      Galois::Runtime::SendBuffer b;
      gSerialize(b, idForSelf(), fn, loopName, num_iter_pull, net.ID, num);

      assert(num == masterNodes[from_id].size());
      StatTimer_extract.start();
      if(num > 0){
        std::vector<typename FnTy::ValTy> val_vec(num);

        if (!FnTy::extract_batch(from_id, &val_vec[0])) {
          Galois::do_all(boost::counting_iterator<uint32_t>(0), boost::counting_iterator<uint32_t>(num), [&](uint32_t n){
              uint32_t localID = masterNodes[from_id][n];
#ifdef __GALOIS_HET_OPENCL__
              auto val = FnTy::extract((localID), clGraph.getDataR((localID)));
#else
              auto val = FnTy::extract((localID), getData(localID));
#endif
              assert(n < num);
              val_vec[n] = val;

              }, Galois::loopname(doall_str.c_str()));
        }

        Galois::Runtime::gSerialize(b, val_vec);
      }
      StatTimer_extract.stop();

      SyncPullReply_send_bytes += b.size();
      net.sendMsg(from_id, syncRecv, b);
   }

   template<typename FnTy>
   void syncPullRecvApply(uint32_t from_id, Galois::Runtime::RecvBuffer& buf, uint32_t num, std::string loopName) {
      std::string set_timer_str("SYNC_SET_" + loopName + "_" + std::to_string(num_run));
      Galois::StatTimer StatTimer_set(set_timer_str.c_str());
      std::string doall_str("LAMBDA::SYNC_PULL_RECV_APPLY_" + loopName + "_" + std::to_string(num_run));
      auto& net = Galois::Runtime::getSystemNetworkInterface();

      assert(num == slaveNodes[from_id].size());

      StatTimer_set.start();

      if(num > 0 ){
        std::vector<typename FnTy::ValTy> val_vec(num);

        Galois::Runtime::gDeserialize(buf, val_vec);

        if (!FnTy::setVal_batch(from_id, &val_vec[0])) {
          Galois::do_all(boost::counting_iterator<uint32_t>(0), boost::counting_iterator<uint32_t>(num), [&](uint32_t n){
              uint32_t localID = slaveNodes[from_id][n];
#ifdef __GALOIS_HET_OPENCL__
              {
              CLNodeDataWrapper d = clGraph.getDataW(localID);
              FnTy::setVal(localID, d, val_vec[n]);
              }
#else
              FnTy::setVal(localID, getData(localID), val_vec[n]);
#endif
              }, Galois::loopname(doall_str.c_str()));
         }
      }
      StatTimer_set.stop();
   }

public:
   typedef typename GraphTy::GraphNode GraphNode;
   typedef typename GraphTy::iterator iterator;
   typedef typename GraphTy::const_iterator const_iterator;
   typedef typename GraphTy::local_iterator local_iterator;
   typedef typename GraphTy::const_local_iterator const_local_iterator;
   typedef typename GraphTy::edge_iterator edge_iterator;

   //hGraph construction is collective
   hGraph(const std::string& filename, unsigned host, unsigned numHosts, std::vector<unsigned> scalefactor = std::vector<unsigned>()) :
         GlobalObject(this), id(host), round(false),statGhostNodes("TotalGhostNodes") {
      OfflineGraph g(filename);
      //std::cerr << "Offline Graph Done\n";

      masterNodes.resize(numHosts);
      slaveNodes.resize(numHosts);
      num_recv_expected = 0;
      num_iter_push = 0;
      num_iter_pull = 0;
      num_run = 0;
      totalNodes = g.size();
      std::cerr << "Total nodes : " << totalNodes << "\n";
      std::cerr << "Total edges : " << g.sizeEdges() << "\n";
      //compute owners for all nodes
      if (scalefactor.empty() || (numHosts == 1)) {
         for (unsigned i = 0; i < numHosts; ++i)
            gid2host.push_back(Galois::block_range(0U, (unsigned) g.size(), i, numHosts));
      } else {
         assert(scalefactor.size() == numHosts);
         unsigned numBlocks = 0;
         for (unsigned i = 0; i < numHosts; ++i)
            numBlocks += scalefactor[i];
         std::vector<std::pair<uint64_t, uint64_t>> blocks;
         for (unsigned i = 0; i < numBlocks; ++i)
            blocks.push_back(Galois::block_range(0U, (unsigned) g.size(), i, numBlocks));
         std::vector<unsigned> prefixSums;
         prefixSums.push_back(0);
         for (unsigned i = 1; i < numHosts; ++i)
            prefixSums.push_back(prefixSums[i - 1] + scalefactor[i - 1]);
         for (unsigned i = 0; i < numHosts; ++i) {
            unsigned firstBlock = prefixSums[i];
            unsigned lastBlock = prefixSums[i] + scalefactor[i] - 1;
            gid2host.push_back(std::make_pair(blocks[firstBlock].first, blocks[lastBlock].second));
         }
      }

      numOwned = gid2host[id].second - gid2host[id].first;
      globalOffset = gid2host[id].first;
      std::cerr << "[" << id << "] Owned nodes: " << numOwned << "\n";

      uint64_t numEdges = g.edge_begin(gid2host[id].second) - g.edge_begin(gid2host[id].first); // depends on Offline graph impl
      std::cerr << "[" << id << "] Edge count Done " << numEdges << "\n";

      std::vector<bool> ghosts(g.size());
#if 0
      for (auto n = gid2host[id].first; n < gid2host[id].second; ++n){
         for (auto ii = g.edge_begin(n), ee = g.edge_end(n); ii < ee; ++ii){
            ghosts[g.getEdgeDst(ii)] = true;
         }
      }
#endif
      auto ee = g.edge_begin(gid2host[id].first);
      for (auto n = gid2host[id].first; n < gid2host[id].second; ++n) {
         auto ii = ee;
         ee = g.edge_end(n);
         for (; ii < ee; ++ii) {
            ghosts[g.getEdgeDst(ii)] = true;
         }
      }
      std::cerr << "[" << id << "] Ghost Finding Done " << std::count(ghosts.begin(), ghosts.end(), true) << "\n";

      for (uint64_t x = 0; x < g.size(); ++x)
         if (ghosts[x] && !isOwned(x))
            ghostMap.push_back(x);
      std::cerr << "[" << id << "] Ghost nodes: " << ghostMap.size() << "\n";

      hostNodes.resize(numHosts, std::make_pair(~0, ~0));
      for (unsigned ln = 0; ln < ghostMap.size(); ++ln) {
         unsigned lid = ln + numOwned;
         auto gid = ghostMap[ln];
         bool found = false;
         for (auto h = 0; h < gid2host.size(); ++h) {
            auto& p = gid2host[h];
            if (gid >= p.first && gid < p.second) {
               hostNodes[h].first = std::min(hostNodes[h].first, lid);
               hostNodes[h].second = lid + 1;
               found = true;
               break;
            }
         }
         assert(found);
      }

      for(unsigned h = 0; h < hostNodes.size(); ++h){
        std::string temp_str = ("GhostNodes_from_" + std::to_string(h));
        Galois::Statistic temp_stat_ghosNode(temp_str);
        uint32_t start, end;
        std::tie(start, end) = nodes_by_host(h);
        temp_stat_ghosNode += (end - start);
        statGhostNodes += (end - start);
      }
      //std::cerr << "hostNodes Done\n";

      uint32_t numNodes = numOwned + ghostMap.size();
      assert((uint64_t )numOwned + (uint64_t )ghostMap.size() == (uint64_t )numNodes);
      graph.allocateFrom(numNodes, numEdges);
      //std::cerr << "Allocate done\n";

      graph.constructNodes();
      //std::cerr << "Construct nodes done\n";
      loadEdges<std::is_void<EdgeTy>::value>(g);
#ifdef __GALOIS_HET_OPENCL__
      clGraph.load_from_hgraph(*this);
#endif
      
      setup_communication();

#ifdef __GALOIS_SIMULATE_COMMUNICATION__
#ifndef __GALOIS_SIMULATE_COMMUNICATION_WITH_GRAPH_DATA__
      simulate_communication();
#endif
#endif
   }

   void setup_communication() {
      Galois::StatTimer StatTimer_comm_setup("COMMUNICATION_SETUP_TIME");
      Galois::Runtime::getHostBarrier().wait();
      StatTimer_comm_setup.start();

      for(uint32_t h = 0; h < hostNodes.size(); ++h){
        uint32_t start, end;
        std::tie(start, end) = nodes_by_host(h);
        for(; start != end; ++start){
          slaveNodes[h].push_back(L2G(start));
        }
      }

      //Exchange information for memoization optimization.
      exchange_info_init();

      for(uint32_t h = 0; h < masterNodes.size(); ++h){
         Galois::do_all(boost::counting_iterator<uint32_t>(0), boost::counting_iterator<uint32_t>(masterNodes[h].size()),
             [&](uint32_t n){
             masterNodes[h][n] = G2L(masterNodes[h][n]);
             }, Galois::loopname("MASTER_NODES"));
      }

      for(uint32_t h = 0; h < slaveNodes.size(); ++h){
         Galois::do_all(boost::counting_iterator<uint32_t>(0), boost::counting_iterator<uint32_t>(slaveNodes[h].size()),
             [&](uint32_t n){
             slaveNodes[h][n] = G2L(slaveNodes[h][n]);
             }, Galois::loopname("SLAVE_NODES"));
      }

      for(auto x = 0; x < masterNodes.size(); ++x){
        std::string master_nodes_str = "MASTER_NODES_TO_" + std::to_string(x);
        Galois::Statistic StatMasterNodes(master_nodes_str);
        StatMasterNodes += masterNodes[x].size();
      }

      for(auto x = 0; x < slaveNodes.size(); ++x){
        std::string slave_nodes_str = "SLAVE_NODES_FROM_" + std::to_string(x);
        Galois::Statistic StatSlaveNodes(slave_nodes_str);
        StatSlaveNodes += slaveNodes[x].size();
      }

      StatTimer_comm_setup.stop();
   }

#ifdef __GALOIS_SIMULATE_COMMUNICATION__
#ifndef __GALOIS_SIMULATE_COMMUNICATION_WITH_GRAPH_DATA__
   void simulate_communication() {
     for (int i = 0; i < 10; ++i) {
     simulate_sync_pull();
     simulate_sync_push();

#ifdef __GALOIS_SIMULATE_BARE_MPI_COMMUNICATION__
     simulate_bare_mpi_sync_pull();
     simulate_bare_mpi_sync_push();
#endif
     }
   }
#endif
#endif

   template<bool isVoidType, typename std::enable_if<!isVoidType>::type* = nullptr>
   void loadEdges(OfflineGraph & g) {
      fprintf(stderr, "Loading edge-data while creating edges.\n");

      uint64_t cur = 0;
      Galois::Timer timer;
      std::cout <<"["<<id<<"]PRE :: NumSeeks ";
      g.num_seeks();
      g.reset_seek_counters();
      timer.start();
#if 1
      auto ee = g.edge_begin(gid2host[id].first);
      for (auto n = gid2host[id].first; n < gid2host[id].second; ++n) {
         auto ii = ee;
         ee=g.edge_end(n);
         for (; ii < ee; ++ii) {
            auto gdst = g.getEdgeDst(ii);
            decltype(gdst) ldst = G2L(gdst);
            auto gdata = g.getEdgeData<EdgeTy>(ii);
            graph.constructEdge(cur++, ldst, gdata);
         }
         graph.fixEndEdge(G2L(n), cur);
      }
      //RK - This code should be slightly faster than the conventional single-phase
      // code to load the edges since the file pointer is not moved between the
      // destination and the data on each edge.
      // NEEDS TO BE FASTER!
#if 0
      for (auto n = g.begin(); n != g.end(); ++n) {
         if (this->isOwned(*n)) {
            auto ii = g.edge_begin(*n), ee = g.edge_end(*n);
            for (; ii < ee; ++ii) {
               auto gdst = g.getEdgeDst(ii);
               decltype(gdst) ldst = G2L(gdst);
               graph.constructEdge(cur++, ldst);
            }
            graph.fixEndEdge(G2L(*n), cur);
         }
      }
      //Now load the edge data.
      cur=0;
      if(false)for (auto n = g.begin(); n != g.end(); ++n) {
         if (this->isOwned(*n)) {
            auto ii = g.edge_begin(*n), ee = g.edge_end(*n);
            for (; ii < ee; ++ii) {
               auto gdata = g.getEdgeData<EdgeTy>(ii);
               graph.getEdgeData(cur++)=gdata;
            }
         }
      }
#endif
#else
      //Old code - single loop for edge destination and edge Data.
      for (auto n = gid2host[id].first; n < gid2host[id].second; ++n) {
         for (auto ii = g.edge_begin(n), ee = g.edge_end(n); ii < ee; ++ii) {
            auto gdst = g.getEdgeDst(ii);
            decltype(gdst) ldst = G2L(gdst);
            auto gdata = g.getEdgeData<EdgeTy>(ii);
            graph.constructEdge(cur++, ldst, gdata);
         }
         graph.fixEndEdge(G2L(n), cur);
      }
#endif
      timer.stop();
      std::cout <<"["<<id<<"]POST :: NumSeeks ";
      g.num_seeks();
      std::cout << "EdgeLoading time " << timer.get_usec()/1000000.0f << " seconds\n";
   }
   template<bool isVoidType, typename std::enable_if<isVoidType>::type* = nullptr>
   void loadEdges(OfflineGraph & g) {
      fprintf(stderr, "Loading void edge-data while creating edges.\n");
      uint64_t cur = 0;
      for (auto n = gid2host[id].first; n < gid2host[id].second; ++n) {
         for (auto ii = g.edge_begin(n), ee = g.edge_end(n); ii < ee; ++ii) {
            auto gdst = g.getEdgeDst(ii);
            decltype(gdst) ldst = G2L(gdst);
            graph.constructEdge(cur++, ldst);
         }
         graph.fixEndEdge(G2L(n), cur);
      }

   }

   NodeTy& getData(GraphNode N, Galois::MethodFlag mflag = Galois::MethodFlag::WRITE) {
      auto& r = getDataImpl<BSPNode>(N, mflag);
//    auto i =Galois::Runtime::NetworkInterface::ID;
      //std::cerr << i << " " << N << " " <<&r << " " << r.dist_current << "\n";
      return r;
   }

   const NodeTy& getData(GraphNode N, Galois::MethodFlag mflag = Galois::MethodFlag::WRITE) const {
      auto& r = getDataImpl<BSPNode>(N, mflag);
//    auto i =Galois::Runtime::NetworkInterface::ID;
      //std::cerr << i << " " << N << " " <<&r << " " << r.dist_current << "\n";
      return r;
   }
   typename GraphTy::edge_data_reference getEdgeData(edge_iterator ni, Galois::MethodFlag mflag = Galois::MethodFlag::WRITE) {
      return getEdgeDataImpl<BSPEdge>(ni, mflag);
   }

   GraphNode getEdgeDst(edge_iterator ni) {
      return graph.getEdgeDst(ni);
   }

   edge_iterator edge_begin(GraphNode N) {
      return graph.edge_begin(N);
   }

   edge_iterator edge_end(GraphNode N) {
      return graph.edge_end(N);
   }

   size_t size() const {
      return graph.size();
   }
   size_t sizeEdges() const {
      return graph.sizeEdges();
   }

   const_iterator begin() const {
      return graph.begin();
   }
   iterator begin() {
      return graph.begin();
   }
   const_iterator end() const {
      return graph.begin() + numOwned;
   }
   iterator end() {
      return graph.begin() + numOwned;
   }

   const_iterator ghost_begin() const {
      return end();
   }
   iterator ghost_begin() {
      return end();
   }
   const_iterator ghost_end() const {
      return graph.end();
   }
   iterator ghost_end() {
      return graph.end();
   }

  void exchange_info_init(){
    auto& net = Galois::Runtime::getSystemNetworkInterface();
    //may be reusing tag, so need a barrier
    Galois::Runtime::getHostBarrier().wait();

    for (unsigned x = 0; x < net.Num; ++x) {
      if((x == id))
        continue;

      Galois::Runtime::SendBuffer b;
      gSerialize(b, (uint64_t)slaveNodes[x].size(), slaveNodes[x]);
      net.sendTagged(x, 1, b);
      std::cout << " number of slaves from : " << x << " : " << slaveNodes[x].size() << "\n";
   }

    //receive
    for (unsigned x = 0; x < net.Num; ++x) {
      if((x == id))
        continue;

      decltype(net.recieveTagged(1, nullptr)) p;
      do {
        net.handleReceives();
        p = net.recieveTagged(1, nullptr);
      } while(!p);

      uint64_t numItems;
      Galois::Runtime::gDeserialize(p->second, numItems);
      Galois::Runtime::gDeserialize(p->second, masterNodes[p->first]);
      std::cout << "from : " << p->first << " -> " << numItems << " --> " << masterNodes[p->first].size() << "\n";
    }

    //may be reusing tag, so need a barrier
    Galois::Runtime::getHostBarrier().wait();
   }

#ifdef __GALOIS_SIMULATE_COMMUNICATION__
#ifdef __GALOIS_SIMULATE_BARE_MPI_COMMUNICATION__
#ifdef __GALOIS_SIMULATE_COMMUNICATION_WITH_GRAPH_DATA__
   template<typename FnTy>
#endif
   void simulate_bare_mpi_sync_pull(bool mem_copy = false) {
      std::cerr << "WARNING: requires MPI_THREAD_MULTIPLE to be set in MPI_Init_thread() and Net to not receive MPI messages with tag 32767\n";
      Galois::StatTimer StatTimer_syncPull("SIMULATE_MPI_SYNC_PULL");
      Galois::Statistic SyncPull_send_bytes("SIMULATE_MPI_SYNC_PULL_SEND_BYTES");

#ifndef __GALOIS_SIMULATE_COMMUNICATION_WITH_GRAPH_DATA__
      MPI_Barrier(MPI_COMM_WORLD);
#endif
      StatTimer_syncPull.start();
      auto& net = Galois::Runtime::getSystemNetworkInterface();

      std::vector<MPI_Request> requests(2 * net.Num);
      unsigned num_requests = 0;

#ifdef __GALOIS_SIMULATE_COMMUNICATION_WITH_GRAPH_DATA__
      std::vector<typename FnTy::ValTy> sb[net.Num];
#else
      std::vector<uint64_t> sb[net.Num];
#endif
      std::vector<uint8_t> bs[net.Num];
      for (unsigned x = 0; x < net.Num; ++x) {
         uint32_t num = masterNodes[x].size();
         if((x == id) || (num == 0))
           continue;
         sb[x].resize(num);

#ifdef __GALOIS_SIMULATE_COMMUNICATION_WITH_GRAPH_DATA__
         size_t size = num * sizeof(typename FnTy::ValTy);
         std::vector<typename FnTy::ValTy> &val_vec = sb[x];
#else
         size_t size = num * sizeof(uint64_t);
         std::vector<uint64_t> &val_vec = sb[x];
#endif

#ifdef __GALOIS_SIMULATE_COMMUNICATION_WITH_GRAPH_DATA__
         if (!FnTy::extract_batch(x, &val_vec[0])) {
           Galois::do_all(boost::counting_iterator<uint32_t>(0), boost::counting_iterator<uint32_t>(num), [&](uint32_t n){
               uint32_t localID = masterNodes[x][n];
#ifdef __GALOIS_HET_OPENCL__
               auto val = FnTy::extract((localID), clGraph.getDataR((localID)));
#else
               auto val = FnTy::extract((localID), getData(localID));
#endif
               val_vec[n] = val;

               }, Galois::loopname("SYNC_PULL_EXTRACT"));
         }
#else
         val_vec[0] = 1;
#endif
         
         if (mem_copy) {
           bs[x].resize(size);
           memcpy(bs[x].data(), sb[x].data(), size);
         }

         SyncPull_send_bytes += size;
         //std::cerr << "[" << id << "]" << " mpi send to " << x << " : " << size << "\n";
         if (mem_copy)
           MPI_Isend((uint8_t *)bs[x].data(), size, MPI_BYTE, x, 32767, MPI_COMM_WORLD, &requests[num_requests++]);
         else
           MPI_Isend((uint8_t *)sb[x].data(), size, MPI_BYTE, x, 32767, MPI_COMM_WORLD, &requests[num_requests++]);
      }

#ifdef __GALOIS_SIMULATE_COMMUNICATION_WITH_GRAPH_DATA__
      std::vector<typename FnTy::ValTy> rb[net.Num];
#else
      std::vector<uint64_t> rb[net.Num];
#endif
      std::vector<uint8_t> b[net.Num];
      for (unsigned x = 0; x < net.Num; ++x) {
         uint32_t num = slaveNodes[x].size();
         if((x == id) || (num == 0))
           continue;
#ifdef __GALOIS_SIMULATE_COMMUNICATION_WITH_GRAPH_DATA__
         size_t size = num * sizeof(typename FnTy::ValTy);
#else
         size_t size = num * sizeof(uint64_t);
#endif
         rb[x].resize(num);
         if (mem_copy) b[x].resize(size);

         //std::cerr << "[" << id << "]" << " mpi receive from " << x << " : " << size << "\n";
         if (mem_copy)
           MPI_Irecv((uint8_t *)b[x].data(), size, MPI_BYTE, x, 32767, MPI_COMM_WORLD, &requests[num_requests++]);
         else
           MPI_Irecv((uint8_t *)rb[x].data(), size, MPI_BYTE, x, 32767, MPI_COMM_WORLD, &requests[num_requests++]);
      }

      MPI_Waitall(num_requests, &requests[0], MPI_STATUSES_IGNORE);

      for (unsigned x = 0; x < net.Num; ++x) {
         uint32_t num = slaveNodes[x].size();
         if((x == id) || (num == 0))
           continue;
         //std::cerr << "[" << id << "]" << " mpi received from " << x << "\n";
         if (mem_copy) memcpy(rb[x].data(), b[x].data(), b[x].size());
#ifdef __GALOIS_SIMULATE_COMMUNICATION_WITH_GRAPH_DATA__
         std::vector<typename FnTy::ValTy> &val_vec = rb[x];
#else
         std::vector<uint64_t> &val_vec = rb[x];
#endif
#ifdef __GALOIS_SIMULATE_COMMUNICATION_WITH_GRAPH_DATA__
         if (!FnTy::setVal_batch(x, &val_vec[0])) {
           Galois::do_all(boost::counting_iterator<uint32_t>(0), boost::counting_iterator<uint32_t>(num), [&](uint32_t n){
               uint32_t localID = slaveNodes[x][n];
#ifdef __GALOIS_HET_OPENCL__
               {
               CLNodeDataWrapper d = clGraph.getDataW(localID);
               FnTy::setVal(localID, d, val_vec[n]);
               }
#else
               FnTy::setVal(localID, getData(localID), val_vec[n]);
#endif
               }, Galois::loopname("SYNC_PULL_SET"));
          }
#endif
      }

      //std::cerr << "[" << id << "]" << "pull mpi done\n";
      StatTimer_syncPull.stop();
   }

#ifdef __GALOIS_SIMULATE_COMMUNICATION_WITH_GRAPH_DATA__
   template<typename FnTy>
#endif
   void simulate_bare_mpi_sync_push(bool mem_copy = false) {
      std::cerr << "WARNING: requires MPI_THREAD_MULTIPLE to be set in MPI_Init_thread() and Net to not receive MPI messages with tag 32767\n";
      Galois::StatTimer StatTimer_syncPush("SIMULATE_MPI_SYNC_PUSH");
      Galois::Statistic SyncPush_send_bytes("SIMULATE_MPI_SYNC_PUSH_SEND_BYTES");

#ifndef __GALOIS_SIMULATE_COMMUNICATION_WITH_GRAPH_DATA__
      MPI_Barrier(MPI_COMM_WORLD);
#endif
      StatTimer_syncPush.start();
      auto& net = Galois::Runtime::getSystemNetworkInterface();

      std::vector<MPI_Request> requests(2 * net.Num);
      unsigned num_requests = 0;

#ifdef __GALOIS_SIMULATE_COMMUNICATION_WITH_GRAPH_DATA__
      std::vector<typename FnTy::ValTy> sb[net.Num];
#else
      std::vector<uint64_t> sb[net.Num];
#endif
      std::vector<uint8_t> bs[net.Num];
      for (unsigned x = 0; x < net.Num; ++x) {
         uint32_t num = slaveNodes[x].size();
         if((x == id) || (num == 0))
           continue;
         sb[x].resize(num);

#ifdef __GALOIS_SIMULATE_COMMUNICATION_WITH_GRAPH_DATA__
         size_t size = num * sizeof(typename FnTy::ValTy);
         std::vector<typename FnTy::ValTy> &val_vec = sb[x];
#else
         size_t size = num * sizeof(uint64_t);
         std::vector<uint64_t> &val_vec = sb[x];
#endif

#ifdef __GALOIS_SIMULATE_COMMUNICATION_WITH_GRAPH_DATA__
         if (!FnTy::extract_reset_batch(x, &val_vec[0])) {
           Galois::do_all(boost::counting_iterator<uint32_t>(0), boost::counting_iterator<uint32_t>(num), [&](uint32_t n){
                uint32_t lid = slaveNodes[x][n];
#ifdef __GALOIS_HET_OPENCL__
                CLNodeDataWrapper d = clGraph.getDataW(lid);
                auto val = FnTy::extract(lid, getData(lid, d));
                FnTy::reset(lid, d);
#else
                auto val = FnTy::extract(lid, getData(lid));
                FnTy::reset(lid, getData(lid));
#endif
                val_vec[n] = val;
               }, Galois::loopname("SYNC_PUSH_EXTRACT"));
         }
#else
         val_vec[0] = 1;
#endif
         
         if (mem_copy) {
           bs[x].resize(size);
           memcpy(bs[x].data(), sb[x].data(), size);
         }

         SyncPush_send_bytes += size;
         //std::cerr << "[" << id << "]" << " mpi send to " << x << " : " << size << "\n";
         if (mem_copy)
           MPI_Isend((uint8_t *)bs[x].data(), size, MPI_BYTE, x, 32767, MPI_COMM_WORLD, &requests[num_requests++]);
         else
           MPI_Isend((uint8_t *)sb[x].data(), size, MPI_BYTE, x, 32767, MPI_COMM_WORLD, &requests[num_requests++]);
      }

#ifdef __GALOIS_SIMULATE_COMMUNICATION_WITH_GRAPH_DATA__
      std::vector<typename FnTy::ValTy> rb[net.Num];
#else
      std::vector<uint64_t> rb[net.Num];
#endif
      std::vector<uint8_t> b[net.Num];
      for (unsigned x = 0; x < net.Num; ++x) {
         uint32_t num = masterNodes[x].size();
         if((x == id) || (num == 0))
           continue;
#ifdef __GALOIS_SIMULATE_COMMUNICATION_WITH_GRAPH_DATA__
         size_t size = num * sizeof(typename FnTy::ValTy);
#else
         size_t size = num * sizeof(uint64_t);
#endif
         rb[x].resize(num);
         if (mem_copy) b[x].resize(size);

         //std::cerr << "[" << id << "]" << " mpi receive from " << x << " : " << size << "\n";
         if (mem_copy)
           MPI_Irecv((uint8_t *)b[x].data(), size, MPI_BYTE, x, 32767, MPI_COMM_WORLD, &requests[num_requests++]);
         else
           MPI_Irecv((uint8_t *)rb[x].data(), size, MPI_BYTE, x, 32767, MPI_COMM_WORLD, &requests[num_requests++]);
      }

      MPI_Waitall(num_requests, &requests[0], MPI_STATUSES_IGNORE);

      for (unsigned x = 0; x < net.Num; ++x) {
         uint32_t num = masterNodes[x].size();
         if((x == id) || (num == 0))
           continue;
         //std::cerr << "[" << id << "]" << " mpi received from " << x << "\n";
         if (mem_copy) memcpy(rb[x].data(), b[x].data(), b[x].size());
#ifdef __GALOIS_SIMULATE_COMMUNICATION_WITH_GRAPH_DATA__
         std::vector<typename FnTy::ValTy> &val_vec = rb[x];
#else
         std::vector<uint64_t> &val_vec = rb[x];
#endif
#ifdef __GALOIS_SIMULATE_COMMUNICATION_WITH_GRAPH_DATA__
         if (!FnTy::reduce_batch(x, &val_vec[0])) {
           Galois::do_all(boost::counting_iterator<uint32_t>(0), boost::counting_iterator<uint32_t>(num),
               [&](uint32_t n){
               uint32_t lid = masterNodes[x][n];
#ifdef __GALOIS_HET_OPENCL__
           CLNodeDataWrapper d = clGraph.getDataW(lid);
           FnTy::reduce(lid, d, val_vec[n]);
#else
           FnTy::reduce(lid, getData(lid), val_vec[n]);
#endif
               }, Galois::loopname("SYNC_PUSH_SET"));
         }
#endif
      }
      
      //std::cerr << "[" << id << "]" << "push mpi done\n";
      StatTimer_syncPush.stop();
   }

#ifdef __GALOIS_SIMULATE_COMMUNICATION_WITH_GRAPH_DATA__
   template<typename FnTy>
#endif
   void simulate_bare_mpi_sync_pull_serialized() {
      std::cerr << "WARNING: requires MPI_THREAD_MULTIPLE to be set in MPI_Init_thread() and Net to not receive MPI messages with tag 32767\n";
      Galois::StatTimer StatTimer_syncPull("SIMULATE_MPI_SYNC_PULL");
      Galois::Statistic SyncPull_send_bytes("SIMULATE_MPI_SYNC_PULL_SEND_BYTES");

#ifndef __GALOIS_SIMULATE_COMMUNICATION_WITH_GRAPH_DATA__
      MPI_Barrier(MPI_COMM_WORLD);
#endif
      StatTimer_syncPull.start();
      auto& net = Galois::Runtime::getSystemNetworkInterface();

      std::vector<MPI_Request> requests(2 * net.Num);
      unsigned num_requests = 0;

      Galois::Runtime::SendBuffer sb[net.Num];
      for (unsigned x = 0; x < net.Num; ++x) {
         uint32_t num = masterNodes[x].size();
         if((x == id) || (num == 0))
           continue;

#ifdef __GALOIS_SIMULATE_COMMUNICATION_WITH_GRAPH_DATA__
         size_t size = num * sizeof(typename FnTy::ValTy);
         std::vector<typename FnTy::ValTy> val_vec(num);
#else
         size_t size = num * sizeof(uint64_t);
         std::vector<uint64_t> val_vec(num);
#endif
         size+=8;

#ifdef __GALOIS_SIMULATE_COMMUNICATION_WITH_GRAPH_DATA__
         if (!FnTy::extract_batch(x, &val_vec[0])) {
           Galois::do_all(boost::counting_iterator<uint32_t>(0), boost::counting_iterator<uint32_t>(num), [&](uint32_t n){
               uint32_t localID = masterNodes[x][n];
#ifdef __GALOIS_HET_OPENCL__
               auto val = FnTy::extract((localID), clGraph.getDataR((localID)));
#else
               auto val = FnTy::extract((localID), getData(localID));
#endif
               val_vec[n] = val;

               }, Galois::loopname("SYNC_PULL_EXTRACT"));
         }
#else
         val_vec[0] = 1;
#endif

         Galois::Runtime::gSerialize(sb[x], val_vec);
         assert(size == sb[x].size());
         
         SyncPull_send_bytes += size;
         //std::cerr << "[" << id << "]" << " mpi send to " << x << " : " << size << "\n";
         MPI_Isend(sb[x].linearData(), size, MPI_BYTE, x, 32767, MPI_COMM_WORLD, &requests[num_requests++]);
      }

      Galois::Runtime::RecvBuffer rb[net.Num];
      for (unsigned x = 0; x < net.Num; ++x) {
         uint32_t num = slaveNodes[x].size();
         if((x == id) || (num == 0))
           continue;
#ifdef __GALOIS_SIMULATE_COMMUNICATION_WITH_GRAPH_DATA__
         size_t size = num * sizeof(typename FnTy::ValTy);
#else
         size_t size = num * sizeof(uint64_t);
#endif
         size+=8;
         rb[x].reset(size);

         //std::cerr << "[" << id << "]" << " mpi receive from " << x << " : " << size << "\n";
         MPI_Irecv((uint8_t *)rb[x].linearData(), size, MPI_BYTE, x, 32767, MPI_COMM_WORLD, &requests[num_requests++]);
      }

      MPI_Waitall(num_requests, &requests[0], MPI_STATUSES_IGNORE);

      for (unsigned x = 0; x < net.Num; ++x) {
         uint32_t num = slaveNodes[x].size();
         if((x == id) || (num == 0))
           continue;
         //std::cerr << "[" << id << "]" << " mpi received from " << x << "\n";
#ifdef __GALOIS_SIMULATE_COMMUNICATION_WITH_GRAPH_DATA__
         std::vector<typename FnTy::ValTy> val_vec(num);
#else
         std::vector<uint64_t> val_vec(num);
#endif
         Galois::Runtime::gDeserialize(rb[x], val_vec);
#ifdef __GALOIS_SIMULATE_COMMUNICATION_WITH_GRAPH_DATA__
         if (!FnTy::setVal_batch(x, &val_vec[0])) {
           Galois::do_all(boost::counting_iterator<uint32_t>(0), boost::counting_iterator<uint32_t>(num), [&](uint32_t n){
               uint32_t localID = slaveNodes[x][n];
#ifdef __GALOIS_HET_OPENCL__
               {
               CLNodeDataWrapper d = clGraph.getDataW(localID);
               FnTy::setVal(localID, d, val_vec[n]);
               }
#else
               FnTy::setVal(localID, getData(localID), val_vec[n]);
#endif
               }, Galois::loopname("SYNC_PULL_SET"));
          }
#endif
      }

      //std::cerr << "[" << id << "]" << "pull mpi done\n";
      StatTimer_syncPull.stop();
   }

#ifdef __GALOIS_SIMULATE_COMMUNICATION_WITH_GRAPH_DATA__
   template<typename FnTy>
#endif
   void simulate_bare_mpi_sync_push_serialized() {
      std::cerr << "WARNING: requires MPI_THREAD_MULTIPLE to be set in MPI_Init_thread() and Net to not receive MPI messages with tag 32767\n";
      Galois::StatTimer StatTimer_syncPush("SIMULATE_MPI_SYNC_PUSH");
      Galois::Statistic SyncPush_send_bytes("SIMULATE_MPI_SYNC_PUSH_SEND_BYTES");

#ifndef __GALOIS_SIMULATE_COMMUNICATION_WITH_GRAPH_DATA__
      MPI_Barrier(MPI_COMM_WORLD);
#endif
      StatTimer_syncPush.start();
      auto& net = Galois::Runtime::getSystemNetworkInterface();

      std::vector<MPI_Request> requests(2 * net.Num);
      unsigned num_requests = 0;

      Galois::Runtime::SendBuffer sb[net.Num];
      for (unsigned x = 0; x < net.Num; ++x) {
         uint32_t num = slaveNodes[x].size();
         if((x == id) || (num == 0))
           continue;

#ifdef __GALOIS_SIMULATE_COMMUNICATION_WITH_GRAPH_DATA__
         size_t size = num * sizeof(typename FnTy::ValTy);
         std::vector<typename FnTy::ValTy> val_vec(num);
#else
         size_t size = num * sizeof(uint64_t);
         std::vector<uint64_t> val_vec(num);
#endif
         size+=8;

#ifdef __GALOIS_SIMULATE_COMMUNICATION_WITH_GRAPH_DATA__
         if (!FnTy::extract_reset_batch(x, &val_vec[0])) {
           Galois::do_all(boost::counting_iterator<uint32_t>(0), boost::counting_iterator<uint32_t>(num), [&](uint32_t n){
                uint32_t lid = slaveNodes[x][n];
#ifdef __GALOIS_HET_OPENCL__
                CLNodeDataWrapper d = clGraph.getDataW(lid);
                auto val = FnTy::extract(lid, getData(lid, d));
                FnTy::reset(lid, d);
#else
                auto val = FnTy::extract(lid, getData(lid));
                FnTy::reset(lid, getData(lid));
#endif
                val_vec[n] = val;
               }, Galois::loopname("SYNC_PUSH_EXTRACT"));
         }
#else
         val_vec[0] = 1;
#endif

         Galois::Runtime::gSerialize(sb[x], val_vec);
         assert(size == sb[x].size());

         SyncPush_send_bytes += size;
         //std::cerr << "[" << id << "]" << " mpi send to " << x << " : " << size << "\n";
         MPI_Isend(sb[x].linearData(), size, MPI_BYTE, x, 32767, MPI_COMM_WORLD, &requests[num_requests++]);
      }

      Galois::Runtime::RecvBuffer rb[net.Num];
      for (unsigned x = 0; x < net.Num; ++x) {
         uint32_t num = masterNodes[x].size();
         if((x == id) || (num == 0))
           continue;
#ifdef __GALOIS_SIMULATE_COMMUNICATION_WITH_GRAPH_DATA__
         size_t size = num * sizeof(typename FnTy::ValTy);
#else
         size_t size = num * sizeof(uint64_t);
#endif
         size+=8;
         rb[x].reset(size);

         //std::cerr << "[" << id << "]" << " mpi receive from " << x << " : " << size << "\n";
         MPI_Irecv((uint8_t *)rb[x].linearData(), size, MPI_BYTE, x, 32767, MPI_COMM_WORLD, &requests[num_requests++]);
      }

      MPI_Waitall(num_requests, &requests[0], MPI_STATUSES_IGNORE);

      for (unsigned x = 0; x < net.Num; ++x) {
         uint32_t num = masterNodes[x].size();
         if((x == id) || (num == 0))
           continue;
         //std::cerr << "[" << id << "]" << " mpi received from " << x << "\n";
#ifdef __GALOIS_SIMULATE_COMMUNICATION_WITH_GRAPH_DATA__
         std::vector<typename FnTy::ValTy> val_vec(num);
#else
         std::vector<uint64_t> val_vec(num);
#endif
         Galois::Runtime::gDeserialize(rb[x], val_vec);
#ifdef __GALOIS_SIMULATE_COMMUNICATION_WITH_GRAPH_DATA__
         if (!FnTy::reduce_batch(x, &val_vec[0])) {
           Galois::do_all(boost::counting_iterator<uint32_t>(0), boost::counting_iterator<uint32_t>(num),
               [&](uint32_t n){
               uint32_t lid = masterNodes[x][n];
#ifdef __GALOIS_HET_OPENCL__
           CLNodeDataWrapper d = clGraph.getDataW(lid);
           FnTy::reduce(lid, d, val_vec[n]);
#else
           FnTy::reduce(lid, getData(lid), val_vec[n]);
#endif
               }, Galois::loopname("SYNC_PUSH_SET"));
         }
#endif
      }
      
      //std::cerr << "[" << id << "]" << "push mpi done\n";
      StatTimer_syncPush.stop();
   }
#endif
#endif

#ifdef __GALOIS_SIMULATE_COMMUNICATION__
#ifdef __GALOIS_SIMULATE_COMMUNICATION_WITH_GRAPH_DATA__
   template<typename FnTy>
#endif
   void syncRecvApplyPull(Galois::Runtime::RecvBuffer& buf) {
     unsigned from_id;
     uint32_t num;
     std::string loopName;
     uint32_t num_iter_push;
     Galois::Runtime::gDeserialize(buf, from_id, num);
#ifdef __GALOIS_SIMULATE_COMMUNICATION_WITH_GRAPH_DATA__
     std::vector<typename FnTy::ValTy> val_vec(num);
#else
     std::vector<uint64_t> val_vec(num);
#endif
     Galois::Runtime::gDeserialize(buf, val_vec);
#ifdef __GALOIS_SIMULATE_COMMUNICATION_WITH_GRAPH_DATA__
     if (!FnTy::setVal_batch(from_id, &val_vec[0])) {
       Galois::do_all(boost::counting_iterator<uint32_t>(0), boost::counting_iterator<uint32_t>(num), [&](uint32_t n){
           uint32_t localID = slaveNodes[from_id][n];
#ifdef __GALOIS_HET_OPENCL__
           {
           CLNodeDataWrapper d = clGraph.getDataW(localID);
           FnTy::setVal(localID, d, val_vec[n]);
           }
#else
           FnTy::setVal(localID, getData(localID), val_vec[n]);
#endif
           }, Galois::loopname("SYNC_PULL_SET"));
      }
#endif
   }

#ifdef __GALOIS_SIMULATE_COMMUNICATION_WITH_GRAPH_DATA__
   template<typename FnTy>
#endif
   void syncRecvApplyPush(Galois::Runtime::RecvBuffer& buf) {
     unsigned from_id;
     uint32_t num;
     std::string loopName;
     uint32_t num_iter_push;
     Galois::Runtime::gDeserialize(buf, from_id, num);
#ifdef __GALOIS_SIMULATE_COMMUNICATION_WITH_GRAPH_DATA__
     std::vector<typename FnTy::ValTy> val_vec(num);
#else
     std::vector<uint64_t> val_vec(num);
#endif
     Galois::Runtime::gDeserialize(buf, val_vec);
#ifdef __GALOIS_SIMULATE_COMMUNICATION_WITH_GRAPH_DATA__
     if (!FnTy::reduce_batch(from_id, &val_vec[0])) {
       Galois::do_all(boost::counting_iterator<uint32_t>(0), boost::counting_iterator<uint32_t>(num),
           [&](uint32_t n){
           uint32_t lid = masterNodes[from_id][n];
#ifdef __GALOIS_HET_OPENCL__
       CLNodeDataWrapper d = clGraph.getDataW(lid);
       FnTy::reduce(lid, d, val_vec[n]);
#else
       FnTy::reduce(lid, getData(lid), val_vec[n]);
#endif
           }, Galois::loopname("SYNC_PUSH_SET"));
     }
#endif
   }

#ifdef __GALOIS_SIMULATE_COMMUNICATION_WITH_GRAPH_DATA__
   template<typename FnTy>
#endif
   void simulate_sync_pull() {
#ifdef __GALOIS_SIMULATE_COMMUNICATION_WITH_GRAPH_DATA__
      void (hGraph::*fn)(Galois::Runtime::RecvBuffer&) = &hGraph::syncRecvApplyPull<FnTy>;
#else
      void (hGraph::*fn)(Galois::Runtime::RecvBuffer&) = &hGraph::syncRecvApplyPull;
#endif
      Galois::StatTimer StatTimer_syncPull("SIMULATE_NET_SYNC_PULL");
      Galois::Statistic SyncPull_send_bytes("SIMULATE_NET_SYNC_PULL_SEND_BYTES");

#ifndef __GALOIS_SIMULATE_COMMUNICATION_WITH_GRAPH_DATA__
      Galois::Runtime::getHostBarrier().wait();
#endif
      StatTimer_syncPull.start();
      auto& net = Galois::Runtime::getSystemNetworkInterface();

      for (unsigned x = 0; x < net.Num; ++x) {
         uint32_t num = masterNodes[x].size();
         if((x == id) || (num == 0))
           continue;

         Galois::Runtime::SendBuffer b;
         gSerialize(b, idForSelf(), fn, net.ID, num);

#ifdef __GALOIS_SIMULATE_COMMUNICATION_WITH_GRAPH_DATA__
         std::vector<typename FnTy::ValTy> val_vec(num);
#else
         std::vector<uint64_t> val_vec(num);
#endif

#ifdef __GALOIS_SIMULATE_COMMUNICATION_WITH_GRAPH_DATA__
         if (!FnTy::extract_batch(x, &val_vec[0])) {
           Galois::do_all(boost::counting_iterator<uint32_t>(0), boost::counting_iterator<uint32_t>(num), [&](uint32_t n){
               uint32_t localID = masterNodes[x][n];
#ifdef __GALOIS_HET_OPENCL__
               auto val = FnTy::extract((localID), clGraph.getDataR((localID)));
#else
               auto val = FnTy::extract((localID), getData(localID));
#endif
               val_vec[n] = val;

               }, Galois::loopname("SYNC_PULL_EXTRACT"));
         }
#else
         val_vec[0] = 1;
#endif

         gSerialize(b, val_vec);

         SyncPull_send_bytes += b.size();
         net.sendMsg(x, syncRecv, b);
      }
      //Will force all messages to be processed before continuing
      net.flush();

      Galois::Runtime::getHostBarrier().wait();
      StatTimer_syncPull.stop();
   }

#ifdef __GALOIS_SIMULATE_COMMUNICATION_WITH_GRAPH_DATA__
   template<typename FnTy>
#endif
   void simulate_sync_push() {
#ifdef __GALOIS_SIMULATE_COMMUNICATION_WITH_GRAPH_DATA__
      void (hGraph::*fn)(Galois::Runtime::RecvBuffer&) = &hGraph::syncRecvApplyPush<FnTy>;
#else
      void (hGraph::*fn)(Galois::Runtime::RecvBuffer&) = &hGraph::syncRecvApplyPush;
#endif
      Galois::StatTimer StatTimer_syncPush("SIMULATE_NET_SYNC_PUSH");
      Galois::Statistic SyncPush_send_bytes("SIMULATE_NET_SYNC_PUSH_SEND_BYTES");

#ifndef __GALOIS_SIMULATE_COMMUNICATION_WITH_GRAPH_DATA__
      Galois::Runtime::getHostBarrier().wait();
#endif
      StatTimer_syncPush.start();
      auto& net = Galois::Runtime::getSystemNetworkInterface();

      for (unsigned x = 0; x < net.Num; ++x) {
         uint32_t num = slaveNodes[x].size();
         if((x == id) || (num == 0))
           continue;

         Galois::Runtime::SendBuffer b;
         gSerialize(b, idForSelf(), fn, net.ID, num);

#ifdef __GALOIS_SIMULATE_COMMUNICATION_WITH_GRAPH_DATA__
         std::vector<typename FnTy::ValTy> val_vec(num);
#else
         std::vector<uint64_t> val_vec(num);
#endif

#ifdef __GALOIS_SIMULATE_COMMUNICATION_WITH_GRAPH_DATA__
         if (!FnTy::extract_reset_batch(x, &val_vec[0])) {
           Galois::do_all(boost::counting_iterator<uint32_t>(0), boost::counting_iterator<uint32_t>(num), [&](uint32_t n){
                uint32_t lid = slaveNodes[x][n];
#ifdef __GALOIS_HET_OPENCL__
                CLNodeDataWrapper d = clGraph.getDataW(lid);
                auto val = FnTy::extract(lid, getData(lid, d));
                FnTy::reset(lid, d);
#else
                auto val = FnTy::extract(lid, getData(lid));
                FnTy::reset(lid, getData(lid));
#endif
                val_vec[n] = val;
               }, Galois::loopname("SYNC_PUSH_EXTRACT"));
         }
#else
         val_vec[0] = 1;
#endif

         gSerialize(b, val_vec);

         SyncPush_send_bytes += b.size();
         net.sendMsg(x, syncRecv, b);
      }
      //Will force all messages to be processed before continuing
      net.flush();

      Galois::Runtime::getHostBarrier().wait();

      StatTimer_syncPush.stop();
   }
#endif


   template<typename FnTy>
   void sync_push(std::string loopName) {
      ++num_iter_push;
      std::string extract_timer_str("SYNC_PUSH_EXTRACT_" + loopName +"_" + std::to_string(num_run));
      std::string timer_str("SYNC_PUSH_" + loopName + "_" + std::to_string(num_run));
      std::string timer_barrier_str("SYNC_PUSH_BARRIER_" + loopName + "_" + std::to_string(num_run));
      std::string statSendBytes_str("SEND_BYTES_SYNC_PUSH_" + loopName + "_" + std::to_string(num_run));
      std::string doall_str("LAMBDA::SYNC_PUSH_" + loopName + "_" + std::to_string(num_run));
      Galois::Statistic SyncPush_send_bytes(statSendBytes_str);
      Galois::StatTimer StatTimer_syncPush(timer_str.c_str());
      Galois::StatTimer StatTimerBarrier_syncPush(timer_barrier_str.c_str());
      Galois::StatTimer StatTimer_extract(extract_timer_str.c_str());

      StatTimer_syncPush.start();
      auto& net = Galois::Runtime::getSystemNetworkInterface();

      for (unsigned x = 0; x < net.Num; ++x) {
         uint32_t num = slaveNodes[x].size();
         if((x == id))
           continue;

         Galois::Runtime::SendBuffer b;

         StatTimer_extract.start();
         if(num > 0 ){
           std::vector<typename FnTy::ValTy> val_vec(num);

           if (!FnTy::extract_reset_batch(x, &val_vec[0])) {
             Galois::do_all(boost::counting_iterator<uint32_t>(0), boost::counting_iterator<uint32_t>(num), [&](uint32_t n){
                  uint32_t lid = slaveNodes[x][n];
#ifdef __GALOIS_HET_OPENCL__
                  CLNodeDataWrapper d = clGraph.getDataW(lid);
                  auto val = FnTy::extract(lid, getData(lid, d));
                  FnTy::reset(lid, d);
#else
                  auto val = FnTy::extract(lid, getData(lid));
                  FnTy::reset(lid, getData(lid));
#endif
                  val_vec[n] = val;
                 }, Galois::loopname(doall_str.c_str()));
           }

           gSerialize(b, val_vec);
         }
         StatTimer_extract.stop();

         SyncPush_send_bytes += b.size();
         net.sendTagged(x, Galois::Runtime::evilPhase, b);
      }
      //Will force all messages to be processed before continuing
      net.flush();

      //receive
      for (unsigned x = 0; x < net.Num; ++x) {
        if ((x == id))
          continue;
        uint32_t num = masterNodes[x].size();
        decltype(net.recieveTagged(Galois::Runtime::evilPhase,nullptr)) p;
        do {
          net.handleReceives();
          p = net.recieveTagged(Galois::Runtime::evilPhase, nullptr);
        } while (!p);
        syncRecvApply<FnTy>(p->first, p->second, num, loopName);
      }
      ++Galois::Runtime::evilPhase;

      StatTimer_syncPush.stop();

   }

   template<typename FnTy>
   void sync_pull(std::string loopName) {
      ++num_iter_pull;
      std::string doall_str("LAMBDA::SYNC_PULL_" + loopName + "_" + std::to_string(num_run));
      std::string timer_str("SYNC_PULL_" + loopName +"_" + std::to_string(num_run));
      std::string timer_barrier_str("SYNC_PULL_BARRIER_" + loopName +"_" + std::to_string(num_run));
      std::string statSendBytes_str("SEND_BYTES_SYNC_PULL_" + loopName +"_" + std::to_string(num_run));
      Galois::Statistic SyncPull_send_bytes(statSendBytes_str);
      Galois::StatTimer StatTimer_syncPull(timer_str.c_str());
      Galois::StatTimer StatTimer_extract("SYNC_PULL_EXTRACT", loopName);
      auto& net = Galois::Runtime::getSystemNetworkInterface();

      StatTimer_syncPull.start();

      for (unsigned x = 0; x < net.Num; ++x) {
        uint32_t num = masterNodes[x].size();
        if((x == id))
          continue;

        Galois::Runtime::SendBuffer b;

        StatTimer_extract.start();
        if(num > 0 ){
          std::vector<typename FnTy::ValTy> val_vec(num);
          if (!FnTy::extract_batch(x, &val_vec[0])) {

            Galois::do_all(boost::counting_iterator<uint32_t>(0), boost::counting_iterator<uint32_t>(num), [&](uint32_t n){
                uint32_t localID = masterNodes[x][n];
                auto val = FnTy::extract((localID), getData(localID));
                val_vec[n] = val;
                }, Galois::loopname(doall_str.c_str()));
          }
          Galois::Runtime::gSerialize(b, val_vec);
        }
        StatTimer_extract.stop();

        SyncPull_send_bytes += b.size();
        net.sendTagged(x, Galois::Runtime::evilPhase, b);

      }


      net.flush();

      //receive
      for (unsigned x = 0; x < net.Num; ++x) {
        if ((x == id))
          continue;
        uint32_t num = slaveNodes[x].size();
        decltype(net.recieveTagged(Galois::Runtime::evilPhase,nullptr)) p;
        do {
          net.handleReceives();
          p = net.recieveTagged(Galois::Runtime::evilPhase, nullptr);
        } while (!p);
        syncPullRecvApply<FnTy>(p->first, p->second, num, loopName);
      }

      ++Galois::Runtime::evilPhase;

      StatTimer_syncPull.stop();
   }

   uint64_t getGID(uint32_t nodeID) const {
      return L2G(nodeID);
   }
   uint32_t getLID(uint64_t nodeID) const {
      return G2L(nodeID);
   }
   unsigned getHostID(uint64_t gid) {
      for (auto i = 0; i < hostNodes.size(); ++i) {
         uint64_t start, end;
         std::tie(start, end) = nodes_by_host_G(i);
         if (gid >= start && gid < end) {
            return i;
         }
      }
      return -1;
   }
   uint32_t getNumOwned() const {
      return numOwned;
   }
   uint64_t getGlobalOffset() const {
      return globalOffset;
   }
#ifdef __GALOIS_HET_CUDA__
   template<bool isVoidType, typename std::enable_if<isVoidType>::type* = nullptr>
   void setMarshalEdge(MarshalGraph &m, size_t index, edge_iterator &e) {
      // do nothing
   }
   template<bool isVoidType, typename std::enable_if<!isVoidType>::type* = nullptr>
   void setMarshalEdge(MarshalGraph &m, size_t index, edge_iterator &e) {
      m.edge_data[index] = getEdgeData(e);
   }
   MarshalGraph getMarshalGraph(unsigned host_id) {
      assert(host_id == id);
      MarshalGraph m;

      m.nnodes = size();
      m.nedges = sizeEdges();
      m.nowned = std::distance(begin(), end());
      assert(m.nowned > 0);
      m.id = host_id;
      m.row_start = (index_type *) calloc(m.nnodes + 1, sizeof(index_type));
      m.edge_dst = (index_type *) calloc(m.nedges, sizeof(index_type));

      // initialize node_data with localID-to-globalID mapping
      m.node_data = (index_type *) calloc(m.nnodes, sizeof(node_data_type));
      for (index_type i = 0; i < m.nnodes; ++i) {
        m.node_data[i] = getGID(i);
      }

      if (std::is_void<EdgeTy>::value) {
         m.edge_data = NULL;
      } else {
         if (!std::is_same<EdgeTy, edge_data_type>::value) {
            fprintf(stderr, "WARNING: Edge data type mismatch between CPU and GPU\n");
         }
         m.edge_data = (edge_data_type *) calloc(m.nedges, sizeof(edge_data_type));
      }

      // pinched from Rashid's LC_LinearArray_Graph.h
      size_t edge_counter = 0, node_counter = 0;
      for (auto n = begin(); n != ghost_end() && *n != m.nnodes; n++, node_counter++) {
         m.row_start[node_counter] = edge_counter;
         if (*n < m.nowned) {
            for (auto e = edge_begin(*n); e != edge_end(*n); e++) {
               if (getEdgeDst(e) < m.nnodes) {
                  setMarshalEdge<std::is_void<EdgeTy>::value>(m, edge_counter, e);
                  m.edge_dst[edge_counter++] = getEdgeDst(e);
               }
            }
         }
      }

      m.row_start[node_counter] = edge_counter;
      m.nedges = edge_counter;

      // copy memoization meta-data
      m.num_master_nodes = (unsigned int *) calloc(hostNodes.size(), sizeof(unsigned int));;
      m.master_nodes = (unsigned int **) calloc(hostNodes.size(), sizeof(unsigned int *));;
      for(uint32_t h = 0; h < hostNodes.size(); ++h){
        m.num_master_nodes[h] = masterNodes[h].size();
        if (masterNodes[h].size() > 0) {
          m.master_nodes[h] = (unsigned int *) calloc(masterNodes[h].size(), sizeof(unsigned int));;
          std::copy(masterNodes[h].begin(), masterNodes[h].end(), m.master_nodes[h]);
        } else {
          m.master_nodes[h] = NULL;
        }
      }
      m.num_slave_nodes = (unsigned int *) calloc(hostNodes.size(), sizeof(unsigned int));;
      m.slave_nodes = (unsigned int **) calloc(hostNodes.size(), sizeof(unsigned int *));;
      for(uint32_t h = 0; h < hostNodes.size(); ++h){
        m.num_slave_nodes[h] = slaveNodes[h].size();
        if (slaveNodes[h].size() > 0) {
          m.slave_nodes[h] = (unsigned int *) calloc(slaveNodes[h].size(), sizeof(unsigned int));;
          std::copy(slaveNodes[h].begin(), slaveNodes[h].end(), m.slave_nodes[h]);
        } else {
          m.slave_nodes[h] = NULL;
        }
      }

      return m;
   }
#endif

#ifdef __GALOIS_HET_OPENCL__
public:
   typedef Galois::OpenCL::Graphs::CL_LC_Graph<NodeTy, EdgeTy> CLGraphType;
   typedef typename CLGraphType::NodeDataWrapper CLNodeDataWrapper;
   typedef typename CLGraphType::NodeIterator CLNodeIterator;
   CLGraphType clGraph;
#endif

#ifdef __GALOIS_HET_OPENCL__
   const cl_mem & device_ptr() {
      return clGraph.device_ptr();
   }
   CLNodeDataWrapper getDataW(GraphNode N, Galois::MethodFlag mflag = Galois::MethodFlag::WRITE) {
      return clGraph.getDataW(N);
   }
   const CLNodeDataWrapper getDataR(GraphNode N,Galois::MethodFlag mflag = Galois::MethodFlag::READ) {
      return clGraph.getDataR(N);
   }

#endif


  /**For resetting num_iter_pull and push.**/
   void reset_num_iter(uint32_t runNum){
      num_iter_pull = 0;
      num_iter_push = 0;
      num_run = runNum;
   }
   /** Report stats to be printed.**/
   void reportStats(){
    statGhostNodes.report();
   }
};
#endif//_GALOIS_DIST_HGRAPH_H
