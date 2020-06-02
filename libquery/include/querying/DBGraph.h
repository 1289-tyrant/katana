/*
 * This file belongs to the Galois project, a C++ library for exploiting
 * parallelism. The code is being released under the terms of the 3-Clause BSD
 * License (a copy is located in LICENSE.txt at the top-level directory).
 *
 * Copyright (C) 2019, The University of Texas at Austin. All rights reserved.
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
 */
#ifndef _GALOIS_DB_GRAPH_
#define _GALOIS_DB_GRAPH_

#include "querying/PythonGraph.h"
#include "galois/graphs/OfflineGraph.h"
#include "galois/graphs/BufferedGraph.h"

namespace galois {
namespace graphs {

/**
 * Acts as a C++ wrapper around attributed graph + adds functionality for
 * using .gr files instead of going through RIPE graph construction code.
 */
class DBGraph {
  //! Underlying attribute graph
  AttributedGraph* attGraph = nullptr;

  //! number of different node labels
  size_t numNodeLabels = 0;
  //! number of different edge labels
  size_t numEdgeLabels = 0;

  /**
   * Setup the different node and edge labels in the attributed graph; assumes
   * it is already allocated.
   */
  void setupNodeEdgeLabelsMeta() {
    // create node/edge labels and save them
    char dummy[10]; // assumption that labels won't get to 8+ digits
    for (size_t i = 0; i < numNodeLabels; i++) {
      std::string thisLabel = "";
      thisLabel             = thisLabel + std::to_string(i);
      strcpy(dummy, thisLabel.c_str());
      setNodeLabelMetadata(attGraph, i, dummy);
    }
    for (size_t i = 0; i < numEdgeLabels; i++) {
      std::string thisLabel = "";
      thisLabel             = thisLabel + std::to_string(i);
      strcpy(dummy, thisLabel.c_str());
      setEdgeLabelMetadata(attGraph, i, dummy);
    }
  }

  void setupNodes(uint32_t numNodes) {
    char dummy[30];
    // set node metadata: uuid is node id as a string and name is also just
    // node id
    // Unfortunately must be done serially as it messes with maps which are
    // not thread safe
    for (size_t i = 0; i < numNodes; i++) {
      std::string id = "ID" + std::to_string(i);
      strcpy(dummy, id.c_str());
      // TODO node labels are round-robin; make this more controllable?
      setNewNode(attGraph, i, dummy, i % numNodeLabels, dummy);
    }

    // TODO node may have more than one label; can add randomly?

    // TODO node attributes
  }

  /**
   * Returns number of edges per vertex
   * where the number of edges for vertex
   * i is in array[i + 1] (array[0] is 0)
   *
   * @param graphTopology Topology of original graph in a buffered graph
   * @returns Array of edges counts where array[i + 1] is number of edges
   * for vertex i
   */
  std::vector<uint64_t>
  getEdgeCounts(galois::graphs::BufferedGraph<uint32_t>& graphTopology) {
    // allocate vector where counts will be stored
    std::vector<uint64_t> edgeCounts;
    // + 1 so that it can be used as a counter for how many edges have been
    // added for a particular vertex
    edgeCounts.resize(graphTopology.size() + 1, 0);

    // loop over all edges, add to that source vertex's edge counts for each
    // endpoint (ignore self loops)
    galois::do_all(
        galois::iterate(0u, graphTopology.size()),
        [&](uint32_t vertexID) {
          for (auto i = graphTopology.edgeBegin(vertexID);
               i < graphTopology.edgeEnd(vertexID); i++) {
            uint64_t dst = graphTopology.edgeDestination(*i);
            if (vertexID != dst) {
              // src increment
              __sync_add_and_fetch(&(edgeCounts[vertexID + 1]), 1);
            }
          }
        },
        galois::steal(), galois::loopname("GetEdgeCounts"));

    return edgeCounts;
  }

public:
  /**
   * Setup meta parameters
   */
  DBGraph() {
    attGraph      = new AttributedGraph;
    numNodeLabels = 1;
    numEdgeLabels = 1;
  }

  /**
   * Destroy attributed graph object
   */
  ~DBGraph() {
    if (attGraph) {
      delete attGraph;
    }
  }

  /**
   * Given graph topology, construct the attributed graph by
   * ignoring self loops.
   */
  void constructDataGraph(const std::string filename, bool useWeights = true) {
    // first, load graph topology
    // NOTE: assumes weighted
    galois::graphs::BufferedGraph<uint32_t> graphTopology;
    graphTopology.loadGraph(filename);

    galois::GAccumulator<uint64_t> keptEdgeCountAccumulator;
    galois::GReduceMax<uint64_t> maxLabels;
    keptEdgeCountAccumulator.reset();
    maxLabels.reset();
    // next, count the number of edges we want to keep (i.e. ignore the self
    // loops)
    galois::do_all(
        galois::iterate(0u, graphTopology.size()),
        [&](uint32_t vertexID) {
          for (auto i = graphTopology.edgeBegin(vertexID);
               i < graphTopology.edgeEnd(vertexID); i++) {
            uint64_t dst = graphTopology.edgeDestination(*i);
            if (vertexID != dst) {
              keptEdgeCountAccumulator += 1;
            }
            maxLabels.update(graphTopology.edgeData(*i));
          }
        },
        galois::steal(), // steal due to edge imbalance among nodes
        galois::loopname("CountKeptEdges"));

    numEdgeLabels = maxLabels.reduce() + 1;
    galois::gInfo("Edge label count is ", numEdgeLabels);

    uint64_t keptEdgeCount = keptEdgeCountAccumulator.reduce();

    galois::gDebug("Kept edge count is ", keptEdgeCount,
                   " compared to "
                   "original ",
                   graphTopology.sizeEdges());

    uint64_t finalEdgeCount = keptEdgeCount;

    ////////////////////////////////////////////////////////////////////////////
    // META SETUP
    ////////////////////////////////////////////////////////////////////////////

    // allocate the memory for the new graph
    allocateGraph(attGraph, graphTopology.size(), finalEdgeCount, numNodeLabels,
                  numEdgeLabels);

    setupNodeEdgeLabelsMeta();

    ////////////////////////////////////////////////////////////////////////////
    // NODE TOPOLOGY
    ////////////////////////////////////////////////////////////////////////////

    setupNodes(graphTopology.size());

    ////////////////////////////////////////////////////////////////////////////
    // EDGE TOPOLOGY
    ////////////////////////////////////////////////////////////////////////////

    // need to count how many edges for each vertex in the graph
    std::vector<uint64_t> edgeCountsPerVertex = getEdgeCounts(graphTopology);

    // prefix sum the edge counts; this will tell us where we can write
    // new edges of a particular vertex
    for (size_t i = 1; i < edgeCountsPerVertex.size(); i++) {
      edgeCountsPerVertex[i] += edgeCountsPerVertex[i - 1];
    }

    // fix edge end points
    galois::do_all(
        galois::iterate(0u, graphTopology.size()),
        [&](uint32_t vertexID) {
          fixEndEdge(attGraph, vertexID, edgeCountsPerVertex[vertexID + 1]);
        },
        galois::loopname("EdgeEndpointFixing"));

    // loop over edges of a graph, add edges (again, ignore self loops)
    galois::do_all(
        galois::iterate(0u, graphTopology.size()),
        [&](uint32_t vertexID) {
          for (auto i = graphTopology.edgeBegin(vertexID);
               i < graphTopology.edgeEnd(vertexID); i++) {
            uint64_t edgeID = *i;
            // label to use for this edge pointing both ways
            // commented out part here is random edge label assignment
            // unsigned labelBit = edgeID % numEdgeLabels;
            unsigned labelBit = graphTopology.edgeData(edgeID);

            // TODO for now timestamp is original edge id
            uint64_t timestamp = edgeID;
            uint64_t dst       = graphTopology.edgeDestination(*i);

            // check if not a self loop
            if (vertexID != dst) {
              // get forward edge id
              uint64_t forwardEdge =
                  __sync_fetch_and_add(&(edgeCountsPerVertex[vertexID]), 1);
              // set forward
              constructNewEdge(attGraph, forwardEdge, dst, labelBit, timestamp);
            }
          }
        },
        galois::steal(), // steal due to edge imbalance among nodes
        galois::loopname("ConstructEdges"));

    // TODO edge attributes and other labels?

    // at this point graph is constructed: build and sort index
    attGraph->graph.constructAndSortIndex();

    GALOIS_ASSERT(edgeCountsPerVertex[graphTopology.size() - 1] ==
                  finalEdgeCount);
    galois::gInfo("Data graph construction from GR complete");

    ////////////////////////////////////////////////////////////////////////////
    // Finishing up
    ////////////////////////////////////////////////////////////////////////////
  }

  ////! Reads graph topology into attributed graph, then sets up its metadata.
  // void readGr(const std::string filename) {
  //  ////////////////////////////////////////////////////////////////////////////
  //  // Graph topology loading
  //  ////////////////////////////////////////////////////////////////////////////
  //  // use offline graph for metadata things
  //  galois::graphs::OfflineGraph og(filename);
  //  size_t numNodes = og.size();
  //  size_t numEdges = og.sizeEdges();

  //  // allocate the graph + node/edge labels
  //  allocateGraph(attGraph, numNodes, numEdges, numNodeLabels, numEdgeLabels);

  //  // open file, pass to LCCSR to directly load topology
  //  int fd = open(filename.c_str(), O_RDONLY);
  //  if (fd == -1) GALOIS_SYS_DIE("failed opening ", "'", filename, "',
  //  LC_CSR");
  //	Graph& lcGraph = attGraph->graph;
  //  lcGraph.readGraphTopology(fd, numNodes, numEdges);
  //  // file done, close it
  //  close(fd);

  //  // TODO problem: directly loading graph does not work as querying code
  //  // currently assume undirected graph; fix this later

  //  ////////////////////////////////////////////////////////////////////////////
  //  // Metadata setup
  //  ////////////////////////////////////////////////////////////////////////////

  //  // Topology now exists: need to create the metadata mappings and such

  //  // create node/edge labels and save them
  //  setupNodeEdgeLabelsMeta();
  //  setupNodes(numNodes);

  //  // edges
  //  for (size_t i = 0; i < numEdges; i++) {
  //    // fill out edge data as edge destinations already come from gr file
  //    // TODO timestamps currently grow with edge index i
  //    lcGraph.setEdgeData(i, QueryEdgeData(1 << (i % numEdgeLabels), i));
  //  }

  //  // TODO edge attributes
  //}

  size_t runCypherQuery(const std::string cypherQueryStr,
                        bool useGraphSimulation,
                        std::string outputFile = "matched.edges") {
    // run the query
    size_t mEdgeCount =
        matchCypherQuery(attGraph, EventLimit(), EventWindow(),
                         cypherQueryStr.c_str(), useGraphSimulation);
    return mEdgeCount;
  }
};

} // namespace graphs
} // namespace galois

#endif
