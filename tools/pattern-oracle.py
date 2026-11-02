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
"""The reference of the scene's patterns (rules PT1 to PT14; pattern.h).

A slow, independent statement of the pixel a pattern fill gives, sharing no code with the library or with any other
oracle.  Every input double is taken as the exact rational it is, every polygon is clipped in `Fraction`s (convex
against convex by exact half-planes, areas by the shoelace formula), and no float takes part in a decision or a value.
A vector's double is the exact rational rounded to the nearest double (`float(Fraction)` is correctly rounded).

**The scene.**  A device grid of W x H pixels (pixel (px, py) is the unit square [px, px + 1] x [py, py + 1], y down).
One pattern use fills the whole grid at coverage 1, so the expected value at a pixel is the lattice picture's value.
All colors are given directly in the layer's color space (no conversion is under test here).  A pattern is the cell
`(x, y, w, h)`, the steps, `repeat_x` and `repeat_y`, an optional tint (a straight color), the paint's alpha
multiplier `a`, the chain map `C` from pattern space to the device (an affine map of doubles, `x' = m0 x + m1 y + m4`,
`y' = m2 x + m3 y + m5`; it already includes the target, instance, item and pattern maps) and the tile: a flat list of
operations, each a fill of a convex region (a polygon in pattern space) with a paint, or a group push and pop (an
isolated group with an opacity and the composite SRC_OVER or MULTIPLY), or a clip push and pop (a convex region).  A
paint is a solid straight color, a linear gradient (two or more stops with strictly increasing offsets, pad extension,
straight interpolation), a NEAREST image (straight rgba8 texels, window the whole image, PAD edges, mapped to the
tile by an axis-aligned map of positive scale), the drawing list's FOREGROUND with an alpha multiplier, or a nested
pattern (its map takes its space to the enclosing tile's space).  The scene also carries the drawing list's foreground.

**The definition.**
- *Copies (PT5).*  The copy (i, j) is the tile translated by `t = (i x_step, j y_step)` (the exact products) and its
  cell is `[x + i x_step, x + i x_step + w] x [y + j y_step, y + j y_step + h]`.  A step is a real number here, so the
  right edge of a cell is `left + w` and, when `x_step == w`, it is the very number the next cell's left edge is.  What
  the *double* formula of the library gives for those edges is a separate matter, checked by `sim_cell` against
  `true_cell`: the left edge is one product and one sum, the right edge is `x + (i + 1) x_step` when the step equals
  the size and `left + w` otherwise, and nothing accumulates.  An axis with `repeat` 0 has the index 0 only.  The copies
  of a pixel are those whose mapped cell meets it in a positive area, in lattice order (increasing j, then increasing i).
- *The pixel of a unit (PT6).*  For a fill of the region R with a paint, the copy x has the device region
  `Rx = C(R + t_x) intersected with C(cell_x) and the pixel`, its area `A_x`, the clip coverage `c_x` (the product, over
  the clips in force for that copy, of the area of `C(clip + t_x) intersected with C(cell_x) and the pixel`), and the
  integral `I_x` of the paint (premultiplied) over `Rx`, the paint of copy x being evaluated in the tile's own frame
  (a gradient or an image moves with its copy; a nested pattern's chain is `C . T(t_x) . G`).  The unit is ONE element
  with the shape `f = sum_x c_x A_x` and the source `S = (sum_x c_x I_x) / f` (0 where `f = 0`), the exact area-weighted
  mean, and it is composited with `apply(L, S, f, SRC_OVER)`; written with the integrals that is
  `L' = sum_x c_x I_x + (1 - sum_x c_x I_x.alpha) L`.  A paint's mean over a convex region is exact: a gradient is
  integrated piecewise between its stops (the region cut at the lines t = offset, and over each piece the premultiplied
  color, a quadratic of the position, by the three-midpoint rule, which is exact for quadratics), an image by the sum
  over texel rectangles of texel x area, a nested pattern's value is its own pixel value (below) times the area.
- *Groups and clips.*  A group is ONE group over all copies: its layer starts transparent, its units are union
  elements as above, and at its pop `apply(P, o G, 1, composite)` composites it onto the enclosing layer (SRC_OVER:
  `L' = S + (1 - S.alpha) L`; MULTIPLY on premultiplied values: `Cr = (1 - ab) Cs + (1 - as) Cb + Cs Cb`,
  `ar = as + ab - as ab`).  A clip block multiplies its clip coverage into the fills it holds (above).  **Restriction
  (flagged):** a group pushed while a clip is in force is refused here (the product of clips would have to be defined
  for a union of copies, and no vector needs it); no scene of the vectors has one.
- *The lattice picture (PT6, PT7, PT8).*  The layer starts at (0, 0, 0, 0), whatever the backdrop is.  When the cells do
  not overlap (`overlaps` is false: on a REPEATING axis `step < size` is overlap; a step on an axis that does not
  repeat has no neighbour and is ignored, which is this oracle's decision and the library's) the units are drawn in order
  as union elements of all the copies.  When they overlap the copies are drawn in lattice order, later over earlier, each
  with every unit restricted GEOMETRICALLY to its cell (the shape of the unit is the polygon `R + t` clipped to the cell
  and the pixel, as in PT6 with one copy; **flagged interpretation:** PT7 describes the restriction as a clip of the cell
  whose coverage multiplies, and multiplying a coverage in would make the all-Normal equality below false, since
  `m G + (1 - m G.alpha) L` is not the fold of the m-scaled items).  There are two algorithms: *isolated* (each copy's
  tile is drawn on a transparent layer, which is then composited onto the lattice layer with SRC_OVER) and *general*
  (each copy's units are drawn directly onto the lattice layer: one non-isolated, non-knockout group over it, so that a
  blending group composites against the lattice layer's content).  They are equal, exactly, when every composite of the tile is
  SRC_OVER (SRC_OVER associates), and the isolated one is the tier for such tiles; the general one is used for a tile
  holding a MULTIPLY group (a nested pattern counts as Normal: its value is isolated).
- *Colored and uncolored (PT9).*  Colored: `S = a x (lattice picture)`.  Uncolored with the tint `(r, g, b, ta)`: with
  `A` the lattice picture's alpha, `alpha = A ta a` and `S = (r alpha, g alpha, b alpha, alpha)`; the tile's colors
  are discarded.  The pixel's value is `apply(transparent, S, 1, SRC_OVER) = S`.
- *Foreground (PT8).*  A FOREGROUND paint is the drawing list's foreground (straight color) with its alpha multiplied
  by the paint's.
- *What draws nothing (PT10).*  `notice`: reason 23 (MAP_NOT_INVERTIBLE) when some map of the chain has a 2x2 part of
  determinant exactly 0 (each map on its own entries), else 37 (PATTERN_DEGENERATE) when `w` or `h` is 0 or a step is 0
  on a repeating axis, else 0.  A pattern with a reason draws nothing.

**Plants.**  `--plant NAME` breaks one rule on purpose; the selftest, and `gen-pattern-vectors.py --check --plant NAME`,
must then fail: `separate-shapes` (PT6 units composited copy by copy: the seam pixel gives 1 - (1 - a)(1 - b)),
`copy0-source` (a unit's paint taken from copy 0 for every copy), `group-per-copy` (a group is a group per copy),
`reverse-lattice-order` (PT7 copies in decreasing order), `tile-not-isolated` (the lattice layer starts from a
backdrop), `keeps-tile-colors` (an uncolored pattern keeps the tile's colors), `add-step` (a copy's position is the
step added i times, in doubles), `left-plus-w` (an abutting cell's right edge is `left + w` in doubles),
`content-not-clipped` (the tile's content is not restricted to its cell), `foreground-from-creator` (a FOREGROUND
paint resolves against another color than the drawing list's).

    python3 tools/pattern-oracle.py --selftest
    python3 tools/pattern-oracle.py --selftest --plant NAME
"""
import struct
import sys
from array import array
from fractions import Fraction as F
from math import ceil, floor

PLANT_NAMES = ("separate-shapes", "copy0-source", "group-per-copy", "reverse-lattice-order", "tile-not-isolated",
               "keeps-tile-colors", "add-step", "left-plus-w", "content-not-clipped", "foreground-from-creator")
PLANTS = set()

SRC_OVER, MULTIPLY = 1, 2
SOLID, LINEAR, IMAGE, PATTERN, FOREGROUND = 1, 2, 3, 4, 5
REASON_NONE, REASON_NOT_INVERTIBLE, REASON_DEGENERATE = 0, 23, 37

ZERO4 = (F(0), F(0), F(0), F(0))
ID = (1, 0, 0, 1, 0, 0)
CREATOR_FOREGROUND = (0.1, 0.2, 0.9, 1.0)      # the plant's: not the drawing list's
BACKDROP = (F(1, 5), F(1, 10), F(1, 20), F(1, 2))   # the plant's: a premultiplied backdrop the layer must not start from
FORCE_PT7 = [None]                              # "isolated" or "general": the selftest overrides the tier's choice


# --- exact geometry ---------------------------------------------------------------------------------------------------
def signed2(P):
    """Twice the signed area of the polygon @p P (the shoelace formula)."""
    s = F(0)
    n = len(P)
    for k in range(n):
        x0, y0 = P[k]
        x1, y1 = P[(k + 1) % n]
        s += x0 * y1 - x1 * y0
    return s


def area(P):
    """The area of the polygon @p P: 0 for fewer than three vertices."""
    if len(P) < 3:
        return F(0)
    s = signed2(P)
    return (s if s >= 0 else -s) / 2


def ccw(P):
    """@p P with positive signed area, or None when it has no area."""
    s = signed2(P)
    if s == 0:
        return None
    return list(P) if s > 0 else list(reversed(P))


def clip_half(P, a, b, c):
    """The part of the convex polygon @p P with a x + b y + c >= 0 (Sutherland-Hodgman, exact)."""
    if not P:
        return []
    out = []
    prev = P[-1]
    dprev = a * prev[0] + b * prev[1] + c
    for cur in P:
        dcur = a * cur[0] + b * cur[1] + c
        if dcur >= 0:
            if dprev < 0:
                u = dprev / (dprev - dcur)
                out.append((prev[0] + (cur[0] - prev[0]) * u, prev[1] + (cur[1] - prev[1]) * u))
            out.append(cur)
        elif dprev > 0:
            u = dprev / (dprev - dcur)
            out.append((prev[0] + (cur[0] - prev[0]) * u, prev[1] + (cur[1] - prev[1]) * u))
        prev, dprev = cur, dcur
    return out


def convex_clip(P, Q):
    """The convex polygon @p P cut by the counterclockwise convex polygon @p Q (every edge a half-plane)."""
    n = len(Q)
    for k in range(n):
        x0, y0 = Q[k]
        x1, y1 = Q[(k + 1) % n]
        a = -(y1 - y0)
        b = x1 - x0
        P = clip_half(P, a, b, -(a * x0 + b * y0))
        if len(P) < 3:
            return []
    return P


def mapf(M, p):
    return (M[0] * p[0] + M[1] * p[1] + M[4], M[2] * p[0] + M[3] * p[1] + M[5])


def compose(A, B):
    """The map A after B."""
    return (A[0] * B[0] + A[1] * B[2], A[0] * B[1] + A[1] * B[3], A[2] * B[0] + A[3] * B[2], A[2] * B[1] + A[3] * B[3],
            A[0] * B[4] + A[1] * B[5] + A[4], A[2] * B[4] + A[3] * B[5] + A[5])


def det(M):
    return M[0] * M[3] - M[1] * M[2]


def invert(M):
    d = det(M)
    i0, i1, i2, i3 = M[3] / d, -M[1] / d, -M[2] / d, M[0] / d
    return (i0, i1, i2, i3, -(i0 * M[4] + i1 * M[5]), -(i2 * M[4] + i3 * M[5]))


def translate(t):
    return (F(1), F(0), F(0), F(1), t[0], t[1])


# --- the scene description --------------------------------------------------------------------------------------------
def poly(*pts):
    return tuple((F(x), F(y)) for x, y in pts)


def rect(x0, y0, x1, y1):
    return poly((x0, y0), (x1, y0), (x1, y1), (x0, y1))


def solid(r, g, b, a):
    return (SOLID, tuple(F(v) for v in (r, g, b, a)))


def foreground(m):
    return (FOREGROUND, F(m))


def linear(p0, p1, stops):
    offs = [F(o) for o, _ in stops]
    assert len(offs) >= 2 and all(offs[k] < offs[k + 1] for k in range(len(offs) - 1)), "offsets strictly increase"
    return (LINEAR, (F(p0[0]), F(p0[1])), (F(p1[0]), F(p1[1])),
            tuple((F(o), tuple(F(v) for v in c)) for o, c in stops))


def image(width, height, texels, sx, sy, tx, ty):
    """A NEAREST image of straight rgba8 texels (row by row, top first) mapped to the tile by x = sx u + tx, y = sy v + ty."""
    assert len(texels) == width * height * 4 and sx > 0 and sy > 0
    return (IMAGE, width, height, tuple(int(v) for v in texels), tuple(F(v) for v in (sx, sy, tx, ty)))


def nested(pat):
    return (PATTERN, pat)


def fill(region, paint):
    return ("fill", region, paint)


def gpush(opacity, composite=SRC_OVER):
    return ("gpush", F(opacity), composite)


GPOP = ("gpop",)


def cpush(region):
    return ("cpush", region)


CPOP = ("cpop",)


def build_tree(ops):
    """The flat operations as a tree: ("fill", R, paint), ("group", o, composite, items), ("clip", R, items)."""
    root = []
    stack = [("root", root)]
    for op in ops:
        k = op[0]
        if k == "fill":
            stack[-1][1].append(op)
        elif k == "gpush":
            node = ("group", op[1], op[2], [])
            stack[-1][1].append(node)
            stack.append(("group", node[3]))
        elif k == "cpush":
            node = ("clip", op[1], [])
            stack[-1][1].append(node)
            stack.append(("clip", node[2]))
        elif k in ("gpop", "cpop"):
            if len(stack) < 2 or stack[-1][0] != ("group" if k == "gpop" else "clip"):
                raise ValueError("unbalanced pop")
            stack.pop()
        else:
            raise ValueError("unknown operation " + repr(k))
    if len(stack) != 1:
        raise ValueError("unclosed push")
    return root


class Pat(object):
    """A pattern: cell (x, y, w, h), steps, repeats, tint (r, g, b, ta) or None, paint alpha, map, tile operations."""

    def __init__(self, cell, step, ops, repeat=(1, 1), tint=None, alpha=1.0, map=ID):
        self.x, self.y, self.w, self.h = (F(v) for v in cell)
        self.xs, self.ys = (F(v) for v in step)
        self.rx, self.ry = int(repeat[0]), int(repeat[1])
        self.tint = None if tint is None else tuple(F(v) for v in tint)
        self.alpha = F(alpha)
        self.map = tuple(F(v) for v in map)
        self.ops = tuple(ops)
        self.tree = build_tree(self.ops)
        self.has_blend = tree_has_blend(self.tree)


def tree_has_blend(items):
    for it in items:
        if it[0] == "group" and (it[2] != SRC_OVER or tree_has_blend(it[3])):
            return True
        if it[0] == "clip" and tree_has_blend(it[2]):
            return True
    return False


class Scene(object):
    def __init__(self, name, pat, width=8, height=8, fg=(0.9, 0.3, 0.1, 0.8), tol=1e-12, note=""):
        self.name, self.pat, self.width, self.height = name, pat, width, height
        self.fg = tuple(F(v) for v in fg)
        self.tol, self.note = tol, note


# --- the lattice (PT5) ------------------------------------------------------------------------------------------------
_ACC = {}


def accumulate(step, i):
    """`step` added `|i|` times in doubles (the planted defect), negated for a negative index: i roundings."""
    s = float(step)
    tab = _ACC.get(s)
    if tab is None:
        tab = _ACC[s] = array("d", [0.0])
    n = abs(i)
    while len(tab) <= n:
        tab.append(tab[-1] + s)
    return tab[n] if i >= 0 else -tab[n]


def translation_axis(step, i):
    """The translation of the index @p i: the exact product, or (planted) the accumulated sum."""
    if "add-step" in PLANTS:
        return F(accumulate(step, i))
    return step * i


def dbl_left(x, step, i):
    """The library's left edge in doubles: one product and one sum."""
    return float(x) + float(i) * float(step)


def cell_edges(p, i, j):
    """The exact (left, top, right, bottom) the oracle uses for the cell (i, j); the planted forms replace it."""
    tx, ty = translation_axis(p.xs, i), translation_axis(p.ys, j)
    left, top = p.x + tx, p.y + ty
    if p.xs == p.w:
        right = p.x + translation_axis(p.xs, i + 1)
        if "left-plus-w" in PLANTS:
            right = F(dbl_left(p.x, p.xs, i) + float(p.w))
    else:
        right = left + p.w
    if p.ys == p.h:
        bottom = p.y + translation_axis(p.ys, j + 1)
        if "left-plus-w" in PLANTS:
            bottom = F(dbl_left(p.y, p.ys, j) + float(p.h))
    else:
        bottom = top + p.h
    return left, top, right, bottom


def true_cell(p, i, j):
    """The exact cell of PT5, no plant."""
    left, top = p.x + p.xs * i, p.y + p.ys * j
    return left, top, left + p.w, top + p.h


def sim_cell(p, i, j):
    """The library's double formula of PT5 simulated in Python floats (IEEE doubles, no fused operations), planted or not."""
    x, y, w, h, xs, ys = (float(v) for v in (p.x, p.y, p.w, p.h, p.xs, p.ys))
    if "add-step" in PLANTS:
        left, top = x + accumulate(xs, i), y + accumulate(ys, j)
    else:
        left, top = x + float(i) * xs, y + float(j) * ys
    if xs == w and "left-plus-w" not in PLANTS:
        right = x + (accumulate(xs, i + 1) if "add-step" in PLANTS else float(i + 1) * xs)
    else:
        right = left + w
    if ys == h and "left-plus-w" not in PLANTS:
        bottom = y + (accumulate(ys, j + 1) if "add-step" in PLANTS else float(j + 1) * ys)
    else:
        bottom = top + h
    return left, top, right, bottom


def ulp_distance(a, b):
    """How many doubles lie between @p a and @p b (0 for equal)."""
    def key(v):
        (u,) = struct.unpack("<q", struct.pack("<d", v))
        return u if u >= 0 else -(u & 0x7fffffffffffffff)
    return abs(key(a) - key(b))


def overlaps(p):
    """PT7 holds: on a repeating axis, the step is less than the size (exact comparison)."""
    return bool((p.rx and p.xs < p.w) or (p.ry and p.ys < p.h))


def degenerate(p):
    return p.w == 0 or p.h == 0 or (p.rx and p.xs == 0) or (p.ry and p.ys == 0)


def notice(maps, w, h, xs, ys, rx, ry):
    """PT10: 23 when some map has a 2x2 part of determinant exactly 0, else 37 for a zero size or a zero step on a repeating axis."""
    for m in maps:
        if F(m[0]) * F(m[3]) - F(m[1]) * F(m[2]) == 0:
            return REASON_NOT_INVERTIBLE
    if w == 0 or h == 0 or (rx and xs == 0) or (ry and ys == 0):
        return REASON_DEGENERATE
    return REASON_NONE


# --- compositing ------------------------------------------------------------------------------------------------------
def over(L, FS):
    """FS (a premultiplied source already scaled by its shape) over the layer L."""
    ia = 1 - FS[3]
    return (FS[0] + ia * L[0], FS[1] + ia * L[1], FS[2] + ia * L[2], FS[3] + ia * L[3])


def apply(L, S, k, composite):
    """apply(L, S, k, composite) on premultiplied values, S scaled by the coverage k."""
    ks = tuple(k * v for v in S)
    if composite == SRC_OVER:
        return over(L, ks)
    assert composite == MULTIPLY
    ia, ib = 1 - ks[3], 1 - L[3]
    return (ib * ks[0] + ia * L[0] + ks[0] * L[0], ib * ks[1] + ia * L[1] + ks[1] * L[1],
            ib * ks[2] + ia * L[2] + ks[2] * L[2], ks[3] + L[3] - ks[3] * L[3])


# --- paint integrals --------------------------------------------------------------------------------------------------
def premul(c):
    return (c[0] * c[3], c[1] * c[3], c[2] * c[3], c[3])


def triangles(P):
    return [(P[0], P[k], P[k + 1]) for k in range(1, len(P) - 1)]


def linear_integral(P, paint):
    """The integral over the convex polygon @p P (tile space) of the premultiplied gradient color."""
    _, p0, p1, stops = paint
    dx, dy = p1[0] - p0[0], p1[1] - p0[1]
    dd = dx * dx + dy * dy
    ta, tb, tc = dx / dd, dy / dd, -(p0[0] * dx + p0[1] * dy) / dd         # t(x, y) = ta x + tb y + tc
    total = [F(0)] * 4

    def constant(piece, color):
        a = area(piece)
        if a:
            pc = premul(color)
            for k in range(4):
                total[k] += a * pc[k]

    first_o, first_c = stops[0]
    last_o, last_c = stops[-1]
    constant(clip_half(P, -ta, -tb, first_o - tc), first_c)                 # t <= first offset
    constant(clip_half(P, ta, tb, tc - last_o), last_c)                     # t >= last offset
    for k in range(len(stops) - 1):
        o0, c0 = stops[k]
        o1, c1 = stops[k + 1]
        piece = clip_half(clip_half(P, ta, tb, tc - o0), -ta, -tb, o1 - tc)
        if len(piece) < 3:
            continue
        for a_, b_, c_ in triangles(piece):
            tri = abs((b_[0] - a_[0]) * (c_[1] - a_[1]) - (c_[0] - a_[0]) * (b_[1] - a_[1])) / 2
            if tri == 0:
                continue
            for m in (((a_[0] + b_[0]) / 2, (a_[1] + b_[1]) / 2), ((b_[0] + c_[0]) / 2, (b_[1] + c_[1]) / 2),
                      ((c_[0] + a_[0]) / 2, (c_[1] + a_[1]) / 2)):
                u = (ta * m[0] + tb * m[1] + tc - o0) / (o1 - o0)
                col = premul(tuple(c0[q] + u * (c1[q] - c0[q]) for q in range(4)))
                for q in range(4):
                    total[q] += tri / 3 * col[q]
    return tuple(total)


def image_integral(P, paint):
    """The integral over the convex polygon @p P (tile space) of the NEAREST, PAD image's premultiplied texel."""
    _, width, height, texels, (sx, sy, tx, ty) = paint
    total = [F(0)] * 4
    for iy in range(height):
        row = P
        if iy > 0:
            row = clip_half(row, 0, 1, -(ty + sy * iy))
        if iy < height - 1:
            row = clip_half(row, 0, -1, ty + sy * (iy + 1))
        if len(row) < 3:
            continue
        for ix in range(width):
            cell = row
            if ix > 0:
                cell = clip_half(cell, 1, 0, -(tx + sx * ix))
            if ix < width - 1:
                cell = clip_half(cell, -1, 0, tx + sx * (ix + 1))
            a = area(cell)
            if not a:
                continue
            o = (iy * width + ix) * 4
            col = premul(tuple(F(texels[o + q], 255) for q in range(4)))
            for q in range(4):
                total[q] += a * col[q]
    return tuple(total)


# --- the evaluation ---------------------------------------------------------------------------------------------------
class Ctx(object):
    """What a pixel's evaluation of one pattern needs: its chain map C, the inverse, the pixel, the foreground."""

    def __init__(self, C, px, py, fg):
        self.C, self.Cinv, self.px, self.py, self.fg = C, invert(C), px, py, fg
        self.pixel = [(F(px), F(py)), (F(px + 1), F(py)), (F(px + 1), F(py + 1)), (F(px), F(py + 1))]


class Copy(object):
    def __init__(self, i, j, t, cellpix):
        self.i, self.j, self.t, self.cellpix = i, j, t, cellpix


def enumerate_copies(p, ctx):
    """The copies (i, j) of the pixel in lattice order, each with its translation and its cell clipped to the pixel."""
    xs_ = [mapf(ctx.Cinv, q) for q in ctx.pixel]
    minx, maxx = min(v[0] for v in xs_), max(v[0] for v in xs_)
    miny, maxy = min(v[1] for v in xs_), max(v[1] for v in xs_)
    ext = 2 if "content-not-clipped" in PLANTS else 0
    if p.rx:
        i0, i1 = ceil((minx - p.x - p.w) / p.xs) - ext, floor((maxx - p.x) / p.xs) + ext
    else:
        i0 = i1 = 0
    if p.ry:
        j0, j1 = ceil((miny - p.y - p.h) / p.ys) - ext, floor((maxy - p.y) / p.ys) + ext
    else:
        j0 = j1 = 0
    out = []
    for j in range(j0, j1 + 1):
        for i in range(i0, i1 + 1):
            t = (translation_axis(p.xs, i), translation_axis(p.ys, j))
            if "content-not-clipped" in PLANTS:
                out.append(Copy(i, j, t, ctx.pixel))
                continue
            l, tp, r, b = cell_edges(p, i, j)
            quad = ccw([mapf(ctx.C, (l, tp)), mapf(ctx.C, (r, tp)), mapf(ctx.C, (r, b)), mapf(ctx.C, (l, b))])
            if quad is None:
                continue
            cp = convex_clip(ctx.pixel, quad)
            if len(cp) < 3 or area(cp) == 0:
                continue
            out.append(Copy(i, j, t, ccw(cp)))
    return out


def device_region(ctx, region, t):
    return [mapf(ctx.C, (v[0] + t[0], v[1] + t[1])) for v in region]


def paint_integral(paint, reg, A, ctx, t):
    """The integral over the device region @p reg (area @p A) of the paint, evaluated in the frame translated by @p t."""
    kind = paint[0]
    if kind == SOLID:
        pc = premul(paint[1])
        return tuple(A * v for v in pc)
    if kind == FOREGROUND:
        fg = ctx.fg if "foreground-from-creator" not in PLANTS else tuple(F(v) for v in CREATOR_FOREGROUND)
        pc = premul((fg[0], fg[1], fg[2], fg[3] * paint[1]))
        return tuple(A * v for v in pc)
    if kind == PATTERN:
        inner = paint[1]
        C = compose(compose(ctx.C, translate(t)), inner.map)
        S = pattern_pixel(inner, C, ctx.px, ctx.py, ctx.fg)
        return tuple(A * v for v in S)
    tile = [(q[0] - t[0], q[1] - t[1]) for q in (mapf(ctx.Cinv, d) for d in reg)]
    return linear_integral(tile, paint) if kind == LINEAR else image_integral(tile, paint)


def draw_fill(item, copies, layer, clips, ctx):
    _, region, paint = item
    total = [F(0)] * 4
    for cp in copies:
        reg = convex_clip(device_region(ctx, region, cp.t), cp.cellpix)
        A = area(reg)
        if A == 0:
            continue
        c = F(1)
        for clip in clips:
            c *= area(convex_clip(device_region(ctx, clip, cp.t), cp.cellpix))
        if c == 0:
            continue
        tp = (F(0), F(0)) if "copy0-source" in PLANTS else cp.t
        I = paint_integral(paint, reg, A, ctx, tp)
        FS = tuple(c * v for v in I)
        if "separate-shapes" in PLANTS:
            layer = over(layer, FS)
        else:
            for k in range(4):
                total[k] += FS[k]
    if "separate-shapes" not in PLANTS:
        layer = over(layer, tuple(total))
    return layer


def draw_group(item, copies, layer, clips, ctx):
    _, o, composite, items = item
    if clips:
        raise ValueError("a group pushed while a clip is in force is not defined here")
    G = draw_items(items, copies, ZERO4, [], ctx)
    return apply(layer, tuple(o * v for v in G), 1, composite)


def draw_items(items, copies, layer, clips, ctx):
    for it in items:
        if it[0] == "fill":
            layer = draw_fill(it, copies, layer, clips, ctx)
        elif it[0] == "group":
            if "group-per-copy" in PLANTS:
                for cp in copies:
                    layer = draw_group(it, [cp], layer, clips, ctx)
            else:
                layer = draw_group(it, copies, layer, clips, ctx)
        else:
            layer = draw_items(it[2], copies, layer, clips + [it[1]], ctx)
    return layer


def lattice_layer(p, ctx, copies, algorithm=None):
    """The lattice picture at the pixel (PT6, PT7, PT8): premultiplied rgba."""
    layer = BACKDROP if "tile-not-isolated" in PLANTS else ZERO4
    if not overlaps(p):
        return draw_items(p.tree, copies, layer, [], ctx)
    algorithm = algorithm or FORCE_PT7[0] or ("general" if p.has_blend else "isolated")
    order = list(reversed(copies)) if "reverse-lattice-order" in PLANTS else copies
    for cp in order:
        if algorithm == "general":
            layer = draw_items(p.tree, [cp], layer, [], ctx)
        else:
            layer = apply(layer, draw_items(p.tree, [cp], ZERO4, [], ctx), 1, SRC_OVER)
    return layer


def pattern_pixel(p, C, px, py, fg, algorithm=None):
    """The premultiplied value of the pattern use with the chain map @p C at the pixel (PT4, PT9)."""
    if degenerate(p) or det(C) == 0:
        return ZERO4
    ctx = Ctx(C, px, py, fg)
    L = lattice_layer(p, ctx, enumerate_copies(p, ctx), algorithm)
    if p.tint is None:
        return tuple(p.alpha * v for v in L)
    r, g, b, ta = p.tint
    if "keeps-tile-colors" in PLANTS:
        return tuple(ta * p.alpha * v for v in L)
    alpha = L[3] * ta * p.alpha
    return (r * alpha, g * alpha, b * alpha, alpha)


def render(scene, algorithm=None):
    """The premultiplied value of every pixel, row by row."""
    p = scene.pat
    return [[pattern_pixel(p, p.map, px, py, scene.fg, algorithm) for px in range(scene.width)]
            for py in range(scene.height)]


# --- the scene catalog ------------------------------------------------------------------------------------------------
def cos_sin_degrees(deg):
    """cos and sin of @p deg degrees as the nearest doubles, by Taylor series in Fractions (not the platform's libm)."""
    pi = F(31415926535897932384626433832795028841971693993751, 10 ** 49)
    x = F(deg) * pi / 180
    c, s, term_c, term_s = F(0), F(0), F(1), x
    for n in range(0, 60):
        c += term_c
        s += term_s
        term_c = -term_c * x * x / ((2 * n + 1) * (2 * n + 2))
        term_s = -term_s * x * x / ((2 * n + 2) * (2 * n + 3))
    return float(c), float(s)


BG = (0.8, 0.2, 0.1, 0.75)
BAND = (0.1, 0.6, 0.3, 0.5)
DOT = (0.1, 0.3, 0.9, 1.0)
EDGE = (0.9, 0.8, 0.1, 0.6)


def std_tile(w, h, bg=BG, band=BAND, dot=DOT, edge=EDGE):
    """A cell-filling background, a band across the cell, a triangle, and a corner rectangle touching two cell edges."""
    return [fill(rect(0, 0, w, h), solid(*bg)),
            fill(rect(0, 0.4 * h, w, 0.6 * h), solid(*band)),
            fill(poly((0.2 * w, 0.2 * h), (0.8 * w, 0.3 * h), (0.4 * w, 0.85 * h)), solid(*dot)),
            fill(rect(0.55 * w, 0.55 * h, w, h), solid(*edge))]


def catalog():
    """The scenes of the vectors, in order.  Every number is a double; the catalog is the only place a scene is written."""
    S = []
    c17, s17 = cos_sin_degrees(17)
    rot17 = (c17, -s17, s17, c17, 2.0, -1.0)
    ulp_below_2 = struct.unpack("<d", struct.pack("<q", struct.unpack("<q", struct.pack("<d", 2.0))[0] - 1))[0]

    def add(name, pat, **kw):
        S.append(Scene(name, pat, **kw))

    # --- abutting (PT6): no seam pixel
    add("abut-translate", Pat((0, 0, 2, 2), (2, 2), std_tile(2, 2), map=(1, 0, 0, 1, 0.375, 0.625)))
    add("abut-rotate17", Pat((0, 0, 3, 3), (3, 3), std_tile(3, 3), map=rot17))
    add("abut-step-0.1", Pat((0, 0, 0.1, 0.1), (0.1, 0.1), std_tile(0.1, 0.1), map=(10, 0, 0, 10, 0.3, 0.7)))
    # --- gapped (CSS space), under a skew and under a scale
    add("gapped-skew", Pat((0, 0, 2, 2), (3, 2), std_tile(2, 2), map=(1, 0.5, 0, 1, 0.25, 0.5)))
    add("gapped-both", Pat((0, 0, 1.5, 2), (2.25, 2.5), std_tile(1.5, 2), map=(1.25, 0, 0, 1.25, 0.4, 0.3)))
    # --- overlapping (PT7), in x, in y, in both, and one ulp below the size
    add("overlap-x", Pat((0, 0, 3, 2), (2, 2), std_tile(3, 2), map=(1, 0, 0, 1, 0.5, 0.25)))
    add("overlap-y", Pat((0, 0, 2, 3), (2, 2), std_tile(2, 3), map=(1, 0, 0, 1, 0.5, 0.25)))
    add("overlap-xy", Pat((0, 0, 3, 3), (2, 2), std_tile(3, 3), map=(1, 0, 0, 1, 0.25, 0.5)))
    add("overlap-one-ulp", Pat((0, 0, 2, 2), (ulp_below_2, 2), std_tile(2, 2), map=(1, 0, 0, 1, 0.5, 0.5)),
        note="x_step is one ulp below w: PT7, so the seam pixel shows the separate-layers artifact")
    add("abut-same-map", Pat((0, 0, 2, 2), (2, 2), std_tile(2, 2), map=(1, 0, 0, 1, 0.5, 0.5)),
        note="the same as overlap-one-ulp with x_step = w: PT6, no seam")
    # --- blend modes and groups in the tile
    blend_overlap = [fill(rect(0, 0, 3, 3), solid(0.2, 0.8, 0.9, 0.8)), gpush(1.0, MULTIPLY),
                     fill(rect(0.5, 0.5, 2.5, 2.5), solid(0.9, 0.3, 0.7, 1.0)), GPOP,
                     fill(poly((0.2, 0.2), (2.0, 0.4), (0.8, 2.6)), solid(*DOT))]
    add("overlap-multiply-group", Pat((0, 0, 3, 3), (2, 3), blend_overlap, map=(1, 0, 0, 1, 0.5, 0.25)))
    normal_group = [fill(rect(0, 0, 3, 3), solid(0.8, 0.2, 0.1, 0.6)), gpush(0.5, SRC_OVER),
                    fill(rect(0.5, 0.5, 2.5, 2.5), solid(0.1, 0.6, 0.3, 1.0)), GPOP]
    add("overlap-normal-group", Pat((0, 0, 3, 3), (2.5, 2), normal_group, map=(1, 0, 0, 1, 0.25, 0.25)))
    add("abut-multiply-group", Pat((0, 0, 2, 2), (2, 2), [fill(rect(0, 0, 2, 2), solid(0.9, 0.8, 0.1, 0.75)),
                                                          gpush(1.0, MULTIPLY),
                                                          fill(rect(0, 0, 2, 2), solid(0.2, 0.8, 0.9, 1.0)), GPOP],
                                   map=(1, 0, 0, 1, 0.5, 0.375)))
    add("abut-opacity-group", Pat((0, 0, 2, 2), (2, 2), [gpush(0.5, SRC_OVER),
                                                         fill(rect(0, 0, 2, 2), solid(0.8, 0.2, 0.1, 1.0)),
                                                         fill(rect(0, 0.5, 2, 1.25), solid(*BAND)), GPOP,
                                                         fill(poly((0.3, 0.3), (1.2, 0.5), (0.6, 1.6)), solid(*DOT))],
                                  map=(1, 0, 0, 1, 0.5, 0.25)))
    # --- paints that move with their copy
    stops3 = [(0.0, (1.0, 0.0, 0.0, 1.0)), (0.5, (0.0, 1.0, 0.0, 0.5)), (1.0, (0.0, 0.0, 1.0, 1.0))]
    grad_tile = [fill(rect(0, 0, 2, 2), linear((0, 0), (2, 0), stops3)),
                 fill(poly((0.3, 0.3), (1.2, 0.5), (0.6, 1.6)), solid(*DOT))]
    add("gradient-abut", Pat((0, 0, 2, 2), (2, 2), grad_tile, map=(1, 0, 0, 1, 0.5, 0.25)))
    add("gradient-gapped", Pat((0, 0, 2, 2), (3, 2.5),
                               [fill(rect(0, 0, 2, 2), linear((0, 0), (2, 2), stops3))],
                               map=(1, 0, 0, 1, 0.3, 0.4)))
    add("gradient-overlap", Pat((0, 0, 3, 2), (2, 2), [fill(rect(0, 0, 3, 2), linear((0, 0), (3, 0), stops3[::2]))],
                                map=(1, 0, 0, 1, 0.5, 0.25)))
    texels = [255, 0, 0, 255, 0, 255, 0, 128, 0, 0, 255, 255,
              255, 255, 0, 64, 0, 255, 255, 255, 128, 128, 128, 200]
    add("image-nearest", Pat((0, 0, 3, 2), (3, 2),
                             [fill(rect(0, 0, 3, 2), image(3, 2, texels, 1, 1, 0, 0)),
                              fill(poly((0.2, 0.2), (1.2, 0.5), (0.6, 1.6)), solid(*DOT))],
                             map=(1.25, 0, 0, 1.25, 0.4, 0.3)))
    inner = Pat((0, 0, 1.5, 1.5), (1.5, 1.5), std_tile(1.5, 1.5, bg=(0.2, 0.4, 0.8, 0.5)), map=(1, 0, 0, 1, 0.25, 0.125))
    add("nested", Pat((0, 0, 4, 4), (4, 4), [fill(rect(0, 0, 4, 4), nested(inner)),
                                             fill(rect(3, 0, 4, 4), solid(*EDGE))],
                      map=(2, 0, 0, 2, 0.3, 0.7)), note="the outer step is not a multiple of the inner step")
    # --- clips and thin shapes in the tile
    clip_tile = [fill(rect(0, 0, 3, 3), solid(*BG)), cpush(poly((-1, -1), (4, 1.5), (1, 4))),
                 fill(rect(0, 0, 3, 3), solid(0.1, 0.4, 0.8, 1.0)), fill(rect(0, 1, 3, 2), solid(*BAND)), CPOP]
    add("clip-block", Pat((0, 0, 3, 3), (3, 3), clip_tile, map=(1, 0, 0, 1, 0.4, 0.3)))
    add("stroke-band", Pat((0, 0, 2, 2), (2, 2),
                           [fill(poly((-0.5, 0.3), (2.5, 0.8), (2.5, 1.3), (-0.5, 0.8)), solid(0.1, 0.1, 0.1, 0.9)),
                            fill(poly((1.7, -0.5), (2.2, -0.5), (2.2, 2.5), (1.7, 2.5)), solid(0.9, 0.5, 0.1, 1.0))],
                           map=(1, 0, 0, 1, 0.3, 0.6)))
    add("tile-spills", Pat((0, 0, 2, 2), (4, 4), [fill(rect(-1, -1, 3, 3), solid(0.3, 0.6, 0.2, 0.5)),
                                                  fill(rect(0.5, 0.5, 1.5, 1.5), solid(*DOT))],
                           map=(1, 0, 0, 1, 0.25, 0.25)), note="content past the cell: nothing may reach the gap")
    # --- colored and uncolored
    add("uncolored-rotate", Pat((0, 0, 3, 3), (3, 3), std_tile(3, 3), tint=(0.9, 0.1, 0.2, 1.0), map=rot17))
    add("uncolored-gradient", Pat((0, 0, 2, 2), (2, 2), grad_tile, tint=(0.2, 0.7, 0.4, 1.0), map=(1, 0, 0, 1, 0.5, 0.25)))
    add("uncolored-half", Pat((0, 0, 2, 2), (2, 2), std_tile(2, 2), tint=(0.2, 0.7, 0.4, 0.5), alpha=0.5,
                              map=(1, 0, 0, 1, 0.375, 0.625)))
    add("colored-half", Pat((0, 0, 2, 2), (2, 2), std_tile(2, 2), alpha=0.5, map=(1, 0, 0, 1, 0.375, 0.625)))
    # --- one axis and no axis
    add("repeat-x-0", Pat((0, 0, 2, 2), (2, 2), std_tile(2, 2), repeat=(0, 1), map=(1, 0, 0, 1, 2.5, 0.25)))
    add("repeat-y-0", Pat((0, 0, 2, 2), (2, 2), std_tile(2, 2), repeat=(1, 0), map=(1, 0, 0, 1, 0.25, 2.5)))
    add("repeat-none", Pat((0, 0, 2, 2), (2, 2), std_tile(2, 2), repeat=(0, 0), map=(1, 0, 0, 1, 2.5, 2.5)))
    add("repeat-x-0-small-step", Pat((0, 0, 3, 2), (1, 2), std_tile(3, 2), repeat=(0, 1), map=(1, 0, 0, 1, 1.5, 0.5)),
        note="x_step < w on an axis that does not repeat: PT6, one copy in x")
    # --- foreground, far lattice, empty
    add("foreground", Pat((0, 0, 2, 2), (2, 2), [fill(rect(0, 0, 2, 2), foreground(0.5)),
                                                 fill(poly((0.3, 0.3), (1.5, 0.5), (0.6, 1.6)), solid(*DOT)),
                                                 fill(rect(1, 1, 2, 2), foreground(1.0))],
                          map=(1, 0, 0, 1, 0.5, 0.25)))
    i0 = 999997
    e = 0.37 - 7.3 * (i0 * 0.1)
    add("far-lattice", Pat((0, 0, 0.1, 8), (0.1, 8), [fill(rect(0, 0, 0.1, 8), solid(0.1, 0.2, 0.3, 0.25)),
                                                       fill(rect(0, 0, 0.05, 8), solid(0.9, 0.2, 0.1, 1.0))],
                           repeat=(1, 0), map=(7.3, 0, 0, 1, e, 0)),
        tol=1e-8, note="copy index near 10^6 at step 0.1: the double positions carry one ulp of 1e5")
    add("empty-tile", Pat((0, 0, 2, 2), (2, 2), [], map=(1, 0, 0, 1, 0.5, 0.5)), width=4, height=4)
    return S


# --- position, overlap and notice cases -------------------------------------------------------------------------------
def position_cases():
    """(pattern, i, j) triples: abutting, offset, gapped and overlapping lattices, and the indices where `left + w` fails."""
    third = 1.0 / 3.0
    big = [0, 1, 2, 3, 7, 10, 100, 999, 1000, 12345, 999999, 1000000, -1, -7, -1000]
    pats = []
    for w in (0.1, 0.3, third):
        pats.append(("abut-%s" % repr(w), Pat((0, 0, w, w), (w, w), [])))
    pats.append(("abut-offset-0.1", Pat((0.25, 0.75, 0.1, 0.1), (0.1, 0.1), [])))
    pats.append(("gapped", Pat((0, 0, 0.1, 0.1), (0.3, 0.3), [])))
    pats.append(("overlap", Pat((0, 0, 0.3, 0.3), (0.1, 0.1), [])))
    cases = []
    for name, p in pats:
        idx = [i for i in big if i >= 0 or i == -1000] if p.x != 0 else list(big)
        if p.xs == p.w and p.x == 0:
            # the first index where the library's right edge differs from `left + w` in doubles
            w = float(p.w)
            for i in range(1, 200000):
                if dbl_left(p.x, p.xs, i) + w != dbl_left(p.x, p.xs, i + 1):
                    idx.append(i)
                    break
            else:
                raise AssertionError("no index where left + w differs for w = %r" % w)
        for i in idx:
            cases.append((name, p, i, i))
        if p.x == 0 and name != "overlap":   # a negative index with a sum that cancels is no test of the product
            cases.append((name, p, 5, -3))
            cases.append((name, p, -2, 1000000))
    return cases


def overlap_cases():
    """(w, h, x_step, y_step, repeat_x, repeat_y) rows for the predicate."""
    below = struct.unpack("<d", struct.pack("<q", struct.unpack("<q", struct.pack("<d", 2.0))[0] - 1))[0]
    above = struct.unpack("<d", struct.pack("<q", struct.unpack("<q", struct.pack("<d", 2.0))[0] + 1))[0]
    rows = []
    for rx, ry in ((1, 1), (1, 0), (0, 1), (0, 0)):
        for xs, ys in ((2.0, 2.0), (below, 2.0), (2.0, below), (below, below), (above, 2.0), (2.0, above), (1.0, 3.0),
                       (3.0, 1.0), (4.0, 4.0), (0.5, 0.5)):
            rows.append((2.0, 2.0, xs, ys, rx, ry))
    return rows


def notice_cases():
    """(name, maps, w, h, xs, ys, rx, ry) rows for PT10."""
    I = (1.0, 0.0, 0.0, 1.0, 0.0, 0.0)
    rot = (0.6, -0.8, 0.8, 0.6, 5.0, 7.0)
    zero_x = (0.0, 0.0, 0.0, 1.0, 0.0, 0.0)
    rank1 = (1.0, 2.0, 2.0, 4.0, 0.0, 0.0)
    tiny = (2.0 ** -600, 0.0, 0.0, 2.0 ** -600, 0.0, 0.0)
    rows = [("plain", [I, I, I], 2.0, 2.0, 2.0, 2.0, 1, 1),
            ("moved", [rot, I, rot], 2.0, 2.0, 2.0, 2.0, 1, 1),
            ("singular-target", [zero_x, I, I], 2.0, 2.0, 2.0, 2.0, 1, 1),
            ("singular-instance", [I, rank1, I, I], 2.0, 2.0, 2.0, 2.0, 1, 1),
            ("singular-item", [I, I, zero_x, I], 2.0, 2.0, 2.0, 2.0, 1, 1),
            ("singular-pattern", [I, I, rank1], 2.0, 2.0, 2.0, 2.0, 1, 1),
            ("singular-and-degenerate", [I, I, rank1], 0.0, 2.0, 2.0, 2.0, 1, 1),
            ("two-tiny-factors", [tiny, tiny, I], 2.0, 2.0, 2.0, 2.0, 1, 1),
            ("w-zero", [I, I, I], 0.0, 2.0, 2.0, 2.0, 1, 1),
            ("h-zero", [I, I, I], 2.0, 0.0, 2.0, 2.0, 1, 1),
            ("x-step-zero", [I, I, I], 2.0, 2.0, 0.0, 2.0, 1, 1),
            ("y-step-zero", [I, I, I], 2.0, 2.0, 2.0, 0.0, 1, 1),
            ("x-step-zero-no-repeat", [I, I, I], 2.0, 2.0, 0.0, 2.0, 0, 1),
            ("y-step-zero-no-repeat", [I, I, I], 2.0, 2.0, 2.0, 0.0, 1, 0),
            ("both-steps-zero-no-repeat", [I, I, I], 2.0, 2.0, 0.0, 0.0, 0, 0),
            ("zero-size-no-repeat", [I, I, I], 0.0, 0.0, 2.0, 2.0, 0, 0)]
    return rows


def notice_of(row):
    _, maps, w, h, xs, ys, rx, ry = row
    return notice(maps, F(w), F(h), F(xs), F(ys), rx, ry)


# --- the selftest -----------------------------------------------------------------------------------------------------
class Check(object):
    def __init__(self):
        self.count = 0
        self.fails = []

    def __call__(self, ok, what):
        self.count += 1
        if not ok:
            self.fails.append(what)


def px_value(scene, px, py, algorithm=None):
    return pattern_pixel(scene.pat, scene.pat.map, px, py, scene.fg, algorithm)


def shifted(pat, k, m):
    """The same pattern with the chain map composed with the translation by (k x_step, m y_step): the same lattice."""
    return Pat((pat.x, pat.y, pat.w, pat.h), (pat.xs, pat.ys), pat.ops, (pat.rx, pat.ry), pat.tint, pat.alpha,
               compose(pat.map, translate((translation_axis(pat.xs, k), translation_axis(pat.ys, m)))))


def run_selftest():
    ck = Check()
    cat = {s.name: s for s in catalog()}
    mid = F(1, 2)

    # --- hand-computed cells: a 1.5-wide stripe in a 2-wide cell, shifted by half a pixel: .5, 1, .5, 1 along a row
    hand = Scene("hand", Pat((0, 0, 2, 2), (2, 2), [fill(rect(0, 0, 1.5, 2), solid(1, 0, 0, 1))], map=(1, 0, 0, 1, 0.5, 0)),
                 width=4, height=2)
    want = [F(1, 2), F(1), F(1, 2), F(1)]
    for px in range(4):
        v = px_value(hand, px, 0)
        ck(v == (want[px], F(0), F(0), want[px]), "hand stripe pixel %d: %r" % (px, v))
    # an abutting translucent background is its own value at every pixel, seams included
    bg = Scene("hand-bg", Pat((0, 0, 2, 2), (2, 2), [fill(rect(0, 0, 2, 2), solid(1, 0.5, 0, 0.5))], map=(1, 0, 0, 1, 0.25, 0.75)),
               width=5, height=5)
    for py in range(5):
        for px in range(5):
            ck(px_value(bg, px, py) == (F(1, 2), F(1, 4), F(0), F(1, 2)),
               "abutting background pixel (%d, %d) is exactly the background (PT6)" % (px, py))
    # an opaque stripe at the corner of four cells: the four quarter areas add to one
    corner = Scene("hand-corner", Pat((0, 0, 2, 2), (2, 2), [fill(rect(1, 1, 2, 2), solid(0, 0, 0, 1)),
                                                              fill(rect(0, 0, 1, 1), solid(0, 0, 0, 1))],
                                      map=(1, 0, 0, 1, 0.5, 0.5)), width=4, height=4)
    # a checkerboard of unit squares shifted by half a pixel: every pixel holds a quarter of each of two squares (pixel
    # (0,0): [0,.5]^2 of the square [-.5,.5]^2 and [.5,1]^2 of the square [.5,1.5]^2).  They are two units, composited
    # one after the other: 1 - (3/4)(3/4) = 7/16
    for q in ((0, 0), (1, 1), (1, 0), (3, 2)):
        ck(px_value(corner, q[0], q[1])[3] == F(7, 16), "hand corner pixel %r is 7/16" % (q,))

    # --- PT6 versus PT7 at the seam: the same seam pixel, abutting and one ulp below
    a, b = cat["abut-same-map"], cat["overlap-one-ulp"]
    ck(any(px_value(a, 2, py) != px_value(b, 2, py) for py in range(8)),
       "the lattice at x_step == w (PT6) and at one ulp below (PT7) differ at the seam column")
    # an isolated bg only: PT6 gives .75, PT7 gives 1 - (1 - .375)(1 - .375)
    bg6 = Scene("bg6", Pat((0, 0, 2, 2), (2, 2), [fill(rect(0, 0, 2, 2), solid(0, 0, 0, 0.75))], map=(1, 0, 0, 1, 0.5, 0)), width=4, height=1)
    below = cat["overlap-one-ulp"].pat.xs
    bg7 = Scene("bg7", Pat((0, 0, 2, 2), (below, 2), [fill(rect(0, 0, 2, 2), solid(0, 0, 0, 0.75))], map=(1, 0, 0, 1, 0.5, 0)), width=4, height=1)
    ck(px_value(bg6, 2, 0)[3] == F(3, 4), "PT6 seam pixel alpha is the background's .75 (no 1 - (1 - a)(1 - b))")
    ck(abs(px_value(bg7, 2, 0)[3] - (1 - (1 - F(3, 8)) * (1 - F(3, 8)))) < F(1, 10 ** 15),
       "PT7 seam pixel is the two layers composited: 1 - (1 - .375)^2 (up to the ulp of the overlap)")

    # --- the all-Normal PT7 tier equals the general form, exactly; a blend tile does not equal the isolated form
    for name in ("overlap-x", "overlap-y", "overlap-xy", "overlap-one-ulp", "overlap-normal-group", "gradient-overlap"):
        s = cat[name]
        for py in range(0, s.height, 3):
            for px in range(0, s.width, 3):
                ck(px_value(s, px, py, "isolated") == px_value(s, px, py, "general"),
                   "%s (%d, %d): isolated and general PT7 agree exactly" % (name, px, py))
    s = cat["overlap-multiply-group"]
    ck(any(px_value(s, px, py, "isolated") != px_value(s, px, py, "general") for py in range(8) for px in range(8)),
       "a blend tile differs between the isolated form and the general form")
    ck(s.pat.has_blend and not any(cat[n].pat.has_blend for n in ("overlap-x", "overlap-normal-group")),
       "has_blend sees a MULTIPLY group and no other composite")

    # --- lattice order: opaque copies in an overlap, the later copy on top
    ov = Scene("order", Pat((0, 0, 3, 1), (2, 1), [fill(rect(0, 0, 3, 1), solid(1, 0, 0, 1)), fill(rect(2.5, 0, 3, 1), solid(0, 0, 1, 1))],
                            map=ID), width=5, height=1)
    # cell 0 = [0,3] (red, blue at [2.5,3]); cell 1 = [2,5] (red, blue at [4.5,5]); pixel 2 = [2,3]: cell 1 is later
    ck(px_value(ov, 2, 0) == (F(1), F(0), F(0), F(1)), "the later copy covers the earlier at pixel 2 (lattice order)")
    ck(px_value(ov, 0, 0) == (F(1), F(0), F(0), F(1)), "pixel 0 is the red of copy 0 alone")

    # --- the tile is isolated: outside the content the lattice picture is exactly transparent
    ck(all(px_value(Scene("gap", Pat((0, 0, 1, 1), (3, 3), [fill(rect(0, 0, 1, 1), solid(1, 1, 1, 1))], map=ID), width=3, height=3), px, py) ==
           ((F(1),) * 4 if (px, py) == (0, 0) else ZERO4) for px in range(3) for py in range(3)),
       "the gap of a gapped lattice is exactly (0, 0, 0, 0)")
    # --- content past the cell does not reach the gap
    sp = cat["tile-spills"]
    ck(all(px_value(sp, 3, k)[3] == 0 and px_value(sp, k, 3)[3] == 0 for k in range(8)),
       "tile-spills: the pixel row and column inside the gap are exactly transparent")
    spill = Scene("spill", Pat((0, 0, 1, 1), (4, 4), [fill(rect(-1, -1, 2, 2), solid(1, 1, 1, 1))], map=ID), width=4, height=4)
    ck(all(px_value(spill, px, py) == ((F(1),) * 4 if (px, py) == (0, 0) else ZERO4) for px in range(4) for py in range(4)),
       "the tile's content is restricted to its cell")

    # --- the paint moves with its copy: an integer-aligned lattice is periodic
    per = Scene("periodic", Pat((0, 0, 4, 4), (4, 4), [fill(rect(0, 0, 4, 4), linear((0, 0), (4, 0), [(0.0, (1, 0, 0, 1)), (1.0, (0, 0, 1, 1))])),
                                                       fill(rect(1, 1, 3, 3), image(2, 2, [255, 0, 0, 255, 0, 255, 0, 128, 0, 0, 255, 255, 255, 255, 255, 255], 1, 1, 1, 1))],
                            map=ID), width=8, height=8)
    ck(all(px_value(per, px, py) == px_value(per, px % 4, py % 4) for px in range(8) for py in range(8)),
       "a gradient and an image in the tile repeat with their copy (periodic pixels)")
    # the mean of the gradient at a seam pixel: the area-weighted mean of the two sides
    sg = Scene("seam-gradient", Pat((0, 0, 2, 1), (2, 1), [fill(rect(0, 0, 2, 1), linear((0, 0), (2, 0), [(0.0, (1, 0, 0, 1)), (1.0, (0, 0, 1, 1))]))],
                                    map=(1, 0, 0, 1, 0.5, 0)), width=4, height=1)
    # pixel 2 = [2,3]: [2,2.5] is copy 0 at tile x in [1.5,2]: t in [.75,1] mean .875; [2.5,3] is copy 1 at tile x in [0,.5]: mean t .125
    # straight colors (1-t, 0, t, 1): mean of premult (same alpha 1) is the mean over both halves
    want_r = (F(1, 2) * (1 - F(7, 8)) + F(1, 2) * (1 - F(1, 8)))
    v = px_value(sg, 2, 0)
    ck(v == (want_r, F(0), 1 - want_r, F(1)), "seam pixel of a gradient is the area-weighted mean of the two copies: %r" % (v,))

    # --- shift invariance: the same lattice under the map composed with an exact multiple of the step
    for name in ("abut-translate", "abut-rotate17", "gapped-skew", "overlap-x", "overlap-multiply-group", "gradient-abut",
                 "nested", "uncolored-rotate", "foreground"):
        s = cat[name]
        sh = Scene(name, shifted(s.pat, 3, -2), s.width, s.height, tuple(float(v) for v in s.fg))
        for py in range(0, s.height, 3):
            for px in range(0, s.width, 3):
                ck(px_value(s, px, py) == px_value(sh, px, py),
                   "%s (%d, %d): a shift by an exact multiple of the step gives the same pixel" % (name, px, py))

    # --- uncolored: alpha is the tile's alpha times the tint's times the paint's; the tile's colors are discarded
    unc = cat["uncolored-half"]
    tint = unc.pat.tint
    base = cat["colored-half"]
    for py in range(0, 8, 2):
        for px in range(0, 8, 2):
            u, c = px_value(unc, px, py), px_value(base, px, py)
            alpha = c[3] / F(1, 2) * tint[3] * F(1, 2)
            ck(u == (tint[0] * alpha, tint[1] * alpha, tint[2] * alpha, alpha),
               "uncolored (%d, %d): tint color with the tile's alpha times the tint's and the paint's" % (px, py))
    dark = Pat((0, 0, 2, 2), (2, 2), std_tile(2, 2, bg=(0, 0, 0, 0.75), band=(0, 0, 0, 0.5), dot=(0, 0, 0, 1), edge=(0, 0, 0, 0.6)),
               tint=(0.2, 0.7, 0.4, 0.5), alpha=0.5, map=(1, 0, 0, 1, 0.375, 0.625))
    ck(all(pattern_pixel(dark, dark.map, px, py, unc.fg) == px_value(unc, px, py) for px in range(0, 8, 2) for py in range(0, 8, 2)),
       "recoloring the tile (alphas kept) does not change an uncolored pattern")
    col = cat["colored-half"]
    full = Pat((0, 0, 2, 2), (2, 2), std_tile(2, 2), map=(1, 0, 0, 1, 0.375, 0.625))
    ck(all(px_value(col, px, py) == tuple(F(1, 2) * v for v in pattern_pixel(full, full.map, px, py, col.fg))
           for px in range(0, 8, 2) for py in range(0, 8, 2)), "colored: the lattice picture times the paint's alpha")

    # --- a group is one group over all copies: at the seam pixel the opacity applies once to the whole
    og = Scene("og", Pat((0, 0, 2, 1), (2, 1), [gpush(0.5), fill(rect(0, 0, 2, 1), solid(1, 0, 0, 1)), GPOP],
                         map=(1, 0, 0, 1, 0.5, 0)), width=4, height=1)
    ck(all(px_value(og, px, 0) == (F(1, 2), F(0), F(0), F(1, 2)) for px in range(4)), "an opacity group over abutting copies is .5 at every pixel, the seam's too")
    mg = Scene("mg", Pat((0, 0, 2, 1), (2, 1), [fill(rect(0, 0, 2, 1), solid(1, 1, 0, 1)), gpush(1.0, MULTIPLY),
                                                fill(rect(0, 0, 2, 1), solid(0, 1, 1, 1)), GPOP], map=(1, 0, 0, 1, 0.5, 0)), width=4, height=1)
    ck(all(px_value(mg, px, 0) == (F(0), F(1), F(0), F(1)) for px in range(4)), "a MULTIPLY group over abutting copies multiplies once (yellow x cyan = green)")

    # --- a FOREGROUND paint is the drawing list's color, not another
    fgs = Scene("fg", Pat((0, 0, 1, 1), (1, 1), [fill(rect(0, 0, 1, 1), foreground(0.5))], map=ID), width=2, height=2, fg=(0.8, 0.4, 0.2, 0.5))
    ck(px_value(fgs, 1, 1) == (F(0.8) * F(1, 4), F(0.4) * F(1, 4), F(0.2) * F(1, 4), F(1, 4)), "foreground: the drawing list's color, alpha .5 x .5")

    # --- one axis, both axes
    for name, nx, ny in (("repeat-x-0", 1, 8), ("repeat-y-0", 8, 1), ("repeat-none", 1, 1)):
        s = cat[name]
        cols = set()
        rows = set()
        for py in range(8):
            for px in range(8):
                if px_value(s, px, py)[3] != 0:
                    cols.add(px)
                    rows.add(py)
        ck((len(cols) <= 3 if nx == 1 else len(cols) == 8) and (len(rows) <= 3 if ny == 1 else len(rows) == 8),
           "%s draws only copy 0 on the axis that does not repeat (%d columns, %d rows)" % (name, len(cols), len(rows)))
    ck(not overlaps(cat["repeat-x-0-small-step"].pat), "x_step < w on a non-repeating axis does not overlap (PT6)")

    # --- predicates and notices
    for row in overlap_cases():
        w, h, xs, ys, rx, ry = row
        p = Pat((0, 0, w, h), (xs, ys), [], repeat=(rx, ry))
        ck(overlaps(p) == bool((rx and xs < w) or (ry and ys < h)), "overlaps %r" % (row,))
    for row in notice_cases():
        reason = notice_of(row)
        want_reason = {"plain": 0, "moved": 0, "two-tiny-factors": 0, "x-step-zero-no-repeat": 0, "y-step-zero-no-repeat": 0,
                       "both-steps-zero-no-repeat": 0}.get(row[0])
        if want_reason is None:
            want_reason = 23 if row[0].startswith("singular") else 37
        ck(reason == want_reason, "notice %s: %d (want %d)" % (row[0], reason, want_reason))
    # a pattern with a reason draws nothing
    dead = Scene("dead", Pat((0, 0, 0, 2), (2, 2), [fill(rect(0, 0, 2, 2), solid(1, 1, 1, 1))], map=ID), width=2, height=2)
    ck(all(px_value(dead, px, py) == ZERO4 for px in range(2) for py in range(2)), "a degenerate pattern draws nothing")
    ck(px_value(Scene("e", Pat((0, 0, 2, 2), (2, 2), [], map=ID), width=1, height=1), 0, 0) == ZERO4, "an empty tile draws nothing")

    # --- positions (PT5): the library's double formula against the exact cell
    for name, p, i, j in position_cases():
        exact = true_cell(p, i, j)
        got = sim_cell(p, i, j)
        for k, (g, e) in enumerate(zip(got, exact)):
            d = ulp_distance(g, float(e))
            ck(d <= 1, "%s (%d, %d) edge %d: %r is %d ulp from the exact %r" % (name, i, j, k, g, d, float(e)))
        if p.xs == p.w:
            ck(sim_cell(p, i, 0)[2] == sim_cell(p, i + 1, 0)[0], "%s: the right edge of cell %d is the left edge of cell %d" % (name, i, i + 1))
        if p.ys == p.h:
            ck(sim_cell(p, 0, j)[3] == sim_cell(p, 0, j + 1)[1], "%s: the bottom edge of row %d is the top edge of row %d" % (name, j, j + 1))
        # the oracle's own exact cell, plants aside, is the true one
        if not (PLANTS & {"add-step", "left-plus-w"}):
            ck(cell_edges(p, i, j) == exact, "cell_edges is the exact cell")
    # the exact right edge of cell i is the exact left edge of cell i + 1 whenever the step equals the size
    for name, p, i, j in position_cases():
        if p.xs == p.w:
            ck(true_cell(p, i, j)[2] == true_cell(p, i + 1, j)[0], "exact abutting edge")
    # a lattice at index 10^6 differs from the accumulated sum by far more than an ulp: the plant is seen
    p01 = Pat((0, 0, 0.1, 0.1), (0.1, 0.1), [])
    acc = accumulate(0.1, 1000000)
    ck(ulp_distance(acc, float(true_cell(p01, 1000000, 0)[0])) > 1000, "accumulating the step 10^6 times is thousands of ulps off")
    # the double formula's two roundings: left + w disagrees with the next left for each abutting width
    for w in (0.1, 0.3, 1.0 / 3.0):
        pw = Pat((0, 0, w, w), (w, w), [])
        ck(any(dbl_left(0, w, i) + w != dbl_left(0, w, i + 1) for i in range(1, 2000)), "left + w differs from the next left for w = %r" % w)

    # --- the geometry itself
    sq = [(F(0), F(0)), (F(2), F(0)), (F(2), F(2)), (F(0), F(2))]
    tri = [(F(1), F(-1)), (F(3), F(1)), (F(1), F(3))]
    ck(area(convex_clip(tri, sq)) == 2, "triangle clipped by a square: 2")
    ck(area(convex_clip(sq, ccw(tri))) == 2, "the same, the other way")
    ck(area(convex_clip(sq, sq)) == 4 and convex_clip(sq, [(F(5), F(5)), (F(6), F(5)), (F(6), F(6))]) == [], "square with itself, with a disjoint triangle")

    for pl in sorted(PLANTS):
        ck(True, "plant %s is active" % pl)
    print("pattern-oracle selftest: %d identities, %d failed%s" % (
        ck.count, len(ck.fails), (" (plants: %s)" % ", ".join(sorted(PLANTS))) if PLANTS else ""))
    for f in ck.fails[:20]:
        print("  FAIL:", f)
    return 1 if ck.fails else 0


def main(argv):
    args = list(argv)
    while "--plant" in args:
        k = args.index("--plant")
        name = args[k + 1] if k + 1 < len(args) else ""
        if name not in PLANT_NAMES:
            sys.exit("pattern-oracle: --plant takes one of: " + ", ".join(PLANT_NAMES))
        PLANTS.add(name)
        del args[k:k + 2]
    if args == ["--selftest"]:
        return run_selftest()
    sys.exit("usage: pattern-oracle.py --selftest [--plant NAME]")


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
