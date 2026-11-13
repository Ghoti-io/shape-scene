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
 * The clip union (clip_union.h, CU6 to CU9, CU14, CU16, CU17): the resource, its canonical form, hash, equality and dump, the
 * member item, and the one disjointness predicate.
 */

#include <ghoti.io/shape-scene/macros.h>

#include "../internal.h"

#include <ghoti.io/shape-geometry/device.h>

#include <stdlib.h>
#include <string.h>

/** The format of a clip union's hash and dump (clip_union.h). */
#define CLIP_UNION_FORMAT_VERSION 1u

#define HEX(x) ((unsigned long long)(x))

/* --- order and equality of members (CU8) -------------------------------------------------- */

/** The order of two canonical maps by their bits: dimension, kind, then the entries a map of that dimension uses. */
static int map_bits_cmp(const GSHGEOM_Map * a, const GSHGEOM_Map * b) {
  if (a->dim != b->dim) {
    return a->dim < b->dim ? -1 : 1;
  }
  if (a->kind != b->kind) {
    return a->kind < b->kind ? -1 : 1;
  }
  for (int i = 0; i < a->dim; i++) {
    for (int j = 0; j < a->dim; j++) {
      uint64_t x = gshgeom_f64_bits(a->m[i][j]);
      uint64_t y = gshgeom_f64_bits(b->m[i][j]);
      if (x != y) {
        return x < y ? -1 : 1;
      }
    }
    uint64_t x = gshgeom_f64_bits(a->t[i]);
    uint64_t y = gshgeom_f64_bits(b->t[i]);
    if (x != y) {
      return x < y ? -1 : 1;
    }
  }
  return 0;
}

/** A member with the keys the order is made of, computed once. */
typedef struct Keyed {
  GSHSCENE_Clip_Member m;   /* `inner` points into the canonical inner clips of the intern (CI6) */
  uint64_t res_hash;
  uint64_t map_hash;
  uint64_t inner_hash;      /* 0 for a plain member; else the hash of the canonical inner clips' keys */
} Keyed;

/** An inner clip with the keys its order is made of. */
typedef struct Ik {
  GSHSCENE_Clip_Inner in;
  uint64_t res_hash;
  uint64_t map_hash;
} Ik;

/** CI6: ascending by (resource hash, map hash, rule); only on a full tie, by the resource id and the map's bits. */
static int ik_cmp(const void * pa, const void * pb) {
  const Ik * a = (const Ik *)pa;
  const Ik * b = (const Ik *)pb;
  if (a->res_hash != b->res_hash) {
    return a->res_hash < b->res_hash ? -1 : 1;
  }
  if (a->map_hash != b->map_hash) {
    return a->map_hash < b->map_hash ? -1 : 1;
  }
  if (a->in.rule != b->in.rule) {
    return (int)a->in.rule < (int)b->in.rule ? -1 : 1;
  }
  uint64_t ia = gshscene_resource_id(a->in.resource);
  uint64_t ib = gshscene_resource_id(b->in.resource);
  if (ia != ib) {
    return ia < ib ? -1 : 1;
  }
  return map_bits_cmp(&a->in.map, &b->in.map);
}

static int inner_same(const GSHSCENE_Clip_Inner * a, const GSHSCENE_Clip_Inner * b) {
  return a->resource == b->resource && a->rule == b->rule && map_bits_cmp(&a->map, &b->map) == 0;
}

static int inners_same(const GSHSCENE_Clip_Inner * a, size_t an, const GSHSCENE_Clip_Inner * b, size_t bn) {
  if (an != bn) {
    return 0;
  }
  for (size_t i = 0; i < an; i++) {
    if (!inner_same(&a[i], &b[i])) {
      return 0;
    }
  }
  return 1;
}

/** The order of two canonical inner sets that the hashes did not tell apart: the count, then each inner clip's id and map bits and rule. */
static int inners_cmp(const GSHSCENE_Clip_Inner * a, size_t an, const GSHSCENE_Clip_Inner * b, size_t bn) {
  if (an != bn) {
    return an < bn ? -1 : 1;
  }
  for (size_t i = 0; i < an; i++) {
    uint64_t x = gshscene_resource_id(a[i].resource);
    uint64_t y = gshscene_resource_id(b[i].resource);
    if (x != y) {
      return x < y ? -1 : 1;
    }
    if (a[i].rule != b[i].rule) {
      return (int)a[i].rule < (int)b[i].rule ? -1 : 1;
    }
    int c = map_bits_cmp(&a[i].map, &b[i].map);
    if (c) {
      return c;
    }
  }
  return 0;
}

/** CU8, CI6: ascending by (resource hash, map hash, rule, inner clips' hash); only on a full tie, by the resource id, the map's bits and the inner clips. */
static int keyed_cmp(const void * pa, const void * pb) {
  const Keyed * a = (const Keyed *)pa;
  const Keyed * b = (const Keyed *)pb;
  if (a->res_hash != b->res_hash) {
    return a->res_hash < b->res_hash ? -1 : 1;
  }
  if (a->map_hash != b->map_hash) {
    return a->map_hash < b->map_hash ? -1 : 1;
  }
  if (a->m.rule != b->m.rule) {
    return (int)a->m.rule < (int)b->m.rule ? -1 : 1;
  }
  if (a->inner_hash != b->inner_hash) {
    return a->inner_hash < b->inner_hash ? -1 : 1;
  }
  uint64_t ia = gshscene_resource_id(a->m.resource);
  uint64_t ib = gshscene_resource_id(b->m.resource);
  if (ia != ib) {
    return ia < ib ? -1 : 1;
  }
  int c = map_bits_cmp(&a->m.map, &b->m.map);
  if (c) {
    return c;
  }
  return inners_cmp(a->m.inner, a->m.inner_count, b->m.inner, b->m.inner_count);
}

/** CU8, CI6: the same resource handle, bit-equal maps, the same rule and the same inner clips. */
static int member_same(const GSHSCENE_Clip_Member * a, const GSHSCENE_Clip_Member * b) {
  return a->resource == b->resource && a->rule == b->rule && map_bits_cmp(&a->map, &b->map) == 0 &&
      inners_same(a->inner, a->inner_count, b->inner, b->inner_count);
}

/** What an intern carries: the canonical members, in place. */
typedef struct Source {
  Keyed * keyed; /* count of them, sorted and distinct */
  size_t count;
  size_t inner_total; /* the inner clips of the distinct members */
} Source;

/* --- hash (CU9, CI6) ---------------------------------------------------------------------- */

/** One inner clip's three fields, as the member's hash and the inner hash take them. */
static void hash_inner(GSHGEOM_Hash * h, const GSHSCENE_Clip_Inner * in) {
  gshgeom_hash_u64(h, gshscene_resource_hash(in->resource));
  gshgeom_hash_u64(h, gshgeom_map_hash(&in->map));
  gshgeom_hash_u8(h, (uint8_t)in->rule);
}

/** The hash of a canonical inner set, the key a member sorts by after its own (CI6). */
static uint64_t inner_set_hash(const GSHSCENE_Clip_Inner * inner, size_t n) {
  if (n == 0) {
    return 0;
  }
  GSHGEOM_Hash h;
  gshgeom_hash_init(&h);
  gshgeom_hash_u64(&h, (uint64_t)n);
  for (size_t i = 0; i < n; i++) {
    hash_inner(&h, &inner[i]);
  }
  return gshgeom_hash_value(&h);
}

static uint64_t content_hash(const Keyed * keyed, size_t count) {
  GSHGEOM_Hash h;
  gshgeom_hash_init(&h);
  gshgeom_hash_u32(&h, CLIP_UNION_FORMAT_VERSION);
  gshgeom_hash_u32(&h, (uint32_t)GSHSCENE_RESOURCE_CLIP_UNION);
  gshgeom_hash_u64(&h, (uint64_t)count);
  for (size_t i = 0; i < count; i++) {
    gshgeom_hash_u64(&h, keyed[i].res_hash);
    gshgeom_hash_u64(&h, keyed[i].map_hash);
    if (keyed[i].m.inner_count == 0) {
      gshgeom_hash_u8(&h, (uint8_t)keyed[i].m.rule); /* a plain member: as before the inner clips existed */
    } else {
      gshgeom_hash_u8(&h, (uint8_t)((uint8_t)keyed[i].m.rule | 0x80u));
      gshgeom_hash_u64(&h, (uint64_t)keyed[i].m.inner_count);
      for (size_t k = 0; k < keyed[i].m.inner_count; k++) {
        hash_inner(&h, &keyed[i].m.inner[k]);
      }
    }
  }
  return gshgeom_hash_value(&h);
}

int gshscene_clip_union_matches(const GSHSCENE_Resource * resource, const void * source) {
  const Source * s = (const Source *)source;
  if (resource->u.clipunion.count != s->count) {
    return 0;
  }
  for (size_t i = 0; i < s->count; i++) {
    if (!member_same(&resource->u.clipunion.members[i], &s->keyed[i].m)) {
      return 0;
    }
  }
  return 1;
}

/* --- build, free -------------------------------------------------------------------------- */

static GSHSCENE_Result build(GSHSCENE_Store * store, const void * source, GSHSCENE_Resource ** out) {
  const Source * s = (const Source *)source;
  GSHSCENE_Clip_Member * members = gshscene_calloc(&store->alloc, s->count, sizeof *members);
  if (!members) {
    return GSHSCENE_ERR_OOM;
  }
  GSHSCENE_Clip_Inner * inners = NULL;
  if (s->inner_total) {
    inners = gshscene_calloc(&store->alloc, s->inner_total, sizeof *inners);
    if (!inners) {
      gshscene_free(&store->alloc, members);
      return GSHSCENE_ERR_OOM;
    }
  }
  GSHSCENE_Resource * resource = gshscene_store_new_resource(store, GSHSCENE_RESOURCE_CLIP_UNION);
  if (!resource) {
    gshscene_free(&store->alloc, inners);
    gshscene_free(&store->alloc, members);
    return GSHSCENE_ERR_OOM;
  }
  size_t at = 0;
  for (size_t i = 0; i < s->count; i++) {
    members[i] = s->keyed[i].m;
    gshscene_resource_retain(members[i].resource);
    if (members[i].inner_count) {
      for (size_t k = 0; k < members[i].inner_count; k++) {
        inners[at + k] = members[i].inner[k];
        gshscene_resource_retain(inners[at + k].resource);
      }
      members[i].inner = &inners[at];
      at += members[i].inner_count;
    } else {
      members[i].inner = NULL;
    }
  }
  resource->u.clipunion.members = members;
  resource->u.clipunion.count = s->count;
  resource->u.clipunion.inners = inners;
  resource->u.clipunion.inner_total = s->inner_total;
  *out = resource;
  return GSHSCENE_OK;
}

void gshscene_clip_union_free(GSHSCENE_Resource * resource) {
  for (size_t i = 0; i < resource->u.clipunion.count; i++) {
    gshscene_resource_release(resource->u.clipunion.members[i].resource);
  }
  for (size_t i = 0; i < resource->u.clipunion.inner_total; i++) {
    gshscene_resource_release(resource->u.clipunion.inners[i].resource);
  }
  gshscene_free(&resource->store->alloc, resource->u.clipunion.inners);
  gshscene_free(&resource->store->alloc, resource->u.clipunion.members);
}

/* --- intern (CU7, CU8, CI6) --------------------------------------------------------------- */

/** The canonical map of a member: a zero-filled one is the 2D identity; else valid, and only the entries its dimension uses. */
static GSHSCENE_Result member_map(const GSHGEOM_Map * in, GSHGEOM_Map * out) {
  if (in->dim == 0 && in->kind == 0) {
    return gshgeom_map_identity(2, out) == GSHGEOM_OK ? GSHSCENE_OK : GSHSCENE_ERR_INTERNAL;
  }
  return gshscene_list_canonical_map(in, out);
}

/** The checks one region of a union gets, member or inner clip (CU7): `ERR_INVALID` and, in @p unsupported, the unsupported ones. */
static GSHSCENE_Result region_check(const GSHSCENE_Store * store, const GSHSCENE_Resource * resource, const GSHGEOM_Map * in_map,
    GSHGEOM_Fill_Rule rule, int * unsupported) {
  if (!resource || resource->store != store ||
      (resource->kind != GSHSCENE_RESOURCE_PATH && resource->kind != GSHSCENE_RESOURCE_GLYPH_RUN)) {
    return GSHSCENE_ERR_INVALID;
  }
  GSHGEOM_Map map;
  if (member_map(in_map, &map) != GSHSCENE_OK || (unsigned)rule >= (unsigned)GSHGEOM_FILL_RULE_COUNT) {
    return GSHSCENE_ERR_INVALID;
  }
  if (map.dim != 2 || (resource->kind == GSHSCENE_RESOURCE_GLYPH_RUN && resource->u.glyphs.run.source == GSHSCENE_GLYPHS_FROM_FONT)) {
    *unsupported = 1;
  }
  return GSHSCENE_OK;
}

GSHSCENE_Result gshscene_store_intern_clip_union(GSHSCENE_Store * store, const GSHSCENE_Clip_Member * members, size_t count,
    const GSHSCENE_Resource ** out) {
  if (!store || !members || !out || count == 0) {
    return GSHSCENE_ERR_INVALID;
  }
  /* CU7, in order: every validity check over every member and every inner clip first ... */
  int unsupported = 0;
  size_t inner_given = 0;
  for (size_t i = 0; i < count; i++) {
    const GSHSCENE_Clip_Member * m = &members[i];
    GSHSCENE_Result r = region_check(store, m->resource, &m->map, m->rule, &unsupported);
    if (r != GSHSCENE_OK) {
      return r;
    }
    if (m->inner_count && !m->inner) {
      return GSHSCENE_ERR_INVALID;
    }
    for (size_t k = 0; k < m->inner_count; k++) {
      r = region_check(store, m->inner[k].resource, &m->inner[k].map, m->inner[k].rule, &unsupported);
      if (r != GSHSCENE_OK) {
        return r;
      }
    }
    if (m->inner_count > SIZE_MAX - inner_given) {
      return GSHSCENE_ERR_LIMIT;
    }
    inner_given += m->inner_count;
  }
  /* ... then the unsupported ones, then the limit (members and inner clips together, CI6), then memory. */
  if (unsupported) {
    return GSHSCENE_ERR_UNSUPPORTED;
  }
  if (count > store->limits.max_clip_members || inner_given > store->limits.max_clip_members - count) {
    return GSHSCENE_ERR_LIMIT;
  }
  Keyed * keyed = gshscene_calloc(&store->alloc, count, sizeof *keyed);
  Ik * ik = inner_given ? gshscene_calloc(&store->alloc, inner_given, sizeof *ik) : NULL;
  GSHSCENE_Clip_Inner * canon = inner_given ? gshscene_calloc(&store->alloc, inner_given, sizeof *canon) : NULL;
  if (!keyed || (inner_given && (!ik || !canon))) {
    gshscene_free(&store->alloc, canon);
    gshscene_free(&store->alloc, ik);
    gshscene_free(&store->alloc, keyed);
    return GSHSCENE_ERR_OOM;
  }
  size_t off = 0;
  for (size_t i = 0; i < count; i++) {
    keyed[i].m = members[i];
    (void)member_map(&members[i].map, &keyed[i].m.map);
    keyed[i].res_hash = gshscene_resource_hash(members[i].resource);
    keyed[i].map_hash = gshgeom_map_hash(&keyed[i].m.map);
    /* CI6: the member's inner clips, sorted and made distinct, in this member's stretch of the canonical array. */
    const size_t n = members[i].inner_count;
    for (size_t k = 0; k < n; k++) {
      ik[off + k].in = members[i].inner[k];
      (void)member_map(&members[i].inner[k].map, &ik[off + k].in.map);
      ik[off + k].res_hash = gshscene_resource_hash(members[i].inner[k].resource);
      ik[off + k].map_hash = gshgeom_map_hash(&ik[off + k].in.map);
    }
    if (n) {
      qsort(&ik[off], n, sizeof *ik, ik_cmp);
    }
    size_t distinct = 0;
    for (size_t k = 0; k < n; k++) {
      if (distinct > 0 && inner_same(&canon[off + distinct - 1], &ik[off + k].in)) {
        continue;
      }
      canon[off + distinct++] = ik[off + k].in;
    }
    keyed[i].m.inner_count = distinct;
    keyed[i].m.inner = distinct ? &canon[off] : NULL;
    keyed[i].inner_hash = inner_set_hash(keyed[i].m.inner, distinct);
    off += n;
  }
  qsort(keyed, count, sizeof *keyed, keyed_cmp);
  size_t distinct = 0;
  size_t inner_total = 0;
  for (size_t i = 0; i < count; i++) {
    if (distinct > 0 && member_same(&keyed[distinct - 1].m, &keyed[i].m)) {
      continue;
    }
    keyed[distinct++] = keyed[i];
    inner_total += keyed[i].m.inner_count;
  }
  Source s = {keyed, distinct, inner_total};
  GSHSCENE_Probe probe = {GSHSCENE_RESOURCE_CLIP_UNION, content_hash(keyed, distinct),
      GSHSCENE_BYTES_BASE + distinct * GSHSCENE_BYTES_PER_CLIP_MEMBER + inner_total * GSHSCENE_BYTES_PER_CLIP_INNER, &s, build};
  GSHSCENE_Result result = gshscene_store_intern(store, &probe, out);
  gshscene_free(&store->alloc, canon);
  gshscene_free(&store->alloc, ik);
  gshscene_free(&store->alloc, keyed);
  return result;
}

size_t gshscene_resource_clip_union_count(const GSHSCENE_Resource * resource) {
  return resource && resource->kind == GSHSCENE_RESOURCE_CLIP_UNION ? resource->u.clipunion.count : 0;
}

GSHSCENE_Result gshscene_resource_clip_union_member(const GSHSCENE_Resource * resource, size_t index,
    GSHSCENE_Clip_Member * out) {
  if (!resource || !out || resource->kind != GSHSCENE_RESOURCE_CLIP_UNION || index >= resource->u.clipunion.count) {
    return GSHSCENE_ERR_INVALID;
  }
  *out = resource->u.clipunion.members[index];
  return GSHSCENE_OK;
}

/* --- the dump (CU9) ----------------------------------------------------------------------- */

int gshscene_clip_union_dump(const GSHSCENE_Resource * resource, FILE * out) {
  if (fprintf(out, "members %llu\n", HEX(resource->u.clipunion.count)) < 0) {
    return -1;
  }
  for (size_t i = 0; i < resource->u.clipunion.count; i++) {
    const GSHSCENE_Clip_Member * m = &resource->u.clipunion.members[i];
    if (fprintf(out, "m %llu res=%s:%016llx map=%016llx rule=%u", HEX(i), gshscene_resource_kind_string(m->resource->kind),
            HEX(gshscene_resource_hash(m->resource)), HEX(gshgeom_map_hash(&m->map)), (unsigned)m->rule) < 0) {
      return -1;
    }
    if (m->inner_count && fprintf(out, " inner=%llu", HEX(m->inner_count)) < 0) {
      return -1;
    }
    if (fprintf(out, "\n") < 0) {
      return -1;
    }
    for (size_t k = 0; k < m->inner_count; k++) {
      const GSHSCENE_Clip_Inner * in = &m->inner[k];
      if (fprintf(out, "i %llu %llu res=%s:%016llx map=%016llx rule=%u\n", HEX(i), HEX(k),
              gshscene_resource_kind_string(in->resource->kind), HEX(gshscene_resource_hash(in->resource)),
              HEX(gshgeom_map_hash(&in->map)), (unsigned)in->rule) < 0) {
        return -1;
      }
    }
  }
  return 0;
}

/* --- the member item (CU14) --------------------------------------------------------------- */

GSHSCENE_Result gshscene_clip_union_member_item(const GSHSCENE_Item * item, size_t index, GSHSCENE_Item * out) {
  if (!item || !out || item->kind != GSHSCENE_ITEM_CLIP_PUSH || !item->resource ||
      item->resource->kind != GSHSCENE_RESOURCE_CLIP_UNION || index >= item->resource->u.clipunion.count) {
    return GSHSCENE_ERR_INVALID;
  }
  const GSHSCENE_Clip_Member * m = &item->resource->u.clipunion.members[index];
  GSHGEOM_Map map;
  /* The composition is pairwise, the item's map after the member's, once.  An identity item map gives the member's bit for bit:
   * the call copies it rather than relying on compose to reproduce it. */
  if (item->map.dim == 2 && item->map.kind == 0) {
    map = m->map;
  } else if (gshgeom_map_compose(&item->map, &m->map, &map) != GSHGEOM_OK) {
    return GSHSCENE_ERR_LIMIT;
  }
  GSHSCENE_Item result = *item;
  result.kind = GSHSCENE_ITEM_CLIP_PUSH;
  result.resource = m->resource;
  result.map = map;
  result.fill_rule = m->rule;
  *out = result;
  return GSHSCENE_OK;
}

GSHSCENE_Result gshscene_clip_union_inner_item(const GSHSCENE_Item * item, size_t member, size_t inner, GSHSCENE_Item * out) {
  if (!item || !out || item->kind != GSHSCENE_ITEM_CLIP_PUSH || !item->resource ||
      item->resource->kind != GSHSCENE_RESOURCE_CLIP_UNION || member >= item->resource->u.clipunion.count ||
      inner >= item->resource->u.clipunion.members[member].inner_count) {
    return GSHSCENE_ERR_INVALID;
  }
  const GSHSCENE_Clip_Inner * in = &item->resource->u.clipunion.members[member].inner[inner];
  GSHGEOM_Map map;
  if (item->map.dim == 2 && item->map.kind == 0) {
    map = in->map;
  } else if (gshgeom_map_compose(&item->map, &in->map, &map) != GSHGEOM_OK) {
    return GSHSCENE_ERR_LIMIT;
  }
  GSHSCENE_Item result = *item;
  result.kind = GSHSCENE_ITEM_CLIP_PUSH;
  result.resource = in->resource;
  result.map = map;
  result.fill_rule = in->rule;
  *out = result;
  return GSHSCENE_OK;
}

/* --- the predicate (CU16, CU17) ----------------------------------------------------------- */

/** A member's box, or why there is none. */
typedef struct Box {
  int32_t x0, y0, x1, y1;
  uint8_t state; /* 0 empty (disjoint from everything), 1 a box, 2 unprovable */
} Box;

/** `target . instances . chain`, one composition at a time and in the order gshgeom_map_compose_chain forms it. */
static GSHGEOM_Result chain_map(const GSHGEOM_Map * instance_maps, size_t instance_count, const GSHGEOM_Map * a,
    const GSHGEOM_Map * b, const GSHGEOM_Map * target, GSHGEOM_Map * out) {
  GSHGEOM_Map acc = instance_count ? instance_maps[0] : *a;
  GSHGEOM_Result r = gshgeom_map_check(&acc);
  for (size_t i = 1; r == GSHGEOM_OK && i <= instance_count; i++) {
    r = gshgeom_map_compose(&acc, i < instance_count ? &instance_maps[i] : a, &acc);
  }
  if (r == GSHGEOM_OK && instance_count == 0) {
    /* acc is already *a */
  }
  if (r == GSHGEOM_OK && b) {
    r = gshgeom_map_compose(&acc, b, &acc);
  }
  if (r == GSHGEOM_OK && target && target->dim != 0) {
    r = gshgeom_map_compose(target, &acc, &acc);
  }
  if (r == GSHGEOM_OK) {
    *out = acc;
  }
  return r;
}

static void box_add(Box * box, const GSHGEOM_Device_Box * b) {
  if (b->empty) {
    return;
  }
  if (box->state == 0) {
    box->x0 = b->x0;
    box->y0 = b->y0;
    box->x1 = b->x1;
    box->y1 = b->y1;
    box->state = 1;
    return;
  }
  box->x0 = b->x0 < box->x0 ? b->x0 : box->x0;
  box->y0 = b->y0 < box->y0 ? b->y0 : box->y0;
  box->x1 = b->x1 > box->x1 ? b->x1 : box->x1;
  box->y1 = b->y1 > box->y1 ? b->y1 : box->y1;
}

/** The box of the device path the sink draws for the member item @p m (a plain clip item), or an unprovable state. */
static Box member_box(const GSHSCENE_Item * m, const GSHGEOM_Map * instance_maps, size_t instance_count,
    const GSHGEOM_Map * target_map) {
  Box box = {0, 0, 0, 0, 0};
  GSHGEOM_Device_Box db;
  const GSHGEOM_Path * path = gshscene_resource_path(m->resource);
  if (path) {
    GSHGEOM_Map map;
    if (chain_map(instance_maps, instance_count, &m->map, NULL, target_map, &map) != GSHGEOM_OK ||
        gshgeom_device_box(path, &map, &db) != GSHGEOM_OK) {
      box.state = 2;
      return box;
    }
    box_add(&box, &db);
    return box;
  }
  GSHSCENE_Glyph_Run run;
  if (gshscene_resource_glyph_run(m->resource, &run) != GSHSCENE_OK || run.source != GSHSCENE_GLYPHS_FROM_OUTLINES) {
    box.state = 2;
    return box;
  }
  /* CU16: the hull of the glyphs' device boxes, each outline under the item's map after the translation to the glyph's origin,
   * as the sink composes it (the instances, the item's map, the translation, then the target). */
  for (size_t i = 0; i < run.glyph_count; i++) {
    const GSHGEOM_Path * outline = gshscene_resource_path(run.glyphs[i].outline);
    if (!outline) {
      continue; /* a glyph with no outline draws nothing */
    }
    GSHGEOM_Map t, map;
    if (gshgeom_map_translate(2, run.glyphs[i].x, run.glyphs[i].y, 0.0, &t) != GSHGEOM_OK ||
        chain_map(instance_maps, instance_count, &m->map, &t, target_map, &map) != GSHGEOM_OK ||
        gshgeom_device_box(outline, &map, &db) != GSHGEOM_OK) {
      box.state = 2;
      return box;
    }
    box_add(&box, &db);
  }
  return box;
}

/**
 * The box of member @p index of @p item (CI3): the box of its own contours, met with the box of each inner clip whose box can be formed
 * (an inner clip that cannot be bounded leaves the box larger, which is sound); unprovable when the member's own box is.
 */
static Box full_member_box(const GSHSCENE_Item * item, size_t index, const GSHGEOM_Map * instance_maps, size_t instance_count,
    const GSHGEOM_Map * target_map) {
  Box box = {0, 0, 0, 0, 2};
  GSHSCENE_Item m;
  if (gshscene_clip_union_member_item(item, index, &m) != GSHSCENE_OK) {
    return box;
  }
  box = member_box(&m, instance_maps, instance_count, target_map);
  const size_t inner = item->resource->u.clipunion.members[index].inner_count;
  for (size_t k = 0; k < inner && box.state == 1; k++) {
    GSHSCENE_Item im;
    if (gshscene_clip_union_inner_item(item, index, k, &im) != GSHSCENE_OK) {
      continue;
    }
    Box ib = member_box(&im, instance_maps, instance_count, target_map);
    if (ib.state == 2) {
      continue;
    }
    if (ib.state == 0) {
      box.state = 0; /* the region lies in an empty box: it is empty */
      break;
    }
    const int32_t x0 = box.x0 > ib.x0 ? box.x0 : ib.x0;
    const int32_t y0 = box.y0 > ib.y0 ? box.y0 : ib.y0;
    const int32_t x1 = box.x1 < ib.x1 ? box.x1 : ib.x1;
    const int32_t y1 = box.y1 < ib.y1 ? box.y1 : ib.y1;
    if (x0 > x1 || y0 > y1) {
      box.state = 0;
      break;
    }
    box.x0 = x0;
    box.y0 = y0;
    box.x1 = x1;
    box.y1 = y1;
  }
  return box;
}

/** CU16: the interiors of two boxes do not meet.  An empty box meets nothing. */
static int interiors_disjoint(const Box * a, const Box * b) {
  if (a->state == 0 || b->state == 0) {
    return 1;
  }
  int32_t lx = a->x0 > b->x0 ? a->x0 : b->x0;
  int32_t hx = a->x1 < b->x1 ? a->x1 : b->x1;
  int32_t ly = a->y0 > b->y0 ? a->y0 : b->y0;
  int32_t hy = a->y1 < b->y1 ? a->y1 : b->y1;
  return !(lx < hx && ly < hy);
}

GSHSCENE_Result gshscene_clip_union_disjoint(const GSHSCENE_Item * item, const GSHGEOM_Map * instance_maps,
    size_t instance_count, const GSHGEOM_Map * target_map, GSHSCENE_Union_Verdict * verdict) {
  if (!verdict || !item || item->kind != GSHSCENE_ITEM_CLIP_PUSH || !item->resource ||
      item->resource->kind != GSHSCENE_RESOURCE_CLIP_UNION || (instance_count && !instance_maps)) {
    return GSHSCENE_ERR_INVALID;
  }
  const size_t n = item->resource->u.clipunion.count;
  Box boxes[GSHSCENE_CLIP_MEMBERS_CEILING];
  verdict->disjoint = 1;
  verdict->tests = 0;
  verdict->first = 0;
  verdict->second = 0;
  if (n > GSHSCENE_CLIP_MEMBERS_CEILING) {
    return GSHSCENE_ERR_INTERNAL; /* the store never builds one */
  }
  for (size_t i = 0; i < n; i++) {
    boxes[i] = full_member_box(item, i, instance_maps, instance_count, target_map); /* an unprovable box: the composition left the bound, or the member cannot be bounded */
  }
  for (size_t i = 0; i < n; i++) {
    if (boxes[i].state == 2 || item->resource->u.clipunion.members[i].inner_count > 0) { /* CI4: a clipped member is never shown disjoint */
      verdict->disjoint = 0;
      verdict->first = i;
      verdict->second = i;
      return GSHSCENE_OK;
    }
    for (size_t j = i + 1; j < n; j++) {
      if (boxes[j].state == 2) {
        continue; /* its own turn comes, as (j, j) */
      }
      verdict->tests++;
      if (!interiors_disjoint(&boxes[i], &boxes[j])) {
        verdict->disjoint = 0;
        verdict->first = i;
        verdict->second = j;
        return GSHSCENE_OK;
      }
    }
  }
  return GSHSCENE_OK;
}

GSHSCENE_Result gshscene_clip_union_member_box(const GSHSCENE_Item * item, size_t index, const GSHGEOM_Map * instance_maps,
    size_t instance_count, const GSHGEOM_Map * target_map, GSHGEOM_Device_Box * out) {
  if (!out || (instance_count && !instance_maps)) {
    return GSHSCENE_ERR_INVALID;
  }
  GSHSCENE_Item m;
  GSHSCENE_Result r = gshscene_clip_union_member_item(item, index, &m);
  if (r != GSHSCENE_OK) {
    return r;
  }
  Box box = full_member_box(item, index, instance_maps, instance_count, target_map);
  if (box.state == 2) {
    return GSHSCENE_ERR_LIMIT;
  }
  out->empty = box.state == 0;
  out->x0 = box.state ? box.x0 : 0;
  out->y0 = box.state ? box.y0 : 0;
  out->x1 = box.state ? box.x1 : 0;
  out->y1 = box.state ? box.y1 : 0;
  return GSHSCENE_OK;
}
