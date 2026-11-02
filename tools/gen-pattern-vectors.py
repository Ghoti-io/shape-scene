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
"""Write tests/pattern_vectors.h: the known answers of the pattern definition (PT1 to PT14), from tools/pattern-oracle.py.

Four tables:

- **Scenes** (`kScenes`): a pattern use filling a small device grid, described by plain structs (the cell, the steps, the
  repeats, the tint, the paint's alpha, the chain map, and the tile as a flat list of operations: fills of convex regions
  with a solid, a linear gradient, a NEAREST image, the drawing list's foreground or a nested pattern, groups and
  clips), the drawing list's foreground, and the expected premultiplied rgba of every pixel, the exact rational rounded
  to the nearest double.  Abutting, gapped, skewed, rotated and overlapping lattices, one ulp below the size, blend
  groups, per-copy paints, a nested pattern, clips, colored and uncolored, one axis and none, a lattice at the copy
  index 10^6.
- **Positions** (`kPositions`): the cell (left, top, right, bottom) of copy (i, j) of a pattern, the exact rational
  rounded to the nearest double.  The library's double formula is within one ulp of it, an abutting cell's right edge
  is the next cell's left edge to the bit, and the indices where `left + w` is not.
- **Overlaps** (`kOverlaps`): the predicate of PT7 on stored doubles.
- **Notices** (`kNotices`): PT10's reason (0, 23 for MAP_NOT_INVERTIBLE, 37 for PATTERN_DEGENERATE) for a chain of maps and a
  pattern's size, steps and repeats.

The header is installed as <ghoti.io/shape-scene/testing/pattern_vectors.h>, so it uses plain structs and arrays and
nothing but <cstddef> and <cstdint>.  tests/ref_pattern.h is the double-precision evaluator that reads it;
tests/unit/test_pattern_oracle.cpp checks the evaluator against it.

    python3 tools/gen-pattern-vectors.py                 # writes tests/pattern_vectors.h
    python3 tools/gen-pattern-vectors.py --check         # fails if the file on disk differs from what is written
    python3 tools/gen-pattern-vectors.py --check --plant NAME

With `--plant` the oracle is broken on purpose (see pattern-oracle.py) and `--check` must report a difference (status 1).
"""
import importlib.util
import os
import sys
from fractions import Fraction as F

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "tests", "pattern_vectors.h")
spec = importlib.util.spec_from_file_location("pattern_oracle", os.path.join(HERE, "pattern-oracle.py"))
oracle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(oracle)

KIND = {"fill": 1, "gpush": 2, "gpop": 3, "cpush": 4, "cpop": 5}


def hexd(x):
    """A double as a C++ hexadecimal floating literal; the value must be exactly a double."""
    x = F(x)
    d = float(x)
    assert F(d) == x, "%r is not a double" % (x,)
    return d.hex()


def hexs(values):
    return ", ".join(hexd(v) for v in values)


def hexr(values):
    """Exact rationals rounded to the nearest doubles (`float(Fraction)` is correctly rounded), as literals."""
    return ", ".join(float(F(v)).hex() for v in values)


class Block(object):
    """The static data of one scene, emitted dependencies first."""

    def __init__(self, prefix):
        self.prefix = prefix
        self.lines = []
        self.n = 0

    def name(self, what):
        self.n += 1
        return "%s_%s%d" % (self.prefix, what, self.n)

    def points(self, region):
        nm = self.name("pts")
        self.lines.append("static const VPoint %s[] = {%s};\n" % (nm, ", ".join("{%s, %s}" % (hexd(x), hexd(y)) for x, y in region)))
        return nm

    def pattern(self, p):
        """Emit pattern @p p (and its nested patterns first); return the C name of its VPattern."""
        ops = []
        for op in p.ops:
            k = op[0]
            kind = KIND[k]
            paint, pts, rgba, ns, stops, p0, p1, img, imap, nest, opacity, comp = 0, "nullptr", (0, 0, 0, 0), 0, "nullptr", (0, 0), (0, 0), "nullptr", (0, 0, 0, 0), "nullptr", 0, 0
            count = 0
            if k == "fill":
                region, pt = op[1], op[2]
                paint = pt[0]
                pts, count = self.points(region), len(region)
                if paint == oracle.SOLID:
                    rgba = pt[1]
                elif paint == oracle.FOREGROUND:
                    rgba = (0, 0, 0, pt[1])
                elif paint == oracle.LINEAR:
                    nm = self.name("stops")
                    self.lines.append("static const VStop %s[] = {%s};\n" % (nm, ", ".join(
                        "{%s, {%s}}" % (hexd(o), hexs(c)) for o, c in pt[3])))
                    stops, ns, p0, p1 = nm, len(pt[3]), pt[1], pt[2]
                elif paint == oracle.IMAGE:
                    nm = self.name("tex")
                    self.lines.append("static const uint8_t %s[] = {%s};\n" % (nm, ", ".join(str(v) for v in pt[3])))
                    im = self.name("img")
                    self.lines.append("static const VImage %s = {%d, %d, %s};\n" % (im, pt[1], pt[2], nm))
                    img, imap = "&" + im, pt[4]
                elif paint == oracle.PATTERN:
                    nest = "&" + self.pattern(pt[1])
            elif k == "gpush":
                opacity, comp = op[1], op[2]
            elif k == "cpush":
                pts, count = self.points(op[1]), len(op[1])
            ops.append("{%d, %d, %d, %s, {%s}, %d, %s, {%s}, {%s}, %s, {%s}, %s, %s, %d}" % (
                kind, paint, count, pts, hexs(rgba), ns, stops, hexs(p0), hexs(p1), img, hexs(imap), nest, hexd(opacity), comp))
        opsname = "nullptr"
        if ops:
            opsname = self.name("ops")
            self.lines.append("static const VOp %s[] = {\n    %s};\n" % (opsname, ",\n    ".join(ops)))
        nm = self.name("pat")
        tint = p.tint if p.tint is not None else (0, 0, 0, 0)
        self.lines.append("static const VPattern %s = {{%s}, {%s}, {%d, %d}, %d, {%s}, %s, {%s}, %d, %s};\n" % (
            nm, hexs((p.x, p.y, p.w, p.h)), hexs((p.xs, p.ys)), p.rx, p.ry, 0 if p.tint is None else 1, hexs(tint),
            hexd(p.alpha), hexs(p.map), len(ops), opsname))
        return nm


def scene_block(k, s):
    """(the static data of scene @p k, its table row)."""
    b = Block("s%02d" % k)
    pat = b.pattern(s.pat)
    pixels = oracle.render(s)
    flat = [v for row in pixels for px in row for v in px]
    pname = "s%02d_pixels" % k
    rows = []
    for y in range(s.height):
        rows.append("    " + ", ".join(hexr(px) for px in pixels[y]))
    b.lines.append("static const double %s[] = {\n%s};\n" % (pname, ",\n".join(rows)))
    text = "// %s%s\n%s\n" % (s.name, (": " + s.note) if s.note else "", "".join(b.lines))
    row = "    {\"%s\", %d, %d, {%s}, &%s, %s, %s}" % (s.name, s.width, s.height, hexs(s.fg), pat, hexd(s.tol), pname)
    return text, row, len(flat)


def position_block():
    rows = []
    for name, p, i, j in oracle.position_cases():
        want = oracle.true_cell(p, i, j)
        got = oracle.sim_cell(p, i, j) if not (oracle.PLANTS & {"add-step", "left-plus-w"}) else None
        if got is not None:      # the library's formula must be within one ulp of every expected value emitted
            for g, e in zip(got, want):
                assert oracle.ulp_distance(g, float(e)) <= 1, (name, i, j)
        edges = oracle.cell_edges(p, i, j)
        rows.append("    {\"%s\", %s, %s, %d, %d, %s}" % (name, hexs((p.x, p.y, p.w, p.h)), hexs((p.xs, p.ys)), i, j, hexr(edges)))
    return ("// The cell (left, top, right, bottom) of copy (i, j): the exact rational rounded to the nearest double.\n"
            "static const VPosition kPositions[] = {\n%s};\n\nstatic const int kPositionCount = %d;\n" % (",\n".join(rows), len(rows)))


def overlap_block():
    rows = []
    for w, h, xs, ys, rx, ry in oracle.overlap_cases():
        p = oracle.Pat((0, 0, w, h), (xs, ys), [], repeat=(rx, ry))
        rows.append("    {%s, %d, %d, %d}" % (hexs((w, h, xs, ys)), rx, ry, 1 if oracle.overlaps(p) else 0))
    return ("// PT7's predicate on stored doubles: on a repeating axis, step < size.\n"
            "static const VOverlap kOverlaps[] = {\n%s};\n\nstatic const int kOverlapCount = %d;\n" % (",\n".join(rows), len(rows)))


def notice_block():
    arrays, rows = [], []
    for k, row in enumerate(oracle.notice_cases()):
        name, maps, w, h, xs, ys, rx, ry = row
        arrays.append("static const double kNoticeMaps%d[%d][6] = {%s};\n" % (k, len(maps), ", ".join("{%s}" % hexs(m) for m in maps)))
        rows.append("    {\"%s\", %d, kNoticeMaps%d, %s, %d, %d, %d}" % (name, len(maps), k, hexs((w, h, xs, ys)), rx, ry,
                                                                           oracle.notice_of(row)))
    return ("%s\n// PT10: 0 draws, 23 MAP_NOT_INVERTIBLE (a map of the chain with a determinant of exactly 0), 37 PATTERN_DEGENERATE.\n"
            "static const VNotice kNotices[] = {\n%s};\n\nstatic const int kNoticeCount = %d;\n" % (
                "".join(arrays), ",\n".join(rows), len(rows)))


HEAD = """// Generated by tools/gen-pattern-vectors.py from tools/pattern-oracle.py (exact rationals).
// Do not edit; run the generator.  Independent of the library's code.
#ifndef GHOTI_IO_GSHSCENE_TEST_PATTERN_VECTORS_H
#define GHOTI_IO_GSHSCENE_TEST_PATTERN_VECTORS_H

#include <cstddef>
#include <cstdint>

namespace pattern_vectors {

// A point of a convex region in the tile's pattern space (y down).
struct VPoint {
  double x;
  double y;
};

// A gradient stop: offset and straight rgba.
struct VStop {
  double offset;
  double rgba[4];
};

// A NEAREST image: straight rgba8 texels, row by row, top row first.
struct VImage {
  int width;
  int height;
  const uint8_t * rgba8;
};

struct VPattern;

// One operation of a tile.  kind: 1 fill, 2 group push, 3 group pop, 4 clip push, 5 clip pop.
// fill: paint 1 solid (rgba straight), 2 linear gradient (stops, p0, p1 in the tile's pattern space; pad; straight
// interpolation), 3 NEAREST image (image; pad; tile x = image_map[0] u + image_map[2], tile y = image_map[1] v +
// image_map[3]), 4 pattern (nested; its map takes its space to the enclosing tile's space), 5 the drawing list's
// foreground (rgba[3] is the paint's alpha multiplier).  A fill and a clip push have point_count points (a convex
// region in pattern space).  group push: opacity in [0, 1] and composite 1 SRC_OVER or 2 MULTIPLY (premultiplied
// separable multiply); the group is isolated and not knockout.
struct VOp {
  int kind;
  int paint;
  int point_count;
  const VPoint * points;
  double rgba[4];
  int stop_count;
  const VStop * stops;
  double p0[2];
  double p1[2];
  const VImage * image;
  double image_map[4];
  const VPattern * nested;
  double opacity;
  int composite;
};

// A pattern: the cell {x, y, w, h}, the steps {x_step, y_step}, the repeats, the tint (straight rgba, used when
// has_tint is 1), the paint's alpha multiplier, the map (x' = m[0] x + m[1] y + m[4], y' = m[2] x + m[3] y + m[5]) from
// pattern space to the device (the root) or to the enclosing tile's space (a nested pattern), and the tile.
struct VPattern {
  double cell[4];
  double step[2];
  int repeat[2];
  int has_tint;
  double tint[4];
  double alpha;
  double map[6];
  int op_count;
  const VOp * ops;
};

// A scene: the root pattern filling a width x height device grid at coverage 1 (pixel (x, y) is [x, x + 1] x [y, y + 1],
// y down) over a transparent target, the drawing list's foreground (straight), the tolerance within which a double
// evaluator must agree, and the expected premultiplied rgba of every pixel, row by row.
struct VScene {
  const char * name;
  int width;
  int height;
  double foreground[4];
  const VPattern * pattern;
  double tolerance;
  const double * pixels;
};

// PT5: the cell {left, top, right, bottom} of the copy (i, j) of the pattern (cell, step); the exact rational rounded
// to the nearest double.
struct VPosition {
  const char * name;
  double cell[4];
  double step[2];
  int64_t i;
  int64_t j;
  double edges[4];
};

// PT7's predicate: 1 when the cells overlap.
struct VOverlap {
  double w;
  double h;
  double x_step;
  double y_step;
  int repeat_x;
  int repeat_y;
  int overlaps;
};

// PT10: the reason a fill draws nothing for a chain of maps and a pattern's size and steps: 0 none, 23, 37.
struct VNotice {
  const char * name;
  int map_count;
  const double (*maps)[6];
  double w;
  double h;
  double x_step;
  double y_step;
  int repeat_x;
  int repeat_y;
  int reason;
};

"""
TAIL = "\n}  // namespace pattern_vectors\n\n#endif\n"


def build(early_exit_against=None):
    """The file text.  With @p early_exit_against (the file on disk, under a plant) return None at the first part that is not in it."""
    def known(text):
        return early_exit_against is None or text in early_exit_against

    pos = position_block()
    if not known(pos):
        return None
    ov, no = overlap_block(), notice_block()
    if not (known(ov) and known(no)):
        return None
    scenes = oracle.catalog()
    blocks, rows, total = [], [], 0
    for k, s in enumerate(scenes):
        text, row, n = scene_block(k, s)
        if not known(text):
            return None
        blocks.append(text)
        rows.append(row)
        total += n
    head = HEAD + "// %d scenes, %d expected values; %d position cases.\n\n" % (len(scenes), total, pos.count("\n    {"))
    return "".join([head, "".join(blocks), "static const VScene kScenes[] = {\n%s};\n\nstatic const int kSceneCount = %d;\n\n" % (
        ",\n".join(rows), len(rows)), pos, "\n", ov, "\n", no, TAIL])


def main(argv):
    args = list(argv)
    plant = None
    if "--plant" in args:
        k = args.index("--plant")
        plant = args[k + 1] if k + 1 < len(args) else ""
        if plant not in oracle.PLANT_NAMES:
            sys.exit("gen-pattern-vectors: --plant takes one of: " + ", ".join(oracle.PLANT_NAMES))
        oracle.PLANTS.add(plant)
        del args[k:k + 2]
    check = args == ["--check"]
    if not check and (args or plant):
        sys.exit("usage: gen-pattern-vectors.py [--check [--plant NAME]]")
    if not check:
        with open(OUT, "w") as f:
            f.write(build())
        print("gen-pattern-vectors: wrote tests/pattern_vectors.h")
        return 0
    with open(OUT) as f:
        disk = f.read()
    text = build(disk if plant else None)
    if text is None or text != disk:
        print("gen-pattern-vectors: tests/pattern_vectors.h is not what the generator writes%s" % (
            " (plant %s)" % plant if plant else ""), file=sys.stderr)
        return 1
    print("gen-pattern-vectors: tests/pattern_vectors.h is current")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
