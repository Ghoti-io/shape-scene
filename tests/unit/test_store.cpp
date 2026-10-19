/*
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * Copyright (C) 2026 Corey Pennycuff
 *
 * This file is part of Ghoti.io Shape Scene.
 *
 * Ghoti.io Shape Scene is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * Ghoti.io Shape Scene is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file
 *
 * The store: interning by content, handles and ids, release listeners, limits,
 * threads and allocation failure (resource.h, AD-27).
 */

#include "scene_helpers.h"

#include "../alloc_tracker.h"

#include <atomic>
#include <cstring>
#include <set>
#include <thread>
#include <vector>

using namespace scene;

namespace {

uint64_t to_zero(uint64_t, void *) {
  return 0;
}

struct Seen {
  uint64_t calls = 0;
  uint64_t last_hash = 0;
  void * ctx_seen = nullptr;
};

uint64_t record_hash(uint64_t content_hash, void * ctx) {
  Seen * seen = (Seen *)ctx;
  seen->calls++;
  seen->last_hash = content_hash;
  seen->ctx_seen = ctx;
  return content_hash;
}

struct Heard {
  std::vector<uint64_t> ids;
  std::vector<GSHSCENE_Resource_Kind> kinds;
  std::vector<uint64_t> hashes;
  std::vector<int> order;
  int tag = 0;
};

std::vector<int> g_order;

void hear(void * ctx, uint64_t id, const GSHSCENE_Resource * resource) {
  Heard * heard = (Heard *)ctx;
  heard->ids.push_back(id);
  heard->kinds.push_back(gshscene_resource_kind(resource));
  heard->hashes.push_back(gshscene_resource_hash(resource));
  g_order.push_back(heard->tag);
}

}  // namespace

// --- creating a store ----------------------------------------------------------

TEST(StoreCreate, RejectsANullOutput) {
  EXPECT_EQ(gshscene_store_create(nullptr, nullptr, nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_store_create_with_index_hash(nullptr, nullptr, nullptr, nullptr, nullptr),
      GSHSCENE_ERR_INVALID);
}

TEST(StoreCreate, DestroyIgnoresNull) {
  gshscene_store_destroy(nullptr);
  SUCCEED();
}

TEST(StoreCreate, StatsOfAFreshStoreAreZeroAndRejectNull) {
  StoreHolder s;
  ASSERT_NE(s.store, nullptr);
  GSHSCENE_Store_Stats stats;
  std::memset(&stats, 0xAB, sizeof stats);
  ASSERT_EQ(gshscene_store_stats(s.store, &stats), GSHSCENE_OK);
  EXPECT_EQ(stats.live, 0u);
  EXPECT_EQ(stats.created, 0u);
  EXPECT_EQ(stats.hits, 0u);
  EXPECT_EQ(stats.collisions, 0u);
  EXPECT_EQ(stats.buckets, 64u) << "the table starts at 64 buckets";
  EXPECT_EQ(gshscene_store_stats(nullptr, &stats), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_store_stats(s.store, nullptr), GSHSCENE_ERR_INVALID);
}

TEST(StoreCreate, AnAllocationFailureAtEveryCallLeavesNothing) {
  for (long k = 0;; k++) {
    tracker::Tracker t;
    t.fail_from = k;
    GSHSCENE_Allocator a = tracker::allocator_for(&t);
    GSHSCENE_Store * store = nullptr;
    GSHSCENE_Result r = gshscene_store_create(&a, nullptr, &store);
    if (r == GSHSCENE_OK) {
      EXPECT_NE(store, nullptr);
      gshscene_store_destroy(store);
    } else {
      EXPECT_EQ(r, GSHSCENE_ERR_OOM) << k;
      EXPECT_EQ(store, nullptr) << k;
    }
    EXPECT_TRUE(t.live.empty()) << "k=" << k;
    if (!t.failed) {
      break;
    }
  }
}

TEST(StoreCreate, ADestroyedStoreWithAResourceHeldStaysValidUntilTheLastRelease) {
  tracker::Tracker t;
  GSHSCENE_Allocator a = tracker::allocator_for(&t);
  GSHSCENE_Store * store = nullptr;
  ASSERT_EQ(gshscene_store_create(&a, nullptr, &store), GSHSCENE_OK);
  const GSHSCENE_Resource * r = intern_square(store);
  ASSERT_NE(r, nullptr);
  gshscene_store_destroy(store);
  EXPECT_FALSE(t.live.empty()) << "the store lives while a resource does";
  const GSHGEOM_Path * path = gshscene_resource_path(r);
  ASSERT_NE(path, nullptr);
  EXPECT_EQ(gshgeom_path_element_count(path), 5u);
  gshscene_resource_release(r);
  EXPECT_TRUE(t.live.empty());
}

// --- interning -------------------------------------------------------------------

TEST(StoreIntern, EqualContentGetsOneHandleAndOneId) {
  StoreHolder s;
  Ref a(intern_square(s.store));
  Ref b(intern_square(s.store));  // a rebuilt copy of the same path
  ASSERT_NE(a.get(), nullptr);
  EXPECT_EQ(a.get(), b.get());
  EXPECT_EQ(gshscene_resource_id(a.get()), gshscene_resource_id(b.get()));
  EXPECT_EQ(gshscene_resource_refcount(a.get()), 2u);
  GSHSCENE_Store_Stats stats;
  ASSERT_EQ(gshscene_store_stats(s.store, &stats), GSHSCENE_OK);
  EXPECT_EQ(stats.live, 1u);
  EXPECT_EQ(stats.created, 1u);
  EXPECT_EQ(stats.hits, 1u);
}

TEST(StoreIntern, AHitCopiesNothing) {
  tracker::Tracker t;
  GSHSCENE_Allocator a = tracker::allocator_for(&t);
  StoreHolder s(nullptr, &a);
  Ref first(intern_square(s.store));
  ASSERT_NE(first.get(), nullptr);
  long before = t.calls;
  Ref second(intern_square(s.store));
  EXPECT_EQ(second.get(), first.get());
  EXPECT_EQ(t.calls, before) << "an intern of present content must not allocate";
}

TEST(StoreIntern, ContentThatDiffersInOneThingGetsAnotherHandle) {
  StoreHolder s;
  Ref base(intern_square(s.store));
  std::vector<Ref *> others;
  auto add = [&](GSHGEOM_Path * p) {
    Ref * r = new Ref(intern(s.store, p));
    gshgeom_path_destroy(p);
    others.push_back(r);
    ASSERT_NE(r->get(), nullptr);
    EXPECT_NE(r->get(), base.get());
  };
  // One coordinate shifted by one unit in the last place.
  {
    GSHGEOM_Path * p = new_path();
    double x = 4.0;
    uint64_t bits;
    std::memcpy(&bits, &x, 8);
    bits += 1;
    std::memcpy(&x, &bits, 8);
    gshgeom_path_move_to(p, P(0, 0));
    gshgeom_path_line_to(p, P(x, 0));
    gshgeom_path_line_to(p, P(4, 4));
    gshgeom_path_line_to(p, P(0, 4));
    gshgeom_path_close(p);
    add(p);
  }
  // -0.0 where the square has +0.0.
  {
    GSHGEOM_Path * p = new_path();
    gshgeom_path_move_to(p, P(-0.0, 0));
    gshgeom_path_line_to(p, P(4, 0));
    gshgeom_path_line_to(p, P(4, 4));
    gshgeom_path_line_to(p, P(0, 4));
    gshgeom_path_close(p);
    add(p);
  }
  // A different verb (a quadratic with its control on the chord) with the same end points.
  {
    GSHGEOM_Path * p = new_path();
    gshgeom_path_move_to(p, P(0, 0));
    gshgeom_path_quadratic_to(p, P(2, 0), P(4, 0));
    gshgeom_path_line_to(p, P(4, 4));
    gshgeom_path_line_to(p, P(0, 4));
    gshgeom_path_close(p);
    add(p);
  }
  // Open instead of closed.
  {
    GSHGEOM_Path * p = new_path();
    gshgeom_path_move_to(p, P(0, 0));
    gshgeom_path_line_to(p, P(4, 0));
    gshgeom_path_line_to(p, P(4, 4));
    gshgeom_path_line_to(p, P(0, 4));
    add(p);
  }
  // Three dimensions with the same x and y.
  {
    GSHGEOM_Path * p = new_path(3);
    gshgeom_path_move_to(p, P(0, 0));
    gshgeom_path_line_to(p, P(4, 0));
    gshgeom_path_line_to(p, P(4, 4));
    gshgeom_path_line_to(p, P(0, 4));
    gshgeom_path_close(p);
    add(p);
  }
  // A rational quadratic against a plain one with the same points.
  {
    GSHGEOM_Path * p = new_path();
    const double w[3] = {1.0, 1.0, 1.0};
    gshgeom_path_move_to(p, P(0, 0));
    gshgeom_path_rational_quadratic_to(p, P(2, 0), P(4, 0), w);
    gshgeom_path_line_to(p, P(4, 4));
    gshgeom_path_line_to(p, P(0, 4));
    gshgeom_path_close(p);
    add(p);
  }
  // Pairwise distinct as well.
  std::set<const GSHSCENE_Resource *> distinct;
  distinct.insert(base.get());
  for (Ref * r : others) {
    EXPECT_TRUE(distinct.insert(r->get()).second);
  }
  for (Ref * r : others) {
    delete r;
  }
}

TEST(StoreIntern, EqualityIsOnTheContentNotOnTheHashOfIt) {
  // A planted collision: the table is indexed by 0 for everything.
  GSHSCENE_Store * store = nullptr;
  ASSERT_EQ(gshscene_store_create_with_index_hash(nullptr, nullptr, to_zero, nullptr, &store), GSHSCENE_OK);
  std::vector<const GSHSCENE_Resource *> first;
  for (int i = 0; i < 40; i++) {
    const GSHSCENE_Resource * r = intern_square(store, (double)i);
    ASSERT_NE(r, nullptr);
    first.push_back(r);
  }
  std::set<const GSHSCENE_Resource *> distinct(first.begin(), first.end());
  EXPECT_EQ(distinct.size(), 40u) << "unequal content must never share a handle";
  for (int i = 0; i < 40; i++) {
    const GSHSCENE_Resource * again = intern_square(store, (double)i);
    EXPECT_EQ(again, first[(size_t)i]) << "equal content must get its own handle back, i=" << i;
    gshscene_resource_release(again);
  }
  GSHSCENE_Store_Stats stats;
  ASSERT_EQ(gshscene_store_stats(store, &stats), GSHSCENE_OK);
  EXPECT_EQ(stats.live, 40u);
  EXPECT_EQ(stats.hits, 40u);
  EXPECT_GT(stats.collisions, 0u) << "the planted collision must have happened";
  // Release the oldest twenty (the tail of the one chain): the other twenty must stay findable
  // and the released ones must be made afresh.
  std::vector<uint64_t> old_ids;
  for (int i = 0; i < 20; i++) {
    old_ids.push_back(gshscene_resource_id(first[(size_t)i]));
    gshscene_resource_release(first[(size_t)i]);
  }
  ASSERT_EQ(gshscene_store_stats(store, &stats), GSHSCENE_OK);
  EXPECT_EQ(stats.live, 20u);
  for (int i = 20; i < 40; i++) {
    const GSHSCENE_Resource * again = intern_square(store, (double)i);
    EXPECT_EQ(again, first[(size_t)i]) << i;
    gshscene_resource_release(again);
  }
  for (int i = 0; i < 20; i++) {
    const GSHSCENE_Resource * fresh = intern_square(store, (double)i);
    ASSERT_NE(fresh, nullptr);
    EXPECT_NE(gshscene_resource_id(fresh), old_ids[(size_t)i]) << "a released resource is made afresh, with a new id";
    first[(size_t)i] = fresh;
  }
  ASSERT_EQ(gshscene_store_stats(store, &stats), GSHSCENE_OK);
  EXPECT_EQ(stats.live, 40u);
  // The content hashes are untouched by the planted index.
  std::set<uint64_t> hashes;
  for (const GSHSCENE_Resource * r : first) {
    hashes.insert(gshscene_resource_hash(r));
  }
  EXPECT_EQ(hashes.size(), 40u);
  for (const GSHSCENE_Resource * r : first) {
    gshscene_resource_release(r);
  }
  gshscene_store_destroy(store);
}

TEST(StoreIntern, ACollisionBetweenKindsIsAlsoNotAMerge) {
  GSHSCENE_Store * store = nullptr;
  ASSERT_EQ(gshscene_store_create_with_index_hash(nullptr, nullptr, to_zero, nullptr, &store), GSHSCENE_OK);
  const GSHSCENE_Resource * path = intern_square(store);
  GSHSCENE_Glyph_Run run;
  std::memset(&run, 0, sizeof run);
  run.source = GSHSCENE_GLYPHS_FROM_FONT;
  run.font_key = 1;
  run.size = 10.0;
  const GSHSCENE_Resource * glyphs = nullptr;
  ASSERT_EQ(gshscene_store_intern_glyph_run(store, &run, &glyphs), GSHSCENE_OK);
  EXPECT_NE(glyphs, path);
  EXPECT_EQ(gshscene_resource_kind(path), GSHSCENE_RESOURCE_PATH);
  EXPECT_EQ(gshscene_resource_kind(glyphs), GSHSCENE_RESOURCE_GLYPH_RUN);
  const GSHSCENE_Resource * again = nullptr;
  ASSERT_EQ(gshscene_store_intern_glyph_run(store, &run, &again), GSHSCENE_OK);
  EXPECT_EQ(again, glyphs);
  gshscene_resource_release(again);
  gshscene_resource_release(glyphs);
  gshscene_resource_release(path);
  gshscene_store_destroy(store);
}

TEST(StoreIntern, TheIndexHashFunctionGetsTheContentHashAndItsContext) {
  Seen seen;
  GSHSCENE_Store * store = nullptr;
  ASSERT_EQ(gshscene_store_create_with_index_hash(nullptr, nullptr, record_hash, &seen, &store), GSHSCENE_OK);
  const GSHSCENE_Resource * r = intern_square(store);
  ASSERT_NE(r, nullptr);
  EXPECT_GE(seen.calls, 1u);
  EXPECT_EQ(seen.last_hash, gshscene_resource_hash(r));
  EXPECT_EQ(seen.ctx_seen, (void *)&seen);
  gshscene_resource_release(r);
  gshscene_store_destroy(store);
}

TEST(StoreIntern, RejectsNullArguments) {
  StoreHolder s;
  GSHGEOM_Path * p = square_path();
  const GSHSCENE_Resource * r = nullptr;
  EXPECT_EQ(gshscene_store_intern_path(nullptr, p, &r), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_store_intern_path(s.store, nullptr, &r), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_store_intern_path(s.store, p, nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(r, nullptr);
  gshgeom_path_destroy(p);
}

TEST(StoreIntern, TheTableGrowsAndEveryHandleIsStillFound) {
  StoreHolder s;
  std::vector<const GSHSCENE_Resource *> handles;
  for (int i = 0; i < 300; i++) {
    handles.push_back(intern_square(s.store, (double)i));
    ASSERT_NE(handles.back(), nullptr);
  }
  GSHSCENE_Store_Stats after;
  ASSERT_EQ(gshscene_store_stats(s.store, &after), GSHSCENE_OK);
  EXPECT_EQ(after.buckets, 512u) << "64, doubled at 65, 129 and 257 live resources";
  EXPECT_EQ(after.live, 300u);
  EXPECT_EQ(after.collisions, 0u) << "entries with other index hashes in a chain are not collisions";
  for (int i = 0; i < 300; i++) {
    const GSHSCENE_Resource * again = intern_square(s.store, (double)i);
    EXPECT_EQ(again, handles[(size_t)i]) << i;
    gshscene_resource_release(again);
  }
  // Releasing out of order unlinks from the middle of chains.
  for (int i = 299; i >= 0; i -= 2) {
    gshscene_resource_release(handles[(size_t)i]);
    handles[(size_t)i] = nullptr;
  }
  ASSERT_EQ(gshscene_store_stats(s.store, &after), GSHSCENE_OK);
  EXPECT_EQ(after.live, 150u);
  for (int i = 0; i < 300; i++) {
    if (handles[(size_t)i]) {
      const GSHSCENE_Resource * again = intern_square(s.store, (double)i);
      EXPECT_EQ(again, handles[(size_t)i]) << i;
      gshscene_resource_release(again);
      gshscene_resource_release(handles[(size_t)i]);
    }
  }
  ASSERT_EQ(gshscene_store_stats(s.store, &after), GSHSCENE_OK);
  EXPECT_EQ(after.live, 0u);
}

TEST(StoreIntern, TheTableDoublesWhenTheLiveCountPassesTheBucketCount) {
  StoreHolder s;
  std::vector<const GSHSCENE_Resource *> handles;
  GSHSCENE_Store_Stats stats;
  for (int i = 0; i < 64; i++) {
    handles.push_back(intern_square(s.store, (double)i));
  }
  ASSERT_EQ(gshscene_store_stats(s.store, &stats), GSHSCENE_OK);
  EXPECT_EQ(stats.buckets, 64u) << "64 live in 64 buckets is not yet over";
  handles.push_back(intern_square(s.store, 64.0));
  ASSERT_EQ(gshscene_store_stats(s.store, &stats), GSHSCENE_OK);
  EXPECT_EQ(stats.buckets, 128u);
  for (const GSHSCENE_Resource * r : handles) {
    gshscene_resource_release(r);
  }
}

// --- handles ---------------------------------------------------------------------

TEST(StoreHandles, IdsAscendFromOneAndAreNeverReused) {
  StoreHolder s;
  std::set<uint64_t> ids;
  uint64_t last = 0;
  for (int i = 0; i < 50; i++) {
    const GSHSCENE_Resource * r = intern_square(s.store, (double)i);
    uint64_t id = gshscene_resource_id(r);
    EXPECT_GT(id, last);
    last = id;
    EXPECT_TRUE(ids.insert(id).second);
    gshscene_resource_release(r);  // freed at once: its memory may be reused, its id may not
  }
  EXPECT_EQ(*ids.begin(), 1u);
  // The same content again, after its last release, is a new resource with a new id.
  const GSHSCENE_Resource * a = intern_square(s.store, 0.0);
  EXPECT_GT(gshscene_resource_id(a), last);
  gshscene_resource_release(a);
}

TEST(StoreHandles, RetainAndReleaseCountAndNullIsIgnored) {
  StoreHolder s;
  const GSHSCENE_Resource * r = intern_square(s.store);
  EXPECT_EQ(gshscene_resource_refcount(r), 1u);
  EXPECT_EQ(gshscene_resource_retain(r), r);
  EXPECT_EQ(gshscene_resource_refcount(r), 2u);
  gshscene_resource_release(r);
  EXPECT_EQ(gshscene_resource_refcount(r), 1u);
  gshscene_resource_release(r);
  EXPECT_EQ(gshscene_resource_retain(nullptr), nullptr);
  gshscene_resource_release(nullptr);
  EXPECT_EQ(gshscene_resource_id(nullptr), 0u);
  EXPECT_EQ(gshscene_resource_refcount(nullptr), 0u);
  EXPECT_EQ(gshscene_resource_hash(nullptr), GSHGEOM_FNV_OFFSET_BASIS);
  EXPECT_EQ(gshscene_resource_kind(nullptr), GSHSCENE_RESOURCE_KIND_COUNT);
}

// --- release listeners --------------------------------------------------------------

TEST(StoreListener, IsCalledOnceAtTheLastReleaseWithTheIdAndAReadableResource) {
  StoreHolder s;
  Heard heard;
  uint64_t token = 0;
  ASSERT_EQ(gshscene_store_add_release_listener(s.store, hear, &heard, &token), GSHSCENE_OK);
  EXPECT_NE(token, 0u) << "a token is never 0";
  const GSHSCENE_Resource * r = intern_square(s.store);
  gshscene_resource_retain(r);
  uint64_t id = gshscene_resource_id(r);
  uint64_t hash = gshscene_resource_hash(r);
  gshscene_resource_release(r);
  EXPECT_TRUE(heard.ids.empty()) << "a release that leaves a reference is not heard";
  gshscene_resource_release(r);
  ASSERT_EQ(heard.ids.size(), 1u);
  EXPECT_EQ(heard.ids[0], id);
  EXPECT_EQ(heard.kinds[0], GSHSCENE_RESOURCE_PATH);
  EXPECT_EQ(heard.hashes[0], hash);
}

TEST(StoreListener, NeverHearsTheSameIdTwice) {
  StoreHolder s;
  Heard heard;
  uint64_t token = 0;
  ASSERT_EQ(gshscene_store_add_release_listener(s.store, hear, &heard, &token), GSHSCENE_OK);
  for (int round = 0; round < 5; round++) {
    const GSHSCENE_Resource * r = intern_square(s.store);  // the same content each time
    gshscene_resource_release(r);
  }
  ASSERT_EQ(heard.ids.size(), 5u);
  std::set<uint64_t> unique(heard.ids.begin(), heard.ids.end());
  EXPECT_EQ(unique.size(), 5u);
}

TEST(StoreListener, TwoListenersAreCalledInTheOrderTheyWereAdded) {
  StoreHolder s;
  Heard first, second;
  first.tag = 1;
  second.tag = 2;
  g_order.clear();
  uint64_t t1 = 0, t2 = 0;
  ASSERT_EQ(gshscene_store_add_release_listener(s.store, hear, &first, &t1), GSHSCENE_OK);
  ASSERT_EQ(gshscene_store_add_release_listener(s.store, hear, &second, &t2), GSHSCENE_OK);
  EXPECT_NE(t1, t2);
  gshscene_resource_release(intern_square(s.store));
  EXPECT_EQ(first.ids.size(), 1u);
  EXPECT_EQ(second.ids.size(), 1u);
  ASSERT_EQ(g_order.size(), 2u);
  EXPECT_EQ(g_order[0], 1);
  EXPECT_EQ(g_order[1], 2);
}

TEST(StoreListener, ARemovedListenerIsNotCalledAndTheOtherIs) {
  StoreHolder s;
  Heard first, second;
  uint64_t t1 = 0, t2 = 0;
  ASSERT_EQ(gshscene_store_add_release_listener(s.store, hear, &first, &t1), GSHSCENE_OK);
  ASSERT_EQ(gshscene_store_add_release_listener(s.store, hear, &second, &t2), GSHSCENE_OK);
  ASSERT_EQ(gshscene_store_remove_release_listener(s.store, t1), GSHSCENE_OK);
  gshscene_resource_release(intern_square(s.store));
  EXPECT_TRUE(first.ids.empty());
  EXPECT_EQ(second.ids.size(), 1u);
  ASSERT_EQ(gshscene_store_remove_release_listener(s.store, t2), GSHSCENE_OK);
  gshscene_resource_release(intern_square(s.store));
  EXPECT_EQ(second.ids.size(), 1u);
}

TEST(StoreListener, RemovingTheMiddleOneKeepsTheEnds) {
  StoreHolder s;
  Heard a, b, c;
  uint64_t ta = 0, tb = 0, tc = 0;
  ASSERT_EQ(gshscene_store_add_release_listener(s.store, hear, &a, &ta), GSHSCENE_OK);
  ASSERT_EQ(gshscene_store_add_release_listener(s.store, hear, &b, &tb), GSHSCENE_OK);
  ASSERT_EQ(gshscene_store_add_release_listener(s.store, hear, &c, &tc), GSHSCENE_OK);
  ASSERT_EQ(gshscene_store_remove_release_listener(s.store, tb), GSHSCENE_OK);
  gshscene_resource_release(intern_square(s.store));
  EXPECT_EQ(a.ids.size(), 1u);
  EXPECT_TRUE(b.ids.empty());
  EXPECT_EQ(c.ids.size(), 1u);
}

TEST(StoreListener, ARegistrationHearsOnlyWhatIsReleasedAfterIt) {
  StoreHolder s;
  const GSHSCENE_Resource * early = intern_square(s.store, 1.0);
  Heard heard;
  uint64_t token = 0;
  ASSERT_EQ(gshscene_store_add_release_listener(s.store, hear, &heard, &token), GSHSCENE_OK);
  gshscene_resource_release(early);
  EXPECT_EQ(heard.ids.size(), 1u) << "a resource made before the registration is heard when it is released after";
}

TEST(StoreListener, RejectsWhatIsWrong) {
  StoreHolder s;
  Heard heard;
  uint64_t token = 0;
  EXPECT_EQ(gshscene_store_add_release_listener(nullptr, hear, &heard, &token), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_store_add_release_listener(s.store, nullptr, &heard, &token), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_store_add_release_listener(s.store, hear, &heard, nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_store_remove_release_listener(nullptr, 1), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_store_remove_release_listener(s.store, 12345), GSHSCENE_ERR_INVALID);
  ASSERT_EQ(gshscene_store_add_release_listener(s.store, hear, &heard, &token), GSHSCENE_OK);
  ASSERT_EQ(gshscene_store_remove_release_listener(s.store, token), GSHSCENE_OK);
  EXPECT_EQ(gshscene_store_remove_release_listener(s.store, token), GSHSCENE_ERR_INVALID) << "twice";
}

namespace {
struct Reenter {
  GSHSCENE_Store * store;
  uint64_t calls = 0;
};
void reenter(void * ctx, uint64_t, const GSHSCENE_Resource *) {
  Reenter * r = (Reenter *)ctx;
  r->calls++;
  if (r->calls > 1) {
    return;  // the nested release below is heard too, and must not start another
  }
  // The callback runs outside the lock: it may use the store.
  GSHSCENE_Store_Stats stats;
  gshscene_store_stats(r->store, &stats);
  const GSHSCENE_Resource * other = intern_square(r->store, 99.0);
  gshscene_resource_release(other);  // and that release is not heard again here: it is a nested release
}
}  // namespace

TEST(StoreListener, MayCallTheStoreFromItsCallback) {
  StoreHolder s;
  Reenter ctx{s.store};
  uint64_t token = 0;
  ASSERT_EQ(gshscene_store_add_release_listener(s.store, reenter, &ctx, &token), GSHSCENE_OK);
  gshscene_resource_release(intern_square(s.store, 1.0));
  EXPECT_EQ(ctx.calls, 2u) << "the release of the nested resource is heard too";
  ASSERT_EQ(gshscene_store_remove_release_listener(s.store, token), GSHSCENE_OK);
}

TEST(StoreListener, AllocationFailureWhileAddingOrRemovingLeavesTheSetAsItWas) {
  for (long k = 0;; k++) {
    tracker::Tracker t;
    GSHSCENE_Allocator a = tracker::allocator_for(&t);
    GSHSCENE_Store * store = nullptr;
    ASSERT_EQ(gshscene_store_create(&a, nullptr, &store), GSHSCENE_OK);
    Heard h1, h2;
    uint64_t t1 = 0, t2 = 0;
    ASSERT_EQ(gshscene_store_add_release_listener(store, hear, &h1, &t1), GSHSCENE_OK);
    t.fail_from = t.calls + k;
    GSHSCENE_Result added = gshscene_store_add_release_listener(store, hear, &h2, &t2);
    GSHSCENE_Result removed = GSHSCENE_OK;
    if (added == GSHSCENE_OK) {
      removed = gshscene_store_remove_release_listener(store, t1);
    } else {
      EXPECT_EQ(added, GSHSCENE_ERR_OOM);
    }
    t.fail_from = -1;
    const GSHSCENE_Resource * r = intern_square(store);
    gshscene_resource_release(r);
    if (added != GSHSCENE_OK) {
      EXPECT_EQ(h1.ids.size(), 1u) << "k=" << k;
      EXPECT_TRUE(h2.ids.empty()) << "k=" << k;
    } else if (removed == GSHSCENE_OK) {
      EXPECT_TRUE(h1.ids.empty()) << "k=" << k;
      EXPECT_EQ(h2.ids.size(), 1u) << "k=" << k;
    } else {
      EXPECT_EQ(removed, GSHSCENE_ERR_OOM);
      EXPECT_EQ(h1.ids.size(), 1u) << "k=" << k;
      EXPECT_EQ(h2.ids.size(), 1u) << "k=" << k;
    }
    gshscene_store_destroy(store);
    EXPECT_TRUE(t.live.empty()) << "k=" << k;
    if (added == GSHSCENE_OK && removed == GSHSCENE_OK) {
      break;
    }
  }
}

// --- limits --------------------------------------------------------------------------

TEST(StoreLimits, MaxResourcesAcceptsExactlyTheLimit) {
  GSHSCENE_Limits limits;
  gshscene_limits_default(&limits);
  limits.max_resources = 3;
  tracker::Tracker t;
  GSHSCENE_Allocator alloc = tracker::allocator_for(&t);
  {
    StoreHolder s(&limits, &alloc);
    Ref a(intern_square(s.store, 1.0)), b(intern_square(s.store, 2.0)), c(intern_square(s.store, 3.0));
    ASSERT_NE(a.get(), nullptr);
    ASSERT_NE(b.get(), nullptr);
    ASSERT_NE(c.get(), nullptr);
    GSHGEOM_Path * p = square_path(4.0);
    const GSHSCENE_Resource * d = nullptr;
    EXPECT_EQ(gshscene_store_intern_path(s.store, p, &d), GSHSCENE_ERR_LIMIT);
    EXPECT_EQ(d, nullptr);
    gshgeom_path_destroy(p);
    GSHSCENE_Store_Stats stats;
    ASSERT_EQ(gshscene_store_stats(s.store, &stats), GSHSCENE_OK);
    EXPECT_EQ(stats.live, 3u);
    // Content already present is not a new resource, so it is not refused.
    Ref again(intern_square(s.store, 2.0));
    EXPECT_EQ(again.get(), b.get());
    // After a release there is room again.
    gshscene_resource_release(a.resource);
    a.resource = nullptr;
    Ref fresh(intern_square(s.store, 4.0));
    EXPECT_NE(fresh.get(), nullptr);
  }
  EXPECT_TRUE(t.live.empty()) << "a refused intern leaves nothing allocated";
}

// --- allocation failure ----------------------------------------------------------------

TEST(StoreAllocation, AFailureAtEveryCallOfAnInternLeavesNothingAndTheStoreAsItWas) {
  GSHGEOM_Path * p0 = square_path(0.0);
  GSHGEOM_Path * p1 = square_path(1.0);
  for (long k = 0;; k++) {
    tracker::Tracker t;
    GSHSCENE_Allocator a = tracker::allocator_for(&t);
    GSHSCENE_Store * store = nullptr;
    ASSERT_EQ(gshscene_store_create(&a, nullptr, &store), GSHSCENE_OK);
    t.fail_from = t.calls + k;
    const GSHSCENE_Resource * r0 = nullptr;
    const GSHSCENE_Resource * r1 = nullptr;
    GSHSCENE_Result a0 = gshscene_store_intern_path(store, p0, &r0);
    GSHSCENE_Result a1 = gshscene_store_intern_path(store, p1, &r1);
    t.fail_from = -1;
    for (GSHSCENE_Result r : {a0, a1}) {
      EXPECT_TRUE(r == GSHSCENE_OK || r == GSHSCENE_ERR_OOM) << k;
    }
    GSHSCENE_Store_Stats stats;
    ASSERT_EQ(gshscene_store_stats(store, &stats), GSHSCENE_OK);
    EXPECT_EQ(stats.live, (a0 == GSHSCENE_OK ? 1u : 0u) + (a1 == GSHSCENE_OK ? 1u : 0u)) << k;
    if (a0 != GSHSCENE_OK) {
      EXPECT_EQ(r0, nullptr);
    }
    gshscene_resource_release(r0);
    gshscene_resource_release(r1);
    gshscene_store_destroy(store);
    EXPECT_TRUE(t.live.empty()) << "k=" << k;
    if (!t.failed) {
      break;
    }
  }
  gshgeom_path_destroy(p0);
  gshgeom_path_destroy(p1);
}

TEST(StoreAllocation, AFailureWhileTheTableGrowsDoesNotLoseAResource) {
  // The 65th of 70 distinct paths passes the table's 64 buckets; the growth that allocates may fail.
  for (long k = 0;; k++) {
    tracker::Tracker t;
    GSHSCENE_Allocator a = tracker::allocator_for(&t);
    GSHSCENE_Store * store = nullptr;
    ASSERT_EQ(gshscene_store_create(&a, nullptr, &store), GSHSCENE_OK);
    std::vector<const GSHSCENE_Resource *> handles(70, nullptr);
    for (int i = 0; i < 64; i++) {
      handles[(size_t)i] = intern_square(store, (double)i);
      ASSERT_NE(handles[(size_t)i], nullptr);
    }
    t.fail_from = t.calls + k;
    for (int i = 64; i < 70; i++) {  // the 65th makes the table grow
      GSHGEOM_Path * p = square_path((double)i);
      gshscene_store_intern_path(store, p, &handles[(size_t)i]);
      gshgeom_path_destroy(p);
    }
    t.fail_from = -1;
    for (int i = 0; i < 70; i++) {
      if (!handles[(size_t)i]) {
        continue;
      }
      const GSHSCENE_Resource * again = intern_square(store, (double)i);
      EXPECT_EQ(again, handles[(size_t)i]) << "k=" << k << " i=" << i;
      gshscene_resource_release(again);
    }
    for (const GSHSCENE_Resource * r : handles) {
      gshscene_resource_release(r);
    }
    gshscene_store_destroy(store);
    EXPECT_TRUE(t.live.empty()) << "k=" << k;
    if (!t.failed) {
      break;
    }
  }
}

// --- threads ---------------------------------------------------------------------------

TEST(StoreThreads, EightThreadsInterningTheSameContentGetOneHandleAndEndWithNothingLive) {
  StoreHolder s;
  constexpr int kPaths = 20;
  constexpr int kThreads = 8;
  constexpr int kRounds = 300;
  std::vector<GSHGEOM_Path *> paths;
  for (int i = 0; i < kPaths; i++) {
    paths.push_back(square_path((double)i));
  }
  // The main thread holds every content, so every thread must get the main thread's handle.
  std::vector<const GSHSCENE_Resource *> held;
  for (int i = 0; i < kPaths; i++) {
    held.push_back(intern(s.store, paths[(size_t)i]));
  }
  std::atomic<int> wrong{0};
  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; t++) {
    threads.emplace_back([&, t]() {
      for (int round = 0; round < kRounds; round++) {
        int i = (round * 7 + t) % kPaths;
        const GSHSCENE_Resource * r = nullptr;
        if (gshscene_store_intern_path(s.store, paths[(size_t)i], &r) != GSHSCENE_OK || r != held[(size_t)i]) {
          wrong++;
        }
        gshscene_resource_retain(r);
        gshscene_resource_release(r);
        gshscene_resource_release(r);
      }
    });
  }
  for (std::thread & th : threads) {
    th.join();
  }
  EXPECT_EQ(wrong.load(), 0);
  for (const GSHSCENE_Resource * r : held) {
    EXPECT_EQ(gshscene_resource_refcount(r), 1u);
    gshscene_resource_release(r);
  }
  GSHSCENE_Store_Stats stats;
  ASSERT_EQ(gshscene_store_stats(s.store, &stats), GSHSCENE_OK);
  EXPECT_EQ(stats.live, 0u);
  for (GSHGEOM_Path * p : paths) {
    gshgeom_path_destroy(p);
  }
}

TEST(StoreThreads, InterningAndReleasingTheLastReferenceRaceWithoutUseAfterFree) {
  // Nobody holds the content between rounds: the last release of one thread races
  // the intern of another.  Under ASan or TSan a use after free or a race is the failure.
  tracker::Counting counting;
  GSHSCENE_Allocator alloc = tracker::counting_allocator(&counting);
  StoreHolder s(nullptr, &alloc);
  constexpr int kThreads = 8;
  constexpr int kRounds = 2000;
  GSHGEOM_Path * a = square_path(0.0);
  GSHGEOM_Path * b = square_path(1.0);
  std::atomic<int> wrong{0};
  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; t++) {
    threads.emplace_back([&, t]() {
      for (int round = 0; round < kRounds; round++) {
        const GSHSCENE_Resource * r = nullptr;
        GSHGEOM_Path * p = ((round + t) & 1) ? a : b;
        if (gshscene_store_intern_path(s.store, p, &r) != GSHSCENE_OK) {
          wrong++;
          continue;
        }
        if (gshgeom_path_element_count(gshscene_resource_path(r)) != 5u || gshscene_resource_refcount(r) == 0) {
          wrong++;
        }
        gshscene_resource_release(r);
      }
    });
  }
  for (std::thread & th : threads) {
    th.join();
  }
  EXPECT_EQ(wrong.load(), 0);
  GSHSCENE_Store_Stats stats;
  ASSERT_EQ(gshscene_store_stats(s.store, &stats), GSHSCENE_OK);
  EXPECT_EQ(stats.live, 0u);
  EXPECT_GE(stats.created, 2u);
  gshgeom_path_destroy(a);
  gshgeom_path_destroy(b);
  gshscene_store_destroy(s.store);
  s.store = nullptr;
  EXPECT_EQ(counting.live.load(), 0);
}

namespace {
std::atomic<uint64_t> g_released{0};
void count_release(void *, uint64_t, const GSHSCENE_Resource *) {
  g_released++;
}
}  // namespace

TEST(StoreThreads, EveryResourceMadeIsReleasedExactlyOnceAndHeardOnce) {
  StoreHolder s;
  g_released = 0;
  uint64_t token = 0;
  ASSERT_EQ(gshscene_store_add_release_listener(s.store, count_release, nullptr, &token), GSHSCENE_OK);
  GSHGEOM_Path * a = square_path(0.0);
  std::vector<std::thread> threads;
  for (int t = 0; t < 6; t++) {
    threads.emplace_back([&]() {
      for (int round = 0; round < 1500; round++) {
        const GSHSCENE_Resource * r = nullptr;
        if (gshscene_store_intern_path(s.store, a, &r) == GSHSCENE_OK) {
          gshscene_resource_release(r);
        }
      }
    });
  }
  for (std::thread & th : threads) {
    th.join();
  }
  GSHSCENE_Store_Stats stats;
  ASSERT_EQ(gshscene_store_stats(s.store, &stats), GSHSCENE_OK);
  EXPECT_EQ(stats.live, 0u);
  EXPECT_EQ(g_released.load(), stats.created) << "one release notice per resource ever made";
  gshgeom_path_destroy(a);
}

#include <barrier>

TEST(StoreThreads, ThreadsStartingTogetherOnNewContentAllGetTheSameHandle) {
  // Nobody holds the content before the round, so every thread misses and builds; the second
  // lookup under the lock has to hand every one of them the first thread's resource.
  tracker::Counting counting;
  GSHSCENE_Allocator alloc = tracker::counting_allocator(&counting);
  StoreHolder s(nullptr, &alloc);
  constexpr int kThreads = 8;
  constexpr int kRounds = 200;
  std::vector<GSHGEOM_Path *> paths;
  for (int i = 0; i < kRounds; i++) {
    paths.push_back(square_path((double)i));
  }
  std::vector<const GSHSCENE_Resource *> got((size_t)kThreads, nullptr);
  std::barrier start(kThreads), held(kThreads), done(kThreads);
  std::atomic<int> wrong{0};
  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; t++) {
    threads.emplace_back([&, t]() {
      for (int round = 0; round < kRounds; round++) {
        start.arrive_and_wait();
        const GSHSCENE_Resource * r = nullptr;
        if (gshscene_store_intern_path(s.store, paths[(size_t)round], &r) != GSHSCENE_OK) {
          wrong++;
        }
        got[(size_t)t] = r;
        held.arrive_and_wait();  // every thread holds its reference now
        if (t == 0) {
          for (int k = 1; k < kThreads; k++) {
            if (got[(size_t)k] != got[0]) {
              wrong++;
            }
          }
          if (gshscene_resource_refcount(got[0]) != (size_t)kThreads) {
            wrong++;
          }
        }
        done.arrive_and_wait();
        gshscene_resource_release(r);
      }
    });
  }
  for (std::thread & th : threads) {
    th.join();
  }
  EXPECT_EQ(wrong.load(), 0);
  GSHSCENE_Store_Stats stats;
  ASSERT_EQ(gshscene_store_stats(s.store, &stats), GSHSCENE_OK);
  EXPECT_EQ(stats.live, 0u);
  EXPECT_EQ(stats.created, (uint64_t)kRounds) << "one resource per content, however the threads raced";
  EXPECT_EQ(stats.hits, (uint64_t)kRounds * (kThreads - 1)) << "every other intern is a hit";
  for (GSHGEOM_Path * p : paths) {
    gshgeom_path_destroy(p);
  }
  gshscene_store_destroy(s.store);
  s.store = nullptr;
  EXPECT_EQ(counting.live.load(), 0) << "a resource built by a thread that lost the race is freed";
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
