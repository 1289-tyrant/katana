#ifndef APPS_PAGERANK_PAGERANK_H
#define APPS_PAGERANK_PAGERANK_H

#include "llvm/Support/CommandLine.h"

#include <iostream>

//! d is the damping factor. Alpha is the prob that user will do a random jump, i.e., 1 - d
static const float alpha = 1.0 - 0.85;
static const float alpha2 = 0.85; // Joyce changed to this which is a usual way to define alpha.

//! maximum relative change until we deem convergence
//static const float tolerance = 0.01; 
static const float tolerance = 0.0001; // Joyce

//ICC v13.1 doesn't yet support std::atomic<float> completely, emmulate its
//behavor with std::atomic<int>
struct atomic_float : public std::atomic<int> {
  static_assert(sizeof(int) == sizeof(float), "int and float must be the same size");

  float atomicIncrement(float value) {
    while (true) {
      union { float as_float; int as_int; } oldValue = { read() };
      union { float as_float; int as_int; } newValue = { oldValue.as_float + value };
      if (this->compare_exchange_strong(oldValue.as_int, newValue.as_int))
        return newValue.as_float;
    }
  }

  float read() {
    union { int as_int; float as_float; } caster = { this->load(std::memory_order_relaxed) };
    return caster.as_float;
  }

  void write(float v) {
    union { float as_float; int as_int; } caster = { v };
    this->store(caster.as_int, std::memory_order_relaxed);
  }
};

struct PNode {
  float value;
  atomic_float accum;
  PNode() { }

  float getPageRank() { return value; }
};

//! Make values unique
template<typename GNode>
struct TopPair {
  float value;
  GNode id;

  TopPair(double v, GNode i): value(v), id(i) { }

  bool operator<(const TopPair& b) const {
    if (value == b.value)
      return id > b.id;
    return value < b.value;
  }
};

template<typename Graph>
static void printTop(Graph& graph, int topn) {
  typedef typename Graph::GraphNode GNode;
  typedef TopPair<GNode> Pair;
  typedef std::map<Pair,GNode> Top;

  Top top;

  for (auto ii = graph.begin(), ei = graph.end(); ii != ei; ++ii) {
    GNode src = *ii;
    auto& n = graph.getData(src);
    auto value = n.getPageRank();
    Pair key(value, src);

    if ((int) top.size() < topn) {
      top.insert(std::make_pair(key, src));
      continue;
    }

    if (top.begin()->first < key) {
      top.erase(top.begin());
      top.insert(std::make_pair(key, src));
    }
  }

  int rank = 1;
  std::cout << "Rank PageRank Id\n";
  for (typename Top::reverse_iterator ii = top.rbegin(), ei = top.rend(); ii != ei; ++ii, ++rank) {
    std::cout << rank << ": " << ii->first.value << " " << ii->first.id << "\n";
  }
}

extern llvm::cl::opt<unsigned int> memoryLimit;
extern llvm::cl::opt<std::string> filename;
extern llvm::cl::opt<unsigned int> maxIterations;

#endif
