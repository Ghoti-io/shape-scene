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
 * Arcs in a path resource (PS1 to PS4) and the primitive tag (primitive.h, PR1 to PR12): interning, content, accounting, limits,
 * allocation failure.
 */

#include "scene_helpers.h"

#include "../alloc_tracker.h"
#include "../known_answers.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace scene;

namespace {

const double kNaN = std::numeric_limits<double>::quiet_NaN();
const double kInf = std::numeric_limits<double>::infinity();

GSHSCENE_Primitive rect(double x, double y, double w, double h, int fx = 0, int fy = 0) {
  GSHSCENE_Primitive p;
  std::memset(&p, 0, sizeof p);
  p.kind = GSHSCENE_PRIMITIVE_RECT;
  p.flip_x = fx;
  p.flip_y = fy;
  p.x = x;
  p.y = y;
  p.w = w;
  p.h = h;
  return p;
}

GSHSCENE_Primitive rrect(double x, double y, double w, double h, const GSHGEOM_Corner r[4]) {
  GSHSCENE_Primitive p = rect(x, y, w, h);
  p.kind = GSHSCENE_PRIMITIVE_ROUNDED_RECT;
  for (int i = 0; i < 4; i++) {
    p.r[i] = r[i];
  }
  return p;
}

GSHSCENE_Primitive ellipse(double cx, double cy, double rx, double ry, int fx = 0, int fy = 0) {
  GSHSCENE_Primitive p;
  std::memset(&p, 0, sizeof p);
  p.kind = GSHSCENE_PRIMITIVE_ELLIPSE;
  p.flip_x = fx;
  p.flip_y = fy;
  p.cx = cx;
  p.cy = cy;
  p.rx = rx;
  p.ry = ry;
  return p;
}

const GSHSCENE_Resource * prim(GSHSCENE_Store * store, const GSHSCENE_Primitive & p, GSHSCENE_Result * result = nullptr) {
  const GSHSCENE_Resource * r = nullptr;
  GSHSCENE_Result rc = gshscene_store_intern_primitive(store, &p, &r);
  if (result) {
    *result = rc;
  }
  EXPECT_EQ(rc == GSHSCENE_OK, r != nullptr);
  return r;
}

const GSHGEOM_Corner kCorners[4] = {{10, 12}, {6, 5}, {4, 4}, {8, 9}};

/** The arc path of tools/gen-scene-vectors.py. */
GSHGEOM_Path * arc_path() {
  GSHGEOM_Path * p = new_path();
  EXPECT_EQ(gshgeom_path_move_to(p, P(0, 0)), GSHGEOM_OK);
  GSHGEOM_Arc_Form f1 = {P(0, 10), P(10, 0), P(0, 10), GSHGEOM_ANGLE_DEGREES, -90.0, 90.0, GSHGEOM_ARC_CIRCULAR};
  EXPECT_EQ(gshgeom_path_arc_conjugate_to(p, &f1, nullptr), GSHGEOM_OK);
  GSHGEOM_Arc_Form f2 = {P(10, 15), P(8, 0), P(0, 5), GSHGEOM_ANGLE_TURNS, -0.25, 0.5, 0u};
  EXPECT_EQ(gshgeom_path_arc_conjugate_to(p, &f2, nullptr), GSHGEOM_OK);
  GSHGEOM_Arc_Form f3 = {P(5, 20), P(5, 0), P(0, 3), GSHGEOM_ANGLE_GRADIANS, 0.0, 200.0, 0u};
  EXPECT_EQ(gshgeom_path_arc_conjugate_to(p, &f3, nullptr), GSHGEOM_OK);
  EXPECT_EQ(gshgeom_path_line_to(p, P(0, 0)), GSHGEOM_OK);
  EXPECT_EQ(gshgeom_path_close(p), GSHGEOM_OK);
  return p;
}

/** A path of @p n quarter arcs about the origin (the first element a move). */
GSHGEOM_Path * arcs_path(size_t n, GSHGEOM_Angle_Unit unit = GSHGEOM_ANGLE_DEGREES, double t0_nudge = 0.0) {
  GSHGEOM_Path * p = new_path();
  EXPECT_EQ(gshgeom_path_move_to(p, P(10, 0)), GSHGEOM_OK);
  for (size_t i = 0; i < n; i++) {
    const double quarter = unit == GSHGEOM_ANGLE_DEGREES ? 90.0 : unit == GSHGEOM_ANGLE_GRADIANS ? 100.0 : 0.25;
    const size_t k = i % 4;
    GSHGEOM_Arc_Form f = {P(0, 0), P(10, 0), P(0, 10), unit, quarter * (double)k + (i == 0 ? t0_nudge : 0.0), quarter, GSHGEOM_ARC_CIRCULAR};
    const GSHGEOM_Result r = gshgeom_path_arc_conjugate_to(p, &f, nullptr);
    EXPECT_EQ(r, GSHGEOM_OK) << i;
    if (r != GSHGEOM_OK) {
      break;
    }
  }
  return p;
}

}  // namespace

TEST(PathArcs, InternHitCopyEqualHashDumpAreTheGeometryLibrarys) {
  StoreHolder s(nullptr, nullptr, all_zero);
  GSHGEOM_Path * p = arc_path();
  Ref a(intern(s.store, p));
  Ref b(intern(s.store, p));
  ASSERT_NE(a.get(), nullptr);
  EXPECT_EQ(a.get(), b.get()) << "equal content is one handle";
  GSHGEOM_Path * copy = nullptr;
  ASSERT_EQ(gshgeom_path_copy(gshscene_resource_path(a.get()), nullptr, nullptr, &copy), GSHGEOM_OK);
  EXPECT_TRUE(gshgeom_path_equal(copy, p));
  EXPECT_EQ(gshgeom_path_hash(copy), gshgeom_path_hash(p));
  Ref c(intern(s.store, copy));
  EXPECT_EQ(c.get(), a.get());
  EXPECT_EQ(dump_resource(a.get()), known::kArcPathResourceDump) << "hash and dump restated by tools/gen-scene-vectors.py";
  EXPECT_EQ(gshscene_resource_hash(a.get()), strtoull(known::kArcPathResourceDump + std::strlen("gshscene.resource 1 kind=path hash="), nullptr, 16));
  gshgeom_path_destroy(copy);
  gshgeom_path_destroy(p);
}

TEST(PathArcs, ANearlyEqualArcIsAnotherHandleAndNoReadingIsTheChord) {
  StoreHolder s(nullptr, nullptr, all_zero);
  std::vector<Ref> held;
  std::set<const GSHSCENE_Resource *> distinct;
  auto add = [&](const char * what, GSHGEOM_Path * p) {
    held.emplace_back(intern(s.store, p));
    gshgeom_path_destroy(p);
    ASSERT_NE(held.back().get(), nullptr) << what;
    EXPECT_TRUE(distinct.insert(held.back().get()).second) << what << " merged with an earlier path";
  };
  add("four arcs", arcs_path(4));
  add("three arcs", arcs_path(3));
  add("the first start parameter one ulp up", arcs_path(4, GSHGEOM_ANGLE_DEGREES, std::nextafter(0.0, 1.0)));
  add("gradians", arcs_path(4, GSHGEOM_ANGLE_GRADIANS));
  add("turns", arcs_path(4, GSHGEOM_ANGLE_TURNS));
  {
    /* the same ends and the same sweep, the other flag */
    GSHGEOM_Path * p = new_path();
    gshgeom_path_move_to(p, P(0, 0));
    GSHGEOM_Arc_Form f = {P(0, 10), P(10, 0), P(0, 10), GSHGEOM_ANGLE_DEGREES, -90.0, 90.0, 0u};
    ASSERT_EQ(gshgeom_path_arc_conjugate_to(p, &f, nullptr), GSHGEOM_OK);
    add("an arc without the circular flag", p);
  }
  {
    /* the chord: a line between the ends is not an arc */
    GSHGEOM_Path * p = new_path();
    gshgeom_path_move_to(p, P(0, 0));
    gshgeom_path_line_to(p, P(10, 10));
    add("the chord of the arc", p);
  }
  {
    GSHGEOM_Path * p = new_path();
    gshgeom_path_move_to(p, P(0, 0));
    GSHGEOM_Arc_Form f = {P(0, 10), P(10, 0), P(0, 10), GSHGEOM_ANGLE_DEGREES, -90.0, 90.0, GSHGEOM_ARC_CIRCULAR};
    ASSERT_EQ(gshgeom_path_arc_conjugate_to(p, &f, nullptr), GSHGEOM_OK);
    add("one circular arc", p);
  }
  {
    GSHGEOM_Path * p = new_path();
    gshgeom_path_move_to(p, P(0, 0));
    GSHGEOM_Arc_Form f = {P(0, 10), P(10, 0), P(0, 10), GSHGEOM_ANGLE_DEGREES, -90.0, 89.0, GSHGEOM_ARC_CIRCULAR};
    ASSERT_EQ(gshgeom_path_arc_conjugate_to(p, &f, nullptr), GSHGEOM_OK);  // the end is not (10, 10): another arc from the same start
    add("the same start and a smaller sweep", p);
  }
}

TEST(PathArcs, AccountedBytesCoverTheAllocatedOnes) {
  /* PS3: the store accounts GSHSCENE_BYTES_PER_PATH_ELEMENT for each element.  Under an allocator that counts bytes, what an arc takes
   * in the store's copy (the element, its four points and its two scalars) is at most that figure, and a path of a thousand arcs holds
   * less than it is accounted for.  The fixed costs (the resource, the path, the table) are the formula's base, not a measurement. */
  struct Counter {
    size_t live = 0;
    std::map<void *, size_t> sizes;
  };
  auto mal = [](void * ctx, size_t n) -> void * {
    Counter * c = (Counter *)ctx;
    void * p = std::malloc(n ? n : 1);
    if (p) {
      c->sizes[p] = n;
      c->live += n;
    }
    return p;
  };
  auto cal = [](void * ctx, size_t k, size_t n) -> void * {
    Counter * c = (Counter *)ctx;
    void * p = std::calloc(k ? k : 1, n ? n : 1);
    if (p) {
      c->sizes[p] = k * n;
      c->live += k * n;
    }
    return p;
  };
  auto rea = [](void * ctx, void * old, size_t n) -> void * {
    Counter * c = (Counter *)ctx;
    void * p = std::realloc(old, n ? n : 1);
    if (p) {
      if (old) {
        c->live -= c->sizes[old];
        c->sizes.erase(old);
      }
      c->sizes[p] = n;
      c->live += n;
    }
    return p;
  };
  auto fre = [](void * ctx, void * p) {
    Counter * c = (Counter *)ctx;
    if (p) {
      c->live -= c->sizes[p];
      c->sizes.erase(p);
    }
    std::free(p);
  };
  size_t held[2] = {0, 0};
  size_t elements[2] = {0, 0};
  const size_t counts[2] = {100, 1100};
  for (int k = 0; k < 2; k++) {
    Counter c;
    GSHSCENE_Allocator alloc;
    alloc.ctx = &c;
    alloc.malloc_fn = mal;
    alloc.calloc_fn = cal;
    alloc.realloc_fn = rea;
    alloc.free_fn = fre;
    StoreHolder s(nullptr, &alloc, all_zero);
    GSHGEOM_Path * p = arcs_path(counts[k]);
    elements[k] = gshgeom_path_element_count(p);
    const size_t before = c.live;
    Ref r(intern(s.store, p));
    ASSERT_NE(r.get(), nullptr);
    held[k] = c.live - before;
    GSHSCENE_Store_Stats stats;
    ASSERT_EQ(gshscene_store_stats(s.store, &stats), GSHSCENE_OK);
    EXPECT_EQ(stats.bytes, GSHSCENE_BYTES_BASE + elements[k] * GSHSCENE_BYTES_PER_PATH_ELEMENT) << counts[k];
    EXPECT_LE(held[k], (size_t)stats.bytes) << counts[k] << " arcs: the store holds more than it accounts for";
    gshgeom_path_destroy(p);
  }
  const size_t each = (held[1] - held[0]) / (elements[1] - elements[0]);
  EXPECT_LE(each, (size_t)GSHSCENE_BYTES_PER_PATH_ELEMENT) << "an arc takes " << each << " bytes in the store's copy";
  EXPECT_GE(each, 100u) << "and the measure sees them";
}

TEST(PathArcs, TheByteLimitAppliesToAnArcAsToAnyElement) {
  GSHGEOM_Path * p = arcs_path(10);
  const size_t need = GSHSCENE_BYTES_BASE + gshgeom_path_element_count(p) * GSHSCENE_BYTES_PER_PATH_ELEMENT;
  GSHSCENE_Limits limits;
  gshscene_limits_default(&limits);
  limits.max_bytes = need;
  {
    StoreHolder s(&limits, nullptr, all_zero);
    Ref ok(intern(s.store, p));
    EXPECT_NE(ok.get(), nullptr) << "exactly the limit";
  }
  limits.max_bytes = need - 1;
  {
    StoreHolder s(&limits, nullptr, all_zero);
    const GSHSCENE_Resource * r = nullptr;
    EXPECT_EQ(gshscene_store_intern_path(s.store, p, &r), GSHSCENE_ERR_LIMIT);
    EXPECT_EQ(r, nullptr);
  }
  limits.max_bytes = 0;
  limits.max_resource_elements = gshgeom_path_element_count(p) - 1;
  {
    StoreHolder s(&limits, nullptr, all_zero);
    const GSHSCENE_Resource * r = nullptr;
    EXPECT_EQ(gshscene_store_intern_path(s.store, p, &r), GSHSCENE_ERR_LIMIT) << "an arc is one element, and the cap counts it";
  }
  gshgeom_path_destroy(p);
}

TEST(PathArcs, AFailureAtEveryAllocationLeavesTheStoreAsItWas) {
  GSHGEOM_Path * p = arc_path();
  bool saw = false;
  for (long k = 0; k < 60; k++) {
    tracker::Tracker t;
    t.fail_from = k;
    GSHSCENE_Allocator alloc = tracker::allocator_for(&t);
    {
      StoreHolder s(nullptr, &alloc, all_zero);
      if (!s.store) {
        continue;
      }
      const GSHSCENE_Resource * r = nullptr;
      const GSHSCENE_Result rc = gshscene_store_intern_path(s.store, p, &r);
      GSHSCENE_Store_Stats stats;
      ASSERT_EQ(gshscene_store_stats(s.store, &stats), GSHSCENE_OK);
      if (rc == GSHSCENE_ERR_OOM) {
        saw = true;
        EXPECT_EQ(r, nullptr);
        EXPECT_EQ(stats.live, 0u) << k;
      } else {
        ASSERT_EQ(rc, GSHSCENE_OK);
        gshscene_resource_release(r);
      }
    }
    EXPECT_TRUE(t.live.empty()) << "fail_from " << k;
  }
  EXPECT_TRUE(saw);
  gshgeom_path_destroy(p);
}

TEST(Primitive, TheTagRoundTripsAndTheKindIsStillAPath) {
  StoreHolder s(nullptr, nullptr, all_zero);
  Ref r(prim(s.store, rect(10, 20, 100, 50, 1, 0)));
  ASSERT_NE(r.get(), nullptr);
  EXPECT_EQ(gshscene_resource_kind(r.get()), GSHSCENE_RESOURCE_PATH);
  GSHSCENE_Primitive out;
  ASSERT_EQ(gshscene_resource_primitive(r.get(), &out), GSHSCENE_OK);
  EXPECT_EQ(out.kind, GSHSCENE_PRIMITIVE_RECT);
  EXPECT_EQ(out.flip_x, 1);
  EXPECT_EQ(out.flip_y, 0);
  EXPECT_EQ(out.x, 10.0);
  EXPECT_EQ(out.y, 20.0);
  EXPECT_EQ(out.w, 100.0);
  EXPECT_EQ(out.h, 50.0);
  EXPECT_EQ(gshgeom_path_element_count(gshscene_resource_path(r.get())), 5u);

  Ref e(prim(s.store, ellipse(100, 100, 50, 20, 0, 1)));
  ASSERT_EQ(gshscene_resource_primitive(e.get(), &out), GSHSCENE_OK);
  EXPECT_EQ(out.kind, GSHSCENE_PRIMITIVE_ELLIPSE);
  EXPECT_EQ(out.flip_y, 1);
  EXPECT_EQ(out.rx, 50.0);
  EXPECT_EQ(out.x, 0.0) << "the fields of another kind are zero";

  Ref q(prim(s.store, rrect(0, 0, 100, 60, kCorners)));
  ASSERT_EQ(gshscene_resource_primitive(q.get(), &out), GSHSCENE_OK);
  EXPECT_EQ(out.kind, GSHSCENE_PRIMITIVE_ROUNDED_RECT);
  EXPECT_EQ(out.r[3].rx, 8.0);
  EXPECT_EQ(out.r[3].ry, 9.0);
  EXPECT_EQ(gshgeom_path_element_count(gshscene_resource_path(q.get())), 10u);

  /* the accessor on what is not a tagged path */
  Ref plain(intern_square(s.store));
  ASSERT_EQ(gshscene_resource_primitive(plain.get(), &out), GSHSCENE_OK);
  EXPECT_EQ(out.kind, GSHSCENE_PRIMITIVE_NONE);
  EXPECT_EQ(out.w, 0.0);
  EXPECT_EQ(gshscene_resource_primitive(nullptr, &out), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_resource_primitive(r.get(), nullptr), GSHSCENE_ERR_INVALID);
  const GSHSCENE_Resource * plane = nullptr;
  ASSERT_EQ(gshscene_store_intern_plane(s.store, &plane), GSHSCENE_OK);
  EXPECT_EQ(gshscene_resource_primitive(plane, &out), GSHSCENE_ERR_INVALID) << "only a path has a tag";
  gshscene_resource_release(plane);
  EXPECT_EQ(gshscene_store_intern_primitive(s.store, nullptr, nullptr), GSHSCENE_ERR_INVALID);
  {
    GSHSCENE_Primitive ok = rect(0, 0, 1, 1);
    EXPECT_EQ(gshscene_store_intern_primitive(s.store, &ok, nullptr), GSHSCENE_ERR_INVALID);
    GSHSCENE_Store_Stats st;
    ASSERT_EQ(gshscene_store_stats(s.store, &st), GSHSCENE_OK);
    EXPECT_EQ(st.live, 4u) << "a refused call keeps nothing (the three above and the square)";
  }
}

TEST(Primitive, ATagIsNeverInferredFromGeometry) {
  StoreHolder s(nullptr, nullptr, all_zero);
  Ref tagged(prim(s.store, rect(10, 20, 100, 50)));
  GSHGEOM_Path * p = nullptr;
  ASSERT_EQ(gshgeom_path_copy(gshscene_resource_path(tagged.get()), nullptr, nullptr, &p), GSHGEOM_OK);
  Ref untagged(intern(s.store, p));
  gshgeom_path_destroy(p);
  ASSERT_NE(untagged.get(), nullptr);
  EXPECT_NE(untagged.get(), tagged.get()) << "the same geometry untagged is another handle";
  GSHSCENE_Primitive out;
  ASSERT_EQ(gshscene_resource_primitive(untagged.get(), &out), GSHSCENE_OK);
  EXPECT_EQ(out.kind, GSHSCENE_PRIMITIVE_NONE);
  EXPECT_NE(gshscene_resource_hash(untagged.get()), gshscene_resource_hash(tagged.get()));
  /* a circle's geometry through intern_path is a path, whatever it looks like */
  GSHGEOM_Path * circle = new_path();
  ASSERT_EQ(gshgeom_path_add_ellipse(circle, 50, 30, 20, 20, 0, 0), GSHGEOM_OK);
  Ref plain(intern(s.store, circle));
  gshgeom_path_destroy(circle);
  ASSERT_EQ(gshscene_resource_primitive(plain.get(), &out), GSHSCENE_OK);
  EXPECT_EQ(out.kind, GSHSCENE_PRIMITIVE_NONE);
  Ref tagged_circle(prim(s.store, ellipse(50, 30, 20, 20)));
  EXPECT_NE(plain.get(), tagged_circle.get());
  EXPECT_TRUE(gshgeom_path_equal(gshscene_resource_path(plain.get()), gshscene_resource_path(tagged_circle.get())));
}

TEST(Primitive, EqualContentIsOneHandleAndNearlyEqualIsAnother) {
  StoreHolder s(nullptr, nullptr, all_zero);
  std::vector<Ref> held;
  std::set<const GSHSCENE_Resource *> distinct;
  auto add = [&](const char * what, const GSHSCENE_Primitive & p) {
    held.emplace_back(prim(s.store, p));
    ASSERT_NE(held.back().get(), nullptr) << what;
    EXPECT_TRUE(distinct.insert(held.back().get()).second) << what << " merged with an earlier primitive";
  };
  add("rect", rect(10, 20, 100, 50));
  add("rect flip_x", rect(10, 20, 100, 50, 1, 0));
  add("rect flip_y", rect(10, 20, 100, 50, 0, 1));
  add("rect both flips", rect(10, 20, 100, 50, 1, 1));
  add("rect, one bit of w", rect(10, 20, std::nextafter(100.0, 200.0), 50));
  add("rect at -0", rect(-0.0, 20, 100, 50));
  add("rect at +0", rect(0.0, 20, 100, 50));
  // a parameter that moves no coordinate: x + (-0) is x, so these two paths are bit equal and the tags are the only difference
  add("rect of width -0", rect(3, 4, -0.0, 5));
  add("rect of width +0", rect(3, 4, 0.0, 5));
  // and parameters a rounding absorbs: y + h is 2^60 for h = 1 and for h = 2 (the spacing there is 256)
  add("rect of height 1 far from the origin", rect(0, 0x1p60, 10, 1));
  add("rect of height 2 far from the origin", rect(0, 0x1p60, 10, 2));
  add("ellipse", ellipse(10, 20, 100, 50));
  add("ellipse flip_x", ellipse(10, 20, 100, 50, 1, 0));
  add("ellipse flip_y", ellipse(10, 20, 100, 50, 0, 1));
  add("ellipse both", ellipse(10, 20, 100, 50, 1, 1));
  add("ellipse, one bit of ry", ellipse(10, 20, 100, std::nextafter(50.0, 100.0)));
  add("rounded rect", rrect(0, 0, 100, 60, kCorners));
  GSHGEOM_Corner other[4] = {{10, 12}, {6, 5}, {4, 4}, {8, std::nextafter(9.0, 10.0)}};
  add("rounded rect, a corner differs in one component", rrect(0, 0, 100, 60, other));
  GSHGEOM_Corner swapped[4] = {{6, 5}, {10, 12}, {4, 4}, {8, 9}};
  add("rounded rect, two corners exchanged", rrect(0, 0, 100, 60, swapped));
  for (int f = 1; f < 4; f++) {
    GSHSCENE_Primitive fp = rrect(0, 0, 100, 60, kCorners);
    fp.flip_x = f & 1;
    fp.flip_y = f >> 1;
    add("rounded rect under a flip pair", fp);
  }
  {
    // flipped, a box with no round corner is a rounded rect with its four lines, one handle per pair and none the rect's
    const GSHGEOM_Corner none[4] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};
    for (int f = 0; f < 4; f++) {
      GSHSCENE_Primitive fp = rrect(0, 0, 100, 60, none);
      fp.flip_x = f & 1;
      fp.flip_y = f >> 1;
      held.emplace_back(prim(s.store, fp));
      ASSERT_NE(held.back().get(), nullptr);
      EXPECT_TRUE(distinct.insert(held.back().get()).second) << f;
      GSHSCENE_Primitive back;
      ASSERT_EQ(gshscene_resource_primitive(held.back().get(), &back), GSHSCENE_OK);
      EXPECT_EQ(back.kind, f == 0 ? GSHSCENE_PRIMITIVE_RECT : GSHSCENE_PRIMITIVE_ROUNDED_RECT) << f;
      EXPECT_EQ(gshgeom_path_element_count(gshscene_resource_path(held.back().get())), f == 0 ? 5u : 6u) << f;
    }
  }
  const size_t n = held.size();
  // the pairs above are two handles, two hashes, and (for the last two) one path
  EXPECT_TRUE(gshgeom_path_equal(gshscene_resource_path(held[7].get()), gshscene_resource_path(held[8].get())));
  EXPECT_TRUE(gshgeom_path_equal(gshscene_resource_path(held[9].get()), gshscene_resource_path(held[10].get())));
  EXPECT_NE(gshscene_resource_hash(held[7].get()), gshscene_resource_hash(held[8].get())) << "-0 and +0 are two hashes";
  EXPECT_NE(gshscene_resource_hash(held[9].get()), gshscene_resource_hash(held[10].get()));
  EXPECT_NE(gshscene_resource_hash(held[5].get()), gshscene_resource_hash(held[6].get()));
  /* the same content again: the same handles */
  EXPECT_EQ(prim(s.store, rect(10, 20, 100, 50, 1, 1)), held[3].get());
  gshscene_resource_release(held[3].get());
  EXPECT_EQ(prim(s.store, rrect(0, 0, 100, 60, kCorners)), held[16].get());
  gshscene_resource_release(held[16].get());
  EXPECT_EQ(distinct.size(), n);
}

TEST(Primitive, ACanonicalFormMakesEqualGeometryEqualContent) {
  StoreHolder s(nullptr, nullptr, all_zero);
  /* a corner with a zero component is square and {0, 0}; all square is a RECT */
  const GSHGEOM_Corner mixed[4] = {{5, 0}, {0, 5}, {-0.0, 3}, {2, -0.0}};
  const GSHGEOM_Corner none[4] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};
  Ref a(prim(s.store, rrect(1, 2, 30, 40, mixed)));
  Ref b(prim(s.store, rrect(1, 2, 30, 40, none)));
  Ref c(prim(s.store, rect(1, 2, 30, 40)));
  ASSERT_NE(a.get(), nullptr);
  EXPECT_EQ(a.get(), b.get());
  EXPECT_EQ(a.get(), c.get());
  GSHSCENE_Primitive out;
  ASSERT_EQ(gshscene_resource_primitive(a.get(), &out), GSHSCENE_OK);
  EXPECT_EQ(out.kind, GSHSCENE_PRIMITIVE_RECT);
  /* one round corner among square ones keeps its radii, the others canonical {+0, +0} */
  const GSHGEOM_Corner one[4] = {{5, 4}, {7, 0}, {0, 3}, {-0.0, -0.0}};
  Ref d(prim(s.store, rrect(0, 0, 100, 60, one)));
  ASSERT_EQ(gshscene_resource_primitive(d.get(), &out), GSHSCENE_OK);
  EXPECT_EQ(out.kind, GSHSCENE_PRIMITIVE_ROUNDED_RECT);
  EXPECT_EQ(out.r[0].rx, 5.0);
  for (int i = 1; i < 4; i++) {
    uint64_t bx, by;
    std::memcpy(&bx, &out.r[i].rx, 8);
    std::memcpy(&by, &out.r[i].ry, 8);
    EXPECT_EQ(bx, 0u) << i;
    EXPECT_EQ(by, 0u) << i;
  }
  EXPECT_EQ(gshgeom_path_element_count(gshscene_resource_path(d.get())), 7u) << "one arc, four lines, a move and a close";
  /* the flips of a RECT are content even where the geometry is symmetric: a unit square under each pair is four handles */
  std::set<const GSHSCENE_Resource *> four;
  std::vector<Ref> held;
  for (int f = 0; f < 4; f++) {
    held.emplace_back(prim(s.store, rect(0, 0, 0, 0, f & 1, f >> 1)));
    four.insert(held.back().get());
  }
  EXPECT_EQ(four.size(), 4u);
}

TEST(Primitive, TheHashAndDumpAreRestatedByTheGenerator) {
  StoreHolder s(nullptr, nullptr, all_zero);
  Ref rf(prim(s.store, rect(10, 20, 100, 50, 1, 0)));
  EXPECT_EQ(dump_resource(rf.get()), known::kRectFlipXResourceDump);
  Ref q(prim(s.store, rrect(0, 0, 100, 60, kCorners)));
  EXPECT_EQ(dump_resource(q.get()), known::kRoundedRectResourceDump);
  // Canvas roundRect with a negative size: the reversal for one flip (the radii at the mirrored corners), the clockwise path for two
  for (int f = 1; f < 4; f++) {
    GSHSCENE_Primitive fp = rrect(0, 0, 100, 60, kCorners);
    fp.flip_x = f & 1;
    fp.flip_y = f >> 1;
    Ref fr(prim(s.store, fp));
    EXPECT_EQ(dump_resource(fr.get()), f == 1 ? known::kRoundedRectFlipXResourceDump
                                       : f == 2 ? known::kRoundedRectFlipYResourceDump
                                                : known::kRoundedRectBothFlipsResourceDump)
        << f;
  }
  Ref e(prim(s.store, ellipse(100, 100, 50, 20, 0, 1)));
  EXPECT_EQ(dump_resource(e.get()), known::kEllipseFlipYResourceDump);
  Ref c(prim(s.store, ellipse(50, 30, 20, 20)));
  EXPECT_EQ(dump_resource(c.get()), known::kCircleResourceDump);
  /* and the same geometry untagged: no tag line, the hash of a path as it always was */
  GSHGEOM_Path * p = nullptr;
  ASSERT_EQ(gshgeom_path_copy(gshscene_resource_path(rf.get()), nullptr, nullptr, &p), GSHGEOM_OK);
  Ref u(intern(s.store, p));
  gshgeom_path_destroy(p);
  EXPECT_EQ(dump_resource(u.get()), known::kRectFlipXUntaggedResourceDump);
  /* an untagged path of an earlier story is unchanged */
  Ref sq(intern_square(s.store));
  EXPECT_EQ(dump_resource(sq.get()), known::kSquareResourceDump);
}

TEST(Primitive, TheRefusalsComeInTheOrderOfTheRule) {
  StoreHolder s(nullptr, nullptr, all_zero);
  const GSHSCENE_Resource * r = nullptr;
  GSHSCENE_Primitive p = rect(0, 0, 1, 1);
  EXPECT_EQ(gshscene_store_intern_primitive(nullptr, &p, &r), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_store_intern_primitive(s.store, nullptr, &r), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_store_intern_primitive(s.store, &p, nullptr), GSHSCENE_ERR_INVALID);
  auto result = [&](const GSHSCENE_Primitive & x) {
    GSHSCENE_Result rc;
    const GSHSCENE_Resource * h = prim(s.store, x, &rc);
    gshscene_resource_release(h);
    return rc;
  };
  // a kind that is no primitive, with the parameters of each valid kind (a bad kind must not be read as one of them)
  for (const GSHSCENE_Primitive & valid : {rect(0, 0, 1, 1), ellipse(5, 5, 2, 3)}) {
    p = valid;
    p.kind = GSHSCENE_PRIMITIVE_NONE;
    EXPECT_EQ(result(p), GSHSCENE_ERR_INVALID) << "NONE is not a primitive";
    p.kind = GSHSCENE_PRIMITIVE_KIND_COUNT;
    EXPECT_EQ(result(p), GSHSCENE_ERR_INVALID);
    p.kind = (GSHSCENE_Primitive_Kind)-1;
    EXPECT_EQ(result(p), GSHSCENE_ERR_INVALID);
  }
  p = rect(0, 0, 1, 1);
  EXPECT_EQ(result(rect(0, 0, 1, 1, 2, 0)), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(result(rect(0, 0, 1, 1, 0, -1)), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(result(rect(kNaN, 0, 1, 1)), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(result(rect(0, kInf, 1, 1)), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(result(rect(0, 0, -1, 1)), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(result(rect(0, 0, 1, -0.5)), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(result(rect(0, 0, 0, 0)), GSHSCENE_OK) << "a rect of no size is a legitimate fact";
  EXPECT_EQ(result(rect(0, 0, -0.0, 1)), GSHSCENE_OK);
  EXPECT_EQ(result(ellipse(0, 0, 0, 1)), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(result(ellipse(0, 0, 1, -1)), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(result(ellipse(0, 0, 1, kNaN)), GSHSCENE_ERR_INVALID);
  const GSHGEOM_Corner negative[4] = {{-1, 1}, {0, 0}, {0, 0}, {0, 0}};
  EXPECT_EQ(result(rrect(0, 0, 10, 10, negative)), GSHSCENE_ERR_INVALID);
  const GSHGEOM_Corner overlap[4] = {{6, 1}, {6, 1}, {0, 0}, {0, 0}};
  EXPECT_EQ(result(rrect(0, 0, 10, 10, overlap)), GSHSCENE_ERR_INVALID);
  const GSHGEOM_Corner fits[4] = {{5, 1}, {5, 1}, {5, 1}, {5, 1}};
  EXPECT_EQ(result(rrect(0, 0, 10, 10, fits)), GSHSCENE_OK) << "equality is allowed";
  /* the order: INVALID before LIMIT, whatever the flips */
  GSHSCENE_Primitive f = rrect(0, 0, 10, 10, fits);
  f.flip_x = 1;
  EXPECT_EQ(result(f), GSHSCENE_OK) << "a flipped rounded rect is Canvas roundRect with a negative size";
  f.flip_x = 0;
  f.flip_y = 1;
  EXPECT_EQ(result(f), GSHSCENE_OK);
  f.flip_y = 2;
  EXPECT_EQ(result(f), GSHSCENE_ERR_INVALID);
  f.flip_y = 0;
  f.flip_x = 2;
  EXPECT_EQ(result(f), GSHSCENE_ERR_INVALID) << "a flip that is not 0 or 1 is invalid on either axis";
  f.flip_x = -1;
  EXPECT_EQ(result(f), GSHSCENE_ERR_INVALID);
  f.flip_x = 1;
  f.flip_y = 1;
  f.x = kNaN;
  EXPECT_EQ(result(f), GSHSCENE_ERR_INVALID) << "a number that is not one is invalid under any flips";
  f.x = 0;
  f.y = kInf;
  EXPECT_EQ(result(f), GSHSCENE_ERR_INVALID);
  f.y = 0;
  f.flip_y = 1;
  f.w = -1;
  EXPECT_EQ(result(f), GSHSCENE_ERR_INVALID) << "a negative size is invalid under any flips";
  f.w = 10;
  f.r[0].rx = 6;
  EXPECT_EQ(result(f), GSHSCENE_ERR_INVALID) << "and so is an overlap";
  f = rrect(1e300, 0, 1e300, 10, fits);
  EXPECT_EQ(result(f), GSHSCENE_ERR_LIMIT) << "unflipped, the coordinate limit";
  f.flip_x = 1;
  EXPECT_EQ(result(f), GSHSCENE_ERR_LIMIT) << "and flipped";
  const GSHGEOM_Corner square[4] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};
  f = rrect(0, 0, 10, 10, square);
  f.flip_x = 1;
  EXPECT_EQ(result(f), GSHSCENE_OK) << "every corner square, flipped";
  /* the coordinate limit: LIMIT, after the validity */
  EXPECT_EQ(result(rect(0x1p99, 0, 0x1p99, 1)), GSHSCENE_OK);
  EXPECT_EQ(result(rect(0x1p99, 0, 0x1p99 + 0x1p48, 1)), GSHSCENE_ERR_LIMIT);
  EXPECT_EQ(result(ellipse(0x1p100, 0, 0x1p48, 1)), GSHSCENE_ERR_LIMIT);
  EXPECT_EQ(result(ellipse(0x1p100, 0, 1, 1)), GSHSCENE_OK) << "cx + rx rounds back to the limit";
  GSHSCENE_Store_Stats stats;
  ASSERT_EQ(gshscene_store_stats(s.store, &stats), GSHSCENE_OK);
  EXPECT_EQ(stats.live, 0u) << "a refusal keeps nothing, and every success above was released";
}

TEST(Primitive, TheCountsAreTheBoundsAndOnePastIsALimit) {
  struct Row {
    GSHSCENE_Primitive p;
    size_t n;
  };
  const GSHGEOM_Corner round4[4] = {{2, 2}, {2, 2}, {2, 2}, {2, 2}};
  const GSHGEOM_Corner round1[4] = {{2, 2}, {0, 0}, {0, 0}, {0, 0}};
  const Row rows[] = {{rect(0, 0, 10, 10), 5}, {ellipse(0, 0, 5, 5), 6}, {rrect(0, 0, 10, 10, round4), 10}, {rrect(0, 0, 10, 10, round1), 7}};
  for (const Row & row : rows) {
    GSHSCENE_Limits limits;
    gshscene_limits_default(&limits);
    limits.max_resource_elements = row.n;
    {
      StoreHolder s(&limits, nullptr, all_zero);
      Ref ok(prim(s.store, row.p));
      ASSERT_NE(ok.get(), nullptr) << row.n;
      EXPECT_EQ(gshgeom_path_element_count(gshscene_resource_path(ok.get())), row.n);
    }
    limits.max_resource_elements = row.n - 1;
    {
      StoreHolder s(&limits, nullptr, all_zero);
      GSHSCENE_Result rc;
      const GSHSCENE_Resource * r = prim(s.store, row.p, &rc);
      EXPECT_EQ(rc, GSHSCENE_ERR_LIMIT) << row.n;
      EXPECT_EQ(r, nullptr);
      GSHSCENE_Store_Stats stats;
      ASSERT_EQ(gshscene_store_stats(s.store, &stats), GSHSCENE_OK);
      EXPECT_EQ(stats.live, 0u);
    }
    /* the bytes: base plus the elements */
    limits.max_resource_elements = 0;
    limits.max_bytes = GSHSCENE_BYTES_BASE + row.n * GSHSCENE_BYTES_PER_PATH_ELEMENT;
    {
      StoreHolder s(&limits, nullptr, all_zero);
      Ref ok(prim(s.store, row.p));
      EXPECT_NE(ok.get(), nullptr);
    }
    limits.max_bytes -= 1;
    {
      StoreHolder s(&limits, nullptr, all_zero);
      GSHSCENE_Result rc;
      prim(s.store, row.p, &rc);
      EXPECT_EQ(rc, GSHSCENE_ERR_LIMIT);
    }
  }
}

TEST(Primitive, AFailureAtEveryAllocationLeavesTheStoreAsItWasAndLeaksNothing) {
  const GSHSCENE_Primitive cases[] = {rect(0, 0, 10, 10, 1, 1), ellipse(0, 0, 5, 6, 0, 1), rrect(0, 0, 100, 60, kCorners)};
  for (const GSHSCENE_Primitive & p : cases) {
    bool saw = false;
    for (long k = 0; k < 80; k++) {
      tracker::Tracker t;
      t.fail_from = k;
      GSHSCENE_Allocator alloc = tracker::allocator_for(&t);
      {
        StoreHolder s(nullptr, &alloc, all_zero);
        if (!s.store) {
          continue;
        }
        GSHSCENE_Result rc;
        const GSHSCENE_Resource * r = prim(s.store, p, &rc);
        GSHSCENE_Store_Stats stats;
        ASSERT_EQ(gshscene_store_stats(s.store, &stats), GSHSCENE_OK);
        if (rc == GSHSCENE_ERR_OOM) {
          saw = true;
          EXPECT_EQ(r, nullptr);
          EXPECT_EQ(stats.live, 0u) << k;
          EXPECT_EQ(stats.bytes, 0u) << k;
        } else {
          ASSERT_EQ(rc, GSHSCENE_OK);
          gshscene_resource_release(r);
        }
      }
      EXPECT_TRUE(t.live.empty()) << "kind " << p.kind << " fail_from " << k;
    }
    EXPECT_TRUE(saw) << p.kind;
  }
}

TEST(Primitive, ADumpThatCannotBeWrittenIsAnIoErrorAtTheTagLineToo) {
  StoreHolder s(nullptr, nullptr, all_zero);
  Ref r(prim(s.store, ellipse(5, 6, 2, 3, 1, 0)));
  ASSERT_NE(r.get(), nullptr);
  const std::string full = dump_resource(r.get());
  const std::string path_part = full.substr(0, full.find("primitive kind="));
  ASSERT_NE(path_part.size(), full.size());
  // an unbuffered stream of every size from the path's text to one byte short of the whole: every write error is reported
  size_t io = 0;
  for (size_t size = path_part.size(); size < full.size(); size++) {
    std::vector<char> buffer(size + 1, 0);
    FILE * f = fmemopen(buffer.data(), size, "w");
    ASSERT_NE(f, nullptr);
    std::setvbuf(f, nullptr, _IONBF, 0);
    const GSHSCENE_Result rc = gshscene_resource_dump(r.get(), f);
    std::fclose(f);
    EXPECT_EQ(rc, GSHSCENE_ERR_IO) << "room for " << size << " of " << full.size() << " bytes";
    io += rc == GSHSCENE_ERR_IO;
  }
  EXPECT_GT(io, 100u);
  std::vector<char> buffer(full.size() + 1, 0);
  FILE * f = fmemopen(buffer.data(), full.size() + 1, "w");
  ASSERT_NE(f, nullptr);
  std::setvbuf(f, nullptr, _IONBF, 0);
  EXPECT_EQ(gshscene_resource_dump(r.get(), f), GSHSCENE_OK);
  std::fclose(f);
}

TEST(Primitive, KindNamesArePinned) {
  EXPECT_STREQ(gshscene_primitive_kind_string(GSHSCENE_PRIMITIVE_NONE), "none");
  EXPECT_STREQ(gshscene_primitive_kind_string(GSHSCENE_PRIMITIVE_RECT), "rect");
  EXPECT_STREQ(gshscene_primitive_kind_string(GSHSCENE_PRIMITIVE_ROUNDED_RECT), "rounded-rect");
  EXPECT_STREQ(gshscene_primitive_kind_string(GSHSCENE_PRIMITIVE_ELLIPSE), "ellipse");
  EXPECT_STREQ(gshscene_primitive_kind_string(GSHSCENE_PRIMITIVE_KIND_COUNT), "?");
  EXPECT_EQ((int)GSHSCENE_PRIMITIVE_RECT, 1);
  EXPECT_EQ((int)GSHSCENE_PRIMITIVE_ROUNDED_RECT, 2);
  EXPECT_EQ((int)GSHSCENE_PRIMITIVE_ELLIPSE, 3);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
