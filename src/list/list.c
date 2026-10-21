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
 * The list: the arena, the items, the record calls, close and reset, equality,
 * hash and dump (list.h).
 *
 * The arena is a chain of chunks from the maker's allocator.  Items live in
 * segments of 16, 32, 64, ... items, each one arena allocation, so an item
 * never moves and `item(i)` is a shift and an index.  A reset frees every
 * chunk but the first and starts over in it.
 */

#include <ghoti.io/shape-scene/macros.h>

#include "../internal.h"

#include <stdlib.h>
#include <string.h>

/** The format of a list's hash and dump. */
#define LIST_FORMAT_VERSION 1u

/** The kind field of a list's hash: "LIST" in little-endian ASCII. */
#define LIST_HASH_KIND UINT32_C(0x5453494c)

/** What an open push was. */
#define PUSH_CLIP 1u
#define PUSH_GROUP 2u

#define CHUNK_HEADER \
  ((sizeof(GSHSCENE_Chunk) + GSHSCENE_ARENA_ALIGN - 1) & ~(size_t)(GSHSCENE_ARENA_ALIGN - 1))

/* Which resource kinds an item takes. */
#define TAKES_PATH 1u
#define TAKES_RUN 2u
#define TAKES_PICTURE 4u

/* --- the arena -------------------------------------------------------------- */

static size_t saturating_add(size_t a, size_t b) {
  return a > SIZE_MAX - b ? SIZE_MAX : a + b;
}

/* Take @p size bytes (aligned) from the arena: bump the newest standard chunk,
 * or take a new chunk, or a dedicated one for a large request. */
static GSHSCENE_Result arena_alloc(GSHSCENE_List * list, size_t size, void ** out) {
  if (size > SIZE_MAX - GSHSCENE_ARENA_ALIGN) {
    return GSHSCENE_ERR_OOM;
  }
  size = (size + GSHSCENE_ARENA_ALIGN - 1) & ~(size_t)(GSHSCENE_ARENA_ALIGN - 1);
  GSHSCENE_Chunk * chunk = list->head;
  int standard = size <= GSHSCENE_CHUNK_PAYLOAD;
  if (standard && chunk && chunk->capacity - chunk->used >= size) {
    *out = (unsigned char *)chunk + CHUNK_HEADER + chunk->used;
    chunk->used += size;
    return GSHSCENE_OK;
  }
  size_t payload = standard ? GSHSCENE_CHUNK_PAYLOAD : size;
  if (payload > SIZE_MAX - CHUNK_HEADER) {
    return GSHSCENE_ERR_OOM;
  }
  size_t total = CHUNK_HEADER + payload;
  if (list->bytes > list->limits.max_list_bytes || total > list->limits.max_list_bytes - list->bytes) {
    return GSHSCENE_ERR_LIMIT;
  }
  chunk = gshscene_alloc(&list->alloc, total);
  if (!chunk) {
    return GSHSCENE_ERR_OOM;
  }
  chunk->capacity = payload;
  chunk->used = size;
  if (standard) {
    chunk->next = list->head;
    list->head = chunk;
    if (!list->first) {
      list->first = chunk;
    }
  } else {
    chunk->next = list->big;
    list->big = chunk;
  }
  list->bytes += total;
  *out = (unsigned char *)chunk + CHUNK_HEADER;
  return GSHSCENE_OK;
}

static void chunks_free(GSHSCENE_List * list, GSHSCENE_Chunk * chunk, const GSHSCENE_Chunk * keep) {
  while (chunk) {
    GSHSCENE_Chunk * next = chunk->next;
    if (chunk != keep) {
      gshscene_free(&list->alloc, chunk);
    }
    chunk = next;
  }
}

/* --- limits ----------------------------------------------------------------- */

void gshscene_limits_effective(const GSHSCENE_Limits * limits, GSHSCENE_Limits * out) {
  gshscene_limits_default(out);
  if (!limits) {
    return;
  }
  if (limits->max_resources) {
    out->max_resources = limits->max_resources;
  }
  if (limits->max_resource_elements) {
    out->max_resource_elements = limits->max_resource_elements;
  }
  if (limits->max_list_items) {
    out->max_list_items = limits->max_list_items;
  }
  if (limits->max_list_bytes) {
    out->max_list_bytes = limits->max_list_bytes;
  }
  if (limits->max_depth) {
    out->max_depth = limits->max_depth;
  }
  if (limits->max_picture_depth) {
    out->max_picture_depth = limits->max_picture_depth;
  }
  if (limits->max_expansion) {
    out->max_expansion = limits->max_expansion;
  }
  if (limits->max_bytes) {
    out->max_bytes = limits->max_bytes;
  }
  if (limits->max_nodes) {
    out->max_nodes = limits->max_nodes;
  }
  if (limits->max_node_depth) {
    out->max_node_depth = limits->max_node_depth;
  }
  if (out->max_node_depth > GSHSCENE_NODE_DEPTH_CEILING) {
    out->max_node_depth = GSHSCENE_NODE_DEPTH_CEILING;
  }
  if (out->max_list_items > GSHSCENE_LIST_ITEMS_CEILING) {
    out->max_list_items = GSHSCENE_LIST_ITEMS_CEILING;
  }
}

/* --- create, reset, destroy --------------------------------------------------- */

GSHSCENE_Result gshscene_list_create_effective(const GSHSCENE_Allocator * alloc,
    const GSHSCENE_Limits * effective, GSHSCENE_Store * store, GSHSCENE_List ** out) {
  GSHSCENE_List * list = gshscene_calloc(alloc, 1, sizeof *list);
  if (!list) {
    return GSHSCENE_ERR_OOM;
  }
  list->alloc = *alloc;
  list->limits = *effective;
  list->store = store;
  gshscene_color_init(&list->foreground);
  gshscene_store_ref(store);
  *out = list;
  return GSHSCENE_OK;
}

GSHSCENE_Result gshscene_list_create(const GSHSCENE_Allocator * alloc, const GSHSCENE_Limits * limits,
    GSHSCENE_Store * store, GSHSCENE_List ** out) {
  if (!store || !out) {
    return GSHSCENE_ERR_INVALID;
  }
  if (!alloc) {
    alloc = gshscene_allocator_default();
  }
  GSHSCENE_Limits effective;
  gshscene_limits_effective(limits, &effective);
  return gshscene_list_create_effective(alloc, &effective, store, out);
}

static void items_release(GSHSCENE_List * list);

static void list_clear(GSHSCENE_List * list) {
  items_release(list);
  gshscene_resource_release(list->palette);
  list->palette = NULL;
  chunks_free(list, list->head, list->first);
  chunks_free(list, list->big, NULL);
  list->big = NULL;
  list->head = list->first;
  list->bytes = 0;
  if (list->first) {
    list->first->used = 0;
    list->bytes = CHUNK_HEADER + list->first->capacity;
  }
  memset(list->segments, 0, sizeof list->segments);
  list->segment_count = 0;
  list->capacity = 0;
  list->count = 0;
  list->stack = NULL;
  list->stack_capacity = 0;
  list->depth = 0;
  list->expansion = 0;
  list->picture_depth = 0;
  list->nonflat = 0;
  gshscene_color_init(&list->foreground);
  gshscene_free(&list->alloc, list->ties);
  list->ties = NULL;
  list->tie_count = 0;
  list->tie_capacity = 0;
  list->closed = 0;
}

GSHSCENE_Result gshscene_list_reset(GSHSCENE_List * list) {
  if (!list) {
    return GSHSCENE_ERR_INVALID;
  }
  list_clear(list);
  return GSHSCENE_OK;
}

void gshscene_list_destroy(GSHSCENE_List * list) {
  if (!list) {
    return;
  }
  items_release(list);
  gshscene_resource_release(list->palette);
  chunks_free(list, list->head, NULL);
  chunks_free(list, list->big, NULL);
  gshscene_free(&list->alloc, list->ties);
  GSHSCENE_Store * store = list->store;
  GSHSCENE_Allocator alloc = list->alloc;
  gshscene_free(&alloc, list);
  gshscene_store_unref(store);
}

/* --- items ------------------------------------------------------------------ */

static GSHSCENE_Item * item_at(const GSHSCENE_List * list, size_t index) {
  size_t q = index / GSHSCENE_SEGMENT_FIRST + 1;
  size_t s = 0;
  while ((q >> (s + 1)) != 0) {
    s++;
  }
  size_t base = GSHSCENE_SEGMENT_FIRST * (((size_t)1 << s) - 1);
  return &list->segments[s][index - base];
}

static void item_release(const GSHSCENE_Item * item) {
  gshscene_resource_release(item->resource);
  gshscene_resource_release(item->paint.gradient);
  gshscene_resource_release(item->group.soft_mask);
  gshscene_resource_release(item->style);
}

static void items_release(GSHSCENE_List * list) {
  for (size_t i = 0; i < list->count; i++) {
    item_release(item_at(list, i));
  }
}

/* The deepest picture an item names: an instance's, or a group's soft mask (T7; a sink that composites masks walks
 * it, so it counts toward the nesting limit now). */
static size_t item_picture_depth(const GSHSCENE_Item * item) {
  if (item->kind == GSHSCENE_ITEM_INSTANCE) {
    return item->resource->u.picture.depth;
  }
  if (item->kind == GSHSCENE_ITEM_GROUP_PUSH && item->group.soft_mask) {
    return item->group.soft_mask->u.picture.depth;
  }
  return 0;
}

/* A zeroed item of a kind: the identity map, the nonzero rule, no resource. */
static void item_init(GSHSCENE_Item * item, GSHSCENE_Item_Kind kind) {
  memset(item, 0, sizeof *item);
  item->kind = kind;
  gshgeom_map_identity(2, &item->map);
  item->fill_rule = GSHGEOM_FILL_NONZERO;
  item->paint.kind = GSHSCENE_PAINT_SOLID;
}

/* Count what close needs to know as an item joins the list. */
static void note_item(GSHSCENE_List * list, const GSHSCENE_Item * item) {
  if (item->kind == GSHSCENE_ITEM_GROUP_PUSH && item->group.ordering != (uint32_t)GSHSCENE_ORDER_FLAT) {
    list->nonflat++;
  }
}

static GSHSCENE_Result segment_alloc(GSHSCENE_List * list) {
  size_t s = list->segment_count;
  if (s >= GSHSCENE_SEGMENT_MAX) {
    return GSHSCENE_ERR_LIMIT;
  }
  size_t items = (size_t)GSHSCENE_SEGMENT_FIRST << s;
  if (items > SIZE_MAX / sizeof(GSHSCENE_Item)) {
    return GSHSCENE_ERR_LIMIT;
  }
  void * memory = NULL;
  GSHSCENE_Result result = arena_alloc(list, items * sizeof(GSHSCENE_Item), &memory);
  if (result != GSHSCENE_OK) {
    return result;
  }
  list->segments[s] = (GSHSCENE_Item *)memory;
  list->segment_count++;
  list->capacity += items;
  return GSHSCENE_OK;
}

/* Append a finished item: check the limits, take the reference, count it.  A
 * failure changes nothing the list reports. */
static GSHSCENE_Result list_append(GSHSCENE_List * list, const GSHSCENE_Item * item) {
  size_t add = 1;
  if (item->kind == GSHSCENE_ITEM_INSTANCE) {
    add = saturating_add(1, gshscene_list_expansion(gshscene_resource_picture(item->resource)));
  }
  if (list->count >= list->limits.max_list_items) {
    return GSHSCENE_ERR_LIMIT;
  }
  size_t total = saturating_add(list->expansion, add);
  if (total > list->limits.max_expansion) {
    return GSHSCENE_ERR_LIMIT;
  }
  if (list->count == list->capacity) {
    GSHSCENE_Result result = segment_alloc(list);
    if (result != GSHSCENE_OK) {
      return result;
    }
  }
  *item_at(list, list->count) = *item;
  gshscene_resource_retain(item->resource);
  gshscene_resource_retain(item->paint.gradient);
  gshscene_resource_retain(item->group.soft_mask);
  gshscene_resource_retain(item->style);
  note_item(list, item);
  list->count++;
  list->expansion = total;
  if (item_picture_depth(item) > list->picture_depth) {
    list->picture_depth = item_picture_depth(item);
  }
  return GSHSCENE_OK;
}

/* --- validating the arguments of a record call ---------------------------------- */

static GSHSCENE_Result check_resource(const GSHSCENE_List * list, const GSHSCENE_Resource * resource, unsigned takes) {
  if (!resource || resource->store != list->store) {
    return GSHSCENE_ERR_INVALID;
  }
  if (resource->kind == GSHSCENE_RESOURCE_PATH && (takes & TAKES_PATH)) {
    return GSHSCENE_OK;
  }
  if (resource->kind == GSHSCENE_RESOURCE_GLYPH_RUN && (takes & TAKES_RUN)) {
    return GSHSCENE_OK;
  }
  if (resource->kind == GSHSCENE_RESOURCE_PICTURE && (takes & TAKES_PICTURE)) {
    return GSHSCENE_OK;
  }
  return GSHSCENE_ERR_INVALID;
}

/* The canonical map: valid, and only the entries a map of its dimension uses. */
static GSHSCENE_Result canonical_map(const GSHGEOM_Map * map, GSHGEOM_Map * out) {
  if (!map) {
    gshgeom_map_identity(2, out);
    return GSHSCENE_OK;
  }
  if (gshgeom_map_check(map) != GSHGEOM_OK) {
    return GSHSCENE_ERR_INVALID;
  }
  gshgeom_map_identity(map->dim, out);
  out->kind = map->kind;
  for (int i = 0; i < map->dim; i++) {
    for (int j = 0; j < map->dim; j++) {
      out->m[i][j] = map->m[i][j];
    }
    out->t[i] = map->t[i];
  }
  return GSHSCENE_OK;
}

static GSHSCENE_Result check_rule(GSHGEOM_Fill_Rule rule) {
  return (unsigned)rule < (unsigned)GSHGEOM_FILL_RULE_COUNT ? GSHSCENE_OK : GSHSCENE_ERR_INVALID;
}

static GSHSCENE_Result canonical_paint(const GSHSCENE_Store * store, const GSHSCENE_Paint * paint, GSHSCENE_Paint * out) {
  memset(out, 0, sizeof *out);
  out->kind = GSHSCENE_PAINT_SOLID;
  out->rgba[3] = 1.0;
  if (!paint) {
    return GSHSCENE_OK;
  }
  if (paint->kind != GSHSCENE_PAINT_SOLID && paint->kind != GSHSCENE_PAINT_FOREGROUND &&
      paint->kind != GSHSCENE_PAINT_PALETTE && paint->kind != GSHSCENE_PAINT_GRADIENT) {
    return GSHSCENE_ERR_INVALID;
  }
  /* One spelling: only a gradient paint names a resource, a gradient of the list's store (G1). */
  if (paint->kind == GSHSCENE_PAINT_GRADIENT
          ? (!paint->gradient || paint->gradient->store != store || paint->gradient->kind != GSHSCENE_RESOURCE_GRADIENT)
          : paint->gradient != NULL) {
    return GSHSCENE_ERR_INVALID;
  }
  /* One spelling: only a palette paint has an entry, and 0xFFFF (a font's foreground) is never one (N14). */
  if (paint->kind == GSHSCENE_PAINT_PALETTE ? paint->entry > GSHSCENE_PALETTE_ENTRY_MAX : paint->entry != 0) {
    return GSHSCENE_ERR_INVALID;
  }
  for (int i = 0; i < 4; i++) {
    if (!gshscene_finite(paint->rgba[i])) {
      return GSHSCENE_ERR_INVALID;
    }
  }
  if (!(paint->rgba[3] >= 0.0 && paint->rgba[3] <= 1.0)) {
    return GSHSCENE_ERR_INVALID;
  }
  if (paint->kind != GSHSCENE_PAINT_SOLID) {
    /* One spelling: the foreground's color is the version's, and an entry's the palette's, so only the alpha
     * multiplier (and the entry) is the paint's. */
    if (gshgeom_f64_bits(paint->rgba[0]) != 0 || gshgeom_f64_bits(paint->rgba[1]) != 0 ||
        gshgeom_f64_bits(paint->rgba[2]) != 0 || !gshscene_color_space_is_target(&paint->space)) {
      return GSHSCENE_ERR_INVALID;
    }
    out->kind = paint->kind;
    out->rgba[3] = paint->rgba[3];
    out->entry = paint->entry;
    out->gradient = paint->gradient;
    return GSHSCENE_OK;
  }
  GSHSCENE_Result result = gshscene_color_space_check(&paint->space);
  if (result != GSHSCENE_OK) {
    return result;
  }
  for (int i = 0; i < 4; i++) {
    out->rgba[i] = paint->rgba[i];
  }
  out->space = paint->space;
  return GSHSCENE_OK;
}

GSHSCENE_Result gshscene_list_canonical_depth(double depth, double * out) {
  if (!gshscene_finite(depth)) {
    return GSHSCENE_ERR_INVALID;
  }
  *out = depth == 0.0 ? 0.0 : depth; /* -0 and +0 are one depth, and one spelling */
  return GSHSCENE_OK;
}

static GSHSCENE_Result canonical_group(const GSHSCENE_Store * store, const GSHSCENE_Group * group, GSHSCENE_Group * out) {
  memset(out, 0, sizeof *out);
  out->opacity = 1.0;
  out->ordering = GSHSCENE_ORDER_FLAT;
  out->composite = GSHSCENE_COMPOSITE_SRC_OVER;
  if (!group) {
    return GSHSCENE_OK;
  }
  if (!gshscene_finite(group->opacity) || !(group->opacity >= 0.0 && group->opacity <= 1.0)) {
    return GSHSCENE_ERR_INVALID;
  }
  if (group->ordering >= (uint32_t)GSHSCENE_ORDERING_COUNT || group->composite >= (uint32_t)GSHSCENE_COMPOSITE_COUNT) {
    return GSHSCENE_ERR_INVALID;
  }
  /* T1: PDF's group fields, their defaults zero. */
  if (group->isolation >= (uint32_t)GSHSCENE_ISOLATION_COUNT || group->knockout > 1u) {
    return GSHSCENE_ERR_INVALID;
  }
  if (group->soft_mask &&
      (group->soft_mask->kind != GSHSCENE_RESOURCE_PICTURE || group->soft_mask->store != store)) {
    return GSHSCENE_ERR_INVALID;
  }
  GSHSCENE_Result result = gshscene_color_space_check(&group->blend_space);
  if (result != GSHSCENE_OK) {
    return result;
  }
  out->opacity = group->opacity;
  out->blend_space = group->blend_space;
  out->ordering = group->ordering;
  out->composite = group->composite;
  out->isolation = group->isolation;
  out->knockout = group->knockout;
  out->soft_mask = group->soft_mask;
  return GSHSCENE_OK;
}

/* --- T1: the spelling of PDF's group fields, shared with the retained scene's group nodes ---------------------- */

int gshscene_group_pdf_default(uint32_t isolation, uint32_t knockout, const GSHSCENE_Resource * soft_mask) {
  return isolation == (uint32_t)GSHSCENE_ISOLATION_ISOLATED && knockout == 0 && soft_mask == NULL;
}

void gshscene_group_hash_composite(GSHGEOM_Hash * h, uint32_t composite, uint32_t isolation, uint32_t knockout,
    const GSHSCENE_Resource * soft_mask) {
  if (gshscene_group_pdf_default(isolation, knockout, soft_mask)) {
    gshgeom_hash_u8(h, (uint8_t)composite); /* the spelling every group had before the fields existed */
    return;
  }
  gshgeom_hash_u8(h, (uint8_t)(composite | 0x80u));
  gshgeom_hash_u8(h, (uint8_t)((isolation == (uint32_t)GSHSCENE_ISOLATION_NON_ISOLATED ? 1u : 0u) | (knockout ? 2u : 0u)));
  gshgeom_hash_u64(h, soft_mask ? gshscene_resource_hash(soft_mask) : 0);
}

int gshscene_group_dump_pdf(FILE * out, uint32_t isolation, uint32_t knockout, const GSHSCENE_Resource * soft_mask) {
  if (gshscene_group_pdf_default(isolation, knockout, soft_mask)) {
    return 0;
  }
  return fprintf(out, " isolation=%u knockout=%u mask=%016llx", (unsigned)isolation, (unsigned)knockout,
      (unsigned long long)(soft_mask ? gshscene_resource_hash(soft_mask) : 0));
}

/* The same checks for a retained scene's nodes (src/scene), which share the rules of a fill. */
GSHSCENE_Result gshscene_list_canonical_map(const GSHGEOM_Map * map, GSHGEOM_Map * out) {
  return canonical_map(map, out);
}

GSHSCENE_Result gshscene_list_canonical_paint_valid(const GSHSCENE_Store * store, const GSHSCENE_Paint * paint,
    GSHSCENE_Paint * out) {
  return canonical_paint(store, paint, out);
}

GSHSCENE_Result gshscene_list_canonical_paint(const GSHSCENE_Store * store, const GSHSCENE_Paint * paint,
    GSHSCENE_Paint * out) {
  GSHSCENE_Result result = canonical_paint(store, paint, out);
  if (result != GSHSCENE_OK) {
    return result;
  }
  /* N1: the bound, after every validity check (a shape node's paint is its last). */
  return gshscene_color_bounded(out->rgba) ? GSHSCENE_OK : GSHSCENE_ERR_LIMIT;
}

GSHSCENE_Result gshscene_list_canonical_group(
    const GSHSCENE_Store * store, const GSHSCENE_Group * group, GSHSCENE_Group * out) {
  return canonical_group(store, group, out);
}

GSHSCENE_Result gshscene_list_check_rule(GSHGEOM_Fill_Rule rule) {
  return check_rule(rule);
}

/* --- the push stack ------------------------------------------------------------ */

/* Room for one more open push, and the depth limit.  Nothing is pushed. */
static GSHSCENE_Result stack_reserve(GSHSCENE_List * list) {
  if (list->depth >= list->limits.max_depth) {
    return GSHSCENE_ERR_LIMIT;
  }
  if (list->depth < list->stack_capacity) {
    return GSHSCENE_OK;
  }
  size_t capacity = list->stack_capacity ? list->stack_capacity * 2 : 16;
  void * memory = NULL;
  GSHSCENE_Result result = arena_alloc(list, capacity, &memory);
  if (result != GSHSCENE_OK) {
    return result;
  }
  if (list->depth) {
    memcpy(memory, list->stack, list->depth);
  }
  list->stack = (uint8_t *)memory;
  list->stack_capacity = capacity;
  return GSHSCENE_OK;
}

/* --- the record calls ---------------------------------------------------------- */

GSHSCENE_Result gshscene_list_fill(GSHSCENE_List * list, const GSHSCENE_Resource * resource,
    const GSHGEOM_Map * map, GSHGEOM_Fill_Rule rule, const GSHSCENE_Paint * paint, int32_t layer, double depth) {
  if (!list || list->closed) {
    return GSHSCENE_ERR_INVALID;
  }
  GSHSCENE_Item item;
  item_init(&item, GSHSCENE_ITEM_FILL);
  GSHSCENE_Result result = check_resource(list, resource, TAKES_PATH | TAKES_RUN);
  if (result == GSHSCENE_OK) {
    result = canonical_map(map, &item.map);
  }
  if (result == GSHSCENE_OK) {
    result = check_rule(rule);
  }
  if (result == GSHSCENE_OK) {
    result = canonical_paint(list->store, paint, &item.paint);
  }
  if (result == GSHSCENE_OK) {
    result = gshscene_list_canonical_depth(depth, &item.depth);
  }
  if (result == GSHSCENE_OK && !gshscene_color_bounded(item.paint.rgba)) {
    result = GSHSCENE_ERR_LIMIT; /* N1, after every validity check */
  }
  if (result != GSHSCENE_OK) {
    return result;
  }
  item.resource = resource;
  item.fill_rule = rule;
  item.layer = layer;
  return list_append(list, &item);
}

/* SK4: whether a centerline is not planar: a 3D path, or a run whose outline is one (a run from a font is 2D). */
int gshscene_centerline_3d(const GSHSCENE_Resource * resource) {
  if (resource->kind == GSHSCENE_RESOURCE_PATH) {
    return gshgeom_path_dimension(resource->u.path) == 3;
  }
  const GSHSCENE_Glyph_Run * run = &resource->u.glyphs.run;
  for (size_t i = 0; run->source == GSHSCENE_GLYPHS_FROM_OUTLINES && i < run->glyph_count; i++) {
    if (gshgeom_path_dimension(run->glyphs[i].outline->u.path) == 3) {
      return 1;
    }
  }
  return 0;
}

GSHSCENE_Result gshscene_list_stroke(GSHSCENE_List * list, const GSHSCENE_Resource * centerline,
    const GSHSCENE_Resource * style, const GSHGEOM_Map * map, const GSHSCENE_Paint * paint, int32_t layer, double depth) {
  if (!list || list->closed) {
    return GSHSCENE_ERR_INVALID;
  }
  GSHSCENE_Item item;
  item_init(&item, GSHSCENE_ITEM_STROKE);
  GSHSCENE_Result result = check_resource(list, centerline, TAKES_PATH | TAKES_RUN);
  if (result == GSHSCENE_OK && (!style || style->store != list->store || style->kind != GSHSCENE_RESOURCE_STROKE_STYLE)) {
    result = GSHSCENE_ERR_INVALID;
  }
  if (result == GSHSCENE_OK) {
    result = canonical_map(map, &item.map);
  }
  if (result == GSHSCENE_OK) {
    result = canonical_paint(list->store, paint, &item.paint);
  }
  if (result == GSHSCENE_OK) {
    result = gshscene_list_canonical_depth(depth, &item.depth);
  }
  if (result == GSHSCENE_OK && (item.map.dim != 2 || gshscene_centerline_3d(centerline))) {
    result = GSHSCENE_ERR_UNSUPPORTED; /* SK4: a stroke is planar */
  }
  if (result == GSHSCENE_OK && !gshscene_color_bounded(item.paint.rgba)) {
    result = GSHSCENE_ERR_LIMIT; /* N1, after every other check */
  }
  if (result != GSHSCENE_OK) {
    return result;
  }
  item.resource = centerline;
  item.style = style;
  item.layer = layer;
  return list_append(list, &item);
}

GSHSCENE_Result gshscene_list_clip_push(GSHSCENE_List * list, const GSHSCENE_Resource * resource,
    const GSHGEOM_Map * map, GSHGEOM_Fill_Rule rule, int32_t layer, double depth) {
  if (!list || list->closed) {
    return GSHSCENE_ERR_INVALID;
  }
  GSHSCENE_Item item;
  item_init(&item, GSHSCENE_ITEM_CLIP_PUSH);
  GSHSCENE_Result result = check_resource(list, resource, TAKES_PATH | TAKES_RUN);
  if (result == GSHSCENE_OK) {
    result = canonical_map(map, &item.map);
  }
  if (result == GSHSCENE_OK) {
    result = check_rule(rule);
  }
  if (result == GSHSCENE_OK) {
    result = gshscene_list_canonical_depth(depth, &item.depth);
  }
  if (result == GSHSCENE_OK) {
    result = stack_reserve(list);
  }
  if (result != GSHSCENE_OK) {
    return result;
  }
  item.resource = resource;
  item.fill_rule = rule;
  item.layer = layer;
  result = list_append(list, &item);
  if (result == GSHSCENE_OK) {
    list->stack[list->depth++] = PUSH_CLIP;
  }
  return result;
}

/* A pop: the innermost open push must be of @p kind. */
static GSHSCENE_Result pop(GSHSCENE_List * list, uint8_t push, GSHSCENE_Item_Kind kind) {
  if (!list || list->closed || list->depth == 0 || list->stack[list->depth - 1] != push) {
    return GSHSCENE_ERR_INVALID;
  }
  GSHSCENE_Item item;
  item_init(&item, kind);
  GSHSCENE_Result result = list_append(list, &item);
  if (result == GSHSCENE_OK) {
    list->depth--;
  }
  return result;
}

GSHSCENE_Result gshscene_list_clip_pop(GSHSCENE_List * list) {
  return pop(list, PUSH_CLIP, GSHSCENE_ITEM_CLIP_POP);
}

GSHSCENE_Result gshscene_list_group_push(GSHSCENE_List * list, const GSHSCENE_Group * group, int32_t layer, double depth) {
  if (!list || list->closed) {
    return GSHSCENE_ERR_INVALID;
  }
  GSHSCENE_Item item;
  item_init(&item, GSHSCENE_ITEM_GROUP_PUSH);
  GSHSCENE_Result result = canonical_group(list->store, group, &item.group);
  if (result == GSHSCENE_OK) {
    result = gshscene_list_canonical_depth(depth, &item.depth);
  }
  if (result == GSHSCENE_OK) {
    result = stack_reserve(list);
  }
  if (result != GSHSCENE_OK) {
    return result;
  }
  item.layer = layer;
  result = list_append(list, &item);
  if (result == GSHSCENE_OK) {
    list->stack[list->depth++] = PUSH_GROUP;
  }
  return result;
}

GSHSCENE_Result gshscene_list_group_pop(GSHSCENE_List * list) {
  return pop(list, PUSH_GROUP, GSHSCENE_ITEM_GROUP_POP);
}

GSHSCENE_Result gshscene_list_instance(
    GSHSCENE_List * list, const GSHSCENE_Resource * picture, const GSHGEOM_Map * map, int32_t layer, double depth) {
  if (!list || list->closed) {
    return GSHSCENE_ERR_INVALID;
  }
  GSHSCENE_Item item;
  item_init(&item, GSHSCENE_ITEM_INSTANCE);
  GSHSCENE_Result result = check_resource(list, picture, TAKES_PICTURE);
  if (result == GSHSCENE_OK) {
    result = canonical_map(map, &item.map);
  }
  if (result == GSHSCENE_OK) {
    result = gshscene_list_canonical_depth(depth, &item.depth);
  }
  if (result != GSHSCENE_OK) {
    return result;
  }
  item.resource = picture;
  item.layer = layer;
  return list_append(list, &item);
}

/* --- truncate (fixed.h) ------------------------------------------------------------ */

GSHSCENE_Result gshscene_list_truncate(GSHSCENE_List * list, size_t count) {
  if (!list || list->closed || count > list->count) {
    return GSHSCENE_ERR_INVALID;
  }
  for (size_t i = count; i < list->count; i++) {
    item_release(item_at(list, i));
  }
  list->count = count;
  /* What recording the first @p count items made, counted again: the open pushes, the expansion, the deepest
   * picture, the non-flat groups.  The stack was at least this deep when they were recorded, so it has room. */
  list->nonflat = 0;
  list->picture_depth = 0;
  list->expansion = 0;
  list->depth = 0;
  for (size_t i = 0; i < count; i++) {
    const GSHSCENE_Item * item = item_at(list, i);
    note_item(list, item);
    int instance = item->kind == GSHSCENE_ITEM_INSTANCE;
    size_t picture = item_picture_depth(item);
    list->picture_depth = picture > list->picture_depth ? picture : list->picture_depth;
    list->expansion = saturating_add(list->expansion,
        instance ? saturating_add(1u, gshscene_list_expansion(gshscene_resource_picture(item->resource))) : 1u);
    if (item->kind == GSHSCENE_ITEM_CLIP_PUSH || item->kind == GSHSCENE_ITEM_GROUP_PUSH) {
      list->stack[list->depth++] = item->kind == GSHSCENE_ITEM_CLIP_PUSH ? PUSH_CLIP : PUSH_GROUP;
    } else if (item->kind == GSHSCENE_ITEM_CLIP_POP || item->kind == GSHSCENE_ITEM_GROUP_POP) {
      list->depth--;
    }
  }
  return GSHSCENE_OK;
}

/* --- the foreground ------------------------------------------------------------ */

GSHSCENE_Result gshscene_list_set_foreground(GSHSCENE_List * list, const GSHSCENE_Color * color) {
  if (!list || list->closed) {
    return GSHSCENE_ERR_INVALID;
  }
  GSHSCENE_Color value;
  gshscene_color_init(&value);
  if (color) {
    GSHSCENE_Result result = gshscene_color_check(color);
    if (result != GSHSCENE_OK) {
      return result;
    }
    value = *color;
  }
  list->foreground = value;
  return GSHSCENE_OK;
}

GSHSCENE_Result gshscene_list_foreground(const GSHSCENE_List * list, GSHSCENE_Color * out) {
  if (!list || !out) {
    return GSHSCENE_ERR_INVALID;
  }
  *out = list->foreground;
  return GSHSCENE_OK;
}

/* --- the palette (N14) ------------------------------------------------------------ */

GSHSCENE_Result gshscene_list_set_palette(GSHSCENE_List * list, const GSHSCENE_Resource * palette) {
  if (!list || list->closed ||
      (palette && (palette->store != list->store || palette->kind != GSHSCENE_RESOURCE_PALETTE))) {
    return GSHSCENE_ERR_INVALID;
  }
  gshscene_resource_retain(palette);
  gshscene_resource_release(list->palette);
  list->palette = palette;
  return GSHSCENE_OK;
}

const GSHSCENE_Resource * gshscene_list_palette(const GSHSCENE_List * list) {
  return list ? list->palette : NULL;
}

GSHSCENE_Result gshscene_paint_resolve(const GSHSCENE_Paint * paint, const GSHSCENE_Color * foreground,
    const GSHSCENE_Resource * palette, GSHSCENE_Color * out) {
  if (!paint || !foreground || !out || (palette && palette->kind != GSHSCENE_RESOURCE_PALETTE)) {
    return GSHSCENE_ERR_INVALID;
  }
  GSHSCENE_Color c;
  switch (paint->kind) {
    case GSHSCENE_PAINT_SOLID:
      memcpy(c.rgba, paint->rgba, sizeof c.rgba);
      c.space = paint->space;
      break;
    case GSHSCENE_PAINT_FOREGROUND:
      c = *foreground;
      c.rgba[3] = foreground->rgba[3] * paint->rgba[3];
      break;
    case GSHSCENE_PAINT_PALETTE:
      /* N14: a missing entry (no palette, or past its count) is refused, never drawn in another color. */
      if (!palette || paint->entry >= palette->u.palette.count) {
        return GSHSCENE_ERR_REFUSED;
      }
      c = palette->u.palette.colors[paint->entry];
      c.rgba[3] = c.rgba[3] * paint->rgba[3];
      break;
    default:
      return GSHSCENE_ERR_INVALID;
  }
  *out = c;
  return GSHSCENE_OK;
}

/* --- composite operators ---------------------------------------------------------- */

const char * gshscene_composite_string(GSHSCENE_Composite op) {
  switch (op) {
    case GSHSCENE_COMPOSITE_SRC_OVER:
      return "src-over";
    case GSHSCENE_COMPOSITE_CLEAR:
      return "clear";
    case GSHSCENE_COMPOSITE_SRC:
      return "src";
    case GSHSCENE_COMPOSITE_DEST:
      return "dest";
    case GSHSCENE_COMPOSITE_DEST_OVER:
      return "dest-over";
    case GSHSCENE_COMPOSITE_SRC_IN:
      return "src-in";
    case GSHSCENE_COMPOSITE_DEST_IN:
      return "dest-in";
    case GSHSCENE_COMPOSITE_SRC_OUT:
      return "src-out";
    case GSHSCENE_COMPOSITE_DEST_OUT:
      return "dest-out";
    case GSHSCENE_COMPOSITE_SRC_ATOP:
      return "src-atop";
    case GSHSCENE_COMPOSITE_DEST_ATOP:
      return "dest-atop";
    case GSHSCENE_COMPOSITE_XOR:
      return "xor";
    case GSHSCENE_COMPOSITE_PLUS:
      return "plus";
    case GSHSCENE_COMPOSITE_SCREEN:
      return "screen";
    case GSHSCENE_COMPOSITE_OVERLAY:
      return "overlay";
    case GSHSCENE_COMPOSITE_DARKEN:
      return "darken";
    case GSHSCENE_COMPOSITE_LIGHTEN:
      return "lighten";
    case GSHSCENE_COMPOSITE_COLOR_DODGE:
      return "color-dodge";
    case GSHSCENE_COMPOSITE_COLOR_BURN:
      return "color-burn";
    case GSHSCENE_COMPOSITE_HARD_LIGHT:
      return "hard-light";
    case GSHSCENE_COMPOSITE_SOFT_LIGHT:
      return "soft-light";
    case GSHSCENE_COMPOSITE_DIFFERENCE:
      return "difference";
    case GSHSCENE_COMPOSITE_EXCLUSION:
      return "exclusion";
    case GSHSCENE_COMPOSITE_MULTIPLY:
      return "multiply";
    case GSHSCENE_COMPOSITE_HUE:
      return "hue";
    case GSHSCENE_COMPOSITE_SATURATION:
      return "saturation";
    case GSHSCENE_COMPOSITE_COLOR:
      return "color";
    case GSHSCENE_COMPOSITE_LUMINOSITY:
      return "luminosity";
    case GSHSCENE_COMPOSITE_COUNT:
      break;
  }
  return "unknown composite";
}

int gshscene_composite_is_unbounded(GSHSCENE_Composite op) {
  switch (op) {
    case GSHSCENE_COMPOSITE_CLEAR:
    case GSHSCENE_COMPOSITE_SRC:
    case GSHSCENE_COMPOSITE_SRC_IN:
    case GSHSCENE_COMPOSITE_DEST_IN:
    case GSHSCENE_COMPOSITE_SRC_OUT:
    case GSHSCENE_COMPOSITE_DEST_ATOP:
      return 1;
    default:
      return 0;
  }
}

/* --- order (N11) -------------------------------------------------------------------- */

int gshscene_order_compare(uint32_t context, int32_t layer_a, double depth_a, int32_t layer_b, double depth_b,
    int * tied) {
  *tied = 0;
  if (context == (uint32_t)GSHSCENE_ORDER_DEPTH) {
    if (depth_a > depth_b) {
      return -1; /* the farther unit is painted first */
    }
    if (depth_a < depth_b) {
      return 1;
    }
    *tied = 1;
  } else if (context != (uint32_t)GSHSCENE_ORDER_OVERLAY) {
    return 0;
  }
  if (layer_a != layer_b) {
    return layer_a < layer_b ? -1 : 1;
  }
  return 0;
}

/* A unit of a sequence: the first item and, for a push, its pop (else the same index). */
typedef struct Unit {
  size_t start;
  size_t end;
} Unit;

/* A sequence being put in order: its units, the next to emit, and what to emit when it is done. */
typedef struct Frame {
  Unit * units;
  size_t base;
  size_t count;
  size_t next;
  size_t prev_pos; /* where the previous unit's first item went */
  size_t closing;  /* the pop to emit when the sequence is finished, or SIZE_MAX */
  uint32_t context;
} Frame;

/* The scratch memory of a resolution: every piece is taken before anything changes. */
typedef struct Scratch {
  size_t * match;
  size_t * out;
  size_t * open;
  Unit * units;
  Unit * temp;
  Frame * frames;
  GSHSCENE_Tie * ties;
} Scratch;

static void scratch_free(const GSHSCENE_Allocator * alloc, Scratch * s) {
  gshscene_free(alloc, s->match);
  gshscene_free(alloc, s->out);
  gshscene_free(alloc, s->open);
  gshscene_free(alloc, s->units);
  gshscene_free(alloc, s->temp);
  gshscene_free(alloc, s->frames);
  gshscene_free(alloc, s->ties);
}

static void * array_alloc(const GSHSCENE_Allocator * alloc, size_t count, size_t size) {
  if (count == 0) {
    count = 1;
  }
  if (count > SIZE_MAX / size) {
    return NULL;
  }
  return gshscene_alloc(alloc, count * size);
}

/* Whether unit @p a is painted strictly before unit @p b in @p context (the sort's predicate). */
static int unit_before(const GSHSCENE_List * list, uint32_t context, const Unit * a, const Unit * b, int * tied) {
  const GSHSCENE_Item * ia = item_at(list, a->start);
  const GSHSCENE_Item * ib = item_at(list, b->start);
  return gshscene_order_compare(context, ia->layer, ia->depth, ib->layer, ib->depth, tied);
}

/* A stable merge sort of @p units (@p temp is as long): equal units keep their declared order. */
static void units_sort(const GSHSCENE_List * list, uint32_t context, Unit * units, Unit * temp, size_t count) {
  for (size_t width = 1; width < count; width *= 2) {
    for (size_t lo = 0; lo < count; lo += 2 * width) {
      size_t mid = lo + width < count ? lo + width : count;
      size_t hi = lo + 2 * width < count ? lo + 2 * width : count;
      size_t i = lo;
      size_t j = mid;
      size_t k = lo;
      while (i < mid && j < hi) {
        int tied;
        /* The right unit goes first only if it is strictly before the left one. */
        if (unit_before(list, context, &units[j], &units[i], &tied) < 0) {
          temp[k++] = units[j++];
        } else {
          temp[k++] = units[i++];
        }
      }
      while (i < mid) {
        temp[k++] = units[i++];
      }
      while (j < hi) {
        temp[k++] = units[j++];
      }
    }
    memcpy(units, temp, count * sizeof *units);
  }
}

/* Put a new sequence of items [lo, hi) on the frame stack, its units sorted. */
static void frame_open(const GSHSCENE_List * list, Scratch * s, size_t * frame_count, size_t * unit_top, size_t lo,
    size_t hi, uint32_t context, size_t closing) {
  Frame * f = &s->frames[(*frame_count)++];
  f->base = *unit_top;
  f->units = &s->units[*unit_top];
  f->count = 0;
  f->next = 0;
  f->prev_pos = 0;
  f->closing = closing;
  f->context = context;
  size_t i = lo;
  while (i < hi) {
    const GSHSCENE_Item * item = item_at(list, i);
    Unit u = {i, i};
    if (item->kind == GSHSCENE_ITEM_CLIP_PUSH || item->kind == GSHSCENE_ITEM_GROUP_PUSH) {
      u.end = s->match[i];
    }
    f->units[f->count++] = u;
    i = u.end + 1;
  }
  *unit_top += f->count;
  if (context != (uint32_t)GSHSCENE_ORDER_FLAT && f->count > 1) {
    units_sort(list, context, f->units, s->temp, f->count);
  }
}

/* Make the order of a closed-to-be list: the permutation of its items and its ties, in @p s. */
static void resolve_order(const GSHSCENE_List * list, Scratch * s, size_t * out_ties) {
  size_t n = list->count;
  /* The matching pop of every push. */
  size_t open_n = 0;
  for (size_t i = 0; i < n; i++) {
    const GSHSCENE_Item * item = item_at(list, i);
    if (item->kind == GSHSCENE_ITEM_CLIP_PUSH || item->kind == GSHSCENE_ITEM_GROUP_PUSH) {
      s->open[open_n++] = i;
    } else if (item->kind == GSHSCENE_ITEM_CLIP_POP || item->kind == GSHSCENE_ITEM_GROUP_POP) {
      s->match[s->open[--open_n]] = i;
    }
  }
  size_t out_n = 0;
  size_t unit_top = 0;
  size_t frame_count = 0;
  size_t tie_n = 0;
  frame_open(list, s, &frame_count, &unit_top, 0, n, GSHSCENE_ORDER_FLAT, SIZE_MAX);
  while (frame_count > 0) {
    Frame * f = &s->frames[frame_count - 1];
    if (f->next == f->count) {
      if (f->closing != SIZE_MAX) {
        s->out[out_n++] = f->closing;
      }
      unit_top = f->base;
      frame_count--;
      continue;
    }
    Unit u = f->units[f->next++];
    size_t pos = out_n;
    const GSHSCENE_Item * item = item_at(list, u.start);
    if (f->context == (uint32_t)GSHSCENE_ORDER_DEPTH && f->next > 1) {
      const Unit * prev = &f->units[f->next - 2];
      const GSHSCENE_Item * before = item_at(list, prev->start);
      if (before->depth == item->depth) {
        GSHSCENE_Tie * tie = &s->ties[tie_n++];
        tie->first = f->prev_pos;
        tie->second = pos;
        tie->depth = item->depth;
        tie->reason = before->layer != item->layer ? GSHSCENE_TIE_LAYER : GSHSCENE_TIE_DECLARED;
      }
    }
    f->prev_pos = pos;
    s->out[out_n++] = u.start;
    if (u.end != u.start) {
      uint32_t inner = item->kind == GSHSCENE_ITEM_GROUP_PUSH ? item->group.ordering : f->context;
      frame_open(list, s, &frame_count, &unit_top, u.start + 1, u.end, inner, u.end);
    }
  }
  *out_ties = tie_n;
}

/* Rewrite the items so that item i is what was item @p out[i] (cycle by cycle; @p out is consumed). */
static void items_permute(GSHSCENE_List * list, size_t * out) {
  size_t n = list->count;
  for (size_t i = 0; i < n; i++) {
    if (out[i] == i) {
      continue;
    }
    GSHSCENE_Item hold = *item_at(list, i);
    size_t j = i;
    while (out[j] != i) {
      size_t k = out[j];
      *item_at(list, j) = *item_at(list, k);
      out[j] = j;
      j = k;
    }
    *item_at(list, j) = hold;
    out[j] = j;
  }
}

static GSHSCENE_Result list_resolve(GSHSCENE_List * list) {
  size_t n = list->count;
  if (list->nonflat == 0 || n == 0) {
    return GSHSCENE_OK;
  }
  const GSHSCENE_Allocator * alloc = &list->alloc;
  Scratch s;
  memset(&s, 0, sizeof s);
  s.match = array_alloc(alloc, n, sizeof *s.match);
  s.out = array_alloc(alloc, n, sizeof *s.out);
  s.open = array_alloc(alloc, n, sizeof *s.open);
  s.units = array_alloc(alloc, n, sizeof *s.units);
  s.temp = array_alloc(alloc, n, sizeof *s.temp);
  s.frames = array_alloc(alloc, n + 1, sizeof *s.frames);
  s.ties = array_alloc(alloc, n, sizeof *s.ties);
  if (!s.match || !s.out || !s.open || !s.units || !s.temp || !s.frames || !s.ties) {
    scratch_free(alloc, &s);
    return GSHSCENE_ERR_OOM;
  }
  size_t tie_n = 0;
  resolve_order(list, &s, &tie_n);
  GSHSCENE_Tie * ties = NULL;
  if (tie_n > 0) {
    ties = array_alloc(alloc, tie_n, sizeof *ties);
    if (!ties) {
      scratch_free(alloc, &s);
      return GSHSCENE_ERR_OOM;
    }
    memcpy(ties, s.ties, tie_n * sizeof *ties);
  }
  items_permute(list, s.out);
  scratch_free(alloc, &s);
  gshscene_free(alloc, list->ties);
  list->ties = ties;
  list->tie_count = tie_n;
  list->tie_capacity = tie_n;
  return GSHSCENE_OK;
}

GSHSCENE_Result gshscene_list_close(GSHSCENE_List * list) {
  if (!list || list->closed || list->depth != 0) {
    return GSHSCENE_ERR_INVALID;
  }
  GSHSCENE_Result result = list_resolve(list);
  if (result != GSHSCENE_OK) {
    return result;
  }
  list->closed = 1;
  return GSHSCENE_OK;
}

static int tie_compare(const void * pa, const void * pb) {
  const GSHSCENE_Tie * a = pa;
  const GSHSCENE_Tie * b = pb;
  return a->second < b->second ? -1 : (a->second > b->second ? 1 : 0);
}

GSHSCENE_Result gshscene_list_close_ordered(GSHSCENE_List * list) {
  if (!list || list->closed || list->depth != 0) {
    return GSHSCENE_ERR_INVALID;
  }
  /* The emitter finds a tie after the units inside the later one: ascending by second item, as close makes them. */
  if (list->tie_count > 1) {
    qsort(list->ties, list->tie_count, sizeof *list->ties, tie_compare);
  }
  list->closed = 1;
  return GSHSCENE_OK;
}

GSHSCENE_Result gshscene_list_add_tie(
    GSHSCENE_List * list, size_t first, size_t second, double depth, GSHSCENE_Tie_Reason reason) {
  if (list->tie_count == list->tie_capacity) {
    size_t capacity = list->tie_capacity ? list->tie_capacity * 2 : 8;
    GSHSCENE_Tie * grown = array_alloc(&list->alloc, capacity, sizeof *grown);
    if (!grown) {
      return GSHSCENE_ERR_OOM;
    }
    if (list->tie_count) {
      memcpy(grown, list->ties, list->tie_count * sizeof *grown);
    }
    gshscene_free(&list->alloc, list->ties);
    list->ties = grown;
    list->tie_capacity = capacity;
  }
  GSHSCENE_Tie * tie = &list->ties[list->tie_count++];
  tie->first = first;
  tie->second = second;
  tie->depth = depth;
  tie->reason = reason;
  return GSHSCENE_OK;
}

size_t gshscene_list_tie_count(const GSHSCENE_List * list) {
  return list && list->closed ? list->tie_count : 0;
}

GSHSCENE_Result gshscene_list_tie(const GSHSCENE_List * list, size_t index, const GSHSCENE_Tie ** out) {
  if (!list || !out || !list->closed || index >= list->tie_count) {
    return GSHSCENE_ERR_INVALID;
  }
  *out = &list->ties[index];
  return GSHSCENE_OK;
}

/* --- reading ------------------------------------------------------------------- */

int gshscene_list_is_closed(const GSHSCENE_List * list) {
  return list ? list->closed : 0;
}

size_t gshscene_list_count(const GSHSCENE_List * list) {
  return list ? list->count : 0;
}

GSHSCENE_Result gshscene_list_item(const GSHSCENE_List * list, size_t index, const GSHSCENE_Item ** out) {
  if (!list || !out || index >= list->count) {
    return GSHSCENE_ERR_INVALID;
  }
  *out = item_at(list, index);
  return GSHSCENE_OK;
}

size_t gshscene_list_expansion(const GSHSCENE_List * list) {
  return list ? list->expansion : 0;
}

size_t gshscene_list_bytes(const GSHSCENE_List * list) {
  return list ? list->bytes : 0;
}

size_t gshscene_list_picture_depth(const GSHSCENE_List * list) {
  return list->picture_depth;
}

/* --- copy ----------------------------------------------------------------------- */

GSHSCENE_Result gshscene_list_copy(
    const GSHSCENE_List * source, const GSHSCENE_Allocator * alloc, GSHSCENE_List ** out) {
  GSHSCENE_List * copy = NULL;
  GSHSCENE_Result result = gshscene_list_create_effective(alloc, &source->limits, source->store, &copy);
  for (size_t i = 0; result == GSHSCENE_OK && i < source->count; i++) {
    result = list_append(copy, item_at(source, i));
  }
  for (size_t i = 0; result == GSHSCENE_OK && i < source->tie_count; i++) {
    const GSHSCENE_Tie * t = &source->ties[i];
    result = gshscene_list_add_tie(copy, t->first, t->second, t->depth, t->reason);
  }
  if (result != GSHSCENE_OK) {
    gshscene_list_destroy(copy);
    return result;
  }
  copy->closed = 1;
  *out = copy;
  return GSHSCENE_OK;
}

/* --- equality ------------------------------------------------------------------- */

static int map_equal(const GSHGEOM_Map * a, const GSHGEOM_Map * b) {
  if (a->dim != b->dim || a->kind != b->kind) {
    return 0;
  }
  for (int i = 0; i < a->dim; i++) {
    for (int j = 0; j < a->dim; j++) {
      if (gshgeom_f64_bits(a->m[i][j]) != gshgeom_f64_bits(b->m[i][j])) {
        return 0;
      }
    }
    if (gshgeom_f64_bits(a->t[i]) != gshgeom_f64_bits(b->t[i])) {
      return 0;
    }
  }
  return 1;
}

int gshscene_space_equal(const GSHSCENE_Color_Space * a, const GSHSCENE_Color_Space * b) {
  return a->primaries == b->primaries && a->transfer == b->transfer &&
         gshgeom_f64_bits(a->gamma) == gshgeom_f64_bits(b->gamma);
}

int gshscene_color_equal(const GSHSCENE_Color * a, const GSHSCENE_Color * b) {
  for (int i = 0; i < 4; i++) {
    if (gshgeom_f64_bits(a->rgba[i]) != gshgeom_f64_bits(b->rgba[i])) {
      return 0;
    }
  }
  return gshscene_space_equal(&a->space, &b->space);
}

static int item_equal(const GSHSCENE_Item * a, const GSHSCENE_Item * b) {
  if (a->kind != b->kind || a->resource != b->resource || a->style != b->style || !map_equal(&a->map, &b->map) ||
      a->fill_rule != b->fill_rule || a->layer != b->layer ||
      gshgeom_f64_bits(a->depth) != gshgeom_f64_bits(b->depth)) {
    return 0;
  }
  if (a->paint.kind != b->paint.kind || !gshscene_space_equal(&a->paint.space, &b->paint.space) ||
      a->paint.gradient != b->paint.gradient || a->paint.entry != b->paint.entry) {
    return 0;
  }
  for (int i = 0; i < 4; i++) {
    if (gshgeom_f64_bits(a->paint.rgba[i]) != gshgeom_f64_bits(b->paint.rgba[i])) {
      return 0;
    }
  }
  return gshgeom_f64_bits(a->group.opacity) == gshgeom_f64_bits(b->group.opacity) &&
         gshscene_space_equal(&a->group.blend_space, &b->group.blend_space) &&
         a->group.composite == b->group.composite && a->group.ordering == b->group.ordering &&
         a->group.isolation == b->group.isolation && a->group.knockout == b->group.knockout &&
         a->group.soft_mask == b->group.soft_mask;
}

int gshscene_list_equal(const GSHSCENE_List * a, const GSHSCENE_List * b) {
  if (!a || !b || a->count != b->count || !gshscene_color_equal(&a->foreground, &b->foreground) || a->palette != b->palette) {
    return 0;
  }
  for (size_t i = 0; i < a->count; i++) {
    if (!item_equal(item_at(a, i), item_at(b, i))) {
      return 0;
    }
  }
  return 1;
}

/* --- hash ----------------------------------------------------------------------- */

static void hash_resource_and_map(GSHGEOM_Hash * h, const GSHSCENE_Item * item) {
  gshgeom_hash_u64(h, gshscene_resource_hash(item->resource));
  gshgeom_hash_u64(h, gshgeom_map_hash(&item->map));
}

void gshscene_space_hash(GSHGEOM_Hash * h, const GSHSCENE_Color_Space * space) {
  gshgeom_hash_u8(h, (uint8_t)space->primaries);
  gshgeom_hash_u8(h, (uint8_t)space->transfer);
  gshgeom_hash_f64(h, space->gamma);
}

void gshscene_color_hash(GSHGEOM_Hash * h, const GSHSCENE_Color * color) {
  for (int k = 0; k < 4; k++) {
    gshgeom_hash_f64(h, color->rgba[k]);
  }
  gshscene_space_hash(h, &color->space);
}

void gshscene_paint_hash(GSHGEOM_Hash * h, const GSHSCENE_Paint * paint) {
  gshgeom_hash_u8(h, (uint8_t)paint->kind);
  for (int k = 0; k < 4; k++) {
    gshgeom_hash_f64(h, paint->rgba[k]);
  }
  gshscene_space_hash(h, &paint->space);
  gshgeom_hash_u32(h, paint->entry);
  gshgeom_hash_u64(h, paint->gradient ? gshscene_resource_hash(paint->gradient) : 0);
}

uint64_t gshscene_list_hash(const GSHSCENE_List * list) {
  GSHGEOM_Hash h;
  gshgeom_hash_init(&h);
  if (!list) {
    return gshgeom_hash_value(&h);
  }
  gshgeom_hash_u32(&h, LIST_FORMAT_VERSION);
  gshgeom_hash_u32(&h, LIST_HASH_KIND);
  gshscene_color_hash(&h, &list->foreground);
  gshgeom_hash_u64(&h, list->palette ? gshscene_resource_hash(list->palette) : 0);
  gshgeom_hash_u64(&h, (uint64_t)list->count);
  for (size_t i = 0; i < list->count; i++) {
    const GSHSCENE_Item * item = item_at(list, i);
    gshgeom_hash_u8(&h, (uint8_t)item->kind);
    switch (item->kind) {
      case GSHSCENE_ITEM_FILL:
        hash_resource_and_map(&h, item);
        gshgeom_hash_u8(&h, (uint8_t)item->fill_rule);
        gshscene_paint_hash(&h, &item->paint);
        gshgeom_hash_i32(&h, item->layer);
        gshgeom_hash_f64(&h, item->depth);
        break;
      case GSHSCENE_ITEM_CLIP_PUSH:
        hash_resource_and_map(&h, item);
        gshgeom_hash_u8(&h, (uint8_t)item->fill_rule);
        gshgeom_hash_i32(&h, item->layer);
        gshgeom_hash_f64(&h, item->depth);
        break;
      case GSHSCENE_ITEM_GROUP_PUSH:
        gshgeom_hash_f64(&h, item->group.opacity);
        gshscene_space_hash(&h, &item->group.blend_space);
        gshscene_group_hash_composite(&h, item->group.composite, item->group.isolation, item->group.knockout,
            item->group.soft_mask);
        gshgeom_hash_u32(&h, item->group.ordering);
        gshgeom_hash_i32(&h, item->layer);
        gshgeom_hash_f64(&h, item->depth);
        break;
      case GSHSCENE_ITEM_INSTANCE:
        hash_resource_and_map(&h, item);
        gshgeom_hash_i32(&h, item->layer);
        gshgeom_hash_f64(&h, item->depth);
        break;
      case GSHSCENE_ITEM_STROKE: /* SK12 */
        gshgeom_hash_u64(&h, gshscene_resource_hash(item->resource));
        gshgeom_hash_u64(&h, gshscene_resource_hash(item->style));
        gshgeom_hash_u64(&h, gshgeom_map_hash(&item->map));
        gshscene_paint_hash(&h, &item->paint);
        gshgeom_hash_i32(&h, item->layer);
        gshgeom_hash_f64(&h, item->depth);
        break;
      default:
        break;
    }
  }
  return gshgeom_hash_value(&h);
}

/* --- dump ------------------------------------------------------------------------ */

#define HEX(x) ((unsigned long long)(x))

static int dump_resource_and_map(FILE * out, const GSHSCENE_Item * item) {
  return fprintf(out, " res=%s:%016llx map=%016llx",
      gshscene_resource_kind_string(gshscene_resource_kind(item->resource)),
      HEX(gshscene_resource_hash(item->resource)), HEX(gshgeom_map_hash(&item->map)));
}

/* A space as P:T:G, the gamma as its 16 hexadecimal digits. */
int gshscene_space_dump(FILE * out, const GSHSCENE_Color_Space * space) {
  return fprintf(out, "%u:%u:%016llx", (unsigned)space->primaries, (unsigned)space->transfer,
      HEX(gshgeom_f64_bits(space->gamma)));
}

/* A paint as a fill's dump line spells it: ` paint=N <r> <g> <b> <a> space=<space> entry=E gradient=<hash>`. */
static int dump_paint(FILE * out, const GSHSCENE_Paint * paint) {
  int written = fprintf(out, " paint=%u %016llx %016llx %016llx %016llx space=", (unsigned)paint->kind,
      HEX(gshgeom_f64_bits(paint->rgba[0])), HEX(gshgeom_f64_bits(paint->rgba[1])), HEX(gshgeom_f64_bits(paint->rgba[2])),
      HEX(gshgeom_f64_bits(paint->rgba[3])));
  if (written >= 0) {
    written = gshscene_space_dump(out, &paint->space);
  }
  if (written >= 0) {
    written = fprintf(out, " entry=%u gradient=%016llx", (unsigned)paint->entry,
        HEX(paint->gradient ? gshscene_resource_hash(paint->gradient) : 0));
  }
  return written;
}

static int dump_layer_depth(FILE * out, const GSHSCENE_Item * item) {
  return fprintf(out, " layer=%lld depth=%016llx", (long long)item->layer, HEX(gshgeom_f64_bits(item->depth)));
}

GSHSCENE_Result gshscene_list_dump(const GSHSCENE_List * list, FILE * out) {
  if (!list || !out) {
    return GSHSCENE_ERR_INVALID;
  }
  /* The counts are printed through an unsigned long long: the width of size_t is
   * a property of the host. */
  const GSHSCENE_Color * fg = &list->foreground;
  if (fprintf(out, "gshscene.list %u items=%llu ties=%llu fg=%016llx %016llx %016llx %016llx ", LIST_FORMAT_VERSION,
          HEX(list->count), HEX(list->closed ? list->tie_count : 0), HEX(gshgeom_f64_bits(fg->rgba[0])),
          HEX(gshgeom_f64_bits(fg->rgba[1])), HEX(gshgeom_f64_bits(fg->rgba[2])), HEX(gshgeom_f64_bits(fg->rgba[3]))) < 0 ||
      gshscene_space_dump(out, &fg->space) < 0 ||
      fprintf(out, " palette=%016llx hash=%016llx\n", HEX(list->palette ? gshscene_resource_hash(list->palette) : 0),
          HEX(gshscene_list_hash(list))) < 0) {
    return GSHSCENE_ERR_IO;
  }
  for (size_t i = 0; i < list->count; i++) {
    const GSHSCENE_Item * item = item_at(list, i);
    int written = 0;
    switch (item->kind) {
      case GSHSCENE_ITEM_FILL:
        written = fputc('F', out) == EOF ? -1 : dump_resource_and_map(out, item);
        if (written >= 0) {
          written = fprintf(out, " rule=%u", (unsigned)item->fill_rule);
        }
        if (written >= 0) {
          written = dump_paint(out, &item->paint);
        }
        if (written >= 0) {
          written = dump_layer_depth(out, item);
        }
        break;
      case GSHSCENE_ITEM_STROKE: /* SK12 */
        written = fprintf(out, "K res=%s:%016llx style=%016llx map=%016llx",
            gshscene_resource_kind_string(gshscene_resource_kind(item->resource)), HEX(gshscene_resource_hash(item->resource)),
            HEX(gshscene_resource_hash(item->style)), HEX(gshgeom_map_hash(&item->map)));
        if (written >= 0) {
          written = dump_paint(out, &item->paint);
        }
        if (written >= 0) {
          written = dump_layer_depth(out, item);
        }
        break;
      case GSHSCENE_ITEM_CLIP_PUSH:
        written = fputc('C', out) == EOF ? -1 : dump_resource_and_map(out, item);
        if (written >= 0) {
          written = fprintf(out, " rule=%u", (unsigned)item->fill_rule);
        }
        if (written >= 0) {
          written = dump_layer_depth(out, item);
        }
        break;
      case GSHSCENE_ITEM_CLIP_POP:
        written = fputc('c', out) == EOF ? -1 : 0;
        break;
      case GSHSCENE_ITEM_GROUP_PUSH:
        written = fprintf(out, "G opacity=%016llx blend=", HEX(gshgeom_f64_bits(item->group.opacity)));
        if (written >= 0) {
          written = gshscene_space_dump(out, &item->group.blend_space);
        }
        if (written >= 0) {
          written = fprintf(out, " composite=%u order=%u", (unsigned)item->group.composite, (unsigned)item->group.ordering);
        }
        if (written >= 0) {
          written = gshscene_group_dump_pdf(out, item->group.isolation, item->group.knockout, item->group.soft_mask);
        }
        if (written >= 0) {
          written = dump_layer_depth(out, item);
        }
        break;
      case GSHSCENE_ITEM_GROUP_POP:
        written = fputc('g', out) == EOF ? -1 : 0;
        break;
      case GSHSCENE_ITEM_INSTANCE:
        written = fputc('I', out) == EOF ? -1 : dump_resource_and_map(out, item);
        if (written >= 0) {
          written = dump_layer_depth(out, item);
        }
        break;
      default:
        break;
    }
    if (written < 0 || fputc('\n', out) == EOF) {
      return GSHSCENE_ERR_IO;
    }
  }
  if (list->closed) {
    for (size_t i = 0; i < list->tie_count; i++) {
      const GSHSCENE_Tie * tie = &list->ties[i];
      if (fprintf(out, "T first=%llu second=%llu depth=%016llx reason=%u\n", HEX(tie->first), HEX(tie->second),
              HEX(gshgeom_f64_bits(tie->depth)), (unsigned)tie->reason) < 0) {
        return GSHSCENE_ERR_IO;
      }
    }
  }
  /* A buffered stream reports a write error at the flush, and a stream's error flag is sticky:
   * a failed write whose return value was lost (an unbuffered stream's fprintf) still sets it. */
  return (fflush(out) == 0 && !ferror(out)) ? GSHSCENE_OK : GSHSCENE_ERR_IO;
}
