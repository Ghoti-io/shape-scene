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
 * The test sink (dump_sink.h): a second backend that composites nothing.  It
 * writes a dump of every unit, stamps marker rectangles into a raster, and its
 * caps are whatever its configuration says, so that every path of the contract
 * is reachable.
 */

#include <ghoti.io/shape-scene/macros.h>

#include "../internal.h"
#include "sink_internal.h"

#include <ghoti.io/shape-geometry/device.h>
#include <ghoti.io/shape-geometry/path.h>

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/** The dump's format, in its first line. */
#define DUMP_FORMAT 1u

/** Room for one record after its chain. */
#define LINE_BYTES 320u

/** What the workspace is carved in. */
#define CARVE_ALIGN 16u

/**
 * The scratch one glyph's provider paths take (dump_sink.h): carved from the run's memory, so a render allocates
 * nothing for its glyphs, and a glyph whose paths do not fit is refused (`RESOURCE_LIMIT`, the sink's own limit).
 */
#define GLYPH_BYTES ((size_t)65536)

/**
 * The range a box edge is clamped to before it is made a whole pixel: an `int32_t`'s.  A double becomes an
 * integer through a 32-bit conversion only, because i686 has no SSE instruction for a 64-bit one and would use the
 * x87 stack (AD-17, the no-x87 gate).  So the test sink stamps within 2^31 - 1 pixels of the origin.
 */
#define EDGE_LOW (-2147483648.0)
#define EDGE_HIGH 2147483647.0

/** One cached resource (S9). */
typedef struct Cached {
  uint64_t id;
  const GSHSCENE_Resource * resource;
} Cached;

/** An open clip or group of a run. */
typedef struct Block {
  int group;           /* a group (else a clip) */
  GSHSCENE_Rect clip;  /* the clip in force for what is inside it */
  GSHSCENE_Rect outer; /* a group: the clip in force at its push */
  GSHSCENE_Rect gathered; /* a group: the union of what it drew */
  int has_gathered;
  uint32_t mark;       /* a group: its push's mark */
} Block;

typedef struct Dump_Sink Dump_Sink;

/**
 * A bump allocator over the glyph scratch, reset for each glyph: a block is a 16-byte header (its size) and its
 * payload.  A realloc is always a new block and a copy: shape-geometry grows a path's arrays in turn, so the block
 * it regrows is never the last one handed out, and growing that one in place would be a branch nothing takes.
 */
typedef struct Glyph_Arena {
  unsigned char * base;
  size_t capacity;
  size_t used;
  GSHGEOM_Allocator self;
} Glyph_Arena;

/** One render of the test sink: what it has to remember between units. */
typedef struct Run {
  Dump_Sink * sink;
  GSHSCENE_Job * job;
  unsigned char * memory; /* the allocation, when not the workspace */
  size_t bytes;           /* the scratch it uses */
  GSHSCENE_Expansion walk;
  GSHGEOM_Map * maps;     /* a map per level */
  Block * blocks;
  size_t block_count;
  size_t groups_open;
  GSHSCENE_Skip skip;
  GSHSCENE_Reason skip_reason;
  GIMG_Raster * raster;   /* a pixel target's surface */
  GSHSCENE_Rect area;     /* the extent and the damage */
  const GSHSCENE_Writer * out;
  uint64_t hash;
  size_t processed;
  size_t done;
  int started;            /* the header is written */
  size_t * stop;          /* the chain of the unit a refusal stopped at */
  size_t stop_count;
  char * line;
  Glyph_Arena glyphs;     /* the scratch of one glyph's provider paths */
  uint64_t * gradients;   /* the ids of the gradients written so far (S17: once per gradient per render) */
  size_t gradient_count;
  uint64_t * styles;      /* the ids of the stroke styles written so far (S20: once per style per render) */
  size_t style_count;
  GSHSCENE_Resolved_Stop * stops; /* room for the most stops of a gradient the list names */
  size_t stop_capacity;
} Run;

struct Dump_Sink {
  GSHSCENE_Allocator alloc;
  GSHSCENE_Dump_Sink_Config config;
  GSHSCENE_Sink * sink;   /* the contract's object */
  GCU_MUTEX_T lock;       /* the pending runs and the cache */
  Run ** pending;      /* max_jobs of them: the contract's busy table never holds more */
  size_t pending_count;
  Cached * cache;
  size_t cache_count;
  size_t cache_capacity;
  uint64_t static_bytes;
};

/* --- the configuration -------------------------------------------------------- */

/** Fill @p caps, which the caller has zero-filled. */
static void caps_full(GSHSCENE_Sink_Caps * caps, int readback) {
  /* Every kind: it records each, a stroke included (S20). */
  for (uint32_t k = GSHSCENE_ITEM_FILL; k < GSHSCENE_ITEM_KIND_COUNT; k++) {
    caps->item_kinds |= UINT32_C(1) << k;
  }
  caps->stroke_native = GSHSCENE_STROKE_NATIVE_ALL; /* every stroke feature, recorded as a stroke */
  for (uint32_t c = 0; c < GSHSCENE_COMPOSITE_COUNT; c++) {
    caps->composites |= UINT32_C(1) << c;
  }
  caps->flags = GSHSCENE_CAP_CAN_BLOCK | GSHSCENE_CAP_CALLER_MEMORY | GSHSCENE_CAP_GLYPHS_FROM_FONT |
      GSHSCENE_CAP_CLIP_AA | GSHSCENE_CAP_BLEND_SPACES | GSHSCENE_CAP_GRADIENT_SPACES | GSHSCENE_CAP_PREPARE |
      (readback ? GSHSCENE_CAP_READBACK : 0);
  for (uint32_t k = GSHSCENE_GRADIENT_LINEAR; k < GSHSCENE_GRADIENT_KIND_COUNT; k++) {
    caps->gradient_kinds |= UINT32_C(1) << k;
  }
  caps->cancel_unit = GSHSCENE_CANCEL_BETWEEN_UNITS;
  caps->cancel_work_bound = 1;
  caps->work_measure = "units";
}

void gshscene_dump_sink_config_default(GSHSCENE_Dump_Sink_Config * config) {
  if (!config) {
    return;
  }
  memset(config, 0, sizeof *config);
  caps_full(&config->raster_caps, 1);
  caps_full(&config->stream_caps, 0);
  config->targets = (UINT32_C(1) << GSHSCENE_TARGET_RASTER) | (UINT32_C(1) << GSHSCENE_TARGET_STREAM);
  /* In-line, no step limit, no extent limit and no log: the zeros the memset wrote. */
}

static int caps_valid(const GSHSCENE_Sink_Caps * caps) {
  for (size_t i = 0; i < sizeof caps->_reserved; i++) {
    if (caps->_reserved[i]) {
      return 0;
    }
  }
  const uint32_t kinds = ((UINT32_C(1) << GSHSCENE_GRADIENT_KIND_COUNT) - 1u) & ~UINT32_C(1);
  return !(caps->flags & ~GSHSCENE_CAP_FLAGS_ALL) && !(caps->gradient_kinds & ~kinds) &&
      !(caps->stroke_native & ~GSHSCENE_STROKE_NATIVE_ALL) &&
      caps->cancel_unit >= GSHSCENE_CANCEL_BETWEEN_UNITS &&
      caps->cancel_unit < GSHSCENE_CANCEL_UNIT_COUNT && caps->work_measure != NULL;
}

/* --- the sink's operations ------------------------------------------------------ */

/** RGBA, 8 bits a channel, interleaved, unsigned normalized: the one raster format the test sink stamps. */
static int raster_drawable(const GIMG_Raster * raster) {
  const GIMG_Pixel_Format * f = gimg_raster_format(raster);
  return f && f->channel_model == GIMG_CHANNEL_RGBA && f->channel_type == GIMG_CHANNEL_UNORM &&
      f->layout == GIMG_LAYOUT_INTERLEAVED && f->channel_count == 4 && f->bits_per_channel[0] == 8 &&
      f->bits_per_channel[1] == 8 && f->bits_per_channel[2] == 8 && f->bits_per_channel[3] == 8;
}

static GSHSCENE_Result op_caps(void * impl, const GSHSCENE_Target * target, GSHSCENE_Sink_Caps * out) {
  Dump_Sink * ds = impl;
  if (!(ds->config.targets & (UINT32_C(1) << target->kind))) {
    return GSHSCENE_ERR_UNSUPPORTED;
  }
  if (target->kind == GSHSCENE_TARGET_RASTER) {
    if (!raster_drawable(target->surface)) {
      return GSHSCENE_ERR_UNSUPPORTED;
    }
    *out = ds->config.raster_caps;
  } else {
    *out = ds->config.stream_caps;
  }
  return GSHSCENE_OK;
}

/** The most clips and groups open at once in @p list's expansion. */
static size_t nesting(const GSHSCENE_List * list) {
  size_t depth = 0;
  size_t most = 0;
  size_t count = gshscene_list_count(list);
  for (size_t i = 0; i < count; i++) {
    const GSHSCENE_Item * item = NULL;
    if (gshscene_list_item(list, i, &item) != GSHSCENE_OK) {
      break;
    }
    if (item->kind == GSHSCENE_ITEM_CLIP_PUSH || item->kind == GSHSCENE_ITEM_GROUP_PUSH) {
      depth++;
    } else if ((item->kind == GSHSCENE_ITEM_CLIP_POP || item->kind == GSHSCENE_ITEM_GROUP_POP) && depth > 0) {
      depth--;
    } else if (item->kind == GSHSCENE_ITEM_INSTANCE) {
      size_t inner = gshscene_sat_add(depth, nesting(gshscene_resource_picture(item->resource)));
      most = inner > most ? inner : most;
    }
    most = depth > most ? depth : most;
  }
  return most;
}

/**
 * The gradient fills and strokes of @p list's expansion (@p fills, saturating), the most stops one of their gradients
 * has, and the strokes (@p strokes, saturating).
 */
static void gradient_need(const GSHSCENE_List * list, size_t * fills, size_t * stops, size_t * strokes) {
  size_t count = gshscene_list_count(list);
  for (size_t i = 0; i < count; i++) {
    const GSHSCENE_Item * item = NULL;
    if (gshscene_list_item(list, i, &item) != GSHSCENE_OK) {
      break;
    }
    if (item->kind == GSHSCENE_ITEM_STROKE) {
      *strokes = gshscene_sat_add(*strokes, 1);
    }
    if ((item->kind == GSHSCENE_ITEM_FILL || item->kind == GSHSCENE_ITEM_STROKE) &&
        item->paint.kind == GSHSCENE_PAINT_GRADIENT) {
      *fills = gshscene_sat_add(*fills, 1);
      size_t n = item->paint.gradient->u.gradient.g.stop_count;
      *stops = n > *stops ? n : *stops;
    } else if (item->kind == GSHSCENE_ITEM_INSTANCE) {
      gradient_need(gshscene_resource_picture(item->resource), fills, stops, strokes);
    }
  }
}

static size_t align_up(size_t n) {
  return gshscene_sat_add(n, CARVE_ALIGN - 1) & ~(size_t)(CARVE_ALIGN - 1);
}

/** The bytes a run of @p list needs, the slack for an unaligned workspace included. */
static size_t run_bytes(const GSHSCENE_List * list, size_t * levels, size_t * blocks, size_t * fills, size_t * stops,
    size_t * strokes) {
  *levels = gshscene_sat_add(gshscene_list_picture_depth(list), 1);
  *blocks = gshscene_sat_add(nesting(list), 1);
  *fills = 0;
  *stops = 0;
  *strokes = 0;
  gradient_need(list, fills, stops, strokes);
  size_t n = CARVE_ALIGN - 1;
  n = gshscene_sat_add(n, align_up(sizeof(Run)));
  n = gshscene_sat_add(n, align_up(gshscene_expansion_bytes(list)));
  n = gshscene_sat_add(n, align_up(gshscene_sat_mul(*levels, sizeof(GSHGEOM_Map))));
  n = gshscene_sat_add(n, align_up(gshscene_sat_mul(*blocks, sizeof(Block))));
  n = gshscene_sat_add(n, align_up(gshscene_sat_mul(*levels, sizeof(size_t))));
  n = gshscene_sat_add(n, align_up(LINE_BYTES));
  n = gshscene_sat_add(n, align_up(gshscene_sat_mul(*fills, sizeof(uint64_t))));
  n = gshscene_sat_add(n, align_up(gshscene_sat_mul(*stops, sizeof(GSHSCENE_Resolved_Stop))));
  n = gshscene_sat_add(n, align_up(gshscene_sat_mul(*strokes, sizeof(uint64_t))));
  n = gshscene_sat_add(n, GLYPH_BYTES);
  return n;
}

/* --- the glyph scratch --------------------------------------------------------------- */

static void * arena_take(Glyph_Arena * a, size_t size) {
  size_t payload = align_up(size ? size : 1);
  if (payload == SIZE_MAX || payload > a->capacity || a->capacity - payload < a->used + CARVE_ALIGN) {
    return NULL;
  }
  unsigned char * block = a->base + a->used;
  memcpy(block, &size, sizeof size);
  a->used += CARVE_ALIGN + payload;
  return block + CARVE_ALIGN;
}

static void * arena_malloc(void * ctx, size_t size) {
  return arena_take(ctx, size);
}

static void * arena_calloc(void * ctx, size_t count, size_t size) {
  if (size != 0 && count > SIZE_MAX / size) {
    return NULL;
  }
  void * p = arena_take(ctx, count * size);
  if (p) {
    memset(p, 0, count * size);
  }
  return p;
}

static void * arena_realloc(void * ctx, void * pointer, size_t size) {
  Glyph_Arena * a = ctx;
  if (!pointer) {
    return arena_take(a, size);
  }
  size_t old;
  memcpy(&old, (unsigned char *)pointer - CARVE_ALIGN, sizeof old);
  void * p = arena_take(a, size);
  if (p) {
    memcpy(p, pointer, old < size ? old : size);
  }
  return p;
}

static void arena_free(void * ctx, void * pointer) {
  (void)ctx;
  (void)pointer; /* kept until the arena is reset */
}

static void arena_init(Glyph_Arena * a, unsigned char * memory, size_t bytes) {
  a->base = memory;
  a->capacity = bytes;
  a->used = 0;
  a->self.ctx = a;
  a->self.malloc_fn = arena_malloc;
  a->self.calloc_fn = arena_calloc;
  a->self.realloc_fn = arena_realloc;
  a->self.free_fn = arena_free;
}

static void arena_reset(Glyph_Arena * a) {
  a->used = 0;
}

static GSHSCENE_Result op_workspace_bytes(void * impl, const GSHSCENE_Render_Request * request, size_t * out) {
  (void)impl;
  size_t levels;
  size_t blocks;
  size_t fills;
  size_t stops;
  size_t strokes;
  *out = run_bytes(request->list, &levels, &blocks, &fills, &stops, &strokes);
  return GSHSCENE_OK;
}

/** Carve @p bytes from @p *cursor. */
static void * carve(unsigned char ** cursor, size_t bytes) {
  void * p = *cursor;
  *cursor += align_up(bytes);
  return p;
}

/* --- output ------------------------------------------------------------------------ */

static void emit(Run * run, const char * text, size_t count, GSHSCENE_Result * failed) {
  for (size_t i = 0; i < count; i++) {
    run->hash = (run->hash ^ (unsigned char)text[i]) * GSHGEOM_FNV_PRIME;
  }
  if (run->out && *failed == GSHSCENE_OK) {
    GSHSCENE_Result r = run->out->write(run->out->ctx, text, count);
    if (r != GSHSCENE_OK) {
      *failed = r;
    }
  }
}

static void emit_text(Run * run, const char * text, GSHSCENE_Result * failed) {
  emit(run, text, strlen(text), failed);
}

static void emit_chain(Run * run, const size_t * chain, size_t count, GSHSCENE_Result * failed) {
  char piece[32];
  for (size_t i = 0; i < count; i++) {
    int n = snprintf(piece, sizeof piece, i ? ".%" PRIu64 : "%" PRIu64, (uint64_t)chain[i]);
    emit(run, piece, (size_t)n, failed);
  }
}

/** The mark of a unit: the low 32 bits of FNV-1a 64 over `u64` chain length and `u64` each index. */
static uint32_t mark_of(const size_t * chain, size_t count) {
  GSHGEOM_Hash h;
  gshgeom_hash_init(&h);
  gshgeom_hash_u64(&h, (uint64_t)count);
  for (size_t i = 0; i < count; i++) {
    gshgeom_hash_u64(&h, (uint64_t)chain[i]);
  }
  return (uint32_t)(gshgeom_hash_value(&h) & UINT32_C(0xffffffff));
}

/* --- boxes ------------------------------------------------------------------------- */

/** A box in model space. */
typedef struct Box {
  int empty;
  double x0, y0, x1, y1;
} Box;

static void box_add(Box * box, double x0, double y0, double x1, double y1) {
  if (box->empty) {
    box->x0 = x0;
    box->y0 = y0;
    box->x1 = x1;
    box->y1 = y1;
    box->empty = 0;
    return;
  }
  box->x0 = x0 < box->x0 ? x0 : box->x0;
  box->y0 = y0 < box->y0 ? y0 : box->y0;
  box->x1 = x1 > box->x1 ? x1 : box->x1;
  box->y1 = y1 > box->y1 ? y1 : box->y1;
}

/**
 * The model-space box of a run from a font: each glyph's design box (the provider's) through the glyph's
 * placement, `x + (size / upem) u` and `y - (size / upem) v`.  A glyph whose box the provider cannot give adds
 * nothing (it is refused when it is drawn).
 */
static int font_box(const GSHSCENE_Resource * resource, const GSHSCENE_Glyph_Run * run, Box * box) {
  GSHSCENE_Face_Info info;
  if (gshscene_face_info(run->face, &info) != GSHSCENE_OK) {
    return 1;
  }
  double s = run->size / (double)(int32_t)info.upem / 64.0; /* model units per 26.6 design unit */
  for (size_t i = 0; i < run->glyph_count; i++) {
    GSHSCENE_Glyph_Box b;
    if (gshscene_glyph_box(resource, i, &b) != GSHSCENE_OK || b.empty) {
      continue;
    }
    const GSHSCENE_Glyph * g = &run->glyphs[i];
    box_add(box, g->x + s * (double)b.x_min, g->y - s * (double)b.y_max, g->x + s * (double)b.x_max,
        g->y - s * (double)b.y_min);
  }
  return 0;
}

/** The model-space box of a path or glyph run (see dump_sink.h); 0, or nonzero when it cannot be formed. */
static int resource_box(const GSHSCENE_Resource * resource, Box * box) {
  box->empty = 1;
  box->x0 = box->y0 = box->x1 = box->y1 = 0.0;
  const GSHGEOM_Path * path = gshscene_resource_path(resource);
  if (path) {
    GSHGEOM_Bounds b;
    if (gshgeom_path_bounds(path, &b) != GSHGEOM_OK) {
      return 1;
    }
    if (!b.empty) {
      box_add(box, b.min.x, b.min.y, b.max.x, b.max.y);
    }
    return 0;
  }
  GSHSCENE_Glyph_Run run;
  if (gshscene_resource_glyph_run(resource, &run) != GSHSCENE_OK) {
    return 1;
  }
  if (run.source == GSHSCENE_GLYPHS_FROM_FONT) {
    return font_box(resource, &run, box);
  }
  for (size_t i = 0; i < run.glyph_count; i++) {
    const GSHSCENE_Glyph * g = &run.glyphs[i];
    const GSHGEOM_Path * outline = gshscene_resource_path(g->outline);
    GSHGEOM_Bounds b;
    if (!outline || gshgeom_path_bounds(outline, &b) != GSHGEOM_OK) {
      return 1;
    }
    if (!b.empty) {
      box_add(box, b.min.x + g->x, b.min.y + g->y, b.max.x + g->x, b.max.y + g->y);
    }
  }
  return 0;
}

/** @p box through @p map: the box of its four corners; nonzero when a corner is not finite. */
static int box_map(const GSHGEOM_Map * map, const Box * in, Box * out) {
  out->empty = 1;
  out->x0 = out->y0 = out->x1 = out->y1 = 0.0;
  if (in->empty) {
    return 0;
  }
  const double xs[2] = {in->x0, in->x1};
  const double ys[2] = {in->y0, in->y1};
  for (int i = 0; i < 2; i++) {
    for (int j = 0; j < 2; j++) {
      GSHGEOM_Point p = {xs[i], ys[j], 0.0};
      GSHGEOM_Point q;
      if (gshgeom_map_apply_point(map, p, &q) != GSHGEOM_OK) {
        return 1;
      }
      box_add(out, q.x, q.y, q.x, q.y);
    }
  }
  return 0;
}

/** The largest whole number not above @p x, clamped to the range of an `int32_t` first. */
static int64_t edge_floor(double x) {
  x = x < EDGE_LOW ? EDGE_LOW : (x > EDGE_HIGH ? EDGE_HIGH : x);
  int32_t t = (int32_t)x;
  return (double)t > x ? (int64_t)t - 1 : (int64_t)t;
}

/** The smallest whole number not below @p x, clamped likewise. */
static int64_t edge_ceil(double x) {
  x = x < EDGE_LOW ? EDGE_LOW : (x > EDGE_HIGH ? EDGE_HIGH : x);
  int32_t t = (int32_t)x;
  return (double)t < x ? (int64_t)t + 1 : (int64_t)t;
}

static GSHSCENE_Rect rect_meet(GSHSCENE_Rect a, GSHSCENE_Rect b) {
  GSHSCENE_Rect r;
  r.x0 = a.x0 > b.x0 ? a.x0 : b.x0;
  r.y0 = a.y0 > b.y0 ? a.y0 : b.y0;
  r.x1 = a.x1 < b.x1 ? a.x1 : b.x1;
  r.y1 = a.y1 < b.y1 ? a.y1 : b.y1;
  return r;
}

static int rect_empty(GSHSCENE_Rect r) {
  return r.x0 >= r.x1 || r.y0 >= r.y1;
}

static GSHSCENE_Rect rect_join(GSHSCENE_Rect a, GSHSCENE_Rect b) {
  GSHSCENE_Rect r;
  r.x0 = a.x0 < b.x0 ? a.x0 : b.x0;
  r.y0 = a.y0 < b.y0 ? a.y0 : b.y0;
  r.x1 = a.x1 > b.x1 ? a.x1 : b.x1;
  r.y1 = a.y1 > b.y1 ? a.y1 : b.y1;
  return r;
}

/** A device box in whole pixels: rounded out. */
static GSHSCENE_Rect box_pixels(const Box * box) {
  GSHSCENE_Rect r = {0, 0, 0, 0};
  if (!box->empty) {
    r.x0 = edge_floor(box->x0);
    r.y0 = edge_floor(box->y0);
    r.x1 = edge_ceil(box->x1);
    r.y1 = edge_ceil(box->y1);
  }
  return r;
}

/* --- stamps -------------------------------------------------------------------------- */

static void stamp(Run * run, GSHSCENE_Rect r, uint32_t mark, GSHSCENE_Result * failed) {
  r = rect_meet(r, run->area);
  if (rect_empty(r)) {
    return;
  }
  unsigned char * pixels = gimg_raster_pixels(run->raster);
  size_t stride = gimg_raster_stride_bytes(run->raster);
  unsigned char bytes[4] = {(unsigned char)(mark & 0xffu), (unsigned char)((mark >> 8) & 0xffu),
      (unsigned char)((mark >> 16) & 0xffu), (unsigned char)((mark >> 24) & 0xffu)};
  for (int64_t y = r.y0; y < r.y1; y++) {
    unsigned char * row = pixels + (size_t)y * stride;
    for (int64_t x = r.x0; x < r.x1; x++) {
      memcpy(row + (size_t)x * 4u, bytes, 4);
    }
  }
  snprintf(run->line, LINE_BYTES, "S %" PRId64 " %" PRId64 " %" PRId64 " %" PRId64 " %08" PRIx32 "\n", r.x0, r.y0,
      r.x1, r.y1, mark);
  emit_text(run, run->line, failed);
}

/** The innermost open group, or NULL. */
static Block * innermost_group(Run * run) {
  for (size_t i = run->block_count; i > 0; i--) {
    if (run->blocks[i - 1].group) {
      return &run->blocks[i - 1];
    }
  }
  return NULL;
}

/** The clip in force: the innermost block's, or the area. */
static GSHSCENE_Rect clip_now(const Run * run) {
  return run->block_count ? run->blocks[run->block_count - 1].clip : run->area;
}

/** Draw @p r: stamped now outside every group, gathered by the innermost group inside one. */
static void draw_rect(Run * run, GSHSCENE_Rect r, uint32_t mark, GSHSCENE_Result * failed) {
  r = rect_meet(r, clip_now(run));
  Block * g = innermost_group(run);
  if (!g) {
    stamp(run, r, mark, failed);
    return;
  }
  if (rect_empty(r)) {
    return;
  }
  g->gathered = g->has_gathered ? rect_join(g->gathered, r) : r;
  g->has_gathered = 1;
}

/* --- one unit ----------------------------------------------------------------------- */

/** The maps of the instances that enclose the unit, root to leaf, in @p run->maps[0] to [count - 2]. */
static int instance_maps(Run * run, size_t count) {
  for (size_t i = 0; i + 1 < count; i++) {
    const GSHSCENE_Item * instance = NULL;
    if (gshscene_list_item(run->walk.lists[i], run->walk.chain[i], &instance) != GSHSCENE_OK) {
      return 1;
    }
    run->maps[i] = instance->map;
  }
  return 0;
}

/** The composed map of the unit: the instances' maps, the item's, the target's last (AD-24). */
static int unit_map(Run * run, const GSHSCENE_Item * item, size_t count, GSHGEOM_Map * out) {
  if (instance_maps(run, count)) {
    return 1;
  }
  run->maps[count - 1] = item->map;
  return gshgeom_map_compose_chain(run->maps, count, &run->job->target.map, out) != GSHGEOM_OK;
}

static void hex(Run * run, const char * prefix, uint64_t value, GSHSCENE_Result * failed) {
  snprintf(run->line, LINE_BYTES, "%s%016" PRIx64, prefix, value);
  emit_text(run, run->line, failed);
}

/** A refusal while drawing (S6): the entry is the caller's; under strict the render stops at this unit. */
static GSHSCENE_Result stop_here(Run * run, const size_t * chain, size_t count, GSHSCENE_Result failed) {
  memcpy(run->stop, chain, count * sizeof *chain);
  run->stop_count = count;
  return failed != GSHSCENE_OK ? failed : GSHSCENE_ERR_REFUSED;
}

/**
 * The glyph records of a run from a font (dump_sink.h): for each glyph, its route's answer and the entry the
 * provider's answer makes (glyph.h).  @return 0 to go on; ::GSHSCENE_ERR_REFUSED to stop (a glyph refused under
 * strict); an error from the report or the stream.
 */
static GSHSCENE_Result glyph_records(Run * run, const GSHSCENE_Item * item, const GSHGEOM_Map * map,
    const GSHSCENE_Glyph_Route * route, const size_t * chain, size_t count, GSHSCENE_Result * failed) {
  GSHSCENE_Glyph_Run glyphs;
  if (gshscene_resource_glyph_run(item->resource, &glyphs) != GSHSCENE_OK) {
    return GSHSCENE_ERR_INTERNAL;
  }
  for (size_t i = 0; i < glyphs.glyph_count; i++) {
    arena_reset(&run->glyphs);
    GSHSCENE_Glyph_Note note;
    memset(&note, 0, sizeof note);
    uint64_t hash = 0;
    char tier = route->route == GSHSCENE_GLYPH_FAST ? 'f' : 'g';
    GSHSCENE_Result r;
    if (tier == 'f') {
      GSHSCENE_Glyph_Origin origin;
      GSHGEOM_Device_Path * path = NULL;
      r = gshscene_glyph_origin(item->resource, map, i, &origin);
      if (r == GSHSCENE_OK) {
        r = gshscene_glyph_device_path(item->resource, route, i, &origin, &run->glyphs.self, NULL, &path, &note);
      }
      if (r == GSHSCENE_OK && path) {
        hash = gshgeom_device_path_hash(path);
      }
      gshgeom_device_path_destroy(path);
    } else {
      GSHGEOM_Map glyph_map;
      GSHGEOM_Path * path = NULL;
      r = gshscene_glyph_map(item->resource, map, i, &glyph_map);
      if (r == GSHSCENE_OK) {
        r = gshscene_glyph_outline(item->resource, i, &run->glyphs.self, NULL, &path, &note);
      }
      if (r == GSHSCENE_OK && path) {
        GSHGEOM_Hash h;
        gshgeom_hash_init(&h);
        gshgeom_hash_u64(&h, gshgeom_path_hash(path));
        gshgeom_hash_u64(&h, gshgeom_map_hash(&glyph_map));
        hash = gshgeom_hash_value(&h);
      }
      gshgeom_path_destroy(path);
    }
    if (r == GSHSCENE_ERR_LIMIT || r == GSHSCENE_ERR_OOM) {
      /* An origin past 26.6, a map past its bounds, or paths past the glyph scratch: the sink's own limit. */
      note.entry_class = GSHSCENE_ENTRY_REFUSED;
      note.reason = GSHSCENE_REASON_RESOURCE_LIMIT;
    } else if (r != GSHSCENE_OK) {
      return r;
    }
    if (note.entry_class == GSHSCENE_ENTRY_REFUSED) {
      snprintf(run->line, LINE_BYTES, "Y %" PRIu64 " x reason=%u\n", (uint64_t)i, (unsigned)note.reason);
    } else {
      snprintf(run->line, LINE_BYTES, "Y %" PRIu64 " %c %016" PRIx64 " status=%u\n", (uint64_t)i, tier, hash,
          (unsigned)note.status);
    }
    emit_text(run, run->line, failed);
    if (note.entry_class) {
      r = gshscene_job_report_glyph(run->job, (GSHSCENE_Entry_Class)note.entry_class, (GSHSCENE_Reason)note.reason,
          chain, count, i);
      if (r != GSHSCENE_OK) {
        return r;
      }
      if (note.entry_class == GSHSCENE_ENTRY_REFUSED && (run->job->request.flags & GSHSCENE_RENDER_STRICT)) {
        return stop_here(run, chain, count, *failed);
      }
    }
  }
  return GSHSCENE_OK;
}

/**
 * The gradient's record (dump_sink.h), the first time a render meets its id (S17): its stops through the one
 * conversion, against the list's foreground and palette and the target's space.
 */
static GSHSCENE_Result gradient_record(Run * run, const GSHSCENE_Resource * gradient, GSHSCENE_Result * failed) {
  uint64_t id = gshscene_resource_id(gradient);
  for (size_t i = 0; i < run->gradient_count; i++) {
    if (run->gradients[i] == id) {
      return GSHSCENE_OK;
    }
  }
  const GSHSCENE_List * list = run->job->request.list;
  GSHSCENE_Color foreground;
  GSHSCENE_Color_Space space;
  gshscene_list_foreground(list, &foreground);
  GSHSCENE_Result r = gshscene_gradient_resolve(gradient, &foreground, gshscene_list_palette(list),
      &run->job->target_space, &space, run->stops, run->stop_capacity);
  if (r != GSHSCENE_OK) {
    return GSHSCENE_ERR_INTERNAL; /* the plan refused a missing entry; nothing else can fail */
  }
  run->gradients[run->gradient_count++] = id;
  size_t n = gradient->u.gradient.g.stop_count;
  snprintf(run->line, LINE_BYTES, "R %" PRIu64 " %016" PRIx64 " space=%u:%u:%016" PRIx64 " stops=%" PRIu64 "\n", id,
      gshscene_resource_hash(gradient), (unsigned)space.primaries, (unsigned)space.transfer,
      gshgeom_f64_bits(space.gamma), (uint64_t)n);
  emit_text(run, run->line, failed);
  for (size_t i = 0; i < n; i++) {
    const GSHSCENE_Resolved_Stop * s = &run->stops[i];
    snprintf(run->line, LINE_BYTES, "r %016" PRIx64 " %016" PRIx64 " %016" PRIx64 " %016" PRIx64 " %016" PRIx64 "\n",
        gshgeom_f64_bits(s->offset), gshgeom_f64_bits(s->rgba[0]), gshgeom_f64_bits(s->rgba[1]),
        gshgeom_f64_bits(s->rgba[2]), gshgeom_f64_bits(s->rgba[3]));
    emit_text(run, run->line, failed);
  }
  return GSHSCENE_OK;
}

/**
 * The style's record (dump_sink.h), the first time a render meets its id (S20): `Q <id> <hash>` and its fields as
 * the style's dump spells them, then a `d` line per dash, as a writer emits a style once.
 */
static GSHSCENE_Result style_record(Run * run, const GSHSCENE_Resource * style, GSHSCENE_Result * failed) {
  uint64_t id = gshscene_resource_id(style);
  for (size_t i = 0; i < run->style_count; i++) {
    if (run->styles[i] == id) {
      return GSHSCENE_OK;
    }
  }
  GSHSCENE_Stroke_Style s;
  if (gshscene_resource_stroke_style(style, &s) != GSHSCENE_OK) {
    return GSHSCENE_ERR_INTERNAL;
  }
  run->styles[run->style_count++] = id;
  snprintf(run->line, LINE_BYTES,
      "Q %" PRIu64 " %016" PRIx64 " width=%016" PRIx64 " space=%u join=%u miter=%016" PRIx64 " cap=%u degenerate=%u "
      "align=%u rule=%u scope=%u offset=%016" PRIx64 " dashes=%" PRIu64 " profile=%016" PRIx64 "\n",
      id, gshscene_resource_hash(style), gshgeom_f64_bits(s.width), (unsigned)s.width_space, (unsigned)s.join,
      gshgeom_f64_bits(s.miter_limit), (unsigned)s.cap, (unsigned)s.degenerate, (unsigned)s.alignment,
      (unsigned)s.alignment_rule, (unsigned)s.dash_scope, gshgeom_f64_bits(s.dash_offset), (uint64_t)s.dash_count,
      (uint64_t)0);
  emit_text(run, run->line, failed);
  for (size_t i = 0; i < s.dash_count; i++) {
    snprintf(run->line, LINE_BYTES, "d %016" PRIx64 "\n", gshgeom_f64_bits(s.dashes[i]));
    emit_text(run, run->line, failed);
  }
  return GSHSCENE_OK;
}

/** Refused while drawing (S6), for @p reason: the record and the entry; under strict the render stops here. */
static GSHSCENE_Result refuse_here(Run * run, const GSHSCENE_Item * item, const size_t * chain, size_t count,
    GSHSCENE_Reason reason, GSHSCENE_Result failed) {
  snprintf(run->line, LINE_BYTES, " X class=%d reason=%d\n", (int)GSHSCENE_ENTRY_REFUSED, (int)reason);
  emit_text(run, run->line, &failed);
  GSHSCENE_Result r = gshscene_job_report(run->job, GSHSCENE_ENTRY_REFUSED, reason, chain, count);
  if (r != GSHSCENE_OK) {
    return r;
  }
  if (run->job->request.flags & GSHSCENE_RENDER_STRICT) {
    return stop_here(run, chain, count, failed);
  }
  gshscene_skip_begin(&run->skip, item, count);
  run->skip_reason = reason;
  return failed;
}

/**
 * A stroke (S20): its notice (SK8, SK9: an `X` record, not stamped); its box, SK10's bound of its centerline's box
 * (the model box a fill of it would have) under the composed map, in target units; the features it requires that the
 * caps do not emit (expanded, since the plan let it through); then its record, the style's record and the gradient's
 * the first time the render meets them, and its stamp.  A run from an unbound face has no geometry: refused
 * (`FACE_UNBOUND`) while drawing, since the plan routes no stroke (SK7).
 */
static GSHSCENE_Result stroke_unit(Run * run, const GSHSCENE_Item * item, const size_t * chain, size_t count,
    GSHSCENE_Result failed) {
  GSHSCENE_Reason notice = gshscene_stroke_notice(item, run->maps, count - 1, &run->job->target.map);
  if (notice) {
    snprintf(run->line, LINE_BYTES, " X class=%d reason=%d\n", (int)GSHSCENE_ENTRY_NOTICE, (int)notice);
    emit_text(run, run->line, &failed);
    return failed;
  }
  GSHSCENE_Glyph_Run glyphs;
  GSHSCENE_Face_Info info;
  if (gshscene_resource_glyph_run(item->resource, &glyphs) == GSHSCENE_OK && glyphs.source == GSHSCENE_GLYPHS_FROM_FONT &&
      gshscene_face_info(glyphs.face, &info) != GSHSCENE_OK) {
    return refuse_here(run, item, chain, count, GSHSCENE_REASON_FACE_UNBOUND, failed);
  }
  GSHGEOM_Map map;
  Box model;
  GSHGEOM_Bounds b;
  memset(&b, 0, sizeof b);
  int bad = unit_map(run, item, count, &map) || resource_box(item->resource, &model);
  if (!bad) {
    GSHGEOM_Bounds centerline;
    memset(&centerline, 0, sizeof centerline);
    centerline.empty = model.empty;
    centerline.min.x = model.x0;
    centerline.min.y = model.y0;
    centerline.max.x = model.x1;
    centerline.max.y = model.y1;
    bad = gshscene_stroke_bounds(item->style, &centerline, &map, run->job->target.unit, &b) != GSHSCENE_OK;
  }
  double limit = run->sink->config.max_extent;
  if (bad || (limit > 0.0 && !b.empty && (b.max.x - b.min.x > limit || b.max.y - b.min.y > limit))) {
    return refuse_here(run, item, chain, count, GSHSCENE_REASON_RESOURCE_LIMIT, failed);
  }
  uint32_t expand =
      gshscene_stroke_required(item, run->maps, count - 1, &run->job->target.map) & ~run->job->caps.stroke_native;
  snprintf(run->line, LINE_BYTES, " K res=%s:%016" PRIx64 " style=%" PRIu64 " map=%016" PRIx64,
      gshscene_resource_kind_string(gshscene_resource_kind(item->resource)), gshscene_resource_hash(item->resource),
      gshscene_resource_id(item->style), gshgeom_map_hash(&map));
  emit_text(run, run->line, &failed);
  if (item->paint.kind == GSHSCENE_PAINT_GRADIENT) {
    snprintf(run->line, LINE_BYTES, " grad=%" PRIu64, gshscene_resource_id(item->paint.gradient));
    emit_text(run, run->line, &failed);
  }
  if (b.empty) {
    snprintf(run->line, LINE_BYTES, " box=- expand=%u outline=-\n", (unsigned)expand);
  } else {
    snprintf(run->line, LINE_BYTES,
        " box=%016" PRIx64 " %016" PRIx64 " %016" PRIx64 " %016" PRIx64 " expand=%u outline=-\n",
        gshgeom_f64_bits(b.min.x), gshgeom_f64_bits(b.min.y), gshgeom_f64_bits(b.max.x), gshgeom_f64_bits(b.max.y),
        (unsigned)expand);
  }
  emit_text(run, run->line, &failed);
  GSHSCENE_Result r = style_record(run, item->style, &failed);
  if (r == GSHSCENE_OK && item->paint.kind == GSHSCENE_PAINT_GRADIENT) {
    r = gradient_record(run, item->paint.gradient, &failed);
  }
  if (r != GSHSCENE_OK) {
    return r;
  }
  if (run->raster) {
    Box box = {b.empty, b.min.x, b.min.y, b.max.x, b.max.y};
    GSHSCENE_Rect zero = {0, 0, 0, 0};
    draw_rect(run, box.empty ? zero : box_pixels(&box), mark_of(chain, count), &failed);
  }
  return failed;
}

/**
 * Go through one unit.  @return 0 to go on; a result to stop with (::GSHSCENE_ERR_REFUSED for a strict refusal while
 * drawing, an I/O error from the stream).
 */
static GSHSCENE_Result unit(Run * run, const GSHSCENE_Item * item, const size_t * chain, size_t count) {
  GSHSCENE_Result failed = GSHSCENE_OK;
  const GSHSCENE_Render_Request * request = &run->job->request;
  int pixel = run->raster != NULL;
  emit_text(run, "U ", &failed);
  emit_chain(run, chain, count, &failed);
  if (gshscene_skip_step(&run->skip, item, count)) {
    snprintf(run->line, LINE_BYTES, " X class=%d reason=%d\n", (int)GSHSCENE_ENTRY_REFUSED, (int)run->skip_reason);
    emit_text(run, run->line, &failed);
    return failed;
  }
  if (instance_maps(run, count)) {
    return GSHSCENE_ERR_INTERNAL; /* the walk's own lists: unreachable */
  }
  GSHSCENE_Disposition d = gshscene_caps_disposition(&run->job->caps, item, run->maps, count - 1, &run->job->target.map,
      &run->job->target_space, gshscene_list_palette(run->job->request.list));
  if (d.entry_class == GSHSCENE_ENTRY_REFUSED) {
    snprintf(run->line, LINE_BYTES, " X class=%d reason=%d\n", (int)d.entry_class, (int)d.reason);
    emit_text(run, run->line, &failed);
    gshscene_skip_begin(&run->skip, item, count);
    run->skip_reason = d.reason;
    return failed;
  }
  switch (item->kind) {
    case GSHSCENE_ITEM_FILL:
    case GSHSCENE_ITEM_CLIP_PUSH: {
      GSHGEOM_Map map;
      Box model;
      Box box;
      int mapped = !unit_map(run, item, count, &map);
      /* S17: a gradient fill that draws nothing (the plan wrote its notice) is gone through and not drawn. */
      GSHSCENE_Reason notice = gshscene_gradient_notice(item, run->maps, count - 1, &run->job->target.map);
      if (notice) {
        snprintf(run->line, LINE_BYTES, " X class=%d reason=%d\n", (int)GSHSCENE_ENTRY_NOTICE, (int)notice);
        emit_text(run, run->line, &failed);
        return failed;
      }
      /* A run from a font: its route, decided as the plan decided it; a refused run was reported by the plan. */
      GSHSCENE_Glyph_Run glyphs;
      GSHSCENE_Glyph_Route route;
      int font = gshscene_resource_glyph_run(item->resource, &glyphs) == GSHSCENE_OK &&
          glyphs.source == GSHSCENE_GLYPHS_FROM_FONT;
      memset(&route, 0, sizeof route);
      int routed = font && mapped && gshscene_glyph_route(item->resource, &map, run->job->target.kind, &route) == GSHSCENE_OK;
      if (routed && route.entry_class == GSHSCENE_ENTRY_REFUSED) {
        snprintf(run->line, LINE_BYTES, " X class=%d reason=%d\n", (int)route.entry_class, (int)route.reason);
        emit_text(run, run->line, &failed);
        gshscene_skip_begin(&run->skip, item, count);
        run->skip_reason = (GSHSCENE_Reason)route.reason;
        return failed;
      }
      int bad = !mapped || (font && !routed) || resource_box(item->resource, &model) || box_map(&map, &model, &box);
      double limit = run->sink->config.max_extent;
      if (bad || (limit > 0.0 && !box.empty && (box.x1 - box.x0 > limit || box.y1 - box.y0 > limit))) {
        /* The sink's own resource limit, found while drawing (S6). */
        snprintf(run->line, LINE_BYTES, " X class=%d reason=%d\n", (int)GSHSCENE_ENTRY_REFUSED,
            (int)GSHSCENE_REASON_RESOURCE_LIMIT);
        emit_text(run, run->line, &failed);
        GSHSCENE_Result r = gshscene_job_report(run->job, GSHSCENE_ENTRY_REFUSED, GSHSCENE_REASON_RESOURCE_LIMIT, chain, count);
        if (r != GSHSCENE_OK) {
          return r;
        }
        if (request->flags & GSHSCENE_RENDER_STRICT) {
          return stop_here(run, chain, count, failed);
        }
        gshscene_skip_begin(&run->skip, item, count);
        run->skip_reason = GSHSCENE_REASON_RESOURCE_LIMIT;
        return failed;
      }
      snprintf(run->line, LINE_BYTES, " %c res=%s:%016" PRIx64 " map=%016" PRIx64 " rule=%d",
          item->kind == GSHSCENE_ITEM_FILL ? 'F' : 'C', gshscene_resource_kind_string(gshscene_resource_kind(item->resource)),
          gshscene_resource_hash(item->resource), gshgeom_map_hash(&map), (int)item->fill_rule);
      emit_text(run, run->line, &failed);
      if (item->kind == GSHSCENE_ITEM_FILL && item->paint.kind == GSHSCENE_PAINT_GRADIENT) {
        snprintf(run->line, LINE_BYTES, " grad=%" PRIu64, gshscene_resource_id(item->paint.gradient));
        emit_text(run, run->line, &failed);
      }
      emit_text(run, " box=", &failed);
      if (box.empty) {
        emit_text(run, "-\n", &failed);
      } else {
        snprintf(run->line, LINE_BYTES, "%016" PRIx64 " %016" PRIx64 " %016" PRIx64 " %016" PRIx64 "\n",
            gshgeom_f64_bits(box.x0), gshgeom_f64_bits(box.y0), gshgeom_f64_bits(box.x1), gshgeom_f64_bits(box.y1));
        emit_text(run, run->line, &failed);
      }
      if (item->kind == GSHSCENE_ITEM_FILL && item->paint.kind == GSHSCENE_PAINT_GRADIENT) {
        GSHSCENE_Result r = gradient_record(run, item->paint.gradient, &failed);
        if (r != GSHSCENE_OK) {
          return r;
        }
      }
      if (routed) {
        GSHSCENE_Result r = glyph_records(run, item, &map, &route, chain, count, &failed);
        if (r != GSHSCENE_OK) {
          return r;
        }
      }
      GSHSCENE_Rect r = box.empty ? (GSHSCENE_Rect){0, 0, 0, 0} : box_pixels(&box);
      if (item->kind == GSHSCENE_ITEM_FILL) {
        if (pixel) {
          draw_rect(run, r, mark_of(chain, count), &failed);
        }
      } else {
        GSHSCENE_Rect inside = rect_meet(clip_now(run), r);
        Block * b = &run->blocks[run->block_count++];
        memset(b, 0, sizeof *b);
        b->clip = inside;
      }
      break;
    }
    case GSHSCENE_ITEM_GROUP_PUSH: {
      if (gshscene_group_pdf_default(item->group.isolation, item->group.knockout, item->group.soft_mask)) {
        snprintf(run->line, LINE_BYTES, " G opacity=%016" PRIx64 " composite=%u\n", gshgeom_f64_bits(item->group.opacity),
            (unsigned)item->group.composite);
      } else {
        /* T1: a sink whose caps claim PDF's group fields records them (a configuration can claim them). */
        snprintf(run->line, LINE_BYTES, " G opacity=%016" PRIx64 " composite=%u isolation=%u knockout=%u mask=%016" PRIx64 "\n",
            gshgeom_f64_bits(item->group.opacity), (unsigned)item->group.composite, (unsigned)item->group.isolation,
            (unsigned)item->group.knockout, item->group.soft_mask ? gshscene_resource_hash(item->group.soft_mask) : 0);
      }
      emit_text(run, run->line, &failed);
      GSHSCENE_Rect outer = clip_now(run);
      Block * b = &run->blocks[run->block_count++];
      memset(b, 0, sizeof *b);
      b->group = 1;
      b->outer = outer;
      b->clip = run->area; /* a group does not take the clips around it into its own layer (N9) */
      b->mark = mark_of(chain, count);
      run->groups_open++;
      break;
    }
    case GSHSCENE_ITEM_CLIP_POP:
    case GSHSCENE_ITEM_GROUP_POP: {
      emit_text(run, item->kind == GSHSCENE_ITEM_CLIP_POP ? " c\n" : " g\n", &failed);
      if (run->block_count == 0) {
        return GSHSCENE_ERR_INTERNAL;
      }
      Block b = run->blocks[--run->block_count];
      if (b.group) {
        run->groups_open--;
        if (pixel && b.has_gathered) {
          GSHSCENE_Rect r = rect_meet(b.gathered, b.outer);
          Block * g = innermost_group(run);
          if (!g) {
            stamp(run, r, b.mark, &failed);
          } else if (!rect_empty(r)) {
            g->gathered = g->has_gathered ? rect_join(g->gathered, r) : r;
            g->has_gathered = 1;
          }
        }
      }
      break;
    }
    case GSHSCENE_ITEM_INSTANCE:
      hex(run, " I res=picture:", gshscene_resource_hash(item->resource), &failed);
      emit_text(run, "\n", &failed);
      break;
    case GSHSCENE_ITEM_STROKE:
      return stroke_unit(run, item, chain, count, failed);
    default:
      return GSHSCENE_ERR_INTERNAL;
  }
  return failed;
}

/* --- a run: start, step, finish -------------------------------------------------------- */

static void pending_remove(Dump_Sink * ds, Run * run) {
  GCU_MUTEX_LOCK(ds->lock);
  for (size_t i = 0; i < ds->pending_count; i++) {
    if (ds->pending[i] == run) {
      ds->pending[i] = ds->pending[--ds->pending_count];
      break;
    }
  }
  GCU_MUTEX_UNLOCK(ds->lock);
}

/** Write the last line, hand the result to the contract, release the run. */
static void finish(Run * run, GSHSCENE_Result result) {
  GSHSCENE_Result failed = GSHSCENE_OK;
  snprintf(run->line, LINE_BYTES, "E result=%d processed=%" PRIu64 " done=%" PRIu64 "\n", (int)result,
      (uint64_t)run->processed, (uint64_t)run->done);
  emit_text(run, run->line, &failed);
  if (result == GSHSCENE_OK && failed != GSHSCENE_OK) {
    result = failed;
  }
  Dump_Sink * ds = run->sink;
  GSHSCENE_Job * job = run->job;
  pending_remove(ds, run);
  gshscene_job_set_impl_value(job, run->hash);
  gshscene_job_set_frame_bytes(job, run->bytes);
  gshscene_job_set_impl_state(job, NULL);
  const char * reason = result == GSHSCENE_OK ? NULL
      : result == GSHSCENE_ERR_CANCELLED ? "cancelled between units"
      : result == GSHSCENE_ERR_REFUSED ? "strict: refused while drawing (the sink's resource limit, or a glyph)"
      : result == GSHSCENE_ERR_IO ? "the stream refused a write"
      : "the test sink failed";
  unsigned char * memory = run->memory;
  gshscene_job_complete(job, result, run->stop, run->stop_count, reason);
  gshscene_free(&ds->alloc, memory);
}

/** Go through up to @p limit units (0: all); 1 when the run finished (and is gone). */
static int advance(Run * run, size_t limit) {
  GSHSCENE_Result failed = GSHSCENE_OK;
  if (!run->started) {
    run->started = 1;
    snprintf(run->line, LINE_BYTES, "gshscene.dump-sink %u kind=%u unit=%u units=%" PRIu64 " list=%016" PRIx64 "\n",
        DUMP_FORMAT, (unsigned)run->job->target.kind, (unsigned)run->job->target.unit,
        (uint64_t)run->job->units_total, gshscene_list_hash(run->job->request.list));
    emit_text(run, run->line, &failed);
    if (failed != GSHSCENE_OK) {
      finish(run, failed);
      return 1;
    }
  }
  size_t steps = 0;
  const GSHSCENE_Item * item;
  const size_t * chain;
  size_t count;
  while (limit == 0 || steps < limit) {
    if (!gshscene_expansion_next(&run->walk, &item, &chain, &count)) {
      finish(run, GSHSCENE_OK);
      return 1;
    }
    steps++;
    gshscene_job_work(run->job, 1);
    GSHSCENE_Result r = unit(run, item, chain, count);
    if (r != GSHSCENE_OK) {
      finish(run, r);
      return 1;
    }
    run->processed++;
    if (run->groups_open == 0) {
      run->done = run->processed;
    }
    if (gshscene_job_unit(run->job, run->processed, run->done)) {
      finish(run, GSHSCENE_ERR_CANCELLED);
      return 1;
    }
  }
  return 0;
}

static GSHSCENE_Result op_begin(void * impl, GSHSCENE_Job * job) {
  Dump_Sink * ds = impl;
  const GSHSCENE_Render_Request * request = gshscene_job_request(job);
  size_t levels;
  size_t blocks;
  size_t fills;
  size_t stops;
  size_t strokes;
  size_t need = run_bytes(request->list, &levels, &blocks, &fills, &stops, &strokes);
  unsigned char * memory = NULL;
  unsigned char * base;
  if (request->flags & GSHSCENE_RENDER_CALLER_MEMORY) {
    base = request->workspace; /* the contract layer has checked it holds need bytes */
  } else {
    memory = need == SIZE_MAX ? NULL : gshscene_alloc(&ds->alloc, need);
    if (!memory) {
      return GSHSCENE_ERR_OOM;
    }
    base = memory;
  }
  unsigned char * cursor = (unsigned char *)(((uintptr_t)base + (CARVE_ALIGN - 1)) & ~(uintptr_t)(CARVE_ALIGN - 1));
  Run * run = carve(&cursor, sizeof(Run));
  memset(run, 0, sizeof *run);
  void * walk_memory = carve(&cursor, gshscene_expansion_bytes(request->list));
  run->maps = carve(&cursor, levels * sizeof(GSHGEOM_Map));
  run->blocks = carve(&cursor, blocks * sizeof(Block));
  run->stop = carve(&cursor, levels * sizeof(size_t));
  run->line = carve(&cursor, LINE_BYTES);
  run->gradients = carve(&cursor, fills * sizeof(uint64_t));
  run->stops = carve(&cursor, stops * sizeof(GSHSCENE_Resolved_Stop));
  run->stop_capacity = stops;
  run->styles = carve(&cursor, strokes * sizeof(uint64_t));
  arena_init(&run->glyphs, carve(&cursor, GLYPH_BYTES), GLYPH_BYTES);
  run->sink = ds;
  run->job = job;
  run->memory = memory;
  run->bytes = need;
  run->hash = GSHGEOM_FNV_OFFSET_BASIS;
  if (gshscene_expansion_init(&run->walk, request->list, walk_memory, gshscene_expansion_bytes(request->list)) != GSHSCENE_OK) {
    gshscene_free(&ds->alloc, memory);
    return GSHSCENE_ERR_INTERNAL;
  }
  const GSHSCENE_Target * target = request->target;
  if (target->kind == GSHSCENE_TARGET_RASTER) {
    run->raster = target->surface;
    run->area = target->damage ? rect_meet(target->extent, *target->damage) : target->extent;
    run->out = ds->config.log;
  } else {
    run->out = target->surface;
  }
  gshscene_job_set_impl_state(job, run);
  if (ds->config.completion == GSHSCENE_DUMP_SINK_DEFERRED) {
    GCU_MUTEX_LOCK(ds->lock);
    /* The contract's busy table holds at most max_jobs, and so does this one. */
    ds->pending[ds->pending_count++] = run;
    GCU_MUTEX_UNLOCK(ds->lock);
    return GSHSCENE_OK;
  }
  advance(run, 0);
  return GSHSCENE_OK;
}

static GSHSCENE_Result op_wait(void * impl, GSHSCENE_Job * job) {
  (void)impl;
  Run * run = gshscene_job_impl_state(job);
  if (run) {
    advance(run, 0);
  }
  return GSHSCENE_OK;
}

GSHSCENE_Result gshscene_dump_sink_step(GSHSCENE_Sink * sink, size_t * out_running) {
  if (!sink || !gshscene_dump_sink_is(sink)) {
    return GSHSCENE_ERR_INVALID;
  }
  Dump_Sink * ds = sink->impl;
  if (sink->ops.thread_affine && gshscene_thread_token() != sink->creator) {
    return GSHSCENE_ERR_INVALID;
  }
  /* The runs pending now; a run begun by a completion callback during this step waits for the next. */
  GCU_MUTEX_LOCK(ds->lock);
  size_t count = ds->pending_count;
  GCU_MUTEX_UNLOCK(ds->lock);
  size_t i = 0;
  while (i < count) {
    GCU_MUTEX_LOCK(ds->lock);
    Run * run = i < ds->pending_count ? ds->pending[i] : NULL;
    GCU_MUTEX_UNLOCK(ds->lock);
    if (!run) {
      break;
    }
    if (advance(run, ds->config.step_units)) {
      count--; /* it left the array, and the last pending run took its place */
    } else {
      i++;
    }
  }
  if (out_running) {
    GCU_MUTEX_LOCK(ds->lock);
    *out_running = ds->pending_count;
    GCU_MUTEX_UNLOCK(ds->lock);
  }
  return GSHSCENE_OK;
}

static GSHSCENE_Result op_readback(void * impl, const GSHSCENE_Job * job, GIMG_Raster * out) {
  (void)impl;
  const GSHSCENE_Target * target = &job->target;
  const GIMG_Raster * raster = target->surface;
  const unsigned char * src = gimg_raster_pixels_const(raster);
  unsigned char * dst = gimg_raster_pixels(out);
  size_t src_stride = gimg_raster_stride_bytes(raster);
  size_t dst_stride = gimg_raster_stride_bytes(out);
  size_t row = (size_t)(target->extent.x1 - target->extent.x0) * 4u;
  for (int64_t y = target->extent.y0; y < target->extent.y1; y++) {
    memcpy(dst + (size_t)(y - target->extent.y0) * dst_stride, src + (size_t)y * src_stride + (size_t)target->extent.x0 * 4u,
        row);
  }
  return GSHSCENE_OK;
}

/* --- prepare and evict (S9) ---------------------------------------------------------------- */

/** Hold @p resource by id, if not held yet. */
static GSHSCENE_Result cache_hold(Dump_Sink * ds, const GSHSCENE_Resource * resource) {
  uint64_t id = gshscene_resource_id(resource);
  for (size_t i = 0; i < ds->cache_count; i++) {
    if (ds->cache[i].id == id) {
      return GSHSCENE_OK;
    }
  }
  if (ds->cache_count == ds->cache_capacity) {
    size_t want = ds->cache_capacity ? ds->cache_capacity * 2 : 16;
    Cached * grown = ds->alloc.realloc_fn(ds->alloc.ctx, ds->cache, want * sizeof *grown);
    if (!grown) {
      return GSHSCENE_ERR_OOM;
    }
    ds->cache = grown;
    ds->cache_capacity = want;
  }
  ds->cache[ds->cache_count].id = id;
  ds->cache[ds->cache_count].resource = gshscene_resource_retain(resource);
  ds->cache_count++;
  ds->static_bytes += gshscene_resource_bytes(resource);
  return GSHSCENE_OK;
}

/** Hold every resource @p list names, its pictures' and their outlines. */
static GSHSCENE_Result cache_list(Dump_Sink * ds, const GSHSCENE_List * list) {
  size_t count = gshscene_list_count(list);
  for (size_t i = 0; i < count; i++) {
    const GSHSCENE_Item * item = NULL;
    if (gshscene_list_item(list, i, &item) != GSHSCENE_OK) {
      return GSHSCENE_ERR_INTERNAL;
    }
    if (!item->resource) {
      continue;
    }
    GSHSCENE_Result result = cache_hold(ds, item->resource);
    GSHSCENE_Glyph_Run run;
    if (result == GSHSCENE_OK && gshscene_resource_glyph_run(item->resource, &run) == GSHSCENE_OK) {
      for (size_t g = 0; g < run.glyph_count && result == GSHSCENE_OK; g++) {
        if (run.glyphs[g].outline) {
          result = cache_hold(ds, run.glyphs[g].outline);
        }
      }
    }
    if (result == GSHSCENE_OK && item->kind == GSHSCENE_ITEM_INSTANCE) {
      result = cache_list(ds, gshscene_resource_picture(item->resource));
    }
    if (result != GSHSCENE_OK) {
      return result;
    }
  }
  return GSHSCENE_OK;
}

static GSHSCENE_Result op_prepare(void * impl, const GSHSCENE_List * list, const GSHSCENE_Target * target) {
  (void)target;
  Dump_Sink * ds = impl;
  GCU_MUTEX_LOCK(ds->lock);
  GSHSCENE_Result result = cache_list(ds, list);
  GCU_MUTEX_UNLOCK(ds->lock);
  return result;
}

static GSHSCENE_Result op_evict(void * impl, uint64_t id) {
  Dump_Sink * ds = impl;
  const GSHSCENE_Resource * released = NULL;
  GCU_MUTEX_LOCK(ds->lock);
  for (size_t i = 0; i < ds->cache_count; i++) {
    if (ds->cache[i].id == id) {
      released = ds->cache[i].resource;
      ds->static_bytes -= gshscene_resource_bytes(released);
      ds->cache[i] = ds->cache[--ds->cache_count];
      break;
    }
  }
  GCU_MUTEX_UNLOCK(ds->lock);
  gshscene_resource_release(released);
  return GSHSCENE_OK;
}

static uint64_t op_static_bytes(void * impl) {
  Dump_Sink * ds = impl;
  GCU_MUTEX_LOCK(ds->lock);
  uint64_t bytes = ds->static_bytes;
  GCU_MUTEX_UNLOCK(ds->lock);
  return bytes;
}

static void op_destroy(void * impl) {
  Dump_Sink * ds = impl;
  for (size_t i = 0; i < ds->cache_count; i++) {
    gshscene_resource_release(ds->cache[i].resource);
  }
  GSHSCENE_Allocator alloc = ds->alloc;
  GCU_MUTEX_DESTROY(ds->lock);
  gshscene_free(&alloc, ds->cache);
  gshscene_free(&alloc, ds->pending);
  gshscene_free(&alloc, ds);
}

/* --- making one ------------------------------------------------------------------------------- */

int gshscene_dump_sink_is(const GSHSCENE_Sink * sink) {
  return sink && sink->ops.caps == op_caps;
}

GSHSCENE_Result gshscene_dump_sink_create(const GSHSCENE_Allocator * alloc, const GSHSCENE_Limits * limits,
    const GSHSCENE_Dump_Sink_Config * config, GSHSCENE_Sink ** out) {
  if (!out) {
    return GSHSCENE_ERR_INVALID;
  }
  GSHSCENE_Dump_Sink_Config c;
  if (config) {
    c = *config;
  } else {
    gshscene_dump_sink_config_default(&c);
  }
  uint32_t affine = c.raster_caps.flags & GSHSCENE_CAP_THREAD_AFFINE;
  int can_block = (c.raster_caps.flags & GSHSCENE_CAP_CAN_BLOCK) != 0;
  for (size_t i = 0; i < sizeof c._reserved; i++) {
    if (c._reserved[i]) {
      return GSHSCENE_ERR_INVALID;
    }
  }
  if (!caps_valid(&c.raster_caps) || !caps_valid(&c.stream_caps) ||
      affine != (c.stream_caps.flags & GSHSCENE_CAP_THREAD_AFFINE) ||
      can_block != ((c.stream_caps.flags & GSHSCENE_CAP_CAN_BLOCK) != 0) ||
      (c.targets & ~((UINT32_C(1) << GSHSCENE_TARGET_RASTER) | (UINT32_C(1) << GSHSCENE_TARGET_STREAM))) ||
      c.completion >= GSHSCENE_DUMP_SINK_COMPLETION_COUNT || !(c.max_extent >= 0.0) || !gshscene_finite(c.max_extent) ||
      (c.completion == GSHSCENE_DUMP_SINK_INLINE && !can_block) || (c.log && !c.log->write)) {
    return GSHSCENE_ERR_INVALID;
  }
  if (!alloc) {
    alloc = gshscene_allocator_default();
  }
  size_t max_jobs = limits && limits->max_jobs ? limits->max_jobs : GSHSCENE_LIMITS_DEFAULT_MAX_JOBS;
  Dump_Sink * ds = gshscene_calloc(alloc, 1, sizeof *ds);
  if (!ds) {
    return GSHSCENE_ERR_OOM;
  }
  ds->alloc = *alloc;
  ds->config = c;
  ds->pending = max_jobs > SIZE_MAX / sizeof(Run *) ? NULL : gshscene_calloc(alloc, max_jobs, sizeof(Run *));
  if (!ds->pending) {
    gshscene_free(alloc, ds);
    return GSHSCENE_ERR_OOM;
  }
  if (GCU_MUTEX_CREATE(ds->lock) != 0) {
    gshscene_free(alloc, ds->pending);
    gshscene_free(alloc, ds);
    return GSHSCENE_ERR_OOM;
  }
  GSHSCENE_Sink_Ops ops = {0};
  ops.name = "dump";
  ops.thread_affine = affine != 0;
  ops.caps = op_caps;
  ops.workspace_bytes = op_workspace_bytes;
  ops.begin = op_begin;
  ops.wait = op_wait;
  ops.readback = op_readback;
  ops.prepare = op_prepare;
  ops.evict = op_evict;
  ops.static_bytes = op_static_bytes;
  ops.destroy = op_destroy;
  GSHSCENE_Result result = gshscene_sink_create(alloc, limits, &ops, ds, &ds->sink);
  if (result != GSHSCENE_OK) {
    op_destroy(ds);
    return result;
  }
  *out = ds->sink;
  return GSHSCENE_OK;
}

GSHSCENE_Result gshscene_dump_sink_output_hash(const GSHSCENE_Job * job, uint64_t * out) {
  if (!job || !out || !gshscene_dump_sink_is(job->sink)) {
    return GSHSCENE_ERR_INVALID;
  }
  GSHSCENE_Job_Status status;
  if (gshscene_job_poll(job, &status) != GSHSCENE_OK || status.state != GSHSCENE_JOB_DONE) {
    return GSHSCENE_ERR_INVALID;
  }
  *out = gshscene_job_impl_value(job);
  return GSHSCENE_OK;
}
