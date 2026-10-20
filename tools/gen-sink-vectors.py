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
"""Print the known answers of the sink contract's spellings (story 13a; S5, AD-21).

A second statement, in Python and none of the library's code, of what
report.h and dump_sink.h say: the report's hash and dump, the test sink's dump
text and output hash, its marks and stamps (so the pixels of a raster target
too), for the fixture below, which tests/unit/test_dump_sink.cpp builds with the
API.  It also states what the render must produce (the entries, their order,
the summary): the generator is the expectation, not a transcription of the
library's output.  The resource, list and map spellings come from
tools/gen-scene-vectors.py, itself independent of the library.

    python3 tools/gen-sink-vectors.py > tests/sink_answers.h
    python3 tools/gen-sink-vectors.py --check      # fails if the file differs

The fixture list (list space, y down), every fill solid and nonzero:

     0  fill  square (2, 2) size 8, red
     1  clip  square (4, 4) size 20
     2  fill  square (0, 0) size 30, blue at alpha 0.5
     3  clip pop
     4  group opacity 0.5, linear sRGB, depth ordering, source-over
     5  fill  square (10, 10) size 6, red, depth 1
     6  fill  square (12, 12) size 6, blue, depth 1        (a tie with 5, by declared order)
     7  group pop
     8  instance of P, translated by (30, 30)
          P: 0 fill square (0, 0) size 10, red; 1 fill square (2, 2) size 4, blue
     9  fill  square (40, 2) size 6, red
    10  group opacity 0, flat, source-over, the target's space (empty)
    11  group pop
    12  fill  square (-6, -6) size 6, red     (its box ends at 0, outside the raster: no stamp)

The renders: the full test sink on a 64 by 64 RGBA8 raster filled with 0x5a
(identity map; its dump goes to the log), the full test sink on a stream in
millimetres with the map `scale(0.5)`, the narrow sink (no antialiased clips, no
blend spaces) on the raster, and the full sink with `max_extent` 7 under strict
on the raster (the first fill is refused while drawing).
"""
import importlib.util
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location("scene_vectors", os.path.join(HERE, "gen-scene-vectors.py"))
sv = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(sv)

fnv, u8, u32, u64, f64, bits = sv.fnv, sv.u8, sv.u32, sv.u64, sv.f64, sv.bits

# --- maps: 2D affine, composed outer . inner with ((a0 b0 + a1 b1) + a2 b2) -------------------------------
TRANSLATES, SCALES = 1, 2


def ident():
    return ([1.0, 0.0, 0.0, 1.0], [0.0, 0.0], 0)


def translate(x, y):
    return ([1.0, 0.0, 0.0, 1.0], [float(x), float(y)], TRANSLATES if (x, y) != (0, 0) else 0)


def scale(s):
    return ([float(s), 0.0, 0.0, float(s)], [0.0, 0.0], SCALES if s != 1 else 0)


def compose(outer, inner):
    a, ta, ka = outer
    b, tb, kb = inner
    # 3 by 3 with the third row and column of the identity for a 2D map: the third terms are 0 * x and add +0.
    m = [a[0] * b[0] + a[1] * b[2] + 0.0, a[0] * b[1] + a[1] * b[3] + 0.0,
         a[2] * b[0] + a[3] * b[2] + 0.0, a[2] * b[1] + a[3] * b[3] + 0.0]
    t = [a[0] * tb[0] + a[1] * tb[1] + ta[0], a[2] * tb[0] + a[3] * tb[1] + ta[1]]
    return (m, t, ka | kb)


def chain(maps, target):
    acc = maps[0]
    for m in maps[1:]:
        acc = compose(acc, m)
    return compose(target, acc)


def mhash(m):
    return sv.map_hash(2, m[0], m[1], m[2])


def apply(m, x, y):
    a, t, _ = m
    return (a[0] * x + a[1] * y + t[0], a[2] * x + a[3] * y + t[1])


# --- the fixture -----------------------------------------------------------------------------------------
RED = (1.0, 0.0, 0.0, 1.0)
BLUE = (0.0, 0.0, 1.0, 0.5)
LINEAR_SRGB = (1, 1, 0.0)  # GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_LINEAR
GROUP_OPACITY = 0.5


def sq(x, y, s):
    return 2, [("M", [(x, y)], None), ("L", [(x + s, y)], None), ("L", [(x + s, y + s)], None),
               ("L", [(x, y + s)], None), ("Z", [], None)]


def sq_box(x, y, s):
    return (float(x), float(y), float(x + s), float(y + s))


def rhash(x, y, s):
    return sv.path_resource_hash(sq(x, y, s))


ID = mhash(ident())
PICTURE_ITEMS = [
    sv.fill(sv.KIND_PATH, rhash(0, 0, 10), ID, 0, RED),
    sv.fill(sv.KIND_PATH, rhash(2, 2, 4), ID, 0, BLUE),
]
PICTURE_HASH = fnv(u32(1) + u32(sv.KIND_PICTURE) + u64(sv.list_hash(PICTURE_ITEMS)))
MOVE = translate(30, 30)
LIST_ITEMS = [
    sv.fill(sv.KIND_PATH, rhash(2, 2, 8), ID, 0, RED),
    sv.clip_push(sv.KIND_PATH, rhash(4, 4, 20), ID, 0),
    sv.fill(sv.KIND_PATH, rhash(0, 0, 30), ID, 0, BLUE),
    sv.CLIP_POP_ITEM,
    sv.group_push(GROUP_OPACITY, LINEAR_SRGB, 0, sv.ORDER_DEPTH),
    sv.fill(sv.KIND_PATH, rhash(10, 10, 6), ID, 0, RED, depth=1.0),
    sv.fill(sv.KIND_PATH, rhash(12, 12, 6), ID, 0, BLUE, depth=1.0),
    sv.GROUP_POP_ITEM,
    sv.instance(sv.KIND_PICTURE, PICTURE_HASH, mhash(MOVE)),
    sv.fill(sv.KIND_PATH, rhash(40, 2, 6), ID, 0, RED),
    sv.group_push(0.0),
    sv.GROUP_POP_ITEM,
    sv.fill(sv.KIND_PATH, rhash(-6, -6, 6), ID, 0, RED),
]
LIST_HASH = sv.list_hash(LIST_ITEMS)

# The units in paint order: (chain, letter, square or None, the instance maps above it).
UNITS = [
    ((0,), "F", (2, 2, 8), []), ((1,), "C", (4, 4, 20), []), ((2,), "F", (0, 0, 30), []), ((3,), "c", None, []),
    ((4,), "G", None, []), ((5,), "F", (10, 10, 6), []), ((6,), "F", (12, 12, 6), []), ((7,), "g", None, []),
    ((8,), "I", None, []), ((8, 0), "F", (0, 0, 10), [MOVE]), ((8, 1), "F", (2, 2, 4), [MOVE]),
    ((9,), "F", (40, 2, 6), []), ((10,), "G0", None, []), ((11,), "g", None, []), ((12,), "F", (-6, -6, 6), []),
]
UNITS_TOTAL = len(UNITS)
assert UNITS_TOTAL == 15

KIND_RASTER, KIND_STREAM = 1, 2
UNIT_DEFAULT, UNIT_MM = 0, 2


def chain_text(c):
    return ".".join(str(i) for i in c)


def mark(c):
    return fnv(u64(len(c)) + b"".join(u64(i) for i in c)) & 0xFFFFFFFF


def floor(x):
    import math
    return int(math.floor(x))


def ceil(x):
    import math
    return int(math.ceil(x))


def meet(a, b):
    return (max(a[0], b[0]), max(a[1], b[1]), min(a[2], b[2]), min(a[3], b[3]))


def join(a, b):
    return (min(a[0], b[0]), min(a[1], b[1]), max(a[2], b[2]), max(a[3], b[3]))


def empty(r):
    return r[0] >= r[2] or r[1] >= r[3]


def render(kind, unit, target_map, area=None):
    """The test sink's dump (dump_sink.h) of the fixture, and the stamps it makes on a pixel target."""
    lines = ["gshscene.dump-sink 1 kind=%d unit=%d units=%d list=%016x" % (kind, unit, UNITS_TOTAL, LIST_HASH)]
    stamps = []
    blocks = []  # (group, clip, outer, gathered or None, mark)

    def clip_now():
        return blocks[-1][1] if blocks else area

    def innermost_group():
        for i in range(len(blocks) - 1, -1, -1):
            if blocks[i][0]:
                return i
        return None

    def stamp(r, m):
        r = meet(r, area)
        if empty(r):
            return
        stamps.append((r, m))
        lines.append("S %d %d %d %d %08x" % (r[0], r[1], r[2], r[3], m))

    for c, letter, square, inst_maps in UNITS:
        head = "U %s" % chain_text(c)
        if letter in "FC":
            x, y, s = square
            composed = chain(inst_maps + [ident()], target_map)
            b = sq_box(x, y, s)
            corners = [apply(composed, px, py) for px in (b[0], b[2]) for py in (b[1], b[3])]
            box = (min(p[0] for p in corners), min(p[1] for p in corners),
                   max(p[0] for p in corners), max(p[1] for p in corners))
            lines.append("%s %s res=path:%016x map=%016x rule=0 box=%s" % (
                head, letter, rhash(x, y, s), mhash(composed), " ".join("%016x" % bits(v) for v in box)))
            if area is None:
                if letter == "C":
                    blocks.append((False, None, None, None, 0))
                continue
            r = (floor(box[0]), floor(box[1]), ceil(box[2]), ceil(box[3]))
            if letter == "F":
                r = meet(r, clip_now())
                g = innermost_group()
                if g is None:
                    stamp(r, mark(c))
                elif not empty(r):
                    grp = blocks[g]
                    blocks[g] = (grp[0], grp[1], grp[2], r if grp[3] is None else join(grp[3], r), grp[4])
            else:
                blocks.append((False, meet(clip_now(), r), None, None, 0))
        elif letter in ("G", "G0"):
            lines.append("%s G opacity=%016x composite=0" % (head, bits(GROUP_OPACITY if letter == "G" else 0.0)))
            blocks.append((True, area, clip_now(), None, mark(c)))
        elif letter in "cg":
            lines.append("%s %s" % (head, letter))
            b = blocks.pop()
            if area is not None and b[0] and b[3] is not None:
                r = meet(b[3], b[2])
                g = innermost_group()
                if g is None:
                    stamp(r, b[4])
                elif not empty(r):
                    grp = blocks[g]
                    blocks[g] = (grp[0], grp[1], grp[2], r if grp[3] is None else join(grp[3], r), grp[4])
        elif letter == "I":
            lines.append("%s I res=picture:%016x" % (head, PICTURE_HASH))
    lines.append("E result=0 processed=%d done=%d" % (UNITS_TOTAL, UNITS_TOTAL))
    text = "\n".join(lines) + "\n"
    return text, fnv(text.encode()), stamps


RASTER_TEXT, RASTER_HASH, RASTER_STAMPS = render(KIND_RASTER, UNIT_DEFAULT, ident(), (0, 0, 64, 64))
STREAM_TEXT, STREAM_HASH, _ = render(KIND_STREAM, UNIT_MM, scale(0.5))


def pixels_hash(stamps, w=64, h=64):
    px = bytearray([0x5A] * (w * h * 4))
    for (x0, y0, x1, y1), m in stamps:
        b = struct.pack("<I", m)
        for y in range(y0, y1):
            for x in range(x0, x1):
                o = (y * w + x) * 4
                px[o:o + 4] = b
    return fnv(bytes(px))


RASTER_PIXELS = pixels_hash(RASTER_STAMPS)

# --- reports (report.h) ------------------------------------------------------------------------------------
REFUSED, APPROXIMATED, AMBIGUITY, HINT = 1, 2, 3, 4
R_BLEND, R_CLIP, R_LIMIT, R_TIE_DECLARED = 4, 5, 6, 11
OK, ERR_REFUSED = 0, 9


def report(result, units, entries, static=0, work=1, failure_chain=(), reason="", frame="%FRAME%",
           workspace="%WORKSPACE%"):
    """entries: (class, reason, chain, second, depth), in the order report.h says."""
    keyed = sorted(entries, key=lambda e: (list(e[2]), e[0], e[1], e[3]))
    assert keyed == list(entries), "the expectation itself is out of order"
    by = [0, 0, 0, 0, 0]
    for e in entries:
        by[e[0]] += 1
    out = u32(1) + u32(0x54525052) + u32(result) + u64(units[0]) + u64(units[1]) + u64(units[2])
    out += u64(len(entries)) + u8(0)
    for c in range(1, 5):
        out += u64(by[c])
    out += u64(static) + u64(work)
    fc = failure_chain if result != OK else ()
    out += u32(result) + u64(len(fc)) + b"".join(u64(i) for i in fc)
    out += u64(len(entries))
    for cls, rsn, ch, second, depth in entries:
        out += u8(cls) + u32(rsn) + u64(len(ch)) + b"".join(u64(i) for i in ch) + u64(second) + f64(depth)
    h = fnv(out)
    lines = ["gshscene.report 1 result=%d units=%d/%d/%d entries=%d counted=%d overflowed=0 static=%d frame=%s "
             "workspace=%s work=%d hash=%016x" % (result, units[0], units[1], units[2], len(entries), len(entries), static,
                                                  frame, workspace, work, h)]
    if result != OK:
        lines.append("failure result=%d chain=%s reason=%s" % (result, chain_text(fc) if fc else "-", reason))
    for cls, rsn, ch, second, depth in entries:
        lines.append("E class=%d reason=%d chain=%s second=%d depth=%016x" % (
            cls, rsn, chain_text(ch) if ch else "-", second, bits(depth)))
    return h, "\n".join(lines) + "\n"


TIE = (AMBIGUITY, R_TIE_DECLARED, (5,), 6, 1.0)
NARROW_HASH, NARROW_DUMP = report(OK, (15, 15, 15), [
    (APPROXIMATED, R_CLIP, (1,), 0, 0.0),
    (APPROXIMATED, R_BLEND, (4,), 0, 0.0),
    TIE,
])
STRICT_HASH, STRICT_DUMP = report(ERR_REFUSED, (15, 0, 0), [
    (REFUSED, R_LIMIT, (0,), 0, 0.0),
    TIE,
], work=1, failure_chain=(0,), reason="strict: refused while drawing (the sink's resource limit)")


def cxx_string(name, text):
    return sv.cxx_string(name, text)


def render_header():
    o = []
    o.append("// Generated by tools/gen-sink-vectors.py from the spellings in report.h and dump_sink.h.\n")
    o.append("// Do not edit; run the generator.  Independent of the library's code.\n")
    o.append("#ifndef GHOTI_IO_GSHSCENE_TEST_SINK_ANSWERS_H\n#define GHOTI_IO_GSHSCENE_TEST_SINK_ANSWERS_H\n\n")
    o.append("#include <cstdint>\n\nnamespace sink_known {\n\n")
    for name, v in (("kListHash", LIST_HASH), ("kPictureHash", PICTURE_HASH), ("kRasterOutputHash", RASTER_HASH),
                    ("kRasterPixelsHash", RASTER_PIXELS), ("kStreamOutputHash", STREAM_HASH),
                    ("kNarrowReportHash", NARROW_HASH), ("kStrictReportHash", STRICT_HASH)):
        o.append("static const std::uint64_t %s = UINT64_C(0x%016x);\n" % (name, v))
    o.append("\n")
    o.append(cxx_string("kRasterDump", RASTER_TEXT))
    o.append(cxx_string("kStreamDump", STREAM_TEXT))
    o.append(cxx_string("kNarrowReportDump", NARROW_DUMP))
    o.append(cxx_string("kStrictReportDump", STRICT_DUMP))
    o.append("\n}  // namespace sink_known\n\n#endif\n")
    return "".join(o)


if __name__ == "__main__":
    text = render_header()
    if len(sys.argv) > 1 and sys.argv[1] == "--check":
        path = os.path.join(HERE, "..", "tests", "sink_answers.h")
        with open(path) as f:
            if f.read() != text:
                sys.exit("gen-sink-vectors: tests/sink_answers.h is not what the generator writes")
        print("gen-sink-vectors: tests/sink_answers.h is current")
    else:
        sys.stdout.write(text)
