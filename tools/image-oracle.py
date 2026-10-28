#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-only
#
# Copyright (C) 2026 Corey Pennycuff
#
# This file is part of Ghoti.io Shape Scene.
#
# Ghoti.io Shape Scene is free software: you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public License version 3
# as published by the Free Software Foundation.
"""The reference of the scene's image sampling (rules IP2 to IP9, IP12 and IP16; image.h's IM rules for the images).

A slow, independent statement of what an image paint contributes at a point, or over a region, sharing no code
with the library or with any other oracle.  Every input double is taken as the exact rational it is.

- **Decode** (IP4): an unsigned normalized sample of n bits is v / (2^n - 1); a float32 sample is that float,
  saturated to [-2^64, 2^64] (alpha never); a gray pixel (g, g, g); alpha 1 when the layout has none; the
  premultiplied value of a texel in its own space is (a r, a g, a b, a) for the straight form and the decoded
  (R, G, B, a) as stored for the premultiplied form.  An alpha-only texel is the single number a, held here as
  (0, 0, 0, a) so that every operation is componentwise.
- **Filter space and conversion** (IP5), in the stated order: the texel in the filter space F (itself when F
  equals the image's space as a value, else un-premultiplied with 0 for alpha 0, saturated, converted, saturated,
  premultiplied again); the filter on those premultiplied values; the result taken to the layer space L as a
  premultiplied value (un-premultiplied, saturated, converted, premultiplied; alpha 0 gives 0) unless F equals L.
  The paint's alpha multiplier m is not part of the primitive output.
- **Edge maps** (IP6), per axis, on integer taps, with a window; **NEAREST, LINEAR, CUBIC** (IP7) in `Fraction`s,
  the cubic being Mitchell-Netravali with B = C = 1/3 as the rational weights k(x) and clamped componentwise to
  the range of its 16 taps; **AREA** (IP7) as the exact mean of the piecewise-constant extended image over a
  parallelogram: by exact polygon clipping against texel squares, and, for an axis-aligned region, by
  per-axis weights in which whole periods of REPEAT and REFLECT are summed as multiples.
- **Which filter** (IP8): `stretches_every_direction` decides A > 1, C > 1, (A - 1)(C - 1) > B^2 on the exact
  values of the four doubles.
- **Alpha-only** (IP9): the filters act on the one number; `sample` returns (0, 0, 0, a'), the tint is the library's.
- **Transfers and gamuts** (N1 to N3) are in `decimal` at 110 digits.  The gamut matrices are solved from the
  chromaticity coordinates written here in decimal (Cramer's rule and an adjugate inverse); a power is
  Decimal's `**` (exp and ln at the working precision).  **Bound:** every converted component is within
  1e-85 * max(1, |component|) of the real value N2 and N3 define (the selftest recomputes a grid of conversions
  at 170 digits and reports the largest difference it saw; with 110 digits the arithmetic itself contributes about
  1e-105, and the conditioning of the matrices and of the 10th power of a saturated value is what the margin is
  for).  Every other quantity is an exact rational.  A vector's double is that rational (or that bounded decimal
  result) rounded to the nearest double.

`sample(...)` and `area(...)` take the image, the window, the two edge modes, the filter space and the layer space,
and return four `Fraction`s: the premultiplied value in the layer space.  A zero space (the target's) is taken to
be the layer's space here (the layer is the target); the zero layer is for content whose every space is zero.

    python3 tools/image-oracle.py --selftest
    python3 tools/image-oracle.py --selftest --plant NAME

runs it against itself (the identities of IP16).  A `--plant` breaks one rule on purpose, and the selftest must
then fail (exit status 1): `centres-at-0` (linear and cubic put texel centres at integers), `straight-filtering`
(a straight image is filtered unpremultiplied), `determinant-predicate` (IP8 by |det| > 1), `reflect-off-by-one`
(REFLECT without the repeated edge texel), `cubic-no-clamp` (no clamp to the taps' range).
"""
import struct
import sys
from collections import defaultdict
from decimal import Context, Decimal as D, localcontext
from fractions import Fraction as F
from math import ceil, floor

PREC = 110

NEAREST, LINEAR, CUBIC, AREA = 0, 1, 2, 3
PAD, REPEAT, REFLECT, NONE = 0, 1, 2, 3
UNORM, FLOAT = 0, 1
STRAIGHT, PREMULTIPLIED = 1, 2

ZERO = (0, 0, 0.0)            # a space: (primaries, transfer, gamma); the zero value is the target's
SAT = F(2) ** 64
PLANTS = set()
PLANT_NAMES = ("centres-at-0", "straight-filtering", "determinant-predicate", "reflect-off-by-one",
               "cubic-no-clamp")

# CIE xy chromaticities (red, green, blue) and the D65 white, as written in the definition.
GAMUTS = {
    1: (("0.64", "0.33"), ("0.30", "0.60"), ("0.15", "0.06")),
    2: (("0.64", "0.33"), ("0.21", "0.71"), ("0.15", "0.06")),
    3: (("0.680", "0.320"), ("0.265", "0.690"), ("0.150", "0.060")),
    4: (("0.708", "0.292"), ("0.170", "0.797"), ("0.131", "0.046")),
}
WHITE = ("0.3127", "0.3290")


# --- decimal: transfers and gamuts ----------------------------------------------------------------------------------
def _ctx():
    return localcontext(Context(prec=PREC, Emin=-999999, Emax=999999))


def set_precision(p):
    global PREC
    PREC = p
    _MATS.clear()


def _dec(x):
    """A Fraction as a Decimal at the working precision (call inside _ctx)."""
    return D(x.numerator) / D(x.denominator)


def _det3(m):
    return (m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
            + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]))


def _inv3(m):
    d = _det3(m)
    cof = [[(m[(c + 1) % 3][(r + 1) % 3] * m[(c + 2) % 3][(r + 2) % 3]
             - m[(c + 1) % 3][(r + 2) % 3] * m[(c + 2) % 3][(r + 1) % 3]) / d for c in range(3)] for r in range(3)]
    return cof


def _mul3(a, b):
    return [[sum(a[r][k] * b[k][c] for k in range(3)) for c in range(3)] for r in range(3)]


_MATS = {}


def _rgb_to_xyz(g):
    """N3: the matrix from linear RGB to XYZ of gamut g: columns S_c (X_c, 1, Z_c) with [X;1;Z] S = W."""
    if (g, PREC) in _MATS:
        return _MATS[(g, PREC)]
    with _ctx():
        cols = []
        for x, y in GAMUTS[g]:
            x, y = D(x), D(y)
            cols.append((x / y, D(1), (1 - x - y) / y))
        wx, wy = D(WHITE[0]), D(WHITE[1])
        w = (wx / wy, D(1), (1 - wx - wy) / wy)
        mat = [[cols[c][r] for c in range(3)] for r in range(3)]
        d = _det3(mat)
        s = []
        for c in range(3):                       # Cramer: replace column c by W
            mc = [[w[r] if k == c else mat[r][k] for k in range(3)] for r in range(3)]
            s.append(_det3(mc) / d)
        m = [[s[c] * cols[c][r] for c in range(3)] for r in range(3)]
        _MATS[(g, PREC)] = m
        return m


def _gamut_product(a, b):
    """M_B^-1 M_A, formed as one matrix."""
    key = ("prod", a, b, PREC)
    if key not in _MATS:
        with _ctx():
            _MATS[key] = _mul3(_inv3(_rgb_to_xyz(b)), _rgb_to_xyz(a))
    return _MATS[key]


def _sat_d(x):
    m = D(2 ** 64)
    return m if x > m else (-m if x < -m else x)


def _decode(x, sp):
    """N2 decode of a Decimal (inside _ctx), then saturation."""
    t = sp[1]
    if t == 1:
        y = x
    elif t == 2:
        y = x / D("12.92") if x <= D("0.04045") else ((x + D("0.055")) / D("1.055")) ** D("2.4")
    elif t == 3:
        g = D(sp[2])
        y = D(0) if x == 0 else (abs(x) ** g if x > 0 else -(abs(x) ** g))
    else:
        raise ValueError("no transfer")
    return _sat_d(y)


def _encode(y, sp):
    t = sp[1]
    if t == 1:
        x = y
    elif t == 2:
        x = D("12.92") * y if y <= D("0.0031308") else D("1.055") * y ** (D(1) / D("2.4")) - D("0.055")
    elif t == 3:
        g = D(sp[2])
        x = D(0) if y == 0 else (abs(y) ** (D(1) / g) if y > 0 else -(abs(y) ** (D(1) / g)))
    else:
        raise ValueError("no transfer")
    return _sat_d(x)


def convert_straight(rgb, a, b):
    """N2 and N3: a straight colour (three Fractions) from space a to space b (both nonzero), three Fractions.

    Identity (exactly) for equal spaces; equal gamuts skip the matrices; each decode, the matrix result and each
    encode is saturated at 2^64.
    """
    if a == b:
        return tuple(rgb)
    if a == ZERO or b == ZERO:
        raise ValueError("a conversion needs both spaces named")
    with _ctx():
        v = [_decode(_dec(c), a) for c in rgb]
        if a[0] != b[0]:
            t = _gamut_product(a[0], b[0])
            v = [_sat_d(sum(t[r][c] * v[c] for c in range(3))) for r in range(3)]
        v = [_encode(c, b) for c in v]
        return tuple(F(c) for c in v)


# --- images and decode (IP4) ----------------------------------------------------------------------------------------
class Image:
    """An image as image.h describes it: samples are the stored values (a float is its IEEE bit pattern)."""

    def __init__(self, name, width, height, layout, typ, bits, alpha_form, alpha_only, space, samples):
        assert len(samples) == width * height * layout
        self.name, self.w, self.h, self.layout = name, width, height, layout
        self.typ, self.bits, self.alpha_form, self.alpha_only = typ, bits, alpha_form, alpha_only
        self.space = space
        self.samples = list(samples)


def float_bits(x):
    return struct.unpack("<I", struct.pack("<f", x))[0]


def bits_float(v):
    return struct.unpack("<f", struct.pack("<I", v))[0]


def decode_channel(img, v, is_alpha):
    if img.typ == UNORM:
        return F(v, (1 << img.bits) - 1)
    x = F(bits_float(v))
    if is_alpha:
        return x
    return max(-SAT, min(SAT, x))


def texel_own(img, x, y):
    """(premultiplied value in the image's own space, straight (r, g, b, a) or None for alpha-only)."""
    base = (y * img.w + x) * img.layout
    ch = lambda c, al=False: decode_channel(img, img.samples[base + c], al)
    if img.alpha_only:
        a = ch(0, True)
        return (F(0), F(0), F(0), a), None
    if img.layout == 1:
        g = ch(0)
        return (g, g, g, F(1)), (g, g, g, F(1))
    r, g, b = ch(0), ch(1), ch(2)
    if img.layout == 3:
        return (r, g, b, F(1)), (r, g, b, F(1))
    a = ch(3, True)
    if img.alpha_form == STRAIGHT:
        return (a * r, a * g, a * b, a), (r, g, b, a)
    # premultiplied: taken as stored; the straight colour is C / a saturated, 0 for alpha 0
    if a == 0:
        return (r, g, b, a), (F(0), F(0), F(0), a)
    return (r, g, b, a), tuple(max(-SAT, min(SAT, c / a)) for c in (r, g, b)) + (a,)


def res(space, layer):
    return layer if space == ZERO else space


_TEXELS = {}


def texel_filter(img, x, y, f_raw, layer):
    """(premultiplied value in F, converted?) for texel (x, y) (IP5's first step).  f_raw is the filter space
    already defaulted to the image's own (it may be the zero space); layer resolves a zero space."""
    key = (img.name, x, y, f_raw, layer, "straight-filtering" in PLANTS)
    hit = _TEXELS.get(key)
    if hit is not None:
        return hit
    p, st = texel_own(img, x, y)
    if img.alpha_only:
        out = (p, False)
    elif "straight-filtering" in PLANTS and img.alpha_form == STRAIGHT:
        conv = f_raw != img.space
        c = convert_straight(st[:3], res(img.space, layer), res(f_raw, layer)) if conv else st[:3]
        out = (tuple(c) + (st[3],), conv)
    elif f_raw == img.space:
        out = (p, False)
    else:
        a = st[3]
        c = convert_straight(st[:3], res(img.space, layer), res(f_raw, layer))
        out = (tuple(ci * a for ci in c) + (a,), True)
    _TEXELS[key] = out
    return out


def to_layer(r, f_res, layer):
    """IP5's last step: a premultiplied value in F taken to L (not at all when F equals L)."""
    if f_res == layer:
        return r, False
    a = r[3]
    if a == 0:
        return (F(0), F(0), F(0), F(0)), True
    c = tuple(max(-SAT, min(SAT, ci / a)) for ci in r[:3])
    c = convert_straight(c, f_res, layer)
    return tuple(ci * a for ci in c) + (a,), True


# --- edge maps (IP6) -------------------------------------------------------------------------------------------------
def edge_map(mode, k, lo, hi):
    """The texel index a tap k reads on an axis of window [lo, hi), or None for NONE's transparent texel."""
    n = hi - lo
    if mode == PAD:
        return min(max(k, lo), hi - 1)
    if mode == REPEAT:
        return lo + ((k - lo) % n)
    if mode == REFLECT:
        m = (k - lo) % (2 * n)
        if "reflect-off-by-one" in PLANTS:
            return lo + m if m < n else lo + 2 * n - m
        return lo + m if m < n else lo + 2 * n - 1 - m
    if mode == NONE:
        return k if lo <= k < hi else None
    raise ValueError("edge mode")


def full_window(img, window):
    return (0, 0, img.w, img.h) if tuple(window) == (0, 0, 0, 0) else tuple(window)


class _Ctx:
    """The per-call parameters of one sample or area computation."""

    def __init__(self, img, window, ex, ey, fspace, layer):
        self.img = img
        self.win = full_window(img, window)
        x0, y0, x1, y1 = self.win
        assert 0 <= x0 < x1 <= img.w and 0 <= y0 < y1 <= img.h
        self.ex, self.ey = ex, ey
        self.f_raw = fspace if (fspace != ZERO and not img.alpha_only) else img.space
        self.layer = layer
        self.f_res = res(self.f_raw, layer)
        self.converted = False
        self.taps = []                  # the texel (x, y) each tap read, or None, in call order
        self.layer_mag = F(0)

    def tap(self, kx, ky):
        x0, y0, x1, y1 = self.win
        a = edge_map(self.ex, kx, x0, x1)
        b = edge_map(self.ey, ky, y0, y1)
        if a is None or b is None:
            self.taps.append(None)
            return (F(0), F(0), F(0), F(0))
        self.taps.append((a, b))
        v, conv = texel_filter(self.img, a, b, self.f_raw, self.layer)
        self.converted = self.converted or conv
        return v

    def finish(self, r):
        if self.img.alpha_only:
            return r
        if "straight-filtering" in PLANTS and self.img.alpha_form == STRAIGHT:
            a = r[3]
            r = (r[0] * a, r[1] * a, r[2] * a, a)
        out, conv = to_layer(r, self.f_res, self.layer)
        self.converted = self.converted or conv
        if conv and r[3] != 0:
            c = tuple(max(-SAT, min(SAT, ci / r[3])) for ci in r[:3])
            cc = convert_straight(c, self.f_res, self.layer)
            self.layer_mag = max(abs(v) for v in tuple(r) + c + cc + tuple(out))
        return out

    def condition(self, weights):
        """C = sum |w_i| M_i over the taps (`weights` in tap order) plus the layer step's magnitude: M_i is the
        largest absolute component the tap passes through (premultiplied in its own space, straight, converted
        straight, premultiplied in F).  A weighted sum of n terms in any order errs by at most n 2^-53 C."""
        total = self.layer_mag
        for w, t in zip(weights, self.taps):
            if t is None:
                continue
            img = self.img
            p, st = texel_own(img, t[0], t[1])
            m = max(abs(v) for v in p)
            if st:
                m = max(m, max(abs(v) for v in st))
            v, conv = texel_filter(img, t[0], t[1], self.f_raw, self.layer)
            m = max(m, max(abs(x) for x in v))
            if conv:
                cc = convert_straight(st[:3], res(img.space, self.layer), self.f_res)
                m = max(m, max(abs(x) for x in cc))
            total += abs(w) * m
        return total


# --- the point filters (IP7) ------------------------------------------------------------------------------------------
def kern(x):
    """Mitchell-Netravali B = C = 1/3 on 0 <= x <= 2, exactly."""
    x = F(x)
    if x < 1:
        return (21 * x ** 3 - 36 * x ** 2 + 16) / 18
    return (-7 * x ** 3 + 36 * x ** 2 - 60 * x + 32) / 18


def cubic_weights(f):
    return [kern(f + 1), kern(f), kern(1 - f), kern(2 - f)]


def _axpy(acc, w, v):
    for i in range(4):
        acc[i] += w * v[i]


def sample(img, window, ex, ey, filt, fspace, layer, u, v, info=None):
    """The premultiplied value in the layer space at image-space point (u, v) for NEAREST, LINEAR or CUBIC.

    `info`, when a dict, receives `terms` (weight, premultiplied tap value in F) and `converted`.
    """
    c = _Ctx(img, window, ex, ey, fspace, layer)
    u, v = F(u), F(v)
    terms = []
    if filt == NEAREST:
        terms.append((F(1), c.tap(floor(u), floor(v))))
    elif filt in (LINEAR, CUBIC):
        s = u if "centres-at-0" in PLANTS else u - F(1, 2)
        t = v if "centres-at-0" in PLANTS else v - F(1, 2)
        i, j = floor(s), floor(t)
        f, g = s - i, t - j
        if filt == LINEAR:
            wx, wy, base = [1 - f, f], [1 - g, g], 0
        else:
            wx, wy, base = cubic_weights(f), cubic_weights(g), -1
        for b, wyb in enumerate(wy):
            for a, wxa in enumerate(wx):
                terms.append((wxa * wyb, c.tap(i + base + a, j + base + b)))
    else:
        raise ValueError("not a point filter")
    r = [F(0)] * 4
    for w, p in terms:
        _axpy(r, w, p)
    if filt == CUBIC and "cubic-no-clamp" not in PLANTS:
        for k in range(4):
            lo = min(p[k] for _, p in terms)
            hi = max(p[k] for _, p in terms)
            r[k] = min(max(r[k], lo), hi)
    out = c.finish(tuple(r))
    if info is not None:
        info["terms"], info["converted"], info["filter"] = terms, c.converted, filt
        info["condition"] = c.condition([w for w, _ in terms])
    return out


# --- AREA (IP7) -------------------------------------------------------------------------------------------------------
def _cell_overlaps(a, b):
    """{cell: length of [a, b] inside the cell}, for the unit cells [c, c+1)."""
    out = {}
    for cell in range(floor(a), ceil(b)):
        w = min(F(cell + 1), b) - max(F(cell), a)
        if w > 0:
            out[cell] = w
    return out


def axis_weights(mode, lo, hi, a, b):
    """{texel index: total length of [a, b] that reads it} on one axis; transparent (NONE) lengths are left out.

    A whole period of REPEAT (n) or REFLECT (2n) is added as a multiple, so a region of any number of periods
    costs one period's cells.
    """
    n = hi - lo
    out = defaultdict(F)
    if mode in (REPEAT, REFLECT):
        period = n if mode == REPEAT else 2 * n
        q = floor((b - a) / period)
        if q >= 1:
            for cell, w in _cell_overlaps(a, a + period).items():
                out[edge_map(mode, cell, lo, hi)] += q * w
            a = a + q * period
        for cell, w in _cell_overlaps(a, b).items():
            out[edge_map(mode, cell, lo, hi)] += w
        return out
    # PAD and NONE: before the window, inside it, after it
    if a < lo:
        seg = min(b, F(lo)) - a
        if mode == PAD:
            out[lo] += seg
    if b > hi:
        seg = b - max(a, F(hi))
        if mode == PAD:
            out[hi - 1] += seg
    ma, mb = max(a, F(lo)), min(b, F(hi))
    if ma < mb:
        for cell, w in _cell_overlaps(ma, mb).items():
            out[cell] += w
    return out


def _clip(poly, axis, val, keep_ge):
    """Clip a convex polygon (list of (x, y) Fractions) to coordinate `axis` >= val (or <= val)."""
    out = []
    inside = (lambda p: p[axis] >= val) if keep_ge else (lambda p: p[axis] <= val)
    for k in range(len(poly)):
        p, q = poly[k], poly[(k + 1) % len(poly)]
        ip, iq = inside(p), inside(q)
        if ip != iq:
            t = (val - p[axis]) / (q[axis] - p[axis])
            out.append((p[0] + t * (q[0] - p[0]), p[1] + t * (q[1] - p[1])))
        if iq:
            out.append(q)
    return out


def _poly_area(poly):
    if len(poly) < 3:
        return F(0)
    s = F(0)
    for k in range(len(poly)):
        p, q = poly[k], poly[(k + 1) % len(poly)]
        s += p[0] * q[1] - q[0] * p[1]
    return abs(s) / 2


def cell_areas(verts, limit=200000):
    """{(cx, cy): area of the convex polygon inside the unit cell} by exact clipping."""
    xs = [p[0] for p in verts]
    ys = [p[1] for p in verts]
    if (ceil(max(xs)) - floor(min(xs))) * (ceil(max(ys)) - floor(min(ys))) > limit:
        raise ValueError("region too large for cell clipping")
    out = defaultdict(F)
    for cx in range(floor(min(xs)), ceil(max(xs))):
        strip = _clip(_clip(verts, 0, F(cx), True), 0, F(cx + 1), False)
        if len(strip) < 3:
            continue
        sy = [p[1] for p in strip]
        for cy in range(floor(min(sy)), ceil(max(sy))):
            cell = _clip(_clip(strip, 1, F(cy), True), 1, F(cy + 1), False)
            a = _poly_area(cell)
            if a > 0:
                out[(cx, cy)] += a
    return out


def area(img, window, ex, ey, fspace, layer, origin, edge_a, edge_b, info=None, force_general=False):
    """The exact mean of the extended image over the parallelogram origin + s edge_a + t edge_b, s, t in [0, 1],
    premultiplied in the layer space."""
    c = _Ctx(img, window, ex, ey, fspace, layer)
    x0, y0, x1, y1 = c.win
    o = (F(origin[0]), F(origin[1]))
    a = (F(edge_a[0]), F(edge_a[1]))
    b = (F(edge_b[0]), F(edge_b[1]))
    total = abs(a[0] * b[1] - a[1] * b[0])
    if total == 0:
        raise ValueError("empty region")
    weights = {}                                            # (texel x, texel y) -> area
    axis_aligned = (a[1] == 0 and b[0] == 0) or (a[0] == 0 and b[1] == 0)
    if axis_aligned and not force_general:
        w1 = [o[0], o[0] + a[0] + b[0]]
        w2 = [o[1], o[1] + a[1] + b[1]]
        wx = axis_weights(ex, x0, x1, min(w1), max(w1))
        wy = axis_weights(ey, y0, y1, min(w2), max(w2))
        for i, p in wx.items():
            for j, q in wy.items():
                weights[(i, j)] = p * q
    else:
        verts = [o, (o[0] + a[0], o[1] + a[1]), (o[0] + a[0] + b[0], o[1] + a[1] + b[1]), (o[0] + b[0], o[1] + b[1])]
        cells = cell_areas(verts)
        assert sum(cells.values()) == total
        for (cx, cy), ar in cells.items():
            i, j = edge_map(ex, cx, x0, x1), edge_map(ey, cy, y0, y1)
            if i is not None and j is not None:
                weights[(i, j)] = weights.get((i, j), F(0)) + ar
    r = [F(0)] * 4
    terms = []
    c.taps = list(weights.keys())
    for (i, j), ar in weights.items():
        p, conv = texel_filter(img, i, j, c.f_raw, c.layer)
        c.converted = c.converted or conv
        _axpy(r, ar / total, p)
        terms.append((ar / total, p))
    out = c.finish(tuple(r))
    if info is not None:
        info["terms"], info["converted"], info["filter"] = terms, c.converted, AREA
        info["condition"] = c.condition([w for w, _ in terms])
    return out


# --- the filter choice (IP8) ------------------------------------------------------------------------------------------
def stretches_every_direction(m00, m01, m10, m11):
    """1 when the smallest singular value of the map exceeds 1, decided on the exact values of the doubles."""
    m00, m01, m10, m11 = F(m00), F(m01), F(m10), F(m11)
    if "determinant-predicate" in PLANTS:
        return 1 if abs(m00 * m11 - m01 * m10) > 1 else 0
    A = m00 * m00 + m10 * m10
    B = m00 * m01 + m10 * m11
    C = m01 * m01 + m11 * m11
    return 1 if (A > 1 and C > 1 and (A - 1) * (C - 1) > B * B) else 0


# --- exactness of a vector (the tolerance 0 cases) -------------------------------------------------------------------
def _short(x, bits=12):
    """x is a dyadic rational whose significand has at most `bits` bits."""
    x = F(x)
    if x == 0:
        return True
    d = x.denominator
    if d & (d - 1):
        return False
    n = abs(x.numerator)
    while n % 2 == 0:
        n //= 2
    return n < (1 << bits)


def is_exact_case(info, result):
    """True when a double evaluation of the case is exact: no conversion, a linear filter, short dyadic factors and
    sums that fit 53 bits (so no order of summation rounds)."""
    if info["converted"] or info["filter"] == CUBIC:
        return False
    for k in range(4):
        prods = []
        for w, p in info["terms"]:
            if not (_short(w) and _short(p[k])):
                return False
            prods.append(w * p[k])
        den = 1
        for x in prods:
            den = max(den, x.denominator)
        if sum(abs(x) for x in prods) * den >= (1 << 53):
            return False
        if F(float(result[k])) != result[k]:
            return False
    return True


# --- the selftest -----------------------------------------------------------------------------------------------------
def _xorshift(seed):
    x = seed

    def nxt():
        nonlocal x
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        return x
    return nxt


def _rand_image(name, rng, w, h, layout, typ, bits, aform, aonly, space):
    pool = [0.0, 0.25, 0.5, 0.75, 1.0, 0.125, -0.5, 1.5, 0.375, 2.0]
    apool = [0.0, 0.25, 0.5, 0.75, 1.0, 0.125, 0.875]
    s = []
    for _ in range(w * h):
        for c in range(layout):
            alpha = (layout == 4 and c == 3) or aonly
            if typ == UNORM:
                s.append(rng() % (1 << bits))
            else:
                s.append(float_bits((apool if alpha else pool)[rng() % (len(apool) if alpha else len(pool))]))
    return Image(name, w, h, layout, typ, bits, aform, aonly, space, s)


SRGB, LIN, P3, ADOBE22, BT2020 = (1, 2, 0.0), (1, 1, 0.0), (3, 2, 0.0), (2, 3, 2.2), (4, 1, 0.0)


def run_selftest():
    fails = []
    count = [0]
    rng = _xorshift(2463534242)

    def check(name, fn):
        count[0] += 1
        try:
            ok = fn()
        except Exception as e:                                  # an exception is a failed identity
            ok = False
            name += " (raised %r)" % (e,)
        if not ok:
            fails.append(name)

    imgs = [
        _rand_image("sf", rng, 5, 4, 4, FLOAT, 32, STRAIGHT, 0, LIN),
        _rand_image("pf", rng, 5, 4, 4, FLOAT, 32, PREMULTIPLIED, 0, LIN),
        _rand_image("g8", rng, 4, 3, 1, UNORM, 8, 0, 0, SRGB),
        _rand_image("rgb12", rng, 3, 3, 3, UNORM, 12, 0, 0, P3),
        _rand_image("a16", rng, 4, 3, 1, UNORM, 16, 0, 1, ZERO),
        _rand_image("s8", rng, 4, 4, 4, UNORM, 8, STRAIGHT, 0, SRGB),
    ]
    wins = lambda im: [(0, 0, 0, 0), (1, 1, im.w, im.h) if im.w > 1 and im.h > 1 else (0, 0, 0, 0)]
    pts = [(F(rng() % 4096, 128) - 8, F(rng() % 4096, 128) - 8) for _ in range(40)]
    modes = (PAD, REPEAT, REFLECT, NONE)

    def layer_of(im):
        return im.space if im.space != ZERO else SRGB

    # --- IP7: the cubic weights sum to 1
    def cubic_sum():
        fs = [F(0), F(1, 2), F(1, 3), 1 - F(1, 2 ** 53), F(1, 2 ** 40)] + [F(rng() % 100000, 100000) for _ in range(300)]
        return all(sum(cubic_weights(f)) == 1 for f in fs) and kern(0) == F(8, 9) and kern(1) == F(1, 18) \
            and kern(2) == 0 and kern(F(1, 1)) == (-7 + 36 - 60 + 32) / F(18)
    check("cubic weights sum to 1; k(0) = 8/9, k(1) = 1/18, k(2) = 0", cubic_sum)

    # --- NEAREST and LINEAR agree at texel centres, per image and window and mode
    def centres():
        for im in imgs:
            for win in wins(im):
                x0, y0, x1, y1 = full_window(im, win)
                for ex in modes:
                    for ey in modes:
                        for i in range(x0, x1):
                            for j in range(y0, y1):
                                for fs in (ZERO,):
                                    a = sample(im, win, ex, ey, NEAREST, fs, layer_of(im), F(2 * i + 1, 2), F(2 * j + 1, 2))
                                    b = sample(im, win, ex, ey, LINEAR, fs, layer_of(im), F(2 * i + 1, 2), F(2 * j + 1, 2))
                                    if a != b:
                                        return False
        return True
    check("LINEAR and NEAREST agree at texel centres", centres)

    # --- LINEAR of a constant is the constant; AREA of a constant image is the constant
    const = Image("const", 3, 3, 4, FLOAT, 32, STRAIGHT, 0, LIN, [float_bits(v) for _ in range(9) for v in (0.5, 0.25, 0.75, 0.5)])
    cval = (F(1, 4), F(1, 8), F(3, 8), F(1, 2))

    def lin_const():
        for ex in (PAD, REPEAT, REFLECT):
            for ey in (PAD, REPEAT, REFLECT):
                for f in (LINEAR, CUBIC):
                    for p in pts:
                        if sample(const, (0, 0, 0, 0), ex, ey, f, ZERO, LIN, p[0], p[1]) != cval:
                            return False
        return True
    check("LINEAR and CUBIC of a constant (padded, repeated, reflected) are the constant", lin_const)

    def area_const():
        for ex in (PAD, REPEAT, REFLECT):
            for ey in (PAD, REPEAT, REFLECT):
                for o, a, b in (((F(-3, 4), F(5, 4)), (F(7, 3), F(1, 2)), (F(-1, 5), F(9, 4))),
                                ((F(40), F(-77, 3)), (F(301, 7), 0), (0, F(83, 11)))):
                    if area(const, (0, 0, 0, 0), ex, ey, ZERO, LIN, o, a, b) != cval:
                        return False
        # NONE: inside the image the mean is still the constant
        return area(const, (0, 0, 0, 0), NONE, NONE, ZERO, LIN, (F(1, 3), F(1, 3)), (F(1), 0), (0, F(2))) == cval
    check("AREA of a constant image is the constant", area_const)

    # --- AREA of an axis-aligned unit square at an integer position is the texel (any mode, any place)
    def unit_texel():
        for im in imgs:
            for win in wins(im):
                x0, y0, x1, y1 = full_window(im, win)
                for ex in modes:
                    for ey in modes:
                        for i in range(-3, im.w + 3):
                            for j in range(-2, im.h + 2):
                                want = sample(im, win, ex, ey, NEAREST, ZERO, layer_of(im), F(2 * i + 1, 2), F(2 * j + 1, 2))
                                got = area(im, win, ex, ey, ZERO, layer_of(im), (i, j), (1, 0), (0, 1))
                                got2 = area(im, win, ex, ey, ZERO, layer_of(im), (i, j), (1, 0), (0, 1), force_general=True)
                                if got != want or got2 != want:
                                    return False
        return True
    check("AREA of an axis-aligned unit square at an integer position is the texel (both algorithms)", unit_texel)

    # --- AREA over an n-fold repeat is the window's mean (and a 2n-fold reflect)
    def n_fold():
        for im in imgs:
            for win in wins(im):
                x0, y0, x1, y1 = full_window(im, win)
                w, h = x1 - x0, y1 - y0
                for fx, fy in ((1, 1), (3, 2), (37, 11)):
                    mean = [F(0)] * 4
                    for i in range(x0, x1):
                        for j in range(y0, y1):
                            _axpy(mean, F(1, w * h), texel_filter(im, i, j, im.space if not im.alpha_only else im.space,
                                                                  layer_of(im))[0])
                    mean, _ = to_layer(tuple(mean), res(im.space, layer_of(im)), layer_of(im))
                    for mode, per in ((REPEAT, 1), (REFLECT, 2)):
                        got = area(im, win, mode, mode, ZERO, layer_of(im), (x0, y0), (per * fx * w, 0), (0, per * fy * h))
                        if got != tuple(mean):
                            return False
                        if fx * fy < 10 and area(im, win, mode, mode, ZERO, layer_of(im), (x0, y0), (per * fx * w, 0),
                                                 (0, per * fy * h), force_general=True) != tuple(mean):
                            return False
        return True
    check("AREA over an n-fold REPEAT (2n-fold REFLECT) is the window's mean", n_fold)

    # --- REPEAT periodic, REFLECT symmetric; and the sampled value too
    def periodic():
        for lo, hi in ((0, 1), (0, 5), (2, 6), (3, 4)):
            n = hi - lo
            for k in range(-40, 40):
                if edge_map(REPEAT, k, lo, hi) != edge_map(REPEAT, k + n, lo, hi):
                    return False
                if edge_map(REFLECT, k, lo, hi) != edge_map(REFLECT, k + 2 * n, lo, hi):
                    return False
                if edge_map(REFLECT, lo - 1 - k, lo, hi) != edge_map(REFLECT, lo + k, lo, hi):
                    return False
                if edge_map(REFLECT, hi + k, lo, hi) != edge_map(REFLECT, hi - 1 - k, lo, hi):
                    return False
                if not (lo <= edge_map(REFLECT, k, lo, hi) < hi and lo <= edge_map(REPEAT, k, lo, hi) < hi):
                    return False
        im = imgs[0]
        for f in (NEAREST, LINEAR, CUBIC):
            for p in pts[:12]:
                a = sample(im, (0, 0, 0, 0), REPEAT, REPEAT, f, ZERO, LIN, p[0], p[1])
                if a != sample(im, (0, 0, 0, 0), REPEAT, REPEAT, f, ZERO, LIN, p[0] + 3 * im.w, p[1] - 5 * im.h):
                    return False
                if f != NEAREST:                                # symmetric about the window's edges (linear, cubic)
                    b = sample(im, (0, 0, 0, 0), REFLECT, REFLECT, f, ZERO, LIN, p[0], p[1])
                    if b != sample(im, (0, 0, 0, 0), REFLECT, REFLECT, f, ZERO, LIN, -p[0], p[1]):
                        return False
                    if b != sample(im, (0, 0, 0, 0), REFLECT, REFLECT, f, ZERO, LIN, 2 * im.w - p[0], 2 * im.h - p[1]):
                        return False
        return True
    check("REPEAT is periodic and REFLECT symmetric (maps and sampled values)", periodic)

    # --- opaque straight and premultiplied agree; and a translucent pair agrees (premultiplied filtering)
    def straight_vs_premult():
        w, h = 5, 4
        ss, pp = [], []
        for _ in range(w * h):
            a = [1.0, 0.5, 0.25, 0.75, 0.0][rng() % 5]
            cs = [(0.5, 0.25, 0.75, 1.0, 0.125, 0.375)[rng() % 6] for _ in range(3)]
            ss += [float_bits(c) for c in cs] + [float_bits(a)]
            pp += [float_bits(c * a) for c in cs] + [float_bits(a)]
        for opaque in (True, False):
            if opaque:
                ss = [float_bits(1.0) if k % 4 == 3 else s for k, s in enumerate(ss)]
                pp = [float_bits(1.0) if k % 4 == 3 else s for k, s in enumerate(pp)]
                pp = [ss[k] if k % 4 != 3 else pp[k] for k in range(len(pp))]
            s = Image("s", w, h, 4, FLOAT, 32, STRAIGHT, 0, LIN, ss)
            p = Image("p", w, h, 4, FLOAT, 32, PREMULTIPLIED, 0, LIN, pp)
            for f in (NEAREST, LINEAR, CUBIC):
                for q in pts[:15]:
                    for ex in (PAD, REPEAT, REFLECT, NONE):
                        for fs, ly in ((ZERO, LIN), (SRGB, LIN), (LIN, SRGB)):
                            if f != AREA and sample(s, (0, 0, 0, 0), ex, PAD, f, fs, ly, q[0], q[1]) != \
                                    sample(p, (0, 0, 0, 0), ex, PAD, f, fs, ly, q[0], q[1]):
                                return False
            if area(s, (0, 0, 0, 0), REPEAT, PAD, ZERO, LIN, (F(1, 3), F(1, 2)), (F(7, 2), F(1, 5)), (F(-1, 4), F(5, 2))) != \
                    area(p, (0, 0, 0, 0), REPEAT, PAD, ZERO, LIN, (F(1, 3), F(1, 2)), (F(7, 2), F(1, 5)), (F(-1, 4), F(5, 2))):
                return False
        return True
    check("a straight image and the premultiplied image of the same colors agree (opaque, and translucent)",
          straight_vs_premult)

    # --- CUBIC at a texel centre is (1/18, 8/9, 1/18) per axis; CUBIC never leaves its taps' range
    def cubic_centre():
        im = imgs[0]
        w = [F(1, 18), F(8, 9), F(1, 18)]
        for i in range(1, im.w - 1):
            for j in range(1, im.h - 1):
                got = sample(im, (0, 0, 0, 0), PAD, PAD, CUBIC, ZERO, LIN, F(2 * i + 1, 2), F(2 * j + 1, 2))
                want = [F(0)] * 4
                for b in range(3):
                    for a in range(3):
                        _axpy(want, w[a] * w[b], texel_filter(im, i - 1 + a, j - 1 + b, LIN, LIN)[0])
                lo = [min(texel_filter(im, i - 1 + a, j - 1 + b, LIN, LIN)[0][k] for a in range(3) for b in range(3)) for k in range(4)]
                hi = [max(texel_filter(im, i - 1 + a, j - 1 + b, LIN, LIN)[0][k] for a in range(3) for b in range(3)) for k in range(4)]
                want = [min(max(want[k], lo[k]), hi[k]) for k in range(4)]
                if tuple(want) != got:
                    return False
        return True
    check("CUBIC at a texel centre is (1/18, 8/9, 1/18) per axis", cubic_centre)

    def cubic_range():
        w, h = 8, 3
        step = Image("step", w, h, 1, FLOAT, 32, 0, 0, LIN, [float_bits(0.0 if (k % w) < 4 else 1.0) for k in range(w * h)])
        for fx in range(0, 64):
            u = F(3) + F(fx, 32)
            r = sample(step, (0, 0, 0, 0), PAD, PAD, CUBIC, ZERO, LIN, u, F(3, 2))
            if not (0 <= r[0] <= 1):
                return False
        lo = Image("oor", 5, 1, 1, FLOAT, 32, 0, 0, LIN, [float_bits(v) for v in (-3.0, 0.5, 1e30, -1e30, 0.5)])
        for fx in range(0, 160):
            r = sample(lo, (0, 0, 0, 0), PAD, PAD, CUBIC, ZERO, LIN, F(fx, 32), F(1, 2))
            if not (-SAT <= r[0] <= SAT):
                return False
        return True
    check("CUBIC stays inside the range of its taps (step edge, out-of-range floats)", cubic_range)

    # --- AREA by two algorithms; the area is conserved; mixed extents
    def two_algorithms():
        im = imgs[5]
        for ex in modes:
            for ey in modes:
                for o, a, b in (((F(-9, 4), F(3, 2)), (F(23, 3), 0), (0, F(41, 7))),
                                ((F(5, 7), F(-11, 3)), (0, F(13, 2)), (F(-17, 3), 0))):
                    if area(im, (1, 1, 4, 3), ex, ey, ZERO, SRGB, o, a, b) != \
                            area(im, (1, 1, 4, 3), ex, ey, ZERO, SRGB, o, a, b, force_general=True):
                        return False
        return True
    check("AREA of an axis-aligned region agrees across the weights and the clipping algorithms", two_algorithms)

    # --- decode (IP4)
    def decode_rules():
        a = Image("d", 3, 1, 4, UNORM, 12, STRAIGHT, 0, LIN, [4095, 0, 2048, 4095] * 3)
        if texel_own(a, 0, 0)[0] != (F(1), F(0), F(2048, 4095), F(1)):
            return False
        f = Image("df", 4, 1, 1, FLOAT, 32, 0, 0, LIN, [float_bits(v) for v in (-3.0, 0.5, 1e30, -1e30)])
        got = [texel_own(f, i, 0)[0] for i in range(4)]
        if got[0] != (F(-3),) * 3 + (F(1),) or got[1] != (F(1, 2),) * 3 + (F(1),):
            return False
        if got[2] != (SAT,) * 3 + (F(1),) or got[3] != (-SAT,) * 3 + (F(1),):
            return False
        pm = Image("dp", 2, 1, 4, UNORM, 8, PREMULTIPLIED, 0, LIN, [200, 10, 0, 100, 5, 5, 5, 0])
        p0, st0 = texel_own(pm, 0, 0)
        p1, st1 = texel_own(pm, 1, 0)
        if p0 != (F(200, 255), F(10, 255), F(0), F(100, 255)):
            return False
        if st0[0] != min(SAT, F(200, 255) / F(100, 255)) or p1 != (F(5, 255),) * 3 + (F(0),) or st1[:3] != (0, 0, 0):
            return False
        ao = Image("da", 2, 1, 1, UNORM, 16, 0, 1, ZERO, [65535, 1])
        return texel_own(ao, 0, 0)[0] == (0, 0, 0, F(1)) and texel_own(ao, 1, 0)[0][3] == F(1, 65535)
    check("decode: unorm v/(2^n-1), float saturation, premultiplied taken as stored, alpha-only", decode_rules)

    # --- the filter choice
    def predicate():
        def dg(x, y):
            return (x, 0.0, 0.0, y)
        nxt = lambda x: struct.unpack("<d", struct.pack("<q", struct.unpack("<q", struct.pack("<d", x))[0] + 1))[0]
        prv = lambda x: struct.unpack("<d", struct.pack("<q", struct.unpack("<q", struct.pack("<d", x))[0] - 1))[0]
        import math
        c30, s30 = math.cos(math.radians(30)), math.sin(math.radians(30))
        cases = [
            ((7.0, 0.0, 0.0, 7.0), 1), ((0.3, 0.0, 0.0, 0.3), 0), ((1.0, 0.0, 0.0, 1.0), 0), ((7.0, 0.0, 0.0, 0.3), 0),
            ((2 * c30, -2 * s30, 2 * s30, 2 * c30), 1), ((0.9 * c30, -0.9 * s30, 0.9 * s30, 0.9 * c30), 0),
            ((1.0, 0.5, 0.0, 1.0), 0), ((1.5, 1.0, 0.0, 1.5), 1), ((-1.0, 0.0, 0.0, 1.0), 0), ((-3.0, 0.0, 0.0, 3.0), 1),
            ((2.0, 3.0, 0.0, 2.0), 0),                     # det 4, sigma_min exactly 1
            ((2.0, prv(3.0), 0.0, 2.0), 1), ((2.0, nxt(3.0), 0.0, 2.0), 0),
            ((nxt(1.0), 0.0, 0.0, nxt(1.0)), 1), ((prv(1.0), 0.0, 0.0, prv(1.0)), 0), ((nxt(1.0), 0.0, 0.0, 1.0), 0),
            ((4.0, 0.0, 0.0, 0.25), 0),                    # determinant 1, sigma_min 0.25
            ((2.0, 0.0, 0.0, 0.5), 0),                     # determinant 1, sigma_min 0.5
        ]
        return all(stretches_every_direction(*m) == want for m, want in cases)
    check("IP8: stretches every direction exactly when sigma_min > 1 (7 by 0.3, determinant 1, one ulp)", predicate)

    # --- the decimal part
    def transfers():
        spaces = [SRGB, LIN, P3, ADOBE22, BT2020, (3, 3, 0.4545), (4, 3, 10.0), (1, 3, 0.1)]
        for sp in spaces:
            for x in (F(0), F(1), F(1, 3), F(-1, 7), F(3, 1000), F(1, 100), F(5), F(-2)):
                lin = convert_straight((x, x, x), sp, (sp[0], 1, 0.0))
                back = convert_straight(lin, (sp[0], 1, 0.0), sp)
                if any(abs(b - x) > F(1, 10 ** 100) for b in back):
                    return False
        # the white of one gamut is the white of the next; the published matrix of sRGB
        for a in (SRGB, P3, BT2020, (2, 1, 0.0)):
            for b in (SRGB, P3, BT2020, (2, 1, 0.0)):
                w = convert_straight((F(1), F(1), F(1)), (a[0], 1, 0.0), (b[0], 1, 0.0))
                if any(abs(c - 1) > F(1, 10 ** 100) for c in w):
                    return False
        m = _rgb_to_xyz(1)
        return abs(m[0][0] - D("0.4123908")) < D("1e-6") and abs(m[1][1] - D("0.7151687")) < D("1e-6") \
            and abs(m[2][2] - D("0.9505322")) < D("1e-6")
    check("transfers round-trip and the gamuts share the white; sRGB's matrix is the published one", transfers)

    def bound():
        saved = PREC
        worst = F(0)
        vals = [F(0), F(1), F(1, 3), F(-1, 7), F(1, 100), F(5), F(-3), F(10 ** 6), F(2) ** 64, -F(2) ** 64, F(1, 10 ** 6)]
        spaces = [SRGB, LIN, P3, ADOBE22, BT2020, (4, 3, 10.0), (1, 3, 0.1), (3, 3, 0.4545)]
        try:
            for a in spaces:
                for b in spaces:
                    for v in vals:
                        w = (v, F(1, 5), -v / 3)
                        set_precision(110)
                        lo = convert_straight(w, a, b)
                        set_precision(170)
                        hi = convert_straight(w, a, b)
                        for x, y in zip(lo, hi):
                            worst = max(worst, abs(x - y) / max(F(1), abs(y)))
        finally:
            set_precision(saved)
        print("  largest relative difference between 110 and 170 digits over the grid: %.3g" % float(worst))
        return worst < F(1, 10 ** 85)
    check("the decimal results are within 1e-85 * max(1, |x|) (110 digits against 170)", bound)

    for f in fails:
        print("FAILED: " + f)
    print("image-oracle selftest: %d identities, %d failed%s" % (
        count[0], len(fails), (" (plants: %s)" % ", ".join(sorted(PLANTS))) if PLANTS else ""))
    return 1 if fails else 0


def main(argv):
    args = list(argv)
    while "--plant" in args:
        k = args.index("--plant")
        name = args[k + 1] if k + 1 < len(args) else ""
        if name not in PLANT_NAMES:
            sys.exit("image-oracle: --plant takes one of: " + ", ".join(PLANT_NAMES))
        PLANTS.add(name)
        del args[k:k + 2]
    if args == ["--selftest"]:
        return run_selftest()
    sys.exit("usage: image-oracle.py --selftest [--plant NAME]")


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
