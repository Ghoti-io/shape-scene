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
"""The reference of the scene's compositing definition (N1 to N11, N14, B1 to B12 and T1 to T7 of documentation/design.md), in exact rationals.

This is a slow, independent statement of what a closed list *means*: the color
spaces and their conversion (N1 to N3), the Porter-Duff operators (N4), the one
primitive `apply` (N5), layers and groups (N6, N8), fills (N7), clips (N9), the
foreground (N10), the order of a sequence (N11), the palette (N14: an entry's color with
its alpha scaled; a missing entry is refused, so its fill draws nothing), the blend modes
(B1 to B12: W3C Compositing and Blending Level 1's mixing functions applied literally to
every real input, then source-over) and the general group of PDF's transparency model (T1
to T7: isolated or not, knockout or not, shape and opacity kept apart), of which N5 to N9 are
the isolated, non-knockout case.  It shares no code with the library or with the other
generators.  Arithmetic is `fractions.Fraction` throughout; the inexact steps are the power
function of a transfer and soft-light's square root of a value that is not the square of a
rational, each evaluated with `decimal` at 80 digits and turned back into the exact rational
of that decimal (an error below 1e-70), and a result that went through one is flagged
inexact.  Everything else is exact: a case whose values are dyadic rationals and whose spaces
are all the same has an exact answer (whether a double evaluator meets it bit for bit is the
generator's question, tools/gen-composite-vectors.py).

    python3 tools/composite-oracle.py --selftest

runs the oracle against itself: the Porter-Duff identities, the published matrices,
the transfer round trips, the primitive's limits, and the order of a sequence
against tools/gen-scene-vectors.py's own statement of it.  tools/gen-composite-vectors.py
imports this file for the answers it commits.

A case is evaluated by `evaluate(items, cov, fg, target, backdrop, palette)`: `items` are
the closed list's items as dictionaries (see the constructors below, the same
shape as gen-scene-vectors.py's), `cov` the coverage in [0, 1] of each fill and
each clip push (by item index; other entries ignored), `fg` the list's foreground,
`target` the space of the root layer (a concrete space) and `backdrop` the
premultiplied value the root layer starts with, and `palette` the list's palette (a list of
(straight rgba, space), or None for none).
"""
import sys
from decimal import Decimal, getcontext
from math import isqrt
from fractions import Fraction as F

getcontext().prec = 80

ZERO = F(0)
ONE = F(1)

# --- N1: the bound of a recorded component and the saturation ---------------------------------------------------------
COMPONENT_MAX = F(2) ** 16
SATURATION = F(2) ** 64


def sat(x):
    """N1: x clamped to [-2^64, 2^64], exactly."""
    return SATURATION if x > SATURATION else -SATURATION if x < -SATURATION else x

# --- N1: the gamuts ----------------------------------------------------------------------------------
WHITE_D65 = (F("0.3127"), F("0.3290"))
GAMUTS = {
    1: ((F("0.64"), F("0.33")), (F("0.30"), F("0.60")), (F("0.15"), F("0.06"))),                 # sRGB
    2: ((F("0.64"), F("0.33")), (F("0.21"), F("0.71")), (F("0.15"), F("0.06"))),                 # Adobe RGB
    3: ((F("0.680"), F("0.320")), (F("0.265"), F("0.690")), (F("0.150"), F("0.060"))),           # Display P3
    4: ((F("0.708"), F("0.292")), (F("0.170"), F("0.797")), (F("0.131"), F("0.046"))),           # BT.2020
}
LINEAR, SRGB, GAMMA = 1, 2, 3

# A space is (primaries, transfer, gamma); gamma is a double (so an exact rational) for GAMMA, else 0.
SPACE_SRGB = (1, SRGB, 0.0)
SPACE_LINEAR_SRGB = (1, LINEAR, 0.0)
SPACE_P3 = (3, SRGB, 0.0)
SPACE_LINEAR_P3 = (3, LINEAR, 0.0)
SPACE_P3_GAMMA = (3, GAMMA, 2.2)
SPACE_ADOBE_GAMMA = (2, GAMMA, 2.19921875)
SPACE_LINEAR_BT2020 = (4, LINEAR, 0.0)
TARGET = (0, 0, 0.0)


def is_target(space):
    return space == TARGET


# --- exact 3x3 linear algebra ------------------------------------------------------------------------
def det3(m):
    return (m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
            + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]))


def inverse3(m):
    d = det3(m)
    cof = [[None] * 3 for _ in range(3)]
    for i in range(3):
        for j in range(3):
            rows = [r for r in range(3) if r != i]
            cols = [c for c in range(3) if c != j]
            minor = m[rows[0]][cols[0]] * m[rows[1]][cols[1]] - m[rows[0]][cols[1]] * m[rows[1]][cols[0]]
            cof[i][j] = (-1) ** (i + j) * minor
    return [[cof[j][i] / d for j in range(3)] for i in range(3)]  # the adjugate over the determinant


def matvec(m, v):
    return [m[i][0] * v[0] + m[i][1] * v[1] + m[i][2] * v[2] for i in range(3)]


def rgb_to_xyz(primaries):
    """N3: the matrix from linear RGB to XYZ for a named gamut (white D65)."""
    cols = []
    for x, y in GAMUTS[primaries]:
        cols.append((x / y, ONE, (ONE - x - y) / y))
    wx, wy = WHITE_D65
    white = [wx / wy, ONE, (ONE - wx - wy) / wy]
    primaries_matrix = [[cols[c][r] for c in range(3)] for r in range(3)]
    s = matvec(inverse3(primaries_matrix), white)
    return [[s[c] * cols[c][r] for c in range(3)] for r in range(3)]


MATRIX = {p: rgb_to_xyz(p) for p in GAMUTS}
MATRIX_INVERSE = {p: inverse3(MATRIX[p]) for p in GAMUTS}


# --- N2: transfer functions ---------------------------------------------------------------------------
class Exactness:
    """Whether a power function was evaluated on the way to a value."""

    def __init__(self):
        self.inexact = False


def to_dec(f):
    return Decimal(f.numerator) / Decimal(f.denominator)


def power(base, exponent, ex):
    """The real power of a positive Fraction by a Fraction exponent, as a Fraction (exact for an integer exponent)."""
    if base == 0:
        return ZERO
    if base == 1:
        return ONE
    if exponent.denominator == 1 and abs(exponent.numerator) <= 64:
        return base ** exponent.numerator
    ex.inexact = True
    return F(to_dec(base) ** to_dec(exponent))


def sign(x):
    return -1 if x < 0 else 1


def decode(space, x, ex):
    transfer = space[1]
    if transfer == LINEAR:
        return x
    if transfer == SRGB:
        if x <= F("0.04045"):
            return x / F("12.92")
        return power((x + F("0.055")) / F("1.055"), F("2.4"), ex)
    g = F(space[2])
    return sign(x) * power(abs(x), g, ex)


def encode(space, y, ex):
    transfer = space[1]
    if transfer == LINEAR:
        return y
    if transfer == SRGB:
        if y <= F("0.0031308"):
            return y * F("12.92")
        return F("1.055") * power(y, ONE / F("2.4"), ex) - F("0.055")
    g = F(space[2])
    if y == 0:
        return ZERO
    # 1/g as the Fraction of an 80-digit decimal: g's reciprocal is not a double
    return sign(y) * F(to_dec(abs(y)) ** (Decimal(1) / to_dec(g))) if not (g == 1) else y


# --- N3: conversion -----------------------------------------------------------------------------------
def convert(rgb, a, b, ex):
    """Straight (r, g, b) from space a to space b, both concrete."""
    if a == b:
        return list(rgb)
    # N1: each step saturated, so that no power is taken of a value past 2^64.
    lin = [sat(decode(a, v, ex)) for v in rgb]
    if a[0] != b[0]:
        lin = [sat(v) for v in matvec(MATRIX_INVERSE[b[0]], matvec(MATRIX[a[0]], lin))]
    return [sat(encode(b, v, ex)) for v in lin]


def convert_premultiplied(px, a, b, ex):
    """Premultiplied (R, G, B, A) from space a to space b."""
    if a == b:
        return list(px)
    alpha = px[3]
    if alpha == 0:
        return [ZERO, ZERO, ZERO, ZERO]
    straight = [sat(px[i] / alpha) for i in range(3)]
    out = convert(straight, a, b, ex)
    return [out[0] * alpha, out[1] * alpha, out[2] * alpha, alpha]


# --- N4, N5: operators and the primitive ----------------------------------------------------------------
OPS = ["src-over", "clear", "src", "dest", "dest-over", "src-in", "dest-in", "src-out", "dest-out", "src-atop",
       "dest-atop", "xor", "plus"]
BLEND_INDEX = {"screen", "overlay", "darken", "lighten", "color-dodge", "color-burn", "hard-light", "soft-light",
               "difference", "exclusion", "multiply", "hue", "saturation", "color", "luminosity"}
OP_INDEX = {name: i for i, name in enumerate(OPS)}


def factors(op, sa, da):
    if op == "clear":
        return ZERO, ZERO
    if op == "src":
        return ONE, ZERO
    if op == "dest":
        return ZERO, ONE
    if op == "src-over":
        return ONE, ONE - sa
    if op == "dest-over":
        return ONE - da, ONE
    if op == "src-in":
        return da, ZERO
    if op == "dest-in":
        return ZERO, sa
    if op == "src-out":
        return ONE - da, ZERO
    if op == "dest-out":
        return ZERO, ONE - sa
    if op == "src-atop":
        return da, ONE - sa
    if op == "dest-atop":
        return ONE - da, sa
    if op == "xor":
        return ONE - da, ONE - sa
    raise ValueError(op)


def operator(op, s, d, ex=None):
    """op(S, D) on premultiplied (R, G, B, A) values."""
    if op in BLEND_INDEX:
        return blend_operator(op, s, d, ex if ex is not None else Exactness())
    if op == "plus":
        return [min(ONE, s[i] + d[i]) for i in range(4)]
    fa, fb = factors(op, s[3], d[3])
    return [fa * s[i] + fb * d[i] for i in range(4)]


# --- B1 to B12: the blend modes (W3C Compositing and Blending Level 1, literally, on every real input) ------------------
# B1: the values 13 to 27 are COLRv1's CompositeMode numbers; the eleven separable modes, then the four non-separable.
BLEND_MODES = ["screen", "overlay", "darken", "lighten", "color-dodge", "color-burn", "hard-light", "soft-light",
               "difference", "exclusion", "multiply", "hue", "saturation", "color", "luminosity"]
OPS = OPS + BLEND_MODES
OP_INDEX = {name: i for i, name in enumerate(OPS)}
SEPARABLE = BLEND_MODES[:11]
NON_SEPARABLE = BLEND_MODES[11:]
HALF = F(1, 2)
QUARTER = F(1, 4)
# B8 (P3-4): Lum's coefficients, W3C's 0.3, 0.59 and 0.11 as the exact rationals; they sum to 1.
LUM = (F(3, 10), F(59, 100), F(11, 100))
# B9: the cap of dodge's and burn's quotients, which an implementation uses so as never to form them past the bound.
QUOTIENT_CAP = F(2) ** 66


def is_square(x):
    """Whether a non-negative Fraction is the square of a rational (so its root is exact)."""
    return isqrt(x.numerator) ** 2 == x.numerator and isqrt(x.denominator) ** 2 == x.denominator


def root(x, ex):
    """The square root of a positive Fraction: exact when x is the square of a rational, else at 80 digits (inexact)."""
    if is_square(x):
        return F(isqrt(x.numerator), isqrt(x.denominator))
    ex.inexact = True
    return F(to_dec(x).sqrt())


def mix_multiply(b, s):
    return b * s


def mix_screen(b, s):
    return b + s - b * s


def mix_hard_light(b, s):
    return mix_multiply(b, 2 * s) if s <= HALF else mix_screen(b, 2 * s - 1)


def mix_color_dodge(b, s):
    if b == 0:
        return ZERO
    if s == 1:
        return ONE
    return min(ONE, b / (ONE - s))  # B10: a negative quotient is kept


def mix_color_burn(b, s):
    if b == 1:
        return ONE
    if s == 0:
        return ZERO
    return ONE - min(ONE, (ONE - b) / s)  # B10: above 1 when the quotient is negative


def mix_soft_light(b, s, ex):
    if s <= HALF:
        return b - (1 - 2 * s) * b * (1 - b)
    if b <= QUARTER:
        dee = ((16 * b - 12) * b + 4) * b  # B10: every b <= 1/4, negatives included
    else:
        dee = root(b, ex)  # b > 1/4: the root's argument is always positive
    return b + (2 * s - 1) * (dee - b)


SEPARABLE_MIX = {
    "multiply": lambda b, s, ex: mix_multiply(b, s),
    "screen": lambda b, s, ex: mix_screen(b, s),
    "overlay": lambda b, s, ex: mix_hard_light(s, b),  # hard-light with backdrop and source exchanged
    "darken": lambda b, s, ex: min(b, s),
    "lighten": lambda b, s, ex: max(b, s),
    "color-dodge": lambda b, s, ex: mix_color_dodge(b, s),
    "color-burn": lambda b, s, ex: mix_color_burn(b, s),
    "hard-light": lambda b, s, ex: mix_hard_light(b, s),
    "soft-light": mix_soft_light,
    "difference": lambda b, s, ex: abs(b - s),
    "exclusion": lambda b, s, ex: b + s - 2 * b * s,
}


def lum(c):
    return LUM[0] * c[0] + LUM[1] * c[1] + LUM[2] * c[2]


def clip_color(c):
    """B8, P3-5, P3-11: L, n and x taken once, on entry; the 0/0 form (every component equal, so n = L = x) leaves C
    alone.  n = L, x = L and n = x are one condition in the reals (Lum is a convex combination with positive weights);
    B10 states it as n = x, which a double evaluation decides exactly."""
    el = lum(c)
    n = min(c)
    x = max(c)
    out = list(c)
    if n < 0 and n != x:
        out = [el + (v - el) * el / (el - n) for v in out]
    if x > 1 and n != x:
        out = [el + (v - el) * (1 - el) / (x - el) for v in out]
    return out


def set_lum(c, el):
    d = el - lum(c)
    return clip_color([v + d for v in c])


def saturation(c):
    return max(c) - min(c)


def set_sat(c, s, order=None):
    """B8: the components holding the largest, middle and smallest values on entry; @p order picks among ties."""
    imin, imid, imax = order if order is not None else sorted(range(3), key=lambda i: c[i])
    out = [ZERO, ZERO, ZERO]
    if c[imax] > c[imin]:
        out[imid] = (c[imid] - c[imin]) * s / (c[imax] - c[imin])
        out[imax] = s
    return out


def mix(mode, cb, cs, ex):
    """B7, B8: the mixing function B(Cb, Cs) on straight triples, before B9's saturation."""
    if mode in SEPARABLE_MIX:
        return [SEPARABLE_MIX[mode](cb[i], cs[i], ex) for i in range(3)]
    if mode == "hue":
        return set_lum(set_sat(cs, saturation(cb)), lum(cb))
    if mode == "saturation":
        return set_lum(set_sat(cb, saturation(cs)), lum(cb))
    if mode == "color":
        return set_lum(cs, lum(cb))
    if mode == "luminosity":
        return set_lum(cb, lum(cs))
    raise ValueError(mode)


def capped_mix(mode, b, s):
    """B9 as an implementation evaluates dodge or burn: the quotient taken as sign * 2^66 once |n| >= 2^66 |d|."""
    def capped(n, d):
        if abs(n) >= QUOTIENT_CAP * abs(d):
            return QUOTIENT_CAP if (n < 0) == (d < 0) else -QUOTIENT_CAP
        return n / d
    if mode == "color-dodge":
        if b == 0:
            return ZERO
        if s == 1:
            return ONE
        return min(ONE, capped(b, ONE - s))
    if b == 1:
        return ONE
    if s == 0:
        return ZERO
    return ONE - min(ONE, capped(ONE - b, s))


def straight(px):
    """N3's un-premultiply with N1's saturation; an alpha of 0 gives the color 0."""
    a = px[3]
    return [sat(px[i] / a) if a != 0 else ZERO for i in range(3)]


BLEND_OBSERVER = None  # gen-composite-vectors.py's exactness tracer: called as (mode, s, d, m) at every blend's apply


def blend_operator(mode, s, d, ex):
    """B2: op_X(S, D) = (cs (1 - ab) + cb (1 - as) + as ab sat(B(Cb, Cs)), as + ab (1 - as))."""
    sa, da = s[3], d[3]
    if sa * da == 0:
        b = [ZERO, ZERO, ZERO]  # the third term is 0, and B is not evaluated
    else:
        b = [sat(v) for v in mix(mode, straight(d), straight(s), ex)]  # B9
    color = [s[i] * (ONE - da) + d[i] * (ONE - sa) + sa * da * b[i] for i in range(3)]
    return color + [sa + da * (ONE - sa)]


def apply(d, s, m, op, ex=None):
    """N5: (1 - m) D + m op(S, D)."""
    if BLEND_OBSERVER is not None and op in BLEND_INDEX:
        BLEND_OBSERVER(op, s, d, m)
    o = operator(op, s, d, ex if ex is not None else Exactness())
    return [sat((ONE - m) * d[i] + m * o[i]) for i in range(4)]


def is_unbounded(op):
    zero = [ZERO] * 4
    probe = [F(1, 4), F(1, 2), F(3, 4), F(1, 2)]
    return operator(op, zero, probe) != probe


# --- N6 to N10: evaluating a closed list ---------------------------------------------------------------------
FILL, CLIP_PUSH, CLIP_POP, GROUP_PUSH, GROUP_POP, INSTANCE = 1, 2, 3, 4, 5, 6
PAINT_SOLID, PAINT_FOREGROUND, PAINT_PALETTE = 0, 2, 3


class Frame:
    def __init__(self, layer, space, group=None, outer=ONE):
        self.layer = layer
        self.space = space
        self.clips = []
        self.group = group
        self.outer = outer  # the product of the clips in force in the enclosing group when this group was pushed


def product(values):
    p = ONE
    for v in values:
        p *= v
    return p


def premultiply(rgb, a):
    return [rgb[0] * a, rgb[1] * a, rgb[2] * a, a]


def evaluate(items, cov, fg, target, backdrop, palette=None):
    """Return (the root layer's value as four Fractions, inexact) after the closed list's items."""
    ex = Exactness()
    cov = [F(c) for c in cov]
    root = Frame([F(v) for v in backdrop], target)
    stack = [root]
    for i, it in enumerate(items):
        top = stack[-1]
        k = it["k"]
        if k == FILL:
            if it["paint"] == PAINT_FOREGROUND:
                rgb = [F(v) for v in fg[0][:3]]
                alpha = F(fg[0][3]) * F(it["rgba"][3])
                space = target if is_target(fg[1]) else fg[1]
            elif it["paint"] == PAINT_PALETTE:
                if palette is None or it["entry"] >= len(palette):
                    continue  # N14: a missing entry is refused; the fill draws nothing
                entry = palette[it["entry"]]
                rgb = [F(v) for v in entry[0][:3]]
                alpha = F(entry[0][3]) * F(it["rgba"][3])
                space = target if is_target(entry[1]) else entry[1]
            else:
                rgb = [F(v) for v in it["rgba"][:3]]
                alpha = F(it["rgba"][3])
                space = target if is_target(it["space"]) else it["space"]
            source = premultiply(convert(rgb, space, top.space, ex), alpha)
            m = cov[i] * product(top.clips)
            top.layer = apply(top.layer, source, m, "src-over", ex)
        elif k == CLIP_PUSH:
            top.clips.append(cov[i])
        elif k == CLIP_POP:
            top.clips.pop()
        elif k == GROUP_PUSH:
            space = top.space if is_target(it["blend"]) else it["blend"]
            stack.append(Frame([ZERO] * 4, space, group=it, outer=product(top.clips)))
        elif k == GROUP_POP:
            done = stack.pop()
            parent = stack[-1]
            moved = convert_premultiplied(done.layer, done.space, parent.space, ex)
            opacity = F(done.group["opacity"])
            source = [opacity * v for v in moved]
            op = OPS[done.group["composite"]]
            parent.layer = apply(parent.layer, source, done.outer, op, ex)
        else:
            raise ValueError("an instance is not evaluated here: a picture is expanded by the caller")
    assert len(stack) == 1
    return root.layer, ex.inexact


# --- T1 to T7: PDF's transparency groups (ISO 32000-1 section 11), of which N5 to N9 are one case ----------------------
ISOLATED, NON_ISOLATED = 0, 1
# T5: the operators PDF has (normal is source-over, B12), whose group element takes the group's shape.
PDF_OPS = {"src-over"} | BLEND_INDEX


class GFrame:
    """A layer of the general model: its value L, its initial backdrop L0, the group alpha and the group shape."""

    def __init__(self, layer, space, group=None, outer=ONE, knockout=False):
        self.layer = list(layer)
        self.initial = list(layer)
        self.alpha_g = ZERO
        self.shape_g = ZERO
        self.space = space
        self.clips = []
        self.group = group
        self.outer = outer
        self.knockout = knockout


def element(frame, f, op, a, ex):
    """T3: one element of shape f into frame's layer.

    For PDF's operators @p a is the element's source with its shape and its opacity both applied (A = f S, alpha
    f q); for every other operator it is S, its opacity alone.  The element's backdrop is the layer's value before it
    (non-knockout) or the layer's initial backdrop (knockout); the group alpha's backdrop is likewise the group alpha
    before it, or 0.
    """
    bk = frame.initial if frame.knockout else frame.layer
    agb = ZERO if frame.knockout else frame.alpha_g
    old = frame.layer
    if op in PDF_OPS:
        qa = a[3]
        if op == "src-over":
            term = a[:3]
        else:
            if qa * bk[3] == 0:
                b = [ZERO, ZERO, ZERO]
            else:
                b = [sat(v) for v in mix(op, straight(bk), straight(a), ex)]
            term = [a[i] * (ONE - bk[3]) + qa * bk[3] * b[i] for i in range(3)]
        frame.layer = [sat((ONE - f) * old[i] + (f - qa) * bk[i] + term[i]) for i in range(3)] + [
            sat((ONE - f) * old[3] + (f - qa) * bk[3] + qa)]
        frame.alpha_g = (ONE - f) * frame.alpha_g + (f - qa) * agb + qa
    else:
        o = operator(op, a, bk, ex)
        frame.layer = [sat((ONE - f) * old[i] + f * o[i]) for i in range(4)]
        frame.alpha_g = (ONE - f) * frame.alpha_g + f * operator(op, a, [ZERO, ZERO, ZERO, agb], ex)[3]
    frame.shape_g = frame.shape_g + f - frame.shape_g * f


def evaluate_general(items, cov, fg, target, backdrop, palette=None):
    """T1 to T7: like evaluate(), with each group push's `isolation` (0 isolated, 1 not) and `knockout` (0 or 1)."""
    ex = Exactness()
    cov = [F(c) for c in cov]
    root = GFrame([F(v) for v in backdrop], target)
    stack = [root]
    for i, it in enumerate(items):
        top = stack[-1]
        k = it["k"]
        if k == FILL:
            # N7, N10, N14 as evaluate() resolves them (the generator requires the two to agree on every default case).
            kind = it["paint"]
            straight_color = (fg[0] if kind == PAINT_FOREGROUND else
                              None if kind == PAINT_PALETTE and (palette is None or it["entry"] >= len(palette)) else
                              palette[it["entry"]][0] if kind == PAINT_PALETTE else it["rgba"])
            if straight_color is None:
                continue
            given = fg[1] if kind == PAINT_FOREGROUND else palette[it["entry"]][1] if kind == PAINT_PALETTE else it["space"]
            scale = F(straight_color[3]) * (F(it["rgba"][3]) if kind in (PAINT_FOREGROUND, PAINT_PALETTE) else ONE)
            moved = convert([F(v) for v in straight_color[:3]], target if is_target(given) else given, top.space, ex)
            opaque = premultiply(moved, scale)          # T2: the opacity, the paint's alpha
            f = cov[i] * product(top.clips)             # T2: the shape, coverage and the clips in force
            element(top, f, "src-over", [f * v for v in opaque], ex)
        elif k == CLIP_PUSH:
            top.clips += [cov[i]]
        elif k == CLIP_POP:
            del top.clips[-1]
        elif k == GROUP_PUSH:
            space = it["blend"] if not is_target(it["blend"]) else top.space
            frame = GFrame([ZERO] * 4, space, group=it, outer=product(top.clips), knockout=bool(it.get("knockout", 0)))
            if it.get("isolation", ISOLATED) == NON_ISOLATED:
                # T4: the initial backdrop is the backdrop the group sees as an element of its parent, in its space.
                seen = top.initial if top.knockout else top.layer
                frame.layer = convert_premultiplied(seen, top.space, space, ex)
                frame.initial = list(frame.layer)
            stack.append(frame)
        elif k == GROUP_POP:
            done = stack.pop()
            parent = stack[-1]
            ag = done.alpha_g
            # T5: the group's own result, the initial backdrop taken out (0 for an isolated group).
            own = [sat(done.layer[i] - (ONE - ag) * done.initial[i]) for i in range(3)] + [ag]
            moved = convert_premultiplied(own, done.space, parent.space, ex)
            opacity = F(done.group["opacity"])
            op = OPS[int(done.group["composite"])]
            if op in PDF_OPS:
                f = done.outer * done.shape_g
                element(parent, f, op, [done.outer * opacity * v for v in moved], ex)
            else:
                element(parent, done.outer, op, [opacity * v for v in moved], ex)
        else:
            raise ValueError("an instance is not evaluated here: a picture is expanded by the caller")
    assert len(stack) == 1
    return root.layer, ex.inexact


def pdf_literal(elements, isolated, knockout, backdrop):
    """ISO 32000-1 section 11.4.8's group formulas, in straight color as PDF writes them, for one group of elements
    (each (shape, opacity, straight rgb, blend mode)) over a straight (rgb, alpha) backdrop.  Independent of
    element(): the self-test compares the two.  Returns the group's straight color, alpha and shape, and the backdrop
    composited with it by the group's normal blend (when the group alpha is not 0)."""
    def union(b, s):
        return s + b * (1 - s)
    cb0, ab0 = backdrop
    c0, a0 = (list(cb0), ab0) if not isolated else ([ZERO] * 3, ZERO)
    c, al = list(c0), a0
    fg, ag = ZERO, ZERO
    for fs, qs, cs, mode in elements:
        cbi, abi = (c0, a0) if knockout else (c, al)
        agbi = ZERO if knockout else ag
        als = fs * qs
        fg = union(fg, fs)
        ag = (ONE - fs) * ag + (fs - als) * agbi + als
        new_al = (ONE - fs) * al + (fs - als) * abi + als
        b = mix(mode, cbi, cs, Exactness()) if mode != "src-over" else list(cs)
        b = [sat(v) for v in b]
        if new_al == 0:
            c = [ZERO] * 3
        else:
            c = [((ONE - fs) * al * c[i] + (fs - als) * abi * cbi[i] + als * ((ONE - abi) * cs[i] + abi * b[i])) / new_al
                 for i in range(3)]
        al = new_al
    if not isolated and ag != 0:
        c = [c[i] + (c[i] - c0[i]) * (a0 / ag - a0) for i in range(3)]
    return c, ag, fg


# --- N11: the order of a sequence ------------------------------------------------------------------------------
ORDER_FLAT, ORDER_DEPTH, ORDER_OVERLAY = 0, 1, 2


def units(items, lo, hi):
    out = []
    i = lo
    while i < hi:
        end = i
        if items[i]["k"] in (CLIP_PUSH, GROUP_PUSH):
            nest = 0
            j = i
            while True:
                if items[j]["k"] in (CLIP_PUSH, GROUP_PUSH):
                    nest += 1
                elif items[j]["k"] in (CLIP_POP, GROUP_POP):
                    nest -= 1
                if nest == 0:
                    break
                j += 1
            end = j
        out.append((i, end))
        i = end + 1
    return out


def paint_order(items):
    """The indices of a declared-order list in paint order, and the ties (first, second, depth, reason)."""
    order = []
    ties = []

    def sequence(lo, hi, context):
        seq = units(items, lo, hi)
        indexed = list(enumerate(seq))
        if context == ORDER_DEPTH:
            indexed.sort(key=lambda e: (-items[e[1][0]]["depth"], items[e[1][0]]["layer"], e[0]))
        elif context == ORDER_OVERLAY:
            indexed.sort(key=lambda e: (items[e[1][0]]["layer"], e[0]))
        previous = None
        for _, (start, end) in indexed:
            here = len(order)
            if context == ORDER_DEPTH and previous is not None:
                before = items[previous[0]]
                after = items[start]
                if before["depth"] == after["depth"]:
                    reason = 1 if before["layer"] != after["layer"] else 3
                    ties.append((previous[1], here, after["depth"], reason))
            previous = (start, here)
            order.append(start)
            if end != start:
                inner = items[start]["order"] if items[start]["k"] == GROUP_PUSH else context
                sequence(start + 1, end, inner)
                order.append(end)

    sequence(0, len(items), ORDER_FLAT)
    ties.sort(key=lambda t: t[1])
    return order, ties


# --- the oracle against itself ---------------------------------------------------------------------------------
def selftest():
    import importlib.util
    import itertools
    import os

    failures = []

    def check(ok, what):
        if not ok:
            failures.append(what)

    # N4: the Porter-Duff identities, on dyadic values.
    values = [F(0), F(1, 4), F(1, 2), F(3, 4), F(1)]
    pixels = []
    for a in values:
        for c in (F(0), F(1, 2), F(1)):
            pixels.append([c * a, F(1, 4) * a, a * F(3, 4), a])
    for s, d in itertools.product(pixels, pixels):
        check(operator("clear", s, d) == [ZERO] * 4, "clear")
        check(operator("src", s, d) == s, "src")
        check(operator("dest", s, d) == d, "dest")
        for a, b in (("src-over", "dest-over"), ("src-in", "dest-in"), ("src-out", "dest-out"),
                     ("src-atop", "dest-atop")):
            check(operator(a, s, d) == operator(b, d, s), "%s mirrors %s" % (a, b))
        check(operator("xor", s, d) == operator("xor", d, s), "xor is symmetric")
        check(operator("plus", s, d) == operator("plus", d, s), "plus is symmetric")
        # over: the result alpha is As + Ad (1 - As); the union
        check(operator("src-over", s, d)[3] == s[3] + d[3] * (ONE - s[3]), "over alpha")
        # In and out partition the source: SRC_IN + SRC_OUT = SRC; likewise atop + xor relations
        a_in, a_out = operator("src-in", s, d), operator("src-out", s, d)
        check([a_in[i] + a_out[i] for i in range(4)] == s, "in + out is the source")
        # atop = in + dest-out; xor = src-out + dest-out
        check(operator("src-atop", s, d) == [operator("src-in", s, d)[i] + operator("dest-out", s, d)[i] for i in range(4)],
              "atop is in plus dest-out")
        check(operator("xor", s, d) == [operator("src-out", s, d)[i] + operator("dest-out", s, d)[i] for i in range(4)],
              "xor is out plus dest-out")
        if s[3] == 1:
            check(operator("src-over", s, d) == s, "an opaque source covers")
    for op in OPS:
        expected_unbounded = op in ("clear", "src", "src-in", "dest-in", "src-out", "dest-atop")
        check(is_unbounded(op) == expected_unbounded, "%s unbounded: %s" % (op, expected_unbounded))
    # N5: the limits of the primitive.
    for s, d in itertools.product(pixels[:6], pixels[:6]):
        for op in OPS:
            check(apply(d, s, ZERO, op) == d, "no coverage changes nothing")
            check(apply(d, s, ONE, op) == operator(op, s, d), "full coverage is the operator")
            half = apply(d, s, F(1, 2), op)
            check(half == [(d[i] + operator(op, s, d)[i]) / 2 for i in range(4)], "half coverage is the mean")

    # N3: the matrices against published values (to the four places they are published to).
    published = {
        1: [[0.4124, 0.3576, 0.1805], [0.2126, 0.7152, 0.0722], [0.0193, 0.1192, 0.9505]],
        2: [[0.5767, 0.1856, 0.1882], [0.2974, 0.6273, 0.0753], [0.0270, 0.0707, 0.9911]],
        3: [[0.4866, 0.2657, 0.1982], [0.2290, 0.6917, 0.0793], [0.0000, 0.0451, 1.0439]],
        4: [[0.6370, 0.1446, 0.1689], [0.2627, 0.6780, 0.0593], [0.0000, 0.0281, 1.0610]],
    }
    for p, rows in published.items():
        for r in range(3):
            for c in range(3):
                check(abs(float(MATRIX[p][r][c]) - rows[r][c]) < 6e-4, "matrix %d[%d][%d] = %s" % (p, r, c, float(MATRIX[p][r][c])))
        # the white point maps to (1, 1, 1) exactly
        w = matvec(MATRIX_INVERSE[p], [WHITE_D65[0] / WHITE_D65[1], ONE, (ONE - WHITE_D65[0] - WHITE_D65[1]) / WHITE_D65[1]])
        check(w == [ONE, ONE, ONE], "the white point of %d is (1, 1, 1) exactly" % p)
        ident = [[sum(MATRIX_INVERSE[p][r][k] * MATRIX[p][k][c] for k in range(3)) for c in range(3)] for r in range(3)]
        check(ident == [[ONE if r == c else ZERO for c in range(3)] for r in range(3)], "the inverse of %d is exact" % p)

    # N2: the transfer functions: known points and round trips.
    ex = Exactness()
    check(decode(SPACE_SRGB, F(1), ex) == ONE, "sRGB decode(1) = 1")
    check(abs(float(decode(SPACE_SRGB, F(1, 2), ex)) - 0.21404114048223255) < 1e-15, "sRGB decode(0.5)")
    check(abs(float(encode(SPACE_SRGB, F(1, 2), ex)) - 0.7353569830524495) < 1e-15, "sRGB encode(0.5)")
    check(decode(SPACE_SRGB, F(-1, 100), ex) == F(-1, 100) / F("12.92"), "the sRGB linear segment continues below zero")
    check(abs(float(decode(SPACE_P3_GAMMA, F(1, 2), ex)) - 0.5 ** 2.2) < 1e-15, "gamma decode")
    check(decode(SPACE_P3_GAMMA, F(-1, 2), ex) == -decode(SPACE_P3_GAMMA, F(1, 2), ex), "gamma is odd")
    check(decode(SPACE_P3_GAMMA, ZERO, ex) == 0 and encode(SPACE_P3_GAMMA, ZERO, ex) == 0, "gamma of zero")
    for space in (SPACE_SRGB, SPACE_P3_GAMMA, SPACE_ADOBE_GAMMA):
        for n in range(-8, 24):
            x = F(n, 16)
            back = encode(space, decode(space, x, ex), ex)
            check(abs(back - x) < F(1, 10 ** 60), "round trip %s at %s" % (space, x))
    # Conversion: a round trip through another space, and the identity when the spaces are equal.
    for a, b in ((SPACE_SRGB, SPACE_P3), (SPACE_LINEAR_SRGB, SPACE_ADOBE_GAMMA), (SPACE_P3_GAMMA, SPACE_LINEAR_BT2020)):
        rgb = [F(1, 4), F(1, 2), F(3, 4)]
        there = convert(rgb, a, b, ex)
        back = convert(there, b, a, ex)
        check(all(abs(back[i] - rgb[i]) < F(1, 10 ** 55) for i in range(3)), "conversion round trip %s %s" % (a, b))
        check(convert(rgb, a, a, ex) == rgb, "conversion to the same space is the identity")
    # the white of one gamut is the white of another (they share D65)
    white = convert([ONE, ONE, ONE], SPACE_LINEAR_SRGB, SPACE_LINEAR_P3, ex)
    check(all(abs(v - 1) < F(1, 10 ** 60) for v in white), "white is white in P3")

    # N1: the saturation is a clamp at 2^64, exact inside; a conversion saturates after each of its steps.
    check(sat(SATURATION) == SATURATION and sat(-SATURATION) == -SATURATION, "sat is inclusive")
    check(sat(SATURATION + 1) == SATURATION and sat(-SATURATION - 1) == -SATURATION, "sat clamps past the bound")
    check(sat(F(1, 3)) == F(1, 3), "sat leaves a value inside alone")
    g10, g01 = (1, GAMMA, 10.0), (1, GAMMA, 0.1)
    out = convert([COMPONENT_MAX, -COMPONENT_MAX, ZERO], g10, (1, GAMMA, 2.0), ex)
    check(out[:2] == [F(2) ** 32, -F(2) ** 32], "the decode saturates before the encode: %s" % out[:2])
    out = convert([COMPONENT_MAX, -COMPONENT_MAX, ZERO], g10, g01, ex)
    check(out[:2] == [SATURATION, -SATURATION], "gamma 10 to gamma 0.1 at the bound is the saturation")
    half = convert_premultiplied([-SATURATION, ZERO, ZERO, F(1, 2)], g01, SPACE_LINEAR_SRGB, ex)[0]
    check(half == convert([-SATURATION, ZERO, ZERO], g01, SPACE_LINEAR_SRGB, ex)[0] / 2,
          "an un-premultiply past the bound is saturated before it is converted")
    check(apply([-SATURATION, ZERO, ZERO, ONE], [-SATURATION, ZERO, ZERO, ONE], ONE, "plus")[0] == -SATURATION,
          "plus past the bound saturates")

    # N6 to N10 with a hand-worked case: a half-covering opaque red over opaque blue, then a group.
    red = dict(k=FILL, paint=PAINT_SOLID, rgba=(1.0, 0.0, 0.0, 1.0), space=TARGET)
    got, inexact = evaluate([red], [0.5], (None, TARGET), SPACE_SRGB, (0.0, 0.0, 1.0, 1.0))
    check(got == [F(1, 2), ZERO, F(1, 2), ONE] and not inexact, "half red over blue: %s" % got)
    # A foreground fill: the foreground's color with its alpha times the paint's.
    fgfill = dict(k=FILL, paint=PAINT_FOREGROUND, rgba=(0.0, 0.0, 0.0, 0.5), space=TARGET)
    got, _ = evaluate([fgfill], [1], ((0.5, 0.25, 0.0, 0.5), TARGET), SPACE_SRGB, (0.0, 0.0, 0.0, 0.0))
    check(got == [F(1, 8), F(1, 16), ZERO, F(1, 4)], "foreground: %s" % got)
    # A palette fill (N14): the entry's color with its alpha times the paint's; a missing entry draws nothing.
    pal = [((1.0, 0.0, 0.5, 1.0), TARGET), ((0.5, 0.25, 0.0, 0.5), TARGET)]
    palfill = dict(k=FILL, paint=PAINT_PALETTE, entry=1, rgba=(0.0, 0.0, 0.0, 0.5), space=TARGET)
    got, _ = evaluate([palfill], [1], (None, TARGET), SPACE_SRGB, (0.0, 0.0, 0.0, 0.0), pal)
    check(got == [F(1, 8), F(1, 16), ZERO, F(1, 4)], "palette: %s" % got)
    for missing, table in ((2, pal), (0, None), (0, [])):
        gone = dict(palfill, entry=missing)
        got, _ = evaluate([gone], [1], (None, TARGET), SPACE_SRGB, (0.0, 0.0, 1.0, 1.0), table)
        check(got == [ZERO, ZERO, ONE, ONE], "a missing entry draws nothing: %s" % got)
    # A group with opacity 1/2 and an unbounded operator over a covered backdrop: SRC replaces it.
    push = dict(k=GROUP_PUSH, opacity=0.5, blend=TARGET, composite=OP_INDEX["src"])
    pop = dict(k=GROUP_POP)
    got, _ = evaluate([push, red, pop], [0, 1, 0], (None, TARGET), SPACE_SRGB, (0.0, 0.0, 1.0, 1.0))
    check(got == [F(1, 2), ZERO, ZERO, F(1, 2)], "src replaces the backdrop: %s" % got)
    # An unbounded operator with an empty group clears the backdrop where it is not clipped.
    clear = dict(k=GROUP_PUSH, opacity=1.0, blend=TARGET, composite=OP_INDEX["clear"])
    got, _ = evaluate([clear, pop], [0, 0], (None, TARGET), SPACE_SRGB, (0.0, 0.0, 1.0, 1.0))
    check(got == [ZERO] * 4, "clear clears")
    # A clip around a group applies once, at the composite; around fills it weighs each fill.
    clip = dict(k=CLIP_PUSH)
    unclip = dict(k=CLIP_POP)
    plain = dict(k=GROUP_PUSH, opacity=1.0, blend=TARGET, composite=0)
    over_fills = evaluate([clip, red, dict(red, rgba=(0.0, 1.0, 0.0, 1.0)), unclip], [0.5, 1, 1, 0], (None, TARGET), SPACE_SRGB,
                          (0.0, 0.0, 0.0, 0.0))[0]
    over_group = evaluate([clip, plain, red, dict(red, rgba=(0.0, 1.0, 0.0, 1.0)), pop, unclip], [0.5, 0, 1, 1, 0, 0], (None, TARGET),
                          SPACE_SRGB, (0.0, 0.0, 0.0, 0.0))[0]
    check(over_group == [ZERO, F(1, 2), ZERO, F(1, 2)], "a clipped group is weighed once: %s" % over_group)
    check(over_fills == [F(1, 2) * F(1, 2), F(1, 2), ZERO, F(3, 4)], "a clip around fills weighs each: %s" % over_fills)
    check(over_fills != over_group, "the two clips differ at a partial pixel")
    # A clip pushed outside a group is not taken into it: nothing inside is weighed by it.
    inner = evaluate([clip, plain, red, pop, unclip], [0.5, 0, 1, 0, 0], (None, TARGET), SPACE_SRGB, (0.0, 0.0, 0.0, 0.0))[0]
    check(inner == [F(1, 2), ZERO, ZERO, F(1, 2)], "a clip outside a group multiplies once: %s" % inner)
    # A blend space: sRGB content in a linear group composited back is converted at the pop.
    lin = dict(k=GROUP_PUSH, opacity=1.0, blend=SPACE_LINEAR_SRGB, composite=0)
    got, inexact = evaluate([lin, red, pop], [0, 1, 0], (None, TARGET), SPACE_SRGB, (0.0, 0.0, 0.0, 0.0))
    check(got == [ONE, ZERO, ZERO, ONE] and not inexact, "pure red survives a linear group exactly: %s" % got)
    mid = dict(k=FILL, paint=PAINT_SOLID, rgba=(0.5, 0.5, 0.5, 1.0), space=TARGET)
    got, inexact = evaluate([lin, mid, pop], [0, 1, 0], (None, TARGET), SPACE_SRGB, (0.0, 0.0, 0.0, 0.0))
    check(inexact and all(abs(got[i] - F(1, 2)) < F(1, 10 ** 60) for i in range(3)), "a mid gray survives a linear group (inexact)")

    # B1: the modes are COLRv1's numbers 13 to 27.
    check(OP_INDEX["screen"] == 13 and OP_INDEX["multiply"] == 23 and OP_INDEX["hue"] == 24 and
          OP_INDEX["luminosity"] == 27 and len(OPS) == 28, "B1: COLRv1's CompositeMode numbers")
    # B7, B8, B10: the identities W3C's definitions imply, on in-range, out-of-range, threshold and pole inputs.
    import random
    rng = random.Random(1203)
    reals = [F(0), F(1), F(1, 2), F(1, 4), F(3, 4), F(-1, 3), F(5, 2), F(-7, 4), F(2) ** 16, -F(2) ** 16,
             F(1) + F(1, 2 ** 48), F(1) - F(1, 2 ** 48), F(1, 2 ** 48), -F(1, 2 ** 48), F(1, 2) + F(1, 2 ** 40)]
    reals += [F(rng.randint(-3000, 3000), rng.randint(1, 1000)) for _ in range(40)]
    ex = Exactness()
    for b, s_ in itertools.product(reals, reals):
        sm = SEPARABLE_MIX
        check(sm["screen"](b, s_, ex) == 1 - sm["multiply"](1 - b, 1 - s_, ex), "screen is multiply's dual")
        check(sm["color-burn"](b, s_, ex) == 1 - sm["color-dodge"](1 - b, 1 - s_, ex), "burn is dodge's dual")
        check(sm["overlay"](b, s_, ex) == sm["hard-light"](s_, b, ex), "overlay is hard-light exchanged")
        check(sm["darken"](b, s_, ex) + sm["lighten"](b, s_, ex) == b + s_, "darken plus lighten")
        check(sm["difference"](b, s_, ex) == sm["difference"](s_, b, ex), "difference is symmetric")
        check(sm["exclusion"](b, s_, ex) == sm["exclusion"](s_, b, ex), "exclusion is symmetric")
        # B9: the capped quotient gives what the real quotient gives, after min(1, .), 1 - . and sat.
        for mode in ("color-dodge", "color-burn"):
            real = sat(sm[mode](b, s_, ex))
            check(real == sat(capped_mix(mode, b, s_)), "B9: the capped quotient of %s at %s, %s" % (mode, b, s_))
    for b in reals:
        check(mix_soft_light(b, HALF, ex) == b, "soft-light at Cs = 1/2 is Cb")
    check(((16 * QUARTER - 12) * QUARTER + 4) * QUARTER == root(QUARTER, ex), "D is continuous at 1/4")
    triples = [[F(1, 4), F(1, 2), F(3, 4)], [F(1, 2)] * 3, [F(-1, 2)] * 3, [F(3, 2)] * 3, [F(-1), F(2), F(1, 2)],
               [F(1, 2), F(1, 2), F(1)], [F(0), F(1), F(1)], [F(-3), F(-3), F(5)], [F(2) ** 64, -F(2) ** 64, F(0)]]
    triples += [[F(rng.randint(-4000, 4000), rng.randint(1, 997)) for _ in range(3)] for _ in range(60)]
    for c in triples:
        for el in (F(0), F(1, 3), F(1), F(-2), F(7, 3)):
            check(lum(set_lum(c, el)) == el, "SetLum gives Lum l exactly: %s %s" % (c, el))
        check(lum(clip_color(c)) == lum(c), "ClipColor keeps Lum: %s" % c)
        check((min(c) == lum(c)) == (min(c) == max(c)) and (max(c) == lum(c)) == (min(c) == max(c)),
              "n = L, x = L and n = x are one condition: %s" % c)
        for sv in (F(0), F(1, 2), F(3)):
            if max(c) > min(c):
                check(saturation(set_sat(c, sv)) == sv, "Sat(SetSat(C, s)) = s")
            outs = set()
            for order in itertools.permutations(range(3)):
                if c[order[0]] <= c[order[1]] <= c[order[2]]:
                    outs.add(tuple(set_sat(c, sv, order)))
            check(len(outs) == 1, "SetSat does not depend on which tied component is called min, mid or max: %s" % c)
        for d in triples[:12]:
            check(mix("luminosity", c, d, ex) == mix("color", d, c, ex), "luminosity(b, s) = color(s, b)")
    pixels_b = [[F(0), F(0), F(0), F(0)], [F(1, 4), F(1, 8), F(1, 2), F(1, 2)], [F(3, 5), F(1, 5), F(2, 5), F(3, 5)],
                [F(-1, 2), F(3, 2), F(1, 4), F(1)], [F(2) ** 16, -F(2) ** 16, F(1, 3), F(1)]]
    for mode in BLEND_MODES:
        for d in pixels_b:
            check(operator(mode, [ZERO] * 4, d) == d, "B3: %s is bounded" % mode)
            for s_ in pixels_b:
                for m in (ZERO, F(1, 3), F(1, 2), ONE):
                    for o in (F(1, 2), F(2, 7), ONE):
                        scaled = [o * v for v in s_]
                        check(apply(d, scaled, m, mode) == [sat(v) for v in operator(mode, [m * v for v in scaled], d)],
                              "B4: coverage and opacity commute with %s" % mode)
    # B12: W3C's normal (B = Cs) is SRC_OVER exactly.
    for d, s_ in itertools.product(pixels_b, pixels_b):
        sa, da = s_[3], d[3]
        normal = [s_[i] * (ONE - da) + d[i] * (ONE - sa) + sa * da * (straight(s_)[i] if sa * da else 0) for i in range(3)]
        normal += [sa + da * (ONE - sa)]
        check(normal == operator("src-over", s_, d), "B12: normal is source-over")

    # T6: with every group isolated and not knockout, the general model is N5 to N9 exactly.
    def random_list(r):
        out, depth = [], 0
        for _ in range(r.randint(1, 9)):
            choice = r.random()
            if choice < 0.45 or depth == 0 and choice >= 0.75:
                out.append(dict(k=FILL, paint=PAINT_SOLID, rgba=(r.choice([0.0, 0.25, 1.0, -0.5, 1.5]), r.random(), 0.75,
                                                                  r.choice([0.0, 0.5, 0.6, 1.0])), space=TARGET))
            elif choice < 0.6:
                out.append(dict(k=CLIP_PUSH))
                out.append(dict(k=CLIP_POP)) if r.random() < 0.3 else depth.__add__(0)
            elif choice < 0.75 or depth > 2:
                if depth > 0:
                    out.append(dict(k=GROUP_POP))
                    depth -= 1
            else:
                out.append(dict(k=GROUP_PUSH, opacity=r.choice([1.0, 0.5, 0.75]), blend=r.choice([TARGET, SPACE_LINEAR_SRGB]),
                                composite=r.randrange(len(OPS))))
                depth += 1
        # close whatever is open, clips and groups in order
        stack = []
        for it in out:
            if it["k"] in (CLIP_PUSH, GROUP_PUSH):
                stack.append(it["k"])
            elif it["k"] in (CLIP_POP, GROUP_POP):
                stack.pop()
        for k in reversed(stack):
            out.append(dict(k=CLIP_POP if k == CLIP_PUSH else GROUP_POP))
        return out

    for t in range(150):
        items = random_list(rng)
        cov = [F(rng.choice([0, 1, 1, 64, 128, 255]), 255) for _ in items]
        backdrop = rng.choice([(0.0, 0.0, 0.0, 0.0), (0.25, 0.125, 0.5, 0.5), (0.0, 0.0, 1.0, 1.0)])
        target = rng.choice([SPACE_SRGB, SPACE_LINEAR_SRGB])
        want = evaluate(items, cov, (None, TARGET), target, backdrop)
        got = evaluate_general(items, cov, (None, TARGET), target, backdrop)
        check(want == got, "T6: the default group is N5 to N9 (case %d)" % t)

    # T3 to T5 against ISO 32000's own straight-color formulas, every combination, with blend modes.
    for t in range(120):
        isolated, knockout = rng.random() < 0.5, rng.random() < 0.5
        elements = []
        for _ in range(rng.randint(1, 4)):
            mode = rng.choice(["src-over", "multiply", "screen", "difference", "color-dodge", "hue", "luminosity"])
            elements.append((F(rng.choice([1, 3, 4, 8]), 8), F(rng.choice([1, 3, 5, 8]), 8),
                             [F(rng.randint(0, 8), 8) for _ in range(3)], mode))
        cb0 = [F(rng.randint(0, 4), 4) for _ in range(3)]
        ab0 = F(rng.choice([0, 1, 2, 4]), 4)
        c_g, a_g, f_g = pdf_literal(elements, isolated, knockout, (cb0, ab0))
        items = [dict(k=GROUP_PUSH, opacity=1.0, blend=TARGET, composite=0, isolation=0 if isolated else 1,
                      knockout=1 if knockout else 0)]
        cov = [ZERO]
        for fs, qs, cs, mode in elements:
            fill_ = dict(k=FILL, paint=PAINT_SOLID, rgba=tuple(float(v) for v in cs) + (float(qs),), space=TARGET)
            if mode == "src-over":
                items.append(fill_)
                cov.append(fs)
            else:
                items += [dict(k=GROUP_PUSH, opacity=1.0, blend=TARGET, composite=OP_INDEX[mode]), fill_, dict(k=GROUP_POP)]
                cov += [ZERO, fs, ZERO]
        items.append(dict(k=GROUP_POP))
        cov.append(ZERO)
        backdrop = [cb0[i] * ab0 for i in range(3)] + [ab0]
        got, _ = evaluate_general(items, cov, (None, TARGET), SPACE_SRGB, backdrop)
        # The group composited onto the backdrop by normal: shape f_g, alpha a_g.
        want = [(ONE - a_g) * backdrop[i] + a_g * c_g[i] for i in range(3)] + [backdrop[3] * (ONE - a_g) + a_g]
        check(got == want, "T3 to T5 are ISO 32000's formulas (isolated %s, knockout %s): %s %s" % (isolated, knockout, got, want))
    # Hand-worked: two half-transparent fills that cover the pixel; a knockout group keeps the second only.
    red_h = dict(k=FILL, paint=PAINT_SOLID, rgba=(1.0, 0.0, 0.0, 0.5), space=TARGET)
    blue_h = dict(k=FILL, paint=PAINT_SOLID, rgba=(0.0, 0.0, 1.0, 0.5), space=TARGET)
    ko = dict(k=GROUP_PUSH, opacity=1.0, blend=TARGET, composite=0, isolation=0, knockout=1)
    got, _ = evaluate_general([ko, red_h, blue_h, dict(k=GROUP_POP)], [0, 1, 1, 0], (None, TARGET), SPACE_SRGB, (0, 0, 0, 0))
    check(got == [ZERO, ZERO, F(1, 2), F(1, 2)], "a knockout group keeps the last element: %s" % got)
    got, _ = evaluate_general([dict(ko, knockout=0), red_h, blue_h, dict(k=GROUP_POP)], [0, 1, 1, 0], (None, TARGET),
                              SPACE_SRGB, (0, 0, 0, 0))
    check(got == [F(1, 4), ZERO, F(1, 2), F(3, 4)], "without knockout the two compose: %s" % got)
    # Isolation changes nothing for content that blends only by normal (PDF 11.4.5): every combination is equal.
    for items_ in ([red_h, blue_h], [red_h, dict(k=GROUP_PUSH, opacity=0.5, blend=TARGET, composite=0), blue_h, dict(k=GROUP_POP)]):
        cov = [F(1)] * len(items_)
        base = None
        for iso in (0, 1):
            push = dict(k=GROUP_PUSH, opacity=0.75, blend=TARGET, composite=0, isolation=iso, knockout=0)
            got, _ = evaluate_general([push] + items_ + [dict(k=GROUP_POP)], [ZERO] + cov + [ZERO], (None, TARGET), SPACE_SRGB,
                                      (0.25, 0.125, 0.5, 0.5))
            base = got if base is None else base
            check(got == base, "isolation does not change normal-only content: %s %s" % (got, base))
    # ... and it does with a blend mode inside: the multiply sees the page under a non-isolated group.
    mul = [dict(k=GROUP_PUSH, opacity=1.0, blend=TARGET, composite=OP_INDEX["multiply"]), dict(red_h, rgba=(1.0, 0.5, 0.5, 1.0)),
           dict(k=GROUP_POP)]
    a_, _ = evaluate_general([dict(ko, knockout=0, isolation=0)] + mul + [dict(k=GROUP_POP)], [0, 0, 1, 0, 0], (None, TARGET),
                             SPACE_SRGB, (0.0, 0.0, 0.5, 0.5))
    b_, _ = evaluate_general([dict(ko, knockout=0, isolation=1)] + mul + [dict(k=GROUP_POP)], [0, 0, 1, 0, 0], (None, TARGET),
                             SPACE_SRGB, (0.0, 0.0, 0.5, 0.5))
    check(a_ != b_, "a blend inside a non-isolated group sees the backdrop")
    c_, _ = evaluate_general(mul, [0, 1, 0], (None, TARGET), SPACE_SRGB, (0.0, 0.0, 0.5, 0.5))
    check(b_ == c_, "a non-isolated group of normal opacity 1 blends its content as if it were not there")

    # N11 against tools/gen-scene-vectors.py's own statement of the order.
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "gen-scene-vectors.py")
    spec = importlib.util.spec_from_file_location("genvec", path)
    genvec = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(genvec)
    declared = genvec.ORDER_ITEMS_DECLARED
    order, ties = paint_order(declared)
    check([declared[i] for i in order] == genvec.ORDER_ITEMS, "the order agrees with the generator's")
    check(ties == genvec.ORDER_TIES, "the ties agree with the generator's")
    # Overlay is stable and depth ties come in a run of three as two ties.
    run = [dict(k=GROUP_PUSH, opacity=1.0, blend=TARGET, composite=0, order=ORDER_DEPTH, layer=0, depth=0.0)]
    for layer in (0, 0, 0):
        run.append(dict(k=FILL, paint=0, rgba=(0, 0, 0, 1), space=TARGET, layer=layer, depth=1.0))
    run.append(dict(k=GROUP_POP))
    o, t = paint_order(run)
    check(o == [0, 1, 2, 3, 4] and [(a, b, r) for a, b, _, r in t] == [(1, 2, 3), (2, 3, 3)], "a run of three ties")

    if failures:
        for f in failures[:40]:
            print("composite-oracle: FAIL:", f)
        print("composite-oracle: %d failures" % len(failures))
        return 1
    print("composite-oracle: selftest passed")
    return 0


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--selftest":
        sys.exit(selftest())
    print(__doc__)
