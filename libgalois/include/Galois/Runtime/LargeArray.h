/** Large array types -*- C++ -*-
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
 * @section Description
 *
 * @author Donald Nguyen <ddn@cs.utexas.edu>
 */

#ifndef GALOIS_LARGEARRAY_H
#define GALOIS_LARGEARRAY_H

//#include "Galois/gstl.h"
//#include "Galois/Substrate/gio.h"
//#include "Galois/Runtime/Mem.h"
#include "Galois/Runtime/NumaMem.h"

#include <utility>
#include <cassert>

namespace Galois {
namespace Runtime {

/**
 * Large array of objects with proper specialization for void type and
 * supporting various allocation and construction policies.
 *
 * @tparam T value type of container
 */
template<typename T>
class LargeArray {
  LAptr m_realdata;
  T* m_data;
  std::size_t m_size;

public:
  typedef T raw_value_type;
  typedef T value_type;
  typedef std::size_t size_type;
  typedef std::ptrdiff_t difference_type;
  typedef value_type& reference;
  typedef const value_type& const_reference;
  typedef value_type* pointer;
  typedef const value_type* const_pointer;
  typedef pointer iterator;
  typedef const_pointer const_iterator;
  const static bool has_value = true;

  // Extra indirection to support incomplete T's
  struct size_of {
    const static size_type value = sizeof(T);
  };

protected:
  enum AllocType {Blocked, Local, Interleaved};
  void allocate(size_type n, unsigned numThreads, AllocType t) {
    assert(!m_data);
    m_size = n;
    switch(t) {
    case Blocked:
      m_realdata = largeMallocBlocked(n*sizeof(T), numThreads);
      break;
    case Interleaved:
      m_realdata = largeMallocInterleaved(n*sizeof(T), numThreads);
    case Local:
      m_realdata = largeMallocLocal(n*sizeof(T));
    };
    m_data = reinterpret_cast<T*>(m_realdata.get());
  }

public:
  /**
   * Wraps existing buffer in LargeArray interface.
   */
  LargeArray(void* d, size_type s): m_data(reinterpret_cast<T*>(d)), m_size(s) { }

  LargeArray(): m_data(0), m_size(0) { }

  LargeArray(LargeArray&& o): m_data(0), m_size(0) {
    std::swap(this->m_realdata, o.m_realdata);
    std::swap(this->m_data, o.m_data);
    std::swap(this->m_size, o.m_size);
  }

  LargeArray& operator=(LargeArray&& o) {
    std::swap(this->m_realdata, o.m_realdata);
    std::swap(this->m_data, o.m_data);
    std::swap(this->m_size, o.m_size);
    return *this;
  }

  LargeArray(const LargeArray&) = delete;
  LargeArray& operator=(const LargeArray&) = delete;

  ~LargeArray() {
    destroy();
    deallocate();
  }

  friend void swap(LargeArray& lhs, LargeArray& rhs) {
    std::swap(lhs.m_data, rhs.m_data);
    std::swap(lhs.m_size, rhs.m_size);
    std::swap(lhs.allocated, rhs.allocated);
  }

  
  const_reference at(difference_type x) const { assert(x < m_size); return m_data[x]; }
  reference at(difference_type x) { assert(x < m_size); return m_data[x]; }
  const_reference operator[](size_type x) const { assert(x < m_size);  return m_data[x]; }
  reference operator[](size_type x) { assert(x < m_size); return m_data[x]; }
  void set(difference_type x, const_reference v) { assert(x < m_size); m_data[x] = v; }
  size_type size() const { return m_size; }
  iterator begin() { return m_data; }
  const_iterator begin() const { return m_data; }
  iterator end() { return m_data + m_size; }
  const_iterator end() const { return m_data + m_size; }
  
  //! Allocates interleaved across NUMA (memory) nodes.
  void allocateInterleaved(size_type n, unsigned numThreads) { allocate(n, numThreads, Interleaved); }

  /**
   * Allocates using default memory policy (usually first-touch) 
   *
   * @param  n         number of elements to allocate 
   * @param  prefault  Prefault/touch memory to place it local to the currently executing
   *                   thread. By default, true because concurrent page-faulting can be a
   *                   scalability bottleneck.
   */
  void allocateBlocked(size_type n, unsigned numThreads) { allocate(n, numThreads, Blocked); }

  /**
   * Allocates using Thread Local memory policy
   *
   * @param  n         number of elements to allocate 
   */
  void allocateLocal(size_type n, unsigned numThreads) { allocate(n, numThreads, Local); }

  template<typename... Args>
  void construct(Args&&... args) {
    for (T* ii = m_data, *ei = m_data + m_size; ii != ei; ++ii)
      new (ii) T(std::forward<Args>(args)...);
  }

  template<typename... Args>
  void constructAt(size_type n, Args&&... args) {
    new (&m_data[n]) T(std::forward<Args>(args)...);
  }

  //! Allocate and construct
  template<typename... Args>
  void create(size_type n, Args&&... args) {
    allocateInterleaved(n);
    construct(std::forward<Args>(args)...);
  }

  void deallocate() {
    m_realdata.reset();
    m_data = 0;
    m_size = 0;
  }

  void destroy() {
    if (!m_data) return;
    uninitialized_destroy(m_data, m_data + m_size);
  }

  void destroyAt(size_type n) {
    (&m_data[n])->~T();
  }

  // The following methods are not shared with void specialization
  const_pointer data() const { return m_data; }
  pointer data() { return m_data; }
};

//! Void specialization
template<>
class LargeArray<void> {
public:
  LargeArray(void* d, std::size_t s) { }
  LargeArray() = default;
  LargeArray(const LargeArray&) = delete;
  LargeArray& operator=(const LargeArray&) = delete;

  friend void swap(LargeArray&, LargeArray&) {}

  typedef void raw_value_type;
  typedef void* value_type;
  typedef std::size_t size_type;
  typedef std::ptrdiff_t difference_type;
  typedef value_type reference;
  typedef const value_type const_reference;
  typedef value_type* pointer;
  typedef const value_type* const_pointer;
  typedef pointer iterator;
  typedef const_pointer const_iterator;
  const static bool has_value = false;
  struct size_of {
    const static std::size_t value = 0;
  };

  const_reference at(difference_type x) const { return 0; }
  reference at(difference_type x) { return 0; }
  const_reference operator[](size_type x) const { return 0; }
  template<typename AnyTy>
  void set(difference_type x, AnyTy v) { }
  size_type size() const { return 0; }
  iterator begin() { return 0; }
  const_iterator begin() const { return 0; }
  iterator end() { return 0; }
  const_iterator end() const { return 0; }

  void allocateInterleaved(size_type n, unsigned) { }
  void allocateBlocked(size_type n, unsigned) { }
  void allocateLocal(size_type n, unsigned, bool prefault = true) { }
  template<typename... Args> void construct(Args&&... args) { }
  template<typename... Args> void constructAt(size_type n, Args&&... args) { }
  template<typename... Args> void create(size_type n, Args&&... args) { }

  void deallocate() { }
  void destroy() { }
  void destroyAt(size_type n) { }

  const_pointer data() const { return 0; }
  pointer data() { return 0; }
};

} // end namespace Runtime
} // end namespace Galois
#endif

