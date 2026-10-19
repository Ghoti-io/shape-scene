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
 * @file resource.h
 *
 * The store and its resources: immutable content, interned by content, held
 * by handle (AD-27, CAP-16).
 *
 * ### The model
 *
 * A **resource** is a path, a glyph run or a picture (a closed list, frozen);
 * two further kinds, a gradient and an image, are named and reserved.  A
 * **store** interns resources: asking it for content it already holds, while a
 * handle to that content is held, returns that handle, so equality of content
 * is equality of pointers within one store.  The store decides by the index
 * hash and then by comparing the content bit for bit.  A hash alone never
 * decides.
 *
 * A handle is a `const GSHSCENE_Resource *`.  It is reference counted
 * (::gshscene_resource_retain, ::gshscene_resource_release), freely shared
 * across threads, and freed when the count reaches zero.  Every intern call
 * returns a reference the caller owns.  A list holds its own reference on every
 * resource it names (list.h).
 *
 * The **id** of a resource is a 64-bit number from a per-store counter that
 * starts at 1 and is never issued twice.  It is the key a sink's cache should
 * use: a pointer can be reused by the allocator the moment a resource dies, an
 * id cannot.  The cache drops an entry from a **release listener**
 * (::gshscene_store_add_release_listener).
 *
 * A **store** is thread-safe.  ::gshscene_store_destroy drops the creator's
 * reference; the memory lives until the last resource of the store has been
 * released.
 *
 * ### The hash of a resource (version 1)
 *
 * The **content hash**, in the spelling of shape-geometry's hash.h.  Neither
 * the id, nor the pointer, nor the allocator, nor the limits are part of it.
 * Each stream starts with `u32` 1 (this format) and `u32` the resource's kind
 * (::GSHSCENE_Resource_Kind, pinned), and goes on:
 *
 * - **path**: `u64` the path's own hash (`gshgeom_path_hash`).
 * - **glyph run**: `u32` the source, `u64` font key, `u32` face index, `f64`
 *   size, `u32` hint mode, `u64` variation key, `u64` glyph count; then for
 *   each glyph `u32` id, `f64` x, `f64` y and `u64` the content hash of the
 *   outline resource (0 for a glyph with none).
 * - **picture**: `u64` the hash of its list (list.h).
 *
 * The hash is of the content, not of the reference to it: a glyph run of
 * outlines hashes its outlines' content hashes, so two stores that were given
 * the same content print the same hash.
 *
 * ### The dump
 *
 * `gshscene.resource 1 kind=<name> hash=<16 hex digits>` and then the payload
 * as text, every `double` as the 16 hexadecimal digits of its bit pattern and
 * every hash as 16 digits: a path is `gshgeom_path_dump`'s own text; a glyph
 * run is a line `run source=S font=<hex> face=N size=<hex> hint=N var=<hex>
 * glyphs=N` and a line `g id=N x=<hex> y=<hex> outline=<hex>` per glyph; a
 * picture is `gshscene_list_dump`'s text.
 */

#ifndef GHOTI_IO_GSHSCENE_RESOURCE_H
#define GHOTI_IO_GSHSCENE_RESOURCE_H

#include <ghoti.io/shape-scene/allocator.h>
#include <ghoti.io/shape-scene/core.h>
#include <ghoti.io/shape-scene/macros.h>
#include <ghoti.io/shape-scene/types.h>

#include <ghoti.io/shape-geometry/path.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief What a resource is.  The values are pinned (they are hashed). */
typedef enum GSHSCENE_Resource_Kind {
  GSHSCENE_RESOURCE_PATH = 1,      ///< A model path.
  GSHSCENE_RESOURCE_GLYPH_RUN = 2, ///< Glyphs, by font and id or as outlines.
  GSHSCENE_RESOURCE_PICTURE = 3,   ///< A closed list, frozen: what an instance draws.
  GSHSCENE_RESOURCE_GRADIENT = 4,  ///< Reserved for story 12: no constructor yet.
  GSHSCENE_RESOURCE_IMAGE = 5,     ///< Reserved for the raster stories: no constructor yet.
  GSHSCENE_RESOURCE_KIND_COUNT     ///< Closes the enum.
} GSHSCENE_Resource_Kind;

/**
 * @brief Name a resource kind.
 *
 * @param kind The kind, including a value outside the enum.
 * @return A static string, never NULL: "path", "glyph-run", "picture",
 *   "gradient", "image", or "unknown resource kind".
 */
GSHSCENE_API const char * gshscene_resource_kind_string(GSHSCENE_Resource_Kind kind);

/** @brief Where a glyph run's glyphs come from.  The values are pinned (they are hashed). */
typedef enum GSHSCENE_Glyph_Source {
  /** The glyph is named by font key, face, variation and id; the glyph path provider (story 14) turns it into an outline. */
  GSHSCENE_GLYPHS_FROM_FONT = 0,
  /** The glyph is a path resource, in run space, drawn translated by the glyph's position. */
  GSHSCENE_GLYPHS_FROM_OUTLINES = 1,
  GSHSCENE_GLYPH_SOURCE_COUNT ///< Closes the enum.
} GSHSCENE_Glyph_Source;

/** @brief One glyph of a run. */
typedef struct GSHSCENE_Glyph {
  uint32_t id; ///< The glyph id in the face (for the outline source, the caller's own number; hashed and compared like any field).
  double x;    ///< Where the glyph's origin is, in run space: finite, at most `GSHGEOM_COORD_LIMIT` in magnitude.
  double y;    ///< Likewise.  Down is positive (AD-6).
  /** From outlines: a path resource of the same store.  From font: NULL. */
  const GSHSCENE_Resource * outline;
} GSHSCENE_Glyph;

/**
 * @brief The content of a glyph run.
 *
 * Positions come from shaping (advances, kerning and offsets already applied),
 * so a paragraph layout is a run per style per line, or per glyph where each
 * glyph needs its own map.  The run is drawn under one map and one paint.
 */
typedef struct GSHSCENE_Glyph_Run {
  GSHSCENE_Glyph_Source source; ///< Where the glyphs come from.
  /** From font: the nonzero identity of the face (for `font`, a hash of the font data).  From outlines: 0. */
  uint64_t font_key;
  uint32_t face_index;          ///< From font: the face in a collection.  From outlines: 0.
  /** From font: the em size in model units, finite, greater than zero, at most `GSHGEOM_COORD_LIMIT`.  From outlines: 0. */
  double size;
  /** From font: the hint mode passed to the glyph path provider unread; 0 is the font's default.  From outlines: 0. */
  uint32_t hint_mode;
  uint64_t variation_key;       ///< From font: a hash of the variation axis values, 0 for none.  From outlines: 0.
  size_t glyph_count;           ///< How many glyphs; 0 is an empty run.
  const GSHSCENE_Glyph * glyphs; ///< The glyphs; may be NULL only when @p glyph_count is 0.
} GSHSCENE_Glyph_Run;

/**
 * @name Accounted bytes
 *
 * `GSHSCENE_Limits.max_bytes` bounds the **accounted bytes** of the live
 * resources of a store.  The figure is a formula, not a measurement of the
 * allocator's blocks, so that the limit means the same on every target and with
 * every allocator: a resource's base, plus, for a path, the per-element figure
 * for each element (the most a path element, a rational cubic in 3D, can take),
 * for a glyph run the per-glyph figure for each glyph, and for a picture the
 * per-item figure for each item of its list.  What a resource holds by handle
 * (a glyph's outline, an instanced picture) is a resource of its own and is
 * counted there.  An intern that would pass the limit is
 * ::GSHSCENE_ERR_LIMIT and builds and keeps nothing; a hit costs nothing; the
 * last release gives the bytes back.
 * @{
 */
#define GSHSCENE_BYTES_BASE ((size_t)128)        ///< Accounted bytes of any resource.
#define GSHSCENE_BYTES_PER_PATH_ELEMENT ((size_t)160) ///< Added for each element of a path.
#define GSHSCENE_BYTES_PER_GLYPH ((size_t)32)    ///< Added for each glyph of a run.
#define GSHSCENE_BYTES_PER_ITEM ((size_t)256)    ///< Added for each item of a picture's list.
/** @} */

/** @brief Counters of a store, for tests and diagnostics. */
typedef struct GSHSCENE_Store_Stats {
  uint64_t live;       ///< Resources in the table with a reference held.
  uint64_t bytes;      ///< Accounted bytes of the live resources (see ::GSHSCENE_BYTES_BASE).
  uint64_t created;    ///< Resources ever made (the ids issued).
  uint64_t hits;       ///< Interns answered by an existing resource.
  uint64_t collisions; ///< Candidates with an equal index hash and unequal content that a lookup examined.
  uint64_t buckets;    ///< The table's current size.
} GSHSCENE_Store_Stats;

/**
 * @brief Maps a content hash to the hash the table is indexed by.
 *
 * The default is the identity.  A test supplies one that sends everything to
 * one value to plant a collision; the dump and hash of a resource are never
 * affected.  It may be called from any thread, concurrently, and must not call
 * the store.
 */
typedef uint64_t (*GSHSCENE_Index_Hash_Fn)(uint64_t content_hash, void * ctx);

/**
 * @brief Called once for each resource at its last release.
 *
 * It runs on the releasing thread, after the resource has left the table and
 * before its memory is freed, outside the store's lock, and may call the
 * store (except to retain this resource, which cannot be retained any more).
 * @p resource is readable for the duration of the call only.
 */
typedef void (*GSHSCENE_Release_Fn)(void * ctx, uint64_t id, const GSHSCENE_Resource * resource);

/**
 * @brief Make a store.
 *
 * @param alloc The allocator, or NULL for the default; copied.
 * @param limits The limits, or NULL for the defaults; copied.
 * @param out The store; written only on success.  Release it with
 *   ::gshscene_store_destroy.
 * @return ::GSHSCENE_OK, ::GSHSCENE_ERR_INVALID (NULL @p out) or
 *   ::GSHSCENE_ERR_OOM.
 */
GSHSCENE_API GSHSCENE_Result gshscene_store_create(const GSHSCENE_Allocator * alloc,
    const GSHSCENE_Limits * limits, GSHSCENE_Store ** out);

/**
 * @brief Make a store whose table is indexed by @p index_hash of the content hash.
 *
 * For tests and diagnostics: it is how a collision is planted.  Otherwise as
 * ::gshscene_store_create.
 *
 * @param index_hash The function, or NULL for the identity.
 * @param index_ctx Passed to it.
 */
GSHSCENE_API GSHSCENE_Result gshscene_store_create_with_index_hash(
    const GSHSCENE_Allocator * alloc, const GSHSCENE_Limits * limits,
    GSHSCENE_Index_Hash_Fn index_hash, void * index_ctx, GSHSCENE_Store ** out);

/**
 * @brief Drop the creator's reference to the store.
 *
 * The store and its table are freed when the last resource of it has been
 * released.  NULL is ignored.
 */
GSHSCENE_API void gshscene_store_destroy(GSHSCENE_Store * store);

/** @brief The store's counters.  @return ::GSHSCENE_OK or ::GSHSCENE_ERR_INVALID (NULL). */
GSHSCENE_API GSHSCENE_Result gshscene_store_stats(
    const GSHSCENE_Store * store, GSHSCENE_Store_Stats * out);

/**
 * @brief Register a release listener.
 *
 * @param store The store.
 * @param fn The function (not NULL).
 * @param ctx Passed to it.
 * @param out_token Names the registration for ::gshscene_store_remove_release_listener; never 0.
 * @return ::GSHSCENE_OK, ::GSHSCENE_ERR_INVALID or ::GSHSCENE_ERR_OOM.
 *
 * A listener hears of the resources released after this call returns, whichever
 * thread releases them.  Listeners are called in the order they were added.
 */
GSHSCENE_API GSHSCENE_Result gshscene_store_add_release_listener(GSHSCENE_Store * store,
    GSHSCENE_Release_Fn fn, void * ctx, uint64_t * out_token);

/**
 * @brief Remove a release listener.
 *
 * A call that began on another thread before this one may still finish after it
 * returns; keep the context alive until no release can be in flight.
 *
 * @return ::GSHSCENE_OK, ::GSHSCENE_ERR_INVALID (NULL, unknown token) or
 *   ::GSHSCENE_ERR_OOM.
 */
GSHSCENE_API GSHSCENE_Result gshscene_store_remove_release_listener(
    GSHSCENE_Store * store, uint64_t token);

/**
 * @brief Intern a path.
 *
 * The path is copied (the store holds its own).  If the store already has equal
 * content (bit for bit) with a reference held, that resource is returned and
 * nothing is copied.
 *
 * @param store The store.
 * @param path A path in 2D or 3D; the store never keeps a pointer to it.
 * @param out The handle, with a reference for the caller; written only on success.
 * @return ::GSHSCENE_OK, ::GSHSCENE_ERR_INVALID (NULL), ::GSHSCENE_ERR_LIMIT
 *   (`max_resource_elements`, `max_resources`) or ::GSHSCENE_ERR_OOM.  Likewise
 *   for the two below.
 */
GSHSCENE_API GSHSCENE_Result gshscene_store_intern_path(GSHSCENE_Store * store,
    const GSHGEOM_Path * path, const GSHSCENE_Resource ** out);

/**
 * @brief Intern a glyph run.
 *
 * The glyph array is copied; a glyph's outline is retained.  ::GSHSCENE_ERR_INVALID for any
 * field the rules in ::GSHSCENE_Glyph_Run forbid (a font field on an outline run, an outline that is
 * not a path resource of this store, a non-finite number).
 */
GSHSCENE_API GSHSCENE_Result gshscene_store_intern_glyph_run(GSHSCENE_Store * store,
    const GSHSCENE_Glyph_Run * run, const GSHSCENE_Resource ** out);

/**
 * @brief Freeze a closed list into a picture and intern it.
 *
 * The list is copied (items, and a reference on each resource they name).
 * ::GSHSCENE_ERR_INVALID for an open list or one made for another store;
 * ::GSHSCENE_ERR_LIMIT when the picture's depth passes `max_picture_depth` or
 * its expansion passes `max_expansion`.
 */
GSHSCENE_API GSHSCENE_Result gshscene_store_intern_picture(GSHSCENE_Store * store,
    const GSHSCENE_List * list, const GSHSCENE_Resource ** out);

/**
 * @brief Take another reference.
 *
 * @return @p resource (for chaining); NULL for NULL.  Only a handle with a
 *   reference held may be retained.
 */
GSHSCENE_API const GSHSCENE_Resource * gshscene_resource_retain(const GSHSCENE_Resource * resource);

/**
 * @brief Drop a reference; the last one frees the resource (and releases what it holds).
 *
 * NULL is ignored.  Release listeners are called at the last release.
 */
GSHSCENE_API void gshscene_resource_release(const GSHSCENE_Resource * resource);

/** @brief The resource's kind; ::GSHSCENE_RESOURCE_KIND_COUNT for NULL. */
GSHSCENE_API GSHSCENE_Resource_Kind gshscene_resource_kind(const GSHSCENE_Resource * resource);

/** @brief The resource's id, never issued twice by its store; 0 for NULL. */
GSHSCENE_API uint64_t gshscene_resource_id(const GSHSCENE_Resource * resource);

/** @brief The content hash, as the file comment spells it; the offset basis for NULL. */
GSHSCENE_API uint64_t gshscene_resource_hash(const GSHSCENE_Resource * resource);

/** @brief The accounted bytes of the resource (see ::GSHSCENE_BYTES_BASE); 0 for NULL. */
GSHSCENE_API size_t gshscene_resource_bytes(const GSHSCENE_Resource * resource);

/** @brief The current reference count, for tests (a racing thread makes it stale); 0 for NULL. */
GSHSCENE_API size_t gshscene_resource_refcount(const GSHSCENE_Resource * resource);

/**
 * @brief The store's copy of a path resource.
 *
 * @return The path, valid while a handle is held; NULL if @p resource is NULL or not a path.
 */
GSHSCENE_API const GSHGEOM_Path * gshscene_resource_path(const GSHSCENE_Resource * resource);

/**
 * @brief Read a glyph run resource back.
 *
 * @param out The run; `glyphs` points into the resource and is valid while a
 *   handle is held.  Written only on success.
 * @return ::GSHSCENE_OK or ::GSHSCENE_ERR_INVALID (NULL, not a glyph run).
 */
GSHSCENE_API GSHSCENE_Result gshscene_resource_glyph_run(
    const GSHSCENE_Resource * resource, GSHSCENE_Glyph_Run * out);

/** @brief The closed list of a picture resource; NULL if @p resource is NULL or not a picture. */
GSHSCENE_API const GSHSCENE_List * gshscene_resource_picture(const GSHSCENE_Resource * resource);

/** @brief A picture's depth (1 for one with no instance); 0 for anything else. */
GSHSCENE_API size_t gshscene_resource_picture_depth(const GSHSCENE_Resource * resource);

/**
 * @brief Write the resource as text (see the file comment).
 *
 * @return ::GSHSCENE_OK, ::GSHSCENE_ERR_INVALID (NULL) or ::GSHSCENE_ERR_IO.
 */
GSHSCENE_API GSHSCENE_Result gshscene_resource_dump(const GSHSCENE_Resource * resource, FILE * out);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GSHSCENE_RESOURCE_H */
