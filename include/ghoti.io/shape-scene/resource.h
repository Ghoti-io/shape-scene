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
 * A **resource** is a path, a glyph run, a picture (a closed list, frozen),
 * a face (glyph_provider.h: a font's face and variation instance, which a run
 * from a font names), a palette (a table of colors, which a palette paint's
 * entry indexes; paint.h, N14) or a gradient (a color line over a geometry,
 * which a gradient paint names; paint.h, G1 to G13); one further kind, an
 * image, is named and reserved.  A
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
 * ### The hash of a resource
 *
 * The **content hash**, in the spelling of shape-geometry's hash.h.  Neither
 * the id, nor the pointer, nor the allocator, nor the limits, nor a face's
 * binding are part of it.  Each stream starts with `u32` the format of its
 * kind and `u32` the resource's kind (::GSHSCENE_Resource_Kind, pinned).  The
 * format is per kind, and every kind's is 1 until the suite's first release
 * (a change before it changes format 1 in place).  It goes on:
 *
 * - **path** (1): `u64` the path's own hash (`gshgeom_path_hash`).
 * - **glyph run** (1): `u32` the source, `u64` the content hash of the face
 *   (0 for a run from outlines), `f64` size, `u32` hint mode, `u32` hint
 *   version, `u64` glyph count; then for each glyph `u32` id, `f64` x, `f64` y
 *   and `u64` the content hash of the outline resource (0 for a glyph with
 *   none).
 * - **picture** (1): `u64` the hash of its list (list.h).
 * - **face** (1): `u64` font key, `u32` face index, `u32` coordinate count and
 *   `u32` each coordinate (its `int16_t` value sign-extended to 32 bits).
 * - **palette** (1): `u64` the entry count, then per entry four `f64` (straight
 *   `r, g, b, a`) and its space (`u8` primaries, `u8` transfer, `f64` gamma, as
 *   list.h spells a space).
 * - **gradient** (1): `u8` kind, `u8` extend, the interpolation space, `u8`
 *   premultiplied, `u64` the map's hash (`gshgeom_map_hash`); the geometry
 *   (linear: six `f64`, `p0`, `p1`, `d`; radial: six `f64`, `c0`, `r0`, `c1`,
 *   `r1`; sweep: two `f64` the center, `u8` the unit, two `f64` `a0`, `a1`);
 *   `u64` the stop count; then per stop, in sorted order, `f64` offset and its
 *   paint as list.h spells a fill's (`u8` kind, four `f64`, a space, `u32`
 *   entry).  The stops are hashed as given: nothing converted is.
 *
 * The hash is of the content, not of the reference to it: a glyph run hashes
 * its face's and its outlines' content hashes, so two stores that were given
 * the same content print the same hash.
 *
 * ### The dump
 *
 * `gshscene.resource F kind=<name> hash=<16 hex digits>`, F the kind's format,
 * and then the payload as text, every `double` as the 16 hexadecimal digits of
 * its bit pattern and every hash as 16 digits: a path is `gshgeom_path_dump`'s
 * own text; a glyph run is a line `run source=S face=<hex> size=<hex> hint=N
 * version=N glyphs=N` and a line `g id=N x=<hex> y=<hex> outline=<hex>` per
 * glyph; a picture is `gshscene_list_dump`'s text; a face is a line `face
 * font=<hex> index=N coords=N` followed, on the same line, by ` C` for each
 * coordinate in signed decimal, and a line `bound=B upem=N flags=N` (B 0 for an
 * unbound face, whose upem and flags are 0); a palette is a line `palette
 * entries=N` and a line `p <r> <g> <b> <a> <space>` per entry, a space spelled
 * `P:T:<gamma hex>` as in list.h; a gradient is a line `gradient kind=K
 * extend=E space=<space> premul=N map=<hash> stops=N`, a line of its geometry
 * (`linear p0=<x> <y> p1=<x> <y> d=<x> <y>`, `radial c0=<x> <y> r0=<r> c1=<x> <y>
 * r1=<r>` or `sweep c=<x> <y> unit=U a0=<a> a1=<a>`) and a line `s <offset>
 * paint=K <r> <g> <b> <a> space=<space> entry=E` per stop.
 */

#ifndef GHOTI_IO_GSHSCENE_RESOURCE_H
#define GHOTI_IO_GSHSCENE_RESOURCE_H

#include <ghoti.io/shape-scene/allocator.h>
#include <ghoti.io/shape-scene/core.h>
#include <ghoti.io/shape-scene/glyph_provider.h>
#include <ghoti.io/shape-scene/macros.h>
#include <ghoti.io/shape-scene/paint.h>
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
  GSHSCENE_RESOURCE_GRADIENT = 4,  ///< A gradient: a color line over a geometry, which a gradient paint names (paint.h, G1).
  GSHSCENE_RESOURCE_IMAGE = 5,     ///< Reserved for the raster stories: no constructor yet.
  GSHSCENE_RESOURCE_FACE = 6,      ///< A font's face and variation instance, bound to a glyph path provider (glyph_provider.h).
  GSHSCENE_RESOURCE_PALETTE = 7,   ///< A table of colors, which a palette paint's entry indexes (paint.h, N14).
  GSHSCENE_RESOURCE_KIND_COUNT     ///< Closes the enum.
} GSHSCENE_Resource_Kind;

/**
 * @brief Name a resource kind.
 *
 * @param kind The kind, including a value outside the enum.
 * @return A static string, never NULL: "path", "glyph-run", "picture",
 *   "gradient", "image", "face", "palette", or "unknown resource kind".
 */
GSHSCENE_API const char * gshscene_resource_kind_string(GSHSCENE_Resource_Kind kind);

/** @brief Where a glyph run's glyphs come from.  The values are pinned (they are hashed). */
typedef enum GSHSCENE_Glyph_Source {
  /** The glyph is named by a face resource and an id; the face's glyph path provider gives its outline (glyph.h). */
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
  /** From font: a face resource of the same store (glyph_provider.h), retained by the run.  From outlines: NULL. */
  const GSHSCENE_Resource * face;
  /** From font: the em size in model units, finite, greater than zero, at most `GSHGEOM_COORD_LIMIT`.  From outlines: 0. */
  double size;
  /** From font: a ::GSHSCENE_Hint_Mode; 0 (`FONT`) is the font's own request.  From outlines: 0. */
  uint32_t hint_mode;
  /** From font: the hint version, passed to the provider unread; 0 is its default.  From outlines: 0. */
  uint32_t hint_version;
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
 * per-item figure for each item of its list, for a face the per-axis
 * figure for each coordinate, for a palette the per-color figure for each
 * entry, and for a gradient the per-stop figure for each stop.  What a resource holds by handle (a run's face,
 * a glyph's outline, an instanced picture) is a resource of its own and is
 * counted there.  An intern that would pass the limit is
 * ::GSHSCENE_ERR_LIMIT and builds and keeps nothing; a hit costs nothing; the
 * last release gives the bytes back.
 * @{
 */
#define GSHSCENE_BYTES_BASE ((size_t)128)        ///< Accounted bytes of any resource.
#define GSHSCENE_BYTES_PER_PATH_ELEMENT ((size_t)160) ///< Added for each element of a path.
#define GSHSCENE_BYTES_PER_GLYPH ((size_t)32)    ///< Added for each glyph of a run.
#define GSHSCENE_BYTES_PER_ITEM ((size_t)256)    ///< Added for each item of a picture's list.
#define GSHSCENE_BYTES_PER_AXIS ((size_t)8)      ///< Added for each variation coordinate of a face.
#define GSHSCENE_BYTES_PER_COLOR ((size_t)48)    ///< Added for each entry of a palette.
#define GSHSCENE_BYTES_PER_STOP ((size_t)96)     ///< Added for each stop of a gradient.
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
 * The glyph array is copied; the face and a glyph's outline are retained.  ::GSHSCENE_ERR_INVALID for any
 * field the rules in ::GSHSCENE_Glyph_Run forbid (a font field on an outline run, a face or an outline that is
 * not a resource of the right kind of this store, a hint mode outside its enum, a non-finite number).
 */
GSHSCENE_API GSHSCENE_Result gshscene_store_intern_glyph_run(GSHSCENE_Store * store,
    const GSHSCENE_Glyph_Run * run, const GSHSCENE_Resource ** out);

/**
 * @brief Freeze a closed list into a picture and intern it.
 *
 * The list is copied (items, ties, and a reference on each resource they name).  A picture has no
 * foreground and no palette of its own (N10, N14): a foreground or palette paint in it resolves against
 * the list that draws it, so a list whose foreground is not the default, or that has a palette, is
 * ::GSHSCENE_ERR_INVALID here.
 * ::GSHSCENE_ERR_INVALID for an open list or one made for another store;
 * ::GSHSCENE_ERR_LIMIT when the picture's depth passes `max_picture_depth` or
 * its expansion passes `max_expansion`.
 */
GSHSCENE_API GSHSCENE_Result gshscene_store_intern_picture(GSHSCENE_Store * store,
    const GSHSCENE_List * list, const GSHSCENE_Resource ** out);

/** The most entries a palette holds: an entry is in `[0, 0xFFFE]` (paint.h), so 65,535 of them. */
#define GSHSCENE_PALETTE_MAX_ENTRIES 65535u

/**
 * @brief Intern a palette: @p count colors, copied.
 *
 * Each color is checked as a foreground is (::gshscene_color_check: finite components, `a` in `[0, 1]`, a space
 * this library carries).  A palette of no entries is legal (every palette paint against it is missing).  An 8-bit
 * table (a font's CPAL) is interned without floats by `gshscene_store_intern_palette_rgba8` (fixed.h).
 *
 * @param colors The colors; may be NULL only when @p count is 0.
 * @param out The handle, with a reference for the caller; written only on success.
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID (NULL, an invalid color); ::GSHSCENE_ERR_UNSUPPORTED (a reserved
 *   space); ::GSHSCENE_ERR_LIMIT (more than ::GSHSCENE_PALETTE_MAX_ENTRIES, the store's limits); ::GSHSCENE_ERR_OOM.
 */
GSHSCENE_API GSHSCENE_Result gshscene_store_intern_palette(GSHSCENE_Store * store, size_t count,
    const GSHSCENE_Color * colors, const GSHSCENE_Resource ** out);

/** @brief How many entries a palette resource has; 0 for NULL and for anything else. */
GSHSCENE_API size_t gshscene_resource_palette_count(const GSHSCENE_Resource * resource);

/**
 * @brief One entry of a palette resource.
 *
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID (NULL, not a palette, @p index at or past the count).
 */
GSHSCENE_API GSHSCENE_Result gshscene_resource_palette_color(
    const GSHSCENE_Resource * resource, size_t index, GSHSCENE_Color * out);

/**
 * @brief Intern a gradient (paint.h, G1 to G3): its content is copied and its stops sorted stably by offset.
 *
 * Nothing is converted (G3): the stops are kept as given, and ::gshscene_gradient_resolve converts them at draw
 * time.  A degenerate gradient (G10) is legal.
 *
 * @param gradient The content.  Each stop's color is checked as a fill's paint is (list.h), and must not be a
 *   gradient; a solid stop in the target's (zero) space needs a gradient whose `space` is zero.
 * @param out The handle, with a reference for the caller; written only on success.
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID (NULL, a kind, extend, unit or flag outside its range, a non-finite
 *   double, a nonzero field of another kind, an invalid or 3D map, an invalid stop); ::GSHSCENE_ERR_UNSUPPORTED (a
 *   reserved space); ::GSHSCENE_ERR_LIMIT (more stops than `max_resource_elements`, the store's other limits);
 *   ::GSHSCENE_ERR_OOM.
 */
GSHSCENE_API GSHSCENE_Result gshscene_store_intern_gradient(GSHSCENE_Store * store, const GSHSCENE_Gradient * gradient,
    const GSHSCENE_Resource ** out);

/**
 * @brief Read a gradient resource back.
 *
 * @param out The content as stored (G2: the stops sorted); `stops` points into the resource and is valid while a
 *   handle is held.  Written only on success.
 * @return ::GSHSCENE_OK or ::GSHSCENE_ERR_INVALID (NULL, not a gradient).
 */
GSHSCENE_API GSHSCENE_Result gshscene_resource_gradient(const GSHSCENE_Resource * resource, GSHSCENE_Gradient * out);

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
