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
 * A union clip: the region of several shapes, clipped to as one.
 *
 * An SVG `<clipPath>` of two children, a CSS `clip-path` of two shapes and a PDF text clip are the union of their regions.  This interns
 * the members (a path each, with its own map and fill rule) as one clip-union resource, clips a fill to it, and gives the list to the
 * test sink three ways: the default configuration (which draws any union), one that draws only unions it can show disjoint (the touching
 * pair is drawn and an overlapping pair is refused, `union-overlap`, with the pair named) and one that draws none (`clip-union`).
 * The members are given in any order and come back in the store's canonical order, one handle.
 */

#include <ghoti.io/shape-scene/shape-scene.h>

#include <stdio.h>
#include <string.h>

static int check(GSHSCENE_Result r, const char * what) {
  if (r != GSHSCENE_OK) {
    fprintf(stderr, "clip_union: %s: %s\n", what, gshscene_result_string(r));
    return 1;
  }
  return 0;
}

static const GSHSCENE_Resource * rect(GSHSCENE_Store * store, double x0, double y0, double x1, double y1) {
  GSHGEOM_Path * path = NULL;
  gshgeom_path_create(NULL, NULL, 2, &path);
  GSHGEOM_Point c[4] = {{x0, y0, 0}, {x1, y0, 0}, {x1, y1, 0}, {x0, y1, 0}};
  gshgeom_path_move_to(path, c[0]);
  for (int i = 1; i < 4; i++) {
    gshgeom_path_line_to(path, c[i]);
  }
  gshgeom_path_close(path);
  const GSHSCENE_Resource * r = NULL;
  gshscene_store_intern_path(store, path, &r);
  gshgeom_path_destroy(path);
  return r;
}

/** A list: a red fill of the whole sheet clipped to @p region. */
static GSHSCENE_List * clipped(GSHSCENE_Store * store, const GSHSCENE_Resource * region, const GSHSCENE_Resource * sheet) {
  GSHSCENE_List * list = NULL;
  gshscene_list_create(NULL, NULL, store, &list);
  GSHSCENE_Paint red = {GSHSCENE_PAINT_SOLID, {1, 0, 0, 1}, {0, 0, 0.0}, NULL, 0};
  gshscene_list_clip_push(list, region, NULL, GSHGEOM_FILL_NONZERO, 0, 0.0);
  gshscene_list_fill(list, sheet, NULL, GSHGEOM_FILL_NONZERO, &red, 0, 0.0);
  gshscene_list_clip_pop(list);
  gshscene_list_close(list);
  return list;
}

static void run(const char * what, const GSHSCENE_Dump_Sink_Config * config, GSHSCENE_List * list) {
  GSHSCENE_Sink * sink = NULL;
  GSHSCENE_Report * report = NULL;
  gshscene_dump_sink_create(NULL, NULL, config, &sink);
  gshscene_report_create(NULL, NULL, &report);
  GIMG_Raster * raster = NULL;
  gimg_raster_create(32, 32, &GIMG_PIXEL_RGBA8, GIMG_RASTER_OWNED, NULL, 0, &raster);
  GSHSCENE_Target target = {0};
  target.kind = GSHSCENE_TARGET_RASTER;
  target.surface = raster;
  GSHSCENE_Render_Request request = {0};
  request.list = list;
  request.target = &target;
  request.report = report;
  gshscene_sink_render(sink, &request);
  printf("%s: %zu entries", what, gshscene_report_count(report));
  for (size_t i = 0; i < gshscene_report_count(report); i++) {
    GSHSCENE_Report_Entry e;
    gshscene_report_entry(report, i, &e);
    printf("; %s %s", gshscene_entry_class_string(e.entry_class), gshscene_reason_string(e.reason));
    if (e.reason == GSHSCENE_REASON_UNION_OVERLAP) {
      printf(" (members %u and %u)", (unsigned)(e.second >> 16), (unsigned)(e.second & 0xffffu));
    }
  }
  printf("\n");
  gshscene_report_destroy(report);
  gshscene_sink_destroy(sink);
  gimg_raster_destroy(raster);
}

int main(void) {
  int failed = 0;
  GSHSCENE_Store * store = NULL;
  failed |= check(gshscene_store_create(NULL, NULL, &store), "store");
  const GSHSCENE_Resource * sheet = rect(store, 0, 0, 32, 32);
  const GSHSCENE_Resource * left = rect(store, 2, 2, 10.5, 14);
  const GSHSCENE_Resource * right = rect(store, 10.5, 2, 20, 14);   /* shares the edge x = 10.5 with `left` */
  const GSHSCENE_Resource * across = rect(store, 8, 6, 24, 18);     /* overlaps both */

  GSHSCENE_Clip_Member touching[2], again[2], clash[2];
  memset(touching, 0, sizeof touching);
  memset(again, 0, sizeof again);
  memset(clash, 0, sizeof clash);
  touching[0].resource = left;
  touching[1].resource = right;
  again[0].resource = right;   /* the same two, the other way round */
  again[1].resource = left;
  clash[0].resource = left;
  clash[1].resource = across;
  const GSHSCENE_Resource * u1 = NULL, * u2 = NULL, * u3 = NULL;
  failed |= check(gshscene_store_intern_clip_union(store, touching, 2, &u1), "union");
  failed |= check(gshscene_store_intern_clip_union(store, again, 2, &u2), "union, reversed");
  failed |= check(gshscene_store_intern_clip_union(store, clash, 2, &u3), "overlapping union");
  printf("any order is one handle: %s; %zu members\n", u1 == u2 ? "yes" : "NO", gshscene_resource_clip_union_count(u1));

  GSHSCENE_Dump_Sink_Config all, disjoint_only, none;
  gshscene_dump_sink_config_default(&all);
  disjoint_only = all;
  none = all;
  disjoint_only.raster_caps.flags &= ~GSHSCENE_CAP_CLIP_UNION_GENERAL;
  none.raster_caps.flags &= ~(GSHSCENE_CAP_CLIP_UNION | GSHSCENE_CAP_CLIP_UNION_GENERAL);
  GSHSCENE_List * touching_list = clipped(store, u1, sheet);
  GSHSCENE_List * clash_list = clipped(store, u3, sheet);
  run("touching, a sink that draws any union", &all, touching_list);
  run("touching, a sink that draws disjoint unions", &disjoint_only, touching_list);
  run("overlapping, a sink that draws disjoint unions", &disjoint_only, clash_list);
  run("overlapping, a sink that draws any union", &all, clash_list);
  run("touching, a sink that draws none", &none, touching_list);

  gshscene_list_destroy(touching_list);
  gshscene_list_destroy(clash_list);
  const GSHSCENE_Resource * held[] = {sheet, left, right, across, u1, u2, u3};
  for (size_t i = 0; i < sizeof held / sizeof held[0]; i++) {
    const GSHSCENE_Resource * r = held[i];
    gshscene_resource_release(r);
  }
  gshscene_store_destroy(store);
  return failed;
}
