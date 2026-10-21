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
 * The hash and the dump of every resource and list, against the known answers
 * of the independent generator (CAP-13, AD-21).
 */

#include "scene_helpers.h"

#include "../alloc_tracker.h"
#include "../known_answers.h"

#include <clocale>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

using namespace scene;

namespace {

/** Every fixture of tools/gen-scene-vectors.py, built through the API in one store. */
struct Fixture {
  GSHSCENE_Store * store;
  const GSHSCENE_Resource * sq = nullptr;
  const GSHSCENE_Resource * p2 = nullptr;
  const GSHSCENE_Resource * face = nullptr;
  const GSHSCENE_Resource * font_run = nullptr;
  const GSHSCENE_Resource * outline_run = nullptr;
  const GSHSCENE_Resource * picture = nullptr;
  const GSHSCENE_Resource * palette = nullptr;   // three entries from doubles (story 14c)
  const GSHSCENE_Resource * palette8 = nullptr;  // three entries from RGBA8 (fixed.h)
  GSHSCENE_List * list = nullptr;
  GSHSCENE_List * order_list = nullptr;  // recorded in declared order, closed into paint order

  explicit Fixture(GSHSCENE_Store * s) : store(s) {
    sq = intern_square(store);
    p2 = intern_fixture(store);
    GSHSCENE_Glyph_Run run;
    std::memset(&run, 0, sizeof run);
    GSHSCENE_Glyph fglyphs[3] = {{36, 0.0, 0.0, nullptr}, {37, 7.5, 0.25, nullptr}, {4294967295u, -1.0, 2.0, nullptr}};
    run.source = GSHSCENE_GLYPHS_FROM_FONT;
    face = intern_face(store);
    run.face = face;
    run.size = 12.5;
    run.hint_mode = GSHSCENE_HINT_NONE;
    run.hint_version = 40;
    run.glyph_count = 3;
    run.glyphs = fglyphs;
    gshscene_store_intern_glyph_run(store, &run, &font_run);
    GSHSCENE_Glyph oglyphs[2] = {{1, 0.0, 0.0, sq}, {2, 5.0, -3.0, p2}};
    std::memset(&run, 0, sizeof run);
    run.source = GSHSCENE_GLYPHS_FROM_OUTLINES;
    run.glyph_count = 2;
    run.glyphs = oglyphs;
    gshscene_store_intern_glyph_run(store, &run, &outline_run);

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
    gshscene_store_intern_picture(store, inner, &picture);
    gshscene_list_destroy(inner);

    gshscene_list_create(nullptr, nullptr, store, &list);
    GSHGEOM_Map design;
    gshgeom_map_design_to_model(2.5, 10, 20, &design);
    GSHSCENE_Paint blue = solid(0.25, 0.5, 0.75, 1.0);
    blue.space = space_of(GCOL_PRIMARIES_ADOBE_RGB, GCOL_TRANSFER_GAMMA, 2.19921875);
    GSHSCENE_Paint wide = solid(2.0, -0.5, 0.0, 0.5);
    GSHSCENE_Paint currentcolor = solid(0.0, 0.0, 0.0, 0.75);
    currentcolor.kind = GSHSCENE_PAINT_FOREGROUND;
    GSHSCENE_Color fg = {{0.1, 0.2, 0.3, 0.5}, linear_srgb_space()};
    gshscene_list_set_foreground(list, &fg);
    const GSHSCENE_Color entries[3] = {{{1.0, 0.5, 0.25, 1.0}, space_of(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_SRGB, 0.0)},
        {{0.0, 0.75, 1.0, 0.5}, space_of(GCOL_PRIMARIES_DISPLAY_P3, GCOL_TRANSFER_GAMMA, 2.2)},
        {{0.125, 0.0, 2.0, 0.25}, linear_srgb_space()}};
    gshscene_store_intern_palette(store, 3, entries, &palette);
    const uint8_t bytes[12] = {255, 0, 0, 255, 0, 128, 255, 51, 1, 2, 254, 0};
    gshscene_store_intern_palette_rgba8(store, 3, bytes, &palette8);
    gshscene_list_set_palette(list, palette);
    GSHSCENE_Paint entry2 = solid(0.0, 0.0, 0.0, 0.5);
    entry2.kind = GSHSCENE_PAINT_PALETTE;
    entry2.entry = 2;
    gshscene_list_fill(list, sq, &design, GSHGEOM_FILL_NONZERO, &blue, -3, 0.5);
    gshscene_list_fill(list, font_run, nullptr, GSHGEOM_FILL_EVEN_ODD, nullptr, 0, 0);
    gshscene_list_clip_push(list, p2, &tr, GSHGEOM_FILL_EVEN_ODD, 4, -2.0);
    GSHSCENE_Group blend = make_group(0.5, GSHSCENE_ORDER_FLAT, GSHSCENE_COMPOSITE_SRC_IN);
    blend.blend_space = linear_srgb_space();
    gshscene_list_group_push(list, &blend, 7, 1.5);
    gshscene_list_instance(list, picture, &tr, 1, 3.25);
    gshscene_list_fill(list, outline_run, nullptr, GSHGEOM_FILL_NONZERO, &wide, 5, 0);
    gshscene_list_fill(list, sq, nullptr, GSHGEOM_FILL_NONZERO, &currentcolor, 6, 0);
    gshscene_list_fill(list, p2, nullptr, GSHGEOM_FILL_EVEN_ODD, &entry2, 8, 0);
    gshscene_list_group_pop(list);
    gshscene_list_clip_pop(list);
    gshscene_list_close(list);

    // The order fixture, in declared order (tools/gen-scene-vectors.py: ORDER_ITEMS_DECLARED).
    gshscene_list_create(nullptr, nullptr, store, &order_list);
    auto orderfill = [&](int id, int32_t layer = 0, double depth = 0.0) {
      GSHSCENE_Paint p = solid(id / 16.0, 0.0, 0.0, 1.0);
      ASSERT_EQ(gshscene_list_fill(order_list, sq, nullptr, GSHGEOM_FILL_NONZERO, &p, layer, depth), GSHSCENE_OK);
    };
    GSHSCENE_Group depth_group = make_group(1.0, GSHSCENE_ORDER_DEPTH);
    GSHSCENE_Group overlay_group = make_group(1.0, GSHSCENE_ORDER_OVERLAY);
    GSHSCENE_Group flat_group = make_group(0.5);
    orderfill(1);
    gshscene_list_group_push(order_list, &depth_group, 0, 0.0);
    orderfill(2, 0, 1.0);
    orderfill(3, 1, 2.0);
    orderfill(4, 0, 2.0);
    orderfill(5, 0, 2.0);
    orderfill(6, 0, 3.0);
    gshscene_list_clip_push(order_list, p2, &tr, GSHGEOM_FILL_EVEN_ODD, 0, 2.0);
    orderfill(7);
    gshscene_list_clip_pop(order_list);
    gshscene_list_group_push(order_list, &overlay_group, 0, 0.0);
    orderfill(8, 3);
    orderfill(9, 1);
    orderfill(10, 3);
    orderfill(11, 2);
    gshscene_list_group_pop(order_list);
    gshscene_list_group_push(order_list, &flat_group, 5, 2.0);
    orderfill(12);
    orderfill(13);
    gshscene_list_group_pop(order_list);
    gshscene_list_group_pop(order_list);
    orderfill(14);
    gshscene_list_group_push(order_list, &depth_group, 0, 0.0);
    orderfill(15);
    orderfill(16);
    gshscene_list_group_pop(order_list);
    gshscene_list_close(order_list);
  }
  ~Fixture() {
    gshscene_list_destroy(order_list);
    gshscene_list_destroy(list);
    gshscene_resource_release(palette8);
    gshscene_resource_release(palette);
    gshscene_resource_release(picture);
    gshscene_resource_release(outline_run);
    gshscene_resource_release(font_run);
    gshscene_resource_release(face);
    gshscene_resource_release(p2);
    gshscene_resource_release(sq);
  }
};

std::vector<std::string> lines_of(const std::string & text) {
  std::vector<std::string> lines;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    lines.push_back(line);
  }
  return lines;
}

std::string hex16(uint64_t v) {
  char buffer[17];
  std::snprintf(buffer, sizeof buffer, "%016llx", (unsigned long long)v);
  return buffer;
}

}  // namespace

TEST(KnownAnswers, TheMapsOfTheFixturesHashAsTheIndependentGeneratorSays) {
  GSHGEOM_Map identity;
  gshgeom_map_identity(2, &identity);
  GSHGEOM_Map tr = translate(1.5, -2.25);
  GSHGEOM_Map design;
  gshgeom_map_design_to_model(2.5, 10, 20, &design);
  EXPECT_EQ(gshgeom_map_hash(&identity), known::kMapIdentityHash);
  EXPECT_EQ(gshgeom_map_hash(&tr), known::kMapTranslateHash);
  EXPECT_EQ(gshgeom_map_hash(&design), known::kMapDesignHash);
}

TEST(KnownAnswers, EveryResourceHashesAsTheIndependentGeneratorSays) {
  StoreHolder s;
  Fixture f(s.store);
  ASSERT_NE(f.picture, nullptr);
  EXPECT_EQ(gshscene_resource_hash(f.sq), known::kSquareResourceHash);
  EXPECT_EQ(gshscene_resource_hash(f.p2), known::kFixturePathResourceHash);
  EXPECT_EQ(gshscene_resource_hash(f.face), known::kFaceHash);
  EXPECT_EQ(gshscene_resource_hash(f.font_run), known::kFontRunHash);
  EXPECT_EQ(gshscene_resource_hash(f.outline_run), known::kOutlineRunHash);
  EXPECT_EQ(gshscene_resource_hash(f.picture), known::kPictureHash);
  EXPECT_EQ(gshscene_resource_hash(f.palette), known::kPaletteHash);
  EXPECT_EQ(gshscene_resource_hash(f.palette8), known::kPalette8Hash);
  EXPECT_EQ(gshscene_list_hash(gshscene_resource_picture(f.picture)), known::kPictureListHash);
}

TEST(KnownAnswers, TheListHashesAsTheIndependentGeneratorSays) {
  StoreHolder s;
  Fixture f(s.store);
  ASSERT_EQ(gshscene_list_count(f.list), 10u);
  EXPECT_EQ(gshscene_list_hash(f.list), known::kListHash);
  ASSERT_EQ(gshscene_list_count(f.order_list), 26u);
  EXPECT_EQ(gshscene_list_hash(f.order_list), known::kOrderListHash)
      << "the closed list is the independent generator's paint order";
  EXPECT_EQ(gshscene_list_tie_count(f.order_list), 5u);
}

TEST(KnownAnswers, EveryDumpIsTheIndependentGeneratorsText) {
  StoreHolder s;
  Fixture f(s.store);
  EXPECT_EQ(dump_resource(f.sq), known::kSquareResourceDump);
  EXPECT_EQ(dump_resource(f.p2), known::kFixturePathResourceDump);
  EXPECT_EQ(dump_resource(f.face), known::kFaceDump);
  EXPECT_EQ(dump_resource(f.font_run), known::kFontRunDump);
  EXPECT_EQ(dump_resource(f.outline_run), known::kOutlineRunDump);
  EXPECT_EQ(dump_resource(f.picture), known::kPictureDump);
  EXPECT_EQ(dump_resource(f.palette), known::kPaletteDump);
  EXPECT_EQ(dump_resource(f.palette8), known::kPalette8Dump);
  EXPECT_EQ(dump_list(f.list), known::kListDump);
  EXPECT_EQ(dump_list(f.order_list), known::kOrderListDump);
}

TEST(Hash, DoesNotDependOnTheStoreTheIdsTheAllocatorOrTheTableIndex) {
  StoreHolder first;
  Fixture a(first.store);
  // A second store with other resources made first, so every id differs, a tracking allocator,
  // and a table indexed by a constant.
  tracker::Tracker t;
  GSHSCENE_Allocator alloc = tracker::allocator_for(&t);
  GSHSCENE_Store * store = nullptr;
  ASSERT_EQ(gshscene_store_create_with_index_hash(&alloc, nullptr,
                [](uint64_t, void *) -> uint64_t { return 7; }, nullptr, &store),
      GSHSCENE_OK);
  Ref pad1(intern_square(store, 50.0)), pad2(intern_square(store, 51.0));
  {
    Fixture b(store);
    EXPECT_NE(gshscene_resource_id(a.sq), gshscene_resource_id(b.sq));
    EXPECT_EQ(gshscene_list_hash(a.list), gshscene_list_hash(b.list));
    EXPECT_EQ(dump_list(a.list), dump_list(b.list));
    EXPECT_EQ(dump_resource(a.picture), dump_resource(b.picture));
    EXPECT_EQ(dump_resource(a.outline_run), dump_resource(b.outline_run));
  }
  gshscene_resource_release(pad1.resource);
  pad1.resource = nullptr;
  gshscene_resource_release(pad2.resource);
  pad2.resource = nullptr;
  gshscene_store_destroy(store);
  EXPECT_TRUE(t.live.empty());
}

namespace {

std::string space_text(const GSHSCENE_Color_Space & sp) {
  return std::to_string((int)sp.primaries) + ":" + std::to_string((int)sp.transfer) + ":" +
         hex16(gshgeom_f64_bits(sp.gamma));
}

std::string layer_depth_text(const GSHSCENE_Item * item) {
  return " layer=" + std::to_string(item->layer) + " depth=" + hex16(gshgeom_f64_bits(item->depth));
}

/** The dump line of an item, built from the fields the dump says it prints. */
std::string item_text(const GSHSCENE_Item * item) {
  std::string res = std::string(" res=") + gshscene_resource_kind_string(gshscene_resource_kind(item->resource)) +
                    ":" + (item->resource ? hex16(gshscene_resource_hash(item->resource)) : "");
  std::string map = " map=" + hex16(gshgeom_map_hash(&item->map));
  switch (item->kind) {
    case GSHSCENE_ITEM_FILL:
      return "F" + res + map + " rule=" + std::to_string((int)item->fill_rule) +
             " paint=" + std::to_string((int)item->paint.kind) + " " + hex16(gshgeom_f64_bits(item->paint.rgba[0])) +
             " " + hex16(gshgeom_f64_bits(item->paint.rgba[1])) + " " + hex16(gshgeom_f64_bits(item->paint.rgba[2])) +
             " " + hex16(gshgeom_f64_bits(item->paint.rgba[3])) + " space=" + space_text(item->paint.space) +
             " entry=" + std::to_string(item->paint.entry) +
             " gradient=" + hex16(item->paint.gradient ? gshscene_resource_hash(item->paint.gradient) : 0) +
             layer_depth_text(item);
    case GSHSCENE_ITEM_CLIP_PUSH:
      return "C" + res + map + " rule=" + std::to_string((int)item->fill_rule) + layer_depth_text(item);
    case GSHSCENE_ITEM_CLIP_POP:
      return "c";
    case GSHSCENE_ITEM_GROUP_PUSH:
      return "G opacity=" + hex16(gshgeom_f64_bits(item->group.opacity)) +
             " blend=" + space_text(item->group.blend_space) + " composite=" + std::to_string(item->group.composite) +
             " order=" + std::to_string(item->group.ordering) + layer_depth_text(item);
    case GSHSCENE_ITEM_GROUP_POP:
      return "g";
    default:
      return "I" + res + map + layer_depth_text(item);
  }
}

}  // namespace

TEST(Dump, ParsesBackToTheHashedFields) {
  StoreHolder s;
  Fixture f(s.store);
  for (const GSHSCENE_List * list : {f.list, f.order_list}) {
    size_t n = gshscene_list_count(list);
    size_t ties = gshscene_list_tie_count(list);
    std::vector<std::string> lines = lines_of(dump_list(list));
    ASSERT_EQ(lines.size(), 1 + n + ties);
    GSHSCENE_Color fg;
    ASSERT_EQ(gshscene_list_foreground(list, &fg), GSHSCENE_OK);
    const GSHSCENE_Resource * palette = gshscene_list_palette(list);
    EXPECT_EQ(lines[0], "gshscene.list 1 items=" + std::to_string(n) + " ties=" + std::to_string(ties) + " fg=" +
                            hex16(gshgeom_f64_bits(fg.rgba[0])) + " " + hex16(gshgeom_f64_bits(fg.rgba[1])) + " " +
                            hex16(gshgeom_f64_bits(fg.rgba[2])) + " " + hex16(gshgeom_f64_bits(fg.rgba[3])) + " " +
                            space_text(fg.space) + " palette=" + hex16(palette ? gshscene_resource_hash(palette) : 0) +
                            " hash=" + hex16(gshscene_list_hash(list)));
    for (size_t i = 0; i < n; i++) {
      const GSHSCENE_Item * item = nullptr;
      ASSERT_EQ(gshscene_list_item(list, i, &item), GSHSCENE_OK);
      EXPECT_EQ(lines[i + 1], item_text(item)) << "item " << i;
    }
    for (size_t i = 0; i < ties; i++) {
      const GSHSCENE_Tie * tie = nullptr;
      ASSERT_EQ(gshscene_list_tie(list, i, &tie), GSHSCENE_OK);
      EXPECT_EQ(lines[1 + n + i], "T first=" + std::to_string(tie->first) + " second=" + std::to_string(tie->second) +
                                      " depth=" + hex16(gshgeom_f64_bits(tie->depth)) +
                                      " reason=" + std::to_string((int)tie->reason));
    }
  }
}

TEST(Dump, ADumpOfAnOpenAndAnEmptyListIsWellFormed) {
  StoreHolder s;
  ListHolder l(s.store);
  EXPECT_EQ(dump_list(l.list), "gshscene.list 1 items=0 ties=0 fg=0000000000000000 0000000000000000 0000000000000000 "
                                   "3ff0000000000000 0:0:0000000000000000 palette=0000000000000000 hash=" +
                                   hex16(gshscene_list_hash(l.list)) + "\n");
  Ref sq(intern_square(s.store));
  ASSERT_EQ(gshscene_list_group_push(l.list, nullptr, 0, 0), GSHSCENE_OK);
  std::vector<std::string> lines = lines_of(dump_list(l.list));
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(lines[1].substr(0, 2), "G ");
  // An empty picture and an empty glyph run dump too.
  GSHSCENE_List * empty = nullptr;
  ASSERT_EQ(gshscene_list_create(nullptr, nullptr, s.store, &empty), GSHSCENE_OK);
  gshscene_list_close(empty);
  const GSHSCENE_Resource * pic = nullptr;
  ASSERT_EQ(gshscene_store_intern_picture(s.store, empty, &pic), GSHSCENE_OK);
  Ref pr(pic);
  gshscene_list_destroy(empty);
  std::vector<std::string> plines = lines_of(dump_resource(pic));
  ASSERT_EQ(plines.size(), 2u);
  EXPECT_EQ(plines[0].rfind("gshscene.resource 1 kind=picture hash=", 0), 0u);
}

namespace {

bool is_hex(const std::string & s) {
  if (s.empty()) {
    return false;
  }
  for (char c : s) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
      return false;
    }
  }
  return true;
}

/** Every hash and every double of a dump is exactly 16 hexadecimal digits, zeros included. */
void expect_sixteen_digits(const std::string & dump, const char * what) {
  static const char * const hex_names[] = {"hash", "map", "res", "opacity", "font", "size", "var", "outline", "x", "y", "depth", "fg"};
  std::istringstream lines(dump);
  std::string line;
  int checked = 0;
  while (std::getline(lines, line)) {
    std::istringstream tokens(line);
    std::string token;
    int index = 0;
    bool header = line.rfind("gsh", 0) == 0;
    while (tokens >> token) {
      if (index++ == 0 || (header && index == 2)) {
        continue;  // the tag of the line, and the format version of a header
      }
      std::string name, value = token;
      size_t eq = token.find('=');
      if (eq != std::string::npos) {
        name = token.substr(0, eq);
        value = token.substr(eq + 1);
        size_t colon = value.find(':');
        if (colon != std::string::npos) {
          value = value.substr(value.rfind(':') + 1);  // a resource's hash, or a space's gamma
        }
        bool hex = name == "space" || name == "blend";
        for (const char * n : hex_names) {
          hex = hex || name == n;
        }
        if (!hex) {
          continue;
        }
      } else if (token.find(':') != std::string::npos) {
        value = token.substr(token.rfind(':') + 1);  // the foreground's space
        name = "fg";
      }
      // a value with no name is one of a paint's four components or a coordinate: a double
      EXPECT_EQ(value.size(), 16u) << what << ": " << token << " in: " << line;
      EXPECT_TRUE(is_hex(value)) << what << ": " << token;
      checked++;
    }
  }
  EXPECT_GT(checked, 0) << what;
}

}  // namespace

TEST(Dump, EveryHashAndDoubleIsSixteenDigitsEvenWhenItStartsWithZeros) {
  StoreHolder s;
  // A map whose hash starts with a zero nibble.
  GSHGEOM_Map zero_map;
  int tries = 0;
  for (;; tries++) {
    ASSERT_LT(tries, 5000);
    zero_map = translate(0.5 * tries, 0.0);
    if ((gshgeom_map_hash(&zero_map) >> 60) == 0) {
      break;
    }
  }
  // A path resource whose hash starts with a zero nibble.
  const GSHSCENE_Resource * zero_path = nullptr;
  for (int i = 0;; i++) {
    ASSERT_LT(i, 5000);
    const GSHSCENE_Resource * r = intern_square(s.store, (double)i);
    if ((gshscene_resource_hash(r) >> 60) == 0) {
      zero_path = r;
      break;
    }
    gshscene_resource_release(r);
  }
  Ref path_ref(zero_path);
  expect_sixteen_digits(dump_resource(zero_path), "a path resource");
  // A list whose own hash starts with a zero nibble (the layer is varied until it does).
  ListHolder l(s.store);
  bool found = false;
  for (int layer = 0; layer < 5000 && !found; layer++) {
    ASSERT_EQ(gshscene_list_reset(l.list), GSHSCENE_OK);
    GSHSCENE_Group transparent = make_group(0.0);
    ASSERT_EQ(gshscene_list_group_push(l.list, &transparent, layer, 0), GSHSCENE_OK);
    GSHSCENE_Paint clear = solid(0.0, 0.0, 0.0, 0.0);
    ASSERT_EQ(gshscene_list_fill(l.list, zero_path, &zero_map, GSHGEOM_FILL_NONZERO, &clear, layer, 0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_group_pop(l.list), GSHSCENE_OK);
    found = (gshscene_list_hash(l.list) >> 60) == 0;
  }
  ASSERT_TRUE(found);
  ASSERT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
  std::string text = dump_list(l.list);
  expect_sixteen_digits(text, "a list");
  EXPECT_NE(text.find("opacity=0000000000000000"), std::string::npos) << text;
  // And a picture of it, and a run from a face whose key is 1.
  Ref picture;
  ASSERT_EQ(gshscene_store_intern_picture(s.store, l.list, &picture.resource), GSHSCENE_OK);
  expect_sixteen_digits(dump_resource(picture.get()), "a picture");
  GSHSCENE_Glyph_Run run;
  std::memset(&run, 0, sizeof run);
  Ref face(intern_face(s.store, 1, 0, 0, 1));
  run.source = GSHSCENE_GLYPHS_FROM_FONT;
  run.face = face.get();
  run.size = 1.0;
  run.hint_version = 1;
  GSHSCENE_Glyph g = {1, 0.0, 0.0, nullptr};
  run.glyph_count = 1;
  run.glyphs = &g;
  Ref run_ref;
  ASSERT_EQ(gshscene_store_intern_glyph_run(s.store, &run, &run_ref.resource), GSHSCENE_OK);
  expect_sixteen_digits(dump_resource(run_ref.get()), "a font run");
  EXPECT_NE(dump_resource(face.get()).find("font=0000000000000001 index=0 coords=2 0 1"), std::string::npos);
  EXPECT_NE(dump_resource(run_ref.get()).find("version=1 "), std::string::npos);
}

TEST(Dump, DoesNotDependOnTheLocale) {
  StoreHolder s;
  Fixture f(s.store);
  std::string before = dump_list(f.list);
  const char * old = std::setlocale(LC_ALL, nullptr);
  std::string saved = old ? old : "C";
  for (const char * name : {"de_DE.UTF-8", "fr_FR.UTF-8", "de_DE", "C.UTF-8"}) {
    if (std::setlocale(LC_ALL, name)) {
      EXPECT_EQ(dump_list(f.list), before) << name;
      EXPECT_EQ(dump_resource(f.font_run), known::kFontRunDump) << name;
    }
  }
  std::setlocale(LC_ALL, saved.c_str());
}

TEST(Dump, AFailingSinkIsReportedAndNullIsInvalid) {
  StoreHolder s;
  Fixture f(s.store);
  FILE * full = std::fopen("/dev/full", "w");
  if (full) {
    EXPECT_EQ(gshscene_list_dump(f.list, full), GSHSCENE_ERR_IO);
    EXPECT_EQ(gshscene_resource_dump(f.sq, full), GSHSCENE_ERR_IO);
    EXPECT_EQ(gshscene_resource_dump(f.font_run, full), GSHSCENE_ERR_IO);
    EXPECT_EQ(gshscene_resource_dump(f.picture, full), GSHSCENE_ERR_IO);
    std::fclose(full);
  }
  // A stream open only for reading refuses the first write.
  FILE * ro = std::fopen("/dev/null", "r");
  ASSERT_NE(ro, nullptr);
  EXPECT_EQ(gshscene_list_dump(f.list, ro), GSHSCENE_ERR_IO);
  EXPECT_EQ(gshscene_resource_dump(f.sq, ro), GSHSCENE_ERR_IO);
  EXPECT_EQ(gshscene_resource_dump(f.outline_run, ro), GSHSCENE_ERR_IO);
  std::fclose(ro);
  EXPECT_EQ(gshscene_list_dump(nullptr, stdout), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_dump(f.list, nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_resource_dump(nullptr, stdout), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_resource_dump(f.sq, nullptr), GSHSCENE_ERR_INVALID);
}

#ifdef __GLIBC__
namespace {

struct Cookie {
  long fail_at;  // the write call that fails; -1 never
  long writes;
};

ssize_t sink_write(void * cookie, const char *, size_t size) {
  Cookie * c = (Cookie *)cookie;
  long n = c->writes++;
  if (n == c->fail_at) {
    return -1;
  }
  return (ssize_t)size;
}

/** Run @p dump into an unbuffered stream whose @p fail_at-th write fails (once); report the result and the number of writes. */
template <typename Dump, typename Object>
GSHSCENE_Result dump_failing_at(Dump dump, const Object * object, long fail_at, long * writes, bool unbuffered) {
  Cookie cookie{fail_at, 0};
  cookie_io_functions_t functions = {nullptr, sink_write, nullptr, nullptr};
  FILE * f = fopencookie(&cookie, "w", functions);
  if (!f) {
    return GSHSCENE_ERR_INTERNAL;
  }
  if (unbuffered) {
    std::setvbuf(f, nullptr, _IONBF, 0);
  }
  GSHSCENE_Result result = (GSHSCENE_Result)dump(object, f);
  std::fclose(f);
  *writes = cookie.writes;
  return result;
}

/** Every write failing in turn must make the dump report ERR_IO; a write past the last one changes nothing.
 *  Unbuffered, every call of the dump is a write; buffered, the writes are the dump's flushes. */
template <typename Dump, typename Object>
void expect_every_write_checked(Dump dump, const Object * object, const char * what) {
  for (bool unbuffered : {true, false}) {
    long total = 0;
    ASSERT_EQ(dump_failing_at(dump, object, -1, &total, unbuffered), GSHSCENE_OK) << what;
    ASSERT_GT(total, 0) << what;
    for (long k = 0; k < total; k++) {
      long writes = 0;
      EXPECT_EQ(dump_failing_at(dump, object, k, &writes, unbuffered), GSHSCENE_ERR_IO)
          << what << (unbuffered ? " (unbuffered)" : " (buffered)") << ": write " << k << " of " << total
          << " failed unnoticed";
    }
    long writes = 0;
    EXPECT_EQ(dump_failing_at(dump, object, total, &writes, unbuffered), GSHSCENE_OK) << what;
  }
}

}  // namespace

TEST(Dump, EveryWriteOfEveryDumpIsChecked) {
  StoreHolder s;
  Fixture f(s.store);
  expect_every_write_checked(gshscene_list_dump, f.list, "list");
  expect_every_write_checked(gshscene_resource_dump, f.sq, "path resource");
  expect_every_write_checked(gshscene_resource_dump, f.font_run, "font run");
  expect_every_write_checked(gshscene_resource_dump, f.outline_run, "outline run");
  expect_every_write_checked(gshscene_resource_dump, f.picture, "picture");
  expect_every_write_checked(gshscene_resource_dump, f.palette, "palette");
  // A gradient of each kind (color-paint story P1): every line of its own and of its stops.
  GSHSCENE_Gradient_Stop stops[2] = {{0.0, solid(0, 0, 0, 1)}, {1.0, solid(1, 1, 1, 1)}};
  for (uint32_t kind : {GSHSCENE_GRADIENT_LINEAR, GSHSCENE_GRADIENT_RADIAL, GSHSCENE_GRADIENT_SWEEP}) {
    GSHSCENE_Gradient g{};
    g.kind = kind;
    gshgeom_map_identity(2, &g.map);
    g.p1[0] = kind == GSHSCENE_GRADIENT_LINEAR ? 1.0 : 0.0;
    g.r1 = kind == GSHSCENE_GRADIENT_RADIAL ? 1.0 : 0.0;
    g.a1 = kind == GSHSCENE_GRADIENT_SWEEP ? 1.0 : 0.0;
    g.stop_count = 2;
    g.stops = stops;
    const GSHSCENE_Resource * r = nullptr;
    ASSERT_EQ(gshscene_store_intern_gradient(s.store, &g, &r), GSHSCENE_OK);
    expect_every_write_checked(gshscene_resource_dump, r, "gradient");
    gshscene_resource_release(r);
  }
  // A stroke style with dashes (strokes story S1): its line and each dash's.
  const double dashes[2] = {1.0, 2.5};
  GSHSCENE_Stroke_Style st;
  gshscene_stroke_style_init(&st);
  st.dash_count = 2;
  st.dashes = dashes;
  const GSHSCENE_Resource * style = nullptr;
  ASSERT_EQ(gshscene_store_intern_stroke_style(s.store, &st, &style), GSHSCENE_OK);
  expect_every_write_checked(gshscene_resource_dump, style, "stroke style");
  gshscene_resource_release(style);
}
#endif

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
