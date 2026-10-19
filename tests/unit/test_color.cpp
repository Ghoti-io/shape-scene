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
 * Color spaces and colors (N1 to N3, N10): what is valid, the mapping from a `color`
 * description, the normative gamuts and transfer formulas against `color`'s own
 * tables and evaluator, and the foreground paint's canonical form (story 12).
 */

#include "scene_fixtures.h"

#include "ref_composite.h"

#include <ghoti.io/color/color.h>
#include <ghoti.io/color/math.h>

#include <cmath>
#include <memory>

using namespace scene;

namespace {

const double kNaN = std::nan("");
const double kInf = INFINITY;

GSHSCENE_Result check(GCOL_Primaries p, GCOL_Transfer t, double g = 0.0) {
  GSHSCENE_Color_Space s = space_of(p, t, g);
  return gshscene_color_space_check(&s);
}

GCOL_Color_Info info_of_space(GCOL_Primaries p, GCOL_Transfer t, double gamma = 0.0) {
  GCOL_Color_Info info;
  std::memset(&info, 0, sizeof info);
  gcol_color_info_set_gamut(&info, p);
  info.transfer = t;
  info.gamma_value = gamma;
  return info;
}

}  // namespace

TEST(ColorSpace, TheZeroSpaceIsTheTargetsAndNothingElseIs) {
  GSHSCENE_Color_Space zero;
  std::memset(&zero, 0, sizeof zero);
  EXPECT_EQ(gshscene_color_space_check(&zero), GSHSCENE_OK);
  EXPECT_EQ(gshscene_color_space_is_target(&zero), 1);
  EXPECT_EQ(gshscene_color_space_is_target(nullptr), 0);
  EXPECT_EQ(gshscene_color_space_check(nullptr), GSHSCENE_ERR_INVALID);
  GSHSCENE_Color_Space s = srgb_space();
  EXPECT_EQ(gshscene_color_space_is_target(&s), 0);
  GSHSCENE_Color_Space neg = zero;
  neg.gamma = -0.0;
  EXPECT_EQ(gshscene_color_space_check(&neg), GSHSCENE_ERR_INVALID) << "-0 is a second spelling of the target's space";
  EXPECT_EQ(gshscene_color_space_is_target(&neg), 0);
}

TEST(ColorSpace, TheFourGamutsAndThreeTransfersAreAccepted) {
  for (GCOL_Primaries p : {GCOL_PRIMARIES_SRGB, GCOL_PRIMARIES_ADOBE_RGB, GCOL_PRIMARIES_DISPLAY_P3, GCOL_PRIMARIES_BT2020}) {
    EXPECT_EQ(check(p, GCOL_TRANSFER_LINEAR), GSHSCENE_OK) << p;
    EXPECT_EQ(check(p, GCOL_TRANSFER_SRGB), GSHSCENE_OK) << p;
    EXPECT_EQ(check(p, GCOL_TRANSFER_GAMMA, 2.2), GSHSCENE_OK) << p;
  }
}

TEST(ColorSpace, EverythingElseIsRefusedAsStated) {
  const GSHSCENE_Result INV = GSHSCENE_ERR_INVALID, UNS = GSHSCENE_ERR_UNSUPPORTED;
  // Reserved: D50, and the curves that are not a linear, sRGB or plain exponent.
  EXPECT_EQ(check(GCOL_PRIMARIES_PROPHOTO, GCOL_TRANSFER_SRGB), UNS);
  EXPECT_EQ(check(GCOL_PRIMARIES_PROPHOTO, GCOL_TRANSFER_LINEAR), UNS);
  for (GCOL_Transfer t : {GCOL_TRANSFER_PARAMETRIC, GCOL_TRANSFER_BT1886, GCOL_TRANSFER_PQ, GCOL_TRANSFER_HLG}) {
    EXPECT_EQ(check(GCOL_PRIMARIES_SRGB, t), UNS) << t;
  }
  // A space is given whole: a gamut and no transfer, or the reverse.
  EXPECT_EQ(check(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_UNKNOWN), INV);
  EXPECT_EQ(check(GCOL_PRIMARIES_UNKNOWN, GCOL_TRANSFER_SRGB), INV);
  EXPECT_EQ(check(GCOL_PRIMARIES_UNKNOWN, GCOL_TRANSFER_UNKNOWN, 2.2), INV) << "a gamma alone";
  // The exponent: only for GAMMA, finite, and in [0.1, 10].
  EXPECT_EQ(check(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_GAMMA, 0.0), INV);
  EXPECT_EQ(check(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_GAMMA, 0.0999), INV);
  EXPECT_EQ(check(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_GAMMA, 0.1), GSHSCENE_OK);
  EXPECT_EQ(check(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_GAMMA, 10.0), GSHSCENE_OK);
  EXPECT_EQ(check(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_GAMMA, 10.001), INV);
  EXPECT_EQ(check(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_GAMMA, kNaN), INV);
  EXPECT_EQ(check(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_GAMMA, kInf), INV);
  EXPECT_EQ(check(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_GAMMA, -2.2), INV);
  EXPECT_EQ(check(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_SRGB, 2.2), INV) << "a gamma where there is none";
  EXPECT_EQ(check(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_LINEAR, 1.0), INV);
  // A reserved value with a bad exponent is invalid first (structure before reservation).
  EXPECT_EQ(check(GCOL_PRIMARIES_PROPHOTO, GCOL_TRANSFER_GAMMA, 0.0), INV);
  // Outside the enums.
  EXPECT_EQ(check((GCOL_Primaries)GCOL_PRIMARIES_COUNT, GCOL_TRANSFER_SRGB), INV);
  EXPECT_EQ(check((GCOL_Primaries)-1, GCOL_TRANSFER_SRGB), INV);
  EXPECT_EQ(check(GCOL_PRIMARIES_SRGB, (GCOL_Transfer)GCOL_TRANSFER_COUNT), INV);
  EXPECT_EQ(check(GCOL_PRIMARIES_SRGB, (GCOL_Transfer)-1), INV);
}

TEST(ColorSpace, AColorDescriptionFromColorMapsToTheSpaceItNames) {
  GSHSCENE_Color_Space out;
  struct Row {
    GCOL_Primaries gamut;
    GCOL_Transfer transfer;
    double gamma;
  };
  for (Row r : {Row{GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_SRGB, 0}, Row{GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_LINEAR, 0},
           Row{GCOL_PRIMARIES_DISPLAY_P3, GCOL_TRANSFER_GAMMA, 2.2}, Row{GCOL_PRIMARIES_ADOBE_RGB, GCOL_TRANSFER_GAMMA, 563.0 / 256.0},
           Row{GCOL_PRIMARIES_BT2020, GCOL_TRANSFER_LINEAR, 0}}) {
    GCOL_Color_Info info = info_of_space(r.gamut, r.transfer, r.gamma);
    std::memset(&out, 0xAB, sizeof out);
    ASSERT_EQ(gshscene_color_space_from_info(&info, &out), GSHSCENE_OK) << r.gamut;
    EXPECT_EQ(out.primaries, r.gamut);
    EXPECT_EQ(out.transfer, r.transfer);
    EXPECT_EQ(out.gamma, r.transfer == GCOL_TRANSFER_GAMMA ? r.gamma : 0.0);
  }
  // An sRGB description that spells its gamut with a hair of error still names sRGB (color's own tolerance).
  GCOL_Color_Info near = info_of_space(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_SRGB);
  near.gamut.red.x += 0.0004;
  ASSERT_EQ(gshscene_color_space_from_info(&near, &out), GSHSCENE_OK);
  EXPECT_EQ(out.primaries, GCOL_PRIMARIES_SRGB);
}

TEST(ColorSpace, ADescriptionThatNamesNothingTheLibraryCarriesIsRefusedAsStated) {
  GSHSCENE_Color_Space out;
  std::memset(&out, 0x5A, sizeof out);
  GSHSCENE_Color_Space before = out;
  const GSHSCENE_Result INV = GSHSCENE_ERR_INVALID, UNS = GSHSCENE_ERR_UNSUPPORTED;
  EXPECT_EQ(gshscene_color_space_from_info(nullptr, &out), INV);
  GCOL_Color_Info ok = info_of_space(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_SRGB);
  EXPECT_EQ(gshscene_color_space_from_info(&ok, nullptr), INV);
  GCOL_Color_Info unstated = ok;
  unstated.primaries_stated = false;
  EXPECT_EQ(gshscene_color_space_from_info(&unstated, &out), INV);
  GCOL_Color_Info no_transfer = ok;
  no_transfer.transfer = GCOL_TRANSFER_UNKNOWN;
  EXPECT_EQ(gshscene_color_space_from_info(&no_transfer, &out), INV);
  GCOL_Color_Info prophoto = info_of_space(GCOL_PRIMARIES_PROPHOTO, GCOL_TRANSFER_SRGB);
  EXPECT_EQ(gshscene_color_space_from_info(&prophoto, &out), UNS);
  for (GCOL_Transfer t : {GCOL_TRANSFER_PARAMETRIC, GCOL_TRANSFER_BT1886, GCOL_TRANSFER_PQ, GCOL_TRANSFER_HLG}) {
    GCOL_Color_Info other = info_of_space(GCOL_PRIMARIES_SRGB, t);
    EXPECT_EQ(gshscene_color_space_from_info(&other, &out), UNS) << t;
  }
  GCOL_Color_Info odd = ok;  // a gamut that is none of the named
  odd.gamut.red = {0.7, 0.3};
  EXPECT_EQ(gshscene_color_space_from_info(&odd, &out), UNS);
  unsigned char profile[4] = {1, 2, 3, 4};
  GCOL_Color_Info icc = ok;
  icc.icc_bytes = profile;
  icc.icc_size = sizeof profile;
  EXPECT_EQ(gshscene_color_space_from_info(&icc, &out), UNS);
  GCOL_Color_Info linked = ok;
  linked.icc_linked_path = "profile.icc";
  EXPECT_EQ(gshscene_color_space_from_info(&linked, &out), UNS);
  GCOL_Color_Info size_only = ok;
  size_only.icc_size = 1;
  EXPECT_EQ(gshscene_color_space_from_info(&size_only, &out), UNS);
  unsigned char one[1] = {0};
  GCOL_Color_Info bytes_only = ok;
  bytes_only.icc_bytes = one;
  EXPECT_EQ(gshscene_color_space_from_info(&bytes_only, &out), UNS) << "bytes with no size";
  GCOL_Color_Info stray = info_of_space(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_SRGB, 2.2);
  GSHSCENE_Color_Space got_stray;
  ASSERT_EQ(gshscene_color_space_from_info(&stray, &got_stray), GSHSCENE_OK);
  EXPECT_EQ(got_stray.gamma, 0.0) << "a gamma value on a transfer that has none is not carried";
  GCOL_Color_Info gamma = info_of_space(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_GAMMA, 0.0);
  EXPECT_EQ(gshscene_color_space_from_info(&gamma, &out), INV) << "a gamma transfer without its exponent";
  gamma.gamma_value = 11.0;
  EXPECT_EQ(gshscene_color_space_from_info(&gamma, &out), INV);
  EXPECT_EQ(std::memcmp(&out, &before, sizeof out), 0) << "written only on success";
}

TEST(ColorSpace, TheNormativeGamutsAreColorsOwnTable) {
  for (GCOL_Primaries p : {GCOL_PRIMARIES_SRGB, GCOL_PRIMARIES_ADOBE_RGB, GCOL_PRIMARIES_DISPLAY_P3, GCOL_PRIMARIES_BT2020}) {
    GCOL_Gamut g;
    ASSERT_TRUE(gcol_gamut_named(p, &g)) << p;
    refc::Chroma c[3];
    refc::gamut((int)p, c);
    EXPECT_NEAR(g.red.x, c[0].x, 1e-12) << p;
    EXPECT_NEAR(g.red.y, c[0].y, 1e-12) << p;
    EXPECT_NEAR(g.green.x, c[1].x, 1e-12) << p;
    EXPECT_NEAR(g.green.y, c[1].y, 1e-12) << p;
    EXPECT_NEAR(g.blue.x, c[2].x, 1e-12) << p;
    EXPECT_NEAR(g.blue.y, c[2].y, 1e-12) << p;
    EXPECT_NEAR(g.white.x, 0.3127, 1e-12) << p;
    EXPECT_NEAR(g.white.y, 0.3290, 1e-12) << p;
  }
}

TEST(ColorSpace, TheNormativeTransfersAreColorsOwnEvaluatorOnAGridIncludingBelowZero) {
  struct Row {
    GCOL_Transfer transfer;
    double gamma;
  };
  for (Row r : {Row{GCOL_TRANSFER_SRGB, 0}, Row{GCOL_TRANSFER_GAMMA, 2.2}, Row{GCOL_TRANSFER_GAMMA, 1.8},
           Row{GCOL_TRANSFER_GAMMA, 563.0 / 256.0}, Row{GCOL_TRANSFER_LINEAR, 0}}) {
    GCOL_Color_Info info;
    std::memset(&info, 0, sizeof info);
    info.transfer = r.transfer;
    info.gamma_value = r.gamma;
    refc::Space s = {1, (int)r.transfer, r.gamma};
    for (int n = -40; n <= 80; n++) {
      double x = n / 32.0;
      EXPECT_NEAR(refc::decode(s, x), gcol_transfer_decode(&info, x), 1e-12) << r.transfer << " decode " << x;
      EXPECT_NEAR(refc::encode(s, x), gcol_transfer_encode(&info, x), 1e-12) << r.transfer << " encode " << x;
    }
    // Encode inverts decode.
    for (int n = -20; n <= 60; n++) {
      double x = n / 32.0;
      EXPECT_NEAR(refc::encode(s, refc::decode(s, x)), x, 1e-12) << r.transfer << " " << x;
    }
  }
}

TEST(ColorSpace, TheMatricesTakeWhiteToWhiteAndAreInverted) {
  for (int p = 1; p <= 4; p++) {
    refc::Mat m, inv;
    refc::rgb_to_xyz(p, m);
    refc::inverse3(m, inv);
    const double white[3] = {0.3127 / 0.3290, 1.0, (1.0 - 0.3127 - 0.3290) / 0.3290};
    double rgb[3];
    refc::mul3(inv, white, rgb);
    for (int i = 0; i < 3; i++) {
      EXPECT_NEAR(rgb[i], 1.0, 1e-13) << p;
    }
    // The published sRGB matrix to the places it is published to.
    if (p == 1) {
      EXPECT_NEAR(m[0][0], 0.4124, 6e-4);
      EXPECT_NEAR(m[1][1], 0.7152, 6e-4);
      EXPECT_NEAR(m[2][2], 0.9505, 6e-4);
    }
  }
}

// --- colors and the foreground ---------------------------------------------------------------------------------------

TEST(Color, TheDefaultForegroundIsOpaqueBlackInTheTargetsSpace) {
  GSHSCENE_Color c;
  std::memset(&c, 0xAB, sizeof c);
  gshscene_color_init(&c);
  EXPECT_EQ(c.rgba[0], 0.0);
  EXPECT_EQ(c.rgba[1], 0.0);
  EXPECT_EQ(c.rgba[2], 0.0);
  EXPECT_EQ(c.rgba[3], 1.0);
  EXPECT_EQ(gshscene_color_space_is_target(&c.space), 1);
  EXPECT_EQ(gshscene_color_check(&c), GSHSCENE_OK);
  gshscene_color_init(nullptr);
  StoreHolder s;
  ListHolder l(s.store);
  GSHSCENE_Color got;
  ASSERT_EQ(gshscene_list_foreground(l.list, &got), GSHSCENE_OK);
  EXPECT_EQ(std::memcmp(&got, &c, sizeof c), 0) << "a new list has the default";
}

TEST(Color, ColorsAreCheckedLikePaints) {
  GSHSCENE_Color c;
  gshscene_color_init(&c);
  EXPECT_EQ(gshscene_color_check(nullptr), GSHSCENE_ERR_INVALID);
  c.rgba[0] = 2.0;
  c.rgba[1] = -0.5;
  c.rgba[2] = 1e300;
  EXPECT_EQ(gshscene_color_check(&c), GSHSCENE_OK) << "outside the gamut is allowed";
  for (int i = 0; i < 4; i++) {
    GSHSCENE_Color bad = c;
    bad.rgba[i] = kNaN;
    EXPECT_EQ(gshscene_color_check(&bad), GSHSCENE_ERR_INVALID) << i;
    bad.rgba[i] = kInf;
    EXPECT_EQ(gshscene_color_check(&bad), GSHSCENE_ERR_INVALID) << i;
  }
  c.rgba[3] = 1.5;
  EXPECT_EQ(gshscene_color_check(&c), GSHSCENE_ERR_INVALID);
  c.rgba[3] = -0.1;
  EXPECT_EQ(gshscene_color_check(&c), GSHSCENE_ERR_INVALID);
  c.rgba[3] = 0.5;
  c.space = space_of(GCOL_PRIMARIES_PROPHOTO, GCOL_TRANSFER_SRGB);
  EXPECT_EQ(gshscene_color_check(&c), GSHSCENE_ERR_UNSUPPORTED);
  c.space = space_of(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_UNKNOWN);
  EXPECT_EQ(gshscene_color_check(&c), GSHSCENE_ERR_INVALID);
}

TEST(Color, AListsForegroundIsSetBeforeCloseAndRefusedWhenInvalidWithoutChange) {
  StoreHolder s;
  ListHolder l(s.store);
  GSHSCENE_Color fg = {{0.25, 0.5, 0.75, 0.5}, srgb_space()};
  ASSERT_EQ(gshscene_list_set_foreground(l.list, &fg), GSHSCENE_OK);
  GSHSCENE_Color got;
  ASSERT_EQ(gshscene_list_foreground(l.list, &got), GSHSCENE_OK);
  EXPECT_EQ(std::memcmp(&got, &fg, sizeof fg), 0);
  GSHSCENE_Color bad = fg;
  bad.rgba[3] = 2.0;
  EXPECT_EQ(gshscene_list_set_foreground(l.list, &bad), GSHSCENE_ERR_INVALID);
  bad = fg;
  bad.space = space_of(GCOL_PRIMARIES_PROPHOTO, GCOL_TRANSFER_SRGB);
  EXPECT_EQ(gshscene_list_set_foreground(l.list, &bad), GSHSCENE_ERR_UNSUPPORTED);
  ASSERT_EQ(gshscene_list_foreground(l.list, &got), GSHSCENE_OK);
  EXPECT_EQ(std::memcmp(&got, &fg, sizeof fg), 0) << "a refused call changes nothing";
  uint64_t with = gshscene_list_hash(l.list);
  ASSERT_EQ(gshscene_list_set_foreground(l.list, nullptr), GSHSCENE_OK) << "NULL is the default";
  EXPECT_NE(gshscene_list_hash(l.list), with);
  {
    GSHSCENE_Color def, now;
    gshscene_color_init(&def);
    ASSERT_EQ(gshscene_list_foreground(l.list, &now), GSHSCENE_OK);
    EXPECT_EQ(std::memcmp(&now, &def, sizeof now), 0);
  }
  ASSERT_EQ(gshscene_list_set_foreground(l.list, &fg), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_hash(l.list), with);
  ASSERT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_set_foreground(l.list, &fg), GSHSCENE_ERR_INVALID) << "a closed list is immutable";
  EXPECT_EQ(gshscene_list_set_foreground(nullptr, &fg), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_foreground(nullptr, &got), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_foreground(l.list, nullptr), GSHSCENE_ERR_INVALID);
}

TEST(Color, TheForegroundIsInTheListHashAndEqualityAndSurvivesAReset) {
  StoreHolder s;
  Ref sq(intern_square(s.store));
  ListHolder a(s.store), b(s.store);
  GSHSCENE_Paint cc = solid(0, 0, 0, 1.0);
  cc.kind = GSHSCENE_PAINT_FOREGROUND;
  ASSERT_EQ(gshscene_list_fill(a.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &cc, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(b.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &cc, 0, 0.0), GSHSCENE_OK);
  EXPECT_TRUE(gshscene_list_equal(a.list, b.list));
  GSHSCENE_Color red = {{1, 0, 0, 1}, GSHSCENE_Color_Space{}};
  ASSERT_EQ(gshscene_list_set_foreground(b.list, &red), GSHSCENE_OK);
  EXPECT_FALSE(gshscene_list_equal(a.list, b.list)) << "two lists that differ only in the foreground differ";
  EXPECT_NE(gshscene_list_hash(a.list), gshscene_list_hash(b.list));
  ASSERT_EQ(gshscene_list_reset(b.list), GSHSCENE_OK);
  GSHSCENE_Color got;
  ASSERT_EQ(gshscene_list_foreground(b.list, &got), GSHSCENE_OK);
  EXPECT_EQ(got.rgba[0], 0.0) << "a reset list has the default foreground again";
}

TEST(Paint, AForegroundPaintCarriesOnlyItsAlphaAndAZeroSpace) {
  StoreHolder s;
  Ref sq(intern_square(s.store));
  ListHolder l(s.store);
  const GSHSCENE_Result INV = GSHSCENE_ERR_INVALID;
  auto fill = [&](const GSHSCENE_Paint & p) {
    return gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0);
  };
  GSHSCENE_Paint p = solid(0, 0, 0, 0.5);
  p.kind = GSHSCENE_PAINT_FOREGROUND;
  EXPECT_EQ(fill(p), GSHSCENE_OK);
  const GSHSCENE_Item * item = nullptr;
  ASSERT_EQ(gshscene_list_item(l.list, 0, &item), GSHSCENE_OK);
  EXPECT_EQ(item->paint.kind, GSHSCENE_PAINT_FOREGROUND);
  EXPECT_EQ(item->paint.rgba[3], 0.5);
  for (int c = 0; c < 3; c++) {
    GSHSCENE_Paint q = p;
    q.rgba[c] = 0.25;
    EXPECT_EQ(fill(q), INV) << "a color component " << c;
    q.rgba[c] = -0.0;
    EXPECT_EQ(fill(q), INV) << "-0 is a second spelling of 0";
  }
  GSHSCENE_Paint q = p;
  q.space = srgb_space();
  EXPECT_EQ(fill(q), INV) << "a foreground paint has no space of its own";
  q = p;
  q.rgba[3] = 1.5;
  EXPECT_EQ(fill(q), INV);
  q.rgba[3] = kNaN;
  EXPECT_EQ(fill(q), INV);
  q = p;
  q.gradient = sq.get();
  EXPECT_EQ(fill(q), INV);
  for (double a : {0.0, 1.0}) {
    q = p;
    q.rgba[3] = a;
    EXPECT_EQ(fill(q), GSHSCENE_OK) << a;
  }
  // The palette stays refused: AD-23 says only that its index is a scene parameter, not whose table it indexes.
  q = p;
  q.kind = GSHSCENE_PAINT_PALETTE;
  EXPECT_EQ(fill(q), GSHSCENE_ERR_UNSUPPORTED);
  q.kind = GSHSCENE_PAINT_GRADIENT;
  EXPECT_EQ(fill(q), GSHSCENE_ERR_UNSUPPORTED);
}

TEST(Paint, ASolidPaintInEveryAcceptedSpaceReadsBack) {
  StoreHolder s;
  Ref sq(intern_square(s.store));
  ListHolder l(s.store);
  std::vector<GSHSCENE_Color_Space> spaces = {GSHSCENE_Color_Space{}, srgb_space(), linear_srgb_space(),
      space_of(GCOL_PRIMARIES_DISPLAY_P3, GCOL_TRANSFER_GAMMA, 2.2), space_of(GCOL_PRIMARIES_ADOBE_RGB, GCOL_TRANSFER_LINEAR),
      space_of(GCOL_PRIMARIES_BT2020, GCOL_TRANSFER_SRGB)};
  for (const GSHSCENE_Color_Space & sp : spaces) {
    GSHSCENE_Paint p = solid(0.25, 0.5, 0.75, 1.0);
    p.space = sp;
    ASSERT_EQ(gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK);
  }
  for (size_t i = 0; i < spaces.size(); i++) {
    const GSHSCENE_Item * item = nullptr;
    ASSERT_EQ(gshscene_list_item(l.list, i, &item), GSHSCENE_OK);
    EXPECT_EQ(item->paint.space.primaries, spaces[i].primaries) << i;
    EXPECT_EQ(item->paint.space.transfer, spaces[i].transfer) << i;
    EXPECT_EQ(item->paint.space.gamma, spaces[i].gamma) << i;
  }
}

TEST(Color, APictureHasNoForegroundOfItsOwn) {
  StoreHolder s;
  Ref sq(intern_square(s.store));
  GSHSCENE_Paint cc = solid(0, 0, 0, 1.0);
  cc.kind = GSHSCENE_PAINT_FOREGROUND;
  GSHSCENE_Color red = {{1, 0, 0, 1}, GSHSCENE_Color_Space{}};
  ListHolder with(s.store), without(s.store), same(s.store);
  for (ListHolder * l : {&with, &without, &same}) {
    ASSERT_EQ(gshscene_list_fill(l->list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &cc, 0, 0.0), GSHSCENE_OK);
  }
  ASSERT_EQ(gshscene_list_set_foreground(with.list, &red), GSHSCENE_OK);
  GSHSCENE_Color black;
  gshscene_color_init(&black);
  ASSERT_EQ(gshscene_list_set_foreground(same.list, &black), GSHSCENE_OK);
  for (ListHolder * l : {&with, &without, &same}) {
    ASSERT_EQ(gshscene_list_close(l->list), GSHSCENE_OK);
  }
  {
    ListHolder spaced(s.store);
    ASSERT_EQ(gshscene_list_fill(spaced.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &cc, 0, 0.0), GSHSCENE_OK);
    GSHSCENE_Color in_space = {{0, 0, 0, 1}, srgb_space()};
    ASSERT_EQ(gshscene_list_set_foreground(spaced.list, &in_space), GSHSCENE_OK);
    ASSERT_EQ(gshscene_list_close(spaced.list), GSHSCENE_OK);
    Ref r;
    EXPECT_EQ(gshscene_store_intern_picture(s.store, spaced.list, &r.resource), GSHSCENE_ERR_INVALID)
        << "black in a space is not the default foreground";
  }
  Ref a, b, c;
  EXPECT_EQ(gshscene_store_intern_picture(s.store, with.list, &a.resource), GSHSCENE_ERR_INVALID)
      << "a list with a foreground of its own cannot become a picture";
  EXPECT_EQ(a.get(), nullptr);
  ASSERT_EQ(gshscene_store_intern_picture(s.store, without.list, &b.resource), GSHSCENE_OK);
  ASSERT_EQ(gshscene_store_intern_picture(s.store, same.list, &c.resource), GSHSCENE_OK) << "the default set explicitly is the default";
  EXPECT_EQ(b.get(), c.get());
  GSHSCENE_Color got;
  ASSERT_EQ(gshscene_list_foreground(gshscene_resource_picture(b.get()), &got), GSHSCENE_OK);
  EXPECT_EQ(std::memcmp(&got, &black, sizeof got), 0);
}

TEST(Pinned, TheValuesThatAreHashedAreWhatTheSpellingsSay) {
  EXPECT_EQ((int)GSHSCENE_PAINT_SOLID, 0);
  EXPECT_EQ((int)GSHSCENE_PAINT_GRADIENT, 1);
  EXPECT_EQ((int)GSHSCENE_PAINT_FOREGROUND, 2);
  EXPECT_EQ((int)GSHSCENE_PAINT_PALETTE, 3);
  EXPECT_EQ((int)GSHSCENE_PAINT_KIND_COUNT, 4);
  EXPECT_EQ((int)GSHSCENE_ORDER_FLAT, 0);
  EXPECT_EQ((int)GSHSCENE_ORDER_DEPTH, 1);
  EXPECT_EQ((int)GSHSCENE_ORDER_OVERLAY, 2);
  EXPECT_EQ((int)GSHSCENE_ORDERING_COUNT, 3);
  const int composites[] = {GSHSCENE_COMPOSITE_SRC_OVER, GSHSCENE_COMPOSITE_CLEAR, GSHSCENE_COMPOSITE_SRC, GSHSCENE_COMPOSITE_DEST,
      GSHSCENE_COMPOSITE_DEST_OVER, GSHSCENE_COMPOSITE_SRC_IN, GSHSCENE_COMPOSITE_DEST_IN, GSHSCENE_COMPOSITE_SRC_OUT,
      GSHSCENE_COMPOSITE_DEST_OUT, GSHSCENE_COMPOSITE_SRC_ATOP, GSHSCENE_COMPOSITE_DEST_ATOP, GSHSCENE_COMPOSITE_XOR,
      GSHSCENE_COMPOSITE_PLUS};
  for (int i = 0; i < 13; i++) {
    EXPECT_EQ(composites[i], i) << "composite operator " << i;
  }
  EXPECT_EQ((int)GSHSCENE_COMPOSITE_COUNT, 13);
  EXPECT_EQ((int)GSHSCENE_TIE_LAYER, 1);
  EXPECT_EQ((int)GSHSCENE_TIE_PATH, 2);
  EXPECT_EQ((int)GSHSCENE_TIE_DECLARED, 3);
  EXPECT_EQ((int)GSHSCENE_TIE_REASON_COUNT, 4);
  EXPECT_EQ((int)GSHSCENE_PROBLEM_PARAMETER, 5);
  EXPECT_EQ((int)GSHSCENE_PROBLEM_REASON_COUNT, 6);
  // color's own enumerations, which a space spells as numbers in the hash.
  EXPECT_EQ((int)GCOL_PRIMARIES_SRGB, 1);
  EXPECT_EQ((int)GCOL_PRIMARIES_ADOBE_RGB, 2);
  EXPECT_EQ((int)GCOL_PRIMARIES_DISPLAY_P3, 3);
  EXPECT_EQ((int)GCOL_PRIMARIES_BT2020, 4);
  EXPECT_EQ((int)GCOL_TRANSFER_LINEAR, 1);
  EXPECT_EQ((int)GCOL_TRANSFER_SRGB, 2);
  EXPECT_EQ((int)GCOL_TRANSFER_GAMMA, 3);
}

TEST(ColorSpace, EveryFieldOfASpaceIsPartOfTheEqualityAndTheHashOfAList) {
  StoreHolder s;
  Ref sq(intern_square(s.store));
  std::vector<GSHSCENE_Color_Space> spaces = {space_of(GCOL_PRIMARIES_DISPLAY_P3, GCOL_TRANSFER_GAMMA, 2.2),
      space_of(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_GAMMA, 2.2), space_of(GCOL_PRIMARIES_DISPLAY_P3, GCOL_TRANSFER_SRGB),
      space_of(GCOL_PRIMARIES_DISPLAY_P3, GCOL_TRANSFER_GAMMA, 2.4), space_of(GCOL_PRIMARIES_DISPLAY_P3, GCOL_TRANSFER_LINEAR)};
  for (int where = 0; where < 3; where++) {  // the paint's space, a group's blend space, the foreground's
    std::vector<uint64_t> hashes;
    std::vector<std::unique_ptr<ListHolder>> lists;
    for (const GSHSCENE_Color_Space & sp : spaces) {
      lists.emplace_back(new ListHolder(s.store));
      GSHSCENE_List * l = lists.back()->list;
      if (where == 0) {
        GSHSCENE_Paint p = solid(0.25, 0.5, 0.75, 1.0);
        p.space = sp;
        ASSERT_EQ(gshscene_list_fill(l, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), GSHSCENE_OK);
      } else if (where == 1) {
        GSHSCENE_Group g = make_group(1.0);
        g.blend_space = sp;
        ASSERT_EQ(gshscene_list_group_push(l, &g, 0, 0.0), GSHSCENE_OK);
      } else {
        GSHSCENE_Color fg = {{0, 0, 0, 1}, sp};
        ASSERT_EQ(gshscene_list_set_foreground(l, &fg), GSHSCENE_OK);
      }
      hashes.push_back(gshscene_list_hash(l));
    }
    for (size_t i = 0; i < spaces.size(); i++) {
      for (size_t j = i + 1; j < spaces.size(); j++) {
        EXPECT_NE(hashes[i], hashes[j]) << "where " << where << ": " << i << " and " << j;
        EXPECT_FALSE(gshscene_list_equal(lists[i]->list, lists[j]->list)) << "where " << where << ": " << i << " and " << j;
      }
    }
  }
}

TEST(Color, EveryComponentOfTheForegroundIsPartOfTheEqualityAndTheHashOfAList) {
  StoreHolder s;
  std::vector<GSHSCENE_Color> colors;
  GSHSCENE_Color base = {{0.25, 0.5, 0.75, 0.5}, srgb_space()};
  colors.push_back(base);
  for (int i = 0; i < 4; i++) {
    GSHSCENE_Color c = base;
    c.rgba[i] = i == 3 ? 0.25 : 0.125;
    colors.push_back(c);
  }
  std::vector<uint64_t> hashes;
  std::vector<std::unique_ptr<ListHolder>> lists;
  for (const GSHSCENE_Color & c : colors) {
    lists.emplace_back(new ListHolder(s.store));
    ASSERT_EQ(gshscene_list_set_foreground(lists.back()->list, &c), GSHSCENE_OK);
    hashes.push_back(gshscene_list_hash(lists.back()->list));
  }
  for (size_t i = 0; i < colors.size(); i++) {
    for (size_t j = i + 1; j < colors.size(); j++) {
      EXPECT_NE(hashes[i], hashes[j]) << i << " and " << j;
      EXPECT_FALSE(gshscene_list_equal(lists[i]->list, lists[j]->list)) << i << " and " << j;
    }
  }
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
