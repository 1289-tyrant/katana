/** Galois Per-Topo Storage -*- C++ -*-
 * @file
 * @section License
 *
 * Galois, a framework to exploit amorphous data-parallelism in irregular
 * programs.
 *
 * Copyright (C) 2012, The University of Texas at Austin. All rights reserved.
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
 * @author Donald Nguyen <ddn@cs.utexas.edu>
 * @author Andrew Lenharth <andrewl@lenharth.org>
 */


#ifndef GALOIS_RUNTIME_PERHOSTSTORAGE
#define GALOIS_RUNTIME_PERHOSTSTORAGE

#include "Galois/Runtime/Serialize.h"
#include "Galois/Runtime/DistSupport.h"
#include "Galois/Runtime/ThreadPool.h"
#include <boost/iterator/iterator_facade.hpp>

namespace Galois {
namespace Runtime {

class PerBackend_v2 {
  std::unordered_map<uint64_t, void*> items;
  std::unordered_map<std::pair<uint64_t, uint32_t>, void*,
		     pairhash<std::pair<uint64_t, uint32_t>>> remoteCache;

  uint32_t nextID;
  LL::SimpleLock<true> lock;

  void* releaseAt_i(uint64_t);
  void* resolve_i(uint64_t);
  void* resolveRemote_i(uint64_t, uint32_t);
  void addRemote(void* ptr, uint32_t srcID, uint64_t off);

  static void pBe2ResolveLP(void* ptr, uint32_t srcID, uint64_t off);
  static void pBe2Resolve(uint32_t dest, uint64_t off);

public:
  PerBackend_v2();

  uint64_t allocateOffset();
  void deallocateOffset(uint64_t);
  
  void createAt(uint64_t, void*);

  template<typename T>
  T* releaseAt(uint64_t off) { return reinterpret_cast<T*>(releaseAt_i(off)); }

  template<typename T>
  T* resolve(uint64_t off ) { return reinterpret_cast<T*>(resolve_i(off)); }

  //returns pointer in remote address space
  template<typename T>
  gptr<T> resolveRemote(uint64_t off, uint32_t hostID) {
    return gptr<T>(hostID, reinterpret_cast<T*>(resolveRemote_i(off, hostID)));
  }
};

PerBackend_v2& getPerHostBackend();

template<typename T>
class PerHost {

  //global name
  uint64_t offset;

  //cached pair
  mutable uint32_t localHost;
  mutable T* localPtr;

  T* resolve() const {
    if (localHost != networkHostID || !localPtr) {
      localHost = networkHostID;
      localPtr = getPerHostBackend().resolve<T>(offset);
    }
    return localPtr;  
  }

  explicit PerHost(uint64_t off) :offset(off), localHost(~0), localPtr(nullptr) {}

  static void allocOnHost(DeSerializeBuffer& buf) {
    uint64_t off;
    gDeserialize(buf, off);
    getPerHostBackend().createAt(off, new T(PerHost(off), buf));
  }

  static void deallocOnHost(uint64_t off) {
    delete getPerHostBackend().releaseAt<T>(off);
  }

public:
  //create a pointer
  static PerHost allocate() {
    uint64_t off = getPerHostBackend().allocateOffset();
    getPerHostBackend().createAt(off, new T(PerHost(off)));
    PerHost ptr(off);
    SerializeBuffer buf;
    gSerialize(buf, off);
    ptr->getInitData(buf);
    getSystemNetworkInterface().broadcast(&allocOnHost, buf);
    return ptr;
  }
  static void deallocate(PerHost ptr) {
    getSystemNetworkInterface().broadcastAlt(&deallocOnHost, ptr.offset);
    deallocOnHost(ptr.offset);
    getPerHostBackend().deallocateOffset(ptr.offset);
  }

  PerHost() : offset(0), localHost(~0), localPtr(nullptr) {}

  gptr<T> remote(uint32_t hostID) {
    return getPerHostBackend().resolveRemote<T>(offset, hostID);
  }

  gptr<T> local() {
    return remote(networkHostID);
  }

  T& operator*() const { return *resolve(); }
  T* operator->() const { return resolve(); }

  bool operator<(const PerHost& rhs)  const { return offset <  rhs.offset; }
  bool operator>(const PerHost& rhs)  const { return offset >  rhs.offset; }
  bool operator==(const PerHost& rhs) const { return offset == rhs.offset; }
  bool operator!=(const PerHost& rhs) const { return offset != rhs.offset; }
  explicit operator bool() const { return offset != 0; }

  //serialize
  typedef int tt_has_serialize;
  void serialize(Galois::Runtime::SerializeBuffer& s) const {
    gSerialize(s,offset);
  }
  void deserialize(Galois::Runtime::DeSerializeBuffer& s) {
    gDeserialize(s,offset);
    localHost = ~0;
    localPtr = nullptr;
  }
};



class PerBackend_v3 {
  static const int dynSlots = 1024;
  static __thread void* space[dynSlots];
  
  std::vector<bool> freelist;
  std::vector<void**> heads;
  std::map<std::tuple<uint64_t, uint32_t, uint32_t>, void*> remoteCache;
  LL::SimpleLock<true> lock;

  void* resolveRemote_i(uint64_t, uint32_t, uint32_t);
  void addRemote(void* ptr, uint32_t srcID, uint64_t off, uint32_t threadID);

  static void pBe2ResolveLP(void* ptr, uint32_t srcID, uint64_t off, uint32_t threadID);
  static void pBe2Resolve(uint32_t dest, uint64_t off, uint32_t threadID);

public:
  PerBackend_v3();

  void initThread();

  uint64_t allocateOffset();
  void deallocateOffset(uint64_t);
  
  template<typename T>
  T*& resolve(uint64_t off ) { return *reinterpret_cast<T**>(&space[off]); }

  template<typename T>
  T*& resolveThread(uint64_t off, uint32_t tid) {
    return *reinterpret_cast<T**>(&heads.at(tid)[off]);
  }

  //returns pointer in remote address space
  template<typename T>
  gptr<T> resolveRemote(uint64_t off, uint32_t hostID, uint32_t threadID) {
    return gptr<T>(hostID, reinterpret_cast<T*>(resolveRemote_i(off, hostID, threadID)));
  }
};

PerBackend_v3&  getPerThreadDistBackend();

template<typename T>
class PerThreadDist {
  //global name
  uint64_t offset;

  T* resolve() const {
    T* r = getPerThreadDistBackend().resolve<T>(offset);
    assert(r);
    return r;
  }

  explicit PerThreadDist(uint64_t off) :offset(off) {}

  static void allocOnHost(DeSerializeBuffer& buf) {
    uint64_t off;
    gDeserialize(buf, off);
    for (unsigned x = 0; x < getSystemThreadPool().getMaxThreads(); ++x) {
      if (!getPerThreadDistBackend().resolveThread<T>(off, x)) {
	auto buf2 = buf;
	getPerThreadDistBackend().resolveThread<T>(off, x) = new T(PerThreadDist(off), buf2);
      }
      //std::cout << off << " " << x << " " << getPerThreadDistBackend().resolveThread<T>(off, x) << "\n";
    }
  }

  static void deallocOnHost(uint64_t off) {
    for (unsigned x = 0; x < getSystemThreadPool().getMaxThreads(); ++x)
      delete getPerThreadDistBackend().resolveThread<T>(off, x);
  }

public:
  //create a pointer
  static PerThreadDist allocate() {
    uint64_t off = getPerThreadDistBackend().allocateOffset();
    getPerThreadDistBackend().resolve<T>(off) = new T(PerThreadDist(off));
    PerThreadDist ptr(off);
    SerializeBuffer buf, buf2;
    gSerialize(buf, off);
    ptr->getInitData(buf);
    buf2 = buf;
    getSystemNetworkInterface().broadcast(&allocOnHost, buf);
    DeSerializeBuffer dbuf(std::move(buf2));
    allocOnHost(dbuf);
    return ptr;
  }
  static void deallocate(PerThreadDist ptr) {
    getSystemNetworkInterface().broadcastAlt(&deallocOnHost, ptr.offset);
    deallocOnHost(ptr.offset);
    getPerHostBackend().deallocateOffset(ptr.offset);
  }

  PerThreadDist() : offset(~0) {}

  gptr<T> remote(uint32_t hostID, unsigned threadID) const {
    if (hostID == networkHostID)
      return gptr<T>(getPerThreadDistBackend().resolveThread<T>(offset, threadID));
    else
      return getPerThreadDistBackend().resolveRemote<T>(offset, hostID, threadID);
  }
  
  gptr<T> local() const {
    return gptr<T>(networkHostID, resolve());
  }

  T& operator*() const { return *resolve(); }
  T* operator->() const { return resolve(); }

  bool operator<(const PerThreadDist& rhs)  const { return offset <  rhs.offset; }
  bool operator>(const PerThreadDist& rhs)  const { return offset >  rhs.offset; }
  bool operator==(const PerThreadDist& rhs) const { return offset == rhs.offset; }
  bool operator!=(const PerThreadDist& rhs) const { return offset != rhs.offset; }
  explicit operator bool() const { return offset != 0; }

  class iterator :public boost::iterator_facade<iterator, gptr<T>, std::forward_iterator_tag, gptr<T>>
  {
    friend class boost::iterator_core_access;
    friend class PerThreadDist;
    uint32_t hostID;
    uint32_t threadID;
    PerThreadDist basePtr;

    gptr<T> dereference() const { return basePtr.remote(hostID, threadID); }
    bool equal(const iterator& rhs) const { return hostID == rhs.hostID && threadID == rhs.threadID && basePtr == rhs.basePtr; }
    void increment() {
      if (threadID < activeThreads)
	++threadID;
      if (threadID == activeThreads && hostID < networkHostNum) { // FIXME: maxthreads on hostID
	++hostID;
	threadID = 0;
      }
      if (hostID == networkHostNum) {
	threadID = activeThreads;
	basePtr = PerThreadDist();
      }
    }
    iterator(uint32_t h, uint32_t t, PerThreadDist p) :hostID(h), threadID(t), basePtr(p) {}
  public:
    iterator() :hostID(networkHostNum), threadID(activeThreads), basePtr() {}
  };

  iterator begin() { return iterator(0,0,*this); }
  iterator end() { return iterator(); }

  // //serialize
  // typedef int tt_has_serialize;
  // void serialize(Galois::Runtime::SerializeBuffer& s) const {
  //   gSerialize(s,offset);
  // }
  // void deserialize(Galois::Runtime::DeSerializeBuffer& s) {
  //   gDeserialize(s,offset);
  //  }
};

  



} // end namespace
} // end namespace

#endif
