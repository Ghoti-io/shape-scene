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
 * The filter's picture (filter.h, FL25): an `IMAGE` primitive that draws a slot of the filter's picture instead of an image.  The
 * order of its refusals, equality and hash by content, the copy into another store, the feature a sink is asked for, the group
 * that may not hold both a mask and such a filter, and the picture counting as a mask's does (depth, the plan's walk).
 */

#include "scene_helpers.h"

#include "../image_fixtures.h"

#include <vector>

using namespace scene;

namespace {

GSHSCENE_Filter_Primitive slot_primitive(uint32_t slot) {
  GSHSCENE_Filter_Primitive p;
  std::memset(&p, 0, sizeof p);
  p.kind = GSHSCENE_FILTER_IMAGE;
  p.use_picture = 1;
  p.slot = slot;
  return p;
}

GSHSCENE_Filter filter_with(const std::vector<GSHSCENE_Filter_Primitive> & prims, const GSHSCENE_Resource * picture) {
  GSHSCENE_Filter f;
  std::memset(&f, 0, sizeof f);
  f.region_mode = GSHSCENE_FILTER_REGION_AUTO;
  f.color_space = GSHSCENE_FILTER_SPACE_LINEAR_RGB;
  f.primitive_count = prims.size();
  f.primitives = prims.data();
  f.picture = picture;
  return f;
}

GSHSCENE_Result intern_filter(GSHSCENE_Store * s, const GSHSCENE_Filter & f, Ref * out = nullptr) {
  const GSHSCENE_Resource * r = nullptr;
  GSHSCENE_Result got = gshscene_store_intern_filter(s, &f, &r);
  if (out && got == GSHSCENE_OK) {
    *out = Ref(r);
  } else if (got == GSHSCENE_OK) {
    gshscene_resource_release(r);
  }
  return got;
}

/** A picture of a filled square, which an instance names. */
const GSHSCENE_Resource * square_picture(GSHSCENE_Store * store, double at) {
  ListHolder l(store);
  Ref path(intern_square(store, at));
  GSHSCENE_Paint paint = solid(1, 0, 0, 1);
  gshscene_list_fill(l.list, path.get(), nullptr, GSHGEOM_FILL_NONZERO, &paint, 0, 0.0);
  gshscene_list_close(l.list);
  const GSHSCENE_Resource * pic = nullptr;
  return gshscene_store_intern_picture(store, l.list, &pic) == GSHSCENE_OK ? pic : nullptr;
}

/** A picture whose items are @p count instances of squares (the filter's slots). */
const GSHSCENE_Resource * slots_picture(GSHSCENE_Store * store, size_t count, bool all_instances = true) {
  ListHolder l(store);
  for (size_t i = 0; i < count; i++) {
    Ref inner(square_picture(store, (double)i));
    GSHGEOM_Map m;
    gshgeom_map_translate(2, (double)i * 3.0, 0.0, 0.0, &m);
    gshscene_list_instance(l.list, inner.get(), &m, 0, 0.0);
  }
  if (!all_instances) {
    Ref path(intern_square(store));
    GSHSCENE_Paint paint = solid(0, 0, 0, 1);
    gshscene_list_fill(l.list, path.get(), nullptr, GSHGEOM_FILL_NONZERO, &paint, 0, 0.0);
  }
  gshscene_list_close(l.list);
  const GSHSCENE_Resource * pic = nullptr;
  return gshscene_store_intern_picture(store, l.list, &pic) == GSHSCENE_OK ? pic : nullptr;
}

}  // namespace

TEST(FilterPicture, ASlotOfThePictureInternsAndEqualContentIsOneHandle) {
  StoreHolder s;
  Ref pic(slots_picture(s.store, 2));
  ASSERT_NE(pic.get(), nullptr);
  std::vector<GSHSCENE_Filter_Primitive> prims = {slot_primitive(1)};
  Ref a, b, other_slot, other_picture;
  ASSERT_EQ(intern_filter(s.store, filter_with(prims, pic.get()), &a), GSHSCENE_OK);
  ASSERT_EQ(intern_filter(s.store, filter_with(prims, pic.get()), &b), GSHSCENE_OK);
  EXPECT_EQ(a.get(), b.get());
  std::vector<GSHSCENE_Filter_Primitive> zero = {slot_primitive(0)};
  ASSERT_EQ(intern_filter(s.store, filter_with(zero, pic.get()), &other_slot), GSHSCENE_OK);
  EXPECT_NE(a.get(), other_slot.get()) << "the slot is content";
  EXPECT_NE(gshscene_resource_hash(a.get()), gshscene_resource_hash(other_slot.get()));
  Ref pic2(slots_picture(s.store, 3));
  ASSERT_EQ(intern_filter(s.store, filter_with(prims, pic2.get()), &other_picture), GSHSCENE_OK);
  EXPECT_NE(a.get(), other_picture.get()) << "the picture is content";
  EXPECT_NE(gshscene_resource_hash(a.get()), gshscene_resource_hash(other_picture.get())) << "and part of the hash";
  GSHSCENE_Filter back;
  ASSERT_EQ(gshscene_resource_filter(a.get(), &back), GSHSCENE_OK);
  EXPECT_EQ(back.picture, pic.get());
  EXPECT_EQ(back.primitives[0].use_picture, 1u);
  EXPECT_EQ(back.primitives[0].slot, 1u);
  EXPECT_EQ(back.primitives[0].placement.dim, 0u) << "the placement of a slot stays zero, so what is read back interns again";
  Ref again;
  EXPECT_EQ(intern_filter(s.store, back, &again), GSHSCENE_OK);
  EXPECT_EQ(again.get(), a.get());
}

TEST(FilterPicture, TheRefusalsInTheirOrder) {
  StoreHolder s, other;
  Ref pic(slots_picture(s.store, 2));
  Ref not_all(slots_picture(s.store, 1, false));
  Ref foreign(slots_picture(other.store, 2));
  Ref path(intern_square(s.store));
  ASSERT_NE(pic.get(), nullptr);
  ASSERT_NE(not_all.get(), nullptr);
  ASSERT_NE(foreign.get(), nullptr);
  std::vector<GSHSCENE_Filter_Primitive> one = {slot_primitive(0)};
  EXPECT_EQ(intern_filter(s.store, filter_with(one, nullptr)), GSHSCENE_ERR_INVALID) << "a slot without a picture";
  EXPECT_EQ(intern_filter(s.store, filter_with(one, foreign.get())), GSHSCENE_ERR_INVALID) << "a picture of another store";
  EXPECT_EQ(intern_filter(s.store, filter_with(one, path.get())), GSHSCENE_ERR_INVALID) << "a picture that is a path";
  EXPECT_EQ(intern_filter(s.store, filter_with(one, not_all.get())), GSHSCENE_ERR_INVALID) << "an item that is not an instance";
  std::vector<GSHSCENE_Filter_Primitive> past = {slot_primitive(2)};
  EXPECT_EQ(intern_filter(s.store, filter_with(past, pic.get())), GSHSCENE_ERR_INVALID) << "a slot past the picture";
  // a picture nothing draws
  GSHSCENE_Filter_Primitive flood;
  std::memset(&flood, 0, sizeof flood);
  flood.kind = GSHSCENE_FILTER_FLOOD;
  flood.opacity = 1.0;
  std::vector<GSHSCENE_Filter_Primitive> unused = {flood};
  EXPECT_EQ(intern_filter(s.store, filter_with(unused, pic.get())), GSHSCENE_ERR_INVALID) << "a picture no primitive draws";
  // fields that do not apply to a slot
  GSHSCENE_Filter_Primitive p = slot_primitive(0);
  p.rendering = 1;
  std::vector<GSHSCENE_Filter_Primitive> bad = {p};
  EXPECT_EQ(intern_filter(s.store, filter_with(bad, pic.get())), GSHSCENE_ERR_INVALID) << "a rendering on a slot";
  p = slot_primitive(0);
  gshgeom_map_identity(2, &p.placement);
  bad = {p};
  EXPECT_EQ(intern_filter(s.store, filter_with(bad, pic.get())), GSHSCENE_ERR_INVALID) << "a placement on a slot";
  p = slot_primitive(0);
  p.image = path.get();
  bad = {p};
  EXPECT_EQ(intern_filter(s.store, filter_with(bad, pic.get())), GSHSCENE_ERR_INVALID) << "an image and a slot";
  p = slot_primitive(0);
  p.use_picture = 2;
  bad = {p};
  EXPECT_EQ(intern_filter(s.store, filter_with(bad, pic.get())), GSHSCENE_ERR_INVALID) << "use_picture is 0 or 1";
  // a slot index on a primitive that draws an image, and `use_picture` on a kind that is not IMAGE
  p = flood;
  p.use_picture = 1;
  bad = {p};
  EXPECT_EQ(intern_filter(s.store, filter_with(bad, pic.get())), GSHSCENE_ERR_INVALID) << "use_picture on a flood";
  p = flood;
  p.slot = 1;
  bad = {p};
  EXPECT_EQ(intern_filter(s.store, filter_with(bad, nullptr)), GSHSCENE_ERR_INVALID) << "a slot on a flood";
  // a picture that is the empty list
  Ref empty(slots_picture(s.store, 0));
  if (empty.get()) {
    EXPECT_EQ(intern_filter(s.store, filter_with(one, empty.get())), GSHSCENE_ERR_INVALID) << "no slot to draw";
  }
}

TEST(FilterPicture, TheFilterHoldsItsPictureAndReleasesItWithItself) {
  StoreHolder s;
  GSHSCENE_Store_Stats empty;
  gshscene_store_stats(s.store, &empty);
  {
    Ref a;
    {
      Ref pic(slots_picture(s.store, 2));
      std::vector<GSHSCENE_Filter_Primitive> prims = {slot_primitive(1)};
      ASSERT_EQ(intern_filter(s.store, filter_with(prims, pic.get()), &a), GSHSCENE_OK);
    }  // the caller's reference to the picture is gone: the filter's keeps it
    GSHSCENE_Filter f;
    ASSERT_EQ(gshscene_resource_filter(a.get(), &f), GSHSCENE_OK);
    EXPECT_EQ(gshscene_list_count(gshscene_resource_picture(f.picture)), 2u);
    GSHSCENE_Store_Stats during;
    gshscene_store_stats(s.store, &during);
    EXPECT_GE(during.live, 4u) << "the filter, its picture, the slots' pictures and a path";
  }
  GSHSCENE_Store_Stats after;
  gshscene_store_stats(s.store, &after);
  EXPECT_EQ(after.live, empty.live) << "nothing is left held";
}

TEST(FilterPicture, AnImageCannotCarryASlot) {
  StoreHolder s;
  Ref pic(slots_picture(s.store, 2));
  GSHSCENE_Filter_Primitive p;
  std::memset(&p, 0, sizeof p);
  p.kind = GSHSCENE_FILTER_IMAGE;
  p.slot = 1;
  std::vector<GSHSCENE_Filter_Primitive> prims = {p};
  EXPECT_EQ(intern_filter(s.store, filter_with(prims, pic.get())), GSHSCENE_ERR_INVALID) << "a slot on a primitive without use_picture (no image either)";
  p.image = pic.get();
  prims = {p};
  EXPECT_EQ(intern_filter(s.store, filter_with(prims, nullptr)), GSHSCENE_ERR_INVALID) << "a picture is not an image";
  // a real image: with no slot it interns, with a slot it does not
  imgfx::Fmt fmt = imgfx::kRgba8;
  GIMG_Pixel_Format format = imgfx::format_of(fmt);
  GIMG_Raster * raster = nullptr;
  ASSERT_EQ(gimg_raster_create(1, 1, &format, GIMG_RASTER_OWNED, nullptr, 0, &raster), GIMG_OK);
  Ref image(imgfx::copy_in(s.store, raster, imgfx::params()));
  gimg_raster_destroy(raster);
  ASSERT_NE(image.get(), nullptr);
  p.image = image.get();
  p.slot = 0;
  prims = {p};
  EXPECT_EQ(intern_filter(s.store, filter_with(prims, nullptr)), GSHSCENE_OK);
  p.slot = 1;
  prims = {p};
  EXPECT_EQ(intern_filter(s.store, filter_with(prims, nullptr)), GSHSCENE_ERR_INVALID) << "a slot beside an image";
}

TEST(FilterPicture, ACopyIntoAnotherStoreKeepsThePictureAndTheSlot) {
  StoreHolder s, d;
  Ref pic(slots_picture(s.store, 2));
  std::vector<GSHSCENE_Filter_Primitive> prims = {slot_primitive(1)};
  Ref a;
  ASSERT_EQ(intern_filter(s.store, filter_with(prims, pic.get()), &a), GSHSCENE_OK);
  const GSHSCENE_Resource * raw = nullptr;
  ASSERT_EQ(gshscene_store_copy_resource(d.store, a.get(), &raw), GSHSCENE_OK);
  Ref copy(raw);
  GSHSCENE_Filter f;
  ASSERT_EQ(gshscene_resource_filter(copy.get(), &f), GSHSCENE_OK);
  ASSERT_NE(f.picture, nullptr);
  EXPECT_EQ(gshscene_list_count(gshscene_resource_picture(f.picture)), 2u);
  EXPECT_EQ(f.primitives[0].slot, 1u);
  EXPECT_EQ(gshscene_resource_hash(copy.get()), gshscene_resource_hash(a.get())) << "the same content, so the same hash";
}

TEST(FilterPicture, ASinkIsAskedForThePictureFeature) {
  StoreHolder s;
  Ref pic(slots_picture(s.store, 1));
  std::vector<GSHSCENE_Filter_Primitive> prims = {slot_primitive(0)};
  Ref a;
  ASSERT_EQ(intern_filter(s.store, filter_with(prims, pic.get()), &a), GSHSCENE_OK);
  EXPECT_EQ(gshscene_filter_required(a.get()),
      GSHSCENE_FILTER_NATIVE_PICTURE | (1u << GSHSCENE_FILTER_IMAGE) | GSHSCENE_FILTER_NATIVE_REGION_AUTO);
}

TEST(FilterPicture, AGroupNamesTheFilterButNotTogetherWithAMask) {
  StoreHolder s;
  Ref pic(slots_picture(s.store, 1));
  std::vector<GSHSCENE_Filter_Primitive> prims = {slot_primitive(0)};
  Ref f;
  ASSERT_EQ(intern_filter(s.store, filter_with(prims, pic.get()), &f), GSHSCENE_OK);
  GSHSCENE_Mask m;
  std::memset(&m, 0, sizeof m);
  m.picture = pic.get();
  m.mode = GSHSCENE_MASK_ALPHA;
  const GSHSCENE_Resource * raw = nullptr;
  ASSERT_EQ(gshscene_store_intern_mask(s.store, &m, &raw), GSHSCENE_OK);
  Ref mask(raw);
  ListHolder l(s.store);
  GSHSCENE_Group g;
  std::memset(&g, 0, sizeof g);
  g.opacity = 1.0;
  g.filter = f.get();
  EXPECT_EQ(gshscene_list_group_push(l.list, &g, 0, 0.0), GSHSCENE_OK);
  GSHSCENE_Group both = g;
  both.soft_mask = mask.get();
  EXPECT_EQ(gshscene_list_group_push(l.list, &both, 0, 0.0), GSHSCENE_ERR_INVALID) << "two pictures at one push";
  // a filter with no picture may share a group with a mask, as before
  std::vector<GSHSCENE_Filter_Primitive> plain(1);
  plain[0].kind = GSHSCENE_FILTER_FLOOD;
  plain[0].opacity = 1.0;
  Ref pf;
  ASSERT_EQ(intern_filter(s.store, filter_with(plain, nullptr), &pf), GSHSCENE_OK);
  GSHSCENE_Group ok = g;
  ok.filter = pf.get();
  ok.soft_mask = mask.get();
  EXPECT_EQ(gshscene_list_group_push(l.list, &ok, 0, 0.0), GSHSCENE_OK);
}

TEST(FilterPicture, ThePicturesDepthCountsInTheListThatHoldsTheGroupAndThePlanEntersIt) {
  StoreHolder s;
  Ref pic(slots_picture(s.store, 2));  // depth 2: the slots' pictures are one level more
  std::vector<GSHSCENE_Filter_Primitive> prims = {slot_primitive(0), slot_primitive(1)};
  Ref f;
  ASSERT_EQ(intern_filter(s.store, filter_with(prims, pic.get()), &f), GSHSCENE_OK);
  Ref path(intern_square(s.store));
  GSHSCENE_Paint paint = solid(0, 0, 0, 1);
  ListHolder l(s.store);
  GSHSCENE_Group g;
  std::memset(&g, 0, sizeof g);
  g.opacity = 1.0;
  g.filter = f.get();
  ASSERT_EQ(gshscene_list_group_push(l.list, &g, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(l.list, path.get(), nullptr, GSHGEOM_FILL_NONZERO, &paint, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_pop(l.list), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
  {
    // the list's deepest picture is the filter's: interned, it is one level more than the filter's picture
    const GSHSCENE_Resource * raw = nullptr;
    ASSERT_EQ(gshscene_store_intern_picture(s.store, l.list, &raw), GSHSCENE_OK);
    Ref holder(raw);
    EXPECT_EQ(gshscene_resource_picture_depth(raw), gshscene_resource_picture_depth(pic.get()) + 1u);
  }
  // the ordinary walk does not enter it; the plan's walk does, one level down, after the group's push
  size_t bytes = gshscene_expansion_bytes(l.list);
  std::vector<unsigned char> memory(bytes);
  GSHSCENE_Expansion walk;
  std::memset(&walk, 0xFF, sizeof walk);
  ASSERT_EQ(gshscene_expansion_init(&walk, l.list, memory.data(), bytes), GSHSCENE_OK);
  const GSHSCENE_Item * item;
  const size_t * chain;
  size_t count;
  size_t plain = 0;
  while (gshscene_expansion_next(&walk, &item, &chain, &count)) {
    EXPECT_EQ(count, 1u);
    plain++;
  }
  EXPECT_EQ(plain, 3u);
  std::memset(&walk, 0xFF, sizeof walk);
  ASSERT_EQ(gshscene_expansion_init(&walk, l.list, memory.data(), bytes), GSHSCENE_OK);
  walk.plan = 1;
  std::vector<size_t> depths;
  while (gshscene_expansion_next(&walk, &item, &chain, &count)) {
    depths.push_back(count);
  }
  // push; the picture's two instances, each entered through its own picture's fill; the group's fill; the pop
  EXPECT_EQ(depths, (std::vector<size_t>{1, 2, 3, 2, 3, 1, 1}));
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
