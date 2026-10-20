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
 * The structures of the store, the resources and the lists, and the functions
 * the three source files share.  Internal: not installed.
 */

#ifndef GHOTI_IO_GSHSCENE_INTERNAL_H
#define GHOTI_IO_GSHSCENE_INTERNAL_H

#include <ghoti.io/shape-scene/macros.h>

#include <ghoti.io/shape-scene/shape-scene.h>

#include <ghoti.io/cutil/atomic.h>
#include <ghoti.io/cutil/mutex.h>

#include <ghoti.io/shape-geometry/hash.h>

#include <stddef.h>
#include <stdint.h>

/** Items per list segment, for the first; each segment holds twice the one before. */
#define GSHSCENE_SEGMENT_FIRST 16u

/** Segments a list can have: with 16 items in the first, `16 * (2^28 - 1)` items. */
#define GSHSCENE_SEGMENT_MAX 28u

/** Bytes of a standard arena chunk's payload. */
#define GSHSCENE_CHUNK_PAYLOAD 16384u

/** Alignment of everything the arena returns. */
#define GSHSCENE_ARENA_ALIGN 16u

/** An arena chunk; its payload follows, aligned. */
typedef struct GSHSCENE_Chunk {
  struct GSHSCENE_Chunk * next;
  size_t capacity; /* payload bytes */
  size_t used;
} GSHSCENE_Chunk;

/** One registered listener. */
typedef struct GSHSCENE_Listener {
  uint64_t token;
  GSHSCENE_Release_Fn fn;
  void * ctx;
} GSHSCENE_Listener;

/** A copy-on-write array of listeners, counted. */
typedef struct GSHSCENE_Listeners {
  GCU_Atomic_Size refs;
  size_t count;
  GSHSCENE_Listener items[]; /* count of them */
} GSHSCENE_Listeners;

struct GSHSCENE_Resource {
  GSHSCENE_Store * store;
  struct GSHSCENE_Resource * next; /* the table's chain */
  GCU_Atomic_Size refs;
  uint64_t id;
  uint64_t hash;  /* the content hash */
  uint64_t index; /* what the table is indexed by */
  size_t bytes;   /* accounted bytes (resource.h) */
  GSHSCENE_Resource_Kind kind;
  union {
    GSHGEOM_Path * path;
    struct {
      GSHSCENE_Glyph_Run run; /* glyphs points at the copy below */
      GSHSCENE_Glyph * glyphs;
    } glyphs;
    struct {
      GSHSCENE_List * list;
      size_t depth;
    } picture;
    struct {
      GSHSCENE_Face face;           /* coords points at the copy below */
      int16_t * coords;
      GSHSCENE_Glyph_Provider ops;  /* the binding's ops, copied; meaningful when bound */
      void * ctx;
      GSHSCENE_Face_Info info;      /* as face_info gave it when the face was built */
      int bound;
    } face;
    struct {
      GSHSCENE_Color * colors; /* count of them, copied */
      size_t count;
    } palette;
  } u;
};

struct GSHSCENE_Store {
  GSHSCENE_Allocator alloc;
  GSHSCENE_Limits limits; /* effective: no zero field */
  GSHSCENE_Index_Hash_Fn index_fn;
  void * index_ctx;
  GCU_Atomic_Size refs; /* the creator and every resource */
  GCU_MUTEX_T lock;
  struct GSHSCENE_Resource ** buckets;
  size_t bucket_count; /* a power of two */
  uint64_t next_id;
  uint64_t next_token;
  uint64_t live;
  uint64_t bytes; /* accounted bytes of the live resources */
  uint64_t hits;
  uint64_t collisions;
  GSHSCENE_Listeners * listeners;
};

struct GSHSCENE_List {
  GSHSCENE_Allocator alloc;
  GSHSCENE_Limits limits; /* effective: no zero field */
  GSHSCENE_Store * store;
  GSHSCENE_Chunk * head;  /* standard chunks, newest first */
  GSHSCENE_Chunk * first; /* the oldest standard chunk: kept by a reset */
  GSHSCENE_Chunk * big;   /* dedicated chunks */
  size_t bytes;           /* chunk bytes taken from the allocator, headers included */
  GSHSCENE_Item * segments[GSHSCENE_SEGMENT_MAX];
  size_t segment_count;
  size_t capacity; /* items the segments hold */
  size_t count;
  uint8_t * stack; /* what each open push was */
  size_t stack_capacity;
  size_t depth;
  size_t expansion;
  size_t picture_depth; /* the deepest picture an instance names */
  size_t nonflat;       /* group pushes whose ordering is not flat: close has an order to make when it is not 0 */
  GSHSCENE_Color foreground;
  const GSHSCENE_Resource * palette; /* the palette parameter (N14), retained; NULL for none */
  GSHSCENE_Tie * ties;  /* the resolved depth ties, ascending by second item */
  size_t tie_count;
  size_t tie_capacity;
  int closed;
};

/** What an intern looks for, and how to build it on a miss. */
typedef struct GSHSCENE_Probe {
  GSHSCENE_Resource_Kind kind;
  uint64_t hash;
  size_t bytes; /* the accounted bytes the resource will have */
  const void * source;
  /** Build the resource (not linked, one reference, a reference on the store). */
  GSHSCENE_Result (*build)(GSHSCENE_Store * store, const void * source, GSHSCENE_Resource ** out);
} GSHSCENE_Probe;

/* --- allocation through the caller's allocator ---------------------------- */

static inline void * gshscene_alloc(const GSHSCENE_Allocator * alloc, size_t size) {
  return alloc->malloc_fn(alloc->ctx, size);
}

static inline void * gshscene_calloc(const GSHSCENE_Allocator * alloc, size_t count, size_t size) {
  return alloc->calloc_fn(alloc->ctx, count, size);
}

static inline void gshscene_free(const GSHSCENE_Allocator * alloc, void * pointer) {
  if (pointer) {
    alloc->free_fn(alloc->ctx, pointer);
  }
}

/** @p a plus @p b, saturating at the largest size_t. */
static inline size_t gshscene_sat_add(size_t a, size_t b) {
  return a > SIZE_MAX - b ? SIZE_MAX : a + b;
}

/** @p a times @p b, saturating at the largest size_t. */
static inline size_t gshscene_sat_mul(size_t a, size_t b) {
  return (b != 0 && a > SIZE_MAX / b) ? SIZE_MAX : a * b;
}

/** A finite double: not NaN, not infinite. */
static inline int gshscene_finite(double x) {
  return x - x == 0.0;
}

/* --- store.c --------------------------------------------------------------- */

void gshscene_store_ref(GSHSCENE_Store * store);
void gshscene_store_unref(GSHSCENE_Store * store);

/** Make an unlinked resource of @p kind: one reference, a reference on the store.  The union is zero. */
GSHSCENE_Resource * gshscene_store_new_resource(GSHSCENE_Store * store, GSHSCENE_Resource_Kind kind);

/** Free a resource that was never linked: its content, itself, its reference on the store. */
void gshscene_store_discard_resource(GSHSCENE_Resource * resource);

GSHSCENE_Result gshscene_store_intern(GSHSCENE_Store * store, const GSHSCENE_Probe * probe,
    const GSHSCENE_Resource ** out);

/* --- resource.c ------------------------------------------------------------ */

/** Whether @p resource has the content @p probe describes (bit for bit). */
int gshscene_resource_matches(const GSHSCENE_Resource * resource, const GSHSCENE_Probe * probe);

/** Release what the resource's content holds and frees its arrays. */
void gshscene_resource_free_content(GSHSCENE_Resource * resource);

/** Whether two paths are the same content, bit for bit. */
int gshscene_path_equal(const GSHGEOM_Path * a, const GSHGEOM_Path * b);

/* --- list.c ---------------------------------------------------------------- */

/** Make a list with limits already effective (no zero field). */
GSHSCENE_Result gshscene_list_create_effective(const GSHSCENE_Allocator * alloc,
    const GSHSCENE_Limits * effective, GSHSCENE_Store * store, GSHSCENE_List ** out);

/** The limits with every zero field replaced by its default. */
void gshscene_limits_effective(const GSHSCENE_Limits * limits, GSHSCENE_Limits * out);

/** A closed copy of a closed list, from @p alloc. */
GSHSCENE_Result gshscene_list_copy(
    const GSHSCENE_List * source, const GSHSCENE_Allocator * alloc, GSHSCENE_List ** out);

/** The deepest picture the list's instances name (0 for none); the list is not NULL. */
size_t gshscene_list_picture_depth(const GSHSCENE_List * list);

/** Close a list whose items are already in paint order (a version's emission): no permutation, ties as recorded. */
GSHSCENE_Result gshscene_list_close_ordered(GSHSCENE_List * list);

/** Record a tie on an open list being emitted in paint order (not validated: the emitter owns the indices). */
GSHSCENE_Result gshscene_list_add_tie(GSHSCENE_List * list, size_t first, size_t second, double depth,
    GSHSCENE_Tie_Reason reason);

/**
 * How a context orders two units by their keys (N11): negative if @p a is painted before @p b, positive if after, 0 if
 * the context cannot tell them apart.  Flat never tells.  Depth is far first, then ascending layer; overlay is ascending
 * layer.  @p tied is set to 1 when the context is depth and the depths are equal, else 0.
 */
int gshscene_order_compare(uint32_t context, int32_t layer_a, double depth_a, int32_t layer_b, double depth_b,
    int * tied);

/** Whether two color spaces are the same, the gamma bit for bit. */
int gshscene_space_equal(const GSHSCENE_Color_Space * a, const GSHSCENE_Color_Space * b);

/** Whether a color is the default foreground (opaque black in the target's space), bit for bit. */
int gshscene_color_is_default(const GSHSCENE_Color * color);
/** Whether two colors are equal, bit for bit (the components and the space). */
int gshscene_color_equal(const GSHSCENE_Color * a, const GSHSCENE_Color * b);

/** A space, then a color, in the spelling of paint.h's N-text (hash.h's primitives): u8 primaries, u8 transfer, f64 gamma; four f64 and a space. */
void gshscene_space_hash(GSHGEOM_Hash * h, const GSHSCENE_Color_Space * space);
void gshscene_color_hash(GSHGEOM_Hash * h, const GSHSCENE_Color * color);
/** A space as `P:T:G` in a dump (the gamma as 16 hexadecimal digits); the result of fprintf. */
int gshscene_space_dump(FILE * out, const GSHSCENE_Color_Space * space);

/** The same checks as a record call's, for a retained scene's nodes. */
GSHSCENE_Result gshscene_list_canonical_group(const GSHSCENE_Group * group, GSHSCENE_Group * out);
GSHSCENE_Result gshscene_list_canonical_depth(double depth, double * out);

#endif /* GHOTI_IO_GSHSCENE_INTERNAL_H */
