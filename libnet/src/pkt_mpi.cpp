/** Galois Network Backend for MPI -*- C++ -*-
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

#include "Galois/Runtime/NetworkIO.h"
#include "hash/crc32.h"
#include "Galois/Runtime/Tracer.h"
#include "Galois/Substrate/SimpleLock.h"

#include <cassert>
#include <cstring>
#include <mpi.h>
#include <deque>
#include <iostream>
#include <string>
#include <fstream>
#include <unistd.h>
#include <vector>

static const bool debugMPI = true;
static const bool debugPrint  = true;

class NetworkIOMPI : public Galois::Runtime::NetworkIO {

  static void handleError(int rc) {
    if (rc != MPI_SUCCESS) {
      //GALOIS_ERROR(false, "MPI ERROR"); 
      MPI_Abort(MPI_COMM_WORLD, rc);
    }
  }

  static int getID() {
    int taskRank;
    handleError(MPI_Comm_rank(MPI_COMM_WORLD, &taskRank));
    return taskRank;
  }

  static int getNum() {
    int numTasks;
    handleError(MPI_Comm_size(MPI_COMM_WORLD, &numTasks));
    return numTasks;
  }
  
  std::pair<int, int> initMPI() {
    int provided;
    handleError(MPI_Init_thread (NULL, NULL, MPI_THREAD_FUNNELED, &provided));
    assert(provided >= MPI_THREAD_FUNNELED);
    return std::make_pair(getID(), getNum());
  }

  struct mpiMessage {
    message m;
    MPI_Request req;
    mpiMessage(message&& _m, MPI_Request _req) : m(std::move(_m)), req(_req) {}
    mpiMessage(uint32_t host, std::unique_ptr<uint8_t[]>&& data, size_t len) :m{host, std::move(data), len} {}
  };

  struct sendQueueTy {
    std::deque<mpiMessage> inflight;

    void complete() {
      if (!inflight.empty()) {
        int flag = 0;
        MPI_Status status;
        auto& f = inflight.front();
        int rv = MPI_Test(&f.req, &flag, &status);
        handleError(rv);
        if (flag) {
          if (debugMPI) {
            uint32_t h;
            memcpy(&h, f.m.data.get() + (f.m.len - 4), 4);
            assert(h == CRC32::hash(f.m.data.get(), f.m.len - 4));
            if (debugPrint)
              std::cerr << getNum() << " C " << std::hex << (uintptr_t)f.m.data.get() << " " << CRC32::hash(f.m.data.get(), f.m.len - 4) << std::dec << " " << f.m.len << "\n";
          }
          inflight.pop_front();
        }
      }
    }

    void send(message m) {
      //if debugging, make a hash value
      if (debugMPI) {
        uint8_t* data = new uint8_t[m.len + 4];
        memcpy(data, m.data.get(), m.len);
        uint32_t hash = CRC32::hash(data, m.len);
        memcpy(data+m.len, &hash, 4);
        m.len += 4;
        m.data.reset(data);
        if (debugPrint)
          std::cerr << getNum() << " S " << std::hex <<  (uintptr_t)m.data.get()  << " " << hash << std::dec << " " << m.len << "\n";
      }
      MPI_Request req;
      inflight.emplace_back(std::move(m), req);
      auto& f = inflight.back();
      int rv = MPI_Isend(f.m.data.get(), f.m.len, MPI_BYTE, f.m.host, 0, MPI_COMM_WORLD, &f.req);
      //inflight.emplace_back(std::move(m), req);

      //std::vector<uint8_t> send_vec(ptr, ptr + 10);
      //Galois::Runtime::print_send(send_vec, m.len, m.host);

      Galois::Runtime::trace("MPI_SEND: to % len % data: %", m.host, m.len, m.data.get());
      //      std::cerr << "s";
      handleError(rv);
    }
  };

  struct recvQueueTy {
    std::deque<mpiMessage> done;

    void probe() {
      int flag = 0;
      MPI_Status status;
      int rv = MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &flag, &status);
      handleError(rv);
      if (flag) {
        int nbytes;
        rv = MPI_Get_count(&status, MPI_BYTE, &nbytes);
        handleError(rv);
        std::unique_ptr<uint8_t[]> ptr{new uint8_t[nbytes]};
        rv = MPI_Recv(ptr.get(), nbytes, MPI_BYTE, status.MPI_SOURCE, status.MPI_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        handleError(rv);
        if (debugMPI) {
          uint32_t h;
          memcpy(&h, ptr.get() + (nbytes - 4), 4);
          if (debugPrint)
            std::cerr << getNum() << " R " << std::hex << (uintptr_t)ptr.get() << " " << CRC32::hash(ptr.get(), nbytes - 4) << " " << h << std::dec << " " << nbytes << "\n";
          assert(h == CRC32::hash(ptr.get(), nbytes - 4));
          nbytes -= 4;
        }

        //auto* ptr_data = ptr.get();
                //std::cerr <<"\n" <<_ID << " mpi_recv " << nbytes << " " << status.MPI_SOURCE << "\n";
        done.emplace_back(status.MPI_SOURCE, std::move(ptr), nbytes);

        //std::vector<uint8_t> recv_vec(ptr_data, ptr_data + 10);
        //Galois::Runtime::print_recv(recv_vec, nbytes, status.MPI_SOURCE);
      }
    }
  };

  sendQueueTy sendQueue;
  recvQueueTy recvQueue;


public:

  NetworkIOMPI(uint32_t& ID, uint32_t& NUM) {
    auto p = initMPI();
    ID = p.first;
    NUM = p.second;
  }

  ~NetworkIOMPI() {
    int rv = MPI_Finalize();
    handleError(rv);
  }
  
  virtual void enqueue(message m) {
    sendQueue.send(std::move(m));
  }

  virtual message dequeue() {
    if (!recvQueue.done.empty()) {
      auto& msg = recvQueue.done.front();
      message retval(std::move(msg.m));
      recvQueue.done.pop_front();
      return retval;
    }
    return message{};
  }

  virtual void progress() {
    sendQueue.complete();
    recvQueue.probe();
  }

};

std::tuple<std::unique_ptr<Galois::Runtime::NetworkIO>,uint32_t,uint32_t> Galois::Runtime::makeNetworkIOMPI() {
  uint32_t ID, NUM;
  std::unique_ptr<Galois::Runtime::NetworkIO> n{new NetworkIOMPI(ID, NUM)};
  return std::make_tuple(std::move(n), ID, NUM);
}

