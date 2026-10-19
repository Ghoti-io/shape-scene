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
    const GSHGEOM_Map * base, size_t level) {
  if (!force && (node->p.flags & GSHSCENE_NODE_HIDDEN)) {
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
        result = w->ops->group_enter(w->ctx, w, node, &group_base);
        if (result != GSHSCENE_OK) {
          return result;
        }
      }
      for (size_t i = 0; i < node->child_count; i++) {
        const GSHSCENE_Node_Rec * child = gshscene_version_find(w->version, node->children[i]);
        if (!child) {
          return GSHSCENE_ERR_INTERNAL;
        }
        result = walk_node(w, child, 0, &group_base, level + 1);
        if (result != GSHSCENE_OK) {
          return result;
        }
      }
      if (w->ops->group_leave) {
        result = w->ops->group_leave(w->ctx, w, node);
      }
      return result;
    }
    case GSHSCENE_NODE_SHAPE: {
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
      result = walk_node(w, target, 1, &identity, level + 1);
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
  GSHSCENE_Result result = walk_node(&walk, node, force, base, 1);
  gshscene_free(alloc, walk.path);
  gshscene_free(alloc, walk.chain);
  return result;
}
