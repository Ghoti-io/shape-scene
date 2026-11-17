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
 * The linetype resource (stroke.h, LT1 to LT8): interning and the order of its refusals, the canonical form, equality, hash, dump,
 * a style that names one (its checks, the dashes its simple elements lower to, its required features, the refusal no expansion
 * overrides), and the copy into another store.
 */

#include "scene_helpers.h"

#include <cmath>
#include <string>
#include <vector>

using namespace scene;

namespace {

GSHSCENE_Linetype_Element dash(double len) {
  GSHSCENE_Linetype_Element e{};
  e.kind = GSHSCENE_LINETYPE_DASH;
  e.length = len;
  return e;
}
GSHSCENE_Linetype_Element space(double len) {
  GSHSCENE_Linetype_Element e{};
  e.kind = GSHSCENE_LINETYPE_SPACE;
  e.length = len;
  return e;
}
GSHSCENE_Linetype_Element dot() {
  GSHSCENE_Linetype_Element e{};
  e.kind = GSHSCENE_LINETYPE_DOT;
  return e;
}
GSHSCENE_Linetype_Element text(const char * t, const char * style = "STANDARD") {
  GSHSCENE_Linetype_Element e{};
  e.kind = GSHSCENE_LINETYPE_TEXT;
  e.text = t;
  e.style = style;
  e.scale = 0.1;
  e.rotation = 0.0;
  e.rotation_mode = GSHSCENE_LINETYPE_ROTATE_RELATIVE;
  e.offset_x = -0.1;
  e.offset_y = -0.05;
  return e;
}
GSHSCENE_Linetype_Element shape(const char * n, const char * file = "ltypeshp.shx") {
  GSHSCENE_Linetype_Element e = text(n, file);
  e.kind = GSHSCENE_LINETYPE_SHAPE;
  return e;
}

Ref intern(GSHSCENE_Store * s, const char * name, std::vector<GSHSCENE_Linetype_Element> els, const char * description = nullptr,
    GSHSCENE_Result * result = nullptr) {
  GSHSCENE_Linetype lt;
  lt.name = name;
  lt.description = description;
  lt.element_count = els.size();
  lt.elements = els.empty() ? nullptr : els.data();
  const GSHSCENE_Resource * r = nullptr;
  GSHSCENE_Result got = gshscene_store_intern_linetype(s, &lt, &r);
  if (result) {
    *result = got;
  }
  return Ref(got == GSHSCENE_OK ? r : nullptr);
}

GSHSCENE_Result intern_style(GSHSCENE_Store * s, const GSHSCENE_Resource * lt, Ref * out, double offset = 0.0) {
  GSHSCENE_Stroke_Style st;
  gshscene_stroke_style_init(&st);
  st.linetype = lt;
  st.dash_offset = offset;
  const GSHSCENE_Resource * r = nullptr;
  GSHSCENE_Result got = gshscene_store_intern_stroke_style(s, &st, &r);
  if (out) {
    *out = Ref(got == GSHSCENE_OK ? r : nullptr);
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

std::vector<double> dashes_of(const GSHSCENE_Resource * style, double * offset = nullptr) {
  GSHSCENE_Stroke_Style st;
  EXPECT_EQ(gshscene_resource_stroke_style(style, &st), GSHSCENE_OK);
  if (offset) {
    *offset = st.dash_offset;
  }
  return std::vector<double>(st.dashes, st.dashes + st.dash_count);
}

/** A store, a square to stroke and the helpers a disposition test needs. */
struct Kit {
  StoreHolder s;
  Ref square;
  Kit() : square(intern_square(s.store)) {}
};

GSHSCENE_Sink_Caps all_caps() {
  GSHSCENE_Sink_Caps c{};
  c.item_kinds = 0xfeu;
  c.composites = 0x0fffffffu;
  c.flags = GSHSCENE_CAP_CAN_BLOCK | GSHSCENE_CAP_CLIP_AA | GSHSCENE_CAP_BLEND_SPACES | GSHSCENE_CAP_GRADIENT_SPACES;
  c.gradient_kinds = 0xeu;
  c.stroke_native = GSHSCENE_STROKE_NATIVE_ALL;
  c.cancel_unit = GSHSCENE_CANCEL_BETWEEN_UNITS;
  c.work_measure = "units";
  return c;
}

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

TEST(Linetype, InternsReadsBackAndIsOneHandleForEqualContent) {
  StoreHolder sh;
  std::string name = "DASHDOT";
  std::string desc = "dash dot";
  Ref a = intern(sh.store, name.c_str(), {dash(0.5), space(0.25), dot(), space(0.25)}, desc.c_str());
  ASSERT_NE(a.get(), nullptr);
  EXPECT_EQ(gshscene_resource_kind(a.get()), GSHSCENE_RESOURCE_LINETYPE);
  EXPECT_STREQ(gshscene_resource_kind_string(GSHSCENE_RESOURCE_LINETYPE), "linetype");
  name[0] = 'X';
  desc[0] = 'X'; // the strings were copied
  GSHSCENE_Linetype back;
  ASSERT_EQ(gshscene_resource_linetype(a.get(), &back), GSHSCENE_OK);
  EXPECT_STREQ(back.name, "DASHDOT");
  EXPECT_STREQ(back.description, "dash dot");
  ASSERT_EQ(back.element_count, 4u);
  EXPECT_EQ(back.elements[0].kind, (uint32_t)GSHSCENE_LINETYPE_DASH);
  EXPECT_EQ(back.elements[0].length, 0.5);
  EXPECT_EQ(back.elements[2].kind, (uint32_t)GSHSCENE_LINETYPE_DOT);
  Ref b = intern(sh.store, "DASHDOT", {dash(0.5), space(0.25), dot(), space(0.25)}, "dash dot");
  EXPECT_EQ(a.get(), b.get());
  EXPECT_EQ(gshscene_resource_hash(a.get()), gshscene_resource_hash(b.get()));
  // no description reads back as none
  Ref c = intern(sh.store, "DASHDOT", {dash(0.5), space(0.25), dot(), space(0.25)});
  ASSERT_EQ(gshscene_resource_linetype(c.get(), &back), GSHSCENE_OK);
  EXPECT_EQ(back.description, nullptr);
  EXPECT_NE(a.get(), c.get());
  EXPECT_EQ(gshscene_resource_linetype(nullptr, &back), GSHSCENE_ERR_INVALID);
  GSHSCENE_Linetype dummy;
  Ref sq(nullptr);
  EXPECT_EQ(gshscene_resource_linetype(a.get(), nullptr), GSHSCENE_ERR_INVALID);
  (void)dummy;
}

TEST(Linetype, ATextAndAShapeElementAreCarriedWithTheirFields) {
  StoreHolder sh;
  GSHSCENE_Linetype_Element t = text("GAS", "Standard");
  t.rotation = 45.0;
  t.rotation_mode = GSHSCENE_LINETYPE_ROTATE_UPRIGHT;
  Ref a = intern(sh.store, "GAS_LINE", {dash(1.0), space(0.2), t, space(0.2), shape("BOX"), space(0.2)});
  ASSERT_NE(a.get(), nullptr);
  GSHSCENE_Linetype back;
  ASSERT_EQ(gshscene_resource_linetype(a.get(), &back), GSHSCENE_OK);
  ASSERT_EQ(back.element_count, 6u);
  EXPECT_STREQ(back.elements[2].text, "GAS");
  EXPECT_STREQ(back.elements[2].style, "Standard");
  EXPECT_EQ(back.elements[2].scale, 0.1);
  EXPECT_EQ(back.elements[2].rotation, 45.0);
  EXPECT_EQ(back.elements[2].rotation_mode, (uint32_t)GSHSCENE_LINETYPE_ROTATE_UPRIGHT);
  EXPECT_EQ(back.elements[2].offset_x, -0.1);
  EXPECT_EQ(back.elements[2].offset_y, -0.05);
  EXPECT_EQ(back.elements[4].kind, (uint32_t)GSHSCENE_LINETYPE_SHAPE);
  EXPECT_STREQ(back.elements[4].text, "BOX");
  EXPECT_STREQ(back.elements[4].style, "ltypeshp.shx");
}

TEST(Linetype, EveryFieldIsPartOfTheContent) {
  StoreHolder sh;
  GSHSCENE_Linetype_Element base = text("A");
  Ref ref = intern(sh.store, "N", {dash(1.0), space(1.0), base});
  ASSERT_NE(ref.get(), nullptr);
  std::vector<Ref> others;
  auto differs = [&](const char * name, std::vector<GSHSCENE_Linetype_Element> els, const char * desc = nullptr) {
    Ref r = intern(sh.store, name, els, desc);
    ASSERT_NE(r.get(), nullptr);
    EXPECT_NE(r.get(), ref.get());
    EXPECT_NE(gshscene_resource_hash(r.get()), gshscene_resource_hash(ref.get()));
    others.push_back(std::move(r));
  };
  differs("M", {dash(1.0), space(1.0), base});
  differs("N", {dash(1.0), space(1.0), base}, "d");
  differs("N", {dash(2.0), space(1.0), base});
  differs("N", {dash(1.0), space(2.0), base});
  GSHSCENE_Linetype_Element e = base;
  e.text = "B";
  differs("N", {dash(1.0), space(1.0), e});
  e = base;
  e.style = "S2";
  differs("N", {dash(1.0), space(1.0), e});
  e = base;
  e.scale = 0.2;
  differs("N", {dash(1.0), space(1.0), e});
  e = base;
  e.rotation = 1.0;
  differs("N", {dash(1.0), space(1.0), e});
  e = base;
  e.rotation_mode = GSHSCENE_LINETYPE_ROTATE_ABSOLUTE;
  differs("N", {dash(1.0), space(1.0), e});
  e = base;
  e.offset_x = 0.3;
  differs("N", {dash(1.0), space(1.0), e});
  e = base;
  e.offset_y = 0.3;
  differs("N", {dash(1.0), space(1.0), e});
  e = shape("A", "STANDARD");
  e.scale = base.scale;
  e.offset_x = base.offset_x;
  e.offset_y = base.offset_y;
  differs("N", {dash(1.0), space(1.0), e}); // a shape is not a text of the same fields
  differs("N", {dash(1.0), space(1.0)});
  differs("N", {dash(1.0), space(1.0), base, space(1.0)});
}

TEST(Linetype, MinusZeroIsPlusZeroAndTheDumpIsTheCanonicalText) {
  StoreHolder sh;
  GSHSCENE_Linetype_Element t = text("A");
  t.rotation = 0.0;
  GSHSCENE_Linetype_Element tn = t;
  tn.rotation = -0.0;
  Ref a = intern(sh.store, "N", {dash(1.0), t});
  Ref b = intern(sh.store, "N", {dash(1.0), tn});
  EXPECT_EQ(a.get(), b.get());
  {
    StoreHolder other;
    Ref only = intern(other.store, "N", {dash(1.0), tn});
    EXPECT_NE(dump_of(only.get()).find("text=A style=STANDARD scale=3fb999999999999a rotation=0000000000000000 mode="), std::string::npos) << "the stored -0 is +0";
  }
  std::string d = dump_of(a.get());
  EXPECT_NE(d.find("linetype name=N description= elements=2\n"), std::string::npos) << d;
  EXPECT_NE(d.find("e kind=0 length=3ff0000000000000 text= style= scale=0000000000000000"), std::string::npos) << d;
  EXPECT_NE(d.find("e kind=3 length=0000000000000000 text=A style=STANDARD scale=3fb999999999999a"), std::string::npos) << d;
}

TEST(Linetype, TheOrderOfRefusalsIsInvalidThenLimit) {
  StoreHolder sh;
  GSHSCENE_Result r;
  intern(sh.store, "N", {dash(1.0)}, nullptr, &r);
  EXPECT_EQ(r, GSHSCENE_OK);
  const struct {
    const char * what;
    const char * name;
    std::vector<GSHSCENE_Linetype_Element> els;
    const char * description = nullptr;
  } bad[] = {
      {"empty name", "", {dash(1.0)}},
      {"no elements", "N", {}},
      {"no dash", "N", {space(1.0)}},
      {"only a dot", "N", {dot()}},
      {"a zero dash", "N", {dash(0.0)}},
      {"a negative space", "N", {dash(1.0), space(-1.0)}},
      {"a dot with a length", "N", {dash(1.0), [] { auto e = dot(); e.length = 1.0; return e; }()}},
      {"a nan length", "N", {dash(NAN)}},
      {"an infinite offset", "N", {dash(1.0), [] { auto e = text("A"); e.offset_x = INFINITY; return e; }()}},
      {"a text without text", "N", {dash(1.0), [] { auto e = text("A"); e.text = nullptr; return e; }()}},
      {"a text with an empty string", "N", {dash(1.0), text("")}},
      {"a text with an empty style", "N", {dash(1.0), text("A", "")}},
      {"a text with a newline", "N", {dash(1.0), text("A\nB")}},
      {"a name with a control character", "N\t", {dash(1.0)}},
      {"a description with a control character", "N", {dash(1.0)}, "d\n"},
      {"a text of scale 0", "N", {dash(1.0), [] { auto e = text("A"); e.scale = 0.0; return e; }()}},
      {"a text of negative scale", "N", {dash(1.0), [] { auto e = text("A"); e.scale = -1.0; return e; }()}},
      {"a rotation mode out of range", "N", {dash(1.0), [] { auto e = text("A"); e.rotation_mode = 3; return e; }()}},
      {"a zero element of kind out of range", "N", {dash(1.0), [] { GSHSCENE_Linetype_Element e{}; e.kind = 5; return e; }()}},
      {"a kind out of range", "N", {dash(1.0), [] { auto e = dash(1.0); e.kind = 5; return e; }()}},
      {"a dash with text", "N", {[] { auto e = dash(1.0); e.text = "x"; return e; }()}},
      {"a dash with a scale", "N", {[] { auto e = dash(1.0); e.scale = 1.0; return e; }()}},
      {"a dash with an offset", "N", {[] { auto e = dash(1.0); e.offset_y = 1.0; return e; }()}},
      {"a dash with a rotation", "N", {[] { auto e = dash(1.0); e.rotation = 1.0; return e; }()}},
      {"a dash with a mode", "N", {[] { auto e = dash(1.0); e.rotation_mode = 1; return e; }()}},
  };
  for (const auto & c : bad) {
    intern(sh.store, c.name, c.els, c.description, &r);
    EXPECT_EQ(r, GSHSCENE_ERR_INVALID) << c.what;
  }
  GSHSCENE_Linetype lt{};
  const GSHSCENE_Resource * out = nullptr;
  EXPECT_EQ(gshscene_store_intern_linetype(nullptr, &lt, &out), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_store_intern_linetype(sh.store, nullptr, &out), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_store_intern_linetype(sh.store, &lt, nullptr), GSHSCENE_ERR_INVALID);
  // The bounds come after the validity checks.
  std::string longname(256, 'a');
  intern(sh.store, longname.c_str(), {dash(1.0)}, nullptr, &r);
  EXPECT_EQ(r, GSHSCENE_ERR_LIMIT);
  std::string ok255(255, 'a');
  intern(sh.store, ok255.c_str(), {dash(1.0)}, nullptr, &r);
  EXPECT_EQ(r, GSHSCENE_OK);
  intern(sh.store, "N", {dash(1.0)}, longname.c_str(), &r);
  EXPECT_EQ(r, GSHSCENE_ERR_LIMIT);
  intern(sh.store, "N", {dash(1.0), text(longname.c_str())}, nullptr, &r);
  EXPECT_EQ(r, GSHSCENE_ERR_LIMIT);
  intern(sh.store, "N", {dash(std::ldexp(1.0, 65))}, nullptr, &r);
  EXPECT_EQ(r, GSHSCENE_ERR_LIMIT);
  intern(sh.store, "N", {dash(std::ldexp(1.0, -65))}, nullptr, &r);
  EXPECT_EQ(r, GSHSCENE_ERR_LIMIT);
  intern(sh.store, "N", {dash(1.0), [] { auto e = text("A"); e.rotation = std::ldexp(1.0, 41); return e; }()}, nullptr, &r);
  EXPECT_EQ(r, GSHSCENE_ERR_LIMIT);
  intern(sh.store, "N", {dash(1.0), [] { auto e = text("A"); e.rotation = -std::ldexp(1.0, 41); return e; }()}, nullptr, &r);
  EXPECT_EQ(r, GSHSCENE_ERR_LIMIT);
  // An invalid element after a too-large one is still INVALID: validity first.
  intern(sh.store, "N", {dash(std::ldexp(1.0, 65)), space(-1.0)}, nullptr, &r);
  EXPECT_EQ(r, GSHSCENE_ERR_INVALID);
  GSHSCENE_Limits limits{};
  limits.max_resource_elements = 3;
  StoreHolder small(&limits);
  intern(small.store, "N", {dash(1.0), space(1.0), dash(1.0)}, nullptr, &r);
  EXPECT_EQ(r, GSHSCENE_OK);
  intern(small.store, "N", {dash(1.0), space(1.0), dash(1.0), space(1.0)}, nullptr, &r);
  EXPECT_EQ(r, GSHSCENE_ERR_LIMIT);
}

TEST(Linetype, AStyleNamingOneLowersItsSimpleElementsToDashes) {
  StoreHolder sh;
  const struct {
    const char * what;
    std::vector<GSHSCENE_Linetype_Element> els;
    std::vector<double> dashes;
    double offset;
  } cases[] = {
      {"a dash and a space", {dash(4.0), space(2.0)}, {4.0, 2.0}, 0.0},
      {"consecutive dashes merge", {dash(1.0), dash(2.0), space(2.0)}, {3.0, 2.0}, 0.0},
      {"a dot adds nothing to a dash", {dash(3.0), dot(), space(1.0)}, {3.0, 1.0}, 0.0},
      {"text and shapes occupy no length", {dash(3.0), space(1.0), text("A"), shape("B")}, {3.0, 1.0}, 0.0},
  };
  for (const auto & c : cases) {
    Ref lt = intern(sh.store, "LT", c.els);
    ASSERT_NE(lt.get(), nullptr) << c.what;
    Ref st;
    ASSERT_EQ(intern_style(sh.store, lt.get(), &st), GSHSCENE_OK) << c.what;
    double off = 1;
    std::vector<double> got = dashes_of(st.get(), &off);
    EXPECT_EQ(got, c.dashes) << c.what;
    EXPECT_EQ(off, c.offset) << c.what;
  }
  {
    Ref lt = intern(sh.store, "DOT", {dash(3.0), space(1.0), dot(), space(1.0)});
    Ref st;
    ASSERT_EQ(intern_style(sh.store, lt.get(), &st), GSHSCENE_OK);
    EXPECT_EQ(dashes_of(st.get()), (std::vector<double>{3.0, 1.0, 0.0, 1.0})) << "a dot is a zero-length dash between spaces";
  }
  // A leading space is the line's start inside the cycle: dashes starting pen-down and an offset.
  Ref lead = intern(sh.store, "LEAD", {space(1.0), dash(3.0), space(2.0)});
  Ref st;
  ASSERT_EQ(intern_style(sh.store, lead.get(), &st), GSHSCENE_OK);
  double off = 0;
  std::vector<double> got = dashes_of(st.get(), &off);
  ASSERT_EQ(got.size(), 2u);
  EXPECT_EQ(got[0], 3.0);
  EXPECT_EQ(got[1], 3.0) << "the trailing and leading spaces are one run";
  EXPECT_EQ(off, 5.0) << "the line starts 1 unit before the dash: phase 5 of 6";
}

TEST(Linetype, AStyleReadsTheLinetypeBackAndTwoStylesDifferByIt) {
  StoreHolder sh;
  Ref a = intern(sh.store, "A", {dash(4.0), space(2.0)});
  Ref b = intern(sh.store, "B", {dash(4.0), space(2.0)});
  Ref sa;
  Ref sb;
  ASSERT_EQ(intern_style(sh.store, a.get(), &sa), GSHSCENE_OK);
  ASSERT_EQ(intern_style(sh.store, b.get(), &sb), GSHSCENE_OK);
  EXPECT_NE(sa.get(), sb.get()) << "equal dashes, another linetype: another style";
  Ref sa2;
  ASSERT_EQ(intern_style(sh.store, a.get(), &sa2), GSHSCENE_OK);
  EXPECT_EQ(sa.get(), sa2.get());
  GSHSCENE_Stroke_Style back;
  ASSERT_EQ(gshscene_resource_stroke_style(sa.get(), &back), GSHSCENE_OK);
  EXPECT_EQ(back.linetype, a.get());
  // The style holds a reference: the linetype outlives the caller's.
  const GSHSCENE_Resource * raw = a.get();
  a = Ref();
  ASSERT_EQ(gshscene_resource_stroke_style(sa.get(), &back), GSHSCENE_OK);
  EXPECT_EQ(back.linetype, raw);
  GSHSCENE_Linetype lt;
  EXPECT_EQ(gshscene_resource_linetype(back.linetype, &lt), GSHSCENE_OK);
  // The dump of a style with one names it, and a style without one has no such line (its text is what it was).
  EXPECT_NE(dump_of(sa.get()).find("linetype "), std::string::npos);
  GSHSCENE_Stroke_Style plain;
  gshscene_stroke_style_init(&plain);
  const GSHSCENE_Resource * pr = nullptr;
  ASSERT_EQ(gshscene_store_intern_stroke_style(sh.store, &plain, &pr), GSHSCENE_OK);
  Ref plain_ref(pr);
  EXPECT_EQ(dump_of(plain_ref.get()).find("linetype"), std::string::npos);
}

TEST(Linetype, AStyleMayNotAlsoGiveDashesAnOffsetOrAScopeAndMustNameOneOfItsStore) {
  StoreHolder sh;
  StoreHolder other;
  Ref lt = intern(sh.store, "A", {dash(4.0), space(2.0)});
  Ref foreign = intern(other.store, "A", {dash(4.0), space(2.0)});
  GSHSCENE_Stroke_Style st;
  gshscene_stroke_style_init(&st);
  st.linetype = lt.get();
  const GSHSCENE_Resource * r = nullptr;
  double d[2] = {1.0, 1.0};
  st.dash_count = 2;
  st.dashes = d;
  EXPECT_EQ(gshscene_store_intern_stroke_style(sh.store, &st, &r), GSHSCENE_ERR_INVALID);
  st.dash_count = 0;
  st.dashes = nullptr;
  EXPECT_EQ(intern_style(sh.store, lt.get(), nullptr, 1.5), GSHSCENE_ERR_INVALID);
  st.dash_scope = GSHSCENE_STROKE_DASH_PATH;
  EXPECT_EQ(gshscene_store_intern_stroke_style(sh.store, &st, &r), GSHSCENE_ERR_INVALID);
  st.dash_scope = GSHSCENE_STROKE_DASH_PER_SUBPATH;
  st.linetype = foreign.get();
  EXPECT_EQ(gshscene_store_intern_stroke_style(sh.store, &st, &r), GSHSCENE_ERR_INVALID);
  Ref path_not_linetype;
  GSHSCENE_Width_Stop stops[1] = {{0.0, 1.0}};
  const GSHSCENE_Resource * wp = nullptr;
  ASSERT_EQ(gshscene_store_intern_width_profile(sh.store, stops, 1, &wp), GSHSCENE_OK);
  Ref profile(wp);
  st.linetype = profile.get();
  EXPECT_EQ(gshscene_store_intern_stroke_style(sh.store, &st, &r), GSHSCENE_ERR_INVALID);
}

TEST(Linetype, ASimpleOneRequiresDashAndAComplexOneAlsoTheLinetypeFeature) {
  Kit k;
  GSHSCENE_Paint black = solid(0, 0, 0, 1);
  GSHGEOM_Map id = translate(0, 0);
  Ref simple = intern(k.s.store, "S", {dash(4.0), space(2.0)});
  Ref complex_lt = intern(k.s.store, "C", {dash(4.0), space(1.0), text("X"), space(1.0)});
  Ref ss;
  Ref sc;
  ASSERT_EQ(intern_style(k.s.store, simple.get(), &ss), GSHSCENE_OK);
  ASSERT_EQ(intern_style(k.s.store, complex_lt.get(), &sc), GSHSCENE_OK);
  GSHSCENE_Item is = stroke_item(k, ss.get(), id, black);
  GSHSCENE_Item ic = stroke_item(k, sc.get(), id, black);
  const uint32_t base = GSHSCENE_STROKE_NATIVE_JOIN_MITER | GSHSCENE_STROKE_NATIVE_CAP_BUTT | GSHSCENE_STROKE_NATIVE_WIDTH_LOCAL |
      GSHSCENE_STROKE_NATIVE_DASH;
  EXPECT_EQ(gshscene_stroke_required(&is, nullptr, 0, nullptr), base);
  EXPECT_EQ(gshscene_stroke_required(&ic, nullptr, 0, nullptr), base | GSHSCENE_STROKE_NATIVE_LINETYPE);
  EXPECT_EQ(GSHSCENE_STROKE_NATIVE_LINETYPE, 1u << 17);
  EXPECT_EQ(GSHSCENE_STROKE_NATIVE_ALL, (1u << 18) - 1u);
}

TEST(Linetype, NoExpansionDrawsTheTextSoASinkThatExpandsStillRefusesItButDrawsASimpleOne) {
  Kit k;
  GSHSCENE_Paint black = solid(0, 0, 0, 1);
  GSHGEOM_Map id = translate(0, 0);
  GSHSCENE_Color_Space srgb = srgb_space();
  Ref simple = intern(k.s.store, "S", {dash(4.0), space(2.0)});
  Ref complex_lt = intern(k.s.store, "C", {dash(4.0), space(1.0), text("X"), space(1.0)});
  Ref ss;
  Ref sc;
  ASSERT_EQ(intern_style(k.s.store, simple.get(), &ss), GSHSCENE_OK);
  ASSERT_EQ(intern_style(k.s.store, complex_lt.get(), &sc), GSHSCENE_OK);
  GSHSCENE_Item is = stroke_item(k, ss.get(), id, black);
  GSHSCENE_Item ic = stroke_item(k, sc.get(), id, black);
  GSHSCENE_Sink_Caps caps = all_caps();
  EXPECT_EQ(gshscene_caps_disposition(&caps, &ic, nullptr, 0, nullptr, &srgb, nullptr).entry_class, 0) << "a sink that emits it";
  GSHSCENE_Sink_Caps lacks = caps;
  lacks.stroke_native &= ~GSHSCENE_STROKE_NATIVE_LINETYPE;
  GSHSCENE_Disposition d = gshscene_caps_disposition(&lacks, &ic, nullptr, 0, nullptr, &srgb, nullptr);
  EXPECT_EQ(d.entry_class, GSHSCENE_ENTRY_REFUSED);
  EXPECT_EQ(d.reason, GSHSCENE_REASON_STROKE_STYLE);
  EXPECT_EQ(d.detail, (uint64_t)GSHSCENE_STROKE_NATIVE_LINETYPE);
  lacks.flags |= GSHSCENE_CAP_STROKE_EXPAND;
  d = gshscene_caps_disposition(&lacks, &ic, nullptr, 0, nullptr, &srgb, nullptr);
  EXPECT_EQ(d.entry_class, GSHSCENE_ENTRY_REFUSED) << "STROKE_EXPAND cannot draw text";
  EXPECT_EQ(d.detail, (uint64_t)GSHSCENE_STROKE_NATIVE_LINETYPE);
  // A simple linetype needs only DASH, which an expanding sink draws.
  GSHSCENE_Sink_Caps none = lacks;
  none.stroke_native = 0;
  EXPECT_EQ(gshscene_caps_disposition(&none, &is, nullptr, 0, nullptr, &srgb, nullptr).entry_class, 0);
  GSHSCENE_Sink_Caps noexpand = caps;
  noexpand.stroke_native = 0;
  noexpand.flags &= ~GSHSCENE_CAP_STROKE_EXPAND;
  d = gshscene_caps_disposition(&noexpand, &ic, nullptr, 0, nullptr, &srgb, nullptr);
  EXPECT_EQ(d.entry_class, GSHSCENE_ENTRY_REFUSED);
  EXPECT_NE(d.detail & GSHSCENE_STROKE_NATIVE_LINETYPE, 0u);
  EXPECT_NE(d.detail & GSHSCENE_STROKE_NATIVE_DASH, 0u) << "without expansion every missing feature is named";
}

TEST(Linetype, ACopyIntoAnotherStoreCarriesTheLinetypeAndTheStyleThatNamesIt) {
  StoreHolder sh;
  StoreHolder dst;
  Ref lt = intern(sh.store, "C", {dash(4.0), space(1.0), text("X"), space(1.0)}, "with text");
  Ref st;
  ASSERT_EQ(intern_style(sh.store, lt.get(), &st), GSHSCENE_OK);
  const GSHSCENE_Resource * copy = nullptr;
  ASSERT_EQ(gshscene_store_copy_resource(dst.store, st.get(), &copy), GSHSCENE_OK);
  Ref copy_ref(copy);
  EXPECT_EQ(gshscene_resource_hash(copy), gshscene_resource_hash(st.get()));
  GSHSCENE_Stroke_Style back;
  ASSERT_EQ(gshscene_resource_stroke_style(copy, &back), GSHSCENE_OK);
  ASSERT_NE(back.linetype, nullptr);
  EXPECT_NE(back.linetype, lt.get());
  GSHSCENE_Linetype l;
  ASSERT_EQ(gshscene_resource_linetype(back.linetype, &l), GSHSCENE_OK);
  EXPECT_STREQ(l.name, "C");
  EXPECT_STREQ(l.description, "with text");
  EXPECT_EQ(dashes_of(copy), dashes_of(st.get()));
  const GSHSCENE_Resource * lc = nullptr;
  ASSERT_EQ(gshscene_store_copy_resource(dst.store, lt.get(), &lc), GSHSCENE_OK);
  Ref lc_ref(lc);
  EXPECT_EQ(lc, back.linetype) << "the copy of the linetype is the one the copied style names";
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

TEST(Linetype, AReadOfAnythingButALinetypeAndAStyleNamingAnythingButOneAreInvalid) {
  Kit k;
  GSHSCENE_Linetype out{};
  EXPECT_EQ(gshscene_resource_linetype(k.square.get(), &out), GSHSCENE_ERR_INVALID);
  GSHSCENE_Stroke_Style st;
  gshscene_stroke_style_init(&st);
  st.linetype = k.square.get();
  const GSHSCENE_Resource * r = nullptr;
  EXPECT_EQ(gshscene_store_intern_stroke_style(k.s.store, &st, &r), GSHSCENE_ERR_INVALID);
}

TEST(Linetype, TheAccountedBytesOfAnElementCountAgainstTheStoresLimit) {
  GSHSCENE_Limits probe{};
  probe.max_bytes = 100000;
  StoreHolder sh(&probe);
  GSHSCENE_Result r;
  std::vector<GSHSCENE_Linetype_Element> few{dash(1.0), space(1.0)};
  intern(sh.store, "A", few, nullptr, &r);
  ASSERT_EQ(r, GSHSCENE_OK);
  // Many elements of one linetype exceed a byte limit that its base cost alone would not.
  GSHSCENE_Limits tight{};
  tight.max_bytes = 600;
  StoreHolder small(&tight);
  std::vector<GSHSCENE_Linetype_Element> many;
  for (int i = 0; i < 200; i++) {
    many.push_back(i % 2 ? space(1.0) : dash(1.0));
  }
  intern(small.store, "A", many, nullptr, &r);
  EXPECT_EQ(r, GSHSCENE_ERR_LIMIT);
}

TEST(Linetype, StylesDifferByLinetypeEvenWhenTheirDashesAgree) {
  StoreHolder sh;
  Ref a = intern(sh.store, "A", {dash(1.0), space(1.0)});
  Ref b = intern(sh.store, "B", {dash(1.0), space(1.0)});
  Ref sa, sb;
  ASSERT_EQ(intern_style(sh.store, a.get(), &sa), GSHSCENE_OK);
  ASSERT_EQ(intern_style(sh.store, b.get(), &sb), GSHSCENE_OK);
  EXPECT_NE(sa.get(), sb.get());
}
