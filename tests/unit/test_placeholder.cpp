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
 * The placeholder (list.h, PH1 and PH2; version.h, PH7; color-paint story P5): the record calls, the hash and dump the
 * independent generator states, the float-free call, the retained node with its emission, edit and diff.  What a sink
 * does with one (PH3 to PH6) is the conformance suite's.
 */

#include "test_helpers.h"

#include "../glyph_test_provider.h"
#include "../scene_fixtures.h"
#include "../known_answers.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <set>

using namespace scene;

namespace {

const double kNaN = std::numeric_limits<double>::quiet_NaN();
const double kInf = std::numeric_limits<double>::infinity();
const uint16_t COLR = GSHSCENE_PLACEHOLDER_SOURCE_COLR;

GSHGEOM_Map design_map() {
  GSHGEOM_Map m;
  gshgeom_map_design_to_model(2.5, 10, 20, &m);
  return m;
}

struct Frame {
  GSHSCENE_Test_Provider tp{};
  StoreHolder s;
  Ref face;
  Ref run;
  Frame() {
    tp.upem = 2048;
    face = Ref(gshscene_tp_face(s.store, &tp, 1, 0x77, 0));
    run = Ref(intern_font_run(s.store, face.get(), 20.48, {GSHSCENE_Glyph{3, 10.0, 20.0, nullptr}}));
  }
};

}  // namespace

/* --- the record call (PH1, PH2) ----------------------------------------------------------------- */

TEST(Placeholder, RecordsTheGeneratorsListAndItsHashAndDump) {
  StoreHolder s;
  Ref sq(intern_square(s.store));
  ListHolder l(s.store);
  GSHSCENE_Paint fill_paint = solid(0.25, 0.5, 0.75, 1.0);
  GSHSCENE_Paint fg{};
  fg.kind = GSHSCENE_PAINT_FOREGROUND;
  fg.rgba[3] = 1.0;
  GSHGEOM_Map tr = translate(1.5, -2.25);
  GSHGEOM_Map design = design_map();
  const double b1[4] = {-1.5, 0.0, 4.25, 6.0};
  const double b2[4] = {0.0, 0.0, 2.0, 2.0};
  const double zero[4] = {0.0, 0.0, 0.0, 0.0};
  ASSERT_EQ(gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &fill_paint, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_placeholder(l.list, COLR, 1, 0, b1, nullptr, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_placeholder(l.list, COLR, 3, 1, b2, &tr, 2, 1.5), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &fg, 2, 1.5), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_placeholder(l.list, COLR, 2, 0, nullptr, &design, -1, -2.5), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_placeholder(l.list, 65535, 65535, 1, zero, nullptr, 0, 0.0), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_hash(l.list), known::kPlaceholderListHash);
  EXPECT_EQ(dump_list(l.list), std::string(known::kPlaceholderListDump));
  ASSERT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_hash(l.list), known::kPlaceholderListHash) << "flat: closing reorders nothing";
  const GSHSCENE_Item * item = nullptr;
  ASSERT_EQ(gshscene_list_item(l.list, 2, &item), GSHSCENE_OK);
  EXPECT_EQ(item->kind, GSHSCENE_ITEM_PLACEHOLDER);
  EXPECT_EQ(item->resource, nullptr) << "a placeholder holds no resource";
  EXPECT_EQ(item->style, nullptr);
  EXPECT_EQ(item->placeholder.source, COLR);
  EXPECT_EQ(item->placeholder.code, 3u);
  EXPECT_EQ(item->placeholder.approximated, 1u);
  EXPECT_EQ(item->placeholder.has_box, 1u);
  EXPECT_EQ(item->placeholder.box[2], 2.0);
  EXPECT_EQ(item->layer, 2);
  EXPECT_EQ(item->depth, 1.5);
  EXPECT_EQ(item->fill_rule, GSHGEOM_FILL_NONZERO);
  EXPECT_EQ(item->paint.kind, GSHSCENE_PAINT_SOLID);
  GSHSCENE_Group zero_group;
  std::memset(&zero_group, 0, sizeof zero_group);
  EXPECT_EQ(std::memcmp(&item->group, &zero_group, sizeof zero_group), 0);
  ASSERT_EQ(gshscene_list_item(l.list, 4, &item), GSHSCENE_OK);
  EXPECT_EQ(item->placeholder.has_box, 0u);
  for (double v : item->placeholder.box) {
    EXPECT_EQ(v, 0.0) << "no box is a zero box: one spelling";
  }
  ASSERT_EQ(gshscene_list_item(l.list, 0, &item), GSHSCENE_OK);
  GSHSCENE_Placeholder none;
  std::memset(&none, 0, sizeof none);
  EXPECT_EQ(std::memcmp(&item->placeholder, &none, sizeof none), 0) << "a fill's placeholder fields are zero";
}

TEST(Placeholder, EveryInvalidArgumentIsRefusedAndLeavesTheListAsItWas) {
  StoreHolder s;
  ListHolder l(s.store);
  const double box[4] = {0.0, 0.0, 1.0, 1.0};
  ASSERT_EQ(gshscene_list_placeholder(l.list, COLR, 1, 0, box, nullptr, 0, 0.0), GSHSCENE_OK);
  const uint64_t before = gshscene_list_hash(l.list);
  auto bad = [&](GSHSCENE_Result want, uint16_t src, uint16_t code, uint32_t approx, const double * b,
                 const GSHGEOM_Map * map, double depth) {
    EXPECT_EQ(gshscene_list_placeholder(l.list, src, code, approx, b, map, 0, depth), want);
    EXPECT_EQ(gshscene_list_count(l.list), 1u);
    EXPECT_EQ(gshscene_list_hash(l.list), before);
  };
  bad(GSHSCENE_ERR_INVALID, 0, 1, 0, box, nullptr, 0.0);
  bad(GSHSCENE_ERR_INVALID, COLR, 0, 0, box, nullptr, 0.0);
  bad(GSHSCENE_ERR_INVALID, COLR, 1, 2, box, nullptr, 0.0);
  bad(GSHSCENE_ERR_INVALID, COLR, 1, 0xffffffffu, box, nullptr, 0.0);
  const double inverted_x[4] = {2.0, 0.0, 1.0, 1.0};
  const double inverted_y[4] = {0.0, 2.0, 1.0, 1.0};
  const double nan_box[4] = {0.0, kNaN, 1.0, 1.0};
  const double inf_box[4] = {0.0, 0.0, kInf, 1.0};
  const double ninf_box[4] = {-kInf, 0.0, 1.0, 1.0};
  bad(GSHSCENE_ERR_INVALID, COLR, 1, 0, inverted_x, nullptr, 0.0);
  bad(GSHSCENE_ERR_INVALID, COLR, 1, 0, inverted_y, nullptr, 0.0);
  bad(GSHSCENE_ERR_INVALID, COLR, 1, 0, nan_box, nullptr, 0.0);
  bad(GSHSCENE_ERR_INVALID, COLR, 1, 0, inf_box, nullptr, 0.0);
  bad(GSHSCENE_ERR_INVALID, COLR, 1, 0, ninf_box, nullptr, 0.0);
  bad(GSHSCENE_ERR_INVALID, COLR, 1, 0, box, nullptr, kNaN);
  bad(GSHSCENE_ERR_INVALID, COLR, 1, 0, box, nullptr, kInf);
  // translate() refuses a NaN and leaves its output unwritten, so the map is made finite and then damaged.
  GSHGEOM_Map nan_map = translate(0.0, 0.0);
  nan_map.t[0] = kNaN;
  bad(GSHSCENE_ERR_INVALID, COLR, 1, 0, box, &nan_map, 0.0);
  GSHGEOM_Map map3;
  ASSERT_EQ(gshgeom_map_identity(3, &map3), GSHGEOM_OK);
  bad(GSHSCENE_ERR_UNSUPPORTED, COLR, 1, 0, box, &map3, 0.0);
  // A point box and a degenerate one are boxes: nothing says a box has area.
  const double point[4] = {3.0, 3.0, 3.0, 3.0};
  EXPECT_EQ(gshscene_list_placeholder(l.list, COLR, 1, 0, point, nullptr, 0, 0.0), GSHSCENE_OK);
  // A source no front end registered is a number the application may use: the registry names, it does not gate.
  EXPECT_EQ(gshscene_list_placeholder(l.list, 9, 77, 0, nullptr, nullptr, 0, 0.0), GSHSCENE_OK);
  // A NULL and a closed list.
  EXPECT_EQ(gshscene_list_placeholder(nullptr, COLR, 1, 0, box, nullptr, 0, 0.0), GSHSCENE_ERR_INVALID);
  ASSERT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_placeholder(l.list, COLR, 1, 0, box, nullptr, 0, 0.0), GSHSCENE_ERR_INVALID);
}

TEST(Placeholder, ANegativeZeroIsStoredAsPositiveZero) {
  StoreHolder s;
  ListHolder a(s.store);
  ListHolder b(s.store);
  const double negative[4] = {-0.0, -0.0, 1.0, 1.0};
  const double positive[4] = {0.0, 0.0, 1.0, 1.0};
  ASSERT_EQ(gshscene_list_placeholder(a.list, COLR, 1, 0, negative, nullptr, 0, -0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_placeholder(b.list, COLR, 1, 0, positive, nullptr, 0, 0.0), GSHSCENE_OK);
  EXPECT_TRUE(gshscene_list_equal(a.list, b.list));
  EXPECT_EQ(gshscene_list_hash(a.list), gshscene_list_hash(b.list));
  const GSHSCENE_Item * item = nullptr;
  ASSERT_EQ(gshscene_list_item(a.list, 0, &item), GSHSCENE_OK);
  EXPECT_FALSE(std::signbit(item->placeholder.box[0]));
  EXPECT_FALSE(std::signbit(item->depth));
}

TEST(Placeholder, TheLimitsApplyAndAFailureTakesNothing) {
  StoreHolder s;
  GSHSCENE_Limits limits{};
  limits.max_list_items = 2;
  GSHSCENE_List * list = nullptr;
  ASSERT_EQ(gshscene_list_create(nullptr, &limits, s.store, &list), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_placeholder(list, COLR, 1, 0, nullptr, nullptr, 0, 0.0), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_placeholder(list, COLR, 2, 0, nullptr, nullptr, 0, 0.0), GSHSCENE_OK);
  const uint64_t two = gshscene_list_hash(list);
  EXPECT_EQ(gshscene_list_placeholder(list, COLR, 3, 0, nullptr, nullptr, 0, 0.0), GSHSCENE_ERR_LIMIT);
  EXPECT_EQ(gshscene_list_count(list), 2u);
  EXPECT_EQ(gshscene_list_hash(list), two);
  gshscene_list_destroy(list);
}

TEST(Placeholder, EqualityAndTheHashSeeEveryField) {
  StoreHolder s;
  const double box[4] = {0.0, 0.0, 1.0, 2.0};
  const double wider[4] = {0.0, 0.0, 1.0, 2.5};
  GSHGEOM_Map tr = translate(1.0, 1.0);
  struct Spec {
    uint16_t source, code;
    uint32_t approx;
    const double * box;
    const GSHGEOM_Map * map;
    int32_t layer;
    double depth;
  };
  const Spec base{COLR, 1, 0, box, nullptr, 0, 0.0};
  const std::vector<Spec> variants = {{2, 1, 0, box, nullptr, 0, 0.0}, {COLR, 2, 0, box, nullptr, 0, 0.0},
      {COLR, 1, 1, box, nullptr, 0, 0.0}, {COLR, 1, 0, wider, nullptr, 0, 0.0}, {COLR, 1, 0, nullptr, nullptr, 0, 0.0},
      {COLR, 1, 0, box, &tr, 0, 0.0}, {COLR, 1, 0, box, nullptr, 1, 0.0}, {COLR, 1, 0, box, nullptr, 0, 1.0}};
  auto make = [&](const Spec & x, ListHolder & l) {
    ASSERT_EQ(gshscene_list_placeholder(l.list, x.source, x.code, x.approx, x.box, x.map, x.layer, x.depth), GSHSCENE_OK);
  };
  ListHolder a(s.store);
  make(base, a);
  ListHolder again(s.store);
  make(base, again);
  EXPECT_TRUE(gshscene_list_equal(a.list, again.list));
  EXPECT_EQ(gshscene_list_hash(a.list), gshscene_list_hash(again.list));
  for (size_t i = 0; i < variants.size(); i++) {
    ListHolder v(s.store);
    make(variants[i], v);
    EXPECT_FALSE(gshscene_list_equal(a.list, v.list)) << i;
    EXPECT_NE(gshscene_list_hash(a.list), gshscene_list_hash(v.list)) << i;
  }
}

TEST(Placeholder, AFillAFixedFillAndAPlaceholderAreOneUnitEachOfTheirSequence) {
  // N11: in a depth group and an overlay group a placeholder moves as one unit (PH1).
  StoreHolder s;
  Ref sq(intern_square(s.store));
  GSHSCENE_Paint black = solid(0, 0, 0, 1);
  for (uint32_t ordering : {GSHSCENE_ORDER_DEPTH, GSHSCENE_ORDER_OVERLAY}) {
    ListHolder l(s.store);
    GSHSCENE_Group g = make_group(1.0, ordering);
    ASSERT_EQ(gshscene_list_group_push(l.list, &g, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_placeholder(l.list, COLR, 1, 0, nullptr, nullptr, 3, 1.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &black, 1, 2.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_placeholder(l.list, COLR, 2, 1, nullptr, nullptr, 2, 2.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_group_pop(l.list), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
    std::vector<int32_t> layers;
    for (size_t i = 1; i + 1 < gshscene_list_count(l.list); i++) {
      const GSHSCENE_Item * item = nullptr;
      ASSERT_EQ(gshscene_list_item(l.list, i, &item), GSHSCENE_OK);
      layers.push_back(item->layer);
    }
    // Depth: far first (2.0 tied: by layer 1 then 2), then 1.0.  Overlay: by layer.
    EXPECT_EQ(layers, (std::vector<int32_t>{1, 2, 3})) << ordering;
    EXPECT_EQ(gshscene_list_tie_count(l.list), ordering == GSHSCENE_ORDER_DEPTH ? 1u : 0u);
  }
}

TEST(Placeholder, TruncateDropsItAndTheListIsAsIfItWasNeverRecorded) {
  StoreHolder s;
  ListHolder l(s.store);
  ASSERT_EQ(gshscene_list_placeholder(l.list, COLR, 1, 0, nullptr, nullptr, 0, 0.0), GSHSCENE_OK);
  const uint64_t one = gshscene_list_hash(l.list);
  ASSERT_EQ(gshscene_list_placeholder(l.list, COLR, 2, 1, nullptr, nullptr, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_truncate(l.list, 1), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_hash(l.list), one);
}

/* --- the float-free call ---------------------------------------------------------------------------- */

TEST(PlaceholderFixed, TheBoxIsPlacedWhereTheRunsGlyphWouldBe) {
  Frame f;
  ASSERT_NE(f.run.get(), nullptr);
  ListHolder l(f.s.store);
  // Box (-100, 50) to (900, 1950) in design units given with 2 fraction bits (x4): nothing rounds.
  const int32_t box[4] = {-400, 200, 3600, 7800};
  ASSERT_EQ(gshscene_list_placeholder_fixed(l.list, f.run.get(), nullptr, 0, box, 2, COLR, 1, 0, 6), GSHSCENE_OK);
  const GSHSCENE_Item * item = nullptr;
  ASSERT_EQ(gshscene_list_item(l.list, 0, &item), GSHSCENE_OK);
  EXPECT_EQ(item->kind, GSHSCENE_ITEM_PLACEHOLDER);
  EXPECT_EQ(item->layer, 6);
  EXPECT_EQ(item->depth, 0.0);
  EXPECT_EQ(item->placeholder.has_box, 1u);
  EXPECT_EQ(item->placeholder.box[0], -100.0);
  EXPECT_EQ(item->placeholder.box[1], 50.0);
  EXPECT_EQ(item->placeholder.box[2], 900.0);
  EXPECT_EQ(item->placeholder.box[3], 1950.0);
  // The item's map is the clip's: a design corner lands where the glyph outline puts it (g + s (x, -y), s = 0.01).
  GSHGEOM_Point q{};
  ASSERT_EQ(gshgeom_map_apply_point(&item->map, GSHGEOM_Point{900, 1950, 0}, &q), GSHGEOM_OK);
  EXPECT_EQ(q.x, 10.0 + 9.0);
  EXPECT_EQ(q.y, 20.0 - 19.5);
  ListHolder clip(f.s.store);
  ASSERT_EQ(gshscene_list_clip_push_design_rect_fixed(clip.list, f.run.get(), nullptr, 0, -400, 200, 3600, 7800, 2, 6),
      GSHSCENE_OK);
  const GSHSCENE_Item * c = nullptr;
  ASSERT_EQ(gshscene_list_item(clip.list, 0, &c), GSHSCENE_OK);
  EXPECT_EQ(gshgeom_map_hash(&item->map), gshgeom_map_hash(&c->map)) << "the same placement as the design rectangle's";
  // Under ops the box moves as the glyph's design space does.
  GSHSCENE_Fixed_Op move{};
  move.kind = GSHSCENE_FIXED_TRANSLATE;
  move.v[0] = 100;
  const int32_t small[4] = {0, 0, 10, 10};
  ASSERT_EQ(gshscene_list_placeholder_fixed(l.list, f.run.get(), &move, 1, small, 0, COLR, 2, 1, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_item(l.list, 1, &item), GSHSCENE_OK);
  EXPECT_EQ(item->placeholder.approximated, 1u);
  ASSERT_EQ(gshgeom_map_apply_point(&item->map, GSHGEOM_Point{0, 0, 0}, &q), GSHGEOM_OK);
  EXPECT_EQ(q.x, 10.0 + 1.0);
  EXPECT_EQ(q.y, 20.0);
  // No box is a placeholder without one; the frame may be absent only without ops.
  ASSERT_EQ(gshscene_list_placeholder_fixed(l.list, f.run.get(), nullptr, 0, nullptr, 0, COLR, 3, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_item(l.list, 2, &item), GSHSCENE_OK);
  EXPECT_EQ(item->placeholder.has_box, 0u);
  ASSERT_EQ(gshscene_list_placeholder_fixed(l.list, nullptr, nullptr, 0, small, 0, COLR, 3, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_item(l.list, 3, &item), GSHSCENE_OK);
  GSHGEOM_Map identity;
  gshgeom_map_identity(2, &identity);
  EXPECT_EQ(gshgeom_map_hash(&item->map), gshgeom_map_hash(&identity));
  // Failures: an inverted box, bits past the maximum, a frame that is no font run, ops without a frame, a zero source
  // or code, a bad approximated flag; each leaves the list as it was.
  const size_t count = gshscene_list_count(l.list);
  const int32_t inverted[4] = {5, 0, 4, 9};
  Ref square(intern_square(f.s.store));
  EXPECT_EQ(gshscene_list_placeholder_fixed(l.list, f.run.get(), nullptr, 0, inverted, 0, COLR, 1, 0, 0), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_placeholder_fixed(l.list, f.run.get(), nullptr, 0, small, 31, COLR, 1, 0, 0), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_placeholder_fixed(l.list, f.run.get(), nullptr, 0, small, 40, COLR, 1, 0, 0), GSHSCENE_ERR_INVALID)
      << "bits far past the most";
  EXPECT_EQ(gshscene_list_placeholder_fixed(l.list, square.get(), nullptr, 0, small, 0, COLR, 1, 0, 0), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_placeholder_fixed(l.list, nullptr, &move, 1, small, 0, COLR, 1, 0, 0), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_placeholder_fixed(l.list, f.run.get(), nullptr, 0, small, 0, 0, 1, 0, 0), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_placeholder_fixed(l.list, f.run.get(), nullptr, 0, small, 0, COLR, 0, 0, 0), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_placeholder_fixed(l.list, f.run.get(), nullptr, 0, small, 0, COLR, 1, 2, 0), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_placeholder_fixed(nullptr, f.run.get(), nullptr, 0, small, 0, COLR, 1, 0, 0), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_count(l.list), count);
}

/* --- the retained node (PH7) -------------------------------------------------------------------------- */

TEST(PlaceholderNode, TheGeneratorsSceneHashesDumpsAndEmitsOnePlaceholderItemPerNode) {
  SceneHolder s;
  const uint64_t kDomain = UINT64_C(0x5356470000000001);
  GSHSCENE_Key key = K(kDomain, 9);
  const double b1[4] = {0.0, 0.0, 8.0, 8.0};
  const double b2[4] = {-1.0, -2.0, 3.0, 4.0};
  VersionRef v1;
  {
    EditHolder e(s);
    GSHSCENE_Node g = group_node();
    g.map = translate(1.5, -2.25);
    uint64_t group = add(e.edit, GSHSCENE_ROOT_ID, g);
    EXPECT_EQ(group, 2u);
    GSHSCENE_Node n3 = placeholder_node(COLR, 1, 0, b1, 1);
    n3.depth = 0.5;
    EXPECT_EQ(add(e.edit, group, n3), 3u);
    GSHSCENE_Node n4 = placeholder_node(COLR, 3, 1, b2);
    n4.map = design_map();
    uint64_t keyed = 0;
    ASSERT_EQ(gshscene_edit_upsert(e.edit, &key, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, &n4, &keyed), GSHSCENE_OK);
    EXPECT_EQ(keyed, 4u);
    GSHSCENE_Node n5 = placeholder_node(COLR, 2);
    n5.flags = GSHSCENE_NODE_HIDDEN;
    EXPECT_EQ(add(e.edit, GSHSCENE_ROOT_ID, n5), 5u);
    publish(s, e, &v1);
  }
  EXPECT_EQ(gshscene_version_hash(v1.get()), known::kPlaceholderSceneHash);
  EXPECT_EQ(dump_version(v1.get()), std::string(known::kPlaceholderSceneDump));
  GSHSCENE_Node_Info info = info_of(v1.get(), 4);
  EXPECT_EQ(info.node.kind, GSHSCENE_NODE_PLACEHOLDER);
  EXPECT_EQ(info.node.placeholder.source, COLR);
  EXPECT_EQ(info.node.placeholder.code, 3u);
  EXPECT_EQ(info.node.placeholder.approximated, 1u);
  EXPECT_EQ(info.node.placeholder.box[3], 4.0);
  EXPECT_EQ(info.node.resource, nullptr);
  EXPECT_EQ(info.child_count, 0u);
  // The emitted list: the group's composed map is on the first item, a hidden node is not emitted, and the list equals
  // the recorded list that says the same.
  const GSHSCENE_List * emitted = nullptr;
  ASSERT_EQ(gshscene_version_list(v1.get(), &emitted), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_hash(emitted), known::kPlaceholderSceneListHash);
  ListHolder recorded(s.store());
  GSHGEOM_Map tr = translate(1.5, -2.25);
  GSHGEOM_Map design = design_map();
  ASSERT_EQ(gshscene_list_placeholder(recorded.list, COLR, 1, 0, b1, &tr, 1, 0.5), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_placeholder(recorded.list, COLR, 3, 1, b2, &design, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(recorded.list), GSHSCENE_OK);
  EXPECT_TRUE(gshscene_list_equal(emitted, recorded.list));
  for (size_t i = 0; i < 2; i++) {
    uint64_t node = 0;
    ASSERT_EQ(gshscene_version_item_node(v1.get(), 0, i, &node), GSHSCENE_OK);
    EXPECT_EQ(node, 3u + i) << "item " << i << " names its node";
  }
  int seen = 0;
  for (const Occurrence & o : occurrences(v1.get())) {
    seen += o.kind == GSHSCENE_NODE_PLACEHOLDER ? 1 : 0;
  }
  EXPECT_EQ(seen, 2) << "the hidden one is not visited in place";
  // An identical re-lowering publishes nothing.
  VersionRef v2;
  {
    EditHolder e(s);
    GSHSCENE_Node n = info.node;
    uint64_t again = 0;
    ASSERT_EQ(gshscene_edit_upsert(e.edit, &key, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, &n, &again), GSHSCENE_OK);
    EXPECT_EQ(again, 4u);
    publish(s, e, &v2);
  }
  EXPECT_EQ(v2.get(), v1.get());
}

TEST(PlaceholderNode, EveryFieldIsValidatedWhereTheNodeEnters) {
  SceneHolder s;
  EditHolder e(s);
  const double box[4] = {0.0, 0.0, 1.0, 1.0};
  auto add_bad = [&](GSHSCENE_Node n, GSHSCENE_Result want) {
    EXPECT_EQ(gshscene_edit_add(e.edit, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, &n, nullptr, nullptr), want);
  };
  add_bad(placeholder_node(COLR, 1, 0, box), GSHSCENE_OK);
  add_bad(placeholder_node(COLR, 1), GSHSCENE_OK);
  add_bad(placeholder_node(0, 1, 0, box), GSHSCENE_ERR_INVALID);
  add_bad(placeholder_node(COLR, 0, 0, box), GSHSCENE_ERR_INVALID);
  add_bad(placeholder_node(COLR, 1, 2, box), GSHSCENE_ERR_INVALID);
  const double inverted[4] = {1.0, 0.0, 0.0, 1.0};
  add_bad(placeholder_node(COLR, 1, 0, inverted), GSHSCENE_ERR_INVALID);
  const double nan_box[4] = {0.0, 0.0, kNaN, 1.0};
  add_bad(placeholder_node(COLR, 1, 0, nan_box), GSHSCENE_ERR_INVALID);
  GSHSCENE_Node noncanonical = placeholder_node(COLR, 1);
  noncanonical.placeholder.box[0] = 1.0;  // a box that is not there, nonzero: two spellings of none
  add_bad(noncanonical, GSHSCENE_ERR_INVALID);
  GSHSCENE_Node map3 = placeholder_node(COLR, 1);
  gshgeom_map_identity(3, &map3.map);
  add_bad(map3, GSHSCENE_ERR_UNSUPPORTED);
  GSHSCENE_Node deep = placeholder_node(COLR, 1);
  deep.depth = kNaN;
  add_bad(deep, GSHSCENE_ERR_INVALID);
  // A placeholder is a leaf: it takes no children.
  uint64_t id = 0;
  GSHSCENE_Node leaf = placeholder_node(COLR, 1);
  ASSERT_EQ(gshscene_edit_add(e.edit, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, &leaf, nullptr, &id), GSHSCENE_OK);
  GSHSCENE_Node child = placeholder_node(COLR, 2);
  EXPECT_NE(gshscene_edit_add(e.edit, id, GSHSCENE_APPEND, &child, nullptr, nullptr), GSHSCENE_OK);
}

TEST(PlaceholderNode, AnEditSetsAndClearsEachFieldAndTheDiffNamesTheNodeAlone) {
  SceneHolder s;
  const double box[4] = {0.0, 0.0, 1.0, 1.0};
  const double other[4] = {0.0, 0.0, 1.0, 3.0};
  VersionRef v1;
  uint64_t id = 0;
  {
    EditHolder e(s);
    id = add(e.edit, GSHSCENE_ROOT_ID, placeholder_node(COLR, 1, 0, box));
    add(e.edit, GSHSCENE_ROOT_ID, placeholder_node(COLR, 2));
    publish(s, e, &v1);
  }
  GSHSCENE_Node_Info base = info_of(v1.get(), id);
  struct Change {
    const char * name;
    GSHSCENE_Node (*apply)(GSHSCENE_Node, const double *);
  };
  const Change changes[] = {
      {"source", [](GSHSCENE_Node n, const double *) { n.placeholder.source = 2; return n; }},
      {"code", [](GSHSCENE_Node n, const double *) { n.placeholder.code = 2; return n; }},
      {"approximated", [](GSHSCENE_Node n, const double *) { n.placeholder.approximated = 1; return n; }},
      {"box", [](GSHSCENE_Node n, const double * b) { for (int i = 0; i < 4; i++) { n.placeholder.box[i] = b[i]; } return n; }},
      {"box cleared", [](GSHSCENE_Node n, const double *) {
         n.placeholder.has_box = 0;
         for (double & v : n.placeholder.box) { v = 0.0; }
         return n; }},
      {"map", [](GSHSCENE_Node n, const double *) { n.map = translate(1.0, 0.0); return n; }},
      {"layer", [](GSHSCENE_Node n, const double *) { n.layer = 4; return n; }},
      {"depth", [](GSHSCENE_Node n, const double *) { n.depth = 2.0; return n; }},
      {"hidden", [](GSHSCENE_Node n, const double *) { n.flags = GSHSCENE_NODE_HIDDEN; return n; }},
  };
  std::set<uint64_t> hashes{gshscene_version_hash(v1.get())};
  VersionRef current;
  current.reset(gshscene_version_retain(v1.get()));
  for (const Change & c : changes) {
    VersionRef changed;
    {
      EditHolder e(s, current.get());
      GSHSCENE_Node n = c.apply(base.node, other);
      ASSERT_EQ(gshscene_edit_set(e.edit, id, &n), GSHSCENE_OK) << c.name;
      publish(s, e, &changed);
    }
    ASSERT_NE(changed.get(), current.get()) << c.name;
    EXPECT_TRUE(hashes.insert(gshscene_version_hash(changed.get())).second) << c.name << ": the hash covers it";
    GSHSCENE_Diff * diff = nullptr;
    ASSERT_EQ(gshscene_diff(current.get(), changed.get(), nullptr, nullptr, &diff), GSHSCENE_OK) << c.name;
    ASSERT_EQ(gshscene_diff_count(diff), 1u) << c.name;
    uint64_t changed_id = 0;
    GSHSCENE_Diff_Kind kind;
    ASSERT_EQ(gshscene_diff_get(diff, 0, &changed_id, &kind), GSHSCENE_OK);
    EXPECT_EQ(changed_id, id) << c.name;
    EXPECT_EQ(kind, GSHSCENE_DIFF_CHANGED) << c.name;
    // It draws nothing, so nothing of the target changes: the damage is empty (PH7).
    GSHSCENE_Damage damage;
    ASSERT_EQ(gshscene_diff_damage(diff, &damage), GSHSCENE_OK);
    EXPECT_TRUE(damage.empty) << c.name;
    EXPECT_FALSE(damage.unbounded) << c.name;
    gshscene_diff_destroy(diff);
    // Back to the base, so that each field is changed alone: the same content, the same hash.
    VersionRef restored;
    {
      EditHolder e(s, changed.get());
      ASSERT_EQ(gshscene_edit_set(e.edit, id, &base.node), GSHSCENE_OK) << c.name;
      publish(s, e, &restored);
    }
    EXPECT_EQ(gshscene_version_hash(restored.get()), gshscene_version_hash(v1.get())) << c.name;
    current = std::move(restored);
  }
}

TEST(PlaceholderNode, AGroupsClipAndOpacityApplyToNothingButTheEntrysChainIsTheEmissions) {
  SceneHolder s;
  Ref sq(intern_square(s.store()));
  Ref clip(intern_square(s.store(), 1.0));
  VersionRef v;
  {
    EditHolder e(s);
    GSHSCENE_Node g = group_node(0.5);
    g.clip = clip.get();
    uint64_t group = add(e.edit, GSHSCENE_ROOT_ID, g);
    add(e.edit, group, placeholder_node(COLR, 1));
    add(e.edit, GSHSCENE_ROOT_ID, shape_node(sq.get()));
    publish(s, e, &v);
  }
  const GSHSCENE_List * list = nullptr;
  ASSERT_EQ(gshscene_version_list(v.get(), &list), GSHSCENE_OK);
  // clip push, group push, the placeholder, group pop, clip pop, the fill: the placeholder is item 2.
  ASSERT_EQ(gshscene_list_count(list), 6u);
  const GSHSCENE_Item * item = nullptr;
  ASSERT_EQ(gshscene_list_item(list, 2, &item), GSHSCENE_OK);
  EXPECT_EQ(item->kind, GSHSCENE_ITEM_PLACEHOLDER);
  uint64_t node = 0;
  ASSERT_EQ(gshscene_version_item_node(v.get(), 0, 2, &node), GSHSCENE_OK);
  EXPECT_EQ(node, 3u);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
