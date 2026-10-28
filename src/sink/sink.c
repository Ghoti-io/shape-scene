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
 * The contract layer (sink.h, S1 to S16): the sink object and its busy guard,
 * the validation of a request and a target, the planning pass, the job, the
 * walk over a list's expansion and the disposition predicate.
 */

#include <ghoti.io/shape-scene/macros.h>

#include "../internal.h"
#include "sink_internal.h"

#include <ghoti.io/color/color.h>

#include <string.h>

/* --- the calling thread -------------------------------------------------------- */

uintptr_t gshscene_thread_token(void) {
  /* Never written: only its address is used, and each live thread has its own. */
  static _Thread_local unsigned char token;
  return (uintptr_t)&token;
}

/* --- the walk ------------------------------------------------------------------ */

/** The levels a walk of @p list needs: the list and each picture nested under it. */
static size_t walk_levels(const GSHSCENE_List * list) {
  return gshscene_sat_add(gshscene_list_picture_depth(list), 1);
}

size_t gshscene_expansion_bytes(const GSHSCENE_List * list) {
  if (!list) {
    return 0;
  }
  return gshscene_sat_mul(walk_levels(list), sizeof(const GSHSCENE_List *) + sizeof(size_t));
}

GSHSCENE_Result gshscene_expansion_init(GSHSCENE_Expansion * walk, const GSHSCENE_List * list, void * memory, size_t bytes) {
  if (!walk || !list || !memory || !gshscene_list_is_closed(list) || bytes < gshscene_expansion_bytes(list)) {
    return GSHSCENE_ERR_INVALID;
  }
  size_t levels = walk_levels(list);
  walk->lists = memory;
  walk->chain = (size_t *)(void *)((unsigned char *)memory + levels * sizeof(const GSHSCENE_List *));
  walk->capacity = levels;
  walk->lists[0] = list;
  walk->chain[0] = 0;
  walk->level = 1;
  walk->pending = 0;
  return GSHSCENE_OK;
}

int gshscene_expansion_next(
    GSHSCENE_Expansion * walk, const GSHSCENE_Item ** out_item, const size_t ** out_chain, size_t * out_count) {
  if (!walk || !out_item || !out_chain || !out_count || walk->level == 0) {
    return 0;
  }
  /* Step past the unit the last call yielded: into its picture, or to the next item; then leave finished lists. */
  if (walk->pending == 2) {
    walk->chain[walk->level] = 0;
    walk->level++;
  } else if (walk->pending == 1) {
    walk->chain[walk->level - 1]++;
  }
  walk->pending = 0;
  while (walk->level > 0 && walk->chain[walk->level - 1] >= gshscene_list_count(walk->lists[walk->level - 1])) {
    walk->level--;
    if (walk->level > 0) {
      walk->chain[walk->level - 1]++;
    }
  }
  if (walk->level == 0) {
    return 0;
  }
  const GSHSCENE_Item * item = NULL;
  if (gshscene_list_item(walk->lists[walk->level - 1], walk->chain[walk->level - 1], &item) != GSHSCENE_OK) {
    walk->level = 0;
    return 0;
  }
  const GSHSCENE_List * picture = item->kind == GSHSCENE_ITEM_INSTANCE ? gshscene_resource_picture(item->resource) : NULL;
  if (picture && walk->level < walk->capacity) {
    walk->lists[walk->level] = picture;
    walk->pending = 2;
  } else {
    walk->pending = 1;
  }
  *out_item = item;
  *out_chain = walk->chain;
  *out_count = walk->level;
  return 1;
}

/* --- the predicate of the planning pass (S6) ------------------------------------ */

/** Whether @p resource is a glyph run whose glyphs are named by a font. */
static int glyphs_from_font(const GSHSCENE_Resource * resource) {
  GSHSCENE_Glyph_Run run;
  return gshscene_resource_glyph_run(resource, &run) == GSHSCENE_OK && run.source == GSHSCENE_GLYPHS_FROM_FONT;
}

GSHSCENE_Disposition gshscene_caps_disposition(const GSHSCENE_Sink_Caps * caps, const GSHSCENE_Item * item,
    const GSHGEOM_Map * instance_maps, size_t instance_count, const GSHGEOM_Map * target_map,
    const GSHSCENE_Color_Space * target_space, const GSHSCENE_Resource * palette) {
  GSHSCENE_Disposition d = {(GSHSCENE_Entry_Class)0, (GSHSCENE_Reason)0, 0};
  if (!caps || !item) {
    return d;
  }
  /* PH3: a placeholder draws nothing and is reported by the list alone, before every caps test: no sink claims the kind,
   * and a sink that does is no different. */
  if (item->kind == GSHSCENE_ITEM_PLACEHOLDER) {
    d.entry_class = item->placeholder.approximated ? GSHSCENE_ENTRY_APPROXIMATED : GSHSCENE_ENTRY_REFUSED;
    d.reason = GSHSCENE_REASON_PLACEHOLDER;
    d.detail = ((uint64_t)item->placeholder.source << 16) | (uint64_t)item->placeholder.code; /* PH4 */
    return d;
  }
  uint32_t kind = (uint32_t)item->kind;
  if (kind >= 32 || !(caps->item_kinds & (UINT32_C(1) << kind))) {
    d.entry_class = GSHSCENE_ENTRY_REFUSED;
    d.reason = GSHSCENE_REASON_ITEM_KIND;
    return d;
  }
  /* A stroke's paint and centerline are a fill's: the same refusals (SK4, SK7).  A stroke that draws nothing (its notice,
   * SK8, SK9) requires nothing of its centerline (ST5): none of the three refusals that follow is made for it. */
  int painted = item->kind == GSHSCENE_ITEM_FILL || item->kind == GSHSCENE_ITEM_STROKE;
  int stroked_run = item->kind == GSHSCENE_ITEM_STROKE && item->resource && item->resource->kind == GSHSCENE_RESOURCE_GLYPH_RUN;
  int draws_nothing = stroked_run && gshscene_stroke_notice(item, instance_maps, instance_count, target_map);
  /* ST5 (1), X6: a stroke of a run from an unbound face is refused here, before the target is touched, since no route is
   * asked for a stroke (SK7) to refuse it. */
  if (stroked_run && !draws_nothing && item->resource->u.glyphs.run.source == GSHSCENE_GLYPHS_FROM_FONT &&
      !(item->resource->u.glyphs.run.face && item->resource->u.glyphs.run.face->u.face.bound)) {
    d.entry_class = GSHSCENE_ENTRY_REFUSED;
    d.reason = GSHSCENE_REASON_FACE_UNBOUND;
    return d;
  }
  /* ST5 (2) */
  if ((painted || item->kind == GSHSCENE_ITEM_CLIP_PUSH) && !(caps->flags & GSHSCENE_CAP_GLYPHS_FROM_FONT) &&
      !draws_nothing && glyphs_from_font(item->resource)) {
    d.entry_class = GSHSCENE_ENTRY_REFUSED;
    d.reason = GSHSCENE_REASON_GLYPH_SOURCE;
    return d;
  }
  const GSHSCENE_Resource * gradient = painted && item->paint.kind == GSHSCENE_PAINT_GRADIENT ? item->paint.resource : NULL;
  if (gradient) {
    /* S17: a kind the sink does not draw is refused, never drawn wrong. */
    uint32_t gk = gradient->u.gradient.g.kind;
    if (gk >= 32 || !(caps->gradient_kinds & (UINT32_C(1) << gk))) {
      d.entry_class = GSHSCENE_ENTRY_REFUSED;
      d.reason = GSHSCENE_REASON_GRADIENT;
      return d;
    }
    /* G3, N14: a palette stop whose entry the palette in force does not have refuses the fill. */
    uint32_t need = gradient->u.gradient.palette_need;
    if (need && (!palette || palette->kind != GSHSCENE_RESOURCE_PALETTE || need > palette->u.palette.count)) {
      d.entry_class = GSHSCENE_ENTRY_REFUSED;
      d.reason = GSHSCENE_REASON_PALETTE_ENTRY;
      return d;
    }
  }
  const GSHSCENE_Resource * image_paint =
      painted && item->paint.kind == GSHSCENE_PAINT_IMAGE && item->paint.resource &&
          item->paint.resource->kind == GSHSCENE_RESOURCE_IMAGE_PAINT ? item->paint.resource : NULL;
  if (image_paint) {
    /* S21, IP15: an edge mode or a sample format the sink does not draw is refused, the missing ones named, and a sink with
     * no image capability at all is refused for all of them; never drawn wrong. */
    uint32_t have = (caps->image_extends & ((UINT32_C(1) << GSHSCENE_IMAGE_REQUIRED_FORMAT_SHIFT) - 1u)) |
        (caps->image_formats << GSHSCENE_IMAGE_REQUIRED_FORMAT_SHIFT);
    uint32_t missing = gshscene_image_required(item) & ~have;
    if (missing) {
      d.entry_class = GSHSCENE_ENTRY_REFUSED;
      d.reason = GSHSCENE_REASON_IMAGE;
      d.detail = missing;
      return d;
    }
    /* N14: a palette tint whose entry the palette in force does not have refuses the fill. */
    uint32_t need = image_paint->u.imagepaint.palette_need;
    if (need && (!palette || palette->kind != GSHSCENE_RESOURCE_PALETTE || need > palette->u.palette.count)) {
      d.entry_class = GSHSCENE_ENTRY_REFUSED;
      d.reason = GSHSCENE_REASON_PALETTE_ENTRY;
      return d;
    }
  }
  if (painted && item->paint.kind == GSHSCENE_PAINT_PALETTE) {
    /* N14: the one resolution decides; the foreground is not read for a palette paint. */
    GSHSCENE_Color unused;
    GSHSCENE_Color resolved;
    gshscene_color_init(&unused);
    if (gshscene_paint_resolve(&item->paint, &unused, palette, &resolved) != GSHSCENE_OK) {
      d.entry_class = GSHSCENE_ENTRY_REFUSED;
      d.reason = GSHSCENE_REASON_PALETTE_ENTRY;
      return d;
    }
  }
  if (item->kind == GSHSCENE_ITEM_GROUP_PUSH &&
      (item->group.composite >= 32 || !(caps->composites & (UINT32_C(1) << item->group.composite)))) {
    d.entry_class = GSHSCENE_ENTRY_REFUSED;
    d.reason = GSHSCENE_REASON_COMPOSITE;
    return d;
  }
  /* S19: PDF's group fields, each refused without its cap, never drawn as the default group. */
  if (item->kind == GSHSCENE_ITEM_GROUP_PUSH) {
    GSHSCENE_Reason refused = (GSHSCENE_Reason)0;
    if (item->group.isolation != (uint32_t)GSHSCENE_ISOLATION_ISOLATED && !(caps->flags & GSHSCENE_CAP_GROUP_NON_ISOLATED)) {
      refused = GSHSCENE_REASON_GROUP_ISOLATION;
    } else if (item->group.knockout && !(caps->flags & GSHSCENE_CAP_GROUP_KNOCKOUT)) {
      refused = GSHSCENE_REASON_GROUP_KNOCKOUT;
    } else if (item->group.soft_mask && !(caps->flags & GSHSCENE_CAP_GROUP_SOFT_MASK)) {
      refused = GSHSCENE_REASON_SOFT_MASK;
    }
    if (refused) {
      d.entry_class = GSHSCENE_ENTRY_REFUSED;
      d.reason = refused;
      return d;
    }
  }
  /* S20, SK7, ST5 (3): a stroke of a run is one region of all its glyphs (SG13), which only a sink that says so unites. */
  if (stroked_run && !(caps->flags & GSHSCENE_CAP_STROKE_RUNS) && !draws_nothing) {
    d.entry_class = GSHSCENE_ENTRY_REFUSED;
    d.reason = GSHSCENE_REASON_STROKE_CENTERLINE;
    return d;
  }
  /* S20, SK11: the features the stroke requires and the sink does not emit, unless it expands; after every other
   * refusal and before any approximation, which would otherwise hide it. */
  if (item->kind == GSHSCENE_ITEM_STROKE && !(caps->flags & GSHSCENE_CAP_STROKE_EXPAND)) {
    uint32_t missing =
        gshscene_stroke_required(item, instance_maps, instance_count, target_map) & ~caps->stroke_native;
    if (missing) {
      d.entry_class = GSHSCENE_ENTRY_REFUSED;
      d.reason = GSHSCENE_REASON_STROKE_STYLE;
      d.detail = missing;
      return d;
    }
  }
  if (item->kind == GSHSCENE_ITEM_CLIP_PUSH && !(caps->flags & GSHSCENE_CAP_CLIP_AA)) {
    d.entry_class = GSHSCENE_ENTRY_APPROXIMATED;
    d.reason = GSHSCENE_REASON_CLIP_ALIASED;
    return d;
  }
  if (item->kind == GSHSCENE_ITEM_GROUP_PUSH && !(caps->flags & GSHSCENE_CAP_BLEND_SPACES) &&
      !gshscene_color_space_is_target(&item->group.blend_space) &&
      !(target_space && gshscene_space_equal(&item->group.blend_space, target_space))) {
    d.entry_class = GSHSCENE_ENTRY_APPROXIMATED;
    d.reason = GSHSCENE_REASON_BLEND_SPACE;
    return d;
  }
  if (gradient && !(caps->flags & GSHSCENE_CAP_GRADIENT_SPACES) &&
      !gshscene_color_space_is_target(&gradient->u.gradient.g.space) &&
      !(target_space && gshscene_space_equal(&gradient->u.gradient.g.space, target_space))) {
    d.entry_class = GSHSCENE_ENTRY_APPROXIMATED;
    d.reason = GSHSCENE_REASON_INTERPOLATION_SPACE;
    return d;
  }
  if (image_paint) {
    /* IP15: among the approximations, the filter the item's map calls for, then the stated filter space.  A chain that cannot be
     * composed has no one filter: it is approximated when either is missing. */
    uint32_t filter = 0;
    uint32_t used;
    if (gshscene_image_filter_of(item, instance_maps, instance_count, target_map, &filter) == GSHSCENE_OK) {
      used = UINT32_C(1) << filter;
    } else {
      used = (UINT32_C(1) << image_paint->u.imagepaint.p.magnify) | (UINT32_C(1) << image_paint->u.imagepaint.p.minify);
    }
    if (used & ~caps->image_filters) {
      d.entry_class = GSHSCENE_ENTRY_APPROXIMATED;
      d.reason = GSHSCENE_REASON_IMAGE_FILTER;
      return d;
    }
    const GSHSCENE_Color_Space * fs = &image_paint->u.imagepaint.p.filter_space;
    if (!(caps->flags & GSHSCENE_CAP_IMAGE_FILTER_SPACES) && !gshscene_color_space_is_target(fs) &&
        !(target_space && gshscene_space_equal(fs, target_space))) {
      d.entry_class = GSHSCENE_ENTRY_APPROXIMATED;
      d.reason = GSHSCENE_REASON_IMAGE_FILTER_SPACE;
      return d;
    }
  }
  return d;
}

/* --- targets (S4) ----------------------------------------------------------------- */

int gshscene_target_kind_has_pixels(GSHSCENE_Target_Kind kind) {
  return kind == GSHSCENE_TARGET_RASTER || kind == GSHSCENE_TARGET_GPU_TEXTURE || kind == GSHSCENE_TARGET_GPU_SURFACE;
}

static int bytes_zero(const uint8_t * bytes, size_t count) {
  for (size_t i = 0; i < count; i++) {
    if (bytes[i] != 0) {
      return 0;
    }
  }
  return 1;
}

static int rect_zero(const GSHSCENE_Rect * r) {
  return r->x0 == 0 && r->y0 == 0 && r->x1 == 0 && r->y1 == 0;
}

/** A zero-filled map (dim 0, every entry zero): the target's default, the 2D identity. */
static int map_zero(const GSHGEOM_Map * map) {
  if (map->dim != 0 || map->kind != 0) {
    return 0;
  }
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      if (map->m[i][j] != 0.0) {
        return 0;
      }
    }
    if (map->t[i] != 0.0) {
      return 0;
    }
  }
  return 1;
}

/**
 * Check a target and write its canonical copy (the map made the identity where it is zero, the extent the whole
 * surface where it is zero) and its resolved blend space.
 */
static GSHSCENE_Result target_check(const GSHSCENE_Target * target, GSHSCENE_Target * out, GSHSCENE_Color_Space * space) {
  if (!target || target->kind == 0 || target->kind >= GSHSCENE_TARGET_KIND_COUNT || !target->surface ||
      (target->flags & ~GSHSCENE_TARGET_FLAGS_ALL) || !bytes_zero(target->_reserved, sizeof target->_reserved) ||
      target->unit >= GSHSCENE_UNIT_COUNT || target->encoding >= GSHSCENE_ENCODING_COUNT) {
    return GSHSCENE_ERR_INVALID;
  }
  if (target->kind == GSHSCENE_TARGET_GPU_TEXTURE || target->kind == GSHSCENE_TARGET_GPU_SURFACE) {
    return GSHSCENE_ERR_UNSUPPORTED;
  }
  GSHSCENE_Target copy = *target;
  if (map_zero(&target->map)) {
    gshgeom_map_identity(2, &copy.map);
  } else if (target->map.dim != 2 || gshgeom_map_check(&target->map) != GSHGEOM_OK) {
    return GSHSCENE_ERR_INVALID;
  }
  GSHSCENE_Result result = gshscene_color_space_check(&target->blend_space);
  if (result != GSHSCENE_OK) {
    return result;
  }
  GSHSCENE_Color_Space resolved = target->blend_space;
  if (target->kind == GSHSCENE_TARGET_STREAM) {
    const GSHSCENE_Writer * writer = target->surface;
    if (!writer->write || target->unit == GSHSCENE_UNIT_DEFAULT || target->aa != 0 ||
        target->encoding != GSHSCENE_ENCODING_SOFTWARE || !rect_zero(&target->extent) || target->damage) {
      return GSHSCENE_ERR_INVALID;
    }
  } else {
    /* A raster: pixels, stored by the sink. */
    const GIMG_Raster * raster = target->surface;
    int64_t width = gimg_raster_width(raster);
    int64_t height = gimg_raster_height(raster);
    if ((target->unit != GSHSCENE_UNIT_DEFAULT && target->unit != GSHSCENE_UNIT_PIXEL) ||
        target->encoding != GSHSCENE_ENCODING_SOFTWARE || width <= 0 || height <= 0) {
      return GSHSCENE_ERR_INVALID;
    }
    if (target->aa != GSHSCENE_AA_BOX) {
      return GSHSCENE_ERR_UNSUPPORTED;
    }
    if (rect_zero(&target->extent)) {
      copy.extent.x1 = width; /* x0 and y0 are the zero the extent already holds */
      copy.extent.y1 = height;
    } else if (!(target->extent.x0 >= 0 && target->extent.x0 < target->extent.x1 && target->extent.x1 <= width &&
                   target->extent.y0 >= 0 && target->extent.y0 < target->extent.y1 && target->extent.y1 <= height)) {
      return GSHSCENE_ERR_INVALID;
    }
    if (gshscene_color_space_is_target(&resolved)) {
      const GCOL_Color_Info * info = gimg_raster_color_info_const(raster);
      if (info && (info->primaries_stated || info->transfer != GCOL_TRANSFER_UNKNOWN || info->icc_bytes ||
                      info->icc_size || info->icc_linked_path)) {
        result = gshscene_color_space_from_info(info, &resolved);
        if (result != GSHSCENE_OK) {
          return result;
        }
      }
    }
  }
  if (gshscene_color_space_is_target(&resolved)) {
    /* An undescribed raster, and a document that states no space: sRGB (S4). */
    resolved.primaries = GCOL_PRIMARIES_SRGB;
    resolved.transfer = GCOL_TRANSFER_SRGB; /* the gamma of the zero space is already 0 */
  }
  if (out) {
    *out = copy;
  }
  if (space) {
    *space = resolved;
  }
  return GSHSCENE_OK;
}

/* --- the sink object (S2, S3, S14) ---------------------------------------------- */

GSHSCENE_Result gshscene_sink_create(const GSHSCENE_Allocator * alloc, const GSHSCENE_Limits * limits,
    const GSHSCENE_Sink_Ops * ops, void * impl, GSHSCENE_Sink ** out) {
  if (!ops || !out || !ops->name || !ops->caps || !ops->begin || !ops->destroy) {
    return GSHSCENE_ERR_INVALID;
  }
  if (!alloc) {
    alloc = gshscene_allocator_default();
  }
  size_t max_jobs = limits && limits->max_jobs ? limits->max_jobs : GSHSCENE_LIMITS_DEFAULT_MAX_JOBS;
  GSHSCENE_Sink * sink = gshscene_calloc(alloc, 1, sizeof *sink);
  if (!sink) {
    return GSHSCENE_ERR_OOM;
  }
  sink->busy = max_jobs > SIZE_MAX / sizeof(void *) ? NULL : gshscene_calloc(alloc, max_jobs, sizeof(void *));
  if (!sink->busy) {
    gshscene_free(alloc, sink);
    return GSHSCENE_ERR_OOM;
  }
  if (GCU_MUTEX_CREATE(sink->lock) != 0) {
    gshscene_free(alloc, sink->busy);
    gshscene_free(alloc, sink);
    return GSHSCENE_ERR_OOM;
  }
  sink->alloc = *alloc;
  sink->ops = *ops;
  sink->impl = impl;
  sink->creator = gshscene_thread_token();
  sink->max_jobs = max_jobs; /* in_flight is the 0 calloc wrote */
  *out = sink;
  return GSHSCENE_OK;
}

const char * gshscene_sink_name(const GSHSCENE_Sink * sink) {
  return sink ? sink->ops.name : "";
}

/** Whether the calling thread may call @p sink, given the caps it states for @p caps' target (S14). */
static int thread_ok(const GSHSCENE_Sink * sink, const GSHSCENE_Sink_Caps * caps) {
  return !(caps->flags & GSHSCENE_CAP_THREAD_AFFINE) || gshscene_thread_token() == sink->creator;
}

/** Validate a target and ask the sink's caps for it. */
static GSHSCENE_Result caps_for(const GSHSCENE_Sink * sink, const GSHSCENE_Target * target, GSHSCENE_Target * copy,
    GSHSCENE_Color_Space * space, GSHSCENE_Sink_Caps * caps) {
  GSHSCENE_Result result = target_check(target, copy, space);
  if (result != GSHSCENE_OK) {
    return result;
  }
  GSHSCENE_Sink_Caps c;
  memset(&c, 0, sizeof c);
  result = sink->ops.caps(sink->impl, copy ? copy : target, &c);
  if (result != GSHSCENE_OK) {
    return result;
  }
  if ((c.flags & ~GSHSCENE_CAP_FLAGS_ALL) || (c.stroke_native & ~GSHSCENE_STROKE_NATIVE_ALL) ||
      (c.image_filters & ~((UINT32_C(1) << GSHSCENE_IMAGE_FILTER_COUNT) - 1u)) ||
      (c.image_extends & ~((UINT32_C(1) << GSHSCENE_IMAGE_EXTEND_COUNT) - 1u)) || (c.image_formats & ~GSHSCENE_IMAGE_FORMAT_ALL) ||
      c.cancel_unit == 0 ||
      c.cancel_unit >= GSHSCENE_CANCEL_UNIT_COUNT ||
      !c.work_measure || !(c.flags & GSHSCENE_CAP_THREAD_AFFINE) != !sink->ops.thread_affine) {
    return GSHSCENE_ERR_INTERNAL;
  }
  *caps = c;
  return GSHSCENE_OK;
}

GSHSCENE_Result gshscene_sink_caps(
    const GSHSCENE_Sink * sink, const GSHSCENE_Target * target, GSHSCENE_Sink_Caps * out) {
  if (!sink || !target || !out) {
    return GSHSCENE_ERR_INVALID;
  }
  GSHSCENE_Target copy;
  GSHSCENE_Sink_Caps caps;
  GSHSCENE_Result result = caps_for(sink, target, &copy, NULL, &caps);
  if (result == GSHSCENE_OK) {
    *out = caps;
  }
  return result;
}

/** Validate a request's own fields (not its target). */
static GSHSCENE_Result request_check(const GSHSCENE_Render_Request * request) {
  if (!request || !request->list || !request->target || !request->report ||
      !gshscene_list_is_closed(request->list) || (request->flags & ~GSHSCENE_RENDER_FLAGS_ALL) ||
      !bytes_zero(request->_reserved, sizeof request->_reserved) ||
      (!request->workspace && request->workspace_bytes != 0)) {
    return GSHSCENE_ERR_INVALID;
  }
  return GSHSCENE_OK;
}

GSHSCENE_Result gshscene_sink_workspace_bytes(
    GSHSCENE_Sink * sink, const GSHSCENE_Render_Request * request, size_t * out) {
  if (!sink || !out) {
    return GSHSCENE_ERR_INVALID;
  }
  GSHSCENE_Result result = request_check(request);
  if (result != GSHSCENE_OK) {
    return result;
  }
  GSHSCENE_Render_Request r = *request;
  GSHSCENE_Target copy;
  GSHSCENE_Sink_Caps caps;
  result = caps_for(sink, request->target, &copy, NULL, &caps);
  if (result != GSHSCENE_OK) {
    return result;
  }
  if (!(caps.flags & GSHSCENE_CAP_CALLER_MEMORY) || !sink->ops.workspace_bytes) {
    return GSHSCENE_ERR_REFUSED;
  }
  r.target = &copy;
  size_t bytes = 0;
  /* A workspace the request brings is sized first, allocating nothing (the op as a render calls it); one too small
   * gives only a bound past it, so the exact need is then asked without one, where the sink may allocate (S9). */
  if (r.workspace) {
    r.flags |= GSHSCENE_RENDER_CALLER_MEMORY;
    result = sink->ops.workspace_bytes(sink->impl, &r, &bytes);
    if (result != GSHSCENE_OK) {
      return result;
    }
    if (bytes <= r.workspace_bytes) {
      *out = bytes;
      return GSHSCENE_OK;
    }
  }
  r.flags &= ~GSHSCENE_RENDER_CALLER_MEMORY;
  r.workspace = NULL;
  r.workspace_bytes = 0;
  result = sink->ops.workspace_bytes(sink->impl, &r, &bytes);
  if (result == GSHSCENE_OK) {
    *out = bytes;
  }
  return result;
}

/** Take @p surface for a job: refused when it is busy or the sink is full. */
static GSHSCENE_Result surface_take(GSHSCENE_Sink * sink, const void * surface) {
  GSHSCENE_Result result = GSHSCENE_OK;
  GCU_MUTEX_LOCK(sink->lock);
  size_t free_slot = sink->max_jobs;
  for (size_t i = 0; i < sink->max_jobs; i++) {
    if (sink->busy[i] == surface) {
      result = GSHSCENE_ERR_INVALID;
      break;
    }
    if (!sink->busy[i] && free_slot == sink->max_jobs) {
      free_slot = i;
    }
  }
  if (result == GSHSCENE_OK) {
    if (free_slot == sink->max_jobs) {
      result = GSHSCENE_ERR_LIMIT;
    } else {
      sink->busy[free_slot] = surface;
      sink->in_flight++;
    }
  }
  GCU_MUTEX_UNLOCK(sink->lock);
  return result;
}

static void surface_give(GSHSCENE_Sink * sink, const void * surface) {
  GCU_MUTEX_LOCK(sink->lock);
  for (size_t i = 0; i < sink->max_jobs; i++) {
    if (sink->busy[i] == surface) {
      sink->busy[i] = NULL;
      sink->in_flight--;
      break;
    }
  }
  GCU_MUTEX_UNLOCK(sink->lock);
}

/* --- the job (S3) --------------------------------------------------------------- */

const GSHSCENE_Render_Request * gshscene_job_request(const GSHSCENE_Job * job) {
  return job ? &job->request : NULL;
}

const GSHSCENE_Sink_Caps * gshscene_job_caps(const GSHSCENE_Job * job) {
  return job ? &job->caps : NULL;
}

const GSHSCENE_Color_Space * gshscene_job_target_space(const GSHSCENE_Job * job) {
  return job ? &job->target_space : NULL;
}

void * gshscene_job_impl_state(const GSHSCENE_Job * job) {
  return job ? job->impl_state : NULL;
}

void gshscene_job_set_impl_state(GSHSCENE_Job * job, void * state) {
  if (job) {
    job->impl_state = state;
  }
}

uint64_t gshscene_job_impl_value(const GSHSCENE_Job * job) {
  return job ? job->impl_value : 0;
}

void gshscene_job_set_impl_value(GSHSCENE_Job * job, uint64_t value) {
  if (job) {
    job->impl_value = value;
  }
}

void gshscene_job_work(GSHSCENE_Job * job, uint64_t amount) {
  if (job) {
    job->work = job->work > UINT64_MAX - amount ? UINT64_MAX : job->work + amount;
  }
}

int gshscene_job_checkpoint(GSHSCENE_Job * job) {
  if (!job) {
    return 0;
  }
  if (job->work > job->work_max) {
    job->work_max = job->work;
  }
  job->work = 0;
  return gshscene_cancel_is_set(job->request.cancel);
}

int gshscene_job_unit(GSHSCENE_Job * job, size_t units_processed, size_t units_done) {
  if (!job) {
    return 0;
  }
  gcu_atomic_size_store(&job->units_processed, units_processed);
  gcu_atomic_size_store(&job->units_done, units_done);
  if (job->request.progress) {
    job->request.progress(job->request.progress_ctx, job, units_processed);
  }
  if (job->caps.cancel_unit == GSHSCENE_CANCEL_BETWEEN_UNITS) {
    return gshscene_job_checkpoint(job);
  }
  return 0;
}

void gshscene_job_set_frame_bytes(GSHSCENE_Job * job, uint64_t bytes) {
  if (job && bytes > job->frame_bytes) {
    job->frame_bytes = bytes;
  }
}

GSHSCENE_Result gshscene_job_report(GSHSCENE_Job * job, GSHSCENE_Entry_Class entry_class, GSHSCENE_Reason reason,
    const size_t * chain, size_t chain_count) {
  if (!job || !job->request.report || entry_class < GSHSCENE_ENTRY_REFUSED ||
      entry_class >= GSHSCENE_ENTRY_CLASS_COUNT || reason < GSHSCENE_REASON_ITEM_KIND ||
      reason >= GSHSCENE_REASON_COUNT || (!chain && chain_count)) {
    return GSHSCENE_ERR_INVALID;
  }
  return gshscene_report_add(job->request.report, entry_class, reason, chain, chain_count, 0, 0.0);
}

GSHSCENE_Result gshscene_job_report_glyph(GSHSCENE_Job * job, GSHSCENE_Entry_Class entry_class, GSHSCENE_Reason reason,
    const size_t * chain, size_t chain_count, size_t glyph) {
  if (!job || !job->request.report || entry_class < GSHSCENE_ENTRY_REFUSED ||
      entry_class >= GSHSCENE_ENTRY_CLASS_COUNT || reason < GSHSCENE_REASON_ITEM_KIND ||
      reason >= GSHSCENE_REASON_COUNT || (!chain && chain_count)) {
    return GSHSCENE_ERR_INVALID;
  }
  return gshscene_report_add(job->request.report, entry_class, reason, chain, chain_count, glyph, 0.0);
}

void gshscene_job_complete(GSHSCENE_Job * job, GSHSCENE_Result result, const size_t * chain, size_t chain_count,
    const char * reason) {
  if (!job || gcu_atomic_int_load(&job->state) != GSHSCENE_JOB_RUNNING) {
    return;
  }
  if (job->work > job->work_max) {
    job->work_max = job->work;
  }
  GSHSCENE_Report * report = job->request.report;
  GSHSCENE_Report_Summary * s = &report->summary;
  s->units_total = job->units_total;
  s->units_processed = gcu_atomic_size_load(&job->units_processed);
  s->units_done = gcu_atomic_size_load(&job->units_done);
  s->frame_bytes = job->frame_bytes;
  s->work_max = job->work_max;
  s->static_bytes = job->sink->ops.static_bytes ? job->sink->ops.static_bytes(job->sink->impl) : 0;
  gshscene_report_seal(report, result, chain, chain_count, reason);
  result = s->result;
  job->result = (int)result;
  gcu_atomic_int_store(&report->busy, 0);
  if (job->holds_surface) {
    surface_give(job->sink, job->target.surface);
  }
  if (job->request.on_complete) {
    job->request.on_complete(job->request.on_complete_ctx, job, result);
  }
  gcu_atomic_int_store(&job->state, GSHSCENE_JOB_DONE);
}

/* --- the planning pass (S6) --------------------------------------------------------- */

/** The report reason of a tie's reason. */
static GSHSCENE_Reason tie_reason(GSHSCENE_Tie_Reason reason) {
  switch (reason) {
    case GSHSCENE_TIE_LAYER:
      return GSHSCENE_REASON_TIE_LAYER;
    case GSHSCENE_TIE_PATH:
      return GSHSCENE_REASON_TIE_PATH;
    default:
      return GSHSCENE_REASON_TIE_DECLARED;
  }
}

/** Forward the ties of @p list, whose items are keyed by @p chain with the last index replaced. */
static GSHSCENE_Result plan_ties(GSHSCENE_Report * report, const GSHSCENE_List * list, size_t * chain, size_t count) {
  size_t ties = gshscene_list_tie_count(list);
  for (size_t i = 0; i < ties; i++) {
    const GSHSCENE_Tie * tie = NULL;
    if (gshscene_list_tie(list, i, &tie) != GSHSCENE_OK) {
      return GSHSCENE_ERR_INTERNAL;
    }
    chain[count - 1] = tie->first;
    GSHSCENE_Result result = gshscene_report_add(report, GSHSCENE_ENTRY_RESOLVED_AMBIGUITY, tie_reason(tie->reason),
        chain, count, tie->second, tie->depth);
    if (result != GSHSCENE_OK) {
      return result;
    }
  }
  return GSHSCENE_OK;
}

/**
 * The route of a fill or clip push of a run from a font (glyph.h), when its map composes: the item's map goes in at
 * @p maps[count - 1], after the enclosing instances' (AD-24, as a sink composes them).  @return 1 and @p route, or 0
 * for anything else (an item that is not such a run, or a map past shape-geometry's bounds, which the sink meets as
 * its own limit).
 */
static int plan_route(const GSHSCENE_Job * job, const GSHSCENE_Item * item, GSHGEOM_Map * maps, size_t count,
    GSHSCENE_Glyph_Route * route) {
  GSHSCENE_Glyph_Run run;
  if ((item->kind != GSHSCENE_ITEM_FILL && item->kind != GSHSCENE_ITEM_CLIP_PUSH) ||
      gshscene_resource_glyph_run(item->resource, &run) != GSHSCENE_OK || run.source != GSHSCENE_GLYPHS_FROM_FONT) {
    return 0;
  }
  maps[count - 1] = item->map;
  GSHGEOM_Map composed;
  if (gshgeom_map_compose_chain(maps, count, &job->target.map, &composed) != GSHGEOM_OK) {
    return 0;
  }
  return gshscene_glyph_route(item->resource, &composed, job->target.kind, route) == GSHSCENE_OK;
}

/**
 * Walk the list against the caps: every refusal and approximation the caps decide, the notice of each gradient fill
 * and each stroke that draws nothing (S17, S20), the entry of each run from a font's route (never a stroke's, SK7),
 * every tie.  @p refused is set to the refusals found.
 */
static GSHSCENE_Result plan(GSHSCENE_Job * job, size_t * refused) {
  GSHSCENE_Report * report = job->request.report;
  const GSHSCENE_List * list = job->request.list;
  size_t walk_bytes = gshscene_expansion_bytes(list);
  size_t levels = walk_levels(list);
  /* The walk's memory, then room for a chain to key the ties by, then a map per level (the routes'). */
  size_t key_bytes = gshscene_sat_mul(levels, sizeof(size_t));
  /* The maps start on a 16-byte boundary of the scratch (which the allocator aligns), whatever a pointer's size. */
  size_t at_maps = gshscene_sat_add(gshscene_sat_add(walk_bytes, key_bytes), 15u) & ~(size_t)15u;
  unsigned char * memory = gshscene_report_scratch(report, gshscene_sat_add(at_maps, gshscene_sat_mul(levels, sizeof(GSHGEOM_Map))));
  if (!memory) {
    return GSHSCENE_ERR_OOM;
  }
  size_t * key = (size_t *)(void *)(memory + walk_bytes);
  GSHGEOM_Map * maps = (GSHGEOM_Map *)(void *)(memory + at_maps);
  GSHSCENE_Expansion walk;
  GSHSCENE_Result result = gshscene_expansion_init(&walk, list, memory, walk_bytes);
  if (result != GSHSCENE_OK) {
    return result;
  }
  result = plan_ties(report, list, key, 1); /* plan_ties writes key[0] */
  if (result != GSHSCENE_OK) {
    return result;
  }
  *refused = 0;
  GSHSCENE_Skip skip = {0, 0, 0};
  const GSHSCENE_Item * item;
  const size_t * chain;
  size_t count;
  while (gshscene_expansion_next(&walk, &item, &chain, &count)) {
    if (gshscene_skip_step(&skip, item, count)) {
      continue;
    }
    GSHSCENE_Disposition d = gshscene_caps_disposition(
        &job->caps, item, maps, count - 1, &job->target.map, &job->target_space, gshscene_list_palette(list));
    if (d.entry_class) {
      /* A stroke refused for its style names the missing features (SK11), a placeholder its source and code (PH4); every
      * other entry's second is 0. */
      result = gshscene_report_add(report, d.entry_class, d.reason, chain, count, (size_t)d.detail, 0.0);
      if (result != GSHSCENE_OK) {
        return result;
      }
    }
    if (d.entry_class == GSHSCENE_ENTRY_REFUSED) {
      (*refused)++;
      gshscene_skip_begin(&skip, item, count);
      continue;
    }
    /* S17, S20: a gradient fill or a stroke that draws nothing is drawn as defined, and the report says so. */
    GSHSCENE_Reason notice = item->kind == GSHSCENE_ITEM_STROKE
        ? gshscene_stroke_notice(item, maps, count - 1, &job->target.map)
        : gshscene_paint_notice(item, maps, count - 1, &job->target.map);
    if (notice) {
      result = gshscene_report_add(report, GSHSCENE_ENTRY_NOTICE, notice, chain, count, 0, 0.0);
      if (result != GSHSCENE_OK) {
        return result;
      }
    }
    GSHSCENE_Glyph_Route route;
    if (plan_route(job, item, maps, count, &route) && route.entry_class) {
      /* A run-level entry (D6, D7): the item's chain, second 0. */
      result = gshscene_report_add(report, (GSHSCENE_Entry_Class)route.entry_class, (GSHSCENE_Reason)route.reason,
          chain, count, 0, 0.0);
      if (result != GSHSCENE_OK) {
        return result;
      }
      if (route.entry_class == GSHSCENE_ENTRY_REFUSED) {
        (*refused)++;
        gshscene_skip_begin(&skip, item, count);
        continue;
      }
    }
    if (item->kind == GSHSCENE_ITEM_INSTANCE) {
      maps[count - 1] = item->map;
      const GSHSCENE_List * picture = gshscene_resource_picture(item->resource);
      memcpy(key, chain, count * sizeof *chain); /* key[count] is written by plan_ties */
      result = plan_ties(report, picture, key, count + 1);
      if (result != GSHSCENE_OK) {
        return result;
      }
    }
  }
  return GSHSCENE_OK;
}

/* --- begin, poll, wait, render (S1, S3, S13) -------------------------------------- */

/** Mark the job begun (state, counters) without touching the sink. */
static void job_start(GSHSCENE_Job * job, GSHSCENE_Sink * sink, const GSHSCENE_Render_Request * request,
    const GSHSCENE_Target * copy, const GSHSCENE_Sink_Caps * caps, const GSHSCENE_Color_Space * space) {
  job->sink = sink;
  job->request = *request;
  job->target = *copy;
  job->request.target = &job->target;
  job->caps = *caps;
  job->target_space = *space;
  gcu_atomic_size_init(&job->units_processed, 0);
  gcu_atomic_size_init(&job->units_done, 0);
  job->units_total = gshscene_list_expansion(request->list);
  job->work = 0;
  job->work_max = 0;
  job->frame_bytes = 0;
  job->result = GSHSCENE_OK;
  job->impl_state = NULL;
  job->impl_value = 0;
  gcu_atomic_int_init(&job->state, GSHSCENE_JOB_RUNNING);
}

/** Complete a job the contract layer ended, and return its result. */
static GSHSCENE_Result end_early(GSHSCENE_Job * job, GSHSCENE_Result result, const char * reason) {
  gshscene_job_complete(job, result, NULL, 0, reason);
  return (GSHSCENE_Result)job->result;
}

GSHSCENE_Result gshscene_sink_begin(GSHSCENE_Sink * sink, const GSHSCENE_Render_Request * request, GSHSCENE_Job * job) {
  if (!sink || !job) {
    return GSHSCENE_ERR_INVALID;
  }
  GSHSCENE_Result result = request_check(request);
  if (result != GSHSCENE_OK) {
    return result;
  }
  GSHSCENE_Target copy;
  GSHSCENE_Color_Space space;
  GSHSCENE_Sink_Caps caps;
  result = caps_for(sink, request->target, &copy, &space, &caps);
  if (result != GSHSCENE_OK) {
    return result;
  }
  if (!thread_ok(sink, &caps)) {
    return GSHSCENE_ERR_INVALID;
  }
  if (gcu_atomic_int_load(&job->state) == GSHSCENE_JOB_RUNNING) {
    return GSHSCENE_ERR_INVALID;
  }
  int idle = 0;
  if (!gcu_atomic_int_compare_exchange(&request->report->busy, &idle, 1)) {
    return GSHSCENE_ERR_INVALID;
  }
  result = surface_take(sink, copy.surface);
  if (result != GSHSCENE_OK) {
    gcu_atomic_int_store(&request->report->busy, 0);
    return result;
  }
  gshscene_report_clear(request->report);
  job_start(job, sink, request, &copy, &caps, &space);
  job->holds_surface = 1;

  /* Before the first unit (S7), the flags the contract refuses itself (S8, S9), then the plan (S6). */
  if (gshscene_job_checkpoint(job)) {
    return end_early(job, GSHSCENE_ERR_CANCELLED, "cancelled before the first unit");
  }
  if (copy.flags & GSHSCENE_TARGET_SEAMLESS) {
    result = gshscene_report_add(request->report, GSHSCENE_ENTRY_REFUSED, GSHSCENE_REASON_SEAMLESS, NULL, 0, 0, 0.0);
    return end_early(job, result != GSHSCENE_OK ? result : GSHSCENE_ERR_REFUSED, "the conflation flag is reserved (AD-29)");
  }
  if (request->flags & GSHSCENE_RENDER_CALLER_MEMORY) {
    if (!(caps.flags & GSHSCENE_CAP_CALLER_MEMORY) || !sink->ops.workspace_bytes) {
      result = gshscene_report_add(request->report, GSHSCENE_ENTRY_REFUSED, GSHSCENE_REASON_CALLER_MEMORY, NULL, 0, 0, 0.0);
      return end_early(job, result != GSHSCENE_OK ? result : GSHSCENE_ERR_REFUSED, "the sink cannot take its memory from a workspace");
    }
    /* Sized in the workspace, allocating nothing (S9): a need past it may be only a bound, so a short workspace is a
     * limit with no need in the summary, and the query gives the need. */
    size_t need = 0;
    result = sink->ops.workspace_bytes(sink->impl, &job->request, &need);
    if (result != GSHSCENE_OK) {
      return end_early(job, result, "the workspace could not be sized");
    }
    if (request->workspace_bytes < need) {
      return end_early(job, GSHSCENE_ERR_LIMIT,
          "the workspace is smaller than the render needs; gshscene_sink_workspace_bytes gives the need");
    }
    request->report->summary.workspace_needed = need;
  }
  size_t refused = 0;
  result = plan(job, &refused);
  if (result != GSHSCENE_OK) {
    return end_early(job, result, "the planning pass failed");
  }
  if (refused && (request->flags & GSHSCENE_RENDER_STRICT)) {
    return end_early(job, GSHSCENE_ERR_REFUSED, "strict: the plan holds a refusal");
  }
  result = sink->ops.begin(sink->impl, job);
  if (result != GSHSCENE_OK && gcu_atomic_int_load(&job->state) == GSHSCENE_JOB_RUNNING) {
    return end_early(job, result, "the sink refused to begin");
  }
  if (gcu_atomic_int_load(&job->state) == GSHSCENE_JOB_DONE) {
    return (GSHSCENE_Result)job->result;
  }
  return GSHSCENE_OK;
}

GSHSCENE_Result gshscene_job_poll(const GSHSCENE_Job * job, GSHSCENE_Job_Status * out) {
  if (!job || !out) {
    return GSHSCENE_ERR_INVALID;
  }
  int state = gcu_atomic_int_load(&job->state);
  if (state != GSHSCENE_JOB_IDLE && job->sink && !thread_ok(job->sink, &job->caps)) {
    return GSHSCENE_ERR_INVALID;
  }
  GSHSCENE_Job_Status s = {GSHSCENE_JOB_IDLE, GSHSCENE_OK, 0, 0, 0, 0};
  s.state = (GSHSCENE_Job_State)state;
  s.result = state == GSHSCENE_JOB_DONE ? (GSHSCENE_Result)job->result : GSHSCENE_OK;
  if (state != GSHSCENE_JOB_IDLE) {
    s.units_total = job->units_total;
    s.units_processed = gcu_atomic_size_load(&job->units_processed);
    s.units_done = gcu_atomic_size_load(&job->units_done);
    s.work_max = job->work_max;
  }
  *out = s;
  return GSHSCENE_OK;
}

GSHSCENE_Result gshscene_sink_wait(GSHSCENE_Sink * sink, GSHSCENE_Job * job) {
  if (!sink || !job) {
    return GSHSCENE_ERR_INVALID;
  }
  int state = gcu_atomic_int_load(&job->state);
  if (state == GSHSCENE_JOB_IDLE || job->sink != sink) {
    return GSHSCENE_ERR_INVALID;
  }
  if (!(job->caps.flags & GSHSCENE_CAP_CAN_BLOCK)) {
    return GSHSCENE_ERR_UNSUPPORTED;
  }
  if (!thread_ok(sink, &job->caps)) {
    return GSHSCENE_ERR_INVALID;
  }
  if (state == GSHSCENE_JOB_RUNNING) {
    if (!sink->ops.wait) {
      return GSHSCENE_ERR_INTERNAL;
    }
    GSHSCENE_Result result = sink->ops.wait(sink->impl, job);
    if (result != GSHSCENE_OK && gcu_atomic_int_load(&job->state) == GSHSCENE_JOB_RUNNING) {
      gshscene_job_complete(job, result, NULL, 0, "the wait failed");
    }
  }
  return (GSHSCENE_Result)job->result;
}

GSHSCENE_Result gshscene_sink_render(GSHSCENE_Sink * sink, const GSHSCENE_Render_Request * request) {
  if (!sink || !request) {
    return GSHSCENE_ERR_INVALID;
  }
  GSHSCENE_Sink_Caps caps;
  GSHSCENE_Result result = request->target ? caps_for(sink, request->target, NULL, NULL, &caps) : GSHSCENE_ERR_INVALID;
  if (result != GSHSCENE_OK) {
    return result;
  }
  if (!(caps.flags & GSHSCENE_CAP_CAN_BLOCK)) {
    return GSHSCENE_ERR_UNSUPPORTED;
  }
  GSHSCENE_Job job;
  memset(&job, 0, sizeof job);
  result = gshscene_sink_begin(sink, request, &job);
  if (gcu_atomic_int_load(&job.state) == GSHSCENE_JOB_IDLE) {
    return result;
  }
  return gshscene_sink_wait(sink, &job);
}

/* --- readback, prepare, evict, destroy (S9, S15) ------------------------------------- */

/** Field by field: a struct comparison would read its padding. */
static int format_equal(const GIMG_Pixel_Format * a, const GIMG_Pixel_Format * b) {
  if (a->channel_model != b->channel_model || a->channel_type != b->channel_type || a->layout != b->layout ||
      a->channel_count != b->channel_count || a->alpha_form != b->alpha_form) {
    return 0;
  }
  for (int i = 0; i < 8; i++) {
    if (a->bits_per_channel[i] != b->bits_per_channel[i]) {
      return 0;
    }
  }
  return 1;
}

GSHSCENE_Result gshscene_sink_readback(GSHSCENE_Sink * sink, const GSHSCENE_Job * job, GIMG_Raster * out) {
  if (!sink || !job || !out || job->sink != sink) {
    return GSHSCENE_ERR_INVALID;
  }
  if (!(job->caps.flags & GSHSCENE_CAP_READBACK) || !sink->ops.readback) {
    return GSHSCENE_ERR_UNSUPPORTED;
  }
  if (!thread_ok(sink, &job->caps) || gcu_atomic_int_load(&job->state) != GSHSCENE_JOB_DONE ||
      !gshscene_target_kind_has_pixels((GSHSCENE_Target_Kind)job->target.kind)) {
    return GSHSCENE_ERR_INVALID;
  }
  const GSHSCENE_Rect * e = &job->target.extent;
  const GIMG_Pixel_Format * want = gimg_raster_format((const GIMG_Raster *)job->target.surface);
  const GIMG_Pixel_Format * have = gimg_raster_format(out);
  if ((int64_t)gimg_raster_width(out) != e->x1 - e->x0 || (int64_t)gimg_raster_height(out) != e->y1 - e->y0 ||
      !want || !have || !format_equal(want, have)) {
    return GSHSCENE_ERR_INVALID;
  }
  return sink->ops.readback(sink->impl, job, out);
}

GSHSCENE_Result gshscene_sink_prepare(GSHSCENE_Sink * sink, const GSHSCENE_List * list, const GSHSCENE_Target * target) {
  if (!sink || !list || !target || !gshscene_list_is_closed(list)) {
    return GSHSCENE_ERR_INVALID;
  }
  GSHSCENE_Target copy;
  GSHSCENE_Sink_Caps caps;
  GSHSCENE_Result result = caps_for(sink, target, &copy, NULL, &caps);
  if (result != GSHSCENE_OK) {
    return result;
  }
  if (!thread_ok(sink, &caps)) {
    return GSHSCENE_ERR_INVALID;
  }
  if (!(caps.flags & GSHSCENE_CAP_PREPARE) || !sink->ops.prepare) {
    return GSHSCENE_OK;
  }
  return sink->ops.prepare(sink->impl, list, &copy);
}

/** The affinity of a call that names no target: the sink's own (its caps say the same for every target). */
static int sink_affine(const GSHSCENE_Sink * sink) {
  return sink->ops.thread_affine != 0;
}

GSHSCENE_Result gshscene_sink_evict(GSHSCENE_Sink * sink, uint64_t id) {
  if (!sink) {
    return GSHSCENE_ERR_INVALID;
  }
  if (sink_affine(sink) && gshscene_thread_token() != sink->creator) {
    return GSHSCENE_ERR_INVALID;
  }
  return sink->ops.evict ? sink->ops.evict(sink->impl, id) : GSHSCENE_OK;
}

GSHSCENE_Result gshscene_sink_static_bytes(const GSHSCENE_Sink * sink, uint64_t * out) {
  if (!sink || !out) {
    return GSHSCENE_ERR_INVALID;
  }
  *out = sink->ops.static_bytes ? sink->ops.static_bytes(sink->impl) : 0;
  return GSHSCENE_OK;
}

GSHSCENE_Result gshscene_sink_destroy(GSHSCENE_Sink * sink) {
  if (!sink) {
    return GSHSCENE_OK;
  }
  if (sink_affine(sink) && gshscene_thread_token() != sink->creator) {
    return GSHSCENE_ERR_INVALID;
  }
  GCU_MUTEX_LOCK(sink->lock);
  size_t in_flight = sink->in_flight;
  GCU_MUTEX_UNLOCK(sink->lock);
  if (in_flight) {
    return GSHSCENE_ERR_INVALID;
  }
  sink->ops.destroy(sink->impl);
  GSHSCENE_Allocator alloc = sink->alloc;
  GCU_MUTEX_DESTROY(sink->lock);
  gshscene_free(&alloc, sink->busy);
  gshscene_free(&alloc, sink);
  return GSHSCENE_OK;
}
