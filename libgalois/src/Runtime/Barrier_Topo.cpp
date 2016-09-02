/** Galois barrier -*- C++ -*-
 * @file
 * @section License
 *
 * This file is part of Galois.  Galois is a framework to exploit
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
 * Copyright (C) 2016, The University of Texas at Austin. All rights
 * reserved.
 *
 * @section Description
 *
 * Topologoy Hybrid Shared-MCS Barriers
 *
 * @author Andrew Lenharth <andrew@lenharth.org>
 */

#include "Galois/Runtime/PerThreadStorage.h"
#include "Galois/Runtime/Barrier.h"
#include "Galois/Runtime/CompilerSpecific.h"

#include <atomic>

namespace {

class TopoBarrier : public Galois::Runtime::Barrier {
  struct treenode {
    //vpid is Galois::Runtime::LL::getTID()

    //package binary tree
    treenode* parentpointer; //null of vpid == 0
    treenode* childpointers[2];

    //waiting values:
    unsigned havechild;
    std::atomic<unsigned> childnotready;

    //signal values
    std::atomic<unsigned> parentsense;

  };

  Galois::Runtime::PerPackageStorage<treenode> nodes;
  Galois::Runtime::PerThreadStorage<unsigned> sense;

  void _reinit(unsigned P) {
    auto& tp = Galois::Runtime::ThreadPool::getThreadPool();
    unsigned pkgs = tp.getCumulativeMaxPackage(P-1) + 1;
    for (unsigned i = 0; i < pkgs; ++i) {
      treenode& n = *nodes.getByPkg(i);
      n.childnotready = 0;
      n.havechild = 0;
      for (int j = 0; j < 4; ++j) {
	if ((4*i+j+1) < pkgs) {
	  ++n.childnotready;
          ++n.havechild;
	}
      }
      for (unsigned j = 0; j < P; ++j) {
	if (tp.getPackage(j) == i && !tp.isLeader(j)) {
          ++n.childnotready;
          ++n.havechild;
        }
      }
      n.parentpointer = (i == 0) ? 0 : nodes.getByPkg((i-1)/4);
      n.childpointers[0] = ((2*i + 1) >= pkgs) ? 0 : nodes.getByPkg(2*i+1);
      n.childpointers[1] = ((2*i + 2) >= pkgs) ? 0 : nodes.getByPkg(2*i+2);
      n.parentsense = 0;
    }
    for (unsigned i = 0; i < P; ++i)
      *sense.get(i) = 1;
  }

public:

  TopoBarrier(unsigned v) {
    _reinit(v);
  }

  //not safe if any thread is in wait
  virtual void reinit(unsigned val) {
    _reinit(val);
  }

  virtual void wait() {
    unsigned id = Galois::Runtime::ThreadPool::getTID();
    treenode& n = *nodes;
    unsigned& s = *sense;
    bool leader = Galois::Runtime::ThreadPool::isLeader();
    //completion tree
    if (leader) {
      while (n.childnotready) { Galois::Runtime::asmPause(); }
      n.childnotready = n.havechild;
      if (n.parentpointer) {
	--n.parentpointer->childnotready;
      }
    } else {
      --n.childnotready;
    }
    
    //wait for signal
    if (id != 0) {
      while (n.parentsense != s) {
	Galois::Runtime::asmPause();
      }
    }
    
    //signal children in wakeup tree
    if (leader) {
      if (n.childpointers[0])
	n.childpointers[0]->parentsense = s;
      if (n.childpointers[1])
	n.childpointers[1]->parentsense = s;
      if (id == 0)
	n.parentsense = s;
    }
    ++s;
  }

  virtual const char* name() const { return "TopoBarrier"; }

};

}

std::unique_ptr<Galois::Runtime::Barrier> Galois::Runtime::createBarrier(unsigned activeThreads) {
  return std::unique_ptr<Barrier>(new TopoBarrier(activeThreads));
}

