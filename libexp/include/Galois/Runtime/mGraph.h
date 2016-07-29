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
 * Derived from hGraph. Graph abstraction for vertex cut.
 * @author Rashid Kaleem <rashid.kaleem@gmail.com>
 * @author Gurbinder Gill <gurbinder533@gmail.com>
 *
 */

#include <vector>
#include <set>
#include <algorithm>
#include <unordered_map>
#include <deque>

#include "Galois/gstl.h"
#include "Galois/Graphs/LC_CSR_Graph.h"
#include "Galois/Runtime/Substrate.h"
#include "Galois/Runtime/Network.h"

#include "Galois/Runtime/Serialize.h"

#include "Galois/Runtime/Tracer.h"
#include "Galois/Threads.h"

#include "Galois/Runtime/GlobalObj.h"
#include "Galois/Runtime/OfflineGraph.h"

#include "Galois/FlatMap.h"

#ifdef __GALOIS_HET_CUDA__
#include "Galois/Runtime/Cuda/cuda_mtypes.h"
#endif

#ifdef __GALOIS_SIMULATE_COMMUNICATION__
#ifdef __GALOIS_SIMULATE_BARE_MPI_COMMUNICATION__
#include "mpi.h"
#endif
#endif

#ifndef _GALOIS_DIST_mGraph_H
#define _GALOIS_DIST_mGraph_H

/** Utilities for reading partitioned graphs. **/
struct NodeInfo {
   NodeInfo() :
         local_id(0), global_id(0), owner_id(0) {
   }
   NodeInfo(size_t l, size_t g, size_t o) :
         local_id(l), global_id(g), owner_id(o) {
   }
   size_t local_id;
   size_t global_id;
   size_t owner_id;
};

/** For merging the multiple meta files into one **/
struct MergeInfo {

  //MergeInfo() : merged_localID(0), partition(0), localID(0) { }
  //MergeInfo(uint32_t ml, uint32_t pn, uint32_t l) : merged_localID(ml), partition(pn), localID(l) { } 

  MergeInfo() : partition(0), localID(0), ownerID(0){ }
  MergeInfo(uint32_t pn, uint32_t l, uint16_t o) : partition(pn), localID(l), ownerID(o){ }

  //uint32_t merged_localID;
  uint16_t ownerID;
  uint32_t partition;
  uint32_t localID;
  //uint64_t globalID;
};

struct Comp_owner_deq
{
  bool operator() ( const std::pair<uint64_t, uint32_t>& s, uint64_t i )
  {
    return s.first < i;
  }

  bool operator() ( uint64_t i, const std::pair<uint64_t, uint32_t>& s )
  {
    return i < s.first;
  }

  bool operator() ( const std::pair<uint64_t, uint32_t>& a , const std::pair<uint64_t, uint32_t>& b )
  {
    return a.first < b.first;
  }


};

struct Comp_mergedInfo_deq
{
  bool operator() ( const std::pair<uint64_t, MergeInfo>& s, uint64_t i )
  {
    return s.first < i;
  }

  bool operator() ( uint64_t i, const std::pair<uint64_t, MergeInfo>& s )
  {
    return i < s.first;
  }

  bool operator() ( const std::pair<uint64_t, MergeInfo>& a, const std::pair<uint64_t, MergeInfo>& b)
  {
    return a.first < b.first;
  }
};

std::string getPartitionFileName(const std::string & basename, unsigned hostID, unsigned num_hosts){
   std::string result = basename;
   result+= ".PART.";
   result+=std::to_string(hostID);
   result+= ".OF.";
   result+=std::to_string(num_hosts);
   return result;
}
std::string getMetaFileName(const std::string & basename, unsigned hostID, unsigned num_hosts){
   std::string result = basename;
   result+= ".META.";
   result+=std::to_string(hostID);
   result+= ".OF.";
   result+=std::to_string(num_hosts);
   return result;
}

bool readMetaFile(const std::string& metaFileName, std::vector<NodeInfo>& localToGlobalMap_meta){
  std::ifstream meta_file(metaFileName, std::ifstream::binary);
  if (!meta_file.is_open()) {
    std::cout << "Unable to open file " << metaFileName << "! Exiting!\n";
    return false;
  }
  size_t num_entries;
  meta_file.read(reinterpret_cast<char*>(&num_entries), sizeof(num_entries));
  std::cout << "Partition :: " << " Number of nodes :: " << num_entries << "\n";
  for (size_t i = 0; i < num_entries; ++i) {
    std::pair<size_t, size_t> entry;
    size_t owner;
    meta_file.read(reinterpret_cast<char*>(&entry.first), sizeof(entry.first));
    meta_file.read(reinterpret_cast<char*>(&entry.second), sizeof(entry.second));
    meta_file.read(reinterpret_cast<char*>(&owner), sizeof(owner));
    localToGlobalMap_meta.push_back(NodeInfo(entry.second, entry.first, owner));
  }
  return true;
}


/**********Global vectors for book keeping*********************/
//std::vector<std::vector<uint64_t>> masterNodes(4); // master nodes on different hosts. For sync_pull
//std::map<uint64_t, uint32_t> GIDtoOwnerMap;

/**************************************************************/

template<typename NodeTy, typename EdgeTy, bool BSPNode = false, bool BSPEdge = false>
class mGraph : public GlobalObject {

   typedef typename std::conditional<BSPNode, std::pair<NodeTy, NodeTy>, NodeTy>::type realNodeTy;
   typedef typename std::conditional<BSPEdge, std::pair<EdgeTy, EdgeTy>, EdgeTy>::type realEdgeTy;

   typedef Galois::Graph::LC_CSR_Graph<realNodeTy, realEdgeTy> GraphTy;

  GraphTy graph;
  bool round;
   uint64_t totalNodes; // Total nodes in the complete graph.
   uint32_t numOwned; // [0, numOwned) = global nodes owned, thus [numOwned, numNodes are replicas
   uint64_t globalOffset; // [numOwned, end) + globalOffset = GID
  unsigned id; // my hostid // FIXME: isn't this just Network::ID?
   //ghost cell ID translation
   std::vector<uint64_t> ghostMap; // GID = ghostMap[LID - numOwned]
   std::vector<std::pair<uint32_t, uint32_t> > hostNodes; //LID Node owned by host i
   //pointer for each host
   std::vector<uintptr_t> hostPtrs;

  /*** Vertex Cut ***/
  //std::vector<std::vector<NodeInfo>> localToGlobalMap_meta;
  std::vector<NodeInfo> localToGlobalMap_meta;
  std::vector<std::vector<size_t>> slaveNodes; // slave nodes from different hosts. For sync_push
  std::vector<std::vector<size_t>> masterNodes; // master nodes on different hosts. For sync_pull
  //std::unordered_map<size_t, size_t> LocalToGlobalMap;
  //std::unordered_map<size_t, size_t> GlobalToLocalMap;

  //std::unordered_map<size_t, size_t> GIDtoOwnerMap;

  std::vector<size_t> OwnerVec; //To store the ownerIDs of sorted according to the Global IDs.
  std::vector<size_t> GlobalVec; //Global Id's sorted vector.
  std::vector<size_t> LocalVec; //Local Id's sorted vector.

   //GID to owner
   //std::vector<std::pair<uint64_t, uint64_t>> gid2host;

   uint32_t num_iter_push; //Keep track of number of iterations.
   uint32_t num_iter_pull; //Keep track of number of iterations.
   uint32_t num_run; //Keep track of number of iterations.

   // For merging metaFiles
   //std::map<uint64_t, std::vector<MergeInfo>> mergedInfoMap;
   //Galois::flat_map<uint64_t, std::vector<MergeInfo>> mergedInfoMap;
   std::deque<std::pair<uint64_t, MergeInfo>> mergedInfoDeq;

   std::vector<std::vector<uint64_t>> GlobalVec_new; //Global Id's sorted vector.
   std::vector<uint64_t> GlobalVec_merged; //Global Id's sorted vector.
   std::vector<uint16_t> OwnerVec_merged; //To store the ownerIDs of sorted according to the Global IDs.
   //Galois::flat_map<uint64_t, uint32_t> OwnerID_merged_map;
   std::deque<std::pair<uint64_t, uint32_t>> OwnerID_merged_deq;
   //std::map<uint64_t, uint32_t> OwnerID_merged_map;

#if 0
   //host -> (lid, lid]
   std::pair<uint32_t, uint32_t> nodes_by_host(uint32_t host) const {
      return hostNodes[host];
   }

   std::pair<uint64_t, uint64_t> nodes_by_host_G(uint32_t host) const {
      return gid2host[host];
   }
#endif

  size_t L2G_merged(size_t lid) {
    return GlobalVec_merged[lid];
  }

  uint32_t G2L_merged(uint64_t gid) {
    //we can assume that GID exits and is unique. Index is localID since it is sorted.
    auto iter = std::lower_bound(GlobalVec_merged.begin(), GlobalVec_merged.end(), gid);
    if(*iter != gid)
      std::cerr << "[" <<id <<"] G2L_merged : " << *iter << ", GID : " << gid << "GlobalVec_merged[0] : " << GlobalVec_merged[0] <<"\n";
    assert(*iter == gid);
    if(*iter == gid)
      return (iter - GlobalVec_merged.begin());
    else
      abort();
  }

#if 0
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
#endif

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
 
   template<typename FnTy>
   void syncRecvApply(uint32_t from_id, Galois::Runtime::RecvBuffer& buf) {
     uint32_t num;
     std::string loopName;
     auto& net = Galois::Runtime::getSystemNetworkInterface();
     Galois::Runtime::gDeserialize(buf, loopName, num);
     std::string doall_str("LAMBDA::SYNC_PUSH_RECV_APPLY_" + loopName + "_" + std::to_string(num_run));
     Galois::Runtime::reportLoopInstance(doall_str);
     Galois::StatTimer StatTimer_set("SYNC_SET", loopName, Galois::start_now);

     assert(num == masterNodes[from_id].size());
     if(num > 0){
       std::vector<typename FnTy::ValTy> val_vec(num);
       Galois::Runtime::gDeserialize(buf, val_vec);
       if (!FnTy::reduce_batch(from_id, &val_vec[0])) {
       Galois::do_all(boost::counting_iterator<uint32_t>(0), boost::counting_iterator<uint32_t>(num),
                      [&](uint32_t n){
                        uint32_t lid = masterNodes[from_id][n];
                        FnTy::reduce(lid, getData(lid), val_vec[n]);
                      }, Galois::loopname(doall_str.c_str()));
       }
     }
   }

   template<typename FnTy>
   void syncPullRecvReply(uint32_t from_id, Galois::Runtime::RecvBuffer& buf) {
     auto& net = Galois::Runtime::getSystemNetworkInterface();
     uint32_t num;
     std::string loopName;
     Galois::Runtime::gDeserialize(buf, loopName, num);
     Galois::StatTimer StatTimer_extract("SYNC_EXTRACT", loopName, Galois::start_now);
     Galois::Statistic SyncPullReply_send_bytes("SEND_BYTES_SYNC_PULL_REPLY", loopName);
     std::string doall_str("LAMBDA::SYNC_PULL_RECV_REPLY_" + loopName + "_" + std::to_string(num_run));
     Galois::Runtime::reportLoopInstance(doall_str);
     Galois::Runtime::SendBuffer b;
     assert(num == masterNodes[from_id].size());
     gSerialize(b, loopName, num);

#if 0
     /********** Serial loop: works ****************/
     for(auto n : masterNodes[from_id]){
       typename FnTy::ValTy val;
       auto localID = G2L(n);
       val = FnTy::extract((localID), getData(localID));
       
       Galois::Runtime::gSerialize(b, n, val);
     }
     
     SyncPullReply_send_bytes += b.size();
     net.sendMsg(from_id, syncRecv, b);
#endif
     
     std::vector<typename FnTy::ValTy> val_vec(num);
     if(num >0) {
       //std::cout << "["<< net.ID << "] num : " << num << "\n";
       
       if (!FnTy::extract_batch(from_id, &val_vec[0])) {
         Galois::do_all(boost::counting_iterator<uint32_t>(0), boost::counting_iterator<uint32_t>(num), [&](uint32_t n){
             uint32_t localID = masterNodes[from_id][n];
             //std::cout << "["<< net.ID << "] n : " << n << "\n";
             auto val = FnTy::extract((localID), getData(localID));
             assert(n < num);
             val_vec[n] = val;
             
           }, Galois::loopname(doall_str.c_str()));
       }
       
     }
     Galois::Runtime::gSerialize(b, val_vec);
     StatTimer_extract.stop();
     //std::cout << "[" << net.ID << "] Serialized : sending to other host\n";
     SyncPullReply_send_bytes += b.size();
     net.sendTagged(from_id, Galois::Runtime::evilPhase + 1, b);
     //     net.sendMsg(from_id, syncRecv, b);
   }
  
  template<typename FnTy>
  void syncPullRecvApply(uint32_t from_id, Galois::Runtime::RecvBuffer& buf) {
    uint32_t num;
    std::string loopName;
    Galois::Runtime::gDeserialize(buf, loopName, num);
    std::string doall_str("LAMBDA::SYNC_PULL_RECV_APPLY_" + loopName + "_" + std::to_string(num_run));
    Galois::Runtime::reportLoopInstance(doall_str);
    Galois::StatTimer StatTimer_set("SYNC_SET", loopName, Galois::start_now);

    assert(num == slaveNodes[from_id].size());
    auto& net = Galois::Runtime::getSystemNetworkInterface();


    //std::cerr << "["<<id<<"] sync_pull APPLY INSIDE sent from : " << from_id << " tag : " << Galois::Runtime::evilPhase << "\n";
    if(num > 0 ){
      std::vector<typename FnTy::ValTy> val_vec(num);

      Galois::Runtime::gDeserialize(buf, val_vec);

      if (!FnTy::setVal_batch(from_id, &val_vec[0])) {
        Galois::do_all(boost::counting_iterator<uint32_t>(0), boost::counting_iterator<uint32_t>(num), [&](uint32_t n){
            uint32_t localID = slaveNodes[from_id][n];
            FnTy::setVal((localID), getData(localID), val_vec[n]);}, Galois::loopname(doall_str.c_str()));
      }
    }
  }

public:
   typedef typename GraphTy::GraphNode GraphNode;
   typedef typename GraphTy::iterator iterator;
   typedef typename GraphTy::const_iterator const_iterator;
   typedef typename GraphTy::local_iterator local_iterator;
   typedef typename GraphTy::const_local_iterator const_local_iterator;
   typedef typename GraphTy::edge_iterator edge_iterator;


  //mGraph construction is collective
  // FIXME: use scalefactor to balance load
  mGraph(const std::string& filename, const std::string& partitionFolder, unsigned host, unsigned numHosts, std::vector<unsigned> scalefactor = std::vector<unsigned>())
    :GlobalObject(this), id(host),round(false)
  {
    Galois::StatTimer StatTimer_graph_construct("GRAPH CONSTRUCTION");
    StatTimer_graph_construct.start();

    std::string part_fileName = getPartitionFileName(partitionFolder,id,numHosts);
    std::string part_metaFile = getMetaFileName(partitionFolder, id, numHosts);

    masterNodes.resize(numHosts);
    slaveNodes.resize(numHosts);

    uint32_t NUMBER = 32;
    uint32_t numPartFiles = NUMBER/numHosts; //multiple of 2 for now.
    std::vector<OfflineGraph*> g_vec;
    totalNodes = 0;
    std::cerr << "NUMPARTSFILES : " << numPartFiles << "\n";
    uint64_t numEdges = 0;
    GlobalVec_new.resize(numPartFiles);

    for(auto i = 0; i < numPartFiles; ++i){
      uint32_t part_id = numPartFiles*id + i;
      std::cerr << "[" << id << "]" << " part_id : " << part_id << "\n";
      std::string part_fileName = getPartitionFileName(partitionFolder,part_id,NUMBER);
      std::string part_metaFile = getMetaFileName(partitionFolder, part_id, NUMBER);

      std::cerr << "[" << id  <<  "]" << part_fileName << "\n";
      g_vec.push_back(new OfflineGraph(part_fileName));
      totalNodes += (*g_vec.back()).size();
      numEdges += g_vec[i]->edge_begin(*(g_vec[i]->end())) - g_vec[i]->edge_begin(*(g_vec[i]->begin())); // depends on Offline graph impl

      readMetaFile(part_metaFile, localToGlobalMap_meta);
      std::cerr << "[" << id << "] LOCALTOGLOBAL_meta_Size : " << localToGlobalMap_meta.size() << "\n";


      /** Load one meta file at a time.*/
      for(auto info : localToGlobalMap_meta){
        mergedInfoDeq.push_back(std::make_pair(info.global_id, MergeInfo(i, info.local_id, (info.owner_id/numPartFiles))));
        slaveNodes[info.owner_id/numPartFiles].push_back(info.global_id);
        assert(info.owner_id/numPartFiles < numHosts);
        //OwnerID_merged_deq.push_back(std::make_pair(info.global_id, info.owner_id/numPartFiles));
        GlobalVec_new[i].push_back(info.global_id);
      }

      assert(mergedInfoDeq.size() == totalNodes);

      /*** FREE the memory for reuse. ***/
      localToGlobalMap_meta.clear();
      localToGlobalMap_meta.shrink_to_fit();
    }


    //SORTING deqs very important
    std::sort(mergedInfoDeq.begin(), mergedInfoDeq.end(), Comp_mergedInfo_deq());
    //std::sort(OwnerID_merged_deq.begin(), OwnerID_merged_deq.end(), Comp_owner_deq());


    std::cerr << "[" << id << "] g_vec SIZE : " << g_vec.size() << "\n";

    num_iter_push = 0;
    num_iter_pull = 0;
    num_run = 0;
    std::cerr << "[" << id << "] SIZE ::::  " << totalNodes << "\n";
    std::cerr << "[" << id << "] TOTAL EDGES ::::  " << numEdges << "\n";


    std::cerr << "[" << id << "]"<< " : Loading data done\n";

    //IMPORTANT
    numOwned = 0;
    std::cerr << "[" << id << "]"<< ": Filling GlobalVec_merged\n";
    //auto mergedInfoDeq_begin = mergedInfoDeq.begin();
    for(size_t deq_index = 0; deq_index < mergedInfoDeq.size();){
      auto p = std::equal_range(mergedInfoDeq.begin() + deq_index, mergedInfoDeq.end(), mergedInfoDeq[deq_index].first, Comp_mergedInfo_deq());
      //assert((*(p.first)).first == mergedInfoDeq[deq_index].first);
      //GlobalVec_merged.push_back(mergedInfoDeq[deq_index].first);
      //OwnerVec_merged.push_back(mergedInfoDeq[deq_index].second.ownerID);
      deq_index += (p.second - p.first);
      ++numOwned;
    }


    GlobalVec_merged.reserve(numOwned);
    OwnerVec_merged.reserve(numOwned);
    for(size_t deq_index = 0; deq_index < mergedInfoDeq.size();){
      auto p = std::equal_range(mergedInfoDeq.begin() + deq_index, mergedInfoDeq.end(), mergedInfoDeq[deq_index].first, Comp_mergedInfo_deq());
      //assert((*(p.first)).first == mergedInfoDeq[deq_index].first);
      GlobalVec_merged.push_back(mergedInfoDeq[deq_index].first);
      OwnerVec_merged.push_back(mergedInfoDeq[deq_index].second.ownerID);
      deq_index += (p.second - p.first);
    }

    assert(numOwned == GlobalVec_merged.size());
    assert(std::is_sorted(GlobalVec_merged.begin(), GlobalVec_merged.end()));

    std::cerr << "[" << id << "]"<< ": DONE Filling GlobalVec_merged, NUMOWNED : "<< numOwned <<"\n";

#if 0
    std::cerr << "[" << id << "]"<< ": Filling OwnerVec_merged\n";
    auto OwnerID_merged_deq_begin = OwnerID_merged_deq.begin();
    for(size_t deq_index = 0; deq_index < OwnerID_merged_deq.size();){
      auto p = std::equal_range(OwnerID_merged_deq.begin() + deq_index, OwnerID_merged_deq.end(), OwnerID_merged_deq[deq_index].first, Comp_owner_deq());
      OwnerVec_merged.push_back(OwnerID_merged_deq[deq_index].second);
      deq_index += (p.second - p.first);
    }
    std::cerr << "[" << id << "]"<< ": DONE Filling OwnerVec_merged\n";

    /*** FREE the memory ***/
    OwnerID_merged_deq.clear();
    OwnerID_merged_deq.shrink_to_fit();
#endif

    graph.allocateFrom(numOwned, numEdges);
    //std::cerr << "Allocate done\n";

    graph.constructNodes();
    //std::cerr << "Construct nodes done\n";
    loadEdges<std::is_void<EdgeTy>::value>(g_vec);

    StatTimer_graph_construct.stop();

    /***Free memory since edges have been constructed.***/
    mergedInfoDeq.clear();
    mergedInfoDeq.shrink_to_fit();


    setup_communication(numHosts);

    std::cerr << "DONE setting up communication\n";

#ifdef __GALOIS_SIMULATE_COMMUNICATION__
#ifndef __GALOIS_SIMULATE_COMMUNICATION_WITH_GRAPH_DATA__
      simulate_communication();
#endif
#endif
  }

  void setup_communication(unsigned numHosts) {
    Galois::StatTimer StatTimer_comm_setup("COMMUNICATION_SETUP_TIME");
    StatTimer_comm_setup.start();

    //Exchange information.
    exchange_info_init();

    std::cerr << "DONE exchange_info_init \n";

    for(uint32_t h = 0; h < masterNodes.size(); ++h){
       Galois::do_all(boost::counting_iterator<uint32_t>(0), boost::counting_iterator<uint32_t>(masterNodes[h].size()),
           [&](uint32_t n){
           masterNodes[h][n] = G2L_merged(masterNodes[h][n]);
           }, Galois::loopname("MASTER_NODES"));
    }

    std::cerr << "DONE converting masterNodes to localID \n";

    for(uint32_t h = 0; h < slaveNodes.size(); ++h){
       Galois::do_all(boost::counting_iterator<uint32_t>(0), boost::counting_iterator<uint32_t>(slaveNodes[h].size()),
           [&](uint32_t n){
           slaveNodes[h][n] = G2L_merged(slaveNodes[h][n]);
           }, Galois::loopname("SLAVE_NODES"));
    }

    std::cerr << "DONE converting salveNodes to localID \n";

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
      void loadEdges(std::vector<OfflineGraph*>& g_vec) {
        fprintf(stderr, "Loading VOID edge-data while creating edges.\n");
        uint64_t cur = 0;
        auto deq_ii = mergedInfoDeq.begin();
        size_t deq_index  = 0;
        uint32_t node = 0;
        for(; deq_index < mergedInfoDeq.size(); ){
          auto p = std::equal_range(mergedInfoDeq.begin() + deq_index, mergedInfoDeq.end(), mergedInfoDeq[deq_index].first, Comp_mergedInfo_deq());
          for(auto p_i = p.first; p_i != p.second; ++p_i){
            auto v = (*p_i).second;
            for(auto ii = g_vec[v.partition]->edge_begin(v.localID), ee = g_vec[v.partition]->edge_end(v.localID); ii < ee; ++ii){
              uint32_t gdst = g_vec[v.partition]->getEdgeDst(ii);
              uint64_t dest_globalID = GlobalVec_new[v.partition][gdst];
              auto gdata = g_vec[v.partition]->getEdgeData<EdgeTy>(ii);
              uint32_t gdst_merged = G2L_merged(dest_globalID); //G2L_merged(globalID);
              graph.constructEdge(cur++, gdst_merged, gdata);
            }
            ++deq_index;
          }
          graph.fixEndEdge(node++, cur);
        }
        assert(numOwned == node);
      }

    template<bool isVoidType, typename std::enable_if<isVoidType>::type* = nullptr>
      void loadEdges(std::vector<OfflineGraph*>& g_vec) {
        fprintf(stderr, "Loading VOID edge-data while creating edges.\n");
        uint64_t cur = 0;
        auto deq_ii = mergedInfoDeq.begin();
        size_t deq_index  = 0;
        uint32_t node = 0;
        for(; deq_index < mergedInfoDeq.size(); ){
          auto p = std::equal_range(mergedInfoDeq.begin() + deq_index, mergedInfoDeq.end(), mergedInfoDeq[deq_index].first, Comp_mergedInfo_deq());
          for(auto p_i = p.first; p_i != p.second; ++p_i){
            auto v = (*p_i).second;
            for(auto ii = g_vec[v.partition]->edge_begin(v.localID), ee = g_vec[v.partition]->edge_end(v.localID); ii < ee; ++ii){
              uint32_t gdst = g_vec[v.partition]->getEdgeDst(ii);
              uint64_t dest_globalID = GlobalVec_new[v.partition][gdst];
              uint32_t gdst_merged = G2L_merged(dest_globalID); //G2L_merged(globalID);
              graph.constructEdge(cur++, gdst_merged);
            }
            ++deq_index;
          }
          graph.fixEndEdge(node++, cur);
        }
        assert(numOwned == node);
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

  size_t size() const { return graph.size(); }
  size_t sizeEdges() const { return graph.sizeEdges(); }

  const_iterator begin() const { return graph.begin(); }
  iterator begin() { return graph.begin(); }
  const_iterator end() const { return graph.begin() + numOwned; }
  iterator end() { return graph.begin() + numOwned; }

  const_iterator ghost_begin() const { return end(); }
  iterator ghost_begin() { return end(); }
  const_iterator ghost_end() const { return graph.end(); }
  iterator ghost_end() { return graph.end(); }


  void exchange_info_init(){
    auto& net = Galois::Runtime::getSystemNetworkInterface();
    //may be reusing tag, so need a barrier
    Galois::Runtime::getHostBarrier().wait();

    //send
    for (unsigned x = 0; x < net.Num; ++x) {
      if((x == id))
        continue;

      Galois::Runtime::SendBuffer b;
      gSerialize(b, (uint64_t)slaveNodes[x].size(), slaveNodes[x]);
      net.sendTagged(x, 1, b);
      std::cout << " number of slaves from : " << x << " : " << slaveNodes[x].size() << "\n";
    }
    //recieve
    for (unsigned x = 0; x < net.Num; ++x) {
      if ((x == id))
        continue;
      decltype(net.recieveTagged(1, nullptr)) p;
      do {
        net.handleReceives();
        p = net.recieveTagged(1, nullptr);
      } while (!p);

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
  static void syncRecv(uint32_t src, Galois::Runtime::RecvBuffer& buf) {
      uint32_t oid;
      void (mGraph::*fn)(Galois::Runtime::RecvBuffer&);
      Galois::Runtime::gDeserialize(buf, oid, fn);
      mGraph* obj = reinterpret_cast<mGraph*>(ptrForObj(oid));
      (obj->*fn)(buf);
      //--(obj->num_recv_expected);
      //std::cout << "[ " << Galois::Runtime::getSystemNetworkInterface().ID << "] " << " NUM RECV EXPECTED : " << (obj->num_recv_expected) << "\n";
   }

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
      void (mGraph::*fn)(Galois::Runtime::RecvBuffer&) = &mGraph::syncRecvApplyPull<FnTy>;
#else
      void (mGraph::*fn)(Galois::Runtime::RecvBuffer&) = &mGraph::syncRecvApplyPull;
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
      void (mGraph::*fn)(Galois::Runtime::RecvBuffer&) = &mGraph::syncRecvApplyPush<FnTy>;
#else
      void (mGraph::*fn)(Galois::Runtime::RecvBuffer&) = &mGraph::syncRecvApplyPush;
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
    std::string doall_str("LAMBDA::SYNC_PUSH_" + loopName + "_" + std::to_string(num_run) + "_" + std::to_string(num_iter_push));
    Galois::Statistic SyncPush_send_bytes("SEND_BYTES_SYNC_PUSH", loopName);
    Galois::StatTimer StatTimer_extract("SYNC_PUSH_EXTRACT", loopName);
    Galois::StatTimer StatTimer_syncPush("SYNC_PUSH", loopName, Galois::start_now);
    auto& net = Galois::Runtime::getSystemNetworkInterface();

    for (unsigned x = 0; x < net.Num; ++x) {
      uint32_t num = slaveNodes[x].size();
      if((x == id))
        continue;

      Galois::Runtime::SendBuffer b;
      gSerialize(b, loopName,  num);

      StatTimer_extract.start();
      if(num > 0 ){
        std::vector<typename FnTy::ValTy> val_vec(num);

        if (!FnTy::extract_reset_batch(x, &val_vec[0])) {
          Galois::do_all(boost::counting_iterator<uint32_t>(0), boost::counting_iterator<uint32_t>(num), [&](uint32_t n){
              uint32_t lid = slaveNodes[x][n];
              auto val = FnTy::extract(lid, getData(lid));
              FnTy::reset(lid, getData(lid));
              val_vec[n] = val;
            }, Galois::loopname(doall_str.c_str()));
        }

        gSerialize(b, val_vec);
      }
      StatTimer_extract.stop();

      SyncPush_send_bytes += b.size();
      net.sendTagged(x, Galois::Runtime::evilPhase, b);
    }

    net.flush();

    for (unsigned x = 0; x < net.Num; ++x) {
      if ((x == id))
        continue;
      decltype(net.recieveTagged(Galois::Runtime::evilPhase,nullptr)) p;
      do {
        net.handleReceives();
        p = net.recieveTagged(Galois::Runtime::evilPhase, nullptr);
      } while (!p);
      syncRecvApply<FnTy>(p->first, p->second);
    }
    ++Galois::Runtime::evilPhase;
    StatTimer_syncPush.stop();
  }


  template<typename FnTy>
  void sync_pull(std::string loopName) {
    ++num_iter_pull;
    std::string doall_str("LAMBDA::SYNC_PULL_" + loopName + "_" + std::to_string(num_run) + "_" + std::to_string(num_iter_pull));
    Galois::Statistic SyncPull_send_bytes("SEND_BYTES_SYNC_PULL", loopName);
    Galois::StatTimer StatTimer_extract("SYNC_PULL_EXTRACT", loopName);
    Galois::StatTimer StatTimer_syncPull("SYNC_PULL", loopName, Galois::start_now);
    auto& net = Galois::Runtime::getSystemNetworkInterface();

    for (unsigned x = 0; x < net.Num; ++x) {
      uint32_t num = masterNodes[x].size();
      if((x == id))
        continue;

      Galois::Runtime::SendBuffer b;
      gSerialize(b, loopName, num);

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
      //std::cerr << "["<<id<<"] sync_pull sent to : " << x << " tag : " << Galois::Runtime::evilPhase <<"\n";

    }

    net.flush();

    for (unsigned x = 0; x < net.Num; ++x) {
      if ((x == id))
        continue;
      decltype(net.recieveTagged(Galois::Runtime::evilPhase,nullptr)) p;
      do {
        net.handleReceives();
        p = net.recieveTagged(Galois::Runtime::evilPhase, nullptr);
      } while (!p);
      //std::cerr << "["<<id<<"] sync_pull APPLY sent from : " << x << " tag : " << Galois::Runtime::evilPhase <<"\n";
      syncPullRecvApply<FnTy>(p->first, p->second);
    }

    ++Galois::Runtime::evilPhase;
    StatTimer_syncPull.stop();
  }

  uint64_t getGID(size_t nodeID) {
      return L2G_merged(nodeID);
   }
  uint32_t getLID(uint64_t nodeID) {
      return G2L_merged(nodeID);
   }

   uint16_t getHostID(uint64_t gid) {
    auto lid = G2L_merged(gid);
    return OwnerVec_merged[lid];
   }
   uint32_t getNumOwned() const {
      return numOwned;
   }

   uint64_t getGlobalOffset() const {
     return 0;
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

  /**For resetting num_iter_pull and push.**/
   void reset_num_iter(uint32_t runNum){
      num_iter_pull = 0;
      num_iter_push = 0;
      num_run = runNum;
   }

};
#endif//_GALOIS_DIST_mGraph_H
