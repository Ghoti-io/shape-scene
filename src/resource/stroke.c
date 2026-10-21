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
  out->degenerate = in->degenerate;
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
