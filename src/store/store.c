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
 * The store: the table that interns resources, the reference counts, and the
 * release listeners (resource.h).
 *
 * The table is a chained hash table indexed by the resource's index hash and
 * guarded by one lock.  A reference count lives in the resource and is atomic;
 * a lookup takes a reference only from a nonzero count (so a resource whose
 * last reference is being dropped is never resurrected), and the thread whose
 * decrement reached zero is the only one that frees the resource.
 */

#include <ghoti.io/shape-scene/macros.h>

#include "../internal.h"

#include <stdlib.h>
#include <string.h>

#define INITIAL_BUCKETS 64u

/* --- the store's own count ------------------------------------------------- */

void gshscene_store_ref(GSHSCENE_Store * store) {
  gcu_atomic_size_fetch_add(&store->refs, 1);
}

static void listeners_unref(const GSHSCENE_Allocator * alloc, GSHSCENE_Listeners * listeners) {
  if (listeners && gcu_atomic_size_fetch_sub(&listeners->refs, 1) == 1) {
    gshscene_free(alloc, listeners);
  }
}

void gshscene_store_unref(GSHSCENE_Store * store) {
  if (gcu_atomic_size_fetch_sub(&store->refs, 1) != 1) {
    return;
  }
  GSHSCENE_Allocator alloc = store->alloc;
  listeners_unref(&alloc, store->listeners);
  gshscene_free(&alloc, store->buckets);
  GCU_MUTEX_DESTROY(store->lock);
  gshscene_free(&alloc, store);
}

/* --- create and destroy ---------------------------------------------------- */

GSHSCENE_Result gshscene_store_create_with_index_hash(const GSHSCENE_Allocator * alloc,
    const GSHSCENE_Limits * limits, GSHSCENE_Index_Hash_Fn index_hash, void * index_ctx,
    GSHSCENE_Store ** out) {
  if (!out) {
    return GSHSCENE_ERR_INVALID;
  }
  if (!alloc) {
    alloc = gshscene_allocator_default();
  }
  GSHSCENE_Store * store = gshscene_calloc(alloc, 1, sizeof *store);
  if (!store) {
    return GSHSCENE_ERR_OOM;
  }
  store->alloc = *alloc;
  gshscene_limits_effective(limits, &store->limits);
  store->index_fn = index_hash;
  store->index_ctx = index_ctx;
  gcu_atomic_size_init(&store->refs, 1);
  store->bucket_count = INITIAL_BUCKETS;
  store->buckets = gshscene_calloc(&store->alloc, store->bucket_count, sizeof *store->buckets);
  if (!store->buckets) {
    gshscene_free(&store->alloc, store);
    return GSHSCENE_ERR_OOM;
  }
  if (GCU_MUTEX_CREATE(store->lock) != 0) {
    gshscene_free(&store->alloc, store->buckets);
    gshscene_free(&store->alloc, store);
    return GSHSCENE_ERR_INTERNAL;
  }
  *out = store;
  return GSHSCENE_OK;
}

GSHSCENE_Result gshscene_store_create(
    const GSHSCENE_Allocator * alloc, const GSHSCENE_Limits * limits, GSHSCENE_Store ** out) {
  return gshscene_store_create_with_index_hash(alloc, limits, NULL, NULL, out);
}

void gshscene_store_destroy(GSHSCENE_Store * store) {
  if (store) {
    gshscene_store_unref(store);
  }
}

GSHSCENE_Result gshscene_store_stats(const GSHSCENE_Store * store, GSHSCENE_Store_Stats * out) {
  if (!store || !out) {
    return GSHSCENE_ERR_INVALID;
  }
  GSHSCENE_Store * mutable_store = (GSHSCENE_Store *)store;
  GCU_MUTEX_LOCK(mutable_store->lock);
  out->live = store->live;
  out->bytes = store->bytes;
  out->created = store->next_id;
  out->hits = store->hits;
  out->collisions = store->collisions;
  out->buckets = store->bucket_count;
  GCU_MUTEX_UNLOCK(mutable_store->lock);
  return GSHSCENE_OK;
}

/* --- resources ------------------------------------------------------------- */

GSHSCENE_Resource * gshscene_store_new_resource(GSHSCENE_Store * store, GSHSCENE_Resource_Kind kind) {
  GSHSCENE_Resource * resource = gshscene_calloc(&store->alloc, 1, sizeof *resource);
  if (!resource) {
    return NULL;
  }
  resource->store = store;
  resource->kind = kind;
  gcu_atomic_size_init(&resource->refs, 1);
  gshscene_store_ref(store);
  return resource;
}

void gshscene_store_discard_resource(GSHSCENE_Resource * resource) {
  GSHSCENE_Store * store = resource->store;
  GSHSCENE_Allocator alloc = store->alloc;
  gshscene_resource_free_content(resource);
  gshscene_free(&alloc, resource);
  gshscene_store_unref(store);
}

/* --- the table ------------------------------------------------------------- */

/* Take a reference on a resource found in the table, unless its count is
 * already zero: then its last release is in progress and it is as good as gone. */
static int try_retain(GSHSCENE_Resource * resource) {
  size_t count = gcu_atomic_size_load(&resource->refs);
  while (count != 0) {
    if (gcu_atomic_size_compare_exchange(&resource->refs, &count, count + 1)) {
      return 1;
    }
  }
  return 0;
}

/* The resource with this content, with a reference taken for the caller, or NULL.
 * Called with the lock held. */
static GSHSCENE_Resource * lookup(
    GSHSCENE_Store * store, const GSHSCENE_Probe * probe, uint64_t index) {
  for (GSHSCENE_Resource * r = store->buckets[index & (store->bucket_count - 1)]; r; r = r->next) {
    if (r->index != index) {
      continue;
    }
    if (r->kind == probe->kind && gshscene_resource_matches(r, probe)) {
      if (try_retain(r)) {
        return r;
      }
    } else {
      store->collisions++;
    }
  }
  return NULL;
}

/* Whether @p bytes more accounted bytes fit.  Called with the lock held. */
static int fits(const GSHSCENE_Store * store, size_t bytes) {
  return store->bytes <= store->limits.max_bytes && bytes <= store->limits.max_bytes - (size_t)store->bytes;
}

/* Double the table.  A failure leaves it as it was: a long chain is slow, not wrong. */
static void grow(GSHSCENE_Store * store) {
  size_t count = store->bucket_count * 2;
  GSHSCENE_Resource ** buckets = gshscene_calloc(&store->alloc, count, sizeof *buckets);
  if (!buckets) {
    return;
  }
  for (size_t i = 0; i < store->bucket_count; i++) {
    GSHSCENE_Resource * r = store->buckets[i];
    while (r) {
      GSHSCENE_Resource * next = r->next;
      size_t slot = r->index & (count - 1);
      r->next = buckets[slot];
      buckets[slot] = r;
      r = next;
    }
  }
  gshscene_free(&store->alloc, store->buckets);
  store->buckets = buckets;
  store->bucket_count = count;
}

GSHSCENE_Result gshscene_store_intern(
    GSHSCENE_Store * store, const GSHSCENE_Probe * probe, const GSHSCENE_Resource ** out) {
  uint64_t index = store->index_fn ? store->index_fn(probe->hash, store->index_ctx) : probe->hash;

  GCU_MUTEX_LOCK(store->lock);
  GSHSCENE_Resource * found = lookup(store, probe, index);
  if (found) {
    store->hits++;
    GCU_MUTEX_UNLOCK(store->lock);
    *out = found;
    return GSHSCENE_OK;
  }
  /* A miss that cannot fit is refused before anything is copied. */
  int room = fits(store, probe->bytes);
  GCU_MUTEX_UNLOCK(store->lock);
  if (!room) {
    return GSHSCENE_ERR_LIMIT;
  }

  /* A miss: build outside the lock, then look again, because another thread
   * may have interned the same content meanwhile. */
  GSHSCENE_Resource * built = NULL;
  GSHSCENE_Result result = probe->build(store, probe->source, &built);
  if (result != GSHSCENE_OK) {
    return result;
  }
  built->hash = probe->hash;
  built->index = index;
  built->bytes = probe->bytes;

  GCU_MUTEX_LOCK(store->lock);
  found = lookup(store, probe, index);
  if (found) {
    store->hits++;
    GCU_MUTEX_UNLOCK(store->lock);
    gshscene_store_discard_resource(built);
    *out = found;
    return GSHSCENE_OK;
  }
  if (store->live >= store->limits.max_resources || !fits(store, probe->bytes)) {
    GCU_MUTEX_UNLOCK(store->lock);
    gshscene_store_discard_resource(built);
    return GSHSCENE_ERR_LIMIT;
  }
  built->id = ++store->next_id;
  size_t slot = index & (store->bucket_count - 1);
  built->next = store->buckets[slot];
  store->buckets[slot] = built;
  store->live++;
  store->bytes += probe->bytes;
  if (store->live > store->bucket_count) {
    grow(store);
  }
  GCU_MUTEX_UNLOCK(store->lock);
  *out = built;
  return GSHSCENE_OK;
}

/* --- handles --------------------------------------------------------------- */

const GSHSCENE_Resource * gshscene_resource_retain(const GSHSCENE_Resource * resource) {
  if (resource) {
    gcu_atomic_size_fetch_add(&((GSHSCENE_Resource *)resource)->refs, 1);
  }
  return resource;
}

void gshscene_resource_release(const GSHSCENE_Resource * handle) {
  if (!handle) {
    return;
  }
  GSHSCENE_Resource * resource = (GSHSCENE_Resource *)handle;
  if (gcu_atomic_size_fetch_sub(&resource->refs, 1) != 1) {
    return;
  }
  /* The last reference.  Leave the table first, so that nothing can find it. */
  GSHSCENE_Store * store = resource->store;
  GCU_MUTEX_LOCK(store->lock);
  GSHSCENE_Resource ** link = &store->buckets[resource->index & (store->bucket_count - 1)];
  while (*link != resource) {
    link = &(*link)->next;
  }
  *link = resource->next;
  store->live--;
  store->bytes -= resource->bytes;
  GSHSCENE_Listeners * listeners = store->listeners;
  if (listeners) {
    gcu_atomic_size_fetch_add(&listeners->refs, 1);
  }
  GCU_MUTEX_UNLOCK(store->lock);

  GSHSCENE_Allocator alloc = store->alloc;
  if (listeners) {
    for (size_t i = 0; i < listeners->count; i++) {
      listeners->items[i].fn(listeners->items[i].ctx, resource->id, resource);
    }
    listeners_unref(&alloc, listeners);
  }
  gshscene_store_discard_resource(resource);
}

GSHSCENE_Resource_Kind gshscene_resource_kind(const GSHSCENE_Resource * resource) {
  return resource ? resource->kind : GSHSCENE_RESOURCE_KIND_COUNT;
}

uint64_t gshscene_resource_id(const GSHSCENE_Resource * resource) {
  return resource ? resource->id : 0;
}

uint64_t gshscene_resource_hash(const GSHSCENE_Resource * resource) {
  return resource ? resource->hash : GSHGEOM_FNV_OFFSET_BASIS;
}

size_t gshscene_resource_bytes(const GSHSCENE_Resource * resource) {
  return resource ? resource->bytes : 0;
}

size_t gshscene_resource_refcount(const GSHSCENE_Resource * resource) {
  return resource ? gcu_atomic_size_load(&((GSHSCENE_Resource *)resource)->refs) : 0;
}

/* --- release listeners ----------------------------------------------------- */

static GSHSCENE_Listeners * listeners_make(const GSHSCENE_Allocator * alloc, size_t count) {
  GSHSCENE_Listeners * listeners =
      gshscene_alloc(alloc, sizeof *listeners + count * sizeof(GSHSCENE_Listener));
  if (listeners) {
    gcu_atomic_size_init(&listeners->refs, 1);
    listeners->count = count;
  }
  return listeners;
}

GSHSCENE_Result gshscene_store_add_release_listener(
    GSHSCENE_Store * store, GSHSCENE_Release_Fn fn, void * ctx, uint64_t * out_token) {
  if (!store || !fn || !out_token) {
    return GSHSCENE_ERR_INVALID;
  }
  GCU_MUTEX_LOCK(store->lock);
  size_t old_count = store->listeners ? store->listeners->count : 0;
  GSHSCENE_Listeners * grown = listeners_make(&store->alloc, old_count + 1);
  if (!grown) {
    GCU_MUTEX_UNLOCK(store->lock);
    return GSHSCENE_ERR_OOM;
  }
  if (old_count) {
    memcpy(grown->items, store->listeners->items, old_count * sizeof(GSHSCENE_Listener));
  }
  uint64_t token = ++store->next_token;
  grown->items[old_count].token = token;
  grown->items[old_count].fn = fn;
  grown->items[old_count].ctx = ctx;
  GSHSCENE_Listeners * old = store->listeners;
  store->listeners = grown;
  GCU_MUTEX_UNLOCK(store->lock);
  listeners_unref(&store->alloc, old);
  *out_token = token;
  return GSHSCENE_OK;
}

GSHSCENE_Result gshscene_store_remove_release_listener(GSHSCENE_Store * store, uint64_t token) {
  if (!store) {
    return GSHSCENE_ERR_INVALID;
  }
  GCU_MUTEX_LOCK(store->lock);
  GSHSCENE_Listeners * old = store->listeners;
  size_t at = 0;
  size_t count = old ? old->count : 0;
  while (at < count && old->items[at].token != token) {
    at++;
  }
  if (at == count) {
    GCU_MUTEX_UNLOCK(store->lock);
    return GSHSCENE_ERR_INVALID;
  }
  GSHSCENE_Listeners * shrunk = NULL;
  if (count > 1) {
    shrunk = listeners_make(&store->alloc, count - 1);
    if (!shrunk) {
      GCU_MUTEX_UNLOCK(store->lock);
      return GSHSCENE_ERR_OOM;
    }
    memcpy(shrunk->items, old->items, at * sizeof(GSHSCENE_Listener));
    memcpy(shrunk->items + at, old->items + at + 1, (count - at - 1) * sizeof(GSHSCENE_Listener));
  }
  store->listeners = shrunk;
  GCU_MUTEX_UNLOCK(store->lock);
  listeners_unref(&store->alloc, old);
  return GSHSCENE_OK;
}
