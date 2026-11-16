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
 * A group's filter mode (D-FL13, FL22): `OWN` or `BACKDROP`.  Validated with the filter's own rules, held, compared, hashed and dumped
 * (the default spelled as before the field), carried through a retained scene's group node and its emission, and through the copy of
 * a picture into another store.
 */

#include "scene_fixtures.h"

#include <string>
#include <vector>

using namespace scene;

namespace {

/** A one-primitive filter, a blur by @p s, interned in @p store. */
Ref blur_filter(GSHSCENE_Store * store, double s) {
  static const GSHSCENE_Filter_Input in = {GSHSCENE_FILTER_INPUT_PREVIOUS, 0};
  GSHSCENE_Filter_Primitive p;
  std::memset(&p, 0, sizeof p);
  p.kind = GSHSCENE_FILTER_GAUSSIAN_BLUR;
  p.input_count = 1;
  p.inputs = &in;
  p.sx = s;
  p.sy = s;
  GSHSCENE_Filter f;
  std::memset(&f, 0, sizeof f);
  f.region_mode = GSHSCENE_FILTER_REGION_AUTO;
  f.color_space = GSHSCENE_FILTER_SPACE_LINEAR_RGB;
  f.primitive_count = 1;
  f.primitives = &p;
  const GSHSCENE_Resource * r = nullptr;
  EXPECT_EQ(gshscene_store_intern_filter(store, &f, &r), GSHSCENE_OK);
  return Ref(r);
}

GSHSCENE_Group filtered(const GSHSCENE_Resource * filter, uint32_t mode) {
  GSHSCENE_Group g = make_group(1.0);
  g.filter = filter;
  g.filter_mode = mode;
  return g;
}

}  // namespace

TEST(FilterBackdrop, TheModeIsValidatedWithTheFilter) {
  StoreHolder s;
  Ref f = blur_filter(s.store, 2.0);
  ListHolder l(s.store);
  GSHSCENE_Group ok = filtered(f.get(), GSHSCENE_FILTER_MODE_BACKDROP);
  ASSERT_EQ(gshscene_list_group_push(l.list, &ok, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_pop(l.list), GSHSCENE_OK);
  const GSHSCENE_Item * item = nullptr;
  ASSERT_EQ(gshscene_list_item(l.list, 0, &item), GSHSCENE_OK);
  EXPECT_EQ(item->group.filter_mode, (uint32_t)GSHSCENE_FILTER_MODE_BACKDROP);
  EXPECT_EQ(item->group.filter, f.get());
  // refused: a mode past the enum, a backdrop with no filter, and the group the filter itself refuses
  GSHSCENE_Group bad = filtered(f.get(), GSHSCENE_FILTER_MODE_COUNT);
  EXPECT_EQ(gshscene_list_group_push(l.list, &bad, 0, 0.0), GSHSCENE_ERR_INVALID);
  bad = filtered(nullptr, GSHSCENE_FILTER_MODE_BACKDROP);
  EXPECT_EQ(gshscene_list_group_push(l.list, &bad, 0, 0.0), GSHSCENE_ERR_INVALID);
  bad = filtered(f.get(), GSHSCENE_FILTER_MODE_BACKDROP);
  bad.isolation = GSHSCENE_ISOLATION_NON_ISOLATED;
  EXPECT_EQ(gshscene_list_group_push(l.list, &bad, 0, 0.0), GSHSCENE_ERR_INVALID);
  bad = filtered(f.get(), GSHSCENE_FILTER_MODE_BACKDROP);
  bad.knockout = 1;
  EXPECT_EQ(gshscene_list_group_push(l.list, &bad, 0, 0.0), GSHSCENE_ERR_INVALID);
  // the failed pushes left no group open
  EXPECT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
}

TEST(FilterBackdrop, TheModeIsPartOfTheHashTheEqualityAndTheDump) {
  StoreHolder s;
  Ref f = blur_filter(s.store, 2.0);
  auto build = [&](uint32_t mode, std::string * dump) {
    ListHolder l(s.store);
    GSHSCENE_Group g = filtered(f.get(), mode);
    EXPECT_EQ(gshscene_list_group_push(l.list, &g, 0, 0.0), GSHSCENE_OK);
    EXPECT_EQ(gshscene_list_group_pop(l.list), GSHSCENE_OK);
    EXPECT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
    *dump = dump_list(l.list);
    return gshscene_list_hash(l.list);
  };
  std::string own, back, back_again;
  const uint64_t h_own = build(GSHSCENE_FILTER_MODE_OWN, &own);
  const uint64_t h_back = build(GSHSCENE_FILTER_MODE_BACKDROP, &back);
  const uint64_t h_again = build(GSHSCENE_FILTER_MODE_BACKDROP, &back_again);
  EXPECT_NE(h_own, h_back) << "the two groups draw differently";
  EXPECT_EQ(h_back, h_again);
  EXPECT_EQ(own.find("fmode="), std::string::npos) << "an own-content filter is spelled as it was: " << own;
  EXPECT_NE(own.find(" filter="), std::string::npos);
  EXPECT_NE(back.find(" fmode=1"), std::string::npos) << back;
  EXPECT_EQ(back, back_again);
  // equality reads the mode
  ListHolder a(s.store), b(s.store);
  GSHSCENE_Group ga = filtered(f.get(), GSHSCENE_FILTER_MODE_OWN), gb = filtered(f.get(), GSHSCENE_FILTER_MODE_BACKDROP);
  ASSERT_EQ(gshscene_list_group_push(a.list, &ga, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_push(b.list, &gb, 0, 0.0), GSHSCENE_OK);
  EXPECT_FALSE(gshscene_list_equal(a.list, b.list));
}

TEST(FilterBackdrop, ANodeCarriesTheModeAndItsEmissionIsAGroupPushWithIt) {
  StoreHolder s;
  Ref f = blur_filter(s.store, 2.0);
  Ref sq(intern_square(s.store));
  SceneHolder sc(nullptr, nullptr, s.store);
  uint64_t gid = 0;
  VersionRef v1, v2;
  {
    EditHolder e(sc);
    GSHSCENE_Node g = group_node();  // opacity 1, source-over, flat: it pushes only for its filter
    g.filter = f.get();
    g.filter_mode = GSHSCENE_FILTER_MODE_BACKDROP;
    gid = add(e.edit, GSHSCENE_ROOT_ID, g);
    add(e.edit, gid, shape_node(sq.get()));
    // refused as the list refuses it: a backdrop without a filter
    GSHSCENE_Node bad = group_node();
    bad.filter_mode = GSHSCENE_FILTER_MODE_BACKDROP;
    uint64_t id = 0;
    EXPECT_EQ(gshscene_edit_add(e.edit, GSHSCENE_ROOT_ID, 1, &bad, nullptr, &id), GSHSCENE_ERR_INVALID);
    publish(sc, e, &v1);
  }
  EXPECT_EQ(info_of(v1.get(), gid).node.filter_mode, (uint32_t)GSHSCENE_FILTER_MODE_BACKDROP);
  const GSHSCENE_List * list = nullptr;
  ASSERT_EQ(gshscene_version_list(v1.get(), &list), GSHSCENE_OK);
  const GSHSCENE_Item * item = nullptr;
  ASSERT_EQ(gshscene_list_item(list, 0, &item), GSHSCENE_OK);
  ASSERT_EQ(item->kind, GSHSCENE_ITEM_GROUP_PUSH);
  EXPECT_EQ(item->group.filter, f.get());
  EXPECT_EQ(item->group.filter_mode, (uint32_t)GSHSCENE_FILTER_MODE_BACKDROP);
  EXPECT_NE(dump_version(v1.get()).find(" fmode=1"), std::string::npos);
  // the same node with the mode at its default is another version
  {
    EditHolder e(sc);
    GSHSCENE_Node g = info_of(v1.get(), gid).node;
    g.filter_mode = GSHSCENE_FILTER_MODE_OWN;
    ASSERT_EQ(gshscene_edit_set(e.edit, gid, &g), GSHSCENE_OK);
    publish(sc, e, &v2);
  }
  EXPECT_NE(gshscene_version_hash(v1.get()), gshscene_version_hash(v2.get()));
  EXPECT_EQ(dump_version(v2.get()).find("fmode="), std::string::npos);
}

TEST(FilterBackdrop, ACopyOfAPictureIntoAnotherStoreKeepsTheMode) {
  StoreHolder src, dst;
  Ref f = blur_filter(src.store, 2.0);
  Ref pic;
  {
    ListHolder l(src.store);
    GSHSCENE_Group g = filtered(f.get(), GSHSCENE_FILTER_MODE_BACKDROP);
    ASSERT_EQ(gshscene_list_group_push(l.list, &g, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_group_pop(l.list), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
    const GSHSCENE_Resource * p = nullptr;
    ASSERT_EQ(gshscene_store_intern_picture(src.store, l.list, &p), GSHSCENE_OK);
    pic = Ref(p);
  }
  const GSHSCENE_Resource * copy = nullptr;
  ASSERT_EQ(gshscene_store_copy_resource(dst.store, pic.get(), &copy), GSHSCENE_OK);
  Ref hold(copy);
  const GSHSCENE_Item * item = nullptr;
  ASSERT_EQ(gshscene_list_item(gshscene_resource_picture(copy), 0, &item), GSHSCENE_OK);
  EXPECT_EQ(item->group.filter_mode, (uint32_t)GSHSCENE_FILTER_MODE_BACKDROP);
  EXPECT_EQ(gshscene_list_hash(gshscene_resource_picture(copy)), gshscene_list_hash(gshscene_resource_picture(pic.get())));
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
