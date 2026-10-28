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
 * The image resource (image.h, IM1 to IM14), one test group per behavior it states.  The samples of every
 * raster here come from the formula tools/gen-scene-vectors.py states, so the generator's hashes can be compared with
 * the library's.
 */

#include "scene_helpers.h"

#include "../alloc_tracker.h"
#include "../image_fixtures.h"
#include "../known_answers.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace scene;

using namespace imgfx;

namespace {

const double kInf = std::numeric_limits<double>::infinity();

Fmt fmt_of(const known::ImageCase & c) {
  Fmt f;
  f.model = c.layout == 1 ? GIMG_CHANNEL_GRAY : c.layout == 3 ? GIMG_CHANNEL_RGB : GIMG_CHANNEL_RGBA;
  f.type = c.type ? GIMG_CHANNEL_FLOAT : GIMG_CHANNEL_UNORM;
  f.channels = c.layout;
  f.bits = c.bits;
  f.alpha = (GIMG_Alpha_Form)c.alpha;
  return f;
}

GSHSCENE_Color_Space space_of_case(const known::ImageCase & c) {
  return space_of((GCOL_Primaries)c.primaries, (GCOL_Transfer)c.transfer, c.gamma);
}

}  // namespace

/** Every sample of every row of @p ref reads back as the generator's formula gives it (IM11). */
void expect_rows(const GSHSCENE_Resource * ref, const known::ImageCase & c) {
  for (uint32_t y = 0; y < c.height; y++) {
    const unsigned char * row = (const unsigned char *)gshscene_image_row(ref, y);
    ASSERT_NE(row, nullptr);
    for (uint32_t x = 0; x < c.width; x++) {
      for (uint32_t ch = 0; ch < c.layout; ch++) {
        unsigned char want[4] = {0, 0, 0, 0};
        put_sample(want, c.bits, sample_value(c.bits, x, y, ch));
        ASSERT_EQ(std::memcmp(row + ((size_t)x * c.layout + ch) * sample_size(c.bits), want, sample_size(c.bits)), 0)
            << "x=" << x << " y=" << y << " c=" << ch;
      }
    }
  }
}

/* --- IM7, IM1, IM6, IM11, IM12: the generator's answers --------------------------------------------------------- */

TEST(Image, EveryCaseOfTheGeneratorHashesAndDumpsAsItDoes) {
  StoreHolder store;
  for (const known::ImageCase & c : known::kImageCases) {
    SCOPED_TRACE(c.name);
    Fmt f = fmt_of(c);
    RasterHolder raster(make_raster(c.width, c.height, f));
    ASSERT_NE(raster.get(), nullptr);
    Ref ref(copy_in(store.store, raster.get(), params(space_of_case(c), c.alpha_only ? GSHSCENE_IMAGE_ALPHA_ONLY : 0)));
    ASSERT_NE(ref.get(), nullptr);
    EXPECT_EQ(gshscene_resource_kind(ref.get()), GSHSCENE_RESOURCE_IMAGE);
    EXPECT_EQ(gshscene_resource_hash(ref.get()), c.hash);
    EXPECT_EQ(dump_resource(ref.get()), c.dump);

    GSHSCENE_Image_Info info;
    ASSERT_EQ(gshscene_resource_image(ref.get(), &info), GSHSCENE_OK);
    EXPECT_EQ(info.width, c.width);
    EXPECT_EQ(info.height, c.height);
    EXPECT_EQ(info.layout, c.layout);
    EXPECT_EQ(info.sample_type, c.type);
    EXPECT_EQ(info.bits, c.bits);
    EXPECT_EQ(info.alpha_form, c.alpha);
    EXPECT_EQ(info.alpha_only, c.alpha_only);
    EXPECT_EQ(info.tile, GSHSCENE_IMAGE_TILE);
    EXPECT_EQ(info.tiles_x, c.tiles_x);
    EXPECT_EQ(info.tiles_y, c.tiles_y);
    for (unsigned ty = 0; ty < c.tiles_y; ty++) {
      for (unsigned tx = 0; tx < c.tiles_x; tx++) {
        EXPECT_EQ(gshscene_image_tile_hash(ref.get(), tx, ty), c.tiles[ty * c.tiles_x + tx]);
      }
    }
    expect_rows(ref.get(), c);
    EXPECT_EQ(gshscene_image_stride(ref.get()), (size_t)c.width * c.layout * sample_size(c.bits)) << "a copy is tightly packed";
  }
}

TEST(Image, ATakenRasterHashesAndReadsTheSameAsACopy) {
  StoreHolder store;
  for (const known::ImageCase & c : known::kImageCases) {
    SCOPED_TRACE(c.name);
    Fmt f = fmt_of(c);
    GIMG_Raster * raster = make_raster(c.width, c.height, f, 13);
    ASSERT_NE(raster, nullptr);
    const GSHSCENE_Resource * r = nullptr;
    GSHSCENE_Image_Params p = params(space_of_case(c), c.alpha_only ? GSHSCENE_IMAGE_ALPHA_ONLY : 0);
    ASSERT_EQ(gshscene_store_intern_image_take(store.store, raster, &p, &r), GSHSCENE_OK);
    Ref ref(r);
    EXPECT_EQ(gshscene_resource_hash(r), c.hash);
    EXPECT_EQ(dump_resource(r), c.dump);
    EXPECT_EQ(gshscene_image_stride(r), (size_t)c.width * c.layout * sample_size(c.bits) + 13) << "a take reads the raster's own rows";
    expect_rows(r, c);
  }
}

/* --- IM6: tile geometry ------------------------------------------------------------------------------------------ */

TEST(Image, TheTileGridIsTheCeilingOfTheSizeOverSixtyFour) {
  struct Size {
    uint32_t w, h, tx, ty;
  };
  const Size sizes[] = {{1, 1, 1, 1}, {63, 63, 1, 1}, {64, 64, 1, 1}, {65, 65, 2, 2}, {64, 65, 1, 2}, {129, 1, 3, 1},
      {1, 129, 1, 3}, {65536, 1, 1024, 1}, {1, 65536, 1, 1024}};
  StoreHolder store;
  for (const Size & s : sizes) {
    SCOPED_TRACE(std::to_string(s.w) + "x" + std::to_string(s.h));
    RasterHolder raster(make_raster(s.w, s.h, kGray8));
    ASSERT_NE(raster.get(), nullptr);
    Ref ref(copy_in(store.store, raster.get(), params()));
    ASSERT_NE(ref.get(), nullptr);
    GSHSCENE_Image_Info info;
    ASSERT_EQ(gshscene_resource_image(ref.get(), &info), GSHSCENE_OK);
    EXPECT_EQ(info.tiles_x, s.tx);
    EXPECT_EQ(info.tiles_y, s.ty);
    EXPECT_NE(gshscene_image_tile_hash(ref.get(), s.tx - 1, s.ty - 1), 0u) << "the last, partial tile is hashed";
    EXPECT_EQ(gshscene_image_tile_hash(ref.get(), s.tx, 0), 0u);
    EXPECT_EQ(gshscene_image_tile_hash(ref.get(), 0, s.ty), 0u);
  }
}

/* --- IM2: accepted formats and what each refusal is -------------------------------------------------------------- */

TEST(Image, EveryAcceptedFormatInternsAndReadsBack) {
  const Fmt formats[] = {kGray8, kRgb8, kRgba8, kRgba8P, kGray12, {GIMG_CHANNEL_RGB, GIMG_CHANNEL_UNORM, 3, 12, GIMG_ALPHA_NONE},
      {GIMG_CHANNEL_RGBA, GIMG_CHANNEL_UNORM, 4, 12, GIMG_ALPHA_STRAIGHT}, kGray16,
      {GIMG_CHANNEL_RGB, GIMG_CHANNEL_UNORM, 3, 16, GIMG_ALPHA_NONE},
      {GIMG_CHANNEL_RGBA, GIMG_CHANNEL_UNORM, 4, 16, GIMG_ALPHA_PREMULTIPLIED}, kGrayF, kRgbF, kRgbaF,
      {GIMG_CHANNEL_RGBA, GIMG_CHANNEL_FLOAT, 4, 32, GIMG_ALPHA_PREMULTIPLIED}};
  StoreHolder store;
  for (const Fmt & f : formats) {
    SCOPED_TRACE(std::to_string(f.channels) + " channels, " + std::to_string(f.bits) + " bits");
    RasterHolder raster(make_raster(7, 5, f));
    ASSERT_NE(raster.get(), nullptr);
    Ref ref(copy_in(store.store, raster.get(), params()));
    ASSERT_NE(ref.get(), nullptr);
    GSHSCENE_Image_Info info;
    ASSERT_EQ(gshscene_resource_image(ref.get(), &info), GSHSCENE_OK);
    EXPECT_EQ(info.layout, f.channels);
    EXPECT_EQ(info.bits, f.bits);
    EXPECT_EQ(info.alpha_form, f.model == GIMG_CHANNEL_RGBA ? (uint32_t)f.alpha : 0u);
  }
}

TEST(Image, AnUnsaidAlphaFormOnRgbaReadsAsStraight) {
  GIMG_Pixel_Format format = format_of(kRgba8);
  format.alpha_form = 0; /* a format from before the field existed */
  GIMG_Raster * raw = nullptr;
  ASSERT_EQ(gimg_raster_create(3, 3, &format, GIMG_RASTER_OWNED, nullptr, 0, &raw), GIMG_OK);
  RasterHolder raster(raw);
  StoreHolder store;
  Ref ref(copy_in(store.store, raster.get(), params()));
  ASSERT_NE(ref.get(), nullptr);
  GSHSCENE_Image_Info info;
  ASSERT_EQ(gshscene_resource_image(ref.get(), &info), GSHSCENE_OK);
  EXPECT_EQ(info.alpha_form, (uint32_t)GSHSCENE_IMAGE_ALPHA_STRAIGHT);
}

/** A raster of @p f over a tiny buffer, which every refusal of the descriptor must leave unread. */
GIMG_Raster * descriptor_raster(const GIMG_Pixel_Format & format, uint32_t w = 4, uint32_t h = 4) {
  GIMG_Raster * raster = nullptr;
  if (gimg_raster_create(w, h, &format, GIMG_RASTER_OWNED, nullptr, 0, &raster) != GIMG_OK) {
    return nullptr;
  }
  return raster;
}

/** Whether @p raster is refused with @p want, with nothing allocated and the store as it was. */
void expect_refused(const GIMG_Raster * raster, GSHSCENE_Result want, const GSHSCENE_Image_Params & p = params()) {
  tracker::Tracker t;
  GSHSCENE_Allocator alloc = tracker::allocator_for(&t);
  StoreHolder store(nullptr, &alloc);
  ASSERT_NE(store.store, nullptr);
  long before = t.calls;
  GSHSCENE_Store_Stats stats = stats_of(store.store);
  const GSHSCENE_Resource * r = nullptr;
  EXPECT_EQ(gshscene_store_intern_image(store.store, raster, &p, &r), want);
  EXPECT_EQ(r, nullptr);
  EXPECT_EQ(t.calls, before) << "a refusal allocates nothing";
  EXPECT_TRUE(same_stats(stats, stats_of(store.store)));
}

TEST(Image, AModelThisResourceDoesNotCarryIsUnsupported) {
  struct Case {
    const char * name;
    GIMG_Pixel_Format format;
  };
  GIMG_Pixel_Format unknown2, unknown3, unknown5;
  ASSERT_EQ(gimg_pixel_format_multichannel(2, 8, &unknown2), GIMG_OK);
  ASSERT_EQ(gimg_pixel_format_multichannel(3, 8, &unknown3), GIMG_OK);
  ASSERT_EQ(gimg_pixel_format_multichannel(5, 8, &unknown5), GIMG_OK);
  GIMG_Pixel_Format ycbcr = format_of({GIMG_CHANNEL_YCBCR, GIMG_CHANNEL_UNORM, 3, 8, GIMG_ALPHA_NONE});
  GIMG_Pixel_Format lab = format_of({GIMG_CHANNEL_LAB, GIMG_CHANNEL_UNORM, 3, 8, GIMG_ALPHA_NONE});
  GIMG_Pixel_Format indexed = format_of({GIMG_CHANNEL_INDEXED, GIMG_CHANNEL_UINT, 1, 8, GIMG_ALPHA_NONE});
  GIMG_Pixel_Format snorm = format_of({GIMG_CHANNEL_RGBA, GIMG_CHANNEL_SNORM, 4, 8, GIMG_ALPHA_STRAIGHT});
  GIMG_Pixel_Format uint = format_of({GIMG_CHANNEL_GRAY, GIMG_CHANNEL_UINT, 1, 8, GIMG_ALPHA_NONE});
  GIMG_Pixel_Format sint = format_of({GIMG_CHANNEL_GRAY, GIMG_CHANNEL_SINT, 1, 16, GIMG_ALPHA_NONE});
  GIMG_Pixel_Format planar = format_of(kRgb8);
  planar.layout = GIMG_LAYOUT_PLANAR;
  GIMG_Pixel_Format ten = format_of({GIMG_CHANNEL_GRAY, GIMG_CHANNEL_UNORM, 1, 10, GIMG_ALPHA_NONE});
  GIMG_Pixel_Format four = format_of({GIMG_CHANNEL_GRAY, GIMG_CHANNEL_UNORM, 1, 4, GIMG_ALPHA_NONE});
  const Case cases[] = {{"CMYK8", GIMG_PIXEL_CMYK8}, {"CMYK12", GIMG_PIXEL_CMYK12}, {"CMYK16", GIMG_PIXEL_CMYK16},
      {"YCBCR", ycbcr}, {"LAB", lab}, {"INDEXED", indexed}, {"UNKNOWN 2", unknown2}, {"UNKNOWN 3", unknown3},
      {"UNKNOWN 5", unknown5}, {"SNORM", snorm}, {"UINT", uint}, {"SINT", sint}, {"planar", planar}, {"UNORM 10", ten},
      {"UNORM 4", four}};
  for (const Case & c : cases) {
    SCOPED_TRACE(c.name);
    RasterHolder raster(descriptor_raster(c.format));
    ASSERT_NE(raster.get(), nullptr);
    expect_refused(raster.get(), GSHSCENE_ERR_UNSUPPORTED);
  }
}

TEST(Image, ADescriptorThatContradictsItselfIsInvalid) {
  GIMG_Pixel_Format three_gray = format_of(kGray8);
  three_gray.channel_count = 3;
  three_gray.bits_per_channel[1] = three_gray.bits_per_channel[2] = 8;
  GIMG_Pixel_Format two_rgb = format_of(kRgb8);
  two_rgb.channel_count = 2;
  GIMG_Pixel_Format uneven = format_of(kRgb8);
  uneven.bits_per_channel[1] = 16;
  GIMG_Pixel_Format float16 = format_of({GIMG_CHANNEL_GRAY, GIMG_CHANNEL_FLOAT, 1, 32, GIMG_ALPHA_NONE});
  const struct {
    const char * name;
    GIMG_Pixel_Format format;
  } cases[] = {{"a channel count that is not the gray model's", three_gray},
      {"a channel count that is not the RGB model's", two_rgb}, {"channels of unequal width", uneven}};
  for (const auto & c : cases) {
    SCOPED_TRACE(c.name);
    RasterHolder raster(descriptor_raster(c.format));
    ASSERT_NE(raster.get(), nullptr);
    expect_refused(raster.get(), GSHSCENE_ERR_INVALID);
  }
  (void)float16; /* gimg_raster_create refuses a float of another width, so no raster can carry one */
}

TEST(Image, NullArgumentsAndBadParametersAreInvalid) {
  StoreHolder store;
  RasterHolder raster(make_raster(4, 4, kRgb8));
  const GSHSCENE_Resource * r = nullptr;
  GSHSCENE_Image_Params p = params();
  EXPECT_EQ(gshscene_store_intern_image(nullptr, raster.get(), &p, &r), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_store_intern_image(store.store, nullptr, &p, &r), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_store_intern_image(store.store, raster.get(), &p, nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_store_intern_image_take(nullptr, raster.get(), &p, &r), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_store_intern_image_take(store.store, nullptr, &p, &r), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_store_intern_image_take(store.store, raster.get(), &p, nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(r, nullptr);

  GSHSCENE_Image_Params reserved = params();
  reserved.reserved = 1;
  expect_refused(raster.get(), GSHSCENE_ERR_INVALID, reserved);
  expect_refused(raster.get(), GSHSCENE_ERR_INVALID, params(GSHSCENE_Color_Space{}, 2));
  expect_refused(raster.get(), GSHSCENE_ERR_INVALID, params(GSHSCENE_Color_Space{}, 0x80000000u));
  /* A partly given space: a gamut and no transfer, a transfer and no gamut, a gamma where it does not belong. */
  expect_refused(raster.get(), GSHSCENE_ERR_INVALID, params(space_of(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_UNKNOWN)));
  expect_refused(raster.get(), GSHSCENE_ERR_INVALID, params(space_of(GCOL_PRIMARIES_UNKNOWN, GCOL_TRANSFER_SRGB)));
  expect_refused(raster.get(), GSHSCENE_ERR_INVALID, params(space_of(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_SRGB, 2.2)));
  expect_refused(raster.get(), GSHSCENE_ERR_INVALID, params(space_of(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_GAMMA, 11.0)));
  expect_refused(raster.get(), GSHSCENE_ERR_INVALID, params(space_of(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_GAMMA, kInf)));
}

TEST(Image, ANullParametersPointerMeansTheZeroSpaceAndNoFlags) {
  StoreHolder store;
  RasterHolder raster(make_raster(4, 4, kRgb8));
  const GSHSCENE_Resource * a = nullptr;
  ASSERT_EQ(gshscene_store_intern_image(store.store, raster.get(), nullptr, &a), GSHSCENE_OK);
  Ref ra(a);
  Ref rb(copy_in(store.store, raster.get(), params()));
  EXPECT_EQ(ra.get(), rb.get());
}

TEST(Image, InvalidBeforeUnsupportedBeforeLimit) {
  /* CMYK with a partly given space: the invalid thing is reported, not the unsupported one. */
  RasterHolder cmyk(descriptor_raster(GIMG_PIXEL_CMYK8));
  expect_refused(cmyk.get(), GSHSCENE_ERR_INVALID, params(space_of(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_UNKNOWN)));
  /* ProPhoto (unsupported) with a reserved field set (invalid): invalid. */
  RasterHolder rgb(make_raster(4, 4, kRgb8));
  GSHSCENE_Image_Params both = params(space_of(GCOL_PRIMARIES_PROPHOTO, GCOL_TRANSFER_SRGB));
  both.reserved = 5;
  expect_refused(rgb.get(), GSHSCENE_ERR_INVALID, both);
  /* An unsupported format over a size past the limit: unsupported, because the limit is decided after. */
  GIMG_Pixel_Format ten = format_of({GIMG_CHANNEL_GRAY, GIMG_CHANNEL_UNORM, 1, 10, GIMG_ALPHA_NONE});
  std::vector<unsigned char> small(64);
  GIMG_Raster * huge = nullptr;
  ASSERT_EQ(gimg_raster_create(70000, 1, &ten, GIMG_RASTER_BORROWED, small.data(), 0, &huge), GIMG_OK);
  RasterHolder holder(huge);
  expect_refused(huge, GSHSCENE_ERR_UNSUPPORTED);
}

/* --- IM3: sample values ------------------------------------------------------------------------------------------ */

TEST(Image, ATwelveBitSampleIsAtMost4095) {
  StoreHolder store;
  RasterHolder raster(make_raster(4, 4, kGray12));
  unsigned char * base = (unsigned char *)gimg_raster_pixels(raster.get());
  size_t stride = gimg_raster_stride_bytes(raster.get());
  uint16_t v = 4095;
  std::memcpy(sample_at(base, stride, kGray12, 2, 1, 0), &v, 2);
  EXPECT_EQ(copy_result(store.store, raster.get(), params()), GSHSCENE_OK);
  v = 4096;
  std::memcpy(sample_at(base, stride, kGray12, 2, 1, 0), &v, 2);
  expect_refused(raster.get(), GSHSCENE_ERR_INVALID);
  v = 0xFFFF;
  std::memcpy(sample_at(base, stride, kGray12, 3, 3, 0), &v, 2); /* the last sample of the last row too */
  v = 7;
  std::memcpy(sample_at(base, stride, kGray12, 2, 1, 0), &v, 2);
  expect_refused(raster.get(), GSHSCENE_ERR_INVALID);
}

TEST(Image, EveryBitPatternOfAnEightOrSixteenBitSampleIsAccepted) {
  StoreHolder store;
  RasterHolder g8(make_raster(16, 16, kGray8));
  unsigned char * p8 = (unsigned char *)gimg_raster_pixels(g8.get());
  for (size_t i = 0; i < 256; i++) {
    p8[i] = (unsigned char)i;
  }
  EXPECT_EQ(copy_result(store.store, g8.get(), params()), GSHSCENE_OK);
  RasterHolder g16(make_raster(16, 16, kGray16));
  unsigned char * p16 = (unsigned char *)gimg_raster_pixels(g16.get());
  for (size_t i = 0; i < 256; i++) {
    uint16_t v = (uint16_t)(i * 257u);
    std::memcpy(p16 + 2 * i, &v, 2);
  }
  EXPECT_EQ(copy_result(store.store, g16.get(), params()), GSHSCENE_OK);
}

/** Set sample (x, y, c) of a float raster to @p value. */
void set_float(GIMG_Raster * raster, const Fmt & f, uint32_t x, uint32_t y, uint32_t c, float value) {
  std::memcpy(sample_at((unsigned char *)gimg_raster_pixels(raster), gimg_raster_stride_bytes(raster), f, x, y, c), &value, 4);
}

TEST(Image, AFloatSampleIsFiniteAndItsAlphaIsInUnitRange) {
  StoreHolder store;
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();
  struct Bad {
    const char * name;
    uint32_t c;
    float v;
  };
  const Bad bad[] = {{"a NaN color", 0, nan}, {"an infinite color", 1, inf}, {"a negative infinite color", 2, -inf},
      {"a NaN alpha", 3, nan}, {"an alpha past one", 3, 1.0000001f}, {"a negative alpha", 3, -0.1f},
      {"an alpha of 1.5", 3, 1.5f}};
  for (const Bad & b : bad) {
    SCOPED_TRACE(b.name);
    RasterHolder raster(make_raster(5, 5, kRgbaF));
    set_float(raster.get(), kRgbaF, 4, 4, b.c, b.v); /* the last sample, so the whole raster is read */
    expect_refused(raster.get(), GSHSCENE_ERR_INVALID);
  }
  /* The edges that are legal. */
  RasterHolder good(make_raster(5, 5, kRgbaF));
  set_float(good.get(), kRgbaF, 0, 0, 3, 1.0f);
  set_float(good.get(), kRgbaF, 1, 0, 3, 0.0f);
  set_float(good.get(), kRgbaF, 2, 0, 3, -0.0f);
  set_float(good.get(), kRgbaF, 3, 0, 0, 1.0e30f); /* a color has no magnitude bound (V5) */
  set_float(good.get(), kRgbaF, 3, 0, 1, -1.0e30f);
  EXPECT_EQ(copy_result(store.store, good.get(), params()), GSHSCENE_OK);
  /* An RGB float image has no alpha: a value past one is a color. */
  RasterHolder rgb(make_raster(5, 5, kRgbF));
  set_float(rgb.get(), kRgbF, 4, 4, 2, 7.5f);
  EXPECT_EQ(copy_result(store.store, rgb.get(), params()), GSHSCENE_OK);
}

TEST(Image, AFloatColorOfTwoToTheHundredIsKeptNotRefused) {
  StoreHolder store;
  RasterHolder raster(make_raster(3, 3, kRgbaF));
  set_float(raster.get(), kRgbaF, 1, 1, 0, std::ldexp(1.0f, 100));
  Ref ref(copy_in(store.store, raster.get(), params()));
  ASSERT_NE(ref.get(), nullptr);
  const float * row = (const float *)gshscene_image_row(ref.get(), 1);
  EXPECT_EQ(row[4], std::ldexp(1.0f, 100)) << "nothing is clamped at intern";
}

TEST(Image, APremultipliedColorAboveItsAlphaIsLegalData) {
  StoreHolder store;
  RasterHolder raster(make_raster(4, 4, kRgba8P));
  unsigned char * base = (unsigned char *)gimg_raster_pixels(raster.get());
  size_t stride = gimg_raster_stride_bytes(raster.get());
  unsigned char * px = sample_at(base, stride, kRgba8P, 1, 1, 0);
  px[0] = 255;
  px[1] = 255;
  px[2] = 255;
  px[3] = 3; /* an additive pixel */
  px = sample_at(base, stride, kRgba8P, 2, 2, 0);
  px[0] = 9;
  px[1] = 8;
  px[2] = 7;
  px[3] = 0; /* alpha zero with a color */
  Ref ref(copy_in(store.store, raster.get(), params()));
  ASSERT_NE(ref.get(), nullptr);
  const unsigned char * row = (const unsigned char *)gshscene_image_row(ref.get(), 1);
  EXPECT_EQ(row[4], 255);
  EXPECT_EQ(row[7], 3);
}

TEST(Image, NegativeZeroIsKeptAndIsADifferentImage) {
  StoreHolder store;
  RasterHolder positive(make_raster(3, 3, kGrayF));
  RasterHolder negative(make_raster(3, 3, kGrayF));
  set_float(positive.get(), kGrayF, 1, 1, 0, 0.0f);
  set_float(negative.get(), kGrayF, 1, 1, 0, -0.0f);
  Ref a(copy_in(store.store, positive.get(), params()));
  Ref b(copy_in(store.store, negative.get(), params()));
  ASSERT_NE(a.get(), nullptr);
  ASSERT_NE(b.get(), nullptr);
  EXPECT_NE(a.get(), b.get());
  EXPECT_NE(gshscene_resource_hash(a.get()), gshscene_resource_hash(b.get()));
  uint32_t bits;
  std::memcpy(&bits, (const unsigned char *)gshscene_image_row(b.get(), 1) + 4, 4);
  EXPECT_EQ(bits, 0x80000000u);
}

/* --- IM4: alpha-only ---------------------------------------------------------------------------------------------- */

TEST(Image, AnAlphaOnlyImageIsGrayWithTheMarkAndTheZeroSpace) {
  StoreHolder store;
  RasterHolder gray(make_raster(6, 6, kGray8));
  Ref marked(copy_in(store.store, gray.get(), params(GSHSCENE_Color_Space{}, GSHSCENE_IMAGE_ALPHA_ONLY)));
  Ref plain(copy_in(store.store, gray.get(), params()));
  ASSERT_NE(marked.get(), nullptr);
  ASSERT_NE(plain.get(), nullptr);
  EXPECT_NE(marked.get(), plain.get()) << "the mark is content";
  GSHSCENE_Image_Info info;
  ASSERT_EQ(gshscene_resource_image(marked.get(), &info), GSHSCENE_OK);
  EXPECT_EQ(info.alpha_only, 1u);
  EXPECT_EQ(info.alpha_form, (uint32_t)GSHSCENE_IMAGE_ALPHA_NONE);
  EXPECT_TRUE(gshscene_color_space_is_target(&info.space));

  expect_refused(gray.get(), GSHSCENE_ERR_INVALID, params(srgb_space(), GSHSCENE_IMAGE_ALPHA_ONLY));
  RasterHolder rgba(make_raster(6, 6, kRgba8));
  expect_refused(rgba.get(), GSHSCENE_ERR_INVALID, params(GSHSCENE_Color_Space{}, GSHSCENE_IMAGE_ALPHA_ONLY));
  RasterHolder rgb(make_raster(6, 6, kRgb8));
  expect_refused(rgb.get(), GSHSCENE_ERR_INVALID, params(GSHSCENE_Color_Space{}, GSHSCENE_IMAGE_ALPHA_ONLY));
}

TEST(Image, AnAlphaOnlyFloatImageHoldsItsChannelToTheUnitRange) {
  StoreHolder store;
  RasterHolder raster(make_raster(4, 4, kGrayF));
  EXPECT_EQ(copy_result(store.store, raster.get(), params(GSHSCENE_Color_Space{}, GSHSCENE_IMAGE_ALPHA_ONLY)), GSHSCENE_OK);
  set_float(raster.get(), kGrayF, 3, 3, 0, 1.5f);
  expect_refused(raster.get(), GSHSCENE_ERR_INVALID, params(GSHSCENE_Color_Space{}, GSHSCENE_IMAGE_ALPHA_ONLY));
  EXPECT_EQ(copy_result(store.store, raster.get(), params()), GSHSCENE_OK) << "a gray color above one is legal";
}

/* --- IM5: the color description ----------------------------------------------------------------------------------- */

TEST(Image, TheNamedSpacesAndTheZeroSpaceAreAcceptedAndRead) {
  StoreHolder store;
  RasterHolder raster(make_raster(4, 4, kRgb8));
  const GCOL_Primaries primaries[] = {GCOL_PRIMARIES_SRGB, GCOL_PRIMARIES_ADOBE_RGB, GCOL_PRIMARIES_DISPLAY_P3, GCOL_PRIMARIES_BT2020};
  std::set<const GSHSCENE_Resource *> distinct;
  std::vector<Ref> held;
  for (GCOL_Primaries pr : primaries) {
    for (int t = 0; t < 3; t++) {
      GSHSCENE_Color_Space space = t == 0 ? space_of(pr, GCOL_TRANSFER_LINEAR)
          : t == 1 ? space_of(pr, GCOL_TRANSFER_SRGB) : space_of(pr, GCOL_TRANSFER_GAMMA, 2.2);
      held.emplace_back(copy_in(store.store, raster.get(), params(space)));
      ASSERT_NE(held.back().get(), nullptr);
      GSHSCENE_Image_Info info;
      ASSERT_EQ(gshscene_resource_image(held.back().get(), &info), GSHSCENE_OK);
      EXPECT_EQ(info.space.primaries, space.primaries);
      EXPECT_EQ(info.space.transfer, space.transfer);
      EXPECT_EQ(info.space.gamma, space.gamma);
      distinct.insert(held.back().get());
    }
  }
  held.emplace_back(copy_in(store.store, raster.get(), params()));
  distinct.insert(held.back().get());
  EXPECT_EQ(distinct.size(), 13u) << "every space is its own image";
}

TEST(Image, ASpaceThisLibraryDoesNotCarryIsUnsupported) {
  RasterHolder raster(make_raster(4, 4, kRgb8));
  expect_refused(raster.get(), GSHSCENE_ERR_UNSUPPORTED, params(space_of(GCOL_PRIMARIES_PROPHOTO, GCOL_TRANSFER_SRGB)));
  expect_refused(raster.get(), GSHSCENE_ERR_UNSUPPORTED, params(space_of(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_PQ)));
  expect_refused(raster.get(), GSHSCENE_ERR_UNSUPPORTED, params(space_of(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_HLG)));
  expect_refused(raster.get(), GSHSCENE_ERR_UNSUPPORTED, params(space_of(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_BT1886)));
  expect_refused(raster.get(), GSHSCENE_ERR_UNSUPPORTED, params(space_of(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_PARAMETRIC)));
}

TEST(Image, TheRasterHelperNamesWhatTheRasterStatesAndGuessesNothing) {
  RasterHolder raster(make_raster(4, 4, kRgb8));
  GSHSCENE_Color_Space space = {};
  EXPECT_EQ(gshscene_image_space_from_raster(raster.get(), &space), GSHSCENE_ERR_INVALID) << "nothing stated: nothing guessed";
  EXPECT_EQ(gshscene_image_space_from_raster(nullptr, &space), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_image_space_from_raster(raster.get(), nullptr), GSHSCENE_ERR_INVALID);

  GCOL_Color_Info info;
  gcol_color_info_default(&info);
  ASSERT_TRUE(gcol_color_info_set_gamut(&info, GCOL_PRIMARIES_DISPLAY_P3));
  info.transfer = GCOL_TRANSFER_SRGB;
  ASSERT_EQ(gimg_raster_set_color_info(raster.get(), &info), GIMG_OK);
  ASSERT_EQ(gshscene_image_space_from_raster(raster.get(), &space), GSHSCENE_OK);
  EXPECT_EQ(space.primaries, GCOL_PRIMARIES_DISPLAY_P3);
  EXPECT_EQ(space.transfer, GCOL_TRANSFER_SRGB);

  /* An ICC profile, even when the decoder also named a gamut, is unsupported: the front end passes what it identified. */
  static const unsigned char profile[4] = {1, 2, 3, 4};
  info.icc_bytes = profile;
  info.icc_size = sizeof profile;
  ASSERT_EQ(gimg_raster_set_color_info(raster.get(), &info), GIMG_OK);
  EXPECT_EQ(gshscene_image_space_from_raster(raster.get(), &space), GSHSCENE_ERR_UNSUPPORTED);

  /* The import never reads the raster's description: this raster interns as the zero space. */
  StoreHolder store;
  Ref ref(copy_in(store.store, raster.get(), params()));
  ASSERT_NE(ref.get(), nullptr);
  GSHSCENE_Image_Info got;
  ASSERT_EQ(gshscene_resource_image(ref.get(), &got), GSHSCENE_OK);
  EXPECT_TRUE(gshscene_color_space_is_target(&got.space));
}

/* --- IM8: equality and interning --------------------------------------------------------------------------------- */

void set_float(GIMG_Raster * raster, const Fmt & f, uint32_t x, uint32_t y, uint32_t c, float value);

/** IM8 under @p index: equal content is one handle and every nearly equal image is its own. */
void equal_and_nearly_equal(GSHSCENE_Index_Hash_Fn index) {
  StoreHolder store(nullptr, nullptr, index);
  RasterHolder base(make_raster(70, 70, kRgba8));
  Ref a(copy_in(store.store, base.get(), params()));
  RasterHolder again(make_raster(70, 70, kRgba8, 11, 0x3C)); /* padded, other junk */
  Ref b(copy_in(store.store, again.get(), params()));
  ASSERT_NE(a.get(), nullptr);
  EXPECT_EQ(a.get(), b.get());
  EXPECT_EQ(stats_of(store.store).live, 1u);

  std::vector<Ref> others;
  /* One sample one bit apart, in the last tile and the first. */
  for (int which = 0; which < 2; which++) {
    RasterHolder raster(make_raster(70, 70, kRgba8));
    unsigned char * base_px = (unsigned char *)gimg_raster_pixels(raster.get());
    size_t stride = gimg_raster_stride_bytes(raster.get());
    unsigned char * px = which == 0 ? sample_at(base_px, stride, kRgba8, 69, 69, 3) : sample_at(base_px, stride, kRgba8, 0, 0, 0);
    *px ^= 1;
    others.emplace_back(copy_in(store.store, raster.get(), params()));
  }
  /* The same bytes read as premultiplied. */
  RasterHolder premult(make_raster(70, 70, kRgba8P));
  others.emplace_back(copy_in(store.store, premult.get(), params()));
  /* Another space. */
  others.emplace_back(copy_in(store.store, base.get(), params(srgb_space())));
  /* Another size with the same prefix of samples. */
  RasterHolder wider(make_raster(71, 70, kRgba8));
  others.emplace_back(copy_in(store.store, wider.get(), params()));
  RasterHolder taller(make_raster(70, 71, kRgba8));
  others.emplace_back(copy_in(store.store, taller.get(), params()));
  std::set<const GSHSCENE_Resource *> all = {a.get()};
  for (const Ref & r : others) {
    ASSERT_NE(r.get(), nullptr);
    all.insert(r.get());
  }
  EXPECT_EQ(all.size(), 1u + others.size()) << "every nearly equal image is its own";
  EXPECT_EQ(stats_of(store.store).live, 1u + others.size());
  /* The alpha-only mark and a float image's `-0` are told apart too. */
  RasterHolder gray(make_raster(70, 70, kGray8));
  Ref plain(copy_in(store.store, gray.get(), params()));
  Ref marked(copy_in(store.store, gray.get(), params(GSHSCENE_Color_Space{}, GSHSCENE_IMAGE_ALPHA_ONLY)));
  EXPECT_NE(plain.get(), marked.get());
  RasterHolder pz(make_raster(70, 70, kGrayF));
  RasterHolder nz(make_raster(70, 70, kGrayF));
  set_float(pz.get(), kGrayF, 69, 69, 0, 0.0f);
  set_float(nz.get(), kGrayF, 69, 69, 0, -0.0f);
  Ref zero(copy_in(store.store, pz.get(), params()));
  Ref minus(copy_in(store.store, nz.get(), params()));
  EXPECT_NE(zero.get(), minus.get());
  /* Different depths of the same shape: a 12-bit and a 16-bit gray image of the same texels' low bytes. */
  RasterHolder g12(make_raster(70, 70, kGray12));
  RasterHolder g16(make_raster(70, 70, kGray16));
  Ref r12(copy_in(store.store, g12.get(), params()));
  Ref r16(copy_in(store.store, g16.get(), params()));
  EXPECT_NE(r12.get(), r16.get());
  /* Zero-filled images that differ in one field only: the samples cannot tell them apart, the descriptor must. */
  std::vector<Ref> zeros;
  auto zero_image = [&](uint32_t w, uint32_t h, const Fmt & f, const GSHSCENE_Image_Params & p) {
    GIMG_Pixel_Format format = format_of(f);
    GIMG_Raster * raw = nullptr;
    EXPECT_EQ(gimg_raster_create(w, h, &format, GIMG_RASTER_OWNED, nullptr, 0, &raw), GIMG_OK);
    RasterHolder holder(raw);
    zeros.emplace_back(copy_in(store.store, holder.get(), p));
    EXPECT_NE(zeros.back().get(), nullptr);
  };
  /* Each pair is interned larger row first, so the comparison of the later one reads only bytes both have. */
  zero_image(70, 70, kRgb8, params());                                            /* three samples a texel... */
  zero_image(71, 70, kGray8, params());                                           /* ...against one, and wider... */
  zero_image(70, 70, kGray8, params());
  zero_image(70, 71, kGray8, params());                                           /* ...and taller */
  zero_image(70, 70, kGray12, params());                                          /* 12 bits... */
  zero_image(70, 70, kGray16, params());                                          /* ...against 16 */
  zero_image(70, 70, kRgba8, params());                                           /* straight */
  zero_image(70, 70, kRgba8P, params());                                          /* premultiplied */
  zero_image(70, 70, kGray8, params(GSHSCENE_Color_Space{}, GSHSCENE_IMAGE_ALPHA_ONLY));
  zero_image(70, 70, kGray8, params(srgb_space()));
  zero_image(70, 70, kGray8, params(linear_srgb_space()));
  std::set<const GSHSCENE_Resource *> distinct_zero;
  for (const Ref & z : zeros) {
    distinct_zero.insert(z.get());
  }
  EXPECT_EQ(distinct_zero.size(), zeros.size()) << "every descriptor field tells zero-filled images apart";
  /* And each is found again by its own content. */
  zero_image(70, 70, kGray12, params());
  EXPECT_EQ(zeros.back().get(), zeros[4].get());
  zero_image(71, 70, kGray8, params());
  EXPECT_EQ(zeros.back().get(), zeros[1].get());
  /* Interning any of them again finds it. */
  Ref again_marked(copy_in(store.store, gray.get(), params(GSHSCENE_Color_Space{}, GSHSCENE_IMAGE_ALPHA_ONLY)));
  EXPECT_EQ(again_marked.get(), marked.get());
  Ref again_minus(copy_in(store.store, nz.get(), params()));
  EXPECT_EQ(again_minus.get(), minus.get());
}

TEST(Image, EqualContentIsOneHandleAndAnythingElseIsItsOwn) {
  equal_and_nearly_equal(nullptr);
}

TEST(Image, EqualContentIsOneHandleAndAnythingElseIsItsOwnWhenEveryIndexCollides) {
  equal_and_nearly_equal(all_zero);
}

TEST(Image, ACollisionOfTheIndexNeverMergesWhatDiffers) {
  StoreHolder store(nullptr, nullptr, all_zero);
  std::vector<Ref> held;
  std::vector<RasterHolder> rasters;
  for (int i = 0; i < 6; i++) {
    RasterHolder raster(make_raster(66, 66, kGray8));
    unsigned char * px = (unsigned char *)gimg_raster_pixels(raster.get());
    px[(size_t)gimg_raster_stride_bytes(raster.get()) * 65 + 65] ^= (unsigned char)(i + 1); /* only the last texel differs */
    held.emplace_back(copy_in(store.store, raster.get(), params()));
    ASSERT_NE(held.back().get(), nullptr);
  }
  std::set<const GSHSCENE_Resource *> all;
  for (const Ref & r : held) {
    all.insert(r.get());
  }
  EXPECT_EQ(all.size(), 6u);
  RasterHolder same(make_raster(66, 66, kGray8));
  unsigned char * px = (unsigned char *)gimg_raster_pixels(same.get());
  px[(size_t)gimg_raster_stride_bytes(same.get()) * 65 + 65] ^= 3;
  Ref again(copy_in(store.store, same.get(), params()));
  EXPECT_EQ(again.get(), held[2].get()) << "an equal image is found among the collisions";
  EXPECT_GT(stats_of(store.store).collisions, 0u);
}

/* --- IM9: copy and take ------------------------------------------------------------------------------------------- */

TEST(Image, ACopyNeverKeepsAPointerIntoTheRasterNorItsPadding) {
  StoreHolder store;
  GIMG_Raster * raster = make_raster(9, 9, kRgb8, 7, 0x11);
  ASSERT_NE(raster, nullptr);
  Ref ref(copy_in(store.store, raster, params()));
  ASSERT_NE(ref.get(), nullptr);
  const void * before = gshscene_image_row(ref.get(), 0);
  EXPECT_NE(before, gimg_raster_pixels_const(raster));
  gimg_raster_destroy(raster); /* the image outlives its source */
  unsigned char want[3];
  for (uint32_t c = 0; c < 3; c++) {
    want[c] = (unsigned char)sample_value(8, 8, 8, c);
  }
  EXPECT_EQ(std::memcmp((const unsigned char *)gshscene_image_row(ref.get(), 8) + 24, want, 3), 0);
}

TEST(Image, ABorrowedRasterAndAnOwnedOneOfTheSameContentAreOneImage) {
  StoreHolder store;
  GIMG_Pixel_Format format = format_of(kRgba8);
  std::vector<unsigned char> buffer(20 * 20 * 4);
  fill_samples(buffer.data(), 20 * 4, 20, 20, kRgba8);
  GIMG_Raster * borrowed = nullptr;
  ASSERT_EQ(gimg_raster_create(20, 20, &format, GIMG_RASTER_BORROWED, buffer.data(), 20 * 4, &borrowed), GIMG_OK);
  RasterHolder b(borrowed);
  RasterHolder o(make_raster(20, 20, kRgba8, 5));
  Ref x(copy_in(store.store, b.get(), params()));
  Ref y(copy_in(store.store, o.get(), params()));
  ASSERT_NE(x.get(), nullptr);
  EXPECT_EQ(x.get(), y.get());
  /* An owned raster that was given its own buffer. */
  unsigned char * given = (unsigned char *)std::malloc(20 * 20 * 4);
  fill_samples(given, 20 * 4, 20, 20, kRgba8);
  GIMG_Raster * adopted = nullptr;
  ASSERT_EQ(gimg_raster_create(20, 20, &format, GIMG_RASTER_OWNED, given, 20 * 4, &adopted), GIMG_OK);
  RasterHolder a(adopted);
  Ref z(copy_in(store.store, a.get(), params()));
  EXPECT_EQ(x.get(), z.get());
}

TEST(Image, ATakeKeepsTheRasterUntilTheImagesLastRelease) {
  StoreHolder store;
  RasterAlloc ralloc;
  GIMG_Raster * raster = make_raster(30, 30, kRgb8, 0, 0xA5, &ralloc.a);
  ASSERT_NE(raster, nullptr);
  size_t held = ralloc.live();
  EXPECT_GT(held, 0u);
  const GSHSCENE_Resource * r = nullptr;
  GSHSCENE_Image_Params p = params();
  ASSERT_EQ(gshscene_store_intern_image_take(store.store, raster, &p, &r), GSHSCENE_OK);
  EXPECT_EQ(ralloc.live(), held) << "the store keeps the raster: it reads its pixels in place";
  EXPECT_EQ(gshscene_image_row(r, 0), gimg_raster_pixels_const(raster));
  gshscene_resource_release(r);
  EXPECT_EQ(ralloc.live(), 0u) << "the last release destroys it";
}

TEST(Image, ATakeOfAnEqualLiveImageReturnsTheLiveOneAndDestroysTheRaster) {
  StoreHolder store;
  RasterAlloc ralloc;
  RasterHolder first(make_raster(30, 30, kRgb8));
  Ref live(copy_in(store.store, first.get(), params()));
  GIMG_Raster * second = make_raster(30, 30, kRgb8, 3, 0x42, &ralloc.a);
  ASSERT_NE(second, nullptr);
  const GSHSCENE_Resource * r = nullptr;
  GSHSCENE_Image_Params p = params();
  ASSERT_EQ(gshscene_store_intern_image_take(store.store, second, &p, &r), GSHSCENE_OK);
  Ref hit(r);
  EXPECT_EQ(hit.get(), live.get());
  EXPECT_EQ(ralloc.live(), 0u) << "the raster was of no use and the store destroyed it";
}

TEST(Image, ABorrowedRasterIsRefusedByTheTakeAndEveryRefusalLeavesTheRasterTheCallers) {
  StoreHolder store;
  RasterAlloc ralloc;
  /* Borrowed. */
  std::vector<unsigned char> buffer(8 * 8 * 3);
  fill_samples(buffer.data(), 8 * 3, 8, 8, kRgb8);
  GIMG_Pixel_Format format = format_of(kRgb8);
  GIMG_Raster * borrowed = nullptr;
  ASSERT_EQ(gimg_raster_create(8, 8, &format, GIMG_RASTER_BORROWED, buffer.data(), 0, &borrowed), GIMG_OK);
  RasterHolder b(borrowed);
  const GSHSCENE_Resource * r = nullptr;
  GSHSCENE_Image_Params p = params();
  EXPECT_EQ(gshscene_store_intern_image_take(store.store, borrowed, &p, &r), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(r, nullptr);
  EXPECT_EQ(gimg_raster_ownership(borrowed), GIMG_RASTER_BORROWED);
  /* An owned raster refused for each reason: still the caller's, still whole. */
  struct Refusal {
    GSHSCENE_Result want;
    GSHSCENE_Image_Params params;
    Fmt fmt;
    bool bad_sample;
  };
  GSHSCENE_Image_Params reserved = params();
  reserved.reserved = 9;
  const Refusal refusals[] = {{GSHSCENE_ERR_INVALID, reserved, kGray8, false},
      {GSHSCENE_ERR_UNSUPPORTED, params(space_of(GCOL_PRIMARIES_PROPHOTO, GCOL_TRANSFER_SRGB)), kGray8, false},
      {GSHSCENE_ERR_INVALID, params(), kGray12, true}};
  for (const Refusal & f : refusals) {
    GIMG_Raster * raster = make_raster(8, 8, f.fmt, 0, 0xA5, &ralloc.a);
    ASSERT_NE(raster, nullptr);
    if (f.bad_sample) {
      uint16_t v = 5000;
      std::memcpy(gimg_raster_pixels(raster), &v, 2);
    }
    EXPECT_EQ(gshscene_store_intern_image_take(store.store, raster, &f.params, &r), f.want);
    EXPECT_GT(ralloc.live(), 0u) << "an error leaves the raster with its caller";
    EXPECT_EQ(gimg_raster_width(raster), 8u);
    gimg_raster_destroy(raster);
    EXPECT_EQ(ralloc.live(), 0u);
  }
}

/* --- IM10: limits ---------------------------------------------------------------------------------------------- */

TEST(Image, TheDimensionBoundPassesAtItsEdgeAndRefusesOneMore) {
  StoreHolder store;
  RasterHolder wide(make_raster(GSHSCENE_IMAGE_DIMENSION_MAX, 1, kGray8));
  EXPECT_EQ(copy_result(store.store, wide.get(), params()), GSHSCENE_OK);
  RasterHolder tall(make_raster(1, GSHSCENE_IMAGE_DIMENSION_MAX, kGray8));
  EXPECT_EQ(copy_result(store.store, tall.get(), params()), GSHSCENE_OK);
  std::vector<unsigned char> buffer(GSHSCENE_IMAGE_DIMENSION_MAX + 1);
  GIMG_Pixel_Format format = format_of(kGray8);
  GIMG_Raster * over = nullptr;
  ASSERT_EQ(gimg_raster_create(GSHSCENE_IMAGE_DIMENSION_MAX + 1, 1, &format, GIMG_RASTER_BORROWED, buffer.data(), 0, &over), GIMG_OK);
  RasterHolder h1(over);
  expect_refused(over, GSHSCENE_ERR_LIMIT);
  GIMG_Raster * over_tall = nullptr;
  ASSERT_EQ(gimg_raster_create(1, GSHSCENE_IMAGE_DIMENSION_MAX + 1, &format, GIMG_RASTER_BORROWED, buffer.data(), 1, &over_tall), GIMG_OK);
  RasterHolder h2(over_tall);
  expect_refused(over_tall, GSHSCENE_ERR_LIMIT);
}

TEST(Image, HostileSizesOverASmallBufferAreRefusedBeforeAnythingIsReadOrAllocated) {
  std::vector<unsigned char> buffer(32, 0xEE);
  GIMG_Pixel_Format format = format_of(kRgba8);
  struct Size {
    uint32_t w, h;
  };
  const Size sizes[] = {{0xFFFFFFFFu, 1}, {1, 0xFFFFFFFFu}, {0xFFFFFFFFu, 0xFFFFFFFFu}, {65537, 65537}, {16385, 16384},
      {16384, 16385}, {40000, 40000}, {65536, 65536}, {65536, 65535}, {65535, 65536}};
  for (const Size & s : sizes) {
    SCOPED_TRACE(std::to_string(s.w) + "x" + std::to_string(s.h));
    GIMG_Raster * raster = nullptr;
    ASSERT_EQ(gimg_raster_create(s.w, s.h, &format, GIMG_RASTER_BORROWED, buffer.data(), (size_t)s.w * 4, &raster), GIMG_OK);
    RasterHolder holder(raster);
    expect_refused(raster, GSHSCENE_ERR_LIMIT);
  }
}

TEST(Image, TheTexelBoundHoldsWhenTheStoreHasBytesToSpare) {
  /* With every byte allowed, the texel bound alone refuses: 2^16 by 2^16 texels wrap a 32-bit product to zero. */
  GSHSCENE_Limits limits;
  std::memset(&limits, 0, sizeof limits);
  limits.max_bytes = std::numeric_limits<size_t>::max();
  tracker::Tracker t;
  GSHSCENE_Allocator alloc = tracker::allocator_for(&t);
  StoreHolder store(&limits, &alloc);
  std::vector<unsigned char> buffer(32, 0xEE);
  GIMG_Pixel_Format format = format_of(kGray8);
  const uint32_t sizes[][2] = {{65536, 65536}, {65536, 4097}, {4097, 65536}, {16385, 16384}, {32768, 32768}};
  for (const auto & size : sizes) {
    SCOPED_TRACE(std::to_string(size[0]) + "x" + std::to_string(size[1]));
    GIMG_Raster * raster = nullptr;
    ASSERT_EQ(gimg_raster_create(size[0], size[1], &format, GIMG_RASTER_BORROWED, buffer.data(), size[0], &raster), GIMG_OK);
    RasterHolder holder(raster);
    long before = t.calls;
    const GSHSCENE_Resource * r = nullptr;
    GSHSCENE_Image_Params p = params();
    EXPECT_EQ(gshscene_store_intern_image(store.store, raster, &p, &r), GSHSCENE_ERR_LIMIT);
    EXPECT_EQ(t.calls, before);
  }
}

TEST(Image, ASampleCountPastThirtyTwoBitsIsRefusedBeforeAnythingIsRead) {
  /* 2^28 texels of four 32-bit samples are 2^32 bytes: a product that wraps a 32-bit size to zero. */
  std::vector<unsigned char> buffer(32, 0xEE);
  GIMG_Pixel_Format format = format_of(kRgbaF);
  const uint32_t sizes[][2] = {{16384, 16384}, {65536, 4096}, {4096, 65536}, {8192, 32768}};
  for (const auto & size : sizes) {
    SCOPED_TRACE(std::to_string(size[0]) + "x" + std::to_string(size[1]));
    GIMG_Raster * raster = nullptr;
    ASSERT_EQ(gimg_raster_create(size[0], size[1], &format, GIMG_RASTER_BORROWED, buffer.data(), (size_t)size[0] * 16u, &raster), GIMG_OK);
    RasterHolder holder(raster);
    expect_refused(raster, GSHSCENE_ERR_LIMIT);
  }
}

TEST(Image, TheTexelBoundPassesAtTwoToTheTwentyEighthAndRefusesOneMoreRow) {
  const char * preload = std::getenv("LD_PRELOAD");
  if (preload && std::strstr(preload, "valgrind")) {
    GTEST_SKIP() << "256 MiB of samples are hashed: too slow under Valgrind";
  }
  /* 2^16 by 2^12 is exactly the bound: a gray8 raster of 256 MiB, taken so that it is not copied. */
  GIMG_Pixel_Format format = format_of(kGray8);
  GIMG_Raster * raster = nullptr;
  ASSERT_EQ(gimg_raster_create(65536, 4096, &format, GIMG_RASTER_OWNED, nullptr, 0, &raster), GIMG_OK);
  GSHSCENE_Limits limits;
  std::memset(&limits, 0, sizeof limits);
  limits.max_bytes = (size_t)1 << 30;
  StoreHolder store(&limits);
  const GSHSCENE_Resource * r = nullptr;
  GSHSCENE_Image_Params p = params();
  ASSERT_EQ(gshscene_store_intern_image_take(store.store, raster, &p, &r), GSHSCENE_OK);
  gshscene_resource_release(r);
  /* One more row: 2^28 + 2^16 texels. */
  std::vector<unsigned char> small(16);
  GIMG_Raster * over = nullptr;
  ASSERT_EQ(gimg_raster_create(65536, 4097, &format, GIMG_RASTER_BORROWED, small.data(), 65536, &over), GIMG_OK);
  RasterHolder holder(over);
  expect_refused(over, GSHSCENE_ERR_LIMIT);
}

TEST(Image, TheByteBoundIsTheAccountedBytesAndPassesExactly) {
  RasterHolder raster(make_raster(100, 100, kRgba8));
  size_t exact = accounted(100, 100, kRgba8);
  EXPECT_EQ(exact, 128u + 40000u + 4u * 16u);
  GSHSCENE_Limits limits;
  std::memset(&limits, 0, sizeof limits);
  limits.max_bytes = exact;
  {
    StoreHolder store(&limits);
    EXPECT_EQ(copy_result(store.store, raster.get(), params()), GSHSCENE_OK);
  }
  limits.max_bytes = exact - 1;
  StoreHolder tight(&limits);
  const GSHSCENE_Resource * r = nullptr;
  GSHSCENE_Image_Params p = params();
  EXPECT_EQ(gshscene_store_intern_image(tight.store, raster.get(), &p, &r), GSHSCENE_ERR_LIMIT);
  EXPECT_EQ(stats_of(tight.store).live, 0u);
}

TEST(Image, TheAccountedBytesAreTheFormulaForEveryDepthAndLayout) {
  const Fmt formats[] = {kGray8, kRgb8, kRgba8, kGray12, kGray16, kRgbF, kRgbaF,
      {GIMG_CHANNEL_RGB, GIMG_CHANNEL_UNORM, 3, 16, GIMG_ALPHA_NONE}};
  StoreHolder store;
  for (const Fmt & f : formats) {
    SCOPED_TRACE(std::to_string(f.channels) + " channels, " + std::to_string(f.bits) + " bits");
    RasterHolder raster(make_raster(130, 70, f));
    Ref ref(copy_in(store.store, raster.get(), params()));
    ASSERT_NE(ref.get(), nullptr);
    EXPECT_EQ(gshscene_resource_bytes(ref.get()), accounted(130, 70, f));
    size_t samples = (size_t)130 * 70 * f.channels * sample_size(f.bits);
    EXPECT_EQ(gshscene_resource_bytes(ref.get()), GSHSCENE_BYTES_BASE + samples + 3u * 2u * GSHSCENE_BYTES_PER_TILE);
  }
}

TEST(Image, TheAccountedBytesRiseByTheFormulaAndAHitAddsNone) {
  StoreHolder store;
  RasterHolder raster(make_raster(130, 70, kRgb8));
  GSHSCENE_Store_Stats s0 = stats_of(store.store);
  Ref a(copy_in(store.store, raster.get(), params()));
  GSHSCENE_Store_Stats s1 = stats_of(store.store);
  EXPECT_EQ(s1.bytes - s0.bytes, accounted(130, 70, kRgb8));
  EXPECT_EQ(gshscene_resource_bytes(a.get()), accounted(130, 70, kRgb8));
  Ref b(copy_in(store.store, raster.get(), params()));
  GSHSCENE_Store_Stats s2 = stats_of(store.store);
  EXPECT_EQ(s2.bytes, s1.bytes);
  EXPECT_EQ(s2.hits, s1.hits + 1);
  a = Ref();
  b = Ref();
  EXPECT_EQ(stats_of(store.store).bytes, s0.bytes);
  EXPECT_EQ(stats_of(store.store).live, 0u);
}

/* --- IM13: lifetime, listeners, threads -------------------------------------------------------------------------- */

namespace {

struct Heard {
  std::vector<uint64_t> ids;
  RasterAlloc * ralloc = nullptr;
  size_t raster_live_at_call = 0;
  bool readable = false;
};

void on_release(void * ctx, uint64_t id, const GSHSCENE_Resource * resource) {
  Heard * h = (Heard *)ctx;
  h->ids.push_back(id);
  GSHSCENE_Image_Info info;
  h->readable = gshscene_resource_image(resource, &info) == GSHSCENE_OK && gshscene_image_row(resource, 0) != nullptr;
  if (h->ralloc) {
    h->raster_live_at_call = h->ralloc->live();
  }
}

}  // namespace

TEST(Image, TheReleaseListenerHearsOnceBeforeTheBytesAndATakenRasterGoesAfter) {
  StoreHolder store;
  Heard heard;
  RasterAlloc ralloc;
  heard.ralloc = &ralloc;
  uint64_t token = 0;
  ASSERT_EQ(gshscene_store_add_release_listener(store.store, on_release, &heard, &token), GSHSCENE_OK);
  GIMG_Raster * raster = make_raster(12, 12, kRgba8, 0, 0xA5, &ralloc.a);
  const GSHSCENE_Resource * r = nullptr;
  GSHSCENE_Image_Params p = params();
  ASSERT_EQ(gshscene_store_intern_image_take(store.store, raster, &p, &r), GSHSCENE_OK);
  uint64_t id = gshscene_resource_id(r);
  gshscene_resource_release(r);
  ASSERT_EQ(heard.ids.size(), 1u);
  EXPECT_EQ(heard.ids[0], id);
  EXPECT_TRUE(heard.readable) << "the image is readable during the call";
  EXPECT_GT(heard.raster_live_at_call, 0u) << "the raster is destroyed after the listeners have run";
  EXPECT_EQ(ralloc.live(), 0u);
}

TEST(Image, EightThreadsInterningEqualAndDistinctImagesGetOneHandlePerContent) {
  StoreHolder store;
  const int kThreads = 8;
  std::vector<std::vector<const GSHSCENE_Resource *>> equal_handles(kThreads);
  std::vector<std::thread> threads;
  RasterHolder shared(make_raster(80, 80, kRgba8));
  for (int t = 0; t < kThreads; t++) {
    threads.emplace_back([&, t]() {
      RasterHolder own(make_raster(80, 80, kRgba8, (size_t)t));
      RasterHolder distinct(make_raster(66 + (uint32_t)t, 66, kRgb8));
      for (int i = 0; i < 40; i++) {
        const GSHSCENE_Resource * e = nullptr;
        GSHSCENE_Image_Params p = params();
        if (gshscene_store_intern_image(store.store, own.get(), &p, &e) == GSHSCENE_OK) {
          equal_handles[(size_t)t].push_back(e);
        }
        const GSHSCENE_Resource * d = nullptr;
        if (gshscene_store_intern_image(store.store, distinct.get(), &p, &d) == GSHSCENE_OK) {
          gshscene_resource_release(d);
        }
      }
    });
  }
  for (std::thread & th : threads) {
    th.join();
  }
  std::set<const GSHSCENE_Resource *> unique;
  for (auto & v : equal_handles) {
    EXPECT_EQ(v.size(), 40u);
    for (const GSHSCENE_Resource * r : v) {
      unique.insert(r);
    }
  }
  EXPECT_EQ(unique.size(), 1u);
  GSHSCENE_Store_Stats s = stats_of(store.store);
  EXPECT_EQ(s.live, 1u) << "the distinct images were all released";
  for (auto & v : equal_handles) {
    for (const GSHSCENE_Resource * r : v) {
      gshscene_resource_release(r);
    }
  }
  s = stats_of(store.store);
  EXPECT_EQ(s.live, 0u);
  EXPECT_EQ(s.bytes, 0u);
}

/* --- allocation failure ------------------------------------------------------------------------------------------ */

TEST(Image, AnAllocationFailureAtEveryStepLeavesTheStoreAndTheRasterAsTheyWere) {
  for (int take = 0; take < 2; take++) {
    SCOPED_TRACE(take ? "take" : "copy");
    bool done = false;
    for (long fail = 0; fail < 64 && !done; fail++) {
      tracker::Tracker t;
      GSHSCENE_Allocator alloc = tracker::allocator_for(&t);
      StoreHolder store(nullptr, &alloc);
      ASSERT_NE(store.store, nullptr);
      size_t baseline = t.live.size();
      RasterAlloc ralloc;
      GIMG_Raster * raster = make_raster(70, 70, kRgba8, 0, 0xA5, &ralloc.a);
      ASSERT_NE(raster, nullptr);
      GSHSCENE_Store_Stats before = stats_of(store.store);
      t.calls = 0;
      t.fail_from = fail;
      const GSHSCENE_Resource * r = nullptr;
      GSHSCENE_Image_Params p = params();
      GSHSCENE_Result got = take ? gshscene_store_intern_image_take(store.store, raster, &p, &r)
                                 : gshscene_store_intern_image(store.store, raster, &p, &r);
      t.fail_from = -1;
      if (got == GSHSCENE_OK) {
        gshscene_resource_release(r);
        done = true;
        if (take) {
          EXPECT_EQ(ralloc.live(), 0u);
        } else {
          gimg_raster_destroy(raster);
        }
        EXPECT_EQ(t.live.size(), baseline) << "no leak on success";
        continue;
      }
      EXPECT_EQ(got, GSHSCENE_ERR_OOM) << "failure at call " << fail;
      EXPECT_EQ(r, nullptr);
      EXPECT_TRUE(same_stats(before, stats_of(store.store)));
      EXPECT_EQ(t.live.size(), baseline) << "no leak after a failure at call " << fail;
      EXPECT_GT(ralloc.live(), 0u) << "the raster is still the caller's";
      gimg_raster_destroy(raster);
      EXPECT_EQ(ralloc.live(), 0u);
    }
    EXPECT_TRUE(done) << "the call never succeeded within 64 allocations";
  }
}

/* --- IM12: the dump ---------------------------------------------------------------------------------------------- */

TEST(Image, TheDumpIsTheHeaderAndTileRowsAndNeverTheSamples) {
  StoreHolder store;
  RasterHolder a(make_raster(200, 70, kRgb8));
  RasterHolder other(make_raster(200, 70, kRgb8));
  *(unsigned char *)gimg_raster_pixels(other.get()) ^= 0xFF;
  Ref ra(copy_in(store.store, a.get(), params()));
  Ref rb(copy_in(store.store, other.get(), params()));
  std::string da = dump_resource(ra.get());
  std::string db = dump_resource(rb.get());
  EXPECT_NE(da, db);
  EXPECT_EQ(da.size(), db.size()) << "the dump's size is independent of the content";
  EXPECT_NE(da.find("kind=image"), std::string::npos);
  EXPECT_NE(da.find("image w=200 h=70 layout=rgb type=unorm bits=8 alpha=none alphaonly=0 space=0:0:0000000000000000 tile=64 tiles=4x2"),
      std::string::npos);
  EXPECT_NE(da.find("\nt 0 "), std::string::npos);
  EXPECT_NE(da.find("\nt 1 "), std::string::npos);
  EXPECT_EQ(da.find("\nt 2 "), std::string::npos);
  RasterHolder tiny(make_raster(1, 1, kGray8));
  Ref rt(copy_in(store.store, tiny.get(), params()));
  EXPECT_LT(dump_resource(rt.get()).size(), da.size());
  EXPECT_STREQ(gshscene_resource_kind_string(GSHSCENE_RESOURCE_IMAGE), "image");
}

TEST(Image, TheAccessorsRefuseWhatIsNotAnImage) {
  StoreHolder store;
  std::vector<Ref> others;
  others.emplace_back(intern_square(store.store));
  others.emplace_back(intern_face(store.store));
  const GSHSCENE_Color colors[2] = {{{1, 0, 0, 1}, {}}, {{0, 1, 0, 1}, {}}};
  const GSHSCENE_Resource * palette = nullptr;
  ASSERT_EQ(gshscene_store_intern_palette(store.store, 2, colors, &palette), GSHSCENE_OK);
  others.emplace_back(palette);
  const GSHSCENE_Resource * plane = nullptr;
  ASSERT_EQ(gshscene_store_intern_plane(store.store, &plane), GSHSCENE_OK);
  others.emplace_back(plane);
  GSHSCENE_Stroke_Style style;
  gshscene_stroke_style_init(&style);
  const GSHSCENE_Resource * stroke = nullptr;
  ASSERT_EQ(gshscene_store_intern_stroke_style(store.store, &style, &stroke), GSHSCENE_OK);
  others.emplace_back(stroke);
  {
    GSHSCENE_Gradient g;
    std::memset(&g, 0, sizeof g);
    g.kind = GSHSCENE_GRADIENT_LINEAR;
    g.p1[0] = 1.0;
    g.p0[1] = 2.0;
    gshgeom_map_identity(2, &g.map);
    GSHSCENE_Gradient_Stop stops[2];
    std::memset(stops, 0, sizeof stops);
    stops[0].color = solid(1, 0, 0, 1);
    stops[1].offset = 1.0;
    stops[1].color = solid(0, 0, 1, 1);
    g.stops = stops;
    g.stop_count = 2;
    const GSHSCENE_Resource * gradient = nullptr;
    ASSERT_EQ(gshscene_store_intern_gradient(store.store, &g, &gradient), GSHSCENE_OK);
    others.emplace_back(gradient);
    Ref face(intern_face(store.store));
    others.emplace_back(intern_font_run(store.store, face.get(), 12.5, {{36, 1.5, 2.5, nullptr}, {37, 7.5, 0.25, nullptr}}));
    ListHolder list(store.store);
    ASSERT_EQ(gshscene_list_close(list.list), GSHSCENE_OK);
    const GSHSCENE_Resource * picture = nullptr;
    ASSERT_EQ(gshscene_store_intern_picture(store.store, list.list, &picture), GSHSCENE_OK);
    others.emplace_back(picture);
  }
  GSHSCENE_Image_Info info;
  for (const Ref & other : others) {
    ASSERT_NE(other.get(), nullptr);
    EXPECT_EQ(gshscene_resource_image(other.get(), &info), GSHSCENE_ERR_INVALID);
    EXPECT_EQ(gshscene_image_row(other.get(), 0), nullptr);
    EXPECT_EQ(gshscene_image_stride(other.get()), 0u);
    EXPECT_EQ(gshscene_image_tile_hash(other.get(), 0, 0), 0u);
  }
  EXPECT_EQ(gshscene_resource_image(nullptr, &info), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_image_row(nullptr, 0), nullptr);
  EXPECT_EQ(gshscene_image_stride(nullptr), 0u);
  EXPECT_EQ(gshscene_image_tile_hash(nullptr, 0, 0), 0u);
  RasterHolder raster(make_raster(4, 4, kGray8));
  Ref img(copy_in(store.store, raster.get(), params()));
  EXPECT_EQ(gshscene_image_row(img.get(), 4), nullptr) << "a row past the height";
  EXPECT_EQ(gshscene_resource_image(img.get(), nullptr), GSHSCENE_ERR_INVALID);
}

/* --- IM14: not a shape ------------------------------------------------------------------------------------------- */

TEST(Image, AListRefusesAnImageWhereItTakesAPathARunOrAPicture) {
  StoreHolder store;
  RasterHolder raster(make_raster(8, 8, kRgba8));
  Ref img(copy_in(store.store, raster.get(), params()));
  ASSERT_NE(img.get(), nullptr);
  GSHSCENE_Stroke_Style style;
  gshscene_stroke_style_init(&style);
  const GSHSCENE_Resource * s = nullptr;
  ASSERT_EQ(gshscene_store_intern_stroke_style(store.store, &style, &s), GSHSCENE_OK);
  Ref stroke_style(s);
  ListHolder list(store.store);
  ASSERT_NE(list.list, nullptr);
  GSHSCENE_Paint paint = solid(0, 0, 0, 1);
  EXPECT_EQ(gshscene_list_fill(list.list, img.get(), nullptr, GSHGEOM_FILL_NONZERO, &paint, 0, 0.0), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_clip_push(list.list, img.get(), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_stroke(list.list, img.get(), stroke_style.get(), nullptr, &paint, 0, 0.0), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_instance(list.list, img.get(), nullptr, 0, 0.0), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_close(list.list), GSHSCENE_OK) << "nothing was recorded";
}

TEST(Image, AGlyphRunNamingAnImageAsItsOutlineOrFaceIsInvalid) {
  StoreHolder store;
  RasterHolder raster(make_raster(8, 8, kRgba8));
  Ref img(copy_in(store.store, raster.get(), params()));
  GSHSCENE_Glyph glyph = {1, 0.0, 0.0, img.get()};
  GSHSCENE_Glyph_Run run;
  std::memset(&run, 0, sizeof run);
  run.source = GSHSCENE_GLYPHS_FROM_OUTLINES;
  run.glyph_count = 1;
  run.glyphs = &glyph;
  const GSHSCENE_Resource * out = nullptr;
  EXPECT_EQ(gshscene_store_intern_glyph_run(store.store, &run, &out), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(out, nullptr);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
