/**
 * @file
 *
 * Strokes at the waist (strokes story S1; SK1 to SK14 of stroke.h and documentation/design.md): the stroke style
 * resource (its defaults, its validity in SK2's order, each bound at both sides, SK3's canonical form, interning,
 * hash and dump against the independent generator, a planted collision, the store's accounting).
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "test_helpers.h"

#include "../alloc_tracker.h"
#include "../known_answers.h"
#include "../scene_helpers.h"

#include <cmath>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

using namespace scene;

namespace {

const double kNaN = std::nan("");
const double kInf = HUGE_VAL;

/** A style description that owns its dashes. */
struct Style {
  GSHSCENE_Stroke_Style s{};
  std::vector<double> dashes;
  Style() {
    gshscene_stroke_style_init(&s);
  }
  Style & dash(std::vector<double> d) {
    dashes = std::move(d);
    return *this;
  }
  const GSHSCENE_Stroke_Style * get() {
    s.dash_count = dashes.size();
    s.dashes = dashes.empty() ? nullptr : dashes.data();
    return &s;
  }
};

GSHSCENE_Result intern(GSHSCENE_Store * store, Style & st, Ref * out = nullptr) {
  const GSHSCENE_Resource * r = nullptr;
  GSHSCENE_Result result = gshscene_store_intern_stroke_style(store, st.get(), &r);
  if (out) {
    *out = Ref(r);
  } else {
    gshscene_resource_release(r);
  }
  return result;
}

/** The generator's fixtures (tools/gen-scene-vectors.py), built through the API. */
Style fixture_default() {
  return Style();
}

Style fixture_device() {
  Style st;
  st.s.width = 0.75;
  st.s.width_space = GSHSCENE_STROKE_WIDTH_DEVICE;
  st.s.join = GSHSCENE_STROKE_JOIN_ROUND;
  st.s.miter_limit = 10.0;
  st.s.cap = GSHSCENE_STROKE_CAP_SQUARE;
  st.s.degenerate = GSHSCENE_STROKE_DEGENERATE_OMIT;
  st.dash({1.0, 2.0, 3.5});
  st.s.dash_offset = -0.5;
  st.s.dash_scope = GSHSCENE_STROKE_DASH_PATH;
  st.s.alignment = GSHSCENE_STROKE_ALIGN_INSIDE;
  st.s.alignment_rule = GSHGEOM_FILL_EVEN_ODD;
  return st;
}

Style fixture_clip() {
  Style st;
  st.s.width = -0.0;
  st.s.join = GSHSCENE_STROKE_JOIN_MITER_CLIP;
  st.s.miter_limit = 2.5;
  st.s.cap = GSHSCENE_STROKE_CAP_ROUND;
  st.dash({0.0, -0.0});
  st.s.dash_offset = 5.0;
  st.s.dash_scope = GSHSCENE_STROKE_DASH_PATH;
  st.s.alignment_rule = GSHGEOM_FILL_EVEN_ODD;
  return st;
}

Style fixture_arcs() {
  Style st;
  st.s.width = 3.0;
  st.s.join = GSHSCENE_STROKE_JOIN_ARCS;
  st.s.miter_limit = 65536.0;
  st.dash({4.0, -1.0});
  st.s.alignment = GSHSCENE_STROKE_ALIGN_OUTSIDE;
  return st;
}

uint64_t bits_of(double x) {
  uint64_t b;
  std::memcpy(&b, &x, sizeof b);
  return b;
}

}  // namespace

/* --- SK1: the defaults and the pinned values ---------------------------------------------- */

TEST(StrokeStyle, InitWritesTheDefaultsOfSk1) {
  GSHSCENE_Stroke_Style s;
  std::memset(&s, 0xab, sizeof s);
  gshscene_stroke_style_init(&s);
  EXPECT_EQ(s.width, 1.0);
  EXPECT_EQ(s.width_space, (uint32_t)GSHSCENE_STROKE_WIDTH_LOCAL);
  EXPECT_EQ(s.join, (uint32_t)GSHSCENE_STROKE_JOIN_MITER);
  EXPECT_EQ(s.miter_limit, 4.0);
  EXPECT_EQ(s.cap, (uint32_t)GSHSCENE_STROKE_CAP_BUTT);
  EXPECT_EQ(s.degenerate, (uint32_t)GSHSCENE_STROKE_DEGENERATE_DRAW);
  EXPECT_EQ(s.dash_count, 0u);
  EXPECT_EQ(s.dashes, nullptr);
  EXPECT_EQ(bits_of(s.dash_offset), 0u);
  EXPECT_EQ(s.dash_scope, (uint32_t)GSHSCENE_STROKE_DASH_PER_SUBPATH);
  EXPECT_EQ(s.alignment, (uint32_t)GSHSCENE_STROKE_ALIGN_CENTER);
  EXPECT_EQ(s.alignment_rule, GSHGEOM_FILL_NONZERO);
  EXPECT_EQ(s.width_profile, nullptr);
  gshscene_stroke_style_init(nullptr); // ignored
}

TEST(StrokeStyle, ThePinnedValuesAreSk1s) {
  // PDF's j and J, then SVG 2's: hashed, so pinned.
  EXPECT_EQ((int)GSHSCENE_STROKE_WIDTH_LOCAL, 0);
  EXPECT_EQ((int)GSHSCENE_STROKE_WIDTH_DEVICE, 1);
  EXPECT_EQ((int)GSHSCENE_STROKE_WIDTH_SPACE_COUNT, 2);
  EXPECT_EQ((int)GSHSCENE_STROKE_JOIN_MITER, 0);
  EXPECT_EQ((int)GSHSCENE_STROKE_JOIN_ROUND, 1);
  EXPECT_EQ((int)GSHSCENE_STROKE_JOIN_BEVEL, 2);
  EXPECT_EQ((int)GSHSCENE_STROKE_JOIN_MITER_CLIP, 3);
  EXPECT_EQ((int)GSHSCENE_STROKE_JOIN_ARCS, 4);
  EXPECT_EQ((int)GSHSCENE_STROKE_JOIN_COUNT, 5);
  EXPECT_EQ((int)GSHSCENE_STROKE_CAP_BUTT, 0);
  EXPECT_EQ((int)GSHSCENE_STROKE_CAP_ROUND, 1);
  EXPECT_EQ((int)GSHSCENE_STROKE_CAP_SQUARE, 2);
  EXPECT_EQ((int)GSHSCENE_STROKE_CAP_COUNT, 3);
  EXPECT_EQ((int)GSHSCENE_STROKE_DEGENERATE_DRAW, 0);
  EXPECT_EQ((int)GSHSCENE_STROKE_DEGENERATE_OMIT, 1);
  EXPECT_EQ((int)GSHSCENE_STROKE_DEGENERATE_COUNT, 2);
  EXPECT_EQ((int)GSHSCENE_STROKE_DASH_PER_SUBPATH, 0);
  EXPECT_EQ((int)GSHSCENE_STROKE_DASH_PATH, 1);
  EXPECT_EQ((int)GSHSCENE_STROKE_DASH_SCOPE_COUNT, 2);
  EXPECT_EQ((int)GSHSCENE_STROKE_ALIGN_CENTER, 0);
  EXPECT_EQ((int)GSHSCENE_STROKE_ALIGN_INSIDE, 1);
  EXPECT_EQ((int)GSHSCENE_STROKE_ALIGN_OUTSIDE, 2);
  EXPECT_EQ((int)GSHSCENE_STROKE_ALIGN_COUNT, 3);
  EXPECT_EQ(GSHSCENE_STROKE_VALUE_MIN, std::ldexp(1.0, -64));
  EXPECT_EQ(GSHSCENE_STROKE_VALUE_MAX, std::ldexp(1.0, 64));
  EXPECT_EQ(GSHSCENE_STROKE_MITER_LIMIT_MAX, 65536.0);
  EXPECT_EQ(GSHSCENE_STROKE_MITER_LIMIT_DEFAULT, 4.0);
  EXPECT_EQ(GSHSCENE_STROKE_DASHES_MAX, 65535u);
  EXPECT_EQ(GSHSCENE_BYTES_PER_DASH, 16u);
}

TEST(StrokeStyle, AZeroFilledStyleIsNotValid) {
  StoreHolder s;
  GSHSCENE_Stroke_Style z;
  std::memset(&z, 0, sizeof z);
  const GSHSCENE_Resource * r = nullptr;
  // Its width 0 is legal (SK8); its miter limit 0 is out of range.
  EXPECT_EQ(gshscene_store_intern_stroke_style(s.store, &z, &r), GSHSCENE_ERR_LIMIT);
  EXPECT_EQ(r, nullptr);
  z.miter_limit = 1.0;
  ASSERT_EQ(gshscene_store_intern_stroke_style(s.store, &z, &r), GSHSCENE_OK);
  gshscene_resource_release(r);
}

/* --- SK12: hash and dump against the independent generator ------------------------------- */

TEST(StrokeStyle, HashAndDumpAreTheGeneratorsOnEveryFixture) {
  StoreHolder s;
  struct Case {
    Style st;
    uint64_t hash;
    const char * dump;
  } cases[] = {
      {fixture_default(), known::kStrokeDefaultHash, known::kStrokeDefaultDump},
      {fixture_device(), known::kStrokeDeviceHash, known::kStrokeDeviceDump},
      {fixture_clip(), known::kStrokeClipHash, known::kStrokeClipDump},
      {fixture_arcs(), known::kStrokeArcsHash, known::kStrokeArcsDump},
  };
  for (Case & c : cases) {
    Ref r;
    ASSERT_EQ(intern(s.store, c.st, &r), GSHSCENE_OK);
    EXPECT_EQ(gshscene_resource_kind(r.get()), GSHSCENE_RESOURCE_STROKE_STYLE);
    EXPECT_EQ(gshscene_resource_hash(r.get()), c.hash);
    EXPECT_EQ(dump_resource(r.get()), std::string(c.dump));
  }
}

TEST(StrokeStyle, TheAccessorReadsBackTheCanonicalForm) {
  StoreHolder s;
  Style st = fixture_device();
  Ref r;
  ASSERT_EQ(intern(s.store, st, &r), GSHSCENE_OK);
  GSHSCENE_Stroke_Style out;
  ASSERT_EQ(gshscene_resource_stroke_style(r.get(), &out), GSHSCENE_OK);
  EXPECT_EQ(out.width, 0.75);
  EXPECT_EQ(out.width_space, (uint32_t)GSHSCENE_STROKE_WIDTH_DEVICE);
  EXPECT_EQ(out.join, (uint32_t)GSHSCENE_STROKE_JOIN_ROUND);
  EXPECT_EQ(out.miter_limit, 4.0) << "SK3 (b): a round join reads no miter limit";
  EXPECT_EQ(out.cap, (uint32_t)GSHSCENE_STROKE_CAP_SQUARE);
  EXPECT_EQ(out.degenerate, (uint32_t)GSHSCENE_STROKE_DEGENERATE_OMIT);
  ASSERT_EQ(out.dash_count, 6u) << "SK3 (a): an odd array repeated once";
  const double want[6] = {1.0, 2.0, 3.5, 1.0, 2.0, 3.5};
  for (size_t i = 0; i < 6; i++) {
    EXPECT_EQ(out.dashes[i], want[i]) << i;
  }
  EXPECT_NE(out.dashes, st.dashes.data()) << "the resource holds its own copy";
  EXPECT_EQ(out.dash_offset, -0.5) << "SK3 (c): the offset is kept as given";
  EXPECT_EQ(out.dash_scope, (uint32_t)GSHSCENE_STROKE_DASH_PATH);
  EXPECT_EQ(out.alignment, (uint32_t)GSHSCENE_STROKE_ALIGN_INSIDE);
  EXPECT_EQ(out.alignment_rule, GSHGEOM_FILL_EVEN_ODD);
  EXPECT_EQ(out.width_profile, nullptr);
  // The wrong kind and NULL.
  Ref path(intern_square(s.store));
  EXPECT_EQ(gshscene_resource_stroke_style(path.get(), &out), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_resource_stroke_style(nullptr, &out), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_resource_stroke_style(r.get(), nullptr), GSHSCENE_ERR_INVALID);
}

/* --- SK3: the canonical form ----------------------------------------------------------- */

TEST(StrokeStyle, EqualCanonicalStylesAreOneHandle) {
  StoreHolder s;
  // Each group is one drawing spelled several ways: one handle and one hash.
  std::vector<std::vector<std::function<void(Style &)>>> groups = {
      // Dashes: none, all zero, a negative length, a lone -0: solid; the dash fields reset with them.
      {[](Style &) {}, [](Style & t) { t.dash({0.0, 0.0}); }, [](Style & t) { t.dash({1.0, -1.0}); },
       [](Style & t) { t.dash({-0.0}); }, [](Style & t) { t.s.dash_offset = 5.0; },
       [](Style & t) { t.s.dash_scope = GSHSCENE_STROKE_DASH_PATH; },
       [](Style & t) { t.dash({-2.0, 3.0, 4.0}).s.dash_offset = -7.0; },
       [](Style & t) { t.s.alignment_rule = GSHGEOM_FILL_EVEN_ODD; }},
      // An odd array is its doubled spelling.
      {[](Style & t) { t.dash({1.0, 2.0, 3.0}); }, [](Style & t) { t.dash({1.0, 2.0, 3.0, 1.0, 2.0, 3.0}); }},
      // A join that reads no miter limit: any limit in range is 4.
      {[](Style & t) { t.s.join = GSHSCENE_STROKE_JOIN_ROUND; },
       [](Style & t) { t.s.join = GSHSCENE_STROKE_JOIN_ROUND; t.s.miter_limit = 10.0; },
       [](Style & t) { t.s.join = GSHSCENE_STROKE_JOIN_ROUND; t.s.miter_limit = 1.0; }},
      {[](Style & t) { t.s.join = GSHSCENE_STROKE_JOIN_BEVEL; t.s.miter_limit = 65536.0; },
       [](Style & t) { t.s.join = GSHSCENE_STROKE_JOIN_BEVEL; }},
      // -0 is +0.
      {[](Style & t) { t.s.width = -0.0; }, [](Style & t) { t.s.width = 0.0; }},
      {[](Style & t) { t.dash({-0.0, 1.0}).s.dash_offset = -0.0; }, [](Style & t) { t.dash({0.0, 1.0}); }},
  };
  std::vector<uint64_t> hashes;
  for (auto & group : groups) {
    Ref first;
    for (size_t i = 0; i < group.size(); i++) {
      Style st;
      group[i](st);
      Ref r;
      ASSERT_EQ(intern(s.store, st, &r), GSHSCENE_OK) << i;
      if (i == 0) {
        first = Ref(gshscene_resource_retain(r.get()));
        hashes.push_back(gshscene_resource_hash(r.get()));
      } else {
        EXPECT_EQ(r.get(), first.get()) << "spelling " << i << " is another handle";
        EXPECT_EQ(gshscene_resource_hash(r.get()), gshscene_resource_hash(first.get()));
      }
    }
  }
  for (size_t i = 0; i < hashes.size(); i++) {
    for (size_t j = i + 1; j < hashes.size(); j++) {
      EXPECT_NE(hashes[i], hashes[j]) << i << " and " << j << " are different drawings";
    }
  }
  EXPECT_EQ(hashes[0], known::kStrokeDefaultHash);
}

TEST(StrokeStyle, FieldsThatApplyAreKept) {
  StoreHolder s;
  // The miter limit under the three joins that read it; the alignment rule off center; the offset and scope with dashes.
  for (uint32_t join : {GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_JOIN_MITER_CLIP, GSHSCENE_STROKE_JOIN_ARCS}) {
    Style st;
    st.s.join = join;
    st.s.miter_limit = 10.0;
    Ref r;
    ASSERT_EQ(intern(s.store, st, &r), GSHSCENE_OK);
    GSHSCENE_Stroke_Style out;
    ASSERT_EQ(gshscene_resource_stroke_style(r.get(), &out), GSHSCENE_OK);
    EXPECT_EQ(out.miter_limit, 10.0) << join;
  }
  for (uint32_t align : {GSHSCENE_STROKE_ALIGN_INSIDE, GSHSCENE_STROKE_ALIGN_OUTSIDE}) {
    Style st;
    st.s.alignment = align;
    st.s.alignment_rule = GSHGEOM_FILL_EVEN_ODD;
    Ref r;
    ASSERT_EQ(intern(s.store, st, &r), GSHSCENE_OK);
    GSHSCENE_Stroke_Style out;
    ASSERT_EQ(gshscene_resource_stroke_style(r.get(), &out), GSHSCENE_OK);
    EXPECT_EQ(out.alignment_rule, GSHGEOM_FILL_EVEN_ODD) << align;
  }
  Style st;
  st.dash({2.0, 1.0}).s.dash_offset = 123.0;
  st.s.dash_scope = GSHSCENE_STROKE_DASH_PATH;
  Ref r;
  ASSERT_EQ(intern(s.store, st, &r), GSHSCENE_OK);
  GSHSCENE_Stroke_Style out;
  ASSERT_EQ(gshscene_resource_stroke_style(r.get(), &out), GSHSCENE_OK);
  EXPECT_EQ(out.dash_offset, 123.0) << "not reduced modulo the period";
  EXPECT_EQ(out.dash_scope, (uint32_t)GSHSCENE_STROKE_DASH_PATH);
  EXPECT_EQ(out.dash_count, 2u);
  // A dash of zero length among others is a dash (a dot with a round cap), kept.
  Style dot;
  dot.dash({0.0, 3.0});
  ASSERT_EQ(intern(s.store, dot, &r), GSHSCENE_OK);
  ASSERT_EQ(gshscene_resource_stroke_style(r.get(), &out), GSHSCENE_OK);
  EXPECT_EQ(out.dash_count, 2u);
}

TEST(StrokeStyle, EachFieldChangedAloneIsAnotherHandleEvenUnderACollision) {
  // Every field of the canonical form, changed alone from the defaults: a different resource.  The store's index
  // sends every hash to one value, so the content alone (never a hash) decides.
  StoreHolder s(nullptr, nullptr, all_zero);
  std::vector<std::function<void(Style &)>> changes = {
      [](Style & t) { t.s.width = 2.0; },
      [](Style & t) { t.s.width = 0.0; },
      [](Style & t) { t.s.width_space = GSHSCENE_STROKE_WIDTH_DEVICE; },
      [](Style & t) { t.s.join = GSHSCENE_STROKE_JOIN_ROUND; },
      [](Style & t) { t.s.join = GSHSCENE_STROKE_JOIN_BEVEL; },
      [](Style & t) { t.s.join = GSHSCENE_STROKE_JOIN_MITER_CLIP; },
      [](Style & t) { t.s.join = GSHSCENE_STROKE_JOIN_ARCS; },
      [](Style & t) { t.s.miter_limit = 4.5; },
      [](Style & t) { t.s.cap = GSHSCENE_STROKE_CAP_ROUND; },
      [](Style & t) { t.s.cap = GSHSCENE_STROKE_CAP_SQUARE; },
      [](Style & t) { t.s.degenerate = GSHSCENE_STROKE_DEGENERATE_OMIT; },
      [](Style & t) { t.dash({1.0, 1.0}); },
      [](Style & t) { t.dash({1.0, 2.0}); },
      [](Style & t) { t.dash({1.0, 1.0, 1.0, 1.0}); },
      [](Style & t) { t.dash({1.0, 1.0}).s.dash_offset = 0.5; },
      [](Style & t) { t.dash({1.0, 1.0}).s.dash_scope = GSHSCENE_STROKE_DASH_PATH; },
      [](Style & t) { t.s.alignment = GSHSCENE_STROKE_ALIGN_INSIDE; },
      [](Style & t) { t.s.alignment = GSHSCENE_STROKE_ALIGN_OUTSIDE; },
      [](Style & t) { t.s.alignment = GSHSCENE_STROKE_ALIGN_INSIDE; t.s.alignment_rule = GSHGEOM_FILL_EVEN_ODD; },
  };
  std::vector<Ref> held;
  std::vector<uint64_t> hashes;
  Style base;
  held.emplace_back();
  ASSERT_EQ(intern(s.store, base, &held.back()), GSHSCENE_OK);
  hashes.push_back(gshscene_resource_hash(held.back().get()));
  for (size_t i = 0; i < changes.size(); i++) {
    Style st;
    changes[i](st);
    held.emplace_back();
    ASSERT_EQ(intern(s.store, st, &held.back()), GSHSCENE_OK) << i;
    hashes.push_back(gshscene_resource_hash(held.back().get()));
  }
  for (size_t i = 0; i < held.size(); i++) {
    for (size_t j = i + 1; j < held.size(); j++) {
      EXPECT_NE(held[i].get(), held[j].get()) << i << " and " << j << " were merged";
      EXPECT_NE(hashes[i], hashes[j]) << i << " and " << j;
    }
  }
  GSHSCENE_Store_Stats stats;
  ASSERT_EQ(gshscene_store_stats(s.store, &stats), GSHSCENE_OK);
  EXPECT_EQ(stats.live, held.size());
  EXPECT_GT(stats.collisions, 0u) << "the planted collision was examined, and refused by content";
  // An equal style is still found under the collision.
  Style again;
  again.s.cap = GSHSCENE_STROKE_CAP_SQUARE;
  Ref r;
  ASSERT_EQ(intern(s.store, again, &r), GSHSCENE_OK);
  EXPECT_EQ(r.get(), held[10].get());
}

/* --- SK2: validity, then bounds -------------------------------------------------------- */

TEST(StrokeStyle, EveryInvalidFieldIsInvalid) {
  StoreHolder s;
  std::vector<std::function<void(Style &)>> invalid = {
      [](Style & t) { t.s.width = kNaN; },
      [](Style & t) { t.s.width = kInf; },
      [](Style & t) { t.s.width = -kInf; },
      [](Style & t) { t.s.width = -1.0; },
      [](Style & t) { t.s.width = -std::ldexp(1.0, -64); },
      [](Style & t) { t.s.miter_limit = kNaN; },
      [](Style & t) { t.s.miter_limit = kInf; },
      [](Style & t) { t.s.dash_offset = kNaN; },
      [](Style & t) { t.s.dash_offset = -kInf; },
      [](Style & t) { t.dash({1.0, kNaN}); },
      [](Style & t) { t.dash({kInf, 1.0}); },
      [](Style & t) { t.dash({-1.0, kNaN}); }, // a NaN is invalid even where a negative length makes the array solid
      [](Style & t) { t.s.width_space = GSHSCENE_STROKE_WIDTH_SPACE_COUNT; },
      [](Style & t) { t.s.join = GSHSCENE_STROKE_JOIN_COUNT; },
      [](Style & t) { t.s.cap = GSHSCENE_STROKE_CAP_COUNT; },
      [](Style & t) { t.s.degenerate = GSHSCENE_STROKE_DEGENERATE_COUNT; },
      [](Style & t) { t.s.dash_scope = GSHSCENE_STROKE_DASH_SCOPE_COUNT; },
      [](Style & t) { t.s.alignment = GSHSCENE_STROKE_ALIGN_COUNT; },
      [](Style & t) { t.s.alignment_rule = GSHGEOM_FILL_RULE_COUNT; },
      [](Style & t) { t.s.alignment_rule = (GSHGEOM_Fill_Rule)-1; },
      [](Style & t) { t.s.join = 0xFFFFFFFFu; },
  };
  for (size_t i = 0; i < invalid.size(); i++) {
    Style st;
    invalid[i](st);
    const GSHSCENE_Resource * r = nullptr;
    EXPECT_EQ(gshscene_store_intern_stroke_style(s.store, st.get(), &r), GSHSCENE_ERR_INVALID) << i;
    EXPECT_EQ(r, nullptr) << i;
  }
  // Dashes counted and not given.
  GSHSCENE_Stroke_Style st;
  gshscene_stroke_style_init(&st);
  st.dash_count = 2;
  const GSHSCENE_Resource * r = nullptr;
  EXPECT_EQ(gshscene_store_intern_stroke_style(s.store, &st, &r), GSHSCENE_ERR_INVALID);
  gshscene_stroke_style_init(&st);
  EXPECT_EQ(gshscene_store_intern_stroke_style(nullptr, &st, &r), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_store_intern_stroke_style(s.store, nullptr, &r), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_store_intern_stroke_style(s.store, &st, nullptr), GSHSCENE_ERR_INVALID);
  GSHSCENE_Store_Stats stats;
  ASSERT_EQ(gshscene_store_stats(s.store, &stats), GSHSCENE_OK);
  EXPECT_EQ(stats.live, 0u) << "a failing intern keeps nothing";
}

TEST(StrokeStyle, TheWidthProfileIsReservedAndDecidedAfterValidity) {
  StoreHolder s;
  Ref path(intern_square(s.store));
  Style st;
  st.s.width_profile = path.get();
  const GSHSCENE_Resource * r = nullptr;
  EXPECT_EQ(gshscene_store_intern_stroke_style(s.store, st.get(), &r), GSHSCENE_ERR_UNSUPPORTED) << "D8";
  st.s.width = kNaN;
  EXPECT_EQ(gshscene_store_intern_stroke_style(s.store, st.get(), &r), GSHSCENE_ERR_INVALID) << "validity first";
  st.s.width = std::ldexp(1.0, 70);
  EXPECT_EQ(gshscene_store_intern_stroke_style(s.store, st.get(), &r), GSHSCENE_ERR_UNSUPPORTED)
      << "the profile before the bounds";
  EXPECT_EQ(r, nullptr);
}

TEST(StrokeStyle, EachBoundAcceptsItsEdgeAndRefusesTheNextDouble) {
  StoreHolder s;
  const double lo = std::ldexp(1.0, -64);
  const double hi = std::ldexp(1.0, 64);
  const double below_lo = std::nextafter(lo, 0.0);
  const double above_hi = std::nextafter(hi, kInf);
  // width, a dash (on, off, and by magnitude when negative), the offset: 0, 2^-64, 2^64 in; one double past each out.
  using Set = std::function<void(Style &, double)>;
  std::vector<std::pair<const char *, Set>> fields = {
      {"width", [](Style & t, double v) { t.s.width = v; }},
      {"dash on", [](Style & t, double v) { t.dash({v, 1.0}); }},
      {"dash off", [](Style & t, double v) { t.dash({1.0, v}); }},
      {"negative dash", [](Style & t, double v) { t.dash({1.0, -v}); }},
      {"offset", [](Style & t, double v) { t.dash({1.0, 1.0}).s.dash_offset = v; }},
      {"negative offset", [](Style & t, double v) { t.dash({1.0, 1.0}).s.dash_offset = -v; }},
      {"offset with no dashes", [](Style & t, double v) { t.s.dash_offset = v; }},
  };
  for (auto & f : fields) {
    for (double v : {0.0, lo, hi, 1.0, 3.0e-10, 1.5e19}) {
      Style st;
      f.second(st, v);
      EXPECT_EQ(intern(s.store, st), GSHSCENE_OK) << f.first << " " << v;
    }
    for (double v : {below_lo, above_hi, std::ldexp(1.0, -1074), std::ldexp(1.0, 1023)}) {
      Style st;
      f.second(st, v);
      EXPECT_EQ(intern(s.store, st), GSHSCENE_ERR_LIMIT) << f.first << " " << v;
    }
  }
  // The miter limit: [1, 2^16], whatever the join (the bound is on the style as given).
  for (uint32_t join : {GSHSCENE_STROKE_JOIN_MITER, GSHSCENE_STROKE_JOIN_ROUND}) {
    for (double v : {1.0, 65536.0, 4.0, 1.5}) {
      Style st;
      st.s.join = join;
      st.s.miter_limit = v;
      EXPECT_EQ(intern(s.store, st), GSHSCENE_OK) << join << " " << v;
    }
    for (double v : {std::nextafter(1.0, 0.0), std::nextafter(65536.0, kInf), 0.0, -4.0, 0.5}) {
      Style st;
      st.s.join = join;
      st.s.miter_limit = v;
      EXPECT_EQ(intern(s.store, st), GSHSCENE_ERR_LIMIT) << join << " " << v;
    }
  }
  // A limit is decided after every validity check.
  Style both;
  both.s.width = above_hi;
  both.s.cap = 7;
  EXPECT_EQ(intern(s.store, both), GSHSCENE_ERR_INVALID);
  Style nan_dash;
  nan_dash.dash({above_hi, kNaN});
  EXPECT_EQ(intern(s.store, nan_dash), GSHSCENE_ERR_INVALID);
}

TEST(StrokeStyle, TheDashCountIsBoundedAfterAnOddArrayIsDoubled) {
  StoreHolder s;
  std::vector<double> d(65535, 1.0);
  Style odd;
  odd.dash(d);
  EXPECT_EQ(intern(s.store, odd), GSHSCENE_ERR_LIMIT) << "65535 doubles to 131070";
  d.pop_back();
  Style even;
  even.dash(d);
  Ref r;
  ASSERT_EQ(intern(s.store, even, &r), GSHSCENE_OK) << "65534";
  GSHSCENE_Stroke_Style out;
  ASSERT_EQ(gshscene_resource_stroke_style(r.get(), &out), GSHSCENE_OK);
  EXPECT_EQ(out.dash_count, 65534u);
  EXPECT_EQ(gshscene_resource_bytes(r.get()), GSHSCENE_BYTES_BASE + 65534u * GSHSCENE_BYTES_PER_DASH);
  std::vector<double> e(32767, 2.0);
  Style half;
  half.dash(e);
  ASSERT_EQ(intern(s.store, half, &r), GSHSCENE_OK) << "32767 doubles to 65534";
  ASSERT_EQ(gshscene_resource_stroke_style(r.get(), &out), GSHSCENE_OK);
  EXPECT_EQ(out.dash_count, 65534u);
  // Past the bound and then solid: the canonical count is what is bounded.
  std::vector<double> f(70000, 1.0);
  f[3] = -1.0;
  Style solid_long;
  solid_long.dash(f);
  ASSERT_EQ(intern(s.store, solid_long, &r), GSHSCENE_OK);
  EXPECT_EQ(r.get(), Ref([&] {
    const GSHSCENE_Resource * d0 = nullptr;
    GSHSCENE_Stroke_Style def;
    gshscene_stroke_style_init(&def);
    gshscene_store_intern_stroke_style(s.store, &def, &d0);
    return d0;
  }()).get()) << "a long array with a negative length is the solid default";
  // The store's own element limit is the smaller bound.
  GSHSCENE_Limits limits;
  gshscene_limits_default(&limits);
  limits.max_resource_elements = 10;
  StoreHolder small(&limits);
  for (size_t n : {size_t(10), size_t(5), size_t(4)}) {
    Style st;
    st.dash(std::vector<double>(n, 1.0));
    EXPECT_EQ(intern(small.store, st), GSHSCENE_OK) << n;
  }
  for (size_t n : {size_t(11), size_t(12), size_t(7)}) {
    Style st;
    st.dash(std::vector<double>(n, 1.0));
    EXPECT_EQ(intern(small.store, st), GSHSCENE_ERR_LIMIT) << n;
  }
}

TEST(StrokeStyle, TheStoreAccountsItsDashesAndItsLimits) {
  GSHSCENE_Limits limits;
  gshscene_limits_default(&limits);
  limits.max_bytes = GSHSCENE_BYTES_BASE + 4 * GSHSCENE_BYTES_PER_DASH;
  StoreHolder s(&limits);
  Style four;
  four.dash({1.0, 2.0, 3.0, 4.0});
  Ref r;
  ASSERT_EQ(intern(s.store, four, &r), GSHSCENE_OK);
  EXPECT_EQ(gshscene_resource_bytes(r.get()), GSHSCENE_BYTES_BASE + 4 * GSHSCENE_BYTES_PER_DASH);
  Style other;
  EXPECT_EQ(intern(s.store, other), GSHSCENE_ERR_LIMIT) << "max_bytes";
  Ref again;
  EXPECT_EQ(intern(s.store, four, &again), GSHSCENE_OK) << "a hit costs nothing";
  EXPECT_EQ(again.get(), r.get());
}

TEST(StrokeStyle, EveryAllocationOfAnInternCanFail) {
  // Fail the n-th allocation of the intern for every n until it succeeds: each failure is ERR_OOM and keeps nothing.
  for (long fail_at = 0;; fail_at++) {
    tracker::Tracker t;
    GSHSCENE_Allocator alloc = tracker::allocator_for(&t);
    GSHSCENE_Store * store = nullptr;
    ASSERT_EQ(gshscene_store_create(&alloc, nullptr, &store), GSHSCENE_OK);
    t.fail_from = t.calls + fail_at; // counted from the intern's first call
    Style st = fixture_device();
    const GSHSCENE_Resource * r = nullptr;
    GSHSCENE_Result result = gshscene_store_intern_stroke_style(store, st.get(), &r);
    t.fail_from = -1;
    if (result == GSHSCENE_OK) {
      EXPECT_EQ(gshscene_resource_hash(r), known::kStrokeDeviceHash);
      gshscene_resource_release(r);
      gshscene_store_destroy(store);
      EXPECT_TRUE(t.live.empty());
      EXPECT_GT(fail_at, 0) << "the intern allocates";
      break;
    }
    EXPECT_EQ(result, GSHSCENE_ERR_OOM) << fail_at;
    EXPECT_EQ(r, nullptr);
    GSHSCENE_Store_Stats stats;
    ASSERT_EQ(gshscene_store_stats(store, &stats), GSHSCENE_OK);
    EXPECT_EQ(stats.live, 0u) << fail_at;
    gshscene_store_destroy(store);
    EXPECT_TRUE(t.live.empty()) << fail_at;
    ASSERT_LT(fail_at, 64);
  }
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
