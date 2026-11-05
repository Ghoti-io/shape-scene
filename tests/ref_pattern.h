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
 * The double-precision reference evaluator of the pattern definition (PT5 to PT10): the same definition as
 * tools/pattern-oracle.py, written in plain `double` arithmetic (+, -, *, / and comparisons; no libm, no `long double`, no
 * fused multiply-add) over the scene structs of pattern_vectors.h, producing the premultiplied rgba of a pixel.  It is what a
 * sink that draws patterns is measured against; tests/unit/test_pattern_oracle.cpp checks it against the oracle's exact
 * answers.
 *
 * **Accuracy.**  Every polygon is clipped in doubles and every area is a shoelace sum taken relative to the polygon's first
 * vertex, so a pixel is within about 1e-14 of the exact value for a lattice whose positions are small, which is the
 * tolerance (1e-12) the vectors state.  Exactness is lost in three places, and the vectors that carry them say so in their
 * own tolerance: a cell's position is formed in doubles as PT5 says (one product and one sum), so a lattice at copy index
 * 10^6 carries the rounding of a number of size 1e5 (about 1.5e-11) into the device; the edge where a cell edge meets a pixel
 * edge is decided by a comparison of rounded numbers, not an exact predicate (a sliver of area 1e-17 may count as a copy);
 * and the notice (PT10) is decided exactly, by a 128-bit product of the doubles' integers, so it does not round or underflow.
 *
 * **Plants.**  Setting ::pattern_ref::g_plant breaks one rule on purpose, the way the oracle's `--plant` does, so a test can
 * show that its comparison with the vectors fails for each: `separate-shapes`, `copy0-source`, `group-per-copy`,
 * `reverse-lattice-order`, `tile-not-isolated`, `keeps-tile-colors`, `add-step`, `left-plus-w`, `content-not-clipped`,
 * `foreground-from-creator`.  `add-step` and `left-plus-w` change ::pattern_ref::cell and the translation of a copy.
 */

#ifndef GHOTI_IO_GSHSCENE_TEST_REF_PATTERN_H
#define GHOTI_IO_GSHSCENE_TEST_REF_PATTERN_H

#include "pattern_vectors.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <stdexcept>
#include <vector>

namespace pattern_ref {

namespace pv = pattern_vectors;

/** The defects the evaluator can be told to commit. */
enum Plant {
  kNoPlant = 0,
  kSeparateShapes,
  kCopy0Source,
  kGroupPerCopy,
  kReverseLatticeOrder,
  kTileNotIsolated,
  kKeepsTileColors,
  kAddStep,
  kLeftPlusW,
  kContentNotClipped,
  kForegroundFromCreator,
  kPlantCount
};

/** The defect in force (kNoPlant: none). */
inline Plant g_plant = kNoPlant;

inline const char * plant_name(Plant p) {
  static const char * const names[] = {"none", "separate-shapes", "copy0-source", "group-per-copy", "reverse-lattice-order",
      "tile-not-isolated", "keeps-tile-colors", "add-step", "left-plus-w", "content-not-clipped", "foreground-from-creator"};
  return names[p];
}

/** Sets a plant for a scope. */
struct PlantScope {
  Plant saved;
  explicit PlantScope(Plant p) : saved(g_plant) { g_plant = p; }
  ~PlantScope() { g_plant = saved; }
  PlantScope(const PlantScope &) = delete;
  PlantScope & operator=(const PlantScope &) = delete;
};

struct Rgba {
  double v[4];
};

inline Rgba zero() { return Rgba{{0.0, 0.0, 0.0, 0.0}}; }

struct Map {
  double m[6];
};

struct Pt {
  double x;
  double y;
};

using Poly = std::vector<Pt>;

// --- maps -----------------------------------------------------------------------------------------------------------
inline Pt map_pt(const Map & M, Pt p) { return Pt{M.m[0] * p.x + M.m[1] * p.y + M.m[4], M.m[2] * p.x + M.m[3] * p.y + M.m[5]}; }

inline Map compose(const Map & A, const Map & B) {
  const double * a = A.m;
  const double * b = B.m;
  return Map{{a[0] * b[0] + a[1] * b[2], a[0] * b[1] + a[1] * b[3], a[2] * b[0] + a[3] * b[2], a[2] * b[1] + a[3] * b[3],
      a[0] * b[4] + a[1] * b[5] + a[4], a[2] * b[4] + a[3] * b[5] + a[5]}};
}

inline double det(const Map & M) { return M.m[0] * M.m[3] - M.m[1] * M.m[2]; }

inline Map invert(const Map & M) {
  double d = det(M);
  double i0 = M.m[3] / d, i1 = -M.m[1] / d, i2 = -M.m[2] / d, i3 = M.m[0] / d;
  return Map{{i0, i1, i2, i3, -(i0 * M.m[4] + i1 * M.m[5]), -(i2 * M.m[4] + i3 * M.m[5])}};
}

inline Map translation_map(double tx, double ty) { return Map{{1.0, 0.0, 0.0, 1.0, tx, ty}}; }

inline Map map_of(const double m[6]) { return Map{{m[0], m[1], m[2], m[3], m[4], m[5]}}; }

// --- polygons -------------------------------------------------------------------------------------------------------
/** Twice the signed area, relative to the first vertex (so that the terms stay of the size of the polygon). */
inline double signed2(const Poly & P) {
  double s = 0.0;
  for (size_t k = 1; k + 1 < P.size(); k++) {
    s += (P[k].x - P[0].x) * (P[k + 1].y - P[0].y) - (P[k + 1].x - P[0].x) * (P[k].y - P[0].y);
  }
  return s;
}

inline double area(const Poly & P) {
  if (P.size() < 3) {
    return 0.0;
  }
  double s = signed2(P);
  return (s < 0.0 ? -s : s) / 2.0;
}

/** The polygon with positive signed area (reversed if need be); empty when it has none. */
inline Poly ccw(const Poly & P) {
  double s = signed2(P);
  if (!(s > 0.0 || s < 0.0)) {
    return Poly();
  }
  if (s > 0.0) {
    return P;
  }
  return Poly(P.rbegin(), P.rend());
}

/** The part of the convex polygon with a x + b y + c >= 0. */
inline Poly clip_half(const Poly & P, double a, double b, double c) {
  Poly out;
  if (P.empty()) {
    return out;
  }
  Pt prev = P.back();
  double dprev = a * prev.x + b * prev.y + c;
  for (const Pt & cur : P) {
    double dcur = a * cur.x + b * cur.y + c;
    if (dcur >= 0.0) {
      if (dprev < 0.0) {
        double u = dprev / (dprev - dcur);
        out.push_back(Pt{prev.x + (cur.x - prev.x) * u, prev.y + (cur.y - prev.y) * u});
      }
      out.push_back(cur);
    } else if (dprev > 0.0) {
      double u = dprev / (dprev - dcur);
      out.push_back(Pt{prev.x + (cur.x - prev.x) * u, prev.y + (cur.y - prev.y) * u});
    }
    prev = cur;
    dprev = dcur;
  }
  return out;
}

/** The convex polygon P cut by the counterclockwise convex polygon Q. */
inline Poly convex_clip(Poly P, const Poly & Q) {
  for (size_t k = 0; k < Q.size(); k++) {
    const Pt & p0 = Q[k];
    const Pt & p1 = Q[(k + 1) % Q.size()];
    double a = -(p1.y - p0.y);
    double b = p1.x - p0.x;
    P = clip_half(P, a, b, -(a * p0.x + b * p0.y));
    if (P.size() < 3) {
      return Poly();
    }
  }
  return P;
}

// --- the lattice (PT5) ----------------------------------------------------------------------------------------------
/** `step` added `|i|` times (the planted defect), negated for a negative index. */
inline double accumulate(double step, int64_t i) {
  static std::map<double, std::vector<double>> tables;
  std::vector<double> & t = tables[step];
  if (t.empty()) {
    t.push_back(0.0);
  }
  size_t n = (size_t)(i < 0 ? -i : i);
  while (t.size() <= n) {
    t.push_back(t.back() + step);
  }
  return i >= 0 ? t[n] : -t[n];
}

/** The translation of the index @p i along an axis: the product (one rounding), or under the plant the accumulated sum. */
inline double translation_axis(double step, int64_t i) {
  return g_plant == kAddStep ? accumulate(step, i) : (double)i * step;
}

/** PT5's cell {left, top, right, bottom} of the copy (i, j), formed in doubles as the library does; planted forms replace it. */
inline void cell(const double c[4], const double step[2], int64_t i, int64_t j, double out[4]) {
  double x = c[0], y = c[1], w = c[2], h = c[3];
  double left = x + translation_axis(step[0], i);
  double top = y + translation_axis(step[1], j);
  double right = step[0] == w && g_plant != kLeftPlusW ? x + translation_axis(step[0], i + 1) : left + w;
  double bottom = step[1] == h && g_plant != kLeftPlusW ? y + translation_axis(step[1], j + 1) : top + h;
  out[0] = left;
  out[1] = top;
  out[2] = right;
  out[3] = bottom;
}

/** PT7's predicate: on a repeating axis, step < size. */
inline int overlaps(double w, double h, double x_step, double y_step, int repeat_x, int repeat_y) {
  return (repeat_x && x_step < w) || (repeat_y && y_step < h) ? 1 : 0;
}

inline int overlaps(const pv::VPattern & p) {
  return overlaps(p.cell[2], p.cell[3], p.step[0], p.step[1], p.repeat[0], p.repeat[1]);
}

inline bool degenerate(const pv::VPattern & p) {
  return p.cell[2] == 0.0 || p.cell[3] == 0.0 || (p.repeat[0] && p.step[0] == 0.0) || (p.repeat[1] && p.step[1] == 0.0);
}

// --- PT10: an exact decision on the doubles ------------------------------------------------------------------------
__extension__ typedef unsigned __int128 U128;

struct Wide {
  bool zero;
  bool neg;
  U128 m;  ///< The magnitude is m * 2^e.
  int e;
};

inline Wide wide_product(double a, double b) {
  uint64_t ba, bb;
  std::memcpy(&ba, &a, sizeof ba);
  std::memcpy(&bb, &b, sizeof bb);
  auto parts = [](uint64_t bits, uint64_t * mant, int * exp) {
    uint64_t ex = (bits >> 52) & 0x7ff;
    uint64_t fr = bits & 0xfffffffffffffull;
    if (ex == 0) {
      *mant = fr;
      *exp = -1074;
    } else {
      *mant = fr | (1ull << 52);
      *exp = (int)ex - 1075;
    }
  };
  uint64_t ma, mb;
  int ea, eb;
  parts(ba, &ma, &ea);
  parts(bb, &mb, &eb);
  Wide w;
  w.zero = ma == 0 || mb == 0;
  w.neg = ((ba >> 63) != (bb >> 63));
  w.m = (U128)ma * (U128)mb;
  w.e = ea + eb;
  if (!w.zero) {
    while (!(w.m >> 127)) {  // normalize so the top bit is set
      w.m <<= 1;
      w.e -= 1;
    }
  }
  return w;
}

/** Whether `a d - b c` is exactly zero, for any doubles (no rounding, no underflow). */
inline bool det_is_zero(double a, double b, double c, double d) {
  Wide p = wide_product(a, d);
  Wide q = wide_product(b, c);
  if (p.zero || q.zero) {
    return p.zero && q.zero;
  }
  return p.neg == q.neg && p.e == q.e && p.m == q.m;
}

/** PT10: 23 when some map has a 2x2 part of determinant exactly 0, else 37 for a zero size or a zero step on a repeating axis, else 0. */
inline int notice(const double (*maps)[6], int count, double w, double h, double x_step, double y_step, int repeat_x,
    int repeat_y) {
  for (int k = 0; k < count; k++) {
    if (det_is_zero(maps[k][0], maps[k][1], maps[k][2], maps[k][3])) {
      return 23;
    }
  }
  if (w == 0.0 || h == 0.0 || (repeat_x && x_step == 0.0) || (repeat_y && y_step == 0.0)) {
    return 37;
  }
  return 0;
}

// --- compositing ----------------------------------------------------------------------------------------------------
inline Rgba over(const Rgba & L, const Rgba & FS) {
  double ia = 1.0 - FS.v[3];
  return Rgba{{FS.v[0] + ia * L.v[0], FS.v[1] + ia * L.v[1], FS.v[2] + ia * L.v[2], FS.v[3] + ia * L.v[3]}};
}

/** apply(L, S, k, composite) on premultiplied values: 1 SRC_OVER, 2 MULTIPLY. */
inline Rgba apply(const Rgba & L, const Rgba & S, double k, int composite) {
  Rgba ks{{k * S.v[0], k * S.v[1], k * S.v[2], k * S.v[3]}};
  if (composite == 1) {
    return over(L, ks);
  }
  double ia = 1.0 - ks.v[3];
  double ib = 1.0 - L.v[3];
  return Rgba{{ib * ks.v[0] + ia * L.v[0] + ks.v[0] * L.v[0], ib * ks.v[1] + ia * L.v[1] + ks.v[1] * L.v[1],
      ib * ks.v[2] + ia * L.v[2] + ks.v[2] * L.v[2], ks.v[3] + L.v[3] - ks.v[3] * L.v[3]}};
}

inline Rgba premul(const double c[4]) { return Rgba{{c[0] * c[3], c[1] * c[3], c[2] * c[3], c[3]}}; }

// --- paint values and integrals ------------------------------------------------------------------------------------------------
/** The premultiplied color of the gradient @p op at the tile-space point @p pt (pad extension, straight interpolation). */
inline Rgba linear_value(const Pt & pt, const pv::VOp & op) {
  double dx = op.p1[0] - op.p0[0], dy = op.p1[1] - op.p0[1];
  double t = ((pt.x - op.p0[0]) * dx + (pt.y - op.p0[1]) * dy) / (dx * dx + dy * dy);
  const pv::VStop * st = op.stops;
  int n = op.stop_count;
  if (t <= st[0].offset) {
    return premul(st[0].rgba);
  }
  if (t >= st[n - 1].offset) {
    return premul(st[n - 1].rgba);
  }
  int k = 0;
  while (k + 2 < n && t > st[k + 1].offset) {
    k++;
  }
  double u = (t - st[k].offset) / (st[k + 1].offset - st[k].offset);
  double col[4];
  for (int s = 0; s < 4; s++) {
    col[s] = st[k].rgba[s] + u * (st[k + 1].rgba[s] - st[k].rgba[s]);
  }
  return premul(col);
}

/** The premultiplied texel of the NEAREST, PAD image of @p op that holds the tile-space point @p pt. */
inline Rgba image_value(const Pt & pt, const pv::VOp & op) {
  const pv::VImage & im = *op.image;
  double sx = op.image_map[0], sy = op.image_map[1], tx = op.image_map[2], ty = op.image_map[3];
  auto texel = [](double v, int n) {  // floor, clamped to the image (PAD)
    double f = static_cast<double>(static_cast<int64_t>(v));
    if (f > v) {
      f -= 1.0;
    }
    return f < 0.0 ? 0 : (f >= n ? n - 1 : static_cast<int>(f));
  };
  int ix = texel((pt.x - tx) / sx, im.width), iy = texel((pt.y - ty) / sy, im.height);
  const uint8_t * t = im.rgba8 + (iy * im.width + ix) * 4;
  double col[4] = {t[0] / 255.0, t[1] / 255.0, t[2] / 255.0, t[3] / 255.0};
  return premul(col);
}

// --- the evaluation -------------------------------------------------------------------------------------------------
struct Ctx {
  Map C;
  Map Cinv;
  int px;
  int py;
  Poly pixel;
  double fg[4];
};

struct Copy {
  int64_t i;
  int64_t j;
  double tx;
  double ty;
  Poly cellpix;  ///< The copy's mapped cell clipped to the pixel (counterclockwise), or the pixel under content-not-clipped.
};

using CopyList = std::vector<const Copy *>;
using Clips = std::vector<const pv::VOp *>;

Rgba pattern_pixel(const pv::VPattern & p, const Map & C, int px, int py, const double fg[4]);

struct Env {
  const pv::VPattern * p;
  const Ctx * ctx;
};

inline Poly device_region(const Ctx & ctx, const pv::VOp & op, const Copy & cp) {
  Poly out;
  for (int k = 0; k < op.point_count; k++) {
    out.push_back(map_pt(ctx.C, Pt{op.points[k].x + cp.tx, op.points[k].y + cp.ty}));
  }
  return out;
}

inline Rgba paint_integral(const Env & e, const pv::VOp & op, double A, double tx, double ty) {
  const Ctx & ctx = *e.ctx;
  Rgba out = zero();
  switch (op.paint) {
    case 1: {
      Rgba pc = premul(op.rgba);
      for (int k = 0; k < 4; k++) {
        out.v[k] = A * pc.v[k];
      }
      return out;
    }
    case 5: {
      static const double creator[4] = {0.1, 0.2, 0.9, 1.0};
      const double * f = g_plant == kForegroundFromCreator ? creator : ctx.fg;
      double col[4] = {f[0], f[1], f[2], f[3] * op.rgba[3]};
      Rgba pc = premul(col);
      for (int k = 0; k < 4; k++) {
        out.v[k] = A * pc.v[k];
      }
      return out;
    }
    case 4: {
      Map G = map_of(op.nested->map);
      Map C = compose(compose(ctx.C, translation_map(tx, ty)), G);
      Rgba S = pattern_pixel(*op.nested, C, ctx.px, ctx.py, ctx.fg);
      for (int k = 0; k < 4; k++) {
        out.v[k] = A * S.v[k];
      }
      return out;
    }
    default:
      break;
  }
  // a gradient or an image is a function of the position: its value at the pixel's centre in the copy's own frame, times the area
  Pt q = map_pt(ctx.Cinv, Pt{ctx.px + 0.5, ctx.py + 0.5});
  Pt pt{q.x - tx, q.y - ty};
  Rgba v = op.paint == 2 ? linear_value(pt, op) : image_value(pt, op);
  for (int k = 0; k < 4; k++) {
    out.v[k] = A * v.v[k];
  }
  return out;
}

inline Rgba draw_fill(const Env & e, const pv::VOp & op, const CopyList & copies, Rgba layer, const Clips & clips) {
  const Ctx & ctx = *e.ctx;
  Rgba total = zero();
  for (const Copy * cp : copies) {
    Poly reg = convex_clip(device_region(ctx, op, *cp), cp->cellpix);
    double A = area(reg);
    if (!(A > 0.0)) {
      continue;
    }
    double c = 1.0;
    for (const pv::VOp * clip : clips) {
      c *= area(convex_clip(device_region(ctx, *clip, *cp), cp->cellpix));
    }
    if (!(c > 0.0)) {
      continue;
    }
    double tx = g_plant == kCopy0Source ? 0.0 : cp->tx;
    double ty = g_plant == kCopy0Source ? 0.0 : cp->ty;
    Rgba I = paint_integral(e, op, A, tx, ty);
    Rgba FS{{c * I.v[0], c * I.v[1], c * I.v[2], c * I.v[3]}};
    if (g_plant == kSeparateShapes) {
      layer = over(layer, FS);
    } else {
      for (int k = 0; k < 4; k++) {
        total.v[k] += FS.v[k];
      }
    }
  }
  return g_plant == kSeparateShapes ? layer : over(layer, total);
}

/** Draw the operations from @p pos up to the matching pop (consumed) or the end onto @p layer, for the given copies. */
inline Rgba draw_items(const Env & e, size_t & pos, const CopyList & copies, Rgba layer, Clips & clips) {
  const pv::VPattern & p = *e.p;
  while (pos < (size_t)p.op_count) {
    const pv::VOp & op = p.ops[pos++];
    switch (op.kind) {
      case 1:
        layer = draw_fill(e, op, copies, layer, clips);
        break;
      case 2: {  // a group: one over all the copies
        if (!clips.empty()) {
          throw std::logic_error("a group pushed while a clip is in force is not defined");
        }
        size_t start = pos, end = pos;
        auto once = [&](const CopyList & cl) {
          size_t q = start;
          Clips none;
          Rgba G = draw_items(e, q, cl, zero(), none);
          Rgba S{{op.opacity * G.v[0], op.opacity * G.v[1], op.opacity * G.v[2], op.opacity * G.v[3]}};
          layer = apply(layer, S, 1.0, op.composite);
          end = q;
        };
        if (g_plant == kGroupPerCopy && !copies.empty()) {
          for (const Copy * cp : copies) {
            once(CopyList{cp});
          }
        } else {
          once(copies);
        }
        pos = end;
        break;
      }
      case 4:
        clips.push_back(&op);
        layer = draw_items(e, pos, copies, layer, clips);
        clips.pop_back();
        break;
      default:  // 3, 5: the matching pop
        return layer;
    }
  }
  return layer;
}

inline bool has_blend(const pv::VPattern & p) {
  for (int k = 0; k < p.op_count; k++) {
    if (p.ops[k].kind == 2 && p.ops[k].composite != 1) {
      return true;
    }
  }
  return false;
}

inline int64_t floor_i(double v) {
  int64_t k = (int64_t)v;
  return (double)k > v ? k - 1 : k;
}

inline int64_t ceil_i(double v) {
  int64_t k = (int64_t)v;
  return (double)k < v ? k + 1 : k;
}

inline std::vector<Copy> enumerate_copies(const pv::VPattern & p, const Ctx & ctx) {
  double minx = 0, maxx = 0, miny = 0, maxy = 0;
  for (size_t k = 0; k < ctx.pixel.size(); k++) {
    Pt q = map_pt(ctx.Cinv, ctx.pixel[k]);
    if (k == 0 || q.x < minx) minx = q.x;
    if (k == 0 || q.x > maxx) maxx = q.x;
    if (k == 0 || q.y < miny) miny = q.y;
    if (k == 0 || q.y > maxy) maxy = q.y;
  }
  const double x = p.cell[0], y = p.cell[1], w = p.cell[2], h = p.cell[3], xs = p.step[0], ys = p.step[1];
  int64_t ext = g_plant == kContentNotClipped ? 2 : 1;  // one extra index either side; the area test drops what misses
  int64_t i0 = 0, i1 = 0, j0 = 0, j1 = 0;
  if (p.repeat[0]) {
    i0 = ceil_i((minx - x - w) / xs) - ext;
    i1 = floor_i((maxx - x) / xs) + ext;
  }
  if (p.repeat[1]) {
    j0 = ceil_i((miny - y - h) / ys) - ext;
    j1 = floor_i((maxy - y) / ys) + ext;
  }
  std::vector<Copy> out;
  for (int64_t j = j0; j <= j1; j++) {
    for (int64_t i = i0; i <= i1; i++) {
      Copy cp;
      cp.i = i;
      cp.j = j;
      cp.tx = translation_axis(xs, i);
      cp.ty = translation_axis(ys, j);
      if (g_plant == kContentNotClipped) {
        cp.cellpix = ctx.pixel;
        out.push_back(cp);
        continue;
      }
      double edges[4];
      cell(p.cell, p.step, i, j, edges);
      Poly quad = ccw(Poly{map_pt(ctx.C, Pt{edges[0], edges[1]}), map_pt(ctx.C, Pt{edges[2], edges[1]}),
          map_pt(ctx.C, Pt{edges[2], edges[3]}), map_pt(ctx.C, Pt{edges[0], edges[3]})});
      if (quad.empty()) {
        continue;
      }
      Poly cp_poly = convex_clip(ctx.pixel, quad);
      if (!(area(cp_poly) > 0.0)) {
        continue;
      }
      cp.cellpix = ccw(cp_poly);
      out.push_back(cp);
    }
  }
  return out;
}

/** The lattice picture at the pixel (PT6, PT7, PT8). */
inline Rgba lattice_layer(const Env & e, const std::vector<Copy> & all) {
  const pv::VPattern & p = *e.p;
  Rgba layer = g_plant == kTileNotIsolated ? Rgba{{0.2, 0.1, 0.05, 0.5}} : zero();
  CopyList copies;
  for (const Copy & c : all) {
    copies.push_back(&c);
  }
  Clips clips;
  size_t pos = 0;
  if (!overlaps(p)) {
    return draw_items(e, pos, copies, layer, clips);
  }
  bool general = has_blend(p);
  if (g_plant == kReverseLatticeOrder) {
    copies.assign(copies.rbegin(), copies.rend());
  }
  for (const Copy * cp : copies) {
    pos = 0;
    Clips none;
    if (general) {
      layer = draw_items(e, pos, CopyList{cp}, layer, none);
    } else {
      layer = over(layer, draw_items(e, pos, CopyList{cp}, zero(), none));
    }
  }
  return layer;
}

/** The premultiplied value of the pattern use with the chain map @p C at the pixel (PT4, PT9). */
inline Rgba pattern_pixel(const pv::VPattern & p, const Map & C, int px, int py, const double fg[4]) {
  if (degenerate(p) || det(C) == 0.0) {
    return zero();
  }
  Ctx ctx;
  ctx.C = C;
  ctx.Cinv = invert(C);
  ctx.px = px;
  ctx.py = py;
  ctx.pixel = Poly{Pt{(double)px, (double)py}, Pt{(double)px + 1.0, (double)py}, Pt{(double)px + 1.0, (double)py + 1.0},
      Pt{(double)px, (double)py + 1.0}};
  for (int k = 0; k < 4; k++) {
    ctx.fg[k] = fg[k];
  }
  Env e{&p, &ctx};
  Rgba L = lattice_layer(e, enumerate_copies(p, ctx));
  if (!p.has_tint) {
    return Rgba{{p.alpha * L.v[0], p.alpha * L.v[1], p.alpha * L.v[2], p.alpha * L.v[3]}};
  }
  if (g_plant == kKeepsTileColors) {
    double k = p.tint[3] * p.alpha;
    return Rgba{{k * L.v[0], k * L.v[1], k * L.v[2], k * L.v[3]}};
  }
  double alpha = L.v[3] * p.tint[3] * p.alpha;
  return Rgba{{p.tint[0] * alpha, p.tint[1] * alpha, p.tint[2] * alpha, alpha}};
}

/** The premultiplied value of pixel (px, py) of the scene. */
inline Rgba scene_pixel(const pv::VScene & s, int px, int py) {
  return pattern_pixel(*s.pattern, map_of(s.pattern->map), px, py, s.foreground);
}

/** The largest absolute difference, over the pixels and the four components, between the evaluator and the scene's expected values. */
inline double scene_max_error(const pv::VScene & s) {
  double worst = 0.0;
  for (int py = 0; py < s.height; py++) {
    for (int px = 0; px < s.width; px++) {
      Rgba v = scene_pixel(s, px, py);
      const double * want = s.pixels + (py * s.width + px) * 4;
      for (int k = 0; k < 4; k++) {
        double d = v.v[k] - want[k];
        d = d < 0.0 ? -d : d;
        if (d > worst) {
          worst = d;
        }
      }
    }
  }
  return worst;
}

}  // namespace pattern_ref

#endif
