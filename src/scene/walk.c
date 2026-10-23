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
 * The one walk over a version's occurrences, in draw order, with the placements
 * composed the way a sink composes them.  Emission, the visitor and the damage
 * are consumers of this and of nothing else, so the three never disagree about
 * what is drawn, in what order, or where.
 */

#include <ghoti.io/shape-scene/macros.h>

#include "internal.h"

#include <string.h>

/* The composition of two valid 2D maps: a failure is a result out of range. */
static GSHSCENE_Result compose(const GSHGEOM_Map * outer, const GSHGEOM_Map * inner, GSHGEOM_Map * out) {
  GSHGEOM_Result result = gshgeom_map_compose(outer, inner, out);
  if (result == GSHGEOM_OK) {
    return GSHSCENE_OK;
  }
  return result == GSHGEOM_ERR_LIMIT ? GSHSCENE_ERR_LIMIT : GSHSCENE_ERR_INTERNAL;
}

GSHSCENE_Result gshscene_walk_compose(
    const GSHSCENE_Walk * walk, const GSHGEOM_Map * item_map, const GSHGEOM_Map * target, GSHGEOM_Map * out) {
  walk->chain[walk->path_count] = *item_map;
  GSHGEOM_Result result = gshgeom_map_compose_chain(walk->chain, walk->path_count + 1, target, out);
  if (result == GSHGEOM_OK) {
    return GSHSCENE_OK;
  }
  return result == GSHGEOM_ERR_LIMIT ? GSHSCENE_ERR_LIMIT : GSHSCENE_ERR_INTERNAL;
}

static GSHSCENE_Result walk_node(GSHSCENE_Walk * w, const GSHSCENE_Node_Rec * node, int force,
    const GSHGEOM_Map * base, size_t level, uint32_t context);

static int node_hidden(const GSHSCENE_Node_Rec * node) {
  return (node->p.flags & GSHSCENE_NODE_HIDDEN) != 0;
}

/* Whether the child @p b is painted strictly before the child @p a (a stable merge sort's test): N11's context, then
 * for depth the ID, which orders siblings as their instance paths do (the paths differ in the last element only). */
static int child_before(uint32_t context, const GSHSCENE_Node_Rec * b, const GSHSCENE_Node_Rec * a) {
  int tied;
  int order = gshscene_order_compare(context, b->p.layer, b->p.depth, a->p.layer, a->p.depth, &tied);
  if (order == 0 && context == (uint32_t)GSHSCENE_ORDER_DEPTH) {
    return b->id < a->id;
  }
  return order < 0;
}

static void children_sort(
    uint32_t context, const GSHSCENE_Node_Rec ** nodes, const GSHSCENE_Node_Rec ** temp, size_t count) {
  for (size_t width = 1; width < count; width *= 2) {
    for (size_t lo = 0; lo < count; lo += 2 * width) {
      size_t mid = lo + width < count ? lo + width : count;
      size_t hi = lo + 2 * width < count ? lo + 2 * width : count;
      size_t i = lo;
      size_t j = mid;
      size_t k = lo;
      while (i < mid && j < hi) {
        if (child_before(context, nodes[j], nodes[i])) {
          temp[k++] = nodes[j++];
        } else {
          temp[k++] = nodes[i++];
        }
      }
      while (i < mid) {
        temp[k++] = nodes[i++];
      }
      while (j < hi) {
        temp[k++] = nodes[j++];
      }
    }
    memcpy(nodes, temp, count * sizeof *nodes);
  }
}

/* The children of a group in the order its context gives, the ties of a depth group reported as they are found. */
static GSHSCENE_Result walk_ordered_children(GSHSCENE_Walk * w, const GSHSCENE_Node_Rec * node,
    const GSHGEOM_Map * group_base, size_t level) {
  const GSHSCENE_Allocator * alloc = &w->version->core->alloc;
  uint32_t context = node->p.ordering;
  size_t room = node->child_count ? node->child_count : 1;
  if (room > SIZE_MAX / (2 * sizeof(const GSHSCENE_Node_Rec *))) {
    return GSHSCENE_ERR_OOM;
  }
  const GSHSCENE_Node_Rec ** list = gshscene_alloc(alloc, 2 * room * sizeof *list);
  if (!list) {
    return GSHSCENE_ERR_OOM;
  }
  const GSHSCENE_Node_Rec ** temp = list + room;
  size_t count = 0;
  GSHSCENE_Result result = GSHSCENE_OK;
  for (size_t i = 0; i < node->child_count && result == GSHSCENE_OK; i++) {
    const GSHSCENE_Node_Rec * child = gshscene_version_find(w->version, node->children[i]);
    if (!child) {
      result = GSHSCENE_ERR_INTERNAL;
    } else if (!node_hidden(child)) {
      list[count++] = child;
    }
  }
  if (result == GSHSCENE_OK) {
    children_sort(context, list, temp, count);
  }
  const GSHSCENE_Node_Rec * previous = NULL; /* the last child that made any items */
  size_t previous_pos = 0;
  for (size_t i = 0; i < count && result == GSHSCENE_OK; i++) {
    const GSHSCENE_Node_Rec * child = list[i];
    size_t before = w->ops->position ? w->ops->position(w->ctx) : 0;
    result = walk_node(w, child, 0, group_base, level + 1, context);
    if (result != GSHSCENE_OK) {
      break;
    }
    size_t after = w->ops->position ? w->ops->position(w->ctx) : 0;
    if (after == before) {
      continue; /* it made nothing: not a unit of the sequence */
    }
    if (previous && context == (uint32_t)GSHSCENE_ORDER_DEPTH && w->ops->tie &&
        previous->p.depth == child->p.depth) {
      GSHSCENE_Tie_Reason reason = previous->p.layer != child->p.layer ? GSHSCENE_TIE_LAYER : GSHSCENE_TIE_PATH;
      result = w->ops->tie(w->ctx, w, previous_pos, before, child->p.depth, reason);
    }
    previous = child;
    previous_pos = before;
  }
  gshscene_free(alloc, list);
  return result;
}

static GSHSCENE_Result walk_node(GSHSCENE_Walk * w, const GSHSCENE_Node_Rec * node, int force,
    const GSHGEOM_Map * base, size_t level, uint32_t context) {
  if (!force && node_hidden(node)) {
    return GSHSCENE_OK;
  }
  /* A published version is validated to be no deeper than this; the check keeps the recursion bounded if it were not. */
  if (level > w->version->core->limits.max_node_depth) {
    return GSHSCENE_ERR_INTERNAL;
  }
  GSHSCENE_Result result;
  switch (node->p.kind) {
    case GSHSCENE_NODE_GROUP: {
      GSHGEOM_Map group_base;
      result = compose(base, &node->p.map, &group_base);
      if (result != GSHSCENE_OK) {
        return result;
      }
      if (w->ops->group_enter) {
        w->context = context;
        result = w->ops->group_enter(w->ctx, w, node, &group_base);
        if (result != GSHSCENE_OK) {
          return result;
        }
      }
      if (node->p.ordering != (uint32_t)GSHSCENE_ORDER_FLAT) {
        result = walk_ordered_children(w, node, &group_base, level);
        if (result != GSHSCENE_OK) {
          return result;
        }
      } else {
        for (size_t i = 0; i < node->child_count; i++) {
          const GSHSCENE_Node_Rec * child = gshscene_version_find(w->version, node->children[i]);
          if (!child) {
            return GSHSCENE_ERR_INTERNAL;
          }
          result = walk_node(w, child, 0, &group_base, level + 1, GSHSCENE_ORDER_FLAT);
          if (result != GSHSCENE_OK) {
            return result;
          }
        }
      }
      if (w->ops->group_leave) {
        w->context = context;
        result = w->ops->group_leave(w->ctx, w, node);
      }
      return result;
    }
    case GSHSCENE_NODE_SHAPE:
    case GSHSCENE_NODE_STROKE:
    case GSHSCENE_NODE_PLACEHOLDER: { /* a leaf: the shape callback, which reads the kind */
      GSHGEOM_Map item_map;
      result = compose(base, &node->p.map, &item_map);
      if (result != GSHSCENE_OK) {
        return result;
      }
      return w->ops->shape ? w->ops->shape(w->ctx, w, node, &item_map) : GSHSCENE_OK;
    }
    default: {
      GSHGEOM_Map item_map;
      result = compose(base, &node->p.map, &item_map);
      if (result != GSHSCENE_OK) {
        return result;
      }
      int descend = 0;
      if (w->ops->instance) {
        result = w->ops->instance(w->ctx, w, node, &item_map, &descend);
        if (result != GSHSCENE_OK) {
          return result;
        }
      }
      if (!descend) {
        return GSHSCENE_OK;
      }
      const GSHSCENE_Node_Rec * target = gshscene_version_find(w->version, node->p.target);
      if (!target) {
        return GSHSCENE_ERR_INTERNAL;
      }
      GSHGEOM_Map identity;
      gshgeom_map_identity(2, &identity);
      w->chain[w->path_count] = item_map;
      w->path[w->path_count] = node->id;
      w->path_count++;
      result = walk_node(w, target, 1, &identity, level + 1, GSHSCENE_ORDER_FLAT);
      w->path_count--;
      if (result == GSHSCENE_OK && w->ops->instance_leave) {
        result = w->ops->instance_leave(w->ctx, w, node);
      }
      return result;
    }
  }
}

GSHSCENE_Result gshscene_walk(const struct GSHSCENE_Version * version, const GSHSCENE_Node_Rec * node, int force,
    const GSHGEOM_Map * base, const GSHSCENE_Walk_Ops * ops, void * ctx) {
  const GSHSCENE_Allocator * alloc = &version->core->alloc;
  size_t room = version->core->limits.max_node_depth + 2;
  GSHSCENE_Walk walk;
  walk.version = version;
  walk.ops = ops;
  walk.ctx = ctx;
  walk.path_count = 0;
  walk.chain_cap = room;
  walk.path = gshscene_alloc(alloc, room * sizeof *walk.path);
  walk.chain = gshscene_alloc(alloc, room * sizeof *walk.chain);
  if (!walk.path || !walk.chain) {
    gshscene_free(alloc, walk.path);
    gshscene_free(alloc, walk.chain);
    return GSHSCENE_ERR_OOM;
  }
  walk.context = GSHSCENE_ORDER_FLAT;
  GSHSCENE_Result result = walk_node(&walk, node, force, base, 1, GSHSCENE_ORDER_FLAT);
  gshscene_free(alloc, walk.path);
  gshscene_free(alloc, walk.chain);
  return result;
}
