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
 * The gradient resource (paint.h, G1 to G13; resource.h; color-paint story P1):
 * its validity, its canonical form (stops sorted stably, `-0` as `+0`), equality,
 * hash, dump and reading back; the one conversion of its stops at draw time
 * (G3); and the one predicate of what draws nothing (G10, G11).
 */

#include <ghoti.io/shape-scene/macros.h>

#include "../internal.h"

#include <ghoti.io/shape-geometry/orient.h>

#include <string.h>

/** The format of a gradient's hash and dump (resource.h). */
#define GRADIENT_FORMAT_VERSION 1u

#define HEX(x) ((unsigned long long)(x))

/** `-0` as `+0`: one spelling for one number. */
static double canonical_zero(double x) {
  return x == 0.0 ? 0.0 : x;
}

static int all_zero_bits(const double * v, size_t count) {
  for (size_t i = 0; i < count; i++) {
    if (gshgeom_f64_bits(v[i]) != 0) {
      return 0;
    }
  }
  return 1;
}

static int all_finite(const double * v, size_t count) {
  for (size_t i = 0; i < count; i++) {
    if (!gshscene_finite(v[i])) {
      return 0;
    }
  }
  return 1;
}

/* --- G14: the limits ------------------------------------------------------------- */

/** G14: zero, or a magnitude in [GSHSCENE_GRADIENT_VALUE_MIN, GSHSCENE_GRADIENT_VALUE_MAX] (@p x finite). */
static int within(double x) {
  double m = x < 0.0 ? -x : x;
  return m == 0.0 || (m >= GSHSCENE_GRADIENT_VALUE_MIN && m <= GSHSCENE_GRADIENT_VALUE_MAX);
}

static int all_within(const double * v, size_t count) {
  for (size_t i = 0; i < count; i++) {
    if (!within(v[i])) {
      return 0;
    }
  }
  return 1;
}

/** G14: whether every double of @p g (canonical: the other kinds' fields are zero) is within the bounds. */
static int limited(const GSHSCENE_Gradient * g) {
  const double values[16] = {g->p0[0], g->p0[1], g->p1[0], g->p1[1], g->d[0], g->d[1], g->c0[0], g->c0[1], g->r0,
      g->c1[0], g->c1[1], g->r1, g->center[0], g->center[1], g->a0, g->a1};
  const double map[6] = {g->map.m[0][0], g->map.m[0][1], g->map.m[1][0], g->map.m[1][1], g->map.t[0], g->map.t[1]};
  if (!all_within(values, 16) || !all_within(map, 6)) {
    return 0;
  }
  for (size_t i = 0; i < g->stop_count; i++) {
    if (!within(g->stops[i].offset)) {
      return 0;
    }
  }
  /* N1's bound on a solid stop's color (a foreground or palette stop's components are zero), refused with G14's. */
  for (size_t i = 0; i < g->stop_count; i++) {
    if (!gshscene_color_bounded(g->stops[i].color.rgba)) {
      return 0;
    }
  }
  return 1;
}

/* --- the canonical content ------------------------------------------------------- */

/**
 * Check @p in and write its canonical form to @p out, the stops (not yet sorted) to @p stops (room for
 * `in->stop_count`).  Nothing is allocated.
 */
static GSHSCENE_Result canonical(const GSHSCENE_Store * store, const GSHSCENE_Gradient * in, GSHSCENE_Gradient * out,
    GSHSCENE_Gradient_Stop * stops) {
  memset(out, 0, sizeof *out);
  out->kind = in->kind;
  /* The geometry of the kind, finite; every other kind's field zero (one spelling). */
  const double linear[6] = {in->p0[0], in->p0[1], in->p1[0], in->p1[1], in->d[0], in->d[1]};
  const double radial[6] = {in->c0[0], in->c0[1], in->r0, in->c1[0], in->c1[1], in->r1};
  const double sweep[4] = {in->center[0], in->center[1], in->a0, in->a1};
  switch (in->kind) {
    case GSHSCENE_GRADIENT_LINEAR:
      if (!all_finite(linear, 6) || !all_zero_bits(radial, 6) || !all_zero_bits(sweep, 4) || in->angle_unit != 0) {
        return GSHSCENE_ERR_INVALID;
      }
      for (int i = 0; i < 2; i++) {
        out->p0[i] = canonical_zero(in->p0[i]);
        out->p1[i] = canonical_zero(in->p1[i]);
        out->d[i] = canonical_zero(in->d[i]);
      }
      break;
    case GSHSCENE_GRADIENT_RADIAL:
      if (!all_finite(radial, 6) || !all_zero_bits(linear, 6) || !all_zero_bits(sweep, 4) || in->angle_unit != 0) {
        return GSHSCENE_ERR_INVALID;
      }
      for (int i = 0; i < 2; i++) {
        out->c0[i] = canonical_zero(in->c0[i]);
        out->c1[i] = canonical_zero(in->c1[i]);
      }
      out->r0 = canonical_zero(in->r0);
      out->r1 = canonical_zero(in->r1);
      break;
    case GSHSCENE_GRADIENT_SWEEP:
      if (!all_finite(sweep, 4) || !all_zero_bits(linear, 6) || !all_zero_bits(radial, 6) ||
          in->angle_unit >= (uint32_t)GSHGEOM_ANGLE_UNIT_COUNT) {
        return GSHSCENE_ERR_INVALID;
      }
      out->center[0] = canonical_zero(in->center[0]);
      out->center[1] = canonical_zero(in->center[1]);
      out->angle_unit = in->angle_unit;
      out->a0 = canonical_zero(in->a0);
      out->a1 = canonical_zero(in->a1);
      break;
    default:
      return GSHSCENE_ERR_INVALID;
  }
  if (in->extend >= (uint32_t)GSHSCENE_EXTEND_COUNT || in->premultiplied > 1u) {
    return GSHSCENE_ERR_INVALID;
  }
  if (in->map.dim != 2 || gshscene_list_canonical_map(&in->map, &out->map) != GSHSCENE_OK) {
    return GSHSCENE_ERR_INVALID;
  }
  GSHSCENE_Result result = gshscene_color_space_check(&in->space);
  if (result != GSHSCENE_OK) {
    return result;
  }
  out->extend = in->extend;
  out->space = in->space;
  out->premultiplied = in->premultiplied;
  int target = gshscene_color_space_is_target(&in->space);
  for (size_t i = 0; i < in->stop_count; i++) {
    const GSHSCENE_Gradient_Stop * s = &in->stops[i];
    if (!gshscene_finite(s->offset) || s->color.kind == GSHSCENE_PAINT_GRADIENT) {
      return GSHSCENE_ERR_INVALID;
    }
    result = gshscene_list_canonical_paint_valid(store, &s->color, &stops[i].color);
    if (result != GSHSCENE_OK) {
      return result;
    }
    /* G3: a solid stop in the target's space only in a gradient that interpolates in the target's space. */
    if (stops[i].color.kind == GSHSCENE_PAINT_SOLID && !target && gshscene_color_space_is_target(&stops[i].color.space)) {
      return GSHSCENE_ERR_INVALID;
    }
    stops[i].offset = canonical_zero(s->offset);
  }
  out->stop_count = in->stop_count;
  out->stops = stops;
  return GSHSCENE_OK;
}

/** G2: sort @p stops by offset, stably (a merge sort through @p scratch, room for @p count). */
static void sort_stops(GSHSCENE_Gradient_Stop * stops, GSHSCENE_Gradient_Stop * scratch, size_t count) {
  for (size_t width = 1; width < count; width *= 2) {
    for (size_t lo = 0; lo < count; lo += 2 * width) {
      size_t mid = lo + width < count ? lo + width : count;
      size_t hi = mid + width < count ? mid + width : count;
      size_t a = lo;
      size_t b = mid;
      size_t k = lo;
      while (a < mid && b < hi) {
        /* Equal offsets keep the given order: the left run's stop goes first. */
        scratch[k++] = stops[b].offset < stops[a].offset ? stops[b++] : stops[a++];
      }
      while (a < mid) {
        scratch[k++] = stops[a++];
      }
      while (b < hi) {
        scratch[k++] = stops[b++];
      }
    }
    memcpy(stops, scratch, count * sizeof *stops);
  }
}

/* --- hash and equality ---------------------------------------------------------- */

static void paint_hash(GSHGEOM_Hash * h, const GSHSCENE_Paint * paint) {
  gshgeom_hash_u8(h, (uint8_t)paint->kind);
  for (int k = 0; k < 4; k++) {
    gshgeom_hash_f64(h, paint->rgba[k]);
  }
  gshscene_space_hash(h, &paint->space);
  gshgeom_hash_u32(h, paint->entry);
}

static uint64_t content_hash(const GSHSCENE_Gradient * g) {
  GSHGEOM_Hash h;
  gshgeom_hash_init(&h);
  gshgeom_hash_u32(&h, GRADIENT_FORMAT_VERSION);
  gshgeom_hash_u32(&h, (uint32_t)GSHSCENE_RESOURCE_GRADIENT);
  gshgeom_hash_u8(&h, (uint8_t)g->kind);
  gshgeom_hash_u8(&h, (uint8_t)g->extend);
  gshscene_space_hash(&h, &g->space);
  gshgeom_hash_u8(&h, (uint8_t)g->premultiplied);
  gshgeom_hash_u64(&h, gshgeom_map_hash(&g->map));
  switch (g->kind) {
    case GSHSCENE_GRADIENT_LINEAR:
      gshgeom_hash_f64(&h, g->p0[0]);
      gshgeom_hash_f64(&h, g->p0[1]);
      gshgeom_hash_f64(&h, g->p1[0]);
      gshgeom_hash_f64(&h, g->p1[1]);
      gshgeom_hash_f64(&h, g->d[0]);
      gshgeom_hash_f64(&h, g->d[1]);
      break;
    case GSHSCENE_GRADIENT_RADIAL:
      gshgeom_hash_f64(&h, g->c0[0]);
      gshgeom_hash_f64(&h, g->c0[1]);
      gshgeom_hash_f64(&h, g->r0);
      gshgeom_hash_f64(&h, g->c1[0]);
      gshgeom_hash_f64(&h, g->c1[1]);
      gshgeom_hash_f64(&h, g->r1);
      break;
    default: /* sweep */
      gshgeom_hash_f64(&h, g->center[0]);
      gshgeom_hash_f64(&h, g->center[1]);
      gshgeom_hash_u8(&h, (uint8_t)g->angle_unit);
      gshgeom_hash_f64(&h, g->a0);
      gshgeom_hash_f64(&h, g->a1);
      break;
  }
  gshgeom_hash_u64(&h, (uint64_t)g->stop_count);
  for (size_t i = 0; i < g->stop_count; i++) {
    gshgeom_hash_f64(&h, g->stops[i].offset);
    paint_hash(&h, &g->stops[i].color);
  }
  return gshgeom_hash_value(&h);
}

static int same_bits(double a, double b) {
  return gshgeom_f64_bits(a) == gshgeom_f64_bits(b);
}

static int map_same(const GSHGEOM_Map * a, const GSHGEOM_Map * b) {
  if (a->dim != b->dim || a->kind != b->kind) {
    return 0;
  }
  for (int i = 0; i < 2; i++) {
    for (int j = 0; j < 2; j++) {
      if (!same_bits(a->m[i][j], b->m[i][j])) {
        return 0;
      }
    }
    if (!same_bits(a->t[i], b->t[i])) {
      return 0;
    }
  }
  return 1;
}

static int paint_same(const GSHSCENE_Paint * a, const GSHSCENE_Paint * b) {
  if (a->kind != b->kind || a->entry != b->entry || !gshscene_space_equal(&a->space, &b->space)) {
    return 0;
  }
  for (int k = 0; k < 4; k++) {
    if (!same_bits(a->rgba[k], b->rgba[k])) {
      return 0;
    }
  }
  return 1;
}

int gshscene_gradient_matches(const GSHSCENE_Resource * resource, const void * source) {
  const GSHSCENE_Gradient * a = &resource->u.gradient.g;
  const GSHSCENE_Gradient * b = (const GSHSCENE_Gradient *)source;
  if (a->kind != b->kind || a->extend != b->extend || a->premultiplied != b->premultiplied ||
      a->angle_unit != b->angle_unit || !gshscene_space_equal(&a->space, &b->space) || !map_same(&a->map, &b->map) ||
      a->stop_count != b->stop_count) {
    return 0;
  }
  const double ga[16] = {a->p0[0], a->p0[1], a->p1[0], a->p1[1], a->d[0], a->d[1], a->c0[0], a->c0[1], a->r0,
      a->c1[0], a->c1[1], a->r1, a->center[0], a->center[1], a->a0, a->a1};
  const double gb[16] = {b->p0[0], b->p0[1], b->p1[0], b->p1[1], b->d[0], b->d[1], b->c0[0], b->c0[1], b->r0,
      b->c1[0], b->c1[1], b->r1, b->center[0], b->center[1], b->a0, b->a1};
  for (int i = 0; i < 16; i++) {
    if (!same_bits(ga[i], gb[i])) {
      return 0;
    }
  }
  for (size_t i = 0; i < a->stop_count; i++) {
    if (!same_bits(a->stops[i].offset, b->stops[i].offset) || !paint_same(&a->stops[i].color, &b->stops[i].color)) {
      return 0;
    }
  }
  return 1;
}

/* --- build, free, intern -------------------------------------------------------------- */

static GSHSCENE_Result build_gradient(GSHSCENE_Store * store, const void * source, GSHSCENE_Resource ** out) {
  const GSHSCENE_Gradient * g = (const GSHSCENE_Gradient *)source;
  GSHSCENE_Resource * resource = gshscene_store_new_resource(store, GSHSCENE_RESOURCE_GRADIENT);
  if (!resource) {
    return GSHSCENE_ERR_OOM;
  }
  if (g->stop_count) {
    resource->u.gradient.stops = gshscene_alloc(&store->alloc, g->stop_count * sizeof(GSHSCENE_Gradient_Stop));
    if (!resource->u.gradient.stops) {
      gshscene_store_discard_resource(resource);
      return GSHSCENE_ERR_OOM;
    }
    memcpy(resource->u.gradient.stops, g->stops, g->stop_count * sizeof(GSHSCENE_Gradient_Stop));
  }
  resource->u.gradient.g = *g;
  resource->u.gradient.g.stops = resource->u.gradient.stops;
  for (size_t i = 0; i < g->stop_count; i++) {
    const GSHSCENE_Paint * p = &g->stops[i].color;
    if (p->kind == GSHSCENE_PAINT_FOREGROUND) {
      resource->u.gradient.foreground = 1;
    } else if (p->kind == GSHSCENE_PAINT_PALETTE && p->entry + 1u > resource->u.gradient.palette_need) {
      resource->u.gradient.palette_need = p->entry + 1u;
    }
  }
  *out = resource;
  return GSHSCENE_OK;
}

void gshscene_gradient_free(GSHSCENE_Resource * resource) {
  gshscene_free(&resource->store->alloc, resource->u.gradient.stops);
}

GSHSCENE_Result gshscene_store_intern_gradient(
    GSHSCENE_Store * store, const GSHSCENE_Gradient * gradient, const GSHSCENE_Resource ** out) {
  if (!store || !gradient || !out || (gradient->stop_count && !gradient->stops)) {
    return GSHSCENE_ERR_INVALID;
  }
  size_t count = gradient->stop_count;
  if (count > store->limits.max_resource_elements || count > GSHSCENE_GRADIENT_STOPS_MAX) {
    return GSHSCENE_ERR_LIMIT; /* G2, G14 */
  }
  GSHSCENE_Gradient_Stop * stops = NULL;
  if (count) {
    /* The canonical stops, then the sort's scratch. */
    stops = gshscene_alloc(&store->alloc, 2 * count * sizeof *stops);
    if (!stops) {
      return GSHSCENE_ERR_OOM;
    }
  }
  GSHSCENE_Gradient g;
  GSHSCENE_Result result = canonical(store, gradient, &g, stops);
  if (result == GSHSCENE_OK && !limited(&g)) {
    result = GSHSCENE_ERR_LIMIT; /* G14, after every validity check */
  }
  if (result == GSHSCENE_OK) {
    sort_stops(stops, count ? stops + count : NULL, count);
    GSHSCENE_Probe probe = {GSHSCENE_RESOURCE_GRADIENT, content_hash(&g),
        gshscene_sat_add(GSHSCENE_BYTES_BASE, gshscene_sat_mul(count, GSHSCENE_BYTES_PER_STOP)), &g, build_gradient};
    result = gshscene_store_intern(store, &probe, out);
  }
  gshscene_free(&store->alloc, stops);
  return result;
}

GSHSCENE_Result gshscene_resource_gradient(const GSHSCENE_Resource * resource, GSHSCENE_Gradient * out) {
  if (!resource || !out || resource->kind != GSHSCENE_RESOURCE_GRADIENT) {
    return GSHSCENE_ERR_INVALID;
  }
  *out = resource->u.gradient.g;
  return GSHSCENE_OK;
}

/* --- dump ------------------------------------------------------------------------------- */

static int dump_xy(FILE * out, const char * name, const double xy[2]) {
  return fprintf(out, " %s=%016llx %016llx", name, HEX(gshgeom_f64_bits(xy[0])), HEX(gshgeom_f64_bits(xy[1])));
}

int gshscene_gradient_dump(const GSHSCENE_Resource * resource, FILE * out) {
  const GSHSCENE_Gradient * g = &resource->u.gradient.g;
  if (fprintf(out, "gradient kind=%u extend=%u space=", (unsigned)g->kind, (unsigned)g->extend) < 0 ||
      gshscene_space_dump(out, &g->space) < 0 ||
      fprintf(out, " premul=%u map=%016llx stops=%llu\n", (unsigned)g->premultiplied, HEX(gshgeom_map_hash(&g->map)),
          HEX(g->stop_count)) < 0) {
    return -1;
  }
  int w = 0;
  switch (g->kind) {
    case GSHSCENE_GRADIENT_LINEAR:
      w = fputs("linear", out) == EOF ? -1 : 0;
      if (w >= 0) {
        w = dump_xy(out, "p0", g->p0);
      }
      if (w >= 0) {
        w = dump_xy(out, "p1", g->p1);
      }
      if (w >= 0) {
        w = dump_xy(out, "d", g->d);
      }
      break;
    case GSHSCENE_GRADIENT_RADIAL:
      w = fputs("radial", out) == EOF ? -1 : 0;
      if (w >= 0) {
        w = dump_xy(out, "c0", g->c0);
      }
      if (w >= 0) {
        w = fprintf(out, " r0=%016llx", HEX(gshgeom_f64_bits(g->r0)));
      }
      if (w >= 0) {
        w = dump_xy(out, "c1", g->c1);
      }
      if (w >= 0) {
        w = fprintf(out, " r1=%016llx", HEX(gshgeom_f64_bits(g->r1)));
      }
      break;
    default:
      w = fputs("sweep", out) == EOF ? -1 : 0;
      if (w >= 0) {
        w = dump_xy(out, "c", g->center);
      }
      if (w >= 0) {
        w = fprintf(out, " unit=%u a0=%016llx a1=%016llx", (unsigned)g->angle_unit, HEX(gshgeom_f64_bits(g->a0)),
            HEX(gshgeom_f64_bits(g->a1)));
      }
      break;
  }
  if (w < 0 || fputc('\n', out) == EOF) {
    return -1;
  }
  for (size_t i = 0; i < g->stop_count; i++) {
    const GSHSCENE_Gradient_Stop * s = &g->stops[i];
    if (fprintf(out, "s %016llx paint=%u %016llx %016llx %016llx %016llx space=", HEX(gshgeom_f64_bits(s->offset)),
            (unsigned)s->color.kind, HEX(gshgeom_f64_bits(s->color.rgba[0])), HEX(gshgeom_f64_bits(s->color.rgba[1])),
            HEX(gshgeom_f64_bits(s->color.rgba[2])), HEX(gshgeom_f64_bits(s->color.rgba[3]))) < 0 ||
        gshscene_space_dump(out, &s->color.space) < 0 || fprintf(out, " entry=%u\n", (unsigned)s->color.entry) < 0) {
      return -1;
    }
  }
  return 0;
}

/* --- G3: the stops at draw time -------------------------------------------------------- */

GSHSCENE_Result gshscene_gradient_resolve(const GSHSCENE_Resource * gradient, const GSHSCENE_Color * foreground,
    const GSHSCENE_Resource * palette, const GSHSCENE_Color_Space * target_space, GSHSCENE_Color_Space * out_space,
    GSHSCENE_Resolved_Stop * out, size_t capacity) {
  if (!gradient || gradient->kind != GSHSCENE_RESOURCE_GRADIENT || !foreground || !target_space ||
      (palette && palette->kind != GSHSCENE_RESOURCE_PALETTE) || gshscene_color_space_is_target(target_space) ||
      gshscene_color_space_check(target_space) != GSHSCENE_OK) {
    return GSHSCENE_ERR_INVALID;
  }
  const GSHSCENE_Gradient * g = &gradient->u.gradient.g;
  if (capacity < g->stop_count || (g->stop_count && !out)) {
    return GSHSCENE_ERR_INVALID;
  }
  /* A missing entry refuses the fill before anything is written (N14). */
  if (gradient->u.gradient.palette_need && (!palette || gradient->u.gradient.palette_need > palette->u.palette.count)) {
    return GSHSCENE_ERR_REFUSED;
  }
  const GSHSCENE_Color_Space * into = gshscene_color_space_is_target(&g->space) ? target_space : &g->space;
  for (size_t i = 0; i < g->stop_count; i++) {
    GSHSCENE_Color c;
    GSHSCENE_Result r = gshscene_paint_resolve(&g->stops[i].color, foreground, palette, &c);
    if (r != GSHSCENE_OK) {
      return r; /* unreachable: the entries were checked above */
    }
    const GSHSCENE_Color_Space * from = gshscene_color_space_is_target(&c.space) ? target_space : &c.space;
    out[i].offset = g->stops[i].offset;
    gshscene_convert_straight(from, into, c.rgba, out[i].rgba);
    out[i].rgba[3] = c.rgba[3];
  }
  if (out_space) {
    *out_space = *into;
  }
  return GSHSCENE_OK;
}

/* --- G10, G11: what draws nothing ------------------------------------------------------ */

/** G11: whether the map's 2x2 linear part has a determinant of exactly zero (the columns' exact orientation). */
int gshscene_map_singular(const GSHGEOM_Map * map) {
  GSHGEOM_Point o = {0.0, 0.0, 0.0};
  GSHGEOM_Point a = {map->m[0][0], map->m[1][0], 0.0};
  GSHGEOM_Point b = {map->m[0][1], map->m[1][1], 0.0};
  int sign = 1;
  /* A zero-filled target map is the identity (sink.h, S4). */
  return map->dim != 0 && gshgeom_orient2d(o, a, b, &sign) == GSHGEOM_OK && sign == 0;
}

/** G10: whether the gradient draws nothing wherever it is. */
int gshscene_gradient_degenerate(const GSHSCENE_Gradient * g) {
  int repeats = g->extend != GSHSCENE_EXTEND_PAD;
  switch (g->kind) {
    case GSHSCENE_GRADIENT_LINEAR: {
      if (g->d[0] == 0.0 && g->d[1] == 0.0) {
        if (g->p0[0] == g->p1[0] && g->p0[1] == g->p1[1]) {
          return 1;
        }
        break;
      }
      /* (p1 - p0) . (-d_y, d_x) is minus the determinant of the rows (p1, 1), (p0, 1), (d, 0): exact. */
      GSHGEOM_Point o = {0.0, 0.0, 0.0};
      GSHGEOM_Point b = {g->p1[0], g->p1[1], 1.0};
      GSHGEOM_Point c = {g->p0[0], g->p0[1], 1.0};
      GSHGEOM_Point d = {g->d[0], g->d[1], 0.0};
      int sign = 1;
      if (gshgeom_orient3d(o, b, c, d, &sign) == GSHGEOM_OK && sign == 0) {
        return 1;
      }
      break;
    }
    case GSHSCENE_GRADIENT_RADIAL:
      if (g->c0[0] == g->c1[0] && g->c0[1] == g->c1[1] && g->r0 == g->r1) {
        return 1;
      }
      break;
    default:
      if (repeats && g->a0 == g->a1) {
        return 1;
      }
      break;
  }
  return repeats && g->stop_count >= 2 && g->stops[0].offset == g->stops[g->stop_count - 1].offset;
}

GSHSCENE_Reason gshscene_gradient_notice(const GSHSCENE_Item * item, const GSHGEOM_Map * instance_maps,
    size_t instance_count, const GSHGEOM_Map * target_map) {
  if (!item || item->kind != GSHSCENE_ITEM_FILL || item->paint.kind != GSHSCENE_PAINT_GRADIENT || !item->paint.resource ||
      item->paint.resource->kind != GSHSCENE_RESOURCE_GRADIENT) {
    return (GSHSCENE_Reason)0;
  }
  const GSHSCENE_Gradient * g = &item->paint.resource->u.gradient.g;
  int not_invertible = (target_map && gshscene_map_singular(target_map)) || gshscene_map_singular(&item->map) ||
      gshscene_map_singular(&g->map);
  for (size_t i = 0; i < instance_count && !not_invertible; i++) {
    not_invertible = gshscene_map_singular(&instance_maps[i]);
  }
  if (not_invertible) {
    return GSHSCENE_REASON_MAP_NOT_INVERTIBLE;
  }
  return gshscene_gradient_degenerate(g) ? GSHSCENE_REASON_GRADIENT_DEGENERATE : (GSHSCENE_Reason)0;
}
