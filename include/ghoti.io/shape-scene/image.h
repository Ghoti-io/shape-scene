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
 * @file image.h
 *
 * The image resource: raster content held by the scene as the samples that were
 * imported, unchanged, interned by content.  Rules IM1 to IM14.
 *
 * ### What an image is (IM1)
 *
 * `{width, height, layout, sample type, bits, alpha form, alpha-only, space, samples}`.
 * Two images are one handle exactly when every field is equal and every sample is
 * equal as a value; a hash never decides.  `width` and `height` are in `[1, 2^16]`.
 * The `layout` is the number of samples a pixel has, interleaved in the order R, G, B,
 * A (a gray pixel is its gray, then its alpha): gray 1, gray-alpha 2 (reserved: the
 * value exists and is ::GSHSCENE_ERR_UNSUPPORTED until the image library has the
 * model), RGB 3, RGBA 4.  A sample is a `UNORM` value at 8, 12 (right-justified in a
 * `uint16_t`, `0..4095`) or 16 bits, or a `FLOAT` at 32.  The alpha form is none
 * (gray and RGB), straight or premultiplied (RGBA).  Nothing is converted,
 * premultiplied, widened, clamped or normalized when an image is interned: what a
 * sample means is the paint's definition, and converting is the sink's.
 *
 * ### Accepted formats (IM2) and sample values (IM3)
 *
 * A `GIMG_Pixel_Format` whose channel model is gray, RGB or RGBA, interleaved, `UNORM`
 * at 8, 12 or 16 bits or `FLOAT` at 32.  ::GSHSCENE_ERR_INVALID: a NULL argument, a
 * descriptor that contradicts itself (a channel count that is not the model's,
 * channels of unequal width, a float of any width but 32, an alpha form out of range
 * or not allowed for the model), a flag or a reserved field that is not defined, a
 * partly given space, alpha-only on a layout other than gray.
 * ::GSHSCENE_ERR_UNSUPPORTED: a well-formed descriptor this resource does not carry
 * (CMYK, YCbCr, Lab, indexed and unknown models, signed and unsigned integer types,
 * planar layout, `UNORM` at any other depth, a space the library does not carry).
 * ::GSHSCENE_ERR_LIMIT: past IM10's bounds.  Then the samples: every bit pattern of an
 * integer type is accepted except a 12-bit sample above 4095; a float sample must be
 * finite, and the alpha channel of a float image (and an alpha-only image's one
 * channel) must lie in `[0, 1]`.  A float color has no magnitude bound (the decode
 * saturates it, N1).  A premultiplied color above its alpha is legal data, and `-0`
 * is kept: an image of `-0` samples and one of `+0` samples are two images.  The
 * checks run in this order: invalid, unsupported, limit, samples, then
 * ::GSHSCENE_ERR_OOM; a refused call leaves the store as it was.
 *
 * ### Alpha-only (IM4)
 *
 * An image marked alpha-only has layout gray and alpha form none; its one channel is
 * read as alpha, never as a color, and the color comes from the paint that uses it.
 * Its space is the zero value (a field that does not apply takes its default).  The
 * mark is content: the same gray bytes with and without it are two images.
 *
 * ### The color description (IM5)
 *
 * The `space` in ::GSHSCENE_Image_Params is the zero value (the target's space, which
 * is what PDF's `DeviceRGB` stands for) or a space ::gshscene_color_space_check accepts.
 * Nothing is guessed from the raster: ::gshscene_image_space_from_raster reads a
 * raster's description for the caller who wants that.  The transfer applies to the
 * color channels, never to alpha.  A gray image's value is `r = g = b` in the space.
 *
 * ### Tiles (IM6)
 *
 * ::GSHSCENE_IMAGE_TILE (64) texels is the edge of a content tile.  The grid is
 * `ceil(w / 64)` by `ceil(h / 64)` tiles in row-major order; an edge tile is partial and
 * keeps its true size.  A tile is the unit of hashing and of equality.  The tile is
 * logical: the samples are one buffer, read by row (IM11).
 *
 * ### The hash (IM7)
 *
 * In shape-geometry's spelling (FNV-1a 64, little-endian fields).  A tile: `u32` 1,
 * `u32` `0x454c4954`, `u32` the tile's width, `u32` its height, then its samples row
 * by row, each as its **value**: `u8` for `UNORM8`, `u16` for `UNORM12` and `UNORM16`,
 * `u32` the IEEE bit pattern for `FLOAT32`.  Never a byte of padding and never a byte
 * as the host stores it, so a big-endian host hashes the same image the same.  The
 * resource, after resource.h's two leading fields (`u32` 1, `u32` 5): `u32` width, `u32`
 * height, `u8` layout, `u8` sample type, `u8` bits, `u8` alpha form, `u8` alpha-only,
 * the space (`u8` primaries, `u8` transfer, `f64` gamma), `u32` ::GSHSCENE_IMAGE_TILE,
 * `u64` the tile count, then `u64` each tile's hash in tile order.
 *
 * ### Import (IM9)
 *
 * ::gshscene_store_intern_image **copies**: the raster may have any ownership and any
 * stride not smaller than a row, and the copy is tightly packed (the padding is not
 * content).  ::gshscene_store_intern_image_take **takes** an owned raster: on success
 * it belongs to the store whether the image was new (the store keeps it and destroys it
 * at the image's last release) or equal to a live one (the live handle is returned and
 * the raster is destroyed); on any error it is untouched and still the caller's.
 *
 * ### Limits (IM10)
 *
 * ::GSHSCENE_IMAGE_DIMENSION_MAX and ::GSHSCENE_IMAGE_TEXELS_MAX, and the store's
 * `max_bytes` against the image's accounted bytes (resource.h), are decided in 64-bit
 * arithmetic before a sample is read or anything is allocated.
 *
 * ### The dump (IM12)
 *
 * After resource.h's header line: `image w=N h=N layout=L type=T bits=B alpha=A
 * alphaonly=0|1 space=<space> tile=64 tiles=XxY` (layout `gray`, `gray-alpha`, `rgb`,
 * `rgba`; type `unorm`, `float`; alpha `none`, `straight`, `premultiplied`), then a line
 * `t <ty> <hash> ...` per tile row.  Never the samples.
 *
 * ### Not a shape (IM14)
 *
 * An image is raster content.  A fill, clip, stroke or instance that names one where it
 * takes a path, a run or a picture is ::GSHSCENE_ERR_INVALID.  Nothing in a list can
 * name an image until the paint that samples it exists.
 */

#ifndef GHOTI_IO_GSHSCENE_IMAGE_H
#define GHOTI_IO_GSHSCENE_IMAGE_H

#include <ghoti.io/shape-scene/core.h>
#include <ghoti.io/shape-scene/macros.h>
#include <ghoti.io/shape-scene/paint.h>
#include <ghoti.io/shape-scene/resource.h>
#include <ghoti.io/shape-scene/types.h>

#include <ghoti.io/image/raster.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The edge of a content tile in texels (IM6).  It is hashed, so changing it changes every image's hash. */
#define GSHSCENE_IMAGE_TILE 64u

/** The most texels along either side (IM10). */
#define GSHSCENE_IMAGE_DIMENSION_MAX 65536u

/** The most texels in an image (IM10): `2^28`. */
#define GSHSCENE_IMAGE_TEXELS_MAX ((uint64_t)268435456u)

/** The accounted bytes added for each tile (IM10): its hash and its share of the table. */
#define GSHSCENE_BYTES_PER_TILE ((size_t)16)

/** The one flag of ::GSHSCENE_Image_Params: the image's one channel is alpha (IM4). */
#define GSHSCENE_IMAGE_ALPHA_ONLY 1u

/** @brief How many samples a pixel has, interleaved.  The values are pinned (they are hashed). */
typedef enum GSHSCENE_Image_Layout {
  GSHSCENE_IMAGE_GRAY = 1,       ///< One sample, gray.
  GSHSCENE_IMAGE_GRAY_ALPHA = 2, ///< Reserved: ::GSHSCENE_ERR_UNSUPPORTED until the image library has the model.
  GSHSCENE_IMAGE_RGB = 3,        ///< Red, green, blue.
  GSHSCENE_IMAGE_RGBA = 4        ///< Red, green, blue, alpha.
} GSHSCENE_Image_Layout;

/** @brief What a sample is.  The values are pinned (they are hashed). */
typedef enum GSHSCENE_Image_Sample_Type {
  GSHSCENE_IMAGE_UNORM = 0, ///< An unsigned normalized integer of 8, 12 or 16 bits.
  GSHSCENE_IMAGE_FLOAT = 1  ///< An IEEE 754 binary32.
} GSHSCENE_Image_Sample_Type;

/** @brief How an image's alpha channel relates to its color channels.  The values are pinned (they are hashed). */
typedef enum GSHSCENE_Image_Alpha {
  GSHSCENE_IMAGE_ALPHA_NONE = 0,          ///< No alpha channel (gray, RGB).
  GSHSCENE_IMAGE_ALPHA_STRAIGHT = 1,      ///< Color is independent of alpha.
  GSHSCENE_IMAGE_ALPHA_PREMULTIPLIED = 2  ///< Color has been multiplied by alpha.
} GSHSCENE_Image_Alpha;

/** @brief What an import is told that the raster does not say (IM9). */
typedef struct GSHSCENE_Image_Params {
  GSHSCENE_Color_Space space; ///< The color description (IM5): zero for the target's space.
  uint32_t flags;             ///< ::GSHSCENE_IMAGE_ALPHA_ONLY or 0; no other bit is defined.
  uint32_t reserved;          ///< Zero.
} GSHSCENE_Image_Params;

/** @brief An image's descriptor, as read back (IM1, IM11). */
typedef struct GSHSCENE_Image_Info {
  uint32_t width;      ///< Texels across, `1..2^16`.
  uint32_t height;     ///< Texels down, `1..2^16`.
  uint32_t layout;     ///< A ::GSHSCENE_Image_Layout.
  uint32_t sample_type; ///< A ::GSHSCENE_Image_Sample_Type.
  uint32_t bits;       ///< Bits of a sample: 8, 12 or 16 for `UNORM`, 32 for `FLOAT`.
  uint32_t alpha_form; ///< A ::GSHSCENE_Image_Alpha.
  uint32_t alpha_only; ///< 1 when the one channel is alpha (IM4), else 0.
  GSHSCENE_Color_Space space; ///< The color description (IM5).
  uint32_t tile;       ///< ::GSHSCENE_IMAGE_TILE.
  uint32_t tiles_x;    ///< `ceil(width / tile)`.
  uint32_t tiles_y;    ///< `ceil(height / tile)`.
} GSHSCENE_Image_Info;

/**
 * @brief Intern an image by copying @p raster's samples (IM9).
 *
 * @param store The store.
 * @param raster Any raster of an accepted format; its samples are copied tightly packed, and the store keeps no
 *   pointer into it.  Its own color info is not read (IM5).
 * @param params The space and flags, or NULL for the zero space and no flags.
 * @param out The handle, with a reference for the caller; written only on success.
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID; ::GSHSCENE_ERR_UNSUPPORTED; ::GSHSCENE_ERR_LIMIT; ::GSHSCENE_ERR_OOM
 *   (in the order the file comment gives).
 */
GSHSCENE_API GSHSCENE_Result gshscene_store_intern_image(GSHSCENE_Store * store, const GIMG_Raster * raster,
    const GSHSCENE_Image_Params * params, const GSHSCENE_Resource ** out);

/**
 * @brief Intern an image by taking an owned @p raster (IM9).
 *
 * On ::GSHSCENE_OK the raster belongs to the store and the caller never touches it again.  On any error it is
 * untouched and still the caller's.  A `GIMG_RASTER_BORROWED` raster is ::GSHSCENE_ERR_INVALID.  Otherwise as
 * ::gshscene_store_intern_image.
 */
GSHSCENE_API GSHSCENE_Result gshscene_store_intern_image_take(GSHSCENE_Store * store, GIMG_Raster * raster,
    const GSHSCENE_Image_Params * params, const GSHSCENE_Resource ** out);

/**
 * @brief The color space a raster's description names (IM5).
 *
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID (NULL, or primaries or transfer not stated: nothing is guessed);
 *   ::GSHSCENE_ERR_UNSUPPORTED (an ICC profile or a path to one, even when the decoder also named a gamut, or a space
 *   the library does not carry).
 */
GSHSCENE_API GSHSCENE_Result gshscene_image_space_from_raster(const GIMG_Raster * raster, GSHSCENE_Color_Space * out);

/**
 * @brief An image resource's descriptor.
 *
 * @return ::GSHSCENE_OK or ::GSHSCENE_ERR_INVALID (NULL, not an image).
 */
GSHSCENE_API GSHSCENE_Result gshscene_resource_image(const GSHSCENE_Resource * resource, GSHSCENE_Image_Info * out);

/**
 * @brief A pointer to the first sample of row @p y (IM11).
 *
 * The row is contiguous and valid while a handle is held.  Samples are in the host's representation: a 16-bit sample
 * is read as a `uint16_t` value.  Nothing is written through the pointer.
 *
 * @return The row; NULL for NULL, a resource that is not an image, or @p y at or past the height.
 */
GSHSCENE_API const void * gshscene_image_row(const GSHSCENE_Resource * resource, uint32_t y);

/** @brief The distance in bytes between two rows of ::gshscene_image_row; 0 for NULL and for a resource that is not an image. */
GSHSCENE_API size_t gshscene_image_stride(const GSHSCENE_Resource * resource);

/** @brief The hash of tile (@p tx, @p ty) (IM7); 0 for NULL, a resource that is not an image, or a tile outside the grid. */
GSHSCENE_API uint64_t gshscene_image_tile_hash(const GSHSCENE_Resource * resource, uint32_t tx, uint32_t ty);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GSHSCENE_IMAGE_H */
