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
 * The hash and the dump of a version against the independent generator's
 * known answers, the dump parsed back to the hashed fields, what the hash is
 * and is not a function of, and every write of the dump checked (version.h,
 * CAP-13, AD-21; story 11).
 */

#include "scene_fixtures.h"

#include "../alloc_tracker.h"
#include "../known_answers.h"

#include <cstdio>
#include <map>
#include <set>
#include <sstream>

using namespace scene;

namespace {

/** The fixture of tools/gen-scene-vectors.py, built through the API in the order of its IDs. */
struct Fixture {
  SceneHolder s;
  Ref sq, p2, face, font_run, outline_run, palette;
  VersionRef v;
  Fixture() {
    sq.resource = intern_square(s.store());
    p2.resource = intern_fixture(s.store());
    GSHSCENE_Glyph_Run run;
    std::memset(&run, 0, sizeof run);
    GSHSCENE_Glyph fglyphs[3] = {{36, 0.0, 0.0, nullptr}, {37, 7.5, 0.25, nullptr}, {4294967295u, -1.0, 2.0, nullptr}};
    run.source = GSHSCENE_GLYPHS_FROM_FONT;
    face.resource = intern_face(s.store());
    run.face = face.get();
    run.size = 12.5;
    run.hint_mode = GSHSCENE_HINT_NONE;
    run.hint_version = 40;
    run.glyph_count = 3;
    run.glyphs = fglyphs;
    gshscene_store_intern_glyph_run(s.store(), &run, &font_run.resource);
    GSHSCENE_Glyph oglyphs[2] = {{1, 0.0, 0.0, sq.get()}, {2, 5.0, -3.0, p2.get()}};
    std::memset(&run, 0, sizeof run);
    run.source = GSHSCENE_GLYPHS_FROM_OUTLINES;
    run.glyph_count = 2;
    run.glyphs = oglyphs;
    gshscene_store_intern_glyph_run(s.store(), &run, &outline_run.resource);

    EditHolder e(s);
    GSHGEOM_Map design;
    gshgeom_map_design_to_model(2.5, 10, 20, &design);
    GSHSCENE_Key k42 = K(UINT64_C(0x5356470000000001), 42);
    GSHSCENE_Key k12 = K(1, 2);
    GSHSCENE_Node g2 = group_node(0.5, 3);
    g2.depth = -1.25;
    g2.blend_space = linear_srgb_space();
    g2.composite = GSHSCENE_COMPOSITE_XOR;
    g2.ordering = GSHSCENE_ORDER_DEPTH;
    g2.map = translate(1.5, -2.25);
    g2.clip = p2.get();
    g2.clip_map = design;
    g2.clip_rule = GSHGEOM_FILL_EVEN_ODD;
    add(e.edit, GSHSCENE_ROOT_ID, g2, &k42);                                    // 2
    GSHSCENE_Node s3 = shape_node(sq.get(), -1);
    s3.paint = solid(0.25, 0.5, 0.75, 1.0);
    s3.paint.space = space_of(GCOL_PRIMARIES_ADOBE_RGB, GCOL_TRANSFER_GAMMA, 2.19921875);
    s3.depth = 2.5;
    add(e.edit, 2, s3);                                                          // 3
    GSHSCENE_Node g4 = group_node();
    g4.flags = GSHSCENE_NODE_HIDDEN;
    add(e.edit, GSHSCENE_ROOT_ID, g4);                                           // 4
    GSHSCENE_Node s5 = shape_node(outline_run.get(), 5);
    s5.map = translate(1.5, -2.25);
    s5.fill_rule = GSHGEOM_FILL_EVEN_ODD;
    s5.paint = solid(2.0, -0.5, 0.0, 0.5);
    add(e.edit, 4, s5, &k12);                                                    // 5
    GSHSCENE_Node s6 = shape_node(font_run.get());
    s6.paint = solid(0.0, 0.0, 0.0, 0.25);
    s6.paint.kind = GSHSCENE_PAINT_FOREGROUND;
    add(e.edit, 4, s6);                                                          // 6
    GSHSCENE_Node i7 = instance_node(4, 9);
    i7.map = design;
    i7.depth = -4.5;
    add(e.edit, GSHSCENE_ROOT_ID, i7);                                           // 7
    GSHSCENE_Node s8 = shape_node(p2.get(), 2);
    s8.paint = solid(0.0, 0.0, 0.0, 1.0);
    s8.paint.kind = GSHSCENE_PAINT_PALETTE;
    s8.paint.entry = GSHSCENE_PALETTE_ENTRY_MAX;
    add(e.edit, GSHSCENE_ROOT_ID, s8);                                           // 8
    GSHSCENE_Color fg = {{1.0, 0.5, 0.25, 0.75}, space_of(GCOL_PRIMARIES_DISPLAY_P3, GCOL_TRANSFER_GAMMA, 2.2)};
    EXPECT_EQ(gshscene_edit_set_foreground(e.edit, &fg), GSHSCENE_OK);
    // The palette of tools/gen-scene-vectors.py (PALETTE).
    const GSHSCENE_Color entries[3] = {{{1.0, 0.5, 0.25, 1.0}, space_of(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_SRGB, 0.0)},
        {{0.0, 0.75, 1.0, 0.5}, space_of(GCOL_PRIMARIES_DISPLAY_P3, GCOL_TRANSFER_GAMMA, 2.2)},
        {{0.125, 0.0, 2.0, 0.25}, linear_srgb_space()}};
    EXPECT_EQ(gshscene_store_intern_palette(s.store(), 3, entries, &palette.resource), GSHSCENE_OK);
    EXPECT_EQ(gshscene_edit_set_palette(e.edit, palette.get()), GSHSCENE_OK);
    publish(s, e, &v);
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

}  // namespace

TEST(VersionHash, TheFixtureHashesAndDumpsAsTheIndependentGeneratorSays) {
  Fixture f;
  ASSERT_NE(f.v.get(), nullptr);
  EXPECT_EQ(gshscene_version_node_count(f.v.get()), 8u);
  EXPECT_EQ(gshscene_version_hash(f.v.get()), known::kSceneHash);
  EXPECT_EQ(dump_version(f.v.get()), known::kSceneDump);
}

TEST(VersionHash, EveryHashAndDigitRunWithALeadingZeroIsPrintedInFull) {
  // The generator searched for maps whose hashes, and a layer for which the version's hash, start with a zero nibble.
  SceneHolder s;
  Ref p2(intern_fixture(s.store()));
  EditHolder e(s);
  GSHSCENE_Node g = group_node(0.0);
  g.map = translate(known::kSceneBTranslate[0][0], known::kSceneBTranslate[0][1]);
  g.clip = p2.get();
  g.clip_map = translate(known::kSceneBTranslate[1][0], known::kSceneBTranslate[1][1]);
  uint64_t gid = add(e.edit, GSHSCENE_ROOT_ID, g);
  GSHSCENE_Node sh = shape_node(p2.get());
  sh.map = translate(known::kSceneBTranslate[2][0], known::kSceneBTranslate[2][1]);
  sh.paint = solid(0, 0, 0, 0);
  add(e.edit, GSHSCENE_ROOT_ID, sh);
  GSHSCENE_Node in = instance_node(gid, known::kSceneBLayer);
  in.map = translate(known::kSceneBTranslate[3][0], known::kSceneBTranslate[3][1]);
  add(e.edit, GSHSCENE_ROOT_ID, in);
  VersionRef v;
  publish(s, e, &v);
  EXPECT_EQ(gshscene_version_hash(v.get()), known::kSceneBHash);
  EXPECT_EQ(dump_version(v.get()), known::kSceneBDump);
}

TEST(VersionHash, TheEmptySceneHashesAndDumpsAsTheGeneratorSays) {
  SceneHolder s;
  VersionRef v;
  ASSERT_EQ(gshscene_scene_current(s.scene, &v.version), GSHSCENE_OK);
  EXPECT_EQ(gshscene_version_hash(v.get()), known::kEmptySceneHash);
  EXPECT_EQ(dump_version(v.get()), known::kEmptySceneDump);
}

TEST(VersionHash, InstancePathKeysAreTheGeneratorsToo) {
  EXPECT_EQ(gshscene_instance_path_key(nullptr, 0), known::kPathKeyEmpty);
  const uint64_t one[] = {1};
  const uint64_t two[] = {4, 5};
  const uint64_t three[] = {7, 7, 3};
  EXPECT_EQ(gshscene_instance_path_key(one, 1), known::kPathKeyOne);
  EXPECT_EQ(gshscene_instance_path_key(two, 2), known::kPathKeyTwo);
  EXPECT_EQ(gshscene_instance_path_key(three, 3), known::kPathKeyThree);
}

TEST(VersionHash, TheNullVersionHashesToTheOffsetBasis) {
  EXPECT_EQ(gshscene_version_hash(nullptr), GSHGEOM_FNV_OFFSET_BASIS);
}

TEST(VersionHash, IsNotAFunctionOfTheVersionNumberOrTheHistory) {
  // The same content reached in one publish and in three: the same hash, the serial differing.
  uint64_t one_shot, in_steps;
  {
    SceneHolder s;
    Ref sq(intern_square(s.store()));
    EditHolder e(s);
    add(e.edit, GSHSCENE_ROOT_ID, shape_node(sq.get(), 1));
    add(e.edit, GSHSCENE_ROOT_ID, shape_node(sq.get(), 2));
    VersionRef v;
    publish(s, e, &v);
    one_shot = gshscene_version_hash(v.get());
    EXPECT_EQ(gshscene_version_serial(v.get()), 1u);
  }
  {
    SceneHolder s;
    Ref sq(intern_square(s.store()));
    VersionRef v;
    for (int i = 1; i <= 2; i++) {
      EditHolder e(s);
      add(e.edit, GSHSCENE_ROOT_ID, shape_node(sq.get(), i));
      publish(s, e, &v);
    }
    EXPECT_EQ(gshscene_version_serial(v.get()), 2u);
    in_steps = gshscene_version_hash(v.get());
  }
  EXPECT_EQ(one_shot, in_steps);
}

TEST(VersionHash, EveryFieldOfEveryNodeKindChangesIt) {
  // A base scene, and the hash after changing each field in turn: all different.
  struct Variant {
    const char * name;
    std::function<void(Fixture &, GSHSCENE_Edit *)> change;
  };
  auto set_group2 = [](Fixture & f, GSHSCENE_Edit * e, std::function<void(GSHSCENE_Node &)> fn) {
    GSHSCENE_Node_Info info;
    ASSERT_EQ(gshscene_edit_node(e, 2, &info), GSHSCENE_OK);
    GSHSCENE_Node n = info.node;
    fn(n);
    ASSERT_EQ(gshscene_edit_set(e, 2, &n), GSHSCENE_OK) << "variant";
    (void)f;
  };
  auto set_node = [](GSHSCENE_Edit * e, uint64_t id, std::function<void(GSHSCENE_Node &)> fn) {
    GSHSCENE_Node_Info info;
    ASSERT_EQ(gshscene_edit_node(e, id, &info), GSHSCENE_OK);
    GSHSCENE_Node n = info.node;
    fn(n);
    ASSERT_EQ(gshscene_edit_set(e, id, &n), GSHSCENE_OK);
  };
  std::vector<Variant> variants = {
      {"base", [](Fixture &, GSHSCENE_Edit *) {}},
      {"group map", [&](Fixture & f, GSHSCENE_Edit * e) { set_group2(f, e, [](GSHSCENE_Node & n) { n.map = translate(9, 9); }); }},
      {"group opacity", [&](Fixture & f, GSHSCENE_Edit * e) { set_group2(f, e, [](GSHSCENE_Node & n) { n.opacity = 0.25; }); }},
      {"group layer", [&](Fixture & f, GSHSCENE_Edit * e) { set_group2(f, e, [](GSHSCENE_Node & n) { n.layer = 4; }); }},
      {"group depth", [&](Fixture & f, GSHSCENE_Edit * e) { set_group2(f, e, [](GSHSCENE_Node & n) { n.depth = 1.5; }); }},
      {"group blend primaries", [&](Fixture & f, GSHSCENE_Edit * e) { set_group2(f, e, [](GSHSCENE_Node & n) { n.blend_space.primaries = GCOL_PRIMARIES_BT2020; }); }},
      {"group blend transfer", [&](Fixture & f, GSHSCENE_Edit * e) { set_group2(f, e, [](GSHSCENE_Node & n) { n.blend_space.transfer = GCOL_TRANSFER_SRGB; }); }},
      {"group blend gamma", [&](Fixture & f, GSHSCENE_Edit * e) { set_group2(f, e, [](GSHSCENE_Node & n) { n.blend_space = space_of(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_GAMMA, 2.4); }); }},
      {"group target blend", [&](Fixture & f, GSHSCENE_Edit * e) { set_group2(f, e, [](GSHSCENE_Node & n) { n.blend_space = GSHSCENE_Color_Space{}; }); }},
      {"group composite", [&](Fixture & f, GSHSCENE_Edit * e) { set_group2(f, e, [](GSHSCENE_Node & n) { n.composite = GSHSCENE_COMPOSITE_PLUS; }); }},
      {"group ordering", [&](Fixture & f, GSHSCENE_Edit * e) { set_group2(f, e, [](GSHSCENE_Node & n) { n.ordering = GSHSCENE_ORDER_OVERLAY; }); }},
      {"group flat", [&](Fixture & f, GSHSCENE_Edit * e) { set_group2(f, e, [](GSHSCENE_Node & n) { n.ordering = GSHSCENE_ORDER_FLAT; }); }},
      {"group flags", [&](Fixture & f, GSHSCENE_Edit * e) { set_group2(f, e, [](GSHSCENE_Node & n) { n.flags = GSHSCENE_NODE_HIDDEN; }); }},
      {"clip resource", [&](Fixture & f, GSHSCENE_Edit * e) { set_group2(f, e, [&](GSHSCENE_Node & n) { n.clip = f.sq.get(); }); }},
      {"clip map", [&](Fixture & f, GSHSCENE_Edit * e) { set_group2(f, e, [](GSHSCENE_Node & n) { n.clip_map = translate(1, 1); }); }},
      {"clip rule", [&](Fixture & f, GSHSCENE_Edit * e) { set_group2(f, e, [](GSHSCENE_Node & n) { n.clip_rule = GSHGEOM_FILL_NONZERO; }); }},
      {"no clip", [&](Fixture & f, GSHSCENE_Edit * e) { set_group2(f, e, [](GSHSCENE_Node & n) { n.clip = nullptr; }); }},
      {"shape resource", [&](Fixture & f, GSHSCENE_Edit * e) { set_node(e, 3, [&](GSHSCENE_Node & n) { n.resource = f.p2.get(); }); }},
      {"shape map", [&](Fixture &, GSHSCENE_Edit * e) { set_node(e, 3, [](GSHSCENE_Node & n) { n.map = translate(1, 0); }); }},
      {"shape rule", [&](Fixture &, GSHSCENE_Edit * e) { set_node(e, 3, [](GSHSCENE_Node & n) { n.fill_rule = GSHGEOM_FILL_EVEN_ODD; }); }},
      {"shape red", [&](Fixture &, GSHSCENE_Edit * e) { set_node(e, 3, [](GSHSCENE_Node & n) { n.paint.rgba[0] = 0.5; }); }},
      {"shape green", [&](Fixture &, GSHSCENE_Edit * e) { set_node(e, 3, [](GSHSCENE_Node & n) { n.paint.rgba[1] = 0.25; }); }},
      {"shape blue", [&](Fixture &, GSHSCENE_Edit * e) { set_node(e, 3, [](GSHSCENE_Node & n) { n.paint.rgba[2] = 0.125; }); }},
      {"shape alpha", [&](Fixture &, GSHSCENE_Edit * e) { set_node(e, 3, [](GSHSCENE_Node & n) { n.paint.rgba[3] = 0.5; }); }},
      {"shape layer", [&](Fixture &, GSHSCENE_Edit * e) { set_node(e, 3, [](GSHSCENE_Node & n) { n.layer = 77; }); }},
      {"shape depth", [&](Fixture &, GSHSCENE_Edit * e) { set_node(e, 3, [](GSHSCENE_Node & n) { n.depth = -2.5; }); }},
      {"shape paint primaries", [&](Fixture &, GSHSCENE_Edit * e) { set_node(e, 3, [](GSHSCENE_Node & n) { n.paint.space.primaries = GCOL_PRIMARIES_SRGB; }); }},
      {"shape paint transfer", [&](Fixture &, GSHSCENE_Edit * e) { set_node(e, 3, [](GSHSCENE_Node & n) { n.paint.space.transfer = GCOL_TRANSFER_LINEAR; n.paint.space.gamma = 0.0; }); }},
      {"shape paint gamma", [&](Fixture &, GSHSCENE_Edit * e) { set_node(e, 3, [](GSHSCENE_Node & n) { n.paint.space.gamma = 2.2; }); }},
      {"shape paint kind", [&](Fixture &, GSHSCENE_Edit * e) { set_node(e, 3, [](GSHSCENE_Node & n) { n.paint = solid(0, 0, 0, 0.25); n.paint.kind = GSHSCENE_PAINT_FOREGROUND; }); }},
      {"foreground alpha multiplier", [&](Fixture &, GSHSCENE_Edit * e) { set_node(e, 6, [](GSHSCENE_Node & n) { n.paint.rgba[3] = 0.5; }); }},
      {"shape layer sign", [&](Fixture &, GSHSCENE_Edit * e) { set_node(e, 3, [](GSHSCENE_Node & n) { n.layer = 1; }); }},
      {"shape hidden", [&](Fixture &, GSHSCENE_Edit * e) { set_node(e, 3, [](GSHSCENE_Node & n) { n.flags = GSHSCENE_NODE_HIDDEN; }); }},
      {"instance target", [&](Fixture &, GSHSCENE_Edit * e) { set_node(e, 7, [](GSHSCENE_Node & n) { n.target = 2; }); }},
      {"instance map", [&](Fixture &, GSHSCENE_Edit * e) { set_node(e, 7, [](GSHSCENE_Node & n) { n.map = translate(2, 2); }); }},
      {"instance layer", [&](Fixture &, GSHSCENE_Edit * e) { set_node(e, 7, [](GSHSCENE_Node & n) { n.layer = 10; }); }},
      {"instance depth", [&](Fixture &, GSHSCENE_Edit * e) { set_node(e, 7, [](GSHSCENE_Node & n) { n.depth = 4.5; }); }},
      {"foreground red", [&](Fixture &, GSHSCENE_Edit * e) { GSHSCENE_Color c; gshscene_edit_foreground(e, &c); c.rgba[0] = 0.0; gshscene_edit_set_foreground(e, &c); }},
      {"foreground green", [&](Fixture &, GSHSCENE_Edit * e) { GSHSCENE_Color c; gshscene_edit_foreground(e, &c); c.rgba[1] = 0.0; gshscene_edit_set_foreground(e, &c); }},
      {"foreground blue", [&](Fixture &, GSHSCENE_Edit * e) { GSHSCENE_Color c; gshscene_edit_foreground(e, &c); c.rgba[2] = 0.0; gshscene_edit_set_foreground(e, &c); }},
      {"foreground alpha", [&](Fixture &, GSHSCENE_Edit * e) { GSHSCENE_Color c; gshscene_edit_foreground(e, &c); c.rgba[3] = 0.5; gshscene_edit_set_foreground(e, &c); }},
      {"foreground space", [&](Fixture &, GSHSCENE_Edit * e) { GSHSCENE_Color c; gshscene_edit_foreground(e, &c); c.space = GSHSCENE_Color_Space{}; gshscene_edit_set_foreground(e, &c); }},
      {"foreground gamma", [&](Fixture &, GSHSCENE_Edit * e) { GSHSCENE_Color c; gshscene_edit_foreground(e, &c); c.space.gamma = 2.4; gshscene_edit_set_foreground(e, &c); }},
      {"unhide", [&](Fixture &, GSHSCENE_Edit * e) { set_node(e, 4, [](GSHSCENE_Node & n) { n.flags = 0; }); }},
      {"reorder children", [&](Fixture &, GSHSCENE_Edit * e) { ASSERT_EQ(gshscene_edit_move(e, 4, GSHSCENE_ROOT_ID, 0), GSHSCENE_OK); }},
      {"move node", [&](Fixture &, GSHSCENE_Edit * e) { ASSERT_EQ(gshscene_edit_move(e, 3, 4, 0), GSHSCENE_OK); }},
      {"remove node", [&](Fixture &, GSHSCENE_Edit * e) { ASSERT_EQ(gshscene_edit_remove(e, 6), GSHSCENE_OK); }},
      {"remove key", [&](Fixture &, GSHSCENE_Edit * e) { GSHSCENE_Key k = K(1, 2); ASSERT_EQ(gshscene_edit_remove_key(e, &k), GSHSCENE_OK); }},
      {"add node", [&](Fixture & f, GSHSCENE_Edit * e) { add(e, GSHSCENE_ROOT_ID, shape_node(f.sq.get())); }},
  };
  std::map<uint64_t, std::string> seen;
  for (const Variant & variant : variants) {
    Fixture f;
    EditHolder e(f.s);
    variant.change(f, e.edit);
    VersionRef v;
    const GSHSCENE_Version * out = nullptr;
    ASSERT_EQ(gshscene_scene_publish(f.s.scene, e.edit, nullptr, &out), GSHSCENE_OK) << variant.name;
    v.reset(out);
    uint64_t h = gshscene_version_hash(v.get());
    auto inserted = seen.insert({h, variant.name});
    EXPECT_TRUE(inserted.second) << variant.name << " has the same hash as " << inserted.first->second;
  }
  EXPECT_EQ(seen.size(), variants.size());
}

// --- the dump parses back to the hashed fields -------------------------------------------------------------------

namespace {

std::map<std::string, std::string> fields_of(const std::string & line, size_t from_token) {
  std::map<std::string, std::string> out;
  std::istringstream in(line);
  std::string token;
  size_t n = 0;
  while (in >> token) {
    if (n++ < from_token) {
      continue;
    }
    size_t eq = token.find('=');
    if (eq != std::string::npos) {
      out[token.substr(0, eq)] = token.substr(eq + 1);
    }
  }
  return out;
}

uint64_t hex(const std::string & s) {
  return std::stoull(s, nullptr, 16);
}

double dbl(const std::string & s) {
  uint64_t bits = hex(s);
  double d;
  std::memcpy(&d, &bits, sizeof d);
  return d;
}

/** A space written `P:T:<gamma hex>` into the hash: u8, u8, f64. */
void hash_space(GSHGEOM_Hash * h, const std::string & text) {
  size_t a = text.find(':');
  size_t b = text.find(':', a + 1);
  gshgeom_hash_u8(h, (uint8_t)std::stoul(text.substr(0, a)));
  gshgeom_hash_u8(h, (uint8_t)std::stoul(text.substr(a + 1, b - a - 1)));
  gshgeom_hash_f64(h, dbl(text.substr(b + 1)));
}

}  // namespace

TEST(VersionDump, ParsesBackToTheFieldsTheHashIsOver) {
  Fixture f;
  std::vector<std::string> lines = lines_of(dump_version(f.v.get()));
  ASSERT_GE(lines.size(), 2u);
  auto header = fields_of(lines[0], 2);
  ASSERT_EQ(lines[0].substr(0, 18), "gshscene.version 4");
  EXPECT_EQ(std::stoull(header["nodes"]), lines.size() - 1);
  EXPECT_EQ(header["root"], "1");
  GSHGEOM_Hash h;
  gshgeom_hash_init(&h);
  gshgeom_hash_u32(&h, 4);
  gshgeom_hash_u32(&h, 0x4e454353u);
  {
    // The foreground: four bare doubles and a space after `fg=`.
    std::istringstream in(lines[0]);
    std::string tok;
    std::vector<std::string> fgtok;
    bool after = false;
    while (in >> tok) {
      if (tok.rfind("fg=", 0) == 0) {
        after = true;
        fgtok.push_back(tok.substr(3));
      } else if (after && tok.rfind("hash=", 0) != 0 && tok.rfind("palette=", 0) != 0) {
        fgtok.push_back(tok);
      }
    }
    ASSERT_EQ(fgtok.size(), 5u);
    for (int k = 0; k < 4; k++) {
      gshgeom_hash_f64(&h, dbl(fgtok[k]));
    }
    hash_space(&h, fgtok[4]);
  }
  gshgeom_hash_u64(&h, hex(header["palette"]));
  gshgeom_hash_u64(&h, lines.size() - 1);
  uint64_t last_id = 0;
  for (size_t i = 1; i < lines.size(); i++) {
    const std::string & line = lines[i];
    auto fld = fields_of(line, 1);
    uint64_t id = std::stoull(fld["id"]);
    EXPECT_GT(id, last_id) << "ascending IDs";
    last_id = id;
    uint8_t kind = line[0] == 'G' ? 1 : line[0] == 'S' ? 2 : 3;
    gshgeom_hash_u64(&h, id);
    gshgeom_hash_u8(&h, kind);
    gshgeom_hash_u8(&h, (uint8_t)std::stoul(fld["flags"]));
    gshgeom_hash_u64(&h, std::stoull(fld["parent"]));
    if (fld["key"] == "-") {
      gshgeom_hash_u8(&h, 0);
    } else {
      gshgeom_hash_u8(&h, 1);
      gshgeom_hash_u64(&h, hex(fld["key"].substr(0, 16)));
      gshgeom_hash_u64(&h, hex(fld["key"].substr(17, 16)));
    }
    if (kind == 1) {
      gshgeom_hash_u64(&h, hex(fld["map"]));
      gshgeom_hash_f64(&h, dbl(fld["opacity"]));
      hash_space(&h, fld["blend"]);
      gshgeom_hash_u8(&h, (uint8_t)std::stoul(fld["composite"]));
      gshgeom_hash_u32(&h, (uint32_t)std::stoul(fld["order"]));
      gshgeom_hash_i32(&h, (int32_t)std::stol(fld["layer"]));
      gshgeom_hash_f64(&h, dbl(fld["depth"]));
      if (fld["clip"] == "-") {
        gshgeom_hash_u8(&h, 0);
      } else {
        gshgeom_hash_u8(&h, 1);
        const std::string & c = fld["clip"];
        size_t a = c.find(':');
        size_t b = c.find(':', a + 1);
        size_t d = c.find(':', b + 1);
        gshgeom_hash_u64(&h, hex(c.substr(a + 1, b - a - 1)));
        gshgeom_hash_u64(&h, hex(c.substr(b + 1, d - b - 1)));
        gshgeom_hash_u8(&h, (uint8_t)std::stoul(c.substr(d + 1)));
      }
      std::vector<uint64_t> kids;
      if (fld["children"] != "-") {
        std::istringstream in(fld["children"]);
        std::string tok;
        while (std::getline(in, tok, ',')) {
          kids.push_back(std::stoull(tok));
        }
      }
      gshgeom_hash_u64(&h, kids.size());
      for (uint64_t k : kids) {
        gshgeom_hash_u64(&h, k);
      }
    } else if (kind == 2) {
      const std::string & r = fld["res"];
      gshgeom_hash_u64(&h, hex(r.substr(r.find(':') + 1)));
      gshgeom_hash_u64(&h, hex(fld["map"]));
      gshgeom_hash_u8(&h, (uint8_t)std::stoul(fld["rule"]));
      gshgeom_hash_u8(&h, (uint8_t)std::stoul(fld["paint"]));
      // The four colour components follow `paint=K` as bare tokens.
      std::istringstream in(line);
      std::string tok;
      double rgba[4];
      int seen = -1;
      while (in >> tok) {
        if (tok.rfind("paint=", 0) == 0) {
          seen = 0;
        } else if (seen >= 0 && seen < 4) {
          rgba[seen++] = dbl(tok);
        }
      }
      ASSERT_EQ(seen, 4);
      for (int k = 0; k < 4; k++) {
        gshgeom_hash_f64(&h, rgba[k]);
      }
      hash_space(&h, fld["space"]);
      gshgeom_hash_u32(&h, (uint32_t)std::stoul(fld["entry"]));
      gshgeom_hash_u64(&h, hex(fld["gradient"]));
      gshgeom_hash_i32(&h, (int32_t)std::stol(fld["layer"]));
      gshgeom_hash_f64(&h, dbl(fld["depth"]));
    } else {
      gshgeom_hash_u64(&h, std::stoull(fld["target"]));
      gshgeom_hash_u64(&h, hex(fld["map"]));
      gshgeom_hash_i32(&h, (int32_t)std::stol(fld["layer"]));
      gshgeom_hash_f64(&h, dbl(fld["depth"]));
    }
  }
  EXPECT_EQ(gshgeom_hash_value(&h), hex(header["hash"]));
  EXPECT_EQ(hex(header["hash"]), gshscene_version_hash(f.v.get()));
}

TEST(VersionDump, RejectsNullAndFailsOnAFailingSink) {
  Fixture f;
  EXPECT_EQ(gshscene_version_dump(nullptr, stdout), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_version_dump(f.v.get(), nullptr), GSHSCENE_ERR_INVALID);
  FILE * full = std::fopen("/dev/full", "w");
  if (full) {
    EXPECT_EQ(gshscene_version_dump(f.v.get(), full), GSHSCENE_ERR_IO);
    std::fclose(full);
  }
  FILE * ro = std::fopen("/dev/null", "r");
  ASSERT_NE(ro, nullptr);
  EXPECT_EQ(gshscene_version_dump(f.v.get(), ro), GSHSCENE_ERR_IO);
  std::fclose(ro);
}

#ifdef __GLIBC__
namespace {

struct Cookie {
  long fail_at;
  long writes;
};

ssize_t sink_write(void * cookie, const char *, size_t size) {
  Cookie * c = (Cookie *)cookie;
  long n = c->writes++;
  return n == c->fail_at ? -1 : (ssize_t)size;
}

GSHSCENE_Result dump_failing_at(const GSHSCENE_Version * v, long fail_at, long * writes, bool unbuffered) {
  Cookie cookie{fail_at, 0};
  cookie_io_functions_t functions = {nullptr, sink_write, nullptr, nullptr};
  FILE * f = fopencookie(&cookie, "w", functions);
  if (!f) {
    return GSHSCENE_ERR_INTERNAL;
  }
  if (unbuffered) {
    std::setvbuf(f, nullptr, _IONBF, 0);
  }
  GSHSCENE_Result result = gshscene_version_dump(v, f);
  std::fclose(f);
  *writes = cookie.writes;
  return result;
}

}  // namespace

TEST(VersionDump, EveryWriteIsChecked) {
  Fixture f;
  for (bool unbuffered : {true, false}) {
    long total = 0;
    ASSERT_EQ(dump_failing_at(f.v.get(), -1, &total, unbuffered), GSHSCENE_OK);
    ASSERT_GT(total, 0);
    for (long k = 0; k < total; k++) {
      long writes = 0;
      EXPECT_EQ(dump_failing_at(f.v.get(), k, &writes, unbuffered), GSHSCENE_ERR_IO)
          << (unbuffered ? "unbuffered" : "buffered") << ": write " << k << " of " << total << " failed unnoticed";
    }
    long writes = 0;
    EXPECT_EQ(dump_failing_at(f.v.get(), total, &writes, unbuffered), GSHSCENE_OK);
  }
}
#endif

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
