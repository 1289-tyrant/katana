/** Parameter -*- C++ -*-
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
 */

#include "galois/runtime/Executor_ParaMeter.h"
#include "galois/gIO.h"

namespace galois {
  namespace runtime {

namespace cll = llvm::cl;
cll::opt<std::string> paraMeterOutFileOpt ("ParaMeterOut", cll::desc ("output csv stats file for ParaMeter"), cll::init("ParaMeter_Stats.csv"));
cll::opt<bool> useParaMeterOpt ("useParaMeter", cll::desc ("enable ParaMeter to measure available parallelism"), cll::init(false));
  } 
}

struct StatsFileManager {
  static const unsigned FNAME_SIZE = 256;

  bool init = false;
  bool isOpen = false;
  FILE* statsFH = nullptr;
  // char statsFileName[FNAME_SIZE];
  std::string statsFileName;


  ~StatsFileManager (void) {
    close ();
  }

  FILE* get (void) {
    if (!init) {
      init = true;

      statsFileName = galois::runtime::paraMeterOutFileOpt;
      // time_t rawtime;
      // struct tm* timeinfo;
// 
      // time(&rawtime);
      // timeinfo = localtime(&rawtime);
// 
      // strftime(statsFileName, FNAME_SIZE, "ParaMeter_Stats_%Y-%m-%d_%H:%M:%S.csv", timeinfo);

      statsFH = fopen(statsFileName.c_str(), "w");
      GALOIS_ASSERT (statsFH != nullptr, "ParaMeter stats file error");

      galois::runtime::ParaMeter::StepStats::printHeader(statsFH);

      fclose(statsFH);

    }

    if (!isOpen) {
      statsFH = fopen (statsFileName.c_str(), "a"); // open in append mode
      GALOIS_ASSERT (statsFH != nullptr, "ParaMeter stats file error");

      isOpen = true;
    }

    return statsFH;

  }

  void close (void) {
    if (isOpen) {
      fclose (statsFH);
      isOpen = false;
      statsFH = nullptr;
    }
  }

};

static StatsFileManager& getStatsFileManager (void) {
  static StatsFileManager s;
  return s;
}

FILE* galois::runtime::ParaMeter::getStatsFile (void) {
  return getStatsFileManager ().get ();
}

void galois::runtime::ParaMeter::closeStatsFile (void) {
  getStatsFileManager ().close ();
}
