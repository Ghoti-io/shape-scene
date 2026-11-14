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
 * The width profile resource (stroke.h, SW1 to SW9): stops of a position and a factor, which a stroke style names to make its width
 * vary along each subpath.  Its validity, bounds, canonical form (a `-0` is a `+0`), equality, hash, dump and reading back.
 */

#include <ghoti.io/shape-scene/macros.h>

#include "../internal.h"

#include <ghoti.io/shape-geometry/stroke.h>

#include <string.h>

/** The format of a width profile's hash and dump. */
#define PROFILE_FORMAT_VERSION 1u

#define HEX(x) ((unsigned long long)(x))

static double canonical_zero(double x) {
  return x == 0.0 ? 0.0 : x;
}

/** Zero, or a magnitude in [2^-64, 2^64] (SW1's band, SK2's). */
static int within(double x) {
  double m = x < 0.0 ? -x : x;
  return m == 0.0 || (m >= GSHSCENE_STROKE_VALUE_MIN && m <= GSHSCENE_STROKE_VALUE_MAX);
}

static uint64_t content_hash(size_t count, const GSHSCENE_Width_Stop * stops) {
  GSHGEOM_Hash h;
  gshgeom_hash_init(&h);
  gshgeom_hash_u32(&h, PROFILE_FORMAT_VERSION);
  gshgeom_hash_u32(&h, (uint32_t)GSHSCENE_RESOURCE_WIDTH_PROFILE);
  gshgeom_hash_u64(&h, (uint64_t)count);
  for (size_t i = 0; i < count; i++) {
    gshgeom_hash_f64(&h, canonical_zero(stops[i].position));
    gshgeom_hash_f64(&h, canonical_zero(stops[i].factor));
  }
  return gshgeom_hash_value(&h);
}

typedef struct Source {
  size_t count;
  const GSHSCENE_Width_Stop * stops;
} Source;

int gshscene_width_profile_matches(const GSHSCENE_Resource * resource, const void * source) {
  const Source * want = (const Source *)source;
  if (resource->u.profile.geom.count != want->count) {
    return 0;
  }
  for (size_t i = 0; i < want->count; i++) {
    if (gshgeom_f64_bits(resource->u.profile.stops[i].position) != gshgeom_f64_bits(canonical_zero(want->stops[i].position)) ||
        gshgeom_f64_bits(resource->u.profile.stops[i].factor) != gshgeom_f64_bits(canonical_zero(want->stops[i].factor))) {
      return 0;
    }
  }
  return 1;
}

static GSHSCENE_Result build(GSHSCENE_Store * store, const void * source, GSHSCENE_Resource ** out) {
  const Source * src = (const Source *)source;
  GSHSCENE_Resource * resource = gshscene_store_new_resource(store, GSHSCENE_RESOURCE_WIDTH_PROFILE);
  if (!resource) {
    return GSHSCENE_ERR_OOM;
  }
  resource->u.profile.stops = gshscene_alloc(&store->alloc, src->count * sizeof *resource->u.profile.stops);
  if (!resource->u.profile.stops) {
    gshscene_store_discard_resource(resource);
    return GSHSCENE_ERR_OOM;
  }
  double most = 0.0;
  for (size_t i = 0; i < src->count; i++) {
    resource->u.profile.stops[i].position = canonical_zero(src->stops[i].position);
    resource->u.profile.stops[i].factor = canonical_zero(src->stops[i].factor);
    most = resource->u.profile.stops[i].factor > most ? resource->u.profile.stops[i].factor : most;
  }
  resource->u.profile.geom.count = src->count;
  resource->u.profile.geom.stops = resource->u.profile.stops;
  resource->u.profile.max_factor = most;
  *out = resource;
  return GSHSCENE_OK;
}

void gshscene_width_profile_free(GSHSCENE_Resource * resource) {
  gshscene_free(&resource->store->alloc, resource->u.profile.stops);
}

GSHSCENE_Result gshscene_store_intern_width_profile(
    GSHSCENE_Store * store, const GSHSCENE_Width_Stop * stops, size_t count, const GSHSCENE_Resource ** out) {
  if (!store || !out || count == 0 || !stops) {
    return GSHSCENE_ERR_INVALID;
  }
  /* SW1: every stop's validity in order (INVALID), then the bounds (LIMIT). */
  double last = 0.0;
  for (size_t i = 0; i < count; i++) {
    if (!gshscene_finite(stops[i].position) || !gshscene_finite(stops[i].factor) || stops[i].position < 0.0 ||
        stops[i].factor < 0.0 || stops[i].position < last) {
      return GSHSCENE_ERR_INVALID;
    }
    last = stops[i].position;
  }
  const size_t most = store->limits.max_resource_elements < GSHGEOM_WIDTH_STOPS_MAX ? store->limits.max_resource_elements
                                                                                    : GSHGEOM_WIDTH_STOPS_MAX;
  if (count > most) {
    return GSHSCENE_ERR_LIMIT;
  }
  for (size_t i = 0; i < count; i++) {
    if (!within(stops[i].position) || !within(stops[i].factor) || stops[i].factor > GSHGEOM_WIDTH_FACTOR_MAX) {
      return GSHSCENE_ERR_LIMIT;
    }
  }
  Source src = {count, stops};
  GSHSCENE_Probe probe = {GSHSCENE_RESOURCE_WIDTH_PROFILE, content_hash(count, stops),
      gshscene_sat_add(GSHSCENE_BYTES_BASE, gshscene_sat_mul(count, GSHSCENE_BYTES_PER_WIDTH_STOP)), &src, build};
  return gshscene_store_intern(store, &probe, out);
}

GSHSCENE_Result gshscene_resource_width_profile(
    const GSHSCENE_Resource * resource, const GSHSCENE_Width_Stop ** stops, size_t * count) {
  if (!resource || !stops || !count || resource->kind != GSHSCENE_RESOURCE_WIDTH_PROFILE) {
    return GSHSCENE_ERR_INVALID;
  }
  *stops = resource->u.profile.stops;
  *count = resource->u.profile.geom.count;
  return GSHSCENE_OK;
}

int gshscene_width_profile_dump(const GSHSCENE_Resource * resource, FILE * out) {
  if (fprintf(out, "stops %llu\n", HEX(resource->u.profile.geom.count)) < 0) {
    return -1;
  }
  for (size_t i = 0; i < resource->u.profile.geom.count; i++) {
    if (fprintf(out, "s %016llx %016llx\n", HEX(gshgeom_f64_bits(resource->u.profile.stops[i].position)),
            HEX(gshgeom_f64_bits(resource->u.profile.stops[i].factor))) < 0) {
      return -1;
    }
  }
  return 0;
}
