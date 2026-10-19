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
 * Reading a version, the visitor and the instance paths, hidden nodes, and the
 * path order and key (version.h; story 11).
 */

#include "scene_fixtures.h"

#include <algorithm>
#include <set>

using namespace scene;

namespace {

using Ids = std::vector<uint64_t>;

GSHGEOM_Point apply(const GSHGEOM_Map & m, double x, double y) {
  GSHGEOM_Point out;
  EXPECT_EQ(gshgeom_map_apply_point(&m, P(x, y), &out), GSHGEOM_OK);
  return out;
}

/** The scene of the visitor tests:
 *  root: G, D (hidden), I2 -> D (translate 100, 0)
 *  G (translate 10, 0): S1, I1 -> D (translate 1, 2), S2
 *  D (hidden): T (translate 0, 5)  */
struct Doc {
  SceneHolder s;
  Ref sq;
  VersionRef v;
  uint64_t g, d, t, s1, i1, s2, i2;
  Doc() : sq(intern_square(s.store())) {
    EditHolder e(s);
    GSHSCENE_Node gn = group_node();
    gn.map = translate(10, 0);
    g = add(e.edit, GSHSCENE_ROOT_ID, gn);
    GSHSCENE_Node dn = group_node();
    dn.flags = GSHSCENE_NODE_HIDDEN;
    d = add(e.edit, GSHSCENE_ROOT_ID, dn);
    GSHSCENE_Node in2 = instance_node(d);
    in2.map = translate(100, 0);
    i2 = add(e.edit, GSHSCENE_ROOT_ID, in2);
    s1 = add(e.edit, g, shape_node(sq.get()));
    GSHSCENE_Node in1 = instance_node(d);
    in1.map = translate(1, 2);
    i1 = add(e.edit, g, in1);
    s2 = add(e.edit, g, shape_node(sq.get()));
    GSHSCENE_Node tn = shape_node(sq.get());
    tn.map = translate(0, 5);
    t = add(e.edit, d, tn);
    publish(s, e, &v);
  }
};

}  // namespace

// --- reading ----------------------------------------------------------------------------------

TEST(VersionRead, NodeChildAndTheirRefusals) {
  Doc doc;
  GSHSCENE_Node_Info info;
  ASSERT_EQ(gshscene_version_node(doc.v.get(), doc.g, &info), GSHSCENE_OK);
  EXPECT_EQ(info.id, doc.g);
  EXPECT_EQ(info.parent, GSHSCENE_ROOT_ID);
  EXPECT_EQ(info.child_count, 3u);
  EXPECT_EQ(info.node.kind, GSHSCENE_NODE_GROUP);
  GSHGEOM_Map ten = translate(10, 0);
  EXPECT_EQ(gshgeom_map_hash(&info.node.map), gshgeom_map_hash(&ten));
  EXPECT_EQ(children_of(doc.v.get(), doc.g), (Ids{doc.s1, doc.i1, doc.s2}));
  EXPECT_EQ(children_of(doc.v.get(), GSHSCENE_ROOT_ID), (Ids{doc.g, doc.d, doc.i2}));
  EXPECT_EQ(info_of(doc.v.get(), doc.s1).child_count, 0u);
  EXPECT_EQ(info_of(doc.v.get(), doc.i1).node.target, doc.d);
  EXPECT_EQ(info_of(doc.v.get(), doc.d).node.flags, GSHSCENE_NODE_HIDDEN);
  uint64_t c = 0;
  EXPECT_EQ(gshscene_version_node(doc.v.get(), 9999, &info), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_version_node(doc.v.get(), 0, &info), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_version_node(nullptr, doc.g, &info), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_version_node(doc.v.get(), doc.g, nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_version_child(doc.v.get(), doc.g, 3, &c), GSHSCENE_ERR_INVALID) << "past the end";
  EXPECT_EQ(gshscene_version_child(doc.v.get(), doc.s1, 0, &c), GSHSCENE_ERR_INVALID) << "a shape has no children";
  EXPECT_EQ(gshscene_version_child(doc.v.get(), 9999, 0, &c), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_version_child(nullptr, doc.g, 0, &c), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_version_child(doc.v.get(), doc.g, 0, nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_version_node_count(doc.v.get()), 8u);
}

TEST(VersionRead, NodesCarryTheirKeys) {
  SceneHolder s;
  Ref sq(intern_square(s.store()));
  EditHolder e(s);
  GSHSCENE_Key k = K(0xdead, 0xbeef);
  uint64_t id = add(e.edit, GSHSCENE_ROOT_ID, shape_node(sq.get()), &k);
  uint64_t plain = add(e.edit, GSHSCENE_ROOT_ID, shape_node(sq.get()));
  VersionRef v;
  publish(s, e, &v);
  GSHSCENE_Node_Info info = info_of(v.get(), id);
  EXPECT_TRUE(info.has_key);
  EXPECT_EQ(info.key.domain, 0xdeadu);
  EXPECT_EQ(info.key.value, 0xbeefu);
  info = info_of(v.get(), plain);
  EXPECT_FALSE(info.has_key);
  EXPECT_EQ(info.key.domain, 0u);
  EXPECT_EQ(info.key.value, 0u);
}

// --- the visitor ---------------------------------------------------------------------------------

TEST(VersionVisit, EveryOccurrenceInDrawOrderWithItsInstancePath) {
  Doc doc;
  std::vector<Occurrence> got = occurrences(doc.v.get());
  const uint64_t R = GSHSCENE_ROOT_ID;
  std::vector<Ids> want_paths = {
      {R}, {doc.g}, {doc.s1}, {doc.i1}, {doc.i1, doc.d}, {doc.i1, doc.t}, {doc.s2}, {doc.i2}, {doc.i2, doc.d}, {doc.i2, doc.t}};
  ASSERT_EQ(got.size(), want_paths.size());
  for (size_t i = 0; i < got.size(); i++) {
    EXPECT_EQ(got[i].path, want_paths[i]) << i;
    EXPECT_EQ(got[i].node, want_paths[i].back()) << i;
  }
  for (const Occurrence & o : got) {
    EXPECT_EQ(o.props.kind, o.kind) << "the visitor gives the node's properties";
  }
  EXPECT_EQ(got[3].props.target, doc.d);
  EXPECT_EQ(got[0].kind, GSHSCENE_NODE_GROUP);
  EXPECT_EQ(got[2].kind, GSHSCENE_NODE_SHAPE);
  EXPECT_EQ(got[3].kind, GSHSCENE_NODE_INSTANCE);
  // The hidden definition is drawn only through its instances: never at the root, in place.
  for (const Occurrence & o : got) {
    if (o.node == doc.d) {
      EXPECT_EQ(o.path.size(), 2u) << "D only under an instance";
    }
  }
  // Composed placement, as a sink composes it.
  GSHGEOM_Point p = apply(got[5].map, 0, 0);  // T under I1: G (10,0), I1 (1,2), T (0,5)
  EXPECT_EQ(p.x, 11.0);
  EXPECT_EQ(p.y, 7.0);
  p = apply(got[9].map, 0, 0);  // T under I2: I2 (100,0), T (0,5)
  EXPECT_EQ(p.x, 100.0);
  EXPECT_EQ(p.y, 5.0);
  p = apply(got[2].map, 0, 0);  // S1 in G
  EXPECT_EQ(p.x, 10.0);
  EXPECT_EQ(p.y, 0.0);
  p = apply(got[1].map, 1, 1);  // a group's map is the one its children are placed by
  EXPECT_EQ(p.x, 11.0);
  EXPECT_EQ(p.y, 1.0);
}

TEST(VersionVisit, EveryOccurrenceHasADistinctPathInATotalOrder) {
  Doc doc;
  std::vector<Occurrence> got = occurrences(doc.v.get());
  for (size_t i = 0; i < got.size(); i++) {
    for (size_t j = 0; j < got.size(); j++) {
      int c = gshscene_instance_path_compare(got[i].path.data(), got[i].path.size(), got[j].path.data(), got[j].path.size());
      int r = gshscene_instance_path_compare(got[j].path.data(), got[j].path.size(), got[i].path.data(), got[i].path.size());
      EXPECT_EQ(c == 0, i == j) << i << "," << j;
      EXPECT_EQ(c < 0, r > 0) << "antisymmetric";
      EXPECT_EQ(c > 0, r < 0);
    }
  }
  std::set<uint64_t> keys;
  for (const Occurrence & o : got) {
    EXPECT_TRUE(keys.insert(gshscene_instance_path_key(o.path.data(), o.path.size())).second);
  }
  // Transitive: sorting twice from two orders agrees.
  std::vector<Ids> a, b;
  for (const Occurrence & o : got) {
    a.push_back(o.path);
  }
  b = a;
  std::reverse(b.begin(), b.end());
  auto less = [](const Ids & x, const Ids & y) {
    return gshscene_instance_path_compare(x.data(), x.size(), y.data(), y.size()) < 0;
  };
  std::sort(a.begin(), a.end(), less);
  std::sort(b.begin(), b.end(), less);
  EXPECT_EQ(a, b);
}

TEST(InstancePath, ComparesElementByElementWithAPrefixBeforeItsExtensions) {
  const uint64_t a[] = {1, 5, 9};
  const uint64_t b[] = {1, 5};
  const uint64_t c[] = {1, 6};
  const uint64_t d[] = {2};
  EXPECT_EQ(gshscene_instance_path_compare(a, 3, a, 3), 0);
  EXPECT_LT(gshscene_instance_path_compare(b, 2, a, 3), 0) << "a prefix comes first";
  EXPECT_GT(gshscene_instance_path_compare(a, 3, b, 2), 0);
  EXPECT_LT(gshscene_instance_path_compare(a, 3, c, 2), 0) << "5 < 6 decides before the length does";
  EXPECT_LT(gshscene_instance_path_compare(c, 2, d, 1), 0);
  EXPECT_LT(gshscene_instance_path_compare(nullptr, 0, a, 3), 0) << "the empty path is first";
  EXPECT_EQ(gshscene_instance_path_compare(nullptr, 0, nullptr, 0), 0);
  const uint64_t big1[] = {UINT64_C(0x8000000000000000)};
  const uint64_t big2[] = {1};
  EXPECT_GT(gshscene_instance_path_compare(big1, 1, big2, 1), 0) << "unsigned";
}

TEST(InstancePath, KeyIsFnv1aOfTheCountAndTheIdsAndDependsOnOrder) {
  const uint64_t p12[] = {1, 2};
  const uint64_t p21[] = {2, 1};
  const uint64_t p1[] = {1};
  EXPECT_NE(gshscene_instance_path_key(p12, 2), gshscene_instance_path_key(p21, 2));
  EXPECT_NE(gshscene_instance_path_key(p1, 1), gshscene_instance_path_key(p12, 2));
  EXPECT_EQ(gshscene_instance_path_key(p12, 2), gshscene_instance_path_key(p12, 2));
  GSHGEOM_Hash h;
  gshgeom_hash_init(&h);
  gshgeom_hash_u64(&h, 2);
  gshgeom_hash_u64(&h, 1);
  gshgeom_hash_u64(&h, 2);
  EXPECT_EQ(gshscene_instance_path_key(p12, 2), gshgeom_hash_value(&h));
  gshgeom_hash_init(&h);
  gshgeom_hash_u64(&h, 0);
  EXPECT_EQ(gshscene_instance_path_key(nullptr, 0), gshgeom_hash_value(&h));
}

TEST(VersionVisit, TheVisitorMayStopAndNullArgumentsAreRefused) {
  Doc doc;
  int calls = 0;
  auto stop_at_three = [](void * ctx, const GSHSCENE_Visit *) {
    int * n = static_cast<int *>(ctx);
    return ++*n == 3 ? 42 : 0;
  };
  EXPECT_EQ(gshscene_version_visit(doc.v.get(), stop_at_three, &calls), 42);
  EXPECT_EQ(calls, 3);
  EXPECT_EQ(gshscene_version_visit(nullptr, collect, nullptr), -1);
  EXPECT_EQ(gshscene_version_visit(doc.v.get(), nullptr, nullptr), -1);
}

TEST(VersionVisit, HiddenFlagsBelowATargetAreHonouredAndTheTargetsOwnIsNot) {
  SceneHolder s;
  Ref sq(intern_square(s.store()));
  EditHolder e(s);
  GSHSCENE_Node dn = group_node();
  dn.flags = GSHSCENE_NODE_HIDDEN;
  uint64_t d = add(e.edit, GSHSCENE_ROOT_ID, dn);
  GSHSCENE_Node hn = shape_node(sq.get());
  hn.flags = GSHSCENE_NODE_HIDDEN;
  uint64_t h = add(e.edit, d, hn);        // hidden inside the hidden group
  uint64_t t = add(e.edit, d, shape_node(sq.get()));
  uint64_t i_d = add(e.edit, GSHSCENE_ROOT_ID, instance_node(d));
  uint64_t i_h = add(e.edit, GSHSCENE_ROOT_ID, instance_node(h));  // the target itself is hidden
  VersionRef v;
  publish(s, e, &v);
  std::vector<Occurrence> got = occurrences(v.get());
  std::vector<Ids> paths;
  for (const Occurrence & o : got) {
    paths.push_back(o.path);
  }
  std::vector<Ids> want = {{GSHSCENE_ROOT_ID}, {i_d}, {i_d, d}, {i_d, t}, {i_h}, {i_h, h}};
  EXPECT_EQ(paths, want);
}

TEST(VersionVisit, AnInstanceOfAnInstanceExtendsThePathTwice) {
  SceneHolder s;
  Ref sq(intern_square(s.store()));
  EditHolder e(s);
  uint64_t shape = add(e.edit, GSHSCENE_ROOT_ID, shape_node(sq.get()));
  GSHSCENE_Node dn = group_node();
  dn.flags = GSHSCENE_NODE_HIDDEN;
  uint64_t inner = add(e.edit, GSHSCENE_ROOT_ID, dn);
  uint64_t inner_shape = add(e.edit, inner, shape_node(sq.get()));
  uint64_t mid = add(e.edit, GSHSCENE_ROOT_ID, dn);
  uint64_t mid_inst = add(e.edit, mid, instance_node(inner));
  uint64_t top = add(e.edit, GSHSCENE_ROOT_ID, instance_node(mid));
  VersionRef v;
  publish(s, e, &v);
  std::vector<Occurrence> got = occurrences(v.get());
  std::vector<Ids> paths;
  for (const Occurrence & o : got) {
    paths.push_back(o.path);
  }
  std::vector<Ids> want = {{GSHSCENE_ROOT_ID}, {shape}, {top}, {top, mid}, {top, mid_inst}, {top, mid_inst, inner},
      {top, mid_inst, inner_shape}};
  EXPECT_EQ(paths, want);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
