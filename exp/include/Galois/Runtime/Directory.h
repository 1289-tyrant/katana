/** Galois Distributed Directory -*- C++ -*-
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
 * @author Andrew Lenharth <andrewl@lenharth.org>
 */

#ifndef GALOIS_RUNTIME_DIRECTORY_H
#define GALOIS_RUNTIME_DIRECTORY_H

#include "Galois/gstl.h"
#include "Galois/MethodFlags.h"
#include "Galois/Runtime/CacheManager.h"
#include "Galois/Runtime/FatPointer.h"
#include "Galois/Runtime/Lockable.h"
#include "Galois/Runtime/Network.h"
#include "Galois/Runtime/Support.h"
#include "Galois/Runtime/Tracer.h"
#include "Galois/Runtime/ll/gio.h"
#include "Galois/Runtime/ll/SimpleLock.h"
#include "Galois/Runtime/ll/TID.h"

#include <boost/intrusive_ptr.hpp>
#include <boost/utility.hpp>

#include <array>
#include <functional>
#include <mutex>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace Galois {
namespace Runtime {

enum ResolveFlag {INV=0, RO=1, RW=2, UP_RO=3, UP_RW=4};

namespace detail {

//Central function for recieving objects
template<typename T>
void recvObject(RecvBuffer& buf);

//Central function for recieving requests
template<typename T>
void recvRequest(RecvBuffer& buf);

template<typename metadata>
class MetaHolder {
  std::unordered_map<fatPointer, metadata> md;
  LL::SimpleLock md_lock;

public:
  metadata& getMD(fatPointer ptr) {
    std::lock_guard<LL::SimpleLock> lg(md_lock);
    auto& retval = md[ptr];
    retval.lock.lock();
    return retval;
  }

  void eraseMD(fatPointer ptr, metadata& m) {
    assert(md.find(ptr) != md.end());
    assert(&m == &md[ptr]);
    md.erase(ptr);
  }
};

} //namespace detail

//Base class for common directory operations
class BaseDirectory {
public:
  //These wrap type information for various dispatching purposes.  This
  //let's us keep vtables out of user objects
  class typeHelper {
  public:
    virtual void deserialize(RecvBuffer&, Lockable*) const = 0;
    virtual void cmCreate(fatPointer, ResolveFlag, RecvBuffer&) const = 0;
    virtual void send(uint32_t dest, fatPointer ptr, Lockable* obj, ResolveFlag flag) const = 0;
    virtual void request(uint32_t dest, fatPointer ptr, uint32_t whom, ResolveFlag flag) const = 0;
  };
  
  template<typename T>
  class typeHelperImpl : public typeHelper {
  public:
    static typeHelperImpl* get() {
      static typeHelperImpl th;
      return &th;
    }

    virtual void deserialize(RecvBuffer&, Lockable*) const;
    virtual void cmCreate(fatPointer, ResolveFlag, RecvBuffer&) const;
    virtual void send(uint32_t, fatPointer, Lockable*, ResolveFlag) const;
    virtual void request(uint32_t, fatPointer, uint32_t, ResolveFlag) const;
  };

protected:
    
  LockManagerBase dirContext;
  LL::SimpleLock dirContext_lock;

  bool dirAcquire(Lockable*);
  void dirRelease(Lockable*);
  bool dirOwns(Lockable*);
};


/**
 * Manages local objects sent to remote hosts
 */
class LocalDirectory : public BaseDirectory {
  struct metadata {
    //Lock protecting this structure
    LL::SimpleLock lock;
    //Locations which have the object in RO state
    std::set<uint32_t> locRO;
    //Location which has the object in RW state
    uint32_t locRW;
    //ID of host recalled for
    uint32_t recalled;
    //outstanding requests
    std::set<uint32_t> reqsRO;
    std::set<uint32_t> reqsRW;
    //Last sent transfer
    //whether object is participating in priority protocol
    bool contended;

    //People to notify
    std::deque<std::function<void(fatPointer)> > notifyList;

    //Type aware helper functions
    typeHelper* th;

    //Add a request
    void addReq(uint32_t dest, ResolveFlag flag);

    //Get next requestor
    std::pair<uint32_t, bool> getNextDest() {
      uint32_t nextDest = ~0;
      bool nextIsRW = false;
      if (!reqsRO.empty())
        nextDest = *reqsRO.begin();
      if (!reqsRW.empty()) {
        nextDest = std::min(nextDest, *reqsRW.begin());
        if (*reqsRW.begin() == nextDest)
          nextIsRW = true;
      }
      return std::make_pair(nextDest, nextIsRW);
    }

    //!returns whether object is still needs processing
    bool writeback();

    uint32_t removeNextRW() {
      uint32_t retval = *reqsRW.begin();
      reqsRW.erase(reqsRW.begin());
      assert(retval != ~0U);
      assert(retval != NetworkInterface::ID);
      assert(locRW == ~0U);
      assert(locRO.empty());
      return retval;
    }

    uint32_t removeNextRO() {
      uint32_t retval = *reqsRO.begin();
      reqsRO.erase(reqsRO.begin());
      locRO.insert(retval);
      assert(retval != ~0U);
      assert(retval != NetworkInterface::ID);
      assert(locRW == ~0U);
      return retval;
    }

    //Returns if object is present and there are no RO replicas
    bool isHere() const {
      return locRW == ~0 && locRO.empty();
    }

    //Returns if object has RO replicas and hasn't been recalled
    bool isRO() const {
      return recalled == ~0 && !locRO.empty();
    }

    metadata() :locRW(~0), recalled(~0), contended(0), th(nullptr) {}

    friend std::ostream& operator<<(std::ostream& os, const metadata& md) {
      std::ostream_iterator<uint32_t> out_it(os, ",");
      os << "locRO:<";
      std::copy(md.locRO.begin(), md.locRO.end(), out_it);
      os << ">,locRW:";
      if (md.locRW != ~0) os << md.locRW;
      os << ",recalled:";
      if (md.recalled != ~0) os << md.recalled;
      os << ",reqsRO:<";
      std::copy(md.reqsRO.begin(), md.reqsRO.end(), out_it);
      os << ">,reqsRW:<";
      std::copy(md.reqsRW.begin(), md.reqsRW.end(), out_it);
      os << ">,contended:" << md.contended << ",th:" << md.th;
      return os;
    }
  };

  std::unordered_map<fatPointer, metadata> dir;
  LL::SimpleLock dir_lock;

  std::unordered_set<fatPointer> pending;
  LL::SimpleLock pending_lock;
  
  //!Add a request to process later
  void addPendingReq(fatPointer ptr) {
    std::lock_guard<LL::SimpleLock> lg(pending_lock);
    pending.insert(ptr);
  }

  metadata& getMD(fatPointer ptr);

  void eraseMD(fatPointer ptr, std::unique_lock<LL::SimpleLock>& mdl);

  //!Send object to all outstanding readers
  void sendToReaders(metadata&, fatPointer);
  
  //!Send invalidate to all outstanding readers
  void invalidateReaders(metadata&, fatPointer, uint32_t);

  //!Forward request to next writer (if available)
  void forwardRequestToNextWriter(metadata&, fatPointer, std::unique_lock<LL::SimpleLock>&);

  //!Consider object for local use or to send on
  void considerObject(metadata& m, fatPointer ptr);

  void fetchImpl(fatPointer ptr, ResolveFlag flag, typeHelper* th, bool setContended);

protected:
  void recvObjectImpl(fatPointer, ResolveFlag, typeHelper* th, RecvBuffer&);

  void recvRequestImpl(fatPointer, uint32_t, ResolveFlag, typeHelper*);

  //allow receiving objects
  template<typename T>
  friend void detail::recvObject(RecvBuffer& buf);
  //allow receiving requests
  template<typename T>
  friend void detail::recvRequest(RecvBuffer& buf);

  void invalidateImpl(fatPointer, typeHelper*);

public:
  //Local portion of API

  bool isRemote(fatPointer ptr, ResolveFlag flag) {
    return dirOwns(static_cast<Lockable*>(ptr.getObj()));
  }

  //! initiate, if necessary, a fetch of a remote object
  template<typename T>
  void fetch(fatPointer ptr, ResolveFlag flag) {
    fetchImpl(ptr, flag, typeHelperImpl<T>::get(), false);
  }

  //! engage priority protocol for ptr.  May issue fetch
  template<typename T>
  void setContended(fatPointer ptr, ResolveFlag flag) {
    fetchImpl(ptr, flag, typeHelperImpl<T>::get(), true);
  }
  //! unengage priority protocol for ptr.  May send object away
  void clearContended(fatPointer ptr);

  //!Send invalidate to all outstanding reader/writers
  //TODO(ddn) remove this in favor of standard acquire, writeback mechanism
  void invalidate(fatPointer ptr);

  //! setup notification on object reciept.  Returns true if
  //! notification registered.  Returns false if object already in
  //! requested state (no notification actualy registered)
  bool notify(fatPointer ptr, ResolveFlag flag, std::function<void (fatPointer)> fnotify);

  void resetStats() {}
  void reportStats(const char* loopname) {}

  void makeProgress();
  void dump();
};

LocalDirectory& getLocalDirectory();

////////////////////////////////////////////////////////////////////////////////

/**
 * Manages remote objects from remote hosts.
 */
class RemoteDirectory : public BaseDirectory {
  //metadata for an object.
  struct metadata {
    enum StateFlag {
      INVALID=0,    //Not present and not requested
      PENDING_RO=1, //Not present and requested RO
      PENDING_RW=2, //Not present and requested RW
      HERE_RO=3,         //present as RO
      HERE_RW=4,         //present as RW
      UPGRADE=5     //present as RO and requested RW
    };
    LL::SimpleLock lock;
    StateFlag state;
    bool contended;

    //People to notify
    std::deque<std::function<void(fatPointer)> > notifyList;

    typeHelper* th;

    metadata();
    void recvObj(ResolveFlag);
    //! returns the message to send.  INV means don't send anything.
    //! Updates internal state assuming the message is sent
    ResolveFlag fetch(ResolveFlag flag);
  };

  struct outstandingReq {
    uint32_t dest;
    ResolveFlag flag;
  };

  //sigh
  friend std::ostream& operator<<(std::ostream& os, const metadata& md);
  //allow receiving objects
  template<typename T>
  friend void detail::recvObject(RecvBuffer& buf);
  //allow receiving requests
  template<typename T>
  friend void detail::recvRequest(RecvBuffer& buf);

  std::unordered_map<fatPointer, metadata> md;
  LL::SimpleLock md_lock;

  std::unordered_map<fatPointer, outstandingReq> reqs;
  LL::SimpleLock reqs_lock;


  //get metadata for pointer
  metadata& getMD(fatPointer ptr);

  //erase the metadata for pointer
  //metadata is for the pointer and the caller must have the lock
  //invalidates the metadata
  void eraseMD(fatPointer, std::unique_lock<LL::SimpleLock>& mdl);

  //!Add a request to process later
  void addPendingReq(fatPointer, uint32_t, ResolveFlag);

  //try to writeback ptr, may fail
  //if succeeds, invalidates md
  bool tryWriteBack(metadata& md, fatPointer ptr, std::unique_lock<LL::SimpleLock>& lg);

protected: // Remote portion of the api
  //! handle incoming object
  void recvObjectImpl(fatPointer, ResolveFlag, typeHelper*, RecvBuffer&);

  //! handle incoming requests
  void recvRequestImpl(metadata&, fatPointer, std::unique_lock<LL::SimpleLock>&, uint32_t, ResolveFlag);
  void recvRequestImpl(fatPointer, uint32_t, ResolveFlag);

  //! handle local requests
  void fetchImpl(fatPointer ptr, ResolveFlag flag, typeHelper* th, bool setContended);

public:
  // Local portion of the api

  //! process any queues
  void makeProgress();

  //! initiate, if necessary, a fetch of a remote object
  template<typename T>
  void fetch(fatPointer ptr, ResolveFlag flag) {
    fetchImpl(ptr, flag, typeHelperImpl<T>::get(), false);
  }

  //! engage priority protocol for ptr.  May issue fetch
  template<typename T>
  void setContended(fatPointer ptr, ResolveFlag flag) {
    fetchImpl(ptr, flag, typeHelperImpl<T>::get(), true);
  }
  //! unengage priority protocol for ptr.  May writeback object
  void clearContended(fatPointer ptr);

  //! setup notification on object reciept.  Returns true if
  //! notification registered.  Returns false if object already in
  //! requested state (no notification actually registered)
  bool notify(fatPointer ptr, ResolveFlag flag, std::function<void (fatPointer)> fnotify);

  void resetStats();
  void reportStats(const char* loopname);

  void dump(fatPointer ptr); //dump one object info
  void dump(); //dump directory status
};

RemoteDirectory& getRemoteDirectory();

////////////////////////////////////////////////////////////////////////////////

template<typename T>
void BaseDirectory::typeHelperImpl<T>::deserialize(RecvBuffer& buf, Lockable* ptr) const {
  gDeserialize(buf, *static_cast<T*>(ptr));
}

template<typename T>
void BaseDirectory::typeHelperImpl<T>::cmCreate(fatPointer ptr, ResolveFlag flag, RecvBuffer& buf) const {
  //FIXME: deal with RO
  getCacheManager().create<T>(ptr, buf);
}

template<typename T>
void BaseDirectory::typeHelperImpl<T>::send(uint32_t dest, fatPointer ptr, Lockable* obj, ResolveFlag flag) const {
  SendBuffer buf;
  gSerialize(buf, ptr, flag, *static_cast<T*>(obj));
  getSystemNetworkInterface().send(dest, detail::recvObject<T>, buf);
}

template<typename T>
void BaseDirectory::typeHelperImpl<T>::request(uint32_t dest, fatPointer ptr, uint32_t whom, ResolveFlag flag) const {
  SendBuffer buf;
  gSerialize(buf, ptr, whom, flag);
  getSystemNetworkInterface().send(dest, detail::recvRequest<T>, buf);
}

////////////////////////////////////////////////////////////////////////////////

struct remote_ex {
  fatPointer ptr;
  Galois::MethodFlag flag;
  void (RemoteDirectory::*rfetch) (fatPointer, ResolveFlag);
  void (LocalDirectory::*lfetch) (fatPointer, ResolveFlag);
};

////////////////////////////////////////////////////////////////////////////////

//! Make progress in the network
inline void doNetworkWork() {
  if ((NetworkInterface::Num > 1)) {// && (LL::getTID() == 0)) {
    auto& net = getSystemNetworkInterface();
    net.flush();
    while (net.handleReceives()) { net.flush(); }
    getRemoteDirectory().makeProgress();
    getLocalDirectory().makeProgress();
    net.flush();
    while (net.handleReceives()) { net.flush(); }
  }
}


} // namespace Runtime

} // namespace Galois

template<typename T>
void Galois::Runtime::detail::recvObject(RecvBuffer& buf) {
  fatPointer ptr;
  ResolveFlag flag;
  gDeserialize(buf, ptr, flag);
  if (ptr.isLocal())
    getLocalDirectory().recvObjectImpl(ptr, flag, BaseDirectory::typeHelperImpl<T>::get(), buf);
  else
    getRemoteDirectory().recvObjectImpl(ptr, flag, BaseDirectory::typeHelperImpl<T>::get(), buf);
}

template<typename T>
void Galois::Runtime::detail::recvRequest(RecvBuffer& buf) {
  fatPointer ptr;
  uint32_t dest;
  ResolveFlag flag;
  gDeserialize(buf, ptr, dest, flag);
  if (ptr.isLocal())
    getLocalDirectory().recvRequestImpl(ptr, dest, flag, BaseDirectory::typeHelperImpl<T>::get());
  else
    getRemoteDirectory().recvRequestImpl(ptr, dest, flag);
}

#endif
