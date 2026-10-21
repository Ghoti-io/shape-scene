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
 * What the retained scene carries of paint, color and composition (story 12): the
 * node fields, the per-version foreground through edit, publish, merge and diff, the
 * operators that reach beyond their bounds, and the push limit of groups that need one.
 */

#include "scene_fixtures.h"

#include <cmath>

using namespace scene;

namespace {

const double kNaN = std::nan("");
const double kInf = INFINITY;

struct Doc {
  SceneHolder s;
  Ref sq;
  explicit Doc(const GSHSCENE_Limits * limits = nullptr) : s(limits), sq(intern_square(s.store())) {}
  GSHSCENE_Node shape_at(double x, double y) {
    GSHSCENE_Node n = shape_node(sq.get());
    n.map = translate(x, y);
    return n;
  }
  GSHSCENE_Node foreground_shape(double x, double y, double alpha = 1.0) {
    GSHSCENE_Node n = shape_at(x, y);
    n.paint = solid(0, 0, 0, alpha);
    n.paint.kind = GSHSCENE_PAINT_FOREGROUND;
    return n;
  }
  VersionRef current() {
    VersionRef v;
    EXPECT_EQ(gshscene_scene_current(s.scene, &v.version), GSHSCENE_OK);
    return v;
  }
  VersionRef publish_edit(EditHolder & e) {
    VersionRef v;
    publish(s, e, &v);
    return v;
  }
};

GSHSCENE_Color color(double r, double g, double b, double a, GSHSCENE_Color_Space sp = GSHSCENE_Color_Space{}) {
  GSHSCENE_Color c = {{r, g, b, a}, sp};
  return c;
}

struct Diffed {
  std::vector<std::pair<uint64_t, int>> changes;
  GSHSCENE_Damage damage;
  int foreground_changed;
};

Diffed diff_of(const GSHSCENE_Version * a, const GSHSCENE_Version * b) {
  Diffed r;
  std::memset(&r.damage, 0xAB, sizeof r.damage);
  GSHSCENE_Diff * d = nullptr;
  EXPECT_EQ(gshscene_diff(a, b, nullptr, nullptr, &d), GSHSCENE_OK);
  if (!d) {
    r.foreground_changed = -1;
    return r;
  }
  for (size_t i = 0; i < gshscene_diff_count(d); i++) {
    uint64_t id;
    GSHSCENE_Diff_Kind kind;
    EXPECT_EQ(gshscene_diff_get(d, i, &id, &kind), GSHSCENE_OK);
    r.changes.push_back({id, (int)kind});
  }
  EXPECT_EQ(gshscene_diff_damage(d, &r.damage), GSHSCENE_OK);
  r.foreground_changed = gshscene_diff_foreground_changed(d);
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

}  // namespace

// --- node fields -----------------------------------------------------------------------------------------------------

TEST(PaintNode, TheDefaultsOfANodeAreTheOldBehaviour) {
  for (GSHSCENE_Node_Kind kind : {GSHSCENE_NODE_GROUP, GSHSCENE_NODE_SHAPE, GSHSCENE_NODE_INSTANCE}) {
    GSHSCENE_Node n;
    std::memset(&n, 0xAB, sizeof n);
    gshscene_node_init(kind, &n);
    EXPECT_EQ(n.depth, 0.0) << kind;
    EXPECT_EQ(n.composite, (uint32_t)GSHSCENE_COMPOSITE_SRC_OVER) << kind;
    EXPECT_EQ(n.ordering, (uint32_t)GSHSCENE_ORDER_FLAT) << kind;
    EXPECT_EQ(gshscene_color_space_is_target(&n.blend_space), 1) << kind;
    EXPECT_EQ(gshscene_color_space_is_target(&n.paint.space), 1) << kind;
  }
}

TEST(PaintNode, AGroupsBlendSpaceCompositeOrderingAndDepthReadBack) {
  Doc d;
  EditHolder e(d.s);
  GSHSCENE_Node g = group_node(0.5, 3);
  g.depth = -2.25;
  g.blend_space = space_of(GCOL_PRIMARIES_DISPLAY_P3, GCOL_TRANSFER_GAMMA, 2.2);
  g.composite = GSHSCENE_COMPOSITE_DEST_ATOP;
  g.ordering = GSHSCENE_ORDER_OVERLAY;
  uint64_t id = add(e.edit, GSHSCENE_ROOT_ID, g);
  GSHSCENE_Node s = d.shape_at(1, 2);
  s.depth = 7.5;
  s.paint = solid(0.25, 0.5, 0.75, 1.0);
  s.paint.space = space_of(GCOL_PRIMARIES_ADOBE_RGB, GCOL_TRANSFER_LINEAR);
  uint64_t sid = add(e.edit, id, s);
  GSHSCENE_Node i = instance_node(id);
  i.depth = -0.5;
  uint64_t iid = add(e.edit, GSHSCENE_ROOT_ID, i);
  VersionRef v = d.publish_edit(e);
  GSHSCENE_Node_Info gi = info_of(v.get(), id);
  EXPECT_EQ(gi.node.depth, -2.25);
  EXPECT_EQ(gi.node.blend_space.primaries, GCOL_PRIMARIES_DISPLAY_P3);
  EXPECT_EQ(gi.node.blend_space.transfer, GCOL_TRANSFER_GAMMA);
  EXPECT_EQ(gi.node.blend_space.gamma, 2.2);
  EXPECT_EQ(gi.node.composite, (uint32_t)GSHSCENE_COMPOSITE_DEST_ATOP);
  EXPECT_EQ(gi.node.ordering, (uint32_t)GSHSCENE_ORDER_OVERLAY);
  GSHSCENE_Node_Info si = info_of(v.get(), sid);
  EXPECT_EQ(si.node.depth, 7.5);
  EXPECT_EQ(si.node.paint.space.primaries, GCOL_PRIMARIES_ADOBE_RGB);
  EXPECT_EQ(si.node.paint.space.transfer, GCOL_TRANSFER_LINEAR);
  EXPECT_EQ(info_of(v.get(), iid).node.depth, -0.5);
}

TEST(PaintNode, AFieldThatDoesNotApplyToTheKindIsTheDefaultOnRead) {
  Doc d;
  EditHolder e(d.s);
  GSHSCENE_Node s = d.shape_at(0, 0);
  s.blend_space = srgb_space();
  s.composite = GSHSCENE_COMPOSITE_XOR;
  s.ordering = GSHSCENE_ORDER_DEPTH;
  uint64_t sid = add(e.edit, GSHSCENE_ROOT_ID, s);
  GSHSCENE_Node i = instance_node(sid);
  i.composite = GSHSCENE_COMPOSITE_XOR;
  i.ordering = GSHSCENE_ORDER_DEPTH;
  uint64_t iid = add(e.edit, GSHSCENE_ROOT_ID, i);
  VersionRef v = d.publish_edit(e);
  for (uint64_t id : {sid, iid}) {
    GSHSCENE_Node_Info info = info_of(v.get(), id);
    EXPECT_EQ(info.node.composite, (uint32_t)GSHSCENE_COMPOSITE_SRC_OVER) << id;
    EXPECT_EQ(info.node.ordering, (uint32_t)GSHSCENE_ORDER_FLAT) << id;
    EXPECT_EQ(gshscene_color_space_is_target(&info.node.blend_space), 1) << id;
  }
}

TEST(PaintNode, EveryInvalidFieldIsRefusedAndTheEditIsAsItWas) {
  Doc d;
  EditHolder e(d.s);
  size_t before = gshscene_edit_node_count(e.edit);
  const GSHSCENE_Result INV = GSHSCENE_ERR_INVALID, UNS = GSHSCENE_ERR_UNSUPPORTED;
  auto add_group = [&](const GSHSCENE_Node & n) { return gshscene_edit_add(e.edit, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, &n, nullptr, nullptr); };
  GSHSCENE_Node g = group_node();
  g.ordering = GSHSCENE_ORDERING_COUNT;
  EXPECT_EQ(add_group(g), INV);
  g = group_node();
  g.composite = GSHSCENE_COMPOSITE_COUNT;
  EXPECT_EQ(add_group(g), INV);
  g = group_node();
  g.blend_space = space_of(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_UNKNOWN);
  EXPECT_EQ(add_group(g), INV);
  g.blend_space = space_of(GCOL_PRIMARIES_PROPHOTO, GCOL_TRANSFER_SRGB);
  EXPECT_EQ(add_group(g), UNS);
  for (double bad : {kNaN, kInf, -kInf}) {
    g = group_node();
    g.depth = bad;
    EXPECT_EQ(add_group(g), INV) << bad;
    GSHSCENE_Node s = d.shape_at(0, 0);
    s.depth = bad;
    EXPECT_EQ(add_group(s), INV) << bad;
    GSHSCENE_Node i = instance_node(1);
    i.depth = bad;
    EXPECT_EQ(add_group(i), INV) << bad;
  }
  // A foreground paint on a shape follows the list's rules.
  GSHSCENE_Node s = d.foreground_shape(0, 0);
  s.paint.rgba[1] = 0.5;
  EXPECT_EQ(add_group(s), INV);
  s = d.foreground_shape(0, 0);
  s.paint.space = srgb_space();
  EXPECT_EQ(add_group(s), INV);
  s = d.foreground_shape(0, 0);
  s.paint.kind = GSHSCENE_PAINT_PALETTE;
  s.paint.entry = 0xFFFFu;
  EXPECT_EQ(add_group(s), INV) << "0xFFFF is never a palette entry (N14)";
  s = d.foreground_shape(0, 0);
  s.paint.kind = GSHSCENE_PAINT_GRADIENT;
  EXPECT_EQ(add_group(s), INV) << "a gradient paint names a gradient resource";
  EXPECT_EQ(gshscene_edit_node_count(e.edit), before) << "nothing was added";
}

TEST(PaintNode, NegativeZeroDepthIsStoredAsZero) {
  Doc d;
  EditHolder e(d.s);
  GSHSCENE_Node a = d.shape_at(0, 0);
  a.depth = -0.0;
  uint64_t ida = add(e.edit, GSHSCENE_ROOT_ID, a);
  VersionRef v1 = d.publish_edit(e);
  EXPECT_FALSE(std::signbit(info_of(v1.get(), ida).node.depth));
  EditHolder f(d.s);
  GSHSCENE_Node same = info_of(v1.get(), ida).node;
  same.depth = 0.0;
  ASSERT_EQ(gshscene_edit_set(f.edit, ida, &same), GSHSCENE_OK);
  VersionRef v2;
  const GSHSCENE_Version * out = nullptr;
  ASSERT_EQ(gshscene_scene_publish(d.s.scene, f.edit, nullptr, &out), GSHSCENE_OK);
  v2.reset(out);
  EXPECT_EQ(v2.get(), v1.get()) << "setting what is there publishes nothing";
}

// --- the foreground, per version -------------------------------------------------------------------------------------

TEST(Foreground, AVersionHasTheDefaultUntilAnEditSetsIt) {
  Doc d;
  VersionRef v = d.current();
  GSHSCENE_Color got;
  ASSERT_EQ(gshscene_version_foreground(v.get(), &got), GSHSCENE_OK);
  GSHSCENE_Color black;
  gshscene_color_init(&black);
  EXPECT_EQ(std::memcmp(&got, &black, sizeof got), 0);
  EXPECT_EQ(gshscene_version_foreground(nullptr, &got), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_version_foreground(v.get(), nullptr), GSHSCENE_ERR_INVALID);
}

TEST(Foreground, AnEditSetsItReadsItBackAndPublishesAVersionOfItsOwn) {
  Doc d;
  VersionRef v0 = d.current();
  EditHolder e(d.s);
  GSHSCENE_Color got;
  ASSERT_EQ(gshscene_edit_foreground(e.edit, &got), GSHSCENE_OK);
  EXPECT_EQ(got.rgba[3], 1.0) << "the view starts as the base";
  GSHSCENE_Color fg = color(0.25, 0.5, 0.75, 0.5, srgb_space());
  ASSERT_EQ(gshscene_edit_set_foreground(e.edit, &fg), GSHSCENE_OK);
  ASSERT_EQ(gshscene_edit_foreground(e.edit, &got), GSHSCENE_OK);
  EXPECT_EQ(std::memcmp(&got, &fg, sizeof fg), 0) << "the edit's view shows its own change";
  VersionRef v1 = d.publish_edit(e);
  EXPECT_NE(v1.get(), v0.get()) << "a change of nothing but the foreground is a new version";
  EXPECT_EQ(gshscene_version_serial(v1.get()), 1u);
  ASSERT_EQ(gshscene_version_foreground(v1.get(), &got), GSHSCENE_OK);
  EXPECT_EQ(std::memcmp(&got, &fg, sizeof fg), 0);
  ASSERT_EQ(gshscene_version_foreground(v0.get(), &got), GSHSCENE_OK);
  EXPECT_EQ(got.rgba[0], 0.0) << "the version it was opened on is unchanged";
  EXPECT_NE(gshscene_version_hash(v0.get()), gshscene_version_hash(v1.get()));
  EXPECT_NE(dump_version(v0.get()), dump_version(v1.get()));
  // Nodes are shared; nothing but the parameter differs.
  EXPECT_EQ(gshscene_version_node_count(v0.get()), gshscene_version_node_count(v1.get()));
  EXPECT_TRUE(diff_of(v0.get(), v1.get()).changes.empty());
}

TEST(Foreground, SettingWhatTheBaseHasChangesNothing) {
  Doc d;
  EditHolder e(d.s);
  GSHSCENE_Color fg = color(0.25, 0.5, 0.75, 0.5);
  ASSERT_EQ(gshscene_edit_set_foreground(e.edit, &fg), GSHSCENE_OK);
  VersionRef v1 = d.publish_edit(e);
  EditHolder f(d.s);
  ASSERT_EQ(gshscene_edit_set_foreground(f.edit, &fg), GSHSCENE_OK);
  const GSHSCENE_Version * out = nullptr;
  ASSERT_EQ(gshscene_scene_publish(d.s.scene, f.edit, nullptr, &out), GSHSCENE_OK);
  VersionRef again(out);
  EXPECT_EQ(again.get(), v1.get()) << "an identical foreground publishes no version";
  // The default set explicitly on the default is the same.
  EditHolder g(d.s);
  GSHSCENE_Color black;
  gshscene_color_init(&black);
  ASSERT_EQ(gshscene_edit_set_foreground(g.edit, &black), GSHSCENE_OK);
  ASSERT_EQ(gshscene_edit_set_foreground(g.edit, &fg), GSHSCENE_OK);
  out = nullptr;
  ASSERT_EQ(gshscene_scene_publish(d.s.scene, g.edit, nullptr, &out), GSHSCENE_OK);
  VersionRef third(out);
  EXPECT_EQ(third.get(), v1.get());
}

TEST(Foreground, ARefusedColorChangesNothingAndASpentEditRefuses) {
  Doc d;
  EditHolder e(d.s);
  GSHSCENE_Color fg = color(0.25, 0.5, 0.75, 0.5);
  ASSERT_EQ(gshscene_edit_set_foreground(e.edit, &fg), GSHSCENE_OK);
  GSHSCENE_Color bad = color(0, 0, 0, 2.0);
  EXPECT_EQ(gshscene_edit_set_foreground(e.edit, &bad), GSHSCENE_ERR_INVALID);
  bad = color(kNaN, 0, 0, 1);
  EXPECT_EQ(gshscene_edit_set_foreground(e.edit, &bad), GSHSCENE_ERR_INVALID);
  bad = color(0, 0, 0, 1, space_of(GCOL_PRIMARIES_PROPHOTO, GCOL_TRANSFER_SRGB));
  EXPECT_EQ(gshscene_edit_set_foreground(e.edit, &bad), GSHSCENE_ERR_UNSUPPORTED);
  EXPECT_EQ(gshscene_edit_set_foreground(e.edit, nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_edit_set_foreground(nullptr, &fg), GSHSCENE_ERR_INVALID);
  GSHSCENE_Color got;
  ASSERT_EQ(gshscene_edit_foreground(e.edit, &got), GSHSCENE_OK);
  EXPECT_EQ(std::memcmp(&got, &fg, sizeof fg), 0) << "a refused call changes nothing";
  EXPECT_EQ(gshscene_edit_foreground(nullptr, &got), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_edit_foreground(e.edit, nullptr), GSHSCENE_ERR_INVALID);
  VersionRef v = d.publish_edit(e);
  EXPECT_EQ(gshscene_edit_set_foreground(e.edit, &fg), GSHSCENE_ERR_INVALID) << "a published edit is spent";
}

TEST(Foreground, TwoEditsThatBothSetItAreRefusedNamingTheRoot) {
  Doc d;
  VersionRef base = d.current();
  EditHolder a(d.s, base.get()), b(d.s, base.get());
  GSHSCENE_Color red = color(1, 0, 0, 1), blue = color(0, 0, 1, 1);
  ASSERT_EQ(gshscene_edit_set_foreground(a.edit, &red), GSHSCENE_OK);
  ASSERT_EQ(gshscene_edit_set_foreground(b.edit, &blue), GSHSCENE_OK);
  GSHSCENE_Edit * both[2] = {a.edit, b.edit};
  ProblemsRef problems;
  const GSHSCENE_Version * out = nullptr;
  EXPECT_EQ(gshscene_scene_merge(d.s.scene, both, 2, &problems.problems, &out), GSHSCENE_ERR_REFUSED);
  EXPECT_EQ(out, nullptr);
  ASSERT_EQ(gshscene_problems_count(problems.problems), 1u);
  uint64_t id;
  GSHSCENE_Problem_Reason reason;
  ASSERT_EQ(gshscene_problems_get(problems.problems, 0, &id, &reason), GSHSCENE_OK);
  EXPECT_EQ(id, GSHSCENE_ROOT_ID);
  EXPECT_EQ(reason, GSHSCENE_PROBLEM_PARAMETER);
  EXPECT_STREQ(gshscene_problem_reason_string(reason), "parameter");
  EXPECT_EQ(gshscene_version_serial(d.current().get()), 0u) << "nothing was published";
  GSHSCENE_Color got;
  ASSERT_EQ(gshscene_edit_foreground(a.edit, &got), GSHSCENE_OK);
  EXPECT_EQ(got.rgba[0], 1.0) << "the edits are as they were";
}

TEST(Foreground, OneEditSettingItAndAnotherAddingAShapeMerge) {
  GSHSCENE_Color red = color(1, 0, 0, 1);
  uint64_t hashes[2] = {0, 0};
  for (int order = 0; order < 2; order++) {
    Doc d;  // each order in a scene of its own: the result must be the same
    uint64_t group;
    {
      EditHolder e(d.s);
      group = add(e.edit, GSHSCENE_ROOT_ID, group_node());
      d.publish_edit(e);
    }
    VersionRef base = d.current();
    EditHolder x(d.s, base.get()), y(d.s, base.get());
    ASSERT_EQ(gshscene_edit_set_foreground(x.edit, &red), GSHSCENE_OK);
    add(y.edit, group, d.foreground_shape(0, 0));
    GSHSCENE_Edit * edits[2] = {order ? y.edit : x.edit, order ? x.edit : y.edit};
    const GSHSCENE_Version * out = nullptr;
    ASSERT_EQ(gshscene_scene_merge(d.s.scene, edits, 2, nullptr, &out), GSHSCENE_OK) << order;
    VersionRef v(out);
    GSHSCENE_Color got;
    ASSERT_EQ(gshscene_version_foreground(v.get(), &got), GSHSCENE_OK);
    EXPECT_EQ(got.rgba[0], 1.0);
    EXPECT_EQ(gshscene_version_node_count(v.get()), 3u);
    hashes[order] = gshscene_version_hash(v.get());
  }
  EXPECT_EQ(hashes[0], hashes[1]) << "the order the edits were given in does not matter";
}

TEST(Foreground, AnEditOnAnOlderBaseConflictsWithAPublishThatSetItAndOnlyThen) {
  Doc d;
  uint64_t g1, g2;
  {
    EditHolder e(d.s);
    g1 = add(e.edit, GSHSCENE_ROOT_ID, group_node());
    g2 = add(e.edit, GSHSCENE_ROOT_ID, group_node());
    d.publish_edit(e);
  }
  VersionRef base = d.current();
  EditHolder late(d.s, base.get()), later(d.s, base.get()), same(d.s, base.get());
  // Someone publishes a foreground on top of the base.
  {
    EditHolder first(d.s, base.get());
    GSHSCENE_Color red = color(1, 0, 0, 1);
    ASSERT_EQ(gshscene_edit_set_foreground(first.edit, &red), GSHSCENE_OK);
    d.publish_edit(first);
  }
  // An edit of the old base that sets it too: refused, the root named.
  GSHSCENE_Color blue = color(0, 0, 1, 1);
  ASSERT_EQ(gshscene_edit_set_foreground(late.edit, &blue), GSHSCENE_OK);
  ProblemsRef problems;
  EXPECT_EQ(gshscene_scene_publish(d.s.scene, late.edit, &problems.problems, nullptr), GSHSCENE_ERR_REFUSED);
  EXPECT_EQ(problems.ids(GSHSCENE_PROBLEM_PARAMETER), (std::vector<uint64_t>{GSHSCENE_ROOT_ID}));
  // An edit of the old base that only adds a shape (under a group nobody touched) publishes, and keeps the foreground
  // the publish set.
  add(later.edit, g1, d.foreground_shape(0, 0));
  const GSHSCENE_Version * out = nullptr;
  ASSERT_EQ(gshscene_scene_publish(d.s.scene, later.edit, nullptr, &out), GSHSCENE_OK);
  VersionRef v(out);
  GSHSCENE_Color got;
  ASSERT_EQ(gshscene_version_foreground(v.get(), &got), GSHSCENE_OK);
  EXPECT_EQ(got.rgba[0], 1.0) << "the foreground of the publish since the base is kept";
  // An edit of the old base that sets it to what its own base had changed nothing, so it conflicts with nothing.
  GSHSCENE_Color black;
  gshscene_color_init(&black);
  ASSERT_EQ(gshscene_edit_set_foreground(same.edit, &black), GSHSCENE_OK);
  add(same.edit, g2, d.foreground_shape(5, 5));
  out = nullptr;
  ASSERT_EQ(gshscene_scene_publish(d.s.scene, same.edit, nullptr, &out), GSHSCENE_OK);
  VersionRef w(out);
  ASSERT_EQ(gshscene_version_foreground(w.get(), &got), GSHSCENE_OK);
  EXPECT_EQ(got.rgba[0], 1.0);
}

// --- diff and damage -------------------------------------------------------------------------------------------------

TEST(ForegroundDamage, AChangedForegroundDamagesExactlyTheShapesThatPaintWithIt) {
  Doc d;
  VersionRef v1;
  {
    EditHolder e(d.s);
    add(e.edit, GSHSCENE_ROOT_ID, d.shape_at(0, 0));            // solid: not damaged
    add(e.edit, GSHSCENE_ROOT_ID, d.foreground_shape(20, 0));   // currentColor
    add(e.edit, GSHSCENE_ROOT_ID, d.foreground_shape(40, 10));  // currentColor
    add(e.edit, GSHSCENE_ROOT_ID, d.shape_at(100, 100));        // solid
    v1 = d.publish_edit(e);
  }
  VersionRef v2;
  {
    EditHolder e(d.s);
    GSHSCENE_Color red = color(1, 0, 0, 1);
    ASSERT_EQ(gshscene_edit_set_foreground(e.edit, &red), GSHSCENE_OK);
    v2 = d.publish_edit(e);
  }
  Diffed r = diff_of(v1.get(), v2.get());
  EXPECT_EQ(r.foreground_changed, 1);
  EXPECT_TRUE(r.changes.empty()) << "no node differs";
  // Shapes at (20, 0) and (40, 10), 4 x 4, outset by the AA support of 1.
  expect_rect(r.damage, 19, -1, 45, 15);
  // The other way, and against itself.
  Diffed back = diff_of(v2.get(), v1.get());
  EXPECT_EQ(back.foreground_changed, 1);
  expect_rect(back.damage, 19, -1, 45, 15);
  Diffed same = diff_of(v2.get(), v2.get());
  EXPECT_EQ(same.foreground_changed, 0);
  EXPECT_EQ(same.damage.empty, 1);
}

TEST(ForegroundDamage, ASceneWithNoShapeThatPaintsWithItHasNoDamageWhenItChanges) {
  Doc d;
  VersionRef v1;
  {
    EditHolder e(d.s);
    add(e.edit, GSHSCENE_ROOT_ID, d.shape_at(0, 0));
    v1 = d.publish_edit(e);
  }
  EditHolder e(d.s);
  GSHSCENE_Color red = color(1, 0, 0, 1);
  ASSERT_EQ(gshscene_edit_set_foreground(e.edit, &red), GSHSCENE_OK);
  VersionRef v2 = d.publish_edit(e);
  Diffed r = diff_of(v1.get(), v2.get());
  EXPECT_EQ(r.foreground_changed, 1);
  EXPECT_EQ(r.damage.empty, 1);
  EXPECT_EQ(r.damage.unbounded, 0);
}

TEST(ForegroundDamage, EveryInstanceOfASymbolThatPaintsWithItIsDamaged) {
  Doc d;
  VersionRef v1;
  {
    EditHolder e(d.s);
    GSHSCENE_Node def = group_node();
    def.flags = GSHSCENE_NODE_HIDDEN;
    uint64_t did = add(e.edit, GSHSCENE_ROOT_ID, def);
    add(e.edit, did, d.foreground_shape(0, 0));
    GSHSCENE_Node i1 = instance_node(did);
    i1.map = translate(10, 0);
    add(e.edit, GSHSCENE_ROOT_ID, i1);
    GSHSCENE_Node i2 = instance_node(did);
    i2.map = translate(50, 30);
    add(e.edit, GSHSCENE_ROOT_ID, i2);
    v1 = d.publish_edit(e);
  }
  EditHolder e(d.s);
  GSHSCENE_Color red = color(1, 0, 0, 1);
  ASSERT_EQ(gshscene_edit_set_foreground(e.edit, &red), GSHSCENE_OK);
  VersionRef v2 = d.publish_edit(e);
  Diffed r = diff_of(v1.get(), v2.get());
  expect_rect(r.damage, 9, -1, 55, 35);
}

TEST(OperatorDamage, AnOperatorThatReachesBeyondItsBoundsMakesTheDamageUnboundedOnlyWhereTheGroupIsTouched) {
  for (int op = 0; op < (int)GSHSCENE_COMPOSITE_COUNT; op++) {
    bool unbounded = gshscene_composite_is_unbounded((GSHSCENE_Composite)op) != 0;
    Doc d;
    uint64_t gid, sid;
    VersionRef v1;
    {
      EditHolder e(d.s);
      GSHSCENE_Node g = group_node();
      g.composite = (uint32_t)op;
      gid = add(e.edit, GSHSCENE_ROOT_ID, g);
      sid = add(e.edit, gid, d.shape_at(0, 0));
      add(e.edit, GSHSCENE_ROOT_ID, d.shape_at(100, 100));
      v1 = d.publish_edit(e);
    }
    // A change inside the group moves nothing outside the child's own bounds.
    VersionRef v2;
    {
      EditHolder e(d.s);
      GSHSCENE_Node s = d.shape_at(0, 0);
      s.paint = solid(1, 0, 0, 1);
      ASSERT_EQ(gshscene_edit_set(e.edit, sid, &s), GSHSCENE_OK);
      v2 = d.publish_edit(e);
    }
    Diffed inside = diff_of(v1.get(), v2.get());
    expect_rect(inside.damage, -1, -1, 5, 5, gshscene_composite_string((GSHSCENE_Composite)op));
    // A change to the group's own operator, from and to source-over, reaches the whole target if either is unbounded.
    VersionRef v3;
    {
      EditHolder e(d.s);
      GSHSCENE_Node g = info_of(v2.get(), gid).node;
      g.composite = GSHSCENE_COMPOSITE_SRC_OVER;
      ASSERT_EQ(gshscene_edit_set(e.edit, gid, &g), GSHSCENE_OK);
      const GSHSCENE_Version * out = nullptr;
      ASSERT_EQ(gshscene_scene_publish(d.s.scene, e.edit, nullptr, &out), GSHSCENE_OK);
      v3.reset(out);
    }
    if (op == GSHSCENE_COMPOSITE_SRC_OVER) {
      EXPECT_EQ(v3.get(), v2.get());
      continue;
    }
    Diffed own = diff_of(v2.get(), v3.get());
    EXPECT_EQ(own.damage.unbounded, unbounded ? 1 : 0) << gshscene_composite_string((GSHSCENE_Composite)op);
    if (!unbounded) {
      expect_rect(own.damage, -1, -1, 5, 5, gshscene_composite_string((GSHSCENE_Composite)op));
    }
    // Adding and removing such a group is the same.
    VersionRef v4;
    {
      EditHolder e(d.s);
      GSHSCENE_Node g = group_node();
      g.composite = (uint32_t)op;
      uint64_t extra = add(e.edit, GSHSCENE_ROOT_ID, g);
      add(e.edit, extra, d.shape_at(200, 0));
      v4 = d.publish_edit(e);
    }
    Diffed added = diff_of(v3.get(), v4.get());
    EXPECT_EQ(added.damage.unbounded, unbounded ? 1 : 0) << gshscene_composite_string((GSHSCENE_Composite)op);
    Diffed removed = diff_of(v4.get(), v3.get());
    EXPECT_EQ(removed.damage.unbounded, unbounded ? 1 : 0) << gshscene_composite_string((GSHSCENE_Composite)op);
  }
}

TEST(OperatorDamage, ADepthChangeDamagesTheMovedNodeAndNothingElse) {
  Doc d;
  uint64_t a, b;
  VersionRef v1;
  {
    EditHolder e(d.s);
    GSHSCENE_Node g = group_node();
    g.ordering = GSHSCENE_ORDER_DEPTH;
    uint64_t gid = add(e.edit, GSHSCENE_ROOT_ID, g);
    GSHSCENE_Node sa = d.shape_at(0, 0);
    sa.depth = 1.0;
    a = add(e.edit, gid, sa);
    GSHSCENE_Node sb = d.shape_at(2, 2);
    sb.depth = 2.0;
    b = add(e.edit, gid, sb);
    add(e.edit, GSHSCENE_ROOT_ID, d.shape_at(100, 100));
    v1 = d.publish_edit(e);
  }
  (void)b;
  EditHolder e(d.s);
  GSHSCENE_Node moved = info_of(v1.get(), a).node;
  moved.depth = 3.0;
  ASSERT_EQ(gshscene_edit_set(e.edit, a, &moved), GSHSCENE_OK);
  VersionRef v2 = d.publish_edit(e);
  Diffed r = diff_of(v1.get(), v2.get());
  EXPECT_EQ(r.changes.size(), 1u);
  expect_rect(r.damage, -1, -1, 5, 5) ;
  // The order did change: the list of the second paints a first.
  const GSHSCENE_List * l1 = nullptr;
  const GSHSCENE_List * l2 = nullptr;
  ASSERT_EQ(gshscene_version_list(v1.get(), &l1), GSHSCENE_OK);
  ASSERT_EQ(gshscene_version_list(v2.get(), &l2), GSHSCENE_OK);
  EXPECT_FALSE(gshscene_list_equal(l1, l2));
}

// --- limits ----------------------------------------------------------------------------------------------------------

TEST(PaintLimits, AGroupThatNeedsAPushCountsInMaxDepthWhateverItNeedsItFor) {
  GSHSCENE_Limits l;
  gshscene_limits_default(&l);
  l.max_depth = 3;
  for (int kind = 0; kind < 4; kind++) {
    for (int count = 3; count <= 4; count++) {
      Doc d(&l);
      EditHolder e(d.s);
      uint64_t parent = GSHSCENE_ROOT_ID;
      for (int i = 0; i < count; i++) {
        GSHSCENE_Node g = group_node();
        if (kind == 0) {
          g.composite = GSHSCENE_COMPOSITE_PLUS;
        } else if (kind == 1) {
          g.blend_space = linear_srgb_space();
        } else if (kind == 2) {
          g.ordering = GSHSCENE_ORDER_OVERLAY;
        } else {
          g.opacity = 0.5;
        }
        parent = add(e.edit, parent, g);
      }
      add(e.edit, parent, d.shape_at(0, 0));
      const GSHSCENE_Version * out = nullptr;
      GSHSCENE_Result r = gshscene_scene_publish(d.s.scene, e.edit, nullptr, &out);
      EXPECT_EQ(r, count == 3 ? GSHSCENE_OK : GSHSCENE_ERR_LIMIT) << "kind " << kind << ", " << count << " nested";
      gshscene_version_release(out);
    }
  }
}

TEST(PaintLimits, AnOrderedGroupAndItsContainerAreTwoPushesNotOne) {
  for (size_t limit = 1; limit <= 2; limit++) {
    GSHSCENE_Limits l;
    gshscene_limits_default(&l);
    l.max_depth = limit;
    Doc d(&l);
    EditHolder e(d.s);
    GSHSCENE_Node ordered = group_node();
    ordered.ordering = GSHSCENE_ORDER_DEPTH;
    uint64_t g = add(e.edit, GSHSCENE_ROOT_ID, ordered);
    uint64_t box = add(e.edit, g, group_node());
    add(e.edit, box, d.shape_at(0, 0));
    const GSHSCENE_Version * out = nullptr;
    EXPECT_EQ(gshscene_scene_publish(d.s.scene, e.edit, nullptr, &out), limit == 2 ? GSHSCENE_OK : GSHSCENE_ERR_LIMIT) << limit;
    gshscene_version_release(out);
  }
}

TEST(PaintLimits, AContainerInAnOrderedGroupCountsAsAPushBecauseItIsOneUnit) {
  GSHSCENE_Limits l;
  gshscene_limits_default(&l);
  l.max_depth = 2;
  for (int nested = 1; nested <= 2; nested++) {
    Doc d(&l);
    EditHolder e(d.s);
    GSHSCENE_Node ordered = group_node();
    ordered.ordering = GSHSCENE_ORDER_DEPTH;
    uint64_t parent = add(e.edit, GSHSCENE_ROOT_ID, ordered);  // one push
    for (int i = 0; i < nested; i++) {
      parent = add(e.edit, parent, group_node());              // plain containers: a push each, in an ordered group
    }
    add(e.edit, parent, d.shape_at(0, 0));
    const GSHSCENE_Version * out = nullptr;
    GSHSCENE_Result r = gshscene_scene_publish(d.s.scene, e.edit, nullptr, &out);
    // The first container is a unit of the ordered group (a push); the second is in a flat sequence (none).  With a limit
    // of 2 the pushes are the ordered group and the first container: both fit.
    EXPECT_EQ(r, GSHSCENE_OK) << nested;
    gshscene_version_release(out);
  }
  // Three levels of ordered groups need three pushes.
  Doc d(&l);
  EditHolder e(d.s);
  GSHSCENE_Node ordered = group_node();
  ordered.ordering = GSHSCENE_ORDER_OVERLAY;
  uint64_t g1 = add(e.edit, GSHSCENE_ROOT_ID, ordered);
  uint64_t g2 = add(e.edit, g1, ordered);
  uint64_t g3 = add(e.edit, g2, ordered);
  add(e.edit, g3, d.shape_at(0, 0));
  const GSHSCENE_Version * out = nullptr;
  EXPECT_EQ(gshscene_scene_publish(d.s.scene, e.edit, nullptr, &out), GSHSCENE_ERR_LIMIT);
  gshscene_version_release(out);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
