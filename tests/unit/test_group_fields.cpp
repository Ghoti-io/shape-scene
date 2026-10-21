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
 * PDF's group fields (T1, color-paint story P3): a group's isolation, knockout and soft mask on a list's group push
 * and a group node, recorded, validated, held, compared, hashed and dumped (the default spelled as before the fields,
 * the others as the independent generator writes them), emitted from a version, damaged by a change, and refused by
 * the planning pass of a sink whose caps lack them (S19).
 */

#include "scene_fixtures.h"

#include "../known_answers.h"

#include <ghoti.io/shape-scene/fixed.h>

using namespace scene;

namespace {

/** The picture fixture of tools/gen-scene-vectors.py (known::kPictureHash). */
const GSHSCENE_Resource * fixture_picture(GSHSCENE_Store * store, const GSHSCENE_Resource * sq, const GSHSCENE_Resource * p2) {
  GSHGEOM_Map tr = translate(1.5, -2.25);
  GSHSCENE_List * inner = nullptr;
  gshscene_list_create(nullptr, nullptr, store, &inner);
  gshscene_list_fill(inner, sq, nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0);
  GSHSCENE_Group half = make_group(0.5);
  gshscene_list_group_push(inner, &half, 0, 0);
  GSHSCENE_Paint orange = solid(1.0, 0.5, 0.25, 0.75);
  gshscene_list_fill(inner, p2, &tr, GSHGEOM_FILL_EVEN_ODD, &orange, 2, 0);
  gshscene_list_group_pop(inner);
  gshscene_list_close(inner);
  const GSHSCENE_Resource * picture = nullptr;
  gshscene_store_intern_picture(store, inner, &picture);
  gshscene_list_destroy(inner);
  return picture;
}

GSHSCENE_Group pdf_group(double opacity, uint32_t isolation, uint32_t knockout, const GSHSCENE_Resource * mask,
    uint32_t composite = GSHSCENE_COMPOSITE_SRC_OVER) {
  GSHSCENE_Group g = make_group(opacity, GSHSCENE_ORDER_FLAT, composite);
  g.isolation = isolation;
  g.knockout = knockout;
  g.soft_mask = mask;
  return g;
}

uint64_t live(GSHSCENE_Store * store) {
  GSHSCENE_Store_Stats stats;
  gshscene_store_stats(store, &stats);
  return stats.live;
}

}  // namespace

TEST(GroupFields, TheDefaultsAreZeroAndReadBack) {
  StoreHolder s;
  ListHolder l(s.store);
  ASSERT_EQ(gshscene_list_group_push(l.list, nullptr, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_pop(l.list), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_push_fixed(l.list, GSHSCENE_COMPOSITE_MULTIPLY, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_pop(l.list), GSHSCENE_OK);
  for (size_t i : {0u, 2u}) {
    const GSHSCENE_Item * item = nullptr;
    ASSERT_EQ(gshscene_list_item(l.list, i, &item), GSHSCENE_OK);
    EXPECT_EQ(item->group.isolation, (uint32_t)GSHSCENE_ISOLATION_ISOLATED);
    EXPECT_EQ(item->group.knockout, 0u);
    EXPECT_EQ(item->group.soft_mask, nullptr);
  }
  EXPECT_EQ((int)GSHSCENE_ISOLATION_ISOLATED, 0) << "a zero-filled group is isolated";
  EXPECT_EQ((int)GSHSCENE_ISOLATION_NON_ISOLATED, 1);
  EXPECT_EQ((int)GSHSCENE_ISOLATION_COUNT, 2);
}

TEST(GroupFields, EachIsRecordedAndEachInvalidValueIsRefused) {
  StoreHolder s;
  StoreHolder other;
  Ref sq(intern_square(s.store));
  Ref p2(intern_fixture(s.store));
  Ref pic(fixture_picture(s.store, sq.get(), p2.get()));
  Ref other_sq(intern_square(other.store));
  Ref other_p2(intern_fixture(other.store));
  Ref other_pic(fixture_picture(other.store, other_sq.get(), other_p2.get()));
  ListHolder l(s.store);
  GSHSCENE_Group g = pdf_group(0.5, GSHSCENE_ISOLATION_NON_ISOLATED, 1, pic.get(), GSHSCENE_COMPOSITE_HUE);
  ASSERT_EQ(gshscene_list_group_push(l.list, &g, 3, 1.0), GSHSCENE_OK);
  const GSHSCENE_Item * item = nullptr;
  ASSERT_EQ(gshscene_list_item(l.list, 0, &item), GSHSCENE_OK);
  EXPECT_EQ(item->group.isolation, (uint32_t)GSHSCENE_ISOLATION_NON_ISOLATED);
  EXPECT_EQ(item->group.knockout, 1u);
  EXPECT_EQ(item->group.soft_mask, pic.get());
  EXPECT_EQ(item->group.composite, (uint32_t)GSHSCENE_COMPOSITE_HUE);
  struct Bad {
    const char * why;
    GSHSCENE_Group g;
  };
  const Bad bad[] = {
      {"an isolation past the enum", pdf_group(1.0, GSHSCENE_ISOLATION_COUNT, 0, nullptr)},
      {"a knockout of 2", pdf_group(1.0, 0, 2, nullptr)},
      {"a knockout of all ones", pdf_group(1.0, 0, 0xffffffffu, nullptr)},
      {"a soft mask that is a path", pdf_group(1.0, 0, 0, sq.get())},
      {"a soft mask of another store", pdf_group(1.0, 0, 0, other_pic.get())},
  };
  size_t count = gshscene_list_count(l.list);
  for (const Bad & b : bad) {
    EXPECT_EQ(gshscene_list_group_push(l.list, &b.g, 0, 0.0), GSHSCENE_ERR_INVALID) << b.why;
    EXPECT_EQ(gshscene_list_count(l.list), count) << b.why << ": a failing call leaves the list as it was";
  }
}

TEST(GroupFields, TheListHoldsTheSoftMaskAndReleasesItOnDestroyResetAndTruncate) {
  StoreHolder s;
  Ref sq(intern_square(s.store));
  Ref p2(intern_fixture(s.store));
  const GSHSCENE_Resource * pic = fixture_picture(s.store, sq.get(), p2.get());
  uint64_t before = live(s.store);
  {
    ListHolder l(s.store);
    GSHSCENE_Group g = pdf_group(1.0, 0, 0, pic);
    ASSERT_EQ(gshscene_list_group_push(l.list, &g, 0, 0.0), GSHSCENE_OK);
    gshscene_resource_release(pic); // the list holds its own
    EXPECT_EQ(live(s.store), before) << "the picture lives while the list names it";
    ASSERT_EQ(gshscene_list_truncate(l.list, 0), GSHSCENE_OK);
    EXPECT_EQ(live(s.store), before - 1) << "truncating the push released the mask";
    pic = fixture_picture(s.store, sq.get(), p2.get());
    g.soft_mask = pic;
    ASSERT_EQ(gshscene_list_group_push(l.list, &g, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_group_pop(l.list), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
    // A picture of a list whose group names a mask holds the mask, and counts it in its nesting.
    const GSHSCENE_Resource * outer = nullptr;
    ASSERT_EQ(gshscene_store_intern_picture(s.store, l.list, &outer), GSHSCENE_OK);
    EXPECT_EQ(gshscene_resource_picture_depth(outer), gshscene_resource_picture_depth(pic) + 1);
    gshscene_resource_release(outer);
    gshscene_resource_release(pic);
  }
  EXPECT_EQ(live(s.store), before - 1) << "destroying the list released the mask";
}

TEST(GroupFields, EqualityHashAndDumpSeeEachField) {
  StoreHolder s;
  Ref sq(intern_square(s.store));
  Ref p2(intern_fixture(s.store));
  Ref pic(fixture_picture(s.store, sq.get(), p2.get()));
  Ref pic2(intern_square(s.store, 1.0)); // not a picture: only used to make a second picture below
  GSHSCENE_List * mask_list = nullptr;
  ASSERT_EQ(gshscene_list_create(nullptr, nullptr, s.store, &mask_list), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(mask_list, pic2.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(mask_list), GSHSCENE_OK);
  const GSHSCENE_Resource * other_pic = nullptr;
  ASSERT_EQ(gshscene_store_intern_picture(s.store, mask_list, &other_pic), GSHSCENE_OK);
  gshscene_list_destroy(mask_list);
  Ref other(other_pic);
  const GSHSCENE_Group variants[] = {
      pdf_group(1.0, 0, 0, nullptr),
      pdf_group(1.0, 1, 0, nullptr),
      pdf_group(1.0, 0, 1, nullptr),
      pdf_group(1.0, 0, 0, pic.get()),
      pdf_group(1.0, 0, 0, other.get()),
      pdf_group(1.0, 1, 1, pic.get()),
  };
  std::vector<uint64_t> hashes;
  std::vector<std::string> dumps;
  for (const GSHSCENE_Group & g : variants) {
    ListHolder a(s.store), b(s.store);
    ASSERT_EQ(gshscene_list_group_push(a.list, &g, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_group_push(b.list, &g, 0, 0.0), GSHSCENE_OK);
    EXPECT_TRUE(gshscene_list_equal(a.list, b.list));
    hashes.push_back(gshscene_list_hash(a.list));
    dumps.push_back(dump_list(a.list));
  }
  for (size_t i = 0; i < hashes.size(); i++) {
    for (size_t j = i + 1; j < hashes.size(); j++) {
      EXPECT_NE(hashes[i], hashes[j]) << i << " " << j;
      EXPECT_NE(dumps[i], dumps[j]) << i << " " << j;
    }
  }
  // Lists that differ in one field only are not equal.
  for (size_t i = 1; i < sizeof variants / sizeof variants[0]; i++) {
    ListHolder a(s.store), b(s.store);
    ASSERT_EQ(gshscene_list_group_push(a.list, &variants[0], 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_group_push(b.list, &variants[i], 0, 0.0), GSHSCENE_OK);
    EXPECT_FALSE(gshscene_list_equal(a.list, b.list)) << i;
  }
  // The default group's line has no field of T1: the spelling is the one before the fields.
  EXPECT_EQ(dumps[0].find("isolation="), std::string::npos);
  EXPECT_NE(dumps[1].find(" isolation=1 knockout=0 mask=0000000000000000 layer="), std::string::npos) << dumps[1];
}

TEST(GroupFields, AListAndAVersionHashAndDumpAsTheGeneratorSays) {
  StoreHolder s;
  Ref sq(intern_square(s.store));
  Ref p2(intern_fixture(s.store));
  Ref pic(fixture_picture(s.store, sq.get(), p2.get()));
  ASSERT_EQ(gshscene_resource_hash(pic.get()), known::kPictureHash);
  ListHolder l(s.store);
  GSHGEOM_Map t = translate(1.5, -2.25);
  GSHSCENE_Paint a = solid(0.25, 0.5, 0.75, 1.0), b = solid(1.0, 0.0, 0.5, 0.75);
  GSHSCENE_Group non_isolated = pdf_group(1.0, GSHSCENE_ISOLATION_NON_ISOLATED, 0, nullptr);
  GSHSCENE_Group knockout = pdf_group(0.5, 0, 1, nullptr, GSHSCENE_COMPOSITE_MULTIPLY);
  GSHSCENE_Group masked = pdf_group(1.0, 0, 0, pic.get());
  masked.blend_space = linear_srgb_space();
  GSHSCENE_Group all = pdf_group(0.75, GSHSCENE_ISOLATION_NON_ISOLATED, 1, pic.get());
  GSHSCENE_Group plain = make_group(0.25);
  ASSERT_EQ(gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &a, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_push(l.list, &non_isolated, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(l.list, p2.get(), &t, GSHGEOM_FILL_EVEN_ODD, &b, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_push(l.list, &knockout, 2, 1.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_push(l.list, &masked, 0, 0.0), GSHSCENE_OK);
  for (int k = 0; k < 3; k++) {
    ASSERT_EQ(gshscene_list_group_pop(l.list), GSHSCENE_OK);
  }
  ASSERT_EQ(gshscene_list_group_push(l.list, &all, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_pop(l.list), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_push(l.list, &plain, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_pop(l.list), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_hash(l.list), known::kPdfGroupListHash);
  EXPECT_EQ(dump_list(l.list), known::kPdfGroupListDump);

  SceneHolder sc(nullptr, nullptr, s.store);
  EditHolder e(sc);
  GSHSCENE_Node g2 = group_node(0.5, 1);
  g2.depth = 2.0;
  g2.isolation = GSHSCENE_ISOLATION_NON_ISOLATED;
  g2.knockout = 1;
  g2.soft_mask = pic.get();
  uint64_t id2 = add(e.edit, GSHSCENE_ROOT_ID, g2);
  GSHSCENE_Node shape = shape_node(sq.get());
  shape.paint = solid(0.25, 0.5, 0.75, 1.0);
  add(e.edit, id2, shape);
  GSHSCENE_Node g4 = group_node();
  g4.knockout = 1;
  g4.composite = GSHSCENE_COMPOSITE_HUE;
  add(e.edit, GSHSCENE_ROOT_ID, g4);
  VersionRef v;
  publish(sc, e, &v);
  EXPECT_EQ(gshscene_version_hash(v.get()), known::kPdfGroupSceneHash);
  EXPECT_EQ(dump_version(v.get()), known::kPdfGroupSceneDump);
  GSHSCENE_Node_Info info = info_of(v.get(), id2);
  EXPECT_EQ(info.node.isolation, (uint32_t)GSHSCENE_ISOLATION_NON_ISOLATED);
  EXPECT_EQ(info.node.knockout, 1u);
  EXPECT_EQ(info.node.soft_mask, pic.get());
}

TEST(GroupFields, AVersionEmitsTheFieldsAsAGroupPushAndAChangeDamagesTheSubtree) {
  StoreHolder s;
  Ref sq(intern_square(s.store));
  Ref p2(intern_fixture(s.store));
  Ref pic(fixture_picture(s.store, sq.get(), p2.get()));
  SceneHolder sc(nullptr, nullptr, s.store);
  uint64_t gid = 0;
  VersionRef v1;
  {
    EditHolder e(sc);
    GSHSCENE_Node g = group_node(); // opacity 1, source-over, flat: no push but for a field of T1
    g.knockout = 1;
    gid = add(e.edit, GSHSCENE_ROOT_ID, g);
    GSHSCENE_Node shape = shape_node(sq.get());
    shape.map = translate(4.0, 4.0);
    add(e.edit, gid, shape);
    publish(sc, e, &v1);
  }
  const GSHSCENE_List * list = nullptr;
  ASSERT_EQ(gshscene_version_list(v1.get(), &list), GSHSCENE_OK);
  const GSHSCENE_Item * push = nullptr;
  ASSERT_EQ(gshscene_list_item(list, 0, &push), GSHSCENE_OK);
  ASSERT_EQ(push->kind, GSHSCENE_ITEM_GROUP_PUSH) << "a knockout group is emitted as a group push";
  EXPECT_EQ(push->group.knockout, 1u);
  VersionRef v2;
  {
    EditHolder e(sc, v1.get());
    GSHSCENE_Node_Info info = info_of(v1.get(), gid);
    GSHSCENE_Node g = info.node;
    g.knockout = 0;
    g.soft_mask = pic.get();
    ASSERT_EQ(gshscene_edit_set(e.edit, gid, &g), GSHSCENE_OK);
    publish(sc, e, &v2);
  }
  GSHSCENE_Diff * d = nullptr;
  ASSERT_EQ(gshscene_diff(v1.get(), v2.get(), nullptr, nullptr, &d), GSHSCENE_OK);
  GSHSCENE_Damage damage;
  ASSERT_EQ(gshscene_diff_damage(d, &damage), GSHSCENE_OK);
  EXPECT_FALSE(damage.empty) << "a changed field of T1 damages the group's subtree";
  gshscene_diff_destroy(d);
  ASSERT_EQ(gshscene_version_list(v2.get(), &list), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_item(list, 0, &push), GSHSCENE_OK);
  EXPECT_EQ(push->group.soft_mask, pic.get());
  // A shape or instance node carries none of them.
  EditHolder e(sc, v2.get());
  GSHSCENE_Node shape = shape_node(sq.get());
  shape.knockout = 1;
  shape.soft_mask = pic.get();
  uint64_t sid = add(e.edit, GSHSCENE_ROOT_ID, shape);
  VersionRef v3;
  publish(sc, e, &v3);
  GSHSCENE_Node_Info si = info_of(v3.get(), sid);
  EXPECT_EQ(si.node.knockout, 0u);
  EXPECT_EQ(si.node.soft_mask, nullptr);
}

TEST(GroupFields, AGroupNodeHoldsItsSoftMaskAndReleasesItWithTheScene) {
  // T1: a node holds the picture it names as its mask, as a list does: the caller's reference can go at once, and the
  // last version and edit to go take the node's with them.
  StoreHolder s;
  Ref sq(intern_square(s.store));
  Ref p2(intern_fixture(s.store));
  const GSHSCENE_Resource * pic = fixture_picture(s.store, sq.get(), p2.get());
  uint64_t with_mask = live(s.store);
  {
    SceneHolder sc(nullptr, nullptr, s.store);
    uint64_t gid = 0;
    {
      EditHolder e(sc);
      GSHSCENE_Node g = group_node();
      g.soft_mask = pic;
      gid = add(e.edit, GSHSCENE_ROOT_ID, g);
      gshscene_resource_release(pic); /* the edit's node holds its own */
      EXPECT_EQ(live(s.store), with_mask) << "the edit's node holds the mask";
      VersionRef v;
      publish(sc, e, &v);
      GSHSCENE_Node_Info info = info_of(v.get(), gid);
      ASSERT_EQ(info.node.soft_mask, pic);
      EXPECT_EQ(gshscene_resource_hash(info.node.soft_mask), known::kPictureHash) << "the mask is alive, and the same";
    }
    EXPECT_EQ(live(s.store), with_mask) << "the scene's version holds the mask";
  }
  EXPECT_EQ(live(s.store), with_mask - 1) << "and with the scene gone nothing does";
}

TEST(GroupFields, ASoftMaskCountsTowardThePictureNestingAtPublish) {
  // T1, T7: a group whose mask is a picture of depth d makes the list of any picture holding it d + 1 deep, and publish
  // refuses (ERR_LIMIT) the version whose instanced group would be past max_picture_depth, as it does for instances.
  for (size_t limit : {2u, 1u}) {
    GSHSCENE_Limits l;
    gshscene_limits_default(&l);
    l.max_picture_depth = limit;
    StoreHolder s;
    Ref sq(intern_square(s.store));
    Ref p2(intern_fixture(s.store));
    Ref pic(fixture_picture(s.store, sq.get(), p2.get())); // depth 1
    ASSERT_EQ(gshscene_resource_picture_depth(pic.get()), 1u);
    SceneHolder sc(&l, nullptr, s.store);
    EditHolder e(sc);
    GSHSCENE_Node g = group_node();
    g.flags = GSHSCENE_NODE_HIDDEN;
    g.soft_mask = pic.get();
    uint64_t def = add(e.edit, GSHSCENE_ROOT_ID, g);
    add(e.edit, def, shape_node(sq.get()));
    add(e.edit, GSHSCENE_ROOT_ID, instance_node(def));
    // The instanced group becomes a picture holding a mask of depth 1: 2 deep.
    const GSHSCENE_Version * v = nullptr;
    GSHSCENE_Result r = gshscene_scene_publish(sc.scene, e.edit, nullptr, &v);
    EXPECT_EQ(r, limit == 2 ? GSHSCENE_OK : GSHSCENE_ERR_LIMIT) << limit;
    if (r == GSHSCENE_OK) {
      const GSHSCENE_List * list = nullptr;
      EXPECT_EQ(gshscene_version_list(v, &list), GSHSCENE_OK) << "a version publish accepted emits";
    }
    gshscene_version_release(v);
  }
}

TEST(GroupFields, ThePlanningPassRefusesEachWithoutItsCap) {
  StoreHolder s;
  Ref sq(intern_square(s.store));
  Ref p2(intern_fixture(s.store));
  Ref pic(fixture_picture(s.store, sq.get(), p2.get()));
  GSHSCENE_Sink_Caps caps{};
  caps.item_kinds = 0x7e;
  caps.composites = 0x0fffffff;
  caps.flags = GSHSCENE_CAP_CAN_BLOCK;
  GSHSCENE_Item item{};
  item.kind = GSHSCENE_ITEM_GROUP_PUSH;
  item.group = make_group(1.0);
  auto decide = [&](const GSHSCENE_Group & g) {
    item.group = g;
    return gshscene_caps_disposition(&caps, &item, nullptr, 0, nullptr, nullptr, nullptr);
  };
  EXPECT_EQ(decide(pdf_group(1.0, 0, 0, nullptr)).entry_class, 0) << "the default group is drawn";
  EXPECT_EQ(decide(pdf_group(1.0, 1, 0, nullptr)).reason, GSHSCENE_REASON_GROUP_ISOLATION);
  EXPECT_EQ(decide(pdf_group(1.0, 0, 1, nullptr)).reason, GSHSCENE_REASON_GROUP_KNOCKOUT);
  EXPECT_EQ(decide(pdf_group(1.0, 0, 0, pic.get())).reason, GSHSCENE_REASON_SOFT_MASK);
  EXPECT_EQ(decide(pdf_group(1.0, 1, 1, pic.get())).reason, GSHSCENE_REASON_GROUP_ISOLATION) << "isolation first";
  EXPECT_EQ(decide(pdf_group(1.0, 1, 1, pic.get())).entry_class, GSHSCENE_ENTRY_REFUSED);
  caps.composites &= ~(1u << GSHSCENE_COMPOSITE_SRC_OVER);
  EXPECT_EQ(decide(pdf_group(1.0, 1, 1, pic.get())).reason, GSHSCENE_REASON_COMPOSITE) << "the operator first";
  caps.composites = 0x0fffffff;
  caps.flags |= GSHSCENE_CAP_GROUP_NON_ISOLATED;
  EXPECT_EQ(decide(pdf_group(1.0, 1, 1, pic.get())).reason, GSHSCENE_REASON_GROUP_KNOCKOUT);
  caps.flags |= GSHSCENE_CAP_GROUP_KNOCKOUT;
  EXPECT_EQ(decide(pdf_group(1.0, 1, 1, pic.get())).reason, GSHSCENE_REASON_SOFT_MASK);
  caps.flags |= GSHSCENE_CAP_GROUP_SOFT_MASK;
  EXPECT_EQ(decide(pdf_group(1.0, 1, 1, pic.get())).entry_class, 0) << "a sink that claims all three draws it";
  EXPECT_STREQ(gshscene_reason_string(GSHSCENE_REASON_GROUP_ISOLATION), "group-isolation");
  EXPECT_STREQ(gshscene_reason_string(GSHSCENE_REASON_GROUP_KNOCKOUT), "group-knockout");
  EXPECT_STREQ(gshscene_reason_string(GSHSCENE_REASON_SOFT_MASK), "soft-mask");
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
