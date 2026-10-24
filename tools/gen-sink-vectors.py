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

Since story 14a, a second fixture: two fills of one run from a face of the test provider (glyph_test_provider.h,
restated below: 2048 units, a program, mode FONT, size 16, glyphs 1, 2, 4 (corrupt) and 3), the first under the
identity and the second under a quarter turn moved by (60, 0), rendered on the raster (the first takes the fast tier
at 16 ppem, the second the general route with a run-level entry) and on the stream (both general).  The generator
restates glyph.h's route, the snapped scale, gshgeom_scale_26_6, the test provider's toy hinter, the device path's and
the model path's hashes and the composition of the general route's map.

Since color-paint story P2, a third fixture: four gradient fills on the stream (two of one linear gradient with a
solid, a foreground and a palette stop, interpolated in linear sRGB; a radial one in the target's space; and a
degenerate one, a notice), and the test sink's gradient records restated: each fill's ` grad=<id>`, and once per
gradient its `R` line and the `r` line of each stop as the one resolve gives it.

Since strokes story S1, a fourth fixture: five strokes of squares (a default style; a device width with round joins
and caps; the default again, whose style's record is not written twice; width 0, a notice; a beveled, square-capped,
dashed, inside-aligned style), rendered by the test sink in its three configurations (every feature native, a
writer's set with STROKE_EXPAND, a toolpath's set without it), on the stream and, natively, on the raster.  SK10's box
and SK11's required features are restated from stroke.h's text; the outward rounding is exact rational arithmetic.
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
REFUSED, APPROXIMATED, AMBIGUITY, HINT, NOTICE = 1, 2, 3, 4, 5
R_BLEND, R_CLIP, R_LIMIT, R_TIE_DECLARED = 4, 5, 6, 11
OK, ERR_REFUSED = 0, 9


def report(result, units, entries, static=0, work=1, failure_chain=(), reason="", frame="%FRAME%",
           workspace="%WORKSPACE%"):
    """entries: (class, reason, chain, second, depth), in the order report.h says."""
    keyed = sorted(entries, key=lambda e: (list(e[2]), e[0], e[1], e[3]))
    assert keyed == list(entries), "the expectation itself is out of order"
    by = [0, 0, 0, 0, 0, 0]
    for e in entries:
        by[e[0]] += 1
    out = u32(1) + u32(0x54525052) + u32(result) + u64(units[0]) + u64(units[1]) + u64(units[2])
    out += u64(len(entries)) + u8(0)
    for c in range(1, 6):
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
], work=1, failure_chain=(0,), reason="strict: refused while drawing (the sink's resource limit, or a glyph)")


# --- runs from a font (story 14a): glyph.h's route and the test sink's glyph records ---------------------------
#
# The test provider (tests/glyph_test_provider.h), restated: its glyphs in 26.6 design units, y up.
def U(v):
    return v * 64


TP_RECT = [("M", [(U(3), U(3))]), ("L", [(U(1021), U(3))]), ("L", [(U(1021), U(1501))]), ("L", [(U(3), U(1501))]),
           ("Z", []), ("M", [(U(301), U(301))]), ("L", [(U(301), U(1203))]), ("L", [(U(723), U(1203))]),
           ("L", [(U(723), U(301))]), ("Z", [])]
TP_RING = [("M", [(U(1023), U(0))]), ("Q", [(U(1771), U(1)), (U(1771), U(751))]),
           ("Q", [(U(1771), U(1501)), (U(1023), U(1501))]), ("Q", [(U(275) + 33, U(1501)), (U(275), U(751) - 7)]),
           ("Q", [(U(275), U(1)), (U(1023), U(0))]), ("Z", []), ("M", [(U(1023), U(301))]),
           ("Q", [(U(577), U(301)), (U(577), U(751))]), ("Q", [(U(577), U(1201)), (U(1023), U(1201))]),
           ("Q", [(U(1469), U(1201)), (U(1469), U(751))]), ("Q", [(U(1469), U(301)), (U(1023), U(301))]), ("Z", [])]
TP_CURL = [("M", [(U(-101) + 5, U(-203) + 17)]),
           ("C", [(U(400) + 1, U(-480)), (U(1301), U(97) + 63), (U(1001), U(905))]),
           ("C", [(U(803), U(1499) + 31), (U(-37), U(1201)), (U(13) - 9, U(611) + 3)]),
           ("L", [(U(511), U(609))]), ("Z", [])]
TP_GLYPHS = {1: TP_RECT, 2: TP_RING, 3: TP_CURL, 5: TP_RECT, 6: TP_RECT, 7: TP_RECT, 0: []}
TP_FAILS = {4}  # corrupt: every op fails


def scale_26_6(p, S, flip):
    v = -p * S if flip else p * S
    return (v + 32768) // 65536  # Python's // is a floor


def glyph_scale(ppem, upem):
    return (ppem * 131072 + upem) // (2 * upem)


def round_26_6(x):
    import math
    return int(math.floor(x * 64 + 0.5))


DEVICE_VERB = {"M": 0, "L": 1, "Q": 2, "C": 3, "Z": 4}


def device_path_hash(els):
    out = u32(1) + u32(0x50564544) + u64(len(els))
    for verb, pts in els:
        out += u8(DEVICE_VERB[verb])
        for x, y in pts:
            out += struct.pack("<i", x) + struct.pack("<i", y)
    return fnv(out)


def fast_tier(gid, S, origin, hint):
    """The provider's device path of glyph gid with the whole pixel added (glyph_provider.h, the test provider)."""
    ox, oy = origin
    px, py = ox // 64, oy // 64
    fx, fy = ox - 64 * px, oy - 64 * py
    els = []
    for verb, pts in TP_GLYPHS[gid]:
        out = []
        for x, y in pts:
            ax = fx + scale_26_6(x, S, 0)
            ay = fy + scale_26_6(y, S, 1)
            if hint:
                ax = (ax + 32) - (ax + 32) % 64
                ay = (ay + 32) - (ay + 32) % 64
            out.append((ax + 64 * px, ay + 64 * py))
        els.append((verb, out))
    return device_path_hash(els)


def outline_hash(gid):
    els = [(verb, [(x / 64.0, y / 64.0) for x, y in pts], None) for verb, pts in TP_GLYPHS[gid]]
    return sv.path_hash((2, els))


def glyph_box(gid):
    pts = [p for _, ps in TP_GLYPHS[gid] for p in ps]
    if not pts:
        return None
    return (min(p[0] for p in pts), min(p[1] for p in pts), max(p[0] for p in pts), max(p[1] for p in pts))


FLIPS_Y, QUARTER_TURNS = 8, 32


def design(s):
    flip = ([1.0, 0.0, 0.0, -1.0], [0.0, 0.0], FLIPS_Y)
    return compose(compose(ident(), scale(s)), flip)


GLYPH_UPEM, GLYPH_SIZE, GLYPH_MODE = 2048, 16.0, 0  # mode FONT; the face has a program
FACE_KEY = 0x51
FACE_HASH = sv.face_hash(FACE_KEY, 0, [])
GLYPHS = [(1, 2.0, 30.0), (2, 13.25, 30.5), (4, 24.0, 30.0), (3, 30.0, 30.0)]
RUN_HASH = sv.run_hash(0, FACE_HASH, GLYPH_SIZE, GLYPH_MODE, 0, [(g, x, y, 0) for g, x, y in GLYPHS])
QUARTER = compose(translate(60, 0), ([0.0, -1.0, 1.0, 0.0], [0.0, 0.0], QUARTER_TURNS))
GLYPH_ITEMS = [
    sv.fill(sv.KIND_RUN, RUN_HASH, ID, 0, RED),
    sv.fill(sv.KIND_RUN, RUN_HASH, mhash(QUARTER), 0, RED),
]
GLYPH_LIST_HASH = sv.list_hash(GLYPH_ITEMS)
R_HINT_ROUTE, R_UNAVAILABLE = 12, 18


def glyph_render(kind, unit, target_map, area=None):
    """The test sink's dump of the glyph fixture: each item's route, box and glyph records."""
    lines = ["gshscene.dump-sink 1 kind=%d unit=%d units=2 list=%016x" % (kind, unit, GLYPH_LIST_HASH)]
    stamps = []
    entries = []
    for index, item_map in enumerate((ident(), QUARTER)):
        c = (index,)
        composed = chain([item_map], target_map)
        a, t, _ = composed
        # The route (glyph.h): pixels, the predicate (no rotation, a uniform positive scale, no flip), an integer ppem.
        fast = (area is not None and a[1] == 0.0 and a[2] == 0.0 and a[0] == a[3] and a[0] > 0
                and GLYPH_SIZE * a[0] == int(GLYPH_SIZE * a[0]))
        if area is not None and not fast:
            entries.append((HINT, R_HINT_ROUTE, c, 0, 0.0))
        # The box: each glyph's design box placed by its position at size / upem, through the composed map.
        sc = GLYPH_SIZE / GLYPH_UPEM / 64.0
        boxes = []
        for gid, x, y in GLYPHS:
            b = glyph_box(gid) if gid not in TP_FAILS else None
            if b:
                boxes.append((x + sc * b[0], y - sc * b[3], x + sc * b[2], y - sc * b[1]))
        mb = (min(b[0] for b in boxes), min(b[1] for b in boxes), max(b[2] for b in boxes), max(b[3] for b in boxes))
        corners = [apply(composed, px, py) for px in (mb[0], mb[2]) for py in (mb[1], mb[3])]
        box = (min(p[0] for p in corners), min(p[1] for p in corners), max(p[0] for p in corners),
               max(p[1] for p in corners))
        lines.append("U %s F res=glyph-run:%016x map=%016x rule=0 box=%s" % (
            chain_text(c), RUN_HASH, mhash(composed), " ".join("%016x" % bits(v) for v in box)))
        S = glyph_scale(int(GLYPH_SIZE * a[0]), GLYPH_UPEM) if fast else 0
        for i, (gid, x, y) in enumerate(GLYPHS):
            if gid in TP_FAILS:
                lines.append("Y %d x reason=%d" % (i, R_UNAVAILABLE))
                entries.append((REFUSED, R_UNAVAILABLE, c, i, 0.0))
                continue
            if fast:
                ox, oy = apply(composed, x, y)
                h = fast_tier(gid, S, (round_26_6(ox), round_26_6(oy)), hint=True)
                lines.append("Y %d f %016x status=0" % (i, h))
            else:
                gmap = compose(compose(composed, translate(x, y)), design(GLYPH_SIZE / GLYPH_UPEM))
                h = fnv(u64(outline_hash(gid)) + u64(mhash(gmap)))
                lines.append("Y %d g %016x status=0" % (i, h))
        if area is not None:
            r = meet((floor(box[0]), floor(box[1]), ceil(box[2]), ceil(box[3])), area)
            if not empty(r):
                stamps.append((r, mark(c)))
                lines.append("S %d %d %d %d %08x" % (r[0], r[1], r[2], r[3], mark(c)))
    lines.append("E result=0 processed=2 done=2")
    text = "\n".join(lines) + "\n"
    entries.sort(key=lambda e: (list(e[2]), e[0], e[1], e[3]))
    return text, fnv(text.encode()), stamps, entries


GLYPH_RASTER_TEXT, GLYPH_RASTER_HASH, GLYPH_RASTER_STAMPS, GLYPH_RASTER_ENTRIES = glyph_render(
    KIND_RASTER, UNIT_DEFAULT, ident(), (0, 0, 64, 64))
GLYPH_STREAM_TEXT, GLYPH_STREAM_HASH, _, GLYPH_STREAM_ENTRIES = glyph_render(KIND_STREAM, UNIT_MM, scale(0.5))
GLYPH_RASTER_PIXELS = pixels_hash(GLYPH_RASTER_STAMPS)
GLYPH_RASTER_REPORT_HASH, GLYPH_RASTER_REPORT_DUMP = report(OK, (2, 2, 2), GLYPH_RASTER_ENTRIES)
GLYPH_STREAM_REPORT_HASH, _ = report(OK, (2, 2, 2), GLYPH_STREAM_ENTRIES)


# --- the gradient fixture (color-paint story P2) --------------------------------------------------------------
# The test sink's gradient records (dump_sink.h: ` grad=<id>` in a fill's record, then once per gradient per render
# `R <id> <hash> space=<space> stops=N` and an `r` line per stop as gshscene_gradient_resolve gives it), restated.
# The test interns, into a fresh store and in this order, the palette (id 1), gradients A, B and D (ids 2, 3, 4)
# and then the squares.  Every conversion here is exact by N2's text: a stop in the interpolation space is copied,
# and an sRGB value at or below 0.04045 decodes to x / 12.92, one division, with no power and no matrix.
SRGB_SRGB = (1, 2, 0.0)
GRAD_PALETTE = [((0.03125, 0.0, 0.0390625, 1.0), SRGB_SRGB)]
GRAD_FG = ((0.02, 0.03125, 0.0, 1.0), SRGB_SRGB)
GRAD_A = sv.gradient(sv.LINEAR, (0.0, 0.0, 10.0, 0.0, 0.0, 0.0), ID,
                     [sv.stop(0.0, sv.PAINT_SOLID, (0.25, 0.5, 1.0, 1.0), LINEAR_SRGB),
                      sv.stop(0.5, sv.PAINT_FOREGROUND, (0.0, 0.0, 0.0, 0.5)),
                      sv.stop(1.0, sv.PAINT_PALETTE, (0.0, 0.0, 0.0, 0.75), entry=0)], space=LINEAR_SRGB)
GRAD_B = sv.gradient(sv.RADIAL, (1.0, 1.0, 0.0, 1.0, 1.0, 5.0), ID,
                     [sv.stop(0.0, sv.PAINT_SOLID, (1.0, 0.0, 0.0, 1.0)), sv.stop(1.0, sv.PAINT_SOLID, (0.0, 0.0, 1.0, 0.5))])
GRAD_D = sv.gradient(sv.LINEAR, (5.0, 5.0, 5.0, 5.0, 0.0, 0.0), ID,
                     [sv.stop(0.0, sv.PAINT_SOLID, (1.0, 0.0, 0.0, 1.0)), sv.stop(1.0, sv.PAINT_SOLID, (0.0, 0.0, 1.0, 1.0))])
GRAD_IDS = {"A": 2, "B": 3, "D": 4}
GRAD_HASH = {"A": sv.gradient_hash(GRAD_A), "B": sv.gradient_hash(GRAD_B), "D": sv.gradient_hash(GRAD_D)}
GRAD_SQUARES = [(2, 2, 8, "A", 1.0), (10, 4, 6, "A", 0.5), (20, 20, 6, "B", 1.0), (30, 2, 4, "D", 1.0)]
GRAD_LIST_ITEMS = [sv.fill(sv.KIND_PATH, rhash(x, y, sz), ID, 0, (0.0, 0.0, 0.0, a), paint=sv.PAINT_GRADIENT,
                           grad=GRAD_HASH[g]) for x, y, sz, g, a in GRAD_SQUARES]
GRAD_LIST_HASH = sv.list_hash(GRAD_LIST_ITEMS, GRAD_FG, sv.palette_hash(GRAD_PALETTE))
R_GRADIENT_DEGENERATE = 24


def decode_srgb_low(x):
    """N2's sRGB decode of an encoded value at or below its knee, 0.04045: x / 12.92."""
    assert x <= 0.04045
    return x / 12.92


def resolved(g, target_space):
    """G3 for the two gradients above: (interpolation space, [(offset, r, g, b, a)])."""
    into = target_space if g["space"] == sv.TARGET else g["space"]
    out = []
    for offset, kind, rgba, sp, entry in g["stops"]:
        if kind == sv.PAINT_SOLID:
            color, space = rgba, sp
        elif kind == sv.PAINT_FOREGROUND:
            color, space = (GRAD_FG[0][0], GRAD_FG[0][1], GRAD_FG[0][2], GRAD_FG[0][3] * rgba[3]), GRAD_FG[1]
        else:
            e = GRAD_PALETTE[entry]
            color, space = (e[0][0], e[0][1], e[0][2], e[0][3] * rgba[3]), e[1]
        space = target_space if space == sv.TARGET else space
        if space == into:
            rgb = color[:3]
        else:
            assert space == SRGB_SRGB and into == LINEAR_SRGB, "only the exact conversions of this fixture"
            rgb = tuple(decode_srgb_low(c) for c in color[:3])
        out.append((offset,) + tuple(rgb) + (color[3],))
    return into, out


def gradient_render(target_map):
    """The test sink's dump of the gradient fixture on a stream (no stamps) and the report's entries."""
    target_space = SRGB_SRGB  # a stream that states no space: sRGB (S4)
    lines = ["gshscene.dump-sink 1 kind=%d unit=%d units=%d list=%016x" % (KIND_STREAM, UNIT_MM, len(GRAD_SQUARES),
                                                                           GRAD_LIST_HASH)]
    seen = set()
    entries = []
    for i, (x, y, sz, g, _) in enumerate(GRAD_SQUARES):
        head = "U %d" % i
        if g == "D":
            lines.append("%s X class=%d reason=%d" % (head, NOTICE, R_GRADIENT_DEGENERATE))
            entries.append((NOTICE, R_GRADIENT_DEGENERATE, (i,), 0, 0.0))
            continue
        composed = chain([ident()], target_map)
        b = sq_box(x, y, sz)
        corners = [apply(composed, px, py) for px in (b[0], b[2]) for py in (b[1], b[3])]
        box = (min(p[0] for p in corners), min(p[1] for p in corners), max(p[0] for p in corners),
               max(p[1] for p in corners))
        lines.append("%s F res=path:%016x map=%016x rule=0 grad=%d box=%s" % (
            head, rhash(x, y, sz), mhash(composed), GRAD_IDS[g], " ".join("%016x" % bits(v) for v in box)))
        if g not in seen:
            seen.add(g)
            into, stops = resolved(GRAD_A if g == "A" else GRAD_B, target_space)
            lines.append("R %d %016x space=%d:%d:%016x stops=%d" % (GRAD_IDS[g], GRAD_HASH[g], into[0], into[1],
                                                                   bits(into[2]), len(stops)))
            for st in stops:
                lines.append("r " + " ".join("%016x" % bits(v) for v in st))
    n = len(GRAD_SQUARES)
    lines.append("E result=0 processed=%d done=%d" % (n, n))
    text = "\n".join(lines) + "\n"
    return text, fnv(text.encode()), entries


GRAD_STREAM_TEXT, GRAD_STREAM_HASH, GRAD_STREAM_ENTRIES = gradient_render(scale(0.5))
GRAD_STREAM_REPORT_HASH, GRAD_STREAM_REPORT_DUMP = report(OK, (4, 4, 4), GRAD_STREAM_ENTRIES)

# --- strokes (strokes story S1, S20): the test sink's stroke records in its three configurations ----------------------
#
# The test interns, into a fresh store and in this order, styles A, B, Z and C (ids 1 to 4), then the squares.  SK10's
# box and SK11's required features are restated here from stroke.h's text, sharing nothing with the library.
from fractions import Fraction
import math

NATIVE = {"JOIN_MITER": 1 << 0, "JOIN_ROUND": 1 << 1, "JOIN_BEVEL": 1 << 2, "JOIN_MITER_CLIP": 1 << 3, "JOIN_ARCS": 1 << 4,
          "CAP_BUTT": 1 << 5, "CAP_ROUND": 1 << 6, "CAP_SQUARE": 1 << 7, "WIDTH_LOCAL": 1 << 8,
          "WIDTH_LOCAL_ANISOTROPIC": 1 << 9, "WIDTH_DEVICE": 1 << 10, "DASH": 1 << 11, "DASH_PATH_SCOPE": 1 << 12,
          "ALIGN": 1 << 13, "ALIGN_EVEN_ODD": 1 << 14, "DEGENERATE_DRAW": 1 << 15}
NATIVE_ALL = (1 << 16) - 1
# The three configurations of the test sink (dump_sink.h): every feature; a writer's (SVG 1.1's set, everything else
# expanded); a toolpath's (what a pen does, nothing expanded).
WRITER_NATIVE = (NATIVE["JOIN_MITER"] | NATIVE["JOIN_ROUND"] | NATIVE["JOIN_BEVEL"] | NATIVE["CAP_BUTT"] |
                 NATIVE["CAP_ROUND"] | NATIVE["CAP_SQUARE"] | NATIVE["WIDTH_LOCAL"] | NATIVE["WIDTH_LOCAL_ANISOTROPIC"] |
                 NATIVE["DASH"])
TOOLPATH_NATIVE = (NATIVE["JOIN_ROUND"] | NATIVE["CAP_ROUND"] | NATIVE["CAP_BUTT"] | NATIVE["WIDTH_LOCAL"] |
                   NATIVE["WIDTH_DEVICE"] | NATIVE["DASH"])
R_STROKE_STYLE, R_STROKE_ZERO_WIDTH = 28, 29
JOIN_NAMES = ["JOIN_MITER", "JOIN_ROUND", "JOIN_BEVEL", "JOIN_MITER_CLIP", "JOIN_ARCS"]
CAP_NAMES = ["CAP_BUTT", "CAP_ROUND", "CAP_SQUARE"]

STYLE_A = sv.style()
STYLE_B = sv.style(width=2.0, space=sv.WIDTH_DEVICE, join=sv.JOIN_ROUND, cap=sv.CAP_ROUND)
STYLE_Z = sv.style(width=0.0)
STYLE_C = sv.style(width=1.5, join=sv.JOIN_BEVEL, cap=sv.CAP_SQUARE, degenerate=sv.DEGENERATE_OMIT,
                   dashes=(1.0, 2.0, 3.0), offset=-0.5, scope=sv.DASH_PATH, align=sv.ALIGN_INSIDE,
                   rule=sv.RULE_EVEN_ODD)
STYLE_IDS = {"A": 1, "B": 2, "Z": 3, "C": 4}
STYLES = {"A": STYLE_A, "B": STYLE_B, "Z": STYLE_Z, "C": STYLE_C}
STROKE_SQUARES = [(2, 2, 8, "A", RED), (10, 4, 6, "B", BLUE), (20, 20, 6, "A", RED), (30, 2, 4, "Z", RED),
                  (40, 40, 4, "C", RED)]
STROKE_LIST_ITEMS = [sv.stroke(sv.KIND_PATH, rhash(x, y, sz), sv.style_hash(STYLES[k]), ID, rgba)
                     for x, y, sz, k, rgba in STROKE_SQUARES]
STROKE_LIST_HASH = sv.list_hash(STROKE_LIST_ITEMS)


def required(st, composed):
    """SK11: the features a stroke of style st under the composed map requires (it draws something here)."""
    need = NATIVE[JOIN_NAMES[st["join"]]] | NATIVE[CAP_NAMES[st["cap"]]]
    if st["space"] == sv.WIDTH_DEVICE:
        need |= NATIVE["WIDTH_DEVICE"]
    else:
        need |= NATIVE["WIDTH_LOCAL"]
        a, b, c, d = composed[0]
        if not ((a == d and b == -c) or (a == -d and b == c)):
            need |= NATIVE["WIDTH_LOCAL_ANISOTROPIC"]
    if st["dashes"]:
        need |= NATIVE["DASH"] | (NATIVE["DASH_PATH_SCOPE"] if st["scope"] == sv.DASH_PATH else 0)
    if st["align"] != sv.ALIGN_CENTER:
        need |= NATIVE["ALIGN"] | (NATIVE["ALIGN_EVEN_ODD"] if st["rule"] == sv.RULE_EVEN_ODD else 0)
    if st["degenerate"] == sv.DEGENERATE_DRAW and st["cap"] != sv.CAP_BUTT:
        need |= NATIVE["DEGENERATE_DRAW"]
    return need


def double_up(q):
    """The least double at or above the rational q (the outward rounding SK10 says an outset gets)."""
    x = float(q)  # nearest
    if Fraction(x) < q:
        x = math.nextafter(x, math.inf)
    return x


def double_down(q):
    x = float(q)
    if Fraction(x) > q:
        x = math.nextafter(x, -math.inf)
    return x


PIXEL = {UNIT_DEFAULT: 1.0, UNIT_MM: 25.4 / 96.0}


def stroke_box(st, square, composed, unit):
    """SK10: h from the style, formed in the stated order and covered by 1 + 2^-50; the outsets rounded outward."""
    if st["width"] == 0:
        return None
    f = 1.0
    if st["join"] == sv.JOIN_MITER:
        f = st["miter"]
    elif st["join"] in (sv.JOIN_MITER_CLIP, sv.JOIN_ARCS):
        f = st["miter"] + 1.0
    if st["cap"] == sv.CAP_SQUARE and f < 1.5:
        f = 1.5
    a = 1.0 if st["align"] == sv.ALIGN_CENTER else 2.0
    device = st["space"] == sv.WIDTH_DEVICE
    h = st["width"] * 0.5 * f * a * (PIXEL[unit] if device else 1.0) * (1.0 + 2.0 ** -50)
    x0, y0, x1, y1 = sq_box(*square)
    if not device:
        x0, y0 = double_down(Fraction(x0) - Fraction(h)), double_down(Fraction(y0) - Fraction(h))
        x1, y1 = double_up(Fraction(x1) + Fraction(h)), double_up(Fraction(y1) + Fraction(h))
    corners = [apply(composed, px, py) for px in (x0, x1) for py in (y0, y1)]
    box = [min(p[0] for p in corners), min(p[1] for p in corners), max(p[0] for p in corners), max(p[1] for p in corners)]
    if device:
        box = [double_down(Fraction(box[0]) - Fraction(h)), double_down(Fraction(box[1]) - Fraction(h)),
               double_up(Fraction(box[2]) + Fraction(h)), double_up(Fraction(box[3]) + Fraction(h))]
    return tuple(box)


def style_record(k):
    st = STYLES[k]
    hx = lambda v: "%016x" % bits(v)
    lines = ["Q %d %016x width=%s space=%d join=%d miter=%s cap=%d degenerate=%d align=%d rule=%d scope=%d offset=%s "
             "dashes=%d profile=%016x" % (STYLE_IDS[k], sv.style_hash(st), hx(st["width"]), st["space"], st["join"],
                                         hx(st["miter"]), st["cap"], st["degenerate"], st["align"], st["rule"],
                                         st["scope"], hx(st["offset"]), len(st["dashes"]), 0)]
    lines += ["d %s" % hx(d) for d in st["dashes"]]
    return lines


def stroke_render(kind, unit, target_map, native, expand, area=None):
    """The test sink's dump of the stroke fixture under caps with these native features (and STROKE_EXPAND or not),
    the stamps on a pixel target, and the report's entries."""
    n = len(STROKE_SQUARES)
    lines = ["gshscene.dump-sink 1 kind=%d unit=%d units=%d list=%016x" % (kind, unit, n, STROKE_LIST_HASH)]
    seen = set()
    entries = []
    stamps = []
    for i, (x, y, sz, k, _) in enumerate(STROKE_SQUARES):
        head = "U %d" % i
        st = STYLES[k]
        composed = chain([ident()], target_map)
        if st["width"] == 0:
            lines.append("%s X class=%d reason=%d" % (head, NOTICE, R_STROKE_ZERO_WIDTH))
            entries.append((NOTICE, R_STROKE_ZERO_WIDTH, (i,), 0, 0.0))
            continue
        missing = required(st, composed) & ~native
        if missing and not expand:
            lines.append("%s X class=%d reason=%d" % (head, REFUSED, R_STROKE_STYLE))
            entries.append((REFUSED, R_STROKE_STYLE, (i,), missing, 0.0))
            continue
        box = stroke_box(st, (x, y, sz), composed, unit)
        outline = OUTLINE_HASH["stream" if kind == KIND_STREAM else "raster"][i]
        lines.append("%s K res=path:%016x style=%d map=%016x box=%s expand=%d outline=%016x" % (
            head, rhash(x, y, sz), STYLE_IDS[k], mhash(composed), " ".join("%016x" % bits(v) for v in box), missing,
            outline))
        if k not in seen:
            seen.add(k)
            lines += style_record(k)
        if area is not None:
            r = meet((floor(box[0]), floor(box[1]), ceil(box[2]), ceil(box[3])), area)
            if not empty(r):
                m = mark((i,))
                stamps.append((r, m))
                lines.append("S %d %d %d %d %08x" % (r[0], r[1], r[2], r[3], m))
    lines.append("E result=0 processed=%d done=%d" % (n, n))
    text = "\n".join(lines) + "\n"
    return text, fnv(text.encode()), entries, stamps


# Strokes story S4 (SC10): the K record's outline field is gshgeom_stroke_outline_hash of the expansion, which this
# generator cannot restate (the expansion is shape-geometry's, and writing it again here would be a second
# implementation).  The values below were measured once, on x86-64 in ghoti-build:gcc16, from the library, and are
# committed; the test checks each against shape-geometry called directly (tests/unit/test_dump_sink.cpp), and the
# cross-target gate (suite/tools/xarch/shape.sh) must reproduce them on every other target.  Keyed by the target
# (the stream is in millimetres under a scale of 1/2, the raster is the identity) and the unit's index.
OUTLINE_HASH = {
    "stream": {0: 0x71471e926f1e92b3, 1: 0x687f32375ef0d216, 2: 0x52713422acfbb41b, 4: 0x6f760735934651b9},
    "raster": {0: 0x2058314624a79f1a, 1: 0xb9bd1eb021424f58, 2: 0x0cca12d828c733bf, 4: 0x0ddff8c919af8a64},
}


STROKE_STREAM_TEXT, STROKE_STREAM_HASH, STROKE_STREAM_ENTRIES, _ = stroke_render(KIND_STREAM, UNIT_MM, scale(0.5),
                                                                                 NATIVE_ALL, False)
STROKE_RASTER_TEXT, STROKE_RASTER_HASH, _, STROKE_RASTER_STAMPS = stroke_render(KIND_RASTER, UNIT_DEFAULT, ident(),
                                                                                 NATIVE_ALL, False, (0, 0, 64, 64))
STROKE_RASTER_PIXELS = pixels_hash(STROKE_RASTER_STAMPS)
STROKE_WRITER_TEXT, STROKE_WRITER_HASH, STROKE_WRITER_ENTRIES, _ = stroke_render(KIND_STREAM, UNIT_MM, scale(0.5),
                                                                                 WRITER_NATIVE, True)
STROKE_TOOLPATH_TEXT, STROKE_TOOLPATH_HASH, STROKE_TOOLPATH_ENTRIES, _ = stroke_render(
    KIND_STREAM, UNIT_MM, scale(0.5), TOOLPATH_NATIVE, False)
STROKE_STREAM_REPORT_HASH, STROKE_STREAM_REPORT_DUMP = report(OK, (5, 5, 5), STROKE_STREAM_ENTRIES)
STROKE_WRITER_REPORT_HASH, STROKE_WRITER_REPORT_DUMP = report(OK, (5, 5, 5), STROKE_WRITER_ENTRIES)
STROKE_TOOLPATH_REPORT_HASH, STROKE_TOOLPATH_REPORT_DUMP = report(OK, (5, 5, 5), STROKE_TOOLPATH_ENTRIES)
assert STROKE_WRITER_ENTRIES == STROKE_STREAM_ENTRIES, "a writer expands what it lacks: no entry but the notice"
assert [e[3] for e in STROKE_TOOLPATH_ENTRIES if e[0] == REFUSED] == [
    NATIVE["JOIN_MITER"], NATIVE["DEGENERATE_DRAW"], NATIVE["JOIN_MITER"],
    NATIVE["JOIN_BEVEL"] | NATIVE["CAP_SQUARE"] | NATIVE["DASH_PATH_SCOPE"] | NATIVE["ALIGN"] | NATIVE["ALIGN_EVEN_ODD"]]


# --- placeholders (color-paint story P5; list.h PH1, sink.h PH3 to PH6, dump_sink.h) ---------------------------------
# A refused placeholder with a box under a map, an approximated one without a box, a fill between them, and a refused
# one with a box at the end.  The planning pass writes each entry from the list alone (PH3), so the three configurations
# of the test sink and both target kinds say the same; the sink writes `P class=N reason=N source=N code=N approx=N
# map=<hash> box=<4 doubles>|-` for each (the class and reason are the plan's).
R_PLACEHOLDER = 31
COLR = 1
PH_BOX_A = (0.0, 0.0, 5.0, 5.0)
PH_BOX_C = (-1.0, -2.0, 3.0, 4.0)
PH_MOVE = translate(3, 4)
PH_ITEMS = [
    sv.fill(sv.KIND_PATH, rhash(2, 2, 8), ID, 0, RED),
    sv.placeholder(COLR, 1, 0, PH_BOX_A, mhash(PH_MOVE)),
    sv.placeholder(COLR, 3, 1, None, ID, layer=2),
    sv.fill(sv.KIND_PATH, rhash(20, 20, 6), ID, 0, BLUE),
    sv.placeholder(COLR, 2, 0, PH_BOX_C, ID),
]
PH_LIST_HASH = sv.list_hash(PH_ITEMS)
PH_UNITS = [  # chain index, item map, source, code, approximated, box or None
    (1, PH_MOVE, COLR, 1, 0, PH_BOX_A), (2, ident(), COLR, 3, 1, None), (4, ident(), COLR, 2, 0, PH_BOX_C)]
PH_FILLS = {0: (2, 2, 8), 3: (20, 20, 6)}


def placeholder_render(kind, unit, target_map, area=None):
    """The test sink's dump of the placeholder fixture, the stamps on a pixel target, and the report's entries."""
    n = len(PH_ITEMS)
    lines = ["gshscene.dump-sink 1 kind=%d unit=%d units=%d list=%016x" % (kind, unit, n, PH_LIST_HASH)]
    stamps = []
    entries = []
    for i in range(n):
        head = "U %d" % i
        if i in PH_FILLS:
            x, y, sz = PH_FILLS[i]
            composed = chain([ident()], target_map)
            b = sq_box(x, y, sz)
            corners = [apply(composed, px, py) for px in (b[0], b[2]) for py in (b[1], b[3])]
            box = (min(p[0] for p in corners), min(p[1] for p in corners), max(p[0] for p in corners),
                   max(p[1] for p in corners))
            lines.append("%s F res=path:%016x map=%016x rule=0 box=%s" % (
                head, rhash(x, y, sz), mhash(composed), " ".join("%016x" % bits(v) for v in box)))
            if area is not None:
                r = meet((floor(box[0]), floor(box[1]), ceil(box[2]), ceil(box[3])), area)
                if not empty(r):
                    stamps.append((r, mark((i,))))
                    lines.append("S %d %d %d %d %08x" % (r[0], r[1], r[2], r[3], mark((i,))))
            continue
        _, item_map, src, code, approx, pbox = [u for u in PH_UNITS if u[0] == i][0]
        composed = chain([item_map], target_map)
        cls = APPROXIMATED if approx else REFUSED
        if pbox is None:
            box_text = "-"
        else:
            corners = [apply(composed, px, py) for px in (pbox[0], pbox[2]) for py in (pbox[1], pbox[3])]
            box = (min(p[0] for p in corners), min(p[1] for p in corners), max(p[0] for p in corners),
                   max(p[1] for p in corners))
            box_text = " ".join("%016x" % bits(v) for v in box)
        lines.append("%s P class=%d reason=%d source=%d code=%d approx=%d map=%016x box=%s" % (
            head, cls, R_PLACEHOLDER, src, code, approx, mhash(composed), box_text))
        entries.append((cls, R_PLACEHOLDER, (i,), (src << 16) | code, 0.0))
    lines.append("E result=0 processed=%d done=%d" % (n, n))
    text = "\n".join(lines) + "\n"
    return text, fnv(text.encode()), stamps, entries


PH_STREAM_TEXT, PH_STREAM_HASH, _, PH_ENTRIES = placeholder_render(KIND_STREAM, UNIT_MM, scale(0.5))
PH_RASTER_TEXT, PH_RASTER_HASH, PH_RASTER_STAMPS, _ = placeholder_render(KIND_RASTER, UNIT_DEFAULT, ident(), (0, 0, 64, 64))
PH_RASTER_PIXELS = pixels_hash(PH_RASTER_STAMPS)
PH_REPORT_HASH, PH_REPORT_DUMP = report(OK, (5, 5, 5), PH_ENTRIES)


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
                    ("kNarrowReportHash", NARROW_HASH), ("kStrictReportHash", STRICT_HASH),
                    ("kGlyphListHash", GLYPH_LIST_HASH), ("kGlyphRunHash", RUN_HASH),
                    ("kGlyphRasterOutputHash", GLYPH_RASTER_HASH), ("kGlyphRasterPixelsHash", GLYPH_RASTER_PIXELS),
                    ("kGlyphStreamOutputHash", GLYPH_STREAM_HASH), ("kGlyphRasterReportHash", GLYPH_RASTER_REPORT_HASH),
                    ("kGlyphStreamReportHash", GLYPH_STREAM_REPORT_HASH), ("kGradientListHash", GRAD_LIST_HASH),
                    ("kGradientAHash", GRAD_HASH["A"]), ("kGradientBHash", GRAD_HASH["B"]),
                    ("kGradientStreamOutputHash", GRAD_STREAM_HASH),
                    ("kGradientStreamReportHash", GRAD_STREAM_REPORT_HASH), ("kStrokeListHash", STROKE_LIST_HASH),
                    ("kStrokeStreamOutputHash", STROKE_STREAM_HASH), ("kStrokeRasterOutputHash", STROKE_RASTER_HASH),
                    ("kStrokeRasterPixelsHash", STROKE_RASTER_PIXELS), ("kStrokeWriterOutputHash", STROKE_WRITER_HASH),
                    ("kStrokeToolpathOutputHash", STROKE_TOOLPATH_HASH),
                    ("kStrokeStreamReportHash", STROKE_STREAM_REPORT_HASH),
                    ("kStrokeWriterReportHash", STROKE_WRITER_REPORT_HASH),
                    ("kStrokeToolpathReportHash", STROKE_TOOLPATH_REPORT_HASH),
                    ("kPlaceholderListHash", PH_LIST_HASH), ("kPlaceholderStreamOutputHash", PH_STREAM_HASH),
                    ("kPlaceholderRasterOutputHash", PH_RASTER_HASH), ("kPlaceholderRasterPixelsHash", PH_RASTER_PIXELS),
                    ("kPlaceholderReportHash", PH_REPORT_HASH)):
        o.append("static const std::uint64_t %s = UINT64_C(0x%016x);\n" % (name, v))
    o.append("\n")
    o.append(cxx_string("kRasterDump", RASTER_TEXT))
    o.append(cxx_string("kStreamDump", STREAM_TEXT))
    o.append(cxx_string("kNarrowReportDump", NARROW_DUMP))
    o.append(cxx_string("kStrictReportDump", STRICT_DUMP))
    o.append(cxx_string("kGlyphRasterDump", GLYPH_RASTER_TEXT))
    o.append(cxx_string("kGlyphStreamDump", GLYPH_STREAM_TEXT))
    o.append(cxx_string("kGlyphRasterReportDump", GLYPH_RASTER_REPORT_DUMP))
    o.append(cxx_string("kGradientStreamDump", GRAD_STREAM_TEXT))
    o.append(cxx_string("kGradientStreamReportDump", GRAD_STREAM_REPORT_DUMP))
    o.append(cxx_string("kStrokeStreamDump", STROKE_STREAM_TEXT))
    o.append(cxx_string("kStrokeRasterDump", STROKE_RASTER_TEXT))
    o.append(cxx_string("kStrokeWriterDump", STROKE_WRITER_TEXT))
    o.append(cxx_string("kStrokeToolpathDump", STROKE_TOOLPATH_TEXT))
    o.append(cxx_string("kStrokeToolpathReportDump", STROKE_TOOLPATH_REPORT_DUMP))
    o.append(cxx_string("kPlaceholderStreamDump", PH_STREAM_TEXT))
    o.append(cxx_string("kPlaceholderRasterDump", PH_RASTER_TEXT))
    o.append(cxx_string("kPlaceholderReportDump", PH_REPORT_DUMP))
    o.append("static const std::uint32_t kStrokeWriterNative = 0x%04xu;\n" % WRITER_NATIVE)
    o.append("static const std::uint32_t kStrokeToolpathNative = 0x%04xu;\n" % TOOLPATH_NATIVE)
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
