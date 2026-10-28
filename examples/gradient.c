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
 * Gradients (paint.h, G1 to G13): a CSS-like linear gradient whose middle stop is `currentColor`, interned once and
 * filled twice; its stops converted the way every sink converts them (gshscene_gradient_resolve), against two
 * foregrounds; a gradient that draws nothing (its two points are one) reported as a notice that never fails a strict
 * render; and a sink without radial gradients refusing one, so it is never drawn wrong.  It checks what it shows and
 * exits non-zero when that does not hold.
 */

#include <ghoti.io/shape-scene/shape-scene.h>

#include <ghoti.io/image/raster.h>

#include <stdio.h>
#include <string.h>

static int failures;

static void expect(int ok, const char * what) {
  if (!ok) {
    fprintf(stderr, "gradient: %s\n", what);
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

/** Render @p list on a 32 by 32 raster through @p sink; print the report's entries. */
static GSHSCENE_Result render(GSHSCENE_Sink * sink, const GSHSCENE_List * list, uint32_t flags, GSHSCENE_Report * report,
    size_t * notices, size_t * refusals) {
  GIMG_Raster * raster = NULL;
  gimg_raster_create(32, 32, &GIMG_PIXEL_RGBA8, GIMG_RASTER_OWNED, NULL, 0, &raster);
  GSHSCENE_Target target;
  memset(&target, 0, sizeof target);
  target.kind = GSHSCENE_TARGET_RASTER;
  target.surface = raster;
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
  for (size_t i = 0; i < gshscene_report_count(report); i++) {
    GSHSCENE_Report_Entry e;
    gshscene_report_entry(report, i, &e);
    printf("  %s: %s at item %zu\n", gshscene_entry_class_string(e.entry_class), gshscene_reason_string(e.reason),
        e.chain_count ? e.chain[0] : 0);
  }
  gimg_raster_destroy(raster);
  return r;
}

int main(void) {
  GSHSCENE_Store * store = NULL;
  if (gshscene_store_create(NULL, NULL, &store) != GSHSCENE_OK) {
    return 1;
  }
  /* linear-gradient(to right, red, currentColor 50%, blue), interpolated in linear sRGB as CSS's `in srgb-linear`. */
  GSHSCENE_Gradient_Stop stops[3];
  memset(stops, 0, sizeof stops);
  GSHSCENE_Color_Space srgb = {GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_SRGB, 0.0};
  stops[0].color.kind = GSHSCENE_PAINT_SOLID;
  stops[0].color.rgba[0] = 1.0;
  stops[0].color.rgba[3] = 1.0;
  stops[0].color.space = srgb;
  stops[1].offset = 0.5;
  stops[1].color.kind = GSHSCENE_PAINT_FOREGROUND;
  stops[1].color.rgba[3] = 1.0;
  stops[2].offset = 1.0;
  stops[2].color.kind = GSHSCENE_PAINT_SOLID;
  stops[2].color.rgba[2] = 1.0;
  stops[2].color.rgba[3] = 1.0;
  stops[2].color.space = srgb;
  GSHSCENE_Gradient g;
  memset(&g, 0, sizeof g);
  g.kind = GSHSCENE_GRADIENT_LINEAR;
  g.p1[0] = 32.0; /* from (0, 0) to (32, 0); the isoline direction zero: perpendicular */
  gshgeom_map_identity(2, &g.map);
  g.space.primaries = GCOL_PRIMARIES_SRGB;
  g.space.transfer = GCOL_TRANSFER_LINEAR;
  g.stop_count = 3;
  g.stops = stops;
  const GSHSCENE_Resource * gradient = NULL;
  const GSHSCENE_Resource * again = NULL;
  expect(gshscene_store_intern_gradient(store, &g, &gradient) == GSHSCENE_OK, "intern the gradient");
  expect(gshscene_store_intern_gradient(store, &g, &again) == GSHSCENE_OK && again == gradient,
      "equal content is one handle");
  gshscene_resource_release(again);
  printf("gradient %016llx, id %llu\n", (unsigned long long)gshscene_resource_hash(gradient),
      (unsigned long long)gshscene_resource_id(gradient));

  /* The stops as every sink converts them: red and blue are decoded into linear sRGB, the foreground resolved first. */
  GSHSCENE_Color_Space target = srgb;
  GSHSCENE_Resolved_Stop out[3];
  GSHSCENE_Color white = {{1.0, 1.0, 1.0, 1.0}, {GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_SRGB, 0.0}};
  GSHSCENE_Color grey = {{0.5, 0.5, 0.5, 1.0}, {GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_SRGB, 0.0}};
  expect(gshscene_gradient_resolve(gradient, &white, NULL, &target, NULL, out, 3) == GSHSCENE_OK, "resolve (white)");
  printf("currentColor white: %.6f %.6f %.6f\n", out[1].rgba[0], out[1].rgba[1], out[1].rgba[2]);
  expect(out[1].rgba[0] == 1.0, "white is 1 in linear light too");
  expect(gshscene_gradient_resolve(gradient, &grey, NULL, &target, NULL, out, 3) == GSHSCENE_OK, "resolve (grey)");
  printf("currentColor grey:  %.6f %.6f %.6f\n", out[1].rgba[0], out[1].rgba[1], out[1].rgba[2]);
  expect(out[1].rgba[0] > 0.21 && out[1].rgba[0] < 0.22, "sRGB 0.5 is about 0.214 in linear light");

  /* A list: the gradient twice, then a gradient whose two points are one (degenerate: it draws nothing). */
  GSHSCENE_Gradient flat = g;
  flat.p1[0] = 0.0;
  const GSHSCENE_Resource * point = NULL;
  expect(gshscene_store_intern_gradient(store, &flat, &point) == GSHSCENE_OK, "a degenerate gradient is legal");
  const GSHSCENE_Resource * a = square(store, 0, 0, 16);
  const GSHSCENE_Resource * b = square(store, 16, 16, 16);
  GSHSCENE_List * list = NULL;
  gshscene_list_create(NULL, NULL, store, &list);
  gshscene_list_set_foreground(list, &grey);
  GSHSCENE_Paint paint;
  memset(&paint, 0, sizeof paint);
  paint.kind = GSHSCENE_PAINT_GRADIENT;
  paint.resource = gradient;
  paint.rgba[3] = 1.0;
  expect(gshscene_list_fill(list, a, NULL, GSHGEOM_FILL_NONZERO, &paint, 0, 0.0) == GSHSCENE_OK, "fill");
  paint.rgba[3] = 0.5;
  expect(gshscene_list_fill(list, b, NULL, GSHGEOM_FILL_NONZERO, &paint, 0, 0.0) == GSHSCENE_OK, "fill at half alpha");
  paint.resource = point;
  expect(gshscene_list_fill(list, a, NULL, GSHGEOM_FILL_NONZERO, &paint, 0, 0.0) == GSHSCENE_OK, "fill, degenerate");
  gshscene_list_close(list);

  GSHSCENE_Sink * sink = NULL;
  gshscene_dump_sink_create(NULL, NULL, NULL, &sink);
  GSHSCENE_Report * report = NULL;
  gshscene_report_create(NULL, NULL, &report);
  size_t notices = 0;
  size_t refusals = 0;
  printf("strict render:\n");
  expect(render(sink, list, GSHSCENE_RENDER_STRICT, report, &notices, &refusals) == GSHSCENE_OK,
      "a notice never fails a strict render");
  expect(notices == 1 && refusals == 0, "one notice: the degenerate fill");

  /* A sink without radial gradients refuses a radial fill in the planning pass, before it can draw it wrong. */
  GSHSCENE_Gradient radial = g;
  memset(radial.p1, 0, sizeof radial.p1);
  radial.kind = GSHSCENE_GRADIENT_RADIAL;
  radial.c0[0] = radial.c1[0] = 16.0;
  radial.c0[1] = radial.c1[1] = 16.0;
  radial.r1 = 16.0;
  const GSHSCENE_Resource * disc = NULL;
  expect(gshscene_store_intern_gradient(store, &radial, &disc) == GSHSCENE_OK, "intern a radial gradient");
  GSHSCENE_List * round = NULL;
  gshscene_list_create(NULL, NULL, store, &round);
  paint.resource = disc;
  paint.rgba[3] = 1.0;
  gshscene_list_fill(round, a, NULL, GSHGEOM_FILL_NONZERO, &paint, 0, 0.0);
  gshscene_list_close(round);
  GSHSCENE_Dump_Sink_Config config;
  gshscene_dump_sink_config_default(&config);
  config.raster_caps.gradient_kinds &= ~(UINT32_C(1) << GSHSCENE_GRADIENT_RADIAL);
  GSHSCENE_Sink * linear_only = NULL;
  gshscene_dump_sink_create(NULL, NULL, &config, &linear_only);
  printf("a sink without radial gradients, strict:\n");
  expect(render(linear_only, round, GSHSCENE_RENDER_STRICT, report, &notices, &refusals) == GSHSCENE_ERR_REFUSED,
      "strict refuses before drawing");
  expect(refusals == 1, "the radial fill is refused, reason gradient");

  gshscene_sink_destroy(linear_only);
  gshscene_sink_destroy(sink);
  gshscene_report_destroy(report);
  gshscene_list_destroy(round);
  gshscene_list_destroy(list);
  gshscene_resource_release(disc);
  gshscene_resource_release(a);
  gshscene_resource_release(b);
  gshscene_resource_release(point);
  gshscene_resource_release(gradient);
  gshscene_store_destroy(store);
  return failures ? 1 : 0;
}
