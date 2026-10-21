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
 * What a closed list means (N1 to N10, B1 to B14): every case of the committed vectors is built
 * through the API, closed, and evaluated by the test-only evaluator in doubles, and
 * the answer must be the exact one the rational oracle computed (story 12).  This is
 * the check that a list expresses the definition without ambiguity, and the harness
 * that story 13's shape-cpu is measured with.
 */

#include "scene_helpers.h"
#include "scene_fixtures.h"

#include <ghoti.io/shape-scene/fixed.h>

#include "../composite_vectors.h"
#include "../group_vectors.h"
#include "../known_answers.h"
#include "ref_composite.h"

#include <cmath>
#include <random>

using namespace scene;
namespace cv = composite_vectors;

namespace {

GSHSCENE_Color_Space space_from(const cv::VSpace & s) {
  GSHSCENE_Color_Space out;
  std::memset(&out, 0, sizeof out);
  out.primaries = (GCOL_Primaries)s.primaries;
  out.transfer = (GCOL_Transfer)s.transfer;
  out.gamma = s.gamma;
  return out;
}

/** Build the list of a case; @p coverage gets the coverage of each item. */
void build(const cv::VCase & c, GSHSCENE_Store * store, const GSHSCENE_Resource * square, ListHolder * l,
    std::vector<double> * coverage) {
  GSHSCENE_Color fg;
  for (int k = 0; k < 4; k++) {
    fg.rgba[k] = c.fg[k];
  }
  fg.space = space_from(c.fg_space);
  ASSERT_EQ(gshscene_list_set_foreground(l->list, &fg), GSHSCENE_OK) << c.name;
  if (c.palette_count >= 0) {
    std::vector<GSHSCENE_Color> entries((size_t)c.palette_count);
    for (int e = 0; e < c.palette_count; e++) {
      for (int k = 0; k < 4; k++) {
        entries[(size_t)e].rgba[k] = c.palette[e].rgba[k];
      }
      entries[(size_t)e].space = space_from(c.palette[e].space);
    }
    const GSHSCENE_Resource * palette = nullptr;
    ASSERT_EQ(gshscene_store_intern_palette(store, entries.size(), entries.data(), &palette), GSHSCENE_OK) << c.name;
    ASSERT_EQ(gshscene_list_set_palette(l->list, palette), GSHSCENE_OK) << c.name;
    gshscene_resource_release(palette); /* the list holds its own */
  }
  for (int i = 0; i < c.item_count; i++) {
    const cv::VItem & it = c.items[i];
    coverage->push_back(it.coverage);
    switch (it.kind) {
      case 1: {
        GSHSCENE_Paint p;
        std::memset(&p, 0, sizeof p);
        p.kind = (GSHSCENE_Paint_Kind)it.paint;
        for (int k = 0; k < 4; k++) {
          p.rgba[k] = it.rgba[k];
        }
        p.space = space_from(it.space);
        p.entry = (uint32_t)it.entry;
        ASSERT_EQ(gshscene_list_fill(l->list, square, nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK) << c.name;
        break;
      }
      case 2:
        ASSERT_EQ(gshscene_list_clip_push(l->list, square, nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0), GSHSCENE_OK) << c.name;
        break;
      case 3:
        ASSERT_EQ(gshscene_list_clip_pop(l->list), GSHSCENE_OK) << c.name;
        break;
      case 4: {
        GSHSCENE_Group g = make_group(it.opacity, GSHSCENE_ORDER_FLAT, (uint32_t)it.composite);
        g.blend_space = space_from(it.blend);
        ASSERT_EQ(gshscene_list_group_push(l->list, &g, 0, 0.0), GSHSCENE_OK) << c.name;
        break;
      }
      default:
        ASSERT_EQ(gshscene_list_group_pop(l->list), GSHSCENE_OK) << c.name;
        break;
    }
  }
  ASSERT_EQ(gshscene_list_close(l->list), GSHSCENE_OK) << c.name;
}

}  // namespace

TEST(Composite, EveryVectorIsTheExactAnswerOfTheOracle) {
  StoreHolder s;
  Ref sq(intern_square(s.store));
  int exact = 0;
  for (int n = 0; n < cv::kCaseCount; n++) {
    const cv::VCase & c = cv::kCases[n];
    ListHolder l(s.store);
    std::vector<double> coverage;
    build(c, s.store, sq.get(), &l, &coverage);
    refc::Px backdrop = {{c.backdrop[0], c.backdrop[1], c.backdrop[2], c.backdrop[3]}};
    refc::Px got = refc::evaluate(l.list, coverage, refc::Space{c.target.primaries, c.target.transfer, c.target.gamma}, backdrop);
    // N1 to N14: within 1e-13 where a transfer or a matrix is evaluated; a blend (STOP-2): within 1e-12.
    bool blends = false;
    for (int i = 0; i < c.item_count; i++) {
      blends = blends || (c.items[i].kind == 4 && refc::is_blend(c.items[i].composite));
    }
    const double tolerance = blends ? 1e-12 : 1e-13;
    for (int k = 0; k < 4; k++) {
      EXPECT_TRUE(std::isfinite(got.v[k])) << c.name;
      if (c.exact) {
        EXPECT_EQ(got.v[k], c.want[k]) << c.name << " channel " << k << " (exact: bit for bit)";
      } else {
        EXPECT_NEAR(got.v[k], c.want[k], tolerance * std::max(1.0, std::fabs(c.want[k]))) << c.name << " channel " << k;
      }
    }
    exact += c.exact ? 1 : 0;
  }
  EXPECT_EQ(exact, cv::kExactCaseCount);
  EXPECT_GE(cv::kExactCaseCount, 50) << "most of the table is exact: dyadic values, no conversion";
  EXPECT_GE(cv::kCaseCount - cv::kExactCaseCount, 10) << "and some evaluates a transfer or a matrix";
}

TEST(Blend, EveryMixingFunctionValueIsTheOraclesAnswer) {
  // B7 to B9 at every branch, threshold, out-of-range value, pole and corner of the table: bit for bit where every
  // step is exact in doubles (STOP-2), else within 1e-12 of max(1, |v|); never a NaN or an infinity.
  int exact = 0;
  double worst = 0.0;
  for (int n = 0; n < cv::kMixCount; n++) {
    const cv::VMix & m = cv::kMix[n];
    double got[3];
    refc::mix(m.mode, m.cb, m.cs, got);
    for (int k = 0; k < 3; k++) {
      ASSERT_TRUE(std::isfinite(got[k])) << n;
      if (m.exact) {
        EXPECT_EQ(got[k], m.want[k]) << "kMix[" << n << "] " << gshscene_composite_string((GSHSCENE_Composite)m.mode)
                                     << " channel " << k << " (exact)";
      } else {
        double e = std::fabs(got[k] - m.want[k]) / std::max(1.0, std::fabs(m.want[k]));
        worst = std::max(worst, e);
        EXPECT_LE(e, 1e-12) << "kMix[" << n << "] " << gshscene_composite_string((GSHSCENE_Composite)m.mode) << " channel " << k;
      }
    }
    exact += m.exact ? 1 : 0;
  }
  EXPECT_EQ(exact, cv::kMixExactCount);
  EXPECT_GE(cv::kMixExactCount, 500) << "most separable samples are exact";
  std::printf("kMix: %d samples, %d exact; worst relative error %.3g\n", cv::kMixCount, exact, worst);
  // Every mode is in the table.
  for (int mode = GSHSCENE_COMPOSITE_SCREEN; mode <= GSHSCENE_COMPOSITE_LUMINOSITY; mode++) {
    bool seen = false;
    for (int n = 0; n < cv::kMixCount; n++) {
      seen = seen || cv::kMix[n].mode == mode;
    }
    EXPECT_TRUE(seen) << gshscene_composite_string((GSHSCENE_Composite)mode);
  }
}

TEST(Blend, ClipColorARoundingOffTheRayIsFiniteAndTheOraclesAnswer) {
  // B10: a triple a rounding off C = L, where Lum(C) - n (or x - Lum(C)) in doubles is 0 and the literal step is
  // 0 / 0; formed from the differences C - n the step is defined and accurate.
  ASSERT_GE(cv::kNearRayCount, 20);
  double worst = 0.0;
  for (int n = 0; n < cv::kNearRayCount; n++) {
    const cv::VNearRay & r = cv::kNearRay[n];
    double got[3];
    refc::mix(r.mode, r.c, r.c, got);
    for (int k = 0; k < 3; k++) {
      ASSERT_TRUE(std::isfinite(got[k])) << "kNearRay[" << n << "] channel " << k;
      double e = std::fabs(got[k] - r.want[k]) / std::max(1.0, std::fabs(r.want[k]));
      worst = std::max(worst, e);
      EXPECT_LE(e, 1e-12) << "kNearRay[" << n << "] " << gshscene_composite_string((GSHSCENE_Composite)r.mode)
                          << " channel " << k;
    }
  }
  std::printf("kNearRay: %d samples; worst relative error %.3g\n", cv::kNearRayCount, worst);
}

TEST(Blend, EveryModeAtTheCornersIsFiniteAndSaturated) {
  // B9: inputs at +-2^64 (and 0, 1) on every channel, every mode: no NaN, no infinity, nothing past the saturation.
  // (The table holds the corners whose double evaluation is conditioned; where it cancels, as screen's 2^64 + 1 -
  // 2^64 does, the generator refuses the sample and only this bound is asserted.)
  const double corner[] = {0x1p64, -0x1p64, 0.0, 1.0, 0x1p16, -0x1p16};
  for (int mode = GSHSCENE_COMPOSITE_SCREEN; mode <= GSHSCENE_COMPOSITE_LUMINOSITY; mode++) {
    for (double a : corner) {
      for (double b : corner) {
        for (double c : corner) {
          double cb[3] = {a, b, c}, cs[3] = {c, a, b}, out[3];
          refc::mix(mode, cb, cs, out);
          for (double v : out) {
            ASSERT_TRUE(std::isfinite(v)) << gshscene_composite_string((GSHSCENE_Composite)mode);
            ASSERT_LE(std::fabs(v), 0x1p64) << gshscene_composite_string((GSHSCENE_Composite)mode);
          }
          refc::Px s = {{cs[0], cs[1], cs[2], 1.0}}, d = {{cb[0], cb[1], cb[2], 1.0}};
          refc::Px o = refc::apply(d, s, 0.5, mode);
          for (double v : o.v) {
            ASSERT_TRUE(std::isfinite(v)) << gshscene_composite_string((GSHSCENE_Composite)mode);
          }
        }
      }
    }
  }
}

TEST(Blend, CoverageAndOpacityCommuteWithBlending) {
  // B4: apply(D, S, m, op_X) = op_X(m S, D), and an opacity scales S without changing Cs.
  std::mt19937_64 rng(4);
  std::uniform_real_distribution<double> unit(0.0, 1.0), wide(-2.0, 3.0);
  for (int t = 0; t < 4000; t++) {
    int mode = GSHSCENE_COMPOSITE_SCREEN + (int)(rng() % 15);
    double as = unit(rng), ab = unit(rng), m = unit(rng);
    refc::Px s = {{wide(rng) * as, wide(rng) * as, wide(rng) * as, as}};
    refc::Px d = {{wide(rng) * ab, wide(rng) * ab, wide(rng) * ab, ab}};
    refc::Px a = refc::apply(d, s, m, mode);
    refc::Px ms = {{m * s.v[0], m * s.v[1], m * s.v[2], m * s.v[3]}};
    refc::Px b = refc::op(mode, ms, d);
    for (int k = 0; k < 4; k++) {
      EXPECT_NEAR(a.v[k], b.v[k], 1e-12 * std::max(1.0, std::fabs(b.v[k]))) << gshscene_composite_string((GSHSCENE_Composite)mode);
    }
  }
}

TEST(Blend, TheIdentitiesOfB7AndB8HoldInTheEvaluator) {
  const double values[] = {0.0, 0.25, 0.5, 0.75, 1.0, -0.5, 1.5, 65536.0};
  for (double b : values) {
    for (double s : values) {
      // Overlay is hard-light exchanged; luminosity(b, s) is color(s, b); difference and exclusion are symmetric.
      EXPECT_EQ(refc::mix_separable(GSHSCENE_COMPOSITE_OVERLAY, b, s), refc::mix_separable(GSHSCENE_COMPOSITE_HARD_LIGHT, s, b));
      EXPECT_EQ(refc::mix_separable(GSHSCENE_COMPOSITE_DIFFERENCE, b, s), refc::mix_separable(GSHSCENE_COMPOSITE_DIFFERENCE, s, b));
      EXPECT_EQ(refc::mix_separable(GSHSCENE_COMPOSITE_EXCLUSION, b, s), refc::mix_separable(GSHSCENE_COMPOSITE_EXCLUSION, s, b));
      double cb[3] = {b, s, 0.25}, cs[3] = {s, 0.75, b}, x[3], y[3];
      refc::mix(GSHSCENE_COMPOSITE_LUMINOSITY, cb, cs, x);
      refc::mix(GSHSCENE_COMPOSITE_COLOR, cs, cb, y);
      for (int k = 0; k < 3; k++) {
        EXPECT_EQ(x[k], y[k]);
      }
    }
    EXPECT_EQ(refc::mix_separable(GSHSCENE_COMPOSITE_SOFT_LIGHT, b, 0.5), b) << "soft-light at Cs = 1/2 is Cb";
  }
  // B12: normal (B = Cs) is source-over; B3: every mode is bounded, exactly.
  refc::Px empty = {{0, 0, 0, 0}}, d = {{0.25, -0.125, 2.5, 0.5}};
  for (int mode = GSHSCENE_COMPOSITE_SCREEN; mode <= GSHSCENE_COMPOSITE_LUMINOSITY; mode++) {
    refc::Px o = refc::op(mode, empty, d);
    for (int k = 0; k < 4; k++) {
      EXPECT_EQ(o.v[k], d.v[k]) << gshscene_composite_string((GSHSCENE_Composite)mode);
    }
  }
}

TEST(Blend, AListOfBlendingGroupsHashesAndDumpsAsTheGeneratorSays) {
  // B14: no spelling changes; the u8 composite holds 13 to 27.
  StoreHolder s;
  Ref sq(intern_square(s.store));
  Ref p2(intern_fixture(s.store));
  ListHolder l(s.store);
  GSHGEOM_Map t = translate(1.5, -2.25);
  GSHSCENE_Paint a = solid(0.25, 0.5, 0.75, 1.0), b = solid(1.0, 0.0, 0.5, 0.75), c = solid(-0.5, 1.5, 0.5, 0.5);
  GSHSCENE_Group screen = make_group(0.5, GSHSCENE_ORDER_FLAT, GSHSCENE_COMPOSITE_SCREEN);
  GSHSCENE_Group lum = make_group(1.0, GSHSCENE_ORDER_FLAT, GSHSCENE_COMPOSITE_LUMINOSITY);
  lum.blend_space = linear_srgb_space();
  GSHSCENE_Group soft = make_group(0.25, GSHSCENE_ORDER_FLAT, GSHSCENE_COMPOSITE_SOFT_LIGHT);
  GSHSCENE_Group mul = make_group(1.0, GSHSCENE_ORDER_FLAT, GSHSCENE_COMPOSITE_MULTIPLY);
  ASSERT_EQ(gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &a, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_push(l.list, &screen, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(l.list, p2.get(), &t, GSHGEOM_FILL_EVEN_ODD, &b, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_pop(l.list), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_push(l.list, &lum, 3, -1.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_push(l.list, &soft, 2, 1.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &c, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_pop(l.list), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_pop(l.list), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_push(l.list, &mul, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_pop(l.list), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_hash(l.list), known::kBlendListHash);
  EXPECT_EQ(dump_list(l.list), known::kBlendListDump);
}

TEST(Blend, EveryModeIsRecordedOnEveryPathAndNothingPastIt) {
  // B1: a list's group push, the float-free push and a group node take 13 to 27; 28 and 255 are refused.
  StoreHolder s;
  ListHolder l(s.store);
  for (uint32_t op = GSHSCENE_COMPOSITE_SCREEN; op <= GSHSCENE_COMPOSITE_LUMINOSITY; op++) {
    GSHSCENE_Group g = make_group(1.0, GSHSCENE_ORDER_FLAT, op);
    ASSERT_EQ(gshscene_list_group_push(l.list, &g, 0, 0.0), GSHSCENE_OK) << op;
    ASSERT_EQ(gshscene_list_group_pop(l.list), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_group_push_fixed(l.list, op, 0), GSHSCENE_OK) << op;
    ASSERT_EQ(gshscene_list_group_pop(l.list), GSHSCENE_OK);
  }
  for (uint32_t op : {(uint32_t)GSHSCENE_COMPOSITE_COUNT, 255u}) {
    GSHSCENE_Group g = make_group(1.0, GSHSCENE_ORDER_FLAT, op);
    EXPECT_EQ(gshscene_list_group_push(l.list, &g, 0, 0.0), GSHSCENE_ERR_INVALID) << op;
    EXPECT_EQ(gshscene_list_group_push_fixed(l.list, op, 0), GSHSCENE_ERR_INVALID) << op;
  }
  for (size_t i = 0; i < gshscene_list_count(l.list); i += 2) {
    const GSHSCENE_Item * item = nullptr;
    ASSERT_EQ(gshscene_list_item(l.list, i, &item), GSHSCENE_OK);
    EXPECT_EQ(item->group.composite, GSHSCENE_COMPOSITE_SCREEN + i / 4) << i;
  }
  SceneHolder sc(nullptr, nullptr, s.store);
  EditHolder e(sc);
  for (uint32_t op : {(uint32_t)GSHSCENE_COMPOSITE_SCREEN, (uint32_t)GSHSCENE_COMPOSITE_LUMINOSITY}) {
    GSHSCENE_Node n = group_node();
    n.composite = op;
    EXPECT_EQ(gshscene_edit_add(e.edit, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, &n, nullptr, nullptr), GSHSCENE_OK) << op;
  }
  GSHSCENE_Node n = group_node();
  n.composite = GSHSCENE_COMPOSITE_COUNT;
  EXPECT_EQ(gshscene_edit_add(e.edit, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, &n, nullptr, nullptr), GSHSCENE_ERR_INVALID);
}

TEST(Composite, TheOperatorsAreN4sThirteenAndB1sFifteenAndSixAreUnbounded) {
  int unbounded = 0;
  for (int op = 0; op < (int)GSHSCENE_COMPOSITE_COUNT; op++) {
    unbounded += gshscene_composite_is_unbounded((GSHSCENE_Composite)op);
    EXPECT_STRNE(gshscene_composite_string((GSHSCENE_Composite)op), "unknown composite") << op;
  }
  EXPECT_EQ(unbounded, 6);
  for (GSHSCENE_Composite op : {GSHSCENE_COMPOSITE_CLEAR, GSHSCENE_COMPOSITE_SRC, GSHSCENE_COMPOSITE_SRC_IN,
           GSHSCENE_COMPOSITE_DEST_IN, GSHSCENE_COMPOSITE_SRC_OUT, GSHSCENE_COMPOSITE_DEST_ATOP}) {
    EXPECT_EQ(gshscene_composite_is_unbounded(op), 1) << gshscene_composite_string(op);
  }
  EXPECT_EQ(gshscene_composite_is_unbounded(GSHSCENE_COMPOSITE_COUNT), 0);
  EXPECT_EQ(gshscene_composite_is_unbounded((GSHSCENE_Composite)99), 0);
  EXPECT_STREQ(gshscene_composite_string(GSHSCENE_COMPOSITE_COUNT), "unknown composite");
  EXPECT_STREQ(gshscene_composite_string(GSHSCENE_COMPOSITE_SRC_OVER), "src-over");
  EXPECT_STREQ(gshscene_composite_string(GSHSCENE_COMPOSITE_PLUS), "plus");
  // B1: W3C's names for the blend modes, in COLRv1's order.
  const char * w3c[] = {"screen", "overlay", "darken", "lighten", "color-dodge", "color-burn", "hard-light", "soft-light",
      "difference", "exclusion", "multiply", "hue", "saturation", "color", "luminosity"};
  for (int i = 0; i < 15; i++) {
    EXPECT_STREQ(gshscene_composite_string((GSHSCENE_Composite)(GSHSCENE_COMPOSITE_SCREEN + i)), w3c[i]);
  }
  EXPECT_EQ((int)GSHSCENE_COMPOSITE_SRC_OVER, 0) << "a zeroed group is source-over";
  // The evaluator's own factors agree with the unbounded property: with an empty source the backdrop changes iff unbounded.
  refc::Px empty = {{0, 0, 0, 0}};
  refc::Px backdrop = {{0.25, 0.125, 0.5, 0.5}};
  for (int op = 0; op < (int)GSHSCENE_COMPOSITE_COUNT; op++) {
    refc::Px out = refc::op(op, empty, backdrop);
    bool changed = false;
    for (int k = 0; k < 4; k++) {
      changed = changed || out.v[k] != backdrop.v[k];
    }
    EXPECT_EQ(changed, gshscene_composite_is_unbounded((GSHSCENE_Composite)op) != 0) << gshscene_composite_string((GSHSCENE_Composite)op);
  }
}

TEST(Composite, TheEvaluatorsLimitsAreTheDefinitionsLimits) {
  // N5: no coverage changes nothing, full coverage is the operator.
  refc::Px d = {{0.25, 0.125, 0.5, 0.5}}, s = {{0.75, 0.375, 0.0, 0.75}};
  for (int op = 0; op < (int)GSHSCENE_COMPOSITE_COUNT; op++) {
    refc::Px none = refc::apply(d, s, 0.0, op);
    refc::Px full = refc::apply(d, s, 1.0, op);
    refc::Px o = refc::op(op, s, d);
    for (int k = 0; k < 4; k++) {
      EXPECT_EQ(none.v[k], d.v[k]) << op;
      EXPECT_EQ(full.v[k], o.v[k]) << op;
    }
  }
}

TEST(Groups, EveryCombinationOfIsolatedAndKnockoutIsTheOraclesAnswer) {
  // T1 to T7: the general model's vectors, each scene in the four combinations, recorded through the API (the group
  // fields are recorded and read back) and evaluated by the test evaluator's statement of T3 to T5.
  namespace gv = group_vectors;
  auto space = [](const gv::VSpace & v) {
    GSHSCENE_Color_Space out{};
    out.primaries = (GCOL_Primaries)v.primaries;
    out.transfer = (GCOL_Transfer)v.transfer;
    out.gamma = v.gamma;
    return out;
  };
  StoreHolder s;
  Ref sq(intern_square(s.store));
  int exact = 0, combos[2][2] = {{0, 0}, {0, 0}};
  double worst = 0.0;
  for (int n = 0; n < gv::kCaseCount; n++) {
    const gv::VCase & c = gv::kCases[n];
    ListHolder l(s.store);
    std::vector<double> coverage;
    for (int i = 0; i < c.item_count; i++) {
      const gv::VItem & it = c.items[i];
      coverage.push_back(it.coverage);
      if (it.kind == 1) {
        GSHSCENE_Paint p = solid(it.rgba[0], it.rgba[1], it.rgba[2], it.rgba[3]);
        p.space = space(it.space);
        ASSERT_EQ(gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK) << c.name;
      } else if (it.kind == 2) {
        ASSERT_EQ(gshscene_list_clip_push(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0), GSHSCENE_OK);
      } else if (it.kind == 3) {
        ASSERT_EQ(gshscene_list_clip_pop(l.list), GSHSCENE_OK);
      } else if (it.kind == 4) {
        GSHSCENE_Group g = make_group(it.opacity, GSHSCENE_ORDER_FLAT, (uint32_t)it.composite);
        g.blend_space = space(it.blend);
        g.isolation = (uint32_t)it.isolation;
        g.knockout = (uint32_t)it.knockout;
        ASSERT_EQ(gshscene_list_group_push(l.list, &g, 0, 0.0), GSHSCENE_OK) << c.name;
        if (i == 0) {
          combos[it.isolation][it.knockout]++;
        }
      } else {
        ASSERT_EQ(gshscene_list_group_pop(l.list), GSHSCENE_OK);
      }
    }
    ASSERT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
    refc::Px backdrop = {{c.backdrop[0], c.backdrop[1], c.backdrop[2], c.backdrop[3]}};
    refc::Space target{c.target.primaries, c.target.transfer, c.target.gamma};
    refc::Px got = refc::evaluate_general(l.list, coverage, target, backdrop);
    for (int k = 0; k < 4; k++) {
      ASSERT_TRUE(std::isfinite(got.v[k])) << c.name;
      if (c.exact) {
        EXPECT_EQ(got.v[k], c.want[k]) << c.name << " channel " << k << " (exact)";
      } else {
        double e = std::fabs(got.v[k] - c.want[k]) / std::max(1.0, std::fabs(c.want[k]));
        worst = std::max(worst, e);
        EXPECT_LE(e, 1e-12) << c.name << " channel " << k;
      }
    }
    exact += c.exact ? 1 : 0;
  }
  EXPECT_EQ(exact, gv::kExactCaseCount);
  for (auto & row : combos) {
    for (int count : row) {
      EXPECT_GE(count, 10) << "each combination of isolated and knockout has its scenes";
    }
  }
  std::printf("group_vectors: %d cases, %d exact; worst relative error %.3g\n", gv::kCaseCount, exact, worst);
}

TEST(Groups, TheDefaultCombinationIsTheCompositeVectors) {
  // T6: on every case of composite_vectors.h (every group isolated and not knockout), the general evaluator gives what
  // the N-rules' evaluator gives, bit for bit; the generator requires the same of the oracle.
  StoreHolder s;
  Ref sq(intern_square(s.store));
  for (int n = 0; n < cv::kCaseCount; n++) {
    const cv::VCase & c = cv::kCases[n];
    bool solid_only = c.palette_count < 0;
    for (int i = 0; i < c.item_count; i++) {
      solid_only = solid_only && !(c.items[i].kind == 1 && c.items[i].paint != 0);
    }
    if (!solid_only) {
      continue; // the general evaluator states fills of solid paint only
    }
    ListHolder l(s.store);
    std::vector<double> coverage;
    build(c, s.store, sq.get(), &l, &coverage);
    refc::Px backdrop = {{c.backdrop[0], c.backdrop[1], c.backdrop[2], c.backdrop[3]}};
    refc::Space target{c.target.primaries, c.target.transfer, c.target.gamma};
    refc::Px a = refc::evaluate(l.list, coverage, target, backdrop);
    refc::Px b = refc::evaluate_general(l.list, coverage, target, backdrop);
    for (int k = 0; k < 4; k++) {
      EXPECT_NEAR(a.v[k], b.v[k], 1e-13 * std::max(1.0, std::fabs(a.v[k]))) << c.name;
      if (c.exact) {
        EXPECT_EQ(a.v[k], b.v[k]) << c.name << " (exact)";
      }
    }
  }
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
