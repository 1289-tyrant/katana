/** Enviroment Checking Code -*- C++ -*-
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
 * @author Andrew Lenharth <andrew@lenharth.org>
 */

#include "Galois/Substrate/EnvCheck.h"

#include <cstdlib>

bool Galois::Substrate::EnvCheck(const char* parm) {
  if (getenv(parm))
    return true;
  return false;
}

bool Galois::Substrate::EnvCheck(const char* parm, int& val) {
  char* t = getenv(parm);
  if (t) {
    val = atoi(t);
    return true;
  }
  return false;
}
