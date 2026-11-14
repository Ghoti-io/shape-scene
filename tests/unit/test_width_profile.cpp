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
 * The width profile resource (stroke.h, SW1 to SW9): interning and the order of its refusals, the canonical form, equality, hash, dump,
 * a style that names one (its checks, its retention, its conversion to the geometry library's parameters, its bounds), and the copy
 * into another store.
 */

#include "scene_helpers.h"

#include "../alloc_tracker.h"

#include <cmath>
#include <string>
#include <vector>

using namespace scene;

namespace {

Ref intern_profile(GSHSCENE_Store * s, const std::vector<GSHSCENE_Width_Stop> & stops, GSHSCENE_Result * result = nullptr) {
  const GSHSCENE_Resource * r = nullptr;
  GSHSCENE_Result got = gshscene_store_intern_width_profile(s, stops.data(), stops.size(), &r);
  if (result) {
    *result = got;
  }
  return Ref(got == GSHSCENE_OK ? r : nullptr);
}

GSHSCENE_Stroke_Style round_style(const GSHSCENE_Resource * profile, double width = 6.0) {
  GSHSCENE_Stroke_Style st;
  gshscene_stroke_style_init(&st);
  st.width = width;
  st.join = GSHSCENE_STROKE_JOIN_ROUND;
  st.cap = GSHSCENE_STROKE_CAP_ROUND;
  st.width_profile = profile;
  return st;
}

std::string dump_of(const GSHSCENE_Resource * r) {
  char * buf = nullptr;
  size_t size = 0;
  FILE * f = open_memstream(&buf, &size);
  EXPECT_EQ(gshscene_resource_dump(r, f), GSHSCENE_OK);
  fclose(f);
  std::string out(buf, size);
  free(buf);
  return out;
}

}  // namespace

TEST(WidthProfile, InternsReadsBackAndIsOneHandleForEqualContent) {
  StoreHolder sh;
  Ref a = intern_profile(sh.store, {{0, 0.5}, {10, 1.0}, {10, 2.0}});
  ASSERT_NE(a.get(), nullptr);
  EXPECT_EQ(gshscene_resource_kind(a.get()), GSHSCENE_RESOURCE_WIDTH_PROFILE);
  EXPECT_STREQ(gshscene_resource_kind_string(GSHSCENE_RESOURCE_WIDTH_PROFILE), "width-profile");
  const GSHSCENE_Width_Stop * stops = nullptr;
  size_t n = 0;
  ASSERT_EQ(gshscene_resource_width_profile(a.get(), &stops, &n), GSHSCENE_OK);
  ASSERT_EQ(n, 3u);
  EXPECT_EQ(stops[1].position, 10.0);
  EXPECT_EQ(stops[2].factor, 2.0);
  Ref b = intern_profile(sh.store, {{0, 0.5}, {10, 1.0}, {10, 2.0}});
  EXPECT_EQ(a.get(), b.get());
  Ref c = intern_profile(sh.store, {{0, 0.5}, {10, 1.0}, {10, 2.5}});
  EXPECT_NE(a.get(), c.get());
  EXPECT_NE(gshscene_resource_hash(a.get()), gshscene_resource_hash(c.get()));
  /* -0 is +0: one spelling */
  Ref z1 = intern_profile(sh.store, {{0.0, 1.0}});
  Ref z2 = intern_profile(sh.store, {{-0.0, 1.0}});
  EXPECT_EQ(z1.get(), z2.get());
  /* readback of a non-profile */
  Ref sq(intern_square(sh.store));
  EXPECT_EQ(gshscene_resource_width_profile(sq.get(), &stops, &n), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_resource_width_profile(nullptr, &stops, &n), GSHSCENE_ERR_INVALID);
}

TEST(WidthProfile, TheOrderOfTheRefusals) {
  StoreHolder sh;
  GSHSCENE_Result r;
  const GSHSCENE_Resource * out = nullptr;
  EXPECT_EQ(gshscene_store_intern_width_profile(nullptr, nullptr, 0, &out), GSHSCENE_ERR_INVALID);
  const GSHSCENE_Width_Stop one[1] = {{0, 1}};
  EXPECT_EQ(gshscene_store_intern_width_profile(sh.store, one, 0, &out), GSHSCENE_ERR_INVALID) << "no stops";
  EXPECT_EQ(gshscene_store_intern_width_profile(sh.store, nullptr, 1, &out), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_store_intern_width_profile(sh.store, one, 1, nullptr), GSHSCENE_ERR_INVALID);
  intern_profile(sh.store, {{0, 1}, {-1, 1}}, &r);
  EXPECT_EQ(r, GSHSCENE_ERR_INVALID) << "a negative position";
  intern_profile(sh.store, {{5, 1}, {4, 1}}, &r);
  EXPECT_EQ(r, GSHSCENE_ERR_INVALID) << "decreasing positions";
  intern_profile(sh.store, {{0, -0.5}}, &r);
  EXPECT_EQ(r, GSHSCENE_ERR_INVALID) << "a negative factor";
  intern_profile(sh.store, {{0, NAN}}, &r);
  EXPECT_EQ(r, GSHSCENE_ERR_INVALID);
  intern_profile(sh.store, {{INFINITY, 1}}, &r);
  EXPECT_EQ(r, GSHSCENE_ERR_INVALID);
  intern_profile(sh.store, {{0, 0x1p20}}, &r);
  EXPECT_EQ(r, GSHSCENE_ERR_LIMIT) << "a factor past 2^16";
  intern_profile(sh.store, {{1e-30, 1}}, &r);
  EXPECT_EQ(r, GSHSCENE_ERR_LIMIT) << "a position below 2^-64 that is not 0";
  intern_profile(sh.store, {{0x1p70, 1}}, &r);
  EXPECT_EQ(r, GSHSCENE_ERR_LIMIT);
  intern_profile(sh.store, {{0, -1}, {1e-30, 1}}, &r);
  EXPECT_EQ(r, GSHSCENE_ERR_INVALID) << "invalid before limit";
  GSHSCENE_Limits lim;
  gshscene_limits_default(&lim);
  lim.max_resource_elements = 3;
  StoreHolder small(&lim);
  intern_profile(small.store, {{0, 1}, {1, 1}, {2, 1}}, &r);
  EXPECT_EQ(r, GSHSCENE_OK);
  intern_profile(small.store, {{0, 1}, {1, 1}, {2, 1}, {3, 1}}, &r);
  EXPECT_EQ(r, GSHSCENE_ERR_LIMIT) << "past the store's element limit";
}

TEST(WidthProfile, HashDumpAndAccounting) {
  StoreHolder sh;
  Ref a = intern_profile(sh.store, {{0, 0.5}, {12.5, 1.5}});
  const std::string dump = dump_of(a.get());
  EXPECT_NE(dump.find("stops 2\n"), std::string::npos) << dump;
  EXPECT_NE(dump.find("s 0000000000000000 3fe0000000000000\n"), std::string::npos) << dump;
  EXPECT_NE(dump.find("s 4029000000000000 3ff8000000000000\n"), std::string::npos) << dump;
  EXPECT_EQ(gshscene_resource_bytes(a.get()), GSHSCENE_BYTES_BASE + 2 * GSHSCENE_BYTES_PER_WIDTH_STOP);
  Ref other = intern_profile(sh.store, {{0, 0.5}, {12.5, 1.5}, {12.5, 1.5}});
  EXPECT_NE(gshscene_resource_hash(a.get()), gshscene_resource_hash(other.get())) << "the count is hashed";
  EXPECT_NE(dump_of(other.get()), dump);
}

TEST(WidthProfile, AStyleThatNamesOneIsCheckedHeldAndConverted) {
  StoreHolder sh;
  Ref p = intern_profile(sh.store, {{0, 0.25}, {20, 1.0}, {40, 3.0}});
  GSHSCENE_Stroke_Style st = round_style(p.get());
  const GSHSCENE_Resource * s = nullptr;
  ASSERT_EQ(gshscene_store_intern_stroke_style(sh.store, &st, &s), GSHSCENE_OK);
  Ref style(s);
  /* the style differs from the same style without a profile, in hash and as a handle */
  GSHSCENE_Stroke_Style plain = round_style(nullptr);
  const GSHSCENE_Resource * s2 = nullptr;
  ASSERT_EQ(gshscene_store_intern_stroke_style(sh.store, &plain, &s2), GSHSCENE_OK);
  Ref plain_style(s2);
  EXPECT_NE(style.get(), plain_style.get());
  EXPECT_NE(gshscene_resource_hash(style.get()), gshscene_resource_hash(plain_style.get()));
  GSHSCENE_Stroke_Style back;
  ASSERT_EQ(gshscene_resource_stroke_style(style.get(), &back), GSHSCENE_OK);
  EXPECT_EQ(back.width_profile, p.get());
  /* the geometry library's parameters name the stops, in the profile resource */
  GSHGEOM_Stroke g;
  ASSERT_EQ(gshscene_resource_stroke_geometry(style.get(), &g), GSHSCENE_OK);
  ASSERT_NE(g.width_profile, nullptr);
  ASSERT_EQ(g.width_profile->count, 3u);
  EXPECT_EQ(g.width_profile->stops[2].factor, 3.0);
  EXPECT_EQ(gshgeom_stroke_check(&g), GSHGEOM_OK);
  ASSERT_EQ(gshscene_resource_stroke_geometry(plain_style.get(), &g), GSHSCENE_OK);
  EXPECT_EQ(g.width_profile, nullptr);
  /* the style holds the profile: it outlives the caller's reference */
  const size_t refs = gshscene_resource_refcount(p.get());
  EXPECT_GE(refs, 2u);
  p = Ref();
  const GSHSCENE_Width_Stop * stops = nullptr;
  size_t n = 0;
  ASSERT_EQ(gshscene_resource_stroke_style(style.get(), &back), GSHSCENE_OK);
  ASSERT_EQ(gshscene_resource_width_profile(back.width_profile, &stops, &n), GSHSCENE_OK);
  EXPECT_EQ(n, 3u);
  style = Ref();
}

TEST(WidthProfile, AStyleWithAProfileHasToBeRoundWithNoDashesAndCenteredAndOfThisStore) {
  StoreHolder sh;
  Ref p = intern_profile(sh.store, {{0, 1}});
  const GSHSCENE_Resource * out = nullptr;
  GSHSCENE_Stroke_Style st = round_style(p.get());
  st.join = GSHSCENE_STROKE_JOIN_MITER;
  EXPECT_EQ(gshscene_store_intern_stroke_style(sh.store, &st, &out), GSHSCENE_ERR_UNSUPPORTED);
  st = round_style(p.get());
  st.cap = GSHSCENE_STROKE_CAP_BUTT;
  EXPECT_EQ(gshscene_store_intern_stroke_style(sh.store, &st, &out), GSHSCENE_ERR_UNSUPPORTED);
  st = round_style(p.get());
  static const double dashes[2] = {2, 1};
  st.dash_count = 2;
  st.dashes = dashes;
  EXPECT_EQ(gshscene_store_intern_stroke_style(sh.store, &st, &out), GSHSCENE_ERR_UNSUPPORTED);
  st = round_style(p.get());
  st.alignment = GSHSCENE_STROKE_ALIGN_INSIDE;
  EXPECT_EQ(gshscene_store_intern_stroke_style(sh.store, &st, &out), GSHSCENE_ERR_UNSUPPORTED);
  st.width = -1;
  EXPECT_EQ(gshscene_store_intern_stroke_style(sh.store, &st, &out), GSHSCENE_ERR_INVALID) << "invalid before unsupported";
  /* a profile of another store, and a resource that is not a profile */
  StoreHolder other;
  Ref foreign = intern_profile(other.store, {{0, 1}});
  st = round_style(foreign.get());
  EXPECT_EQ(gshscene_store_intern_stroke_style(sh.store, &st, &out), GSHSCENE_ERR_INVALID);
  Ref sq(intern_square(sh.store));
  st = round_style(sq.get());
  EXPECT_EQ(gshscene_store_intern_stroke_style(sh.store, &st, &out), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(out, nullptr);
}

TEST(WidthProfile, TheStrokesBoundsGrowWithTheLargestFactor) {
  StoreHolder sh;
  Ref narrow = intern_profile(sh.store, {{0, 1.0}});
  Ref wide = intern_profile(sh.store, {{0, 0.5}, {10, 4.0}, {20, 1.0}});
  GSHSCENE_Stroke_Style a = round_style(narrow.get(), 4.0);
  GSHSCENE_Stroke_Style b = round_style(wide.get(), 4.0);
  const GSHSCENE_Resource * ra = nullptr;
  const GSHSCENE_Resource * rb = nullptr;
  ASSERT_EQ(gshscene_store_intern_stroke_style(sh.store, &a, &ra), GSHSCENE_OK);
  ASSERT_EQ(gshscene_store_intern_stroke_style(sh.store, &b, &rb), GSHSCENE_OK);
  Ref hold_a(ra), hold_b(rb);
  GSHGEOM_Bounds center;
  std::memset(&center, 0, sizeof center);
  center.min = GSHGEOM_Point{10, 10, 0};
  center.max = GSHGEOM_Point{30, 20, 0};
  GSHGEOM_Map id;
  gshgeom_map_identity(2, &id);
  GSHGEOM_Bounds ba, bb;
  ASSERT_EQ(gshscene_stroke_bounds(ra, &center, &id, GSHSCENE_UNIT_PIXEL, &ba), GSHSCENE_OK);
  ASSERT_EQ(gshscene_stroke_bounds(rb, &center, &id, GSHSCENE_UNIT_PIXEL, &bb), GSHSCENE_OK);
  EXPECT_LE(ba.min.x, 10.0 - 2.0) << "half of width 4 at factor 1";
  EXPECT_LE(bb.min.x, 10.0 - 8.0) << "half of width 4 at the largest factor, 4";
  EXPECT_LT(bb.min.x, ba.min.x);
  EXPECT_GT(bb.max.y, ba.max.y);
}

TEST(WidthProfile, ACopyIntoAnotherStoreCarriesTheProfileAndTheStyleThatNamesIt) {
  StoreHolder sh;
  Ref p = intern_profile(sh.store, {{0, 0.25}, {7, 2.0}, {7, 1.0}});
  GSHSCENE_Stroke_Style st = round_style(p.get(), 3.0);
  const GSHSCENE_Resource * s = nullptr;
  ASSERT_EQ(gshscene_store_intern_stroke_style(sh.store, &st, &s), GSHSCENE_OK);
  Ref style(s);
  StoreHolder dst;
  const GSHSCENE_Resource * copy = nullptr;
  ASSERT_EQ(gshscene_store_copy_resource(dst.store, style.get(), &copy), GSHSCENE_OK);
  Ref hold(copy);
  EXPECT_EQ(gshscene_resource_hash(copy), gshscene_resource_hash(style.get()));
  EXPECT_EQ(dump_of(copy), dump_of(style.get()));
  GSHSCENE_Stroke_Style back;
  ASSERT_EQ(gshscene_resource_stroke_style(copy, &back), GSHSCENE_OK);
  ASSERT_NE(back.width_profile, nullptr);
  EXPECT_NE(back.width_profile, p.get()) << "the copy's profile is the destination store's";
  EXPECT_EQ(gshscene_resource_hash(back.width_profile), gshscene_resource_hash(p.get()));
  const GSHSCENE_Resource * pc = nullptr;
  ASSERT_EQ(gshscene_store_copy_resource(dst.store, p.get(), &pc), GSHSCENE_OK);
  Ref hold_p(pc);
  EXPECT_EQ(pc, back.width_profile) << "one handle for equal content in the destination";
}

TEST(WidthProfile, EveryFailingAllocationInterningAProfileAndAStyleLeavesNothing) {
  tracker::Tracker t;
  GSHSCENE_Allocator a = tracker::allocator_for(&t);
  {
    StoreHolder sh(nullptr, &a);
    ASSERT_NE(sh.store, nullptr);
    bool done = false;
    for (long k = 0; k < 200 && !done; k++) {
      t.persistent = false;
      t.failed = false;
      t.fail_from = t.calls + k;
      GSHSCENE_Result r;
      Ref p = intern_profile(sh.store, {{0, 1}, {5, 2}, {9, 0.5}}, &r);
      GSHSCENE_Result rs = GSHSCENE_OK;
      Ref style;
      if (r == GSHSCENE_OK) {
        GSHSCENE_Stroke_Style st = round_style(p.get());
        const GSHSCENE_Resource * s = nullptr;
        rs = gshscene_store_intern_stroke_style(sh.store, &st, &s);
        style = Ref(rs == GSHSCENE_OK ? s : nullptr);
      }
      t.fail_from = -1;
      if (r == GSHSCENE_OK && rs == GSHSCENE_OK) {
        done = true;
        EXPECT_FALSE(t.failed);
      } else {
        EXPECT_TRUE(r == GSHSCENE_ERR_OOM || (r == GSHSCENE_OK && rs == GSHSCENE_ERR_OOM)) << "allocation " << k;
      }
    }
    EXPECT_TRUE(done);
  }
  EXPECT_TRUE(t.live.empty()) << t.live.size() << " blocks leaked";
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
