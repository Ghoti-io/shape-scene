/**
 * @file
 *
 * Gradients (color-paint story P1; G1 to G13 of paint.h and documentation/design.md): the gradient resource (its
 * rules, its canonical form, the stable sort of its stops, interning, hash and dump against the independent
 * generator, a planted collision), the gradient paint in a list and in a retained scene (references, equality, hash,
 * damage), the one conversion of the stops at draw time (gshscene_gradient_resolve, against the oracle's
 * conversions), the one predicate of what draws nothing (G10, G11), the planning pass (S17: refusal by kind, the
 * interpolation space, the palette, the notices) and the test sink's gradient records, the float-free constructor,
 * and tests/ref_composite.h's evaluator against tools/gradient-oracle.py's answers (tests/gradient_vectors.h).
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "test_helpers.h"

#include "../glyph_test_provider.h"
#include "../gradient_vectors.h"
#include "../known_answers.h"
#include "ref_composite.h"
#include "../scene_fixtures.h"
#include "../scene_helpers.h"

#include <ghoti.io/shape-scene/fixed.h>

#include <cmath>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

using namespace scene;

namespace {

const double kNaN = std::nan("");
const double kInf = HUGE_VAL;

GSHSCENE_Paint fg_paint(double alpha) {
  GSHSCENE_Paint p{};
  p.kind = GSHSCENE_PAINT_FOREGROUND;
  p.rgba[3] = alpha;
  return p;
}

GSHSCENE_Paint entry_paint(uint32_t entry, double alpha) {
  GSHSCENE_Paint p{};
  p.kind = GSHSCENE_PAINT_PALETTE;
  p.entry = entry;
  p.rgba[3] = alpha;
  return p;
}

GSHSCENE_Paint spaced(double r, double g, double b, double a, GSHSCENE_Color_Space sp) {
  GSHSCENE_Paint p = solid(r, g, b, a);
  p.space = sp;
  return p;
}

GSHSCENE_Paint gradient_paint(const GSHSCENE_Resource * g, double alpha = 1.0) {
  GSHSCENE_Paint p{};
  p.kind = GSHSCENE_PAINT_GRADIENT;
  p.gradient = g;
  p.rgba[3] = alpha;
  return p;
}

GSHGEOM_Map identity() {
  GSHGEOM_Map m;
  gshgeom_map_identity(2, &m);
  return m;
}

GSHGEOM_Map design_map() {
  GSHGEOM_Map m;
  gshgeom_map_design_to_model(2.5, 10, 20, &m);
  return m;
}

GSHGEOM_Map matrix(double a, double b, double c, double d, double e = 0, double f = 0) {
  const double m[4] = {a, b, c, d};
  const double t[2] = {e, f};
  GSHGEOM_Map out;
  EXPECT_EQ(gshgeom_map_from_matrix(2, m, t, &out), GSHGEOM_OK);
  return out;
}

GSHSCENE_Color_Space p3_gamma() {
  return space_of(GCOL_PRIMARIES_DISPLAY_P3, GCOL_TRANSFER_GAMMA, 2.2);
}

GSHSCENE_Color_Space adobe_gamma() {
  return space_of(GCOL_PRIMARIES_ADOBE_RGB, GCOL_TRANSFER_GAMMA, 2.19921875);
}

/** A gradient description, owning its stops. */
struct Desc {
  GSHSCENE_Gradient g{};
  std::vector<GSHSCENE_Gradient_Stop> stops;
  Desc() {
    g.map = identity();
  }
  Desc & stop(double offset, GSHSCENE_Paint color) {
    stops.push_back(GSHSCENE_Gradient_Stop{offset, color});
    return *this;
  }
  const GSHSCENE_Gradient * get() {
    g.stop_count = stops.size();
    g.stops = stops.empty() ? nullptr : stops.data();
    return &g;
  }
};

Desc linear(double x0, double y0, double x1, double y1, double dx = 0, double dy = 0) {
  Desc d;
  d.g.kind = GSHSCENE_GRADIENT_LINEAR;
  d.g.p0[0] = x0;
  d.g.p0[1] = y0;
  d.g.p1[0] = x1;
  d.g.p1[1] = y1;
  d.g.d[0] = dx;
  d.g.d[1] = dy;
  return d;
}

Desc radial(double c0x, double c0y, double r0, double c1x, double c1y, double r1) {
  Desc d;
  d.g.kind = GSHSCENE_GRADIENT_RADIAL;
  d.g.c0[0] = c0x;
  d.g.c0[1] = c0y;
  d.g.r0 = r0;
  d.g.c1[0] = c1x;
  d.g.c1[1] = c1y;
  d.g.r1 = r1;
  return d;
}

Desc sweep(double cx, double cy, uint32_t unit, double a0, double a1) {
  Desc d;
  d.g.kind = GSHSCENE_GRADIENT_SWEEP;
  d.g.center[0] = cx;
  d.g.center[1] = cy;
  d.g.angle_unit = unit;
  d.g.a0 = a0;
  d.g.a1 = a1;
  return d;
}

/** The three gradients of tools/gen-scene-vectors.py, built through the API. */
Desc gen_linear() {
  Desc d = linear(0, 0, 10, 0, 1, 3);
  d.g.map = translate(1.5, -2.25);
  d.g.extend = GSHSCENE_EXTEND_REPEAT;
  d.g.space = linear_srgb_space();
  d.g.premultiplied = 1;
  d.stop(1.0, spaced(1.0, 0.5, 0.25, 1.0, srgb_space()))
      .stop(0.5, fg_paint(0.5))
      .stop(-0.0, entry_paint(2, 0.75))
      .stop(0.5, spaced(0.0, 1.0, 0.0, 0.25, p3_gamma()));
  return d;
}

Desc gen_radial() {
  Desc d = radial(1.0, 2.0, -1.5, 4.0, 2.0, 6.25);
  d.g.map = design_map();
  d.stop(0.0, solid(0, 0, 0, 1)).stop(1.0, solid(1, 1, 1, 0));
  return d;
}

Desc gen_sweep() {
  Desc d = sweep(3.0, -4.0, GSHGEOM_ANGLE_DEGREES, 90.0, 450.0);
  d.g.extend = GSHSCENE_EXTEND_REFLECT;
  d.g.space = p3_gamma();
  d.stop(0.25, spaced(0.5, 0.25, 1.0, 1.0, adobe_gamma()));
  return d;
}

const GSHSCENE_Resource * intern_g(GSHSCENE_Store * store, Desc d) {
  const GSHSCENE_Resource * r = nullptr;
  EXPECT_EQ(gshscene_store_intern_gradient(store, d.get(), &r), GSHSCENE_OK);
  return r;
}

GSHSCENE_Result try_intern(GSHSCENE_Store * store, Desc d) {
  const GSHSCENE_Resource * r = nullptr;
  GSHSCENE_Result result = gshscene_store_intern_gradient(store, d.get(), &r);
  gshscene_resource_release(r);
  return result;
}

uint64_t bits(double x) {
  return gshgeom_f64_bits(x);
}

GSHSCENE_Color_Space srgb() {
  return srgb_space();
}

}  // namespace

// --- the pinned values -------------------------------------------------------------------------------------------------

TEST(GradientValues, ThePinnedValuesArePinned) {
  EXPECT_EQ((int)GSHSCENE_PAINT_GRADIENT, 1);
  EXPECT_EQ((int)GSHSCENE_RESOURCE_GRADIENT, 4);
  EXPECT_EQ((int)GSHSCENE_GRADIENT_LINEAR, 1);
  EXPECT_EQ((int)GSHSCENE_GRADIENT_RADIAL, 2);
  EXPECT_EQ((int)GSHSCENE_GRADIENT_SWEEP, 3);
  EXPECT_EQ((int)GSHSCENE_GRADIENT_KIND_COUNT, 4);
  EXPECT_EQ((int)GSHSCENE_EXTEND_PAD, 0);
  EXPECT_EQ((int)GSHSCENE_EXTEND_REPEAT, 1);
  EXPECT_EQ((int)GSHSCENE_EXTEND_REFLECT, 2);
  EXPECT_EQ((int)GSHSCENE_EXTEND_COUNT, 3);
  EXPECT_EQ(GSHSCENE_BYTES_PER_STOP, 96u);
  EXPECT_EQ(GSHSCENE_FIXED_GRADIENT_LINEAR, (unsigned)GSHSCENE_GRADIENT_LINEAR);
  EXPECT_EQ(GSHSCENE_FIXED_GRADIENT_RADIAL, (unsigned)GSHSCENE_GRADIENT_RADIAL);
  EXPECT_EQ(GSHSCENE_FIXED_GRADIENT_SWEEP, (unsigned)GSHSCENE_GRADIENT_SWEEP);
  EXPECT_EQ(sizeof(GSHSCENE_Sink_Caps) % 8u, 0u);
  EXPECT_EQ(GSHSCENE_RESOLVE_TOLERANCE, 1e-13);
  EXPECT_EQ(GSHSCENE_GRADIENT_VALUE_MAX, 0x1p64);
  EXPECT_EQ(GSHSCENE_GRADIENT_VALUE_MIN, 0x1p-64);
  EXPECT_EQ(GSHSCENE_GRADIENT_STOPS_MAX, 65535u);
  EXPECT_EQ(GSHSCENE_GRADIENT_DOMAIN, 0x1p128);
  EXPECT_STREQ(gshscene_resource_kind_string(GSHSCENE_RESOURCE_GRADIENT), "gradient");
}

// --- G1, G2: the resource -----------------------------------------------------------------------------------------------

TEST(GradientResource, EachKindInternsAndReadsBackCanonically) {
  StoreHolder s;
  for (Desc d : {gen_linear(), gen_radial(), gen_sweep()}) {
    Ref g(intern_g(s.store, d));
    ASSERT_NE(g.get(), nullptr);
    EXPECT_EQ(gshscene_resource_kind(g.get()), GSHSCENE_RESOURCE_GRADIENT);
    GSHSCENE_Gradient back;
    ASSERT_EQ(gshscene_resource_gradient(g.get(), &back), GSHSCENE_OK);
    EXPECT_EQ(back.kind, d.g.kind);
    EXPECT_EQ(back.extend, d.g.extend);
    EXPECT_EQ(back.premultiplied, d.g.premultiplied);
    EXPECT_EQ(back.angle_unit, d.g.angle_unit);
    EXPECT_EQ(gshgeom_map_hash(&back.map), gshgeom_map_hash(&d.g.map));
    EXPECT_EQ(back.space.primaries, d.g.space.primaries);
    ASSERT_EQ(back.stop_count, d.stops.size());
    for (size_t i = 1; i < back.stop_count; i++) {
      EXPECT_LE(back.stops[i - 1].offset, back.stops[i].offset) << "sorted (G2)";
    }
    EXPECT_EQ(bits(back.p0[0]), bits(d.g.p0[0]));
    EXPECT_EQ(bits(back.d[1]), bits(d.g.d[1]));
    EXPECT_EQ(bits(back.r0), bits(d.g.r0));
    EXPECT_EQ(bits(back.r1), bits(d.g.r1));
    EXPECT_EQ(bits(back.center[1]), bits(d.g.center[1]));
    EXPECT_EQ(bits(back.a1), bits(d.g.a1));
    EXPECT_EQ(gshscene_resource_bytes(g.get()), GSHSCENE_BYTES_BASE + d.stops.size() * GSHSCENE_BYTES_PER_STOP);
  }
  GSHSCENE_Gradient out;
  EXPECT_EQ(gshscene_resource_gradient(nullptr, &out), GSHSCENE_ERR_INVALID);
  Ref sq(intern_square(s.store));
  EXPECT_EQ(gshscene_resource_gradient(sq.get(), &out), GSHSCENE_ERR_INVALID);
  Ref g(intern_g(s.store, gen_radial()));
  EXPECT_EQ(gshscene_resource_gradient(g.get(), nullptr), GSHSCENE_ERR_INVALID);
}

TEST(GradientResource, TheStopsAreSortedStablyAndMinusZeroIsZero) {
  StoreHolder s;
  // The generator's linear fixture: -0 first, then the two at 0.5 in the order given, then 1.
  Ref g(intern_g(s.store, gen_linear()));
  GSHSCENE_Gradient back;
  ASSERT_EQ(gshscene_resource_gradient(g.get(), &back), GSHSCENE_OK);
  ASSERT_EQ(back.stop_count, 4u);
  EXPECT_EQ(bits(back.stops[0].offset), 0u) << "-0 is stored as +0";
  EXPECT_EQ(back.stops[0].color.kind, GSHSCENE_PAINT_PALETTE);
  EXPECT_EQ(back.stops[1].color.kind, GSHSCENE_PAINT_FOREGROUND) << "equal offsets keep the given order";
  EXPECT_EQ(back.stops[2].color.kind, GSHSCENE_PAINT_SOLID);
  EXPECT_EQ(back.stops[3].offset, 1.0);
  // The two orders of two equal offsets are two gradients; the same order is one.
  Desc a = linear(0, 0, 1, 0);
  a.stop(0.5, solid(1, 0, 0, 1)).stop(0.5, solid(0, 0, 1, 1));
  Desc b = linear(0, 0, 1, 0);
  b.stop(0.5, solid(0, 0, 1, 1)).stop(0.5, solid(1, 0, 0, 1));
  Ref ga(intern_g(s.store, a));
  Ref gb(intern_g(s.store, b));
  Ref ga2(intern_g(s.store, a));
  EXPECT_NE(ga.get(), gb.get());
  EXPECT_EQ(ga.get(), ga2.get());
  EXPECT_NE(gshscene_resource_hash(ga.get()), gshscene_resource_hash(gb.get()));
  // Order given does not matter for distinct offsets; a long line is sorted (the merge sort's runs).
  Desc up = linear(0, 0, 1, 0);
  Desc down = linear(0, 0, 1, 0);
  for (int i = 0; i < 37; i++) {
    up.stop((double)i / 4.0, solid((double)i / 64.0, 0, 0, 1));
    down.stop((double)(36 - i) / 4.0, solid((double)(36 - i) / 64.0, 0, 0, 1));
  }
  Ref gu(intern_g(s.store, up));
  Ref gd(intern_g(s.store, down));
  EXPECT_EQ(gu.get(), gd.get());
  // Equal offsets inside a long line keep their order through every pass.
  Desc mix = linear(0, 0, 1, 0);
  for (int i = 0; i < 21; i++) {
    mix.stop((double)(i % 3), solid((double)i / 32.0, 0, 0, 1));
  }
  Ref gm(intern_g(s.store, mix));
  ASSERT_EQ(gshscene_resource_gradient(gm.get(), &back), GSHSCENE_OK);
  for (size_t i = 1; i < back.stop_count; i++) {
    if (back.stops[i].offset == back.stops[i - 1].offset) {
      EXPECT_LT(back.stops[i - 1].color.rgba[0], back.stops[i].color.rgba[0]) << "stable at " << i;
    } else {
      EXPECT_LT(back.stops[i - 1].offset, back.stops[i].offset);
    }
  }
  // -0 and +0 in the geometry are one gradient.
  Ref z1(intern_g(s.store, linear(-0.0, 0, 1, 0)));
  Ref z2(intern_g(s.store, linear(0.0, 0, 1, 0)));
  EXPECT_EQ(z1.get(), z2.get());
}

TEST(GradientResource, EqualContentIsOneHandleAndEveryFieldTellsTwoApart) {
  StoreHolder s;
  Ref base(intern_g(s.store, gen_linear()));
  Ref again(intern_g(s.store, gen_linear()));
  EXPECT_EQ(base.get(), again.get());
  GSHSCENE_Store_Stats st;
  ASSERT_EQ(gshscene_store_stats(s.store, &st), GSHSCENE_OK);
  EXPECT_EQ(st.hits, 1u);
  std::vector<Desc> others;
  for (int field = 0; field < 16; field++) {
    Desc d = gen_linear();
    switch (field) {
      case 0: d.g.p0[0] = 0.5; break;
      case 1: d.g.p0[1] = 0.5; break;
      case 2: d.g.p1[0] = 9; break;
      case 3: d.g.p1[1] = 1; break;
      case 4: d.g.d[0] = 2; break;
      case 5: d.g.d[1] = 2; break;
      case 6: d.g.extend = GSHSCENE_EXTEND_REFLECT; break;
      case 7: d.g.premultiplied = 0; break;
      case 8: d.g.space = srgb(); break;
      case 9: d.g.map = translate(1.5, 2.25); break;
      case 10: d.stops[0].offset = 0.875; break;
      case 11: d.stops[0].color.rgba[2] = 0.5; break;
      case 12: d.stops[1].color.rgba[3] = 0.25; break;
      case 13: d.stops[2].color.entry = 3; break;
      case 14: d.stops[0].color.space = linear_srgb_space(); break;
      case 15: d.stops.pop_back(); break;
    }
    others.push_back(d);
  }
  for (size_t i = 0; i < others.size(); i++) {
    Ref g(intern_g(s.store, others[i]));
    EXPECT_NE(g.get(), base.get()) << "field " << i;
    EXPECT_NE(gshscene_resource_hash(g.get()), gshscene_resource_hash(base.get())) << "field " << i;
  }
  // The other kinds' fields: a radial and a sweep differing in each of theirs.
  Ref r(intern_g(s.store, gen_radial()));
  for (int field = 0; field < 6; field++) {
    Desc d = gen_radial();
    double * v[6] = {&d.g.c0[0], &d.g.c0[1], &d.g.r0, &d.g.c1[0], &d.g.c1[1], &d.g.r1};
    *v[field] += 0.5;
    Ref g(intern_g(s.store, d));
    EXPECT_NE(g.get(), r.get()) << "radial field " << field;
  }
  Ref w(intern_g(s.store, gen_sweep()));
  for (int field = 0; field < 5; field++) {
    Desc d = gen_sweep();
    double * v[4] = {&d.g.center[0], &d.g.center[1], &d.g.a0, &d.g.a1};
    if (field < 4) {
      *v[field] += 0.5;
    } else {
      d.g.angle_unit = GSHGEOM_ANGLE_GRADIANS;
    }
    Ref g(intern_g(s.store, d));
    EXPECT_NE(g.get(), w.get()) << "sweep field " << field;
  }
}

TEST(GradientResource, APlantedCollisionNeverMergesUnequalGradients) {
  StoreHolder s(nullptr, nullptr, all_zero);
  Ref a(intern_g(s.store, gen_linear()));
  Ref b(intern_g(s.store, gen_radial()));
  Ref c(intern_g(s.store, gen_sweep()));
  Desc near = gen_linear();
  near.stops[3].color.rgba[0] = 0.0625;
  Ref d(intern_g(s.store, near));
  EXPECT_NE(a.get(), b.get());
  EXPECT_NE(a.get(), c.get());
  EXPECT_NE(a.get(), d.get());
  Ref a2(intern_g(s.store, gen_linear()));
  EXPECT_EQ(a.get(), a2.get());
  // Equality alone tells each field apart, the geometry of every kind and the unit included.
  std::vector<Ref> held;
  std::vector<const GSHSCENE_Resource *> seen = {a.get(), b.get(), c.get(), d.get()};
  for (int field = 0; field < 23; field++) {
    Desc v = field < 6 ? gen_linear() : field < 12 ? gen_radial() : field < 17 ? gen_sweep() : gen_linear();
    switch (field) {
      case 0: v.g.p0[0] += 1; break;
      case 1: v.g.p0[1] += 1; break;
      case 2: v.g.p1[0] += 1; break;
      case 3: v.g.p1[1] += 1; break;
      case 4: v.g.d[0] += 1; break;
      case 5: v.g.d[1] += 1; break;
      case 6: v.g.c0[0] += 1; break;
      case 7: v.g.c0[1] += 1; break;
      case 8: v.g.r0 += 1; break;
      case 9: v.g.c1[0] += 1; break;
      case 10: v.g.c1[1] += 1; break;
      case 11: v.g.r1 += 1; break;
      case 12: v.g.center[0] += 1; break;
      case 13: v.g.center[1] += 1; break;
      case 14: v.g.a0 += 1; break;
      case 15: v.g.a1 += 1; break;
      case 16: v.g.angle_unit = GSHGEOM_ANGLE_TURNS; break;
      case 17: v.g.extend = GSHSCENE_EXTEND_PAD; break;
      case 18: v.g.premultiplied = 0; break;
      case 19: v.g.space = srgb(); break;
      case 20: v.g.map = translate(1.5, 2.25); break;
      case 21: v.stops[3].offset = 0.875; break;
      case 22: v.stops[3].color.space = linear_srgb_space(); break;
    }
    held.emplace_back(intern_g(s.store, v));
    for (const GSHSCENE_Resource * other : seen) {
      EXPECT_NE(held.back().get(), other) << "field " << field;
    }
    seen.push_back(held.back().get());
  }
  // The same map values with another kind tag are another gradient.
  Desc tagged = gen_linear();
  tagged.g.map.kind |= GSHGEOM_MAP_SKEWS;
  Ref t1(intern_g(s.store, tagged));
  EXPECT_NE(t1.get(), a.get());
  GSHSCENE_Store_Stats st;
  ASSERT_EQ(gshscene_store_stats(s.store, &st), GSHSCENE_OK);
  EXPECT_GT(st.collisions, 0u);
}

TEST(GradientResource, EveryRuleIsCheckedAndAFailureKeepsNothing) {
  StoreHolder s;
  const GSHSCENE_Resource * out = nullptr;
  EXPECT_EQ(gshscene_store_intern_gradient(nullptr, gen_linear().get(), &out), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_store_intern_gradient(s.store, nullptr, &out), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_store_intern_gradient(s.store, gen_linear().get(), nullptr), GSHSCENE_ERR_INVALID);
  {
    Desc d = gen_linear();
    d.get();
    d.g.stops = nullptr;
    EXPECT_EQ(gshscene_store_intern_gradient(s.store, &d.g, &out), GSHSCENE_ERR_INVALID) << "stops without a pointer";
  }
  const GSHSCENE_Result INV = GSHSCENE_ERR_INVALID;
  auto bad = [&](std::function<void(Desc &)> change, GSHSCENE_Result want, const char * what, Desc (*make)() = gen_linear) {
    Desc d = make();
    change(d);
    EXPECT_EQ(try_intern(s.store, d), want) << what;
  };
  bad([](Desc & d) { d.g.kind = 0; }, INV, "kind 0");
  bad([](Desc & d) { d.g.kind = GSHSCENE_GRADIENT_KIND_COUNT; }, INV, "a kind past the last");
  bad([](Desc & d) { d.g.extend = GSHSCENE_EXTEND_COUNT; }, INV, "an extend past the last");
  bad([](Desc & d) { d.g.premultiplied = 2; }, INV, "a flag of 2");
  // Every double of the kind finite.
  for (int i = 0; i < 6; i++) {
    for (double v : {kNaN, kInf, -kInf}) {
      bad([&](Desc & d) { double * f[6] = {&d.g.p0[0], &d.g.p0[1], &d.g.p1[0], &d.g.p1[1], &d.g.d[0], &d.g.d[1]}; *f[i] = v; }, INV, "linear non-finite");
      bad([&](Desc & d) { double * f[6] = {&d.g.c0[0], &d.g.c0[1], &d.g.r0, &d.g.c1[0], &d.g.c1[1], &d.g.r1}; *f[i] = v; }, INV, "radial non-finite", gen_radial);
      if (i < 4) {
        bad([&](Desc & d) { double * f[4] = {&d.g.center[0], &d.g.center[1], &d.g.a0, &d.g.a1}; *f[i] = v; }, INV, "sweep non-finite", gen_sweep);
      }
    }
  }
  // Another kind's field nonzero (one spelling); -0 counts as a field set.
  for (int i = 0; i < 6; i++) {
    bad([&](Desc & d) { double * f[6] = {&d.g.c0[0], &d.g.c0[1], &d.g.r0, &d.g.c1[0], &d.g.c1[1], &d.g.r1}; *f[i] = -0.0; }, INV, "a radial field on a linear");
    bad([&](Desc & d) { double * f[6] = {&d.g.p0[0], &d.g.p0[1], &d.g.p1[0], &d.g.p1[1], &d.g.d[0], &d.g.d[1]}; *f[i] = 1; }, INV, "a linear field on a radial", gen_radial);
    bad([&](Desc & d) { double * f[6] = {&d.g.p0[0], &d.g.p0[1], &d.g.p1[0], &d.g.p1[1], &d.g.d[0], &d.g.d[1]}; *f[i] = 1; }, INV, "a linear field on a sweep", gen_sweep);
    bad([&](Desc & d) { double * f[6] = {&d.g.c0[0], &d.g.c0[1], &d.g.r0, &d.g.c1[0], &d.g.c1[1], &d.g.r1}; *f[i] = 1; }, INV, "a radial field on a sweep", gen_sweep);
  }
  for (int i = 0; i < 4; i++) {
    bad([&](Desc & d) { double * f[4] = {&d.g.center[0], &d.g.center[1], &d.g.a0, &d.g.a1}; *f[i] = 1; }, INV, "a sweep field on a linear");
    bad([&](Desc & d) { double * f[4] = {&d.g.center[0], &d.g.center[1], &d.g.a0, &d.g.a1}; *f[i] = 1; }, INV, "a sweep field on a radial", gen_radial);
  }
  bad([](Desc & d) { d.g.angle_unit = GSHGEOM_ANGLE_DEGREES; }, INV, "a unit on a linear");
  bad([](Desc & d) { d.g.angle_unit = GSHGEOM_ANGLE_DEGREES; }, INV, "a unit on a radial", gen_radial);
  bad([](Desc & d) { d.g.angle_unit = GSHGEOM_ANGLE_UNIT_COUNT; }, INV, "a unit past the last", gen_sweep);
  EXPECT_EQ(try_intern(s.store, [] { Desc d = gen_sweep(); d.g.angle_unit = GSHGEOM_ANGLE_TURNS; return d; }()), GSHSCENE_OK);
  // The map: valid and 2D.
  bad([](Desc & d) { std::memset(&d.g.map, 0, sizeof d.g.map); }, INV, "a zero-filled map");
  bad([](Desc & d) { gshgeom_map_identity(3, &d.g.map); }, INV, "a 3D map");
  bad([](Desc & d) { d.g.map.m[0][1] = kNaN; }, INV, "a map with a NaN");
  bad([](Desc & d) { d.g.map.kind = 1u << 12; }, INV, "a map with an undefined kind bit");
  // A singular map is legal (G11 is decided when drawn).
  EXPECT_EQ(try_intern(s.store, [] { Desc d = gen_linear(); d.g.map = matrix(0, 0, 0, 1); return d; }()), GSHSCENE_OK);
  // The space.
  bad([](Desc & d) { d.g.space = space_of(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_UNKNOWN); }, INV, "a partial space");
  bad([](Desc & d) { d.g.space = space_of(GCOL_PRIMARIES_PROPHOTO, GCOL_TRANSFER_LINEAR); }, GSHSCENE_ERR_UNSUPPORTED, "ProPhoto");
  // The stops.
  bad([](Desc & d) { d.stops[1].offset = kNaN; }, INV, "a NaN offset");
  bad([](Desc & d) { d.stops[1].offset = kInf; }, INV, "an infinite offset");
  bad([](Desc & d) { d.stops[1].color.kind = GSHSCENE_PAINT_GRADIENT; }, INV, "a gradient stop");
  {
    // A stop is never a gradient, not even one of this store that a fill could paint with.
    Ref other(intern_g(s.store, gen_radial()));
    Desc d = gen_linear();
    d.stops[1].color = gradient_paint(other.get(), 1.0);
    EXPECT_EQ(try_intern(s.store, d), INV) << "a stop naming a gradient";
  }
  bad([](Desc & d) { d.stops[1].color.kind = (GSHSCENE_Paint_Kind)9; }, INV, "a stop of no kind");
  bad([](Desc & d) { d.stops[1].color.rgba[0] = 0.5; }, INV, "a foreground stop's color is the foreground's");
  bad([](Desc & d) { d.stops[2].color.entry = 0xFFFFu; }, INV, "0xFFFF is never an entry");
  bad([](Desc & d) { d.stops[0].color.rgba[3] = 1.5; }, INV, "an alpha above 1");
  bad([](Desc & d) { d.stops[0].color.space = GSHSCENE_Color_Space{}; }, INV, "a target-space solid in a stated space");
  bad([](Desc & d) { d.stops[0].color.space = space_of(GCOL_PRIMARIES_PROPHOTO, GCOL_TRANSFER_LINEAR); },
      GSHSCENE_ERR_UNSUPPORTED, "a stop in ProPhoto");
  {
    Desc d = gen_linear();
    d.stops[0].color.gradient = (const GSHSCENE_Resource *)&d;
    EXPECT_EQ(try_intern(s.store, d), INV) << "a solid stop naming a resource";
  }
  // A target-space solid stop is fine in a target-space gradient (G3, Q8).
  EXPECT_EQ(try_intern(s.store, gen_radial()), GSHSCENE_OK);
  // Degenerate gradients are legal (G10): never refused at record.
  EXPECT_EQ(try_intern(s.store, linear(2, 3, 2, 3)), GSHSCENE_OK);
  EXPECT_EQ(try_intern(s.store, linear(0, 0, 2, 1, 4, 2)), GSHSCENE_OK);
  EXPECT_EQ(try_intern(s.store, radial(1, 1, 2, 1, 1, 2)), GSHSCENE_OK);
  EXPECT_EQ(try_intern(s.store, sweep(0, 0, 1, 90, 90)), GSHSCENE_OK);
  // No stops and one stop are legal (G4).
  EXPECT_EQ(try_intern(s.store, linear(0, 0, 1, 0)), GSHSCENE_OK);
  GSHSCENE_Store_Stats st;
  ASSERT_EQ(gshscene_store_stats(s.store, &st), GSHSCENE_OK);
  EXPECT_EQ(st.live, 0u) << "a refused intern kept nothing, and the legal ones were released";
}

TEST(GradientResource, TheStopCountIsLimitedAndTheBytesAreAccounted) {
  GSHSCENE_Limits limits;
  gshscene_limits_default(&limits);
  limits.max_resource_elements = 5;
  StoreHolder s(&limits);
  Desc d = linear(0, 0, 1, 0);
  for (int i = 0; i < 5; i++) {
    d.stop((double)i, solid(0, 0, 0, 1));
  }
  EXPECT_EQ(try_intern(s.store, d), GSHSCENE_OK) << "the bound passes";
  d.stop(9, solid(0, 0, 0, 1));
  EXPECT_EQ(try_intern(s.store, d), GSHSCENE_ERR_LIMIT) << "one more is refused";
  limits.max_resource_elements = 0;
  limits.max_bytes = GSHSCENE_BYTES_BASE + 2 * GSHSCENE_BYTES_PER_STOP;
  StoreHolder small(&limits);
  Desc two = linear(0, 0, 1, 0);
  two.stop(0, solid(0, 0, 0, 1)).stop(1, solid(1, 1, 1, 1));
  Ref g(intern_g(small.store, two));
  ASSERT_NE(g.get(), nullptr);
  Desc three = two;
  three.stop(2, solid(1, 0, 0, 1));
  EXPECT_EQ(try_intern(small.store, three), GSHSCENE_ERR_LIMIT) << "the store's bytes";
}

// G14: every geometry double, map entry and offset is zero or of magnitude in [2^-64, 2^64]; each bound interns and
// one step past it is ERR_LIMIT, in either sign, and a non-finite value is still ERR_INVALID.
TEST(GradientResource, G14TheValuesAreLimitedAtEachBoundAndOneStepPast) {
  StoreHolder s;
  const double big = GSHSCENE_GRADIENT_VALUE_MAX;
  const double small = GSHSCENE_GRADIENT_VALUE_MIN;
  const double past_big = std::nextafter(big, INFINITY);
  const double past_small = std::nextafter(small, 0.0);
  // Each geometry field of each kind, set alone (the others a legal gradient's).
  struct Field {
    Desc base;
    double * (*at)(Desc &);
  };
  std::vector<Field> fields = {
      {linear(0, 0, 1, 0), [](Desc & d) { return &d.g.p0[0]; }},
      {linear(0, 0, 1, 0), [](Desc & d) { return &d.g.p0[1]; }},
      {linear(0, 0, 1, 0), [](Desc & d) { return &d.g.p1[0]; }},
      {linear(0, 0, 1, 0), [](Desc & d) { return &d.g.p1[1]; }},
      {linear(0, 0, 1, 0), [](Desc & d) { return &d.g.d[0]; }},
      {linear(0, 0, 1, 0), [](Desc & d) { return &d.g.d[1]; }},
      {radial(0, 0, 1, 0, 0, 2), [](Desc & d) { return &d.g.c0[0]; }},
      {radial(0, 0, 1, 0, 0, 2), [](Desc & d) { return &d.g.c0[1]; }},
      {radial(0, 0, 1, 0, 0, 2), [](Desc & d) { return &d.g.r0; }},
      {radial(0, 0, 1, 0, 0, 2), [](Desc & d) { return &d.g.c1[0]; }},
      {radial(0, 0, 1, 0, 0, 2), [](Desc & d) { return &d.g.c1[1]; }},
      {radial(0, 0, 1, 0, 0, 2), [](Desc & d) { return &d.g.r1; }},
      {sweep(0, 0, GSHGEOM_ANGLE_DEGREES, 0, 90), [](Desc & d) { return &d.g.center[0]; }},
      {sweep(0, 0, GSHGEOM_ANGLE_DEGREES, 0, 90), [](Desc & d) { return &d.g.center[1]; }},
      {sweep(0, 0, GSHGEOM_ANGLE_DEGREES, 0, 90), [](Desc & d) { return &d.g.a0; }},
      {sweep(0, 0, GSHGEOM_ANGLE_DEGREES, 0, 90), [](Desc & d) { return &d.g.a1; }},
  };
  for (size_t i = 0; i < fields.size(); i++) {
    for (double sign : {1.0, -1.0}) {
      for (double v : {big, small, 0.0}) {
        Desc d = fields[i].base;
        d.stop(0, solid(0, 0, 0, 1)).stop(1, solid(1, 1, 1, 1));
        *fields[i].at(d) = sign * v;
        EXPECT_EQ(try_intern(s.store, d), GSHSCENE_OK) << "field " << i << " at " << sign * v;
      }
      for (double v : {past_big, past_small, 0x1p-1074}) {
        Desc d = fields[i].base;
        d.stop(0, solid(0, 0, 0, 1)).stop(1, solid(1, 1, 1, 1));
        *fields[i].at(d) = sign * v;
        EXPECT_EQ(try_intern(s.store, d), GSHSCENE_ERR_LIMIT) << "field " << i << " at " << sign * v;
      }
    }
    Desc d = fields[i].base;
    *fields[i].at(d) = kNaN;
    d.g.p0[1] = d.g.kind == GSHSCENE_GRADIENT_LINEAR && fields[i].at(d) != &d.g.p0[1] ? past_big : d.g.p0[1];
    EXPECT_EQ(try_intern(s.store, d), GSHSCENE_ERR_INVALID) << "field " << i << ": a NaN is invalid, decided first";
  }
  // A value past a bound in a gradient that is invalid otherwise (a bad extend, read after the geometry) is invalid.
  {
    Desc d = linear(past_big, 0, 1, 0);
    d.g.extend = 7;
    EXPECT_EQ(try_intern(s.store, d), GSHSCENE_ERR_INVALID) << "invalid is decided before G14";
  }
  // Each entry of the gradient's map: the linear part and the translation.
  for (int e = 0; e < 6; e++) {
    for (double v : {big, -big, small, -small, past_big, -past_small}) {
      double m[6] = {1, 0, 0, 1, 0, 0};
      m[e] = v;
      Desc d = linear(0, 0, 1, 0);
      d.stop(0, solid(0, 0, 0, 1)).stop(1, solid(1, 1, 1, 1));
      d.g.map = matrix(m[0], m[1], m[2], m[3], m[4], m[5]);
      bool ok = v == big || v == -big || v == small || v == -small;
      EXPECT_EQ(try_intern(s.store, d), ok ? GSHSCENE_OK : GSHSCENE_ERR_LIMIT) << "map entry " << e << " at " << v;
    }
  }
  // Each offset, wherever it sorts.
  for (double v : {big, -big, small, -small, past_big, -past_big, past_small, -past_small}) {
    for (int where = 0; where < 3; where++) {
      Desc d = linear(0, 0, 1, 0);
      d.stop(where == 0 ? v : 0.5, solid(0, 0, 0, 1)).stop(where == 1 ? v : 0.5, solid(1, 0, 0, 1));
      d.stop(where == 2 ? v : 0.5, solid(1, 1, 1, 1));
      bool ok = v == big || v == -big || v == small || v == -small;
      EXPECT_EQ(try_intern(s.store, d), ok ? GSHSCENE_OK : GSHSCENE_ERR_LIMIT) << "offset " << where << " at " << v;
    }
  }
  GSHSCENE_Store_Stats st;
  ASSERT_EQ(gshscene_store_stats(s.store, &st), GSHSCENE_OK);
  EXPECT_EQ(st.live, 0u) << "a refused intern keeps nothing";
}

// G14: at most 65535 stops whatever the store allows; the bound interns and one more is ERR_LIMIT.
TEST(GradientResource, G14TheStopsNumberAtMost65535) {
  StoreHolder s;
  Desc d = linear(0, 0, 1, 0);
  for (size_t i = 0; i < GSHSCENE_GRADIENT_STOPS_MAX; i++) {
    d.stop((double)(i % 7), solid(0, 0, 0, 1));
  }
  const GSHSCENE_Resource * r = nullptr;
  ASSERT_EQ(gshscene_store_intern_gradient(s.store, d.get(), &r), GSHSCENE_OK) << "the bound passes";
  GSHSCENE_Gradient back;
  ASSERT_EQ(gshscene_resource_gradient(r, &back), GSHSCENE_OK);
  EXPECT_EQ(back.stop_count, 65535u);
  gshscene_resource_release(r);
  d.stop(3, solid(0, 0, 0, 1));
  EXPECT_EQ(try_intern(s.store, d), GSHSCENE_ERR_LIMIT) << "one more is refused";
}

// --- hash and dump against the independent generator ---------------------------------------------------------------

TEST(GradientDump, TheHashAndDumpAreWhatTheGeneratorWrites) {
  StoreHolder s;
  Ref l(intern_g(s.store, gen_linear()));
  Ref r(intern_g(s.store, gen_radial()));
  Ref w(intern_g(s.store, gen_sweep()));
  EXPECT_EQ(gshscene_resource_hash(l.get()), known::kGradientLinearHash);
  EXPECT_EQ(gshscene_resource_hash(r.get()), known::kGradientRadialHash);
  EXPECT_EQ(gshscene_resource_hash(w.get()), known::kGradientSweepHash);
  EXPECT_EQ(dump_resource(l.get()), known::kGradientLinearDump);
  EXPECT_EQ(dump_resource(r.get()), known::kGradientRadialDump);
  EXPECT_EQ(dump_resource(w.get()), known::kGradientSweepDump);
  // A list of their fills.
  Ref sq(intern_square(s.store));
  Ref p2(intern_fixture(s.store));
  ListHolder list(s.store);
  GSHGEOM_Map t = translate(1.5, -2.25);
  GSHGEOM_Map dm = design_map();
  GSHSCENE_Paint pl = gradient_paint(l.get(), 1.0);
  GSHSCENE_Paint pr = gradient_paint(r.get(), 0.5);
  GSHSCENE_Paint pw = gradient_paint(w.get(), 0.25);
  GSHSCENE_Paint pl0 = gradient_paint(l.get(), 0.0);
  ASSERT_EQ(gshscene_list_fill(list.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &pl, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(list.list, p2.get(), &t, GSHGEOM_FILL_EVEN_ODD, &pr, 2, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(list.list, sq.get(), &dm, GSHGEOM_FILL_NONZERO, &pw, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(list.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &pl0, 0, 1.0), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_hash(list.list), known::kGradientListHash);
  EXPECT_EQ(dump_list(list.list), known::kGradientListDump);
  // A scene with a gradient shape.
  SceneHolder sc(nullptr, nullptr, s.store);
  EditHolder e(sc);
  GSHSCENE_Node n = shape_node(sq.get());
  n.paint = gradient_paint(r.get(), 0.5);
  add(e.edit, GSHSCENE_ROOT_ID, n);
  VersionRef v;
  publish(sc, e, &v);
  EXPECT_EQ(gshscene_version_hash(v.get()), known::kGradientSceneHash);
  EXPECT_EQ(dump_version(v.get()), known::kGradientSceneDump);
}

// --- the paint in a list and in a scene ------------------------------------------------------------------------------

TEST(GradientPaint, AListHoldsItsGradientAndReleasesItOnEveryPath) {
  StoreHolder s;
  Ref sq(intern_square(s.store));
  Ref g(intern_g(s.store, gen_radial()));
  size_t base = gshscene_resource_refcount(g.get());
  GSHSCENE_Paint p = gradient_paint(g.get(), 0.5);
  {
    ListHolder l(s.store);
    ASSERT_EQ(gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK);
    EXPECT_EQ(gshscene_resource_refcount(g.get()), base + 1);
    const GSHSCENE_Item * item = nullptr;
    ASSERT_EQ(gshscene_list_item(l.list, 0, &item), GSHSCENE_OK);
    EXPECT_EQ(item->paint.kind, GSHSCENE_PAINT_GRADIENT);
    EXPECT_EQ(item->paint.gradient, g.get());
    EXPECT_EQ(item->paint.rgba[3], 0.5);
    ASSERT_EQ(gshscene_list_truncate(l.list, 0), GSHSCENE_OK);
    EXPECT_EQ(gshscene_resource_refcount(g.get()), base) << "truncate releases it";
    ASSERT_EQ(gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_reset(l.list), GSHSCENE_OK);
    EXPECT_EQ(gshscene_resource_refcount(g.get()), base) << "reset releases it";
    ASSERT_EQ(gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
    // A picture of it holds its own reference.
    const GSHSCENE_Resource * pic = nullptr;
    ASSERT_EQ(gshscene_store_intern_picture(s.store, l.list, &pic), GSHSCENE_OK);
    EXPECT_EQ(gshscene_resource_refcount(g.get()), base + 2);
    gshscene_resource_release(pic);
  }
  EXPECT_EQ(gshscene_resource_refcount(g.get()), base) << "destroy releases it";
}

TEST(GradientPaint, ThePaintsRulesAreAFillsAndTheGradientMustBeOfTheStore) {
  StoreHolder s;
  StoreHolder other;
  Ref sq(intern_square(s.store));
  Ref g(intern_g(s.store, gen_radial()));
  Ref og(intern_g(other.store, gen_radial()));
  ListHolder l(s.store);
  auto fill = [&](GSHSCENE_Paint p) { return gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0); };
  EXPECT_EQ(fill(gradient_paint(g.get(), 1.0)), GSHSCENE_OK);
  EXPECT_EQ(fill(gradient_paint(nullptr, 1.0)), GSHSCENE_ERR_INVALID) << "no gradient";
  EXPECT_EQ(fill(gradient_paint(og.get(), 1.0)), GSHSCENE_ERR_INVALID) << "another store's";
  EXPECT_EQ(fill(gradient_paint(sq.get(), 1.0)), GSHSCENE_ERR_INVALID) << "a path is no gradient";
  GSHSCENE_Paint p = gradient_paint(g.get(), 1.0);
  p.rgba[0] = 0.5;
  EXPECT_EQ(fill(p), GSHSCENE_ERR_INVALID) << "only the alpha is the paint's";
  p = gradient_paint(g.get(), 1.0);
  p.space = srgb();
  EXPECT_EQ(fill(p), GSHSCENE_ERR_INVALID) << "the space is the gradient's";
  p = gradient_paint(g.get(), 1.0);
  p.entry = 1;
  EXPECT_EQ(fill(p), GSHSCENE_ERR_INVALID) << "no entry";
  EXPECT_EQ(fill(gradient_paint(g.get(), 1.5)), GSHSCENE_ERR_INVALID) << "alpha above 1";
  EXPECT_EQ(fill(gradient_paint(g.get(), -0.5)), GSHSCENE_ERR_INVALID) << "alpha below 0";
  EXPECT_EQ(fill(gradient_paint(g.get(), kNaN)), GSHSCENE_ERR_INVALID) << "a NaN alpha";
  GSHSCENE_Paint solid_named = solid(0, 0, 0, 1);
  solid_named.gradient = g.get();
  EXPECT_EQ(fill(solid_named), GSHSCENE_ERR_INVALID) << "a solid paint names no gradient";
  EXPECT_EQ(gshscene_list_count(l.list), 1u);
}

TEST(GradientPaint, EqualityAndTheHashSeeTheGradient) {
  StoreHolder s;
  Ref sq(intern_square(s.store));
  Ref a(intern_g(s.store, gen_radial()));
  Ref b(intern_g(s.store, gen_sweep()));
  auto make = [&](const GSHSCENE_Resource * g, double alpha) {
    GSHSCENE_List * l = nullptr;
    gshscene_list_create(nullptr, nullptr, s.store, &l);
    GSHSCENE_Paint p = gradient_paint(g, alpha);
    gshscene_list_fill(l, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0);
    gshscene_list_close(l);
    return l;
  };
  GSHSCENE_List * la = make(a.get(), 1.0);
  GSHSCENE_List * la2 = make(a.get(), 1.0);
  GSHSCENE_List * lb = make(b.get(), 1.0);
  GSHSCENE_List * la5 = make(a.get(), 0.5);
  EXPECT_TRUE(gshscene_list_equal(la, la2));
  EXPECT_FALSE(gshscene_list_equal(la, lb)) << "another gradient";
  EXPECT_FALSE(gshscene_list_equal(la, la5)) << "another alpha";
  EXPECT_EQ(gshscene_list_hash(la), gshscene_list_hash(la2));
  EXPECT_NE(gshscene_list_hash(la), gshscene_list_hash(lb));
  EXPECT_NE(gshscene_list_hash(la), gshscene_list_hash(la5));
  // Two stores given the same content print the same list hash: the gradient's content hash, not its handle.
  StoreHolder s2;
  Ref sq2(intern_square(s2.store));
  Ref a2(intern_g(s2.store, gen_radial()));
  GSHSCENE_List * other = nullptr;
  gshscene_list_create(nullptr, nullptr, s2.store, &other);
  GSHSCENE_Paint p = gradient_paint(a2.get(), 1.0);
  gshscene_list_fill(other, sq2.get(), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0);
  gshscene_list_close(other);
  EXPECT_EQ(gshscene_list_hash(other), gshscene_list_hash(la));
  for (GSHSCENE_List * l : {la, la2, lb, la5, other}) {
    gshscene_list_destroy(l);
  }
}

TEST(GradientScene, AShapeHoldsItsGradientAndAParameterChangeDamagesItsShapes) {
  SceneHolder sc;
  GSHSCENE_Store * store = sc.store();
  Ref sq(intern_square(store, 0.0));
  Ref sq2(intern_square(store, 40.0));
  Ref sq3(intern_square(store, 80.0));
  // A gradient with a foreground stop, one with a palette stop, and one of solids only.
  Desc fgd = linear(0, 0, 1, 0);
  fgd.stop(0, fg_paint(1.0)).stop(1, solid(1, 1, 1, 1));
  Desc pald = linear(0, 0, 1, 0);
  pald.stop(0, entry_paint(1, 1.0)).stop(1, solid(1, 1, 1, 1));
  Ref gfg(intern_g(store, fgd));
  Ref gpal(intern_g(store, pald));
  Ref gsol(intern_g(store, gen_radial()));
  size_t before = gshscene_resource_refcount(gfg.get());
  VersionRef v1;
  {
    EditHolder e(sc);
    GSHSCENE_Node a = shape_node(sq.get());
    a.paint = gradient_paint(gfg.get(), 1.0);
    GSHSCENE_Node b = shape_node(sq2.get());
    b.paint = gradient_paint(gpal.get(), 1.0);
    GSHSCENE_Node c = shape_node(sq3.get());
    c.paint = gradient_paint(gsol.get(), 1.0);
    add(e.edit, GSHSCENE_ROOT_ID, a);
    add(e.edit, GSHSCENE_ROOT_ID, b);
    add(e.edit, GSHSCENE_ROOT_ID, c);
    publish(sc, e, &v1);
  }
  EXPECT_GT(gshscene_resource_refcount(gfg.get()), before) << "the node holds its gradient";
  const GSHSCENE_List * list = nullptr;
  ASSERT_EQ(gshscene_version_list(v1.get(), &list), GSHSCENE_OK);
  const GSHSCENE_Item * item = nullptr;
  ASSERT_EQ(gshscene_list_item(list, 0, &item), GSHSCENE_OK);
  EXPECT_EQ(item->paint.gradient, gfg.get()) << "the version's list carries the gradient";
  auto damage = [&](const GSHSCENE_Version * a, const GSHSCENE_Version * b) {
    GSHSCENE_Diff * diff = nullptr;
    GSHSCENE_Damage d{};
    EXPECT_EQ(gshscene_diff(a, b, nullptr, nullptr, &diff), GSHSCENE_OK);
    EXPECT_EQ(gshscene_diff_damage(diff, &d), GSHSCENE_OK);
    gshscene_diff_destroy(diff);
    return d;
  };
  VersionRef v2;
  {
    EditHolder e(sc);
    GSHSCENE_Color red = {{1, 0, 0, 1}, {}};
    ASSERT_EQ(gshscene_edit_set_foreground(e.edit, &red), GSHSCENE_OK);
    publish(sc, e, &v2);
  }
  GSHSCENE_Damage fg = damage(v1.get(), v2.get());
  EXPECT_EQ(fg.empty, 0);
  EXPECT_LT(fg.rect.x1, 40) << "only the gradient with a foreground stop";
  const GSHSCENE_Color entries[2] = {{{0, 0, 0, 1}, {}}, {{0, 1, 0, 1}, {}}};
  const GSHSCENE_Resource * pal = nullptr;
  ASSERT_EQ(gshscene_store_intern_palette(store, 2, entries, &pal), GSHSCENE_OK);
  VersionRef v3;
  {
    EditHolder e(sc);
    ASSERT_EQ(gshscene_edit_set_palette(e.edit, pal), GSHSCENE_OK);
    publish(sc, e, &v3);
  }
  gshscene_resource_release(pal);
  GSHSCENE_Damage pd = damage(v2.get(), v3.get());
  EXPECT_EQ(pd.empty, 0);
  EXPECT_GE(pd.rect.x0, 30) << "only the gradient with a palette stop";
  EXPECT_LT(pd.rect.x1, 80);
}

// --- G3: the stops at draw time ------------------------------------------------------------------------------------

TEST(GradientResolve, EveryStopIsResolvedAndConvertedIntoTheInterpolationSpace) {
  StoreHolder s;
  const GSHSCENE_Color entries[3] = {{{0.25, 0.5, 0.75, 0.5}, {}}, {{1, 0, 0, 1}, {}}, {{0.5, 0.5, 0.5, 1.0}, srgb_space()}};
  const GSHSCENE_Resource * pal = nullptr;
  ASSERT_EQ(gshscene_store_intern_palette(s.store, 3, entries, &pal), GSHSCENE_OK);
  Ref palette(pal);
  Ref g(intern_g(s.store, gen_linear()));
  GSHSCENE_Color fg = {{0.5, 0.25, 0.125, 0.5}, linear_srgb_space()};
  GSHSCENE_Color_Space target = srgb_space();
  GSHSCENE_Resolved_Stop out[4];
  GSHSCENE_Color_Space into{};
  ASSERT_EQ(gshscene_gradient_resolve(g.get(), &fg, palette.get(), &target, &into, out, 4), GSHSCENE_OK);
  EXPECT_TRUE(into.primaries == GCOL_PRIMARIES_SRGB && into.transfer == GCOL_TRANSFER_LINEAR);
  // 0: palette entry 2 (sRGB 0.5) alpha 1 * 0.75, into linear sRGB.
  EXPECT_EQ(out[0].offset, 0.0);
  EXPECT_NEAR(out[0].rgba[0], 0.21404114048223255, 1e-15);
  EXPECT_EQ(out[0].rgba[3], 0.75);
  // 1: the foreground, already in linear sRGB: copied exactly, its alpha scaled.
  EXPECT_EQ(out[1].offset, 0.5);
  EXPECT_EQ(out[1].rgba[0], 0.5);
  EXPECT_EQ(out[1].rgba[1], 0.25);
  EXPECT_EQ(out[1].rgba[3], 0.25);
  // 2: P3 gamma 2.2 green into linear sRGB: the matrices and both transfers.
  EXPECT_LT(out[2].rgba[0], 0.0) << "P3's green is outside sRGB";
  EXPECT_EQ(out[2].rgba[3], 0.25) << "alpha is never converted";
  // 3: sRGB orange into linear sRGB: only the transfer.
  EXPECT_EQ(out[3].rgba[0], 1.0);
  EXPECT_NEAR(out[3].rgba[1], 0.21404114048223255, 1e-15);
  // A target-space gradient with target-space stops: copied exactly into the target's space.
  Ref r(intern_g(s.store, gen_radial()));
  GSHSCENE_Resolved_Stop two[2];
  ASSERT_EQ(gshscene_gradient_resolve(r.get(), &fg, nullptr, &target, &into, two, 2), GSHSCENE_OK);
  EXPECT_TRUE(into.primaries == GCOL_PRIMARIES_SRGB && into.transfer == GCOL_TRANSFER_SRGB) << "the target's";
  EXPECT_EQ(two[1].rgba[0], 1.0);
  EXPECT_EQ(two[1].rgba[3], 0.0);
  // A target-space foreground stop in a stated gradient is taken in the target's space.
  Desc d = linear(0, 0, 1, 0);
  d.g.space = linear_srgb_space();
  d.stop(0, fg_paint(1.0));
  Ref fgg(intern_g(s.store, d));
  GSHSCENE_Color black{};
  gshscene_color_init(&black);
  black.rgba[0] = 0.5;
  ASSERT_EQ(gshscene_gradient_resolve(fgg.get(), &black, nullptr, &target, nullptr, two, 1), GSHSCENE_OK);
  EXPECT_NEAR(two[0].rgba[0], 0.21404114048223255, 1e-15) << "decoded from the target's sRGB";
  // Nothing to write is fine; a short room, a missing entry and bad arguments are not.
  Ref empty(intern_g(s.store, linear(0, 0, 1, 0)));
  EXPECT_EQ(gshscene_gradient_resolve(empty.get(), &fg, nullptr, &target, nullptr, nullptr, 0), GSHSCENE_OK);
  EXPECT_EQ(gshscene_gradient_resolve(g.get(), &fg, palette.get(), &target, nullptr, out, 3), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_gradient_resolve(g.get(), &fg, palette.get(), &target, nullptr, nullptr, 4), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_gradient_resolve(g.get(), &fg, nullptr, &target, nullptr, out, 4), GSHSCENE_ERR_REFUSED);
  {
    Desc e0 = linear(0, 0, 1, 0);
    e0.stop(0, entry_paint(0, 1.0));
    Ref g0(intern_g(s.store, e0));
    EXPECT_EQ(gshscene_gradient_resolve(g0.get(), &fg, nullptr, &target, nullptr, out, 4), GSHSCENE_ERR_REFUSED)
        << "entry 0 with no palette is missing";
    EXPECT_EQ(gshscene_gradient_resolve(g0.get(), &fg, palette.get(), &target, nullptr, out, 4), GSHSCENE_OK);
  }
  const GSHSCENE_Resource * short_pal = nullptr;
  ASSERT_EQ(gshscene_store_intern_palette(s.store, 2, entries, &short_pal), GSHSCENE_OK);
  Ref sp(short_pal);
  EXPECT_EQ(gshscene_gradient_resolve(g.get(), &fg, sp.get(), &target, nullptr, out, 4), GSHSCENE_ERR_REFUSED)
      << "entry 2 of two";
  GSHSCENE_Color_Space zero{};
  EXPECT_EQ(gshscene_gradient_resolve(g.get(), &fg, palette.get(), &zero, nullptr, out, 4), GSHSCENE_ERR_INVALID);
  GSHSCENE_Color_Space partial = space_of(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_UNKNOWN);
  EXPECT_EQ(gshscene_gradient_resolve(g.get(), &fg, palette.get(), &partial, nullptr, out, 4), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_gradient_resolve(g.get(), &fg, palette.get(), nullptr, nullptr, out, 4), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_gradient_resolve(g.get(), nullptr, palette.get(), &target, nullptr, out, 4), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_gradient_resolve(nullptr, &fg, palette.get(), &target, nullptr, out, 4), GSHSCENE_ERR_INVALID);
  Ref sq(intern_square(s.store));
  EXPECT_EQ(gshscene_gradient_resolve(sq.get(), &fg, palette.get(), &target, nullptr, out, 4), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_gradient_resolve(g.get(), &fg, sq.get(), &target, nullptr, out, 4), GSHSCENE_ERR_INVALID);
}

TEST(GradientResolve, TheConversionIsWithinItsStatedBoundOfTheOracle) {
  StoreHolder s;
  double worst = 0.0;
  int exact_mismatch = 0;
  for (int i = 0; i < gradient_vectors::kResolveCaseCount; i++) {
    const gradient_vectors::VResolve & c = gradient_vectors::kResolveCases[i];
    GSHSCENE_Color_Space from = space_of((GCOL_Primaries)c.from[0], (GCOL_Transfer)c.from[1], c.from_gamma);
    GSHSCENE_Color_Space to = space_of((GCOL_Primaries)c.to[0], (GCOL_Transfer)c.to[1], c.to_gamma);
    Desc d = linear(0, 0, 1, 0);
    d.g.space = to;
    d.stop(0, spaced(c.rgb[0], c.rgb[1], c.rgb[2], 0.5, from));
    Ref g(intern_g(s.store, d));
    GSHSCENE_Color fg{};
    gshscene_color_init(&fg);
    GSHSCENE_Color_Space target = srgb_space();
    GSHSCENE_Resolved_Stop out[1];
    ASSERT_EQ(gshscene_gradient_resolve(g.get(), &fg, nullptr, &target, nullptr, out, 1), GSHSCENE_OK);
    EXPECT_EQ(out[0].rgba[3], 0.5);
    for (int k = 0; k < 3; k++) {
      double err = std::fabs(out[0].rgba[k] - c.want[k]) / std::max(1.0, std::fabs(c.want[k]));
      worst = std::max(worst, err);
      EXPECT_LE(err, GSHSCENE_RESOLVE_TOLERANCE) << "case " << i << " component " << k;
      bool same = c.from[0] == c.to[0] && c.from[1] == c.to[1] && c.from_gamma == c.to_gamma;
      if ((c.exact && same) && out[0].rgba[k] != c.want[k]) {
        exact_mismatch++;
      }
    }
  }
  EXPECT_EQ(exact_mismatch, 0) << "a stop already in the interpolation space is copied bit for bit";
  std::printf("resolve: %d conversions, worst relative error %.3g (bound %.0e)\n", gradient_vectors::kResolveCaseCount,
      worst, GSHSCENE_RESOLVE_TOLERANCE);
}

// --- G10, G11: what draws nothing ------------------------------------------------------------------------------------

namespace {

GSHSCENE_Item fill_item(const GSHSCENE_Resource * g, GSHGEOM_Map map) {
  GSHSCENE_Item item{};
  item.kind = GSHSCENE_ITEM_FILL;
  item.map = map;
  item.paint = gradient_paint(g, 1.0);
  return item;
}

}  // namespace

TEST(GradientNotice, EachDegenerateCaseAndEachSingularMapIsNamed) {
  StoreHolder s;
  auto notice = [&](Desc d, GSHGEOM_Map item_map = identity(), std::vector<GSHGEOM_Map> instances = {},
                    const GSHGEOM_Map * target = nullptr) {
    Ref g(intern_g(s.store, d));
    GSHSCENE_Item item = fill_item(g.get(), item_map);
    return gshscene_gradient_notice(&item, instances.empty() ? nullptr : instances.data(), instances.size(), target);
  };
  const GSHSCENE_Reason NONE = (GSHSCENE_Reason)0;
  const GSHSCENE_Reason DEG = GSHSCENE_REASON_GRADIENT_DEGENERATE;
  const GSHSCENE_Reason SING = GSHSCENE_REASON_MAP_NOT_INVERTIBLE;
  Desc two = linear(0, 0, 4, 0);
  two.stop(0, solid(0, 0, 0, 1)).stop(1, solid(1, 1, 1, 1));
  EXPECT_EQ(notice(two), NONE);
  // G7: p0 = p1 (with and without d), and d parallel to p1 - p0 (exactly; nearly parallel is not).
  EXPECT_EQ(notice(linear(2, 3, 2, 3)), DEG);
  EXPECT_EQ(notice(linear(2, 3, 2, 3, 1, 0)), DEG);
  EXPECT_EQ(notice(linear(0, 0, 2, 1, 4, 2)), DEG);
  EXPECT_EQ(notice(linear(0, 0, 2, 1, -4, -2)), DEG);
  EXPECT_EQ(notice(linear(0, 0, 2, 1, 4, 2.0000000000000004)), NONE) << "an exact test, not a tolerance";
  EXPECT_EQ(notice(linear(0.1, 0.2, 0.30000000000000004, 0.5, 0.2, 0.3)), NONE) << "the doubles, not their decimals";
  EXPECT_EQ(notice(linear(0, 0, GSHSCENE_GRADIENT_VALUE_MIN, 0)), NONE) << "the shortest line G14 allows is a line";
  EXPECT_EQ(notice(linear(0, 0, 0, 1, 0, 3)), DEG) << "a vertical line with a vertical isoline";
  EXPECT_EQ(notice(linear(0, 0, 0, 4)), NONE) << "a vertical line is a line";
  EXPECT_EQ(notice(linear(0, 0, 4, 0)), NONE) << "and so is a horizontal one";
  // G8: equal circles, whatever the radius; a strip (equal radii, distinct centers) and a point circle are not.
  EXPECT_EQ(notice(radial(1, 1, 2, 1, 1, 2)), DEG);
  EXPECT_EQ(notice(radial(1, 1, 0, 1, 1, 0)), DEG);
  EXPECT_EQ(notice(radial(1, 1, -2, 1, 1, -2)), DEG);
  EXPECT_EQ(notice(radial(1, 1, 2, 2, 1, 2)), NONE);
  EXPECT_EQ(notice(radial(1, 1, 2, 1, 1, 3)), NONE);
  EXPECT_EQ(notice(radial(1, 1, 2, 1, 2, 2)), NONE);
  // G9: a0 = a1 under repeat and reflect, never under pad.
  for (uint32_t e : {GSHSCENE_EXTEND_PAD, GSHSCENE_EXTEND_REPEAT, GSHSCENE_EXTEND_REFLECT}) {
    Desc w = sweep(0, 0, GSHGEOM_ANGLE_DEGREES, 45, 45);
    w.g.extend = e;
    EXPECT_EQ(notice(w), e == GSHSCENE_EXTEND_PAD ? NONE : DEG) << "a0 = a1, extend " << e;
    Desc w2 = sweep(0, 0, GSHGEOM_ANGLE_DEGREES, 45, 46);
    w2.g.extend = e;
    EXPECT_EQ(notice(w2), NONE);
  }
  // G5: P = 0 with two or more stops under repeat and reflect; one stop never.
  for (uint32_t e : {GSHSCENE_EXTEND_PAD, GSHSCENE_EXTEND_REPEAT, GSHSCENE_EXTEND_REFLECT}) {
    Desc flat = linear(0, 0, 4, 0);
    flat.g.extend = e;
    flat.stop(0.5, solid(1, 0, 0, 1)).stop(0.5, solid(0, 0, 1, 1));
    EXPECT_EQ(notice(flat), e == GSHSCENE_EXTEND_PAD ? NONE : DEG) << "P = 0, extend " << e;
    Desc one = linear(0, 0, 4, 0);
    one.g.extend = e;
    one.stop(0.5, solid(1, 0, 0, 1));
    EXPECT_EQ(notice(one), NONE) << "one stop";
    Desc spread = linear(0, 0, 4, 0);
    spread.g.extend = e;
    spread.stop(0.5, solid(1, 0, 0, 1)).stop(0.5, solid(0, 0, 1, 1)).stop(0.5000000000000001, solid(0, 1, 0, 1));
    EXPECT_EQ(notice(spread), NONE) << "a period of one ulp is a period";
  }
  // G11: a zero determinant in each place of the chain, exactly; G11 before G10.
  GSHGEOM_Map zero = matrix(0, 0, 0, 1);
  GSHGEOM_Map rank1 = matrix(1, 2, 2, 4);
  GSHGEOM_Map tiny = matrix(0x1p-600, 0, 0, 0x1p-600);
  GSHGEOM_Map almost = matrix(1, 2, 2, 4.000000000000001);
  EXPECT_EQ(notice(two, zero), SING) << "the item's";
  EXPECT_EQ(notice(two, rank1), SING);
  EXPECT_EQ(notice(two, identity(), {identity(), rank1}), SING) << "an instance's";
  EXPECT_EQ(notice(two, identity(), {zero}), SING);
  EXPECT_EQ(notice(two, identity(), {}, &zero), SING) << "the target's";
  Desc gz = two;
  gz.g.map = rank1;
  EXPECT_EQ(notice(gz), SING) << "the gradient's";
  EXPECT_EQ(notice(two, tiny), NONE) << "a determinant below the smallest double is not zero";
  EXPECT_EQ(notice(two, identity(), {tiny, tiny}, &tiny), NONE);
  EXPECT_EQ(notice(two, almost), NONE);
  EXPECT_EQ(notice(linear(1, 1, 1, 1), zero), SING) << "not invertible first";
  GSHGEOM_Map zero_filled{};
  EXPECT_EQ(notice(two, identity(), {}, &zero_filled), NONE) << "a zero-filled target map is the identity (S4)";
  // Anything else draws as stated.
  Ref g(intern_g(s.store, linear(1, 1, 1, 1)));
  GSHSCENE_Item item = fill_item(g.get(), identity());
  item.kind = GSHSCENE_ITEM_CLIP_PUSH;
  EXPECT_EQ(gshscene_gradient_notice(&item, nullptr, 0, nullptr), NONE);
  item = fill_item(g.get(), identity());
  item.paint = solid(0, 0, 0, 1);
  EXPECT_EQ(gshscene_gradient_notice(&item, nullptr, 0, nullptr), NONE);
  item.paint.kind = GSHSCENE_PAINT_GRADIENT;
  EXPECT_EQ(gshscene_gradient_notice(&item, nullptr, 0, nullptr), NONE) << "no gradient named";
  Ref sq(intern_square(s.store));
  item.paint.gradient = sq.get();
  EXPECT_EQ(gshscene_gradient_notice(&item, nullptr, 0, nullptr), NONE) << "not a gradient";
  EXPECT_EQ(gshscene_gradient_notice(nullptr, nullptr, 0, nullptr), NONE);
}

// --- S17: the planning pass and the test sink ------------------------------------------------------------------------

namespace {

struct Render {
  GSHSCENE_Sink * sink = nullptr;
  GSHSCENE_Report * report = nullptr;
  GIMG_Raster * raster = nullptr;
  std::string dump;
  GSHSCENE_Writer log{};
  explicit Render(GSHSCENE_Dump_Sink_Config config) {
    log.write = [](void * ctx, const void * bytes, size_t count) {
      ((std::string *)ctx)->append((const char *)bytes, count);
      return GSHSCENE_OK;
    };
    log.ctx = &dump;
    config.log = &log;
    EXPECT_EQ(gshscene_dump_sink_create(nullptr, nullptr, &config, &sink), GSHSCENE_OK);
    gshscene_report_create(nullptr, nullptr, &report);
    GIMG_Pixel_Format format = GIMG_PIXEL_RGBA8;
    gimg_raster_create(64, 64, &format, GIMG_RASTER_OWNED, nullptr, 0, &raster);
  }
  ~Render() {
    gshscene_sink_destroy(sink);
    gshscene_report_destroy(report);
    gimg_raster_destroy(raster);
  }
  GSHSCENE_Result run(const GSHSCENE_List * list, uint32_t flags = 0, GSHGEOM_Map target_map = identity()) {
    dump.clear();
    GSHSCENE_Target target{};
    target.kind = GSHSCENE_TARGET_RASTER;
    target.surface = raster;
    target.map = target_map;
    GSHSCENE_Render_Request rq{};
    rq.list = list;
    rq.target = &target;
    rq.report = report;
    rq.flags = flags;
    return gshscene_sink_render(sink, &rq);
  }
  std::vector<GSHSCENE_Report_Entry> entries(GSHSCENE_Entry_Class c) {
    std::vector<GSHSCENE_Report_Entry> out;
    for (size_t i = 0; i < gshscene_report_count(report); i++) {
      GSHSCENE_Report_Entry e{};
      gshscene_report_entry(report, i, &e);
      if (e.entry_class == c) {
        out.push_back(e);
      }
    }
    return out;
  }
};

GSHSCENE_Dump_Sink_Config full_config() {
  GSHSCENE_Dump_Sink_Config c;
  gshscene_dump_sink_config_default(&c);
  return c;
}

size_t count_of(const std::string & text, const std::string & what) {
  size_t n = 0;
  for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + 1)) {
    n++;
  }
  return n;
}

}  // namespace

TEST(GradientPlan, AKindTheSinkLacksIsRefusedAndStrictLeavesTheTargetUntouched) {
  StoreHolder s;
  Ref sq(intern_square(s.store));
  Ref lin(intern_g(s.store, gen_radial()));
  Desc rd = radial(4, 4, 1, 4, 4, 8);
  rd.stop(0, solid(0, 0, 0, 1)).stop(1, solid(1, 1, 1, 1));
  Ref rad(intern_g(s.store, rd));
  ListHolder l(s.store);
  GSHSCENE_Paint p = gradient_paint(rad.get(), 1.0);
  ASSERT_EQ(gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
  for (uint32_t kinds : {0u, (1u << GSHSCENE_GRADIENT_LINEAR) | (1u << GSHSCENE_GRADIENT_SWEEP)}) {
    GSHSCENE_Dump_Sink_Config c = full_config();
    c.raster_caps.gradient_kinds = kinds;
    Render r(c);
    EXPECT_EQ(r.run(l.list), GSHSCENE_OK);
    auto refused = r.entries(GSHSCENE_ENTRY_REFUSED);
    ASSERT_EQ(refused.size(), 1u);
    EXPECT_EQ(refused[0].reason, GSHSCENE_REASON_GRADIENT);
    EXPECT_EQ(count_of(r.dump, "X class=1 reason=21"), 1u);
    EXPECT_EQ(count_of(r.dump, "R "), 0u) << "a refused gradient is never resolved";
    // Strict: refused before the sink is called.
    unsigned char * px = (unsigned char *)gimg_raster_pixels(r.raster);
    std::memset(px, 0x33, 64 * 64 * 4);
    EXPECT_EQ(r.run(l.list, GSHSCENE_RENDER_STRICT), GSHSCENE_ERR_REFUSED);
    EXPECT_TRUE(r.dump.empty()) << "the sink was never called";
    for (size_t i = 0; i < 64 * 64 * 4; i++) {
      ASSERT_EQ(px[i], 0x33);
    }
  }
  // With the kind, drawn and resolved once.
  Render full(full_config());
  EXPECT_EQ(full.run(l.list), GSHSCENE_OK);
  EXPECT_EQ(gshscene_report_count(full.report), 0u);
  EXPECT_EQ(count_of(full.dump, "R "), 1u);
}

TEST(GradientPlan, AnUnhonoredInterpolationSpaceIsApproximated) {
  StoreHolder s;
  Ref sq(intern_square(s.store));
  Desc stated = linear(0, 0, 8, 0);
  stated.g.space = linear_srgb_space();
  stated.stop(0, solid(0, 0, 0, 1) /* target space: not allowed */);
  stated.stops[0].color.space = linear_srgb_space();
  Desc as_target = linear(0, 0, 8, 0);
  as_target.g.space = srgb_space();
  as_target.stop(0, spaced(0, 0, 0, 1, srgb_space()));
  Ref a(intern_g(s.store, stated));
  Ref b(intern_g(s.store, as_target));
  Ref c(intern_g(s.store, gen_radial()));
  ListHolder l(s.store);
  for (const GSHSCENE_Resource * g : {a.get(), b.get(), c.get()}) {
    GSHSCENE_Paint p = gradient_paint(g, 1.0);
    ASSERT_EQ(gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK);
  }
  ASSERT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
  GSHSCENE_Dump_Sink_Config cfg = full_config();
  cfg.raster_caps.flags &= ~GSHSCENE_CAP_GRADIENT_SPACES;
  Render r(cfg);
  EXPECT_EQ(r.run(l.list, GSHSCENE_RENDER_STRICT), GSHSCENE_OK) << "an approximation never fails";
  auto approx = r.entries(GSHSCENE_ENTRY_APPROXIMATED);
  ASSERT_EQ(approx.size(), 1u) << "only the stated space other than the target's (sRGB)";
  EXPECT_EQ(approx[0].reason, GSHSCENE_REASON_INTERPOLATION_SPACE);
  EXPECT_EQ(approx[0].chain[0], 0u);
  Render full(full_config());
  EXPECT_EQ(full.run(l.list), GSHSCENE_OK);
  EXPECT_EQ(gshscene_report_count(full.report), 0u);
}

TEST(GradientPlan, AMissingPaletteStopIsRefusedPerFillInTheListAndInAPicture) {
  StoreHolder s;
  Ref sq(intern_square(s.store));
  Desc d = linear(0, 0, 8, 0);
  d.stop(0, entry_paint(3, 1.0)).stop(1, solid(1, 1, 1, 1));
  Ref g(intern_g(s.store, d));
  GSHSCENE_Paint p = gradient_paint(g.get(), 1.0);
  ListHolder inner(s.store);
  ASSERT_EQ(gshscene_list_fill(inner.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(inner.list), GSHSCENE_OK);
  const GSHSCENE_Resource * pic = nullptr;
  ASSERT_EQ(gshscene_store_intern_picture(s.store, inner.list, &pic), GSHSCENE_OK);
  Ref picture(pic);
  const GSHSCENE_Color four[4] = {{{0, 0, 0, 1}, {}}, {{0, 0, 0, 1}, {}}, {{0, 0, 0, 1}, {}}, {{0, 1, 0, 1}, {}}};
  for (size_t entries : {0u, 3u, 4u}) {
    ListHolder l(s.store);
    Ref pal;
    if (entries) {
      const GSHSCENE_Resource * r = nullptr;
      ASSERT_EQ(gshscene_store_intern_palette(s.store, entries, four, &r), GSHSCENE_OK);
      pal = Ref(r);
      ASSERT_EQ(gshscene_list_set_palette(l.list, pal.get()), GSHSCENE_OK);
    }
    ASSERT_EQ(gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_instance(l.list, picture.get(), nullptr, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
    Render r(full_config());
    EXPECT_EQ(r.run(l.list), GSHSCENE_OK);
    auto refused = r.entries(GSHSCENE_ENTRY_REFUSED);
    if (entries == 4) {
      EXPECT_TRUE(refused.empty());
      EXPECT_EQ(count_of(r.dump, "R "), 1u) << "one gradient, resolved once though drawn twice";
      EXPECT_EQ(count_of(r.dump, " grad="), 2u);
    } else {
      ASSERT_EQ(refused.size(), 2u) << entries << " entries";
      EXPECT_EQ(refused[0].reason, GSHSCENE_REASON_PALETTE_ENTRY);
      EXPECT_EQ(refused[1].reason, GSHSCENE_REASON_PALETTE_ENTRY);
      EXPECT_EQ(refused[1].chain_count, 2u) << "the picture's fill, keyed through the instance";
    }
  }
}

TEST(GradientPlan, WhatDrawsNothingIsANoticeThatNeverFailsAndIsNotDrawn) {
  StoreHolder s;
  Ref sq(intern_square(s.store, 2.0));
  Desc good = linear(0, 0, 8, 0);
  good.stop(0, solid(1, 0, 0, 1)).stop(1, solid(0, 0, 1, 1));
  Ref ok(intern_g(s.store, good));
  Ref point(intern_g(s.store, linear(1, 1, 1, 1)));
  ListHolder inner(s.store);
  GSHSCENE_Paint pok = gradient_paint(ok.get(), 1.0);
  ASSERT_EQ(gshscene_list_fill(inner.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &pok, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(inner.list), GSHSCENE_OK);
  const GSHSCENE_Resource * pic = nullptr;
  ASSERT_EQ(gshscene_store_intern_picture(s.store, inner.list, &pic), GSHSCENE_OK);
  Ref picture(pic);
  ListHolder l(s.store);
  GSHSCENE_Paint ppoint = gradient_paint(point.get(), 1.0);
  GSHGEOM_Map flat = matrix(1, 0, 0, 0);
  GSHGEOM_Map scale = matrix(2, 0, 0, 2);
  ASSERT_EQ(gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &pok, 0, 0.0), GSHSCENE_OK);      // drawn
  ASSERT_EQ(gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &ppoint, 0, 0.0), GSHSCENE_OK);   // degenerate
  ASSERT_EQ(gshscene_list_fill(l.list, sq.get(), &flat, GSHGEOM_FILL_NONZERO, &pok, 0, 0.0), GSHSCENE_OK);        // item map
  ASSERT_EQ(gshscene_list_instance(l.list, picture.get(), &flat, 0, 0.0), GSHSCENE_OK);                          // instance map
  ASSERT_EQ(gshscene_list_instance(l.list, picture.get(), &scale, 0, 0.0), GSHSCENE_OK);                         // drawn
  ASSERT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
  Render r(full_config());
  EXPECT_EQ(r.run(l.list, GSHSCENE_RENDER_STRICT), GSHSCENE_OK) << "a notice never fails strict";
  auto notes = r.entries(GSHSCENE_ENTRY_NOTICE);
  ASSERT_EQ(notes.size(), 3u);
  EXPECT_EQ(notes[0].reason, GSHSCENE_REASON_GRADIENT_DEGENERATE);
  EXPECT_EQ(notes[0].chain[0], 1u);
  EXPECT_EQ(notes[1].reason, GSHSCENE_REASON_MAP_NOT_INVERTIBLE);
  EXPECT_EQ(notes[1].chain[0], 2u);
  EXPECT_EQ(notes[2].reason, GSHSCENE_REASON_MAP_NOT_INVERTIBLE);
  ASSERT_EQ(notes[2].chain_count, 2u);
  EXPECT_EQ(notes[2].chain[0], 3u);
  EXPECT_EQ(notes[2].chain[1], 0u);
  GSHSCENE_Report_Summary sum{};
  ASSERT_EQ(gshscene_report_summary(r.report, &sum), GSHSCENE_OK);
  EXPECT_EQ(sum.by_class[GSHSCENE_ENTRY_NOTICE], 3u);
  EXPECT_EQ(sum.units_done, sum.units_total);
  // The test sink goes through them and draws nothing for them: three X records, two stamps.
  EXPECT_EQ(count_of(r.dump, "X class=5 reason=24"), 1u);
  EXPECT_EQ(count_of(r.dump, "X class=5 reason=23"), 2u);
  EXPECT_EQ(count_of(r.dump, "\nS "), 2u);
  // A singular target map makes every gradient fill a notice; a solid fill is drawn as before.
  ListHolder l2(s.store);
  GSHSCENE_Paint red = solid(1, 0, 0, 1);
  ASSERT_EQ(gshscene_list_fill(l2.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &pok, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(l2.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l2.list), GSHSCENE_OK);
  EXPECT_EQ(r.run(l2.list, 0, matrix(1, 1, 1, 1)), GSHSCENE_OK);
  notes = r.entries(GSHSCENE_ENTRY_NOTICE);
  ASSERT_EQ(notes.size(), 1u);
  EXPECT_EQ(notes[0].chain[0], 0u);
  EXPECT_EQ(notes[0].reason, GSHSCENE_REASON_MAP_NOT_INVERTIBLE);
}

TEST(GradientPlan, AGroupMapOfZeroScaleInAVersionIsANoticeOnItsEmittedFills) {
  // G11 reads the chain of the list a sink is given; a retained scene composes a group's map into the item maps it
  // emits, and a zero scale stays an exact zero there.
  SceneHolder sc;
  Ref sq(intern_square(sc.store()));
  Desc d = linear(0, 0, 8, 0);
  d.stop(0, solid(0, 0, 0, 1)).stop(1, solid(1, 1, 1, 1));
  Ref g(intern_g(sc.store(), d));
  VersionRef v;
  {
    EditHolder e(sc);
    GSHSCENE_Node group = group_node();
    group.map = matrix(0, 0, 0, 1, 3, 4);
    uint64_t gid = add(e.edit, GSHSCENE_ROOT_ID, group);
    GSHSCENE_Node shape = shape_node(sq.get());
    shape.paint = gradient_paint(g.get(), 1.0);
    shape.map = matrix(2, 1, 1, 3);
    add(e.edit, gid, shape);
    GSHSCENE_Node plain = shape_node(sq.get());
    plain.paint = gradient_paint(g.get(), 1.0);
    add(e.edit, GSHSCENE_ROOT_ID, plain);
    publish(sc, e, &v);
  }
  const GSHSCENE_List * list = nullptr;
  ASSERT_EQ(gshscene_version_list(v.get(), &list), GSHSCENE_OK);
  Render r(full_config());
  EXPECT_EQ(r.run(list, GSHSCENE_RENDER_STRICT), GSHSCENE_OK);
  auto notes = r.entries(GSHSCENE_ENTRY_NOTICE);
  ASSERT_EQ(notes.size(), 1u);
  EXPECT_EQ(notes[0].reason, GSHSCENE_REASON_MAP_NOT_INVERTIBLE);
  uint64_t node = 0;
  uint64_t path[4];
  size_t count = 0;
  size_t index = 0;
  for (size_t i = 0; i < gshscene_report_count(r.report); i++) {
    GSHSCENE_Report_Entry e{};
    gshscene_report_entry(r.report, i, &e);
    if (e.entry_class == GSHSCENE_ENTRY_NOTICE) {
      index = i;
    }
  }
  ASSERT_EQ(gshscene_report_resolve(r.report, v.get(), index, &node, path, 4, &count), GSHSCENE_OK);
  EXPECT_EQ(node, 3u) << "the shape inside the flattened group";
}

TEST(GradientPlan, APaletteStopOfEntryZeroIsMissingWithoutAPalette) {
  StoreHolder s;
  Ref sq(intern_square(s.store));
  Desc d = linear(0, 0, 8, 0);
  d.stop(0, entry_paint(0, 1.0)).stop(1, solid(1, 1, 1, 1));
  Ref g(intern_g(s.store, d));
  ListHolder l(s.store);
  GSHSCENE_Paint p = gradient_paint(g.get(), 1.0);
  ASSERT_EQ(gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
  Render r(full_config());
  EXPECT_EQ(r.run(l.list), GSHSCENE_OK);
  auto refused = r.entries(GSHSCENE_ENTRY_REFUSED);
  ASSERT_EQ(refused.size(), 1u);
  EXPECT_EQ(refused[0].reason, GSHSCENE_REASON_PALETTE_ENTRY);
}

TEST(GradientPlan, AGradientOnlyInAPictureIsResolved) {
  // The test sink sizes its room for gradients over the pictures too: a list whose only gradient fill is inside an
  // instanced picture.
  StoreHolder s;
  Ref sq(intern_square(s.store));
  Desc d = linear(0, 0, 8, 0);
  d.stop(0, solid(0, 0, 0, 1)).stop(0.5, solid(0, 1, 0, 1)).stop(1, solid(1, 1, 1, 1));
  Ref g(intern_g(s.store, d));
  ListHolder inner(s.store);
  GSHSCENE_Paint p = gradient_paint(g.get(), 1.0);
  ASSERT_EQ(gshscene_list_fill(inner.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(inner.list), GSHSCENE_OK);
  const GSHSCENE_Resource * pic = nullptr;
  ASSERT_EQ(gshscene_store_intern_picture(s.store, inner.list, &pic), GSHSCENE_OK);
  Ref picture(pic);
  ListHolder l(s.store);
  ASSERT_EQ(gshscene_list_instance(l.list, picture.get(), nullptr, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
  Render r(full_config());
  ASSERT_EQ(r.run(l.list), GSHSCENE_OK);
  EXPECT_EQ(count_of(r.dump, "\nR "), 1u);
  EXPECT_EQ(count_of(r.dump, "\nr "), 3u);
}

TEST(GradientPlan, TheTestSinkWritesEachGradientOnceWithItsResolvedStops) {
  StoreHolder s;
  Ref sq(intern_square(s.store));
  Desc d = linear(0, 0, 8, 0);
  d.g.space = srgb_space();
  d.stop(1, spaced(0.25, 0.5, 0.75, 1, srgb_space())).stop(0, fg_paint(0.5));
  Ref g(intern_g(s.store, d));
  Ref h(intern_g(s.store, gen_radial()));
  ListHolder l(s.store);
  GSHSCENE_Color fg = {{1, 0.5, 0, 1}, srgb_space()};
  ASSERT_EQ(gshscene_list_set_foreground(l.list, &fg), GSHSCENE_OK);
  GSHSCENE_Paint pg = gradient_paint(g.get(), 1.0);
  GSHSCENE_Paint ph = gradient_paint(h.get(), 0.5);
  Desc three = linear(0, 0, 8, 0);
  three.stop(0, solid(0, 0, 0, 1)).stop(0.5, solid(0, 1, 0, 1)).stop(1, solid(1, 1, 1, 1));
  Ref g3(intern_g(s.store, three));
  GSHSCENE_Paint p3 = gradient_paint(g3.get(), 1.0);
  ASSERT_EQ(gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &p3, 0, 0.0), GSHSCENE_OK);
  for (int i = 0; i < 3; i++) {
    ASSERT_EQ(gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &pg, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &ph, 0, 0.0), GSHSCENE_OK);
  }
  ASSERT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
  Render r(full_config());
  ASSERT_EQ(r.run(l.list), GSHSCENE_OK);
  EXPECT_EQ(count_of(r.dump, "\nR "), 3u) << "once per gradient per render (S17)";
  EXPECT_EQ(count_of(r.dump, "\nr "), 7u);
  char line[256];
  std::snprintf(line, sizeof line, "\nR %llu %016llx space=1:2:0000000000000000 stops=2\n",
      (unsigned long long)gshscene_resource_id(g.get()), (unsigned long long)gshscene_resource_hash(g.get()));
  EXPECT_NE(r.dump.find(line), std::string::npos) << r.dump;
  // The stops in stored order, resolved: the foreground (its alpha halved) first, copied exactly (same space).
  EXPECT_NE(r.dump.find("\nr 0000000000000000 3ff0000000000000 3fe0000000000000 0000000000000000 3fe0000000000000\n"),
      std::string::npos) << r.dump;
  EXPECT_NE(r.dump.find("\nr 3ff0000000000000 3fd0000000000000 3fe0000000000000 3fe8000000000000 3ff0000000000000\n"),
      std::string::npos);
  std::snprintf(line, sizeof line, " grad=%llu box=", (unsigned long long)gshscene_resource_id(h.get()));
  EXPECT_EQ(count_of(r.dump, line), 3u);
  // A second render resolves again: the record is per render.
  ASSERT_EQ(r.run(l.list), GSHSCENE_OK);
  EXPECT_EQ(count_of(r.dump, "\nR "), 3u);
}

TEST(GradientPlan, TheCapsGradientKindsAreValidated) {
  GSHSCENE_Dump_Sink_Config c = full_config();
  c.raster_caps.gradient_kinds |= 1u;
  GSHSCENE_Sink * sink = nullptr;
  EXPECT_EQ(gshscene_dump_sink_create(nullptr, nullptr, &c, &sink), GSHSCENE_ERR_INVALID) << "bit 0 is no kind";
  c = full_config();
  c.stream_caps.gradient_kinds |= 1u << GSHSCENE_GRADIENT_KIND_COUNT;
  EXPECT_EQ(gshscene_dump_sink_create(nullptr, nullptr, &c, &sink), GSHSCENE_ERR_INVALID) << "a kind past the last";
  c = full_config();
  c.raster_caps.gradient_kinds = 0;
  ASSERT_EQ(gshscene_dump_sink_create(nullptr, nullptr, &c, &sink), GSHSCENE_OK) << "none is a sink without gradients";
  gshscene_sink_destroy(sink);
}

// --- the float-free constructor (fixed.h) -------------------------------------------------------------------------------

TEST(GradientFixed, IntegersConvertExactlyAndTheMapPlacesTheDesignSpace) {
  GSHSCENE_Test_Provider tp{};
  tp.upem = 2048;
  StoreHolder s;
  Ref face(gshscene_tp_face(s.store, &tp, 1, 0x77, 0));
  Ref run(intern_font_run(s.store, face.get(), 20.48, {GSHSCENE_Glyph{3, 10.0, 20.0, nullptr}}));
  ASSERT_NE(run.get(), nullptr);
  GSHSCENE_Gradient_Stop_Fixed stops[3] = {
      {16384, 14, {GSHSCENE_FIXED_PAINT_PALETTE, 2, 16384, 14}},
      {0, 14, {GSHSCENE_FIXED_PAINT_PALETTE, 0xFFFFu, 8192, 14}},
      {-8192, 14, {GSHSCENE_FIXED_PAINT_FOREGROUND, 0, 1, 0}},
  };
  GSHSCENE_Gradient_Fixed f{};
  f.kind = GSHSCENE_FIXED_GRADIENT_LINEAR;
  f.extend = GSHSCENE_EXTEND_REFLECT;
  f.bits = 0;
  int32_t v[6] = {-100, 50, 900, 50, -100, 2050};
  std::memcpy(f.v, v, sizeof v);
  f.primaries = GCOL_PRIMARIES_SRGB;
  f.transfer = GCOL_TRANSFER_LINEAR;
  f.premultiplied = 1;
  f.stop_count = 3;
  f.stops = stops;
  const GSHSCENE_Resource * out = nullptr;
  ASSERT_EQ(gshscene_store_intern_gradient_fixed(s.store, run.get(), &f, &out), GSHSCENE_OK);
  Ref g(out);
  GSHSCENE_Gradient back;
  ASSERT_EQ(gshscene_resource_gradient(g.get(), &back), GSHSCENE_OK);
  EXPECT_EQ(back.p0[0], -100.0);
  EXPECT_EQ(back.p1[0], 900.0);
  EXPECT_EQ(back.d[0], 0.0) << "d = p2 - p0";
  EXPECT_EQ(back.d[1], 2000.0);
  EXPECT_EQ(back.extend, (uint32_t)GSHSCENE_EXTEND_REFLECT);
  EXPECT_EQ(back.premultiplied, 1u);
  EXPECT_EQ(back.space.primaries, GCOL_PRIMARIES_SRGB);
  EXPECT_EQ(back.space.transfer, GCOL_TRANSFER_LINEAR);
  ASSERT_EQ(back.stop_count, 3u);
  EXPECT_EQ(back.stops[0].offset, -0.5);
  EXPECT_EQ(back.stops[0].color.kind, GSHSCENE_PAINT_FOREGROUND);
  EXPECT_EQ(back.stops[1].offset, 0.0);
  EXPECT_EQ(back.stops[1].color.kind, GSHSCENE_PAINT_FOREGROUND) << "0xFFFF is the foreground";
  EXPECT_EQ(back.stops[1].color.rgba[3], 0.5);
  EXPECT_EQ(back.stops[2].color.kind, GSHSCENE_PAINT_PALETTE);
  EXPECT_EQ(back.stops[2].color.entry, 2u);
  // The map: a design point lands where the glyph's outline puts it, g + s (x, -y), s = 20.48 / 2048.
  GSHGEOM_Point q{};
  ASSERT_EQ(gshgeom_map_apply_point(&back.map, GSHGEOM_Point{100, 200, 0}, &q), GSHGEOM_OK);
  EXPECT_EQ(q.x, 10.0 + 1.0);
  EXPECT_EQ(q.y, 20.0 - 2.0);
  // With ops: T first, then the placement.
  GSHSCENE_Fixed_Op move{};
  move.kind = GSHSCENE_FIXED_TRANSLATE;
  move.v[0] = 100;
  f.ops = &move;
  f.op_count = 1;
  ASSERT_EQ(gshscene_store_intern_gradient_fixed(s.store, run.get(), &f, &out), GSHSCENE_OK);
  Ref g2(out);
  ASSERT_EQ(gshscene_resource_gradient(g2.get(), &back), GSHSCENE_OK);
  ASSERT_EQ(gshgeom_map_apply_point(&back.map, GSHGEOM_Point{100, 200, 0}, &q), GSHGEOM_OK);
  EXPECT_EQ(q.x, 10.0 + 2.0);
  // Radial and sweep: the radii, and half turns as degrees.
  GSHSCENE_Gradient_Fixed r{};
  r.kind = GSHSCENE_FIXED_GRADIENT_RADIAL;
  r.bits = 2;
  int32_t rv[6] = {4, -8, 6, 0, 0, 65535 * 4};
  std::memcpy(r.v, rv, sizeof rv);
  r.primaries = GCOL_PRIMARIES_SRGB;
  r.transfer = GCOL_TRANSFER_SRGB;
  ASSERT_EQ(gshscene_store_intern_gradient_fixed(s.store, run.get(), &r, &out), GSHSCENE_OK);
  Ref g3(out);
  ASSERT_EQ(gshscene_resource_gradient(g3.get(), &back), GSHSCENE_OK);
  EXPECT_EQ(back.c0[0], 1.0);
  EXPECT_EQ(back.c0[1], -2.0);
  EXPECT_EQ(back.r0, 1.5);
  EXPECT_EQ(back.r1, 65535.0);
  GSHSCENE_Gradient_Fixed w{};
  w.kind = GSHSCENE_FIXED_GRADIENT_SWEEP;
  w.bits = 14;
  int32_t wv[6] = {0, 0, 8192, -16384 * 3, 0, 0};
  std::memcpy(w.v, wv, sizeof wv);
  w.primaries = GCOL_PRIMARIES_SRGB;
  w.transfer = GCOL_TRANSFER_SRGB;
  ASSERT_EQ(gshscene_store_intern_gradient_fixed(s.store, run.get(), &w, &out), GSHSCENE_OK);
  Ref g4(out);
  ASSERT_EQ(gshscene_resource_gradient(g4.get(), &back), GSHSCENE_OK);
  EXPECT_EQ(back.angle_unit, (uint32_t)GSHGEOM_ANGLE_DEGREES);
  EXPECT_EQ(back.a0, 90.0);
  EXPECT_EQ(back.a1, -540.0);
  int32_t big[6] = {0, 0, 2147483647, -2147483647 - 1, 0, 0};
  std::memcpy(w.v, big, sizeof big);
  w.bits = 30;
  ASSERT_EQ(gshscene_store_intern_gradient_fixed(s.store, run.get(), &w, &out), GSHSCENE_OK);
  Ref g5(out);
  ASSERT_EQ(gshscene_resource_gradient(g5.get(), &back), GSHSCENE_OK);
  EXPECT_EQ(back.a0, 2147483647.0 * 180.0 / 1073741824.0) << "exact";
  EXPECT_EQ(back.a1, -360.0);
}

TEST(GradientFixed, EveryRuleIsChecked) {
  GSHSCENE_Test_Provider tp{};
  StoreHolder s;
  StoreHolder other;
  Ref face(gshscene_tp_face(s.store, &tp, 1, 0x77, 0));
  Ref run(intern_font_run(s.store, face.get(), 12, {GSHSCENE_Glyph{3, 0, 0, nullptr}}));
  Ref empty_run(intern_font_run(s.store, face.get(), 12, {}));
  Ref sq(intern_square(s.store));
  GSHSCENE_Gradient_Fixed f{};
  f.kind = GSHSCENE_FIXED_GRADIENT_LINEAR;
  f.v[2] = 10;
  f.primaries = GCOL_PRIMARIES_SRGB;
  f.transfer = GCOL_TRANSFER_LINEAR;
  const GSHSCENE_Resource * out = nullptr;
  auto try_ = [&](GSHSCENE_Gradient_Fixed g, const GSHSCENE_Resource * r) {
    const GSHSCENE_Resource * o = nullptr;
    GSHSCENE_Result result = gshscene_store_intern_gradient_fixed(s.store, r, &g, &o);
    gshscene_resource_release(o);
    return result;
  };
  const GSHSCENE_Result INV = GSHSCENE_ERR_INVALID;
  EXPECT_EQ(try_(f, run.get()), GSHSCENE_OK);
  EXPECT_EQ(gshscene_store_intern_gradient_fixed(nullptr, run.get(), &f, &out), INV);
  EXPECT_EQ(gshscene_store_intern_gradient_fixed(s.store, run.get(), nullptr, &out), INV);
  EXPECT_EQ(gshscene_store_intern_gradient_fixed(s.store, run.get(), &f, nullptr), INV);
  EXPECT_EQ(try_(f, nullptr), INV);
  EXPECT_EQ(try_(f, sq.get()), INV) << "a path has no design space";
  EXPECT_EQ(try_(f, empty_run.get()), INV) << "no glyph to be about";
  Ref oface(gshscene_tp_face(other.store, &tp, 1, 0x77, 0));
  Ref orun(intern_font_run(other.store, oface.get(), 12, {GSHSCENE_Glyph{3, 0, 0, nullptr}}));
  EXPECT_EQ(try_(f, orun.get()), INV) << "another store's run";
  GSHSCENE_Gradient_Fixed g = f;
  g.kind = 0;
  EXPECT_EQ(try_(g, run.get()), INV);
  g.kind = 4;
  EXPECT_EQ(try_(g, run.get()), INV);
  g = f;
  g.bits = GSHSCENE_FIXED_MAX_BITS + 1;
  EXPECT_EQ(try_(g, run.get()), INV);
  g = f;
  g.kind = GSHSCENE_FIXED_GRADIENT_SWEEP;
  g.v[2] = 0;
  g.v[4] = 1;
  EXPECT_EQ(try_(g, run.get()), INV) << "a value the kind does not use";
  g.v[4] = 0;
  g.v[5] = 1;
  EXPECT_EQ(try_(g, run.get()), INV);
  g = f;
  g.transfer = GCOL_TRANSFER_GAMMA;
  EXPECT_EQ(try_(g, run.get()), INV) << "no gamma without a double";
  g.transfer = GCOL_TRANSFER_UNKNOWN;
  EXPECT_EQ(try_(g, run.get()), INV);
  g = f;
  g.primaries = GCOL_PRIMARIES_PROPHOTO;
  EXPECT_EQ(try_(g, run.get()), GSHSCENE_ERR_UNSUPPORTED);
  g = f;
  g.extend = 3;
  EXPECT_EQ(try_(g, run.get()), INV);
  g = f;
  g.premultiplied = 2;
  EXPECT_EQ(try_(g, run.get()), INV);
  g = f;
  g.stop_count = 1;
  EXPECT_EQ(try_(g, run.get()), INV) << "stops without a pointer";
  GSHSCENE_Gradient_Stop_Fixed bad_bits = {0, 31, {GSHSCENE_FIXED_PAINT_FOREGROUND, 0, 1, 0}};
  g.stops = &bad_bits;
  EXPECT_EQ(try_(g, run.get()), INV);
  GSHSCENE_Gradient_Stop_Fixed bad_paint = {0, 0, {GSHSCENE_FIXED_PAINT_PALETTE, 0x10000u, 1, 0}};
  g.stops = &bad_paint;
  EXPECT_EQ(try_(g, run.get()), INV);
  GSHSCENE_Gradient_Stop_Fixed bad_alpha = {0, 0, {GSHSCENE_FIXED_PAINT_FOREGROUND, 0, 2, 0}};
  g.stops = &bad_alpha;
  EXPECT_EQ(try_(g, run.get()), INV);
  g = f;
  g.op_count = 1;
  EXPECT_EQ(try_(g, run.get()), INV) << "ops without a pointer";
  std::vector<GSHSCENE_Fixed_Op> ops(GSHSCENE_FIXED_MAX_OPS + 1);
  for (auto & o : ops) {
    o.kind = GSHSCENE_FIXED_TRANSLATE;
  }
  g.ops = ops.data();
  g.op_count = ops.size();
  EXPECT_EQ(try_(g, run.get()), GSHSCENE_ERR_LIMIT);
  g.op_count = GSHSCENE_FIXED_MAX_OPS;
  EXPECT_EQ(try_(g, run.get()), GSHSCENE_OK);
  ops[0].kind = 99;
  EXPECT_EQ(try_(g, run.get()), INV) << "a bad op";
  GSHSCENE_Limits limits;
  gshscene_limits_default(&limits);
  limits.max_resource_elements = 1;
  StoreHolder tight(&limits);
  Ref tface(gshscene_tp_face(tight.store, &tp, 1, 0x77, 0));
  Ref trun(intern_font_run(tight.store, tface.get(), 12, {GSHSCENE_Glyph{3, 0, 0, nullptr}}));
  GSHSCENE_Gradient_Stop_Fixed two[2] = {{0, 0, {GSHSCENE_FIXED_PAINT_FOREGROUND, 0, 1, 0}}, {1, 0, {GSHSCENE_FIXED_PAINT_FOREGROUND, 0, 1, 0}}};
  g = f;
  g.stop_count = 2;
  g.stops = two;
  EXPECT_EQ(gshscene_store_intern_gradient_fixed(tight.store, trun.get(), &g, &out), GSHSCENE_ERR_LIMIT);
}

TEST(GradientFixed, AFillOfAGradientRecordsItsAlphaAndMap) {
  GSHSCENE_Test_Provider tp{};
  StoreHolder s;
  Ref face(gshscene_tp_face(s.store, &tp, 1, 0x77, 0));
  Ref run(intern_font_run(s.store, face.get(), 12, {GSHSCENE_Glyph{3, 0, 0, nullptr}}));
  Ref g(intern_g(s.store, gen_radial()));
  ListHolder l(s.store);
  ASSERT_EQ(gshscene_list_fill_gradient_fixed(l.list, run.get(), nullptr, 0, GSHGEOM_FILL_EVEN_ODD, g.get(), 3, 2, 5),
      GSHSCENE_OK);
  const GSHSCENE_Item * item = nullptr;
  ASSERT_EQ(gshscene_list_item(l.list, 0, &item), GSHSCENE_OK);
  EXPECT_EQ(item->paint.kind, GSHSCENE_PAINT_GRADIENT);
  EXPECT_EQ(item->paint.gradient, g.get());
  EXPECT_EQ(item->paint.rgba[3], 0.75);
  EXPECT_EQ(item->layer, 5);
  EXPECT_EQ(item->fill_rule, GSHGEOM_FILL_EVEN_ODD);
  EXPECT_EQ(gshscene_list_fill_gradient_fixed(l.list, run.get(), nullptr, 0, GSHGEOM_FILL_NONZERO, g.get(), 5, 2, 0),
      GSHSCENE_ERR_INVALID) << "alpha above 1";
  EXPECT_EQ(gshscene_list_fill_gradient_fixed(l.list, run.get(), nullptr, 0, GSHGEOM_FILL_NONZERO, g.get(), -1, 2, 0),
      GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_fill_gradient_fixed(l.list, run.get(), nullptr, 0, GSHGEOM_FILL_NONZERO, g.get(), 1, 31, 0),
      GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_fill_gradient_fixed(l.list, run.get(), nullptr, 0, GSHGEOM_FILL_NONZERO, nullptr, 1, 0, 0),
      GSHSCENE_ERR_INVALID) << "no gradient";
  EXPECT_EQ(gshscene_list_fill_gradient_fixed(l.list, run.get(), nullptr, 0, GSHGEOM_FILL_NONZERO, run.get(), 1, 0, 0),
      GSHSCENE_ERR_INVALID) << "a run is no gradient";
  EXPECT_EQ(gshscene_list_fill_gradient_fixed(nullptr, run.get(), nullptr, 0, GSHGEOM_FILL_NONZERO, g.get(), 1, 0, 0),
      GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_fill_gradient_fixed(l.list, nullptr, nullptr, 0, GSHGEOM_FILL_NONZERO, g.get(), 1, 0, 0),
      GSHSCENE_ERR_INVALID);
  GSHSCENE_Fixed_Op bad{};
  bad.kind = 77;
  EXPECT_EQ(gshscene_list_fill_gradient_fixed(l.list, run.get(), &bad, 1, GSHGEOM_FILL_NONZERO, g.get(), 1, 0, 0),
      GSHSCENE_ERR_INVALID);
  ASSERT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_fill_gradient_fixed(l.list, run.get(), nullptr, 0, GSHGEOM_FILL_NONZERO, g.get(), 1, 0, 0),
      GSHSCENE_ERR_INVALID) << "closed";
  EXPECT_EQ(gshscene_list_count(l.list), 1u);
}

// --- the evaluator against the oracle (G4 to G12) ---------------------------------------------------------------------

TEST(GradientVectors, TheEvaluatorTakesTheLastOfEqualOffsetsAtThatOffset) {
  // G4 on the discontinuity itself, which the vectors leave out (no sample lies within 1e-30 of one).
  std::vector<refc::GStop> stops = {{0.0, {1, 0, 0, 1}}, {0.5, {0, 1, 0, 1}}, {0.5, {0, 0, 1, 1}}, {1.0, {1, 1, 1, 1}}};
  const double line[6] = {0, 0, 8, 0, 0, 0};
  std::vector<refc::GMap> chain = {refc::GMap{{1, 0, 0, 1, 0, 0}}};
  refc::GPoint at = refc::gradient_at(GSHSCENE_GRADIENT_LINEAR, line, 0, GSHSCENE_EXTEND_PAD, 0, stops, chain, 4, 0, 1);
  EXPECT_EQ(at.rgba[2], 1.0) << "the last of the stops at 0.5";
  EXPECT_EQ(at.rgba[1], 0.0);
  refc::GPoint below = refc::gradient_at(GSHSCENE_GRADIENT_LINEAR, line, 0, GSHSCENE_EXTEND_PAD, 0, stops, chain,
      3.9999999999999996, 0, 1);
  EXPECT_GT(below.rgba[1], 0.99) << "the first just below";
}

TEST(GradientVectors, TheEvaluatorAgreesWithTheOracleAndTheLibraryResolvesItsStops) {
  StoreHolder s;
  GSHSCENE_Color fg{};
  gshscene_color_init(&fg);
  GSHSCENE_Color_Space target = srgb_space();
  double worst[4] = {0, 0, 0, 0};
  int exact_cases = 0;
  for (int i = 0; i < gradient_vectors::kGradientCaseCount; i++) {
    const gradient_vectors::VCase & c = gradient_vectors::kGradientCases[i];
    // Build the resource through the API: the stops as solids in the interpolation space (linear sRGB).
    Desc d;
    d.g.kind = (uint32_t)c.kind;
    if (c.kind == GSHSCENE_GRADIENT_LINEAR) {
      d = linear(c.geom[0], c.geom[1], c.geom[2], c.geom[3], c.geom[4], c.geom[5]);
    } else if (c.kind == GSHSCENE_GRADIENT_RADIAL) {
      d = radial(c.geom[0], c.geom[1], c.geom[2], c.geom[3], c.geom[4], c.geom[5]);
    } else {
      d = sweep(c.geom[0], c.geom[1], (uint32_t)c.unit, c.geom[2], c.geom[3]);
    }
    d.g.extend = (uint32_t)c.extend;
    d.g.premultiplied = (uint32_t)c.premultiplied;
    d.g.space = linear_srgb_space();
    const gradient_vectors::VMap & gm = c.chain[c.chain_count - 1];
    d.g.map = matrix(gm.m[0], gm.m[1], gm.m[2], gm.m[3], gm.m[4], gm.m[5]);
    for (int k = 0; k < c.stop_count; k++) {
      const gradient_vectors::VStop & st = c.stops[k];
      d.stop(st.offset, spaced(st.rgba[0], st.rgba[1], st.rgba[2], st.rgba[3], linear_srgb_space()));
    }
    Ref g(intern_g(s.store, d));
    ASSERT_NE(g.get(), nullptr) << c.name;
    std::vector<GSHSCENE_Resolved_Stop> resolved((size_t)c.stop_count + 1);
    ASSERT_EQ(gshscene_gradient_resolve(g.get(), &fg, nullptr, &target, nullptr, resolved.data(), resolved.size()),
        GSHSCENE_OK);
    std::vector<refc::GStop> stops;
    for (int k = 0; k < c.stop_count; k++) {
      refc::GStop st{resolved[(size_t)k].offset, {resolved[(size_t)k].rgba[0], resolved[(size_t)k].rgba[1],
          resolved[(size_t)k].rgba[2], resolved[(size_t)k].rgba[3]}};
      stops.push_back(st);
    }
    std::vector<refc::GMap> chain;
    for (int k = 0; k < c.chain_count; k++) {
      refc::GMap m;
      std::memcpy(m.m, c.chain[k].m, sizeof m.m);
      chain.push_back(m);
    }
    // The library's predicate and the oracle agree on what draws nothing (G10, G11).
    {
      std::vector<GSHGEOM_Map> instances;
      for (int k = 1; k + 2 < c.chain_count; k++) {
        const gradient_vectors::VMap & m = c.chain[k];
        instances.push_back(matrix(m.m[0], m.m[1], m.m[2], m.m[3], m.m[4], m.m[5]));
      }
      const gradient_vectors::VMap & tm = c.chain[0];
      const gradient_vectors::VMap & im = c.chain[c.chain_count - 2];
      GSHGEOM_Map target_map = matrix(tm.m[0], tm.m[1], tm.m[2], tm.m[3], tm.m[4], tm.m[5]);
      GSHSCENE_Item item = fill_item(g.get(), matrix(im.m[0], im.m[1], im.m[2], im.m[3], im.m[4], im.m[5]));
      GSHSCENE_Reason n = gshscene_gradient_notice(&item, instances.data(), instances.size(), &target_map);
      int want = c.reason == 3 ? GSHSCENE_REASON_MAP_NOT_INVERTIBLE : c.reason == 2 ? GSHSCENE_REASON_GRADIENT_DEGENERATE : 0;
      EXPECT_EQ((int)n, want) << c.name;
    }
    refc::GPoint r = refc::gradient_at(c.kind, c.geom, c.unit, c.extend, c.premultiplied, stops, chain, c.q[0], c.q[1],
        c.alpha);
    ASSERT_EQ(r.drawn, c.drawn) << c.name;
    EXPECT_EQ(r.reason, c.reason) << c.name;
    if (!c.drawn) {
      continue;
    }
    if (c.exact) {
      exact_cases++;
      EXPECT_EQ(r.t, c.t) << c.name;
      for (int k = 0; k < 4; k++) {
        EXPECT_EQ(r.rgba[k], c.rgba[k]) << c.name << " component " << k;
      }
    } else {
      bool step = c.kind == GSHSCENE_GRADIENT_SWEEP && c.geom[2] == c.geom[3];
      if (!step) {
        EXPECT_NEAR(r.t, c.t, 1e-12 * std::max(1.0, std::fabs(c.t))) << c.name;
      }
      for (int k = 0; k < 4; k++) {
        EXPECT_NEAR(r.rgba[k], c.rgba[k], 1e-12) << c.name << " component " << k;
      }
    }
    int slot = c.kind == GSHSCENE_GRADIENT_SWEEP ? 2 : c.kind - 1;
    for (int k = 0; k < 4; k++) {
      worst[slot] = std::max(worst[slot], std::fabs(r.rgba[k] - c.rgba[k]));
    }
    if (!(c.kind == GSHSCENE_GRADIENT_SWEEP && c.geom[2] == c.geom[3])) {
      worst[3] = std::max(worst[3], std::fabs(r.t - c.t) / std::max(1.0, std::fabs(c.t)));
    }
  }
  EXPECT_GT(exact_cases, 100);
  std::printf("gradient vectors: %d points, %d exact; worst color error linear %.3g radial %.3g sweep %.3g, t %.3g\n",
      gradient_vectors::kGradientCaseCount, exact_cases, worst[0], worst[1], worst[2], worst[3]);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
