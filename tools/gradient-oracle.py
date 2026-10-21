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
"""The reference of the scene's gradients (G1 to G13 of documentation/design.md and paint.h), exact where it can be.

A slow, independent statement of what a gradient paints at a point, sharing no code with the library, with
tools/composite-oracle.py or with any other generator (color-paint story P1, CAP-3).  Every input double is taken
as the exact rational it is.

- **Where a point is** (G6, G11): the chain of maps (the target's, the instances', the item's, the gradient's) is
  composed in rationals and inverted exactly; a map whose 2x2 determinant is zero makes the fill draw nothing.
- **Linear** (G7) is rational: `t` and the color are exact.
- **Radial** (G8) is exact in Q(sqrt D): the roots of the cone's quadratic are `u + v sqrt(D)` with rational `u`,
  `v`, `D`; every comparison (with `r(w) > 0`, between the roots, with an offset, with a period boundary) is decided
  exactly by signs; only the final color is rounded to a rational within 1e-70.
- **Sweep** (G9) needs an angle: `atan` is a `decimal` series at 110 digits with a stated bound (the result is
  within 1e-100 of the true angle, and so `t` within 1e-60 whenever `|a1 - a0| >= 1e-6` turn); the four quarter
  turns of `p - c` are exact.  Any comparison closer than 1e-60 is refused (`Undecided`).
- **The stops' conversion** (G3) is N2 and N3 in rationals, the power function in `decimal` at 100 digits
  (ln, exp), turned back into a rational and flagged inexact.

`evaluate(case)` returns a dictionary: `drawn` (0 when nothing is drawn: G8's outside, G10, G11), `reason`
("", "outside", "degenerate", "not-invertible"), `t` (a Fraction, exact or rounded), the straight color at the
point in the interpolation space with the paint's alpha applied (four Fractions), `exact` (no rounding anywhere), and
`margin`: how far, in `t`, the point is from the nearest discontinuity (a stop, a period boundary, the cone's edge
measured in `w`, the sweep's cut); a generator rejects a point whose margin is below 1e-30.

    python3 tools/gradient-oracle.py --selftest

runs it against itself: reflect is symmetric and repeat periodic, the cone whose start circle is inside the end
circle covers the plane, the sweep's quarter turns are exact, `atan` and `pi` against published digits, linear
against a second statement by projection, and the conversion's round trips.
"""
import sys
from decimal import Decimal as D, getcontext, localcontext
from fractions import Fraction as F

getcontext().prec = 110

LINEAR, RADIAL, SWEEP = 1, 2, 3
PAD, REPEAT, REFLECT = 0, 1, 2
RADIANS, DEGREES, GRADIANS, TURNS = 0, 1, 2, 3
SOLID, FOREGROUND, PALETTE = 0, 2, 3

ZERO = F(0)
ONE = F(1)


class Undecided(Exception):
    """A comparison the bounded arithmetic cannot settle: the point lies on (or within the bound of) a discontinuity."""


# --- Q(sqrt D) -------------------------------------------------------------------------------------------------------
class Q:
    """u + v sqrt(d), u, v, d rational, d >= 0."""

    __slots__ = ("u", "v", "d")

    def __init__(self, u, v=ZERO, d=ZERO):
        self.u, self.v, self.d = F(u), F(v), F(d)
        if self.d == 0:
            self.v = ZERO

    def lift(self, x):
        return x if isinstance(x, Q) else Q(x, 0, self.d)

    def _same(self, o):
        o = self.lift(o)
        if o.v != 0 and self.v != 0 and o.d != self.d:
            raise ValueError("mixed radicands")
        d = self.d if self.v != 0 else o.d
        return o, d

    def __add__(self, o):
        o, d = self._same(o)
        return Q(self.u + o.u, self.v + o.v, d)

    __radd__ = __add__

    def __neg__(self):
        return Q(-self.u, -self.v, self.d)

    def __sub__(self, o):
        return self + (-self.lift(o))

    def __rsub__(self, o):
        return self.lift(o) - self

    def __mul__(self, o):
        o, d = self._same(o)
        return Q(self.u * o.u + self.v * o.v * d, self.u * o.v + self.v * o.u, d)

    __rmul__ = __mul__

    def div_rational(self, r):
        r = F(r)
        return Q(self.u / r, self.v / r, self.d)

    def sign(self):
        su = (self.u > 0) - (self.u < 0)
        sv = (self.v > 0) - (self.v < 0)
        if sv == 0 or self.d == 0:
            return su
        if su == 0:
            return sv
        if su == sv:
            return su
        # opposite signs: compare u^2 with v^2 d
        a, b = self.u * self.u, self.v * self.v * self.d
        if a == b:
            return 0
        return su if a > b else sv

    def rational(self):
        return self.v == 0 or self.d == 0

    def decimal(self):
        r = D(self.d.numerator) / D(self.d.denominator)
        return D(self.u.numerator) / D(self.u.denominator) + D(self.v.numerator) / D(self.v.denominator) * r.sqrt()


def to_fraction(x):
    """A Decimal (or a Q) as the exact rational of its decimal digits."""
    if isinstance(x, Q):
        if x.rational():
            return x.u
        x = x.decimal()
    if isinstance(x, F):
        return x
    return F(x)


def dec(x):
    if isinstance(x, Q):
        return x.decimal()
    if isinstance(x, D):
        return x
    x = F(x)
    return D(x.numerator) / D(x.denominator)


# --- maps: (m00, m01, m10, m11, t0, t1), x' = m00 x + m01 y + t0 ---------------------------------------------------------
def compose(outer, inner):
    a, b, c, d, e, f = outer
    p, q, r, s, t, u = inner
    return (a * p + b * r, a * q + b * s, c * p + d * r, c * q + d * s, a * t + b * u + e, c * t + d * u + f)


def det(m):
    return m[0] * m[3] - m[1] * m[2]


def invert_point(m, x, y):
    dd = det(m)
    x, y = x - m[4], y - m[5]
    return ((m[3] * x - m[1] * y) / dd, (-m[2] * x + m[0] * y) / dd)


# --- the color line (G2, G4, G5) -------------------------------------------------------------------------------------
def interpolant(rgba, premul):
    r, g, b, a = rgba
    return [a * r, a * g, a * b, a] if premul else [r, g, b, a]


def lt(a, b):
    """a < b, exactly for Q and Fractions, within the bound for Decimals."""
    if isinstance(a, D) or isinstance(b, D):
        diff = dec(a) - dec(b)
        if abs(diff) < D("1e-60"):
            raise Undecided("a comparison closer than 1e-60")
        return diff < 0
    a = a if isinstance(a, Q) else Q(a)
    return (a - b).sign() < 0


def eq(a, b):
    if isinstance(a, D) or isinstance(b, D):
        return False if abs(dec(a) - dec(b)) >= D("1e-60") else _undecided()
    a = a if isinstance(a, Q) else Q(a)
    return (a - b).sign() == 0


def _undecided():
    raise Undecided("an equality within 1e-60")


def color_at(stops, premul, t):
    """G4: C(t) for t in [o_first, o_last] (stops sorted, at least two, o_first < o_last)."""
    o_last = stops[-1][0]
    if not lt(t, o_last):
        return interpolant(stops[-1][1], premul)
    j = 0
    while j < len(stops) and not lt(t, stops[j][0]):
        j += 1
    i = j - 1
    oi, oj = stops[i][0], stops[j][0]
    Ii, Ij = interpolant(stops[i][1], premul), interpolant(stops[j][1], premul)
    frac_num = t - dec(oi) if isinstance(t, D) else t - oi
    span = oj - oi
    out = []
    for k in range(4):
        if isinstance(frac_num, D):
            out.append(dec(Ii[k]) + frac_num / dec(span) * dec(Ij[k] - Ii[k]))
        elif isinstance(frac_num, Q):
            out.append((frac_num * (Ij[k] - Ii[k])).div_rational(span) + Ii[k])
        else:
            out.append(Ii[k] + F(frac_num) / span * (Ij[k] - Ii[k]))
    return out


def floor_of(x):
    """The floor of a Fraction, a Q or a Decimal, exactly (a Decimal within its bound)."""
    if isinstance(x, F):
        return x.numerator // x.denominator
    if isinstance(x, D):
        k = int(x.to_integral_value(rounding="ROUND_FLOOR"))
        if x - k < D("1e-60") or (k + 1) - x < D("1e-60"):
            raise Undecided("on a period boundary")
        return k
    k = int(x.decimal().to_integral_value(rounding="ROUND_FLOOR"))
    while (x - k).sign() < 0:
        k -= 1
    while (x - (k + 1)).sign() >= 0:
        k += 1
    return k


def extended(stops, extend, premul, t):
    """G4 and G5: the color at t, or None where nothing is drawn."""
    n = len(stops)
    if n == 0:
        return [ZERO] * 4
    if n == 1:
        return interpolant(stops[0][1], premul)
    o_first, o_last = stops[0][0], stops[-1][0]
    period = o_last - o_first
    if period == 0:
        if extend != PAD:
            return None
        return interpolant(stops[0][1] if lt(t, o_first) else stops[-1][1], premul)
    if lt(t, o_first) or lt(o_last, t):
        if extend == PAD:
            return interpolant(stops[0][1] if lt(t, o_first) else stops[-1][1], premul)
        if extend == REPEAT:
            if isinstance(t, D):
                rel = (t - dec(o_first)) / dec(period)
            elif isinstance(t, Q):
                rel = (t - o_first).div_rational(period)
            else:
                rel = (F(t) - o_first) / period
            k = floor_of(rel)
            t = t - k * period if not isinstance(t, D) else t - D(k) * dec(period)
        else:
            if isinstance(t, D):
                rel = (t - dec(o_first)) / dec(2 * period)
            elif isinstance(t, Q):
                rel = (t - o_first).div_rational(2 * period)
            else:
                rel = (F(t) - o_first) / (2 * period)
            k = floor_of(rel)
            if isinstance(t, D):
                m = t - dec(o_first) - D(k) * dec(2 * period)
                t = dec(o_first) + dec(period) - abs(m - dec(period))
            else:
                m = t - o_first - k * 2 * period
                mp = m - period
                neg = (mp if isinstance(mp, Q) else Q(mp)).sign() < 0
                t = o_first + period - (-mp if neg else mp)
    return color_at(stops, premul, t)


# --- the geometries (G7, G8, G9) -------------------------------------------------------------------------------------
def linear_t(geom, p):
    p0x, p0y, p1x, p1y, dx, dy = geom
    if dx == 0 and dy == 0:
        nx, ny = p1x - p0x, p1y - p0y
    else:
        nx, ny = -dy, dx
    den = (p1x - p0x) * nx + (p1y - p0y) * ny
    if den == 0:
        return None
    return ((p[0] - p0x) * nx + (p[1] - p0y) * ny) / den


def radial_w(geom, p):
    """G8: the largest w with r(w) > 0 and |p - c(w)| = r(w), as a Q; None outside; 'degenerate' for equal circles."""
    c0x, c0y, r0, c1x, c1y, r1 = geom
    if c0x == c1x and c0y == c1y and r0 == r1:
        return "degenerate"
    dcx, dcy, dr = c1x - c0x, c1y - c0y, r1 - r0
    pdx, pdy = p[0] - c0x, p[1] - c0y
    a = dcx * dcx + dcy * dcy - dr * dr
    b = pdx * dcx + pdy * dcy + r0 * dr
    c = pdx * pdx + pdy * pdy - r0 * r0
    candidates = []
    if a == 0:
        if b == 0:
            if c == 0:
                raise Undecided("every w solves: the point is on the cone's boundary line")
            return None
        candidates = [Q(c / (2 * b))]
    else:
        disc = b * b - a * c
        if disc < 0:
            return None
        hi = Q(b / a, ONE / a, disc) if a > 0 else Q(b / a, -ONE / a, disc)
        lo = Q(b / a, -ONE / a, disc) if a > 0 else Q(b / a, ONE / a, disc)
        candidates = [hi, lo]
    for w in candidates:
        rw = w * dr + r0
        if rw.sign() > 0:
            return w
    return None


PER_TURN = {DEGREES: F(360), GRADIANS: F(400), TURNS: F(1)}


def pi():
    """Machin: pi = 16 atan(1/5) - 4 atan(1/239), at the context's precision plus guard digits."""
    with localcontext() as ctx:
        ctx.prec += 10

        def atan_inv(n):
            x = D(1) / n
            x2 = x * x
            term, total, k = x, x, 1
            while True:
                term *= -x2
                k += 2
                add = term / k
                if add == 0 or abs(add) < D(10) ** (-(ctx.prec + 5)):
                    break
                total += add
            return total

        value = 16 * atan_inv(5) - 4 * atan_inv(239)
    return +value


def atan(x):
    """atan of a Decimal, by halving the argument three times and the Taylor series."""
    with localcontext() as ctx:
        ctx.prec += 10
        x = D(x)
        halvings = 0
        while abs(x) > D("0.1"):
            x = x / (1 + (1 + x * x).sqrt())
            halvings += 1
        x2 = x * x
        term, total, k = x, x, 1
        while True:
            term *= -x2
            k += 2
            add = term / k
            if add == 0 or abs(add) < D(10) ** (-(ctx.prec + 5)):
                break
            total += add
        value = total * (2 ** halvings)
    return +value


def theta_turns(x, y):
    """The angle of (x, y) in [0, 1) turns, from +x toward +y; exact (a Fraction) on the axes, else a Decimal."""
    if y == 0 and x >= 0:
        return F(0)
    if x == 0:
        return F(1, 4) if y > 0 else F(3, 4)
    if y == 0:
        return F(1, 2)
    two_pi = 2 * pi()
    base = atan(dec(F(y) / F(x)))
    if x > 0:
        angle = base if y > 0 else base + two_pi
    else:
        angle = base + two_pi / 2
    return angle / two_pi


def sweep_t(geom, unit, p):
    cx, cy, a0, a1 = geom
    turns = theta_turns(p[0] - cx, p[1] - cy)
    if unit == RADIANS:
        theta = dec(turns) * 2 * pi()
    else:
        theta = turns * PER_TURN[unit] if isinstance(turns, F) else turns * dec(PER_TURN[unit])
    return theta


# --- one point (G6 to G12) ----------------------------------------------------------------------------------------------
def evaluate(case):
    """See the module comment.  case: kind, geom, unit, extend, premul, stops [(offset, (r, g, b, a))] in the
    interpolation space (any order: sorted stably here, G2), chain [maps, target first, gradient last], q, alpha."""
    kind, geom = case["kind"], tuple(F(v) for v in case["geom"])
    stops = sorted(((F(o), tuple(F(c) for c in rgba)) for o, rgba in case["stops"]), key=lambda s: s[0])
    premul, extend = case["premul"], case["extend"]
    alpha = F(case.get("alpha", 1.0))
    chain = [tuple(F(v) for v in m) for m in case["chain"]]
    nothing = dict(drawn=0, t=None, rgba=[ZERO] * 4, exact=True, margin=None)
    # G11: a map whose determinant is exactly zero.
    for m in chain:
        if det(m) == 0:
            return dict(nothing, reason="not-invertible")
    composed = chain[0]
    for m in chain[1:]:
        composed = compose(composed, m)
    q = (F(case["q"][0]), F(case["q"][1]))
    p = invert_point(composed, q[0], q[1])
    margin = None
    if kind == LINEAR:
        t = linear_t(geom, p)
        if t is None:
            return dict(nothing, reason="degenerate")
    elif kind == RADIAL:
        w = radial_w(geom, p)
        if w == "degenerate":
            return dict(nothing, reason="degenerate")
        if w is None:
            return dict(nothing, reason="outside")
        t = w if not w.rational() else w.u
        # The cone's edge: how far r(w) is from zero and the discriminant from zero, in w.
        rw = dec(w * (geom[5] - geom[2]) + geom[2]) if isinstance(w, Q) else dec(w * (geom[5] - geom[2]) + geom[2])
        margin = abs(rw)
        if isinstance(w, Q) and w.v != 0:
            margin = min(margin, abs(dec(w.v)) * dec(w.d).sqrt())
    else:
        cx, cy, a0, a1 = geom
        unit = case["unit"]
        if a0 == a1:
            if extend != PAD:
                return dict(nothing, reason="degenerate")
            theta = sweep_t(geom, unit, p)
            if isinstance(theta, D) and abs(theta - dec(a0)) < D("1e-60"):
                raise Undecided("on the step of a0 = a1")
            below = (dec(theta) < dec(a0)) if isinstance(theta, D) else theta < a0
            if len(stops) == 0:
                rgba = [ZERO] * 4
            else:
                rgba = interpolant(stops[0][1] if below else stops[-1][1], premul)
            return finish(rgba, premul, alpha, None, True, abs(dec(theta) - dec(a0)))
        theta = sweep_t(geom, unit, p)
        if isinstance(theta, D):
            t = (theta - dec(a0)) / dec(a1 - a0)
        else:
            t = (theta - a0) / (a1 - a0)
        # The cut: theta near 0 (one turn), in t units.
        turns = theta_turns(p[0] - cx, p[1] - cy)
        cut = min(dec(turns), 1 - dec(turns)) if isinstance(turns, D) else None
        if cut is not None:
            margin = cut
    rgba = extended(stops, extend, premul, t)
    if rgba is None:
        return dict(nothing, reason="degenerate")
    # The margin to the nearest stop and period boundary, in t.
    td = dec(t)
    if len(stops) >= 2:
        o_first, o_last = stops[0][0], stops[-1][0]
        period = o_last - o_first
        for o, _ in stops:
            if extend == PAD or period == 0:
                d = abs(td - dec(o))
            else:
                per = dec(period) * (1 if extend == REPEAT else 2)
                rel = (td - dec(o)) / per
                frac = rel - rel.to_integral_value(rounding="ROUND_FLOOR")
                d = min(frac, 1 - frac) * per
                if extend == REFLECT:
                    rel2 = (td - (2 * dec(o_last) - dec(o))) / per
                    frac2 = rel2 - rel2.to_integral_value(rounding="ROUND_FLOOR")
                    d = min(d, min(frac2, 1 - frac2) * per)
            margin = d if margin is None else min(margin, d)
    exact = not isinstance(t, D) and not (isinstance(t, Q) and not t.rational())
    return finish(rgba, premul, alpha, t, exact, margin)


def finish(rgba, premul, alpha, t, exact, margin):
    """G4's straight color, its alpha times the paint's (G12)."""
    vals = [to_fraction(c) if isinstance(c, (Q, D)) else F(c) for c in rgba]
    if any(isinstance(c, D) or (isinstance(c, Q) and not c.rational()) for c in rgba):
        exact = False
    if premul:
        a = vals[3]
        vals = [ZERO] * 4 if a == 0 else [vals[0] / a, vals[1] / a, vals[2] / a, a]
    vals[3] = vals[3] * alpha
    return dict(drawn=1, reason="", t=None if t is None else to_fraction(t), rgba=vals, exact=exact, margin=margin)


# --- G3: a stop's conversion (N2, N3) --------------------------------------------------------------------------------------
GAMUTS = {1: ((F("0.64"), F("0.33")), (F("0.30"), F("0.60")), (F("0.15"), F("0.06"))),
          2: ((F("0.64"), F("0.33")), (F("0.21"), F("0.71")), (F("0.15"), F("0.06"))),
          3: ((F("0.680"), F("0.320")), (F("0.265"), F("0.690")), (F("0.150"), F("0.060"))),
          4: ((F("0.708"), F("0.292")), (F("0.170"), F("0.797")), (F("0.131"), F("0.046")))}
WHITE = (F("0.3127"), F("0.3290"))


def solve3(m, v):
    """m x = v by Cramer, exactly."""
    def d3(a):
        return (a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) +
                a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]))
    dd = d3(m)
    out = []
    for col in range(3):
        a = [[v[r] if c == col else m[r][c] for c in range(3)] for r in range(3)]
        out.append(d3(a) / dd)
    return out


def to_xyz(primaries):
    xy = GAMUTS[primaries]
    cols = [(x / y, ONE, (1 - x - y) / y) for x, y in xy]
    p = [[cols[c][r] for c in range(3)] for r in range(3)]
    wx, wy = WHITE
    s = solve3(p, [wx / wy, ONE, (1 - wx - wy) / wy])
    return [[s[c] * p[r][c] for c in range(3)] for r in range(3)]


def power(x, e):
    """x^e for x > 0, by decimal ln and exp, as a rational (inexact)."""
    with localcontext() as ctx:
        ctx.prec = 100
        return F((dec(e) * dec(x).ln()).exp())


def decode(space, x):
    transfer, gamma = space[1], space[2]
    if transfer == 1:
        return x, True
    if transfer == 2:
        if x <= F("0.04045"):
            return x / F("12.92"), True
        return power((x + F("0.055")) / F("1.055"), F("2.4")), False
    if x == 0:
        return ZERO, True
    s = 1 if x > 0 else -1
    return s * power(abs(x), F(gamma)), False


def encode(space, y):
    transfer, gamma = space[1], space[2]
    if transfer == 1:
        return y, True
    if transfer == 2:
        if y <= F("0.0031308"):
            return y * F("12.92"), True
        return F("1.055") * power(y, 1 / F("2.4")) - F("0.055"), False
    if y == 0:
        return ZERO, True
    s = 1 if y > 0 else -1
    return s * power(abs(y), 1 / F(gamma)), False


def convert(rgb, a, b):
    """N3: straight (r, g, b) from space a to space b (concrete spaces (primaries, transfer, gamma)); and exactness."""
    rgb = [F(c) for c in rgb]
    if (a[0], a[1], F(a[2])) == (b[0], b[1], F(b[2])):
        return rgb, True
    exact = True
    lin = []
    for c in rgb:
        v, e = decode(a, c)
        lin.append(v)
        exact = exact and e
    if a[0] != b[0]:
        ma, mb = to_xyz(a[0]), to_xyz(b[0])
        xyz = [sum(ma[r][k] * lin[k] for k in range(3)) for r in range(3)]
        lin = solve3(mb, xyz)
    out = []
    for c in lin:
        v, e = encode(b, c)
        out.append(v)
        exact = exact and e
    return out, exact


# --- the selftest ----------------------------------------------------------------------------------------------------------
def selftest():
    failures = []

    def check(ok, what):
        if not ok:
            failures.append(what)

    ident = (1, 0, 0, 1, 0, 0)
    stops = [(0.0, (1.0, 0.0, 0.0, 1.0)), (0.5, (0.0, 1.0, 0.0, 0.5)), (1.0, (0.0, 0.0, 1.0, 1.0))]
    base = dict(kind=LINEAR, geom=(0, 0, 8, 0, 0, 0), unit=0, premul=0, stops=stops, chain=[ident], alpha=1.0)
    # Reflect is symmetric about every period boundary, and repeat periodic.
    for x in (F(1, 3), F(5, 7), F(13, 11)):
        for k in (1, 2, 3):
            r1 = evaluate(dict(base, extend=REFLECT, q=(8 * k + 8 * x, 0)))
            r2 = evaluate(dict(base, extend=REFLECT, q=(8 * k - 8 * x, 0)))
            check(r1["rgba"] == r2["rgba"], "reflect is symmetric about t = %d" % k)
            r3 = evaluate(dict(base, extend=REPEAT, q=(8 * x, 0)))
            r4 = evaluate(dict(base, extend=REPEAT, q=(8 * x + 8 * k, 0)))
            r5 = evaluate(dict(base, extend=REPEAT, q=(8 * x - 8 * k, 0)))
            check(r3["rgba"] == r4["rgba"] == r5["rgba"], "repeat is periodic")
    # Pad: below the first stop and above the last.
    check(evaluate(dict(base, extend=PAD, q=(-100, 3)))["rgba"] == [1, 0, 0, 1], "pad below")
    check(evaluate(dict(base, extend=PAD, q=(100, 3)))["rgba"] == [0, 0, 1, 1], "pad above")
    # Linear by a second statement: the projection onto p1 - p0 when d is zero, at random-ish points.
    for qx, qy in ((F(3), F(7)), (F(-2, 3), F(5)), (F(9, 4), F(-1, 8))):
        g = (F(1), F(2), F(5), F(-1), F(0), F(0))
        t = evaluate(dict(base, geom=g, extend=PAD, q=(qx, qy)))["t"]
        vx, vy = g[2] - g[0], g[3] - g[1]
        proj = ((qx - g[0]) * vx + (qy - g[1]) * vy) / (vx * vx + vy * vy)
        check(t == proj, "linear is the projection when d is zero")
        # An isoline direction perpendicular to p1 - p0 is the same gradient.
        t2 = evaluate(dict(base, geom=g[:4] + (vy, -vx), extend=PAD, q=(qx, qy)))["t"]
        check(t2 == proj, "an isoline direction perpendicular to p1 - p0 changes nothing")
    # Equal offsets: the first below, the last at and above (G4).
    eqs = [(0.0, (0, 0, 0, 1)), (0.5, (1, 0, 0, 1)), (0.5, (0, 1, 0, 1)), (1.0, (0, 0, 1, 1))]
    check(evaluate(dict(base, stops=eqs, extend=PAD, q=(4, 0)))["rgba"] == [0, 1, 0, 1], "the last at the offset")
    below = evaluate(dict(base, stops=eqs, extend=PAD, q=(F(399, 100), 0)))["rgba"]
    check(below[1] == 0 and below[0] > F(99, 100), "the first just below")
    # The cone whose start circle is inside the end circle covers the plane.
    cone = dict(base, kind=RADIAL, geom=(F(1), F(0), F(1), F(0), F(0), F(10)), extend=PAD)
    for qx, qy in ((0, 0), (1, 0), (F(1, 3), F(7, 2)), (-50, 40), (100, -100)):
        check(evaluate(dict(cone, q=(qx, qy)))["drawn"] == 1, "a nested cone covers (%s, %s)" % (qx, qy))
    # On the end circle t is 1 exactly; on the start circle 0.
    check(evaluate(dict(cone, q=(0, 10)))["t"] == 1, "the end circle is t = 1")
    check(evaluate(dict(cone, q=(2, 0)))["t"] == 0, "the start circle is t = 0")
    # A cone whose start circle is outside the end circle leaves points outside; equal circles draw nothing.
    split = dict(cone, geom=(F(0), F(0), F(1), F(10), F(0), F(2)))
    check(evaluate(dict(split, q=(0, 20)))["reason"] == "outside", "outside the cone draws nothing")
    check(evaluate(dict(cone, geom=(1, 1, 2, 1, 1, 2), q=(0, 0)))["reason"] == "degenerate", "equal circles")
    # Negative radii: r0 = -1, r1 = -2 paints only where r(w) > 0, i.e. w <= -1/... none at w in [0, 1].
    neg = dict(cone, geom=(F(0), F(0), F(-1), F(1), F(0), F(-2)))
    r = evaluate(dict(neg, q=(-5, 0)))
    check(r["drawn"] == 0 or r["t"] < -1, "negative radii paint only where r(w) > 0")
    # The sweep's quarter turns are exact in degrees, gradians and turns.
    sweep = dict(base, kind=SWEEP, geom=(F(1), F(1), F(0), F(360)), unit=DEGREES, extend=PAD,
                 stops=[(0.0, (0, 0, 0, 1)), (1.0, (1, 1, 1, 1))])
    for (qx, qy), t in (((5, 1), F(0)), ((1, 5), F(1, 4)), ((-3, 1), F(1, 2)), ((1, -2), F(3, 4))):
        res = evaluate(dict(sweep, q=(qx, qy)))
        check(res["t"] == t and res["exact"], "the quarter turn at (%s, %s)" % (qx, qy))
    # a0 = a1 under pad is a step: the first stop below a0, the last at and above it (exactly at a0 on an axis).
    step = dict(sweep, geom=(F(0), F(0), F(90), F(90)), stops=[(0.0, (1, 0, 0, 1)), (1.0, (0, 0, 1, 1))])
    check(evaluate(dict(step, q=(0, 3)))["rgba"] == [0, 0, 1, 1], "a0 = a1: the last stop at a0")
    check(evaluate(dict(step, q=(3, 1)))["rgba"] == [1, 0, 0, 1], "a0 = a1: the first stop below a0")
    check(evaluate(dict(step, q=(-3, 1)))["rgba"] == [0, 0, 1, 1], "a0 = a1: the last stop above a0")
    for unit, per in ((GRADIANS, 400), (TURNS, 1)):
        res = evaluate(dict(sweep, unit=unit, geom=(F(1), F(1), F(0), F(per)), q=(1, 5)))
        check(res["t"] == F(1, 4), "a quarter turn in unit %d" % unit)
    # pi and atan against published digits.
    PI50 = "3.14159265358979323846264338327950288419716939937510"
    check(str(pi())[:52] == PI50, "pi")
    check(abs(atan(D(1)) * 4 - pi()) < D("1e-100"), "atan(1) is pi / 4")
    check(abs(atan(D(1) / D(3).sqrt()) * 6 - pi()) < D("1e-100"), "atan(1/sqrt 3) is pi / 6")
    ATAN_HALF = "0.46364760900080611621425623146121440202853705428612"  # atan(1/2), published
    check(str(atan(D("0.5")))[:52] == ATAN_HALF, "atan(1/2)")
    # The eighth turn of a sweep in radians: t = (pi/4) / (2 pi) exactly enough.
    rad = evaluate(dict(sweep, unit=RADIANS, geom=(F(0), F(0), F(0), F(1)), q=(1, 1)))
    check(abs(dec(rad["t"]) - pi() / 4) < D("1e-90"), "an eighth turn in radians")
    # G11: a zero scale anywhere in the chain draws nothing; a tiny nonzero one draws.
    check(evaluate(dict(base, extend=PAD, q=(1, 1), chain=[ident, (0, 0, 0, 1, 0, 0)]))["reason"] == "not-invertible",
          "a zero scale is not invertible")
    tiny = F(1, 2 ** 600)
    check(evaluate(dict(base, extend=PAD, q=(0, 0), chain=[(tiny, 0, 0, tiny, 0, 0)]))["drawn"] == 1,
          "a tiny scale is invertible")
    # G10 for linear: p0 = p1, and d parallel to p1 - p0.
    check(evaluate(dict(base, geom=(1, 1, 1, 1, 0, 0), extend=PAD, q=(0, 0)))["reason"] == "degenerate", "p0 = p1")
    check(evaluate(dict(base, geom=(0, 0, 2, 1, 4, 2), extend=PAD, q=(0, 0)))["reason"] == "degenerate", "d parallel")
    # P = 0: pad is a step, repeat and reflect draw nothing.
    flat = [(0.5, (1, 0, 0, 1)), (0.5, (0, 0, 1, 1))]
    check(evaluate(dict(base, stops=flat, extend=PAD, q=(1, 0)))["rgba"] == [1, 0, 0, 1], "a P = 0 step below")
    check(evaluate(dict(base, stops=flat, extend=PAD, q=(4, 0)))["rgba"] == [0, 0, 1, 1], "a P = 0 step at")
    check(evaluate(dict(base, stops=flat, extend=REPEAT, q=(1, 0)))["drawn"] == 0, "a P = 0 repeat draws nothing")
    # Premultiplied interpolation of a transparent stop keeps the other's color (G4).
    pm = [(0.0, (1, 0, 0, 1)), (1.0, (0, 0, 1, 0))]
    mid = evaluate(dict(base, stops=pm, premul=1, extend=PAD, q=(4, 0)))["rgba"]
    check(mid == [1, 0, 0, F(1, 2)], "premultiplied: a transparent stop does not darken")
    mid0 = evaluate(dict(base, stops=pm, premul=0, extend=PAD, q=(4, 0)))["rgba"]
    check(mid0 == [F(1, 2), 0, F(1, 2), F(1, 2)], "straight: the colors mix")
    # N3: the identity is exact, a transfer round trip is within the bound, and white maps to white.
    srgb, lin_srgb, p3 = (1, 2, 0.0), (1, 1, 0.0), (3, 2, 0.0)
    v, e = convert((F(1, 4), F(1, 2), F(3, 4)), srgb, srgb)
    check(v == [F(1, 4), F(1, 2), F(3, 4)] and e, "the same space is the identity")
    there, _ = convert((F(1, 4), F(1, 2), F(3, 4)), srgb, lin_srgb)
    back, _ = convert(there, lin_srgb, srgb)
    check(all(abs(back[i] - [F(1, 4), F(1, 2), F(3, 4)][i]) < F(1, 10 ** 60) for i in range(3)), "an sRGB round trip")
    white, _ = convert((ONE, ONE, ONE), srgb, p3)
    check(all(abs(c - 1) < F(1, 10 ** 60) for c in white), "white is white in every gamut")
    m = to_xyz(1)
    check(abs(m[0][0] - F("0.4123907992659595")) < F(1, 10 ** 15), "the sRGB matrix against its published value")
    if failures:
        for f in failures:
            print("FAIL  gradient-oracle selftest: %s" % f)
        return 1
    print("gradient-oracle: selftest passed")
    return 0


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--selftest":
        sys.exit(selftest())
    sys.exit("usage: gradient-oracle.py --selftest (gen-gradient-vectors.py imports it)")
