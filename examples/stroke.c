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
 * Strokes (stroke.h, SK1 to SK14): an SVG-like element, a square filled and then stroked (SK14: two items in paint
 * order), with a dashed, round-joined style; the same drawing spelled two ways interned as one style (SK3); a
 * zero-width stroke reported as a notice that never fails a strict render (SK8); and the same list given to three
 * sinks that differ only in their stroke capabilities: one that draws every feature as a stroke, a writer that emits
 * what its format has and expands the rest, and a toolpath that follows the centerline and refuses what its pen cannot
 * do, naming the missing features (S20).  It checks what it shows and exits non-zero when that does not hold.
 */

#include <ghoti.io/shape-scene/shape-scene.h>

#include <stdio.h>
#include <string.h>

static int failures;

static void expect(int ok, const char * what) {
  if (!ok) {
    fprintf(stderr, "stroke: %s\n", what);
    failures++;
  }
}

static const GSHSCENE_Resource * square(GSHSCENE_Store * store, double x, double y, double size) {
  GSHGEOM_Path * path = NULL;
  gshgeom_path_create(NULL, NULL, 2, &path);
  gshgeom_path_move_to(path, (GSHGEOM_Point){x, y, 0});
  gshgeom_path_line_to(path, (GSHGEOM_Point){x + size, y, 0});
  gshgeom_path_line_to(path, (GSHGEOM_Point){x + size, y + size, 0});
  gshgeom_path_line_to(path, (GSHGEOM_Point){x, y + size, 0});
  gshgeom_path_close(path);
  const GSHSCENE_Resource * r = NULL;
  gshscene_store_intern_path(store, path, &r);
  gshgeom_path_destroy(path);
  return r;
}

/** A writer's bytes, kept to show. */
typedef struct Text {
  char bytes[4096];
  size_t used;
} Text;

static GSHSCENE_Result keep(void * ctx, const void * bytes, size_t count) {
  Text * t = ctx;
  size_t room = sizeof t->bytes - 1 - t->used;
  size_t n = count < room ? count : room;
  memcpy(t->bytes + t->used, bytes, n);
  t->used += n;
  t->bytes[t->used] = '\0';
  return GSHSCENE_OK;
}

/** Render @p list to a stream in points through a test sink with @p caps; count the entries by class. */
static GSHSCENE_Result render(const GSHSCENE_Sink_Caps * caps, const GSHSCENE_List * list, uint32_t flags,
    const char * name, size_t * notices, size_t * refusals) {
  GSHSCENE_Dump_Sink_Config config;
  gshscene_dump_sink_config_default(&config);
  config.stream_caps = *caps;
  config.targets = 1u << GSHSCENE_TARGET_STREAM;
  GSHSCENE_Sink * sink = NULL;
  if (gshscene_dump_sink_create(NULL, NULL, &config, &sink) != GSHSCENE_OK) {
    return GSHSCENE_ERR_INTERNAL;
  }
  Text text = {{0}, 0};
  GSHSCENE_Writer writer = {keep, &text};
  GSHSCENE_Target target;
  memset(&target, 0, sizeof target);
  target.kind = GSHSCENE_TARGET_STREAM;
  target.surface = &writer;
  target.unit = GSHSCENE_UNIT_POINT;
  GSHSCENE_Report * report = NULL;
  gshscene_report_create(NULL, NULL, &report);
  GSHSCENE_Render_Request request;
  memset(&request, 0, sizeof request);
  request.list = list;
  request.target = &target;
  request.report = report;
  request.flags = flags;
  GSHSCENE_Result r = gshscene_sink_render(sink, &request);
  GSHSCENE_Report_Summary sum;
  gshscene_report_summary(report, &sum);
  *notices = sum.by_class[GSHSCENE_ENTRY_NOTICE];
  *refusals = sum.by_class[GSHSCENE_ENTRY_REFUSED];
  printf("--- %s%s: %s, %zu refused, %zu noticed\n", name, flags & GSHSCENE_RENDER_STRICT ? " (strict)" : "",
      gshscene_result_string(r), *refusals, *notices);
  for (size_t i = 0; i < gshscene_report_count(report); i++) {
    GSHSCENE_Report_Entry e;
    gshscene_report_entry(report, i, &e);
    printf("  item %zu: %s, %s", e.chain[0], gshscene_entry_class_string(e.entry_class), gshscene_reason_string(e.reason));
    if (e.reason == GSHSCENE_REASON_STROKE_STYLE) {
      printf(", missing features 0x%04zx", e.second);
    }
    printf("\n");
  }
  fputs(text.bytes, stdout);
  gshscene_report_destroy(report);
  gshscene_sink_destroy(sink);
  return r;
}

int main(void) {
  GSHSCENE_Store * store = NULL;
  if (gshscene_store_create(NULL, NULL, &store) != GSHSCENE_OK) {
    return 1;
  }
  const GSHSCENE_Resource * box = square(store, 10, 10, 40);

  /* SVG's `stroke-width: 3; stroke-linejoin: round; stroke-dasharray: 6 3 2` (odd: it repeats); its default miter
   * limit 4 is read by no round join, so the style keeps the canonical 4 whatever the source said. */
  const double dashes[3] = {6.0, 3.0, 2.0};
  GSHSCENE_Stroke_Style style;
  gshscene_stroke_style_init(&style);
  style.width = 3.0;
  style.join = GSHSCENE_STROKE_JOIN_ROUND;
  style.miter_limit = 10.0;
  style.dash_count = 3;
  style.dashes = dashes;
  const GSHSCENE_Resource * dashed = NULL;
  expect(gshscene_store_intern_stroke_style(store, &style, &dashed) == GSHSCENE_OK, "the dashed style interns");
  const double doubled[6] = {6.0, 3.0, 2.0, 6.0, 3.0, 2.0};
  style.miter_limit = 4.0;
  style.dash_count = 6;
  style.dashes = doubled;
  const GSHSCENE_Resource * same = NULL;
  gshscene_store_intern_stroke_style(store, &style, &same);
  expect(same == dashed, "SK3: the doubled spelling of the dashes is the same style, one handle");
  GSHSCENE_Stroke_Style back;
  gshscene_resource_stroke_style(dashed, &back);
  printf("dashed style: %zu dashes stored, miter limit %g, hash %016llx\n", back.dash_count, back.miter_limit,
      (unsigned long long)gshscene_resource_hash(dashed));

  /* A hairline in device space, and a stroke of width 0 (SK8: it draws nothing; a notice, never a refusal). */
  GSHSCENE_Stroke_Style thin;
  gshscene_stroke_style_init(&thin);
  thin.width_space = GSHSCENE_STROKE_WIDTH_DEVICE;
  thin.join = GSHSCENE_STROKE_JOIN_ROUND;
  thin.cap = GSHSCENE_STROKE_CAP_ROUND;
  thin.degenerate = GSHSCENE_STROKE_DEGENERATE_OMIT;
  const GSHSCENE_Resource * hairline = NULL;
  gshscene_store_intern_stroke_style(store, &thin, &hairline);
  thin.width = 0.0;
  const GSHSCENE_Resource * nothing = NULL;
  gshscene_store_intern_stroke_style(store, &thin, &nothing);

  /* The element: its fill, then its stroke (SK14); then the hairline and the zero-width stroke. */
  GSHSCENE_List * list = NULL;
  gshscene_list_create(NULL, NULL, store, &list);
  GSHSCENE_Paint fill = {GSHSCENE_PAINT_SOLID, {0.2, 0.4, 0.8, 1.0}, {0, 0, 0}, NULL, 0};
  GSHSCENE_Paint ink = {GSHSCENE_PAINT_SOLID, {0.0, 0.0, 0.0, 1.0}, {0, 0, 0}, NULL, 0};
  expect(gshscene_list_fill(list, box, NULL, GSHGEOM_FILL_NONZERO, &fill, 0, 0.0) == GSHSCENE_OK, "the fill records");
  expect(gshscene_list_stroke(list, box, dashed, NULL, &ink, 0, 0.0) == GSHSCENE_OK, "the stroke records");
  expect(gshscene_list_stroke(list, box, hairline, NULL, &ink, 0, 0.0) == GSHSCENE_OK, "the hairline records");
  expect(gshscene_list_stroke(list, box, nothing, NULL, &ink, 0, 0.0) == GSHSCENE_OK, "width 0 records");
  gshscene_list_close(list);

  /* Three sinks: every feature; a writer with SVG 1.1's set that expands the rest; a toolpath that cannot expand. */
  GSHSCENE_Dump_Sink_Config defaults;
  gshscene_dump_sink_config_default(&defaults);
  GSHSCENE_Sink_Caps full = defaults.stream_caps;
  GSHSCENE_Sink_Caps writer = full;
  writer.flags |= GSHSCENE_CAP_STROKE_EXPAND;
  writer.stroke_native = GSHSCENE_STROKE_NATIVE_JOIN_MITER | GSHSCENE_STROKE_NATIVE_JOIN_ROUND |
      GSHSCENE_STROKE_NATIVE_JOIN_BEVEL | GSHSCENE_STROKE_NATIVE_CAP_BUTT | GSHSCENE_STROKE_NATIVE_CAP_ROUND |
      GSHSCENE_STROKE_NATIVE_CAP_SQUARE | GSHSCENE_STROKE_NATIVE_WIDTH_LOCAL |
      GSHSCENE_STROKE_NATIVE_WIDTH_LOCAL_ANISOTROPIC | GSHSCENE_STROKE_NATIVE_DASH;
  GSHSCENE_Sink_Caps toolpath = full;
  toolpath.stroke_native = GSHSCENE_STROKE_NATIVE_JOIN_ROUND | GSHSCENE_STROKE_NATIVE_CAP_ROUND |
      GSHSCENE_STROKE_NATIVE_CAP_BUTT | GSHSCENE_STROKE_NATIVE_WIDTH_LOCAL | GSHSCENE_STROKE_NATIVE_WIDTH_DEVICE;
  size_t notices = 0;
  size_t refusals = 0;
  expect(render(&full, list, GSHSCENE_RENDER_STRICT, "every feature as a stroke", &notices, &refusals) == GSHSCENE_OK,
      "a sink with every feature draws every stroke, strict included");
  expect(notices == 1 && refusals == 0, "the zero-width stroke is the one notice");
  expect(render(&writer, list, GSHSCENE_RENDER_STRICT, "a writer", &notices, &refusals) == GSHSCENE_OK,
      "a writer expands the hairline (a device width SVG 1.1 lacks): no refusal");
  expect(refusals == 0, "expanding is not refusing");
  expect(render(&toolpath, list, 0, "a toolpath", &notices, &refusals) == GSHSCENE_OK, "best effort completes");
  expect(refusals == 1, "the toolpath refuses the dashed stroke (dashes it lacks), naming them");
  expect(render(&toolpath, list, GSHSCENE_RENDER_STRICT, "a toolpath", &notices, &refusals) == GSHSCENE_ERR_REFUSED,
      "under strict a planned refusal fails before the sink is called");

  gshscene_list_destroy(list);
  gshscene_resource_release(box);
  gshscene_resource_release(dashed);
  gshscene_resource_release(same);
  gshscene_resource_release(hairline);
  gshscene_resource_release(nothing);
  gshscene_store_destroy(store);
  return failures ? 1 : 0;
}
