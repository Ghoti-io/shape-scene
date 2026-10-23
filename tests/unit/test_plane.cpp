/**
 * @file
 *
 * The plane, the design-space rectangle clip and the group in a stated space (color-paint story P4; PL1 to PL10 of
 * resource.h and documentation/design.md): the plane resource (one value, its hash and dump, where a call may name it),
 * the test sink's record and stamp of a plane fill (no box, no extent limit, the clip region only, inside a group, under
 * a damage), and the float-free calls of fixed.h (the fills of the plane, the rectangle clip placed in a run's design
 * space, the group's blend space).
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "test_helpers.h"

#include "../glyph_test_provider.h"
#include "../scene_fixtures.h"
#include "../scene_helpers.h"

#include <ghoti.io/shape-scene/fixed.h>

#include <cstring>
#include <string>
#include <vector>

using namespace scene;

namespace {

const GSHSCENE_Resource * plane_of(GSHSCENE_Store * store) {
  const GSHSCENE_Resource * r = nullptr;
  return gshscene_store_intern_plane(store, &r) == GSHSCENE_OK ? r : nullptr;
}

GSHSCENE_Paint foreground(double alpha = 1.0) {
  GSHSCENE_Paint p{};
  p.kind = GSHSCENE_PAINT_FOREGROUND;
  p.rgba[3] = alpha;
  return p;
}

/** The hash of PL1's stream, restated: the format, then the kind. */
uint64_t restated_plane_hash() {
  GSHGEOM_Hash h;
  gshgeom_hash_init(&h);
  gshgeom_hash_u32(&h, 1u);
  gshgeom_hash_u32(&h, 9u);
  return gshgeom_hash_value(&h);
}

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
    gimg_raster_create(64, 48, &format, GIMG_RASTER_OWNED, nullptr, 0, &raster);
  }
  ~Render() {
    gshscene_sink_destroy(sink);
    gshscene_report_destroy(report);
    gimg_raster_destroy(raster);
  }
  GSHSCENE_Result run(const GSHSCENE_List * list, const GSHSCENE_Rect * damage = nullptr) {
    dump.clear();
    GSHSCENE_Target target{};
    target.kind = GSHSCENE_TARGET_RASTER;
    target.surface = raster;
    target.damage = damage;
    GSHSCENE_Render_Request rq{};
    rq.list = list;
    rq.target = &target;
    rq.report = report;
    return gshscene_sink_render(sink, &rq);
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

/** The first line of @p text that starts with @p prefix, or the empty string. */
std::string line_with(const std::string & text, const std::string & prefix) {
  size_t at = text.find(prefix);
  if (at == std::string::npos) {
    return "";
  }
  size_t end = text.find('\n', at);
  return text.substr(at, end == std::string::npos ? std::string::npos : end - at);
}

}  // namespace

// --- PL1: the resource ---------------------------------------------------------------------------------------------

TEST(Plane, IsOneResourceWithTheFormatAndKindForItsHash) {
  EXPECT_EQ((int)GSHSCENE_RESOURCE_PLANE, 9);
  EXPECT_EQ((int)GSHSCENE_RESOURCE_KIND_COUNT, 10);
  EXPECT_STREQ(gshscene_resource_kind_string(GSHSCENE_RESOURCE_PLANE), "plane");
  StoreHolder a;
  StoreHolder b;
  Ref p1(plane_of(a.store));
  Ref p2(plane_of(a.store));
  Ref other(plane_of(b.store));
  ASSERT_NE(p1.get(), nullptr);
  EXPECT_EQ(p1.get(), p2.get()) << "equal content, one handle (AD-27)";
  EXPECT_NE(p1.get(), other.get());
  EXPECT_EQ(gshscene_resource_kind(p1.get()), GSHSCENE_RESOURCE_PLANE);
  EXPECT_EQ(gshscene_resource_hash(p1.get()), restated_plane_hash());
  EXPECT_EQ(gshscene_resource_hash(other.get()), restated_plane_hash()) << "the hash is of the content, not the store";
  char want[96];
  std::snprintf(want, sizeof want, "gshscene.resource 1 kind=plane hash=%016llx\n",
      (unsigned long long)restated_plane_hash());
  EXPECT_EQ(dump_resource(p1.get()), std::string(want)) << "the header line is the whole dump";
  EXPECT_EQ(gshscene_resource_bytes(p1.get()), GSHSCENE_BYTES_BASE);
  // A different kind of the same store never hashes as it does.
  Ref square(intern_square(a.store));
  EXPECT_NE(gshscene_resource_hash(square.get()), gshscene_resource_hash(p1.get()));
  const GSHSCENE_Resource * none = reinterpret_cast<const GSHSCENE_Resource *>(0x1);
  EXPECT_EQ(gshscene_store_intern_plane(nullptr, &none), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_store_intern_plane(a.store, nullptr), GSHSCENE_ERR_INVALID);
}

TEST(Plane, CountsAgainstTheStoresLimits) {
  GSHSCENE_Limits limits;
  gshscene_limits_default(&limits);
  limits.max_resources = 1;
  StoreHolder s(&limits);
  Ref square(intern_square(s.store));
  ASSERT_NE(square.get(), nullptr);
  const GSHSCENE_Resource * out = nullptr;
  EXPECT_EQ(gshscene_store_intern_plane(s.store, &out), GSHSCENE_ERR_LIMIT);
  EXPECT_EQ(out, nullptr);
}

// --- PL3: where a call may name it ---------------------------------------------------------------------------------

TEST(Plane, IsTheShapeOfAFillOnly) {
  StoreHolder s;
  Ref plane(plane_of(s.store));
  Ref square(intern_square(s.store));
  ListHolder l(s.store);
  GSHSCENE_Paint p = foreground();
  EXPECT_EQ(gshscene_list_fill(l.list, plane.get(), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_fill(l.list, plane.get(), nullptr, GSHGEOM_FILL_EVEN_ODD, &p, 3, 0.0), GSHSCENE_OK)
      << "the rule is checked and has no effect";
  EXPECT_EQ(gshscene_list_count(l.list), 2u);
  EXPECT_EQ(gshscene_list_clip_push(l.list, plane.get(), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0), GSHSCENE_ERR_INVALID)
      << "not a clip";
  GSHSCENE_Stroke_Style style;
  gshscene_stroke_style_init(&style);
  const GSHSCENE_Resource * sty = nullptr;
  ASSERT_EQ(gshscene_store_intern_stroke_style(s.store, &style, &sty), GSHSCENE_OK);
  Ref styleref(sty);
  EXPECT_EQ(gshscene_list_stroke(l.list, plane.get(), styleref.get(), nullptr, &p, 0, 0.0), GSHSCENE_ERR_INVALID)
      << "not a centerline";
  EXPECT_EQ(gshscene_list_count(l.list), 2u) << "a refused call leaves the list as it was";
  // A bad rule, a 3D map, another store's plane.
  EXPECT_EQ(gshscene_list_fill(l.list, plane.get(), nullptr, (GSHGEOM_Fill_Rule)9, &p, 0, 0.0), GSHSCENE_ERR_INVALID);
  GSHGEOM_Map m3;
  ASSERT_EQ(gshgeom_map_identity(3, &m3), GSHGEOM_OK);
  EXPECT_EQ(gshscene_list_fill(l.list, plane.get(), &m3, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_ERR_UNSUPPORTED)
      << "a plane is planar";
  StoreHolder other;
  Ref foreign(plane_of(other.store));
  EXPECT_EQ(gshscene_list_fill(l.list, foreign.get(), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_count(l.list), 2u);
}

TEST(Plane, IsNotAShapeOfARetainedNode) {
  SceneHolder sc;
  Ref plane(plane_of(sc.store()));
  EditHolder e(sc);
  GSHSCENE_Node node = shape_node(plane.get());
  uint64_t id = 0;
  GSHSCENE_Result r = gshscene_edit_add(e.edit, 0, GSHSCENE_APPEND, &node, nullptr, &id);
  EXPECT_EQ(r, GSHSCENE_ERR_INVALID) << "PL5: a node never names a plane, so damage never meets one";
}

// --- PL6: the test sink --------------------------------------------------------------------------------------------

TEST(PlaneSink, RecordsNoBoxAndStampsTheClipRegion) {
  StoreHolder s;
  Ref plane(plane_of(s.store));
  Ref square(intern_square(s.store, 0.0));  // 4 by 4, scaled below to 8 by 12
  ListHolder l(s.store);
  GSHGEOM_Map scale;
  const double m[4] = {2.0, 0.0, 0.0, 3.0};
  const double t[2] = {4.0, 10.0};
  ASSERT_EQ(gshgeom_map_from_matrix(2, m, t, &scale), GSHGEOM_OK);
  ASSERT_EQ(gshscene_list_clip_push(l.list, square.get(), &scale, GSHGEOM_FILL_NONZERO, 0, 0.0), GSHSCENE_OK);
  GSHSCENE_Paint p = foreground();
  ASSERT_EQ(gshscene_list_fill(l.list, plane.get(), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_clip_pop(l.list), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(l.list, plane.get(), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
  Render r(full_config());
  ASSERT_EQ(r.run(l.list), GSHSCENE_OK);
  EXPECT_EQ(count_of(r.dump, " F res=plane:"), 2u);
  EXPECT_EQ(count_of(r.dump, " box=*\n"), 2u) << "unbounded, not empty";
  EXPECT_EQ(count_of(r.dump, "box=-"), 0u);
  EXPECT_EQ(count_of(r.dump, "S 4 10 12 22 "), 1u) << "the clip region of the first";
  EXPECT_EQ(count_of(r.dump, "S 0 0 64 48 "), 1u) << "the whole extent where no clip is in force";
}

TEST(PlaneSink, IsNeverRefusedForItsExtent) {
  StoreHolder s;
  Ref plane(plane_of(s.store));
  Ref square(intern_square(s.store, 0.0));
  ListHolder l(s.store);
  GSHSCENE_Paint p = foreground();
  ASSERT_EQ(gshscene_list_fill(l.list, square.get(), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(l.list, plane.get(), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
  GSHSCENE_Dump_Sink_Config c = full_config();
  c.max_extent = 0.5;  // smaller than the square's box
  Render r(c);
  ASSERT_EQ(r.run(l.list), GSHSCENE_OK);
  EXPECT_EQ(count_of(r.dump, "reason=" + std::to_string((int)GSHSCENE_REASON_RESOURCE_LIMIT)), 1u)
      << "the path is refused";
  EXPECT_EQ(count_of(r.dump, " F res=plane:"), 1u);
  EXPECT_EQ(count_of(r.dump, "S 0 0 64 48 "), 1u) << "and the plane is drawn";
  EXPECT_EQ(gshscene_report_count(r.report), 1u) << "only the path is reported";
}

TEST(PlaneSink, InAGroupAddsItsRegionToWhatTheGroupGathers) {
  StoreHolder s;
  Ref plane(plane_of(s.store));
  Ref square(intern_square(s.store, 0.0));
  ListHolder l(s.store);
  GSHGEOM_Map scale;
  const double m[4] = {2.5, 0.0, 0.0, 2.5};
  const double t[2] = {2.0, 3.0};
  ASSERT_EQ(gshgeom_map_from_matrix(2, m, t, &scale), GSHGEOM_OK);
  GSHSCENE_Group g = make_group(1.0);
  ASSERT_EQ(gshscene_list_group_push(l.list, &g, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_clip_push(l.list, square.get(), &scale, GSHGEOM_FILL_NONZERO, 0, 0.0), GSHSCENE_OK);
  GSHSCENE_Paint p = foreground();
  ASSERT_EQ(gshscene_list_fill(l.list, plane.get(), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_clip_pop(l.list), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_pop(l.list), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
  Render r(full_config());
  ASSERT_EQ(r.run(l.list), GSHSCENE_OK);
  EXPECT_EQ(count_of(r.dump, "S "), 1u) << "stamped once, at the group's pop";
  EXPECT_EQ(count_of(r.dump, "S 2 3 12 13 "), 1u);
}

TEST(PlaneSink, StaysInsideTheDamage) {
  StoreHolder s;
  Ref plane(plane_of(s.store));
  ListHolder l(s.store);
  GSHSCENE_Paint p = foreground();
  ASSERT_EQ(gshscene_list_fill(l.list, plane.get(), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
  Render r(full_config());
  GSHSCENE_Rect damage{5, 6, 20, 30};
  ASSERT_EQ(r.run(l.list, &damage), GSHSCENE_OK);
  EXPECT_EQ(count_of(r.dump, "S 5 6 20 30 "), 1u);
}

TEST(PlaneSink, ARecordsGradientIsNamedAsOnAnyFill) {
  StoreHolder s;
  Ref plane(plane_of(s.store));
  GSHSCENE_Gradient g;
  std::memset(&g, 0, sizeof g);
  g.kind = GSHSCENE_GRADIENT_LINEAR;
  g.p1[0] = 10.0;
  g.d[1] = 1.0;
  gshgeom_map_identity(2, &g.map);
  GSHSCENE_Gradient_Stop stops[2];
  std::memset(stops, 0, sizeof stops);
  stops[0].color = solid(1, 0, 0, 1);
  stops[1].offset = 1.0;
  stops[1].color = solid(0, 0, 1, 1);
  g.stop_count = 2;
  g.stops = stops;
  const GSHSCENE_Resource * gr = nullptr;
  ASSERT_EQ(gshscene_store_intern_gradient(s.store, &g, &gr), GSHSCENE_OK);
  Ref gref(gr);
  ListHolder l(s.store);
  GSHSCENE_Paint p{};
  p.kind = GSHSCENE_PAINT_GRADIENT;
  p.gradient = gr;
  p.rgba[3] = 1.0;
  ASSERT_EQ(gshscene_list_fill(l.list, plane.get(), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
  Render r(full_config());
  ASSERT_EQ(r.run(l.list), GSHSCENE_OK);
  EXPECT_NE(line_with(r.dump, " F res=plane:").find(" grad="), std::string::npos);
  EXPECT_EQ(count_of(r.dump, "R "), 1u) << "the gradient is resolved once";
}

// --- fixed.h: the fills of the plane, the design rectangle, the spaced group ------------------------------------------

namespace {

struct Frame {
  GSHSCENE_Test_Provider tp{};
  StoreHolder s;
  Ref face;
  Ref run;
  Frame() {
    tp.upem = 2048;
    face = Ref(gshscene_tp_face(s.store, &tp, 1, 0x77, 0));
    run = Ref(intern_font_run(s.store, face.get(), 20.48, {GSHSCENE_Glyph{3, 10.0, 20.0, nullptr}}));
  }
};

GSHGEOM_Map item_map(const GSHSCENE_List * list, size_t at) {
  const GSHSCENE_Item * item = nullptr;
  EXPECT_EQ(gshscene_list_item(list, at, &item), GSHSCENE_OK);
  return item->map;
}

}  // namespace

TEST(PlaneFixed, TheFillTakesTheItemMapAGlyphFillTakes) {
  Frame f;
  ASSERT_NE(f.run.get(), nullptr);
  Ref plane(plane_of(f.s.store));
  ListHolder l(f.s.store);
  GSHSCENE_Fixed_Op ops[2]{};
  ops[0].kind = GSHSCENE_FIXED_ROTATE;
  ops[0].bits = 15;
  ops[0].v[0] = 8192;  // an eighth of a turn
  ops[1].kind = GSHSCENE_FIXED_TRANSLATE;
  ops[1].bits = 0;
  ops[1].v[0] = 100;
  ops[1].v[1] = -50;
  GSHSCENE_Paint_Fixed paint{GSHSCENE_FIXED_PAINT_PALETTE, 1, 8192, 14};
  ASSERT_EQ(gshscene_list_fill_fixed(l.list, f.run.get(), ops, 2, GSHGEOM_FILL_NONZERO, &paint, 4), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill_plane_fixed(l.list, plane.get(), f.run.get(), ops, 2, GSHGEOM_FILL_NONZERO, &paint, 4),
      GSHSCENE_OK);
  GSHGEOM_Map glyph_map = item_map(l.list, 0);
  GSHGEOM_Map plane_map = item_map(l.list, 1);
  EXPECT_EQ(gshgeom_map_hash(&glyph_map), gshgeom_map_hash(&plane_map))
      << "the same ops for the same frame place the same map";
  const GSHSCENE_Item * item = nullptr;
  ASSERT_EQ(gshscene_list_item(l.list, 1, &item), GSHSCENE_OK);
  EXPECT_EQ(item->resource, plane.get());
  EXPECT_EQ(item->layer, 4);
  EXPECT_EQ(item->paint.kind, GSHSCENE_PAINT_PALETTE);
  EXPECT_EQ(item->paint.rgba[3], 0.5);
  // No ops: the identity, and no frame is needed for it.
  ASSERT_EQ(gshscene_list_fill_plane_fixed(l.list, plane.get(), nullptr, nullptr, 0, GSHGEOM_FILL_NONZERO, &paint, 0),
      GSHSCENE_OK);
  GSHGEOM_Map id;
  gshgeom_map_identity(2, &id);
  GSHGEOM_Map none_map = item_map(l.list, 2);
  EXPECT_EQ(gshgeom_map_hash(&none_map), gshgeom_map_hash(&id));
  // The refusals, each leaving the list as it was.
  size_t before = gshscene_list_count(l.list);
  EXPECT_EQ(gshscene_list_fill_plane_fixed(l.list, f.run.get(), f.run.get(), ops, 2, GSHGEOM_FILL_NONZERO, &paint, 0),
      GSHSCENE_ERR_INVALID) << "a run is not the plane";
  EXPECT_EQ(gshscene_list_fill_plane_fixed(l.list, plane.get(), nullptr, ops, 2, GSHGEOM_FILL_NONZERO, &paint, 0),
      GSHSCENE_ERR_INVALID) << "ops need a frame to be placed for";
  EXPECT_EQ(gshscene_list_fill_plane_fixed(l.list, plane.get(), f.run.get(), ops, 2, GSHGEOM_FILL_NONZERO, nullptr, 0),
      GSHSCENE_ERR_INVALID);
  GSHSCENE_Paint_Fixed bad{GSHSCENE_FIXED_PAINT_PALETTE, 1, 99999, 14};
  EXPECT_EQ(gshscene_list_fill_plane_fixed(l.list, plane.get(), f.run.get(), ops, 2, GSHGEOM_FILL_NONZERO, &bad, 0),
      GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_count(l.list), before);
}

TEST(PlaneFixed, TheGradientFillTakesTheGradientAndTheItemMap) {
  Frame f;
  Ref plane(plane_of(f.s.store));
  GSHSCENE_Gradient_Stop_Fixed stops[2] = {
      {0, 14, {GSHSCENE_FIXED_PAINT_PALETTE, 0, 16384, 14}},
      {16384, 14, {GSHSCENE_FIXED_PAINT_PALETTE, 1, 16384, 14}},
  };
  GSHSCENE_Gradient_Fixed gf{};
  gf.kind = GSHSCENE_FIXED_GRADIENT_LINEAR;
  gf.bits = 0;
  int32_t v[6] = {0, 0, 1000, 0, 0, 1000};
  std::memcpy(gf.v, v, sizeof v);
  gf.primaries = GCOL_PRIMARIES_SRGB;
  gf.transfer = GCOL_TRANSFER_LINEAR;
  gf.premultiplied = 1;
  gf.stop_count = 2;
  gf.stops = stops;
  const GSHSCENE_Resource * out = nullptr;
  ASSERT_EQ(gshscene_store_intern_gradient_fixed(f.s.store, f.run.get(), &gf, &out), GSHSCENE_OK);
  Ref grad(out);
  ListHolder l(f.s.store);
  GSHSCENE_Fixed_Op move{};
  move.kind = GSHSCENE_FIXED_TRANSLATE;
  move.v[0] = 40;
  ASSERT_EQ(gshscene_list_fill_plane_gradient_fixed(l.list, plane.get(), f.run.get(), &move, 1, GSHGEOM_FILL_NONZERO,
                grad.get(), 8192, 14, 2),
      GSHSCENE_OK);
  const GSHSCENE_Item * item = nullptr;
  ASSERT_EQ(gshscene_list_item(l.list, 0, &item), GSHSCENE_OK);
  EXPECT_EQ(item->resource, plane.get());
  EXPECT_EQ(item->paint.kind, GSHSCENE_PAINT_GRADIENT);
  EXPECT_EQ(item->paint.gradient, grad.get());
  EXPECT_EQ(item->paint.rgba[3], 0.5);
  EXPECT_EQ(gshscene_list_fill_plane_gradient_fixed(l.list, plane.get(), f.run.get(), &move, 1, GSHGEOM_FILL_NONZERO,
                grad.get(), -1, 14, 2),
      GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_fill_plane_gradient_fixed(l.list, plane.get(), f.run.get(), &move, 1, GSHGEOM_FILL_NONZERO,
                grad.get(), 1, 31, 2),
      GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_count(l.list), 1u);
}

TEST(PlaneFixed, TheDesignRectangleIsClippedWhereTheRunsGlyphWouldBe) {
  Frame f;
  ListHolder l(f.s.store);
  // Box (-100, 50) to (900, 1950) in 26.6-free design units, 2 fraction bits to show nothing rounds: x4.
  ASSERT_EQ(gshscene_list_clip_push_design_rect_fixed(l.list, f.run.get(), nullptr, 0, -400, 200, 3600, 7800, 2, 6),
      GSHSCENE_OK);
  const GSHSCENE_Item * item = nullptr;
  ASSERT_EQ(gshscene_list_item(l.list, 0, &item), GSHSCENE_OK);
  EXPECT_EQ(item->kind, GSHSCENE_ITEM_CLIP_PUSH);
  EXPECT_EQ(item->layer, 6);
  EXPECT_EQ(item->fill_rule, GSHGEOM_FILL_NONZERO);
  const GSHGEOM_Path * path = gshscene_resource_path(item->resource);
  ASSERT_NE(path, nullptr);
  GSHGEOM_Bounds b;
  ASSERT_EQ(gshgeom_path_bounds(path, &b), GSHGEOM_OK);
  EXPECT_EQ(b.min.x, -100.0);
  EXPECT_EQ(b.min.y, 50.0);
  EXPECT_EQ(b.max.x, 900.0);
  EXPECT_EQ(b.max.y, 1950.0);
  // A design corner lands where the glyph outline puts it: g + s (x, -y), s = 20.48 / 2048 = 0.01, g = (10, 20).
  GSHGEOM_Point q{};
  ASSERT_EQ(gshgeom_map_apply_point(&item->map, GSHGEOM_Point{900, 1950, 0}, &q), GSHGEOM_OK);
  EXPECT_EQ(q.x, 10.0 + 9.0);
  EXPECT_EQ(q.y, 20.0 - 19.5);
  // Under ops the box moves as the glyph's design space does: here a translation by (100, 0) design units.
  GSHSCENE_Fixed_Op move{};
  move.kind = GSHSCENE_FIXED_TRANSLATE;
  move.v[0] = 100;
  ASSERT_EQ(gshscene_list_clip_push_design_rect_fixed(l.list, f.run.get(), &move, 1, 0, 0, 10, 10, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_item(l.list, 1, &item), GSHSCENE_OK);
  ASSERT_EQ(gshgeom_map_apply_point(&item->map, GSHGEOM_Point{0, 0, 0}, &q), GSHGEOM_OK);
  EXPECT_EQ(q.x, 10.0 + 1.0);
  EXPECT_EQ(q.y, 20.0);
  // A box that is a point is a valid clip (it clips everything); an inverted one, past-precision bits and a bad frame are not.
  size_t before = gshscene_list_count(l.list);
  EXPECT_EQ(gshscene_list_clip_push_design_rect_fixed(l.list, f.run.get(), nullptr, 0, 5, 5, 5, 5, 0, 0), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_clip_push_design_rect_fixed(l.list, f.run.get(), nullptr, 0, 5, 0, 4, 9, 0, 0),
      GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_clip_push_design_rect_fixed(l.list, f.run.get(), nullptr, 0, 0, 5, 4, 4, 0, 0),
      GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_clip_push_design_rect_fixed(l.list, f.run.get(), nullptr, 0, 0, 0, 1, 1, 17, 0),
      GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_clip_push_design_rect_fixed(l.list, nullptr, nullptr, 0, 0, 0, 1, 1, 0, 0),
      GSHSCENE_ERR_INVALID);
  Ref square(intern_square(f.s.store));
  EXPECT_EQ(gshscene_list_clip_push_design_rect_fixed(l.list, square.get(), nullptr, 0, 0, 0, 1, 1, 0, 0),
      GSHSCENE_ERR_INVALID) << "a path is not a frame";
  EXPECT_EQ(gshscene_list_count(l.list), before + 1);
  // The clips are balanced by their pops.
  for (size_t i = 0; i < 3; i++) {
    EXPECT_EQ(gshscene_list_clip_pop(l.list), GSHSCENE_OK);
  }
  EXPECT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
}

TEST(PlaneFixed, TheSpaceValuesAFloatFreeCallerPassesArePinnedToTheColorLibrarys) {
  EXPECT_EQ(GSHSCENE_FIXED_PRIMARIES_SRGB, (uint32_t)GCOL_PRIMARIES_SRGB);
  EXPECT_EQ(GSHSCENE_FIXED_TRANSFER_LINEAR, (uint32_t)GCOL_TRANSFER_LINEAR);
  EXPECT_EQ(GSHSCENE_FIXED_TRANSFER_SRGB, (uint32_t)GCOL_TRANSFER_SRGB);
}

TEST(PlaneFixed, TheSpacedGroupStatesItsBlendSpace) {
  StoreHolder s;
  ListHolder l(s.store);
  ASSERT_EQ(gshscene_list_group_push_space_fixed(l.list, GSHSCENE_COMPOSITE_SRC_OVER, GCOL_PRIMARIES_SRGB,
                GCOL_TRANSFER_LINEAR, 3),
      GSHSCENE_OK);
  const GSHSCENE_Item * item = nullptr;
  ASSERT_EQ(gshscene_list_item(l.list, 0, &item), GSHSCENE_OK);
  EXPECT_EQ(item->kind, GSHSCENE_ITEM_GROUP_PUSH);
  EXPECT_EQ(item->group.opacity, 1.0);
  EXPECT_EQ(item->group.blend_space.primaries, GCOL_PRIMARIES_SRGB);
  EXPECT_EQ(item->group.blend_space.transfer, GCOL_TRANSFER_LINEAR);
  EXPECT_EQ(item->group.blend_space.gamma, 0.0);
  EXPECT_EQ(item->group.composite, (uint32_t)GSHSCENE_COMPOSITE_SRC_OVER);
  EXPECT_EQ(item->group.isolation, (uint32_t)GSHSCENE_ISOLATION_ISOLATED);
  EXPECT_EQ(item->layer, 3);
  ASSERT_EQ(gshscene_list_group_pop(l.list), GSHSCENE_OK);
  // A blend mode composites in the stated space too; the others are refused with nothing recorded.
  EXPECT_EQ(gshscene_list_group_push_space_fixed(l.list, 13u, GCOL_PRIMARIES_DISPLAY_P3, GCOL_TRANSFER_SRGB, 0),
      GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_pop(l.list), GSHSCENE_OK);
  size_t before = gshscene_list_count(l.list);
  EXPECT_EQ(gshscene_list_group_push_space_fixed(l.list, 0, GCOL_PRIMARIES_UNKNOWN, GCOL_TRANSFER_LINEAR, 0),
      GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_group_push_space_fixed(l.list, 0, GCOL_PRIMARIES_PROPHOTO, GCOL_TRANSFER_LINEAR, 0),
      GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_group_push_space_fixed(l.list, 0, GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_GAMMA, 0),
      GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_group_push_space_fixed(l.list, 0, GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_UNKNOWN, 0),
      GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_group_push_space_fixed(l.list, 9999u, GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_SRGB, 0),
      GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_count(l.list), before);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
