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
 * @file image_paint.h
 *
 * The image paint: what a sample of an image means, and how a fill takes its color from one.  Rules IP1 to IP18.  An
 * image (image.h) holds samples; an **image-paint resource** says how a fill samples them, and a fill names it by a paint
 * of kind ::GSHSCENE_PAINT_IMAGE.  Every sink, a software rasterizer, a GPU, a document writer, draws the same
 * definition; `shape-cpu` is the reference the others are measured against.
 *
 * ### The resource (IP1)
 *
 * `{image, window, map, extend x, extend y, magnify, minify, filter space, tint}`.  The `image` is an image resource of the
 * same store, which the image paint retains.  The `window` is an integer rectangle `[x0, x1) x [y0, y1)` of texels,
 * `0 <= x0 < x1 <= width` and likewise in y; the all-zero value means the whole image and the store keeps the explicit
 * rectangle, so one drawing has one spelling.  The `map` takes **image space** to the **local space** of the item that paints
 * with it (the space its path or run is in, before the item's map), valid and 2D, each entry zero or of magnitude in
 * `[2^-64, 2^64]`.  The two extend modes are per axis.  `magnify` and `minify` name the filter used when the map stretches every
 * direction and otherwise (IP8).  The `filter space` is an N1 space: **zero means the image's own space**.  The `tint` is a
 * paint of kind solid, foreground or palette, present exactly when the image is alpha-only.
 *
 * ### Image space and the sample point (IP2, IP3)
 *
 * Texel `(i, j)` covers `[i, i + 1) x [j, j + 1)`, y down; its centre is `(i + 1/2, j + 1/2)`.  A fill whose chain is the
 * target map `T`, the instance maps `I_1 .. I_k`, the item map `M` and this paint's map `G` has the composed map
 * `C = T I_1 .. I_k M G` by `gshgeom_map_compose_chain`; a device point `q` takes the image-space point `p = C^-1(q)`.
 * `NEAREST`, `LINEAR` and `CUBIC` are functions of a point; `AREA` is a function of the region `C^-1(Q)` of the device
 * region `Q` the sink states (a raster sink: the pixel square, sampled at the pixel centre).
 *
 * ### Decode (IP4)
 *
 * An unsigned normalized sample of `n` bits is `v / (2^n - 1)`; a float is that float as a double, saturated to
 * `[-2^64, 2^64]` (N1's list of saturation points gains it).  A gray pixel `g` is `(g, g, g)`; alpha is 1 for a layout
 * without it.  The texel's **premultiplied value in its own space** is `(a r, a g, a b, a)` for the straight form and the
 * decoded `(R, G, B, a)` as stored for the premultiplied form.  ::gshscene_image_decode is the one reference: a sink never
 * decodes by a rule of its own.
 *
 * ### Filter space and conversion (IP5)
 *
 * Let `F` be the filter space, or the image's space when it is zero, and `L` the layer's space.  The texel's premultiplied
 * value in `F` is itself when `F` equals the image's space, and otherwise the straight color converted by N2 and N3,
 * saturated, then multiplied by alpha again.  The filters act on these premultiplied values, componentwise, giving a
 * value premultiplied in `F`, which is taken to `L` as N3 takes a premultiplied value (and not at all when `F` is `L`).  The
 * source is that value times the paint's alpha multiplier, on all four components.  Nothing is dithered.
 *
 * ### Edge modes (IP6)
 *
 * Per axis, acting on integer **taps** (so the same maps serve every filter) at the window's edges: `PAD` clamps the tap
 * into the window; `REPEAT` takes it modulo the window's size; `REFLECT` reflects about the window's boundary line, the edge
 * texel repeated; `NONE` reads the texel `(0, 0, 0, 0)` outside the window.  No texel outside the window is ever read.
 *
 * ### The filters (IP7)
 *
 * `NEAREST`: the texel `(floor u, floor v)`.  `LINEAR`: the four texels whose centres surround the point, weighted
 * `(1 - f, f)` in each axis.  `CUBIC`: Mitchell-Netravali with `B = C = 1/3`, separable over `4 x 4` taps with the weights
 * `k(f + 1), k(f), k(1 - f), k(2 - f)`, `k(x) = (21 x^3 - 36 x^2 + 16) / 18` for `0 <= x < 1` and
 * `k(x) = (-7 x^3 + 36 x^2 - 60 x + 32) / 18` for `1 <= x < 2`, **clamped componentwise to the range of the 16 taps' same
 * component**, so nothing overshoots; it is not interpolating (at a texel centre the weights are `(1/18, 8/9, 1/18)`).
 * `AREA`: the exact mean of the piecewise-constant extended image over the preimage region, the box filter that coverage uses.
 *
 * ### Which filter an item uses (IP8)
 *
 * Per item, never per pixel: `magnify` when the composed map's 2x2 linear part stretches every direction exactly
 * (`gshgeom_map_stretches_every_direction`: its smallest singular value exceeds 1), `minify` otherwise, an exactly unit
 * scale included.  A determinant would call a 7 by 0.3 map a magnification.
 *
 * ### The tint (IP9)
 *
 * An alpha-only image's filters act on the one number `a`; the source is the tint's straight color converted to `L` and
 * premultiplied by `m a_t a'`.  A PDF stencil mask painted in the fill color and a monochrome bitmap glyph in the foreground
 * are the same paint.
 *
 * ### What draws nothing (IP11), limits (IP12)
 *
 * A fill whose chain has a map of exactly zero determinant draws nothing and is noticed (::gshscene_paint_notice).  The
 * **sampling domain** is the image-space points inside `[-::GSHSCENE_IMAGE_DOMAIN, ::GSHSCENE_IMAGE_DOMAIN]`; a sink samples
 * inside it or refuses the fill as its own resource limit.  The **discontinuity band**, ::GSHSCENE_IMAGE_BAND texel around a
 * texel boundary, is where `NEAREST` may take either neighbour.
 *
 * ### Writers (IP14)
 *
 * `drawImage`, `<image>`, a CSS background and a PDF `Do` lower to a fill of the destination rectangle with this paint and
 * `PAD` on both axes.  A writer recognizes that shape exactly with ::gshscene_image_fill_is_draw, and a front end builds the
 * path with ::gshscene_image_paint_rect_path, which interns exactly the four corners the predicate looks for.
 *
 * ### Hash and dump (IP13)
 *
 * After resource.h's two leading fields: `u64` the image's content hash, `u32` `x0`, `y0`, `x1`, `y1`, `u64`
 * `gshgeom_map_hash` of the map, `u8` extend x, `u8` extend y, `u8` magnify, `u8` minify, the filter space (`u8`
 * primaries, `u8` transfer, `f64` gamma), `u8` has-tint and, with a tint, `u8` its kind, four `f64`, its space and `u32` its
 * entry.  The dump is `imagepaint image=<hash> window=x0 y0 x1 y1 map=<hash> extend=X,Y filter=MAG,MIN space=<space>
 * tint=<paint or ->`.
 */

#ifndef GHOTI_IO_GSHSCENE_IMAGE_PAINT_H
#define GHOTI_IO_GSHSCENE_IMAGE_PAINT_H

#include <ghoti.io/shape-scene/core.h>
#include <ghoti.io/shape-scene/image.h>
#include <ghoti.io/shape-scene/list.h>
#include <ghoti.io/shape-scene/macros.h>
#include <ghoti.io/shape-scene/paint.h>
#include <ghoti.io/shape-scene/resource.h>
#include <ghoti.io/shape-scene/types.h>

#include <ghoti.io/shape-geometry/map.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The half-width of the sampling domain in texels (IP12): `2^32`. */
#define GSHSCENE_IMAGE_DOMAIN 4294967296.0

/** The discontinuity band around a texel boundary in texels (IP12): `2^-16`. */
#define GSHSCENE_IMAGE_BAND 0.0000152587890625

/**
 * @name Image formats a sink reads (S21)
 *
 * The bits of ::GSHSCENE_Sink_Caps `image_formats`, and of what an image paint requires: its sample type, its layout and,
 * for an alpha-only image, the mark.  A sink reads an image when it has every bit the image requires.
 * @{
 */
#define GSHSCENE_IMAGE_FORMAT_UNORM8 (UINT32_C(1) << 0)
#define GSHSCENE_IMAGE_FORMAT_UNORM12 (UINT32_C(1) << 1)
#define GSHSCENE_IMAGE_FORMAT_UNORM16 (UINT32_C(1) << 2)
#define GSHSCENE_IMAGE_FORMAT_FLOAT32 (UINT32_C(1) << 3)
#define GSHSCENE_IMAGE_FORMAT_GRAY (UINT32_C(1) << 4)
#define GSHSCENE_IMAGE_FORMAT_RGB (UINT32_C(1) << 5)
#define GSHSCENE_IMAGE_FORMAT_RGBA (UINT32_C(1) << 6)
#define GSHSCENE_IMAGE_FORMAT_ALPHA_ONLY (UINT32_C(1) << 7)
#define GSHSCENE_IMAGE_FORMAT_ALL ((UINT32_C(1) << 8) - 1)
/** @} */

/** @brief What an image does past its window along one axis (IP6).  The values are pinned (they are hashed). */
typedef enum GSHSCENE_Image_Extend {
  GSHSCENE_IMAGE_EXTEND_PAD = 0,     ///< The edge texels continue.
  GSHSCENE_IMAGE_EXTEND_REPEAT = 1,  ///< The window repeats.
  GSHSCENE_IMAGE_EXTEND_REFLECT = 2, ///< The window repeats, every other copy reversed.
  GSHSCENE_IMAGE_EXTEND_NONE = 3,    ///< Transparent.
  GSHSCENE_IMAGE_EXTEND_COUNT        ///< Closes the enum.
} GSHSCENE_Image_Extend;

/** @brief How an image is filtered (IP7).  The values are pinned (they are hashed). */
typedef enum GSHSCENE_Image_Filter {
  GSHSCENE_IMAGE_FILTER_NEAREST = 0, ///< The texel containing the point.
  GSHSCENE_IMAGE_FILTER_LINEAR = 1,  ///< Bilinear.
  GSHSCENE_IMAGE_FILTER_CUBIC = 2,   ///< Mitchell-Netravali, clamped.
  GSHSCENE_IMAGE_FILTER_AREA = 3,    ///< The exact mean over the preimage region.
  GSHSCENE_IMAGE_FILTER_COUNT        ///< Closes the enum.
} GSHSCENE_Image_Filter;

/** @brief The content of an image paint (IP1). */
typedef struct GSHSCENE_Image_Paint {
  const GSHSCENE_Resource * image; ///< An image resource of the same store; retained by the image paint.
  uint32_t window[4];              ///< `x0, y0, x1, y1` in texels; all zero means the whole image.
  GSHGEOM_Map map;                 ///< Image space to the item's local space: 2D, valid, entries zero or in `[2^-64, 2^64]` in magnitude.
  uint32_t extend_x;               ///< A ::GSHSCENE_Image_Extend.
  uint32_t extend_y;               ///< Likewise.
  uint32_t magnify;                ///< A ::GSHSCENE_Image_Filter, used when the map stretches every direction (IP8).
  uint32_t minify;                 ///< Likewise, otherwise.
  GSHSCENE_Color_Space filter_space; ///< The space the filters act in; zero is the image's own space.
  /** A `SOLID`, `FOREGROUND` or `PALETTE` paint: present (not NULL) exactly when the image is alpha-only; copied. */
  const GSHSCENE_Paint * tint;
} GSHSCENE_Image_Paint;

/** The most a refusal's `second` can carry: the required extends in bits 0 to 3, the required formats from bit 4 (::gshscene_image_required). */
#define GSHSCENE_IMAGE_REQUIRED_FORMAT_SHIFT 4

/** Fill @p paint with the defaults: no image, the whole image, the identity map, `PAD` and `LINEAR`, the image's own space, no tint.  NULL is ignored. */
GSHSCENE_API void gshscene_image_paint_init(GSHSCENE_Image_Paint * paint);

/**
 * @brief Intern an image paint (IP1): its content is copied, the explicit window stored, `-0` stored as `+0`.
 *
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID (NULL, an image that is not an image resource of this store, a window
 *   that is empty or past the image, a map that is not a valid 2D map, an enum value out of range, a non-finite double, a
 *   tint on a color image or none on an alpha-only one, a tint that is not solid, foreground or palette, or invalid as a
 *   fill's paint is); ::GSHSCENE_ERR_UNSUPPORTED (a reserved space); ::GSHSCENE_ERR_LIMIT (a map entry or a tint
 *   component past its bound, decided after every validity check, or the store's limits); ::GSHSCENE_ERR_OOM.
 */
GSHSCENE_API GSHSCENE_Result gshscene_store_intern_image_paint(GSHSCENE_Store * store, const GSHSCENE_Image_Paint * paint,
    const GSHSCENE_Resource ** out);

/**
 * @brief Read an image paint back.
 *
 * @param out The content as stored (the window explicit; the filter space zeroed for an alpha-only image); `image` and `tint`
 *   point into the resource and are valid while a handle is held.  Written only on success.
 * @return ::GSHSCENE_OK or ::GSHSCENE_ERR_INVALID (NULL, not an image paint).
 */
GSHSCENE_API GSHSCENE_Result gshscene_resource_image_paint(const GSHSCENE_Resource * resource, GSHSCENE_Image_Paint * out);

/**
 * @brief The texel `(x, y)` of @p image decoded as IP4 says: its premultiplied value in the image's own space.
 *
 * Each division is correctly rounded.  For an alpha-only image the value is `(0, 0, 0, a)`.
 *
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID (NULL, not an image, a texel outside the image).
 */
GSHSCENE_API GSHSCENE_Result gshscene_image_decode(const GSHSCENE_Resource * image, uint32_t x, uint32_t y, double out[4]);

/**
 * @brief Whether a fill is a drawn image (IP14): the item is a fill with an image paint whose extends are both `PAD`, whose
 *   path is one closed subpath of four line segments, and whose four points are, bit for bit, the corners of the window
 *   mapped by the paint's map, in either winding and from either starting corner.
 *
 * @param item The item.
 * @param quad The four corners `(x0, y0), (x1, y0), (x1, y1), (x0, y1)` of the window as the paint's map sends them, 8
 *   doubles; written when the result is 1; may be NULL.
 * @return 1 if the fill is a drawn image; 0 for anything else.
 */
GSHSCENE_API int gshscene_image_fill_is_draw(const GSHSCENE_Item * item, double quad[8]);

/**
 * @brief The features a fill with an image paint requires of a sink (S21): the extend modes it uses in bits 0 to 3
 *   (`1 << extend`) and, from bit ::GSHSCENE_IMAGE_REQUIRED_FORMAT_SHIFT, the `GSHSCENE_IMAGE_FORMAT_*` bits of its image.
 *
 * @return The bits; 0 for NULL and for an item that has no image paint.
 */
GSHSCENE_API uint32_t gshscene_image_required(const GSHSCENE_Item * item);

/**
 * @brief Which filter a fill with an image paint uses (IP8): `magnify` when the composed map stretches every direction,
 *   `minify` otherwise.
 *
 * The chain is the target map, @p instance_maps (root to leaf), the item's map and the paint's, composed by
 * `gshgeom_map_compose_chain`.  A chain that cannot be composed (its entries leave the double range) has no one filter.
 *
 * @param target_map The target's map, or NULL for the identity.
 * @param out The ::GSHSCENE_Image_Filter; written only when the result is ::GSHSCENE_OK.
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID (NULL, not an image paint); ::GSHSCENE_ERR_LIMIT (not composable).
 */
GSHSCENE_API GSHSCENE_Result gshscene_image_filter_of(const GSHSCENE_Item * item, const GSHGEOM_Map * instance_maps,
    size_t instance_count, const GSHGEOM_Map * target_map, uint32_t * out);

/**
 * @brief Intern the path a front end fills to draw an image paint's window: one closed subpath whose four points are exactly
 *   the ones ::gshscene_image_fill_is_draw looks for.
 *
 * @param image_paint An image-paint resource.
 * @param out The path resource; a reference for the caller; written only on success.
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID (NULL, not an image paint); ::GSHSCENE_ERR_LIMIT; ::GSHSCENE_ERR_OOM.
 */
GSHSCENE_API GSHSCENE_Result gshscene_image_paint_rect_path(GSHSCENE_Store * store, const GSHSCENE_Resource * image_paint,
    const GSHSCENE_Resource ** out);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GSHSCENE_IMAGE_PAINT_H */
