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
 * Versions: making and freeing them, reading a node, the key map, the hash and
 * the dump (version.h), the instance paths and the visitor.
 */

#include <ghoti.io/shape-scene/macros.h>

#include "internal.h"

#include <stdlib.h>
#include <string.h>

#define HEX(x) ((unsigned long long)(x))

/* --- keys ------------------------------------------------------------------------------- */

void gshscene_keys_ref(GSHSCENE_Keys * keys) {
  if (keys) {
    gcu_atomic_size_fetch_add(&keys->refs, 1);
  }
}

void gshscene_keys_unref(GSHSCENE_Keys * keys) {
  if (keys && gcu_atomic_size_fetch_sub(&keys->refs, 1) == 1) {
    GSHSCENE_Allocator alloc = keys->alloc;
    gshscene_free(&alloc, keys);
  }
}

uint64_t gshscene_keys_find(const GSHSCENE_Keys * keys, const GSHSCENE_Key * key) {
  if (!keys) {
    return 0;
  }
  size_t lo = 0;
  size_t hi = keys->count;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    int c = gshscene_key_compare(&keys->entries[mid].key, key);
    if (c == 0) {
      return keys->entries[mid].id;
    }
    if (c < 0) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return 0;
}

/* --- making and freeing ------------------------------------------------------------------- */

GSHSCENE_Result gshscene_version_make(GSHSCENE_Core * core, uint64_t number, GSHSCENE_Node_Rec ** nodes,
    size_t count, GSHSCENE_Keys * keys, const GSHSCENE_Color * foreground, const GSHSCENE_Resource * palette,
    const struct GSHSCENE_Version ** out) {
  struct GSHSCENE_Version * version = gshscene_calloc(&core->alloc, 1, sizeof *version);
  if (!version) {
    return GSHSCENE_ERR_OOM;
  }
  if (GCU_MUTEX_CREATE(version->lock) != 0) {
    gshscene_free(&core->alloc, version);
    return GSHSCENE_ERR_INTERNAL;
  }
  gcu_atomic_size_init(&version->refs, 1);
  version->core = core;
  gshscene_core_ref(core);
  version->number = number;
  version->nodes = nodes;
  version->count = count;
  for (size_t i = 0; i < count; i++) {
    gshscene_node_retain(nodes[i]);
  }
  version->keys = keys;
  gshscene_keys_ref(keys);
  version->foreground = *foreground;
  version->palette = gshscene_resource_retain(palette);
  *out = version;
  return GSHSCENE_OK;
}

const GSHSCENE_Version * gshscene_version_retain(const GSHSCENE_Version * version) {
  if (version) {
    gcu_atomic_size_fetch_add(&((struct GSHSCENE_Version *)version)->refs, 1);
  }
  return version;
}

void gshscene_version_release(const GSHSCENE_Version * handle) {
  if (!handle) {
    return;
  }
  struct GSHSCENE_Version * version = (struct GSHSCENE_Version *)handle;
  if (gcu_atomic_size_fetch_sub(&version->refs, 1) != 1) {
    return;
  }
  GSHSCENE_Core * core = version->core;
  GSHSCENE_Allocator alloc = core->alloc;
  gshscene_emission_free(version->emission, &alloc);
  for (size_t i = 0; i < version->count; i++) {
    gshscene_node_release(version->nodes[i]);
  }
  gshscene_free(&alloc, version->nodes);
  gshscene_keys_unref(version->keys);
  gshscene_resource_release(version->palette);
  GCU_MUTEX_DESTROY(version->lock);
  gshscene_free(&alloc, version);
  gshscene_core_unref(core);
}

/* --- reading -------------------------------------------------------------------------------- */

uint64_t gshscene_version_serial(const GSHSCENE_Version * version) {
  return version ? version->number : 0;
}

size_t gshscene_version_node_count(const GSHSCENE_Version * version) {
  return version ? version->count : 0;
}

GSHSCENE_Result gshscene_version_foreground(const GSHSCENE_Version * version, GSHSCENE_Color * out) {
  if (!version || !out) {
    return GSHSCENE_ERR_INVALID;
  }
  *out = version->foreground;
  return GSHSCENE_OK;
}

const GSHSCENE_Resource * gshscene_version_palette(const GSHSCENE_Version * version) {
  return version ? version->palette : NULL;
}

size_t gshscene_version_key_count(const GSHSCENE_Version * version) {
  return version && version->keys ? version->keys->count : 0;
}

size_t gshscene_nodes_find(GSHSCENE_Node_Rec * const * nodes, size_t count, uint64_t id) {
  size_t lo = 0;
  size_t hi = count;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (nodes[mid]->id == id) {
      return mid;
    }
    if (nodes[mid]->id < id) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return GSHSCENE_NONE;
}

GSHSCENE_Node_Rec * gshscene_version_find(const struct GSHSCENE_Version * version, uint64_t id) {
  size_t at = gshscene_nodes_find(version->nodes, version->count, id);
  return at == GSHSCENE_NONE ? NULL : version->nodes[at];
}

GSHSCENE_Result gshscene_version_node(const GSHSCENE_Version * version, uint64_t id, GSHSCENE_Node_Info * out) {
  if (!version || !out) {
    return GSHSCENE_ERR_INVALID;
  }
  const GSHSCENE_Node_Rec * node = gshscene_version_find(version, id);
  if (!node) {
    return GSHSCENE_ERR_INVALID;
  }
  memset(out, 0, sizeof *out);
  out->id = node->id;
  out->parent = node->parent;
  out->has_key = node->has_key;
  if (node->has_key) {
    out->key = node->key;
  }
  out->child_count = node->child_count;
  out->node = node->p;
  return GSHSCENE_OK;
}

GSHSCENE_Result gshscene_version_child(
    const GSHSCENE_Version * version, uint64_t id, size_t index, uint64_t * out_id) {
  if (!version || !out_id) {
    return GSHSCENE_ERR_INVALID;
  }
  const GSHSCENE_Node_Rec * node = gshscene_version_find(version, id);
  if (!node || node->p.kind != GSHSCENE_NODE_GROUP || index >= node->child_count) {
    return GSHSCENE_ERR_INVALID;
  }
  *out_id = node->children[index];
  return GSHSCENE_OK;
}

GSHSCENE_Result gshscene_version_lookup_key(
    const GSHSCENE_Version * version, const GSHSCENE_Key * key, uint64_t * out_id) {
  if (!version || !key || !out_id) {
    return GSHSCENE_ERR_INVALID;
  }
  uint64_t id = gshscene_keys_find(version->keys, key);
  if (id == 0) {
    return GSHSCENE_ERR_INVALID;
  }
  *out_id = id;
  return GSHSCENE_OK;
}

/* --- hash and dump --------------------------------------------------------------------------- */

uint64_t gshscene_version_hash(const GSHSCENE_Version * version) {
  GSHGEOM_Hash h;
  gshgeom_hash_init(&h);
  if (!version) {
    return gshgeom_hash_value(&h);
  }
  gshgeom_hash_u32(&h, VERSION_FORMAT_VERSION);
  gshgeom_hash_u32(&h, VERSION_HASH_KIND);
  gshscene_color_hash(&h, &version->foreground);
  gshgeom_hash_u64(&h, version->palette ? gshscene_resource_hash(version->palette) : 0);
  gshgeom_hash_u64(&h, (uint64_t)version->count);
  for (size_t i = 0; i < version->count; i++) {
    gshscene_node_hash(&h, version->nodes[i]);
  }
  return gshgeom_hash_value(&h);
}

/* Write "-" or the key as two 16-digit numbers. */
static int put_key(FILE * out, const GSHSCENE_Node_Rec * node) {
  if (!node->has_key) {
    return fputs(" key=-", out) < 0 ? -1 : 0;
  }
  return fprintf(out, " key=%016llx:%016llx", HEX(node->key.domain), HEX(node->key.value)) < 0 ? -1 : 0;
}

static int put_common(FILE * out, char letter, const GSHSCENE_Node_Rec * node) {
  if (fprintf(out, "%c id=%llu parent=%llu flags=%u", letter, HEX(node->id), HEX(node->parent),
          (unsigned)node->p.flags) < 0) {
    return -1;
  }
  return put_key(out, node);
}

static int put_node(FILE * out, const GSHSCENE_Node_Rec * node) {
  const GSHSCENE_Node * p = &node->p;
  switch (p->kind) {
    case GSHSCENE_NODE_GROUP: {
      if (put_common(out, 'G', node) < 0 ||
          fprintf(out, " map=%016llx opacity=%016llx blend=", HEX(gshgeom_map_hash(&p->map)),
              HEX(gshgeom_f64_bits(p->opacity))) < 0 ||
          gshscene_space_dump(out, &p->blend_space) < 0 ||
          fprintf(out, " composite=%u order=%u", (unsigned)p->composite, (unsigned)p->ordering) < 0 ||
          gshscene_group_dump_pdf(out, p->isolation, p->knockout, p->soft_mask) < 0 ||
          fprintf(out, " layer=%lld depth=%016llx", (long long)p->layer, HEX(gshgeom_f64_bits(p->depth))) < 0) {
        return -1;
      }
      if (p->clip) {
        if (fprintf(out, " clip=%s:%016llx:%016llx:%u", gshscene_resource_kind_string(gshscene_resource_kind(p->clip)),
                HEX(gshscene_resource_hash(p->clip)), HEX(gshgeom_map_hash(&p->clip_map)),
                (unsigned)p->clip_rule) < 0) {
          return -1;
        }
      } else if (fputs(" clip=-", out) < 0) {
        return -1;
      }
      if (fputs(" children=", out) < 0) {
        return -1;
      }
      if (node->child_count == 0) {
        return fputc('-', out) == EOF ? -1 : 0;
      }
      for (size_t i = 0; i < node->child_count; i++) {
        if (fprintf(out, i ? ",%llu" : "%llu", HEX(node->children[i])) < 0) {
          return -1;
        }
      }
      return 0;
    }
    case GSHSCENE_NODE_SHAPE:
      if (put_common(out, 'S', node) < 0 ||
          fprintf(out, " res=%s:%016llx map=%016llx rule=%u paint=%u %016llx %016llx %016llx %016llx space=",
              gshscene_resource_kind_string(gshscene_resource_kind(p->resource)),
              HEX(gshscene_resource_hash(p->resource)), HEX(gshgeom_map_hash(&p->map)), (unsigned)p->fill_rule,
              (unsigned)p->paint.kind, HEX(gshgeom_f64_bits(p->paint.rgba[0])), HEX(gshgeom_f64_bits(p->paint.rgba[1])),
              HEX(gshgeom_f64_bits(p->paint.rgba[2])), HEX(gshgeom_f64_bits(p->paint.rgba[3]))) < 0 ||
          gshscene_space_dump(out, &p->paint.space) < 0 ||
          fprintf(out, " entry=%u gradient=%016llx layer=%lld depth=%016llx", (unsigned)p->paint.entry,
              HEX(p->paint.gradient ? gshscene_resource_hash(p->paint.gradient) : 0), (long long)p->layer,
              HEX(gshgeom_f64_bits(p->depth))) < 0) {
        return -1;
      }
      return 0;
    default:
      if (put_common(out, 'I', node) < 0 ||
          fprintf(out, " target=%llu map=%016llx layer=%lld depth=%016llx", HEX(p->target),
              HEX(gshgeom_map_hash(&p->map)), (long long)p->layer, HEX(gshgeom_f64_bits(p->depth))) < 0) {
        return -1;
      }
      return 0;
  }
}

GSHSCENE_Result gshscene_version_dump(const GSHSCENE_Version * version, FILE * out) {
  if (!version || !out) {
    return GSHSCENE_ERR_INVALID;
  }
  const GSHSCENE_Color * fg = &version->foreground;
  if (fprintf(out, "gshscene.version %u nodes=%llu root=%llu fg=%016llx %016llx %016llx %016llx ", VERSION_FORMAT_VERSION,
          HEX(version->count), HEX(GSHSCENE_ROOT_ID), HEX(gshgeom_f64_bits(fg->rgba[0])),
          HEX(gshgeom_f64_bits(fg->rgba[1])), HEX(gshgeom_f64_bits(fg->rgba[2])), HEX(gshgeom_f64_bits(fg->rgba[3]))) < 0 ||
      gshscene_space_dump(out, &fg->space) < 0 ||
      fprintf(out, " palette=%016llx hash=%016llx\n",
          HEX(version->palette ? gshscene_resource_hash(version->palette) : 0), HEX(gshscene_version_hash(version))) < 0) {
    return GSHSCENE_ERR_IO;
  }
  for (size_t i = 0; i < version->count; i++) {
    if (put_node(out, version->nodes[i]) < 0 || fputc('\n', out) == EOF) {
      return GSHSCENE_ERR_IO;
    }
  }
  /* A buffered stream reports a write error at the flush, and a stream's error flag is sticky:
   * a failed write whose return value was lost (an unbuffered stream's fprintf) still sets it. */
  return (fflush(out) == 0 && !ferror(out)) ? GSHSCENE_OK : GSHSCENE_ERR_IO;
}

/* --- instance paths ------------------------------------------------------------------------------ */

int gshscene_instance_path_compare(const uint64_t * a, size_t a_count, const uint64_t * b, size_t b_count) {
  size_t n = a_count < b_count ? a_count : b_count;
  for (size_t i = 0; i < n; i++) {
    if (a[i] != b[i]) {
      return a[i] < b[i] ? -1 : 1;
    }
  }
  if (a_count != b_count) {
    return a_count < b_count ? -1 : 1;
  }
  return 0;
}

uint64_t gshscene_instance_path_key(const uint64_t * path, size_t count) {
  GSHGEOM_Hash h;
  gshgeom_hash_init(&h);
  gshgeom_hash_u64(&h, (uint64_t)count);
  for (size_t i = 0; i < count; i++) {
    gshgeom_hash_u64(&h, path[i]);
  }
  return gshgeom_hash_value(&h);
}

/* --- the visitor ------------------------------------------------------------------------------------ */

/* A result no walk callback returns otherwise: the visitor asked to stop. */
#define WALK_STOP ((GSHSCENE_Result)1000)

typedef struct Visitor {
  GSHSCENE_Visit_Fn fn;
  void * ctx;
  int stopped_with;
} Visitor;

static GSHSCENE_Result visit_one(
    Visitor * v, const GSHSCENE_Walk * walk, const GSHSCENE_Node_Rec * node, const GSHGEOM_Map * item_map) {
  GSHSCENE_Visit visit;
  visit.node = node->id;
  walk->path[walk->path_count] = node->id;
  visit.path = walk->path;
  visit.path_count = walk->path_count + 1;
  visit.kind = node->p.kind;
  visit.props = &node->p;
  GSHSCENE_Result result = gshscene_walk_compose(walk, item_map, NULL, &visit.map);
  if (result != GSHSCENE_OK) {
    return result;
  }
  int stop = v->fn(v->ctx, &visit);
  if (stop != 0) {
    v->stopped_with = stop;
    return WALK_STOP;
  }
  return GSHSCENE_OK;
}

static GSHSCENE_Result visit_group_enter(
    void * ctx, const GSHSCENE_Walk * walk, const GSHSCENE_Node_Rec * group, const GSHGEOM_Map * base) {
  return visit_one((Visitor *)ctx, walk, group, base);
}

static GSHSCENE_Result visit_shape(
    void * ctx, const GSHSCENE_Walk * walk, const GSHSCENE_Node_Rec * shape, const GSHGEOM_Map * map) {
  return visit_one((Visitor *)ctx, walk, shape, map);
}

static GSHSCENE_Result visit_instance(void * ctx, const GSHSCENE_Walk * walk, const GSHSCENE_Node_Rec * instance,
    const GSHGEOM_Map * map, int * descend) {
  *descend = 1;
  return visit_one((Visitor *)ctx, walk, instance, map);
}

int gshscene_version_visit(const GSHSCENE_Version * version, GSHSCENE_Visit_Fn fn, void * ctx) {
  if (!version || !fn) {
    return -1;
  }
  static const GSHSCENE_Walk_Ops ops = {visit_group_enter, NULL, visit_shape, visit_instance, NULL, NULL, NULL};
  Visitor visitor = {fn, ctx, 0};
  GSHGEOM_Map identity;
  gshgeom_map_identity(2, &identity);
  const GSHSCENE_Node_Rec * root = gshscene_version_find(version, GSHSCENE_ROOT_ID);
  if (!root) {
    return -1;
  }
  GSHSCENE_Result result = gshscene_walk(version, root, 0, &identity, &ops, &visitor);
  if (result == WALK_STOP) {
    return visitor.stopped_with;
  }
  return result == GSHSCENE_OK ? 0 : -1;
}
