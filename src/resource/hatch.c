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
 * The hatch resource and its paint (hatch.h, HX1 to HX3, HX9, HX15): the canonical form, the hash and the dump, the
 * notice predicate and the bounds.  The geometry (the lines, the trim, the dashes, the stroke) is `shape-geometry`'s.
 */

#include "../internal.h"

#include <ghoti.io/shape-geometry/hatch.h>
#include <ghoti.io/shape-geometry/stroke.h>

#include <string.h>

/** The format of a hatch's hash and dump (resource.h). */
#define HATCH_FORMAT_VERSION 1u

#define HEX(x) ((unsigned long long)(x))

void gshscene_hatch_init(GSHSCENE_Hatch * hatch) {
  if (!hatch) {
    return;
  }
  memset(hatch, 0, sizeof *hatch);
  hatch->angle_unit = GSHGEOM_ANGLE_DEGREES;
  hatch->nesting = GSHSCENE_HATCH_NESTING_RULE;
  gshgeom_map_identity(2, &hatch->map);
  hatch->ink.kind = GSHSCENE_PAINT_SOLID;
  hatch->ink.rgba[3] = 1.0;
}

/** A value within G14's band: zero, or a magnitude in `[2^-64, 2^64]`. */
static int in_band(double x) {
  double m = x < 0.0 ? -x : x;
  return m == 0.0 || (m >= GSHSCENE_GRADIENT_VALUE_MIN && m <= GSHSCENE_GRADIENT_VALUE_MAX);
}

static double plus_zero(double x) {
  return x == 0.0 ? 0.0 : x;
}

/** What an intern carries: the canonical content, the families' dashes in one array behind them. */
typedef struct Source {
  GSHSCENE_Hatch h;
  GSHSCENE_Hatch_Family * families; /* the store's allocator's, freed by free_source */
  double * dashes;
  size_t dash_total;
  GSHSCENE_Allocator alloc;
} Source;

static void free_source(Source * s) {
  gshscene_free(&s->alloc, s->families);
  gshscene_free(&s->alloc, s->dashes);
  s->families = NULL;
  s->dashes = NULL;
}

/** HX2's validity, then the unsupported, then the bounds, in the header's order; on success @p s holds the canonical content. */
static GSHSCENE_Result canonical(const GSHSCENE_Store * store, const GSHSCENE_Hatch * in, Source * s) {
  if (in->angle_unit >= (uint32_t)GSHGEOM_ANGLE_UNIT_COUNT || in->nesting >= (uint32_t)GSHSCENE_HATCH_NESTING_COUNT) {
    return GSHSCENE_ERR_INVALID;
  }
  if (!in->style || in->style->kind != GSHSCENE_RESOURCE_STROKE_STYLE || in->style->store != store) {
    return GSHSCENE_ERR_INVALID;
  }
  if (in->family_count < 1 || !in->families) {
    return GSHSCENE_ERR_INVALID;
  }
  size_t total = 0;
  for (size_t i = 0; i < in->family_count; i++) {
    const GSHSCENE_Hatch_Family * f = &in->families[i];
    if (!gshscene_finite(f->angle) || !gshscene_finite(f->origin_x) || !gshscene_finite(f->origin_y) ||
        !gshscene_finite(f->offset_along) || !gshscene_finite(f->offset_across) || (f->dash_count && !f->dashes)) {
      return GSHSCENE_ERR_INVALID;
    }
    for (size_t k = 0; k < f->dash_count; k++) {
      if (!gshscene_finite(f->dashes[k])) {
        return GSHSCENE_ERR_INVALID;
      }
    }
    total = gshscene_sat_add(total, f->dash_count);
  }
  GSHGEOM_Map map;
  if (gshscene_list_canonical_map(&in->map, &map) != GSHSCENE_OK || (map.dim != 2 && map.dim != 3)) {
    return GSHSCENE_ERR_INVALID;
  }
  if (in->ink.kind != GSHSCENE_PAINT_SOLID && in->ink.kind != GSHSCENE_PAINT_FOREGROUND &&
      in->ink.kind != GSHSCENE_PAINT_PALETTE) {
    return GSHSCENE_ERR_INVALID;
  }
  GSHSCENE_Paint ink;
  GSHSCENE_Result ink_result = gshscene_list_canonical_paint_valid(store, &in->ink, &ink);
  /* a family whose elements draw nothing (no pen-down element) or have no length has no period: invalid at record, not at render */
  for (size_t i = 0; i < in->family_count; i++) {
    const GSHSCENE_Hatch_Family * f = &in->families[i];
    if (f->dash_count) {
      int down = 0;
      double sum = 0.0;
      for (size_t k = 0; k < f->dash_count; k++) {
        down |= f->dashes[k] >= 0.0;
        sum += f->dashes[k] < 0.0 ? -f->dashes[k] : f->dashes[k];
      }
      if (!down || !(sum > 0.0)) {
        return GSHSCENE_ERR_INVALID;
      }
    }
  }
  if (map.dim == 3 || ink_result != GSHSCENE_OK) {
    return ink_result != GSHSCENE_OK ? ink_result : GSHSCENE_ERR_UNSUPPORTED;
  }
  const GSHSCENE_Stroke_Style * st = &in->style->u.stroke.s;
  if (st->dash_count != 0 || st->alignment != (uint32_t)GSHSCENE_STROKE_ALIGN_CENTER) {
    return GSHSCENE_ERR_INVALID; /* a second dash source, or an open piece with an inside */
  }
  /* the bounds */
  const size_t most_families = store->limits.max_resource_elements < GSHSCENE_HATCH_FAMILIES_MAX
      ? store->limits.max_resource_elements : GSHSCENE_HATCH_FAMILIES_MAX;
  const size_t most_dashes = store->limits.max_resource_elements < 65535u ? store->limits.max_resource_elements : 65535u;
  if (in->family_count > most_families) {
    return GSHSCENE_ERR_LIMIT;
  }
  for (size_t i = 0; i < in->family_count; i++) {
    const GSHSCENE_Hatch_Family * f = &in->families[i];
    double mag = f->angle < 0.0 ? -f->angle : f->angle;
    double c, sn;
    if (f->dash_count > most_dashes || !in_band(f->origin_x) || !in_band(f->origin_y) || !in_band(f->offset_along) ||
        !in_band(f->offset_across) || mag > 0x1p40 ||
        gshgeom_sincos((GSHGEOM_Angle_Unit)in->angle_unit, f->angle, &c, &sn) != GSHGEOM_OK) {
      return GSHSCENE_ERR_LIMIT;
    }
    for (size_t k = 0; k < f->dash_count; k++) {
      if (!in_band(f->dashes[k])) {
        return GSHSCENE_ERR_LIMIT;
      }
    }
  }
  for (int a = 0; a < 2; a++) {
    for (int b = 0; b < 2; b++) {
      if (!in_band(map.m[a][b])) {
        return GSHSCENE_ERR_LIMIT;
      }
    }
    if (!in_band(map.t[a])) {
      return GSHSCENE_ERR_LIMIT;
    }
  }
  if (ink.kind == GSHSCENE_PAINT_SOLID && !gshscene_color_bounded(ink.rgba)) {
    return GSHSCENE_ERR_LIMIT;
  }

  memset(s, 0, sizeof *s);
  s->alloc = store->alloc;
  s->families = total || in->family_count ? gshscene_calloc(&s->alloc, in->family_count, sizeof *s->families) : NULL;
  s->dashes = total ? gshscene_calloc(&s->alloc, total, sizeof(double)) : NULL;
  if (!s->families || (total && !s->dashes)) {
    free_source(s);
    return GSHSCENE_ERR_OOM;
  }
  s->dash_total = total;
  size_t at = 0;
  for (size_t i = 0; i < in->family_count; i++) {
    const GSHSCENE_Hatch_Family * f = &in->families[i];
    GSHSCENE_Hatch_Family * g = &s->families[i];
    g->angle = plus_zero(f->angle);
    g->origin_x = plus_zero(f->origin_x);
    g->origin_y = plus_zero(f->origin_y);
    g->offset_along = plus_zero(f->offset_along);
    g->offset_across = plus_zero(f->offset_across);
    g->dash_count = f->dash_count;
    g->dashes = f->dash_count ? s->dashes + at : NULL;
    for (size_t k = 0; k < f->dash_count; k++) {
      s->dashes[at++] = plus_zero(f->dashes[k]);
    }
  }
  s->h.angle_unit = in->angle_unit;
  s->h.nesting = in->nesting;
  s->h.style = in->style;
  s->h.map = map;
  for (int a = 0; a < 2; a++) {
    for (int b = 0; b < 2; b++) {
      s->h.map.m[a][b] = plus_zero(map.m[a][b]);
    }
    s->h.map.t[a] = plus_zero(map.t[a]);
  }
  s->h.ink = ink;
  for (int i = 0; i < 4; i++) {
    s->h.ink.rgba[i] = plus_zero(ink.rgba[i]);
  }
  s->h.family_count = in->family_count;
  s->h.families = s->families;
  return GSHSCENE_OK;
}

static uint64_t content_hash(const Source * s) {
  const GSHSCENE_Hatch * h = &s->h;
  GSHGEOM_Hash x;
  gshgeom_hash_init(&x);
  gshgeom_hash_u32(&x, HATCH_FORMAT_VERSION);
  gshgeom_hash_u32(&x, (uint32_t)GSHSCENE_RESOURCE_HATCH);
  gshgeom_hash_u8(&x, (uint8_t)h->angle_unit);
  gshgeom_hash_u8(&x, (uint8_t)h->nesting);
  gshgeom_hash_u64(&x, gshscene_resource_hash(h->style));
  gshgeom_hash_u64(&x, gshgeom_map_hash(&h->map));
  gshgeom_hash_u8(&x, (uint8_t)h->ink.kind);
  for (int i = 0; i < 4; i++) {
    gshgeom_hash_f64(&x, h->ink.rgba[i]);
  }
  gshscene_space_hash(&x, &h->ink.space);
  gshgeom_hash_u32(&x, h->ink.entry);
  gshgeom_hash_u64(&x, (uint64_t)h->family_count);
  for (size_t i = 0; i < h->family_count; i++) {
    const GSHSCENE_Hatch_Family * f = &h->families[i];
    gshgeom_hash_f64(&x, f->angle);
    gshgeom_hash_f64(&x, f->origin_x);
    gshgeom_hash_f64(&x, f->origin_y);
    gshgeom_hash_f64(&x, f->offset_along);
    gshgeom_hash_f64(&x, f->offset_across);
    gshgeom_hash_u64(&x, (uint64_t)f->dash_count);
    for (size_t k = 0; k < f->dash_count; k++) {
      gshgeom_hash_f64(&x, f->dashes[k]);
    }
  }
  return gshgeom_hash_value(&x);
}

static int bits_equal(double a, double b) {
  return gshgeom_f64_bits(a) == gshgeom_f64_bits(b);
}

int gshscene_hatch_matches(const GSHSCENE_Resource * resource, const void * source) {
  const GSHSCENE_Hatch * a = &resource->u.hatch.h;
  const GSHSCENE_Hatch * b = &((const Source *)source)->h;
  if (a->angle_unit != b->angle_unit || a->nesting != b->nesting || a->style != b->style || a->family_count != b->family_count ||
      a->map.kind != b->map.kind || a->ink.kind != b->ink.kind || a->ink.entry != b->ink.entry ||
      !gshscene_space_equal(&a->ink.space, &b->ink.space)) {
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
  for (int i = 0; i < 4; i++) {
    if (!bits_equal(a->ink.rgba[i], b->ink.rgba[i])) {
      return 0;
    }
  }
  for (size_t i = 0; i < a->family_count; i++) {
    const GSHSCENE_Hatch_Family *f = &a->families[i], *g = &b->families[i];
    if (!bits_equal(f->angle, g->angle) || !bits_equal(f->origin_x, g->origin_x) || !bits_equal(f->origin_y, g->origin_y) ||
        !bits_equal(f->offset_along, g->offset_along) || !bits_equal(f->offset_across, g->offset_across) ||
        f->dash_count != g->dash_count) {
      return 0;
    }
    for (size_t k = 0; k < f->dash_count; k++) {
      if (!bits_equal(f->dashes[k], g->dashes[k])) {
        return 0;
      }
    }
  }
  return 1;
}

static GSHSCENE_Result build(GSHSCENE_Store * store, const void * source, GSHSCENE_Resource ** out) {
  Source * s = (Source *)source;
  GSHSCENE_Resource * resource = gshscene_store_new_resource(store, GSHSCENE_RESOURCE_HATCH);
  if (!resource) {
    return GSHSCENE_ERR_OOM;
  }
  /* the resource's own copies of the families and their elements (the source frees its own) */
  resource->u.hatch.h = s->h;
  resource->u.hatch.families = gshscene_calloc(&store->alloc, s->h.family_count, sizeof *resource->u.hatch.families);
  resource->u.hatch.dashes = s->dash_total ? gshscene_calloc(&store->alloc, s->dash_total, sizeof(double)) : NULL;
  if (!resource->u.hatch.families || (s->dash_total && !resource->u.hatch.dashes)) {
    resource->u.hatch.style = NULL;
    resource->u.hatch.h.style = NULL;
    gshscene_store_discard_resource(resource);
    return GSHSCENE_ERR_OOM;
  }
  if (s->dash_total) {
    memcpy(resource->u.hatch.dashes, s->dashes, s->dash_total * sizeof(double));
  }
  size_t at = 0;
  for (size_t i = 0; i < s->h.family_count; i++) {
    resource->u.hatch.families[i] = s->families[i];
    resource->u.hatch.families[i].dashes = s->families[i].dash_count ? resource->u.hatch.dashes + at : NULL;
    at += s->families[i].dash_count;
  }
  resource->u.hatch.style = gshscene_resource_retain(s->h.style);
  resource->u.hatch.h.style = resource->u.hatch.style;
  resource->u.hatch.h.families = resource->u.hatch.families;
  resource->u.hatch.foreground = gshscene_paint_reads_foreground(&s->h.ink);
  resource->u.hatch.palette_need = gshscene_paint_palette_need(&s->h.ink);
  *out = resource;
  return GSHSCENE_OK;
}

void gshscene_hatch_free(GSHSCENE_Resource * resource) {
  const GSHSCENE_Allocator * alloc = &resource->store->alloc;
  gshscene_free(alloc, resource->u.hatch.families);
  gshscene_free(alloc, resource->u.hatch.dashes);
  resource->u.hatch.families = NULL;
  resource->u.hatch.dashes = NULL;
  gshscene_resource_release(resource->u.hatch.style);
}

GSHSCENE_Result gshscene_store_intern_hatch(GSHSCENE_Store * store, const GSHSCENE_Hatch * hatch, const GSHSCENE_Resource ** out) {
  if (!store || !hatch || !out) {
    return GSHSCENE_ERR_INVALID;
  }
  Source s;
  GSHSCENE_Result r = canonical(store, hatch, &s);
  if (r != GSHSCENE_OK) {
    return r;
  }
  GSHSCENE_Probe probe = {GSHSCENE_RESOURCE_HATCH, content_hash(&s), GSHSCENE_BYTES_BASE, &s, build};
  r = gshscene_store_intern(store, &probe, out);
  free_source(&s); /* what an intern that found an equal resource (or failed) did not take */
  return r;
}

GSHSCENE_Result gshscene_resource_hatch(const GSHSCENE_Resource * resource, GSHSCENE_Hatch * out) {
  if (!resource || !out || resource->kind != GSHSCENE_RESOURCE_HATCH) {
    return GSHSCENE_ERR_INVALID;
  }
  *out = resource->u.hatch.h;
  return GSHSCENE_OK;
}

int gshscene_hatch_dump(const GSHSCENE_Resource * resource, FILE * out) {
  const GSHSCENE_Hatch * h = &resource->u.hatch.h;
  if (fprintf(out, "hatch unit=%u nesting=%u style=%016llx map=%016llx ink=%u %016llx %016llx %016llx %016llx ", h->angle_unit, h->nesting,
          HEX(gshscene_resource_hash(h->style)), HEX(gshgeom_map_hash(&h->map)), (unsigned)h->ink.kind,
          HEX(gshgeom_f64_bits(h->ink.rgba[0])), HEX(gshgeom_f64_bits(h->ink.rgba[1])), HEX(gshgeom_f64_bits(h->ink.rgba[2])),
          HEX(gshgeom_f64_bits(h->ink.rgba[3]))) < 0 ||
      gshscene_space_dump(out, &h->ink.space) < 0 ||
      fprintf(out, " %u families=%llu\n", h->ink.entry, HEX(h->family_count)) < 0) {
    return -1;
  }
  for (size_t i = 0; i < h->family_count; i++) {
    const GSHSCENE_Hatch_Family * f = &h->families[i];
    if (fprintf(out, "family angle=%016llx origin=%016llx %016llx along=%016llx across=%016llx dashes=%llu", HEX(gshgeom_f64_bits(f->angle)),
            HEX(gshgeom_f64_bits(f->origin_x)), HEX(gshgeom_f64_bits(f->origin_y)), HEX(gshgeom_f64_bits(f->offset_along)),
            HEX(gshgeom_f64_bits(f->offset_across)), HEX(f->dash_count)) < 0) {
      return -1;
    }
    for (size_t k = 0; k < f->dash_count; k++) {
      if (fprintf(out, " %016llx", HEX(gshgeom_f64_bits(f->dashes[k]))) < 0) {
        return -1;
      }
    }
    if (fputc('\n', out) == EOF) {
      return -1;
    }
  }
  return 0;
}

/* --- HX9: what draws nothing ------------------------------------------------------------------ */

static const GSHSCENE_Resource * hatch_of(const GSHSCENE_Item * item) {
  if (!item || item->kind != GSHSCENE_ITEM_FILL || item->paint.kind != GSHSCENE_PAINT_HATCH || !item->paint.resource ||
      item->paint.resource->kind != GSHSCENE_RESOURCE_HATCH) {
    return NULL;
  }
  return item->paint.resource;
}

const GSHSCENE_Resource * gshscene_hatch_style(const GSHSCENE_Resource * resource) {
  return resource && resource->kind == GSHSCENE_RESOURCE_HATCH ? resource->u.hatch.style : NULL;
}

GSHSCENE_Reason gshscene_hatch_notice(const GSHSCENE_Item * item, const GSHGEOM_Map * instance_maps, size_t instance_count,
    const GSHGEOM_Map * target_map) {
  const GSHSCENE_Resource * hr = hatch_of(item);
  if (!hr || (instance_count && !instance_maps)) {
    return (GSHSCENE_Reason)0;
  }
  const GSHSCENE_Hatch * h = &hr->u.hatch.h;
  /* each map on its own doubles, no composition formed (HX9, SK9, G11) */
  int not_invertible = (target_map && gshscene_map_singular(target_map)) || gshscene_map_singular(&item->map) ||
      gshscene_map_singular(&h->map);
  for (size_t i = 0; i < instance_count && !not_invertible; i++) {
    not_invertible = gshscene_map_singular(&instance_maps[i]);
  }
  if (not_invertible) {
    return GSHSCENE_REASON_MAP_NOT_INVERTIBLE;
  }
  if (h->style->u.stroke.s.width == 0.0) {
    return GSHSCENE_REASON_STROKE_ZERO_WIDTH;
  }
  for (size_t i = 0; i < h->family_count; i++) {
    if (h->families[i].offset_across == 0.0) {
      return GSHSCENE_REASON_HATCH_DEGENERATE;
    }
  }
  return (GSHSCENE_Reason)0;
}

uint32_t gshscene_hatch_required(const GSHSCENE_Item * item, const GSHGEOM_Map * instance_maps, size_t instance_count,
    const GSHGEOM_Map * target_map) {
  const GSHSCENE_Resource * hr = hatch_of(item);
  if (!hr || gshscene_hatch_notice(item, instance_maps, instance_count, target_map) == GSHSCENE_REASON_MAP_NOT_INVERTIBLE) {
    return 0; /* nothing is drawn; a width of zero needs nothing either way, as gshscene_stroke_required says of a stroke's */
  }
  /* the style's requirements as a stroke of the same style with the item's map: the cap, the width space, an anisotropic local width;
   * a hatch has no join, no dashes (the style has none) and no alignment */
  GSHSCENE_Item as_stroke = *item;
  as_stroke.kind = GSHSCENE_ITEM_STROKE;
  as_stroke.style = hr->u.hatch.style;
  as_stroke.paint.kind = GSHSCENE_PAINT_SOLID;
  as_stroke.paint.resource = NULL;
  uint32_t need = gshscene_stroke_required(&as_stroke, instance_maps, instance_count, target_map);
  /* no join is made: drop the join bits the style's join would add */
  need &= ~(GSHSCENE_STROKE_NATIVE_JOIN_MITER | GSHSCENE_STROKE_NATIVE_JOIN_ROUND | GSHSCENE_STROKE_NATIVE_JOIN_BEVEL |
            GSHSCENE_STROKE_NATIVE_JOIN_MITER_CLIP | GSHSCENE_STROKE_NATIVE_JOIN_ARCS | GSHSCENE_STROKE_NATIVE_DEGENERATE_DRAW);
  /* a dot (a zero element) is a dash of the pattern, drawn whatever the style's `degenerate` says: a sink that strokes the pieces must
   * draw a degenerate shape for it, unless a butt cap draws nothing either way */
  const GSHSCENE_Hatch * h = &hr->u.hatch.h;
  int dot = 0;
  for (size_t i = 0; i < h->family_count && !dot; i++) {
    for (size_t k = 0; k < h->families[i].dash_count; k++) {
      dot |= h->families[i].dashes[k] == 0.0;
    }
  }
  if (dot && hr->u.hatch.style->u.stroke.s.cap != (uint32_t)GSHSCENE_STROKE_CAP_BUTT) {
    need |= GSHSCENE_STROKE_NATIVE_DEGENERATE_DRAW;
  }
  return need;
}
