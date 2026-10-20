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
"""The reference of the scene's compositing definition (N1 to N11 and N14 of documentation/design.md), in exact rationals.

This is a slow, independent statement of what a closed list *means*: the color
spaces and their conversion (N1 to N3), the Porter-Duff operators (N4), the one
primitive `apply` (N5), layers and groups (N6, N8), fills (N7), clips (N9), the
foreground (N10), the order of a sequence (N11) and the palette (N14: an entry's color with
its alpha scaled; a missing entry is refused, so its fill draws nothing).  It shares no code with the
library or with the other generators.  Arithmetic is `fractions.Fraction`
throughout; the only inexact step is the power function of a transfer, which is
evaluated with `decimal` at 80 digits and turned back into the exact rational of
that decimal (an error below 1e-70), and a result that went through one is flagged
inexact.  Everything else is exact: a case whose values are dyadic rationals and
whose spaces are all the same has an exact answer a double evaluator can match bit
for bit.

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
from fractions import Fraction as F

getcontext().prec = 80

ZERO = F(0)
ONE = F(1)

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
    lin = [decode(a, v, ex) for v in rgb]
    if a[0] != b[0]:
        lin = matvec(MATRIX_INVERSE[b[0]], matvec(MATRIX[a[0]], lin))
    return [encode(b, v, ex) for v in lin]


def convert_premultiplied(px, a, b, ex):
    """Premultiplied (R, G, B, A) from space a to space b."""
    if a == b:
        return list(px)
    alpha = px[3]
    if alpha == 0:
        return [ZERO, ZERO, ZERO, ZERO]
    straight = [px[i] / alpha for i in range(3)]
    out = convert(straight, a, b, ex)
    return [out[0] * alpha, out[1] * alpha, out[2] * alpha, alpha]


# --- N4, N5: operators and the primitive ----------------------------------------------------------------
OPS = ["src-over", "clear", "src", "dest", "dest-over", "src-in", "dest-in", "src-out", "dest-out", "src-atop",
       "dest-atop", "xor", "plus"]
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


def operator(op, s, d):
    """op(S, D) on premultiplied (R, G, B, A) values."""
    if op == "plus":
        return [min(ONE, s[i] + d[i]) for i in range(4)]
    fa, fb = factors(op, s[3], d[3])
    return [fa * s[i] + fb * d[i] for i in range(4)]


def apply(d, s, m, op):
    """N5: (1 - m) D + m op(S, D)."""
    o = operator(op, s, d)
    return [(ONE - m) * d[i] + m * o[i] for i in range(4)]


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
            top.layer = apply(top.layer, source, m, "src-over")
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
            parent.layer = apply(parent.layer, source, done.outer, op)
        else:
            raise ValueError("an instance is not evaluated here: a picture is expanded by the caller")
    assert len(stack) == 1
    return root.layer, ex.inexact


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
