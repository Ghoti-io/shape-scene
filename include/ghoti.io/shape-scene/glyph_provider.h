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
 * @file glyph_provider.h
 *
 * The glyph path provider seam (AD-5, story 14a): how a sink reaches a font's
 * glyphs without including a font header, and how a font (`font`, whose
 * headers name no floating type) binds its faces to the waist.
 *
 * **This header is float-free**, on the same terms as shape-geometry's
 * `fill.h`: no floating type appears in it or in a header it includes, so a
 * `GFNT_` header can include it.  A gate preprocesses it and fails on a
 * `float`, `double` or `long double` token.  Everything here is integers.
 *
 * ### The face (D4)
 *
 * A **face** is a resource (`GSHSCENE_RESOURCE_FACE`).  Its content is the
 * nonzero `font_key` (for `font`, a hash of the font data), the face index in a
 * collection and the normalized variation coordinates as F2.14 (each in
 * `[-16384, 16384]`, at most ::GSHSCENE_FACE_MAX_AXES, trailing zeros
 * dropped, so equal instances are one handle).  It carries a **binding**, the
 * provider's ops and a context, which is not content: the first intern of a
 * content builds the face and keeps the binding it was given (retained by
 * `retain` then, released by `release` once, at the face's last release); an
 * intern of equal content later returns that face and ignores the binding it
 * was given.  A face with no binding is legal and **unbound**: a run that names
 * it is refused by the planning pass (`FACE_UNBOUND`).  The face's
 * information is asked once, when the face is built.
 *
 * The information carries the **identity** a writer needs to reference the font
 * rather than outline it (R1): the family and PostScript names, with the face
 * index and the coordinates of the resource and the font key; and `face_data`,
 * where the provider has it, the font's bytes for a writer that embeds them.
 * Keeping text as text (code points, positions) is a layout concern above this
 * seam.
 *
 * ### The ops (D5)
 *
 * Integers in both directions; any op may be called from any thread at once
 * (the provider locks its own caches); a provider keeps no pointer to an
 * argument past the call.
 *
 * - **`face_info`** (required): upem in `[16, 16384]`, the axis count, flags
 *   (::GSHSCENE_FACE_HINTING, ::GSHSCENE_FACE_COLOR), the largest ppem it
 *   hints (0 for ::GSHSCENE_GLYPH_MAX_PPEM), the glyph count and the names.
 * - **`glyph_outline`** (required): the unhinted outline of the instance, in
 *   26.6 design units, y up, given to the path the sink made through
 *   shape-geometry's fixed-point builder with ::GSHSCENE_GLYPH_OUTLINE_BITS
 *   fraction bits (`gshgeom_path_move_to_fixed` and its verbs, and
 *   `gshgeom_path_close`): lines, quadratics and cubics, each subpath begun by
 *   a move.  It is size-independent, and it is the **general route**'s input:
 *   any map applies after it.  **A GPU sink that keeps signed-distance or
 *   multi-channel signed-distance (MSDF) atlases takes this outline, not the
 *   device path (R2)**: a distance field is resampled at every size, and
 *   hinting belongs to one size.  So does a writer that emits curves.
 * - **`glyph_device_path`** (optional; without it every run takes the general
 *   route): the **fast tier**.  For the request's ppem, its snapped 16.16
 *   scale, its origin fraction in `[0, 64)` on each axis, its hint mode and its
 *   hint version, the y-down 26.6 device path of the glyph relative to the
 *   whole-pixel origin, given to the device path the sink made through
 *   shape-geometry's device path builder, with the hint status and the
 *   advance.  **Hinted** points (status `HINTED` or `WITH_ERRORS`) are the
 *   hinter's.  **Unhinted** points (mode `NONE`, status `NO_PROGRAM` or
 *   `PROGRAM_FAILED`) are normative: the elements of `glyph_outline`'s outline,
 *   in order, with the same verbs, each point `(x, y)` (26.6 design units)
 *   becoming `(fx + gshgeom_scale_26_6(x, scale, 0), fy +
 *   gshgeom_scale_26_6(y, scale, 1))`.  That is the general route under the
 *   **snapped map** (::gshscene_glyph_snap_map) bit for bit, which is AD-5's
 *   gate: with hinting off, `fast(map) == general(snap(map))`.  Under
 *   `FONT_STRICT` a program that fails is ::GSHSCENE_ERR_REFUSED (the sink
 *   refuses the glyph); a glyph the face cannot give is any other failure.
 * - **`glyph_box`** (required): the box of the outline's points, control
 *   points included, in 26.6 design units, y up; `empty` for a glyph with no
 *   outline.
 * - **`face_data`** (optional): the font's bytes, valid while the binding is
 *   retained.
 * - **`retain`**, **`release`** (optional): the binding's context, retained
 *   when a face is built with it and released at that face's last release.
 *
 * ### Sizes (D2, D3)
 *
 * The fast tier takes an integer device ppem in `[1, max_ppem]` and the scale
 * ::gshscene_glyph_scale gives it, `round(ppem * 65536 / upem)` (half up): the
 * scale `font` itself uses.  The route and the origin are glyph.h's.
 *
 * ### Cache keys (D8, normative)
 *
 * A cache of a provider's answers is keyed by the face's resource id and the
 * glyph, never by a run (a run is cheap to re-intern when a paragraph reflows,
 * and its glyphs still hit): an outline by `(face id, glyph)`, which is
 * size-independent; a device path by `(face id, glyph, ppem, hint mode, hint
 * version, fraction x, fraction y)`.  A cache drops a face's entries from a
 * release listener (resource.h).  shape-cpu keeps no cache across frames.
 *
 * ### Runs without floats
 *
 * ::gshscene_store_intern_glyph_run_fixed interns a run from a face with its
 * size and positions as `int32_t` values with 0 to 16 fraction bits, converted
 * exactly, so a front end that names no floating type records text.
 */

#ifndef GHOTI_IO_GSHSCENE_GLYPH_PROVIDER_H
#define GHOTI_IO_GSHSCENE_GLYPH_PROVIDER_H

#include <ghoti.io/shape-scene/core.h>
#include <ghoti.io/shape-scene/macros.h>
#include <ghoti.io/shape-scene/types.h>

#include <ghoti.io/shape-geometry/macros.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief A model path (shape-geometry's path.h).  Opaque here. */
typedef struct GSHGEOM_Path GSHGEOM_Path;

/** @brief A 26.6 device path (shape-geometry's device.h).  Opaque here. */
typedef struct GSHGEOM_Device_Path GSHGEOM_Device_Path;

/** @brief How a run asks to be hinted.  The values are pinned (hashed) and are `font`'s `GFNT_HintMode`'s. */
typedef enum GSHSCENE_Hint_Mode {
  GSHSCENE_HINT_FONT = 0,        ///< What the font asks; a face with no program, or a program that fails, is drawn unhinted.
  GSHSCENE_HINT_NONE = 1,        ///< Unhinted.  No report entry, ever.
  GSHSCENE_HINT_FONT_STRICT = 2, ///< As `FONT`, but a run that cannot be hinted, or a program that fails, is refused (D6).
  GSHSCENE_HINT_MODE_COUNT       ///< Closes the enum.
} GSHSCENE_Hint_Mode;

/** @brief What became of a glyph asked to be hinted.  The values are `font`'s `GFNT_HintStatus`'s. */
typedef enum GSHSCENE_Hint_Status {
  GSHSCENE_HINT_STATUS_HINTED = 0,         ///< Its program ran.
  GSHSCENE_HINT_STATUS_NO_PROGRAM = 1,     ///< It has none, or the font does not hint at this size.
  GSHSCENE_HINT_STATUS_PROGRAM_FAILED = 2, ///< A program failed; the outline is as drawn.
  GSHSCENE_HINT_STATUS_WITH_ERRORS = 3,    ///< A tolerant program erred and the outline is as far as it got.
  GSHSCENE_HINT_STATUS_COUNT               ///< Closes the enum.
} GSHSCENE_Hint_Status;

/** The largest device ppem the fast tier takes (and a face's `max_ppem` at most). */
#define GSHSCENE_GLYPH_MAX_PPEM 4096u
/** The smallest upem a face may state. */
#define GSHSCENE_FACE_UPEM_MIN 16u
/** The largest upem a face may state. */
#define GSHSCENE_FACE_UPEM_MAX 16384u
/** The most variation coordinates a face carries. */
#define GSHSCENE_FACE_MAX_AXES 64u
/** One in F2.14: the bound of a normalized coordinate's magnitude. */
#define GSHSCENE_F2DOT14_ONE 16384
/** The fraction bits of `glyph_outline`'s coordinates and of a box: 26.6 design units. */
#define GSHSCENE_GLYPH_OUTLINE_BITS 6u
/** The version of the ops table this header describes. */
#define GSHSCENE_GLYPH_PROVIDER_VERSION 1u

/** @name Face flags
 * @{ */
#define GSHSCENE_FACE_HINTING (UINT32_C(1) << 0) ///< The face has a hinting program.
#define GSHSCENE_FACE_COLOR (UINT32_C(1) << 1)   ///< The face has color glyphs (story 14c).
#define GSHSCENE_FACE_FLAGS_ALL (GSHSCENE_FACE_HINTING | GSHSCENE_FACE_COLOR) ///< Every flag defined.
/** @} */

/** @brief A face as an op sees it: the face resource's content.  Valid for the call. */
typedef struct GSHSCENE_Face {
  uint64_t font_key;      ///< Nonzero.
  uint32_t face_index;    ///< The face in a collection.
  uint32_t coord_count;   ///< The variation coordinates (trailing zeros dropped); 0 is the default instance.
  const int16_t * coords; ///< F2.14, each in `[-16384, 16384]`; NULL when @p coord_count is 0.
} GSHSCENE_Face;

/** @brief What a face is (`face_info`). */
typedef struct GSHSCENE_Face_Info {
  uint32_t upem;                ///< Units per em, in `[16, 16384]`.
  uint32_t axis_count;          ///< The variation axes of the font.
  uint32_t flags;               ///< `GSHSCENE_FACE_*`.
  uint32_t max_ppem;            ///< The largest ppem it hints; 0 is ::GSHSCENE_GLYPH_MAX_PPEM.  At most that.
  uint32_t glyph_count;         ///< The glyphs; an id at or past it is unavailable.
  const char * family_name;     ///< UTF-8, or NULL: R1's identity.  Valid while the binding is retained.
  const char * postscript_name; ///< Likewise.
  uint8_t _reserved[16];        ///< Zero.
} GSHSCENE_Face_Info;

/** @brief A glyph's design box, in 26.6 design units, y up (`glyph_box`). */
typedef struct GSHSCENE_Glyph_Box {
  int32_t x_min; ///< Left.
  int32_t y_min; ///< Bottom.
  int32_t x_max; ///< Right.
  int32_t y_max; ///< Top.
  uint32_t empty; ///< Nonzero for a glyph with no outline (then the four are zero).
} GSHSCENE_Glyph_Box;

/** @brief What the fast tier asks for one glyph (`glyph_device_path`). */
typedef struct GSHSCENE_Glyph_Request {
  uint32_t glyph;        ///< The glyph id.
  uint32_t ppem;         ///< The device ppem, in `[1, max_ppem]`.
  int32_t scale;         ///< The snapped 16.16 scale per design unit: ::gshscene_glyph_scale of the ppem and the upem.
  uint32_t frac_x;       ///< The origin's 26.6 fraction in x, `[0, 64)`.
  uint32_t frac_y;       ///< Likewise in y (down).
  uint32_t hint_mode;    ///< A ::GSHSCENE_Hint_Mode.
  uint32_t hint_version; ///< The run's, passed unread (for `font`, a `GFNT_HintVersion`; 0 is its default).
  uint8_t _reserved[16]; ///< Zero.
} GSHSCENE_Glyph_Request;

/** @brief What the fast tier says of a glyph besides its path. */
typedef struct GSHSCENE_Glyph_Answer {
  uint32_t status; ///< A ::GSHSCENE_Hint_Status (ignored under mode `NONE`).
  int32_t advance; ///< The advance in x, 26.6 pixels (hinted where hinted).
} GSHSCENE_Glyph_Answer;

/** @brief A provider's operations (see the file comment).  Copied by the face. */
typedef struct GSHSCENE_Glyph_Provider {
  uint32_t version; ///< ::GSHSCENE_GLYPH_PROVIDER_VERSION.
  /** Required.  The face's information; ::GSHSCENE_OK or a failure, which fails the intern. */
  GSHSCENE_Result (*face_info)(void * ctx, const GSHSCENE_Face * face, GSHSCENE_Face_Info * out);
  /** Required.  The unhinted outline into @p out (a 2D path the caller made). */
  GSHSCENE_Result (*glyph_outline)(void * ctx, const GSHSCENE_Face * face, uint32_t glyph, GSHGEOM_Path * out);
  /** Optional: the fast tier.  The device path into @p out (empty, made by the caller), and @p answer. */
  GSHSCENE_Result (*glyph_device_path)(void * ctx, const GSHSCENE_Face * face, const GSHSCENE_Glyph_Request * request,
      GSHGEOM_Device_Path * out, GSHSCENE_Glyph_Answer * answer);
  /** Required.  The design box. */
  GSHSCENE_Result (*glyph_box)(void * ctx, const GSHSCENE_Face * face, uint32_t glyph, GSHSCENE_Glyph_Box * out);
  /** Optional.  The font's bytes. */
  GSHSCENE_Result (*face_data)(void * ctx, const GSHSCENE_Face * face, const uint8_t ** data, size_t * size);
  /** Optional.  Take a reference on the context. */
  void (*retain)(void * ctx);
  /** Optional.  Drop it. */
  void (*release)(void * ctx);
} GSHSCENE_Glyph_Provider;

/** @brief What a face is interned from. */
typedef struct GSHSCENE_Face_Desc {
  uint64_t font_key;                        ///< Nonzero.
  uint32_t face_index;                      ///< The face in a collection.
  uint32_t coord_count;                     ///< At most ::GSHSCENE_FACE_MAX_AXES.
  const int16_t * coords;                   ///< F2.14 in `[-16384, 16384]`; may be NULL only when @p coord_count is 0.
  const GSHSCENE_Glyph_Provider * provider; ///< The binding's ops (copied), or NULL for an unbound face.
  void * ctx;                               ///< The binding's context.
} GSHSCENE_Face_Desc;

/**
 * @brief Intern a face (see the file comment).
 *
 * @param out The handle, with a reference for the caller; written only on success.
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID (NULL, a zero key, a coordinate out of range, an ops table with a
 *   required op missing or another version, information past its bounds); ::GSHSCENE_ERR_LIMIT (more than
 *   ::GSHSCENE_FACE_MAX_AXES coordinates, the store's limits); ::GSHSCENE_ERR_OOM; or what `face_info` failed with.
 */
GSHSCENE_API GSHSCENE_Result gshscene_store_intern_face(
    GSHSCENE_Store * store, const GSHSCENE_Face_Desc * desc, const GSHSCENE_Resource ** out);

/**
 * @brief A face resource's content.
 *
 * @param out Written only on success; its coordinates point into the resource, valid while a handle is held.
 * @return ::GSHSCENE_OK or ::GSHSCENE_ERR_INVALID (NULL, not a face).
 */
GSHSCENE_API GSHSCENE_Result gshscene_resource_face(const GSHSCENE_Resource * resource, GSHSCENE_Face * out);

/**
 * @brief A bound face's information, as `face_info` gave it when the face was built.
 *
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID (NULL, not a face, unbound).
 */
GSHSCENE_API GSHSCENE_Result gshscene_face_info(const GSHSCENE_Resource * resource, GSHSCENE_Face_Info * out);

/**
 * @brief A face's binding: its copy of the ops and the context.
 *
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID (NULL, not a face, unbound).
 */
GSHSCENE_API GSHSCENE_Result gshscene_face_binding(
    const GSHSCENE_Resource * resource, const GSHSCENE_Glyph_Provider ** ops, void ** ctx);

/**
 * @brief The snapped 16.16 scale of the fast tier: `round(ppem * 65536 / upem)`, half up (D2).
 *
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID (NULL, @p ppem not in `[1, GSHSCENE_GLYPH_MAX_PPEM]`, @p upem not
 *   in `[16, 16384]`).
 */
GSHSCENE_API GSHSCENE_Result gshscene_glyph_scale(uint32_t ppem, uint32_t upem, int32_t * out);

/** @brief A glyph of a run made without floats: its id and its position, fixed point. */
typedef struct GSHSCENE_Glyph_Fixed {
  uint32_t id; ///< The glyph id.
  int32_t x;   ///< The origin in run space, `x / 2^bits`.
  int32_t y;   ///< Likewise; down is positive.
} GSHSCENE_Glyph_Fixed;

/**
 * @brief Intern a run from a face, every number fixed point: the size `size / 2^bits` (greater than zero) and each
 *   position `/ 2^bits`, converted exactly, `bits` from 0 to 16.  Otherwise as `gshscene_store_intern_glyph_run`
 *   (resource.h) with the source `GLYPHS_FROM_FONT`.
 *
 * @return As that, and ::GSHSCENE_ERR_INVALID for @p bits past 16 or a size not above zero.
 */
GSHSCENE_API GSHSCENE_Result gshscene_store_intern_glyph_run_fixed(GSHSCENE_Store * store,
    const GSHSCENE_Resource * face, int32_t size, uint32_t hint_mode, uint32_t hint_version, unsigned bits,
    size_t glyph_count, const GSHSCENE_Glyph_Fixed * glyphs, const GSHSCENE_Resource ** out);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GSHSCENE_GLYPH_PROVIDER_H */
