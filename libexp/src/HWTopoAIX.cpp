/** Machine Descriptions on Linux -*- C++ -*-
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
 * Copyright (C) 2015, The University of Texas at Austin. All rights
 * reserved.
 *
 * @section Description
 *
 * See HWTopoLinux.cpp.
 *
 * @author Donald Nguyen <ddn@cs.utexas.edu>
 */
#include "Galois/Runtime/ll/HWTopo.h"
#include "Galois/Runtime/ll/EnvCheck.h"
#include "Galois/Runtime/ll/gio.h"

#include <vector>
#include <cerrno>
#include <cassert>
#include <algorithm>
#include <numeric>
#include <boost/iterator/counting_iterator.hpp>
#include <string.h>
#include <sys/rset.h>
#include <sys/processor.h>
#include <sys/thread.h>

using namespace Galois::Runtime::LL;

namespace {

static bool bindToProcessor(int proc) {
  tid_t tid = thread_self();
  if (bindprocessor(BINDTHREAD, tid, proc)) {
    gWarn("Could not set CPU affinity for thread ", proc, "(", strerror(errno), ")");
    return false;
  }
  return true;
}

struct Policy {
  //! number of hardware threads
  int numThreads;
  //! number of "real" processors
  int numCores;
  //! number of packages
  int numPackages;

  std::vector<int> packages;
  std::vector<int> maxPackage;
  std::vector<int> virtmap;
  std::vector<int> leaders;

  /**
   * Raw data from rs_info is a set of mappings for various levels of
   * of processor tree. For example, for two dual-core processors with
   * 2-way SMT (note that processor 3 is not mapped):
   * <pre>
   * levels[0] = [ 0, 1, 2, 4, 5, 6, 7 ]  "AIX logical processor"
   * levels[2] = [ 0, 0, 1, 2, 2, 3, 3 ]  "Core"
   * levels[4] = [ 0, 0, 0, 1, 1, 1, 1 ]  "Package"
   *
   * levels[1] = [ 0, 1, 0, 0, 1, 0, 1 ]  "Core siblings" (i.e., SMT)
   * levels[3] = [ 0, 1, 2, 0, 1, 2, 3 ]  "Package siblings"
   * </pre>
   *
   * The sibling levels are just an alternate indexing to simplify mapping operations.
   */
  typedef std::vector<std::vector<int>> Levels;
  Levels levels;

  struct SortedByThread {
    Levels& l;
    SortedByThread(Levels& l): l(l) { }

    bool operator()(int a, int b) {
      // Only use SMT threads after all the cores have been used
      if (l[1][a] != l[1][b])
        return l[1][a] < l[1][b];
      if (l[2][a] != l[2][b])
        return l[2][a] < l[2][b];
      return l[0][a] < l[0][b];
    }
  };

  void parse() {
    int maxProcs = rs_getinfo(NULL, R_MAXPROCS, 0);
    for (int i = 0; i < 5; ++i)
      levels.emplace_back(maxProcs, -1);   

    // SMP level
    int sdl;
    if ((sdl = rs_getinfo(NULL, R_SMPSDL, 0)) == 0)
      GALOIS_DIE("rs_getinfo failed");
    numThreads = populate(sdl, levels[0], false);
    for (int i = 0; i < maxProcs; ++i) {
      if (levels[0][i] != -1)
        levels[0][i] = i;
    }

    // NUMA level
    if ((sdl = rs_getinfo(NULL, R_MCMSDL, 0)) == 0)
      return;
    populate(sdl, levels[4], false);
    populate(sdl, levels[3], true);
    numPackages = *std::max_element(levels[4].begin(), levels[4].end()) + 1;

    // Search for SDL for core because it doesn't have an explicit name
    int maxSdl = rs_getinfo(NULL, R_MAXSDL, 0);
    for (sdl = 0; sdl <= maxSdl; ++sdl) {
      if (sdl == rs_getinfo(NULL, R_SMPSDL, 0))
        continue;
      if (sdl == rs_getinfo(NULL, R_MCMSDL, 0))
        continue;
      populate(sdl, levels[2], false);
      int v = *std::max_element(levels[2].begin(), levels[2].end()) + 1;
      if (numPackages < v && v < numThreads) {
        numCores = v;
        populate(sdl, levels[1], true);
        break;
      }
    }
  }

  //! Fill in level vector, returns the number of entries set 
  int populate(int sdl, std::vector<int>& l, bool siblingsHaveDifferentNames) {
    // Get resources for current logical partition
    rsethandle_t rset = rs_alloc(RS_PARTITION);
    rsethandle_t rad = rs_alloc(RS_EMPTY);

    int sumNumProcs = 0;
    int numRads = rs_numrads(rset, sdl, 0);
    for (int rindex = 0; rindex < numRads; ++rindex) {
      if (rs_getrad(rset, rad, sdl, rindex, 0))
        GALOIS_SYS_DIE("rs_getrad() failed");
      sumNumProcs += rs_getinfo(rad, R_NUMPROCS, 0);
      int maxCpus = rs_getinfo(rad, R_MAXPROCS, 0);
      int id = 0;
      for (int i = 0; i < maxCpus; ++i) {
        if (rs_op(RS_TESTRESOURCE, rad, NULL, R_PROCS, i)) {
          if (siblingsHaveDifferentNames)
            l[i] = id++;
          else
	    l[i] = rindex;
        }
      }
    }
    rs_free(rad);
    rs_free(rset);

    return sumNumProcs;
  }

  void generateUniformLevel(int l) {
    int id = 0;
    for (unsigned i = 0; i < levels[0].size(); ++i) {
      if (levels[0][i] >= 0) {
        levels[l][i] = id++;
        levels[l+1][i] = 0;
      }
    }
  }
 
  void generate() {
    if (numCores == -1) {
      // Assume each thread is a core
      generateUniformLevel(1);
      numCores = numThreads;
    }
    if (numPackages == -1) {
      // Assume one package
      generateUniformLevel(3);
      numPackages = 1;
    }

    // Remove unmapped processors
    for (unsigned i = 0; i < levels.size(); ++i) {
      auto isMapped = [](int x) { return x >= 0; };
      auto it = std::stable_partition(levels[i].begin(), levels[i].end(), isMapped);
      levels[i].resize(std::distance(levels[i].begin(), it));
    }
    
    // Generate mappings
    std::vector<int> lthreads(numThreads);
    std::copy(boost::counting_iterator<int>(0), boost::counting_iterator<int>(numThreads), lthreads.begin());
    std::sort(lthreads.begin(), lthreads.end(), SortedByThread(levels));

    virtmap.resize(numThreads);
    leaders.resize(numPackages);
    packages.resize(numThreads);
    for (int gid = 0; gid < numThreads; ++gid) {
      int lid = lthreads[gid];
      virtmap[gid] = levels[0][lid];
      packages[gid] = levels[4][lid];
      if (levels[3][lid] == 0) 
        leaders[packages[gid]] = gid;
    }
    maxPackage.resize(numThreads);
    std::partial_sum(packages.begin(), packages.end(), maxPackage.begin(), [](int a, int b) { return std::max(a, b); });
  }

  Policy(): numThreads(-1), numCores(-1), numPackages(-1) {
    parse();
    generate();
    if (EnvCheck("GALOIS_DEBUG_TOPO"))
      printConfiguration();
    levels.clear();
  }

  void printConfiguration() {
    for (unsigned level = 0; level < levels.size(); ++level) {
      gPrint("levels[", level, "] = [ ");
      for (int i = 0; i < numThreads; ++i) {
        gPrint(levels[level][i], " ");
      }
      gPrint("]\n");
    }

    gPrint("Threads: ", numThreads, "\n");
    gPrint("Cores: ", numCores, "\n");
    gPrint("Packages: ", numPackages, "\n");

    for (int i = 0; i < (int) virtmap.size(); ++i) {
      gPrint(
          "T ", i, 
          " P ", packages[i],
          " Tr ", virtmap[i], 
          " L? ", (i == leaders[packages[i]] ? 1 : 0));
      if (i >= numCores)
        gPrint(" HT");
      gPrint("\n");
    }
  }
};

static Policy& getPolicy() {
  static Policy A;
  return A;
}

} //namespace

bool Galois::Runtime::LL::bindThreadToProcessor(int id) {
  assert(size_t(id) < getPolicy().virtmap.size());
  return bindToProcessor(getPolicy().virtmap[id]);
}

unsigned Galois::Runtime::LL::getProcessorForThread(int id) {
  assert(size_t(id) < getPolicy().virtmap.size());
  return getPolicy().virtmap[id];
}

unsigned Galois::Runtime::LL::getMaxThreads() {
  return getPolicy().numThreads;
}

unsigned Galois::Runtime::LL::getMaxCores() {
  return getPolicy().numCores;
}

unsigned Galois::Runtime::LL::getMaxPackages() {
  return getPolicy().numPackages;
}

unsigned Galois::Runtime::LL::getPackageForThread(int id) {
  assert(size_t(id) < getPolicy().packages.size());
  return getPolicy().packages[id];
}

unsigned Galois::Runtime::LL::getMaxPackageForThread(int id) {
  assert(size_t(id) < getPolicy().maxPackage.size());
  return getPolicy().maxPackage[id];
}

bool Galois::Runtime::LL::isPackageLeader(int id) {
  assert(size_t(id) < getPolicy().packages.size());
  return getPolicy().leaders[getPolicy().packages[id]] == id;
}

unsigned Galois::Runtime::LL::getLeaderForThread(int id) {
  assert(size_t(id) < getPolicy().packages.size());
  return getPolicy().leaders[getPolicy().packages[id]];
}

unsigned Galois::Runtime::LL::getLeaderForPackage(int id) {
  assert(size_t(id) < getPolicy().leaders.size());
  return getPolicy().leaders[id];
}
