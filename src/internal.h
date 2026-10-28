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
    struct {
      GSHSCENE_Gradient g;            /* canonical; stops points at the copy below */
      GSHSCENE_Gradient_Stop * stops; /* sorted stably by offset, stop_count of them */
      uint32_t palette_need;          /* the largest palette entry a stop names, plus one; 0 for none */
      int foreground;                 /* a stop is a foreground paint */
    } gradient;
    struct {
      GSHSCENE_Stroke_Style s; /* canonical (SK3); dashes points at the copy below */
      double * dashes;         /* dash_count of them, NULL for none */
    } stroke;
    struct {
      GSHSCENE_Image_Paint p;     /* canonical (IP1); image and tint point at the copies below */
      GSHSCENE_Paint tint;        /* the tint, when the image is alpha-only */
      const GSHSCENE_Resource * image; /* retained */
      int foreground;             /* the tint is a foreground paint */
      uint32_t palette_need;      /* the tint's palette entry, plus one; 0 for none */
    } imagepaint;
    struct {
      GSHSCENE_Image_Info info;   /* IM1 */
      const unsigned char * rows; /* the first row: the copy below, or the taken raster's pixels */
      size_t stride;              /* bytes between rows: a copy's is its row, a raster's is its own */
      size_t row_bytes;           /* bytes of samples in a row */
      unsigned char * copy;       /* a copy's samples, tightly packed; NULL for a take */
      GIMG_Raster * raster;       /* a taken raster; NULL for a copy */
      int owns_raster;            /* the raster is the store's to destroy (set once the intern has linked it) */
      uint64_t * tiles;           /* tiles_x * tiles_y hashes in tile order */
    } image;
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

/** N1: `sat(x)`, @p x clamped to `[-2^64, 2^64]` (an infinity to its end; a value inside is returned bit for bit). */
static inline double gshscene_saturate(double x) {
  return x > GSHSCENE_COLOR_SATURATION ? GSHSCENE_COLOR_SATURATION
      : x < -GSHSCENE_COLOR_SATURATION ? -GSHSCENE_COLOR_SATURATION : x;
}

/** N1: the three color components of @p rgba (finite) within `[-2^16, 2^16]`; the alpha is the validity check's. */
static inline int gshscene_color_bounded(const double rgba[4]) {
  for (int i = 0; i < 3; i++) {
    if (!(rgba[i] >= -GSHSCENE_COLOR_COMPONENT_MAX && rgba[i] <= GSHSCENE_COLOR_COMPONENT_MAX)) {
      return 0;
    }
  }
  return 1;
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

/* --- gradient.c ------------------------------------------------------------- */

/** Whether a gradient resource has the canonical content @p source (a GSHSCENE_Gradient, stops sorted). */
int gshscene_gradient_matches(const GSHSCENE_Resource * resource, const void * source);

/** Free a gradient's stops. */
void gshscene_gradient_free(GSHSCENE_Resource * resource);

/** The payload of a gradient's dump (resource.h); the result of the writes, negative on failure. */
int gshscene_gradient_dump(const GSHSCENE_Resource * resource, FILE * out);

/** G11's condition on one map: its 2x2 linear part's determinant is exactly zero (a zero-filled map is the identity). */
int gshscene_map_singular(const GSHGEOM_Map * map);

/** G10: whether a gradient draws nothing wherever it is. */
int gshscene_gradient_degenerate(const GSHSCENE_Gradient * g);

/* --- stroke.c ---------------------------------------------------------------- */

/** Whether a stroke style resource has the canonical content @p source (a GSHSCENE_Stroke_Style). */
int gshscene_stroke_matches(const GSHSCENE_Resource * resource, const void * source);

/** Free a stroke style's dashes. */
void gshscene_stroke_free(GSHSCENE_Resource * resource);

/** The payload of a stroke style's dump (resource.h, SK12); negative on failure. */
int gshscene_stroke_dump(const GSHSCENE_Resource * resource, FILE * out);

/**
 * SK10: the box of a centerline (a path or a glyph run) in its model space: a path's bounds; a run's glyphs placed,
 * each outline's bounds moved by the glyph's position, or (from a font, SK7's general route) each glyph's design box
 * under its glyph map.  ::GSHSCENE_ERR_UNSUPPORTED when it cannot be had (a 3D path, an unbound face, a map past its
 * bounds): the bound is unknown.
 */
GSHSCENE_Result gshscene_centerline_box(const GSHSCENE_Resource * resource, GSHGEOM_Bounds * out);

/* --- image_paint.c ---------------------------------------------------------- */

/** Whether an image-paint resource has the canonical content @p source (a GSHSCENE_Image_Paint, window explicit). */
int gshscene_image_paint_matches(const GSHSCENE_Resource * resource, const void * source);

/** Release the image an image paint holds. */
void gshscene_image_paint_free(GSHSCENE_Resource * resource);

/** The payload of an image paint's dump (image_paint.h, IP13); negative on failure. */
int gshscene_image_paint_dump(const GSHSCENE_Resource * resource, FILE * out);

/** The distinct reasons a map of an image paint's chain makes the fill draw nothing: 1 when the paint's own map is singular. */
int gshscene_image_paint_singular(const GSHSCENE_Resource * resource);

/* --- image.c ---------------------------------------------------------------- */

/** Whether an image resource has the content @p source describes (an image.c Image_Source). */
int gshscene_image_matches(const GSHSCENE_Resource * resource, const void * source);

/** Free an image's tile hashes and copy, and destroy a taken raster the store owns. */
void gshscene_image_free(GSHSCENE_Resource * resource);

/** The payload of an image's dump (image.h, IM12); negative on failure. */
int gshscene_image_dump(const GSHSCENE_Resource * resource, FILE * out);

/* --- convert.c -------------------------------------------------------------- */

/** N2, N3: a straight color from space @p from to space @p to, both concrete (not zero); exact when they are equal. */
void gshscene_convert_straight(const GSHSCENE_Color_Space * from, const GSHSCENE_Color_Space * to, const double in[3],
    double out[3]);

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
/** SK4: whether a path or glyph run is not planar (a 3D path, or a run whose outline is one). */
int gshscene_centerline_3d(const GSHSCENE_Resource * resource);

/** A paint as a fill spells it in a hash (list.h): `u8` kind, four `f64`, a space, `u32` entry, `u64` the gradient's hash. */
void gshscene_paint_hash(GSHGEOM_Hash * h, const GSHSCENE_Paint * paint);
/** A space as `P:T:G` in a dump (the gamma as 16 hexadecimal digits); the result of fprintf. */
int gshscene_space_dump(FILE * out, const GSHSCENE_Color_Space * space);

/**
 * A paint by a fill's rules (list.h), for the list of @p store: a gradient must be a gradient resource of it; then
 * N1's bound on its components (::GSHSCENE_ERR_LIMIT), after every validity check.
 */
GSHSCENE_Result gshscene_list_canonical_paint(const GSHSCENE_Store * store, const GSHSCENE_Paint * paint,
    GSHSCENE_Paint * out);

/** The validity checks of ::gshscene_list_canonical_paint alone, without N1's bound (a gradient's stops: G14). */
GSHSCENE_Result gshscene_list_canonical_paint_valid(const GSHSCENE_Store * store, const GSHSCENE_Paint * paint,
    GSHSCENE_Paint * out);

/** The validity checks of ::gshscene_color_check alone, without N1's bound (a palette's entries, all checked first). */
GSHSCENE_Result gshscene_color_check_valid(const GSHSCENE_Color * color);

/** A map by an item's rules (list.h): valid, and only the entries a map of its dimension uses. */
GSHSCENE_Result gshscene_list_canonical_map(const GSHGEOM_Map * map, GSHGEOM_Map * out);

/** The same checks as a record call's, for a retained scene's nodes. */
GSHSCENE_Result gshscene_list_canonical_group(
    const GSHSCENE_Store * store, const GSHSCENE_Group * group, GSHSCENE_Group * out);

/** T1: whether a group's isolation, knockout and soft mask are the defaults (isolated, 0, none). */
int gshscene_group_pdf_default(uint32_t isolation, uint32_t knockout, const GSHSCENE_Resource * soft_mask);

/** T1: a group's `u8` composite in a hash, and after it PDF's fields when one is not the default (list.h). */
void gshscene_group_hash_composite(GSHGEOM_Hash * h, uint32_t composite, uint32_t isolation, uint32_t knockout,
    const GSHSCENE_Resource * soft_mask);

/** T1: ` isolation=N knockout=N mask=<hash>` in a dump when one of them is not the default; else nothing.  < 0 on error. */
int gshscene_group_dump_pdf(FILE * out, uint32_t isolation, uint32_t knockout, const GSHSCENE_Resource * soft_mask);
GSHSCENE_Result gshscene_list_canonical_depth(double depth, double * out);

/** PH1, PH2: a placeholder's fields by the rules of the record call (a retained scene's node shares them); -0 becomes +0. */
GSHSCENE_Result gshscene_placeholder_canonical(uint16_t source, uint16_t code, uint32_t approximated, const double * box,
    GSHSCENE_Placeholder * out);

/** PH1: a placeholder's content in a list's or a version's hash (list.h). */
void gshscene_placeholder_hash(GSHGEOM_Hash * h, const GSHSCENE_Placeholder * p);

/** PH1: ` source=N code=N approx=N box=...` in a dump (list.h).  < 0 on error. */
int gshscene_placeholder_dump(FILE * out, const GSHSCENE_Placeholder * p);

#endif /* GHOTI_IO_GSHSCENE_INTERNAL_H */
