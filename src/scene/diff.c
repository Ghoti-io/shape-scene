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
 * The diff of two versions: the changed IDs, and conservative damage (diff.h).
 *
 * The damage is found by walking both versions' occurrences with the one walk
 * (walk.c) and adding the bounds of every shape and stroke occurrence (a stroke's
 * by SK10's conservative outset of its centerline) that lies inside a
 * marked subtree.  A node is marked in a version when its own properties differ
 * from the other version's (then its whole subtree is suspect), or when it is a
 * child that the other version's list for the same parent lacks, or when the
 * order of the children both versions have differs (then the parent is marked).
 */

#include <ghoti.io/shape-scene/macros.h>

#include "internal.h"

#include <ghoti.io/shape-geometry/device.h>

#include <stdlib.h>
#include <string.h>

struct GSHSCENE_Diff {
  GSHSCENE_Allocator alloc;
  size_t count;
  GSHSCENE_Diff_Entry * entries;
  GSHSCENE_Damage damage;
  int foreground_changed;
  int palette_changed;
};

static int foreground_differs(const GSHSCENE_Color * a, const GSHSCENE_Color * b) {
  for (int i = 0; i < 4; i++) {
    if (gshgeom_f64_bits(a->rgba[i]) != gshgeom_f64_bits(b->rgba[i])) {
      return 1;
    }
  }
  return !gshscene_space_equal(&a->space, &b->space);
}

/* --- the changed ids --------------------------------------------------------------------------------- */

static int node_differs(const GSHSCENE_Node_Rec * a, const GSHSCENE_Node_Rec * b) {
  return a != b && !gshscene_node_equal(a, b);
}

/* Walk two ascending arrays together; with @p fill NULL, only count the differences. */
static size_t entries_pass(const struct GSHSCENE_Version * a, const struct GSHSCENE_Version * b,
    GSHSCENE_Diff_Entry * fill) {
  size_t i = 0;
  size_t j = 0;
  size_t n = 0;
  while (i < a->count || j < b->count) {
    GSHSCENE_Diff_Kind kind = GSHSCENE_DIFF_CHANGED;
    uint64_t id;
    int differs;
    if (j >= b->count || (i < a->count && a->nodes[i]->id < b->nodes[j]->id)) {
      id = a->nodes[i]->id;
      kind = GSHSCENE_DIFF_REMOVED;
      differs = 1;
      i++;
    } else if (i >= a->count || b->nodes[j]->id < a->nodes[i]->id) {
      id = b->nodes[j]->id;
      kind = GSHSCENE_DIFF_ADDED;
      differs = 1;
      j++;
    } else {
      id = a->nodes[i]->id;
      differs = node_differs(a->nodes[i], b->nodes[j]);
      i++;
      j++;
    }
    if (differs) {
      if (fill) {
        fill[n].id = id;
        fill[n].kind = kind;
      }
      n++;
    }
  }
  return n;
}

GSHSCENE_Result gshscene_diff_entries(const struct GSHSCENE_Version * a, const struct GSHSCENE_Version * b,
    GSHSCENE_Diff_Entry ** out, size_t * count) {
  size_t n = entries_pass(a, b, NULL);
  GSHSCENE_Diff_Entry * entries = NULL;
  if (n) {
    entries = n > SIZE_MAX / sizeof *entries ? NULL : gshscene_alloc(&a->core->alloc, n * sizeof *entries);
    if (!entries) {
      return GSHSCENE_ERR_OOM;
    }
    entries_pass(a, b, entries);
  }
  *out = entries;
  *count = n;
  return GSHSCENE_OK;
}

/* --- rounding outward, without libm -------------------------------------------------------------------- */

/* Beyond this magnitude a double is not converted: the damage is unbounded. */
#define RANGE_LIMIT 4.0e18

/* The magnitude of |x| below 2^62 truncated toward zero, whether a fraction was dropped, and the sign, by integer
 * arithmetic on the bits: a 32-bit x86 target converts a double and an int64 through the x87 stack (and its control word),
 * which the determinism gate refuses, and the result here is the same on every target. */
static void split_i64(double x, uint64_t * magnitude, int * fraction, int * negative) {
  uint64_t bits = gshgeom_f64_bits(x);
  *negative = (int)(bits >> 63);
  int exponent = (int)((bits >> 52) & 0x7ff);
  uint64_t mantissa = bits & ((UINT64_C(1) << 52) - 1);
  *magnitude = 0;
  *fraction = 0;
  if (exponent == 0) { /* zero, or a subnormal below one */
    *fraction = mantissa != 0;
    return;
  }
  mantissa |= UINT64_C(1) << 52;
  int shift = exponent - 1075;
  if (shift >= 0) { /* |x| < 2^62 makes this at most 9 */
    *magnitude = mantissa << shift;
  } else if (shift <= -64) {
    *fraction = 1;
  } else {
    *magnitude = mantissa >> -shift;
    *fraction = (mantissa & ((UINT64_C(1) << -shift) - 1)) != 0;
  }
}

static int floor_i64(double x, int64_t * out) {
  if (!(x > -RANGE_LIMIT && x < RANGE_LIMIT)) {
    return 0;
  }
  uint64_t magnitude;
  int fraction, negative;
  split_i64(x, &magnitude, &fraction, &negative);
  int64_t t = (int64_t)magnitude;
  *out = negative ? -t - (fraction ? 1 : 0) : t;
  return 1;
}

static int ceil_i64(double x, int64_t * out) {
  if (!(x > -RANGE_LIMIT && x < RANGE_LIMIT)) {
    return 0;
  }
  uint64_t magnitude;
  int fraction, negative;
  split_i64(x, &magnitude, &fraction, &negative);
  int64_t t = (int64_t)magnitude;
  *out = negative ? -t : t + (fraction ? 1 : 0);
  return 1;
}

/* --- marks ------------------------------------------------------------------------------------------------ */

static int u64_compare(const void * pa, const void * pb) {
  uint64_t a = *(const uint64_t *)pa;
  uint64_t b = *(const uint64_t *)pb;
  return a < b ? -1 : (a > b ? 1 : 0);
}

static int sorted_contains(const uint64_t * sorted, size_t n, uint64_t id) {
  size_t lo = 0;
  size_t hi = n;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (sorted[mid] == id) {
      return 1;
    }
    if (sorted[mid] < id) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return 0;
}

typedef struct Marks {
  unsigned char * a; /* per node of the first version */
  unsigned char * b; /* per node of the second */
} Marks;

static void mark(const struct GSHSCENE_Version * v, unsigned char * marks, uint64_t id) {
  size_t at = gshscene_nodes_find(v->nodes, v->count, id);
  if (at != GSHSCENE_NONE) {
    marks[at] = 1;
  }
}

/* The children of a group that one version's list has and the other's lacks, marked in the first version; and whether
 * the children both have are in different orders. */
static GSHSCENE_Result mark_children(const GSHSCENE_Allocator * alloc, const struct GSHSCENE_Version * from,
    unsigned char * marks, const GSHSCENE_Node_Rec * mine, const uint64_t * theirs_sorted, size_t theirs_n,
    uint64_t * common_out, size_t * common_n) {
  (void)alloc;
  size_t common = 0;
  for (size_t i = 0; i < mine->child_count; i++) {
    if (sorted_contains(theirs_sorted, theirs_n, mine->children[i])) {
      common_out[common++] = mine->children[i];
    } else {
      mark(from, marks, mine->children[i]);
    }
  }
  *common_n = common;
  return GSHSCENE_OK;
}

static GSHSCENE_Result compute_marks(const GSHSCENE_Allocator * alloc, const struct GSHSCENE_Version * a,
    const struct GSHSCENE_Version * b, const GSHSCENE_Diff_Entry * entries, size_t n, Marks * marks) {
  for (size_t e = 0; e < n; e++) {
    if (entries[e].kind != GSHSCENE_DIFF_CHANGED) {
      continue;
    }
    const GSHSCENE_Node_Rec * na = gshscene_version_find(a, entries[e].id);
    const GSHSCENE_Node_Rec * nb = gshscene_version_find(b, entries[e].id);
    if (!gshscene_props_equal(&na->p, &nb->p)) {
      mark(a, marks->a, entries[e].id);
      mark(b, marks->b, entries[e].id);
      continue;
    }
    if (na->p.kind != GSHSCENE_NODE_GROUP) {
      continue;
    }
    int same_children = na->child_count == nb->child_count &&
                        (na->child_count == 0 ||
                            memcmp(na->children, nb->children, na->child_count * sizeof *na->children) == 0);
    if (same_children) {
      continue;
    }
    /* Sorted copies for membership, and the children both have in each version's order. */
    size_t room = na->child_count + nb->child_count;
    uint64_t * scratch = gshscene_alloc(alloc, (room ? room : 1) * 4 * sizeof *scratch);
    if (!scratch) {
      return GSHSCENE_ERR_OOM;
    }
    uint64_t * sorted_a = scratch;
    uint64_t * sorted_b = sorted_a + na->child_count;
    uint64_t * common_a = sorted_b + nb->child_count;
    uint64_t * common_b = common_a + na->child_count;
    if (na->child_count) {
      memcpy(sorted_a, na->children, na->child_count * sizeof *sorted_a);
      qsort(sorted_a, na->child_count, sizeof *sorted_a, u64_compare);
    }
    if (nb->child_count) {
      memcpy(sorted_b, nb->children, nb->child_count * sizeof *sorted_b);
      qsort(sorted_b, nb->child_count, sizeof *sorted_b, u64_compare);
    }
    size_t ca = 0;
    size_t cb = 0;
    mark_children(alloc, a, marks->a, na, sorted_b, nb->child_count, common_a, &ca);
    mark_children(alloc, b, marks->b, nb, sorted_a, na->child_count, common_b, &cb);
    if (ca != cb || (ca && memcmp(common_a, common_b, ca * sizeof *common_a) != 0)) {
      mark(a, marks->a, entries[e].id);
      mark(b, marks->b, entries[e].id);
    }
    gshscene_free(alloc, scratch);
  }
  return GSHSCENE_OK;
}

/* --- bounds ------------------------------------------------------------------------------------------------- */

typedef struct Damage_Walk {
  const GSHSCENE_Allocator * alloc; /* a font run's provider paths (fast tier) */
  const struct GSHSCENE_Version * version;
  const unsigned char * marks;
  const GSHGEOM_Map * device;
  size_t inside;
  int foreground; /* the foreground differs between the versions: the shapes that paint with it are damaged */
  int palette;    /* likewise the palette (N14): the shapes that paint from it */
  int any;
  int unbounded;
  double min_x, min_y, max_x, max_y;
} Damage_Walk;

static void add_point(Damage_Walk * d, double x, double y) {
  if (!d->any) {
    d->min_x = d->max_x = x;
    d->min_y = d->max_y = y;
    d->any = 1;
    return;
  }
  d->min_x = x < d->min_x ? x : d->min_x;
  d->max_x = x > d->max_x ? x : d->max_x;
  d->min_y = y < d->min_y ? y : d->min_y;
  d->max_y = y > d->max_y ? y : d->max_y;
}

/* The four corners of a box in the path's own space, mapped, added. */
static void add_box(Damage_Walk * d, const GSHGEOM_Map * map, double x0, double y0, double x1, double y1) {
  const double xs[2] = {x0, x1};
  const double ys[2] = {y0, y1};
  for (int i = 0; i < 2; i++) {
    for (int j = 0; j < 2; j++) {
      GSHGEOM_Point p = {xs[i], ys[j], 0.0};
      GSHGEOM_Point q;
      if (gshgeom_map_apply_point(map, p, &q) != GSHGEOM_OK) {
        d->unbounded = 1;
        return;
      }
      add_point(d, q.x, q.y);
    }
  }
}

/**
 * A run from a font's bounds (glyph.h), as a pixel sink draws it: on the fast tier the control box of each glyph's
 * device path (the provider's, so hinting is inside it); on the general route each glyph's design box under its map.
 * An unbound face has none.  A glyph the provider refuses is not drawn and adds nothing.
 */
static GSHSCENE_Result add_font_run(Damage_Walk * d, const GSHSCENE_Resource * resource, size_t count,
    const GSHGEOM_Map * composed) {
  GSHSCENE_Glyph_Route route;
  if (gshscene_glyph_route(resource, composed, GSHSCENE_TARGET_RASTER, &route) != GSHSCENE_OK ||
      route.reason == GSHSCENE_REASON_FACE_UNBOUND) {
    d->unbounded = 1;
    return GSHSCENE_OK;
  }
  for (size_t i = 0; i < count && !d->unbounded; i++) {
    if (route.route == GSHSCENE_GLYPH_FAST) {
      GSHSCENE_Glyph_Origin origin;
      GSHGEOM_Device_Path * path = NULL;
      GSHSCENE_Glyph_Note note;
      GSHSCENE_Result r = gshscene_glyph_origin(resource, composed, i, &origin);
      if (r == GSHSCENE_OK) {
        r = gshscene_glyph_device_path(resource, &route, i, &origin, d->alloc, NULL, &path, &note);
      }
      if (r == GSHSCENE_ERR_OOM) {
        return r;
      }
      if (r != GSHSCENE_OK) {
        d->unbounded = 1;
        break;
      }
      size_t n = gshgeom_device_path_element_count(path);
      for (size_t k = 0; k < n; k++) {
        GSHGEOM_Device_Element e;
        if (gshgeom_device_path_element(path, k, &e) == GSHGEOM_OK) {
          for (unsigned j = 0; j < e.count && j < 3; j++) {
            add_point(d, (double)e.p[j].x / 64.0, (double)e.p[j].y / 64.0);
          }
        }
      }
      gshgeom_device_path_destroy(path);
      continue;
    }
    GSHSCENE_Glyph_Box box = {0}; /* written only on success */
    GSHGEOM_Map map;
    if (gshscene_glyph_box(resource, i, &box) != GSHSCENE_OK || box.empty) {
      continue; /* not drawn, or nothing to draw */
    }
    if (gshscene_glyph_map(resource, composed, i, &map) != GSHSCENE_OK) {
      d->unbounded = 1;
      break;
    }
    add_box(d, &map, (double)box.x_min / 64.0, (double)box.y_min / 64.0, (double)box.x_max / 64.0,
        (double)box.y_max / 64.0);
  }
  return GSHSCENE_OK;
}

/* A stroke occurrence's bounds (SK10): its centerline's model box through the style's conservative outset, in pixels
 * of the damage's map. */
static void add_stroke(Damage_Walk * d, const GSHSCENE_Node_Rec * stroke, const GSHGEOM_Map * composed) {
  GSHGEOM_Bounds centerline;
  GSHGEOM_Bounds b;
  if (gshscene_centerline_box(stroke->p.resource, &centerline) != GSHSCENE_OK ||
      gshscene_stroke_bounds(stroke->p.style, &centerline, composed, GSHSCENE_UNIT_PIXEL, &b) != GSHSCENE_OK) {
    d->unbounded = 1; /* an unbound face, or a box the map cannot take: nothing can be said of it */
    return;
  }
  if (!b.empty) {
    add_point(d, b.min.x, b.min.y);
    add_point(d, b.max.x, b.max.y);
  }
}

/* A shape or stroke occurrence's bounds. */
static GSHSCENE_Result add_shape(Damage_Walk * d, const GSHSCENE_Walk * walk, const GSHSCENE_Node_Rec * shape,
    const GSHGEOM_Map * item_map) {
  if (shape->p.kind == GSHSCENE_NODE_PLACEHOLDER) {
    return GSHSCENE_OK; /* PH7: it draws nothing, so no pixel of the target changes where it stands */
  }
  GSHGEOM_Map composed;
  GSHSCENE_Result result = gshscene_walk_compose(walk, item_map, d->device, &composed);
  if (result == GSHSCENE_ERR_LIMIT) {
    d->unbounded = 1;
    return GSHSCENE_OK;
  }
  if (result != GSHSCENE_OK) {
    return result;
  }
  if (shape->p.kind == GSHSCENE_NODE_STROKE) {
    add_stroke(d, shape, &composed);
    return GSHSCENE_OK;
  }
  const GSHSCENE_Resource * resource = shape->p.resource;
  if (gshscene_resource_kind(resource) == GSHSCENE_RESOURCE_PATH) {
    const GSHGEOM_Path * path = gshscene_resource_path(resource);
    GSHGEOM_Bounds b;
    if (gshgeom_path_dimension(path) != 2 || gshgeom_path_bounds(path, &b) != GSHGEOM_OK) {
      d->unbounded = 1;
    } else if (!b.empty) {
      add_box(d, &composed, b.min.x, b.min.y, b.max.x, b.max.y);
    }
    return GSHSCENE_OK;
  }
  if (gshscene_resource_kind(resource) == GSHSCENE_RESOURCE_PLANE) {
    d->unbounded = 1; /* PL5: a plane has no box; no node names one today (check_region), and if one did the damage is unbounded */
    return GSHSCENE_OK;
  }
  GSHSCENE_Glyph_Run run;
  if (gshscene_resource_glyph_run(resource, &run) != GSHSCENE_OK) {
    d->unbounded = 1;
    return GSHSCENE_OK;
  }
  if (run.source == GSHSCENE_GLYPHS_FROM_FONT) {
    return add_font_run(d, resource, run.glyph_count, &composed);
  }
  for (size_t i = 0; i < run.glyph_count && !d->unbounded; i++) {
    const GSHGEOM_Path * path = gshscene_resource_path(run.glyphs[i].outline);
    GSHGEOM_Bounds b;
    if (gshgeom_path_dimension(path) != 2 || gshgeom_path_bounds(path, &b) != GSHGEOM_OK) {
      d->unbounded = 1;
    } else if (!b.empty) {
      add_box(d, &composed, b.min.x + run.glyphs[i].x, b.min.y + run.glyphs[i].y, b.max.x + run.glyphs[i].x,
          b.max.y + run.glyphs[i].y);
    }
  }
  return GSHSCENE_OK;
}

static int is_marked(const Damage_Walk * d, const GSHSCENE_Node_Rec * node) {
  size_t at = gshscene_nodes_find(d->version->nodes, d->version->count, node->id);
  return at != GSHSCENE_NONE && d->marks[at];
}

static GSHSCENE_Result damage_group_enter(
    void * ctx, const GSHSCENE_Walk * walk, const GSHSCENE_Node_Rec * group, const GSHGEOM_Map * base) {
  (void)walk;
  (void)base;
  Damage_Walk * d = ctx;
  d->inside += is_marked(d, group) ? 1u : 0u;
  /* An operator that changes the backdrop where the group has no content reaches beyond its bounds (N13). */
  if (d->inside > 0 && gshscene_composite_is_unbounded((GSHSCENE_Composite)group->p.composite)) {
    d->unbounded = 1;
  }
  return GSHSCENE_OK;
}

static GSHSCENE_Result damage_group_leave(void * ctx, const GSHSCENE_Walk * walk, const GSHSCENE_Node_Rec * group) {
  (void)walk;
  Damage_Walk * d = ctx;
  d->inside -= is_marked(d, group) ? 1u : 0u;
  return GSHSCENE_OK;
}

/** N13: whether a paint reads the foreground: a foreground paint, or a gradient with a foreground stop (G3). */
static int paints_with_foreground(const GSHSCENE_Paint * paint) {
  return paint->kind == GSHSCENE_PAINT_FOREGROUND ||
      (paint->kind == GSHSCENE_PAINT_GRADIENT && paint->resource->u.gradient.foreground);
}

/** N13: whether a paint reads the palette: a palette paint, or a gradient with a palette stop (G3). */
static int paints_from_palette(const GSHSCENE_Paint * paint) {
  return paint->kind == GSHSCENE_PAINT_PALETTE ||
      (paint->kind == GSHSCENE_PAINT_GRADIENT && paint->resource->u.gradient.palette_need != 0);
}

static GSHSCENE_Result damage_shape(
    void * ctx, const GSHSCENE_Walk * walk, const GSHSCENE_Node_Rec * shape, const GSHGEOM_Map * map) {
  Damage_Walk * d = ctx;
  if (d->inside > 0 || is_marked(d, shape) || (d->foreground && paints_with_foreground(&shape->p.paint)) ||
      (d->palette && paints_from_palette(&shape->p.paint))) {
    return add_shape(d, walk, shape, map);
  }
  return GSHSCENE_OK;
}

static GSHSCENE_Result damage_instance(void * ctx, const GSHSCENE_Walk * walk, const GSHSCENE_Node_Rec * instance,
    const GSHGEOM_Map * map, int * descend) {
  (void)walk;
  (void)map;
  Damage_Walk * d = ctx;
  d->inside += is_marked(d, instance) ? 1u : 0u;
  *descend = 1;
  return GSHSCENE_OK;
}

static GSHSCENE_Result damage_instance_leave(void * ctx, const GSHSCENE_Walk * walk, const GSHSCENE_Node_Rec * instance) {
  (void)walk;
  Damage_Walk * d = ctx;
  d->inside -= is_marked(d, instance) ? 1u : 0u;
  return GSHSCENE_OK;
}

static const GSHSCENE_Walk_Ops damage_ops = {
    damage_group_enter, damage_group_leave, damage_shape, damage_instance, damage_instance_leave, NULL, NULL};

static GSHSCENE_Result walk_marked(const struct GSHSCENE_Version * v, const unsigned char * marks,
    const GSHGEOM_Map * device, Damage_Walk * d) {
  d->version = v;
  d->marks = marks;
  d->device = device;
  d->inside = 0;
  GSHGEOM_Map identity;
  gshgeom_map_identity(2, &identity);
  const GSHSCENE_Node_Rec * root = gshscene_version_find(v, GSHSCENE_ROOT_ID);
  if (!root) {
    return GSHSCENE_ERR_INTERNAL;
  }
  GSHSCENE_Result result = gshscene_walk(v, root, 0, &identity, &damage_ops, d);
  if (result == GSHSCENE_ERR_LIMIT) { /* a composed map out of range: nothing can be said of it */
    d->unbounded = 1;
    return GSHSCENE_OK;
  }
  return result;
}

static GSHSCENE_Result compute_damage(const struct GSHSCENE_Version * a, const struct GSHSCENE_Version * b,
    const GSHGEOM_Map * device, double aa, const GSHSCENE_Diff_Entry * entries, size_t n, int foreground,
    int palette, GSHSCENE_Damage * out) {
  memset(out, 0, sizeof *out);
  if (device->dim != 2) {
    out->unbounded = 1;
    return GSHSCENE_OK;
  }
  if (n == 0 && !foreground && !palette) {
    out->empty = 1;
    return GSHSCENE_OK;
  }
  const GSHSCENE_Allocator * alloc = &a->core->alloc;
  Marks marks;
  marks.a = gshscene_calloc(alloc, a->count, 1);
  marks.b = gshscene_calloc(alloc, b->count, 1);
  if (!marks.a || !marks.b) {
    gshscene_free(alloc, marks.a);
    gshscene_free(alloc, marks.b);
    return GSHSCENE_ERR_OOM;
  }
  GSHSCENE_Result result = compute_marks(alloc, a, b, entries, n, &marks);
  Damage_Walk d;
  memset(&d, 0, sizeof d);
  d.alloc = alloc;
  d.foreground = foreground;
  d.palette = palette;
  if (result == GSHSCENE_OK) {
    result = walk_marked(a, marks.a, device, &d);
  }
  if (result == GSHSCENE_OK) {
    result = walk_marked(b, marks.b, device, &d);
  }
  gshscene_free(alloc, marks.a);
  gshscene_free(alloc, marks.b);
  if (result != GSHSCENE_OK) {
    return result;
  }
  if (d.unbounded) {
    out->unbounded = 1;
    return GSHSCENE_OK;
  }
  if (!d.any) {
    out->empty = 1;
    return GSHSCENE_OK;
  }
  GSHSCENE_Rect r;
  if (!floor_i64(d.min_x - aa, &r.x0) || !floor_i64(d.min_y - aa, &r.y0) || !ceil_i64(d.max_x + aa, &r.x1) ||
      !ceil_i64(d.max_y + aa, &r.y1)) {
    out->unbounded = 1;
    return GSHSCENE_OK;
  }
  out->rect = r;
  return GSHSCENE_OK;
}

/* --- the public diff ------------------------------------------------------------------------------------------- */

GSHSCENE_Result gshscene_diff(const GSHSCENE_Version * a, const GSHSCENE_Version * b, const GSHGEOM_Map * map,
    const GSHSCENE_Damage_Options * options, GSHSCENE_Diff ** out) {
  if (!a || !b || !out || a->core != b->core) {
    return GSHSCENE_ERR_INVALID;
  }
  GSHGEOM_Map device;
  if (map) {
    if (gshgeom_map_check(map) != GSHGEOM_OK) {
      return GSHSCENE_ERR_INVALID;
    }
    device = *map;
  } else {
    gshgeom_map_identity(2, &device);
  }
  double aa = GSHSCENE_AA_SUPPORT_DEFAULT;
  if (options) {
    if (!(options->aa_support >= 0.0) || !gshscene_finite(options->aa_support)) {
      return GSHSCENE_ERR_INVALID;
    }
    aa = options->aa_support;
  }
  const GSHSCENE_Allocator * alloc = &a->core->alloc;
  GSHSCENE_Diff * diff = gshscene_calloc(alloc, 1, sizeof *diff);
  if (!diff) {
    return GSHSCENE_ERR_OOM;
  }
  diff->alloc = *alloc;
  GSHSCENE_Result result = gshscene_diff_entries(a, b, &diff->entries, &diff->count);
  diff->foreground_changed = foreground_differs(&a->foreground, &b->foreground);
  diff->palette_changed = a->palette != b->palette;
  if (result == GSHSCENE_OK) {
    result = compute_damage(a, b, &device, aa, diff->entries, diff->count, diff->foreground_changed,
        diff->palette_changed, &diff->damage);
  }
  if (result != GSHSCENE_OK) {
    gshscene_diff_destroy(diff);
    return result;
  }
  *out = diff;
  return GSHSCENE_OK;
}

size_t gshscene_diff_count(const GSHSCENE_Diff * diff) {
  return diff ? diff->count : 0;
}

GSHSCENE_Result gshscene_diff_get(
    const GSHSCENE_Diff * diff, size_t index, uint64_t * out_id, GSHSCENE_Diff_Kind * out_kind) {
  if (!diff || !out_id || !out_kind || index >= diff->count) {
    return GSHSCENE_ERR_INVALID;
  }
  *out_id = diff->entries[index].id;
  *out_kind = diff->entries[index].kind;
  return GSHSCENE_OK;
}

int gshscene_diff_foreground_changed(const GSHSCENE_Diff * diff) {
  return diff ? diff->foreground_changed : 0;
}

int gshscene_diff_palette_changed(const GSHSCENE_Diff * diff) {
  return diff ? diff->palette_changed : 0;
}

GSHSCENE_Result gshscene_diff_damage(const GSHSCENE_Diff * diff, GSHSCENE_Damage * out) {
  if (!diff || !out) {
    return GSHSCENE_ERR_INVALID;
  }
  *out = diff->damage;
  return GSHSCENE_OK;
}

void gshscene_diff_destroy(GSHSCENE_Diff * diff) {
  if (diff) {
    GSHSCENE_Allocator alloc = diff->alloc;
    gshscene_free(&alloc, diff->entries);
    gshscene_free(&alloc, diff);
  }
}
