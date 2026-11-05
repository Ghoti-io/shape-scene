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
 * The hatch (hatch.h, HX1 to HX3, HX9, HX14, HX15): the resource's rules in the order they are checked, its bounds at the bound and one
 * past it, equality, its hash and dump against the independent generator, the paint and where it is allowed, what draws nothing, the
 * bounds, a sink's disposition of a hatch fill, and the retained scene's damage.
 */

#include "scene_fixtures.h"

#include "../alloc_tracker.h"
#include "../known_answers.h"

#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <string>
#include <vector>

using namespace scene;

namespace {

const double kInf = std::numeric_limits<double>::infinity();
const double kNaN = std::numeric_limits<double>::quiet_NaN();

/** A stroke style as the tests need it, interned. */
struct StyleSpec {
  double width = 1.0;
  uint32_t width_space = GSHSCENE_STROKE_WIDTH_LOCAL;
  uint32_t join = GSHSCENE_STROKE_JOIN_MITER;
  uint32_t cap = GSHSCENE_STROKE_CAP_BUTT;
  uint32_t degenerate = GSHSCENE_STROKE_DEGENERATE_DRAW;
  double miter = 4.0;
  std::vector<double> dashes;
  uint32_t alignment = GSHSCENE_STROKE_ALIGN_CENTER;
};

const GSHSCENE_Resource * make_style(GSHSCENE_Store * store, const StyleSpec & sp) {
  GSHSCENE_Stroke_Style st;
  gshscene_stroke_style_init(&st);
  st.width = sp.width;
  st.width_space = sp.width_space;
  st.join = sp.join;
  st.cap = sp.cap;
  st.degenerate = sp.degenerate;
  st.miter_limit = sp.miter;
  st.dash_count = sp.dashes.size();
  st.dashes = sp.dashes.empty() ? nullptr : sp.dashes.data();
  st.alignment = sp.alignment;
  const GSHSCENE_Resource * r = nullptr;
  EXPECT_EQ(gshscene_store_intern_stroke_style(store, &st, &r), GSHSCENE_OK);
  return r;
}

/** A store, a stroke style and the square. */
struct World {
  StoreHolder s;
  explicit World(const GSHSCENE_Limits * limits = nullptr, GSHSCENE_Index_Hash_Fn index_hash = nullptr) : s(limits, nullptr, index_hash) {}
  Ref style{make_style(s.store, StyleSpec())};
  Ref sq{intern_square(s.store)};

  GSHSCENE_Hatch base(const GSHSCENE_Hatch_Family * f, size_t n) const {
    GSHSCENE_Hatch h;
    gshscene_hatch_init(&h);
    h.style = style.get();
    h.family_count = n;
    h.families = f;
    return h;
  }
  GSHSCENE_Result intern(const GSHSCENE_Hatch & h, Ref * out = nullptr) const {
    const GSHSCENE_Resource * r = nullptr;
    GSHSCENE_Result result = gshscene_store_intern_hatch(s.store, &h, &r);
    if (out) {
      *out = Ref(result == GSHSCENE_OK ? r : nullptr);
    } else if (result == GSHSCENE_OK) {
      gshscene_resource_release(r);
    }
    return result;
  }
};

GSHSCENE_Hatch_Family family(double angle = 45, double ox = 0, double oy = 0, double dx = 0, double dy = 1.5,
    const std::vector<double> * dashes = nullptr) {
  GSHSCENE_Hatch_Family f;
  f.angle = angle;
  f.origin_x = ox;
  f.origin_y = oy;
  f.offset_along = dx;
  f.offset_across = dy;
  f.dash_count = dashes ? dashes->size() : 0;
  f.dashes = dashes && !dashes->empty() ? dashes->data() : nullptr;
  return f;
}

GSHSCENE_Paint hatch_paint(const GSHSCENE_Resource * hatch, double alpha = 1.0) {
  GSHSCENE_Paint p = solid(0, 0, 0, alpha);
  p.kind = GSHSCENE_PAINT_HATCH;
  p.resource = hatch;
  return p;
}

GSHGEOM_Map scale_map(double sx, double sy) {
  GSHGEOM_Map m;
  gshgeom_map_scale(2, sx, sy, 1.0, &m);
  return m;
}

}  // namespace

// --- the pinned values -------------------------------------------------------------------------------------------------

TEST(Hatch, ThePinnedValuesArePinned) {
  EXPECT_EQ((int)GSHSCENE_RESOURCE_HATCH, 13);
  EXPECT_EQ((int)GSHSCENE_PAINT_HATCH, 6);
  EXPECT_EQ((int)GSHSCENE_REASON_HATCH, 39);
  EXPECT_EQ((int)GSHSCENE_REASON_HATCH_DEGENERATE, 40);
  EXPECT_EQ(GSHSCENE_CAP_HATCH, 0x10000u);
  EXPECT_EQ((int)GSHSCENE_HATCH_NESTING_RULE, 0);
  EXPECT_EQ((int)GSHSCENE_HATCH_NESTING_OUTER, 1);
  EXPECT_EQ((int)GSHSCENE_HATCH_NESTING_IGNORE, 2);
  EXPECT_EQ(GSHSCENE_HATCH_FAMILIES_MAX, (size_t)256);
}

TEST(Hatch, InitGivesTheDefaultsAndIgnoresNull) {
  gshscene_hatch_init(nullptr);
  GSHSCENE_Hatch h;
  std::memset(&h, 0xAB, sizeof h);
  gshscene_hatch_init(&h);
  EXPECT_EQ(h.angle_unit, (uint32_t)GSHGEOM_ANGLE_DEGREES);
  EXPECT_EQ(h.nesting, (uint32_t)GSHSCENE_HATCH_NESTING_RULE);
  EXPECT_EQ(h.style, nullptr);
  EXPECT_EQ(h.map.dim, 2);
  EXPECT_EQ(h.ink.kind, GSHSCENE_PAINT_SOLID);
  EXPECT_EQ(h.ink.rgba[3], 1.0);
  EXPECT_EQ(h.family_count, 0u);
  EXPECT_EQ(h.families, nullptr);
}

// --- HX2: the order of the checks, and each bound -------------------------------------------------------------------------

TEST(Hatch, AValidHatchInternsAndReadsBack) {
  World w;
  std::vector<double> dashes = {3.0, -1.0, 0.0};
  GSHSCENE_Hatch_Family f[2] = {family(30, 1, 2, 0.5, 2.5, &dashes), family(120, -3, 4, 0, 1)};
  GSHSCENE_Hatch h = w.base(f, 2);
  h.nesting = GSHSCENE_HATCH_NESTING_OUTER;
  Ref r;
  ASSERT_EQ(w.intern(h, &r), GSHSCENE_OK);
  EXPECT_EQ(gshscene_resource_kind(r.get()), GSHSCENE_RESOURCE_HATCH);
  GSHSCENE_Hatch back;
  ASSERT_EQ(gshscene_resource_hatch(r.get(), &back), GSHSCENE_OK);
  EXPECT_EQ(back.family_count, 2u);
  EXPECT_EQ(back.nesting, (uint32_t)GSHSCENE_HATCH_NESTING_OUTER);
  EXPECT_EQ(back.style, w.style.get());
  EXPECT_EQ(back.families[0].dash_count, 3u) << "the elements are stored as given, odd count included";
  EXPECT_EQ(back.families[0].dashes[2], 0.0);
  EXPECT_NE(back.families[0].dashes, dashes.data()) << "the resource holds its own copy";
  EXPECT_EQ(back.families[1].dash_count, 0u);
  EXPECT_EQ(gshscene_resource_hatch(w.sq.get(), &back), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_resource_hatch(nullptr, &back), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_resource_hatch(r.get(), nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_resource_refcount(w.style.get()) > 1u, true) << "the hatch holds its style";
}

TEST(Hatch, TheChecksComeInTheDocumentedOrder) {
  World w;
  GSHSCENE_Hatch_Family f = family();
  GSHSCENE_Hatch h = w.base(&f, 1);
  const GSHSCENE_Resource * out = nullptr;
  EXPECT_EQ(gshscene_store_intern_hatch(nullptr, &h, &out), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_store_intern_hatch(w.s.store, nullptr, &out), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_store_intern_hatch(w.s.store, &h, nullptr), GSHSCENE_ERR_INVALID);
  // an invalid value with every later thing wrong too: invalid wins
  {
    GSHSCENE_Hatch_Family g = family(kNaN, 1e30, 0, 0, 1);
    GSHSCENE_Hatch bad = w.base(&g, 1);
    bad.map.dim = 3;
    EXPECT_EQ(w.intern(bad), GSHSCENE_ERR_INVALID);
  }
  // a 3D map beats the style with dashes and the limits
  {
    Ref dashed(make_style(w.s.store, [] { StyleSpec s; s.dashes = {2, 1}; return s; }()));
    GSHSCENE_Hatch_Family g = family(0, 1e30, 0, 0, 1);
    GSHSCENE_Hatch bad = w.base(&g, 1);
    bad.style = dashed.get();
    gshgeom_map_identity(3, &bad.map);
    EXPECT_EQ(w.intern(bad), GSHSCENE_ERR_UNSUPPORTED);
    gshgeom_map_identity(2, &bad.map);
    EXPECT_EQ(w.intern(bad), GSHSCENE_ERR_INVALID) << "a style with dashes is invalid, and decided before the limits";
  }
  // the limit is last
  {
    GSHSCENE_Hatch_Family g = family(0, std::ldexp(1.0, 65), 0, 0, 1);
    GSHSCENE_Hatch bad = w.base(&g, 1);
    EXPECT_EQ(w.intern(bad), GSHSCENE_ERR_LIMIT);
  }
  // each invalid thing
  struct Case {
    const char * what;
    std::function<void(GSHSCENE_Hatch &, GSHSCENE_Hatch_Family &)> set;
  };
  const Case cases[] = {
      {"unit", [](GSHSCENE_Hatch & hh, GSHSCENE_Hatch_Family &) { hh.angle_unit = GSHGEOM_ANGLE_UNIT_COUNT; }},
      {"nesting", [](GSHSCENE_Hatch & hh, GSHSCENE_Hatch_Family &) { hh.nesting = GSHSCENE_HATCH_NESTING_COUNT; }},
      {"no style", [](GSHSCENE_Hatch & hh, GSHSCENE_Hatch_Family &) { hh.style = nullptr; }},
      {"style of another kind", [&](GSHSCENE_Hatch & hh, GSHSCENE_Hatch_Family &) { hh.style = w.sq.get(); }},
      {"no families", [](GSHSCENE_Hatch & hh, GSHSCENE_Hatch_Family &) { hh.family_count = 0; }},
      {"families null", [](GSHSCENE_Hatch & hh, GSHSCENE_Hatch_Family &) { hh.families = nullptr; }},
      {"angle nan", [](GSHSCENE_Hatch &, GSHSCENE_Hatch_Family & ff) { ff.angle = kNaN; }},
      {"origin inf", [](GSHSCENE_Hatch &, GSHSCENE_Hatch_Family & ff) { ff.origin_x = kInf; }},
      {"along nan", [](GSHSCENE_Hatch &, GSHSCENE_Hatch_Family & ff) { ff.offset_along = kNaN; }},
      {"across inf", [](GSHSCENE_Hatch &, GSHSCENE_Hatch_Family & ff) { ff.offset_across = -kInf; }},
      {"dashes counted, not given", [](GSHSCENE_Hatch &, GSHSCENE_Hatch_Family & ff) { ff.dash_count = 2; ff.dashes = nullptr; }},
      {"map nan", [](GSHSCENE_Hatch & hh, GSHSCENE_Hatch_Family &) { hh.map.m[0][0] = kNaN; }},
      {"origin y nan", [](GSHSCENE_Hatch &, GSHSCENE_Hatch_Family & ff) { ff.origin_y = kNaN; }},
      {"a dash nan", [](GSHSCENE_Hatch &, GSHSCENE_Hatch_Family & ff) { static const double bad[2] = {1.0, kNaN}; ff.dash_count = 2; ff.dashes = bad; }},
      {"a dash inf", [](GSHSCENE_Hatch &, GSHSCENE_Hatch_Family & ff) { static const double bad[2] = {1.0, kInf}; ff.dash_count = 2; ff.dashes = bad; }},
      {"ink a gradient", [](GSHSCENE_Hatch & hh, GSHSCENE_Hatch_Family &) { hh.ink.kind = GSHSCENE_PAINT_GRADIENT; }},
      {"ink a hatch", [](GSHSCENE_Hatch & hh, GSHSCENE_Hatch_Family &) { hh.ink.kind = GSHSCENE_PAINT_HATCH; }},
      {"ink a hatch that exists", [&](GSHSCENE_Hatch & hh, GSHSCENE_Hatch_Family &) {
         static Ref inner;
         static GSHSCENE_Hatch_Family inner_family = family();
         if (!inner.get()) {
           GSHSCENE_Hatch ih = w.base(&inner_family, 1);
           EXPECT_EQ(w.intern(ih, &inner), GSHSCENE_OK);
         }
         hh.ink.kind = GSHSCENE_PAINT_HATCH;
         hh.ink.resource = inner.get();
       }},
      {"ink in a space that is not wholly given", [](GSHSCENE_Hatch & hh, GSHSCENE_Hatch_Family &) { hh.ink.space = space_of(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_UNKNOWN); }},
      {"ink alpha 2", [](GSHSCENE_Hatch & hh, GSHSCENE_Hatch_Family &) { hh.ink.rgba[3] = 2.0; }},
  };
  for (const Case & c : cases) {
    GSHSCENE_Hatch_Family g = family();
    GSHSCENE_Hatch hh = w.base(&g, 1);
    c.set(hh, g);
    EXPECT_EQ(w.intern(hh), GSHSCENE_ERR_INVALID) << c.what;
  }
  // a style of another store is not this store's
  {
    World other;
    GSHSCENE_Hatch_Family g = family();
    GSHSCENE_Hatch hh = w.base(&g, 1);
    hh.style = other.style.get();
    EXPECT_EQ(w.intern(hh), GSHSCENE_ERR_INVALID);
  }
  // an ink in a space the library does not convert is unsupported, after the validity of the rest
  {
    GSHSCENE_Hatch_Family g = family();
    GSHSCENE_Hatch hh = w.base(&g, 1);
    hh.ink = solid(0, 0, 0, 1);
    hh.ink.space = space_of(GCOL_PRIMARIES_PROPHOTO, GCOL_TRANSFER_LINEAR);
    EXPECT_EQ(w.intern(hh), GSHSCENE_ERR_UNSUPPORTED);
    hh.ink.rgba[3] = 2.0;
    EXPECT_EQ(w.intern(hh), GSHSCENE_ERR_INVALID);
  }
}

TEST(Hatch, TheElementsOfAFamilyNeedAPenDownElementAndALength) {
  World w;
  std::vector<double> spaces = {-1.0, -2.0}, zero = {0.0}, dot_space = {0.0, -1.0}, only_dash = {2.0};
  const std::vector<double> * ok[] = {&dot_space, &only_dash};
  for (auto * d : ok) {
    GSHSCENE_Hatch_Family f = family(0, 0, 0, 0, 1, d);
    GSHSCENE_Hatch h = w.base(&f, 1);
    EXPECT_EQ(w.intern(h), GSHSCENE_OK);
  }
  for (auto * d : {&spaces, &zero}) {
    GSHSCENE_Hatch_Family f = family(0, 0, 0, 0, 1, d);
    GSHSCENE_Hatch h = w.base(&f, 1);
    EXPECT_EQ(w.intern(h), GSHSCENE_ERR_INVALID);
  }
}

TEST(Hatch, AStyleWithDashesOrAnAlignmentIsRefusedAndASolidSpellingIsNot) {
  World w;
  GSHSCENE_Hatch_Family f = family();
  StyleSpec dashed;
  dashed.dashes = {3, 1};
  StyleSpec inside;
  inside.alignment = GSHSCENE_STROKE_ALIGN_INSIDE;
  StyleSpec outside;
  outside.alignment = GSHSCENE_STROKE_ALIGN_OUTSIDE;
  StyleSpec spaces_only;
  spaces_only.dashes = {-1, 2};  // SK3: a negative length is solid
  StyleSpec zeros;
  zeros.dashes = {0, 0};  // all zero: solid
  for (const StyleSpec * sp : {&dashed, &inside, &outside}) {
    Ref st(make_style(w.s.store, *sp));
    GSHSCENE_Hatch h = w.base(&f, 1);
    h.style = st.get();
    EXPECT_EQ(w.intern(h), GSHSCENE_ERR_INVALID);
  }
  for (const StyleSpec * sp : {&spaces_only, &zeros}) {
    Ref st(make_style(w.s.store, *sp));
    GSHSCENE_Hatch h = w.base(&f, 1);
    h.style = st.get();
    EXPECT_EQ(w.intern(h), GSHSCENE_OK) << "canonically solid";
  }
}

TEST(Hatch, EachBoundHoldsAtTheBoundAndFailsOnePast) {
  World w;
  const double lo = std::ldexp(1.0, -64), hi = std::ldexp(1.0, 64);
  struct Probe {
    const char * what;
    std::function<void(GSHSCENE_Hatch_Family &, double)> set;
    double ok, bad;
  };
  const Probe probes[] = {
      {"origin x", [](GSHSCENE_Hatch_Family & f, double v) { f.origin_x = v; }, hi, std::ldexp(1.0, 65)},
      {"origin y", [](GSHSCENE_Hatch_Family & f, double v) { f.origin_y = v; }, -lo, -std::ldexp(1.0, -65)},
      {"along", [](GSHSCENE_Hatch_Family & f, double v) { f.offset_along = v; }, lo, std::ldexp(1.0, -65)},
      {"across", [](GSHSCENE_Hatch_Family & f, double v) { f.offset_across = v; }, -hi, -std::ldexp(1.0, 65)},
      {"angle", [](GSHSCENE_Hatch_Family & f, double v) { f.angle = v; }, std::ldexp(1.0, 40), std::ldexp(1.0, 41)},
  };
  for (const Probe & p : probes) {
    GSHSCENE_Hatch_Family f = family(0, 0, 0, 0, 1);
    GSHSCENE_Hatch h = w.base(&f, 1);
    p.set(f, p.ok);
    // a unit of radians keeps 2^40 within the quarter-turn bound of the rotation; degrees at 2^40 are 2^40/90 quarter turns
    EXPECT_EQ(w.intern(h), GSHSCENE_OK) << p.what << " at the bound";
    p.set(f, p.bad);
    EXPECT_EQ(w.intern(h), GSHSCENE_ERR_LIMIT) << p.what << " one past";
  }
  // zero is allowed wherever the band allows zero; the spacing too (HX9's notice, not a refusal)
  {
    GSHSCENE_Hatch_Family f = family(0, 0, 0, 0, 0);
    EXPECT_EQ(w.intern(w.base(&f, 1)), GSHSCENE_OK);
  }
  // a dash and the map
  {
    std::vector<double> d = {hi, -lo};
    GSHSCENE_Hatch_Family f = family(0, 0, 0, 0, 1, &d);
    EXPECT_EQ(w.intern(w.base(&f, 1)), GSHSCENE_OK);
    d[0] = std::ldexp(1.0, 65);
    EXPECT_EQ(w.intern(w.base(&f, 1)), GSHSCENE_ERR_LIMIT);
    d[0] = std::ldexp(1.0, -65);
    EXPECT_EQ(w.intern(w.base(&f, 1)), GSHSCENE_ERR_LIMIT);
    GSHSCENE_Hatch_Family g = family();
    GSHSCENE_Hatch h = w.base(&g, 1);
    h.map = scale_map(hi, lo);
    EXPECT_EQ(w.intern(h), GSHSCENE_OK);
    h.map = scale_map(std::ldexp(1.0, 65), 1.0);
    EXPECT_EQ(w.intern(h), GSHSCENE_ERR_LIMIT);
    {
      // quarter turns past what ::gshgeom_sincos takes are a limit although the angle is within its own band
      GSHSCENE_Hatch_Family t = family(std::ldexp(1.0, 39));
      GSHSCENE_Hatch ht = w.base(&t, 1);
      ht.angle_unit = GSHGEOM_ANGLE_TURNS;
      EXPECT_EQ(w.intern(ht), GSHSCENE_ERR_LIMIT);
      t.angle = std::ldexp(1.0, 38);
      EXPECT_EQ(w.intern(ht), GSHSCENE_OK);
    }
    h.map = translate(std::ldexp(1.0, 64), -std::ldexp(1.0, 64));
    EXPECT_EQ(w.intern(h), GSHSCENE_OK) << "a translation at the bound";
    h.map = translate(0.0, std::ldexp(1.0, 65));
    EXPECT_EQ(w.intern(h), GSHSCENE_ERR_LIMIT) << "a translation past it";
    h.map = translate(std::ldexp(1.0, 65), 0.0);
    EXPECT_EQ(w.intern(h), GSHSCENE_ERR_LIMIT);
  }
  // a solid ink within N1's bound
  {
    GSHSCENE_Hatch_Family f = family();
    GSHSCENE_Hatch h = w.base(&f, 1);
    h.ink = solid(0, 0, 0, 1);
    h.ink.rgba[0] = 1e300;
    EXPECT_EQ(w.intern(h), GSHSCENE_ERR_LIMIT);
  }
}

TEST(Hatch, TheCountsAreBoundedByTheStoresLimitAndByTheirOwn) {
  GSHSCENE_Limits limits;
  std::memset(&limits, 0, sizeof limits);
  limits.max_resource_elements = 4;
  World w(&limits);
  std::vector<GSHSCENE_Hatch_Family> fams(5, family());
  EXPECT_EQ(w.intern(w.base(fams.data(), 4)), GSHSCENE_OK);
  EXPECT_EQ(w.intern(w.base(fams.data(), 5)), GSHSCENE_ERR_LIMIT) << "the store's limit";
  std::vector<double> five(5, 1.0), four(4, 1.0);
  GSHSCENE_Hatch_Family f = family(0, 0, 0, 0, 1, &four);
  EXPECT_EQ(w.intern(w.base(&f, 1)), GSHSCENE_OK);
  GSHSCENE_Hatch_Family g = family(0, 0, 0, 0, 1, &five);
  EXPECT_EQ(w.intern(w.base(&g, 1)), GSHSCENE_ERR_LIMIT);
  World big;
  std::vector<GSHSCENE_Hatch_Family> many(257, family());
  EXPECT_EQ(big.intern(big.base(many.data(), 256)), GSHSCENE_OK);
  EXPECT_EQ(big.intern(big.base(many.data(), 257)), GSHSCENE_ERR_LIMIT);
  std::vector<double> d65535(65535, 1.0), d65536(65536, 1.0);
  GSHSCENE_Hatch_Family a = family(0, 0, 0, 0, 1, &d65535);
  EXPECT_EQ(big.intern(big.base(&a, 1)), GSHSCENE_OK);
  GSHSCENE_Hatch_Family b = family(0, 0, 0, 0, 1, &d65536);
  EXPECT_EQ(big.intern(big.base(&b, 1)), GSHSCENE_ERR_LIMIT);
}

// --- equality and canonical form --------------------------------------------------------------------------------------------

static void equal_content_is_one_handle(World & w) {
  std::vector<double> d = {2.0, -1.0};
  GSHSCENE_Hatch_Family f = family(30, 1, 2, 0.5, 2.5, &d);
  GSHSCENE_Hatch h = w.base(&f, 1);
  Ref a, b;
  ASSERT_EQ(w.intern(h, &a), GSHSCENE_OK);
  ASSERT_EQ(w.intern(h, &b), GSHSCENE_OK);
  EXPECT_EQ(a.get(), b.get());
  auto differs = [&](const char * what, const std::function<void(GSHSCENE_Hatch &, GSHSCENE_Hatch_Family &)> & edit) {
    std::vector<double> d2 = d;
    GSHSCENE_Hatch_Family g = family(30, 1, 2, 0.5, 2.5, &d2);
    GSHSCENE_Hatch hh = w.base(&g, 1);
    edit(hh, g);
    Ref c;
    ASSERT_EQ(w.intern(hh, &c), GSHSCENE_OK) << what;
    EXPECT_NE(c.get(), a.get()) << what;
  };
  differs("one ulp of the angle", [](GSHSCENE_Hatch &, GSHSCENE_Hatch_Family & g) { g.angle = std::nextafter(30.0, 31.0); });
  differs("origin", [](GSHSCENE_Hatch &, GSHSCENE_Hatch_Family & g) { g.origin_y = 2.25; });
  differs("along", [](GSHSCENE_Hatch &, GSHSCENE_Hatch_Family & g) { g.offset_along = 0.25; });
  differs("across", [](GSHSCENE_Hatch &, GSHSCENE_Hatch_Family & g) { g.offset_across = 2.0; });
  differs("a dash", [&](GSHSCENE_Hatch &, GSHSCENE_Hatch_Family & g) { const_cast<double *>(g.dashes)[1] = -1.5; });
  differs("a dash more", [&](GSHSCENE_Hatch &, GSHSCENE_Hatch_Family & g) {
    static std::vector<double> longer = {2.0, -1.0, 0.0};
    g.dashes = longer.data();
    g.dash_count = 3;
  });
  differs("unit", [](GSHSCENE_Hatch & hh, GSHSCENE_Hatch_Family &) { hh.angle_unit = GSHGEOM_ANGLE_RADIANS; });
  differs("nesting", [](GSHSCENE_Hatch & hh, GSHSCENE_Hatch_Family &) { hh.nesting = GSHSCENE_HATCH_NESTING_IGNORE; });
  differs("map", [](GSHSCENE_Hatch & hh, GSHSCENE_Hatch_Family &) { hh.map = translate(1, 0); });
  differs("ink", [](GSHSCENE_Hatch & hh, GSHSCENE_Hatch_Family &) { hh.ink = solid(0, 0, 1, 1); });
  differs("ink kind", [](GSHSCENE_Hatch & hh, GSHSCENE_Hatch_Family &) { hh.ink = solid(0, 0, 0, 1); hh.ink.kind = GSHSCENE_PAINT_FOREGROUND; });
  differs("the style", [&](GSHSCENE_Hatch & hh, GSHSCENE_Hatch_Family &) {
    static Ref other;
    StyleSpec sp;
    sp.width = 3.0;
    other = Ref(make_style(w.s.store, sp));
    hh.style = other.get();
  });
  differs("ink alpha", [](GSHSCENE_Hatch & hh, GSHSCENE_Hatch_Family &) { hh.ink = solid(0, 0, 0, 0.5); });
  differs("ink space", [](GSHSCENE_Hatch & hh, GSHSCENE_Hatch_Family &) { hh.ink = solid(0, 0, 0, 1); hh.ink.space = space_of(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_SRGB); });
  differs("ink palette entry", [](GSHSCENE_Hatch & hh, GSHSCENE_Hatch_Family &) { hh.ink = solid(0, 0, 0, 1); hh.ink.kind = GSHSCENE_PAINT_PALETTE; hh.ink.entry = 1; });
  differs("another palette entry", [](GSHSCENE_Hatch & hh, GSHSCENE_Hatch_Family &) { hh.ink = solid(0, 0, 0, 1); hh.ink.kind = GSHSCENE_PAINT_PALETTE; hh.ink.entry = 2; });
  differs("map linear part", [](GSHSCENE_Hatch & hh, GSHSCENE_Hatch_Family &) { hh.map = scale_map(2.0, 1.0); });
  differs("map shear", [](GSHSCENE_Hatch & hh, GSHSCENE_Hatch_Family &) { hh.map = scale_map(1.0, 1.0); hh.map.m[0][1] = 0.5; hh.map.kind = 0; });
  differs("map translation y", [](GSHSCENE_Hatch & hh, GSHSCENE_Hatch_Family &) { hh.map = translate(0, 1); });
  differs("an origin x", [](GSHSCENE_Hatch &, GSHSCENE_Hatch_Family & g) { g.origin_x = 1.25; });
  differs("a second family", [&](GSHSCENE_Hatch & hh, GSHSCENE_Hatch_Family &) {
    static GSHSCENE_Hatch_Family two[2];
    two[0] = family(30, 1, 2, 0.5, 2.5, &d);
    two[1] = family(30, 1, 2, 0.5, 2.5, &d);
    hh.families = two;
    hh.family_count = 2;
  });
  differs("the second family", [&](GSHSCENE_Hatch & hh, GSHSCENE_Hatch_Family &) {
    static GSHSCENE_Hatch_Family two[2];
    static std::vector<double> d3 = {2.0, -1.0};
    two[0] = family(30, 1, 2, 0.5, 2.5, &d);
    two[1] = family(30, 1, 2, 0.5, 2.5, &d3);
    two[1].angle = 60;
    hh.families = two;
    hh.family_count = 2;
  });
  // the elements are not merged: [1, 1, -1] and [2, -1] are two handles
  std::vector<double> one = {1.0, 1.0, -1.0}, two = {2.0, -1.0};
  GSHSCENE_Hatch_Family f1 = family(0, 0, 0, 0, 1, &one), f2 = family(0, 0, 0, 0, 1, &two);
  Ref r1, r2;
  ASSERT_EQ(w.intern(w.base(&f1, 1), &r1), GSHSCENE_OK);
  ASSERT_EQ(w.intern(w.base(&f2, 1), &r2), GSHSCENE_OK);
  EXPECT_NE(r1.get(), r2.get());
  GSHSCENE_Hatch back;
  ASSERT_EQ(gshscene_resource_hatch(r1.get(), &back), GSHSCENE_OK);
  EXPECT_EQ(back.families[0].dash_count, 3u);
  // -0 is stored as +0
  GSHSCENE_Hatch_Family z = family(-0.0, -0.0, -0.0, -0.0, 1.0);
  GSHSCENE_Hatch_Family p = family(0.0, 0.0, 0.0, 0.0, 1.0);
  Ref rz, rp;
  ASSERT_EQ(w.intern(w.base(&z, 1), &rz), GSHSCENE_OK);
  ASSERT_EQ(w.intern(w.base(&p, 1), &rp), GSHSCENE_OK);
  EXPECT_EQ(rz.get(), rp.get());
  ASSERT_EQ(gshscene_resource_hatch(rz.get(), &back), GSHSCENE_OK);
  EXPECT_FALSE(std::signbit(back.families[0].angle));
  EXPECT_FALSE(std::signbit(back.families[0].origin_x));
  EXPECT_FALSE(std::signbit(back.families[0].origin_y));
  EXPECT_FALSE(std::signbit(back.families[0].offset_along));
  GSHSCENE_Hatch h0 = w.base(&p, 1);
  h0.map = translate(-0.0, 0.0);
  Ref rm;
  ASSERT_EQ(w.intern(h0, &rm), GSHSCENE_OK);
  EXPECT_FALSE(std::signbit(rm.get() ? [&] { GSHSCENE_Hatch x; gshscene_resource_hatch(rm.get(), &x); return x.map.t[0]; }() : 1.0));
}

TEST(Hatch, EqualContentIsOneHandleAndEveryDifferenceIsAnother) {
  World w;
  equal_content_is_one_handle(w);
}

TEST(Hatch, OnlyTheContentDecidesWhenEveryHashCollides) {
  // every hatch in one bucket: equality alone tells a hatch from each of its neighbors, field by field
  World w(nullptr, all_zero);
  equal_content_is_one_handle(w);
}

// --- HX2: the hash and the dump against the independent generator -----------------------------------------------------------------

TEST(Hatch, HashAndDumpAreWhatTheIndependentGeneratorSays) {
  World w;
  GSHSCENE_Resource_Kind unused = GSHSCENE_RESOURCE_HATCH;
  (void)unused;
  // the styles of the generator's cases
  StyleSpec dev;
  dev.width = 0.75;
  dev.width_space = GSHSCENE_STROKE_WIDTH_DEVICE;
  dev.join = GSHSCENE_STROKE_JOIN_ROUND;
  dev.miter = 10.0;
  dev.cap = GSHSCENE_STROKE_CAP_SQUARE;
  dev.degenerate = GSHSCENE_STROKE_DEGENERATE_OMIT;
  Ref style_default(make_style(w.s.store, StyleSpec()));
  Ref style_device(make_style(w.s.store, dev));
  ASSERT_EQ(gshscene_resource_hash(style_default.get()), known::kStrokeDefaultHash);
  GSHGEOM_Map design;
  gshgeom_map_design_to_model(2.5, 10, 20, &design);
  GSHGEOM_Map tr = translate(1.5, -2.25);
  std::vector<double> d1 = {3.0, -1.0}, d2 = {0.0, -1.5, 2.0}, d3 = {std::ldexp(1.0, -64), -std::ldexp(1.0, 64)};
  GSHSCENE_Hatch_Family f0[1] = {family(45.0, 0, 0, 0, 1.5)};
  GSHSCENE_Hatch_Family f1[2] = {family(0.0, 1.0, 2.0, 0.5, 2.0, &d1), family(90.0, 0, 0, 0, 2.0, &d2)};
  GSHSCENE_Hatch_Family f2[1] = {family(0.7853981633974483, -2.5, 0.25, 0.0, 0.0)};
  GSHSCENE_Hatch_Family f3[1] = {family(0.125, std::ldexp(1.0, -64), std::ldexp(1.0, 64), -std::ldexp(1.0, 64), std::ldexp(1.0, -64), &d3)};
  GSHSCENE_Hatch h[4] = {w.base(f0, 1), w.base(f1, 2), w.base(f2, 1), w.base(f3, 1)};
  h[1].nesting = GSHSCENE_HATCH_NESTING_OUTER;
  h[1].style = style_device.get();
  h[1].map = tr;
  h[1].ink = solid(0, 0, 0, 0.5);
  h[1].ink.kind = GSHSCENE_PAINT_FOREGROUND;
  h[2].angle_unit = GSHGEOM_ANGLE_RADIANS;
  h[2].nesting = GSHSCENE_HATCH_NESTING_IGNORE;
  h[2].map = design;
  h[2].ink = solid(0, 0, 0, 1);
  h[2].ink.kind = GSHSCENE_PAINT_PALETTE;
  h[2].ink.entry = 3;
  h[3].angle_unit = GSHGEOM_ANGLE_TURNS;
  h[3].ink = solid(0.25, 0.5, 0.75, 1.0);
  h[3].ink.space = space_of(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_SRGB);
  const char * dumps[4] = {known::kHatchDump0, known::kHatchDump1, known::kHatchDump2, known::kHatchDump3};
  Ref held[4];
  for (int i = 0; i < 4; i++) {
    ASSERT_EQ(w.intern(h[i], &held[i]), GSHSCENE_OK) << i;
    EXPECT_EQ(gshscene_resource_hash(held[i].get()), known::kHatchHashes[i]) << i;
    EXPECT_EQ(dump_resource(held[i].get()), dumps[i]) << i;
  }
}

// --- HX3: the paint, and where it is allowed ----------------------------------------------------------------------------------------

TEST(Hatch, APaintOfTheKindIsAllowedOnAFillAndNotOnAStrokeOrAStopOrATint) {
  World w;
  GSHSCENE_Hatch_Family f = family();
  Ref hr;
  ASSERT_EQ(w.intern(w.base(&f, 1), &hr), GSHSCENE_OK);
  GSHSCENE_Paint p = hatch_paint(hr.get(), 0.5);
  ListHolder l(w.s.store);
  EXPECT_EQ(gshscene_list_fill(l.list, w.sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0), GSHSCENE_OK);
  const GSHSCENE_Item * item = nullptr;
  ASSERT_EQ(gshscene_list_item(l.list, 0, &item), GSHSCENE_OK);
  EXPECT_EQ(item->paint.kind, GSHSCENE_PAINT_HATCH);
  EXPECT_EQ(item->paint.resource, hr.get());
  EXPECT_EQ(item->paint.rgba[3], 0.5);
  // on a stroke: unsupported at record
  Ref style(make_style(w.s.store, StyleSpec()));
  EXPECT_EQ(gshscene_list_stroke(l.list, w.sq.get(), style.get(), nullptr, &p, 0, 0), GSHSCENE_ERR_UNSUPPORTED);
  EXPECT_EQ(gshscene_list_count(l.list), 1u) << "nothing recorded";
  // a paint of the wrong resource kind, another store's resource, a missing resource
  GSHSCENE_Paint wrong = p;
  wrong.resource = w.sq.get();
  EXPECT_EQ(gshscene_list_fill(l.list, w.sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &wrong, 0, 0), GSHSCENE_ERR_INVALID);
  wrong.resource = nullptr;
  EXPECT_EQ(gshscene_list_fill(l.list, w.sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &wrong, 0, 0), GSHSCENE_ERR_INVALID);
  World other;
  GSHSCENE_Hatch_Family g = family();
  Ref foreign;
  ASSERT_EQ(other.intern(other.base(&g, 1), &foreign), GSHSCENE_OK);
  GSHSCENE_Paint fp = hatch_paint(foreign.get());
  EXPECT_EQ(gshscene_list_fill(l.list, w.sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &fp, 0, 0), GSHSCENE_ERR_INVALID);
  // the spelling: rgb zero, the entry zero, the alpha in [0, 1]
  GSHSCENE_Paint bad = p;
  bad.rgba[0] = 0.5;
  EXPECT_EQ(gshscene_list_fill(l.list, w.sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &bad, 0, 0), GSHSCENE_ERR_INVALID);
  bad = p;
  bad.entry = 1;
  EXPECT_EQ(gshscene_list_fill(l.list, w.sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &bad, 0, 0), GSHSCENE_ERR_INVALID);
  bad = p;
  bad.rgba[3] = 1.5;
  EXPECT_EQ(gshscene_list_fill(l.list, w.sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &bad, 0, 0), GSHSCENE_ERR_INVALID);
  // a 3D map on the item
  GSHGEOM_Map m3;
  gshgeom_map_identity(3, &m3);
  EXPECT_EQ(gshscene_list_fill(l.list, w.sq.get(), &m3, GSHGEOM_FILL_NONZERO, &p, 0, 0), GSHSCENE_ERR_UNSUPPORTED);
  // a gradient stop and a tint of a pattern are plain colors
  GSHSCENE_Gradient grad{};
  gshgeom_map_identity(2, &grad.map);
  GSHSCENE_Gradient_Stop stops[2];
  std::memset(stops, 0, sizeof stops);
  stops[0].color = solid(0, 0, 0, 1);
  stops[1].offset = 1.0;
  stops[1].color = p;
  grad.kind = GSHSCENE_GRADIENT_LINEAR;
  grad.stop_count = 2;
  grad.stops = stops;
  grad.p1[0] = 1.0;
  const GSHSCENE_Resource * gr = nullptr;
  EXPECT_EQ(gshscene_store_intern_gradient(w.s.store, &grad, &gr), GSHSCENE_ERR_INVALID);
  gshscene_resource_release(gr);
}

TEST(Hatch, APlaneHasNoBoundaryToTrimTo) {
  World w;
  GSHSCENE_Hatch_Family f = family();
  Ref hr;
  ASSERT_EQ(w.intern(w.base(&f, 1), &hr), GSHSCENE_OK);
  const GSHSCENE_Resource * plane = nullptr;
  ASSERT_EQ(gshscene_store_intern_plane(w.s.store, &plane), GSHSCENE_OK);
  Ref pr(plane);
  GSHSCENE_Paint p = hatch_paint(hr.get());
  ListHolder l(w.s.store);
  EXPECT_EQ(gshscene_list_fill(l.list, plane, nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0), GSHSCENE_ERR_UNSUPPORTED);
}

// --- HX9: what draws nothing -------------------------------------------------------------------------------------------------

namespace {

/** A fill painted with a hatch, built into a list; the item is read back from it. */
struct Unit {
  ListHolder l;
  const GSHSCENE_Item * item = nullptr;
  Unit(GSHSCENE_Store * store, const GSHSCENE_Resource * region, const GSHGEOM_Map * map, const GSHSCENE_Resource * hatch, double alpha = 1.0)
      : l(store) {
    GSHSCENE_Paint p = hatch_paint(hatch, alpha);
    EXPECT_EQ(gshscene_list_fill(l.list, region, map, GSHGEOM_FILL_NONZERO, &p, 0, 0), GSHSCENE_OK);
    EXPECT_EQ(gshscene_list_item(l.list, 0, &item), GSHSCENE_OK);
  }
};

}  // namespace

TEST(Hatch, TheNoticeIsTheFirstOfTheMapsThenTheWidthThenTheSpacing) {
  World w;
  GSHSCENE_Hatch_Family fine = family();
  GSHSCENE_Hatch_Family zero_dy[2] = {family(0, 0, 0, 0, 1), family(90, 0, 0, 0, 0)};
  Ref ok, degenerate;
  ASSERT_EQ(w.intern(w.base(&fine, 1), &ok), GSHSCENE_OK);
  ASSERT_EQ(w.intern(w.base(zero_dy, 2), &degenerate), GSHSCENE_OK);
  Unit plain(w.s.store, w.sq.get(), nullptr, ok.get());
  EXPECT_EQ(gshscene_hatch_notice(plain.item, nullptr, 0, nullptr), (GSHSCENE_Reason)0);
  Unit deg(w.s.store, w.sq.get(), nullptr, degenerate.get());
  EXPECT_EQ(gshscene_hatch_notice(deg.item, nullptr, 0, nullptr), GSHSCENE_REASON_HATCH_DEGENERATE);
  // the same through the one predicate every sink calls for a fill
  EXPECT_EQ(gshscene_paint_notice(deg.item, nullptr, 0, nullptr), GSHSCENE_REASON_HATCH_DEGENERATE);
  // width 0
  Ref zero_style(make_style(w.s.store, [] { StyleSpec s; s.width = 0; return s; }()));
  GSHSCENE_Hatch zh = w.base(&fine, 1);
  zh.style = zero_style.get();
  Ref zr;
  ASSERT_EQ(w.intern(zh, &zr), GSHSCENE_OK);
  Unit zero(w.s.store, w.sq.get(), nullptr, zr.get());
  EXPECT_EQ(gshscene_hatch_notice(zero.item, nullptr, 0, nullptr), GSHSCENE_REASON_STROKE_ZERO_WIDTH);
  // each map of the chain on its own: the target's, an instance's, the item's, the hatch's
  GSHGEOM_Map sing = scale_map(0.0, 1.0);
  EXPECT_EQ(gshscene_hatch_notice(plain.item, nullptr, 0, &sing), GSHSCENE_REASON_MAP_NOT_INVERTIBLE);
  GSHGEOM_Map inst[2] = {scale_map(2, 2), sing};
  EXPECT_EQ(gshscene_hatch_notice(plain.item, inst, 2, nullptr), GSHSCENE_REASON_MAP_NOT_INVERTIBLE);
  Unit sing_item(w.s.store, w.sq.get(), &sing, ok.get());
  EXPECT_EQ(gshscene_hatch_notice(sing_item.item, nullptr, 0, nullptr), GSHSCENE_REASON_MAP_NOT_INVERTIBLE);
  GSHSCENE_Hatch sh = w.base(&fine, 1);
  sh.map = sing;
  Ref sr;
  ASSERT_EQ(w.intern(sh, &sr), GSHSCENE_OK);
  Unit sing_hatch(w.s.store, w.sq.get(), nullptr, sr.get());
  EXPECT_EQ(gshscene_hatch_notice(sing_hatch.item, nullptr, 0, nullptr), GSHSCENE_REASON_MAP_NOT_INVERTIBLE);
  // the maps beat the width, the width beats the spacing
  GSHSCENE_Hatch both = w.base(zero_dy, 2);
  both.style = zero_style.get();
  Ref br;
  ASSERT_EQ(w.intern(both, &br), GSHSCENE_OK);
  Unit width_and_spacing(w.s.store, w.sq.get(), nullptr, br.get());
  EXPECT_EQ(gshscene_hatch_notice(width_and_spacing.item, nullptr, 0, nullptr), GSHSCENE_REASON_STROKE_ZERO_WIDTH);
  EXPECT_EQ(gshscene_hatch_notice(width_and_spacing.item, nullptr, 0, &sing), GSHSCENE_REASON_MAP_NOT_INVERTIBLE);
  // factors that compose to a zero determinant are not what is asked: each is tested alone
  GSHGEOM_Map tiny = scale_map(std::ldexp(1.0, -600), 1.0);
  EXPECT_EQ(gshscene_hatch_notice(plain.item, &tiny, 1, &tiny), (GSHSCENE_Reason)0);
  // an alpha of zero, or a region with no interior, is no notice
  Unit transparent(w.s.store, w.sq.get(), nullptr, ok.get(), 0.0);
  EXPECT_EQ(gshscene_hatch_notice(transparent.item, nullptr, 0, nullptr), (GSHSCENE_Reason)0);
  // not a hatch fill: nothing
  ListHolder l(w.s.store);
  GSHSCENE_Paint s = solid(0, 0, 0, 1);
  gshscene_list_fill(l.list, w.sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &s, 0, 0);
  const GSHSCENE_Item * it = nullptr;
  gshscene_list_item(l.list, 0, &it);
  EXPECT_EQ(gshscene_hatch_notice(it, nullptr, 0, nullptr), (GSHSCENE_Reason)0);
  EXPECT_EQ(gshscene_hatch_notice(nullptr, nullptr, 0, nullptr), (GSHSCENE_Reason)0);
  EXPECT_EQ(gshscene_hatch_notice(plain.item, nullptr, 1, nullptr), (GSHSCENE_Reason)0) << "a count without maps";
}

// --- HX15: the bounds -----------------------------------------------------------------------------------------------------------------

namespace {

GSHGEOM_Bounds box_of(double x0, double y0, double x1, double y1) {
  GSHGEOM_Bounds b{};
  b.empty = 0;
  b.min = P(x0, y0);
  b.max = P(x1, y1);
  return b;
}

}  // namespace

TEST(Hatch, TheBoundsAreTheRegionsOutsetAsAStrokesAreWithoutAJoin) {
  World w;
  struct Case {
    const char * what;
    StyleSpec spec;
    double reach;  // in half widths
  };
  StyleSpec butt;
  butt.width = 2.0;
  StyleSpec round;
  round.width = 2.0;
  round.cap = GSHSCENE_STROKE_CAP_ROUND;
  StyleSpec square;
  square.width = 2.0;
  square.cap = GSHSCENE_STROKE_CAP_SQUARE;
  StyleSpec mitered;  // a miter join of limit 10 would outset a stroke by ten half widths; a hatch has no join
  mitered.width = 2.0;
  mitered.miter = 10.0;
  const Case cases[] = {{"butt", butt, 1.0}, {"round", round, 1.0}, {"square", square, 1.5}, {"a miter join, which a hatch never makes", mitered, 1.0}};
  GSHGEOM_Map id = translate(0, 0);
  for (const Case & c : cases) {
    Ref st(make_style(w.s.store, c.spec));
    GSHGEOM_Bounds region = box_of(1, 2, 5, 8), out{};
    ASSERT_EQ(gshscene_hatch_bounds(st.get(), &region, &id, GSHSCENE_UNIT_DEFAULT, &out), GSHSCENE_OK) << c.what;
    ASSERT_EQ(out.empty, 0);
    const double h = c.reach * 1.0;  // half the width 2
    EXPECT_LE(out.min.x, 1 - h) << c.what;
    EXPECT_LE(out.min.y, 2 - h) << c.what;
    EXPECT_GE(out.max.x, 5 + h) << c.what;
    EXPECT_GE(out.max.y, 8 + h) << c.what;
    EXPECT_GE(out.min.x, 1 - h - 1e-9) << c.what << ": not loose";
    EXPECT_LE(out.max.y, 8 + h + 1e-9) << c.what;
  }
  // a stroke of the mitered style is bigger (the join), so the two functions are not one
  Ref ms(make_style(w.s.store, mitered));
  GSHGEOM_Bounds region = box_of(1, 2, 5, 8), sb{}, hb{};
  ASSERT_EQ(gshscene_stroke_bounds(ms.get(), &region, &id, GSHSCENE_UNIT_DEFAULT, &sb), GSHSCENE_OK);
  ASSERT_EQ(gshscene_hatch_bounds(ms.get(), &region, &id, GSHSCENE_UNIT_DEFAULT, &hb), GSHSCENE_OK);
  EXPECT_GT(hb.min.x, sb.min.x);
  // an alignment other than center is the stroke's doubling: a hatch has none and its style refuses one
  // a device width is in pixels around the mapped region; a local width goes through the map
  StyleSpec dev;
  dev.width = 4.0;
  dev.width_space = GSHSCENE_STROKE_WIDTH_DEVICE;
  Ref ds(make_style(w.s.store, dev));
  GSHGEOM_Map two = scale_map(2, 2);
  GSHGEOM_Bounds d{};
  ASSERT_EQ(gshscene_hatch_bounds(ds.get(), &region, &two, GSHSCENE_UNIT_PIXEL, &d), GSHSCENE_OK);
  EXPECT_LE(d.min.x, 2.0 - 2.0);
  EXPECT_GE(d.max.x, 10.0 + 2.0);
  Ref ls(make_style(w.s.store, butt));
  ASSERT_EQ(gshscene_hatch_bounds(ls.get(), &region, &two, GSHSCENE_UNIT_PIXEL, &d), GSHSCENE_OK);
  EXPECT_LE(d.min.x, 2.0 - 2.0) << "the width 2 is doubled by the map";
  // an empty region, a width of zero
  GSHGEOM_Bounds empty{};
  empty.empty = 1;
  ASSERT_EQ(gshscene_hatch_bounds(ls.get(), &empty, &id, GSHSCENE_UNIT_DEFAULT, &d), GSHSCENE_OK);
  EXPECT_EQ(d.empty, 1);
  Ref zs(make_style(w.s.store, [] { StyleSpec s; s.width = 0; return s; }()));
  ASSERT_EQ(gshscene_hatch_bounds(zs.get(), &region, &id, GSHSCENE_UNIT_DEFAULT, &d), GSHSCENE_OK);
  EXPECT_EQ(d.empty, 1);
  // arguments
  EXPECT_EQ(gshscene_hatch_bounds(nullptr, &region, &id, GSHSCENE_UNIT_DEFAULT, &d), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_hatch_bounds(w.sq.get(), &region, &id, GSHSCENE_UNIT_DEFAULT, &d), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_hatch_bounds(ls.get(), nullptr, &id, GSHSCENE_UNIT_DEFAULT, &d), GSHSCENE_ERR_INVALID);
}

// --- HX14: a sink's disposition -------------------------------------------------------------------------------------------------------

namespace {

GSHSCENE_Sink_Caps all_caps(bool hatch, bool expand = true, uint32_t native = GSHSCENE_STROKE_NATIVE_ALL) {
  GSHSCENE_Sink_Caps caps;
  std::memset(&caps, 0, sizeof caps);
  caps.item_kinds = 0xFFFFFFFFu;
  caps.composites = 0xFFFFFFFFu;
  caps.stroke_native = native;
  caps.gradient_kinds = 0xe;
  caps.image_filters = 0xf;
  caps.image_extends = 0xf;
  caps.image_formats = GSHSCENE_IMAGE_FORMAT_ALL;
  caps.mask_native = GSHSCENE_MASK_NATIVE_ALL;
  caps.flags = GSHSCENE_CAP_CLIP_AA | GSHSCENE_CAP_BLEND_SPACES | GSHSCENE_CAP_GRADIENT_SPACES | GSHSCENE_CAP_GLYPHS_FROM_FONT |
      GSHSCENE_CAP_STROKE_RUNS | GSHSCENE_CAP_IMAGE_FILTER_SPACES | (hatch ? GSHSCENE_CAP_HATCH : 0u) |
      (expand ? GSHSCENE_CAP_STROKE_EXPAND : 0u);
  caps.cancel_unit = GSHSCENE_CANCEL_BETWEEN_UNITS;
  caps.work_measure = "units";
  return caps;
}

GSHSCENE_Disposition dispose(const GSHSCENE_Sink_Caps & caps, const GSHSCENE_Item * item, const GSHGEOM_Map * maps = nullptr,
    size_t n = 0, const GSHSCENE_Resource * palette = nullptr) {
  GSHSCENE_Color_Space srgb = srgb_space();
  return gshscene_caps_disposition(&caps, item, maps, n, nullptr, &srgb, palette);
}

}  // namespace

TEST(HatchSink, AFillPaintedWithAHatchIsRefusedWithoutTheFlagWhateverElseIsTrueOfIt) {
  World w;
  GSHSCENE_Hatch_Family f = family();
  Ref hr;
  ASSERT_EQ(w.intern(w.base(&f, 1), &hr), GSHSCENE_OK);
  Unit u(w.s.store, w.sq.get(), nullptr, hr.get());
  GSHSCENE_Disposition without = dispose(all_caps(false), u.item);
  EXPECT_EQ(without.entry_class, GSHSCENE_ENTRY_REFUSED);
  EXPECT_EQ(without.reason, GSHSCENE_REASON_HATCH);
  GSHSCENE_Disposition with = dispose(all_caps(true), u.item);
  EXPECT_EQ((int)with.entry_class, 0);
  // the flag before a pen the sink cannot express
  GSHSCENE_Disposition neither = dispose(all_caps(false, false, 0), u.item);
  EXPECT_EQ(neither.reason, GSHSCENE_REASON_HATCH);
}

TEST(HatchSink, ThePenIsCheckedAsAStrokesIsAndOnlyForASinkThatDoesNotExpand) {
  World w;
  GSHSCENE_Hatch_Family f = family();
  StyleSpec sq;
  sq.cap = GSHSCENE_STROKE_CAP_SQUARE;
  sq.width_space = GSHSCENE_STROKE_WIDTH_DEVICE;
  Ref st(make_style(w.s.store, sq));
  GSHSCENE_Hatch h = w.base(&f, 1);
  h.style = st.get();
  Ref hr;
  ASSERT_EQ(w.intern(h, &hr), GSHSCENE_OK);
  Unit u(w.s.store, w.sq.get(), nullptr, hr.get());
  // a sink that draws hatches by expanding them needs nothing of the pen
  EXPECT_EQ((int)dispose(all_caps(true, true, 0), u.item).entry_class, 0);
  // one that strokes the pieces names what it lacks
  GSHSCENE_Disposition d = dispose(all_caps(true, false, GSHSCENE_STROKE_NATIVE_CAP_BUTT | GSHSCENE_STROKE_NATIVE_WIDTH_LOCAL), u.item);
  EXPECT_EQ(d.entry_class, GSHSCENE_ENTRY_REFUSED);
  EXPECT_EQ(d.reason, GSHSCENE_REASON_STROKE_STYLE);
  EXPECT_EQ(d.detail, (uint64_t)(GSHSCENE_STROKE_NATIVE_CAP_SQUARE | GSHSCENE_STROKE_NATIVE_WIDTH_DEVICE));
  EXPECT_EQ(d.detail & GSHSCENE_STROKE_NATIVE_DEGENERATE_DRAW, 0u) << "a hatch with no dot makes no degenerate subpath";
  EXPECT_EQ((int)dispose(all_caps(true, false, GSHSCENE_STROKE_NATIVE_CAP_SQUARE | GSHSCENE_STROKE_NATIVE_WIDTH_DEVICE), u.item).entry_class, 0)
      << "no join is required: the pieces have none";
  // the required features are the style's cap and width space and not its join
  EXPECT_EQ(gshscene_hatch_required(u.item, nullptr, 0, nullptr), GSHSCENE_STROKE_NATIVE_CAP_SQUARE | GSHSCENE_STROKE_NATIVE_WIDTH_DEVICE);
  // a dot under a round or square cap is a degenerate shape the sink must draw, whatever the style's own `degenerate` says
  {
    std::vector<double> dots = {0.0, -1.0};
    GSHSCENE_Hatch_Family df = family(0, 0, 0, 0, 1, &dots);
    GSHSCENE_Hatch dh = w.base(&df, 1);
    dh.style = st.get();
    Ref dr;
    ASSERT_EQ(w.intern(dh, &dr), GSHSCENE_OK);
    Unit du(w.s.store, w.sq.get(), nullptr, dr.get());
    EXPECT_EQ(gshscene_hatch_required(du.item, nullptr, 0, nullptr),
        GSHSCENE_STROKE_NATIVE_CAP_SQUARE | GSHSCENE_STROKE_NATIVE_WIDTH_DEVICE | GSHSCENE_STROKE_NATIVE_DEGENERATE_DRAW);
    StyleSpec bt;
    Ref bs(make_style(w.s.store, bt));
    GSHSCENE_Hatch bh = w.base(&df, 1);
    bh.style = bs.get();
    Ref br;
    ASSERT_EQ(w.intern(bh, &br), GSHSCENE_OK);
    Unit bu(w.s.store, w.sq.get(), nullptr, br.get());
    EXPECT_EQ(gshscene_hatch_required(bu.item, nullptr, 0, nullptr) & GSHSCENE_STROKE_NATIVE_DEGENERATE_DRAW, 0u) << "a butt dot draws nothing";
  }
  // an anisotropic local width under a non-uniform map
  StyleSpec loc;
  Ref ls(make_style(w.s.store, loc));
  GSHSCENE_Hatch lh = w.base(&f, 1);
  lh.style = ls.get();
  Ref lr;
  ASSERT_EQ(w.intern(lh, &lr), GSHSCENE_OK);
  GSHGEOM_Map aniso = scale_map(2, 1);
  Unit au(w.s.store, w.sq.get(), &aniso, lr.get());
  EXPECT_NE(gshscene_hatch_required(au.item, nullptr, 0, nullptr) & GSHSCENE_STROKE_NATIVE_WIDTH_LOCAL_ANISOTROPIC, 0u);
  // a fill that draws nothing requires nothing
  GSHGEOM_Map sing = scale_map(0, 1);
  Unit nothing(w.s.store, w.sq.get(), &sing, lr.get());
  EXPECT_EQ(gshscene_hatch_required(nothing.item, nullptr, 0, nullptr), 0u);
}

TEST(HatchSink, AnInkOfAPaletteEntryTheListLacksRefusesTheFill) {
  World w;
  GSHSCENE_Hatch_Family f = family();
  GSHSCENE_Hatch h = w.base(&f, 1);
  h.ink = solid(0, 0, 0, 1);
  h.ink.kind = GSHSCENE_PAINT_PALETTE;
  h.ink.entry = 2;
  Ref hr;
  ASSERT_EQ(w.intern(h, &hr), GSHSCENE_OK);
  Unit u(w.s.store, w.sq.get(), nullptr, hr.get());
  GSHSCENE_Disposition d = dispose(all_caps(true), u.item);
  EXPECT_EQ(d.entry_class, GSHSCENE_ENTRY_REFUSED);
  EXPECT_EQ(d.reason, GSHSCENE_REASON_PALETTE_ENTRY);
  // with the entry in the palette in force, the fill is let through
  GSHSCENE_Color colors[3];
  for (auto & c : colors) {
    gshscene_color_init(&c);
  }
  const GSHSCENE_Resource * pal = nullptr;
  ASSERT_EQ(gshscene_store_intern_palette(w.s.store, 3, colors, &pal), GSHSCENE_OK);
  Ref palette(pal);
  EXPECT_EQ((int)dispose(all_caps(true), u.item, nullptr, 0, palette.get()).entry_class, 0);
}

// --- a retained scene: damage and validation -----------------------------------------------------------------------------------------------

TEST(HatchScene, ANodeCanBeHatchedAndAStrokeNodeCannot) {
  SceneHolder s;
  GSHSCENE_Store * store = s.store();
  Ref sq(intern_square(store));
  Ref style(make_style(store, StyleSpec()));
  GSHSCENE_Hatch_Family f = family();
  GSHSCENE_Hatch h;
  gshscene_hatch_init(&h);
  h.style = style.get();
  h.family_count = 1;
  h.families = &f;
  const GSHSCENE_Resource * hr_raw = nullptr;
  ASSERT_EQ(gshscene_store_intern_hatch(store, &h, &hr_raw), GSHSCENE_OK);
  Ref hr(hr_raw);
  EditHolder e(s);
  GSHSCENE_Node n = shape_node(sq.get());
  n.paint = hatch_paint(hr.get(), 0.5);
  EXPECT_NE(add(e.edit, GSHSCENE_ROOT_ID, n), 0u);
  GSHSCENE_Node st = stroke_node(sq.get(), style.get());
  st.paint = hatch_paint(hr.get());
  uint64_t id = 0;
  EXPECT_EQ(gshscene_edit_add(e.edit, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, &st, nullptr, &id), GSHSCENE_ERR_UNSUPPORTED);
  const GSHSCENE_Resource * plane = nullptr;
  ASSERT_EQ(gshscene_store_intern_plane(store, &plane), GSHSCENE_OK);
  Ref pr(plane);
  GSHSCENE_Node pn = shape_node(plane);
  pn.paint = hatch_paint(hr.get());
  EXPECT_NE(gshscene_edit_add(e.edit, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, &pn, nullptr, &id), GSHSCENE_OK) << "a node names no plane";
  publish(s, e);
}

// --- the rules the planted defects pinned down (check-planted.py, ht- plants) ----------------------------------------------------------

TEST(Hatch, EveryZeroIsStoredPositiveAndEqualIsDecidedByTheContent) {
  World w(nullptr, all_zero);  // one bucket: the equality alone decides
  std::vector<double> neg = {-0.0, -1.0, 2.0}, pos = {0.0, -1.0, 2.0};
  GSHSCENE_Hatch_Family fn = family(-0.0, -0.0, -0.0, -0.0, 1.5, &neg), fp = family(0.0, 0.0, 0.0, 0.0, 1.5, &pos);
  Ref a, b;
  GSHSCENE_Hatch hn = w.base(&fn, 1), hp = w.base(&fp, 1);
  ASSERT_EQ(w.intern(hn, &a), GSHSCENE_OK);
  ASSERT_EQ(w.intern(hp, &b), GSHSCENE_OK);
  EXPECT_EQ(a.get(), b.get()) << "a dot spelled -0";
  GSHSCENE_Hatch back;
  ASSERT_EQ(gshscene_resource_hatch(a.get(), &back), GSHSCENE_OK);
  EXPECT_FALSE(std::signbit(back.families[0].dashes[0]));
  // the spacing and the map and the ink
  GSHSCENE_Hatch_Family zn = family(10, 0, 0, 0, -0.0), zp = family(10, 0, 0, 0, 0.0);
  Ref c, d;
  ASSERT_EQ(w.intern(w.base(&zn, 1), &c), GSHSCENE_OK);
  ASSERT_EQ(w.intern(w.base(&zp, 1), &d), GSHSCENE_OK);
  EXPECT_EQ(c.get(), d.get()) << "a spacing of -0";
  ASSERT_EQ(gshscene_resource_hatch(c.get(), &back), GSHSCENE_OK);
  EXPECT_FALSE(std::signbit(back.families[0].offset_across));
  GSHSCENE_Hatch_Family f = family();
  GSHSCENE_Hatch hm = w.base(&f, 1);
  hm.map = scale_map(1.0, 1.0);
  hm.map.m[0][1] = -0.0;
  hm.map.m[1][0] = -0.0;
  hm.map.t[0] = -0.0;
  hm.map.t[1] = -0.0;
  hm.map.kind = 0;
  hm.ink = solid(-0.0, -0.0, -0.0, 1.0);
  GSHSCENE_Hatch hz = w.base(&f, 1);
  hz.map = scale_map(1.0, 1.0);
  hz.map.kind = 0;
  hz.ink = solid(0.0, 0.0, 0.0, 1.0);
  Ref e, g;
  ASSERT_EQ(w.intern(hm, &e), GSHSCENE_OK);
  ASSERT_EQ(w.intern(hz, &g), GSHSCENE_OK);
  EXPECT_EQ(e.get(), g.get()) << "a map and an ink of -0";
  ASSERT_EQ(gshscene_resource_hatch(e.get(), &back), GSHSCENE_OK);
  EXPECT_FALSE(std::signbit(back.map.m[0][1]));
  EXPECT_FALSE(std::signbit(back.map.m[1][0]));
  EXPECT_FALSE(std::signbit(back.map.t[0]));
  EXPECT_FALSE(std::signbit(back.map.t[1]));
  EXPECT_FALSE(std::signbit(back.ink.rgba[0]));
  EXPECT_FALSE(std::signbit(back.ink.rgba[2]));
}

TEST(Hatch, TwoMapsOfTheSameKindAreToldApartByTheirTranslationAndTheirKindAlone) {
  World w(nullptr, all_zero);
  GSHSCENE_Hatch_Family f = family();
  auto handle = [&](const GSHSCENE_Hatch & h) {
    Ref r;
    EXPECT_EQ(w.intern(h, &r), GSHSCENE_OK);
    return r;
  };
  GSHSCENE_Hatch a = w.base(&f, 1), b = w.base(&f, 1), c = w.base(&f, 1), d = w.base(&f, 1);
  a.map = translate(1.0, 0.0);
  b.map = translate(2.0, 0.0);
  c.map = translate(1.0, 0.5);
  Ref ra = handle(a), rb = handle(b), rc = handle(c);
  EXPECT_NE(ra.get(), rb.get());
  EXPECT_NE(ra.get(), rc.get());
  GSHSCENE_Hatch ident = w.base(&f, 1);
  d.map = ident.map;
  d.map.kind = GSHGEOM_MAP_SCALES;  // the identity matrix with another kind
  Ref r0 = handle(ident), r1 = handle(d);
  EXPECT_NE(r0.get(), r1.get()) << "the kind is part of the map";
  // palette inks of two entries
  GSHSCENE_Hatch p1 = w.base(&f, 1), p2 = w.base(&f, 1);
  p1.ink = solid(0, 0, 0, 1);
  p1.ink.kind = GSHSCENE_PAINT_PALETTE;
  p1.ink.entry = 1;
  p2.ink = p1.ink;
  p2.ink.entry = 2;
  Ref q1 = handle(p1), q2 = handle(p2);
  EXPECT_NE(q1.get(), q2.get());
}

TEST(Hatch, AHandleReleasesItsStyleAndItsCopiesWhenTheLastReferenceGoes) {
  tracker::Tracker t;
  GSHSCENE_Allocator alloc = tracker::allocator_for(&t);
  {
    StoreHolder s(nullptr, &alloc);
    ASSERT_NE(s.store, nullptr);
    {
      Ref style(make_style(s.store, StyleSpec()));
      std::vector<double> d = {1.0, -1.0, 0.0};
      GSHSCENE_Hatch_Family f = family(20, 0, 0, 0, 1.0, &d);
      GSHSCENE_Hatch h;
      gshscene_hatch_init(&h);
      h.style = style.get();
      h.family_count = 1;
      h.families = &f;
      Ref hr;
      const GSHSCENE_Resource * raw = nullptr;
      ASSERT_EQ(gshscene_store_intern_hatch(s.store, &h, &raw), GSHSCENE_OK);
      hr = Ref(raw);
      GSHSCENE_Store_Stats st;
      ASSERT_EQ(gshscene_store_stats(s.store, &st), GSHSCENE_OK);
      EXPECT_EQ(st.live, 2u);
    }
    GSHSCENE_Store_Stats st;
    ASSERT_EQ(gshscene_store_stats(s.store, &st), GSHSCENE_OK);
    EXPECT_EQ(st.live, 0u) << "the hatch and then its style are freed";
  }
  EXPECT_TRUE(t.live.empty());
}

TEST(Hatch, AnInkThatReadsTheForegroundDamagesTheFillWhenTheForegroundChanges) {
  SceneHolder sh;
  GSHSCENE_Store * store = sh.store();
  Ref sq(intern_square(store));
  Ref style(make_style(store, StyleSpec()));
  GSHSCENE_Hatch_Family f = family();
  auto damaged = [&](GSHSCENE_Paint_Kind ink_kind) {
    GSHSCENE_Hatch h;
    gshscene_hatch_init(&h);
    h.style = style.get();
    h.family_count = 1;
    h.families = &f;
    h.ink = solid(0, 0, 0, 1);
    h.ink.kind = ink_kind;
    const GSHSCENE_Resource * raw = nullptr;
    EXPECT_EQ(gshscene_store_intern_hatch(store, &h, &raw), GSHSCENE_OK);
    Ref hr(raw);
    SceneHolder own(nullptr, nullptr, store);
    VersionRef v1, v2;
    {
      EditHolder e(own);
      GSHSCENE_Node n = shape_node(sq.get());
      n.paint = hatch_paint(hr.get());
      EXPECT_NE(add(e.edit, GSHSCENE_ROOT_ID, n), 0u);
      GSHSCENE_Color black = {{0, 0, 0, 1}, {}};
      EXPECT_EQ(gshscene_edit_set_foreground(e.edit, &black), GSHSCENE_OK);
      publish(own, e, &v1);
    }
    {
      EditHolder e(own, v1.get());
      GSHSCENE_Color red = {{1, 0, 0, 1}, {}};
      EXPECT_EQ(gshscene_edit_set_foreground(e.edit, &red), GSHSCENE_OK);
      publish(own, e, &v2);
    }
    GSHSCENE_Diff * diff = nullptr;
    EXPECT_EQ(gshscene_diff(v1.get(), v2.get(), nullptr, nullptr, &diff), GSHSCENE_OK);
    GSHSCENE_Damage damage;
    EXPECT_EQ(gshscene_diff_damage(diff, &damage), GSHSCENE_OK);
    gshscene_diff_destroy(diff);
    return !damage.empty;
  };
  EXPECT_TRUE(damaged(GSHSCENE_PAINT_FOREGROUND));
  EXPECT_FALSE(damaged(GSHSCENE_PAINT_SOLID));
}

TEST(Hatch, TheNoticeAndTheRequirementAreOnlyOfAFillPaintedWithAHatch) {
  World w;
  Ref zero(make_style(w.s.store, [] { StyleSpec s; s.width = 0; return s; }()));
  GSHSCENE_Hatch_Family f = family();
  GSHSCENE_Hatch h = w.base(&f, 1);
  h.style = zero.get();
  Ref hr;
  ASSERT_EQ(w.intern(h, &hr), GSHSCENE_OK);
  Unit u(w.s.store, w.sq.get(), nullptr, hr.get());
  EXPECT_EQ(gshscene_hatch_notice(u.item, nullptr, 0, nullptr), GSHSCENE_REASON_STROKE_ZERO_WIDTH);
  // the same item with another kind, or another paint, or a hatch paint that names another kind of resource, is nothing of a hatch's
  GSHSCENE_Item as_stroke = *u.item;
  as_stroke.kind = GSHSCENE_ITEM_STROKE;
  EXPECT_EQ((int)gshscene_hatch_notice(&as_stroke, nullptr, 0, nullptr), 0);
  EXPECT_EQ(gshscene_hatch_required(&as_stroke, nullptr, 0, nullptr), 0u);
  GSHSCENE_Item solid_paint = *u.item;
  solid_paint.paint.kind = GSHSCENE_PAINT_SOLID;
  EXPECT_EQ((int)gshscene_hatch_notice(&solid_paint, nullptr, 0, nullptr), 0);
  GSHSCENE_Item wrong = *u.item;
  wrong.paint.resource = w.sq.get();
  EXPECT_EQ((int)gshscene_hatch_notice(&wrong, nullptr, 0, nullptr), 0);
  EXPECT_EQ(gshscene_hatch_required(&wrong, nullptr, 0, nullptr), 0u);
  EXPECT_EQ((int)gshscene_hatch_notice(nullptr, nullptr, 0, nullptr), 0);
  // instances need their maps
  GSHGEOM_Map maps[1] = {scale_map(1, 1)};
  EXPECT_EQ((int)gshscene_hatch_notice(u.item, nullptr, 1, nullptr), 0) << "a count without maps is not looked at";
  EXPECT_EQ(gshscene_hatch_notice(u.item, maps, 1, nullptr), GSHSCENE_REASON_STROKE_ZERO_WIDTH);
  // a hatch whose pen has width and a round cap needs a degenerate shape only for a dot; spaces are not dots
  Ref round(make_style(w.s.store, [] { StyleSpec s; s.cap = GSHSCENE_STROKE_CAP_ROUND; return s; }()));
  std::vector<double> spaces = {2.0, -1.0}, dotted = {2.0, -1.0, 0.0};
  for (int dot = 0; dot < 2; dot++) {
    GSHSCENE_Hatch_Family g = family(0, 0, 0, 0, 1.0, dot ? &dotted : &spaces);
    GSHSCENE_Hatch hh = w.base(&g, 1);
    hh.style = round.get();
    Ref r;
    ASSERT_EQ(w.intern(hh, &r), GSHSCENE_OK);
    Unit v(w.s.store, w.sq.get(), nullptr, r.get());
    EXPECT_EQ((gshscene_hatch_required(v.item, nullptr, 0, nullptr) & GSHSCENE_STROKE_NATIVE_DEGENERATE_DRAW) != 0, dot == 1) << dot;
    EXPECT_NE(gshscene_hatch_required(v.item, nullptr, 0, nullptr) & GSHSCENE_STROKE_NATIVE_CAP_ROUND, 0u);
    // never a join
    EXPECT_EQ(gshscene_hatch_required(v.item, nullptr, 0, nullptr) & (GSHSCENE_STROKE_NATIVE_JOIN_MITER | GSHSCENE_STROKE_NATIVE_JOIN_ROUND |
                  GSHSCENE_STROKE_NATIVE_JOIN_BEVEL | GSHSCENE_STROKE_NATIVE_JOIN_MITER_CLIP | GSHSCENE_STROKE_NATIVE_JOIN_ARCS), 0u);
    // and a map that cannot be inverted, or a zero width, needs nothing
    GSHGEOM_Map sing = scale_map(0, 1);
    Unit x(w.s.store, w.sq.get(), &sing, r.get());
    EXPECT_EQ(gshscene_hatch_required(x.item, nullptr, 0, nullptr), 0u);
  }
  // the pen of a hatch with a butt cap and a dot needs no degenerate shape (a butt end draws nothing either way)
  std::vector<double> dot_only = {0.0, -2.0};
  GSHSCENE_Hatch_Family g = family(0, 0, 0, 0, 1.0, &dot_only);
  GSHSCENE_Hatch hb = w.base(&g, 1);
  Ref br;
  ASSERT_EQ(w.intern(hb, &br), GSHSCENE_OK);
  Unit y(w.s.store, w.sq.get(), nullptr, br.get());
  EXPECT_EQ(gshscene_hatch_required(y.item, nullptr, 0, nullptr) & GSHSCENE_STROKE_NATIVE_DEGENERATE_DRAW, 0u);
  // the zero-width hatch needs nothing
  Unit z(w.s.store, w.sq.get(), nullptr, hr.get());
  EXPECT_EQ(gshscene_hatch_required(z.item, nullptr, 0, nullptr), 0u);
}


TEST(Hatch, AThreeDimensionalRegionOrMapIsUnsupportedAndSoIsAPlaneInANode) {
  World w;
  GSHSCENE_Hatch_Family f = family();
  Ref hr;
  ASSERT_EQ(w.intern(w.base(&f, 1), &hr), GSHSCENE_OK);
  GSHGEOM_Path * p3 = nullptr;
  ASSERT_EQ(gshgeom_path_create(nullptr, nullptr, 3, &p3), GSHGEOM_OK);
  ASSERT_EQ(gshgeom_path_move_to(p3, P(0, 0, 0)), GSHGEOM_OK);
  ASSERT_EQ(gshgeom_path_line_to(p3, P(4, 0, 1)), GSHGEOM_OK);
  ASSERT_EQ(gshgeom_path_line_to(p3, P(4, 4, 2)), GSHGEOM_OK);
  ASSERT_EQ(gshgeom_path_close(p3), GSHGEOM_OK);
  const GSHSCENE_Resource * raw = nullptr;
  ASSERT_EQ(gshscene_store_intern_path(w.s.store, p3, &raw), GSHSCENE_OK);
  gshgeom_path_destroy(p3);
  Ref region3(raw);
  GSHSCENE_Paint p = hatch_paint(hr.get());
  {
    ListHolder l(w.s.store);
    EXPECT_EQ(gshscene_list_fill(l.list, region3.get(), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0), GSHSCENE_ERR_UNSUPPORTED) << "a 3D region";
    GSHGEOM_Map m3;
    ASSERT_EQ(gshgeom_map_identity(3, &m3), GSHGEOM_OK);
    EXPECT_EQ(gshscene_list_fill(l.list, w.sq.get(), &m3, GSHGEOM_FILL_NONZERO, &p, 0, 0), GSHSCENE_ERR_UNSUPPORTED) << "a 3D map";
    EXPECT_EQ(gshscene_list_fill(l.list, w.sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0), GSHSCENE_OK);
  }
  SceneHolder s(nullptr, nullptr, w.s.store);
  EditHolder e(s);
  GSHSCENE_Node n = shape_node(region3.get());
  n.paint = p;
  uint64_t id = 0;
  EXPECT_EQ(gshscene_edit_add(e.edit, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, &n, nullptr, &id), GSHSCENE_ERR_UNSUPPORTED) << "a 3D region in a node";
  const GSHSCENE_Resource * plane = nullptr;
  ASSERT_EQ(gshscene_store_intern_plane(w.s.store, &plane), GSHSCENE_OK);
  Ref pr(plane);
  GSHSCENE_Node pn = shape_node(plane);
  pn.paint = p;
  EXPECT_EQ(gshscene_edit_add(e.edit, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, &pn, nullptr, &id), GSHSCENE_ERR_INVALID) << "a node names no plane";
}

namespace {

/** The damage between a scene with one hatched fill of @p region (and the pen @p style) and the empty one. */
GSHSCENE_Damage hatch_damage(World & w, const GSHSCENE_Resource * region, const GSHSCENE_Resource * style, const GSHGEOM_Map * group_map = nullptr,
                             int groups = 0) {
  GSHSCENE_Hatch_Family f = family();
  GSHSCENE_Hatch h = w.base(&f, 1);
  h.style = style;
  Ref hr;
  EXPECT_EQ(w.intern(h, &hr), GSHSCENE_OK);
  SceneHolder own(nullptr, nullptr, w.s.store);
  VersionRef v1, v2;
  {
    EditHolder e(own);
    publish(own, e, &v1);
  }
  {
    EditHolder e(own, v1.get());
    uint64_t parent = GSHSCENE_ROOT_ID;
    for (int i = 0; i < groups; i++) {
      GSHSCENE_Node g = group_node();
      g.map = *group_map;
      parent = add(e.edit, parent, g);
      EXPECT_NE(parent, 0u);
    }
    GSHSCENE_Node n = shape_node(region);
    n.paint = hatch_paint(hr.get());
    EXPECT_NE(add(e.edit, parent, n), 0u);
    publish(own, e, &v2);
  }
  GSHSCENE_Diff * diff = nullptr;
  EXPECT_EQ(gshscene_diff(v1.get(), v2.get(), nullptr, nullptr, &diff), GSHSCENE_OK);
  GSHSCENE_Damage damage;
  EXPECT_EQ(gshscene_diff_damage(diff, &damage), GSHSCENE_OK);
  gshscene_diff_destroy(diff);
  return damage;
}

}  // namespace

TEST(HatchScene, TheDamageOfAHatchFillReachesPastItsRegionByThePen) {
  World w;
  Ref wide(make_style(w.s.store, [] { StyleSpec s; s.width = 8.0; s.cap = GSHSCENE_STROKE_CAP_SQUARE; return s; }()));
  GSHGEOM_Path * path = nullptr;
  ASSERT_EQ(gshgeom_path_create(nullptr, nullptr, 2, &path), GSHGEOM_OK);
  ASSERT_EQ(gshgeom_path_add_rect(path, 20, 20, 10, 10, 0, 0), GSHGEOM_OK);
  const GSHSCENE_Resource * raw = nullptr;
  ASSERT_EQ(gshscene_store_intern_path(w.s.store, path, &raw), GSHSCENE_OK);
  Ref region(raw);
  GSHSCENE_Damage d = hatch_damage(w, region.get(), wide.get());
  ASSERT_FALSE(d.unbounded);
  ASSERT_FALSE(d.empty);
  EXPECT_LE(d.rect.x0, 20 - 4) << "the half width of the pen past the left edge";
  EXPECT_LE(d.rect.y0, 20 - 4);
  EXPECT_GE(d.rect.x1, 30 + 4);
  EXPECT_GE(d.rect.y1, 30 + 4);
  gshgeom_path_destroy(path);
  // a box the composed map cannot take (a coordinate of 2^99 under fifteen groups of scale 2^63 overflows) leaves the damage unbounded
  GSHGEOM_Path * far = nullptr;
  ASSERT_EQ(gshgeom_path_create(nullptr, nullptr, 2, &far), GSHGEOM_OK);
  ASSERT_EQ(gshgeom_path_add_rect(far, std::ldexp(1.0, 99), std::ldexp(1.0, 99), std::ldexp(1.0, 98), std::ldexp(1.0, 98), 0, 0), GSHGEOM_OK);
  const GSHSCENE_Resource * raw3 = nullptr;
  ASSERT_EQ(gshscene_store_intern_path(w.s.store, far, &raw3), GSHSCENE_OK);
  Ref far_region(raw3);
  GSHGEOM_Map big = scale_map(std::ldexp(1.0, 63), std::ldexp(1.0, 63));
  GSHSCENE_Damage u = hatch_damage(w, far_region.get(), wide.get(), &big, 15);
  EXPECT_TRUE(u.unbounded);
  gshgeom_path_destroy(far);
  // a region with no extent (a lone move) has an empty box: no damage, and not the garbage of an empty box
  GSHGEOM_Path * lone = nullptr;
  ASSERT_EQ(gshgeom_path_create(nullptr, nullptr, 2, &lone), GSHGEOM_OK);
  ASSERT_EQ(gshgeom_path_move_to(lone, P(5, 5)), GSHGEOM_OK);
  const GSHSCENE_Resource * raw4 = nullptr;
  ASSERT_EQ(gshscene_store_intern_path(w.s.store, lone, &raw4), GSHSCENE_OK);
  Ref lone_region(raw4);
  GSHSCENE_Damage l = hatch_damage(w, lone_region.get(), wide.get());
  EXPECT_FALSE(l.unbounded);
  EXPECT_TRUE(l.empty);
  gshgeom_path_destroy(lone);
}

TEST(HatchScene, AnInkOfAPaletteEntryDamagesTheFillWhenThePaletteChanges) {
  World w;
  GSHSCENE_Hatch_Family f = family();
  GSHSCENE_Hatch h = w.base(&f, 1);
  h.ink = solid(0, 0, 0, 1);
  h.ink.kind = GSHSCENE_PAINT_PALETTE;
  h.ink.entry = 1;
  Ref hr;
  ASSERT_EQ(w.intern(h, &hr), GSHSCENE_OK);
  GSHSCENE_Color a[2] = {{{1, 0, 0, 1}, {}}, {{0, 1, 0, 1}, {}}};
  GSHSCENE_Color b[2] = {{{1, 0, 0, 1}, {}}, {{0, 0, 1, 1}, {}}};
  const GSHSCENE_Resource * pa = nullptr;
  const GSHSCENE_Resource * pb = nullptr;
  ASSERT_EQ(gshscene_store_intern_palette(w.s.store, 2, a, &pa), GSHSCENE_OK);
  ASSERT_EQ(gshscene_store_intern_palette(w.s.store, 2, b, &pb), GSHSCENE_OK);
  Ref ra(pa), rb(pb);
  SceneHolder own(nullptr, nullptr, w.s.store);
  VersionRef v1, v2;
  {
    EditHolder e(own);
    GSHSCENE_Node n = shape_node(w.sq.get());
    n.paint = hatch_paint(hr.get());
    EXPECT_NE(add(e.edit, GSHSCENE_ROOT_ID, n), 0u);
    EXPECT_EQ(gshscene_edit_set_palette(e.edit, pa), GSHSCENE_OK);
    publish(own, e, &v1);
  }
  {
    EditHolder e(own, v1.get());
    EXPECT_EQ(gshscene_edit_set_palette(e.edit, pb), GSHSCENE_OK);
    publish(own, e, &v2);
  }
  GSHSCENE_Diff * diff = nullptr;
  ASSERT_EQ(gshscene_diff(v1.get(), v2.get(), nullptr, nullptr, &diff), GSHSCENE_OK);
  GSHSCENE_Damage damage;
  ASSERT_EQ(gshscene_diff_damage(diff, &damage), GSHSCENE_OK);
  gshscene_diff_destroy(diff);
  EXPECT_FALSE(damage.empty);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
