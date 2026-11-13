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
 * Import: a subtree of another version copied into an edit with new IDs (edit.h, IM1 to IM6): what is copied, the IDs, the keys,
 * shared resources and the store they imply, instances inside and outside the subtree, and atomicity under every failing allocation.
 */

#include "scene_fixtures.h"

#include "../alloc_tracker.h"

#include <sstream>

using namespace scene;

namespace {

using Ids = std::vector<uint64_t>;

/** Every node of the view up to @p max_id, as text: what a failing import must not have changed. */
std::string view_state(const GSHSCENE_Edit * edit, uint64_t max_id) {
  std::ostringstream out;
  out << "count=" << gshscene_edit_node_count(edit);
  for (uint64_t id = 1; id <= max_id; id++) {
    GSHSCENE_Node_Info info;
    if (gshscene_edit_node(edit, id, &info) != GSHSCENE_OK) {
      out << " [" << id << " -]";
      continue;
    }
    out << " [" << id << " p" << info.parent << " k" << (int)info.node.kind << " op" << info.node.opacity << " l"
        << info.node.layer << " t" << info.node.target << (info.has_key ? " K" : "") << " c";
    for (size_t i = 0; i < info.child_count; i++) {
      uint64_t c = 0;
      gshscene_edit_child(edit, id, i, &c);
      out << c << ",";
    }
    out << "]";
  }
  return out.str();
}

Ids kids_of(const GSHSCENE_Edit * edit, uint64_t id) {
  GSHSCENE_Node_Info info;
  EXPECT_EQ(gshscene_edit_node(edit, id, &info), GSHSCENE_OK);
  Ids out;
  for (size_t i = 0; i < info.child_count; i++) {
    uint64_t c = 0;
    EXPECT_EQ(gshscene_edit_child(edit, id, i, &c), GSHSCENE_OK);
    out.push_back(c);
  }
  return out;
}

/**
 * A source scene: root > A (opacity 0.5, layer 3, hidden) > [S1 (keyed), B > [S2, S3 (layer 7)]], and a shape S4 under the root.
 * Published; the IDs are held in the fields.
 */
struct Source {
  SceneHolder s;
  Ref sq;
  VersionRef v;
  uint64_t a = 0, s1 = 0, b = 0, s2 = 0, s3 = 0, s4 = 0;
  explicit Source(GSHSCENE_Store * store = nullptr, const GSHSCENE_Allocator * alloc = nullptr)
      : s(nullptr, alloc, store), sq(intern_square(s.store())) {
    EditHolder e(s);
    GSHSCENE_Node an = group_node(0.5, 3);
    an.flags = GSHSCENE_NODE_HIDDEN;
    a = add(e.edit, GSHSCENE_ROOT_ID, an);
    GSHSCENE_Key k = K(9, 1);
    s1 = add(e.edit, a, shape_node(sq.get()), &k);
    b = add(e.edit, a, group_node());
    s2 = add(e.edit, b, shape_node(sq.get()));
    s3 = add(e.edit, b, shape_node(sq.get(), 7));
    s4 = add(e.edit, GSHSCENE_ROOT_ID, shape_node(sq.get()));
    publish(s, e, &v);
  }
};

}  // namespace

TEST(EditImport, ASubtreeIsCopiedWithItsShapeItsOrderAndItsPropertiesAndNewIds) {
  Source src;
  EditHolder e(src.s, src.v.get());  // an edit of the same scene: a copy within one scene
  const uint64_t before_count = gshscene_edit_node_count(e.edit);
  uint64_t copy = 0;
  ASSERT_EQ(gshscene_edit_import(e.edit, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, src.v.get(), src.a, &copy), GSHSCENE_OK);
  EXPECT_EQ(gshscene_edit_node_count(e.edit), before_count + 5u) << "A, S1, B, S2 and S3";
  /* IM2: new IDs from the scene's counter, in draw order: the copy of A, then S1, B, S2, S3. */
  const uint64_t base = src.s4 + 1;
  EXPECT_EQ(copy, base);
  Ids top = kids_of(e.edit, copy);
  ASSERT_EQ(top.size(), 2u);
  EXPECT_EQ(top[0], base + 1) << "the copy of S1";
  EXPECT_EQ(top[1], base + 2) << "the copy of B";
  EXPECT_EQ(kids_of(e.edit, top[1]), (Ids{base + 3, base + 4}));
  /* IM1: the properties. */
  GSHSCENE_Node_Info ci;
  ASSERT_EQ(gshscene_edit_node(e.edit, copy, &ci), GSHSCENE_OK);
  EXPECT_EQ(ci.node.kind, GSHSCENE_NODE_GROUP);
  EXPECT_EQ(ci.node.opacity, 0.5);
  EXPECT_EQ(ci.node.layer, 3);
  EXPECT_EQ(ci.node.flags, GSHSCENE_NODE_HIDDEN);
  EXPECT_EQ(ci.parent, GSHSCENE_ROOT_ID);
  GSHSCENE_Node_Info s3;
  ASSERT_EQ(gshscene_edit_node(e.edit, base + 4, &s3), GSHSCENE_OK);
  EXPECT_EQ(s3.node.kind, GSHSCENE_NODE_SHAPE);
  EXPECT_EQ(s3.node.layer, 7);
  EXPECT_EQ(s3.node.resource, src.sq.get()) << "IM4: the resource is the same";
  EXPECT_EQ(s3.parent, base + 2);
  /* The source nodes are untouched, and the root now has the copy last. */
  EXPECT_EQ(kids_of(e.edit, src.a), (Ids{src.s1, src.b}));
  EXPECT_EQ(kids_of(e.edit, GSHSCENE_ROOT_ID), (Ids{src.a, src.s4, copy}));
  /* It publishes, and the version holds the copy. */
  VersionRef v2;
  publish(src.s, e, &v2);
  EXPECT_EQ(gshscene_version_node_count(v2.get()), before_count + 5u);
}

TEST(EditImport, TheSameImportIntoTheSameStateGivesTheSameIds) {
  Source src;
  Ids first, second;
  for (int run = 0; run < 2; run++) {
    EditHolder e(src.s, src.v.get());
    uint64_t copy = 0;
    ASSERT_EQ(gshscene_edit_import(e.edit, GSHSCENE_ROOT_ID, 0, src.v.get(), src.b, &copy), GSHSCENE_OK);
    Ids ids{copy};
    for (uint64_t k : kids_of(e.edit, copy)) {
      ids.push_back(k);
    }
    (run ? second : first) = ids;
    /* the second edit draws the IDs after the first's, so compare the offsets from the copy */
    for (uint64_t & id : (run ? second : first)) {
      id -= copy;
    }
  }
  EXPECT_EQ(first, second);
  EXPECT_EQ(first, (Ids{0, 1, 2}));
}

TEST(EditImport, APositionInsertsTheCopyBeforeThatChild) {
  Source src;
  EditHolder e(src.s, src.v.get());
  uint64_t copy = 0;
  ASSERT_EQ(gshscene_edit_import(e.edit, GSHSCENE_ROOT_ID, 0, src.v.get(), src.s2, &copy), GSHSCENE_OK);
  EXPECT_EQ(kids_of(e.edit, GSHSCENE_ROOT_ID), (Ids{copy, src.a, src.s4}));
  EXPECT_EQ(gshscene_edit_import(e.edit, src.b, 1, src.v.get(), src.s4, &copy), GSHSCENE_OK);
  EXPECT_EQ(kids_of(e.edit, src.b), (Ids{src.s2, copy, src.s3}));
}

TEST(EditImport, KeysAreNotCarried) {
  Source src;
  EditHolder e(src.s, src.v.get());
  uint64_t copy = 0;
  ASSERT_EQ(gshscene_edit_import(e.edit, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, src.v.get(), src.a, &copy), GSHSCENE_OK);
  GSHSCENE_Key k = K(9, 1);
  uint64_t found = 0;
  ASSERT_EQ(gshscene_edit_lookup_key(e.edit, &k, &found), GSHSCENE_OK);
  EXPECT_EQ(found, src.s1) << "the key still names the original";
  GSHSCENE_Node_Info ci;
  ASSERT_EQ(gshscene_edit_node(e.edit, kids_of(e.edit, copy)[0], &ci), GSHSCENE_OK);
  EXPECT_EQ(ci.has_key, 0);
}

TEST(EditImport, TheRootMayBeTheSourceAndItsCopyIsAnOrdinaryGroup) {
  Source src;
  EditHolder e(src.s, src.v.get());
  uint64_t copy = 0;
  ASSERT_EQ(gshscene_edit_import(e.edit, src.b, GSHSCENE_APPEND, src.v.get(), GSHSCENE_ROOT_ID, &copy), GSHSCENE_OK);
  EXPECT_EQ(gshscene_edit_node_count(e.edit), 2u * gshscene_version_node_count(src.v.get())) << "every node again";
  GSHSCENE_Node_Info ci;
  ASSERT_EQ(gshscene_edit_node(e.edit, copy, &ci), GSHSCENE_OK);
  EXPECT_EQ(ci.node.kind, GSHSCENE_NODE_GROUP);
  EXPECT_EQ(ci.parent, src.b);
  EXPECT_EQ(ci.child_count, 2u);
}

TEST(EditImport, ScenesThatShareAStoreExchangeContent) {
  SceneHolder first;
  ASSERT_NE(first.scene, nullptr);
  Source src(first.store());  // a second scene over the first's store
  ASSERT_NE(src.s.scene, nullptr);
  EditHolder e(first);
  uint64_t copy = 0;
  ASSERT_EQ(gshscene_edit_import(e.edit, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, src.v.get(), src.a, &copy), GSHSCENE_OK);
  EXPECT_EQ(copy, 2u) << "the destination's own counter: the first ID it issues is 2";
  VersionRef v;
  publish(first, e, &v);
  EXPECT_EQ(gshscene_version_node_count(v.get()), 6u);
  EXPECT_EQ(info_of(v.get(), copy + 4).node.resource, src.sq.get());
}

/** A scene of its own store: root > G (clip, hidden-free) > [shape, stroke (style), group > shape]. */
struct Rich {
  SceneHolder s;
  VersionRef v;
  Ref sq, tri, style;
  uint64_t g = 0;
  explicit Rich(const GSHSCENE_Allocator * alloc = nullptr) : s(nullptr, alloc), sq(intern_square(s.store())), tri(intern_square(s.store(), 5.0)) {
    GSHSCENE_Stroke_Style st;
    gshscene_stroke_style_init(&st);
    st.width = 2.5;
    const GSHSCENE_Resource * r = nullptr;
    EXPECT_EQ(gshscene_store_intern_stroke_style(s.store(), &st, &r), GSHSCENE_OK);
    style = Ref(r);
    EditHolder e(s);
    GSHSCENE_Node gn = group_node(0.75, 2);
    gn.clip = tri.get();
    gn.clip_rule = GSHGEOM_FILL_EVEN_ODD;
    g = add(e.edit, GSHSCENE_ROOT_ID, gn);
    add(e.edit, g, shape_node(sq.get()));
    add(e.edit, g, stroke_node(tri.get(), style.get()));
    const uint64_t inner = add(e.edit, g, group_node());
    add(e.edit, inner, shape_node(tri.get(), 4));
    publish(s, e, &v);
  }
};

TEST(EditImport, ScenesWithStoresOfTheirOwnExchangeContentByCopyingTheResources) {
  Rich src;
  SceneHolder other;  // its own store
  ASSERT_NE(other.scene, nullptr);
  EditHolder e(other);
  const std::string before = view_state(e.edit, 20);
  uint64_t copy = 0;
  ASSERT_EQ(gshscene_edit_import(e.edit, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, src.v.get(), src.g, &copy), GSHSCENE_OK);
  EXPECT_NE(view_state(e.edit, 20), before);
  VersionRef v;
  publish(other, e, &v);
  EXPECT_EQ(gshscene_version_node_count(v.get()), gshscene_version_node_count(src.v.get()));
  /* every resource of the copy is of the destination's store, with the original's content */
  Ids top = children_of(v.get(), copy);
  ASSERT_EQ(top.size(), 3u);
  GSHSCENE_Node_Info gi = info_of(v.get(), copy);
  ASSERT_NE(gi.node.clip, nullptr);
  EXPECT_NE(gi.node.clip, src.tri.get());
  EXPECT_EQ(gshscene_resource_hash(gi.node.clip), gshscene_resource_hash(src.tri.get()));
  EXPECT_EQ(gi.node.clip_rule, GSHGEOM_FILL_EVEN_ODD);
  GSHSCENE_Node_Info si = info_of(v.get(), top[0]);
  EXPECT_NE(si.node.resource, src.sq.get());
  EXPECT_EQ(gshscene_resource_hash(si.node.resource), gshscene_resource_hash(src.sq.get()));
  GSHSCENE_Node_Info ti = info_of(v.get(), top[1]);
  EXPECT_EQ(gshscene_resource_hash(ti.node.style), gshscene_resource_hash(src.style.get()));
  EXPECT_EQ(gshscene_resource_hash(ti.node.resource), gshscene_resource_hash(src.tri.get()));
  /* the emitted list is the same as the source scene's, spelled by content */
  const GSHSCENE_List *la = nullptr, *lb = nullptr;
  ASSERT_EQ(gshscene_version_list(src.v.get(), &la), GSHSCENE_OK);
  ASSERT_EQ(gshscene_version_list(v.get(), &lb), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_hash(lb), gshscene_list_hash(la)) << "the same scene, spelled by content";
  EXPECT_EQ(dump_list(lb), dump_list(la));
}

TEST(EditImport, TheDestinationStoresLimitsRefuseTheImportAndTheViewIsAsItWas) {
  Rich src;
  GSHSCENE_Limits l;
  gshscene_limits_default(&l);
  l.max_resources = 1;  // the copies need a path, a second path and a style
  SceneHolder other(&l);
  ASSERT_NE(other.scene, nullptr);
  EditHolder e(other);
  const std::string before = view_state(e.edit, 20);
  uint64_t out = 9;
  EXPECT_EQ(gshscene_edit_import(e.edit, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, src.v.get(), src.g, &out), GSHSCENE_ERR_LIMIT);
  EXPECT_EQ(out, 9u);
  EXPECT_EQ(view_state(e.edit, 20), before);
  GSHSCENE_Store_Stats stats;
  gshscene_store_stats(other.store(), &stats);
  EXPECT_EQ(stats.live, 0u) << "what the failed copies made is released";
}

TEST(EditImport, AnInstanceInsideTheSubtreePointsAtTheCopyOfItsTarget) {
  SceneHolder s;
  Ref sq(intern_square(s.store()));
  VersionRef v;
  uint64_t g = 0, target = 0, inst = 0;
  {
    EditHolder e(s);
    g = add(e.edit, GSHSCENE_ROOT_ID, group_node());
    GSHSCENE_Node hid = group_node();
    hid.flags = GSHSCENE_NODE_HIDDEN;
    target = add(e.edit, g, hid);
    add(e.edit, target, shape_node(sq.get()));
    inst = add(e.edit, g, instance_node(target));
    publish(s, e, &v);
  }
  EditHolder e(s, v.get());
  uint64_t copy = 0;
  ASSERT_EQ(gshscene_edit_import(e.edit, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, v.get(), g, &copy), GSHSCENE_OK);
  Ids k = kids_of(e.edit, copy);
  ASSERT_EQ(k.size(), 2u);
  GSHSCENE_Node_Info ii, ti;
  ASSERT_EQ(gshscene_edit_node(e.edit, k[1], &ii), GSHSCENE_OK);
  ASSERT_EQ(gshscene_edit_node(e.edit, k[0], &ti), GSHSCENE_OK);
  EXPECT_EQ(ii.node.kind, GSHSCENE_NODE_INSTANCE);
  EXPECT_EQ(ii.node.target, k[0]) << "the copy of the target, not the original";
  EXPECT_NE(ii.node.target, target);
  (void)inst;
  VersionRef v2;
  publish(s, e, &v2);  // the scene validates the targets at publish
}

TEST(EditImport, AnInstanceWhoseTargetIsOutsideTheSubtreeIsInvalidAndTheViewIsAsItWas) {
  SceneHolder s;
  Ref sq(intern_square(s.store()));
  VersionRef v;
  uint64_t g = 0, target = 0;
  {
    EditHolder e(s);
    GSHSCENE_Node hid = group_node();
    hid.flags = GSHSCENE_NODE_HIDDEN;
    target = add(e.edit, GSHSCENE_ROOT_ID, hid);
    add(e.edit, target, shape_node(sq.get()));
    g = add(e.edit, GSHSCENE_ROOT_ID, group_node());
    add(e.edit, g, shape_node(sq.get()));
    add(e.edit, g, instance_node(target));
    publish(s, e, &v);
  }
  EditHolder e(s, v.get());
  const std::string before = view_state(e.edit, 30);
  uint64_t out = 5;
  EXPECT_EQ(gshscene_edit_import(e.edit, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, v.get(), g, &out), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(out, 5u);
  EXPECT_EQ(view_state(e.edit, 30), before) << "the copies made before the instance was met are gone";
  /* and an edit that failed so publishes as it was: nothing is touched */
  VersionRef v2;
  const GSHSCENE_Version * nv = nullptr;
  EXPECT_EQ(gshscene_scene_publish(s.scene, e.edit, nullptr, &nv), GSHSCENE_OK);
  gshscene_version_release(nv);
}

TEST(EditImport, TheArgumentsAreChecked) {
  Source src;
  EditHolder e(src.s, src.v.get());
  const std::string before = view_state(e.edit, 30);
  uint64_t out = 0;
  EXPECT_EQ(gshscene_edit_import(nullptr, GSHSCENE_ROOT_ID, 0, src.v.get(), src.a, &out), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_edit_import(e.edit, GSHSCENE_ROOT_ID, 0, nullptr, src.a, &out), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_edit_import(e.edit, GSHSCENE_ROOT_ID, 0, src.v.get(), 9999, &out), GSHSCENE_ERR_INVALID) << "no such source node";
  EXPECT_EQ(gshscene_edit_import(e.edit, 9999, 0, src.v.get(), src.a, &out), GSHSCENE_ERR_INVALID) << "no such parent";
  EXPECT_EQ(gshscene_edit_import(e.edit, src.s4, 0, src.v.get(), src.a, &out), GSHSCENE_ERR_INVALID) << "the parent is a shape";
  EXPECT_EQ(gshscene_edit_import(e.edit, GSHSCENE_ROOT_ID, 99, src.v.get(), src.a, &out), GSHSCENE_ERR_INVALID) << "position past the end";
  EXPECT_EQ(view_state(e.edit, 30), before);
  EXPECT_EQ(gshscene_edit_import(e.edit, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, src.v.get(), src.s4, nullptr), GSHSCENE_OK) << "out_id is optional";
}

TEST(EditImport, TheNodeLimitRefusesTheImportAndTheCopiesMadeBeforeItAreGone) {
  GSHSCENE_Limits l;
  gshscene_limits_default(&l);
  l.max_nodes = 8;
  SceneHolder s(&l);
  Ref sq(intern_square(s.store()));
  VersionRef v;
  uint64_t g = 0;
  {
    EditHolder e(s);
    g = add(e.edit, GSHSCENE_ROOT_ID, group_node());
    for (int i = 0; i < 4; i++) {
      add(e.edit, g, shape_node(sq.get()));
    }
    publish(s, e, &v);
  }
  EditHolder e(s, v.get());  // 6 nodes: the copy of g (5) would make 11
  const std::string before = view_state(e.edit, 30);
  uint64_t out = 0;
  EXPECT_EQ(gshscene_edit_import(e.edit, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, v.get(), g, &out), GSHSCENE_ERR_LIMIT);
  EXPECT_EQ(view_state(e.edit, 30), before);
  EXPECT_EQ(gshscene_edit_import(e.edit, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, v.get(), g + 1, &out), GSHSCENE_OK) << "one shape fits";
}

TEST(EditImport, EveryFailingAllocationLeavesTheViewAsItWasAndNothingLeaks) {
  tracker::Tracker t;
  GSHSCENE_Allocator a = tracker::allocator_for(&t);
  {
    Source src(nullptr, &a);
    ASSERT_NE(src.s.scene, nullptr);
    EditHolder e(src.s, src.v.get());
    ASSERT_NE(e.edit, nullptr);
    bool succeeded = false;
    for (long k = 0; k < 400 && !succeeded; k++) {
      const std::string before = view_state(e.edit, 60);
      t.persistent = false;
      t.failed = false;
      t.fail_from = t.calls + k;
      uint64_t out = 0;
      GSHSCENE_Result r = gshscene_edit_import(e.edit, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, src.v.get(), GSHSCENE_ROOT_ID, &out);
      t.fail_from = -1;
      if (r == GSHSCENE_OK) {
        succeeded = true;
        EXPECT_FALSE(t.failed) << "it succeeded only when no allocation failed";
      } else {
        EXPECT_EQ(r, GSHSCENE_ERR_OOM) << "at allocation " << k;
        EXPECT_EQ(view_state(e.edit, 60), before) << "at allocation " << k;
      }
    }
    EXPECT_TRUE(succeeded) << "the sweep reached an import that no failure touched";
    VersionRef v;
    publish(src.s, e, &v);
  }
  EXPECT_TRUE(t.live.empty()) << t.live.size() << " blocks leaked";
}

TEST(EditImport, EveryFailingAllocationOfAnImportAcrossStoresLeavesTheViewAsItWasAndNothingLeaks) {
  tracker::Tracker t;
  GSHSCENE_Allocator a = tracker::allocator_for(&t);
  {
    Rich src(&a);
    SceneHolder other(nullptr, &a);
    ASSERT_NE(other.scene, nullptr);
    EditHolder e(other);
    ASSERT_NE(e.edit, nullptr);
    bool succeeded = false;
    for (long k = 0; k < 2000 && !succeeded; k++) {
      const std::string before = view_state(e.edit, 80);
      GSHSCENE_Store_Stats stats_before;
      gshscene_store_stats(other.store(), &stats_before);
      t.persistent = false;
      t.failed = false;
      t.fail_from = t.calls + k;
      uint64_t out = 0;
      GSHSCENE_Result r = gshscene_edit_import(e.edit, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, src.v.get(), src.g, &out);
      t.fail_from = -1;
      if (r == GSHSCENE_OK) {
        succeeded = true;
      } else {
        EXPECT_EQ(r, GSHSCENE_ERR_OOM) << "at allocation " << k;
        EXPECT_EQ(view_state(e.edit, 80), before) << "at allocation " << k;
        GSHSCENE_Store_Stats stats_after;
        gshscene_store_stats(other.store(), &stats_after);
        EXPECT_EQ(stats_after.live, stats_before.live) << "at allocation " << k << ": no resource is held by a failed import";
      }
    }
    EXPECT_TRUE(succeeded);
    VersionRef v;
    publish(other, e, &v);
  }
  EXPECT_TRUE(t.live.empty()) << t.live.size() << " blocks leaked";
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
