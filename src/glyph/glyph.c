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
 * A run from a font at render time (glyph.h): the route, the two routes' maps,
 * the snapped map, and the calls to a face's provider with what each answer
 * means for the report.
 */

#include <ghoti.io/shape-scene/macros.h>

#include "../internal.h"

#include <ghoti.io/shape-geometry/device.h>

#include <string.h>

/* --- sizes ------------------------------------------------------------------------ */

GSHSCENE_Result gshscene_glyph_scale(uint32_t ppem, uint32_t upem, int32_t * out) {
  if (!out || ppem < 1 || ppem > GSHSCENE_GLYPH_MAX_PPEM || upem < GSHSCENE_FACE_UPEM_MIN ||
      upem > GSHSCENE_FACE_UPEM_MAX) {
    return GSHSCENE_ERR_INVALID;
  }
  /* round(ppem * 2^16 / upem), half up: floor((2 ppem 2^16 + upem) / (2 upem)).  At most 4096 * 2^16 / 16 = 2^24. */
  uint64_t twice = ((uint64_t)ppem << 17) + upem;
  *out = (int32_t)(twice / (2u * (uint64_t)upem));
  return GSHSCENE_OK;
}

/**
 * Split @p a into a high half of 26 bits and the rest (Veltkamp), so that a product of halves is exact.  No FMA is
 * involved (the library is built with contraction off) and no value here comes near overflow: a map's entries and a
 * run's size are at most 2^100.
 */
static void split(double a, double * hi, double * lo) {
  double c = 134217729.0 * a; /* 2^27 + 1 */
  double big = c - a;
  *hi = c - big;
  *lo = a - *hi;
}

/** Whether @p p, the rounded product of @p a and @p b, is their exact product (Dekker's error term is zero). */
static int product_exact(double a, double b, double p) {
  double ah;
  double al;
  double bh;
  double bl;
  split(a, &ah, &al);
  split(b, &bh, &bl);
  double err = ((ah * bh - p) + ah * bl + al * bh) + al * bl;
  return err == 0.0;
}

/* --- the route ------------------------------------------------------------------- */

/** The face of a run from a font, or NULL. */
static const GSHSCENE_Resource * run_face(const GSHSCENE_Resource * run) {
  if (!run || run->kind != GSHSCENE_RESOURCE_GLYPH_RUN || run->u.glyphs.run.source != GSHSCENE_GLYPHS_FROM_FONT) {
    return NULL;
  }
  return run->u.glyphs.run.face;
}

/** The general route for a reason of the map or the size: an entry under FONT, a refusal under strict, none else. */
static void general_for(GSHSCENE_Glyph_Route * r, GSHSCENE_Reason reason) {
  r->route = GSHSCENE_GLYPH_GENERAL;
  if (r->hint_mode == GSHSCENE_HINT_FONT_STRICT) {
    r->entry_class = GSHSCENE_ENTRY_REFUSED;
    r->reason = GSHSCENE_REASON_HINT_STRICT_ROUTE;
  } else if (r->hint_mode != GSHSCENE_HINT_NONE) {
    r->entry_class = GSHSCENE_ENTRY_HINT_STATUS;
    r->reason = reason;
  }
}

GSHSCENE_Result gshscene_glyph_route(
    const GSHSCENE_Resource * run, const GSHGEOM_Map * map, uint32_t target_kind, GSHSCENE_Glyph_Route * out) {
  const GSHSCENE_Resource * face = run_face(run);
  if (!face || !map || !out || map->dim != 2 || gshgeom_map_check(map) != GSHGEOM_OK) {
    return GSHSCENE_ERR_INVALID;
  }
  GSHSCENE_Glyph_Route r;
  memset(&r, 0, sizeof r);
  r.route = GSHSCENE_GLYPH_GENERAL;
  r.hint_mode = run->u.glyphs.run.hint_mode;
  /* 1: a face with no provider cannot be drawn at all. */
  if (!face->u.face.bound) {
    r.entry_class = GSHSCENE_ENTRY_REFUSED;
    r.reason = GSHSCENE_REASON_FACE_UNBOUND;
    *out = r;
    return GSHSCENE_OK;
  }
  const GSHSCENE_Face_Info * info = &face->u.face.info;
  r.upem = info->upem;
  r.face_flags = info->flags;
  int none = r.hint_mode == GSHSCENE_HINT_NONE;
  /* 2: a document has no pixels to hint to. */
  if (!gshscene_target_kind_has_pixels((GSHSCENE_Target_Kind)target_kind)) {
    *out = r;
    return GSHSCENE_OK;
  }
  /* 3: a provider with no fast tier. */
  if (!face->u.face.ops.glyph_device_path) {
    if (!none) {
      r.entry_class = GSHSCENE_ENTRY_HINT_STATUS;
      r.reason = GSHSCENE_REASON_HINT_NO_PROGRAM;
    }
    *out = r;
    return GSHSCENE_OK;
  }
  /* 4: the predicate (AD-5), with no y flip: a flipped map would draw the glyphs upside down. */
  int aligned = 0;
  GSHGEOM_Grid grid;
  memset(&grid, 0, sizeof grid);
  if (gshgeom_map_grid_aligned(map, &aligned, &grid) != GSHGEOM_OK) {
    return GSHSCENE_ERR_INVALID;
  }
  if (!aligned || grid.flip_y) {
    general_for(&r, GSHSCENE_REASON_HINT_ROUTE);
    *out = r;
    return GSHSCENE_OK;
  }
  /* 5: an exact integer ppem in [1, max_ppem] (D3).  The comparisons come first, so the conversion is in range; only
   * int32_t values cross between double and integer (i686 would take an unsigned one through x87). */
  double size = run->u.glyphs.run.size;
  double ppem = size * grid.scale;
  int32_t max = (int32_t)(info->max_ppem ? info->max_ppem : GSHSCENE_GLYPH_MAX_PPEM);
  if (!(ppem >= 1.0 && ppem <= (double)max) || (double)(int32_t)ppem != ppem || !product_exact(size, grid.scale, ppem)) {
    general_for(&r, GSHSCENE_REASON_HINT_SIZE);
    *out = r;
    return GSHSCENE_OK;
  }
  /* 6: the fast tier. */
  r.route = GSHSCENE_GLYPH_FAST;
  r.ppem = (uint32_t)(int32_t)ppem;
  if (gshscene_glyph_scale(r.ppem, r.upem, &r.scale) != GSHSCENE_OK) {
    return GSHSCENE_ERR_INTERNAL;
  }
  if (!none && !(info->flags & GSHSCENE_FACE_HINTING)) {
    r.entry_class = GSHSCENE_ENTRY_HINT_STATUS;
    r.reason = GSHSCENE_REASON_HINT_NO_PROGRAM;
  }
  *out = r;
  return GSHSCENE_OK;
}

/* --- the maps ---------------------------------------------------------------------- */

static GSHSCENE_Result from_geometry(GSHGEOM_Result r) {
  switch (r) {
    case GSHGEOM_OK:
      return GSHSCENE_OK;
    case GSHGEOM_ERR_LIMIT:
      return GSHSCENE_ERR_LIMIT;
    case GSHGEOM_ERR_OOM:
      return GSHSCENE_ERR_OOM;
    default:
      return GSHSCENE_ERR_INVALID;
  }
}

/** Glyph @p index of a run from a font, or NULL past the run. */
static const GSHSCENE_Glyph * run_glyph(const GSHSCENE_Resource * run, size_t index) {
  return index < run->u.glyphs.run.glyph_count ? &run->u.glyphs.glyphs[index] : NULL;
}

GSHSCENE_Result gshscene_glyph_map(
    const GSHSCENE_Resource * run, const GSHGEOM_Map * map, size_t index, GSHGEOM_Map * out) {
  const GSHSCENE_Resource * face = run_face(run);
  if (!face || !face->u.face.bound || !map || !out || !run_glyph(run, index)) {
    return GSHSCENE_ERR_INVALID;
  }
  const GSHSCENE_Glyph * g = run_glyph(run, index);
  GSHGEOM_Map translate;
  GSHGEOM_Map design;
  GSHGEOM_Map placed;
  /* map . translate(x, y) . design_to_model(size / upem), through the one composer (AD-24). */
  GSHGEOM_Result r = gshgeom_map_translate(2, g->x, g->y, 0.0, &translate);
  if (r == GSHGEOM_OK) {
    r = gshgeom_map_design_to_model(run->u.glyphs.run.size / (double)(int32_t)face->u.face.info.upem, 0.0, 0.0, &design);
    if (r == GSHGEOM_ERR_INVALID) {
      r = GSHGEOM_ERR_LIMIT; /* a scale that underflowed to zero: past the range the definition covers */
    }
  }
  if (r == GSHGEOM_OK) {
    r = gshgeom_map_compose(map, &translate, &placed);
  }
  if (r == GSHGEOM_OK) {
    r = gshgeom_map_compose(&placed, &design, out);
  }
  return from_geometry(r);
}

GSHSCENE_Result gshscene_glyph_origin(
    const GSHSCENE_Resource * run, const GSHGEOM_Map * map, size_t index, GSHSCENE_Glyph_Origin * out) {
  if (!run_face(run) || !map || !out || !run_glyph(run, index)) {
    return GSHSCENE_ERR_INVALID;
  }
  const GSHSCENE_Glyph * g = run_glyph(run, index);
  GSHGEOM_Point p = {g->x, g->y, 0.0};
  GSHGEOM_Device_Point d;
  GSHGEOM_Result r = gshgeom_device_point(map, p, &d);
  if (r != GSHGEOM_OK) {
    return from_geometry(r);
  }
  /* The whole pixel and the fraction, by a floor of either sign (no shift of a negative number). */
  int64_t fx = ((int64_t)d.x % 64 + 64) % 64;
  int64_t fy = ((int64_t)d.y % 64 + 64) % 64;
  out->x = d.x;
  out->y = d.y;
  out->px = (int32_t)(((int64_t)d.x - fx) / 64);
  out->py = (int32_t)(((int64_t)d.y - fy) / 64);
  out->fx = (uint32_t)fx;
  out->fy = (uint32_t)fy;
  return GSHSCENE_OK;
}

GSHSCENE_Result gshscene_glyph_snap_map(
    const GSHSCENE_Glyph_Route * route, const GSHSCENE_Glyph_Origin * origin, GSHGEOM_Map * out) {
  if (!route || !origin || !out || route->route != GSHSCENE_GLYPH_FAST) {
    return GSHSCENE_ERR_INVALID;
  }
  /* Every number here is exact: a 16.16 scale and a 26.6 origin are dyadic and short. */
  double s = (double)route->scale / 65536.0;
  const double m[4] = {s, 0.0, 0.0, -s};
  const double t[2] = {(double)origin->x / 64.0, (double)origin->y / 64.0};
  return from_geometry(gshgeom_map_from_matrix(2, m, t, out));
}

/* --- the provider ------------------------------------------------------------------- */

static void note_set(GSHSCENE_Glyph_Note * note, GSHSCENE_Entry_Class entry_class, GSHSCENE_Reason reason) {
  note->entry_class = (uint32_t)entry_class;
  note->reason = (uint32_t)reason;
}

/**
 * What a provider's failure means for a glyph: out of memory stops the render (returned); anything else refuses the
 * glyph (@p note), a limit as the sink's own, a strict program's failure as such, the rest as unavailable.
 */
static GSHSCENE_Result failure_note(GSHSCENE_Result result, GSHSCENE_Glyph_Note * note) {
  if (result == GSHSCENE_ERR_OOM) {
    return result;
  }
  if (result == GSHSCENE_ERR_LIMIT) {
    note_set(note, GSHSCENE_ENTRY_REFUSED, GSHSCENE_REASON_RESOURCE_LIMIT);
  } else if (result == GSHSCENE_ERR_REFUSED) {
    note_set(note, GSHSCENE_ENTRY_REFUSED, GSHSCENE_REASON_HINT_PROGRAM_FAILED);
  } else {
    note_set(note, GSHSCENE_ENTRY_REFUSED, GSHSCENE_REASON_GLYPH_UNAVAILABLE);
  }
  return GSHSCENE_OK;
}

/** What a fast-tier status means (glyph.h, D7). */
static void status_note(const GSHSCENE_Glyph_Route * route, uint32_t status, GSHSCENE_Glyph_Note * note) {
  if (status >= (uint32_t)GSHSCENE_HINT_STATUS_COUNT) {
    note_set(note, GSHSCENE_ENTRY_REFUSED, GSHSCENE_REASON_GLYPH_UNAVAILABLE);
    return;
  }
  if (route->hint_mode == GSHSCENE_HINT_NONE) {
    return;
  }
  switch ((GSHSCENE_Hint_Status)status) {
    case GSHSCENE_HINT_STATUS_NO_PROGRAM:
      /* A face without a program has the run's entry; a glyph of one with a program has its own. */
      if (route->face_flags & GSHSCENE_FACE_HINTING) {
        note_set(note, GSHSCENE_ENTRY_HINT_STATUS, GSHSCENE_REASON_HINT_NO_PROGRAM);
      }
      break;
    case GSHSCENE_HINT_STATUS_PROGRAM_FAILED:
      note_set(note, route->hint_mode == GSHSCENE_HINT_FONT_STRICT ? GSHSCENE_ENTRY_REFUSED : GSHSCENE_ENTRY_HINT_STATUS,
          GSHSCENE_REASON_HINT_PROGRAM_FAILED);
      break;
    case GSHSCENE_HINT_STATUS_WITH_ERRORS:
      note_set(note, GSHSCENE_ENTRY_HINT_STATUS, GSHSCENE_REASON_HINT_WITH_ERRORS);
      break;
    default:
      break;
  }
}

/** A device path's builder result as the note or the render's. */
static GSHSCENE_Result build_failure(GSHGEOM_Result r, GSHSCENE_Glyph_Note * note) {
  if (r == GSHGEOM_ERR_OOM) {
    return GSHSCENE_ERR_OOM;
  }
  note_set(note, GSHSCENE_ENTRY_REFUSED, GSHSCENE_REASON_RESOURCE_LIMIT);
  return GSHSCENE_OK;
}

/** Copy @p from moved by (@p dx, @p dy) 1/64 pixel into @p to; a point past the 26.6 range is a limit. */
static GSHGEOM_Result moved(const GSHGEOM_Device_Path * from, int64_t dx, int64_t dy, GSHGEOM_Device_Path * to) {
  size_t n = gshgeom_device_path_element_count(from);
  for (size_t i = 0; i < n; i++) {
    GSHGEOM_Device_Element e;
    GSHGEOM_Result r = gshgeom_device_path_element(from, i, &e);
    if (r != GSHGEOM_OK) {
      return GSHGEOM_ERR_INTERNAL;
    }
    GSHGEOM_Device_Point p[3];
    for (unsigned k = 0; k < e.count && k < 3; k++) {
      int64_t x = (int64_t)e.p[k].x + dx;
      int64_t y = (int64_t)e.p[k].y + dy;
      if (x < INT32_MIN || x > INT32_MAX || y < INT32_MIN || y > INT32_MAX) {
        return GSHGEOM_ERR_LIMIT;
      }
      p[k].x = (int32_t)x;
      p[k].y = (int32_t)y;
    }
    switch (e.verb) {
      case GSHGEOM_DEVICE_MOVE:
        r = gshgeom_device_path_move_to(to, p[0]);
        break;
      case GSHGEOM_DEVICE_LINE:
        r = gshgeom_device_path_line_to(to, p[0]);
        break;
      case GSHGEOM_DEVICE_QUADRATIC:
        r = gshgeom_device_path_quadratic_to(to, p[0], p[1]);
        break;
      case GSHGEOM_DEVICE_CUBIC:
        r = gshgeom_device_path_cubic_to(to, p[0], p[1], p[2]);
        break;
      case GSHGEOM_DEVICE_CLOSE:
        r = gshgeom_device_path_close(to);
        break;
      default:
        r = GSHGEOM_ERR_INTERNAL;
        break;
    }
    if (r != GSHGEOM_OK) {
      return r;
    }
  }
  return GSHGEOM_OK;
}

GSHSCENE_Result gshscene_glyph_device_path(const GSHSCENE_Resource * run, const GSHSCENE_Glyph_Route * route,
    size_t index, const GSHSCENE_Glyph_Origin * origin, const GSHGEOM_Allocator * alloc, const GSHGEOM_Limits * limits,
    GSHGEOM_Device_Path ** out, GSHSCENE_Glyph_Note * note) {
  const GSHSCENE_Resource * face = run_face(run);
  if (!face || !face->u.face.bound || !route || route->route != GSHSCENE_GLYPH_FAST || !origin || !out || !note ||
      !run_glyph(run, index) || !face->u.face.ops.glyph_device_path || origin->fx >= 64u || origin->fy >= 64u) {
    return GSHSCENE_ERR_INVALID;
  }
  *out = NULL;
  memset(note, 0, sizeof *note);
  GSHSCENE_Glyph_Request request;
  memset(&request, 0, sizeof request);
  request.glyph = run_glyph(run, index)->id;
  request.ppem = route->ppem;
  request.scale = route->scale;
  request.frac_x = origin->fx;
  request.frac_y = origin->fy;
  request.hint_mode = run->u.glyphs.run.hint_mode;
  request.hint_version = run->u.glyphs.run.hint_version;
  GSHGEOM_Device_Path * relative = NULL;
  GSHGEOM_Result g = gshgeom_device_path_create(alloc, limits, &relative);
  if (g != GSHGEOM_OK) {
    return build_failure(g, note);
  }
  GSHSCENE_Glyph_Answer answer = {GSHSCENE_HINT_STATUS_HINTED, 0};
  GSHSCENE_Result result =
      face->u.face.ops.glyph_device_path(face->u.face.ctx, &face->u.face.face, &request, relative, &answer);
  if (result != GSHSCENE_OK) {
    gshgeom_device_path_destroy(relative);
    return failure_note(result, note);
  }
  status_note(route, answer.status, note);
  note->status = answer.status;
  if (note->entry_class == GSHSCENE_ENTRY_REFUSED) {
    gshgeom_device_path_destroy(relative);
    return GSHSCENE_OK;
  }
  /* The whole pixel, added as an exact integer. */
  GSHGEOM_Device_Path * absolute = NULL;
  g = gshgeom_device_path_create(alloc, limits, &absolute);
  if (g == GSHGEOM_OK) {
    g = moved(relative, (int64_t)origin->px * 64, (int64_t)origin->py * 64, absolute);
  }
  gshgeom_device_path_destroy(relative);
  if (g != GSHGEOM_OK) {
    gshgeom_device_path_destroy(absolute);
    note->entry_class = 0;
    note->reason = 0;
    return g == GSHGEOM_ERR_INTERNAL ? GSHSCENE_ERR_INTERNAL : build_failure(g, note);
  }
  *out = absolute;
  return GSHSCENE_OK;
}

GSHSCENE_Result gshscene_glyph_outline(const GSHSCENE_Resource * run, size_t index, const GSHGEOM_Allocator * alloc,
    const GSHGEOM_Limits * limits, GSHGEOM_Path ** out, GSHSCENE_Glyph_Note * note) {
  const GSHSCENE_Resource * face = run_face(run);
  if (!face || !face->u.face.bound || !out || !note || !run_glyph(run, index)) {
    return GSHSCENE_ERR_INVALID;
  }
  *out = NULL;
  memset(note, 0, sizeof *note);
  GSHGEOM_Path * path = NULL;
  GSHGEOM_Result g = gshgeom_path_create(alloc, limits, 2, &path);
  if (g != GSHGEOM_OK) {
    return build_failure(g, note);
  }
  GSHSCENE_Result result =
      face->u.face.ops.glyph_outline(face->u.face.ctx, &face->u.face.face, run_glyph(run, index)->id, path);
  if (result != GSHSCENE_OK) {
    gshgeom_path_destroy(path);
    return failure_note(result, note);
  }
  *out = path;
  return GSHSCENE_OK;
}

GSHSCENE_Result gshscene_glyph_box(const GSHSCENE_Resource * run, size_t index, GSHSCENE_Glyph_Box * out) {
  const GSHSCENE_Resource * face = run_face(run);
  if (!face || !face->u.face.bound || !out || !run_glyph(run, index)) {
    return GSHSCENE_ERR_INVALID;
  }
  GSHSCENE_Glyph_Box box;
  memset(&box, 0, sizeof box);
  GSHSCENE_Result result =
      face->u.face.ops.glyph_box(face->u.face.ctx, &face->u.face.face, run_glyph(run, index)->id, &box);
  if (result == GSHSCENE_OK) {
    *out = box;
  }
  return result;
}
