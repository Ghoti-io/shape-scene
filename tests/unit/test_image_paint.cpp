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
 * The image paint (image_paint.h, IP1 to IP18): the sampling definition against the oracle's vectors, the resource's rules, its
 * hash and dump against the independent generator, what draws nothing, the writers' recognition of a drawn image, and what a
 * sink's capabilities make of an image fill.
 */

#include "scene_helpers.h"

#include "../alloc_tracker.h"
#include "../image_fixtures.h"
#include "../scene_fixtures.h"
#include "../image_vectors.h"
#include "../known_answers.h"
#include "../ref_image.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <string>
#include <vector>

using namespace scene;
using namespace imgfx;

namespace {

const double kInf = std::numeric_limits<double>::infinity();
const double kNaN = std::numeric_limits<double>::quiet_NaN();

GSHSCENE_Color_Space space_from(const imagevec::Space & s) {
  return space_of((GCOL_Primaries)s.primaries, (GCOL_Transfer)s.transfer, s.gamma);
}

/** An image of the oracle's: a raster of its samples, interned with its space and its alpha-only mark. */
const GSHSCENE_Resource * intern_vector_image(GSHSCENE_Store * store, const imagevec::Image & image) {
  Fmt f;
  f.model = image.layout == 1 ? GIMG_CHANNEL_GRAY : image.layout == 3 ? GIMG_CHANNEL_RGB : GIMG_CHANNEL_RGBA;
  f.type = image.type ? GIMG_CHANNEL_FLOAT : GIMG_CHANNEL_UNORM;
  f.channels = image.layout;
  f.bits = image.bits;
  f.alpha = (GIMG_Alpha_Form)image.alpha_form;
  GIMG_Pixel_Format format = format_of(f);
  GIMG_Raster * raster = nullptr;
  if (gimg_raster_create(image.width, image.height, &format, GIMG_RASTER_OWNED, nullptr, 0, &raster) != GIMG_OK) {
    return nullptr;
  }
  unsigned char * pixels = (unsigned char *)gimg_raster_pixels(raster);
  size_t stride = gimg_raster_stride_bytes(raster);
  size_t at = 0;
  for (unsigned y = 0; y < image.height; y++) {
    for (unsigned x = 0; x < image.width; x++) {
      for (unsigned c = 0; c < image.layout; c++) {
        put_sample(sample_at(pixels, stride, f, x, y, c), image.bits, image.samples[at++]);
      }
    }
  }
  GSHSCENE_Image_Params p = params(space_from(image.space), image.alpha_only ? GSHSCENE_IMAGE_ALPHA_ONLY : 0);
  const GSHSCENE_Resource * r = nullptr;
  GSHSCENE_Result result = gshscene_store_intern_image(store, raster, &p, &r);
  gimg_raster_destroy(raster);
  return result == GSHSCENE_OK ? r : nullptr;
}

/** Every vector image interned once, held. */
struct VectorImages {
  StoreHolder store;
  std::vector<Ref> images;
  VectorImages() {
    for (unsigned i = 0; i < imagevec::kImageCount; i++) {
      images.emplace_back(intern_vector_image(store.store, imagevec::kImages[i]));
    }
  }
};

refimg::Setup setup_of(const VectorImages & v, int image, const int window[4], unsigned ex, unsigned ey, unsigned filter,
    const imagevec::Space & filter_space, const imagevec::Space & layer) {
  refimg::Setup s;
  s.image = v.images[(size_t)image].get();
  for (int k = 0; k < 4; k++) {
    s.window[k] = window[k];
  }
  s.extend_x = ex;
  s.extend_y = ey;
  s.filter = filter;
  s.filter_space = space_from(filter_space);
  s.layer = space_from(layer);
  return s;
}

/**
 * Whether @p got is within `tolerance * max(1, the largest |want|)` of @p want in every component (exactly, for a tolerance of 0):
 * a conversion between gamuts mixes the components, so the scale is the pixel's and not the component's.
 */
::testing::AssertionResult close(const double got[4], const double want[4], double tolerance) {
  double scale = 1.0;
  for (int k = 0; k < 4; k++) {
    scale = std::fmax(scale, std::fabs(want[k]));
  }
  for (int k = 0; k < 4; k++) {
    double bound = tolerance * scale;
    if (!(std::fabs(got[k] - want[k]) <= bound)) {
      return ::testing::AssertionFailure() << "component " << k << ": " << got[k] << " against " << want[k] << " (bound "
                                           << bound << ")";
    }
  }
  return ::testing::AssertionSuccess();
}

}  // namespace

/* --- IP2 to IP9: the sampling definition against the oracle ------------------------------------------------------ */

TEST(ImagePaintVectors, TheVectorImagesIntern) {
  VectorImages v;
  for (unsigned i = 0; i < imagevec::kImageCount; i++) {
    ASSERT_NE(v.images[i].get(), nullptr) << imagevec::kImages[i].name;
  }
}

TEST(ImagePaintVectors, EveryPointOfTheOracleIsSampledAsItSamplesIt) {
  VectorImages v;
  unsigned alpha_only = 0, exact = 0;
  for (unsigned i = 0; i < imagevec::kPointCount; i++) {
    const imagevec::Point & p = imagevec::kPoints[i];
    refimg::Setup s = setup_of(v, p.image, p.window, p.extend_x, p.extend_y, p.filter, p.filter_space, p.layer);
    double got[4];
    refimg::point(s, p.u, p.v, got);
    EXPECT_TRUE(close(got, p.expected, p.tolerance)) << p.name << " (vector " << i << ")";
    alpha_only += imagevec::kImages[p.image].alpha_only;
    exact += p.tolerance == 0.0;
  }
  EXPECT_GT(alpha_only, 0u);
  EXPECT_GT(exact, 0u) << "some vectors are exact";
}

TEST(ImagePaintVectors, EveryAreaOfTheOracleIsAveragedAsItAveragesIt) {
  VectorImages v;
  unsigned exact = 0;
  for (unsigned i = 0; i < imagevec::kAreaCount; i++) {
    const imagevec::Area & a = imagevec::kAreas[i];
    refimg::Setup s = setup_of(v, a.image, a.window, a.extend_x, a.extend_y, GSHSCENE_IMAGE_FILTER_AREA, a.filter_space, a.layer);
    double got[4];
    refimg::area(s, a.origin, a.edge_a, a.edge_b, got);
    // The evaluator sums in doubles what the oracle sums in rationals: a region of many cells may differ in the last digits
    // even where the oracle's answer is dyadic, so the exactness the oracle claims is checked at its own stated tolerance
    // widened to a few ulps of the cell count.
    double tolerance = a.tolerance == 0.0 ? 1e-13 : a.tolerance;
    EXPECT_TRUE(close(got, a.expected, tolerance)) << a.name << " (vector " << i << ")";
    exact += a.tolerance == 0.0;
  }
  EXPECT_GT(exact, 0u);
}

TEST(ImagePaintVectors, EveryMapOfTheOracleStretchesOrDoesNotAsItSays) {
  unsigned stretching = 0;
  for (unsigned i = 0; i < imagevec::kStretchCount; i++) {
    const imagevec::Stretch & s = imagevec::kStretches[i];
    const double m[4] = {s.m[0], s.m[1], s.m[2], s.m[3]};
    const double t[2] = {0.0, 0.0};
    GSHGEOM_Map map;
    ASSERT_EQ(gshgeom_map_from_matrix(2, m, t, &map), GSHGEOM_OK) << s.name;
    EXPECT_EQ(gshgeom_map_stretches_every_direction(&map), s.expected) << s.name;
    stretching += (unsigned)s.expected;
  }
  EXPECT_GT(stretching, 0u);
  EXPECT_LT(stretching, imagevec::kStretchCount);
}

/* --- IP4: the decode every sink shares ---------------------------------------------------------------------------- */

TEST(ImageDecode, EachSampleFormatDecodesAsIP4Says) {
  StoreHolder store;
  // 8, 12 and 16 bits: v / (2^n - 1), each division correctly rounded; a gray texel is (g, g, g).
  struct Case {
    Fmt f;
    uint32_t value;
    double want;
  };
  const Case unorm[] = {{kGray8, 255, 1.0}, {kGray8, 0, 0.0}, {kGray8, 51, 51.0 / 255.0}, {kGray12, 4095, 1.0},
      {kGray12, 1000, 1000.0 / 4095.0}, {kGray16, 65535, 1.0}, {kGray16, 12345, 12345.0 / 65535.0}};
  for (const Case & c : unorm) {
    SCOPED_TRACE(std::to_string(c.f.bits) + " bits, " + std::to_string(c.value));
    GIMG_Pixel_Format format = format_of(c.f);
    GIMG_Raster * raster = nullptr;
    ASSERT_EQ(gimg_raster_create(1, 1, &format, GIMG_RASTER_OWNED, nullptr, 0, &raster), GIMG_OK);
    put_sample((unsigned char *)gimg_raster_pixels(raster), c.f.bits, c.value);
    Ref image(copy_in(store.store, raster, params()));
    gimg_raster_destroy(raster);
    ASSERT_NE(image.get(), nullptr);
    double out[4];
    ASSERT_EQ(gshscene_image_decode(image.get(), 0, 0, out), GSHSCENE_OK);
    EXPECT_EQ(out[0], c.want);
    EXPECT_EQ(out[1], c.want);
    EXPECT_EQ(out[2], c.want);
    EXPECT_EQ(out[3], 1.0);
  }
}

TEST(ImageDecode, AFloatSaturatesAtTwoToTheSixtyFourAndAlphaIsNeverConverted) {
  StoreHolder store;
  GIMG_Pixel_Format format = format_of(kRgbaF);
  GIMG_Raster * raster = nullptr;
  ASSERT_EQ(gimg_raster_create(2, 1, &format, GIMG_RASTER_OWNED, nullptr, 0, &raster), GIMG_OK);
  float texels[8] = {-3.0f, 0.5f, 1.0e30f, 1.0f, -1.0e30f, 3.0e38f, 2.0f, 0.5f};
  std::memcpy(gimg_raster_pixels(raster), texels, sizeof texels);
  Ref image(copy_in(store.store, raster, params()));
  gimg_raster_destroy(raster);
  ASSERT_NE(image.get(), nullptr);
  double a[4], b[4];
  ASSERT_EQ(gshscene_image_decode(image.get(), 0, 0, a), GSHSCENE_OK);
  EXPECT_EQ(a[0], -3.0);
  EXPECT_EQ(a[1], 0.5);
  EXPECT_EQ(a[2], 0x1p64) << "1e30 is past 2^64 and saturates";
  EXPECT_EQ(a[3], 1.0);
  ASSERT_EQ(gshscene_image_decode(image.get(), 1, 0, b), GSHSCENE_OK);
  // The second texel is straight with alpha 0.5: IP4 saturates the decoded color, then the product with alpha is formed.
  EXPECT_EQ(b[3], 0.5);
  EXPECT_EQ(b[0], 0.5 * -0x1p64) << "-1e30 saturates at -2^64";
  EXPECT_EQ(b[1], 0.5 * 0x1p64) << "3e38 saturates at 2^64, then is multiplied by alpha";
  EXPECT_EQ(b[2], 0.5 * 2.0);
}

TEST(ImageDecode, AStraightTexelIsMultipliedByItsAlphaAndAPremultipliedOneIsTakenAsStored) {
  StoreHolder store;
  const uint32_t texel[4] = {200, 100, 50, 128};
  for (int premultiplied = 0; premultiplied < 2; premultiplied++) {
    GIMG_Pixel_Format format = format_of(premultiplied ? kRgba8P : kRgba8);
    GIMG_Raster * raster = nullptr;
    ASSERT_EQ(gimg_raster_create(1, 1, &format, GIMG_RASTER_OWNED, nullptr, 0, &raster), GIMG_OK);
    unsigned char * px = (unsigned char *)gimg_raster_pixels(raster);
    for (int c = 0; c < 4; c++) {
      px[c] = (unsigned char)texel[c];
    }
    Ref image(copy_in(store.store, raster, params()));
    gimg_raster_destroy(raster);
    double out[4];
    ASSERT_EQ(gshscene_image_decode(image.get(), 0, 0, out), GSHSCENE_OK);
    double a = 128.0 / 255.0;
    EXPECT_EQ(out[3], a);
    for (int c = 0; c < 3; c++) {
      EXPECT_EQ(out[c], premultiplied ? texel[c] / 255.0 : a * (texel[c] / 255.0)) << "channel " << c;
    }
  }
  // An additive premultiplied texel (a color above its alpha) and one of alpha 0 with a color are taken as given.
  GIMG_Pixel_Format format = format_of(kRgba8P);
  GIMG_Raster * raster = nullptr;
  ASSERT_EQ(gimg_raster_create(2, 1, &format, GIMG_RASTER_OWNED, nullptr, 0, &raster), GIMG_OK);
  const unsigned char bytes[8] = {255, 255, 255, 3, 9, 8, 7, 0};
  std::memcpy(gimg_raster_pixels(raster), bytes, 8);
  Ref image(copy_in(store.store, raster, params()));
  gimg_raster_destroy(raster);
  double add[4], zero[4];
  ASSERT_EQ(gshscene_image_decode(image.get(), 0, 0, add), GSHSCENE_OK);
  ASSERT_EQ(gshscene_image_decode(image.get(), 1, 0, zero), GSHSCENE_OK);
  EXPECT_EQ(add[0], 1.0);
  EXPECT_EQ(add[3], 3.0 / 255.0);
  EXPECT_EQ(zero[0], 9.0 / 255.0);
  EXPECT_EQ(zero[3], 0.0);
}

TEST(ImageDecode, AnAlphaOnlyTexelIsTheOneNumberAndTheCallRefusesWhatIsNotATexel) {
  StoreHolder store;
  RasterHolder raster(make_raster(3, 3, kGray8));
  Ref image(copy_in(store.store, raster.get(), params(GSHSCENE_Color_Space{}, GSHSCENE_IMAGE_ALPHA_ONLY)));
  double out[4];
  ASSERT_EQ(gshscene_image_decode(image.get(), 2, 1, out), GSHSCENE_OK);
  EXPECT_EQ(out[0], 0.0);
  EXPECT_EQ(out[1], 0.0);
  EXPECT_EQ(out[2], 0.0);
  EXPECT_EQ(out[3], (double)sample_value(8, 2, 1, 0) / 255.0);
  EXPECT_EQ(gshscene_image_decode(image.get(), 3, 0, out), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_image_decode(image.get(), 0, 3, out), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_image_decode(nullptr, 0, 0, out), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_image_decode(image.get(), 0, 0, nullptr), GSHSCENE_ERR_INVALID);
  Ref square(intern_square(store.store));
  EXPECT_EQ(gshscene_image_decode(square.get(), 0, 0, out), GSHSCENE_ERR_INVALID);
}


/* --- IP1: the resource ------------------------------------------------------------------------------------------- */

namespace {

/** A store with a color image (RGBA8, 70 by 66) and a stencil (gray8 alpha-only, 65 by 65). */
struct Fixture {
  StoreHolder store;
  Ref color;
  Ref stencil;
  Ref other_color;  // another store's image
  StoreHolder other;
  Fixture() : Fixture(nullptr) {}
  explicit Fixture(GSHSCENE_Index_Hash_Fn index_hash) : store(nullptr, nullptr, index_hash) {
    RasterHolder c(make_raster(70, 66, kRgba8));
    color = Ref(copy_in(store.store, c.get(), params()));
    RasterHolder g(make_raster(65, 65, kGray8));
    stencil = Ref(copy_in(store.store, g.get(), params(GSHSCENE_Color_Space{}, GSHSCENE_IMAGE_ALPHA_ONLY)));
    other_color = Ref(copy_in(other.store, c.get(), params()));
  }
};

GSHSCENE_Image_Paint paint_of(const GSHSCENE_Resource * image) {
  GSHSCENE_Image_Paint p;
  gshscene_image_paint_init(&p);
  p.image = image;
  return p;
}

GSHSCENE_Result intern_paint(GSHSCENE_Store * store, const GSHSCENE_Image_Paint & p, Ref * out = nullptr) {
  const GSHSCENE_Resource * r = nullptr;
  GSHSCENE_Result result = gshscene_store_intern_image_paint(store, &p, &r);
  if (out) {
    *out = Ref(result == GSHSCENE_OK ? r : nullptr);
  } else {
    gshscene_resource_release(result == GSHSCENE_OK ? r : nullptr);
  }
  return result;
}

GSHSCENE_Paint solid_tint(double r = 0.25, double g = 0.5, double b = 0.75, double a = 1.0) {
  return solid(r, g, b, a);
}

GSHSCENE_Paint kind_tint(GSHSCENE_Paint_Kind kind, uint32_t entry = 0, double alpha = 1.0) {
  GSHSCENE_Paint t = solid(0, 0, 0, alpha);
  t.kind = kind;
  t.entry = entry;
  return t;
}

GSHGEOM_Map matrix_map(double a, double b, double c, double d, double tx = 0.0, double ty = 0.0) {
  const double m[4] = {a, b, c, d};
  const double t[2] = {tx, ty};
  GSHGEOM_Map map;
  gshgeom_map_from_matrix(2, m, t, &map);
  return map;
}

}  // namespace

TEST(ImagePaint, InternsReadsBackAndKeepsTheWholeImageWindowExplicit) {
  Fixture f;
  GSHSCENE_Image_Paint p = paint_of(f.color.get());
  p.extend_x = GSHSCENE_IMAGE_EXTEND_REPEAT;
  p.extend_y = GSHSCENE_IMAGE_EXTEND_NONE;
  p.magnify = GSHSCENE_IMAGE_FILTER_CUBIC;
  p.minify = GSHSCENE_IMAGE_FILTER_AREA;
  p.filter_space = linear_srgb_space();
  p.map = translate(1.5, -2.25);
  Ref r;
  ASSERT_EQ(intern_paint(f.store.store, p, &r), GSHSCENE_OK);
  EXPECT_EQ(gshscene_resource_kind(r.get()), GSHSCENE_RESOURCE_IMAGE_PAINT);
  GSHSCENE_Image_Paint back;
  ASSERT_EQ(gshscene_resource_image_paint(r.get(), &back), GSHSCENE_OK);
  EXPECT_EQ(back.image, f.color.get());
  EXPECT_EQ(back.window[0], 0u);
  EXPECT_EQ(back.window[1], 0u);
  EXPECT_EQ(back.window[2], 70u) << "the all-zero window is stored as the whole image";
  EXPECT_EQ(back.window[3], 66u);
  EXPECT_EQ(gshgeom_map_hash(&back.map), gshgeom_map_hash(&p.map));
  EXPECT_EQ(back.extend_x, (uint32_t)GSHSCENE_IMAGE_EXTEND_REPEAT);
  EXPECT_EQ(back.extend_y, (uint32_t)GSHSCENE_IMAGE_EXTEND_NONE);
  EXPECT_EQ(back.magnify, (uint32_t)GSHSCENE_IMAGE_FILTER_CUBIC);
  EXPECT_EQ(back.minify, (uint32_t)GSHSCENE_IMAGE_FILTER_AREA);
  EXPECT_EQ(back.filter_space.transfer, GCOL_TRANSFER_LINEAR);
  EXPECT_EQ(back.tint, nullptr);
  EXPECT_EQ(gshscene_resource_bytes(r.get()), (size_t)GSHSCENE_BYTES_BASE);
  // The explicit whole window is the same paint as the zero one.
  GSHSCENE_Image_Paint explicit_window = p;
  explicit_window.window[2] = 70;
  explicit_window.window[3] = 66;
  Ref again;
  ASSERT_EQ(intern_paint(f.store.store, explicit_window, &again), GSHSCENE_OK);
  EXPECT_EQ(again.get(), r.get());
}

TEST(ImagePaint, TheDefaultsAreTheWholeImageIdentityPadLinearAndTheImagesOwnSpace) {
  GSHSCENE_Image_Paint p;
  std::memset(&p, 0xAB, sizeof p);
  gshscene_image_paint_init(&p);
  EXPECT_EQ(p.image, nullptr);
  for (uint32_t w : p.window) {
    EXPECT_EQ(w, 0u);
  }
  EXPECT_EQ(p.map.dim, 2);
  EXPECT_EQ(p.extend_x, (uint32_t)GSHSCENE_IMAGE_EXTEND_PAD);
  EXPECT_EQ(p.extend_y, (uint32_t)GSHSCENE_IMAGE_EXTEND_PAD);
  EXPECT_EQ(p.magnify, (uint32_t)GSHSCENE_IMAGE_FILTER_LINEAR);
  EXPECT_EQ(p.minify, (uint32_t)GSHSCENE_IMAGE_FILTER_LINEAR);
  EXPECT_TRUE(gshscene_color_space_is_target(&p.filter_space));
  EXPECT_EQ(p.tint, nullptr);
  gshscene_image_paint_init(nullptr);
}

TEST(ImagePaint, AWindowIsInsideTheImageAndNonEmpty) {
  Fixture f;
  struct Case {
    uint32_t w[4];
    bool ok;
  };
  const Case cases[] = {{{0, 0, 0, 0}, true}, {{1, 2, 40, 50}, true}, {{69, 65, 70, 66}, true}, {{0, 0, 1, 1}, true},
      {{3, 3, 3, 10}, false},   // empty in x
      {{3, 3, 10, 3}, false},   // empty in y
      {{5, 3, 4, 10}, false},   // inverted
      {{0, 0, 71, 66}, false},  // past the width
      {{0, 0, 70, 67}, false},  // past the height
      {{70, 0, 71, 10}, false}, {{0, 0, 0, 5}, false}, {{0, 0, 5, 0}, false}};
  for (const Case & c : cases) {
    GSHSCENE_Image_Paint p = paint_of(f.color.get());
    for (int k = 0; k < 4; k++) {
      p.window[k] = c.w[k];
    }
    SCOPED_TRACE(std::to_string(c.w[0]) + "," + std::to_string(c.w[1]) + "," + std::to_string(c.w[2]) + "," + std::to_string(c.w[3]));
    EXPECT_EQ(intern_paint(f.store.store, p), c.ok ? GSHSCENE_OK : GSHSCENE_ERR_INVALID);
  }
}

TEST(ImagePaint, ATintIsPresentExactlyWhenTheImageIsAlphaOnly) {
  Fixture f;
  GSHSCENE_Paint solid_t = solid_tint();
  GSHSCENE_Paint fg = kind_tint(GSHSCENE_PAINT_FOREGROUND, 0, 0.5);
  GSHSCENE_Paint pal = kind_tint(GSHSCENE_PAINT_PALETTE, 7);
  for (const GSHSCENE_Paint * t : {&solid_t, &fg, &pal}) {
    GSHSCENE_Image_Paint stencil = paint_of(f.stencil.get());
    stencil.tint = t;
    Ref r;
    ASSERT_EQ(intern_paint(f.store.store, stencil, &r), GSHSCENE_OK) << "kind " << (int)t->kind;
    GSHSCENE_Image_Paint back;
    ASSERT_EQ(gshscene_resource_image_paint(r.get(), &back), GSHSCENE_OK);
    ASSERT_NE(back.tint, nullptr);
    EXPECT_EQ(back.tint->kind, t->kind);
    EXPECT_EQ(back.tint->entry, t->entry);
    GSHSCENE_Image_Paint colored = paint_of(f.color.get());
    colored.tint = t;
    EXPECT_EQ(intern_paint(f.store.store, colored), GSHSCENE_ERR_INVALID) << "a tint on a color image";
  }
  GSHSCENE_Image_Paint bare = paint_of(f.stencil.get());
  EXPECT_EQ(intern_paint(f.store.store, bare), GSHSCENE_ERR_INVALID) << "an alpha-only image without a tint";
  // The tint is a fill's paint, so kinds that are not a color are refused, and a bad alpha, and a bad entry.
  GSHSCENE_Paint gradient_like = solid_tint();
  gradient_like.kind = GSHSCENE_PAINT_GRADIENT;
  GSHSCENE_Paint image_like = solid_tint();
  image_like.kind = GSHSCENE_PAINT_IMAGE;
  GSHSCENE_Paint bad_alpha = solid_tint(0, 0, 0, 2.0);
  GSHSCENE_Paint nan_color = solid_tint(kNaN, 0, 0, 1.0);
  GSHSCENE_Paint entry_past = kind_tint(GSHSCENE_PAINT_PALETTE, 0xFFFFu);
  for (const GSHSCENE_Paint * t : {&gradient_like, &image_like, &bad_alpha, &nan_color, &entry_past}) {
    GSHSCENE_Image_Paint p = paint_of(f.stencil.get());
    p.tint = t;
    EXPECT_EQ(intern_paint(f.store.store, p), GSHSCENE_ERR_INVALID);
  }
  // N1's bound is a limit, decided after validity.
  GSHSCENE_Paint at_bound = solid_tint(65536.0, -65536.0, 0, 1.0);
  GSHSCENE_Paint past_bound = solid_tint(65537.0, 0, 0, 1.0);
  GSHSCENE_Paint past_bad_alpha = solid_tint(1e300, 0, 0, 2.0);
  GSHSCENE_Image_Paint p = paint_of(f.stencil.get());
  p.tint = &at_bound;
  EXPECT_EQ(intern_paint(f.store.store, p), GSHSCENE_OK);
  p.tint = &past_bound;
  EXPECT_EQ(intern_paint(f.store.store, p), GSHSCENE_ERR_LIMIT);
  p.tint = &past_bad_alpha;
  EXPECT_EQ(intern_paint(f.store.store, p), GSHSCENE_ERR_INVALID) << "an invalid alpha is decided before the bound";
}

TEST(ImagePaint, TheMapIsValidTwoDimensionalAndInsideTheBand) {
  Fixture f;
  GSHSCENE_Image_Paint p = paint_of(f.color.get());
  // Entries at the band's edges pass; one step outside is a limit.
  const double hi = 0x1p64, lo = 0x1p-64;
  p.map = matrix_map(hi, 0, 0, lo, hi, -hi);
  EXPECT_EQ(intern_paint(f.store.store, p), GSHSCENE_OK);
  p.map = matrix_map(std::nextafter(hi, kInf), 0, 0, 1);
  EXPECT_EQ(intern_paint(f.store.store, p), GSHSCENE_ERR_LIMIT);
  p.map = matrix_map(1, 0, 0, std::nextafter(lo, 0.0));
  EXPECT_EQ(intern_paint(f.store.store, p), GSHSCENE_ERR_LIMIT);
  p.map = translate(std::nextafter(hi, kInf), 0);
  EXPECT_EQ(intern_paint(f.store.store, p), GSHSCENE_ERR_LIMIT) << "the translation is held to the band too";
  p.map = matrix_map(0, 0, 0, 0);
  EXPECT_EQ(intern_paint(f.store.store, p), GSHSCENE_OK) << "a singular map is legal: it draws nothing";
  // Not valid maps.
  p.map = matrix_map(1, 0, 0, 1);
  p.map.m[0][0] = kNaN;
  EXPECT_EQ(intern_paint(f.store.store, p), GSHSCENE_ERR_INVALID);
  p.map = matrix_map(1, 0, 0, 1);
  p.map.m[1][1] = kInf;
  EXPECT_EQ(intern_paint(f.store.store, p), GSHSCENE_ERR_INVALID);
  const double m3[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  const double t3[3] = {0, 0, 0};
  ASSERT_EQ(gshgeom_map_from_matrix(3, m3, t3, &p.map), GSHGEOM_OK);
  EXPECT_EQ(intern_paint(f.store.store, p), GSHSCENE_ERR_INVALID) << "a 3D map";
  std::memset(&p.map, 0, sizeof p.map);
  EXPECT_EQ(intern_paint(f.store.store, p), GSHSCENE_ERR_INVALID) << "a zero-filled map is not an image-space map";
  // Validity is decided before the bound: a past-the-band entry beside an invalid enum is invalid.
  p.map = matrix_map(std::nextafter(hi, kInf), 0, 0, 1);
  p.extend_x = 9;
  EXPECT_EQ(intern_paint(f.store.store, p), GSHSCENE_ERR_INVALID);
}

TEST(ImagePaint, TheEnumerationsAndTheFilterSpaceAreChecked) {
  Fixture f;
  for (int field = 0; field < 4; field++) {
    GSHSCENE_Image_Paint ok = paint_of(f.color.get());
    uint32_t * slot = field == 0 ? &ok.extend_x : field == 1 ? &ok.extend_y : field == 2 ? &ok.magnify : &ok.minify;
    uint32_t count = field < 2 ? (uint32_t)GSHSCENE_IMAGE_EXTEND_COUNT : (uint32_t)GSHSCENE_IMAGE_FILTER_COUNT;
    *slot = count - 1;
    EXPECT_EQ(intern_paint(f.store.store, ok), GSHSCENE_OK) << field;
    *slot = count;
    EXPECT_EQ(intern_paint(f.store.store, ok), GSHSCENE_ERR_INVALID) << field;
    *slot = 0xFFFFFFFFu;
    EXPECT_EQ(intern_paint(f.store.store, ok), GSHSCENE_ERR_INVALID) << field;
  }
  GSHSCENE_Image_Paint p = paint_of(f.color.get());
  p.filter_space = space_of(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_UNKNOWN);
  EXPECT_EQ(intern_paint(f.store.store, p), GSHSCENE_ERR_INVALID) << "a partly given space";
  p.filter_space = space_of(GCOL_PRIMARIES_PROPHOTO, GCOL_TRANSFER_SRGB);
  EXPECT_EQ(intern_paint(f.store.store, p), GSHSCENE_ERR_UNSUPPORTED) << "a reserved space";
  p.filter_space = space_of(GCOL_PRIMARIES_DISPLAY_P3, GCOL_TRANSFER_GAMMA, 2.2);
  EXPECT_EQ(intern_paint(f.store.store, p), GSHSCENE_OK);
}

TEST(ImagePaint, AStencilsFilterSpaceDoesNotApplyAndIsStoredAsZero) {
  Fixture f;
  GSHSCENE_Paint tint = solid_tint();
  GSHSCENE_Image_Paint with_space = paint_of(f.stencil.get());
  with_space.tint = &tint;
  with_space.filter_space = linear_srgb_space();
  GSHSCENE_Image_Paint without = paint_of(f.stencil.get());
  without.tint = &tint;
  Ref a, b;
  ASSERT_EQ(intern_paint(f.store.store, with_space, &a), GSHSCENE_OK);
  ASSERT_EQ(intern_paint(f.store.store, without, &b), GSHSCENE_OK);
  EXPECT_EQ(a.get(), b.get()) << "one drawing has one spelling";
  GSHSCENE_Image_Paint back;
  ASSERT_EQ(gshscene_resource_image_paint(a.get(), &back), GSHSCENE_OK);
  EXPECT_TRUE(gshscene_color_space_is_target(&back.filter_space));
  // But an invalid space is still invalid.
  with_space.filter_space = space_of(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_UNKNOWN);
  EXPECT_EQ(intern_paint(f.store.store, with_space), GSHSCENE_ERR_INVALID);
}

static void every_field_tells_paints_apart(GSHSCENE_Index_Hash_Fn index_hash) {
  Fixture f(index_hash);
  GSHSCENE_Image_Paint base = paint_of(f.color.get());
  Ref a;
  ASSERT_EQ(intern_paint(f.store.store, base, &a), GSHSCENE_OK);
  Ref b;
  ASSERT_EQ(intern_paint(f.store.store, base, &b), GSHSCENE_OK);
  EXPECT_EQ(a.get(), b.get());
  std::vector<GSHSCENE_Image_Paint> variants;
  auto vary = [&](auto change) {
    GSHSCENE_Image_Paint p = base;
    change(p);
    variants.push_back(p);
  };
  vary([](GSHSCENE_Image_Paint & p) { p.window[0] = 1; p.window[2] = 70; p.window[3] = 66; });
  vary([](GSHSCENE_Image_Paint & p) { p.window[1] = 1; p.window[2] = 70; p.window[3] = 66; });
  vary([](GSHSCENE_Image_Paint & p) { p.window[2] = 69; p.window[3] = 66; });
  vary([](GSHSCENE_Image_Paint & p) { p.window[2] = 70; p.window[3] = 65; });
  vary([](GSHSCENE_Image_Paint & p) { p.map = translate(0.5, 0.0); });
  // Maps of one kind that differ in one entry only: the comparison must read every entry, not the kind.
  vary([](GSHSCENE_Image_Paint & p) { p.map = translate(0.5, 0.25); });
  vary([](GSHSCENE_Image_Paint & p) { p.map = translate(0.25, 0.5); });
  vary([](GSHSCENE_Image_Paint & p) { p.map = matrix_map(2, 0, 0, 1); });
  vary([](GSHSCENE_Image_Paint & p) { p.map = matrix_map(1, 0, 0, 2); });
  vary([](GSHSCENE_Image_Paint & p) { p.map = matrix_map(1, 1, 0, 1); });
  vary([](GSHSCENE_Image_Paint & p) { p.map = matrix_map(1, 0, 1, 1); });
  vary([](GSHSCENE_Image_Paint & p) { p.extend_x = GSHSCENE_IMAGE_EXTEND_REPEAT; });
  vary([](GSHSCENE_Image_Paint & p) { p.extend_y = GSHSCENE_IMAGE_EXTEND_REFLECT; });
  vary([](GSHSCENE_Image_Paint & p) { p.magnify = GSHSCENE_IMAGE_FILTER_NEAREST; });
  vary([](GSHSCENE_Image_Paint & p) { p.minify = GSHSCENE_IMAGE_FILTER_AREA; });
  vary([](GSHSCENE_Image_Paint & p) { p.filter_space = linear_srgb_space(); });
  vary([](GSHSCENE_Image_Paint & p) { p.filter_space = srgb_space(); });
  std::set<const GSHSCENE_Resource *> distinct = {a.get()};
  std::vector<Ref> held;
  for (const GSHSCENE_Image_Paint & p : variants) {
    held.emplace_back();
    ASSERT_EQ(intern_paint(f.store.store, p, &held.back()), GSHSCENE_OK);
    distinct.insert(held.back().get());
  }
  EXPECT_EQ(distinct.size(), 1u + variants.size());
  // A tint's every field too.
  std::vector<GSHSCENE_Paint> tints = {solid_tint(), solid_tint(0.26, 0.5, 0.75, 1.0), solid_tint(0.25, 0.5, 0.75, 0.5),
      kind_tint(GSHSCENE_PAINT_FOREGROUND, 0, 1.0), kind_tint(GSHSCENE_PAINT_FOREGROUND, 0, 0.5),
      kind_tint(GSHSCENE_PAINT_PALETTE, 1), kind_tint(GSHSCENE_PAINT_PALETTE, 2), kind_tint(GSHSCENE_PAINT_PALETTE, 0, 1.0)};
  GSHSCENE_Paint other_space = solid_tint();
  other_space.space = srgb_space();
  tints.push_back(other_space);
  std::set<const GSHSCENE_Resource *> tinted;
  std::vector<Ref> tint_held;
  for (const GSHSCENE_Paint & t : tints) {
    GSHSCENE_Image_Paint p = paint_of(f.stencil.get());
    p.tint = &t;
    tint_held.emplace_back();
    ASSERT_EQ(intern_paint(f.store.store, p, &tint_held.back()), GSHSCENE_OK);
    tinted.insert(tint_held.back().get());
  }
  EXPECT_EQ(tinted.size(), tints.size());
  // Another image is another paint; an image is held by the paint.
  RasterHolder other_raster(make_raster(70, 66, kRgba8P));
  Ref premult(copy_in(f.store.store, other_raster.get(), params()));
  GSHSCENE_Image_Paint on_other = paint_of(premult.get());
  Ref c;
  ASSERT_EQ(intern_paint(f.store.store, on_other, &c), GSHSCENE_OK);
  EXPECT_NE(c.get(), a.get());
}

TEST(ImagePaint, EqualContentIsOneHandleAndEveryFieldTellsPaintsApart) {
  every_field_tells_paints_apart(nullptr);
}

// The index finds a candidate; only the comparison tells paints apart once every index collides.
TEST(ImagePaint, EqualContentIsOneHandleAndEveryFieldTellsPaintsApartWhenEveryIndexCollides) {
  every_field_tells_paints_apart(all_zero);
}

TEST(ImagePaint, NegativeZeroIsStoredAsPositiveZeroInTheMapAndTheTint) {
  Fixture f;
  GSHSCENE_Image_Paint plus = paint_of(f.stencil.get());
  GSHSCENE_Paint tint_plus = solid_tint(0.0, 0.5, 0.0, 1.0);
  plus.tint = &tint_plus;
  plus.map = matrix_map(1, 0.0, 0.0, 1, 0.0, 0.0);
  GSHSCENE_Image_Paint minus = plus;
  GSHSCENE_Paint tint_minus = solid_tint(-0.0, 0.5, -0.0, 1.0);
  minus.tint = &tint_minus;
  minus.map.m[0][1] = -0.0;
  minus.map.m[1][0] = -0.0;
  minus.map.t[0] = -0.0;
  Ref a, b;
  ASSERT_EQ(intern_paint(f.store.store, plus, &a), GSHSCENE_OK);
  ASSERT_EQ(intern_paint(f.store.store, minus, &b), GSHSCENE_OK);
  EXPECT_EQ(a.get(), b.get());
  GSHSCENE_Image_Paint back;
  ASSERT_EQ(gshscene_resource_image_paint(b.get(), &back), GSHSCENE_OK);
  EXPECT_FALSE(std::signbit(back.map.t[0]));
  EXPECT_FALSE(std::signbit(back.tint->rgba[0]));
}

TEST(ImagePaint, TheImageIsAnImageResourceOfThisStoreAndNothingElse) {
  Fixture f;
  GSHSCENE_Image_Paint p = paint_of(nullptr);
  EXPECT_EQ(intern_paint(f.store.store, p), GSHSCENE_ERR_INVALID);
  p = paint_of(f.other_color.get());
  EXPECT_EQ(intern_paint(f.store.store, p), GSHSCENE_ERR_INVALID) << "another store's image";
  Ref square(intern_square(f.store.store));
  p = paint_of(square.get());
  EXPECT_EQ(intern_paint(f.store.store, p), GSHSCENE_ERR_INVALID) << "a path";
  const GSHSCENE_Resource * out = nullptr;
  GSHSCENE_Image_Paint ok = paint_of(f.color.get());
  EXPECT_EQ(gshscene_store_intern_image_paint(nullptr, &ok, &out), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_store_intern_image_paint(f.store.store, nullptr, &out), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_store_intern_image_paint(f.store.store, &ok, nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(out, nullptr);
  GSHSCENE_Image_Paint back;
  EXPECT_EQ(gshscene_resource_image_paint(f.color.get(), &back), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_resource_image_paint(nullptr, &back), GSHSCENE_ERR_INVALID);
  Ref paint;
  ASSERT_EQ(intern_paint(f.store.store, ok, &paint), GSHSCENE_OK);
  EXPECT_EQ(gshscene_resource_image_paint(paint.get(), nullptr), GSHSCENE_ERR_INVALID);
}

TEST(ImagePaint, ThePaintHoldsItsImageUntilItsLastRelease) {
  Fixture f;
  size_t before = gshscene_resource_refcount(f.color.get());
  {
    GSHSCENE_Image_Paint p = paint_of(f.color.get());
    Ref r;
    ASSERT_EQ(intern_paint(f.store.store, p, &r), GSHSCENE_OK);
    EXPECT_EQ(gshscene_resource_refcount(f.color.get()), before + 1);
    Ref again;
    ASSERT_EQ(intern_paint(f.store.store, p, &again), GSHSCENE_OK);
    EXPECT_EQ(gshscene_resource_refcount(f.color.get()), before + 1) << "a hit holds nothing more";
  }
  EXPECT_EQ(gshscene_resource_refcount(f.color.get()), before);
}

TEST(ImagePaint, AGradientStopIsNeverAnImagePaint) {
  Fixture f;
  Ref paint;
  ASSERT_EQ(intern_paint(f.store.store, paint_of(f.color.get()), &paint), GSHSCENE_OK);
  GSHSCENE_Gradient g;
  std::memset(&g, 0, sizeof g);
  g.kind = GSHSCENE_GRADIENT_LINEAR;
  g.p1[0] = 1.0;
  gshgeom_map_identity(2, &g.map);
  GSHSCENE_Gradient_Stop stops[2];
  std::memset(stops, 0, sizeof stops);
  stops[0].color = solid(1, 0, 0, 1);
  stops[1].offset = 1.0;
  stops[1].color = solid(0, 0, 1, 1);
  stops[1].color.kind = GSHSCENE_PAINT_IMAGE;
  stops[1].color.resource = paint.get();
  g.stops = stops;
  g.stop_count = 2;
  const GSHSCENE_Resource * out = nullptr;
  EXPECT_EQ(gshscene_store_intern_gradient(f.store.store, &g, &out), GSHSCENE_ERR_INVALID);
}

/* --- IP1, IP13: a list names an image paint ----------------------------------------------------------------------- */

TEST(ImagePaint, AFillAndAStrokeNameAnImagePaintAndAGradientNeverStandsForIt) {
  Fixture f;
  Ref paint;
  ASSERT_EQ(intern_paint(f.store.store, paint_of(f.color.get()), &paint), GSHSCENE_OK);
  Ref square(intern_square(f.store.store));
  GSHSCENE_Stroke_Style style;
  gshscene_stroke_style_init(&style);
  const GSHSCENE_Resource * s = nullptr;
  ASSERT_EQ(gshscene_store_intern_stroke_style(f.store.store, &style, &s), GSHSCENE_OK);
  Ref stroke_style(s);

  ListHolder list(f.store.store);
  for (double alpha : {0.0, 0.5, 1.0}) {
    GSHSCENE_Paint p = solid(0, 0, 0, alpha);
    p.kind = GSHSCENE_PAINT_IMAGE;
    p.resource = paint.get();
    EXPECT_EQ(gshscene_list_fill(list.list, square.get(), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK) << alpha;
    EXPECT_EQ(gshscene_list_stroke(list.list, square.get(), stroke_style.get(), nullptr, &p, 0, 0.0), GSHSCENE_OK) << alpha;
  }
  EXPECT_EQ(gshscene_list_count(list.list), 6u);
  // The wrong things in the paint.
  GSHSCENE_Paint good = solid(0, 0, 0, 1.0);
  good.kind = GSHSCENE_PAINT_IMAGE;
  good.resource = paint.get();
  auto fill = [&](const GSHSCENE_Paint & p) {
    return gshscene_list_fill(list.list, square.get(), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0);
  };
  GSHSCENE_Paint no_resource = good;
  no_resource.resource = nullptr;
  EXPECT_EQ(fill(no_resource), GSHSCENE_ERR_INVALID);
  GSHSCENE_Paint as_gradient = good;
  as_gradient.kind = GSHSCENE_PAINT_GRADIENT;
  EXPECT_EQ(fill(as_gradient), GSHSCENE_ERR_INVALID) << "an image paint where a gradient is named";
  GSHSCENE_Paint as_solid = good;
  EXPECT_EQ(fill(as_solid), GSHSCENE_OK);
  as_solid.kind = GSHSCENE_PAINT_SOLID;
  EXPECT_EQ(fill(as_solid), GSHSCENE_ERR_INVALID) << "a solid paint names no resource";
  GSHSCENE_Paint with_entry = good;
  with_entry.entry = 1;
  EXPECT_EQ(fill(with_entry), GSHSCENE_ERR_INVALID);
  GSHSCENE_Paint with_color = good;
  with_color.rgba[0] = 0.5;
  EXPECT_EQ(fill(with_color), GSHSCENE_ERR_INVALID);
  GSHSCENE_Paint with_space = good;
  with_space.space = srgb_space();
  EXPECT_EQ(fill(with_space), GSHSCENE_ERR_INVALID);
  GSHSCENE_Paint past_one = good;
  past_one.rgba[3] = 1.5;
  EXPECT_EQ(fill(past_one), GSHSCENE_ERR_INVALID);
  GSHSCENE_Paint wrong_kind = good;
  wrong_kind.resource = f.color.get();
  EXPECT_EQ(fill(wrong_kind), GSHSCENE_ERR_INVALID) << "an image resource is not an image paint";
  Ref foreign;
  {
    StoreHolder other;
    RasterHolder r(make_raster(4, 4, kRgba8));
    Ref other_image(copy_in(other.store, r.get(), params()));
    ASSERT_EQ(intern_paint(other.store, paint_of(other_image.get()), &foreign), GSHSCENE_OK);
    GSHSCENE_Paint from_elsewhere = good;
    from_elsewhere.resource = foreign.get();
    EXPECT_EQ(fill(from_elsewhere), GSHSCENE_ERR_INVALID) << "another store's image paint";
  }
  // A gradient paint cannot name an image paint.
  GSHSCENE_Paint gradient_names_it = good;
  gradient_names_it.kind = GSHSCENE_PAINT_GRADIENT;
  EXPECT_EQ(fill(gradient_names_it), GSHSCENE_ERR_INVALID);
}

TEST(ImagePaint, TheHashAndTheDumpAreWhatTheIndependentGeneratorSays) {
  StoreHolder store;
  // The images of the generator's cases, interned with their spaces.
  auto image_named = [&](const char * name) -> const GSHSCENE_Resource * {
    for (const known::ImageCase & c : known::kImageCases) {
      if (std::strcmp(c.name, name) == 0) {
        Fmt fm;
        fm.model = c.layout == 1 ? GIMG_CHANNEL_GRAY : c.layout == 3 ? GIMG_CHANNEL_RGB : GIMG_CHANNEL_RGBA;
        fm.type = c.type ? GIMG_CHANNEL_FLOAT : GIMG_CHANNEL_UNORM;
        fm.channels = c.layout;
        fm.bits = c.bits;
        fm.alpha = (GIMG_Alpha_Form)c.alpha;
        RasterHolder raster(make_raster(c.width, c.height, fm));
        GSHSCENE_Image_Params p = params(space_of((GCOL_Primaries)c.primaries, (GCOL_Transfer)c.transfer, c.gamma),
            c.alpha_only ? GSHSCENE_IMAGE_ALPHA_ONLY : 0);
        return copy_in(store.store, raster.get(), p);
      }
    }
    return nullptr;
  };
  Ref rgba(image_named("rgba8-straight-65x129"));
  Ref stencil(image_named("gray8-alpha-only-65x65"));
  Ref rgb12(image_named("rgb12-70x66"));
  Ref rgbaf(image_named("rgbaf32-premultiplied-2x66"));
  ASSERT_NE(rgba.get(), nullptr);
  ASSERT_NE(stencil.get(), nullptr);
  ASSERT_NE(rgb12.get(), nullptr);
  ASSERT_NE(rgbaf.get(), nullptr);
  GSHGEOM_Map design;
  gshgeom_map_design_to_model(2.5, 10, 20, &design);

  std::vector<GSHSCENE_Image_Paint> paints(6);
  GSHSCENE_Paint solid_t = solid(0.25, 0.5, 0.75, 1.0);
  solid_t.space = srgb_space();
  GSHSCENE_Paint fg_t = kind_tint(GSHSCENE_PAINT_FOREGROUND, 0, 0.5);
  GSHSCENE_Paint pal_t = kind_tint(GSHSCENE_PAINT_PALETTE, 2, 1.0);
  paints[0] = paint_of(rgba.get());
  paints[0].window[2] = 65;
  paints[0].window[3] = 129;
  paints[1] = paint_of(stencil.get());
  paints[1].window[0] = 1;
  paints[1].window[1] = 2;
  paints[1].window[2] = 40;
  paints[1].window[3] = 50;
  paints[1].map = translate(1.5, -2.25);
  paints[1].extend_x = GSHSCENE_IMAGE_EXTEND_REPEAT;
  paints[1].extend_y = GSHSCENE_IMAGE_EXTEND_REFLECT;
  paints[1].magnify = GSHSCENE_IMAGE_FILTER_NEAREST;
  paints[1].minify = GSHSCENE_IMAGE_FILTER_AREA;
  paints[1].tint = &solid_t;
  paints[2] = paint_of(rgb12.get());
  paints[2].window[0] = 4;
  paints[2].window[1] = 4;
  paints[2].window[2] = 66;
  paints[2].window[3] = 60;
  paints[2].map = design;
  paints[2].extend_x = GSHSCENE_IMAGE_EXTEND_NONE;
  paints[2].extend_y = GSHSCENE_IMAGE_EXTEND_REFLECT;
  paints[2].magnify = GSHSCENE_IMAGE_FILTER_CUBIC;
  paints[2].minify = GSHSCENE_IMAGE_FILTER_AREA;
  paints[2].filter_space = linear_srgb_space();
  paints[3] = paint_of(stencil.get());
  paints[3].minify = GSHSCENE_IMAGE_FILTER_NEAREST;
  paints[3].tint = &fg_t;
  paints[4] = paint_of(stencil.get());
  paints[4].minify = GSHSCENE_IMAGE_FILTER_NEAREST;
  paints[4].tint = &pal_t;
  paints[5] = paint_of(rgbaf.get());
  paints[5].map = translate(1.5, -2.25);
  paints[5].extend_x = GSHSCENE_IMAGE_EXTEND_REFLECT;
  paints[5].extend_y = GSHSCENE_IMAGE_EXTEND_REPEAT;
  paints[5].magnify = GSHSCENE_IMAGE_FILTER_AREA;
  paints[5].minify = GSHSCENE_IMAGE_FILTER_CUBIC;
  paints[5].filter_space = space_of(GCOL_PRIMARIES_DISPLAY_P3, GCOL_TRANSFER_GAMMA, 2.2);
  std::vector<Ref> held;
  const char * const expected_dump[6] = {known::kImagePaintDump0, known::kImagePaintDump1, known::kImagePaintDump2,
      known::kImagePaintDump3, known::kImagePaintDump4, known::kImagePaintDump5};
  for (size_t i = 0; i < paints.size(); i++) {
    SCOPED_TRACE(i);
    held.emplace_back();
    ASSERT_EQ(intern_paint(store.store, paints[i], &held.back()), GSHSCENE_OK);
    EXPECT_EQ(gshscene_resource_hash(held.back().get()), known::kImagePaintHashes[i]);
    EXPECT_EQ(dump_resource(held.back().get()), expected_dump[i]);
  }
  // A list of fills with them, hashed and dumped as the generator spells it.
  Ref square(intern_square(store.store));
  Ref fixture(intern_fixture(store.store));
  ListHolder list(store.store);
  auto image_fill = [&](const GSHSCENE_Resource * path, const GSHGEOM_Map * map, GSHGEOM_Fill_Rule rule, double alpha,
                        const GSHSCENE_Resource * paint, int32_t layer, double depth) {
    GSHSCENE_Paint p = solid(0, 0, 0, alpha);
    p.kind = GSHSCENE_PAINT_IMAGE;
    p.resource = paint;
    GSHGEOM_Map identity;
    gshgeom_map_identity(2, &identity);
    return gshscene_list_fill(list.list, path, map ? map : &identity, rule, &p, layer, depth);
  };
  GSHGEOM_Map tr = translate(1.5, -2.25);
  ASSERT_EQ(image_fill(square.get(), nullptr, GSHGEOM_FILL_NONZERO, 1.0, held[0].get(), 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(image_fill(fixture.get(), &tr, GSHGEOM_FILL_EVEN_ODD, 0.5, held[1].get(), 2, 0.0), GSHSCENE_OK);
  ASSERT_EQ(image_fill(square.get(), &design, GSHGEOM_FILL_NONZERO, 0.25, held[2].get(), 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(image_fill(square.get(), nullptr, GSHGEOM_FILL_NONZERO, 0.0, held[0].get(), 0, 1.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(list.list), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_hash(list.list), known::kImagePaintListHash);
  EXPECT_EQ(dump_list(list.list), known::kImagePaintListDump);
}


/* --- the items the next groups look at ---------------------------------------------------------------------------- */

namespace {

/** A closed list of one fill (or stroke) of @p path with the image paint @p paint at alpha @p alpha. */
struct Unit {
  ListHolder list;
  Ref style;
  const GSHSCENE_Item * item = nullptr;
  Unit(GSHSCENE_Store * store, const GSHSCENE_Resource * path, const GSHGEOM_Map * item_map, const GSHSCENE_Resource * paint,
      double alpha = 1.0, bool stroke = false)
      : list(store) {
    GSHSCENE_Paint p = solid(0, 0, 0, alpha);
    p.kind = GSHSCENE_PAINT_IMAGE;
    p.resource = paint;
    if (stroke) {
      GSHSCENE_Stroke_Style st;
      gshscene_stroke_style_init(&st);
      const GSHSCENE_Resource * s = nullptr;
      EXPECT_EQ(gshscene_store_intern_stroke_style(store, &st, &s), GSHSCENE_OK);
      style = Ref(s);
      EXPECT_EQ(gshscene_list_stroke(list.list, path, style.get(), item_map, &p, 0, 0.0), GSHSCENE_OK);
    } else {
      EXPECT_EQ(gshscene_list_fill(list.list, path, item_map, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK);
    }
    EXPECT_EQ(gshscene_list_close(list.list), GSHSCENE_OK);
    EXPECT_EQ(gshscene_list_item(list.list, 0, &item), GSHSCENE_OK);
  }
};

GSHSCENE_Sink_Caps full_caps() {
  GSHSCENE_Sink_Caps caps;
  std::memset(&caps, 0, sizeof caps);
  caps.item_kinds = (1u << GSHSCENE_ITEM_FILL) | (1u << GSHSCENE_ITEM_STROKE);
  caps.stroke_native = GSHSCENE_STROKE_NATIVE_ALL;
  caps.image_filters = (1u << GSHSCENE_IMAGE_FILTER_COUNT) - 1u;
  caps.image_extends = (1u << GSHSCENE_IMAGE_EXTEND_COUNT) - 1u;
  caps.image_formats = GSHSCENE_IMAGE_FORMAT_ALL;
  caps.flags = GSHSCENE_CAP_IMAGE_FILTER_SPACES | GSHSCENE_CAP_STROKE_EXPAND;
  caps.cancel_unit = GSHSCENE_CANCEL_BETWEEN_UNITS;
  caps.work_measure = "units";
  return caps;
}

}  // namespace

/* --- IP11: what draws nothing ------------------------------------------------------------------------------------- */

TEST(ImagePaintNotice, AMapOfExactlyZeroDeterminantAnywhereInTheChainDrawsNothing) {
  Fixture f;
  Ref square(intern_square(f.store.store));
  const GSHGEOM_Map singular = matrix_map(1, 2, 2, 4);
  const GSHGEOM_Map nearly = matrix_map(1, 2, 2, std::nextafter(4.0, 5.0));
  const GSHGEOM_Map scale2 = matrix_map(2, 0, 0, 2);
  for (int stroke = 0; stroke < 2; stroke++) {
    SCOPED_TRACE(stroke ? "stroke" : "fill");
    auto notice = [&](const Unit & u, const GSHGEOM_Map * instances, size_t count, const GSHGEOM_Map * target) {
      return stroke ? gshscene_stroke_notice(u.item, instances, count, target) : gshscene_paint_notice(u.item, instances, count, target);
    };
    GSHSCENE_Image_Paint p = paint_of(f.color.get());
    Ref plain;
    ASSERT_EQ(intern_paint(f.store.store, p, &plain), GSHSCENE_OK);
    Unit ok(f.store.store, square.get(), &scale2, plain.get(), 1.0, stroke != 0);
    EXPECT_EQ(notice(ok, nullptr, 0, nullptr), 0u) << "an invertible chain";
    EXPECT_EQ(notice(ok, &scale2, 1, &scale2), 0u);
    // The paint's own map.
    p.map = singular;
    Ref bad_g;
    ASSERT_EQ(intern_paint(f.store.store, p, &bad_g), GSHSCENE_OK);
    Unit g(f.store.store, square.get(), nullptr, bad_g.get(), 1.0, stroke != 0);
    EXPECT_EQ(notice(g, nullptr, 0, nullptr), (unsigned)GSHSCENE_REASON_MAP_NOT_INVERTIBLE) << "the paint's map";
    p.map = nearly;
    Ref near_g;
    ASSERT_EQ(intern_paint(f.store.store, p, &near_g), GSHSCENE_OK);
    Unit n(f.store.store, square.get(), nullptr, near_g.get(), 1.0, stroke != 0);
    EXPECT_EQ(notice(n, nullptr, 0, nullptr), 0u) << "a determinant of one ulp is not zero";
    // The item's map, an instance's, the target's.
    Unit m(f.store.store, square.get(), &singular, plain.get(), 1.0, stroke != 0);
    EXPECT_EQ(notice(m, nullptr, 0, nullptr), (unsigned)GSHSCENE_REASON_MAP_NOT_INVERTIBLE) << "the item's map";
    EXPECT_EQ(notice(ok, &singular, 1, nullptr), (unsigned)GSHSCENE_REASON_MAP_NOT_INVERTIBLE) << "an instance's map";
    const GSHGEOM_Map two[2] = {scale2, singular};
    EXPECT_EQ(notice(ok, two, 2, nullptr), (unsigned)GSHSCENE_REASON_MAP_NOT_INVERTIBLE) << "the leaf instance";
    EXPECT_EQ(notice(ok, nullptr, 0, &singular), (unsigned)GSHSCENE_REASON_MAP_NOT_INVERTIBLE) << "the target's map";
    EXPECT_EQ(notice(ok, nullptr, 0, &nearly), 0u);
    // A zero alpha multiplier is a legal paint that draws nothing without being noticed.
    Unit zero_alpha(f.store.store, square.get(), nullptr, plain.get(), 0.0, stroke != 0);
    EXPECT_EQ(notice(zero_alpha, nullptr, 0, nullptr), 0u);
  }
}

TEST(ImagePaintNotice, ThePredicateIsNotAboutAnythingButFillsAndStrokesOfAnImagePaint) {
  Fixture f;
  Ref square(intern_square(f.store.store));
  ListHolder list(f.store.store);
  GSHSCENE_Paint plain = solid(0, 0, 0, 1);
  ASSERT_EQ(gshscene_list_fill(list.list, square.get(), nullptr, GSHGEOM_FILL_NONZERO, &plain, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(list.list), GSHSCENE_OK);
  const GSHSCENE_Item * item = nullptr;
  ASSERT_EQ(gshscene_list_item(list.list, 0, &item), GSHSCENE_OK);
  EXPECT_EQ(gshscene_paint_notice(item, nullptr, 0, nullptr), 0u);
  EXPECT_EQ(gshscene_paint_notice(nullptr, nullptr, 0, nullptr), 0u);
}

/* --- IP14: a drawn image ------------------------------------------------------------------------------------------ */

namespace {

/** A path of four points (a closed subpath of four lines). */
const GSHSCENE_Resource * quad_path(GSHSCENE_Store * store, const double p[8], bool close = true, int points = 4) {
  GSHGEOM_Path * path = new_path();
  gshgeom_path_move_to(path, P(p[0], p[1]));
  for (int i = 1; i < points; i++) {
    gshgeom_path_line_to(path, P(p[2 * (i % 4)], p[2 * (i % 4) + 1]));
  }
  if (close) {
    gshgeom_path_close(path);
  }
  const GSHSCENE_Resource * r = intern(store, path);
  gshgeom_path_destroy(path);
  return r;
}

}  // namespace

TEST(ImagePaintDraw, TheHelperPathAlwaysSatisfiesThePredicateAndTheStatedShapesOnlyDo) {
  Fixture f;
  GSHSCENE_Image_Paint p = paint_of(f.color.get());
  p.window[0] = 3;
  p.window[1] = 5;
  p.window[2] = 60;
  p.window[3] = 50;
  p.map = matrix_map(0.6, -0.8, 0.8, 0.6, 12.5, -3.25);  // a rotation, a scale and a move, none exact in binary
  Ref paint;
  ASSERT_EQ(intern_paint(f.store.store, p, &paint), GSHSCENE_OK);
  Ref rect;
  ASSERT_EQ(gshscene_image_paint_rect_path(f.store.store, paint.get(), &rect.resource), GSHSCENE_OK);
  GSHSCENE_Image_Paint back;
  ASSERT_EQ(gshscene_resource_image_paint(paint.get(), &back), GSHSCENE_OK);
  double want[8];
  const double xs[4] = {3, 60, 60, 3}, ys[4] = {5, 5, 50, 50};
  for (int i = 0; i < 4; i++) {
    GSHGEOM_Point q;
    ASSERT_EQ(gshgeom_map_apply_point(&back.map, P(xs[i], ys[i]), &q), GSHGEOM_OK);
    want[2 * i] = q.x;
    want[2 * i + 1] = q.y;
  }
  {
    Unit u(f.store.store, rect.get(), nullptr, paint.get());
    double quad[8] = {0};
    EXPECT_EQ(gshscene_image_fill_is_draw(u.item, quad), 1) << "the helper's own path";
    for (int k = 0; k < 8; k++) {
      EXPECT_EQ(quad[k], want[k]) << k;
    }
    EXPECT_EQ(gshscene_image_fill_is_draw(u.item, nullptr), 1) << "the corners are optional";
  }
  { // the item's own map is not part of the test
    GSHGEOM_Map m = matrix_map(3, 1, -1, 3, 7, 9);
    Unit u(f.store.store, rect.get(), &m, paint.get());
    EXPECT_EQ(gshscene_image_fill_is_draw(u.item, nullptr), 1);
  }
  { // either winding, from either starting corner
    for (int reversed = 0; reversed < 2; reversed++) {
      for (int shift = 0; shift < 4; shift++) {
        double pts[8];
        for (int i = 0; i < 4; i++) {
          int k = reversed ? ((shift - i) % 4 + 4) % 4 : (shift + i) % 4;
          pts[2 * i] = want[2 * k];
          pts[2 * i + 1] = want[2 * k + 1];
        }
        Ref path(quad_path(f.store.store, pts));
        Unit u(f.store.store, path.get(), nullptr, paint.get());
        EXPECT_EQ(gshscene_image_fill_is_draw(u.item, nullptr), 1) << "reversed " << reversed << " shift " << shift;
      }
    }
  }
  // What is not a drawn image.
  auto no = [&](const GSHSCENE_Resource * path, const GSHSCENE_Resource * with_paint, const char * why) {
    Unit u(f.store.store, path, nullptr, with_paint);
    EXPECT_EQ(gshscene_image_fill_is_draw(u.item, nullptr), 0) << why;
  };
  { // one ulp off in one coordinate of one corner
    double pts[8];
    std::memcpy(pts, want, sizeof pts);
    pts[3] = std::nextafter(pts[3], 1e9);
    Ref path(quad_path(f.store.store, pts));
    no(path.get(), paint.get(), "one ulp off");
    std::memcpy(pts, want, sizeof pts);
    pts[0] = std::nextafter(pts[0], -1e9);
    Ref path2(quad_path(f.store.store, pts));
    no(path2.get(), paint.get(), "one ulp off in the first corner");
  }
  { // a rotation of the path alone
    GSHGEOM_Map turn;
    gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, 10.0, &turn);
    double pts[8];
    for (int i = 0; i < 4; i++) {
      GSHGEOM_Point q;
      gshgeom_map_apply_point(&turn, P(want[2 * i], want[2 * i + 1]), &q);
      pts[2 * i] = q.x;
      pts[2 * i + 1] = q.y;
    }
    Ref path(quad_path(f.store.store, pts));
    no(path.get(), paint.get(), "the path turned and the paint not");
  }
  { // a path that is not a quadrilateral of four lines closed once
    Ref open(quad_path(f.store.store, want, false));
    no(open.get(), paint.get(), "open");
    Ref triangle(quad_path(f.store.store, want, true, 3));
    no(triangle.get(), paint.get(), "three points");
    Ref square(intern_square(f.store.store));
    no(square.get(), paint.get(), "another square");
    GSHGEOM_Path * hex = new_path();
    gshgeom_path_move_to(hex, P(want[0], want[1]));
    for (int i = 1; i < 4; i++) {
      gshgeom_path_line_to(hex, P(want[2 * i], want[2 * i + 1]));
    }
    gshgeom_path_line_to(hex, P(want[0], want[1]));
    gshgeom_path_close(hex);
    Ref five(intern(f.store.store, hex));
    gshgeom_path_destroy(hex);
    no(five.get(), paint.get(), "a path that comes back to its start before it closes");
    GSHGEOM_Path * curve = new_path();
    gshgeom_path_move_to(curve, P(want[0], want[1]));
    gshgeom_path_line_to(curve, P(want[2], want[3]));
    gshgeom_path_quadratic_to(curve, P(want[4], want[5]), P(want[4], want[5]));
    gshgeom_path_line_to(curve, P(want[6], want[7]));
    gshgeom_path_close(curve);
    Ref curved(intern(f.store.store, curve));
    gshgeom_path_destroy(curve);
    no(curved.get(), paint.get(), "a curve for one side");
  }
  { // an edge mode that is not PAD, in either axis
    for (int axis = 0; axis < 2; axis++) {
      for (uint32_t mode : {(uint32_t)GSHSCENE_IMAGE_EXTEND_REPEAT, (uint32_t)GSHSCENE_IMAGE_EXTEND_REFLECT,
               (uint32_t)GSHSCENE_IMAGE_EXTEND_NONE}) {
        GSHSCENE_Image_Paint q = p;
        (axis ? q.extend_y : q.extend_x) = mode;
        Ref other;
        ASSERT_EQ(intern_paint(f.store.store, q, &other), GSHSCENE_OK);
        Ref its_rect;
        ASSERT_EQ(gshscene_image_paint_rect_path(f.store.store, other.get(), &its_rect.resource), GSHSCENE_OK);
        no(its_rect.get(), other.get(), "an edge mode that is not PAD");
      }
    }
  }
  { // a window that is not the one the path was made for
    GSHSCENE_Image_Paint q = p;
    q.window[2] = 59;
    Ref other;
    ASSERT_EQ(intern_paint(f.store.store, q, &other), GSHSCENE_OK);
    no(rect.get(), other.get(), "the path of another window");
  }
  { // a stroke, a glyph run, a clip are not a drawn image
    Unit s(f.store.store, rect.get(), nullptr, paint.get(), 1.0, true);
    EXPECT_EQ(gshscene_image_fill_is_draw(s.item, nullptr), 0) << "a stroke";
    EXPECT_EQ(gshscene_image_fill_is_draw(nullptr, nullptr), 0);
    ListHolder list(f.store.store);
    GSHSCENE_Paint plain = solid(0, 0, 0, 1);
    ASSERT_EQ(gshscene_list_fill(list.list, rect.get(), nullptr, GSHGEOM_FILL_NONZERO, &plain, 0, 0.0), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_close(list.list), GSHSCENE_OK);
    const GSHSCENE_Item * item = nullptr;
    ASSERT_EQ(gshscene_list_item(list.list, 0, &item), GSHSCENE_OK);
    EXPECT_EQ(gshscene_image_fill_is_draw(item, nullptr), 0) << "a solid fill";
  }
}

TEST(ImagePaintDraw, TheHelperRefusesWhatIsNotAnImagePaintOfTheStoreAndInternsOnePathOnce) {
  Fixture f;
  Ref paint;
  ASSERT_EQ(intern_paint(f.store.store, paint_of(f.color.get()), &paint), GSHSCENE_OK);
  const GSHSCENE_Resource * out = nullptr;
  EXPECT_EQ(gshscene_image_paint_rect_path(nullptr, paint.get(), &out), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_image_paint_rect_path(f.store.store, nullptr, &out), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_image_paint_rect_path(f.store.store, f.color.get(), &out), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_image_paint_rect_path(f.other.store, paint.get(), &out), GSHSCENE_ERR_INVALID) << "another store's paint";
  EXPECT_EQ(gshscene_image_paint_rect_path(f.store.store, paint.get(), nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(out, nullptr);
  Ref a, b;
  ASSERT_EQ(gshscene_image_paint_rect_path(f.store.store, paint.get(), &a.resource), GSHSCENE_OK);
  ASSERT_EQ(gshscene_image_paint_rect_path(f.store.store, paint.get(), &b.resource), GSHSCENE_OK);
  EXPECT_EQ(a.get(), b.get());
  EXPECT_EQ(gshscene_resource_kind(a.get()), GSHSCENE_RESOURCE_PATH);
  // A map the corners of which leave the double range is a limit, not a wrong path.
  GSHSCENE_Image_Paint huge = paint_of(f.color.get());
  huge.map = matrix_map(0x1p64, 0, 0, 0x1p64, 0x1p64, 0x1p64);
  Ref big;
  ASSERT_EQ(intern_paint(f.store.store, huge, &big), GSHSCENE_OK);
  Ref path;
  GSHSCENE_Result r = gshscene_image_paint_rect_path(f.store.store, big.get(), &path.resource);
  EXPECT_TRUE(r == GSHSCENE_OK || r == GSHSCENE_ERR_LIMIT);
}

/* --- IP8: which filter an item uses ------------------------------------------------------------------------------- */

TEST(ImagePaintFilter, MagnifyOnlyWhenTheComposedMapStretchesEveryDirectionStrictly) {
  Fixture f;
  Ref square(intern_square(f.store.store));
  GSHSCENE_Image_Paint p = paint_of(f.color.get());
  p.magnify = GSHSCENE_IMAGE_FILTER_CUBIC;
  p.minify = GSHSCENE_IMAGE_FILTER_AREA;
  const double c30 = std::cos(M_PI / 6), s30 = std::sin(M_PI / 6);
  struct Case {
    const char * name;
    GSHGEOM_Map map;
    bool magnify;
  };
  const Case cases[] = {{"scale 7", matrix_map(7, 0, 0, 7), true}, {"scale 0.3", matrix_map(0.3, 0, 0, 0.3), false},
      {"unit", matrix_map(1, 0, 0, 1), false}, {"7 by 0.3", matrix_map(7, 0, 0, 0.3), false},
      {"30 degrees at 2", matrix_map(2 * c30, -2 * s30, 2 * s30, 2 * c30), true},
      {"30 degrees at 0.9", matrix_map(0.9 * c30, -0.9 * s30, 0.9 * s30, 0.9 * c30), false},
      {"a shear", matrix_map(1, 1, 0, 1), false}, {"a mirror at 2", matrix_map(-2, 0, 0, 2), true},
      {"determinant 1, smallest 0.5", matrix_map(2, 0, 0, 0.5), false}, {"translation only", matrix_map(1, 0, 0, 1, 40, 40), false}};
  for (const Case & c : cases) {
    p.map = c.map;
    Ref paint;
    ASSERT_EQ(intern_paint(f.store.store, p, &paint), GSHSCENE_OK) << c.name;
    Unit u(f.store.store, square.get(), nullptr, paint.get());
    uint32_t filter = 99;
    ASSERT_EQ(gshscene_image_filter_of(u.item, nullptr, 0, nullptr, &filter), GSHSCENE_OK) << c.name;
    EXPECT_EQ(filter, c.magnify ? (uint32_t)GSHSCENE_IMAGE_FILTER_CUBIC : (uint32_t)GSHSCENE_IMAGE_FILTER_AREA) << c.name;
  }
}

TEST(ImagePaintFilter, TheWholeChainDecidesAndNotThePaintsMapAlone) {
  Fixture f;
  Ref square(intern_square(f.store.store));
  GSHSCENE_Image_Paint p = paint_of(f.color.get());
  p.magnify = GSHSCENE_IMAGE_FILTER_NEAREST;
  p.minify = GSHSCENE_IMAGE_FILTER_LINEAR;
  p.map = matrix_map(20, 0, 0, 20);
  Ref paint;
  ASSERT_EQ(intern_paint(f.store.store, p, &paint), GSHSCENE_OK);
  GSHGEOM_Map shrink = matrix_map(0.1, 0, 0, 0.1);
  Unit u(f.store.store, square.get(), &shrink, paint.get());
  uint32_t filter = 99;
  ASSERT_EQ(gshscene_image_filter_of(u.item, nullptr, 0, nullptr, &filter), GSHSCENE_OK);
  EXPECT_EQ(filter, (uint32_t)GSHSCENE_IMAGE_FILTER_NEAREST) << "20 * 0.1 = 2: stretched";
  GSHGEOM_Map half = matrix_map(0.5, 0, 0, 0.5);
  ASSERT_EQ(gshscene_image_filter_of(u.item, &half, 1, nullptr, &filter), GSHSCENE_OK);
  EXPECT_EQ(filter, (uint32_t)GSHSCENE_IMAGE_FILTER_LINEAR) << "an instance halves it to 1: not stretched";
  GSHGEOM_Map two = matrix_map(2, 0, 0, 2);
  ASSERT_EQ(gshscene_image_filter_of(u.item, &half, 1, &two, &filter), GSHSCENE_OK);
  EXPECT_EQ(filter, (uint32_t)GSHSCENE_IMAGE_FILTER_NEAREST) << "the target doubles it again";
  GSHGEOM_Map zero_target;
  std::memset(&zero_target, 0, sizeof zero_target);
  ASSERT_EQ(gshscene_image_filter_of(u.item, nullptr, 0, &zero_target, &filter), GSHSCENE_OK);
  EXPECT_EQ(filter, (uint32_t)GSHSCENE_IMAGE_FILTER_NEAREST) << "a zero-filled target map is the identity";
  const GSHGEOM_Map three[3] = {half, half, half};
  ASSERT_EQ(gshscene_image_filter_of(u.item, three, 3, nullptr, &filter), GSHSCENE_OK);
  EXPECT_EQ(filter, (uint32_t)GSHSCENE_IMAGE_FILTER_LINEAR) << "2 * 0.125";
  // The chain's composition leaves the range: no one filter.
  GSHGEOM_Map big = matrix_map(0x1p60, 0, 0, 0x1p60);
  const GSHGEOM_Map bigs[3] = {big, big, big};
  EXPECT_EQ(gshscene_image_filter_of(u.item, bigs, 3, &big, &filter), GSHSCENE_ERR_LIMIT);
  // Nothing else.
  EXPECT_EQ(gshscene_image_filter_of(nullptr, nullptr, 0, nullptr, &filter), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_image_filter_of(u.item, nullptr, 0, nullptr, nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_image_filter_of(u.item, nullptr, 1, nullptr, &filter), GSHSCENE_ERR_INVALID) << "instances counted and not given";
  ListHolder list(f.store.store);
  GSHSCENE_Paint plain = solid(0, 0, 0, 1);
  ASSERT_EQ(gshscene_list_fill(list.list, square.get(), nullptr, GSHGEOM_FILL_NONZERO, &plain, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(list.list), GSHSCENE_OK);
  const GSHSCENE_Item * item = nullptr;
  ASSERT_EQ(gshscene_list_item(list.list, 0, &item), GSHSCENE_OK);
  EXPECT_EQ(gshscene_image_filter_of(item, nullptr, 0, nullptr, &filter), GSHSCENE_ERR_INVALID);
}

/* --- IP15, S21: what a sink's capabilities make of an image fill ------------------------------------------------- */

TEST(ImagePaintCaps, AnImageFillIsDrawnWhereTheSinkSaysItDrawsEverythingTheFillNeeds) {
  Fixture f;
  Ref square(intern_square(f.store.store));
  Ref paint;
  ASSERT_EQ(intern_paint(f.store.store, paint_of(f.color.get()), &paint), GSHSCENE_OK);
  GSHSCENE_Sink_Caps caps = full_caps();
  for (int stroke = 0; stroke < 2; stroke++) {
    Unit u(f.store.store, square.get(), nullptr, paint.get(), 1.0, stroke != 0);
    GSHSCENE_Disposition d = gshscene_caps_disposition(&caps, u.item, nullptr, 0, nullptr, nullptr, nullptr);
    EXPECT_EQ((int)d.entry_class, 0) << "a sink with every capability draws it";
  }
}

TEST(ImagePaintCaps, ASinkWithNoImageCapabilityRefusesEveryImageFillNamingWhatIsMissing) {
  Fixture f;
  Ref square(intern_square(f.store.store));
  GSHSCENE_Image_Paint p = paint_of(f.color.get());
  p.extend_x = GSHSCENE_IMAGE_EXTEND_REPEAT;
  p.extend_y = GSHSCENE_IMAGE_EXTEND_REFLECT;
  Ref paint;
  ASSERT_EQ(intern_paint(f.store.store, p, &paint), GSHSCENE_OK);
  GSHSCENE_Sink_Caps caps = full_caps();
  caps.image_filters = 0;
  caps.image_extends = 0;
  caps.image_formats = 0;
  caps.flags &= ~GSHSCENE_CAP_IMAGE_FILTER_SPACES;
  Unit u(f.store.store, square.get(), nullptr, paint.get());
  GSHSCENE_Disposition d = gshscene_caps_disposition(&caps, u.item, nullptr, 0, nullptr, nullptr, nullptr);
  EXPECT_EQ(d.entry_class, GSHSCENE_ENTRY_REFUSED);
  EXPECT_EQ(d.reason, GSHSCENE_REASON_IMAGE);
  uint32_t required = gshscene_image_required(u.item);
  EXPECT_EQ(d.detail, required);
  EXPECT_EQ(required & 0xFu, (1u << GSHSCENE_IMAGE_EXTEND_REPEAT) | (1u << GSHSCENE_IMAGE_EXTEND_REFLECT));
  EXPECT_EQ(required >> GSHSCENE_IMAGE_REQUIRED_FORMAT_SHIFT, GSHSCENE_IMAGE_FORMAT_UNORM8 | GSHSCENE_IMAGE_FORMAT_RGBA);
}

TEST(ImagePaintCaps, ARefusalNamesExactlyTheEdgeModesAndFormatsTheSinkLacks) {
  Fixture f;
  Ref square(intern_square(f.store.store));
  GSHSCENE_Image_Paint p = paint_of(f.color.get());
  p.extend_x = GSHSCENE_IMAGE_EXTEND_REFLECT;
  p.extend_y = GSHSCENE_IMAGE_EXTEND_NONE;
  Ref paint;
  ASSERT_EQ(intern_paint(f.store.store, p, &paint), GSHSCENE_OK);
  Unit u(f.store.store, square.get(), nullptr, paint.get());
  GSHSCENE_Sink_Caps caps = full_caps();
  caps.image_extends &= ~(1u << GSHSCENE_IMAGE_EXTEND_REFLECT);
  GSHSCENE_Disposition d = gshscene_caps_disposition(&caps, u.item, nullptr, 0, nullptr, nullptr, nullptr);
  EXPECT_EQ(d.reason, GSHSCENE_REASON_IMAGE);
  EXPECT_EQ(d.detail, 1u << GSHSCENE_IMAGE_EXTEND_REFLECT) << "only the mode it lacks";
  caps = full_caps();
  caps.image_extends &= ~(1u << GSHSCENE_IMAGE_EXTEND_NONE);
  d = gshscene_caps_disposition(&caps, u.item, nullptr, 0, nullptr, nullptr, nullptr);
  EXPECT_EQ(d.detail, 1u << GSHSCENE_IMAGE_EXTEND_NONE);
  caps = full_caps();
  caps.image_formats &= ~GSHSCENE_IMAGE_FORMAT_RGBA;
  d = gshscene_caps_disposition(&caps, u.item, nullptr, 0, nullptr, nullptr, nullptr);
  EXPECT_EQ(d.reason, GSHSCENE_REASON_IMAGE);
  EXPECT_EQ(d.detail, GSHSCENE_IMAGE_FORMAT_RGBA << GSHSCENE_IMAGE_REQUIRED_FORMAT_SHIFT);
  caps = full_caps();
  caps.image_formats &= ~GSHSCENE_IMAGE_FORMAT_UNORM8;
  d = gshscene_caps_disposition(&caps, u.item, nullptr, 0, nullptr, nullptr, nullptr);
  EXPECT_EQ(d.detail, GSHSCENE_IMAGE_FORMAT_UNORM8 << GSHSCENE_IMAGE_REQUIRED_FORMAT_SHIFT);
  caps.image_formats &= ~GSHSCENE_IMAGE_FORMAT_RGBA;
  d = gshscene_caps_disposition(&caps, u.item, nullptr, 0, nullptr, nullptr, nullptr);
  EXPECT_EQ(d.detail, (GSHSCENE_IMAGE_FORMAT_UNORM8 | GSHSCENE_IMAGE_FORMAT_RGBA) << GSHSCENE_IMAGE_REQUIRED_FORMAT_SHIFT);
}

TEST(ImagePaintCaps, EachSampleTypeLayoutAndTheStencilMarkIsItsOwnFormatBit) {
  StoreHolder store;
  Ref square(intern_square(store.store));
  struct Case {
    Fmt f;
    bool stencil;
    uint32_t want;
  };
  const Case cases[] = {{kGray8, false, GSHSCENE_IMAGE_FORMAT_UNORM8 | GSHSCENE_IMAGE_FORMAT_GRAY},
      {kGray12, false, GSHSCENE_IMAGE_FORMAT_UNORM12 | GSHSCENE_IMAGE_FORMAT_GRAY},
      {kGray16, false, GSHSCENE_IMAGE_FORMAT_UNORM16 | GSHSCENE_IMAGE_FORMAT_GRAY},
      {kRgb8, false, GSHSCENE_IMAGE_FORMAT_UNORM8 | GSHSCENE_IMAGE_FORMAT_RGB},
      {kRgbF, false, GSHSCENE_IMAGE_FORMAT_FLOAT32 | GSHSCENE_IMAGE_FORMAT_RGB},
      {kRgba8P, false, GSHSCENE_IMAGE_FORMAT_UNORM8 | GSHSCENE_IMAGE_FORMAT_RGBA},
      {kGrayF, false, GSHSCENE_IMAGE_FORMAT_FLOAT32 | GSHSCENE_IMAGE_FORMAT_GRAY},
      {kGray8, true, GSHSCENE_IMAGE_FORMAT_UNORM8 | GSHSCENE_IMAGE_FORMAT_GRAY | GSHSCENE_IMAGE_FORMAT_ALPHA_ONLY}};
  for (const Case & c : cases) {
    RasterHolder raster(make_raster(5, 5, c.f));
    Ref image(copy_in(store.store, raster.get(), params(GSHSCENE_Color_Space{}, c.stencil ? GSHSCENE_IMAGE_ALPHA_ONLY : 0)));
    GSHSCENE_Image_Paint p = paint_of(image.get());
    GSHSCENE_Paint tint = solid_tint();
    p.tint = c.stencil ? &tint : nullptr;
    Ref paint;
    ASSERT_EQ(intern_paint(store.store, p, &paint), GSHSCENE_OK);
    Unit u(store.store, square.get(), nullptr, paint.get());
    EXPECT_EQ(gshscene_image_required(u.item) >> GSHSCENE_IMAGE_REQUIRED_FORMAT_SHIFT, c.want)
        << c.f.channels << " channels, " << c.f.bits << " bits" << (c.stencil ? ", stencil" : "");
    EXPECT_EQ(gshscene_image_required(u.item) & 0xFu, 1u << GSHSCENE_IMAGE_EXTEND_PAD);
  }
  EXPECT_EQ(gshscene_image_required(nullptr), 0u);
}

TEST(ImagePaintCaps, AMissingFilterAndAMissingFilterSpaceAreApproximatedInThatOrder) {
  Fixture f;
  Ref square(intern_square(f.store.store));
  GSHSCENE_Image_Paint p = paint_of(f.color.get());
  p.magnify = GSHSCENE_IMAGE_FILTER_CUBIC;
  p.minify = GSHSCENE_IMAGE_FILTER_AREA;
  p.filter_space = linear_srgb_space();
  p.map = matrix_map(1, 0, 0, 1);
  Ref paint;
  ASSERT_EQ(intern_paint(f.store.store, p, &paint), GSHSCENE_OK);
  Unit u(f.store.store, square.get(), nullptr, paint.get());
  // The unit map minifies: AREA is the filter used.
  GSHSCENE_Sink_Caps caps = full_caps();
  caps.image_filters &= ~(1u << GSHSCENE_IMAGE_FILTER_CUBIC);
  GSHSCENE_Disposition d = gshscene_caps_disposition(&caps, u.item, nullptr, 0, nullptr, nullptr, nullptr);
  EXPECT_EQ((int)d.entry_class, 0) << "the filter the map calls for is AREA: CUBIC is not used here";
  caps.image_filters &= ~(1u << GSHSCENE_IMAGE_FILTER_AREA);
  d = gshscene_caps_disposition(&caps, u.item, nullptr, 0, nullptr, nullptr, nullptr);
  EXPECT_EQ(d.entry_class, GSHSCENE_ENTRY_APPROXIMATED);
  EXPECT_EQ(d.reason, GSHSCENE_REASON_IMAGE_FILTER);
  // The filter first, then the space.
  caps.flags &= ~GSHSCENE_CAP_IMAGE_FILTER_SPACES;
  d = gshscene_caps_disposition(&caps, u.item, nullptr, 0, nullptr, nullptr, nullptr);
  EXPECT_EQ(d.reason, GSHSCENE_REASON_IMAGE_FILTER) << "both wrong: the filter is named";
  caps = full_caps();
  caps.flags &= ~GSHSCENE_CAP_IMAGE_FILTER_SPACES;
  d = gshscene_caps_disposition(&caps, u.item, nullptr, 0, nullptr, nullptr, nullptr);
  EXPECT_EQ(d.entry_class, GSHSCENE_ENTRY_APPROXIMATED);
  EXPECT_EQ(d.reason, GSHSCENE_REASON_IMAGE_FILTER_SPACE);
  // A stated space that is the target's is nothing to approximate.
  GSHSCENE_Color_Space target = linear_srgb_space();
  d = gshscene_caps_disposition(&caps, u.item, nullptr, 0, nullptr, &target, nullptr);
  EXPECT_EQ((int)d.entry_class, 0);
  // The image's own space (zero) needs no capability.
  GSHSCENE_Image_Paint own = paint_of(f.color.get());
  Ref own_paint;
  ASSERT_EQ(intern_paint(f.store.store, own, &own_paint), GSHSCENE_OK);
  Unit uo(f.store.store, square.get(), nullptr, own_paint.get());
  d = gshscene_caps_disposition(&caps, uo.item, nullptr, 0, nullptr, nullptr, nullptr);
  EXPECT_EQ((int)d.entry_class, 0);
  // With a magnifying map the other filter is the one used.
  GSHGEOM_Map big = matrix_map(3, 0, 0, 3);
  Unit magnified(f.store.store, square.get(), &big, paint.get());
  caps = full_caps();
  caps.image_filters &= ~(1u << GSHSCENE_IMAGE_FILTER_CUBIC);
  d = gshscene_caps_disposition(&caps, magnified.item, nullptr, 0, nullptr, nullptr, nullptr);
  EXPECT_EQ(d.reason, GSHSCENE_REASON_IMAGE_FILTER);
  // A chain that cannot be composed has no one filter: either missing is enough.
  GSHGEOM_Map huge = matrix_map(0x1p60, 0, 0, 0x1p60);
  const GSHGEOM_Map instances[2] = {huge, huge};
  d = gshscene_caps_disposition(&caps, u.item, instances, 2, &huge, nullptr, nullptr);
  EXPECT_EQ(d.reason, GSHSCENE_REASON_IMAGE_FILTER);
}

TEST(ImagePaintCaps, TheOrderOfThePlanningPassIsRefusalsBeforeApproximations) {
  Fixture f;
  Ref square(intern_square(f.store.store));
  GSHSCENE_Image_Paint p = paint_of(f.color.get());
  p.extend_x = GSHSCENE_IMAGE_EXTEND_REFLECT;
  p.filter_space = linear_srgb_space();
  p.magnify = p.minify = GSHSCENE_IMAGE_FILTER_CUBIC;
  Ref paint;
  ASSERT_EQ(intern_paint(f.store.store, p, &paint), GSHSCENE_OK);
  Unit u(f.store.store, square.get(), nullptr, paint.get());
  GSHSCENE_Sink_Caps caps = full_caps();
  caps.image_extends &= ~(1u << GSHSCENE_IMAGE_EXTEND_REFLECT);
  caps.image_filters = 0;
  caps.flags &= ~GSHSCENE_CAP_IMAGE_FILTER_SPACES;
  GSHSCENE_Disposition d = gshscene_caps_disposition(&caps, u.item, nullptr, 0, nullptr, nullptr, nullptr);
  EXPECT_EQ(d.entry_class, GSHSCENE_ENTRY_REFUSED) << "wrong in three ways: the refusal first";
  EXPECT_EQ(d.reason, GSHSCENE_REASON_IMAGE);
  // A sink that does not draw fills at all is refused for that before anything about the image.
  GSHSCENE_Sink_Caps none = full_caps();
  none.item_kinds = 0;
  d = gshscene_caps_disposition(&none, u.item, nullptr, 0, nullptr, nullptr, nullptr);
  EXPECT_EQ(d.reason, GSHSCENE_REASON_ITEM_KIND);
}

TEST(ImagePaintCaps, APaletteTintNeedsTheEntryInThePaletteInForce) {
  Fixture f;
  Ref square(intern_square(f.store.store));
  GSHSCENE_Paint tint = kind_tint(GSHSCENE_PAINT_PALETTE, 1);
  GSHSCENE_Image_Paint p = paint_of(f.stencil.get());
  p.tint = &tint;
  Ref paint;
  ASSERT_EQ(intern_paint(f.store.store, p, &paint), GSHSCENE_OK);
  Unit u(f.store.store, square.get(), nullptr, paint.get());
  GSHSCENE_Sink_Caps caps = full_caps();
  GSHSCENE_Disposition d = gshscene_caps_disposition(&caps, u.item, nullptr, 0, nullptr, nullptr, nullptr);
  EXPECT_EQ(d.reason, GSHSCENE_REASON_PALETTE_ENTRY) << "no palette at all";
  const GSHSCENE_Color one[1] = {{{1, 0, 0, 1}, {}}};
  const GSHSCENE_Color two[2] = {{{1, 0, 0, 1}, {}}, {{0, 1, 0, 1}, {}}};
  const GSHSCENE_Resource * pal = nullptr;
  ASSERT_EQ(gshscene_store_intern_palette(f.store.store, 1, one, &pal), GSHSCENE_OK);
  Ref short_palette(pal);
  d = gshscene_caps_disposition(&caps, u.item, nullptr, 0, nullptr, nullptr, short_palette.get());
  EXPECT_EQ(d.reason, GSHSCENE_REASON_PALETTE_ENTRY) << "entry 1 of a palette of one";
  ASSERT_EQ(gshscene_store_intern_palette(f.store.store, 2, two, &pal), GSHSCENE_OK);
  Ref long_palette(pal);
  d = gshscene_caps_disposition(&caps, u.item, nullptr, 0, nullptr, nullptr, long_palette.get());
  EXPECT_EQ((int)d.entry_class, 0);
  // The image refusal comes before the palette one.
  caps.image_extends = 0;
  d = gshscene_caps_disposition(&caps, u.item, nullptr, 0, nullptr, nullptr, nullptr);
  EXPECT_EQ(d.reason, GSHSCENE_REASON_IMAGE);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

// --- gaps the plants found ------------------------------------------------------------------------------------------

TEST(ImageDecode, ATakenRasterIsReadAtItsOwnStride) {
  StoreHolder store;
  GIMG_Raster * raster = make_raster(9, 9, kRgb8, 7, 0x11); // rows padded: the stride is not the row's bytes
  ASSERT_NE(raster, nullptr);
  unsigned char * px = (unsigned char *)gimg_raster_pixels(raster);
  size_t stride = gimg_raster_stride_bytes(raster);
  ASSERT_GT(stride, (size_t)27);
  px[stride * 4 + 2 * 3 + 0] = 255;
  px[stride * 4 + 2 * 3 + 1] = 0;
  px[stride * 4 + 2 * 3 + 2] = 51;
  const GSHSCENE_Resource * r = nullptr;
  GSHSCENE_Image_Params prm = params();
  ASSERT_EQ(gshscene_store_intern_image_take(store.store, raster, &prm, &r), GSHSCENE_OK);
  Ref image(r);
  double out[4];
  ASSERT_EQ(gshscene_image_decode(image.get(), 2, 4, out), GSHSCENE_OK);
  EXPECT_EQ(out[0], 1.0);
  EXPECT_EQ(out[1], 0.0);
  EXPECT_EQ(out[2], 51.0 / 255.0);
  EXPECT_EQ(out[3], 1.0);
}

TEST(ImagePaintDraw, OnlyOneClosedSubpathOfFourLinesInTwoDimensionsIsADrawnImage) {
  Fixture f;
  GSHSCENE_Image_Paint p = paint_of(f.color.get());
  p.map = matrix_map(2, 0, 0, 3, 5, 7);
  Ref paint;
  ASSERT_EQ(intern_paint(f.store.store, p, &paint), GSHSCENE_OK);
  Ref rect;
  ASSERT_EQ(gshscene_image_paint_rect_path(f.store.store, paint.get(), &rect.resource), GSHSCENE_OK);
  double q[8];
  {
    Unit u(f.store.store, rect.get(), nullptr, paint.get());
    ASSERT_EQ(gshscene_image_fill_is_draw(u.item, q), 1);
  }
  for (int dim = 2; dim <= 3; dim++) {
    for (int extra = 0; extra < 2; extra++) {
      GSHGEOM_Path * path = nullptr;
      ASSERT_EQ(gshgeom_path_create(nullptr, nullptr, dim, &path), GSHGEOM_OK);
      ASSERT_EQ(gshgeom_path_move_to(path, GSHGEOM_Point{q[0], q[1], 0}), GSHGEOM_OK);
      for (int i = 1; i < 4; i++) {
        ASSERT_EQ(gshgeom_path_line_to(path, GSHGEOM_Point{q[2 * i], q[2 * i + 1], 0}), GSHGEOM_OK);
      }
      ASSERT_EQ(gshgeom_path_close(path), GSHGEOM_OK);
      if (extra) {
        ASSERT_EQ(gshgeom_path_move_to(path, GSHGEOM_Point{q[0], q[1], 0}), GSHGEOM_OK); // a sixth element
      }
      const GSHSCENE_Resource * r = nullptr;
      GSHSCENE_Result made = gshscene_store_intern_path(f.store.store, path, &r);
      gshgeom_path_destroy(path);
      ASSERT_EQ(made, GSHSCENE_OK);
      Ref res(r);
      GSHSCENE_Paint pt = solid(0, 0, 0, 1);
      pt.kind = GSHSCENE_PAINT_IMAGE;
      pt.resource = paint.get();
      ListHolder list(f.store.store);
      GSHSCENE_Result filled = gshscene_list_fill(list.list, res.get(), nullptr, GSHGEOM_FILL_NONZERO, &pt, 0, 0.0);
      if (filled != GSHSCENE_OK) {
        continue; // the list refuses this path (a 3D one): there is no item to recognize
      }
      ASSERT_EQ(gshscene_list_close(list.list), GSHSCENE_OK);
      const GSHSCENE_Item * item = nullptr;
      ASSERT_EQ(gshscene_list_item(list.list, 0, &item), GSHSCENE_OK);
      EXPECT_EQ(gshscene_image_fill_is_draw(item, nullptr), dim == 2 && !extra ? 1 : 0) << "dim " << dim << " extra " << extra;
    }
  }
}

TEST(ImagePaintCaps, AStrokeWithAnImagePaintNeedsWhatAFillOfItDoes) {
  Fixture f;
  Ref square(intern_square(f.store.store));
  GSHSCENE_Image_Paint p = paint_of(f.color.get());
  p.extend_x = GSHSCENE_IMAGE_EXTEND_REFLECT;
  Ref paint;
  ASSERT_EQ(intern_paint(f.store.store, p, &paint), GSHSCENE_OK);
  Unit fill(f.store.store, square.get(), nullptr, paint.get());
  Unit stroke(f.store.store, square.get(), nullptr, paint.get(), 1.0, true);
  EXPECT_NE(gshscene_image_required(fill.item), 0u);
  EXPECT_EQ(gshscene_image_required(stroke.item), gshscene_image_required(fill.item));
}

TEST(ImagePaintFilter, TheTargetIsComposedAfterTheChainAndNotBefore) {
  Fixture f;
  Ref square(intern_square(f.store.store));
  GSHSCENE_Image_Paint p = paint_of(f.color.get());
  p.magnify = GSHSCENE_IMAGE_FILTER_NEAREST;
  p.minify = GSHSCENE_IMAGE_FILTER_LINEAR;
  Ref paint;
  ASSERT_EQ(intern_paint(f.store.store, p, &paint), GSHSCENE_OK);
  GSHGEOM_Map item_map = matrix_map(10, 0, 0, 1.5);
  Unit u(f.store.store, square.get(), &item_map, paint.get());
  // A shear in the target: target after item stretches the short direction less than item after target would.
  int discriminated = 0;
  for (int upper = 0; upper < 2; upper++) {
    GSHGEOM_Map shear = upper ? matrix_map(1, 3, 0, 1) : matrix_map(1, 0, 3, 1);
    GSHGEOM_Map chain[2] = {item_map, p.map};
    GSHGEOM_Map right, wrong;
    ASSERT_EQ(gshgeom_map_compose_chain(chain, 2, &shear, &right), GSHGEOM_OK);
    GSHGEOM_Map inner;
    ASSERT_EQ(gshgeom_map_compose(&chain[0], &chain[1], &inner), GSHGEOM_OK);
    ASSERT_EQ(gshgeom_map_compose(&inner, &shear, &wrong), GSHGEOM_OK);
    int a = gshgeom_map_stretches_every_direction(&right);
    int b = gshgeom_map_stretches_every_direction(&wrong);
    uint32_t filter = 99;
    ASSERT_EQ(gshscene_image_filter_of(u.item, nullptr, 0, &shear, &filter), GSHSCENE_OK);
    EXPECT_EQ(filter, a ? (uint32_t)GSHSCENE_IMAGE_FILTER_NEAREST : (uint32_t)GSHSCENE_IMAGE_FILTER_LINEAR);
    discriminated += a != b;
  }
  EXPECT_GT(discriminated, 0) << "the chosen maps must tell the two orders apart";
}

TEST(ImagePaintScene, ATintThatReadsTheForegroundOrThePaletteDamagesOnlyItsShapes) {
  SceneHolder sc;
  GSHSCENE_Store * store = sc.store();
  Ref sq(intern_square(store, 0.0));
  Ref sq2(intern_square(store, 40.0));
  Ref sq3(intern_square(store, 80.0));
  RasterHolder gray(make_raster(8, 8, kGray8));
  RasterHolder rgba(make_raster(8, 8, kRgba8));
  Ref stencil(copy_in(store, gray.get(), params(GSHSCENE_Color_Space{}, GSHSCENE_IMAGE_ALPHA_ONLY)));
  Ref color(copy_in(store, rgba.get(), params()));
  GSHSCENE_Paint fg_t = kind_tint(GSHSCENE_PAINT_FOREGROUND, 0, 1.0);
  GSHSCENE_Paint pal_t = kind_tint(GSHSCENE_PAINT_PALETTE, 1, 1.0);
  GSHSCENE_Image_Paint a = paint_of(stencil.get());
  a.tint = &fg_t;
  GSHSCENE_Image_Paint b = paint_of(stencil.get());
  b.tint = &pal_t;
  GSHSCENE_Image_Paint c = paint_of(color.get());
  Ref pa, pb, pc;
  ASSERT_EQ(intern_paint(store, a, &pa), GSHSCENE_OK);
  ASSERT_EQ(intern_paint(store, b, &pb), GSHSCENE_OK);
  ASSERT_EQ(intern_paint(store, c, &pc), GSHSCENE_OK);
  VersionRef v1;
  {
    EditHolder e(sc);
    const GSHSCENE_Resource * paints[3] = {pa.get(), pb.get(), pc.get()};
    const GSHSCENE_Resource * shapes[3] = {sq.get(), sq2.get(), sq3.get()};
    for (int i = 0; i < 3; i++) {
      GSHSCENE_Node n = shape_node(shapes[i]);
      n.paint = solid(0, 0, 0, 1);
      n.paint.kind = GSHSCENE_PAINT_IMAGE;
      n.paint.resource = paints[i];
      add(e.edit, GSHSCENE_ROOT_ID, n);
    }
    publish(sc, e, &v1);
  }
  auto damage = [&](const GSHSCENE_Version * x, const GSHSCENE_Version * y) {
    GSHSCENE_Diff * diff = nullptr;
    GSHSCENE_Damage d{};
    EXPECT_EQ(gshscene_diff(x, y, nullptr, nullptr, &diff), GSHSCENE_OK);
    EXPECT_EQ(gshscene_diff_damage(diff, &d), GSHSCENE_OK);
    gshscene_diff_destroy(diff);
    return d;
  };
  VersionRef v2;
  {
    EditHolder e(sc);
    GSHSCENE_Color red = {{1, 0, 0, 1}, {}};
    ASSERT_EQ(gshscene_edit_set_foreground(e.edit, &red), GSHSCENE_OK);
    publish(sc, e, &v2);
  }
  GSHSCENE_Damage fg = damage(v1.get(), v2.get());
  EXPECT_EQ(fg.empty, 0);
  EXPECT_LT(fg.rect.x1, 40) << "only the shape whose tint is the foreground";
  const GSHSCENE_Color entries[2] = {{{0, 0, 0, 1}, {}}, {{0, 1, 0, 1}, {}}};
  const GSHSCENE_Resource * pal = nullptr;
  ASSERT_EQ(gshscene_store_intern_palette(store, 2, entries, &pal), GSHSCENE_OK);
  VersionRef v3;
  {
    EditHolder e(sc);
    ASSERT_EQ(gshscene_edit_set_palette(e.edit, pal), GSHSCENE_OK);
    publish(sc, e, &v3);
  }
  gshscene_resource_release(pal);
  GSHSCENE_Damage pd = damage(v2.get(), v3.get());
  EXPECT_EQ(pd.empty, 0);
  EXPECT_GE(pd.rect.x0, 30) << "only the shape whose tint is a palette entry";
  EXPECT_LT(pd.rect.x1, 80);
}
