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

#include <algorithm>
#include <cstddef>
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
  double lin[3] = {decode(a, in[0]), decode(a, in[1]), decode(a, in[2])};
  if (a.primaries != b.primaries) {
    Mat ma, mb, mbi;
    rgb_to_xyz(a.primaries, ma);
    rgb_to_xyz(b.primaries, mb);
    inverse3(mb, mbi);
    double xyz[3], moved[3];
    mul3(ma, lin, xyz);
    mul3(mbi, xyz, moved);
    for (int i = 0; i < 3; i++) {
      lin[i] = moved[i];
    }
  }
  for (int i = 0; i < 3; i++) {
    out[i] = encode(b, lin[i]);
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
  double straight[3] = {p.v[0] / p.v[3], p.v[1] / p.v[3], p.v[2] / p.v[3]};
  double c[3];
  convert(straight, a, b, c);
  return Px{{c[0] * p.v[3], c[1] * p.v[3], c[2] * p.v[3], p.v[3]}};
}

/** N4. */
inline Px op(int composite, const Px & s, const Px & d) {
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
    out.v[i] = (1 - m) * d.v[i] + m * o.v[i];
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

}  // namespace refc

#endif /* GHOTI_IO_GSHSCENE_TEST_REF_COMPOSITE_H */
