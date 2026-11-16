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
 * @file filter.h
 *
 * The filter: what a group's filter slot names.  A filter is a region and a directed acyclic graph of primitives (Filter Effects
 * Module Level 1: offset, flood, merge, blend, composite, color matrix, component transfer, Gaussian blur, drop shadow, morphology,
 * convolve matrix, tile, image, displacement map, turbulence, diffuse and specular lighting) that processes the group's result at
 * its pop (FL1 to FL24).  This header defines the resource, its validity, canonical form, hash and dump (FL1 to FL6, FL12) and what a
 * sink needs to draw one (FL5, D-FL11); what each primitive computes is in `documentation/design.md` and is drawn by a sink.
 *
 * ### The content (FL1, FL2)
 *
 * `{region mode, region, color space, primitives}`.  Every length is in the user space of the group's items; a front end resolves
 * `objectBoundingBox` units before it interns the filter.  The region is `EXPLICIT` (four finite numbers, `width >= 0` and
 * `height >= 0`) or `AUTO` (the four numbers are zero: the sink derives the region from the content and each primitive's reach, FL5).
 * A primitive is `{kind, color space, inputs, subregion, parameters}`: a primitive takes `PREVIOUS` (the primitive before it, or
 * the source graphic for the first), `SOURCE_GRAPHIC`, `SOURCE_ALPHA` or `RESULT(j)` for an earlier primitive `j`, so the graph is
 * acyclic by construction.  The last primitive's result is the filter's.  The fields a kind does not use must be zero
 * (::GSHSCENE_ERR_INVALID otherwise): a field that cannot matter must not split a handle.
 *
 * ### Validity and limits (FL1, D-FL10)
 *
 * Decided in this order: ::GSHSCENE_ERR_INVALID (NULL, an enumeration out of range, a non-finite number, an input that names a
 * later primitive or a wrong number of inputs for the kind, a field out of its range or set where the kind forbids it, a handle of
 * another store or of the wrong kind); ::GSHSCENE_ERR_UNSUPPORTED (a 3D placement map); ::GSHSCENE_ERR_LIMIT (more than
 * ::GSHSCENE_FILTER_MAX_PRIMITIVES primitives, a kernel past ::GSHSCENE_FILTER_MAX_KERNEL entries, more than
 * ::GSHSCENE_FILTER_MAX_VALUES table entries over the filter, more than ::GSHSCENE_FILTER_MAX_MERGE merge inputs, octaves past
 * ::GSHSCENE_FILTER_MAX_OCTAVES, a magnitude past ::GSHSCENE_FILTER_VALUE_MAX).
 *
 * ### Hash, dump (FL12)
 *
 * Format 1.  The content is one **canonical byte string**, little-endian: `u32` format (1), `u32` kind 16, `u32` region mode, four
 * `f64` the region, `u8` the filter's color space, `u64` the primitive count, then for each primitive `u8` kind, `u8` color space,
 * `u64` the input count and per input `u8` kind and `u32` index, `u8` the subregion mask and four `f64`, then the groups of fields
 * its kind uses in the order offset (`f64` dx, dy), color (four `f64`, `u8` primaries, `u8` transfer, `f64` gamma), opacity (`f64`),
 * mode (`u32`), arithmetic (four `f64`, for `COMPOSITE` `ARITHMETIC` only), values (`u64` count, `f64` each), the four transfers
 * (`u8` type, `u64` count, `f64` each entry, `f64` slope, intercept, amplitude, exponent, offset), deviation (`f64` sx, sy), edge
 * (`u32`), convolution (`u32` order x and y, target x and y, `f64` divisor, bias, `u32` preserve alpha), displacement (`f64` scale,
 * `u32` x and y channel), noise (`u32` octaves, `f64` seed, `u32` stitch), lighting (`f64` surface scale, constant, the light: `u8` kind,
 * `f64` azimuth, elevation, three position, three points-at, spot exponent, `u8` has cone, `f64` cone angle), specular (`f64` exponent),
 * kernel unit length (two `f64`) and image (`u64` its content hash, the placement as `u32` dimension, `u32` kind and the `f64` entries
 * row by row each with its translation, `u32` rendering); every `f64` a `-0` spelled `+0`.  The hash is FNV-1a over `u32` 1, `u32`
 * 16, `u64` the string's length and its bytes; equality compares the strings and an image's handle.  The dump is a line
 * `filter region=M x=<hex> y=<hex> w=<hex> h=<hex> cspace=C primitives=N` and a line `fp i=I kind=K cspace=C sub=<mask> <hex> <hex> <hex>
 * <hex> in=<kind>:<index>,... params=<hash of the primitive's own canonical bytes>`, never an image's samples.
 */

#ifndef GHOTI_IO_GSHSCENE_FILTER_H
#define GHOTI_IO_GSHSCENE_FILTER_H

#include <ghoti.io/shape-scene/image_paint.h>
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

/** The most primitives in one filter (D-FL10). */
#define GSHSCENE_FILTER_MAX_PRIMITIVES ((size_t)256)
/** The most entries of one convolution kernel (`order_x * order_y`) (D-FL10). */
#define GSHSCENE_FILTER_MAX_KERNEL ((size_t)1024)
/** The most table entries or values over a whole filter (component-transfer tables, color-matrix values, kernels) (D-FL10). */
#define GSHSCENE_FILTER_MAX_VALUES ((size_t)4096)
/** The most inputs of one merge (D-FL10). */
#define GSHSCENE_FILTER_MAX_MERGE ((size_t)256)
/** The most octaves of a turbulence (D-FL10). */
#define GSHSCENE_FILTER_MAX_OCTAVES 16u
/** The largest magnitude of a length, a factor or a coefficient of a filter (D-FL10). */
#define GSHSCENE_FILTER_VALUE_MAX 65536.0

/** @brief How the filter region is given (FL1).  The values are pinned (they are hashed). */
typedef enum GSHSCENE_Filter_Region {
  GSHSCENE_FILTER_REGION_EXPLICIT = 0, ///< The four numbers, in the group's user space.
  GSHSCENE_FILTER_REGION_AUTO = 1,     ///< Derived by the sink from the content and the primitives' reach (FL5); the numbers are zero.
  GSHSCENE_FILTER_REGION_COUNT         ///< Closes the enum.
} GSHSCENE_Filter_Region;

/** @brief The color space a primitive works in (FL1, D-FL7).  The values are pinned; a filter's is never `INHERIT`. */
typedef enum GSHSCENE_Filter_Space {
  GSHSCENE_FILTER_SPACE_INHERIT = 0,    ///< A primitive: the filter's.
  GSHSCENE_FILTER_SPACE_LINEAR_RGB = 1, ///< `linearRGB`, the standard's default: sRGB's primaries with a linear transfer.
  GSHSCENE_FILTER_SPACE_SRGB = 2,       ///< `sRGB`.
  GSHSCENE_FILTER_SPACE_COUNT           ///< Closes the enum.
} GSHSCENE_Filter_Space;

/** @brief The primitives (FL3).  The values are pinned (they are hashed); zero is invalid. */
typedef enum GSHSCENE_Filter_Kind {
  GSHSCENE_FILTER_OFFSET = 1,             ///< `feOffset`.
  GSHSCENE_FILTER_FLOOD = 2,              ///< `feFlood`.
  GSHSCENE_FILTER_MERGE = 3,              ///< `feMerge`.
  GSHSCENE_FILTER_BLEND = 4,              ///< `feBlend`.
  GSHSCENE_FILTER_COMPOSITE = 5,          ///< `feComposite`.
  GSHSCENE_FILTER_COLOR_MATRIX = 6,       ///< `feColorMatrix`.
  GSHSCENE_FILTER_COMPONENT_TRANSFER = 7, ///< `feComponentTransfer`.
  GSHSCENE_FILTER_GAUSSIAN_BLUR = 8,      ///< `feGaussianBlur`.
  GSHSCENE_FILTER_DROP_SHADOW = 9,        ///< `feDropShadow`.
  GSHSCENE_FILTER_MORPHOLOGY = 10,        ///< `feMorphology`.
  GSHSCENE_FILTER_CONVOLVE_MATRIX = 11,   ///< `feConvolveMatrix`.
  GSHSCENE_FILTER_TILE = 12,              ///< `feTile`.
  GSHSCENE_FILTER_IMAGE = 13,             ///< `feImage`.
  GSHSCENE_FILTER_DISPLACEMENT_MAP = 14,  ///< `feDisplacementMap`.
  GSHSCENE_FILTER_TURBULENCE = 15,        ///< `feTurbulence`.
  GSHSCENE_FILTER_DIFFUSE_LIGHTING = 16,  ///< `feDiffuseLighting`.
  GSHSCENE_FILTER_SPECULAR_LIGHTING = 17, ///< `feSpecularLighting`.
  GSHSCENE_FILTER_KIND_COUNT              ///< Closes the enum.
} GSHSCENE_Filter_Kind;

/** @brief What a primitive's input names (FL2, D-FL4).  The values are pinned. */
typedef enum GSHSCENE_Filter_Input_Kind {
  GSHSCENE_FILTER_INPUT_PREVIOUS = 0,       ///< The previous primitive's result, or the source graphic for the first primitive (the default).
  GSHSCENE_FILTER_INPUT_SOURCE_GRAPHIC = 1, ///< The group's isolated result, cropped to the region.
  GSHSCENE_FILTER_INPUT_SOURCE_ALPHA = 2,   ///< Its alpha, with color zero.
  GSHSCENE_FILTER_INPUT_RESULT = 3,         ///< The result of the earlier primitive `index`.
  GSHSCENE_FILTER_INPUT_COUNT               ///< Closes the enum.
} GSHSCENE_Filter_Input_Kind;

/** @brief The operators of a `COMPOSITE` and the modes of the other primitives that take a `mode` (FL6).  Values pinned. */
typedef enum GSHSCENE_Filter_Composite_Op {
  GSHSCENE_FILTER_COMPOSITE_OVER = 0,
  GSHSCENE_FILTER_COMPOSITE_IN = 1,
  GSHSCENE_FILTER_COMPOSITE_OUT = 2,
  GSHSCENE_FILTER_COMPOSITE_ATOP = 3,
  GSHSCENE_FILTER_COMPOSITE_XOR = 4,
  GSHSCENE_FILTER_COMPOSITE_ARITHMETIC = 5,
  GSHSCENE_FILTER_COMPOSITE_COUNT
} GSHSCENE_Filter_Composite_Op;

/** @brief The types of a `COLOR_MATRIX` (FL14).  Values pinned. */
typedef enum GSHSCENE_Filter_Matrix_Type {
  GSHSCENE_FILTER_MATRIX_MATRIX = 0,             ///< Twenty values, row-major, over `(r, g, b, a, 1)`.
  GSHSCENE_FILTER_MATRIX_SATURATE = 1,           ///< One value `s >= 0`.
  GSHSCENE_FILTER_MATRIX_HUE_ROTATE = 2,         ///< One value, in degrees.
  GSHSCENE_FILTER_MATRIX_LUMINANCE_TO_ALPHA = 3, ///< No values.
  GSHSCENE_FILTER_MATRIX_COUNT
} GSHSCENE_Filter_Matrix_Type;

/** @brief The functions of a component transfer (FL15).  Values pinned. */
typedef enum GSHSCENE_Filter_Transfer_Type {
  GSHSCENE_FILTER_TRANSFER_IDENTITY = 0,
  GSHSCENE_FILTER_TRANSFER_TABLE = 1,    ///< `count >= 1` values, linear between them.
  GSHSCENE_FILTER_TRANSFER_DISCRETE = 2, ///< `count >= 1` values, steps.
  GSHSCENE_FILTER_TRANSFER_LINEAR = 3,   ///< `slope C + intercept`.
  GSHSCENE_FILTER_TRANSFER_GAMMA = 4,    ///< `amplitude C^exponent + offset`.
  GSHSCENE_FILTER_TRANSFER_COUNT
} GSHSCENE_Filter_Transfer_Type;

/** @brief What a morphology does (FL17).  Values pinned. */
typedef enum GSHSCENE_Filter_Morphology_Op {
  GSHSCENE_FILTER_MORPHOLOGY_ERODE = 0,
  GSHSCENE_FILTER_MORPHOLOGY_DILATE = 1,
  GSHSCENE_FILTER_MORPHOLOGY_COUNT
} GSHSCENE_Filter_Morphology_Op;

/** @brief How a blur or a convolution reads beyond its input (D-FL9, FL17).  Values pinned. */
typedef enum GSHSCENE_Filter_Edge {
  GSHSCENE_FILTER_EDGE_NONE = 0,      ///< Transparent black.
  GSHSCENE_FILTER_EDGE_DUPLICATE = 1, ///< The nearest edge pixel.
  GSHSCENE_FILTER_EDGE_WRAP = 2,      ///< The opposite edge.
  GSHSCENE_FILTER_EDGE_COUNT
} GSHSCENE_Filter_Edge;

/** @brief The channels a displacement map reads (FL17).  Values pinned. */
typedef enum GSHSCENE_Filter_Channel {
  GSHSCENE_FILTER_CHANNEL_R = 0,
  GSHSCENE_FILTER_CHANNEL_G = 1,
  GSHSCENE_FILTER_CHANNEL_B = 2,
  GSHSCENE_FILTER_CHANNEL_A = 3,
  GSHSCENE_FILTER_CHANNEL_COUNT
} GSHSCENE_Filter_Channel;

/** @brief What a turbulence makes (FL18).  Values pinned. */
typedef enum GSHSCENE_Filter_Noise {
  GSHSCENE_FILTER_NOISE_TURBULENCE = 0,
  GSHSCENE_FILTER_NOISE_FRACTAL = 1,
  GSHSCENE_FILTER_NOISE_COUNT
} GSHSCENE_Filter_Noise;

/** @brief The light of a lighting primitive (FL19).  Values pinned. */
typedef enum GSHSCENE_Filter_Light_Kind {
  GSHSCENE_FILTER_LIGHT_DISTANT = 0, ///< `azimuth`, `elevation` in degrees.
  GSHSCENE_FILTER_LIGHT_POINT = 1,   ///< `position`.
  GSHSCENE_FILTER_LIGHT_SPOT = 2,    ///< `position`, `points_at`, `spot_exponent`, an optional cone.
  GSHSCENE_FILTER_LIGHT_COUNT
} GSHSCENE_Filter_Light_Kind;

/** @brief One input of a primitive (FL2). */
typedef struct GSHSCENE_Filter_Input {
  uint32_t kind;  ///< A ::GSHSCENE_Filter_Input_Kind.
  uint32_t index; ///< `RESULT`: the earlier primitive's index; zero otherwise.
} GSHSCENE_Filter_Input;

/** @brief One channel's function of a component transfer (FL15). */
typedef struct GSHSCENE_Filter_Transfer {
  uint32_t type;           ///< A ::GSHSCENE_Filter_Transfer_Type.
  size_t count;            ///< `TABLE`, `DISCRETE`: the entries (at least 1, finite); zero otherwise.
  const double * table;    ///< The entries; NULL otherwise.
  double slope;            ///< `LINEAR`.
  double intercept;        ///< `LINEAR`.
  double amplitude;        ///< `GAMMA`.
  double exponent;         ///< `GAMMA`, in `[0, GSHSCENE_FILTER_VALUE_MAX]`.
  double offset;           ///< `GAMMA`.
} GSHSCENE_Filter_Transfer;

/** @brief The light source of a lighting primitive (FL19). */
typedef struct GSHSCENE_Filter_Light {
  uint32_t kind;          ///< A ::GSHSCENE_Filter_Light_Kind.
  double azimuth;         ///< `DISTANT`, degrees.
  double elevation;       ///< `DISTANT`, degrees.
  double position[3];     ///< `POINT`, `SPOT`: in user space.
  double points_at[3];    ///< `SPOT`.
  double spot_exponent;   ///< `SPOT`: positive (the standard's `specularExponent` of the light, default 1).
  uint32_t has_cone;      ///< `SPOT`: 0 or 1, whether the cone below applies.
  double cone_angle;      ///< `SPOT`, with a cone: `limitingConeAngle` in degrees, finite.
} GSHSCENE_Filter_Light;

/** @brief One primitive of a filter (FL2, FL6).  Fields a kind does not use are zero. */
typedef struct GSHSCENE_Filter_Primitive {
  uint32_t kind;                      ///< A ::GSHSCENE_Filter_Kind; nonzero.
  uint32_t color_space;               ///< A ::GSHSCENE_Filter_Space.
  size_t input_count;                 ///< 0 for `FLOOD`, `TURBULENCE`, `IMAGE`; 2 for `BLEND`, `COMPOSITE`, `DISPLACEMENT_MAP`; 1 to ::GSHSCENE_FILTER_MAX_MERGE for `MERGE`; 1 for the rest.
  const GSHSCENE_Filter_Input * inputs; ///< `input_count` of them; NULL for none.
  uint32_t subregion_mask;            ///< Bit 0 `x`, bit 1 `y`, bit 2 `width`, bit 3 `height`: which of @p subregion are given (FL4).
  double subregion[4];                ///< `x, y, width, height`, user space; a given width or height is `>= 0`; zero where not given.
  double dx;                          ///< `OFFSET`, `DROP_SHADOW`.
  double dy;                          ///< `OFFSET`, `DROP_SHADOW`.
  GSHSCENE_Color color;               ///< `FLOOD`, `DROP_SHADOW`, the lighting color: straight, in its own space (zero: the target's).
  double opacity;                     ///< `FLOOD`, `DROP_SHADOW`: in `[0, 1]`.
  uint32_t mode;                      ///< `BLEND`: a ::GSHSCENE_Composite blend mode of P3's list (the 16 of the standard); `COMPOSITE`: a ::GSHSCENE_Filter_Composite_Op; `COLOR_MATRIX`: a ::GSHSCENE_Filter_Matrix_Type; `MORPHOLOGY`: a ::GSHSCENE_Filter_Morphology_Op; `TURBULENCE`: a ::GSHSCENE_Filter_Noise.
  double k[4];                        ///< `COMPOSITE` arithmetic: `k1` to `k4`.
  size_t value_count;                 ///< `COLOR_MATRIX`: 20 for `MATRIX`, 1 for `SATURATE` and `HUE_ROTATE`, 0 otherwise; `CONVOLVE_MATRIX`: `order_x * order_y`.
  const double * values;              ///< Those values; NULL for none.
  GSHSCENE_Filter_Transfer transfer[4]; ///< `COMPONENT_TRANSFER`: R, G, B, A.
  double sx;                          ///< `GAUSSIAN_BLUR`, `DROP_SHADOW`: `stdDeviation` x; `MORPHOLOGY`: `rx`; `TURBULENCE`: `baseFrequency` x.  Nonnegative.
  double sy;                          ///< The y of the same.
  uint32_t edge_mode;                 ///< `GAUSSIAN_BLUR`, `CONVOLVE_MATRIX`: a ::GSHSCENE_Filter_Edge.
  uint32_t order_x;                   ///< `CONVOLVE_MATRIX`: at least 1.
  uint32_t order_y;                   ///< `CONVOLVE_MATRIX`: at least 1.
  uint32_t target_x;                  ///< `CONVOLVE_MATRIX`: below `order_x`.
  uint32_t target_y;                  ///< `CONVOLVE_MATRIX`: below `order_y`.
  double divisor;                     ///< `CONVOLVE_MATRIX`: nonzero.
  double bias;                        ///< `CONVOLVE_MATRIX`.
  uint32_t preserve_alpha;            ///< `CONVOLVE_MATRIX`: 0 or 1.
  double scale;                       ///< `DISPLACEMENT_MAP`.
  uint32_t x_channel;                 ///< `DISPLACEMENT_MAP`: a ::GSHSCENE_Filter_Channel.
  uint32_t y_channel;                 ///< `DISPLACEMENT_MAP`.
  uint32_t octaves;                   ///< `TURBULENCE`: at most ::GSHSCENE_FILTER_MAX_OCTAVES.
  double seed;                        ///< `TURBULENCE`.
  uint32_t stitch_tiles;              ///< `TURBULENCE`: 0 or 1.
  double surface_scale;               ///< Lighting.
  double constant;                    ///< Lighting: `diffuseConstant` or `specularConstant`, nonnegative.
  double exponent;                    ///< `SPECULAR_LIGHTING`: `specularExponent` in `[1, 128]`.
  double kernel_unit_length[2];       ///< Lighting, `CONVOLVE_MATRIX`: carried; nonnegative (FL6, open question 5).
  GSHSCENE_Filter_Light light;        ///< Lighting.
  const GSHSCENE_Resource * image;    ///< `IMAGE`: an image resource of the same store; retained.
  GSHGEOM_Map placement;              ///< `IMAGE`: the image's space to the group's user space; a zero-filled map is the identity.
  uint32_t rendering;                 ///< `IMAGE`: a ::GSHSCENE_Image_Filter of the image paint's list.
} GSHSCENE_Filter_Primitive;

/** @brief The content of a filter (FL1). */
typedef struct GSHSCENE_Filter {
  uint32_t region_mode;                    ///< A ::GSHSCENE_Filter_Region.
  double region[4];                        ///< `x, y, width, height`, user space; zero for `AUTO`.
  uint32_t color_space;                    ///< `LINEAR_RGB` or `SRGB`; the filter's default for a primitive.
  size_t primitive_count;                  ///< At least 1, at most ::GSHSCENE_FILTER_MAX_PRIMITIVES.
  const GSHSCENE_Filter_Primitive * primitives; ///< `primitive_count` of them.
} GSHSCENE_Filter;

/**
 * @brief Intern a filter (FL1).
 *
 * The content is copied (the inputs, tables, values and light too); an image is retained.  Equal content (bit for bit, `-0`
 * spelled `+0`) is one handle while one is held; a hash never decides.
 *
 * @param store The store.
 * @param filter The content.
 * @param out The handle, with a reference for the caller; written only on success.
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID, ::GSHSCENE_ERR_UNSUPPORTED, ::GSHSCENE_ERR_LIMIT (in that order of decision, see
 *   the file comment); ::GSHSCENE_ERR_OOM.
 */
GSHSCENE_API GSHSCENE_Result gshscene_store_intern_filter(
    GSHSCENE_Store * store, const GSHSCENE_Filter * filter, const GSHSCENE_Resource ** out);

/**
 * @brief Read a filter resource back.
 *
 * @param out The content as stored (canonical); the arrays point into the resource and are valid while a handle is held.
 *   Written only on success.
 * @return ::GSHSCENE_OK or ::GSHSCENE_ERR_INVALID (NULL, not a filter).
 */
GSHSCENE_API GSHSCENE_Result gshscene_resource_filter(const GSHSCENE_Resource * resource, GSHSCENE_Filter * out);

/** @name What a filter needs of a sink (D-FL11): `GSHSCENE_Sink_Caps.filter_native`
 *  Bit `kind` for each ::GSHSCENE_Filter_Kind drawn (bits 1 to 17), and the four features below.
 *  @{ */
#define GSHSCENE_FILTER_NATIVE_REGION_AUTO (UINT32_C(1) << 24)       ///< A filter whose region is `AUTO` (FL3).
#define GSHSCENE_FILTER_NATIVE_CANONICAL_SPACE (UINT32_C(1) << 25)   ///< A filter on a group whose map has a rotation (not a multiple of 90 degrees) or a shear (D-FL8).
#define GSHSCENE_FILTER_NATIVE_EDGE_DUPLICATE_WRAP (UINT32_C(1) << 26) ///< A blur or convolution whose edge mode is `DUPLICATE` or `WRAP` (D-FL9).
#define GSHSCENE_FILTER_NATIVE_BACKDROP (UINT32_C(1) << 27)          ///< A group whose filter's source is the backdrop under it (FL22, D-FL13): the planning pass adds it for a `BACKDROP` group, not ::gshscene_filter_required.
/** Every bit a filter can require (the kinds 1 to 17 and the four features). */
#define GSHSCENE_FILTER_NATIVE_ALL ((UINT32_C(0x3fffe)) | GSHSCENE_FILTER_NATIVE_REGION_AUTO | GSHSCENE_FILTER_NATIVE_CANONICAL_SPACE | GSHSCENE_FILTER_NATIVE_EDGE_DUPLICATE_WRAP | GSHSCENE_FILTER_NATIVE_BACKDROP)
/** @} */

/**
 * @brief The `GSHSCENE_FILTER_NATIVE_*` bits a filter requires of a sink, other than the map (D-FL11).
 *
 * The bit of each primitive's kind, `REGION_AUTO` for an `AUTO` region, `EDGE_DUPLICATE_WRAP` for a blur or convolution that
 * says `DUPLICATE` or `WRAP`.  The planning pass adds `CANONICAL_SPACE` when the group's composed map needs it
 * (::gshscene_filter_axis_aligned).
 *
 * @return The bits; 0 for NULL or a resource that is not a filter.
 */
GSHSCENE_API uint32_t gshscene_filter_required(const GSHSCENE_Resource * resource);

/**
 * @brief Whether a map's linear part is axis aligned (D-FL8): a scale, a mirror, a turn by a multiple of 90 degrees, or a
 *   composition of those (exactly: one entry of each row and column is zero).
 *
 * @param map A 2D map; a map of another dimension is not axis aligned.
 */
GSHSCENE_API int gshscene_filter_axis_aligned(const GSHGEOM_Map * map);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GSHSCENE_FILTER_H */
