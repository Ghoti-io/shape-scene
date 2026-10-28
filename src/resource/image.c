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
 * @file
 *
 * The image resource (image.h, IM1 to IM14): validation, the sample pass, the
 * tile hashes, equality, import by copy and by take, the readers and the dump.
 */

#include <ghoti.io/shape-scene/macros.h>

#include "../internal.h"

#include <string.h>

/** The format of an image's hash and dump (resource.h). */
#define IMAGE_FORMAT_VERSION 1u

/** `TILE` as a little-endian `u32`: the second field of a tile's hash (IM7). */
#define TILE_MAGIC 0x454c4954u

#define HEX(x) ((unsigned long long)(x))

/** An image as the hash, the equality and the sample pass see it: a descriptor over rows of samples. */
typedef struct Image_Source {
  GSHSCENE_Image_Info info;
  const unsigned char * rows; /* the first row */
  size_t stride;              /* bytes between rows */
  size_t row_bytes;           /* bytes of samples in a row */
  size_t sample_bytes;        /* 1, 2 or 4 */
  GIMG_Raster * raster;       /* the raster to take, else NULL */
  uint64_t * tiles;           /* the tile hashes, tiles_x * tiles_y of them */
  int adopted;                /* a miss gave the tile hashes to the resource: they are freed with it */
  uint64_t hash;              /* the content hash */
} Image_Source;

/* --- host byte order -------------------------------------------------------- */

/** Whether the host stores a multi-byte integer least significant byte first: then a sample's bytes are its hash's. */
static int host_little(void) {
  const uint16_t one = 1;
  unsigned char first;
  memcpy(&first, &one, 1);
  return first == 1;
}

/* --- validation (IM2, IM3, IM4, IM5) ---------------------------------------- */

/** The samples a format of @p bits has: 1, 2 or 4 bytes each. */
static size_t bytes_of(uint32_t bits) {
  return bits <= 8u ? 1u : bits <= 16u ? 2u : 4u;
}

/**
 * IM2's descriptor, read from the raster's format and @p params.  The result is the first of ::GSHSCENE_OK,
 * ::GSHSCENE_ERR_INVALID and ::GSHSCENE_ERR_UNSUPPORTED the rules give, in IM2's order: every invalid thing before any
 * unsupported one.  @p src's descriptor fields are written on ::GSHSCENE_OK.
 */
static GSHSCENE_Result describe(const GIMG_Raster * raster, const GSHSCENE_Image_Params * params, Image_Source * src) {
  const GIMG_Pixel_Format * f = gimg_raster_format(raster);
  uint32_t width = gimg_raster_width(raster);
  uint32_t height = gimg_raster_height(raster);
  int invalid = 0;
  int unsupported = 0;

  if (!f || width == 0 || height == 0) {
    return GSHSCENE_ERR_INVALID;
  }
  if (params->reserved != 0 || (params->flags & ~GSHSCENE_IMAGE_ALPHA_ONLY) != 0) {
    invalid = 1;
  }
  GSHSCENE_Result space = gshscene_color_space_check(&params->space);
  if (space == GSHSCENE_ERR_INVALID) {
    invalid = 1;
  } else if (space == GSHSCENE_ERR_UNSUPPORTED) {
    unsupported = 1;
  }

  /* The descriptor must agree with itself. */
  if ((unsigned)f->channel_model >= (unsigned)GIMG_CHANNEL_COUNT || (unsigned)f->channel_type >= (unsigned)GIMG_CHANNEL_TYPE_COUNT ||
      (unsigned)f->layout >= (unsigned)GIMG_LAYOUT_COUNT || f->channel_count == 0 || f->alpha_form > (uint8_t)GIMG_ALPHA_PREMULTIPLIED) {
    return GSHSCENE_ERR_INVALID;
  }
  uint32_t expected = 0;
  switch (f->channel_model) {
    case GIMG_CHANNEL_GRAY:
      expected = 1;
      break;
    case GIMG_CHANNEL_RGB:
      expected = 3;
      break;
    case GIMG_CHANNEL_RGBA:
      expected = 4;
      break;
    default:
      break;
  }
  if (expected != 0 && f->channel_count != expected) {
    invalid = 1;
  }
  uint32_t bits = f->bits_per_channel[0];
  size_t listed = f->channel_count < 8u ? f->channel_count : 8u;
  for (size_t i = 0; i < listed; i++) {
    if (f->bits_per_channel[i] != bits) {
      invalid = 1;
    }
  }
  if (bits == 0 || (f->channel_type == GIMG_CHANNEL_FLOAT && bits != 32u)) {
    invalid = 1;
  }
  /* The image library's values are mapped, not copied: this resource's are pinned because they are hashed. */
  uint32_t alpha = (uint32_t)GSHSCENE_IMAGE_ALPHA_NONE;
  if (f->channel_model == GIMG_CHANNEL_RGBA) {
    alpha = f->alpha_form == (uint8_t)GIMG_ALPHA_PREMULTIPLIED ? (uint32_t)GSHSCENE_IMAGE_ALPHA_PREMULTIPLIED
                                                                : (uint32_t)GSHSCENE_IMAGE_ALPHA_STRAIGHT; /* unsaid reads straight, as image does */
  } else if (f->alpha_form != (uint8_t)GIMG_ALPHA_NONE) {
    invalid = 1;
  }
  uint32_t alpha_only = (params->flags & GSHSCENE_IMAGE_ALPHA_ONLY) ? 1u : 0u;
  if (alpha_only && (f->channel_model != GIMG_CHANNEL_GRAY || f->channel_count != 1u)) {
    invalid = 1;
  }
  if (alpha_only && !gshscene_color_space_is_target(&params->space)) {
    invalid = 1; /* IM4: a field that does not apply takes its default */
  }
  if (invalid) {
    return GSHSCENE_ERR_INVALID;
  }

  /* Well formed: does this resource carry it? */
  if (f->channel_model != GIMG_CHANNEL_GRAY && f->channel_model != GIMG_CHANNEL_RGB && f->channel_model != GIMG_CHANNEL_RGBA) {
    unsupported = 1;
  }
  if (f->channel_type != GIMG_CHANNEL_UNORM && f->channel_type != GIMG_CHANNEL_FLOAT) {
    unsupported = 1;
  }
  if (f->layout != GIMG_LAYOUT_INTERLEAVED) {
    unsupported = 1;
  }
  if (f->channel_type == GIMG_CHANNEL_UNORM && bits != 8u && bits != 12u && bits != 16u) {
    unsupported = 1;
  }
  if (unsupported) {
    return GSHSCENE_ERR_UNSUPPORTED;
  }

  memset(&src->info, 0, sizeof src->info);
  src->info.width = width;
  src->info.height = height;
  src->info.layout = f->channel_model == GIMG_CHANNEL_GRAY ? (uint32_t)GSHSCENE_IMAGE_GRAY
      : f->channel_model == GIMG_CHANNEL_RGB ? (uint32_t)GSHSCENE_IMAGE_RGB : (uint32_t)GSHSCENE_IMAGE_RGBA;
  src->info.sample_type = f->channel_type == GIMG_CHANNEL_FLOAT ? (uint32_t)GSHSCENE_IMAGE_FLOAT : (uint32_t)GSHSCENE_IMAGE_UNORM;
  src->info.bits = bits;
  src->info.alpha_form = alpha;
  src->info.alpha_only = alpha_only;
  src->info.space = params->space;
  src->info.tile = GSHSCENE_IMAGE_TILE;
  src->info.tiles_x = (uint32_t)(((uint64_t)width + GSHSCENE_IMAGE_TILE - 1u) / GSHSCENE_IMAGE_TILE);
  src->info.tiles_y = (uint32_t)(((uint64_t)height + GSHSCENE_IMAGE_TILE - 1u) / GSHSCENE_IMAGE_TILE);
  src->sample_bytes = bytes_of(bits);
  return GSHSCENE_OK;
}

/** The accounted bytes of an image (IM10), saturating: its base, its samples and its tiles. */
static size_t accounted_bytes(const GSHSCENE_Image_Info * info) {
  uint64_t samples = (uint64_t)info->width * info->height * info->layout * bytes_of(info->bits);
  uint64_t tiles = (uint64_t)info->tiles_x * info->tiles_y;
  uint64_t total = GSHSCENE_BYTES_BASE + samples + tiles * GSHSCENE_BYTES_PER_TILE;
  return total > (uint64_t)SIZE_MAX ? SIZE_MAX : (size_t)total;
}

/** IM10: the dimensions, the texels and the accounted bytes against the store's, in 64 bits, before a sample is read. */
static GSHSCENE_Result within_limits(const GSHSCENE_Store * store, const GSHSCENE_Image_Info * info) {
  if (info->width > GSHSCENE_IMAGE_DIMENSION_MAX || info->height > GSHSCENE_IMAGE_DIMENSION_MAX) {
    return GSHSCENE_ERR_LIMIT;
  }
  if ((uint64_t)info->width * info->height > GSHSCENE_IMAGE_TEXELS_MAX) {
    return GSHSCENE_ERR_LIMIT;
  }
  if (accounted_bytes(info) > store->limits.max_bytes) {
    return GSHSCENE_ERR_LIMIT;
  }
  return GSHSCENE_OK;
}

/** Whether the bits of a binary32 are a finite number. */
static int float_finite(uint32_t bits) {
  return (bits & 0x7f800000u) != 0x7f800000u;
}

/** Whether a finite binary32 given by its bits lies in `[0, 1]` (`-0` does). */
static int float_unit(uint32_t bits) {
  float x;
  memcpy(&x, &bits, sizeof x);
  return x >= 0.0f && x <= 1.0f;
}

/** IM3: every sample of the source is a legal value, in one pass.  ::GSHSCENE_OK or ::GSHSCENE_ERR_INVALID. */
static GSHSCENE_Result check_samples(const Image_Source * src) {
  const GSHSCENE_Image_Info * info = &src->info;
  size_t layout = info->layout;
  int unit_last = info->sample_type == (uint32_t)GSHSCENE_IMAGE_FLOAT &&
      (info->alpha_form != (uint32_t)GSHSCENE_IMAGE_ALPHA_NONE || info->alpha_only);
  if (info->sample_type == (uint32_t)GSHSCENE_IMAGE_UNORM && info->bits != 12u) {
    return GSHSCENE_OK; /* every bit pattern of an 8- or 16-bit sample is a value */
  }
  for (uint32_t y = 0; y < info->height; y++) {
    const unsigned char * row = src->rows + (size_t)y * src->stride;
    for (uint32_t x = 0; x < info->width; x++) {
      for (size_t c = 0; c < layout; c++) {
        const unsigned char * at = row + ((size_t)x * layout + c) * src->sample_bytes;
        if (info->sample_type == (uint32_t)GSHSCENE_IMAGE_UNORM) {
          uint16_t v;
          memcpy(&v, at, sizeof v);
          if (v > 4095u) {
            return GSHSCENE_ERR_INVALID;
          }
        } else {
          uint32_t bits;
          memcpy(&bits, at, sizeof bits);
          if (!float_finite(bits) || (unit_last && c + 1 == layout && !float_unit(bits))) {
            return GSHSCENE_ERR_INVALID;
          }
        }
      }
    }
  }
  return GSHSCENE_OK;
}

/* --- hashes (IM6, IM7) ------------------------------------------------------- */

/** Mix @p count samples starting at @p at, each as its value (IM7). */
static void mix_samples(GSHGEOM_Hash * h, const unsigned char * at, size_t count, uint32_t sample_type, size_t sample_bytes,
    int little) {
  if (sample_bytes == 1 || little) {
    gshgeom_hash_bytes(h, at, count * sample_bytes);
    return;
  }
  for (size_t i = 0; i < count; i++) {
    if (sample_bytes == 2) {
      uint16_t v;
      memcpy(&v, at + i * 2u, sizeof v);
      gshgeom_hash_u16(h, v);
    } else {
      uint32_t v;
      memcpy(&v, at + i * 4u, sizeof v);
      gshgeom_hash_u32(h, v);
    }
  }
  (void)sample_type;
}

/** IM7: the hash of tile (@p tx, @p ty) of the source. */
static uint64_t hash_tile(const Image_Source * src, uint32_t tx, uint32_t ty, int little) {
  const GSHSCENE_Image_Info * info = &src->info;
  uint32_t x0 = tx * GSHSCENE_IMAGE_TILE;
  uint32_t y0 = ty * GSHSCENE_IMAGE_TILE;
  uint32_t x1 = (uint64_t)x0 + GSHSCENE_IMAGE_TILE < info->width ? x0 + GSHSCENE_IMAGE_TILE : info->width;
  uint32_t y1 = (uint64_t)y0 + GSHSCENE_IMAGE_TILE < info->height ? y0 + GSHSCENE_IMAGE_TILE : info->height;
  GSHGEOM_Hash h;
  gshgeom_hash_init(&h);
  gshgeom_hash_u32(&h, 1u);
  gshgeom_hash_u32(&h, TILE_MAGIC);
  gshgeom_hash_u32(&h, x1 - x0);
  gshgeom_hash_u32(&h, y1 - y0);
  for (uint32_t y = y0; y < y1; y++) {
    const unsigned char * at = src->rows + (size_t)y * src->stride + (size_t)x0 * info->layout * src->sample_bytes;
    mix_samples(&h, at, (size_t)(x1 - x0) * info->layout, info->sample_type, src->sample_bytes, little);
  }
  return gshgeom_hash_value(&h);
}

/** IM7: the image's content hash from its descriptor and its tile hashes. */
static uint64_t hash_image(const GSHSCENE_Image_Info * info, const uint64_t * tiles) {
  GSHGEOM_Hash h;
  gshgeom_hash_init(&h);
  gshgeom_hash_u32(&h, IMAGE_FORMAT_VERSION);
  gshgeom_hash_u32(&h, (uint32_t)GSHSCENE_RESOURCE_IMAGE);
  gshgeom_hash_u32(&h, info->width);
  gshgeom_hash_u32(&h, info->height);
  gshgeom_hash_u8(&h, (uint8_t)info->layout);
  gshgeom_hash_u8(&h, (uint8_t)info->sample_type);
  gshgeom_hash_u8(&h, (uint8_t)info->bits);
  gshgeom_hash_u8(&h, (uint8_t)info->alpha_form);
  gshgeom_hash_u8(&h, (uint8_t)info->alpha_only);
  gshscene_space_hash(&h, &info->space);
  gshgeom_hash_u32(&h, info->tile);
  uint64_t count = (uint64_t)info->tiles_x * info->tiles_y;
  gshgeom_hash_u64(&h, count);
  for (uint64_t i = 0; i < count; i++) {
    gshgeom_hash_u64(&h, tiles[i]);
  }
  return gshgeom_hash_value(&h);
}

/* --- equality (IM8) ------------------------------------------------------------ */

/** Whether two descriptors are equal, field for field (the gamma bit for bit). */
static int info_equal(const GSHSCENE_Image_Info * a, const GSHSCENE_Image_Info * b) {
  return a->width == b->width && a->height == b->height && a->layout == b->layout && a->sample_type == b->sample_type &&
      a->bits == b->bits && a->alpha_form == b->alpha_form && a->alpha_only == b->alpha_only && a->tile == b->tile &&
      gshscene_space_equal(&a->space, &b->space);
}

int gshscene_image_matches(const GSHSCENE_Resource * resource, const void * source) {
  const Image_Source * want = (const Image_Source *)source;
  const GSHSCENE_Image_Info * have = &resource->u.image.info;
  if (!info_equal(have, &want->info)) {
    return 0;
  }
  /* The samples, row by row over the row's own bytes and never the padding: as values, so `-0` is not `+0`. */
  for (uint32_t y = 0; y < have->height; y++) {
    if (memcmp(resource->u.image.rows + (size_t)y * resource->u.image.stride, want->rows + (size_t)y * want->stride,
            want->row_bytes) != 0) {
      return 0;
    }
  }
  return 1;
}

/* --- build, free, intern -------------------------------------------------------- */

static GSHSCENE_Result build_image(GSHSCENE_Store * store, const void * source, GSHSCENE_Resource ** out) {
  Image_Source * src = (Image_Source *)source;
  GSHSCENE_Resource * resource = gshscene_store_new_resource(store, GSHSCENE_RESOURCE_IMAGE);
  if (!resource) {
    return GSHSCENE_ERR_OOM;
  }
  resource->u.image.info = src->info;
  resource->u.image.row_bytes = src->row_bytes;
  if (src->raster) {
    /* A take: the resource reads the raster in place and owns it only once the intern has linked it (see take). */
    resource->u.image.rows = src->rows;
    resource->u.image.stride = src->stride;
    resource->u.image.raster = src->raster;
  } else {
    size_t bytes = src->row_bytes * src->info.height;
    resource->u.image.copy = gshscene_alloc(&store->alloc, bytes);
    if (!resource->u.image.copy) {
      gshscene_store_discard_resource(resource);
      return GSHSCENE_ERR_OOM;
    }
    for (uint32_t y = 0; y < src->info.height; y++) {
      memcpy(resource->u.image.copy + (size_t)y * src->row_bytes, src->rows + (size_t)y * src->stride, src->row_bytes);
    }
    resource->u.image.rows = resource->u.image.copy;
    resource->u.image.stride = src->row_bytes;
  }
  resource->u.image.tiles = src->tiles;
  src->adopted = 1; /* the resource has the tile hashes now */
  *out = resource;
  return GSHSCENE_OK;
}

void gshscene_image_free(GSHSCENE_Resource * resource) {
  const GSHSCENE_Allocator * alloc = &resource->store->alloc;
  gshscene_free(alloc, resource->u.image.tiles);
  gshscene_free(alloc, resource->u.image.copy);
  if (resource->u.image.owns_raster) {
    gimg_raster_destroy(resource->u.image.raster);
  }
}

/** IM2 to IM10 and the intern of one image: @p raster is read, and @p owned (the same raster, or NULL) is taken. */
static GSHSCENE_Result intern_image(GSHSCENE_Store * store, const GIMG_Raster * raster, GIMG_Raster * owned,
    const GSHSCENE_Image_Params * params, const GSHSCENE_Resource ** out) {
  if (!store || !raster || !out) {
    return GSHSCENE_ERR_INVALID;
  }
  int take = owned != NULL;
  GSHSCENE_Image_Params none;
  if (!params) {
    memset(&none, 0, sizeof none);
    params = &none;
  }
  if (take && gimg_raster_ownership(raster) != GIMG_RASTER_OWNED) {
    return GSHSCENE_ERR_INVALID; /* a borrowed buffer is never kept */
  }
  Image_Source src;
  memset(&src, 0, sizeof src);
  GSHSCENE_Result result = describe(raster, params, &src);
  if (result != GSHSCENE_OK) {
    return result;
  }
  result = within_limits(store, &src.info);
  if (result != GSHSCENE_OK) {
    return result;
  }
  src.sample_bytes = bytes_of(src.info.bits);
  src.row_bytes = (size_t)src.info.width * src.info.layout * src.sample_bytes;
  src.stride = gimg_raster_stride_bytes(raster);
  src.rows = (const unsigned char *)gimg_raster_pixels_const(raster);
  if (!src.rows || src.stride < src.row_bytes) {
    return GSHSCENE_ERR_INVALID;
  }
  result = check_samples(&src);
  if (result != GSHSCENE_OK) {
    return result;
  }

  size_t count = (size_t)src.info.tiles_x * src.info.tiles_y;
  src.tiles = gshscene_alloc(&store->alloc, count * sizeof *src.tiles);
  if (!src.tiles) {
    return GSHSCENE_ERR_OOM;
  }
  int little = host_little();
  for (uint32_t ty = 0; ty < src.info.tiles_y; ty++) {
    for (uint32_t tx = 0; tx < src.info.tiles_x; tx++) {
      src.tiles[(size_t)ty * src.info.tiles_x + tx] = hash_tile(&src, tx, ty, little);
    }
  }
  src.hash = hash_image(&src.info, src.tiles);
  src.raster = owned;

  GSHSCENE_Probe probe = {GSHSCENE_RESOURCE_IMAGE, src.hash, accounted_bytes(&src.info), &src, build_image};
  const GSHSCENE_Resource * found = NULL;
  result = gshscene_store_intern(store, &probe, &found);
  if (!src.adopted) {
    gshscene_free(&store->alloc, src.tiles);
  }
  if (result != GSHSCENE_OK) {
    return result; /* nothing was taken: the raster is still the caller's */
  }
  if (take) {
    GSHSCENE_Resource * live = (GSHSCENE_Resource *)found;
    if (live->u.image.raster == owned) {
      live->u.image.owns_raster = 1; /* ours is the one linked: the store owns the raster from here */
    } else {
      gimg_raster_destroy(owned); /* an equal image was already live: this raster is of no use */
    }
  }
  *out = found;
  return GSHSCENE_OK;
}

GSHSCENE_Result gshscene_store_intern_image(GSHSCENE_Store * store, const GIMG_Raster * raster,
    const GSHSCENE_Image_Params * params, const GSHSCENE_Resource ** out) {
  return intern_image(store, raster, NULL, params, out);
}

GSHSCENE_Result gshscene_store_intern_image_take(GSHSCENE_Store * store, GIMG_Raster * raster,
    const GSHSCENE_Image_Params * params, const GSHSCENE_Resource ** out) {
  return intern_image(store, raster, raster, params, out);
}

GSHSCENE_Result gshscene_image_space_from_raster(const GIMG_Raster * raster, GSHSCENE_Color_Space * out) {
  if (!raster || !out) {
    return GSHSCENE_ERR_INVALID;
  }
  return gshscene_color_space_from_info(gimg_raster_color_info_const(raster), out);
}

/* --- reading ---------------------------------------------------------------------- */

GSHSCENE_Result gshscene_resource_image(const GSHSCENE_Resource * resource, GSHSCENE_Image_Info * out) {
  if (!resource || !out || resource->kind != GSHSCENE_RESOURCE_IMAGE) {
    return GSHSCENE_ERR_INVALID;
  }
  *out = resource->u.image.info;
  return GSHSCENE_OK;
}

const void * gshscene_image_row(const GSHSCENE_Resource * resource, uint32_t y) {
  if (!resource || resource->kind != GSHSCENE_RESOURCE_IMAGE || y >= resource->u.image.info.height) {
    return NULL;
  }
  return resource->u.image.rows + (size_t)y * resource->u.image.stride;
}

size_t gshscene_image_stride(const GSHSCENE_Resource * resource) {
  return resource && resource->kind == GSHSCENE_RESOURCE_IMAGE ? resource->u.image.stride : 0;
}

uint64_t gshscene_image_tile_hash(const GSHSCENE_Resource * resource, uint32_t tx, uint32_t ty) {
  if (!resource || resource->kind != GSHSCENE_RESOURCE_IMAGE || tx >= resource->u.image.info.tiles_x ||
      ty >= resource->u.image.info.tiles_y) {
    return 0;
  }
  return resource->u.image.tiles[(size_t)ty * resource->u.image.info.tiles_x + tx];
}

/* --- the dump (IM12) ----------------------------------------------------------------- */

static const char * layout_name(uint32_t layout) {
  switch (layout) {
    case GSHSCENE_IMAGE_GRAY:
      return "gray";
    case GSHSCENE_IMAGE_GRAY_ALPHA:
      return "gray-alpha";
    case GSHSCENE_IMAGE_RGB:
      return "rgb";
    default:
      return "rgba";
  }
}

static const char * alpha_name(uint32_t alpha) {
  return alpha == (uint32_t)GSHSCENE_IMAGE_ALPHA_NONE ? "none"
      : alpha == (uint32_t)GSHSCENE_IMAGE_ALPHA_STRAIGHT ? "straight" : "premultiplied";
}

int gshscene_image_dump(const GSHSCENE_Resource * resource, FILE * out) {
  const GSHSCENE_Image_Info * info = &resource->u.image.info;
  if (fprintf(out, "image w=%u h=%u layout=%s type=%s bits=%u alpha=%s alphaonly=%u space=", info->width, info->height,
          layout_name(info->layout), info->sample_type == (uint32_t)GSHSCENE_IMAGE_FLOAT ? "float" : "unorm", info->bits,
          alpha_name(info->alpha_form), info->alpha_only) < 0 ||
      gshscene_space_dump(out, &info->space) < 0 ||
      fprintf(out, " tile=%u tiles=%ux%u\n", info->tile, info->tiles_x, info->tiles_y) < 0) {
    return -1;
  }
  for (uint32_t ty = 0; ty < info->tiles_y; ty++) {
    if (fprintf(out, "t %u", ty) < 0) {
      return -1;
    }
    for (uint32_t tx = 0; tx < info->tiles_x; tx++) {
      if (fprintf(out, " %016llx", HEX(resource->u.image.tiles[(size_t)ty * info->tiles_x + tx])) < 0) {
        return -1;
      }
    }
    if (fputc('\n', out) == EOF) {
      return -1;
    }
  }
  return 0;
}
