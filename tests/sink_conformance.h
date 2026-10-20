/**
 * @file
 *
 * The sink conformance suite (story 13a, S1 to S16 of sink.h): one set of tests
 * that every sink passes, run unchanged against each configuration of the test
 * sink now, and against shape-cpu (13b) and the GPU sinks later.  It knows a
 * sink only through the contract: a fixture says how to make one, which target
 * kinds it draws and how to turn its event loop when it cannot block, and every
 * expectation is read from the sink's own caps (what it refuses, whether it can
 * block, its cancel unit and bound, its affinity, readback, caller memory,
 * prepare).  A rule a sink's caps make inapplicable is skipped, never passed.
 *
 * Use: include it in one test file, define the fixtures, and
 * `INSTANTIATE_TEST_SUITE_P(Name, SinkConformance, ::testing::Values(&fixture, ...))`.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#ifndef GHOTI_IO_GSHSCENE_TEST_SINK_CONFORMANCE_H
#define GHOTI_IO_GSHSCENE_TEST_SINK_CONFORMANCE_H

#include <gtest/gtest.h>

#include <ghoti.io/image/raster.h>
#include <ghoti.io/shape-geometry/shape-geometry.h>
#include <ghoti.io/shape-scene/shape-scene.h>

#include "alloc_tracker.h"

#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace sinkconf {

/* --- what a target draws on ---------------------------------------------------- */

/** FNV-1a 64 over bytes, continuing @p state. */
inline uint64_t fnv(const void * data, size_t n, uint64_t state = 0xcbf29ce484222325ull) {
  const unsigned char * p = (const unsigned char *)data;
  for (size_t i = 0; i < n; i++) {
    state = (state ^ p[i]) * 0x100000001b3ull;
  }
  return state;
}

/** A target and its surface: a raster, or a stream that keeps what is written and can fail a chosen write. */
struct Surface {
  GSHSCENE_Target target{};
  GIMG_Raster * raster = nullptr;
  GSHSCENE_Writer writer{};
  std::string bytes;
  long fail_at = -1; // the write (from 0) that fails; -1 never
  long writes = 0;
  Surface() = default;
  Surface(const Surface &) = delete;
  Surface & operator=(const Surface &) = delete;
  ~Surface() {
    gimg_raster_destroy(raster);
  }
  /** What the target holds: a raster's rows (not its stride), or the stream's bytes. */
  uint64_t snapshot() const {
    if (raster) {
      uint64_t h = 0xcbf29ce484222325ull;
      const unsigned char * px = (const unsigned char *)gimg_raster_pixels_const(raster);
      size_t stride = gimg_raster_stride_bytes(raster);
      size_t row = gimg_raster_width(raster) * gimg_raster_bytes_per_pixel(gimg_raster_format(raster));
      for (uint32_t y = 0; y < gimg_raster_height(raster); y++) {
        h = fnv(px + y * stride, row, h);
      }
      return h;
    }
    return fnv(bytes.data(), bytes.size());
  }
  /** The four bytes of pixel (x, y) of a raster, as an integer. */
  uint32_t pixel(uint32_t x, uint32_t y) const {
    const unsigned char * px = (const unsigned char *)gimg_raster_pixels_const(raster);
    uint32_t v;
    std::memcpy(&v, px + y * gimg_raster_stride_bytes(raster) + x * 4u, 4);
    return v;
  }
};

inline GSHSCENE_Result surface_write(void * ctx, const void * data, size_t count) {
  Surface * s = (Surface *)ctx;
  if (s->fail_at >= 0 && s->writes++ == s->fail_at) {
    return GSHSCENE_ERR_IO;
  }
  s->bytes.append((const char *)data, count);
  return GSHSCENE_OK;
}

/** A 64 by 64 RGBA8 raster filled with 0x5a, whole-surface target. */
inline std::unique_ptr<Surface> make_raster(uint8_t alpha_form = GIMG_ALPHA_STRAIGHT, uint32_t w = 64, uint32_t h = 64) {
  auto s = std::make_unique<Surface>();
  GIMG_Pixel_Format format = GIMG_PIXEL_RGBA8;
  format.alpha_form = alpha_form;
  if (gimg_raster_create(w, h, &format, GIMG_RASTER_OWNED, nullptr, 0, &s->raster) != GIMG_OK) {
    return nullptr;
  }
  unsigned char * px = (unsigned char *)gimg_raster_pixels(s->raster);
  for (uint32_t y = 0; y < h; y++) {
    std::memset(px + y * gimg_raster_stride_bytes(s->raster), 0x5a, (size_t)w * 4u);
  }
  s->target.kind = GSHSCENE_TARGET_RASTER;
  s->target.surface = s->raster;
  return s;
}

/** A stream target in millimetres. */
inline std::unique_ptr<Surface> make_stream(uint32_t unit = GSHSCENE_UNIT_MILLIMETER) {
  auto s = std::make_unique<Surface>();
  s->writer.write = surface_write;
  s->writer.ctx = s.get();
  s->target.kind = GSHSCENE_TARGET_STREAM;
  s->target.surface = &s->writer;
  s->target.unit = unit;
  return s;
}

inline std::unique_ptr<Surface> make_surface(uint32_t kind) {
  return kind == GSHSCENE_TARGET_RASTER ? make_raster() : make_stream();
}

/* --- the scenes ------------------------------------------------------------------- */

inline GSHGEOM_Path * square(double x, double y, double size) {
  GSHGEOM_Path * p = nullptr;
  gshgeom_path_create(nullptr, nullptr, 2, &p);
  gshgeom_path_move_to(p, GSHGEOM_Point{x, y, 0});
  gshgeom_path_line_to(p, GSHGEOM_Point{x + size, y, 0});
  gshgeom_path_line_to(p, GSHGEOM_Point{x + size, y + size, 0});
  gshgeom_path_line_to(p, GSHGEOM_Point{x, y + size, 0});
  gshgeom_path_close(p);
  return p;
}

inline GSHSCENE_Paint solid(double r, double g, double b, double a = 1.0) {
  GSHSCENE_Paint paint{};
  paint.kind = GSHSCENE_PAINT_SOLID;
  paint.rgba[0] = r;
  paint.rgba[1] = g;
  paint.rgba[2] = b;
  paint.rgba[3] = a;
  return paint;
}

/** A store, the resources it holds for the test, and lists over them; everything released at the end. */
struct Scene {
  GSHSCENE_Store * store = nullptr;
  std::vector<const GSHSCENE_Resource *> held;
  std::vector<GSHSCENE_List *> lists;
  explicit Scene(const GSHSCENE_Allocator * alloc = nullptr) {
    gshscene_store_create(alloc, nullptr, &store);
  }
  Scene(const Scene &) = delete;
  Scene & operator=(const Scene &) = delete;
  ~Scene() {
    for (GSHSCENE_List * l : lists) {
      gshscene_list_destroy(l);
    }
    for (const GSHSCENE_Resource * r : held) {
      gshscene_resource_release(r);
    }
    gshscene_store_destroy(store);
  }
  const GSHSCENE_Resource * sq(double x, double y, double size) {
    GSHGEOM_Path * p = square(x, y, size);
    const GSHSCENE_Resource * r = nullptr;
    gshscene_store_intern_path(store, p, &r);
    gshgeom_path_destroy(p);
    held.push_back(r);
    return r;
  }
  GSHSCENE_List * open() {
    GSHSCENE_List * l = nullptr;
    gshscene_list_create(nullptr, nullptr, store, &l);
    lists.push_back(l);
    return l;
  }
  /** A glyph run named by a font: what a sink without a glyph path provider refuses. */
  const GSHSCENE_Resource * font_run() {
    GSHSCENE_Glyph glyphs[2] = {{36, 2.0, 40.0, nullptr}, {37, 9.0, 40.0, nullptr}};
    GSHSCENE_Glyph_Run run{};
    run.source = GSHSCENE_GLYPHS_FROM_FONT;
    run.font_key = 0x1122334455667788ull;
    run.size = 6.0;
    run.glyph_count = 2;
    run.glyphs = glyphs;
    const GSHSCENE_Resource * r = nullptr;
    gshscene_store_intern_glyph_run(store, &run, &r);
    held.push_back(r);
    return r;
  }
};

/**
 * The basic list: a fill; a clip around a fill; a depth group in a stated linear blend space around two fills of
 * equal depth (a tie, broken by layer); an instance of a picture holding a fill and a depth group with a tie broken
 * by declared order.  Everything inside 0..48 in list space.
 */
inline const GSHSCENE_List * basic(Scene & s) {
  GSHSCENE_Paint red = solid(1, 0, 0);
  GSHSCENE_Paint blue = solid(0, 0, 1, 0.5);
  GSHSCENE_Group depth{};
  depth.opacity = 1.0;
  depth.ordering = GSHSCENE_ORDER_DEPTH;
  depth.composite = GSHSCENE_COMPOSITE_SRC_OVER;

  GSHSCENE_List * pl = s.open();
  gshscene_list_fill(pl, s.sq(0, 0, 10), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0);
  gshscene_list_group_push(pl, &depth, 0, 0.0);
  gshscene_list_fill(pl, s.sq(2, 2, 4), nullptr, GSHGEOM_FILL_NONZERO, &blue, 0, 2.0);
  gshscene_list_fill(pl, s.sq(3, 3, 4), nullptr, GSHGEOM_FILL_EVEN_ODD, &red, 0, 2.0);
  gshscene_list_group_pop(pl);
  gshscene_list_close(pl);
  const GSHSCENE_Resource * picture = nullptr;
  gshscene_store_intern_picture(s.store, pl, &picture);
  s.held.push_back(picture);

  GSHSCENE_Group linear = depth;
  linear.opacity = 0.5;
  linear.blend_space.primaries = GCOL_PRIMARIES_SRGB;
  linear.blend_space.transfer = GCOL_TRANSFER_LINEAR;

  GSHSCENE_List * l = s.open();
  gshscene_list_fill(l, s.sq(2, 2, 8), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0);
  gshscene_list_clip_push(l, s.sq(4, 4, 20), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0);
  gshscene_list_fill(l, s.sq(0, 0, 30), nullptr, GSHGEOM_FILL_NONZERO, &blue, 0, 0.0);
  gshscene_list_clip_pop(l);
  gshscene_list_group_push(l, &linear, 0, 0.0);
  gshscene_list_fill(l, s.sq(10, 10, 6), nullptr, GSHGEOM_FILL_NONZERO, &red, 1, 1.0);
  gshscene_list_fill(l, s.sq(12, 12, 6), nullptr, GSHGEOM_FILL_NONZERO, &blue, 0, 1.0);
  gshscene_list_group_pop(l);
  GSHGEOM_Map move;
  gshgeom_map_translate(2, 30.0, 30.0, 0.0, &move);
  gshscene_list_instance(l, picture, &move, 0, 0.0);
  gshscene_list_fill(l, s.sq(40, 2, 6), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0);
  gshscene_list_close(l);
  return l;
}

/** The basic list's ties: the root group's and the picture's (forwarded once per instance). */
inline size_t basic_ties(const GSHSCENE_List * l) {
  size_t n = gshscene_list_tie_count(l);
  for (size_t i = 0; i < gshscene_list_count(l); i++) {
    const GSHSCENE_Item * item = nullptr;
    gshscene_list_item(l, i, &item);
    if (item->kind == GSHSCENE_ITEM_INSTANCE) {
      n += gshscene_list_tie_count(gshscene_resource_picture(item->resource));
    }
  }
  return n;
}

/**
 * A list holding one item @p caps refuse at planning, between two fills, and what it is; or nullptr when the caps
 * refuse nothing the suite can build.
 */
inline const GSHSCENE_List * refusable(Scene & s, const GSHSCENE_Sink_Caps & caps, GSHSCENE_Reason * reason) {
  GSHSCENE_Paint red = solid(1, 0, 0);
  GSHSCENE_List * l = s.open();
  gshscene_list_fill(l, s.sq(1, 1, 5), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0);
  if (!(caps.flags & GSHSCENE_CAP_GLYPHS_FROM_FONT)) {
    gshscene_list_fill(l, s.font_run(), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0);
    *reason = GSHSCENE_REASON_GLYPH_SOURCE;
  } else {
    uint32_t op = GSHSCENE_COMPOSITE_COUNT;
    for (uint32_t c = 0; c < GSHSCENE_COMPOSITE_COUNT; c++) {
      if (!(caps.composites & (1u << c))) {
        op = c;
        break;
      }
    }
    if (op != GSHSCENE_COMPOSITE_COUNT && (caps.item_kinds & (1u << GSHSCENE_ITEM_GROUP_PUSH))) {
      GSHSCENE_Group g{};
      g.opacity = 1.0;
      g.composite = op;
      gshscene_list_group_push(l, &g, 0, 0.0);
      gshscene_list_fill(l, s.sq(20, 20, 5), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0);
      gshscene_list_group_pop(l);
      *reason = GSHSCENE_REASON_COMPOSITE;
    } else if (!(caps.item_kinds & (1u << GSHSCENE_ITEM_CLIP_PUSH))) {
      gshscene_list_clip_push(l, s.sq(0, 0, 30), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0);
      gshscene_list_fill(l, s.sq(20, 20, 5), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0);
      gshscene_list_clip_pop(l);
      *reason = GSHSCENE_REASON_ITEM_KIND;
    } else {
      return nullptr;
    }
  }
  gshscene_list_fill(l, s.sq(30, 30, 5), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0);
  gshscene_list_close(l);
  return l;
}

/* --- the fixtures ------------------------------------------------------------------- */

/** How to make one sink, which targets it draws, and how to turn its event loop. */
struct Fixture {
  const char * name;
  GSHSCENE_Sink * (*make)(const GSHSCENE_Allocator * alloc, const GSHSCENE_Limits * limits);
  std::vector<uint32_t> kinds;
  /** One turn of the event loop of a sink that completes later (a deferred sink); NULL for one that never needs it. */
  void (*pump)(GSHSCENE_Sink * sink);
};

inline std::ostream & operator<<(std::ostream & os, const Fixture * f) {
  return os << f->name;
}

/** A sink, destroyed at the end of the test. */
struct SinkRef {
  GSHSCENE_Sink * sink = nullptr;
  explicit SinkRef(GSHSCENE_Sink * s) : sink(s) {}
  SinkRef(const SinkRef &) = delete;
  SinkRef & operator=(const SinkRef &) = delete;
  ~SinkRef() {
    EXPECT_EQ(gshscene_sink_destroy(sink), GSHSCENE_OK);
  }
};

struct ReportRef {
  GSHSCENE_Report * report = nullptr;
  explicit ReportRef(const GSHSCENE_Allocator * alloc = nullptr, const GSHSCENE_Limits * limits = nullptr) {
    gshscene_report_create(alloc, limits, &report);
  }
  ReportRef(const ReportRef &) = delete;
  ReportRef & operator=(const ReportRef &) = delete;
  ~ReportRef() {
    gshscene_report_destroy(report);
  }
};

inline GSHSCENE_Sink_Caps caps_of(GSHSCENE_Sink * sink, const Surface & s) {
  GSHSCENE_Sink_Caps caps{};
  EXPECT_EQ(gshscene_sink_caps(sink, &s.target, &caps), GSHSCENE_OK);
  return caps;
}

/** What a render came to. */
struct Outcome {
  GSHSCENE_Result result = GSHSCENE_ERR_INTERNAL;
  GSHSCENE_Job_Status status{};
  int callbacks = 0;
  GSHSCENE_Result callback_result = GSHSCENE_ERR_INTERNAL;
};

inline void count_complete(void * ctx, GSHSCENE_Job * job, GSHSCENE_Result result) {
  Outcome * o = (Outcome *)ctx;
  o->callbacks++;
  o->callback_result = result;
  // S3: the report is sealed and the job does not read as done until this returns.
  GSHSCENE_Job_Status st{};
  EXPECT_EQ(gshscene_job_poll(job, &st), GSHSCENE_OK);
  EXPECT_EQ(st.state, GSHSCENE_JOB_RUNNING) << "on_complete ran after the job read as done";
  GSHSCENE_Report_Summary sum{};
  gshscene_report_summary(gshscene_job_request(job)->report, &sum);
  EXPECT_EQ(sum.result, result) << "on_complete ran before the report was sealed";
}

/**
 * Render through the job path: begin, then wait (a sink that can block) or turn the event loop until done (one that
 * cannot).  The request's `on_complete` is the outcome's counter.
 */
inline Outcome run(const Fixture * f, GSHSCENE_Sink * sink, GSHSCENE_Render_Request request) {
  Outcome o;
  request.on_complete = count_complete;
  request.on_complete_ctx = &o;
  GSHSCENE_Job job{};
  o.result = gshscene_sink_begin(sink, &request, &job);
  EXPECT_EQ(gshscene_job_poll(&job, &o.status), GSHSCENE_OK);
  if (o.status.state == GSHSCENE_JOB_IDLE) {
    return o;
  }
  if (o.status.state == GSHSCENE_JOB_DONE) {
    EXPECT_EQ(o.result, o.status.result) << "begin returns the result of a job it completed (S3)";
  }
  GSHSCENE_Sink_Caps caps{};
  gshscene_sink_caps(sink, request.target, &caps);
  for (long turns = 0; o.status.state == GSHSCENE_JOB_RUNNING; turns++) {
    if (caps.flags & GSHSCENE_CAP_CAN_BLOCK) {
      o.result = gshscene_sink_wait(sink, &job);
    } else {
      if (!f || !f->pump || turns > 1000000) {
        ADD_FAILURE() << "a job of a sink that cannot block is still running and nothing turns its loop";
        return o;
      }
      f->pump(sink);
    }
    gshscene_job_poll(&job, &o.status);
  }
  o.result = o.status.result;
  return o;
}

inline GSHSCENE_Render_Request request_for(const GSHSCENE_List * list, const Surface & s, GSHSCENE_Report * report) {
  GSHSCENE_Render_Request r{};
  r.list = list;
  r.target = &s.target;
  r.report = report;
  return r;
}

inline std::vector<GSHSCENE_Report_Entry> entries(const GSHSCENE_Report * report) {
  std::vector<GSHSCENE_Report_Entry> out;
  for (size_t i = 0; i < gshscene_report_count(report); i++) {
    GSHSCENE_Report_Entry e{};
    gshscene_report_entry(report, i, &e);
    out.push_back(e);
  }
  return out;
}

inline GSHSCENE_Report_Summary summary(const GSHSCENE_Report * report) {
  GSHSCENE_Report_Summary s{};
  gshscene_report_summary(report, &s);
  return s;
}

} // namespace sinkconf

/* --- the suite --------------------------------------------------------------------------------- */

class SinkConformance : public ::testing::TestWithParam<const sinkconf::Fixture *> {
 protected:
  const sinkconf::Fixture * f() const {
    return GetParam();
  }
  GSHSCENE_Sink * make(const GSHSCENE_Allocator * alloc = nullptr, const GSHSCENE_Limits * limits = nullptr) {
    return f()->make(alloc, limits);
  }
};

using namespace sinkconf;

// S1: the zeroed request with a list, a target and a report is a whole-frame best-effort render.
TEST_P(SinkConformance, S1_TheZeroedRequestRendersTheWholeList) {
  for (uint32_t kind : f()->kinds) {
    SinkRef sink(make());
    Scene s;
    const GSHSCENE_List * list = basic(s);
    auto surface = make_surface(kind);
    ReportRef report;
    GSHSCENE_Sink_Caps caps = caps_of(sink.sink, *surface);
    Outcome o = run(f(), sink.sink, request_for(list, *surface, report.report));
    EXPECT_EQ(o.result, GSHSCENE_OK) << kind;
    EXPECT_EQ(o.callbacks, 1);
    EXPECT_EQ(o.callback_result, GSHSCENE_OK);
    GSHSCENE_Report_Summary sum = summary(report.report);
    EXPECT_EQ(sum.result, GSHSCENE_OK);
    EXPECT_EQ(sum.units_total, gshscene_list_expansion(list));
    EXPECT_EQ(sum.units_done, sum.units_total);
    EXPECT_EQ(sum.units_processed, sum.units_total);
    EXPECT_EQ(o.status.units_done, sum.units_total);
    EXPECT_EQ(sum.by_class[GSHSCENE_ENTRY_RESOLVED_AMBIGUITY], basic_ties(list));
    EXPECT_EQ(sum.by_class[GSHSCENE_ENTRY_REFUSED], 0u);
    if (caps.flags & GSHSCENE_CAP_CAN_BLOCK) {
      // The one-call path, on the same sink and a fresh target, says the same.
      auto again = make_surface(kind);
      ReportRef r2;
      GSHSCENE_Render_Request req = request_for(list, *again, r2.report);
      EXPECT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_OK);
      EXPECT_EQ(again->snapshot(), surface->snapshot());
      EXPECT_EQ(gshscene_report_hash(r2.report), gshscene_report_hash(report.report));
    }
  }
}

// S1: a bad request begins nothing and touches nothing.
TEST_P(SinkConformance, S1_ABadRequestBeginsNothing) {
  for (uint32_t kind : f()->kinds) {
    SinkRef sink(make());
    Scene s;
    const GSHSCENE_List * list = basic(s);
    GSHSCENE_List * open = s.open();
    auto surface = make_surface(kind);
    uint64_t before = surface->snapshot();
    ReportRef report;
    GSHSCENE_Render_Request good = request_for(list, *surface, report.report);
    std::vector<GSHSCENE_Render_Request> bad(7, good);
    bad[0].list = nullptr;
    bad[1].list = open;
    bad[2].target = nullptr;
    bad[3].report = nullptr;
    bad[4].flags = 1u << 7;
    bad[5]._reserved[3] = 1;
    bad[6].workspace_bytes = 16;
    for (size_t i = 0; i < bad.size(); i++) {
      GSHSCENE_Job job{};
      EXPECT_EQ(gshscene_sink_begin(sink.sink, &bad[i], &job), GSHSCENE_ERR_INVALID) << i;
      GSHSCENE_Job_Status st{};
      gshscene_job_poll(&job, &st);
      EXPECT_EQ(st.state, GSHSCENE_JOB_IDLE) << i;
    }
    EXPECT_EQ(gshscene_sink_begin(sink.sink, &good, nullptr), GSHSCENE_ERR_INVALID);
    EXPECT_EQ(gshscene_sink_begin(nullptr, &good, nullptr), GSHSCENE_ERR_INVALID);
    EXPECT_EQ(surface->snapshot(), before);
  }
}

// S2: caps are asked per target; the reserved kinds are unsupported; a kind the sink does not draw is unsupported.
TEST_P(SinkConformance, S2_CapsArePerTarget) {
  SinkRef sink(make());
  for (uint32_t kind = GSHSCENE_TARGET_RASTER; kind <= GSHSCENE_TARGET_STREAM; kind++) {
    auto surface = make_surface(kind);
    GSHSCENE_Sink_Caps caps{};
    GSHSCENE_Result r = gshscene_sink_caps(sink.sink, &surface->target, &caps);
    bool drawn = false;
    for (uint32_t k : f()->kinds) {
      drawn = drawn || k == kind;
    }
    if (drawn) {
      ASSERT_EQ(r, GSHSCENE_OK) << kind;
      EXPECT_EQ(caps.flags & ~GSHSCENE_CAP_FLAGS_ALL, 0u);
      EXPECT_TRUE(caps.cancel_unit == GSHSCENE_CANCEL_BETWEEN_UNITS || caps.cancel_unit == GSHSCENE_CANCEL_BEFORE_SUBMIT);
      EXPECT_NE(caps.work_measure, nullptr);
    } else {
      EXPECT_EQ(r, GSHSCENE_ERR_UNSUPPORTED) << kind;
    }
  }
  int token = 0;
  for (uint32_t kind : {(uint32_t)GSHSCENE_TARGET_GPU_TEXTURE, (uint32_t)GSHSCENE_TARGET_GPU_SURFACE}) {
    GSHSCENE_Target t{};
    t.kind = kind;
    t.surface = &token;
    GSHSCENE_Sink_Caps caps{};
    EXPECT_EQ(gshscene_sink_caps(sink.sink, &t, &caps), GSHSCENE_ERR_UNSUPPORTED);
  }
}

// S3: completion is observable; on_complete runs once, before the job reads as done; the job can be reused.
TEST_P(SinkConformance, S3_TheJobCompletesOnceAndCanBeReused) {
  uint32_t kind = f()->kinds[0];
  SinkRef sink(make());
  Scene s;
  const GSHSCENE_List * list = basic(s);
  auto surface = make_surface(kind);
  ReportRef report;
  for (int round = 0; round < 3; round++) {
    Outcome o = run(f(), sink.sink, request_for(list, *surface, report.report));
    EXPECT_EQ(o.result, GSHSCENE_OK);
    EXPECT_EQ(o.callbacks, 1);
    EXPECT_EQ(o.status.state, GSHSCENE_JOB_DONE);
  }
  GSHSCENE_Job_Status st{};
  EXPECT_EQ(gshscene_job_poll(nullptr, &st), GSHSCENE_ERR_INVALID);
}

namespace sinkconf {
struct Reentry {
  GSHSCENE_Sink * sink;
  GSHSCENE_Render_Request request; // on the same surface
  GSHSCENE_Result seen = GSHSCENE_OK;
  int calls = 0;
};
inline void begin_again(void * ctx, const GSHSCENE_Job *, size_t units) {
  Reentry * r = (Reentry *)ctx;
  if (units == 1) {
    r->calls++;
    GSHSCENE_Job job{};
    r->seen = gshscene_sink_begin(r->sink, &r->request, &job);
  }
}
} // namespace sinkconf

// S3: from begin until completion the surface is the sink's: a second render on it is refused.
TEST_P(SinkConformance, S3_ABusySurfaceIsRefused) {
  uint32_t kind = f()->kinds[0];
  SinkRef sink(make());
  Scene s;
  const GSHSCENE_List * list = basic(s);
  auto surface = make_surface(kind);
  ReportRef report;
  ReportRef other;
  Reentry re{sink.sink, request_for(list, *surface, other.report)};
  GSHSCENE_Render_Request req = request_for(list, *surface, report.report);
  req.progress = begin_again;
  req.progress_ctx = &re;
  Outcome o = run(f(), sink.sink, req);
  EXPECT_EQ(o.result, GSHSCENE_OK);
  ASSERT_EQ(re.calls, 1);
  EXPECT_EQ(re.seen, GSHSCENE_ERR_INVALID);
  // After completion the surface is free again.
  Outcome again = run(f(), sink.sink, request_for(list, *surface, other.report));
  EXPECT_EQ(again.result, GSHSCENE_OK);
}

// S4: target validation.
TEST_P(SinkConformance, S4_AnInvalidTargetIsRefused) {
  SinkRef sink(make());
  Scene s;
  const GSHSCENE_List * list = basic(s);
  ReportRef report;
  for (uint32_t kind : f()->kinds) {
    auto surface = make_surface(kind);
    uint64_t before = surface->snapshot();
    std::vector<std::pair<GSHSCENE_Target, GSHSCENE_Result>> cases;
    GSHSCENE_Target t = surface->target;
    GSHSCENE_Target c = t;
    c.kind = 0;
    cases.push_back({c, GSHSCENE_ERR_INVALID});
    c = t;
    c.kind = GSHSCENE_TARGET_KIND_COUNT;
    cases.push_back({c, GSHSCENE_ERR_INVALID});
    c = t;
    c.surface = nullptr;
    cases.push_back({c, GSHSCENE_ERR_INVALID});
    c = t;
    c.flags = 1u << 5;
    cases.push_back({c, GSHSCENE_ERR_INVALID});
    c = t;
    c._reserved[0] = 1;
    cases.push_back({c, GSHSCENE_ERR_INVALID});
    c = t;
    c.encoding = GSHSCENE_ENCODING_COUNT;
    cases.push_back({c, GSHSCENE_ERR_INVALID});
    c = t;
    c.encoding = GSHSCENE_ENCODING_HARDWARE_SRGB; // neither a raster nor a document is encoded by hardware
    cases.push_back({c, GSHSCENE_ERR_INVALID});
    c = t;
    c.unit = GSHSCENE_UNIT_COUNT;
    cases.push_back({c, GSHSCENE_ERR_INVALID});
    c = t;
    c.map.dim = 2; // a map that is neither zero nor valid
    c.map.m[0][0] = 1.0;
    c.map.m[1][1] = 0.0 / 1.0;
    c.map.t[0] = 1e300 * 1e300;
    cases.push_back({c, GSHSCENE_ERR_INVALID});
    c = t;
    c.blend_space.transfer = GCOL_TRANSFER_LINEAR; // a space given in part
    cases.push_back({c, GSHSCENE_ERR_INVALID});
    c = t;
    c.blend_space.primaries = GCOL_PRIMARIES_SRGB;
    c.blend_space.transfer = GCOL_TRANSFER_PQ;
    cases.push_back({c, GSHSCENE_ERR_UNSUPPORTED});
    if (kind == GSHSCENE_TARGET_RASTER) {
      c = t;
      c.aa = 1;
      cases.push_back({c, GSHSCENE_ERR_UNSUPPORTED});
      c = t;
      c.unit = GSHSCENE_UNIT_MILLIMETER;
      cases.push_back({c, GSHSCENE_ERR_INVALID});
      c = t;
      c.extent = GSHSCENE_Rect{0, 0, 65, 64};
      cases.push_back({c, GSHSCENE_ERR_INVALID});
      c = t;
      c.extent = GSHSCENE_Rect{-1, 0, 8, 8};
      cases.push_back({c, GSHSCENE_ERR_INVALID});
      c = t;
      c.extent = GSHSCENE_Rect{8, 8, 8, 9};
      cases.push_back({c, GSHSCENE_ERR_INVALID});
    } else {
      static const GSHSCENE_Rect damage{0, 0, 4, 4};
      c = t;
      c.unit = GSHSCENE_UNIT_DEFAULT; // a document states its unit
      cases.push_back({c, GSHSCENE_ERR_INVALID});
      c = t;
      c.extent = GSHSCENE_Rect{0, 0, 4, 4}; // pixel-kind fields are zero on a stream
      cases.push_back({c, GSHSCENE_ERR_INVALID});
      c = t;
      c.damage = &damage;
      cases.push_back({c, GSHSCENE_ERR_INVALID});
      c = t;
      c.aa = 1;
      cases.push_back({c, GSHSCENE_ERR_INVALID});
    }
    for (size_t i = 0; i < cases.size(); i++) {
      GSHSCENE_Render_Request req = request_for(list, *surface, report.report);
      req.target = &cases[i].first;
      GSHSCENE_Job job{};
      EXPECT_EQ(gshscene_sink_begin(sink.sink, &req, &job), cases[i].second) << kind << " case " << i;
      GSHSCENE_Sink_Caps caps{};
      EXPECT_EQ(gshscene_sink_caps(sink.sink, &cases[i].first, &caps), cases[i].second) << kind << " case " << i;
    }
    EXPECT_EQ(surface->snapshot(), before);
  }
}

// S4: inside the damage the render equals the whole render; outside nothing changes.
TEST_P(SinkConformance, S4_DamageDrawsInsideAndTouchesNothingOutside) {
  bool any = false;
  for (uint32_t kind : f()->kinds) {
    if (kind != GSHSCENE_TARGET_RASTER) {
      continue;
    }
    any = true;
    SinkRef sink(make());
    Scene s;
    const GSHSCENE_List * list = basic(s);
    auto whole = make_raster();
    auto part = make_raster();
    auto fresh = make_raster();
    ReportRef report;
    ASSERT_EQ(run(f(), sink.sink, request_for(list, *whole, report.report)).result, GSHSCENE_OK);
    const GSHSCENE_Rect damage{5, 7, 37, 41};
    part->target.damage = &damage;
    ASSERT_EQ(run(f(), sink.sink, request_for(list, *part, report.report)).result, GSHSCENE_OK);
    for (uint32_t y = 0; y < 64; y++) {
      for (uint32_t x = 0; x < 64; x++) {
        bool inside = x >= 5 && x < 37 && y >= 7 && y < 41;
        ASSERT_EQ(part->pixel(x, y), inside ? whole->pixel(x, y) : fresh->pixel(x, y)) << x << "," << y;
      }
    }
    // An extent is a damage that is also the clip of the render.
    auto ext = make_raster();
    ext->target.extent = GSHSCENE_Rect{5, 7, 37, 41};
    ASSERT_EQ(run(f(), sink.sink, request_for(list, *ext, report.report)).result, GSHSCENE_OK);
    EXPECT_EQ(ext->snapshot(), part->snapshot());
  }
  if (!any) {
    GTEST_SKIP() << "no pixel target: damage is a pixel-kind field (S4)";
  }
}

// S5: the report's order and its summary agree with its entries.
TEST_P(SinkConformance, S5_TheReportIsInOrderAndItsSummaryAgrees) {
  for (uint32_t kind : f()->kinds) {
    SinkRef sink(make());
    Scene s;
    const GSHSCENE_List * list = basic(s);
    auto surface = make_surface(kind);
    ReportRef report;
    ASSERT_EQ(run(f(), sink.sink, request_for(list, *surface, report.report)).result, GSHSCENE_OK);
    auto e = entries(report.report);
    GSHSCENE_Report_Summary sum = summary(report.report);
    size_t by_class[GSHSCENE_ENTRY_CLASS_COUNT] = {0, 0, 0, 0, 0};
    for (size_t i = 0; i < e.size(); i++) {
      by_class[e[i].entry_class]++;
      if (i == 0) {
        continue;
      }
      std::vector<size_t> a(e[i - 1].chain, e[i - 1].chain + e[i - 1].chain_count);
      std::vector<size_t> b(e[i].chain, e[i].chain + e[i].chain_count);
      ASSERT_TRUE(a < b || (a == b && (e[i - 1].entry_class < e[i].entry_class ||
                                          (e[i - 1].entry_class == e[i].entry_class && e[i - 1].reason <= e[i].reason))))
          << "entries " << i - 1 << " and " << i << " are out of order";
    }
    for (int c = 1; c < GSHSCENE_ENTRY_CLASS_COUNT; c++) {
      EXPECT_EQ(sum.by_class[c], by_class[c]) << c;
    }
    EXPECT_EQ(sum.counted, e.size());
    EXPECT_EQ(sum.overflowed, 0);
  }
}

// S5: past the cap the report counts, says it overflowed, and the render is ERR_LIMIT.
TEST_P(SinkConformance, S5_TheReportCapIsALimitNotATruncation) {
  uint32_t kind = f()->kinds[0];
  Scene s;
  const GSHSCENE_List * list = basic(s);
  size_t ties = basic_ties(list);
  ASSERT_GE(ties, 2u);
  for (size_t cap : {ties - 1, ties}) {
    SinkRef sink(make());
    GSHSCENE_Limits limits{};
    limits.max_report_entries = cap;
    ReportRef report(nullptr, &limits);
    auto surface = make_surface(kind);
    GSHSCENE_Sink_Caps caps = caps_of(sink.sink, *surface);
    size_t expected = ties;
    if (!(caps.flags & GSHSCENE_CAP_CLIP_AA)) {
      expected += 1;
    }
    if (!(caps.flags & GSHSCENE_CAP_BLEND_SPACES)) {
      expected += 1;
    }
    Outcome o = run(f(), sink.sink, request_for(list, *surface, report.report));
    GSHSCENE_Report_Summary sum = summary(report.report);
    EXPECT_EQ(sum.counted, expected);
    if (expected > cap) {
      EXPECT_EQ(o.result, GSHSCENE_ERR_LIMIT) << cap;
      EXPECT_EQ(sum.overflowed, 1);
      EXPECT_EQ(gshscene_report_count(report.report), cap);
    } else {
      EXPECT_EQ(o.result, GSHSCENE_OK) << cap;
      EXPECT_EQ(sum.overflowed, 0);
    }
  }
}

// S6: best effort skips what the caps refuse and says so; strict refuses before the target is touched.
TEST_P(SinkConformance, S6_BestEffortSkipsAndStrictRefusesBeforeDrawing) {
  bool any = false;
  for (uint32_t kind : f()->kinds) {
    SinkRef sink(make());
    Scene s;
    auto surface = make_surface(kind);
    GSHSCENE_Sink_Caps caps = caps_of(sink.sink, *surface);
    GSHSCENE_Reason reason = GSHSCENE_REASON_COUNT;
    const GSHSCENE_List * list = refusable(s, caps, &reason);
    if (!list) {
      continue;
    }
    any = true;
    ReportRef report;
    Outcome o = run(f(), sink.sink, request_for(list, *surface, report.report));
    EXPECT_EQ(o.result, GSHSCENE_OK);
    auto e = entries(report.report);
    ASSERT_EQ(summary(report.report).by_class[GSHSCENE_ENTRY_REFUSED], 1u);
    bool found = false;
    for (const auto & x : e) {
      if (x.entry_class == GSHSCENE_ENTRY_REFUSED) {
        found = true;
        EXPECT_EQ(x.reason, reason);
        ASSERT_EQ(x.chain_count, 1u);
        EXPECT_EQ(x.chain[0], 1u);
      }
    }
    EXPECT_TRUE(found);
    EXPECT_EQ(summary(report.report).units_done, gshscene_list_expansion(list));

    auto strict_surface = make_surface(kind);
    uint64_t before = strict_surface->snapshot();
    ReportRef strict_report;
    GSHSCENE_Render_Request req = request_for(list, *strict_surface, strict_report.report);
    req.flags = GSHSCENE_RENDER_STRICT;
    Outcome so = run(f(), sink.sink, req);
    EXPECT_EQ(so.result, GSHSCENE_ERR_REFUSED);
    EXPECT_EQ(so.callbacks, 1);
    EXPECT_EQ(strict_surface->snapshot(), before) << "strict with a planned refusal touched the target";
    EXPECT_EQ(summary(strict_report.report).units_processed, 0u);
    EXPECT_EQ(summary(strict_report.report).by_class[GSHSCENE_ENTRY_REFUSED], 1u);
  }
  if (!any) {
    GTEST_SKIP() << "the caps refuse nothing the suite can build";
  }
}

// S6, AD-12, AD-23: an approximation is reported and never fails a render, strict or not.
TEST_P(SinkConformance, S6_ApproximationsAreReportedAndNeverFail) {
  for (uint32_t kind : f()->kinds) {
    SinkRef sink(make());
    Scene s;
    const GSHSCENE_List * list = basic(s);
    auto surface = make_surface(kind);
    GSHSCENE_Sink_Caps caps = caps_of(sink.sink, *surface);
    size_t expected = (caps.flags & GSHSCENE_CAP_CLIP_AA ? 0u : 1u) + (caps.flags & GSHSCENE_CAP_BLEND_SPACES ? 0u : 1u);
    ReportRef report;
    GSHSCENE_Render_Request req = request_for(list, *surface, report.report);
    req.flags = GSHSCENE_RENDER_STRICT;
    EXPECT_EQ(run(f(), sink.sink, req).result, GSHSCENE_OK);
    EXPECT_EQ(summary(report.report).by_class[GSHSCENE_ENTRY_APPROXIMATED], expected);
    for (const auto & x : entries(report.report)) {
      if (x.entry_class == GSHSCENE_ENTRY_APPROXIMATED) {
        EXPECT_TRUE(x.reason == (x.chain[0] == 1 ? GSHSCENE_REASON_CLIP_ALIASED : GSHSCENE_REASON_BLEND_SPACE));
      }
    }
  }
}

namespace sinkconf {
struct CancelAt {
  GSHSCENE_Cancel * cancel;
  size_t at;
};
inline void cancel_at(void * ctx, const GSHSCENE_Job *, size_t units) {
  CancelAt * c = (CancelAt *)ctx;
  if (units == c->at) {
    gshscene_cancel_set(c->cancel);
  }
}
} // namespace sinkconf

// S7: a token set before the render begins: nothing is done.
TEST_P(SinkConformance, S7_CancelledBeforeTheFirstUnitDoesNothing) {
  for (uint32_t kind : f()->kinds) {
    SinkRef sink(make());
    Scene s;
    const GSHSCENE_List * list = basic(s);
    auto surface = make_surface(kind);
    uint64_t before = surface->snapshot();
    ReportRef report;
    GSHSCENE_Cancel cancel;
    gshscene_cancel_init(&cancel);
    gshscene_cancel_set(&cancel);
    GSHSCENE_Render_Request req = request_for(list, *surface, report.report);
    req.cancel = &cancel;
    Outcome o = run(f(), sink.sink, req);
    EXPECT_EQ(o.result, GSHSCENE_ERR_CANCELLED);
    EXPECT_EQ(o.callbacks, 1);
    EXPECT_EQ(o.status.units_processed, 0u);
    EXPECT_EQ(surface->snapshot(), before);
    EXPECT_TRUE(gshscene_cancel_is_set(&cancel)) << "a sink never clears the token";
  }
}

// S7: the token set in the progress call of unit k; the sink stops where its caps say, within its declared bound.
TEST_P(SinkConformance, S7_CancelStopsAtTheDeclaredUnitWithinTheDeclaredBound) {
  for (uint32_t kind : f()->kinds) {
    Scene s;
    const GSHSCENE_List * list = basic(s);
    size_t total = gshscene_list_expansion(list);
    for (size_t k = 1; k < total; k++) {
      SinkRef sink(make());
      auto surface = make_surface(kind);
      GSHSCENE_Sink_Caps caps = caps_of(sink.sink, *surface);
      ReportRef report;
      GSHSCENE_Cancel cancel;
      gshscene_cancel_init(&cancel);
      CancelAt at{&cancel, k};
      GSHSCENE_Render_Request req = request_for(list, *surface, report.report);
      req.cancel = &cancel;
      req.progress = cancel_at;
      req.progress_ctx = &at;
      Outcome o = run(f(), sink.sink, req);
      GSHSCENE_Report_Summary sum = summary(report.report);
      EXPECT_LE(sum.work_max, caps.cancel_work_bound) << "k " << k;
      EXPECT_LE(sum.units_done, sum.units_processed);
      if (caps.cancel_unit == GSHSCENE_CANCEL_BETWEEN_UNITS) {
        EXPECT_EQ(o.result, GSHSCENE_ERR_CANCELLED) << "k " << k;
        EXPECT_EQ(sum.units_processed, k) << "a token set during unit k's progress call is seen before unit k + 1";
      } else {
        EXPECT_EQ(o.result, GSHSCENE_OK) << "a before-submit sink completes once submitted";
        EXPECT_EQ(sum.units_processed, total);
      }
    }
  }
}

// S7: a whole render keeps its declared work bound.
TEST_P(SinkConformance, S7_TheWorkBetweenChecksIsWithinTheDeclaredBound) {
  for (uint32_t kind : f()->kinds) {
    SinkRef sink(make());
    Scene s;
    const GSHSCENE_List * list = basic(s);
    auto surface = make_surface(kind);
    GSHSCENE_Sink_Caps caps = caps_of(sink.sink, *surface);
    ReportRef report;
    Outcome o = run(f(), sink.sink, request_for(list, *surface, report.report));
    ASSERT_EQ(o.result, GSHSCENE_OK);
    EXPECT_LE(summary(report.report).work_max, caps.cancel_work_bound);
    EXPECT_EQ(o.status.work_max, summary(report.report).work_max);
    EXPECT_GT(summary(report.report).work_max, 0u) << "a sink that did work states some";
  }
}

// S8: the conflation flag is refused before drawing, with and without strict.
TEST_P(SinkConformance, S8_TheConflationFlagIsRefusedBeforeDrawing) {
  for (uint32_t kind : f()->kinds) {
    for (uint32_t flags : {0u, (uint32_t)GSHSCENE_RENDER_STRICT}) {
      SinkRef sink(make());
      Scene s;
      const GSHSCENE_List * list = basic(s);
      auto surface = make_surface(kind);
      surface->target.flags = GSHSCENE_TARGET_SEAMLESS;
      uint64_t before = surface->snapshot();
      ReportRef report;
      GSHSCENE_Render_Request req = request_for(list, *surface, report.report);
      req.flags = flags;
      Outcome o = run(f(), sink.sink, req);
      EXPECT_EQ(o.result, GSHSCENE_ERR_REFUSED);
      EXPECT_EQ(surface->snapshot(), before);
      auto e = entries(report.report);
      ASSERT_EQ(e.size(), 1u);
      EXPECT_EQ(e[0].entry_class, GSHSCENE_ENTRY_REFUSED);
      EXPECT_EQ(e[0].reason, GSHSCENE_REASON_SEAMLESS);
      EXPECT_EQ(e[0].chain_count, 0u);
    }
  }
}

// S9: caller memory: the exact workspace, no allocation over a loop of frames, the same output; one byte less is a
// limit.  A sink without the cap refuses the flag.
TEST_P(SinkConformance, S9_CallerMemoryAllocatesNothing) {
  for (uint32_t kind : f()->kinds) {
    tracker::Tracker t;
    GSHSCENE_Allocator alloc = tracker::allocator_for(&t);
    Scene s;
    const GSHSCENE_List * list = basic(s);
    {
      SinkRef sink(make(&alloc));
      ReportRef report(&alloc);
      auto reference = make_surface(kind);
      ASSERT_EQ(run(f(), sink.sink, request_for(list, *reference, report.report)).result, GSHSCENE_OK);
      uint64_t reference_report = gshscene_report_hash(report.report);
      auto surface = make_surface(kind);
      GSHSCENE_Sink_Caps caps = caps_of(sink.sink, *surface);
      GSHSCENE_Render_Request req = request_for(list, *surface, report.report);
      req.flags = GSHSCENE_RENDER_CALLER_MEMORY;
      size_t need = 0;
      if (!(caps.flags & GSHSCENE_CAP_CALLER_MEMORY)) {
        EXPECT_EQ(gshscene_sink_workspace_bytes(sink.sink, &req, &need), GSHSCENE_ERR_REFUSED);
        uint64_t before = surface->snapshot();
        Outcome o = run(f(), sink.sink, req);
        EXPECT_EQ(o.result, GSHSCENE_ERR_REFUSED);
        EXPECT_EQ(surface->snapshot(), before);
        auto e = entries(report.report);
        ASSERT_EQ(e.size(), 1u);
        EXPECT_EQ(e[0].reason, GSHSCENE_REASON_CALLER_MEMORY);
        continue;
      }
      ASSERT_EQ(gshscene_sink_workspace_bytes(sink.sink, &req, &need), GSHSCENE_OK);
      ASSERT_GT(need, 0u);
      // The workspace at an odd address, and 4096 bytes of canary after it: a sink writes nothing outside it.
      std::vector<unsigned char> workspace(need + 1 + 4096, 0xc5);
      // One byte less: a limit, naming the need, the target untouched.
      req.workspace = workspace.data() + 1;
      req.workspace_bytes = need - 1;
      uint64_t before = surface->snapshot();
      Outcome small = run(f(), sink.sink, req);
      EXPECT_EQ(small.result, GSHSCENE_ERR_LIMIT);
      EXPECT_EQ(summary(report.report).workspace_needed, need);
      EXPECT_EQ(surface->snapshot(), before);
      // Exactly the need, at an odd address: frames 2..n allocate nothing anywhere.
      req.workspace_bytes = need;
      for (int frame = 0; frame < 5; frame++) {
        auto fresh = make_surface(kind);
        req.target = &fresh->target;
        long calls = t.calls;
        Outcome o = run(f(), sink.sink, req);
        EXPECT_EQ(o.result, GSHSCENE_OK);
        if (frame > 0) {
          EXPECT_EQ(t.calls, calls) << "frame " << frame << " allocated";
        }
        EXPECT_EQ(fresh->snapshot(), reference->snapshot());
        for (size_t i = need + 1; i < workspace.size(); i++) {
          ASSERT_EQ(workspace[i], 0xc5) << "the sink wrote past its workspace, at byte " << i - 1;
        }
        GSHSCENE_Report_Summary sum = summary(report.report);
        EXPECT_EQ(sum.workspace_needed, need);
        // The report is the reference's but for the workspace it states.
        (void)reference_report;
      }
    }
    EXPECT_TRUE(t.live.empty());
  }
}

// S10: after a render the sink holds no reference; prepare holds by id until evict or destroy.
TEST_P(SinkConformance, S10_NoReferenceOutlivesTheRenderButPreparedOnes) {
  for (uint32_t kind : f()->kinds) {
    Scene s;
    const GSHSCENE_List * list = basic(s);
    std::vector<size_t> counts;
    for (const GSHSCENE_Resource * r : s.held) {
      counts.push_back(gshscene_resource_refcount(r));
    }
    auto surface = make_surface(kind);
    tracker::Tracker t;
    GSHSCENE_Allocator alloc = tracker::allocator_for(&t);
    {
      SinkRef sink(make(&alloc));
      GSHSCENE_Sink_Caps caps = caps_of(sink.sink, *surface);
      ReportRef report;
      ASSERT_EQ(run(f(), sink.sink, request_for(list, *surface, report.report)).result, GSHSCENE_OK);
      for (size_t i = 0; i < s.held.size(); i++) {
        EXPECT_EQ(gshscene_resource_refcount(s.held[i]), counts[i]) << i;
      }
      ASSERT_EQ(gshscene_sink_prepare(sink.sink, list, &surface->target), GSHSCENE_OK);
      uint64_t bytes = 0;
      ASSERT_EQ(gshscene_sink_static_bytes(sink.sink, &bytes), GSHSCENE_OK);
      if (!(caps.flags & GSHSCENE_CAP_PREPARE)) {
        EXPECT_EQ(bytes, 0u);
        continue;
      }
      uint64_t expect = 0;
      for (size_t i = 0; i < s.held.size(); i++) {
        EXPECT_EQ(gshscene_resource_refcount(s.held[i]), counts[i] + 1) << i;
        expect += gshscene_resource_bytes(s.held[i]);
      }
      EXPECT_EQ(bytes, expect);
      ASSERT_EQ(run(f(), sink.sink, request_for(list, *surface, report.report)).result, GSHSCENE_OK);
      EXPECT_EQ(summary(report.report).static_bytes, expect);
      EXPECT_EQ(gshscene_sink_evict(sink.sink, gshscene_resource_id(s.held[0])), GSHSCENE_OK);
      EXPECT_EQ(gshscene_resource_refcount(s.held[0]), counts[0]);
      ASSERT_EQ(gshscene_sink_static_bytes(sink.sink, &bytes), GSHSCENE_OK);
      EXPECT_EQ(bytes, expect - gshscene_resource_bytes(s.held[0])) << "evict gives the bytes back";
      EXPECT_EQ(gshscene_sink_evict(sink.sink, 0), GSHSCENE_OK);
    }
    for (size_t i = 0; i < s.held.size(); i++) {
      EXPECT_EQ(gshscene_resource_refcount(s.held[i]), counts[i]) << "destroy releases what prepare held";
    }
    EXPECT_TRUE(t.live.empty()) << "the sink freed everything it took";
  }
}

// S12: the output and the report do not depend on the allocator, the instance of the sink or the workspace's address.
TEST_P(SinkConformance, S12_TheOutputDependsOnTheListTargetAndRequestOnly) {
  for (uint32_t kind : f()->kinds) {
    Scene s;
    const GSHSCENE_List * list = basic(s);
    tracker::Tracker t;
    GSHSCENE_Allocator alloc = tracker::allocator_for(&t);
    SinkRef a(make());
    SinkRef b(make(&alloc));
    auto sa = make_surface(kind);
    auto sb = make_surface(kind);
    ReportRef ra;
    ReportRef rb(&alloc);
    ASSERT_EQ(run(f(), a.sink, request_for(list, *sa, ra.report)).result, GSHSCENE_OK);
    ASSERT_EQ(run(f(), b.sink, request_for(list, *sb, rb.report)).result, GSHSCENE_OK);
    EXPECT_EQ(sa->snapshot(), sb->snapshot());
    EXPECT_EQ(gshscene_report_hash(ra.report), gshscene_report_hash(rb.report));
  }
}

// S13: a sink that cannot block refuses wait and the one call before anything begins; poll and on_complete see the end.
TEST_P(SinkConformance, S13_ASinkThatCannotBlockNeverBlocks) {
  for (uint32_t kind : f()->kinds) {
    SinkRef sink(make());
    Scene s;
    const GSHSCENE_List * list = basic(s);
    auto surface = make_surface(kind);
    GSHSCENE_Sink_Caps caps = caps_of(sink.sink, *surface);
    if (caps.flags & GSHSCENE_CAP_CAN_BLOCK) {
      continue;
    }
    uint64_t before = surface->snapshot();
    ReportRef report;
    GSHSCENE_Render_Request req = request_for(list, *surface, report.report);
    EXPECT_EQ(gshscene_sink_render(sink.sink, &req), GSHSCENE_ERR_UNSUPPORTED);
    EXPECT_EQ(surface->snapshot(), before);
    Outcome o;
    req.on_complete = count_complete;
    req.on_complete_ctx = &o;
    GSHSCENE_Job job{};
    ASSERT_EQ(gshscene_sink_begin(sink.sink, &req, &job), GSHSCENE_OK);
    EXPECT_EQ(gshscene_sink_wait(sink.sink, &job), GSHSCENE_ERR_UNSUPPORTED);
    GSHSCENE_Job_Status st{};
    for (int turns = 0; turns < 1000000; turns++) {
      gshscene_job_poll(&job, &st);
      if (st.state == GSHSCENE_JOB_DONE) {
        break;
      }
      ASSERT_NE(f()->pump, nullptr);
      f()->pump(sink.sink);
    }
    EXPECT_EQ(st.state, GSHSCENE_JOB_DONE);
    EXPECT_EQ(st.result, GSHSCENE_OK);
    EXPECT_EQ(o.callbacks, 1);
    EXPECT_EQ(gshscene_sink_wait(sink.sink, &job), GSHSCENE_ERR_UNSUPPORTED) << "even when the job is done";
  }
}

// S14: a thread-affine sink refuses calls from another thread; the cancel token works from any thread.
TEST_P(SinkConformance, S14_ThreadAffinity) {
  for (uint32_t kind : f()->kinds) {
    SinkRef sink(make());
    Scene s;
    const GSHSCENE_List * list = basic(s);
    auto surface = make_surface(kind);
    GSHSCENE_Sink_Caps caps = caps_of(sink.sink, *surface);
    ReportRef report;
    GSHSCENE_Render_Request req = request_for(list, *surface, report.report);
    GSHSCENE_Result from_other = GSHSCENE_ERR_INTERNAL;
    GSHSCENE_Job other_job{};
    std::thread th([&] { from_other = gshscene_sink_begin(sink.sink, &req, &other_job); });
    th.join();
    if (caps.flags & GSHSCENE_CAP_THREAD_AFFINE) {
      EXPECT_EQ(from_other, GSHSCENE_ERR_INVALID);
      EXPECT_EQ(surface->snapshot(), make_surface(kind)->snapshot());
      // A job begun here cannot be polled there.
      GSHSCENE_Job job{};
      GSHSCENE_Result begun = gshscene_sink_begin(sink.sink, &req, &job);
      ASSERT_TRUE(begun == GSHSCENE_OK);
      GSHSCENE_Result polled = GSHSCENE_ERR_INTERNAL;
      std::thread tp([&] {
        GSHSCENE_Job_Status st{};
        polled = gshscene_job_poll(&job, &st);
      });
      tp.join();
      EXPECT_EQ(polled, GSHSCENE_ERR_INVALID);
      if (caps.flags & GSHSCENE_CAP_CAN_BLOCK) {
        GSHSCENE_Result waited = GSHSCENE_ERR_INTERNAL;
        std::thread tw([&] { waited = gshscene_sink_wait(sink.sink, &job); });
        tw.join();
        EXPECT_EQ(waited, GSHSCENE_ERR_INVALID) << "a wait from another thread";
      }
      GSHSCENE_Job_Status st{};
      for (int turns = 0; turns < 1000000; turns++) {
        gshscene_job_poll(&job, &st);
        if (st.state == GSHSCENE_JOB_DONE) {
          break;
        }
        if (caps.flags & GSHSCENE_CAP_CAN_BLOCK) {
          gshscene_sink_wait(sink.sink, &job);
        } else {
          f()->pump(sink.sink);
        }
      }
      // destroy from another thread is refused too
      GSHSCENE_Result destroyed = GSHSCENE_ERR_INTERNAL;
      std::thread td([&] { destroyed = gshscene_sink_destroy(sink.sink); });
      td.join();
      EXPECT_EQ(destroyed, GSHSCENE_ERR_INVALID);
    } else {
      EXPECT_EQ(from_other, GSHSCENE_OK);
      GSHSCENE_Job_Status st{};
      for (int turns = 0; turns < 1000000; turns++) {
        gshscene_job_poll(&other_job, &st);
        if (st.state == GSHSCENE_JOB_DONE) {
          break;
        }
        if (caps.flags & GSHSCENE_CAP_CAN_BLOCK) {
          gshscene_sink_wait(sink.sink, &other_job);
        } else {
          f()->pump(sink.sink);
        }
      }
      EXPECT_EQ(st.result, GSHSCENE_OK);
    }
    // The token, set on another thread, is honored here.
    GSHSCENE_Cancel cancel;
    gshscene_cancel_init(&cancel);
    std::thread tc([&] { gshscene_cancel_set(&cancel); });
    tc.join();
    ReportRef r2;
    auto s2 = make_surface(kind);
    GSHSCENE_Render_Request rq = request_for(list, *s2, r2.report);
    rq.cancel = &cancel;
    EXPECT_EQ(run(f(), sink.sink, rq).result, GSHSCENE_ERR_CANCELLED);
  }
}

// S15: readback copies the extent's pixels of a completed render; a sink without it says so.
TEST_P(SinkConformance, S15_ReadbackCopiesTheExtent) {
  for (uint32_t kind : f()->kinds) {
    SinkRef sink(make());
    Scene s;
    const GSHSCENE_List * list = basic(s);
    auto surface = make_surface(kind);
    if (kind == GSHSCENE_TARGET_RASTER) {
      surface->target.extent = GSHSCENE_Rect{3, 5, 40, 44};
    }
    GSHSCENE_Sink_Caps caps = caps_of(sink.sink, *surface);
    ReportRef report;
    GSHSCENE_Render_Request req = request_for(list, *surface, report.report);
    GSHSCENE_Job job{};
    GSHSCENE_Result begun = gshscene_sink_begin(sink.sink, &req, &job);
    ASSERT_EQ(begun, GSHSCENE_OK);
    GSHSCENE_Job_Status st{};
    for (int turns = 0; turns < 1000000; turns++) {
      gshscene_job_poll(&job, &st);
      if (st.state == GSHSCENE_JOB_DONE) {
        break;
      }
      GIMG_Raster * early = nullptr;
      if (kind == GSHSCENE_TARGET_RASTER && turns == 0 && (caps.flags & GSHSCENE_CAP_READBACK)) {
        gimg_raster_create(37, 39, gimg_raster_format(surface->raster), GIMG_RASTER_OWNED, nullptr, 0, &early);
        EXPECT_EQ(gshscene_sink_readback(sink.sink, &job, early), GSHSCENE_ERR_INVALID) << "not done";
        gimg_raster_destroy(early);
      }
      if (caps.flags & GSHSCENE_CAP_CAN_BLOCK) {
        gshscene_sink_wait(sink.sink, &job);
      } else {
        f()->pump(sink.sink);
      }
    }
    ASSERT_EQ(st.result, GSHSCENE_OK);
    if (kind != GSHSCENE_TARGET_RASTER) {
      auto r = make_raster();
      GSHSCENE_Result got = gshscene_sink_readback(sink.sink, &job, r->raster);
      EXPECT_TRUE(got == GSHSCENE_ERR_INVALID || got == GSHSCENE_ERR_UNSUPPORTED);
      continue;
    }
    GIMG_Raster * out = nullptr;
    ASSERT_EQ(gimg_raster_create(37, 39, gimg_raster_format(surface->raster), GIMG_RASTER_OWNED, nullptr, 0, &out), GIMG_OK);
    GSHSCENE_Result got = gshscene_sink_readback(sink.sink, &job, out);
    if (!(caps.flags & GSHSCENE_CAP_READBACK)) {
      EXPECT_EQ(got, GSHSCENE_ERR_UNSUPPORTED);
    } else {
      ASSERT_EQ(got, GSHSCENE_OK);
      for (uint32_t y = 0; y < 39; y++) {
        for (uint32_t x = 0; x < 37; x++) {
          uint32_t v;
          std::memcpy(&v, (const unsigned char *)gimg_raster_pixels_const(out) + y * gimg_raster_stride_bytes(out) + x * 4u, 4);
          ASSERT_EQ(v, surface->pixel(x + 3, y + 5));
        }
      }
      GIMG_Raster * wrong = nullptr;
      gimg_raster_create(36, 39, gimg_raster_format(surface->raster), GIMG_RASTER_OWNED, nullptr, 0, &wrong);
      EXPECT_EQ(gshscene_sink_readback(sink.sink, &job, wrong), GSHSCENE_ERR_INVALID);
      gimg_raster_destroy(wrong);
    }
    gimg_raster_destroy(out);
  }
}

// Allocation failure at every call of a render: ERR_OOM or OK, a consistent report, no leak.
TEST_P(SinkConformance, EveryAllocationOfARenderCanFail) {
  for (uint32_t kind : f()->kinds) {
    Scene s;
    const GSHSCENE_List * list = basic(s);
    bool reached_ok = false;
    int ooms = 0;
    for (long fail = 0; fail < 400 && !reached_ok; fail++) {
      tracker::Tracker t;
      GSHSCENE_Allocator alloc = tracker::allocator_for(&t);
      GSHSCENE_Sink * raw = make(&alloc);
      ASSERT_NE(raw, nullptr);
      {
        SinkRef sink(raw);
        GSHSCENE_Report * report = nullptr;
        ASSERT_EQ(gshscene_report_create(&alloc, nullptr, &report), GSHSCENE_OK);
        auto surface = make_surface(kind);
        t.fail_from = t.calls + fail;
        Outcome o = run(f(), sink.sink, request_for(list, *surface, report));
        t.fail_from = -1;
        if (o.result == GSHSCENE_OK) {
          reached_ok = true;
        } else {
          EXPECT_EQ(o.result, GSHSCENE_ERR_OOM) << "fail " << fail;
          EXPECT_EQ(summary(report).result, GSHSCENE_ERR_OOM);
          EXPECT_EQ(o.callbacks, 1);
          ooms++;
        }
        gshscene_report_destroy(report);
      }
      EXPECT_TRUE(t.live.empty()) << "fail " << fail << " leaked";
    }
    EXPECT_TRUE(reached_ok);
    EXPECT_GT(ooms, 0) << "no allocation of a render was failed: the sweep saw nothing";
  }
}

// A failing stream at every write: ERR_IO.
TEST_P(SinkConformance, EveryWriteToAStreamCanFail) {
  bool any = false;
  for (uint32_t kind : f()->kinds) {
    if (kind != GSHSCENE_TARGET_STREAM) {
      continue;
    }
    any = true;
    Scene s;
    const GSHSCENE_List * list = basic(s);
    SinkRef sink(make());
    long writes = 0;
    {
      auto surface = make_stream();
      ReportRef report;
      ASSERT_EQ(run(f(), sink.sink, request_for(list, *surface, report.report)).result, GSHSCENE_OK);
      writes = surface->writes;
      ASSERT_EQ(writes, 0) << "counting starts when fail_at is set";
      auto counting = make_stream();
      counting->fail_at = 1L << 40;
      ASSERT_EQ(run(f(), sink.sink, request_for(list, *counting, report.report)).result, GSHSCENE_OK);
      writes = counting->writes;
    }
    ASSERT_GT(writes, 0);
    for (long k = 0; k < writes; k++) {
      auto surface = make_stream();
      surface->fail_at = k;
      ReportRef report;
      EXPECT_EQ(run(f(), sink.sink, request_for(list, *surface, report.report)).result, GSHSCENE_ERR_IO) << "write " << k;
    }
  }
  if (!any) {
    GTEST_SKIP() << "no stream target";
  }
}

#endif /* GHOTI_IO_GSHSCENE_TEST_SINK_CONFORMANCE_H */
