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
 * Copying a resource into another store (resource.h, CP1 to CP5): each kind is read back through its accessor and interned into
 * the destination, the resources it names (a glyph's outline, a face, a picture's items, a mask's picture, a pattern's tile, an
 * image paint's image, a hatch's stroke style, a clip union's members) copied first.  The result has the content hash of the original, which is what
 * shows the copy is whole: the hash covers every field of every kind, and names a resource by its content.
 */

#include <ghoti.io/shape-scene/macros.h>

#include "../internal.h"

#include <string.h>

/** The references a copy holds while it builds one resource: each is released once the resource that retains it exists. */
typedef struct Held {
  const GSHSCENE_Resource ** items;
  size_t count;
  size_t cap;
  const GSHSCENE_Allocator * alloc;
} Held;

static void held_release(Held * h) {
  for (size_t i = 0; i < h->count; i++) {
    gshscene_resource_release(h->items[i]);
  }
  gshscene_free(h->alloc, (void *)h->items);
  h->items = NULL;
  h->count = h->cap = 0;
}

/** Copy @p from into @p dst and keep the reference in @p h; the copy, or NULL on failure (@p result says why). */
static const GSHSCENE_Resource * copy_held(GSHSCENE_Store * dst, const GSHSCENE_Resource * from, Held * h, GSHSCENE_Result * result);

/** Copy @p from into @p dst, for a caller that releases it by hand. */
static GSHSCENE_Result copy_into(GSHSCENE_Store * dst, const GSHSCENE_Resource * from, const GSHSCENE_Resource ** out);

static const GSHSCENE_Resource * copy_held(GSHSCENE_Store * dst, const GSHSCENE_Resource * from, Held * h, GSHSCENE_Result * result) {
  if (!from) {
    return NULL;
  }
  if (h->count == h->cap) {
    size_t cap = h->cap ? h->cap * 2 : 8;
    if (cap > SIZE_MAX / sizeof *h->items) {
      *result = GSHSCENE_ERR_OOM;
      return NULL;
    }
    const GSHSCENE_Resource ** grown = gshscene_alloc(h->alloc, cap * sizeof *grown);
    if (!grown) {
      *result = GSHSCENE_ERR_OOM;
      return NULL;
    }
    if (h->count) {
      memcpy((void *)grown, (const void *)h->items, h->count * sizeof *grown);
    }
    gshscene_free(h->alloc, (void *)h->items);
    h->items = grown;
    h->cap = cap;
  }
  const GSHSCENE_Resource * copy = NULL;
  *result = copy_into(dst, from, &copy);
  if (*result != GSHSCENE_OK) {
    return NULL;
  }
  h->items[h->count++] = copy;
  return copy;
}

/** A paint with the resource it names copied into @p dst (held in @p h). */
static GSHSCENE_Result copy_paint(GSHSCENE_Store * dst, const GSHSCENE_Paint * in, GSHSCENE_Paint * out, Held * h) {
  *out = *in;
  if (in->resource) {
    GSHSCENE_Result r = GSHSCENE_OK;
    out->resource = copy_held(dst, in->resource, h, &r);
    return r;
  }
  return GSHSCENE_OK;
}

/** The items of the closed list @p from, recorded again into a new list of @p dst with every resource copied, and interned as a picture. */
static GSHSCENE_Result copy_picture(GSHSCENE_Store * dst, const GSHSCENE_Resource * from, const GSHSCENE_Resource ** out) {
  const GSHSCENE_List * list = gshscene_resource_picture(from);
  if (!list) {
    return GSHSCENE_ERR_INTERNAL;
  }
  GSHSCENE_List * copy = NULL;
  GSHSCENE_Result r = gshscene_list_create(&dst->alloc, NULL, dst, &copy);
  if (r != GSHSCENE_OK) {
    return r;
  }
  Held held = {NULL, 0, 0, &dst->alloc};
  const size_t count = gshscene_list_count(list);
  for (size_t i = 0; i < count && r == GSHSCENE_OK; i++) {
    const GSHSCENE_Item * it = NULL;
    if (gshscene_list_item(list, i, &it) != GSHSCENE_OK) {
      r = GSHSCENE_ERR_INTERNAL;
      break;
    }
    switch (it->kind) {
    case GSHSCENE_ITEM_FILL: {
      GSHSCENE_Paint paint;
      r = copy_paint(dst, &it->paint, &paint, &held);
      const GSHSCENE_Resource * res = r == GSHSCENE_OK ? copy_held(dst, it->resource, &held, &r) : NULL;
      if (r == GSHSCENE_OK) {
        r = gshscene_list_fill_antialias(copy, res, &it->map, it->fill_rule, &paint, (GSHSCENE_Antialias)it->antialias, it->layer,
            it->depth);
      }
      break;
    }
    case GSHSCENE_ITEM_STROKE: {
      GSHSCENE_Paint paint;
      r = copy_paint(dst, &it->paint, &paint, &held);
      const GSHSCENE_Resource * center = r == GSHSCENE_OK ? copy_held(dst, it->resource, &held, &r) : NULL;
      const GSHSCENE_Resource * style = r == GSHSCENE_OK ? copy_held(dst, it->style, &held, &r) : NULL;
      if (r == GSHSCENE_OK) {
        r = gshscene_list_stroke_antialias(copy, center, style, &it->map, &paint, (GSHSCENE_Antialias)it->antialias, it->layer,
            it->depth);
      }
      break;
    }
    case GSHSCENE_ITEM_CLIP_PUSH: {
      const GSHSCENE_Resource * res = copy_held(dst, it->resource, &held, &r);
      if (r == GSHSCENE_OK) {
        r = gshscene_list_clip_push(copy, res, &it->map, it->fill_rule, it->layer, it->depth);
      }
      break;
    }
    case GSHSCENE_ITEM_CLIP_POP:
      r = gshscene_list_clip_pop(copy);
      break;
    case GSHSCENE_ITEM_GROUP_PUSH: {
      GSHSCENE_Group group = it->group;
      group.soft_mask = copy_held(dst, it->group.soft_mask, &held, &r);
      if (r == GSHSCENE_OK) {
        r = gshscene_list_group_push(copy, &group, it->layer, it->depth);
      }
      break;
    }
    case GSHSCENE_ITEM_GROUP_POP:
      r = gshscene_list_group_pop(copy);
      break;
    case GSHSCENE_ITEM_INSTANCE: {
      const GSHSCENE_Resource * picture = copy_held(dst, it->resource, &held, &r);
      if (r == GSHSCENE_OK) {
        r = gshscene_list_instance(copy, picture, &it->map, it->layer, it->depth);
      }
      break;
    }
    case GSHSCENE_ITEM_PLACEHOLDER:
      r = gshscene_list_placeholder(copy, it->placeholder.source, it->placeholder.code, it->placeholder.approximated,
          it->placeholder.has_box ? it->placeholder.box : NULL, &it->map, it->layer, it->depth);
      break;
    case GSHSCENE_ITEM_KIND_COUNT:
    default:
      r = GSHSCENE_ERR_INTERNAL;
      break;
    }
  }
  if (r == GSHSCENE_OK) {
    r = gshscene_list_close(copy);
  }
  if (r == GSHSCENE_OK) {
    r = gshscene_store_intern_picture(dst, copy, out);
  }
  gshscene_list_destroy(copy);
  held_release(&held);
  return r;
}

static GSHSCENE_Result copy_glyph_run(GSHSCENE_Store * dst, const GSHSCENE_Resource * from, const GSHSCENE_Resource ** out) {
  GSHSCENE_Glyph_Run run;
  GSHSCENE_Result r = gshscene_resource_glyph_run(from, &run);
  if (r != GSHSCENE_OK) {
    return r;
  }
  Held held = {NULL, 0, 0, &dst->alloc};
  GSHSCENE_Glyph * glyphs = NULL;
  if (run.glyph_count > 0) {
    if (run.glyph_count > SIZE_MAX / sizeof *glyphs) {
      return GSHSCENE_ERR_OOM;
    }
    glyphs = gshscene_alloc(&dst->alloc, run.glyph_count * sizeof *glyphs);
    if (!glyphs) {
      return GSHSCENE_ERR_OOM;
    }
    memcpy(glyphs, run.glyphs, run.glyph_count * sizeof *glyphs);
  }
  run.face = copy_held(dst, run.face, &held, &r);
  for (size_t i = 0; i < run.glyph_count && r == GSHSCENE_OK; i++) {
    glyphs[i].outline = copy_held(dst, glyphs[i].outline, &held, &r);
  }
  if (r == GSHSCENE_OK) {
    run.glyphs = glyphs;
    r = gshscene_store_intern_glyph_run(dst, &run, out);
  }
  gshscene_free(&dst->alloc, glyphs);
  held_release(&held);
  return r;
}

static GSHSCENE_Result copy_face(GSHSCENE_Store * dst, const GSHSCENE_Resource * from, const GSHSCENE_Resource ** out) {
  GSHSCENE_Face face;
  GSHSCENE_Result r = gshscene_resource_face(from, &face);
  if (r != GSHSCENE_OK) {
    return r;
  }
  GSHSCENE_Face_Desc desc;
  memset(&desc, 0, sizeof desc);
  desc.font_key = face.font_key;
  desc.face_index = face.face_index;
  desc.coord_count = face.coord_count;
  desc.coords = face.coords;
  const GSHSCENE_Glyph_Provider * ops = NULL;
  void * ctx = NULL;
  if (gshscene_face_binding(from, &ops, &ctx) == GSHSCENE_OK) {
    desc.provider = ops; /* the binding is shared: the destination's face runs the same provider on the same context */
    desc.ctx = ctx;
  }
  return gshscene_store_intern_face(dst, &desc, out);
}

static GSHSCENE_Result copy_palette(GSHSCENE_Store * dst, const GSHSCENE_Resource * from, const GSHSCENE_Resource ** out) {
  const size_t count = gshscene_resource_palette_count(from);
  GSHSCENE_Color * colors = NULL;
  if (count > 0) {
    if (count > SIZE_MAX / sizeof *colors) {
      return GSHSCENE_ERR_OOM;
    }
    colors = gshscene_alloc(&dst->alloc, count * sizeof *colors);
    if (!colors) {
      return GSHSCENE_ERR_OOM;
    }
  }
  GSHSCENE_Result r = GSHSCENE_OK;
  for (size_t i = 0; i < count && r == GSHSCENE_OK; i++) {
    r = gshscene_resource_palette_color(from, i, &colors[i]);
  }
  if (r == GSHSCENE_OK) {
    r = gshscene_store_intern_palette(dst, count, colors, out);
  }
  gshscene_free(&dst->alloc, colors);
  return r;
}

static GSHSCENE_Result copy_clip_union(GSHSCENE_Store * dst, const GSHSCENE_Resource * from, const GSHSCENE_Resource ** out) {
  const size_t count = gshscene_resource_clip_union_count(from);
  GSHSCENE_Clip_Member * members = NULL;
  if (count == 0 || count > SIZE_MAX / sizeof *members) {
    return GSHSCENE_ERR_INTERNAL;
  }
  members = gshscene_alloc(&dst->alloc, count * sizeof *members);
  if (!members) {
    return GSHSCENE_ERR_OOM;
  }
  Held held = {NULL, 0, 0, &dst->alloc};
  GSHSCENE_Result r = GSHSCENE_OK;
  for (size_t i = 0; i < count && r == GSHSCENE_OK; i++) {
    r = gshscene_resource_clip_union_member(from, i, &members[i]);
    if (r == GSHSCENE_OK) {
      members[i].resource = copy_held(dst, members[i].resource, &held, &r);
    }
  }
  if (r == GSHSCENE_OK) {
    r = gshscene_store_intern_clip_union(dst, members, count, out);
  }
  gshscene_free(&dst->alloc, members);
  held_release(&held);
  return r;
}

static GSHSCENE_Result copy_into(GSHSCENE_Store * dst, const GSHSCENE_Resource * from, const GSHSCENE_Resource ** out) {
  if (from->store == dst) {
    *out = gshscene_resource_retain(from);
    return GSHSCENE_OK;
  }
  Held held = {NULL, 0, 0, &dst->alloc};
  GSHSCENE_Result r = GSHSCENE_OK;
  switch (from->kind) {
  case GSHSCENE_RESOURCE_PATH: {
    GSHSCENE_Primitive prim;
    r = gshscene_resource_primitive(from, &prim);
    if (r == GSHSCENE_OK) {
      r = gshscene_store_intern_path_tagged(dst, gshscene_resource_path(from), &prim, out);
    }
    break;
  }
  case GSHSCENE_RESOURCE_GLYPH_RUN:
    r = copy_glyph_run(dst, from, out);
    break;
  case GSHSCENE_RESOURCE_PICTURE:
    r = copy_picture(dst, from, out);
    break;
  case GSHSCENE_RESOURCE_GRADIENT: {
    GSHSCENE_Gradient g;
    r = gshscene_resource_gradient(from, &g);
    if (r == GSHSCENE_OK) {
      r = gshscene_store_intern_gradient(dst, &g, out);
    }
    break;
  }
  case GSHSCENE_RESOURCE_IMAGE:
    r = gshscene_store_copy_image(dst, from, out);
    break;
  case GSHSCENE_RESOURCE_FACE:
    r = copy_face(dst, from, out);
    break;
  case GSHSCENE_RESOURCE_PALETTE:
    r = copy_palette(dst, from, out);
    break;
  case GSHSCENE_RESOURCE_STROKE_STYLE: {
    GSHSCENE_Stroke_Style s;
    r = gshscene_resource_stroke_style(from, &s);
    if (r == GSHSCENE_OK) {
      r = gshscene_store_intern_stroke_style(dst, &s, out);
    }
    break;
  }
  case GSHSCENE_RESOURCE_PLANE:
    r = gshscene_store_intern_plane(dst, out);
    break;
  case GSHSCENE_RESOURCE_IMAGE_PAINT: {
    GSHSCENE_Image_Paint p;
    r = gshscene_resource_image_paint(from, &p);
    if (r == GSHSCENE_OK) {
      p.image = copy_held(dst, p.image, &held, &r);
    }
    if (r == GSHSCENE_OK) {
      r = gshscene_store_intern_image_paint(dst, &p, out);
    }
    break;
  }
  case GSHSCENE_RESOURCE_MASK: {
    GSHSCENE_Mask m;
    r = gshscene_resource_mask(from, &m);
    if (r == GSHSCENE_OK) {
      m.picture = copy_held(dst, m.picture, &held, &r);
    }
    if (r == GSHSCENE_OK) {
      r = gshscene_store_intern_mask(dst, &m, out);
    }
    break;
  }
  case GSHSCENE_RESOURCE_PATTERN: {
    GSHSCENE_Pattern p;
    r = gshscene_resource_pattern(from, &p);
    if (r == GSHSCENE_OK) {
      p.tile = copy_held(dst, p.tile, &held, &r);
    }
    if (r == GSHSCENE_OK) {
      r = gshscene_store_intern_pattern(dst, &p, out);
    }
    break;
  }
  case GSHSCENE_RESOURCE_HATCH: {
    GSHSCENE_Hatch hatch;
    r = gshscene_resource_hatch(from, &hatch);
    if (r == GSHSCENE_OK) {
      hatch.style = copy_held(dst, hatch.style, &held, &r);
    }
    if (r == GSHSCENE_OK) {
      r = gshscene_store_intern_hatch(dst, &hatch, out);
    }
    break;
  }
  case GSHSCENE_RESOURCE_CLIP_UNION:
    r = copy_clip_union(dst, from, out);
    break;
  default:
    r = GSHSCENE_ERR_UNSUPPORTED;
    break;
  }
  held_release(&held);
  return r;
}

GSHSCENE_Result gshscene_store_copy_resource(GSHSCENE_Store * dst, const GSHSCENE_Resource * src, const GSHSCENE_Resource ** out) {
  if (!dst || !src || !out) {
    return GSHSCENE_ERR_INVALID;
  }
  return copy_into(dst, src, out);
}
