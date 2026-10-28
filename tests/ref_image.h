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
 * The image-sampling definition (image_paint.h, IP2 to IP9) evaluated in `double`, written from the rules and not from any
 * sink: the library's own test evaluator.  tests/unit/test_image_paint.cpp checks it against the vectors of
 * tools/gen-image-vectors.py (exact rationals, sharing no code), and the conformance suite uses it as the reference a sink's
 * pixels are compared with.  A texel is read through ::gshscene_image_decode, the one decode every sink shares.  The colors are
 * converted by tests/ref_composite.h, the independent evaluator of N2 and N3.
 *
 * The convention of the vectors for a zero space: the image's own space is the layer's when the image says zero, and a zero
 * layer appears only where every space of a case is zero.
 */

#ifndef GHOTI_IO_GSHSCENE_TEST_REF_IMAGE_H
#define GHOTI_IO_GSHSCENE_TEST_REF_IMAGE_H

#include "ref_composite.h"

#include <ghoti.io/shape-scene/shape-scene.h>

#include <cmath>
#include <vector>

namespace refimg {

/** What a sample is taken from and how. */
struct Setup {
  const GSHSCENE_Resource * image = nullptr;
  int window[4] = {0, 0, 0, 0};            // x0, y0, x1, y1; all zero means the whole image
  unsigned extend_x = GSHSCENE_IMAGE_EXTEND_PAD;
  unsigned extend_y = GSHSCENE_IMAGE_EXTEND_PAD;
  unsigned filter = GSHSCENE_IMAGE_FILTER_LINEAR;
  GSHSCENE_Color_Space filter_space = {};  // zero: the image's own space
  GSHSCENE_Color_Space layer = {};         // L
};

inline long long floor_to(double x) {
  return (long long)std::floor(x);
}

inline long long mod(long long a, long long n) {
  long long r = a % n;
  return r < 0 ? r + n : r;
}

/** IP6: the texel index a tap `k` reads along one axis of the window `[lo, hi)`, or -1 for the transparent texel of `NONE`. */
inline long long tap(long long k, long long lo, long long hi, unsigned mode) {
  long long n = hi - lo;
  switch (mode) {
    case GSHSCENE_IMAGE_EXTEND_PAD:
      return k < lo ? lo : k > hi - 1 ? hi - 1 : k;
    case GSHSCENE_IMAGE_EXTEND_REPEAT:
      return lo + mod(k - lo, n);
    case GSHSCENE_IMAGE_EXTEND_REFLECT: {
      long long m = mod(k - lo, 2 * n);
      return m < n ? lo + m : lo + 2 * n - 1 - m;
    }
    default:
      return k >= lo && k < hi ? k : -1;
  }
}

struct Resolved {
  int window[4];
  refc::Space image_space;  // the image's own space, the layer's when it says zero
  refc::Space filter;       // F
  refc::Space layer;        // L
  bool alpha_only;
};

inline Resolved resolve(const Setup & s) {
  GSHSCENE_Image_Info info;
  gshscene_resource_image(s.image, &info);
  Resolved r;
  bool whole = !s.window[0] && !s.window[1] && !s.window[2] && !s.window[3];
  r.window[0] = whole ? 0 : s.window[0];
  r.window[1] = whole ? 0 : s.window[1];
  r.window[2] = whole ? (int)info.width : s.window[2];
  r.window[3] = whole ? (int)info.height : s.window[3];
  r.layer = refc::from(s.layer);
  r.image_space = refc::is_target(refc::from(info.space)) ? r.layer : refc::from(info.space);
  r.filter = refc::is_target(refc::from(s.filter_space)) ? r.image_space : refc::from(s.filter_space);
  r.alpha_only = info.alpha_only != 0;
  return r;
}

/** The premultiplied value of the texel read by taps (@p ix, @p iy) in the filter space F (IP4, IP5). */
inline void texel(const Setup & s, const Resolved & r, long long ix, long long iy, double out[4]) {
  long long tx = tap(ix, r.window[0], r.window[2], s.extend_x);
  long long ty = tap(iy, r.window[1], r.window[3], s.extend_y);
  if (tx < 0 || ty < 0) {
    out[0] = out[1] = out[2] = out[3] = 0.0;
    return;
  }
  double p[4];
  gshscene_image_decode(s.image, (uint32_t)tx, (uint32_t)ty, p);
  if (r.alpha_only || refc::same(r.image_space, r.filter)) {
    for (int k = 0; k < 4; k++) {
      out[k] = p[k];
    }
    return;
  }
  double a = p[3];
  double straight[3] = {0, 0, 0};
  if (a != 0.0) {
    for (int k = 0; k < 3; k++) {
      straight[k] = refc::unpremultiply(p[k], a);
    }
  }
  double converted[3];
  refc::convert(straight, r.image_space, r.filter, converted);
  for (int k = 0; k < 3; k++) {
    out[k] = refc::sat(converted[k]) * a;
  }
  out[3] = a;
}

/** Mitchell-Netravali with B = C = 1/3 (IP7), exactly as the rule states it. */
inline double kernel(double x) {
  if (x < 1.0) {
    return (21.0 * x * x * x - 36.0 * x * x + 16.0) / 18.0;
  }
  return (-7.0 * x * x * x + 36.0 * x * x - 60.0 * x + 32.0) / 18.0;
}

/** IP5's last step: the value premultiplied in F to the layer's space L (not at all when they are one). */
inline void to_layer(const Resolved & r, double v[4]) {
  if (r.alpha_only || refc::same(r.filter, r.layer)) {
    return;
  }
  refc::Px in = {{v[0], v[1], v[2], v[3]}};
  refc::Px out = refc::convert_premultiplied(in, r.filter, r.layer);
  for (int k = 0; k < 4; k++) {
    v[k] = out.v[k];
  }
}

/** IP7: `NEAREST`, `LINEAR` or `CUBIC` at the image-space point (@p u, @p v); the value premultiplied in the layer's space. */
inline void point(const Setup & s, double u, double v, double out[4]) {
  Resolved r = resolve(s);
  double acc[4] = {0, 0, 0, 0};
  if (s.filter == GSHSCENE_IMAGE_FILTER_NEAREST) {
    texel(s, r, floor_to(u), floor_to(v), acc);
  } else if (s.filter == GSHSCENE_IMAGE_FILTER_LINEAR) {
    double sx = u - 0.5, sy = v - 0.5;
    long long i = floor_to(sx), j = floor_to(sy);
    double f = sx - (double)i, g = sy - (double)j;
    const double wx[2] = {1.0 - f, f};
    const double wy[2] = {1.0 - g, g};
    for (int b = 0; b < 2; b++) {
      for (int a = 0; a < 2; a++) {
        double t[4];
        texel(s, r, i + a, j + b, t);
        for (int k = 0; k < 4; k++) {
          acc[k] += wx[a] * wy[b] * t[k];
        }
      }
    }
  } else {
    double sx = u - 0.5, sy = v - 0.5;
    long long i = floor_to(sx), j = floor_to(sy);
    double f = sx - (double)i, g = sy - (double)j;
    const double wx[4] = {kernel(f + 1.0), kernel(f), kernel(1.0 - f), kernel(2.0 - f)};
    const double wy[4] = {kernel(g + 1.0), kernel(g), kernel(1.0 - g), kernel(2.0 - g)};
    double lo[4] = {1e300, 1e300, 1e300, 1e300};
    double hi[4] = {-1e300, -1e300, -1e300, -1e300};
    for (int b = 0; b < 4; b++) {
      for (int a = 0; a < 4; a++) {
        double t[4];
        texel(s, r, i - 1 + a, j - 1 + b, t);
        for (int k = 0; k < 4; k++) {
          acc[k] += wx[a] * wy[b] * t[k];
          lo[k] = t[k] < lo[k] ? t[k] : lo[k];
          hi[k] = t[k] > hi[k] ? t[k] : hi[k];
        }
      }
    }
    for (int k = 0; k < 4; k++) {
      acc[k] = acc[k] < lo[k] ? lo[k] : acc[k] > hi[k] ? hi[k] : acc[k];
    }
  }
  to_layer(r, acc);
  for (int k = 0; k < 4; k++) {
    out[k] = acc[k];
  }
}

/** The area of a convex polygon clipped to the unit square, by Sutherland-Hodgman, coordinates relative to the cell. */
inline double clipped_area(const std::vector<double> & polygon) {
  std::vector<double> p = polygon;
  for (int edge = 0; edge < 4 && !p.empty(); edge++) {
    std::vector<double> q;
    size_t n = p.size() / 2;
    for (size_t k = 0; k < n; k++) {
      double ax = p[2 * k], ay = p[2 * k + 1];
      double bx = p[2 * ((k + 1) % n)], by = p[2 * ((k + 1) % n) + 1];
      // signed distance inside the half-plane: x >= 0, x <= 1, y >= 0, y <= 1
      auto side = [edge](double x, double y) { return edge == 0 ? x : edge == 1 ? 1.0 - x : edge == 2 ? y : 1.0 - y; };
      double da = side(ax, ay), db = side(bx, by);
      if (da >= 0.0) {
        q.push_back(ax);
        q.push_back(ay);
      }
      if ((da >= 0.0) != (db >= 0.0)) {
        double t = da / (da - db);
        q.push_back(ax + t * (bx - ax));
        q.push_back(ay + t * (by - ay));
      }
    }
    p.swap(q);
  }
  double a = 0.0;
  size_t n = p.size() / 2;
  for (size_t k = 0; k < n; k++) {
    a += p[2 * k] * p[2 * ((k + 1) % n) + 1] - p[2 * ((k + 1) % n)] * p[2 * k + 1];
  }
  return std::fabs(a) / 2.0;
}

/** IP7: `AREA`: the mean of the extended image over the parallelogram `origin + s edge_a + t edge_b`, the layer's space. */
inline void area(const Setup & s, const double origin[2], const double edge_a[2], const double edge_b[2], double out[4]) {
  Resolved r = resolve(s);
  const double px[4] = {origin[0], origin[0] + edge_a[0], origin[0] + edge_a[0] + edge_b[0], origin[0] + edge_b[0]};
  const double py[4] = {origin[1], origin[1] + edge_a[1], origin[1] + edge_a[1] + edge_b[1], origin[1] + edge_b[1]};
  double total = std::fabs(edge_a[0] * edge_b[1] - edge_a[1] * edge_b[0]);
  double minx = px[0], maxx = px[0], miny = py[0], maxy = py[0];
  for (int k = 1; k < 4; k++) {
    minx = px[k] < minx ? px[k] : minx;
    maxx = px[k] > maxx ? px[k] : maxx;
    miny = py[k] < miny ? py[k] : miny;
    maxy = py[k] > maxy ? py[k] : maxy;
  }
  double sum[4] = {0, 0, 0, 0};
  double comp[4] = {0, 0, 0, 0};  // Kahan
  for (long long cj = floor_to(miny); cj < (long long)std::ceil(maxy); cj++) {
    for (long long ci = floor_to(minx); ci < (long long)std::ceil(maxx); ci++) {
      std::vector<double> poly;
      for (int k = 0; k < 4; k++) {
        poly.push_back(px[k] - (double)ci);
        poly.push_back(py[k] - (double)cj);
      }
      double a = clipped_area(poly);
      if (a <= 0.0) {
        continue;
      }
      double t[4];
      texel(s, r, ci, cj, t);
      for (int k = 0; k < 4; k++) {
        double term = a * t[k] - comp[k];
        double next = sum[k] + term;
        comp[k] = (next - sum[k]) - term;
        sum[k] = next;
      }
    }
  }
  double mean[4];
  for (int k = 0; k < 4; k++) {
    mean[k] = sum[k] / total;
  }
  to_layer(r, mean);
  for (int k = 0; k < 4; k++) {
    out[k] = mean[k];
  }
}

}  // namespace refimg

#endif
