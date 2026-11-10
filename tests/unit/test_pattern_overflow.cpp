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
 * A pattern whose tile may draw past its cell (pattern.h, PT15): the field and its order of checks, the one spelling of the default (the
 * handle, hash and dump of a clipped pattern are what they were), the extent found at intern from what the tile paints (a fill, a stroke's
 * pen, an instance's map, an empty tile, and the tiles whose extent is not known), and the planning pass's disposition of a unit painted with it.
 */

#include "scene_fixtures.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace scene;

namespace {

GSHSCENE_Paint pattern_paint(const GSHSCENE_Resource * pattern) {
  GSHSCENE_Paint p = solid(0, 0, 0, 1.0);
  p.kind = GSHSCENE_PAINT_PATTERN;
  p.resource = pattern;
  return p;
}

template <typename Fill>
const GSHSCENE_Resource * make_picture(GSHSCENE_Store * store, Fill fill) {
  GSHSCENE_List * l = nullptr;
  gshscene_list_create(nullptr, nullptr, store, &l);
  fill(l);
  gshscene_list_close(l);
  const GSHSCENE_Resource * picture = nullptr;
  GSHSCENE_Result r = gshscene_store_intern_picture(store, l, &picture);
  gshscene_list_destroy(l);
  return r == GSHSCENE_OK ? picture : nullptr;
}

struct World {
  StoreHolder s;
  Ref sq{intern_square(s.store)};  // [0, 4] x [0, 4]

  Ref picture_of_square(const GSHGEOM_Map * map) {
    return Ref(make_picture(s.store, [&](GSHSCENE_List * l) {
      gshscene_list_fill(l, sq.get(), map, GSHGEOM_FILL_NONZERO, nullptr, 0, 0);
    }));
  }
  GSHSCENE_Pattern pattern(const GSHSCENE_Resource * tile, double w, double h, uint32_t overflow) const {
    GSHSCENE_Pattern p;
    gshscene_pattern_init(&p);
    p.tile = tile;
    p.w = p.x_step = w;
    p.h = p.y_step = h;
    p.overflow = overflow;
    return p;
  }
  GSHSCENE_Result intern(const GSHSCENE_Pattern & p, Ref * out) const {
    const GSHSCENE_Resource * r = nullptr;
    GSHSCENE_Result result = gshscene_store_intern_pattern(s.store, &p, &r);
    *out = Ref(result == GSHSCENE_OK ? r : nullptr);
    return result;
  }
};

}  // namespace

TEST(PatternOverflow, ThePinnedValuesArePinned) {
  EXPECT_EQ((int)GSHSCENE_REASON_PATTERN_OVERFLOW, 44);
  EXPECT_STREQ(gshscene_reason_string(GSHSCENE_REASON_PATTERN_OVERFLOW), "pattern-overflow");
  EXPECT_EQ(GSHSCENE_CAP_PATTERN_OVERFLOW, UINT32_C(1) << 21);
  GSHSCENE_Pattern p;
  gshscene_pattern_init(&p);
  EXPECT_EQ(p.overflow, 0u) << "a pattern clips unless it is asked not to";
}

TEST(PatternOverflow, TheFieldIsCheckedAndMakesAnotherHandleAndHash) {
  World w;
  Ref tile = w.picture_of_square(nullptr);
  ASSERT_TRUE(tile.get());
  Ref clipped, visible, visible_again;
  ASSERT_EQ(w.intern(w.pattern(tile.get(), 2, 2, 0), &clipped), GSHSCENE_OK);
  ASSERT_EQ(w.intern(w.pattern(tile.get(), 2, 2, 1), &visible), GSHSCENE_OK);
  ASSERT_EQ(w.intern(w.pattern(tile.get(), 2, 2, 1), &visible_again), GSHSCENE_OK);
  EXPECT_NE(clipped.get(), visible.get());
  EXPECT_EQ(visible.get(), visible_again.get()) << "equal content is one handle";
  EXPECT_NE(gshscene_resource_hash(clipped.get()), gshscene_resource_hash(visible.get()));
  GSHSCENE_Pattern back;
  ASSERT_EQ(gshscene_resource_pattern(visible.get(), &back), GSHSCENE_OK);
  EXPECT_EQ(back.overflow, 1u);
  ASSERT_EQ(gshscene_resource_pattern(clipped.get(), &back), GSHSCENE_OK);
  EXPECT_EQ(back.overflow, 0u);
  Ref none;
  EXPECT_EQ(w.intern(w.pattern(tile.get(), 2, 2, 2), &none), GSHSCENE_ERR_INVALID) << "a value other than 0 and 1";
}

TEST(PatternOverflow, TheDumpNamesTheFieldOnlyWhenItIsSet) {
  World w;
  Ref tile = w.picture_of_square(nullptr);
  Ref clipped, visible;
  ASSERT_EQ(w.intern(w.pattern(tile.get(), 2, 2, 0), &clipped), GSHSCENE_OK);
  ASSERT_EQ(w.intern(w.pattern(tile.get(), 2, 2, 1), &visible), GSHSCENE_OK);
  auto dump_of = [&](const GSHSCENE_Resource * r) {
    char * text = nullptr;
    size_t size = 0;
    FILE * f = open_memstream(&text, &size);
    EXPECT_EQ(gshscene_resource_dump(r, f), GSHSCENE_OK);
    fclose(f);
    std::string out(text, size);
    free(text);
    return out;
  };
  const std::string a = dump_of(clipped.get());
  const std::string b = dump_of(visible.get());
  EXPECT_EQ(a.find("overflow"), std::string::npos);
  EXPECT_NE(b.find(" overflow=1\n"), std::string::npos);
  EXPECT_NE(a.find("pattern cell="), std::string::npos);
}

TEST(PatternOverflow, TheExtentIsTheCellWidenedToWhatTheTilePaints) {
  World w;
  double e[4];
  // A square [0, 4]^2 in a 2 x 2 cell: the extent is the square.
  Ref tile = w.picture_of_square(nullptr);
  Ref r;
  ASSERT_EQ(w.intern(w.pattern(tile.get(), 2, 2, 1), &r), GSHSCENE_OK);
  ASSERT_EQ(gshscene_pattern_extent(r.get(), e), GSHSCENE_OK);
  EXPECT_EQ(e[0], 0.0);
  EXPECT_EQ(e[1], 0.0);
  EXPECT_EQ(e[2], 4.0);
  EXPECT_EQ(e[3], 4.0);
  // The same tile with overflow 0 has the cell.
  ASSERT_EQ(w.intern(w.pattern(tile.get(), 2, 2, 0), &r), GSHSCENE_OK);
  ASSERT_EQ(gshscene_pattern_extent(r.get(), e), GSHSCENE_OK);
  EXPECT_EQ(e[2], 2.0);
  EXPECT_EQ(e[3], 2.0);
  // A cell larger than the content keeps the cell.
  ASSERT_EQ(w.intern(w.pattern(tile.get(), 10, 6, 1), &r), GSHSCENE_OK);
  ASSERT_EQ(gshscene_pattern_extent(r.get(), e), GSHSCENE_OK);
  EXPECT_EQ(e[2], 10.0);
  EXPECT_EQ(e[3], 6.0);
  // The fill's own map moves the box.
  GSHGEOM_Map tr = translate(-3.0, 5.0);
  Ref moved = w.picture_of_square(&tr);
  ASSERT_EQ(w.intern(w.pattern(moved.get(), 2, 2, 1), &r), GSHSCENE_OK);
  ASSERT_EQ(gshscene_pattern_extent(r.get(), e), GSHSCENE_OK);
  EXPECT_EQ(e[0], -3.0);
  EXPECT_EQ(e[1], 0.0);
  EXPECT_EQ(e[2], 2.0);
  EXPECT_EQ(e[3], 9.0);
  // An instance's map composes with the fill's.
  Ref inner = w.picture_of_square(nullptr);
  GSHGEOM_Map scale;
  gshgeom_map_identity(2, &scale);
  scale.m[0][0] = 2.0;
  scale.m[1][1] = 2.0;
  scale.t[0] = 1.0;
  Ref outer(make_picture(w.s.store, [&](GSHSCENE_List * l) { gshscene_list_instance(l, inner.get(), &scale, 0, 0); }));
  ASSERT_TRUE(outer.get());
  ASSERT_EQ(w.intern(w.pattern(outer.get(), 2, 2, 1), &r), GSHSCENE_OK);
  ASSERT_EQ(gshscene_pattern_extent(r.get(), e), GSHSCENE_OK);
  EXPECT_EQ(e[0], 0.0) << "the cell's own left edge is in it";
  EXPECT_EQ(e[2], 9.0) << "1 + 2 * 4";
  EXPECT_EQ(e[3], 8.0) << "2 * 4";
  // An empty tile paints nothing: the cell.
  Ref empty(make_picture(w.s.store, [](GSHSCENE_List *) {}));
  ASSERT_EQ(w.intern(w.pattern(empty.get(), 3, 5, 1), &r), GSHSCENE_OK);
  ASSERT_EQ(gshscene_pattern_extent(r.get(), e), GSHSCENE_OK);
  EXPECT_EQ(e[2], 3.0);
  EXPECT_EQ(e[3], 5.0);
  EXPECT_EQ(gshscene_pattern_extent(nullptr, e), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_pattern_extent(w.sq.get(), e), GSHSCENE_ERR_INVALID) << "not a pattern";
}

TEST(PatternOverflow, AStrokeIsOutsetByItsPenAndADeviceWidthIsUnknown) {
  World w;
  GSHSCENE_Stroke_Style st;
  gshscene_stroke_style_init(&st);
  st.width = 2.0;
  st.join = GSHSCENE_STROKE_JOIN_BEVEL;
  st.cap = GSHSCENE_STROKE_CAP_BUTT;
  const GSHSCENE_Resource * style = nullptr;
  ASSERT_EQ(gshscene_store_intern_stroke_style(w.s.store, &st, &style), GSHSCENE_OK);
  Ref style_ref(style);
  GSHSCENE_Paint paint = solid(0, 0, 0, 1);
  Ref tile(make_picture(w.s.store, [&](GSHSCENE_List * l) {
    gshscene_list_stroke(l, w.sq.get(), style, nullptr, &paint, 0, 0);
  }));
  Ref r;
  ASSERT_EQ(w.intern(w.pattern(tile.get(), 1, 1, 1), &r), GSHSCENE_OK);
  double e[4];
  ASSERT_EQ(gshscene_pattern_extent(r.get(), e), GSHSCENE_OK);
  EXPECT_LE(e[0], -1.0) << "half the width past the left edge, at least";
  EXPECT_LT(e[0], -0.9999);
  EXPECT_GE(e[2], 5.0);
  EXPECT_LT(e[2], 5.5) << "a bevel join does not reach as far as a miter";

  st.width_space = GSHSCENE_STROKE_WIDTH_DEVICE;
  const GSHSCENE_Resource * device = nullptr;
  ASSERT_EQ(gshscene_store_intern_stroke_style(w.s.store, &st, &device), GSHSCENE_OK);
  Ref device_ref(device);
  Ref dtile(make_picture(w.s.store, [&](GSHSCENE_List * l) {
    gshscene_list_stroke(l, w.sq.get(), device, nullptr, &paint, 0, 0);
  }));
  ASSERT_EQ(w.intern(w.pattern(dtile.get(), 1, 1, 1), &r), GSHSCENE_OK) << "unknown is not invalid: the pattern interns";
  EXPECT_EQ(gshscene_pattern_extent(r.get(), e), GSHSCENE_ERR_LIMIT) << "a device width's outset depends on the whole chain";
  ASSERT_EQ(w.intern(w.pattern(dtile.get(), 1, 1, 0), &r), GSHSCENE_OK);
  EXPECT_EQ(gshscene_pattern_extent(r.get(), e), GSHSCENE_OK) << "a pattern that clips never asks";
}

TEST(PatternOverflow, AHatchInTheTileHasNoKnownExtent) {
  World w;
  GSHSCENE_Stroke_Style st;
  gshscene_stroke_style_init(&st);
  const GSHSCENE_Resource * style = nullptr;
  ASSERT_EQ(gshscene_store_intern_stroke_style(w.s.store, &st, &style), GSHSCENE_OK);
  Ref style_ref(style);
  GSHSCENE_Hatch_Family family;
  family.angle = 45;
  family.origin_x = family.origin_y = 0;
  family.offset_along = 0;
  family.offset_across = 1.5;
  family.dash_count = 0;
  family.dashes = nullptr;
  GSHSCENE_Hatch h;
  gshscene_hatch_init(&h);
  h.style = style;
  h.family_count = 1;
  h.families = &family;
  const GSHSCENE_Resource * hatch = nullptr;
  ASSERT_EQ(gshscene_store_intern_hatch(w.s.store, &h, &hatch), GSHSCENE_OK);
  Ref hatch_ref(hatch);
  GSHSCENE_Paint paint = solid(0, 0, 0, 1);
  paint.kind = GSHSCENE_PAINT_HATCH;
  paint.resource = hatch;
  Ref tile(make_picture(w.s.store, [&](GSHSCENE_List * l) {
    gshscene_list_fill(l, w.sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &paint, 0, 0);
  }));
  ASSERT_TRUE(tile.get());
  Ref r;
  ASSERT_EQ(w.intern(w.pattern(tile.get(), 2, 2, 1), &r), GSHSCENE_OK);
  double e[4];
  EXPECT_EQ(gshscene_pattern_extent(r.get(), e), GSHSCENE_ERR_LIMIT) << "the pen of a hatch is outside the fill's region";
}

namespace {

GSHSCENE_Sink_Caps caps_of(uint32_t flags) {
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

const uint32_t kBase = GSHSCENE_CAP_CLIP_AA | GSHSCENE_CAP_BLEND_SPACES | GSHSCENE_CAP_GRADIENT_SPACES | GSHSCENE_CAP_GLYPHS_FROM_FONT |
    GSHSCENE_CAP_STROKE_RUNS | GSHSCENE_CAP_IMAGE_FILTER_SPACES | GSHSCENE_CAP_STROKE_EXPAND | GSHSCENE_CAP_PATTERN;

GSHSCENE_Disposition dispose(const GSHSCENE_Sink_Caps & caps, const GSHSCENE_Item * item) {
  GSHSCENE_Color_Space srgb = srgb_space();
  return gshscene_caps_disposition(&caps, item, nullptr, 0, nullptr, &srgb, nullptr);
}

/** A list with one fill painted with @p pattern; the item is read back. */
const GSHSCENE_Item * fill_with(GSHSCENE_List * l, World & w, const GSHSCENE_Resource * pattern) {
  GSHSCENE_Paint paint = pattern_paint(pattern);
  EXPECT_EQ(gshscene_list_fill(l, w.sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &paint, 0, 0), GSHSCENE_OK);
  const GSHSCENE_Item * item = nullptr;
  EXPECT_EQ(gshscene_list_item(l, 0, &item), GSHSCENE_OK);
  return item;
}

}  // namespace

TEST(PatternOverflow, ASinkWithoutTheFlagClipsAndSaysSoAndAnUnknownExtentSaysSoToo) {
  World w;
  Ref tile = w.picture_of_square(nullptr);
  Ref clipped, visible;
  ASSERT_EQ(w.intern(w.pattern(tile.get(), 2, 2, 0), &clipped), GSHSCENE_OK);
  ASSERT_EQ(w.intern(w.pattern(tile.get(), 2, 2, 1), &visible), GSHSCENE_OK);
  const GSHSCENE_Sink_Caps without = caps_of(kBase);
  const GSHSCENE_Sink_Caps with = caps_of(kBase | GSHSCENE_CAP_PATTERN_OVERFLOW);
  ListHolder l1(w.s.store);
  ListHolder l2(w.s.store);
  const GSHSCENE_Item * item_clipped = fill_with(l1.list, w, clipped.get());
  const GSHSCENE_Item * item_visible = fill_with(l2.list, w, visible.get());
  EXPECT_EQ(dispose(without, item_clipped).entry_class, 0) << "a pattern that clips needs no flag";
  EXPECT_EQ(dispose(with, item_clipped).entry_class, 0);
  GSHSCENE_Disposition d = dispose(without, item_visible);
  EXPECT_EQ(d.entry_class, GSHSCENE_ENTRY_APPROXIMATED);
  EXPECT_EQ(d.reason, GSHSCENE_REASON_PATTERN_OVERFLOW);
  EXPECT_EQ(dispose(with, item_visible).entry_class, 0);
  // A sink with no pattern at all refuses it, whatever it overflows.
  const GSHSCENE_Sink_Caps no_pattern = caps_of(kBase & ~GSHSCENE_CAP_PATTERN);
  EXPECT_EQ(dispose(no_pattern, item_visible).entry_class, GSHSCENE_ENTRY_REFUSED);
  EXPECT_EQ(dispose(no_pattern, item_visible).reason, GSHSCENE_REASON_PATTERN);

  // An extent that is not known is drawn clipped even by a sink that has the flag.
  GSHSCENE_Stroke_Style st;
  gshscene_stroke_style_init(&st);
  st.width_space = GSHSCENE_STROKE_WIDTH_DEVICE;
  const GSHSCENE_Resource * style = nullptr;
  ASSERT_EQ(gshscene_store_intern_stroke_style(w.s.store, &st, &style), GSHSCENE_OK);
  Ref style_ref(style);
  GSHSCENE_Paint paint = solid(0, 0, 0, 1);
  Ref dtile(make_picture(w.s.store, [&](GSHSCENE_List * l) {
    gshscene_list_stroke(l, w.sq.get(), style, nullptr, &paint, 0, 0);
  }));
  Ref unknown;
  ASSERT_EQ(w.intern(w.pattern(dtile.get(), 1, 1, 1), &unknown), GSHSCENE_OK);
  ListHolder l3(w.s.store);
  const GSHSCENE_Item * item_unknown = fill_with(l3.list, w, unknown.get());
  d = dispose(with, item_unknown);
  EXPECT_EQ(d.entry_class, GSHSCENE_ENTRY_APPROXIMATED);
  EXPECT_EQ(d.reason, GSHSCENE_REASON_PATTERN_OVERFLOW);
}

TEST(PatternOverflow, ATileRefusalWinsOverTheApproximation) {
  // A tile that holds a unit the sink refuses is refused whole (PT13), whether the pattern overflows or not.
  World w;
  GSHSCENE_Group group = make_group(1.0);
  group.knockout = 1;
  Ref tile(make_picture(w.s.store, [&](GSHSCENE_List * l) {
    gshscene_list_group_push(l, &group, 0, 0);
    gshscene_list_fill(l, w.sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0);
    gshscene_list_group_pop(l);
  }));
  Ref visible;
  ASSERT_EQ(w.intern(w.pattern(tile.get(), 2, 2, 1), &visible), GSHSCENE_OK);
  ListHolder l(w.s.store);
  const GSHSCENE_Item * item = fill_with(l.list, w, visible.get());
  const GSHSCENE_Sink_Caps without = caps_of(kBase);
  const GSHSCENE_Disposition d = dispose(without, item);
  EXPECT_EQ(d.entry_class, GSHSCENE_ENTRY_REFUSED);
  EXPECT_EQ(d.reason, GSHSCENE_REASON_PATTERN);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
