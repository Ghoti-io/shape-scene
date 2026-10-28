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
 * Images (image.h, IM1 to IM14): a decoded raster put in the store, by copy and by take; the samples kept exactly as
 * they were imported (a 12-bit sample stays below 4096, a straight and a premultiplied image of the same bytes are two
 * images); equal content found again as the same handle however the source was laid out; and what a front end must do
 * about a raster whose color description it did not state.  It checks what it shows and exits non-zero when that does
 * not hold.
 */

#include <ghoti.io/shape-scene/shape-scene.h>

#include <stdio.h>
#include <string.h>

static int failures;

static void expect(int ok, const char * what) {
  if (!ok) {
    fprintf(stderr, "image: %s\n", what);
    failures++;
  }
}

/** An RGBA8 raster of @p w by @p h whose texel (x, y) is (x, y, x + y, 255), with @p pad junk bytes after each row. */
static GIMG_Raster * gradient_raster(uint32_t w, uint32_t h, size_t pad) {
  GIMG_Raster * raster = NULL;
  size_t stride = (size_t)w * 4u + pad;
  if (gimg_raster_create(w, h, &GIMG_PIXEL_RGBA8, GIMG_RASTER_OWNED, NULL, stride, &raster) != GIMG_OK) {
    return NULL;
  }
  unsigned char * pixels = gimg_raster_pixels(raster);
  memset(pixels, 0x5A, stride * h);
  for (uint32_t y = 0; y < h; y++) {
    for (uint32_t x = 0; x < w; x++) {
      unsigned char * px = pixels + (size_t)y * stride + (size_t)x * 4u;
      px[0] = (unsigned char)x;
      px[1] = (unsigned char)y;
      px[2] = (unsigned char)(x + y);
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

  /* A copy: the store keeps its own, tightly packed, whatever the source's stride. */
  GIMG_Raster * padded = gradient_raster(100, 70, 9);
  GIMG_Raster * tight = gradient_raster(100, 70, 0);
  const GSHSCENE_Resource * a = NULL;
  const GSHSCENE_Resource * b = NULL;
  GSHSCENE_Image_Params params;
  memset(&params, 0, sizeof params); /* the target's color space, no flags */
  expect(gshscene_store_intern_image(store, padded, &params, &a) == GSHSCENE_OK, "intern a padded raster");
  expect(gshscene_store_intern_image(store, tight, &params, &b) == GSHSCENE_OK, "intern a tight raster");
  expect(a && a == b, "equal content is one handle, whatever the stride and the padding");
  GSHSCENE_Image_Info info;
  expect(gshscene_resource_image(a, &info) == GSHSCENE_OK && info.width == 100 && info.height == 70, "the descriptor reads back");
  expect(info.tiles_x == 2 && info.tiles_y == 2, "a 100 by 70 image is two tiles across and two down, the edge ones partial");
  expect(gshscene_image_stride(a) == 400, "a copy is packed to its row");
  const unsigned char * row = gshscene_image_row(a, 5);
  expect(row && row[3 * 4 + 0] == 3 && row[3 * 4 + 1] == 5 && row[3 * 4 + 2] == 8, "a texel reads as imported");
  printf("image %016llx: %u by %u, %u by %u tiles\n", (unsigned long long)gshscene_resource_hash(a), info.width, info.height,
      info.tiles_x, info.tiles_y);

  /* Nothing is converted: the same bytes called premultiplied are another image. */
  GIMG_Pixel_Format premultiplied = GIMG_PIXEL_RGBA8;
  premultiplied.alpha_form = GIMG_ALPHA_PREMULTIPLIED;
  GIMG_Raster * pm = NULL;
  gimg_raster_create(100, 70, &premultiplied, GIMG_RASTER_OWNED, NULL, 0, &pm);
  memcpy(gimg_raster_pixels(pm), gimg_raster_pixels_const(tight), 100u * 70u * 4u);
  const GSHSCENE_Resource * c = NULL;
  expect(gshscene_store_intern_image(store, pm, &params, &c) == GSHSCENE_OK, "intern the premultiplied twin");
  expect(c && c != a, "a straight image and its premultiplied twin are two images");

  /* A front end that knows its standard says so; one that does not, asks the helper, which guesses nothing. */
  GSHSCENE_Color_Space space;
  expect(gshscene_image_space_from_raster(tight, &space) == GSHSCENE_ERR_INVALID, "an untagged raster is refused by the helper");
  memset(&params, 0, sizeof params);
  params.space.primaries = GCOL_PRIMARIES_SRGB;
  params.space.transfer = GCOL_TRANSFER_SRGB;
  const GSHSCENE_Resource * srgb = NULL;
  expect(gshscene_store_intern_image(store, tight, &params, &srgb) == GSHSCENE_OK, "intern it as sRGB");
  expect(srgb && srgb != a, "the color space is part of the image");

  /* A take: the store keeps the raster and destroys it at the image's last release. */
  GIMG_Raster * big = gradient_raster(300, 200, 0);
  const GSHSCENE_Resource * taken = NULL;
  memset(&params, 0, sizeof params);
  expect(gshscene_store_intern_image_take(store, big, &params, &taken) == GSHSCENE_OK, "take an owned raster");
  expect(taken && gshscene_image_row(taken, 0) == gimg_raster_pixels_const(big), "the taken image reads the raster in place");
  GSHSCENE_Store_Stats stats;
  gshscene_store_stats(store, &stats);
  expect(stats.live == 4, "four images are live");

  /* Refusals are named and leave the caller's raster the caller's. */
  GIMG_Raster * cmyk = NULL;
  gimg_raster_create(8, 8, &GIMG_PIXEL_CMYK8, GIMG_RASTER_OWNED, NULL, 0, &cmyk);
  const GSHSCENE_Resource * none = NULL;
  expect(gshscene_store_intern_image_take(store, cmyk, &params, &none) == GSHSCENE_ERR_UNSUPPORTED, "CMYK is unsupported");
  expect(none == NULL && gimg_raster_width(cmyk) == 8, "a refused take leaves the raster alone");
  gimg_raster_destroy(cmyk);

  gshscene_resource_release(taken);
  gshscene_resource_release(srgb);
  gshscene_resource_release(c);
  gshscene_resource_release(b);
  gshscene_resource_release(a);
  gimg_raster_destroy(pm);
  gimg_raster_destroy(tight);
  gimg_raster_destroy(padded);
  gshscene_store_stats(store, &stats);
  expect(stats.live == 0 && stats.bytes == 0, "every image is gone and the bytes are returned");
  gshscene_store_destroy(store);
  return failures ? 1 : 0;
}
