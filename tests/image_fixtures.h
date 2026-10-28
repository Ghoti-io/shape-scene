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
 * Rasters and images for the tests that need them: a format named by model, type, channels and bits; the samples of
 * tools/gen-scene-vectors.py's formula; an owned raster with padded rows and junk in the padding; and the calls that intern one.
 */

#ifndef GHOTI_IO_GSHSCENE_TEST_IMAGE_FIXTURES_H
#define GHOTI_IO_GSHSCENE_TEST_IMAGE_FIXTURES_H

#include "scene_helpers.h"

#include "alloc_tracker.h"

#include <cstdint>
#include <cstring>

namespace imgfx {

/** A format as a test names it: model, type, channels, bits, alpha. */
struct Fmt {
  GIMG_Channel_Model model;
  GIMG_Channel_Type type;
  unsigned channels;
  unsigned bits;
  GIMG_Alpha_Form alpha;
};

static const Fmt kGray8 = {GIMG_CHANNEL_GRAY, GIMG_CHANNEL_UNORM, 1, 8, GIMG_ALPHA_NONE};
static const Fmt kRgb8 = {GIMG_CHANNEL_RGB, GIMG_CHANNEL_UNORM, 3, 8, GIMG_ALPHA_NONE};
static const Fmt kRgba8 = {GIMG_CHANNEL_RGBA, GIMG_CHANNEL_UNORM, 4, 8, GIMG_ALPHA_STRAIGHT};
static const Fmt kRgba8P = {GIMG_CHANNEL_RGBA, GIMG_CHANNEL_UNORM, 4, 8, GIMG_ALPHA_PREMULTIPLIED};
static const Fmt kGray12 = {GIMG_CHANNEL_GRAY, GIMG_CHANNEL_UNORM, 1, 12, GIMG_ALPHA_NONE};
static const Fmt kGray16 = {GIMG_CHANNEL_GRAY, GIMG_CHANNEL_UNORM, 1, 16, GIMG_ALPHA_NONE};
static const Fmt kGrayF = {GIMG_CHANNEL_GRAY, GIMG_CHANNEL_FLOAT, 1, 32, GIMG_ALPHA_NONE};
static const Fmt kRgbF = {GIMG_CHANNEL_RGB, GIMG_CHANNEL_FLOAT, 3, 32, GIMG_ALPHA_NONE};
static const Fmt kRgbaF = {GIMG_CHANNEL_RGBA, GIMG_CHANNEL_FLOAT, 4, 32, GIMG_ALPHA_STRAIGHT};

inline GIMG_Pixel_Format format_of(const Fmt & f) {
  GIMG_Pixel_Format format;
  std::memset(&format, 0, sizeof format);
  format.channel_model = f.model;
  format.channel_type = f.type;
  format.layout = GIMG_LAYOUT_INTERLEAVED;
  format.channel_count = (uint8_t)f.channels;
  for (unsigned i = 0; i < f.channels && i < 8; i++) {
    format.bits_per_channel[i] = (uint8_t)f.bits;
  }
  format.alignment = 1;
  format.alpha_form = (uint8_t)f.alpha;
  return format;
}

/** tools/gen-scene-vectors.py's sample at column x, row y, channel c: the integer, or the binary32's bit pattern. */
inline uint32_t sample_value(unsigned bits, uint32_t x, uint32_t y, uint32_t c) {
  switch (bits) {
    case 8:
      return (7 * x + 13 * y + 29 * c + 1) % 256u;
    case 12:
      return (37 * x + 101 * y + 263 * c + 5) % 4096u;
    case 16:
      return (257 * x + 911 * y + 4099 * c + 0x1234u) % 65536u;
    default: {
      float f = (float)((7 * x + 3 * y + 5 * c) % 61u) / 64.0f;
      uint32_t out;
      std::memcpy(&out, &f, sizeof out);
      return out;
    }
  }
}

inline size_t sample_size(unsigned bits) {
  return bits <= 8 ? 1 : bits <= 16 ? 2 : 4;
}

inline void put_sample(unsigned char * at, unsigned bits, uint32_t v) {
  if (bits <= 8) {
    *at = (unsigned char)v;
  } else if (bits <= 16) {
    uint16_t w = (uint16_t)v;
    std::memcpy(at, &w, 2);
  } else {
    std::memcpy(at, &v, 4);
  }
}

/** Where a sample lives in a tightly packed or padded raster. */
inline unsigned char * sample_at(unsigned char * base, size_t stride, const Fmt & f, uint32_t x, uint32_t y, uint32_t c) {
  return base + (size_t)y * stride + ((size_t)x * f.channels + c) * sample_size(f.bits);
}

/** An allocator for rasters that counts what it hands out, and what comes back. */
struct RasterAlloc {
  tracker::Tracker t;
  GIMG_Allocator a;
  RasterAlloc() {
    GSHSCENE_Allocator s = tracker::allocator_for(&t);
    a.ctx = s.ctx;
    a.malloc_fn = s.malloc_fn;
    a.calloc_fn = s.calloc_fn;
    a.realloc_fn = s.realloc_fn;
    a.free_fn = s.free_fn;
  }
  size_t live() const { return t.live.size(); }
};

/** Fill a raster of @p f from the generator's formula, over @p base (junk in the padding). */
inline void fill_samples(unsigned char * base, size_t stride, uint32_t w, uint32_t h, const Fmt & f) {
  for (uint32_t y = 0; y < h; y++) {
    for (uint32_t x = 0; x < w; x++) {
      for (uint32_t c = 0; c < f.channels; c++) {
        put_sample(sample_at(base, stride, f, x, y, c), f.bits, sample_value(f.bits, x, y, c));
      }
    }
  }
}

/** An owned raster of the generator's samples, with @p pad bytes of @p junk after every row; NULL on failure. */
inline GIMG_Raster * make_raster(uint32_t w, uint32_t h, const Fmt & f, size_t pad = 0, unsigned char junk = 0xA5,
    const GIMG_Allocator * alloc = nullptr) {
  GIMG_Pixel_Format format = format_of(f);
  size_t row = (size_t)w * f.channels * sample_size(f.bits);
  size_t stride = row + pad;
  GIMG_Raster * raster = nullptr;
  if (gimg_raster_create_with_allocator(alloc, w, h, &format, GIMG_RASTER_OWNED, nullptr, stride, &raster) != GIMG_OK) {
    return nullptr;
  }
  unsigned char * base = (unsigned char *)gimg_raster_pixels(raster);
  std::memset(base, junk, stride * h);
  fill_samples(base, stride, w, h, f);
  return raster;
}

struct RasterHolder {
  GIMG_Raster * raster;
  explicit RasterHolder(GIMG_Raster * r) : raster(r) {}
  ~RasterHolder() { gimg_raster_destroy(raster); }
  RasterHolder(const RasterHolder &) = delete;
  RasterHolder & operator=(const RasterHolder &) = delete;
  GIMG_Raster * get() const { return raster; }
};

inline GSHSCENE_Image_Params params(GSHSCENE_Color_Space space = GSHSCENE_Color_Space{}, uint32_t flags = 0) {
  GSHSCENE_Image_Params p;
  std::memset(&p, 0, sizeof p);
  p.space = space;
  p.flags = flags;
  return p;
}

inline GSHSCENE_Store_Stats stats_of(GSHSCENE_Store * store) {
  GSHSCENE_Store_Stats s;
  std::memset(&s, 0, sizeof s);
  gshscene_store_stats(store, &s);
  return s;
}

inline bool same_stats(const GSHSCENE_Store_Stats & a, const GSHSCENE_Store_Stats & b) {
  return a.live == b.live && a.bytes == b.bytes && a.created == b.created && a.hits == b.hits;
}

/** Intern by copy; NULL and @p result set on failure. */
inline const GSHSCENE_Resource * copy_in(GSHSCENE_Store * store, const GIMG_Raster * raster, const GSHSCENE_Image_Params & p,
    GSHSCENE_Result * result = nullptr) {
  const GSHSCENE_Resource * r = nullptr;
  GSHSCENE_Result got = gshscene_store_intern_image(store, raster, &p, &r);
  if (result) {
    *result = got;
  }
  return got == GSHSCENE_OK ? r : nullptr;
}

inline GSHSCENE_Result copy_result(GSHSCENE_Store * store, const GIMG_Raster * raster, const GSHSCENE_Image_Params & p) {
  GSHSCENE_Result got;
  scene::Ref ref(copy_in(store, raster, p, &got));
  return got;
}

/** The accounted bytes image.h gives. */
inline size_t accounted(uint32_t w, uint32_t h, const Fmt & f) {
  size_t tiles = (size_t)((w + 63) / 64) * ((h + 63) / 64);
  return GSHSCENE_BYTES_BASE + (size_t)w * h * f.channels * sample_size(f.bits) + tiles * GSHSCENE_BYTES_PER_TILE;
}


}  // namespace imgfx

#endif
