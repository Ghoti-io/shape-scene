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
 * Image paints (image_paint.h, IP1 to IP18): a small image painted onto a rectangle; the window stored explicitly so one
 * drawing has one spelling; the fill a front end makes for a drawn image, recognized exactly as a writer does; the filter
 * an item's map calls for; and a sink without CUBIC or REFLECT refusing a fill that needs them before it draws one wrong.
 * It checks what it shows and exits non-zero when that does not hold.
 */

#include <ghoti.io/shape-scene/shape-scene.h>

#include <ghoti.io/image/raster.h>

#include <stdio.h>
#include <string.h>

static int failures;

static void expect(int ok, const char * what) {
  if (!ok) {
    fprintf(stderr, "image_paint: %s\n", what);
    failures++;
  }
}

static GIMG_Raster * checker(uint32_t w, uint32_t h) {
  GIMG_Raster * raster = NULL;
  if (gimg_raster_create(w, h, &GIMG_PIXEL_RGBA8, GIMG_RASTER_OWNED, NULL, 0, &raster) != GIMG_OK) {
    return NULL;
  }
  unsigned char * pixels = gimg_raster_pixels(raster);
  for (uint32_t y = 0; y < h; y++) {
    for (uint32_t x = 0; x < w; x++) {
      unsigned char v = (unsigned char)(((x ^ y) & 1u) ? 255 : 0);
      unsigned char * px = pixels + ((size_t)y * w + x) * 4u;
      px[0] = v;
      px[1] = v;
      px[2] = v;
      px[3] = 255;
    }
  }
  return raster;
}

int main(void) {
  GSHSCENE_Store * store = NULL;
  if (gshscene_store_create(NULL, NULL, &store) != GSHSCENE_OK) {
    return 2;
  }
  GIMG_Raster * raster = checker(8, 8);
  GSHSCENE_Image_Params params;
  memset(&params, 0, sizeof params);
  params.space.primaries = GCOL_PRIMARIES_SRGB;
  params.space.transfer = GCOL_TRANSFER_SRGB;
  const GSHSCENE_Resource * image = NULL;
  expect(gshscene_store_intern_image(store, raster, &params, &image) == GSHSCENE_OK, "intern the image");

  /* The 8 by 8 image drawn into a 64 by 64 square: nearest texels, magnified 8 times. */
  GSHSCENE_Image_Paint ip;
  gshscene_image_paint_init(&ip);
  ip.image = image;
  gshgeom_map_scale(2, 8.0, 8.0, 1.0, &ip.map);
  ip.magnify = GSHSCENE_IMAGE_FILTER_NEAREST;
  const GSHSCENE_Resource * paint = NULL;
  const GSHSCENE_Resource * again = NULL;
  expect(gshscene_store_intern_image_paint(store, &ip, &paint) == GSHSCENE_OK, "intern the image paint");
  ip.window[0] = ip.window[1] = 0;
  ip.window[2] = ip.window[3] = 8;
  expect(gshscene_store_intern_image_paint(store, &ip, &again) == GSHSCENE_OK && again == paint,
      "the whole image and its explicit window are one paint");
  GSHSCENE_Image_Paint back;
  expect(gshscene_resource_image_paint(paint, &back) == GSHSCENE_OK && back.window[2] == 8 && back.window[3] == 8,
      "the store keeps the explicit window");
  printf("image paint %016llx\n", (unsigned long long)gshscene_resource_hash(paint));

  /* The fill a front end makes for a drawn image, and what a writer sees in it. */
  const GSHSCENE_Resource * rect = NULL;
  expect(gshscene_image_paint_rect_path(store, paint, &rect) == GSHSCENE_OK, "the destination rectangle");
  GSHSCENE_List * list = NULL;
  gshscene_list_create(NULL, NULL, store, &list);
  GSHSCENE_Paint fill;
  memset(&fill, 0, sizeof fill);
  fill.kind = GSHSCENE_PAINT_IMAGE;
  fill.resource = paint;
  fill.rgba[3] = 1.0;
  expect(gshscene_list_fill(list, rect, NULL, GSHGEOM_FILL_NONZERO, &fill, 0, 0.0) == GSHSCENE_OK, "fill");
  gshscene_list_close(list);
  const GSHSCENE_Item * item = NULL;
  expect(gshscene_list_item(list, 0, &item) == GSHSCENE_OK, "the item");
  double quad[8];
  expect(gshscene_image_fill_is_draw(item, quad) == 1, "the fill is a drawn image");
  expect(quad[0] == 0.0 && quad[2] == 64.0 && quad[5] == 64.0, "its corners are the window under the map");
  uint32_t filter = 99;
  expect(gshscene_image_filter_of(item, NULL, 0, NULL, &filter) == GSHSCENE_OK && filter == GSHSCENE_IMAGE_FILTER_NEAREST,
      "scaled up 8 times the magnify filter applies");
  printf("required: %08x\n", (unsigned)gshscene_image_required(item));

  /* A sink that cannot draw the image says so before it draws anything. */
  GSHSCENE_Dump_Sink_Config config;
  gshscene_dump_sink_config_default(&config);
  config.raster_caps.image_extends = 0;
  GSHSCENE_Sink * sink = NULL;
  gshscene_dump_sink_create(NULL, NULL, &config, &sink);
  GIMG_Raster * target_raster = NULL;
  gimg_raster_create(64, 64, &GIMG_PIXEL_RGBA8, GIMG_RASTER_OWNED, NULL, 0, &target_raster);
  GSHSCENE_Target target;
  memset(&target, 0, sizeof target);
  target.kind = GSHSCENE_TARGET_RASTER;
  target.surface = target_raster;
  GSHSCENE_Report * report = NULL;
  gshscene_report_create(NULL, NULL, &report);
  GSHSCENE_Render_Request request;
  memset(&request, 0, sizeof request);
  request.list = list;
  request.target = &target;
  request.report = report;
  request.flags = GSHSCENE_RENDER_STRICT;
  expect(gshscene_sink_render(sink, &request) == GSHSCENE_ERR_REFUSED, "a sink with no edge mode refuses, strict");
  GSHSCENE_Report_Entry e;
  expect(gshscene_report_count(report) == 1 && gshscene_report_entry(report, 0, &e) == GSHSCENE_OK &&
      e.reason == GSHSCENE_REASON_IMAGE && (e.second & (1u << GSHSCENE_IMAGE_EXTEND_PAD)),
      "the refusal names the image reason and the missing PAD");

  gshscene_report_destroy(report);
  gimg_raster_destroy(target_raster);
  gshscene_sink_destroy(sink);
  gshscene_list_destroy(list);
  gshscene_resource_release(rect);
  gshscene_resource_release(again);
  gshscene_resource_release(paint);
  gshscene_resource_release(image);
  gimg_raster_destroy(raster);
  gshscene_store_destroy(store);
  return failures ? 1 : 0;
}
