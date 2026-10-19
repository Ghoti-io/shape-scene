/**
 * @file
 *
 * An allocator that fails the k-th call (or every call from the k-th) and
 * records what is live, for the allocation-failure sweeps (CAP-15).
 *
 * Copyright 2026 by Corey Pennycuff
 */

#ifndef GHOTI_IO_GSHSCENE_TEST_ALLOC_TRACKER_H
#define GHOTI_IO_GSHSCENE_TEST_ALLOC_TRACKER_H

#include <ghoti.io/shape-scene/shape-scene.h>

#include <cstdlib>
#include <set>

#include <gtest/gtest.h>

namespace tracker {

struct Tracker {
  long fail_from = -1;      // the call number that first fails; -1 never
  bool persistent = false;  // every call from fail_from on fails
  long calls = 0;
  bool failed = false;
  std::set<void *> live;
  bool fails() {
    long n = calls++;
    if (fail_from >= 0 && (persistent ? n >= fail_from : n == fail_from)) {
      failed = true;
      return true;
    }
    return false;
  }
};

inline void * t_malloc(void * ctx, size_t n) {
  Tracker * t = (Tracker *)ctx;
  if (t->fails()) return nullptr;
  void * p = std::malloc(n ? n : 1);
  if (p) t->live.insert(p);
  return p;
}
inline void * t_calloc(void * ctx, size_t c, size_t n) {
  Tracker * t = (Tracker *)ctx;
  if (t->fails()) return nullptr;
  void * p = std::calloc(c ? c : 1, n ? n : 1);
  if (p) t->live.insert(p);
  return p;
}
inline void * t_realloc(void * ctx, void * old, size_t n) {
  Tracker * t = (Tracker *)ctx;
  if (t->fails()) return nullptr;
  void * p = std::realloc(old, n ? n : 1);
  if (p) {
    t->live.erase(old);
    t->live.insert(p);
  }
  return p;
}
inline void t_free(void * ctx, void * p) {
  Tracker * t = (Tracker *)ctx;
  if (p) {
    EXPECT_EQ(t->live.erase(p), 1u) << "freed what was not allocated here";
  }
  std::free(p);
}

inline GSHSCENE_Allocator allocator_for(Tracker * t) {
  GSHSCENE_Allocator a;
  a.ctx = t;
  a.malloc_fn = t_malloc;
  a.calloc_fn = t_calloc;
  a.realloc_fn = t_realloc;
  a.free_fn = t_free;
  return a;
}


#include <atomic>

/** A thread-safe allocator that only counts what is live, for the tests that run threads. */
struct Counting {
  std::atomic<long> live{0};
  std::atomic<long> calls{0};
};

inline void * c_malloc(void * ctx, size_t n) {
  Counting * c = (Counting *)ctx;
  void * p = std::malloc(n ? n : 1);
  if (p) {
    c->live++;
    c->calls++;
  }
  return p;
}
inline void * c_calloc(void * ctx, size_t k, size_t n) {
  Counting * c = (Counting *)ctx;
  void * p = std::calloc(k ? k : 1, n ? n : 1);
  if (p) {
    c->live++;
    c->calls++;
  }
  return p;
}
inline void * c_realloc(void * ctx, void * old, size_t n) {
  Counting * c = (Counting *)ctx;
  void * p = std::realloc(old, n ? n : 1);
  if (p && !old) {
    c->live++;
  }
  return p;
}
inline void c_free(void * ctx, void * p) {
  if (p) {
    ((Counting *)ctx)->live--;
  }
  std::free(p);
}

inline GSHSCENE_Allocator counting_allocator(Counting * c) {
  GSHSCENE_Allocator a;
  a.ctx = c;
  a.malloc_fn = c_malloc;
  a.calloc_fn = c_calloc;
  a.realloc_fn = c_realloc;
  a.free_fn = c_free;
  return a;
}

}  // namespace tracker

#endif /* GHOTI_IO_GSHSCENE_TEST_ALLOC_TRACKER_H */
