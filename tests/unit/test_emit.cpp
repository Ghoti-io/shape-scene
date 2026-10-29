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
 * A version's emitted list: equal to the immediate-mode list that draws the
 * same, one picture per instanced target, the origin tables, the cache and
 * failure (version.h; story 11).
 */

#include "scene_fixtures.h"

#include "../alloc_tracker.h"

#include <atomic>
#include <cmath>
#include <thread>

using namespace scene;

namespace {

using Ids = std::vector<uint64_t>;

GSHGEOM_Map compose(const GSHGEOM_Map & outer, const GSHGEOM_Map & inner) {
  GSHGEOM_Map out;
  EXPECT_EQ(gshgeom_map_compose(&outer, &inner, &out), GSHGEOM_OK);
  return out;
}

GSHGEOM_Map scale_map(double s) {
  GSHGEOM_Map m;
  gshgeom_map_scale(2, s, s, 1.0, &m);
  return m;
}

const GSHSCENE_List * list_of(const GSHSCENE_Version * v) {
  const GSHSCENE_List * l = nullptr;
  EXPECT_EQ(gshscene_version_list(v, &l), GSHSCENE_OK);
  return l;
}

const GSHSCENE_Item * item_of(const GSHSCENE_List * list, size_t i) {
  const GSHSCENE_Item * item = nullptr;
  EXPECT_EQ(gshscene_list_item(list, i, &item), GSHSCENE_OK);
  return item;
}

Ids origins(const GSHSCENE_Version * v, uint64_t table, size_t n) {
  Ids out;
  for (size_t i = 0; i < n; i++) {
    uint64_t node = 0;
    EXPECT_EQ(gshscene_version_item_node(v, table, i, &node), GSHSCENE_OK) << i;
    out.push_back(node);
  }
  return out;
}

/** The scene of the emission tests (see the expected list below). */
struct Doc {
  SceneHolder s;
  Ref a, b, c, t, clip;
  VersionRef v;
  uint64_t A, G, B, C2, D, T, I1, P, E;
  GSHGEOM_Map map_a, map_g, map_b, map_e, map_p, map_clip, map_i1, map_t;
  Doc() : a(intern_square(s.store(), 0)), b(intern_square(s.store(), 1)), c(intern_square(s.store(), 2)),
          t(intern_square(s.store(), 3)), clip(intern_fixture(s.store())) {
    EditHolder e(s);
    map_a = translate(1, 1);
    GSHSCENE_Node an = shape_node(a.get(), 1);
    an.map = map_a;
    an.fill_rule = GSHGEOM_FILL_EVEN_ODD;
    an.paint = solid(1, 0, 0, 1);
    A = add(e.edit, GSHSCENE_ROOT_ID, an);
    map_g = scale_map(2);
    map_clip = translate(0.5, 0.25);
    GSHSCENE_Node gn = group_node(0.5, 3);
    gn.map = map_g;
    gn.clip = clip.get();
    gn.clip_map = map_clip;
    gn.clip_rule = GSHGEOM_FILL_EVEN_ODD;
    G = add(e.edit, GSHSCENE_ROOT_ID, gn);
    map_b = translate(5, 0);
    GSHSCENE_Node bn = shape_node(b.get(), 4);
    bn.map = map_b;
    B = add(e.edit, G, bn);
    C2 = add(e.edit, G, shape_node(c.get(), 5));
    GSHSCENE_Node dn = group_node();
    dn.flags = GSHSCENE_NODE_HIDDEN;
    D = add(e.edit, GSHSCENE_ROOT_ID, dn);
    map_t = translate(0, 7);
    GSHSCENE_Node tn = shape_node(t.get(), 6);
    tn.map = map_t;
    T = add(e.edit, D, tn);
    map_i1 = translate(20, 20);
    GSHSCENE_Node in = instance_node(D, 7);
    in.map = map_i1;
    I1 = add(e.edit, GSHSCENE_ROOT_ID, in);
    map_p = translate(3, 3);
    GSHSCENE_Node pn = group_node();  // a container: opacity 1, no clip
    pn.map = map_p;
    P = add(e.edit, GSHSCENE_ROOT_ID, pn);
    map_e = translate(0, 1);
    GSHSCENE_Node en = shape_node(a.get(), 8);
    en.map = map_e;
    E = add(e.edit, P, en);
    publish(s, e, &v);
  }
};

}  // namespace

TEST(Emit, TheListIsTheImmediateModeListThatDrawsTheSame) {
  Doc d;
  const GSHSCENE_List * list = list_of(d.v.get());
  ASSERT_NE(list, nullptr);
  EXPECT_EQ(gshscene_list_is_closed(list), 1);
  // The same drawing, recorded by hand against the same store.
  GSHSCENE_Store * store = d.s.store();
  // The picture of D: T at D's placement (the identity) composed with its own map.
  ListHolder inner(store);
  GSHSCENE_Paint black = solid(0, 0, 0, 1);
  ASSERT_EQ(gshscene_list_fill(inner.list, d.t.get(), &d.map_t, GSHGEOM_FILL_NONZERO, &black, 6, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(inner.list), GSHSCENE_OK);
  Ref picture;
  ASSERT_EQ(gshscene_store_intern_picture(store, inner.list, &picture.resource), GSHSCENE_OK);
  ListHolder want(store);
  GSHSCENE_Paint red = solid(1, 0, 0, 1);
  ASSERT_EQ(gshscene_list_fill(want.list, d.a.get(), &d.map_a, GSHGEOM_FILL_EVEN_ODD, &red, 1, 0), GSHSCENE_OK);
  GSHGEOM_Map clip_map = compose(d.map_g, d.map_clip);
  // A group's clip push carries the group's layer and depth: the clip block is the group's unit of its sequence (N11).
  ASSERT_EQ(gshscene_list_clip_push(want.list, d.clip.get(), &clip_map, GSHGEOM_FILL_EVEN_ODD, 3, 0), GSHSCENE_OK);
  GSHSCENE_Group g = make_group(0.5);
  ASSERT_EQ(gshscene_list_group_push(want.list, &g, 3, 0), GSHSCENE_OK);
  GSHGEOM_Map mb = compose(d.map_g, d.map_b);
  ASSERT_EQ(gshscene_list_fill(want.list, d.b.get(), &mb, GSHGEOM_FILL_NONZERO, &black, 4, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(want.list, d.c.get(), &d.map_g, GSHGEOM_FILL_NONZERO, &black, 5, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_pop(want.list), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_clip_pop(want.list), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_instance(want.list, picture.get(), &d.map_i1, 7, 0), GSHSCENE_OK);
  GSHGEOM_Map me = compose(d.map_p, d.map_e);
  ASSERT_EQ(gshscene_list_fill(want.list, d.a.get(), &me, GSHGEOM_FILL_NONZERO, &black, 8, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(want.list), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_count(list), 9u);
  EXPECT_EQ(gshscene_list_equal(list, want.list), 1) << dump_list(list) << "\n---\n" << dump_list(want.list);
  EXPECT_EQ(gshscene_list_hash(list), gshscene_list_hash(want.list));
  const GSHSCENE_Resource * got = nullptr;
  ASSERT_EQ(gshscene_version_picture(d.v.get(), d.D, &got), GSHSCENE_OK);
  EXPECT_EQ(got, picture.get()) << "equal content is one handle: the version's picture is the one interned by hand";
}

TEST(Emit, OriginsNameTheNodeEachItemCameFrom) {
  Doc d;
  const GSHSCENE_List * list = list_of(d.v.get());
  EXPECT_EQ(origins(d.v.get(), 0, gshscene_list_count(list)), (Ids{d.A, d.G, d.G, d.B, d.C2, d.G, d.G, d.I1, d.E}));
  const GSHSCENE_Resource * pic = nullptr;
  ASSERT_EQ(gshscene_version_picture(d.v.get(), d.D, &pic), GSHSCENE_OK);
  EXPECT_EQ(origins(d.v.get(), d.D, gshscene_list_count(gshscene_resource_picture(pic))), (Ids{d.T}));
  uint64_t node = 0;
  EXPECT_EQ(gshscene_version_item_node(d.v.get(), 0, 9, &node), GSHSCENE_ERR_INVALID) << "past the end";
  EXPECT_EQ(gshscene_version_item_node(d.v.get(), d.D, 1, &node), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_version_item_node(d.v.get(), d.G, 0, &node), GSHSCENE_ERR_INVALID) << "G is not an instanced node";
  EXPECT_EQ(gshscene_version_item_node(d.v.get(), 9999, 0, &node), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_version_item_node(nullptr, 0, 0, &node), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_version_item_node(d.v.get(), 0, 0, nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_version_picture(d.v.get(), d.G, &pic), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_version_picture(nullptr, d.D, &pic), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_version_picture(d.v.get(), d.D, nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_version_list(nullptr, nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_version_list(d.v.get(), nullptr), GSHSCENE_ERR_INVALID);
}

TEST(Emit, TheListIsBuiltOnceAndKeptByTheVersion) {
  Doc d;
  const GSHSCENE_List * first = list_of(d.v.get());
  const GSHSCENE_List * second = list_of(d.v.get());
  EXPECT_EQ(first, second);
  GSHSCENE_Store_Stats before;
  ASSERT_EQ(gshscene_store_stats(d.s.store(), &before), GSHSCENE_OK);
  list_of(d.v.get());
  GSHSCENE_Store_Stats after;
  ASSERT_EQ(gshscene_store_stats(d.s.store(), &after), GSHSCENE_OK);
  EXPECT_EQ(after.created, before.created) << "a second call interns nothing";
}

TEST(Emit, SeveralInstancesOfOneTargetShareOnePictureAndOneResource) {
  SceneHolder s;
  Ref sq(intern_square(s.store()));
  EditHolder e(s);
  GSHSCENE_Node dn = group_node();
  dn.flags = GSHSCENE_NODE_HIDDEN;
  uint64_t d = add(e.edit, GSHSCENE_ROOT_ID, dn);
  uint64_t t = add(e.edit, d, shape_node(sq.get()));
  Ids insts;
  for (int i = 0; i < 5; i++) {
    GSHSCENE_Node in = instance_node(d);
    in.map = translate(10.0 * i, 0);
    insts.push_back(add(e.edit, GSHSCENE_ROOT_ID, in));
  }
  VersionRef v;
  publish(s, e, &v);
  const GSHSCENE_List * list = list_of(v.get());
  ASSERT_EQ(gshscene_list_count(list), 5u);
  const GSHSCENE_Resource * pic = nullptr;
  ASSERT_EQ(gshscene_version_picture(v.get(), d, &pic), GSHSCENE_OK);
  for (size_t i = 0; i < 5; i++) {
    EXPECT_EQ(item_of(list, i)->resource, pic) << i;
    EXPECT_EQ(item_of(list, i)->kind, GSHSCENE_ITEM_INSTANCE);
  }
  GSHSCENE_Store_Stats stats;
  ASSERT_EQ(gshscene_store_stats(s.store(), &stats), GSHSCENE_OK);
  EXPECT_EQ(stats.live, 2u) << "the path and one picture";
  EXPECT_EQ(origins(v.get(), 0, 5), insts);
  (void)t;
}

TEST(Emit, NestedInstancesMakePicturesOfPictures) {
  SceneHolder s;
  Ref sq(intern_square(s.store()));
  EditHolder e(s);
  GSHSCENE_Node hidden = group_node();
  hidden.flags = GSHSCENE_NODE_HIDDEN;
  uint64_t inner = add(e.edit, GSHSCENE_ROOT_ID, hidden);
  uint64_t t = add(e.edit, inner, shape_node(sq.get()));
  uint64_t outer = add(e.edit, GSHSCENE_ROOT_ID, hidden);
  uint64_t oi = add(e.edit, outer, instance_node(inner));
  uint64_t top = add(e.edit, GSHSCENE_ROOT_ID, instance_node(outer));
  VersionRef v;
  publish(s, e, &v);
  const GSHSCENE_List * list = list_of(v.get());
  ASSERT_EQ(gshscene_list_count(list), 1u);
  const GSHSCENE_Resource * outer_pic = nullptr;
  const GSHSCENE_Resource * inner_pic = nullptr;
  ASSERT_EQ(gshscene_version_picture(v.get(), outer, &outer_pic), GSHSCENE_OK);
  ASSERT_EQ(gshscene_version_picture(v.get(), inner, &inner_pic), GSHSCENE_OK);
  EXPECT_EQ(gshscene_resource_picture_depth(inner_pic), 1u);
  EXPECT_EQ(gshscene_resource_picture_depth(outer_pic), 2u);
  EXPECT_EQ(item_of(list, 0)->resource, outer_pic);
  EXPECT_EQ(item_of(gshscene_resource_picture(outer_pic), 0)->resource, inner_pic);
  EXPECT_EQ(origins(v.get(), outer, 1), (Ids{oi}));
  EXPECT_EQ(origins(v.get(), inner, 1), (Ids{t}));
  EXPECT_EQ(origins(v.get(), 0, 1), (Ids{top}));
  EXPECT_EQ(gshscene_list_expansion(list), 3u) << "an instance is 1 plus its picture's expansion: 1 + (1 + 1)";
}

TEST(Emit, WrappersAreEmittedOnlyWhereOpacityOrAClipNeedThem) {
  SceneHolder s;
  Ref sq(intern_square(s.store()));
  EditHolder e(s);
  uint64_t plain = add(e.edit, GSHSCENE_ROOT_ID, group_node(1.0));
  add(e.edit, plain, shape_node(sq.get()));
  uint64_t faint = add(e.edit, GSHSCENE_ROOT_ID, group_node(0.999, 9));
  add(e.edit, faint, shape_node(sq.get()));
  GSHSCENE_Node cn = group_node();
  cn.clip = sq.get();
  uint64_t clipped = add(e.edit, GSHSCENE_ROOT_ID, cn);
  add(e.edit, clipped, shape_node(sq.get()));
  uint64_t empty_faint = add(e.edit, GSHSCENE_ROOT_ID, group_node(0.0));
  add(e.edit, GSHSCENE_ROOT_ID, group_node());  // an empty container: nothing
  VersionRef v;
  publish(s, e, &v);
  const GSHSCENE_List * list = list_of(v.get());
  std::vector<GSHSCENE_Item_Kind> kinds;
  for (size_t i = 0; i < gshscene_list_count(list); i++) {
    kinds.push_back(item_of(list, i)->kind);
  }
  std::vector<GSHSCENE_Item_Kind> want = {
      GSHSCENE_ITEM_FILL,                                                                       // plain
      GSHSCENE_ITEM_GROUP_PUSH, GSHSCENE_ITEM_FILL, GSHSCENE_ITEM_GROUP_POP,                    // faint
      GSHSCENE_ITEM_CLIP_PUSH, GSHSCENE_ITEM_FILL, GSHSCENE_ITEM_CLIP_POP,                     // clipped
      GSHSCENE_ITEM_GROUP_PUSH, GSHSCENE_ITEM_GROUP_POP};                                      // empty, opacity 0
  EXPECT_EQ(kinds, want);
  EXPECT_EQ(item_of(list, 1)->layer, 9) << "a group push carries the group's layer";
  EXPECT_EQ(item_of(list, 1)->group.opacity, 0.999);
  EXPECT_EQ(item_of(list, 7)->group.opacity, 0.0);
  EXPECT_EQ(origins(v.get(), 0, 9), (Ids{3, faint, 5, faint, clipped, 7, clipped, empty_faint, empty_faint}));
}

TEST(Emit, AClippedFadingGroupPutsTheClipOutsideAndItsMapBeforeTheClipsOwn) {
  SceneHolder s;
  Ref sq(intern_square(s.store()));
  Ref cp(intern_fixture(s.store()));
  EditHolder e(s);
  GSHSCENE_Node outer = group_node();
  outer.map = translate(100, 0);
  uint64_t go = add(e.edit, GSHSCENE_ROOT_ID, outer);
  GSHSCENE_Node inner = group_node(0.5);
  inner.map = scale_map(3);
  inner.clip = cp.get();
  inner.clip_map = translate(1, 2);
  uint64_t gi = add(e.edit, go, inner);
  GSHSCENE_Node leaf = shape_node(sq.get());
  leaf.map = translate(4, 5);
  add(e.edit, gi, leaf);
  VersionRef v;
  publish(s, e, &v);
  const GSHSCENE_List * list = list_of(v.get());
  ASSERT_EQ(gshscene_list_count(list), 5u);
  EXPECT_EQ(item_of(list, 0)->kind, GSHSCENE_ITEM_CLIP_PUSH);
  EXPECT_EQ(item_of(list, 1)->kind, GSHSCENE_ITEM_GROUP_PUSH);
  EXPECT_EQ(item_of(list, 2)->kind, GSHSCENE_ITEM_FILL);
  EXPECT_EQ(item_of(list, 3)->kind, GSHSCENE_ITEM_GROUP_POP);
  EXPECT_EQ(item_of(list, 4)->kind, GSHSCENE_ITEM_CLIP_POP);
  GSHGEOM_Map group_space = compose(translate(100, 0), scale_map(3));
  GSHGEOM_Map want_clip = compose(group_space, translate(1, 2));
  GSHGEOM_Map want_leaf = compose(group_space, translate(4, 5));
  EXPECT_EQ(gshgeom_map_hash(&item_of(list, 0)->map), gshgeom_map_hash(&want_clip));
  EXPECT_EQ(gshgeom_map_hash(&item_of(list, 2)->map), gshgeom_map_hash(&want_leaf));
  (void)gi;
}

TEST(Emit, AMaskPlacementPastTheMapBoundIsErrLimitAtEmissionAndOnlyForAGroupWithAMask) {
  // MK6: the placement is composed with the group's accumulated map as the clip's is, and the composition fails as the clip's does.
  SceneHolder s;
  Ref sq(intern_square(s.store()));
  GSHSCENE_List * l = nullptr;
  ASSERT_EQ(gshscene_list_create(nullptr, nullptr, s.store(), &l), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(l, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  const GSHSCENE_Resource * picture = nullptr;
  ASSERT_EQ(gshscene_store_intern_picture(s.store(), l, &picture), GSHSCENE_OK);
  gshscene_list_destroy(l);
  Ref pic(picture);
  Ref mask(known_mask(s.store(), pic.get(), 0));
  ASSERT_NE(mask.get(), nullptr);
  for (int masked = 0; masked < 2; masked++) {
    EditHolder e(s);
    GSHSCENE_Node g = group_node();
    g.map = scale_map(std::ldexp(1.0, 60));
    if (masked) {
      g.soft_mask = mask.get();
      g.soft_mask_map = scale_map(std::ldexp(1.0, 60));  // 2^120 with the group's map: past the 2^100 bound
    }
    uint64_t gid = add(e.edit, GSHSCENE_ROOT_ID, g);
    add(e.edit, gid, shape_node(sq.get()));
    VersionRef v;
    publish(s, e, &v);
    const GSHSCENE_List * list = nullptr;
    EXPECT_EQ(gshscene_version_list(v.get(), &list), masked ? GSHSCENE_ERR_LIMIT : GSHSCENE_OK) << (masked ? "masked" : "unmasked");
    EXPECT_EQ(list == nullptr, masked != 0);
  }
}

TEST(Emit, AComposedMapPastTheMapBoundIsErrLimitAtEmissionAndNeverTruncated) {
  SceneHolder s;
  Ref sq(intern_square(s.store()));
  EditHolder e(s);
  GSHSCENE_Node outer = group_node();
  outer.map = scale_map(std::ldexp(1.0, 60));
  uint64_t g1 = add(e.edit, GSHSCENE_ROOT_ID, outer);
  uint64_t g2 = add(e.edit, g1, outer);  // 2^120: past the 2^100 bound of a map entry
  add(e.edit, g2, shape_node(sq.get()));
  VersionRef v;
  publish(s, e, &v);  // publish composes nothing: it is structure
  const GSHSCENE_List * list = nullptr;
  EXPECT_EQ(gshscene_version_list(v.get(), &list), GSHSCENE_ERR_LIMIT);
  EXPECT_EQ(list, nullptr);
  EXPECT_EQ(gshscene_version_visit(v.get(), [](void *, const GSHSCENE_Visit *) { return 0; }, nullptr), -1) << "the visitor reports the failure";
}

TEST(Emit, ALongListAndManyPicturesGrowTheirTables) {
  SceneHolder s;
  std::vector<Ref> paths;
  for (int i = 0; i < 14; i++) {
    paths.emplace_back(intern_square(s.store(), 10.0 * i));
  }
  EditHolder e(s);
  Ids defs, insts;
  for (int i = 0; i < 14; i++) {  // fourteen definitions, each with its own shape
    GSHSCENE_Node hidden = group_node();
    hidden.flags = GSHSCENE_NODE_HIDDEN;
    uint64_t d = add(e.edit, GSHSCENE_ROOT_ID, hidden);
    add(e.edit, d, shape_node(paths[i].get()));
    defs.push_back(d);
  }
  Ids shapes;
  for (int i = 0; i < 90; i++) {
    shapes.push_back(add(e.edit, GSHSCENE_ROOT_ID, shape_node(paths[i % 14].get(), i)));
    if (i % 6 == 0) {
      insts.push_back(add(e.edit, GSHSCENE_ROOT_ID, instance_node(defs[(i / 6) % 14])));
    }
  }
  VersionRef v;
  publish(s, e, &v);
  const GSHSCENE_List * list = list_of(v.get());
  ASSERT_EQ(gshscene_list_count(list), 90u + insts.size());
  for (size_t i = 0; i < gshscene_list_count(list); i++) {
    uint64_t node = 0;
    ASSERT_EQ(gshscene_version_item_node(v.get(), 0, i, &node), GSHSCENE_OK) << i;
    GSHSCENE_Node_Info info = info_of(v.get(), node);
    const GSHSCENE_Item * item = item_of(list, i);
    if (item->kind == GSHSCENE_ITEM_INSTANCE) {
      EXPECT_EQ(info.node.kind, GSHSCENE_NODE_INSTANCE);
      const GSHSCENE_Resource * pic = nullptr;
      ASSERT_EQ(gshscene_version_picture(v.get(), info.node.target, &pic), GSHSCENE_OK);
      EXPECT_EQ(pic, item->resource);
      uint64_t inner = 0;
      ASSERT_EQ(gshscene_version_item_node(v.get(), info.node.target, 0, &inner), GSHSCENE_OK);
    } else {
      EXPECT_EQ(info.node.kind, GSHSCENE_NODE_SHAPE);
      EXPECT_EQ(info.node.resource, item->resource);
    }
  }
  for (uint64_t d : defs) {
    const GSHSCENE_Resource * pic = nullptr;
    EXPECT_EQ(gshscene_version_picture(v.get(), d, &pic), GSHSCENE_OK) << d;
  }
}

TEST(Emit, AClipWhoseComposedMapPassesTheBoundIsErrLimit) {
  SceneHolder s;
  Ref sq(intern_square(s.store()));
  EditHolder e(s);
  GSHSCENE_Node g = group_node();
  g.map = scale_map(std::ldexp(1.0, 60));  // fine alone
  g.clip = sq.get();
  g.clip_map = scale_map(std::ldexp(1.0, 60));  // and alone; composed they are 2^120
  add(e.edit, GSHSCENE_ROOT_ID, g);
  VersionRef v;
  publish(s, e, &v);
  const GSHSCENE_List * list = nullptr;
  EXPECT_EQ(gshscene_version_list(v.get(), &list), GSHSCENE_ERR_LIMIT);
}

TEST(Emit, AnEmptySceneEmitsAnEmptyClosedList) {
  SceneHolder s;
  VersionRef v;
  ASSERT_EQ(gshscene_scene_current(s.scene, &v.version), GSHSCENE_OK);
  const GSHSCENE_List * list = list_of(v.get());
  EXPECT_EQ(gshscene_list_count(list), 0u);
  EXPECT_EQ(gshscene_list_is_closed(list), 1);
  uint64_t node;
  EXPECT_EQ(gshscene_version_item_node(v.get(), 0, 0, &node), GSHSCENE_ERR_INVALID);
}

TEST(Emit, AHiddenRootDrawsNothing) {
  SceneHolder s;
  Ref sq(intern_square(s.store()));
  EditHolder e(s);
  add(e.edit, GSHSCENE_ROOT_ID, shape_node(sq.get()));
  GSHSCENE_Node root = group_node();
  root.flags = GSHSCENE_NODE_HIDDEN;
  ASSERT_EQ(gshscene_edit_set(e.edit, GSHSCENE_ROOT_ID, &root), GSHSCENE_OK);
  VersionRef v;
  publish(s, e, &v);
  EXPECT_EQ(gshscene_list_count(list_of(v.get())), 0u);
}

TEST(Emit, GlyphRunsFillAndClipLikePaths) {
  SceneHolder s;
  Ref sq(intern_square(s.store()));
  GSHSCENE_Glyph_Run run;
  std::memset(&run, 0, sizeof run);
  run.source = GSHSCENE_GLYPHS_FROM_FONT;
  Ref run_face(intern_face(s.store(), 0x1234));
  run.face = run_face.get();
  run.size = 16;
  GSHSCENE_Glyph glyphs[2] = {{5, 0, 0, nullptr}, {6, 9, 0, nullptr}};
  run.glyph_count = 2;
  run.glyphs = glyphs;
  Ref text;
  ASSERT_EQ(gshscene_store_intern_glyph_run(s.store(), &run, &text.resource), GSHSCENE_OK);
  EditHolder e(s);
  uint64_t t = add(e.edit, GSHSCENE_ROOT_ID, shape_node(text.get()));
  GSHSCENE_Node cn = group_node();
  cn.clip = text.get();
  uint64_t c = add(e.edit, GSHSCENE_ROOT_ID, cn);
  VersionRef v;
  publish(s, e, &v);
  const GSHSCENE_List * list = list_of(v.get());
  ASSERT_EQ(gshscene_list_count(list), 3u);
  EXPECT_EQ(item_of(list, 0)->resource, text.get());
  EXPECT_EQ(item_of(list, 1)->resource, text.get());
  (void)t;
  (void)c;
}

TEST(Emit, TheListsLimitsAreTheScenesAndABigListIsRefusedWithErrLimitAtEmission) {
  // The arena of the emitted list is bounded by max_list_bytes: met when the list is emitted, not at publish.
  GSHSCENE_Limits limits;
  gshscene_limits_default(&limits);
  limits.max_list_bytes = 1024;
  SceneHolder s(&limits);
  Ref sq(intern_square(s.store()));
  EditHolder e(s);
  for (int i = 0; i < 40; i++) {
    add(e.edit, GSHSCENE_ROOT_ID, shape_node(sq.get()));
  }
  VersionRef v;
  publish(s, e, &v);
  const GSHSCENE_List * list = nullptr;
  EXPECT_EQ(gshscene_version_list(v.get(), &list), GSHSCENE_ERR_LIMIT);
  EXPECT_EQ(list, nullptr);
  EXPECT_EQ(gshscene_version_list(v.get(), &list), GSHSCENE_ERR_LIMIT) << "nothing was kept: the same answer";
}

TEST(Emit, TheStoresResourceLimitIsMetAtEmissionAndNothingIsLeftBehind) {
  GSHSCENE_Limits limits;
  gshscene_limits_default(&limits);
  limits.max_resources = 2;  // the path, and one picture
  SceneHolder s(&limits);
  Ref sq(intern_square(s.store()));
  Ref other(intern_square(s.store(), 5));
  EditHolder e(s);
  GSHSCENE_Node hidden = group_node();
  hidden.flags = GSHSCENE_NODE_HIDDEN;
  uint64_t d1 = add(e.edit, GSHSCENE_ROOT_ID, hidden);
  add(e.edit, d1, shape_node(sq.get()));
  uint64_t d2 = add(e.edit, GSHSCENE_ROOT_ID, hidden);
  add(e.edit, d2, shape_node(other.get()));
  add(e.edit, GSHSCENE_ROOT_ID, instance_node(d1));
  add(e.edit, GSHSCENE_ROOT_ID, instance_node(d2));
  VersionRef v;
  publish(s, e, &v);
  GSHSCENE_Store_Stats before;
  ASSERT_EQ(gshscene_store_stats(s.store(), &before), GSHSCENE_OK);
  const GSHSCENE_List * list = nullptr;
  EXPECT_EQ(gshscene_version_list(v.get(), &list), GSHSCENE_ERR_LIMIT);
  GSHSCENE_Store_Stats after;
  ASSERT_EQ(gshscene_store_stats(s.store(), &after), GSHSCENE_OK);
  EXPECT_EQ(after.live, before.live) << "a failed emission releases what it interned";
}

TEST(Emit, EveryFailingAllocationLeavesTheVersionAbleToEmitAgain) {
  tracker::Tracker t;
  GSHSCENE_Allocator a = tracker::allocator_for(&t);
  {
    SceneHolder s(nullptr, &a);
    Ref sq(intern_square(s.store()));
    EditHolder e(s);
    GSHSCENE_Node hidden = group_node();
    hidden.flags = GSHSCENE_NODE_HIDDEN;
    uint64_t d = add(e.edit, GSHSCENE_ROOT_ID, hidden);
    add(e.edit, d, shape_node(sq.get()));
    for (int i = 0; i < 20; i++) {
      add(e.edit, GSHSCENE_ROOT_ID, instance_node(d));
      add(e.edit, GSHSCENE_ROOT_ID, shape_node(sq.get()));
    }
    GSHSCENE_Node cn = group_node(0.5);
    cn.clip = sq.get();
    add(e.edit, GSHSCENE_ROOT_ID, cn);
    VersionRef v;
    publish(s, e, &v);
    uint64_t want_hash = 0;
    bool done = false;
    for (long k = 0; k < 600 && !done; k++) {
      t.fail_from = t.calls + k;
      t.persistent = false;
      t.failed = false;
      const GSHSCENE_List * list = nullptr;
      GSHSCENE_Result r = gshscene_version_list(v.get(), &list);
      t.fail_from = -1;
      if (r == GSHSCENE_OK) {
        EXPECT_FALSE(t.failed) << "k=" << k;
        want_hash = gshscene_list_hash(list);
        done = true;
      } else {
        EXPECT_EQ(r, GSHSCENE_ERR_OOM) << "k=" << k;
        EXPECT_EQ(list, nullptr);
        GSHSCENE_Store_Stats stats;
        ASSERT_EQ(gshscene_store_stats(s.store(), &stats), GSHSCENE_OK);
        EXPECT_EQ(stats.live, 1u) << "k=" << k << ": the failed emission kept a picture";
      }
    }
    EXPECT_TRUE(done);
    // The list made after failures is the list made without them.
    SceneHolder s2;
    Ref sq2(intern_square(s2.store()));
    EditHolder e2(s2);
    GSHSCENE_Node hidden2 = group_node();
    hidden2.flags = GSHSCENE_NODE_HIDDEN;
    uint64_t d2 = add(e2.edit, GSHSCENE_ROOT_ID, hidden2);
    add(e2.edit, d2, shape_node(sq2.get()));
    for (int i = 0; i < 20; i++) {
      add(e2.edit, GSHSCENE_ROOT_ID, instance_node(d2));
      add(e2.edit, GSHSCENE_ROOT_ID, shape_node(sq2.get()));
    }
    GSHSCENE_Node cn2 = group_node(0.5);
    cn2.clip = sq2.get();
    add(e2.edit, GSHSCENE_ROOT_ID, cn2);
    VersionRef v2;
    publish(s2, e2, &v2);
    EXPECT_EQ(gshscene_list_hash(list_of(v2.get())), want_hash);
  }
  EXPECT_TRUE(t.live.empty()) << t.live.size() << " blocks leaked";
}

TEST(Emit, ManyThreadsAskingTogetherGetOneList) {
  Doc d;
  const int kThreads = 8;
  std::vector<const GSHSCENE_List *> got(kThreads, nullptr);
  std::atomic<int> go{0};
  std::vector<std::thread> threads;
  for (int i = 0; i < kThreads; i++) {
    threads.emplace_back([&, i] {
      while (go.load() == 0) {
      }
      const GSHSCENE_List * l = nullptr;
      if (gshscene_version_list(d.v.get(), &l) == GSHSCENE_OK) {
        got[i] = l;
      }
    });
  }
  go = 1;
  for (auto & t : threads) {
    t.join();
  }
  for (int i = 0; i < kThreads; i++) {
    EXPECT_NE(got[i], nullptr);
    EXPECT_EQ(got[i], got[0]);
  }
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
