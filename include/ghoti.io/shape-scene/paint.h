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
 * The paint of a fill, the color spaces it is in and the gradients it can be
 * (AD-6, AD-7, AD-23, AD-24).
 *
 * A scene color is **straight** (not premultiplied) `double` components and a
 * **color space**, never an 8-bit value (AD-23).  The scene carries colors and
 * spaces; converting a fill's color between spaces is the sink's, and is defined
 * here exactly.  The one conversion the scene evaluates is a gradient's stops,
 * in ::gshscene_gradient_resolve, which every sink calls (G3; through `math`'s
 * `gmath_pow`, never libm, which AD-22 forbids).  The full normative text (N1 to
 * N14, G1 to G13) is in `documentation/design.md`; the parts that belong to
 * color and paint are these.
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
 *
 * ### N14 The palette
 *
 * A **palette** is a resource (resource.h): an interned table of colors, each a
 * ::GSHSCENE_Color.  A version and a list carry a **palette parameter**, a palette
 * handle or none, beside the foreground (story 14c): it is AD-23's "palette
 * index", made a handle by the front end once (a font's CPAL palette number
 * becomes a palette resource), so a theme change is a new parameter and never a
 * re-lowering.  A ::GSHSCENE_PAINT_PALETTE paint names an **entry** of it, in
 * `[0, 0xFFFE]`, with an alpha multiplier `a`: it is the entry's color with its
 * alpha multiplied by `a`, straight `(pr, pg, pb, pa * a)` in the entry's space.
 * `0xFFFF`, which a font's tables use for the foreground, is never stored: the
 * float-free paint (fixed.h) lowers it to ::GSHSCENE_PAINT_FOREGROUND, and a
 * `double` paint that names it is ::GSHSCENE_ERR_INVALID.  An entry that the
 * palette in force does not have (no palette, or an entry at or past its count)
 * is **missing**: the fill is refused by the planning pass, reason
 * `GSHSCENE_REASON_PALETTE_ENTRY`, on every sink, and is never drawn in another
 * color.  A picture has no palette of its own: its palette paints, like its
 * foreground paints, resolve against the list that draws it.
 *
 * ### Gradients (G1 to G13, color-paint story P1)
 *
 * In real arithmetic, as N1 to N14: every value is the exact real computed from
 * the doubles given.
 *
 * **G1 The resource.**  A gradient (::GSHSCENE_Gradient, interned as a resource:
 * resource.h) is `{kind, geometry, map, stops, extend, space, premultiplied}`.
 * `kind` is linear, radial or sweep; `map` is a valid 2D map from **gradient
 * space** to the local space of the item that paints with it (the space its path
 * or run is in, before the item's map); `space` is an N1 space, the
 * **interpolation space** (zero: the target's); `premultiplied` is 0 or 1.  Every
 * double is finite.  Equal content is one handle; a hash never decides equality.
 *
 * **G2 The color line.**  `n` stops, `0 <= n <= max_resource_elements`, each an
 * `offset` (any finite double) and a stop color.  The store sorts them by offset,
 * **stably**, when it interns; hash, dump and equality see the sorted line.
 * `o_first` and `o_last` are the first and last offsets, `P = o_last - o_first`.
 *
 * **G3 Stop colors.**  A stop color is a ::GSHSCENE_Paint of kind `SOLID`,
 * `FOREGROUND` or `PALETTE` with N7, N10 and N14's rules.  The resource holds the
 * stops as given; nothing is converted at intern.  At draw time every stop is
 * resolved (a solid stop is its own color; a foreground or palette stop resolves
 * against the drawing list's foreground and palette) and converted by N2/N3 into
 * the interpolation space: `space`, or the target's resolved space when `space`
 * is zero (a stop's zero space is the target's too).  ::gshscene_gradient_resolve
 * does this, and every sink calls it, once per gradient per render.  A missing
 * palette entry refuses the fill, `PALETTE_ENTRY`, as N14 does.  A solid stop in
 * the target's space is allowed only in a gradient whose `space` is zero.
 *
 * **G4 The color function.**  For a stop `(r, g, b, a)` in the interpolation
 * space, its **interpolant** is `(a r, a g, a b, a)` when `premultiplied` is 1
 * and `(r, g, b, a)` when it is 0.  `C(t)` for `t` in `[o_first, o_last]`: with
 * `n = 0`, transparent black everywhere; with `n = 1`, that stop's interpolant
 * everywhere (whatever the extend).  Otherwise, among stops of equal offset `o`
 * the **first** given is used below `o` and the **last** given at and above `o`;
 * for adjacent distinct offsets `o_i <= t < o_j`, `C(t) = I_i + (t - o_i) / (o_j -
 * o_i) (I_j - I_i)`; `C(o_last)` is the last stop's interpolant.  The **straight
 * color at t** is `C(t)` (flag 0) or `C(t)` un-premultiplied (flag 1; an alpha of
 * 0 gives `(0, 0, 0, 0)`).
 *
 * **G5 Extend.**  For `t` outside `[o_first, o_last]` and `n >= 2`: `PAD` uses
 * `C(o_first)` below and `C(o_last)` above.  With `P > 0`, `REPEAT` uses
 * `C(o_first + ((t - o_first) mod P))`, `mod` into `[0, P)`, and `REFLECT` uses
 * `C(o_first + P - |((t - o_first) mod 2P) - P|)`.  With `P = 0`: `PAD` is G4's
 * step at `o`; `REPEAT` and `REFLECT` draw **nothing**.
 *
 * **G6 Where a point gets its color (AD-24).**  For a fill whose chain is the
 * target map `T`, the instance maps root to leaf, the item map `M` and the
 * gradient map `G`, a target point `q` takes the gradient-space value at
 * `p = (T I_1 ... I_k M G)^-1 (q)`.  A sink samples where its own coverage
 * definition says.
 *
 * **G7 Linear.**  `p0`, `p1` and an **isoline direction** `d` (zero: perpendicular
 * to `p1 - p0`).  With `n_d = (-d_y, d_x)`, or `p1 - p0` when `d` is zero,
 * `t(p) = ((p - p0) . n_d) / ((p1 - p0) . n_d)`.  A denominator of exactly zero
 * (`p0 = p1`, or `d` parallel to `p1 - p0`) is **degenerate**: nothing is drawn.
 *
 * **G8 Radial: the two-circle cone.**  `c0, r0, c1, r1`, radii any finite double.
 * With `c(w) = c0 + w (c1 - c0)` and `r(w) = r0 + w (r1 - r0)`, the value at `p`
 * is the color at the **largest** real `w` with `r(w) > 0` and
 * `|p - c(w)| = r(w)`; with no such `w`, `p` gets nothing (transparent).  Equal
 * circles (`c0 = c1` and `r0 = r1`) are degenerate: nothing is drawn.
 *
 * **G9 Sweep (AD-6).**  A center `c`, a start and end angle `a0, a1` and their
 * unit (`GSHGEOM_Angle_Unit`, the source's).  `theta(p)` is the angle of `p - c`
 * in `[0, one turn)`, from `+x` toward `+y` (shape-geometry's positive
 * direction), 0 at `p = c`; `t = (theta - a0) / (a1 - a0)`.  Angles outside
 * `[0, one turn)` are legal; only `theta` in `[0, one turn)` is sampled.  With
 * `a0 = a1`: `REPEAT` and `REFLECT` draw nothing; `PAD` uses `C(o_first)` for
 * `theta < a0` and `C(o_last)` for `theta >= a0`.
 *
 * **G10 Degenerate.**  Never refused at record.  G7's zero denominator, G8's
 * equal circles and G9's `a0 = a1` with repeat or reflect draw nothing whatever
 * the stops; G5's `P = 0` with two or more stops and repeat or reflect draws
 * nothing; the render report says so (G13).
 *
 * **G11 Not invertible (AD-24's open question, D7).**  A gradient fill's chain is
 * **not invertible** exactly when some map of it (G6's `T`, `I_1` to `I_k`, `M`,
 * `G`) has a 2x2 linear part (the plane's, for a 3D map) whose determinant is
 * exactly zero, decided by shape-geometry's exact arithmetic on that map's own
 * doubles; no composition is formed.  The fill draws nothing and the report says
 * so (G13).
 *
 * **G12 The fill.**  N7 with a gradient: at each point the source is the
 * straight color at `t` (G4, G5) in the interpolation space, its alpha multiplied
 * by the paint's `rgba[3]`, converted (N3) to the layer's blend space and
 * premultiplied; where G8, G10 or G11 says nothing, the source is `(0, 0, 0, 0)`.
 * A picture's foreground and palette stops resolve against the list that draws it.
 *
 * **G13 Sinks.**  A sink gets the gradient by handle and reads it with
 * ::gshscene_resource_gradient (the stops as given, sorted) and
 * ::gshscene_gradient_resolve; the scene bakes no ramp or table.  The caps name
 * the gradient kinds a sink draws (sink.h): a kind it lacks is `REFUSED`, reason
 * `GRADIENT`; without `GRADIENT_SPACES`, a stated interpolation space other than
 * the target's is `APPROXIMATED`, reason `INTERPOLATION_SPACE`.  The planning
 * pass reports G11 and G10 as `NOTICE` entries (drawn as defined, never failing
 * strict), reasons `MAP_NOT_INVERTIBLE` and `GRADIENT_DEGENERATE`, through the
 * one predicate ::gshscene_gradient_notice.  A writer dedupes gradients by
 * resource id; a GPU sink caches its upload by id and evicts on release (AD-27).
 */

#ifndef GHOTI_IO_GSHSCENE_PAINT_H
#define GHOTI_IO_GSHSCENE_PAINT_H

#include <ghoti.io/shape-scene/core.h>
#include <ghoti.io/shape-scene/macros.h>
#include <ghoti.io/shape-scene/types.h>

#include <ghoti.io/color/color.h>

#include <ghoti.io/shape-geometry/map.h>

#include <stddef.h>
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
  /** A gradient resource of the list's store (G1 to G13): `gradient` names it, `rgba[0..2]` zero, `rgba[3]` its alpha multiplier, the space zero, the entry zero. */
  GSHSCENE_PAINT_GRADIENT = 1,
  /** The foreground color (`currentColor`), a parameter of the version and of the list (N10): `rgba[0..2]` zero, `rgba[3]` its alpha multiplier, the space zero. */
  GSHSCENE_PAINT_FOREGROUND = 2,
  /**
   * An entry of the palette in force, a parameter of the version and of the list (N14): `entry` in `[0, 0xFFFE]`,
   * `rgba[0..2]` zero, `rgba[3]` its alpha multiplier, the space zero.  A COLR solid paint lowers to it (story 14c).
   */
  GSHSCENE_PAINT_PALETTE = 3,
  GSHSCENE_PAINT_KIND_COUNT    ///< Closes the enum.
} GSHSCENE_Paint_Kind;

/** The largest palette entry a paint names: `0xFFFF` is a font's foreground and is never stored (N14). */
#define GSHSCENE_PALETTE_ENTRY_MAX 0xFFFEu

/** @brief A paint. */
typedef struct GSHSCENE_Paint {
  GSHSCENE_Paint_Kind kind; ///< The kind.
  /** Solid: straight `r, g, b, a`.  Each finite; `a` in `[0, 1]`; the others may leave `[0, 1]` (a color outside the gamut).  Foreground: `0, 0, 0, a`. */
  double rgba[4];
  /** Solid: the color space of the components (zero is the target's).  Foreground: zero. */
  GSHSCENE_Color_Space space;
  /** Gradient: the gradient resource (the list holds a reference).  NULL for a solid, foreground or palette paint (and required to be). */
  const GSHSCENE_Resource * gradient;
  /** Palette: the entry, in `[0, GSHSCENE_PALETTE_ENTRY_MAX]`.  Zero for every other kind (and required to be). */
  uint32_t entry;
} GSHSCENE_Paint;

/**
 * @brief The straight color a paint stands for (N7, N10, N14): a solid paint's own color, a foreground paint's
 *   foreground with its alpha scaled, a palette paint's entry with its alpha scaled.
 *
 * The one resolution every sink uses, so that every sink refuses the same fills.
 *
 * @param paint The paint (as a closed list holds it).
 * @param foreground The foreground in force (the drawing list's).
 * @param palette The palette in force (the drawing list's), or NULL for none.
 * @param out The color; written only on success.  A space of zero is still the target's (N7).
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_REFUSED for a palette paint whose entry is missing (N14);
 *   ::GSHSCENE_ERR_INVALID for NULL, a paint of another kind, or a @p palette that is not a palette resource.
 */
GSHSCENE_API GSHSCENE_Result gshscene_paint_resolve(const GSHSCENE_Paint * paint, const GSHSCENE_Color * foreground,
    const GSHSCENE_Resource * palette, GSHSCENE_Color * out);

/** @brief What a gradient is (G7, G8, G9).  The values are pinned (they are hashed). */
typedef enum GSHSCENE_Gradient_Kind {
  GSHSCENE_GRADIENT_LINEAR = 1, ///< Two points and an isoline direction (G7).
  GSHSCENE_GRADIENT_RADIAL = 2, ///< Two circles: the cone of Canvas and COLRv1 (G8).
  GSHSCENE_GRADIENT_SWEEP = 3,  ///< A center and two angles: CSS's conic (G9).
  GSHSCENE_GRADIENT_KIND_COUNT  ///< Closes the enum.
} GSHSCENE_Gradient_Kind;

/** @brief What a gradient does outside its color line (G5).  The values are pinned (they are hashed). */
typedef enum GSHSCENE_Extend {
  GSHSCENE_EXTEND_PAD = 0,     ///< The end colors continue.
  GSHSCENE_EXTEND_REPEAT = 1,  ///< The line repeats with period `o_last - o_first`.
  GSHSCENE_EXTEND_REFLECT = 2, ///< The line repeats, every other copy reversed.
  GSHSCENE_EXTEND_COUNT        ///< Closes the enum.
} GSHSCENE_Extend;

/** @brief One stop of a color line (G2, G3). */
typedef struct GSHSCENE_Gradient_Stop {
  double offset;        ///< Any finite double (`-0` is stored as `+0`).
  GSHSCENE_Paint color; ///< A `SOLID`, `FOREGROUND` or `PALETTE` paint, by a fill's rules; never a gradient.
} GSHSCENE_Gradient_Stop;

/**
 * @brief The content of a gradient (G1): what ::gshscene_store_intern_gradient takes and
 *   ::gshscene_resource_gradient gives back.
 *
 * Only the geometry of its kind is read; the other kinds' fields are zero (and required to be), so a gradient has
 * one spelling.  Every double is finite; `-0` is stored as `+0`.
 */
typedef struct GSHSCENE_Gradient {
  uint32_t kind;        ///< A ::GSHSCENE_Gradient_Kind.
  double p0[2];         ///< Linear: the point of offset 0.
  double p1[2];         ///< Linear: the point of offset 1.
  double d[2];          ///< Linear: the isoline direction; `(0, 0)` is perpendicular to `p1 - p0` (G7).
  double c0[2];         ///< Radial: the start circle's center.
  double r0;            ///< Radial: its radius (any finite double: G8 reads `r(w) > 0`).
  double c1[2];         ///< Radial: the end circle's center.
  double r1;            ///< Radial: its radius.
  double center[2];     ///< Sweep: the center.
  uint32_t angle_unit;  ///< Sweep: a `GSHGEOM_Angle_Unit`, the source's (0, radians, for the other kinds).
  double a0;            ///< Sweep: the angle of offset 0.
  double a1;            ///< Sweep: the angle of offset 1.
  /** From gradient space to the item's local space: a valid 2D map (a zero-filled map is refused; use the identity). */
  GSHGEOM_Map map;
  uint32_t extend;            ///< A ::GSHSCENE_Extend.
  GSHSCENE_Color_Space space; ///< The interpolation space; zero is the target's.
  uint32_t premultiplied;     ///< 1: interpolate premultiplied (G4); 0: straight.
  size_t stop_count;          ///< The stops, at most the store's `max_resource_elements`.
  /** The stops: given in any order to intern (sorted stably by offset), and sorted when read back.  NULL only with no stops. */
  const GSHSCENE_Gradient_Stop * stops;
} GSHSCENE_Gradient;

/** @brief A stop as ::gshscene_gradient_resolve gives it: its offset and its straight color in the interpolation space. */
typedef struct GSHSCENE_Resolved_Stop {
  double offset;  ///< The stop's offset, as stored.
  double rgba[4]; ///< Straight `r, g, b, a` in the interpolation space (G3), the stop's alpha unconverted.
} GSHSCENE_Resolved_Stop;

/**
 * @brief Every stop of a gradient converted into its interpolation space (G3): the one conversion of stops, which
 *   every sink calls once per gradient per render.
 *
 * Each stop resolves as a fill's paint does (::gshscene_paint_resolve: a solid stop is its color, a foreground stop
 * @p foreground with its alpha scaled, a palette stop an entry of @p palette with its alpha scaled) and is then
 * converted by N2 and N3 from its space (the target's when zero) into the interpolation space (the gradient's
 * `space`, or @p target_space when that is zero).  A stop already in the interpolation space is copied exactly.
 * The transfer functions are evaluated with `math`'s `gmath_pow`; each converted component is within
 * `GSHSCENE_RESOLVE_TOLERANCE * max(1, |c|)` of N3's exact value (measured against tools/gradient-oracle.py).
 *
 * @param gradient A gradient resource.
 * @param foreground The foreground in force (the drawing list's).
 * @param palette The palette in force (the drawing list's), or NULL for none.
 * @param target_space The target's resolved space (`gshscene_job_target_space`): a valid space, not zero.
 * @param out_space The interpolation space, or NULL; written only on success.
 * @param out Room for the stops, in their stored order; written only on success.
 * @param capacity Its length: at least the gradient's stop count.
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_REFUSED (a palette stop whose entry is missing, N14);
 *   ::GSHSCENE_ERR_INVALID (NULL where a value is needed, not a gradient, a @p palette that is not a palette, an
 *   invalid or zero @p target_space, a @p capacity below the stop count).
 */
GSHSCENE_API GSHSCENE_Result gshscene_gradient_resolve(const GSHSCENE_Resource * gradient,
    const GSHSCENE_Color * foreground, const GSHSCENE_Resource * palette, const GSHSCENE_Color_Space * target_space,
    GSHSCENE_Color_Space * out_space, GSHSCENE_Resolved_Stop * out, size_t capacity);

/** The bound of ::gshscene_gradient_resolve's error, relative to `max(1, |c|)` for each component (G3). */
#define GSHSCENE_RESOLVE_TOLERANCE 1e-13

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GSHSCENE_PAINT_H */
