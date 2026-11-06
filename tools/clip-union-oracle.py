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
y down) and a fill rule ("nonzero" or "evenodd"); the region it bounds is that of the polygon **as the scan converter cuts it** (`cut_polyline`:
render.h, UC2), because every area the sink adds is the area of the pieces it deposits, whose cut points are rounded to the lattice.  A union is a list of members; the grid is W x H pixels, pixel (px, py)
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

The committed byte is `min(255, floor(min(8192, area) / 32))`.  A **disjointness** half classifies pairs of members by the
interiors of their lattice boxes (the box of the vertices of polygon members).

**The general tier (story A3b, rules CG1 to CG4; `union_units`).**  The members are partitioned into **groups**, the connected
components of the relation "the interiors of the two members' boxes meet" (CU16's overlap, the graph's edges).  The area of a pixel is
the sum over the groups, held at 8192, of the group's area: for a group of one member its own area `a_i` (the number of AD-12, the
member's accumulated winding area reduced by its rule, which for a member that does not overlap itself inside the pixel is the exact
area of its region); for a group of two or more, the **floor** of the exact area (a) of the union of the group's members' regions in
the pixel, a whole number of units.  When every group is a single member (the members' boxes are pairwise interior-disjoint) this is
(b), the fast tier's form, so the general tier equals the fast tier wherever the fast tier applies, including for a member that
overlaps itself.  A pixel's exact area (a) is a rational; the sink computes it with an error of less than (8 K + 2 E + 1) 2^-30 of a unit for K meetings and E evaluations
at slabs' ends (under 2^-12 up to 2^17 meetings), so a vector is committed only where its distance below the next whole unit is 0 or at
least 2^-12 (`margin_ok`), and the selftest asserts it.

`--selftest [--plant NAME]` runs the oracle's invariants over a family of cases (a must lie in c's bracket, and equal b where the
boxes are disjoint) and `--plant` breaks the oracle on purpose; each plant must make the selftest fail:

  sum-bytes        the coverage is the sum of the members' bytes
  touching-overlap a pair of boxes that share an edge is read as overlapping
  edge-twice       the area of a shared edge's pixel counts the abutting member's cover twice
  one-rule         the first member's rule is applied to every member
  no-saturate      the sum of areas is not held at 8192
  outer-ignores-boundary  the bracket's outer count leaves out the cells an edge meets
  one-group        every member is one group (a self-overlapping member far from the others is then made exact)
  no-groups        every member is a group of its own (an overlapping pair is then summed, counting the overlap twice)
  round-not-floor  a group's exact area is rounded to the nearest unit, ties up, instead of floored
  group-by-closed-boxes  groups by the closed boxes (boxes that touch are joined)
  dense-in-group   a group of several members takes the sum of its members' own areas, saturated, where it overlaps itself
  uncut            the edges are not cut at the pixel boundaries (the polygons are the devices paths' own, not the pieces the scan converter makes)
"""
import sys
from fractions import Fraction as F
from itertools import combinations

PLANT = None


def round_div(n, d):
    """Integer division rounding half away from zero (the scan converter's rounding of a cut)."""
    q = (abs(n) + abs(d) // 2) // abs(d)
    return -q if (n < 0) != (d < 0) else q


def cut_polyline(a, b):
    """The lattice points of the edge a -> b as the scan converter cuts it (render.h, UC2), in the direction a -> b: the edge is cut from
    its upper end (the lesser y; an edge that goes up is cut as its reverse is and then handed on reversed), at every row boundary it
    crosses, and each row's piece at every column boundary it crosses; every cut point is the rounding of the cut from the start of
    the piece it is cut from (the edge's, then the row piece's), so a slanted edge becomes a polyline of lattice points that is not
    the edge.  A horizontal edge is not cut."""
    if a[1] == b[1]:
        return [a, b]
    rev = a[1] > b[1]
    f, t = (b, a) if rev else (a, b)
    pts = [f]
    for ey in range(f[1] // 64, t[1] // 64 + 1):
        if PLANT == "uncut":
            break
        pa = f if ey == f[1] // 64 else (f[0] + round_div((ey * 64 - f[1]) * (t[0] - f[0]), t[1] - f[1]), ey * 64)
        pb = t if ey == t[1] // 64 else (f[0] + round_div(((ey + 1) * 64 - f[1]) * (t[0] - f[0]), t[1] - f[1]), (ey + 1) * 64)
        ex1, ex2 = pa[0] // 64, pb[0] // 64
        if ex1 != ex2:
            right = ex2 > ex1
            for ex in (range(ex1, ex2) if right else range(ex1, ex2, -1)):
                bx = (ex + 1) * 64 if right else ex * 64
                pts.append((bx, pa[1] + round_div((bx - pa[0]) * (pb[1] - pa[1]), pb[0] - pa[0])))
        pts.append(pb)
    if PLANT == "uncut":
        pts = [f, t]
    out = []
    for p in pts:
        if not out or out[-1] != p:
            out.append(p)
    return out[::-1] if rev else out


_EDGES = {}


def edges_of(member):
    """The member's edges as the scan converter deposits them (cut_polyline); kept, because every area asks for them again."""
    hit = _EDGES.get(id(member))
    if hit is not None and hit[0] is member and hit[1] == PLANT:
        return hit[2]
    es = _edges_of(member)
    _EDGES[id(member)] = (member, PLANT, es)
    return es


def _edges_of(member):
    es = []
    for contour in member[0]:
        n = len(contour)
        for i in range(n):
            a, b = contour[i], contour[(i + 1) % n]
            if a != b:
                pts = cut_polyline(a, b)
                for u, v in zip(pts, pts[1:]):
                    es.append((F(u[0]), F(u[1]), F(v[0]), F(v[1])))
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


_AREAS = {}


def exact_area(members, px, py):
    """(a): the exact area of the union in the pixel, in 1/8192 pixel (kept: the selftest asks for the same area several times)."""
    key = (tuple(id(m) for m in members), px, py, PLANT)
    hit = _AREAS.get(key)
    if hit is not None and all(a is b for a, b in zip(hit[0], members)):
        return hit[1]
    area = _exact_area(members, px, py)
    _AREAS[key] = (list(members), area)
    return area


def _exact_area(members, px, py):
    """(a), computed."""
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
    """The committed byte of a pixel (CG3): the level of the sum over the groups of each group's area."""
    if PLANT == "sum-bytes":
        return min(255, sum(byte_of(fold(member_area(m, px, py), m[1])) for m in members))
    return general_byte(members, px, py)


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
                cx, cy = bx0 + cell / 2 + F(1, 4096), by0 + cell / 2 + F(1, 8192)  # off the lattice, where a vertex's x would make the winding ambiguous
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


def group_of(members):
    """CG2: the groups, the connected components of the relation "the interiors of the boxes meet"; each a list of member indices, in order."""
    n = len(members)
    parent = list(range(n))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    if PLANT == "one-group":
        return [list(range(n))] if n else []
    if PLANT != "no-groups":
        for i in range(n):
            for j in range(i + 1, n):
                if PLANT == "group-by-closed-boxes":
                    bi, bj = box_of(members[i]), box_of(members[j])
                    meet = bi is not None and bj is not None and max(bi[0], bj[0]) <= min(bi[2], bj[2]) and max(bi[1], bj[1]) <= min(bi[3], bj[3])
                else:
                    meet = not boxes_disjoint(members[i], members[j])
                if meet:
                    parent[find(i)] = find(j)
    out = {}
    for i in range(n):
        out.setdefault(find(i), []).append(i)
    return [out[k] for k in sorted(out, key=lambda r: min(out[r]))]


def group_units(members, g, px, py):
    """CG3: the area of one group in the pixel, in whole units."""
    if len(g) == 1:
        m = members[g[0]]
        return fold(member_area(m, px, py), m[1])
    sub = [members[i] for i in g]
    a = exact_area(sub, px, py)
    if PLANT == "dense-in-group":
        return min(F(8192), sum(fold(member_area(m, px, py), m[1]) for m in sub))
    if PLANT == "round-not-floor":
        return min(F(8192), (a + F(1, 2)) // 1)
    return min(F(8192), a // 1)


def union_units(members, px, py):
    """CG3: the pixel's area, the sum over the groups held at 8192, in whole units (the committed byte is its level)."""
    total = F(0)
    for g in group_of(members):
        total += group_units(members, g, px, py)
    return min(total, F(8192))


def margin_ok(members, px, py):
    """The sink's error bound (under 2^-12 of a unit for up to 2^17 meetings): the exact area of a group is 0 below a whole unit or at least 2^-12 below the next."""
    for g in group_of(members):
        if len(g) > 1:
            a = exact_area([members[i] for i in g], px, py)
            gap = (-a) % 1
            if gap != 0 and gap < F(1, 4096):
                return False
    return True


def general_byte(members, px, py):
    """The byte of a union under the general tier (CG3): the level of union_units."""
    return byte_of(union_units(members, px, py))


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


def general_cases():
    """Named unions the general tier is committed against (CG3): overlapping, nested, with holes, abutting and coincident edges, a rule per
    member, boxes that touch, a member that overlaps itself beside and inside a group, and seeded random polygons."""
    R = rect
    cases = []
    cases.append(("g-overlap-one-unit", [([R(0, 0, 100, 100)], "nonzero"), ([R(99, 0, 200, 100)], "nonzero")]))
    cases.append(("g-overlap-pixel", [([R(10, 10, 120, 120)], "nonzero"), ([R(56, 10, 190, 120)], "nonzero")]))
    cases.append(("g-nested", [([R(0, 0, 192, 192)], "nonzero"), ([R(60, 60, 130, 130)], "nonzero"), ([R(150, 10, 250, 80)], "nonzero")]))
    cases.append(("g-hole-filled-partly", [([R(0, 0, 192, 192), R(48, 48, 144, 144)[::-1]], "nonzero"), ([R(100, 60, 230, 130)], "nonzero")]))
    cases.append(("g-evenodd-ring-and-nonzero", [([R(0, 0, 192, 192), R(48, 48, 144, 144)], "evenodd"), ([R(100, 60, 230, 130)], "nonzero")]))
    cases.append(("g-coincident-edges", [([R(0, 0, 100, 100)], "nonzero"), ([R(0, 20, 60, 80)], "nonzero"), ([R(30, 0, 130, 40)], "nonzero")]))
    cases.append(("g-same-region-two-rules", [([R(10, 10, 150, 150)], "nonzero"), ([R(10, 10, 150, 150)], "evenodd")]))
    cases.append(("g-crossing-slivers", [([[(0, 0), (192, 61), (192, 101), (0, 40)]], "nonzero"), ([[(0, 120), (192, 20), (192, 60), (0, 161)]], "nonzero")]))
    star = [(96, 0), (156, 186), (0, 70), (192, 70), (36, 186)]
    cases.append(("g-star-rules", [([star], "nonzero"), ([[(x + 40, y + 20) for x, y in star]], "evenodd")]))
    cases.append(("g-chain", [([R(0, 0, 100, 100)], "nonzero"), ([R(90, 0, 190, 100)], "nonzero"), ([R(180, 0, 280, 100)], "nonzero")]))
    cases.append(("g-two-groups-and-a-single", [([R(0, 0, 80, 80)], "nonzero"), ([R(50, 20, 120, 100)], "evenodd"),
                                                ([[(200, 0), (330, 90), (200, 100)]], "nonzero"), ([[(200, 70), (340, 10), (330, 100)]], "nonzero"),
                                                ([R(400, 10, 470, 90)], "nonzero")]))
    cases.append(("g-selfoverlap-singleton-beside-group", [([R(0, 0, 100, 100)], "nonzero"), ([R(60, 30, 150, 120)], "nonzero"),
                                                           ([R(256, 0, 304, 64), R(256, 0, 304, 64)], "nonzero")]))
    cases.append(("g-selfoverlap-in-group", [([R(0, 0, 40, 64), R(0, 0, 40, 64)], "nonzero"), ([R(20, 10, 60, 50)], "nonzero")]))
    cases.append(("g-touching-boxes", [([R(0, 0, 100, 100)], "nonzero"), ([R(100, 0, 200, 100)], "nonzero"), ([R(200, 0, 300, 100)], "nonzero")]))
    cases.append(("g-corner-touch-and-overlap", [([R(0, 0, 64, 64)], "nonzero"), ([R(64, 64, 128, 128)], "nonzero"), ([R(40, 40, 100, 100)], "nonzero")]))
    cases.append(("g-degenerate-members", [([], "nonzero"), ([R(30, 30, 30, 90)], "nonzero"), ([R(0, 0, 90, 90)], "nonzero"), ([R(45, 45, 130, 130)], "evenodd")]))
    cases.append(("g-many-in-one-pixel", [([[(0, 0), (60, 20), (0, 9)]], "nonzero"), ([[(0, 30), (63, 3), (63, 14)]], "nonzero"),
                                          ([[(5, 0), (30, 63), (14, 63)]], "nonzero"), ([[(60, 0), (33, 63), (49, 63)]], "nonzero"),
                                          ([[(0, 55), (63, 40), (63, 51)]], "evenodd")]))
    cases.append(("g-mixed-orientation", [([R(0, 0, 100, 100)], "nonzero"), ([R(60, 40, 170, 130)[::-1]], "nonzero")]))
    dodeca = [(48 + int(round(40 * __import__("math").cos(k * 3.14159265358979 / 6))), 48 + int(round(40 * __import__("math").sin(k * 3.14159265358979 / 6)))) for k in range(12)]
    cases.append(("g-twelve-gons", [([dodeca], "nonzero"), ([[(x + 50, y + 10) for x, y in dodeca]], "nonzero"), ([R(30, 60, 110, 100)], "nonzero")]))
    import random
    rng = random.Random(20261106)
    made = 0
    attempts = 0
    while made < 20 and attempts < 400:
        attempts += 1
        k = rng.choice([2, 2, 3, 4])
        members = []
        for _ in range(k):
            nv = rng.choice([3, 3, 4, 5, 6])
            poly = [(rng.randrange(0, 192), rng.randrange(0, 192)) for _ in range(nv)]
            members.append(([poly], rng.choice(["nonzero", "evenodd"])))
        if union_disjoint(members):
            continue
        w = max(1, (max(box_of(m)[2] for m in members if box_of(m)) + 63) // 64)
        h = max(1, (max(box_of(m)[3] for m in members if box_of(m)) + 63) // 64)
        if not all(margin_ok(members, x, y) for y in range(h) for x in range(w)):
            continue
        cases.append(("g-rand-%02d" % made, members))
        made += 1
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
                if ci not in (10, 11) and byte(members, x, y) != byte_of(a):
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
    bad.extend(selftest_general())
    # the quantization example: 48 + 48 units are 96 units, three levels
    two = selftest_cases()[2]
    if byte(two, 0, 0) != 255:
        bad.append("two abutting halves of a pixel are not 255")
    quant = selftest_cases()[9]
    if byte(quant, 0, 0) != 3:
        bad.append("48 and 48 units are not three levels (one quantization): %d" % byte(quant, 0, 0))
    return bad


SELF_OVERLAP_CASES = ("g-selfoverlap-singleton-beside-group", "g-selfoverlap-in-group")
SELF_OVERLAP_APART = "g-selfoverlap-singleton-beside-group"  # the singleton keeps its own (AD-12) number: not the exact area of the whole union


def selftest_cut():
    """The cut of an edge at the pixel boundaries (render.h, UC2), against points worked by hand."""
    bad = []
    want = {
        ((0, 0), (192, 61)): [(0, 0), (64, 20), (128, 41), (192, 61)],        # 64 * 61 / 192 = 20.33, 128 * 61 / 192 = 40.67
        ((10, 0), (40, 100)): [(10, 0), (29, 64), (40, 100)],                  # 10 + 64 * 30 / 100 = 29.2
        ((40, 100), (10, 0)): [(40, 100), (29, 64), (10, 0)],                  # the reverse is the same points, cut from the upper end
        ((0, 0), (0, 150)): [(0, 0), (0, 64), (0, 128), (0, 150)],             # a vertical edge keeps its x
        ((0, 5), (100, 5)): [(0, 5), (100, 5)],                                # a horizontal edge is not cut
        ((130, 70), (-10, 130)): [(130, 70), (128, 71), (64, 98), (0, 126), (-5, 128), (-10, 130)],  # leftward, across three columns and the row boundary y = 128 at x = -5
    }
    for (a, b), pts in want.items():
        got = cut_polyline(a, b)
        if got != pts:
            bad.append("cut_polyline(%s, %s) = %s, worked by hand %s" % (a, b, got, pts))
    return bad


def selftest_general():
    """CG2 to CG4 over the general cases."""
    bad = selftest_cut()
    seen_fraction_over_half = False
    seen_group_of_three = False
    for name, members in selftest_cases_named() + general_cases():
        boxes = [box_of(m) for m in members if box_of(m)]
        w = max(1, (max(b[2] for b in boxes) + 63) // 64) if boxes else 1
        h = max(1, (max(b[3] for b in boxes) + 63) // 64) if boxes else 1
        groups = group_of(members)
        disjoint = union_disjoint(members)
        if disjoint and any(len(g) > 1 for g in groups):
            bad.append("%s: boxes are disjoint but a group has several members" % name)
        if any(len(g) >= 3 for g in groups):
            seen_group_of_three = True
        differs = False
        for y in range(h):
            for x in range(w):
                u = union_units(members, x, y)
                if u != int(u):
                    bad.append("%s pixel (%d,%d): the area %s is not a whole unit" % (name, x, y, u))
                if not margin_ok(members, x, y):
                    bad.append("%s pixel (%d,%d): the exact area is within 2^-12 below a whole unit" % (name, x, y))
                if disjoint and u != sum_area(members, x, y):
                    bad.append("%s pixel (%d,%d): the general tier's area %s differs from the fast tier's form %s" % (name, x, y, u, sum_area(members, x, y)))
                whole = min(F(8192), exact_area(members, x, y) // 1)
                if u != whole:
                    differs = True
                    if name not in SELF_OVERLAP_CASES and name != "ring-and-rect" and name != "self-overlap-beside":
                        bad.append("%s pixel (%d,%d): area %s differs from the floor of the exact area of the whole union %s" % (name, x, y, u, whole))
                for g in groups:
                    if len(g) > 1:
                        a = exact_area([members[i] for i in g], x, y)
                        lo, hi = bracket([members[i] for i in g], x, y, 4)
                        if not (lo <= a <= hi):
                            bad.append("%s pixel (%d,%d): a group's exact area %s outside its bracket [%s, %s]" % (name, x, y, a, lo, hi))
                        if a - (a // 1) >= F(1, 2):
                            seen_fraction_over_half = True
        if name == SELF_OVERLAP_APART and not differs:
            bad.append("%s: a self-overlapping member apart from the group left no difference from the exact area of the whole union" % name)
        if name == "g-selfoverlap-in-group":
            ins = [members[i] for i in groups[0]]
            if all(union_units(members, x, y) == min(F(8192), sum(fold(member_area(m, x, y), m[1]) for m in ins)) for y in range(h) for x in range(w)):
                bad.append("%s: a self-overlapping member in a group left no difference from the sum of the members' own areas" % name)
    if not seen_fraction_over_half:
        bad.append("no vector has a group's exact area with a fraction of a half or more: floor and rounding are not told apart")
    if not seen_group_of_three:
        bad.append("no vector has a group of three or more members")
    return bad


CASE_NAMES = ["abutting-at-3.5", "shared-corner", "48-and-48-units", "apart-mixed-rules", "ring-beside-rect", "two-triangles",
              "overlapping", "opposite-orientation", "empty-member", "one-quantization", "ring-and-rect", "self-overlap-beside", "rect-then-ring"]


def selftest_cases_named():
    return list(zip(CASE_NAMES, selftest_cases()))


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
