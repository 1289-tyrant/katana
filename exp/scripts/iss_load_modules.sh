#!/bin/bash

if [ "$(basename -- $0)" == "iss_load_modules.sh" ]; then
  echo "Source this file instead of running directly" >&2
  exit 1
fi

# first up remove everything
module purge

module load sl6
module use /net/faraday/workspace/local/modules/modulefiles
module load lapack
module load vtune

#if [ "$1" == "intel" ]; then
#    module load intel
#    module load cmake
#else 
#    module load gcc/4.8.1-scale
#    module load cmake
#fi

module load atc
module load cmake
module load tbb
module load boost
module load eigen
module load neon
module load subversion
module load git

if [ "$1" != "min" ]; then
  module load gdb
  module load mkl
  module load mpich2
  module load gnuplot
  module load doxygen
  module load texlive
  module load ghostscript
  module load python
  module load screen
  module load valgrind
fi
