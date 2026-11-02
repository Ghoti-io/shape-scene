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
 * Patterns (pattern.h, PT1 to PT14): a tile repeated on a lattice; the position of a copy a million cells out, one product
 * and one sum and no accumulated error; the shared edge of abutting cells, one double for both; what a pattern that draws
 * nothing is noticed as; and a sink without the pattern capability refusing a fill painted with one, and a sink with it
 * refusing the fill whose tile holds a unit it cannot draw, with the unit in the tile named by its chain.  It checks what
 * it shows and exits non-zero when that does not hold.
 */

#include <ghoti.io/shape-scene/shape-scene.h>

#include <stdio.h>
#include <string.h>

static int failures;

static void expect(int ok, const char * what) {
  if (!ok) {
    fprintf(stderr, "pattern: %s\n", what);
    failures++;
  }
}

static const GSHSCENE_Resource * square(GSHSCENE_Store * store, double x, double y, double side) {
  GSHGEOM_Path * path = NULL;
  const GSHSCENE_Resource * r = NULL;
  if (gshgeom_path_create(NULL, NULL, 2, &path) != GSHGEOM_OK) {
    return NULL;
  }
  gshgeom_path_move_to(path, (GSHGEOM_Point){x, y, 0.0});
  gshgeom_path_line_to(path, (GSHGEOM_Point){x + side, y, 0.0});
  gshgeom_path_line_to(path, (GSHGEOM_Point){x + side, y + side, 0.0});
  gshgeom_path_line_to(path, (GSHGEOM_Point){x, y + side, 0.0});
  gshgeom_path_close(path);
  if (gshscene_store_intern_path(store, path, &r) != GSHSCENE_OK) {
    r = NULL;
  }
  gshgeom_path_destroy(path);
  return r;
}

int main(void) {
  GSHSCENE_Store * store = NULL;
  if (gshscene_store_create(NULL, NULL, &store) != GSHSCENE_OK) {
    return 2;
  }
  const GSHSCENE_Resource * cell_path = square(store, 0.0, 0.0, 0.1);
  const GSHSCENE_Resource * target_path = square(store, 2.0, 2.0, 60.0);

  /* The tile: one red square the size of a cell. */
  GSHSCENE_List * tile_list = NULL;
  gshscene_list_create(NULL, NULL, store, &tile_list);
  GSHSCENE_Paint red;
  memset(&red, 0, sizeof red);
  red.rgba[0] = 1.0;
  red.rgba[3] = 1.0;
  expect(gshscene_list_fill(tile_list, cell_path, NULL, GSHGEOM_FILL_NONZERO, &red, 0, 0.0) == GSHSCENE_OK, "the tile's fill");
  gshscene_list_close(tile_list);
  const GSHSCENE_Resource * tile = NULL;
  expect(gshscene_store_intern_picture(store, tile_list, &tile) == GSHSCENE_OK, "the tile");

  /* An abutting lattice of 0.1 by 0.1 cells: the step is the size. */
  GSHSCENE_Pattern p;
  gshscene_pattern_init(&p);
  p.tile = tile;
  p.w = p.h = p.x_step = p.y_step = 0.1;
  const GSHSCENE_Resource * pattern = NULL;
  const GSHSCENE_Resource * again = NULL;
  expect(gshscene_store_intern_pattern(store, &p, &pattern) == GSHSCENE_OK, "intern the pattern");
  expect(gshscene_store_intern_pattern(store, &p, &again) == GSHSCENE_OK && again == pattern, "equal content is one handle");
  printf("pattern %016llx\n", (unsigned long long)gshscene_resource_hash(pattern));

  /* Where the copy a million cells out sits: one product and one sum, not a million additions. */
  GSHSCENE_Pattern stored;
  expect(gshscene_resource_pattern(pattern, &stored) == GSHSCENE_OK, "read it back");
  double far_cell[4], next[4];
  expect(gshscene_pattern_cell(&stored, 1000000, 0, far_cell) == GSHSCENE_OK, "a cell a million out");
  double accumulated = 0.0;
  for (int i = 0; i < 1000000; i++) {
    accumulated += 0.1;
  }
  expect(far_cell[0] == 1000000.0 * 0.1, "its left edge is the index times the step");
  expect(accumulated != far_cell[0], "adding the step a million times is not the same number");
  printf("copy 1000000 starts at %.17g (adding gives %.17g)\n", far_cell[0], accumulated);

  /* The shared edge: the right edge of a cell is the left edge of the next, the same double. */
  expect(gshscene_pattern_cell(&stored, 1000001, 0, next) == GSHSCENE_OK, "the next cell");
  expect(far_cell[2] == next[0], "abutting cells name one double for their shared edge");
  expect(far_cell[0] + 0.1 != next[0] || 1, "(a cell's left plus its width need not be the next cell's left)");
  expect(gshscene_pattern_overlaps(&stored) == 0, "abutting cells do not overlap");

  /* A fill painted with the pattern; one with a width of zero is noticed, not refused. */
  GSHSCENE_Paint paint;
  memset(&paint, 0, sizeof paint);
  paint.kind = GSHSCENE_PAINT_PATTERN;
  paint.resource = pattern;
  paint.rgba[3] = 1.0;
  GSHSCENE_List * list = NULL;
  gshscene_list_create(NULL, NULL, store, &list);
  expect(gshscene_list_fill(list, target_path, NULL, GSHGEOM_FILL_NONZERO, &paint, 0, 0.0) == GSHSCENE_OK, "the fill");
  gshscene_list_close(list);
  const GSHSCENE_Item * item = NULL;
  expect(gshscene_list_item(list, 0, &item) == GSHSCENE_OK, "the item");
  expect(gshscene_pattern_notice(item, NULL, 0, NULL) == 0, "the pattern draws");
  p.w = 0.0;
  const GSHSCENE_Resource * empty_cell = NULL;
  expect(gshscene_store_intern_pattern(store, &p, &empty_cell) == GSHSCENE_OK, "a cell of width 0 is legal");
  GSHSCENE_Paint nothing = paint;
  nothing.resource = empty_cell;
  GSHSCENE_List * nothing_list = NULL;
  gshscene_list_create(NULL, NULL, store, &nothing_list);
  gshscene_list_fill(nothing_list, target_path, NULL, GSHGEOM_FILL_NONZERO, &nothing, 0, 0.0);
  gshscene_list_close(nothing_list);
  const GSHSCENE_Item * nothing_item = NULL;
  gshscene_list_item(nothing_list, 0, &nothing_item);
  expect(gshscene_pattern_notice(nothing_item, NULL, 0, NULL) == GSHSCENE_REASON_PATTERN_DEGENERATE,
      "a cell of width 0 draws nothing");

  /* A sink without the capability refuses the fill; one with it draws it. */
  GIMG_Raster * raster = NULL;
  gimg_raster_create(64, 64, &GIMG_PIXEL_RGBA8, GIMG_RASTER_OWNED, NULL, 0, &raster);
  GSHSCENE_Target target;
  memset(&target, 0, sizeof target);
  target.kind = GSHSCENE_TARGET_RASTER;
  target.surface = raster;
  GSHSCENE_Report * report = NULL;
  gshscene_report_create(NULL, NULL, &report);
  GSHSCENE_Render_Request request;
  memset(&request, 0, sizeof request);
  request.list = list;
  request.target = &target;
  request.report = report;
  request.flags = GSHSCENE_RENDER_STRICT;
  GSHSCENE_Dump_Sink_Config config;
  gshscene_dump_sink_config_default(&config);
  GSHSCENE_Sink * drawing = NULL;
  gshscene_dump_sink_create(NULL, NULL, &config, &drawing);
  expect(gshscene_sink_render(drawing, &request) == GSHSCENE_OK, "a sink that draws patterns draws it, strict");
  config.raster_caps.flags &= ~GSHSCENE_CAP_PATTERN;
  GSHSCENE_Sink * pen = NULL;
  gshscene_dump_sink_create(NULL, NULL, &config, &pen);
  GSHSCENE_Report_Entry e;
  expect(gshscene_sink_render(pen, &request) == GSHSCENE_ERR_REFUSED, "a sink without the capability refuses, strict");
  expect(gshscene_report_count(report) == 1 && gshscene_report_entry(report, 0, &e) == GSHSCENE_OK &&
      e.reason == GSHSCENE_REASON_PATTERN && e.chain_count == 1 && e.chain[0] == 0,
      "the refusal names the pattern reason at the fill's chain");

  /* A tile that holds a unit the drawing sink refuses (a knockout group; the test sink composites none): the unit in the tile
   * is reported by its chain, and the fill painted with the pattern is refused as well. */
  GSHSCENE_List * bad_tile_list = NULL;
  gshscene_list_create(NULL, NULL, store, &bad_tile_list);
  gshscene_list_fill(bad_tile_list, cell_path, NULL, GSHGEOM_FILL_NONZERO, &red, 0, 0.0);
  GSHSCENE_Group knockout;
  memset(&knockout, 0, sizeof knockout);
  knockout.opacity = 1.0;
  knockout.knockout = 1;
  gshscene_list_group_push(bad_tile_list, &knockout, 0, 0.0);
  gshscene_list_fill(bad_tile_list, cell_path, NULL, GSHGEOM_FILL_NONZERO, &red, 0, 0.0);
  gshscene_list_group_pop(bad_tile_list);
  gshscene_list_close(bad_tile_list);
  const GSHSCENE_Resource * bad_tile = NULL;
  expect(gshscene_store_intern_picture(store, bad_tile_list, &bad_tile) == GSHSCENE_OK, "the tile with a knockout group");
  p.w = 0.1;
  p.tile = bad_tile;
  const GSHSCENE_Resource * bad_pattern = NULL;
  expect(gshscene_store_intern_pattern(store, &p, &bad_pattern) == GSHSCENE_OK, "the pattern over it");
  GSHSCENE_Paint bad_paint = paint;
  bad_paint.resource = bad_pattern;
  GSHSCENE_List * bad_list = NULL;
  gshscene_list_create(NULL, NULL, store, &bad_list);
  gshscene_list_fill(bad_list, target_path, NULL, GSHGEOM_FILL_NONZERO, &bad_paint, 0, 0.0);
  gshscene_list_close(bad_list);
  request.list = bad_list;
  expect(gshscene_sink_render(drawing, &request) == GSHSCENE_ERR_REFUSED, "a tile the sink cannot draw refuses the fill");
  size_t knockouts = 0, patterns = 0;
  for (size_t i = 0; i < gshscene_report_count(report); i++) {
    if (gshscene_report_entry(report, i, &e) != GSHSCENE_OK) {
      continue;
    }
    knockouts += e.reason == GSHSCENE_REASON_GROUP_KNOCKOUT && e.chain_count == 2 && e.chain[0] == 0 && e.chain[1] == 1;
    patterns += e.reason == GSHSCENE_REASON_PATTERN && e.chain_count == 1 && e.chain[0] == 0;
  }
  expect(knockouts == 1 && patterns == 1, "the group in the tile is reported once by its chain, and the fill is refused too");

  gshscene_report_destroy(report);
  gimg_raster_destroy(raster);
  gshscene_sink_destroy(drawing);
  gshscene_sink_destroy(pen);
  gshscene_list_destroy(bad_list);
  gshscene_list_destroy(bad_tile_list);
  gshscene_list_destroy(nothing_list);
  gshscene_list_destroy(list);
  gshscene_list_destroy(tile_list);
  gshscene_resource_release(bad_pattern);
  gshscene_resource_release(bad_tile);
  gshscene_resource_release(empty_cell);
  gshscene_resource_release(again);
  gshscene_resource_release(pattern);
  gshscene_resource_release(tile);
  gshscene_resource_release(target_path);
  gshscene_resource_release(cell_path);
  gshscene_store_destroy(store);
  return failures ? 1 : 0;
}
