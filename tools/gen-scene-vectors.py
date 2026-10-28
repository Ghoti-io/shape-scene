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
"""Print the known answers of the resource, list and version spellings (CAP-13, CAP-16, CAP-7, AD-21, AD-27, AD-9).

This is a second statement of the spellings in include/ghoti.io/shape-scene/
resource.h, list.h and version.h (and of the path and map spellings they build on, in
shape-geometry's path.h and map.h), written with Python's `struct` and none of
the library's code, so that a test comparing the two compares two readings of
the text.  It writes tests/known_answers.h, which is committed; run it after
changing a spelling, with the fixtures below (the same objects as
test_dump.cpp builds with the API).

    python3 tools/gen-scene-vectors.py > tests/known_answers.h
    python3 tools/gen-scene-vectors.py --check      # fails if the file differs

The fixtures: a square path, shape-geometry's own fixture path (every verb), a
face, a glyph run from that face and one from outlines, a palette (story 14c; one
made from doubles and the same entries from RGBA8), four stroke styles (strokes story S1: each put in SK3's
canonical form by this script's own reading of the text), three gradients (color-paint story
P1: linear, radial and sweep, with a list of their fills and a scene with a gradient
shape), a picture, and a list holding every
kind of item and every paint, with a foreground and a palette, each also in its dump text; and a scene (version.h) holding every kind
of node, with a clip, a key, a hidden node and an instance, built by the test in the
order of the IDs below (the root 1, then 2 to 7), with its hash and its dump, and the
empty scene, and the keys of some instance paths.
"""
import os
import struct
import sys

BASIS = 0xCBF29CE484222325
PRIME = 0x100000001B3
M64 = (1 << 64) - 1


def fnv(data, state=BASIS):
    for b in data:
        state = ((state ^ b) * PRIME) & M64
    return state


assert fnv(b"") == 0xCBF29CE484222325
assert fnv(b"a") == 0xAF63DC4C8601EC8C
assert fnv(b"foobar") == 0x85944171F73967E8


def u8(v):
    return struct.pack("<B", v)


def u16(v):
    return struct.pack("<H", v)


def u32(v):
    return struct.pack("<I", v)


def u64(v):
    return struct.pack("<Q", v)


def i32(v):
    return struct.pack("<i", v)


def f64(v):
    return struct.pack("<d", v)


def bits(v):
    return struct.unpack("<Q", struct.pack("<d", v))[0]


# --- paths (shape-geometry's spelling, path.h) -------------------------------
VERB = {"M": 0, "L": 1, "Q": 2, "C": 3, "RQ": 4, "RC": 5, "Z": 6}


def square():
    return 2, [("M", [(0.0, 0.0)], None), ("L", [(4.0, 0.0)], None), ("L", [(4.0, 4.0)], None),
               ("L", [(0.0, 4.0)], None), ("Z", [], None)]


def fixture2():
    return 2, [
        ("M", [(1.5, -2.25)], None),
        ("L", [(3.0, 4.0)], None),
        ("Q", [(5.0, 6.5), (7.0, 8.0)], None),
        ("C", [(9.0, 10.0), (11.25, 12.0), (13.0, 14.0)], None),
        ("RQ", [(15.0, 16.0), (17.0, 18.0)], [1.0, 0.5, 2.0]),
        ("RC", [(19.0, 20.0), (21.0, 22.0), (23.0, 24.1)], [1.0, 2.0, 0.5, 4.0]),
        ("Z", [], None),
    ]


def path_hash(path):
    dim, els = path
    out = u32(1) + u32(dim) + u64(len(els))
    for name, pts, w in els:
        out += u8(VERB[name])
        for p in pts:
            out += f64(p[0]) + f64(p[1])
        for x in (w or []):
            out += f64(x)
    return fnv(out)


def path_dump(path):
    dim, els = path
    lines = ["gshgeom.path 1 dim=%d elements=%d hash=%016x" % (dim, len(els), path_hash(path))]
    for name, pts, w in els:
        s = name
        for x in (w or []):
            s += " %016x" % bits(x)
        for p in pts:
            s += " %016x %016x" % (bits(p[0]), bits(p[1]))
        lines.append(s)
    return "\n".join(lines) + "\n"


# --- maps (map.h): identity, translate, design-to-model ----------------------
def map_hash(dim, m, t, kind):
    out = u32(1) + u32(dim) + u32(kind)
    for v in m:
        out += f64(float(v))
    for v in t:
        out += f64(float(v))
    return fnv(out)


IDENTITY = map_hash(2, [1, 0, 0, 1], [0, 0], 0)
TRANSLATE = map_hash(2, [1, 0, 0, 1], [1.5, -2.25], 1)
DESIGN = map_hash(2, [2.5, 0, 0, -2.5], [10, 20], 1 | 2 | 8)

# --- resources (resource.h) ----------------------------------------------------
KIND_PATH, KIND_RUN, KIND_PICTURE, KIND_GRADIENT, KIND_FACE, KIND_PALETTE, KIND_STROKE = 1, 2, 3, 4, 6, 7, 8
KIND_NAME = {KIND_PATH: "path", KIND_RUN: "glyph-run", KIND_PICTURE: "picture", KIND_FACE: "face",
             KIND_PALETTE: "palette", KIND_GRADIENT: "gradient", KIND_STROKE: "stroke-style"}
# The format is per kind (resource.h); every kind's is 1 until the suite's first release.
RUN_FORMAT = 1


def path_resource_hash(path):
    return fnv(u32(1) + u32(KIND_PATH) + u64(path_hash(path)))


def face_hash(key, index, coords):
    # Trailing zero coordinates are dropped before the face is interned.
    coords = list(coords)
    while coords and coords[-1] == 0:
        coords.pop()
    out = u32(1) + u32(KIND_FACE) + u64(key) + u32(index) + u32(len(coords))
    for c in coords:
        out += u32(c & 0xFFFFFFFF)
    return fnv(out)


def face_dump(key, index, coords):
    h = face_hash(key, index, coords)
    coords = list(coords)
    while coords and coords[-1] == 0:
        coords.pop()
    return ("gshscene.resource 1 kind=face hash=%016x\n" % h +
            "face font=%016x index=%d coords=%d" % (key, index, len(coords)) + "".join(" %d" % c for c in coords) +
            "\nbound=0 upem=0 flags=0\n")


def run_hash(source, face, size, hint, version, glyphs):
    out = u32(RUN_FORMAT) + u32(KIND_RUN) + u32(source) + u64(face) + f64(size) + u32(hint) + u32(version)
    out += u64(len(glyphs))
    for gid, x, y, outline in glyphs:
        out += u32(gid) + f64(x) + f64(y) + u64(outline)
    return fnv(out)


def run_dump(source, face, size, hint, version, glyphs):
    h = run_hash(source, face, size, hint, version, glyphs)
    lines = ["gshscene.resource %d kind=glyph-run hash=%016x" % (RUN_FORMAT, h),
             "run source=%d face=%016x size=%016x hint=%d version=%d glyphs=%d" % (
                 source, face, bits(size), hint, version, len(glyphs))]
    for gid, x, y, outline in glyphs:
        lines.append("g id=%d x=%016x y=%016x outline=%016x" % (gid, bits(x), bits(y), outline))
    return "\n".join(lines) + "\n"


SQ = square()
P2 = fixture2()
SQ_RES = path_resource_hash(SQ)
P2_RES = path_resource_hash(P2)

# The face (story 14a): key, face index 2, coordinates 8192 and -4096, unbound.
FACE = (0x1122334455667788, 2, [8192, -4096])
FACE_HASH = face_hash(*FACE)
FONT_RUN = (0, FACE_HASH, 12.5, 1, 40,
            [(36, 0.0, 0.0, 0), (37, 7.5, 0.25, 0), (4294967295, -1.0, 2.0, 0)])
OUTLINE_RUN = (1, 0, 0.0, 0, 0, [(1, 0.0, 0.0, SQ_RES), (2, 5.0, -3.0, P2_RES)])
FONT_RUN_HASH = run_hash(*FONT_RUN)
OUTLINE_RUN_HASH = run_hash(*OUTLINE_RUN)

# --- color spaces (paint.h) ---------------------------------------------------------------
# GCOL_Primaries: UNKNOWN 0, SRGB 1, ADOBE_RGB 2, DISPLAY_P3 3, BT2020 4, PROPHOTO 5.
# GCOL_Transfer: UNKNOWN 0, LINEAR 1, SRGB 2, GAMMA 3, PARAMETRIC 4, BT1886 5, PQ 6, HLG 7.
TARGET = (0, 0, 0.0)          # the zero space is the target's
SRGB_SRGB = (1, 2, 0.0)
SRGB_LINEAR = (1, 1, 0.0)
P3_GAMMA = (3, 3, 2.2)
ADOBE_GAMMA = (2, 3, 2.19921875)


def space_bytes(sp):
    return u8(sp[0]) + u8(sp[1]) + f64(sp[2])


def space_text(sp):
    return "%d:%d:%016x" % (sp[0], sp[1], bits(sp[2]))


def color_bytes(rgba, sp):
    return b"".join(f64(c) for c in rgba) + space_bytes(sp)


def color_text(rgba, sp):
    return "%s %s" % (" ".join("%016x" % bits(c) for c in rgba), space_text(sp))


# --- palettes (resource.h, N14; story 14c) ---------------------------------------------------
def palette_hash(entries):
    out = u32(1) + u32(KIND_PALETTE) + u64(len(entries))
    for rgba, sp in entries:
        out += color_bytes(rgba, sp)
    return fnv(out)


def palette_dump(entries):
    lines = ["gshscene.resource 1 kind=palette hash=%016x" % palette_hash(entries),
             "palette entries=%d" % len(entries)]
    lines += ["p %s" % color_text(rgba, sp) for rgba, sp in entries]
    return "\n".join(lines) + "\n"


def rgba8(r, g, b, a):
    """fixed.h: each component c / 255 (Python's division of two floats is IEEE's, correctly rounded), in sRGB."""
    return ((r / 255.0, g / 255.0, b / 255.0, a / 255.0), SRGB_SRGB)


# Three entries in three spaces, and the same three as a CPAL table would give them (8-bit sRGB).
PALETTE = [((1.0, 0.5, 0.25, 1.0), SRGB_SRGB), ((0.0, 0.75, 1.0, 0.5), P3_GAMMA), ((0.125, 0.0, 2.0, 0.25), SRGB_LINEAR)]
PALETTE_HASH = palette_hash(PALETTE)
PALETTE8_BYTES = [(255, 0, 0, 255), (0, 128, 255, 51), (1, 2, 254, 0)]
PALETTE8 = [rgba8(*c) for c in PALETTE8_BYTES]
PALETTE8_HASH = palette_hash(PALETTE8)


# --- gradients (resource.h, paint.h G1 to G3; color-paint story P1) ------------------------------------
LINEAR, RADIAL, SWEEP = 1, 2, 3
PAINT_SOLID, PAINT_GRADIENT, PAINT_FOREGROUND, PAINT_PALETTE = 0, 1, 2, 3
PAD, REPEAT, REFLECT = 0, 1, 2
DEGREES, TURNS = 1, 3


def stop(offset, kind, rgba=(0.0, 0.0, 0.0, 1.0), space=(0, 0, 0.0), entry=0):
    """A stop as given: an offset and a paint (kind, four doubles, a space, an entry)."""
    return (offset, kind, rgba, space, entry)


def gradient(kind, geom, gmap, stops, extend=PAD, space=(0, 0, 0.0), premul=0, unit=0):
    """geom: linear (p0x, p0y, p1x, p1y, dx, dy); radial (c0x, c0y, r0, c1x, c1y, r1); sweep (cx, cy, a0, a1)."""
    # G2: the store sorts by offset, stably; Python's sort is stable.  -0 is +0.
    z = lambda v: 0.0 if v == 0 else v
    ordered = sorted(((z(s[0]),) + s[1:] for s in stops), key=lambda s: s[0])
    return dict(kind=kind, geom=tuple(z(v) for v in geom), map=gmap, stops=ordered, extend=extend, space=space,
                premul=premul, unit=unit)


def gradient_hash(g):
    out = u32(1) + u32(KIND_GRADIENT) + u8(g["kind"]) + u8(g["extend"]) + space_bytes(g["space"]) + u8(g["premul"])
    out += u64(g["map"])
    if g["kind"] == SWEEP:
        cx, cy, a0, a1 = g["geom"]
        out += f64(cx) + f64(cy) + u8(g["unit"]) + f64(a0) + f64(a1)
    else:
        out += b"".join(f64(v) for v in g["geom"])
    out += u64(len(g["stops"]))
    for offset, kind, rgba, sp, entry in g["stops"]:
        out += f64(offset) + u8(kind) + color_bytes(rgba, sp) + u32(entry)
    return fnv(out)


def gradient_dump(g):
    h = gradient_hash(g)
    hx = lambda v: "%016x" % bits(v)
    lines = ["gshscene.resource 1 kind=gradient hash=%016x" % h,
             "gradient kind=%d extend=%d space=%s premul=%d map=%016x stops=%d" % (
                 g["kind"], g["extend"], space_text(g["space"]), g["premul"], g["map"], len(g["stops"]))]
    v = g["geom"]
    if g["kind"] == LINEAR:
        lines.append("linear p0=%s %s p1=%s %s d=%s %s" % tuple(hx(x) for x in v))
    elif g["kind"] == RADIAL:
        lines.append("radial c0=%s %s r0=%s c1=%s %s r1=%s" % tuple(hx(x) for x in v))
    else:
        lines.append("sweep c=%s %s unit=%d a0=%s a1=%s" % (hx(v[0]), hx(v[1]), g["unit"], hx(v[2]), hx(v[3])))
    for offset, kind, rgba, sp, entry in g["stops"]:
        lines.append("s %s paint=%d %s space=%s entry=%d" % (hx(offset), kind, " ".join(hx(c) for c in rgba),
                                                            space_text(sp), entry))
    return "\n".join(lines) + "\n"


# Three gradients (test_gradient.cpp builds the same): a linear one with an isoline direction, its stops given out of
# order with two at one offset and one of each paint kind, in linear sRGB premultiplied; a radial cone with a negative
# radius under a design map, in the target's space; a sweep in degrees, reflected, in P3 with a gamma.
GRAD_LINEAR = gradient(LINEAR, (0.0, 0.0, 10.0, 0.0, 1.0, 3.0), TRANSLATE,
                       [stop(1.0, PAINT_SOLID, (1.0, 0.5, 0.25, 1.0), SRGB_SRGB),
                        stop(0.5, PAINT_FOREGROUND, (0.0, 0.0, 0.0, 0.5)),
                        stop(-0.0, PAINT_PALETTE, (0.0, 0.0, 0.0, 0.75), entry=2),
                        stop(0.5, PAINT_SOLID, (0.0, 1.0, 0.0, 0.25), P3_GAMMA)],
                       extend=REPEAT, space=SRGB_LINEAR, premul=1)
GRAD_RADIAL = gradient(RADIAL, (1.0, 2.0, -1.5, 4.0, 2.0, 6.25), DESIGN,
                       [stop(0.0, PAINT_SOLID, (0.0, 0.0, 0.0, 1.0)), stop(1.0, PAINT_SOLID, (1.0, 1.0, 1.0, 0.0))])
GRAD_SWEEP = gradient(SWEEP, (3.0, -4.0, 90.0, 450.0), IDENTITY,
                      [stop(0.25, PAINT_SOLID, (0.5, 0.25, 1.0, 1.0), ADOBE_GAMMA)],
                      extend=REFLECT, space=P3_GAMMA, unit=DEGREES)
GRAD_LINEAR_HASH = gradient_hash(GRAD_LINEAR)
GRAD_RADIAL_HASH = gradient_hash(GRAD_RADIAL)
GRAD_SWEEP_HASH = gradient_hash(GRAD_SWEEP)


# --- stroke styles (stroke.h, SK1 to SK3, SK12; strokes story S1) ---------------------------------------
WIDTH_LOCAL, WIDTH_DEVICE = 0, 1
JOIN_MITER, JOIN_ROUND, JOIN_BEVEL, JOIN_MITER_CLIP, JOIN_ARCS = 0, 1, 2, 3, 4
CAP_BUTT, CAP_ROUND, CAP_SQUARE = 0, 1, 2
DEGENERATE_DRAW, DEGENERATE_OMIT = 0, 1
DASH_PER_SUBPATH, DASH_PATH = 0, 1
ALIGN_CENTER, ALIGN_INSIDE, ALIGN_OUTSIDE = 0, 1, 2
RULE_NONZERO, RULE_EVEN_ODD = 0, 1


def style(width=1.0, space=WIDTH_LOCAL, join=JOIN_MITER, miter=4.0, cap=CAP_BUTT, degenerate=DEGENERATE_DRAW,
          dashes=(), offset=0.0, scope=DASH_PER_SUBPATH, align=ALIGN_CENTER, rule=RULE_NONZERO):
    """A style as given (SK1's defaults), put in SK3's canonical form here, from the text alone."""
    z = lambda v: 0.0 if v == 0 else v
    dashes = list(dashes)
    if any(d < 0 for d in dashes) or all(d == 0 for d in dashes):
        dashes = []                       # SK3 (a): a negative length, or all zero (or none), is solid
    elif len(dashes) % 2:
        dashes = dashes + dashes          # an odd array repeated once
    dashes = [z(d) for d in dashes]
    if not dashes:
        offset, scope = 0.0, DASH_PER_SUBPATH  # (b) the dash fields do not apply
    if join not in (JOIN_MITER, JOIN_MITER_CLIP, JOIN_ARCS):
        miter = 4.0                       # (b) the miter limit does not apply
    if align == ALIGN_CENTER:
        rule = RULE_NONZERO               # (b) the alignment rule does not apply
    if cap == CAP_BUTT:
        degenerate = DEGENERATE_DRAW      # (b) a butt cap draws nothing of a degenerate subpath either way
    return dict(width=z(width), space=space, join=join, miter=miter, cap=cap, degenerate=degenerate, dashes=dashes,
                offset=z(offset), scope=scope, align=align, rule=rule)


def style_hash(st):
    out = u32(1) + u32(KIND_STROKE) + f64(st["width"]) + u8(st["space"]) + u8(st["join"]) + f64(st["miter"])
    out += u8(st["cap"]) + u8(st["degenerate"]) + u8(st["align"]) + u8(st["rule"]) + u8(st["scope"])
    out += f64(st["offset"]) + u64(len(st["dashes"])) + b"".join(f64(d) for d in st["dashes"]) + u64(0)
    return fnv(out)


def style_dump(st):
    hx = lambda v: "%016x" % bits(v)
    lines = ["gshscene.resource 1 kind=stroke-style hash=%016x" % style_hash(st),
             "stroke width=%s space=%d join=%d miter=%s cap=%d degenerate=%d align=%d rule=%d scope=%d offset=%s "
             "dashes=%d profile=%016x" % (hx(st["width"]), st["space"], st["join"], hx(st["miter"]), st["cap"],
                                         st["degenerate"], st["align"], st["rule"], st["scope"], hx(st["offset"]),
                                         len(st["dashes"]), 0)]
    lines += ["d %s" % hx(d) for d in st["dashes"]]
    return "\n".join(lines) + "\n"


# Four styles (test_stroke.cpp builds the same): SK1's defaults; a device width with a round join (its miter limit
# reset), a square cap, omitted degenerate subpaths, an odd dash array (doubled) with a negative offset (kept) and the
# path's scope, inside alignment by even-odd; a miter-clip join with an all-zero dash array (solid: offset and scope
# reset), centered by even-odd (reset), width 0 (kept); an arcs join with a negative dash (solid), outside by nonzero.
STYLE_DEFAULT = style()
STYLE_DEVICE = style(width=0.75, space=WIDTH_DEVICE, join=JOIN_ROUND, miter=10.0, cap=CAP_SQUARE,
                     degenerate=DEGENERATE_OMIT, dashes=(1.0, 2.0, 3.5), offset=-0.5, scope=DASH_PATH,
                     align=ALIGN_INSIDE, rule=RULE_EVEN_ODD)
STYLE_CLIP = style(width=-0.0, join=JOIN_MITER_CLIP, miter=2.5, cap=CAP_ROUND, dashes=(0.0, -0.0), offset=5.0,
                   scope=DASH_PATH, rule=RULE_EVEN_ODD)
STYLE_ARCS = style(width=3.0, join=JOIN_ARCS, miter=65536.0, dashes=(4.0, -1.0), align=ALIGN_OUTSIDE)
STYLE_DEFAULT_HASH = style_hash(STYLE_DEFAULT)
STYLE_DEVICE_HASH = style_hash(STYLE_DEVICE)
STYLE_CLIP_HASH = style_hash(STYLE_CLIP)
STYLE_ARCS_HASH = style_hash(STYLE_ARCS)


# --- lists (list.h) ---------------------------------------------------------------
FILL, CLIP_PUSH, CLIP_POP, GROUP_PUSH, GROUP_POP, INSTANCE, STROKE, PLACEHOLDER = 1, 2, 3, 4, 5, 6, 7, 8
ORDER_FLAT, ORDER_DEPTH, ORDER_OVERLAY = 0, 1, 2
COMPOSITE = {"src-over": 0, "clear": 1, "src": 2, "dest": 3, "dest-over": 4, "src-in": 5, "dest-in": 6,
             "src-out": 7, "dest-out": 8, "src-atop": 9, "dest-atop": 10, "xor": 11, "plus": 12,
             # B1: the blend modes, COLRv1's CompositeMode numbers
             "screen": 13, "overlay": 14, "darken": 15, "lighten": 16, "color-dodge": 17, "color-burn": 18,
             "hard-light": 19, "soft-light": 20, "difference": 21, "exclusion": 22, "multiply": 23, "hue": 24,
             "saturation": 25, "color": 26, "luminosity": 27}
TIE_LAYER, TIE_PATH, TIE_DECLARED = 1, 2, 3
DEFAULT_FG = ((0.0, 0.0, 0.0, 1.0), TARGET)


def fill(rkind, rhash, mhash, rule, rgba, space=TARGET, layer=0, depth=0.0, paint=PAINT_SOLID, entry=0, grad=0):
    return dict(k=FILL, rkind=rkind, rhash=rhash, mhash=mhash, rule=rule, rgba=rgba, space=space, layer=layer,
                depth=depth, paint=paint, entry=entry, grad=grad)


def clip_push(rkind, rhash, mhash, rule, layer=0, depth=0.0):
    return dict(k=CLIP_PUSH, rkind=rkind, rhash=rhash, mhash=mhash, rule=rule, layer=layer, depth=depth)


def group_push(opacity, blend=TARGET, composite=0, order=ORDER_FLAT, layer=0, depth=0.0, isolation=0, knockout=0, mask=0):
    return dict(k=GROUP_PUSH, opacity=opacity, blend=blend, composite=composite, order=order, layer=layer, depth=depth,
                isolation=isolation, knockout=knockout, mask=mask)


def composite_bytes(composite, isolation=0, knockout=0, mask=0):
    """T1 (list.h): the u8 composite, and PDF's group fields after it, bit 7 set, only when one is not the default."""
    if not isolation and not knockout and not mask:
        return u8(composite)
    return u8(0x80 | composite) + u8((1 if isolation else 0) + (2 if knockout else 0)) + u64(mask)


def pdf_fields_text(isolation=0, knockout=0, mask=0):
    if not isolation and not knockout and not mask:
        return ""
    return " isolation=%d knockout=%d mask=%016x" % (isolation, knockout, mask)


def stroke(rkind, rhash, shash, mhash, rgba, space=TARGET, layer=0, depth=0.0, paint=PAINT_SOLID, entry=0, grad=0):
    """A stroke item (SK4): a centerline, a style, a map, a paint as a fill's, a layer and a depth."""
    return dict(k=STROKE, rkind=rkind, rhash=rhash, shash=shash, mhash=mhash, rgba=rgba, space=space, layer=layer,
                depth=depth, paint=paint, entry=entry, grad=grad)


def placeholder(source, code, approx, box, mhash, layer=0, depth=0.0):
    """A placeholder item (PH1): a source, a code, the approximated flag, a box (x0, y0, x1, y1) or None, a map."""
    return dict(k=PLACEHOLDER, source=source, code=code, approx=approx, box=box, mhash=mhash, layer=layer, depth=depth)


def placeholder_bytes(p):
    """PH1: u16 source, u16 code, u8 approximated, u8 1 if there is a box, then its four f64 (a list's and a version's)."""
    out = u16(p["source"]) + u16(p["code"]) + u8(p["approx"]) + u8(0 if p["box"] is None else 1)
    if p["box"] is not None:
        out += b"".join(f64(v) for v in p["box"])
    return out


def placeholder_text(p):
    box = "-" if p["box"] is None else " ".join("%016x" % bits(v) for v in p["box"])
    return "source=%d code=%d approx=%d box=%s" % (p["source"], p["code"], p["approx"], box)


def instance(rkind, rhash, mhash, layer=0, depth=0.0):
    return dict(k=INSTANCE, rkind=rkind, rhash=rhash, mhash=mhash, layer=layer, depth=depth)


CLIP_POP_ITEM = dict(k=CLIP_POP)
GROUP_POP_ITEM = dict(k=GROUP_POP)


def item_bytes(it):
    k = it["k"]
    out = u8(k)
    if k == FILL:
        out += u64(it["rhash"]) + u64(it["mhash"]) + u8(it["rule"]) + u8(it["paint"])
        out += color_bytes(it["rgba"], it["space"]) + u32(it["entry"]) + u64(it["grad"]) + i32(it["layer"])
        out += f64(it["depth"])
    elif k == CLIP_PUSH:
        out += u64(it["rhash"]) + u64(it["mhash"]) + u8(it["rule"]) + i32(it["layer"]) + f64(it["depth"])
    elif k == GROUP_PUSH:
        out += f64(it["opacity"]) + space_bytes(it["blend"])
        out += composite_bytes(it["composite"], it.get("isolation", 0), it.get("knockout", 0), it.get("mask", 0))
        out += u32(it["order"]) + i32(it["layer"]) + f64(it["depth"])
    elif k == INSTANCE:
        out += u64(it["rhash"]) + u64(it["mhash"]) + i32(it["layer"]) + f64(it["depth"])
    elif k == STROKE:
        # SK12: the centerline, the style, the map, the paint as a fill spells it, the layer, the depth.
        out += u64(it["rhash"]) + u64(it["shash"]) + u64(it["mhash"]) + u8(it["paint"])
        out += color_bytes(it["rgba"], it["space"]) + u32(it["entry"]) + u64(it["grad"]) + i32(it["layer"])
        out += f64(it["depth"])
    elif k == PLACEHOLDER:
        out += placeholder_bytes(it) + u64(it["mhash"]) + i32(it["layer"]) + f64(it["depth"])
    return out


def list_hash(items, fg=DEFAULT_FG, palette=0):
    out = u32(1) + u32(0x5453494C) + color_bytes(fg[0], fg[1]) + u64(palette) + u64(len(items))
    for it in items:
        out += item_bytes(it)
    return fnv(out)


def item_line(it):
    k = it["k"]
    ld = "layer=%d depth=%016x" % (it["layer"], bits(it["depth"])) if k != CLIP_POP and k != GROUP_POP else ""
    if k == FILL:
        return "F res=%s:%016x map=%016x rule=%d paint=%d %s space=%s entry=%d resource=%016x %s" % (
            KIND_NAME[it["rkind"]], it["rhash"], it["mhash"], it["rule"], it["paint"],
            " ".join("%016x" % bits(c) for c in it["rgba"]), space_text(it["space"]), it["entry"], it["grad"], ld)
    if k == CLIP_PUSH:
        return "C res=%s:%016x map=%016x rule=%d %s" % (KIND_NAME[it["rkind"]], it["rhash"], it["mhash"], it["rule"], ld)
    if k == CLIP_POP:
        return "c"
    if k == GROUP_PUSH:
        return "G opacity=%016x blend=%s composite=%d order=%d%s %s" % (
            bits(it["opacity"]), space_text(it["blend"]), it["composite"], it["order"],
            pdf_fields_text(it.get("isolation", 0), it.get("knockout", 0), it.get("mask", 0)), ld)
    if k == GROUP_POP:
        return "g"
    if k == STROKE:
        return "K res=%s:%016x style=%016x map=%016x paint=%d %s space=%s entry=%d resource=%016x %s" % (
            KIND_NAME[it["rkind"]], it["rhash"], it["shash"], it["mhash"], it["paint"],
            " ".join("%016x" % bits(c) for c in it["rgba"]), space_text(it["space"]), it["entry"], it["grad"], ld)
    if k == PLACEHOLDER:
        return "P %s map=%016x %s" % (placeholder_text(it), it["mhash"], ld)
    return "I res=%s:%016x map=%016x %s" % (KIND_NAME[it["rkind"]], it["rhash"], it["mhash"], ld)


def list_dump(items, fg=DEFAULT_FG, ties=(), palette=0):
    lines = ["gshscene.list 1 items=%d ties=%d fg=%s palette=%016x hash=%016x" % (
        len(items), len(ties), color_text(fg[0], fg[1]), palette, list_hash(items, fg, palette))]
    lines += [item_line(it) for it in items]
    for first, second, depth, reason in ties:
        lines.append("T first=%d second=%d depth=%016x reason=%d" % (first, second, bits(depth), reason))
    return "\n".join(lines) + "\n"


# --- order (list.h, N11): an independent statement of how a closed list is put in paint order -----------------------
def parse_units(items, lo, hi):
    """The units of items[lo:hi]: (start, end) with end the matching pop, else start."""
    units = []
    i = lo
    while i < hi:
        end = i
        if items[i]["k"] in (CLIP_PUSH, GROUP_PUSH):
            depth = 0
            j = i
            while True:
                if items[j]["k"] in (CLIP_PUSH, GROUP_PUSH):
                    depth += 1
                elif items[j]["k"] in (CLIP_POP, GROUP_POP):
                    depth -= 1
                if depth == 0:
                    break
                j += 1
            end = j
        units.append((i, end))
        i = end + 1
    return units


def resolve(items):
    """Return (paint-ordered items, ties) for a list recorded in declared order."""
    out = []
    ties = []

    def sequence(lo, hi, ctx):
        units = parse_units(items, lo, hi)
        if ctx == ORDER_DEPTH:
            units = [u for _, u in sorted(((i, u) for i, u in enumerate(units)),
                                          key=lambda e: (-items[e[1][0]]["depth"], items[e[1][0]]["layer"], e[0]))]
        elif ctx == ORDER_OVERLAY:
            units = [u for _, u in sorted(((i, u) for i, u in enumerate(units)),
                                          key=lambda e: (items[e[1][0]]["layer"], e[0]))]
        previous = None
        for start, end in units:
            here = len(out)
            if ctx == ORDER_DEPTH and previous is not None:
                before = items[previous[0]]
                after = items[start]
                if before["depth"] == after["depth"]:
                    ties.append((previous[1], here, after["depth"],
                                 TIE_LAYER if before["layer"] != after["layer"] else TIE_DECLARED))
            previous = (start, here)
            out.append(items[start])
            if end != start:
                inner = items[start]["order"] if items[start]["k"] == GROUP_PUSH else ctx
                sequence(start + 1, end, inner)
                out.append(items[end])

    sequence(0, len(items), ORDER_FLAT)
    ties.sort(key=lambda t: t[1])
    return out, ties


BLACK = (0.0, 0.0, 0.0, 1.0)
PICTURE_ITEMS = [
    fill(KIND_PATH, SQ_RES, IDENTITY, 0, BLACK),
    group_push(0.5),
    fill(KIND_PATH, P2_RES, TRANSLATE, 1, (1.0, 0.5, 0.25, 0.75), layer=2),
    GROUP_POP_ITEM,
]
PICTURE_LIST_HASH = list_hash(PICTURE_ITEMS)
PICTURE_RES = fnv(u32(1) + u32(KIND_PICTURE) + u64(PICTURE_LIST_HASH))

LIST_FG = ((0.1, 0.2, 0.3, 0.5), SRGB_LINEAR)
LIST_ITEMS = [
    fill(KIND_PATH, SQ_RES, DESIGN, 0, (0.25, 0.5, 0.75, 1.0), space=ADOBE_GAMMA, layer=-3, depth=0.5),
    fill(KIND_RUN, FONT_RUN_HASH, IDENTITY, 1, BLACK),
    clip_push(KIND_PATH, P2_RES, TRANSLATE, 1, layer=4, depth=-2.0),
    group_push(0.5, blend=SRGB_LINEAR, composite=COMPOSITE["src-in"], layer=7, depth=1.5),
    instance(KIND_PICTURE, PICTURE_RES, TRANSLATE, layer=1, depth=3.25),
    fill(KIND_RUN, OUTLINE_RUN_HASH, IDENTITY, 0, (2.0, -0.5, 0.0, 0.5), layer=5),
    fill(KIND_PATH, SQ_RES, IDENTITY, 0, (0.0, 0.0, 0.0, 0.75), paint=PAINT_FOREGROUND, layer=6),
    fill(KIND_PATH, P2_RES, IDENTITY, 1, (0.0, 0.0, 0.0, 0.5), paint=PAINT_PALETTE, entry=2, layer=8),
    GROUP_POP_ITEM,
    CLIP_POP_ITEM,
]
LIST_HASH = list_hash(LIST_ITEMS, LIST_FG, PALETTE_HASH)

# A list of gradient fills (test_gradient.cpp): each gradient once, the linear one twice, alphas of their own.
GRAD_LIST_ITEMS = [
    fill(KIND_PATH, SQ_RES, IDENTITY, 0, (0.0, 0.0, 0.0, 1.0), paint=PAINT_GRADIENT, grad=GRAD_LINEAR_HASH),
    fill(KIND_PATH, P2_RES, TRANSLATE, 1, (0.0, 0.0, 0.0, 0.5), paint=PAINT_GRADIENT, grad=GRAD_RADIAL_HASH, layer=2),
    fill(KIND_PATH, SQ_RES, DESIGN, 0, (0.0, 0.0, 0.0, 0.25), paint=PAINT_GRADIENT, grad=GRAD_SWEEP_HASH),
    fill(KIND_PATH, SQ_RES, IDENTITY, 0, (0.0, 0.0, 0.0, 0.0), paint=PAINT_GRADIENT, grad=GRAD_LINEAR_HASH, depth=1.0),
]
GRAD_LIST_HASH = list_hash(GRAD_LIST_ITEMS)

# Blending groups (B1, B14): the u8 composite holds 13 to 27 in the same spelling (test_list.cpp records it).
BLEND_LIST_ITEMS = [
    fill(KIND_PATH, SQ_RES, IDENTITY, 0, (0.25, 0.5, 0.75, 1.0)),
    group_push(0.5, composite=COMPOSITE["screen"]),
    fill(KIND_PATH, P2_RES, TRANSLATE, 1, (1.0, 0.0, 0.5, 0.75)),
    GROUP_POP_ITEM,
    group_push(1.0, blend=SRGB_LINEAR, composite=COMPOSITE["luminosity"], layer=3, depth=-1.0),
    group_push(0.25, composite=COMPOSITE["soft-light"], layer=2, depth=1.0),
    fill(KIND_PATH, SQ_RES, IDENTITY, 0, (-0.5, 1.5, 0.5, 0.5)),
    GROUP_POP_ITEM,
    GROUP_POP_ITEM,
    group_push(1.0, composite=COMPOSITE["multiply"]),
    GROUP_POP_ITEM,
]
BLEND_LIST_HASH = list_hash(BLEND_LIST_ITEMS)

# PDF's group fields (T1): a non-isolated group, a knockout multiply group and a group whose soft mask is the picture
# fixture; and a default group after them, spelled as every group was before the fields.
PDF_LIST_ITEMS = [
    fill(KIND_PATH, SQ_RES, IDENTITY, 0, (0.25, 0.5, 0.75, 1.0)),
    group_push(1.0, isolation=1),
    fill(KIND_PATH, P2_RES, TRANSLATE, 1, (1.0, 0.0, 0.5, 0.75)),
    group_push(0.5, composite=COMPOSITE["multiply"], knockout=1, layer=2, depth=1.0),
    group_push(1.0, blend=SRGB_LINEAR, mask=PICTURE_RES),
    GROUP_POP_ITEM,
    GROUP_POP_ITEM,
    GROUP_POP_ITEM,
    group_push(0.75, isolation=1, knockout=1, mask=PICTURE_RES),
    GROUP_POP_ITEM,
    group_push(0.25),
    GROUP_POP_ITEM,
]
PDF_LIST_HASH = list_hash(PDF_LIST_ITEMS)

# The order fixture: recorded in declared order (ORDER_ITEMS_DECLARED); the generator puts it in paint order.
# A depth group holding: a (depth 1), b (depth 2, layer 1), c (depth 2, layer 0), d (depth 2, layer 0: declared
# after c), e (depth 3), a clip block (depth 2, layer 0) around f, an overlay group (depth 0) with fills of layers
# 3, 1, 3, 2; then a flat fill; and a flat group inside the depth group (depth 2, layer 5) holding two fills.
def orderfill(rgb_id, layer=0, depth=0.0):
    return fill(KIND_PATH, SQ_RES, IDENTITY, 0, (rgb_id / 16.0, 0.0, 0.0, 1.0), layer=layer, depth=depth)


ORDER_ITEMS_DECLARED = [
    orderfill(1),
    group_push(1.0, order=ORDER_DEPTH, layer=0, depth=0.0),
    orderfill(2, 0, 1.0),                                   # a
    orderfill(3, 1, 2.0),                                   # b
    orderfill(4, 0, 2.0),                                   # c
    orderfill(5, 0, 2.0),                                   # d
    orderfill(6, 0, 3.0),                                   # e
    clip_push(KIND_PATH, P2_RES, TRANSLATE, 1, layer=0, depth=2.0),
    orderfill(7),                                           # f, in the clip block
    CLIP_POP_ITEM,
    group_push(1.0, order=ORDER_OVERLAY, layer=0, depth=0.0),
    orderfill(8, 3), orderfill(9, 1), orderfill(10, 3), orderfill(11, 2),
    GROUP_POP_ITEM,
    group_push(0.5, layer=5, depth=2.0),
    orderfill(12), orderfill(13),
    GROUP_POP_ITEM,
    GROUP_POP_ITEM,
    orderfill(14),
    group_push(1.0, order=ORDER_DEPTH),                     # a second depth group whose tie is at depth 0
    orderfill(15), orderfill(16),
    GROUP_POP_ITEM,
]
ORDER_ITEMS, ORDER_TIES = resolve(ORDER_ITEMS_DECLARED)
ORDER_LIST_HASH = list_hash(ORDER_ITEMS)


# --- versions (version.h) ---------------------------------------------------------------------------------
NODE_GROUP, NODE_SHAPE, NODE_INSTANCE, NODE_STROKE, NODE_PLACEHOLDER = 1, 2, 3, 4, 5
DOMAIN = 0x5356470000000001
SCENE_FG = ((1.0, 0.5, 0.25, 0.75), P3_GAMMA)


def key_bytes(key):
    if key is None:
        return u8(0)
    return u8(1) + u64(key[0]) + u64(key[1])


def node_bytes(n):
    out = u64(n["id"]) + u8(n["kind"]) + u8(n["flags"]) + u64(n["parent"]) + key_bytes(n["key"])
    if n["kind"] == NODE_GROUP:
        out += u64(n["map"]) + f64(n["opacity"]) + space_bytes(n["blend"])
        out += composite_bytes(n["composite"], n.get("isolation", 0), n.get("knockout", 0), n.get("mask", 0))
        out += u32(n["order"]) + i32(n["layer"]) + f64(n["depth"])
        clip = n["clip"]
        if clip is None:
            out += u8(0)
        else:
            out += u8(1) + u64(clip[1]) + u64(clip[2]) + u8(clip[3])
        out += u64(len(n["children"])) + b"".join(u64(c) for c in n["children"])
    elif n["kind"] in (NODE_SHAPE, NODE_STROKE):
        # SK12: a stroke is spelled as a shape with the style's hash after the resource's.
        out += u64(n["res"][1]) + (u64(n["style"]) if n["kind"] == NODE_STROKE else b"")
        out += u64(n["map"]) + u8(n["rule"]) + u8(n["paint"])
        out += b"".join(f64(c) for c in n["rgba"]) + space_bytes(n["space"]) + u32(n["entry"]) + u64(n["grad"])
        out += i32(n["layer"]) + f64(n["depth"])
    elif n["kind"] == NODE_PLACEHOLDER:
        # PH7: a list item's spelling.
        out += placeholder_bytes(n) + u64(n["map"]) + i32(n["layer"]) + f64(n["depth"])
    else:
        out += u64(n["target"]) + u64(n["map"]) + i32(n["layer"]) + f64(n["depth"])
    return out


def version_hash(nodes, fg=DEFAULT_FG, palette=0):
    out = u32(1) + u32(0x4E454353) + color_bytes(fg[0], fg[1]) + u64(palette) + u64(len(nodes))
    for n in nodes:
        out += node_bytes(n)
    return fnv(out)


def key_text(key):
    return "-" if key is None else "%016x:%016x" % key


def node_line(n):
    head = "%s id=%d parent=%d flags=%d key=%s" % ("GSIKP"[n["kind"] - 1], n["id"], n["parent"], n["flags"], key_text(n["key"]))
    if n["kind"] == NODE_GROUP:
        clip = n["clip"]
        clip_text = "-" if clip is None else "%s:%016x:%016x:%d" % (KIND_NAME[clip[0]], clip[1], clip[2], clip[3])
        children = ",".join(str(c) for c in n["children"]) if n["children"] else "-"
        return "%s map=%016x opacity=%016x blend=%s composite=%d order=%d%s layer=%d depth=%016x clip=%s children=%s" % (
            head, n["map"], bits(n["opacity"]), space_text(n["blend"]), n["composite"], n["order"],
            pdf_fields_text(n.get("isolation", 0), n.get("knockout", 0), n.get("mask", 0)), n["layer"],
            bits(n["depth"]), clip_text, children)
    if n["kind"] in (NODE_SHAPE, NODE_STROKE):
        style = " style=%016x" % n["style"] if n["kind"] == NODE_STROKE else ""
        return "%s res=%s:%016x%s map=%016x rule=%d paint=%d %s space=%s entry=%d resource=%016x layer=%d depth=%016x" % (
            head, KIND_NAME[n["res"][0]], n["res"][1], style, n["map"], n["rule"], n["paint"],
            " ".join("%016x" % bits(c) for c in n["rgba"]), space_text(n["space"]), n["entry"], n["grad"], n["layer"],
            bits(n["depth"]))
    if n["kind"] == NODE_PLACEHOLDER:
        return "%s %s map=%016x layer=%d depth=%016x" % (head, placeholder_text(n), n["map"], n["layer"], bits(n["depth"]))
    return "%s target=%d map=%016x layer=%d depth=%016x" % (head, n["target"], n["map"], n["layer"], bits(n["depth"]))


def version_dump(nodes, fg=DEFAULT_FG, palette=0):
    lines = ["gshscene.version 1 nodes=%d root=1 fg=%s palette=%016x hash=%016x" % (
        len(nodes), color_text(fg[0], fg[1]), palette, version_hash(nodes, fg, palette))]
    lines += [node_line(n) for n in nodes]
    return "\n".join(lines) + "\n"


def group_node(i, parent, children, **kw):
    n = dict(id=i, kind=NODE_GROUP, flags=0, parent=parent, key=None, map=IDENTITY, opacity=1.0, blend=TARGET,
             composite=0, order=ORDER_FLAT, layer=0, depth=0.0, clip=None, children=children)
    n.update(kw)
    return n


def shape_node(i, parent, res, **kw):
    n = dict(id=i, kind=NODE_SHAPE, flags=0, parent=parent, key=None, res=res, map=IDENTITY, rule=0, paint=PAINT_SOLID,
             rgba=BLACK, space=TARGET, entry=0, grad=0, layer=0, depth=0.0)
    n.update(kw)
    return n


def stroke_node(i, parent, res, style, **kw):
    n = shape_node(i, parent, res, **kw)
    n["kind"] = NODE_STROKE
    n["style"] = style
    return n


def placeholder_node(i, parent, source, code, approx=0, box=None, **kw):
    n = dict(id=i, kind=NODE_PLACEHOLDER, flags=0, parent=parent, key=None, source=source, code=code, approx=approx,
             box=box, map=IDENTITY, layer=0, depth=0.0)
    n.update(kw)
    return n


def instance_node(i, parent, target, **kw):
    n = dict(id=i, kind=NODE_INSTANCE, flags=0, parent=parent, key=None, target=target, map=IDENTITY, layer=0,
             depth=0.0)
    n.update(kw)
    return n


SCENE_NODES = [
    group_node(1, 0, [2, 4, 7]),
    group_node(2, 1, [3], key=(DOMAIN, 42), map=TRANSLATE, opacity=0.5, layer=3, depth=-1.25,
               blend=SRGB_LINEAR, composite=COMPOSITE["xor"], order=ORDER_DEPTH,
               clip=(KIND_PATH, P2_RES, DESIGN, 1)),
    shape_node(3, 2, (KIND_PATH, SQ_RES), rgba=(0.25, 0.5, 0.75, 1.0), layer=-1, depth=2.5, space=ADOBE_GAMMA),
    group_node(4, 1, [5, 6], flags=1),
    shape_node(5, 4, (KIND_RUN, OUTLINE_RUN_HASH), key=(1, 2), map=TRANSLATE, rule=1, rgba=(2.0, -0.5, 0.0, 0.5), layer=5),
    shape_node(6, 4, (KIND_RUN, FONT_RUN_HASH), paint=PAINT_FOREGROUND, rgba=(0.0, 0.0, 0.0, 0.25)),
    instance_node(7, 1, 4, map=DESIGN, layer=9, depth=-4.5),
    shape_node(8, 1, (KIND_PATH, P2_RES), paint=PAINT_PALETTE, entry=65534, rgba=(0.0, 0.0, 0.0, 1.0), layer=2),
]
SCENE_NODES[0]["children"].append(8)
SCENE_HASH = version_hash(SCENE_NODES, SCENE_FG, PALETTE_HASH)
GRAD_SCENE_NODES = [
    group_node(1, 0, [2]),
    shape_node(2, 1, (KIND_PATH, SQ_RES), paint=PAINT_GRADIENT, rgba=(0.0, 0.0, 0.0, 0.5), grad=GRAD_RADIAL_HASH),
]
GRAD_SCENE_HASH = version_hash(GRAD_SCENE_NODES)
PDF_SCENE_NODES = [
    group_node(1, 0, [2, 4]),
    group_node(2, 1, [3], isolation=1, knockout=1, mask=PICTURE_RES, opacity=0.5, layer=1, depth=2.0),
    shape_node(3, 2, (KIND_PATH, SQ_RES), rgba=(0.25, 0.5, 0.75, 1.0)),
    group_node(4, 1, [], knockout=1, composite=COMPOSITE["hue"]),
]
PDF_SCENE_HASH = version_hash(PDF_SCENE_NODES)
# Strokes (SK4, SK12, SK13, SK14; strokes story S1): a list holding an element's fill then its stroke, and strokes of
# a run from outlines, of the fixture path and of a run from a font, in the paints a stroke takes; and a scene of a
# shape, a stroke of the same path after it, and a keyed stroke of a run, with the list it emits.
STROKE_LIST_ITEMS = [
    fill(KIND_PATH, SQ_RES, IDENTITY, 0, (0.25, 0.5, 0.75, 1.0)),
    stroke(KIND_PATH, SQ_RES, STYLE_DEFAULT_HASH, IDENTITY, BLACK),
    stroke(KIND_RUN, OUTLINE_RUN_HASH, STYLE_DEVICE_HASH, TRANSLATE, (0.0, 0.0, 0.0, 0.5), paint=PAINT_FOREGROUND,
           layer=2, depth=1.5),
    stroke(KIND_PATH, P2_RES, STYLE_ARCS_HASH, DESIGN, (0.0, 0.0, 0.0, 0.75), paint=PAINT_PALETTE, entry=1, layer=-1),
    stroke(KIND_RUN, FONT_RUN_HASH, STYLE_CLIP_HASH, IDENTITY, (0.0, 0.0, 0.0, 1.0), paint=PAINT_GRADIENT,
           grad=GRAD_RADIAL_HASH),
    stroke(KIND_PATH, SQ_RES, STYLE_DEFAULT_HASH, IDENTITY, (2.0, -0.5, 0.25, 0.5), space=P3_GAMMA, depth=-3.0),
]
STROKE_LIST_HASH = list_hash(STROKE_LIST_ITEMS)
STROKE_SCENE_NODES = [
    group_node(1, 0, [2, 3, 4]),
    shape_node(2, 1, (KIND_PATH, SQ_RES), rgba=(0.25, 0.5, 0.75, 1.0)),
    stroke_node(3, 1, (KIND_PATH, SQ_RES), STYLE_DEFAULT_HASH),
    stroke_node(4, 1, (KIND_RUN, OUTLINE_RUN_HASH), STYLE_DEVICE_HASH, key=(DOMAIN, 7), map=TRANSLATE,
                paint=PAINT_FOREGROUND, rgba=(0.0, 0.0, 0.0, 0.5), layer=3, depth=1.0),
]
STROKE_SCENE_HASH = version_hash(STROKE_SCENE_NODES)
STROKE_SCENE_LIST_HASH = list_hash([
    fill(KIND_PATH, SQ_RES, IDENTITY, 0, (0.25, 0.5, 0.75, 1.0)),
    stroke(KIND_PATH, SQ_RES, STYLE_DEFAULT_HASH, IDENTITY, BLACK),
    stroke(KIND_RUN, OUTLINE_RUN_HASH, STYLE_DEVICE_HASH, TRANSLATE, (0.0, 0.0, 0.0, 0.5), paint=PAINT_FOREGROUND,
           layer=3, depth=1.0),
])
# Placeholders (PH1, PH7; color-paint story P5): a list holding every shape of one -- a refused one with a box, an
# approximated one after a fill (a stand-in), one without a box, a negative-zero edge, a layer and a depth, a map -- and
# a scene of a group holding a placeholder under a transformed group, a keyed approximated one and a hidden one, with
# the list it emits (the node's composed map is the item's, so the transformed group's map is the translation).
PLACEHOLDER_LIST_ITEMS = [
    fill(KIND_PATH, SQ_RES, IDENTITY, 0, (0.25, 0.5, 0.75, 1.0)),
    placeholder(1, 1, 0, (-1.5, 0.0, 4.25, 6.0), IDENTITY),
    placeholder(1, 3, 1, (0.0, 0.0, 2.0, 2.0), TRANSLATE, layer=2, depth=1.5),
    fill(KIND_PATH, SQ_RES, IDENTITY, 0, (0.0, 0.0, 0.0, 1.0), paint=PAINT_FOREGROUND, layer=2, depth=1.5),
    placeholder(1, 2, 0, None, DESIGN, layer=-1, depth=-2.5),
    placeholder(65535, 65535, 1, (0.0, 0.0, 0.0, 0.0), IDENTITY),
]
PLACEHOLDER_LIST_HASH = list_hash(PLACEHOLDER_LIST_ITEMS)
PLACEHOLDER_SCENE_NODES = [
    group_node(1, 0, [2, 4, 5]),
    group_node(2, 1, [3], map=TRANSLATE),
    placeholder_node(3, 2, 1, 1, box=(0.0, 0.0, 8.0, 8.0), layer=1, depth=0.5),
    placeholder_node(4, 1, 1, 3, approx=1, key=(DOMAIN, 9), map=DESIGN, box=(-1.0, -2.0, 3.0, 4.0)),
    placeholder_node(5, 1, 1, 2, flags=1),
]
PLACEHOLDER_SCENE_HASH = version_hash(PLACEHOLDER_SCENE_NODES)
PLACEHOLDER_SCENE_LIST_HASH = list_hash([
    placeholder(1, 1, 0, (0.0, 0.0, 8.0, 8.0), TRANSLATE, layer=1, depth=0.5),
    placeholder(1, 3, 1, (-1.0, -2.0, 3.0, 4.0), DESIGN),
])
EMPTY_NODES = [group_node(1, 0, [])]
EMPTY_HASH = version_hash(EMPTY_NODES)


# --- scene B: every hash and digit run that can have a leading zero has one (version.h's widths) ------------------------
def zero_led_translations(count):
    """Translations whose map hashes start with a zero nibble, so a dump that prints %llx for %016llx is seen."""
    out = []
    a = 1.0
    while len(out) < count:
        h = map_hash(2, [1, 0, 0, 1], [a, -a], 1)
        if h >> 60 == 0:
            out.append((a, -a, h))
        a += 0.25
    return out


B_T = zero_led_translations(4)


def scene_b(layer):
    return [
        group_node(1, 0, [2, 3, 4]),
        group_node(2, 1, [], map=B_T[0][2], opacity=0.0, clip=(KIND_PATH, P2_RES, B_T[1][2], 0)),
        shape_node(3, 1, (KIND_PATH, P2_RES), map=B_T[2][2], rgba=(0.0, 0.0, 0.0, 0.0)),
        instance_node(4, 1, 2, map=B_T[3][2], layer=layer),
    ]


B_LAYER = 0
while version_hash(scene_b(B_LAYER)) >> 60 != 0:
    B_LAYER += 1
SCENE_B = scene_b(B_LAYER)


def path_key(ids):
    return fnv(u64(len(ids)) + b"".join(u64(i) for i in ids))


# --- images (image.h, IM1 to IM12; masks-images-patterns story I1) ----------------------------------------------
# Every tile hash and image hash below is restated here from image.h's text with `struct` alone.  The samples come from
# one formula, which tests/unit/test_image.cpp repeats: a sample at column x, row y, channel c is
#   8 bits:   (7x + 13y + 29c + 1) mod 256
#   12 bits:  (37x + 101y + 263c + 5) mod 4096
#   16 bits:  (257x + 911y + 4099c + 0x1234) mod 65536   (its two bytes differ, so a byte order shows)
#   float:    ((7x + 3y + 5c) mod 61) / 64 as an IEEE binary32 (a dyadic number: exact, and in [0, 1])
KIND_IMAGE = 5
IMAGE_TILE = 64
IMAGE_TILE_MAGIC = 0x454C4954
LAYOUT_GRAY, LAYOUT_GRAY_ALPHA, LAYOUT_RGB, LAYOUT_RGBA = 1, 2, 3, 4
T_UNORM, T_FLOAT = 0, 1
A_NONE, A_STRAIGHT, A_PREMULTIPLIED = 0, 1, 2


def image_sample(bits, x, y, c):
    """One sample as its value: an integer for UNORM, the binary32's bit pattern for float."""
    if bits == 8:
        return (7 * x + 13 * y + 29 * c + 1) % 256
    if bits == 12:
        return (37 * x + 101 * y + 263 * c + 5) % 4096
    if bits == 16:
        return (257 * x + 911 * y + 4099 * c + 0x1234) % 65536
    assert bits == 32
    return struct.unpack("<I", struct.pack("<f", ((7 * x + 3 * y + 5 * c) % 61) / 64.0))[0]


def sample_bytes(bits, v):
    return u8(v) if bits == 8 else u16(v) if bits < 32 else u32(v)


def image_tile_hash(w, h, layout, bits, tx, ty):
    x0, y0 = tx * IMAGE_TILE, ty * IMAGE_TILE
    x1, y1 = min(x0 + IMAGE_TILE, w), min(y0 + IMAGE_TILE, h)
    out = u32(1) + u32(IMAGE_TILE_MAGIC) + u32(x1 - x0) + u32(y1 - y0)
    for y in range(y0, y1):
        for x in range(x0, x1):
            for c in range(layout):
                out += sample_bytes(bits, image_sample(bits, x, y, c))
    return fnv(out)


def image_case(name, w, h, layout, kind, bits, alpha, alphaonly=0, space=TARGET):
    tx_n, ty_n = (w + IMAGE_TILE - 1) // IMAGE_TILE, (h + IMAGE_TILE - 1) // IMAGE_TILE
    tiles = [image_tile_hash(w, h, layout, bits, tx, ty) for ty in range(ty_n) for tx in range(tx_n)]
    out = u32(1) + u32(KIND_IMAGE) + u32(w) + u32(h) + u8(layout) + u8(kind) + u8(bits) + u8(alpha) + u8(alphaonly)
    out += space_bytes(space) + u32(IMAGE_TILE) + u64(len(tiles)) + b"".join(u64(t) for t in tiles)
    digest = fnv(out)
    lines = ["gshscene.resource 1 kind=image hash=%016x" % digest,
             "image w=%d h=%d layout=%s type=%s bits=%d alpha=%s alphaonly=%d space=%s tile=%d tiles=%dx%d" % (
                 w, h, {1: "gray", 2: "gray-alpha", 3: "rgb", 4: "rgba"}[layout], "float" if kind else "unorm", bits,
                 {0: "none", 1: "straight", 2: "premultiplied"}[alpha], alphaonly, space_text(space), IMAGE_TILE,
                 tx_n, ty_n)]
    for ty in range(ty_n):
        lines.append("t %d %s" % (ty, " ".join("%016x" % tiles[ty * tx_n + tx] for tx in range(tx_n))))
    return dict(name=name, w=w, h=h, layout=layout, kind=kind, bits=bits, alpha=alpha, alphaonly=alphaonly, space=space,
                hash=digest, tx=tx_n, ty=ty_n, tiles=tiles, dump="\n".join(lines) + "\n")


# One case per layout x sample type x alpha form, with partial edge tiles on both axes, a multiple of 64 and a single texel.
IMAGE_CASES = [
    image_case("gray8-1x1", 1, 1, LAYOUT_GRAY, T_UNORM, 8, A_NONE),
    image_case("gray8-alpha-only-65x65", 65, 65, LAYOUT_GRAY, T_UNORM, 8, A_NONE, alphaonly=1),
    image_case("rgb8-64x64", 64, 64, LAYOUT_RGB, T_UNORM, 8, A_NONE),
    image_case("rgba8-straight-65x129", 65, 129, LAYOUT_RGBA, T_UNORM, 8, A_STRAIGHT),
    image_case("rgba8-premultiplied-63x63", 63, 63, LAYOUT_RGBA, T_UNORM, 8, A_PREMULTIPLIED),
    image_case("gray12-130x3", 130, 3, LAYOUT_GRAY, T_UNORM, 12, A_NONE),
    image_case("rgb12-70x66", 70, 66, LAYOUT_RGB, T_UNORM, 12, A_NONE),
    image_case("rgba12-straight-5x5", 5, 5, LAYOUT_RGBA, T_UNORM, 12, A_STRAIGHT),
    image_case("gray16-129x1", 129, 1, LAYOUT_GRAY, T_UNORM, 16, A_NONE),
    image_case("rgb16-65x64", 65, 64, LAYOUT_RGB, T_UNORM, 16, A_NONE),
    image_case("rgba16-premultiplied-3x70", 3, 70, LAYOUT_RGBA, T_UNORM, 16, A_PREMULTIPLIED),
    image_case("grayf32-65x65", 65, 65, LAYOUT_GRAY, T_FLOAT, 32, A_NONE),
    image_case("rgbf32-64x65", 64, 65, LAYOUT_RGB, T_FLOAT, 32, A_NONE),
    image_case("rgbaf32-straight-66x2", 66, 2, LAYOUT_RGBA, T_FLOAT, 32, A_STRAIGHT),
    image_case("rgbaf32-premultiplied-2x66", 2, 66, LAYOUT_RGBA, T_FLOAT, 32, A_PREMULTIPLIED),
    image_case("rgb8-srgb-linear-3x3", 3, 3, LAYOUT_RGB, T_UNORM, 8, A_NONE, space=SRGB_LINEAR),
    image_case("rgba16-p3-gamma-4x4", 4, 4, LAYOUT_RGBA, T_UNORM, 16, A_STRAIGHT, space=P3_GAMMA),
]


# --- image paints (image_paint.h, IP1 and IP13; masks-images-patterns story I2) ----------------------------------------
KIND_IMAGE_PAINT = 10
PAINT_IMAGE = 4
EXT_PAD, EXT_REPEAT, EXT_REFLECT, EXT_NONE = 0, 1, 2, 3
F_NEAREST, F_LINEAR, F_CUBIC, F_AREA = 0, 1, 2, 3


def image_hash_of(name):
    return [c for c in IMAGE_CASES if c["name"] == name][0]["hash"]


def image_paint(image, window, mhash, ex, ey, mag, mn, fspace=TARGET, tint=None):
    """A tint is (paint kind, rgba, space, entry).  The window is explicit (the store keeps the whole image spelled out)."""
    out = u32(1) + u32(KIND_IMAGE_PAINT) + u64(image) + b"".join(u32(v) for v in window) + u64(mhash)
    out += u8(ex) + u8(ey) + u8(mag) + u8(mn) + space_bytes(fspace)
    if tint is None:
        out += u8(0)
        tint_text = "-"
    else:
        kind, rgba, space, entry = tint
        out += u8(1) + u8(kind) + b"".join(f64(v) for v in rgba) + space_bytes(space) + u32(entry)
        tint_text = "%d %s %s" % (kind, " ".join("%016x" % bits(v) for v in rgba), space_text(space)) + " %d" % entry
    digest = fnv(out)
    dump = "gshscene.resource 1 kind=image-paint hash=%016x\nimagepaint image=%016x window=%d %d %d %d map=%016x extend=%d,%d filter=%d,%d space=%s tint=%s\n" % (
        digest, image, window[0], window[1], window[2], window[3], mhash, ex, ey, mag, mn, space_text(fspace), tint_text)
    return dict(hash=digest, dump=dump)


IMAGE_PAINTS = [
    image_paint(image_hash_of("rgba8-straight-65x129"), (0, 0, 65, 129), IDENTITY, EXT_PAD, EXT_PAD, F_LINEAR, F_LINEAR),
    image_paint(image_hash_of("gray8-alpha-only-65x65"), (1, 2, 40, 50), TRANSLATE, EXT_REPEAT, EXT_REFLECT, F_NEAREST, F_AREA,
                tint=(PAINT_SOLID, (0.25, 0.5, 0.75, 1.0), SRGB_SRGB, 0)),
    image_paint(image_hash_of("rgb12-70x66"), (4, 4, 66, 60), DESIGN, EXT_NONE, EXT_REFLECT, F_CUBIC, F_AREA, fspace=SRGB_LINEAR),
    image_paint(image_hash_of("gray8-alpha-only-65x65"), (0, 0, 65, 65), IDENTITY, EXT_PAD, EXT_PAD, F_LINEAR, F_NEAREST,
                tint=(PAINT_FOREGROUND, (0.0, 0.0, 0.0, 0.5), TARGET, 0)),
    image_paint(image_hash_of("gray8-alpha-only-65x65"), (0, 0, 65, 65), IDENTITY, EXT_PAD, EXT_PAD, F_LINEAR, F_NEAREST,
                tint=(PAINT_PALETTE, (0.0, 0.0, 0.0, 1.0), TARGET, 2)),
    image_paint(image_hash_of("rgbaf32-premultiplied-2x66"), (0, 0, 2, 66), TRANSLATE, EXT_REFLECT, EXT_REPEAT, F_AREA, F_CUBIC,
                fspace=P3_GAMMA),
]
IMAGE_PAINT_LIST_ITEMS = [
    fill(KIND_PATH, SQ_RES, IDENTITY, 0, (0.0, 0.0, 0.0, 1.0), paint=PAINT_IMAGE, grad=IMAGE_PAINTS[0]["hash"]),
    fill(KIND_PATH, P2_RES, TRANSLATE, 1, (0.0, 0.0, 0.0, 0.5), paint=PAINT_IMAGE, grad=IMAGE_PAINTS[1]["hash"], layer=2),
    fill(KIND_PATH, SQ_RES, DESIGN, 0, (0.0, 0.0, 0.0, 0.25), paint=PAINT_IMAGE, grad=IMAGE_PAINTS[2]["hash"]),
    fill(KIND_PATH, SQ_RES, IDENTITY, 0, (0.0, 0.0, 0.0, 0.0), paint=PAINT_IMAGE, grad=IMAGE_PAINTS[0]["hash"], depth=1.0),
]
IMAGE_PAINT_LIST_HASH = list_hash(IMAGE_PAINT_LIST_ITEMS)


def cxx_string(name, text):
    return 'static const char * const %s =\nR"DUMP(%s)DUMP";\n' % (name, text)


def render():
    o = []
    o.append("// Generated by tools/gen-scene-vectors.py from the spellings in resource.h and list.h.\n")
    o.append("// Do not edit; run the generator.  Independent of the library's code.\n")
    o.append("#ifndef GHOTI_IO_GSHSCENE_TEST_KNOWN_ANSWERS_H\n#define GHOTI_IO_GSHSCENE_TEST_KNOWN_ANSWERS_H\n\n#include <cstdint>\n\n")
    o.append("namespace known {\n\n")
    for name, v in (("kSquareResourceHash", SQ_RES), ("kFixturePathResourceHash", P2_RES),
                    ("kFaceHash", FACE_HASH), ("kFontRunHash", FONT_RUN_HASH), ("kOutlineRunHash", OUTLINE_RUN_HASH),
                    ("kPaletteHash", PALETTE_HASH), ("kPalette8Hash", PALETTE8_HASH),
                    ("kPictureListHash", PICTURE_LIST_HASH), ("kPictureHash", PICTURE_RES),
                    ("kListHash", LIST_HASH), ("kOrderListHash", ORDER_LIST_HASH), ("kMapIdentityHash", IDENTITY),
                    ("kMapTranslateHash", TRANSLATE), ("kMapDesignHash", DESIGN),
                    ("kSceneHash", SCENE_HASH), ("kEmptySceneHash", EMPTY_HASH), ("kSceneBHash", version_hash(SCENE_B)),
                    ("kPathKeyEmpty", path_key([])), ("kPathKeyOne", path_key([1])),
                    ("kPathKeyTwo", path_key([4, 5])), ("kPathKeyThree", path_key([7, 7, 3])),
                    ("kGradientLinearHash", GRAD_LINEAR_HASH), ("kGradientRadialHash", GRAD_RADIAL_HASH),
                    ("kGradientSweepHash", GRAD_SWEEP_HASH), ("kGradientListHash", GRAD_LIST_HASH),
                    ("kGradientSceneHash", GRAD_SCENE_HASH), ("kBlendListHash", BLEND_LIST_HASH),
                    ("kPdfGroupListHash", PDF_LIST_HASH), ("kPdfGroupSceneHash", PDF_SCENE_HASH),
                    ("kStrokeDefaultHash", STYLE_DEFAULT_HASH), ("kStrokeDeviceHash", STYLE_DEVICE_HASH),
                    ("kStrokeClipHash", STYLE_CLIP_HASH), ("kStrokeArcsHash", STYLE_ARCS_HASH),
                    ("kPlaceholderListHash", PLACEHOLDER_LIST_HASH), ("kPlaceholderSceneHash", PLACEHOLDER_SCENE_HASH),
                    ("kPlaceholderSceneListHash", PLACEHOLDER_SCENE_LIST_HASH),
                    ("kStrokeListHash", STROKE_LIST_HASH), ("kStrokeSceneHash", STROKE_SCENE_HASH),
                    ("kStrokeSceneListHash", STROKE_SCENE_LIST_HASH)):
        o.append("static const std::uint64_t %s = UINT64_C(0x%016x);\n" % (name, v))
    o.append("static const int kSceneBLayer = %d;\n" % B_LAYER)
    o.append("static const double kSceneBTranslate[4][2] = {%s};\n" % ", ".join("{%r, %r}" % (t[0], t[1]) for t in B_T))
    o.append("\n")
    o.append(cxx_string("kSquareResourceDump", "gshscene.resource 1 kind=path hash=%016x\n" % SQ_RES + path_dump(SQ)))
    o.append(cxx_string("kFixturePathResourceDump", "gshscene.resource 1 kind=path hash=%016x\n" % P2_RES + path_dump(P2)))
    o.append(cxx_string("kFaceDump", face_dump(*FACE)))
    o.append(cxx_string("kFontRunDump", run_dump(*FONT_RUN)))
    o.append(cxx_string("kOutlineRunDump", run_dump(*OUTLINE_RUN)))
    o.append(cxx_string("kPaletteDump", palette_dump(PALETTE)))
    o.append(cxx_string("kPalette8Dump", palette_dump(PALETTE8)))
    o.append(cxx_string("kPictureDump", "gshscene.resource 1 kind=picture hash=%016x\n" % PICTURE_RES + list_dump(PICTURE_ITEMS)))
    o.append(cxx_string("kListDump", list_dump(LIST_ITEMS, LIST_FG, palette=PALETTE_HASH)))
    o.append(cxx_string("kOrderListDump", list_dump(ORDER_ITEMS, DEFAULT_FG, ORDER_TIES)))
    o.append(cxx_string("kSceneDump", version_dump(SCENE_NODES, SCENE_FG, PALETTE_HASH)))
    o.append(cxx_string("kEmptySceneDump", version_dump(EMPTY_NODES)))
    o.append(cxx_string("kSceneBDump", version_dump(SCENE_B)))
    o.append(cxx_string("kGradientLinearDump", gradient_dump(GRAD_LINEAR)))
    o.append(cxx_string("kGradientRadialDump", gradient_dump(GRAD_RADIAL)))
    o.append(cxx_string("kGradientSweepDump", gradient_dump(GRAD_SWEEP)))
    o.append(cxx_string("kGradientListDump", list_dump(GRAD_LIST_ITEMS)))
    o.append(cxx_string("kGradientSceneDump", version_dump(GRAD_SCENE_NODES)))
    o.append(cxx_string("kBlendListDump", list_dump(BLEND_LIST_ITEMS)))
    o.append(cxx_string("kPdfGroupListDump", list_dump(PDF_LIST_ITEMS)))
    o.append(cxx_string("kPdfGroupSceneDump", version_dump(PDF_SCENE_NODES)))
    o.append(cxx_string("kStrokeDefaultDump", style_dump(STYLE_DEFAULT)))
    o.append(cxx_string("kStrokeDeviceDump", style_dump(STYLE_DEVICE)))
    o.append(cxx_string("kStrokeClipDump", style_dump(STYLE_CLIP)))
    o.append(cxx_string("kStrokeArcsDump", style_dump(STYLE_ARCS)))
    o.append(cxx_string("kStrokeListDump", list_dump(STROKE_LIST_ITEMS)))
    o.append(cxx_string("kStrokeSceneDump", version_dump(STROKE_SCENE_NODES)))
    o.append(cxx_string("kPlaceholderListDump", list_dump(PLACEHOLDER_LIST_ITEMS)))
    o.append(cxx_string("kPlaceholderSceneDump", version_dump(PLACEHOLDER_SCENE_NODES)))
    o.append("struct ImageCase {\n  const char * name;\n  unsigned width, height, layout, type, bits, alpha, alpha_only;\n"
             "  unsigned primaries, transfer;\n  double gamma;\n  std::uint64_t hash;\n  unsigned tiles_x, tiles_y;\n"
             "  const std::uint64_t * tiles;\n  const char * dump;\n};\n\n")
    for i, c in enumerate(IMAGE_CASES):
        o.append("static const std::uint64_t kImageTiles%d[] = {%s};\n" % (i, ", ".join("UINT64_C(0x%016x)" % t for t in c["tiles"])))
    o.append("\n")
    for i, c in enumerate(IMAGE_CASES):
        o.append(cxx_string("kImageDump%d" % i, c["dump"]))
    o.append("\nstatic const ImageCase kImageCases[] = {\n")
    for i, c in enumerate(IMAGE_CASES):
        o.append('    {"%s", %d, %d, %d, %d, %d, %d, %d, %d, %d, %r, UINT64_C(0x%016x), %d, %d, kImageTiles%d, kImageDump%d},\n' % (
            c["name"], c["w"], c["h"], c["layout"], c["kind"], c["bits"], c["alpha"], c["alphaonly"], c["space"][0],
            c["space"][1], c["space"][2], c["hash"], c["tx"], c["ty"], i, i))
    o.append("};\n")
    o.append("static const std::uint64_t kImagePaintHashes[] = {%s};\n" % ", ".join("UINT64_C(0x%016x)" % p["hash"] for p in IMAGE_PAINTS))
    o.append("static const std::uint64_t kImagePaintListHash = UINT64_C(0x%016x);\n\n" % IMAGE_PAINT_LIST_HASH)
    for i, ip in enumerate(IMAGE_PAINTS):
        o.append(cxx_string("kImagePaintDump%d" % i, ip["dump"]))
    o.append(cxx_string("kImagePaintListDump", list_dump(IMAGE_PAINT_LIST_ITEMS)))
    o.append("\n}  // namespace known\n\n#endif\n")
    return "".join(o)


if __name__ == "__main__":
    text = render()
    if len(sys.argv) > 1 and sys.argv[1] == "--check":
        path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tests", "known_answers.h")
        with open(path) as f:
            if f.read() != text:
                sys.exit("gen-scene-vectors: tests/known_answers.h is not what the generator writes")
        print("gen-scene-vectors: tests/known_answers.h is current")
    else:
        sys.stdout.write(text)
