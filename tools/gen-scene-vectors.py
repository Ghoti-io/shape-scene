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
face, a glyph run from that face and one from outlines, a picture, and a list holding every
kind of item, each also in its dump text; and a scene (version.h) holding every kind
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
KIND_PATH, KIND_RUN, KIND_PICTURE, KIND_FACE = 1, 2, 3, 6
KIND_NAME = {KIND_PATH: "path", KIND_RUN: "glyph-run", KIND_PICTURE: "picture", KIND_FACE: "face"}
# The format is per kind (resource.h): a glyph run's is 2 since story 14a, the rest 1.
RUN_FORMAT = 2


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


# --- lists (list.h) ---------------------------------------------------------------
FILL, CLIP_PUSH, CLIP_POP, GROUP_PUSH, GROUP_POP, INSTANCE = 1, 2, 3, 4, 5, 6
PAINT_SOLID, PAINT_FOREGROUND = 0, 2
ORDER_FLAT, ORDER_DEPTH, ORDER_OVERLAY = 0, 1, 2
COMPOSITE = {"src-over": 0, "clear": 1, "src": 2, "dest": 3, "dest-over": 4, "src-in": 5, "dest-in": 6,
             "src-out": 7, "dest-out": 8, "src-atop": 9, "dest-atop": 10, "xor": 11, "plus": 12}
TIE_LAYER, TIE_PATH, TIE_DECLARED = 1, 2, 3
DEFAULT_FG = ((0.0, 0.0, 0.0, 1.0), TARGET)


def fill(rkind, rhash, mhash, rule, rgba, space=TARGET, layer=0, depth=0.0, paint=PAINT_SOLID):
    return dict(k=FILL, rkind=rkind, rhash=rhash, mhash=mhash, rule=rule, rgba=rgba, space=space, layer=layer,
                depth=depth, paint=paint)


def clip_push(rkind, rhash, mhash, rule, layer=0, depth=0.0):
    return dict(k=CLIP_PUSH, rkind=rkind, rhash=rhash, mhash=mhash, rule=rule, layer=layer, depth=depth)


def group_push(opacity, blend=TARGET, composite=0, order=ORDER_FLAT, layer=0, depth=0.0):
    return dict(k=GROUP_PUSH, opacity=opacity, blend=blend, composite=composite, order=order, layer=layer, depth=depth)


def instance(rkind, rhash, mhash, layer=0, depth=0.0):
    return dict(k=INSTANCE, rkind=rkind, rhash=rhash, mhash=mhash, layer=layer, depth=depth)


CLIP_POP_ITEM = dict(k=CLIP_POP)
GROUP_POP_ITEM = dict(k=GROUP_POP)


def item_bytes(it):
    k = it["k"]
    out = u8(k)
    if k == FILL:
        out += u64(it["rhash"]) + u64(it["mhash"]) + u8(it["rule"]) + u8(it["paint"])
        out += color_bytes(it["rgba"], it["space"]) + i32(it["layer"]) + f64(it["depth"])
    elif k == CLIP_PUSH:
        out += u64(it["rhash"]) + u64(it["mhash"]) + u8(it["rule"]) + i32(it["layer"]) + f64(it["depth"])
    elif k == GROUP_PUSH:
        out += f64(it["opacity"]) + space_bytes(it["blend"]) + u8(it["composite"]) + u32(it["order"])
        out += i32(it["layer"]) + f64(it["depth"])
    elif k == INSTANCE:
        out += u64(it["rhash"]) + u64(it["mhash"]) + i32(it["layer"]) + f64(it["depth"])
    return out


def list_hash(items, fg=DEFAULT_FG):
    out = u32(2) + u32(0x5453494C) + color_bytes(fg[0], fg[1]) + u64(len(items))
    for it in items:
        out += item_bytes(it)
    return fnv(out)


def item_line(it):
    k = it["k"]
    ld = "layer=%d depth=%016x" % (it["layer"], bits(it["depth"])) if k != CLIP_POP and k != GROUP_POP else ""
    if k == FILL:
        return "F res=%s:%016x map=%016x rule=%d paint=%d %s space=%s %s" % (
            KIND_NAME[it["rkind"]], it["rhash"], it["mhash"], it["rule"], it["paint"],
            " ".join("%016x" % bits(c) for c in it["rgba"]), space_text(it["space"]), ld)
    if k == CLIP_PUSH:
        return "C res=%s:%016x map=%016x rule=%d %s" % (KIND_NAME[it["rkind"]], it["rhash"], it["mhash"], it["rule"], ld)
    if k == CLIP_POP:
        return "c"
    if k == GROUP_PUSH:
        return "G opacity=%016x blend=%s composite=%d order=%d %s" % (
            bits(it["opacity"]), space_text(it["blend"]), it["composite"], it["order"], ld)
    if k == GROUP_POP:
        return "g"
    return "I res=%s:%016x map=%016x %s" % (KIND_NAME[it["rkind"]], it["rhash"], it["mhash"], ld)


def list_dump(items, fg=DEFAULT_FG, ties=()):
    lines = ["gshscene.list 2 items=%d ties=%d fg=%s hash=%016x" % (
        len(items), len(ties), color_text(fg[0], fg[1]), list_hash(items, fg))]
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
    GROUP_POP_ITEM,
    CLIP_POP_ITEM,
]
LIST_HASH = list_hash(LIST_ITEMS, LIST_FG)

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
NODE_GROUP, NODE_SHAPE, NODE_INSTANCE = 1, 2, 3
DOMAIN = 0x5356470000000001
SCENE_FG = ((1.0, 0.5, 0.25, 0.75), P3_GAMMA)


def key_bytes(key):
    if key is None:
        return u8(0)
    return u8(1) + u64(key[0]) + u64(key[1])


def node_bytes(n):
    out = u64(n["id"]) + u8(n["kind"]) + u8(n["flags"]) + u64(n["parent"]) + key_bytes(n["key"])
    if n["kind"] == NODE_GROUP:
        out += u64(n["map"]) + f64(n["opacity"]) + space_bytes(n["blend"]) + u8(n["composite"]) + u32(n["order"])
        out += i32(n["layer"]) + f64(n["depth"])
        clip = n["clip"]
        if clip is None:
            out += u8(0)
        else:
            out += u8(1) + u64(clip[1]) + u64(clip[2]) + u8(clip[3])
        out += u64(len(n["children"])) + b"".join(u64(c) for c in n["children"])
    elif n["kind"] == NODE_SHAPE:
        out += u64(n["res"][1]) + u64(n["map"]) + u8(n["rule"]) + u8(n["paint"])
        out += b"".join(f64(c) for c in n["rgba"]) + space_bytes(n["space"]) + i32(n["layer"]) + f64(n["depth"])
    else:
        out += u64(n["target"]) + u64(n["map"]) + i32(n["layer"]) + f64(n["depth"])
    return out


def version_hash(nodes, fg=DEFAULT_FG):
    out = u32(2) + u32(0x4E454353) + color_bytes(fg[0], fg[1]) + u64(len(nodes))
    for n in nodes:
        out += node_bytes(n)
    return fnv(out)


def key_text(key):
    return "-" if key is None else "%016x:%016x" % key


def node_line(n):
    head = "%s id=%d parent=%d flags=%d key=%s" % ("GSI"[n["kind"] - 1], n["id"], n["parent"], n["flags"], key_text(n["key"]))
    if n["kind"] == NODE_GROUP:
        clip = n["clip"]
        clip_text = "-" if clip is None else "%s:%016x:%016x:%d" % (KIND_NAME[clip[0]], clip[1], clip[2], clip[3])
        children = ",".join(str(c) for c in n["children"]) if n["children"] else "-"
        return "%s map=%016x opacity=%016x blend=%s composite=%d order=%d layer=%d depth=%016x clip=%s children=%s" % (
            head, n["map"], bits(n["opacity"]), space_text(n["blend"]), n["composite"], n["order"], n["layer"],
            bits(n["depth"]), clip_text, children)
    if n["kind"] == NODE_SHAPE:
        return "%s res=%s:%016x map=%016x rule=%d paint=%d %s space=%s layer=%d depth=%016x" % (
            head, KIND_NAME[n["res"][0]], n["res"][1], n["map"], n["rule"], n["paint"],
            " ".join("%016x" % bits(c) for c in n["rgba"]), space_text(n["space"]), n["layer"], bits(n["depth"]))
    return "%s target=%d map=%016x layer=%d depth=%016x" % (head, n["target"], n["map"], n["layer"], bits(n["depth"]))


def version_dump(nodes, fg=DEFAULT_FG):
    lines = ["gshscene.version 2 nodes=%d root=1 fg=%s hash=%016x" % (
        len(nodes), color_text(fg[0], fg[1]), version_hash(nodes, fg))]
    lines += [node_line(n) for n in nodes]
    return "\n".join(lines) + "\n"


def group_node(i, parent, children, **kw):
    n = dict(id=i, kind=NODE_GROUP, flags=0, parent=parent, key=None, map=IDENTITY, opacity=1.0, blend=TARGET,
             composite=0, order=ORDER_FLAT, layer=0, depth=0.0, clip=None, children=children)
    n.update(kw)
    return n


def shape_node(i, parent, res, **kw):
    n = dict(id=i, kind=NODE_SHAPE, flags=0, parent=parent, key=None, res=res, map=IDENTITY, rule=0, paint=PAINT_SOLID,
             rgba=BLACK, space=TARGET, layer=0, depth=0.0)
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
]
SCENE_HASH = version_hash(SCENE_NODES, SCENE_FG)
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
                    ("kPictureListHash", PICTURE_LIST_HASH), ("kPictureHash", PICTURE_RES),
                    ("kListHash", LIST_HASH), ("kOrderListHash", ORDER_LIST_HASH), ("kMapIdentityHash", IDENTITY),
                    ("kMapTranslateHash", TRANSLATE), ("kMapDesignHash", DESIGN),
                    ("kSceneHash", SCENE_HASH), ("kEmptySceneHash", EMPTY_HASH), ("kSceneBHash", version_hash(SCENE_B)),
                    ("kPathKeyEmpty", path_key([])), ("kPathKeyOne", path_key([1])),
                    ("kPathKeyTwo", path_key([4, 5])), ("kPathKeyThree", path_key([7, 7, 3]))):
        o.append("static const std::uint64_t %s = UINT64_C(0x%016x);\n" % (name, v))
    o.append("static const int kSceneBLayer = %d;\n" % B_LAYER)
    o.append("static const double kSceneBTranslate[4][2] = {%s};\n" % ", ".join("{%r, %r}" % (t[0], t[1]) for t in B_T))
    o.append("\n")
    o.append(cxx_string("kSquareResourceDump", "gshscene.resource 1 kind=path hash=%016x\n" % SQ_RES + path_dump(SQ)))
    o.append(cxx_string("kFixturePathResourceDump", "gshscene.resource 1 kind=path hash=%016x\n" % P2_RES + path_dump(P2)))
    o.append(cxx_string("kFaceDump", face_dump(*FACE)))
    o.append(cxx_string("kFontRunDump", run_dump(*FONT_RUN)))
    o.append(cxx_string("kOutlineRunDump", run_dump(*OUTLINE_RUN)))
    o.append(cxx_string("kPictureDump", "gshscene.resource 1 kind=picture hash=%016x\n" % PICTURE_RES + list_dump(PICTURE_ITEMS)))
    o.append(cxx_string("kListDump", list_dump(LIST_ITEMS, LIST_FG)))
    o.append(cxx_string("kOrderListDump", list_dump(ORDER_ITEMS, DEFAULT_FG, ORDER_TIES)))
    o.append(cxx_string("kSceneDump", version_dump(SCENE_NODES, SCENE_FG)))
    o.append(cxx_string("kEmptySceneDump", version_dump(EMPTY_NODES)))
    o.append(cxx_string("kSceneBDump", version_dump(SCENE_B)))
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
