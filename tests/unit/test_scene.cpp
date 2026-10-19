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
 * The scene: publishing, immutable versions, merging sibling edits, refusing
 * overlaps, validation at publish, the key map and threads (scene.h; story 11).
 */

#include "scene_fixtures.h"

#include "../alloc_tracker.h"

#include <algorithm>
#include <atomic>
#include <thread>

using namespace scene;

namespace {

using Ids = std::vector<uint64_t>;

/** A scene with a square and a store, and a helper to open edits. */
struct Fx {
  SceneHolder s;
  Ref sq;
  Fx(const GSHSCENE_Limits * limits = nullptr) : s(limits), sq(intern_square(s.store())) {}
  GSHSCENE_Edit * open(const GSHSCENE_Version * base = nullptr) {
    GSHSCENE_Edit * e = nullptr;
    EXPECT_EQ(gshscene_scene_open_edit(s.scene, base, &e), GSHSCENE_OK);
    return e;
  }
  const GSHSCENE_Version * current() {
    const GSHSCENE_Version * v = nullptr;
    EXPECT_EQ(gshscene_scene_current(s.scene, &v), GSHSCENE_OK);
    return v;
  }
  uint64_t shape(GSHSCENE_Edit * e, uint64_t parent = GSHSCENE_ROOT_ID, const GSHSCENE_Key * key = nullptr, int32_t layer = 0) {
    return add(e, parent, shape_node(sq.get(), layer), key);
  }
  uint64_t group(GSHSCENE_Edit * e, uint64_t parent = GSHSCENE_ROOT_ID, const GSHSCENE_Key * key = nullptr) {
    return add(e, parent, group_node(), key);
  }
};

/** Destroys edits at the end of a test. */
struct Edits {
  std::vector<GSHSCENE_Edit *> list;
  GSHSCENE_Edit * keep(GSHSCENE_Edit * e) {
    list.push_back(e);
    return e;
  }
  ~Edits() {
    for (GSHSCENE_Edit * e : list) {
      gshscene_edit_destroy(e);
    }
  }
};

}  // namespace

// --- create ---------------------------------------------------------------------------------

TEST(SceneCreate, StartsWithVersionZeroHoldingTheRoot) {
  SceneHolder s;
  ASSERT_NE(s.scene, nullptr);
  VersionRef v;
  ASSERT_EQ(gshscene_scene_current(s.scene, &v.version), GSHSCENE_OK);
  EXPECT_EQ(gshscene_version_serial(v.get()), 0u);
  EXPECT_EQ(gshscene_version_node_count(v.get()), 1u);
  EXPECT_EQ(gshscene_version_key_count(v.get()), 0u);
  GSHSCENE_Node_Info root = info_of(v.get(), GSHSCENE_ROOT_ID);
  EXPECT_EQ(root.node.kind, GSHSCENE_NODE_GROUP);
  EXPECT_EQ(root.parent, 0u);
  EXPECT_EQ(root.child_count, 0u);
  EXPECT_EQ(root.node.opacity, 1.0);
  EXPECT_FALSE(root.has_key);
  EXPECT_NE(s.store(), nullptr);
}

TEST(SceneCreate, RejectsNullArgumentsAndDestroyIgnoresNull) {
  GSHSCENE_Scene * scene = nullptr;
  EXPECT_EQ(gshscene_scene_create(nullptr, nullptr, nullptr, nullptr), GSHSCENE_ERR_INVALID);
  gshscene_scene_destroy(nullptr);
  EXPECT_EQ(gshscene_scene_store(nullptr), nullptr);
  const GSHSCENE_Version * v = nullptr;
  EXPECT_EQ(gshscene_scene_current(nullptr, &v), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_scene_create(nullptr, nullptr, nullptr, &scene), GSHSCENE_OK);
  EXPECT_EQ(gshscene_scene_current(scene, nullptr), GSHSCENE_ERR_INVALID);
  GSHSCENE_Edit * e = nullptr;
  EXPECT_EQ(gshscene_scene_open_edit(scene, nullptr, nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_scene_open_edit(nullptr, nullptr, &e), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(e, nullptr);
  gshscene_scene_destroy(scene);
  gshscene_edit_destroy(nullptr);
  gshscene_version_release(nullptr);
  EXPECT_EQ(gshscene_version_retain(nullptr), nullptr);
  EXPECT_EQ(gshscene_version_serial(nullptr), 0u);
  EXPECT_EQ(gshscene_version_node_count(nullptr), 0u);
  EXPECT_EQ(gshscene_version_key_count(nullptr), 0u);
}

TEST(SceneCreate, AStoreGivenIsSharedAndOutlivesTheScene) {
  StoreHolder store;
  Ref sq(intern_square(store.store));
  {
    SceneHolder s(nullptr, nullptr, store.store);
    EXPECT_EQ(s.store(), store.store);
    EditHolder e(s);
    add(e.edit, GSHSCENE_ROOT_ID, shape_node(sq.get()));
    publish(s, e);
  }
  GSHSCENE_Store_Stats stats;
  ASSERT_EQ(gshscene_store_stats(store.store, &stats), GSHSCENE_OK);
  EXPECT_EQ(stats.live, 1u);
  EXPECT_EQ(gshscene_resource_refcount(sq.get()), 1u) << "the scene let go of what its nodes held";
}

TEST(SceneCreate, TheSceneOwnsAStoreItMakesAndAVersionOutlivesTheScene) {
  VersionRef v;
  GSHSCENE_Store_Stats before;
  {
    SceneHolder s;
    Ref sq(intern_square(s.store()));
    EditHolder e(s);
    add(e.edit, GSHSCENE_ROOT_ID, shape_node(sq.get()));
    publish(s, e, &v);
    ASSERT_EQ(gshscene_store_stats(s.store(), &before), GSHSCENE_OK);
  }
  // The scene handle, the edit, and the creator's resource are gone; the version is whole.
  EXPECT_EQ(gshscene_version_node_count(v.get()), 2u);
  EXPECT_EQ(gshscene_version_serial(v.get()), 1u);
  const GSHSCENE_List * list = nullptr;
  ASSERT_EQ(gshscene_version_list(v.get(), &list), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_count(list), 1u);
  EXPECT_NE(dump_version(v.get()).find("gshscene.version 2 nodes=2"), std::string::npos);
}

TEST(SceneEdit, OpenOnANamedVersionOrTheCurrentOne) {
  Fx f;
  VersionRef v0(f.current());
  Edits keep;
  GSHSCENE_Edit * a = keep.keep(f.open());
  f.shape(a);
  VersionRef v1;
  ASSERT_EQ(gshscene_scene_publish(f.s.scene, a, nullptr, &v1.version), GSHSCENE_OK);
  GSHSCENE_Edit * on_old = keep.keep(f.open(v0.get()));
  EXPECT_EQ(gshscene_edit_base(on_old), v0.get());
  EXPECT_EQ(gshscene_edit_node_count(on_old), 1u);
  GSHSCENE_Edit * on_current = keep.keep(f.open());
  EXPECT_EQ(gshscene_edit_base(on_current), v1.get());
  EXPECT_EQ(gshscene_edit_node_count(on_current), 2u);
  // A version of another scene is refused.
  Fx other;
  VersionRef foreign(other.current());
  GSHSCENE_Edit * bad = nullptr;
  EXPECT_EQ(gshscene_scene_open_edit(f.s.scene, foreign.get(), &bad), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(bad, nullptr);
}

// --- publish and immutable versions -------------------------------------------------------------

TEST(ScenePublish, AMakesANewVersionAndTheOldOneNeverChanges) {
  Fx f;
  VersionRef v0(f.current());
  std::string d0 = dump_version(v0.get());
  uint64_t h0 = gshscene_version_hash(v0.get());
  Edits keep;
  GSHSCENE_Edit * e = keep.keep(f.open());
  uint64_t g = f.group(e);
  uint64_t s = f.shape(e, g);
  VersionRef v1;
  ASSERT_EQ(gshscene_scene_publish(f.s.scene, e, nullptr, &v1.version), GSHSCENE_OK);
  EXPECT_EQ(gshscene_version_serial(v1.get()), 1u);
  EXPECT_EQ(gshscene_version_node_count(v1.get()), 3u);
  EXPECT_EQ(children_of(v1.get(), GSHSCENE_ROOT_ID), (Ids{g}));
  EXPECT_EQ(children_of(v1.get(), g), (Ids{s}));
  std::string d1 = dump_version(v1.get());
  uint64_t h1 = gshscene_version_hash(v1.get());
  // More publishes: change, remove, add.
  GSHSCENE_Edit * e2 = keep.keep(f.open());
  GSHSCENE_Node changed = shape_node(f.sq.get(), 5);
  ASSERT_EQ(gshscene_edit_set(e2, s, &changed), GSHSCENE_OK);
  f.shape(e2);
  VersionRef v2;
  ASSERT_EQ(gshscene_scene_publish(f.s.scene, e2, nullptr, &v2.version), GSHSCENE_OK);
  GSHSCENE_Edit * e3 = keep.keep(f.open());
  ASSERT_EQ(gshscene_edit_remove(e3, g), GSHSCENE_OK);
  VersionRef v3;
  ASSERT_EQ(gshscene_scene_publish(f.s.scene, e3, nullptr, &v3.version), GSHSCENE_OK);
  EXPECT_EQ(gshscene_version_serial(v3.get()), 3u);
  EXPECT_EQ(dump_version(v0.get()), d0);
  EXPECT_EQ(gshscene_version_hash(v0.get()), h0);
  EXPECT_EQ(dump_version(v1.get()), d1);
  EXPECT_EQ(gshscene_version_hash(v1.get()), h1);
  EXPECT_NE(h0, h1);
  EXPECT_NE(gshscene_version_hash(v2.get()), h1);
  EXPECT_EQ(info_of(v1.get(), s).node.layer, 0);
  EXPECT_EQ(info_of(v2.get(), s).node.layer, 5);
  GSHSCENE_Node_Info gone;
  EXPECT_EQ(gshscene_version_node(v3.get(), g, &gone), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_version_node(v1.get(), g, &gone), GSHSCENE_OK);
  // The current version is the last published, with a reference for the caller.
  VersionRef cur(f.current());
  EXPECT_EQ(cur.get(), v3.get());
}

TEST(ScenePublish, UnchangedNodesAreSharedBetweenVersionsAndChangedOnesAreNot) {
  Fx f;
  Edits keep;
  GSHSCENE_Edit * e = keep.keep(f.open());
  Ref other(intern_square(f.s.store(), 20));
  uint64_t a = add(e, GSHSCENE_ROOT_ID, shape_node(f.sq.get()));
  uint64_t b = add(e, GSHSCENE_ROOT_ID, shape_node(other.get()));
  VersionRef v1;
  ASSERT_EQ(gshscene_scene_publish(f.s.scene, e, nullptr, &v1.version), GSHSCENE_OK);
  // A node holds one reference on its resource: sharing is seen as references not taken.
  size_t sq_refs = gshscene_resource_refcount(f.sq.get());
  size_t other_refs = gshscene_resource_refcount(other.get());
  GSHSCENE_Edit * e2 = keep.keep(f.open());
  GSHSCENE_Node changed = shape_node(f.sq.get(), 9);
  ASSERT_EQ(gshscene_edit_set(e2, a, &changed), GSHSCENE_OK);
  VersionRef v2;
  ASSERT_EQ(gshscene_scene_publish(f.s.scene, e2, nullptr, &v2.version), GSHSCENE_OK);
  EXPECT_EQ(gshscene_resource_refcount(other.get()), other_refs) << "the untouched node is the same node in both versions";
  EXPECT_EQ(gshscene_resource_refcount(f.sq.get()), sq_refs + 1) << "the changed node is a new one, holding its own reference";
  (void)b;
}

TEST(ScenePublish, AnEmptyEditPublishesNothingAndReturnsTheCurrentVersion) {
  Fx f;
  Edits keep;
  VersionRef v0(f.current());
  GSHSCENE_Edit * e = keep.keep(f.open());
  VersionRef out;
  ASSERT_EQ(gshscene_scene_publish(f.s.scene, e, nullptr, &out.version), GSHSCENE_OK);
  EXPECT_EQ(out.get(), v0.get());
  VersionRef cur(f.current());
  EXPECT_EQ(cur.get(), v0.get());
  EXPECT_EQ(gshscene_version_serial(cur.get()), 0u);
  // An edit that added and removed a node, or set it back, is empty too.
  GSHSCENE_Edit * e2 = keep.keep(f.open());
  uint64_t id = f.shape(e2);
  ASSERT_EQ(gshscene_edit_remove(e2, id), GSHSCENE_OK);
  VersionRef out2;
  ASSERT_EQ(gshscene_scene_publish(f.s.scene, e2, nullptr, &out2.version), GSHSCENE_OK);
  EXPECT_EQ(out2.get(), v0.get());
  // The out parameter may be NULL.
  GSHSCENE_Edit * e3 = keep.keep(f.open());
  f.shape(e3);
  EXPECT_EQ(gshscene_scene_publish(f.s.scene, e3, nullptr, nullptr), GSHSCENE_OK);
}

TEST(ScenePublish, IdsAndHashesReproduceRunToRun) {
  auto build = [](std::string * dump, uint64_t * hash, Ids * ids) {
    SceneHolder s;
    Ref sq(intern_square(s.store()));
    Ref fx(intern_fixture(s.store()));
    EditHolder e(s);
    GSHSCENE_Key k = K(7, 70);
    uint64_t g = add(e.edit, GSHSCENE_ROOT_ID, group_node(0.5, 2), &k);
    uint64_t a = add(e.edit, g, shape_node(sq.get(), 1));
    uint64_t b = add(e.edit, g, shape_node(fx.get(), 3));
    uint64_t i = add(e.edit, GSHSCENE_ROOT_ID, instance_node(g, 4));
    ids->assign({g, a, b, i});
    VersionRef v;
    publish(s, e, &v);
    *dump = dump_version(v.get());
    *hash = gshscene_version_hash(v.get());
  };
  std::string d1, d2;
  uint64_t h1 = 0, h2 = 0;
  Ids i1, i2;
  build(&d1, &h1, &i1);
  build(&d2, &h2, &i2);
  EXPECT_EQ(i1, (Ids{2, 3, 4, 5}));
  EXPECT_EQ(i1, i2);
  EXPECT_EQ(d1, d2);
  EXPECT_EQ(h1, h2);
}

TEST(ScenePublish, AFailedPublishChangesNeitherTheSceneNorTheEdit) {
  Fx f;
  Edits keep;
  VersionRef v0(f.current());
  GSHSCENE_Edit * e = keep.keep(f.open());
  uint64_t inst = add(e, GSHSCENE_ROOT_ID, instance_node(9999));
  std::string before = dump_version(v0.get());
  ProblemsRef p;
  EXPECT_EQ(gshscene_scene_publish(f.s.scene, e, &p.problems, nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(p.ids(), (Ids{inst}));
  VersionRef cur(f.current());
  EXPECT_EQ(cur.get(), v0.get());
  EXPECT_EQ(dump_version(cur.get()), before);
  // The edit is as it was: fix it and publish.
  ASSERT_EQ(gshscene_edit_remove(e, inst), GSHSCENE_OK);
  f.shape(e);
  VersionRef v1;
  EXPECT_EQ(gshscene_scene_publish(f.s.scene, e, nullptr, &v1.version), GSHSCENE_OK);
  EXPECT_EQ(gshscene_version_node_count(v1.get()), 2u);
  EXPECT_EQ(gshscene_version_serial(v1.get()), 1u) << "the failed publish did not take a version number";
}

// --- merging ------------------------------------------------------------------------------------------

namespace {

/** A version with two groups, A and B, each with a shape; the IDs. */
struct Base {
  Fx f;
  uint64_t a, b, a1, b1;
  VersionRef v;
  Base(const GSHSCENE_Limits * limits = nullptr) : f(limits) {
    EditHolder e(f.s);
    a = f.group(e.edit);
    b = f.group(e.edit);
    a1 = f.shape(e.edit, a);
    b1 = f.shape(e.edit, b);
    publish(f.s, e, &v);
  }
};

}  // namespace

TEST(SceneMerge, DisjointSiblingsMergeToOneVersionInEitherOrder) {
  Base base;
  Edits keep;
  GSHSCENE_Edit * ea = keep.keep(base.f.open(base.v.get()));
  GSHSCENE_Edit * eb = keep.keep(base.f.open(base.v.get()));
  uint64_t na = base.f.shape(ea, base.a);  // touches a
  GSHSCENE_Node changed = shape_node(base.f.sq.get(), 8);
  ASSERT_EQ(gshscene_edit_set(eb, base.b1, &changed), GSHSCENE_OK);  // touches b1
  GSHSCENE_Edit * both[2] = {ea, eb};
  GSHSCENE_Edit * swapped[2] = {eb, ea};
  // Merge in one order in this scene...
  VersionRef merged;
  ASSERT_EQ(gshscene_scene_merge(base.f.s.scene, both, 2, nullptr, &merged.version), GSHSCENE_OK);
  EXPECT_EQ(gshscene_version_serial(merged.get()), 2u);
  EXPECT_EQ(children_of(merged.get(), base.a), (Ids{base.a1, na}));
  EXPECT_EQ(info_of(merged.get(), base.b1).node.layer, 8);
  // ... and the other, in a second scene built the same way, and as two publishes in a third.
  Base base2;
  GSHSCENE_Edit * ea2 = keep.keep(base2.f.open(base2.v.get()));
  GSHSCENE_Edit * eb2 = keep.keep(base2.f.open(base2.v.get()));
  base2.f.shape(ea2, base2.a);
  GSHSCENE_Node changed2 = shape_node(base2.f.sq.get(), 8);
  ASSERT_EQ(gshscene_edit_set(eb2, base2.b1, &changed2), GSHSCENE_OK);
  GSHSCENE_Edit * rev[2] = {eb2, ea2};
  VersionRef merged2;
  ASSERT_EQ(gshscene_scene_merge(base2.f.s.scene, rev, 2, nullptr, &merged2.version), GSHSCENE_OK);
  EXPECT_EQ(gshscene_version_hash(merged2.get()), gshscene_version_hash(merged.get()));
  EXPECT_EQ(dump_version(merged2.get()), dump_version(merged.get()));
  Base base3;
  GSHSCENE_Edit * ea3 = keep.keep(base3.f.open(base3.v.get()));
  GSHSCENE_Edit * eb3 = keep.keep(base3.f.open(base3.v.get()));
  base3.f.shape(ea3, base3.a);
  GSHSCENE_Node changed3 = shape_node(base3.f.sq.get(), 8);
  ASSERT_EQ(gshscene_edit_set(eb3, base3.b1, &changed3), GSHSCENE_OK);
  VersionRef p1, p2;
  ASSERT_EQ(gshscene_scene_publish(base3.f.s.scene, ea3, nullptr, &p1.version), GSHSCENE_OK);
  ASSERT_EQ(gshscene_scene_publish(base3.f.s.scene, eb3, nullptr, &p2.version), GSHSCENE_OK);
  EXPECT_EQ(gshscene_version_serial(p2.get()), 3u);
  EXPECT_EQ(gshscene_version_hash(p2.get()), gshscene_version_hash(merged.get())) << "one after the other is the same version";
  (void)swapped;
}

TEST(SceneMerge, TheResultDoesNotDependOnTheOrderOfTheEdits) {
  std::vector<uint64_t> hashes;
  Ids order = {0, 1, 2};
  do {
    Base base;
    Edits keep;
    GSHSCENE_Edit * e[3];
    for (int i = 0; i < 3; i++) {
      e[i] = keep.keep(base.f.open(base.v.get()));
    }
    GSHSCENE_Key k0 = K(1, 1), k2 = K(1, 3);
    base.f.shape(e[0], base.a, &k0);
    GSHSCENE_Node changed = shape_node(base.f.sq.get(), 4);
    ASSERT_EQ(gshscene_edit_set(e[1], base.b1, &changed), GSHSCENE_OK);
    base.f.shape(e[2], base.b, &k2);
    GSHSCENE_Edit * arr[3] = {e[order[0]], e[order[1]], e[order[2]]};
    VersionRef v;
    ASSERT_EQ(gshscene_scene_merge(base.f.s.scene, arr, 3, nullptr, &v.version), GSHSCENE_OK);
    hashes.push_back(gshscene_version_hash(v.get()));
    EXPECT_EQ(gshscene_version_key_count(v.get()), 2u);
  } while (std::next_permutation(order.begin(), order.end()));
  ASSERT_EQ(hashes.size(), 6u);
  for (uint64_t h : hashes) {
    EXPECT_EQ(h, hashes[0]);
  }
}

namespace {

/** Merge two edits that overlap and check the refusal names exactly @p want, ascending, and nothing changed. */
void expect_refused(Base & base, GSHSCENE_Edit * x, GSHSCENE_Edit * y, const Ids & want) {
  VersionRef before(base.f.current());
  std::string dump_before = dump_version(before.get());
  GSHSCENE_Edit * both[2] = {x, y};
  ProblemsRef p;
  const GSHSCENE_Version * out = nullptr;
  EXPECT_EQ(gshscene_scene_merge(base.f.s.scene, both, 2, &p.problems, &out), GSHSCENE_ERR_REFUSED);
  EXPECT_EQ(out, nullptr);
  EXPECT_EQ(p.ids(GSHSCENE_PROBLEM_OVERLAP), want);
  EXPECT_EQ(p.ids().size(), want.size());
  VersionRef cur(base.f.current());
  EXPECT_EQ(cur.get(), before.get());
  EXPECT_EQ(dump_version(cur.get()), dump_before);
  for (size_t i = 0; i + 1 < gshscene_problems_count(p.problems); i++) {
    uint64_t a, b;
    GSHSCENE_Problem_Reason ra, rb;
    gshscene_problems_get(p.problems, i, &a, &ra);
    gshscene_problems_get(p.problems, i + 1, &b, &rb);
    EXPECT_LT(a, b) << "ascending";
  }
}

}  // namespace

TEST(SceneMerge, OverlappingEditsAreRefusedNamingTheIds_SetAgainstRemove) {
  Base base;
  Edits keep;
  GSHSCENE_Edit * ea = keep.keep(base.f.open(base.v.get()));
  GSHSCENE_Edit * eb = keep.keep(base.f.open(base.v.get()));
  GSHSCENE_Node changed = shape_node(base.f.sq.get(), 3);
  ASSERT_EQ(gshscene_edit_set(ea, base.a1, &changed), GSHSCENE_OK);
  ASSERT_EQ(gshscene_edit_remove(eb, base.a1), GSHSCENE_OK);  // touches a1 and its parent a
  expect_refused(base, ea, eb, Ids{base.a1});
  // Each edit is as it was: either publishes alone.
  VersionRef v;
  EXPECT_EQ(gshscene_scene_publish(base.f.s.scene, ea, nullptr, &v.version), GSHSCENE_OK);
}

TEST(SceneMerge, OverlappingEditsAreRefusedNamingTheIds_TwoInsertionsUnderOneParent) {
  Base base;
  Edits keep;
  GSHSCENE_Edit * ea = keep.keep(base.f.open(base.v.get()));
  GSHSCENE_Edit * eb = keep.keep(base.f.open(base.v.get()));
  base.f.shape(ea, base.a);
  base.f.shape(eb, base.a);
  expect_refused(base, ea, eb, Ids{base.a});
}

TEST(SceneMerge, OverlappingEditsAreRefusedNamingTheIds_AMoveIntoAGroupTheOtherRemoved) {
  Base base;
  Edits keep;
  GSHSCENE_Edit * ea = keep.keep(base.f.open(base.v.get()));
  GSHSCENE_Edit * eb = keep.keep(base.f.open(base.v.get()));
  // A removes group b (touching b, b1 and the root, whose list loses b); B moves a1 (from a) into b.
  ASSERT_EQ(gshscene_edit_remove(ea, base.b), GSHSCENE_OK);
  ASSERT_EQ(gshscene_edit_move(eb, base.a1, base.b, 0), GSHSCENE_OK);  // touches a1, a, b
  expect_refused(base, ea, eb, Ids{base.b});
}

TEST(SceneMerge, OverlappingEditsAreRefusedNamingTheIds_EveryIdTheyShare) {
  Base base;
  Edits keep;
  GSHSCENE_Edit * ea = keep.keep(base.f.open(base.v.get()));
  GSHSCENE_Edit * eb = keep.keep(base.f.open(base.v.get()));
  GSHSCENE_Node c1 = shape_node(base.f.sq.get(), 1), c2 = shape_node(base.f.sq.get(), 2);
  ASSERT_EQ(gshscene_edit_set(ea, base.a1, &c1), GSHSCENE_OK);
  ASSERT_EQ(gshscene_edit_set(ea, base.b1, &c1), GSHSCENE_OK);
  ASSERT_EQ(gshscene_edit_set(eb, base.b1, &c2), GSHSCENE_OK);
  ASSERT_EQ(gshscene_edit_set(eb, base.a1, &c2), GSHSCENE_OK);
  expect_refused(base, ea, eb, (Ids{base.a1, base.b1}));
}

TEST(SceneMerge, TwoEditsThatSetOneNodeToTheSameValueDoNotOverlapAsNeitherChangesIt) {
  Base base;
  Edits keep;
  GSHSCENE_Edit * ea = keep.keep(base.f.open(base.v.get()));
  GSHSCENE_Edit * eb = keep.keep(base.f.open(base.v.get()));
  GSHSCENE_Node same = shape_node(base.f.sq.get(), 0);  // what it already is
  ASSERT_EQ(gshscene_edit_set(ea, base.a1, &same), GSHSCENE_OK);
  GSHSCENE_Node changed = shape_node(base.f.sq.get(), 6);
  ASSERT_EQ(gshscene_edit_set(eb, base.a1, &changed), GSHSCENE_OK);
  GSHSCENE_Edit * both[2] = {ea, eb};
  VersionRef v;
  ASSERT_EQ(gshscene_scene_merge(base.f.s.scene, both, 2, nullptr, &v.version), GSHSCENE_OK);
  EXPECT_EQ(info_of(v.get(), base.a1).node.layer, 6);
}

TEST(SceneMerge, AnEditIsRebasedOverPublishesSinceItsBase) {
  Base base;
  Edits keep;
  GSHSCENE_Edit * ea = keep.keep(base.f.open(base.v.get()));
  GSHSCENE_Edit * eb = keep.keep(base.f.open(base.v.get()));
  uint64_t na = base.f.shape(ea, base.a);
  uint64_t nb = base.f.shape(eb, base.b);
  VersionRef va;
  ASSERT_EQ(gshscene_scene_publish(base.f.s.scene, ea, nullptr, &va.version), GSHSCENE_OK);
  VersionRef vb;
  ASSERT_EQ(gshscene_scene_publish(base.f.s.scene, eb, nullptr, &vb.version), GSHSCENE_OK);
  EXPECT_EQ(gshscene_version_serial(vb.get()), 3u);
  EXPECT_EQ(children_of(vb.get(), base.a), (Ids{base.a1, na})) << "A's work is in B's version";
  EXPECT_EQ(children_of(vb.get(), base.b), (Ids{base.b1, nb}));
  // A third edit on the old base overlaps what A changed: refused, naming the IDs.
  GSHSCENE_Edit * ec = keep.keep(base.f.open(base.v.get()));
  base.f.shape(ec, base.a);
  ProblemsRef p;
  EXPECT_EQ(gshscene_scene_publish(base.f.s.scene, ec, &p.problems, nullptr), GSHSCENE_ERR_REFUSED);
  EXPECT_EQ(p.ids(), (Ids{base.a}));
  // And one that touches a node A removed.
  VersionRef cur(base.f.current());
  GSHSCENE_Edit * ed = keep.keep(base.f.open(cur.get()));
  ASSERT_EQ(gshscene_edit_remove(ed, base.a1), GSHSCENE_OK);
  VersionRef vd;
  ASSERT_EQ(gshscene_scene_publish(base.f.s.scene, ed, nullptr, &vd.version), GSHSCENE_OK);
  GSHSCENE_Edit * ee = keep.keep(base.f.open(cur.get()));
  GSHSCENE_Node changed = shape_node(base.f.sq.get(), 2);
  ASSERT_EQ(gshscene_edit_set(ee, base.a1, &changed), GSHSCENE_OK);
  ProblemsRef p2;
  EXPECT_EQ(gshscene_scene_publish(base.f.s.scene, ee, &p2.problems, nullptr), GSHSCENE_ERR_REFUSED);
  EXPECT_EQ(p2.ids(), (Ids{base.a1}));
}

TEST(SceneMerge, TwoEditsBindingOneKeyToDifferentNodesAreRefusedNamingBoth) {
  Base base;
  Edits keep;
  GSHSCENE_Edit * ea = keep.keep(base.f.open(base.v.get()));
  GSHSCENE_Edit * eb = keep.keep(base.f.open(base.v.get()));
  GSHSCENE_Key k = K(4, 4);
  uint64_t x = base.f.shape(ea, base.a, &k);
  uint64_t y = base.f.shape(eb, base.b, &k);
  GSHSCENE_Edit * both[2] = {ea, eb};
  ProblemsRef p;
  EXPECT_EQ(gshscene_scene_merge(base.f.s.scene, both, 2, &p.problems, nullptr), GSHSCENE_ERR_REFUSED);
  EXPECT_EQ(p.ids(GSHSCENE_PROBLEM_KEY_CONFLICT), (Ids{x, y}));
  // Published one after the other, the second is refused naming the first's node and its own.
  VersionRef v;
  ASSERT_EQ(gshscene_scene_publish(base.f.s.scene, ea, nullptr, &v.version), GSHSCENE_OK);
  ProblemsRef p2;
  EXPECT_EQ(gshscene_scene_publish(base.f.s.scene, eb, &p2.problems, nullptr), GSHSCENE_ERR_REFUSED);
  EXPECT_EQ(p2.ids(GSHSCENE_PROBLEM_KEY_CONFLICT), (Ids{x, y}));
  uint64_t bound = 0;
  VersionRef cur(base.f.current());
  ASSERT_EQ(gshscene_version_lookup_key(cur.get(), &k, &bound), GSHSCENE_OK);
  EXPECT_EQ(bound, x);
}

TEST(SceneMerge, DifferentKeysMergeAndEachResolves) {
  Base base;
  Edits keep;
  GSHSCENE_Edit * ea = keep.keep(base.f.open(base.v.get()));
  GSHSCENE_Edit * eb = keep.keep(base.f.open(base.v.get()));
  GSHSCENE_Key k1 = K(4, 1), k2 = K(4, 2);
  uint64_t x = base.f.shape(ea, base.a, &k1);
  uint64_t y = base.f.shape(eb, base.b, &k2);
  GSHSCENE_Edit * both[2] = {ea, eb};
  VersionRef v;
  ASSERT_EQ(gshscene_scene_merge(base.f.s.scene, both, 2, nullptr, &v.version), GSHSCENE_OK);
  uint64_t id = 0;
  ASSERT_EQ(gshscene_version_lookup_key(v.get(), &k1, &id), GSHSCENE_OK);
  EXPECT_EQ(id, x);
  ASSERT_EQ(gshscene_version_lookup_key(v.get(), &k2, &id), GSHSCENE_OK);
  EXPECT_EQ(id, y);
  EXPECT_EQ(gshscene_version_key_count(v.get()), 2u);
}

TEST(SceneMerge, TheArgumentsAreChecked) {
  Base base;
  Base other;
  Edits keep;
  GSHSCENE_Edit * ea = keep.keep(base.f.open(base.v.get()));
  GSHSCENE_Edit * eb = keep.keep(base.f.open(base.v.get()));
  base.f.shape(ea, base.a);
  base.f.shape(eb, base.b);
  GSHSCENE_Edit * twice[2] = {ea, ea};
  EXPECT_EQ(gshscene_scene_merge(base.f.s.scene, twice, 2, nullptr, nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_scene_merge(base.f.s.scene, nullptr, 2, nullptr, nullptr), GSHSCENE_ERR_INVALID);
  GSHSCENE_Edit * one[1] = {ea};
  EXPECT_EQ(gshscene_scene_merge(base.f.s.scene, one, 0, nullptr, nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_scene_merge(nullptr, one, 1, nullptr, nullptr), GSHSCENE_ERR_INVALID);
  GSHSCENE_Edit * withnull[2] = {ea, nullptr};
  EXPECT_EQ(gshscene_scene_merge(base.f.s.scene, withnull, 2, nullptr, nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_scene_publish(base.f.s.scene, nullptr, nullptr, nullptr), GSHSCENE_ERR_INVALID);
  // An edit of another scene.
  GSHSCENE_Edit * foreign = keep.keep(other.f.open(other.v.get()));
  other.f.shape(foreign, other.a);
  EXPECT_EQ(gshscene_scene_publish(base.f.s.scene, foreign, nullptr, nullptr), GSHSCENE_ERR_INVALID);
  // Edits on different bases.
  VersionRef v;
  ASSERT_EQ(gshscene_scene_publish(base.f.s.scene, ea, nullptr, &v.version), GSHSCENE_OK);
  GSHSCENE_Edit * later = keep.keep(base.f.open());
  base.f.shape(later, base.b);
  GSHSCENE_Edit * mixed[2] = {eb, later};
  EXPECT_EQ(gshscene_scene_merge(base.f.s.scene, mixed, 2, nullptr, nullptr), GSHSCENE_ERR_INVALID);
  // A spent edit.
  GSHSCENE_Edit * spent[2] = {ea, eb};
  EXPECT_EQ(gshscene_scene_merge(base.f.s.scene, spent, 2, nullptr, nullptr), GSHSCENE_ERR_INVALID);
}

// --- validation at publish -----------------------------------------------------------------------------

TEST(ScenePublishValidation, ADanglingInstanceIsRefusedNamingIt) {
  Fx f;
  Edits keep;
  GSHSCENE_Edit * e = keep.keep(f.open());
  uint64_t a = f.shape(e);
  uint64_t i1 = add(e, GSHSCENE_ROOT_ID, instance_node(a));
  uint64_t i2 = add(e, GSHSCENE_ROOT_ID, instance_node(a + 100));
  uint64_t i3 = add(e, GSHSCENE_ROOT_ID, instance_node(a + 200));
  ProblemsRef p;
  EXPECT_EQ(gshscene_scene_publish(f.s.scene, e, &p.problems, nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(p.ids(GSHSCENE_PROBLEM_DANGLING_TARGET), (Ids{i2, i3}));
  EXPECT_EQ(p.ids().size(), 2u);
  (void)i1;
}

TEST(ScenePublishValidation, ACycleThroughInstancesIsRefused) {
  Fx f;
  Edits keep;
  {
    // An instance of its own parent group.
    GSHSCENE_Edit * e = keep.keep(f.open());
    uint64_t g = f.group(e);
    uint64_t i = add(e, g, instance_node(g));
    ProblemsRef p;
    EXPECT_EQ(gshscene_scene_publish(f.s.scene, e, &p.problems, nullptr), GSHSCENE_ERR_INVALID);
    EXPECT_EQ(p.ids(GSHSCENE_PROBLEM_CYCLE), (Ids{i}));
  }
  {
    // An instance of itself.
    GSHSCENE_Edit * e = keep.keep(f.open());
    GSHSCENE_Node n = instance_node(2);  // the ID the next add will get
    uint64_t i = add(e, GSHSCENE_ROOT_ID, n);
    EXPECT_EQ(i, 4u) << "IDs 2 and 3 went to the first, refused edit";
    GSHSCENE_Node fix = instance_node(i);
    ASSERT_EQ(gshscene_edit_set(e, i, &fix), GSHSCENE_OK);
    ProblemsRef p;
    EXPECT_EQ(gshscene_scene_publish(f.s.scene, e, &p.problems, nullptr), GSHSCENE_ERR_INVALID);
    EXPECT_EQ(p.ids(GSHSCENE_PROBLEM_CYCLE), (Ids{i}));
  }
  {
    // Two groups that each instance the other.
    GSHSCENE_Edit * e = keep.keep(f.open());
    uint64_t g1 = f.group(e);
    uint64_t g2 = f.group(e);
    uint64_t i1 = add(e, g1, instance_node(g2));
    uint64_t i2 = add(e, g2, instance_node(g1));
    ProblemsRef p;
    EXPECT_EQ(gshscene_scene_publish(f.s.scene, e, &p.problems, nullptr), GSHSCENE_ERR_INVALID);
    std::vector<uint64_t> named = p.ids(GSHSCENE_PROBLEM_CYCLE);
    ASSERT_FALSE(named.empty());
    for (uint64_t id : named) {
      EXPECT_TRUE(id == i1 || id == i2) << id;
    }
  }
}

TEST(ScenePublishValidation, AnInstanceOfANodeThatIsNotAnAncestorIsNotACycle) {
  Fx f;
  Edits keep;
  GSHSCENE_Edit * e = keep.keep(f.open());
  uint64_t def = add(e, GSHSCENE_ROOT_ID, group_node());
  uint64_t s = f.shape(e, def);
  uint64_t i1 = add(e, GSHSCENE_ROOT_ID, instance_node(def));
  uint64_t i2 = add(e, GSHSCENE_ROOT_ID, instance_node(s));
  uint64_t i3 = add(e, GSHSCENE_ROOT_ID, instance_node(i1));  // an instance of an instance
  VersionRef v;
  ASSERT_EQ(gshscene_scene_publish(f.s.scene, e, nullptr, &v.version), GSHSCENE_OK);
  (void)i2;
  (void)i3;
}

TEST(ScenePublishValidation, AMergeThatLeavesAnInstanceDanglingIsRefusedAndNothingIsPublished) {
  Base base;
  Edits keep;
  GSHSCENE_Edit * ea = keep.keep(base.f.open(base.v.get()));
  GSHSCENE_Edit * eb = keep.keep(base.f.open(base.v.get()));
  ASSERT_EQ(gshscene_edit_remove(ea, base.a1), GSHSCENE_OK);                    // touches a1, a
  uint64_t inst = add(eb, base.b, instance_node(base.a1));                      // touches b only
  GSHSCENE_Edit * both[2] = {ea, eb};
  VersionRef before(base.f.current());
  ProblemsRef p;
  EXPECT_EQ(gshscene_scene_merge(base.f.s.scene, both, 2, &p.problems, nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(p.ids(GSHSCENE_PROBLEM_DANGLING_TARGET), (Ids{inst}));
  VersionRef cur(base.f.current());
  EXPECT_EQ(cur.get(), before.get());
  // Each alone publishes.
  VersionRef v;
  EXPECT_EQ(gshscene_scene_publish(base.f.s.scene, eb, nullptr, &v.version), GSHSCENE_OK);
}

TEST(ScenePublishValidation, TheProblemsOutParameterMayBeNull) {
  Fx f;
  Edits keep;
  GSHSCENE_Edit * e = keep.keep(f.open());
  add(e, GSHSCENE_ROOT_ID, instance_node(1234));
  EXPECT_EQ(gshscene_scene_publish(f.s.scene, e, nullptr, nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_problems_count(nullptr), 0u);
  uint64_t id;
  GSHSCENE_Problem_Reason r;
  EXPECT_EQ(gshscene_problems_get(nullptr, 0, &id, &r), GSHSCENE_ERR_INVALID);
  gshscene_problems_destroy(nullptr);
}

TEST(ScenePublishValidation, ProblemsGetChecksItsArguments) {
  Fx f;
  Edits keep;
  GSHSCENE_Edit * e = keep.keep(f.open());
  uint64_t inst = add(e, GSHSCENE_ROOT_ID, instance_node(777));
  ProblemsRef p;
  ASSERT_EQ(gshscene_scene_publish(f.s.scene, e, &p.problems, nullptr), GSHSCENE_ERR_INVALID);
  ASSERT_EQ(gshscene_problems_count(p.problems), 1u);
  uint64_t id = 0;
  GSHSCENE_Problem_Reason reason = GSHSCENE_PROBLEM_OVERLAP;
  EXPECT_EQ(gshscene_problems_get(p.problems, 0, &id, &reason), GSHSCENE_OK);
  EXPECT_EQ(id, inst);
  EXPECT_EQ(reason, GSHSCENE_PROBLEM_DANGLING_TARGET);
  EXPECT_EQ(gshscene_problems_get(p.problems, 1, &id, &reason), GSHSCENE_ERR_INVALID) << "past the end";
  EXPECT_EQ(gshscene_problems_get(p.problems, 0, nullptr, &reason), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_problems_get(p.problems, 0, &id, nullptr), GSHSCENE_ERR_INVALID);
}

TEST(SceneMerge, ARebindingOfAKeyBySomeoneElseIsAConflictAndNamesTheNewNode) {
  Base base;
  Edits keep;
  GSHSCENE_Key k = K(8, 8);
  VersionRef bound;
  uint64_t n;
  {
    GSHSCENE_Edit * e = keep.keep(base.f.open(base.v.get()));
    n = base.f.shape(e, base.a, &k);
    ASSERT_EQ(gshscene_scene_publish(base.f.s.scene, e, nullptr, &bound.version), GSHSCENE_OK);
  }
  GSHSCENE_Edit * ea = keep.keep(base.f.open(bound.get()));   // will remove n (and with it unbind k)
  GSHSCENE_Edit * eb = keep.keep(base.f.open(bound.get()));   // removes n too and binds k to a new node
  ASSERT_EQ(gshscene_edit_remove(ea, n), GSHSCENE_OK);
  ASSERT_EQ(gshscene_edit_remove(eb, n), GSHSCENE_OK);
  uint64_t m = base.f.shape(eb, base.b, &k);
  VersionRef vb;
  ASSERT_EQ(gshscene_scene_publish(base.f.s.scene, eb, nullptr, &vb.version), GSHSCENE_OK);
  ProblemsRef p;
  EXPECT_EQ(gshscene_scene_publish(base.f.s.scene, ea, &p.problems, nullptr), GSHSCENE_ERR_REFUSED);
  EXPECT_EQ(p.ids(GSHSCENE_PROBLEM_OVERLAP), (Ids{base.a, n})) << "both removed n, which touches the parent a";
  EXPECT_EQ(p.ids(GSHSCENE_PROBLEM_KEY_CONFLICT), (Ids{m})) << "k is now bound to m, which the removal of n would have left alone";
}

TEST(ScenePublishValidation, ManyProblemsAreAllNamedAscendingAndEachOnce) {
  Fx f;
  Edits keep;
  GSHSCENE_Edit * e = keep.keep(f.open());
  Ids want;
  for (int i = 0; i < 30; i++) {
    want.push_back(add(e, GSHSCENE_ROOT_ID, instance_node(5000 + i)));
  }
  ProblemsRef p;
  EXPECT_EQ(gshscene_scene_publish(f.s.scene, e, &p.problems, nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(p.ids(GSHSCENE_PROBLEM_DANGLING_TARGET), want);
  EXPECT_EQ(gshscene_problems_count(p.problems), 30u);
}

TEST(SceneMerge, AnIdThatSeveralChecksNameIsNamedOnce) {
  Base base;
  Edits keep;
  GSHSCENE_Edit * ea = keep.keep(base.f.open(base.v.get()));
  GSHSCENE_Edit * eb = keep.keep(base.f.open(base.v.get()));
  GSHSCENE_Edit * ec = keep.keep(base.f.open(base.v.get()));
  GSHSCENE_Node n1 = shape_node(base.f.sq.get(), 1), n2 = shape_node(base.f.sq.get(), 2), n3 = shape_node(base.f.sq.get(), 3);
  ASSERT_EQ(gshscene_edit_set(ea, base.a1, &n1), GSHSCENE_OK);
  ASSERT_EQ(gshscene_edit_set(eb, base.a1, &n2), GSHSCENE_OK);
  ASSERT_EQ(gshscene_edit_set(ec, base.a1, &n3), GSHSCENE_OK);
  VersionRef v;
  ASSERT_EQ(gshscene_scene_publish(base.f.s.scene, ec, nullptr, &v.version), GSHSCENE_OK);
  GSHSCENE_Edit * both[2] = {ea, eb};  // each overlaps the other and the publish before them
  ProblemsRef p;
  EXPECT_EQ(gshscene_scene_merge(base.f.s.scene, both, 2, &p.problems, nullptr), GSHSCENE_ERR_REFUSED);
  EXPECT_EQ(p.ids(), (Ids{base.a1}));
  EXPECT_EQ(gshscene_problems_count(p.problems), 1u);
}

TEST(ScenePublishValidation, ReleasingAndFailingWithProblemsLeavesNothingBehind) {
  tracker::Tracker t;
  GSHSCENE_Allocator a = tracker::allocator_for(&t);
  {
    SceneHolder s(nullptr, &a);
    Ref sq(intern_square(s.store()));
    for (long k = 0; k < 300; k++) {
      EditHolder e(s);
      add(e.edit, GSHSCENE_ROOT_ID, instance_node(900));
      add(e.edit, GSHSCENE_ROOT_ID, instance_node(901));
      t.fail_from = t.calls + k;
      t.persistent = false;
      t.failed = false;
      GSHSCENE_Problems * problems = nullptr;
      GSHSCENE_Result r = gshscene_scene_publish(s.scene, e.edit, &problems, nullptr);
      t.fail_from = -1;
      EXPECT_TRUE(r == GSHSCENE_ERR_INVALID || r == GSHSCENE_ERR_OOM) << k;
      if (r == GSHSCENE_ERR_INVALID) {
        EXPECT_EQ(gshscene_problems_count(problems), 2u);
        EXPECT_FALSE(t.failed);
      } else {
        EXPECT_EQ(problems, nullptr);
      }
      gshscene_problems_destroy(problems);
      if (!t.failed) {
        break;
      }
    }
  }
  EXPECT_TRUE(t.live.empty()) << t.live.size() << " blocks leaked";
}

TEST(ScenePublishValidation, ProblemReasonsHavePinnedValuesAndNames) {
  EXPECT_EQ((int)GSHSCENE_PROBLEM_OVERLAP, 1);
  EXPECT_EQ((int)GSHSCENE_PROBLEM_KEY_CONFLICT, 2);
  EXPECT_EQ((int)GSHSCENE_PROBLEM_DANGLING_TARGET, 3);
  EXPECT_EQ((int)GSHSCENE_PROBLEM_CYCLE, 4);
  EXPECT_EQ((int)GSHSCENE_PROBLEM_PARAMETER, 5);
  EXPECT_EQ((int)GSHSCENE_PROBLEM_REASON_COUNT, 6);
  EXPECT_STREQ(gshscene_problem_reason_string(GSHSCENE_PROBLEM_OVERLAP), "overlap");
  EXPECT_STREQ(gshscene_problem_reason_string(GSHSCENE_PROBLEM_KEY_CONFLICT), "key-conflict");
  EXPECT_STREQ(gshscene_problem_reason_string(GSHSCENE_PROBLEM_DANGLING_TARGET), "dangling-target");
  EXPECT_STREQ(gshscene_problem_reason_string(GSHSCENE_PROBLEM_CYCLE), "cycle");
  EXPECT_STREQ(gshscene_problem_reason_string(GSHSCENE_PROBLEM_PARAMETER), "parameter");
  EXPECT_STREQ(gshscene_problem_reason_string(GSHSCENE_PROBLEM_REASON_COUNT), "unknown problem");
  EXPECT_STREQ(gshscene_problem_reason_string((GSHSCENE_Problem_Reason)0), "unknown problem");
}

// --- the key map: re-lowering keeps IDs ------------------------------------------------------------------

namespace {

/** A front-end's document: key value -> layer; lowering it is an upsert of every element. */
struct Doc {
  std::vector<std::pair<uint64_t, int32_t>> elements;
};

const uint64_t kDomain = 0x5356470000000001ull;  // "SVG"-ish

void lower(Fx & f, GSHSCENE_Edit * e, const Doc & doc, const Doc * previous = nullptr) {
  for (const auto & el : doc.elements) {
    GSHSCENE_Key k = K(kDomain, el.first);
    GSHSCENE_Node n = shape_node(f.sq.get(), el.second);
    uint64_t id = 0;
    EXPECT_EQ(gshscene_edit_upsert(e, &k, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, &n, &id), GSHSCENE_OK);
  }
  if (previous) {  // elements that left the document leave the scene
    for (const auto & old : previous->elements) {
      bool kept = std::any_of(doc.elements.begin(), doc.elements.end(), [&](const auto & el) { return el.first == old.first; });
      if (!kept) {
        GSHSCENE_Key k = K(kDomain, old.first);
        EXPECT_EQ(gshscene_edit_remove_key(e, &k), GSHSCENE_OK);
      }
    }
  }
}

Ids ids_of(const GSHSCENE_Version * v, const Doc & doc) {
  Ids out;
  for (const auto & el : doc.elements) {
    GSHSCENE_Key k = K(kDomain, el.first);
    uint64_t id = 0;
    out.push_back(gshscene_version_lookup_key(v, &k, &id) == GSHSCENE_OK ? id : 0);
  }
  return out;
}

}  // namespace

TEST(SceneKeys, ReloweringTheSameKeysKeepsEveryId) {
  Fx f;
  Doc doc{{{10, 0}, {11, 1}, {12, 2}, {13, 3}}};
  VersionRef v1;
  {
    EditHolder e(f.s);
    lower(f, e.edit, doc);
    publish(f.s, e, &v1);
  }
  Ids first = ids_of(v1.get(), doc);
  EXPECT_EQ(first, (Ids{2, 3, 4, 5}));
  EXPECT_EQ(gshscene_version_key_count(v1.get()), 4u);
  // An identical re-lowering publishes nothing at all.
  VersionRef v2;
  {
    EditHolder e(f.s);
    lower(f, e.edit, doc, &doc);
    publish(f.s, e, &v2);
  }
  EXPECT_EQ(v2.get(), v1.get());
  EXPECT_EQ(gshscene_version_serial(v2.get()), 1u);
  // One element changed, one removed, one added: the unchanged keep their IDs, the changed one too.
  Doc next{{{10, 0}, {11, 9}, {13, 3}, {14, 4}}};
  VersionRef v3;
  {
    EditHolder e(f.s);
    lower(f, e.edit, next, &doc);
    publish(f.s, e, &v3);
  }
  Ids second = ids_of(v3.get(), next);
  EXPECT_EQ(second[0], first[0]);
  EXPECT_EQ(second[1], first[1]);
  EXPECT_EQ(second[2], first[3]);
  EXPECT_EQ(second[3], 6u) << "the new element's ID is the next in the scene's counter";
  EXPECT_EQ(gshscene_version_key_count(v3.get()), 4u);
  GSHSCENE_Key gone = K(kDomain, 12);
  uint64_t id = 0;
  EXPECT_EQ(gshscene_version_lookup_key(v3.get(), &gone, &id), GSHSCENE_ERR_INVALID);
  // The diff of v1 and v3 is exactly: 3 (layer 1 -> 9) changed, 4 removed (element 12), 6 added, and the root's list.
  GSHSCENE_Diff * diff = nullptr;
  ASSERT_EQ(gshscene_diff(v1.get(), v3.get(), nullptr, nullptr, &diff), GSHSCENE_OK);
  std::vector<std::pair<uint64_t, int>> got;
  for (size_t i = 0; i < gshscene_diff_count(diff); i++) {
    uint64_t did;
    GSHSCENE_Diff_Kind kind;
    ASSERT_EQ(gshscene_diff_get(diff, i, &did, &kind), GSHSCENE_OK);
    got.push_back({did, (int)kind});
  }
  gshscene_diff_destroy(diff);
  std::vector<std::pair<uint64_t, int>> want = {{1, GSHSCENE_DIFF_CHANGED}, {3, GSHSCENE_DIFF_CHANGED},
      {4, GSHSCENE_DIFF_REMOVED}, {6, GSHSCENE_DIFF_ADDED}};
  EXPECT_EQ(got, want);
}

TEST(SceneKeys, TheKeysOfAVersionAreItsOwnAndTheOldOnesStayBound) {
  Fx f;
  GSHSCENE_Key k = K(1, 1);
  VersionRef v1, v2;
  uint64_t id;
  {
    EditHolder e(f.s);
    id = f.shape(e.edit, GSHSCENE_ROOT_ID, &k);
    publish(f.s, e, &v1);
  }
  {
    EditHolder e(f.s);
    ASSERT_EQ(gshscene_edit_remove_key(e.edit, &k), GSHSCENE_OK);
    publish(f.s, e, &v2);
  }
  uint64_t found = 0;
  EXPECT_EQ(gshscene_version_lookup_key(v1.get(), &k, &found), GSHSCENE_OK);
  EXPECT_EQ(found, id);
  EXPECT_EQ(gshscene_version_lookup_key(v2.get(), &k, &found), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_version_key_count(v1.get()), 1u);
  EXPECT_EQ(gshscene_version_key_count(v2.get()), 0u);
  EXPECT_EQ(gshscene_version_lookup_key(nullptr, &k, &found), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_version_lookup_key(v1.get(), nullptr, &found), GSHSCENE_ERR_INVALID);
}

// --- allocation failure --------------------------------------------------------------------------------------

TEST(ScenePublish, EveryFailingAllocationInCreateAndPublishLeavesThingsAsTheyWere) {
  tracker::Tracker t;
  GSHSCENE_Allocator a = tracker::allocator_for(&t);
  // Scene creation, for every k.
  for (long k = 0; k < 40; k++) {
    t.fail_from = t.calls + k;
    t.persistent = false;
    t.failed = false;
    GSHSCENE_Scene * scene = nullptr;
    GSHSCENE_Result r = gshscene_scene_create(&a, nullptr, nullptr, &scene);
    t.fail_from = -1;
    if (r == GSHSCENE_OK) {
      gshscene_scene_destroy(scene);
      EXPECT_FALSE(t.failed) << k;
      break;
    }
    EXPECT_EQ(r, GSHSCENE_ERR_OOM) << k;
    EXPECT_EQ(scene, nullptr);
    EXPECT_TRUE(t.live.empty()) << "create leaked at k=" << k;
  }
  EXPECT_TRUE(t.live.empty());
  {
    SceneHolder s(nullptr, &a);
    Ref sq(intern_square(s.store()));
    // A publish of a changed edit, a merge with keys, for every k: the scene is as it was.
    for (int round = 0; round < 2; round++) {
      EditHolder e1(s);
      EditHolder e2(s);
      GSHSCENE_Key k1 = K(1, 100 + round), k2 = K(1, 200 + round);
      uint64_t ga = add(e1.edit, GSHSCENE_ROOT_ID, group_node(), &k1);
      uint64_t gb = add(e2.edit, GSHSCENE_ROOT_ID, group_node(), &k2);
      (void)ga;
      (void)gb;
      GSHSCENE_Edit * both[2] = {e1.edit, e2.edit};
      (void)both;
      VersionRef before;
      ASSERT_EQ(gshscene_scene_current(s.scene, &before.version), GSHSCENE_OK);
      std::string dump_before = dump_version(before.get());
      for (long k = 0; k < 400; k++) {
        t.fail_from = t.calls + k;
        t.persistent = false;
        t.failed = false;
        const GSHSCENE_Version * out = nullptr;
        // e1 and e2 both touch the root: one publish only; merge would refuse. Publish e1 alone.
        GSHSCENE_Result r = gshscene_scene_publish(s.scene, e1.edit, nullptr, &out);
        t.fail_from = -1;
        if (r == GSHSCENE_OK) {
          EXPECT_FALSE(t.failed) << "k=" << k;
          gshscene_version_release(out);
          break;
        }
        EXPECT_EQ(r, GSHSCENE_ERR_OOM) << "k=" << k;
        VersionRef cur;
        ASSERT_EQ(gshscene_scene_current(s.scene, &cur.version), GSHSCENE_OK);
        EXPECT_EQ(cur.get(), before.get()) << "k=" << k;
        EXPECT_EQ(dump_version(cur.get()), dump_before) << "k=" << k;
      }
    }
  }
  EXPECT_TRUE(t.live.empty()) << t.live.size() << " blocks leaked";
}

// --- threads --------------------------------------------------------------------------------------------------

TEST(SceneThreads, EightThreadsPublishIntoTheirOwnGroupsAndNothingIsLost) {
  Fx f;
  const int kThreads = 8;
  const int kRounds = 25;
  Ids groups;
  {
    EditHolder e(f.s);
    for (int i = 0; i < kThreads; i++) {
      groups.push_back(f.group(e.edit));
    }
    publish(f.s, e);
  }
  std::atomic<int> refused{0};
  std::atomic<int> failed{0};
  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; t++) {
    threads.emplace_back([&, t] {
      for (int r = 0; r < kRounds; r++) {
        GSHSCENE_Edit * e = nullptr;
        if (gshscene_scene_open_edit(f.s.scene, nullptr, &e) != GSHSCENE_OK) {
          failed++;
          return;
        }
        GSHSCENE_Node n = shape_node(f.sq.get(), r);
        uint64_t id = 0;
        if (gshscene_edit_add(e, groups[t], GSHSCENE_APPEND, &n, nullptr, &id) != GSHSCENE_OK) {
          failed++;
        }
        // Other threads publish meanwhile: this edit's base is stale, and it merges on top (disjoint groups).
        GSHSCENE_Result result = gshscene_scene_publish(f.s.scene, e, nullptr, nullptr);
        if (result == GSHSCENE_ERR_REFUSED) {
          refused++;
        } else if (result != GSHSCENE_OK) {
          failed++;
        }
        gshscene_edit_destroy(e);
        // A reader on the way: the current version is whole whatever is being published.
        const GSHSCENE_Version * cur = nullptr;
        if (gshscene_scene_current(f.s.scene, &cur) == GSHSCENE_OK) {
          (void)gshscene_version_hash(cur);
          gshscene_version_release(cur);
        }
      }
    });
  }
  for (auto & th : threads) {
    th.join();
  }
  EXPECT_EQ(failed.load(), 0);
  EXPECT_EQ(refused.load(), 0) << "the groups are disjoint";
  VersionRef cur(f.current());
  EXPECT_EQ(gshscene_version_node_count(cur.get()), 1u + kThreads + (size_t)kThreads * kRounds);
  EXPECT_EQ(gshscene_version_serial(cur.get()), 1u + (uint64_t)kThreads * kRounds);
  for (int t = 0; t < kThreads; t++) {
    EXPECT_EQ(info_of(cur.get(), groups[t]).child_count, (size_t)kRounds) << t;
  }
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
