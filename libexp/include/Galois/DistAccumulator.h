/** Distributed Accumulator type -*- C++ -*-
 * @file
 * @section License
 *
 * This file is part of Galois.  Galoisis a framework to exploit
 * amorphous data-parallelism in irregular programs.
 *
 * Galois is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
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
 * @author Gurbinder Gill <gurbinder533@gmail.com>
 */

#ifndef GALOIS_DISTACCUMULATOR_H
#define GALOIS_DISTACCUMULATOR_H

#include <limits>
#include "Galois/Galois.h"

#ifdef __GALOIS_HET_OPENCL__
#include "Galois/OpenCL/CL_Header.h"
#endif

namespace Galois {

template<typename Ty>
class DGAccumulator {
   Galois::Runtime::NetworkInterface& net = Galois::Runtime::getSystemNetworkInterface();

   std::atomic<Ty> mdata;
   static Ty others_mdata;
   static unsigned num_Hosts_recvd;
#ifdef __GALOIS_HET_OPENCL__
   cl_mem dev_data;
#endif

public:
   //Default constructor
   DGAccumulator(){
#ifdef __GALOIS_HET_OPENCL__
      Galois::OpenCL::CLContext * ctx = Galois::OpenCL::getCLContext();
      cl_int err;
      dev_data= clCreateBuffer(ctx->get_default_device()->context(), CL_MEM_READ_WRITE, sizeof(Ty) , nullptr, &err);
      Galois::OpenCL::CHECK_CL_ERROR(err, "Error allocating DGAccumulator!\n");
      Ty val = 0;
      cl_command_queue queue = ctx->get_default_device()->command_queue();
      err = clEnqueueWriteBuffer(queue, dev_data, CL_TRUE, 0, sizeof(Ty), &val, 0, NULL, NULL);
      Galois::OpenCL::CHECK_CL_ERROR(err, "Error Writing DGAccumulator!\n");
#endif
   }
   DGAccumulator& operator+=(const Ty& rhs) {
      Galois::atomicAdd(mdata, rhs);
      return *this;
   }
   /************************************************************
    *
    ************************************************************/
   void operator=(const Ty rhs) {
      mdata.store(rhs);
#ifdef __GALOIS_HET_OPENCL__
      int err;
      Galois::OpenCL::CLContext * ctx = Galois::OpenCL::getCLContext();
      cl_command_queue queue = ctx->get_default_device()->command_queue();
      Ty val = mdata.load();
      err = clEnqueueWriteBuffer(queue, dev_data, CL_TRUE, 0, sizeof(Ty), &val, 0, NULL, NULL);
      Galois::OpenCL::CHECK_CL_ERROR(err, "Error Writing DGAccumulator!\n");
#endif

   }
   /************************************************************
    *
    ************************************************************/

   void set(const Ty rhs) {
      mdata.store(rhs);
#ifdef __GALOIS_HET_OPENCL__
      int err;
      Galois::OpenCL::CLContext * ctx = Galois::OpenCL::getCLContext();
      cl_command_queue queue = ctx->get_default_device()->command_queue();
      err = clEnqueueWriteBuffer(queue, dev_data, CL_TRUE, 0, sizeof(Ty), &mdata.load(), 0, NULL, NULL);
      Galois::OpenCL::CHECK_CL_ERROR(err, "Error writing DGAccumulator!\n");
#endif

   }
   
   Ty read() {
     return mdata.load();
   }

   /************************************************************
    *
    ************************************************************/
  static void reduce_landingPad(uint32_t src, Galois::Runtime::RecvBuffer& buf) {
      uint32_t x_id;
      Ty x_mdata;
      gDeserialize(buf, x_id, x_mdata);
      others_mdata += x_mdata;
      ++num_Hosts_recvd;
   }

   /************************************************************
    *
    ************************************************************/
   Ty reduce() {
#ifdef __GALOIS_HET_OPENCL__
      Ty tmp;
      Galois::OpenCL::CLContext * ctx = Galois::OpenCL::getCLContext();
      cl_int err = clEnqueueReadBuffer(ctx->get_default_device()->command_queue(), dev_data, CL_TRUE, 0, sizeof(Ty), &tmp, 0, NULL, NULL);
//      fprintf(stderr, "READ-DGA[%d, %d]\n", Galois::Runtime::NetworkInterface::ID, tmp);
      Galois::OpenCL::CHECK_CL_ERROR(err, "Error reading DGAccumulator!\n");
      Galois::atomicAdd(mdata, tmp);
#endif
      for (auto x = 0; x < net.Num; ++x) {
         if (x == net.ID)
            continue;
         Galois::Runtime::SendBuffer b;
         gSerialize(b, net.ID, mdata);
         net.sendMsg(x, reduce_landingPad, b);
      }

      net.flush();
      while (num_Hosts_recvd < (net.Num - 1)) {
         net.handleReceives();
      }
      Galois::Runtime::getHostBarrier().wait();

      Galois::atomicAdd(mdata, others_mdata);
      others_mdata = 0;
      num_Hosts_recvd = 0;
      return mdata;
   }
   /************************************************************
       *
       ************************************************************/
#ifdef __GALOIS_HET_OPENCL__
   const cl_mem &device_ptr(){
      reset();
      return dev_data;
   }
#endif
   /************************************************************
    *
    ************************************************************/
   Ty reset() {
      Ty retval = mdata.exchange(0);
#ifdef __GALOIS_HET_OPENCL__
      int err;
      Ty val = mdata.load();
      Galois::OpenCL::CLContext * ctx = Galois::OpenCL::getCLContext();
      cl_command_queue queue = ctx->get_default_device()->command_queue();
      err = clEnqueueWriteBuffer(queue, dev_data, CL_TRUE, 0, sizeof(Ty), &val, 0, NULL, NULL);
      Galois::OpenCL::CHECK_CL_ERROR(err, "Error writing (reset) DGAccumulator!\n");
//      fprintf(stderr, "RESET-DGA[%d, %d]\n", Galois::Runtime::NetworkInterface::ID, val);
#endif
      return retval;
   }

};

template<typename Ty>
Ty DGAccumulator<Ty>::others_mdata;

template<typename Ty>
unsigned DGAccumulator<Ty>::num_Hosts_recvd = 0;
}
#endif
