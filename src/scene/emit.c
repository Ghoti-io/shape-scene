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
 * Emission: a version's list over interned resources (version.h).
 *
 * The version's list is made by the one walk, and each instanced target is made
 * by the same walk started at the target (its hidden flag ignored, at the
 * identity), closed and interned as a picture, once per target.  Alongside each
 * list goes a table of origins: the ID of the node each item came from.
 */

#include <ghoti.io/shape-scene/macros.h>

#include "internal.h"

#include <stdlib.h>
#include <string.h>

/** One picture and the origins of its items. */
typedef struct Table {
  uint64_t target;
  const GSHSCENE_Resource * picture;
  uint64_t * origin;
  size_t count;
} Table;

struct GSHSCENE_Emission {
  GSHSCENE_List * top;
  uint64_t * top_origin;
  size_t top_count;
  Table * tables;
  size_t table_count;
  size_t table_cap;
  GSHSCENE_Index index; /* over tables, by target */
};

void gshscene_emission_free(struct GSHSCENE_Emission * em, const GSHSCENE_Allocator * alloc) {
  if (!em) {
    return;
  }
  gshscene_list_destroy(em->top);
  gshscene_free(alloc, em->top_origin);
  for (size_t i = 0; i < em->table_count; i++) {
    gshscene_resource_release(em->tables[i].picture);
    gshscene_free(alloc, em->tables[i].origin);
  }
  gshscene_free(alloc, em->tables);
  gshscene_index_free(&em->index, alloc);
  gshscene_free(alloc, em);
}

static uint64_t table_hash(const void * owner, size_t entry) {
  return gshscene_mix64(((const struct GSHSCENE_Emission *)owner)->tables[entry].target);
}

static int table_match(const void * owner, size_t entry, const void * key) {
  return ((const struct GSHSCENE_Emission *)owner)->tables[entry].target == *(const uint64_t *)key;
}

static size_t table_find(const struct GSHSCENE_Emission * em, uint64_t target) {
  return gshscene_index_find(&em->index, gshscene_mix64(target), em, table_match, &target);
}

/* --- the sink: one list and its origins ------------------------------------------------------- */

typedef struct Sink {
  struct GSHSCENE_Emission * em;
  const GSHSCENE_Core * core;
  GSHSCENE_List * list;
  uint64_t * origin;
  size_t count;
  size_t cap;
} Sink;

static GSHSCENE_Result sink_origin(Sink * s, uint64_t node) {
  if (s->count == s->cap) {
    size_t cap = s->cap ? s->cap * 2 : 32;
    if (cap > SIZE_MAX / sizeof *s->origin) {
      return GSHSCENE_ERR_OOM;
    }
    uint64_t * grown = gshscene_alloc(&s->core->alloc, cap * sizeof *grown);
    if (!grown) {
      return GSHSCENE_ERR_OOM;
    }
    if (s->count) {
      memcpy(grown, s->origin, s->count * sizeof *grown);
    }
    gshscene_free(&s->core->alloc, s->origin);
    s->origin = grown;
    s->cap = cap;
  }
  s->origin[s->count++] = node;
  return GSHSCENE_OK;
}

static GSHSCENE_Result ensure_picture(Sink * parent, const GSHSCENE_Walk * walk, uint64_t target, size_t * table);

static GSHSCENE_Result emit_group_enter(
    void * ctx, const GSHSCENE_Walk * walk, const GSHSCENE_Node_Rec * group, const GSHGEOM_Map * base) {
  Sink * s = ctx;
  const GSHSCENE_Node * p = &group->p;
  GSHSCENE_Result result;
  if (p->clip) {
    GSHGEOM_Map clip_map;
    if (gshgeom_map_compose(base, &p->clip_map, &clip_map) != GSHGEOM_OK) {
      return GSHSCENE_ERR_LIMIT;
    }
    result = gshscene_list_clip_push(s->list, p->clip, &clip_map, p->clip_rule, p->layer, p->depth);
    if (result == GSHSCENE_OK) {
      result = sink_origin(s, group->id);
    }
    if (result != GSHSCENE_OK) {
      return result;
    }
  }
  if (gshscene_group_pushes(p, walk->context)) {
    GSHSCENE_Group g = {p->opacity, p->blend_space, p->ordering, p->composite};
    result = gshscene_list_group_push(s->list, &g, p->layer, p->depth);
    if (result == GSHSCENE_OK) {
      result = sink_origin(s, group->id);
    }
    if (result != GSHSCENE_OK) {
      return result;
    }
  }
  return GSHSCENE_OK;
}

static GSHSCENE_Result emit_group_leave(void * ctx, const GSHSCENE_Walk * walk, const GSHSCENE_Node_Rec * group) {
  Sink * s = ctx;
  GSHSCENE_Result result = GSHSCENE_OK;
  if (gshscene_group_pushes(&group->p, walk->context)) {
    result = gshscene_list_group_pop(s->list);
    if (result == GSHSCENE_OK) {
      result = sink_origin(s, group->id);
    }
    if (result != GSHSCENE_OK) {
      return result;
    }
  }
  if (group->p.clip) {
    result = gshscene_list_clip_pop(s->list);
    if (result == GSHSCENE_OK) {
      result = sink_origin(s, group->id);
    }
  }
  return result;
}

static GSHSCENE_Result emit_shape(
    void * ctx, const GSHSCENE_Walk * walk, const GSHSCENE_Node_Rec * shape, const GSHGEOM_Map * map) {
  (void)walk;
  Sink * s = ctx;
  GSHSCENE_Result result = gshscene_list_fill(
      s->list, shape->p.resource, map, shape->p.fill_rule, &shape->p.paint, shape->p.layer, shape->p.depth);
  if (result == GSHSCENE_OK) {
    result = sink_origin(s, shape->id);
  }
  return result;
}

static GSHSCENE_Result emit_instance(void * ctx, const GSHSCENE_Walk * walk, const GSHSCENE_Node_Rec * instance,
    const GSHGEOM_Map * map, int * descend) {
  Sink * s = ctx;
  *descend = 0;
  size_t table = 0;
  GSHSCENE_Result result = ensure_picture(s, walk, instance->p.target, &table);
  if (result != GSHSCENE_OK) {
    return result;
  }
  result = gshscene_list_instance(
      s->list, s->em->tables[table].picture, map, instance->p.layer, instance->p.depth);
  if (result == GSHSCENE_OK) {
    result = sink_origin(s, instance->id);
  }
  return result;
}

static size_t emit_position(void * ctx) {
  return gshscene_list_count(((Sink *)ctx)->list);
}

static GSHSCENE_Result emit_tie(void * ctx, const GSHSCENE_Walk * walk, size_t first, size_t second, double depth,
    GSHSCENE_Tie_Reason reason) {
  (void)walk;
  return gshscene_list_add_tie(((Sink *)ctx)->list, first, second, depth, reason);
}

static const GSHSCENE_Walk_Ops emit_ops = {
    emit_group_enter, emit_group_leave, emit_shape, emit_instance, NULL, emit_position, emit_tie};

/* Make a sink for a new list. */
static GSHSCENE_Result sink_open(Sink * s, struct GSHSCENE_Emission * em, const GSHSCENE_Core * core) {
  memset(s, 0, sizeof *s);
  s->em = em;
  s->core = core;
  return gshscene_list_create(&core->alloc, &core->limits, core->store, &s->list);
}

static void sink_discard(Sink * s) {
  gshscene_list_destroy(s->list);
  gshscene_free(&s->core->alloc, s->origin);
  s->list = NULL;
  s->origin = NULL;
}

/* The picture of a target: found, or made now and recorded. */
static GSHSCENE_Result ensure_picture(Sink * parent, const GSHSCENE_Walk * walk, uint64_t target, size_t * table) {
  struct GSHSCENE_Emission * em = parent->em;
  size_t found = table_find(em, target);
  if (found != GSHSCENE_NONE) {
    *table = found;
    return GSHSCENE_OK;
  }
  const GSHSCENE_Core * core = parent->core;
  const GSHSCENE_Node_Rec * node = gshscene_version_find(walk->version, target);
  if (!node) {
    return GSHSCENE_ERR_INTERNAL;
  }
  Sink sink;
  GSHSCENE_Result result = sink_open(&sink, em, core);
  if (result != GSHSCENE_OK) {
    return result;
  }
  GSHGEOM_Map identity;
  gshgeom_map_identity(2, &identity);
  result = gshscene_walk(walk->version, node, 1, &identity, &emit_ops, &sink);
  if (result == GSHSCENE_OK) {
    result = gshscene_list_close_ordered(sink.list);
  }
  const GSHSCENE_Resource * picture = NULL;
  if (result == GSHSCENE_OK) {
    result = gshscene_store_intern_picture(core->store, sink.list, &picture);
  }
  gshscene_list_destroy(sink.list);
  sink.list = NULL;
  /* Room for the table and its index entry, before anything is recorded. */
  if (result == GSHSCENE_OK && em->table_count == em->table_cap) {
    size_t cap = em->table_cap ? em->table_cap * 2 : 8;
    Table * grown = cap > SIZE_MAX / sizeof *grown ? NULL : gshscene_alloc(&core->alloc, cap * sizeof *grown);
    if (!grown) {
      result = GSHSCENE_ERR_OOM;
    } else {
      if (em->table_count) {
        memcpy(grown, em->tables, em->table_count * sizeof *grown);
      }
      gshscene_free(&core->alloc, em->tables);
      em->tables = grown;
      em->table_cap = cap;
    }
  }
  if (result == GSHSCENE_OK) {
    result = gshscene_index_reserve(&em->index, &core->alloc, em->table_count, em->table_count + 1, em, table_hash);
  }
  if (result != GSHSCENE_OK) {
    gshscene_resource_release(picture);
    gshscene_free(&core->alloc, sink.origin);
    return result;
  }
  Table * t = &em->tables[em->table_count];
  t->target = target;
  t->picture = picture;
  t->origin = sink.origin;
  t->count = sink.count;
  gshscene_index_insert(&em->index, gshscene_mix64(target), em->table_count);
  *table = em->table_count++;
  return GSHSCENE_OK;
}

/* --- the version's list ------------------------------------------------------------------------- */

/* Build the emission under the version's lock. */
static GSHSCENE_Result build(const struct GSHSCENE_Version * version, struct GSHSCENE_Emission ** out) {
  const GSHSCENE_Core * core = version->core;
  struct GSHSCENE_Emission * em = gshscene_calloc(&core->alloc, 1, sizeof *em);
  if (!em) {
    return GSHSCENE_ERR_OOM;
  }
  Sink sink;
  GSHSCENE_Result result = sink_open(&sink, em, core);
  if (result != GSHSCENE_OK) {
    gshscene_emission_free(em, &core->alloc);
    return result;
  }
  /* The foreground and the palette are the version's; a picture, which any list may draw, has neither of its own. */
  result = gshscene_list_set_foreground(sink.list, &version->foreground);
  if (result == GSHSCENE_OK) {
    result = gshscene_list_set_palette(sink.list, version->palette);
  }
  if (result != GSHSCENE_OK) {
    sink_discard(&sink);
    gshscene_emission_free(em, &core->alloc);
    return result;
  }
  GSHGEOM_Map identity;
  gshgeom_map_identity(2, &identity);
  const GSHSCENE_Node_Rec * root = gshscene_version_find(version, GSHSCENE_ROOT_ID);
  result = root ? gshscene_walk(version, root, 0, &identity, &emit_ops, &sink) : GSHSCENE_ERR_INTERNAL;
  if (result == GSHSCENE_OK) {
    result = gshscene_list_close_ordered(sink.list);
  }
  if (result != GSHSCENE_OK) {
    sink_discard(&sink);
    gshscene_emission_free(em, &core->alloc);
    return result;
  }
  em->top = sink.list;
  em->top_origin = sink.origin;
  em->top_count = sink.count;
  *out = em;
  return GSHSCENE_OK;
}

static GSHSCENE_Result ensure(const GSHSCENE_Version * handle, struct GSHSCENE_Emission ** out) {
  struct GSHSCENE_Version * version = (struct GSHSCENE_Version *)handle;
  GCU_MUTEX_LOCK(version->lock);
  GSHSCENE_Result result = GSHSCENE_OK;
  if (!version->emission) {
    result = build(version, &version->emission);
  }
  *out = version->emission;
  GCU_MUTEX_UNLOCK(version->lock);
  return result;
}

GSHSCENE_Result gshscene_version_list(const GSHSCENE_Version * version, const GSHSCENE_List ** out) {
  if (!version || !out) {
    return GSHSCENE_ERR_INVALID;
  }
  struct GSHSCENE_Emission * em = NULL;
  GSHSCENE_Result result = ensure(version, &em);
  if (result != GSHSCENE_OK) {
    return result;
  }
  *out = em->top;
  return GSHSCENE_OK;
}

GSHSCENE_Result gshscene_version_item_node(
    const GSHSCENE_Version * version, uint64_t table, size_t index, uint64_t * out_node) {
  if (!version || !out_node) {
    return GSHSCENE_ERR_INVALID;
  }
  struct GSHSCENE_Emission * em = NULL;
  GSHSCENE_Result result = ensure(version, &em);
  if (result != GSHSCENE_OK) {
    return result;
  }
  if (table == 0) {
    if (index >= em->top_count) {
      return GSHSCENE_ERR_INVALID;
    }
    *out_node = em->top_origin[index];
    return GSHSCENE_OK;
  }
  size_t at = table_find(em, table);
  if (at == GSHSCENE_NONE || index >= em->tables[at].count) {
    return GSHSCENE_ERR_INVALID;
  }
  *out_node = em->tables[at].origin[index];
  return GSHSCENE_OK;
}

GSHSCENE_Result gshscene_version_picture(
    const GSHSCENE_Version * version, uint64_t target, const GSHSCENE_Resource ** out_picture) {
  if (!version || !out_picture) {
    return GSHSCENE_ERR_INVALID;
  }
  struct GSHSCENE_Emission * em = NULL;
  GSHSCENE_Result result = ensure(version, &em);
  if (result != GSHSCENE_OK) {
    return result;
  }
  size_t at = table_find(em, target);
  if (at == GSHSCENE_NONE) {
    return GSHSCENE_ERR_INVALID;
  }
  *out_picture = em->tables[at].picture;
  return GSHSCENE_OK;
}
