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
 * The filter resource (filter.h, FL1 to FL6, FL12): interning and the order of its refusals, the canonical form, equality, the
 * hash restated from the header's text, the dump, a group that names one (its checks, its retention, its hash), the copy into
 * another store, and what a sink is asked for (::gshscene_filter_required, ::gshscene_filter_axis_aligned).
 */

#include "scene_helpers.h"

#include "../alloc_tracker.h"
#include "../image_fixtures.h"

#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace scene;

namespace {

const double kNaN = std::numeric_limits<double>::quiet_NaN();
const double kInf = std::numeric_limits<double>::infinity();

GSHSCENE_Filter_Primitive prim(uint32_t kind) {
  GSHSCENE_Filter_Primitive p;
  std::memset(&p, 0, sizeof p);
  p.kind = kind;
  return p;
}

const GSHSCENE_Filter_Input kPrev = {GSHSCENE_FILTER_INPUT_PREVIOUS, 0};
const GSHSCENE_Filter_Input kSrc = {GSHSCENE_FILTER_INPUT_SOURCE_GRAPHIC, 0};
const GSHSCENE_Filter_Input kAlpha = {GSHSCENE_FILTER_INPUT_SOURCE_ALPHA, 0};

GSHSCENE_Filter_Primitive blur(double sx, double sy) {
  static const GSHSCENE_Filter_Input in = kPrev;
  GSHSCENE_Filter_Primitive p = prim(GSHSCENE_FILTER_GAUSSIAN_BLUR);
  p.input_count = 1;
  p.inputs = &in;
  p.sx = sx;
  p.sy = sy;
  return p;
}

GSHSCENE_Filter_Primitive offset(double dx, double dy) {
  static const GSHSCENE_Filter_Input in = kPrev;
  GSHSCENE_Filter_Primitive p = prim(GSHSCENE_FILTER_OFFSET);
  p.input_count = 1;
  p.inputs = &in;
  p.dx = dx;
  p.dy = dy;
  return p;
}

GSHSCENE_Filter_Primitive flood(double r, double g, double b, double a, double opacity = 1.0) {
  GSHSCENE_Filter_Primitive p = prim(GSHSCENE_FILTER_FLOOD);
  p.color.rgba[0] = r;
  p.color.rgba[1] = g;
  p.color.rgba[2] = b;
  p.color.rgba[3] = a;
  p.opacity = opacity;
  return p;
}

GSHSCENE_Filter filter_of(const std::vector<GSHSCENE_Filter_Primitive> & prims) {
  GSHSCENE_Filter f;
  std::memset(&f, 0, sizeof f);
  f.region_mode = GSHSCENE_FILTER_REGION_AUTO;
  f.color_space = GSHSCENE_FILTER_SPACE_LINEAR_RGB;
  f.primitive_count = prims.size();
  f.primitives = prims.data();
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

std::string dump_of(const GSHSCENE_Resource * r) {
  char * buf = nullptr;
  size_t size = 0;
  FILE * f = open_memstream(&buf, &size);
  EXPECT_EQ(gshscene_resource_dump(r, f), GSHSCENE_OK);
  fclose(f);
  std::string out(buf, size);
  free(buf);
  return out;
}

/** The text's FNV-1a 64: a second statement of the hash, over the bytes the header's text spells. */
struct Spell {
  std::vector<uint8_t> b;
  void u8(uint8_t v) { b.push_back(v); }
  void u32(uint32_t v) {
    for (int i = 0; i < 4; i++) {
      b.push_back((uint8_t)(v >> (8 * i)));
    }
  }
  void u64(uint64_t v) {
    for (int i = 0; i < 8; i++) {
      b.push_back((uint8_t)(v >> (8 * i)));
    }
  }
  void f64(double v) {
    uint64_t bits;
    std::memcpy(&bits, &v, 8);
    if (v == 0.0) {
      bits = 0;
    }
    u64(bits);
  }
};

uint64_t fnv(const std::vector<uint8_t> & bytes) {
  uint64_t h = 0xcbf29ce484222325ull;
  auto mix = [&](uint8_t v) {
    h ^= v;
    h *= 0x00000100000001b3ull;
  };
  Spell head;
  head.u32(1);
  head.u32(16);
  head.u64(bytes.size());
  for (uint8_t v : head.b) {
    mix(v);
  }
  for (uint8_t v : bytes) {
    mix(v);
  }
  return h;
}

}  // namespace

TEST(Filter, InternsReadsBackAndIsOneHandleForEqualContent) {
  StoreHolder sh;
  std::vector<GSHSCENE_Filter_Primitive> prims = {offset(2, 3), blur(1.5, 4)};
  GSHSCENE_Filter f = filter_of(prims);
  Ref a, b;
  ASSERT_EQ(intern_filter(sh.store, f, &a), GSHSCENE_OK);
  ASSERT_EQ(intern_filter(sh.store, f, &b), GSHSCENE_OK);
  EXPECT_EQ(a.get(), b.get()) << "equal content is one handle";
  EXPECT_EQ(gshscene_resource_kind(a.get()), GSHSCENE_RESOURCE_FILTER);
  EXPECT_STREQ(gshscene_resource_kind_string(GSHSCENE_RESOURCE_FILTER), "filter");
  GSHSCENE_Filter back;
  ASSERT_EQ(gshscene_resource_filter(a.get(), &back), GSHSCENE_OK);
  EXPECT_EQ(back.region_mode, (uint32_t)GSHSCENE_FILTER_REGION_AUTO);
  EXPECT_EQ(back.color_space, (uint32_t)GSHSCENE_FILTER_SPACE_LINEAR_RGB);
  ASSERT_EQ(back.primitive_count, 2u);
  EXPECT_EQ(back.primitives[0].kind, (uint32_t)GSHSCENE_FILTER_OFFSET);
  EXPECT_EQ(back.primitives[0].dx, 2.0);
  EXPECT_EQ(back.primitives[1].sx, 1.5);
  ASSERT_EQ(back.primitives[1].input_count, 1u);
  EXPECT_EQ(back.primitives[1].inputs[0].kind, (uint32_t)GSHSCENE_FILTER_INPUT_PREVIOUS);
  EXPECT_NE(back.primitives, prims.data()) << "the content is copied";
  // other content is another handle
  std::vector<GSHSCENE_Filter_Primitive> other = {offset(2, 3), blur(1.5, 4.5)};
  Ref c;
  ASSERT_EQ(intern_filter(sh.store, filter_of(other), &c), GSHSCENE_OK);
  EXPECT_NE(c.get(), a.get());
  EXPECT_NE(gshscene_resource_hash(c.get()), gshscene_resource_hash(a.get()));
  // an explicit region and the filter's color space are content
  GSHSCENE_Filter g = f;
  g.region_mode = GSHSCENE_FILTER_REGION_EXPLICIT;
  g.region[2] = 10;
  g.region[3] = 10;
  Ref d;
  ASSERT_EQ(intern_filter(sh.store, g, &d), GSHSCENE_OK);
  EXPECT_NE(d.get(), a.get());
  g = f;
  g.color_space = GSHSCENE_FILTER_SPACE_SRGB;
  Ref e;
  ASSERT_EQ(intern_filter(sh.store, g, &e), GSHSCENE_OK);
  EXPECT_NE(e.get(), a.get());
}

TEST(Filter, NegativeZeroIsOneSpelling) {
  StoreHolder sh;
  std::vector<GSHSCENE_Filter_Primitive> a = {offset(0.0, 1.0)};
  std::vector<GSHSCENE_Filter_Primitive> b = {offset(-0.0, 1.0)};
  Ref ra, rb;
  ASSERT_EQ(intern_filter(sh.store, filter_of(a), &ra), GSHSCENE_OK);
  ASSERT_EQ(intern_filter(sh.store, filter_of(b), &rb), GSHSCENE_OK);
  EXPECT_EQ(ra.get(), rb.get());
  GSHSCENE_Filter back;
  ASSERT_EQ(gshscene_resource_filter(rb.get(), &back), GSHSCENE_OK);
  EXPECT_FALSE(std::signbit(back.primitives[0].dx));
}

TEST(Filter, EachInvalidContentIsRefused) {
  StoreHolder sh;
  struct Case {
    const char * why;
    GSHSCENE_Filter_Primitive p;
    std::vector<GSHSCENE_Filter_Primitive> before;  // earlier primitives
  };
  const GSHSCENE_Filter_Input forward = {GSHSCENE_FILTER_INPUT_RESULT, 5};
  const GSHSCENE_Filter_Input self = {GSHSCENE_FILTER_INPUT_RESULT, 0};
  const GSHSCENE_Filter_Input stray = {GSHSCENE_FILTER_INPUT_SOURCE_GRAPHIC, 1};
  const GSHSCENE_Filter_Input two[2] = {kSrc, kSrc};
  std::vector<Case> cases;
  {
    GSHSCENE_Filter_Primitive p = blur(1, 1);
    p.inputs = &forward;
    cases.push_back({"an input naming a later primitive", p, {}});
    p.inputs = &self;
    cases.push_back({"an input naming itself", p, {}});
    p = blur(1, 1);
    p.inputs = &stray;
    cases.push_back({"an index on an input that takes none", p, {}});
    p = blur(-1, 1);
    cases.push_back({"a negative deviation", p, {}});
    p = blur(kNaN, 1);
    cases.push_back({"a NaN deviation", p, {}});
    p = blur(kInf, 1);
    cases.push_back({"an infinite deviation", p, {}});
    p = blur(1, 1);
    p.dx = 1;
    cases.push_back({"a field the kind does not use", p, {}});
    p = blur(1, 1);
    p.edge_mode = 9;
    cases.push_back({"an edge mode out of range", p, {}});
    p = blur(1, 1);
    p.color_space = 9;
    cases.push_back({"a color space out of range", p, {}});
    p = blur(1, 1);
    p.input_count = 2;
    p.inputs = two;
    cases.push_back({"two inputs on a blur", p, {}});
    p = blur(1, 1);
    p.subregion_mask = 16;
    cases.push_back({"a subregion mask past four bits", p, {}});
    p = blur(1, 1);
    p.subregion[0] = 5;
    cases.push_back({"a subregion value without its bit", p, {}});
    p = blur(1, 1);
    p.subregion_mask = 4;
    p.subregion[2] = -1;
    cases.push_back({"a negative subregion width", p, {}});
    p = flood(0, 0, 0, 1, 2.0);
    cases.push_back({"an opacity above one", p, {}});
    p = flood(0, 0, 0, 2.0);
    cases.push_back({"an alpha above one", p, {}});
    p = flood(0, 0, 0, 1);
    p.input_count = 1;
    p.inputs = &kSrc;
    cases.push_back({"an input on a flood", p, {}});
    p = prim(0);
    cases.push_back({"kind zero", p, {}});
    p = prim(GSHSCENE_FILTER_KIND_COUNT);
    cases.push_back({"a kind past the enum", p, {}});
    p = prim(GSHSCENE_FILTER_COLOR_MATRIX);
    p.input_count = 1;
    p.inputs = &kSrc;
    p.mode = GSHSCENE_FILTER_MATRIX_MATRIX;
    cases.push_back({"a matrix without its twenty values", p, {}});
    p = prim(GSHSCENE_FILTER_SPECULAR_LIGHTING);
    p.input_count = 1;
    p.inputs = &kAlpha;
    p.exponent = 200;
    p.light.kind = GSHSCENE_FILTER_LIGHT_DISTANT;
    cases.push_back({"a specular exponent past 128", p, {}});
    p = prim(GSHSCENE_FILTER_IMAGE);
    cases.push_back({"an image primitive without an image", p, {}});
  }
  for (const Case & c : cases) {
    std::vector<GSHSCENE_Filter_Primitive> prims = c.before;
    prims.push_back(c.p);
    EXPECT_EQ(intern_filter(sh.store, filter_of(prims)), GSHSCENE_ERR_INVALID) << c.why;
  }
  // the filter itself
  std::vector<GSHSCENE_Filter_Primitive> one = {blur(1, 1)};
  GSHSCENE_Filter f = filter_of(one);
  GSHSCENE_Filter bad = f;
  bad.primitive_count = 0;
  EXPECT_EQ(intern_filter(sh.store, bad), GSHSCENE_ERR_INVALID) << "no primitives";
  bad = f;
  bad.primitives = nullptr;
  EXPECT_EQ(intern_filter(sh.store, bad), GSHSCENE_ERR_INVALID) << "no array";
  bad = f;
  bad.region_mode = 7;
  EXPECT_EQ(intern_filter(sh.store, bad), GSHSCENE_ERR_INVALID) << "a region mode out of range";
  bad = f;
  bad.region[0] = 1;
  EXPECT_EQ(intern_filter(sh.store, bad), GSHSCENE_ERR_INVALID) << "an AUTO region with numbers";
  bad = f;
  bad.region_mode = GSHSCENE_FILTER_REGION_EXPLICIT;
  bad.region[2] = -1;
  EXPECT_EQ(intern_filter(sh.store, bad), GSHSCENE_ERR_INVALID) << "a negative width";
  bad = f;
  bad.region_mode = GSHSCENE_FILTER_REGION_EXPLICIT;
  bad.region[0] = kNaN;
  EXPECT_EQ(intern_filter(sh.store, bad), GSHSCENE_ERR_INVALID) << "a NaN region";
  bad = f;
  bad.color_space = GSHSCENE_FILTER_SPACE_INHERIT;
  EXPECT_EQ(intern_filter(sh.store, bad), GSHSCENE_ERR_INVALID) << "a filter space that inherits";
  GSHSCENE_Result r = intern_filter(sh.store, f);
  EXPECT_EQ(r, GSHSCENE_OK);
  EXPECT_EQ(gshscene_store_intern_filter(nullptr, &f, nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_store_intern_filter(sh.store, nullptr, nullptr), GSHSCENE_ERR_INVALID);
}

TEST(Filter, ALimitIsReportedAfterEveryInvalidField) {
  StoreHolder sh;
  // 257 primitives
  std::vector<GSHSCENE_Filter_Primitive> many(GSHSCENE_FILTER_MAX_PRIMITIVES + 1, blur(1, 1));
  EXPECT_EQ(intern_filter(sh.store, filter_of(many)), GSHSCENE_ERR_LIMIT);
  many.resize(GSHSCENE_FILTER_MAX_PRIMITIVES);
  EXPECT_EQ(intern_filter(sh.store, filter_of(many)), GSHSCENE_OK) << "the bound itself";
  // an invalid primitive past the bound's count is still INVALID (decided first)
  std::vector<GSHSCENE_Filter_Primitive> mixed(GSHSCENE_FILTER_MAX_PRIMITIVES + 1, blur(1, 1));
  mixed[3].sx = -1;
  EXPECT_EQ(intern_filter(sh.store, filter_of(mixed)), GSHSCENE_ERR_INVALID);
  // a magnitude past the bound
  std::vector<GSHSCENE_Filter_Primitive> big = {blur(GSHSCENE_FILTER_VALUE_MAX * 2, 1)};
  EXPECT_EQ(intern_filter(sh.store, filter_of(big)), GSHSCENE_ERR_LIMIT);
  big = {blur(GSHSCENE_FILTER_VALUE_MAX, 1)};
  EXPECT_EQ(intern_filter(sh.store, filter_of(big)), GSHSCENE_OK);
  // a limit and an invalid field in different primitives: INVALID wins
  std::vector<GSHSCENE_Filter_Primitive> both = {blur(GSHSCENE_FILTER_VALUE_MAX * 2, 1), blur(-1, 1)};
  EXPECT_EQ(intern_filter(sh.store, filter_of(both)), GSHSCENE_ERR_INVALID);
  // a merge past the bound
  std::vector<GSHSCENE_Filter_Input> inputs(GSHSCENE_FILTER_MAX_MERGE + 1, kSrc);
  GSHSCENE_Filter_Primitive m = prim(GSHSCENE_FILTER_MERGE);
  m.input_count = inputs.size();
  m.inputs = inputs.data();
  EXPECT_EQ(intern_filter(sh.store, filter_of({m})), GSHSCENE_ERR_LIMIT);
  inputs.resize(GSHSCENE_FILTER_MAX_MERGE);
  m.input_count = inputs.size();
  EXPECT_EQ(intern_filter(sh.store, filter_of({m})), GSHSCENE_OK);
  // octaves past the bound
  GSHSCENE_Filter_Primitive t = prim(GSHSCENE_FILTER_TURBULENCE);
  t.octaves = GSHSCENE_FILTER_MAX_OCTAVES + 1;
  EXPECT_EQ(intern_filter(sh.store, filter_of({t})), GSHSCENE_ERR_LIMIT);
  t.octaves = GSHSCENE_FILTER_MAX_OCTAVES;
  EXPECT_EQ(intern_filter(sh.store, filter_of({t})), GSHSCENE_OK);
  // a kernel past the bound
  std::vector<double> kernel(GSHSCENE_FILTER_MAX_KERNEL + 1, 1.0);
  GSHSCENE_Filter_Primitive c = prim(GSHSCENE_FILTER_CONVOLVE_MATRIX);
  c.input_count = 1;
  c.inputs = &kSrc;
  c.order_x = (uint32_t)kernel.size();
  c.order_y = 1;
  c.value_count = kernel.size();
  c.values = kernel.data();
  c.divisor = 1;
  EXPECT_EQ(intern_filter(sh.store, filter_of({c})), GSHSCENE_ERR_LIMIT);
  kernel.resize(GSHSCENE_FILTER_MAX_KERNEL);
  c.order_x = (uint32_t)kernel.size();
  c.value_count = kernel.size();
  EXPECT_EQ(intern_filter(sh.store, filter_of({c})), GSHSCENE_OK);
}

TEST(Filter, TablesMatricesAndLightsAreCopiedAndRead) {
  StoreHolder sh;
  std::vector<double> table = {0.0, 0.25, 1.0};
  GSHSCENE_Filter_Primitive ct = prim(GSHSCENE_FILTER_COMPONENT_TRANSFER);
  ct.input_count = 1;
  ct.inputs = &kSrc;
  ct.transfer[0].type = GSHSCENE_FILTER_TRANSFER_TABLE;
  ct.transfer[0].count = table.size();
  ct.transfer[0].table = table.data();
  ct.transfer[3].type = GSHSCENE_FILTER_TRANSFER_GAMMA;
  ct.transfer[3].amplitude = 2.0;
  ct.transfer[3].exponent = 0.5;
  ct.transfer[3].offset = -0.0;
  std::vector<double> values(20, 0.0);
  values[0] = values[6] = values[12] = values[18] = 1.0;
  GSHSCENE_Filter_Primitive cm = prim(GSHSCENE_FILTER_COLOR_MATRIX);
  cm.input_count = 1;
  cm.inputs = &kPrev;
  cm.mode = GSHSCENE_FILTER_MATRIX_MATRIX;
  cm.value_count = values.size();
  cm.values = values.data();
  GSHSCENE_Filter_Primitive lit = prim(GSHSCENE_FILTER_SPECULAR_LIGHTING);
  lit.input_count = 1;
  lit.inputs = &kAlpha;
  lit.surface_scale = 3;
  lit.constant = 1;
  lit.exponent = 20;
  lit.light.kind = GSHSCENE_FILTER_LIGHT_SPOT;
  lit.light.position[0] = 1;
  lit.light.position[1] = 2;
  lit.light.position[2] = 3;
  lit.light.points_at[0] = 4;
  lit.light.spot_exponent = 2;
  lit.light.has_cone = 1;
  lit.light.cone_angle = 30;
  lit.color.rgba[0] = 1;
  lit.color.rgba[3] = 1;
  std::vector<GSHSCENE_Filter_Primitive> prims = {ct, cm, lit};
  Ref a;
  ASSERT_EQ(intern_filter(sh.store, filter_of(prims), &a), GSHSCENE_OK);
  table[1] = 9.0;  // the caller's arrays are not held
  values[0] = 9.0;
  GSHSCENE_Filter back;
  ASSERT_EQ(gshscene_resource_filter(a.get(), &back), GSHSCENE_OK);
  EXPECT_EQ(back.primitives[0].transfer[0].table[1], 0.25);
  EXPECT_EQ(back.primitives[0].transfer[0].count, 3u);
  EXPECT_FALSE(std::signbit(back.primitives[0].transfer[3].offset));
  EXPECT_EQ(back.primitives[1].values[0], 1.0);
  EXPECT_EQ(back.primitives[2].light.kind, (uint32_t)GSHSCENE_FILTER_LIGHT_SPOT);
  EXPECT_EQ(back.primitives[2].light.cone_angle, 30.0);
  EXPECT_EQ(back.primitives[2].light.points_at[0], 4.0);
  // a light with a field its kind does not use
  GSHSCENE_Filter_Primitive bad = lit;
  bad.light.kind = GSHSCENE_FILTER_LIGHT_DISTANT;
  EXPECT_EQ(intern_filter(sh.store, filter_of({bad})), GSHSCENE_ERR_INVALID);
  bad = lit;
  bad.light.spot_exponent = 0;
  EXPECT_EQ(intern_filter(sh.store, filter_of({bad})), GSHSCENE_ERR_INVALID);
  bad = lit;
  bad.light.has_cone = 0;
  EXPECT_EQ(intern_filter(sh.store, filter_of({bad})), GSHSCENE_ERR_INVALID) << "a cone angle without its bit";
}

TEST(Filter, TheHashIsTheTextsByteString) {
  StoreHolder sh;
  // flood (0.25, 0.5, 0.75, 1) at opacity 0.5 in sRGB, then a blur of (1, 2) of the previous, on an explicit region
  GSHSCENE_Filter_Primitive fl = flood(0.25, 0.5, 0.75, 1.0, 0.5);
  fl.color_space = GSHSCENE_FILTER_SPACE_SRGB;
  fl.subregion_mask = 3;
  fl.subregion[0] = 1;
  fl.subregion[1] = -0.0;
  GSHSCENE_Filter_Primitive bl = blur(1, 2);
  std::vector<GSHSCENE_Filter_Primitive> prims = {fl, bl};
  GSHSCENE_Filter f = filter_of(prims);
  f.region_mode = GSHSCENE_FILTER_REGION_EXPLICIT;
  f.region[0] = -5;
  f.region[1] = 0;
  f.region[2] = 100;
  f.region[3] = 50;
  Ref a;
  ASSERT_EQ(intern_filter(sh.store, f, &a), GSHSCENE_OK);
  Spell s;
  s.u32(1);
  s.u32(16);
  s.u32(GSHSCENE_FILTER_REGION_EXPLICIT);
  for (double v : {-5.0, 0.0, 100.0, 50.0}) {
    s.f64(v);
  }
  s.u8(GSHSCENE_FILTER_SPACE_LINEAR_RGB);
  s.u64(2);
  // the flood: kind 2, space sRGB, no inputs, subregion mask 3 and four numbers, then color, opacity
  s.u8(GSHSCENE_FILTER_FLOOD);
  s.u8(GSHSCENE_FILTER_SPACE_SRGB);
  s.u64(0);
  s.u8(3);
  for (double v : {1.0, 0.0, 0.0, 0.0}) {
    s.f64(v);
  }
  for (double v : {0.25, 0.5, 0.75, 1.0}) {
    s.f64(v);
  }
  s.u8(0);  // the target's space: primaries unknown
  s.u8(0);  // transfer unknown
  s.f64(0.0);
  s.f64(0.5);
  // the blur: kind 8, inherits, one PREVIOUS input, no subregion, deviation, edge none
  s.u8(GSHSCENE_FILTER_GAUSSIAN_BLUR);
  s.u8(GSHSCENE_FILTER_SPACE_INHERIT);
  s.u64(1);
  s.u8(GSHSCENE_FILTER_INPUT_PREVIOUS);
  s.u32(0);
  s.u8(0);
  for (int i = 0; i < 4; i++) {
    s.f64(0.0);
  }
  s.f64(1.0);
  s.f64(2.0);
  s.u32(GSHSCENE_FILTER_EDGE_NONE);
  EXPECT_EQ(gshscene_resource_hash(a.get()), fnv(s.b));
}

TEST(Filter, TheDumpNamesTheRegionAndEachPrimitive) {
  StoreHolder sh;
  std::vector<GSHSCENE_Filter_Primitive> prims = {offset(2, 3), blur(1, 1)};
  Ref a;
  ASSERT_EQ(intern_filter(sh.store, filter_of(prims), &a), GSHSCENE_OK);
  const std::string d = dump_of(a.get());
  EXPECT_NE(d.find("filter region=1 "), std::string::npos) << d;
  EXPECT_NE(d.find("primitives=2"), std::string::npos) << d;
  EXPECT_NE(d.find("fp i=0 kind=1 "), std::string::npos) << d;
  EXPECT_NE(d.find("fp i=1 kind=8 "), std::string::npos) << d;
  EXPECT_NE(d.find("in=0:0 params="), std::string::npos) << d;
  // two different filters dump differently, equal ones identically
  std::vector<GSHSCENE_Filter_Primitive> other = {offset(2, 4), blur(1, 1)};
  Ref b;
  ASSERT_EQ(intern_filter(sh.store, filter_of(other), &b), GSHSCENE_OK);
  EXPECT_NE(dump_of(b.get()), d);
}

TEST(Filter, WhatASinkIsAskedFor) {
  StoreHolder sh;
  std::vector<GSHSCENE_Filter_Primitive> prims = {offset(1, 1), blur(1, 1), flood(0, 0, 0, 1)};
  Ref a;
  ASSERT_EQ(intern_filter(sh.store, filter_of(prims), &a), GSHSCENE_OK);
  EXPECT_EQ(gshscene_filter_required(a.get()),
      GSHSCENE_FILTER_NATIVE_REGION_AUTO | (1u << GSHSCENE_FILTER_OFFSET) | (1u << GSHSCENE_FILTER_GAUSSIAN_BLUR) |
          (1u << GSHSCENE_FILTER_FLOOD));
  // an explicit region and a duplicate edge
  prims[1].edge_mode = GSHSCENE_FILTER_EDGE_DUPLICATE;
  GSHSCENE_Filter f = filter_of(prims);
  f.region_mode = GSHSCENE_FILTER_REGION_EXPLICIT;
  f.region[2] = f.region[3] = 4;
  Ref b;
  ASSERT_EQ(intern_filter(sh.store, f, &b), GSHSCENE_OK);
  EXPECT_EQ(gshscene_filter_required(b.get()),
      GSHSCENE_FILTER_NATIVE_EDGE_DUPLICATE_WRAP | (1u << GSHSCENE_FILTER_OFFSET) | (1u << GSHSCENE_FILTER_GAUSSIAN_BLUR) |
          (1u << GSHSCENE_FILTER_FLOOD));
  EXPECT_EQ(gshscene_filter_required(nullptr), 0u);
  EXPECT_EQ((GSHSCENE_FILTER_NATIVE_ALL & ~(1u << 0)) & (1u << GSHSCENE_FILTER_SPECULAR_LIGHTING), 1u << 17);
  // the axis-aligned test
  GSHGEOM_Map m;
  gshgeom_map_identity(2, &m);
  EXPECT_TRUE(gshscene_filter_axis_aligned(&m));
  m.m[0][0] = 3;
  m.m[1][1] = -2;
  EXPECT_TRUE(gshscene_filter_axis_aligned(&m));
  m.m[0][0] = 0;
  m.m[1][1] = 0;
  m.m[0][1] = 1;
  m.m[1][0] = -1;
  EXPECT_TRUE(gshscene_filter_axis_aligned(&m)) << "a quarter turn";
  m.m[0][0] = 1;
  EXPECT_FALSE(gshscene_filter_axis_aligned(&m)) << "a shear";
  gshgeom_map_identity(2, &m);
  m.m[0][1] = 0.5;
  EXPECT_FALSE(gshscene_filter_axis_aligned(&m));
  gshgeom_map_identity(3, &m);
  EXPECT_FALSE(gshscene_filter_axis_aligned(&m));
  EXPECT_FALSE(gshscene_filter_axis_aligned(nullptr));
}

TEST(Filter, AGroupNamesAFilterAndTheListRetainsIt) {
  StoreHolder sh;
  StoreHolder other;
  std::vector<GSHSCENE_Filter_Primitive> prims = {blur(2, 2)};
  Ref a, foreign;
  ASSERT_EQ(intern_filter(sh.store, filter_of(prims), &a), GSHSCENE_OK);
  ASSERT_EQ(intern_filter(other.store, filter_of(prims), &foreign), GSHSCENE_OK);
  Ref path(intern_square(sh.store));
  GSHSCENE_Store_Stats before;
  gshscene_store_stats(sh.store, &before);
  {
    ListHolder l(sh.store);
    GSHSCENE_Group g;
    std::memset(&g, 0, sizeof g);
    g.opacity = 1.0;
    g.filter = a.get();
    ASSERT_EQ(gshscene_list_group_push(l.list, &g, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_group_pop(l.list), GSHSCENE_OK);
    const GSHSCENE_Item * item = nullptr;
    ASSERT_EQ(gshscene_list_item(l.list, 0, &item), GSHSCENE_OK);
    EXPECT_EQ(item->group.filter, a.get());
    // refused: another store's filter, a resource of the wrong kind, a non-isolated group, a knockout group
    GSHSCENE_Group bad = g;
    bad.filter = foreign.get();
    EXPECT_EQ(gshscene_list_group_push(l.list, &bad, 0, 0.0), GSHSCENE_ERR_INVALID);
    bad = g;
    bad.filter = path.get();
    EXPECT_EQ(gshscene_list_group_push(l.list, &bad, 0, 0.0), GSHSCENE_ERR_INVALID);
    bad = g;
    bad.isolation = GSHSCENE_ISOLATION_NON_ISOLATED;
    EXPECT_EQ(gshscene_list_group_push(l.list, &bad, 0, 0.0), GSHSCENE_ERR_INVALID);
    bad = g;
    bad.knockout = 1;
    EXPECT_EQ(gshscene_list_group_push(l.list, &bad, 0, 0.0), GSHSCENE_ERR_INVALID);
    // the list holds the filter: dropping the caller's reference leaves it alive
    a = Ref();
    GSHSCENE_Store_Stats during;
    gshscene_store_stats(sh.store, &during);
    EXPECT_EQ(during.live, before.live) << "the filter lives on in the list";
  }
}

TEST(Filter, AFilteredGroupHashesAndDumpsDifferentlyFromAPlainOne) {
  StoreHolder sh;
  std::vector<GSHSCENE_Filter_Primitive> p1 = {blur(2, 2)};
  std::vector<GSHSCENE_Filter_Primitive> p2 = {blur(3, 2)};
  Ref a, b;
  ASSERT_EQ(intern_filter(sh.store, filter_of(p1), &a), GSHSCENE_OK);
  ASSERT_EQ(intern_filter(sh.store, filter_of(p2), &b), GSHSCENE_OK);
  auto list_hash = [&](const GSHSCENE_Resource * filter, std::string * dump) {
    ListHolder l(sh.store);
    GSHSCENE_Group g;
    std::memset(&g, 0, sizeof g);
    g.opacity = 1.0;
    g.filter = filter;
    EXPECT_EQ(gshscene_list_group_push(l.list, &g, 0, 0.0), GSHSCENE_OK);
    EXPECT_EQ(gshscene_list_group_pop(l.list), GSHSCENE_OK);
    EXPECT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
    char * buf = nullptr;
    size_t size = 0;
    FILE * f = open_memstream(&buf, &size);
    EXPECT_EQ(gshscene_list_dump(l.list, f), GSHSCENE_OK);
    fclose(f);
    *dump = std::string(buf, size);
    free(buf);
    return gshscene_list_hash(l.list);
  };
  std::string plain_dump, a_dump, b_dump;
  const uint64_t plain = list_hash(nullptr, &plain_dump);
  const uint64_t ha = list_hash(a.get(), &a_dump);
  const uint64_t hb = list_hash(b.get(), &b_dump);
  EXPECT_NE(plain, ha);
  EXPECT_NE(ha, hb);
  EXPECT_EQ(plain_dump.find("filter="), std::string::npos) << "a default group's dump is as it was";
  char want[40];
  std::snprintf(want, sizeof want, " filter=%016llx", (unsigned long long)gshscene_resource_hash(a.get()));
  EXPECT_NE(a_dump.find(want), std::string::npos) << a_dump;
}

TEST(Filter, ACopyInAnotherStoreIsTheSameContent) {
  StoreHolder sh;
  StoreHolder other;
  std::vector<GSHSCENE_Filter_Primitive> prims = {flood(0.1, 0.2, 0.3, 0.4, 0.5), offset(1, 2), blur(0, 3)};
  Ref a;
  ASSERT_EQ(intern_filter(sh.store, filter_of(prims), &a), GSHSCENE_OK);
  const GSHSCENE_Resource * copy = nullptr;
  ASSERT_EQ(gshscene_store_copy_resource(other.store, a.get(), &copy), GSHSCENE_OK);
  Ref c(copy);
  EXPECT_EQ(gshscene_resource_hash(c.get()), gshscene_resource_hash(a.get()));
  EXPECT_EQ(dump_of(c.get()), dump_of(a.get()));
  Ref again;
  ASSERT_EQ(intern_filter(other.store, filter_of(prims), &again), GSHSCENE_OK);
  EXPECT_EQ(again.get(), c.get()) << "the copy is the interned handle";
}

TEST(Filter, AnAllocationFailureAtAnyPointLeavesNothingBehind) {
  std::vector<GSHSCENE_Filter_Primitive> prims = {flood(0, 0, 0, 1), offset(1, 2), blur(1, 1)};
  std::vector<double> table = {0, 1};
  GSHSCENE_Filter_Primitive ct = prim(GSHSCENE_FILTER_COMPONENT_TRANSFER);
  ct.input_count = 1;
  ct.inputs = &kPrev;
  ct.transfer[1].type = GSHSCENE_FILTER_TRANSFER_TABLE;
  ct.transfer[1].count = 2;
  ct.transfer[1].table = table.data();
  prims.push_back(ct);
  bool finished = false;
  long ooms = 0;
  for (long k = 0; k < 400 && !finished; k++) {
    tracker::Tracker t;
    GSHSCENE_Allocator alloc = tracker::allocator_for(&t);
    {
      StoreHolder sh(nullptr, &alloc);
      ASSERT_NE(sh.store, nullptr);
      t.fail_from = t.calls + k;
      Ref a;
      GSHSCENE_Result r = intern_filter(sh.store, filter_of(prims), &a);
      t.fail_from = -1;
      if (r == GSHSCENE_ERR_OOM) {
        ooms++;
      } else {
        ASSERT_EQ(r, GSHSCENE_OK);
        finished = true;
      }
    }
    EXPECT_TRUE(t.live.empty()) << "call " << k << " leaked";
  }
  EXPECT_TRUE(finished);
  EXPECT_GT(ooms, 0);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
