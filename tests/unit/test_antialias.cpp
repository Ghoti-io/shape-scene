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
 * The antialiasing of an item (list.h, AA1 to AA6): the enumeration and its pinned values, the two record calls and the rules they
 * check in order, the default left unspelled and the mode spelled as the independent generator spells it (the list's hash and dump,
 * the retained scene's), equality, the node that carries it and the item it emits, the planning pass's disposition, and what the test
 * sink reports and records.
 */

#include "../known_answers.h"
#include "scene_fixtures.h"
#include "sink_conformance.h"

#include <string>

using namespace sinkconf;

namespace {

/** The default stroke style, interned and held. */
scene::Ref default_style(GSHSCENE_Store * store) {
  GSHSCENE_Stroke_Style st;
  gshscene_stroke_style_init(&st);
  const GSHSCENE_Resource * r = nullptr;
  EXPECT_EQ(gshscene_store_intern_stroke_style(store, &st, &r), GSHSCENE_OK);
  return scene::Ref(r);
}

const GSHSCENE_Item * item_at(const GSHSCENE_List * list, size_t i) {
  const GSHSCENE_Item * item = nullptr;
  EXPECT_EQ(gshscene_list_item(list, i, &item), GSHSCENE_OK);
  return item;
}

GSHSCENE_Sink_Caps caps_with(uint32_t flags) {
  GSHSCENE_Sink_Caps caps;
  std::memset(&caps, 0, sizeof caps);
  caps.item_kinds = 0xFFFFFFFFu;
  caps.composites = 0xFFFFFFFFu;
  caps.stroke_native = GSHSCENE_STROKE_NATIVE_ALL;
  caps.gradient_kinds = 0xe;
  caps.image_filters = 0xf;
  caps.image_extends = 0xf;
  caps.image_formats = GSHSCENE_IMAGE_FORMAT_ALL;
  caps.mask_native = GSHSCENE_MASK_NATIVE_ALL;
  caps.flags = flags;
  caps.cancel_unit = GSHSCENE_CANCEL_BETWEEN_UNITS;
  caps.work_measure = "units";
  return caps;
}

GSHSCENE_Disposition dispose(const GSHSCENE_Sink_Caps & caps, const GSHSCENE_Item * item) {
  GSHSCENE_Color_Space srgb = scene::srgb_space();
  return gshscene_caps_disposition(&caps, item, nullptr, 0, nullptr, &srgb, nullptr);
}

const uint32_t kBase = GSHSCENE_CAP_CLIP_AA | GSHSCENE_CAP_BLEND_SPACES | GSHSCENE_CAP_GRADIENT_SPACES |
    GSHSCENE_CAP_GLYPHS_FROM_FONT | GSHSCENE_CAP_STROKE_RUNS | GSHSCENE_CAP_IMAGE_FILTER_SPACES | GSHSCENE_CAP_STROKE_EXPAND;

} // namespace

TEST(Antialias, TheValuesAreEnumerationAndFlagsArePinned) {
  EXPECT_EQ((int)GSHSCENE_ANTIALIAS_AREA, 0);
  EXPECT_EQ((int)GSHSCENE_ANTIALIAS_NONE, 1);
  EXPECT_EQ((int)GSHSCENE_ANTIALIAS_NONE_TOP_LEFT, 2);
  EXPECT_EQ((int)GSHSCENE_ANTIALIAS_COUNT, 3);
  EXPECT_EQ(GSHSCENE_CAP_ANTIALIAS_NONE, UINT32_C(1) << 19);
  EXPECT_EQ(GSHSCENE_CAP_ANTIALIAS_NONE_TOP_LEFT, UINT32_C(1) << 20);
  EXPECT_EQ(GSHSCENE_CAP_FLAGS_ALL & GSHSCENE_CAP_ANTIALIAS_NONE, GSHSCENE_CAP_ANTIALIAS_NONE);
  EXPECT_EQ(GSHSCENE_CAP_FLAGS_ALL, 0x3fffffu) << "twenty-two flags";
  EXPECT_EQ((int)GSHSCENE_REASON_ANTIALIAS, 43);
  EXPECT_STREQ(gshscene_reason_string(GSHSCENE_REASON_ANTIALIAS), "antialias");
}

TEST(Antialias, AZeroedItemAndTheOldCallsAreAreaCoverage) {
  scene::StoreHolder s;
  scene::ListHolder l(s.store);
  scene::Ref sq(scene::intern_square(s.store));
  scene::Ref style = default_style(s.store);
  ASSERT_EQ(gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_stroke(l.list, sq.get(), style.get(), nullptr, nullptr, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_clip_push(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_clip_pop(l.list), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_push(l.list, nullptr, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_pop(l.list), GSHSCENE_OK);
  for (size_t i = 0; i < gshscene_list_count(l.list); i++) {
    EXPECT_EQ(item_at(l.list, i)->antialias, 0u) << i;
  }
  // The explicit area mode is the same item: equal, the same hash, the same dump.
  scene::ListHolder m(s.store);
  ASSERT_EQ(gshscene_list_fill_antialias(m.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, GSHSCENE_ANTIALIAS_AREA, 0, 0.0),
      GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_stroke_antialias(m.list, sq.get(), style.get(), nullptr, nullptr, GSHSCENE_ANTIALIAS_AREA, 0, 0.0),
      GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_clip_push(m.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_clip_pop(m.list), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_push(m.list, nullptr, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_pop(m.list), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_equal(l.list, m.list), 1);
  EXPECT_EQ(gshscene_list_hash(l.list), gshscene_list_hash(m.list));
  EXPECT_EQ(scene::dump_list(l.list), scene::dump_list(m.list));
  EXPECT_EQ(scene::dump_list(l.list).find("aa="), std::string::npos) << "the default is spelled by nothing";
}

TEST(Antialias, TheModeIsRecordedOnAFillAndAStrokeAndNowhereElse) {
  scene::StoreHolder s;
  scene::ListHolder l(s.store);
  scene::Ref sq(scene::intern_square(s.store));
  scene::Ref style = default_style(s.store);
  ASSERT_EQ(gshscene_list_fill_antialias(l.list, sq.get(), nullptr, GSHGEOM_FILL_EVEN_ODD, nullptr, GSHSCENE_ANTIALIAS_NONE, 3, 1.5),
      GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_stroke_antialias(l.list, sq.get(), style.get(), nullptr, nullptr, GSHSCENE_ANTIALIAS_NONE, 4, 2.5),
      GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_clip_push(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_clip_pop(l.list), GSHSCENE_OK);
  const GSHSCENE_Item * f = item_at(l.list, 0);
  EXPECT_EQ(f->kind, GSHSCENE_ITEM_FILL);
  EXPECT_EQ(f->antialias, (uint32_t)GSHSCENE_ANTIALIAS_NONE);
  EXPECT_EQ(f->fill_rule, GSHGEOM_FILL_EVEN_ODD) << "the rule is its own field";
  EXPECT_EQ(f->layer, 3);
  const GSHSCENE_Item * k = item_at(l.list, 1);
  EXPECT_EQ(k->kind, GSHSCENE_ITEM_STROKE);
  EXPECT_EQ(k->antialias, (uint32_t)GSHSCENE_ANTIALIAS_NONE);
  EXPECT_EQ(k->depth, 2.5);
  EXPECT_EQ(item_at(l.list, 2)->antialias, 0u) << "a clip push has none";
}

TEST(Antialias, AModeOutsideTheEnumerationIsInvalidAndLeavesTheListAsItWas) {
  scene::StoreHolder s;
  scene::ListHolder l(s.store);
  scene::Ref sq(scene::intern_square(s.store));
  scene::Ref style = default_style(s.store);
  const GSHSCENE_Antialias bad = (GSHSCENE_Antialias)GSHSCENE_ANTIALIAS_COUNT;
  EXPECT_EQ(gshscene_list_fill_antialias(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, bad, 0, 0.0), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_fill_antialias(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, (GSHSCENE_Antialias)0xffffffffu, 0,
                0.0),
      GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_stroke_antialias(l.list, sq.get(), style.get(), nullptr, nullptr, bad, 0, 0.0), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_count(l.list), 0u);
  // Decided with the validity of the arguments and before N1's limit (a solid component past 2^16).
  GSHSCENE_Paint huge = solid(1e9, 0, 0);
  EXPECT_EQ(gshscene_list_fill_antialias(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &huge, bad, 0, 0.0), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_fill_antialias(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &huge, GSHSCENE_ANTIALIAS_NONE, 0, 0.0),
      GSHSCENE_ERR_LIMIT);
  // A closed list and a NULL list are invalid whatever the mode.
  EXPECT_EQ(gshscene_list_fill_antialias(nullptr, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, GSHSCENE_ANTIALIAS_NONE, 0, 0.0),
      GSHSCENE_ERR_INVALID);
  ASSERT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_fill_antialias(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, GSHSCENE_ANTIALIAS_NONE, 0, 0.0),
      GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_stroke_antialias(l.list, sq.get(), style.get(), nullptr, nullptr, GSHSCENE_ANTIALIAS_NONE, 0, 0.0),
      GSHSCENE_ERR_INVALID);
}

TEST(Antialias, AFillOfThePlaneRecordsTheModeItCannotUse) {
  scene::StoreHolder s;
  scene::ListHolder l(s.store);
  const GSHSCENE_Resource * plane = nullptr;
  ASSERT_EQ(gshscene_store_intern_plane(s.store, &plane), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_fill_antialias(l.list, plane, nullptr, GSHGEOM_FILL_NONZERO, nullptr, GSHSCENE_ANTIALIAS_NONE, 0, 0.0),
      GSHSCENE_OK);
  EXPECT_EQ(item_at(l.list, 0)->antialias, (uint32_t)GSHSCENE_ANTIALIAS_NONE);
  gshscene_resource_release(plane);
}

TEST(Antialias, TheListSpellsTheModeAsTheGeneratorDoesAndTheDefaultAsItAlwaysWas) {
  scene::StoreHolder s;
  scene::ListHolder l(s.store);
  scene::Ref sq(scene::intern_square(s.store));
  scene::Ref style = default_style(s.store);
  GSHSCENE_Paint a = solid(0.25, 0.5, 0.75, 1.0);
  GSHSCENE_Paint fg{};
  fg.kind = GSHSCENE_PAINT_FOREGROUND;
  fg.rgba[3] = 0.5;
  GSHSCENE_Paint half = solid(0, 0, 0, 0.5);
  ASSERT_EQ(gshscene_list_fill_antialias(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &a, GSHSCENE_ANTIALIAS_NONE, 0, 0.0),
      GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_stroke_antialias(l.list, sq.get(), style.get(), nullptr, nullptr, GSHSCENE_ANTIALIAS_NONE, 0, 0.0),
      GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_EVEN_ODD, &fg, 2, 1.5), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_stroke_antialias(l.list, sq.get(), style.get(), nullptr, &half, GSHSCENE_ANTIALIAS_NONE, 2, 1.5), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_hash(l.list), known::kAntialiasListHash);
  EXPECT_EQ(scene::dump_list(l.list), std::string(known::kAntialiasListDump));
  ASSERT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_hash(l.list), known::kAntialiasListHash) << "flat: closing reorders nothing";
}

TEST(Antialias, TheModeIsPartOfEqualityAndOfTheHashAndTheDefaultIsNot) {
  scene::StoreHolder s;
  scene::Ref sq(scene::intern_square(s.store));
  scene::Ref style = default_style(s.store);
  auto build = [&](GSHSCENE_Antialias fill_mode, GSHSCENE_Antialias stroke_mode, scene::ListHolder & l) {
    ASSERT_EQ(gshscene_list_fill_antialias(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, fill_mode, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_stroke_antialias(l.list, sq.get(), style.get(), nullptr, nullptr, stroke_mode, 0, 0.0), GSHSCENE_OK);
  };
  scene::ListHolder aa(s.store), an(s.store), na(s.store), nn(s.store), nn2(s.store);
  build(GSHSCENE_ANTIALIAS_AREA, GSHSCENE_ANTIALIAS_AREA, aa);
  build(GSHSCENE_ANTIALIAS_AREA, GSHSCENE_ANTIALIAS_NONE, an);
  build(GSHSCENE_ANTIALIAS_NONE, GSHSCENE_ANTIALIAS_AREA, na);
  build(GSHSCENE_ANTIALIAS_NONE, GSHSCENE_ANTIALIAS_NONE, nn);
  build(GSHSCENE_ANTIALIAS_NONE, GSHSCENE_ANTIALIAS_NONE, nn2);
  const GSHSCENE_List * all[4] = {aa.list, an.list, na.list, nn.list};
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      EXPECT_EQ(gshscene_list_equal(all[i], all[j]), i == j ? 1 : 0) << i << " " << j;
      EXPECT_EQ(gshscene_list_hash(all[i]) == gshscene_list_hash(all[j]), i == j) << i << " " << j;
    }
  }
  EXPECT_EQ(gshscene_list_equal(nn.list, nn2.list), 1);
  EXPECT_EQ(gshscene_list_hash(nn.list), gshscene_list_hash(nn2.list));
}

TEST(Antialias, APictureKeepsTheModeOfItsItemsAndIsAnotherPictureForIt) {
  scene::StoreHolder s;
  scene::Ref sq(scene::intern_square(s.store));
  scene::Ref style = default_style(s.store);
  auto make = [&](GSHSCENE_Antialias mode, scene::Ref * out) {
    scene::ListHolder l(s.store);
    ASSERT_EQ(gshscene_list_fill_antialias(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, mode, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_stroke_antialias(l.list, sq.get(), style.get(), nullptr, nullptr, mode, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
    const GSHSCENE_Resource * r = nullptr;
    ASSERT_EQ(gshscene_store_intern_picture(s.store, l.list, &r), GSHSCENE_OK);
    *out = scene::Ref(r);
  };
  scene::Ref crisp, smooth;
  make(GSHSCENE_ANTIALIAS_NONE, &crisp);
  make(GSHSCENE_ANTIALIAS_AREA, &smooth);
  EXPECT_NE(crisp.get(), smooth.get()) << "two pictures";
  EXPECT_NE(gshscene_resource_hash(crisp.get()), gshscene_resource_hash(smooth.get()));
  const GSHSCENE_List * inner = gshscene_resource_picture(crisp.get());
  ASSERT_NE(inner, nullptr);
  ASSERT_EQ(gshscene_list_count(inner), 2u);
  EXPECT_EQ(item_at(inner, 0)->antialias, (uint32_t)GSHSCENE_ANTIALIAS_NONE);
  EXPECT_EQ(item_at(inner, 1)->antialias, (uint32_t)GSHSCENE_ANTIALIAS_NONE);
  EXPECT_EQ(item_at(gshscene_resource_picture(smooth.get()), 0)->antialias, 0u);
}

TEST(Antialias, ASceneNodeCarriesItAndEmitsItOnTheItem) {
  scene::SceneHolder h;
  scene::Ref sq(scene::intern_square(h.store()));
  scene::Ref style = default_style(h.store());
  scene::EditHolder e(h);
  const uint64_t root = 1;
  GSHSCENE_Node shape = scene::shape_node(sq.get());
  shape.antialias = GSHSCENE_ANTIALIAS_NONE;
  shape.paint = solid(0.25, 0.5, 0.75, 1.0);
  GSHSCENE_Node stroke = scene::stroke_node(sq.get(), style.get());
  stroke.antialias = GSHSCENE_ANTIALIAS_NONE;
  const uint64_t a = scene::add(e.edit, root, shape);
  const uint64_t b = scene::add(e.edit, root, stroke);
  const uint64_t c = scene::add(e.edit, root, scene::shape_node(sq.get()));
  ASSERT_NE(a, 0u);
  ASSERT_NE(b, 0u);
  ASSERT_NE(c, 0u);
  scene::VersionRef v;
  scene::publish(h, e, &v);
  ASSERT_NE(v.version, nullptr);
  EXPECT_EQ(gshscene_version_hash(v.version), known::kAntialiasSceneHash);
  EXPECT_EQ(scene::dump_version(v.version), std::string(known::kAntialiasSceneDump));
  EXPECT_EQ(scene::info_of(v.version, a).node.antialias, (uint32_t)GSHSCENE_ANTIALIAS_NONE);
  EXPECT_EQ(scene::info_of(v.version, c).node.antialias, 0u);
  EXPECT_EQ(scene::info_of(v.version, root).node.antialias, 0u) << "a group has none";
  const GSHSCENE_List * list = nullptr;
  ASSERT_EQ(gshscene_version_list(v.version, &list), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_hash(list), known::kAntialiasSceneListHash);
  ASSERT_EQ(gshscene_list_count(list), 3u);
  EXPECT_EQ(item_at(list, 0)->antialias, (uint32_t)GSHSCENE_ANTIALIAS_NONE);
  EXPECT_EQ(item_at(list, 1)->antialias, (uint32_t)GSHSCENE_ANTIALIAS_NONE);
  EXPECT_EQ(item_at(list, 2)->antialias, 0u);
}

TEST(Antialias, ANodeWithAModeOutsideTheEnumerationIsInvalidAndAGroupDropsTheField) {
  scene::SceneHolder h;
  scene::Ref sq(scene::intern_square(h.store()));
  scene::Ref style = default_style(h.store());
  scene::EditHolder e(h);
  GSHSCENE_Node shape = scene::shape_node(sq.get());
  shape.antialias = GSHSCENE_ANTIALIAS_COUNT;
  uint64_t id = 0;
  EXPECT_EQ(gshscene_edit_add(e.edit, 1, GSHSCENE_APPEND, &shape, nullptr, &id), GSHSCENE_ERR_INVALID);
  GSHSCENE_Node stroke = scene::stroke_node(sq.get(), style.get());
  stroke.antialias = 7;
  EXPECT_EQ(gshscene_edit_add(e.edit, 1, GSHSCENE_APPEND, &stroke, nullptr, &id), GSHSCENE_ERR_INVALID);
  GSHSCENE_Node group = scene::group_node();
  group.antialias = GSHSCENE_ANTIALIAS_NONE;
  ASSERT_EQ(gshscene_edit_add(e.edit, 1, GSHSCENE_APPEND, &group, nullptr, &id), GSHSCENE_OK);
  scene::VersionRef v;
  scene::publish(h, e, &v);
  EXPECT_EQ(scene::info_of(v.version, id).node.antialias, 0u) << "one spelling: a field a kind does not use is zero";
}

TEST(Antialias, EditingTheModeIsAChangeTheDiffSees) {
  scene::SceneHolder h;
  scene::Ref sq(scene::intern_square(h.store()));
  uint64_t id = 0;
  scene::VersionRef v1;
  {
    scene::EditHolder e(h);
    id = scene::add(e.edit, 1, scene::shape_node(sq.get()));
    scene::publish(h, e, &v1);
  }
  scene::VersionRef v2;
  {
    scene::EditHolder e(h, v1.version);
    GSHSCENE_Node n = scene::shape_node(sq.get());
    n.antialias = GSHSCENE_ANTIALIAS_NONE;
    ASSERT_EQ(gshscene_edit_set(e.edit, id, &n), GSHSCENE_OK);
    scene::publish(h, e, &v2);
  }
  EXPECT_NE(gshscene_version_hash(v1.version), gshscene_version_hash(v2.version));
  const GSHSCENE_List * l1 = nullptr;
  const GSHSCENE_List * l2 = nullptr;
  ASSERT_EQ(gshscene_version_list(v1.version, &l1), GSHSCENE_OK);
  ASSERT_EQ(gshscene_version_list(v2.version, &l2), GSHSCENE_OK);
  EXPECT_NE(gshscene_list_hash(l1), gshscene_list_hash(l2));
  EXPECT_EQ(scene::info_of(v2.version, id).node.antialias, (uint32_t)GSHSCENE_ANTIALIAS_NONE);
}

TEST(Antialias, ASinkWithoutTheCapabilityApproximatesAFillOrAStrokeThatAsksForNone) {
  scene::StoreHolder s;
  scene::ListHolder l(s.store);
  scene::Ref sq(scene::intern_square(s.store));
  scene::Ref style = default_style(s.store);
  ASSERT_EQ(gshscene_list_fill_antialias(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, GSHSCENE_ANTIALIAS_NONE, 0, 0.0),
      GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_stroke_antialias(l.list, sq.get(), style.get(), nullptr, nullptr, GSHSCENE_ANTIALIAS_NONE, 0, 0.0),
      GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_stroke(l.list, sq.get(), style.get(), nullptr, nullptr, 0, 0.0), GSHSCENE_OK);
  const GSHSCENE_Sink_Caps without = caps_with(kBase);
  const GSHSCENE_Sink_Caps with = caps_with(kBase | GSHSCENE_CAP_ANTIALIAS_NONE);
  for (size_t i = 0; i < 2; i++) {
    GSHSCENE_Disposition d = dispose(without, item_at(l.list, i));
    EXPECT_EQ(d.entry_class, GSHSCENE_ENTRY_APPROXIMATED) << i;
    EXPECT_EQ(d.reason, GSHSCENE_REASON_ANTIALIAS) << i;
    EXPECT_EQ((int)dispose(with, item_at(l.list, i)).entry_class, 0) << i << ": the capability draws it as asked";
  }
  for (size_t i = 2; i < 4; i++) {
    EXPECT_EQ((int)dispose(without, item_at(l.list, i)).entry_class, 0) << i << ": the default is drawn by every sink";
    EXPECT_EQ((int)dispose(with, item_at(l.list, i)).entry_class, 0) << i;
  }
}

// AA3, AA5: the two modes differ only in the tie, are two modes for the list (equality, hash, dump) and are two capabilities, each of
// which serves its own mode only.
TEST(Antialias, TheTwoTiesAreTwoModesAndTwoCapabilities) {
  scene::StoreHolder s;
  scene::ListHolder l(s.store);
  scene::Ref sq(scene::intern_square(s.store));
  ASSERT_EQ(gshscene_list_fill_antialias(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, GSHSCENE_ANTIALIAS_NONE, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill_antialias(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, GSHSCENE_ANTIALIAS_NONE_TOP_LEFT, 0, 0.0),
      GSHSCENE_OK);
  const GSHSCENE_Sink_Caps none = caps_with(kBase);
  const GSHSCENE_Sink_Caps bottom_right = caps_with(kBase | GSHSCENE_CAP_ANTIALIAS_NONE);
  const GSHSCENE_Sink_Caps top_left = caps_with(kBase | GSHSCENE_CAP_ANTIALIAS_NONE_TOP_LEFT);
  const GSHSCENE_Sink_Caps both = caps_with(kBase | GSHSCENE_CAP_ANTIALIAS_NONE | GSHSCENE_CAP_ANTIALIAS_NONE_TOP_LEFT);
  auto approximated = [&](const GSHSCENE_Sink_Caps & caps, size_t i) {
    const GSHSCENE_Disposition d = dispose(caps, item_at(l.list, i));
    return d.entry_class == GSHSCENE_ENTRY_APPROXIMATED && d.reason == GSHSCENE_REASON_ANTIALIAS;
  };
  EXPECT_TRUE(approximated(none, 0));
  EXPECT_TRUE(approximated(none, 1));
  EXPECT_FALSE(approximated(bottom_right, 0));
  EXPECT_TRUE(approximated(bottom_right, 1)) << "the bottom-right capability does not draw the top-left mode";
  EXPECT_TRUE(approximated(top_left, 0)) << "the top-left capability does not draw the bottom-right mode";
  EXPECT_FALSE(approximated(top_left, 1));
  EXPECT_FALSE(approximated(both, 0));
  EXPECT_FALSE(approximated(both, 1));
  // two modes for the list: not equal, not the same hash, and the dump spells each
  scene::ListHolder a(s.store), b(s.store);
  ASSERT_EQ(gshscene_list_fill_antialias(a.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, GSHSCENE_ANTIALIAS_NONE, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill_antialias(b.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, GSHSCENE_ANTIALIAS_NONE_TOP_LEFT, 0, 0.0),
      GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_equal(a.list, b.list), 0);
  EXPECT_NE(gshscene_list_hash(a.list), gshscene_list_hash(b.list));
  EXPECT_NE(scene::dump_list(a.list).find(" aa=1"), std::string::npos);
  EXPECT_NE(scene::dump_list(b.list).find(" aa=2"), std::string::npos);
  // the enumeration ends after the second
  EXPECT_EQ(gshscene_list_fill_antialias(a.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, (GSHSCENE_Antialias)GSHSCENE_ANTIALIAS_COUNT, 0, 0.0),
      GSHSCENE_ERR_INVALID);
}

TEST(Antialias, TheApproximationFollowsEveryRefusalAndPrecedesTheOtherApproximations) {
  scene::StoreHolder s;
  scene::ListHolder l(s.store);
  scene::Ref sq(scene::intern_square(s.store));
  GSHSCENE_Paint palette = solid(0, 0, 0);
  palette.kind = GSHSCENE_PAINT_PALETTE;
  palette.entry = 5;
  ASSERT_EQ(gshscene_list_fill_antialias(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &palette, GSHSCENE_ANTIALIAS_NONE, 0, 0.0),
      GSHSCENE_OK);
  // A palette entry the list does not have is a refusal, and it stays the first thing said.
  GSHSCENE_Disposition refused = dispose(caps_with(kBase), item_at(l.list, 0));
  EXPECT_EQ(refused.entry_class, GSHSCENE_ENTRY_REFUSED);
  EXPECT_EQ(refused.reason, GSHSCENE_REASON_PALETTE_ENTRY);
  // A sink that lacks the kind refuses it before anything is approximated.
  GSHSCENE_Sink_Caps no_fills = caps_with(kBase);
  no_fills.item_kinds = 0;
  EXPECT_EQ(dispose(no_fills, item_at(l.list, 0)).reason, GSHSCENE_REASON_ITEM_KIND);
}

TEST(Antialias, TheTestSinkRecordsTheModeReportsItWithoutTheCapabilityAndStrictDoesNotFailIt) {
  Scene s;
  GSHSCENE_Paint red = solid(1, 0, 0);
  GSHSCENE_List * l = s.open();
  ASSERT_EQ(gshscene_list_fill_antialias(l, s.sq(4, 4, 8), nullptr, GSHGEOM_FILL_NONZERO, &red, GSHSCENE_ANTIALIAS_NONE, 0, 0.0),
      GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(l, s.sq(20, 4, 8), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0), GSHSCENE_OK);
  GSHSCENE_Stroke_Style st;
  gshscene_stroke_style_init(&st);
  const GSHSCENE_Resource * style = nullptr;
  ASSERT_EQ(gshscene_store_intern_stroke_style(s.store, &st, &style), GSHSCENE_OK);
  s.held.push_back(style);
  ASSERT_EQ(gshscene_list_stroke_antialias(l, s.sq(4, 20, 8), style, nullptr, &red, GSHSCENE_ANTIALIAS_NONE, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_stroke(l, s.sq(20, 20, 8), style, nullptr, &red, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  std::vector<size_t> first_chain;
  auto run = [&](uint32_t drop, uint32_t flags, std::string * stream_text, std::vector<GSHSCENE_Report_Entry> * out,
                 GSHSCENE_Result expect) {
    GSHSCENE_Dump_Sink_Config c;
    gshscene_dump_sink_config_default(&c);
    c.stream_caps.flags &= ~drop;
    GSHSCENE_Sink * sink = nullptr;
    ASSERT_EQ(gshscene_dump_sink_create(nullptr, nullptr, &c, &sink), GSHSCENE_OK);
    auto stream = make_stream();
    ReportRef report;
    GSHSCENE_Render_Request req = request_for(l, *stream, report.report);
    req.flags = flags;
    EXPECT_EQ(gshscene_sink_render(sink, &req), expect);
    *out = entries(report.report);
    first_chain.assign(out->empty() ? nullptr : (*out)[0].chain, out->empty() ? nullptr : (*out)[0].chain + (*out)[0].chain_count);
    *stream_text = stream->bytes;
    EXPECT_EQ(gshscene_sink_destroy(sink), GSHSCENE_OK);
  };
  std::string honored_text, approximated_text;
  std::vector<GSHSCENE_Report_Entry> honored, approximated, strict;
  run(0, 0, &honored_text, &honored, GSHSCENE_OK);
  EXPECT_TRUE(honored.empty()) << "a sink with the capability says nothing";
  size_t marks = 0;
  for (size_t at = honored_text.find(" aa=1"); at != std::string::npos; at = honored_text.find(" aa=1", at + 1)) {
    marks++;
    const size_t line = honored_text.rfind('\n', at);
    const std::string head = honored_text.substr(line == std::string::npos ? 0 : line + 1, at - (line == std::string::npos ? 0 : line + 1));
    EXPECT_TRUE(head.find(" F res=") != std::string::npos || head.find(" K res=") != std::string::npos)
        << "the mode follows the head of a fill's or a stroke's record: " << head;
  }
  EXPECT_EQ(marks, 2u) << "and records the mode of the fill and of the stroke that ask, and of no other";
  EXPECT_NE(honored_text.find(" K res="), std::string::npos);
  run(GSHSCENE_CAP_ANTIALIAS_NONE, 0, &approximated_text, &approximated, GSHSCENE_OK);
  ASSERT_EQ(approximated.size(), 2u) << "the fill and the stroke that ask, and neither of the defaults";
  EXPECT_EQ(approximated[0].entry_class, GSHSCENE_ENTRY_APPROXIMATED);
  EXPECT_EQ(approximated[0].reason, GSHSCENE_REASON_ANTIALIAS);
  EXPECT_EQ(approximated[1].reason, GSHSCENE_REASON_ANTIALIAS);
  ASSERT_EQ(first_chain.size(), 1u);
  EXPECT_EQ(first_chain[0], 0u) << "the first item that asked";
  EXPECT_EQ(approximated_text.find("X class=2 reason=43"), std::string::npos) << "an approximation is not an X record";
  run(GSHSCENE_CAP_ANTIALIAS_NONE, GSHSCENE_RENDER_STRICT, &approximated_text, &strict, GSHSCENE_OK);
  EXPECT_EQ(strict.size(), 2u) << "strict fails a refusal, never an approximation";
}

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(SinkConformance);

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
