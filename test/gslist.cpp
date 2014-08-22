#include "Galois/Galois.h"
#include "Galois/gslist.h"
#include "Galois/Runtime/ll/gio.h"
#include "Galois/Runtime/mm/Mem.h"
#include <map>

int main(int argc, char** argv) {
  typedef Galois::Runtime::MM::FixedSizeHeap Heap;
  typedef std::unique_ptr<Heap> HeapPtr;
  typedef Galois::Runtime::PerThreadStorage<HeapPtr> Heaps;
  typedef Galois::concurrent_gslist<int> Collection;
  int numThreads = 2;
  unsigned size = 100;
  if (argc > 1)
    numThreads = atoi(argv[1]);
  if (size <= 0)
    numThreads = 2;
  if (argc > 2)
    size = atoi(argv[2]);
  if (size <= 0)
    size = 10000;

  Galois::setActiveThreads(numThreads);

  Heaps heaps;
  Collection c;

  Galois::on_each([&](unsigned id, unsigned total) {
    HeapPtr& hp = *heaps.getLocal();
    hp = std::move(HeapPtr(new Heap(sizeof(Collection::block_type))));
    for (unsigned i = 0; i < size; ++i)
      c.push_front(*hp, i);
  });

  std::map<int, int> counter;
  for (auto i : c) {
    counter[i] += 1;
  }
  for (unsigned i = 0; i < size; ++i) {
    GALOIS_ASSERT(counter[i] == numThreads);
  }
  GALOIS_ASSERT(counter.size() == size);

  Galois::on_each([&](unsigned id, unsigned total) {
    while (c.pop_front(Collection::promise_to_dealloc()))
      ;
  });

  return 0;
}
