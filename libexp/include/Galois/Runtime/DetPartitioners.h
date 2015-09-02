#ifndef GALOIS_RUNTIME_DET_PARTITIONERS_H
#define GALOIS_RUNTIME_DET_PARTITIONERS_H

namespace Galois {
namespace Runtime {

template <typename G, typename M>
struct GreedyPartitioner {

  using ParCounter = Galois::GAccumulator<size_t>;
  using PartCounters = std::vector<ParCounter>;
  using GNode = typename G::GraphNode;

  static constexpr size_t SIZE_LIM_MULT = 2;

  G& graph;
  M& dagManager;
  const unsigned numPart;


  struct NborStat {
    unsigned partition;
    unsigned count;
  };

  PartCounters  partSizes;
  const size_t perThrdSizeLim;
  Galois::PerThreadVector<NborStat> perThrdNborStats;
  Galois::PerThreadVector<size_t> perThrdPartSizes;


  GreedyPartitioner (
      G& graph,
      M& dagManager,
      const unsigned numPart)
    :
      graph (graph),
      dagManager (dagManager),
      numPart (numPart),
      partSizes (numPart),
      perThrdSizeLim ((graph.size () + numPart)/ (Galois::getActiveThreads () * numPart))
  {
    for (unsigned i = 0; i < perThrdNborStats.numRows (); ++i) {
      perThrdNborStats.get (i).clear ();
      perThrdNborStats.get (i).resize (numPart, NborStat{0, 0});
      perThrdPartSizes.get (i).resize (numPart, 0);
    }
  }

  template <typename R>
  void blockStart (const R& range) {

          size_t lim = std::distance(range.begin (), range.end ()) / numPart;

    on_each_impl (
        [this, &range, &lim] (const unsigned tid, const unsigned numT) {
          unsigned partPerThread = (numPart + numT - 1) / numT;
          unsigned pbeg = tid * partPerThread;


          size_t size = 0;
          unsigned currP = pbeg;
          for (auto i = range.local_begin (), end_i = range.local_end ();
            i != end_i; ++i) {

            auto& nd = graph.getData (*i, Galois::MethodFlag::UNPROTECTED);
            GALOIS_ASSERT (nd.partition == -1);

            nd.partition = currP;
            ++size;

            if (size >= lim) {
              ++currP;
              if (currP >= numPart) {
                currP = pbeg;
              }
              size = 0;
            }
          }
        });

  }

  template <typename R>
  void cyclicStart (const R& range) {
    on_each_impl (
        [this, &range] (const unsigned tid, const unsigned numT) {
          int counter = 0;
          for (auto i = range.local_begin (), end_i = range.local_end ();
            i != end_i; ++i) {

            auto& nd = graph.getData (*i, Galois::MethodFlag::UNPROTECTED);
            GALOIS_ASSERT (nd.partition == -1);

            nd.partition = counter % numPart;
            ++counter;
          }
        });
  }

  void assignPartition (GNode src) {

    auto& sd = graph.getData (src, Galois::MethodFlag::UNPROTECTED);

    if (sd.partition != -1) {
      return;
    }

    auto& nborStats = perThrdNborStats.get ();
    assert (nborStats.size () == numPart);

    for (unsigned i = 0; i < nborStats.size (); ++i) {
      nborStats[i] = NborStat {i, 0};
      assert (nborStats[i].partition == i);
      assert (nborStats[i].count == 0);
    }

    // std::fill (nborStats.begin (), nborStats.end (), 0);

    auto adjClosure = [this, &nborStats, &sd] (GNode dst) {
      auto& dd = graph.getData (dst, MethodFlag::UNPROTECTED);

      if (dd.partition != -1) {
        assert (size_t (dd.partition) < nborStats.size ());
        nborStats[dd.partition].count += 1;
      }
    };

    dagManager.applyToAdj (src, adjClosure);

    // auto maxIndex = std::max_element (nborStats.begin (), nborStats.end ());
    // assert (std::distance (nborStats.begin (), maxIndex) >= 0);
    // assert (maxIndex != nborStats.end ());
    // unsigned maxPart = maxIndex - nborStats.begin ();
    // sd.partition = maxPart;

    auto sortFunc = [] (const NborStat& left, const NborStat& right) -> bool {
      return left.count < right.count; // sort
    };

    std::sort (nborStats.begin (), nborStats.end (), sortFunc);
    std::reverse (nborStats.begin (), nborStats.end ());

    // check sort, for debug only
    for (unsigned i = 1; i < nborStats.size (); ++i) {
      assert (nborStats[i].count <= nborStats[i-1].count);
    }

    bool success = false;
    for (const NborStat& ns: nborStats) {
      if (false || perThrdPartSizes.get ()[ns.partition] < (SIZE_LIM_MULT * perThrdSizeLim)) {
        sd.partition = ns.partition;
        perThrdPartSizes.get ()[sd.partition] += 1;
        success = true;
        break;
      }
    }

    GALOIS_ASSERT (success);


  }


  void partition (void) {

    Galois::StatTimer ptime ("partition time");
    ptime.start ();


    Galois::PerThreadBag<GNode, 64> sources;

    dagManager.initDAG ();
    dagManager.collectSources (sources);

    cyclicStart (makeLocalRange (sources));
    // blockStart (makeLocalRange (sources));


    auto f = [this] (GNode src) { this->assignPartition (src); };
    dagManager.runDAGcomputation (f, sources, "greedy-partition");
  }


  template <typename A>
  void initCoarseAdj (A& adjMatrix) {

    Galois::StatTimer t("time initCoarseAdj");

    t.start ();

    PartCounters partBoundarySizes (numPart);
    for (ParCounter& c: partSizes) {
      c.reset ();
    }

    do_all_coupled (makeLocalRange (graph),
        [this, &partBoundarySizes, &adjMatrix] (GNode src) {

          bool boundary = false;

          auto& sd = graph.getData (src, Galois::MethodFlag::UNPROTECTED);

          GALOIS_ASSERT (sd.partition != -1);

          partSizes[sd.partition] += 1;

          auto visitAdjClosure = [this, &sd, &boundary, &adjMatrix] (GNode dst) {
            auto& dd = graph.getData (dst, Galois::MethodFlag::UNPROTECTED);

            GALOIS_ASSERT (dd.partition != -1);
            if (dd.partition != sd.partition) {
              boundary = true;

              if (adjMatrix [sd.partition][dd.partition] != 1) {
                adjMatrix [sd.partition][dd.partition] = 1;
              }

              if (adjMatrix [dd.partition][sd.partition] != 1) {
                adjMatrix [dd.partition][sd.partition] = 1;
              }
            }
          };


          dagManager.applyToAdj (src, visitAdjClosure);
          sd.isBoundary = boundary;

          if (boundary) {
            partBoundarySizes[sd.partition] += 1;
          }

          // GALOIS_ASSERT (sd.isBoundary == boundary);
         
        },
        "check_partitions");

    for (size_t i = 0; i < numPart; ++i) {
      size_t total = partSizes[i].reduceRO ();
      size_t boundary = partBoundarySizes[i].reduceRO ();
      assert (total >= boundary);
      size_t inner = total - boundary;
      std::printf ("partition %zd, size =%zd, boundary=%zd, inner=%zd\n"
          , i, total, boundary, inner);
    }
    
    t.stop ();
  }


};

template <typename G, typename M>
struct BFSpartitioner {
  using ParCounter = Galois::GAccumulator<size_t>;
  using PartCounters = std::vector<ParCounter>;
  using GNode = typename G::GraphNode;

  G& graph;
  M& dagManager;
  const unsigned numPart;

  PartCounters  partSizes;
  const size_t partSizeLim;

  BFSpartitioner (
      G& graph,
      M& dagManager,
      const unsigned numPart)
    :
      graph (graph),
      dagManager (dagManager),
      numPart (numPart),
      partSizes (numPart),
      partSizeLim ((graph.size () + numPart)/numPart)
  {}

  template <typename R, typename B>
  void pickSources (const R& range, const size_t rangeSize, B& sources) {

    ptrdiff_t jump_size = std::max (rangeSize /(numPart), size_t (1));

    auto iter = range.begin ();
    size_t dist = 0;

    for (unsigned i = 0; i < numPart && dist < rangeSize; ++i) {


      if (partSizes[i].reduceRO () < partSizeLim) {
        auto& nd = graph.getData (*iter, Galois::MethodFlag::UNPROTECTED);
        nd.partition = i;
        partSizes [i] += 1;

        sources.push_back (*iter);
        std::advance (iter, jump_size);
        dist += jump_size;
      }
    }
  }

  template <typename I>
  void bfsTraversal (I beg, I end) {

    using WL = Galois::WorkList::dChunkedFIFO<32>;
    // using WL = Galois::WorkList::AltChunkedFIFO<8>;

    Galois::for_each (beg, end,
        [this] (GNode src, Galois::UserContext<GNode>& ctxt) {

          auto& sd = graph.getData (src, Galois::MethodFlag::UNPROTECTED);
          GALOIS_ASSERT (sd.partition != -1);
          size_t psize = partSizes[sd.partition].reduceRO ();

          // bool addMore = (psize < partSizeLim); 
          bool addMore = true;

          if (addMore) {
            auto addMoreClosure = [this, &sd, &ctxt] (GNode dst) {
              auto& dd = graph.getData (dst, Galois::MethodFlag::UNPROTECTED);

              if (dd.partition == -1) {
                dd.partition = sd.partition;
                partSizes[dd.partition] += 1;
                ctxt.push (dst);
              }
            };

            dagManager.applyToAdj (src, addMoreClosure, Galois::MethodFlag::UNPROTECTED);
          }

          
        },
        Galois::loopname ("partition_bfs"),
        Galois::wl<WL> ());
  }

  template <typename R, typename W>
  void filterUnpartitioned (const R& range, W& unpartitioned) {

    assert (unpartitioned.empty_all ());
    Galois::Runtime::do_all_coupled (range,
        [this, &unpartitioned] (GNode n) {
          auto& nd = graph.getData (n, Galois::MethodFlag::UNPROTECTED);
          if (nd.partition == -1) {
            unpartitioned.push_back (n);
          }
        },
        "find-unpartitioned");
  }

  void partition (void) {

    Galois::StatTimer ptime ("partition time");
    ptime.start ();

    Galois::PerThreadBag<GNode, 64>* currRemaining = new PerThreadBag<GNode, 64> ();
    Galois::PerThreadBag<GNode, 64>* nextRemaining = new PerThreadBag<GNode, 64> ();

    Galois::gdeque<GNode, 64> sources;

    bool first = true;
    unsigned rounds = 0;

    do {
      ++rounds;

      sources.clear ();
      if (first) {
        pickSources (makeLocalRange (graph), graph.size (),  sources);

      } else {
        pickSources (makeLocalRange (*currRemaining), currRemaining->size_all (), sources);
      }

      bfsTraversal (sources.begin (), sources.end ());

      if (first) {
        first = false;
        filterUnpartitioned (makeLocalRange (graph), *nextRemaining);

      } else {
        filterUnpartitioned (makeLocalRange (*currRemaining), *nextRemaining);
      }

      std::swap (currRemaining, nextRemaining);
      nextRemaining->clear_all_parallel ();


    } while (!currRemaining->empty_all ());

    std::printf ("BFSpartitioner partitioned in %d rounds\n", rounds);

    delete currRemaining; currRemaining = nullptr;
    delete nextRemaining; nextRemaining = nullptr;

    ptime.stop ();

  }

  unsigned countComponents (void) {

    Galois::StatTimer cctime ("countComponents time");
    cctime.start ();

    std::vector<unsigned> componentIDs (graph.size (), 0);
    
    auto nextSource = graph.begin ();

    unsigned numComp = 0;

    while (nextSource != graph.end ()) {

      ++numComp;
      // run BFS

      componentIDs[*nextSource] = numComp;
      GNode init[] = { *nextSource };
      Galois::for_each ( &init[0], &init[1], 
          [this, &componentIDs, &numComp] (GNode n, Galois::UserContext<GNode>& ctxt) {

            assert (componentIDs[n] != 0);

            auto visitAdjClosure = [this, &numComp, &componentIDs, &ctxt] (GNode dst) {
              if (componentIDs[dst] == 0) {
                componentIDs[dst] = numComp;
                ctxt.push (dst);
              }
            };

            dagManager.applyToAdj (n, visitAdjClosure);

          },
          Galois::loopname ("find_component_bfs"),
          Galois::wl<Galois::WorkList::dChunkedFIFO<32> > ());
      

      // find next Source
      for (auto end = graph.end (); nextSource != end; ++nextSource) {
        if (componentIDs[*nextSource] == 0) {
          break;
        }
      }
      
    } // end while

    cctime.stop ();

    return numComp;

  }

}; // end class BFSpartitioner

template <typename G, typename M>
struct BlockPartitioner: public GreedyPartitioner<G, M> {
  using Base = GreedyPartitioner<G, M>;;
  
  BlockPartitioner (
      G& graph,
      M& dagManager,
      const unsigned numPart)
    : 
      Base (graph, dagManager, numPart)
  {}

  void partition (void) {

    Base::blockStart (makeLocalRange (Base::graph));
  }
};

template <typename G, typename M>
struct CyclicPartitioner: public GreedyPartitioner<G, M> {

  using Base = GreedyPartitioner<G, M>;;
  
  CyclicPartitioner (
      G& graph,
      M& dagManager,
      const unsigned numPart)
    : 
      Base (graph, dagManager, numPart)
  {}

  void partition (void) {
    Base::cyclicStart (makeLocalRange (Base::graph));
  }
};



} // end namespace Runtime
} // end namespace Galois


#endif //  GALOIS_RUNTIME_DET_PARTITIONERS_H
