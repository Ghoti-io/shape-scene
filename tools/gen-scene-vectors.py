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
glyph run from a font and one from outlines, a picture, and a list holding every
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
KIND_PATH, KIND_RUN, KIND_PICTURE = 1, 2, 3
KIND_NAME = {KIND_PATH: "path", KIND_RUN: "glyph-run", KIND_PICTURE: "picture"}


def path_resource_hash(path):
    return fnv(u32(1) + u32(KIND_PATH) + u64(path_hash(path)))


def run_hash(source, key, face, size, hint, var, glyphs):
    out = u32(1) + u32(KIND_RUN) + u32(source) + u64(key) + u32(face) + f64(size) + u32(hint) + u64(var)
    out += u64(len(glyphs))
    for gid, x, y, outline in glyphs:
        out += u32(gid) + f64(x) + f64(y) + u64(outline)
    return fnv(out)


def run_dump(source, key, face, size, hint, var, glyphs):
    h = run_hash(source, key, face, size, hint, var, glyphs)
    lines = ["gshscene.resource 1 kind=glyph-run hash=%016x" % h,
             "run source=%d font=%016x face=%d size=%016x hint=%d var=%016x glyphs=%d" % (
                 source, key, face, bits(size), hint, var, len(glyphs))]
    for gid, x, y, outline in glyphs:
        lines.append("g id=%d x=%016x y=%016x outline=%016x" % (gid, bits(x), bits(y), outline))
    return "\n".join(lines) + "\n"


SQ = square()
P2 = fixture2()
SQ_RES = path_resource_hash(SQ)
P2_RES = path_resource_hash(P2)

FONT_RUN = (0, 0x1122334455667788, 2, 12.5, 3, 0xAABBCCDD00112233,
            [(36, 0.0, 0.0, 0), (37, 7.5, 0.25, 0), (4294967295, -1.0, 2.0, 0)])
OUTLINE_RUN = (1, 0, 0, 0.0, 0, 0, [(1, 0.0, 0.0, SQ_RES), (2, 5.0, -3.0, P2_RES)])
FONT_RUN_HASH = run_hash(*FONT_RUN)
OUTLINE_RUN_HASH = run_hash(*OUTLINE_RUN)

# --- lists (list.h) ---------------------------------------------------------------
FILL, CLIP_PUSH, CLIP_POP, GROUP_PUSH, GROUP_POP, INSTANCE = 1, 2, 3, 4, 5, 6


def item_bytes(it):
    k = it[0]
    out = u8(k)
    if k == FILL:
        _, rkind, rhash, mhash, rule, rgba, space, layer = it
        out += u64(rhash) + u64(mhash) + u8(rule) + u8(0) + b"".join(f64(c) for c in rgba) + u32(space) + i32(layer)
    elif k == CLIP_PUSH:
        _, rkind, rhash, mhash, rule = it
        out += u64(rhash) + u64(mhash) + u8(rule)
    elif k == GROUP_PUSH:
        _, opacity, blend, order, layer = it
        out += f64(opacity) + u32(blend) + u32(order) + i32(layer)
    elif k == INSTANCE:
        _, rkind, rhash, mhash, layer = it
        out += u64(rhash) + u64(mhash) + i32(layer)
    return out


def list_hash(items):
    out = u32(1) + u32(0x5453494C) + u64(len(items))
    for it in items:
        out += item_bytes(it)
    return fnv(out)


def item_line(it):
    k = it[0]
    if k == FILL:
        _, rkind, rhash, mhash, rule, rgba, space, layer = it
        return "F res=%s:%016x map=%016x rule=%d paint=0 %s space=%d layer=%d" % (
            KIND_NAME[rkind], rhash, mhash, rule, " ".join("%016x" % bits(c) for c in rgba), space, layer)
    if k == CLIP_PUSH:
        _, rkind, rhash, mhash, rule = it
        return "C res=%s:%016x map=%016x rule=%d" % (KIND_NAME[rkind], rhash, mhash, rule)
    if k == CLIP_POP:
        return "c"
    if k == GROUP_PUSH:
        _, opacity, blend, order, layer = it
        return "G opacity=%016x blend=%d order=%d layer=%d" % (bits(opacity), blend, order, layer)
    if k == GROUP_POP:
        return "g"
    _, rkind, rhash, mhash, layer = it
    return "I res=%s:%016x map=%016x layer=%d" % (KIND_NAME[rkind], rhash, mhash, layer)


def list_dump(items):
    lines = ["gshscene.list 1 items=%d hash=%016x" % (len(items), list_hash(items))]
    lines += [item_line(it) for it in items]
    return "\n".join(lines) + "\n"


BLACK = (0.0, 0.0, 0.0, 1.0)
PICTURE_ITEMS = [
    (FILL, KIND_PATH, SQ_RES, IDENTITY, 0, BLACK, 0, 0),
    (GROUP_PUSH, 0.5, 0, 0, 0),
    (FILL, KIND_PATH, P2_RES, TRANSLATE, 1, (1.0, 0.5, 0.25, 0.75), 0, 2),
    (GROUP_POP,),
]
PICTURE_LIST_HASH = list_hash(PICTURE_ITEMS)
PICTURE_RES = fnv(u32(1) + u32(KIND_PICTURE) + u64(PICTURE_LIST_HASH))

LIST_ITEMS = [
    (FILL, KIND_PATH, SQ_RES, DESIGN, 0, (0.25, 0.5, 0.75, 1.0), 0, -3),
    (FILL, KIND_RUN, FONT_RUN_HASH, IDENTITY, 1, BLACK, 0, 0),
    (CLIP_PUSH, KIND_PATH, P2_RES, TRANSLATE, 1),
    (GROUP_PUSH, 0.5, 0, 0, 7),
    (INSTANCE, KIND_PICTURE, PICTURE_RES, TRANSLATE, 1),
    (FILL, KIND_RUN, OUTLINE_RUN_HASH, IDENTITY, 0, (2.0, -0.5, 0.0, 0.5), 0, 5),
    (GROUP_POP,),
    (CLIP_POP,),
]
LIST_HASH = list_hash(LIST_ITEMS)


# --- versions (version.h) ---------------------------------------------------------------------------------
NODE_GROUP, NODE_SHAPE, NODE_INSTANCE = 1, 2, 3
DOMAIN = 0x5356470000000001


def key_bytes(key):
    if key is None:
        return u8(0)
    return u8(1) + u64(key[0]) + u64(key[1])


def node_bytes(n):
    out = u64(n["id"]) + u8(n["kind"]) + u8(n["flags"]) + u64(n["parent"]) + key_bytes(n["key"])
    if n["kind"] == NODE_GROUP:
        out += u64(n["map"]) + f64(n["opacity"]) + i32(n["layer"])
        clip = n["clip"]
        if clip is None:
            out += u8(0)
        else:
            out += u8(1) + u64(clip[1]) + u64(clip[2]) + u8(clip[3])
        out += u64(len(n["children"])) + b"".join(u64(c) for c in n["children"])
    elif n["kind"] == NODE_SHAPE:
        out += u64(n["res"][1]) + u64(n["map"]) + u8(n["rule"]) + u8(0)
        out += b"".join(f64(c) for c in n["rgba"]) + u32(n["space"]) + i32(n["layer"])
    else:
        out += u64(n["target"]) + u64(n["map"]) + i32(n["layer"])
    return out


def version_hash(nodes):
    out = u32(1) + u32(0x4E454353) + u64(len(nodes))
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
        return "%s map=%016x opacity=%016x layer=%d clip=%s children=%s" % (
            head, n["map"], bits(n["opacity"]), n["layer"], clip_text, children)
    if n["kind"] == NODE_SHAPE:
        return "%s res=%s:%016x map=%016x rule=%d paint=0 %s space=%d layer=%d" % (
            head, KIND_NAME[n["res"][0]], n["res"][1], n["map"], n["rule"],
            " ".join("%016x" % bits(c) for c in n["rgba"]), n["space"], n["layer"])
    return "%s target=%d map=%016x layer=%d" % (head, n["target"], n["map"], n["layer"])


def version_dump(nodes):
    lines = ["gshscene.version 1 nodes=%d root=1 hash=%016x" % (len(nodes), version_hash(nodes))]
    lines += [node_line(n) for n in nodes]
    return "\n".join(lines) + "\n"


def group_node(i, parent, children, **kw):
    n = dict(id=i, kind=NODE_GROUP, flags=0, parent=parent, key=None, map=IDENTITY, opacity=1.0, layer=0, clip=None,
             children=children)
    n.update(kw)
    return n


def shape_node(i, parent, res, **kw):
    n = dict(id=i, kind=NODE_SHAPE, flags=0, parent=parent, key=None, res=res, map=IDENTITY, rule=0,
             rgba=BLACK, space=0, layer=0)
    n.update(kw)
    return n


def instance_node(i, parent, target, **kw):
    n = dict(id=i, kind=NODE_INSTANCE, flags=0, parent=parent, key=None, target=target, map=IDENTITY, layer=0)
    n.update(kw)
    return n


SCENE_NODES = [
    group_node(1, 0, [2, 4, 7]),
    group_node(2, 1, [3], key=(DOMAIN, 42), map=TRANSLATE, opacity=0.5, layer=3,
               clip=(KIND_PATH, P2_RES, DESIGN, 1)),
    shape_node(3, 2, (KIND_PATH, SQ_RES), rgba=(0.25, 0.5, 0.75, 1.0), layer=-1),
    group_node(4, 1, [5, 6], flags=1),
    shape_node(5, 4, (KIND_RUN, OUTLINE_RUN_HASH), key=(1, 2), map=TRANSLATE, rule=1, rgba=(2.0, -0.5, 0.0, 0.5), layer=5),
    shape_node(6, 4, (KIND_RUN, FONT_RUN_HASH)),
    instance_node(7, 1, 4, map=DESIGN, layer=9),
]
SCENE_HASH = version_hash(SCENE_NODES)
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
                    ("kFontRunHash", FONT_RUN_HASH), ("kOutlineRunHash", OUTLINE_RUN_HASH),
                    ("kPictureListHash", PICTURE_LIST_HASH), ("kPictureHash", PICTURE_RES),
                    ("kListHash", LIST_HASH), ("kMapIdentityHash", IDENTITY),
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
    o.append(cxx_string("kFontRunDump", run_dump(*FONT_RUN)))
    o.append(cxx_string("kOutlineRunDump", run_dump(*OUTLINE_RUN)))
    o.append(cxx_string("kPictureDump", "gshscene.resource 1 kind=picture hash=%016x\n" % PICTURE_RES + list_dump(PICTURE_ITEMS)))
    o.append(cxx_string("kListDump", list_dump(LIST_ITEMS)))
    o.append(cxx_string("kSceneDump", version_dump(SCENE_NODES)))
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
