/** Dikstra style termination detection -*- C++ -*-
 * @file
 * @section License
 *
 * This file is part of Galois.  Galoisis a gramework to exploit
 * amorphous data-parallelism in irregular programs.
 *
 * Galois is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, version 2.1 of the
 * License.
 *
 * Galois is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Galois.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * @section Copyright
 *
 * Copyright (C) 2015, The University of Texas at Austin. All rights
 * reserved.
 *
 * @section Description
 *
 * Implementation of Dikstra dual-ring Termination Detection
 *
 * @author Andrew Lenharth <andrew@lenharth.org>
 */

#include "Galois/Substrate/Termination.h"

using namespace Galois::Substrate;

namespace {
//Dijkstra style 2-pass ring termination detection
class LocalTerminationDetection : public TerminationDetection {
  struct TokenHolder {
    friend class TerminationDetection;
    std::atomic<long> tokenIsBlack;
    std::atomic<long> hasToken;
    long processIsBlack;
    bool lastWasWhite; // only used by the master
  };

  Galois::Substrate::PerThreadStorage<TokenHolder> data;
  
  unsigned activeThreads;

  //send token onwards
  void propToken(bool isBlack) {
    unsigned id = ThreadPool::getTID();
    TokenHolder& th = *data.getRemote((id + 1) % activeThreads);
    th.tokenIsBlack = isBlack;
    th.hasToken = true;
  }

  void propGlobalTerm() {
    globalTerm = true;
  }

  bool isSysMaster() const {
    return ThreadPool::getTID() == 0;
  }

public:
  LocalTerminationDetection() {}

  void init(unsigned aThreads) {
    activeThreads = aThreads;
  }

  virtual void initializeThread() {
    TokenHolder& th = *data.getLocal();
    th.tokenIsBlack = false;
    th.processIsBlack = true;
    th.lastWasWhite = true;
    globalTerm = false;
    if (isSysMaster())
      th.hasToken = true;
    else 
      th.hasToken = false;
  }

  virtual void localTermination(bool workHappened) {
    assert(!(workHappened && globalTerm.get()));
    TokenHolder& th = *data.getLocal();
    th.processIsBlack |= workHappened;
    if (th.hasToken) {
      if (isSysMaster()) {
	bool failed = th.tokenIsBlack || th.processIsBlack;
	th.tokenIsBlack = th.processIsBlack = false;
	if (th.lastWasWhite && !failed) {
	  //This was the second success
	  propGlobalTerm();
	  return;
	}
	th.lastWasWhite = !failed;
      }
      //Normal thread or recirc by master
      assert (!globalTerm.get() && "no token should be in progress after globalTerm");
      bool taint = th.processIsBlack || th.tokenIsBlack;
      th.processIsBlack = th.tokenIsBlack = false;
      th.hasToken = false;
      propToken(taint);
    }
  }
};

static LocalTerminationDetection& getLocalTermination(unsigned activeThreads) {
  static LocalTerminationDetection term;
  term.init(activeThreads);
  return term;
}


//Dijkstra style 2-pass tree termination detection
class TreeTerminationDetection : public TerminationDetection {
  static const int num = 2;

  struct TokenHolder {
    friend class TerminationDetection;
    //incoming from above
    volatile long down_token;
    //incoming from below
    volatile long up_token[num];
    //my state
    long processIsBlack;
    bool hasToken;
    bool lastWasWhite; // only used by the master
    int parent;
    int parent_offset;
    TokenHolder* child[num];
  };

  PerThreadStorage<TokenHolder> data;

  unsigned activeThreads;

  void processToken() {
    TokenHolder& th = *data.getLocal();
    //int myid = LL::getTID();
    //have all up tokens?
    bool haveAll = th.hasToken;
    bool black = th.processIsBlack;
    for (int i = 0; i < num; ++i) {
      if (th.child[i]) {
	if( th.up_token[i] == -1 )
	  haveAll = false;
	else
	  black |= th.up_token[i];
      }
    }
    //Have the tokens, propagate
    if (haveAll) {
      th.processIsBlack = false;
      th.hasToken = false;
      if (isSysMaster()) {
	if (th.lastWasWhite && !black) {
	  //This was the second success
	  propGlobalTerm();
	  return;
	}
	th.lastWasWhite = !black;
	th.down_token = true;
      } else {
	data.getRemote(th.parent)->up_token[th.parent_offset] = black;
      }
    }

    //recieved a down token, propagate
    if (th.down_token) {
      th.down_token = false;
      th.hasToken = true;
      for (int i = 0; i < num; ++i) {
	th.up_token[i] = -1;
	if (th.child[i])
	  th.child[i]->down_token = true;
      }
    }
  }

  void propGlobalTerm() {
    globalTerm = true;
  }

  bool isSysMaster() const {
    return ThreadPool::getTID() == 0;
  }

public:
  TreeTerminationDetection() {}

  void init(unsigned aThreads) {
    activeThreads = aThreads;
  }

  virtual void initializeThread() {
    TokenHolder& th = *data.getLocal();
    th.down_token = false;
    for (int i = 0; i < num; ++i) 
      th.up_token[i] = false;
    th.processIsBlack = true;
    th.hasToken = false;
    th.lastWasWhite = false;
    globalTerm = false;
    auto tid = ThreadPool::getTID();
    th.parent = (tid - 1) / num;
    th.parent_offset = (tid - 1) % num;
    for (unsigned i = 0; i < num; ++i) {
      unsigned cn = tid * num + i + 1;
      if (cn < activeThreads)
	th.child[i] = data.getRemote(cn);
      else
	th.child[i] = 0;
    }
    if (isSysMaster()) {
      th.down_token = true;
    }
  }

  virtual void localTermination(bool workHappened) {
    assert(!(workHappened && globalTerm.get()));
    TokenHolder& th = *data.getLocal();
    th.processIsBlack |= workHappened;
    processToken();
  }
};

__attribute__((unused))
static TreeTerminationDetection& getTreeTermination(unsigned activeThreads) {
  static TreeTerminationDetection term;
  term.init(activeThreads);
  return term;
}

} // namespace

Galois::Substrate::TerminationDetection& Galois::Substrate::getSystemTermination(unsigned activeThreads) {
  return getLocalTermination(activeThreads);
  //return getTreeTermination();
}

