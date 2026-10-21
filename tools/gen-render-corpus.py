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

Every file is an input of tests/fuzz/fuzz_render.c: seven header bytes (the test
sink's caps, the request, where the token is set, the extent limit and the
report's cap, the write and the allocation that fail, the units of a deferred
step), then an op count and ops of four bytes each (op, a, b, c).  The **random**
files are bytes from a seeded generator, read as valid input by construction;
the **scripted** ones each reach one outcome the replay requires (`make
fuzz-replay` fails if the corpus as a whole misses one), and say which in their
names.  Keep the op numbers in step with the C file.
"""
import os
import sys

FILL, CLIP, CLIP_POP, GROUP, GROUP_POP, INSTANCE, FONT_RUN, OUTLINE_RUN, FREEZE, FOREGROUND = range(10)
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


def header(caps=0, request=0, cancel=0, limits=0, write=0, alloc=0, step=0):
    return bytes([caps, request, cancel, limits, write, alloc, step])


def ops(*items):
    out = bytearray([len(items) - 1 if items else 0])
    for it in items:
        out += bytes(it)
    return bytes(out)


def fill(x=2, y=2, c=3):
    return (FILL, x, y, c)


def random_file(i):
    r = Rng(i)
    head = [r.byte() for _ in range(7)]
    # Most random inputs cancel nowhere, fail nothing and name a list, so that they reach the end of a render.
    if r.byte() < 160:
        head[2] = 0 if r.byte() < 200 else r.byte() % 24
        head[4] = 0
        head[5] = 0
    n = 8 + r.byte() % 60
    body = bytearray([n])
    for _ in range(n):
        body += bytes([r.byte() % 10, r.byte(), r.byte(), r.byte()])
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
