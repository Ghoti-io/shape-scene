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
 * The pattern (pattern.h, PT1 to PT14): the resource, its canonical form, hash and dump, the exact stepping of the
 * lattice (PT5), the notice predicate (PT10) and the walk into a tile that a sink's disposition of a unit needs (PT13).
 */

#include <ghoti.io/shape-scene/macros.h>

#include "../internal.h"
#include "../sink/sink_internal.h"

#include <string.h>

/** The format of a pattern's hash and dump (resource.h). */
#define PATTERN_FORMAT_VERSION 1u

#define HEX(x) ((unsigned long long)(x))

void gshscene_pattern_init(GSHSCENE_Pattern * pattern) {
  if (!pattern) {
    return;
  }
  memset(pattern, 0, sizeof *pattern);
  pattern->w = 1.0;
  pattern->h = 1.0;
  pattern->x_step = 1.0;
  pattern->y_step = 1.0;
  pattern->repeat_x = 1;
  pattern->repeat_y = 1;
  gshgeom_map_identity(2, &pattern->map);
}

/* --- the canonical form (PT2) ------------------------------------------------------------ */

/** A value within G14's band: zero, or a magnitude in `[2^-64, 2^64]`. */
static int in_band(double x) {
  double m = x < 0.0 ? -x : x;
  return m == 0.0 || (m >= GSHSCENE_GRADIENT_VALUE_MIN && m <= GSHSCENE_GRADIENT_VALUE_MAX);
}

static int map_in_band(const GSHGEOM_Map * map) {
  for (int i = 0; i < 2; i++) {
    for (int j = 0; j < 2; j++) {
      if (!in_band(map->m[i][j])) {
        return 0;
      }
    }
    if (!in_band(map->t[i])) {
      return 0;
    }
  }
  return 1;
}

/** `-0` as `+0`: one spelling for one number. */
static double plus_zero(double x) {
  return x == 0.0 ? 0.0 : x;
}

/** Whether @p paint is the zero paint: what a colored pattern stores in place of a tint. */
static int paint_is_zero(const GSHSCENE_Paint * paint) {
  return paint->kind == GSHSCENE_PAINT_SOLID && gshgeom_f64_bits(paint->rgba[0]) == 0 &&
      gshgeom_f64_bits(paint->rgba[1]) == 0 && gshgeom_f64_bits(paint->rgba[2]) == 0 &&
      gshgeom_f64_bits(paint->rgba[3]) == 0 && gshscene_color_space_is_target(&paint->space) && paint->resource == NULL &&
      paint->entry == 0;
}

/** What an intern carries: the canonical content (its map and tint in place). */
typedef struct Source {
  GSHSCENE_Pattern p;
} Source;

/**
 * PT2: the content of @p in as the store keeps it, or the result of its refusal.  Every validity check comes first, in the
 * order the header states; then a 3D map or a reserved space (`UNSUPPORTED`); then the bounds (`LIMIT`).
 */
static GSHSCENE_Result canonical(const GSHSCENE_Store * store, const GSHSCENE_Pattern * in, GSHSCENE_Pattern * out) {
  const GSHSCENE_Resource * tile = in->tile;
  const double scalars[6] = {in->x, in->y, in->w, in->h, in->x_step, in->y_step};
  if (!tile || tile->kind != GSHSCENE_RESOURCE_PICTURE || tile->store != store) {
    return GSHSCENE_ERR_INVALID;
  }
  for (int i = 0; i < 6; i++) {
    if (!gshscene_finite(scalars[i])) {
      return GSHSCENE_ERR_INVALID;
    }
  }
  if (in->repeat_x > 1u || in->repeat_y > 1u || in->has_tint > 1u || in->overflow > 1u) {
    return GSHSCENE_ERR_INVALID;
  }
  if (in->w < 0.0 || in->h < 0.0 || in->x_step < 0.0 || in->y_step < 0.0) {
    return GSHSCENE_ERR_INVALID;
  }
  GSHGEOM_Map map;
  if (gshscene_list_canonical_map(&in->map, &map) != GSHSCENE_OK || (map.dim != 2 && map.dim != 3)) {
    return GSHSCENE_ERR_INVALID;
  }
  GSHSCENE_Paint tint;
  GSHSCENE_Result tint_result = GSHSCENE_OK;
  memset(&tint, 0, sizeof tint);
  if (in->has_tint) {
    if (in->tint.kind != GSHSCENE_PAINT_SOLID && in->tint.kind != GSHSCENE_PAINT_FOREGROUND &&
        in->tint.kind != GSHSCENE_PAINT_PALETTE) {
      return GSHSCENE_ERR_INVALID;
    }
    tint_result = gshscene_list_canonical_paint_valid(store, &in->tint, &tint);
    if (tint_result == GSHSCENE_ERR_INVALID) {
      return tint_result;
    }
  } else if (!paint_is_zero(&in->tint)) {
    return GSHSCENE_ERR_INVALID; /* a tint beside a colored pattern */
  }
  if (map.dim == 3 || tint_result != GSHSCENE_OK) {
    return tint_result != GSHSCENE_OK ? tint_result : GSHSCENE_ERR_UNSUPPORTED;
  }
  /* One spelling (PT3): a step on an axis that does not repeat is never read (the one copy has no neighbour), so a field that
   * cannot matter must not split a handle: the store keeps the size there, the step that abuts the cell. */
  const double x_step = in->repeat_x ? in->x_step : in->w;
  const double y_step = in->repeat_y ? in->y_step : in->h;
  /* The bounds, after every validity check (PT2, G14, N1). */
  if (!in_band(in->x) || !in_band(in->y) || !in_band(in->w) || !in_band(in->h) || !in_band(x_step) ||
      !in_band(y_step) || !map_in_band(&map) ||
      (in->has_tint && tint.kind == GSHSCENE_PAINT_SOLID && !gshscene_color_bounded(tint.rgba)) ||
      tile->u.picture.depth + 1u > store->limits.max_picture_depth) {
    return GSHSCENE_ERR_LIMIT;
  }

  memset(out, 0, sizeof *out);
  out->tile = tile;
  out->x = plus_zero(in->x);
  out->y = plus_zero(in->y);
  out->w = plus_zero(in->w);
  out->h = plus_zero(in->h);
  out->x_step = plus_zero(x_step);
  out->y_step = plus_zero(y_step);
  out->repeat_x = in->repeat_x;
  out->repeat_y = in->repeat_y;
  out->map = map;
  for (int i = 0; i < 2; i++) {
    for (int j = 0; j < 2; j++) {
      out->map.m[i][j] = plus_zero(map.m[i][j]);
    }
    out->map.t[i] = plus_zero(map.t[i]);
  }
  out->has_tint = in->has_tint;
  out->overflow = in->overflow;
  if (in->has_tint) {
    out->tint = tint;
    for (int i = 0; i < 4; i++) {
      out->tint.rgba[i] = plus_zero(tint.rgba[i]);
    }
  }
  return GSHSCENE_OK;
}

/* --- hash (PT3) ----------------------------------------------------------------------------- */

static uint64_t content_hash(const GSHSCENE_Pattern * p) {
  GSHGEOM_Hash h;
  gshgeom_hash_init(&h);
  gshgeom_hash_u32(&h, PATTERN_FORMAT_VERSION);
  gshgeom_hash_u32(&h, (uint32_t)GSHSCENE_RESOURCE_PATTERN);
  gshgeom_hash_u64(&h, gshscene_resource_hash(p->tile));
  gshgeom_hash_f64(&h, p->x);
  gshgeom_hash_f64(&h, p->y);
  gshgeom_hash_f64(&h, p->w);
  gshgeom_hash_f64(&h, p->h);
  gshgeom_hash_f64(&h, p->x_step);
  gshgeom_hash_f64(&h, p->y_step);
  gshgeom_hash_u8(&h, (uint8_t)p->repeat_x);
  gshgeom_hash_u8(&h, (uint8_t)p->repeat_y);
  gshgeom_hash_u64(&h, gshgeom_map_hash(&p->map));
  gshgeom_hash_u8(&h, (uint8_t)p->has_tint);
  if (p->has_tint) {
    gshgeom_hash_u8(&h, (uint8_t)p->tint.kind);
    for (int i = 0; i < 4; i++) {
      gshgeom_hash_f64(&h, p->tint.rgba[i]);
    }
    gshscene_space_hash(&h, &p->tint.space);
    gshgeom_hash_u32(&h, p->tint.entry);
  }
  if (p->overflow) {
    gshgeom_hash_u8(&h, 1); /* PT15: written only when set, so that a pattern that clips has the hash it always had */
  }
  return gshgeom_hash_value(&h);
}

/* --- equality, build, free -------------------------------------------------------------------- */

static int bits_equal(double a, double b) {
  return gshgeom_f64_bits(a) == gshgeom_f64_bits(b);
}

int gshscene_pattern_matches(const GSHSCENE_Resource * resource, const void * source) {
  const GSHSCENE_Pattern * a = &resource->u.pattern.p;
  const GSHSCENE_Pattern * b = &((const Source *)source)->p;
  if (a->tile != b->tile || !bits_equal(a->x, b->x) || !bits_equal(a->y, b->y) || !bits_equal(a->w, b->w) ||
      !bits_equal(a->h, b->h) || !bits_equal(a->x_step, b->x_step) || !bits_equal(a->y_step, b->y_step) ||
      a->repeat_x != b->repeat_x || a->repeat_y != b->repeat_y || a->has_tint != b->has_tint || a->overflow != b->overflow ||
      a->map.dim != b->map.dim || a->map.kind != b->map.kind) {
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
  if (!a->has_tint) {
    return 1;
  }
  for (int i = 0; i < 4; i++) {
    if (!bits_equal(a->tint.rgba[i], b->tint.rgba[i])) {
      return 0;
    }
  }
  return a->tint.kind == b->tint.kind && a->tint.entry == b->tint.entry &&
      gshscene_space_equal(&a->tint.space, &b->tint.space);
}

int gshscene_paint_reads_foreground(const GSHSCENE_Paint * paint) {
  if (!paint) {
    return 0;
  }
  switch (paint->kind) {
    case GSHSCENE_PAINT_FOREGROUND:
      return 1;
    case GSHSCENE_PAINT_GRADIENT:
      return paint->resource && paint->resource->u.gradient.foreground != 0;
    case GSHSCENE_PAINT_IMAGE:
      return paint->resource && paint->resource->u.imagepaint.foreground != 0; /* IP10: a foreground tint */
    case GSHSCENE_PAINT_PATTERN:
      return paint->resource && paint->resource->u.pattern.foreground != 0; /* PT12 */
    case GSHSCENE_PAINT_HATCH:
      return paint->resource && paint->resource->u.hatch.foreground != 0; /* HX1: the ink */
    default:
      return 0;
  }
}

uint32_t gshscene_paint_palette_need(const GSHSCENE_Paint * paint) {
  if (!paint) {
    return 0;
  }
  switch (paint->kind) {
    case GSHSCENE_PAINT_PALETTE:
      return paint->entry + 1u;
    case GSHSCENE_PAINT_GRADIENT:
      return paint->resource ? paint->resource->u.gradient.palette_need : 0; /* G3 */
    case GSHSCENE_PAINT_IMAGE:
      return paint->resource ? paint->resource->u.imagepaint.palette_need : 0; /* IP10: a palette tint */
    case GSHSCENE_PAINT_PATTERN:
      return paint->resource ? paint->resource->u.pattern.palette_need : 0; /* PT12 */
    case GSHSCENE_PAINT_HATCH:
      return paint->resource ? paint->resource->u.hatch.palette_need : 0; /* HX1: the ink */
    default:
      return 0;
  }
}

/**
 * PT12: what the tile @p list reads of the drawing list: whether any paint in its expansion reads the foreground, and the
 * largest palette entry any names plus one.  The expansion is entered as the planning pass enters it: an instance's picture, a
 * group's mask picture, and (through the paint's own flags, which the pattern computed when it was interned) a nested tile.
 */
static void scan_tile(const GSHSCENE_List * list, int * foreground, uint32_t * palette_need) {
  size_t count = gshscene_list_count(list);
  for (size_t i = 0; i < count; i++) {
    const GSHSCENE_Item * item = NULL;
    if (gshscene_list_item(list, i, &item) != GSHSCENE_OK) {
      return;
    }
    if (item->kind == GSHSCENE_ITEM_FILL || item->kind == GSHSCENE_ITEM_STROKE) {
      if (gshscene_paint_reads_foreground(&item->paint)) {
        *foreground = 1;
      }
      uint32_t need = gshscene_paint_palette_need(&item->paint);
      if (need > *palette_need) {
        *palette_need = need;
      }
    } else if (item->kind == GSHSCENE_ITEM_INSTANCE) {
      scan_tile(gshscene_resource_picture(item->resource), foreground, palette_need);
    } else if (item->kind == GSHSCENE_ITEM_GROUP_PUSH && item->group.soft_mask) {
      scan_tile(gshscene_resource_picture(item->group.soft_mask->u.mask.m.picture), foreground, palette_need);
    }
  }
}

/* --- PT15: the extent of a pattern whose tile may draw past its cell ---------------------------------- */

static void box_add(GSHGEOM_Bounds * acc, const GSHGEOM_Bounds * b) {
  if (b->empty) {
    return;
  }
  if (acc->empty) {
    *acc = *b;
    acc->empty = 0;
    return;
  }
  acc->min.x = b->min.x < acc->min.x ? b->min.x : acc->min.x;
  acc->min.y = b->min.y < acc->min.y ? b->min.y : acc->min.y;
  acc->max.x = b->max.x > acc->max.x ? b->max.x : acc->max.x;
  acc->max.y = b->max.y > acc->max.y ? b->max.y : acc->max.y;
}

/** The box of @p in under @p map (the four corners mapped), or a non-OK result when a corner cannot be had. */
static GSHSCENE_Result box_mapped(const GSHGEOM_Map * map, const GSHGEOM_Bounds * in, GSHGEOM_Bounds * out) {
  memset(out, 0, sizeof *out);
  out->empty = 1;
  if (in->empty) {
    return GSHSCENE_OK;
  }
  const double xs[2] = {in->min.x, in->max.x};
  const double ys[2] = {in->min.y, in->max.y};
  for (int a = 0; a < 2; a++) {
    for (int c = 0; c < 2; c++) {
      GSHGEOM_Point q;
      if (gshgeom_map_apply_point(map, (GSHGEOM_Point){xs[a], ys[c], 0.0}, &q) != GSHGEOM_OK || !gshscene_finite(q.x) ||
          !gshscene_finite(q.y)) {
        return GSHSCENE_ERR_LIMIT;
      }
      GSHGEOM_Bounds pt;
      memset(&pt, 0, sizeof pt);
      pt.min.x = pt.max.x = q.x;
      pt.min.y = pt.max.y = q.y;
      box_add(out, &pt);
    }
  }
  return GSHSCENE_OK;
}

/**
 * PT15: a conservative box, in the tile's own space, of everything the tile can paint: each fill's region and each stroke's
 * region outset by its pen (SK10), under the maps of the instances that hold it.  Clips and masks are ignored (a box that is too large costs
 * copies, never pixels).  ::GSHSCENE_OK with @p box possibly empty, or a non-OK result when some unit's extent is not known: a
 * device-width pen (its outset depends on the whole chain to the target), a hatch, a bound the maps cannot take, or a tile
 * nested past the levels the walk has.
 */
static GSHSCENE_Result tile_box(const GSHSCENE_Resource * tile_resource, GSHGEOM_Bounds * box) {
  const GSHSCENE_List * tile = gshscene_resource_picture(tile_resource);
  memset(box, 0, sizeof *box);
  box->empty = 1;
  if (gshscene_expansion_bytes(tile) > GSHSCENE_PATTERN_CHECK_LEVELS * (sizeof(const GSHSCENE_List *) + sizeof(size_t))) {
    return GSHSCENE_ERR_LIMIT;
  }
  uint64_t memory[GSHSCENE_PATTERN_CHECK_LEVELS * 2];
  GSHGEOM_Map maps[GSHSCENE_PATTERN_CHECK_MAPS];
  size_t base[GSHSCENE_PATTERN_CHECK_LEVELS + 1];
  base[0] = 0;
  GSHSCENE_Expansion walk;
  if (gshscene_expansion_init(&walk, tile, memory, sizeof memory) != GSHSCENE_OK) {
    return GSHSCENE_ERR_LIMIT;
  }
  walk.plan = 1;
  const GSHSCENE_Item * unit;
  const size_t * chain;
  size_t count;
  while (gshscene_expansion_next(&walk, &unit, &chain, &count)) {
    if (count > GSHSCENE_PATTERN_CHECK_LEVELS) {
      return GSHSCENE_ERR_LIMIT;
    }
    const size_t prefix = base[count - 1];
    if (prefix + 2 > GSHSCENE_PATTERN_CHECK_MAPS) {
      return GSHSCENE_ERR_LIMIT;
    }
    if (unit->kind == GSHSCENE_ITEM_FILL || unit->kind == GSHSCENE_ITEM_STROKE) {
      if (unit->paint.kind == GSHSCENE_PAINT_HATCH) {
        return GSHSCENE_ERR_UNSUPPORTED;
      }
      GSHGEOM_Bounds centerline;
      GSHGEOM_Map composed;
      maps[prefix] = unit->map;
      if (gshscene_centerline_box(unit->resource, &centerline) != GSHSCENE_OK ||
          gshgeom_map_compose_chain(maps, prefix + 1u, NULL, &composed) != GSHGEOM_OK) {
        return GSHSCENE_ERR_UNSUPPORTED;
      }
      GSHGEOM_Bounds b;
      if (unit->kind == GSHSCENE_ITEM_STROKE) {
        if (!unit->style || unit->style->kind != GSHSCENE_RESOURCE_STROKE_STYLE ||
            unit->style->u.stroke.s.width_space == (uint32_t)GSHSCENE_STROKE_WIDTH_DEVICE ||
            gshscene_stroke_bounds(unit->style, &centerline, &composed, 0, &b) != GSHSCENE_OK) {
          return GSHSCENE_ERR_UNSUPPORTED;
        }
      } else if (box_mapped(&composed, &centerline, &b) != GSHSCENE_OK) {
        return GSHSCENE_ERR_UNSUPPORTED;
      }
      box_add(box, &b);
    }
    /* The maps a level adds, as the planning pass adds them and gshscene_pattern_tile_refused does. */
    if (unit->kind == GSHSCENE_ITEM_INSTANCE) {
      maps[prefix] = unit->map;
      base[count] = prefix + 1;
    } else if (unit->kind == GSHSCENE_ITEM_GROUP_PUSH && unit->group.soft_mask) {
      maps[prefix] = unit->group.soft_mask_map;
      base[count] = prefix + 1;
    } else if ((unit->kind == GSHSCENE_ITEM_FILL || unit->kind == GSHSCENE_ITEM_STROKE) &&
               unit->paint.kind == GSHSCENE_PAINT_PATTERN) {
      maps[prefix] = unit->map;
      maps[prefix + 1] = unit->paint.resource->u.pattern.p.map;
      base[count] = prefix + 2;
    }
  }
  return GSHSCENE_OK;
}

/** PT15: the extent of @p r (a pattern resource being built), once, at intern. */
static void compute_extent(GSHSCENE_Resource * r) {
  const GSHSCENE_Pattern * p = &r->u.pattern.p;
  r->u.pattern.extent_state = 0;
  if (!p->overflow) {
    return;
  }
  GSHGEOM_Bounds box;
  const double cell[4] = {p->x, p->y, p->x + p->w, p->y + p->h};
  if (tile_box(p->tile, &box) != GSHSCENE_OK) {
    r->u.pattern.extent_state = 2;
    return;
  }
  double e[4] = {cell[0], cell[1], cell[2], cell[3]};
  if (!box.empty) {
    e[0] = box.min.x < e[0] ? box.min.x : e[0];
    e[1] = box.min.y < e[1] ? box.min.y : e[1];
    e[2] = box.max.x > e[2] ? box.max.x : e[2];
    e[3] = box.max.y > e[3] ? box.max.y : e[3];
  }
  for (int i = 0; i < 4; i++) {
    if (!gshscene_finite(e[i])) {
      r->u.pattern.extent_state = 2;
      return;
    }
    r->u.pattern.extent[i] = e[i];
  }
  r->u.pattern.extent_state = 1;
}

GSHSCENE_Result gshscene_pattern_extent(const GSHSCENE_Resource * resource, double out[4]) {
  if (!resource || !out || resource->kind != GSHSCENE_RESOURCE_PATTERN) {
    return GSHSCENE_ERR_INVALID;
  }
  const GSHSCENE_Pattern * p = &resource->u.pattern.p;
  if (!p->overflow) {
    out[0] = p->x;
    out[1] = p->y;
    out[2] = p->x + p->w;
    out[3] = p->y + p->h;
    return GSHSCENE_OK;
  }
  if (resource->u.pattern.extent_state != 1) {
    return GSHSCENE_ERR_LIMIT;
  }
  for (int i = 0; i < 4; i++) {
    out[i] = resource->u.pattern.extent[i];
  }
  return GSHSCENE_OK;
}

static GSHSCENE_Result build(GSHSCENE_Store * store, const void * source, GSHSCENE_Resource ** out) {
  const Source * s = (const Source *)source;
  GSHSCENE_Resource * resource = gshscene_store_new_resource(store, GSHSCENE_RESOURCE_PATTERN);
  if (!resource) {
    return GSHSCENE_ERR_OOM;
  }
  resource->u.pattern.p = s->p;
  resource->u.pattern.tile = gshscene_resource_retain(s->p.tile);
  resource->u.pattern.p.tile = resource->u.pattern.tile;
  resource->u.pattern.depth = s->p.tile->u.picture.depth;
  int foreground = 0;
  uint32_t need = 0;
  scan_tile(gshscene_resource_picture(s->p.tile), &foreground, &need);
  if (s->p.has_tint) {
    foreground = foreground || gshscene_paint_reads_foreground(&s->p.tint);
    uint32_t tint_need = gshscene_paint_palette_need(&s->p.tint);
    need = tint_need > need ? tint_need : need;
  }
  resource->u.pattern.foreground = foreground;
  resource->u.pattern.palette_need = need;
  compute_extent(resource);
  *out = resource;
  return GSHSCENE_OK;
}

void gshscene_pattern_free(GSHSCENE_Resource * resource) {
  gshscene_resource_release(resource->u.pattern.tile);
}

GSHSCENE_Result gshscene_store_intern_pattern(GSHSCENE_Store * store, const GSHSCENE_Pattern * pattern,
    const GSHSCENE_Resource ** out) {
  if (!store || !pattern || !out) {
    return GSHSCENE_ERR_INVALID;
  }
  Source s;
  memset(&s, 0, sizeof s);
  GSHSCENE_Result r = canonical(store, pattern, &s.p);
  if (r != GSHSCENE_OK) {
    return r;
  }
  GSHSCENE_Probe probe = {GSHSCENE_RESOURCE_PATTERN, content_hash(&s.p), GSHSCENE_BYTES_BASE, &s, build};
  return gshscene_store_intern(store, &probe, out);
}

GSHSCENE_Result gshscene_resource_pattern(const GSHSCENE_Resource * resource, GSHSCENE_Pattern * out) {
  if (!resource || !out || resource->kind != GSHSCENE_RESOURCE_PATTERN) {
    return GSHSCENE_ERR_INVALID;
  }
  *out = resource->u.pattern.p;
  return GSHSCENE_OK;
}

int gshscene_pattern_singular(const GSHSCENE_Resource * resource) {
  return resource && resource->kind == GSHSCENE_RESOURCE_PATTERN && gshscene_map_singular(&resource->u.pattern.p.map);
}

/* --- the dump (PT3) ------------------------------------------------------------------------------ */

int gshscene_pattern_dump(const GSHSCENE_Resource * resource, FILE * out) {
  const GSHSCENE_Pattern * p = &resource->u.pattern.p;
  if (fprintf(out, "pattern cell=%016llx %016llx %016llx %016llx step=%016llx %016llx repeat=%u%u map=%016llx tile=%016llx tint=",
          HEX(gshgeom_f64_bits(p->x)), HEX(gshgeom_f64_bits(p->y)), HEX(gshgeom_f64_bits(p->w)),
          HEX(gshgeom_f64_bits(p->h)), HEX(gshgeom_f64_bits(p->x_step)), HEX(gshgeom_f64_bits(p->y_step)), p->repeat_x,
          p->repeat_y, HEX(gshgeom_map_hash(&p->map)), HEX(gshscene_resource_hash(p->tile))) < 0) {
    return -1;
  }
  if (!p->has_tint) {
    if (fputs("-", out) == EOF) {
      return -1;
    }
  } else {
    const GSHSCENE_Paint * t = &p->tint;
    if (fprintf(out, "%u %016llx %016llx %016llx %016llx ", (unsigned)t->kind, HEX(gshgeom_f64_bits(t->rgba[0])),
            HEX(gshgeom_f64_bits(t->rgba[1])), HEX(gshgeom_f64_bits(t->rgba[2])), HEX(gshgeom_f64_bits(t->rgba[3]))) < 0 ||
        gshscene_space_dump(out, &t->space) < 0 || fprintf(out, " %u", t->entry) < 0) {
      return -1;
    }
  }
  /* PT15: named only when set, so that the dump of a pattern that clips is the text it always was. */
  if (fputs(p->overflow ? " overflow=1\n" : "\n", out) == EOF) {
    return -1;
  }
  return gshscene_list_dump(gshscene_resource_picture(p->tile), out) == GSHSCENE_OK ? 0 : -1;
}

/* --- the lattice (PT5, PT7) ---------------------------------------------------------------- */

/**
 * The double of an index of magnitude at most 2^53 (exact), without a 64-bit-integer-to-double instruction: on i686 that is an x87 one (fild),
 * which the determinism gate refuses.  The high half is a signed 32-bit integer and the low half an unsigned one, each converted through SSE2.
 */
static double index_to_double(int64_t v) {
  const int32_t hi = (int32_t)(v >> 32);
  const uint32_t lo = (uint32_t)((uint64_t)v & 0xffffffffu);
  return (double)hi * 4294967296.0 + (double)lo;
}

GSHSCENE_Result gshscene_pattern_cell(const GSHSCENE_Pattern * pattern, int64_t i, int64_t j, double out[4]) {
  if (!pattern || !out || i < -GSHSCENE_PATTERN_INDEX_MAX || i > GSHSCENE_PATTERN_INDEX_MAX || j < -GSHSCENE_PATTERN_INDEX_MAX ||
      j > GSHSCENE_PATTERN_INDEX_MAX) {
    return GSHSCENE_ERR_INVALID;
  }
  /* The index is an integer below 2^53, so it is exact as a double; the edge is one product and one sum, whatever the index. */
  double left = pattern->x + index_to_double(i) * pattern->x_step;
  double top = pattern->y + index_to_double(j) * pattern->y_step;
  /* Abutting cells name one double for their shared edge: the next cell's left, not this left plus the width. */
  double right = pattern->x_step == pattern->w ? pattern->x + index_to_double(i + 1) * pattern->x_step : left + pattern->w;
  double bottom = pattern->y_step == pattern->h ? pattern->y + index_to_double(j + 1) * pattern->y_step : top + pattern->h;
  out[0] = left;
  out[1] = top;
  out[2] = right;
  out[3] = bottom;
  return GSHSCENE_OK;
}

int gshscene_pattern_overlaps(const GSHSCENE_Pattern * pattern) {
  /* A step on an axis that does not repeat is not used: the one copy on it has no neighbour to overlap. */
  return pattern && ((pattern->repeat_x && pattern->x_step < pattern->w) || (pattern->repeat_y && pattern->y_step < pattern->h));
}

/* --- PT10: what draws nothing ------------------------------------------------------------------ */

GSHSCENE_Reason gshscene_pattern_notice(const GSHSCENE_Item * item, const GSHGEOM_Map * instance_maps,
    size_t instance_count, const GSHGEOM_Map * target_map) {
  if (!item || (item->kind != GSHSCENE_ITEM_FILL && item->kind != GSHSCENE_ITEM_STROKE) ||
      item->paint.kind != GSHSCENE_PAINT_PATTERN || !item->paint.resource ||
      item->paint.resource->kind != GSHSCENE_RESOURCE_PATTERN || (instance_count && !instance_maps)) {
    return (GSHSCENE_Reason)0;
  }
  const GSHSCENE_Pattern * p = &item->paint.resource->u.pattern.p;
  /* Each map on its own doubles, no composition formed: invertible factors can compose into a determinant of exactly zero by
   * underflow, and a singular factor is singular whatever the product rounds to. */
  int not_invertible = (target_map && gshscene_map_singular(target_map)) || gshscene_map_singular(&item->map) ||
      gshscene_map_singular(&p->map);
  for (size_t i = 0; i < instance_count && !not_invertible; i++) {
    not_invertible = gshscene_map_singular(&instance_maps[i]);
  }
  if (not_invertible) {
    return GSHSCENE_REASON_MAP_NOT_INVERTIBLE;
  }
  /* D6's zero size and zero step; a step on an axis that does not repeat is not used. */
  if (p->w == 0.0 || p->h == 0.0 || (p->repeat_x && p->x_step == 0.0) || (p->repeat_y && p->y_step == 0.0)) {
    return GSHSCENE_REASON_PATTERN_DEGENERATE;
  }
  return (GSHSCENE_Reason)0;
}

/* --- PT13: the walk into a tile ---------------------------------------------------------------- */

int gshscene_pattern_tile_refused(const GSHSCENE_Sink_Caps * caps, const GSHSCENE_Item * item,
    const GSHGEOM_Map * instance_maps, size_t instance_count, const GSHGEOM_Map * target_map,
    const GSHSCENE_Color_Space * target_space, const GSHSCENE_Resource * palette) {
  const GSHSCENE_Resource * pattern = item->paint.resource;
  const GSHSCENE_List * tile = gshscene_resource_picture(pattern->u.pattern.tile);
  /* The walk's memory and the maps live here: a tile nested past the levels of this check is refused, not walked. */
  if (gshscene_expansion_bytes(tile) > GSHSCENE_PATTERN_CHECK_LEVELS * (sizeof(const GSHSCENE_List *) + sizeof(size_t)) ||
      instance_count > GSHSCENE_PATTERN_CHECK_LEVELS) {
    return 1;
  }
  uint64_t memory[GSHSCENE_PATTERN_CHECK_LEVELS * 2];
  GSHGEOM_Map maps[GSHSCENE_PATTERN_CHECK_MAPS];
  size_t base[GSHSCENE_PATTERN_CHECK_LEVELS + 1];
  size_t n = 0;
  for (; n < instance_count; n++) {
    maps[n] = instance_maps[n];
  }
  maps[n++] = item->map;
  maps[n++] = pattern->u.pattern.p.map;
  base[0] = n;
  GSHSCENE_Expansion walk;
  if (gshscene_expansion_init(&walk, tile, memory, sizeof memory) != GSHSCENE_OK) {
    return 1;
  }
  walk.plan = 1;
  const GSHSCENE_Item * unit;
  const size_t * chain;
  size_t count;
  while (gshscene_expansion_next(&walk, &unit, &chain, &count)) {
    if (count > GSHSCENE_PATTERN_CHECK_LEVELS) {
      return 1;
    }
    size_t prefix = base[count - 1];
    if (prefix + 2 > GSHSCENE_PATTERN_CHECK_MAPS) {
      return 1;
    }
    if (gshscene_caps_disposition_unit(caps, unit, maps, prefix, target_map, target_space, palette).entry_class ==
        GSHSCENE_ENTRY_REFUSED) {
      return 1;
    }
    /* The maps a level adds, as the planning pass adds them (sink.c): an instance's, a mask's placement, a pattern's two. */
    if (unit->kind == GSHSCENE_ITEM_INSTANCE) {
      maps[prefix] = unit->map;
      base[count] = prefix + 1;
    } else if (unit->kind == GSHSCENE_ITEM_GROUP_PUSH && unit->group.soft_mask) {
      maps[prefix] = unit->group.soft_mask_map;
      base[count] = prefix + 1;
    } else if ((unit->kind == GSHSCENE_ITEM_FILL || unit->kind == GSHSCENE_ITEM_STROKE) &&
               unit->paint.kind == GSHSCENE_PAINT_PATTERN) {
      maps[prefix] = unit->map;
      maps[prefix + 1] = unit->paint.resource->u.pattern.p.map;
      base[count] = prefix + 2;
    }
  }
  return 0;
}
