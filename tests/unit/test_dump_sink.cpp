/**
 * @file
 *
 * The test sink (dump_sink.h, story 13a): the conformance suite run against
 * each of its configurations, and what only the test sink does: its dump text
 * and output hash, its stamps, its resource limit, deferred completion, the
 * jobs in flight, its configuration's validation.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "sink_conformance.h"

#include <ghoti.io/shape-scene/shape-scene.h>

using namespace sinkconf;

namespace {

GSHSCENE_Sink * make_with(const GSHSCENE_Dump_Sink_Config & c, const GSHSCENE_Allocator * alloc,
    const GSHSCENE_Limits * limits) {
  GSHSCENE_Sink * sink = nullptr;
  GSHSCENE_Result r = gshscene_dump_sink_create(alloc, limits, &c, &sink);
  EXPECT_EQ(r, GSHSCENE_OK);
  return sink;
}

GSHSCENE_Dump_Sink_Config config_default() {
  GSHSCENE_Dump_Sink_Config c;
  gshscene_dump_sink_config_default(&c);
  return c;
}

/** Every flag on: the default. */
GSHSCENE_Sink * make_full(const GSHSCENE_Allocator * alloc, const GSHSCENE_Limits * limits) {
  return make_with(config_default(), alloc, limits);
}

/** The default with its one resource limit, `max_extent`, at @p level target units (S4's refusal rule). */
GSHSCENE_Sink * make_full_limited(const GSHSCENE_Allocator * alloc, unsigned level) {
  GSHSCENE_Dump_Sink_Config c = config_default();
  c.max_extent = (double)level / 4.0;
  return make_with(c, alloc, nullptr);
}

/** A sink that refuses glyphs from a font, XOR, the non-separable blend modes and radial gradients, clips without
 *  antialiasing, blends and interpolates only in the target's space, has no readback, no caller memory and no prepare. */
GSHSCENE_Dump_Sink_Config config_narrow() {
  GSHSCENE_Dump_Sink_Config c = config_default();
  for (GSHSCENE_Sink_Caps * caps : {&c.raster_caps, &c.stream_caps}) {
    caps->flags = GSHSCENE_CAP_CAN_BLOCK;
    caps->composites &= ~(1u << GSHSCENE_COMPOSITE_XOR);
    for (uint32_t mode = GSHSCENE_COMPOSITE_HUE; mode <= GSHSCENE_COMPOSITE_LUMINOSITY; mode++) {
      caps->composites &= ~(1u << mode); /* B13: a sink of the separable modes only refuses the other four */
    }
    caps->gradient_kinds &= ~(1u << GSHSCENE_GRADIENT_RADIAL);
  }
  return c;
}
GSHSCENE_Sink * make_narrow(const GSHSCENE_Allocator * alloc, const GSHSCENE_Limits * limits) {
  return make_with(config_narrow(), alloc, limits);
}

/** Refuses XOR but draws glyphs from a font: the suite's refusal is then the operator. */
GSHSCENE_Sink * make_no_xor(const GSHSCENE_Allocator * alloc, const GSHSCENE_Limits * limits) {
  GSHSCENE_Dump_Sink_Config c = config_default();
  c.raster_caps.composites &= ~(1u << GSHSCENE_COMPOSITE_XOR);
  c.stream_caps.composites &= ~(1u << GSHSCENE_COMPOSITE_XOR);
  return make_with(c, alloc, limits);
}

/** A browser's sink: cannot block, completes when its loop turns, a few units a turn. */
GSHSCENE_Dump_Sink_Config config_deferred() {
  GSHSCENE_Dump_Sink_Config c = config_default();
  c.raster_caps.flags &= ~GSHSCENE_CAP_CAN_BLOCK;
  c.stream_caps.flags &= ~GSHSCENE_CAP_CAN_BLOCK;
  c.completion = GSHSCENE_DUMP_SINK_DEFERRED;
  c.step_units = 3;
  return c;
}
GSHSCENE_Sink * make_deferred(const GSHSCENE_Allocator * alloc, const GSHSCENE_Limits * limits) {
  return make_with(config_deferred(), alloc, limits);
}

/** A GL context's sink: thread-affine, deferred, and it can block (it steps when waited for). */
GSHSCENE_Sink * make_affine(const GSHSCENE_Allocator * alloc, const GSHSCENE_Limits * limits) {
  GSHSCENE_Dump_Sink_Config c = config_default();
  c.raster_caps.flags |= GSHSCENE_CAP_THREAD_AFFINE;
  c.stream_caps.flags |= GSHSCENE_CAP_THREAD_AFFINE;
  c.completion = GSHSCENE_DUMP_SINK_DEFERRED;
  c.step_units = 2;
  return make_with(c, alloc, limits);
}

/** A non-blocking, thread-affine sink: WebGL on a browser's main thread. */
GSHSCENE_Sink * make_affine_deferred(const GSHSCENE_Allocator * alloc, const GSHSCENE_Limits * limits) {
  GSHSCENE_Dump_Sink_Config c = config_deferred();
  c.raster_caps.flags |= GSHSCENE_CAP_THREAD_AFFINE;
  c.stream_caps.flags |= GSHSCENE_CAP_THREAD_AFFINE;
  c.step_units = 1;
  c.targets = 1u << GSHSCENE_TARGET_RASTER;
  return make_with(c, alloc, limits);
}

/** A GPU sink's cancel: only before submission. */
GSHSCENE_Sink * make_before_submit(const GSHSCENE_Allocator * alloc, const GSHSCENE_Limits * limits) {
  GSHSCENE_Dump_Sink_Config c = config_default();
  for (GSHSCENE_Sink_Caps * caps : {&c.raster_caps, &c.stream_caps}) {
    caps->cancel_unit = GSHSCENE_CANCEL_BEFORE_SUBMIT;
    caps->cancel_work_bound = UINT64_MAX;
    caps->work_measure = "units (one check, before submission)";
  }
  return make_with(c, alloc, limits);
}

/** A writer: stream targets only. */
GSHSCENE_Sink * make_writer(const GSHSCENE_Allocator * alloc, const GSHSCENE_Limits * limits) {
  GSHSCENE_Dump_Sink_Config c = config_default();
  c.targets = 1u << GSHSCENE_TARGET_STREAM;
  return make_with(c, alloc, limits);
}

/**
 * A writer's stroke caps (S20, strokes story S1; the SVG 1.1 set): miter, round and bevel joins, every cap, a local
 * width under any map (SVG transforms a stroke), dashes restarting per subpath, natively; everything else expanded.
 */
GSHSCENE_Dump_Sink_Config config_writer_like() {
  GSHSCENE_Dump_Sink_Config c = config_default();
  for (GSHSCENE_Sink_Caps * caps : {&c.raster_caps, &c.stream_caps}) {
    caps->flags |= GSHSCENE_CAP_STROKE_EXPAND;
    caps->stroke_native = GSHSCENE_STROKE_NATIVE_JOIN_MITER | GSHSCENE_STROKE_NATIVE_JOIN_ROUND |
        GSHSCENE_STROKE_NATIVE_JOIN_BEVEL | GSHSCENE_STROKE_NATIVE_CAP_BUTT | GSHSCENE_STROKE_NATIVE_CAP_ROUND |
        GSHSCENE_STROKE_NATIVE_CAP_SQUARE | GSHSCENE_STROKE_NATIVE_WIDTH_LOCAL |
        GSHSCENE_STROKE_NATIVE_WIDTH_LOCAL_ANISOTROPIC | GSHSCENE_STROKE_NATIVE_DASH;
    // An image (S21): kept by handle and emitted once; a nearest and a bilinear filter (SVG's image-rendering, PDF's /Interpolate),
    // no cubic and no area average, no reflection, and no filtering in a stated space.
    caps->image_filters = (1u << GSHSCENE_IMAGE_FILTER_NEAREST) | (1u << GSHSCENE_IMAGE_FILTER_LINEAR);
    caps->image_extends = (1u << GSHSCENE_IMAGE_EXTEND_PAD) | (1u << GSHSCENE_IMAGE_EXTEND_REPEAT) | (1u << GSHSCENE_IMAGE_EXTEND_NONE);
    caps->flags &= ~GSHSCENE_CAP_IMAGE_FILTER_SPACES;
  }
  return c;
}
GSHSCENE_Sink * make_writer_like(const GSHSCENE_Allocator * alloc, const GSHSCENE_Limits * limits) {
  return make_with(config_writer_like(), alloc, limits);
}

/**
 * A toolpath's stroke caps (S20; a pen or a needle follows the centerline): a round join, round and butt caps, a
 * local width under a similarity and a device width, dashes; no expansion, so anything else is refused.
 */
GSHSCENE_Dump_Sink_Config config_toolpath_like() {
  GSHSCENE_Dump_Sink_Config c = config_default();
  for (GSHSCENE_Sink_Caps * caps : {&c.raster_caps, &c.stream_caps}) {
    // A pen follows a run's centerline (ST1's path): it claims STROKE_RUNS like every configuration here (ST10).
    caps->stroke_native = GSHSCENE_STROKE_NATIVE_JOIN_ROUND | GSHSCENE_STROKE_NATIVE_CAP_ROUND |
        GSHSCENE_STROKE_NATIVE_CAP_BUTT | GSHSCENE_STROKE_NATIVE_WIDTH_LOCAL | GSHSCENE_STROKE_NATIVE_WIDTH_DEVICE |
        GSHSCENE_STROKE_NATIVE_DASH;
    // A pen draws no raster (S21): every image paint is refused, naming what it needs.
    caps->image_filters = 0;
    caps->image_extends = 0;
    caps->image_formats = 0;
    caps->flags &= ~GSHSCENE_CAP_IMAGE_FILTER_SPACES;
  }
  return c;
}
GSHSCENE_Sink * make_toolpath_like(const GSHSCENE_Allocator * alloc, const GSHSCENE_Limits * limits) {
  return make_with(config_toolpath_like(), alloc, limits);
}

/**
 * A GPU's image caps (S21): every filter and edge mode through its samplers, no 12-bit and no float samples (no native
 * texture format), and hardware filtering in linear light or the encoded space only, never a stated one.
 */
GSHSCENE_Sink * make_gpu_like(const GSHSCENE_Allocator * alloc, const GSHSCENE_Limits * limits) {
  GSHSCENE_Dump_Sink_Config c = config_default();
  for (GSHSCENE_Sink_Caps * caps : {&c.raster_caps, &c.stream_caps}) {
    caps->image_formats &= ~(GSHSCENE_IMAGE_FORMAT_UNORM12 | GSHSCENE_IMAGE_FORMAT_FLOAT32);
    caps->image_filters &= ~(1u << GSHSCENE_IMAGE_FILTER_AREA);
    caps->flags &= ~GSHSCENE_CAP_IMAGE_FILTER_SPACES;
  }
  return make_with(c, alloc, limits);
}

/** A sink that draws everything but a stroke of a run: it does not unite a run's glyphs (ST5). */
GSHSCENE_Dump_Sink_Config config_no_run_strokes() {
  GSHSCENE_Dump_Sink_Config c = config_default();
  for (GSHSCENE_Sink_Caps * caps : {&c.raster_caps, &c.stream_caps}) {
    caps->flags &= ~GSHSCENE_CAP_STROKE_RUNS;
  }
  return c;
}

void step(GSHSCENE_Sink * sink) {
  EXPECT_EQ(gshscene_dump_sink_step(sink, nullptr), GSHSCENE_OK);
}

const Fixture kFull{"full", make_full, {GSHSCENE_TARGET_RASTER, GSHSCENE_TARGET_STREAM}, nullptr, make_full_limited};
const Fixture kNarrow{"narrow", make_narrow, {GSHSCENE_TARGET_RASTER, GSHSCENE_TARGET_STREAM}, nullptr};
const Fixture kNoXor{"no_xor", make_no_xor, {GSHSCENE_TARGET_RASTER, GSHSCENE_TARGET_STREAM}, nullptr};
const Fixture kDeferred{"deferred", make_deferred, {GSHSCENE_TARGET_RASTER, GSHSCENE_TARGET_STREAM}, step};
const Fixture kAffine{"affine", make_affine, {GSHSCENE_TARGET_RASTER, GSHSCENE_TARGET_STREAM}, step};
const Fixture kAffineDeferred{"affine_deferred", make_affine_deferred, {GSHSCENE_TARGET_RASTER}, step};
const Fixture kBeforeSubmit{"before_submit", make_before_submit, {GSHSCENE_TARGET_RASTER, GSHSCENE_TARGET_STREAM}, nullptr};
const Fixture kWriter{"writer", make_writer, {GSHSCENE_TARGET_STREAM}, nullptr};
const Fixture kWriterLike{"writer_like", make_writer_like, {GSHSCENE_TARGET_RASTER, GSHSCENE_TARGET_STREAM}, nullptr};
const Fixture kToolpathLike{"toolpath_like", make_toolpath_like, {GSHSCENE_TARGET_RASTER, GSHSCENE_TARGET_STREAM}, nullptr};
const Fixture kGpuLike{"gpu_like", make_gpu_like, {GSHSCENE_TARGET_RASTER, GSHSCENE_TARGET_STREAM}, nullptr};

} // namespace

INSTANTIATE_TEST_SUITE_P(DumpSink, SinkConformance,
    ::testing::Values(&kFull, &kNarrow, &kNoXor, &kDeferred, &kAffine, &kAffineDeferred, &kBeforeSubmit, &kWriter,
        &kWriterLike, &kToolpathLike, &kGpuLike),
    [](const ::testing::TestParamInfo<const Fixture *> & info) { return std::string(info.param->name); });

/* --- the fixture of tools/gen-sink-vectors.py ----------------------------------------------------------- */

#include "../sink_answers.h"

#include <cstdio>
#include <functional>
#include <thread>

namespace {

const GSHSCENE_List * answer_list(Scene & s) {
  GSHSCENE_Paint red = solid(1, 0, 0);
  GSHSCENE_Paint blue = solid(0, 0, 1, 0.5);
  GSHSCENE_List * pl = s.open();
  gshscene_list_fill(pl, s.sq(0, 0, 10), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0);
  gshscene_list_fill(pl, s.sq(2, 2, 4), nullptr, GSHGEOM_FILL_NONZERO, &blue, 0, 0.0);
  gshscene_list_close(pl);
  const GSHSCENE_Resource * picture = nullptr;
  gshscene_store_intern_picture(s.store, pl, &picture);
  s.held.push_back(picture);
  GSHSCENE_Group g{};
  g.opacity = 0.5;
  g.blend_space.primaries = GCOL_PRIMARIES_SRGB;
  g.blend_space.transfer = GCOL_TRANSFER_LINEAR;
  g.ordering = GSHSCENE_ORDER_DEPTH;
  GSHSCENE_List * l = s.open();
  gshscene_list_fill(l, s.sq(2, 2, 8), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0);
  gshscene_list_clip_push(l, s.sq(4, 4, 20), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0);
  gshscene_list_fill(l, s.sq(0, 0, 30), nullptr, GSHGEOM_FILL_NONZERO, &blue, 0, 0.0);
  gshscene_list_clip_pop(l);
  gshscene_list_group_push(l, &g, 0, 0.0);
  gshscene_list_fill(l, s.sq(10, 10, 6), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 1.0);
  gshscene_list_fill(l, s.sq(12, 12, 6), nullptr, GSHGEOM_FILL_NONZERO, &blue, 0, 1.0);
  gshscene_list_group_pop(l);
  GSHGEOM_Map move;
  gshgeom_map_translate(2, 30.0, 30.0, 0.0, &move);
  gshscene_list_instance(l, picture, &move, 0, 0.0);
  gshscene_list_fill(l, s.sq(40, 2, 6), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0);
  GSHSCENE_Group empty{};
  empty.opacity = 0.0;
  gshscene_list_group_push(l, &empty, 0, 0.0);
  gshscene_list_group_pop(l);
  gshscene_list_fill(l, s.sq(-6, -6, 6), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0);
  EXPECT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  return l;
}

/** A writer that keeps what it is given. */
struct Log {
  std::string text;
  GSHSCENE_Writer writer{};
  Log() {
    writer.write = [](void * ctx, const void * b, size_t n) {
      ((Log *)ctx)->text.append((const char *)b, n);
      return GSHSCENE_OK;
    };
    writer.ctx = this;
  }
};

std::string report_dump(const GSHSCENE_Report * report) {
  char * buffer = nullptr;
  size_t size = 0;
  FILE * f = open_memstream(&buffer, &size);
  EXPECT_EQ(gshscene_report_dump(report, f), GSHSCENE_OK);
  fclose(f);
  std::string out(buffer, size);
  free(buffer);
  return out;
}

/** The generator's report text with the host's frame and workspace bytes put in. */
std::string with_bytes(std::string text, const GSHSCENE_Report * report) {
  GSHSCENE_Report_Summary sum = summary(report);
  auto put = [&](const std::string & key, uint64_t value) {
    size_t at = text.find(key);
    if (at != std::string::npos) {
      text.replace(at, key.size(), std::to_string(value));
    }
  };
  put("%FRAME%", sum.frame_bytes);
  put("%WORKSPACE%", sum.workspace_needed);
  return text;
}

struct Rendered {
  GSHSCENE_Result result;
  uint64_t output;
};

Rendered render_job(GSHSCENE_Sink * sink, GSHSCENE_Render_Request req) {
  GSHSCENE_Job job{};
  Rendered r{gshscene_sink_begin(sink, &req, &job), 0};
  GSHSCENE_Job_Status st{};
  gshscene_job_poll(&job, &st);
  while (st.state == GSHSCENE_JOB_RUNNING) {
    gshscene_dump_sink_step(sink, nullptr);
    gshscene_job_poll(&job, &st);
  }
  if (st.state == GSHSCENE_JOB_DONE) {
    r.result = st.result;
    EXPECT_EQ(gshscene_dump_sink_output_hash(&job, &r.output), GSHSCENE_OK);
  }
  return r;
}

} // namespace

TEST(DumpSink, TheRasterDumpStampsAndOutputHashAreTheGenerators) {
  Scene s;
  const GSHSCENE_List * list = answer_list(s);
  EXPECT_EQ(gshscene_list_hash(list), sink_known::kListHash);
  Log log;
  GSHSCENE_Dump_Sink_Config c = config_default();
  c.log = &log.writer;
  SinkRef sink(make_with(c, nullptr, nullptr));
  auto raster = make_raster();
  ReportRef report;
  Rendered r = render_job(sink.sink, request_for(list, *raster, report.report));
  ASSERT_EQ(r.result, GSHSCENE_OK);
  EXPECT_EQ(log.text, sink_known::kRasterDump);
  EXPECT_EQ(r.output, sink_known::kRasterOutputHash);
  EXPECT_EQ(fnv(log.text.data(), log.text.size()), sink_known::kRasterOutputHash);
  EXPECT_EQ(raster->snapshot(), sink_known::kRasterPixelsHash);
}

TEST(DumpSink, AConfigurationThatClaimsPdfsGroupFieldsRecordsThem) {
  // T1, S19: the default caps claim none of the three, so the planning pass refuses each; a configuration that claims
  // them has the group drawn, and its record names the fields (the default group's record is as before).
  Scene s;
  GSHSCENE_List * l = s.open();
  GSHSCENE_Paint red = solid(1, 0, 0);
  GSHSCENE_Group knockout{};
  knockout.opacity = 1.0;
  knockout.knockout = 1;
  knockout.isolation = GSHSCENE_ISOLATION_NON_ISOLATED;
  ASSERT_EQ(gshscene_list_group_push(l, &knockout, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(l, s.sq(2, 2, 6), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
  GSHSCENE_Group plain{};
  plain.opacity = 0.5;
  ASSERT_EQ(gshscene_list_group_push(l, &plain, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  for (int claims = 0; claims < 2; claims++) {
    Log log;
    GSHSCENE_Dump_Sink_Config c = config_default();
    c.log = &log.writer;
    if (claims) {
      c.raster_caps.flags |= GSHSCENE_CAP_GROUP_NON_ISOLATED | GSHSCENE_CAP_GROUP_KNOCKOUT | GSHSCENE_CAP_GROUP_SOFT_MASK;
    }
    SinkRef sink(make_with(c, nullptr, nullptr));
    auto raster = make_raster();
    ReportRef report;
    Rendered r = render_job(sink.sink, request_for(l, *raster, report.report));
    ASSERT_EQ(r.result, GSHSCENE_OK);
    bool recorded = log.text.find(" isolation=1 knockout=1 mask=0000000000000000\n") != std::string::npos;
    EXPECT_EQ(recorded, claims == 1) << log.text;
    EXPECT_EQ(gshscene_report_count(report.report), claims ? 0u : 1u) << "refused (GROUP_ISOLATION) without the caps";
    EXPECT_NE(log.text.find(" G opacity=3fe0000000000000 composite=0\n"), std::string::npos) << "the default record";
  }
}

TEST(DumpSink, TheStreamDumpInMillimetresUnderAMapIsTheGenerators) {
  Scene s;
  const GSHSCENE_List * list = answer_list(s);
  SinkRef sink(make_full(nullptr, nullptr));
  auto stream = make_stream(GSHSCENE_UNIT_MILLIMETER);
  gshgeom_map_scale(2, 0.5, 0.5, 1.0, &stream->target.map);
  ReportRef report;
  Rendered r = render_job(sink.sink, request_for(list, *stream, report.report));
  ASSERT_EQ(r.result, GSHSCENE_OK);
  EXPECT_EQ(stream->bytes, sink_known::kStreamDump);
  EXPECT_EQ(r.output, sink_known::kStreamOutputHash);
}

TEST(DumpSink, TheNarrowSinksReportIsTheGenerators) {
  Scene s;
  const GSHSCENE_List * list = answer_list(s);
  SinkRef sink(make_narrow(nullptr, nullptr));
  auto raster = make_raster();
  ReportRef report;
  ASSERT_EQ(render_job(sink.sink, request_for(list, *raster, report.report)).result, GSHSCENE_OK);
  EXPECT_EQ(gshscene_report_hash(report.report), sink_known::kNarrowReportHash);
  EXPECT_EQ(report_dump(report.report), with_bytes(sink_known::kNarrowReportDump, report.report));
  EXPECT_EQ(raster->snapshot(), sink_known::kRasterPixelsHash) << "approximations change nothing a marker shows";
}

/* --- the glyph fixture of tools/gen-sink-vectors.py (story 14a) -------------------------------------------- */

namespace {

/** Two fills of one run from the test provider's 2048-unit face with a program: the identity and a quarter turn. */
const GSHSCENE_List * glyph_list(Scene & s) {
  const GSHSCENE_Resource * face = gshscene_tp_face(s.store, &s.provider, 1, 0x51, 0);
  s.held.push_back(face);
  std::vector<GSHSCENE_Glyph> g = {{1, 2.0, 30.0, nullptr}, {2, 13.25, 30.5, nullptr}, {4, 24.0, 30.0, nullptr},
      {3, 30.0, 30.0, nullptr}};
  GSHSCENE_Glyph_Run run{};
  run.source = GSHSCENE_GLYPHS_FROM_FONT;
  run.face = face;
  run.size = 16.0;
  run.hint_mode = GSHSCENE_HINT_FONT;
  run.glyph_count = g.size();
  run.glyphs = g.data();
  const GSHSCENE_Resource * r = nullptr;
  EXPECT_EQ(gshscene_store_intern_glyph_run(s.store, &run, &r), GSHSCENE_OK);
  s.held.push_back(r);
  GSHGEOM_Map quarter;
  GSHGEOM_Map move;
  gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, 90.0, &quarter);
  gshgeom_map_translate(2, 60.0, 0.0, 0.0, &move);
  gshgeom_map_compose(&move, &quarter, &quarter);
  GSHSCENE_Paint red = solid(1, 0, 0);
  GSHSCENE_List * l = s.open();
  gshscene_list_fill(l, r, nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0);
  gshscene_list_fill(l, r, &quarter, GSHGEOM_FILL_NONZERO, &red, 0, 0.0);
  EXPECT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  return l;
}

}  // namespace

TEST(DumpSink, TheGlyphRecordsOnARasterAreTheGenerators) {
  Scene s;
  const GSHSCENE_List * list = glyph_list(s);
  EXPECT_EQ(gshscene_list_hash(list), sink_known::kGlyphListHash);
  const GSHSCENE_Item * item = nullptr;
  ASSERT_EQ(gshscene_list_item(list, 0, &item), GSHSCENE_OK);
  EXPECT_EQ(gshscene_resource_hash(item->resource), sink_known::kGlyphRunHash);
  Log log;
  GSHSCENE_Dump_Sink_Config c = config_default();
  c.log = &log.writer;
  SinkRef sink(make_with(c, nullptr, nullptr));
  auto raster = make_raster();
  ReportRef report;
  gshscene_tp_reset(&s.provider);
  Rendered r = render_job(sink.sink, request_for(list, *raster, report.report));
  ASSERT_EQ(r.result, GSHSCENE_OK);
  EXPECT_EQ(log.text, sink_known::kGlyphRasterDump);
  EXPECT_EQ(r.output, sink_known::kGlyphRasterOutputHash);
  EXPECT_EQ(raster->snapshot(), sink_known::kGlyphRasterPixelsHash);
  EXPECT_EQ(gshscene_report_hash(report.report), sink_known::kGlyphRasterReportHash);
  EXPECT_EQ(report_dump(report.report), with_bytes(sink_known::kGlyphRasterReportDump, report.report));
  EXPECT_EQ(gcu_atomic_size_load(&s.provider.fast_calls), 4u) << "the first item's glyphs, by the fast tier";
  EXPECT_EQ(gcu_atomic_size_load(&s.provider.outline_calls), 4u) << "the second's, by the general route";
}

TEST(DumpSink, TheGlyphRecordsOnAStreamAreTheGenerators) {
  Scene s;
  const GSHSCENE_List * list = glyph_list(s);
  SinkRef sink(make_full(nullptr, nullptr));
  auto stream = make_stream(GSHSCENE_UNIT_MILLIMETER);
  gshgeom_map_scale(2, 0.5, 0.5, 1.0, &stream->target.map);
  ReportRef report;
  Rendered r = render_job(sink.sink, request_for(list, *stream, report.report));
  ASSERT_EQ(r.result, GSHSCENE_OK);
  EXPECT_EQ(stream->bytes, sink_known::kGlyphStreamDump);
  EXPECT_EQ(r.output, sink_known::kGlyphStreamOutputHash);
  EXPECT_EQ(gshscene_report_hash(report.report), sink_known::kGlyphStreamReportHash);
}

// Color-paint story P2: the gradient records, restated by tools/gen-sink-vectors.py.  The fixture interns, into a
// fresh store and in this order, the palette and gradients A, B and D, so that their ids are the generator's.
TEST(DumpSink, TheGradientRecordsOnAStreamAreTheGenerators) {
  Scene s;
  GSHSCENE_Color_Space srgb{};
  srgb.primaries = GCOL_PRIMARIES_SRGB;
  srgb.transfer = GCOL_TRANSFER_SRGB;
  GSHSCENE_Color_Space linear{};
  linear.primaries = GCOL_PRIMARIES_SRGB;
  linear.transfer = GCOL_TRANSFER_LINEAR;
  GSHSCENE_Color entry{{0.03125, 0.0, 0.0390625, 1.0}, srgb};
  const GSHSCENE_Resource * palette = nullptr;
  ASSERT_EQ(gshscene_store_intern_palette(s.store, 1, &entry, &palette), GSHSCENE_OK);
  s.held.push_back(palette);
  auto intern = [&](GSHSCENE_Gradient g, std::vector<GSHSCENE_Gradient_Stop> stops) {
    gshgeom_map_identity(2, &g.map);
    g.stops = stops.data();
    g.stop_count = stops.size();
    const GSHSCENE_Resource * r = nullptr;
    EXPECT_EQ(gshscene_store_intern_gradient(s.store, &g, &r), GSHSCENE_OK);
    s.held.push_back(r);
    return r;
  };
  GSHSCENE_Paint fg{};
  fg.kind = GSHSCENE_PAINT_FOREGROUND;
  fg.rgba[3] = 0.5;
  GSHSCENE_Paint pal{};
  pal.kind = GSHSCENE_PAINT_PALETTE;
  pal.entry = 0;
  pal.rgba[3] = 0.75;
  GSHSCENE_Paint first = solid(0.25, 0.5, 1.0);
  first.space = linear;
  GSHSCENE_Gradient a{};
  a.kind = GSHSCENE_GRADIENT_LINEAR;
  a.p1[0] = 10.0;
  a.space = linear;
  GSHSCENE_Gradient b{};
  b.kind = GSHSCENE_GRADIENT_RADIAL;
  b.c0[0] = b.c0[1] = b.c1[0] = b.c1[1] = 1.0;
  b.r1 = 5.0;
  GSHSCENE_Gradient d{};
  d.kind = GSHSCENE_GRADIENT_LINEAR;
  d.p0[0] = d.p0[1] = d.p1[0] = d.p1[1] = 5.0;
  const GSHSCENE_Resource * ga = intern(a, {{0.0, first}, {0.5, fg}, {1.0, pal}});
  const GSHSCENE_Resource * gb = intern(b, {{0.0, solid(1, 0, 0)}, {1.0, solid(0, 0, 1, 0.5)}});
  const GSHSCENE_Resource * gd = intern(d, {{0.0, solid(1, 0, 0)}, {1.0, solid(0, 0, 1)}});
  ASSERT_EQ(gshscene_resource_id(ga), 2u) << "the generator's ids: a fresh store, in this order";
  ASSERT_EQ(gshscene_resource_id(gb), 3u);
  ASSERT_EQ(gshscene_resource_id(gd), 4u);
  EXPECT_EQ(gshscene_resource_hash(ga), sink_known::kGradientAHash);
  EXPECT_EQ(gshscene_resource_hash(gb), sink_known::kGradientBHash);
  GSHSCENE_List * l = s.open();
  GSHSCENE_Color fgc{{0.02, 0.03125, 0.0, 1.0}, srgb};
  ASSERT_EQ(gshscene_list_set_foreground(l, &fgc), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_set_palette(l, palette), GSHSCENE_OK);
  struct F {
    double x, y, size;
    const GSHSCENE_Resource * g;
    double alpha;
  };
  for (const F & f : {F{2, 2, 8, ga, 1.0}, F{10, 4, 6, ga, 0.5}, F{20, 20, 6, gb, 1.0}, F{30, 2, 4, gd, 1.0}}) {
    GSHSCENE_Paint p{};
    p.kind = GSHSCENE_PAINT_GRADIENT;
    p.resource = f.g;
    p.rgba[3] = f.alpha;
    ASSERT_EQ(gshscene_list_fill(l, s.sq(f.x, f.y, f.size), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK);
  }
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_hash(l), sink_known::kGradientListHash);
  SinkRef sink(make_full(nullptr, nullptr));
  auto stream = make_stream(GSHSCENE_UNIT_MILLIMETER);
  gshgeom_map_scale(2, 0.5, 0.5, 1.0, &stream->target.map);
  ReportRef report;
  Rendered r = render_job(sink.sink, request_for(l, *stream, report.report));
  ASSERT_EQ(r.result, GSHSCENE_OK);
  EXPECT_EQ(stream->bytes, sink_known::kGradientStreamDump);
  EXPECT_EQ(r.output, sink_known::kGradientStreamOutputHash);
  EXPECT_EQ(gshscene_report_hash(report.report), sink_known::kGradientStreamReportHash);
  EXPECT_EQ(report_dump(report.report), with_bytes(sink_known::kGradientStreamReportDump, report.report));
}

// Strokes story S1 (S20): the stroke records in the test sink's three configurations, restated by
// tools/gen-sink-vectors.py.  The fixture interns, into a fresh store and in this order, styles A, B, Z and C, so that
// their ids are the generator's.
namespace {

const GSHSCENE_List * stroke_list(Scene & s) {
  auto intern_style = [&](GSHSCENE_Stroke_Style st, std::vector<double> dashes = {}) {
    st.dash_count = dashes.size();
    st.dashes = dashes.empty() ? nullptr : dashes.data();
    const GSHSCENE_Resource * r = nullptr;
    EXPECT_EQ(gshscene_store_intern_stroke_style(s.store, &st, &r), GSHSCENE_OK);
    s.held.push_back(r);
    return r;
  };
  GSHSCENE_Stroke_Style a;
  gshscene_stroke_style_init(&a);
  GSHSCENE_Stroke_Style b = a;
  b.width = 2.0;
  b.width_space = GSHSCENE_STROKE_WIDTH_DEVICE;
  b.join = GSHSCENE_STROKE_JOIN_ROUND;
  b.cap = GSHSCENE_STROKE_CAP_ROUND;
  GSHSCENE_Stroke_Style z = a;
  z.width = 0.0;
  GSHSCENE_Stroke_Style c = a;
  c.width = 1.5;
  c.join = GSHSCENE_STROKE_JOIN_BEVEL;
  c.cap = GSHSCENE_STROKE_CAP_SQUARE;
  c.degenerate = GSHSCENE_STROKE_DEGENERATE_OMIT;
  c.dash_offset = -0.5;
  c.dash_scope = GSHSCENE_STROKE_DASH_PATH;
  c.alignment = GSHSCENE_STROKE_ALIGN_INSIDE;
  c.alignment_rule = GSHGEOM_FILL_EVEN_ODD;
  const GSHSCENE_Resource * sa = intern_style(a);
  const GSHSCENE_Resource * sb = intern_style(b);
  const GSHSCENE_Resource * sz = intern_style(z);
  const GSHSCENE_Resource * sc = intern_style(c, {1.0, 2.0, 3.0});
  EXPECT_EQ(gshscene_resource_id(sa), 1u) << "the generator's ids: a fresh store, in this order";
  EXPECT_EQ(gshscene_resource_id(sc), 4u);
  GSHSCENE_Paint red = solid(1, 0, 0);
  GSHSCENE_Paint blue = solid(0, 0, 1, 0.5);
  GSHSCENE_List * l = s.open();
  EXPECT_EQ(gshscene_list_stroke(l, s.sq(2, 2, 8), sa, nullptr, &red, 0, 0.0), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_stroke(l, s.sq(10, 4, 6), sb, nullptr, &blue, 0, 0.0), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_stroke(l, s.sq(20, 20, 6), sa, nullptr, &red, 0, 0.0), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_stroke(l, s.sq(30, 2, 4), sz, nullptr, &red, 0, 0.0), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_stroke(l, s.sq(40, 40, 4), sc, nullptr, &red, 0, 0.0), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  return l;
}

}  // namespace

TEST(DumpSink, TheStrokeRecordsAreTheGeneratorsInEachConfiguration) {
  struct Case {
    const char * what;
    GSHSCENE_Dump_Sink_Config config;
    const char * dump;
    uint64_t output;
    uint64_t report;
  } cases[] = {
      {"every feature native", config_default(), sink_known::kStrokeStreamDump, sink_known::kStrokeStreamOutputHash,
          sink_known::kStrokeStreamReportHash},
      {"a writer's set, the rest expanded", config_writer_like(), sink_known::kStrokeWriterDump,
          sink_known::kStrokeWriterOutputHash, sink_known::kStrokeWriterReportHash},
      {"a toolpath's set, the rest refused", config_toolpath_like(), sink_known::kStrokeToolpathDump,
          sink_known::kStrokeToolpathOutputHash, sink_known::kStrokeToolpathReportHash},
  };
  EXPECT_EQ(config_writer_like().stream_caps.stroke_native, sink_known::kStrokeWriterNative);
  EXPECT_EQ(config_toolpath_like().stream_caps.stroke_native, sink_known::kStrokeToolpathNative);
  for (const Case & c : cases) {
    Scene s;
    const GSHSCENE_List * list = stroke_list(s);
    EXPECT_EQ(gshscene_list_hash(list), sink_known::kStrokeListHash) << c.what;
    SinkRef sink(make_with(c.config, nullptr, nullptr));
    auto stream = make_stream(GSHSCENE_UNIT_MILLIMETER);
    gshgeom_map_scale(2, 0.5, 0.5, 1.0, &stream->target.map);
    ReportRef report;
    Rendered r = render_job(sink.sink, request_for(list, *stream, report.report));
    ASSERT_EQ(r.result, GSHSCENE_OK) << c.what;
    EXPECT_EQ(stream->bytes, c.dump) << c.what;
    EXPECT_EQ(r.output, c.output) << c.what;
    EXPECT_EQ(gshscene_report_hash(report.report), c.report) << c.what;
  }
  // The toolpath's report, in full: each refusal names the features the pen lacks.
  Scene s;
  const GSHSCENE_List * list = stroke_list(s);
  SinkRef sink(make_with(config_toolpath_like(), nullptr, nullptr));
  auto stream = make_stream(GSHSCENE_UNIT_MILLIMETER);
  gshgeom_map_scale(2, 0.5, 0.5, 1.0, &stream->target.map);
  ReportRef report;
  ASSERT_EQ(render_job(sink.sink, request_for(list, *stream, report.report)).result, GSHSCENE_OK);
  EXPECT_EQ(report_dump(report.report), with_bytes(sink_known::kStrokeToolpathReportDump, report.report));
  // Under strict the toolpath's planned refusals fail the render before the sink is called.
  auto strict_stream = make_stream(GSHSCENE_UNIT_MILLIMETER);
  GSHSCENE_Render_Request strict = request_for(list, *strict_stream, report.report);
  strict.flags = GSHSCENE_RENDER_STRICT;
  EXPECT_EQ(render_job(sink.sink, strict).result, GSHSCENE_ERR_REFUSED);
  EXPECT_TRUE(strict_stream->bytes.empty()) << "the target is untouched";
}

TEST(DumpSink, TheStrokeRecordsOnARasterAreTheGenerators) {
  Scene s;
  const GSHSCENE_List * list = stroke_list(s);
  Log log;
  GSHSCENE_Dump_Sink_Config c = config_default();
  c.log = &log.writer;
  SinkRef sink(make_with(c, nullptr, nullptr));
  auto raster = make_raster();
  ReportRef report;
  Rendered r = render_job(sink.sink, request_for(list, *raster, report.report));
  ASSERT_EQ(r.result, GSHSCENE_OK);
  EXPECT_EQ(log.text, sink_known::kStrokeRasterDump);
  EXPECT_EQ(r.output, sink_known::kStrokeRasterOutputHash);
  EXPECT_EQ(raster->snapshot(), sink_known::kStrokeRasterPixelsHash);
}

// Color-paint story P5 (PH3, PH6, PH8): the placeholder records and the report entries in the test sink's configurations,
// restated by tools/gen-sink-vectors.py.  Every configuration says the same, whatever its caps claim.
namespace {

const GSHSCENE_List * placeholder_list(Scene & s) {
  GSHSCENE_Paint red = solid(1, 0, 0);
  GSHSCENE_Paint blue = solid(0, 0, 1, 0.5);
  GSHGEOM_Map move;
  gshgeom_map_translate(2, 3.0, 4.0, 0.0, &move);
  const double box_a[4] = {0.0, 0.0, 5.0, 5.0};
  const double box_c[4] = {-1.0, -2.0, 3.0, 4.0};
  const uint16_t colr = GSHSCENE_PLACEHOLDER_SOURCE_COLR;
  GSHSCENE_List * l = s.open();
  EXPECT_EQ(gshscene_list_fill(l, s.sq(2, 2, 8), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_placeholder(l, colr, GSHSCENE_PLACEHOLDER_COLR_UNBOUNDED, 0, box_a, &move, 0, 0.0), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_placeholder(l, colr, GSHSCENE_PLACEHOLDER_COLR_WALK_LIMIT, 1, nullptr, nullptr, 2, 0.0), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_fill(l, s.sq(20, 20, 6), nullptr, GSHGEOM_FILL_NONZERO, &blue, 0, 0.0), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_placeholder(l, colr, GSHSCENE_PLACEHOLDER_COLR_NO_RECORD, 0, box_c, nullptr, 0, 0.0), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  return l;
}

}  // namespace

TEST(DumpSink, ThePlaceholderRecordsAndEntriesAreTheGeneratorsInEveryConfiguration) {
  const GSHSCENE_Dump_Sink_Config configs[] = {config_default(), config_writer_like(), config_toolpath_like(), config_narrow()};
  for (const GSHSCENE_Dump_Sink_Config & config : configs) {
    Scene s;
    const GSHSCENE_List * list = placeholder_list(s);
    EXPECT_EQ(gshscene_list_hash(list), sink_known::kPlaceholderListHash);
    SinkRef sink(make_with(config, nullptr, nullptr));
    auto stream = make_stream(GSHSCENE_UNIT_MILLIMETER);
    gshgeom_map_scale(2, 0.5, 0.5, 1.0, &stream->target.map);
    ReportRef report;
    Rendered r = render_job(sink.sink, request_for(list, *stream, report.report));
    ASSERT_EQ(r.result, GSHSCENE_OK);
    EXPECT_EQ(stream->bytes, sink_known::kPlaceholderStreamDump);
    EXPECT_EQ(r.output, sink_known::kPlaceholderStreamOutputHash);
    EXPECT_EQ(gshscene_report_hash(report.report), sink_known::kPlaceholderReportHash);
    EXPECT_EQ(report_dump(report.report), with_bytes(sink_known::kPlaceholderReportDump, report.report));
    // Strict stops before the sink is called: the refused ones are planned.
    auto strict_stream = make_stream(GSHSCENE_UNIT_MILLIMETER);
    ReportRef strict_report;
    GSHSCENE_Render_Request strict = request_for(list, *strict_stream, strict_report.report);
    strict.flags = GSHSCENE_RENDER_STRICT;
    EXPECT_EQ(render_job(sink.sink, strict).result, GSHSCENE_ERR_REFUSED);
    EXPECT_TRUE(strict_stream->bytes.empty()) << "the target is untouched";
  }
}

TEST(DumpSink, ThePlaceholderRecordsOnARasterAreTheGenerators) {
  Scene s;
  const GSHSCENE_List * list = placeholder_list(s);
  Log log;
  GSHSCENE_Dump_Sink_Config c = config_default();
  c.log = &log.writer;
  SinkRef sink(make_with(c, nullptr, nullptr));
  auto raster = make_raster();
  ReportRef report;
  Rendered r = render_job(sink.sink, request_for(list, *raster, report.report));
  ASSERT_EQ(r.result, GSHSCENE_OK);
  EXPECT_EQ(log.text, sink_known::kPlaceholderRasterDump);
  EXPECT_EQ(r.output, sink_known::kPlaceholderRasterOutputHash);
  EXPECT_EQ(raster->snapshot(), sink_known::kPlaceholderRasterPixelsHash) << "a placeholder stamps nothing";
  EXPECT_EQ(gshscene_report_hash(report.report), sink_known::kPlaceholderReportHash);
}

TEST(DumpSink, ASinkThatClaimsEveryKindAndOneThatClaimsNoneReportAPlaceholderAlike) {
  // PH3: no item_kinds bit is needed or consulted.  A list of placeholders alone is all a sink that draws nothing can
  // be given and not refuse for its kinds.
  auto entries_of_config = [&](const GSHSCENE_Dump_Sink_Config & config, std::vector<std::vector<size_t>> * rows) {
    Scene s;
    GSHSCENE_List * l = s.open();
    const double box[4] = {0.0, 0.0, 1.0, 1.0};
    EXPECT_EQ(gshscene_list_placeholder(l, 1, 1, 0, box, nullptr, 0, 0.0), GSHSCENE_OK);
    EXPECT_EQ(gshscene_list_placeholder(l, 1, 2, 1, nullptr, nullptr, 0, 0.0), GSHSCENE_OK);
    EXPECT_EQ(gshscene_list_close(l), GSHSCENE_OK);
    SinkRef sink(make_with(config, nullptr, nullptr));
    auto stream = make_stream(GSHSCENE_UNIT_MILLIMETER);
    ReportRef report;
    EXPECT_EQ(render_job(sink.sink, request_for(l, *stream, report.report)).result, GSHSCENE_OK);
    for (size_t i = 0; i < gshscene_report_count(report.report); i++) {
      GSHSCENE_Report_Entry e{};
      EXPECT_EQ(gshscene_report_entry(report.report, i, &e), GSHSCENE_OK);
      rows->push_back({(size_t)e.entry_class, (size_t)e.reason, e.second, e.chain_count ? e.chain[0] : 99});
    }
  };
  GSHSCENE_Dump_Sink_Config all = config_default();
  all.stream_caps.item_kinds = 0xffffffffu;
  GSHSCENE_Dump_Sink_Config none = config_default();
  none.stream_caps.item_kinds = 0;
  std::vector<std::vector<size_t>> a, b;
  entries_of_config(all, &a);
  entries_of_config(none, &b);
  ASSERT_EQ(a.size(), 2u);
  EXPECT_EQ(a, b);
  EXPECT_EQ(a[0], (std::vector<size_t>{GSHSCENE_ENTRY_REFUSED, GSHSCENE_REASON_PLACEHOLDER, (size_t)65537, 0}));
  EXPECT_EQ(a[1], (std::vector<size_t>{GSHSCENE_ENTRY_APPROXIMATED, GSHSCENE_REASON_PLACEHOLDER, (size_t)65538, 1}));
}

namespace {

/** The outline hash of a square's stroke as shape-geometry gives it, called directly: the style copied field by field. */
uint64_t direct_outline(double x, double y, double size, const GSHSCENE_Stroke_Style & st, const std::vector<double> & dashes,
    double scale, const GSHGEOM_Map * pixels_map = nullptr) {
  GSHGEOM_Stroke g;
  gshgeom_stroke_init(&g);
  g.width = st.width;
  g.width_space = st.width_space;
  g.join = st.join;
  g.miter_limit = st.miter_limit;
  g.cap = st.cap;
  g.degenerate = st.degenerate;
  g.dash_count = dashes.size();
  g.dashes = dashes.empty() ? nullptr : dashes.data();
  g.dash_offset = st.dash_offset;
  g.dash_scope = st.dash_scope;
  g.alignment = st.alignment;
  g.alignment_rule = st.alignment_rule;
  // The canonical stroke's canonical form (SK3) for the style the fixture interns: a butt cap resets nothing here.
  GSHGEOM_Map map;
  gshgeom_map_scale(2, scale, scale, 1.0, &map);
  if (pixels_map) {
    map = *pixels_map;
  }
  GSHGEOM_Map canon;
  int64_t sx = 0;
  int64_t sy = 0;
  EXPECT_EQ(gshgeom_stroke_canonical_map(&map, &canon, &sx, &sy), GSHGEOM_OK);
  GSHGEOM_Path * p = nullptr;
  EXPECT_EQ(gshgeom_path_create(nullptr, nullptr, 2, &p), GSHGEOM_OK);
  GSHGEOM_Point a = {x, y, 0.0};
  GSHGEOM_Point b = {x + size, y, 0.0};
  GSHGEOM_Point c = {x + size, y + size, 0.0};
  GSHGEOM_Point d = {x, y + size, 0.0};
  gshgeom_path_move_to(p, a);
  gshgeom_path_line_to(p, b);
  gshgeom_path_line_to(p, c);
  gshgeom_path_line_to(p, d);
  gshgeom_path_close(p);
  GSHGEOM_Stroke_Outline * o = nullptr;
  EXPECT_EQ(gshgeom_stroke_expand(p, &g, &canon, nullptr, nullptr, &o), GSHGEOM_OK);
  uint64_t h = gshgeom_stroke_outline_hash(o);
  gshgeom_stroke_outline_destroy(o);
  gshgeom_path_destroy(p);
  return h;
}

}  // namespace

TEST(DumpSink, TheOutlineFieldIsShapeGeometrysHashOfTheCanonicalExpansion) {
  // SC10: the field is gshgeom_stroke_outline_hash of the expansion at the canonical map of the composed map in pixels,
  // checked here against shape-geometry called directly, whatever the generator's committed values say.
  Scene s;
  const GSHSCENE_List * list = stroke_list(s);
  GSHSCENE_Stroke_Style a;
  gshscene_stroke_style_init(&a);
  GSHSCENE_Stroke_Style b = a;
  b.width = 2.0;
  b.width_space = GSHSCENE_STROKE_WIDTH_DEVICE;
  b.join = GSHSCENE_STROKE_JOIN_ROUND;
  b.cap = GSHSCENE_STROKE_CAP_ROUND;
  GSHSCENE_Stroke_Style c = a;
  c.width = 1.5;
  c.join = GSHSCENE_STROKE_JOIN_BEVEL;
  c.cap = GSHSCENE_STROKE_CAP_SQUARE;
  c.degenerate = GSHSCENE_STROKE_DEGENERATE_OMIT;
  c.dash_offset = -0.5;
  c.dash_scope = GSHSCENE_STROKE_DASH_PATH;
  c.alignment = GSHSCENE_STROKE_ALIGN_INSIDE;
  c.alignment_rule = GSHGEOM_FILL_EVEN_ODD;
  struct Want {
    int index;
    double x, y, size;
    const GSHSCENE_Stroke_Style * style;
    std::vector<double> dashes;
  } wants[] = {{0, 2, 2, 8, &a, {}}, {1, 10, 4, 6, &b, {}}, {2, 20, 20, 6, &a, {}},
      {4, 40, 40, 4, &c, {1.0, 2.0, 3.0, 1.0, 2.0, 3.0}}};
  for (int kind : {0, 1}) {
    SinkRef sink(make_with(config_default(), nullptr, nullptr));
    ReportRef report;
    Log log;
    GSHSCENE_Dump_Sink_Config cfg = config_default();
    cfg.log = &log.writer;
    SinkRef raster_sink(make_with(cfg, nullptr, nullptr));
    std::string text;
    double scale = 1.0;
    if (kind == 0) {
      auto raster = make_raster();
      ASSERT_EQ(render_job(raster_sink.sink, request_for(list, *raster, report.report)).result, GSHSCENE_OK);
      text = log.text;
    } else {
      auto stream = make_stream(GSHSCENE_UNIT_MILLIMETER);
      gshgeom_map_scale(2, 0.5, 0.5, 1.0, &stream->target.map);
      ASSERT_EQ(render_job(sink.sink, request_for(list, *stream, report.report)).result, GSHSCENE_OK);
      text = stream->bytes;
      scale = 0.5 / gshscene_stroke_pixel_size(GSHSCENE_UNIT_MILLIMETER);
    }
    for (const Want & w : wants) {
      char want[40];
      std::snprintf(want, sizeof want, "outline=%016llx",
          (unsigned long long)direct_outline(w.x, w.y, w.size, *w.style, w.dashes, scale));
      std::string line = "U " + std::to_string(w.index) + " K ";
      size_t at = text.find(line);
      ASSERT_NE(at, std::string::npos) << text;
      size_t end = text.find('\n', at);
      EXPECT_NE(text.substr(at, end - at).find(want), std::string::npos)
          << "unit " << w.index << " kind " << kind << ": " << text.substr(at, end - at) << " wants " << want;
    }
  }
}

TEST(DumpSink, TheOutlineFieldFollowsAPixelTargetsMapAndTheCanonicalSplitOfItsTranslation) {
  // The same square strokes under a pixel target whose map scales and moves by a whole and a fractional part: the field
  // is the hash of the expansion at the canonical map of that map (the whole part of the move is split off), not of
  // the identity and not of the map as it stands.
  Scene s;
  const GSHSCENE_List * list = stroke_list(s);
  GSHSCENE_Stroke_Style a;
  gshscene_stroke_style_init(&a);
  GSHGEOM_Map scale;
  gshgeom_map_scale(2, 1.5, 1.5, 1.0, &scale);
  GSHGEOM_Map move;
  gshgeom_map_translate(2, 37.25, -11.5, 0.0, &move);
  GSHGEOM_Map target;
  ASSERT_EQ(gshgeom_map_compose(&move, &scale, &target), GSHGEOM_OK);
  SinkRef sink(make_with(config_default(), nullptr, nullptr));
  auto stream = make_stream(GSHSCENE_UNIT_PIXEL);
  stream->target.map = target;
  ReportRef report;
  ASSERT_EQ(render_job(sink.sink, request_for(list, *stream, report.report)).result, GSHSCENE_OK);
  char want[40];
  std::snprintf(want, sizeof want, "outline=%016llx", (unsigned long long)direct_outline(2, 2, 8, a, {}, 1.0, &target));
  const std::string line = "U 0 K ";
  const size_t at = stream->bytes.find(line);
  ASSERT_NE(at, std::string::npos) << stream->bytes;
  const size_t end = stream->bytes.find('\n', at);
  EXPECT_NE(stream->bytes.substr(at, end - at).find(want), std::string::npos)
      << stream->bytes.substr(at, end - at) << " wants " << want;
  // The map matters: the identity's and the scale-only map's hashes are other numbers.
  EXPECT_NE(direct_outline(2, 2, 8, a, {}, 1.0, &target), direct_outline(2, 2, 8, a, {}, 1.0));
  EXPECT_NE(direct_outline(2, 2, 8, a, {}, 1.0, &target), direct_outline(2, 2, 8, a, {}, 1.5));
}

namespace {

/** The `outline=` field of the first stroke record of @p text, or empty. */
std::string outline_field(const std::string & text) {
  size_t at = text.find(" outline=");
  if (at == std::string::npos) {
    return "";
  }
  size_t end = text.find('\n', at);
  return text.substr(at + 9, end - at - 9);
}

/** The hash of the expansion of the path @p p under @p style at the identity map in pixels, shape-geometry called directly. */
uint64_t direct_path_outline(const GSHGEOM_Path * p, const GSHSCENE_Resource * style) {
  GSHGEOM_Stroke g;
  EXPECT_EQ(gshscene_resource_stroke_geometry(style, &g), GSHSCENE_OK);
  GSHGEOM_Map id;
  gshgeom_map_identity(2, &id);
  GSHGEOM_Map canon;
  int64_t sx = 0;
  int64_t sy = 0;
  EXPECT_EQ(gshgeom_stroke_canonical_map(&id, &canon, &sx, &sy), GSHGEOM_OK);
  GSHGEOM_Stroke_Outline * o = nullptr;
  EXPECT_EQ(gshgeom_stroke_expand(p, &g, &canon, nullptr, nullptr, &o), GSHGEOM_OK);
  uint64_t h = gshgeom_stroke_outline_hash(o);
  gshgeom_stroke_outline_destroy(o);
  return h;
}

}  // namespace

TEST(DumpSink, AStrokeOfARunRecordsTheHashOfTheExpansionOfItsCenterlineAndAnotherConfigurationMayNotStrokeRuns) {
  // ST10: `outline=` is the hash of the expansion of ST1's path at the canonical map, exactly as for a path; a
  // configuration that does not say STROKE_RUNS refuses the stroke in the plan, for its centerline (ST5).
  Scene s;
  GSHSCENE_Stroke_Style st;
  gshscene_stroke_style_init(&st);
  st.width = 1.5;
  st.join = GSHSCENE_STROKE_JOIN_ROUND; // a pen follows it: round join and butt cap are native there
  const GSHSCENE_Resource * style = nullptr;
  ASSERT_EQ(gshscene_store_intern_stroke_style(s.store, &st, &style), GSHSCENE_OK);
  s.held.push_back(style);
  GSHSCENE_Paint red = solid(1, 0, 0);
  const GSHSCENE_Resource * text = s.glyphs(s.face(), {1, 2}, 8.0, GSHSCENE_HINT_NONE);
  GSHSCENE_List * l = s.open();
  ASSERT_EQ(gshscene_list_stroke(l, text, style, nullptr, &red, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  // The centerline is ST1's path, interned as a path of its own: a stroke of it is the same stroke (ST11).
  GSHGEOM_Path * centerline = nullptr;
  ASSERT_EQ(gshscene_glyph_run_centerline(text, nullptr, nullptr, &centerline, nullptr), GSHSCENE_OK);
  const GSHSCENE_Resource * as_path = nullptr;
  ASSERT_EQ(gshscene_store_intern_path(s.store, centerline, &as_path), GSHSCENE_OK);
  s.held.push_back(as_path);
  GSHSCENE_List * lp = s.open();
  ASSERT_EQ(gshscene_list_stroke(lp, as_path, style, nullptr, &red, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(lp), GSHSCENE_OK);
  char want[40];
  std::snprintf(want, sizeof want, "%016llx", (unsigned long long)direct_path_outline(centerline, style));
  gshgeom_path_destroy(centerline);
  for (const GSHSCENE_Dump_Sink_Config & config : {config_default(), config_writer_like(), config_toolpath_like()}) {
    SinkRef sink(make_with(config, nullptr, nullptr));
    auto stream = make_stream(GSHSCENE_UNIT_PIXEL);
    ReportRef report;
    ASSERT_EQ(render_job(sink.sink, request_for(l, *stream, report.report)).result, GSHSCENE_OK);
    EXPECT_NE(stream->bytes.find(" K res=glyph-run:"), std::string::npos) << stream->bytes;
    EXPECT_EQ(outline_field(stream->bytes), want) << stream->bytes;
    EXPECT_EQ(stream->bytes.find(" Y "), std::string::npos) << "a stroke takes no route: no glyph record (ST10)";
    EXPECT_EQ(entries(report.report).size(), 0u);
    auto stream_p = make_stream(GSHSCENE_UNIT_PIXEL);
    ReportRef report_p;
    ASSERT_EQ(render_job(sink.sink, request_for(lp, *stream_p, report_p.report)).result, GSHSCENE_OK);
    EXPECT_EQ(outline_field(stream_p->bytes), want) << "the stroke of the run is the stroke of its path";
  }
  SinkRef pen(make_with(config_no_run_strokes(), nullptr, nullptr));
  auto stream2 = make_stream(GSHSCENE_UNIT_PIXEL);
  ReportRef report2;
  ASSERT_EQ(render_job(pen.sink, request_for(l, *stream2, report2.report)).result, GSHSCENE_OK);
  EXPECT_EQ(stream2->bytes.find(" K res="), std::string::npos) << "refused in the plan, never recorded";
  GSHSCENE_Report_Summary sum2{};
  ASSERT_EQ(gshscene_report_summary(report2.report, &sum2), GSHSCENE_OK);
  ASSERT_EQ(sum2.by_class[GSHSCENE_ENTRY_REFUSED], 1u);
  GSHSCENE_Report_Entry e;
  ASSERT_EQ(gshscene_report_entry(report2.report, 0, &e), GSHSCENE_OK);
  EXPECT_EQ(e.reason, GSHSCENE_REASON_STROKE_CENTERLINE);
  // A run from outlines is a centerline too.
  GSHSCENE_Glyph oglyphs[2] = {{1, 0.0, 0.0, s.sq(0, 0, 4)}, {2, 7.0, 1.0, s.sq(0, 0, 3)}};
  GSHSCENE_Glyph_Run orun{};
  orun.source = GSHSCENE_GLYPHS_FROM_OUTLINES;
  orun.glyph_count = 2;
  orun.glyphs = oglyphs;
  const GSHSCENE_Resource * otext = nullptr;
  ASSERT_EQ(gshscene_store_intern_glyph_run(s.store, &orun, &otext), GSHSCENE_OK);
  s.held.push_back(otext);
  GSHSCENE_List * lo = s.open();
  ASSERT_EQ(gshscene_list_stroke(lo, otext, style, nullptr, &red, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(lo), GSHSCENE_OK);
  SinkRef full(make_with(config_default(), nullptr, nullptr));
  auto stream3 = make_stream(GSHSCENE_UNIT_PIXEL);
  ReportRef report3;
  ASSERT_EQ(render_job(full.sink, request_for(lo, *stream3, report3.report)).result, GSHSCENE_OK);
  EXPECT_EQ(outline_field(stream3->bytes).size(), 16u) << stream3->bytes;
}

TEST(DumpSink, AGlyphOfAStrokedRunTheProviderCannotGiveIsANoteAndAnUnboundFaceIsRefusedByThePlan) {
  Scene s;
  GSHSCENE_Stroke_Style st;
  gshscene_stroke_style_init(&st);
  st.width = 1.0;
  const GSHSCENE_Resource * style = nullptr;
  ASSERT_EQ(gshscene_store_intern_stroke_style(s.store, &st, &style), GSHSCENE_OK);
  s.held.push_back(style);
  GSHSCENE_Paint red = solid(1, 0, 0);
  // Glyph 4 is corrupt: its note (second = 1) is reported; the others are stroked, as in the path of the two.
  const GSHSCENE_Resource * face = s.face();
  auto make_run = [&](const GSHSCENE_Resource * f, std::vector<GSHSCENE_Glyph> g, uint32_t mode) {
    GSHSCENE_Glyph_Run run{};
    run.source = GSHSCENE_GLYPHS_FROM_FONT;
    run.face = f;
    run.size = 8.0;
    run.hint_mode = mode;
    run.glyph_count = g.size();
    run.glyphs = g.data();
    const GSHSCENE_Resource * r = nullptr;
    EXPECT_EQ(gshscene_store_intern_glyph_run(s.store, &run, &r), GSHSCENE_OK);
    s.held.push_back(r);
    return r;
  };
  const GSHSCENE_Resource * three =
      make_run(face, {{1, 2.0, 40.0, nullptr}, {4, 13.0, 40.0, nullptr}, {2, 24.0, 40.0, nullptr}}, GSHSCENE_HINT_NONE);
  const GSHSCENE_Resource * two =
      make_run(face, {{1, 2.0, 40.0, nullptr}, {2, 24.0, 40.0, nullptr}}, GSHSCENE_HINT_NONE);
  GSHSCENE_List * la = s.open();
  ASSERT_EQ(gshscene_list_stroke(la, three, style, nullptr, &red, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(la), GSHSCENE_OK);
  GSHSCENE_List * lb = s.open();
  ASSERT_EQ(gshscene_list_stroke(lb, two, style, nullptr, &red, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(lb), GSHSCENE_OK);
  SinkRef sink(make_with(config_default(), nullptr, nullptr));
  auto sa = make_stream(GSHSCENE_UNIT_PIXEL);
  ReportRef ra;
  ASSERT_EQ(render_job(sink.sink, request_for(la, *sa, ra.report)).result, GSHSCENE_OK);
  auto sb = make_stream(GSHSCENE_UNIT_PIXEL);
  ReportRef rb;
  ASSERT_EQ(render_job(sink.sink, request_for(lb, *sb, rb.report)).result, GSHSCENE_OK);
  EXPECT_EQ(outline_field(sa->bytes), outline_field(sb->bytes)) << "the corrupt glyph adds nothing";
  auto got = entries(ra.report);
  ASSERT_EQ(got.size(), 1u);
  EXPECT_EQ(got[0].entry_class, GSHSCENE_ENTRY_REFUSED);
  EXPECT_EQ(got[0].reason, GSHSCENE_REASON_GLYPH_UNAVAILABLE);
  EXPECT_EQ(got[0].second, 1u);
  EXPECT_EQ(entries(rb.report).size(), 0u);
  // Under strict a refused glyph stops the render at the item, before it is recorded.
  auto ss = make_stream(GSHSCENE_UNIT_PIXEL);
  ReportRef rs;
  GSHSCENE_Render_Request req = request_for(la, *ss, rs.report);
  req.flags = GSHSCENE_RENDER_STRICT;
  EXPECT_EQ(render_job(sink.sink, req).result, GSHSCENE_ERR_REFUSED);
  EXPECT_EQ(ss->bytes.find(" K res="), std::string::npos);
  // An unbound face: the plan refuses (ST5), whatever the hint mode; strict fails before the sink is called.
  const GSHSCENE_Resource * unbound = make_run(s.face(1, 0), {{1, 2.0, 40.0, nullptr}}, GSHSCENE_HINT_FONT_STRICT);
  GSHSCENE_List * lu = s.open();
  ASSERT_EQ(gshscene_list_stroke(lu, unbound, style, nullptr, &red, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(lu), GSHSCENE_OK);
  auto su = make_stream(GSHSCENE_UNIT_PIXEL);
  ReportRef ru;
  EXPECT_EQ(render_job(sink.sink, request_for(lu, *su, ru.report)).result, GSHSCENE_OK);
  auto gu = entries(ru.report);
  ASSERT_EQ(gu.size(), 1u);
  EXPECT_EQ(gu[0].entry_class, GSHSCENE_ENTRY_REFUSED);
  EXPECT_EQ(gu[0].reason, GSHSCENE_REASON_FACE_UNBOUND);
  EXPECT_EQ(su->bytes.find(" K res="), std::string::npos);
  auto su2 = make_stream(GSHSCENE_UNIT_PIXEL);
  ReportRef ru2;
  GSHSCENE_Render_Request req2 = request_for(lu, *su2, ru2.report);
  req2.flags = GSHSCENE_RENDER_STRICT;
  EXPECT_EQ(render_job(sink.sink, req2).result, GSHSCENE_ERR_REFUSED);
  EXPECT_EQ(su2->bytes.size(), 0u) << "strict failed before the target was touched";
}

TEST(DumpSink, AStrokeOfARunNeedsNoHintStatusAndMakesNoFastTierCall) {
  // ST3: a strict-hinted run at a hinting size on a grid-aligned map is stroked on the general route alone.
  Scene s;
  GSHSCENE_Stroke_Style st;
  gshscene_stroke_style_init(&st);
  st.width = 1.0;
  const GSHSCENE_Resource * style = nullptr;
  ASSERT_EQ(gshscene_store_intern_stroke_style(s.store, &st, &style), GSHSCENE_OK);
  s.held.push_back(style);
  GSHSCENE_Paint red = solid(1, 0, 0);
  const GSHSCENE_Resource * text = s.glyphs(s.face(), {1, 2}, 16.0, GSHSCENE_HINT_FONT_STRICT);
  GSHSCENE_List * l = s.open();
  ASSERT_EQ(gshscene_list_stroke(l, text, style, nullptr, &red, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  SinkRef sink(make_with(config_default(), nullptr, nullptr));
  auto stream = make_stream(GSHSCENE_UNIT_PIXEL);
  ReportRef report;
  ASSERT_EQ(render_job(sink.sink, request_for(l, *stream, report.report)).result, GSHSCENE_OK);
  EXPECT_EQ(entries(report.report).size(), 0u);
  EXPECT_EQ(gcu_atomic_size_load(&s.provider.fast_calls), 0u);
  EXPECT_GT(gcu_atomic_size_load(&s.provider.outline_calls), 0u);
}

TEST(DumpSink, AStrokeOfARunHasNoCenterlineForAHostilePathAndOneOfItIsRefusedAsTheSinksLimit) {
  // A zigzag of 60000 segments under a wide round-join stroke expands past the sink's cap of points: the sink's own limit.
  {
    Scene s;
    GSHGEOM_Path * p = nullptr;
    ASSERT_EQ(gshgeom_path_create(nullptr, nullptr, 2, &p), GSHGEOM_OK);
    gshgeom_path_move_to(p, GSHGEOM_Point{0, 0, 0});
    for (int i = 1; i <= 60000; i++) {
      gshgeom_path_line_to(p, GSHGEOM_Point{(double)i * 0.01, (i % 2) ? 5.0 : 0.0, 0});
    }
    const GSHSCENE_Resource * path = nullptr;
    ASSERT_EQ(gshscene_store_intern_path(s.store, p, &path), GSHSCENE_OK);
    gshgeom_path_destroy(p);
    s.held.push_back(path);
    GSHSCENE_Stroke_Style st;
    gshscene_stroke_style_init(&st);
    st.width = 3.0;
    st.join = GSHSCENE_STROKE_JOIN_ROUND;
    st.cap = GSHSCENE_STROKE_CAP_ROUND;
    const GSHSCENE_Resource * style = nullptr;
    ASSERT_EQ(gshscene_store_intern_stroke_style(s.store, &st, &style), GSHSCENE_OK);
    s.held.push_back(style);
    GSHSCENE_Paint red = solid(1, 0, 0);
    GSHSCENE_List * l = s.open();
    ASSERT_EQ(gshscene_list_stroke(l, path, style, nullptr, &red, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
    SinkRef sink(make_with(config_default(), nullptr, nullptr));
    auto stream = make_stream(GSHSCENE_UNIT_PIXEL);
    ReportRef report;
    ASSERT_EQ(render_job(sink.sink, request_for(l, *stream, report.report)).result, GSHSCENE_OK);
    EXPECT_NE(stream->bytes.find(" X class=" + std::to_string((int)GSHSCENE_ENTRY_REFUSED) + " reason=" +
                  std::to_string((int)GSHSCENE_REASON_RESOURCE_LIMIT)),
        std::string::npos)
        << stream->bytes;
    EXPECT_EQ(stream->bytes.find(" K res="), std::string::npos);
    // Under strict the refusal stops the render there.
    auto strict_stream = make_stream(GSHSCENE_UNIT_PIXEL);
    ReportRef strict_report;
    GSHSCENE_Render_Request strict = request_for(l, *strict_stream, strict_report.report);
    strict.flags = GSHSCENE_RENDER_STRICT;
    EXPECT_EQ(render_job(sink.sink, strict).result, GSHSCENE_ERR_REFUSED);
  }
}

// A zigzag of @p segments under a wide round-join stroke, rendered to a stream by a sink whose cap of stroke points is
// @p max_stroke_points (0 is the default).  True when the stroke was drawn, false when it was refused as the sink's limit.
static bool zigzag_stroke_drawn(int segments, size_t max_stroke_points) {
  Scene s;
  GSHGEOM_Path * p = nullptr;
  EXPECT_EQ(gshgeom_path_create(nullptr, nullptr, 2, &p), GSHGEOM_OK);
  gshgeom_path_move_to(p, GSHGEOM_Point{0, 0, 0});
  for (int i = 1; i <= segments; i++) {
    gshgeom_path_line_to(p, GSHGEOM_Point{(double)i * 0.01, (i % 2) ? 5.0 : 0.0, 0});
  }
  const GSHSCENE_Resource * path = nullptr;
  EXPECT_EQ(gshscene_store_intern_path(s.store, p, &path), GSHSCENE_OK);
  gshgeom_path_destroy(p);
  s.held.push_back(path);
  GSHSCENE_Stroke_Style st;
  gshscene_stroke_style_init(&st);
  st.width = 3.0;
  st.join = GSHSCENE_STROKE_JOIN_ROUND;
  st.cap = GSHSCENE_STROKE_CAP_ROUND;
  const GSHSCENE_Resource * style = nullptr;
  EXPECT_EQ(gshscene_store_intern_stroke_style(s.store, &st, &style), GSHSCENE_OK);
  s.held.push_back(style);
  GSHSCENE_Paint red = solid(1, 0, 0);
  GSHSCENE_List * l = s.open();
  EXPECT_EQ(gshscene_list_stroke(l, path, style, nullptr, &red, 0, 0.0), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  GSHSCENE_Dump_Sink_Config c = config_default();
  c.max_stroke_points = max_stroke_points;
  SinkRef sink(make_with(c, nullptr, nullptr));
  auto stream = make_stream(GSHSCENE_UNIT_PIXEL);
  ReportRef report;
  EXPECT_EQ(render_job(sink.sink, request_for(l, *stream, report.report)).result, GSHSCENE_OK);
  bool refused = stream->bytes.find(" reason=" + std::to_string((int)GSHSCENE_REASON_RESOURCE_LIMIT)) != std::string::npos;
  bool drawn = stream->bytes.find(" K res=") != std::string::npos;
  EXPECT_NE(refused, drawn) << stream->bytes;
  return drawn;
}

TEST(DumpSink, AStrokeIsRefusedByItsCapOfPointsAndNotByTheBytesItTakes) {
  // The cap is a count of points, which is the same on every target; the bytes an expansion takes differ with the word size
  // and decide nothing.  200 segments are drawn by default and refused under a cap of 1000 points; 8000 are refused by
  // default (past 131072 points) and drawn under a cap of eight times that.
  EXPECT_TRUE(zigzag_stroke_drawn(200, 0));
  EXPECT_TRUE(zigzag_stroke_drawn(200, 131072)) << "0 is the default, 131072";
  EXPECT_FALSE(zigzag_stroke_drawn(200, 1000));
  EXPECT_FALSE(zigzag_stroke_drawn(8000, 0));
  EXPECT_TRUE(zigzag_stroke_drawn(8000, 1048576));
}

TEST(DumpSink, ACapOfStrokePointsTooLargeToGiveScratchIsRefusedAtCreation) {
  GSHSCENE_Dump_Sink_Config c = config_default();
  c.max_stroke_points = SIZE_MAX / 256;
  GSHSCENE_Sink * sink = nullptr;
  EXPECT_EQ(gshscene_dump_sink_create(nullptr, nullptr, &c, &sink), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(sink, nullptr);
}

TEST(DumpSink, AStrokePastTheExtentAndAStrokeOfAnUnboundFaceAreRefusedWhileDrawing) {
  Scene s;
  const GSHSCENE_List * list = stroke_list(s);
  GSHSCENE_Dump_Sink_Config c = config_default();
  c.max_extent = 9.0; // the first stroke's box is about 12 wide; the second's (device, round) is about 7
  Log log;
  c.log = &log.writer;
  SinkRef sink(make_with(c, nullptr, nullptr));
  auto raster = make_raster();
  ReportRef report;
  ASSERT_EQ(render_job(sink.sink, request_for(list, *raster, report.report)).result, GSHSCENE_OK);
  auto refused_record = [](GSHSCENE_Reason reason) {
    return " X class=" + std::to_string((int)GSHSCENE_ENTRY_REFUSED) + " reason=" + std::to_string((int)reason) + "\n";
  };
  EXPECT_NE(log.text.find(refused_record(GSHSCENE_REASON_RESOURCE_LIMIT)), std::string::npos)
      << "the refusal is recorded where it happened";
  // Under strict the refusal while drawing stops the render there (S6).
  auto strict_raster = make_raster();
  ReportRef strict_report;
  GSHSCENE_Render_Request strict = request_for(list, *strict_raster, strict_report.report);
  strict.flags = GSHSCENE_RENDER_STRICT;
  EXPECT_EQ(render_job(sink.sink, strict).result, GSHSCENE_ERR_REFUSED);
  auto es = entries(report.report);
  ASSERT_GE(es.size(), 1u);
  EXPECT_EQ(es[0].entry_class, GSHSCENE_ENTRY_REFUSED);
  EXPECT_EQ(es[0].reason, GSHSCENE_REASON_RESOURCE_LIMIT);
  EXPECT_EQ(es[0].chain[0], 0u);
  bool second_refused = false;
  for (const GSHSCENE_Report_Entry & e : es) {
    second_refused = second_refused || (e.chain[0] == 1u && e.entry_class == GSHSCENE_ENTRY_REFUSED);
  }
  EXPECT_FALSE(second_refused);
  // A run from an unbound face: the plan routes no stroke (SK7), so the sink refuses it, FACE_UNBOUND.
  Scene t;
  GSHSCENE_Stroke_Style st;
  gshscene_stroke_style_init(&st);
  st.join = GSHSCENE_STROKE_JOIN_ROUND;
  const GSHSCENE_Resource * style = nullptr;
  ASSERT_EQ(gshscene_store_intern_stroke_style(t.store, &st, &style), GSHSCENE_OK);
  t.held.push_back(style);
  GSHSCENE_List * l = t.open();
  GSHSCENE_Paint red = solid(1, 0, 0);
  ASSERT_EQ(gshscene_list_stroke(l, t.glyphs(t.face(1, 0), {1}, 8.0, GSHSCENE_HINT_FONT), style, nullptr, &red, 0, 0.0),
      GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  Log log2;
  GSHSCENE_Dump_Sink_Config c2 = config_default();
  c2.log = &log2.writer;
  SinkRef full(make_with(c2, nullptr, nullptr));
  auto r2 = make_raster();
  ReportRef rep2;
  ASSERT_EQ(render_job(full.sink, request_for(l, *r2, rep2.report)).result, GSHSCENE_OK);
  auto e2 = entries(rep2.report);
  ASSERT_EQ(e2.size(), 1u);
  EXPECT_EQ(e2[0].entry_class, GSHSCENE_ENTRY_REFUSED);
  EXPECT_EQ(e2[0].reason, GSHSCENE_REASON_FACE_UNBOUND);
  EXPECT_NE(log2.text.find(refused_record(GSHSCENE_REASON_FACE_UNBOUND)), std::string::npos);
}

TEST(DumpSink, AStrokePastTheExtentInOneDirectionAloneIsRefused) {
  Scene s;
  GSHSCENE_Stroke_Style st;
  gshscene_stroke_style_init(&st);
  st.join = GSHSCENE_STROKE_JOIN_ROUND;
  const GSHSCENE_Resource * style = nullptr;
  ASSERT_EQ(gshscene_store_intern_stroke_style(s.store, &st, &style), GSHSCENE_OK);
  s.held.push_back(style);
  // A wide, flat rectangle: 20 by 2 (with the pen 21 by 3), against an extent of 10; and its transpose.
  for (bool tall : {false, true}) {
    GSHGEOM_Path * p = nullptr;
    ASSERT_EQ(gshgeom_path_create(nullptr, nullptr, 2, &p), GSHGEOM_OK);
    double w = tall ? 2 : 20, h = tall ? 20 : 2;
    gshgeom_path_move_to(p, GSHGEOM_Point{4, 4, 0});
    gshgeom_path_line_to(p, GSHGEOM_Point{4 + w, 4, 0});
    gshgeom_path_line_to(p, GSHGEOM_Point{4 + w, 4 + h, 0});
    gshgeom_path_close(p);
    const GSHSCENE_Resource * line = nullptr;
    ASSERT_EQ(gshscene_store_intern_path(s.store, p, &line), GSHSCENE_OK);
    gshgeom_path_destroy(p);
    s.held.push_back(line);
    GSHSCENE_List * l = s.open();
    GSHSCENE_Paint red = solid(1, 0, 0);
    ASSERT_EQ(gshscene_list_stroke(l, line, style, nullptr, &red, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
    GSHSCENE_Dump_Sink_Config c = config_default();
    c.max_extent = 10.0;
    SinkRef sink(make_with(c, nullptr, nullptr));
    auto raster = make_raster();
    ReportRef report;
    ASSERT_EQ(render_job(sink.sink, request_for(l, *raster, report.report)).result, GSHSCENE_OK);
    auto es = entries(report.report);
    ASSERT_EQ(es.size(), 1u) << (tall ? "tall" : "wide");
    EXPECT_EQ(es[0].reason, GSHSCENE_REASON_RESOURCE_LIMIT) << (tall ? "tall" : "wide");
  }
}

TEST(DumpSink, AStyleIsRecordedOncePerRenderAndAnEmptyOrGradientStrokeIsSpelledAsDefined) {
  Scene s;
  GSHSCENE_Stroke_Style st;
  gshscene_stroke_style_init(&st);
  st.join = GSHSCENE_STROKE_JOIN_ROUND;
  const double dashes[2] = {0.0, 2.0}; // a zero dash among others is kept, its bits all zero digits
  st.dash_count = 2;
  st.dashes = dashes;
  const GSHSCENE_Resource * style = nullptr;
  ASSERT_EQ(gshscene_store_intern_stroke_style(s.store, &st, &style), GSHSCENE_OK);
  s.held.push_back(style);
  GSHGEOM_Path * nothing = nullptr;
  ASSERT_EQ(gshgeom_path_create(nullptr, nullptr, 2, &nothing), GSHGEOM_OK);
  const GSHSCENE_Resource * empty = nullptr;
  ASSERT_EQ(gshscene_store_intern_path(s.store, nothing, &empty), GSHSCENE_OK);
  gshgeom_path_destroy(nothing);
  s.held.push_back(empty);
  GSHSCENE_Gradient g{};
  g.kind = GSHSCENE_GRADIENT_LINEAR;
  g.p1[0] = 10.0;
  gshgeom_map_identity(2, &g.map);
  GSHSCENE_Gradient_Stop stops[2] = {{0.0, solid(1, 0, 0)}, {1.0, solid(0, 0, 1)}};
  g.stops = stops;
  g.stop_count = 2;
  const GSHSCENE_Resource * gradient = nullptr;
  ASSERT_EQ(gshscene_store_intern_gradient(s.store, &g, &gradient), GSHSCENE_OK);
  s.held.push_back(gradient);
  GSHSCENE_List * l = s.open();
  GSHSCENE_Paint red = solid(1, 0, 0);
  GSHSCENE_Paint shaded{};
  shaded.kind = GSHSCENE_PAINT_GRADIENT;
  shaded.resource = gradient;
  shaded.rgba[3] = 1.0;
  ASSERT_EQ(gshscene_list_stroke(l, s.sq(2, 2, 6), style, nullptr, &red, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_stroke(l, s.sq(12, 2, 6), style, nullptr, &shaded, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_stroke(l, empty, style, nullptr, &red, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  SinkRef sink(make_full(nullptr, nullptr));
  auto stream = make_stream(GSHSCENE_UNIT_PIXEL);
  ReportRef report;
  ASSERT_EQ(render_job(sink.sink, request_for(l, *stream, report.report)).result, GSHSCENE_OK);
  const std::string & out = stream->bytes;
  auto count = [&](const std::string & what) {
    size_t n = 0;
    for (size_t at = out.find(what); at != std::string::npos; at = out.find(what, at + 1)) {
      n++;
    }
    return n;
  };
  EXPECT_EQ(count("\nQ "), 1u) << "one style, three strokes: one record (S20)\n" << out;
  EXPECT_EQ(count(" K res="), 3u) << out;
  EXPECT_EQ(count(" grad=" + std::to_string(gshscene_resource_id(gradient)) + " box="), 1u) << out;
  EXPECT_EQ(count("\nR " + std::to_string(gshscene_resource_id(gradient)) + " "), 1u) << "the gradient's record" << out;
  EXPECT_EQ(count(" box=- expand=0 outline="), 1u) << "an empty centerline has an empty box" << out;
  EXPECT_EQ(count("\nd 0000000000000000\nd 4000000000000000\n"), 1u) << "the dashes in sixteen digits" << out;
}

TEST(DumpSink, AGlyphPastTheScratchIsRefusedAsTheSinksOwnLimit) {
  // Glyph 8's 3000 lines do not fit the 64 KiB of a glyph's scratch, on either route: refused, the rest drawn.
  for (uint32_t kind : {(uint32_t)GSHSCENE_TARGET_RASTER, (uint32_t)GSHSCENE_TARGET_STREAM}) {
    Scene s;
    const GSHSCENE_List * list = one_run(s, s.glyphs(s.face(), {8, 1}, 8.0, GSHSCENE_HINT_NONE));
    SinkRef sink(make_full(nullptr, nullptr));
    auto surface = make_surface(kind);
    ReportRef report;
    ASSERT_EQ(render_job(sink.sink, request_for(list, *surface, report.report)).result, GSHSCENE_OK);
    auto refused = of_class(report.report, GSHSCENE_ENTRY_REFUSED);
    ASSERT_EQ(refused.size(), 1u) << kind;
    EXPECT_EQ(refused[0].reason, GSHSCENE_REASON_RESOURCE_LIMIT);
    EXPECT_EQ(refused[0].second, 0u);
  }
}

TEST(DumpSink, TheScratchIsEachGlyphsAlone) {
  // 300 rings: together far past 64 KiB, each well inside it, so none is refused.
  Scene s;
  std::vector<uint32_t> ids(300, 2u);
  const GSHSCENE_List * list = one_run(s, s.glyphs(s.face(), ids, 8.0, GSHSCENE_HINT_NONE, 0.0, 30.0));
  SinkRef sink(make_full(nullptr, nullptr));
  for (uint32_t kind : {(uint32_t)GSHSCENE_TARGET_RASTER, (uint32_t)GSHSCENE_TARGET_STREAM}) {
    auto surface = make_surface(kind);
    ReportRef report;
    ASSERT_EQ(render_job(sink.sink, request_for(list, *surface, report.report)).result, GSHSCENE_OK);
    EXPECT_EQ(gshscene_report_count(report.report), 0u) << kind;
  }
}

// A glyph whose paths grow inside the scratch: shape-geometry grows its element and point arrays in turn, so
// each array is regrown while the other is the scratch's last block and the copy that moves it is the one that
// carries its contents.  The record's hash is the one the glyph's paths have from the C library's allocator.
TEST(DumpSink, AGlyphGrownInsideTheScratchIsTheSameGlyph) {
  for (uint32_t lines : {14u, 15u, 40u, 333u, 510u}) {
    for (bool fast : {true, false}) {
      Scene s;
      s.provider.zigzag = lines;
      const GSHSCENE_Resource * run = s.glyphs(s.face(), {8u}, 8.0, GSHSCENE_HINT_NONE);
      GSHGEOM_Map map;
      gshgeom_map_identity(2, &map);
      if (!fast) {
        gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, 30.0, &map);
      }
      const GSHSCENE_List * list = one_run(s, run, &map);
      Log log;
      GSHSCENE_Dump_Sink_Config c = config_default();
      c.log = &log.writer;
      SinkRef sink(make_with(c, nullptr, nullptr));
      auto raster = make_raster();
      ReportRef report;
      ASSERT_EQ(render_job(sink.sink, request_for(list, *raster, report.report)).result, GSHSCENE_OK) << lines;
      // The expected record, from the C library's allocator.
      GSHSCENE_Glyph_Route route;
      ASSERT_EQ(gshscene_glyph_route(run, &map, GSHSCENE_TARGET_RASTER, &route), GSHSCENE_OK);
      ASSERT_EQ(route.route, fast ? (uint32_t)GSHSCENE_GLYPH_FAST : (uint32_t)GSHSCENE_GLYPH_GENERAL);
      GSHSCENE_Glyph_Note note;
      uint64_t hash = 0;
      if (fast) {
        GSHSCENE_Glyph_Origin o;
        GSHGEOM_Device_Path * p = nullptr;
        ASSERT_EQ(gshscene_glyph_origin(run, &map, 0, &o), GSHSCENE_OK);
        ASSERT_EQ(gshscene_glyph_device_path(run, &route, 0, &o, nullptr, nullptr, &p, &note), GSHSCENE_OK);
        ASSERT_NE(p, nullptr);
        hash = gshgeom_device_path_hash(p);
        gshgeom_device_path_destroy(p);
      } else {
        GSHGEOM_Map glyph_map;
        GSHGEOM_Path * p = nullptr;
        ASSERT_EQ(gshscene_glyph_map(run, &map, 0, &glyph_map), GSHSCENE_OK);
        ASSERT_EQ(gshscene_glyph_outline(run, 0, nullptr, nullptr, &p, &note), GSHSCENE_OK);
        ASSERT_NE(p, nullptr);
        GSHGEOM_Hash h;
        gshgeom_hash_init(&h);
        gshgeom_hash_u64(&h, gshgeom_path_hash(p));
        gshgeom_hash_u64(&h, gshgeom_map_hash(&glyph_map));
        hash = gshgeom_hash_value(&h);
        gshgeom_path_destroy(p);
      }
      char want[64];
      std::snprintf(want, sizeof want, "\nY 0 %c %016llx status=%d\n", fast ? 'f' : 'g', (unsigned long long)hash,
          fast ? 1 : 0); // under NONE the fast tier answers NO_PROGRAM; the general route has no status
      EXPECT_NE(log.text.find(want), std::string::npos) << lines << (fast ? " fast" : " general") << "\n" << log.text;
      EXPECT_EQ(gshscene_report_count(report.report), 0u) << lines;
    }
  }
}

TEST(DumpSink, UnderCallerMemoryTheGlyphScratchIsTheWorkspaces) {
  // Under caller memory the glyph scratch is the workspace's: the run's paths fit, and the render allocates nothing.
  Scene s;
  const GSHSCENE_List * list = glyph_list(s);
  tracker::Tracker t;
  GSHSCENE_Allocator a = tracker::allocator_for(&t);
  SinkRef sink(make_full(&a, nullptr));
  auto raster = make_raster();
  ReportRef report;
  GSHSCENE_Render_Request req = request_for(list, *raster, report.report);
  req.flags = GSHSCENE_RENDER_CALLER_MEMORY;
  size_t need = 0;
  ASSERT_EQ(gshscene_sink_workspace_bytes(sink.sink, &req, &need), GSHSCENE_OK);
  EXPECT_GE(need, (size_t)65536) << "the glyph scratch is part of the workspace";
  std::vector<unsigned char> workspace(need);
  req.workspace = workspace.data();
  req.workspace_bytes = need;
  long calls = t.calls;
  ASSERT_EQ(render_job(sink.sink, req).result, GSHSCENE_OK);
  EXPECT_EQ(t.calls, calls) << "caller memory: nothing allocated for the glyphs";
  EXPECT_EQ(gshscene_report_hash(report.report), sink_known::kGlyphRasterReportHash);
}

TEST(DumpSink, AStrictRefusalWhileDrawingKeepsThePrefixAndNamesTheUnit) {
  Scene s;
  const GSHSCENE_List * list = answer_list(s);
  GSHSCENE_Dump_Sink_Config c = config_default();
  c.max_extent = 7.0;
  SinkRef sink(make_with(c, nullptr, nullptr));
  auto raster = make_raster();
  uint64_t before = raster->snapshot();
  ReportRef report;
  GSHSCENE_Render_Request req = request_for(list, *raster, report.report);
  req.flags = GSHSCENE_RENDER_STRICT;
  ASSERT_EQ(render_job(sink.sink, req).result, GSHSCENE_ERR_REFUSED);
  EXPECT_EQ(gshscene_report_hash(report.report), sink_known::kStrictReportHash);
  EXPECT_EQ(report_dump(report.report), with_bytes(sink_known::kStrictReportDump, report.report));
  EXPECT_EQ(raster->snapshot(), before) << "the prefix before unit 0 is empty";
  GSHSCENE_Report_Summary sum = summary(report.report);
  ASSERT_EQ(sum.failure.chain_count, 1u);
  EXPECT_EQ(sum.failure.chain[0], 0u);
}

// The resource limit at its bound and one past it, best effort: the bound passes, past it the unit is skipped.
TEST(DumpSink, TheExtentLimitPassesAtItsBoundAndRefusesPastIt) {
  for (double limit : {8.0, 7.999999999999999}) {
    Scene s;
    GSHSCENE_Paint red = solid(1, 0, 0);
    GSHSCENE_List * l = s.open();
    gshscene_list_fill(l, s.sq(2, 2, 8), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0);
    gshscene_list_clip_push(l, s.sq(0, 0, 8), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0);
    gshscene_list_fill(l, s.sq(1, 1, 3), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0);
    gshscene_list_clip_pop(l);
    gshscene_list_close(l);
    GSHSCENE_Dump_Sink_Config c = config_default();
    c.max_extent = limit;
    SinkRef sink(make_with(c, nullptr, nullptr));
    auto stream = make_stream();
    ReportRef report;
    ASSERT_EQ(render_job(sink.sink, request_for(l, *stream, report.report)).result, GSHSCENE_OK);
    auto e = entries(report.report);
    if (limit == 8.0) {
      EXPECT_TRUE(e.empty());
      EXPECT_EQ(stream->bytes.find(" X "), std::string::npos);
    } else {
      // The fill and the clip are refused; the clip's block (its fill and its pop) is skipped with it.
      ASSERT_EQ(e.size(), 2u);
      EXPECT_EQ(e[0].reason, GSHSCENE_REASON_RESOURCE_LIMIT);
      EXPECT_EQ(e[0].chain[0], 0u);
      EXPECT_EQ(e[1].chain[0], 1u);
      EXPECT_NE(stream->bytes.find("U 2 X class=1 reason=6\nU 3 X class=1 reason=6\n"), std::string::npos) << stream->bytes;
    }
  }
}

// S7: a cancel at unit k leaves the dump's prefix of k units, and a group still open is never stamped.
TEST(DumpSink, ACancelledRenderIsAPrefixWithNoHalfBuiltGroup) {
  Scene s;
  const GSHSCENE_List * list = answer_list(s);
  Log whole_log;
  GSHSCENE_Dump_Sink_Config c = config_default();
  c.log = &whole_log.writer;
  {
    SinkRef sink(make_with(c, nullptr, nullptr));
    auto raster = make_raster();
    ReportRef report;
    ASSERT_EQ(render_job(sink.sink, request_for(list, *raster, report.report)).result, GSHSCENE_OK);
  }
  std::vector<std::string> whole;
  for (size_t at = 0, next; at < whole_log.text.size(); at = next + 1) {
    next = whole_log.text.find('\n', at);
    whole.push_back(whole_log.text.substr(at, next - at + 1));
  }
  for (size_t k = 1; k < 15; k++) {
    Log log;
    c.log = &log.writer;
    SinkRef sink(make_with(c, nullptr, nullptr));
    auto raster = make_raster();
    ReportRef report;
    GSHSCENE_Cancel cancel;
    gshscene_cancel_init(&cancel);
    CancelAt at{&cancel, k};
    GSHSCENE_Render_Request req = request_for(list, *raster, report.report);
    req.cancel = &cancel;
    req.progress = cancel_at;
    req.progress_ctx = &at;
    ASSERT_EQ(render_job(sink.sink, req).result, GSHSCENE_ERR_CANCELLED);
    // The dump is the whole render's first k units, then its end.
    std::string expect;
    size_t units = 0;
    size_t i = 0;
    expect += whole[i++];
    for (; i < whole.size() && whole[i][0] != 'E'; i++) {
      if (whole[i][0] == 'U' && units++ == k) {
        break;
      }
      expect += whole[i];
    }
    GSHSCENE_Report_Summary sum = summary(report.report);
    // Units 5 and 6 are inside the group pushed at 4 and popped at 7: done stops at 4 for k in 5..7.
    size_t done = (k >= 5 && k <= 7) ? 4 : (k == 13 ? 12 : k); // unit 13 is the push of the empty group at 10
    EXPECT_EQ(sum.units_done, done) << k;
    expect += "E result=10 processed=" + std::to_string(k) + " done=" + std::to_string(done) + "\n";
    EXPECT_EQ(log.text, expect) << k;
    if (k >= 5 && k <= 7) {
      EXPECT_EQ(log.text.find("S 10 10 18 18"), std::string::npos) << "a group still open was stamped";
    }
  }
}

// S3: at most max_jobs renders in flight on one sink; a report is used by one render at a time; a running job is
// not begun again; a sink with a job in flight is not destroyed.
TEST(DumpSink, JobsInFlightAreBounded) {
  Scene s;
  const GSHSCENE_List * list = answer_list(s);
  GSHSCENE_Limits limits{};
  limits.max_jobs = 2;
  GSHSCENE_Sink * sink = make_deferred(nullptr, &limits);
  std::unique_ptr<Surface> surfaces[3] = {make_raster(), make_raster(), make_raster()};
  ReportRef reports[3];
  GSHSCENE_Job jobs[3] = {};
  for (int i = 0; i < 2; i++) {
    GSHSCENE_Render_Request req = request_for(list, *surfaces[i], reports[i].report);
    ASSERT_EQ(gshscene_sink_begin(sink, &req, &jobs[i]), GSHSCENE_OK) << i;
  }
  GSHSCENE_Render_Request third = request_for(list, *surfaces[2], reports[2].report);
  EXPECT_EQ(gshscene_sink_begin(sink, &third, &jobs[2]), GSHSCENE_ERR_LIMIT);
  GSHSCENE_Render_Request same_report = request_for(list, *surfaces[2], reports[0].report);
  EXPECT_EQ(gshscene_sink_begin(sink, &same_report, &jobs[2]), GSHSCENE_ERR_INVALID);
  GSHSCENE_Render_Request again = request_for(list, *surfaces[2], reports[2].report);
  EXPECT_EQ(gshscene_sink_begin(sink, &again, &jobs[0]), GSHSCENE_ERR_INVALID) << "a running job is not begun again";
  EXPECT_EQ(gshscene_report_reset(reports[0].report), GSHSCENE_ERR_INVALID) << "a report in use is not reset";
  EXPECT_EQ(gshscene_sink_destroy(sink), GSHSCENE_ERR_INVALID);
  uint64_t hash = 0;
  EXPECT_EQ(gshscene_dump_sink_output_hash(&jobs[0], &hash), GSHSCENE_ERR_INVALID) << "not done";
  size_t running = 9;
  for (int turns = 0; turns < 100 && running; turns++) {
    ASSERT_EQ(gshscene_dump_sink_step(sink, &running), GSHSCENE_OK);
  }
  EXPECT_EQ(running, 0u);
  EXPECT_EQ(gshscene_sink_begin(sink, &third, &jobs[2]), GSHSCENE_OK) << "the bound counts jobs in flight";
  while (running == 0) {
    gshscene_dump_sink_step(sink, &running);
    break;
  }
  for (int turns = 0; turns < 100; turns++) {
    gshscene_dump_sink_step(sink, &running);
  }
  EXPECT_EQ(running, 0u);
  EXPECT_EQ(gshscene_sink_destroy(sink), GSHSCENE_OK);
}

TEST(DumpSink, TheConfigurationIsValidated) {
  GSHSCENE_Sink * sink = nullptr;
  std::vector<std::function<void(GSHSCENE_Dump_Sink_Config &)>> bad = {
      [](GSHSCENE_Dump_Sink_Config & c) { c.raster_caps.flags |= 1u << 20; },
      [](GSHSCENE_Dump_Sink_Config & c) { c.stream_caps.cancel_unit = 0; },
      [](GSHSCENE_Dump_Sink_Config & c) { c.stream_caps.cancel_unit = GSHSCENE_CANCEL_UNIT_COUNT; },
      [](GSHSCENE_Dump_Sink_Config & c) { c.raster_caps.work_measure = nullptr; },
      [](GSHSCENE_Dump_Sink_Config & c) { c.raster_caps._reserved[2] = 1; },
      [](GSHSCENE_Dump_Sink_Config & c) { c.stream_caps.stroke_native |= 1u << 16; },
      [](GSHSCENE_Dump_Sink_Config & c) { c.raster_caps.flags |= GSHSCENE_CAP_THREAD_AFFINE; },
      [](GSHSCENE_Dump_Sink_Config & c) { c.stream_caps.flags &= ~GSHSCENE_CAP_CAN_BLOCK; },
      [](GSHSCENE_Dump_Sink_Config & c) { c.targets |= 1u << GSHSCENE_TARGET_GPU_TEXTURE; },
      [](GSHSCENE_Dump_Sink_Config & c) { c.completion = GSHSCENE_DUMP_SINK_COMPLETION_COUNT; },
      [](GSHSCENE_Dump_Sink_Config & c) { c.max_extent = -1.0; },
      [](GSHSCENE_Dump_Sink_Config & c) { c.max_extent = 1e300 * 1e300; },
      [](GSHSCENE_Dump_Sink_Config & c) { c._reserved[0] = 1; },
      [](GSHSCENE_Dump_Sink_Config & c) {
        c.raster_caps.flags &= ~GSHSCENE_CAP_CAN_BLOCK;
        c.stream_caps.flags &= ~GSHSCENE_CAP_CAN_BLOCK; // an in-line sink completes in begin: it can block
      },
      [](GSHSCENE_Dump_Sink_Config & c) {
        static const GSHSCENE_Writer no_write{nullptr, nullptr};
        c.log = &no_write;
      },
  };
  for (size_t i = 0; i < bad.size(); i++) {
    GSHSCENE_Dump_Sink_Config c = config_default();
    bad[i](c);
    EXPECT_EQ(gshscene_dump_sink_create(nullptr, nullptr, &c, &sink), GSHSCENE_ERR_INVALID) << i;
  }
  EXPECT_EQ(gshscene_dump_sink_create(nullptr, nullptr, nullptr, nullptr), GSHSCENE_ERR_INVALID);
  ASSERT_EQ(gshscene_dump_sink_create(nullptr, nullptr, nullptr, &sink), GSHSCENE_OK);
  EXPECT_STREQ(gshscene_sink_name(sink), "dump");
  EXPECT_EQ(gshscene_sink_destroy(sink), GSHSCENE_OK);
  gshscene_dump_sink_config_default(nullptr);
  GSHSCENE_Dump_Sink_Config d;
  std::memset(&d, 0xab, sizeof d);
  gshscene_dump_sink_config_default(&d);
  EXPECT_EQ(d.targets, (1u << GSHSCENE_TARGET_RASTER) | (1u << GSHSCENE_TARGET_STREAM));
  EXPECT_EQ(d.completion, (uint32_t)GSHSCENE_DUMP_SINK_INLINE);
  EXPECT_EQ(d.step_units, 0u);
  EXPECT_EQ(d.max_extent, 0.0);
  EXPECT_EQ(d.log, nullptr);
  for (unsigned char b : d._reserved) {
    EXPECT_EQ(b, 0);
  }
  for (const GSHSCENE_Sink_Caps * caps : {&d.raster_caps, &d.stream_caps}) {
    EXPECT_EQ(caps->item_kinds, 0x1feu) << "every kind, the stroke (S20) and the placeholder (PH1) included";
    EXPECT_EQ(caps->stroke_native, (uint32_t)GSHSCENE_STROKE_NATIVE_ALL) << "every stroke feature, as a stroke";
    EXPECT_EQ(caps->composites, 0x0fffffffu) << "every operator and blend mode: it records the number (B13)";
    EXPECT_EQ(caps->cancel_unit, (uint32_t)GSHSCENE_CANCEL_BETWEEN_UNITS);
    EXPECT_EQ(caps->cancel_work_bound, 1u);
    EXPECT_STREQ(caps->work_measure, "units");
    EXPECT_EQ(caps->gradient_kinds, 0xeu) << "linear, radial and sweep (S17)";
    EXPECT_EQ(caps->image_filters, 0xfu) << "every image filter (S21)";
    EXPECT_EQ(caps->image_extends, 0xfu) << "every edge mode";
    EXPECT_EQ(caps->image_formats, 0xffu) << "every sample type, layout and the alpha-only mark";
    for (unsigned char b : caps->_reserved) {
      EXPECT_EQ(b, 0);
    }
  }
  uint32_t common = GSHSCENE_CAP_CAN_BLOCK | GSHSCENE_CAP_CALLER_MEMORY | GSHSCENE_CAP_GLYPHS_FROM_FONT |
      GSHSCENE_CAP_CLIP_AA | GSHSCENE_CAP_BLEND_SPACES | GSHSCENE_CAP_GRADIENT_SPACES | GSHSCENE_CAP_PREPARE |
      GSHSCENE_CAP_STROKE_RUNS | GSHSCENE_CAP_IMAGE_FILTER_SPACES;
  EXPECT_EQ(d.raster_caps.flags, common | GSHSCENE_CAP_READBACK);
  EXPECT_EQ(d.stream_caps.flags, common);
  // Not a test sink, not a test sink's job.
  EXPECT_EQ(gshscene_dump_sink_step(nullptr, nullptr), GSHSCENE_ERR_INVALID);
  uint64_t h = 0;
  EXPECT_EQ(gshscene_dump_sink_output_hash(nullptr, &h), GSHSCENE_ERR_INVALID);
  struct Other {
    static GSHSCENE_Result caps(void *, const GSHSCENE_Target *, GSHSCENE_Sink_Caps * out) {
      GSHSCENE_Dump_Sink_Config c;
      gshscene_dump_sink_config_default(&c);
      *out = c.stream_caps;
      return GSHSCENE_OK;
    }
    static GSHSCENE_Result begin(void *, GSHSCENE_Job * job) {
      gshscene_job_set_impl_value(job, 77);
      gshscene_job_complete(job, GSHSCENE_OK, nullptr, 0, nullptr);
      return GSHSCENE_OK;
    }
    static void destroy(void *) {}
  };
  GSHSCENE_Sink_Ops ops{};
  ops.name = "other";
  ops.caps = Other::caps;
  ops.begin = Other::begin;
  ops.destroy = Other::destroy;
  GSHSCENE_Sink * other = nullptr;
  ASSERT_EQ(gshscene_sink_create(nullptr, nullptr, &ops, nullptr, &other), GSHSCENE_OK);
  EXPECT_EQ(gshscene_dump_sink_step(other, nullptr), GSHSCENE_ERR_INVALID);
  Scene s;
  const GSHSCENE_List * list = basic(s);
  auto stream = make_stream();
  ReportRef report;
  GSHSCENE_Render_Request req = request_for(list, *stream, report.report);
  GSHSCENE_Job job{};
  ASSERT_EQ(gshscene_sink_begin(other, &req, &job), GSHSCENE_OK);
  EXPECT_EQ(gshscene_dump_sink_output_hash(&job, &h), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_sink_destroy(other), GSHSCENE_OK);
}

// A raster the test sink does not stamp (not RGBA8, not interleaved) is unsupported; so is a kind it does not take.
TEST(DumpSink, ItDrawsRgba8RastersAndStreamsOnly) {
  SinkRef sink(make_full(nullptr, nullptr));
  for (const GIMG_Pixel_Format * f : {&GIMG_PIXEL_RGBA16, &GIMG_PIXEL_GRAY8, &GIMG_PIXEL_RGBAF32}) {
    GIMG_Raster * r = nullptr;
    ASSERT_EQ(gimg_raster_create(8, 8, f, GIMG_RASTER_OWNED, nullptr, 0, &r), GIMG_OK);
    GSHSCENE_Target t{};
    t.kind = GSHSCENE_TARGET_RASTER;
    t.surface = r;
    GSHSCENE_Sink_Caps caps{};
    EXPECT_EQ(gshscene_sink_caps(sink.sink, &t, &caps), GSHSCENE_ERR_UNSUPPORTED);
    gimg_raster_destroy(r);
  }
  for (uint8_t form : {(uint8_t)GIMG_ALPHA_STRAIGHT, (uint8_t)GIMG_ALPHA_PREMULTIPLIED}) {
    auto r = make_raster(form);
    GSHSCENE_Sink_Caps caps{};
    EXPECT_EQ(gshscene_sink_caps(sink.sink, &r->target, &caps), GSHSCENE_OK);
  }
  SinkRef writer(make_writer(nullptr, nullptr));
  auto r = make_raster();
  GSHSCENE_Sink_Caps caps{};
  EXPECT_EQ(gshscene_sink_caps(writer.sink, &r->target, &caps), GSHSCENE_ERR_UNSUPPORTED);
}

// A refused instance skips its picture; a refused group skips its block (S6), in the plan and in the dump alike.
TEST(DumpSink, ARefusedBlockIsSkippedWhole) {
  Scene s;
  GSHSCENE_Paint red = solid(1, 0, 0);
  GSHSCENE_List * pl = s.open();
  gshscene_list_fill(pl, s.font_run(), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0);
  gshscene_list_fill(pl, s.sq(0, 0, 3), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0);
  gshscene_list_close(pl);
  const GSHSCENE_Resource * picture = nullptr;
  gshscene_store_intern_picture(s.store, pl, &picture);
  s.held.push_back(picture);
  GSHSCENE_Group x{};
  x.opacity = 1.0;
  x.composite = GSHSCENE_COMPOSITE_XOR;
  GSHSCENE_List * l = s.open();
  gshscene_list_group_push(l, &x, 0, 0.0);
  gshscene_list_clip_push(l, s.sq(0, 0, 9), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0);
  gshscene_list_instance(l, picture, nullptr, 0, 0.0);
  gshscene_list_clip_pop(l);
  gshscene_list_group_pop(l);
  gshscene_list_instance(l, picture, nullptr, 0, 0.0);
  gshscene_list_fill(l, s.sq(5, 5, 3), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0);
  gshscene_list_close(l);
  SinkRef sink(make_narrow(nullptr, nullptr));
  auto stream = make_stream();
  ReportRef report;
  ASSERT_EQ(render_job(sink.sink, request_for(l, *stream, report.report)).result, GSHSCENE_OK);
  auto e = entries(report.report);
  // The XOR group (the clip and the instance inside it are not reported), and the font run in the second instance.
  ASSERT_EQ(e.size(), 2u);
  EXPECT_EQ(e[0].reason, GSHSCENE_REASON_COMPOSITE);
  ASSERT_EQ(e[0].chain_count, 1u);
  EXPECT_EQ(e[0].chain[0], 0u);
  EXPECT_EQ(e[1].reason, GSHSCENE_REASON_GLYPH_SOURCE);
  ASSERT_EQ(e[1].chain_count, 2u);
  EXPECT_EQ(e[1].chain[0], 5u);
  EXPECT_EQ(e[1].chain[1], 0u);
  const char * expect_lines[] = {"U 0 X class=1 reason=2\n", "U 1 X class=1 reason=2\n", "U 2 X class=1 reason=2\n",
      "U 2.0 X class=1 reason=2\n", "U 2.1 X class=1 reason=2\n", "U 3 X class=1 reason=2\n",
      "U 4 X class=1 reason=2\n", "U 5 I res=picture:", "U 5.0 X class=1 reason=3\n", "U 5.1 F res=path:", "U 6 F res=path:"};
  size_t at = 0;
  for (const char * line : expect_lines) {
    size_t found = stream->bytes.find(line, at);
    ASSERT_NE(found, std::string::npos) << line << "\n" << stream->bytes;
    at = found + 1;
  }
  EXPECT_EQ(summary(report.report).units_total, 11u);
  EXPECT_EQ(summary(report.report).units_done, 11u);
}

// S4: the target's blend space: an undescribed raster is sRGB; a described one is its description; a stream's is sRGB.
TEST(DumpSink, TheTargetsBlendSpaceIsResolved) {
  struct Case {
    GSHSCENE_Color_Space group;
    bool described_linear;
    size_t approximated;
  } cases[] = {
      {{GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_SRGB, 0.0}, false, 0},
      {{GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_LINEAR, 0.0}, false, 1},
      {{GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_LINEAR, 0.0}, true, 0},
      {{GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_SRGB, 0.0}, true, 1},
      {{GCOL_PRIMARIES_UNKNOWN, GCOL_TRANSFER_UNKNOWN, 0.0}, true, 0},
  };
  for (const Case & c : cases) {
    Scene s;
    GSHSCENE_Group g{};
    g.opacity = 1.0;
    g.blend_space = c.group;
    GSHSCENE_List * l = s.open();
    gshscene_list_group_push(l, &g, 0, 0.0);
    gshscene_list_group_pop(l);
    gshscene_list_close(l);
    SinkRef sink(make_narrow(nullptr, nullptr));
    auto raster = make_raster();
    if (c.described_linear) {
      GCOL_Color_Info info;
      gcol_color_info_default(&info);
      gcol_color_info_set_gamut(&info, GCOL_PRIMARIES_SRGB);
      info.transfer = GCOL_TRANSFER_LINEAR;
      ASSERT_EQ(gimg_raster_set_color_info(raster->raster, &info), GIMG_OK);
    }
    ReportRef report;
    ASSERT_EQ(render_job(sink.sink, request_for(l, *raster, report.report)).result, GSHSCENE_OK);
    EXPECT_EQ(summary(report.report).by_class[GSHSCENE_ENTRY_APPROXIMATED], c.approximated);
  }
  // A raster described in part is refused; one whose gamut matches no name is unsupported.
  Scene s;
  GSHSCENE_List * l = s.open();
  gshscene_list_close(l);
  SinkRef sink(make_full(nullptr, nullptr));
  auto raster = make_raster();
  GCOL_Color_Info info;
  gcol_color_info_default(&info);
  info.transfer = GCOL_TRANSFER_LINEAR;
  ASSERT_EQ(gimg_raster_set_color_info(raster->raster, &info), GIMG_OK);
  GSHSCENE_Sink_Caps caps{};
  ReportRef report;
  GSHSCENE_Render_Request req = request_for(l, *raster, report.report);
  GSHSCENE_Job job{};
  EXPECT_EQ(gshscene_sink_begin(sink.sink, &req, &job), GSHSCENE_ERR_INVALID);
  (void)caps;
}

// A failing log writer on a raster render: ERR_IO.
TEST(DumpSink, AFailingLogIsAnIOError) {
  Scene s;
  const GSHSCENE_List * list = answer_list(s);
  struct Fail {
    static GSHSCENE_Result write(void *, const void *, size_t) {
      return GSHSCENE_ERR_IO;
    }
  };
  GSHSCENE_Writer w{Fail::write, nullptr};
  GSHSCENE_Dump_Sink_Config c = config_default();
  c.log = &w;
  SinkRef sink(make_with(c, nullptr, nullptr));
  auto raster = make_raster();
  ReportRef report;
  EXPECT_EQ(render_job(sink.sink, request_for(list, *raster, report.report)).result, GSHSCENE_ERR_IO);
}

namespace {

/** A list of one fill of @p resource under @p map. */
const GSHSCENE_List * one_fill(Scene & s, const GSHSCENE_Resource * r, const GSHGEOM_Map * map = nullptr) {
  GSHSCENE_Paint red = solid(1, 0, 0);
  GSHSCENE_List * l = s.open();
  gshscene_list_fill(l, r, map, GSHGEOM_FILL_NONZERO, &red, 0, 0.0);
  gshscene_list_close(l);
  return l;
}

const GSHSCENE_Resource * rect(Scene & s, double x0, double y0, double x1, double y1) {
  GSHGEOM_Path * p = nullptr;
  gshgeom_path_create(nullptr, nullptr, 2, &p);
  gshgeom_path_move_to(p, GSHGEOM_Point{x0, y0, 0});
  gshgeom_path_line_to(p, GSHGEOM_Point{x1, y0, 0});
  gshgeom_path_line_to(p, GSHGEOM_Point{x1, y1, 0});
  gshgeom_path_line_to(p, GSHGEOM_Point{x0, y1, 0});
  gshgeom_path_close(p);
  const GSHSCENE_Resource * r = nullptr;
  gshscene_store_intern_path(s.store, p, &r);
  gshgeom_path_destroy(p);
  s.held.push_back(r);
  return r;
}

/** The pixels a render of @p list stamps on a fresh 64 by 64 raster, as the set of stamped rows' spans of x. */
std::string stamped(const GSHSCENE_List * list) {
  SinkRef sink(make_full(nullptr, nullptr));
  auto raster = make_raster();
  ReportRef report;
  EXPECT_EQ(render_job(sink.sink, request_for(list, *raster, report.report)).result, GSHSCENE_OK);
  std::string out;
  for (uint32_t y = 0; y < 64; y++) {
    int first = -1;
    int last = -1;
    for (uint32_t x = 0; x < 64; x++) {
      if (raster->pixel(x, y) != 0x5a5a5a5au) {
        first = first < 0 ? (int)x : first;
        last = (int)x;
      }
    }
    if (first >= 0) {
      out += std::to_string(y) + ":" + std::to_string(first) + "-" + std::to_string(last) + " ";
    }
  }
  return out;
}

} // namespace

// Boxes are rounded out to whole pixels: floor of the low edges, ceiling of the high ones, of either sign.
TEST(DumpSink, ABoxIsRoundedOutToWholePixels) {
  Scene s;
  EXPECT_EQ(stamped(one_fill(s, rect(s, 2.25, 3.75, 4.5, 5.0))), "3:2-4 4:2-4 ");
  GSHGEOM_Map move;
  gshgeom_map_translate(2, -0.5, -0.25, 0.0, &move);
  EXPECT_EQ(stamped(one_fill(s, rect(s, 1.0, 1.0, 2.0, 2.0), &move)), "0:0-1 1:0-1 ");
  gshgeom_map_translate(2, -2.5, 0.0, 0.0, &move);
  EXPECT_EQ(stamped(one_fill(s, rect(s, 0.0, 0.0, 3.0, 1.0), &move)), "0:0-0 ");
}

// A box past the range of a whole pixel is clamped before it is made one, and stamps the area it covers.
TEST(DumpSink, AHugeBoxIsClampedAndStampsTheArea) {
  Scene s;
  GSHGEOM_Map huge;
  gshgeom_map_scale(2, 0x1p80, 0x1p80, 1.0, &huge);
  std::string whole;
  for (int y = 0; y < 64; y++) {
    whole += std::to_string(y) + ":0-63 ";
  }
  EXPECT_EQ(stamped(one_fill(s, rect(s, -1.0, -1.0, 1.0, 1.0), &huge)), whole);
}

// A box that only touches the area, or the clip, stamps nothing and writes no stamp line.
TEST(DumpSink, ATouchingBoxStampsNothing) {
  Scene s;
  EXPECT_EQ(stamped(one_fill(s, rect(s, 64.0, 10.0, 70.0, 12.0))), "");
  EXPECT_EQ(stamped(one_fill(s, rect(s, -3.0, 0.0, 0.0, 9.0))), "");
  GSHSCENE_Paint red = solid(1, 0, 0);
  GSHSCENE_List * l = s.open();
  gshscene_list_clip_push(l, rect(s, 0, 0, 10, 10), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0);
  gshscene_list_fill(l, rect(s, 10, 0, 20, 10), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0);
  gshscene_list_clip_pop(l);
  gshscene_list_close(l);
  Log log;
  GSHSCENE_Dump_Sink_Config c = config_default();
  c.log = &log.writer;
  SinkRef sink(make_with(c, nullptr, nullptr));
  auto raster = make_raster();
  ReportRef report;
  ASSERT_EQ(render_job(sink.sink, request_for(l, *raster, report.report)).result, GSHSCENE_OK);
  EXPECT_EQ(log.text.find("\nS "), std::string::npos) << log.text;
}

// The extent limit is on either side: a box too tall is refused like one too wide, and the dump names the unit.
TEST(DumpSink, TheExtentLimitIsOnEitherSide) {
  for (bool tall : {false, true}) {
    Scene s;
    const GSHSCENE_List * l = one_fill(s, tall ? rect(s, 0, 0, 2, 20) : rect(s, 0, 0, 20, 2));
    GSHSCENE_Dump_Sink_Config c = config_default();
    c.max_extent = 10.0;
    SinkRef sink(make_with(c, nullptr, nullptr));
    auto stream = make_stream();
    ReportRef report;
    ASSERT_EQ(render_job(sink.sink, request_for(l, *stream, report.report)).result, GSHSCENE_OK);
    EXPECT_EQ(summary(report.report).by_class[GSHSCENE_ENTRY_REFUSED], 1u) << tall;
    EXPECT_NE(stream->bytes.find("U 0 X class=1 reason=6\n"), std::string::npos) << stream->bytes;
  }
}

// A strict refusal deep in a picture names the whole chain.
TEST(DumpSink, AStrictRefusalInAPictureNamesItsChain) {
  Scene s;
  GSHSCENE_Paint red = solid(1, 0, 0);
  GSHSCENE_List * pl = s.open();
  gshscene_list_fill(pl, s.sq(0, 0, 2), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0);
  gshscene_list_fill(pl, s.sq(0, 0, 30), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0);
  gshscene_list_close(pl);
  const GSHSCENE_Resource * picture = nullptr;
  gshscene_store_intern_picture(s.store, pl, &picture);
  s.held.push_back(picture);
  GSHSCENE_List * l = s.open();
  gshscene_list_fill(l, s.sq(0, 0, 3), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0);
  gshscene_list_fill(l, s.sq(5, 5, 3), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0);
  gshscene_list_instance(l, picture, nullptr, 0, 0.0);
  gshscene_list_close(l);
  GSHSCENE_Dump_Sink_Config c = config_default();
  c.max_extent = 10.0;
  SinkRef sink(make_with(c, nullptr, nullptr));
  auto stream = make_stream();
  ReportRef report;
  GSHSCENE_Render_Request req = request_for(l, *stream, report.report);
  req.flags = GSHSCENE_RENDER_STRICT;
  ASSERT_EQ(render_job(sink.sink, req).result, GSHSCENE_ERR_REFUSED);
  GSHSCENE_Report_Summary sum = summary(report.report);
  ASSERT_EQ(sum.failure.chain_count, 2u);
  EXPECT_EQ(sum.failure.chain[0], 2u);
  EXPECT_EQ(sum.failure.chain[1], 1u);
  EXPECT_EQ(sum.units_processed, 4u);
  EXPECT_STREQ(sum.failure.reason, "strict: refused while drawing (the sink's resource limit, or a glyph)");
}

// The frame bytes are the scratch a render used, with a workspace or without; the sink's own failures say why.
TEST(DumpSink, FrameBytesAndTheTestSinksReasons) {
  Scene s;
  const GSHSCENE_List * list = answer_list(s);
  SinkRef sink(make_full(nullptr, nullptr));
  auto stream = make_stream();
  ReportRef report;
  GSHSCENE_Render_Request req = request_for(list, *stream, report.report);
  size_t need = 0;
  req.flags = GSHSCENE_RENDER_CALLER_MEMORY;
  ASSERT_EQ(gshscene_sink_workspace_bytes(sink.sink, &req, &need), GSHSCENE_OK);
  req.flags = 0;
  ASSERT_EQ(render_job(sink.sink, req).result, GSHSCENE_OK);
  EXPECT_EQ(summary(report.report).frame_bytes, need);
  stream->fail_at = 2;
  ASSERT_EQ(render_job(sink.sink, req).result, GSHSCENE_ERR_IO);
  EXPECT_STREQ(summary(report.report).failure.reason, "the stream refused a write");
  stream->fail_at = -1;
  GSHSCENE_Cancel cancel;
  gshscene_cancel_init(&cancel);
  CancelAt at{&cancel, 3};
  req.cancel = &cancel;
  req.progress = cancel_at;
  req.progress_ctx = &at;
  ASSERT_EQ(render_job(sink.sink, req).result, GSHSCENE_ERR_CANCELLED);
  EXPECT_STREQ(summary(report.report).failure.reason, "cancelled between units");
  // A failing write of the last line of a render that had already failed keeps the first failure.
  gshscene_cancel_init(&cancel);
  auto counting = make_stream();
  counting->fail_at = 1L << 40;
  GSHSCENE_Render_Request rc = req;
  rc.target = &counting->target;
  ASSERT_EQ(render_job(sink.sink, rc).result, GSHSCENE_ERR_CANCELLED);
  gshscene_cancel_init(&cancel);
  auto last = make_stream();
  last->fail_at = counting->writes - 1;
  rc.target = &last->target;
  EXPECT_EQ(render_job(sink.sink, rc).result, GSHSCENE_ERR_CANCELLED);
  // The sink's own allocation failing while drawing (a report entry, when the planned ones filled the report's first
  // storage: 16 ties): its generic reason.
  GSHSCENE_Group depth{};
  depth.opacity = 1.0;
  depth.ordering = GSHSCENE_ORDER_DEPTH;
  GSHSCENE_Paint red = solid(1, 0, 0);
  GSHSCENE_List * tied = s.open();
  gshscene_list_group_push(tied, &depth, 0, 0.0);
  for (int i = 0; i < 17; i++) {
    gshscene_list_fill(tied, s.sq(0, 0, 1), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 1.0);
  }
  gshscene_list_group_pop(tied);
  gshscene_list_fill(tied, s.sq(0, 0, 9), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0);
  gshscene_list_close(tied);
  list = tied;
  GSHSCENE_Dump_Sink_Config c = config_default();
  c.max_extent = 7.0;
  SinkRef limited(make_with(c, nullptr, nullptr));
  tracker::Tracker t;
  GSHSCENE_Allocator a = tracker::allocator_for(&t);
  bool seen = false;
  for (long fail = 0; fail < 40 && !seen; fail++) {
    ReportRef r(&a);
    GSHSCENE_Render_Request q = request_for(list, *stream, r.report);
    t.fail_from = t.calls + fail;
    GSHSCENE_Result got = render_job(limited.sink, q).result;
    t.fail_from = -1;
    if (got == GSHSCENE_ERR_OOM && std::string(summary(r.report).failure.reason) == "the test sink failed") {
      seen = true;
    }
  }
  EXPECT_TRUE(seen) << "no allocation failure reached a report entry added while drawing";
}

// Deferred, the output is the in-line output; a step goes through exactly step_units units of every running job.
TEST(DumpSink, ADeferredRenderStepsEveryJobAndEndsAsTheInlineOne) {
  Scene s;
  const GSHSCENE_List * list = answer_list(s);
  SinkRef inline_sink(make_full(nullptr, nullptr));
  auto reference = make_stream();
  ReportRef rr;
  Rendered ref = render_job(inline_sink.sink, request_for(list, *reference, rr.report));
  ASSERT_EQ(ref.result, GSHSCENE_OK);
  GSHSCENE_Sink * sink = make_deferred(nullptr, nullptr); // three units a step
  auto a = make_stream();
  auto b = make_stream();
  ReportRef ra;
  ReportRef rb;
  GSHSCENE_Render_Request qa = request_for(list, *a, ra.report);
  GSHSCENE_Render_Request qb = request_for(list, *b, rb.report);
  GSHSCENE_Job ja{};
  GSHSCENE_Job jb{};
  ASSERT_EQ(gshscene_sink_begin(sink, &qa, &ja), GSHSCENE_OK);
  ASSERT_EQ(gshscene_sink_begin(sink, &qb, &jb), GSHSCENE_OK);
  GSHSCENE_Job_Status st{};
  gshscene_job_poll(&ja, &st);
  EXPECT_EQ(st.units_processed, 0u);
  size_t running = 0;
  ASSERT_EQ(gshscene_dump_sink_step(sink, &running), GSHSCENE_OK);
  EXPECT_EQ(running, 2u);
  gshscene_job_poll(&ja, &st);
  EXPECT_EQ(st.units_processed, 3u);
  gshscene_job_poll(&jb, &st);
  EXPECT_EQ(st.units_processed, 3u);
  while (running) {
    gshscene_dump_sink_step(sink, &running);
  }
  EXPECT_EQ(a->bytes, reference->bytes);
  EXPECT_EQ(b->bytes, reference->bytes);
  uint64_t h = 0;
  ASSERT_EQ(gshscene_dump_sink_output_hash(&ja, &h), GSHSCENE_OK);
  EXPECT_EQ(h, ref.output);
  // The job reused for a shorter render starts from nothing.
  GSHSCENE_List * one = s.open();
  GSHSCENE_Paint red = solid(1, 0, 0);
  gshscene_list_fill(one, s.sq(0, 0, 1), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0);
  gshscene_list_close(one);
  GSHSCENE_Render_Request qc = request_for(one, *a, ra.report);
  ASSERT_EQ(gshscene_sink_begin(sink, &qc, &ja), GSHSCENE_OK);
  gshscene_job_poll(&ja, &st);
  EXPECT_EQ(st.state, GSHSCENE_JOB_RUNNING);
  EXPECT_EQ(st.units_processed, 0u);
  EXPECT_EQ(st.units_done, 0u);
  EXPECT_EQ(st.units_total, 1u);
  EXPECT_EQ(st.work_max, 0u);
  EXPECT_EQ(gshscene_dump_sink_output_hash(&ja, &h), GSHSCENE_ERR_INVALID);
  while (running || st.state == GSHSCENE_JOB_RUNNING) {
    gshscene_dump_sink_step(sink, &running);
    gshscene_job_poll(&ja, &st);
  }
  size_t need = 0;
  qc.flags = GSHSCENE_RENDER_CALLER_MEMORY;
  ASSERT_EQ(gshscene_sink_workspace_bytes(sink, &qc, &need), GSHSCENE_OK);
  EXPECT_EQ(summary(ra.report).frame_bytes, need) << "the peak is this render's, not the last one's";
  EXPECT_EQ(summary(ra.report).work_max, 1u);
  // A step from another thread on an affine sink is refused.
  EXPECT_EQ(gshscene_sink_destroy(sink), GSHSCENE_OK);
  GSHSCENE_Sink * affine = make_affine(nullptr, nullptr);
  GSHSCENE_Result stepped = GSHSCENE_ERR_INTERNAL;
  std::thread other([&] { stepped = gshscene_dump_sink_step(affine, nullptr); });
  other.join();
  EXPECT_EQ(stepped, GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_sink_destroy(affine), GSHSCENE_OK);
}

// Prepare holds a run's outlines too, by id; destroy gives everything back; making a test sink can fail cleanly.
TEST(DumpSink, PrepareHoldsOutlinesAndCreateCanFail) {
  Scene s;
  const GSHSCENE_Resource * a = s.sq(0, 0, 2);
  const GSHSCENE_Resource * b = s.sq(1, 1, 2);
  GSHSCENE_Glyph glyphs[3] = {{1, 0, 0, a}, {2, 5, 0, b}, {3, 9, 0, a}};
  GSHSCENE_Glyph_Run run{};
  run.source = GSHSCENE_GLYPHS_FROM_OUTLINES;
  run.glyph_count = 3;
  run.glyphs = glyphs;
  const GSHSCENE_Resource * text = nullptr;
  ASSERT_EQ(gshscene_store_intern_glyph_run(s.store, &run, &text), GSHSCENE_OK);
  s.held.push_back(text);
  std::vector<const GSHSCENE_Resource *> many;
  GSHSCENE_Paint red = solid(1, 0, 0);
  GSHSCENE_List * l = s.open();
  gshscene_list_fill(l, text, nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0);
  for (int i = 0; i < 40; i++) {
    gshscene_list_fill(l, s.sq(i, 0, 1), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0);
  }
  gshscene_list_close(l);
  size_t ca = gshscene_resource_refcount(a);
  size_t cb = gshscene_resource_refcount(b);
  tracker::Tracker t;
  GSHSCENE_Allocator alloc = tracker::allocator_for(&t);
  {
    SinkRef sink(make_full(&alloc, nullptr));
    auto stream = make_stream();
    ASSERT_EQ(gshscene_sink_prepare(sink.sink, l, &stream->target), GSHSCENE_OK);
    EXPECT_EQ(gshscene_resource_refcount(a), ca + 1);
    EXPECT_EQ(gshscene_resource_refcount(b), cb + 1);
    uint64_t bytes = 0;
    gshscene_sink_static_bytes(sink.sink, &bytes);
    uint64_t expect = gshscene_resource_bytes(text) + gshscene_resource_bytes(a) + gshscene_resource_bytes(b);
    for (int i = 0; i < 40; i++) {
      expect += gshscene_resource_bytes(s.held[s.held.size() - 40 + (size_t)i]);
    }
    EXPECT_EQ(bytes, expect) << "the outlines are squares the list also fills: held once";
  }
  EXPECT_EQ(gshscene_resource_refcount(a), ca);
  EXPECT_TRUE(t.live.empty());
  for (long fail = 0; fail < 8; fail++) {
    tracker::Tracker f;
    f.fail_from = fail;
    GSHSCENE_Allocator fa = tracker::allocator_for(&f);
    GSHSCENE_Sink * sink = nullptr;
    GSHSCENE_Result r = gshscene_dump_sink_create(&fa, nullptr, nullptr, &sink);
    if (r == GSHSCENE_OK) {
      EXPECT_EQ(gshscene_sink_destroy(sink), GSHSCENE_OK);
    } else {
      EXPECT_EQ(r, GSHSCENE_ERR_OOM) << fail;
    }
    EXPECT_TRUE(f.live.empty()) << fail;
  }
  GSHSCENE_Limits huge{};
  huge.max_jobs = SIZE_MAX;
  GSHSCENE_Sink * sink = nullptr;
  EXPECT_EQ(gshscene_dump_sink_create(nullptr, &huge, nullptr, &sink), GSHSCENE_ERR_OOM);
}

// Masks-images-patterns story I2 (S21): the records of an image fill.  The image is written once, then its paint once,
// each by id the first time a render meets it, and every fill names its paint with `img=`.
TEST(DumpSink, AnImageAndItsPaintAreRecordedOnceAndEveryFillNamesItsPaint) {
  Scene s;
  GIMG_Raster * raster = nullptr;
  ASSERT_EQ(gimg_raster_create(5, 4, &GIMG_PIXEL_RGBA8, GIMG_RASTER_OWNED, nullptr, 0, &raster), GIMG_OK);
  GSHSCENE_Image_Params params{};
  const GSHSCENE_Resource * image = nullptr;
  ASSERT_EQ(gshscene_store_intern_image(s.store, raster, &params, &image), GSHSCENE_OK);
  gimg_raster_destroy(raster);
  s.held.push_back(image);
  auto paint_resource = [&](uint32_t filter) {
    GSHSCENE_Image_Paint ip;
    gshscene_image_paint_init(&ip);
    ip.image = image;
    ip.magnify = ip.minify = filter;
    const GSHSCENE_Resource * r = nullptr;
    EXPECT_EQ(gshscene_store_intern_image_paint(s.store, &ip, &r), GSHSCENE_OK);
    s.held.push_back(r);
    return r;
  };
  const GSHSCENE_Resource * linear = paint_resource(GSHSCENE_IMAGE_FILTER_LINEAR);
  const GSHSCENE_Resource * nearest = paint_resource(GSHSCENE_IMAGE_FILTER_NEAREST);
  GSHSCENE_List * l = s.open();
  for (const GSHSCENE_Resource * paint : {linear, nearest, linear, linear}) {
    GSHSCENE_Paint p = solid(0, 0, 0, 1.0);
    p.kind = GSHSCENE_PAINT_IMAGE;
    p.resource = paint;
    ASSERT_EQ(gshscene_list_fill(l, s.sq(2, 2, 8), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK);
  }
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  SinkRef sink(make_full(nullptr, nullptr));
  auto stream = make_stream(GSHSCENE_UNIT_MILLIMETER);
  ReportRef report;
  Rendered r = render_job(sink.sink, request_for(l, *stream, report.report));
  ASSERT_EQ(r.result, GSHSCENE_OK);
  std::vector<std::string> lines;
  for (size_t at = 0; at < stream->bytes.size();) {
    size_t end = stream->bytes.find('\n', at);
    lines.push_back(stream->bytes.substr(at, end - at));
    at = end + 1;
  }
  size_t images = 0, paints = 0, fills = 0;
  std::string image_id = std::to_string(gshscene_resource_id(image));
  for (const std::string & line : lines) {
    if (line.rfind("M ", 0) == 0) {
      images++;
      EXPECT_EQ(line.rfind("M " + image_id + " ", 0), 0u);
      EXPECT_NE(line.find(" w=5 h=4 "), std::string::npos);
    } else if (line.rfind("J ", 0) == 0) {
      paints++;
      EXPECT_NE(line.find(" image=" + image_id + " "), std::string::npos);
      EXPECT_NE(line.find(" tint=-"), std::string::npos);
    } else if (line.find(" img=") != std::string::npos) {
      fills++;
    }
  }
  EXPECT_EQ(images, 1u) << "the image once, however many paints and fills name it";
  EXPECT_EQ(paints, 2u) << "each paint once";
  EXPECT_EQ(fills, 4u);
  EXPECT_NE(stream->bytes.find(" img=" + std::to_string(gshscene_resource_id(linear)) + " "), std::string::npos);
  EXPECT_NE(stream->bytes.find(" img=" + std::to_string(gshscene_resource_id(nearest)) + " "), std::string::npos);
}

// The writer-like sink has no reflection: a REFLECT image fill is refused there and nothing of the image is written; the
// toolpath-like sink has no image at all.
TEST(DumpSink, ARefusedImageFillWritesNoRecordOfItsImage) {
  for (int configuration = 0; configuration < 2; configuration++) {
    Scene s;
    GIMG_Raster * raster = nullptr;
    ASSERT_EQ(gimg_raster_create(3, 3, &GIMG_PIXEL_RGBA8, GIMG_RASTER_OWNED, nullptr, 0, &raster), GIMG_OK);
    GSHSCENE_Image_Params params{};
    const GSHSCENE_Resource * image = nullptr;
    ASSERT_EQ(gshscene_store_intern_image(s.store, raster, &params, &image), GSHSCENE_OK);
    gimg_raster_destroy(raster);
    s.held.push_back(image);
    GSHSCENE_Image_Paint ip;
    gshscene_image_paint_init(&ip);
    ip.image = image;
    ip.extend_x = GSHSCENE_IMAGE_EXTEND_REFLECT;
    const GSHSCENE_Resource * paint = nullptr;
    ASSERT_EQ(gshscene_store_intern_image_paint(s.store, &ip, &paint), GSHSCENE_OK);
    s.held.push_back(paint);
    GSHSCENE_Paint p = solid(0, 0, 0, 1.0);
    p.kind = GSHSCENE_PAINT_IMAGE;
    p.resource = paint;
    GSHSCENE_List * l = s.open();
    ASSERT_EQ(gshscene_list_fill(l, s.sq(2, 2, 8), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
    SinkRef sink(configuration ? make_toolpath_like(nullptr, nullptr) : make_writer_like(nullptr, nullptr));
    auto stream = make_stream(GSHSCENE_UNIT_MILLIMETER);
    ReportRef report;
    Rendered r = render_job(sink.sink, request_for(l, *stream, report.report));
    ASSERT_EQ(r.result, GSHSCENE_OK);
    EXPECT_NE(stream->bytes.find(" X class=1 reason=32"), std::string::npos) << configuration;
    EXPECT_EQ(stream->bytes.find("\nM "), std::string::npos) << "no image record for a refused fill";
    EXPECT_EQ(stream->bytes.find("\nJ "), std::string::npos);
    auto refused = sinkconf::entries(report.report);
    ASSERT_EQ(refused.size(), 1u);
    EXPECT_EQ(refused[0].reason, GSHSCENE_REASON_IMAGE);
    size_t reflect = 1u << GSHSCENE_IMAGE_EXTEND_REFLECT;
    size_t everything = reflect | (1u << GSHSCENE_IMAGE_EXTEND_PAD) |
        ((size_t)(GSHSCENE_IMAGE_FORMAT_UNORM8 | GSHSCENE_IMAGE_FORMAT_RGBA) << GSHSCENE_IMAGE_REQUIRED_FORMAT_SHIFT);
    EXPECT_EQ(refused[0].second, configuration ? everything : reflect) << "the writer lacks only reflection; the pen lacks all";
  }
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
