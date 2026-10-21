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
 * Nodes: the canonical form of a node's properties and its validation, copy,
 * equality and hash; the keys; and the index the edits keep over their changes.
 */

#include <ghoti.io/shape-scene/macros.h>

#include "internal.h"

#include <stdlib.h>
#include <string.h>

/* --- the index --------------------------------------------------------------------- */

#define INDEX_MIN_CAP 16u

uint64_t gshscene_mix64(uint64_t x) {
  x ^= x >> 30;
  x *= UINT64_C(0xbf58476d1ce4e5b9);
  x ^= x >> 27;
  x *= UINT64_C(0x94d049bb133111eb);
  x ^= x >> 31;
  return x;
}

GSHSCENE_Result gshscene_index_reserve(GSHSCENE_Index * index, const GSHSCENE_Allocator * alloc, size_t have,
    size_t want, const void * owner, GSHSCENE_Index_Hash hash) {
  if (want > SIZE_MAX / 4) {
    return GSHSCENE_ERR_OOM;
  }
  if (index->cap != 0 && want * 2 <= index->cap) {
    return GSHSCENE_OK;
  }
  size_t cap = index->cap ? index->cap : INDEX_MIN_CAP;
  while (want * 2 > cap) {
    cap *= 2;
  }
  size_t * slot = gshscene_calloc(alloc, cap, sizeof *slot);
  if (!slot) {
    return GSHSCENE_ERR_OOM;
  }
  for (size_t entry = 0; entry < have; entry++) {
    size_t at = (size_t)(hash(owner, entry) & (cap - 1));
    while (slot[at] != 0) {
      at = (at + 1) & (cap - 1);
    }
    slot[at] = entry + 1;
  }
  gshscene_free(alloc, index->slot);
  index->slot = slot;
  index->cap = cap;
  return GSHSCENE_OK;
}

size_t gshscene_index_find(const GSHSCENE_Index * index, uint64_t hash, const void * owner,
    GSHSCENE_Index_Match match, const void * key) {
  if (index->cap == 0) {
    return GSHSCENE_NONE;
  }
  size_t at = (size_t)(hash & (index->cap - 1));
  while (index->slot[at] != 0) {
    size_t entry = index->slot[at] - 1;
    if (match(owner, entry, key)) {
      return entry;
    }
    at = (at + 1) & (index->cap - 1);
  }
  return GSHSCENE_NONE;
}

void gshscene_index_insert(GSHSCENE_Index * index, uint64_t hash, size_t entry) {
  size_t at = (size_t)(hash & (index->cap - 1));
  while (index->slot[at] != 0) {
    at = (at + 1) & (index->cap - 1);
  }
  index->slot[at] = entry + 1;
}

void gshscene_index_free(GSHSCENE_Index * index, const GSHSCENE_Allocator * alloc) {
  gshscene_free(alloc, index->slot);
  index->slot = NULL;
  index->cap = 0;
}

/* --- keys ---------------------------------------------------------------------------- */

int gshscene_key_compare(const GSHSCENE_Key * a, const GSHSCENE_Key * b) {
  if (a->domain != b->domain) {
    return a->domain < b->domain ? -1 : 1;
  }
  if (a->value != b->value) {
    return a->value < b->value ? -1 : 1;
  }
  return 0;
}

/* --- the core ------------------------------------------------------------------------- */

void gshscene_core_ref(GSHSCENE_Core * core) {
  gcu_atomic_size_fetch_add(&core->refs, 1);
}

void gshscene_core_unref(GSHSCENE_Core * core) {
  if (gcu_atomic_size_fetch_sub(&core->refs, 1) != 1) {
    return;
  }
  GSHSCENE_Allocator alloc = core->alloc;
  gshscene_store_destroy(core->store);
  GCU_MUTEX_DESTROY(core->lock);
  gshscene_free(&alloc, core);
}

/* --- canonical properties ---------------------------------------------------------------- */

void gshscene_node_init(GSHSCENE_Node_Kind kind, GSHSCENE_Node * node) {
  if (!node) {
    return;
  }
  memset(node, 0, sizeof *node);
  node->kind = kind;
  gshgeom_map_identity(2, &node->map);
  gshgeom_map_identity(2, &node->clip_map);
  node->opacity = 1.0;
  node->composite = GSHSCENE_COMPOSITE_SRC_OVER;
  node->ordering = GSHSCENE_ORDER_FLAT;
  node->clip_rule = GSHGEOM_FILL_NONZERO;
  node->fill_rule = GSHGEOM_FILL_NONZERO;
  node->paint.kind = GSHSCENE_PAINT_SOLID;
  node->paint.rgba[3] = 1.0;
}

int gshscene_group_pushes(const GSHSCENE_Node * group, uint32_t context) {
  return group->opacity < 1.0 || group->composite != (uint32_t)GSHSCENE_COMPOSITE_SRC_OVER ||
         !gshscene_color_space_is_target(&group->blend_space) || group->ordering != (uint32_t)GSHSCENE_ORDER_FLAT ||
         !gshscene_group_pdf_default(group->isolation, group->knockout, group->soft_mask) ||
         context != (uint32_t)GSHSCENE_ORDER_FLAT;
}

const char * gshscene_node_kind_string(GSHSCENE_Node_Kind kind) {
  switch (kind) {
    case GSHSCENE_NODE_GROUP:
      return "group";
    case GSHSCENE_NODE_SHAPE:
      return "shape";
    case GSHSCENE_NODE_INSTANCE:
      return "instance";
    case GSHSCENE_NODE_KIND_COUNT:
      break;
  }
  return "unknown node kind";
}

/* A resource a node may name: a path or glyph run of the scene's store. */
static GSHSCENE_Result check_region(const GSHSCENE_Store * store, const GSHSCENE_Resource * resource) {
  if (!resource || resource->store != store) {
    return GSHSCENE_ERR_INVALID;
  }
  if (resource->kind != GSHSCENE_RESOURCE_PATH && resource->kind != GSHSCENE_RESOURCE_GLYPH_RUN) {
    return GSHSCENE_ERR_INVALID;
  }
  return GSHSCENE_OK;
}

/* A map of a node: valid, and 2D (a 3D map waits for the depth ordering context). */
static GSHSCENE_Result node_map(const GSHGEOM_Map * map, GSHGEOM_Map * out) {
  GSHSCENE_Result result = gshscene_list_canonical_map(map, out);
  if (result != GSHSCENE_OK) {
    return result;
  }
  return out->dim == 2 ? GSHSCENE_OK : GSHSCENE_ERR_UNSUPPORTED;
}

GSHSCENE_Result gshscene_node_canonical(const GSHSCENE_Store * store, const GSHSCENE_Node * in, GSHSCENE_Node * out) {
  if (!in) {
    return GSHSCENE_ERR_INVALID;
  }
  if ((unsigned)in->kind < (unsigned)GSHSCENE_NODE_GROUP || (unsigned)in->kind >= (unsigned)GSHSCENE_NODE_KIND_COUNT) {
    return GSHSCENE_ERR_INVALID;
  }
  if ((in->flags & ~GSHSCENE_NODE_HIDDEN) != 0) {
    return GSHSCENE_ERR_INVALID;
  }
  GSHSCENE_Node node;
  gshscene_node_init(in->kind, &node);
  node.flags = in->flags;
  GSHSCENE_Result result = node_map(&in->map, &node.map);
  if (result != GSHSCENE_OK) {
    return result;
  }
  node.layer = in->layer;
  result = gshscene_list_canonical_depth(in->depth, &node.depth);
  if (result != GSHSCENE_OK) {
    return result;
  }
  switch (in->kind) {
    case GSHSCENE_NODE_GROUP: {
      /* The rules of a list's group push, so that a node and its emitted item agree. */
      GSHSCENE_Group group = {in->opacity, in->blend_space, in->ordering, in->composite, in->isolation, in->knockout,
          in->soft_mask};
      GSHSCENE_Group canonical;
      result = gshscene_list_canonical_group(store, &group, &canonical);
      if (result != GSHSCENE_OK) {
        return result;
      }
      node.opacity = canonical.opacity;
      node.blend_space = canonical.blend_space;
      node.ordering = canonical.ordering;
      node.composite = canonical.composite;
      node.isolation = canonical.isolation;
      node.knockout = canonical.knockout;
      node.soft_mask = canonical.soft_mask;
      if (in->clip) {
        result = check_region(store, in->clip);
        if (result == GSHSCENE_OK) {
          result = node_map(&in->clip_map, &node.clip_map);
        }
        if (result == GSHSCENE_OK) {
          result = gshscene_list_check_rule(in->clip_rule);
        }
        if (result != GSHSCENE_OK) {
          return result;
        }
        node.clip = in->clip;
        node.clip_rule = in->clip_rule;
      }
      break;
    }
    case GSHSCENE_NODE_SHAPE:
      result = check_region(store, in->resource);
      if (result == GSHSCENE_OK) {
        result = gshscene_list_check_rule(in->fill_rule);
      }
      if (result == GSHSCENE_OK) {
        result = gshscene_list_canonical_paint(store, &in->paint, &node.paint);
      }
      if (result != GSHSCENE_OK) {
        return result;
      }
      node.resource = in->resource;
      node.fill_rule = in->fill_rule;
      break;
    default: /* GSHSCENE_NODE_INSTANCE */
      if (in->target == 0) {
        return GSHSCENE_ERR_INVALID;
      }
      node.target = in->target;
      break;
  }
  *out = node;
  return GSHSCENE_OK;
}

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

int gshscene_props_equal(const GSHSCENE_Node * a, const GSHSCENE_Node * b) {
  if (a->kind != b->kind || a->flags != b->flags || a->layer != b->layer || !map_equal(&a->map, &b->map) ||
      gshgeom_f64_bits(a->depth) != gshgeom_f64_bits(b->depth)) {
    return 0;
  }
  switch (a->kind) {
    case GSHSCENE_NODE_GROUP:
      if (gshgeom_f64_bits(a->opacity) != gshgeom_f64_bits(b->opacity) || a->clip != b->clip ||
          !gshscene_space_equal(&a->blend_space, &b->blend_space) || a->composite != b->composite ||
          a->ordering != b->ordering || a->isolation != b->isolation || a->knockout != b->knockout ||
          a->soft_mask != b->soft_mask) {
        return 0;
      }
      return a->clip == NULL || (a->clip_rule == b->clip_rule && map_equal(&a->clip_map, &b->clip_map));
    case GSHSCENE_NODE_SHAPE:
      if (a->resource != b->resource || a->fill_rule != b->fill_rule || a->paint.kind != b->paint.kind ||
          !gshscene_space_equal(&a->paint.space, &b->paint.space) || a->paint.gradient != b->paint.gradient ||
          a->paint.entry != b->paint.entry) {
        return 0;
      }
      for (int i = 0; i < 4; i++) {
        if (gshgeom_f64_bits(a->paint.rgba[i]) != gshgeom_f64_bits(b->paint.rgba[i])) {
          return 0;
        }
      }
      return 1;
    default:
      return a->target == b->target;
  }
}

int gshscene_node_equal(const GSHSCENE_Node_Rec * a, const GSHSCENE_Node_Rec * b) {
  if (a->id != b->id || a->parent != b->parent || a->has_key != b->has_key || a->child_count != b->child_count) {
    return 0;
  }
  if (a->has_key && gshscene_key_compare(&a->key, &b->key) != 0) {
    return 0;
  }
  if (!gshscene_props_equal(&a->p, &b->p)) {
    return 0;
  }
  return a->child_count == 0 || memcmp(a->children, b->children, a->child_count * sizeof *a->children) == 0;
}

/* --- records ----------------------------------------------------------------------------- */

static void props_retain(const GSHSCENE_Node * p) {
  gshscene_resource_retain(p->clip);
  gshscene_resource_retain(p->resource);
  gshscene_resource_retain(p->paint.gradient);
  gshscene_resource_retain(p->soft_mask);
}

static void props_release(const GSHSCENE_Node * p) {
  gshscene_resource_release(p->clip);
  gshscene_resource_release(p->resource);
  gshscene_resource_release(p->paint.gradient);
  gshscene_resource_release(p->soft_mask);
}

GSHSCENE_Node_Rec * gshscene_node_new(
    GSHSCENE_Core * core, uint64_t id, uint64_t parent, const GSHSCENE_Node * canonical) {
  GSHSCENE_Node_Rec * node = gshscene_calloc(&core->alloc, 1, sizeof *node);
  if (!node) {
    return NULL;
  }
  gcu_atomic_size_init(&node->refs, 1);
  node->core = core;
  gshscene_core_ref(core);
  node->id = id;
  node->parent = parent;
  node->p = *canonical;
  props_retain(&node->p);
  return node;
}

GSHSCENE_Node_Rec * gshscene_node_clone(const GSHSCENE_Node_Rec * node) {
  GSHSCENE_Node_Rec * copy = gshscene_node_new(node->core, node->id, node->parent, &node->p);
  if (!copy) {
    return NULL;
  }
  copy->has_key = node->has_key;
  copy->key = node->key;
  if (node->child_count) {
    copy->children = gshscene_alloc(&node->core->alloc, node->child_count * sizeof *copy->children);
    if (!copy->children) {
      gshscene_node_release(copy);
      return NULL;
    }
    memcpy(copy->children, node->children, node->child_count * sizeof *copy->children);
    copy->child_count = node->child_count;
    copy->child_cap = node->child_count;
  }
  return copy;
}

void gshscene_node_retain(GSHSCENE_Node_Rec * node) {
  gcu_atomic_size_fetch_add(&node->refs, 1);
}

void gshscene_node_release(GSHSCENE_Node_Rec * node) {
  if (!node || gcu_atomic_size_fetch_sub(&node->refs, 1) != 1) {
    return;
  }
  GSHSCENE_Core * core = node->core;
  props_release(&node->p);
  gshscene_free(&core->alloc, node->children);
  gshscene_free(&core->alloc, node);
  gshscene_core_unref(core);
}

void gshscene_node_assign(GSHSCENE_Node_Rec * node, const GSHSCENE_Node * canonical) {
  props_retain(canonical);
  props_release(&node->p);
  node->p = *canonical;
}

/* --- children ----------------------------------------------------------------------------- */

GSHSCENE_Result gshscene_node_reserve_children(GSHSCENE_Node_Rec * node, size_t extra) {
  if (extra > SIZE_MAX / sizeof *node->children - node->child_count) {
    return GSHSCENE_ERR_OOM;
  }
  size_t need = node->child_count + extra;
  if (need <= node->child_cap) {
    return GSHSCENE_OK;
  }
  size_t cap = node->child_cap ? node->child_cap : 4;
  while (cap < need) {
    cap *= 2;
  }
  uint64_t * grown = gshscene_alloc(&node->core->alloc, cap * sizeof *grown);
  if (!grown) {
    return GSHSCENE_ERR_OOM;
  }
  if (node->child_count) {
    memcpy(grown, node->children, node->child_count * sizeof *grown);
  }
  gshscene_free(&node->core->alloc, node->children);
  node->children = grown;
  node->child_cap = cap;
  return GSHSCENE_OK;
}

void gshscene_node_insert_child(GSHSCENE_Node_Rec * node, size_t position, uint64_t child) {
  if (position < node->child_count) {
    memmove(&node->children[position + 1], &node->children[position],
        (node->child_count - position) * sizeof *node->children);
  }
  node->children[position] = child;
  node->child_count++;
}

size_t gshscene_node_remove_child(GSHSCENE_Node_Rec * node, uint64_t child) {
  for (size_t i = 0; i < node->child_count; i++) {
    if (node->children[i] == child) {
      memmove(&node->children[i], &node->children[i + 1], (node->child_count - i - 1) * sizeof *node->children);
      node->child_count--;
      return i;
    }
  }
  return GSHSCENE_NONE;
}

/* --- hash --------------------------------------------------------------------------------- */

void gshscene_node_hash(GSHGEOM_Hash * h, const GSHSCENE_Node_Rec * node) {
  const GSHSCENE_Node * p = &node->p;
  gshgeom_hash_u64(h, node->id);
  gshgeom_hash_u8(h, (uint8_t)p->kind);
  gshgeom_hash_u8(h, (uint8_t)p->flags);
  gshgeom_hash_u64(h, node->parent);
  gshgeom_hash_u8(h, node->has_key ? 1 : 0);
  if (node->has_key) {
    gshgeom_hash_u64(h, node->key.domain);
    gshgeom_hash_u64(h, node->key.value);
  }
  switch (p->kind) {
    case GSHSCENE_NODE_GROUP:
      gshgeom_hash_u64(h, gshgeom_map_hash(&p->map));
      gshgeom_hash_f64(h, p->opacity);
      gshscene_space_hash(h, &p->blend_space);
      gshscene_group_hash_composite(h, p->composite, p->isolation, p->knockout, p->soft_mask);
      gshgeom_hash_u32(h, p->ordering);
      gshgeom_hash_i32(h, p->layer);
      gshgeom_hash_f64(h, p->depth);
      gshgeom_hash_u8(h, p->clip ? 1 : 0);
      if (p->clip) {
        gshgeom_hash_u64(h, gshscene_resource_hash(p->clip));
        gshgeom_hash_u64(h, gshgeom_map_hash(&p->clip_map));
        gshgeom_hash_u8(h, (uint8_t)p->clip_rule);
      }
      gshgeom_hash_u64(h, (uint64_t)node->child_count);
      for (size_t i = 0; i < node->child_count; i++) {
        gshgeom_hash_u64(h, node->children[i]);
      }
      break;
    case GSHSCENE_NODE_SHAPE:
      gshgeom_hash_u64(h, gshscene_resource_hash(p->resource));
      gshgeom_hash_u64(h, gshgeom_map_hash(&p->map));
      gshgeom_hash_u8(h, (uint8_t)p->fill_rule);
      gshgeom_hash_u8(h, (uint8_t)p->paint.kind);
      for (int k = 0; k < 4; k++) {
        gshgeom_hash_f64(h, p->paint.rgba[k]);
      }
      gshscene_space_hash(h, &p->paint.space);
      gshgeom_hash_u32(h, p->paint.entry);
      gshgeom_hash_u64(h, p->paint.gradient ? gshscene_resource_hash(p->paint.gradient) : 0);
      gshgeom_hash_i32(h, p->layer);
      gshgeom_hash_f64(h, p->depth);
      break;
    default:
      gshgeom_hash_u64(h, p->target);
      gshgeom_hash_u64(h, gshgeom_map_hash(&p->map));
      gshgeom_hash_i32(h, p->layer);
      gshgeom_hash_f64(h, p->depth);
      break;
  }
}
