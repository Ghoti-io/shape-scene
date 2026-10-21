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
 * The kinds of resource: copy, equality, hash and dump of a path, a glyph run,
 * a picture, a face and a palette (resource.h; a gradient's are gradient.c's).
 */

#include <ghoti.io/shape-scene/macros.h>

#include "../internal.h"

#include <ghoti.io/shape-geometry/curve.h>

#include <string.h>

/** The format of a resource's hash and dump, per kind (resource.h); every kind's is 1 until the suite's first release. */
#define RESOURCE_FORMAT_VERSION 1u
#define RUN_FORMAT_VERSION 1u

const char * gshscene_resource_kind_string(GSHSCENE_Resource_Kind kind) {
  switch (kind) {
    case GSHSCENE_RESOURCE_PATH:
      return "path";
    case GSHSCENE_RESOURCE_GLYPH_RUN:
      return "glyph-run";
    case GSHSCENE_RESOURCE_PICTURE:
      return "picture";
    case GSHSCENE_RESOURCE_GRADIENT:
      return "gradient";
    case GSHSCENE_RESOURCE_IMAGE:
      return "image";
    case GSHSCENE_RESOURCE_FACE:
      return "face";
    case GSHSCENE_RESOURCE_PALETTE:
      return "palette";
    case GSHSCENE_RESOURCE_KIND_COUNT:
      break;
  }
  return "unknown resource kind";
}

/* --- hashes ---------------------------------------------------------------- */

/* The content hash of a resource a run names (its face, a glyph's outline): 0 for none. */
static uint64_t outline_hash(const GSHSCENE_Resource * outline) {
  return outline ? gshscene_resource_hash(outline) : 0;
}

/** The format of a kind's hash and dump. */
static uint32_t kind_format(GSHSCENE_Resource_Kind kind) {
  return kind == GSHSCENE_RESOURCE_GLYPH_RUN ? RUN_FORMAT_VERSION : RESOURCE_FORMAT_VERSION;
}

static void hash_begin(GSHGEOM_Hash * h, GSHSCENE_Resource_Kind kind) {
  gshgeom_hash_init(h);
  gshgeom_hash_u32(h, kind_format(kind));
  gshgeom_hash_u32(h, (uint32_t)kind);
}

static uint64_t path_content_hash(const GSHGEOM_Path * path) {
  GSHGEOM_Hash h;
  hash_begin(&h, GSHSCENE_RESOURCE_PATH);
  gshgeom_hash_u64(&h, gshgeom_path_hash(path));
  return gshgeom_hash_value(&h);
}

static uint64_t run_content_hash(const GSHSCENE_Glyph_Run * run, const GSHSCENE_Glyph * glyphs) {
  GSHGEOM_Hash h;
  hash_begin(&h, GSHSCENE_RESOURCE_GLYPH_RUN);
  gshgeom_hash_u32(&h, (uint32_t)run->source);
  gshgeom_hash_u64(&h, outline_hash(run->face));
  gshgeom_hash_f64(&h, run->size);
  gshgeom_hash_u32(&h, run->hint_mode);
  gshgeom_hash_u32(&h, run->hint_version);
  gshgeom_hash_u64(&h, (uint64_t)run->glyph_count);
  for (size_t i = 0; i < run->glyph_count; i++) {
    gshgeom_hash_u32(&h, glyphs[i].id);
    gshgeom_hash_f64(&h, glyphs[i].x);
    gshgeom_hash_f64(&h, glyphs[i].y);
    gshgeom_hash_u64(&h, outline_hash(glyphs[i].outline));
  }
  return gshgeom_hash_value(&h);
}

/* A face's content: its key, index and coordinates (the binding is not content). */
static uint64_t face_content_hash(uint64_t font_key, uint32_t face_index, uint32_t count, const int16_t * coords) {
  GSHGEOM_Hash h;
  hash_begin(&h, GSHSCENE_RESOURCE_FACE);
  gshgeom_hash_u64(&h, font_key);
  gshgeom_hash_u32(&h, face_index);
  gshgeom_hash_u32(&h, count);
  for (uint32_t i = 0; i < count; i++) {
    gshgeom_hash_u32(&h, (uint32_t)(int32_t)coords[i]);
  }
  return gshgeom_hash_value(&h);
}

/* A palette's content: its entries, each a color (four f64 and a space). */
static uint64_t palette_content_hash(size_t count, const GSHSCENE_Color * colors) {
  GSHGEOM_Hash h;
  hash_begin(&h, GSHSCENE_RESOURCE_PALETTE);
  gshgeom_hash_u64(&h, (uint64_t)count);
  for (size_t i = 0; i < count; i++) {
    gshscene_color_hash(&h, &colors[i]);
  }
  return gshgeom_hash_value(&h);
}

static uint64_t picture_content_hash(const GSHSCENE_List * list) {
  GSHGEOM_Hash h;
  hash_begin(&h, GSHSCENE_RESOURCE_PICTURE);
  gshgeom_hash_u64(&h, gshscene_list_hash(list));
  return gshgeom_hash_value(&h);
}

/* --- path equality --------------------------------------------------------- */

static int point_equal(const GSHGEOM_Point * a, const GSHGEOM_Point * b) {
  return gshgeom_f64_bits(a->x) == gshgeom_f64_bits(b->x) && gshgeom_f64_bits(a->y) == gshgeom_f64_bits(b->y) &&
         gshgeom_f64_bits(a->z) == gshgeom_f64_bits(b->z);
}

/* How many points after the start a verb stores, and how many weights. */
static size_t verb_points(GSHGEOM_Verb verb) {
  switch (verb) {
    case GSHGEOM_VERB_MOVE:
    case GSHGEOM_VERB_LINE:
      return 1;
    case GSHGEOM_VERB_QUADRATIC:
    case GSHGEOM_VERB_RATIONAL_QUADRATIC:
      return 2;
    case GSHGEOM_VERB_CUBIC:
    case GSHGEOM_VERB_RATIONAL_CUBIC:
      return 3;
    default:
      return 0;
  }
}

static size_t verb_weights(GSHGEOM_Verb verb) {
  if (verb == GSHGEOM_VERB_RATIONAL_QUADRATIC) {
    return 3;
  }
  return verb == GSHGEOM_VERB_RATIONAL_CUBIC ? 4 : 0;
}

int gshscene_path_equal(const GSHGEOM_Path * a, const GSHGEOM_Path * b) {
  if (gshgeom_path_dimension(a) != gshgeom_path_dimension(b)) {
    return 0;
  }
  size_t count = gshgeom_path_element_count(a);
  if (count != gshgeom_path_element_count(b)) {
    return 0;
  }
  for (size_t i = 0; i < count; i++) {
    GSHGEOM_Element ea, eb;
    if (gshgeom_path_element(a, i, &ea) != GSHGEOM_OK || gshgeom_path_element(b, i, &eb) != GSHGEOM_OK) {
      return 0;
    }
    if (ea.verb != eb.verb) {
      return 0;
    }
    /* The start of a segment is the end of the one before, and a close's line is
     * derived from the subpath, so the points after the start are what differs. */
    for (size_t k = 1; k <= verb_points(ea.verb); k++) {
      if (!point_equal(&ea.curve.p[k], &eb.curve.p[k])) {
        return 0;
      }
    }
    for (size_t k = 0; k < verb_weights(ea.verb); k++) {
      if (gshgeom_f64_bits(ea.curve.w[k]) != gshgeom_f64_bits(eb.curve.w[k])) {
        return 0;
      }
    }
  }
  return 1;
}

/* --- glyph run equality ---------------------------------------------------- */

static int run_equal(const GSHSCENE_Glyph_Run * a, const GSHSCENE_Glyph * ga, const GSHSCENE_Glyph_Run * b,
    const GSHSCENE_Glyph * gb) {
  if (a->source != b->source || a->face != b->face || gshgeom_f64_bits(a->size) != gshgeom_f64_bits(b->size) ||
      a->hint_mode != b->hint_mode || a->hint_version != b->hint_version || a->glyph_count != b->glyph_count) {
    return 0;
  }
  for (size_t i = 0; i < a->glyph_count; i++) {
    if (ga[i].id != gb[i].id || gshgeom_f64_bits(ga[i].x) != gshgeom_f64_bits(gb[i].x) ||
        gshgeom_f64_bits(ga[i].y) != gshgeom_f64_bits(gb[i].y) || ga[i].outline != gb[i].outline) {
      return 0;
    }
  }
  return 1;
}

/* What a palette is built from. */
typedef struct Palette_Source {
  size_t count;
  const GSHSCENE_Color * colors;
} Palette_Source;

/* --- matching a resource against a probe ----------------------------------- */

int gshscene_resource_matches(const GSHSCENE_Resource * resource, const GSHSCENE_Probe * probe) {
  switch (probe->kind) {
    case GSHSCENE_RESOURCE_PATH:
      return gshscene_path_equal(resource->u.path, (const GSHGEOM_Path *)probe->source);
    case GSHSCENE_RESOURCE_GLYPH_RUN: {
      const GSHSCENE_Glyph_Run * run = (const GSHSCENE_Glyph_Run *)probe->source;
      return run_equal(&resource->u.glyphs.run, resource->u.glyphs.glyphs, run, run->glyphs);
    }
    case GSHSCENE_RESOURCE_PICTURE:
      return gshscene_list_equal(resource->u.picture.list, (const GSHSCENE_List *)probe->source);
    case GSHSCENE_RESOURCE_FACE: {
      const GSHSCENE_Face * want = (const GSHSCENE_Face *)probe->source;
      const GSHSCENE_Face * have = &resource->u.face.face;
      if (have->font_key != want->font_key || have->face_index != want->face_index ||
          have->coord_count != want->coord_count) {
        return 0;
      }
      for (uint32_t i = 0; i < want->coord_count; i++) {
        if (have->coords[i] != want->coords[i]) {
          return 0;
        }
      }
      return 1;
    }
    case GSHSCENE_RESOURCE_PALETTE: {
      const Palette_Source * want = (const Palette_Source *)probe->source;
      if (resource->u.palette.count != want->count) {
        return 0;
      }
      for (size_t i = 0; i < want->count; i++) {
        if (!gshscene_color_equal(&resource->u.palette.colors[i], &want->colors[i])) {
          return 0;
        }
      }
      return 1;
    }
    case GSHSCENE_RESOURCE_GRADIENT:
      return gshscene_gradient_matches(resource, probe->source);
    default:
      return 0;
  }
}

/* --- freeing --------------------------------------------------------------- */

void gshscene_resource_free_content(GSHSCENE_Resource * resource) {
  const GSHSCENE_Allocator * alloc = &resource->store->alloc;
  switch (resource->kind) {
    case GSHSCENE_RESOURCE_PATH:
      gshgeom_path_destroy(resource->u.path);
      break;
    case GSHSCENE_RESOURCE_GLYPH_RUN:
      for (size_t i = 0; i < resource->u.glyphs.run.glyph_count; i++) {
        gshscene_resource_release(resource->u.glyphs.glyphs[i].outline);
      }
      gshscene_free(alloc, resource->u.glyphs.glyphs);
      gshscene_resource_release(resource->u.glyphs.run.face);
      break;
    case GSHSCENE_RESOURCE_FACE:
      /* The binding was retained when the face was built: released once, here. */
      if (resource->u.face.bound && resource->u.face.ops.release) {
        resource->u.face.ops.release(resource->u.face.ctx);
      }
      gshscene_free(alloc, resource->u.face.coords);
      break;
    case GSHSCENE_RESOURCE_PICTURE:
      gshscene_list_destroy(resource->u.picture.list);
      break;
    case GSHSCENE_RESOURCE_PALETTE:
      gshscene_free(alloc, resource->u.palette.colors);
      break;
    case GSHSCENE_RESOURCE_GRADIENT:
      gshscene_gradient_free(resource);
      break;
    default:
      break;
  }
}

/* --- path ------------------------------------------------------------------ */

static GSHSCENE_Result from_geometry(GSHGEOM_Result result) {
  switch (result) {
    case GSHGEOM_OK:
      return GSHSCENE_OK;
    case GSHGEOM_ERR_LIMIT:
      return GSHSCENE_ERR_LIMIT;
    case GSHGEOM_ERR_OOM:
      return GSHSCENE_ERR_OOM;
    default:
      return GSHSCENE_ERR_INTERNAL;
  }
}

/* Replay one element of a source path into the copy. */
static GSHGEOM_Result replay(GSHGEOM_Path * copy, const GSHGEOM_Element * e) {
  const GSHGEOM_Curve * c = &e->curve;
  switch (e->verb) {
    case GSHGEOM_VERB_MOVE:
      return gshgeom_path_move_to(copy, c->p[1]);
    case GSHGEOM_VERB_LINE:
      return gshgeom_path_line_to(copy, c->p[1]);
    case GSHGEOM_VERB_QUADRATIC:
      return gshgeom_path_quadratic_to(copy, c->p[1], c->p[2]);
    case GSHGEOM_VERB_CUBIC:
      return gshgeom_path_cubic_to(copy, c->p[1], c->p[2], c->p[3]);
    case GSHGEOM_VERB_RATIONAL_QUADRATIC:
      return gshgeom_path_rational_quadratic_to(copy, c->p[1], c->p[2], c->w);
    case GSHGEOM_VERB_RATIONAL_CUBIC:
      return gshgeom_path_rational_cubic_to(copy, c->p[1], c->p[2], c->p[3], c->w);
    case GSHGEOM_VERB_CLOSE:
      return gshgeom_path_close(copy);
    default:
      return GSHGEOM_ERR_INTERNAL;
  }
}

static GSHSCENE_Result build_path(GSHSCENE_Store * store, const void * source, GSHSCENE_Resource ** out) {
  const GSHGEOM_Path * path = (const GSHGEOM_Path *)source;
  GSHSCENE_Resource * resource = gshscene_store_new_resource(store, GSHSCENE_RESOURCE_PATH);
  if (!resource) {
    return GSHSCENE_ERR_OOM;
  }
  GSHGEOM_Limits limits;
  gshgeom_limits_default(&limits);
  limits.max_elements = store->limits.max_resource_elements;
  GSHGEOM_Result result =
      gshgeom_path_create(&store->alloc, &limits, gshgeom_path_dimension(path), &resource->u.path);
  size_t count = gshgeom_path_element_count(path);
  for (size_t i = 0; result == GSHGEOM_OK && i < count; i++) {
    GSHGEOM_Element element;
    result = gshgeom_path_element(path, i, &element);
    if (result == GSHGEOM_OK) {
      result = replay(resource->u.path, &element);
    }
  }
  if (result != GSHGEOM_OK) {
    gshscene_store_discard_resource(resource);
    return from_geometry(result);
  }
  *out = resource;
  return GSHSCENE_OK;
}

GSHSCENE_Result gshscene_store_intern_path(
    GSHSCENE_Store * store, const GSHGEOM_Path * path, const GSHSCENE_Resource ** out) {
  if (!store || !path || !out) {
    return GSHSCENE_ERR_INVALID;
  }
  GSHSCENE_Probe probe = {GSHSCENE_RESOURCE_PATH, path_content_hash(path),
      gshscene_sat_add(GSHSCENE_BYTES_BASE,
          gshscene_sat_mul(gshgeom_path_element_count(path), GSHSCENE_BYTES_PER_PATH_ELEMENT)),
      path, build_path};
  return gshscene_store_intern(store, &probe, out);
}

/* --- glyph run -------------------------------------------------------------- */

static GSHSCENE_Result check_run(const GSHSCENE_Store * store, const GSHSCENE_Glyph_Run * run) {
  if (!run || (run->glyph_count && !run->glyphs)) {
    return GSHSCENE_ERR_INVALID;
  }
  if ((unsigned)run->source >= (unsigned)GSHSCENE_GLYPH_SOURCE_COUNT) {
    return GSHSCENE_ERR_INVALID;
  }
  if (run->source == GSHSCENE_GLYPHS_FROM_FONT) {
    if (!run->face || run->face->store != store || run->face->kind != GSHSCENE_RESOURCE_FACE ||
        !gshscene_finite(run->size) || !(run->size > 0.0) || run->size > GSHGEOM_COORD_LIMIT ||
        run->hint_mode >= (uint32_t)GSHSCENE_HINT_MODE_COUNT) {
      return GSHSCENE_ERR_INVALID;
    }
  } else if (run->face || gshgeom_f64_bits(run->size) != 0 || run->hint_mode != 0 || run->hint_version != 0) {
    return GSHSCENE_ERR_INVALID;
  }
  if (run->glyph_count > store->limits.max_resource_elements || run->glyph_count > SIZE_MAX / sizeof(GSHSCENE_Glyph)) {
    return GSHSCENE_ERR_LIMIT;
  }
  for (size_t i = 0; i < run->glyph_count; i++) {
    const GSHSCENE_Glyph * g = &run->glyphs[i];
    if (!gshscene_finite(g->x) || !gshscene_finite(g->y) || g->x > GSHGEOM_COORD_LIMIT ||
        g->x < -GSHGEOM_COORD_LIMIT || g->y > GSHGEOM_COORD_LIMIT || g->y < -GSHGEOM_COORD_LIMIT) {
      return GSHSCENE_ERR_INVALID;
    }
    if (run->source == GSHSCENE_GLYPHS_FROM_FONT) {
      if (g->outline) {
        return GSHSCENE_ERR_INVALID;
      }
    } else if (!g->outline || g->outline->store != store || g->outline->kind != GSHSCENE_RESOURCE_PATH) {
      return GSHSCENE_ERR_INVALID;
    }
  }
  return GSHSCENE_OK;
}

static GSHSCENE_Result build_run(GSHSCENE_Store * store, const void * source, GSHSCENE_Resource ** out) {
  const GSHSCENE_Glyph_Run * run = (const GSHSCENE_Glyph_Run *)source;
  GSHSCENE_Resource * resource = gshscene_store_new_resource(store, GSHSCENE_RESOURCE_GLYPH_RUN);
  if (!resource) {
    return GSHSCENE_ERR_OOM;
  }
  resource->u.glyphs.run = *run;
  resource->u.glyphs.run.glyph_count = 0; /* counts only what is retained so far */
  resource->u.glyphs.run.glyphs = NULL;
  resource->u.glyphs.run.face = NULL;     /* likewise: retained below */
  if (run->glyph_count) {
    resource->u.glyphs.glyphs = gshscene_alloc(&store->alloc, run->glyph_count * sizeof(GSHSCENE_Glyph));
    if (!resource->u.glyphs.glyphs) {
      gshscene_store_discard_resource(resource);
      return GSHSCENE_ERR_OOM;
    }
    memcpy(resource->u.glyphs.glyphs, run->glyphs, run->glyph_count * sizeof(GSHSCENE_Glyph));
    for (size_t i = 0; i < run->glyph_count; i++) {
      gshscene_resource_retain(run->glyphs[i].outline);
    }
  }
  gshscene_resource_retain(run->face);
  resource->u.glyphs.run.face = run->face;
  resource->u.glyphs.run.glyph_count = run->glyph_count;
  resource->u.glyphs.run.glyphs = resource->u.glyphs.glyphs;
  *out = resource;
  return GSHSCENE_OK;
}

GSHSCENE_Result gshscene_store_intern_glyph_run(
    GSHSCENE_Store * store, const GSHSCENE_Glyph_Run * run, const GSHSCENE_Resource ** out) {
  if (!store || !out) {
    return GSHSCENE_ERR_INVALID;
  }
  GSHSCENE_Result checked = check_run(store, run);
  if (checked != GSHSCENE_OK) {
    return checked;
  }
  GSHSCENE_Probe probe = {GSHSCENE_RESOURCE_GLYPH_RUN, run_content_hash(run, run->glyphs),
      gshscene_sat_add(GSHSCENE_BYTES_BASE, gshscene_sat_mul(run->glyph_count, GSHSCENE_BYTES_PER_GLYPH)),
      run, build_run};
  return gshscene_store_intern(store, &probe, out);
}

GSHSCENE_Result gshscene_store_intern_glyph_run_fixed(GSHSCENE_Store * store, const GSHSCENE_Resource * face,
    int32_t size, uint32_t hint_mode, uint32_t hint_version, unsigned bits, size_t glyph_count,
    const GSHSCENE_Glyph_Fixed * glyphs, const GSHSCENE_Resource ** out) {
  if (!store || !out || bits > 16u || size <= 0 || (glyph_count && !glyphs)) {
    return GSHSCENE_ERR_INVALID;
  }
  if (glyph_count > store->limits.max_resource_elements || glyph_count > SIZE_MAX / sizeof(GSHSCENE_Glyph)) {
    return GSHSCENE_ERR_LIMIT;
  }
  /* Each number converts exactly: an int32_t into a double, then a division by a power of two. */
  double unit = (double)((int32_t)1 << bits); /* an int32_t: i686 converts an unsigned one through x87 */
  GSHSCENE_Glyph * converted = NULL;
  if (glyph_count) {
    converted = gshscene_alloc(&store->alloc, glyph_count * sizeof *converted);
    if (!converted) {
      return GSHSCENE_ERR_OOM;
    }
  }
  for (size_t i = 0; i < glyph_count; i++) {
    converted[i].id = glyphs[i].id;
    converted[i].x = (double)glyphs[i].x / unit;
    converted[i].y = (double)glyphs[i].y / unit;
    converted[i].outline = NULL;
  }
  GSHSCENE_Glyph_Run run;
  memset(&run, 0, sizeof run);
  run.source = GSHSCENE_GLYPHS_FROM_FONT;
  run.face = face;
  run.size = (double)size / unit;
  run.hint_mode = hint_mode;
  run.hint_version = hint_version;
  run.glyph_count = glyph_count;
  run.glyphs = converted;
  GSHSCENE_Result result = gshscene_store_intern_glyph_run(store, &run, out);
  gshscene_free(&store->alloc, converted);
  return result;
}

/* --- face ------------------------------------------------------------------- */

/** What a face is built from: the canonical content and the binding. */
typedef struct Face_Source {
  GSHSCENE_Face face;
  const GSHSCENE_Glyph_Provider * provider;
  void * ctx;
} Face_Source;

/** Whether @p info is within its bounds. */
static int face_info_ok(const GSHSCENE_Face_Info * info) {
  for (size_t i = 0; i < sizeof info->_reserved; i++) {
    if (info->_reserved[i] != 0) {
      return 0;
    }
  }
  return info->upem >= GSHSCENE_FACE_UPEM_MIN && info->upem <= GSHSCENE_FACE_UPEM_MAX &&
         !(info->flags & ~GSHSCENE_FACE_FLAGS_ALL) && info->max_ppem <= GSHSCENE_GLYPH_MAX_PPEM;
}

static GSHSCENE_Result build_face(GSHSCENE_Store * store, const void * source, GSHSCENE_Resource ** out) {
  const Face_Source * src = (const Face_Source *)source;
  GSHSCENE_Face_Info info;
  memset(&info, 0, sizeof info);
  if (src->provider) {
    /* Asked once, when the face is built (glyph_provider.h). */
    GSHSCENE_Result asked = src->provider->face_info(src->ctx, &src->face, &info);
    if (asked != GSHSCENE_OK) {
      return asked;
    }
    if (!face_info_ok(&info)) {
      return GSHSCENE_ERR_INVALID;
    }
  }
  GSHSCENE_Resource * resource = gshscene_store_new_resource(store, GSHSCENE_RESOURCE_FACE);
  if (!resource) {
    return GSHSCENE_ERR_OOM;
  }
  if (src->face.coord_count) {
    resource->u.face.coords = gshscene_alloc(&store->alloc, src->face.coord_count * sizeof(int16_t));
    if (!resource->u.face.coords) {
      gshscene_store_discard_resource(resource);
      return GSHSCENE_ERR_OOM;
    }
    memcpy(resource->u.face.coords, src->face.coords, src->face.coord_count * sizeof(int16_t));
  }
  resource->u.face.face = src->face;
  resource->u.face.face.coords = resource->u.face.coords;
  resource->u.face.info = info;
  if (src->provider) {
    resource->u.face.ops = *src->provider;
    resource->u.face.ctx = src->ctx;
    if (src->provider->retain) {
      src->provider->retain(src->ctx);
    }
    resource->u.face.bound = 1; /* from here on, discarding the face releases the binding */
  }
  *out = resource;
  return GSHSCENE_OK;
}

GSHSCENE_Result gshscene_store_intern_face(
    GSHSCENE_Store * store, const GSHSCENE_Face_Desc * desc, const GSHSCENE_Resource ** out) {
  if (!store || !desc || !out || desc->font_key == 0 || (desc->coord_count && !desc->coords)) {
    return GSHSCENE_ERR_INVALID;
  }
  if (desc->coord_count > GSHSCENE_FACE_MAX_AXES) {
    return GSHSCENE_ERR_LIMIT;
  }
  const GSHSCENE_Glyph_Provider * provider = desc->provider;
  if (provider && (provider->version != GSHSCENE_GLYPH_PROVIDER_VERSION || !provider->face_info ||
                      !provider->glyph_outline || !provider->glyph_box)) {
    return GSHSCENE_ERR_INVALID;
  }
  uint32_t count = desc->coord_count;
  for (uint32_t i = 0; i < count; i++) {
    if (desc->coords[i] < -GSHSCENE_F2DOT14_ONE || desc->coords[i] > GSHSCENE_F2DOT14_ONE) {
      return GSHSCENE_ERR_INVALID;
    }
  }
  /* Trailing zeros are the default of their axes: dropped, so equal instances are one face. */
  while (count > 0 && desc->coords[count - 1] == 0) {
    count--;
  }
  Face_Source src;
  src.face.font_key = desc->font_key;
  src.face.face_index = desc->face_index;
  src.face.coord_count = count;
  src.face.coords = count ? desc->coords : NULL;
  src.provider = provider;
  src.ctx = desc->ctx;
  GSHSCENE_Probe probe = {GSHSCENE_RESOURCE_FACE, face_content_hash(desc->font_key, desc->face_index, count, desc->coords),
      gshscene_sat_add(GSHSCENE_BYTES_BASE, gshscene_sat_mul(count, GSHSCENE_BYTES_PER_AXIS)), &src.face, build_face};
  /* The probe's source is the face; build_face reads the binding beside it. */
  return gshscene_store_intern(store, &probe, out);
}

GSHSCENE_Result gshscene_resource_face(const GSHSCENE_Resource * resource, GSHSCENE_Face * out) {
  if (!resource || !out || resource->kind != GSHSCENE_RESOURCE_FACE) {
    return GSHSCENE_ERR_INVALID;
  }
  *out = resource->u.face.face;
  return GSHSCENE_OK;
}

GSHSCENE_Result gshscene_face_info(const GSHSCENE_Resource * resource, GSHSCENE_Face_Info * out) {
  if (!resource || !out || resource->kind != GSHSCENE_RESOURCE_FACE || !resource->u.face.bound) {
    return GSHSCENE_ERR_INVALID;
  }
  *out = resource->u.face.info;
  return GSHSCENE_OK;
}

GSHSCENE_Result gshscene_face_binding(
    const GSHSCENE_Resource * resource, const GSHSCENE_Glyph_Provider ** ops, void ** ctx) {
  if (!resource || !ops || !ctx || resource->kind != GSHSCENE_RESOURCE_FACE || !resource->u.face.bound) {
    return GSHSCENE_ERR_INVALID;
  }
  *ops = &resource->u.face.ops;
  *ctx = resource->u.face.ctx;
  return GSHSCENE_OK;
}

/* --- palette (N14) ---------------------------------------------------------- */

static GSHSCENE_Result build_palette(GSHSCENE_Store * store, const void * source, GSHSCENE_Resource ** out) {
  const Palette_Source * src = (const Palette_Source *)source;
  GSHSCENE_Resource * resource = gshscene_store_new_resource(store, GSHSCENE_RESOURCE_PALETTE);
  if (!resource) {
    return GSHSCENE_ERR_OOM;
  }
  if (src->count) {
    resource->u.palette.colors = gshscene_alloc(&store->alloc, src->count * sizeof(GSHSCENE_Color));
    if (!resource->u.palette.colors) {
      gshscene_store_discard_resource(resource);
      return GSHSCENE_ERR_OOM;
    }
    memcpy(resource->u.palette.colors, src->colors, src->count * sizeof(GSHSCENE_Color));
  }
  resource->u.palette.count = src->count;
  *out = resource;
  return GSHSCENE_OK;
}

GSHSCENE_Result gshscene_store_intern_palette(
    GSHSCENE_Store * store, size_t count, const GSHSCENE_Color * colors, const GSHSCENE_Resource ** out) {
  if (!store || !out || (count && !colors)) {
    return GSHSCENE_ERR_INVALID;
  }
  if (count > GSHSCENE_PALETTE_MAX_ENTRIES) {
    return GSHSCENE_ERR_LIMIT;
  }
  for (size_t i = 0; i < count; i++) {
    GSHSCENE_Result checked = gshscene_color_check_valid(&colors[i]);
    if (checked != GSHSCENE_OK) {
      return checked;
    }
  }
  /* N1: the bound, after every entry's validity. */
  for (size_t i = 0; i < count; i++) {
    if (!gshscene_color_bounded(colors[i].rgba)) {
      return GSHSCENE_ERR_LIMIT;
    }
  }
  Palette_Source src = {count, colors};
  GSHSCENE_Probe probe = {GSHSCENE_RESOURCE_PALETTE, palette_content_hash(count, colors),
      gshscene_sat_add(GSHSCENE_BYTES_BASE, gshscene_sat_mul(count, GSHSCENE_BYTES_PER_COLOR)), &src, build_palette};
  return gshscene_store_intern(store, &probe, out);
}

GSHSCENE_Result gshscene_store_intern_palette_rgba8(
    GSHSCENE_Store * store, size_t count, const uint8_t * rgba, const GSHSCENE_Resource ** out) {
  if (!store || !out || (count && !rgba)) {
    return GSHSCENE_ERR_INVALID;
  }
  if (count > GSHSCENE_PALETTE_MAX_ENTRIES) {
    return GSHSCENE_ERR_LIMIT;
  }
  GSHSCENE_Color * colors = NULL;
  if (count) {
    colors = gshscene_alloc(&store->alloc, count * sizeof *colors);
    if (!colors) {
      return GSHSCENE_ERR_OOM;
    }
  }
  for (size_t i = 0; i < count; i++) {
    memset(&colors[i], 0, sizeof colors[i]);
    for (int k = 0; k < 4; k++) {
      /* fixed.h: c / 255, the double nearest (one correctly rounded division of two exact integers). */
      colors[i].rgba[k] = (double)(int32_t)rgba[4u * i + (size_t)k] / 255.0;
    }
    colors[i].space.primaries = GCOL_PRIMARIES_SRGB;
    colors[i].space.transfer = GCOL_TRANSFER_SRGB;
  }
  GSHSCENE_Result result = gshscene_store_intern_palette(store, count, colors, out);
  gshscene_free(&store->alloc, colors);
  return result;
}

size_t gshscene_resource_palette_count(const GSHSCENE_Resource * resource) {
  return resource && resource->kind == GSHSCENE_RESOURCE_PALETTE ? resource->u.palette.count : 0;
}

GSHSCENE_Result gshscene_resource_palette_color(
    const GSHSCENE_Resource * resource, size_t index, GSHSCENE_Color * out) {
  if (!resource || !out || resource->kind != GSHSCENE_RESOURCE_PALETTE || index >= resource->u.palette.count) {
    return GSHSCENE_ERR_INVALID;
  }
  *out = resource->u.palette.colors[index];
  return GSHSCENE_OK;
}

/* --- picture ---------------------------------------------------------------- */

static GSHSCENE_Result build_picture(GSHSCENE_Store * store, const void * source, GSHSCENE_Resource ** out) {
  const GSHSCENE_List * list = (const GSHSCENE_List *)source;
  GSHSCENE_Resource * resource = gshscene_store_new_resource(store, GSHSCENE_RESOURCE_PICTURE);
  if (!resource) {
    return GSHSCENE_ERR_OOM;
  }
  GSHSCENE_Result result = gshscene_list_copy(list, &store->alloc, &resource->u.picture.list);
  if (result != GSHSCENE_OK) {
    gshscene_store_discard_resource(resource);
    return result;
  }
  resource->u.picture.depth = gshscene_list_picture_depth(list) + 1;
  *out = resource;
  return GSHSCENE_OK;
}

GSHSCENE_Result gshscene_store_intern_picture(
    GSHSCENE_Store * store, const GSHSCENE_List * list, const GSHSCENE_Resource ** out) {
  if (!store || !list || !out || !gshscene_list_is_closed(list) || list->store != store) {
    return GSHSCENE_ERR_INVALID;
  }
  /* A picture has no foreground and no palette of its own: its foreground and palette paints resolve against the
   * list that draws it (N10, N14). */
  if (!gshscene_color_is_default(&list->foreground) || list->palette) {
    return GSHSCENE_ERR_INVALID;
  }
  if (gshscene_list_count(list) > store->limits.max_resource_elements ||
      gshscene_list_expansion(list) > store->limits.max_expansion ||
      gshscene_list_picture_depth(list) + 1 > store->limits.max_picture_depth) {
    return GSHSCENE_ERR_LIMIT;
  }
  GSHSCENE_Probe probe = {GSHSCENE_RESOURCE_PICTURE, picture_content_hash(list),
      gshscene_sat_add(GSHSCENE_BYTES_BASE, gshscene_sat_mul(gshscene_list_count(list), GSHSCENE_BYTES_PER_ITEM)),
      list, build_picture};
  return gshscene_store_intern(store, &probe, out);
}

/* --- reading a resource back ------------------------------------------------ */

const GSHGEOM_Path * gshscene_resource_path(const GSHSCENE_Resource * resource) {
  return resource && resource->kind == GSHSCENE_RESOURCE_PATH ? resource->u.path : NULL;
}

GSHSCENE_Result gshscene_resource_glyph_run(const GSHSCENE_Resource * resource, GSHSCENE_Glyph_Run * out) {
  if (!resource || !out || resource->kind != GSHSCENE_RESOURCE_GLYPH_RUN) {
    return GSHSCENE_ERR_INVALID;
  }
  *out = resource->u.glyphs.run;
  return GSHSCENE_OK;
}

const GSHSCENE_List * gshscene_resource_picture(const GSHSCENE_Resource * resource) {
  return resource && resource->kind == GSHSCENE_RESOURCE_PICTURE ? resource->u.picture.list : NULL;
}

size_t gshscene_resource_picture_depth(const GSHSCENE_Resource * resource) {
  return resource && resource->kind == GSHSCENE_RESOURCE_PICTURE ? resource->u.picture.depth : 0;
}

/* --- dump -------------------------------------------------------------------- */

#define HEX(x) ((unsigned long long)(x))

GSHSCENE_Result gshscene_resource_dump(const GSHSCENE_Resource * resource, FILE * out) {
  if (!resource || !out) {
    return GSHSCENE_ERR_INVALID;
  }
  if (fprintf(out, "gshscene.resource %u kind=%s hash=%016llx\n", kind_format(resource->kind),
          gshscene_resource_kind_string(resource->kind), HEX(resource->hash)) < 0) {
    return GSHSCENE_ERR_IO;
  }
  GSHSCENE_Result result = GSHSCENE_OK;
  switch (resource->kind) {
    case GSHSCENE_RESOURCE_PATH:
      result = gshgeom_path_dump(resource->u.path, out) == GSHGEOM_OK ? GSHSCENE_OK : GSHSCENE_ERR_IO;
      break;
    case GSHSCENE_RESOURCE_GLYPH_RUN: {
      const GSHSCENE_Glyph_Run * run = &resource->u.glyphs.run;
      if (fprintf(out, "run source=%u face=%016llx size=%016llx hint=%u version=%u glyphs=%llu\n",
              (unsigned)run->source, HEX(outline_hash(run->face)), HEX(gshgeom_f64_bits(run->size)), run->hint_mode,
              run->hint_version, HEX(run->glyph_count)) < 0) {
        return GSHSCENE_ERR_IO;
      }
      for (size_t i = 0; i < run->glyph_count; i++) {
        const GSHSCENE_Glyph * g = &run->glyphs[i];
        if (fprintf(out, "g id=%u x=%016llx y=%016llx outline=%016llx\n", g->id, HEX(gshgeom_f64_bits(g->x)),
                HEX(gshgeom_f64_bits(g->y)), HEX(outline_hash(g->outline))) < 0) {
          return GSHSCENE_ERR_IO;
        }
      }
      break;
    }
    case GSHSCENE_RESOURCE_PICTURE:
      result = gshscene_list_dump(resource->u.picture.list, out);
      break;
    case GSHSCENE_RESOURCE_FACE: {
      const GSHSCENE_Face * face = &resource->u.face.face;
      if (fprintf(out, "face font=%016llx index=%u coords=%u", HEX(face->font_key), face->face_index,
              face->coord_count) < 0) {
        return GSHSCENE_ERR_IO;
      }
      for (uint32_t i = 0; i < face->coord_count; i++) {
        if (fprintf(out, " %d", (int)face->coords[i]) < 0) {
          return GSHSCENE_ERR_IO;
        }
      }
      if (fprintf(out, "\nbound=%d upem=%u flags=%u\n", resource->u.face.bound, resource->u.face.info.upem,
              resource->u.face.info.flags) < 0) {
        return GSHSCENE_ERR_IO;
      }
      break;
    }
    case GSHSCENE_RESOURCE_PALETTE: {
      size_t count = resource->u.palette.count;
      if (fprintf(out, "palette entries=%llu\n", HEX(count)) < 0) {
        return GSHSCENE_ERR_IO;
      }
      for (size_t i = 0; i < count; i++) {
        const GSHSCENE_Color * c = &resource->u.palette.colors[i];
        if (fprintf(out, "p %016llx %016llx %016llx %016llx ", HEX(gshgeom_f64_bits(c->rgba[0])),
                HEX(gshgeom_f64_bits(c->rgba[1])), HEX(gshgeom_f64_bits(c->rgba[2])),
                HEX(gshgeom_f64_bits(c->rgba[3]))) < 0 ||
            gshscene_space_dump(out, &c->space) < 0 || fputc('\n', out) == EOF) {
          return GSHSCENE_ERR_IO;
        }
      }
      break;
    }
    case GSHSCENE_RESOURCE_GRADIENT:
      if (gshscene_gradient_dump(resource, out) < 0) {
        return GSHSCENE_ERR_IO;
      }
      break;
    default:
      break;
  }
  if (result != GSHSCENE_OK) {
    return result;
  }
  /* A buffered stream reports a write error at the flush, and a stream's error flag is sticky:
   * a failed write whose return value was lost (an unbuffered stream's fprintf) still sets it. */
  return (fflush(out) == 0 && !ferror(out)) ? GSHSCENE_OK : GSHSCENE_ERR_IO;
}
