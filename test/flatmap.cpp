#include "Galois/Galois.h"
#include "Galois/FlatMap.h"
#ifdef GALOIS_USE_EXP
#include "Galois/ConcurrentFlatMap.h"
#endif
#include "Galois/Timer.h"

#include <boost/iterator/counting_iterator.hpp>
#include <cstdlib>
#include <iostream>
#include <map>
#include <random>


struct element {
  volatile int val;
  element(): val() { }
  element(int x): val(x) { }
  operator int() const { return val; }
};

std::ostream& operator<<(std::ostream& out, const element& e) {
  out << e.val;
  return out;
}

template<typename MapTy>
struct Fn1 {
  MapTy* m;
  void operator()(const int& x) {
    (*m)[x] = element(x);
  }
};

template<typename MapTy>
struct Fn2 {
  MapTy* m;
  void operator()(const int& x) {
    int v = (*m)[x].val;
    if (v != x && v != 0) {
      GALOIS_DIE("wrong value");
    }
  }
};

template<typename MapTy>
void timeMapParallel(std::string c, const std::vector<int>& keys) {
  MapTy m;
  Galois::Timer t1, t2;
  t1.start();
  Galois::do_all(keys.begin(), keys.end(), Fn1<MapTy> { &m });
  t1.stop();
  t2.start();
  Galois::do_all(keys.begin(), keys.end(), Fn2<MapTy> { &m });
  t2.stop();
  std::cout << c << " " << t1.get() << " " << t2.get() << "\n";
}

template<typename MapTy>
void timeMap(std::string c, const std::vector<int>& keys) {
  MapTy m;
  Galois::Timer t1, t2;
  t1.start();
  for (auto& x : keys) {
    m[x] = element(x);
  }
  t1.stop();
  t2.start();
  for (auto& x : keys) {
    int v = m[x].val;
    if (v != x)
      GALOIS_DIE("wrong value");
  }
  t2.stop();
  std::cout << c << " " << t1.get() << " " << t2.get() << "\n";
}

template<typename MapTy>
void testMap() {
  MapTy m;
  MapTy m2(m);
  MapTy m3;

  m3.insert(std::make_pair(10, 0));
  m3.insert(std::make_pair(20, 0));
  
  MapTy m4(m3.begin(), m3.end());

  m2 = m3;
  m3 = std::move(m2);

  m[0] = 0;
  m[1] = 1;
  m[3] = 2;
  m[3] = m[3] + 3;
  m[4] = 4;

  m.insert(std::make_pair(5, 4));
  m.insert(m4.begin(), m4.end());

  std::cout << "10 == " << m.find(10)->first << "\n";

  //m.erase(10);
  //m.erase(1);

  if (m.size() != 7 || m.empty())
    abort();
  std::swap(m, m3);
  if (m.size() != 2 || m.empty())
    abort();
  m.clear();
  if (m.size() != 0 || !m.empty())
    abort();
  std::swap(m, m3);
  if (m.size() != 7 || m.empty())
    abort();

  for (auto ii = m.begin(), ee = m.end(); ii != ee; ++ii)
    std::cout << ii->first << " " << ii->second << " ";
  std::cout << "\n";

  for (auto ii = m.cbegin(), ee = m.cend(); ii != ee; ++ii)
    std::cout << ii->first << " " << ii->second << " ";
  std::cout << "\n";

  for (auto ii = m.rbegin(), ee = m.rend(); ii != ee; ++ii)
    std::cout << ii->first << " " << ii->second << " ";
  std::cout << "\n";

  for (auto ii = m.crbegin(), ee = m.crend(); ii != ee; ++ii)
    std::cout << ii->first << " " << ii->second << " ";
  std::cout << "\n";
}

void timeTests(std::string prefix, const std::vector<int>& keys) {
  for (int i = 0; i < 3; ++i)
    timeMap<std::map<int, element>>(prefix + "std::map", keys);
  for (int i = 0; i < 3; ++i)
    timeMap<Galois::flat_map<int, element>>(prefix + "flat_map", keys);
#ifdef GALOIS_USE_EXP
  for (int i = 0; i < 3; ++i)
    timeMap<Galois::concurrent_flat_map<int, element>>(prefix + "concurrent_flat_map", keys);
  for (int i = 0; i < 3; ++i)
    timeMapParallel<Galois::concurrent_flat_map<int, element>>(prefix + "concurrent_flat_map (parallel)", keys);
#endif
}

int main(int argc, char** argv) {
  testMap<std::map<int, element>>();
  testMap<Galois::flat_map<int, element>>();
#ifdef GALOIS_USE_EXP
  testMap<Galois::concurrent_flat_map<int, element>>();
#endif
  Galois::setActiveThreads(8);

  int size = 1;
  if (argc > 1)
    size = atoi(argv[1]);
  if (size <= 0)
    size = 1000000;

  std::mt19937 mt(0);
  std::uniform_int_distribution<int> dist(0, size);
  std::vector<int> randomKeys;
  std::vector<int> keys;
  for (int i = 0; i < size; ++i) {
    randomKeys.push_back(dist(mt));
    keys.push_back(i);
  }

  timeTests("seq ", keys);
  timeTests("random ", randomKeys);
  return 0;
}
