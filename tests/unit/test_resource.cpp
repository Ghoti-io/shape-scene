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
 * The kinds of resource: paths, glyph runs and pictures (resource.h, AD-27,
 * CAP-16).
 */

#include "scene_helpers.h"

#include "../alloc_tracker.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <vector>

using namespace scene;

namespace {

const double kNaN = std::numeric_limits<double>::quiet_NaN();
const double kInf = std::numeric_limits<double>::infinity();

/** The font run of tools/gen-scene-vectors.py, from its face in @p store (held). */
struct FontRun {
  GSHSCENE_Glyph_Run run;
  std::vector<GSHSCENE_Glyph> glyphs;
  const GSHSCENE_Resource * face = nullptr;
  explicit FontRun(GSHSCENE_Store * store) {
    std::memset(&run, 0, sizeof run);
    glyphs = {{36, 0.0, 0.0, nullptr}, {37, 7.5, 0.25, nullptr}, {4294967295u, -1.0, 2.0, nullptr}};
    face = intern_face(store);
    run.source = GSHSCENE_GLYPHS_FROM_FONT;
    run.face = face;
    run.size = 12.5;
    run.hint_mode = GSHSCENE_HINT_NONE;
    run.hint_version = 40;
    sync();
  }
  FontRun(const FontRun & o) : run(o.run), glyphs(o.glyphs), face(gshscene_resource_retain(o.face)) { sync(); }
  FontRun & operator=(const FontRun &) = delete;
  ~FontRun() { gshscene_resource_release(face); }
  /** Name another face (whose reference this takes). */
  void use_face(const GSHSCENE_Resource * f) {
    gshscene_resource_release(face);
    face = f;
    run.face = f;
  }
  void sync() {
    run.glyph_count = glyphs.size();
    run.glyphs = glyphs.empty() ? nullptr : glyphs.data();
  }
};

/** A run of outlines. */
struct OutlineRun {
  GSHSCENE_Glyph_Run run;
  std::vector<GSHSCENE_Glyph> glyphs;
  OutlineRun(const GSHSCENE_Resource * a, const GSHSCENE_Resource * b) {
    std::memset(&run, 0, sizeof run);
    run.source = GSHSCENE_GLYPHS_FROM_OUTLINES;
    glyphs = {{1, 0.0, 0.0, a}, {2, 5.0, -3.0, b}};
    sync();
  }
  OutlineRun(const OutlineRun & o) : run(o.run), glyphs(o.glyphs) { sync(); }
  void sync() {
    run.glyph_count = glyphs.size();
    run.glyphs = glyphs.empty() ? nullptr : glyphs.data();
  }
};

const GSHSCENE_Resource * intern_run(GSHSCENE_Store * store, const GSHSCENE_Glyph_Run & run,
    GSHSCENE_Result * result = nullptr) {
  const GSHSCENE_Resource * r = nullptr;
  GSHSCENE_Result rc = gshscene_store_intern_glyph_run(store, &run, &r);
  if (result) {
    *result = rc;
  }
  return rc == GSHSCENE_OK ? r : nullptr;
}

/** A closed list with @p n fills of @p path at layers 0 .. n - 1. */
GSHSCENE_List * closed_fills(GSHSCENE_Store * store, const GSHSCENE_Resource * path, int n,
    const GSHSCENE_Limits * limits = nullptr) {
  GSHSCENE_List * list = nullptr;
  if (gshscene_list_create(nullptr, limits, store, &list) != GSHSCENE_OK) {
    return nullptr;
  }
  for (int i = 0; i < n; i++) {
    if (gshscene_list_fill(list, path, nullptr, GSHGEOM_FILL_NONZERO, nullptr, i, 0) != GSHSCENE_OK) {
      gshscene_list_destroy(list);
      return nullptr;
    }
  }
  gshscene_list_close(list);
  return list;
}

const GSHSCENE_Resource * freeze(GSHSCENE_Store * store, const GSHSCENE_List * list,
    GSHSCENE_Result * result = nullptr) {
  const GSHSCENE_Resource * r = nullptr;
  GSHSCENE_Result rc = gshscene_store_intern_picture(store, list, &r);
  if (result) {
    *result = rc;
  }
  return rc == GSHSCENE_OK ? r : nullptr;
}

}  // namespace

// --- kinds -------------------------------------------------------------------------

TEST(ResourceKind, EveryKindHasItsPinnedValueAndName) {
  EXPECT_EQ((int)GSHSCENE_RESOURCE_PATH, 1);
  EXPECT_EQ((int)GSHSCENE_RESOURCE_GLYPH_RUN, 2);
  EXPECT_EQ((int)GSHSCENE_RESOURCE_PICTURE, 3);
  EXPECT_EQ((int)GSHSCENE_RESOURCE_GRADIENT, 4);
  EXPECT_EQ((int)GSHSCENE_RESOURCE_IMAGE, 5);
  EXPECT_EQ((int)GSHSCENE_RESOURCE_FACE, 6);
  EXPECT_EQ((int)GSHSCENE_RESOURCE_PALETTE, 7);
  EXPECT_EQ((int)GSHSCENE_RESOURCE_STROKE_STYLE, 8);
  EXPECT_EQ((int)GSHSCENE_RESOURCE_PLANE, 9);
  EXPECT_EQ((int)GSHSCENE_RESOURCE_KIND_COUNT, 10);
  EXPECT_STREQ(gshscene_resource_kind_string(GSHSCENE_RESOURCE_PLANE), "plane");
  EXPECT_STREQ(gshscene_resource_kind_string(GSHSCENE_RESOURCE_STROKE_STYLE), "stroke-style");
  EXPECT_STREQ(gshscene_resource_kind_string(GSHSCENE_RESOURCE_FACE), "face");
  EXPECT_STREQ(gshscene_resource_kind_string(GSHSCENE_RESOURCE_PALETTE), "palette");
  EXPECT_STREQ(gshscene_resource_kind_string(GSHSCENE_RESOURCE_PATH), "path");
  EXPECT_STREQ(gshscene_resource_kind_string(GSHSCENE_RESOURCE_GLYPH_RUN), "glyph-run");
  EXPECT_STREQ(gshscene_resource_kind_string(GSHSCENE_RESOURCE_PICTURE), "picture");
  EXPECT_STREQ(gshscene_resource_kind_string(GSHSCENE_RESOURCE_GRADIENT), "gradient");
  EXPECT_STREQ(gshscene_resource_kind_string(GSHSCENE_RESOURCE_IMAGE), "image");
  EXPECT_STREQ(gshscene_resource_kind_string(GSHSCENE_RESOURCE_KIND_COUNT), "unknown resource kind");
  EXPECT_STREQ(gshscene_resource_kind_string((GSHSCENE_Resource_Kind)0), "unknown resource kind");
}

TEST(ResourceKind, AccessorsOfTheWrongKindAnswerNothing) {
  StoreHolder s(nullptr, nullptr, all_zero);
  Ref path(intern_square(s.store));
  FontRun fr(s.store);
  Ref run(intern_run(s.store, fr.run));
  ASSERT_NE(path.get(), nullptr);
  ASSERT_NE(run.get(), nullptr);
  GSHSCENE_Glyph_Run out;
  EXPECT_EQ(gshscene_resource_glyph_run(path.get(), &out), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_resource_glyph_run(nullptr, &out), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_resource_glyph_run(run.get(), nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_resource_path(run.get()), nullptr);
  EXPECT_EQ(gshscene_resource_path(nullptr), nullptr);
  EXPECT_EQ(gshscene_resource_picture(path.get()), nullptr);
  EXPECT_EQ(gshscene_resource_picture(run.get()), nullptr);
  EXPECT_EQ(gshscene_resource_picture(nullptr), nullptr);
  EXPECT_EQ(gshscene_resource_picture_depth(path.get()), 0u);
  EXPECT_EQ(gshscene_resource_picture_depth(run.get()), 0u);
  EXPECT_EQ(gshscene_resource_picture_depth(nullptr), 0u);
  GSHSCENE_List * list = closed_fills(s.store, path.get(), 1);
  Ref picture(freeze(s.store, list));
  gshscene_list_destroy(list);
  ASSERT_NE(picture.get(), nullptr);
  EXPECT_EQ(gshscene_resource_path(picture.get()), nullptr) << "a picture is not a path";
  OutlineRun orun(path.get(), path.get());
  Ref outlines(intern_run(s.store, orun.run));
  EXPECT_EQ(gshscene_resource_path(outlines.get()), nullptr) << "an outline run is not a path";
  EXPECT_EQ(gshscene_resource_picture(outlines.get()), nullptr);
  EXPECT_EQ(gshscene_resource_picture_depth(outlines.get()), 0u);
}

// --- paths --------------------------------------------------------------------------

TEST(PathResource, ReadsBackTheContentOfEveryVerb) {
  StoreHolder s(nullptr, nullptr, all_zero);
  GSHGEOM_Path * source = fixture_path();
  Ref r(intern(s.store, source));
  ASSERT_NE(r.get(), nullptr);
  EXPECT_EQ(gshscene_resource_kind(r.get()), GSHSCENE_RESOURCE_PATH);
  const GSHGEOM_Path * copy = gshscene_resource_path(r.get());
  ASSERT_NE(copy, nullptr);
  EXPECT_NE(copy, source) << "the store holds its own copy";
  EXPECT_EQ(gshgeom_path_hash(copy), gshgeom_path_hash(source));
  EXPECT_EQ(dump_of(gshgeom_path_dump, copy), dump_of(gshgeom_path_dump, source));
  gshgeom_path_destroy(source);
  EXPECT_EQ(gshgeom_path_element_count(copy), 7u) << "still there after the source is gone";
}

TEST(PathResource, AThreeDimensionalPathKeepsItsDimension) {
  StoreHolder s(nullptr, nullptr, all_zero);
  GSHGEOM_Path * source = new_path(3);
  gshgeom_path_move_to(source, P(1, 2, 3));
  gshgeom_path_line_to(source, P(4, 5, 6));
  Ref r(intern(s.store, source));
  ASSERT_NE(r.get(), nullptr);
  EXPECT_EQ(gshgeom_path_dimension(gshscene_resource_path(r.get())), 3);
  EXPECT_EQ(gshgeom_path_hash(gshscene_resource_path(r.get())), gshgeom_path_hash(source));
  gshgeom_path_destroy(source);
}

TEST(PathResource, AnEmptyPathIsAResource) {
  StoreHolder s(nullptr, nullptr, all_zero);
  GSHGEOM_Path * source = new_path();
  Ref r(intern(s.store, source));
  ASSERT_NE(r.get(), nullptr);
  EXPECT_EQ(gshgeom_path_element_count(gshscene_resource_path(r.get())), 0u);
  Ref again(intern(s.store, source));
  EXPECT_EQ(again.get(), r.get());
  gshgeom_path_destroy(source);
}

TEST(PathResource, TheStoreKeepsNothingOfTheCallersPath) {
  StoreHolder s(nullptr, nullptr, all_zero);
  GSHGEOM_Path * source = square_path();
  Ref r(intern(s.store, source));
  uint64_t before = gshgeom_path_hash(gshscene_resource_path(r.get()));
  gshgeom_path_line_to(source, P(9, 9));  // change the caller's path after the intern
  EXPECT_EQ(gshgeom_path_hash(gshscene_resource_path(r.get())), before);
  EXPECT_EQ(gshgeom_path_element_count(gshscene_resource_path(r.get())), 5u);
  gshgeom_path_destroy(source);
}


namespace {

/** The fixture path with the @p slot-th number of its elements raised by one unit in the last place. */
GSHGEOM_Path * fixture_variant(int slot) {
  int counter = 0;
  auto v = [&](double x) {
    return counter++ == slot ? std::nextafter(x, kInf) : x;
  };
  auto pt = [&](double x, double y) {
    double vx = v(x);
    double vy = v(y);
    return P(vx, vy);
  };
  GSHGEOM_Path * p = new_path();
  gshgeom_path_move_to(p, pt(1.5, -2.25));
  gshgeom_path_line_to(p, pt(3.0, 4.0));
  {
    GSHGEOM_Point c = pt(5.0, 6.5), e = pt(7.0, 8.0);
    gshgeom_path_quadratic_to(p, c, e);
  }
  {
    GSHGEOM_Point c1 = pt(9.0, 10.0), c2 = pt(11.25, 12.0), e = pt(13.0, 14.0);
    gshgeom_path_cubic_to(p, c1, c2, e);
  }
  {
    GSHGEOM_Point c = pt(15.0, 16.0), e = pt(17.0, 18.0);
    double w[3];
    for (double & x : w) {
      x = 0;
    }
    const double base[3] = {1.0, 0.5, 2.0};
    for (int i = 0; i < 3; i++) {
      w[i] = v(base[i]);
    }
    gshgeom_path_rational_quadratic_to(p, c, e, w);
  }
  {
    GSHGEOM_Point c1 = pt(19.0, 20.0), c2 = pt(21.0, 22.0), e = pt(23.0, 24.1);
    const double base[4] = {1.0, 2.0, 0.5, 4.0};
    double w[4];
    for (int i = 0; i < 4; i++) {
      w[i] = v(base[i]);
    }
    gshgeom_path_rational_cubic_to(p, c1, c2, e, w);
  }
  gshgeom_path_close(p);
  return p;
}

}  // namespace

TEST(PathResource, EveryNumberOfEveryVerbIsPartOfTheContent) {
  StoreHolder s(nullptr, nullptr, all_zero);
  GSHGEOM_Path * base = fixture_path();
  Ref baseline(intern(s.store, base));
  gshgeom_path_destroy(base);
  ASSERT_NE(baseline.get(), nullptr);
  std::set<const GSHSCENE_Resource *> distinct;
  distinct.insert(baseline.get());
  std::vector<Ref *> held;
  // 2 + 2 + 4 + 6 + (4 + 3) + (6 + 4) numbers.
  for (int slot = 0; slot < 31; slot++) {
    GSHGEOM_Path * p = fixture_variant(slot);
    Ref * r = new Ref(intern(s.store, p));
    gshgeom_path_destroy(p);
    held.push_back(r);
    ASSERT_NE(r->get(), nullptr) << slot;
    EXPECT_TRUE(distinct.insert(r->get()).second) << "slot " << slot << " does not change the content";
  }
  GSHGEOM_Path * unchanged = fixture_variant(31);  // past the last number: the fixture itself
  Ref same(intern(s.store, unchanged));
  gshgeom_path_destroy(unchanged);
  EXPECT_EQ(same.get(), baseline.get());
  for (Ref * r : held) {
    delete r;
  }
}

TEST(PathResource, TheSignOfAZeroIsPartOfTheContentInEveryCoordinate) {
  StoreHolder s(nullptr, nullptr, all_zero);
  std::set<const GSHSCENE_Resource *> distinct;
  std::vector<Ref *> held;
  for (int dim : {2, 3}) {
    auto build = [&](int negative_slot) {
      // The square's zeros, in order: for each of the four points, x then y (then z in 3D).
      int counter = 0;
      auto z = [&](double x) {
        return (x == 0.0 && counter++ == negative_slot) ? -0.0 : x;
      };
      GSHGEOM_Path * p = new_path(dim);
      const double pts[4][2] = {{0, 0}, {4, 0}, {4, 4}, {0, 4}};
      for (int i = 0; i < 4; i++) {
        double x = z(pts[i][0]);
        double y = z(pts[i][1]);
        double zz = z(0.0);
        GSHGEOM_Point pt = P(x, y, zz);
        if (i == 0) {
          gshgeom_path_move_to(p, pt);
        } else {
          gshgeom_path_line_to(p, pt);
        }
      }
      return p;
    };
    GSHGEOM_Path * base = build(-1);
    Ref * baseline = new Ref(intern(s.store, base));
    gshgeom_path_destroy(base);
    held.push_back(baseline);
    ASSERT_NE(baseline->get(), nullptr);
    EXPECT_TRUE(distinct.insert(baseline->get()).second);
    // 2D: the zeros are M.x, M.y, M.z, L1.y, L1.z, L2.z, L3.x, L3.z; in 3D the same eight and all are kept.
    for (int slot = 0; slot < 8; slot++) {
      GSHGEOM_Path * p = build(slot);
      Ref * r = new Ref(intern(s.store, p));
      gshgeom_path_destroy(p);
      held.push_back(r);
      ASSERT_NE(r->get(), nullptr);
      if (dim == 3 || slot == 0 || slot == 1 || slot == 3 || slot == 6) {
        // A zero a path of this dimension keeps: x or y in 2D, any of them in 3D.
        EXPECT_TRUE(distinct.insert(r->get()).second) << "dim " << dim << " slot " << slot;
      } else {
        EXPECT_EQ(r->get(), baseline->get()) << "z of a 2D path is not content (dim " << dim << " slot " << slot << ")";
      }
    }
  }
  for (Ref * r : held) {
    delete r;
  }
}

TEST(PathResource, MaxResourceElementsAcceptsExactlyTheLimit) {
  GSHSCENE_Limits limits;
  gshscene_limits_default(&limits);
  limits.max_resource_elements = 50;
  tracker::Tracker t;
  GSHSCENE_Allocator alloc = tracker::allocator_for(&t);
  {
    StoreHolder s(&limits, &alloc, all_zero);
    GSHGEOM_Path * fits = long_path(50);
    GSHGEOM_Path * over = long_path(51);
    Ref ok(intern(s.store, fits));
    ASSERT_NE(ok.get(), nullptr);
    EXPECT_EQ(gshgeom_path_element_count(gshscene_resource_path(ok.get())), 50u);
    const GSHSCENE_Resource * r = nullptr;
    EXPECT_EQ(gshscene_store_intern_path(s.store, over, &r), GSHSCENE_ERR_LIMIT);
    EXPECT_EQ(r, nullptr);
    GSHSCENE_Store_Stats stats;
    ASSERT_EQ(gshscene_store_stats(s.store, &stats), GSHSCENE_OK);
    EXPECT_EQ(stats.live, 1u) << "a refused path leaves nothing behind";
    gshgeom_path_destroy(fits);
    gshgeom_path_destroy(over);
  }
  EXPECT_TRUE(t.live.empty()) << "and nothing allocated";
}

TEST(PathResource, ElementsOfDifferentVerbsWithTheSamePointsAreDifferentContent) {
  StoreHolder s(nullptr, nullptr, all_zero);
  std::vector<Ref *> held;
  std::set<const GSHSCENE_Resource *> distinct;
  auto add = [&](const char * what, GSHGEOM_Path * p) {
    Ref * r = new Ref(intern(s.store, p));
    gshgeom_path_destroy(p);
    held.push_back(r);
    ASSERT_NE(r->get(), nullptr) << what;
    EXPECT_TRUE(distinct.insert(r->get()).second) << what << " merged with an earlier path";
  };
  const double unit[4] = {1.0, 1.0, 1.0, 1.0};
  {
    GSHGEOM_Path * p = new_path();
    gshgeom_path_move_to(p, P(0, 0));
    gshgeom_path_move_to(p, P(1, 1));
    add("a move to (1, 1)", p);
  }
  {
    GSHGEOM_Path * p = new_path();
    gshgeom_path_move_to(p, P(0, 0));
    gshgeom_path_line_to(p, P(1, 1));
    add("a line to (1, 1)", p);
  }
  {
    GSHGEOM_Path * p = new_path();
    gshgeom_path_move_to(p, P(0, 0));
    gshgeom_path_quadratic_to(p, P(2, 0), P(1, 1));
    add("a quadratic", p);
  }
  {
    GSHGEOM_Path * p = new_path();
    gshgeom_path_move_to(p, P(0, 0));
    gshgeom_path_rational_quadratic_to(p, P(2, 0), P(1, 1), unit);
    add("a rational quadratic of unit weights, the same points", p);
  }
  {
    GSHGEOM_Path * p = new_path();
    gshgeom_path_move_to(p, P(0, 0));
    gshgeom_path_cubic_to(p, P(2, 0), P(0, 2), P(1, 1));
    add("a cubic", p);
  }
  {
    GSHGEOM_Path * p = new_path();
    gshgeom_path_move_to(p, P(0, 0));
    gshgeom_path_rational_cubic_to(p, P(2, 0), P(0, 2), P(1, 1), unit);
    add("a rational cubic of unit weights, the same points", p);
  }
  {
    GSHGEOM_Path * p = new_path();
    gshgeom_path_move_to(p, P(0, 0));
    gshgeom_path_line_to(p, P(1, 1));
    gshgeom_path_close(p);
    add("the line, closed", p);
  }
  for (Ref * r : held) {
    delete r;
  }
}

TEST(PathResource, APathThatIsAPrefixOfAnotherIsNotMergedWithItWhicheverIsFirst) {
  auto open_square = []() {
    GSHGEOM_Path * p = new_path();
    gshgeom_path_move_to(p, P(0, 0));
    gshgeom_path_line_to(p, P(4, 0));
    gshgeom_path_line_to(p, P(4, 4));
    gshgeom_path_line_to(p, P(0, 4));
    return p;
  };
  {
    StoreHolder s(nullptr, nullptr, all_zero);
    GSHGEOM_Path * shorter = open_square();
    GSHGEOM_Path * longer = square_path();
    Ref a(intern(s.store, shorter)), b(intern(s.store, longer));
    EXPECT_NE(a.get(), b.get()) << "the shorter first";
    gshgeom_path_destroy(shorter);
    gshgeom_path_destroy(longer);
  }
  {
    StoreHolder s(nullptr, nullptr, all_zero);
    GSHGEOM_Path * shorter = open_square();
    GSHGEOM_Path * longer = square_path();
    Ref b(intern(s.store, longer)), a(intern(s.store, shorter));
    EXPECT_NE(a.get(), b.get()) << "the longer first";
    gshgeom_path_destroy(shorter);
    gshgeom_path_destroy(longer);
  }
}

// --- glyph runs ---------------------------------------------------------------------

TEST(GlyphRun, AFontRunReadsBackAndCopiesItsGlyphs) {
  StoreHolder s(nullptr, nullptr, all_zero);
  FontRun fr(s.store);
  Ref r(intern_run(s.store, fr.run));
  ASSERT_NE(r.get(), nullptr);
  EXPECT_EQ(gshscene_resource_kind(r.get()), GSHSCENE_RESOURCE_GLYPH_RUN);
  GSHSCENE_Glyph_Run out;
  std::memset(&out, 0xAB, sizeof out);
  ASSERT_EQ(gshscene_resource_glyph_run(r.get(), &out), GSHSCENE_OK);
  EXPECT_EQ(out.source, GSHSCENE_GLYPHS_FROM_FONT);
  EXPECT_EQ(out.face, fr.face);
  EXPECT_EQ(out.size, 12.5);
  EXPECT_EQ(out.hint_mode, 1u);
  EXPECT_EQ(out.hint_version, 40u);
  ASSERT_EQ(out.glyph_count, 3u);
  EXPECT_NE(out.glyphs, fr.run.glyphs) << "the glyph array is the store's own";
  EXPECT_EQ(out.glyphs[0].id, 36u);
  EXPECT_EQ(out.glyphs[1].x, 7.5);
  EXPECT_EQ(out.glyphs[1].y, 0.25);
  EXPECT_EQ(out.glyphs[2].id, 4294967295u);
  EXPECT_EQ(out.glyphs[2].x, -1.0);
  EXPECT_EQ(out.glyphs[2].outline, nullptr);
  fr.glyphs[0].id = 99;  // the caller's array may change afterwards
  EXPECT_EQ(out.glyphs[0].id, 36u);
}

TEST(GlyphRun, AnOutlineRunRetainsItsOutlinesAndReleasesThemWithTheRun) {
  StoreHolder s(nullptr, nullptr, all_zero);
  Ref a(intern_square(s.store)), b(intern_fixture(s.store));
  ASSERT_NE(a.get(), nullptr);
  ASSERT_NE(b.get(), nullptr);
  EXPECT_EQ(gshscene_resource_refcount(a.get()), 1u);
  OutlineRun orun(a.get(), b.get());
  const GSHSCENE_Resource * run = intern_run(s.store, orun.run);
  ASSERT_NE(run, nullptr);
  EXPECT_EQ(gshscene_resource_refcount(a.get()), 2u);
  EXPECT_EQ(gshscene_resource_refcount(b.get()), 2u);
  GSHSCENE_Glyph_Run out;
  ASSERT_EQ(gshscene_resource_glyph_run(run, &out), GSHSCENE_OK);
  EXPECT_EQ(out.source, GSHSCENE_GLYPHS_FROM_OUTLINES);
  EXPECT_EQ(out.glyphs[0].outline, a.get());
  EXPECT_EQ(out.glyphs[1].outline, b.get());
  // An equal run is a hit and takes nothing more.
  const GSHSCENE_Resource * again = intern_run(s.store, orun.run);
  EXPECT_EQ(again, run);
  EXPECT_EQ(gshscene_resource_refcount(a.get()), 2u);
  gshscene_resource_release(again);
  gshscene_resource_release(run);
  EXPECT_EQ(gshscene_resource_refcount(a.get()), 1u);
  EXPECT_EQ(gshscene_resource_refcount(b.get()), 1u);
}

TEST(GlyphRun, EqualRunsShareAHandleAndOneDifferenceSeparatesThem) {
  StoreHolder s(nullptr, nullptr, all_zero);
  Ref sq(intern_square(s.store)), sq2(intern_square(s.store, 1.0));
  FontRun base(s.store);
  Ref baseline(intern_run(s.store, base.run));
  ASSERT_NE(baseline.get(), nullptr);
  {
    FontRun same(s.store);
    Ref again(intern_run(s.store, same.run));
    EXPECT_EQ(again.get(), baseline.get());
  }
  std::vector<Ref *> others;
  auto differs = [&](const char * what, auto mutate) {
    FontRun fr(s.store);
    mutate(fr);
    fr.sync();
    Ref * r = new Ref(intern_run(s.store, fr.run));
    others.push_back(r);
    ASSERT_NE(r->get(), nullptr) << what;
    EXPECT_NE(r->get(), baseline.get()) << what;
  };
  differs("font key", [&](FontRun & f) { f.use_face(intern_face(s.store, UINT64_C(0x1122334455667789))); });
  differs("face index", [&](FontRun & f) { f.use_face(intern_face(s.store, UINT64_C(0x1122334455667788), 3)); });
  differs("a coordinate", [&](FontRun & f) { f.use_face(intern_face(s.store, UINT64_C(0x1122334455667788), 2, 8192, -4095)); });
  differs("size", [](FontRun & f) { f.run.size = std::nextafter(f.run.size, 100.0); });
  differs("hint mode", [](FontRun & f) { f.run.hint_mode += 1; });
  differs("hint version", [](FontRun & f) { f.run.hint_version += 1; });
  differs("glyph count", [](FontRun & f) { f.glyphs.pop_back(); });
  differs("a glyph id", [](FontRun & f) { f.glyphs[1].id += 1; });
  differs("a glyph x", [](FontRun & f) { f.glyphs[1].x = std::nextafter(f.glyphs[1].x, 100.0); });
  differs("a glyph y", [](FontRun & f) { f.glyphs[1].y = std::nextafter(f.glyphs[1].y, 100.0); });
  differs("the sign of a zero position", [](FontRun & f) { f.glyphs[0].x = -0.0; });
  differs("the order of glyphs", [](FontRun & f) { std::swap(f.glyphs[0], f.glyphs[1]); });
  std::set<const GSHSCENE_Resource *> distinct;
  distinct.insert(baseline.get());
  for (Ref * r : others) {
    EXPECT_TRUE(distinct.insert(r->get()).second);
  }
  for (Ref * r : others) {
    delete r;
  }
  // Outline runs: a different outline is a different run, and so is the source.
  OutlineRun o1(sq.get(), sq.get()), o2(sq.get(), sq2.get());
  Ref r1(intern_run(s.store, o1.run)), r2(intern_run(s.store, o2.run));
  ASSERT_NE(r1.get(), nullptr);
  ASSERT_NE(r2.get(), nullptr);
  EXPECT_NE(r1.get(), r2.get());
  EXPECT_NE(r1.get(), baseline.get());
}

TEST(GlyphRun, AnEmptyRunIsValidForBothSources) {
  StoreHolder s(nullptr, nullptr, all_zero);
  FontRun fr(s.store);
  fr.glyphs.clear();
  fr.sync();
  Ref font(intern_run(s.store, fr.run));
  ASSERT_NE(font.get(), nullptr);
  GSHSCENE_Glyph_Run out;
  ASSERT_EQ(gshscene_resource_glyph_run(font.get(), &out), GSHSCENE_OK);
  EXPECT_EQ(out.glyph_count, 0u);
  GSHSCENE_Glyph_Run empty_outlines;
  std::memset(&empty_outlines, 0, sizeof empty_outlines);
  empty_outlines.source = GSHSCENE_GLYPHS_FROM_OUTLINES;
  Ref outlines(intern_run(s.store, empty_outlines));
  ASSERT_NE(outlines.get(), nullptr);
  EXPECT_NE(outlines.get(), font.get());
}

TEST(GlyphRun, EveryForbiddenFieldIsRefusedAndNothingIsLeftBehind) {
  StoreHolder s(nullptr, nullptr, all_zero);
  Ref sq(intern_square(s.store));
  StoreHolder other;
  Ref foreign(intern_square(other.store));
  Ref foreign_face(intern_face(other.store));
  FontRun fr0(s.store);
  Ref somerun(intern_run(s.store, fr0.run));
  GSHSCENE_Store_Stats before;
  ASSERT_EQ(gshscene_store_stats(s.store, &before), GSHSCENE_OK);

  auto refused = [&](const char * what, GSHSCENE_Result want, const GSHSCENE_Glyph_Run & run) {
    GSHSCENE_Result rc = GSHSCENE_OK;
    const GSHSCENE_Resource * r = intern_run(s.store, run, &rc);
    EXPECT_EQ(rc, want) << what;
    EXPECT_EQ(r, nullptr) << what;
    gshscene_resource_release(r);
  };
  auto font = [&](auto mutate) {
    FontRun f(s.store);
    mutate(f);
    f.sync();
    return f;
  };
  auto outlines = [&](auto mutate) {
    OutlineRun o(sq.get(), sq.get());
    mutate(o);
    o.sync();
    return o;
  };
  const GSHSCENE_Result INV = GSHSCENE_ERR_INVALID;

  refused("no face", INV, font([](FontRun & f) { f.run.face = nullptr; }).run);
  refused("a face of another store", INV, font([&](FontRun & f) { f.run.face = foreign_face.get(); }).run);
  refused("a path as the face", INV, font([&](FontRun & f) { f.run.face = sq.get(); }).run);
  refused("a hint mode outside the enum", INV, font([](FontRun & f) { f.run.hint_mode = GSHSCENE_HINT_MODE_COUNT; }).run);
  refused("size 0", INV, font([](FontRun & f) { f.run.size = 0.0; }).run);
  refused("size -0", INV, font([](FontRun & f) { f.run.size = -0.0; }).run);
  refused("negative size", INV, font([](FontRun & f) { f.run.size = -1.0; }).run);
  refused("NaN size", INV, font([](FontRun & f) { f.run.size = kNaN; }).run);
  refused("infinite size", INV, font([](FontRun & f) { f.run.size = kInf; }).run);
  refused("huge size", INV, font([](FontRun & f) { f.run.size = std::ldexp(1.0, 101); }).run);
  refused("NaN position", INV, font([](FontRun & f) { f.glyphs[0].x = kNaN; }).run);
  refused("infinite position", INV, font([](FontRun & f) { f.glyphs[1].y = kInf; }).run);
  refused("huge x", INV, font([](FontRun & f) { f.glyphs[2].x = std::ldexp(1.0, 101); }).run);
  refused("huge negative y", INV, font([](FontRun & f) { f.glyphs[2].y = -std::ldexp(1.0, 101); }).run);
  refused("an outline on a font glyph", INV, font([&](FontRun & f) { f.glyphs[0].outline = sq.get(); }).run);
  refused("source outside the enum", INV, font([](FontRun & f) { f.run.source = (GSHSCENE_Glyph_Source)7; }).run);
  refused("source at the end of the enum", INV, font([](FontRun & f) { f.run.source = GSHSCENE_GLYPH_SOURCE_COUNT; }).run);
  refused("an otherwise valid outline run, source outside the enum", INV,
      outlines([](OutlineRun & o) { o.run.source = (GSHSCENE_Glyph_Source)7; }).run);
  refused("an otherwise valid outline run, source at the end of the enum", INV,
      outlines([](OutlineRun & o) { o.run.source = GSHSCENE_GLYPH_SOURCE_COUNT; }).run);
  refused("NaN y", INV, font([](FontRun & f) { f.glyphs[1].y = kNaN; }).run);
  refused("NaN x", INV, font([](FontRun & f) { f.glyphs[2].x = kNaN; }).run);
  {
    FontRun f(s.store);
    f.run.glyphs = nullptr;  // a count with no array
    refused("glyphs NULL with a count", INV, f.run);
  }

  refused("outline NULL", INV, outlines([](OutlineRun & o) { o.glyphs[1].outline = nullptr; }).run);
  refused("a face on an outline run", INV, outlines([&](OutlineRun & o) { o.run.face = fr0.face; }).run);
  refused("a hint version on an outline run", INV, outlines([](OutlineRun & o) { o.run.hint_version = 1; }).run);
  refused("a size on an outline run", INV, outlines([](OutlineRun & o) { o.run.size = 1.0; }).run);
  refused("a size of -0 on an outline run", INV, outlines([](OutlineRun & o) { o.run.size = -0.0; }).run);
  refused("a hint mode on an outline run", INV, outlines([](OutlineRun & o) { o.run.hint_mode = 1; }).run);
  refused("a foreign outline", INV, outlines([&](OutlineRun & o) { o.glyphs[0].outline = foreign.get(); }).run);
  refused("an outline that is a glyph run", INV, outlines([&](OutlineRun & o) { o.glyphs[0].outline = somerun.get(); }).run);
  EXPECT_EQ(gshscene_store_intern_glyph_run(s.store, nullptr, nullptr), GSHSCENE_ERR_INVALID);
  const GSHSCENE_Resource * r = nullptr;
  EXPECT_EQ(gshscene_store_intern_glyph_run(s.store, &fr0.run, nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_store_intern_glyph_run(nullptr, &fr0.run, &r), GSHSCENE_ERR_INVALID);
  OutlineRun orun(sq.get(), sq.get());
  EXPECT_EQ(gshscene_store_intern_glyph_run(nullptr, &orun.run, &r), GSHSCENE_ERR_INVALID) << "an outline run too";
  EXPECT_EQ(gshscene_store_intern_glyph_run(s.store, nullptr, &r), GSHSCENE_ERR_INVALID);

  GSHSCENE_Store_Stats after;
  ASSERT_EQ(gshscene_store_stats(s.store, &after), GSHSCENE_OK);
  EXPECT_EQ(after.live, before.live);
  EXPECT_EQ(gshscene_resource_refcount(sq.get()), 1u) << "a refused run takes no reference";
}

TEST(GlyphRun, TheLargestPositionsAndSizeAreAccepted) {
  StoreHolder s(nullptr, nullptr, all_zero);
  FontRun fr(s.store);
  fr.run.size = GSHGEOM_COORD_LIMIT;
  fr.glyphs[0].x = GSHGEOM_COORD_LIMIT;
  fr.glyphs[0].y = -GSHGEOM_COORD_LIMIT;
  Ref r(intern_run(s.store, fr.run));
  EXPECT_NE(r.get(), nullptr);
}

TEST(GlyphRun, MaxResourceElementsAcceptsExactlyTheLimitInGlyphs) {
  GSHSCENE_Limits limits;
  gshscene_limits_default(&limits);
  limits.max_resource_elements = 10;
  StoreHolder s(&limits, nullptr, all_zero);
  FontRun fr(s.store);
  fr.glyphs.assign(10, GSHSCENE_Glyph{1, 0.0, 0.0, nullptr});
  fr.sync();
  Ref ok(intern_run(s.store, fr.run));
  EXPECT_NE(ok.get(), nullptr);
  fr.glyphs.push_back(GSHSCENE_Glyph{1, 0.0, 0.0, nullptr});
  fr.sync();
  GSHSCENE_Result rc = GSHSCENE_OK;
  EXPECT_EQ(intern_run(s.store, fr.run, &rc), nullptr);
  EXPECT_EQ(rc, GSHSCENE_ERR_LIMIT);
}


TEST(GlyphRun, ARunWithAnExtraOrAMissingGlyphIsAnotherRunWhicheverWasInternedFirst) {
  for (int order = 0; order < 2; order++) {
    StoreHolder s(nullptr, nullptr, all_zero);
    FontRun base(s.store);
    FontRun extra(s.store);
    extra.glyphs.push_back(GSHSCENE_Glyph{36, 0.0, 0.0, nullptr});
    extra.sync();
    FontRun fewer(s.store);
    fewer.glyphs.pop_back();
    fewer.sync();
    if (order == 0) {
      Ref a(intern_run(s.store, base.run)), b(intern_run(s.store, extra.run)), c(intern_run(s.store, fewer.run));
      EXPECT_NE(a.get(), b.get()) << "base first, then a longer run with the same first glyphs";
      EXPECT_NE(a.get(), c.get());
      EXPECT_NE(b.get(), c.get());
    } else {
      Ref c(intern_run(s.store, fewer.run)), a(intern_run(s.store, base.run));
      EXPECT_NE(a.get(), c.get()) << "the shorter run first, then the longer one";
    }
  }
}

TEST(GlyphRun, ThePositionsRangeIsCheckedOnEverySideOfEveryAxis) {
  StoreHolder s(nullptr, nullptr, all_zero);
  const double big = std::ldexp(1.0, 101);
  for (int slot = 0; slot < 4; slot++) {
    FontRun fr(s.store);
    GSHSCENE_Glyph & g = fr.glyphs[1];
    switch (slot) {
      case 0: g.x = big; break;
      case 1: g.x = -big; break;
      case 2: g.y = big; break;
      default: g.y = -big; break;
    }
    GSHSCENE_Result rc = GSHSCENE_OK;
    EXPECT_EQ(intern_run(s.store, fr.run, &rc), nullptr) << slot;
    EXPECT_EQ(rc, GSHSCENE_ERR_INVALID) << slot;
    // And the limit itself, either sign, is kept.
    FontRun edge(s.store);
    GSHSCENE_Glyph & e = edge.glyphs[1];
    switch (slot) {
      case 0: e.x = GSHGEOM_COORD_LIMIT; break;
      case 1: e.x = -GSHGEOM_COORD_LIMIT; break;
      case 2: e.y = GSHGEOM_COORD_LIMIT; break;
      default: e.y = -GSHGEOM_COORD_LIMIT; break;
    }
    Ref ok(intern_run(s.store, edge.run, &rc));
    EXPECT_NE(ok.get(), nullptr) << slot;
  }
  FontRun fr(s.store);
  fr.run.size = std::ldexp(GSHGEOM_COORD_LIMIT, 1) ;
  GSHSCENE_Result rc = GSHSCENE_OK;
  EXPECT_EQ(intern_run(s.store, fr.run, &rc), nullptr);
  EXPECT_EQ(rc, GSHSCENE_ERR_INVALID);
}

TEST(GlyphRun, ACountWhoseArrayWouldOverflowIsALimitAndTheArrayIsNeverRead) {
  GSHSCENE_Limits limits;
  gshscene_limits_default(&limits);
  limits.max_resource_elements = SIZE_MAX;
  StoreHolder s(&limits, nullptr, all_zero);
  FontRun fr(s.store);
  GSHSCENE_Glyph_Run run = fr.run;
  run.glyph_count = SIZE_MAX / sizeof(GSHSCENE_Glyph) + 1;
  GSHSCENE_Result rc = GSHSCENE_OK;
  EXPECT_EQ(intern_run(s.store, run, &rc), nullptr);
  EXPECT_EQ(rc, GSHSCENE_ERR_LIMIT);
}

TEST(GlyphRun, AnAllocationFailureAtEveryCallLeavesNothingAndNoReference) {
  GSHGEOM_Path * sqp = square_path();
  for (long k = 0;; k++) {
    tracker::Tracker t;
    GSHSCENE_Allocator a = tracker::allocator_for(&t);
    GSHSCENE_Store * store = nullptr;
    ASSERT_EQ(gshscene_store_create(&a, nullptr, &store), GSHSCENE_OK);
    const GSHSCENE_Resource * sq = intern(store, sqp);
    ASSERT_NE(sq, nullptr);
    OutlineRun orun(sq, sq);
    t.fail_from = t.calls + k;
    GSHSCENE_Result rc = GSHSCENE_OK;
    const GSHSCENE_Resource * r = intern_run(store, orun.run, &rc);
    t.fail_from = -1;
    if (r) {
      EXPECT_EQ(gshscene_resource_refcount(sq), 3u);
    } else {
      EXPECT_EQ(rc, GSHSCENE_ERR_OOM) << k;
      EXPECT_EQ(gshscene_resource_refcount(sq), 1u) << "a failed intern keeps no reference, k=" << k;
    }
    gshscene_resource_release(r);
    gshscene_resource_release(sq);
    gshscene_store_destroy(store);
    EXPECT_TRUE(t.live.empty()) << "k=" << k;
    if (!t.failed) {
      break;
    }
  }
  gshgeom_path_destroy(sqp);
}

TEST(GlyphRun, TheContentHashDependsOnTheOutlinesContentNotOnTheStore) {
  StoreHolder s1(nullptr, nullptr, all_zero), s2(nullptr, nullptr, all_zero);
  Ref a1(intern_square(s1.store)), b1(intern_fixture(s1.store));
  Ref b2(intern_fixture(s2.store)), a2(intern_square(s2.store));  // the other order
  OutlineRun o1(a1.get(), b1.get()), o2(a2.get(), b2.get());
  Ref r1(intern_run(s1.store, o1.run)), r2(intern_run(s2.store, o2.run));
  ASSERT_NE(r1.get(), nullptr);
  ASSERT_NE(r2.get(), nullptr);
  EXPECT_EQ(gshscene_resource_hash(r1.get()), gshscene_resource_hash(r2.get()));
  EXPECT_NE(gshscene_resource_id(a1.get()), gshscene_resource_id(a2.get())) << "ids differ, hashes do not";
  EXPECT_EQ(dump_resource(r1.get()), dump_resource(r2.get()));
}

// --- pictures -----------------------------------------------------------------------

TEST(Picture, EqualListsFreezeToOneHandleAndADifferenceSeparatesThem) {
  StoreHolder s(nullptr, nullptr, all_zero);
  Ref sq(intern_square(s.store));
  GSHSCENE_List * l1 = closed_fills(s.store, sq.get(), 3);
  GSHSCENE_List * l2 = closed_fills(s.store, sq.get(), 3);
  GSHSCENE_List * l3 = closed_fills(s.store, sq.get(), 4);
  Ref p1(freeze(s.store, l1)), p2(freeze(s.store, l2)), p3(freeze(s.store, l3));
  ASSERT_NE(p1.get(), nullptr);
  EXPECT_EQ(p1.get(), p2.get());
  EXPECT_NE(p1.get(), p3.get());
  EXPECT_EQ(gshscene_resource_kind(p1.get()), GSHSCENE_RESOURCE_PICTURE);
  // A list that differs only in a layer.
  GSHSCENE_List * l4 = nullptr;
  ASSERT_EQ(gshscene_list_create(nullptr, nullptr, s.store, &l4), GSHSCENE_OK);
  for (int i = 0; i < 3; i++) {
    ASSERT_EQ(gshscene_list_fill(l4, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, i == 2 ? 9 : i, 0), GSHSCENE_OK);
  }
  gshscene_list_close(l4);
  Ref p4(freeze(s.store, l4));
  EXPECT_NE(p4.get(), p1.get());
  gshscene_list_destroy(l1);
  gshscene_list_destroy(l2);
  gshscene_list_destroy(l3);
  gshscene_list_destroy(l4);
}

TEST(Picture, IsACopyThatHoldsTheItemsReferences) {
  StoreHolder s(nullptr, nullptr, all_zero);
  Ref sq(intern_square(s.store));
  GSHSCENE_List * list = closed_fills(s.store, sq.get(), 2);
  ASSERT_NE(list, nullptr);
  EXPECT_EQ(gshscene_resource_refcount(sq.get()), 3u) << "the handle and the two items of the list";
  const GSHSCENE_Resource * picture = freeze(s.store, list);
  ASSERT_NE(picture, nullptr);
  EXPECT_EQ(gshscene_resource_refcount(sq.get()), 5u) << "the picture's copy holds two more";
  const GSHSCENE_List * inside = gshscene_resource_picture(picture);
  ASSERT_NE(inside, nullptr);
  EXPECT_NE(inside, list);
  EXPECT_TRUE(gshscene_list_is_closed(inside));
  EXPECT_EQ(gshscene_list_equal(inside, list), 1);
  gshscene_list_destroy(list);
  EXPECT_EQ(gshscene_list_count(inside), 2u) << "the picture outlives the list it was made from";
  EXPECT_EQ(gshscene_resource_refcount(sq.get()), 3u);
  gshscene_resource_release(picture);
  EXPECT_EQ(gshscene_resource_refcount(sq.get()), 1u);
}

TEST(Picture, AListThatFillsAGlyphRunIsNotMistakenForOneThatInstancesAPicture) {
  StoreHolder s(nullptr, nullptr, all_zero);
  FontRun fr(s.store);
  Ref run(intern_run(s.store, fr.run));
  GSHSCENE_List * list = nullptr;
  ASSERT_EQ(gshscene_list_create(nullptr, nullptr, s.store, &list), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(list, run.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0), GSHSCENE_OK);
  gshscene_list_close(list);
  Ref picture(freeze(s.store, list));
  gshscene_list_destroy(list);
  ASSERT_NE(picture.get(), nullptr);
  EXPECT_EQ(gshscene_resource_picture_depth(picture.get()), 1u);
}

TEST(Picture, AnEmptyClosedListIsAPicture) {
  StoreHolder s(nullptr, nullptr, all_zero);
  Ref sq(intern_square(s.store));
  GSHSCENE_List * list = closed_fills(s.store, sq.get(), 0);
  ASSERT_NE(list, nullptr);
  Ref p(freeze(s.store, list));
  ASSERT_NE(p.get(), nullptr);
  EXPECT_EQ(gshscene_resource_picture_depth(p.get()), 1u);
  EXPECT_EQ(gshscene_list_count(gshscene_resource_picture(p.get())), 0u);
  gshscene_list_destroy(list);
}

TEST(Picture, RefusesAnOpenListAForeignListAndNull) {
  StoreHolder s(nullptr, nullptr, all_zero), other(nullptr, nullptr, all_zero);
  Ref sq(intern_square(s.store)), foreign(intern_square(other.store));
  GSHSCENE_List * open = nullptr;
  ASSERT_EQ(gshscene_list_create(nullptr, nullptr, s.store, &open), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(open, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0), GSHSCENE_OK);
  GSHSCENE_List * foreign_list = closed_fills(other.store, foreign.get(), 1);
  GSHSCENE_Result rc;
  EXPECT_EQ(freeze(s.store, open, &rc), nullptr);
  EXPECT_EQ(rc, GSHSCENE_ERR_INVALID);
  EXPECT_EQ(freeze(s.store, foreign_list, &rc), nullptr);
  EXPECT_EQ(rc, GSHSCENE_ERR_INVALID);
  EXPECT_EQ(freeze(s.store, nullptr, &rc), nullptr);
  EXPECT_EQ(rc, GSHSCENE_ERR_INVALID);
  const GSHSCENE_Resource * r = nullptr;
  GSHSCENE_List * closed = closed_fills(s.store, sq.get(), 1);
  EXPECT_EQ(gshscene_store_intern_picture(nullptr, open, &r), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_store_intern_picture(s.store, open, nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_store_intern_picture(s.store, closed, nullptr), GSHSCENE_ERR_INVALID);
  gshscene_list_destroy(closed);
  gshscene_list_destroy(open);
  gshscene_list_destroy(foreign_list);
}

TEST(Picture, DepthCountsNestedPicturesAndTheLimitIsExact) {
  GSHSCENE_Limits limits;
  gshscene_limits_default(&limits);
  limits.max_picture_depth = 3;
  StoreHolder s(&limits, nullptr, all_zero);
  Ref sq(intern_square(s.store));
  GSHSCENE_List * base = closed_fills(s.store, sq.get(), 1);
  const GSHSCENE_Resource * level[5] = {nullptr};
  level[1] = freeze(s.store, base);
  ASSERT_NE(level[1], nullptr);
  EXPECT_EQ(gshscene_resource_picture_depth(level[1]), 1u);
  GSHSCENE_Result rc = GSHSCENE_OK;
  for (int d = 2; d <= 4; d++) {
    GSHSCENE_List * list = nullptr;
    ASSERT_EQ(gshscene_list_create(nullptr, nullptr, s.store, &list), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_instance(list, level[d - 1], nullptr, 0, 0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_close(list), GSHSCENE_OK);
    level[d] = freeze(s.store, list, &rc);
    gshscene_list_destroy(list);
    if (d <= 3) {
      ASSERT_NE(level[d], nullptr) << "depth " << d;
      EXPECT_EQ(gshscene_resource_picture_depth(level[d]), (size_t)d);
    } else {
      EXPECT_EQ(level[d], nullptr) << "depth 4 passes max_picture_depth 3";
      EXPECT_EQ(rc, GSHSCENE_ERR_LIMIT);
    }
  }
  for (int d = 1; d <= 3; d++) {
    gshscene_resource_release(level[d]);
  }
  gshscene_list_destroy(base);
}

TEST(Picture, TheDepthIsTheDeepestInstanceNotTheLastOne) {
  StoreHolder s(nullptr, nullptr, all_zero);
  Ref sq(intern_square(s.store));
  GSHSCENE_List * base = closed_fills(s.store, sq.get(), 1);
  Ref p1(freeze(s.store, base));
  GSHSCENE_List * l2 = nullptr;
  ASSERT_EQ(gshscene_list_create(nullptr, nullptr, s.store, &l2), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_instance(l2, p1.get(), nullptr, 0, 0), GSHSCENE_OK);
  gshscene_list_close(l2);
  Ref p2(freeze(s.store, l2));
  GSHSCENE_List * l3 = nullptr;
  ASSERT_EQ(gshscene_list_create(nullptr, nullptr, s.store, &l3), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_instance(l3, p2.get(), nullptr, 0, 0), GSHSCENE_OK);  // depth 2 first
  ASSERT_EQ(gshscene_list_instance(l3, p1.get(), nullptr, 0, 0), GSHSCENE_OK);  // then depth 1
  gshscene_list_close(l3);
  Ref p3(freeze(s.store, l3));
  EXPECT_EQ(gshscene_resource_picture_depth(p3.get()), 3u);
  gshscene_list_destroy(base);
  gshscene_list_destroy(l2);
  gshscene_list_destroy(l3);
}

TEST(Picture, TheStoresExpansionLimitIsExactAtIntern) {
  GSHSCENE_Limits limits;
  gshscene_limits_default(&limits);
  limits.max_expansion = 10;
  StoreHolder s(&limits, nullptr, all_zero);
  Ref sq(intern_square(s.store));
  GSHSCENE_List * ten = closed_fills(s.store, sq.get(), 10);
  GSHSCENE_List * eleven = nullptr;  // made under roomier list limits than the store's
  ASSERT_EQ(gshscene_list_create(nullptr, nullptr, s.store, &eleven), GSHSCENE_OK);
  GSHSCENE_Result rc = GSHSCENE_OK;
  Ref ok(freeze(s.store, ten, &rc));
  EXPECT_NE(ok.get(), nullptr) << "exactly the limit";
  GSHSCENE_Limits roomy;
  gshscene_limits_default(&roomy);
  GSHSCENE_List * big = closed_fills(s.store, sq.get(), 11, &roomy);
  ASSERT_NE(big, nullptr);
  EXPECT_EQ(freeze(s.store, big, &rc), nullptr);
  EXPECT_EQ(rc, GSHSCENE_ERR_LIMIT);
  gshscene_list_destroy(ten);
  gshscene_list_destroy(eleven);
  gshscene_list_destroy(big);
}

TEST(Picture, TheStoresItemLimitIsExactAtIntern) {
  GSHSCENE_Limits limits;
  gshscene_limits_default(&limits);
  limits.max_resource_elements = 5;
  StoreHolder s(&limits, nullptr, all_zero);
  Ref sq(intern_square(s.store));  // 5 elements: at the limit
  ASSERT_NE(sq.get(), nullptr);
  GSHSCENE_List * five = closed_fills(s.store, sq.get(), 5);
  GSHSCENE_List * six = closed_fills(s.store, sq.get(), 6);
  GSHSCENE_Result rc = GSHSCENE_OK;
  Ref ok(freeze(s.store, five, &rc));
  EXPECT_NE(ok.get(), nullptr);
  EXPECT_EQ(freeze(s.store, six, &rc), nullptr);
  EXPECT_EQ(rc, GSHSCENE_ERR_LIMIT);
  gshscene_list_destroy(five);
  gshscene_list_destroy(six);
}

TEST(Picture, AnAllocationFailureAtEveryCallLeavesNothing) {
  GSHGEOM_Path * sqp = square_path();
  for (long k = 0;; k++) {
    tracker::Tracker t;
    GSHSCENE_Allocator a = tracker::allocator_for(&t);
    GSHSCENE_Store * store = nullptr;
    ASSERT_EQ(gshscene_store_create(&a, nullptr, &store), GSHSCENE_OK);
    const GSHSCENE_Resource * sq = intern(store, sqp);
    ASSERT_NE(sq, nullptr);
    GSHSCENE_List * list = closed_fills(store, sq, 40);  // 40 items: three segments
    ASSERT_NE(list, nullptr);
    t.fail_from = t.calls + k;
    GSHSCENE_Result rc = GSHSCENE_OK;
    const GSHSCENE_Resource * p = freeze(store, list, &rc);
    t.fail_from = -1;
    if (p) {
      EXPECT_EQ(rc, GSHSCENE_OK);
    } else {
      EXPECT_EQ(rc, GSHSCENE_ERR_OOM) << k;
    }
    GSHSCENE_Store_Stats stats;
    ASSERT_EQ(gshscene_store_stats(store, &stats), GSHSCENE_OK);
    EXPECT_EQ(stats.live, p ? 2u : 1u) << k;
    EXPECT_EQ(gshscene_resource_refcount(sq), p ? 81u : 41u) << k;
    gshscene_resource_release(p);
    gshscene_list_destroy(list);
    gshscene_resource_release(sq);
    gshscene_store_destroy(store);
    EXPECT_TRUE(t.live.empty()) << "k=" << k;
    if (!t.failed) {
      break;
    }
  }
  gshgeom_path_destroy(sqp);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
