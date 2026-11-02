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
 * The pattern (pattern.h, PT1 to PT14): the resource's rules in the order they are checked, its bounds at the bound and one
 * past it, equality, its hash and dump against the independent generator, the exact stepping of the lattice, what draws
 * nothing, the paint and its place in a list, the picture depth through a tile, a sink's disposition of a unit painted with
 * one and of the units in its tile, and the retained scene's diff and validation.
 */

#include "scene_fixtures.h"

#include "../alloc_tracker.h"
#include "../known_answers.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace scene;

namespace {

const double kInf = std::numeric_limits<double>::infinity();
const double kNaN = std::numeric_limits<double>::quiet_NaN();

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

/** A closed list of the given fills (a lambda fills it), frozen as a picture; the caller owns the reference. */
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

/** A store, the square, the fixture path and the fixture picture. */
struct World {
  StoreHolder s;
  explicit World(const GSHSCENE_Limits * limits = nullptr) : s(limits) {}
  Ref sq{intern_square(s.store)};
  Ref p2{intern_fixture(s.store)};
  Ref pic{fixture_picture(s.store, sq.get(), p2.get())};

  GSHSCENE_Pattern base(const GSHSCENE_Resource * tile = nullptr) const {
    GSHSCENE_Pattern p;
    gshscene_pattern_init(&p);
    p.tile = tile ? tile : pic.get();
    return p;
  }
  GSHSCENE_Result intern(const GSHSCENE_Pattern & p, Ref * out = nullptr) const {
    const GSHSCENE_Resource * r = nullptr;
    GSHSCENE_Result result = gshscene_store_intern_pattern(s.store, &p, &r);
    if (out) {
      *out = Ref(result == GSHSCENE_OK ? r : nullptr);
    } else if (result == GSHSCENE_OK) {
      gshscene_resource_release(r);
    }
    return result;
  }
};

/** The picture depth a list reports, read through the picture it freezes into (a picture is one deeper than its list); the list is closed already. */
size_t list_depth(GSHSCENE_Store * store, const GSHSCENE_List * list) {
  const GSHSCENE_Resource * picture = nullptr;
  if (gshscene_store_intern_picture(store, list, &picture) != GSHSCENE_OK) {
    return (size_t)-1;
  }
  size_t depth = gshscene_resource_picture_depth(picture) - 1;
  gshscene_resource_release(picture);
  return depth;
}

GSHSCENE_Paint pattern_paint(const GSHSCENE_Resource * pattern, double alpha = 1.0) {
  GSHSCENE_Paint p = solid(0, 0, 0, alpha);
  p.kind = GSHSCENE_PAINT_PATTERN;
  p.resource = pattern;
  return p;
}

GSHSCENE_Paint tint_of(GSHSCENE_Paint_Kind kind, double r, double g, double b, double a, uint32_t entry = 0) {
  GSHSCENE_Paint p = solid(r, g, b, a);
  p.kind = kind;
  p.entry = entry;
  return p;
}

}  // namespace

// --- the pinned values ------------------------------------------------------------------------------------------------

TEST(Pattern, ThePinnedValuesArePinned) {
  EXPECT_EQ((int)GSHSCENE_RESOURCE_PATTERN, 12);
  EXPECT_EQ((int)GSHSCENE_PAINT_PATTERN, 5);
  EXPECT_EQ((int)GSHSCENE_REASON_PATTERN, 36);
  EXPECT_EQ((int)GSHSCENE_REASON_PATTERN_DEGENERATE, 37);
  EXPECT_EQ((int)GSHSCENE_REASON_PATTERN_RESAMPLED, 38);
  EXPECT_EQ(GSHSCENE_CAP_PATTERN, 0x8000u);
  EXPECT_EQ(GSHSCENE_PATTERN_INDEX_MAX, INT64_C(9007199254740991));
}

TEST(Pattern, InitGivesTheDefaultsAndIgnoresNull) {
  gshscene_pattern_init(nullptr);
  GSHSCENE_Pattern p;
  std::memset(&p, 0xAB, sizeof p);
  gshscene_pattern_init(&p);
  EXPECT_EQ(p.tile, nullptr);
  EXPECT_EQ(p.x, 0.0);
  EXPECT_EQ(p.y, 0.0);
  EXPECT_EQ(p.w, 1.0);
  EXPECT_EQ(p.h, 1.0);
  EXPECT_EQ(p.x_step, 1.0);
  EXPECT_EQ(p.y_step, 1.0);
  EXPECT_EQ(p.repeat_x, 1u);
  EXPECT_EQ(p.repeat_y, 1u);
  EXPECT_EQ(p.map.dim, 2);
  EXPECT_EQ(p.has_tint, 0u);
  EXPECT_EQ(p.tint.kind, GSHSCENE_PAINT_SOLID);
  EXPECT_EQ(p.tint.rgba[3], 0.0);
}

// --- PT3: hash and dump against the independent generator --------------------------------------------------------------

TEST(Pattern, HashAndDumpAreWhatTheIndependentGeneratorSays) {
  World w;
  ASSERT_EQ(gshscene_resource_hash(w.pic.get()), known::kPictureHash);
  GSHGEOM_Map design;
  gshgeom_map_design_to_model(2.5, 10, 20, &design);
  GSHGEOM_Map tr = translate(1.5, -2.25);
  GSHSCENE_Pattern p[5];
  for (auto & x : p) {
    x = w.base();
  }
  p[1].x = 1.5;
  p[1].y = -0.25;
  p[1].w = 3.0;
  p[1].h = 2.0;
  p[1].x_step = 4.5;
  p[1].y_step = 2.5;
  p[1].map = tr;
  p[1].has_tint = 1;
  p[1].tint = solid(0.25, 0.5, 0.75, 1.0);
  p[1].tint.space = space_of(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_SRGB);
  p[2].w = 2.0;
  p[2].h = 2.0;
  p[2].x_step = 1.0;
  p[2].y_step = 0.5;
  p[2].repeat_y = 0;
  p[2].map = design;
  p[2].has_tint = 1;
  p[2].tint = tint_of(GSHSCENE_PAINT_FOREGROUND, 0, 0, 0, 0.5);
  p[3].w = -0.0;  // -0 is stored as +0: the generator spells +0
  p[3].h = 3.0;
  p[3].x_step = 0.0;
  p[3].y_step = -0.0;
  p[3].repeat_x = 0;
  p[3].repeat_y = 0;
  p[3].has_tint = 1;
  p[3].tint = tint_of(GSHSCENE_PAINT_PALETTE, 0, 0, 0, 1.0, 2);
  p[4].x = std::ldexp(1.0, -64);
  p[4].y = std::ldexp(1.0, 64);
  p[4].w = 0.1;
  p[4].h = 0.3;
  p[4].x_step = 0.1;
  p[4].y_step = 0.3;
  p[4].repeat_x = 0;
  p[4].map = tr;
  const char * const dumps[5] = {known::kPatternDump0, known::kPatternDump1, known::kPatternDump2, known::kPatternDump3,
      known::kPatternDump4};
  std::vector<Ref> held(5);
  for (int i = 0; i < 5; i++) {
    SCOPED_TRACE(i);
    ASSERT_EQ(w.intern(p[i], &held[(size_t)i]), GSHSCENE_OK);
    EXPECT_EQ(gshscene_resource_kind(held[(size_t)i].get()), GSHSCENE_RESOURCE_PATTERN);
    EXPECT_EQ(gshscene_resource_hash(held[(size_t)i].get()), known::kPatternHashes[i]);
    EXPECT_EQ(dump_resource(held[(size_t)i].get()), dumps[i]);
  }
  // A list of fills with them, hashed and dumped as the generator spells it.
  Ref square(intern_square(w.s.store));
  Ref fixture(intern_fixture(w.s.store));
  ListHolder list(w.s.store);
  auto pattern_fill = [&](const GSHSCENE_Resource * path, const GSHGEOM_Map * map, GSHGEOM_Fill_Rule rule, double alpha,
                          const GSHSCENE_Resource * pattern, int32_t layer, double depth) {
    GSHSCENE_Paint paint = pattern_paint(pattern, alpha);
    GSHGEOM_Map identity;
    gshgeom_map_identity(2, &identity);
    return gshscene_list_fill(list.list, path, map ? map : &identity, rule, &paint, layer, depth);
  };
  ASSERT_EQ(pattern_fill(square.get(), nullptr, GSHGEOM_FILL_NONZERO, 1.0, held[0].get(), 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(pattern_fill(fixture.get(), &tr, GSHGEOM_FILL_EVEN_ODD, 0.5, held[1].get(), 2, 0.0), GSHSCENE_OK);
  ASSERT_EQ(pattern_fill(square.get(), &design, GSHGEOM_FILL_NONZERO, 0.25, held[2].get(), 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(pattern_fill(square.get(), nullptr, GSHGEOM_FILL_NONZERO, 0.0, held[0].get(), 0, 1.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(list.list), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_hash(list.list), known::kPatternListHash);
  EXPECT_EQ(dump_list(list.list), known::kPatternListDump);
}

TEST(Pattern, AReadBackPatternIsWhatWasStoredWithItsTileAndTintCopied) {
  World w;
  GSHSCENE_Pattern in = w.base();
  in.x = -0.0;
  in.w = 2.5;
  in.has_tint = 1;
  in.tint = solid(-0.0, 0.5, 1.0, 0.25);
  Ref r;
  ASSERT_EQ(w.intern(in, &r), GSHSCENE_OK);
  GSHSCENE_Pattern out;
  std::memset(&out, 0xAB, sizeof out);
  ASSERT_EQ(gshscene_resource_pattern(r.get(), &out), GSHSCENE_OK);
  EXPECT_EQ(out.tile, w.pic.get());
  EXPECT_EQ(std::signbit(out.x), false) << "-0 is stored as +0";
  EXPECT_EQ(std::signbit(out.tint.rgba[0]), false);
  EXPECT_EQ(out.w, 2.5);
  EXPECT_EQ(out.has_tint, 1u);
  EXPECT_EQ(out.tint.rgba[1], 0.5);
  // A colored pattern's tint reads back as the zero paint.
  Ref colored;
  ASSERT_EQ(w.intern(w.base(), &colored), GSHSCENE_OK);
  ASSERT_EQ(gshscene_resource_pattern(colored.get(), &out), GSHSCENE_OK);
  EXPECT_EQ(out.has_tint, 0u);
  EXPECT_EQ(out.tint.kind, GSHSCENE_PAINT_SOLID);
  for (double c : out.tint.rgba) {
    EXPECT_EQ(c, 0.0);
  }
  // A path, a picture and NULL are not patterns.
  EXPECT_EQ(gshscene_resource_pattern(w.sq.get(), &out), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_resource_pattern(w.pic.get(), &out), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_resource_pattern(nullptr, &out), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_resource_pattern(r.get(), nullptr), GSHSCENE_ERR_INVALID);
}

// --- PT2: the checks, in their order --------------------------------------------------------------------------------

TEST(Pattern, EachFieldIsCheckedAndInvalidComesBeforeUnsupportedBeforeLimit) {
  World w;
  GSHSCENE_Store * store = w.s.store;
  EXPECT_EQ(gshscene_store_intern_pattern(nullptr, nullptr, nullptr), GSHSCENE_ERR_INVALID);
  const GSHSCENE_Resource * out = nullptr;
  EXPECT_EQ(gshscene_store_intern_pattern(store, nullptr, &out), GSHSCENE_ERR_INVALID);
  GSHSCENE_Pattern ok = w.base();
  EXPECT_EQ(gshscene_store_intern_pattern(store, &ok, nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_store_intern_pattern(nullptr, &ok, &out), GSHSCENE_ERR_INVALID);

  struct Case {
    const char * name;
    void (*mutate)(GSHSCENE_Pattern &, const World &);
    GSHSCENE_Result want;
  };
  const double huge = std::ldexp(1.0, 65);
  (void)huge;
  const Case cases[] = {
      {"no tile", [](GSHSCENE_Pattern & p, const World &) { p.tile = nullptr; }, GSHSCENE_ERR_INVALID},
      {"a path for a tile", [](GSHSCENE_Pattern & p, const World & w) { p.tile = w.sq.get(); }, GSHSCENE_ERR_INVALID},
      {"NaN x", [](GSHSCENE_Pattern & p, const World &) { p.x = kNaN; }, GSHSCENE_ERR_INVALID},
      {"infinite y", [](GSHSCENE_Pattern & p, const World &) { p.y = kInf; }, GSHSCENE_ERR_INVALID},
      {"NaN w", [](GSHSCENE_Pattern & p, const World &) { p.w = kNaN; }, GSHSCENE_ERR_INVALID},
      {"infinite h", [](GSHSCENE_Pattern & p, const World &) { p.h = -kInf; }, GSHSCENE_ERR_INVALID},
      {"NaN x step", [](GSHSCENE_Pattern & p, const World &) { p.x_step = kNaN; }, GSHSCENE_ERR_INVALID},
      {"infinite y step", [](GSHSCENE_Pattern & p, const World &) { p.y_step = kInf; }, GSHSCENE_ERR_INVALID},
      {"negative w", [](GSHSCENE_Pattern & p, const World &) { p.w = -1.0; }, GSHSCENE_ERR_INVALID},
      {"negative h", [](GSHSCENE_Pattern & p, const World &) { p.h = -0.5; }, GSHSCENE_ERR_INVALID},
      {"negative x step", [](GSHSCENE_Pattern & p, const World &) { p.x_step = -1.0; }, GSHSCENE_ERR_INVALID},
      {"negative y step", [](GSHSCENE_Pattern & p, const World &) { p.y_step = -4.0; }, GSHSCENE_ERR_INVALID},
      {"repeat x 2", [](GSHSCENE_Pattern & p, const World &) { p.repeat_x = 2; }, GSHSCENE_ERR_INVALID},
      {"repeat y 2", [](GSHSCENE_Pattern & p, const World &) { p.repeat_y = 7; }, GSHSCENE_ERR_INVALID},
      {"has_tint 2", [](GSHSCENE_Pattern & p, const World &) { p.has_tint = 2; }, GSHSCENE_ERR_INVALID},
      {"a zero-filled map", [](GSHSCENE_Pattern & p, const World &) { std::memset(&p.map, 0, sizeof p.map); }, GSHSCENE_ERR_INVALID},
      {"a map entry that is NaN", [](GSHSCENE_Pattern & p, const World &) { p.map.m[0][0] = kNaN; }, GSHSCENE_ERR_INVALID},
      {"a tint beside has_tint 0",
          [](GSHSCENE_Pattern & p, const World &) { p.tint = solid(0, 0, 0, 1.0); }, GSHSCENE_ERR_INVALID},
      {"a foreground tint beside has_tint 0",
          [](GSHSCENE_Pattern & p, const World &) { p.tint.kind = GSHSCENE_PAINT_FOREGROUND; }, GSHSCENE_ERR_INVALID},
      {"has_tint 1 and the zero tint is a transparent black solid, which is valid",
          [](GSHSCENE_Pattern & p, const World &) { p.has_tint = 1; }, GSHSCENE_OK},
      {"a gradient as the tint",
          [](GSHSCENE_Pattern & p, const World &) { p.has_tint = 1; p.tint.kind = GSHSCENE_PAINT_GRADIENT; }, GSHSCENE_ERR_INVALID},
      {"a pattern as the tint",
          [](GSHSCENE_Pattern & p, const World &) { p.has_tint = 1; p.tint.kind = GSHSCENE_PAINT_PATTERN; }, GSHSCENE_ERR_INVALID},
      {"an invalid tint alpha",
          [](GSHSCENE_Pattern & p, const World &) { p.has_tint = 1; p.tint = solid(0, 0, 0, 2.0); }, GSHSCENE_ERR_INVALID},
      {"a palette tint with an entry past the largest",
          [](GSHSCENE_Pattern & p, const World &) { p.has_tint = 1; p.tint = tint_of(GSHSCENE_PAINT_PALETTE, 0, 0, 0, 1, 0xFFFF); },
          GSHSCENE_ERR_INVALID},
      {"a 3D map", [](GSHSCENE_Pattern & p, const World &) { gshgeom_map_identity(3, &p.map); }, GSHSCENE_ERR_UNSUPPORTED},
      // The bounds, at the bound and one past it.
      {"x at 2^64", [](GSHSCENE_Pattern & p, const World &) { p.x = std::ldexp(1.0, 64); }, GSHSCENE_OK},
      {"x past 2^64", [](GSHSCENE_Pattern & p, const World &) { p.x = std::nextafter(std::ldexp(1.0, 64), kInf); }, GSHSCENE_ERR_LIMIT},
      {"x at -2^64", [](GSHSCENE_Pattern & p, const World &) { p.x = -std::ldexp(1.0, 64); }, GSHSCENE_OK},
      {"x past -2^64", [](GSHSCENE_Pattern & p, const World &) { p.x = -std::nextafter(std::ldexp(1.0, 64), kInf); }, GSHSCENE_ERR_LIMIT},
      {"y past 2^64", [](GSHSCENE_Pattern & p, const World &) { p.y = std::nextafter(std::ldexp(1.0, 64), kInf); }, GSHSCENE_ERR_LIMIT},
      {"w at 2^64", [](GSHSCENE_Pattern & p, const World &) { p.w = std::ldexp(1.0, 64); }, GSHSCENE_OK},
      {"w past 2^64", [](GSHSCENE_Pattern & p, const World &) { p.w = std::nextafter(std::ldexp(1.0, 64), kInf); }, GSHSCENE_ERR_LIMIT},
      {"h past 2^64", [](GSHSCENE_Pattern & p, const World &) { p.h = std::nextafter(std::ldexp(1.0, 64), kInf); }, GSHSCENE_ERR_LIMIT},
      {"x step past 2^64", [](GSHSCENE_Pattern & p, const World &) { p.x_step = std::nextafter(std::ldexp(1.0, 64), kInf); }, GSHSCENE_ERR_LIMIT},
      {"y step past 2^64", [](GSHSCENE_Pattern & p, const World &) { p.y_step = std::nextafter(std::ldexp(1.0, 64), kInf); }, GSHSCENE_ERR_LIMIT},
      {"w at 2^-64", [](GSHSCENE_Pattern & p, const World &) { p.w = std::ldexp(1.0, -64); }, GSHSCENE_OK},
      {"w below 2^-64", [](GSHSCENE_Pattern & p, const World &) { p.w = std::nextafter(std::ldexp(1.0, -64), 0.0); }, GSHSCENE_ERR_LIMIT},
      {"x below 2^-64 in magnitude", [](GSHSCENE_Pattern & p, const World &) { p.x = std::ldexp(1.0, -65); }, GSHSCENE_ERR_LIMIT},
      {"a negative x below 2^-64 in magnitude", [](GSHSCENE_Pattern & p, const World &) { p.x = -std::ldexp(1.0, -65); }, GSHSCENE_ERR_LIMIT},
      {"w zero", [](GSHSCENE_Pattern & p, const World &) { p.w = 0.0; }, GSHSCENE_OK},
      {"h zero", [](GSHSCENE_Pattern & p, const World &) { p.h = 0.0; }, GSHSCENE_OK},
      {"x step zero", [](GSHSCENE_Pattern & p, const World &) { p.x_step = 0.0; }, GSHSCENE_OK},
      {"y step zero", [](GSHSCENE_Pattern & p, const World &) { p.y_step = 0.0; }, GSHSCENE_OK},
      {"a map entry at 2^64", [](GSHSCENE_Pattern & p, const World &) { p.map.m[0][1] = std::ldexp(1.0, 64); }, GSHSCENE_OK},
      {"a map entry past 2^64", [](GSHSCENE_Pattern & p, const World &) { p.map.m[1][0] = std::nextafter(std::ldexp(1.0, 64), kInf); }, GSHSCENE_ERR_LIMIT},
      {"a translation past 2^64", [](GSHSCENE_Pattern & p, const World &) { p.map.t[0] = std::nextafter(std::ldexp(1.0, 64), kInf); }, GSHSCENE_ERR_LIMIT},
      {"a translation below 2^-64", [](GSHSCENE_Pattern & p, const World &) { p.map.t[1] = std::ldexp(1.0, -70); }, GSHSCENE_ERR_LIMIT},
      {"a solid tint component at 2^16",
          [](GSHSCENE_Pattern & p, const World &) { p.has_tint = 1; p.tint = solid(65536.0, 0, 0, 1); }, GSHSCENE_OK},
      {"a solid tint component past 2^16",
          [](GSHSCENE_Pattern & p, const World &) { p.has_tint = 1; p.tint = solid(std::nextafter(65536.0, kInf), 0, 0, 1); },
          GSHSCENE_ERR_LIMIT},
      // The order: an invalid field beside a bound that is past is invalid; a 3D map beside one is unsupported.
      {"a negative step beside a value past its bound",
          [](GSHSCENE_Pattern & p, const World &) { p.x_step = -1.0; p.x = std::ldexp(1.0, 70); }, GSHSCENE_ERR_INVALID},
      {"a tint beside has_tint 0 beside a value past its bound",
          [](GSHSCENE_Pattern & p, const World &) { p.tint = solid(0, 0, 0, 1.0); p.w = std::ldexp(1.0, 70); }, GSHSCENE_ERR_INVALID},
      {"a 3D map beside a value past its bound",
          [](GSHSCENE_Pattern & p, const World &) { gshgeom_map_identity(3, &p.map); p.w = std::ldexp(1.0, 70); }, GSHSCENE_ERR_UNSUPPORTED},
      {"a 3D map beside an invalid field",
          [](GSHSCENE_Pattern & p, const World &) { gshgeom_map_identity(3, &p.map); p.h = -1.0; }, GSHSCENE_ERR_INVALID},
      {"a reserved tint space beside a value past its bound is unsupported",
          [](GSHSCENE_Pattern & p, const World &) {
            p.has_tint = 1;
            p.tint = solid(0, 0, 0, 1);
            p.tint.space = space_of(GCOL_PRIMARIES_PROPHOTO, GCOL_TRANSFER_SRGB);
            p.w = std::ldexp(1.0, 70);
          },
          GSHSCENE_ERR_UNSUPPORTED},
  };
  for (const Case & c : cases) {
    GSHSCENE_Pattern p = w.base();
    c.mutate(p, w);
    EXPECT_EQ(w.intern(p), c.want) << c.name;
  }
  // A tile of another store is not a tile.
  World other;
  GSHSCENE_Pattern foreign = w.base(other.pic.get());
  EXPECT_EQ(w.intern(foreign), GSHSCENE_ERR_INVALID);
  // Nothing was left behind by the refusals.
  EXPECT_EQ(gshscene_resource_refcount(w.pic.get()), 1u);
}

TEST(Pattern, ATileTooDeepToBeNestedOnceMoreIsALimitAtInternAndNotBefore) {
  GSHSCENE_Limits limits;
  gshscene_limits_default(&limits);
  limits.max_picture_depth = 3;
  World w(&limits);
  // The fixture picture has depth 1; a pattern over it is depth 1 and may be nested twice more.
  ASSERT_EQ(gshscene_resource_picture_depth(w.pic.get()), 1u);
  Ref p0;
  ASSERT_EQ(w.intern(w.base(), &p0), GSHSCENE_OK);
  Ref t1(make_picture(w.s.store, [&](GSHSCENE_List * l) {
    GSHSCENE_Paint paint = pattern_paint(p0.get());
    EXPECT_EQ(gshscene_list_fill(l, w.sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &paint, 0, 0), GSHSCENE_OK);
  }));
  ASSERT_NE(t1.get(), nullptr);
  EXPECT_EQ(gshscene_resource_picture_depth(t1.get()), 2u) << "a tile that names a pattern is one deeper";
  Ref p1;
  ASSERT_EQ(w.intern(w.base(t1.get()), &p1), GSHSCENE_OK) << "2 + 1 is at the limit";
  Ref t2(make_picture(w.s.store, [&](GSHSCENE_List * l) {
    GSHSCENE_Paint paint = pattern_paint(p1.get());
    EXPECT_EQ(gshscene_list_fill(l, w.sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &paint, 0, 0), GSHSCENE_OK);
  }));
  ASSERT_NE(t2.get(), nullptr);
  EXPECT_EQ(gshscene_resource_picture_depth(t2.get()), 3u);
  EXPECT_EQ(w.intern(w.base(t2.get())), GSHSCENE_ERR_LIMIT) << "3 + 1 is one past it";
  // The list that names the deepest pattern cannot be frozen into a picture past the limit either: a fourth level fails.
  GSHSCENE_List * l = nullptr;
  ASSERT_EQ(gshscene_list_create(nullptr, nullptr, w.s.store, &l), GSHSCENE_OK);
  GSHSCENE_Paint paint = pattern_paint(p1.get());
  EXPECT_EQ(gshscene_list_fill(l, w.sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &paint, 0, 0), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  EXPECT_EQ(list_depth(w.s.store, l), 2u) << "a pattern's depth is its tile's picture depth";
  gshscene_list_destroy(l);
}

// --- PT3: equality is content -------------------------------------------------------------------------------------

TEST(Pattern, EqualContentIsOneHandleAndNearlyEqualContentIsNot) {
  World w;
  GSHSCENE_Pattern a = w.base();
  a.x = 0.5;
  a.y = 0.75;
  a.map = translate(3.0, 4.0);
  Ref r1, r2;
  ASSERT_EQ(w.intern(a, &r1), GSHSCENE_OK);
  ASSERT_EQ(w.intern(a, &r2), GSHSCENE_OK);
  EXPECT_EQ(r1.get(), r2.get());
  EXPECT_EQ(gshscene_resource_id(r1.get()), gshscene_resource_id(r2.get()));
  struct Variant {
    const char * name;
    void (*mutate)(GSHSCENE_Pattern &, const World &);
  };
  const Variant variants[] = {
      {"x one ulp", [](GSHSCENE_Pattern & p, const World &) { p.x = std::nextafter(p.x, kInf); }},
      {"y", [](GSHSCENE_Pattern & p, const World &) { p.y = std::nextafter(p.y, kInf); }},
      {"w one ulp", [](GSHSCENE_Pattern & p, const World &) { p.w = std::nextafter(p.w, 0.0); }},
      {"h one ulp", [](GSHSCENE_Pattern & p, const World &) { p.h = std::nextafter(p.h, kInf); }},
      {"x step one ulp", [](GSHSCENE_Pattern & p, const World &) { p.x_step = std::nextafter(p.x_step, kInf); }},
      {"y step one ulp", [](GSHSCENE_Pattern & p, const World &) { p.y_step = std::nextafter(p.y_step, 0.0); }},
      {"repeat x", [](GSHSCENE_Pattern & p, const World &) { p.repeat_x = 0; }},
      {"repeat y", [](GSHSCENE_Pattern & p, const World &) { p.repeat_y = 0; }},
      {"a map entry one ulp", [](GSHSCENE_Pattern & p, const World &) { p.map.m[0][0] = std::nextafter(1.0, 2.0); }},
      {"the translation", [](GSHSCENE_Pattern & p, const World &) { p.map.t[1] = std::nextafter(p.map.t[1], kInf); }},
      {"a tint", [](GSHSCENE_Pattern & p, const World &) { p.has_tint = 1; p.tint = solid(0, 0, 0, 1); }},
      {"a tile", [](GSHSCENE_Pattern & p, const World & w) { p.tile = w.sq.get(); }},
  };
  for (const Variant & v : variants) {
    GSHSCENE_Pattern b = a;
    if (std::string(v.name) == "a tile") {
      // Another picture of the same store.
      Ref other(make_picture(w.s.store, [&](GSHSCENE_List * l) {
        EXPECT_EQ(gshscene_list_fill(l, w.sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 7, 0), GSHSCENE_OK);
      }));
      b.tile = other.get();
      Ref r;
      ASSERT_EQ(w.intern(b, &r), GSHSCENE_OK);
      EXPECT_NE(r.get(), r1.get()) << v.name;
      continue;
    }
    v.mutate(b, w);
    Ref r;
    ASSERT_EQ(w.intern(b, &r), GSHSCENE_OK) << v.name;
    EXPECT_NE(r.get(), r1.get()) << v.name;
    EXPECT_NE(gshscene_resource_hash(r.get()), gshscene_resource_hash(r1.get())) << v.name;
  }
  // A tint that differs in its space only is another pattern; -0 and +0 are one.
  GSHSCENE_Pattern t1 = w.base();
  t1.has_tint = 1;
  t1.tint = solid(0.25, 0.5, 0.75, 1.0);
  GSHSCENE_Pattern t2 = t1;
  t2.tint.space = space_of(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_SRGB);
  Ref a1, a2;
  ASSERT_EQ(w.intern(t1, &a1), GSHSCENE_OK);
  ASSERT_EQ(w.intern(t2, &a2), GSHSCENE_OK);
  EXPECT_NE(a1.get(), a2.get());
  GSHSCENE_Pattern z1 = w.base();
  GSHSCENE_Pattern z2 = z1;
  z1.x = 0.0;
  z2.x = -0.0;
  z2.map.t[0] = -0.0;
  Ref b1, b2;
  ASSERT_EQ(w.intern(z1, &b1), GSHSCENE_OK);
  ASSERT_EQ(w.intern(z2, &b2), GSHSCENE_OK);
  EXPECT_EQ(b1.get(), b2.get());
  // A hash never decides: every pattern in one bucket still tells equal from near.
  StoreHolder shared(nullptr, nullptr, all_zero);
  Ref sq(intern_square(shared.store));
  Ref pic(make_picture(shared.store, [&](GSHSCENE_List * l) {
    EXPECT_EQ(gshscene_list_fill(l, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0), GSHSCENE_OK);
  }));
  GSHSCENE_Pattern p;
  gshscene_pattern_init(&p);
  p.tile = pic.get();
  Ref x1, x2, x3;
  ASSERT_EQ(gshscene_store_intern_pattern(shared.store, &p, (const GSHSCENE_Resource **)&x1.resource), GSHSCENE_OK);
  p.x_step = std::nextafter(1.0, 2.0);
  ASSERT_EQ(gshscene_store_intern_pattern(shared.store, &p, (const GSHSCENE_Resource **)&x2.resource), GSHSCENE_OK);
  p.x_step = 1.0;
  ASSERT_EQ(gshscene_store_intern_pattern(shared.store, &p, (const GSHSCENE_Resource **)&x3.resource), GSHSCENE_OK);
  EXPECT_NE(x1.get(), x2.get());
  EXPECT_EQ(x1.get(), x3.get());
}

TEST(Pattern, ThePatternRetainsItsTileUntilItIsReleased) {
  World w;
  const GSHSCENE_Resource * tile = w.pic.get();
  size_t before = gshscene_resource_refcount(tile);
  {
    Ref r;
    ASSERT_EQ(w.intern(w.base(), &r), GSHSCENE_OK);
    EXPECT_EQ(gshscene_resource_refcount(tile), before + 1);
    Ref again;
    ASSERT_EQ(w.intern(w.base(), &again), GSHSCENE_OK);
    EXPECT_EQ(gshscene_resource_refcount(tile), before + 1) << "one pattern, one reference";
  }
  EXPECT_EQ(gshscene_resource_refcount(tile), before);
}

TEST(Pattern, EveryAllocationOfAnInternCanFail) {
  // The store's allocator fails the n-th call: the intern reports OOM, leaks nothing, and does not retain the tile.
  for (long fail = 0; fail < 64; fail++) {
    tracker::Tracker t;
    GSHSCENE_Allocator alloc = tracker::allocator_for(&t);
    GSHSCENE_Store * store = nullptr;
    ASSERT_EQ(gshscene_store_create_with_index_hash(&alloc, nullptr, nullptr, nullptr, &store), GSHSCENE_OK);
    Ref sq(intern_square(store));
    Ref pic(make_picture(store, [&](GSHSCENE_List * l) {
      EXPECT_EQ(gshscene_list_fill(l, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0), GSHSCENE_OK);
    }));
    ASSERT_NE(pic.get(), nullptr);
    size_t refs = gshscene_resource_refcount(pic.get());
    GSHSCENE_Pattern p;
    gshscene_pattern_init(&p);
    p.tile = pic.get();
    p.has_tint = 1;
    p.tint = solid(1, 0, 0, 1);
    t.calls = 0;
    t.fail_from = fail;
    const GSHSCENE_Resource * r = nullptr;
    GSHSCENE_Result result = gshscene_store_intern_pattern(store, &p, &r);
    t.fail_from = -1;
    if (result == GSHSCENE_OK) {
      EXPECT_FALSE(t.failed) << fail;
      gshscene_resource_release(r);
    } else {
      EXPECT_EQ(result, GSHSCENE_ERR_OOM) << fail;
      EXPECT_TRUE(t.failed) << fail;
      EXPECT_EQ(gshscene_resource_refcount(pic.get()), refs) << fail;
    }
    sq = Ref();
    pic = Ref();
    gshscene_store_destroy(store);
    EXPECT_TRUE(t.live.empty()) << fail;
    if (result == GSHSCENE_OK) {
      break;
    }
  }
}

// --- PT5: the lattice and exact stepping --------------------------------------------------------------------------

TEST(PatternLattice, TheCellOfACopyIsTheStepTimesItsIndexAndNothingAccumulates) {
  World w;
  GSHSCENE_Pattern p = w.base();
  p.x = 0.25;
  p.y = -0.5;
  p.w = 0.07;
  p.h = 0.2;
  p.x_step = 0.1;
  p.y_step = 0.3;
  double cell[4];
  ASSERT_EQ(gshscene_pattern_cell(&p, 0, 0, cell), GSHSCENE_OK);
  EXPECT_EQ(cell[0], 0.25);
  EXPECT_EQ(cell[1], -0.5);
  EXPECT_EQ(cell[2], 0.25 + 0.07);
  EXPECT_EQ(cell[3], -0.5 + 0.2);
  // Copy 10^6 at step 0.1 is one product and one sum: within one rounding of the product and one of the sum of the exact
  // position, and not the 10^6 roundings of adding the step that many times.
  ASSERT_EQ(gshscene_pattern_cell(&p, 1000000, -1000000, cell), GSHSCENE_OK);
  EXPECT_EQ(cell[0], 0.25 + 1000000.0 * 0.1);
  EXPECT_EQ(cell[1], -0.5 + -1000000.0 * 0.3);
  double accumulated = 0.25;
  for (int i = 0; i < 1000000; i++) {
    accumulated += 0.1;
  }
  EXPECT_NE(accumulated, cell[0]) << "the loop is wrong by many ulps; the product is not";
  // The exact position of copy 10^6 (0.1 is not 1/10, but 3602879701896397 / 2^55; the oracle computes the sum in rationals):
  // 0x1.86a04p+16, and the double is within two ulps of it (one rounding of the product, one of the sum).
  EXPECT_LE(std::fabs(cell[0] - 0x1.86a04p+16), 2 * std::ldexp(1.0, 16 - 52));
  EXPECT_LE(std::fabs(accumulated - 0x1.86a04p+16), 1.0) << "the loop is far off";
  EXPECT_GT(std::fabs(accumulated - 0x1.86a04p+16), 1e-8) << "by many ulps";
  EXPECT_LE(std::fabs(cell[1] - -0x1.24f82p+18), 2 * std::ldexp(1.0, 18 - 52));
  // The index is bounded by an exact double.
  EXPECT_EQ(gshscene_pattern_cell(&p, GSHSCENE_PATTERN_INDEX_MAX + 1, 0, cell), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_pattern_cell(&p, 0, -GSHSCENE_PATTERN_INDEX_MAX - 1, cell), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_pattern_cell(&p, GSHSCENE_PATTERN_INDEX_MAX, -GSHSCENE_PATTERN_INDEX_MAX, cell), GSHSCENE_OK);
  EXPECT_EQ(gshscene_pattern_cell(nullptr, 0, 0, cell), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_pattern_cell(&p, 0, 0, nullptr), GSHSCENE_ERR_INVALID);
}

TEST(PatternLattice, AbuttingCellsNameTheSameDoubleForTheirSharedEdge) {
  World w;
  for (double width : {0.1, 0.3, 1.0 / 3.0, 0.7, 1e-3, 123.456}) {
    GSHSCENE_Pattern p = w.base();
    p.w = width;
    p.h = width;
    p.x_step = width;
    p.y_step = width;
    for (int64_t i : {-1000001LL, -3LL, -1LL, 0LL, 1LL, 2LL, 7LL, 999999LL}) {
      double a[4], b[4];
      ASSERT_EQ(gshscene_pattern_cell(&p, i, i, a), GSHSCENE_OK);
      ASSERT_EQ(gshscene_pattern_cell(&p, i + 1, i + 1, b), GSHSCENE_OK);
      EXPECT_EQ(a[2], b[0]) << width << " " << i << ": the right edge of the cell is the left edge of the next";
      EXPECT_EQ(a[3], b[1]) << width << " " << i;
    }
  }
  // Formed as left + w the edges differ for these widths (the defect this rule exists to prevent).
  int differ = 0;
  for (double width : {0.1, 0.3, 1.0 / 3.0}) {
    for (int i = 1; i < 100; i++) {
      double left = (double)i * width;
      double next_left = (double)(i + 1) * width;
      if (left + width != next_left) {
        differ++;
      }
    }
  }
  EXPECT_GT(differ, 0);
  // A step that is not the width gives the cell its own width past the left edge.
  GSHSCENE_Pattern g = w.base();
  g.w = 0.3;
  g.x_step = 0.5;
  double c[4];
  ASSERT_EQ(gshscene_pattern_cell(&g, 3, 0, c), GSHSCENE_OK);
  EXPECT_EQ(c[0], 0.0 + 3.0 * 0.5);
  EXPECT_EQ(c[2], c[0] + 0.3);
}

TEST(PatternLattice, OverlapIsDecidedOnTheAxesThatRepeat) {
  World w;
  GSHSCENE_Pattern p = w.base();
  EXPECT_EQ(gshscene_pattern_overlaps(&p), 0) << "abutting";
  p.x_step = 2.0;
  EXPECT_EQ(gshscene_pattern_overlaps(&p), 0) << "a gap";
  p.x_step = std::nextafter(1.0, 0.0);
  EXPECT_EQ(gshscene_pattern_overlaps(&p), 1) << "one ulp below the width: the change of rule is the definition";
  p.repeat_x = 0;
  EXPECT_EQ(gshscene_pattern_overlaps(&p), 0) << "a step on an axis that does not repeat is not used";
  p.repeat_y = 1;
  p.y_step = 0.5;
  EXPECT_EQ(gshscene_pattern_overlaps(&p), 1);
  p.repeat_y = 0;
  EXPECT_EQ(gshscene_pattern_overlaps(&p), 0);
  EXPECT_EQ(gshscene_pattern_overlaps(nullptr), 0);
}

// --- PT10: what draws nothing ------------------------------------------------------------------------------------

namespace {

/** A closed list of one item painted with @p pattern: a fill, or a stroke. */
struct Unit {
  ListHolder list;
  Ref style;
  const GSHSCENE_Item * item = nullptr;
  Unit(GSHSCENE_Store * store, const GSHSCENE_Resource * path, const GSHGEOM_Map * item_map, const GSHSCENE_Resource * pattern,
      double alpha = 1.0, bool stroke = false)
      : list(store) {
    GSHSCENE_Paint p = pattern_paint(pattern, alpha);
    if (stroke) {
      GSHSCENE_Stroke_Style st;
      gshscene_stroke_style_init(&st);
      const GSHSCENE_Resource * s = nullptr;
      EXPECT_EQ(gshscene_store_intern_stroke_style(store, &st, &s), GSHSCENE_OK);
      style = Ref(s);
      EXPECT_EQ(gshscene_list_stroke(list.list, path, style.get(), item_map, &p, 0, 0.0), GSHSCENE_OK);
    } else {
      EXPECT_EQ(gshscene_list_fill(list.list, path, item_map, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK);
    }
    EXPECT_EQ(gshscene_list_close(list.list), GSHSCENE_OK);
    EXPECT_EQ(gshscene_list_item(list.list, 0, &item), GSHSCENE_OK);
  }
};

GSHGEOM_Map scale_map(double sx, double sy) {
  GSHGEOM_Map m;
  gshgeom_map_scale(2, sx, sy, 1.0, &m);
  return m;
}

}  // namespace

TEST(PatternNotice, EachMapOfTheChainIsTestedOnItsOwnDoubles) {
  World w;
  Ref pat;
  ASSERT_EQ(w.intern(w.base(), &pat), GSHSCENE_OK);
  GSHGEOM_Map singular = scale_map(0.0, 1.0);
  GSHGEOM_Map fine = scale_map(2.0, 3.0);
  GSHGEOM_Map rank_one = scale_map(1.0, 1.0);
  rank_one.m[1][0] = 1.0;  // [1 0; 1 1] is fine; make the rows dependent
  rank_one.m[1][1] = 0.0;
  rank_one.m[0][1] = 0.0;  // [1 0; 1 0]: determinant exactly zero
  for (bool stroke : {false, true}) {
    SCOPED_TRACE(stroke);
    Unit u(w.s.store, w.sq.get(), &fine, pat.get(), 1.0, stroke);
    auto notice = [&](const GSHGEOM_Map * inst, size_t n, const GSHGEOM_Map * target) {
      return gshscene_pattern_notice(u.item, inst, n, target);
    };
    EXPECT_EQ(notice(nullptr, 0, nullptr), (GSHSCENE_Reason)0);
    EXPECT_EQ(notice(&fine, 1, &fine), (GSHSCENE_Reason)0);
    EXPECT_EQ(notice(nullptr, 0, &singular), GSHSCENE_REASON_MAP_NOT_INVERTIBLE) << "the target's";
    EXPECT_EQ(notice(&rank_one, 1, nullptr), GSHSCENE_REASON_MAP_NOT_INVERTIBLE) << "an instance's";
    GSHGEOM_Map two[2] = {fine, singular};
    EXPECT_EQ(notice(two, 2, nullptr), GSHSCENE_REASON_MAP_NOT_INVERTIBLE) << "the second instance's";
    Unit item_singular(w.s.store, w.sq.get(), &singular, pat.get(), 1.0, stroke);
    EXPECT_EQ(gshscene_pattern_notice(item_singular.item, nullptr, 0, nullptr), GSHSCENE_REASON_MAP_NOT_INVERTIBLE) << "the item's";
    GSHSCENE_Pattern sp = w.base();
    sp.map = singular;
    Ref spat;
    ASSERT_EQ(w.intern(sp, &spat), GSHSCENE_OK);
    Unit paint_singular(w.s.store, w.sq.get(), &fine, spat.get(), 1.0, stroke);
    EXPECT_EQ(gshscene_pattern_notice(paint_singular.item, nullptr, 0, nullptr), GSHSCENE_REASON_MAP_NOT_INVERTIBLE) << "the pattern's";
    // Degenerate: a size or a step of zero, after the maps.
    for (int field = 0; field < 4; field++) {
      GSHSCENE_Pattern d = w.base();
      (field == 0 ? d.w : field == 1 ? d.h : field == 2 ? d.x_step : d.y_step) = 0.0;
      Ref dr;
      ASSERT_EQ(w.intern(d, &dr), GSHSCENE_OK) << "zero is never refused at record";
      Unit du(w.s.store, w.sq.get(), &fine, dr.get(), 1.0, stroke);
      EXPECT_EQ(gshscene_pattern_notice(du.item, nullptr, 0, nullptr), GSHSCENE_REASON_PATTERN_DEGENERATE) << field;
      EXPECT_EQ(gshscene_pattern_notice(du.item, nullptr, 0, &singular), GSHSCENE_REASON_MAP_NOT_INVERTIBLE)
          << field << ": the maps come first";
    }
  }
}

TEST(PatternNotice, AStepOnAnAxisThatDoesNotRepeatIsNotADegenerateOne) {
  World w;
  GSHGEOM_Map fine = scale_map(2.0, 3.0);
  for (int axis = 0; axis < 2; axis++) {
    GSHSCENE_Pattern p = w.base();
    (axis == 0 ? p.x_step : p.y_step) = 0.0;
    (axis == 0 ? p.repeat_x : p.repeat_y) = 0;
    Ref r;
    ASSERT_EQ(w.intern(p, &r), GSHSCENE_OK);
    Unit u(w.s.store, w.sq.get(), &fine, r.get());
    EXPECT_EQ(gshscene_pattern_notice(u.item, nullptr, 0, nullptr), (GSHSCENE_Reason)0) << axis;
  }
  // A size of zero is degenerate whatever repeats.
  GSHSCENE_Pattern p = w.base();
  p.w = 0.0;
  p.repeat_x = 0;
  p.repeat_y = 0;
  Ref r;
  ASSERT_EQ(w.intern(p, &r), GSHSCENE_OK);
  Unit u(w.s.store, w.sq.get(), &fine, r.get());
  EXPECT_EQ(gshscene_pattern_notice(u.item, nullptr, 0, nullptr), GSHSCENE_REASON_PATTERN_DEGENERATE);
}

TEST(PatternNotice, InvertibleFactorsThatComposeToAZeroDeterminantAreNotNoticed) {
  // Each map has a determinant that is not zero; their composition underflows to exactly zero. PT10 tests each map on its own
  // doubles, no composition formed: nothing is noticed.
  World w;
  GSHSCENE_Pattern p = w.base();
  p.map = scale_map(std::ldexp(1.0, -64), std::ldexp(1.0, -64));
  Ref pat;
  ASSERT_EQ(w.intern(p, &pat), GSHSCENE_OK);
  GSHGEOM_Map tiny = scale_map(std::ldexp(1.0, -600), std::ldexp(1.0, -600));
  GSHGEOM_Map item_map = scale_map(std::ldexp(1.0, -64), std::ldexp(1.0, -64));
  Unit u(w.s.store, w.sq.get(), &item_map, pat.get());
  GSHGEOM_Map composed;
  ASSERT_EQ(gshgeom_map_compose(&tiny, &item_map, &composed), GSHGEOM_OK);
  ASSERT_EQ(gshgeom_map_compose(&composed, &p.map, &composed), GSHGEOM_OK);
  double det = composed.m[0][0] * composed.m[1][1] - composed.m[0][1] * composed.m[1][0];
  ASSERT_EQ(det, 0.0) << "the premise: the composed determinant underflows";
  EXPECT_EQ(gshscene_pattern_notice(u.item, nullptr, 0, &tiny), (GSHSCENE_Reason)0);
}

TEST(PatternNotice, AnEmptyTileDrawsNothingAndIsNoNotice) {
  World w;
  Ref empty(make_picture(w.s.store, [](GSHSCENE_List *) {}));
  ASSERT_NE(empty.get(), nullptr);
  Ref pat;
  ASSERT_EQ(w.intern(w.base(empty.get()), &pat), GSHSCENE_OK);
  GSHGEOM_Map fine = scale_map(2.0, 3.0);
  Unit u(w.s.store, w.sq.get(), &fine, pat.get());
  EXPECT_EQ(gshscene_pattern_notice(u.item, nullptr, 0, nullptr), (GSHSCENE_Reason)0);
}

TEST(PatternNotice, OnlyAFillOrStrokeWithAPatternPaintIsEverNoticed) {
  World w;
  Ref pat;
  ASSERT_EQ(w.intern(w.base(), &pat), GSHSCENE_OK);
  EXPECT_EQ(gshscene_pattern_notice(nullptr, nullptr, 0, nullptr), (GSHSCENE_Reason)0);
  ListHolder l(w.s.store);
  EXPECT_EQ(gshscene_list_fill(l.list, w.sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0.0), GSHSCENE_OK);
  GSHSCENE_Group g = make_group(1.0);
  EXPECT_EQ(gshscene_list_group_push(l.list, &g, 0, 0.0), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_group_pop(l.list), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
  GSHGEOM_Map singular = scale_map(0.0, 1.0);
  for (size_t i = 0; i < 3; i++) {
    const GSHSCENE_Item * item = nullptr;
    ASSERT_EQ(gshscene_list_item(l.list, i, &item), GSHSCENE_OK);
    EXPECT_EQ(gshscene_pattern_notice(item, nullptr, 0, &singular), (GSHSCENE_Reason)0) << i;
  }
  // The shared predicate routes: the fill's gradient notice and the stroke's notice return this one's answer for a pattern.
  GSHSCENE_Pattern d = w.base();
  d.w = 0.0;
  Ref dr;
  ASSERT_EQ(w.intern(d, &dr), GSHSCENE_OK);
  GSHGEOM_Map fine = scale_map(2.0, 3.0);
  Unit fill(w.s.store, w.sq.get(), &fine, dr.get());
  EXPECT_EQ(gshscene_paint_notice(fill.item, nullptr, 0, nullptr), GSHSCENE_REASON_PATTERN_DEGENERATE);
  Unit stroke(w.s.store, w.sq.get(), &fine, dr.get(), 1.0, true);
  EXPECT_EQ(gshscene_stroke_notice(stroke.item, nullptr, 0, nullptr), GSHSCENE_REASON_PATTERN_DEGENERATE);
  Unit stroke_ok(w.s.store, w.sq.get(), &fine, pat.get(), 1.0, true);
  EXPECT_EQ(gshscene_stroke_notice(stroke_ok.item, nullptr, 0, nullptr), (GSHSCENE_Reason)0);
  EXPECT_EQ(gshscene_stroke_notice(stroke_ok.item, nullptr, 0, &singular), GSHSCENE_REASON_MAP_NOT_INVERTIBLE);
}

// --- PT4: the paint in a list ---------------------------------------------------------------------------------------------

TEST(PatternPaint, AFillOrStrokeNamesAPatternByAPaintOfItsOwnKind) {
  World w;
  World other;
  Ref pat, foreign;
  ASSERT_EQ(w.intern(w.base(), &pat), GSHSCENE_OK);
  ASSERT_EQ(other.intern(other.base(), &foreign), GSHSCENE_OK);
  ListHolder l(w.s.store);
  auto fill = [&](GSHSCENE_Paint p) {
    return gshscene_list_fill(l.list, w.sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0);
  };
  EXPECT_EQ(fill(pattern_paint(pat.get(), 0.5)), GSHSCENE_OK);
  EXPECT_EQ(fill(pattern_paint(pat.get(), 0.0)), GSHSCENE_OK) << "an alpha multiplier of 0 is valid";
  EXPECT_EQ(fill(pattern_paint(nullptr)), GSHSCENE_ERR_INVALID) << "no resource";
  EXPECT_EQ(fill(pattern_paint(foreign.get())), GSHSCENE_ERR_INVALID) << "another store's";
  EXPECT_EQ(fill(pattern_paint(w.pic.get())), GSHSCENE_ERR_INVALID) << "a picture is not a pattern";
  EXPECT_EQ(fill(pattern_paint(pat.get(), 1.5)), GSHSCENE_ERR_INVALID) << "alpha past 1";
  EXPECT_EQ(fill(pattern_paint(pat.get(), kNaN)), GSHSCENE_ERR_INVALID);
  GSHSCENE_Paint colored = pattern_paint(pat.get());
  colored.rgba[1] = 0.5;
  EXPECT_EQ(fill(colored), GSHSCENE_ERR_INVALID) << "rgb zero";
  GSHSCENE_Paint spaced = pattern_paint(pat.get());
  spaced.space = space_of(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_SRGB);
  EXPECT_EQ(fill(spaced), GSHSCENE_ERR_INVALID) << "the space zero";
  GSHSCENE_Paint entry = pattern_paint(pat.get());
  entry.entry = 1;
  EXPECT_EQ(fill(entry), GSHSCENE_ERR_INVALID) << "the entry zero";
  GSHSCENE_Paint solid_with = solid(1, 0, 0, 1);
  solid_with.resource = pat.get();
  EXPECT_EQ(fill(solid_with), GSHSCENE_ERR_INVALID) << "only a gradient, an image paint or a pattern names a resource";
  GSHSCENE_Paint gradient_kind = pattern_paint(pat.get());
  gradient_kind.kind = GSHSCENE_PAINT_GRADIENT;
  EXPECT_EQ(fill(gradient_kind), GSHSCENE_ERR_INVALID) << "a pattern is not a gradient";
  // A stroke may be painted with one too (SK4).
  GSHSCENE_Stroke_Style st;
  gshscene_stroke_style_init(&st);
  const GSHSCENE_Resource * style = nullptr;
  ASSERT_EQ(gshscene_store_intern_stroke_style(w.s.store, &st, &style), GSHSCENE_OK);
  Ref style_ref(style);
  GSHSCENE_Paint sp = pattern_paint(pat.get());
  EXPECT_EQ(gshscene_list_stroke(l.list, w.sq.get(), style, nullptr, &sp, 0, 0.0), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
  // The list holds a reference: the pattern outlives its handle.
  size_t held = gshscene_resource_refcount(pat.get());
  EXPECT_GE(held, 4u);
}

TEST(PatternPaint, AGradientStopAndAnImageTintAreNeverAPattern) {
  World w;
  Ref pat;
  ASSERT_EQ(w.intern(w.base(), &pat), GSHSCENE_OK);
  GSHSCENE_Gradient_Stop stops[2];
  stops[0].offset = 0.0;
  stops[0].color = solid(1, 0, 0, 1);
  stops[1].offset = 1.0;
  stops[1].color = pattern_paint(pat.get());
  GSHSCENE_Gradient g;
  std::memset(&g, 0, sizeof g);
  g.kind = GSHSCENE_GRADIENT_LINEAR;
  g.p1[0] = 1.0;
  gshgeom_map_identity(2, &g.map);
  g.stop_count = 2;
  g.stops = stops;
  const GSHSCENE_Resource * r = nullptr;
  EXPECT_EQ(gshscene_store_intern_gradient(w.s.store, &g, &r), GSHSCENE_ERR_INVALID);
  // The pattern's own tint is likewise a plain color (checked with the rest of PT2 above): a pattern tint of kind pattern.
  GSHSCENE_Pattern p = w.base();
  p.has_tint = 1;
  p.tint = pattern_paint(pat.get());
  EXPECT_EQ(w.intern(p), GSHSCENE_ERR_INVALID);
}

TEST(PatternPaint, ATilePicturesDepthCountsInTheListThatNamesThePattern) {
  World w;
  Ref pat;
  ASSERT_EQ(w.intern(w.base(), &pat), GSHSCENE_OK);
  ListHolder l(w.s.store);
  {
    ListHolder plain(w.s.store);
    ASSERT_EQ(gshscene_list_fill(plain.list, w.sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_close(plain.list), GSHSCENE_OK);
    EXPECT_EQ(list_depth(w.s.store, plain.list), 0u);
  }
  GSHSCENE_Paint p = pattern_paint(pat.get());
  ASSERT_EQ(gshscene_list_fill(l.list, w.sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
  EXPECT_EQ(list_depth(w.s.store, l.list), gshscene_resource_picture_depth(w.pic.get()))
      << "as an instance of the tile would count: the picture the list freezes into is one deeper than the tile";
}

// --- PT13: a sink's disposition of a unit painted with a pattern -----------------------------------------------------

namespace {

GSHSCENE_Sink_Caps all_caps(bool pattern) {
  GSHSCENE_Sink_Caps caps;
  std::memset(&caps, 0, sizeof caps);
  for (uint32_t k = GSHSCENE_ITEM_FILL; k < GSHSCENE_ITEM_KIND_COUNT; k++) {
    caps.item_kinds |= 1u << k;
  }
  for (uint32_t c = 0; c < GSHSCENE_COMPOSITE_COUNT; c++) {
    caps.composites |= 1u << c;
  }
  caps.stroke_native = GSHSCENE_STROKE_NATIVE_ALL;
  caps.gradient_kinds = 0xe;
  caps.image_filters = 0xf;
  caps.image_extends = 0xf;
  caps.image_formats = GSHSCENE_IMAGE_FORMAT_ALL;
  caps.mask_native = GSHSCENE_MASK_NATIVE_ALL;
  caps.flags = GSHSCENE_CAP_CLIP_AA | GSHSCENE_CAP_BLEND_SPACES | GSHSCENE_CAP_GRADIENT_SPACES |
      GSHSCENE_CAP_GROUP_NON_ISOLATED | GSHSCENE_CAP_GROUP_KNOCKOUT | GSHSCENE_CAP_GROUP_SOFT_MASK |
      GSHSCENE_CAP_GLYPHS_FROM_FONT | GSHSCENE_CAP_STROKE_RUNS | GSHSCENE_CAP_IMAGE_FILTER_SPACES |
      (pattern ? GSHSCENE_CAP_PATTERN : 0u);
  caps.cancel_unit = GSHSCENE_CANCEL_BETWEEN_UNITS;
  caps.work_measure = "units";
  return caps;
}

GSHSCENE_Disposition dispose(const GSHSCENE_Sink_Caps & caps, const GSHSCENE_Item * item,
    const GSHGEOM_Map * maps = nullptr, size_t n = 0, const GSHSCENE_Resource * palette = nullptr) {
  GSHSCENE_Color_Space srgb = srgb_space();
  return gshscene_caps_disposition(&caps, item, maps, n, nullptr, &srgb, palette);
}

}  // namespace

TEST(PatternSink, AUnitPaintedWithAPatternIsRefusedWithoutTheFlagWhateverElseIsTrueOfIt) {
  World w;
  Ref pat;
  ASSERT_EQ(w.intern(w.base(), &pat), GSHSCENE_OK);
  GSHGEOM_Map fine = scale_map(2.0, 3.0);
  for (bool stroke : {false, true}) {
    Unit u(w.s.store, w.sq.get(), &fine, pat.get(), 1.0, stroke);
    GSHSCENE_Disposition with = dispose(all_caps(true), u.item);
    EXPECT_EQ(with.entry_class, (GSHSCENE_Entry_Class)0) << stroke;
    GSHSCENE_Disposition without = dispose(all_caps(false), u.item);
    EXPECT_EQ(without.entry_class, GSHSCENE_ENTRY_REFUSED) << stroke;
    EXPECT_EQ(without.reason, GSHSCENE_REASON_PATTERN) << stroke;
    // A pattern that draws nothing is refused all the same by a sink without the flag.
    GSHSCENE_Pattern d = w.base();
    d.w = 0.0;
    Ref dr;
    ASSERT_EQ(w.intern(d, &dr), GSHSCENE_OK);
    Unit nothing(w.s.store, w.sq.get(), &fine, dr.get(), 1.0, stroke);
    EXPECT_EQ(dispose(all_caps(false), nothing.item).reason, GSHSCENE_REASON_PATTERN) << stroke;
    EXPECT_EQ(dispose(all_caps(true), nothing.item).entry_class, (GSHSCENE_Entry_Class)0) << stroke;
  }
}

TEST(PatternSink, AnUncoloredPatternWhosePaletteTintIsMissingIsRefusedForTheEntry) {
  World w;
  GSHSCENE_Pattern p = w.base();
  p.has_tint = 1;
  p.tint = tint_of(GSHSCENE_PAINT_PALETTE, 0, 0, 0, 1.0, 2);
  Ref pat;
  ASSERT_EQ(w.intern(p, &pat), GSHSCENE_OK);
  GSHGEOM_Map fine = scale_map(2.0, 3.0);
  Unit u(w.s.store, w.sq.get(), &fine, pat.get());
  GSHSCENE_Color colors[3] = {{{1, 0, 0, 1}, {}}, {{0, 1, 0, 1}, {}}, {{0, 0, 1, 1}, {}}};
  GSHSCENE_Color two[2] = {colors[0], colors[1]};
  const GSHSCENE_Resource * three_palette = nullptr;
  const GSHSCENE_Resource * two_palette = nullptr;
  ASSERT_EQ(gshscene_store_intern_palette(w.s.store, 3, colors, &three_palette), GSHSCENE_OK);
  ASSERT_EQ(gshscene_store_intern_palette(w.s.store, 2, two, &two_palette), GSHSCENE_OK);
  Ref a(three_palette), b(two_palette);
  EXPECT_EQ(dispose(all_caps(true), u.item, nullptr, 0, three_palette).entry_class, (GSHSCENE_Entry_Class)0);
  GSHSCENE_Disposition d = dispose(all_caps(true), u.item, nullptr, 0, two_palette);
  EXPECT_EQ(d.entry_class, GSHSCENE_ENTRY_REFUSED);
  EXPECT_EQ(d.reason, GSHSCENE_REASON_PALETTE_ENTRY);
  d = dispose(all_caps(true), u.item, nullptr, 0, nullptr);
  EXPECT_EQ(d.reason, GSHSCENE_REASON_PALETTE_ENTRY);
  // The flag is checked first.
  EXPECT_EQ(dispose(all_caps(false), u.item, nullptr, 0, nullptr).reason, GSHSCENE_REASON_PATTERN);
}

TEST(PatternSink, AUnitThatTheCapsRefuseInTheTileRefusesTheUnitPaintedWithIt) {
  World w;
  GSHSCENE_Group knockout = make_group(1.0);
  knockout.knockout = 1;
  Ref ko_tile(make_picture(w.s.store, [&](GSHSCENE_List * l) {
    EXPECT_EQ(gshscene_list_fill(l, w.sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0), GSHSCENE_OK);
    EXPECT_EQ(gshscene_list_group_push(l, &knockout, 0, 0.0), GSHSCENE_OK);
    EXPECT_EQ(gshscene_list_fill(l, w.sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0), GSHSCENE_OK);
    EXPECT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
  }));
  Ref ok_tile(make_picture(w.s.store, [&](GSHSCENE_List * l) {
    EXPECT_EQ(gshscene_list_fill(l, w.sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0), GSHSCENE_OK);
  }));
  Ref ko_pat, ok_pat;
  ASSERT_EQ(w.intern(w.base(ko_tile.get()), &ko_pat), GSHSCENE_OK);
  ASSERT_EQ(w.intern(w.base(ok_tile.get()), &ok_pat), GSHSCENE_OK);
  GSHGEOM_Map fine = scale_map(2.0, 3.0);
  Unit ko(w.s.store, w.sq.get(), &fine, ko_pat.get());
  Unit ok(w.s.store, w.sq.get(), &fine, ok_pat.get());
  GSHSCENE_Sink_Caps caps = all_caps(true);
  EXPECT_EQ(dispose(caps, ko.item).entry_class, (GSHSCENE_Entry_Class)0) << "a sink with every cap draws it";
  caps.flags &= ~GSHSCENE_CAP_GROUP_KNOCKOUT;
  GSHSCENE_Disposition d = dispose(caps, ko.item);
  EXPECT_EQ(d.entry_class, GSHSCENE_ENTRY_REFUSED);
  EXPECT_EQ(d.reason, GSHSCENE_REASON_PATTERN) << "the pattern with a missing part is not the pattern";
  EXPECT_EQ(dispose(caps, ok.item).entry_class, (GSHSCENE_Entry_Class)0) << "a tile without the group is whole";
  // An approximation in the tile does not refuse the pattern: a clip drawn aliased.
  Ref clip_tile(make_picture(w.s.store, [&](GSHSCENE_List * l) {
    EXPECT_EQ(gshscene_list_clip_push(l, w.sq.get(), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0), GSHSCENE_OK);
    EXPECT_EQ(gshscene_list_fill(l, w.sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0), GSHSCENE_OK);
    EXPECT_EQ(gshscene_list_clip_pop(l), GSHSCENE_OK);
  }));
  Ref clip_pat;
  ASSERT_EQ(w.intern(w.base(clip_tile.get()), &clip_pat), GSHSCENE_OK);
  Unit clip(w.s.store, w.sq.get(), &fine, clip_pat.get());
  caps.flags &= ~GSHSCENE_CAP_CLIP_AA;
  EXPECT_EQ(dispose(caps, clip.item).entry_class, (GSHSCENE_Entry_Class)0);
}

TEST(PatternSink, TheCheckGoesThroughInstancesMasksAndNestedPatternsInTheTile) {
  World w;
  GSHSCENE_Group knockout = make_group(1.0);
  knockout.knockout = 1;
  Ref bad(make_picture(w.s.store, [&](GSHSCENE_List * l) {
    EXPECT_EQ(gshscene_list_group_push(l, &knockout, 0, 0.0), GSHSCENE_OK);
    EXPECT_EQ(gshscene_list_fill(l, w.sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0), GSHSCENE_OK);
    EXPECT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
  }));
  GSHGEOM_Map fine = scale_map(2.0, 3.0);
  GSHSCENE_Sink_Caps caps = all_caps(true);
  caps.flags &= ~GSHSCENE_CAP_GROUP_KNOCKOUT;
  // Through an instance of the bad picture.
  Ref via_instance(make_picture(w.s.store, [&](GSHSCENE_List * l) {
    EXPECT_EQ(gshscene_list_instance(l, bad.get(), nullptr, 0, 0.0), GSHSCENE_OK);
  }));
  Ref pat_instance;
  ASSERT_EQ(w.intern(w.base(via_instance.get()), &pat_instance), GSHSCENE_OK);
  Unit u1(w.s.store, w.sq.get(), &fine, pat_instance.get());
  EXPECT_EQ(dispose(caps, u1.item).reason, GSHSCENE_REASON_PATTERN) << "through an instance";
  // Through a nested pattern.
  Ref inner;
  ASSERT_EQ(w.intern(w.base(bad.get()), &inner), GSHSCENE_OK);
  Ref via_pattern(make_picture(w.s.store, [&](GSHSCENE_List * l) {
    GSHSCENE_Paint paint = pattern_paint(inner.get());
    EXPECT_EQ(gshscene_list_fill(l, w.sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &paint, 0, 0), GSHSCENE_OK);
  }));
  Ref pat_nested;
  ASSERT_EQ(w.intern(w.base(via_pattern.get()), &pat_nested), GSHSCENE_OK);
  Unit u2(w.s.store, w.sq.get(), &fine, pat_nested.get());
  EXPECT_EQ(dispose(caps, u2.item).reason, GSHSCENE_REASON_PATTERN) << "through a nested pattern";
  EXPECT_EQ(dispose(all_caps(true), u2.item).entry_class, (GSHSCENE_Entry_Class)0);
  // Through a mask's picture.
  Ref mask_picture(make_picture(w.s.store, [&](GSHSCENE_List * l) {
    EXPECT_EQ(gshscene_list_group_push(l, &knockout, 0, 0.0), GSHSCENE_OK);
    EXPECT_EQ(gshscene_list_fill(l, w.sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0), GSHSCENE_OK);
    EXPECT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
  }));
  Ref mask(known_mask(w.s.store, mask_picture.get(), 0));
  ASSERT_NE(mask.get(), nullptr);
  GSHSCENE_Group masked = make_group(1.0);
  masked.soft_mask = mask.get();
  Ref via_mask(make_picture(w.s.store, [&](GSHSCENE_List * l) {
    EXPECT_EQ(gshscene_list_group_push(l, &masked, 0, 0.0), GSHSCENE_OK);
    EXPECT_EQ(gshscene_list_fill(l, w.sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0), GSHSCENE_OK);
    EXPECT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
  }));
  Ref pat_mask;
  ASSERT_EQ(w.intern(w.base(via_mask.get()), &pat_mask), GSHSCENE_OK);
  Unit u3(w.s.store, w.sq.get(), &fine, pat_mask.get());
  EXPECT_EQ(dispose(caps, u3.item).reason, GSHSCENE_REASON_PATTERN) << "through a mask's picture";
  EXPECT_EQ(dispose(all_caps(true), u3.item).entry_class, (GSHSCENE_Entry_Class)0);
  // A placeholder the sink refuses is a missing part too.
  Ref placeholder_tile(make_picture(w.s.store, [&](GSHSCENE_List * l) {
    EXPECT_EQ(gshscene_list_placeholder(l, 1, 1, 0, nullptr, nullptr, 0, 0.0), GSHSCENE_OK);
  }));
  if (placeholder_tile.get()) {
    Ref pat_ph;
    ASSERT_EQ(w.intern(w.base(placeholder_tile.get()), &pat_ph), GSHSCENE_OK);
    Unit u4(w.s.store, w.sq.get(), &fine, pat_ph.get());
    EXPECT_EQ(dispose(all_caps(true), u4.item).reason, GSHSCENE_REASON_PATTERN);
  }
}

TEST(PatternSink, TheTilesUnitsSeeTheMapsOfTheChainIncludingThePatternsOwn) {
  // A stroke in the tile whose style needs a local width under the maps of the chain: with a pattern map that is not a
  // similarity the stroke's required features differ from those under the identity, and the tile check passes the maps of the
  // chain (the item's map and the pattern's among them), not an empty chain.
  World w;
  GSHSCENE_Stroke_Style st;
  gshscene_stroke_style_init(&st);
  const GSHSCENE_Resource * style = nullptr;
  ASSERT_EQ(gshscene_store_intern_stroke_style(w.s.store, &st, &style), GSHSCENE_OK);
  Ref style_ref(style);
  Ref stroke_tile(make_picture(w.s.store, [&](GSHSCENE_List * l) {
    EXPECT_EQ(gshscene_list_stroke(l, w.sq.get(), style, nullptr, nullptr, 0, 0.0), GSHSCENE_OK);
  }));
  GSHSCENE_Pattern anisotropic = w.base(stroke_tile.get());
  anisotropic.map = scale_map(1.0, 4.0);
  GSHSCENE_Pattern plain = w.base(stroke_tile.get());
  Ref ra, rp;
  ASSERT_EQ(w.intern(anisotropic, &ra), GSHSCENE_OK);
  ASSERT_EQ(w.intern(plain, &rp), GSHSCENE_OK);
  GSHSCENE_Sink_Caps caps = all_caps(true);
  caps.flags &= ~GSHSCENE_CAP_STROKE_EXPAND;
  caps.stroke_native = GSHSCENE_STROKE_NATIVE_ALL & ~GSHSCENE_STROKE_NATIVE_WIDTH_LOCAL_ANISOTROPIC;
  GSHGEOM_Map identity;
  gshgeom_map_identity(2, &identity);
  Unit ua(w.s.store, w.sq.get(), &identity, ra.get());
  Unit up(w.s.store, w.sq.get(), &identity, rp.get());
  EXPECT_EQ(dispose(caps, up.item).entry_class, (GSHSCENE_Entry_Class)0) << "a similarity needs no anisotropic width";
  EXPECT_EQ(dispose(caps, ua.item).reason, GSHSCENE_REASON_PATTERN)
      << "the pattern's map makes the tile's stroke anisotropic: the check saw the chain";
}

// --- the retained scene: validation, diff and damage ----------------------------------------------------------------------

namespace {

struct PatternDoc {
  SceneHolder s;
  Ref sq;
  Ref pic;
  explicit PatternDoc(const GSHSCENE_Limits * limits = nullptr) : s(limits), sq(intern_square(s.store())) {
    pic = Ref(make_picture(s.store(), [&](GSHSCENE_List * l) {
      EXPECT_EQ(gshscene_list_fill(l, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0), GSHSCENE_OK);
    }));
  }
  Ref pattern(const GSHSCENE_Resource * tile, double x_step = 1.0, bool fg_tint = false) {
    GSHSCENE_Pattern p;
    gshscene_pattern_init(&p);
    p.tile = tile ? tile : pic.get();
    p.x_step = x_step;
    if (fg_tint) {
      p.has_tint = 1;
      p.tint = tint_of(GSHSCENE_PAINT_FOREGROUND, 0, 0, 0, 1.0);
    }
    const GSHSCENE_Resource * r = nullptr;
    EXPECT_EQ(gshscene_store_intern_pattern(s.store(), &p, &r), GSHSCENE_OK);
    return Ref(r);
  }
  GSHSCENE_Node shape(double x, double y, const GSHSCENE_Resource * pattern) {
    GSHSCENE_Node n = shape_node(sq.get());
    n.map = translate(x, y);
    if (pattern) {
      n.paint = pattern_paint(pattern);
    }
    return n;
  }
};

struct Diffed {
  std::vector<std::pair<uint64_t, int>> changes;
  GSHSCENE_Damage damage;
};

Diffed diff_of(const GSHSCENE_Version * a, const GSHSCENE_Version * b) {
  Diffed r;
  std::memset(&r.damage, 0xAB, sizeof r.damage);
  GSHSCENE_Diff * d = nullptr;
  EXPECT_EQ(gshscene_diff(a, b, nullptr, nullptr, &d), GSHSCENE_OK);
  if (!d) {
    return r;
  }
  for (size_t i = 0; i < gshscene_diff_count(d); i++) {
    uint64_t id;
    GSHSCENE_Diff_Kind kind;
    EXPECT_EQ(gshscene_diff_get(d, i, &id, &kind), GSHSCENE_OK);
    r.changes.push_back({id, (int)kind});
  }
  EXPECT_EQ(gshscene_diff_damage(d, &r.damage), GSHSCENE_OK);
  gshscene_diff_destroy(d);
  return r;
}

}  // namespace

TEST(PatternScene, ANodeThatPaintsWithAPatternIsDiffedAndDamagedByHandleAndNothingElseIs) {
  PatternDoc d;
  Ref p1 = d.pattern(nullptr, 1.0);
  Ref p2 = d.pattern(nullptr, 2.0);
  VersionRef v1;
  uint64_t painted = 0, other = 0;
  {
    EditHolder e(d.s);
    painted = add(e.edit, GSHSCENE_ROOT_ID, d.shape(0, 0, p1.get()));
    other = add(e.edit, GSHSCENE_ROOT_ID, d.shape(100, 0, p1.get()));
    add(e.edit, GSHSCENE_ROOT_ID, d.shape(200, 0, nullptr));
    publish(d.s, e, &v1);
  }
  // Edit one node's paint to another pattern: that node alone changes and is damaged.
  VersionRef v2;
  {
    EditHolder e(d.s, v1.get());
    GSHSCENE_Node n = d.shape(0, 0, p2.get());
    ASSERT_EQ(gshscene_edit_set(e.edit, painted, &n), GSHSCENE_OK);
    publish(d.s, e, &v2);
  }
  Diffed diff = diff_of(v1.get(), v2.get());
  ASSERT_EQ(diff.changes.size(), 1u);
  EXPECT_EQ(diff.changes[0].first, painted);
  EXPECT_EQ(diff.damage.unbounded, 0);
  EXPECT_EQ(diff.damage.empty, 0);
  EXPECT_LT(diff.damage.rect.x1, 50) << "the damage is the node's own bounds: a pattern adds no outset (PT12)";
  (void)other;
  // The same handle again is no change.
  VersionRef v3;
  {
    EditHolder e(d.s, v2.get());
    GSHSCENE_Node n = d.shape(0, 0, p2.get());
    ASSERT_EQ(gshscene_edit_set(e.edit, painted, &n), GSHSCENE_OK);
    publish(d.s, e, &v3);
  }
  EXPECT_EQ(diff_of(v2.get(), v3.get()).changes.size(), 0u);
  // The version hash and dump name the paint by the pattern's hash.
  std::string dump = dump_version(v1.get());
  char want[32];
  std::snprintf(want, sizeof want, "%016llx", (unsigned long long)gshscene_resource_hash(p1.get()));
  EXPECT_NE(dump.find(want), std::string::npos);
  EXPECT_NE(gshscene_version_hash(v1.get()), gshscene_version_hash(v2.get()));
}

TEST(PatternScene, AChangedForegroundDamagesTheFillsWhoseTileOrTintReadsItAndNoOthers) {
  PatternDoc d;
  GSHSCENE_Paint fg = tint_of(GSHSCENE_PAINT_FOREGROUND, 0, 0, 0, 1.0);
  Ref fg_tile(make_picture(d.s.store(), [&](GSHSCENE_List * l) {
    EXPECT_EQ(gshscene_list_fill(l, d.sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &fg, 0, 0), GSHSCENE_OK);
  }));
  Ref nested_tile(make_picture(d.s.store(), [&](GSHSCENE_List * l) {
    EXPECT_EQ(gshscene_list_instance(l, fg_tile.get(), nullptr, 0, 0.0), GSHSCENE_OK);
  }));
  Ref plain = d.pattern(nullptr);
  Ref reads_tile = d.pattern(fg_tile.get());
  Ref reads_nested = d.pattern(nested_tile.get());
  Ref reads_tint = d.pattern(nullptr, 1.0, true);
  Ref via_outer;
  {
    // A pattern whose tile holds a fill painted with a pattern that reads the foreground.
    Ref tile(make_picture(d.s.store(), [&](GSHSCENE_List * l) {
      GSHSCENE_Paint paint = pattern_paint(reads_tile.get());
      EXPECT_EQ(gshscene_list_fill(l, d.sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &paint, 0, 0), GSHSCENE_OK);
    }));
    via_outer = d.pattern(tile.get());
  }
  VersionRef v1;
  {
    EditHolder e(d.s);
    add(e.edit, GSHSCENE_ROOT_ID, d.shape(0, 0, plain.get()));         // reads nothing
    add(e.edit, GSHSCENE_ROOT_ID, d.shape(100, 0, reads_tile.get()));   // 100: the tile
    add(e.edit, GSHSCENE_ROOT_ID, d.shape(200, 0, reads_nested.get())); // 200: through an instance in the tile
    add(e.edit, GSHSCENE_ROOT_ID, d.shape(300, 0, reads_tint.get()));   // 300: the tint
    add(e.edit, GSHSCENE_ROOT_ID, d.shape(400, 0, via_outer.get()));    // 400: through a pattern in the tile
    add(e.edit, GSHSCENE_ROOT_ID, d.shape(500, 0, nullptr));            // solid
    publish(d.s, e, &v1);
  }
  VersionRef v2;
  {
    EditHolder e(d.s, v1.get());
    GSHSCENE_Color red = {{1, 0, 0, 1}, {}};
    ASSERT_EQ(gshscene_edit_set_foreground(e.edit, &red), GSHSCENE_OK);
    publish(d.s, e, &v2);
  }
  GSHSCENE_Diff * diff = nullptr;
  ASSERT_EQ(gshscene_diff(v1.get(), v2.get(), nullptr, nullptr, &diff), GSHSCENE_OK);
  EXPECT_EQ(gshscene_diff_foreground_changed(diff), 1);
  GSHSCENE_Damage damage;
  ASSERT_EQ(gshscene_diff_damage(diff, &damage), GSHSCENE_OK);
  gshscene_diff_destroy(diff);
  EXPECT_EQ(damage.unbounded, 0);
  EXPECT_EQ(damage.empty, 0);
  // From the tile at 100 to the shape at 400 (each 4 wide, one pixel of antialiasing around): x in [99, 405); not the plain
  // shape at 0 and not the solid at 500.
  EXPECT_EQ(damage.rect.x0, 99);
  EXPECT_EQ(damage.rect.x1, 405);
}

TEST(PatternScene, AChangedPaletteDamagesTheFillsWhoseTileOrTintNamesAnEntry) {
  PatternDoc d;
  GSHSCENE_Paint pal = tint_of(GSHSCENE_PAINT_PALETTE, 0, 0, 0, 1.0, 1);
  Ref pal_tile(make_picture(d.s.store(), [&](GSHSCENE_List * l) {
    EXPECT_EQ(gshscene_list_fill(l, d.sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &pal, 0, 0), GSHSCENE_OK);
  }));
  Ref plain = d.pattern(nullptr);
  Ref reads = d.pattern(pal_tile.get());
  GSHSCENE_Color a[2] = {{{1, 0, 0, 1}, {}}, {{0, 1, 0, 1}, {}}};
  GSHSCENE_Color b[2] = {{{1, 0, 0, 1}, {}}, {{0, 0, 1, 1}, {}}};
  const GSHSCENE_Resource * pa = nullptr;
  const GSHSCENE_Resource * pb = nullptr;
  ASSERT_EQ(gshscene_store_intern_palette(d.s.store(), 2, a, &pa), GSHSCENE_OK);
  ASSERT_EQ(gshscene_store_intern_palette(d.s.store(), 2, b, &pb), GSHSCENE_OK);
  Ref ra(pa), rb(pb);
  VersionRef v1;
  {
    EditHolder e(d.s);
    add(e.edit, GSHSCENE_ROOT_ID, d.shape(0, 0, plain.get()));
    add(e.edit, GSHSCENE_ROOT_ID, d.shape(100, 0, reads.get()));
    ASSERT_EQ(gshscene_edit_set_palette(e.edit, pa), GSHSCENE_OK);
    publish(d.s, e, &v1);
  }
  VersionRef v2;
  {
    EditHolder e(d.s, v1.get());
    ASSERT_EQ(gshscene_edit_set_palette(e.edit, pb), GSHSCENE_OK);
    publish(d.s, e, &v2);
  }
  GSHSCENE_Diff * diff = nullptr;
  ASSERT_EQ(gshscene_diff(v1.get(), v2.get(), nullptr, nullptr, &diff), GSHSCENE_OK);
  EXPECT_EQ(gshscene_diff_palette_changed(diff), 1);
  GSHSCENE_Damage damage;
  ASSERT_EQ(gshscene_diff_damage(diff, &damage), GSHSCENE_OK);
  gshscene_diff_destroy(diff);
  EXPECT_EQ(damage.rect.x0, 99) << "the fill whose tile names an entry, and only it";
  EXPECT_EQ(damage.rect.x1, 105);
}

TEST(PatternScene, TheTilesDepthCountsTowardTheNestingLimitOfAnInstanceChain) {
  GSHSCENE_Limits limits;
  gshscene_limits_default(&limits);
  limits.max_picture_depth = 3;
  // The tile is depth 2 (a picture of a list that names a depth-1 picture): the pattern is depth 2 and may be nested once.
  // A chain of `pictures` hidden groups, the first holding the shape painted with the pattern, each later one the instance of
  // the one before, and an instance of the last: with a plain shape the chain is as deep as it has groups, with the pattern the
  // first group is two deeper than a plain one.
  auto publish_chain = [&](bool with_pattern, int pictures) {
    PatternDoc d(&limits);
    Ref inner(make_picture(d.s.store(), [&](GSHSCENE_List * l) {
      EXPECT_EQ(gshscene_list_fill(l, d.sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0), GSHSCENE_OK);
    }));
    Ref tile(make_picture(d.s.store(), [&](GSHSCENE_List * l) {
      EXPECT_EQ(gshscene_list_instance(l, inner.get(), nullptr, 0, 0.0), GSHSCENE_OK);
    }));
    EXPECT_EQ(gshscene_resource_picture_depth(tile.get()), 2u);
    Ref pat = d.pattern(tile.get());
    EditHolder e(d.s);
    GSHSCENE_Node hidden = group_node(1.0);
    hidden.flags = GSHSCENE_NODE_HIDDEN;
    uint64_t prev = add(e.edit, GSHSCENE_ROOT_ID, hidden);
    add(e.edit, prev, d.shape(0, 0, with_pattern ? pat.get() : nullptr));
    for (int i = 1; i < pictures; i++) {
      uint64_t g = add(e.edit, GSHSCENE_ROOT_ID, hidden);
      add(e.edit, g, instance_node(prev));
      prev = g;
    }
    add(e.edit, GSHSCENE_ROOT_ID, instance_node(prev));
    const GSHSCENE_Version * out = nullptr;
    GSHSCENE_Result r = gshscene_scene_publish(d.s.scene, e.edit, nullptr, &out);
    gshscene_version_release(out);
    return r;
  };
  // One instance of the group that holds the pattern: 1 + 2 = 3 is at the limit.
  EXPECT_EQ(publish_chain(true, 1), GSHSCENE_OK);
  // Two levels: 1 + 3 = 4, one past it. The same chain of plain shapes is only 2 deep, so the tile's depth is what counts.
  EXPECT_EQ(publish_chain(false, 2), GSHSCENE_OK);
  EXPECT_EQ(publish_chain(true, 2), GSHSCENE_ERR_LIMIT);
}

// --- the emitted list of a node painted with a pattern ---------------------------------------------------------------

TEST(PatternScene, TheEmittedListKeepsThePatternPaintAndItsChain) {
  PatternDoc d;
  Ref pat = d.pattern(nullptr, 2.0);
  VersionRef v;
  {
    EditHolder e(d.s);
    GSHSCENE_Node n = d.shape(5, 6, pat.get());
    n.paint.rgba[3] = 0.5;
    add(e.edit, GSHSCENE_ROOT_ID, n);
    publish(d.s, e, &v);
  }
  const GSHSCENE_List * list = nullptr;
  ASSERT_EQ(gshscene_version_list(v.get(), &list), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_count(list), 1u);
  const GSHSCENE_Item * item = nullptr;
  ASSERT_EQ(gshscene_list_item(list, 0, &item), GSHSCENE_OK);
  EXPECT_EQ(item->paint.kind, GSHSCENE_PAINT_PATTERN);
  EXPECT_EQ(item->paint.resource, pat.get());
  EXPECT_EQ(item->paint.rgba[3], 0.5);
  EXPECT_EQ(list_depth(d.s.store(), list), gshscene_resource_picture_depth(d.pic.get()))
      << "the emitted list counts the tile as an instance's picture is counted";
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
