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
 * Every structural limit of a retained scene, asserted by its bound: exactly N
 * publishes and N + 1 is ERR_LIMIT, the edit and the scene as they were
 * (scene.h, AD-11, AD-19; story 11).
 */

#include "scene_fixtures.h"

#include <cstdint>
#include <limits>

using namespace scene;

namespace {

using Ids = std::vector<uint64_t>;

GSHSCENE_Limits limits_with() {
  GSHSCENE_Limits l;
  gshscene_limits_default(&l);
  return l;
}

struct Fx {
  SceneHolder s;
  Ref sq;
  explicit Fx(const GSHSCENE_Limits & limits) : s(&limits), sq(intern_square(s.store())) {}
  GSHSCENE_Node hidden_group(double opacity = 1.0) {
    GSHSCENE_Node n = group_node(opacity);
    n.flags = GSHSCENE_NODE_HIDDEN;
    return n;
  }
  /** Publish; returns the result and checks that a failure left the scene as it was. */
  GSHSCENE_Result publish_result(GSHSCENE_Edit * e) {
    VersionRef before;
    EXPECT_EQ(gshscene_scene_current(s.scene, &before.version), GSHSCENE_OK);
    std::string dump_before = dump_version(before.get());
    const GSHSCENE_Version * out = nullptr;
    GSHSCENE_Result r = gshscene_scene_publish(s.scene, e, nullptr, &out);
    if (r == GSHSCENE_OK) {
      gshscene_version_release(out);
    } else {
      EXPECT_EQ(out, nullptr);
      VersionRef after;
      EXPECT_EQ(gshscene_scene_current(s.scene, &after.version), GSHSCENE_OK);
      EXPECT_EQ(after.get(), before.get()) << "a refused publish changed the current version";
      EXPECT_EQ(dump_version(after.get()), dump_before);
    }
    return r;
  }
};

}  // namespace

TEST(SceneLimits, MaxNodesPublishesExactlyTheLimitAndAnEditCannotGrowPastIt) {
  GSHSCENE_Limits l = limits_with();
  l.max_nodes = 6;
  Fx f(l);
  EditHolder e(f.s);
  for (int i = 0; i < 5; i++) {
    add(e.edit, GSHSCENE_ROOT_ID, shape_node(f.sq.get()));
  }
  EXPECT_EQ(f.publish_result(e.edit), GSHSCENE_OK) << "the root and five: six nodes";
  EditHolder more(f.s);
  GSHSCENE_Node n = shape_node(f.sq.get());
  uint64_t id = 0;
  EXPECT_EQ(gshscene_edit_add(more.edit, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, &n, nullptr, &id), GSHSCENE_ERR_LIMIT) << "a seventh";
}

TEST(SceneLimits, MaxNodesRefusesASiblingMergeThatPassesTheLimit) {
  GSHSCENE_Limits l = limits_with();
  l.max_nodes = 6;
  Fx g(l);
  uint64_t ga, gb;
  {
    EditHolder e(g.s);
    ga = add(e.edit, GSHSCENE_ROOT_ID, group_node());
    gb = add(e.edit, GSHSCENE_ROOT_ID, group_node());
    publish(g.s, e);
  }
  EditHolder ea(g.s), eb(g.s);
  add(ea.edit, ga, shape_node(g.sq.get()));
  add(ea.edit, ga, shape_node(g.sq.get()));
  add(eb.edit, gb, shape_node(g.sq.get()));
  add(eb.edit, gb, shape_node(g.sq.get()));
  GSHSCENE_Edit * both[2] = {ea.edit, eb.edit};
  VersionRef before;
  ASSERT_EQ(gshscene_scene_current(g.s.scene, &before.version), GSHSCENE_OK);
  const GSHSCENE_Version * out = nullptr;
  EXPECT_EQ(gshscene_scene_merge(g.s.scene, both, 2, nullptr, &out), GSHSCENE_ERR_LIMIT) << "3 + 2 + 2 = 7, each edit alone is 5";
  EXPECT_EQ(out, nullptr);
  VersionRef after;
  ASSERT_EQ(gshscene_scene_current(g.s.scene, &after.version), GSHSCENE_OK);
  EXPECT_EQ(after.get(), before.get());
  VersionRef v;
  EXPECT_EQ(gshscene_scene_publish(g.s.scene, ea.edit, nullptr, &v.version), GSHSCENE_OK) << "the edits are as they were";
}

TEST(SceneLimits, MaxNodesMergeAtExactlyTheLimitPublishes) {
  GSHSCENE_Limits l = limits_with();
  l.max_nodes = 7;
  Fx g(l);
  uint64_t ga, gb;
  {
    EditHolder e(g.s);
    ga = add(e.edit, GSHSCENE_ROOT_ID, group_node());
    gb = add(e.edit, GSHSCENE_ROOT_ID, group_node());
    publish(g.s, e);
  }
  EditHolder ea(g.s), eb(g.s);
  add(ea.edit, ga, shape_node(g.sq.get()));
  add(ea.edit, ga, shape_node(g.sq.get()));
  add(eb.edit, gb, shape_node(g.sq.get()));
  add(eb.edit, gb, shape_node(g.sq.get()));
  GSHSCENE_Edit * both[2] = {ea.edit, eb.edit};
  VersionRef v;
  EXPECT_EQ(gshscene_scene_merge(g.s.scene, both, 2, nullptr, &v.version), GSHSCENE_OK) << "3 + 2 + 2 = 7";
  EXPECT_EQ(gshscene_version_node_count(v.get()), 7u);
}

TEST(SceneLimits, MaxNodeDepthPublishesExactlyTheLimitAndRefusesOneMore) {
  GSHSCENE_Limits l = limits_with();
  l.max_node_depth = 5;
  Fx f(l);
  EditHolder e(f.s);
  uint64_t g1 = add(e.edit, GSHSCENE_ROOT_ID, group_node());
  uint64_t g2 = add(e.edit, g1, group_node());
  uint64_t g3 = add(e.edit, g2, group_node());
  add(e.edit, g3, shape_node(f.sq.get()));  // root, g1, g2, g3, shape: 5 levels
  EXPECT_EQ(f.publish_result(e.edit), GSHSCENE_OK);
  EditHolder deeper(f.s);
  uint64_t d1 = add(deeper.edit, GSHSCENE_ROOT_ID, group_node());
  uint64_t d2 = add(deeper.edit, d1, group_node());
  uint64_t d3 = add(deeper.edit, d2, group_node());
  uint64_t d4 = add(deeper.edit, d3, group_node());
  add(deeper.edit, d4, shape_node(f.sq.get()));  // 6 levels
  EXPECT_EQ(f.publish_result(deeper.edit), GSHSCENE_ERR_LIMIT);
  ASSERT_EQ(gshscene_edit_remove(deeper.edit, d4), GSHSCENE_OK);
  EXPECT_EQ(f.publish_result(deeper.edit), GSHSCENE_OK) << "the edit was as it was; cut back to 5 levels it publishes";
}

TEST(SceneLimits, AnInstancesTargetCountsAsItsChildInTheDepth) {
  GSHSCENE_Limits l = limits_with();
  l.max_node_depth = 4;
  Fx f(l);
  {
    EditHolder e(f.s);
    uint64_t d = add(e.edit, GSHSCENE_ROOT_ID, f.hidden_group());
    add(e.edit, d, shape_node(f.sq.get()));  // D: 2 levels
    add(e.edit, GSHSCENE_ROOT_ID, instance_node(d));  // I: 3, root: 4
    EXPECT_EQ(f.publish_result(e.edit), GSHSCENE_OK);
  }
  Fx g(l);
  EditHolder e(g.s);
  uint64_t d = add(e.edit, GSHSCENE_ROOT_ID, g.hidden_group());
  uint64_t inner = add(e.edit, d, group_node());
  add(e.edit, inner, shape_node(g.sq.get()));  // D: 3 levels
  add(e.edit, GSHSCENE_ROOT_ID, instance_node(d));  // I: 4, root: 5
  EXPECT_EQ(g.publish_result(e.edit), GSHSCENE_ERR_LIMIT);
}

TEST(SceneLimits, TheDepthCeilingIsEnforcedWhateverTheLimitSays) {
  GSHSCENE_Limits l = limits_with();
  l.max_node_depth = SIZE_MAX;
  Fx f(l);
  EditHolder e(f.s);
  uint64_t parent = GSHSCENE_ROOT_ID;
  for (size_t level = 2; level <= GSHSCENE_NODE_DEPTH_CEILING; level++) {
    parent = add(e.edit, parent, group_node());
  }
  ASSERT_NE(parent, 0u);  // root and 1023 groups: 1024 levels
  EXPECT_EQ(f.publish_result(e.edit), GSHSCENE_OK);
  EditHolder deeper(f.s);
  uint64_t p = GSHSCENE_ROOT_ID;
  for (size_t level = 2; level <= GSHSCENE_NODE_DEPTH_CEILING + 1; level++) {
    p = add(deeper.edit, p, group_node());
  }
  EXPECT_EQ(f.publish_result(deeper.edit), GSHSCENE_ERR_LIMIT) << "1025 levels";
}

TEST(SceneLimits, MaxExpansionCountsOccurrencesAndIsExactForPlainNodes) {
  GSHSCENE_Limits l = limits_with();
  l.max_expansion = 6;
  Fx f(l);
  {
    EditHolder e(f.s);
    for (int i = 0; i < 5; i++) {
      add(e.edit, GSHSCENE_ROOT_ID, shape_node(f.sq.get()));
    }
    EXPECT_EQ(f.publish_result(e.edit), GSHSCENE_OK) << "the root and five shapes: six occurrences";
  }
  Fx g(l);
  EditHolder e(g.s);
  for (int i = 0; i < 6; i++) {
    add(e.edit, GSHSCENE_ROOT_ID, shape_node(g.sq.get()));
  }
  EXPECT_EQ(g.publish_result(e.edit), GSHSCENE_ERR_LIMIT) << "seven";
}

namespace {

/** A bomb: D0 holds one shape; each next definition holds ten instances of the one before; the root draws the last. */
void bomb(Fx & f, GSHSCENE_Edit * e, int levels) {
  uint64_t prev = add(e, GSHSCENE_ROOT_ID, f.hidden_group());
  add(e, prev, shape_node(f.sq.get()));
  for (int level = 1; level < levels; level++) {
    uint64_t d = add(e, GSHSCENE_ROOT_ID, f.hidden_group());
    for (int i = 0; i < 10; i++) {
      add(e, d, instance_node(prev));
    }
    prev = d;
  }
  add(e, GSHSCENE_ROOT_ID, instance_node(prev));
}

}  // namespace

TEST(SceneLimits, AnInstanceBombIsRefusedAtTheLevelThatPassesTheBound) {
  // occ(D0) = 2, occ(D1) = 1 + 10 * 3 = 31, occ(D2) = 1 + 10 * 32 = 321, occ(D3) = 3221; the root adds itself and the instance.
  GSHSCENE_Limits l = limits_with();
  l.max_expansion = 323;
  {
    Fx f(l);
    EditHolder e(f.s);
    bomb(f, e.edit, 3);  // root + instance + D2 = 1 + 1 + 321 = 323
    EXPECT_EQ(f.publish_result(e.edit), GSHSCENE_OK) << "exactly the limit";
  }
  l.max_expansion = 322;
  {
    Fx f(l);
    EditHolder e(f.s);
    bomb(f, e.edit, 3);
    EXPECT_EQ(f.publish_result(e.edit), GSHSCENE_ERR_LIMIT) << "one under";
  }
  l.max_expansion = 323;
  {
    Fx f(l);
    EditHolder e(f.s);
    bomb(f, e.edit, 4);
    EXPECT_EQ(f.publish_result(e.edit), GSHSCENE_ERR_LIMIT) << "the next level: 3221";
  }
  // The default bound refuses a deep bomb (ten levels of ten: 10^10 occurrences) without expanding it.
  {
    GSHSCENE_Limits d = limits_with();
    d.max_picture_depth = 100;
    d.max_node_depth = 100;
    Fx f(d);
    EditHolder e(f.s);
    bomb(f, e.edit, 12);
    EXPECT_EQ(f.publish_result(e.edit), GSHSCENE_ERR_LIMIT);
  }
}

TEST(SceneLimits, MaxListItemsBoundsTheVersionsListAndEveryPicture) {
  GSHSCENE_Limits l = limits_with();
  l.max_list_items = 4;
  {
    Fx f(l);
    EditHolder e(f.s);
    for (int i = 0; i < 4; i++) {
      add(e.edit, GSHSCENE_ROOT_ID, shape_node(f.sq.get()));
    }
    EXPECT_EQ(f.publish_result(e.edit), GSHSCENE_OK);
  }
  {
    Fx f(l);
    EditHolder e(f.s);
    for (int i = 0; i < 5; i++) {
      add(e.edit, GSHSCENE_ROOT_ID, shape_node(f.sq.get()));
    }
    EXPECT_EQ(f.publish_result(e.edit), GSHSCENE_ERR_LIMIT);
  }
  // An instanced node's picture is a list too.
  {
    Fx f(l);
    EditHolder e(f.s);
    uint64_t d = add(e.edit, GSHSCENE_ROOT_ID, f.hidden_group());
    for (int i = 0; i < 4; i++) {
      add(e.edit, d, shape_node(f.sq.get()));
    }
    add(e.edit, GSHSCENE_ROOT_ID, instance_node(d));
    EXPECT_EQ(f.publish_result(e.edit), GSHSCENE_OK) << "four items in the picture";
  }
  {
    Fx f(l);
    EditHolder e(f.s);
    uint64_t d = add(e.edit, GSHSCENE_ROOT_ID, f.hidden_group());
    for (int i = 0; i < 5; i++) {
      add(e.edit, d, shape_node(f.sq.get()));
    }
    add(e.edit, GSHSCENE_ROOT_ID, instance_node(d));
    EXPECT_EQ(f.publish_result(e.edit), GSHSCENE_ERR_LIMIT) << "five in the picture";
  }
  // Hidden nodes that nothing instances are not in any list.
  {
    Fx f(l);
    EditHolder e(f.s);
    uint64_t d = add(e.edit, GSHSCENE_ROOT_ID, f.hidden_group());
    for (int i = 0; i < 9; i++) {
      add(e.edit, d, shape_node(f.sq.get()));
    }
    EXPECT_EQ(f.publish_result(e.edit), GSHSCENE_OK);
  }
}

TEST(SceneLimits, MaxResourceElementsBoundsAPicturesItemsAndNotTheVersionsOwnList) {
  GSHSCENE_Limits l = limits_with();
  l.max_resource_elements = 3;
  // A path of two elements fits the limit (the square of the other tests has five).
  auto tiny = [](GSHSCENE_Store * store) {
    GSHGEOM_Path * p = new_path();
    gshgeom_path_move_to(p, P(0, 0));
    gshgeom_path_line_to(p, P(4, 4));
    const GSHSCENE_Resource * r = intern(store, p);
    gshgeom_path_destroy(p);
    return r;
  };
  {
    SceneHolder s(&l);
    Ref t(tiny(s.store()));
    ASSERT_NE(t.get(), nullptr);
    EditHolder e(s);
    for (int i = 0; i < 20; i++) {
      add(e.edit, GSHSCENE_ROOT_ID, shape_node(t.get()));
    }
    VersionRef v;
    publish(s, e, &v);  // the version's own list is bounded by max_list_items
  }
  for (int count : {3, 4}) {
    SceneHolder s(&l);
    Ref t(tiny(s.store()));
    EditHolder e(s);
    GSHSCENE_Node hidden = group_node();
    hidden.flags = GSHSCENE_NODE_HIDDEN;
    uint64_t d = add(e.edit, GSHSCENE_ROOT_ID, hidden);
    for (int i = 0; i < count; i++) {
      add(e.edit, d, shape_node(t.get()));
    }
    add(e.edit, GSHSCENE_ROOT_ID, instance_node(d));
    const GSHSCENE_Version * out = nullptr;
    GSHSCENE_Result r = gshscene_scene_publish(s.scene, e.edit, nullptr, &out);
    EXPECT_EQ(r, count == 3 ? GSHSCENE_OK : GSHSCENE_ERR_LIMIT) << count << " items in the picture";
    gshscene_version_release(out);
  }
}

TEST(SceneLimits, MaxDepthBoundsThePushNestingOfTheListsAndOfEveryPicture) {
  GSHSCENE_Limits l = limits_with();
  l.max_depth = 4;
  {
    Fx f(l);
    EditHolder e(f.s);
    // Two nested groups that each opacity and a clip wrap: four pushes.
    GSHSCENE_Node g = group_node(0.5);
    g.clip = f.sq.get();
    uint64_t g1 = add(e.edit, GSHSCENE_ROOT_ID, g);
    uint64_t g2 = add(e.edit, g1, g);
    add(e.edit, g2, shape_node(f.sq.get()));
    EXPECT_EQ(f.publish_result(e.edit), GSHSCENE_OK);
  }
  {
    Fx f(l);
    EditHolder e(f.s);
    GSHSCENE_Node g = group_node(0.5);
    g.clip = f.sq.get();
    uint64_t g1 = add(e.edit, GSHSCENE_ROOT_ID, g);
    uint64_t g2 = add(e.edit, g1, g);
    uint64_t g3 = add(e.edit, g2, group_node(0.5));
    add(e.edit, g3, shape_node(f.sq.get()));
    EXPECT_EQ(f.publish_result(e.edit), GSHSCENE_ERR_LIMIT) << "five";
  }
  // A group that nothing draws wraps nothing: a hidden, uninstanced group's depth is not a list's.
  {
    Fx f(l);
    EditHolder e(f.s);
    GSHSCENE_Node g = f.hidden_group(0.5);
    g.clip = f.sq.get();
    uint64_t g1 = add(e.edit, GSHSCENE_ROOT_ID, g);
    GSHSCENE_Node h = group_node(0.5);
    h.clip = f.sq.get();
    uint64_t g2 = add(e.edit, g1, h);
    add(e.edit, g2, group_node(0.5));
    EXPECT_EQ(f.publish_result(e.edit), GSHSCENE_OK);
  }
  // A picture is bounded too.
  {
    Fx f(l);
    EditHolder e(f.s);
    GSHSCENE_Node g = f.hidden_group(0.5);
    g.clip = f.sq.get();
    uint64_t d = add(e.edit, GSHSCENE_ROOT_ID, g);
    GSHSCENE_Node h = group_node(0.5);
    h.clip = f.sq.get();
    uint64_t d2 = add(e.edit, d, h);
    add(e.edit, d2, group_node(0.5));  // d: 2 + (2 + 1) = 5
    add(e.edit, GSHSCENE_ROOT_ID, instance_node(d));
    EXPECT_EQ(f.publish_result(e.edit), GSHSCENE_ERR_LIMIT);
  }
}

TEST(SceneLimits, AnEmptyGroupIsALevelAndItsWrappersAreItems) {
  GSHSCENE_Limits l = limits_with();
  l.max_node_depth = 3;
  {
    Fx f(l);
    EditHolder e(f.s);
    uint64_t g1 = add(e.edit, GSHSCENE_ROOT_ID, group_node());
    add(e.edit, g1, group_node());  // root, g1, an empty group: three levels
    EXPECT_EQ(f.publish_result(e.edit), GSHSCENE_OK);
  }
  {
    Fx f(l);
    EditHolder e(f.s);
    uint64_t g1 = add(e.edit, GSHSCENE_ROOT_ID, group_node());
    uint64_t g2 = add(e.edit, g1, group_node());
    add(e.edit, g2, group_node());  // four levels, the last empty
    EXPECT_EQ(f.publish_result(e.edit), GSHSCENE_ERR_LIMIT);
  }
  // Wrappers are items: a faded group of two shapes is four items.
  l = limits_with();
  l.max_list_items = 4;
  for (int shapes : {2, 3}) {
    Fx f(l);
    EditHolder e(f.s);
    uint64_t g = add(e.edit, GSHSCENE_ROOT_ID, group_node(0.5));
    for (int i = 0; i < shapes; i++) {
      add(e.edit, g, shape_node(f.sq.get()));
    }
    EXPECT_EQ(f.publish_result(e.edit), shapes == 2 ? GSHSCENE_OK : GSHSCENE_ERR_LIMIT) << shapes;
  }
}

TEST(SceneLimits, TheExpansionOfAListCountsWrappersAndInstancesNotOnlyOccurrences) {
  // A clipped, faded group around one shape: two occurrences and five items (two pushes, two pops, a fill).
  GSHSCENE_Limits l = limits_with();
  for (size_t limit : {5u, 4u}) {
    l.max_expansion = limit;
    Fx f(l);
    EditHolder e(f.s);
    GSHSCENE_Node g = group_node(0.5);
    g.clip = f.sq.get();
    uint64_t gid = add(e.edit, GSHSCENE_ROOT_ID, g);
    add(e.edit, gid, shape_node(f.sq.get()));
    // occurrences: root, group, shape = 3 (within both limits); expansion: 5
    EXPECT_EQ(f.publish_result(e.edit), limit == 5 ? GSHSCENE_OK : GSHSCENE_ERR_LIMIT) << limit;
  }
  // The same group instanced: the instance adds one to the picture's five.
  for (size_t limit : {6u, 5u}) {
    l.max_expansion = limit;
    Fx f(l);
    EditHolder e(f.s);
    GSHSCENE_Node g = group_node(0.5);
    g.clip = f.sq.get();
    g.flags = GSHSCENE_NODE_HIDDEN;
    uint64_t def = add(e.edit, GSHSCENE_ROOT_ID, g);
    add(e.edit, def, shape_node(f.sq.get()));
    add(e.edit, GSHSCENE_ROOT_ID, instance_node(def));
    // occurrences: root, instance, def, shape = 4; expansion of the list: 1 + 5 = 6
    EXPECT_EQ(f.publish_result(e.edit), limit == 6 ? GSHSCENE_OK : GSHSCENE_ERR_LIMIT) << limit;
  }
}

TEST(SceneLimits, APicturesPushNestingAtExactlyTheLimitPublishes) {
  GSHSCENE_Limits l = limits_with();
  l.max_depth = 4;
  Fx f(l);
  EditHolder e(f.s);
  GSHSCENE_Node g = f.hidden_group(0.5);
  g.clip = f.sq.get();
  uint64_t d = add(e.edit, GSHSCENE_ROOT_ID, g);
  GSHSCENE_Node h = group_node(0.5);
  h.clip = f.sq.get();
  uint64_t d2 = add(e.edit, d, h);
  add(e.edit, d2, shape_node(f.sq.get()));  // 2 + 2 = 4 pushes
  add(e.edit, GSHSCENE_ROOT_ID, instance_node(d));
  EXPECT_EQ(f.publish_result(e.edit), GSHSCENE_OK);
}

TEST(SceneLimits, MaxPictureDepthPublishesExactlyTheLimitAndRefusesOneMore) {
  GSHSCENE_Limits l = limits_with();
  l.max_picture_depth = 3;
  auto chain = [](Fx & f, GSHSCENE_Edit * e, int pictures) {
    uint64_t prev = add(e, GSHSCENE_ROOT_ID, f.hidden_group());
    add(e, prev, shape_node(f.sq.get()));
    for (int i = 1; i < pictures; i++) {
      uint64_t d = add(e, GSHSCENE_ROOT_ID, f.hidden_group());
      add(e, d, instance_node(prev));
      prev = d;
    }
    add(e, GSHSCENE_ROOT_ID, instance_node(prev));
  };
  {
    Fx f(l);
    EditHolder e(f.s);
    chain(f, e.edit, 3);
    EXPECT_EQ(f.publish_result(e.edit), GSHSCENE_OK) << "pictures nested three deep";
    const GSHSCENE_Version * v = nullptr;
    ASSERT_EQ(gshscene_scene_current(f.s.scene, &v), GSHSCENE_OK);
    VersionRef hold(v);
    const GSHSCENE_List * list = nullptr;
    ASSERT_EQ(gshscene_version_list(v, &list), GSHSCENE_OK) << "and the emission, bounded by the same limit, succeeds";
    const GSHSCENE_Item * item = nullptr;
    ASSERT_EQ(gshscene_list_item(list, 0, &item), GSHSCENE_OK);
    EXPECT_EQ(gshscene_resource_picture_depth(item->resource), 3u);
  }
  {
    Fx f(l);
    EditHolder e(f.s);
    chain(f, e.edit, 4);
    EXPECT_EQ(f.publish_result(e.edit), GSHSCENE_ERR_LIMIT);
  }
}

TEST(SceneLimits, MaxListBytesIsMetWhenTheListIsEmittedAndIsExact) {
  // The arena the emitted list takes is a function of the list: measure it, then give exactly that, and one byte less.
  auto build = [](const GSHSCENE_Limits * limits, VersionRef * v) {
    SceneHolder * s = new SceneHolder(limits);
    EditHolder e(*s);
    Ref sq(intern_square(s->store()));
    for (int i = 0; i < 100; i++) {
      add(e.edit, GSHSCENE_ROOT_ID, shape_node(sq.get()));
    }
    publish(*s, e, v);
    return s;
  };
  size_t needed = 0;
  {
    VersionRef v;
    SceneHolder * s = build(nullptr, &v);
    const GSHSCENE_List * list = nullptr;
    ASSERT_EQ(gshscene_version_list(v.get(), &list), GSHSCENE_OK);
    needed = gshscene_list_bytes(list);
    v.reset();
    delete s;
  }
  ASSERT_GT(needed, 0u);
  GSHSCENE_Limits l = limits_with();
  l.max_list_bytes = needed;
  {
    VersionRef v;
    SceneHolder * s = build(&l, &v);
    const GSHSCENE_List * list = nullptr;
    EXPECT_EQ(gshscene_version_list(v.get(), &list), GSHSCENE_OK) << "exactly the arena it needs";
    v.reset();
    delete s;
  }
  l.max_list_bytes = needed - 1;
  {
    VersionRef v;
    SceneHolder * s = build(&l, &v);
    const GSHSCENE_List * list = nullptr;
    EXPECT_EQ(gshscene_version_list(v.get(), &list), GSHSCENE_ERR_LIMIT);
    v.reset();
    delete s;
  }
}

TEST(SceneLimits, TheDefaultsAreTheDocumentedOnes) {
  GSHSCENE_Limits l;
  gshscene_limits_default(&l);
  SceneHolder s(&l);
  EditHolder e(s);
  Ref sq(intern_square(s.store()));
  // A modest scene is nowhere near any of them.
  uint64_t g = add(e.edit, GSHSCENE_ROOT_ID, group_node());
  for (int i = 0; i < 1000; i++) {
    add(e.edit, g, shape_node(sq.get()));
  }
  VersionRef v;
  publish(s, e, &v);
  EXPECT_EQ(gshscene_version_node_count(v.get()), 1002u);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
