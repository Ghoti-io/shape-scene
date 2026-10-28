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
 * The stroke style resource (stroke.h, SK1 to SK3, SK12; strokes story S1): its
 * defaults, its validity and bounds, its canonical form, equality, hash, dump and
 * reading back.
 */

#include <ghoti.io/shape-scene/macros.h>

#include "../internal.h"

#include <ghoti.io/shape-geometry/stroke.h>

#include <string.h>

/** The format of a stroke style's hash and dump (resource.h). */
#define STROKE_FORMAT_VERSION 1u

#define HEX(x) ((unsigned long long)(x))

/** `-0` as `+0`: one spelling for one number. */
static double canonical_zero(double x) {
  return x == 0.0 ? 0.0 : x;
}

/** SK2: zero, or a magnitude in [GSHSCENE_STROKE_VALUE_MIN, GSHSCENE_STROKE_VALUE_MAX] (@p x finite). */
static int within(double x) {
  double m = x < 0.0 ? -x : x;
  return m == 0.0 || (m >= GSHSCENE_STROKE_VALUE_MIN && m <= GSHSCENE_STROKE_VALUE_MAX);
}

/** Whether a join reads the miter limit (SK1, SK3). */
static int join_reads_miter(uint32_t join) {
  return join == (uint32_t)GSHSCENE_STROKE_JOIN_MITER || join == (uint32_t)GSHSCENE_STROKE_JOIN_MITER_CLIP ||
      join == (uint32_t)GSHSCENE_STROKE_JOIN_ARCS;
}

void gshscene_stroke_style_init(GSHSCENE_Stroke_Style * style) {
  if (!style) {
    return;
  }
  memset(style, 0, sizeof *style);
  style->width = 1.0;
  style->width_space = GSHSCENE_STROKE_WIDTH_LOCAL;
  style->join = GSHSCENE_STROKE_JOIN_MITER;
  style->miter_limit = GSHSCENE_STROKE_MITER_LIMIT_DEFAULT;
  style->cap = GSHSCENE_STROKE_CAP_BUTT;
  style->degenerate = GSHSCENE_STROKE_DEGENERATE_DRAW;
  style->dash_scope = GSHSCENE_STROKE_DASH_PER_SUBPATH;
  style->alignment = GSHSCENE_STROKE_ALIGN_CENTER;
  style->alignment_rule = GSHGEOM_FILL_NONZERO;
}

/* --- SK2 and SK3 ------------------------------------------------------------------- */

/** SK2's validity: every check that makes the style ::GSHSCENE_ERR_INVALID, then the reserved profile. */
static GSHSCENE_Result valid(const GSHSCENE_Stroke_Style * in) {
  if (in->dash_count && !in->dashes) {
    return GSHSCENE_ERR_INVALID;
  }
  if (!gshscene_finite(in->width) || !gshscene_finite(in->miter_limit) || !gshscene_finite(in->dash_offset)) {
    return GSHSCENE_ERR_INVALID;
  }
  for (size_t i = 0; i < in->dash_count; i++) {
    if (!gshscene_finite(in->dashes[i])) {
      return GSHSCENE_ERR_INVALID;
    }
  }
  if (in->width_space >= (uint32_t)GSHSCENE_STROKE_WIDTH_SPACE_COUNT || in->join >= (uint32_t)GSHSCENE_STROKE_JOIN_COUNT ||
      in->cap >= (uint32_t)GSHSCENE_STROKE_CAP_COUNT || in->degenerate >= (uint32_t)GSHSCENE_STROKE_DEGENERATE_COUNT ||
      in->dash_scope >= (uint32_t)GSHSCENE_STROKE_DASH_SCOPE_COUNT ||
      in->alignment >= (uint32_t)GSHSCENE_STROKE_ALIGN_COUNT ||
      (unsigned)in->alignment_rule >= (unsigned)GSHGEOM_FILL_RULE_COUNT) {
    return GSHSCENE_ERR_INVALID;
  }
  if (in->width < 0.0) {
    return GSHSCENE_ERR_INVALID;
  }
  if (in->width_profile) {
    return GSHSCENE_ERR_UNSUPPORTED; /* D8: reserved until a profile is defined */
  }
  return GSHSCENE_OK;
}

/** SK2's bounds, on the style as given (each dash by magnitude). */
static int bounded(const GSHSCENE_Stroke_Style * in) {
  if (!within(in->width) || !within(in->dash_offset) ||
      !(in->miter_limit >= 1.0 && in->miter_limit <= GSHSCENE_STROKE_MITER_LIMIT_MAX)) {
    return 0;
  }
  for (size_t i = 0; i < in->dash_count; i++) {
    if (!within(in->dashes[i])) {
      return 0;
    }
  }
  return 1;
}

/** SK3 (a): how many dashes the canonical array has: 0 for a solid stroke, the given count doubled when it is odd. */
static size_t canonical_dash_count(const GSHSCENE_Stroke_Style * in) {
  int any = 0;
  for (size_t i = 0; i < in->dash_count; i++) {
    if (in->dashes[i] < 0.0) {
      return 0; /* a negative length: solid */
    }
    any = any || in->dashes[i] != 0.0;
  }
  if (!any) {
    return 0; /* none, or every length zero: solid */
  }
  return in->dash_count % 2u ? gshscene_sat_mul(in->dash_count, 2u) : in->dash_count;
}

/** SK3: the canonical form of a valid, bounded style, its dashes written to @p dashes (room for @p count). */
static void canonical(const GSHSCENE_Stroke_Style * in, size_t count, double * dashes, GSHSCENE_Stroke_Style * out) {
  memset(out, 0, sizeof *out);
  out->width = canonical_zero(in->width);
  out->width_space = in->width_space;
  out->join = in->join;
  out->miter_limit = join_reads_miter(in->join) ? in->miter_limit : GSHSCENE_STROKE_MITER_LIMIT_DEFAULT;
  out->cap = in->cap;
  /* SK3 (b): a butt cap leaves a degenerate subpath nothing to draw either way, so the field does not apply. */
  out->degenerate = in->cap == (uint32_t)GSHSCENE_STROKE_CAP_BUTT ? (uint32_t)GSHSCENE_STROKE_DEGENERATE_DRAW : in->degenerate;
  out->alignment = in->alignment;
  out->alignment_rule = in->alignment == (uint32_t)GSHSCENE_STROKE_ALIGN_CENTER ? GSHGEOM_FILL_NONZERO : in->alignment_rule;
  out->dash_count = count;
  out->dashes = count ? dashes : NULL;
  if (count) {
    for (size_t i = 0; i < count; i++) {
      dashes[i] = canonical_zero(in->dashes[i % in->dash_count]); /* an odd array repeated once */
    }
    out->dash_offset = canonical_zero(in->dash_offset);
    out->dash_scope = in->dash_scope;
  } else {
    out->dash_offset = 0.0;
    out->dash_scope = GSHSCENE_STROKE_DASH_PER_SUBPATH;
  }
  out->width_profile = NULL;
}

/* --- hash and equality ------------------------------------------------------------- */

static uint64_t content_hash(const GSHSCENE_Stroke_Style * s) {
  GSHGEOM_Hash h;
  gshgeom_hash_init(&h);
  gshgeom_hash_u32(&h, STROKE_FORMAT_VERSION);
  gshgeom_hash_u32(&h, (uint32_t)GSHSCENE_RESOURCE_STROKE_STYLE);
  gshgeom_hash_f64(&h, s->width);
  gshgeom_hash_u8(&h, (uint8_t)s->width_space);
  gshgeom_hash_u8(&h, (uint8_t)s->join);
  gshgeom_hash_f64(&h, s->miter_limit);
  gshgeom_hash_u8(&h, (uint8_t)s->cap);
  gshgeom_hash_u8(&h, (uint8_t)s->degenerate);
  gshgeom_hash_u8(&h, (uint8_t)s->alignment);
  gshgeom_hash_u8(&h, (uint8_t)s->alignment_rule);
  gshgeom_hash_u8(&h, (uint8_t)s->dash_scope);
  gshgeom_hash_f64(&h, s->dash_offset);
  gshgeom_hash_u64(&h, (uint64_t)s->dash_count);
  for (size_t i = 0; i < s->dash_count; i++) {
    gshgeom_hash_f64(&h, s->dashes[i]);
  }
  gshgeom_hash_u64(&h, s->width_profile ? gshscene_resource_hash(s->width_profile) : 0);
  return gshgeom_hash_value(&h);
}

static int same_bits(double a, double b) {
  return gshgeom_f64_bits(a) == gshgeom_f64_bits(b);
}

int gshscene_stroke_matches(const GSHSCENE_Resource * resource, const void * source) {
  const GSHSCENE_Stroke_Style * a = &resource->u.stroke.s;
  const GSHSCENE_Stroke_Style * b = (const GSHSCENE_Stroke_Style *)source;
  if (!same_bits(a->width, b->width) || a->width_space != b->width_space || a->join != b->join ||
      !same_bits(a->miter_limit, b->miter_limit) || a->cap != b->cap || a->degenerate != b->degenerate ||
      a->alignment != b->alignment || a->alignment_rule != b->alignment_rule || a->dash_scope != b->dash_scope ||
      !same_bits(a->dash_offset, b->dash_offset) || a->dash_count != b->dash_count ||
      a->width_profile != b->width_profile) {
    return 0;
  }
  for (size_t i = 0; i < a->dash_count; i++) {
    if (!same_bits(a->dashes[i], b->dashes[i])) {
      return 0;
    }
  }
  return 1;
}

/* --- build, free, intern, read --------------------------------------------------------- */

static GSHSCENE_Result build_stroke(GSHSCENE_Store * store, const void * source, GSHSCENE_Resource ** out) {
  const GSHSCENE_Stroke_Style * s = (const GSHSCENE_Stroke_Style *)source;
  GSHSCENE_Resource * resource = gshscene_store_new_resource(store, GSHSCENE_RESOURCE_STROKE_STYLE);
  if (!resource) {
    return GSHSCENE_ERR_OOM;
  }
  if (s->dash_count) {
    resource->u.stroke.dashes = gshscene_alloc(&store->alloc, s->dash_count * sizeof(double));
    if (!resource->u.stroke.dashes) {
      gshscene_store_discard_resource(resource);
      return GSHSCENE_ERR_OOM;
    }
    memcpy(resource->u.stroke.dashes, s->dashes, s->dash_count * sizeof(double));
  }
  resource->u.stroke.s = *s;
  resource->u.stroke.s.dashes = resource->u.stroke.dashes;
  *out = resource;
  return GSHSCENE_OK;
}

void gshscene_stroke_free(GSHSCENE_Resource * resource) {
  gshscene_free(&resource->store->alloc, resource->u.stroke.dashes);
}

GSHSCENE_Result gshscene_store_intern_stroke_style(
    GSHSCENE_Store * store, const GSHSCENE_Stroke_Style * style, const GSHSCENE_Resource ** out) {
  if (!store || !style || !out) {
    return GSHSCENE_ERR_INVALID;
  }
  GSHSCENE_Result result = valid(style);
  if (result != GSHSCENE_OK) {
    return result;
  }
  /* SK2's bounds, after every other check; the count is the canonical one (an odd array doubled). */
  size_t count = canonical_dash_count(style);
  size_t most = store->limits.max_resource_elements < GSHSCENE_STROKE_DASHES_MAX ? store->limits.max_resource_elements
                                                                                 : GSHSCENE_STROKE_DASHES_MAX;
  if (!bounded(style) || count > most) {
    return GSHSCENE_ERR_LIMIT;
  }
  double * dashes = NULL;
  if (count) {
    dashes = gshscene_alloc(&store->alloc, count * sizeof *dashes);
    if (!dashes) {
      return GSHSCENE_ERR_OOM;
    }
  }
  GSHSCENE_Stroke_Style s;
  canonical(style, count, dashes, &s);
  GSHSCENE_Probe probe = {GSHSCENE_RESOURCE_STROKE_STYLE, content_hash(&s),
      gshscene_sat_add(GSHSCENE_BYTES_BASE, gshscene_sat_mul(count, GSHSCENE_BYTES_PER_DASH)), &s, build_stroke};
  result = gshscene_store_intern(store, &probe, out);
  gshscene_free(&store->alloc, dashes);
  return result;
}

GSHSCENE_Result gshscene_resource_stroke_style(const GSHSCENE_Resource * resource, GSHSCENE_Stroke_Style * out) {
  if (!resource || !out || resource->kind != GSHSCENE_RESOURCE_STROKE_STYLE) {
    return GSHSCENE_ERR_INVALID;
  }
  *out = resource->u.stroke.s;
  return GSHSCENE_OK;
}

/*
 * The conversion to shape-geometry's parameters, row by row (strokes story S4, SC2).  The two libraries state the
 * enumerations separately, so each pair is asserted equal here, at compile time, where both are visible.
 */
_Static_assert((int)GSHSCENE_STROKE_WIDTH_LOCAL == (int)GSHGEOM_STROKE_WIDTH_LOCAL &&
        (int)GSHSCENE_STROKE_WIDTH_DEVICE == (int)GSHGEOM_STROKE_WIDTH_DEVICE &&
        (int)GSHSCENE_STROKE_WIDTH_SPACE_COUNT == (int)GSHGEOM_STROKE_WIDTH_SPACE_COUNT,
    "width space");
_Static_assert((int)GSHSCENE_STROKE_JOIN_MITER == (int)GSHGEOM_STROKE_JOIN_MITER &&
        (int)GSHSCENE_STROKE_JOIN_ROUND == (int)GSHGEOM_STROKE_JOIN_ROUND &&
        (int)GSHSCENE_STROKE_JOIN_BEVEL == (int)GSHGEOM_STROKE_JOIN_BEVEL &&
        (int)GSHSCENE_STROKE_JOIN_MITER_CLIP == (int)GSHGEOM_STROKE_JOIN_MITER_CLIP &&
        (int)GSHSCENE_STROKE_JOIN_ARCS == (int)GSHGEOM_STROKE_JOIN_ARCS &&
        (int)GSHSCENE_STROKE_JOIN_COUNT == (int)GSHGEOM_STROKE_JOIN_COUNT,
    "join");
_Static_assert((int)GSHSCENE_STROKE_CAP_BUTT == (int)GSHGEOM_STROKE_CAP_BUTT &&
        (int)GSHSCENE_STROKE_CAP_ROUND == (int)GSHGEOM_STROKE_CAP_ROUND &&
        (int)GSHSCENE_STROKE_CAP_SQUARE == (int)GSHGEOM_STROKE_CAP_SQUARE &&
        (int)GSHSCENE_STROKE_CAP_COUNT == (int)GSHGEOM_STROKE_CAP_COUNT,
    "cap");
_Static_assert((int)GSHSCENE_STROKE_DEGENERATE_DRAW == (int)GSHGEOM_STROKE_DEGENERATE_DRAW &&
        (int)GSHSCENE_STROKE_DEGENERATE_OMIT == (int)GSHGEOM_STROKE_DEGENERATE_OMIT &&
        (int)GSHSCENE_STROKE_DEGENERATE_COUNT == (int)GSHGEOM_STROKE_DEGENERATE_COUNT,
    "degenerate");
_Static_assert((int)GSHSCENE_STROKE_DASH_PER_SUBPATH == (int)GSHGEOM_STROKE_DASH_PER_SUBPATH &&
        (int)GSHSCENE_STROKE_DASH_PATH == (int)GSHGEOM_STROKE_DASH_PATH &&
        (int)GSHSCENE_STROKE_DASH_SCOPE_COUNT == (int)GSHGEOM_STROKE_DASH_SCOPE_COUNT,
    "dash scope");
_Static_assert((int)GSHSCENE_STROKE_ALIGN_CENTER == (int)GSHGEOM_STROKE_ALIGN_CENTER &&
        (int)GSHSCENE_STROKE_ALIGN_INSIDE == (int)GSHGEOM_STROKE_ALIGN_INSIDE &&
        (int)GSHSCENE_STROKE_ALIGN_OUTSIDE == (int)GSHGEOM_STROKE_ALIGN_OUTSIDE &&
        (int)GSHSCENE_STROKE_ALIGN_COUNT == (int)GSHGEOM_STROKE_ALIGN_COUNT,
    "alignment");
_Static_assert(GSHSCENE_STROKE_DASHES_MAX == GSHGEOM_STROKE_DASHES_MAX, "dashes");

GSHSCENE_Result gshscene_resource_stroke_geometry(const GSHSCENE_Resource * resource, GSHGEOM_Stroke * out) {
  if (!resource || !out || resource->kind != GSHSCENE_RESOURCE_STROKE_STYLE) {
    return GSHSCENE_ERR_INVALID;
  }
  const GSHSCENE_Stroke_Style * s = &resource->u.stroke.s;
  GSHGEOM_Stroke g;
  gshgeom_stroke_init(&g); /* the _reserved bytes zero, and no field left to chance */
  g.width = s->width;
  g.width_space = s->width_space;
  g.join = s->join;
  g.miter_limit = s->miter_limit;
  g.cap = s->cap;
  g.degenerate = s->degenerate;
  g.dash_count = s->dash_count;
  g.dashes = s->dashes; /* into the resource: valid while a handle is held (S10) */
  g.dash_offset = s->dash_offset;
  g.dash_scope = s->dash_scope;
  g.alignment = s->alignment;
  g.alignment_rule = s->alignment_rule;
  g.width_profile = NULL; /* reserved: a style with one is not interned (D8) */
  if (gshgeom_stroke_check(&g) != GSHGEOM_OK) {
    return GSHSCENE_ERR_INTERNAL; /* SK2 equals SG2: a style the store holds passes */
  }
  *out = g;
  return GSHSCENE_OK;
}

/* --- dump ---------------------------------------------------------------------------- */

int gshscene_stroke_dump(const GSHSCENE_Resource * resource, FILE * out) {
  const GSHSCENE_Stroke_Style * s = &resource->u.stroke.s;
  if (fprintf(out,
          "stroke width=%016llx space=%u join=%u miter=%016llx cap=%u degenerate=%u align=%u rule=%u scope=%u "
          "offset=%016llx dashes=%llu profile=%016llx\n",
          HEX(gshgeom_f64_bits(s->width)), (unsigned)s->width_space, (unsigned)s->join,
          HEX(gshgeom_f64_bits(s->miter_limit)), (unsigned)s->cap, (unsigned)s->degenerate, (unsigned)s->alignment,
          (unsigned)s->alignment_rule, (unsigned)s->dash_scope, HEX(gshgeom_f64_bits(s->dash_offset)),
          HEX(s->dash_count), HEX(s->width_profile ? gshscene_resource_hash(s->width_profile) : 0)) < 0) {
    return -1;
  }
  for (size_t i = 0; i < s->dash_count; i++) {
    if (fprintf(out, "d %016llx\n", HEX(gshgeom_f64_bits(s->dashes[i]))) < 0) {
      return -1;
    }
  }
  return 0;
}

/* --- SK5 and SK10: the device pixel and the bounds --------------------------------------- */

double gshscene_stroke_pixel_size(uint32_t unit) {
  switch (unit) {
    case GSHSCENE_UNIT_DEFAULT:
    case GSHSCENE_UNIT_PIXEL:
      return 1.0;
    case GSHSCENE_UNIT_MILLIMETER:
      return 25.4 / 96.0; /* the CSS reference pixel, 1/96 inch (K8) */
    case GSHSCENE_UNIT_POINT:
      return 72.0 / 96.0;
    case GSHSCENE_UNIT_INCH:
      return 1.0 / 96.0;
    default:
      return 0.0;
  }
}

/**
 * SK10's cover of the roundings: `h` is formed by at most five correctly rounded steps (the sum `miter_limit + 1`,
 * two products, the unit's constant and the product by it), each within `2^-53` of its real value, so their product is
 * at least `(1 - 2^-53)^5` times the real `h`; one more product by `1 + 2^-50` (itself rounded) lifts it above.
 */
#define STROKE_ROUND_UP (1.0 + 0x1p-50)

/** The next double above @p x (finite). */
static double next_up(double x) {
  if (x == 0.0) {
    return 0x1p-1074; /* the smallest subnormal, above both zeros */
  }
  uint64_t bits = gshgeom_f64_bits(x);
  bits = x > 0.0 ? bits + 1u : bits - 1u;
  double y;
  memcpy(&y, &bits, sizeof y);
  return y;
}

/** `a + b` rounded upward: the nearest sum, raised one double when the exact sum (TwoSum's error) is above it. */
static double add_up(double a, double b) {
  double s = a + b;
  double bb = s - a;
  double e = (a - (s - bb)) + (b - bb);
  return e > 0.0 ? next_up(s) : s;
}

/** `a - b` rounded downward. */
static double sub_down(double a, double b) {
  return -add_up(-a, b);
}

/** SK10: `h` for @p s, in units of @p pixel per width unit, never below the real one. */
static double reach(const GSHSCENE_Stroke_Style * s, double pixel) {
  double f = 1.0;
  if (s->join == (uint32_t)GSHSCENE_STROKE_JOIN_MITER) {
    f = s->miter_limit; /* the miter's tip: 1 / sin(θ/2) half widths from its vertex, at most the limit */
  } else if (s->join == (uint32_t)GSHSCENE_STROKE_JOIN_MITER_CLIP || s->join == (uint32_t)GSHSCENE_STROKE_JOIN_ARCS) {
    f = s->miter_limit + 1.0; /* a clipped corner: the limit along the bisector, up to one half width across */
  }
  if (s->cap == (uint32_t)GSHSCENE_STROKE_CAP_SQUARE && f < 1.5) {
    f = 1.5; /* a square cap's corner, sqrt 2 half widths from the end */
  }
  double a = s->alignment == (uint32_t)GSHSCENE_STROKE_ALIGN_CENTER ? 1.0 : 2.0;
  return (s->width * 0.5) * f * a * pixel * STROKE_ROUND_UP;
}

GSHSCENE_Result gshscene_stroke_bounds(const GSHSCENE_Resource * style, const GSHGEOM_Bounds * centerline,
    const GSHGEOM_Map * composed, uint32_t unit, GSHGEOM_Bounds * out) {
  double pixel = gshscene_stroke_pixel_size(unit);
  if (!style || style->kind != GSHSCENE_RESOURCE_STROKE_STYLE || !centerline || !composed || !out || pixel == 0.0 ||
      composed->dim != 2 || gshgeom_map_check(composed) != GSHGEOM_OK) {
    return GSHSCENE_ERR_INVALID;
  }
  const GSHSCENE_Stroke_Style * s = &style->u.stroke.s;
  GSHGEOM_Bounds b;
  memset(&b, 0, sizeof b);
  b.empty = 1;
  if (centerline->empty || s->width == 0.0) {
    *out = b; /* nothing to stroke, or SK8: width 0 covers nothing */
    return GSHSCENE_OK;
  }
  int device = s->width_space == (uint32_t)GSHSCENE_STROKE_WIDTH_DEVICE;
  double h = reach(s, device ? pixel : 1.0);
  double x0 = centerline->min.x;
  double y0 = centerline->min.y;
  double x1 = centerline->max.x;
  double y1 = centerline->max.y;
  if (!device) {
    /* Local: the box outset in model space, then mapped. */
    x0 = sub_down(x0, h);
    y0 = sub_down(y0, h);
    x1 = add_up(x1, h);
    y1 = add_up(y1, h);
  }
  const double xs[2] = {x0, x1};
  const double ys[2] = {y0, y1};
  for (int i = 0; i < 2; i++) {
    for (int j = 0; j < 2; j++) {
      GSHGEOM_Point q;
      if (gshgeom_map_apply_point(composed, (GSHGEOM_Point){xs[i], ys[j], 0.0}, &q) != GSHGEOM_OK) {
        return GSHSCENE_ERR_LIMIT;
      }
      if (b.empty) {
        b.min = q;
        b.max = q;
        b.empty = 0;
      } else {
        b.min.x = q.x < b.min.x ? q.x : b.min.x;
        b.min.y = q.y < b.min.y ? q.y : b.min.y;
        b.max.x = q.x > b.max.x ? q.x : b.max.x;
        b.max.y = q.y > b.max.y ? q.y : b.max.y;
      }
    }
  }
  if (device) {
    /* Device: the mapped box outset in target units. */
    b.min.x = sub_down(b.min.x, h);
    b.min.y = sub_down(b.min.y, h);
    b.max.x = add_up(b.max.x, h);
    b.max.y = add_up(b.max.y, h);
  }
  if (!gshscene_finite(b.min.x) || !gshscene_finite(b.min.y) || !gshscene_finite(b.max.x) || !gshscene_finite(b.max.y)) {
    return GSHSCENE_ERR_LIMIT;
  }
  b.min.z = b.max.z = 0.0;
  *out = b;
  return GSHSCENE_OK;
}

/* --- a centerline's box (SK10, for the damage) ---------------------------------------------- */

GSHSCENE_Result gshscene_centerline_box(const GSHSCENE_Resource * resource, GSHGEOM_Bounds * out) {
  GSHGEOM_Bounds box;
  memset(&box, 0, sizeof box);
  box.empty = 1;
  const GSHGEOM_Path * path = gshscene_resource_path(resource);
  if (path) {
    if (gshgeom_path_dimension(path) != 2 || gshgeom_path_bounds(path, &box) != GSHGEOM_OK) {
      return GSHSCENE_ERR_UNSUPPORTED;
    }
    *out = box;
    return GSHSCENE_OK;
  }
  GSHSCENE_Glyph_Run run;
  if (gshscene_resource_glyph_run(resource, &run) != GSHSCENE_OK) {
    return GSHSCENE_ERR_UNSUPPORTED;
  }
  GSHGEOM_Map identity;
  gshgeom_map_identity(2, &identity);
  for (size_t i = 0; i < run.glyph_count; i++) {
    double gx0, gy0, gx1, gy1;
    GSHGEOM_Map map;
    if (run.source == GSHSCENE_GLYPHS_FROM_FONT) {
      /* SK7: the general route, each glyph's design box under its map into model space; an unbound face has none. */
      GSHSCENE_Glyph_Box g = {0, 0, 0, 0, 0};
      if (gshscene_glyph_box(resource, i, &g) != GSHSCENE_OK || gshscene_glyph_map(resource, &identity, i, &map) != GSHSCENE_OK) {
        return GSHSCENE_ERR_UNSUPPORTED;
      }
      if (g.empty) {
        continue;
      }
      gx0 = (double)g.x_min / 64.0;
      gy0 = (double)g.y_min / 64.0;
      gx1 = (double)g.x_max / 64.0;
      gy1 = (double)g.y_max / 64.0;
    } else {
      GSHGEOM_Bounds ob;
      const GSHGEOM_Path * outline = gshscene_resource_path(run.glyphs[i].outline);
      if (!outline || gshgeom_path_dimension(outline) != 2 || gshgeom_path_bounds(outline, &ob) != GSHGEOM_OK) {
        return GSHSCENE_ERR_UNSUPPORTED;
      }
      if (ob.empty) {
        continue;
      }
      gx0 = ob.min.x;
      gy0 = ob.min.y;
      gx1 = ob.max.x;
      gy1 = ob.max.y;
      gshgeom_map_translate(2, run.glyphs[i].x, run.glyphs[i].y, 0.0, &map);
    }
    const double xs[2] = {gx0, gx1};
    const double ys[2] = {gy0, gy1};
    for (int a = 0; a < 2; a++) {
      for (int c = 0; c < 2; c++) {
        GSHGEOM_Point q;
        if (gshgeom_map_apply_point(&map, (GSHGEOM_Point){xs[a], ys[c], 0.0}, &q) != GSHGEOM_OK) {
          return GSHSCENE_ERR_UNSUPPORTED;
        }
        if (box.empty) {
          box.min = q;
          box.max = q;
          box.empty = 0;
        } else {
          box.min.x = q.x < box.min.x ? q.x : box.min.x;
          box.min.y = q.y < box.min.y ? q.y : box.min.y;
          box.max.x = q.x > box.max.x ? q.x : box.max.x;
          box.max.y = q.y > box.max.y ? q.y : box.max.y;
        }
      }
    }
  }
  *out = box;
  return GSHSCENE_OK;
}

/* --- SK8, SK9: what draws nothing ----------------------------------------------------------- */

/** The gradient a stroke paints with, or NULL. */
static const GSHSCENE_Resource * paint_gradient(const GSHSCENE_Item * item) {
  const GSHSCENE_Resource * g = item->paint.kind == GSHSCENE_PAINT_GRADIENT ? item->paint.resource : NULL;
  return g && g->kind == GSHSCENE_RESOURCE_GRADIENT ? g : NULL;
}

/** Whether @p item is a stroke whose style is one, with its instance maps given when counted. */
static int stroke_item(const GSHSCENE_Item * item, const GSHGEOM_Map * instance_maps, size_t instance_count) {
  return item && item->kind == GSHSCENE_ITEM_STROKE && item->style &&
      item->style->kind == GSHSCENE_RESOURCE_STROKE_STYLE && (instance_count == 0 || instance_maps);
}

static GSHGEOM_Result compose_item(const GSHGEOM_Map * instance_maps, size_t instance_count, const GSHGEOM_Map * item_map,
    const GSHGEOM_Map * target_map, GSHGEOM_Map * out);

GSHSCENE_Reason gshscene_stroke_notice(const GSHSCENE_Item * item, const GSHGEOM_Map * instance_maps,
    size_t instance_count, const GSHGEOM_Map * target_map) {
  if (!stroke_item(item, instance_maps, instance_count)) {
    return (GSHSCENE_Reason)0;
  }
  const GSHSCENE_Resource * gradient = paint_gradient(item);
  const GSHSCENE_Resource * image_paint = item->paint.kind == GSHSCENE_PAINT_IMAGE && item->paint.resource &&
          item->paint.resource->kind == GSHSCENE_RESOURCE_IMAGE_PAINT ? item->paint.resource : NULL;
  /* SK9: G11's condition on each map of the chain, for both width spaces ... */
  int not_invertible = (target_map && gshscene_map_singular(target_map)) || gshscene_map_singular(&item->map) ||
      (gradient && gshscene_map_singular(&gradient->u.gradient.g.map)) || gshscene_image_paint_singular(image_paint);
  for (size_t i = 0; i < instance_count && !not_invertible; i++) {
    not_invertible = gshscene_map_singular(&instance_maps[i]);
  }
  /* ... or the composed map's: invertible factors can compose (by underflow) into a map of determinant exactly zero,
   * which the expansion would meet as SG15 (strokes story S4, E6).  A map that cannot be formed is not decided here:
   * it is the sink's own limit, as a fill's is. */
  if (!not_invertible) {
    GSHGEOM_Map composed;
    int invertible = 1;
    memset(&composed, 0, sizeof composed);
    not_invertible = compose_item(instance_maps, instance_count, &item->map, target_map, &composed) == GSHGEOM_OK &&
        gshgeom_stroke_map_invertible(&composed, &invertible) == GSHGEOM_OK && !invertible;
  }
  if (not_invertible) {
    return GSHSCENE_REASON_MAP_NOT_INVERTIBLE;
  }
  if (item->style->u.stroke.s.width == 0.0) {
    return GSHSCENE_REASON_STROKE_ZERO_WIDTH; /* SK8 */
  }
  return gradient && gshscene_gradient_degenerate(&gradient->u.gradient.g) ? GSHSCENE_REASON_GRADIENT_DEGENERATE
                                                                         : (GSHSCENE_Reason)0;
}

/* --- SK11: the features a stroke requires ---------------------------------------------------- */

/**
 * The composed map as `gshgeom_map_compose_chain` forms it for the chain (the instances' maps root to leaf, then the
 * item's) and the target, one composition at a time and in the same order, so the result is the same bits whatever
 * the chain's length.  A zero-filled target map is the identity (S4).
 */
static GSHGEOM_Result compose_item(const GSHGEOM_Map * instance_maps, size_t instance_count, const GSHGEOM_Map * item_map,
    const GSHGEOM_Map * target_map, GSHGEOM_Map * out) {
  GSHGEOM_Map acc = instance_count ? instance_maps[0] : *item_map;
  GSHGEOM_Result r = gshgeom_map_check(&acc);
  for (size_t i = 1; r == GSHGEOM_OK && i <= instance_count; i++) {
    r = gshgeom_map_compose(&acc, i < instance_count ? &instance_maps[i] : item_map, &acc);
  }
  if (r == GSHGEOM_OK && target_map && target_map->dim != 0) {
    r = gshgeom_map_compose(target_map, &acc, &acc);
  }
  if (r == GSHGEOM_OK) {
    *out = acc;
  }
  return r;
}

/** Whether a 2D map's linear part is a uniform scale times an orthogonal matrix, exactly: `[a b; -b a]` or `[a b; b -a]`. */
static int similarity(const GSHGEOM_Map * m) {
  double a = m->m[0][0];
  double b = m->m[0][1];
  double c = m->m[1][0];
  double d = m->m[1][1];
  return (a == d && b == -c) || (a == -d && b == c);
}

uint32_t gshscene_stroke_required(const GSHSCENE_Item * item, const GSHGEOM_Map * instance_maps, size_t instance_count,
    const GSHGEOM_Map * target_map) {
  if (!stroke_item(item, instance_maps, instance_count) ||
      gshscene_stroke_notice(item, instance_maps, instance_count, target_map)) {
    return 0; /* not a stroke, or one that draws nothing (SK8, SK9) */
  }
  const GSHSCENE_Stroke_Style * s = &item->style->u.stroke.s;
  static const uint32_t joins[GSHSCENE_STROKE_JOIN_COUNT] = {GSHSCENE_STROKE_NATIVE_JOIN_MITER,
      GSHSCENE_STROKE_NATIVE_JOIN_ROUND, GSHSCENE_STROKE_NATIVE_JOIN_BEVEL, GSHSCENE_STROKE_NATIVE_JOIN_MITER_CLIP,
      GSHSCENE_STROKE_NATIVE_JOIN_ARCS};
  static const uint32_t caps[GSHSCENE_STROKE_CAP_COUNT] = {GSHSCENE_STROKE_NATIVE_CAP_BUTT,
      GSHSCENE_STROKE_NATIVE_CAP_ROUND, GSHSCENE_STROKE_NATIVE_CAP_SQUARE};
  /* The join and the cap always: whether the centerline has a vertex or an end is not decided from the style. */
  uint32_t need = joins[s->join] | caps[s->cap];
  if (s->width_space == (uint32_t)GSHSCENE_STROKE_WIDTH_DEVICE) {
    need |= GSHSCENE_STROKE_NATIVE_WIDTH_DEVICE;
  } else {
    need |= GSHSCENE_STROKE_NATIVE_WIDTH_LOCAL;
    GSHGEOM_Map composed;
    memset(&composed, 0, sizeof composed); /* read only once compose_item has written it */
    if (compose_item(instance_maps, instance_count, &item->map, target_map, &composed) != GSHGEOM_OK ||
        !similarity(&composed)) {
      need |= GSHSCENE_STROKE_NATIVE_WIDTH_LOCAL_ANISOTROPIC; /* an elliptical pen, or a map that cannot be formed */
    }
  }
  if (s->dash_count) {
    need |= GSHSCENE_STROKE_NATIVE_DASH;
    if (s->dash_scope == (uint32_t)GSHSCENE_STROKE_DASH_PATH) {
      need |= GSHSCENE_STROKE_NATIVE_DASH_PATH_SCOPE;
    }
  }
  if (s->alignment != (uint32_t)GSHSCENE_STROKE_ALIGN_CENTER) {
    need |= GSHSCENE_STROKE_NATIVE_ALIGN;
    if (s->alignment_rule == GSHGEOM_FILL_EVEN_ODD) {
      need |= GSHSCENE_STROKE_NATIVE_ALIGN_EVEN_ODD;
    }
  }
  if (s->degenerate == (uint32_t)GSHSCENE_STROKE_DEGENERATE_DRAW && s->cap != (uint32_t)GSHSCENE_STROKE_CAP_BUTT) {
    need |= GSHSCENE_STROKE_NATIVE_DEGENERATE_DRAW; /* with a butt cap a degenerate subpath draws nothing either way */
  }
  return need;
}
