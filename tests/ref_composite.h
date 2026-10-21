/**
 * @file
 *
 * A deliberately simple evaluator of a closed list in doubles (N1 to N10 of
 * documentation/design.md), for the tests: one pixel, the coverage of every fill and
 * clip push given, the transfer function evaluated with `math`.  It is the shape
 * story 13's shape-cpu test takes: the list says what to draw and this says what the
 * definition makes of it, to be compared with the exact answers of
 * tools/composite-oracle.py (tests/composite_vectors.h).
 *
 * Nothing here is part of the library: the scene never evaluates a color.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#ifndef GHOTI_IO_GSHSCENE_TEST_REF_COMPOSITE_H
#define GHOTI_IO_GSHSCENE_TEST_REF_COMPOSITE_H

#include <ghoti.io/shape-scene/shape-scene.h>

#include <ghoti.io/math/math.h>
#include <ghoti.io/shape-geometry/orient.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <utility>
#include <vector>

namespace refc {

struct Space {
  int primaries;
  int transfer;
  double gamma;
};

inline Space from(const GSHSCENE_Color_Space & s) {
  return Space{(int)s.primaries, (int)s.transfer, s.gamma};
}

inline bool is_target(const Space & s) {
  return s.primaries == 0 && s.transfer == 0;
}

inline bool same(const Space & a, const Space & b) {
  return a.primaries == b.primaries && a.transfer == b.transfer && a.gamma == b.gamma;
}

struct Chroma {
  double x, y;
};

/** N1: red, green, blue of a gamut (CIE xy); the white is D65 for all four. */
inline void gamut(int primaries, Chroma out[3]) {
  switch (primaries) {
    case 1:
      out[0] = {0.64, 0.33}; out[1] = {0.30, 0.60}; out[2] = {0.15, 0.06};
      break;
    case 2:
      out[0] = {0.64, 0.33}; out[1] = {0.21, 0.71}; out[2] = {0.15, 0.06};
      break;
    case 3:
      out[0] = {0.680, 0.320}; out[1] = {0.265, 0.690}; out[2] = {0.150, 0.060};
      break;
    default:
      out[0] = {0.708, 0.292}; out[1] = {0.170, 0.797}; out[2] = {0.131, 0.046};
      break;
  }
}

typedef double Mat[3][3];

inline double det3(const Mat m) {
  return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
         m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

inline void inverse3(const Mat m, Mat out) {
  double d = det3(m);
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      int r0 = (j + 1) % 3, r1 = (j + 2) % 3, c0 = (i + 1) % 3, c1 = (i + 2) % 3;
      out[i][j] = (m[r0][c0] * m[r1][c1] - m[r0][c1] * m[r1][c0]) / d;
    }
  }
}

inline void mul3(const Mat m, const double v[3], double out[3]) {
  for (int i = 0; i < 3; i++) {
    out[i] = m[i][0] * v[0] + m[i][1] * v[1] + m[i][2] * v[2];
  }
}

/** N3: the matrix from linear RGB to XYZ of a gamut. */
inline void rgb_to_xyz(int primaries, Mat out) {
  Chroma c[3];
  gamut(primaries, c);
  Mat p;
  for (int i = 0; i < 3; i++) {
    p[0][i] = c[i].x / c[i].y;
    p[1][i] = 1.0;
    p[2][i] = (1.0 - c[i].x - c[i].y) / c[i].y;
  }
  const double wx = 0.3127, wy = 0.3290;
  const double white[3] = {wx / wy, 1.0, (1.0 - wx - wy) / wy};
  Mat pi;
  inverse3(p, pi);
  double s[3];
  mul3(pi, white, s);
  for (int r = 0; r < 3; r++) {
    for (int k = 0; k < 3; k++) {
      out[r][k] = s[k] * p[r][k];
    }
  }
}

inline double sign_of(double x) {
  return x < 0 ? -1.0 : 1.0;
}

/** N1: sat(x), x clamped to [-2^64, 2^64]. */
inline double sat(double x) {
  const double k = 0x1p64;
  return x > k ? k : x < -k ? -k : x;
}

/** N1: sat(c / a) for a != 0, with no infinity formed: past the bound the quotient is not taken. */
inline double unpremultiply(double c, double a) {
  const double k = 0x1p64;
  double m = a < 0 ? -a : a;
  if ((c < 0 ? -c : c) >= k * m) {
    return (c < 0) != (a < 0) ? -k : k;
  }
  return c / a;
}

/** N2. */
inline double decode(const Space & s, double x) {
  if (s.transfer == 1) {
    return x;
  }
  if (s.transfer == 2) {
    return x <= 0.04045 ? x / 12.92 : gmath_pow((x + 0.055) / 1.055, 2.4);
  }
  return x == 0.0 ? 0.0 : sign_of(x) * gmath_pow(x < 0 ? -x : x, s.gamma);
}

inline double encode(const Space & s, double y) {
  if (s.transfer == 1) {
    return y;
  }
  if (s.transfer == 2) {
    return y <= 0.0031308 ? y * 12.92 : 1.055 * gmath_pow(y, 1.0 / 2.4) - 0.055;
  }
  return y == 0.0 ? 0.0 : sign_of(y) * gmath_pow(y < 0 ? -y : y, 1.0 / s.gamma);
}

/** N3: a straight color from space a to space b (both concrete). */
inline void convert(const double in[3], const Space & a, const Space & b, double out[3]) {
  if (same(a, b)) {
    for (int i = 0; i < 3; i++) {
      out[i] = in[i];
    }
    return;
  }
  double lin[3] = {sat(decode(a, in[0])), sat(decode(a, in[1])), sat(decode(a, in[2]))};
  if (a.primaries != b.primaries) {
    Mat ma, mb, mbi;
    rgb_to_xyz(a.primaries, ma);
    rgb_to_xyz(b.primaries, mb);
    inverse3(mb, mbi);
    double xyz[3], moved[3];
    mul3(ma, lin, xyz);
    mul3(mbi, xyz, moved);
    for (int i = 0; i < 3; i++) {
      lin[i] = sat(moved[i]);
    }
  }
  for (int i = 0; i < 3; i++) {
    out[i] = sat(encode(b, lin[i]));
  }
}

struct Px {
  double v[4];
};

inline Px convert_premultiplied(const Px & p, const Space & a, const Space & b) {
  if (same(a, b)) {
    return p;
  }
  if (p.v[3] == 0.0) {
    return Px{{0, 0, 0, 0}};
  }
  double straight[3] = {unpremultiply(p.v[0], p.v[3]), unpremultiply(p.v[1], p.v[3]), unpremultiply(p.v[2], p.v[3])};
  double c[3];
  convert(straight, a, b, c);
  return Px{{c[0] * p.v[3], c[1] * p.v[3], c[2] * p.v[3], p.v[3]}};
}

/* --- B1 to B12: the blend modes, in S11's order of operations (shape-cpu's render.h) ---------------------------------- */

inline bool is_blend(int composite) {
  return composite >= GSHSCENE_COMPOSITE_SCREEN && composite <= GSHSCENE_COMPOSITE_LUMINOSITY;
}

/** B9: n / d, taken as sign * 2^66 once |n| >= 2^66 |d| (d != 0), so it is never formed past the bound. */
inline double quotient(double n, double d) {
  const double cap = 0x1p66;
  double mn = n < 0 ? -n : n, md = d < 0 ? -d : d;
  if (mn >= cap * md) {
    return (n < 0) != (d < 0) ? -cap : cap;
  }
  return n / d;
}

inline double mix_screen(double b, double s) {
  return b + s - b * s;
}

inline double mix_hard_light(double b, double s) {
  return s <= 0.5 ? b * (2.0 * s) : mix_screen(b, 2.0 * s - 1.0);
}

/** B7: one channel of a separable mode. */
inline double mix_separable(int mode, double b, double s) {
  switch (mode) {
    case GSHSCENE_COMPOSITE_MULTIPLY: return b * s;
    case GSHSCENE_COMPOSITE_SCREEN: return mix_screen(b, s);
    case GSHSCENE_COMPOSITE_OVERLAY: return mix_hard_light(s, b);
    case GSHSCENE_COMPOSITE_DARKEN: return b < s ? b : s;
    case GSHSCENE_COMPOSITE_LIGHTEN: return b > s ? b : s;
    case GSHSCENE_COMPOSITE_COLOR_DODGE: {
      if (b == 0.0) return 0.0;
      if (s == 1.0) return 1.0;
      double q = quotient(b, 1.0 - s);
      return q < 1.0 ? q : 1.0;
    }
    case GSHSCENE_COMPOSITE_COLOR_BURN: {
      if (b == 1.0) return 1.0;
      if (s == 0.0) return 0.0;
      double q = quotient(1.0 - b, s);
      return 1.0 - (q < 1.0 ? q : 1.0);
    }
    case GSHSCENE_COMPOSITE_HARD_LIGHT: return mix_hard_light(b, s);
    case GSHSCENE_COMPOSITE_SOFT_LIGHT: {
      if (s <= 0.5) return b - (1.0 - 2.0 * s) * b * (1.0 - b);
      double d = b <= 0.25 ? ((16.0 * b - 12.0) * b + 4.0) * b : gmath_sqrt(b);
      return b + (2.0 * s - 1.0) * (d - b);
    }
    case GSHSCENE_COMPOSITE_DIFFERENCE: return b - s < 0 ? s - b : b - s;
    default: return b + s - 2.0 * b * s; /* exclusion */
  }
}

inline double lum(const double c[3]) {
  return 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2];
}

/** B8: ClipColor, with L, n and x read once on entry; the all-equal (0/0) form, n = x, leaves C alone (B10). */
inline void clip_color(double c[3]) {
  double l = lum(c);
  double n = std::min(c[0], std::min(c[1], c[2]));
  double x = std::max(c[0], std::max(c[1], c[2]));
  if (n < 0.0 && n != x) {
    for (int i = 0; i < 3; i++) c[i] = l + (c[i] - l) * l / (l - n);
  }
  if (x > 1.0 && n != x) {
    for (int i = 0; i < 3; i++) c[i] = l + (c[i] - l) * (1.0 - l) / (x - l);
  }
}

inline void set_lum(double c[3], double l) {
  double d = l - lum(c);
  for (int i = 0; i < 3; i++) c[i] += d;
  clip_color(c);
}

inline double sat_of(const double c[3]) {
  return std::max(c[0], std::max(c[1], c[2])) - std::min(c[0], std::min(c[1], c[2]));
}

inline void set_sat(double c[3], double s) {
  int i[3] = {0, 1, 2};
  if (c[i[0]] > c[i[1]]) std::swap(i[0], i[1]);
  if (c[i[1]] > c[i[2]]) std::swap(i[1], i[2]);
  if (c[i[0]] > c[i[1]]) std::swap(i[0], i[1]);
  double out[3] = {0, 0, 0};
  if (c[i[2]] > c[i[0]]) {
    out[i[1]] = (c[i[1]] - c[i[0]]) * s / (c[i[2]] - c[i[0]]);
    out[i[2]] = s;
  }
  for (int k = 0; k < 3; k++) c[k] = out[k];
}

/** B7 to B9: B(Cb, Cs) of a blend mode, saturated. */
inline void mix(int mode, const double cb[3], const double cs[3], double out[3]) {
  double c[3];
  switch (mode) {
    case GSHSCENE_COMPOSITE_HUE:
      std::memcpy(c, cs, sizeof c);
      set_sat(c, sat_of(cb));
      set_lum(c, lum(cb));
      break;
    case GSHSCENE_COMPOSITE_SATURATION:
      std::memcpy(c, cb, sizeof c);
      set_sat(c, sat_of(cs));
      set_lum(c, lum(cb));
      break;
    case GSHSCENE_COMPOSITE_COLOR:
      std::memcpy(c, cs, sizeof c);
      set_lum(c, lum(cb));
      break;
    case GSHSCENE_COMPOSITE_LUMINOSITY:
      std::memcpy(c, cb, sizeof c);
      set_lum(c, lum(cs));
      break;
    default:
      for (int k = 0; k < 3; k++) c[k] = mix_separable(mode, cb[k], cs[k]);
      break;
  }
  for (int k = 0; k < 3; k++) out[k] = sat(c[k]);
}

/** B2: op_X(S, D), premultiplied. */
inline Px blend(int mode, const Px & s, const Px & d) {
  double sa = s.v[3], da = d.v[3];
  double b[3] = {0, 0, 0};
  if (sa * da != 0.0) {
    double cs[3], cb[3];
    for (int k = 0; k < 3; k++) {
      cs[k] = unpremultiply(s.v[k], sa);
      cb[k] = unpremultiply(d.v[k], da);
    }
    mix(mode, cb, cs, b);
  }
  Px out;
  for (int k = 0; k < 3; k++) out.v[k] = s.v[k] * (1.0 - da) + d.v[k] * (1.0 - sa) + sa * da * b[k];
  out.v[3] = sa + da * (1.0 - sa);
  return out;
}

/** N4, and B2 for a blend mode. */
inline Px op(int composite, const Px & s, const Px & d) {
  if (is_blend(composite)) {
    return blend(composite, s, d);
  }
  double sa = s.v[3], da = d.v[3];
  double fa = 0, fb = 0;
  switch (composite) {
    case GSHSCENE_COMPOSITE_CLEAR: fa = 0; fb = 0; break;
    case GSHSCENE_COMPOSITE_SRC: fa = 1; fb = 0; break;
    case GSHSCENE_COMPOSITE_DEST: fa = 0; fb = 1; break;
    case GSHSCENE_COMPOSITE_SRC_OVER: fa = 1; fb = 1 - sa; break;
    case GSHSCENE_COMPOSITE_DEST_OVER: fa = 1 - da; fb = 1; break;
    case GSHSCENE_COMPOSITE_SRC_IN: fa = da; fb = 0; break;
    case GSHSCENE_COMPOSITE_DEST_IN: fa = 0; fb = sa; break;
    case GSHSCENE_COMPOSITE_SRC_OUT: fa = 1 - da; fb = 0; break;
    case GSHSCENE_COMPOSITE_DEST_OUT: fa = 0; fb = 1 - sa; break;
    case GSHSCENE_COMPOSITE_SRC_ATOP: fa = da; fb = 1 - sa; break;
    case GSHSCENE_COMPOSITE_DEST_ATOP: fa = 1 - da; fb = sa; break;
    case GSHSCENE_COMPOSITE_XOR: fa = 1 - da; fb = 1 - sa; break;
    default: break;
  }
  Px out;
  for (int i = 0; i < 4; i++) {
    out.v[i] = composite == GSHSCENE_COMPOSITE_PLUS ? std::min(1.0, s.v[i] + d.v[i]) : fa * s.v[i] + fb * d.v[i];
  }
  return out;
}

/** N5. */
inline Px apply(const Px & d, const Px & s, double m, int composite) {
  Px o = op(composite, s, d);
  Px out;
  for (int i = 0; i < 4; i++) {
    out.v[i] = sat((1 - m) * d.v[i] + m * o.v[i]);
  }
  return out;
}

struct Frame {
  Px layer;
  Space space;
  std::vector<double> clips;
  GSHSCENE_Group group;
  double outer;
};

inline double product(const std::vector<double> & v) {
  double p = 1.0;
  for (double x : v) {
    p *= x;
  }
  return p;
}

/**
 * Evaluate a closed list (flat order, no instances): @p coverage is the coverage of each item by index (read for fills
 * and clip pushes), @p target the concrete space of the root layer, @p backdrop its initial premultiplied value.
 */
inline Px evaluate(const GSHSCENE_List * list, const std::vector<double> & coverage, const Space & target, const Px & backdrop) {
  GSHSCENE_Color fgc;
  gshscene_list_foreground(list, &fgc);
  std::vector<Frame> stack;
  Frame root;
  root.layer = backdrop;
  root.space = target;
  root.outer = 1.0;
  std::memset(&root.group, 0, sizeof root.group);
  stack.push_back(root);
  for (size_t i = 0; i < gshscene_list_count(list); i++) {
    const GSHSCENE_Item * item = nullptr;
    gshscene_list_item(list, i, &item);
    Frame & top = stack.back();
    switch (item->kind) {
      case GSHSCENE_ITEM_FILL: {
        double rgb[3], alpha;
        Space space;
        if (item->paint.kind == GSHSCENE_PAINT_FOREGROUND) {
          for (int k = 0; k < 3; k++) {
            rgb[k] = fgc.rgba[k];
          }
          alpha = fgc.rgba[3] * item->paint.rgba[3];
          space = from(fgc.space);
        } else if (item->paint.kind == GSHSCENE_PAINT_PALETTE) {
          // N14: the entry's color, its alpha scaled; a missing entry is refused and draws nothing.
          const GSHSCENE_Resource * palette = gshscene_list_palette(list);
          GSHSCENE_Color e;
          if (!palette || item->paint.entry >= gshscene_resource_palette_count(palette) ||
              gshscene_resource_palette_color(palette, item->paint.entry, &e) != GSHSCENE_OK) {
            break;
          }
          for (int k = 0; k < 3; k++) {
            rgb[k] = e.rgba[k];
          }
          alpha = e.rgba[3] * item->paint.rgba[3];
          space = from(e.space);
        } else {
          for (int k = 0; k < 3; k++) {
            rgb[k] = item->paint.rgba[k];
          }
          alpha = item->paint.rgba[3];
          space = from(item->paint.space);
        }
        if (is_target(space)) {
          space = target;
        }
        double c[3];
        convert(rgb, space, top.space, c);
        Px s = {{c[0] * alpha, c[1] * alpha, c[2] * alpha, alpha}};
        top.layer = apply(top.layer, s, coverage[i] * product(top.clips), GSHSCENE_COMPOSITE_SRC_OVER);
        break;
      }
      case GSHSCENE_ITEM_CLIP_PUSH:
        top.clips.push_back(coverage[i]);
        break;
      case GSHSCENE_ITEM_CLIP_POP:
        top.clips.pop_back();
        break;
      case GSHSCENE_ITEM_GROUP_PUSH: {
        Frame f;
        f.layer = Px{{0, 0, 0, 0}};
        Space blend = from(item->group.blend_space);
        f.space = is_target(blend) ? top.space : blend;
        f.group = item->group;
        f.outer = product(top.clips);
        stack.push_back(f);
        break;
      }
      case GSHSCENE_ITEM_GROUP_POP: {
        Frame done = stack.back();
        stack.pop_back();
        Frame & parent = stack.back();
        Px moved = convert_premultiplied(done.layer, done.space, parent.space);
        Px s = {{done.group.opacity * moved.v[0], done.group.opacity * moved.v[1], done.group.opacity * moved.v[2],
            done.group.opacity * moved.v[3]}};
        parent.layer = apply(parent.layer, s, done.outer, (int)done.group.composite);
        break;
      }
      default:
        break;
    }
  }
  return stack.back().layer;
}

/* --- gradients (G4 to G11; color-paint story P1) --------------------------------------------------------------------- */

/** A stop of a resolved color line: its offset and straight rgba in the interpolation space. */
struct GStop {
  double offset;
  double rgba[4];
};

/** A 2D map: x' = m[0] x + m[1] y + m[4], y' = m[2] x + m[3] y + m[5]. */
struct GMap {
  double m[6];
};

/** What a gradient paints at a point: drawn (0: G8's outside, G10, G11, with reason 1, 2 or 3), t, straight rgba. */
struct GPoint {
  int drawn = 0;
  int reason = 0;
  double t = 0.0;
  double rgba[4] = {0, 0, 0, 0};
};

/** G11: an exactly zero determinant, by shape-geometry's exact orientation of the columns. */
inline bool g_singular(const GMap & m) {
  GSHGEOM_Point o = {0, 0, 0};
  GSHGEOM_Point a = {m.m[0], m.m[2], 0};
  GSHGEOM_Point b = {m.m[1], m.m[3], 0};
  int sign = 1;
  gshgeom_orient2d(o, a, b, &sign);
  return sign == 0;
}

/** p = m^-1 (q), the matrix scaled by its largest entry first so a tiny or huge scale neither under- nor overflows. */
inline void g_unmap(const GMap & m, double & x, double & y) {
  double s = 0.0;
  for (int i = 0; i < 4; i++) {
    s = std::max(s, m.m[i] < 0 ? -m.m[i] : m.m[i]);
  }
  double a = m.m[0] / s, b = m.m[1] / s, c = m.m[2] / s, d = m.m[3] / s;
  double det = a * d - b * c;
  double u = x - m.m[4], v = y - m.m[5];
  x = (d * u - b * v) / det / s;
  y = (-c * u + a * v) / det / s;
}

inline void g_interpolant(const GStop & s, int premul, double out[4]) {
  double a = s.rgba[3];
  for (int k = 0; k < 3; k++) {
    out[k] = premul ? s.rgba[k] * a : s.rgba[k];
  }
  out[3] = a;
}

/** G4: C(t) for t in [o_first, o_last], at least two stops, o_first < o_last. */
inline void g_line(const std::vector<GStop> & stops, int premul, double t, double out[4]) {
  if (!(t < stops.back().offset)) {
    g_interpolant(stops.back(), premul, out);
    return;
  }
  size_t j = 0;
  while (j < stops.size() && !(t < stops[j].offset)) {
    j++;
  }
  size_t i = j - 1;
  double a[4], b[4];
  g_interpolant(stops[i], premul, a);
  g_interpolant(stops[j], premul, b);
  double f = (t - stops[i].offset) / (stops[j].offset - stops[i].offset);
  for (int k = 0; k < 4; k++) {
    out[k] = a[k] + f * (b[k] - a[k]);
  }
}

/** G4, G5: the interpolant at t; false where nothing is drawn. */
inline bool g_extended(const std::vector<GStop> & stops, int extend, int premul, double t, double out[4]) {
  size_t n = stops.size();
  if (n == 0) {
    out[0] = out[1] = out[2] = out[3] = 0.0;
    return true;
  }
  if (n == 1) {
    g_interpolant(stops[0], premul, out);
    return true;
  }
  double o0 = stops.front().offset, o1 = stops.back().offset, period = o1 - o0;
  if (period == 0.0) {
    if (extend != GSHSCENE_EXTEND_PAD) {
      return false;
    }
    g_interpolant(t < o0 ? stops.front() : stops.back(), premul, out);
    return true;
  }
  if (t < o0 || t > o1) {
    if (extend == GSHSCENE_EXTEND_PAD) {
      g_interpolant(t < o0 ? stops.front() : stops.back(), premul, out);
      return true;
    }
    if (extend == GSHSCENE_EXTEND_REPEAT) {
      t = o0 + (t - o0 - std::floor((t - o0) / period) * period);
    } else {
      double m = t - o0 - std::floor((t - o0) / (2 * period)) * (2 * period);
      t = o0 + period - std::fabs(m - period);
    }
  }
  g_line(stops, premul, t, out);
  return true;
}

/**
 * The gradient's straight color at target point (qx, qy) (G6 to G12), in doubles: the chain's maps (target first,
 * gradient last), the stops already resolved into the interpolation space (sorted), the paint's alpha.
 */
inline GPoint gradient_at(int kind, const double geom[6], int unit, int extend, int premul,
    const std::vector<GStop> & stops, const std::vector<GMap> & chain, double qx, double qy, double alpha) {
  GPoint r;
  for (const GMap & m : chain) {
    if (g_singular(m)) {
      r.reason = 3;
      return r;
    }
  }
  double x = qx, y = qy;
  for (const GMap & m : chain) {
    g_unmap(m, x, y);
  }
  double t = 0.0;
  if (kind == GSHSCENE_GRADIENT_LINEAR) {
    double vx = geom[2] - geom[0], vy = geom[3] - geom[1];
    double nx = vx, ny = vy;
    if (geom[4] != 0.0 || geom[5] != 0.0) {
      nx = -geom[5];
      ny = geom[4];
    }
    double den = vx * nx + vy * ny;
    if (den == 0.0) {
      r.reason = 2;
      return r;
    }
    t = ((x - geom[0]) * nx + (y - geom[1]) * ny) / den;
  } else if (kind == GSHSCENE_GRADIENT_RADIAL) {
    double c0x = geom[0], c0y = geom[1], r0 = geom[2], c1x = geom[3], c1y = geom[4], r1 = geom[5];
    if (c0x == c1x && c0y == c1y && r0 == r1) {
      r.reason = 2;
      return r;
    }
    double dcx = c1x - c0x, dcy = c1y - c0y, dr = r1 - r0, px = x - c0x, py = y - c0y;
    double a = dcx * dcx + dcy * dcy - dr * dr;
    double b = px * dcx + py * dcy + r0 * dr;
    double c = px * px + py * py - r0 * r0;
    double w[2];
    int nw = 0;
    if (a == 0.0) {
      if (b != 0.0) {
        w[nw++] = c / (2 * b);
      }
    } else {
      double disc = b * b - a * c;
      if (disc >= 0.0) {
        double sq = gmath_sqrt(disc);
        double hi = (b + sq) / a, lo = (b - sq) / a;
        if (hi < lo) {
          std::swap(hi, lo);
        }
        w[nw++] = hi;
        w[nw++] = lo;
      }
    }
    bool found = false;
    for (int i = 0; i < nw && !found; i++) {
      if (r0 + w[i] * dr > 0.0) {
        t = w[i];
        found = true;
      }
    }
    if (!found) {
      r.reason = 1;
      return r;
    }
  } else {
    double dx = x - geom[0], dy = y - geom[1];
    double per_turn = unit == GSHGEOM_ANGLE_DEGREES ? 360.0 : unit == GSHGEOM_ANGLE_GRADIANS ? 400.0
        : unit == GSHGEOM_ANGLE_TURNS ? 1.0 : 2.0 * 3.141592653589793238462643383279502884;
    double theta;
    if (dy == 0.0 && dx >= 0.0) {
      theta = 0.0;
    } else if (dx == 0.0) {
      theta = per_turn * (dy > 0 ? 0.25 : 0.75);
    } else if (dy == 0.0) {
      theta = per_turn * 0.5;
    } else {
      double rad = gmath_atan2(dy, dx);
      const double two_pi = 2.0 * 3.141592653589793238462643383279502884;
      if (rad < 0) {
        rad += two_pi;
      }
      theta = unit == GSHGEOM_ANGLE_RADIANS ? rad : rad / two_pi * per_turn;
    }
    double a0 = geom[2], a1 = geom[3];
    if (a0 == a1) {
      if (extend != GSHSCENE_EXTEND_PAD) {
        r.reason = 2;
        return r;
      }
      double c[4] = {0, 0, 0, 0};
      if (!stops.empty()) {
        g_interpolant(theta < a0 ? stops.front() : stops.back(), premul, c);
      }
      r.drawn = 1;
      r.t = 0.0;
      for (int k = 0; k < 4; k++) {
        r.rgba[k] = c[k];
      }
      goto finish;
    }
    t = (theta - a0) / (a1 - a0);
  }
  {
    double c[4];
    if (!g_extended(stops, extend, premul, t, c)) {
      r.reason = 2;
      return r;
    }
    r.drawn = 1;
    r.t = t;
    for (int k = 0; k < 4; k++) {
      r.rgba[k] = c[k];
    }
  }
finish:
  if (premul) {
    double a = r.rgba[3];
    if (a == 0.0) {
      r.rgba[0] = r.rgba[1] = r.rgba[2] = 0.0;
    } else {
      for (int k = 0; k < 3; k++) {
        r.rgba[k] = unpremultiply(r.rgba[k], a);
      }
    }
  }
  r.rgba[3] = r.rgba[3] * alpha;
  return r;
}

}  // namespace refc

#endif /* GHOTI_IO_GSHSCENE_TEST_REF_COMPOSITE_H */
