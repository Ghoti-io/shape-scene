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
 * @file fixed.h
 *
 * Recording without floats (story 14c): the calls a front end whose headers
 * name no floating type (`font`, AD-5) records a list with, the palette's and
 * the gradient's integer constructors, and the truncation that makes a lowering
 * atomic.
 *
 * **This header is float-free**, on the same terms as glyph_provider.h: no
 * floating type appears in it or in a header of the suite it includes, and the
 * same gate (`make check-float-free`) reads it.  Every number is an `int32_t`
 * with a stated count of fraction bits, `v / 2^bits`, which converts to a
 * `double` exactly.  The calls are the record calls of list.h with the same
 * rules and the same failures; what they add is said here.
 *
 * ### A design map (N14's companion: how a lowering places a glyph)
 *
 * A fill or a clip push of a **run from a font** may carry a design map: a
 * chain of ops, `ops[0]` the outermost, each an affine map of the face's design
 * space (units, y up).  The ops (every value `v[i] / 2^bits`):
 *
 * | Kind | Values | Map |
 * | --- | --- | --- |
 * | `AFFINE` | `xx yx xy yy dx dy` | `x' = xx x + xy y + dx`, `y' = yx x + yy y + dy` |
 * | `TRANSLATE` | `dx dy` | `x' = x + dx`, `y' = y + dy` |
 * | `SCALE` | `sx sy cx cy` | a scale by `sx, sy` about the center `(cx, cy)` |
 * | `ROTATE` | `a cx cy` | a rotation by `a` turns, from +x toward +y (counterclockwise, y up), about the center |
 * | `SKEW` | `ax ay cx cy` | `x' = x - tan(ax) y`, `y' = tan(ay) x + y`, angles in turns, about the center |
 *
 * Each op's map is shape-geometry's: `gshgeom_map_from_matrix` for the affine,
 * the scale and the skew's linear part, `gshgeom_map_translate`, and
 * `gshgeom_map_rotate` in turns (so a quarter turn is exact); about a center
 * `c` the op is `translate(c) . M . translate(-c)`, and `tan(a)` is the sine
 * over the cosine of `gshgeom_map_rotate(TURNS, a)` (its entries `m[1][0] /
 * m[0][0]`; a cosine of zero is ::GSHSCENE_ERR_INVALID).  The chain is `T =
 * ops[0] . ops[1] . ... . ops[n-1]`, composed by `gshgeom_map_compose_chain`.
 *
 * The item's map is then `T` carried into run space about the run's **first
 * glyph**, at `g` (run space, y down), at the run's scale `s = size / upem` (the
 * face's upem, as `face_info` gave it).  With `T = (A, b)`: when `A` is exactly
 * the identity the item map is `gshgeom_map_translate(s b_x, -(s b_y))`;
 * otherwise it is `gshgeom_map_compose_chain` of `translate(g)`, `flip_y`,
 * `(A, (s b_x, s b_y))` (from the matrix), `flip_y` and `translate(-g)`.  No
 * inverse is formed (the scale cancels exactly from the linear part).  So a
 * glyph drawn by the run is drawn as its design outline under `T`, placed where
 * the run places it.  The ops need a run from a font whose face is bound and a
 * glyph to be about; given for any other resource they are
 * ::GSHSCENE_ERR_INVALID.  No ops is the identity.
 *
 * ### A paint
 *
 * ::GSHSCENE_Paint_Fixed is a foreground or a palette paint (paint.h, N10 and
 * N14) with its alpha `alpha / 2^bits`, in `[0, 1]`.  A palette entry of
 * `0xFFFF`, a font's foreground, **becomes the foreground paint**: this is
 * where `0xFFFF` is lowered, so every front end lowers it the same way.
 *
 * ### A gradient (color-paint story P1, G1 to G13 of paint.h)
 *
 * ::gshscene_store_intern_gradient_fixed interns a gradient whose numbers are
 * COLRv1's, placed for a **run from a font** as a fill of that run is: in the
 * face's design space (units, y up) under a design map of ops (above), about the
 * run's first glyph `g` at the run's scale `s = size / upem`.  The gradient map is
 * `gshgeom_map_compose_chain` of `gshgeom_map_design_to_model(s, g_x, g_y)` and
 * `T`, the ops' chain (no ops: `T` is the identity): a design point lands where
 * the run's glyph outline puts it, and a fill's own design map composes in front
 * of it as the item map.  Every value is `v / 2^bits`, exactly:
 *
 * | Kind | `v[0..5]` | The gradient |
 * | --- | --- | --- |
 * | `LINEAR` | `x0 y0 x1 y1 x2 y2` | `p0 = (x0, y0)`, `p1 = (x1, y1)`, `d = (x2 - x0, y2 - y0)` (COLRv1's rotation point, G7) |
 * | `RADIAL` | `x0 y0 r0 x1 y1 r1` | the circles `(x0, y0), r0` and `(x1, y1), r1` |
 * | `SWEEP` | `cx cy a0 a1 0 0` | the center, and the angles in half turns (COLRv1's: 1.0 is 180 degrees), stored as `v * 180 / 2^bits` degrees |
 *
 * Each difference and product is exact in a double (an `int32_t` has 31 bits).
 * The space is primaries and transfer (linear or sRGB) with no gamma; COLRv1's
 * is sRGB primaries, linear transfer, premultiplied 1.  Each stop is an offset
 * `offset / 2^bits` and a ::GSHSCENE_Paint_Fixed (an entry of `0xFFFF` is the
 * foreground); the store sorts them stably.  The rules and failures are
 * `gshscene_store_intern_gradient`'s.
 *
 * ### Atomic lowering
 *
 * ::gshscene_list_truncate drops the items from a count on, as if they had not
 * been recorded (their references are released; the arena keeps its memory
 * until a reset).  A front end that lowers one construct into several items
 * reads ::gshscene_list_count first and truncates back to it when it refuses
 * the construct or a call fails, so the list holds all of it or none.
 */

#ifndef GHOTI_IO_GSHSCENE_FIXED_H
#define GHOTI_IO_GSHSCENE_FIXED_H

#include <ghoti.io/shape-scene/core.h>
#include <ghoti.io/shape-scene/macros.h>
#include <ghoti.io/shape-scene/types.h>

#include <ghoti.io/shape-geometry/fill.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief What a design-map op is.  The values are pinned. */
typedef enum GSHSCENE_Fixed_Op_Kind {
  GSHSCENE_FIXED_AFFINE = 0,    ///< `v[0..5]`: `xx yx xy yy dx dy`.
  GSHSCENE_FIXED_TRANSLATE = 1, ///< `v[0..1]`: `dx dy`.
  GSHSCENE_FIXED_SCALE = 2,     ///< `v[0..3]`: `sx sy cx cy`.
  GSHSCENE_FIXED_ROTATE = 3,    ///< `v[0..2]`: the angle in turns, `cx cy`.
  GSHSCENE_FIXED_SKEW = 4,      ///< `v[0..3]`: the x and y skew angles in turns, `cx cy`.
  GSHSCENE_FIXED_OP_COUNT       ///< Closes the enum.
} GSHSCENE_Fixed_Op_Kind;

/** The most fraction bits of a fixed value here. */
#define GSHSCENE_FIXED_MAX_BITS 30u
/** The most ops of a design map. */
#define GSHSCENE_FIXED_MAX_OPS 64u

/** @brief One op of a design map (see the file comment). */
typedef struct GSHSCENE_Fixed_Op {
  uint32_t kind; ///< A ::GSHSCENE_Fixed_Op_Kind.
  uint32_t bits; ///< The fraction bits of every value, at most ::GSHSCENE_FIXED_MAX_BITS.
  int32_t v[6];  ///< The values; the ones a kind does not use are zero (and required to be).
} GSHSCENE_Fixed_Op;

/** The foreground paint kind (`GSHSCENE_PAINT_FOREGROUND`, paint.h; a test pins the two together). */
#define GSHSCENE_FIXED_PAINT_FOREGROUND 2u
/** The palette paint kind (`GSHSCENE_PAINT_PALETTE`, paint.h). */
#define GSHSCENE_FIXED_PAINT_PALETTE 3u
/** The palette entry that is the foreground (a font's): it becomes ::GSHSCENE_FIXED_PAINT_FOREGROUND. */
#define GSHSCENE_FIXED_ENTRY_FOREGROUND 0xFFFFu

/** @brief A foreground or palette paint, its alpha fixed point. */
typedef struct GSHSCENE_Paint_Fixed {
  uint32_t kind;  ///< ::GSHSCENE_FIXED_PAINT_FOREGROUND or ::GSHSCENE_FIXED_PAINT_PALETTE.
  uint32_t entry; ///< Palette: the entry, `[0, 0xFFFF]` (`0xFFFF` is the foreground).  Foreground: 0.
  int32_t alpha;  ///< The alpha multiplier, `alpha / 2^bits`, in `[0, 1]`.
  uint32_t bits;  ///< At most ::GSHSCENE_FIXED_MAX_BITS.
} GSHSCENE_Paint_Fixed;

/** The gradient kinds (`GSHSCENE_GRADIENT_*`, paint.h; a test pins them together). */
#define GSHSCENE_FIXED_GRADIENT_LINEAR 1u
#define GSHSCENE_FIXED_GRADIENT_RADIAL 2u
#define GSHSCENE_FIXED_GRADIENT_SWEEP 3u

/** @brief One stop of a fixed gradient. */
typedef struct GSHSCENE_Gradient_Stop_Fixed {
  int32_t offset;              ///< `offset / 2^bits`.
  uint32_t bits;               ///< At most ::GSHSCENE_FIXED_MAX_BITS.
  GSHSCENE_Paint_Fixed color;  ///< A foreground or palette paint (`0xFFFF` is the foreground).
} GSHSCENE_Gradient_Stop_Fixed;

/** @brief A gradient in integers (see the file comment). */
typedef struct GSHSCENE_Gradient_Fixed {
  uint32_t kind;          ///< ::GSHSCENE_FIXED_GRADIENT_LINEAR, `_RADIAL` or `_SWEEP`.
  uint32_t extend;        ///< 0 pad, 1 repeat, 2 reflect (`GSHSCENE_Extend`).
  uint32_t bits;          ///< The fraction bits of @p v, at most ::GSHSCENE_FIXED_MAX_BITS.
  int32_t v[6];           ///< The geometry (see the table); the values a kind does not use are zero.
  uint32_t primaries;     ///< A `GCOL_Primaries` value: sRGB, Adobe RGB, Display P3 or BT.2020.
  uint32_t transfer;      ///< A `GCOL_Transfer` value: linear or sRGB.
  uint32_t premultiplied; ///< 0 or 1 (G4).
  const GSHSCENE_Fixed_Op * ops; ///< The design map of the gradient (`T`), or NULL with @p op_count 0.
  size_t op_count;        ///< At most ::GSHSCENE_FIXED_MAX_OPS.
  size_t stop_count;      ///< The stops.
  const GSHSCENE_Gradient_Stop_Fixed * stops; ///< NULL only with no stops.
} GSHSCENE_Gradient_Fixed;

/**
 * @brief Intern a gradient (resource.h's `gshscene_store_intern_gradient`) from integers, placed for @p run (see the
 *   file comment).
 *
 * @param run A run from a bound face with a glyph, of @p store.
 * @return As `gshscene_store_intern_gradient`; ::GSHSCENE_ERR_INVALID for a value outside its rules, a run that is not
 *   one from a bound face with a glyph, or a NULL; ::GSHSCENE_ERR_LIMIT for more than ::GSHSCENE_FIXED_MAX_OPS ops or
 *   a map past shape-geometry's bounds.
 */
GSHSCENE_API GSHSCENE_Result gshscene_store_intern_gradient_fixed(GSHSCENE_Store * store, const GSHSCENE_Resource * run,
    const GSHSCENE_Gradient_Fixed * gradient, const GSHSCENE_Resource ** out);

/**
 * @brief Record a fill (list.h's `gshscene_list_fill`) with a design map, a fixed paint, and depth 0.
 *
 * @param ops The design map, or NULL with @p op_count 0 for the identity.
 * @return As `gshscene_list_fill`; ::GSHSCENE_ERR_INVALID for an op or a paint outside its rules, ops on a resource
 *   that is not a run from a bound face with a glyph, a NULL paint; ::GSHSCENE_ERR_LIMIT for more than
 *   ::GSHSCENE_FIXED_MAX_OPS ops or a map past shape-geometry's bounds.
 */
GSHSCENE_API GSHSCENE_Result gshscene_list_fill_fixed(GSHSCENE_List * list, const GSHSCENE_Resource * resource,
    const GSHSCENE_Fixed_Op * ops, size_t op_count, GSHGEOM_Fill_Rule rule, const GSHSCENE_Paint_Fixed * paint,
    int32_t layer);

/**
 * @brief Record a fill with a design map whose paint is @p gradient (a gradient resource of the list's store, G1) with
 *   the alpha multiplier `alpha / 2^bits`, in `[0, 1]`, at depth 0.
 *
 * @return As ::gshscene_list_fill_fixed.
 */
GSHSCENE_API GSHSCENE_Result gshscene_list_fill_gradient_fixed(GSHSCENE_List * list, const GSHSCENE_Resource * resource,
    const GSHSCENE_Fixed_Op * ops, size_t op_count, GSHGEOM_Fill_Rule rule, const GSHSCENE_Resource * gradient,
    int32_t alpha, uint32_t bits, int32_t layer);

/** @brief Begin a clip (list.h's `gshscene_list_clip_push`) with a design map, at depth 0. */
GSHSCENE_API GSHSCENE_Result gshscene_list_clip_push_fixed(GSHSCENE_List * list, const GSHSCENE_Resource * resource,
    const GSHSCENE_Fixed_Op * ops, size_t op_count, GSHGEOM_Fill_Rule rule, int32_t layer);

/**
 * @brief Begin a group (list.h's `gshscene_list_group_push`) of opacity 1, the enclosing blend space, flat order,
 *   composited by @p composite (a `GSHSCENE_Composite`, N4), at depth 0.
 */
GSHSCENE_API GSHSCENE_Result gshscene_list_group_push_fixed(GSHSCENE_List * list, uint32_t composite, int32_t layer);

/** @brief End the innermost clip (list.h, declared here too). */
GSHSCENE_API GSHSCENE_Result gshscene_list_clip_pop(GSHSCENE_List * list);

/** @brief End the innermost group (list.h, declared here too). */
GSHSCENE_API GSHSCENE_Result gshscene_list_group_pop(GSHSCENE_List * list);

/** @brief Drop a reference to a resource (resource.h, declared here too): a lowering releases the runs it interns. */
GSHSCENE_API void gshscene_resource_release(const GSHSCENE_Resource * resource);

/** @brief How many items (list.h, declared here too); 0 for NULL. */
GSHSCENE_API size_t gshscene_list_count(const GSHSCENE_List * list);

/**
 * @brief Drop the items from @p count on, as if they had not been recorded (see the file comment).
 *
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID (NULL, a closed list, @p count past the list's count).
 */
GSHSCENE_API GSHSCENE_Result gshscene_list_truncate(GSHSCENE_List * list, size_t count);

/**
 * @brief Intern a palette (resource.h) from straight 8-bit sRGB, as a font's `CPAL` stores it.
 *
 * Entry `i`'s components are `rgba[4i]` to `rgba[4i + 3]` (red, green, blue, alpha); each becomes `c / 255`, the
 * double nearest, and the space is sRGB (primaries and transfer).
 *
 * @return As `gshscene_store_intern_palette`.
 */
GSHSCENE_API GSHSCENE_Result gshscene_store_intern_palette_rgba8(
    GSHSCENE_Store * store, size_t count, const uint8_t * rgba, const GSHSCENE_Resource ** out);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GSHSCENE_FIXED_H */
