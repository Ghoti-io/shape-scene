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
 * N1's bound and saturation (Corey, 2026-10-21; color-paint story P2's change log): every color component the scene
 * records is at most `2^16` in magnitude, refused with ::GSHSCENE_ERR_LIMIT past it, after every validity check, at
 * each entry point that records one (a fill's paint, a shape node's, a solid gradient stop, the foreground of a list
 * and of an edit, a palette's entries, ::gshscene_color_check itself); and the scene's one conversion (G3's resolve)
 * and the test evaluator saturate at `2^64` where N2, N3 and G4 say.
 */

#include "test_helpers.h"

#include "../scene_fixtures.h"
#include "../scene_helpers.h"
#include "ref_composite.h"

#include <cmath>
#include <cstring>
#include <vector>

using namespace scene;

namespace {

const double kMax = 0x1p16;
const double kPast = 0x1.0000000000001p16; // the next double past the bound
const double kSat = 0x1p64;

/** Past the bound, each sign, from just past it to far past it (all finite). */
const double kPastValues[] = {0x1.0000000000001p16, -0x1.0000000000001p16, 0x1p17, -0x1p64, 1e300};

GSHSCENE_Color_Space gamma_space(double g) {
  return space_of(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_GAMMA, g);
}

GSHSCENE_Color color3(int k, double v, double a = 1.0, GSHSCENE_Color_Space sp = GSHSCENE_Color_Space{}) {
  GSHSCENE_Color c{};
  c.rgba[0] = c.rgba[1] = c.rgba[2] = 0.5;
  c.rgba[k] = v;
  c.rgba[3] = a;
  c.space = sp;
  return c;
}

GSHSCENE_Paint paint3(int k, double v, double a = 1.0) {
  GSHSCENE_Paint p = solid(0.5, 0.5, 0.5, a);
  p.rgba[k] = v;
  return p;
}

GSHSCENE_Gradient linear_with(std::vector<GSHSCENE_Gradient_Stop> & stops) {
  GSHSCENE_Gradient g{};
  g.kind = GSHSCENE_GRADIENT_LINEAR;
  g.p1[0] = 8.0;
  gshgeom_map_identity(2, &g.map);
  g.stop_count = stops.size();
  g.stops = stops.data();
  return g;
}

GSHSCENE_Result intern_stops(GSHSCENE_Store * store, std::vector<GSHSCENE_Gradient_Stop> stops) {
  GSHSCENE_Gradient g = linear_with(stops);
  const GSHSCENE_Resource * r = nullptr;
  GSHSCENE_Result result = gshscene_store_intern_gradient(store, &g, &r);
  gshscene_resource_release(r);
  return result;
}

bool same_bits(double a, double b) {
  return std::memcmp(&a, &b, sizeof a) == 0;
}

}  // namespace

TEST(ColorBound, TheBoundIsTwoToTheSixteenAndTheSaturationTwoToTheSixtyFour) {
  EXPECT_EQ(GSHSCENE_COLOR_COMPONENT_MAX, 65536.0);
  EXPECT_EQ(GSHSCENE_COLOR_SATURATION, 18446744073709551616.0);
  EXPECT_EQ(std::nextafter(kMax, 1e300), kPast) << "kPast is one step past the bound";
}

// --- the entry points ---------------------------------------------------------------------------------------------

TEST(ColorBound, TheColorCheckTakesTheBoundAndRefusesOneStepPast) {
  for (int k = 0; k < 3; k++) {
    for (double v : {kMax, -kMax, 0.0, 1.0}) {
      GSHSCENE_Color c = color3(k, v);
      EXPECT_EQ(gshscene_color_check(&c), GSHSCENE_OK) << k << " " << v;
    }
    for (double v : kPastValues) {
      GSHSCENE_Color c = color3(k, v);
      EXPECT_EQ(gshscene_color_check(&c), GSHSCENE_ERR_LIMIT) << k << " " << v;
      // Every validity check is decided first.
      GSHSCENE_Color bad_alpha = color3(k, v, 1.5);
      EXPECT_EQ(gshscene_color_check(&bad_alpha), GSHSCENE_ERR_INVALID) << k << " " << v;
      GSHSCENE_Color bad_space = color3(k, v, 1.0, gamma_space(11.0));
      EXPECT_EQ(gshscene_color_check(&bad_space), GSHSCENE_ERR_INVALID) << k << " " << v;
      GSHSCENE_Color nan_too = color3(k, v);
      nan_too.rgba[(k + 1) % 3] = std::nan("");
      EXPECT_EQ(gshscene_color_check(&nan_too), GSHSCENE_ERR_INVALID) << k << " " << v;
    }
    for (double v : {std::nan(""), HUGE_VAL, -HUGE_VAL}) {
      GSHSCENE_Color c = color3(k, v);
      EXPECT_EQ(gshscene_color_check(&c), GSHSCENE_ERR_INVALID) << "a non-finite value is invalid, not past the bound";
    }
  }
}

TEST(ColorBound, AFillTakesTheBoundAndRefusesOneStepPastAfterEveryValidityCheck) {
  StoreHolder s;
  Ref sq(intern_square(s.store));
  ListHolder l(s.store);
  size_t count = 0;
  for (int k = 0; k < 3; k++) {
    for (double v : {kMax, -kMax}) {
      GSHSCENE_Paint p = paint3(k, v);
      ASSERT_EQ(gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK);
      count++;
    }
    for (double v : kPastValues) {
      GSHSCENE_Paint p = paint3(k, v);
      EXPECT_EQ(gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_ERR_LIMIT)
          << k << " " << v;
      EXPECT_EQ(gshscene_list_count(l.list), count) << "nothing is recorded";
      EXPECT_EQ(gshscene_list_fill(l.list, sq.get(), nullptr, (GSHGEOM_Fill_Rule)99, &p, 0, 0.0), GSHSCENE_ERR_INVALID);
      EXPECT_EQ(gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, std::nan("")),
          GSHSCENE_ERR_INVALID) << "the depth, checked after the paint, is decided first too";
      GSHSCENE_Paint alpha = paint3(k, v, 2.0);
      EXPECT_EQ(gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &alpha, 0, 0.0), GSHSCENE_ERR_INVALID);
      GSHSCENE_Paint spaced = paint3(k, v);
      spaced.space = gamma_space(0.05);
      EXPECT_EQ(gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &spaced, 0, 0.0), GSHSCENE_ERR_INVALID);
      // A foreground or palette paint's components must be zero: any other value is invalid, never past the bound.
      GSHSCENE_Paint fg = paint3(k, v);
      fg.kind = GSHSCENE_PAINT_FOREGROUND;
      EXPECT_EQ(gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &fg, 0, 0.0), GSHSCENE_ERR_INVALID);
      EXPECT_EQ(gshscene_list_count(l.list), count);
    }
  }
}

TEST(ColorBound, TheForegroundOfAListAndOfAnEditTakeTheBoundAndRefuseOneStepPast) {
  StoreHolder s;
  ListHolder l(s.store);
  SceneHolder sc;
  EditHolder e(sc);
  for (int k = 0; k < 3; k++) {
    for (double v : {kMax, -kMax}) {
      GSHSCENE_Color c = color3(k, v);
      ASSERT_EQ(gshscene_list_set_foreground(l.list, &c), GSHSCENE_OK);
      ASSERT_EQ(gshscene_edit_set_foreground(e.edit, &c), GSHSCENE_OK);
    }
    GSHSCENE_Color held_list{};
    GSHSCENE_Color held_edit{};
    ASSERT_EQ(gshscene_list_foreground(l.list, &held_list), GSHSCENE_OK);
    ASSERT_EQ(gshscene_edit_foreground(e.edit, &held_edit), GSHSCENE_OK);
    for (double v : kPastValues) {
      GSHSCENE_Color c = color3(k, v);
      EXPECT_EQ(gshscene_list_set_foreground(l.list, &c), GSHSCENE_ERR_LIMIT) << k << " " << v;
      EXPECT_EQ(gshscene_edit_set_foreground(e.edit, &c), GSHSCENE_ERR_LIMIT) << k << " " << v;
      GSHSCENE_Color bad = color3(k, v, -0.5);
      EXPECT_EQ(gshscene_list_set_foreground(l.list, &bad), GSHSCENE_ERR_INVALID);
      EXPECT_EQ(gshscene_edit_set_foreground(e.edit, &bad), GSHSCENE_ERR_INVALID);
      GSHSCENE_Color now{};
      ASSERT_EQ(gshscene_list_foreground(l.list, &now), GSHSCENE_OK);
      EXPECT_TRUE(same_bits(now.rgba[k], held_list.rgba[k])) << "the list's foreground is unchanged";
      ASSERT_EQ(gshscene_edit_foreground(e.edit, &now), GSHSCENE_OK);
      EXPECT_TRUE(same_bits(now.rgba[k], held_edit.rgba[k])) << "the edit's foreground is unchanged";
    }
  }
}

TEST(ColorBound, APaletteTakesTheBoundAndRefusesAnEntryPastItAfterEveryEntrysValidity) {
  StoreHolder s;
  for (int k = 0; k < 3; k++) {
    std::vector<GSHSCENE_Color> ok = {color3(k, kMax), color3(k, -kMax), color3(k, 0.25)};
    const GSHSCENE_Resource * p = nullptr;
    ASSERT_EQ(gshscene_store_intern_palette(s.store, ok.size(), ok.data(), &p), GSHSCENE_OK);
    gshscene_resource_release(p);
    for (double v : kPastValues) {
      for (size_t at = 0; at < 3; at++) {
        std::vector<GSHSCENE_Color> entries = ok;
        entries[at] = color3(k, v);
        p = nullptr;
        EXPECT_EQ(gshscene_store_intern_palette(s.store, entries.size(), entries.data(), &p), GSHSCENE_ERR_LIMIT)
            << k << " " << v << " at " << at;
        EXPECT_EQ(p, nullptr);
        // An invalid entry anywhere, before or after the one past the bound, is decided first.
        entries[(at + 1) % 3] = color3(k, 0.5, 2.0);
        EXPECT_EQ(gshscene_store_intern_palette(s.store, entries.size(), entries.data(), &p), GSHSCENE_ERR_INVALID)
            << k << " " << v << " at " << at;
      }
    }
  }
}

TEST(ColorBound, AGradientsSolidStopTakesTheBoundAndIsRefusedWithG14) {
  StoreHolder s;
  for (int k = 0; k < 3; k++) {
    for (double v : {kMax, -kMax}) {
      EXPECT_EQ(intern_stops(s.store, {{0.0, paint3(k, v)}, {1.0, solid(0, 0, 0, 1)}}), GSHSCENE_OK);
      EXPECT_EQ(intern_stops(s.store, {{0.0, solid(0, 0, 0, 1)}, {1.0, paint3(k, v)}}), GSHSCENE_OK);
    }
    for (double v : kPastValues) {
      EXPECT_EQ(intern_stops(s.store, {{0.0, paint3(k, v)}, {1.0, solid(0, 0, 0, 1)}}), GSHSCENE_ERR_LIMIT) << k << " " << v;
      EXPECT_EQ(intern_stops(s.store, {{0.0, solid(0, 0, 0, 1)}, {1.0, paint3(k, v)}}), GSHSCENE_ERR_LIMIT) << k << " " << v;
      // A later stop's invalid offset or color is decided first.
      EXPECT_EQ(intern_stops(s.store, {{0.0, paint3(k, v)}, {std::nan(""), solid(0, 0, 0, 1)}}), GSHSCENE_ERR_INVALID);
      EXPECT_EQ(intern_stops(s.store, {{0.0, paint3(k, v)}, {1.0, solid(0, 0, 0, 3.0)}}), GSHSCENE_ERR_INVALID);
      GSHSCENE_Paint fg = paint3(k, v);
      fg.kind = GSHSCENE_PAINT_FOREGROUND;
      EXPECT_EQ(intern_stops(s.store, {{0.0, fg}}), GSHSCENE_ERR_INVALID) << "a foreground stop's components are zero";
    }
  }
}

TEST(ColorBound, AShapeNodesPaintTakesTheBoundOnAddAndOnSet) {
  SceneHolder sc;
  EditHolder e(sc);
  Ref sq(intern_square(sc.store()));
  uint64_t id = 0;
  for (int k = 0; k < 3; k++) {
    for (double v : {kMax, -kMax}) {
      GSHSCENE_Node n = shape_node(sq.get());
      n.paint = paint3(k, v);
      id = add(e.edit, GSHSCENE_ROOT_ID, n);
      ASSERT_NE(id, 0u);
      ASSERT_EQ(gshscene_edit_set(e.edit, id, &n), GSHSCENE_OK);
    }
  }
  size_t count = gshscene_edit_node_count(e.edit);
  GSHSCENE_Node_Info before;
  ASSERT_EQ(gshscene_edit_node(e.edit, id, &before), GSHSCENE_OK);
  for (int k = 0; k < 3; k++) {
    for (double v : kPastValues) {
      GSHSCENE_Node n = shape_node(sq.get());
      n.paint = paint3(k, v);
      uint64_t out = 0;
      EXPECT_EQ(gshscene_edit_add(e.edit, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, &n, nullptr, &out), GSHSCENE_ERR_LIMIT)
          << k << " " << v;
      EXPECT_EQ(gshscene_edit_set(e.edit, id, &n), GSHSCENE_ERR_LIMIT) << k << " " << v;
      n.fill_rule = (GSHGEOM_Fill_Rule)99;
      EXPECT_EQ(gshscene_edit_add(e.edit, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, &n, nullptr, &out), GSHSCENE_ERR_INVALID);
      n.fill_rule = GSHGEOM_FILL_NONZERO;
      n.paint.rgba[3] = 1.25;
      EXPECT_EQ(gshscene_edit_set(e.edit, id, &n), GSHSCENE_ERR_INVALID);
    }
  }
  EXPECT_EQ(gshscene_edit_node_count(e.edit), count);
  GSHSCENE_Node_Info after;
  ASSERT_EQ(gshscene_edit_node(e.edit, id, &after), GSHSCENE_OK);
  for (int k = 0; k < 4; k++) {
    EXPECT_TRUE(same_bits(after.node.paint.rgba[k], before.node.paint.rgba[k])) << "the node is unchanged";
  }
}

// --- the saturation of the scene's one conversion (G3) and of the test evaluator (G4) ------------------------------

TEST(ColorBound, TheResolveOfAStopAtTheBoundSaturatesAtEachStep) {
  StoreHolder s;
  GSHSCENE_Color fg;
  gshscene_color_init(&fg);
  GSHSCENE_Color_Space target = srgb_space();
  struct Case {
    GSHSCENE_Color_Space from;
    GSHSCENE_Color_Space into;
    double rgb[3];
    double want[3];
  };
  const GSHSCENE_Color_Space bt2020_g10 = space_of(GCOL_PRIMARIES_BT2020, GCOL_TRANSFER_GAMMA, 10.0);
  const Case cases[] = {
      // decode (2^160) and encode ((2^64)^10) saturate: exactly 2^64
      {gamma_space(10.0), gamma_space(0.1), {kMax, -kMax, 0.0}, {kSat, -kSat, 0.0}},
      // the decode saturates before a gamma-2 encode: 2^32, not 2^80
      {gamma_space(10.0), gamma_space(2.0), {kMax, -kMax, 0.0}, {0x1p32, -0x1p32, 0.0}},
      // a linear stop's encode into gamma 0.1
      {linear_srgb_space(), gamma_space(0.1), {kMax, -kMax, 1.0}, {kSat, -kSat, 1.0}},
      // the matrix (2.32 * 2^64 before it) saturates, then a gamma-10 encode: (2^64)^(1/10), not (2.32 * 2^64)^(1/10)
      {bt2020_g10, gamma_space(10.0), {kMax, -kMax, -kMax}, {0x1.51cb453b9536ep+6, -0x1.51cb453b9536ep+6, -0x1.51cb453b9536ep+6}},
      // the same space: copied exactly, never saturated
      {linear_srgb_space(), linear_srgb_space(), {kMax, -kMax, 0.5}, {kMax, -kMax, 0.5}},
  };
  for (const Case & c : cases) {
    std::vector<GSHSCENE_Gradient_Stop> stops = {{0.0, solid(c.rgb[0], c.rgb[1], c.rgb[2], 0.5)}};
    stops[0].color.space = c.from;
    GSHSCENE_Gradient g = linear_with(stops);
    g.space = c.into;
    const GSHSCENE_Resource * r = nullptr;
    ASSERT_EQ(gshscene_store_intern_gradient(s.store, &g, &r), GSHSCENE_OK);
    Ref held(r);
    GSHSCENE_Resolved_Stop out[1];
    ASSERT_EQ(gshscene_gradient_resolve(r, &fg, nullptr, &target, nullptr, out, 1), GSHSCENE_OK);
    for (int k = 0; k < 3; k++) {
      EXPECT_TRUE(std::isfinite(out[0].rgba[k]));
      EXPECT_LE(std::fabs(out[0].rgba[k]), kSat);
      double err = std::fabs(out[0].rgba[k] - c.want[k]) / std::max(1.0, std::fabs(c.want[k]));
      EXPECT_LE(err, GSHSCENE_RESOLVE_TOLERANCE) << k << ": " << out[0].rgba[k] << " want " << c.want[k];
    }
    EXPECT_EQ(out[0].rgba[3], 0.5) << "alpha is never converted";
  }
}

TEST(ColorBound, TheEvaluatorsUnpremultiplySaturatesWhereDoublesWouldPassTheBound) {
  // G4 with the flag: two stops of color 2^64 (a resolved stop may be at the saturation) and alphas of one and two
  // subnormal steps.  At t = 1/4 the alpha 1.25 * 2^-1074 rounds to 2^-1074 while the color's interpolant is exact,
  // so a plain division gives 1.25 * 2^64; the definition's answer (a mean of 2^64 and 2^64) is 2^64.
  std::vector<refc::GStop> stops = {{0.0, {kSat, -kSat, 0.0, 0x1p-1074}}, {1.0, {kSat, -kSat, 0.0, 0x1p-1073}}};
  const double line[6] = {0, 0, 4, 0, 0, 0};
  std::vector<refc::GMap> chain = {refc::GMap{{1, 0, 0, 1, 0, 0}}};
  refc::GPoint at = refc::gradient_at(GSHSCENE_GRADIENT_LINEAR, line, 0, GSHSCENE_EXTEND_PAD, 1, stops, chain, 1, 0, 1);
  ASSERT_EQ(at.drawn, 1);
  EXPECT_EQ(at.rgba[0], kSat);
  EXPECT_EQ(at.rgba[1], -kSat);
  EXPECT_EQ(at.rgba[3], 0x1p-1074);
  // And the composite primitives saturate: plus of two values at the bound, and an un-premultiply past it.
  refc::Px d = {{-kSat, kSat, 0.0, 1.0}};
  refc::Px out = refc::apply(d, d, 1.0, GSHSCENE_COMPOSITE_PLUS);
  EXPECT_EQ(out.v[0], -kSat);
  EXPECT_EQ(out.v[1], 1.0) << "plus clamps from above at 1 first";
  refc::Px small = {{-kSat, 0.0, 0.0, 0.5}};
  refc::Px moved = refc::convert_premultiplied(small, refc::Space{1, 3, 0.1}, refc::Space{1, 1, 0.0});
  EXPECT_EQ(moved.v[0], -gmath_pow(kSat, 0.1) * 0.5) << "the un-premultiply's 2^65 saturates before the decode";
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
