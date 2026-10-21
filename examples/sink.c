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
 * A render through the sink contract (sink.h), with the test sink.
 *
 * It records a list with a clip, a group and a fill of glyphs named by a font,
 * then renders it three ways through one `GSHSCENE_Sink *`: the zeroed request in
 * one call onto a raster (the test sink stamps a marker per drawn unit), the same
 * on a stream in millimetres (a writer's target: the dump goes to stdout), and
 * a sink that cannot draw glyphs from a font, best effort and then strict, to
 * show the report and the untouched target.  Then a frame loop with caller
 * memory, which allocates nothing after the first frame.
 */

#include <ghoti.io/shape-scene/shape-scene.h>

#include <ghoti.io/image/raster.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

static int check(GSHSCENE_Result r, const char * what) {
  if (r != GSHSCENE_OK) {
    fprintf(stderr, "sink: %s: %s\n", what, gshscene_result_string(r));
    return 1;
  }
  return 0;
}

static GSHSCENE_Result to_stdout(void * ctx, const void * bytes, size_t count) {
  (void)ctx;
  return fwrite(bytes, 1, count, stdout) == count ? GSHSCENE_OK : GSHSCENE_ERR_IO;
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

static void print_report(const char * title, const GSHSCENE_Report * report) {
  printf("-- %s\n", title);
  gshscene_report_dump(report, stdout);
  for (size_t i = 0; i < gshscene_report_count(report); i++) {
    GSHSCENE_Report_Entry e;
    gshscene_report_entry(report, i, &e);
    printf("   entry %zu: %s, %s, item %zu\n", i, gshscene_entry_class_string(e.entry_class),
        gshscene_reason_string(e.reason), e.chain_count ? e.chain[0] : (size_t)0);
  }
}

int main(void) {
  int failed = 0;
  GSHSCENE_Store * store = NULL;
  failed |= check(gshscene_store_create(NULL, NULL, &store), "store");
  const GSHSCENE_Resource * a = square(store, 4, 4, 24);
  const GSHSCENE_Resource * b = square(store, 8, 8, 40);
  /* A face bound to no glyph path provider: a sink that draws runs from a font refuses this one (face-unbound). */
  GSHSCENE_Face_Desc face_desc = {0x1234, 0, 0, NULL, NULL, NULL};
  const GSHSCENE_Resource * face = NULL;
  failed |= check(gshscene_store_intern_face(store, &face_desc, &face), "face");
  GSHSCENE_Glyph glyphs[2] = {{36, 10, 50, NULL}, {37, 18, 50, NULL}};
  GSHSCENE_Glyph_Run run = {GSHSCENE_GLYPHS_FROM_FONT, face, 8.0, 0, 0, 2, glyphs};
  const GSHSCENE_Resource * text = NULL;
  failed |= check(gshscene_store_intern_glyph_run(store, &run, &text), "glyph run");
  gshscene_resource_release(face);

  GSHSCENE_Paint red = {GSHSCENE_PAINT_SOLID, {1, 0, 0, 1}, {0, 0, 0.0}, NULL, 0};
  GSHSCENE_Group half = {.opacity = 0.5, .ordering = GSHSCENE_ORDER_FLAT, .composite = GSHSCENE_COMPOSITE_SRC_OVER};
  GSHSCENE_List * list = NULL;
  failed |= check(gshscene_list_create(NULL, NULL, store, &list), "list");
  failed |= check(gshscene_list_fill(list, a, NULL, GSHGEOM_FILL_NONZERO, &red, 0, 0.0), "fill");
  failed |= check(gshscene_list_clip_push(list, b, NULL, GSHGEOM_FILL_NONZERO, 0, 0.0), "clip");
  failed |= check(gshscene_list_group_push(list, &half, 0, 0.0), "group");
  failed |= check(gshscene_list_fill(list, b, NULL, GSHGEOM_FILL_NONZERO, &red, 0, 0.0), "fill");
  failed |= check(gshscene_list_group_pop(list), "pop");
  failed |= check(gshscene_list_clip_pop(list), "pop");
  failed |= check(gshscene_list_fill(list, text, NULL, GSHGEOM_FILL_NONZERO, NULL, 0, 0.0), "text");
  failed |= check(gshscene_list_close(list), "close");

  GSHSCENE_Sink * sink = NULL;
  failed |= check(gshscene_dump_sink_create(NULL, NULL, NULL, &sink), "test sink");
  GSHSCENE_Report * report = NULL;
  failed |= check(gshscene_report_create(NULL, NULL, &report), "report");

  /* 1. The zeroed request, in one call, onto a raster. */
  GIMG_Raster * raster = NULL;
  if (gimg_raster_create(64, 64, &GIMG_PIXEL_RGBA8, GIMG_RASTER_OWNED, NULL, 0, &raster) != GIMG_OK) {
    return 1;
  }
  GSHSCENE_Target target = {0};
  target.kind = GSHSCENE_TARGET_RASTER;
  target.surface = raster;
  GSHSCENE_Render_Request request = {0};
  request.list = list;
  request.target = &target;
  request.report = report;
  failed |= check(gshscene_sink_render(sink, &request), "render");
  print_report("raster, best effort", report);

  /* 2. A writer's target: a document in millimetres, the map from list space to it a scale. */
  GSHSCENE_Writer out = {to_stdout, NULL};
  GSHSCENE_Target page = {0};
  page.kind = GSHSCENE_TARGET_STREAM;
  page.surface = &out;
  page.unit = GSHSCENE_UNIT_MILLIMETER;
  gshgeom_map_scale(2, 0.5, 0.5, 1.0, &page.map);
  request.target = &page;
  printf("-- stream, millimetres\n");
  failed |= check(gshscene_sink_render(sink, &request), "render to a stream");

  /* 3. A sink with no glyph path provider: best effort skips the glyphs, strict refuses before drawing. */
  GSHSCENE_Dump_Sink_Config config;
  gshscene_dump_sink_config_default(&config);
  config.raster_caps.flags &= ~GSHSCENE_CAP_GLYPHS_FROM_FONT;
  config.stream_caps.flags &= ~GSHSCENE_CAP_GLYPHS_FROM_FONT;
  GSHSCENE_Sink * narrow = NULL;
  failed |= check(gshscene_dump_sink_create(NULL, NULL, &config, &narrow), "narrow sink");
  request.target = &target;
  failed |= check(gshscene_sink_render(narrow, &request), "best effort");
  print_report("no glyphs from a font, best effort", report);
  request.flags = GSHSCENE_RENDER_STRICT;
  GSHSCENE_Result strict = gshscene_sink_render(narrow, &request);
  printf("strict: %s\n", gshscene_result_string(strict));
  failed |= strict != GSHSCENE_ERR_REFUSED;
  print_report("no glyphs from a font, strict", report);

  /* 4. A frame loop with caller memory: the exact workspace, asked once, and nothing allocated per frame. */
  request.flags = GSHSCENE_RENDER_CALLER_MEMORY;
  size_t need = 0;
  failed |= check(gshscene_sink_workspace_bytes(sink, &request, &need), "workspace");
  request.workspace = malloc(need);
  request.workspace_bytes = need;
  for (int frame = 0; frame < 3 && request.workspace; frame++) {
    failed |= check(gshscene_sink_render(sink, &request), "frame");
  }
  printf("-- frames with a workspace of %zu bytes\n", need);
  free(request.workspace);

  failed |= check(gshscene_sink_destroy(narrow), "destroy");
  failed |= check(gshscene_sink_destroy(sink), "destroy");
  gshscene_report_destroy(report);
  gimg_raster_destroy(raster);
  gshscene_list_destroy(list);
  gshscene_resource_release(text);
  gshscene_resource_release(b);
  gshscene_resource_release(a);
  gshscene_store_destroy(store);
  return failed;
}
