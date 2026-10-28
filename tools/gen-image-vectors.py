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
"""Write tests/image_vectors.h: the known answers of the image sampling definition (IP2 to IP9, IP12).

Four tables, all from tools/image-oracle.py (exact rationals; transfers and gamut matrices in 110-digit decimal):

- **Images** (`kImages`): small images of every layout, sample type, bit depth and alpha form (alpha-only included),
  with their samples as stored (a float is its IEEE bit pattern).  A few are wider than one 64-texel tile.
- **Points** (`kPoints`): the premultiplied value in the layer space at an image-space point for NEAREST, LINEAR
  and CUBIC, over every edge mode on each axis, a full, a partial and a one-texel window, texel centres, the
  places between them, window edges and points many periods outside the image, the filter space and the layer
  space; `in_band` marks a NEAREST point within 2^-16 texel of a texel boundary, where a sink may take either
  neighbour (IP12).
- **Areas** (`kAreas`): the exact mean of the extended image over a parallelogram (AREA).
- **Stretches** (`kStretches`): IP8's predicate on the four entries of a map's linear part.

`tolerance` is 0 where a double evaluation is exact (no conversion, a linear filter, short dyadic factors) and
otherwise max(1e-12, 2^-47 C / max(1, max |expected|)) with C the vector's condition sum, scaled by
max(1, the largest |expected component| of the pixel).  A zero image space or filter space is the target's space, which the
vectors take to be `layer`; the zero `layer` is used only where every space of the case is zero.

    python3 tools/gen-image-vectors.py > tests/image_vectors.h
    python3 tools/gen-image-vectors.py --check

The checks the oracle makes of itself (`python3 tools/image-oracle.py --selftest`) are what the answers rest on.
"""
import importlib.util
import math
import os
import struct
import sys
from fractions import Fraction as F

HERE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location("image_oracle", os.path.join(HERE, "image-oracle.py"))
O = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(O)

PAD, REPEAT, REFLECT, NONE = O.PAD, O.REPEAT, O.REFLECT, O.NONE
NEAREST, LINEAR, CUBIC, AREA = O.NEAREST, O.LINEAR, O.CUBIC, O.AREA
UNORM, FLOAT, STRAIGHT, PREMULT = O.UNORM, O.FLOAT, O.STRAIGHT, O.PREMULTIPLIED
ZERO = O.ZERO
SRGB, LIN, P3, ADOBE22, BT2020 = (1, 2, 0.0), (1, 1, 0.0), (3, 2, 0.0), (2, 3, 2.2), (4, 1, 0.0)
P3LIN, GAMMA24, BT2020G = (3, 1, 0.0), (1, 3, 2.4), (4, 3, 2.2)
FULL = (0, 0, 0, 0)
MODE = "PRFN"
FILT = ("nearest", "linear", "cubic")


def hexd(x):
    """A double as a C++ hexadecimal floating literal, without trailing zeros of the mantissa (0.0 for zero)."""
    x = float(x)
    if x == 0.0:
        return "-0.0" if math.copysign(1.0, x) < 0 else "0.0"
    h = x.hex()
    mant, exp = h.split("p")
    if "." in mant:
        mant = mant.rstrip("0").rstrip(".")
    return mant + "p" + exp


# --- the images ---------------------------------------------------------------------------------------------------------
_state = [0x2545F491]


def rng():
    x = _state[0]
    x ^= (x << 13) & 0xFFFFFFFF
    x ^= x >> 17
    x ^= (x << 5) & 0xFFFFFFFF
    _state[0] = x
    return x


FL = O.float_bits
POOL = [0.0, 0.25, 0.5, 0.75, 1.0, 0.125, -0.5, 1.5, 0.375, 2.0, 0.625, -0.25]
APOOL = [0.0, 0.25, 0.5, 0.75, 1.0, 0.125, 0.875, 0.5]
BIG = [-3.0, 0.5, 1e30, -1e30, 0.25, 1.5, 0.0, 1.0, 0.75, -0.125]

IMAGES = []
INDEX = {}


def add_image(name, w, h, layout, typ, bits, aform, aonly, space, samples):
    assert len(samples) == w * h * layout and name not in INDEX
    INDEX[name] = len(IMAGES)
    IMAGES.append(O.Image(name, w, h, layout, typ, bits, aform, aonly, space, samples))


def unorm(name, w, h, layout, bits, aform, aonly, space, special=()):
    top = (1 << bits) - 1
    s = [rng() % (top + 1) for _ in range(w * h * layout)]
    s[:layout] = [top] * layout                        # the first texel is all maximum (4095 for 12 bits)
    s[layout:2 * layout] = [0] * layout                # the second is all zero
    for texel, vals in special:
        s[texel * layout:(texel + 1) * layout] = list(vals)
    add_image(name, w, h, layout, UNORM, bits, aform, aonly, space, s)


def floats(name, w, h, layout, aform, aonly, space, pool, special=()):
    s = []
    for _ in range(w * h):
        for c in range(layout):
            alpha = aonly or (layout == 4 and c == 3)
            p = APOOL if alpha else pool
            s.append(FL(p[rng() % len(p)]))
    for texel, vals in special:
        s[texel * layout:(texel + 1) * layout] = [FL(v) for v in vals]
    add_image(name, w, h, layout, FLOAT, 32, aform, aonly, space, s)


unorm("g8", 4, 3, 1, 8, 0, 0, SRGB)
unorm("rgb8", 4, 4, 3, 8, 0, 0, SRGB)
unorm("rgba8s", 4, 4, 4, 8, STRAIGHT, 0, SRGB, special=[(2, (50, 60, 70, 0)), (3, (10, 200, 90, 255))])
unorm("rgba8p", 4, 4, 4, 8, PREMULT, 0, SRGB, special=[(2, (50, 60, 70, 0)), (3, (200, 10, 30, 100)), (5, (40, 40, 40, 128))])
unorm("rgba8s_p3", 4, 4, 4, 8, STRAIGHT, 0, P3)
unorm("g12", 4, 2, 1, 12, 0, 0, LIN)
unorm("rgb12", 3, 3, 3, 12, 0, 0, P3)
unorm("rgba12s", 3, 3, 4, 12, STRAIGHT, 0, SRGB)
unorm("rgba12p", 3, 3, 4, 12, PREMULT, 0, LIN, special=[(2, (4000, 100, 7, 0)), (4, (3000, 2000, 100, 2048))])
unorm("g16", 4, 2, 1, 16, 0, 0, SRGB)
unorm("rgb16", 3, 3, 3, 16, 0, 0, ADOBE22)
unorm("rgba16s", 3, 3, 4, 16, STRAIGHT, 0, BT2020)
unorm("rgba16p", 3, 3, 4, 16, PREMULT, 0, P3, special=[(2, (500, 600, 700, 0)), (4, (60000, 100, 200, 40000))])
floats("gf", 4, 3, 1, 0, 0, LIN, BIG)
floats("rgbf", 3, 3, 3, 0, 0, SRGB, BIG)
floats("rgbaf_s", 4, 4, 4, STRAIGHT, 0, LIN, POOL)
floats("rgbaf_p", 4, 4, 4, PREMULT, 0, LIN, POOL, special=[(1, (0.75, 0.25, 0.5, 0.25)), (2, (0.5, 0.0, 0.25, 0.0))])
floats("rgbaf_pb", 3, 3, 4, PREMULT, 0, SRGB, BIG,
       special=[(0, (1e30, -3.0, 0.5, 0.5)), (4, (0.25, -1e30, 2.0, 0.75))])
floats("rgbaf_sb", 3, 3, 4, STRAIGHT, 0, SRGB, BIG, special=[(0, (1e30, -3.0, 0.5, 0.5)), (4, (0.25, -1e30, 2.0, 0.0))])
unorm("a8", 4, 4, 1, 8, 0, 1, ZERO)
unorm("a12", 3, 3, 1, 12, 0, 1, ZERO)
unorm("a16", 4, 3, 1, 16, 0, 1, ZERO)
floats("af", 4, 3, 1, 0, 1, ZERO, POOL)
unorm("rgb8z", 4, 4, 3, 8, 0, 0, ZERO)
floats("rgbafz", 4, 4, 4, STRAIGHT, 0, ZERO, POOL)
unorm("wide70", 70, 3, 4, 8, STRAIGHT, 0, SRGB)
floats("one1", 1, 1, 4, STRAIGHT, 0, LIN, POOL, special=[(0, (0.5, 0.25, 0.75, 0.5))])
floats("edge", 5, 4, 4, STRAIGHT, 0, LIN, POOL)
STEP = [(0.0 if x < 4 else 1.0) if y != 2 else 0.5 for y in range(4) for x in range(8)]
add_image("step", 8, 4, 1, FLOAT, 32, 0, 0, LIN, [FL(v) for v in STEP])
floats("oor", 6, 3, 3, 0, 0, LIN, BIG, special=[(0, (-3.0, 0.5, 1e30)), (1, (0.5, -1e30, 4.0)), (2, (2.0, 2.0, 2.0))])
add_image("const8", 3, 3, 1, UNORM, 8, 0, 0, LIN, [100] * 9)

# --- the tables ----------------------------------------------------------------------------------------------------------
POINTS = []
AREAS = []
STRETCHES = []


def layer_of(im):
    return im.space if im.space != ZERO else SRGB


def near_boundary(x):
    x = F(x)
    return abs(x - round(x)) <= F(1, 65536)


def tolerance(exact, info, r):
    """0 where a double evaluation is exact; else max(1e-12, 2^-47 C / max(1, max |expected|)), C the condition
    sum of the vector's taps (image-oracle.condition): the bound a sink gets is max(1e-12 scale, 2^-47 C)."""
    if exact:
        return 0.0
    scale = max(F(1), max(abs(c) for c in r))
    return max(1e-12, float(F(info["condition"]) / (2 ** 47) / scale))


def add_pt(imname, win, ex, ey, filt, fsp, layer, u, v, tag):
    im = IMAGES[INDEX[imname]]
    if im.alpha_only:
        fsp = ZERO
    info = {}
    r = O.sample(im, win, ex, ey, filt, fsp, layer, u, v, info)
    exact = O.is_exact_case(info, r)
    band = int(filt == NEAREST and (near_boundary(u) or near_boundary(v)))
    POINTS.append(dict(name="%s/%s/%s%s/%s" % (imname, FILT[filt], MODE[ex], MODE[ey], tag), image=INDEX[imname],
                       win=win, ex=ex, ey=ey, filt=filt, fsp=fsp, layer=layer, u=u, v=v,
                       expected=[float(c) for c in r], tol=tolerance(exact, info, r), band=band))


def add_area(imname, win, ex, ey, fsp, layer, origin, a, b, tag):
    im = IMAGES[INDEX[imname]]
    if im.alpha_only:
        fsp = ZERO
    info = {}
    r = O.area(im, win, ex, ey, fsp, layer, origin, a, b, info)
    exact = O.is_exact_case(info, r)
    AREAS.append(dict(name="%s/area/%s%s/%s" % (imname, MODE[ex], MODE[ey], tag), image=INDEX[imname], win=win,
                      ex=ex, ey=ey, fsp=fsp, layer=layer, origin=origin, a=a, b=b,
                      expected=[float(c) for c in r], tol=tolerance(exact, info, r)))


def build_points():
    # decode: every texel's centre, NEAREST, the layer being the image's own space (no conversion)
    for im in IMAGES:
        if im.name == "wide70":
            texels = [(x, y) for y in range(3) for x in (0, 1, 62, 63, 64, 65, 68, 69)]
        elif im.name in ("step", "oor", "const8", "edge", "one1"):
            continue
        else:
            texels = [(x, y) for y in range(im.h) for x in range(im.w)]
        for x, y in texels:
            add_pt(im.name, FULL, PAD, PAD, NEAREST, ZERO, layer_of(im), x + 0.5, y + 0.5, "decode%d,%d" % (x, y))

    # the three filters at centres, between texels, at the edges, per image, in its own space
    modes3 = [(PAD, PAD), (REPEAT, REFLECT), (REFLECT, NONE)]
    for name in ("g8", "rgba8s", "rgba8p", "rgb12", "rgba16s", "gf", "rgbaf_p", "rgbaf_s", "a8"):
        im = IMAGES[INDEX[name]]
        w, h = im.w, im.h
        coords = [(0.5, 0.5), (1.5, 1.5), (w - 0.5, h - 0.5), (1.0, 1.0), (w / 2, h / 2), (1.25, 0.75), (2.625, 1.375),
                  (w - 0.125, 0.0625), (0.0, 0.0), (-0.375, h + 0.4375)]
        for k, (ex, ey) in enumerate(modes3 if name in ("rgba8s", "rgbaf_s", "a8") else modes3[:1]):
            for f in (NEAREST, LINEAR, CUBIC):
                for i, (u, v) in enumerate(coords):
                    add_pt(name, FULL, ex, ey, f, ZERO, layer_of(im), u, v, "p%d.%d" % (k, i))

    # NEAREST near texel boundaries (the discontinuity band, IP12), inside and outside it
    e = 2.0 ** -16
    for i, (u, v) in enumerate([(2.0, 1.5), (2.0 + e / 2, 1.5), (2.0 - e / 2, 1.5), (2.0 + 2 * e, 1.5), (2.0 - 2 * e, 1.5),
                               (1.5, 2.0 + e / 4), (1.5, 2.0 - e / 4), (1.5, 2.0 + 4 * e), (0.0, 0.0), (4.0 - e / 8, 3.9999),
                               (2.0 + 2.0 ** -40, 1.0 - 2.0 ** -40), (3.25, 2.75)]):
        add_pt("rgba8s", FULL, PAD, PAD, NEAREST, ZERO, SRGB, u, v, "band%d" % i)
    for i, (u, v) in enumerate([(5.0 + e / 2, 1.5), (5.0 - e / 2, 1.5), (-e / 2, 1.5), (1.5, 4.0 + e / 2), (1.5, e / 2), (6.0, 2.5)]):
        add_pt("edge", FULL, REPEAT, REFLECT, NEAREST, ZERO, LIN, u, v, "seam%d" % i)
        add_pt("edge", (1, 1, 4, 3), NONE, PAD, NEAREST, ZERO, LIN, u, v, "seamw%d" % i)

    # edge modes: every combination per axis, with a partial window, a one-texel window, and the whole image
    pts_full = [(-3.25, -2.5), (0.5, 0.5), (2.375, 1.625), (4.75, 3.5), (6.125, 5.25), (-40.5, 30.75), (1003.375, -501.625)]
    pts_sub = [(0.25, 0.75), (1.5, 1.5), (2.5, 2.25), (3.875, 2.9375), (4.5, 3.5), (-6.625, 9.375), (517.125, -203.5)]
    pts_one = [(2.5, 1.5), (1.75, 0.25), (3.5, 2.5), (-5.5, 7.25)]
    mixed = [(PAD, PAD), (REPEAT, REPEAT), (REFLECT, REFLECT), (NONE, NONE), (PAD, REPEAT), (REPEAT, REFLECT),
             (REFLECT, NONE), (NONE, PAD)]
    for f in (NEAREST, LINEAR, CUBIC):
        for ex, ey in mixed:
            for i, (u, v) in enumerate([pts_full[i] for i in (0, 2, 3, 5, 6)]):
                add_pt("edge", FULL, ex, ey, f, ZERO, LIN, u, v, "full%d" % i)
        for ex in range(4):
            for ey in range(4):
                for i, (u, v) in enumerate([pts_sub[0], pts_sub[2], pts_sub[3], pts_sub[5]]):
                    add_pt("edge", (1, 1, 4, 3), ex, ey, f, ZERO, LIN, u, v, "win%d" % i)
                for i, (u, v) in enumerate(pts_one[:2]):
                    add_pt("edge", (2, 1, 3, 2), ex, ey, f, ZERO, LIN, u, v, "one%d" % i)
    # a window on a colour image in sRGB values (the decode of the taps), and a 1x1 image
    for f in (NEAREST, LINEAR, CUBIC):
        for ex, ey in ((REFLECT, REPEAT), (NONE, NONE)):
            for i, (u, v) in enumerate([(0.25, 0.75), (-1.5, 2.5), (3.5, -2.25), (11.5, 7.75)]):
                add_pt("rgba8s", (1, 1, 3, 4), ex, ey, f, ZERO, SRGB, u, v, "w%d" % i)
                add_pt("one1", FULL, ex, ey, f, ZERO, LIN, u, v, "one%d" % i)

    # CUBIC with a step edge and out-of-range floats (the clamp)
    for i in range(17):
        add_pt("step", FULL, PAD, PAD, CUBIC, ZERO, LIN, 3.0 + i / 8, 1.5, "x%d" % i)
        add_pt("step", FULL, REPEAT, PAD, CUBIC, ZERO, LIN, 7.0 + i / 8, 2.5, "seam%d" % i)
    for i in range(0, 9):
        add_pt("step", FULL, PAD, PAD, CUBIC, ZERO, LIN, 3.9375, 1.0 + i / 4, "y%d" % i)
        add_pt("step", FULL, PAD, PAD, LINEAR, ZERO, LIN, 4.0 + (i - 4) / 16, 2.0 + (i - 4) / 8, "lin%d" % i)
    for i in range(25):
        add_pt("oor", FULL, PAD, PAD, CUBIC, ZERO, LIN, i / 4, 1.5, "u%d" % i)
        add_pt("oor", FULL, REFLECT, REPEAT, CUBIC, ZERO, LIN, i / 4 - 1.0, 0.5 + i / 8, "r%d" % i)
    for i in range(6):
        add_pt("oor", FULL, PAD, PAD, LINEAR, ZERO, LIN, 0.5 + i * 0.75, 1.5, "lin%d" % i)

    # filter space and layer space (IP5): default, linear light, other gamuts; a P3 image filtered in sRGB
    combos = [
        ("rgba8s", LIN, SRGB), ("rgba8s", LIN, P3), ("rgba8s", ZERO, LIN), ("rgba8s", P3, BT2020), ("rgba8s", GAMMA24, SRGB),
        ("rgba8p", LIN, SRGB), ("rgba8p", ZERO, LIN), ("rgba8p", P3LIN, SRGB),
        ("rgba8s_p3", SRGB, SRGB), ("rgba8s_p3", LIN, SRGB), ("rgba8s_p3", ZERO, SRGB), ("rgba8s_p3", SRGB, P3),
        ("rgba16p", SRGB, SRGB), ("rgba16s", LIN, SRGB), ("rgba12s", BT2020G, LIN), ("rgba12p", SRGB, SRGB),
        ("rgb16", LIN, SRGB), ("rgb16", ZERO, SRGB), ("rgb12", SRGB, LIN), ("g12", SRGB, LIN), ("g8", LIN, SRGB),
        ("rgbf", LIN, SRGB), ("rgbf", ZERO, P3), ("rgbaf_pb", LIN, SRGB), ("rgbaf_pb", BT2020G, SRGB),
        ("rgbaf_sb", LIN, SRGB), ("rgbaf_sb", ZERO, SRGB), ("rgbaf_s", SRGB, LIN), ("rgbaf_p", SRGB, LIN),
        ("rgbaf_p", ZERO, SRGB), ("rgb8z", ZERO, SRGB), ("rgb8z", LIN, SRGB), ("rgb8z", SRGB, SRGB), ("rgb8z", SRGB, LIN),
        ("rgbafz", ZERO, SRGB), ("rgbafz", LIN, SRGB), ("rgbafz", ZERO, ZERO), ("rgb8z", ZERO, ZERO),
    ]
    for name, fsp, layer in combos:
        im = IMAGES[INDEX[name]]
        coords = [(1.5, 1.5), (1.25, 0.75), (im.w - 0.25, im.h - 0.5)]
        for f in (NEAREST, LINEAR, CUBIC):
            for i, (u, v) in enumerate(coords):
                add_pt(name, FULL, PAD if i % 2 == 0 else REPEAT, PAD, f, fsp, layer, u, v, "fs%d" % i)

    # alpha-only (IP9): the filtered alpha
    for name in ("a8", "a12", "a16", "af"):
        im = IMAGES[INDEX[name]]
        for ex, ey in ((PAD, PAD), (NONE, NONE), (REPEAT, REFLECT)):
            for f in (NEAREST, LINEAR, CUBIC):
                for i, (u, v) in enumerate([(1.5, 1.5), (0.75, 1.25), (im.w + 0.3125, -0.6875)]):
                    add_pt(name, FULL, ex, ey, f, ZERO, SRGB, u, v, "al%d" % i)

    # an image wider than a tile
    wins = [FULL, (63, 1, 65, 2)]
    for wi, win in enumerate(wins):
        for ex, ey in ((PAD, PAD), (REPEAT, REFLECT)):
            for f in (NEAREST, LINEAR, CUBIC):
                for i, (u, v) in enumerate([(63.5, 1.5), (64.5, 0.75), (63.9990234375, 1.25), (64.25, 2.5), (69.75, 1.5),
                                           (70.5, 0.5), (62.0, 2.0), (-1.5, 1.5)]):
                    add_pt("wide70", win, ex, ey, f, ZERO, SRGB, u, v, "t%d.%d" % (wi, i))


def build_areas():
    # a constant image is the constant; a unit square at an integer position is the texel
    for ex, ey in ((PAD, PAD), (REPEAT, REFLECT), (REFLECT, REPEAT)):
        for i, (o, a, b) in enumerate([((0.25, 0.5), (1.5, 0.0), (0.0, 2.0)), ((-3.75, 1.5), (7.25, 0.5), (-1.0, 3.0)),
                                       ((40.0, -77 / 3), (301 / 7, 0.0), (0.0, 83 / 11)), ((1.0, 1.0), (1.0, 1.0), (-1.0, 1.0))]):
            add_area("const8", FULL, ex, ey, ZERO, LIN, o, a, b, "const%d" % i)
    add_area("const8", FULL, NONE, NONE, ZERO, LIN, (0.5, 0.25), (2.0, 0.0), (0.0, 2.5), "const-none")
    for name in ("rgba8s", "rgba8p", "g8", "rgbaf_p", "a8"):
        im = IMAGES[INDEX[name]]
        for i, (x, y, ex, ey) in enumerate([(0, 0, PAD, PAD), (im.w - 1, im.h - 1, PAD, PAD), (1, 2 % im.h, NONE, NONE),
                                            (-2, 1, PAD, PAD), (im.w + 1, -1, REPEAT, REPEAT), (im.w + 3, 2, REFLECT, REFLECT),
                                            (-1, -1, NONE, PAD)]):
            add_area(name, FULL, ex, ey, ZERO, layer_of(IMAGES[INDEX[name]]) if name != "a8" else SRGB,
                     (float(x), float(y)), (1.0, 0.0), (0.0, 1.0), "unit%d" % i)

    # fractional positions, window edges, PAD smear, NONE
    for name in ("rgba8s", "g8", "rgbaf_s"):
        lay = layer_of(IMAGES[INDEX[name]])
        for i, (o, a, b) in enumerate([((0.25, 0.5), (2.0, 0.0), (0.0, 1.5)), ((1.5, 1.25), (0.0, 1.75), (-2.25, 0.0)),
                                       ((0.5, 0.5), (0.5, 0.0), (0.0, 0.5)), ((0.125, 0.0625), (3.75, 0.0), (0.0, 2.9375)),
                                       ((-7.5, -3.0), (3.0, 0.0), (0.0, 2.25)), ((-2.0, -2.0), (8.0, 0.0), (0.0, 8.0)),
                                       ((2.5, 0.5), (4.0, 0.0), (0.0, 2.0)), ((3.0, 2.0), (-4.0, 0.0), (0.0, -3.5))]):
            for ex, ey in ((PAD, PAD), (NONE, NONE), (REPEAT, REFLECT)):
                add_area(name, FULL, ex, ey, ZERO, lay, o, a, b, "ax%d" % i)
    for ex, ey in ((PAD, PAD), (REPEAT, REPEAT), (REFLECT, REFLECT), (NONE, NONE), (NONE, PAD), (REFLECT, REPEAT), (PAD, NONE)):
        for win in ((1, 1, 4, 3), (2, 1, 3, 2)):
            for i, (o, a, b) in enumerate([((0.5, 0.25), (4.0, 0.0), (0.0, 3.5)), ((1.0, 1.0), (3.0, 0.0), (0.0, 2.0)),
                                           ((-2.5, -1.75), (9.0, 0.0), (0.0, 6.5))]):
                add_area("edge", win, ex, ey, ZERO, LIN, o, a, b, "win%d,%d.%d" % (win[0], win[2], i))

    # many periods: axis-aligned (whole periods summed) and rotated (cell clipping)
    for ex, ey in ((REPEAT, REPEAT), (REFLECT, REFLECT), (REPEAT, PAD), (REFLECT, NONE), (NONE, REPEAT)):
        for win in (FULL, (1, 1, 4, 3)):
            add_area("edge", win, ex, ey, ZERO, LIN, (-37.25, 11.5), (1003.5, 0.0), (0.0, 517.25), "many")
            add_area("edge", win, ex, ey, ZERO, LIN, (0.0, 1.0), (0.0, -2500.0), (2001.75, 0.0), "manyT")
    add_area("g8", FULL, REPEAT, REFLECT, ZERO, SRGB, (-100.5, 17.25), (4001.0, 0.0), (0.0, 1203.5), "many")
    add_area("rgba8p", FULL, REFLECT, REPEAT, ZERO, SRGB, (3.0, -9.0), (800.0, 0.0), (0.0, 360.0), "many")

    c30, s30 = math.cos(math.radians(30)), math.sin(math.radians(30))
    rot = [((1.5, 1.0), (2 * c30, 2 * s30), (-2 * s30, 2 * c30)), ((0.0, 0.0), (3 * c30, 3 * s30), (-3 * s30, 3 * c30)),
           ((1.25, 0.5), (3.5, 0.75), (1.25, 2.5)), ((2.0, 2.0), (1.0, 1.0), (-1.0, 1.0)),
           ((1.0, 0.5), (0.25, 0.125), (-0.0625, 0.25)), ((-1.5, 3.5), (-2.0, 1.0), (1.0, 2.0))]
    for name in ("rgba8p", "edge", "g8"):
        lay = layer_of(IMAGES[INDEX[name]])
        for i, (o, a, b) in enumerate(rot):
            for ex, ey in ((PAD, PAD), (REPEAT, REPEAT), (REFLECT, REFLECT), (NONE, NONE), (REFLECT, PAD)):
                add_area(name, FULL, ex, ey, ZERO, lay, o, a, b, "rot%d" % i)
    for ex, ey in ((REPEAT, REPEAT), (REFLECT, REFLECT), (REPEAT, REFLECT)):
        for win in (FULL, (1, 1, 4, 3)):
            add_area("edge", win, ex, ey, ZERO, LIN, (-20.5, 3.25), (40.0, 12.0), (-9.0, 33.0), "rotmany")
            add_area("rgba8s", win, ex, ey, ZERO, SRGB, (-20.5, 3.25), (31.5, -7.0), (11.0, 28.5), "rotmany")

    # filter space and layer space
    fs = [("rgba8s", LIN, SRGB), ("rgba8s", ZERO, LIN), ("rgba8s_p3", SRGB, SRGB), ("rgba8p", LIN, SRGB), ("rgbf", LIN, SRGB),
          ("rgbaf_pb", ZERO, P3), ("rgba16p", SRGB, SRGB), ("rgb8z", LIN, SRGB), ("rgb8z", ZERO, SRGB), ("rgbafz", ZERO, ZERO)]
    for name, fsp, layer in fs:
        for i, (o, a, b) in enumerate([((0.25, 0.5), (2.5, 0.0), (0.0, 1.75)), ((1.5, 0.5), (2 * c30, 2 * s30), (-s30, c30)),
                                       ((-1.5, -0.5), (5.0, 0.0), (0.0, 4.0))]):
            add_area(name, FULL, REPEAT if i == 0 else PAD, PAD, fsp, layer, o, a, b, "fs%d" % i)

    # alpha-only, and an image wider than a tile
    for name in ("a8", "a12", "af"):
        for ex, ey in ((PAD, PAD), (NONE, NONE), (REPEAT, REFLECT)):
            add_area(name, FULL, ex, ey, ZERO, SRGB, (0.75, 0.25), (2.5, 0.0), (0.0, 2.75), "al0")
            add_area(name, FULL, ex, ey, ZERO, SRGB, (0.5, 0.5), (2.0, 1.0), (-1.0, 2.0), "al1")
    for win in (FULL, (60, 0, 70, 3)):
        for ex, ey in ((PAD, PAD), (REPEAT, REFLECT), (NONE, REPEAT)):
            add_area("wide70", win, ex, ey, ZERO, SRGB, (60.5, 0.25), (8.0, 0.0), (0.0, 2.5), "t0")
            add_area("wide70", win, ex, ey, ZERO, SRGB, (62.0, 0.5), (4.5, 0.75), (-1.0, 2.0), "t1")
            add_area("wide70", win, ex, ey, ZERO, SRGB, (-90.5, -4.0), (300.0, 0.0), (0.0, 20.0), "t2")


def ulp_up(x):
    return struct.unpack("<d", struct.pack("<q", struct.unpack("<q", struct.pack("<d", x))[0] + 1))[0]


def ulp_down(x):
    return struct.unpack("<d", struct.pack("<q", struct.unpack("<q", struct.pack("<d", x))[0] - 1))[0]


def build_stretches():
    c30, s30 = math.cos(math.radians(30)), math.sin(math.radians(30))
    one = 1.0
    ms = [
        ("scale 7", (7.0, 0.0, 0.0, 7.0)), ("scale 0.3", (0.3, 0.0, 0.0, 0.3)), ("scale exactly 1", (1.0, 0.0, 0.0, 1.0)),
        ("scale 7 by 0.3", (7.0, 0.0, 0.0, 0.3)), ("scale 0.3 by 7", (0.3, 0.0, 0.0, 7.0)),
        ("turn 30 degrees at scale 2", (2 * c30, -2 * s30, 2 * s30, 2 * c30)),
        ("turn 30 degrees at scale 0.9", (0.9 * c30, -0.9 * s30, 0.9 * s30, 0.9 * c30)),
        ("turn 30 degrees at scale 1", (c30, -s30, s30, c30)),
        ("skew 1 over 2", (1.0, 0.5, 0.0, 1.0)), ("skew 2 over 3", (1.5, 1.0, 0.0, 1.5)),
        ("mirror in x", (-1.0, 0.0, 0.0, 1.0)), ("mirror in x at scale 3", (-3.0, 0.0, 0.0, 3.0)),
        ("mirror with turn", (-2 * c30, 2 * s30, 2 * s30, 2 * c30)),
        ("determinant 1, sigma_min 0.5", (2.0, 0.0, 0.0, 0.5)), ("determinant 1, sigma_min 0.25", (4.0, 0.0, 0.0, 0.25)),
        ("determinant 1 shear", (1.0, 3.0, 0.0, 1.0)),
        ("sigma_min exactly 1, determinant 4", (2.0, 3.0, 0.0, 2.0)),
        ("sigma_min one ulp over 1", (2.0, ulp_down(3.0), 0.0, 2.0)), ("sigma_min one ulp under 1", (2.0, ulp_up(3.0), 0.0, 2.0)),
        ("scale one ulp over 1", (ulp_up(one), 0.0, 0.0, ulp_up(one))), ("scale one ulp under 1", (ulp_down(one), 0.0, 0.0, ulp_down(one))),
        ("x one ulp over 1, y exactly 1", (ulp_up(one), 0.0, 0.0, one)), ("x 2, y exactly 1", (2.0, 0.0, 0.0, 1.0)),
        ("x 2, y one ulp over 1", (2.0, 0.0, 0.0, ulp_up(one))),
        ("turn 90 degrees at scale 1.0000001", (0.0, -1.0000001, 1.0000001, 0.0)),
        ("turn 90 degrees at scale 1", (0.0, -1.0, 1.0, 0.0)),
        ("swap axes at scale 2", (0.0, 2.0, 2.0, 0.0)), ("swap axes at scale 0.5", (0.0, 0.5, 0.5, 0.0)),
        ("singular", (1.0, 2.0, 2.0, 4.0)), ("zero", (0.0, 0.0, 0.0, 0.0)), ("rank one, large", (8.0, 8.0, 8.0, 8.0)),
        ("tiny", (2.0 ** -600, 0.0, 0.0, 2.0 ** -600)), ("huge", (2.0 ** 60, 0.0, 0.0, 2.0 ** 60)),
        ("huge by tiny", (2.0 ** 60, 0.0, 0.0, 2.0 ** -60)),
        ("negative zero entries, scale 2", (2.0, -0.0, -0.0, 2.0)),
        ("shear with scale 1.5", (1.5, 0.75, 0.0, 1.5)), ("rotate 45 degrees at scale 1.4142", (1.0001, -1.0001, 1.0001, 1.0001)),
        ("rotate 45 degrees at scale 1.4142 under", (0.9999, -0.9999, 0.9999, 0.9999)),
    ]
    for name, m in ms:
        STRETCHES.append(dict(name=name, m=m, expected=O.stretches_every_direction(*m)))


# --- the header ------------------------------------------------------------------------------------------------------------
def sp(s):
    return "{%d, %d, %s}" % (s[0], s[1], hexd(s[2]))


def render():
    _state[0] = 0x2545F491
    build_points()
    build_areas()
    build_stretches()
    o = []
    w = o.append
    w("// Generated by tools/gen-image-vectors.py from tools/image-oracle.py (exact rationals; transfers and gamut\n")
    w("// matrices in 110-digit decimal, each result within 1e-85 * max(1, |x|)).  Do not edit; run the generator.\n")
    w("// Independent of the library's code.  %d images, %d points, %d areas, %d stretches.\n" % (
        len(IMAGES), len(POINTS), len(AREAS), len(STRETCHES)))
    w("#ifndef GHOTI_IO_GSHSCENE_TEST_IMAGE_VECTORS_H\n#define GHOTI_IO_GSHSCENE_TEST_IMAGE_VECTORS_H\n\n#include <cstdint>\n\n")
    w("namespace imagevec {\n\n")
    w("// A color space as GSHSCENE_Color_Space spells it.  GCOL numbering: primaries UNKNOWN 0, SRGB 1, ADOBE_RGB 2,\n"
      "// DISPLAY_P3 3, BT2020 4; transfer UNKNOWN 0, LINEAR 1, SRGB 2, GAMMA 3.  The zero Space {0, 0, 0.0} is the target's\n"
      "// space; in these vectors the target's space is the case's `layer` (a zero `layer` appears only where every\n"
      "// space of the case is zero).\n")
    w("struct Space {\n  unsigned primaries, transfer;\n  double gamma;\n};\n\n")
    w("// An image as image.h describes it.  layout is the number of samples a pixel has: 1 gray, 3 RGB, 4 RGBA.  type 0\n"
      "// unorm, 1 float.  bits 8, 12, 16 or 32.  alpha_form 0 none, 1 straight, 2 premultiplied.  samples: width * height *\n"
      "// layout values, row-major, channels in storage order; a float is its IEEE binary32 bit pattern.\n")
    w("struct Image {\n  const char * name;\n  unsigned width, height, layout, type, bits, alpha_form, alpha_only;\n"
      "  Space space;\n  const std::uint32_t * samples;\n};\n\n")
    w("// A point sample (NEAREST 0, LINEAR 1, CUBIC 2) at image-space point (u, v), y down, texel (i, j) covering\n"
      "// [i, i + 1) x [j, j + 1).  window: x0, y0, x1, y1 in texels, all zero for the whole image.  extend: PAD 0, REPEAT 1,\n"
      "// REFLECT 2, NONE 3.  filter_space: zero for the image's own space.  expected: the premultiplied value in `layer`\n"
      "// without the paint's alpha multiplier; for an alpha-only image expected[3] is the filtered alpha and\n"
      "// expected[0..2] are 0.  A sink matches within tolerance * max(1, max over k of |expected[k]|).  Where not exact, tolerance is max(1e-12, 2^-47 C / max(1, max |expected|)), C = sum |w_i| M_i over the taps (w_i the weight, 1 for NEAREST, M_i the largest absolute component the tap passes through: premultiplied in its own space, straight, converted, premultiplied in F; plus the layer step's own largest magnitude): a weighted sum of n terms errs by at most n 2^-53 C in any order, which huge cancelling taps (saturated floats, gamut conversion) make large.  in_band: a NEAREST point within\n"
      "// 2^-16 texel of a texel boundary (edge-mode seams included): a sink may take either neighbour.\n")
    w("struct Point {\n  const char * name;\n  int image;\n  int window[4];\n  unsigned extend_x, extend_y;\n  unsigned filter;\n"
      "  Space filter_space;\n  Space layer;\n  double u, v;\n  double expected[4];\n  double tolerance;\n  int in_band;\n};\n\n")
    w("// An AREA sample: the exact mean of the extended image over the parallelogram origin + s edge_a + t edge_b,\n"
      "// s and t in [0, 1] (image space).  The other fields as Point.\n")
    w("struct Area {\n  const char * name;\n  int image;\n  int window[4];\n  unsigned extend_x, extend_y;\n"
      "  Space filter_space;\n  Space layer;\n  double origin[2], edge_a[2], edge_b[2];\n  double expected[4];\n"
      "  double tolerance;\n};\n\n")
    w("// IP8: m = {m00, m01, m10, m11}, the linear part of the composed map.  expected is 1 when every direction is\n"
      "// stretched (the smallest singular value exceeds 1), decided exactly on the doubles.\n")
    w("struct Stretch {\n  const char * name;\n  double m[4];\n  int expected;\n};\n\n")

    for k, im in enumerate(IMAGES):
        w("static const std::uint32_t kSamples%d[] = {" % k)
        w(", ".join(str(v) for v in im.samples))
        w("};\n")
    w("\nstatic const Image kImages[] = {\n")
    for k, im in enumerate(IMAGES):
        w("    {\"%s\", %d, %d, %d, %d, %d, %d, %d, %s, kSamples%d},\n" % (
            im.name, im.w, im.h, im.layout, im.typ, im.bits, im.alpha_form, int(im.alpha_only), sp(im.space), k))
    w("};\n\nstatic const unsigned kImageCount = %d;\n\n" % len(IMAGES))

    w("static const Point kPoints[] = {\n")
    for p in POINTS:
        w("    {\"%s\", %d, {%s}, %d, %d, %d, %s, %s, %s, %s, {%s}, %s, %d},\n" % (
            p["name"], p["image"], ", ".join(str(v) for v in p["win"]), p["ex"], p["ey"], p["filt"], sp(p["fsp"]),
            sp(p["layer"]), hexd(p["u"]), hexd(p["v"]), ", ".join(hexd(v) for v in p["expected"]),
            hexd(p["tol"]), p["band"]))
    w("};\n\nstatic const unsigned kPointCount = %d;\n\n" % len(POINTS))

    w("static const Area kAreas[] = {\n")
    for a in AREAS:
        w("    {\"%s\", %d, {%s}, %d, %d, %s, %s, {%s}, {%s}, {%s}, {%s}, %s},\n" % (
            a["name"], a["image"], ", ".join(str(v) for v in a["win"]), a["ex"], a["ey"], sp(a["fsp"]), sp(a["layer"]),
            ", ".join(hexd(v) for v in a["origin"]), ", ".join(hexd(v) for v in a["a"]), ", ".join(hexd(v) for v in a["b"]),
            ", ".join(hexd(v) for v in a["expected"]), hexd(a["tol"])))
    w("};\n\nstatic const unsigned kAreaCount = %d;\n\n" % len(AREAS))

    w("static const Stretch kStretches[] = {\n")
    for s in STRETCHES:
        w("    {\"%s\", {%s}, %d},\n" % (s["name"], ", ".join(hexd(v) for v in s["m"]), s["expected"]))
    w("};\n\nstatic const unsigned kStretchCount = %d;\n\n" % len(STRETCHES))
    w("}  // namespace imagevec\n\n#endif\n")
    return "".join(o)


if __name__ == "__main__":
    text = render()
    if len(sys.argv) > 1 and sys.argv[1] == "--check":
        path = os.path.join(HERE, "..", "tests", "image_vectors.h")
        try:
            with open(path) as f:
                same = f.read() == text
        except OSError:
            same = False
        if not same:
            sys.exit("gen-image-vectors: tests/image_vectors.h is not what the generator writes")
        print("gen-image-vectors: tests/image_vectors.h is current")
    else:
        sys.stdout.write(text)
