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
"""Write the seed corpus of the render-request fuzzer, deterministically (story 13a, CAP-15, AD-19).

    python3 tools/gen-render-corpus.py            # write tests/fuzz/corpus/render/
    python3 tools/gen-render-corpus.py --check    # fail if the committed files differ

Every file is an input of tests/fuzz/fuzz_render.c: eight header bytes (the test
sink's caps, the request, where the token is set, the extent limit and the
report's cap, the write and the allocation that fail, the units of a deferred
step, the stroke caps), then an op count and ops of four bytes each (op, a, b, c).  The **random**
files are bytes from a seeded generator, read as valid input by construction;
the **scripted** ones each reach one outcome the replay requires (`make
fuzz-replay` fails if the corpus as a whole misses one), and say which in their
names.  Keep the op numbers in step with the C file.
"""
import os
import sys

FILL, CLIP, CLIP_POP, GROUP, GROUP_POP, INSTANCE, FONT_RUN, OUTLINE_RUN, FREEZE, FOREGROUND, STROKE, PLACEHOLDER = range(12)
RANDOM_OPS = 11  # the random files draw ops 0 to 10: the placeholder (P5) is only in the scripted ones, so they read as they did
RANDOM_FILES = 96


class Rng:
    def __init__(self, seed):
        self.s = (seed * 0x9E3779B97F4A7C15 + 0x5EED) & (2**64 - 1)

    def next(self):
        s = self.s
        s ^= s >> 12
        s ^= (s << 25) & (2**64 - 1)
        s ^= s >> 27
        self.s = s
        return (s * 2685821657736338717) & (2**64 - 1)

    def byte(self):
        return self.next() >> 56


def header(caps=0, request=0, cancel=0, limits=0, write=0, alloc=0, step=0, stroke=0):
    return bytes([caps, request, cancel, limits, write, alloc, step, stroke])


def ops(*items):
    out = bytearray([len(items) - 1 if items else 0])
    for it in items:
        out += bytes(it)
    return bytes(out)


def fill(x=2, y=2, c=3):
    return (FILL, x, y, c)


def random_file(i):
    r = Rng(i)
    head = [r.byte() for _ in range(8)]
    # Most random inputs cancel nowhere, fail nothing and name a list, so that they reach the end of a render.
    if r.byte() < 160:
        head[2] = 0 if r.byte() < 200 else r.byte() % 24
        head[4] = 0
        head[5] = 0
    n = 8 + r.byte() % 60
    body = bytearray([n])
    for _ in range(n):
        body += bytes([r.byte() % RANDOM_OPS, r.byte(), r.byte(), r.byte()])
    return bytes(head) + bytes(body)


def scripted():
    out = {}
    simple = ops(fill(), fill(10, 10, 5), fill(20, 4, 7))
    tie = ops((GROUP, 0, 0, 1), fill(3, 3, 4), fill(5, 5, 4), (GROUP_POP, 0, 0, 0), fill(30, 30, 2))
    out["scripted-ok.bin"] = header() + simple
    out["scripted-strict-planned.bin"] = header(caps=1, request=1) + ops(fill(), (FONT_RUN, 4, 0, 0), fill())
    out["scripted-strict-drawing.bin"] = header(request=1, limits=1) + ops(fill(1, 1, 2), fill(0, 0, 19))
    out["scripted-cancel-start.bin"] = header(cancel=0xFF) + simple
    out["scripted-cancel-mid.bin"] = header(cancel=2) + simple
    out["scripted-before-submit.bin"] = header(caps=32, cancel=1) + simple
    out["scripted-workspace-small.bin"] = header(request=2 | 0x10) + simple
    out["scripted-workspace-exact.bin"] = header(request=2 | 0x20) + tie
    out["scripted-report-cap.bin"] = header(caps=4, limits=8) + ops((CLIP, 0, 0, 30), fill(), (CLIP_POP, 0, 0, 0),
                                                                     (CLIP, 1, 1, 30), fill(), (CLIP_POP, 0, 0, 0))
    out["scripted-io.bin"] = header(request=4, write=3) + simple
    out["scripted-oom.bin"] = header(alloc=2) + simple
    out["scripted-seamless.bin"] = header(request=8) + simple
    out["scripted-caller-memory-refused.bin"] = header(caps=64, request=2) + simple
    out["scripted-approximated.bin"] = header(caps=8) + ops((GROUP, 1, 0, 0), fill(), (GROUP_POP, 0, 0, 0))
    out["scripted-ties.bin"] = header() + tie
    out["scripted-deferred.bin"] = header(caps=16, step=2) + tie
    out["scripted-deferred-affine.bin"] = header(caps=16 | 128, step=1) + simple
    out["scripted-invalid.bin"] = header(cancel=0xE0) + simple
    out["scripted-unsupported.bin"] = header(cancel=0xF0) + simple
    out["scripted-damage-extent.bin"] = header(request=64 | 128, step=3, write=5, alloc=0) + ops(
        fill(0, 0, 19), fill(30, 30, 19), (FREEZE, 0, 0, 0), (INSTANCE, 7, 9, 0), (OUTLINE_RUN, 3, 0, 0),
        (FOREGROUND, 1, 2, 3))
    # Gradients (color-paint story P1, S17): op FOREGROUND with a & 12 == 12 is a gradient fill, b % 6 the gradient
    # (0 linear, 1 radial, 2 sweep, 3 and 4 degenerate, 5 a missing palette stop), c & 0xe0 == 0xe0 a singular map.
    gradients = ops((FOREGROUND, 12, 0, 3), (FOREGROUND, 13, 1, 4), (FOREGROUND, 12, 2, 5), (FOREGROUND, 12, 3, 6),
                    (FOREGROUND, 12, 4, 7), (FOREGROUND, 12, 5, 8), (FOREGROUND, 12, 0, 0xE3), fill())
    out["scripted-gradients-stream.bin"] = header(request=4) + gradients
    out["scripted-gradients-strict.bin"] = header(request=1, step=32 | 128) + gradients
    out["scripted-gradients-none.bin"] = header(request=4, step=64) + gradients
    # Story P2: b >= 200 builds the gradient from the bytes after the op: six values (b % 64 - 16, halved when b & 64;
    # 252 to 255 G14's bounds and one step past each), the kind byte (k % 3: linear, radial, sweep; k & 4 an isoline;
    # (k >> 3) % 4 the unit), the flags (extend, premultiplied, linear sRGB, a turned or flat map), the stop count and
    # (offset, color) per stop.  A linear repeat under a turned map with a solid, a foreground and a palette stop; a
    # radial cone, premultiplied in linear sRGB; a sweep in degrees; and p0's x one step past 2^64 (refused).
    built = [bytes([FOREGROUND, 12, 200, 3]) + bytes([18, 18, 46, 20, 16, 16, 0, 0x11, 3, 0, 0xF0, 4, 4, 8, 5]),
             bytes([FOREGROUND, 13, 201, 4]) + bytes([20, 20, 17, 40, 30, 28, 1, 0x0C, 2, 0, 0x10, 8, 0xE1]),
             bytes([FOREGROUND, 12, 202, 5]) + bytes([24, 24, 16, 106, 16, 16, 11, 0x02, 2, 0, 0x30, 8, 0xC4]),
             bytes([FOREGROUND, 12, 203, 6]) + bytes([253, 16, 30, 16, 16, 16, 0, 0, 2, 0, 0x30, 8, 0xC4])]
    body = bytearray([len(built) - 1])
    for b in built:
        body += b
    out["scripted-gradients-built.bin"] = header(request=4) + bytes(body)
    # N1 (Corey, 2026-10-21): fills whose c byte has its two high bits set take a color at 2^16 (b >> 5 = 0, 1) or one
    # step past it (2, 3) in gamma 10 (a & 2) or gamma 0.1; a group with a % 4 = 3 and c's high bit blends in gamma 0.1;
    # and a built gradient interpolating in linear sRGB (flags 0x08) whose stop color byte is 0xF0 holds 2^16 in gamma
    # 10, resolved to the saturation.
    sat = bytes([FOREGROUND, 12, 204, 3]) + bytes([16, 16, 24, 16, 16, 16, 0, 0x08, 1, 0, 0xF0])
    out["scripted-color-bound.bin"] = header(request=4) + ops(
        (GROUP, 3, 0, 0x80), (FILL, 2, 0x00, 0xC3), (FILL, 0, 0x20, 0xC4), (GROUP_POP, 0, 0, 0), (FILL, 3, 0x40, 0xC5),
        (FILL, 1, 0x60, 0xC6), tuple(sat[:4])) + sat[4:]
    # Color-paint story P3.  A group's b byte picks its operator (b % 28: 23 multiply, 24 hue) and, by its top three
    # bits, PDF's fields (101 non-isolated, 110 knockout, 111 both); the limits byte's bit 6 makes a sink of the
    # separable blend modes only, and bit 7 one that claims PDF's fields.  A multiply group recorded on a stream; a hue
    # group that sink refuses; a non-isolated soft-light group (b = 0xA0) that the default caps refuse, and recorded
    # with its fields by the sink that claims them.
    blend = ops((GROUP, 0, 23, 0), fill(), (GROUP_POP, 0, 0, 0))
    out["scripted-blend-recorded.bin"] = header(request=4) + blend
    out["scripted-blend-refused.bin"] = header(limits=0x40) + ops((GROUP, 0, 24, 0), fill(), (GROUP_POP, 0, 0, 0))
    pdf = ops(fill(1, 1, 5), (GROUP, 0, 0xA0, 0), fill(), (GROUP_POP, 0, 0, 0))
    out["scripted-pdf-refused.bin"] = header() + pdf
    out["scripted-pdf-recorded.bin"] = header(request=4, limits=0x80) + pdf
    # Strokes story S1 (S20).  Op STROKE strokes a square, or a glyph run when c & 0x10 (from a font when c & 0x08),
    # with style c >> 5 (0 SK1's defaults, 1 width 0, 2 a device hairline, 3 odd dashes with square caps, 4 miter-clip,
    # 5 arcs with dashes across subpaths, 6 inside alignment, 7 outside by even-odd), under an anisotropic map when
    # a & 0x80 and a singular one when a & 0x40 alone, with a gradient when b's two high bits are set.  The stroke
    # header byte: bit 0 expands what the caps lack, bits 1 and 2 pick the features they emit (every one, a writer's,
    # a toolpath's, none), bit 3 drops the stroke kind.
    strokes = ops((STROKE, 2, 2, 0x03), (STROKE, 4, 4, 0x23), (STROKE, 6, 6, 0x45), (STROKE, 0x88, 8, 0x63),
                  (STROKE, 10, 10, 0x84), (STROKE, 12, 0xC0, 0xA5), (STROKE, 0x4E, 14, 0xC6), (STROKE, 16, 16, 0xE7),
                  (STROKE, 1, 0, 0x18), (STROKE, 2, 0, 0x10), fill())
    out["scripted-strokes-every.bin"] = header(request=4) + strokes
    out["scripted-strokes-writer.bin"] = header(request=4, stroke=1 | 2) + strokes
    out["scripted-strokes-toolpath.bin"] = header(request=4, stroke=4) + strokes
    out["scripted-strokes-none-strict.bin"] = header(request=1, stroke=6) + strokes
    out["scripted-strokes-no-kind.bin"] = header(stroke=8) + strokes
    out["scripted-strokes-raster.bin"] = header(stroke=1 | 4) + strokes
    # Color-paint story P5 (PH1, PH3 to PH6).  Op PLACEHOLDER (a, b, c): source 1 + a % 3 (0 when c & 0x80, invalid),
    # code 1 + b % 3 (0 when c & 0x40, invalid), approximated when c & 1 (2, invalid, with c & 4), a box when c & 0x20, a
    # translation when a & 0x80.  A refused one with a box between fills, an approximated one, an invalid source, an invalid
    # code and an invalid flag; strict (a refused placeholder is planned: the sink is never called); the same on a raster
    # target; and an approximated one alone under strict, which never fails.
    placeholders = ops(fill(), (PLACEHOLDER, 0, 1, 0x20), (PLACEHOLDER, 0x81, 2, 0x21), fill(10, 10, 5), (PLACEHOLDER, 1, 1, 0x80),
                       (PLACEHOLDER, 1, 1, 0x40), (PLACEHOLDER, 1, 1, 0x05), (PLACEHOLDER, 2, 0, 0))
    out["scripted-placeholders.bin"] = header(request=4) + placeholders
    out["scripted-placeholders-strict.bin"] = header(request=1) + placeholders
    out["scripted-placeholders-raster.bin"] = header() + placeholders
    out["scripted-placeholders-approximated.bin"] = header(request=1) + ops(fill(), (PLACEHOLDER, 0, 1, 0x21), fill(4, 4, 3))
    # Masks-images-patterns story M1 (MK9, MK14).  A group whose c byte has bits 4 to 6 set takes a mask over a picture frozen before
    # it: mode b & 1 (luminance), coefficient set (a >> 2) & 3, a space b & 2, a backdrop c & 1, a transfer (a >> 4) & 3 (identity,
    # exponential, table, stitching), acting on a & 0x40, a placement c & 0x80.  The stroke header's high nibble takes mask features
    # from the caps: 1 the flag and all, 2 a writer's set, 3 the table and the stitching, 4 the shape and the backdrop, 5 all
    # but alpha.  A mask recorded by the sink with every feature; refused for its table; refused by a writer's set for a transfer, a
    # coefficient set and the shape; refused without the flag; and a refused placeholder inside a mask's picture, reported with the
    # chain [group, item].
    def masked(a, b, c=0x70):
        return (FILL, 2, 2, 6), (FREEZE, 0, 0, 0), (GROUP, a, b, c), (FILL, 4, 4, 9), (GROUP_POP, 0, 0, 0)
    out["scripted-mask-recorded.bin"] = header(request=4) + ops(*masked(0x04, 1, 0xF1))
    out["scripted-mask-refused-table.bin"] = header(stroke=0x30) + ops(*masked(0x20, 0))
    out["scripted-mask-refused-writer.bin"] = header(stroke=0x20) + ops(*masked(0x78, 3, 0xF1))
    out["scripted-mask-refused-strict.bin"] = header(request=1, stroke=0x50) + ops(*masked(0x04, 1))
    out["scripted-mask-refused-flag.bin"] = header(stroke=0x10) + ops(*masked(0x04, 1))
    out["scripted-mask-refused-shape.bin"] = header(stroke=0x40) + ops(*masked(0x40, 1, 0xF1))
    out["scripted-mask-picture-entry.bin"] = header() + ops(fill(), (PLACEHOLDER, 0, 1, 0x20), (FREEZE, 0, 0, 0),
                                                           (GROUP, 0, 0, 0x70), fill(4, 4, 9), (GROUP_POP, 0, 0, 0))
    return out


def files():
    out = {"random-%03d.bin" % i: random_file(i + 1) for i in range(RANDOM_FILES)}
    out.update(scripted())
    return out


def main():
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tests", "fuzz", "corpus", "render")
    want = files()
    if "--check" in sys.argv:
        have = sorted(os.listdir(root)) if os.path.isdir(root) else []
        if have != sorted(want):
            sys.exit("gen-render-corpus: the committed corpus is not the generator's set of files")
        for name, data in want.items():
            with open(os.path.join(root, name), "rb") as f:
                if f.read() != data:
                    sys.exit("gen-render-corpus: %s differs from the generator's" % name)
        print("gen-render-corpus: the committed corpus (%d files) is current" % len(want))
        return
    os.makedirs(root, exist_ok=True)
    for name, data in want.items():
        with open(os.path.join(root, name), "wb") as f:
            f.write(data)
    print("gen-render-corpus: wrote %d files" % len(want))


if __name__ == "__main__":
    main()
