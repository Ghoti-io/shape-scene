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
 * @file mask.h
 *
 * The mask: what a group's soft-mask slot names, and the transfer that turns a mask picture's value into a number.
 * Rules MK1 to MK7 are the ones this header defines; the frame, the order at the group's pop and the planning pass
 * (MK6, MK7, MK9) are used by the sinks and the planner.
 *
 * ### The resource (MK1)
 *
 * `{picture, mode, coefficients, space, render space, backdrop, transfer, acts on}`, interned by content.  The `picture` is
 * a picture resource of the same store, which the mask retains.  The `mode` is ::GSHSCENE_MASK_ALPHA or
 * ::GSHSCENE_MASK_LUMINANCE (zero is invalid, so a front-end must say).  The `coefficients` name the luminance set
 * (MK3).  `space` is the luminance space `L` and `render_space` the space `R` the picture is blended in (both N1 spaces;
 * zero in `space` means `R`, zero in `render_space` means the blend space of the layer the masked group composites
 * into).  The `backdrop` is an opaque color of three finite components of magnitude at most `2^16`, in
 * `backdrop_space` (zero: the luminance space `L`); zero is black.  In `ALPHA` mode `coefficients`, `space`,
 * `backdrop` and `backdrop_space` are zero (::GSHSCENE_ERR_INVALID otherwise), because alpha has no color and a
 * field that cannot matter must not split a handle.
 *
 * A space outside the ones this library carries is ::GSHSCENE_ERR_UNSUPPORTED, a backdrop component or a transfer
 * past a bound ::GSHSCENE_ERR_LIMIT, each decided after every ::GSHSCENE_ERR_INVALID.
 *
 * ### The coefficient sets (MK3)
 *
 * Exact rationals, each summing to exactly 1: `W3C` `(2125, 7154, 721) / 10000`, `REC709` `(2126, 7152, 722) / 10000`,
 * `LUM` `(30, 59, 11) / 100`, and `SPACE_Y`, the Y row of N3's RGB-to-XYZ matrix of the luminance space.  They apply to the
 * components of `L` as they are (encoded or linear, as the space is).  ::gshscene_mask_resolve returns them as doubles,
 * the one answer every sink uses.
 *
 * ### The transfer (MK4)
 *
 * One of `IDENTITY`; `EXPONENTIAL {c0, c1, n}` (`c0 + x^n (c1 - c0)`, `n` finite in `[0, 2^16]`, `0^0 = 1`, the power
 * `gmath_pow`); `TABLE` (`m >= 2` finite samples on an equally spaced grid over `[0, 1]`, linear between them);
 * `STITCHING` (`k >= 1` pieces `{lo, hi, e0, e1, leaf}`, `lo_0 = 0`, `hi_{k-1} = 1`, `hi_i = lo_{i+1}`, `lo < hi`, `e0` and
 * `e1` in `[0, 1]`, the leaf an identity, an exponential or a table).  The input is clamped to `[0, 1]` first and the
 * output after; a NaN clamps to 0.  At most ::GSHSCENE_MASK_MAX_PIECES pieces and ::GSHSCENE_MASK_MAX_SAMPLES samples in
 * all, past them ::GSHSCENE_ERR_LIMIT.  Nested stitchings are flattened by the front-end.  A zero ::GSHSCENE_Transfer is
 * the identity.
 *
 * ### Hash, dump (MK13)
 *
 * Format 1: `u32` format, `u32` kind, `u64` the picture's content hash, `u8` mode, `u8` coefficients, the luminance
 * space, the render space, three `f64` the backdrop, its space, `u8` acts on, then the canonical transfer: `u8` kind and,
 * for an exponential `f64` `c0`, `c1`, `n`; for a table `u64` the sample count and `f64` each sample; for a stitching `u64`
 * the piece count and, per piece, `f64` `lo`, `hi`, `e0`, `e1` and the leaf as a transfer.  The dump is a line `mask
 * picture=<hash> mode=M coeff=C acts=A rspace=<space> lspace=<space> backdrop=<hex> <hex> <hex> bspace=<space>`, a line
 * `tr kind=K c0=<hex> c1=<hex> n=<hex> samples=N pieces=N` and a line `s <hex>` per top-level sample, then per piece a
 * line `p lo=<hex> hi=<hex> e0=<hex> e1=<hex> leaf=K c0=<hex> c1=<hex> n=<hex> samples=N` and a line `s <hex>` per sample;
 * never the picture's items.
 */

#ifndef GHOTI_IO_GSHSCENE_MASK_H
#define GHOTI_IO_GSHSCENE_MASK_H

#include <ghoti.io/shape-scene/macros.h>
#include <ghoti.io/shape-scene/paint.h>
#include <ghoti.io/shape-scene/resource.h>
#include <ghoti.io/shape-scene/types.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief What a mask takes from its picture.  The values are pinned (they are hashed); zero is invalid. */
typedef enum GSHSCENE_Mask_Mode {
  GSHSCENE_MASK_ALPHA = 1,     ///< The picture's alpha (MK2).
  GSHSCENE_MASK_LUMINANCE = 2, ///< The luminance of the picture composited over the backdrop (MK2).
  GSHSCENE_MASK_MODE_COUNT     ///< Closes the enum.
} GSHSCENE_Mask_Mode;

/** @brief The luminance coefficient sets (MK3).  The values are pinned; zero is the default. */
typedef enum GSHSCENE_Mask_Coefficients {
  GSHSCENE_MASK_COEFF_W3C = 0,    ///< `(2125, 7154, 721) / 10000`: CSS Masking's and SVG's `luminanceToAlpha`.
  GSHSCENE_MASK_COEFF_REC709 = 1, ///< `(2126, 7152, 722) / 10000`.
  GSHSCENE_MASK_COEFF_LUM = 2,    ///< `(30, 59, 11) / 100`: the non-separable blend modes' `Lum`, and PDF's `DeviceRGB`.
  GSHSCENE_MASK_COEFF_SPACE_Y = 3, ///< The Y row of N3's RGB-to-XYZ matrix of the luminance space.
  GSHSCENE_MASK_COEFF_COUNT       ///< Closes the enum.
} GSHSCENE_Mask_Coefficients;

/** @brief Which input of T3 the mask value multiplies (MK7).  The values are pinned; zero is the default. */
typedef enum GSHSCENE_Mask_Acts_On {
  GSHSCENE_MASK_ACTS_ON_OPACITY = 0, ///< `o` becomes `o v` (SVG, CSS, PDF without alpha-is-shape).
  GSHSCENE_MASK_ACTS_ON_SHAPE = 1,   ///< `m` becomes `m v` (PDF with alpha-is-shape).
  GSHSCENE_MASK_ACTS_ON_COUNT        ///< Closes the enum.
} GSHSCENE_Mask_Acts_On;

/** @brief The kinds of transfer (MK4).  The values are pinned (they are hashed); zero is the identity. */
typedef enum GSHSCENE_Transfer_Kind {
  GSHSCENE_TRANSFER_IDENTITY = 0,    ///< `x`.
  GSHSCENE_TRANSFER_EXPONENTIAL = 1, ///< PDF function type 2: `c0 + x^n (c1 - c0)`.
  GSHSCENE_TRANSFER_TABLE = 2,       ///< PDF function type 0 of order 1 on an equally spaced grid.
  GSHSCENE_TRANSFER_STITCHING = 3,   ///< PDF function type 3, one level.
  GSHSCENE_TRANSFER_KIND_COUNT       ///< Closes the enum.
} GSHSCENE_Transfer_Kind;

/** The most stitching pieces in one transfer (MK4, M1-D8). */
#define GSHSCENE_MASK_MAX_PIECES ((size_t)256)
/** The most table samples in one transfer, all tables together (MK4, M1-D8). */
#define GSHSCENE_MASK_MAX_SAMPLES ((size_t)4096)
/** The largest exponent of an exponential transfer (MK4). */
#define GSHSCENE_MASK_MAX_EXPONENT 65536.0
/** The largest magnitude of a backdrop component (MK1, N1). */
#define GSHSCENE_MASK_BACKDROP_MAX 65536.0

/** @brief One piece of a stitching transfer (MK4): the input interval `[lo, hi)`, the leaf's input range `[e0, e1]` and the leaf. */
typedef struct GSHSCENE_Transfer_Piece {
  double lo;   ///< The interval's start.
  double hi;   ///< Its end; the next piece's `lo`.
  double e0;   ///< The leaf's input at `lo`, in `[0, 1]`.
  double e1;   ///< The leaf's input at `hi`, in `[0, 1]`.
  uint32_t leaf_kind;       ///< ::GSHSCENE_TRANSFER_IDENTITY, `_EXPONENTIAL` or `_TABLE`.
  double c0;                ///< Exponential leaf.
  double c1;                ///< Exponential leaf.
  double n;                 ///< Exponential leaf.
  size_t sample_count;      ///< Table leaf: the samples.
  const double * samples;   ///< Table leaf: `sample_count` of them.
} GSHSCENE_Transfer_Piece;

/** @brief A transfer (MK4).  Fields of another kind are zero. */
typedef struct GSHSCENE_Transfer {
  uint32_t kind;                          ///< A ::GSHSCENE_Transfer_Kind.
  double c0;                              ///< Exponential: the output at 0.
  double c1;                              ///< Exponential: the output at 1.
  double n;                               ///< Exponential: the exponent, finite in `[0, 2^16]`.
  size_t sample_count;                    ///< Table: the samples, at least 2.
  const double * samples;                 ///< Table: `sample_count` of them.
  size_t piece_count;                     ///< Stitching: the pieces, at least 1.
  const GSHSCENE_Transfer_Piece * pieces; ///< Stitching: `piece_count` of them.
} GSHSCENE_Transfer;

/** @brief The content of a mask (MK1). */
typedef struct GSHSCENE_Mask {
  const GSHSCENE_Resource * picture; ///< A picture resource of the same store; retained.  Required.
  uint32_t mode;                     ///< A ::GSHSCENE_Mask_Mode; nonzero.
  uint32_t coefficients;             ///< A ::GSHSCENE_Mask_Coefficients; zero (and unused) in `ALPHA` mode.
  GSHSCENE_Color_Space space;        ///< The luminance space `L`; zero means `R`.  Zero in `ALPHA` mode.
  GSHSCENE_Color_Space render_space; ///< The picture's blend space `R`; zero means the enclosing layer's.
  double backdrop[3];                ///< The opaque backdrop's components, finite, magnitude at most `2^16`; zero is black.  Zero in `ALPHA` mode.
  GSHSCENE_Color_Space backdrop_space; ///< The backdrop's space; zero means `L`.  Zero in `ALPHA` mode.
  GSHSCENE_Transfer transfer;        ///< The transfer; zero is the identity.
  uint32_t acts_on;                  ///< A ::GSHSCENE_Mask_Acts_On.
} GSHSCENE_Mask;

/**
 * @brief Intern a mask (MK1).
 *
 * The content is copied (the transfer's samples and pieces too); the picture is retained.  Equal content (bit for bit,
 * `-0` spelled as `+0`) is one handle while one is held; a hash never decides.
 *
 * @param store The store.
 * @param mask The content.
 * @param out The handle, with a reference for the caller; written only on success.
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID (NULL, a picture that is not a picture resource of this store, a
 *   field outside its range or set where its mode forbids it, a non-finite number, an ill-formed transfer);
 *   ::GSHSCENE_ERR_UNSUPPORTED (a space this library does not carry); ::GSHSCENE_ERR_LIMIT (a backdrop component past
 *   `2^16`, an exponent past `2^16`, more than ::GSHSCENE_MASK_MAX_PIECES pieces or ::GSHSCENE_MASK_MAX_SAMPLES samples,
 *   the store's limits), decided after every invalid field; ::GSHSCENE_ERR_OOM.
 */
GSHSCENE_API GSHSCENE_Result gshscene_store_intern_mask(
    GSHSCENE_Store * store, const GSHSCENE_Mask * mask, const GSHSCENE_Resource ** out);

/**
 * @brief Read a mask resource back.
 *
 * @param out The content as stored (canonical); the transfer's arrays point into the resource and are valid while a handle
 *   is held.  Written only on success.
 * @return ::GSHSCENE_OK or ::GSHSCENE_ERR_INVALID (NULL, not a mask).
 */
GSHSCENE_API GSHSCENE_Result gshscene_resource_mask(const GSHSCENE_Resource * resource, GSHSCENE_Mask * out);

/** @brief What a mask resolves to for the layer it is used in (MK3). */
typedef struct GSHSCENE_Mask_Resolved {
  double k[3];                       ///< The luminance coefficients as doubles: the nearest double of each exact rational.
  GSHSCENE_Color_Space render_space; ///< `R`: the mask's, or the enclosing space when it is zero.
  GSHSCENE_Color_Space luminance_space; ///< `L`: the mask's, or `R` when it is zero.
  GSHSCENE_Color_Space backdrop_space;  ///< The backdrop's space: the mask's, or `L` when it is zero.
} GSHSCENE_Mask_Resolved;

/**
 * @brief Resolve a mask's coefficients and spaces (MK3, M1-D5): the one answer every sink uses.
 *
 * `SPACE_Y` is the Y row of N3's RGB-to-XYZ matrix of `L`, within ::GSHSCENE_RESOLVE_TOLERANCE of the exact row.
 *
 * @param mask A mask resource.
 * @param enclosing The concrete space (not zero) the masked group composites into; it may be NULL or zero only when the
 *   mask's `render_space` is concrete, or `ALPHA`.
 * @param out Written only on success.
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID (NULL, not a mask, or a space that needs @p enclosing and has none).
 */
GSHSCENE_API GSHSCENE_Result gshscene_mask_resolve(const GSHSCENE_Resource * mask, const GSHSCENE_Color_Space * enclosing,
    GSHSCENE_Mask_Resolved * out);

/**
 * @brief The mask's transfer of @p x (MK4): the input clamped to `[0, 1]`, the transfer evaluated, the output clamped.
 *
 * The one evaluation of the waist; the power is `gmath_pow`.  A NaN input or output is 0.
 *
 * @param mask A mask resource; anything else gives 0.
 */
GSHSCENE_API double gshscene_mask_transfer(const GSHSCENE_Resource * mask, double x);

/** @brief A transfer's value at @p x (MK4), as ::gshscene_mask_transfer defines it, for a transfer not yet in a mask. */
GSHSCENE_API double gshscene_transfer_eval(const GSHSCENE_Transfer * transfer, double x);

/**
 * @brief The `GSHSCENE_MASK_NATIVE_*` features (sink.h) a mask needs a sink to have (MK9).
 *
 * `SPACES` is set when `space` or `render_space` is not zero; the planner refines it against the enclosing space.
 * 0 for NULL and for anything but a mask.
 */
GSHSCENE_API uint32_t gshscene_mask_required(const GSHSCENE_Resource * mask);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GSHSCENE_MASK_H */
