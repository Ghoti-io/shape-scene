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
 * N2 and N3 in doubles: the one conversion the scene evaluates, for a gradient's
 * stops (paint.h, G3; color-paint story P1).  The power function is `math`'s
 * `gmath_pow`, which gives the same bits on every target; libm is never called
 * (AD-22, check-libm).  Every operation is a plain IEEE double operation in a
 * fixed order, built with contraction off, so a conversion has the same bits on
 * every target.
 */

#include <ghoti.io/shape-scene/macros.h>

#include "../internal.h"

#include <ghoti.io/math/math.h>

/** sRGB (N2): the encoded value at and below which the curve is linear, the linear value likewise, and the constants. */
#define SRGB_DECODE_KNEE 0.04045
#define SRGB_ENCODE_KNEE 0.0031308
#define SRGB_SLOPE 12.92
#define SRGB_OFFSET 0.055
#define SRGB_SCALE 1.055
#define SRGB_EXPONENT 2.4

/** The D65 white of N1, CIE xy. */
#define WHITE_X 0.3127
#define WHITE_Y 0.3290

/** N1: the chromaticities of a gamut's red, green and blue, CIE xy, as a row each. */
static void chromaticities(GCOL_Primaries primaries, double xy[3][2]) {
  static const double SRGB[3][2] = {{0.64, 0.33}, {0.30, 0.60}, {0.15, 0.06}};
  static const double ADOBE[3][2] = {{0.64, 0.33}, {0.21, 0.71}, {0.15, 0.06}};
  static const double P3[3][2] = {{0.680, 0.320}, {0.265, 0.690}, {0.150, 0.060}};
  static const double BT2020[3][2] = {{0.708, 0.292}, {0.170, 0.797}, {0.131, 0.046}};
  const double (*table)[2] = SRGB;
  switch (primaries) {
    case GCOL_PRIMARIES_ADOBE_RGB:
      table = ADOBE;
      break;
    case GCOL_PRIMARIES_DISPLAY_P3:
      table = P3;
      break;
    case GCOL_PRIMARIES_BT2020:
      table = BT2020;
      break;
    default: /* sRGB: a checked space has one of the four */
      break;
  }
  for (int c = 0; c < 3; c++) {
    xy[c][0] = table[c][0];
    xy[c][1] = table[c][1];
  }
}

static double det3(double m[3][3]) {
  return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
         m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

/** The inverse by the adjugate (the gamut matrices are far from singular). */
static void inverse3(double m[3][3], double out[3][3]) {
  double d = det3(m);
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      int r0 = (j + 1) % 3;
      int r1 = (j + 2) % 3;
      int c0 = (i + 1) % 3;
      int c1 = (i + 2) % 3;
      out[i][j] = (m[r0][c0] * m[r1][c1] - m[r0][c1] * m[r1][c0]) / d;
    }
  }
}

static void mul3(double m[3][3], const double v[3], double out[3]) {
  for (int i = 0; i < 3; i++) {
    out[i] = m[i][0] * v[0] + m[i][1] * v[1] + m[i][2] * v[2];
  }
}

/** N3: the matrix from linear RGB to XYZ of a gamut, column c being `S_c (X_c, 1, Z_c)`. */
static void rgb_to_xyz(GCOL_Primaries primaries, double out[3][3]) {
  double xy[3][2];
  chromaticities(primaries, xy);
  double p[3][3];
  for (int c = 0; c < 3; c++) {
    p[0][c] = xy[c][0] / xy[c][1];
    p[1][c] = 1.0;
    p[2][c] = (1.0 - xy[c][0] - xy[c][1]) / xy[c][1];
  }
  const double white[3] = {WHITE_X / WHITE_Y, 1.0, (1.0 - WHITE_X - WHITE_Y) / WHITE_Y};
  double pi[3][3];
  inverse3(p, pi);
  double s[3];
  mul3(pi, white, s);
  for (int r = 0; r < 3; r++) {
    for (int c = 0; c < 3; c++) {
      out[r][c] = s[c] * p[r][c];
    }
  }
}

/** `sign(x) |x|^e`, zero for zero. */
static double signed_pow(double x, double e) {
  if (x == 0.0) {
    return 0.0;
  }
  return x < 0.0 ? -gmath_pow(-x, e) : gmath_pow(x, e);
}

/** N2: an encoded component to linear light. */
static double decode(const GSHSCENE_Color_Space * space, double x) {
  switch (space->transfer) {
    case GCOL_TRANSFER_SRGB:
      return x <= SRGB_DECODE_KNEE ? x / SRGB_SLOPE : gmath_pow((x + SRGB_OFFSET) / SRGB_SCALE, SRGB_EXPONENT);
    case GCOL_TRANSFER_GAMMA:
      return signed_pow(x, space->gamma);
    default: /* linear */
      return x;
  }
}

/** N2: linear light to an encoded component. */
static double encode(const GSHSCENE_Color_Space * space, double y) {
  switch (space->transfer) {
    case GCOL_TRANSFER_SRGB:
      return y <= SRGB_ENCODE_KNEE ? y * SRGB_SLOPE : SRGB_SCALE * gmath_pow(y, 1.0 / SRGB_EXPONENT) - SRGB_OFFSET;
    case GCOL_TRANSFER_GAMMA:
      return signed_pow(y, 1.0 / space->gamma);
    default: /* linear */
      return y;
  }
}

void gshscene_convert_straight(const GSHSCENE_Color_Space * from, const GSHSCENE_Color_Space * to, const double in[3],
    double out[3]) {
  if (gshscene_space_equal(from, to)) {
    /* N3: untouched, exactly, when the spaces are the same. */
    out[0] = in[0];
    out[1] = in[1];
    out[2] = in[2];
    return;
  }
  /* N1's saturation after each step, so that a power only ever takes a value of magnitude at most 2^64. */
  double lin[3];
  for (int i = 0; i < 3; i++) {
    lin[i] = gshscene_saturate(decode(from, in[i]));
  }
  if (from->primaries != to->primaries) {
    /* (When only the transfers differ the matrices cancel and are not applied.) */
    double ma[3][3];
    double mb[3][3];
    double mbi[3][3];
    double xyz[3];
    rgb_to_xyz(from->primaries, ma);
    rgb_to_xyz(to->primaries, mb);
    inverse3(mb, mbi);
    mul3(ma, lin, xyz);
    mul3(mbi, xyz, lin);
    for (int i = 0; i < 3; i++) {
      lin[i] = gshscene_saturate(lin[i]);
    }
  }
  for (int i = 0; i < 3; i++) {
    out[i] = gshscene_saturate(encode(to, lin[i]));
  }
}
