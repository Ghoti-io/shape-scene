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
 * The store's total-bytes limit: the accounted bytes of a resource, the live
 * total, and the limit asserted at N and N + 1 (resource.h, AD-19; story 11, the
 * gap that story 10's notes found).
 */

#include "scene_helpers.h"

#include "../alloc_tracker.h"

using namespace scene;

namespace {

GSHSCENE_Limits limits_with_bytes(size_t bytes) {
  GSHSCENE_Limits l;
  gshscene_limits_default(&l);
  l.max_bytes = bytes;
  return l;
}

const GSHSCENE_Resource * intern_line(GSHSCENE_Store * store, double x) {
  GSHGEOM_Path * p = new_path();
  gshgeom_path_move_to(p, P(x, 0));
  gshgeom_path_line_to(p, P(x + 1, 1));
  const GSHSCENE_Resource * r = intern(store, p);
  gshgeom_path_destroy(p);
  return r;
}

uint64_t live_bytes(GSHSCENE_Store * store) {
  GSHSCENE_Store_Stats stats;
  EXPECT_EQ(gshscene_store_stats(store, &stats), GSHSCENE_OK);
  return stats.bytes;
}

}  // namespace

TEST(StoreBytes, TheAccountedBytesFollowTheFormulaForEveryKind) {
  EXPECT_EQ(GSHSCENE_BYTES_BASE, 128u);
  EXPECT_EQ(GSHSCENE_BYTES_PER_PATH_ELEMENT, 160u);
  EXPECT_EQ(GSHSCENE_BYTES_PER_GLYPH, 32u);
  EXPECT_EQ(GSHSCENE_BYTES_PER_ITEM, 256u);
  EXPECT_EQ(GSHSCENE_BYTES_PER_AXIS, 8u);
  StoreHolder s;
  Ref sq(intern_square(s.store));  // five elements
  EXPECT_EQ(gshscene_resource_bytes(sq.get()), 128u + 5 * 160u);
  Ref line(intern_line(s.store, 0));  // two elements
  EXPECT_EQ(gshscene_resource_bytes(line.get()), 128u + 2 * 160u);
  GSHGEOM_Path * empty = new_path();
  Ref none(intern(s.store, empty));
  gshgeom_path_destroy(empty);
  EXPECT_EQ(gshscene_resource_bytes(none.get()), 128u) << "an empty path costs its base";
  GSHSCENE_Glyph_Run run;
  std::memset(&run, 0, sizeof run);
  Ref face(intern_face(s.store));
  EXPECT_EQ(gshscene_resource_bytes(face.get()), 128u + 2 * 8u) << "a face: its base and 8 for each coordinate";
  run.source = GSHSCENE_GLYPHS_FROM_FONT;
  run.face = face.get();
  run.size = 10;
  GSHSCENE_Glyph glyphs[3] = {{1, 0, 0, nullptr}, {2, 1, 0, nullptr}, {3, 2, 0, nullptr}};
  run.glyph_count = 3;
  run.glyphs = glyphs;
  Ref text;
  ASSERT_EQ(gshscene_store_intern_glyph_run(s.store, &run, &text.resource), GSHSCENE_OK);
  EXPECT_EQ(gshscene_resource_bytes(text.get()), 128u + 3 * 32u);
  ListHolder l(s.store);
  gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0);
  gshscene_list_fill(l.list, line.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0);
  gshscene_list_group_push(l.list, nullptr, 0, 0);
  gshscene_list_group_pop(l.list);
  ASSERT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
  Ref picture;
  ASSERT_EQ(gshscene_store_intern_picture(s.store, l.list, &picture.resource), GSHSCENE_OK);
  EXPECT_EQ(gshscene_resource_bytes(picture.get()), 128u + 4 * 256u);
  EXPECT_EQ(gshscene_resource_bytes(nullptr), 0u);
  EXPECT_EQ(live_bytes(s.store),
      (128u + 5 * 160u) + (128u + 2 * 160u) + 128u + (128u + 2 * 8u) + (128u + 3 * 32u) + (128u + 4 * 256u));
}

TEST(StoreBytes, AReleaseGivesTheBytesBackAndAHitCostsNothing) {
  StoreHolder s;
  Ref a(intern_line(s.store, 0));
  uint64_t one = live_bytes(s.store);
  EXPECT_EQ(one, gshscene_resource_bytes(a.get()));
  Ref again(intern_line(s.store, 0));
  EXPECT_EQ(again.get(), a.get());
  EXPECT_EQ(live_bytes(s.store), one) << "an intern that finds its content costs nothing";
  Ref b(intern_line(s.store, 5));
  EXPECT_EQ(live_bytes(s.store), 2 * one);
  b.resource = (gshscene_resource_release(b.resource), nullptr);
  EXPECT_EQ(live_bytes(s.store), one);
  again.resource = (gshscene_resource_release(again.resource), nullptr);
  EXPECT_EQ(live_bytes(s.store), one) << "a handle still held";
  a.resource = (gshscene_resource_release(a.resource), nullptr);
  EXPECT_EQ(live_bytes(s.store), 0u);
}

TEST(StoreBytes, MaxBytesAcceptsExactlyTheTotalAndRefusesOneByteMore) {
  size_t one = 128 + 2 * 160;
  {
    GSHSCENE_Limits l = limits_with_bytes(2 * one);  // exactly two
    StoreHolder s(&l);
    Ref a(intern_line(s.store, 0));
    Ref b(intern_line(s.store, 5));
    ASSERT_NE(a.get(), nullptr);
    ASSERT_NE(b.get(), nullptr);
    EXPECT_EQ(live_bytes(s.store), 2 * one);
    const GSHSCENE_Resource * third = nullptr;
    GSHGEOM_Path * p = new_path();
    gshgeom_path_move_to(p, P(9, 9));
    gshgeom_path_line_to(p, P(10, 10));
    GSHSCENE_Store_Stats before;
    ASSERT_EQ(gshscene_store_stats(s.store, &before), GSHSCENE_OK);
    EXPECT_EQ(gshscene_store_intern_path(s.store, p, &third), GSHSCENE_ERR_LIMIT);
    EXPECT_EQ(third, nullptr);
    GSHSCENE_Store_Stats after;
    ASSERT_EQ(gshscene_store_stats(s.store, &after), GSHSCENE_OK);
    EXPECT_EQ(after.created, before.created) << "a refused intern built nothing and issued no ID";
    EXPECT_EQ(after.live, before.live);
    EXPECT_EQ(after.bytes, before.bytes);
    // A hit at the limit is free.
    Ref hit(intern_line(s.store, 5));
    EXPECT_EQ(hit.get(), b.get());
    // Releasing one makes room for the third, exactly.
    a.resource = (gshscene_resource_release(a.resource), nullptr);
    EXPECT_EQ(gshscene_store_intern_path(s.store, p, &third), GSHSCENE_OK);
    gshscene_resource_release(third);
    gshgeom_path_destroy(p);
  }
  {
    GSHSCENE_Limits l = limits_with_bytes(2 * one - 1);  // one byte too few for the second
    StoreHolder s(&l);
    Ref a(intern_line(s.store, 0));
    EXPECT_NE(a.get(), nullptr);
    const GSHSCENE_Resource * b = nullptr;
    GSHGEOM_Path * p = new_path();
    gshgeom_path_move_to(p, P(5, 0));
    gshgeom_path_line_to(p, P(6, 1));
    EXPECT_EQ(gshscene_store_intern_path(s.store, p, &b), GSHSCENE_ERR_LIMIT);
    gshgeom_path_destroy(p);
  }
}

TEST(StoreBytes, ASingleResourceBiggerThanTheLimitIsRefused) {
  GSHSCENE_Limits l = limits_with_bytes(128);
  StoreHolder s(&l);
  GSHGEOM_Path * empty = new_path();
  const GSHSCENE_Resource * r = nullptr;
  EXPECT_EQ(gshscene_store_intern_path(s.store, empty, &r), GSHSCENE_OK) << "128 bytes: exactly the base";
  gshscene_resource_release(r);
  gshgeom_path_destroy(empty);
  GSHSCENE_Limits tight = limits_with_bytes(127);
  StoreHolder s2(&tight);
  GSHGEOM_Path * empty2 = new_path();
  r = nullptr;
  EXPECT_EQ(gshscene_store_intern_path(s2.store, empty2, &r), GSHSCENE_ERR_LIMIT) << "127 bytes: not even the base";
  gshgeom_path_destroy(empty2);
  // A zero is the default, not a limit of zero.
  GSHSCENE_Limits zero;
  gshscene_limits_default(&zero);
  zero.max_bytes = 0;
  StoreHolder s3(&zero);
  Ref sq(intern_square(s3.store));
  EXPECT_NE(sq.get(), nullptr);
}

TEST(StoreBytes, PicturesAndGlyphRunsAreAccountedAndRefusedAtTheLimit) {
  size_t path_bytes = 128 + 5 * 160;
  size_t picture_bytes = 128 + 1 * 256;
  GSHSCENE_Limits l = limits_with_bytes(path_bytes + picture_bytes);
  StoreHolder s(&l);
  Ref sq(intern_square(s.store));
  ListHolder list(s.store);
  gshscene_list_fill(list.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0);
  gshscene_list_close(list.list);
  Ref picture;
  EXPECT_EQ(gshscene_store_intern_picture(s.store, list.list, &picture.resource), GSHSCENE_OK) << "exactly the limit";
  GSHSCENE_Glyph_Run run;
  std::memset(&run, 0, sizeof run);
  run.source = GSHSCENE_GLYPHS_FROM_OUTLINES;
  const GSHSCENE_Resource * text = nullptr;
  EXPECT_EQ(gshscene_store_intern_glyph_run(s.store, &run, &text), GSHSCENE_ERR_LIMIT) << "an empty run still costs its base";
  EXPECT_EQ(text, nullptr);
}

TEST(StoreBytes, AFailureBuildsNothingAndLeaksNothing) {
  tracker::Tracker t;
  GSHSCENE_Allocator a = tracker::allocator_for(&t);
  {
    size_t one = 128 + 2 * 160;
    GSHSCENE_Limits l = limits_with_bytes(one);
    StoreHolder s(&l, &a);
    Ref first(intern_line(s.store, 0));
    long calls_before = t.calls;
    GSHGEOM_Path * p = new_path();
    gshgeom_path_move_to(p, P(7, 7));
    gshgeom_path_line_to(p, P(8, 8));
    const GSHSCENE_Resource * r = nullptr;
    EXPECT_EQ(gshscene_store_intern_path(s.store, p, &r), GSHSCENE_ERR_LIMIT);
    EXPECT_EQ(t.calls, calls_before) << "refused before anything was copied: the allocator was not called";
    gshgeom_path_destroy(p);
  }
  EXPECT_TRUE(t.live.empty());
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
