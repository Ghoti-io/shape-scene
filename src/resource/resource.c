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
 * The kinds of resource: copy, equality, hash and dump of a path, a glyph run
 * and a picture (resource.h).
 */

#include <ghoti.io/shape-scene/macros.h>

#include "../internal.h"

#include <ghoti.io/shape-geometry/curve.h>

#include <string.h>

/** The format of a resource's hash and dump. */
#define RESOURCE_FORMAT_VERSION 1u

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
    case GSHSCENE_RESOURCE_KIND_COUNT:
      break;
  }
  return "unknown resource kind";
}

/* --- hashes ---------------------------------------------------------------- */

/* The hash of a glyph's outline: 0 for a glyph with none. */
static uint64_t outline_hash(const GSHSCENE_Resource * outline) {
  return outline ? gshscene_resource_hash(outline) : 0;
}

static void hash_begin(GSHGEOM_Hash * h, GSHSCENE_Resource_Kind kind) {
  gshgeom_hash_init(h);
  gshgeom_hash_u32(h, RESOURCE_FORMAT_VERSION);
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
  gshgeom_hash_u64(&h, run->font_key);
  gshgeom_hash_u32(&h, run->face_index);
  gshgeom_hash_f64(&h, run->size);
  gshgeom_hash_u32(&h, run->hint_mode);
  gshgeom_hash_u64(&h, run->variation_key);
  gshgeom_hash_u64(&h, (uint64_t)run->glyph_count);
  for (size_t i = 0; i < run->glyph_count; i++) {
    gshgeom_hash_u32(&h, glyphs[i].id);
    gshgeom_hash_f64(&h, glyphs[i].x);
    gshgeom_hash_f64(&h, glyphs[i].y);
    gshgeom_hash_u64(&h, outline_hash(glyphs[i].outline));
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
  if (a->source != b->source || a->font_key != b->font_key || a->face_index != b->face_index ||
      gshgeom_f64_bits(a->size) != gshgeom_f64_bits(b->size) || a->hint_mode != b->hint_mode ||
      a->variation_key != b->variation_key || a->glyph_count != b->glyph_count) {
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
      break;
    case GSHSCENE_RESOURCE_PICTURE:
      gshscene_list_destroy(resource->u.picture.list);
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
  GSHSCENE_Probe probe = {GSHSCENE_RESOURCE_PATH, path_content_hash(path), path, build_path};
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
    if (run->font_key == 0 || !gshscene_finite(run->size) || !(run->size > 0.0) || run->size > GSHGEOM_COORD_LIMIT) {
      return GSHSCENE_ERR_INVALID;
    }
  } else if (run->font_key != 0 || run->face_index != 0 || gshgeom_f64_bits(run->size) != 0 ||
             run->hint_mode != 0 || run->variation_key != 0) {
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
  GSHSCENE_Probe probe = {GSHSCENE_RESOURCE_GLYPH_RUN, run_content_hash(run, run->glyphs), run, build_run};
  return gshscene_store_intern(store, &probe, out);
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
  if (gshscene_list_count(list) > store->limits.max_resource_elements ||
      gshscene_list_expansion(list) > store->limits.max_expansion ||
      gshscene_list_picture_depth(list) + 1 > store->limits.max_picture_depth) {
    return GSHSCENE_ERR_LIMIT;
  }
  GSHSCENE_Probe probe = {GSHSCENE_RESOURCE_PICTURE, picture_content_hash(list), list, build_picture};
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
  if (fprintf(out, "gshscene.resource %u kind=%s hash=%016llx\n", RESOURCE_FORMAT_VERSION,
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
      if (fprintf(out, "run source=%u font=%016llx face=%u size=%016llx hint=%u var=%016llx glyphs=%llu\n",
              (unsigned)run->source, HEX(run->font_key), run->face_index, HEX(gshgeom_f64_bits(run->size)),
              run->hint_mode, HEX(run->variation_key), HEX(run->glyph_count)) < 0) {
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
