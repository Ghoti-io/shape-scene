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
"""The reference of the clip union's coverage (rules CU1, CU2, CU16; clip_union.h).

Independent of the library and of every other oracle; `fractions.Fraction` throughout, no float takes part in a decision.

A **member** is a list of closed polygons (contours) whose vertices are integer points of the 26.6 lattice (1/64 pixel,
y down) and a fill rule ("nonzero" or "evenodd").  A union is a list of members; the grid is W x H pixels, pixel (px, py)
being the lattice square [64 px, 64 px + 64] x [64 py, 64 py + 64].

Three numbers per pixel, which must agree where the members' boxes have disjoint interiors:

(a) `exact_area`: the exact area of the union of the members' regions inside the pixel, in units of 1/8192 of a pixel (a
    lattice unit squared is 2 of those), by an exact slab sweep: every x at which anything changes (a vertex, a crossing of two
    edges, a crossing of an edge with the pixel's rows or columns) bounds a slab; inside a slab the order of the edges is fixed, so the
    measure of the union is linear in x and its integral is the slab's width times the measure at the slab's middle line,
    where each member's rule is applied to the winding of that member there.
(b) `sum_area`: the sum of the members' own areas, each `a_i` the member's accumulated winding area reduced by its rule (the
    magnitude fold: |area|, held at 8192 under nonzero, folded mod 16384 and reflected at 8192 under even-odd), the sum held at
    8192.  This is CU2's fast-tier form.
(c) `bracket`: a set-theoretic bracket `[inner, outer]` of (a), from a K x K grid of cells per pixel: a cell no edge of any member
    meets has constant membership in every member, so it counts whole in `inner` when the union holds at its centre and is left out
    otherwise; every cell an edge meets is added whole to `outer` (exact segment-against-closed-box tests).

The committed byte is `min(255, floor(min(8192, area) / 32))` of (a).  A **disjointness** half classifies pairs of members by the
interiors of their lattice boxes (the box of the vertices of polygon members).

`--selftest [--plant NAME]` runs the oracle's invariants over a family of cases (a must lie in c's bracket, and equal b where the
boxes are disjoint) and `--plant` breaks the oracle on purpose; each plant must make the selftest fail:

  sum-bytes        the coverage is the sum of the members' bytes
  touching-overlap a pair of boxes that share an edge is read as overlapping
  edge-twice       the area of a shared edge's pixel counts the abutting member's cover twice
  one-rule         the first member's rule is applied to every member
  no-saturate      the sum of areas is not held at 8192
  outer-ignores-boundary  the bracket's outer count leaves out the cells an edge meets
"""
import sys
from fractions import Fraction as F
from itertools import combinations

PLANT = None


def edges_of(member):
    es = []
    for contour in member[0]:
        n = len(contour)
        for i in range(n):
            a, b = contour[i], contour[(i + 1) % n]
            if a != b:
                es.append((F(a[0]), F(a[1]), F(b[0]), F(b[1])))
    return es


def y_at(e, x):
    x0, y0, x1, y1 = e
    return y0 + (y1 - y0) * (x - x0) / (x1 - x0)


def crossing_x(e, f):
    """The x at which edges e and f (non-vertical) cross, if they do properly or touch, else None."""
    ax0, ay0, ax1, ay1 = e
    bx0, by0, bx1, by1 = f
    da = (ay1 - ay0) / (ax1 - ax0)
    db = (by1 - by0) / (bx1 - bx0)
    if da == db:
        return None
    x = (by0 - ay0 + da * ax0 - db * bx0) / (da - db)
    lo = max(min(ax0, ax1), min(bx0, bx1))
    hi = min(max(ax0, ax1), max(bx0, bx1))
    return x if lo <= x <= hi else None


def winding_intervals(edges, x, y0, y1):
    """The y-intervals of [y0, y1] with their winding at the vertical line x (x is never a vertex's x)."""
    cr = []
    for (ex0, ey0, ex1, ey1) in edges:
        if ex0 == ex1:
            continue
        if min(ex0, ex1) < x < max(ex0, ex1):
            cr.append((y_at((ex0, ey0, ex1, ey1), x), 1 if ex1 > ex0 else -1))
    cr.sort()
    out = []
    w = 0
    prev = y0
    for (y, d) in cr:
        yc = min(max(y, y0), y1)
        if yc > prev:
            out.append((prev, yc, w))
            prev = yc
        w += d
    if y1 > prev:
        out.append((prev, y1, w))
    return out


def holds(rule, w):
    return (w % 2 != 0) if rule == "evenodd" else (w != 0)


def breakpoints(members, px0, px1, py0, py1):
    xs = {F(px0), F(px1)}
    es = [e for m in members for e in edges_of(m)]
    for e in es:
        for x in (e[0], e[2]):
            if px0 <= x <= px1:
                xs.add(x)
        if e[0] != e[2]:
            for yy in (py0, py1):
                if e[1] != e[3]:
                    x = e[0] + (e[2] - e[0]) * (yy - e[1]) / (e[3] - e[1])
                    if min(e[0], e[2]) <= x <= max(e[0], e[2]) and px0 <= x <= px1:
                        xs.add(x)
    nv = [e for e in es if e[0] != e[2]]
    for e, f in combinations(nv, 2):
        x = crossing_x(e, f)
        if x is not None and px0 <= x <= px1:
            xs.add(x)
    return sorted(xs)


def measure(intervals_by_member, rules, union=True):
    """The y-measure of the union (or, with union False, the list of per-member measures) of the intervals where each member's rule holds."""
    per = []
    for ivs, rule in zip(intervals_by_member, rules):
        per.append([(a, b) for (a, b, w) in ivs if holds(rule, w)])
    if not union:
        return [sum(b - a for a, b in p) for p in per]
    allv = sorted(i for p in per for i in p)
    total = F(0)
    cur_a = cur_b = None
    for a, b in allv:
        if cur_b is None or a > cur_b:
            if cur_b is not None:
                total += cur_b - cur_a
            cur_a, cur_b = a, b
        else:
            cur_b = max(cur_b, b)
    if cur_b is not None:
        total += cur_b - cur_a
    return total


def exact_area(members, px, py):
    """(a): the exact area of the union in the pixel, in 1/8192 pixel."""
    px0, px1, py0, py1 = F(64 * px), F(64 * px + 64), F(64 * py), F(64 * py + 64)
    xs = breakpoints(members, px0, px1, py0, py1)
    rules = [m[1] if PLANT != "one-rule" else members[0][1] for m in members]
    edge_lists = [edges_of(m) for m in members]
    area = F(0)
    for xa, xb in zip(xs, xs[1:]):
        mid = (xa + xb) / 2
        ivs = [winding_intervals(el, mid, py0, py1) for el in edge_lists]
        area += (xb - xa) * measure(ivs, rules)
    return area * 2


def member_area(member, px, py, rule=None):
    """The accumulated winding area of one member in the pixel (signed), in 1/8192 pixel."""
    px0, px1, py0, py1 = F(64 * px), F(64 * px + 64), F(64 * py), F(64 * py + 64)
    xs = breakpoints([member], px0, px1, py0, py1)
    el = edges_of(member)
    total = F(0)
    for xa, xb in zip(xs, xs[1:]):
        mid = (xa + xb) / 2
        s = F(0)
        for (a, b, w) in winding_intervals(el, mid, py0, py1):
            s += (b - a) * w
        total += (xb - xa) * s
    return total * 2


def fold(area, rule):
    m = abs(area)
    if rule == "evenodd":
        m = m % 16384
        if m > 8192:
            m = 16384 - m
    else:
        m = min(m, F(8192))
    return m


def sum_area(members, px, py):
    """(b): the sum of the members' reduced areas, held at 8192."""
    total = sum(fold(member_area(m, px, py), m[1]) for m in members)
    if PLANT == "edge-twice":
        total += sum(fold(member_area(m, px, py), m[1]) for m in members[1:2]) if len(members) > 1 else 0
    return total if PLANT == "no-saturate" else min(total, F(8192))


def byte_of(area):
    a = min(F(8192), area)
    return min(255, int(a // 32))


def byte(members, px, py):
    if PLANT == "sum-bytes":
        return min(255, sum(byte_of(fold(member_area(m, px, py), m[1])) for m in members))
    return byte_of(exact_area(members, px, py))


def point_in(member, x, y):
    """Winding of the member at (x, y), which is never on an edge for the cell centres the bracket uses (x is shifted off any vertex x)."""
    w = 0
    for (ex0, ey0, ex1, ey1) in edges_of(member):
        if ex0 == ex1:
            continue
        if min(ex0, ex1) < x < max(ex0, ex1) and y_at((ex0, ey0, ex1, ey1), x) > y:
            w += 1 if ex1 > ex0 else -1
    return w


def seg_meets_box(e, bx0, by0, bx1, by1):
    x0, y0, x1, y1 = e
    # Liang-Barsky on the closed box, exact
    t0, t1 = F(0), F(1)
    for p, q in ((-(x1 - x0), x0 - bx0), (x1 - x0, bx1 - x0), (-(y1 - y0), y0 - by0), (y1 - y0, by1 - y0)):
        if p == 0:
            if q < 0:
                return False
        else:
            r = q / p
            if p < 0:
                if r > t1:
                    return False
                t0 = max(t0, r)
            else:
                if r < t0:
                    return False
                t1 = min(t1, r)
    return t0 <= t1


def bracket(members, px, py, k=8):
    """(c): [inner, outer] in 1/8192 pixel."""
    es = [e for m in members for e in edges_of(m)]
    inner = outer = F(0)
    cell = F(64, k)
    unit = cell * cell * 2
    for i in range(k):
        for j in range(k):
            bx0, by0 = F(64 * px) + i * cell, F(64 * py) + j * cell
            bx1, by1 = bx0 + cell, by0 + cell
            touched = any(seg_meets_box(e, bx0, by0, bx1, by1) for e in es)
            if touched:
                if PLANT != "outer-ignores-boundary":
                    outer += unit
            else:
                cx, cy = bx0 + cell / 2, by0 + cell / 2
                if any(holds(m[1], point_in(m, cx, cy)) for m in members):
                    inner += unit
                    outer += unit
    return inner, outer


def box_of(member):
    pts = [p for c in member[0] for p in c]
    if not pts:
        return None
    return (min(p[0] for p in pts), min(p[1] for p in pts), max(p[0] for p in pts), max(p[1] for p in pts))


def boxes_disjoint(a, b):
    """CU16: the interiors of the boxes do not meet; an empty box meets nothing."""
    ba, bb = box_of(a), box_of(b)
    if ba is None or bb is None:
        return True
    if PLANT == "touching-overlap":
        return max(ba[0], bb[0]) <= min(ba[2], bb[2]) and max(ba[1], bb[1]) <= min(ba[3], bb[3]) and False
    return not (max(ba[0], bb[0]) < min(ba[2], bb[2]) and max(ba[1], bb[1]) < min(ba[3], bb[3]))


def union_disjoint(members):
    return all(boxes_disjoint(a, b) for a, b in combinations(members, 2))


def render(members, w, h):
    return [[byte(members, x, y) for x in range(w)] for y in range(h)]


def rect(x0, y0, x1, y1):
    return [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]


def selftest_cases():
    S = 64
    cases = []
    cases.append([([rect(0, 0, 224, 256)], "nonzero"), ([rect(224, 0, 448, 256)], "nonzero")])  # abutting at x = 3.5 px
    cases.append([([rect(10, 10, 100, 100)], "nonzero"), ([rect(100, 100, 200, 190)], "nonzero")])  # a shared corner
    cases.append([([rect(0, 0, 48, 64)], "nonzero"), ([rect(48, 0, 96, 64)], "nonzero")])  # 48 and 48 units in one pixel
    cases.append([([rect(5, 5, 90, 90)], "nonzero"), ([rect(200, 5, 300, 90)], "evenodd")])
    cases.append([([rect(0, 0, 64, 64), rect(16, 16, 48, 48)], "evenodd"), ([rect(64, 0, 128, 64)], "nonzero")])
    cases.append([([[(0, 0), (64, 0), (0, 64)]], "nonzero"), ([[(64, 0), (64, 64), (0, 64)]], "nonzero")])  # two triangles of a square
    cases.append([([rect(0, 0, 100, 100)], "nonzero"), ([rect(50, 50, 150, 150)], "nonzero")])  # overlapping
    cases.append([([rect(0, 0, 64, 64)[::-1]], "nonzero"), ([rect(64, 0, 130, 64)], "nonzero")])  # opposite orientation
    cases.append([([], "nonzero"), ([rect(3, 3, 30, 30)], "nonzero")])  # an empty member
    cases.append([([rect(0, 0, 1, 24)], "nonzero"), ([rect(1, 0, 2, 24)], "nonzero")])  # 48 and 48 units: one quantization gives 3 levels
    cases.append([([rect(0, 0, 40, 64)], "nonzero"), ([rect(40, 0, 64, 64), rect(48, 16, 56, 48)], "evenodd")])  # a ring beside a nonzero rect: each member under its own rule
    cases.append([([rect(0, 0, 48, 64), rect(0, 0, 48, 64)], "nonzero"), ([rect(48, 0, 64, 64)], "nonzero")])  # a member that overlaps itself, beside another: the sum must saturate
    cases.append([([rect(0, 0, 64, 64)], "nonzero"), ([rect(64, 0, 192, 128), rect(96, 32, 160, 96)], "evenodd")])  # a nonzero rect, then a ring whose hole is the second member's own rule
    return cases


def selftest():
    bad = []
    for ci, members in enumerate(selftest_cases()):
        w = max(1, (max((box_of(m) or (0, 0, 0, 0))[2] for m in members) + 63) // 64)
        h = max(1, (max((box_of(m) or (0, 0, 0, 0))[3] for m in members) + 63) // 64)
        disjoint = union_disjoint(members)
        for y in range(h):
            for x in range(w):
                a = exact_area(members, x, y)
                lo, hi = bracket(members, x, y)
                if not (lo <= a <= hi):
                    bad.append("case %d pixel (%d,%d): exact area %s outside bracket [%s, %s]" % (ci, x, y, a, lo, hi))
                if byte(members, x, y) != byte_of(a):
                    bad.append("case %d pixel (%d,%d): byte %d != byte of the exact area %d" % (ci, x, y, byte(members, x, y), byte_of(a)))
                if disjoint and ci == 10:
                    # The inherited difference (CU2): a member whose contours overlap inside the pixel (a hole inside a pixel
                    # the outer contour only partly covers) has the saturating-sum number of AD-12, not the exact area.
                    if (x, y) == (0, 0) and sum_area(members, x, y) == a:
                        bad.append("case 10: the self-overlap difference of CU2 disappeared")
                    continue
                if disjoint:
                    b = sum_area(members, x, y)
                    if a != b:
                        bad.append("case %d pixel (%d,%d): exact area %s != sum of areas %s" % (ci, x, y, a, b))
                    if byte(members, x, y) != byte_of(b):
                        bad.append("case %d pixel (%d,%d): byte differs from the fast tier's form" % (ci, x, y))
        if ci == 0 and not disjoint:
            bad.append("case 0 (abutting): touching boxes read as overlapping")
        if ci == 6 and disjoint:
            bad.append("case 6 (overlap): read as disjoint")
    # the quantization example: 48 + 48 units are 96 units, three levels
    two = selftest_cases()[2]
    if byte(two, 0, 0) != 255:
        bad.append("two abutting halves of a pixel are not 255")
    quant = selftest_cases()[9]
    if byte(quant, 0, 0) != 3:
        bad.append("48 and 48 units are not three levels (one quantization): %d" % byte(quant, 0, 0))
    return bad


def main(argv):
    global PLANT
    if "--plant" in argv:
        PLANT = argv[argv.index("--plant") + 1]
    if "--selftest" in argv:
        bad = selftest()
        for b in bad[:5]:
            print("FAIL " + b)
        if bad:
            print("selftest: %d failure(s)%s" % (len(bad), (" (plant %s)" % PLANT) if PLANT else ""))
            return 1
        print("selftest: ok")
        return 0
    print(__doc__)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
