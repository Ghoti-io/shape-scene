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
 * The properties of a node: the defaults, the canonical form and every rule
 * that refuses one (version.h, edit.h; story 11).  A node is validated where it
 * enters, in an edit, so the rules are driven through `gshscene_edit_add` and
 * `gshscene_edit_set` and read back through the edit's view.
 */

#include "scene_fixtures.h"

#include <cmath>
#include <limits>

using namespace scene;

namespace {

const double kNaN = std::numeric_limits<double>::quiet_NaN();
const double kInf = std::numeric_limits<double>::infinity();

struct Fx {
  SceneHolder s;
  EditHolder e;
  Ref sq;
  Fx() : e(s), sq(intern_square(s.store())) {}
  GSHSCENE_Result add(const GSHSCENE_Node & n, uint64_t * id = nullptr) {
    return gshscene_edit_add(e.edit, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, &n, nullptr, id);
  }
};

uint64_t identity_hash() {
  GSHGEOM_Map m;
  gshgeom_map_identity(2, &m);
  return gshgeom_map_hash(&m);
}

}  // namespace

TEST(NodeInit, EveryKindStartsAtTheDocumentedDefaults) {
  for (GSHSCENE_Node_Kind kind : {GSHSCENE_NODE_GROUP, GSHSCENE_NODE_SHAPE, GSHSCENE_NODE_INSTANCE, GSHSCENE_NODE_STROKE}) {
    GSHSCENE_Node n;
    std::memset(&n, 0xAB, sizeof n);
    gshscene_node_init(kind, &n);
    EXPECT_EQ(n.kind, kind);
    EXPECT_EQ(n.style, nullptr);
    EXPECT_EQ(n.flags, 0u);
    EXPECT_EQ(n.layer, 0);
    EXPECT_EQ(n.opacity, 1.0);
    EXPECT_EQ(n.clip, nullptr);
    EXPECT_EQ(n.resource, nullptr);
    EXPECT_EQ(n.target, 0u);
    EXPECT_EQ(n.fill_rule, GSHGEOM_FILL_NONZERO);
    EXPECT_EQ(n.clip_rule, GSHGEOM_FILL_NONZERO);
    EXPECT_EQ(gshgeom_map_hash(&n.map), identity_hash());
    EXPECT_EQ(gshgeom_map_hash(&n.clip_map), identity_hash());
    EXPECT_EQ(n.map.dim, 2);
    EXPECT_EQ(n.paint.kind, GSHSCENE_PAINT_SOLID);
    EXPECT_EQ(n.paint.rgba[0], 0.0);
    EXPECT_EQ(n.paint.rgba[1], 0.0);
    EXPECT_EQ(n.paint.rgba[2], 0.0);
    EXPECT_EQ(n.paint.rgba[3], 1.0) << "opaque black";
    EXPECT_TRUE(gshscene_color_space_is_target(&n.paint.space));
    EXPECT_EQ(n.paint.gradient, nullptr);
  }
  gshscene_node_init(GSHSCENE_NODE_GROUP, nullptr);  // ignored
}

TEST(NodeInit, KindsHavePinnedValuesAndNames) {
  EXPECT_EQ((int)GSHSCENE_NODE_GROUP, 1);
  EXPECT_EQ((int)GSHSCENE_NODE_SHAPE, 2);
  EXPECT_EQ((int)GSHSCENE_NODE_INSTANCE, 3);
  EXPECT_EQ((int)GSHSCENE_NODE_STROKE, 4);
  EXPECT_EQ((int)GSHSCENE_NODE_KIND_COUNT, 5);
  EXPECT_STREQ(gshscene_node_kind_string(GSHSCENE_NODE_STROKE), "stroke");
  EXPECT_STREQ(gshscene_node_kind_string(GSHSCENE_NODE_GROUP), "group");
  EXPECT_STREQ(gshscene_node_kind_string(GSHSCENE_NODE_SHAPE), "shape");
  EXPECT_STREQ(gshscene_node_kind_string(GSHSCENE_NODE_INSTANCE), "instance");
  EXPECT_STREQ(gshscene_node_kind_string(GSHSCENE_NODE_KIND_COUNT), "unknown node kind");
  EXPECT_STREQ(gshscene_node_kind_string((GSHSCENE_Node_Kind)0), "unknown node kind");
  EXPECT_EQ(GSHSCENE_NODE_HIDDEN, 1u);
  EXPECT_EQ(GSHSCENE_ROOT_ID, 1u);
  EXPECT_EQ(GSHSCENE_APPEND, (size_t)-1);
}

TEST(NodeRules, TheKindAndTheFlagsAreChecked) {
  Fx f;
  GSHSCENE_Node n = group_node();
  n.kind = (GSHSCENE_Node_Kind)0;
  EXPECT_EQ(f.add(n), GSHSCENE_ERR_INVALID);
  n.kind = GSHSCENE_NODE_KIND_COUNT;
  EXPECT_EQ(f.add(n), GSHSCENE_ERR_INVALID);
  n.kind = (GSHSCENE_Node_Kind)99;
  EXPECT_EQ(f.add(n), GSHSCENE_ERR_INVALID);
  n = group_node();
  n.flags = 2;
  EXPECT_EQ(f.add(n), GSHSCENE_ERR_INVALID);
  n.flags = 3;
  EXPECT_EQ(f.add(n), GSHSCENE_ERR_INVALID);
  n.flags = GSHSCENE_NODE_HIDDEN;
  uint64_t id = 0;
  EXPECT_EQ(f.add(n, &id), GSHSCENE_OK);
  GSHSCENE_Node_Info info;
  ASSERT_EQ(gshscene_edit_node(f.e.edit, id, &info), GSHSCENE_OK);
  EXPECT_EQ(info.node.flags, GSHSCENE_NODE_HIDDEN);
  EXPECT_EQ(gshscene_edit_add(f.e.edit, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, nullptr, nullptr, &id), GSHSCENE_ERR_INVALID);
}

TEST(NodeRules, AKindOutsideTheEnumIsRefusedWhateverTheOtherFieldsSay) {
  Fx f;
  for (int bad : {0, 5, 6, 99, -1}) {
    GSHSCENE_Node n = instance_node(GSHSCENE_ROOT_ID);  // a node that would pass as an instance
    n.kind = (GSHSCENE_Node_Kind)bad;
    EXPECT_EQ(f.add(n), GSHSCENE_ERR_INVALID) << bad;
    n = shape_node(f.sq.get());
    n.kind = (GSHSCENE_Node_Kind)bad;
    n.target = 7;
    EXPECT_EQ(f.add(n), GSHSCENE_ERR_INVALID) << bad;
  }
}

TEST(NodeRules, ANodeKeepsItsKeyThroughASetAndHoldsItsClipOnlyWhileItLives) {
  SceneHolder s;
  Ref sq(intern_square(s.store()));
  Ref cp(intern_fixture(s.store()));
  GSHSCENE_Key k = K(3, 3);
  uint64_t id;
  VersionRef v1;
  {
    EditHolder e(s);
    GSHSCENE_Node g = group_node(0.5);
    g.clip = cp.get();
    g.clip_rule = GSHGEOM_FILL_EVEN_ODD;
    id = add(e.edit, GSHSCENE_ROOT_ID, g, &k);
    publish(s, e, &v1);
  }
  EXPECT_EQ(gshscene_resource_refcount(cp.get()), 2u) << "the test's and the node's";
  {
    EditHolder e(s);
    // The same properties: nothing is touched, even with a clip (a clip's rule and map are compared).
    GSHSCENE_Node same = group_node(0.5);
    same.clip = cp.get();
    same.clip_rule = GSHGEOM_FILL_EVEN_ODD;
    ASSERT_EQ(gshscene_edit_set(e.edit, id, &same), GSHSCENE_OK);
    VersionRef v;
    publish(s, e, &v);
    EXPECT_EQ(v.get(), v1.get());
  }
  VersionRef v2;
  {
    EditHolder e(s);
    GSHSCENE_Node g = group_node(0.25);  // a change: the node is copied, and the copy keeps its key
    g.clip = cp.get();
    g.clip_rule = GSHGEOM_FILL_EVEN_ODD;
    ASSERT_EQ(gshscene_edit_set(e.edit, id, &g), GSHSCENE_OK);
    EXPECT_EQ(gshscene_resource_refcount(cp.get()), 3u) << "the test, v1's node, and the copy: held once each";
    publish(s, e, &v2);
  }
  GSHSCENE_Node_Info info = info_of(v2.get(), id);
  EXPECT_TRUE(info.has_key);
  EXPECT_EQ(info.key.value, 3u);
  uint64_t found = 0;
  EXPECT_EQ(gshscene_version_lookup_key(v2.get(), &k, &found), GSHSCENE_OK);
  v1.reset();
  v2.reset();
  gshscene_scene_destroy(s.scene);
  s.scene = nullptr;
  EXPECT_EQ(gshscene_resource_refcount(cp.get()), 1u) << "every node let go of its clip";
}

TEST(NodeRules, TheMapIsValidAnd2D) {
  Fx f;
  for (GSHSCENE_Node_Kind kind : {GSHSCENE_NODE_GROUP, GSHSCENE_NODE_SHAPE, GSHSCENE_NODE_INSTANCE}) {
    GSHSCENE_Node n = kind == GSHSCENE_NODE_GROUP ? group_node()
                      : kind == GSHSCENE_NODE_SHAPE ? shape_node(f.sq.get())
                                                    : instance_node(GSHSCENE_ROOT_ID);
    n.map.m[0][0] = kNaN;
    EXPECT_EQ(f.add(n), GSHSCENE_ERR_INVALID) << kind;
    n.map.m[0][0] = kInf;
    EXPECT_EQ(f.add(n), GSHSCENE_ERR_INVALID) << kind;
    gshgeom_map_identity(3, &n.map);
    EXPECT_EQ(f.add(n), GSHSCENE_ERR_UNSUPPORTED) << kind << ": a 3D map waits for the depth ordering context";
    n.map.dim = 4;
    EXPECT_EQ(f.add(n), GSHSCENE_ERR_INVALID) << kind;
  }
  EXPECT_EQ(gshscene_edit_node_count(f.e.edit), 1u) << "nothing was added";
}

TEST(NodeRules, AGroupsOpacityIsFiniteAndInUnitRange) {
  Fx f;
  for (double bad : {-0.001, 1.001, kNaN, kInf, -kInf, 2.0, -1.0}) {
    EXPECT_EQ(f.add(group_node(bad)), GSHSCENE_ERR_INVALID) << bad;
  }
  for (double good : {0.0, 1.0, 0.5, 1.0 - 1e-16}) {
    EXPECT_EQ(f.add(group_node(good)), GSHSCENE_OK) << good;
  }
}

TEST(NodeRules, AGroupsClipIsAPathOrGlyphRunOfTheStoreWithAValidMapAndRule) {
  Fx f;
  StoreHolder other;
  Ref foreign(intern_square(other.store));
  GSHSCENE_Node n = group_node();
  n.clip = foreign.get();
  EXPECT_EQ(f.add(n), GSHSCENE_ERR_INVALID) << "another store's resource";
  // A picture is not a region.
  ListHolder l(f.s.store());
  ASSERT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
  Ref picture;
  ASSERT_EQ(gshscene_store_intern_picture(f.s.store(), l.list, &picture.resource), GSHSCENE_OK);
  n.clip = picture.get();
  EXPECT_EQ(f.add(n), GSHSCENE_ERR_INVALID);
  // A glyph run is.
  GSHSCENE_Glyph_Run run;
  std::memset(&run, 0, sizeof run);
  run.source = GSHSCENE_GLYPHS_FROM_FONT;
  Ref run_face(intern_face(f.s.store(), 7));
  run.face = run_face.get();
  run.size = 10;
  Ref font_run;
  ASSERT_EQ(gshscene_store_intern_glyph_run(f.s.store(), &run, &font_run.resource), GSHSCENE_OK);
  n.clip = font_run.get();
  EXPECT_EQ(f.add(n), GSHSCENE_OK);
  n.clip = f.sq.get();
  EXPECT_EQ(f.add(n), GSHSCENE_OK);
  n.clip_rule = (GSHGEOM_Fill_Rule)2;
  EXPECT_EQ(f.add(n), GSHSCENE_ERR_INVALID);
  n.clip_rule = GSHGEOM_FILL_EVEN_ODD;
  n.clip_map.t[0] = kNaN;
  EXPECT_EQ(f.add(n), GSHSCENE_ERR_INVALID);
  n.clip_map = translate(1, 2);
  gshgeom_map_identity(3, &n.clip_map);
  EXPECT_EQ(f.add(n), GSHSCENE_ERR_UNSUPPORTED);
  n.clip_map = translate(1, 2);
  uint64_t id = 0;
  EXPECT_EQ(f.add(n, &id), GSHSCENE_OK);
  GSHSCENE_Node_Info info;
  ASSERT_EQ(gshscene_edit_node(f.e.edit, id, &info), GSHSCENE_OK);
  EXPECT_EQ(info.node.clip, f.sq.get());
  EXPECT_EQ(info.node.clip_rule, GSHGEOM_FILL_EVEN_ODD);
  EXPECT_EQ(gshgeom_map_hash(&info.node.clip_map), gshgeom_map_hash(&n.clip_map));
}

TEST(NodeRules, WithoutAClipItsMapAndRuleAreIgnoredAndReadBackCanonical) {
  Fx f;
  GSHSCENE_Node n = group_node();
  n.clip_rule = (GSHGEOM_Fill_Rule)9;  // would be refused with a clip
  n.clip_map = translate(5, 5);
  uint64_t id = 0;
  ASSERT_EQ(f.add(n, &id), GSHSCENE_OK);
  GSHSCENE_Node_Info info;
  ASSERT_EQ(gshscene_edit_node(f.e.edit, id, &info), GSHSCENE_OK);
  EXPECT_EQ(info.node.clip, nullptr);
  EXPECT_EQ(info.node.clip_rule, GSHGEOM_FILL_NONZERO);
  EXPECT_EQ(gshgeom_map_hash(&info.node.clip_map), identity_hash());
}

TEST(NodeRules, AShapeNeedsAPathOrGlyphRunOfTheStoreAndAValidRuleAndPaint) {
  Fx f;
  GSHSCENE_Node n = shape_node(nullptr);
  EXPECT_EQ(f.add(n), GSHSCENE_ERR_INVALID) << "no resource";
  StoreHolder other;
  Ref foreign(intern_square(other.store));
  n.resource = foreign.get();
  EXPECT_EQ(f.add(n), GSHSCENE_ERR_INVALID);
  ListHolder l(f.s.store());
  gshscene_list_close(l.list);
  Ref picture;
  ASSERT_EQ(gshscene_store_intern_picture(f.s.store(), l.list, &picture.resource), GSHSCENE_OK);
  n.resource = picture.get();
  EXPECT_EQ(f.add(n), GSHSCENE_ERR_INVALID) << "a picture is drawn by an instance, not filled";
  n = shape_node(f.sq.get());
  n.fill_rule = (GSHGEOM_Fill_Rule)2;
  EXPECT_EQ(f.add(n), GSHSCENE_ERR_INVALID);
  n.fill_rule = GSHGEOM_FILL_EVEN_ODD;
  EXPECT_EQ(f.add(n), GSHSCENE_OK);
  // The paint follows the rules of a list's fill.
  n = shape_node(f.sq.get());
  n.paint.kind = GSHSCENE_PAINT_GRADIENT;
  EXPECT_EQ(f.add(n), GSHSCENE_ERR_INVALID) << "a gradient paint names a gradient resource";
  n = shape_node(f.sq.get());
  n.paint.kind = GSHSCENE_PAINT_PALETTE;
  n.paint.entry = 0xFFFFu;
  EXPECT_EQ(f.add(n), GSHSCENE_ERR_INVALID) << "0xFFFF is a font's foreground and is never an entry (N14)";
  n.paint.entry = GSHSCENE_PALETTE_ENTRY_MAX;
  EXPECT_EQ(f.add(n), GSHSCENE_OK) << "a palette paint follows a list fill's rules";
  n = shape_node(f.sq.get());
  n.paint.kind = (GSHSCENE_Paint_Kind)9;
  EXPECT_EQ(f.add(n), GSHSCENE_ERR_INVALID);
  for (double alpha : {-0.1, 1.1, kNaN, kInf}) {
    n = shape_node(f.sq.get());
    n.paint = solid(0, 0, 0, alpha);
    EXPECT_EQ(f.add(n), GSHSCENE_ERR_INVALID) << alpha;
  }
  n = shape_node(f.sq.get());
  n.paint = solid(kNaN, 0, 0, 1);
  EXPECT_EQ(f.add(n), GSHSCENE_ERR_INVALID);
  n.paint = solid(0, 0, 0, 1);
  n.paint.space = space_of(GCOL_PRIMARIES_PROPHOTO, GCOL_TRANSFER_SRGB);
  EXPECT_EQ(f.add(n), GSHSCENE_ERR_UNSUPPORTED);
  n.paint = solid(-0.5, 2.0, 1e9, 0.25);  // past N1's bound (2^16): refused, after the checks above
  EXPECT_EQ(f.add(n), GSHSCENE_ERR_LIMIT);
  n.paint = solid(-0.5, 2.0, 65536.0, 0.25);  // outside the gamut is allowed (AD-23), up to the bound
  uint64_t id = 0;
  ASSERT_EQ(f.add(n, &id), GSHSCENE_OK);
  GSHSCENE_Node_Info info;
  ASSERT_EQ(gshscene_edit_node(f.e.edit, id, &info), GSHSCENE_OK);
  EXPECT_EQ(info.node.paint.rgba[0], -0.5);
  EXPECT_EQ(info.node.paint.rgba[1], 2.0);
  EXPECT_EQ(info.node.paint.rgba[2], 65536.0);
  EXPECT_EQ(info.node.paint.rgba[3], 0.25);
  EXPECT_EQ(info.node.resource, f.sq.get());
}

TEST(NodeRules, AnInstanceNeedsATargetAndNothingElse) {
  Fx f;
  GSHSCENE_Node n = instance_node(0);
  EXPECT_EQ(f.add(n), GSHSCENE_ERR_INVALID);
  n = instance_node(1234567);  // a target that does not exist yet is allowed: publish checks it
  uint64_t id = 0;
  EXPECT_EQ(f.add(n, &id), GSHSCENE_OK);
  GSHSCENE_Node_Info info;
  ASSERT_EQ(gshscene_edit_node(f.e.edit, id, &info), GSHSCENE_OK);
  EXPECT_EQ(info.node.target, 1234567u);
}

TEST(NodeRules, FieldsThatDoNotApplyAreDroppedSoANodeHasOneSpelling) {
  Fx f;
  GSHSCENE_Node n = instance_node(GSHSCENE_ROOT_ID, 4);
  n.opacity = 0.25;
  n.clip = f.sq.get();
  n.resource = f.sq.get();
  n.fill_rule = GSHGEOM_FILL_EVEN_ODD;
  n.paint = solid(1, 1, 1, 1);
  uint64_t id = 0;
  ASSERT_EQ(f.add(n, &id), GSHSCENE_OK);
  GSHSCENE_Node_Info info;
  ASSERT_EQ(gshscene_edit_node(f.e.edit, id, &info), GSHSCENE_OK);
  GSHSCENE_Node want = instance_node(GSHSCENE_ROOT_ID, 4);
  EXPECT_EQ(info.node.opacity, want.opacity);
  EXPECT_EQ(info.node.clip, nullptr);
  EXPECT_EQ(info.node.resource, nullptr);
  EXPECT_EQ(info.node.fill_rule, GSHGEOM_FILL_NONZERO);
  EXPECT_EQ(info.node.paint.rgba[3], 1.0);
  EXPECT_EQ(info.node.paint.rgba[0], 0.0);
  EXPECT_EQ(info.node.layer, 4);
  // And a shape with group fields, a group with shape fields.
  n = shape_node(f.sq.get(), -2);
  n.opacity = 0.5;
  n.target = 77;
  ASSERT_EQ(f.add(n, &id), GSHSCENE_OK);
  ASSERT_EQ(gshscene_edit_node(f.e.edit, id, &info), GSHSCENE_OK);
  EXPECT_EQ(info.node.opacity, 1.0);
  EXPECT_EQ(info.node.target, 0u);
  n = group_node(0.5, 3);
  n.resource = f.sq.get();
  n.target = 5;
  n.fill_rule = GSHGEOM_FILL_EVEN_ODD;
  ASSERT_EQ(f.add(n, &id), GSHSCENE_OK);
  ASSERT_EQ(gshscene_edit_node(f.e.edit, id, &info), GSHSCENE_OK);
  EXPECT_EQ(info.node.resource, nullptr);
  EXPECT_EQ(info.node.target, 0u);
  EXPECT_EQ(info.node.fill_rule, GSHGEOM_FILL_NONZERO);
  EXPECT_EQ(info.node.opacity, 0.5);
}

TEST(NodeRules, ANodeHoldsTheResourcesItNamesAndReleasesThem) {
  SceneHolder s;
  Ref sq(intern_square(s.store()));
  EXPECT_EQ(gshscene_resource_refcount(sq.get()), 1u);
  {
    EditHolder e(s);
    GSHSCENE_Node n = shape_node(sq.get());
    uint64_t id = add(e.edit, GSHSCENE_ROOT_ID, n);
    EXPECT_NE(id, 0u);
    EXPECT_EQ(gshscene_resource_refcount(sq.get()), 2u) << "the node holds one";
    VersionRef v;
    publish(s, e, &v);
    EXPECT_EQ(gshscene_resource_refcount(sq.get()), 2u) << "the version's node, shared with the spent edit's release";
  }
  // The scene's current version still holds it; destroying the scene lets go.
  EXPECT_EQ(gshscene_resource_refcount(sq.get()), 2u);
  gshscene_scene_destroy(s.scene);
  s.scene = nullptr;
  EXPECT_EQ(gshscene_resource_refcount(sq.get()), 1u);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
