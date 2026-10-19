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
  chunks_free(list, list->head, NULL);
  chunks_free(list, list->big, NULL);
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

static void items_release(GSHSCENE_List * list) {
  for (size_t i = 0; i < list->count; i++) {
    gshscene_resource_release(item_at(list, i)->resource);
  }
}

/* A zeroed item of a kind: the identity map, the nonzero rule, no resource. */
static void item_init(GSHSCENE_Item * item, GSHSCENE_Item_Kind kind) {
  memset(item, 0, sizeof *item);
  item->kind = kind;
  gshgeom_map_identity(2, &item->map);
  item->fill_rule = GSHGEOM_FILL_NONZERO;
  item->paint.kind = GSHSCENE_PAINT_SOLID;
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
  list->count++;
  list->expansion = total;
  if (item->kind == GSHSCENE_ITEM_INSTANCE && item->resource->u.picture.depth > list->picture_depth) {
    list->picture_depth = item->resource->u.picture.depth;
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

static GSHSCENE_Result canonical_paint(const GSHSCENE_Paint * paint, GSHSCENE_Paint * out) {
  memset(out, 0, sizeof *out);
  out->kind = GSHSCENE_PAINT_SOLID;
  out->rgba[3] = 1.0;
  if (!paint) {
    return GSHSCENE_OK;
  }
  if (paint->kind == GSHSCENE_PAINT_GRADIENT || paint->kind == GSHSCENE_PAINT_FOREGROUND ||
      paint->kind == GSHSCENE_PAINT_PALETTE) {
    return GSHSCENE_ERR_UNSUPPORTED;
  }
  if (paint->kind != GSHSCENE_PAINT_SOLID || paint->gradient) {
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
  if (paint->color_space != 0) {
    return GSHSCENE_ERR_UNSUPPORTED;
  }
  for (int i = 0; i < 4; i++) {
    out->rgba[i] = paint->rgba[i];
  }
  return GSHSCENE_OK;
}

static GSHSCENE_Result canonical_group(const GSHSCENE_Group * group, GSHSCENE_Group * out) {
  out->opacity = 1.0;
  out->blend_space = 0;
  out->ordering = GSHSCENE_ORDER_FLAT;
  if (!group) {
    return GSHSCENE_OK;
  }
  if (!gshscene_finite(group->opacity) || !(group->opacity >= 0.0 && group->opacity <= 1.0)) {
    return GSHSCENE_ERR_INVALID;
  }
  if (group->ordering >= (uint32_t)GSHSCENE_ORDERING_COUNT) {
    return GSHSCENE_ERR_INVALID;
  }
  if (group->blend_space != 0 || group->ordering != GSHSCENE_ORDER_FLAT) {
    return GSHSCENE_ERR_UNSUPPORTED;
  }
  out->opacity = group->opacity;
  return GSHSCENE_OK;
}

/* The same checks for a retained scene's nodes (src/scene), which share the rules of a fill. */
GSHSCENE_Result gshscene_list_canonical_map(const GSHGEOM_Map * map, GSHGEOM_Map * out) {
  return canonical_map(map, out);
}

GSHSCENE_Result gshscene_list_canonical_paint(const GSHSCENE_Paint * paint, GSHSCENE_Paint * out) {
  return canonical_paint(paint, out);
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
    const GSHGEOM_Map * map, GSHGEOM_Fill_Rule rule, const GSHSCENE_Paint * paint, int32_t layer) {
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
    result = canonical_paint(paint, &item.paint);
  }
  if (result != GSHSCENE_OK) {
    return result;
  }
  item.resource = resource;
  item.fill_rule = rule;
  item.layer = layer;
  return list_append(list, &item);
}

GSHSCENE_Result gshscene_list_clip_push(GSHSCENE_List * list, const GSHSCENE_Resource * resource,
    const GSHGEOM_Map * map, GSHGEOM_Fill_Rule rule) {
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
    result = stack_reserve(list);
  }
  if (result != GSHSCENE_OK) {
    return result;
  }
  item.resource = resource;
  item.fill_rule = rule;
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

GSHSCENE_Result gshscene_list_group_push(GSHSCENE_List * list, const GSHSCENE_Group * group, int32_t layer) {
  if (!list || list->closed) {
    return GSHSCENE_ERR_INVALID;
  }
  GSHSCENE_Item item;
  item_init(&item, GSHSCENE_ITEM_GROUP_PUSH);
  GSHSCENE_Result result = canonical_group(group, &item.group);
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
    GSHSCENE_List * list, const GSHSCENE_Resource * picture, const GSHGEOM_Map * map, int32_t layer) {
  if (!list || list->closed) {
    return GSHSCENE_ERR_INVALID;
  }
  GSHSCENE_Item item;
  item_init(&item, GSHSCENE_ITEM_INSTANCE);
  GSHSCENE_Result result = check_resource(list, picture, TAKES_PICTURE);
  if (result == GSHSCENE_OK) {
    result = canonical_map(map, &item.map);
  }
  if (result != GSHSCENE_OK) {
    return result;
  }
  item.resource = picture;
  item.layer = layer;
  return list_append(list, &item);
}

GSHSCENE_Result gshscene_list_close(GSHSCENE_List * list) {
  if (!list || list->closed || list->depth != 0) {
    return GSHSCENE_ERR_INVALID;
  }
  list->closed = 1;
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

static int item_equal(const GSHSCENE_Item * a, const GSHSCENE_Item * b) {
  if (a->kind != b->kind || a->resource != b->resource || !map_equal(&a->map, &b->map) ||
      a->fill_rule != b->fill_rule || a->layer != b->layer) {
    return 0;
  }
  if (a->paint.kind != b->paint.kind || a->paint.color_space != b->paint.color_space ||
      a->paint.gradient != b->paint.gradient) {
    return 0;
  }
  for (int i = 0; i < 4; i++) {
    if (gshgeom_f64_bits(a->paint.rgba[i]) != gshgeom_f64_bits(b->paint.rgba[i])) {
      return 0;
    }
  }
  return gshgeom_f64_bits(a->group.opacity) == gshgeom_f64_bits(b->group.opacity) &&
         a->group.blend_space == b->group.blend_space && a->group.ordering == b->group.ordering;
}

int gshscene_list_equal(const GSHSCENE_List * a, const GSHSCENE_List * b) {
  if (!a || !b || a->count != b->count) {
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

uint64_t gshscene_list_hash(const GSHSCENE_List * list) {
  GSHGEOM_Hash h;
  gshgeom_hash_init(&h);
  if (!list) {
    return gshgeom_hash_value(&h);
  }
  gshgeom_hash_u32(&h, LIST_FORMAT_VERSION);
  gshgeom_hash_u32(&h, LIST_HASH_KIND);
  gshgeom_hash_u64(&h, (uint64_t)list->count);
  for (size_t i = 0; i < list->count; i++) {
    const GSHSCENE_Item * item = item_at(list, i);
    gshgeom_hash_u8(&h, (uint8_t)item->kind);
    switch (item->kind) {
      case GSHSCENE_ITEM_FILL:
        hash_resource_and_map(&h, item);
        gshgeom_hash_u8(&h, (uint8_t)item->fill_rule);
        gshgeom_hash_u8(&h, (uint8_t)item->paint.kind);
        for (int k = 0; k < 4; k++) {
          gshgeom_hash_f64(&h, item->paint.rgba[k]);
        }
        gshgeom_hash_u32(&h, item->paint.color_space);
        gshgeom_hash_i32(&h, item->layer);
        break;
      case GSHSCENE_ITEM_CLIP_PUSH:
        hash_resource_and_map(&h, item);
        gshgeom_hash_u8(&h, (uint8_t)item->fill_rule);
        break;
      case GSHSCENE_ITEM_GROUP_PUSH:
        gshgeom_hash_f64(&h, item->group.opacity);
        gshgeom_hash_u32(&h, item->group.blend_space);
        gshgeom_hash_u32(&h, item->group.ordering);
        gshgeom_hash_i32(&h, item->layer);
        break;
      case GSHSCENE_ITEM_INSTANCE:
        hash_resource_and_map(&h, item);
        gshgeom_hash_i32(&h, item->layer);
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

GSHSCENE_Result gshscene_list_dump(const GSHSCENE_List * list, FILE * out) {
  if (!list || !out) {
    return GSHSCENE_ERR_INVALID;
  }
  /* The count is printed through an unsigned long long: the width of size_t is
   * a property of the host. */
  if (fprintf(out, "gshscene.list %u items=%llu hash=%016llx\n", LIST_FORMAT_VERSION,
          HEX(list->count), HEX(gshscene_list_hash(list))) < 0) {
    return GSHSCENE_ERR_IO;
  }
  for (size_t i = 0; i < list->count; i++) {
    const GSHSCENE_Item * item = item_at(list, i);
    int written = 0;
    switch (item->kind) {
      case GSHSCENE_ITEM_FILL:
        written = fputc('F', out) == EOF ? -1 : dump_resource_and_map(out, item);
        if (written >= 0) {
          written = fprintf(out, " rule=%u paint=%u %016llx %016llx %016llx %016llx space=%u layer=%lld",
              (unsigned)item->fill_rule, (unsigned)item->paint.kind, HEX(gshgeom_f64_bits(item->paint.rgba[0])),
              HEX(gshgeom_f64_bits(item->paint.rgba[1])), HEX(gshgeom_f64_bits(item->paint.rgba[2])),
              HEX(gshgeom_f64_bits(item->paint.rgba[3])), (unsigned)item->paint.color_space,
              (long long)item->layer);
        }
        break;
      case GSHSCENE_ITEM_CLIP_PUSH:
        written = fputc('C', out) == EOF ? -1 : dump_resource_and_map(out, item);
        if (written >= 0) {
          written = fprintf(out, " rule=%u", (unsigned)item->fill_rule);
        }
        break;
      case GSHSCENE_ITEM_CLIP_POP:
        written = fputc('c', out) == EOF ? -1 : 0;
        break;
      case GSHSCENE_ITEM_GROUP_PUSH:
        written = fprintf(out, "G opacity=%016llx blend=%u order=%u layer=%lld", HEX(gshgeom_f64_bits(item->group.opacity)),
            (unsigned)item->group.blend_space, (unsigned)item->group.ordering, (long long)item->layer);
        break;
      case GSHSCENE_ITEM_GROUP_POP:
        written = fputc('g', out) == EOF ? -1 : 0;
        break;
      case GSHSCENE_ITEM_INSTANCE:
        written = fputc('I', out) == EOF ? -1 : dump_resource_and_map(out, item);
        if (written >= 0) {
          written = fprintf(out, " layer=%lld", (long long)item->layer);
        }
        break;
      default:
        break;
    }
    if (written < 0 || fputc('\n', out) == EOF) {
      return GSHSCENE_ERR_IO;
    }
  }
  /* A buffered stream reports a write error at the flush, and a stream's error flag is sticky:
   * a failed write whose return value was lost (an unbuffered stream's fprintf) still sets it. */
  return (fflush(out) == 0 && !ferror(out)) ? GSHSCENE_OK : GSHSCENE_ERR_IO;
}
