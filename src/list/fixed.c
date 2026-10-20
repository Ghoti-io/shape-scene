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

/** The item map of a design map on @p resource (fixed.h); the identity for no ops. */
static GSHSCENE_Result design_map(const GSHSCENE_Resource * resource, const GSHSCENE_Fixed_Op * ops, size_t count,
    GSHGEOM_Map * out) {
  if (count == 0) {
    return from_geometry(gshgeom_map_identity(2, out));
  }
  if (!ops) {
    return GSHSCENE_ERR_INVALID;
  }
  if (count > GSHSCENE_FIXED_MAX_OPS) {
    return GSHSCENE_ERR_LIMIT;
  }
  GSHSCENE_Glyph_Run run;
  GSHSCENE_Face_Info info;
  if (gshscene_resource_glyph_run(resource, &run) != GSHSCENE_OK || run.source != GSHSCENE_GLYPHS_FROM_FONT ||
      run.glyph_count == 0 || gshscene_face_info(run.face, &info) != GSHSCENE_OK) {
    return GSHSCENE_ERR_INVALID;
  }
  GSHGEOM_Map maps[GSHSCENE_FIXED_MAX_OPS];
  for (size_t i = 0; i < count; i++) {
    GSHSCENE_Result r = op_map(&ops[i], &maps[i]);
    if (r != GSHSCENE_OK) {
      return r;
    }
  }
  GSHGEOM_Map t;
  GSHGEOM_Result g = gshgeom_map_compose_chain(maps, count, NULL, &t);
  if (g != GSHGEOM_OK) {
    return from_geometry(g);
  }
  const double s = run.size / (double)(int32_t)info.upem;
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
