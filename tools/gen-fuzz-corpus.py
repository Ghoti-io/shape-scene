#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-only
#
# Copyright (C) 2026 Corey Pennycuff
#
# This file is part of Ghoti.io Shape Scene.
#
# Ghoti.io Shape Scene is free software: you can redistribute it and/or modify it
# under the terms of the GNU Lesser General Public License version 3 as
# published by the Free Software Foundation.
"""Write the seed corpus of the recording-API fuzzer, deterministically (CAP-15, AD-19).

    python3 tools/gen-fuzz-corpus.py            # write tests/fuzz/corpus/record/
    python3 tools/gen-fuzz-corpus.py --check    # fail if the committed files differ

Every file is an input of tests/fuzz/fuzz_record.c: two option bytes that pick the
limits, then a stream of ops, each an op number and a few argument bytes.  The
files are of two kinds.  The **biased** ones choose the ops by a weight table that
favours the retained scene (open an edit, add, publish, merge, diff, emit) and the
recording of a list, with random argument bytes; the **plain** ones are random
bytes, which the fuzzer reads as valid input by construction.  The options cover
every row of the limit tables, since the limits are what the inputs are for.  The
replay gate (`make fuzz-replay`) fails if these files, together, never reach a
limit, a refusal or an invalid publish: that is what keeps the corpus from
measuring nothing, and what to read if an op number below drifts from the C file.
"""
import base64
import os
import sys

# Keep in step with the enum in tests/fuzz/fuzz_record.c.
OPS = ["LIST_CREATE", "FILL", "CLIP_PUSH", "CLIP_POP", "GROUP_PUSH", "GROUP_POP", "INSTANCE", "CLOSE", "RESET",
       "FREEZE", "LIST_DESTROY", "INTERN_PATH", "INTERN_RUN", "INTERN_OUTLINES", "RELEASE_RES", "SCENE_CREATE",
       "SCENE_DESTROY", "OPEN_EDIT", "DESTROY_EDIT", "ADD", "SET", "REMOVE", "MOVE", "UPSERT", "REMOVE_KEY",
       "PUBLISH", "MERGE", "HOLD", "RELEASE_VERSION", "DIFF", "EMIT", "VISIT", "DUMP", "SCENE_INTERN",
       "FILL_EX", "CLIP_PUSH_EX", "GROUP_PUSH_EX", "LIST_FOREGROUND", "NODE_PROPS", "EDIT_FOREGROUND", "LIST_PALETTE",
       "FILL_FIXED", "TRUNCATE", "EDIT_PALETTE", "INTERN_GRADIENT", "FILL_GRADIENT", "SCENE_GRADIENT", "GRADIENT_NODE",
       "COLOR_BOUND", "GROUP_PDF", "NODE_PDF", "INTERN_STYLE", "STROKE", "STROKE_NODE", "PLACEHOLDER", "PLACEHOLDER_NODE", "INTERN_IMAGE", "IMAGE_NOT_SHAPE", "IMAGE_PAINT",
       "INTERN_MASK", "GROUP_MASK", "NODE_MASK", "INTERN_PRIMITIVE"]
OP = {name: i for i, name in enumerate(OPS)}

BIASED = {
    "FILL": 5, "CLIP_PUSH": 3, "CLIP_POP": 3, "GROUP_PUSH": 3, "GROUP_POP": 3, "INSTANCE": 4, "CLOSE": 1, "FREEZE": 3,
    "LIST_CREATE": 1, "INTERN_PATH": 3, "INTERN_RUN": 1, "INTERN_OUTLINES": 1, "OPEN_EDIT": 7, "ADD": 22, "SET": 5,
    "REMOVE": 3, "MOVE": 3, "UPSERT": 5, "REMOVE_KEY": 1, "PUBLISH": 9, "MERGE": 6, "HOLD": 5, "DIFF": 5, "EMIT": 4,
    "VISIT": 1, "DUMP": 1, "SCENE_INTERN": 2, "RESET": 1, "DESTROY_EDIT": 1, "SCENE_DESTROY": 1, "SCENE_CREATE": 1,
    "FILL_EX": 5, "CLIP_PUSH_EX": 2, "GROUP_PUSH_EX": 4, "LIST_FOREGROUND": 1, "NODE_PROPS": 5, "EDIT_FOREGROUND": 2,
    "LIST_PALETTE": 2, "FILL_FIXED": 4, "TRUNCATE": 2, "EDIT_PALETTE": 2, "INTERN_GRADIENT": 4, "FILL_GRADIENT": 4,
    "SCENE_GRADIENT": 4, "GRADIENT_NODE": 5, "INTERN_STYLE": 5, "STROKE": 4, "STROKE_NODE": 4,
    "INTERN_IMAGE": 6, "IMAGE_NOT_SHAPE": 2, "IMAGE_PAINT": 6, "INTERN_MASK": 8, "GROUP_MASK": 6, "NODE_MASK": 3,
    "INTERN_PRIMITIVE": 5,
}

BIASED_FILES = 160
PLAIN_FILES = 60


class Rng:
    def __init__(self, seed):
        self.s = (seed * 0x9E3779B97F4A7C15 + 12345) & (2**64 - 1)

    def next(self):
        s = self.s
        s ^= s >> 12
        s ^= (s << 25) & (2**64 - 1)
        s ^= s >> 27
        self.s = s
        return (s * 2685821657736338717) & (2**64 - 1)

    def below(self, n):
        return self.next() % n

    def byte(self):
        return self.next() & 255


def biased(seed):
    rng = Rng(seed)
    table = []
    for name, weight in BIASED.items():
        table += [OP[name]] * weight
    out = bytearray([rng.byte(), rng.byte()])
    for _ in range(40 + rng.below(110)):
        out.append(table[rng.below(len(table))])
        for _ in range(3 + rng.below(12)):
            out.append(rng.byte())
    return bytes(out)


def plain(seed):
    rng = Rng(1000 + seed)
    return bytes(rng.byte() for _ in range(2 + 100 + rng.below(500)))


def open_edit(slot=0):
    return bytes([OP["OPEN_EDIT"], slot, 4])  # the edit in a slot, on the current version


def add_shape(parent_arg, slot=0):
    # ADD reads: edit slot, parent, position, kind, map, layer, flags, resource, paint, two rule picks, three key picks
    return bytes([OP["ADD"], slot, parent_arg, 5, 1, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0])


def add_group(parent_arg, slot=0):
    # ... kind 0 (group): opacity pick, then the clip selector instead of paint and rules
    return bytes([OP["ADD"], slot, parent_arg, 5, 0, 1, 2, 0, 8, 4, 1, 0, 0, 0])


def publish(slot=0):
    return bytes([OP["PUBLISH"], slot])


def merge_two():
    # MERGE reads the first edit's slot, then one pick for each other edit that exists (1: include it)
    return bytes([OP["MERGE"], 0, 1])


def scripted():
    """Inputs that reach, by construction, the outcomes that random bytes reach only by luck."""
    out = {}
    # Options b1 = 51: max_nodes 4 (the root and three).  A fourth add is refused by the bound.
    seq = bytearray([0xFF, 51]) + open_edit()
    for _ in range(5):
        seq += add_shape(0)
    seq += publish()
    out["scripted-edit-limit.bin"] = bytes(seq)
    # Options b1 = 15: max_node_depth 2.  A group under the root and a shape under it is three levels: refused at publish.
    seq = bytearray([0xFF, 15]) + open_edit() + add_group(0) + add_shape(2) + publish()
    out["scripted-publish-limit.bin"] = bytes(seq)
    # Two sibling edits that both add under the root overlap on the root: the merge is refused.
    seq = bytearray([0xFF, 0xFF]) + open_edit(0) + open_edit(1) + add_shape(0, 0) + add_shape(0, 1) + merge_two()
    out["scripted-merge-refused.bin"] = bytes(seq)
    # Two sibling edits of disjoint parts merge: a group published first, then one edit adds a shape under it and the other
    # one under the root (random inputs reached this by luck until the op table grew).
    seq = (bytearray([0xFF, 0xFF]) + open_edit(0) + add_group(0, 0) + publish(0) + bytes([OP["DESTROY_EDIT"], 0])
           + open_edit(0) + open_edit(1) + add_shape(2, 0) + add_shape(0, 1) + merge_two())
    out["scripted-merge-ok.bin"] = bytes(seq)
    # An instance of a node that is not there: refused at publish, naming it (ADD kind 2: the target is an id pick, 1 is bogus).
    seq = bytearray([0xFF, 0xFF]) + open_edit(0) + bytes([OP["ADD"], 0, 0, 5, 2, 1, 2, 0, 0, 1, 0, 0, 0]) + publish(0)
    out["scripted-publish-invalid.bin"] = bytes(seq)
    # A depth group in a list: two fills of one depth are a tie, which close records (GROUP_PUSH_EX: opacity 1, the target's
    # blend space, ordering depth, source-over, layer, depth; FILL_EX: a solid fill of depth -1; GROUP_POP; CLOSE).
    group_depth = bytes([OP["GROUP_PUSH_EX"], 4, 0, 1, 0, 1, 0])
    depth_fill = bytes([OP["FILL_EX"], 0, 0, 4, 0, 0, 0, 0])
    seq = bytearray([0xFF, 0xFF]) + group_depth + depth_fill + depth_fill + bytes([OP["GROUP_POP"], OP["CLOSE"]])
    out["scripted-list-ties.bin"] = bytes(seq)
    # An edit that sets the foreground, published: a version whose foreground differs (EDIT_FOREGROUND: red 1, green 0.5,
    # blue 0, alpha 1, a space of the table).
    seq = bytearray([0xFF, 0xFF]) + open_edit() + bytes([OP["EDIT_FOREGROUND"], 0, 2, 1, 0, 4, 1]) + publish()
    out["scripted-foreground-version.bin"] = bytes(seq)
    # Two sibling edits that both set it: the merge is refused, the root named with the parameter reason.
    seq = (bytearray([0xFF, 0xFF]) + open_edit(0) + open_edit(1) + bytes([OP["EDIT_FOREGROUND"], 0, 2, 1, 0, 4, 1])
           + bytes([OP["EDIT_FOREGROUND"], 1, 4, 0, 1, 4, 2]) + merge_two())
    out["scripted-foreground-refused.bin"] = bytes(seq)
    # A depth group in a scene with two shapes of equal layer and depth, published and emitted: the emitted list has a tie.
    # (NODE_PROPS: edit slot, the id pick, depth, ordering depth, source-over, a space, a paint kind.)
    seq = (bytearray([0xFF, 0xFF]) + open_edit() + add_group(0) + bytes([OP["NODE_PROPS"], 0, 2, 0, 1, 0, 0, 2])
           + add_shape(2) + add_shape(2) + publish() + bytes([OP["EMIT"], 0]))
    out["scripted-emit-ties.bin"] = bytes(seq)
    # Named primitives (primitive.h).  INTERN_PRIMITIVE reads: the slot, the kind pick (3 a rect, 1 a rounded rect, 2 an ellipse, 0x50 past
    # the enum), the flips (bit 0 flip_x, bit 1 flip_y, 0x10 a flip that is not 0 or 1), then twelve value bytes (a byte below 8 is
    # 0, -0, 1, 2, 0.5, 1e300, 2^100, -1; else (b - 8) / 4): x y w h, the four corners' rx, the four corners' ry (or cx cy rx ry).
    ip = OP["INTERN_PRIMITIVE"]
    box = bytes([12, 12, 20, 24])         # 1, 1, 3, 4
    wide = bytes([12, 12, 88, 88])        # 1, 1, 20, 20
    two = bytes([16, 16, 16, 16])         # 2.0 each
    none = bytes([0, 0, 0, 0])
    out["scripted-primitive-rect.bin"] = bytes([0xFF, 0xFF, ip, 0, 3, 0]) + box + none + none
    out["scripted-primitive-rect-flipped.bin"] = bytes([0xFF, 0xFF, ip, 0, 3, 3]) + box + none + none
    out["scripted-primitive-ellipse.bin"] = bytes([0xFF, 0xFF, ip, 1, 2, 1]) + box + none + none
    out["scripted-primitive-rounded.bin"] = bytes([0xFF, 0xFF, ip, 2, 1, 0]) + wide + two + two
    out["scripted-primitive-canonical.bin"] = bytes([0xFF, 0xFF, ip, 2, 1, 0]) + wide + none + none
    out["scripted-primitive-rounded-flipped.bin"] = bytes([0xFF, 0xFF, ip, 2, 1, 2]) + wide + two + two
    out["scripted-primitive-invalid.bin"] = bytes([0xFF, 0xFF, ip, 0, 0x50, 0]) + box + none + none + bytes([ip, 0, 3, 0x10]) + box + none + none
    out["scripted-primitive-overlap.bin"] = bytes([0xFF, 0xFF, ip, 3, 1, 0]) + wide + bytes([88, 88, 0, 0]) + two
    # b1 = 0xFC: max_resource_elements 3, so the five elements of a rectangle are past it
    out["scripted-primitive-limit.bin"] = bytes([0xFF, 0xFC, ip, 0, 3, 0]) + box + none + none
    # a path of arcs by INTERN_PATH (recipe 6)
    out["scripted-arc-path.bin"] = bytes([0xFF, 0xFF, OP["INTERN_PATH"], 0, 6, OP["INTERN_PATH"], 1, 7])
    # Color-paint story P2: a linear gradient in the scene's store (p0 (1, 1), p1 (10, 1); a solid stop and a
    # foreground stop), a shape node painted with it, published and emitted.  SCENE_GRADIENT reads the slot, then
    # fuzz_gradient's bytes: kind, six values, the isoline pick, extend, premultiplied, space, map, the stop count and
    # each stop (offset, kind, alpha, and for a solid red, green, space), then the many-stops pick.
    linear = bytes([0, 9, 9, 18, 9, 9, 9, 1, 0, 0, 0, 0, 2, 1, 0, 4, 2, 1, 0, 7, 2, 4, 1])
    node = bytes([OP["GRADIENT_NODE"], 0, 0, 0, 0, 4, 1, 0])
    seq = (bytearray([0xFF, 0xFF]) + open_edit() + bytes([OP["SCENE_GRADIENT"], 0]) + linear + node + publish()
           + bytes([OP["EMIT"], 0]))
    out["scripted-gradient-node.bin"] = bytes(seq)
    # The same gradient with p0's x one step past G14's bound (a value byte of 48: the table's entry 12): refused.
    past = bytearray(linear)
    past[1] = 48
    seq = bytearray([0xFF, 0xFF]) + bytes([OP["SCENE_GRADIENT"], 0]) + past
    out["scripted-gradient-past-bound.bin"] = bytes(seq)
    # N1's bound (Corey, 2026-10-21).  COLOR_BOUND reads: the value (0.5, 2^16, -2^16, one step past each, 2^17, 2^64,
    # -1e300), the component, the alpha pick (0: an invalid alpha), the space, where (0 a fill, 1 the list's
    # foreground, 2 an edit's, 3 a palette, 4 a gradient stop, 5 a shape node), then where's own picks.  At the bound
    # and one step past it at each entry point, and one past it with an invalid alpha (invalid, decided first).
    cb = OP["COLOR_BOUND"]
    seq = (bytearray([0xFF, 0xFF]) + bytes([cb, 1, 0, 1, 0, 1]) + bytes([cb, 3, 1, 1, 2, 1]) + bytes([cb, 2, 2, 1, 0, 0, 0])
           + bytes([cb, 4, 2, 1, 0, 0, 0]) + bytes([cb, 4, 0, 0, 0, 0, 0]) + bytes([cb, 3, 2, 1, 1, 3, 1])
           + bytes([cb, 5, 0, 1, 2, 4, 0]) + open_edit() + bytes([cb, 6, 1, 1, 0, 2, 0])
           + bytes([cb, 7, 0, 1, 0, 5, 0, 0, 1, 0]) + bytes([cb, 1, 0, 1, 0, 5, 0, 0, 1, 0]))
    out["scripted-color-bound.bin"] = bytes(seq)
    # INTERN_GRADIENT reads: the slot, the stop count and each stop (offset, kind, alpha, and for a solid red and space),
    # the fixed pick (not 0: doubles), the kind, six values (b % 9 - 4), the other-kind pick, the extend, the flag picks,
    # the space and the map.  A degenerate linear gradient (p0 = p1) in slot 0, a drawn one in slot 1, and a fill of it
    # (FILL_GRADIENT: the slot, the alpha, the map, the path).  Random inputs reached these by luck until the op table
    # grew (N1's COLOR_BOUND), which is why they are scripted.
    stops = bytes([2, 2, 0, 4, 2, 2, 6, 2, 4])
    degenerate = bytes([OP["INTERN_GRADIENT"], 0]) + stops + bytes([1, 1, 4, 4, 4, 4, 4, 4, 1, 0, 0, 0, 2, 0])
    drawn = bytes([OP["INTERN_GRADIENT"], 1]) + stops + bytes([1, 1, 4, 4, 12, 4, 4, 4, 1, 0, 0, 0, 2, 0])
    fillg = bytes([OP["FILL_GRADIENT"], 1, 4, 0, 0])
    out["scripted-gradient-fill.bin"] = bytes(bytearray([0xFF, 0xFF]) + degenerate + drawn + fillg)
    # Color-paint story P3.  GROUP_PDF reads the opacity (4: 1), the composite (23: multiply), the isolation, the
    # knockout, the mask pick (1: a picture of the pool, then its slot).  A closed empty list frozen into pool slot 0,
    # then a non-isolated knockout multiply group masked by it, and one with an isolation past the enum (invalid).
    gp = OP["GROUP_PDF"]
    seq = (bytearray([0xFF, 0xFF]) + bytes([OP["LIST_CREATE"], OP["CLOSE"], OP["FREEZE"], 0, OP["LIST_CREATE"]])
           + bytes([gp, 4, 23, 1, 1, 1, 0]) + bytes([gp, 4, 24, 2, 0, 0]))
    out["scripted-group-pdf.bin"] = bytes(seq)
    # NODE_PDF reads the edit slot, the id pick (2: the group just added), the isolation, the knockout and the mask
    # pick (1: a picture made in the scene's store).  Published and emitted, the group's push carries them.
    seq = (bytearray([0xFF, 0xFF]) + open_edit() + add_group(0) + bytes([OP["NODE_PDF"], 0, 2, 1, 1, 1]) + publish()
           + bytes([OP["EMIT"], 0]))
    out["scripted-node-pdf.bin"] = bytes(seq)
    # Strokes story S1.  INTERN_STYLE reads: the store pick (1: the scene's), the slot, the width, the width space, the
    # join, the miter limit, the cap, the degenerate rule, the dash scope, the alignment and its rule, the dash count
    # and each dash, the null-array pick when there are dashes (0: dashes counted but not given), the offset and the
    # profile pick (0: a path as a width profile, reserved).  Values index the fuzzer's table (0 zero, 1 one, 2 2.5,
    # 3 -0, 4 2^-64, 5 2^-65, 6 2^64, 7 one step past, 8 -1, 9 NaN, 10 infinity, 11 three).  An odd array doubled; a
    # negative dash (solid) in the scene's store; a NaN width (invalid); a width past SK2's band (limit); a profile
    # (unsupported); a width of 0.  STROKE reads: the centerline's recipe (5 is 3D), the style pick, the map, the 3D map
    # pick (0: 3D) and the paint.  STROKE_NODE reads: the edit slot, the map, the centerline, the style pick, the
    # paint, the set-or-add pick and the parent.
    st = OP["INTERN_STYLE"]
    styles = (bytes([st, 0, 0, 1, 0, 1, 1, 1, 0, 0, 0, 0, 3, 11, 1, 2, 1, 1, 1])
              + bytes([st, 1, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 2, 8, 1, 1, 3, 1])
              + bytes([st, 0, 1, 9, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1])
              + bytes([st, 0, 1, 5, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1])
              + bytes([st, 0, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0])
              + bytes([st, 0, 2, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1]))
    sk = OP["STROKE"]
    strokes = bytes([sk, 2, 0, 0, 1, 0]) + bytes([sk, 5, 0, 0, 1, 0]) + bytes([sk, 2, 2, 3, 0, 0]) + bytes([sk, 3, 2, 2, 1, 1])
    node = bytes([OP["STROKE_NODE"], 0, 0, 0, 0, 0, 1, 0])
    seq = bytearray([0xFF, 0xFF]) + styles + strokes + open_edit() + node + publish() + bytes([OP["EMIT"], 0])
    out["scripted-strokes.bin"] = bytes(seq)
    # Color-paint story P5.  PLACEHOLDER reads the source pick (4), the code pick (4), the approximated pick (3, 2 is
    # invalid), the box pick (5: 0 none, 1 and 2 valid, 3 inverted, 4 NaN), the map and the 3D pick (8: 0 is 3D).  A valid
    # one with a box, one with no box approximated, then each invalid shape (a zero source, a zero code, approximated 2, an
    # inverted box, a NaN box, a 3D map).  PLACEHOLDER_NODE reads the edit slot, the source, the code, the approximated
    # pick, the box pick (4: 3 is inverted), the map, the set-or-add pick (0: set) and the parent.  A valid node, an invalid one (a
    # zero source), published and emitted.
    ph = OP["PLACEHOLDER"]
    records = (bytes([ph, 1, 1, 0, 1, 0, 1]) + bytes([ph, 2, 3, 1, 0, 1, 1]) + bytes([ph, 0, 1, 0, 1, 0, 1])
               + bytes([ph, 1, 0, 0, 1, 0, 1]) + bytes([ph, 1, 1, 2, 1, 0, 1]) + bytes([ph, 1, 1, 0, 3, 0, 1])
               + bytes([ph, 1, 1, 0, 4, 0, 1]) + bytes([ph, 1, 1, 0, 1, 0, 0]))
    node_ok = bytes([OP["PLACEHOLDER_NODE"], 0, 1, 2, 1, 1, 0, 1, 0])
    node_bad = bytes([OP["PLACEHOLDER_NODE"], 0, 0, 2, 0, 0, 0, 1, 0])
    seq = bytearray([0xFF, 0xFF]) + records + open_edit() + node_ok + node_bad + publish() + bytes([OP["EMIT"], 0])
    out["scripted-placeholders.bin"] = bytes(seq)
    # A palette fill, which random bytes reached by luck until the op table grew (masks-images-patterns story I1).
    # LIST_PALETTE reads the entry count (pick of 5) and four bytes each, then the none pick (0: no palette).  FILL_FIXED
    # reads: the target, the op count (0), the paint kind (2 + pick of 2), the alpha (pick of 20, minus 2), the fraction
    # bits, the entry, the clip pick (0: a clip push) and the layer; every combination of kind and bits is tried, one
    # of which draws with a palette entry.
    fills = bytearray()
    for kind in (0, 1):
        for bits in (0, 1, 2, 3):
            for alpha in (2, 3, 4, 6):
                fills += bytes([OP["FILL_FIXED"], 0, 0, kind, alpha, bits, 1, 1, 0])
    out["scripted-palette-fill.bin"] = bytes(bytearray([0xFF, 0xFF]) + bytes([OP["LIST_PALETTE"], 3, 255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 1]) + fills)
    # Masks-images-patterns story I1.  INTERN_IMAGE reads: the take pick, the model (gray, RGB, RGBA, CMYK), the kind (8, 12
    # and 16 bits, float, 10 bits, planar), the width and height picks, the row padding, the space pick (6 is reserved, 7
    # and 8 partly given), the flag pick (2 is alpha-only, 3 and 4 and 5 are bad), the reserved pick (0: set), the
    # premultiplied pick, the bad-sample pick (12-bit 4096 at 1; a float NaN, infinity and an alpha of 1.5 at 1, 2, 3), the
    # hostile pick (0: a size past a bound over a small buffer), the base byte and the slot.  IMAGE_NOT_SHAPE reads the slot.
    def image(take, model, kind, wsel, hsel, pad, space, flag, reserved, premult, bad, hostile, base, slot):
        return bytes([OP["INTERN_IMAGE"], take, model, kind, wsel, hsel, pad, space, flag, reserved, premult, bad, hostile, base, slot])
    images = (image(0, 2, 0, 5, 3, 3, 1, 0, 1, 0, 0, 1, 7, 0)           # a straight RGBA8 copy, padded, in sRGB
              + image(1, 1, 2, 4, 2, 0, 0, 0, 1, 0, 0, 1, 9, 1)         # an RGB16 taken
              + image(1, 0, 3, 2, 1, 2, 2, 0, 1, 0, 0, 1, 3, 2)         # a float gray taken
              + image(0, 2, 3, 3, 2, 1, 3, 0, 1, 1, 0, 1, 5, 0)         # a premultiplied RGBA float copy in Display P3
              + image(0, 0, 1, 6, 4, 0, 0, 0, 1, 0, 0, 1, 11, 1)        # a 12-bit gray
              + image(0, 0, 0, 2, 1, 0, 0, 2, 1, 0, 0, 1, 13, 2)        # an alpha-only gray8
              + image(0, 0, 1, 2, 1, 0, 0, 0, 1, 0, 1, 1, 15, 0)        # a 12-bit sample of 4096: refused
              + image(0, 0, 3, 2, 1, 0, 0, 0, 1, 0, 1, 1, 17, 0)        # a float NaN: refused
              + image(0, 0, 3, 2, 1, 0, 0, 0, 1, 0, 2, 1, 19, 0)        # a float infinity: refused
              + image(0, 2, 3, 2, 1, 0, 0, 0, 1, 0, 3, 1, 21, 0)        # a float alpha of 1.5: refused
              + image(0, 1, 0, 2, 1, 0, 0, 0, 0, 0, 0, 1, 23, 0)        # the reserved field set: invalid
              + image(0, 1, 0, 2, 1, 0, 0, 3, 1, 0, 0, 1, 25, 0)        # an undefined flag: invalid
              + image(0, 1, 0, 2, 1, 0, 7, 0, 1, 0, 0, 1, 27, 0)        # a partly given space: invalid
              + image(0, 1, 0, 2, 1, 0, 0, 2, 1, 0, 0, 1, 29, 0)        # alpha-only on RGB: invalid
              + image(1, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 31, 0)        # a take of a borrowed raster: invalid
              + image(0, 3, 0, 2, 1, 0, 0, 0, 1, 0, 0, 1, 33, 0)        # CMYK: unsupported
              + image(0, 1, 4, 2, 1, 0, 0, 0, 1, 0, 0, 1, 35, 0)        # 10 bits: unsupported
              + image(0, 1, 5, 2, 1, 0, 0, 0, 1, 0, 0, 1, 37, 0)        # planar: unsupported
              + image(0, 1, 0, 2, 1, 0, 6, 0, 1, 0, 0, 1, 39, 0)        # ProPhoto: unsupported
              + image(0, 2, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 41, 0)        # 65537 by 1: past the dimension bound
              + image(0, 1, 3, 1, 0, 0, 1, 0, 1, 0, 0, 0, 43, 0)        # 1 by 65537
              + image(0, 2, 3, 2, 0, 0, 1, 0, 1, 0, 0, 0, 45, 0)        # 40000 by 40000: past the texel bound
              + bytes([OP["IMAGE_NOT_SHAPE"], 0, OP["IMAGE_NOT_SHAPE"], 1]))
    # Story I2.  IMAGE_PAINT reads: the image pick (0 to 2 the pool, 3 a path, 4 none), the window mode (0, 1 whole; 2 a
    # sub-rectangle from the next four bytes; 3 empty; 4 past the image), those four bytes, the map (identity, scale 3, scale
    # 1/4, singular, a translation, past the band, NaN), both edge modes and both filters (4 and over are out of range), the
    # filter space, the tint (none, solid, foreground, past N1's bound, a gradient kind) and the item to fill (the last
    # argument of the helper is ignored: the item is pool slot 0).
    def paint(img, wmode, wa, wb, wc, wd, mapsel, ex, ey, mag, mini, space, tsel, where):
        return bytes([OP["IMAGE_PAINT"], img, wmode, wa, wb, wc, wd, mapsel, ex, ey, mag, mini, space, tsel, 0])  # the item is always pool slot 0, the path
    paints = (bytes([OP["LIST_CREATE"], OP["INTERN_PATH"], 0, 0])
              + image(0, 2, 0, 5, 3, 3, 1, 0, 1, 0, 0, 1, 7, 0)         # slot 0: an RGBA8 image, padded, in sRGB
              + image(0, 0, 0, 2, 1, 0, 0, 2, 1, 0, 0, 1, 13, 2)        # slot 2: an alpha-only gray8
              + paint(0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0)         # the whole image, defaults: interned, filled
              + paint(0, 2, 3, 2, 9, 9, 1, 1, 2, 0, 3, 1, 0, 1)         # a window, scaled, REPEAT/REFLECT, in linear sRGB
              + paint(0, 0, 0, 0, 0, 0, 3, 3, 3, 2, 2, 0, 0, 0)         # a singular map: legal
              + paint(0, 0, 0, 0, 0, 0, 4, 0, 0, 3, 3, 0, 0, 2)         # a translation, AREA both ways
              + paint(2, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 0)         # a stencil with a solid tint: interned, tinted
              + paint(2, 1, 0, 0, 0, 0, 2, 0, 0, 1, 1, 1, 2, 3)         # a stencil with the foreground; its filter space dropped
              + paint(2, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0)         # a stencil without a tint: invalid
              + paint(0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 0)         # a color image with a tint: invalid
              + paint(2, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 3, 0)         # a tint past N1's bound: past the bound
              + paint(2, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 4, 0)         # a gradient as the tint: invalid
              + paint(0, 0, 0, 0, 0, 0, 5, 0, 0, 1, 1, 0, 0, 0)         # a map past the band: past the bound
              + paint(0, 0, 0, 0, 0, 0, 6, 0, 0, 1, 1, 0, 0, 0)         # a NaN in the map: invalid
              + paint(0, 3, 1, 1, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0)         # an empty window: invalid
              + paint(0, 4, 1, 1, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0)         # a window past the image: invalid
              + paint(0, 0, 0, 0, 0, 0, 0, 4, 0, 1, 1, 0, 0, 0)         # an edge mode out of range: invalid
              + paint(0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 1, 0, 0, 0)         # a filter out of range: invalid
              + paint(3, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0)         # a path where an image goes: invalid
              + paint(4, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0))        # nothing: invalid
    out["scripted-image-paints.bin"] = bytes(bytearray([0xFF, 0xFF]) + paints)
    # Masks-images-patterns story M1.  INTERN_MASK reads: the picture pick (0 to 7 the pool, 8 a path, 9 a picture of the scene's
    # store, 10 none), the mode (0 and 3 invalid), the coefficient set (4 invalid), the acting-on (2 invalid), the luminance, render
    # and backdrop space picks (0 none, 1 to 3 carried, 4 ProPhoto: unsupported, 5 half given: invalid), the backdrop pick (0 zero,
    # 1 ordinary, 2 at the bound, 3 one step past it, 4 NaN, 5 minus zero), the transfer pick (0 and 1 identity, 2 an exponential,
    # 3 a negative exponent, 4 and 5 the exponent at and past 2^16, 6 a two-sample table, 7 a one-sample table, 8 a two-piece
    # stitching, 9 one with a gap, 10 257 pieces, 11 256, 12 4097 samples), the pool slot and whether an alpha mask is spelled as
    # MK1 asks (nonzero: the coefficient, luminance space and backdrop are cleared).  GROUP_MASK reads the mask's slot (4 none),
    # the placement pick (0 none given, 1 the identity, 2 a translation, 3 a rotation, 4 singular, 5 NaN, 6 three-dimensional,
    # 7 another translation), the isolation, the knockout and the opacity.  NODE_MASK reads two mask variants, the placement
    # (identity, translation, rotation, singular), the isolation pick and the node's own translation.
    def mask(pic, mode, coeff, acts, lsel, rsel, bsel, back, tsel, slot, clean):
        return bytes([OP["INTERN_MASK"], pic, mode, coeff, acts, lsel, rsel, bsel, back, tsel, slot, clean])

    def gmask(slot, place, isolation=0, knockout=0, opacity=4):
        return bytes([OP["GROUP_MASK"], slot, place, isolation, knockout, opacity])

    pop = bytes([OP["GROUP_POP"]])
    start = bytes([OP["LIST_CREATE"], OP["CLOSE"], OP["FREEZE"], 0, OP["LIST_CREATE"]])  # the empty picture in pool slot 0, a fresh list
    made = (mask(0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1)                # slot 0: an alpha mask, identity transfer
            + mask(0, 2, 0, 0, 0, 0, 0, 0, 0, 1, 1)              # slot 1: a luminance mask, W3C
            + mask(0, 2, 2, 1, 1, 2, 1, 1, 2, 2, 1)              # slot 2: luminance, LUM, three spaces, a backdrop, exponential, shape
            + mask(0, 1, 0, 0, 0, 3, 0, 0, 6, 3, 1)              # slot 3: alpha in a render space, a two-sample table
            + mask(0, 1, 0, 0, 0, 0, 0, 5, 8, 0, 1)              # slot 0 again: a two-piece stitching, a backdrop of minus zero
            + mask(0, 1, 0, 0, 0, 0, 0, 0, 11, 0, 1)             # 256 pieces: at the bound
            + mask(0, 1, 0, 0, 0, 0, 0, 0, 8, 0, 1)              # back to two pieces in slot 0
            + mask(0, 2, 0, 0, 0, 0, 0, 2, 4, 1, 1)              # the backdrop and the exponent at their bounds
            + mask(0, 2, 3, 1, 3, 0, 0, 0, 0, 1, 1)             # slot 1: SPACE_Y in a gamma space, acting on the shape
            + mask(0, 1, 0, 0, 0, 0, 0, 0, 0, 3, 1))             # slot 3: the first again (found)
    refused = (mask(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1)             # mode zero
               + mask(0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 1)           # mode past the enum
               + mask(0, 1, 4, 0, 0, 0, 0, 0, 0, 0, 1)           # a coefficient set past the enum
               + mask(0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0)           # a coefficient set on an alpha mask
               + mask(0, 1, 0, 2, 0, 0, 0, 0, 0, 0, 1)           # acting-on past the enum
               + mask(0, 1, 0, 0, 2, 0, 0, 0, 0, 0, 0)           # a luminance space on an alpha mask
               + mask(0, 2, 0, 0, 5, 0, 0, 0, 0, 0, 1)           # a half-given space
               + mask(0, 2, 0, 0, 0, 0, 0, 4, 0, 0, 1)           # a NaN backdrop
               + mask(0, 2, 0, 0, 0, 0, 0, 0, 3, 0, 1)           # a negative exponent
               + mask(0, 2, 0, 0, 0, 0, 0, 0, 7, 0, 1)           # a one-sample table
               + mask(0, 2, 0, 0, 0, 0, 0, 0, 9, 0, 1)           # a stitching with a gap
               + mask(8, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1)           # a path where a picture goes
               + mask(9, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1)           # a picture of another store
               + mask(10, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1)          # no picture
               + mask(1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1)           # an empty pool slot
               + mask(0, 2, 0, 0, 4, 0, 0, 0, 0, 0, 1)           # a space this library does not carry
               + mask(0, 2, 0, 0, 0, 0, 0, 3, 0, 0, 1)           # a backdrop past 2^16
               + mask(0, 1, 0, 0, 0, 0, 0, 0, 5, 0, 1)           # an exponent past 2^16
               + mask(0, 1, 0, 0, 0, 0, 0, 0, 10, 0, 1)          # 257 pieces
               + mask(0, 1, 0, 0, 0, 0, 0, 0, 12, 0, 1))         # 4097 samples
    groups = (gmask(0, 0) + pop                                   # a group with a mask and no placement given
              + gmask(1, 2, 1, 1) + pop                           # placed, non-isolated knockout
              + gmask(2, 3) + pop + gmask(3, 4) + pop             # rotated, singular
              + gmask(0, 1) + pop + gmask(1, 7, 0, 1, 2) + pop    # an identity given, another translation
              + gmask(2, 5) + gmask(2, 6)                         # NaN: invalid; three-dimensional: unsupported
              + gmask(4, 2) + gmask(4, 5) + gmask(4, 6)           # a placement and no mask: invalid
              + gmask(4, 0) + pop)                                # no mask, no placement: an ordinary group
    frozen = gmask(0, 2) + pop + bytes([OP["CLOSE"], OP["FREEZE"], 1])  # a picture holding a masked group, into pool slot 1
    nested = bytes([OP["LIST_CREATE"]]) + mask(1, 1, 0, 0, 0, 0, 0, 0, 0, 2, 1) + gmask(2, 2) + pop + bytes([OP["CLOSE"], OP["FREEZE"], 2])
    out["scripted-masks.bin"] = bytes(bytearray([0xFF, 0xFF]) + start + made + refused + groups + frozen + nested)
    # The same under a picture depth of one: a picture holding a masked group is one level deeper than the mask's picture, so its
    # freeze passes the bound and is refused (b0 = 0xCF: depth 256, items 2^20, picture depth 1, expansion 2^24).
    out["scripted-masks-depth.bin"] = bytes(bytearray([0xCF, 0xFF]) + start + made[:12] + gmask(0, 0) + pop + bytes([OP["CLOSE"], OP["FREEZE"], 1]))
    # A group node of a scene of its own: two mask variants, a placement, an isolation pick and the node's translation.
    def node(va, vb, place, iso, tr):
        return bytes([OP["NODE_MASK"], va, vb, place, iso, tr])
    out["scripted-mask-nodes.bin"] = bytes(bytearray([0xFF, 0xFF]) + node(1, 3, 1, 1, 1) + node(0, 0, 0, 0, 0) + node(2, 5, 2, 2, 2) + node(4, 1, 3, 0, 1))
    out["scripted-images.bin"] = bytes(bytearray([0xFF, 0xFF]) + images)
    # The same under a store whose bytes are bounded (b1 = 0x40: max_bytes 1500): an image that does not fit is refused by
    # the store's limit, and one that does is interned.
    out["scripted-images-small-store.bin"] = bytes(bytearray([0xFF, 0x40]) + images)
    # An instance of a picture: a closed empty list frozen into pool slot 0, a new list, and an instance of it under
    # the identity (INSTANCE reads the map and the picture's slot).  Random inputs reached it by luck until the op
    # table grew (strokes story S1).
    seq = bytearray([0xFF, 0xFF]) + bytes([OP["LIST_CREATE"], OP["CLOSE"], OP["FREEZE"], 0, OP["LIST_CREATE"],
                                           OP["INSTANCE"], 0, 0])
    out["scripted-instance-picture.bin"] = bytes(seq)
    # Found by strokes story S1's libFuzzer run: a fill at N1's bound in a list whose expansion was at its limit, which
    # the color-bound op's check took for a past value (the limit was the list's own).
    out["regression-fill-at-the-expansion-limit.bin"] = base64.b64decode(
        "DoEB6sekMh674RIT4sKMJ6F6h3LIVP9NGQzMjfHFAVqKzSYS6ywzWshc8eGAShPF3ZrhE2jTq+xZZqikG8KioBrzKzqshSF27Y9UDk5NSionddkd"
        "78lOLWCck9UTBDsbQcLke+0HMtqwD3qSNicjvXJCmasGiyZNrKL4+Pj4+Pj4+Pj4//9QkAASPDWMvbyVsbMENWisGft2eWBrZiQjP4dsT8wD6ATg"
        "gYKphMosEmFRFZt8ERopJ1MQ8ckpDkNYXf//EQAEEQEEJwACAQAEAScB////////////////////////////////////////////////////////"
        "/////////////////////////////////////////////////////////////////////////////////////////////////////////wQAAQQC"
        "GgD6KrHaqqKEUMS1zBrhqLAci9QyRJq0polEE7/qCDTxvy63dBP9r5bQmQLZVV3SPRIBb3CssId5+sDpzikXNNg4fEV/n72zUFSkAQWsIF8GTspf"
        "Qy7ZoEoTq1FD13hJcVscjhqMbZSb9UfzOGd0TQHSIiiPChISSAWEOdqqooRQxLXMGuGosByL1DJEmrSmiUQTv+oINPG/Lrd0E/2vltCZAtlVXdI9"
        "EgFvcKywh3n6wOnOKRc02Dh8RX+fvbNQVKQBBawgXwZOyl9DLtmgShOrUUPXeElxWxyOGoxtlJv1R/M4Z3RNAdIiKI8KEhJIBYQ5AXFwhGkUKb3Y"
        "cCxx14eJGfBlhZyAFHE0OyKqBhPjLcD5PSc/odkiOGChsDE+PdAW17XyuUM9gIQBe0ftPMdVRyrgcmScvAFxcIRpFCm92HAscdeHiRnwZYWcgBRx"
        "NDsiqgYT4y3A+T0nP6HZIjhgobAxPj3QFte18rlDPYCEAXtH7TzHVUcq4HJknLw0kBJvXyZniNJVwBprjRDQLNjyudE=")
    # Found by story 16's libFuzzer soak: an instance of its own parent group, entered first as another instance's
    # target, which publish answered with ERR_INTERNAL instead of a cycle problem.
    out["regression-cycle-entered-through-an-instance.bin"] = base64.b64decode(
        "//8RAP+LQTMRAAQTAAAFAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA8D8AAAAAAAAAAAAAAAAAAAAAAAAFAQsAAAAB/AEAAAAAAAAA"
        "EwAEDkEyEQAEEwAmAAUBCwCRAQAAAQIBAAAAAAAAABMAAAUBAQIAAAAAAP//////////BQAABdYBAQIAAAAAAAATAAAFQTMRAAQTAAAFAQsA"
        "AAAB/AEAAAAAAAAAEwAADkEyEQAEEwAmAAUBCwCRAQAAAQIBAAABAABsAAAAAAATAAAFAQECAAAAAAD//////////wUAAAXWAQECAAAAAAAA"
        "BPQTAAAFAAECAAgGAQAAACYAAgABAAACEwACBQEBAgAAAAAAAN9aWlpaWlpaWlpaWlpaWlpaWlpaWlpaWlpaWlpaWlpaWlpaWlpaWlpaWlpa"
        "WlpaZ7oTapgUjxzHmnYBsmMDLIx6GcM4icjtFxqPhZTLIxNKEujxMSxKe4IkYV30ehwTBxIkQHkomtPlOjK9MpsYw8yX2kChtOodJEOllc8R"
        "i8luVzMruI4sLz71UVCE7g70egrOBBNc6G1nmXW7GiAAAQEAAhMAAgUBAQIAAOXl5eXl5eXlAgABAQAC")
    return out


def files():
    out = {}
    for i in range(BIASED_FILES):
        out["biased-%03d.bin" % i] = biased(i + 1)
    for i in range(PLAIN_FILES):
        out["plain-%03d.bin" % i] = plain(i + 1)
    out.update(scripted())
    return out


def main():
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tests", "fuzz", "corpus", "record")
    want = files()
    if "--check" in sys.argv:
        have = sorted(os.listdir(root)) if os.path.isdir(root) else []
        if have != sorted(want):
            sys.exit("gen-fuzz-corpus: the committed corpus is not the generator's set of files")
        for name, data in want.items():
            with open(os.path.join(root, name), "rb") as f:
                if f.read() != data:
                    sys.exit("gen-fuzz-corpus: %s differs from the generator's" % name)
        print("gen-fuzz-corpus: the committed corpus (%d files) is current" % len(want))
        return
    os.makedirs(root, exist_ok=True)
    for name, data in want.items():
        with open(os.path.join(root, name), "wb") as f:
            f.write(data)
    print("gen-fuzz-corpus: wrote %d files" % len(want))


if __name__ == "__main__":
    main()
