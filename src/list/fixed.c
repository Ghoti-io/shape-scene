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
 * Recording without floats (fixed.h): the design map, the fixed paint and the
 * calls that take them, each converted exactly and handed to list.h's own
 * record call, so that the rules and the failures are the list's.
 */

#include <ghoti.io/shape-scene/macros.h>

#include "../internal.h"

#include <string.h>

/** How many values an op of each kind uses (fixed.h's table). */
static const unsigned OP_VALUES[GSHSCENE_FIXED_OP_COUNT] = {6u, 2u, 4u, 3u, 4u};

/** `v / 2^bits`, exactly: an int32_t into a double, then a division by a power of two. */
static double fixed_value(int32_t v, uint32_t bits) {
  return (double)v / (double)((int32_t)1 << bits); /* an int32_t: i686 converts an unsigned one through x87 */
}

static GSHSCENE_Result from_geometry(GSHGEOM_Result result) {
  switch (result) {
    case GSHGEOM_OK:
      return GSHSCENE_OK;
    case GSHGEOM_ERR_LIMIT:
      return GSHSCENE_ERR_LIMIT;
    default:
      return GSHSCENE_ERR_INVALID;
  }
}

/** @p inner carried about the center @p (cx, cy): `translate(c) . inner . translate(-c)`, by the one composer. */
static GSHSCENE_Result about(const GSHGEOM_Map * inner, double cx, double cy, GSHGEOM_Map * out) {
  GSHGEOM_Map chain[3];
  chain[1] = *inner;
  GSHGEOM_Result r = gshgeom_map_translate(2, cx, cy, 0.0, &chain[0]);
  if (r == GSHGEOM_OK) {
    r = gshgeom_map_translate(2, -cx, -cy, 0.0, &chain[2]);
  }
  if (r == GSHGEOM_OK) {
    r = gshgeom_map_compose_chain(chain, 3, NULL, out);
  }
  return from_geometry(r);
}

/** The tangent of @p turns as fixed.h defines it: the sine over the cosine of shape-geometry's rotation. */
static GSHSCENE_Result tangent(double turns, double * out) {
  GSHGEOM_Map r;
  GSHGEOM_Result g = gshgeom_map_rotate(GSHGEOM_ANGLE_TURNS, turns, &r);
  if (g != GSHGEOM_OK) {
    return from_geometry(g);
  }
  if (r.m[0][0] == 0.0) {
    return GSHSCENE_ERR_INVALID; /* a quarter turn: the skew is infinite */
  }
  *out = r.m[1][0] / r.m[0][0];
  return GSHSCENE_OK;
}

/** The map of one op (fixed.h's table). */
static GSHSCENE_Result op_map(const GSHSCENE_Fixed_Op * op, GSHGEOM_Map * out) {
  if (op->kind >= (uint32_t)GSHSCENE_FIXED_OP_COUNT || op->bits > GSHSCENE_FIXED_MAX_BITS) {
    return GSHSCENE_ERR_INVALID;
  }
  for (unsigned i = OP_VALUES[op->kind]; i < 6u; i++) {
    if (op->v[i] != 0) {
      return GSHSCENE_ERR_INVALID; /* one spelling: a value the kind does not use is zero */
    }
  }
  double v[6];
  for (int i = 0; i < 6; i++) {
    v[i] = fixed_value(op->v[i], op->bits);
  }
  const double t0[2] = {0.0, 0.0};
  GSHGEOM_Map core;
  GSHGEOM_Result g = GSHGEOM_OK;
  switch ((GSHSCENE_Fixed_Op_Kind)op->kind) {
    case GSHSCENE_FIXED_AFFINE: {
      const double m[4] = {v[0], v[2], v[1], v[3]}; /* row major: x' = xx x + xy y, y' = yx x + yy y */
      const double t[2] = {v[4], v[5]};
      return from_geometry(gshgeom_map_from_matrix(2, m, t, out));
    }
    case GSHSCENE_FIXED_TRANSLATE:
      return from_geometry(gshgeom_map_translate(2, v[0], v[1], 0.0, out));
    case GSHSCENE_FIXED_SCALE: {
      const double m[4] = {v[0], 0.0, 0.0, v[1]};
      g = gshgeom_map_from_matrix(2, m, t0, &core);
      if (g != GSHGEOM_OK) {
        return from_geometry(g);
      }
      return about(&core, v[2], v[3], out);
    }
    case GSHSCENE_FIXED_ROTATE:
      g = gshgeom_map_rotate(GSHGEOM_ANGLE_TURNS, v[0], &core);
      if (g != GSHGEOM_OK) {
        return from_geometry(g);
      }
      return about(&core, v[1], v[2], out);
    case GSHSCENE_FIXED_SKEW: {
      double tx = 0.0;
      double ty = 0.0;
      GSHSCENE_Result r = tangent(v[0], &tx);
      if (r == GSHSCENE_OK) {
        r = tangent(v[1], &ty);
      }
      if (r != GSHSCENE_OK) {
        return r;
      }
      const double m[4] = {1.0, -tx, ty, 1.0};
      g = gshgeom_map_from_matrix(2, m, t0, &core);
      if (g != GSHGEOM_OK) {
        return from_geometry(g);
      }
      return about(&core, v[2], v[3], out);
    }
    default:
      return GSHSCENE_ERR_INVALID;
  }
}

/** Whether @p count ops at @p ops can be a design map (fixed.h): a pointer for any, at most the most. */
static GSHSCENE_Result check_ops(const GSHSCENE_Fixed_Op * ops, size_t count) {
  if (count && !ops) {
    return GSHSCENE_ERR_INVALID;
  }
  return count > GSHSCENE_FIXED_MAX_OPS ? GSHSCENE_ERR_LIMIT : GSHSCENE_OK;
}

/** The maps of @p count checked ops, into @p maps. */
static GSHSCENE_Result ops_maps(const GSHSCENE_Fixed_Op * ops, size_t count, GSHGEOM_Map * maps) {
  for (size_t i = 0; i < count; i++) {
    GSHSCENE_Result r = op_map(&ops[i], &maps[i]);
    if (r != GSHSCENE_OK) {
      return r;
    }
  }
  return GSHSCENE_OK;
}

/** A run from a bound face with a glyph to be about (fixed.h): its content and its scale `size / upem`. */
static GSHSCENE_Result font_run(const GSHSCENE_Resource * resource, GSHSCENE_Glyph_Run * run, double * scale) {
  GSHSCENE_Face_Info info;
  if (gshscene_resource_glyph_run(resource, run) != GSHSCENE_OK || run->source != GSHSCENE_GLYPHS_FROM_FONT ||
      run->glyph_count == 0 || gshscene_face_info(run->face, &info) != GSHSCENE_OK) {
    return GSHSCENE_ERR_INVALID;
  }
  *scale = run->size / (double)(int32_t)info.upem;
  return GSHSCENE_OK;
}

/** The item map of a design map on @p resource (fixed.h); the identity for no ops. */
static GSHSCENE_Result design_map(const GSHSCENE_Resource * resource, const GSHSCENE_Fixed_Op * ops, size_t count,
    GSHGEOM_Map * out) {
  if (count == 0) {
    return from_geometry(gshgeom_map_identity(2, out));
  }
  GSHSCENE_Result checked = check_ops(ops, count);
  if (checked != GSHSCENE_OK) {
    return checked;
  }
  GSHSCENE_Glyph_Run run;
  double s;
  checked = font_run(resource, &run, &s);
  if (checked != GSHSCENE_OK) {
    return checked;
  }
  GSHGEOM_Map maps[GSHSCENE_FIXED_MAX_OPS];
  checked = ops_maps(ops, count, maps);
  if (checked != GSHSCENE_OK) {
    return checked;
  }
  GSHGEOM_Map t;
  GSHGEOM_Result g = gshgeom_map_compose_chain(maps, count, NULL, &t);
  if (g != GSHGEOM_OK) {
    return from_geometry(g);
  }
  const double bx = s * t.t[0];
  const double by = s * t.t[1];
  if (t.m[0][0] == 1.0 && t.m[0][1] == 0.0 && t.m[1][0] == 0.0 && t.m[1][1] == 1.0) {
    return from_geometry(gshgeom_map_translate(2, bx, -by, 0.0, out));
  }
  const double gx = run.glyphs[0].x;
  const double gy = run.glyphs[0].y;
  GSHGEOM_Map chain[5];
  const double m[4] = {t.m[0][0], t.m[0][1], t.m[1][0], t.m[1][1]};
  const double b[2] = {bx, by};
  g = gshgeom_map_translate(2, gx, gy, 0.0, &chain[0]);
  if (g == GSHGEOM_OK) {
    g = gshgeom_map_flip_y(2, &chain[1]);
  }
  if (g == GSHGEOM_OK) {
    g = gshgeom_map_from_matrix(2, m, b, &chain[2]);
  }
  if (g == GSHGEOM_OK) {
    g = gshgeom_map_flip_y(2, &chain[3]);
  }
  if (g == GSHGEOM_OK) {
    g = gshgeom_map_translate(2, -gx, -gy, 0.0, &chain[4]);
  }
  if (g == GSHGEOM_OK) {
    g = gshgeom_map_compose_chain(chain, 5, NULL, out);
  }
  return from_geometry(g);
}

/** The paint of a fixed paint (fixed.h): a palette entry of 0xFFFF is the foreground. */
static GSHSCENE_Result fixed_paint(const GSHSCENE_Paint_Fixed * fixed, GSHSCENE_Paint * out) {
  if (!fixed || fixed->bits > GSHSCENE_FIXED_MAX_BITS || fixed->alpha < 0 ||
      fixed->alpha > ((int32_t)1 << fixed->bits)) {
    return GSHSCENE_ERR_INVALID;
  }
  memset(out, 0, sizeof *out);
  out->rgba[3] = fixed_value(fixed->alpha, fixed->bits);
  if (fixed->kind == GSHSCENE_FIXED_PAINT_PALETTE) {
    if (fixed->entry > GSHSCENE_FIXED_ENTRY_FOREGROUND) {
      return GSHSCENE_ERR_INVALID;
    }
    if (fixed->entry == GSHSCENE_FIXED_ENTRY_FOREGROUND) {
      out->kind = GSHSCENE_PAINT_FOREGROUND; /* N14: where 0xFFFF is lowered */
      return GSHSCENE_OK;
    }
    out->kind = GSHSCENE_PAINT_PALETTE;
    out->entry = fixed->entry;
    return GSHSCENE_OK;
  }
  if (fixed->kind == GSHSCENE_FIXED_PAINT_FOREGROUND && fixed->entry == 0) {
    out->kind = GSHSCENE_PAINT_FOREGROUND;
    return GSHSCENE_OK;
  }
  return GSHSCENE_ERR_INVALID;
}

GSHSCENE_Result gshscene_list_fill_fixed(GSHSCENE_List * list, const GSHSCENE_Resource * resource,
    const GSHSCENE_Fixed_Op * ops, size_t op_count, GSHGEOM_Fill_Rule rule, const GSHSCENE_Paint_Fixed * paint,
    int32_t layer) {
  if (!list || list->closed || !resource || resource->store != list->store) {
    return GSHSCENE_ERR_INVALID;
  }
  GSHSCENE_Paint p;
  GSHSCENE_Result r = fixed_paint(paint, &p);
  GSHGEOM_Map map;
  if (r == GSHSCENE_OK) {
    r = design_map(resource, ops, op_count, &map);
  }
  return r == GSHSCENE_OK ? gshscene_list_fill(list, resource, &map, rule, &p, layer, 0.0) : r;
}

GSHSCENE_Result gshscene_list_clip_push_fixed(GSHSCENE_List * list, const GSHSCENE_Resource * resource,
    const GSHSCENE_Fixed_Op * ops, size_t op_count, GSHGEOM_Fill_Rule rule, int32_t layer) {
  if (!list || list->closed || !resource || resource->store != list->store) {
    return GSHSCENE_ERR_INVALID;
  }
  GSHGEOM_Map map;
  GSHSCENE_Result r = design_map(resource, ops, op_count, &map);
  return r == GSHSCENE_OK ? gshscene_list_clip_push(list, resource, &map, rule, layer, 0.0) : r;
}

GSHSCENE_Result gshscene_list_group_push_fixed(GSHSCENE_List * list, uint32_t composite, int32_t layer) {
  GSHSCENE_Group group;
  memset(&group, 0, sizeof group);
  group.opacity = 1.0;
  group.ordering = GSHSCENE_ORDER_FLAT;
  group.composite = composite;
  return gshscene_list_group_push(list, &group, layer, 0.0);
}

GSHSCENE_Result gshscene_list_fill_gradient_fixed(GSHSCENE_List * list, const GSHSCENE_Resource * resource,
    const GSHSCENE_Fixed_Op * ops, size_t op_count, GSHGEOM_Fill_Rule rule, const GSHSCENE_Resource * gradient,
    int32_t alpha, uint32_t bits, int32_t layer) {
  if (!list || list->closed || !resource || resource->store != list->store || bits > GSHSCENE_FIXED_MAX_BITS ||
      alpha < 0 || alpha > ((int32_t)1 << bits)) {
    return GSHSCENE_ERR_INVALID;
  }
  GSHSCENE_Paint p;
  memset(&p, 0, sizeof p);
  p.kind = GSHSCENE_PAINT_GRADIENT;
  p.rgba[3] = fixed_value(alpha, bits);
  p.gradient = gradient;
  GSHGEOM_Map map;
  GSHSCENE_Result r = design_map(resource, ops, op_count, &map);
  return r == GSHSCENE_OK ? gshscene_list_fill(list, resource, &map, rule, &p, layer, 0.0) : r;
}

/* --- a gradient (fixed.h, G1 to G13) ------------------------------------------------------ */

/** How many values each gradient kind uses (fixed.h's table), by kind. */
static const unsigned GRADIENT_VALUES[4] = {0u, 6u, 6u, 4u};

/** The gradient map of fixed.h: `design_to_model(size / upem, g) . T`, T the ops' chain. */
static GSHSCENE_Result gradient_map(const GSHSCENE_Resource * resource, const GSHSCENE_Fixed_Op * ops, size_t count,
    GSHGEOM_Map * out) {
  GSHSCENE_Result checked = check_ops(ops, count);
  if (checked != GSHSCENE_OK) {
    return checked;
  }
  GSHSCENE_Glyph_Run run;
  double s;
  checked = font_run(resource, &run, &s);
  if (checked != GSHSCENE_OK) {
    return checked;
  }
  GSHGEOM_Map maps[GSHSCENE_FIXED_MAX_OPS + 1];
  GSHGEOM_Result g = gshgeom_map_design_to_model(s, run.glyphs[0].x, run.glyphs[0].y, &maps[0]);
  if (g != GSHGEOM_OK) {
    return from_geometry(g);
  }
  checked = ops_maps(ops, count, maps + 1);
  if (checked != GSHSCENE_OK) {
    return checked;
  }
  return from_geometry(gshgeom_map_compose_chain(maps, count + 1, NULL, out));
}

GSHSCENE_Result gshscene_store_intern_gradient_fixed(GSHSCENE_Store * store, const GSHSCENE_Resource * run,
    const GSHSCENE_Gradient_Fixed * fixed, const GSHSCENE_Resource ** out) {
  if (!store || !run || !fixed || !out || run->store != store || fixed->kind < GSHSCENE_FIXED_GRADIENT_LINEAR ||
      fixed->kind > GSHSCENE_FIXED_GRADIENT_SWEEP || fixed->bits > GSHSCENE_FIXED_MAX_BITS ||
      (fixed->stop_count && !fixed->stops)) {
    return GSHSCENE_ERR_INVALID;
  }
  for (unsigned i = GRADIENT_VALUES[fixed->kind]; i < 6u; i++) {
    if (fixed->v[i] != 0) {
      return GSHSCENE_ERR_INVALID; /* one spelling: a value the kind does not use is zero */
    }
  }
  /* A space of primaries and a linear or sRGB transfer, no gamma: what a float-free caller can name. */
  if (fixed->transfer != (uint32_t)GCOL_TRANSFER_LINEAR && fixed->transfer != (uint32_t)GCOL_TRANSFER_SRGB) {
    return GSHSCENE_ERR_INVALID;
  }
  if (fixed->stop_count > store->limits.max_resource_elements ||
      fixed->stop_count > SIZE_MAX / sizeof(GSHSCENE_Gradient_Stop)) {
    return GSHSCENE_ERR_LIMIT;
  }
  GSHSCENE_Gradient g;
  memset(&g, 0, sizeof g);
  g.kind = fixed->kind;
  g.extend = fixed->extend;
  g.premultiplied = fixed->premultiplied;
  g.space.primaries = (GCOL_Primaries)fixed->primaries;
  g.space.transfer = (GCOL_Transfer)fixed->transfer;
  const int32_t * v = fixed->v;
  const double unit = (double)((int32_t)1 << fixed->bits); /* an int32_t: i686 converts an unsigned one through x87 */
  switch (fixed->kind) {
    case GSHSCENE_FIXED_GRADIENT_LINEAR:
      g.p0[0] = fixed_value(v[0], fixed->bits);
      g.p0[1] = fixed_value(v[1], fixed->bits);
      g.p1[0] = fixed_value(v[2], fixed->bits);
      g.p1[1] = fixed_value(v[3], fixed->bits);
      /* COLRv1's rotation point: d = p2 - p0, each difference of two int32_t exact in a double. */
      g.d[0] = ((double)v[4] - (double)v[0]) / unit;
      g.d[1] = ((double)v[5] - (double)v[1]) / unit;
      break;
    case GSHSCENE_FIXED_GRADIENT_RADIAL:
      g.c0[0] = fixed_value(v[0], fixed->bits);
      g.c0[1] = fixed_value(v[1], fixed->bits);
      g.r0 = fixed_value(v[2], fixed->bits);
      g.c1[0] = fixed_value(v[3], fixed->bits);
      g.c1[1] = fixed_value(v[4], fixed->bits);
      g.r1 = fixed_value(v[5], fixed->bits);
      break;
    default:
      g.center[0] = fixed_value(v[0], fixed->bits);
      g.center[1] = fixed_value(v[1], fixed->bits);
      g.angle_unit = GSHGEOM_ANGLE_DEGREES;
      /* Half turns to degrees: a 31-bit integer times 180 is exact in a double, and so is the power of two. */
      g.a0 = (double)v[2] * 180.0 / unit;
      g.a1 = (double)v[3] * 180.0 / unit;
      break;
  }
  GSHSCENE_Result r = gradient_map(run, fixed->ops, fixed->op_count, &g.map);
  if (r != GSHSCENE_OK) {
    return r;
  }
  GSHSCENE_Gradient_Stop * stops = NULL;
  if (fixed->stop_count) {
    stops = gshscene_alloc(&store->alloc, fixed->stop_count * sizeof *stops);
    if (!stops) {
      return GSHSCENE_ERR_OOM;
    }
  }
  for (size_t i = 0; i < fixed->stop_count && r == GSHSCENE_OK; i++) {
    const GSHSCENE_Gradient_Stop_Fixed * s = &fixed->stops[i];
    if (s->bits > GSHSCENE_FIXED_MAX_BITS) {
      r = GSHSCENE_ERR_INVALID;
      break;
    }
    stops[i].offset = fixed_value(s->offset, s->bits);
    r = fixed_paint(&s->color, &stops[i].color);
  }
  if (r == GSHSCENE_OK) {
    g.stop_count = fixed->stop_count;
    g.stops = stops;
    r = gshscene_store_intern_gradient(store, &g, out);
  }
  gshscene_free(&store->alloc, stops);
  return r;
}
