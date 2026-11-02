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
 * What a pattern fill means (PT5 to PT10), against the independent oracle: the double-precision reference evaluator of
 * ref_pattern.h reproduces every scene of the committed vectors within the vector's tolerance; each defect the evaluator can
 * be told to commit makes that comparison fail; the library's cell positions (::gshscene_pattern_cell) are the oracle's exact
 * ones to one ulp, abutting cells share their edge to the bit, and the two planted ways of forming a position are off by far
 * more; and the overlap predicate and the notice decision agree with the vectors.
 */

#include "../pattern_vectors.h"  // first: the installed vectors header stands alone

#include "test_helpers.h"

#include "ref_pattern.h"

#include <cstdint>
#include <cstring>

namespace pv = pattern_vectors;
namespace pr = pattern_ref;

namespace {

/** The number of doubles between @p a and @p b. */
uint64_t ulp_distance(double a, double b) {
  auto key = [](double v) {
    int64_t u;
    std::memcpy(&u, &v, sizeof u);
    return u >= 0 ? u : -(int64_t)((uint64_t)u & 0x7fffffffffffffffull);
  };
  int64_t d = key(a) - key(b);
  return (uint64_t)(d < 0 ? -d : d);
}

/** A library pattern with the geometry of a position or overlap vector (the tile is not needed by the lattice functions). */
GSHSCENE_Pattern geometry(const double cell[4], const double step[2], int rx = 1, int ry = 1) {
  GSHSCENE_Pattern p;
  gshscene_pattern_init(&p);
  p.x = cell[0];
  p.y = cell[1];
  p.w = cell[2];
  p.h = cell[3];
  p.x_step = step[0];
  p.y_step = step[1];
  p.repeat_x = (uint32_t)rx;
  p.repeat_y = (uint32_t)ry;
  return p;
}

bool abutting_x(const pv::VPosition & c) { return c.step[0] == c.cell[2]; }
bool abutting_y(const pv::VPosition & c) { return c.step[1] == c.cell[3]; }

/** Whether the planted evaluator's cell function is seen by the position vectors: off by more than an ulp, or an abutting edge not shared. */
bool positions_catch(pr::Plant plant) {
  pr::PlantScope scope(plant);
  for (int k = 0; k < pv::kPositionCount; k++) {
    const pv::VPosition & c = pv::kPositions[k];
    double e[4];
    pr::cell(c.cell, c.step, c.i, c.j, e);
    for (int q = 0; q < 4; q++) {
      if (ulp_distance(e[q], c.edges[q]) > 1) {
        return true;
      }
    }
    double next[4];
    if (abutting_x(c)) {
      pr::cell(c.cell, c.step, c.i + 1, c.j, next);
      if (e[2] != next[0]) {
        return true;
      }
    }
    if (abutting_y(c)) {
      pr::cell(c.cell, c.step, c.i, c.j + 1, next);
      if (e[3] != next[1]) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace

TEST(PatternOracle, VectorsAreWellFormed) {
  EXPECT_GE(pv::kSceneCount, 30);
  EXPECT_GE(pv::kPositionCount, 90);
  EXPECT_GE(pv::kOverlapCount, 30);
  EXPECT_GE(pv::kNoticeCount, 12);
  for (int k = 0; k < pv::kSceneCount; k++) {
    const pv::VScene & s = pv::kScenes[k];
    ASSERT_NE(s.pattern, nullptr) << s.name;
    EXPECT_GT(s.width, 0) << s.name;
    EXPECT_GT(s.height, 0) << s.name;
    EXPECT_GT(s.tolerance, 0.0) << s.name;
    for (int q = 0; q < s.width * s.height * 4; q++) {
      double v = s.pixels[q];
      EXPECT_TRUE(v >= 0.0 && v <= 1.0) << s.name << " value " << q << " is " << v;  // premultiplied: in [0, 1]
    }
  }
}

TEST(PatternOracle, EvaluatorReproducesEveryScene) {
  ASSERT_EQ(pr::g_plant, pr::kNoPlant);
  for (int k = 0; k < pv::kSceneCount; k++) {
    const pv::VScene & s = pv::kScenes[k];
    EXPECT_LE(pr::scene_max_error(s), s.tolerance) << s.name;
  }
}

TEST(PatternOracle, EachPlantedDefectFailsTheVectors) {
  for (int p = pr::kSeparateShapes; p < pr::kPlantCount; p++) {
    pr::Plant plant = (pr::Plant)p;
    bool caught_by_scene = false;
    {
      pr::PlantScope scope(plant);
      for (int k = 0; k < pv::kSceneCount && !caught_by_scene; k++) {
        caught_by_scene = pr::scene_max_error(pv::kScenes[k]) > pv::kScenes[k].tolerance;
      }
    }
    bool caught_by_position = positions_catch(plant);
    if (plant == pr::kLeftPlusW) {
      // An abutting edge formed as left + w is one ulp off and does not reach a pixel: only the shared edge shows it.
      EXPECT_TRUE(caught_by_position) << pr::plant_name(plant);
    } else if (plant == pr::kAddStep) {
      EXPECT_TRUE(caught_by_scene) << "add-step by scene";
      EXPECT_TRUE(caught_by_position) << "add-step by position";
    } else {
      EXPECT_TRUE(caught_by_scene) << pr::plant_name(plant);
    }
  }
  // The control: with no plant the same comparisons pass.
  EXPECT_FALSE(positions_catch(pr::kNoPlant));
}

TEST(PatternOracle, PlantedSceneFailuresAreNotNoise) {
  // A plant is caught by more than the tolerance: by at least 1e-3 on some pixel for the rules that change what a pixel shows
  // (the lattice-position plants move a pixel by far less, and are checked above).
  for (pr::Plant plant : {pr::kSeparateShapes, pr::kCopy0Source, pr::kGroupPerCopy, pr::kReverseLatticeOrder, pr::kTileNotIsolated,
           pr::kKeepsTileColors, pr::kContentNotClipped, pr::kForegroundFromCreator}) {
    pr::PlantScope scope(plant);
    double worst = 0.0;
    for (int k = 0; k < pv::kSceneCount; k++) {
      double e = pr::scene_max_error(pv::kScenes[k]);
      worst = e > worst ? e : worst;
    }
    EXPECT_GT(worst, 1e-3) << pr::plant_name(plant);
  }
}

TEST(PatternOracle, LibraryCellMatchesTheExactPositions) {
  bool saw_million = false;
  for (int k = 0; k < pv::kPositionCount; k++) {
    const pv::VPosition & c = pv::kPositions[k];
    GSHSCENE_Pattern p = geometry(c.cell, c.step);
    double out[4] = {0.0, 0.0, 0.0, 0.0};
    ASSERT_EQ(gshscene_pattern_cell(&p, c.i, c.j, out), GSHSCENE_OK) << c.name;
    for (int q = 0; q < 4; q++) {
      EXPECT_LE(ulp_distance(out[q], c.edges[q]), 1u) << c.name << " (" << c.i << ", " << c.j << ") edge " << q;
    }
    if (c.i == 1000000 && c.cell[2] == 0x1.999999999999ap-4 && c.step[0] == c.cell[2]) {
      saw_million = true;
    }
    // The evaluator forms the same positions in the same way.
    double mine[4];
    pr::cell(c.cell, c.step, c.i, c.j, mine);
    for (int q = 0; q < 4; q++) {
      EXPECT_EQ(mine[q], out[q]) << c.name << " (" << c.i << ", " << c.j << ") edge " << q;
    }
  }
  EXPECT_TRUE(saw_million) << "the vectors hold copy index 10^6 at step 0.1";
}

TEST(PatternOracle, AbuttingCellsShareTheirEdgeToTheBit) {
  int checked = 0;
  for (int k = 0; k < pv::kPositionCount; k++) {
    const pv::VPosition & c = pv::kPositions[k];
    GSHSCENE_Pattern p = geometry(c.cell, c.step);
    double here[4], right[4], below[4];
    ASSERT_EQ(gshscene_pattern_cell(&p, c.i, c.j, here), GSHSCENE_OK);
    ASSERT_EQ(gshscene_pattern_cell(&p, c.i + 1, c.j, right), GSHSCENE_OK);
    ASSERT_EQ(gshscene_pattern_cell(&p, c.i, c.j + 1, below), GSHSCENE_OK);
    if (abutting_x(c)) {
      EXPECT_EQ(here[2], right[0]) << c.name << " index " << c.i;
      checked++;
    }
    if (abutting_y(c)) {
      EXPECT_EQ(here[3], below[1]) << c.name << " index " << c.j;
      checked++;
    }
  }
  EXPECT_GT(checked, 100);
}

TEST(PatternOracle, SharedEdgesForTenthsThirdsAndTheirKind) {
  // w = x_step = 0.1, 0.3 and 1/3: for every index of the vectors the right edge of cell i is the left edge of cell i + 1.
  const double widths[3] = {0.1, 0.3, 1.0 / 3.0};
  for (double w : widths) {
    const double cell[4] = {0.0, 0.0, w, w};
    const double step[2] = {w, w};
    GSHSCENE_Pattern p = geometry(cell, step);
    int64_t witness = -1;
    for (int64_t i = 0; i < 20000; i++) {
      double a[4], b[4];
      ASSERT_EQ(gshscene_pattern_cell(&p, i, i, a), GSHSCENE_OK);
      ASSERT_EQ(gshscene_pattern_cell(&p, i + 1, i + 1, b), GSHSCENE_OK);
      ASSERT_EQ(a[2], b[0]) << "w = " << w << " i = " << i;
      ASSERT_EQ(a[3], b[1]) << "w = " << w << " i = " << i;
      // The planted form, left + w, is a different double for some index.
      if (witness < 0 && a[0] + w != b[0]) {
        witness = i;
      }
    }
    EXPECT_GE(witness, 0) << "left + w differs from the next cell's left edge for w = " << w;
  }
}

TEST(PatternOracle, AccumulatingTheStepIsOffByFarMoreThanAnUlp) {
  int checked = 0;
  for (int k = 0; k < pv::kPositionCount; k++) {
    const pv::VPosition & c = pv::kPositions[k];
    if (c.i < 999999 || !abutting_x(c) || c.cell[0] != 0.0) {
      continue;
    }
    // The step added i times, the way a sink that walks the lattice would form the position.
    double acc = c.cell[0];
    for (int64_t n = 0; n < c.i; n++) {
      acc += c.step[0];
    }
    GSHSCENE_Pattern p = geometry(c.cell, c.step);
    double out[4];
    ASSERT_EQ(gshscene_pattern_cell(&p, c.i, c.j, out), GSHSCENE_OK);
    EXPECT_LE(ulp_distance(out[0], c.edges[0]), 1u) << c.name;
    EXPECT_GT(ulp_distance(acc, c.edges[0]), 1000u) << c.name << ": the accumulated position is " << acc << ", the exact one " << c.edges[0];
    checked++;
  }
  EXPECT_GE(checked, 3);  // 0.1, 0.3 and 1/3 at index 10^6 (10^6 - 1 is below the range of the test only when the step is exact)
}

TEST(PatternOracle, OverlapPredicate) {
  for (int k = 0; k < pv::kOverlapCount; k++) {
    const pv::VOverlap & o = pv::kOverlaps[k];
    const double cell[4] = {0.0, 0.0, o.w, o.h};
    const double step[2] = {o.x_step, o.y_step};
    GSHSCENE_Pattern p = geometry(cell, step, o.repeat_x, o.repeat_y);
    EXPECT_EQ(gshscene_pattern_overlaps(&p), o.overlaps) << "row " << k;
    EXPECT_EQ(pr::overlaps(o.w, o.h, o.x_step, o.y_step, o.repeat_x, o.repeat_y), o.overlaps) << "row " << k;
  }
  EXPECT_EQ(gshscene_pattern_overlaps(nullptr), 0);
}

TEST(PatternOracle, NoticeDecisionOfTheEvaluator) {
  int seen[3] = {0, 0, 0};
  for (int k = 0; k < pv::kNoticeCount; k++) {
    const pv::VNotice & n = pv::kNotices[k];
    EXPECT_EQ(pr::notice(n.maps, n.map_count, n.w, n.h, n.x_step, n.y_step, n.repeat_x, n.repeat_y), n.reason) << n.name;
    seen[n.reason == 0 ? 0 : (n.reason == 23 ? 1 : 2)]++;
  }
  // Every reason, and the case of two invertible factors whose determinants underflow, is in the vectors.
  EXPECT_GT(seen[0], 0);
  EXPECT_GT(seen[1], 0);
  EXPECT_GT(seen[2], 0);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
