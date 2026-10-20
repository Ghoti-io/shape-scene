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
 * What a closed list means (N1 to N10): every case of the committed vectors is built
 * through the API, closed, and evaluated by the test-only evaluator in doubles, and
 * the answer must be the exact one the rational oracle computed (story 12).  This is
 * the check that a list expresses the definition without ambiguity, and the harness
 * that story 13's shape-cpu is measured with.
 */

#include "scene_helpers.h"

#include "../composite_vectors.h"
#include "ref_composite.h"

#include <cmath>

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
    for (int k = 0; k < 4; k++) {
      if (c.exact) {
        EXPECT_EQ(got.v[k], c.want[k]) << c.name << " channel " << k << " (exact: bit for bit)";
      } else {
        EXPECT_NEAR(got.v[k], c.want[k], 1e-13) << c.name << " channel " << k;
      }
    }
    exact += c.exact ? 1 : 0;
  }
  EXPECT_EQ(exact, cv::kExactCaseCount);
  EXPECT_GE(cv::kExactCaseCount, 50) << "most of the table is exact: dyadic values, no conversion";
  EXPECT_GE(cv::kCaseCount - cv::kExactCaseCount, 10) << "and some evaluates a transfer or a matrix";
}

TEST(Composite, TheOperatorsAreTheThirteenOfN4AndSixAreUnbounded) {
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

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
