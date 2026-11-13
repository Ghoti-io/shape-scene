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
 * Picking (pick.h, PK1 to PK12): a walk of a closed list's expansion in paint order that decides one point against the
 * region of each fill and stroke and the clips in force.
 *
 * The walk is iterative, with an explicit stack of frames (one per level of picture), so that the depth of nested pictures
 * costs memory from the allocator given and never the machine stack.  The instance maps of the levels above the current
 * one are `maps[0 .. level)`; a clip push records the level it was pushed at and so which prefix of them it is placed
 * under, and its answer is made when first needed and kept (the point does not change).
 */

#include <ghoti.io/shape-scene/macros.h>

#include "../internal.h"

#include <ghoti.io/shape-scene/clip_union.h>
#include <ghoti.io/shape-scene/glyph.h>
#include <ghoti.io/shape-scene/pick.h>
#include <ghoti.io/shape-scene/report.h>
#include <ghoti.io/shape-scene/resource.h>
#include <ghoti.io/shape-scene/sink.h>
#include <ghoti.io/shape-scene/stroke.h>
#include <ghoti.io/shape-scene/version.h>

#include <ghoti.io/shape-geometry/flatten.h>
#include <ghoti.io/shape-geometry/hit.h>
#include <ghoti.io/shape-geometry/stroke.h>

#include <string.h>

/* --- the four-valued answer --------------------------------------------------------------------- */

enum { S_OUT = 0, S_BOUND = 1, S_IN = 2, S_UNK = 3 };

/** An answer and, when it is undecided, why. */
typedef struct St {
  uint8_t s;
  uint8_t why;
} St;

static St st_make(int s) {
  St r = {(uint8_t)s, 0};
  return r;
}

static St st_unknown(GSHSCENE_Pick_Why why) {
  St r = {S_UNK, (uint8_t)why};
  return r;
}

/** PK3's intersection: outside if any part is, else undecided if any is, else the lesser of inside and the boundary. */
static St st_and(St a, St b) {
  if (a.s == S_OUT || b.s == S_OUT) {
    return st_make(S_OUT);
  }
  if (a.s == S_UNK) {
    return a;
  }
  if (b.s == S_UNK) {
    return b;
  }
  return st_make(a.s < b.s ? a.s : b.s);
}

/** PK3's union: inside if any part is, else undecided if any is, else the greater of outside and the boundary. */
static St st_or(St a, St b) {
  if (a.s == S_IN || b.s == S_IN) {
    return st_make(S_IN);
  }
  if (a.s == S_UNK) {
    return a;
  }
  if (b.s == S_UNK) {
    return b;
  }
  return st_make(a.s > b.s ? a.s : b.s);
}

/** PK3's complement. */
static St st_not(St a) {
  if (a.s == S_IN) {
    return st_make(S_OUT);
  }
  if (a.s == S_OUT) {
    return st_make(S_IN);
  }
  return a;
}

/* --- the pick ------------------------------------------------------------------------------------ */

typedef struct Hit {
  uint8_t state;
  uint8_t why;
  uint8_t kind;
  size_t offset; /* into the pool */
  size_t count;
} Hit;

struct GSHSCENE_Pick {
  GSHSCENE_Allocator alloc;
  Hit * hits;
  size_t hit_count;
  size_t hit_cap;
  size_t * pool;
  size_t pool_count;
  size_t pool_cap;
  size_t total; /* items with a hit, kept or not */
  size_t first; /* the index of the oldest hit kept, when max_hits dropped the earlier ones */
};

typedef struct Frame {
  const GSHSCENE_List * list;
  size_t count;
  size_t next;
  size_t clip_base;
  size_t group_base;
} Frame;

typedef struct Clip {
  const GSHSCENE_Item * item;
  size_t level;
  int known;
  St st;
} Clip;

typedef struct Ctx {
  const GSHSCENE_Allocator * alloc;
  GSHGEOM_Allocator * galloc;
  GSHGEOM_Limits limits;
  GSHGEOM_Map target;
  GSHGEOM_Device_Point point;
  uint32_t flags;
  size_t max_hits;
  GSHSCENE_Pick * pick;
  Frame * frames;
  GSHGEOM_Map * maps;
  size_t * chain;
  GSHGEOM_Map * scratch; /* a chain of maps being composed: the levels' maps and the item's, and a glyph's */
  size_t levels_cap;
  Clip * clips;
  size_t clip_count;
  size_t clip_cap;
  uint8_t * groups; /* 1 for a group that hides what it holds (opacity 0 under ::GSHSCENE_PICK_VISIBLE) */
  size_t group_count;
  size_t group_cap;
  size_t hidden; /* groups open that hide */
} Ctx;

/** Grow an array of @p count elements of @p size bytes to hold at least @p need; NULL on failure (the old one is then valid). */
static void * grow(const GSHSCENE_Allocator * alloc, void * old, size_t count, size_t * cap, size_t need, size_t size) {
  if (need <= *cap) {
    return old;
  }
  size_t next = *cap ? *cap : 8;
  while (next < need) {
    if (next > SIZE_MAX / 2) {
      return NULL;
    }
    next *= 2;
  }
  if (next > SIZE_MAX / size) {
    return NULL;
  }
  void * grown = gshscene_alloc(alloc, next * size);
  if (!grown) {
    return NULL;
  }
  if (count) {
    memcpy(grown, old, count * size);
  }
  gshscene_free(alloc, old);
  *cap = next;
  return grown;
}

/* --- the winding of a region ----------------------------------------------------------------------- */

/** The winding numbers of the contours of one shape, summed (AA2: their windings add), and what stopped it. */
typedef struct Acc {
  int64_t winding;
  int boundary;
  int unknown; /* a ::GSHSCENE_Pick_Why, 0 for none */
} Acc;

/** Add one flat path's winding at the point. */
static GSHSCENE_Result acc_flat(Ctx * c, const GSHGEOM_Flat_Path * flat, Acc * acc) {
  int64_t w = 0;
  int b = 0;
  if (gshgeom_flat_path_winding(flat, c->point, &w, &b) != GSHGEOM_OK) {
    return GSHSCENE_ERR_INTERNAL;
  }
  acc->winding += w;
  acc->boundary |= b;
  return GSHSCENE_OK;
}

/** The point against the closed box of a device path: 1 when it can be on the region, 0 when it cannot (CU15). */
static int in_box(const Ctx * c, const GSHGEOM_Device_Box * box) {
  return !box->empty && c->point.x >= box->x0 && c->point.x <= box->x1 && c->point.y >= box->y0 && c->point.y <= box->y1;
}

/** Flatten a device path at the default tolerance and add its winding; the device path is destroyed. */
static GSHSCENE_Result acc_device(Ctx * c, GSHGEOM_Device_Path * device, Acc * acc) {
  GSHGEOM_Flat_Path * flat = NULL;
  GSHGEOM_Result g = gshgeom_device_path_flatten(device, c->galloc, &c->limits, &flat);
  gshgeom_device_path_destroy(device);
  if (g == GSHGEOM_ERR_OOM) {
    return GSHSCENE_ERR_OOM;
  }
  if (g != GSHGEOM_OK) {
    acc->unknown = GSHSCENE_PICK_WHY_LIMIT; /* more points than the limit */
    return GSHSCENE_OK;
  }
  GSHSCENE_Result r = acc_flat(c, flat, acc);
  gshgeom_flat_path_destroy(flat);
  return r;
}

/** A model path under the composed map @p map: the box test first, then the flattened path as a sink makes it. */
static GSHSCENE_Result acc_path(Ctx * c, const GSHGEOM_Path * path, const GSHGEOM_Map * map, Acc * acc) {
  GSHGEOM_Device_Box box;
  GSHGEOM_Result g = gshgeom_device_box(path, map, &box);
  if (g != GSHGEOM_OK) {
    acc->unknown = GSHSCENE_PICK_WHY_LIMIT; /* a coordinate past 26.6 */
    return GSHSCENE_OK;
  }
  if (!in_box(c, &box)) {
    return GSHSCENE_OK; /* every point of the region is in the box: winding 0, on no edge */
  }
  GSHGEOM_Device_Path * device = NULL;
  g = gshgeom_device_path_from_model(path, map, c->galloc, &c->limits, &device);
  if (g == GSHGEOM_ERR_OOM) {
    return GSHSCENE_ERR_OOM;
  }
  if (g != GSHGEOM_OK) {
    acc->unknown = GSHSCENE_PICK_WHY_LIMIT;
    return GSHSCENE_OK;
  }
  return acc_device(c, device, acc);
}

/** The chain of maps for an item placed at @p level: the instance maps above it, then @p own.  Returns the length. */
static size_t place(Ctx * c, size_t level, const GSHGEOM_Map * own) {
  for (size_t i = 0; i < level; i++) {
    c->scratch[i] = c->maps[i];
  }
  c->scratch[level] = *own;
  return level + 1;
}

/** Compose the chain of @p n maps and the target's. */
static GSHSCENE_Result compose(Ctx * c, size_t n, GSHGEOM_Map * out) {
  return gshgeom_map_compose_chain(c->scratch, n, &c->target, out) == GSHGEOM_OK ? GSHSCENE_OK : GSHSCENE_ERR_LIMIT;
}

/** A glyph of a run from a font, by the route the raster sinks take (glyph.h): its device path into @p acc. */
static GSHSCENE_Result acc_font_glyph(Ctx * c, const GSHSCENE_Resource * run, const GSHGEOM_Map * map,
    const GSHSCENE_Glyph_Route * route, size_t i, Acc * acc) {
  GSHGEOM_Device_Path * device = NULL;
  GSHSCENE_Glyph_Note note;
  memset(&note, 0, sizeof note);
  GSHSCENE_Result r;
  if (route->route == GSHSCENE_GLYPH_FAST) {
    GSHSCENE_Glyph_Origin origin;
    r = gshscene_glyph_origin(run, map, i, &origin);
    if (r == GSHSCENE_OK) {
      r = gshscene_glyph_device_path(run, route, i, &origin, c->galloc, &c->limits, &device, &note);
    }
  } else {
    GSHGEOM_Map glyph_map;
    GSHGEOM_Path * outline = NULL;
    r = gshscene_glyph_map(run, map, i, &glyph_map);
    if (r == GSHSCENE_OK) {
      r = gshscene_glyph_outline(run, i, c->galloc, &c->limits, &outline, &note);
    }
    if (r == GSHSCENE_OK && outline) {
      GSHGEOM_Result g = gshgeom_device_path_from_model(outline, &glyph_map, c->galloc, &c->limits, &device);
      r = g == GSHGEOM_OK ? GSHSCENE_OK : g == GSHGEOM_ERR_OOM ? GSHSCENE_ERR_OOM : GSHSCENE_ERR_LIMIT;
    }
    gshgeom_path_destroy(outline);
  }
  if (r == GSHSCENE_ERR_OOM) {
    gshgeom_device_path_destroy(device);
    return GSHSCENE_ERR_OOM;
  }
  if (r != GSHSCENE_OK || note.entry_class == GSHSCENE_ENTRY_REFUSED) {
    gshgeom_device_path_destroy(device); /* an origin past 26.6, a map past the bounds, or a glyph the provider could not make */
    acc->unknown = r != GSHSCENE_OK ? GSHSCENE_PICK_WHY_LIMIT : GSHSCENE_PICK_WHY_GLYPH;
    return GSHSCENE_OK;
  }
  if (!device) {
    return GSHSCENE_OK; /* a glyph with no outline draws nothing */
  }
  return acc_device(c, device, acc);
}

/** The state of an accumulated winding under a rule (HT3, HT5). */
static St acc_state(const Acc * acc, GSHGEOM_Fill_Rule rule) {
  if (acc->unknown) {
    return st_unknown((GSHSCENE_Pick_Why)acc->unknown);
  }
  if (acc->boundary) {
    return st_make(S_BOUND);
  }
  const int inside = rule == GSHGEOM_FILL_EVEN_ODD ? (acc->winding & 1) != 0 : acc->winding != 0;
  return st_make(inside ? S_IN : S_OUT);
}

/**
 * The state of the point against the region of the fill or clip shape @p item places at @p level: a path, the glyphs of a run
 * (their windings added), or the plane.
 */
static GSHSCENE_Result shape_region(Ctx * c, const GSHSCENE_Item * item, size_t level, St * out) {
  const GSHSCENE_Resource * res = item->resource;
  if (gshscene_resource_kind(res) == GSHSCENE_RESOURCE_PLANE) {
    *out = st_make(S_IN); /* PL1: every point the clips allow */
    return GSHSCENE_OK;
  }
  Acc acc;
  memset(&acc, 0, sizeof acc);
  size_t n = place(c, level, &item->map);
  GSHGEOM_Map map;
  const GSHGEOM_Path * path = gshscene_resource_path(res);
  if (path) {
    if (compose(c, n, &map) != GSHSCENE_OK) {
      *out = st_unknown(GSHSCENE_PICK_WHY_LIMIT);
      return GSHSCENE_OK;
    }
    GSHSCENE_Result r = acc_path(c, path, &map, &acc);
    if (r != GSHSCENE_OK) {
      return r;
    }
    *out = acc_state(&acc, item->fill_rule);
    return GSHSCENE_OK;
  }
  GSHSCENE_Glyph_Run run;
  if (gshscene_resource_glyph_run(res, &run) != GSHSCENE_OK) {
    *out = st_make(S_OUT); /* nothing else is a shape */
    return GSHSCENE_OK;
  }
  if (run.source == GSHSCENE_GLYPHS_FROM_FONT) {
    GSHSCENE_Glyph_Route route;
    if (compose(c, n, &map) != GSHSCENE_OK || gshscene_glyph_route(res, &map, GSHSCENE_TARGET_RASTER, &route) != GSHSCENE_OK) {
      *out = st_unknown(GSHSCENE_PICK_WHY_LIMIT);
      return GSHSCENE_OK;
    }
    if (route.entry_class == GSHSCENE_ENTRY_REFUSED) {
      *out = st_unknown(GSHSCENE_PICK_WHY_GLYPH);
      return GSHSCENE_OK;
    }
    for (size_t i = 0; i < run.glyph_count && !acc.unknown; i++) {
      GSHSCENE_Result r = acc_font_glyph(c, res, &map, &route, i, &acc);
      if (r != GSHSCENE_OK) {
        return r;
      }
    }
  } else {
    for (size_t i = 0; i < run.glyph_count && !acc.unknown; i++) {
      const GSHGEOM_Path * outline = gshscene_resource_path(run.glyphs[i].outline);
      if (!outline) {
        continue; /* a glyph with no outline draws nothing */
      }
      GSHGEOM_Map move;
      if (gshgeom_map_translate(2, run.glyphs[i].x, run.glyphs[i].y, 0.0, &move) != GSHGEOM_OK) {
        acc.unknown = GSHSCENE_PICK_WHY_LIMIT;
        break;
      }
      c->scratch[n] = move;
      if (compose(c, n + 1, &map) != GSHSCENE_OK) {
        acc.unknown = GSHSCENE_PICK_WHY_LIMIT;
        break;
      }
      GSHSCENE_Result r = acc_path(c, outline, &map, &acc);
      if (r != GSHSCENE_OK) {
        return r;
      }
    }
  }
  *out = acc_state(&acc, item->fill_rule);
  return GSHSCENE_OK;
}

/** A union clip's region (CU1, CI1): the union over members of each member intersected with its inner clips. */
static GSHSCENE_Result union_region(Ctx * c, const GSHSCENE_Item * item, size_t level, St * out) {
  const size_t members = gshscene_resource_clip_union_count(item->resource);
  St result = st_make(S_OUT);
  for (size_t m = 0; m < members; m++) {
    GSHSCENE_Clip_Member info;
    GSHSCENE_Item part;
    if (gshscene_resource_clip_union_member(item->resource, m, &info) != GSHSCENE_OK ||
        gshscene_clip_union_member_item(item, m, &part) != GSHSCENE_OK) {
      return GSHSCENE_ERR_INTERNAL;
    }
    St member;
    GSHSCENE_Result r = shape_region(c, &part, level, &member);
    for (size_t k = 0; r == GSHSCENE_OK && k < info.inner_count; k++) {
      if (gshscene_clip_union_inner_item(item, m, k, &part) != GSHSCENE_OK) {
        return GSHSCENE_ERR_INTERNAL;
      }
      St inner;
      r = shape_region(c, &part, level, &inner);
      member = st_and(member, inner);
    }
    if (r != GSHSCENE_OK) {
      return r;
    }
    result = st_or(result, member);
  }
  *out = result;
  return GSHSCENE_OK;
}

/** The region of a clip push. */
static GSHSCENE_Result clip_region(Ctx * c, const GSHSCENE_Item * item, size_t level, St * out) {
  if (gshscene_resource_kind(item->resource) == GSHSCENE_RESOURCE_CLIP_UNION) {
    return union_region(c, item, level, out);
  }
  return shape_region(c, item, level, out);
}

/* --- a stroke's region ------------------------------------------------------------------------------- */

/** The state of the point against one nonzero-wound part of an outline, which may be absent. */
static GSHSCENE_Result part_state(Ctx * c, const GSHGEOM_Flat_Path * flat, GSHGEOM_Device_Point p, GSHGEOM_Fill_Rule rule, St * out) {
  if (!flat) {
    *out = st_make(S_OUT);
    return GSHSCENE_OK;
  }
  Acc acc;
  memset(&acc, 0, sizeof acc);
  const GSHGEOM_Device_Point saved = c->point;
  c->point = p;
  GSHSCENE_Result r = acc_flat(c, flat, &acc);
  c->point = saved;
  if (r != GSHSCENE_OK) {
    return r;
  }
  *out = acc_state(&acc, rule);
  return GSHSCENE_OK;
}

/** SK5: the body, together with the aligned part where the clip part selects it (or its complement). */
static GSHSCENE_Result stroke_region(Ctx * c, const GSHSCENE_Item * item, size_t level, St * out) {
  if (gshscene_resource_kind(item->style) == GSHSCENE_RESOURCE_HATCH) {
    *out = st_unknown(GSHSCENE_PICK_WHY_HATCH);
    return GSHSCENE_OK;
  }
  GSHGEOM_Stroke params;
  const GSHGEOM_Path * path = gshscene_resource_path(item->resource);
  GSHSCENE_Glyph_Run run;
  memset(&run, 0, sizeof run);
  const int is_run = !path && gshscene_resource_glyph_run(item->resource, &run) == GSHSCENE_OK;
  if ((!path && !is_run) || gshscene_resource_stroke_geometry(item->style, &params) != GSHSCENE_OK) {
    return GSHSCENE_ERR_INTERNAL; /* a style and a centerline the store holds pass the checks */
  }
  size_t n = place(c, level, &item->map);
  GSHGEOM_Map composed;
  GSHGEOM_Map canon;
  int64_t sx = 0;
  int64_t sy = 0;
  if (compose(c, n, &composed) != GSHSCENE_OK || gshgeom_stroke_canonical_map(&composed, &canon, &sx, &sy) != GSHGEOM_OK) {
    *out = st_unknown(GSHSCENE_PICK_WHY_LIMIT);
    return GSHSCENE_OK;
  }
  GSHGEOM_Path * centerline = NULL;
  if (is_run) {
    /* ST1: the one function that defines a run's centerline; a glyph it left out is a glyph the provider refused. */
    GSHSCENE_Glyph_Note * notes = NULL;
    if (run.glyph_count > 0) {
      notes = gshscene_calloc(c->alloc, run.glyph_count, sizeof *notes);
      if (!notes) {
        return GSHSCENE_ERR_OOM;
      }
    }
    GSHSCENE_Result cr = gshscene_glyph_run_centerline(item->resource, c->galloc, &c->limits, &centerline, notes);
    int refused = 0;
    for (size_t i = 0; notes && i < run.glyph_count; i++) {
      refused |= notes[i].entry_class == GSHSCENE_ENTRY_REFUSED;
    }
    gshscene_free(c->alloc, notes);
    if (cr == GSHSCENE_ERR_OOM) {
      return cr;
    }
    if (cr != GSHSCENE_OK || refused) {
      gshgeom_path_destroy(centerline);
      *out = st_unknown(cr != GSHSCENE_OK ? GSHSCENE_PICK_WHY_LIMIT : GSHSCENE_PICK_WHY_GLYPH);
      return GSHSCENE_OK;
    }
    path = centerline;
  }
  GSHGEOM_Stroke_Outline * outline = NULL;
  GSHGEOM_Result g = gshgeom_stroke_expand(path, &params, &canon, c->galloc, &c->limits, &outline);
  gshgeom_path_destroy(centerline);
  if (g == GSHGEOM_ERR_OOM) {
    return GSHSCENE_ERR_OOM;
  }
  if (g != GSHGEOM_OK) {
    *out = st_unknown(GSHSCENE_PICK_WHY_LIMIT);
    return GSHSCENE_OK;
  }
  if (gshgeom_stroke_outline_status(outline) != GSHGEOM_STROKE_DRAWN) {
    gshgeom_stroke_outline_destroy(outline);
    *out = st_make(S_OUT); /* SG14, SG15: nothing is covered */
    return GSHSCENE_OK;
  }
  /* The outline is of the canonical map, a whole-pixel shift from the map's: the point moves back by it instead. */
  const int64_t px = (int64_t)c->point.x - 64 * sx;
  const int64_t py = (int64_t)c->point.y - 64 * sy;
  if (px < INT32_MIN || px > INT32_MAX || py < INT32_MIN || py > INT32_MAX) {
    gshgeom_stroke_outline_destroy(outline); /* past every coordinate of an outline: the point is on none of it */
    *out = st_make(S_OUT);
    return GSHSCENE_OK;
  }
  GSHGEOM_Device_Point p = {(int32_t)px, (int32_t)py};
  St body;
  GSHSCENE_Result r = part_state(c, gshgeom_stroke_outline_body(outline), p, GSHGEOM_FILL_NONZERO, &body);
  const GSHGEOM_Flat_Path * aligned = gshgeom_stroke_outline_aligned(outline);
  if (r == GSHSCENE_OK && aligned) {
    St al;
    St cl;
    r = part_state(c, aligned, p, GSHGEOM_FILL_NONZERO, &al);
    if (r == GSHSCENE_OK) {
      r = part_state(c, gshgeom_stroke_outline_clip(outline), p, gshgeom_stroke_outline_clip_rule(outline), &cl);
    }
    if (r == GSHSCENE_OK) {
      if (gshgeom_stroke_outline_clip_complement(outline)) {
        cl = st_not(cl);
      }
      body = st_or(body, st_and(al, cl));
    }
  }
  gshgeom_stroke_outline_destroy(outline);
  *out = body;
  return r;
}

/* --- recording a hit ----------------------------------------------------------------------------------- */

static GSHSCENE_Result record(Ctx * c, St st, GSHSCENE_Item_Kind kind, size_t chain_count) {
  GSHSCENE_Pick * pick = c->pick;
  pick->total++;
  Hit * hits = grow(c->alloc, pick->hits, pick->hit_count, &pick->hit_cap, pick->hit_count + 1, sizeof *pick->hits);
  if (!hits) {
    return GSHSCENE_ERR_OOM;
  }
  pick->hits = hits;
  size_t * pool = grow(c->alloc, pick->pool, pick->pool_count, &pick->pool_cap, pick->pool_count + chain_count, sizeof *pick->pool);
  if (!pool) {
    return GSHSCENE_ERR_OOM;
  }
  pick->pool = pool;
  Hit * h = &pick->hits[pick->hit_count++];
  h->state = st.s == S_IN ? GSHSCENE_PICK_INSIDE : st.s == S_BOUND ? GSHSCENE_PICK_BOUNDARY : GSHSCENE_PICK_UNDECIDED;
  h->why = st.s == S_UNK ? st.why : 0;
  h->kind = (uint8_t)kind;
  h->offset = pick->pool_count;
  h->count = chain_count;
  memcpy(pick->pool + pick->pool_count, c->chain, chain_count * sizeof *c->chain);
  pick->pool_count += chain_count;
  return GSHSCENE_OK;
}

/** Test one fill or stroke at @p level, whose chain is `chain[0 .. level]`. */
static GSHSCENE_Result test_item(Ctx * c, const GSHSCENE_Item * item, size_t level) {
  if ((c->flags & GSHSCENE_PICK_VISIBLE) && item->paint.rgba[3] == 0.0) {
    return GSHSCENE_OK;
  }
  St st;
  GSHSCENE_Result r = item->kind == GSHSCENE_ITEM_STROKE ? stroke_region(c, item, level, &st) : shape_region(c, item, level, &st);
  if (r != GSHSCENE_OK || st.s == S_OUT) {
    return r;
  }
  for (size_t i = 0; i < c->clip_count; i++) {
    Clip * clip = &c->clips[i];
    if (!clip->known) {
      r = clip_region(c, clip->item, clip->level, &clip->st);
      if (r != GSHSCENE_OK) {
        return r;
      }
      clip->known = 1;
    }
    st = st_and(st, clip->st);
    if (st.s == S_OUT) {
      return GSHSCENE_OK;
    }
  }
  return record(c, st, item->kind, level + 1);
}

/* --- the walk ---------------------------------------------------------------------------------------------- */

static GSHSCENE_Result ensure_levels(Ctx * c, size_t need) {
  if (need <= c->levels_cap) {
    return GSHSCENE_OK;
  }
  size_t cap = c->levels_cap ? c->levels_cap : 8;
  while (cap < need) {
    if (cap > SIZE_MAX / 4 / sizeof(GSHGEOM_Map)) {
      return GSHSCENE_ERR_OOM;
    }
    cap *= 2;
  }
  /* All four arrays are made before any is swapped in, so a failure leaves the walk as it was. */
  Frame * frames = gshscene_alloc(c->alloc, cap * sizeof *frames);
  GSHGEOM_Map * maps = gshscene_alloc(c->alloc, cap * sizeof *maps);
  size_t * chain = gshscene_alloc(c->alloc, cap * sizeof *chain);
  GSHGEOM_Map * scratch = gshscene_alloc(c->alloc, (cap + 2) * sizeof *scratch);
  if (!frames || !maps || !chain || !scratch) {
    gshscene_free(c->alloc, frames);
    gshscene_free(c->alloc, maps);
    gshscene_free(c->alloc, chain);
    gshscene_free(c->alloc, scratch);
    return GSHSCENE_ERR_OOM;
  }
  if (c->levels_cap) {
    memcpy(frames, c->frames, c->levels_cap * sizeof *frames);
    memcpy(maps, c->maps, c->levels_cap * sizeof *maps);
    memcpy(chain, c->chain, c->levels_cap * sizeof *chain);
  }
  gshscene_free(c->alloc, c->frames);
  gshscene_free(c->alloc, c->maps);
  gshscene_free(c->alloc, c->chain);
  gshscene_free(c->alloc, c->scratch);
  c->frames = frames;
  c->maps = maps;
  c->chain = chain;
  c->scratch = scratch;
  c->levels_cap = cap;
  return GSHSCENE_OK;
}

static GSHSCENE_Result push_frame(Ctx * c, size_t level, const GSHSCENE_List * list) {
  GSHSCENE_Result r = ensure_levels(c, level + 1);
  if (r != GSHSCENE_OK) {
    return r;
  }
  Frame * f = &c->frames[level];
  f->list = list;
  f->count = gshscene_list_count(list);
  f->next = 0;
  f->clip_base = c->clip_count;
  f->group_base = c->group_count;
  return GSHSCENE_OK;
}

static GSHSCENE_Result push_clip(Ctx * c, const GSHSCENE_Item * item, size_t level) {
  Clip * clips = grow(c->alloc, c->clips, c->clip_count, &c->clip_cap, c->clip_count + 1, sizeof *clips);
  if (!clips) {
    return GSHSCENE_ERR_OOM;
  }
  c->clips = clips;
  Clip * clip = &c->clips[c->clip_count++];
  memset(clip, 0, sizeof *clip);
  clip->item = item;
  clip->level = level;
  return GSHSCENE_OK;
}

static GSHSCENE_Result push_group(Ctx * c, int hides) {
  uint8_t * groups = grow(c->alloc, c->groups, c->group_count, &c->group_cap, c->group_count + 1, sizeof *groups);
  if (!groups) {
    return GSHSCENE_ERR_OOM;
  }
  c->groups = groups;
  c->groups[c->group_count++] = (uint8_t)hides;
  c->hidden += (size_t)hides;
  return GSHSCENE_OK;
}

/** Leave a frame: what its list left pushed (nothing, for a balanced list) is discarded. */
static void pop_frame_state(Ctx * c, const Frame * f) {
  while (c->group_count > f->group_base) {
    c->hidden -= c->groups[--c->group_count];
  }
  c->clip_count = f->clip_base;
}

static GSHSCENE_Result walk(Ctx * c, const GSHSCENE_List * top) {
  GSHSCENE_Result r = push_frame(c, 0, top);
  size_t level = 0;
  while (r == GSHSCENE_OK) {
    Frame * f = &c->frames[level];
    if (f->next == f->count) {
      pop_frame_state(c, f);
      if (level == 0) {
        break;
      }
      level--;
      continue;
    }
    const size_t index = f->next++;
    const GSHSCENE_Item * item = NULL;
    if (gshscene_list_item(f->list, index, &item) != GSHSCENE_OK) {
      return GSHSCENE_ERR_INTERNAL;
    }
    c->chain[level] = index;
    switch (item->kind) {
      case GSHSCENE_ITEM_FILL:
      case GSHSCENE_ITEM_STROKE:
        if (c->hidden == 0) {
          r = test_item(c, item, level);
        }
        break;
      case GSHSCENE_ITEM_CLIP_PUSH:
        r = push_clip(c, item, level);
        break;
      case GSHSCENE_ITEM_CLIP_POP:
        if (c->clip_count > f->clip_base) {
          c->clip_count--;
        }
        break;
      case GSHSCENE_ITEM_GROUP_PUSH:
        r = push_group(c, (c->flags & GSHSCENE_PICK_VISIBLE) && item->group.opacity == 0.0);
        break;
      case GSHSCENE_ITEM_GROUP_POP:
        if (c->group_count > f->group_base) {
          c->hidden -= c->groups[--c->group_count];
        }
        break;
      case GSHSCENE_ITEM_INSTANCE: {
        const GSHSCENE_List * picture = c->hidden == 0 ? gshscene_resource_picture(item->resource) : NULL;
        if (picture) {
          r = ensure_levels(c, level + 2);
          if (r == GSHSCENE_OK) {
            c->maps[level] = item->map;
            r = push_frame(c, level + 1, picture);
            level++;
          }
        }
        break;
      }
      default: /* a placeholder draws nothing (PK7) */
        break;
    }
  }
  return r;
}

/* --- the entry points ------------------------------------------------------------------------------------- */

static void pick_free(GSHSCENE_Pick * pick) {
  if (!pick) {
    return;
  }
  const GSHSCENE_Allocator alloc = pick->alloc;
  gshscene_free(&alloc, pick->hits);
  gshscene_free(&alloc, pick->pool);
  gshscene_free(&alloc, pick);
}

void gshscene_pick_destroy(GSHSCENE_Pick * pick) {
  pick_free(pick);
}

GSHSCENE_Result gshscene_pick_list(const GSHSCENE_Allocator * alloc, const GSHSCENE_List * list,
    const GSHSCENE_Pick_Params * params, GSHSCENE_Pick ** out) {
  if (!list || !params || !out || !gshscene_list_is_closed(list) || (params->flags & ~GSHSCENE_PICK_VISIBLE)) {
    return GSHSCENE_ERR_INVALID;
  }
  for (size_t i = 0; i < sizeof params->_reserved; i++) {
    if (params->_reserved[i]) {
      return GSHSCENE_ERR_INVALID;
    }
  }
  if (!alloc) {
    alloc = gshscene_allocator_default();
  }
  GSHGEOM_Map target = params->target;
  if (target.dim == 0) {
    gshgeom_map_identity(2, &target);
  }
  if (target.dim != 2 || gshgeom_map_check(&target) != GSHGEOM_OK) {
    return GSHSCENE_ERR_INVALID;
  }
  GSHSCENE_Pick * pick = gshscene_calloc(alloc, 1, sizeof *pick);
  if (!pick) {
    return GSHSCENE_ERR_OOM;
  }
  pick->alloc = *alloc;
  GSHSCENE_Allocator own = *alloc; /* the one the geometry and the scratch take from; read through a pointer that outlives the calls */
  Ctx c;
  memset(&c, 0, sizeof c);
  c.alloc = &own;
  c.galloc = &own;
  gshgeom_limits_default(&c.limits);
  c.target = target;
  c.point = params->point;
  c.flags = params->flags;
  c.max_hits = params->max_hits;
  c.pick = pick;
  GSHSCENE_Result r = walk(&c, list);
  gshscene_free(&own, c.frames);
  gshscene_free(&own, c.maps);
  gshscene_free(&own, c.chain);
  gshscene_free(&own, c.scratch);
  gshscene_free(&own, c.clips);
  gshscene_free(&own, c.groups);
  if (r != GSHSCENE_OK) {
    pick_free(pick);
    return r;
  }
  /* PK12: keep the topmost max_hits, which are the last made (the walk goes in paint order). */
  if (c.max_hits && pick->hit_count > c.max_hits) {
    pick->first = pick->hit_count - c.max_hits;
  }
  *out = pick;
  return GSHSCENE_OK;
}

GSHSCENE_Result gshscene_pick_version(const GSHSCENE_Allocator * alloc, const GSHSCENE_Version * version,
    const GSHSCENE_Pick_Params * params, GSHSCENE_Pick ** out) {
  if (!version) {
    return GSHSCENE_ERR_INVALID;
  }
  const GSHSCENE_List * list = NULL;
  GSHSCENE_Result r = gshscene_version_list(version, &list);
  return r == GSHSCENE_OK ? gshscene_pick_list(alloc, list, params, out) : r;
}

size_t gshscene_pick_count(const GSHSCENE_Pick * pick) {
  return pick ? pick->hit_count - pick->first : 0;
}

size_t gshscene_pick_total(const GSHSCENE_Pick * pick) {
  return pick ? pick->total : 0;
}

/** Entry @p index topmost first: the hits were made in paint order, so the last made is entry 0. */
static const Hit * hit_at(const GSHSCENE_Pick * pick, size_t index) {
  return &pick->hits[pick->hit_count - 1 - index];
}

GSHSCENE_Result gshscene_pick_entry(const GSHSCENE_Pick * pick, size_t index, GSHSCENE_Pick_Entry * out) {
  if (!pick || !out || index >= gshscene_pick_count(pick)) {
    return GSHSCENE_ERR_INVALID;
  }
  const Hit * h = hit_at(pick, index);
  out->state = (GSHSCENE_Pick_State)h->state;
  out->why = (GSHSCENE_Pick_Why)h->why;
  out->kind = (GSHSCENE_Item_Kind)h->kind;
  out->chain = pick->pool + h->offset;
  out->chain_count = h->count;
  return GSHSCENE_OK;
}

GSHSCENE_Result gshscene_pick_resolve(const GSHSCENE_Pick * pick, const GSHSCENE_Version * version, size_t index,
    uint64_t * out_node, uint64_t * out_path, size_t capacity, size_t * out_count) {
  if (!pick || !version || !out_node || !out_path || !out_count || index >= gshscene_pick_count(pick)) {
    return GSHSCENE_ERR_INVALID;
  }
  const Hit * h = hit_at(pick, index);
  if (h->count == 0 || capacity < h->count) {
    return GSHSCENE_ERR_INVALID;
  }
  const size_t * chain = pick->pool + h->offset;
  uint64_t table = 0;
  uint64_t node = 0;
  for (size_t i = 0; i < h->count; i++) {
    GSHSCENE_Result result = gshscene_version_item_node(version, table, chain[i], &node);
    if (result != GSHSCENE_OK) {
      return result;
    }
    if (i + 1 < h->count) {
      GSHSCENE_Node_Info info;
      if (gshscene_version_node(version, node, &info) != GSHSCENE_OK || info.node.kind != GSHSCENE_NODE_INSTANCE) {
        return GSHSCENE_ERR_INVALID;
      }
      table = info.node.target;
    }
    out_path[i] = node;
  }
  *out_node = node;
  *out_count = h->count;
  return GSHSCENE_OK;
}

const char * gshscene_pick_state_string(GSHSCENE_Pick_State state) {
  switch (state) {
    case GSHSCENE_PICK_BOUNDARY:
      return "boundary";
    case GSHSCENE_PICK_INSIDE:
      return "inside";
    case GSHSCENE_PICK_UNDECIDED:
      return "undecided";
    case GSHSCENE_PICK_STATE_COUNT:
      break;
  }
  return "unknown pick state";
}

const char * gshscene_pick_why_string(GSHSCENE_Pick_Why why) {
  switch (why) {
    case GSHSCENE_PICK_WHY_NONE:
      return "none";
    case GSHSCENE_PICK_WHY_HATCH:
      return "hatch";
    case GSHSCENE_PICK_WHY_GLYPH:
      return "glyph";
    case GSHSCENE_PICK_WHY_LIMIT:
      return "limit";
    case GSHSCENE_PICK_WHY_COUNT:
      break;
  }
  return "unknown pick reason";
}
