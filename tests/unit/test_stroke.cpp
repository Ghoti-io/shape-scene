/**
 * @file
 *
 * Strokes at the waist (strokes story S1; SK1 to SK14 of stroke.h and documentation/design.md): the stroke style
 * resource (its defaults, its validity in SK2's order, each bound at both sides, SK3's canonical form, interning,
 * hash and dump against the independent generator, a planted collision, the store's accounting); the stroke item
 * (SK4: what it records and refuses, its references, equality, hash and dump, its place in an order); the stroke node
 * (SK13: its rules, hash and dump, emission, the visitor, re-lowering); SK10's bounds and the damage of strokes; the
 * notice of what draws nothing (SK8, SK9), the features a stroke requires (SK11) and the disposition and planning pass
 * (S20).  The test sink's records and the conformance suite's stroke cases are in test_dump_sink.cpp.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "test_helpers.h"

#include "../alloc_tracker.h"
#include "../known_answers.h"
#include "../scene_fixtures.h"
#include "../scene_helpers.h"
#include "../glyph_test_provider.h"

#include <algorithm>
#include <cfloat>
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

TEST(StrokeStyle, TheDumpWritesEveryDoubleInSixteenDigits) {
  // A zero dash among others is kept (SK3 makes only an all-zero array solid); its bits are all zero digits.
  StoreHolder s;
  Style st;
  st.dash({0.0, 2.0});
  Ref r;
  ASSERT_EQ(intern(s.store, st, &r), GSHSCENE_OK);
  std::string text = dump_resource(r.get());
  EXPECT_NE(text.find("\nd 0000000000000000\nd 4000000000000000\n"), std::string::npos) << text;
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
       [](Style & t) { t.s.alignment_rule = GSHGEOM_FILL_EVEN_ODD; },
       // A butt cap draws nothing of a degenerate subpath either way: omit is draw there (SK3 (b)).
       [](Style & t) { t.s.degenerate = GSHSCENE_STROKE_DEGENERATE_OMIT; }},
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
  // The degenerate rule under a round and a square cap.
  for (uint32_t cap : {GSHSCENE_STROKE_CAP_ROUND, GSHSCENE_STROKE_CAP_SQUARE}) {
    Style dg;
    dg.s.cap = cap;
    dg.s.degenerate = GSHSCENE_STROKE_DEGENERATE_OMIT;
    Ref dr;
    ASSERT_EQ(intern(s.store, dg, &dr), GSHSCENE_OK);
    GSHSCENE_Stroke_Style dout;
    ASSERT_EQ(gshscene_resource_stroke_style(dr.get(), &dout), GSHSCENE_OK);
    EXPECT_EQ(dout.degenerate, (uint32_t)GSHSCENE_STROKE_DEGENERATE_OMIT) << cap;
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
      [](Style & t) { t.s.cap = GSHSCENE_STROKE_CAP_ROUND; t.s.degenerate = GSHSCENE_STROKE_DEGENERATE_OMIT; },
      [](Style & t) { t.s.cap = GSHSCENE_STROKE_CAP_SQUARE; t.s.degenerate = GSHSCENE_STROKE_DEGENERATE_OMIT; },
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
  st.s.width = -1.0;
  EXPECT_EQ(gshscene_store_intern_stroke_style(s.store, st.get(), &r), GSHSCENE_ERR_INVALID)
      << "a negative width is decided before the profile too";
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

/* --- SK4: the stroke item ------------------------------------------------------------------- */

namespace {

/** A store holding the resources a stroke list names: paths, runs, a picture, a style, a gradient. */
struct Kit {
  GSHSCENE_Test_Provider tp{}; // outlives the store's resources: the bound face keeps a pointer to it
  StoreHolder s;
  Ref square, fixture, three, outline_run, font_run, face, picture, style, device, gradient, run3, bound_face, bound_run;
  Kit() {
    tp.upem = 2048;
    square = Ref(intern_square(s.store));
    fixture = Ref(intern_fixture(s.store));
    GSHGEOM_Path * p3 = new_path(3);
    gshgeom_path_move_to(p3, P(0, 0, 1));
    gshgeom_path_line_to(p3, P(1, 1, 2));
    three = Ref(scene::intern(s.store, p3));
    gshgeom_path_destroy(p3);
    GSHSCENE_Glyph glyphs[2] = {{1, 0.0, 0.0, square.get()}, {2, 5.0, -3.0, fixture.get()}};
    GSHSCENE_Glyph_Run run{};
    run.source = GSHSCENE_GLYPHS_FROM_OUTLINES;
    run.glyph_count = 2;
    run.glyphs = glyphs;
    const GSHSCENE_Resource * r = nullptr;
    EXPECT_EQ(gshscene_store_intern_glyph_run(s.store, &run, &r), GSHSCENE_OK);
    outline_run = Ref(r);
    GSHSCENE_Glyph g3[1] = {{9, 0.0, 0.0, three.get()}};
    run.glyph_count = 1;
    run.glyphs = g3;
    EXPECT_EQ(gshscene_store_intern_glyph_run(s.store, &run, &r), GSHSCENE_OK);
    run3 = Ref(r);
    face = Ref(intern_face(s.store));
    bound_face = Ref(gshscene_tp_face(s.store, &tp, 1, 0x51, 0));
    bound_run = Ref(intern_font_run(s.store, bound_face.get(), 12.5, {{1, 0.0, 0.0, nullptr}, {2, 7.5, 0.25, nullptr}}, 1, 40));
    font_run = Ref(intern_font_run(s.store, face.get(), 12.5,
        {{36, 0.0, 0.0, nullptr}, {37, 7.5, 0.25, nullptr}, {4294967295u, -1.0, 2.0, nullptr}}, 1, 40));
    ListHolder pl(s.store);
    GSHSCENE_Paint black = solid(0, 0, 0, 1);
    gshscene_list_fill(pl.list, square.get(), nullptr, GSHGEOM_FILL_NONZERO, &black, 0, 0.0);
    gshscene_list_close(pl.list);
    EXPECT_EQ(gshscene_store_intern_picture(s.store, pl.list, &r), GSHSCENE_OK);
    picture = Ref(r);
    Style st = fixture_default();
    EXPECT_EQ(intern(s.store, st, &style), GSHSCENE_OK);
    Style dv = fixture_device();
    EXPECT_EQ(intern(s.store, dv, &device), GSHSCENE_OK);
    // The generator's radial gradient (gen-scene-vectors.py's GRAD_RADIAL).
    GSHSCENE_Gradient_Stop stops[2] = {{0.0, solid(0, 0, 0, 1)}, {1.0, solid(1, 1, 1, 0)}};
    GSHSCENE_Gradient g{};
    g.kind = GSHSCENE_GRADIENT_RADIAL;
    g.c0[0] = 1.0;
    g.c0[1] = 2.0;
    g.r0 = -1.5;
    g.c1[0] = 4.0;
    g.c1[1] = 2.0;
    g.r1 = 6.25;
    gshgeom_map_design_to_model(2.5, 10, 20, &g.map);
    g.stop_count = 2;
    g.stops = stops;
    EXPECT_EQ(gshscene_store_intern_gradient(s.store, &g, &r), GSHSCENE_OK);
    gradient = Ref(r);
  }
};

GSHSCENE_Paint paint_of(GSHSCENE_Paint_Kind kind, double alpha, uint32_t entry = 0, const GSHSCENE_Resource * g = nullptr) {
  GSHSCENE_Paint p{};
  p.kind = kind;
  p.rgba[3] = alpha;
  p.entry = entry;
  p.gradient = g;
  return p;
}

GSHGEOM_Map map_of(double a, double b, double c, double d, double e = 0, double f = 0) {
  const double m[4] = {a, b, c, d};
  const double t[2] = {e, f};
  GSHGEOM_Map out;
  EXPECT_EQ(gshgeom_map_from_matrix(2, m, t, &out), GSHGEOM_OK);
  return out;
}

}  // namespace

TEST(StrokeItem, RecordsTheGeneratorsListAndItsHashAndDump) {
  Kit k;
  ListHolder l(k.s.store);
  Ref arcs, clip;
  Style a = fixture_arcs();
  Style c = fixture_clip();
  ASSERT_EQ(intern(k.s.store, a, &arcs), GSHSCENE_OK);
  ASSERT_EQ(intern(k.s.store, c, &clip), GSHSCENE_OK);
  GSHSCENE_Paint fill_paint = solid(0.25, 0.5, 0.75, 1.0);
  GSHSCENE_Paint black = solid(0, 0, 0, 1);
  GSHSCENE_Paint fg = paint_of(GSHSCENE_PAINT_FOREGROUND, 0.5);
  GSHSCENE_Paint entry = paint_of(GSHSCENE_PAINT_PALETTE, 0.75, 1);
  GSHSCENE_Paint grad = paint_of(GSHSCENE_PAINT_GRADIENT, 1.0, 0, k.gradient.get());
  GSHSCENE_Paint p3 = solid(2.0, -0.5, 0.25, 0.5);
  p3.space = space_of(GCOL_PRIMARIES_DISPLAY_P3, GCOL_TRANSFER_GAMMA, 2.2);
  GSHGEOM_Map tr = translate(1.5, -2.25);
  GSHGEOM_Map design;
  gshgeom_map_design_to_model(2.5, 10, 20, &design);
  ASSERT_EQ(gshscene_list_fill(l.list, k.square.get(), nullptr, GSHGEOM_FILL_NONZERO, &fill_paint, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_stroke(l.list, k.square.get(), k.style.get(), nullptr, &black, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_stroke(l.list, k.outline_run.get(), k.device.get(), &tr, &fg, 2, 1.5), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_stroke(l.list, k.fixture.get(), arcs.get(), &design, &entry, -1, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_stroke(l.list, k.font_run.get(), clip.get(), nullptr, &grad, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_stroke(l.list, k.square.get(), k.style.get(), nullptr, &p3, 0, -3.0), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_hash(l.list), known::kStrokeListHash);
  EXPECT_EQ(dump_list(l.list), std::string(known::kStrokeListDump));
  ASSERT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_hash(l.list), known::kStrokeListHash) << "flat: closing reorders nothing";
  const GSHSCENE_Item * item = nullptr;
  ASSERT_EQ(gshscene_list_item(l.list, 2, &item), GSHSCENE_OK);
  EXPECT_EQ(item->kind, GSHSCENE_ITEM_STROKE);
  EXPECT_EQ((int)GSHSCENE_ITEM_STROKE, 7);
  EXPECT_EQ((int)GSHSCENE_ITEM_PLACEHOLDER, 8);
  EXPECT_EQ((int)GSHSCENE_ITEM_KIND_COUNT, 9);
  EXPECT_EQ(item->resource, k.outline_run.get());
  EXPECT_EQ(item->style, k.device.get());
  EXPECT_EQ(item->fill_rule, GSHGEOM_FILL_NONZERO);
  EXPECT_EQ(item->layer, 2);
  EXPECT_EQ(item->depth, 1.5);
  EXPECT_EQ(item->paint.kind, GSHSCENE_PAINT_FOREGROUND);
  GSHSCENE_Group zero;
  std::memset(&zero, 0, sizeof zero);
  EXPECT_EQ(std::memcmp(&item->group, &zero, sizeof zero), 0) << "a stroke's group is zero";
  ASSERT_EQ(gshscene_list_item(l.list, 0, &item), GSHSCENE_OK);
  EXPECT_EQ(item->style, nullptr) << "a fill names no style";
}

TEST(StrokeItem, EveryCenterlineAndPaintIsRecordedOrRefusedAsSk4Says) {
  Kit k;
  StoreHolder other;
  Ref foreign_style;
  Style st;
  ASSERT_EQ(intern(other.store, st, &foreign_style), GSHSCENE_OK);
  Ref foreign_path(intern_square(other.store));
  ListHolder l(k.s.store);
  GSHSCENE_Paint black = solid(0, 0, 0, 1);
  GSHGEOM_Map m3;
  gshgeom_map_identity(3, &m3);
  GSHGEOM_Map bad = translate(0, 0);
  bad.m[0][0] = std::nan("");
  struct Case {
    const char * what;
    const GSHSCENE_Resource * centerline;
    const GSHSCENE_Resource * style;
    const GSHGEOM_Map * map;
    GSHSCENE_Paint paint;
    double depth;
    GSHSCENE_Result want;
  };
  GSHSCENE_Paint big = solid(std::ldexp(1.0, 17), 0, 0, 1);
  GSHSCENE_Paint bad_alpha = solid(0, 0, 0, 2.0);
  GSHSCENE_Paint foreign_grad = paint_of(GSHSCENE_PAINT_GRADIENT, 1.0, 0, k.style.get());
  std::vector<Case> cases = {
      {"a path", k.square.get(), k.style.get(), nullptr, black, 0.0, GSHSCENE_OK},
      {"a run from outlines", k.outline_run.get(), k.style.get(), nullptr, black, 0.0, GSHSCENE_OK},
      {"a run from a font", k.font_run.get(), k.style.get(), nullptr, black, 0.0, GSHSCENE_OK},
      {"a foreground paint", k.square.get(), k.style.get(), nullptr, paint_of(GSHSCENE_PAINT_FOREGROUND, 0.5), 0.0, GSHSCENE_OK},
      {"a palette paint", k.square.get(), k.style.get(), nullptr, paint_of(GSHSCENE_PAINT_PALETTE, 1.0, 7), 0.0, GSHSCENE_OK},
      {"a gradient paint", k.square.get(), k.style.get(), nullptr, paint_of(GSHSCENE_PAINT_GRADIENT, 1.0, 0, k.gradient.get()), 0.0, GSHSCENE_OK},
      {"a picture", k.picture.get(), k.style.get(), nullptr, black, 0.0, GSHSCENE_ERR_INVALID},
      {"a style as the centerline", k.style.get(), k.style.get(), nullptr, black, 0.0, GSHSCENE_ERR_INVALID},
      {"no centerline", nullptr, k.style.get(), nullptr, black, 0.0, GSHSCENE_ERR_INVALID},
      {"a centerline of another store", foreign_path.get(), k.style.get(), nullptr, black, 0.0, GSHSCENE_ERR_INVALID},
      {"no style", k.square.get(), nullptr, nullptr, black, 0.0, GSHSCENE_ERR_INVALID},
      {"a path as the style", k.square.get(), k.fixture.get(), nullptr, black, 0.0, GSHSCENE_ERR_INVALID},
      {"a style of another store", k.square.get(), foreign_style.get(), nullptr, black, 0.0, GSHSCENE_ERR_INVALID},
      {"an invalid map", k.square.get(), k.style.get(), &bad, black, 0.0, GSHSCENE_ERR_INVALID},
      {"an invalid paint", k.square.get(), k.style.get(), nullptr, bad_alpha, 0.0, GSHSCENE_ERR_INVALID},
      {"a style named as a gradient", k.square.get(), k.style.get(), nullptr, foreign_grad, 0.0, GSHSCENE_ERR_INVALID},
      {"a depth that is not finite", k.square.get(), k.style.get(), nullptr, black, std::nan(""), GSHSCENE_ERR_INVALID},
      {"a 3D path", k.three.get(), k.style.get(), nullptr, black, 0.0, GSHSCENE_ERR_UNSUPPORTED},
      {"a run whose outline is 3D", k.run3.get(), k.style.get(), nullptr, black, 0.0, GSHSCENE_ERR_UNSUPPORTED},
      {"a 3D map", k.square.get(), k.style.get(), &m3, black, 0.0, GSHSCENE_ERR_UNSUPPORTED},
      {"a 3D path and no style", k.three.get(), nullptr, nullptr, black, 0.0, GSHSCENE_ERR_INVALID},
      {"N1's bound", k.square.get(), k.style.get(), nullptr, big, 0.0, GSHSCENE_ERR_LIMIT},
      {"N1's bound and a 3D path", k.three.get(), k.style.get(), nullptr, big, 0.0, GSHSCENE_ERR_UNSUPPORTED},
      {"N1's bound and no style", k.square.get(), nullptr, nullptr, big, 0.0, GSHSCENE_ERR_INVALID},
  };
  size_t count = 0;
  for (const Case & c : cases) {
    size_t before_centerline = c.centerline ? gshscene_resource_refcount(c.centerline) : 0;
    size_t before_style = c.style ? gshscene_resource_refcount(c.style) : 0;
    EXPECT_EQ(gshscene_list_stroke(l.list, c.centerline, c.style, c.map, &c.paint, 0, c.depth), c.want) << c.what;
    if (c.want == GSHSCENE_OK) {
      count++;
      EXPECT_EQ(gshscene_resource_refcount(c.centerline), before_centerline + 1) << c.what << ": the list holds it";
    } else if (c.centerline) {
      EXPECT_EQ(gshscene_resource_refcount(c.centerline), before_centerline) << c.what << ": no reference taken";
    }
    if (c.want != GSHSCENE_OK && c.style) {
      EXPECT_EQ(gshscene_resource_refcount(c.style), before_style) << c.what;
    }
    EXPECT_EQ(gshscene_list_count(l.list), count) << c.what << ": a failing call leaves the list as it was";
  }
  // A NULL paint is opaque black; a NULL map the identity.
  ASSERT_EQ(gshscene_list_stroke(l.list, k.square.get(), k.style.get(), nullptr, nullptr, 0, -0.0), GSHSCENE_OK);
  const GSHSCENE_Item * item = nullptr;
  ASSERT_EQ(gshscene_list_item(l.list, count, &item), GSHSCENE_OK);
  EXPECT_EQ(item->paint.kind, GSHSCENE_PAINT_SOLID);
  EXPECT_EQ(item->paint.rgba[3], 1.0);
  EXPECT_EQ(item->map.dim, 2);
  EXPECT_EQ(bits_of(item->depth), 0u) << "-0 is stored as +0";
  EXPECT_EQ(gshscene_list_stroke(nullptr, k.square.get(), k.style.get(), nullptr, nullptr, 0, 0.0), GSHSCENE_ERR_INVALID);
  ASSERT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_stroke(l.list, k.square.get(), k.style.get(), nullptr, nullptr, 0, 0.0), GSHSCENE_ERR_INVALID)
      << "a closed list";
}

TEST(StrokeItem, TheListHoldsTheCenterlineAndTheStyleUntilItIsResetOrDestroyed) {
  Kit k;
  size_t centerline = gshscene_resource_refcount(k.square.get());
  size_t style = gshscene_resource_refcount(k.style.get());
  size_t gradient = gshscene_resource_refcount(k.gradient.get());
  GSHSCENE_Paint grad = paint_of(GSHSCENE_PAINT_GRADIENT, 1.0, 0, k.gradient.get());
  {
    ListHolder l(k.s.store);
    for (int i = 0; i < 3; i++) {
      ASSERT_EQ(gshscene_list_stroke(l.list, k.square.get(), k.style.get(), nullptr, &grad, 0, 0.0), GSHSCENE_OK);
    }
    EXPECT_EQ(gshscene_resource_refcount(k.square.get()), centerline + 3);
    EXPECT_EQ(gshscene_resource_refcount(k.style.get()), style + 3);
    EXPECT_EQ(gshscene_resource_refcount(k.gradient.get()), gradient + 3);
    ASSERT_EQ(gshscene_list_reset(l.list), GSHSCENE_OK);
    EXPECT_EQ(gshscene_resource_refcount(k.style.get()), style);
    ASSERT_EQ(gshscene_list_stroke(l.list, k.square.get(), k.style.get(), nullptr, &grad, 0, 0.0), GSHSCENE_OK);
    // A picture of strokes copies them with their references.
    ASSERT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
    const GSHSCENE_Resource * pic = nullptr;
    ASSERT_EQ(gshscene_store_intern_picture(k.s.store, l.list, &pic), GSHSCENE_OK);
    EXPECT_EQ(gshscene_resource_refcount(k.style.get()), style + 2);
    gshscene_resource_release(pic);
    EXPECT_EQ(gshscene_resource_refcount(k.style.get()), style + 1);
  }
  EXPECT_EQ(gshscene_resource_refcount(k.square.get()), centerline);
  EXPECT_EQ(gshscene_resource_refcount(k.style.get()), style);
  EXPECT_EQ(gshscene_resource_refcount(k.gradient.get()), gradient);
}

TEST(StrokeItem, EqualityComparesTheStyleByHandle) {
  Kit k;
  ListHolder a(k.s.store), b(k.s.store), c(k.s.store), d(k.s.store);
  GSHSCENE_Paint black = solid(0, 0, 0, 1);
  for (GSHSCENE_List * l : {a.list, b.list}) {
    ASSERT_EQ(gshscene_list_stroke(l, k.square.get(), k.style.get(), nullptr, &black, 0, 0.0), GSHSCENE_OK);
  }
  ASSERT_EQ(gshscene_list_stroke(c.list, k.square.get(), k.device.get(), nullptr, &black, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(d.list, k.square.get(), nullptr, GSHGEOM_FILL_NONZERO, &black, 0, 0.0), GSHSCENE_OK);
  EXPECT_TRUE(gshscene_list_equal(a.list, b.list));
  EXPECT_FALSE(gshscene_list_equal(a.list, c.list)) << "another style";
  EXPECT_NE(gshscene_list_hash(a.list), gshscene_list_hash(c.list));
  EXPECT_FALSE(gshscene_list_equal(a.list, d.list)) << "a fill of the same path is not its stroke";
  EXPECT_NE(gshscene_list_hash(a.list), gshscene_list_hash(d.list));
}

TEST(StrokeItem, AStrokeIsOneUnitOfItsSequence) {
  // N11: in a depth group and an overlay group a stroke moves as one unit, and a depth tie between a fill and a stroke
  // is recorded like any other (SK4).
  Kit k;
  GSHSCENE_Paint black = solid(0, 0, 0, 1);
  for (uint32_t ordering : {GSHSCENE_ORDER_DEPTH, GSHSCENE_ORDER_OVERLAY}) {
    ListHolder l(k.s.store);
    GSHSCENE_Group g = make_group(1.0, ordering);
    ASSERT_EQ(gshscene_list_group_push(l.list, &g, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_stroke(l.list, k.square.get(), k.style.get(), nullptr, &black, 3, 1.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_fill(l.list, k.fixture.get(), nullptr, GSHGEOM_FILL_NONZERO, &black, 1, 2.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_stroke(l.list, k.fixture.get(), k.device.get(), nullptr, &black, 2, 2.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_group_pop(l.list), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
    std::vector<int32_t> layers;
    for (size_t i = 1; i + 1 < gshscene_list_count(l.list); i++) {
      const GSHSCENE_Item * item = nullptr;
      ASSERT_EQ(gshscene_list_item(l.list, i, &item), GSHSCENE_OK);
      layers.push_back(item->layer);
    }
    // Depth: far first (2, 2 tied: by layer), then 1.  Overlay: by layer.
    EXPECT_EQ(layers, (std::vector<int32_t>{1, 2, 3})) << ordering;
    EXPECT_EQ(gshscene_list_tie_count(l.list), ordering == GSHSCENE_ORDER_DEPTH ? 1u : 0u);
  }
}

/* --- SK13: the stroke node ------------------------------------------------------------------- */

TEST(StrokeNode, TheGeneratorsSceneHashesDumpsAndEmitsOneStrokeItemPerNode) {
  SceneHolder s;
  Ref square(intern_square(s.store()));
  Ref fixture(intern_fixture(s.store()));
  GSHSCENE_Glyph glyphs[2] = {{1, 0.0, 0.0, square.get()}, {2, 5.0, -3.0, fixture.get()}};
  GSHSCENE_Glyph_Run run{};
  run.source = GSHSCENE_GLYPHS_FROM_OUTLINES;
  run.glyph_count = 2;
  run.glyphs = glyphs;
  const GSHSCENE_Resource * r = nullptr;
  ASSERT_EQ(gshscene_store_intern_glyph_run(s.store(), &run, &r), GSHSCENE_OK);
  Ref outline_run(r);
  Ref style, device;
  Style st = fixture_default();
  Style dv = fixture_device();
  ASSERT_EQ(intern(s.store(), st, &style), GSHSCENE_OK);
  ASSERT_EQ(intern(s.store(), dv, &device), GSHSCENE_OK);
  const uint64_t kDomain = UINT64_C(0x5356470000000001);
  GSHSCENE_Key key = K(kDomain, 7);
  VersionRef v1;
  uint64_t keyed = 0;
  {
    EditHolder e(s);
    GSHSCENE_Node fill = shape_node(square.get());
    fill.paint = solid(0.25, 0.5, 0.75, 1.0);
    EXPECT_EQ(add(e.edit, GSHSCENE_ROOT_ID, fill), 2u);
    EXPECT_EQ(add(e.edit, GSHSCENE_ROOT_ID, stroke_node(square.get(), style.get())), 3u);
    GSHSCENE_Node n = stroke_node(outline_run.get(), device.get(), 3);
    n.map = translate(1.5, -2.25);
    n.paint = paint_of(GSHSCENE_PAINT_FOREGROUND, 0.5);
    n.depth = 1.0;
    ASSERT_EQ(gshscene_edit_upsert(e.edit, &key, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, &n, &keyed), GSHSCENE_OK);
    EXPECT_EQ(keyed, 4u);
    publish(s, e, &v1);
  }
  EXPECT_EQ(gshscene_version_hash(v1.get()), known::kStrokeSceneHash);
  EXPECT_EQ(dump_version(v1.get()), std::string(known::kStrokeSceneDump));
  GSHSCENE_Node_Info info = info_of(v1.get(), 4);
  EXPECT_EQ(info.node.kind, GSHSCENE_NODE_STROKE);
  EXPECT_EQ(info.node.style, device.get());
  EXPECT_EQ(info.node.resource, outline_run.get());
  EXPECT_STREQ(gshscene_node_kind_string(GSHSCENE_NODE_STROKE), "stroke");
  EXPECT_EQ((int)GSHSCENE_NODE_STROKE, 4);
  EXPECT_EQ((int)GSHSCENE_NODE_KIND_COUNT, 6);
  // The emitted list: the fill, then the strokes (SK14's order), equal to the recorded list that draws the same.
  const GSHSCENE_List * emitted = nullptr;
  ASSERT_EQ(gshscene_version_list(v1.get(), &emitted), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_hash(emitted), known::kStrokeSceneListHash);
  ListHolder recorded(s.store());
  GSHSCENE_Paint fill_paint = solid(0.25, 0.5, 0.75, 1.0);
  GSHSCENE_Paint black = solid(0, 0, 0, 1);
  GSHSCENE_Paint fg = paint_of(GSHSCENE_PAINT_FOREGROUND, 0.5);
  GSHGEOM_Map tr = translate(1.5, -2.25);
  gshscene_list_fill(recorded.list, square.get(), nullptr, GSHGEOM_FILL_NONZERO, &fill_paint, 0, 0.0);
  gshscene_list_stroke(recorded.list, square.get(), style.get(), nullptr, &black, 0, 0.0);
  gshscene_list_stroke(recorded.list, outline_run.get(), device.get(), &tr, &fg, 3, 1.0);
  ASSERT_EQ(gshscene_list_close(recorded.list), GSHSCENE_OK);
  EXPECT_TRUE(gshscene_list_equal(emitted, recorded.list));
  for (size_t i = 0; i < 3; i++) {
    uint64_t node = 0;
    ASSERT_EQ(gshscene_version_item_node(v1.get(), 0, i, &node), GSHSCENE_OK);
    EXPECT_EQ(node, 2u + i) << "item " << i << " names its node";
  }
  // The visitor reports the stroke's kind and its composed map.
  int strokes = 0;
  for (const Occurrence & o : occurrences(v1.get())) {
    if (o.kind == GSHSCENE_NODE_STROKE) {
      strokes++;
      EXPECT_EQ(o.props.style, o.node == 3 ? style.get() : device.get());
    }
  }
  EXPECT_EQ(strokes, 2);
  // An identical re-lowering (an upsert of the same properties) publishes nothing.
  VersionRef v2;
  {
    EditHolder e(s);
    GSHSCENE_Node n = info.node;
    uint64_t again = 0;
    ASSERT_EQ(gshscene_edit_upsert(e.edit, &key, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, &n, &again), GSHSCENE_OK);
    EXPECT_EQ(again, keyed);
    publish(s, e, &v2);
  }
  EXPECT_EQ(v2.get(), v1.get());
  // An upsert with another style changes that node, and only it.
  VersionRef v3;
  {
    EditHolder e(s);
    GSHSCENE_Node n = info.node;
    n.style = style.get();
    ASSERT_EQ(gshscene_edit_upsert(e.edit, &key, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, &n, nullptr), GSHSCENE_OK);
    publish(s, e, &v3);
  }
  ASSERT_NE(v3.get(), v1.get());
  GSHSCENE_Diff * diff = nullptr;
  ASSERT_EQ(gshscene_diff(v1.get(), v3.get(), nullptr, nullptr, &diff), GSHSCENE_OK);
  ASSERT_EQ(gshscene_diff_count(diff), 1u);
  uint64_t id = 0;
  GSHSCENE_Diff_Kind kind;
  ASSERT_EQ(gshscene_diff_get(diff, 0, &id, &kind), GSHSCENE_OK);
  EXPECT_EQ(id, keyed);
  EXPECT_EQ(kind, GSHSCENE_DIFF_CHANGED);
  gshscene_diff_destroy(diff);
  EXPECT_NE(gshscene_version_hash(v3.get()), gshscene_version_hash(v1.get()));
}

TEST(StrokeNode, ANodeTakesAStrokeItemsRulesAndHoldsItsStyle) {
  SceneHolder s;
  Ref square(intern_square(s.store()));
  GSHGEOM_Path * p3 = new_path(3);
  gshgeom_path_move_to(p3, P(0, 0, 1));
  gshgeom_path_line_to(p3, P(1, 1, 2));
  Ref three(scene::intern(s.store(), p3));
  gshgeom_path_destroy(p3);
  Ref style;
  Style st;
  ASSERT_EQ(intern(s.store(), st, &style), GSHSCENE_OK);
  StoreHolder other;
  Ref foreign;
  ASSERT_EQ(intern(other.store, st, &foreign), GSHSCENE_OK);
  EditHolder e(s);
  struct Case {
    const char * what;
    GSHSCENE_Node node;
    GSHSCENE_Result want;
  };
  GSHSCENE_Node big = stroke_node(square.get(), style.get());
  big.paint = solid(std::ldexp(1.0, 17), 0, 0, 1);
  GSHSCENE_Node m3 = stroke_node(square.get(), style.get());
  gshgeom_map_identity(3, &m3.map);
  std::vector<Case> cases = {
      {"no style", stroke_node(square.get(), nullptr), GSHSCENE_ERR_INVALID},
      {"a path as the style", stroke_node(square.get(), square.get()), GSHSCENE_ERR_INVALID},
      {"a style of another store", stroke_node(square.get(), foreign.get()), GSHSCENE_ERR_INVALID},
      {"a style as the centerline", stroke_node(style.get(), style.get()), GSHSCENE_ERR_INVALID},
      {"a 3D centerline", stroke_node(three.get(), style.get()), GSHSCENE_ERR_UNSUPPORTED},
      {"a 3D map", m3, GSHSCENE_ERR_UNSUPPORTED},
      {"N1's bound", big, GSHSCENE_ERR_LIMIT},
  };
  for (const Case & c : cases) {
    uint64_t id = 0;
    EXPECT_EQ(gshscene_edit_add(e.edit, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, &c.node, nullptr, &id), c.want) << c.what;
  }
  size_t before = gshscene_resource_refcount(style.get());
  uint64_t id = add(e.edit, GSHSCENE_ROOT_ID, stroke_node(square.get(), style.get()));
  ASSERT_NE(id, 0u);
  EXPECT_EQ(gshscene_resource_refcount(style.get()), before + 1) << "the node holds its style";
  // A node of another kind cannot become a stroke.
  GSHSCENE_Node shape = shape_node(square.get());
  EXPECT_EQ(gshscene_edit_set(e.edit, id, &shape), GSHSCENE_ERR_INVALID);
  publish(s, e);
}

/* --- SK10: bounds ------------------------------------------------------------------------------ */

namespace {

/** The bound of a stroke of @p style around the box (x0, y0, x1, y1) under @p map in @p unit. */
GSHGEOM_Bounds bound_of(GSHSCENE_Store * store, Style st, double x0, double y0, double x1, double y1,
    const GSHGEOM_Map & map, uint32_t unit = GSHSCENE_UNIT_DEFAULT, GSHSCENE_Result want = GSHSCENE_OK) {
  Ref r;
  EXPECT_EQ(intern(store, st, &r), GSHSCENE_OK);
  GSHGEOM_Bounds c{};
  c.empty = 0;
  c.min = P(x0, y0);
  c.max = P(x1, y1);
  GSHGEOM_Bounds out{};
  std::memset(&out, 0xab, sizeof out);
  EXPECT_EQ(gshscene_stroke_bounds(r.get(), &c, &map, unit, &out), want);
  return out;
}

/** Whether @p b holds (x0, y0, x1, y1) outset by @p h exactly, and is within a relative 2^-48 of it. */
void expect_outset(const GSHGEOM_Bounds & b, double x0, double y0, double x1, double y1, long double h, const char * what) {
  ASSERT_EQ(b.empty, 0) << what;
  long double lo_x = (long double)x0 - h, lo_y = (long double)y0 - h, hi_x = (long double)x1 + h, hi_y = (long double)y1 + h;
  EXPECT_LE((long double)b.min.x, lo_x) << what << ": the bound is below the real one";
  EXPECT_LE((long double)b.min.y, lo_y) << what;
  EXPECT_GE((long double)b.max.x, hi_x) << what;
  EXPECT_GE((long double)b.max.y, hi_y) << what;
  long double slack = (h + 64.0L) * std::ldexp(1.0L, -48);
  EXPECT_GE((long double)b.min.x, lo_x - slack) << what << ": not conservative but loose";
  EXPECT_LE((long double)b.max.x, hi_x + slack) << what;
}

}  // namespace

TEST(StrokeBounds, EachFactorOfSk10) {
  StoreHolder s;
  GSHGEOM_Map id = translate(0, 0);
  struct Case {
    const char * what;
    std::function<void(Style &)> set;
    long double h; // in half widths times the width
  };
  // Width 2 throughout, so h is f * a.
  std::vector<Case> cases = {
      {"miter, its limit", [](Style &) {}, 4.0L},
      {"miter limit 10", [](Style & t) { t.s.miter_limit = 10.0; }, 10.0L},
      {"miter limit 1, a square cap", [](Style & t) { t.s.miter_limit = 1.0; t.s.cap = GSHSCENE_STROKE_CAP_SQUARE; }, 1.5L},
      {"miter-clip: the limit and one more", [](Style & t) { t.s.join = GSHSCENE_STROKE_JOIN_MITER_CLIP; }, 5.0L},
      {"arcs: the limit and one more", [](Style & t) { t.s.join = GSHSCENE_STROKE_JOIN_ARCS; t.s.miter_limit = 1.5; }, 2.5L},
      {"round", [](Style & t) { t.s.join = GSHSCENE_STROKE_JOIN_ROUND; }, 1.0L},
      {"bevel", [](Style & t) { t.s.join = GSHSCENE_STROKE_JOIN_BEVEL; }, 1.0L},
      {"round join, square cap", [](Style & t) { t.s.join = GSHSCENE_STROKE_JOIN_ROUND; t.s.cap = GSHSCENE_STROKE_CAP_SQUARE; }, 1.5L},
      {"round join, round cap", [](Style & t) { t.s.join = GSHSCENE_STROKE_JOIN_ROUND; t.s.cap = GSHSCENE_STROKE_CAP_ROUND; }, 1.0L},
      {"inside: twice", [](Style & t) { t.s.join = GSHSCENE_STROKE_JOIN_ROUND; t.s.alignment = GSHSCENE_STROKE_ALIGN_INSIDE; }, 2.0L},
      {"outside: twice", [](Style & t) { t.s.join = GSHSCENE_STROKE_JOIN_BEVEL; t.s.alignment = GSHSCENE_STROKE_ALIGN_OUTSIDE; }, 2.0L},
      {"miter-clip outside", [](Style & t) { t.s.join = GSHSCENE_STROKE_JOIN_MITER_CLIP; t.s.alignment = GSHSCENE_STROKE_ALIGN_OUTSIDE; }, 10.0L},
      {"dashes never enlarge it", [](Style & t) { t.s.join = GSHSCENE_STROKE_JOIN_ROUND; t.dash({0.0, 1.0}); t.s.cap = GSHSCENE_STROKE_CAP_ROUND; }, 1.0L},
      {"a limit past 2^16 / 2 plus one", [](Style & t) { t.s.join = GSHSCENE_STROKE_JOIN_MITER_CLIP; t.s.miter_limit = 65536.0; }, 65537.0L},
  };
  for (const Case & c : cases) {
    Style st;
    st.s.width = 2.0;
    c.set(st);
    GSHGEOM_Bounds b = bound_of(s.store, st, 1.0, 2.0, 5.0, 3.0, id);
    expect_outset(b, 1.0, 2.0, 5.0, 3.0, c.h, c.what);
  }
}

TEST(StrokeBounds, ADegenerateSquareInAnyOrientationIsCovered) {
  // D4 as amended: a degenerate subpath's square points along SVG 2's effective tangent, so it can lie at any angle;
  // its corners are h sqrt 2 from its point, the most (at 45 degrees) h sqrt 2 along an axis.  SK10's square-cap factor
  // 3/2 covers that before any mapping, so the bound of a point holds every orientation of the square, in local width
  // (also under a rotation, the square then built in model space) and in device width.
  StoreHolder s;
  for (uint32_t space : {GSHSCENE_STROKE_WIDTH_LOCAL, GSHSCENE_STROKE_WIDTH_DEVICE}) {
    for (uint32_t join : {GSHSCENE_STROKE_JOIN_ROUND, GSHSCENE_STROKE_JOIN_BEVEL, GSHSCENE_STROKE_JOIN_MITER}) {
      Style st;
      st.s.width = 4.0; // h = 2
      st.s.width_space = space;
      st.s.join = join;
      st.s.miter_limit = 1.0;
      st.s.cap = GSHSCENE_STROKE_CAP_SQUARE;
      for (double degrees : {0.0, 30.0, 45.0, 60.0, 90.0, 135.0}) {
        double a = degrees * 3.14159265358979323846 / 180.0;
        // The real square's corners about the point (5, 7): (+-h, +-h) rotated by a.
        double c = std::cos(a), sn = std::sin(a);
        GSHGEOM_Bounds b = bound_of(s.store, st, 5.0, 7.0, 5.0, 7.0, translate(0, 0));
        for (double u : {-2.0, 2.0}) {
          for (double v : {-2.0, 2.0}) {
            double x = 5.0 + c * u - sn * v;
            double y = 7.0 + sn * u + c * v;
            EXPECT_LE(b.min.x, x) << degrees;
            EXPECT_GE(b.max.x, x) << degrees;
            EXPECT_LE(b.min.y, y) << degrees;
            EXPECT_GE(b.max.y, y) << degrees;
          }
        }
        if (space == GSHSCENE_STROKE_WIDTH_LOCAL) {
          // Under a rotation map the square is built in model space and mapped: the mapped bound still holds it.
          GSHGEOM_Map rot = map_of(c, -sn, sn, c);
          GSHGEOM_Bounds rb = bound_of(s.store, st, 5.0, 7.0, 5.0, 7.0, rot);
          for (double u : {-2.0, 2.0}) {
            for (double v : {-2.0, 2.0}) {
              GSHGEOM_Point q;
              ASSERT_EQ(gshgeom_map_apply_point(&rot, P(5.0 + u, 7.0 + v), &q), GSHGEOM_OK);
              EXPECT_LE(rb.min.x, q.x + 1e-12) << degrees;
              EXPECT_GE(rb.max.x, q.x - 1e-12) << degrees;
              EXPECT_LE(rb.min.y, q.y + 1e-12) << degrees;
              EXPECT_GE(rb.max.y, q.y - 1e-12) << degrees;
            }
          }
        }
      }
    }
  }
}

TEST(StrokeBounds, LocalWidthIsMappedAndDeviceWidthIsNot) {
  StoreHolder s;
  Style local;
  local.s.width = 2.0;
  local.s.join = GSHSCENE_STROKE_JOIN_ROUND;
  Style device = local;
  device.s.width_space = GSHSCENE_STROKE_WIDTH_DEVICE;
  // A scale of (4, 0.5) and a translation: local width outsets in model space and is mapped, so the pen is 8 by 1;
  // device width outsets the mapped box by 1 pixel in each direction.
  GSHGEOM_Map m = map_of(4.0, 0.0, 0.0, 0.5, 10.0, 20.0);
  GSHGEOM_Bounds lb = bound_of(s.store, local, 0.0, 0.0, 2.0, 4.0, m);
  expect_outset(lb, 10.0 - 4.0, 20.0 - 0.5, 18.0 + 4.0, 22.0 + 0.5, 0.0L, "local");
  GSHGEOM_Bounds db = bound_of(s.store, device, 0.0, 0.0, 2.0, 4.0, m);
  expect_outset(db, 10.0, 20.0, 18.0, 22.0, 1.0L, "device");
  // A rotation by a quarter turn exchanges the axes.
  GSHGEOM_Map q = map_of(0.0, -1.0, 1.0, 0.0);
  GSHGEOM_Bounds rb = bound_of(s.store, local, 0.0, 0.0, 2.0, 4.0, q);
  expect_outset(rb, -4.0, 0.0, 0.0, 2.0, 1.0L, "rotated");
  // A stream's device pixel is 1/96 inch in its unit (K8): millimetres, points, inches.
  struct U {
    uint32_t unit;
    long double size;
  } units[] = {{GSHSCENE_UNIT_PIXEL, 1.0L}, {GSHSCENE_UNIT_MILLIMETER, 25.4L / 96.0L}, {GSHSCENE_UNIT_POINT, 0.75L},
      {GSHSCENE_UNIT_INCH, 1.0L / 96.0L}};
  for (const U & u : units) {
    GSHGEOM_Bounds ub = bound_of(s.store, device, 0.0, 0.0, 2.0, 4.0, m, u.unit);
    expect_outset(ub, 10.0, 20.0, 18.0, 22.0, u.size, "a device pixel in a stream's unit");
    GSHGEOM_Bounds lu = bound_of(s.store, local, 0.0, 0.0, 2.0, 4.0, m, u.unit);
    EXPECT_EQ(std::memcmp(&lu, &lb, sizeof lu), 0) << "a local width ignores the unit";
  }
  EXPECT_EQ(gshscene_stroke_pixel_size(GSHSCENE_UNIT_DEFAULT), 1.0);
  EXPECT_EQ(gshscene_stroke_pixel_size(GSHSCENE_UNIT_PIXEL), 1.0);
  EXPECT_EQ(gshscene_stroke_pixel_size(GSHSCENE_UNIT_MILLIMETER), 25.4 / 96.0);
  EXPECT_EQ(gshscene_stroke_pixel_size(GSHSCENE_UNIT_POINT), 0.75);
  EXPECT_EQ(gshscene_stroke_pixel_size(GSHSCENE_UNIT_INCH), 1.0 / 96.0);
  EXPECT_EQ(gshscene_stroke_pixel_size(GSHSCENE_UNIT_COUNT), 0.0);
}

TEST(StrokeBounds, EmptyAndZeroWidthAndRefusals) {
  StoreHolder s;
  GSHGEOM_Map id = translate(0, 0);
  Style zero;
  zero.s.width = 0.0;
  GSHGEOM_Bounds b = bound_of(s.store, zero, 0, 0, 4, 4, id);
  EXPECT_EQ(b.empty, 1) << "SK8: width 0 covers nothing";
  Ref style;
  Style st;
  ASSERT_EQ(intern(s.store, st, &style), GSHSCENE_OK);
  GSHGEOM_Bounds empty{};
  empty.empty = 1;
  GSHGEOM_Bounds out;
  ASSERT_EQ(gshscene_stroke_bounds(style.get(), &empty, &id, GSHSCENE_UNIT_DEFAULT, &out), GSHSCENE_OK);
  EXPECT_EQ(out.empty, 1);
  GSHGEOM_Bounds box{};
  box.max = P(1, 1);
  Ref path(intern_square(s.store));
  GSHGEOM_Map m3;
  gshgeom_map_identity(3, &m3);
  EXPECT_EQ(gshscene_stroke_bounds(path.get(), &box, &id, 0, &out), GSHSCENE_ERR_INVALID) << "not a style";
  EXPECT_EQ(gshscene_stroke_bounds(nullptr, &box, &id, 0, &out), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_stroke_bounds(style.get(), nullptr, &id, 0, &out), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_stroke_bounds(style.get(), &box, nullptr, 0, &out), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_stroke_bounds(style.get(), &box, &id, 0, nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_stroke_bounds(style.get(), &box, &m3, 0, &out), GSHSCENE_ERR_INVALID) << "a 3D map";
  EXPECT_EQ(gshscene_stroke_bounds(style.get(), &box, &id, GSHSCENE_UNIT_COUNT, &out), GSHSCENE_ERR_INVALID);
  // A box the map takes past the doubles: the bound is unknown.
  GSHGEOM_Bounds far{};
  far.max = P(1e300, 1);
  GSHGEOM_Map huge = map_of(1e29, 0, 0, 1);
  Style wide;
  wide.s.width = std::ldexp(1.0, 64);
  Ref wr;
  ASSERT_EQ(intern(s.store, wide, &wr), GSHSCENE_OK);
  EXPECT_EQ(gshscene_stroke_bounds(wr.get(), &far, &huge, 0, &out), GSHSCENE_ERR_LIMIT);
  // A device width outset past the largest double after a mapping that succeeds: the bound is unknown too.
  Style dv;
  dv.s.width_space = GSHSCENE_STROKE_WIDTH_DEVICE;
  Ref dr;
  ASSERT_EQ(intern(s.store, dv, &dr), GSHSCENE_OK);
  GSHGEOM_Bounds top{};
  top.min = P(0, 0);
  top.max = P(DBL_MAX, 1);
  EXPECT_EQ(gshscene_stroke_bounds(dr.get(), &top, &id, 0, &out), GSHSCENE_ERR_LIMIT);
}

TEST(StrokeBounds, TheOutsetIsRoundedOutward) {
  // A box at 1e16 (where a double's spacing is 2) outset by a quarter: rounding to nearest would give the box back.
  StoreHolder s;
  Style st;
  st.s.width = 0.5;
  st.s.join = GSHSCENE_STROKE_JOIN_ROUND;
  GSHGEOM_Map id = translate(0, 0);
  GSHGEOM_Bounds b = bound_of(s.store, st, 1e16, -1e16, 1e16, -1e16, id);
  EXPECT_LT(b.min.x, 1e16);
  EXPECT_GT(b.max.x, 1e16);
  EXPECT_LT(b.min.y, -1e16);
  EXPECT_GT(b.max.y, -1e16);
  Style dv = st;
  dv.s.width_space = GSHSCENE_STROKE_WIDTH_DEVICE;
  b = bound_of(s.store, dv, 1e16, 0.0, 1e16, 0.0, id);
  EXPECT_LT(b.min.x, 1e16);
  EXPECT_GT(b.max.x, 1e16);
  EXPECT_LT(b.min.y, 0.0);
  EXPECT_GT(b.max.y, 0.0);
  // At zero the outward step from 0 is not lost either.
  Style tiny;
  tiny.s.width = std::ldexp(1.0, -64);
  b = bound_of(s.store, tiny, 0.0, 0.0, 0.0, 0.0, id);
  EXPECT_LT(b.min.x, 0.0);
  EXPECT_GT(b.max.x, 0.0);
}

/* --- SK10, N13: the damage of strokes ---------------------------------------------------------------- */

namespace {

GSHSCENE_Damage damage_of(const GSHSCENE_Version * a, const GSHSCENE_Version * b, const GSHGEOM_Map * map = nullptr) {
  GSHSCENE_Diff * d = nullptr;
  GSHSCENE_Damage out{};
  EXPECT_EQ(gshscene_diff(a, b, map, nullptr, &d), GSHSCENE_OK);
  EXPECT_EQ(gshscene_diff_damage(d, &out), GSHSCENE_OK);
  gshscene_diff_destroy(d);
  return out;
}

/**
 * Whether the damage covers the real region (x0, y0, x1, y1) in pixels, outset by the AA support (1) and rounded
 * outward, and is at most one pixel larger on each side (SK10's cover and the mapping's rounding).
 */
void expect_covers(const GSHSCENE_Damage & d, double x0, double y0, double x1, double y1, const char * what) {
  ASSERT_EQ(d.unbounded, 0) << what;
  ASSERT_EQ(d.empty, 0) << what;
  const int64_t want[4] = {(int64_t)std::floor(x0 - 1.0), (int64_t)std::floor(y0 - 1.0), (int64_t)std::ceil(x1 + 1.0),
      (int64_t)std::ceil(y1 + 1.0)};
  EXPECT_LE(d.rect.x0, want[0]) << what;
  EXPECT_LE(d.rect.y0, want[1]) << what;
  EXPECT_GE(d.rect.x1, want[2]) << what;
  EXPECT_GE(d.rect.y1, want[3]) << what;
  EXPECT_GE(d.rect.x0, want[0] - 1) << what << ": loose";
  EXPECT_GE(d.rect.y0, want[1] - 1) << what;
  EXPECT_LE(d.rect.x1, want[2] + 1) << what;
  EXPECT_LE(d.rect.y1, want[3] + 1) << what;
}

}  // namespace

TEST(StrokeDamage, AChangedStrokeDamagesItsBoundsInBothVersions) {
  SceneHolder s;
  Ref square(intern_square(s.store())); // 0..4
  Ref miter, round, inside, device;
  Style m;
  m.s.width = 2.0;
  Style r = m;
  r.s.join = GSHSCENE_STROKE_JOIN_ROUND;
  Style in = r;
  in.s.alignment = GSHSCENE_STROKE_ALIGN_INSIDE;
  Style dv = r;
  dv.s.width_space = GSHSCENE_STROKE_WIDTH_DEVICE;
  ASSERT_EQ(intern(s.store(), m, &miter), GSHSCENE_OK);
  ASSERT_EQ(intern(s.store(), r, &round), GSHSCENE_OK);
  ASSERT_EQ(intern(s.store(), in, &inside), GSHSCENE_OK);
  ASSERT_EQ(intern(s.store(), dv, &device), GSHSCENE_OK);
  VersionRef v0, v1, v2, v3, v4;
  uint64_t id = 0;
  {
    EditHolder e(s);
    v0.reset(nullptr);
    publish(s, e, &v0);
  }
  {
    EditHolder e(s);
    GSHSCENE_Node n = stroke_node(square.get(), round.get());
    n.map = translate(10, 10);
    id = add(e.edit, GSHSCENE_ROOT_ID, n);
    publish(s, e, &v1);
  }
  // Round: h = 1: the square 10..14 outset to 9..15.
  expect_covers(damage_of(v0.get(), v1.get()), 9, 9, 15, 15, "an added round stroke");
  {
    EditHolder e(s);
    GSHSCENE_Node n = info_of(v1.get(), id).node;
    n.style = miter.get();
    ASSERT_EQ(gshscene_edit_set(e.edit, id, &n), GSHSCENE_OK);
    publish(s, e, &v2);
  }
  // Miter (limit 4): h = 4: 6..18, and the round version inside it.
  expect_covers(damage_of(v1.get(), v2.get()), 6, 6, 18, 18, "round to miter");
  expect_covers(damage_of(v2.get(), v1.get()), 6, 6, 18, 18, "miter to round");
  {
    EditHolder e(s);
    GSHSCENE_Node n = info_of(v2.get(), id).node;
    n.style = inside.get();
    ASSERT_EQ(gshscene_edit_set(e.edit, id, &n), GSHSCENE_OK);
    publish(s, e, &v3);
  }
  // Inside (round): twice the width, h = 2: 8..16.
  expect_covers(damage_of(v1.get(), v3.get()), 8, 8, 16, 16, "round to inside");
  {
    EditHolder e(s);
    GSHSCENE_Node n = info_of(v1.get(), id).node;
    n.style = device.get();
    ASSERT_EQ(gshscene_edit_set(e.edit, id, &n), GSHSCENE_OK);
    publish(s, e, &v4);
  }
  // Device width under a zoom of 4: the local stroke's pen is mapped (h = 4 pixels), the device stroke's is not (1).
  GSHGEOM_Map zoom = map_of(4, 0, 0, 4);
  GSHSCENE_Damage dz = damage_of(v1.get(), v4.get(), &zoom);
  // Local: (10 - 1) * 4 to (14 + 1) * 4, 36..60.  Device: 40..56 outset by 1 pixel, 39..57, inside it.
  expect_covers(dz, 36, 36, 60, 60, "local and device under a zoom");
  GSHSCENE_Damage only_device = damage_of(v0.get(), v4.get(), &zoom);
  expect_covers(only_device, 39, 39, 57, 57, "a device stroke's pen is not zoomed");
}

TEST(StrokeDamage, AForegroundChangeDamagesTheStrokesThatPaintWithIt) {
  SceneHolder s;
  Ref a(intern_square(s.store(), 0.0));
  Ref style;
  Style st;
  st.s.join = GSHSCENE_STROKE_JOIN_ROUND;
  ASSERT_EQ(intern(s.store(), st, &style), GSHSCENE_OK);
  VersionRef v1, v2;
  {
    EditHolder e(s);
    GSHSCENE_Node fg = stroke_node(a.get(), style.get());
    fg.paint = paint_of(GSHSCENE_PAINT_FOREGROUND, 1.0);
    fg.map = translate(20, 0);
    add(e.edit, GSHSCENE_ROOT_ID, fg);
    GSHSCENE_Node plain = stroke_node(a.get(), style.get());
    plain.map = translate(100, 100);
    add(e.edit, GSHSCENE_ROOT_ID, plain);
    publish(s, e, &v1);
  }
  {
    EditHolder e(s);
    GSHSCENE_Color red;
    gshscene_color_init(&red);
    red.rgba[0] = 1.0;
    ASSERT_EQ(gshscene_edit_set_foreground(e.edit, &red), GSHSCENE_OK);
    publish(s, e, &v2);
  }
  // Only the foreground stroke (round, h = 0.5): 19.5..24.5 by -0.5..4.5; the other stroke, at 100, is not in it.
  GSHSCENE_Damage d = damage_of(v1.get(), v2.get());
  expect_covers(d, 19.5, -0.5, 24.5, 4.5, "the stroke that paints with the foreground");
}

TEST(StrokeDamage, AStrokeOfARunIsBoundedByItsGlyphsOutset) {
  GSHSCENE_Test_Provider tp{}; // outlives the scene, which holds its face until the end
  SceneHolder s;
  Ref square(intern_square(s.store())); // 0..4
  GSHGEOM_Path * nothing = new_path();
  Ref empty(scene::intern(s.store(), nothing));
  gshgeom_path_destroy(nothing);
  // Outlines: squares at (120, 60) and (100, 50), the second the lower left, and an empty outline far away, which
  // bounds nothing; and a run left of and above the origin, its second glyph the upper right.
  GSHSCENE_Glyph glyphs[3] = {{1, 120.0, 60.0, square.get()}, {2, 500.0, 500.0, empty.get()}, {3, 100.0, 50.0, square.get()}};
  GSHSCENE_Glyph_Run run{};
  run.source = GSHSCENE_GLYPHS_FROM_OUTLINES;
  run.glyph_count = 3;
  run.glyphs = glyphs;
  Ref outlines;
  ASSERT_EQ(gshscene_store_intern_glyph_run(s.store(), &run, &outlines.resource), GSHSCENE_OK);
  GSHSCENE_Glyph negative[2] = {{1, -100.0, -50.0, square.get()}, {2, -80.0, -40.0, square.get()}};
  run.glyph_count = 2;
  run.glyphs = negative;
  Ref below;
  ASSERT_EQ(gshscene_store_intern_glyph_run(s.store(), &run, &below.resource), GSHSCENE_OK);
  // From a bound face (the test provider): glyph 1, a rectangle, at (300, 200), and glyph 0, which has no outline.
  Ref face(gshscene_tp_face(s.store(), &tp, 0, 0x5354u, 0));
  ASSERT_NE(face.get(), nullptr);
  Ref text(intern_font_run(s.store(), face.get(), 20.0, {{1, 300.0, 200.0, nullptr}, {0, 900.0, 900.0, nullptr}}));
  Ref style;
  Style st;
  st.s.width = 2.0;
  st.s.join = GSHSCENE_STROKE_JOIN_ROUND;
  ASSERT_EQ(intern(s.store(), st, &style), GSHSCENE_OK);
  VersionRef v1, v2, v3;
  {
    EditHolder e(s);
    publish(s, e, &v1);
  }
  {
    EditHolder e(s);
    add(e.edit, GSHSCENE_ROOT_ID, stroke_node(outlines.get(), style.get()));
    publish(s, e, &v2);
  }
  // h = 1 around 100..124 by 50..64.
  expect_covers(damage_of(v1.get(), v2.get()), 99, 49, 125, 65, "the outline run's glyphs");
  VersionRef vb;
  {
    EditHolder e(s);
    add(e.edit, GSHSCENE_ROOT_ID, stroke_node(below.get(), style.get()));
    publish(s, e, &vb);
    // h = 1 around -100..-76 by -50..-36: every corner below zero.
    expect_covers(damage_of(v2.get(), vb.get()), -101, -51, -75, -35, "a run left of and above the origin");
  }
  {
    EditHolder e(s);
    add(e.edit, GSHSCENE_ROOT_ID, stroke_node(text.get(), style.get()));
    publish(s, e, &v3);
  }
  // The font run: glyph 1's design box under its map into model space (the general route, SK7); glyph 0 is empty.
  GSHSCENE_Glyph_Box g{};
  ASSERT_EQ(gshscene_glyph_box(text.get(), 1, &g), GSHSCENE_OK);
  EXPECT_EQ(g.empty, 1) << "glyph 0 has no outline";
  ASSERT_EQ(gshscene_glyph_box(text.get(), 0, &g), GSHSCENE_OK);
  ASSERT_EQ(g.empty, 0);
  GSHGEOM_Map id = translate(0, 0);
  GSHGEOM_Map gm;
  ASSERT_EQ(gshscene_glyph_map(text.get(), &id, 0, &gm), GSHSCENE_OK);
  double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300;
  for (int32_t gx : {g.x_min, g.x_max}) {
    for (int32_t gy : {g.y_min, g.y_max}) {
      GSHGEOM_Point q;
      ASSERT_EQ(gshgeom_map_apply_point(&gm, P(gx / 64.0, gy / 64.0), &q), GSHGEOM_OK);
      x0 = std::min(x0, q.x);
      y0 = std::min(y0, q.y);
      x1 = std::max(x1, q.x);
      y1 = std::max(y1, q.y);
    }
  }
  EXPECT_GT(x0, 290.0) << "the glyph is placed at its origin";
  expect_covers(damage_of(vb.get(), v3.get()), x0 - 1, y0 - 1, x1 + 1, y1 + 1, "the font run's glyph");
}

TEST(StrokeDamage, AStrokeOfARunFromAnUnboundFaceIsUnbounded) {
  SceneHolder s;
  Ref face(intern_face(s.store()));
  Ref run(intern_font_run(s.store(), face.get(), 12.0, {{1, 0.0, 0.0, nullptr}}));
  Ref style;
  Style st;
  ASSERT_EQ(intern(s.store(), st, &style), GSHSCENE_OK);
  VersionRef v1, v2;
  {
    EditHolder e(s);
    publish(s, e, &v1);
  }
  {
    EditHolder e(s);
    add(e.edit, GSHSCENE_ROOT_ID, stroke_node(run.get(), style.get()));
    publish(s, e, &v2);
  }
  EXPECT_EQ(damage_of(v1.get(), v2.get()).unbounded, 1);
}

/* --- SK8, SK9, SK11, S20: the notice, the required features and the disposition ------------------------------------ */

namespace {

GSHSCENE_Sink_Caps all_caps() {
  GSHSCENE_Sink_Caps c{};
  c.item_kinds = 0xfeu;
  c.composites = 0x0fffffffu;
  c.flags = GSHSCENE_CAP_CAN_BLOCK | GSHSCENE_CAP_GLYPHS_FROM_FONT | GSHSCENE_CAP_CLIP_AA | GSHSCENE_CAP_BLEND_SPACES |
      GSHSCENE_CAP_GRADIENT_SPACES | GSHSCENE_CAP_STROKE_RUNS;
  c.gradient_kinds = 0xeu;
  c.stroke_native = GSHSCENE_STROKE_NATIVE_ALL;
  c.cancel_unit = GSHSCENE_CANCEL_BETWEEN_UNITS;
  c.work_measure = "units";
  return c;
}

/** A stroke item of @p style over the square, under @p map, painted with @p paint. */
GSHSCENE_Item stroke_item(const Kit & k, const GSHSCENE_Resource * style, GSHGEOM_Map map, GSHSCENE_Paint paint) {
  GSHSCENE_Item item{};
  item.kind = GSHSCENE_ITEM_STROKE;
  item.resource = k.square.get();
  item.style = style;
  item.map = map;
  item.fill_rule = GSHGEOM_FILL_NONZERO;
  item.paint = paint;
  return item;
}

}  // namespace

TEST(StrokeNotice, TheOrderIsMapThenWidthThenGradient) {
  Kit k;
  Ref zero;
  Style z;
  z.s.width = 0.0;
  ASSERT_EQ(intern(k.s.store, z, &zero), GSHSCENE_OK);
  GSHSCENE_Paint black = solid(0, 0, 0, 1);
  GSHGEOM_Map id = translate(0, 0);
  GSHGEOM_Map flat = map_of(0, 0, 0, 1);
  GSHGEOM_Map rank1 = map_of(1, 2, 2, 4);
  // A degenerate gradient: a linear one from a point to itself.
  GSHSCENE_Gradient_Stop stops[2] = {{0.0, solid(0, 0, 0, 1)}, {1.0, solid(1, 1, 1, 1)}};
  GSHSCENE_Gradient g{};
  g.kind = GSHSCENE_GRADIENT_LINEAR;
  gshgeom_map_identity(2, &g.map);
  g.stop_count = 2;
  g.stops = stops;
  const GSHSCENE_Resource * dr = nullptr;
  ASSERT_EQ(gshscene_store_intern_gradient(k.s.store, &g, &dr), GSHSCENE_OK);
  Ref degenerate(dr);
  g.map = flat;
  g.p1[0] = 1.0;
  ASSERT_EQ(gshscene_store_intern_gradient(k.s.store, &g, &dr), GSHSCENE_OK);
  Ref singular_map(dr);
  GSHSCENE_Paint dg = paint_of(GSHSCENE_PAINT_GRADIENT, 1.0, 0, degenerate.get());
  GSHSCENE_Paint sg = paint_of(GSHSCENE_PAINT_GRADIENT, 1.0, 0, singular_map.get());
  struct Case {
    const char * what;
    GSHSCENE_Item item;
    const GSHGEOM_Map * instances;
    size_t count;
    const GSHGEOM_Map * target;
    GSHSCENE_Reason want;
  } cases[] = {
      {"drawn", stroke_item(k, k.style.get(), id, black), nullptr, 0, nullptr, (GSHSCENE_Reason)0},
      {"width 0", stroke_item(k, zero.get(), id, black), nullptr, 0, nullptr, GSHSCENE_REASON_STROKE_ZERO_WIDTH},
      {"a flat item map", stroke_item(k, k.style.get(), flat, black), nullptr, 0, nullptr, GSHSCENE_REASON_MAP_NOT_INVERTIBLE},
      {"a flat target map", stroke_item(k, k.style.get(), id, black), nullptr, 0, &flat, GSHSCENE_REASON_MAP_NOT_INVERTIBLE},
      {"a flat instance map", stroke_item(k, k.style.get(), id, black), &rank1, 1, nullptr, GSHSCENE_REASON_MAP_NOT_INVERTIBLE},
      {"a flat gradient map", stroke_item(k, k.style.get(), id, sg), nullptr, 0, nullptr, GSHSCENE_REASON_MAP_NOT_INVERTIBLE},
      {"a degenerate gradient", stroke_item(k, k.style.get(), id, dg), nullptr, 0, nullptr, GSHSCENE_REASON_GRADIENT_DEGENERATE},
      {"width 0 before a degenerate gradient", stroke_item(k, zero.get(), id, dg), nullptr, 0, nullptr,
          GSHSCENE_REASON_STROKE_ZERO_WIDTH},
      {"the map before width 0", stroke_item(k, zero.get(), flat, dg), nullptr, 0, nullptr, GSHSCENE_REASON_MAP_NOT_INVERTIBLE},
      {"a device width under a flat map too (SK9)", stroke_item(k, k.device.get(), rank1, black), nullptr, 0, nullptr,
          GSHSCENE_REASON_MAP_NOT_INVERTIBLE},
  };
  for (const Case & c : cases) {
    EXPECT_EQ(gshscene_stroke_notice(&c.item, c.instances, c.count, c.target), c.want) << c.what;
    if (c.want) {
      EXPECT_EQ(gshscene_stroke_required(&c.item, c.instances, c.count, c.target), 0u)
          << c.what << ": what draws nothing requires nothing";
    }
  }
  // Anything that is not a stroke draws as stated; a zero-filled target map is the identity.
  GSHSCENE_Item fill = stroke_item(k, k.style.get(), flat, black);
  fill.kind = GSHSCENE_ITEM_FILL;
  EXPECT_EQ(gshscene_stroke_notice(&fill, nullptr, 0, nullptr), 0);
  EXPECT_EQ(gshscene_stroke_required(&fill, nullptr, 0, nullptr), 0u);
  EXPECT_EQ(gshscene_stroke_notice(nullptr, nullptr, 0, nullptr), 0);
  GSHGEOM_Map zero_filled;
  std::memset(&zero_filled, 0, sizeof zero_filled);
  GSHSCENE_Item ok = stroke_item(k, k.style.get(), id, black);
  EXPECT_EQ(gshscene_stroke_notice(&ok, nullptr, 0, &zero_filled), 0);
  EXPECT_EQ(gshscene_stroke_notice(&ok, nullptr, 2, nullptr), 0) << "instances counted and not given: not decided";
}

TEST(StrokeNotice, TheComposedMapIsDecidedToo) {
  // SK9 as amended by strokes story S4 (E6): invertible factors can compose into a map of determinant exactly zero.
  Kit k;
  GSHSCENE_Paint black = solid(0, 0, 0, 1);
  GSHGEOM_Map tiny = map_of(0x1p-600, 0, 0, 0x1p-600); // determinant 2^-1200, exactly nonzero
  GSHGEOM_Map huge = map_of(0x1p100, 0, 0, 0x1p100); // the largest entry a map takes
  GSHGEOM_Map id = translate(0, 0);
  int invertible = 0;
  ASSERT_EQ(gshgeom_stroke_map_invertible(&tiny, &invertible), GSHGEOM_OK);
  ASSERT_EQ(invertible, 1) << "each factor passes the factor test";
  GSHGEOM_Map composed;
  ASSERT_EQ(gshgeom_map_compose(&tiny, &tiny, &composed), GSHGEOM_OK);
  ASSERT_EQ(gshgeom_stroke_map_invertible(&composed, &invertible), GSHGEOM_OK);
  ASSERT_EQ(invertible, 0) << "the product underflows to a singular map";
  GSHSCENE_Item item = stroke_item(k, k.style.get(), tiny, black);
  // The item's map and the target's: each factor passes the factor test, the product does not.
  EXPECT_EQ(gshscene_stroke_notice(&item, nullptr, 0, &tiny), GSHSCENE_REASON_MAP_NOT_INVERTIBLE);
  // An instance's map and the item's.
  EXPECT_EQ(gshscene_stroke_notice(&item, &tiny, 1, nullptr), GSHSCENE_REASON_MAP_NOT_INVERTIBLE);
  // Two instances and the item, with the target last: every pair of the tiny maps composes to nothing.
  GSHGEOM_Map two[2] = {tiny, id};
  EXPECT_EQ(gshscene_stroke_notice(&item, two, 2, nullptr), GSHSCENE_REASON_MAP_NOT_INVERTIBLE);
  // The same chain with a factor that brings the product back is drawn.
  GSHSCENE_Item scaled = stroke_item(k, k.style.get(), huge, black);
  EXPECT_EQ(gshscene_stroke_notice(&scaled, nullptr, 0, &tiny), 0) << "2^100 times 2^-600 is 2^-500: not singular";
  // Width 0 still follows the map, and a composed map that cannot be formed is no notice (the sink's limit).
  EXPECT_EQ(gshscene_stroke_notice(&scaled, nullptr, 0, &huge), 0)
      << "the product passes the map bound (2^100): the sink's resource limit, not a notice";
  // An instance's map of determinant zero is the notice even when the chain cannot be composed (2^100 three times is
  // past the map bound): each map of the chain is decided by itself first (SK9), not only their product.
  GSHGEOM_Map flat = map_of(1, 0, 0, 0);
  GSHGEOM_Map past[2] = {flat, huge};
  EXPECT_EQ(gshscene_stroke_notice(&scaled, past, 2, &huge), GSHSCENE_REASON_MAP_NOT_INVERTIBLE)
      << "a singular instance map, and a product past the bound";
  GSHGEOM_Map past_ok[2] = {huge, huge};
  EXPECT_EQ(gshscene_stroke_notice(&scaled, past_ok, 2, &huge), 0)
      << "the same chain without the singular map is no notice (the sink's limit)";
  Ref zero;
  Style z;
  z.s.width = 0.0;
  ASSERT_EQ(intern(k.s.store, z, &zero), GSHSCENE_OK);
  GSHSCENE_Item flat_width = stroke_item(k, zero.get(), tiny, black);
  EXPECT_EQ(gshscene_stroke_notice(&flat_width, nullptr, 0, &tiny), GSHSCENE_REASON_MAP_NOT_INVERTIBLE)
      << "the map before the width";
  EXPECT_EQ(gshscene_stroke_required(&item, nullptr, 0, &tiny), 0u) << "what draws nothing requires nothing";
}

TEST(StrokeGeometry, TheStyleConvertsRowByRow) {
  Kit k;
  // Every field is copied, in the geometry library's own spelling, and the check accepts what the store holds.
  Style st;
  st.s.width = 3.25;
  st.s.width_space = GSHSCENE_STROKE_WIDTH_DEVICE;
  st.s.join = GSHSCENE_STROKE_JOIN_ARCS;
  st.s.miter_limit = 7.5;
  st.s.cap = GSHSCENE_STROKE_CAP_SQUARE;
  st.s.degenerate = GSHSCENE_STROKE_DEGENERATE_OMIT;
  static const double dashes[3] = {4.0, 2.0, 1.0};
  st.dashes.assign(dashes, dashes + 3);
  st.s.dash_offset = -1.5;
  st.s.dash_scope = GSHSCENE_STROKE_DASH_PATH;
  st.s.alignment = GSHSCENE_STROKE_ALIGN_OUTSIDE;
  st.s.alignment_rule = GSHGEOM_FILL_EVEN_ODD;
  Ref r;
  ASSERT_EQ(intern(k.s.store, st, &r), GSHSCENE_OK);
  GSHGEOM_Stroke g;
  ASSERT_EQ(gshscene_resource_stroke_geometry(r.get(), &g), GSHSCENE_OK);
  GSHSCENE_Stroke_Style back;
  ASSERT_EQ(gshscene_resource_stroke_style(r.get(), &back), GSHSCENE_OK);
  EXPECT_EQ(g.width, 3.25);
  EXPECT_EQ(g.width_space, (uint32_t)GSHGEOM_STROKE_WIDTH_DEVICE);
  EXPECT_EQ(g.join, (uint32_t)GSHGEOM_STROKE_JOIN_ARCS);
  EXPECT_EQ(g.miter_limit, 7.5);
  EXPECT_EQ(g.cap, (uint32_t)GSHGEOM_STROKE_CAP_SQUARE);
  EXPECT_EQ(g.degenerate, (uint32_t)GSHGEOM_STROKE_DEGENERATE_OMIT);
  EXPECT_EQ(g.dash_count, 6u) << "SK3 doubles an odd array";
  EXPECT_EQ(g.dashes, back.dashes) << "the dashes are the resource's own, not a copy";
  EXPECT_EQ(g.dash_offset, -1.5);
  EXPECT_EQ(g.dash_scope, (uint32_t)GSHGEOM_STROKE_DASH_PATH);
  EXPECT_EQ(g.alignment, (uint32_t)GSHGEOM_STROKE_ALIGN_OUTSIDE);
  EXPECT_EQ(g.alignment_rule, GSHGEOM_FILL_EVEN_ODD);
  EXPECT_EQ(g.width_profile, nullptr);
  for (uint8_t b : g._reserved) {
    EXPECT_EQ(b, 0);
  }
  EXPECT_EQ(gshgeom_stroke_check(&g), GSHGEOM_OK);
  // The defaults convert to the geometry library's defaults, field for field.
  GSHGEOM_Stroke want;
  gshgeom_stroke_init(&want);
  GSHGEOM_Stroke got;
  ASSERT_EQ(gshscene_resource_stroke_geometry(k.style.get(), &got), GSHSCENE_OK);
  GSHSCENE_Stroke_Style defaults;
  gshscene_stroke_style_init(&defaults);
  EXPECT_EQ(defaults.width, want.width);
  EXPECT_EQ(defaults.miter_limit, want.miter_limit);
  EXPECT_EQ(GSHSCENE_STROKE_VALUE_MIN, GSHGEOM_STROKE_VALUE_MIN);
  EXPECT_EQ(GSHSCENE_STROKE_VALUE_MAX, GSHGEOM_STROKE_VALUE_MAX);
  EXPECT_EQ(GSHSCENE_STROKE_MITER_LIMIT_MAX, GSHGEOM_STROKE_MITER_LIMIT_MAX);
  EXPECT_EQ(GSHSCENE_STROKE_MITER_LIMIT_DEFAULT, GSHGEOM_STROKE_MITER_LIMIT_DEFAULT);
  // Not a style: refused; nothing written.
  GSHGEOM_Stroke untouched = want;
  EXPECT_EQ(gshscene_resource_stroke_geometry(k.square.get(), &untouched), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_resource_stroke_geometry(nullptr, &untouched), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_resource_stroke_geometry(k.style.get(), nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(std::memcmp(&untouched, &want, sizeof want), 0);
}

TEST(StrokeRequired, AnisotropyIsDecidedOnTheComposedChain) {
  Kit k;
  GSHSCENE_Paint black = solid(0, 0, 0, 1);
  GSHGEOM_Map id = translate(0, 0);
  const uint32_t base = GSHSCENE_STROKE_NATIVE_JOIN_MITER | GSHSCENE_STROKE_NATIVE_CAP_BUTT |
      GSHSCENE_STROKE_NATIVE_WIDTH_LOCAL;
  const uint32_t aniso = base | GSHSCENE_STROKE_NATIVE_WIDTH_LOCAL_ANISOTROPIC;
  GSHGEOM_Map wide = map_of(2, 0, 0, 1);
  GSHGEOM_Map tall = map_of(1, 0, 0, 2);
  GSHGEOM_Map rot = map_of(0, -1, 1, 0);
  GSHGEOM_Map uni = map_of(3, 0, 0, 3, 7, -2);
  GSHGEOM_Map refl = map_of(0, 2, 2, 0);
  GSHSCENE_Item item = stroke_item(k, k.style.get(), id, black);
  EXPECT_EQ(gshscene_stroke_required(&item, nullptr, 0, nullptr), base);
  item.map = wide;
  EXPECT_EQ(gshscene_stroke_required(&item, nullptr, 0, nullptr), aniso) << "scale(2, 1)";
  item.map = rot;
  EXPECT_EQ(gshscene_stroke_required(&item, nullptr, 0, nullptr), base) << "a rotation";
  item.map = uni;
  EXPECT_EQ(gshscene_stroke_required(&item, nullptr, 0, nullptr), base) << "a uniform scale";
  item.map = refl;
  EXPECT_EQ(gshscene_stroke_required(&item, nullptr, 0, nullptr), base) << "a scaled reflection";
  // The chain: an instance's scale(2, 1) and the item's scale(1, 2) compose to a uniform scale; the target's own
  // scale(2, 1) after them makes it anisotropic again; a device width is never anisotropic.
  item.map = tall;
  EXPECT_EQ(gshscene_stroke_required(&item, &wide, 1, nullptr), base) << "anisotropic factors, a uniform product";
  EXPECT_EQ(gshscene_stroke_required(&item, &wide, 1, &wide), aniso) << "the target's map is in the chain";
  EXPECT_EQ(gshscene_stroke_required(&item, nullptr, 0, &wide), base) << "tall then wide is uniform";
  GSHSCENE_Item dev = stroke_item(k, k.device.get(), wide, black);
  EXPECT_EQ(gshscene_stroke_required(&dev, nullptr, 0, nullptr) & GSHSCENE_STROKE_NATIVE_WIDTH_LOCAL_ANISOTROPIC, 0u);
  EXPECT_NE(gshscene_stroke_required(&dev, nullptr, 0, nullptr) & GSHSCENE_STROKE_NATIVE_WIDTH_DEVICE, 0u);
  // A chain the composer cannot form (an entry of the product past the map bound) is taken as anisotropic: the
  // conservative answer.
  GSHGEOM_Map big = map_of(std::ldexp(1.0, 60), 0, 0, std::ldexp(1.0, 60));
  item.map = big;
  EXPECT_EQ(gshscene_stroke_required(&item, &big, 1, nullptr), aniso) << "2^120 per entry, past the bound";
}

TEST(StrokeRequired, EachFieldOfTheStyleRequiresItsFeature) {
  Kit k;
  GSHSCENE_Paint black = solid(0, 0, 0, 1);
  GSHGEOM_Map id = translate(0, 0);
  struct Case {
    const char * what;
    std::function<void(Style &)> set;
    uint32_t want;
  };
  const uint32_t L = GSHSCENE_STROKE_NATIVE_WIDTH_LOCAL;
  std::vector<Case> cases = {
      {"miter, butt", [](Style &) {}, GSHSCENE_STROKE_NATIVE_JOIN_MITER | GSHSCENE_STROKE_NATIVE_CAP_BUTT | L},
      {"round, round: the dot", [](Style & t) { t.s.join = GSHSCENE_STROKE_JOIN_ROUND; t.s.cap = GSHSCENE_STROKE_CAP_ROUND; },
          GSHSCENE_STROKE_NATIVE_JOIN_ROUND | GSHSCENE_STROKE_NATIVE_CAP_ROUND | L | GSHSCENE_STROKE_NATIVE_DEGENERATE_DRAW},
      {"bevel, square: the square dot", [](Style & t) { t.s.join = GSHSCENE_STROKE_JOIN_BEVEL; t.s.cap = GSHSCENE_STROKE_CAP_SQUARE; },
          GSHSCENE_STROKE_NATIVE_JOIN_BEVEL | GSHSCENE_STROKE_NATIVE_CAP_SQUARE | L | GSHSCENE_STROKE_NATIVE_DEGENERATE_DRAW},
      {"square, omitted", [](Style & t) { t.s.cap = GSHSCENE_STROKE_CAP_SQUARE; t.s.degenerate = GSHSCENE_STROKE_DEGENERATE_OMIT; },
          GSHSCENE_STROKE_NATIVE_JOIN_MITER | GSHSCENE_STROKE_NATIVE_CAP_SQUARE | L},
      {"butt, omitted", [](Style & t) { t.s.degenerate = GSHSCENE_STROKE_DEGENERATE_OMIT; },
          GSHSCENE_STROKE_NATIVE_JOIN_MITER | GSHSCENE_STROKE_NATIVE_CAP_BUTT | L},
      {"miter-clip", [](Style & t) { t.s.join = GSHSCENE_STROKE_JOIN_MITER_CLIP; },
          GSHSCENE_STROKE_NATIVE_JOIN_MITER_CLIP | GSHSCENE_STROKE_NATIVE_CAP_BUTT | L},
      {"arcs", [](Style & t) { t.s.join = GSHSCENE_STROKE_JOIN_ARCS; },
          GSHSCENE_STROKE_NATIVE_JOIN_ARCS | GSHSCENE_STROKE_NATIVE_CAP_BUTT | L},
      {"device", [](Style & t) { t.s.width_space = GSHSCENE_STROKE_WIDTH_DEVICE; },
          GSHSCENE_STROKE_NATIVE_JOIN_MITER | GSHSCENE_STROKE_NATIVE_CAP_BUTT | GSHSCENE_STROKE_NATIVE_WIDTH_DEVICE},
      {"dashes", [](Style & t) { t.dash({1.0, 1.0}); },
          GSHSCENE_STROKE_NATIVE_JOIN_MITER | GSHSCENE_STROKE_NATIVE_CAP_BUTT | L | GSHSCENE_STROKE_NATIVE_DASH},
      {"dashes along the path", [](Style & t) { t.dash({1.0, 1.0}).s.dash_scope = GSHSCENE_STROKE_DASH_PATH; },
          GSHSCENE_STROKE_NATIVE_JOIN_MITER | GSHSCENE_STROKE_NATIVE_CAP_BUTT | L | GSHSCENE_STROKE_NATIVE_DASH |
              GSHSCENE_STROKE_NATIVE_DASH_PATH_SCOPE},
      {"inside", [](Style & t) { t.s.alignment = GSHSCENE_STROKE_ALIGN_INSIDE; },
          GSHSCENE_STROKE_NATIVE_JOIN_MITER | GSHSCENE_STROKE_NATIVE_CAP_BUTT | L | GSHSCENE_STROKE_NATIVE_ALIGN},
      {"outside, even-odd", [](Style & t) { t.s.alignment = GSHSCENE_STROKE_ALIGN_OUTSIDE; t.s.alignment_rule = GSHGEOM_FILL_EVEN_ODD; },
          GSHSCENE_STROKE_NATIVE_JOIN_MITER | GSHSCENE_STROKE_NATIVE_CAP_BUTT | L | GSHSCENE_STROKE_NATIVE_ALIGN |
              GSHSCENE_STROKE_NATIVE_ALIGN_EVEN_ODD},
  };
  for (const Case & c : cases) {
    Style st;
    c.set(st);
    Ref r;
    ASSERT_EQ(intern(k.s.store, st, &r), GSHSCENE_OK) << c.what;
    GSHSCENE_Item item = stroke_item(k, r.get(), id, black);
    EXPECT_EQ(gshscene_stroke_required(&item, nullptr, 0, nullptr), c.want) << c.what;
  }
}

TEST(StrokeDisposition, StrokeStyleIsDecidedAfterTheOtherRefusalsAndBeforeAnyApproximation) {
  Kit k;
  GSHSCENE_Paint black = solid(0, 0, 0, 1);
  GSHGEOM_Map id = translate(0, 0);
  GSHSCENE_Color_Space srgb = srgb_space();
  GSHSCENE_Item item = stroke_item(k, k.style.get(), id, black);
  GSHSCENE_Sink_Caps caps = all_caps();
  auto d = [&](const GSHSCENE_Sink_Caps & c, const GSHSCENE_Item & it, const GSHGEOM_Map * inst = nullptr,
               size_t n = 0, const GSHGEOM_Map * target = nullptr, const GSHSCENE_Resource * pal = nullptr) {
    return gshscene_caps_disposition(&c, &it, inst, n, target, &srgb, pal);
  };
  EXPECT_EQ(d(caps, item).entry_class, 0) << "every feature native: drawn as a stroke";
  // A feature the sink lacks: refused, the missing features in detail; with STROKE_EXPAND, drawn.
  GSHSCENE_Sink_Caps lacks = caps;
  lacks.stroke_native &= ~(GSHSCENE_STROKE_NATIVE_JOIN_MITER | GSHSCENE_STROKE_NATIVE_DASH);
  GSHSCENE_Disposition x = d(lacks, item);
  EXPECT_EQ(x.entry_class, GSHSCENE_ENTRY_REFUSED);
  EXPECT_EQ(x.reason, GSHSCENE_REASON_STROKE_STYLE);
  EXPECT_EQ(x.detail, (uint64_t)GSHSCENE_STROKE_NATIVE_JOIN_MITER) << "only what this stroke requires";
  GSHSCENE_Sink_Caps expands = lacks;
  expands.flags |= GSHSCENE_CAP_STROKE_EXPAND;
  x = d(expands, item);
  EXPECT_EQ(x.entry_class, 0) << "expanded exactly: no entry";
  EXPECT_EQ(x.detail, 0u);
  // The chain decides anisotropy: a target map of scale(2, 1) makes the default style need the elliptical pen.
  GSHSCENE_Sink_Caps round_pen = caps;
  round_pen.stroke_native &= ~GSHSCENE_STROKE_NATIVE_WIDTH_LOCAL_ANISOTROPIC;
  GSHGEOM_Map wide = map_of(2, 0, 0, 1);
  EXPECT_EQ(d(round_pen, item).entry_class, 0);
  x = d(round_pen, item, nullptr, 0, &wide);
  EXPECT_EQ(x.reason, GSHSCENE_REASON_STROKE_STYLE);
  EXPECT_EQ(x.detail, (uint64_t)GSHSCENE_STROKE_NATIVE_WIDTH_LOCAL_ANISOTROPIC);
  x = d(round_pen, item, &wide, 1, nullptr);
  EXPECT_EQ(x.detail, (uint64_t)GSHSCENE_STROKE_NATIVE_WIDTH_LOCAL_ANISOTROPIC) << "an instance's map too";
  // What draws nothing requires nothing: a zero-width stroke on a sink that lacks every feature is not refused.
  Ref zero;
  Style z;
  z.s.width = 0.0;
  ASSERT_EQ(intern(k.s.store, z, &zero), GSHSCENE_OK);
  GSHSCENE_Sink_Caps bare = caps;
  bare.stroke_native = 0;
  EXPECT_EQ(d(bare, stroke_item(k, zero.get(), id, black)).entry_class, 0);
  EXPECT_EQ(d(bare, item).detail, (uint64_t)(GSHSCENE_STROKE_NATIVE_JOIN_MITER | GSHSCENE_STROKE_NATIVE_CAP_BUTT |
      GSHSCENE_STROKE_NATIVE_WIDTH_LOCAL));
  // The kind first: a sink without the stroke kind refuses ITEM_KIND, whatever the style.
  GSHSCENE_Sink_Caps no_kind = bare;
  no_kind.item_kinds &= ~(1u << GSHSCENE_ITEM_STROKE);
  EXPECT_EQ(d(no_kind, item).reason, GSHSCENE_REASON_ITEM_KIND);
  EXPECT_EQ(d(no_kind, stroke_item(k, zero.get(), id, black)).reason, GSHSCENE_REASON_ITEM_KIND);
  // A run from a font needs GLYPHS_FROM_FONT (SK7), before the style.
  GSHSCENE_Item font = item;
  font.resource = k.bound_run.get();
  GSHSCENE_Sink_Caps no_font = bare;
  no_font.flags &= ~GSHSCENE_CAP_GLYPHS_FROM_FONT;
  EXPECT_EQ(d(no_font, font).reason, GSHSCENE_REASON_GLYPH_SOURCE);
  EXPECT_EQ(d(bare, font).reason, GSHSCENE_REASON_STROKE_STYLE);
  // A gradient paint: its kind, then its palette, before the style; its interpolation space is approximated only when
  // the stroke is drawn.
  GSHSCENE_Item grad = stroke_item(k, k.style.get(), id, paint_of(GSHSCENE_PAINT_GRADIENT, 1.0, 0, k.gradient.get()));
  GSHSCENE_Sink_Caps no_radial = bare;
  no_radial.gradient_kinds &= ~(1u << GSHSCENE_GRADIENT_RADIAL);
  EXPECT_EQ(d(no_radial, grad).reason, GSHSCENE_REASON_GRADIENT);
  GSHSCENE_Item entry = stroke_item(k, k.style.get(), id, paint_of(GSHSCENE_PAINT_PALETTE, 1.0, 5));
  EXPECT_EQ(d(bare, entry).reason, GSHSCENE_REASON_PALETTE_ENTRY) << "a missing entry, before the style";
  EXPECT_EQ(d(caps, entry).reason, GSHSCENE_REASON_PALETTE_ENTRY);
  // An approximation never hides the style's refusal: a stated interpolation space on a sink without GRADIENT_SPACES.
  GSHSCENE_Gradient_Stop stops[2] = {{0.0, solid(0, 0, 0, 1)}, {1.0, solid(1, 1, 1, 1)}};
  stops[0].color.space = srgb_space(); // G3: a stated interpolation space takes stops in stated spaces
  stops[1].color.space = srgb_space();
  GSHSCENE_Gradient g{};
  g.kind = GSHSCENE_GRADIENT_LINEAR;
  g.p1[0] = 4.0;
  gshgeom_map_identity(2, &g.map);
  g.space = linear_srgb_space();
  g.stop_count = 2;
  g.stops = stops;
  const GSHSCENE_Resource * sr = nullptr;
  ASSERT_EQ(gshscene_store_intern_gradient(k.s.store, &g, &sr), GSHSCENE_OK);
  Ref spaced(sr);
  GSHSCENE_Item interp = stroke_item(k, k.style.get(), id, paint_of(GSHSCENE_PAINT_GRADIENT, 1.0, 0, spaced.get()));
  GSHSCENE_Sink_Caps no_spaces = caps;
  no_spaces.flags &= ~GSHSCENE_CAP_GRADIENT_SPACES;
  EXPECT_EQ(d(no_spaces, interp).entry_class, GSHSCENE_ENTRY_APPROXIMATED);
  EXPECT_EQ(d(no_spaces, interp).reason, GSHSCENE_REASON_INTERPOLATION_SPACE);
  GSHSCENE_Sink_Caps both = no_spaces;
  both.stroke_native = 0;
  EXPECT_EQ(d(both, interp).entry_class, GSHSCENE_ENTRY_REFUSED);
  EXPECT_EQ(d(both, interp).reason, GSHSCENE_REASON_STROKE_STYLE);
  // A fill is never decided by stroke features.
  GSHSCENE_Item fill = item;
  fill.kind = GSHSCENE_ITEM_FILL;
  fill.style = nullptr;
  EXPECT_EQ(d(bare, fill).entry_class, 0);
}

TEST(StrokeDisposition, ARunCenterlineNeedsStrokeRunsAndIsRefusedBeforeTheStyle) {
  Kit k;
  GSHSCENE_Paint black = solid(0, 0, 0, 1);
  GSHGEOM_Map id = translate(0, 0);
  GSHSCENE_Color_Space srgb = srgb_space();
  auto d = [&](const GSHSCENE_Sink_Caps & c, const GSHSCENE_Item & it) {
    return gshscene_caps_disposition(&c, &it, nullptr, 0, nullptr, &srgb, nullptr);
  };
  GSHSCENE_Sink_Caps caps = all_caps();
  GSHSCENE_Sink_Caps no_runs = caps;
  no_runs.flags &= ~GSHSCENE_CAP_STROKE_RUNS;
  GSHSCENE_Item outline_run = stroke_item(k, k.style.get(), id, black);
  outline_run.resource = k.outline_run.get();
  GSHSCENE_Item font_run = outline_run;
  font_run.resource = k.bound_run.get();
  // A sink that strokes runs draws both kinds of run; one that does not refuses each, whatever the style says.
  EXPECT_EQ(d(caps, outline_run).entry_class, 0);
  EXPECT_EQ(d(caps, font_run).entry_class, 0);
  for (const GSHSCENE_Item * it : {&outline_run, &font_run}) {
    GSHSCENE_Disposition x = d(no_runs, *it);
    EXPECT_EQ(x.entry_class, GSHSCENE_ENTRY_REFUSED);
    EXPECT_EQ(x.reason, GSHSCENE_REASON_STROKE_CENTERLINE);
    EXPECT_EQ(x.detail, 0u);
  }
  // A path is never refused for its centerline, and a fill of a run is a fill's business, not the stroke's.
  EXPECT_EQ(d(no_runs, stroke_item(k, k.style.get(), id, black)).entry_class, 0);
  GSHSCENE_Item fill = outline_run;
  fill.kind = GSHSCENE_ITEM_FILL;
  fill.style = nullptr;
  EXPECT_EQ(d(no_runs, fill).entry_class, 0);
  // The order: the kind, then a run from a font's provider, then the centerline, then the style.
  GSHSCENE_Sink_Caps bare = no_runs;
  bare.stroke_native = 0;
  EXPECT_EQ(d(bare, outline_run).reason, GSHSCENE_REASON_STROKE_CENTERLINE) << "before the style";
  GSHSCENE_Sink_Caps no_font = no_runs;
  no_font.flags &= ~GSHSCENE_CAP_GLYPHS_FROM_FONT;
  EXPECT_EQ(d(no_font, font_run).reason, GSHSCENE_REASON_GLYPH_SOURCE) << "the provider first";
  GSHSCENE_Sink_Caps no_kind = no_runs;
  no_kind.item_kinds &= ~(1u << GSHSCENE_ITEM_STROKE);
  EXPECT_EQ(d(no_kind, outline_run).reason, GSHSCENE_REASON_ITEM_KIND);
  // What draws nothing is not refused for what it would have drawn: width 0 and a singular map.
  Ref zero;
  Style z;
  z.s.width = 0.0;
  ASSERT_EQ(intern(k.s.store, z, &zero), GSHSCENE_OK);
  GSHSCENE_Item flat_width = stroke_item(k, zero.get(), id, black);
  flat_width.resource = k.outline_run.get();
  EXPECT_EQ(d(no_runs, flat_width).entry_class, 0);
  GSHSCENE_Item flat_map = outline_run;
  flat_map.map = map_of(0, 0, 0, 1);
  EXPECT_EQ(d(no_runs, flat_map).entry_class, 0);
}

// ST5 (strokes story S5, X6): a stroke of a run is planned in a stated order, and what draws nothing is refused for none of
// the three centerline refusals.
TEST(StrokeDisposition, ARunStrokeIsRefusedForItsFaceItsProviderAndItsCenterlineInThatOrder) {
  Kit k;
  GSHSCENE_Paint black = solid(0, 0, 0, 1);
  GSHGEOM_Map id = translate(0, 0);
  GSHSCENE_Color_Space srgb = srgb_space();
  auto d = [&](const GSHSCENE_Sink_Caps & c, const GSHSCENE_Item & it) {
    return gshscene_caps_disposition(&c, &it, nullptr, 0, nullptr, &srgb, nullptr);
  };
  GSHSCENE_Sink_Caps caps = all_caps();
  GSHSCENE_Item bound = stroke_item(k, k.style.get(), id, black);
  bound.resource = k.bound_run.get();
  GSHSCENE_Item unbound = bound;
  unbound.resource = k.font_run.get();
  GSHSCENE_Item outlines = bound;
  outlines.resource = k.outline_run.get();
  // (1) an unbound face: refused by the plan, whatever the hint mode and whatever the sink says of runs.
  GSHSCENE_Disposition x = d(caps, unbound);
  EXPECT_EQ(x.entry_class, GSHSCENE_ENTRY_REFUSED);
  EXPECT_EQ(x.reason, GSHSCENE_REASON_FACE_UNBOUND);
  EXPECT_EQ(x.detail, 0u);
  EXPECT_EQ(d(caps, bound).entry_class, 0) << "a bound face is stroked";
  EXPECT_EQ(d(caps, outlines).entry_class, 0) << "an outlines run has no face";
  // (2) the provider, (3) the centerline, (4) the style: each is found only when the ones before it pass.
  GSHSCENE_Sink_Caps no_font = caps;
  no_font.flags &= ~GSHSCENE_CAP_GLYPHS_FROM_FONT;
  EXPECT_EQ(d(no_font, bound).reason, GSHSCENE_REASON_GLYPH_SOURCE);
  EXPECT_EQ(d(no_font, unbound).reason, GSHSCENE_REASON_FACE_UNBOUND) << "the face is asked first (ST5 (1), (2))";
  GSHSCENE_Sink_Caps no_runs = caps;
  no_runs.flags &= ~GSHSCENE_CAP_STROKE_RUNS;
  EXPECT_EQ(d(no_runs, bound).reason, GSHSCENE_REASON_STROKE_CENTERLINE);
  EXPECT_EQ(d(no_runs, unbound).reason, GSHSCENE_REASON_FACE_UNBOUND) << "the face before the sink's flag";
  GSHSCENE_Sink_Caps bare = no_runs;
  bare.flags &= ~GSHSCENE_CAP_STROKE_EXPAND;
  bare.stroke_native = 0;
  EXPECT_EQ(d(bare, bound).reason, GSHSCENE_REASON_STROKE_CENTERLINE) << "the flag before the style";
  bare.flags |= GSHSCENE_CAP_STROKE_RUNS;
  EXPECT_EQ(d(bare, bound).reason, GSHSCENE_REASON_STROKE_STYLE);
  // A fill of the unbound run is a fill's business (the route reports it), not the disposition's.
  GSHSCENE_Item fill = unbound;
  fill.kind = GSHSCENE_ITEM_FILL;
  fill.style = nullptr;
  EXPECT_EQ(d(caps, fill).entry_class, 0);
  // What draws nothing is refused for none of them: width 0 and a singular map, on an unbound face, a sink without the
  // provider and a sink without runs alike.
  Ref zero;
  Style z;
  z.s.width = 0.0;
  ASSERT_EQ(intern(k.s.store, z, &zero), GSHSCENE_OK);
  GSHSCENE_Item flat_width = unbound;
  flat_width.style = zero.get();
  GSHSCENE_Item flat_map = unbound;
  flat_map.map = map_of(0, 0, 0, 1);
  GSHSCENE_Sink_Caps neither = no_runs;
  neither.flags &= ~GSHSCENE_CAP_GLYPHS_FROM_FONT;
  for (const GSHSCENE_Item * it : {&flat_width, &flat_map}) {
    EXPECT_EQ(d(caps, *it).entry_class, 0);
    EXPECT_EQ(d(neither, *it).entry_class, 0) << "its notice, nothing else";
  }
}

TEST(StrokePlan, ThePlanningPassRefusesAndNoticesStrokesBeforeTheSinkIsCalled) {
  Kit k;
  // A list with a stroke the caps refuse inside an instance, a zero-width stroke and a drawn one.
  Ref zero;
  Style z;
  z.s.width = 0.0;
  ASSERT_EQ(intern(k.s.store, z, &zero), GSHSCENE_OK);
  GSHSCENE_Paint black = solid(0, 0, 0, 1);
  ListHolder pl(k.s.store);
  ASSERT_EQ(gshscene_list_stroke(pl.list, k.square.get(), k.style.get(), nullptr, &black, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(pl.list), GSHSCENE_OK);
  const GSHSCENE_Resource * pr = nullptr;
  ASSERT_EQ(gshscene_store_intern_picture(k.s.store, pl.list, &pr), GSHSCENE_OK);
  Ref pic(pr);
  ListHolder l(k.s.store);
  GSHGEOM_Map wide = map_of(2, 0, 0, 1);
  ASSERT_EQ(gshscene_list_instance(l.list, pic.get(), &wide, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_stroke(l.list, k.square.get(), zero.get(), nullptr, &black, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_stroke(l.list, k.square.get(), k.style.get(), nullptr, &black, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
  // A sink whose pen is round: the instance's scale(2, 1) makes the picture's stroke need the elliptical pen.
  GSHSCENE_Dump_Sink_Config c;
  gshscene_dump_sink_config_default(&c);
  c.stream_caps.stroke_native &= ~GSHSCENE_STROKE_NATIVE_WIDTH_LOCAL_ANISOTROPIC;
  GSHSCENE_Sink * sink = nullptr;
  ASSERT_EQ(gshscene_dump_sink_create(nullptr, nullptr, &c, &sink), GSHSCENE_OK);
  std::string bytes;
  GSHSCENE_Writer w{[](void * ctx, const void * data, size_t n) {
    ((std::string *)ctx)->append((const char *)data, n);
    return GSHSCENE_OK;
  }, &bytes};
  GSHSCENE_Target t{};
  t.kind = GSHSCENE_TARGET_STREAM;
  t.surface = &w;
  t.unit = GSHSCENE_UNIT_POINT;
  GSHSCENE_Report * report = nullptr;
  ASSERT_EQ(gshscene_report_create(nullptr, nullptr, &report), GSHSCENE_OK);
  GSHSCENE_Render_Request req{};
  req.list = l.list;
  req.target = &t;
  req.report = report;
  ASSERT_EQ(gshscene_sink_render(sink, &req), GSHSCENE_OK);
  ASSERT_EQ(gshscene_report_count(report), 2u);
  GSHSCENE_Report_Entry e;
  ASSERT_EQ(gshscene_report_entry(report, 0, &e), GSHSCENE_OK);
  EXPECT_EQ(e.entry_class, GSHSCENE_ENTRY_REFUSED);
  EXPECT_EQ(e.reason, GSHSCENE_REASON_STROKE_STYLE);
  ASSERT_EQ(e.chain_count, 2u) << "the picture's stroke, keyed through its instance";
  EXPECT_EQ(e.second, (size_t)GSHSCENE_STROKE_NATIVE_WIDTH_LOCAL_ANISOTROPIC);
  ASSERT_EQ(gshscene_report_entry(report, 1, &e), GSHSCENE_OK);
  EXPECT_EQ(e.entry_class, GSHSCENE_ENTRY_NOTICE);
  EXPECT_EQ(e.reason, GSHSCENE_REASON_STROKE_ZERO_WIDTH);
  EXPECT_EQ(e.chain[0], 1u);
  EXPECT_NE(bytes.find("U 0.0 X class=1 reason=28"), std::string::npos) << bytes;
  EXPECT_NE(bytes.find("U 1 X class=5 reason=29"), std::string::npos) << bytes;
  EXPECT_NE(bytes.find("U 2 K res=path:"), std::string::npos) << bytes;
  EXPECT_NE(bytes.find("expand=0 outline="), std::string::npos) << bytes;
  EXPECT_EQ(bytes.find("outline=-"), std::string::npos) << "a stroke of a path has its expansion's hash (SC10)\n" << bytes;
  // Strict: the planned refusal fails the render and nothing is written.
  bytes.clear();
  req.flags = GSHSCENE_RENDER_STRICT;
  EXPECT_EQ(gshscene_sink_render(sink, &req), GSHSCENE_ERR_REFUSED);
  EXPECT_TRUE(bytes.empty());
  gshscene_report_destroy(report);
  EXPECT_EQ(gshscene_sink_destroy(sink), GSHSCENE_OK);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
