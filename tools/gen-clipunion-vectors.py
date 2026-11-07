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
"""Write tests/clipunion_vectors.h: the known answers of the clip union's coverage (CU1, CU2, CU16; the general tier, CG1 to CG4), from tools/clip-union-oracle.py.

One table, `kUnionCases`: a union of polygon members (integer 26.6 vertices, each member with its own fill rule), a pixel grid, the
expected clip byte of every pixel (the exact area of the union, quantized once), and whether the members' boxes are disjoint (CU16).
Touching and abutting halves, partial pixels, both rules, both orientations, an empty member, 48 and 48 units in a pixel, a
member that overlaps itself beside another, and overlapping unions (for which only the general tier applies).

The header is installed as <ghoti.io/shape-scene/testing/clipunion_vectors.h>, so it uses plain structs and arrays and nothing but
<cstddef> and <cstdint>.

    python3 tools/gen-clipunion-vectors.py                 # writes tests/clipunion_vectors.h
    python3 tools/gen-clipunion-vectors.py --check         # fails if the file on disk differs from what is written
    python3 tools/gen-clipunion-vectors.py --check --plant NAME

With `--plant` the oracle is broken on purpose (see clip-union-oracle.py) and `--check` must report a difference (status 1).
"""
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "tests", "clipunion_vectors.h")
spec = importlib.util.spec_from_file_location("clip_union_oracle", os.path.join(HERE, "clip-union-oracle.py"))
oracle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(oracle)

NAMES = oracle.CASE_NAMES


def cases():
    out = [(n, m) for n, m in zip(NAMES, oracle.selftest_cases())]
    # more partial-pixel unions on a grid, at positions that are not pixel edges
    R = oracle.rect
    out.append(("three-in-a-row", [([R(20, 20, 150, 120)], "nonzero"), ([R(150, 20, 230, 120)], "nonzero"), ([R(230, 20, 300, 120)], "evenodd")]))
    out.append(("stacked", [([R(10, 10, 90, 70)], "nonzero"), ([R(10, 70, 90, 130)], "nonzero")]))
    out.append(("diagonal-halves-of-pixels", [([[(0, 0), (128, 0), (0, 128)]], "nonzero"), ([[(128, 0), (128, 128), (0, 128)]], "nonzero")]))
    out.extend(oracle.general_cases())
    out.extend(random_overlapping_unions())
    return out


def random_overlapping_unions():
    """Seeded unions of 2 to 5 convex members (triangles and quadrilaterals on the 26.6 lattice) in a 5 by 5 pixel grid, most of them
    overlapping, so that the general tier's sweep meets edges of different members crossing at places that are not pixel edges, in many
    orders and depths: the rules of its exchange of two neighbours (the depth at the meeting, the step at the last row of a cell, the
    neighbours made new by an exchange) are on the answer's path for some pixel of some case."""
    import random
    rng = random.Random(20261107)

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])

    def hull(pts):
        pts = sorted(set(pts))
        if len(pts) < 3:
            return None
        lo = []
        for q in pts:
            while len(lo) >= 2 and cross(lo[-2], lo[-1], q) <= 0:
                lo.pop()
            lo.append(q)
        up = []
        for q in reversed(pts):
            while len(up) >= 2 and cross(up[-2], up[-1], q) <= 0:
                up.pop()
            up.append(q)
        h = lo[:-1] + up[:-1]
        return h if len(h) >= 3 else None

    out = []
    for k in range(240):
        members = []
        for _ in range(2 + rng.randrange(4)):
            while True:
                n = 3 + rng.randrange(3)
                pts = [(rng.randrange(0, 5 * 64), rng.randrange(0, 5 * 64)) for _ in range(n)]
                h = hull(pts)
                if h and abs(sum(cross(h[0], h[i], h[i + 1]) for i in range(1, len(h) - 1))) >= 64 * 64 // 4:
                    break
            members.append(([h], "nonzero"))
        out.append(("random-union-%03d" % k, members))
    return out


def grid(members):
    boxes = [oracle.box_of(m) for m in members if oracle.box_of(m)]
    w = max(1, (max(b[2] for b in boxes) + 63) // 64) if boxes else 1
    h = max(1, (max(b[3] for b in boxes) + 63) // 64) if boxes else 1
    return w, h


def emit():
    verts, contours, members_t, cases_t, expect = [], [], [], [], []
    for name, members in cases():
        w, h = grid(members)
        first_member = len(members_t)
        for contour_list, rule in members:
            first_contour = len(contours)
            for c in contour_list:
                contours.append((len(verts), len(c)))
                verts.extend(c)
            members_t.append((first_contour, len(contour_list), 1 if rule == "evenodd" else 0))
        first_expect = len(expect)
        for y in range(h):
            for x in range(w):
                # CG3: the level of the sum over the groups of each group's area (a member alone is AD-12's number, which for a
                # member that overlaps itself inside the pixel, a hole in a pixel its outer contour only partly covers, is the
                # saturating-sum area and not the exact area; a group of several is the floor of the exact area of its union).
                expect.append(oracle.byte(members, x, y))
        cases_t.append((name, w, h, first_member, len(members), first_expect, 1 if oracle.union_disjoint(members) else 0))
    L = []
    L.append("// Generated by tools/gen-clipunion-vectors.py from tools/clip-union-oracle.py; do not edit.\n")
    L.append("// The known answers of the clip union's coverage (clip_union.h, CU1, CU2, CU16).\n")
    L.append("#ifndef GHOTI_IO_GSHSCENE_CLIPUNION_VECTORS_H\n#define GHOTI_IO_GSHSCENE_CLIPUNION_VECTORS_H\n\n#include <cstddef>\n#include <cstdint>\n\n")
    L.append("namespace clipunion_vectors {\n\n")
    L.append("struct Contour { size_t first; size_t count; };\n")
    L.append("struct Member { size_t contour_first; size_t contour_count; int even_odd; };\n")
    L.append("struct Case { const char * name; int width; int height; size_t member_first; size_t member_count; size_t expect_first; int disjoint; };\n\n")
    L.append("static const int kVerts[][2] = {%s};\n" % ", ".join("{%d, %d}" % v for v in verts))
    L.append("static const Contour kContours[] = {%s};\n" % ", ".join("{%d, %d}" % c for c in contours))
    L.append("static const Member kMembers[] = {%s};\n" % ", ".join("{%d, %d, %d}" % m for m in members_t))
    L.append("static const Case kCases[] = {\n%s};\n" % "".join('    {"%s", %d, %d, %d, %d, %d, %d},\n' % c for c in cases_t))
    L.append("static const uint8_t kExpect[] = {%s};\n\n" % ", ".join(str(e) for e in expect))
    L.append("}  // namespace clipunion_vectors\n\n#endif\n")
    return "".join(L)


def main(argv):
    if "--plant" in argv:
        oracle.PLANT = argv[argv.index("--plant") + 1]
    text = emit()
    if "--check" in argv:
        try:
            have = open(OUT).read()
        except OSError:
            have = None
        if have != text:
            print("tests/clipunion_vectors.h differs from what the oracle writes")
            return 1
        print("tests/clipunion_vectors.h is current")
        return 0
    with open(OUT, "w") as f:
        f.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
