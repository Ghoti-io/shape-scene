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
 * The primitive tag (primitive.h, PR1 to PR12): validation, the build from shape-geometry's builders, the canonical form, the hash,
 * equality and dump text, and the accessor.  A tag is set here and nowhere else.
 */

#include <ghoti.io/shape-scene/macros.h>

#include "../internal.h"

#include <ghoti.io/shape-scene/primitive.h>

#include <string.h>

#define HEX(x) ((unsigned long long)(x))

const char * gshscene_primitive_kind_string(GSHSCENE_Primitive_Kind kind) {
  switch (kind) {
    case GSHSCENE_PRIMITIVE_NONE:
      return "none";
    case GSHSCENE_PRIMITIVE_RECT:
      return "rect";
    case GSHSCENE_PRIMITIVE_ROUNDED_RECT:
      return "rounded-rect";
    case GSHSCENE_PRIMITIVE_ELLIPSE:
      return "ellipse";
    default:
      return "?";
  }
}

static int bits_equal(double a, double b) {
  return gshgeom_f64_bits(a) == gshgeom_f64_bits(b);
}

static int is_number(double v) {
  return v - v == 0.0;
}

/** The eight radius components of a rounded rectangle, canonical: a corner with a zero component is `{+0, +0}`. */
static GSHGEOM_Corner canonical_corner(GSHGEOM_Corner c) {
  if (c.rx == 0.0 || c.ry == 0.0) {
    c.rx = 0.0;
    c.ry = 0.0;
  }
  return c;
}

static GSHSCENE_Result from_geometry(GSHGEOM_Result r) {
  switch (r) {
    case GSHGEOM_OK:
      return GSHSCENE_OK;
    case GSHGEOM_ERR_INVALID:
      return GSHSCENE_ERR_INVALID;
    case GSHGEOM_ERR_LIMIT:
      return GSHSCENE_ERR_LIMIT;
    case GSHGEOM_ERR_OOM:
      return GSHSCENE_ERR_OOM;
    default:
      return GSHSCENE_ERR_INTERNAL;
  }
}

GSHSCENE_Result gshscene_primitive_build(
    GSHSCENE_Store * store, const GSHSCENE_Primitive * in, GSHGEOM_Path ** path, GSHSCENE_Primitive * canon) {
  if (!in || !path || !canon || in->kind <= GSHSCENE_PRIMITIVE_NONE || in->kind >= GSHSCENE_PRIMITIVE_KIND_COUNT ||
      (in->flip_x != 0 && in->flip_x != 1) || (in->flip_y != 0 && in->flip_y != 1)) {
    return GSHSCENE_ERR_INVALID;
  }
  GSHSCENE_Primitive c;
  memset(&c, 0, sizeof c);
  c.kind = in->kind;
  c.flip_x = in->flip_x;
  c.flip_y = in->flip_y;
  GSHGEOM_Path * p = NULL;
  /* The numbers and the overlap come first, so that a bad number is INVALID before a flip is UNSUPPORTED before a limit. */
  if (in->kind == GSHSCENE_PRIMITIVE_ROUNDED_RECT) {
    if (!is_number(in->x) || !is_number(in->y) || gshgeom_rounded_rect_check(in->w, in->h, in->r) != GSHGEOM_OK) {
      return GSHSCENE_ERR_INVALID;
    }
    if (in->flip_x || in->flip_y) {
      return GSHSCENE_ERR_UNSUPPORTED;
    }
    c.x = in->x;
    c.y = in->y;
    c.w = in->w;
    c.h = in->h;
    int round = 0;
    for (int i = 0; i < 4; i++) {
      c.r[i] = canonical_corner(in->r[i]);
      round |= c.r[i].rx != 0.0;
    }
    if (!round) {
      c.kind = GSHSCENE_PRIMITIVE_RECT; /* A2-D2: a rounded rectangle with no round corner is a rectangle (its corners are {0, 0} already) */
    }
  } else if (in->kind == GSHSCENE_PRIMITIVE_RECT) {
    /* the builder refuses a number that is not one and a negative size, as INVALID */
    c.x = in->x;
    c.y = in->y;
    c.w = in->w;
    c.h = in->h;
  } else {
    c.cx = in->cx;
    c.cy = in->cy;
    c.rx = in->rx;
    c.ry = in->ry;
  }
  GSHGEOM_Result g = gshgeom_path_create(&store->alloc, NULL, 2, &p);
  if (g == GSHGEOM_OK) {
    if (c.kind == GSHSCENE_PRIMITIVE_RECT) {
      g = gshgeom_path_add_rect(p, c.x, c.y, c.w, c.h, c.flip_x, c.flip_y);
    } else if (c.kind == GSHSCENE_PRIMITIVE_ROUNDED_RECT) {
      g = gshgeom_path_add_rounded_rect(p, c.x, c.y, c.w, c.h, c.r);
    } else {
      g = gshgeom_path_add_ellipse(p, c.cx, c.cy, c.rx, c.ry, c.flip_x, c.flip_y);
    }
  }
  if (g != GSHGEOM_OK) {
    gshgeom_path_destroy(p);
    return from_geometry(g);
  }
  *path = p;
  *canon = c;
  return GSHSCENE_OK;
}

/** The parameters of a tag in the order the hash and the dump take them; returns how many. */
static size_t parameters(const GSHSCENE_Primitive * p, double out[12]) {
  switch (p->kind) {
    case GSHSCENE_PRIMITIVE_RECT:
      out[0] = p->x;
      out[1] = p->y;
      out[2] = p->w;
      out[3] = p->h;
      return 4;
    case GSHSCENE_PRIMITIVE_ROUNDED_RECT:
      out[0] = p->x;
      out[1] = p->y;
      out[2] = p->w;
      out[3] = p->h;
      for (int i = 0; i < 4; i++) {
        out[4 + 2 * i] = p->r[i].rx;
        out[5 + 2 * i] = p->r[i].ry;
      }
      return 12;
    case GSHSCENE_PRIMITIVE_ELLIPSE:
      out[0] = p->cx;
      out[1] = p->cy;
      out[2] = p->rx;
      out[3] = p->ry;
      return 4;
    default:
      return 0;
  }
}

void gshscene_primitive_hash(GSHGEOM_Hash * h, const GSHSCENE_Primitive * p) {
  if (p->kind == GSHSCENE_PRIMITIVE_NONE) {
    return;
  }
  double v[12];
  const size_t n = parameters(p, v);
  gshgeom_hash_u8(h, (uint8_t)p->kind);
  gshgeom_hash_u8(h, (uint8_t)((p->flip_x ? 1u : 0u) | (p->flip_y ? 2u : 0u)));
  for (size_t i = 0; i < n; i++) {
    gshgeom_hash_f64(h, v[i]);
  }
}

int gshscene_primitive_equal(const GSHSCENE_Primitive * a, const GSHSCENE_Primitive * b) {
  if (a->kind != b->kind || a->flip_x != b->flip_x || a->flip_y != b->flip_y) {
    return 0;
  }
  double va[12], vb[12];
  const size_t n = parameters(a, va);
  parameters(b, vb);
  for (size_t i = 0; i < n; i++) {
    if (!bits_equal(va[i], vb[i])) {
      return 0;
    }
  }
  return 1;
}

void gshscene_primitive_dump(const GSHSCENE_Primitive * p, FILE * out) {
  if (p->kind == GSHSCENE_PRIMITIVE_NONE) {
    return;
  }
  double v[12];
  const size_t n = parameters(p, v);
  /* A write that fails sets the stream's sticky error flag, which gshscene_resource_dump reads after the last line. */
  fprintf(out, "primitive kind=%s flip_x=%d flip_y=%d", gshscene_primitive_kind_string(p->kind), p->flip_x ? 1 : 0,
      p->flip_y ? 1 : 0);
  for (size_t i = 0; i < n; i++) {
    fprintf(out, " %016llx", HEX(gshgeom_f64_bits(v[i])));
  }
  fputc('\n', out);
}

GSHSCENE_Result gshscene_store_intern_primitive(
    GSHSCENE_Store * store, const GSHSCENE_Primitive * prim, const GSHSCENE_Resource ** out) {
  if (!store || !prim) {
    return GSHSCENE_ERR_INVALID; /* a NULL out is refused by the intern below, after nothing is kept */
  }
  GSHGEOM_Path * path = NULL;
  GSHSCENE_Primitive canon;
  GSHSCENE_Result r = gshscene_primitive_build(store, prim, &path, &canon);
  if (r != GSHSCENE_OK) {
    return r;
  }
  r = gshscene_store_intern_path_tagged(store, path, &canon, out);
  gshgeom_path_destroy(path);
  return r;
}

GSHSCENE_Result gshscene_resource_primitive(const GSHSCENE_Resource * resource, GSHSCENE_Primitive * out) {
  if (!resource || !out || resource->kind != GSHSCENE_RESOURCE_PATH) {
    return GSHSCENE_ERR_INVALID;
  }
  *out = resource->primitive;
  return GSHSCENE_OK;
}
