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
 * The edit: add, set, remove, move, upsert and the key map, through the edit's
 * own view, atomic under every failing allocation (edit.h; story 11).
 */

#include "scene_fixtures.h"

#include "../alloc_tracker.h"

#include <set>
#include <sstream>

using namespace scene;

namespace {

struct Fx {
  SceneHolder s;
  EditHolder e;
  Ref sq;
  Fx(const GSHSCENE_Limits * limits = nullptr) : s(limits), e(s), sq(intern_square(s.store())) {}
  uint64_t group(uint64_t parent = GSHSCENE_ROOT_ID, const GSHSCENE_Key * key = nullptr, size_t pos = GSHSCENE_APPEND) {
    return add(e.edit, parent, group_node(), key, pos);
  }
  uint64_t shape(uint64_t parent = GSHSCENE_ROOT_ID, const GSHSCENE_Key * key = nullptr, size_t pos = GSHSCENE_APPEND) {
    return add(e.edit, parent, shape_node(sq.get()), key, pos);
  }
  std::vector<uint64_t> kids(uint64_t id) {
    GSHSCENE_Node_Info info;
    EXPECT_EQ(gshscene_edit_node(e.edit, id, &info), GSHSCENE_OK);
    std::vector<uint64_t> out;
    for (size_t i = 0; i < info.child_count; i++) {
      uint64_t c = 0;
      EXPECT_EQ(gshscene_edit_child(e.edit, id, i, &c), GSHSCENE_OK);
      out.push_back(c);
    }
    return out;
  }
  uint64_t parent_of(uint64_t id) {
    GSHSCENE_Node_Info info;
    EXPECT_EQ(gshscene_edit_node(e.edit, id, &info), GSHSCENE_OK);
    return info.parent;
  }
  bool exists(uint64_t id) {
    GSHSCENE_Node_Info info;
    return gshscene_edit_node(e.edit, id, &info) == GSHSCENE_OK;
  }
};

using Ids = std::vector<uint64_t>;

/** Every node of the view up to @p max_id, as text: what a failing operation must not have changed. */
std::string view_state(const GSHSCENE_Edit * edit, uint64_t max_id) {
  std::ostringstream out;
  out << "count=" << gshscene_edit_node_count(edit);
  for (uint64_t id = 1; id <= max_id; id++) {
    GSHSCENE_Node_Info info;
    if (gshscene_edit_node(edit, id, &info) != GSHSCENE_OK) {
      out << " [" << id << " -]";
      continue;
    }
    out << " [" << id << " p" << info.parent << " k" << (int)info.node.kind << " op" << info.node.opacity
        << " l" << info.node.layer << " t" << info.node.target << (info.has_key ? " K" : "") << " c";
    for (size_t i = 0; i < info.child_count; i++) {
      uint64_t c = 0;
      gshscene_edit_child(edit, id, i, &c);
      out << c << ",";
    }
    out << "]";
  }
  return out.str();
}

}  // namespace

// --- add ------------------------------------------------------------------------------------

TEST(EditAdd, IdsAscendFromTwoAndChildrenKeepTheirOrder) {
  Fx f;
  EXPECT_EQ(gshscene_edit_node_count(f.e.edit), 1u);
  uint64_t a = f.group();
  uint64_t b = f.shape();
  uint64_t c = f.shape(a);
  EXPECT_EQ(a, 2u);
  EXPECT_EQ(b, 3u);
  EXPECT_EQ(c, 4u);
  EXPECT_EQ(f.kids(GSHSCENE_ROOT_ID), (Ids{2, 3}));
  EXPECT_EQ(f.kids(a), (Ids{4}));
  EXPECT_EQ(f.parent_of(c), a);
  EXPECT_EQ(f.parent_of(a), GSHSCENE_ROOT_ID);
  EXPECT_EQ(f.parent_of(GSHSCENE_ROOT_ID), 0u);
  EXPECT_EQ(gshscene_edit_node_count(f.e.edit), 4u);
}

TEST(EditAdd, APositionInsertsBeforeThatChild) {
  Fx f;
  uint64_t a = f.shape();
  uint64_t b = f.shape();
  uint64_t first = f.shape(GSHSCENE_ROOT_ID, nullptr, 0);
  uint64_t middle = f.shape(GSHSCENE_ROOT_ID, nullptr, 2);
  uint64_t last = f.shape(GSHSCENE_ROOT_ID, nullptr, 4);
  EXPECT_EQ(f.kids(GSHSCENE_ROOT_ID), (Ids{first, a, middle, b, last}));
  uint64_t id = 0;
  GSHSCENE_Node n = shape_node(f.sq.get());
  EXPECT_EQ(gshscene_edit_add(f.e.edit, GSHSCENE_ROOT_ID, 6, &n, nullptr, &id), GSHSCENE_ERR_INVALID) << "past the end";
  EXPECT_EQ(gshscene_edit_add(f.e.edit, GSHSCENE_ROOT_ID, 5, &n, nullptr, &id), GSHSCENE_OK);
  EXPECT_EQ(f.kids(GSHSCENE_ROOT_ID).back(), id);
}

TEST(EditAdd, TheParentMustBeAGroupOfTheView) {
  Fx f;
  uint64_t s = f.shape();
  GSHSCENE_Node n = shape_node(f.sq.get());
  uint64_t id = 0;
  EXPECT_EQ(gshscene_edit_add(f.e.edit, s, GSHSCENE_APPEND, &n, nullptr, &id), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_edit_add(f.e.edit, 999, GSHSCENE_APPEND, &n, nullptr, &id), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_edit_add(f.e.edit, 0, GSHSCENE_APPEND, &n, nullptr, &id), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_edit_add(nullptr, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, &n, nullptr, &id), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_edit_add(f.e.edit, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, nullptr, nullptr, &id), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_edit_add(f.e.edit, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, &n, nullptr, nullptr), GSHSCENE_OK) << "out_id may be NULL";
}

TEST(EditAdd, AFailingCallConsumesNoId) {
  Fx f;
  EXPECT_EQ(f.shape(), 2u);
  GSHSCENE_Node bad = group_node(2.0);
  uint64_t id = 0;
  EXPECT_EQ(gshscene_edit_add(f.e.edit, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, &bad, nullptr, &id), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_edit_add(f.e.edit, 77, GSHSCENE_APPEND, &bad, nullptr, &id), GSHSCENE_ERR_INVALID);
  GSHSCENE_Node good = shape_node(f.sq.get());
  EXPECT_EQ(gshscene_edit_add(f.e.edit, 77, GSHSCENE_APPEND, &good, nullptr, &id), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(f.shape(), 3u) << "no gap after three refused calls";
}

TEST(EditAdd, ReadingTheViewNeverShowsAnotherEditsWork) {
  SceneHolder s;
  Ref sq(intern_square(s.store()));
  EditHolder a(s);
  EditHolder b(s);
  uint64_t id = add(a.edit, GSHSCENE_ROOT_ID, shape_node(sq.get()));
  GSHSCENE_Node_Info info;
  EXPECT_EQ(gshscene_edit_node(a.edit, id, &info), GSHSCENE_OK);
  EXPECT_EQ(gshscene_edit_node(b.edit, id, &info), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_edit_node_count(b.edit), 1u);
  const GSHSCENE_Version * base = gshscene_edit_base(a.edit);
  ASSERT_NE(base, nullptr);
  EXPECT_EQ(gshscene_version_node_count(base), 1u) << "the base is never touched";
  EXPECT_EQ(gshscene_edit_store(a.edit), s.store());
  EXPECT_EQ(gshscene_edit_base(nullptr), nullptr);
  EXPECT_EQ(gshscene_edit_store(nullptr), nullptr);
  EXPECT_EQ(gshscene_edit_node_count(nullptr), 0u);
}

// --- keys -------------------------------------------------------------------------------------

TEST(EditKeys, AKeyIsBoundToItsNodeAndLookedUp) {
  Fx f;
  GSHSCENE_Key k1 = K(1, 100);
  GSHSCENE_Key k2 = K(2, 100);  // the same value in another domain is another key
  uint64_t a = f.shape(GSHSCENE_ROOT_ID, &k1);
  uint64_t b = f.shape(GSHSCENE_ROOT_ID, &k2);
  uint64_t id = 0;
  ASSERT_EQ(gshscene_edit_lookup_key(f.e.edit, &k1, &id), GSHSCENE_OK);
  EXPECT_EQ(id, a);
  ASSERT_EQ(gshscene_edit_lookup_key(f.e.edit, &k2, &id), GSHSCENE_OK);
  EXPECT_EQ(id, b);
  GSHSCENE_Key none = K(1, 101);
  EXPECT_EQ(gshscene_edit_lookup_key(f.e.edit, &none, &id), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_edit_lookup_key(f.e.edit, nullptr, &id), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_edit_lookup_key(nullptr, &k1, &id), GSHSCENE_ERR_INVALID);
  GSHSCENE_Node_Info info;
  ASSERT_EQ(gshscene_edit_node(f.e.edit, a, &info), GSHSCENE_OK);
  EXPECT_TRUE(info.has_key);
  EXPECT_EQ(info.key.domain, 1u);
  EXPECT_EQ(info.key.value, 100u);
  uint64_t c = f.shape();
  ASSERT_EQ(gshscene_edit_node(f.e.edit, c, &info), GSHSCENE_OK);
  EXPECT_FALSE(info.has_key);
}

TEST(EditKeys, ABoundKeyCannotBeBoundAgainAndRemovalFreesIt) {
  Fx f;
  GSHSCENE_Key k = K(5, 5);
  uint64_t a = f.shape(GSHSCENE_ROOT_ID, &k);
  GSHSCENE_Node n = shape_node(f.sq.get());
  uint64_t id = 0;
  EXPECT_EQ(gshscene_edit_add(f.e.edit, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, &n, &k, &id), GSHSCENE_ERR_INVALID);
  ASSERT_EQ(gshscene_edit_remove(f.e.edit, a), GSHSCENE_OK);
  EXPECT_EQ(gshscene_edit_lookup_key(f.e.edit, &k, &id), GSHSCENE_ERR_INVALID) << "removing a node unbinds its key";
  uint64_t b = f.shape(GSHSCENE_ROOT_ID, &k);
  EXPECT_NE(b, a) << "an ID is never reused";
  ASSERT_EQ(gshscene_edit_lookup_key(f.e.edit, &k, &id), GSHSCENE_OK);
  EXPECT_EQ(id, b);
}

TEST(EditKeys, RemovingASubtreeUnbindsEveryKeyInIt) {
  Fx f;
  GSHSCENE_Key kg = K(1, 1), ks = K(1, 2), ko = K(1, 3);
  uint64_t g = f.group(GSHSCENE_ROOT_ID, &kg);
  f.shape(g, &ks);
  uint64_t other = f.shape(GSHSCENE_ROOT_ID, &ko);
  ASSERT_EQ(gshscene_edit_remove_key(f.e.edit, &kg), GSHSCENE_OK);
  uint64_t id = 0;
  EXPECT_EQ(gshscene_edit_lookup_key(f.e.edit, &kg, &id), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_edit_lookup_key(f.e.edit, &ks, &id), GSHSCENE_ERR_INVALID);
  ASSERT_EQ(gshscene_edit_lookup_key(f.e.edit, &ko, &id), GSHSCENE_OK);
  EXPECT_EQ(id, other);
  EXPECT_EQ(gshscene_edit_remove_key(f.e.edit, &kg), GSHSCENE_ERR_INVALID) << "no longer bound";
  EXPECT_EQ(gshscene_edit_remove_key(f.e.edit, nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_edit_remove_key(nullptr, &kg), GSHSCENE_ERR_INVALID);
}

TEST(EditKeys, ABaseVersionsKeysAreVisibleThroughTheEdit) {
  SceneHolder s;
  Ref sq(intern_square(s.store()));
  GSHSCENE_Key k = K(9, 9);
  uint64_t id;
  {
    EditHolder e(s);
    id = add(e.edit, GSHSCENE_ROOT_ID, shape_node(sq.get()), &k);
    publish(s, e);
  }
  EditHolder e2(s);
  uint64_t found = 0;
  ASSERT_EQ(gshscene_edit_lookup_key(e2.edit, &k, &found), GSHSCENE_OK);
  EXPECT_EQ(found, id);
  ASSERT_EQ(gshscene_edit_remove(e2.edit, id), GSHSCENE_OK);
  EXPECT_EQ(gshscene_edit_lookup_key(e2.edit, &k, &found), GSHSCENE_ERR_INVALID) << "removed in the edit, still bound in its base";
  uint64_t again = add(e2.edit, GSHSCENE_ROOT_ID, shape_node(sq.get()), &k);
  EXPECT_NE(again, id);
  ASSERT_EQ(gshscene_edit_lookup_key(e2.edit, &k, &found), GSHSCENE_OK);
  EXPECT_EQ(found, again);
}

TEST(EditKeys, ManyKeysInOneEditGrowTheTable) {
  Fx f;
  std::vector<uint64_t> ids;
  for (uint64_t i = 0; i < 40; i++) {
    GSHSCENE_Key k = K(7, i * 977);
    ids.push_back(f.shape(GSHSCENE_ROOT_ID, &k));
  }
  for (uint64_t i = 0; i < 40; i++) {
    GSHSCENE_Key k = K(7, i * 977);
    uint64_t id = 0;
    ASSERT_EQ(gshscene_edit_lookup_key(f.e.edit, &k, &id), GSHSCENE_OK) << i;
    EXPECT_EQ(id, ids[i]);
  }
  VersionRef v;
  publish(f.s, f.e, &v);
  EXPECT_EQ(gshscene_version_key_count(v.get()), 40u);
}

TEST(EditRead, ArgumentsAreChecked) {
  Fx f;
  uint64_t g = f.group();
  f.shape(g);
  GSHSCENE_Node_Info info;
  uint64_t id = 0;
  EXPECT_EQ(gshscene_edit_node(nullptr, g, &info), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_edit_node(f.e.edit, g, nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_edit_node(f.e.edit, 12345, &info), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_edit_child(nullptr, g, 0, &id), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_edit_child(f.e.edit, g, 0, nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_edit_child(f.e.edit, g, 1, &id), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_edit_child(f.e.edit, 3, 0, &id), GSHSCENE_ERR_INVALID) << "a shape has no children";
  EXPECT_EQ(gshscene_edit_child(f.e.edit, 999, 0, &id), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_edit_child(f.e.edit, g, 0, &id), GSHSCENE_OK);
  GSHSCENE_Key k = K(1, 1);
  EXPECT_EQ(gshscene_edit_lookup_key(f.e.edit, &k, nullptr), GSHSCENE_ERR_INVALID);
  // A read gives the node whole: a node without a key reads with the key zero.
  ASSERT_EQ(gshscene_edit_node(f.e.edit, g, &info), GSHSCENE_OK);
  EXPECT_EQ(info.id, g);
  EXPECT_FALSE(info.has_key);
  EXPECT_EQ(info.key.domain, 0u);
  EXPECT_EQ(info.key.value, 0u);
  EXPECT_EQ(info.child_count, 1u);
}

TEST(EditSpent, APublishedEditHoldsNothingAndItsViewIsTheBaseAgain) {
  SceneHolder s;
  Ref sq(intern_square(s.store()));
  EditHolder e(s);
  GSHSCENE_Key k = K(4, 4);
  uint64_t id = add(e.edit, GSHSCENE_ROOT_ID, shape_node(sq.get()), &k);
  EXPECT_EQ(gshscene_resource_refcount(sq.get()), 2u);
  VersionRef v;
  publish(s, e, &v);
  EXPECT_EQ(gshscene_resource_refcount(sq.get()), 2u) << "the spent edit let go of its node: only the version holds it";
  uint64_t found = 0;
  EXPECT_EQ(gshscene_edit_lookup_key(e.edit, &k, &found), GSHSCENE_ERR_INVALID) << "the edit's bindings were spent with it";
  GSHSCENE_Node_Info info;
  EXPECT_EQ(gshscene_edit_node(e.edit, id, &info), GSHSCENE_ERR_INVALID) << "and so was its node: the view is the base's";
}

TEST(EditRemove, ASubtreeLargerThanTheQueueIsRemovedWhole) {
  Fx f;
  uint64_t top = f.group();
  Ids all = {top};
  for (int i = 0; i < 6; i++) {
    uint64_t mid = f.group(top);
    all.push_back(mid);
    for (int k = 0; k < 8; k++) {
      all.push_back(f.shape(mid));
    }
  }
  EXPECT_EQ(gshscene_edit_node_count(f.e.edit), 1u + all.size());
  ASSERT_EQ(gshscene_edit_remove(f.e.edit, top), GSHSCENE_OK);
  for (uint64_t id : all) {
    EXPECT_FALSE(f.exists(id)) << id;
  }
  EXPECT_EQ(gshscene_edit_node_count(f.e.edit), 1u);
}

TEST(EditMove, AGroupAtItsCapacityTakesAnotherChild) {
  Fx f;
  uint64_t dest = f.group();
  for (int i = 0; i < 4; i++) {
    f.shape(dest);
  }
  Ids movers;
  for (int i = 0; i < 6; i++) {
    movers.push_back(f.shape());
  }
  for (uint64_t m : movers) {
    ASSERT_EQ(gshscene_edit_move(f.e.edit, m, dest, GSHSCENE_APPEND), GSHSCENE_OK) << m;
  }
  EXPECT_EQ(f.kids(dest).size(), 10u);
  EXPECT_EQ(f.kids(GSHSCENE_ROOT_ID), (Ids{dest}));
}

// --- set --------------------------------------------------------------------------------------

TEST(EditSet, ReplacesThePropertiesAndKeepsTheChildren) {
  Fx f;
  uint64_t g = f.group();
  uint64_t c = f.shape(g);
  GSHSCENE_Node n = group_node(0.5, 7);
  n.map = translate(3, 4);
  ASSERT_EQ(gshscene_edit_set(f.e.edit, g, &n), GSHSCENE_OK);
  GSHSCENE_Node_Info info;
  ASSERT_EQ(gshscene_edit_node(f.e.edit, g, &info), GSHSCENE_OK);
  EXPECT_EQ(info.node.opacity, 0.5);
  EXPECT_EQ(info.node.layer, 7);
  EXPECT_EQ(gshgeom_map_hash(&info.node.map), gshgeom_map_hash(&n.map));
  EXPECT_EQ(f.kids(g), (Ids{c}));
  // A shape's resource changes.
  Ref other(intern_square(f.s.store(), 10));
  GSHSCENE_Node sn = shape_node(other.get(), 2);
  ASSERT_EQ(gshscene_edit_set(f.e.edit, c, &sn), GSHSCENE_OK);
  ASSERT_EQ(gshscene_edit_node(f.e.edit, c, &info), GSHSCENE_OK);
  EXPECT_EQ(info.node.resource, other.get());
  EXPECT_EQ(info.node.layer, 2);
}

TEST(EditSet, TheKindCannotChangeAndTheRootCanBeSet) {
  Fx f;
  uint64_t g = f.group();
  GSHSCENE_Node s = shape_node(f.sq.get());
  EXPECT_EQ(gshscene_edit_set(f.e.edit, g, &s), GSHSCENE_ERR_INVALID);
  GSHSCENE_Node i = instance_node(g);
  EXPECT_EQ(gshscene_edit_set(f.e.edit, g, &i), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_edit_set(f.e.edit, 999, &s), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_edit_set(f.e.edit, g, nullptr), GSHSCENE_ERR_INVALID);
  GSHSCENE_Node bad = group_node(-1);
  EXPECT_EQ(gshscene_edit_set(f.e.edit, g, &bad), GSHSCENE_ERR_INVALID);
  GSHSCENE_Node root = group_node(0.25);
  EXPECT_EQ(gshscene_edit_set(f.e.edit, GSHSCENE_ROOT_ID, &root), GSHSCENE_OK);
  GSHSCENE_Node_Info info;
  ASSERT_EQ(gshscene_edit_node(f.e.edit, GSHSCENE_ROOT_ID, &info), GSHSCENE_OK);
  EXPECT_EQ(info.node.opacity, 0.25);
  EXPECT_EQ(f.kids(GSHSCENE_ROOT_ID), (Ids{g}));
}

TEST(EditSet, GivingANodeItsOwnPropertiesTouchesNothing) {
  SceneHolder s;
  Ref sq(intern_square(s.store()));
  uint64_t id;
  VersionRef v1;
  {
    EditHolder e(s);
    id = add(e.edit, GSHSCENE_ROOT_ID, shape_node(sq.get(), 3));
    publish(s, e, &v1);
  }
  EditHolder e(s);
  GSHSCENE_Node same = shape_node(sq.get(), 3);
  ASSERT_EQ(gshscene_edit_set(e.edit, id, &same), GSHSCENE_OK);
  VersionRef v2;
  publish(s, e, &v2);
  EXPECT_EQ(v2.get(), v1.get()) << "nothing was touched, so nothing was published";
  EXPECT_EQ(gshscene_version_serial(v2.get()), 1u);
}

// --- remove ------------------------------------------------------------------------------------

TEST(EditRemove, TakesTheSubtreeAndLeavesTheSiblings) {
  Fx f;
  uint64_t a = f.group();
  uint64_t a1 = f.group(a);
  uint64_t a2 = f.shape(a1);
  uint64_t b = f.shape();
  EXPECT_EQ(gshscene_edit_node_count(f.e.edit), 5u);
  ASSERT_EQ(gshscene_edit_remove(f.e.edit, a), GSHSCENE_OK);
  EXPECT_FALSE(f.exists(a));
  EXPECT_FALSE(f.exists(a1));
  EXPECT_FALSE(f.exists(a2));
  EXPECT_TRUE(f.exists(b));
  EXPECT_EQ(f.kids(GSHSCENE_ROOT_ID), (Ids{b}));
  EXPECT_EQ(gshscene_edit_node_count(f.e.edit), 2u);
  EXPECT_EQ(f.shape(), 6u) << "removed IDs are not reissued";
}

TEST(EditRemove, TheRootAndWhatIsNotThereAreRefused) {
  Fx f;
  EXPECT_EQ(gshscene_edit_remove(f.e.edit, GSHSCENE_ROOT_ID), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_edit_remove(f.e.edit, 55), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_edit_remove(f.e.edit, 0), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_edit_remove(nullptr, 5), GSHSCENE_ERR_INVALID);
  uint64_t a = f.shape();
  ASSERT_EQ(gshscene_edit_remove(f.e.edit, a), GSHSCENE_OK);
  EXPECT_EQ(gshscene_edit_remove(f.e.edit, a), GSHSCENE_ERR_INVALID) << "twice";
}

TEST(EditRemove, ABaseNodeRemovedInTheEditIsStillInTheBase) {
  SceneHolder s;
  Ref sq(intern_square(s.store()));
  uint64_t id;
  {
    EditHolder e(s);
    id = add(e.edit, GSHSCENE_ROOT_ID, shape_node(sq.get()));
    publish(s, e);
  }
  EditHolder e(s);
  ASSERT_EQ(gshscene_edit_remove(e.edit, id), GSHSCENE_OK);
  GSHSCENE_Node_Info info;
  EXPECT_EQ(gshscene_edit_node(e.edit, id, &info), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_version_node(gshscene_edit_base(e.edit), id, &info), GSHSCENE_OK);
  EXPECT_EQ(gshscene_edit_node_count(e.edit), 1u);
  EXPECT_EQ(gshscene_version_node_count(gshscene_edit_base(e.edit)), 2u);
}

// --- move ---------------------------------------------------------------------------------------

TEST(EditMove, ChangesTheParentAndThePosition) {
  Fx f;
  uint64_t a = f.group();
  uint64_t b = f.group();
  uint64_t x = f.shape(a);
  uint64_t y = f.shape(a);
  uint64_t z = f.shape(b);
  ASSERT_EQ(gshscene_edit_move(f.e.edit, x, b, 0), GSHSCENE_OK);
  EXPECT_EQ(f.kids(a), (Ids{y}));
  EXPECT_EQ(f.kids(b), (Ids{x, z}));
  EXPECT_EQ(f.parent_of(x), b);
  ASSERT_EQ(gshscene_edit_move(f.e.edit, y, b, GSHSCENE_APPEND), GSHSCENE_OK);
  EXPECT_EQ(f.kids(b), (Ids{x, z, y}));
  EXPECT_EQ(f.kids(a), (Ids{}));
  EXPECT_EQ(gshscene_edit_node_count(f.e.edit), 6u) << "a move adds and removes nothing";
  // A group moves with its subtree.
  ASSERT_EQ(gshscene_edit_move(f.e.edit, b, a, 0), GSHSCENE_OK);
  EXPECT_EQ(f.kids(a), (Ids{b}));
  EXPECT_EQ(f.kids(b), (Ids{x, z, y}));
}

TEST(EditMove, WithinOneGroupThePositionIsAmongTheOthers) {
  Fx f;
  uint64_t a = f.shape(), b = f.shape(), c = f.shape(), d = f.shape();
  ASSERT_EQ(gshscene_edit_move(f.e.edit, a, GSHSCENE_ROOT_ID, 2), GSHSCENE_OK);
  EXPECT_EQ(f.kids(GSHSCENE_ROOT_ID), (Ids{b, c, a, d}));
  ASSERT_EQ(gshscene_edit_move(f.e.edit, d, GSHSCENE_ROOT_ID, 0), GSHSCENE_OK);
  EXPECT_EQ(f.kids(GSHSCENE_ROOT_ID), (Ids{d, b, c, a}));
  ASSERT_EQ(gshscene_edit_move(f.e.edit, d, GSHSCENE_ROOT_ID, GSHSCENE_APPEND), GSHSCENE_OK);
  EXPECT_EQ(f.kids(GSHSCENE_ROOT_ID), (Ids{b, c, a, d}));
  EXPECT_EQ(gshscene_edit_move(f.e.edit, d, GSHSCENE_ROOT_ID, 4), GSHSCENE_ERR_INVALID) << "among the other three, 3 is the last place";
  ASSERT_EQ(gshscene_edit_move(f.e.edit, d, GSHSCENE_ROOT_ID, 3), GSHSCENE_OK) << "where it is already: nothing";
  EXPECT_EQ(f.kids(GSHSCENE_ROOT_ID), (Ids{b, c, a, d}));
}

TEST(EditMove, RefusesTheRootACycleANonGroupAndMissingNodes) {
  Fx f;
  uint64_t a = f.group();
  uint64_t b = f.group(a);
  uint64_t c = f.group(b);
  uint64_t s = f.shape();
  EXPECT_EQ(gshscene_edit_move(f.e.edit, GSHSCENE_ROOT_ID, a, 0), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_edit_move(f.e.edit, a, a, 0), GSHSCENE_ERR_INVALID) << "into itself";
  EXPECT_EQ(gshscene_edit_move(f.e.edit, a, c, 0), GSHSCENE_ERR_INVALID) << "into its grandchild";
  EXPECT_EQ(gshscene_edit_move(f.e.edit, a, b, 0), GSHSCENE_ERR_INVALID) << "into its child";
  EXPECT_EQ(gshscene_edit_move(f.e.edit, c, s, 0), GSHSCENE_ERR_INVALID) << "into a shape";
  EXPECT_EQ(gshscene_edit_move(f.e.edit, 99, a, 0), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_edit_move(f.e.edit, c, 99, 0), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_edit_move(f.e.edit, c, a, 5), GSHSCENE_ERR_INVALID) << "a position past the end";
  EXPECT_EQ(gshscene_edit_move(nullptr, c, a, 0), GSHSCENE_ERR_INVALID);
  ASSERT_EQ(gshscene_edit_move(f.e.edit, c, a, 1), GSHSCENE_OK) << "the end of a's one child is position 1";
  EXPECT_EQ(f.kids(a), (Ids{b, c}));
}

// --- upsert --------------------------------------------------------------------------------------

TEST(EditUpsert, AddsOnceThenFindsTheSameNode) {
  Fx f;
  GSHSCENE_Key k = K(3, 33);
  GSHSCENE_Node n = shape_node(f.sq.get(), 1);
  uint64_t first = 0;
  ASSERT_EQ(gshscene_edit_upsert(f.e.edit, &k, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, &n, &first), GSHSCENE_OK);
  uint64_t other = f.shape();
  n.layer = 9;
  uint64_t second = 0;
  ASSERT_EQ(gshscene_edit_upsert(f.e.edit, &k, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, &n, &second), GSHSCENE_OK);
  EXPECT_EQ(second, first);
  GSHSCENE_Node_Info info;
  ASSERT_EQ(gshscene_edit_node(f.e.edit, first, &info), GSHSCENE_OK);
  EXPECT_EQ(info.node.layer, 9);
  EXPECT_EQ(f.kids(GSHSCENE_ROOT_ID), (Ids{first, other})) << "an update does not move the node";
  // Where it is added is only used when it is not there yet.
  uint64_t third = 0;
  ASSERT_EQ(gshscene_edit_upsert(f.e.edit, &k, 12345, 77, &n, &third), GSHSCENE_OK);
  EXPECT_EQ(third, first);
  EXPECT_EQ(gshscene_edit_upsert(f.e.edit, &k, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, &n, nullptr), GSHSCENE_OK) << "out_id may be NULL";
}

TEST(EditUpsert, AKindThatDiffersIsRefusedAndTheArgumentsAreChecked) {
  Fx f;
  GSHSCENE_Key k = K(3, 34);
  GSHSCENE_Node g = group_node();
  ASSERT_EQ(gshscene_edit_upsert(f.e.edit, &k, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, &g, nullptr), GSHSCENE_OK);
  GSHSCENE_Node s = shape_node(f.sq.get());
  EXPECT_EQ(gshscene_edit_upsert(f.e.edit, &k, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, &s, nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_edit_upsert(f.e.edit, nullptr, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, &s, nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_edit_upsert(f.e.edit, &k, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, nullptr, nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_edit_upsert(nullptr, &k, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, &s, nullptr), GSHSCENE_ERR_INVALID);
  GSHSCENE_Key fresh = K(3, 35);
  uint64_t id = 0;
  EXPECT_EQ(gshscene_edit_upsert(f.e.edit, &fresh, 999, GSHSCENE_APPEND, &s, &id), GSHSCENE_ERR_INVALID) << "a new node needs a real parent";
}

// --- limits and a spent edit ---------------------------------------------------------------------

TEST(EditLimits, MaxNodesAcceptsExactlyTheLimitAsTheEditGrows) {
  GSHSCENE_Limits limits;
  gshscene_limits_default(&limits);
  limits.max_nodes = 5;
  Fx f(&limits);
  for (int i = 0; i < 4; i++) {
    EXPECT_NE(f.shape(), 0u) << i;
  }
  EXPECT_EQ(gshscene_edit_node_count(f.e.edit), 5u);
  GSHSCENE_Node n = shape_node(f.sq.get());
  uint64_t id = 0;
  EXPECT_EQ(gshscene_edit_add(f.e.edit, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, &n, nullptr, &id), GSHSCENE_ERR_LIMIT);
  EXPECT_EQ(gshscene_edit_node_count(f.e.edit), 5u);
  ASSERT_EQ(gshscene_edit_remove(f.e.edit, 3), GSHSCENE_OK);
  EXPECT_EQ(gshscene_edit_add(f.e.edit, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, &n, nullptr, &id), GSHSCENE_OK) << "room again";
}

TEST(EditSpent, EveryCallOnAPublishedEditIsRefusedButTheReadsStillWork) {
  SceneHolder s;
  Ref sq(intern_square(s.store()));
  EditHolder e(s);
  uint64_t id = add(e.edit, GSHSCENE_ROOT_ID, shape_node(sq.get()));
  publish(s, e);
  GSHSCENE_Node n = shape_node(sq.get());
  GSHSCENE_Key k = K(1, 1);
  uint64_t out = 0;
  EXPECT_EQ(gshscene_edit_add(e.edit, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, &n, nullptr, &out), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_edit_set(e.edit, id, &n), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_edit_remove(e.edit, id), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_edit_move(e.edit, id, GSHSCENE_ROOT_ID, 0), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_edit_upsert(e.edit, &k, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, &n, &out), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_edit_remove_key(e.edit, &k), GSHSCENE_ERR_INVALID);
  GSHSCENE_Problems * problems = nullptr;
  EXPECT_EQ(gshscene_scene_publish(s.scene, e.edit, &problems, nullptr), GSHSCENE_ERR_INVALID) << "twice";
  EXPECT_EQ(problems, nullptr);
  GSHSCENE_Node_Info info;
  EXPECT_EQ(gshscene_edit_node(e.edit, 1, &info), GSHSCENE_OK);
}

// --- allocation failure --------------------------------------------------------------------------

namespace {

/** Run @p op with the k-th allocation (counted from now) failing, for k = 0, 1, ... until it succeeds; after each failure the
 *  view must be as it was; then the operation is run for real (it succeeded on the last trial). */
template <typename Op>
void sweep_op(tracker::Tracker & t, GSHSCENE_Edit * edit, uint64_t max_id, const char * what, Op op) {
  for (long k = 0; k < 64; k++) {
    std::string before = view_state(edit, max_id);
    t.fail_from = t.calls + k;
    t.persistent = false;
    t.failed = false;
    GSHSCENE_Result r = op();
    t.fail_from = -1;
    if (r == GSHSCENE_OK) {
      EXPECT_FALSE(t.failed) << what << " k=" << k << ": an allocation failed and the call reported success";
      return;
    }
    EXPECT_EQ(r, GSHSCENE_ERR_OOM) << what << " k=" << k;
    EXPECT_EQ(view_state(edit, max_id), before) << what << " k=" << k << ": the failing call changed the view";
  }
  ADD_FAILURE() << what << ": still failing after 64 allocations";
}

}  // namespace

TEST(EditAllocation, EveryFailingAllocationLeavesTheEditAsItWas) {
  tracker::Tracker t;
  GSHSCENE_Allocator a = tracker::allocator_for(&t);
  {
    SceneHolder s(nullptr, &a);
    ASSERT_NE(s.scene, nullptr);
    Ref sq(intern_square(s.store()));
    EditHolder e(s);
    ASSERT_NE(e.edit, nullptr);
    const uint64_t kMax = 40;
    GSHSCENE_Key k1 = K(1, 1), k2 = K(1, 2);
    uint64_t g1 = 0, g2 = 0, sh = 0, sh2 = 0, up = 0;
    GSHSCENE_Node gn = group_node();
    GSHSCENE_Node sn = shape_node(sq.get());
    sweep_op(t, e.edit, kMax, "add group", [&] {
      return gshscene_edit_add(e.edit, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, &gn, &k1, &g1);
    });
    sweep_op(t, e.edit, kMax, "add group 2", [&] {
      return gshscene_edit_add(e.edit, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, &gn, nullptr, &g2);
    });
    sweep_op(t, e.edit, kMax, "add shape", [&] { return gshscene_edit_add(e.edit, g1, 0, &sn, &k2, &sh); });
    for (int i = 0; i < 20; i++) {  // grow the arrays past their first capacities
      sweep_op(t, e.edit, kMax, "add many", [&] { return gshscene_edit_add(e.edit, g2, GSHSCENE_APPEND, &sn, nullptr, &sh2); });
    }
    GSHSCENE_Node n2 = group_node(0.5, 3);
    sweep_op(t, e.edit, kMax, "set", [&] { return gshscene_edit_set(e.edit, g1, &n2); });
    sweep_op(t, e.edit, kMax, "move", [&] { return gshscene_edit_move(e.edit, sh, g2, 0); });
    GSHSCENE_Key k3 = K(1, 3);
    sweep_op(t, e.edit, kMax, "upsert add", [&] { return gshscene_edit_upsert(e.edit, &k3, g1, GSHSCENE_APPEND, &sn, &up); });
    GSHSCENE_Node sn2 = shape_node(sq.get(), 4);
    sweep_op(t, e.edit, kMax, "upsert set", [&] { return gshscene_edit_upsert(e.edit, &k3, g1, GSHSCENE_APPEND, &sn2, &up); });
    sweep_op(t, e.edit, kMax, "remove subtree", [&] { return gshscene_edit_remove(e.edit, g2); });
    sweep_op(t, e.edit, kMax, "remove key", [&] { return gshscene_edit_remove_key(e.edit, &k3); });
    VersionRef v;
    publish(s, e, &v);
  }
  EXPECT_TRUE(t.live.empty()) << t.live.size() << " blocks leaked";
}

TEST(EditAllocation, OpeningAnEditFailsCleanly) {
  tracker::Tracker t;
  GSHSCENE_Allocator a = tracker::allocator_for(&t);
  {
    SceneHolder s(nullptr, &a);
    ASSERT_NE(s.scene, nullptr);
    t.fail_from = t.calls;
    t.persistent = true;
    GSHSCENE_Edit * edit = nullptr;
    EXPECT_EQ(gshscene_scene_open_edit(s.scene, nullptr, &edit), GSHSCENE_ERR_OOM);
    EXPECT_EQ(edit, nullptr);
    t.fail_from = -1;
  }
  EXPECT_TRUE(t.live.empty());
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
