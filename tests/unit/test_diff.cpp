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
 * The diff: the changed IDs, the damage for a map, its conservative cases, and
 * an oracle that shares none of its structure (diff.h; story 11).
 */

#include "scene_fixtures.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

using namespace scene;

namespace {

using Ids = std::vector<uint64_t>;

GSHGEOM_Map scale_map(double s) {
  GSHGEOM_Map m;
  gshgeom_map_scale(2, s, s, 1.0, &m);
  return m;
}

struct Result {
  std::vector<std::pair<uint64_t, int>> changes;
  GSHSCENE_Damage damage;
};

Result diff_of(const GSHSCENE_Version * a, const GSHSCENE_Version * b, const GSHGEOM_Map * map = nullptr,
    const GSHSCENE_Damage_Options * options = nullptr) {
  Result r;
  std::memset(&r.damage, 0xAB, sizeof r.damage);
  GSHSCENE_Diff * d = nullptr;
  EXPECT_EQ(gshscene_diff(a, b, map, options, &d), GSHSCENE_OK);
  if (!d) {
    return r;
  }
  for (size_t i = 0; i < gshscene_diff_count(d); i++) {
    uint64_t id;
    GSHSCENE_Diff_Kind kind;
    EXPECT_EQ(gshscene_diff_get(d, i, &id, &kind), GSHSCENE_OK);
    r.changes.push_back({id, (int)kind});
  }
  EXPECT_EQ(gshscene_diff_damage(d, &r.damage), GSHSCENE_OK);
  gshscene_diff_destroy(d);
  return r;
}

void expect_rect(const GSHSCENE_Damage & d, int64_t x0, int64_t y0, int64_t x1, int64_t y1, const char * what = "") {
  EXPECT_EQ(d.unbounded, 0) << what;
  EXPECT_EQ(d.empty, 0) << what;
  EXPECT_EQ(d.rect.x0, x0) << what;
  EXPECT_EQ(d.rect.y0, y0) << what;
  EXPECT_EQ(d.rect.x1, x1) << what;
  EXPECT_EQ(d.rect.y1, y1) << what;
}

/** A scene with a few squares (4 x 4 at their own origin), at known places. */
struct Doc {
  SceneHolder s;
  Ref sq;
  explicit Doc() : sq(intern_square(s.store())) {}
  uint64_t shape(EditHolder & e, uint64_t parent, double x, double y, int32_t layer = 0) {
    GSHSCENE_Node n = shape_node(sq.get(), layer);
    n.map = translate(x, y);
    return add(e.edit, parent, n);
  }
  VersionRef publish_edit(EditHolder & e) {
    VersionRef v;
    publish(s, e, &v);
    return v;
  }
  VersionRef current() {
    VersionRef v;
    EXPECT_EQ(gshscene_scene_current(s.scene, &v.version), GSHSCENE_OK);
    return v;
  }
};

}  // namespace

// --- the changed ids ---------------------------------------------------------------------------

TEST(DiffIds, IdenticalVersionsDifferInNothing) {
  Doc d;
  EditHolder e(d.s);
  d.shape(e, GSHSCENE_ROOT_ID, 0, 0);
  VersionRef v = d.publish_edit(e);
  Result r = diff_of(v.get(), v.get());
  EXPECT_TRUE(r.changes.empty());
  EXPECT_EQ(r.damage.empty, 1);
  EXPECT_EQ(r.damage.unbounded, 0);
  EXPECT_EQ(r.damage.rect.x1, 0);
}

TEST(DiffIds, AddedRemovedAndChangedAreNamedAscending) {
  Doc d;
  uint64_t a, b, c;
  VersionRef v1;
  {
    EditHolder e(d.s);
    a = d.shape(e, GSHSCENE_ROOT_ID, 0, 0);
    b = d.shape(e, GSHSCENE_ROOT_ID, 10, 0);
    c = d.shape(e, GSHSCENE_ROOT_ID, 20, 0);
    v1 = d.publish_edit(e);
  }
  uint64_t added;
  VersionRef v2;
  {
    EditHolder e(d.s);
    ASSERT_EQ(gshscene_edit_remove(e.edit, a), GSHSCENE_OK);
    GSHSCENE_Node n = shape_node(d.sq.get(), 3);
    ASSERT_EQ(gshscene_edit_set(e.edit, c, &n), GSHSCENE_OK);
    added = d.shape(e, GSHSCENE_ROOT_ID, 30, 0);
    v2 = d.publish_edit(e);
  }
  Result r = diff_of(v1.get(), v2.get());
  std::vector<std::pair<uint64_t, int>> want = {{1, GSHSCENE_DIFF_CHANGED}, {a, GSHSCENE_DIFF_REMOVED},
      {c, GSHSCENE_DIFF_CHANGED}, {added, GSHSCENE_DIFF_ADDED}};
  EXPECT_EQ(r.changes, want);
  // The other direction reverses added and removed.
  Result back = diff_of(v2.get(), v1.get());
  std::vector<std::pair<uint64_t, int>> want_back = {{1, GSHSCENE_DIFF_CHANGED}, {a, GSHSCENE_DIFF_ADDED},
      {c, GSHSCENE_DIFF_CHANGED}, {added, GSHSCENE_DIFF_REMOVED}};
  EXPECT_EQ(back.changes, want_back);
  (void)b;
}

TEST(DiffIds, TwoIndependentVersionsWithEqualContentDifferInNothing) {
  // The same edit made twice in two scenes that share a store is the same content: no change, though the nodes are two.
  StoreHolder store;
  Ref sq(intern_square(store.store));
  SceneHolder s1(nullptr, nullptr, store.store);
  VersionRef base1;
  gshscene_scene_current(s1.scene, &base1.version);
  EditHolder e1(s1);
  add(e1.edit, GSHSCENE_ROOT_ID, shape_node(sq.get(), 2));
  VersionRef v1;
  publish(s1, e1, &v1);
  // A second edit of the same scene that sets what is there: nothing.
  EditHolder e2(s1);
  GSHSCENE_Node same = shape_node(sq.get(), 2);
  ASSERT_EQ(gshscene_edit_set(e2.edit, 2, &same), GSHSCENE_OK);
  VersionRef v2;
  publish(s1, e2, &v2);
  Result r = diff_of(v1.get(), v2.get());
  EXPECT_TRUE(r.changes.empty());
}

TEST(DiffIds, VersionsOfDifferentScenesAreRefusedAndArgumentsChecked) {
  Doc a, b;
  VersionRef va = a.current();
  VersionRef vb = b.current();
  GSHSCENE_Diff * out = nullptr;
  EXPECT_EQ(gshscene_diff(va.get(), vb.get(), nullptr, nullptr, &out), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(out, nullptr);
  EXPECT_EQ(gshscene_diff(nullptr, va.get(), nullptr, nullptr, &out), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_diff(va.get(), nullptr, nullptr, nullptr, &out), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_diff(va.get(), va.get(), nullptr, nullptr, nullptr), GSHSCENE_ERR_INVALID);
  GSHGEOM_Map bad = translate(1, 1);
  bad.m[0][0] = std::nan("");
  EXPECT_EQ(gshscene_diff(va.get(), va.get(), &bad, nullptr, &out), GSHSCENE_ERR_INVALID);
  GSHSCENE_Damage_Options o;
  o.aa_support = -0.5;
  EXPECT_EQ(gshscene_diff(va.get(), va.get(), nullptr, &o, &out), GSHSCENE_ERR_INVALID);
  o.aa_support = std::nan("");
  EXPECT_EQ(gshscene_diff(va.get(), va.get(), nullptr, &o, &out), GSHSCENE_ERR_INVALID);
  o.aa_support = INFINITY;
  EXPECT_EQ(gshscene_diff(va.get(), va.get(), nullptr, &o, &out), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_diff_count(nullptr), 0u);
  uint64_t id;
  GSHSCENE_Diff_Kind kind;
  EXPECT_EQ(gshscene_diff_get(nullptr, 0, &id, &kind), GSHSCENE_ERR_INVALID);
  GSHSCENE_Damage damage;
  EXPECT_EQ(gshscene_diff_damage(nullptr, &damage), GSHSCENE_ERR_INVALID);
  gshscene_diff_destroy(nullptr);
  GSHSCENE_Diff * ok = nullptr;
  ASSERT_EQ(gshscene_diff(va.get(), va.get(), nullptr, nullptr, &ok), GSHSCENE_OK);
  EXPECT_EQ(gshscene_diff_get(ok, 0, &id, &kind), GSHSCENE_ERR_INVALID) << "no changes";
  EXPECT_EQ(gshscene_diff_damage(ok, nullptr), GSHSCENE_ERR_INVALID);
  gshscene_diff_destroy(ok);
}

TEST(DiffIds, KindsHavePinnedValues) {
  EXPECT_EQ((int)GSHSCENE_DIFF_ADDED, 1);
  EXPECT_EQ((int)GSHSCENE_DIFF_REMOVED, 2);
  EXPECT_EQ((int)GSHSCENE_DIFF_CHANGED, 3);
  EXPECT_EQ((int)GSHSCENE_DIFF_KIND_COUNT, 4);
  EXPECT_EQ(GSHSCENE_AA_SUPPORT_DEFAULT, 1.0);
}

// --- damage, the concrete cases ----------------------------------------------------------------------------

TEST(Damage, MovingAShapeDamagesWhereItWasAndWhereItIs) {
  Doc d;
  uint64_t a;
  VersionRef v1;
  {
    EditHolder e(d.s);
    a = d.shape(e, GSHSCENE_ROOT_ID, 10, 10);
    d.shape(e, GSHSCENE_ROOT_ID, 100, 100);  // somewhere else, untouched
    v1 = d.publish_edit(e);
  }
  VersionRef v2;
  {
    EditHolder e(d.s);
    GSHSCENE_Node n = shape_node(d.sq.get());
    n.map = translate(30, 0);
    ASSERT_EQ(gshscene_edit_set(e.edit, a, &n), GSHSCENE_OK);
    v2 = d.publish_edit(e);
  }
  Result r = diff_of(v1.get(), v2.get());
  // Old [10,14] x [10,14], new [30,34] x [0,4]; the union [10,34] x [0,14], and one pixel of AA support, rounded outward.
  expect_rect(r.damage, 9, -1, 35, 15);
}

TEST(Damage, TheMapOfTheSinkAndTheAASupportAndRoundingOutward) {
  Doc d;
  uint64_t a;
  VersionRef v1, v2;
  {
    EditHolder e(d.s);
    a = d.shape(e, GSHSCENE_ROOT_ID, 10.25, 10.5);
    v1 = d.publish_edit(e);
  }
  {
    EditHolder e(d.s);
    GSHSCENE_Node n = shape_node(d.sq.get(), 1);
    n.map = translate(10.25, 10.5);
    ASSERT_EQ(gshscene_edit_set(e.edit, a, &n), GSHSCENE_OK);
    v2 = d.publish_edit(e);
  }
  // Bounds [10.25, 14.25] x [10.5, 14.5].
  GSHSCENE_Damage_Options none;
  none.aa_support = 0.0;
  expect_rect(diff_of(v1.get(), v2.get(), nullptr, &none).damage, 10, 10, 15, 15, "no support: rounded outward only");
  GSHSCENE_Damage_Options half;
  half.aa_support = 0.5;
  expect_rect(diff_of(v1.get(), v2.get(), nullptr, &half).damage, 9, 10, 15, 15, "10.25-0.5 floors to 9; 10.5-0.5 is 10");
  expect_rect(diff_of(v1.get(), v2.get()).damage, 9, 9, 16, 16, "the default is one pixel");
  // The sink's map: a scale of 2 and a translation.
  GSHGEOM_Map device;
  gshgeom_map_translate(2, 100, 0, 0, &device);
  GSHGEOM_Map two = scale_map(2);
  GSHGEOM_Map composed;
  ASSERT_EQ(gshgeom_map_compose(&device, &two, &composed), GSHGEOM_OK);
  expect_rect(diff_of(v1.get(), v2.get(), &composed, &none).damage, 120, 21, 129, 29, "x: 2*10.25+100=120.5 to 2*14.25+100=128.5; y: 21 to 29");
  // A rotation by a quarter turn.
  GSHGEOM_Map quarter;
  ASSERT_EQ(gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, 90.0, &quarter), GSHGEOM_OK);
  GSHSCENE_Damage dmg = diff_of(v1.get(), v2.get(), &quarter, &none).damage;
  EXPECT_EQ(dmg.unbounded, 0);
  EXPECT_LE(dmg.rect.x0, -15);
  EXPECT_GE(dmg.rect.x1, -10);
}

TEST(Damage, RoundingOutwardHandlesNegativeAndFractionalAndExactBounds) {
  struct Case {
    double x, y;
    int64_t x0, y0, x1, y1;
  };
  // The square is 4 x 4, no AA support: floor of the low corner and ceiling of the high one.
  const Case cases[] = {
      {-10.25, -3.5, -11, -4, -6, 1},    // negative fractions floor away from zero
      {-8.0, -2.0, -8, -2, -4, 2},       // exact integers stay
      {0.25, 0.75, 0, 0, 5, 5},          // small positive fractions
      {-0.25, -4.25, -1, -5, 4, 0},      // a fraction just below zero
      {3.0e-300, -3.0e-300, 0, -1, 4, 4},  // a tiny offset is a fraction at the low corner and lost at the high one
  };
  GSHSCENE_Damage_Options none;
  none.aa_support = 0.0;
  for (const Case & c : cases) {
    Doc d;
    VersionRef v1, v2;
    uint64_t id;
    {
      EditHolder e(d.s);
      id = d.shape(e, GSHSCENE_ROOT_ID, c.x, c.y);
      v1 = d.publish_edit(e);
    }
    {
      EditHolder e(d.s);
      GSHSCENE_Node n = shape_node(d.sq.get(), 1);
      n.map = translate(c.x, c.y);
      ASSERT_EQ(gshscene_edit_set(e.edit, id, &n), GSHSCENE_OK);
      v2 = d.publish_edit(e);
    }
    expect_rect(diff_of(v1.get(), v2.get(), nullptr, &none).damage, c.x0, c.y0, c.x1, c.y1);
  }
}

TEST(Damage, AChangedGroupDamagesItsWholeSubtreeAndNothingElse) {
  Doc d;
  uint64_t g;
  VersionRef v1, v2;
  {
    EditHolder e(d.s);
    g = add(e.edit, GSHSCENE_ROOT_ID, group_node());
    d.shape(e, g, 0, 0);
    d.shape(e, g, 20, 0);
    d.shape(e, GSHSCENE_ROOT_ID, 200, 200);
    v1 = d.publish_edit(e);
  }
  {
    EditHolder e(d.s);
    GSHSCENE_Node half = group_node(0.5);
    ASSERT_EQ(gshscene_edit_set(e.edit, g, &half), GSHSCENE_OK);
    v2 = d.publish_edit(e);
  }
  Result r = diff_of(v1.get(), v2.get());
  expect_rect(r.damage, -1, -1, 25, 5, "both shapes of the group, not the one at 200");
  EXPECT_EQ(r.changes.size(), 1u);
  // The group's map moves its descendants: old and new places.
  VersionRef v3;
  {
    EditHolder e(d.s);
    GSHSCENE_Node n = group_node(0.5);
    n.map = translate(0, 50);
    ASSERT_EQ(gshscene_edit_set(e.edit, g, &n), GSHSCENE_OK);
    v3 = d.publish_edit(e);
  }
  expect_rect(diff_of(v2.get(), v3.get()).damage, -1, -1, 25, 55);
}

TEST(Damage, AnAddedChildDamagesItsOwnBoundsAndNotItsSiblings) {
  Doc d;
  VersionRef v1, v2;
  uint64_t added;
  {
    EditHolder e(d.s);
    d.shape(e, GSHSCENE_ROOT_ID, 0, 0);
    d.shape(e, GSHSCENE_ROOT_ID, 100, 0);
    v1 = d.publish_edit(e);
  }
  {
    EditHolder e(d.s);
    added = d.shape(e, GSHSCENE_ROOT_ID, 200, 50);
    v2 = d.publish_edit(e);
  }
  Result r = diff_of(v1.get(), v2.get());
  expect_rect(r.damage, 199, 49, 205, 55);
  // A removal likewise (in the first version only).
  VersionRef v3;
  {
    EditHolder e(d.s);
    ASSERT_EQ(gshscene_edit_remove(e.edit, added), GSHSCENE_OK);
    v3 = d.publish_edit(e);
  }
  expect_rect(diff_of(v2.get(), v3.get()).damage, 199, 49, 205, 55);
}

TEST(Damage, ReorderingChildrenDamagesEveryChild) {
  Doc d;
  VersionRef v1, v2;
  uint64_t a;
  {
    EditHolder e(d.s);
    a = d.shape(e, GSHSCENE_ROOT_ID, 0, 0);
    d.shape(e, GSHSCENE_ROOT_ID, 100, 0);
    d.shape(e, GSHSCENE_ROOT_ID, 300, 0);
    v1 = d.publish_edit(e);
  }
  {
    EditHolder e(d.s);
    ASSERT_EQ(gshscene_edit_move(e.edit, a, GSHSCENE_ROOT_ID, GSHSCENE_APPEND), GSHSCENE_OK);
    v2 = d.publish_edit(e);
  }
  expect_rect(diff_of(v1.get(), v2.get()).damage, -1, -1, 305, 5);
}

TEST(Damage, MovingAShapeBetweenGroupsDamagesItAndNotTheOthers) {
  Doc d;
  VersionRef v1, v2;
  uint64_t p1, p2, mover;
  {
    EditHolder e(d.s);
    p1 = add(e.edit, GSHSCENE_ROOT_ID, group_node());
    p2 = add(e.edit, GSHSCENE_ROOT_ID, group_node());
    mover = d.shape(e, p1, 10, 10);
    d.shape(e, p1, 400, 0);
    d.shape(e, p2, 800, 0);
    v1 = d.publish_edit(e);
  }
  {
    EditHolder e(d.s);
    ASSERT_EQ(gshscene_edit_move(e.edit, mover, p2, 0), GSHSCENE_OK);
    v2 = d.publish_edit(e);
  }
  Result r = diff_of(v1.get(), v2.get());
  expect_rect(r.damage, 9, 9, 15, 15);
  (void)p1;
}

TEST(Damage, AChangeToAHiddenNodeNobodyInstancesDamagesNothing) {
  Doc d;
  VersionRef v1, v2;
  uint64_t t;
  {
    EditHolder e(d.s);
    GSHSCENE_Node hidden = group_node();
    hidden.flags = GSHSCENE_NODE_HIDDEN;
    uint64_t def = add(e.edit, GSHSCENE_ROOT_ID, hidden);
    t = d.shape(e, def, 0, 0);
    d.shape(e, GSHSCENE_ROOT_ID, 50, 50);
    v1 = d.publish_edit(e);
  }
  {
    EditHolder e(d.s);
    GSHSCENE_Node n = shape_node(d.sq.get(), 4);
    ASSERT_EQ(gshscene_edit_set(e.edit, t, &n), GSHSCENE_OK);
    v2 = d.publish_edit(e);
  }
  Result r = diff_of(v1.get(), v2.get());
  EXPECT_EQ(r.changes.size(), 1u);
  EXPECT_EQ(r.damage.empty, 1);
  EXPECT_EQ(r.damage.unbounded, 0);
}

TEST(Damage, EditingASymbolDamagesEveryInstanceOfIt) {
  Doc d;
  VersionRef v1, v2;
  uint64_t t;
  {
    EditHolder e(d.s);
    GSHSCENE_Node hidden = group_node();
    hidden.flags = GSHSCENE_NODE_HIDDEN;
    uint64_t def = add(e.edit, GSHSCENE_ROOT_ID, hidden);
    t = d.shape(e, def, 0, 0);
    for (double x : {0.0, 100.0, 300.0}) {
      GSHSCENE_Node in = instance_node(def);
      in.map = translate(x, 10);
      add(e.edit, GSHSCENE_ROOT_ID, in);
    }
    d.shape(e, GSHSCENE_ROOT_ID, 1000, 1000);  // not an instance
    v1 = d.publish_edit(e);
  }
  {
    EditHolder e(d.s);
    GSHSCENE_Node n = shape_node(d.sq.get(), 0);
    n.map = translate(0, 20);  // the symbol's shape moves down by 20 inside the symbol
    ASSERT_EQ(gshscene_edit_set(e.edit, t, &n), GSHSCENE_OK);
    v2 = d.publish_edit(e);
  }
  Result r = diff_of(v1.get(), v2.get());
  // Instances at x = 0, 100, 300 and y = 10; the shape is at y 0..4 before and 20..24 after, inside: y 10..14 and 30..34.
  expect_rect(r.damage, -1, 9, 305, 35);
}

TEST(Damage, ChildrenInNoParticularIdOrderAreFoundByMembershipNotByPosition) {
  Doc d;
  VersionRef v1, v2;
  {
    EditHolder e(d.s);
    Ids ids;
    for (int i = 0; i < 6; i++) {
      ids.push_back(d.shape(e, GSHSCENE_ROOT_ID, 100.0 * i, 0));
    }
    for (size_t i = 0; i < ids.size(); i++) {  // reverse the order: the list is descending by id
      ASSERT_EQ(gshscene_edit_move(e.edit, ids[i], GSHSCENE_ROOT_ID, 0), GSHSCENE_OK);
    }
    v1 = d.publish_edit(e);
  }
  {
    EditHolder e(d.s);
    d.shape(e, GSHSCENE_ROOT_ID, 1000, 1000);
    v2 = d.publish_edit(e);
  }
  expect_rect(diff_of(v1.get(), v2.get()).damage, 999, 999, 1005, 1005, "only the added child, though the list is not ascending");
}

TEST(Damage, ADeviceMapThatPushesTheComposedMapPastTheBoundIsUnbounded) {
  Doc d;
  VersionRef v1, v2;
  {
    EditHolder e(d.s);
    v1 = d.publish_edit(e);
  }
  {
    EditHolder e(d.s);
    GSHSCENE_Node g = group_node();
    g.map = scale_map(std::ldexp(1.0, 50));
    uint64_t gid = add(e.edit, GSHSCENE_ROOT_ID, g);
    d.shape(e, gid, 0, 0);
    v2 = d.publish_edit(e);
  }
  GSHGEOM_Map device = scale_map(std::ldexp(1.0, 60));  // the version alone composes; with this it is 2^110, past 2^100
  EXPECT_EQ(diff_of(v1.get(), v2.get(), &device).damage.unbounded, 1);
  GSHGEOM_Map small = scale_map(2.0);
  EXPECT_EQ(diff_of(v1.get(), v2.get(), &small).damage.unbounded, 0);
}

TEST(Damage, AGlyphRunWhoseOutlineIs3DIsUnbounded) {
  Doc d;
  VersionRef v1, v2;
  {
    EditHolder e(d.s);
    v1 = d.publish_edit(e);
  }
  GSHGEOM_Path * three = new_path(3);
  gshgeom_path_move_to(three, P(0, 0, 0));
  gshgeom_path_line_to(three, P(1, 1, 1));
  Ref outline(intern(d.s.store(), three));
  gshgeom_path_destroy(three);
  GSHSCENE_Glyph glyphs[1] = {{1, 0, 0, outline.get()}};
  GSHSCENE_Glyph_Run run;
  std::memset(&run, 0, sizeof run);
  run.source = GSHSCENE_GLYPHS_FROM_OUTLINES;
  run.glyph_count = 1;
  run.glyphs = glyphs;
  Ref text;
  ASSERT_EQ(gshscene_store_intern_glyph_run(d.s.store(), &run, &text.resource), GSHSCENE_OK);
  {
    EditHolder e(d.s);
    add(e.edit, GSHSCENE_ROOT_ID, shape_node(text.get()));
    v2 = d.publish_edit(e);
  }
  EXPECT_EQ(diff_of(v1.get(), v2.get()).damage.unbounded, 1);
}

TEST(Damage, AChangedInstanceDamagesItsPictureAndNothingAfterIt) {
  Doc d;
  VersionRef v1, v2;
  uint64_t inst;
  {
    EditHolder e(d.s);
    GSHSCENE_Node hidden = group_node();
    hidden.flags = GSHSCENE_NODE_HIDDEN;
    uint64_t def = add(e.edit, GSHSCENE_ROOT_ID, hidden);
    d.shape(e, def, 0, 0);
    GSHSCENE_Node in = instance_node(def);
    in.map = translate(10, 10);
    inst = add(e.edit, GSHSCENE_ROOT_ID, in);
    d.shape(e, GSHSCENE_ROOT_ID, 500, 500);  // after the instance, unchanged
    v1 = d.publish_edit(e);
  }
  {
    EditHolder e(d.s);
    GSHSCENE_Node in = instance_node(0);
    GSHSCENE_Node_Info info;
    ASSERT_EQ(gshscene_edit_node(e.edit, inst, &info), GSHSCENE_OK);
    in = info.node;
    in.map = translate(30, 10);
    ASSERT_EQ(gshscene_edit_set(e.edit, inst, &in), GSHSCENE_OK);
    v2 = d.publish_edit(e);
  }
  expect_rect(diff_of(v1.get(), v2.get()).damage, 9, 9, 35, 15, "old and new places of the instance, not the shape at 500");
}

TEST(Damage, GlyphRunsFromOutlinesAreBoundedAndFromAFontAreNot) {
  Doc d;
  VersionRef v1, v2, v3;
  {
    EditHolder e(d.s);
    v1 = d.publish_edit(e);
  }
  // A run of outlines: two squares at (0,0) and (20, 5), drawn at a translation.
  GSHSCENE_Glyph glyphs[2] = {{1, 0, 0, d.sq.get()}, {2, 20, 5, d.sq.get()}};
  GSHSCENE_Glyph_Run run;
  std::memset(&run, 0, sizeof run);
  run.source = GSHSCENE_GLYPHS_FROM_OUTLINES;
  run.glyph_count = 2;
  run.glyphs = glyphs;
  Ref outlines;
  ASSERT_EQ(gshscene_store_intern_glyph_run(d.s.store(), &run, &outlines.resource), GSHSCENE_OK);
  {
    EditHolder e(d.s);
    GSHSCENE_Node n = shape_node(outlines.get());
    n.map = translate(100, 100);
    add(e.edit, GSHSCENE_ROOT_ID, n);
    v2 = d.publish_edit(e);
  }
  Result r = diff_of(v1.get(), v2.get());
  expect_rect(r.damage, 99, 99, 125, 110, "squares at 100..104 and 120..124 by 100..104 and 105..109");
  // A run from a font has no bounds the scene can know.
  GSHSCENE_Glyph fg[1] = {{9, 0, 0, nullptr}};
  GSHSCENE_Glyph_Run frun;
  std::memset(&frun, 0, sizeof frun);
  frun.source = GSHSCENE_GLYPHS_FROM_FONT;
  frun.font_key = 0x77;
  frun.size = 12;
  frun.glyph_count = 1;
  frun.glyphs = fg;
  Ref text;
  ASSERT_EQ(gshscene_store_intern_glyph_run(d.s.store(), &frun, &text.resource), GSHSCENE_OK);
  {
    EditHolder e(d.s);
    add(e.edit, GSHSCENE_ROOT_ID, shape_node(text.get()));
    v3 = d.publish_edit(e);
  }
  Result u = diff_of(v2.get(), v3.get());
  EXPECT_EQ(u.damage.unbounded, 1);
  EXPECT_EQ(u.damage.empty, 0);
  // An empty run bounds nothing.
  GSHSCENE_Glyph_Run none;
  std::memset(&none, 0, sizeof none);
  none.source = GSHSCENE_GLYPHS_FROM_OUTLINES;
  Ref empty;
  ASSERT_EQ(gshscene_store_intern_glyph_run(d.s.store(), &none, &empty.resource), GSHSCENE_OK);
  VersionRef v4;
  {
    EditHolder e(d.s);
    add(e.edit, GSHSCENE_ROOT_ID, shape_node(empty.get()));
    v4 = d.publish_edit(e);
  }
  Result e = diff_of(v3.get(), v4.get());
  EXPECT_EQ(e.damage.unbounded, 0) << "the font run is in both versions and unchanged: only the added empty run is damaged";
  EXPECT_EQ(e.damage.empty, 1) << "and an empty run bounds nothing";
}

TEST(Damage, APathWithNoSegmentBoundsNothingAndA3DPathAndA3DMapAreUnbounded) {
  Doc d;
  VersionRef v1, v2, v3;
  {
    EditHolder e(d.s);
    v1 = d.publish_edit(e);
  }
  GSHGEOM_Path * lone = new_path();
  gshgeom_path_move_to(lone, P(3, 3));
  Ref lone_res(intern(d.s.store(), lone));
  gshgeom_path_destroy(lone);
  {
    EditHolder e(d.s);
    add(e.edit, GSHSCENE_ROOT_ID, shape_node(lone_res.get()));
    v2 = d.publish_edit(e);
  }
  Result r = diff_of(v1.get(), v2.get());
  EXPECT_EQ(r.damage.empty, 1) << "a move that starts no segment draws nothing";
  EXPECT_EQ(r.damage.unbounded, 0);
  GSHGEOM_Path * three = new_path(3);
  gshgeom_path_move_to(three, P(0, 0, 0));
  gshgeom_path_line_to(three, P(1, 1, 1));
  Ref three_res(intern(d.s.store(), three));
  gshgeom_path_destroy(three);
  {
    EditHolder e(d.s);
    add(e.edit, GSHSCENE_ROOT_ID, shape_node(three_res.get()));
    v3 = d.publish_edit(e);
  }
  EXPECT_EQ(diff_of(v2.get(), v3.get()).damage.unbounded, 1) << "3D path";
  GSHGEOM_Map m3;
  gshgeom_map_identity(3, &m3);
  EXPECT_EQ(diff_of(v2.get(), v3.get(), &m3).damage.unbounded, 1) << "3D map";
  // A 3D map with nothing changed still says nothing is damaged.
  Result same = diff_of(v3.get(), v3.get(), &m3);
  EXPECT_EQ(same.damage.unbounded, 1) << "the map cannot be applied, whatever changed";
}

TEST(Damage, AResultPastTheRangeOfTheIntegersIsUnbounded) {
  Doc d;
  VersionRef v1, v2;
  {
    EditHolder e(d.s);
    v1 = d.publish_edit(e);
  }
  {
    EditHolder e(d.s);
    d.shape(e, GSHSCENE_ROOT_ID, 0, 0);
    v2 = d.publish_edit(e);
  }
  GSHGEOM_Map huge = scale_map(1e19);
  EXPECT_EQ(diff_of(v1.get(), v2.get(), &huge).damage.unbounded, 1);
  GSHGEOM_Map big_but_fine = scale_map(1e15);
  Result r = diff_of(v1.get(), v2.get(), &big_but_fine);
  EXPECT_EQ(r.damage.unbounded, 0);
  EXPECT_EQ(r.damage.rect.x1, (int64_t)4000000000000000ll + 1) << "4 * 1e15 plus the support";
  GSHGEOM_Map overflow;
  ASSERT_EQ(gshgeom_map_scale(2, 1e29, 1e29, 1.0, &overflow), GSHGEOM_OK);
  EXPECT_EQ(diff_of(v1.get(), v2.get(), &overflow).damage.unbounded, 1) << "4e29 is finite, and far past int64";
}

TEST(Damage, AComposedMapPastTheMapBoundMakesTheDamageUnbounded) {
  Doc d;
  VersionRef v1, v2;
  {
    EditHolder e(d.s);
    v1 = d.publish_edit(e);
  }
  {
    EditHolder e(d.s);
    GSHSCENE_Node outer = group_node();
    outer.map = scale_map(std::ldexp(1.0, 60));
    uint64_t g1 = add(e.edit, GSHSCENE_ROOT_ID, outer);
    uint64_t g2 = add(e.edit, g1, outer);  // 2^120 composed: past the bound of a map entry
    d.shape(e, g2, 0, 0);
    v2 = d.publish_edit(e);
  }
  EXPECT_EQ(diff_of(v1.get(), v2.get()).damage.unbounded, 1);
  EXPECT_EQ(diff_of(v2.get(), v1.get()).damage.unbounded, 1) << "in the first version as well as the second";
}

TEST(Damage, ANodeWhoseOnlyChangeIsAnAdditionToAnEmptyGroupDamagesJustThatChild) {
  Doc d;
  VersionRef v1, v2;
  uint64_t g;
  {
    EditHolder e(d.s);
    GSHSCENE_Node gn = group_node();
    gn.map = translate(50, 50);
    g = add(e.edit, GSHSCENE_ROOT_ID, gn);
    v1 = d.publish_edit(e);
  }
  {
    EditHolder e(d.s);
    d.shape(e, g, 1, 2);
    v2 = d.publish_edit(e);
  }
  expect_rect(diff_of(v1.get(), v2.get()).damage, 50, 51, 56, 57);
}

// --- the oracle -------------------------------------------------------------------------------------------------------

namespace {

struct Rng {
  uint64_t state;
  explicit Rng(uint64_t seed) : state(seed * UINT64_C(0x9E3779B97F4A7C15) + 12345) {}
  uint64_t next() {
    state ^= state >> 12;
    state ^= state << 25;
    state ^= state >> 27;
    return state * UINT64_C(2685821657736338717);
  }
  unsigned below(unsigned n) { return (unsigned)(next() % n); }
};

/** One drawing record of an occurrence of a shape: where it can touch, and everything that decides what it draws. */
struct Op {
  double x0, y0, x1, y1;
  uint64_t signature;
};

/** A hash of everything that decides what a shape occurrence draws, from the public API alone. */
uint64_t signature_of(const GSHSCENE_Version * v, const Occurrence & o) {
  GSHGEOM_Hash h;
  gshgeom_hash_init(&h);
  GSHSCENE_Node_Info shape = info_of(v, o.node);
  gshgeom_hash_u64(&h, gshscene_resource_hash(shape.node.resource));
  gshgeom_hash_u8(&h, (uint8_t)shape.node.fill_rule);
  gshgeom_hash_i32(&h, shape.node.layer);
  gshgeom_hash_f64(&h, shape.node.depth);
  gshgeom_hash_u8(&h, (uint8_t)shape.node.paint.kind);
  for (int k = 0; k < 4; k++) {
    gshgeom_hash_f64(&h, shape.node.paint.rgba[k]);
  }
  gshgeom_hash_u8(&h, (uint8_t)shape.node.paint.space.primaries);
  gshgeom_hash_u8(&h, (uint8_t)shape.node.paint.space.transfer);
  gshgeom_hash_f64(&h, shape.node.paint.space.gamma);
  if (shape.node.paint.kind == GSHSCENE_PAINT_FOREGROUND) {
    // What currentColor draws is the version's foreground.
    GSHSCENE_Color fg;
    EXPECT_EQ(gshscene_version_foreground(v, &fg), GSHSCENE_OK);
    for (int k = 0; k < 4; k++) {
      gshgeom_hash_f64(&h, fg.rgba[k]);
    }
    gshgeom_hash_u8(&h, (uint8_t)fg.space.primaries);
    gshgeom_hash_u8(&h, (uint8_t)fg.space.transfer);
    gshgeom_hash_f64(&h, fg.space.gamma);
  }
  gshgeom_hash_u64(&h, gshgeom_map_hash(&o.map));
  // The groups above it, and above each instance it was reached through.
  for (uint64_t id : o.path) {
    uint64_t up = info_of(v, id).parent;
    while (up != 0) {
      GSHSCENE_Node_Info g = info_of(v, up);
      gshgeom_hash_f64(&h, g.node.opacity);
      gshgeom_hash_u8(&h, (uint8_t)g.node.blend_space.primaries);
      gshgeom_hash_u8(&h, (uint8_t)g.node.blend_space.transfer);
      gshgeom_hash_f64(&h, g.node.blend_space.gamma);
      gshgeom_hash_u8(&h, (uint8_t)g.node.composite);
      gshgeom_hash_u8(&h, (uint8_t)g.node.ordering);
      gshgeom_hash_u64(&h, gshscene_resource_hash(g.node.clip));
      gshgeom_hash_u64(&h, gshgeom_map_hash(&g.node.clip_map));
      gshgeom_hash_u8(&h, (uint8_t)g.node.clip_rule);
      up = g.parent;
    }
  }
  return gshgeom_hash_value(&h);
}

std::vector<Op> ops_of(const GSHSCENE_Version * v, bool * unbounded) {
  std::vector<Op> ops;
  for (const Occurrence & o : occurrences(v)) {
    if (o.kind == GSHSCENE_NODE_GROUP && gshscene_composite_is_unbounded((GSHSCENE_Composite)o.props.composite)) {
      // A group whose operator changes the backdrop where it has no content touches every pixel (N4, N13).
      Op everywhere;
      everywhere.x0 = everywhere.y0 = -1e12;
      everywhere.x1 = everywhere.y1 = 1e12;
      GSHGEOM_Hash h;
      gshgeom_hash_init(&h);
      gshgeom_hash_u64(&h, o.node);
      gshgeom_hash_f64(&h, o.props.opacity);
      gshgeom_hash_u8(&h, (uint8_t)o.props.composite);
      gshgeom_hash_u8(&h, (uint8_t)o.props.blend_space.primaries);
      gshgeom_hash_u8(&h, (uint8_t)o.props.blend_space.transfer);
      everywhere.signature = gshgeom_hash_value(&h);
      ops.push_back(everywhere);
      continue;
    }
    if (o.kind != GSHSCENE_NODE_SHAPE) {
      continue;
    }
    GSHSCENE_Node_Info shape = info_of(v, o.node);
    const GSHGEOM_Path * path = gshscene_resource_path(shape.node.resource);
    if (!path) {
      *unbounded = true;
      continue;
    }
    GSHGEOM_Bounds b;
    EXPECT_EQ(gshgeom_path_bounds(path, &b), GSHGEOM_OK);
    if (b.empty) {
      continue;
    }
    Op op;
    bool first = true;
    for (double x : {b.min.x, b.max.x}) {
      for (double y : {b.min.y, b.max.y}) {
        GSHGEOM_Point q;
        EXPECT_EQ(gshgeom_map_apply_point(&o.map, P(x, y), &q), GSHGEOM_OK);
        if (first) {
          op.x0 = op.x1 = q.x;
          op.y0 = op.y1 = q.y;
          first = false;
        } else {
          op.x0 = std::min(op.x0, q.x);
          op.x1 = std::max(op.x1, q.x);
          op.y0 = std::min(op.y0, q.y);
          op.y1 = std::max(op.y1, q.y);
        }
      }
    }
    op.signature = signature_of(v, o);
    ops.push_back(op);
  }
  return ops;
}

/** What the pixel (px, py) is drawn with: the signatures of the records that can touch it, in draw order. */
std::vector<uint64_t> column(const std::vector<Op> & ops, int px, int py) {
  std::vector<uint64_t> out;
  for (const Op & op : ops) {
    if (op.x0 < px + 1 && op.x1 > px && op.y0 < py + 1 && op.y1 > py) {
      out.push_back(op.signature);
    }
  }
  return out;
}

/** The scene of one oracle case: a random tree and the lists of what can be edited. */
struct Case {
  SceneHolder s;
  std::vector<Ref> paths;
  Ids groups, defs, shapes, instances;
  Rng rng;
  explicit Case(uint64_t seed) : rng(seed) {
    for (int i = 0; i < 6; i++) {
      GSHGEOM_Path * p = new_path();
      double w = 2 + rng.below(5), h = 2 + rng.below(5);
      gshgeom_path_move_to(p, P(0, 0));
      gshgeom_path_line_to(p, P(w, 0));
      gshgeom_path_line_to(p, P(w, h));
      gshgeom_path_line_to(p, P(0, h));
      gshgeom_path_close(p);
      paths.emplace_back(intern(s.store(), p));
      gshgeom_path_destroy(p);
    }
  }
  const GSHSCENE_Resource * path() { return paths[rng.below((unsigned)paths.size())].get(); }
  GSHGEOM_Map map() {
    GSHGEOM_Map t = translate(rng.below(60), rng.below(60));
    if (rng.below(4) == 0) {
      GSHGEOM_Map sc = scale_map(1.0 + rng.below(3));
      GSHGEOM_Map out;
      gshgeom_map_compose(&t, &sc, &out);
      return out;
    }
    return t;
  }
  GSHSCENE_Node shape() {
    GSHSCENE_Node n = shape_node(path(), (int32_t)rng.below(4));
    n.map = map();
    n.fill_rule = rng.below(2) ? GSHGEOM_FILL_EVEN_ODD : GSHGEOM_FILL_NONZERO;
    n.paint = solid(rng.below(3) / 2.0, rng.below(2), 0.25, 1.0 - rng.below(2) * 0.5);
    n.depth = (double)rng.below(3) * 0.5;
    if (rng.below(4) == 0) {
      n.paint = solid(0, 0, 0, 1.0 - rng.below(2) * 0.5);
      n.paint.kind = GSHSCENE_PAINT_FOREGROUND;
    } else if (rng.below(5) == 0) {
      n.paint.space = srgb_space();
    }
    return n;
  }
  GSHSCENE_Node group() {
    GSHSCENE_Node n = group_node(rng.below(3) == 0 ? 0.5 : 1.0, (int32_t)rng.below(3));
    n.map = rng.below(2) ? map() : translate(0, 0);
    n.depth = (double)rng.below(3) * 0.5;
    n.ordering = rng.below(4) == 0 ? (uint32_t)(1 + rng.below(2)) : (uint32_t)GSHSCENE_ORDER_FLAT;
    if (rng.below(5) == 0) {
      // Half of these operators reach beyond the group's own bounds (N4).
      n.composite = rng.below((unsigned)GSHSCENE_COMPOSITE_COUNT);
    }
    if (rng.below(6) == 0) {
      n.blend_space = linear_srgb_space();
    }
    if (rng.below(4) == 0) {
      n.clip = path();
      n.clip_map = translate(rng.below(10), rng.below(10));
    }
    return n;
  }
  uint64_t any(const Ids & ids) { return ids.empty() ? 0 : ids[rng.below((unsigned)ids.size())]; }

  void build(EditHolder & e) {
    groups.push_back(GSHSCENE_ROOT_ID);
    unsigned ng = 2 + rng.below(3);
    for (unsigned i = 0; i < ng; i++) {
      groups.push_back(add(e.edit, any(groups), group()));
    }
    unsigned nd = 1 + rng.below(2);
    for (unsigned i = 0; i < nd; i++) {
      GSHSCENE_Node def = group();
      def.flags = GSHSCENE_NODE_HIDDEN;
      uint64_t id = add(e.edit, GSHSCENE_ROOT_ID, def);
      defs.push_back(id);
      for (unsigned k = 0; k < 1 + rng.below(3); k++) {
        shapes.push_back(add(e.edit, id, shape()));
      }
    }
    unsigned ns = 3 + rng.below(6);
    for (unsigned i = 0; i < ns; i++) {
      shapes.push_back(add(e.edit, any(groups), shape()));
    }
    unsigned ni = 1 + rng.below(4);
    for (unsigned i = 0; i < ni; i++) {
      GSHSCENE_Node in = instance_node(any(defs), (int32_t)rng.below(3));
      in.map = map();
      instances.push_back(add(e.edit, any(groups), in));
    }
  }

  /** One random edit; failures of single operations are fine (the generator does not check legality). */
  void mutate(EditHolder & e) {
    unsigned n = 1 + rng.below(3);
    for (unsigned i = 0; i < n; i++) {
      switch (rng.below(9)) {
        case 0: {
          GSHSCENE_Node s = shape();
          gshscene_edit_set(e.edit, any(shapes), &s);
          break;
        }
        case 1: {
          GSHSCENE_Node g = group();
          uint64_t id = any(groups);
          if (id != GSHSCENE_ROOT_ID) {
            gshscene_edit_set(e.edit, id, &g);
          }
          break;
        }
        case 2:
          shapes.push_back(add(e.edit, any(groups), shape()));
          break;
        case 3: {
          uint64_t victim = any(shapes);
          gshscene_edit_remove(e.edit, victim);
          break;
        }
        case 4:
          gshscene_edit_move(e.edit, any(shapes), any(groups), GSHSCENE_APPEND);
          break;
        case 5: {
          uint64_t id = any(shapes);
          GSHSCENE_Node_Info info;
          if (gshscene_edit_node(e.edit, id, &info) == GSHSCENE_OK) {
            gshscene_edit_move(e.edit, id, info.parent, 0);
          }
          break;
        }
        case 6: {
          uint64_t id = any(groups);
          GSHSCENE_Node_Info info;
          if (id != GSHSCENE_ROOT_ID && gshscene_edit_node(e.edit, id, &info) == GSHSCENE_OK) {
            GSHSCENE_Node g = info.node;
            g.flags ^= GSHSCENE_NODE_HIDDEN;
            gshscene_edit_set(e.edit, id, &g);
          }
          break;
        }
        case 7:
          if (rng.below(2) == 0) {
            GSHSCENE_Color fg = {{rng.below(3) / 2.0, 0.25, rng.below(2) * 1.0, 1.0}, rng.below(2) ? srgb_space() : GSHSCENE_Color_Space{}};
            gshscene_edit_set_foreground(e.edit, &fg);
            break;
          }
          {
          uint64_t id = any(instances);
          GSHSCENE_Node_Info info;
          if (id != 0 && gshscene_edit_node(e.edit, id, &info) == GSHSCENE_OK) {
            GSHSCENE_Node in = info.node;
            in.map = map();
            gshscene_edit_set(e.edit, id, &in);
          }
          break;
        }
        default: {
          // Edit the symbol: the shapes in a definition.
          uint64_t id = any(shapes);
          GSHSCENE_Node_Info info;
          if (gshscene_edit_node(e.edit, id, &info) == GSHSCENE_OK) {
            GSHSCENE_Node s = info.node;
            s.layer += 1;
            gshscene_edit_set(e.edit, id, &s);
          }
          break;
        }
      }
    }
  }
};

}  // namespace

TEST(DamageOracle, EveryPixelWhoseDrawingDiffersLiesInsideTheDamage) {
  const int kCases = 120;
  int bounded = 0, nonempty = 0, tighter = 0, checked_cells = 0, differing_cells = 0, published = 0, unbounded_cases = 0;
  for (int c = 0; c < kCases; c++) {
    Case cs((uint64_t)c + 1);
    VersionRef v1;
    {
      EditHolder e(cs.s);
      cs.build(e);
      publish(cs.s, e, &v1);
    }
    VersionRef v2;
    {
      EditHolder e(cs.s);
      cs.mutate(e);
      const GSHSCENE_Version * out = nullptr;
      if (gshscene_scene_publish(cs.s.scene, e.edit, nullptr, &out) != GSHSCENE_OK) {
        continue;  // an edit the generator did not make legal (a dangling instance): not a case
      }
      v2.reset(out);
    }
    published++;
    Result r = diff_of(v1.get(), v2.get());
    bool unbounded_a = false, unbounded_b = false;
    std::vector<Op> a = ops_of(v1.get(), &unbounded_a);
    std::vector<Op> b = ops_of(v2.get(), &unbounded_b);
    ASSERT_FALSE(unbounded_a || unbounded_b);
    if (r.damage.unbounded) {
      // Every shape here has bounds, so the only reason is a group whose operator reaches beyond its own (N13).
      bool reaches = false;
      for (const GSHSCENE_Version * v : {v1.get(), v2.get()}) {
        for (const Occurrence & o : occurrences(v)) {
          reaches = reaches || (o.kind == GSHSCENE_NODE_GROUP &&
                                   gshscene_composite_is_unbounded((GSHSCENE_Composite)o.props.composite));
        }
      }
      EXPECT_TRUE(reaches) << "case " << c << ": unbounded damage with no unbounded operator in either version";
      unbounded_cases++;
      continue;
    }
    bounded++;
    if (!r.damage.empty) {
      nonempty++;
    }
    bool any_differs = false;
    const int lo = -10, hi = 300;
    for (int py = lo; py < hi; py++) {
      for (int px = lo; px < hi; px++) {
        checked_cells++;
        if (column(a, px, py) != column(b, px, py)) {
          differing_cells++;
          any_differs = true;
          bool inside = !r.damage.empty && px >= r.damage.rect.x0 && px < r.damage.rect.x1 && py >= r.damage.rect.y0 &&
                        py < r.damage.rect.y1;
          ASSERT_TRUE(inside) << "case " << c << ": pixel (" << px << ", " << py << ") is drawn differently and is outside the damage"
                              << (r.damage.empty ? " (empty)" : "");
        }
      }
    }
    if (any_differs && !r.damage.empty && (r.damage.rect.x1 - r.damage.rect.x0 < hi - lo - 20)) {
      tighter++;
    }
    GSHSCENE_Color fa, fb;
    ASSERT_EQ(gshscene_version_foreground(v1.get(), &fa), GSHSCENE_OK);
    ASSERT_EQ(gshscene_version_foreground(v2.get(), &fb), GSHSCENE_OK);
    bool fg_same = std::memcmp(&fa, &fb, sizeof fa) == 0;
    if (r.changes.empty() && fg_same) {
      EXPECT_TRUE(r.damage.empty);
    }
  }
  // The oracle is not vacuous: most cases changed something, and the damage is a real rectangle, not the whole target.
  EXPECT_GE(published, kCases * 3 / 4);
  EXPECT_GE(nonempty, published / 2);
  EXPECT_GE(differing_cells, 2000);
  EXPECT_GE(tighter, published / 4) << "damage that is always huge would pass and say nothing";
  EXPECT_GE(unbounded_cases, 1) << "some case must reach the operators that reach beyond their bounds";
  EXPECT_LE(unbounded_cases, published / 2) << "and most must not, or the oracle says little";
  (void)bounded;
  (void)checked_cells;
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
