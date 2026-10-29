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
 * Masks (mask.h, MK1 to MK14): the two masks a front end makes most, and what the waist does with them.
 *
 * An SVG-style mask: the luminance of a picture (a white square on black) with CSS Masking's coefficients and a black
 * backdrop, so the mask is the picture's luminance times its alpha and, outside the picture, 0.  A PDF-style soft mask:
 * a luminosity mask in sRGB with the `LUM` weights, a white backdrop (PDF's `BC`) and a stitching transfer function,
 * acting on the shape (alpha-is-shape).  The example interns both (equal content is one handle), reads the numbers every
 * sink shares (the resolved coefficients, the transfer, the value outside everything the picture draws), puts each on a
 * group and renders it with a sink that has every feature, one without the transfer and one without soft masks, to show
 * the reasons of the report.  It checks what it shows and exits non-zero when that does not hold.
 */

#include <ghoti.io/shape-scene/shape-scene.h>

#include <ghoti.io/image/raster.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;

static void expect(int ok, const char * what) {
  if (!ok) {
    fprintf(stderr, "mask: %s\n", what);
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

/* A picture of one opaque square of a gray level. */
static const GSHSCENE_Resource * picture(GSHSCENE_Store * store, const GSHSCENE_Resource * shape, double gray) {
  GSHSCENE_List * list = NULL;
  gshscene_list_create(NULL, NULL, store, &list);
  GSHSCENE_Paint paint = {GSHSCENE_PAINT_SOLID, {gray, gray, gray, 1.0}, {0}, NULL, 0};
  gshscene_list_fill(list, shape, NULL, GSHGEOM_FILL_NONZERO, &paint, 0, 0.0);
  gshscene_list_close(list);
  const GSHSCENE_Resource * pic = NULL;
  expect(gshscene_store_intern_picture(store, list, &pic) == GSHSCENE_OK, "intern the picture");
  gshscene_list_destroy(list);
  return pic;
}

/* A render of a list holding one masked group, strict, onto a raster; returns the result and leaves the report. */
static GSHSCENE_Result render(GSHSCENE_Sink * sink, GSHSCENE_List * list, GSHSCENE_Report * report) {
  GIMG_Raster * raster = NULL;
  gimg_raster_create(64, 64, &GIMG_PIXEL_RGBA8, GIMG_RASTER_OWNED, NULL, 0, &raster);
  GSHSCENE_Target target;
  memset(&target, 0, sizeof target);
  target.kind = GSHSCENE_TARGET_RASTER;
  target.surface = raster;
  GSHSCENE_Render_Request request;
  memset(&request, 0, sizeof request);
  request.list = list;
  request.target = &target;
  request.report = report;
  request.flags = GSHSCENE_RENDER_STRICT;
  GSHSCENE_Result result = gshscene_sink_render(sink, &request);
  gimg_raster_destroy(raster);
  return result;
}

int main(void) {
  GSHSCENE_Store * store = NULL;
  if (gshscene_store_create(NULL, NULL, &store) != GSHSCENE_OK) {
    return 2;
  }
  const GSHSCENE_Resource * region = square(store, 8, 8, 32);
  const GSHSCENE_Resource * content = square(store, 0, 0, 64);
  const GSHSCENE_Resource * white = picture(store, region, 1.0);

  /* The SVG-style mask: luminance, CSS Masking's coefficients, a black backdrop, no transfer, acting on the opacity. */
  GSHSCENE_Mask svg;
  memset(&svg, 0, sizeof svg);
  svg.picture = white;
  svg.mode = GSHSCENE_MASK_LUMINANCE;
  svg.coefficients = GSHSCENE_MASK_COEFF_W3C;
  const GSHSCENE_Resource * svg_mask = NULL;
  const GSHSCENE_Resource * svg_again = NULL;
  expect(gshscene_store_intern_mask(store, &svg, &svg_mask) == GSHSCENE_OK, "intern the SVG-style mask");
  expect(gshscene_store_intern_mask(store, &svg, &svg_again) == GSHSCENE_OK && svg_again == svg_mask,
      "equal content is one handle");
  GSHSCENE_Mask_Resolved resolved;
  GSHSCENE_Color_Space srgb = {GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_SRGB, 0.0};
  expect(gshscene_mask_resolve(svg_mask, &srgb, &resolved) == GSHSCENE_OK, "resolve");
  expect(fabs(resolved.k[0] + resolved.k[1] + resolved.k[2] - 1.0) < 1e-12, "a coefficient set sums to one (MK3)");
  expect(gshscene_mask_transfer(svg_mask, 0.25) == 0.25, "the identity transfer");
  printf("svg mask %016llx: k = %.4f %.4f %.4f, requires %03x\n", (unsigned long long)gshscene_resource_hash(svg_mask),
      resolved.k[0], resolved.k[1], resolved.k[2], (unsigned)gshscene_mask_required(svg_mask));

  /* The PDF-style soft mask: luminosity, LUM, a white backdrop, a two-piece stitching transfer, alpha-is-shape. */
  const GSHSCENE_Resource * gray = picture(store, region, 0.5);
  GSHSCENE_Transfer_Piece pieces[2];
  memset(pieces, 0, sizeof pieces);
  pieces[0].lo = 0.0;
  pieces[0].hi = 0.5;
  pieces[0].e0 = 0.0;
  pieces[0].e1 = 1.0;
  pieces[0].leaf_kind = GSHSCENE_TRANSFER_IDENTITY; /* the first half of the input is stretched over the whole range */
  pieces[1].lo = 0.5;
  pieces[1].hi = 1.0;
  pieces[1].e0 = 0.0;
  pieces[1].e1 = 1.0;
  pieces[1].leaf_kind = GSHSCENE_TRANSFER_EXPONENTIAL;
  pieces[1].c0 = 1.0;
  pieces[1].c1 = 0.0; /* ... and the second half runs from 1 back to 0 */
  pieces[1].n = 1.0;
  GSHSCENE_Mask pdf;
  memset(&pdf, 0, sizeof pdf);
  pdf.picture = gray;
  pdf.mode = GSHSCENE_MASK_LUMINANCE;
  pdf.coefficients = GSHSCENE_MASK_COEFF_LUM;
  pdf.render_space = srgb;
  pdf.backdrop[0] = pdf.backdrop[1] = pdf.backdrop[2] = 1.0;
  pdf.transfer.kind = GSHSCENE_TRANSFER_STITCHING;
  pdf.transfer.piece_count = 2;
  pdf.transfer.pieces = pieces;
  pdf.acts_on = GSHSCENE_MASK_ACTS_ON_SHAPE;
  const GSHSCENE_Resource * pdf_mask = NULL;
  expect(gshscene_store_intern_mask(store, &pdf, &pdf_mask) == GSHSCENE_OK, "intern the PDF-style mask");
  expect(gshscene_mask_transfer(pdf_mask, 0.25) == 0.5, "the stitching's first piece");
  expect(gshscene_mask_transfer(pdf_mask, 0.75) == 0.5, "the stitching's second piece");
  expect(gshscene_mask_transfer(pdf_mask, 0.5) == 1.0, "at the bound the upper piece takes the input (MK4)");
  /* MK5: outside everything the picture draws the value is the transfer of the backdrop's value, here a white backdrop
   * whose luminance is 1, which the second piece maps to 0; the mask has no extent of its own. */
  expect(gshscene_mask_transfer(pdf_mask, 1.0) == 0.0, "the outside value of a white backdrop is the transfer of 1");
  printf("pdf mask %016llx requires %03x\n", (unsigned long long)gshscene_resource_hash(pdf_mask),
      (unsigned)gshscene_mask_required(pdf_mask));

  /* A group names a mask and says where it sits (MK6); the mask is one handle however many groups use it. */
  GSHSCENE_Paint blue = {GSHSCENE_PAINT_SOLID, {0, 0, 1, 1}, {0}, NULL, 0};
  GSHSCENE_List * list = NULL;
  gshscene_list_create(NULL, NULL, store, &list);
  GSHSCENE_Group masked = {.opacity = 1.0, .ordering = GSHSCENE_ORDER_FLAT, .composite = GSHSCENE_COMPOSITE_SRC_OVER};
  masked.soft_mask = pdf_mask;
  gshgeom_map_translate(2, 4.0, 4.0, 0.0, &masked.soft_mask_map);
  expect(gshscene_list_group_push(list, &masked, 0, 0.0) == GSHSCENE_OK, "a group with a mask and its placement");
  gshscene_list_fill(list, content, NULL, GSHGEOM_FILL_NONZERO, &blue, 0, 0.0);
  gshscene_list_group_pop(list);
  gshscene_list_close(list);

  GSHSCENE_Report * report = NULL;
  gshscene_report_create(NULL, NULL, &report);
  GSHSCENE_Dump_Sink_Config config;
  GSHSCENE_Sink * sink = NULL;

  /* A sink with every feature composites the group. */
  gshscene_dump_sink_config_default(&config);
  expect(gshscene_dump_sink_create(NULL, NULL, &config, &sink) == GSHSCENE_OK, "the full sink");
  expect(render(sink, list, report) == GSHSCENE_OK && gshscene_report_count(report) == 0, "a sink with every feature draws it");
  gshscene_sink_destroy(sink);

  /* A writer without the stitching transfer: refused, and the entry names the missing bit, before the target is touched. */
  gshscene_dump_sink_config_default(&config);
  config.raster_caps.mask_native &= ~GSHSCENE_MASK_NATIVE_TRANSFER_STITCHING;
  expect(gshscene_dump_sink_create(NULL, NULL, &config, &sink) == GSHSCENE_OK, "the sink without the transfer");
  expect(render(sink, list, report) == GSHSCENE_ERR_REFUSED, "strict refuses a mask whose transfer the sink lacks");
  GSHSCENE_Report_Entry e;
  expect(gshscene_report_count(report) == 1 && gshscene_report_entry(report, 0, &e) == GSHSCENE_OK &&
      e.reason == GSHSCENE_REASON_MASK_FEATURE && (e.second & GSHSCENE_MASK_NATIVE_TRANSFER_STITCHING),
      "the entry is MASK_FEATURE and names the stitching");
  gshscene_sink_destroy(sink);

  /* A sink with no soft masks at all: the earlier reason. */
  gshscene_dump_sink_config_default(&config);
  config.raster_caps.flags &= ~GSHSCENE_CAP_GROUP_SOFT_MASK;
  expect(gshscene_dump_sink_create(NULL, NULL, &config, &sink) == GSHSCENE_OK, "the sink without soft masks");
  expect(render(sink, list, report) == GSHSCENE_ERR_REFUSED, "strict refuses without the flag");
  expect(gshscene_report_count(report) == 1 && gshscene_report_entry(report, 0, &e) == GSHSCENE_OK &&
      e.reason == GSHSCENE_REASON_SOFT_MASK, "the entry is SOFT_MASK, which is decided before MASK_FEATURE");
  gshscene_sink_destroy(sink);

  expect(svg_mask != pdf_mask, "different content is a different handle");

  gshscene_report_destroy(report);
  gshscene_list_destroy(list);
  gshscene_resource_release(pdf_mask);
  gshscene_resource_release(svg_again);
  gshscene_resource_release(svg_mask);
  gshscene_resource_release(gray);
  gshscene_resource_release(white);
  gshscene_resource_release(content);
  gshscene_resource_release(region);
  gshscene_store_destroy(store);
  return failures ? 1 : 0;
}
