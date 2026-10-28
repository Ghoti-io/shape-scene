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
 * The image paint (image_paint.h, IP1 to IP18): the resource, its canonical form, hash and dump, the decode of a texel
 * (IP4) that every sink shares, and the writers' recognition of a drawn image (IP14).
 */

#include <ghoti.io/shape-scene/macros.h>

#include "../internal.h"

#include <string.h>

/** The format of an image paint's hash and dump (resource.h). */
#define IMAGE_PAINT_FORMAT_VERSION 1u

#define HEX(x) ((unsigned long long)(x))

void gshscene_image_paint_init(GSHSCENE_Image_Paint * paint) {
  if (!paint) {
    return;
  }
  memset(paint, 0, sizeof *paint);
  gshgeom_map_identity(2, &paint->map);
  paint->extend_x = GSHSCENE_IMAGE_EXTEND_PAD;
  paint->extend_y = GSHSCENE_IMAGE_EXTEND_PAD;
  paint->magnify = GSHSCENE_IMAGE_FILTER_LINEAR;
  paint->minify = GSHSCENE_IMAGE_FILTER_LINEAR;
}

/* --- the canonical form ------------------------------------------------------------------ */

/** A map entry within G14's band: zero, or a magnitude in `[2^-64, 2^64]` (IP1). */
static int entry_in_band(double x) {
  double m = x < 0.0 ? -x : x;
  return m == 0.0 || (m >= 0x1p-64 && m <= 0x1p64);
}

/** Whether every entry of the 2D map is within the band. */
static int map_in_band(const GSHGEOM_Map * map) {
  for (int i = 0; i < 2; i++) {
    for (int j = 0; j < 2; j++) {
      if (!entry_in_band(map->m[i][j])) {
        return 0;
      }
    }
    if (!entry_in_band(map->t[i])) {
      return 0;
    }
  }
  return 1;
}

/** `-0` as `+0`: one spelling for one number. */
static double plus_zero(double x) {
  return x == 0.0 ? 0.0 : x;
}

/**
 * IP1: the content of @p in as the store keeps it, or the result of its refusal.  Every validity check comes before the bound
 * checks, which are ::GSHSCENE_ERR_LIMIT.  @p tint receives the tint's canonical paint when there is one.
 */
static GSHSCENE_Result canonical(const GSHSCENE_Store * store, const GSHSCENE_Image_Paint * in, GSHSCENE_Image_Paint * out,
    GSHSCENE_Paint * tint) {
  const GSHSCENE_Resource * image = in->image;
  if (!image || image->kind != GSHSCENE_RESOURCE_IMAGE || image->store != store) {
    return GSHSCENE_ERR_INVALID;
  }
  const GSHSCENE_Image_Info * info = &image->u.image.info;
  uint32_t window[4] = {in->window[0], in->window[1], in->window[2], in->window[3]};
  if (window[0] == 0 && window[1] == 0 && window[2] == 0 && window[3] == 0) {
    window[2] = info->width;
    window[3] = info->height;
  } else if (!(window[0] < window[2] && window[2] <= info->width && window[1] < window[3] && window[3] <= info->height)) {
    return GSHSCENE_ERR_INVALID;
  }
  GSHGEOM_Map map;
  if (in->map.dim != 2 || gshscene_list_canonical_map(&in->map, &map) != GSHSCENE_OK) {
    return GSHSCENE_ERR_INVALID;
  }
  if (in->extend_x >= (uint32_t)GSHSCENE_IMAGE_EXTEND_COUNT || in->extend_y >= (uint32_t)GSHSCENE_IMAGE_EXTEND_COUNT ||
      in->magnify >= (uint32_t)GSHSCENE_IMAGE_FILTER_COUNT || in->minify >= (uint32_t)GSHSCENE_IMAGE_FILTER_COUNT) {
    return GSHSCENE_ERR_INVALID;
  }
  GSHSCENE_Result space = gshscene_color_space_check(&in->filter_space);
  if (space == GSHSCENE_ERR_INVALID) {
    return space;
  }
  /* W5: a tint exactly when the image is alpha-only. */
  int stencil = info->alpha_only != 0;
  if (stencil != (in->tint != NULL)) {
    return GSHSCENE_ERR_INVALID;
  }
  GSHSCENE_Result tint_result = GSHSCENE_OK;
  memset(tint, 0, sizeof *tint);
  if (in->tint) {
    if (in->tint->kind != GSHSCENE_PAINT_SOLID && in->tint->kind != GSHSCENE_PAINT_FOREGROUND &&
        in->tint->kind != GSHSCENE_PAINT_PALETTE) {
      return GSHSCENE_ERR_INVALID;
    }
    tint_result = gshscene_list_canonical_paint_valid(store, in->tint, tint);
    if (tint_result == GSHSCENE_ERR_INVALID) {
      return tint_result;
    }
  }
  if (space != GSHSCENE_OK) {
    return space; /* a reserved space */
  }
  if (tint_result != GSHSCENE_OK) {
    return tint_result;
  }
  /* The bounds, after every validity check (IP1, G14, N1). */
  if (!map_in_band(&map) || (in->tint && tint->kind == GSHSCENE_PAINT_SOLID && !gshscene_color_bounded(tint->rgba))) {
    return GSHSCENE_ERR_LIMIT;
  }

  memset(out, 0, sizeof *out);
  out->image = image;
  for (int i = 0; i < 4; i++) {
    out->window[i] = window[i];
  }
  out->map = map;
  for (int i = 0; i < 2; i++) {
    for (int j = 0; j < 2; j++) {
      out->map.m[i][j] = plus_zero(map.m[i][j]);
    }
    out->map.t[i] = plus_zero(map.t[i]);
  }
  out->extend_x = in->extend_x;
  out->extend_y = in->extend_y;
  out->magnify = in->magnify;
  out->minify = in->minify;
  if (!stencil) {
    out->filter_space = in->filter_space; /* an alpha-only image has no color: the field does not apply and is zero */
  }
  if (tint->kind == GSHSCENE_PAINT_SOLID) {
    for (int i = 0; i < 4; i++) {
      tint->rgba[i] = plus_zero(tint->rgba[i]);
    }
  }
  return GSHSCENE_OK;
}

/* --- hash (IP13) ----------------------------------------------------------------------------- */

static uint64_t content_hash(const GSHSCENE_Image_Paint * p, int has_tint, const GSHSCENE_Paint * tint) {
  GSHGEOM_Hash h;
  gshgeom_hash_init(&h);
  gshgeom_hash_u32(&h, IMAGE_PAINT_FORMAT_VERSION);
  gshgeom_hash_u32(&h, (uint32_t)GSHSCENE_RESOURCE_IMAGE_PAINT);
  gshgeom_hash_u64(&h, gshscene_resource_hash(p->image));
  for (int i = 0; i < 4; i++) {
    gshgeom_hash_u32(&h, p->window[i]);
  }
  gshgeom_hash_u64(&h, gshgeom_map_hash(&p->map));
  gshgeom_hash_u8(&h, (uint8_t)p->extend_x);
  gshgeom_hash_u8(&h, (uint8_t)p->extend_y);
  gshgeom_hash_u8(&h, (uint8_t)p->magnify);
  gshgeom_hash_u8(&h, (uint8_t)p->minify);
  gshscene_space_hash(&h, &p->filter_space);
  gshgeom_hash_u8(&h, has_tint ? 1u : 0u);
  if (has_tint) {
    gshgeom_hash_u8(&h, (uint8_t)tint->kind);
    for (int i = 0; i < 4; i++) {
      gshgeom_hash_f64(&h, tint->rgba[i]);
    }
    gshscene_space_hash(&h, &tint->space);
    gshgeom_hash_u32(&h, tint->entry);
  }
  return gshgeom_hash_value(&h);
}

/* --- equality, build, free ------------------------------------------------------------------ */

/** What an intern carries: the canonical content and its tint (or none). */
typedef struct Source {
  GSHSCENE_Image_Paint p;
  GSHSCENE_Paint tint;
  int has_tint;
} Source;

static int bits_equal(double a, double b) {
  return gshgeom_f64_bits(a) == gshgeom_f64_bits(b);
}

/** Whether two tints are the same paint, bit for bit. */
static int paint_equal(const GSHSCENE_Paint * a, const GSHSCENE_Paint * b) {
  for (int i = 0; i < 4; i++) {
    if (!bits_equal(a->rgba[i], b->rgba[i])) {
      return 0;
    }
  }
  return a->kind == b->kind && a->entry == b->entry && gshscene_space_equal(&a->space, &b->space);
}

int gshscene_image_paint_matches(const GSHSCENE_Resource * resource, const void * source) {
  const Source * want = (const Source *)source;
  const GSHSCENE_Image_Paint * a = &resource->u.imagepaint.p;
  const GSHSCENE_Image_Paint * b = &want->p;
  if (a->image != b->image || a->extend_x != b->extend_x || a->extend_y != b->extend_y || a->magnify != b->magnify ||
      a->minify != b->minify || !gshscene_space_equal(&a->filter_space, &b->filter_space)) {
    return 0;
  }
  for (int i = 0; i < 4; i++) {
    if (a->window[i] != b->window[i]) {
      return 0;
    }
  }
  if (a->map.dim != b->map.dim || a->map.kind != b->map.kind) {
    return 0;
  }
  for (int i = 0; i < 2; i++) {
    for (int j = 0; j < 2; j++) {
      if (!bits_equal(a->map.m[i][j], b->map.m[i][j])) {
        return 0;
      }
    }
    if (!bits_equal(a->map.t[i], b->map.t[i])) {
      return 0;
    }
  }
  int has = a->tint != NULL;
  if (has != want->has_tint) {
    return 0;
  }
  return !has || paint_equal(&resource->u.imagepaint.tint, &want->tint);
}

static GSHSCENE_Result build(GSHSCENE_Store * store, const void * source, GSHSCENE_Resource ** out) {
  const Source * s = (const Source *)source;
  GSHSCENE_Resource * resource = gshscene_store_new_resource(store, GSHSCENE_RESOURCE_IMAGE_PAINT);
  if (!resource) {
    return GSHSCENE_ERR_OOM;
  }
  resource->u.imagepaint.p = s->p;
  resource->u.imagepaint.tint = s->tint;
  resource->u.imagepaint.image = gshscene_resource_retain(s->p.image);
  resource->u.imagepaint.p.image = resource->u.imagepaint.image;
  resource->u.imagepaint.p.tint = s->has_tint ? &resource->u.imagepaint.tint : NULL;
  resource->u.imagepaint.foreground = s->has_tint && s->tint.kind == GSHSCENE_PAINT_FOREGROUND;
  resource->u.imagepaint.palette_need = s->has_tint && s->tint.kind == GSHSCENE_PAINT_PALETTE ? s->tint.entry + 1u : 0u;
  *out = resource;
  return GSHSCENE_OK;
}

void gshscene_image_paint_free(GSHSCENE_Resource * resource) {
  gshscene_resource_release(resource->u.imagepaint.image);
}

GSHSCENE_Result gshscene_store_intern_image_paint(GSHSCENE_Store * store, const GSHSCENE_Image_Paint * paint,
    const GSHSCENE_Resource ** out) {
  if (!store || !paint || !out) {
    return GSHSCENE_ERR_INVALID;
  }
  Source s;
  memset(&s, 0, sizeof s);
  GSHSCENE_Result r = canonical(store, paint, &s.p, &s.tint);
  if (r != GSHSCENE_OK) {
    return r;
  }
  s.has_tint = paint->tint != NULL;
  s.p.tint = s.has_tint ? &s.tint : NULL;
  GSHSCENE_Probe probe = {GSHSCENE_RESOURCE_IMAGE_PAINT, content_hash(&s.p, s.has_tint, &s.tint), GSHSCENE_BYTES_BASE, &s, build};
  return gshscene_store_intern(store, &probe, out);
}

GSHSCENE_Result gshscene_resource_image_paint(const GSHSCENE_Resource * resource, GSHSCENE_Image_Paint * out) {
  if (!resource || !out || resource->kind != GSHSCENE_RESOURCE_IMAGE_PAINT) {
    return GSHSCENE_ERR_INVALID;
  }
  *out = resource->u.imagepaint.p;
  return GSHSCENE_OK;
}

int gshscene_image_paint_singular(const GSHSCENE_Resource * resource) {
  return resource && resource->kind == GSHSCENE_RESOURCE_IMAGE_PAINT && gshscene_map_singular(&resource->u.imagepaint.p.map);
}

/* --- the dump (IP13) --------------------------------------------------------------------------- */

int gshscene_image_paint_dump(const GSHSCENE_Resource * resource, FILE * out) {
  const GSHSCENE_Image_Paint * p = &resource->u.imagepaint.p;
  if (fprintf(out, "imagepaint image=%016llx window=%u %u %u %u map=%016llx extend=%u,%u filter=%u,%u space=",
          HEX(gshscene_resource_hash(p->image)), p->window[0], p->window[1], p->window[2], p->window[3],
          HEX(gshgeom_map_hash(&p->map)), p->extend_x, p->extend_y, p->magnify, p->minify) < 0 ||
      gshscene_space_dump(out, &p->filter_space) < 0 || fputs(" tint=", out) == EOF) {
    return -1;
  }
  if (!p->tint) {
    return fputs("-\n", out) == EOF ? -1 : 0;
  }
  const GSHSCENE_Paint * t = p->tint;
  if (fprintf(out, "%u %016llx %016llx %016llx %016llx ", (unsigned)t->kind, HEX(gshgeom_f64_bits(t->rgba[0])),
          HEX(gshgeom_f64_bits(t->rgba[1])), HEX(gshgeom_f64_bits(t->rgba[2])), HEX(gshgeom_f64_bits(t->rgba[3]))) < 0 ||
      gshscene_space_dump(out, &t->space) < 0 || fprintf(out, " %u\n", t->entry) < 0) {
    return -1;
  }
  return 0;
}

/* --- IP4: the decode every sink shares -------------------------------------------------------- */

/** The sample @p c of texel (@p x, @p y), decoded: `v / (2^n - 1)`, or a float saturated at `+-2^64`. */
static double decode_sample(const GSHSCENE_Image_Info * info, const unsigned char * row, uint32_t x, uint32_t c) {
  size_t bytes = info->bits <= 8u ? 1u : info->bits <= 16u ? 2u : 4u;
  const unsigned char * at = row + ((size_t)x * info->layout + c) * bytes;
  if (info->sample_type == (uint32_t)GSHSCENE_IMAGE_FLOAT) {
    float f;
    memcpy(&f, at, sizeof f);
    return gshscene_saturate((double)f);
  }
  if (bytes == 1) {
    return (double)*at / 255.0;
  }
  uint16_t v;
  memcpy(&v, at, sizeof v);
  return (double)v / (info->bits == 12u ? 4095.0 : 65535.0);
}

GSHSCENE_Result gshscene_image_decode(const GSHSCENE_Resource * image, uint32_t x, uint32_t y, double out[4]) {
  if (!image || !out || image->kind != GSHSCENE_RESOURCE_IMAGE || x >= image->u.image.info.width ||
      y >= image->u.image.info.height) {
    return GSHSCENE_ERR_INVALID;
  }
  const GSHSCENE_Image_Info * info = &image->u.image.info;
  const unsigned char * row = image->u.image.rows + (size_t)y * image->u.image.stride;
  if (info->alpha_only) {
    out[0] = out[1] = out[2] = 0.0;
    out[3] = decode_sample(info, row, x, 0);
    return GSHSCENE_OK;
  }
  double c[3];
  double a = 1.0;
  if (info->layout == (uint32_t)GSHSCENE_IMAGE_GRAY) {
    c[0] = c[1] = c[2] = decode_sample(info, row, x, 0);
  } else {
    for (uint32_t k = 0; k < 3; k++) {
      c[k] = decode_sample(info, row, x, k);
    }
    if (info->layout == (uint32_t)GSHSCENE_IMAGE_RGBA) {
      a = decode_sample(info, row, x, 3);
    }
  }
  /* The straight form multiplies by alpha; the premultiplied form is taken as stored (IP4). */
  int straight = info->alpha_form == (uint32_t)GSHSCENE_IMAGE_ALPHA_STRAIGHT;
  for (int k = 0; k < 3; k++) {
    out[k] = straight ? a * c[k] : c[k];
  }
  out[3] = a;
  return GSHSCENE_OK;
}

/* --- IP14: a drawn image -------------------------------------------------------------------- */

/** The window's corners as the paint's map sends them, in the order (x0, y0), (x1, y0), (x1, y1), (x0, y1). */
static int window_quad(const GSHSCENE_Image_Paint * p, double quad[8]) {
  const double xs[4] = {(double)p->window[0], (double)p->window[2], (double)p->window[2], (double)p->window[0]};
  const double ys[4] = {(double)p->window[1], (double)p->window[1], (double)p->window[3], (double)p->window[3]};
  for (int i = 0; i < 4; i++) {
    GSHGEOM_Point q;
    if (gshgeom_map_apply_point(&p->map, (GSHGEOM_Point){xs[i], ys[i], 0.0}, &q) != GSHGEOM_OK) {
      return 0;
    }
    quad[2 * i] = q.x;
    quad[2 * i + 1] = q.y;
  }
  return 1;
}

/** The four points of the path's one closed subpath of four line segments; 0 for any other path. */
static int path_quad(const GSHGEOM_Path * path, double pts[8]) {
  if (!path || gshgeom_path_dimension(path) != 2 || gshgeom_path_element_count(path) != 5) {
    return 0;
  }
  static const GSHGEOM_Verb want[5] = {GSHGEOM_VERB_MOVE, GSHGEOM_VERB_LINE, GSHGEOM_VERB_LINE, GSHGEOM_VERB_LINE,
      GSHGEOM_VERB_CLOSE};
  for (size_t i = 0; i < 5; i++) {
    GSHGEOM_Element e;
    if (gshgeom_path_element(path, i, &e) != GSHGEOM_OK || e.verb != want[i]) {
      return 0;
    }
    if (i < 4) {
      GSHGEOM_Point end = e.verb == GSHGEOM_VERB_MOVE ? e.curve.p[0] : e.curve.p[1];
      pts[2 * i] = end.x;
      pts[2 * i + 1] = end.y;
    }
  }
  return 1;
}

int gshscene_image_fill_is_draw(const GSHSCENE_Item * item, double quad[8]) {
  if (!item || item->kind != GSHSCENE_ITEM_FILL || item->paint.kind != GSHSCENE_PAINT_IMAGE || !item->paint.resource ||
      item->paint.resource->kind != GSHSCENE_RESOURCE_IMAGE_PAINT || !item->resource ||
      item->resource->kind != GSHSCENE_RESOURCE_PATH) {
    return 0;
  }
  const GSHSCENE_Image_Paint * p = &item->paint.resource->u.imagepaint.p;
  if (p->extend_x != (uint32_t)GSHSCENE_IMAGE_EXTEND_PAD || p->extend_y != (uint32_t)GSHSCENE_IMAGE_EXTEND_PAD) {
    return 0;
  }
  double want[8];
  double got[8];
  if (!window_quad(p, want) || !path_quad(item->resource->u.path, got)) {
    return 0;
  }
  /* Either winding, from either starting corner: the path's points are the corners rotated by `shift`, forward or reversed.
   * Equal as doubles (so -0 and +0 are one corner), never within a tolerance. */
  for (int reversed = 0; reversed < 2; reversed++) {
    for (int shift = 0; shift < 4; shift++) {
      int same = 1;
      for (int i = 0; i < 4 && same; i++) {
        int k = reversed ? ((shift - i) % 4 + 4) % 4 : (shift + i) % 4;
        same = got[2 * i] == want[2 * k] && got[2 * i + 1] == want[2 * k + 1];
      }
      if (same) {
        if (quad) {
          memcpy(quad, want, sizeof want);
        }
        return 1;
      }
    }
  }
  return 0;
}

GSHSCENE_Result gshscene_image_paint_rect_path(GSHSCENE_Store * store, const GSHSCENE_Resource * image_paint,
    const GSHSCENE_Resource ** out) {
  if (!store || !image_paint || !out || image_paint->kind != GSHSCENE_RESOURCE_IMAGE_PAINT || image_paint->store != store) {
    return GSHSCENE_ERR_INVALID;
  }
  double quad[8];
  if (!window_quad(&image_paint->u.imagepaint.p, quad)) {
    return GSHSCENE_ERR_LIMIT;
  }
  GSHGEOM_Path * path = NULL;
  if (gshgeom_path_create(&store->alloc, NULL, 2, &path) != GSHGEOM_OK) {
    return GSHSCENE_ERR_OOM;
  }
  GSHGEOM_Result g = gshgeom_path_move_to(path, (GSHGEOM_Point){quad[0], quad[1], 0.0});
  for (int i = 1; i < 4 && g == GSHGEOM_OK; i++) {
    g = gshgeom_path_line_to(path, (GSHGEOM_Point){quad[2 * i], quad[2 * i + 1], 0.0});
  }
  if (g == GSHGEOM_OK) {
    g = gshgeom_path_close(path);
  }
  GSHSCENE_Result r = g == GSHGEOM_OK ? gshscene_store_intern_path(store, path, out)
      : g == GSHGEOM_ERR_LIMIT ? GSHSCENE_ERR_LIMIT : GSHSCENE_ERR_OOM;
  gshgeom_path_destroy(path);
  return r;
}

/* --- S21, IP8: what a sink needs, and which filter an item uses -------------------------------- */

uint32_t gshscene_image_required(const GSHSCENE_Item * item) {
  if (!item || (item->kind != GSHSCENE_ITEM_FILL && item->kind != GSHSCENE_ITEM_STROKE) ||
      item->paint.kind != GSHSCENE_PAINT_IMAGE || !item->paint.resource ||
      item->paint.resource->kind != GSHSCENE_RESOURCE_IMAGE_PAINT) {
    return 0;
  }
  const GSHSCENE_Image_Paint * p = &item->paint.resource->u.imagepaint.p;
  const GSHSCENE_Image_Info * info = &p->image->u.image.info;
  uint32_t formats = info->sample_type == (uint32_t)GSHSCENE_IMAGE_FLOAT ? GSHSCENE_IMAGE_FORMAT_FLOAT32
      : info->bits == 8u ? GSHSCENE_IMAGE_FORMAT_UNORM8
      : info->bits == 12u ? GSHSCENE_IMAGE_FORMAT_UNORM12 : GSHSCENE_IMAGE_FORMAT_UNORM16;
  formats |= info->layout == (uint32_t)GSHSCENE_IMAGE_GRAY ? GSHSCENE_IMAGE_FORMAT_GRAY
      : info->layout == (uint32_t)GSHSCENE_IMAGE_RGB ? GSHSCENE_IMAGE_FORMAT_RGB : GSHSCENE_IMAGE_FORMAT_RGBA;
  if (info->alpha_only) {
    formats |= GSHSCENE_IMAGE_FORMAT_ALPHA_ONLY;
  }
  return ((UINT32_C(1) << p->extend_x) | (UINT32_C(1) << p->extend_y)) | (formats << GSHSCENE_IMAGE_REQUIRED_FORMAT_SHIFT);
}

GSHSCENE_Result gshscene_image_filter_of(const GSHSCENE_Item * item, const GSHGEOM_Map * instance_maps,
    size_t instance_count, const GSHGEOM_Map * target_map, uint32_t * out) {
  if (!item || !out || (item->kind != GSHSCENE_ITEM_FILL && item->kind != GSHSCENE_ITEM_STROKE) ||
      item->paint.kind != GSHSCENE_PAINT_IMAGE || !item->paint.resource ||
      item->paint.resource->kind != GSHSCENE_RESOURCE_IMAGE_PAINT || (instance_count && !instance_maps)) {
    return GSHSCENE_ERR_INVALID;
  }
  const GSHSCENE_Image_Paint * p = &item->paint.resource->u.imagepaint.p;
  /* The chain as gshgeom_map_compose_chain forms it, one composition at a time and in its order: the instances root to leaf,
   * the item's map, the paint's (the leaf), then the target (a zero-filled one is the identity, S4). */
  GSHGEOM_Map acc = instance_count ? instance_maps[0] : item->map;
  GSHGEOM_Result r = gshgeom_map_check(&acc);
  for (size_t i = 1; r == GSHGEOM_OK && i <= instance_count; i++) {
    r = gshgeom_map_compose(&acc, i < instance_count ? &instance_maps[i] : &item->map, &acc);
  }
  if (r == GSHGEOM_OK) {
    r = gshgeom_map_compose(&acc, &p->map, &acc);
  }
  if (r == GSHGEOM_OK && target_map && target_map->dim != 0) {
    r = gshgeom_map_compose(target_map, &acc, &acc);
  }
  if (r != GSHGEOM_OK) {
    return GSHSCENE_ERR_LIMIT;
  }
  *out = gshgeom_map_stretches_every_direction(&acc) ? p->magnify : p->minify;
  return GSHSCENE_OK;
}
