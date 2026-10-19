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
 * @file paint.h
 *
 * The paint of a fill and the color spaces it is in (AD-7, AD-23).
 *
 * A scene color is **straight** (not premultiplied) `double` components and a
 * **color space**, never an 8-bit value (AD-23).  The scene carries colors and
 * spaces and never evaluates a transfer function (`color`'s evaluator is libm's
 * `pow`, `exp` and `log`, which AD-22 forbids in the shape family): converting a
 * color between spaces is the sink's, and is defined here exactly.  The full
 * normative text (N1 to N13) is in `documentation/design.md`; the parts that
 * belong to color are these.
 *
 * ### N1 Color space
 *
 * A ::GSHSCENE_Color_Space is `(primaries, transfer, gamma)`.  The zero value is
 * **the target's space**.  Otherwise `primaries` is one of four gamuts, all with
 * the D65 white `(0.3127, 0.3290)` (CIE xy, red, green, blue):
 *
 * | Gamut | red | green | blue |
 * | --- | --- | --- | --- |
 * | sRGB | 0.64, 0.33 | 0.30, 0.60 | 0.15, 0.06 |
 * | Adobe RGB | 0.64, 0.33 | 0.21, 0.71 | 0.15, 0.06 |
 * | Display P3 | 0.680, 0.320 | 0.265, 0.690 | 0.150, 0.060 |
 * | BT.2020 | 0.708, 0.292 | 0.170, 0.797 | 0.131, 0.046 |
 *
 * `transfer` is `LINEAR`, `SRGB` or `GAMMA`, and `gamma` is 0 except for
 * `GAMMA`, where it is finite and in `[0.1, 10]`.  ProPhoto (D50: converting to
 * it needs a chromatic adaptation the milestone does not choose), the parametric
 * and BT.1886 curves, PQ, HLG and ICC profiles are ::GSHSCENE_ERR_UNSUPPORTED.
 *
 * ### N2 Transfer
 *
 * `decode` takes an encoded component to linear light and `encode` is its
 * inverse, on real values of any sign: LINEAR is the identity; SRGB decodes
 * `x / 12.92` for `x <= 0.04045` and `((x + 0.055) / 1.055)^2.4` above, and
 * encodes `12.92 y` for `y <= 0.0031308` and `1.055 y^(1/2.4) - 0.055` above (the
 * linear segment extends below zero, as in `color`); GAMMA `g` decodes
 * `sign(x) |x|^g` and encodes `sign(y) |y|^(1/g)`.  The power is the real power
 * function; a sink evaluates it with `math` and states its error.
 *
 * ### N3 Conversion
 *
 * With `X_c = x_c / y_c`, `Y_c = 1`, `Z_c = (1 - x_c - y_c) / y_c` for each
 * primary and the white, solve `[X_r X_g X_b; 1 1 1; Z_r Z_g Z_b] S = W`; the
 * matrix `M` taking linear RGB to XYZ has the columns `S_c (X_c, 1, Z_c)`.  A
 * straight color `(r, g, b)` goes from space `A` to space `B` as
 * `encode_B(M_B^-1 M_A decode_A(r, g, b))`, and is untouched (exactly) when the
 * spaces are the same; alpha is never converted.  A premultiplied value is
 * un-premultiplied, converted and premultiplied again (an alpha of 0 gives 0).
 *
 * ### N10 The foreground
 *
 * The **foreground** is a ::GSHSCENE_Color that belongs to a version and to a
 * list (list.h, version.h); its default is opaque black in the target's space.  A
 * ::GSHSCENE_PAINT_FOREGROUND paint is the foreground with its alpha multiplied
 * by the paint's `rgba[3]`: straight `(fr, fg, fb, fa * a)` in the foreground's
 * space.  It is what SVG's `currentColor` lowers to, and is never baked into a
 * fill, so an icon used in ten colors is one picture.
 */

#ifndef GHOTI_IO_GSHSCENE_PAINT_H
#define GHOTI_IO_GSHSCENE_PAINT_H

#include <ghoti.io/shape-scene/core.h>
#include <ghoti.io/shape-scene/macros.h>
#include <ghoti.io/shape-scene/types.h>

#include <ghoti.io/color/color.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief A color space: a named gamut, a transfer function and, for a plain exponent, its gamma.  Zero is the target's space. */
typedef struct GSHSCENE_Color_Space {
  GCOL_Primaries primaries; ///< `GCOL_PRIMARIES_SRGB`, `_ADOBE_RGB`, `_DISPLAY_P3` or `_BT2020`; `GCOL_PRIMARIES_UNKNOWN` (0) with the other fields zero for the target's.
  GCOL_Transfer transfer;   ///< `GCOL_TRANSFER_LINEAR`, `_SRGB` or `_GAMMA`; `GCOL_TRANSFER_UNKNOWN` (0) for the target's.
  double gamma;             ///< The exponent when @p transfer is `GCOL_TRANSFER_GAMMA`, finite and in `[0.1, 10]`; else 0.
} GSHSCENE_Color_Space;

/**
 * @brief Whether @p space is a valid space of the ones this library can carry.
 *
 * @return ::GSHSCENE_OK (including the zero space); ::GSHSCENE_ERR_INVALID for NULL, a partly given space (a gamut
 *   and no transfer, a gamma where it does not belong, or out of range) or an enum value outside its range;
 *   ::GSHSCENE_ERR_UNSUPPORTED for ProPhoto, the parametric, BT.1886, PQ and HLG transfers.
 */
GSHSCENE_API GSHSCENE_Result gshscene_color_space_check(const GSHSCENE_Color_Space * space);

/** @brief Nonzero if @p space is the zero value, the target's space; 0 for NULL. */
GSHSCENE_API int gshscene_color_space_is_target(const GSHSCENE_Color_Space * space);

/**
 * @brief The space a `color` description names.
 *
 * @param info A description whose gamut is stated and one of the four named, whose transfer is linear, sRGB or a
 *   plain gamma (its `gamma_value`), and that carries no ICC profile and no linked path.
 * @param out The space; written only on success.
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID (NULL, a gamut that is not stated); ::GSHSCENE_ERR_UNSUPPORTED (a
 *   gamut that matches no name or ProPhoto, another transfer, an ICC profile or a path to one).
 */
GSHSCENE_API GSHSCENE_Result gshscene_color_space_from_info(const GCOL_Color_Info * info, GSHSCENE_Color_Space * out);

/** @brief A color: straight `r, g, b, a` and its space.  The foreground of a version or a list is one. */
typedef struct GSHSCENE_Color {
  double rgba[4];              ///< Straight components: each finite, `a` in `[0, 1]`, the others may leave `[0, 1]` (outside the gamut).
  GSHSCENE_Color_Space space;  ///< The space of the components.
} GSHSCENE_Color;

/** @brief Fill @p color with the default foreground: opaque black in the target's space.  NULL is ignored. */
GSHSCENE_API void gshscene_color_init(GSHSCENE_Color * color);

/** @brief Whether @p color is valid (finite components, `a` in `[0, 1]`, a valid space); the result of the check as for a space. */
GSHSCENE_API GSHSCENE_Result gshscene_color_check(const GSHSCENE_Color * color);

/** @brief What a paint is.  The values are pinned (they are hashed). */
typedef enum GSHSCENE_Paint_Kind {
  GSHSCENE_PAINT_SOLID = 0,    ///< One color, in its space.
  GSHSCENE_PAINT_GRADIENT = 1, ///< Reserved: a gradient resource (CAP-8 is solid paint); ::GSHSCENE_ERR_UNSUPPORTED until then.
  /** The foreground color (`currentColor`), a parameter of the version and of the list (N10): `rgba[0..2]` zero, `rgba[3]` its alpha multiplier, the space zero. */
  GSHSCENE_PAINT_FOREGROUND = 2,
  /**
   * Reserved: an entry of the font's color palette, by index, with an alpha (a COLRv1 solid paint; AD-23).  Refused,
   * because AD-23 says only that the palette index is a scene parameter, not whose table it selects in or what a
   * missing entry is; that is settled with the font's CPAL (story 14).  ::GSHSCENE_ERR_UNSUPPORTED.
   */
  GSHSCENE_PAINT_PALETTE = 3,
  GSHSCENE_PAINT_KIND_COUNT    ///< Closes the enum.
} GSHSCENE_Paint_Kind;

/** @brief A paint. */
typedef struct GSHSCENE_Paint {
  GSHSCENE_Paint_Kind kind; ///< The kind.
  /** Solid: straight `r, g, b, a`.  Each finite; `a` in `[0, 1]`; the others may leave `[0, 1]` (a color outside the gamut).  Foreground: `0, 0, 0, a`. */
  double rgba[4];
  /** Solid: the color space of the components (zero is the target's).  Foreground: zero. */
  GSHSCENE_Color_Space space;
  /** Gradient: the gradient resource.  NULL for a solid or foreground paint (and required to be). */
  const GSHSCENE_Resource * gradient;
} GSHSCENE_Paint;

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GSHSCENE_PAINT_H */
