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
"""Write tests/composite_vectors.h: the known answers of the compositing definition (N1 to N10, B1 to B14).

Every case is a closed list in paint order (fills, clips and groups), the coverage
each fill and clip push is given, the foreground, the space and the initial value of
the root layer, and the exact answer the reference (tools/composite-oracle.py, exact
rationals) computes.  A case whose values are dyadic and that converts between no
spaces has an answer a double evaluator can match bit for bit ("exact"); the others
are compared within a tolerance because they evaluate a transfer function or a matrix.
tests/test_composite.cpp builds each list through the library's API and evaluates it
with tests/ref_composite.h; story 13's shape-cpu is measured against the same table.

The blend modes (color-paint story P3) add two things.  A table of the mixing function
alone, `kMix`: B(Cb, Cs) after B9's saturation for straight inputs, every branch of B7 and
B8 on both sides of each threshold and at it, out-of-range inputs, B9's poles and the
corners at +-2^64.  And cases of blending groups: every mode in four blend spaces, at
alphas 0, 1/2, 1 and a non-dyadic one, under coverage and opacity, nested, at the root
over a non-opaque target, out of range and at B9's saturation.  A blend vector is exact
(STOP-2, Corey 2026-10-21) only where every step of the double evaluation is exact: the
tracer below walks S11's order of operations (shape-cpu's render.h, the same order
tests/ref_composite.h uses) on the exact values, carrying a bound on each double's distance
from them, and a sample is exact when every bound stays 0; a non-separable mode never is
(Lum's 0.3, 0.59 and 0.11 are not doubles), nor is a root that is not of a square.  A sample
whose bound passes an eighth of the tolerance (cancellation at large magnitudes: screen's
2^64 + 1 - 2^64), or whose bound reaches a discontinuity of B10 (dodge's and burn's poles,
ClipColor's and SetSat's equal components), is refused: the grid's are left out and counted
in the header, and a hand-written case that is refused stops the generator.

tests/group_vectors.h (--groups) holds the general group of PDF's transparency model (T1 to T7):
scenes in the four combinations of isolated and knockout.  The generator requires the general
model to give every case of composite_vectors.h exactly (T6), so those are the default
combination's vectors bit for bit.

    python3 tools/gen-composite-vectors.py > tests/composite_vectors.h
    python3 tools/gen-composite-vectors.py --groups > tests/group_vectors.h
    python3 tools/gen-composite-vectors.py --masks > tests/mask_vectors.h
    python3 tools/gen-composite-vectors.py --check
    python3 tools/gen-composite-vectors.py --masks --check [--plant NAME]

tests/mask_vectors.h (--masks) holds the soft mask (MK1 to MK7): the mask's value from its picture through every mode,
coefficient set, backdrop and transfer (probe scenes whose answer is the value itself), the outside value, the transfer
edges, a masked group under all four isolated and knockout parents and the operators of every family with the value
acting on opacity and on shape, the order of clip, mask, opacity and blend, a mask of a mask; and two tables of known
answers for ``gshscene_mask_transfer`` and ``gshscene_mask_resolve``.  A `--plant` breaks one rule of the oracle's
mask on purpose (``composite-oracle.py --selftest --plant`` lists them) and the check must then fail.
"""
import importlib.util
import os
import sys
from fractions import Fraction as F

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("oracle", os.path.join(HERE, "composite-oracle.py"))
oracle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(oracle)

TARGET = oracle.TARGET
SRGB = oracle.SPACE_SRGB
LIN = oracle.SPACE_LINEAR_SRGB
P3 = oracle.SPACE_P3
LINP3 = oracle.SPACE_LINEAR_P3
P3G = oracle.SPACE_P3_GAMMA
ADOBE = oracle.SPACE_ADOBE_GAMMA
BT2020L = oracle.SPACE_LINEAR_BT2020

FILL, CLIP_PUSH, CLIP_POP, GROUP_PUSH, GROUP_POP = 1, 2, 3, 4, 5
FG = ((0.5, 0.25, 0.0, 0.5), TARGET)


def fill(rgba, cov=1.0, space=TARGET, paint=0):
    return dict(k=FILL, paint=paint, rgba=rgba, space=space, cov=cov)


def currentcolor(alpha, cov=1.0):
    return dict(k=FILL, paint=2, rgba=(0.0, 0.0, 0.0, alpha), space=TARGET, cov=cov)


def entry(index, alpha, cov=1.0):
    """A palette fill (N14): entry @p index of the case's palette, its alpha scaled by @p alpha."""
    return dict(k=FILL, paint=3, entry=index, rgba=(0.0, 0.0, 0.0, alpha), space=TARGET, cov=cov)


def group(opacity=1.0, op="src-over", blend=TARGET):
    return dict(k=GROUP_PUSH, opacity=opacity, blend=blend, composite=oracle.OP_INDEX[op], cov=0.0)


def pop():
    return dict(k=GROUP_POP, cov=0.0)


def clip(cov):
    return dict(k=CLIP_PUSH, cov=cov)


def unclip():
    return dict(k=CLIP_POP, cov=0.0)


BACKDROP = (0.25, 0.125, 0.5, 0.5)       # premultiplied: straight (0.5, 0.25, 1.0) at alpha 0.5
OPAQUE_BACKDROP = (0.0, 0.0, 1.0, 1.0)   # opaque blue
CASES = []


def case(name, items, target=SRGB, backdrop=BACKDROP, fg=FG, palette=None):
    CASES.append(dict(name=name, items=items, target=target, backdrop=backdrop, fg=fg, palette=palette))


SOURCE = (1.0, 0.5, 0.0, 0.75)  # straight; premultiplied (0.75, 0.375, 0, 0.75)

# Every operator of N4, opacity 1 and 1/2, over the half-transparent backdrop (the blend modes are below).
PORTER_DUFF = oracle.OPS[:13]
for op in PORTER_DUFF:
    for opacity in (1.0, 0.5):
        case("%s opacity %s" % (op, "1" if opacity == 1.0 else "1/2"), [group(opacity, op), fill(SOURCE), pop()])
# ... with the group weighed by a clip of 1/4 (the clip pushed outside the group), and a fill covering 1/2.
for op in ("src-over", "src", "clear", "dest-in", "src-in", "xor", "plus", "dest-over", "src-atop"):
    case("%s weighed by a clip of 1/4" % op, [clip(0.25), group(1.0, op), fill(SOURCE, 0.5), pop(), unclip()])
# A fill alone.
for cov in (0.0, 0.25, 1.0):
    case("a fill of coverage %s" % cov, [fill(SOURCE, cov)])
case("a fill of an opaque color is the color where it covers fully", [fill((0.25, 0.5, 0.75, 1.0), 1.0)])
case("a fill with alpha 0 changes nothing", [fill((1.0, 1.0, 1.0, 0.0), 1.0)])
case("a fill over an opaque backdrop", [fill(SOURCE, 0.5)], backdrop=OPAQUE_BACKDROP)
case("an empty target", [fill(SOURCE, 0.5)], backdrop=(0.0, 0.0, 0.0, 0.0))
# Out of gamut and negative components are not clamped.
case("components outside the gamut", [fill((2.0, -0.5, 0.25, 0.5), 1.0)])
# PLUS clamps every premultiplied channel at 1.
case("plus clamps", [group(1.0, "plus"), fill((1.0, 1.0, 0.5, 1.0)), pop()], backdrop=(0.5, 0.75, 0.5, 0.75))
# Nested groups: each composites onto the one outside it.
case("nested groups", [group(0.5, "src-over"), fill((1.0, 0.0, 0.0, 1.0), 1.0), group(0.5, "xor"),
                       fill((0.0, 1.0, 0.0, 1.0), 0.5), pop(), fill((0.0, 0.0, 1.0, 0.5), 0.25), pop()])
case("an unbounded operator in a nested group clears only the group's layer", [
    group(1.0, "src-over"), fill((1.0, 0.0, 0.0, 1.0), 1.0), group(1.0, "clear"), pop(), pop()])
case("a group of opacity 0", [group(0.0, "src-over"), fill(SOURCE), pop()])
case("a group of opacity 0 with an unbounded operator clears the backdrop", [group(0.0, "src"), fill(SOURCE), pop()])
# Clips: around fills each fill is weighed, around a group the group is weighed once, inside a group the clip stays inside.
case("a clip around two overlapping fills weighs each",
     [clip(0.5), fill((1.0, 0.0, 0.0, 1.0)), fill((0.0, 1.0, 0.0, 1.0)), unclip()], backdrop=(0.0, 0.0, 0.0, 0.0))
case("a clip around a group weighs it once",
     [clip(0.5), group(), fill((1.0, 0.0, 0.0, 1.0)), fill((0.0, 1.0, 0.0, 1.0)), pop(), unclip()], backdrop=(0.0, 0.0, 0.0, 0.0))
case("a clip outside a group is not taken into it",
     [clip(0.5), group(), fill((1.0, 0.0, 0.0, 1.0), 1.0), pop(), unclip()], backdrop=(0.0, 0.0, 0.0, 0.0))
case("a clip inside a group is that group's alone",
     [group(), clip(0.5), fill((1.0, 0.0, 0.0, 1.0)), unclip(), fill((0.0, 0.0, 1.0, 1.0), 0.25), pop()], backdrop=(0.0, 0.0, 0.0, 0.0))
case("two clips multiply", [clip(0.5), clip(0.5), fill(SOURCE), unclip(), unclip()])
case("a clip popped is no longer in force", [clip(0.5), fill(SOURCE), unclip(), fill((0.0, 0.0, 1.0, 1.0), 0.25)])
case("a clip of 0 around an unbounded group leaves the backdrop", [clip(0.0), group(1.0, "clear"), pop(), unclip()])
case("a clip of 1/2 around an unbounded group halves the backdrop", [clip(0.5), group(1.0, "clear"), pop(), unclip()])
# The foreground: the version's color with its alpha times the paint's.
case("currentColor", [currentcolor(0.5)])
case("currentColor with alpha 1 in a group", [group(0.5), currentcolor(1.0), pop()])
case("currentColor in the target's space is not converted", [currentcolor(1.0, 0.5)],
     fg=((0.5, 0.25, 0.0, 0.5), TARGET))
# Spaces: where a conversion is needed the answer evaluates a transfer function or a matrix.
case("a paint in another transfer", [fill((0.5, 0.5, 0.5, 1.0), 1.0, LIN)], backdrop=(0.0, 0.0, 0.0, 0.0))
case("a paint in linear light, half covered", [fill((0.25, 0.5, 1.0, 0.75), 0.5, LIN)])
case("a paint in Display P3", [fill((1.0, 0.0, 0.0, 1.0), 1.0, P3)], backdrop=(0.0, 0.0, 0.0, 0.0))
case("a paint in Display P3 with a gamma", [fill((0.25, 0.5, 0.75, 1.0), 1.0, P3G)], backdrop=(0.0, 0.0, 0.0, 0.0))
case("a paint in Adobe RGB", [fill((0.25, 0.5, 0.75, 0.5), 1.0, ADOBE)])
case("a paint in linear BT.2020", [fill((0.25, 0.5, 0.75, 1.0), 1.0, BT2020L)], backdrop=(0.0, 0.0, 0.0, 0.0))
case("pure red survives a linear group", [group(1.0, "src-over", LIN), fill((1.0, 0.0, 0.0, 1.0)), pop()],
     backdrop=(0.0, 0.0, 0.0, 0.0))
case("a mid gray in a linear group", [group(1.0, "src-over", LIN), fill((0.5, 0.5, 0.5, 1.0)), pop()],
     backdrop=(0.0, 0.0, 0.0, 0.0))
case("a half-transparent mid gray in a linear group", [group(1.0, "src-over", LIN), fill((0.5, 0.5, 0.5, 0.5)), pop()])
case("two fills blended in linear light", [group(1.0, "src-over", LIN), fill((1.0, 1.0, 1.0, 0.5)), fill((0.0, 0.0, 0.0, 0.5)), pop()],
     backdrop=(0.0, 0.0, 0.0, 0.0))
case("the same two fills blended in the target's space", [group(1.0, "src-over"), fill((1.0, 1.0, 1.0, 0.5)), fill((0.0, 0.0, 0.0, 0.5)), pop()],
     backdrop=(0.0, 0.0, 0.0, 0.0))
case("a group in P3 holding a paint in sRGB", [group(0.5, "src-over", LINP3), fill((0.25, 0.5, 0.75, 1.0)), pop()])
case("a group that inherits the enclosing group's space", [group(1.0, "src-over", LIN), group(1.0, "src-over"),
                                                           fill((0.5, 0.5, 0.5, 1.0)), pop(), pop()], backdrop=(0.0, 0.0, 0.0, 0.0))
case("a negative component in a gamma space", [fill((-0.25, 0.5, 0.75, 1.0), 1.0, P3G)], backdrop=(0.0, 0.0, 0.0, 0.0))
case("a negative component through a linear group", [group(1.0, "src-over", LIN), fill((-0.25, 0.5, 2.0, 1.0)), pop()],
     backdrop=(0.0, 0.0, 0.0, 0.0))
case("an inherited blend space blends in the enclosing group's space",
     [group(1.0, "src-over", LIN), group(1.0, "src-over"), fill((1.0, 1.0, 1.0, 0.5)), fill((0.0, 0.0, 0.0, 0.5)), pop(), pop()],
     backdrop=(0.0, 0.0, 0.0, 0.0))
case("an empty group in another space composites nothing", [group(1.0, "src-over", LIN), pop()])
case("a paint just above the sRGB decode threshold", [fill((0.040455, 0.04046, 0.5, 1.0), 1.0, SRGB)], target=LIN,
     backdrop=(0.0, 0.0, 0.0, 0.0))
case("a paint just above the sRGB encode threshold", [fill((0.00313085, 0.00313095, 0.5, 1.0), 1.0, LIN)], target=SRGB,
     backdrop=(0.0, 0.0, 0.0, 0.0))
case("foreground in another space", [currentcolor(1.0)], fg=((0.5, 0.25, 0.75, 1.0), LIN))
case("foreground in another space inside a linear group", [group(1.0, "src-over", LIN), currentcolor(0.5), pop()],
     fg=((0.5, 0.25, 0.75, 1.0), P3))
case("a gamma space target", [fill((0.5, 0.25, 0.75, 0.5), 1.0, SRGB)], target=P3G)
# The palette (N14, story 14c): an entry's color with its alpha scaled; a missing entry draws nothing.
PALETTE = [((1.0, 0.5, 0.0, 1.0), TARGET), ((0.0, 0.25, 1.0, 0.5), TARGET), ((0.5, 0.5, 0.5, 1.0), LIN),
           ((255 / 255.0, 128 / 255.0, 1 / 255.0, 51 / 255.0), SRGB)]
case("a palette entry", [entry(0, 1.0, 0.5)], palette=PALETTE)
case("a palette entry's alpha times the paint's", [entry(1, 0.5)], palette=PALETTE)
case("a palette entry in another space", [entry(2, 1.0)], palette=PALETTE, backdrop=(0.0, 0.0, 0.0, 0.0))
case("a palette entry from 8 bits, its space stated", [entry(3, 1.0)], palette=PALETTE, target=LIN)
case("palette entries in a group of an operator", [group(1.0, "src-in"), entry(0, 1.0), entry(1, 1.0, 0.25), pop()],
     palette=PALETTE)
case("a missing palette entry draws nothing", [entry(4, 1.0), entry(0, 0.5)], palette=PALETTE)
case("no palette: every palette fill draws nothing", [entry(0, 1.0), fill(SOURCE, 0.5)])
case("a linear target and an sRGB paint", [fill((0.5, 0.25, 0.75, 0.5), 1.0, SRGB)], target=LIN)
# N1 (Corey, 2026-10-21): a recorded component at the bound, and the saturation at each point that names it.  Only
# these cases reach it: every other answer above is the same with and without it.
K16 = 2.0 ** 16
G10 = (1, oracle.GAMMA, 10.0)    # sRGB primaries, gamma 10: decode raises to the 10th power
G01 = (1, oracle.GAMMA, 0.1)     # gamma 0.1: encode raises to the 10th power
case("a component at the bound in the target's space is not saturated", [fill((K16, -K16, 0.5, 1.0))],
     target=LIN, backdrop=(0.0, 0.0, 0.0, 0.0))
case("saturation: gamma 10 to gamma 0.1 at 2^16 (the decode and the encode)", [fill((K16, -K16, 0.5, 1.0), 1.0, G10)],
     target=G01, backdrop=(0.0, 0.0, 0.0, 0.0))
case("saturation: a gamma 0.1 group's encode, decoded at its pop", [
    group(1.0, "src-over", G01), fill((K16, -K16, 1.0, 1.0), 1.0, LIN), pop()], target=LIN, backdrop=(0.0, 0.0, 0.0, 0.0))
case("saturation: the decode of a gamma-10 paint, encoded into a gamma-2 target", [fill((K16, -K16, 0.5, 1.0), 1.0, G10)],
     target=(1, oracle.GAMMA, 2.0), backdrop=(0.0, 0.0, 0.0, 0.0))
case("saturation: the matrix from a gamma-10 BT.2020 paint into a gamma-10 sRGB target", [
    fill((K16, -K16, -K16, 1.0), 1.0, (4, oracle.GAMMA, 10.0))], target=G10, backdrop=(0.0, 0.0, 0.0, 0.0))
case("saturation: negative plus groups accumulate to the bound", [
    group(1.0, "src-over", LIN), fill((-K16, K16, 0.25, 1.0), 1.0, G10),
    group(1.0, "plus"), fill((-K16, K16, 0.25, 1.0), 1.0, G10), pop(),
    group(1.0, "plus"), fill((-K16, K16, 0.25, 1.0), 1.0, G10), pop(),
    group(1.0, "plus"), fill((-K16, -K16, 0.25, 1.0), 1.0, G10), pop(), pop()], target=LIN, backdrop=(0.0, 0.0, 0.0, 0.0))
case("saturation: the un-premultiply of a layer that plus under a clip left past the bound", [
    group(1.0, "src-over", G01), fill((-K16, K16, 0.5, 0.5), 1.0, LIN),
    clip(0.25), group(1.0, "plus"), fill((-K16, K16, 0.5, 1.0), 1.0, LIN), pop(), unclip(), pop()], target=LIN,
     backdrop=(0.0, 0.0, 0.0, 0.0))
case("saturation: nested groups in alternating spaces", [
    group(1.0, "src-over", G01), group(0.5, "src-over", G10), group(1.0, "src-over", G01), group(1.0, "src-over", LIN),
    fill((K16, -K16, 2.0, 1.0), 1.0, G10), pop(), pop(), pop(), pop()], target=SRGB, backdrop=(0.0, 0.0, 0.0, 0.0))
case("saturation: the foreground at the bound in gamma 10, in a gamma 0.1 target", [currentcolor(1.0)],
     fg=((K16, -K16, 0.0, 1.0), G10), target=G01, backdrop=(0.0, 0.0, 0.0, 0.0))
case("saturation: a palette entry at the bound in gamma 10, in a gamma 0.1 target", [entry(0, 0.5)],
     palette=[((-K16, K16, 0.25, 1.0), G10)], target=G01, backdrop=(0.0, 0.0, 0.0, 0.0))

# --- B1 to B14: blend modes (color-paint story P3) --------------------------------------------------------------------
MODES = oracle.BLEND_MODES
P2_48 = 2.0 ** -48
K64 = 2.0 ** 64


def straight_backdrop(rgb, alpha):
    """The root's initial value for a straight backdrop color at @p alpha, as the doubles a target would hold."""
    return tuple(float(F(v) * F(alpha)) for v in rgb) + (float(alpha),)


def blended(mode, cs, alpha=1.0, cov=1.0, opacity=1.0, space=TARGET, blend=TARGET):
    return [group(opacity, mode, blend), fill(tuple(cs) + (alpha,), cov, space), pop()]


GRID = [0.0, 0.25, 0.5, 0.75, 1.0]
CB_GRID = [(0.0, 0.25, 0.5), (0.75, 1.0, 0.0), (0.25, 0.5, 0.75), (1.0, 0.0, 0.25), (0.5, 0.75, 1.0)]
CS_GRID = [(0.0, 0.0, 0.0), (0.25, 0.25, 0.25), (0.5, 0.5, 0.5), (0.75, 0.75, 0.75), (1.0, 1.0, 1.0)]
for mode in MODES:
    # In range at alpha 1: each grid value of Cb against each value of Cs once (kMix has every pair of the grid).
    for cb, cs in zip(CB_GRID, CS_GRID):
        case("%s: Cb %s, Cs %s" % (mode, cb, cs), blended(mode, cs), backdrop=straight_backdrop(cb, 1.0))
    if mode in oracle.NON_SEPARABLE:
        for cb, cs in (((1.0, 0.0, 0.0), (0.0, 0.0, 1.0)), ((0.25, 0.5, 0.75), (0.75, 0.25, 0.5)), ((0.5, 0.5, 0.5), (1.0, 0.0, 0.5)),
                       ((1.0, 0.5, 0.5), (0.5, 0.5, 0.5)), ((0.0, 1.0, 1.0), (1.0, 1.0, 0.0)), ((0.75, 0.75, 0.25), (0.25, 0.75, 0.75))):
            case("%s: saturated, grey and tied: Cb %s, Cs %s" % (mode, cb, cs), blended(mode, cs), backdrop=straight_backdrop(cb, 1.0))
    # Alphas: 0, 1/2 and a non-dyadic one, on each side.
    cb, cs = (0.25, 0.75, 0.5), (0.75, 0.25, 1.0)
    for a_s, a_b in ((0.5, 1.0), (1.0, 0.5), (0.5, 0.5), (0.0, 1.0), (1.0, 0.0), (0.6, 1.0), (1.0, 0.6), (0.6, 0.3)):
        case("%s: alpha %s over alpha %s" % (mode, a_s, a_b), blended(mode, cs, a_s), backdrop=straight_backdrop(cb, a_b))
    # Coverage and opacity below 1 (B4), and a clip around the blending group.
    case("%s: coverage 1/2, opacity 1/2" % mode, blended(mode, cs, 0.75, 0.5, 0.5), backdrop=straight_backdrop(cb, 0.75))
    case("%s: under a clip of 1/4" % mode, [clip(0.25)] + blended(mode, cs, 1.0, 1.0) + [unclip()], backdrop=straight_backdrop(cb, 1.0))
    # The blend space is the enclosing layer's (B5): three more parents, the paint in sRGB converted into each.
    for target in (LIN, LINP3, P3G):
        case("%s: in the parent's space %s" % (mode, target), blended(mode, (0.75, 0.25, 0.5), 0.75, space=SRGB),
             target=target, backdrop=straight_backdrop((0.25, 0.5, 0.75), 0.5))
    case("%s: a blending group's own space is where its content composites, not where it blends" % mode,
         blended(mode, (0.75, 0.25, 0.5), 0.75, blend=LIN), backdrop=straight_backdrop((0.25, 0.5, 0.75), 1.0))
    case("%s: inside a linear group, over a fill in it" % mode,
         [group(1.0, "src-over", LIN), fill((0.25, 0.5, 0.75, 1.0))] + blended(mode, (0.75, 0.25, 0.5), 0.75) + [pop()],
         backdrop=straight_backdrop((0.5, 0.5, 0.5), 0.5))
    # Nested: a blending group inside a blending group, and an isolated blend against the root's.
    case("%s: nested in a multiply group" % mode,
         [group(1.0, "multiply"), fill((0.5, 0.75, 1.0, 1.0))] + blended(mode, (1.0, 0.25, 0.5), 0.5) + [pop()],
         backdrop=straight_backdrop((0.75, 0.5, 0.25), 1.0))
    case("%s: inside a source-over group, which isolates it from the target" % mode,
         [group(1.0, "src-over")] + blended(mode, (1.0, 0.25, 0.5), 1.0) + [pop()], backdrop=straight_backdrop((0.75, 0.5, 0.25), 1.0))
    # Out of range (B10): negative and above 1, on each side, and the recorded bound.
    case("%s: out of range" % mode, blended(mode, (-0.5, 1.5, 0.25), 1.0), backdrop=straight_backdrop((1.5, -0.25, 2.0), 1.0))
    case("%s: at the recorded bound" % mode, blended(mode, (65536.0, -65536.0, 0.5), 0.5), backdrop=straight_backdrop((-65536.0, 0.25, 65536.0), 0.5))
    case("%s: an empty group leaves the backdrop (B3)" % mode, [group(1.0, mode), pop()], backdrop=straight_backdrop((0.5, 0.25, 1.5), 0.75))
# B9: the poles and the saturation.
case("color-dodge: Cb = 2^16, Cs = 1 + 2^-48: the quotient is -2^64", blended("color-dodge", (1.0 + P2_48, 1.0, 1.0 - P2_48), 1.0),
     backdrop=(65536.0, 65536.0, 65536.0, 1.0))
case("color-dodge: Cs = 1 + 2^-52: the quotient's real value is -2^68, saturated", blended("color-dodge", (1.0 + 2.0 ** -52, 0.5, 2.0), 1.0),
     backdrop=(65536.0, -65536.0, 65536.0, 1.0))
case("color-burn: Cs = 2^-52 from 1 - Cb = -2^15: the quotient's real value is -2^67, saturated",
     blended("color-burn", (2.0 ** -52, -(2.0 ** -52), 0.5), 1.0), backdrop=(32769.0, 32769.0, -65536.0, 1.0))
case("color-dodge and burn at their branch points", [group(1.0, "color-dodge"), fill((1.0, 0.5, 1.0, 1.0)), pop(),
                                                     group(1.0, "color-burn"), fill((0.0, 0.5, 0.0, 1.0)), pop()],
     backdrop=(0.0, 0.5, 1.0, 1.0))
case("dodge's pole approached from above with Cb > 0, at a recorded alpha of 1/2",
     blended("color-dodge", (1.0 + 2.0 ** -20, 1.0 + 2.0 ** -10, 1.0 + 2.0 ** -40), 0.5), backdrop=(0.5, 0.25, 0.125, 0.5))
# B8 and B10: ClipColor with n < 0 and x > 1 together, the equal-component (0/0) cases, and SetSat's ties.
case("luminosity: ClipColor with n < 0 and x > 1 together", blended("luminosity", (0.5, 0.5, 0.5), 1.0), backdrop=(-1.0, 3.0, 0.5, 1.0))
case("color: ClipColor with n < 0 and x > 1 together", blended("color", (-2.0, 0.5, 4.0), 1.0), backdrop=(0.5, 0.25, 0.75, 1.0))
case("luminosity: every component equal to L below 0 (the 0/0 form)", blended("luminosity", (-0.5, -0.5, -0.5), 1.0),
     backdrop=(0.0, 0.0, 0.0, 1.0))
case("luminosity: every component equal to L above 1 (the 0/0 form)", blended("luminosity", (2.0, 2.0, 2.0), 1.0),
     backdrop=(0.25, 0.25, 0.25, 1.0))
case("hue: a grey source (Cmax = Cmin)", blended("hue", (0.5, 0.5, 0.5), 1.0), backdrop=(1.0, 0.25, 0.0, 1.0))
case("saturation: two tied components", blended("saturation", (1.0, 0.0, 0.5), 1.0), backdrop=(0.75, 0.75, 0.25, 1.0))
case("hue at the bound", blended("hue", (65536.0, -65536.0, 0.0), 1.0), backdrop=(-65536.0, 0.0, 65536.0, 1.0))
# Thresholds of the separable modes exactly, in a case: hard-light and soft-light at Cs = 1/2, soft-light's D at Cb = 1/4.
case("hard-light, soft-light and overlay at 1/2, and soft-light's D at Cb = 1/4", [
    group(1.0, "hard-light"), fill((0.5, 0.5, 0.5, 1.0)), pop(), group(1.0, "soft-light"), fill((0.75, 0.5, 0.75, 1.0)), pop(),
    group(1.0, "overlay"), fill((0.25, 0.75, 1.0, 1.0)), pop()], backdrop=(0.25, 0.5, 0.25, 1.0))
case("soft-light's root of a square (exact)", blended("soft-light", (0.75, 1.0, 0.875), 1.0), backdrop=(0.5625, 0.25, 0.390625, 1.0))
case("soft-light's root of a non-square (inexact)", blended("soft-light", (0.75, 1.0, 0.875), 1.0), backdrop=(0.5, 0.3, 0.7, 1.0))
case("a blend at the root over a non-opaque target, then another over it", blended("multiply", (0.5, 1.0, 0.25), 0.5) +
     blended("screen", (0.25, 0.5, 0.75), 0.75), backdrop=(0.125, 0.25, 0.375, 0.5))
case("a blend over an empty target is the source over it", blended("difference", (0.5, 1.0, 0.25), 0.75), backdrop=(0.0, 0.0, 0.0, 0.0))
case("the COLRv1 shape: an outer group of the backdrop's items, an inner one of the source's with the mode", [
    group(1.0, "src-over", LIN), fill((0.25, 0.5, 0.75, 1.0)), group(1.0, "multiply"), fill((1.0, 0.5, 0.25, 1.0)),
    fill((0.5, 0.5, 0.5, 0.5)), pop(), pop()], target=SRGB, backdrop=(0.0, 0.0, 0.0, 0.0))


# --- the mixing function alone (kMix) -----------------------------------------------------------------------------------
HALF_UP, HALF_DOWN = 0.5 + 2.0 ** -16, 0.5 - 2.0 ** -16
VALUES = GRID + [-0.5, 1.5, 65536.0, -65536.0, HALF_UP, HALF_DOWN, 0.25 + 2.0 ** -16, 0.25 - 2.0 ** -16,
                 1.0 + P2_48, 1.0 + 2.0 ** -52, 1.0 - 2.0 ** -16, 2.0 ** -20, -(2.0 ** -20)]
CORNERS = [K64, -K64, 0.0, 1.0]
TRIPLES = [(0.25, 0.5, 0.75), (1.0, 0.0, 0.0), (0.0, 1.0, 1.0), (0.5, 0.5, 0.5), (1.0, 1.0, 0.5), (0.5, 0.0, 0.0),
           (-0.5, 0.5, 1.5), (2.0, 2.0, 2.0), (-1.0, -1.0, -1.0), (65536.0, -65536.0, 0.0), (0.75, 0.75, 0.25),
           (3.0, -1.0, 0.5), (0.125, 0.875, 0.5), (K64, -K64, 0.0)]
# B10: triples a rounding off the ray C = L, where Lum(C) - n (or x - Lum(C)) evaluated literally in doubles is 0 and
# the literal step is 0 / 0 (shape-cpu's fuzzer found a NaN there).  The ray is a discontinuity of ClipColor, so the grid
# refuses such samples; these are kept apart (kNearRay), each mixed with itself in every non-separable mode, which brings
# it to ClipColor a rounding off the ray: the inputs are exact, and S11's evaluation from the differences C - n meets
# the exact answer within the tolerance.
NEAR_RAY = [(-0.75, -0.75, -0.75 + 2.0 ** -53), (-0.75 + 2.0 ** -53, -0.75, -0.75), (-3.0, -3.0, -3.0 + 2.0 ** -51),
            (1.75, 1.75, 1.75 + 2.0 ** -52), (-1e10, -1e10, -1e10 + 2.0 ** -19),
            (-0.18212577361196883, -0.18212577361196883, -0.1821257736119688)]
MIX = []


def pack(pairs):
    """Three (Cb, Cs) pairs a row, the last padded with its own first pair."""
    rows = []
    for i in range(0, len(pairs), 3):
        chunk = pairs[i:i + 3]
        while len(chunk) < 3:
            chunk.append(chunk[0])
        rows.append((tuple(p[0] for p in chunk), tuple(p[1] for p in chunk)))
    return rows


for mode in MODES:
    if mode in oracle.SEPARABLE:
        pairs = [(b, s_) for b in VALUES for s_ in VALUES] + [(b, s_) for b in CORNERS for s_ in CORNERS]
        for cb, cs in pack(pairs):
            MIX.append((mode, cb, cs))
    else:
        for cb in TRIPLES:
            for cs in TRIPLES:
                MIX.append((mode, cb, cs))


# --- exactness and conditioning (STOP-2): S11's double evaluation, traced with an error bound ------------------------
# Each value of the double evaluation is carried as its exact value and a bound on how far the double can be from it,
# in the standard model of rounding (one operation's double is its exact result on its inputs' doubles, rounded once,
# within 2^-53 of it): an operation whose inputs are exact and whose exact result is a double adds no error.  A sample is
# exact when every bound stays 0, so the double evaluation is the exact answer bit for bit.  Otherwise it is kept only
# when its final bound is below TRACE_LIMIT times max(1, |v|) (an eighth of the tolerance) and no discontinuity of B10 lies
# within the bound of the value that decides it: else the double evaluation may land on the other side of a jump, or
# lose to cancellation more than the tolerance allows, and the sample is refused.
U = F(1, 2 ** 53)
TRACE_LIMIT = F(1, 8) * F(1, 10 ** 12)


def is_double(x):
    """Whether the exact rational @p x is a finite double."""
    if abs(x) > F(2) ** 1024:
        return False
    return F(float(x)) == x


class Refused(Exception):
    """A discontinuity, or a divisor's sign, is decided within the error bound: the sample is not conditioned."""


class E:
    __slots__ = ("v", "e")

    def __init__(self, v, e=F(0)):
        self.v = F(v)
        self.e = F(e)


def rnd(v, prop):
    return E(v, prop + (F(0) if prop == 0 and is_double(v) else U * (abs(v) + prop)))


def e_add(a, b):
    return rnd(a.v + b.v, a.e + b.e)


def e_sub(a, b):
    return rnd(a.v - b.v, a.e + b.e)


def e_mul(a, b):
    return rnd(a.v * b.v, abs(a.v) * b.e + abs(b.v) * a.e + a.e * b.e)


def e_div(a, b):
    if abs(b.v) <= b.e or b.v == 0:
        raise Refused("a divisor within its error of zero")
    q = a.v / b.v
    return rnd(q, (a.e + abs(q) * b.e) / (abs(b.v) - b.e))


def e_const(x):
    """A constant of the formulas: the exact rational, and how far the double the code uses is from it."""
    x = F(x)
    return E(x, abs(F(float(x)) - x))


def e_min(a, b):
    return E(min(a.v, b.v), max(a.e, b.e))


def e_max(a, b):
    return E(max(a.v, b.v), max(a.e, b.e))


def e_sat(a):
    return E(oracle.sat(a.v), a.e)


def e_le(a, t, jump):
    """a <= t, decided on the exact value; at a jump the double must decide it alike."""
    if jump and a.v != t and abs(a.v - t) <= a.e:
        raise Refused("a discontinuity within the error bound")
    return a.v <= t


def e_eq(a, b, jump):
    if jump and a.v != b.v and abs(a.v - b.v) <= a.e + b.e:
        raise Refused("a discontinuity within the error bound")
    return a.v == b.v


ONE_E = E(1)


def trace_separable(mode, b, s_):
    """B7 in S11's order of operations."""
    if mode == "multiply":
        return e_mul(b, s_)
    if mode == "screen":
        return e_sub(e_add(b, s_), e_mul(b, s_))
    if mode in ("hard-light", "overlay"):
        if mode == "overlay":
            b, s_ = s_, b
        two_s = e_mul(E(2), s_)
        if e_le(s_, F(1, 2), False):
            return e_mul(b, two_s)
        u = e_sub(two_s, ONE_E)
        return e_sub(e_add(b, u), e_mul(b, u))
    if mode == "darken":
        return e_min(b, s_)
    if mode == "lighten":
        return e_max(b, s_)
    if mode == "color-dodge":
        if e_eq(b, E(0), True):
            return E(0)
        if e_eq(s_, ONE_E, True):
            return ONE_E
        den = e_sub(ONE_E, s_)
        q = e_div(b, den) if abs(b.v) < oracle.QUOTIENT_CAP * abs(den.v) else E(oracle.capped_mix(mode, b.v, s_.v))
        return e_min(q, ONE_E)
    if mode == "color-burn":
        if e_eq(b, ONE_E, True):
            return ONE_E
        if e_eq(s_, E(0), True):
            return E(0)
        num = e_sub(ONE_E, b)
        q = e_div(num, s_) if abs(num.v) < oracle.QUOTIENT_CAP * abs(s_.v) else E(1 - oracle.capped_mix(mode, b.v, s_.v))
        return e_sub(ONE_E, e_min(q, ONE_E))
    if mode == "soft-light":
        two_s = e_mul(E(2), s_)
        if e_le(s_, F(1, 2), False):
            return e_sub(b, e_mul(e_mul(e_sub(ONE_E, two_s), b), e_sub(ONE_E, b)))
        if e_le(b, F(1, 4), False):
            dee = e_mul(e_add(e_mul(e_sub(e_mul(E(16), b), E(12)), b), E(4)), b)
        else:
            r = oracle.root(b.v, oracle.Exactness())
            prop = b.e / (2 * r) if b.e else F(0)
            dee = E(r, prop + (F(0) if prop == 0 and is_double(r) else U * (r + prop)))
        return e_add(b, e_mul(e_sub(two_s, ONE_E), e_sub(dee, b)))
    if mode == "difference":
        d = e_sub(b, s_)
        return E(abs(d.v), d.e)
    if mode == "exclusion":
        return e_sub(e_add(b, s_), e_mul(e_mul(E(2), b), s_))
    raise ValueError(mode)


def trace_lum(c):
    return e_add(e_add(e_mul(e_const(F(3, 10)), c[0]), e_mul(e_const(F(59, 100)), c[1])), e_mul(e_const(F(11, 100)), c[2]))


def trace_clip(c):
    el = trace_lum(c)
    n = c[0] if c[0].v <= c[1].v and c[0].v <= c[2].v else c[1] if c[1].v <= c[2].v else c[2]
    x = c[0] if c[0].v >= c[1].v and c[0].v >= c[2].v else c[1] if c[1].v >= c[2].v else c[2]
    grey = e_eq(n, x, True)
    if grey or (n.v >= 0 and x.v <= 1):
        return list(c)
    # S11 forms the offsets and both denominators from the differences C - n (B10), so that L - n is a sum of terms
    # that are not negative: L - n = Lum(C - n), x - L = (x - n) - (L - n), C - L = (C - n) - (L - n).
    d = [e_sub(v, n) for v in c]
    ln = trace_lum(d)
    xl = e_sub(e_sub(x, n), ln)
    t = [e_sub(v, ln) for v in d]
    if n.v < 0:
        t = [e_div(e_mul(v, el), ln) for v in t]
    if x.v > 1:
        t = [e_div(e_mul(v, e_sub(ONE_E, el)), xl) for v in t]
    return [e_add(el, v) for v in t]


def trace_set_lum(c, el):
    d = e_sub(el, trace_lum(c))
    return trace_clip([e_add(v, d) for v in c])


def trace_sat(c):
    return e_sub(e_max(e_max(c[0], c[1]), c[2]), e_min(e_min(c[0], c[1]), c[2]))


def trace_set_sat(c, sv):
    imin, imid, imax = sorted(range(3), key=lambda i: c[i].v)
    out = [E(0), E(0), E(0)]
    if not e_eq(c[imax], c[imin], True):
        out[imid] = e_div(e_mul(e_sub(c[imid], c[imin]), sv), e_sub(c[imax], c[imin]))
        out[imax] = sv
    return out


def trace_mix(mode, cb, cs):
    """B7 to B9 traced: the saturated result, each channel an E."""
    if mode in oracle.SEPARABLE:
        out = [trace_separable(mode, cb[i], cs[i]) for i in range(3)]
    elif mode == "hue":
        out = trace_set_lum(trace_set_sat(cs, trace_sat(cb)), trace_lum(cb))
    elif mode == "saturation":
        out = trace_set_lum(trace_set_sat(cb, trace_sat(cs)), trace_lum(cb))
    elif mode == "color":
        out = trace_set_lum(cs, trace_lum(cb))
    else:
        out = trace_set_lum(cb, trace_lum(cs))
    return [e_sat(v) for v in out]


def bound_ok(values):
    """Exact (every bound 0), or kept (every bound under TRACE_LIMIT of max(1, |v|)): (exact, kept)."""
    exact = all(v.e == 0 for v in values)
    kept = all(v.e <= TRACE_LIMIT * max(F(1), abs(v.v)) for v in values)
    return exact, kept


def mix_verdict(mode, cb, cs):
    """kMix: exact inputs; (exact, kept)."""
    try:
        out = trace_mix(mode, [E(v) for v in cb], [E(v) for v in cs])
    except Refused:
        return False, False
    return bound_ok(out)


def input_e(v):
    """A layer value reaching a blend: exact when it is a double the case has computed exactly (the caller's
    heuristics say so); otherwise allowed a few roundings of error."""
    return E(v, F(0) if is_double(v) else 4 * U * abs(v))


def step_verdict(mode, s_, d, m):
    """B2 and N5 for one blend at one pixel in S11's order: (exact, kept)."""
    try:
        sv = [input_e(v) for v in s_]
        dv = [input_e(v) for v in d]
        mv = input_e(m)
        sa, da = sv[3], dv[3]
        if sa.v * da.v == 0:
            b = [E(0)] * 3
        else:
            cs = [e_div(sv[i], sa) if abs(sv[i].v) < oracle.SATURATION * abs(sa.v) else E(oracle.straight(s_)[i]) for i in range(3)]
            cb = [e_div(dv[i], da) if abs(dv[i].v) < oracle.SATURATION * abs(da.v) else E(oracle.straight(d)[i]) for i in range(3)]
            b = trace_mix(mode, cb, cs)
        out = []
        for i in range(3):
            out.append(e_add(e_add(e_mul(sv[i], e_sub(ONE_E, da)), e_mul(dv[i], e_sub(ONE_E, sa))), e_mul(e_mul(sa, da), b[i])))
        out.append(e_add(sa, e_mul(da, e_sub(ONE_E, sa))))
        res = [e_sat(e_add(e_mul(e_sub(ONE_E, mv), dv[i]), e_mul(mv, out[i]))) for i in range(4)]
    except Refused:
        return False, False
    return bound_ok(res)


def converts(c):
    """Whether any value of the case moves between two different spaces (so a transfer or a matrix is evaluated)."""
    stack = [c["target"]]
    for it in c["items"]:
        if it["k"] == FILL:
            if it["paint"] == 3:
                table = c["palette"] or []
                if it["entry"] >= len(table):
                    continue
                space = table[it["entry"]][1]
            else:
                space = it["space"] if it["paint"] != 2 else c["fg"][1]
            space = c["target"] if oracle.is_target(space) else space
            if space != stack[-1]:
                return True
        elif it["k"] == GROUP_PUSH:
            stack.append(stack[-1] if oracle.is_target(it["blend"]) else it["blend"])
        elif it["k"] == GROUP_POP:
            inner = stack.pop()
            if inner != stack[-1]:
                return True
    return False


class BlendWatch:
    """The oracle's observer at every blend's apply: is each step exact in doubles, and is the sample conditioned?"""

    def __init__(self, name):
        self.name = name
        self.exact = True

    def __call__(self, mode, s_, d, m):
        exact, kept = step_verdict(mode, s_, d, m)
        if not kept:
            sys.exit("gen-composite-vectors: %s: a %s step whose double evaluation is not conditioned (S %s, D %s, m %s); "
                     "choose another sample" % (self.name, mode, [float(v) for v in s_], [float(v) for v in d], float(m)))
        if not exact:
            self.exact = False


def evaluate_case(c):
    watch = BlendWatch(c["name"])
    oracle.BLEND_OBSERVER = watch
    try:
        got, inexact = evaluate_items(c)
    finally:
        oracle.BLEND_OBSERVER = None
    return got, inexact or not watch.exact


def evaluate_items(c):
    items = []
    cov = []
    for it in c["items"]:
        d = dict(it)
        if it["k"] == FILL:
            d["rgba"] = it["rgba"]
        items.append(d)
        cov.append(it["cov"])
    got, inexact = oracle.evaluate(items, cov, c["fg"], c["target"], c["backdrop"], c["palette"])
    return got, inexact


def is_dyadic_double(x):
    return F(float(x)) == x


def hexf(x):
    return float(x).hex()


def space_cxx(sp):
    return "{%d, %d, %s}" % (sp[0], sp[1], hexf(F(sp[2])))


def render():
    out = []
    out.append("// Generated by tools/gen-composite-vectors.py from tools/composite-oracle.py (exact rationals).\n")
    out.append("// Do not edit; run the generator.  Independent of the library's code.\n")
    out.append("#ifndef GHOTI_IO_GSHSCENE_TEST_COMPOSITE_VECTORS_H\n#define GHOTI_IO_GSHSCENE_TEST_COMPOSITE_VECTORS_H\n\n")
    out.append("namespace composite_vectors {\n\n")
    out.append("struct VSpace {\n  int primaries;\n  int transfer;\n  double gamma;\n};\n\n")
    out.append("// kind: 1 fill, 2 clip push, 3 clip pop, 4 group push, 5 group pop.  paint: 0 solid, 2 foreground, 3 palette.\n")
    out.append("struct VItem {\n  int kind;\n  int paint;\n  double rgba[4];\n  VSpace space;\n  double opacity;\n  VSpace blend;\n"
               "  int composite;\n  double coverage;\n  int entry;\n};\n\n")
    out.append("// A palette entry (N14): straight rgba and its space.\n")
    out.append("struct VColor {\n  double rgba[4];\n  VSpace space;\n};\n\n")
    out.append("// exact: dyadic values and no conversion between spaces, so a double evaluator matches bit for bit.\n")
    out.append("// palette_count -1: the list has no palette (and palette is NULL).\n")
    out.append("struct VCase {\n  const char * name;\n  bool exact;\n  VSpace target;\n  double backdrop[4];\n  double fg[4];\n"
               "  VSpace fg_space;\n  int item_count;\n  const VItem * items;\n  double want[4];\n  int palette_count;\n"
               "  const VColor * palette;\n};\n\n")
    exact_count = 0
    for n, c in enumerate(CASES):
        got, inexact = evaluate_case(c)
        exact = (not inexact) and not converts(c) and all(is_dyadic_double(v) for v in got)
        exact_count += exact
        out.append("static const VItem kItems%d[] = {\n" % n)
        for it in c["items"]:
            k = it["k"]
            rgba = it.get("rgba", (0.0, 0.0, 0.0, 0.0))
            out.append("    {%d, %d, {%s, %s, %s, %s}, %s, %s, %s, %d, %s, %d},\n" % (
                k, it.get("paint", 0), hexf(F(rgba[0])), hexf(F(rgba[1])), hexf(F(rgba[2])), hexf(F(rgba[3])),
                space_cxx(it.get("space", TARGET)), hexf(F(it.get("opacity", 1.0))), space_cxx(it.get("blend", TARGET)),
                it.get("composite", 0), hexf(F(it["cov"])), it.get("entry", 0)))
        out.append("};\n")
        if c["palette"]:
            out.append("static const VColor kPalette%d[] = {\n" % n)
            for rgba, sp in c["palette"]:
                out.append("    {{%s}, %s},\n" % (", ".join(hexf(F(v)) for v in rgba), space_cxx(sp)))
            out.append("};\n")
    out.append("\n// B(Cb, Cs) of B7 and B8 after B9's saturation, on straight inputs: mode is 13 to 27; exact: every step of S11's\n")
    out.append("// double evaluation is exact (STOP-2), so a double evaluator matches want bit for bit.\n")
    out.append("struct VMix {\n  int mode;\n  double cb[3];\n  double cs[3];\n  double want[3];\n  bool exact;\n};\n\n")
    rows = []
    for mode, cb, cs in MIX:
        exact, kept = mix_verdict(mode, [F(v) for v in cb], [F(v) for v in cs])
        if exact or kept:
            rows.append((mode, cb, cs, exact))
    out.append("// %d samples of the grid were refused as not conditioned (the double evaluation's error bound past an\n"
               "// eighth of the tolerance, or a discontinuity within it), and are not here.\n" % (len(MIX) - len(rows)))
    out.append("static const VMix kMix[] = {\n")
    mix_exact = 0
    for mode, cb, cs, exact in rows:
        fcb, fcs = [F(v) for v in cb], [F(v) for v in cs]
        want = [oracle.sat(v) for v in oracle.mix(mode, fcb, fcs, oracle.Exactness())]
        mix_exact += exact
        out.append("    {%d, {%s}, {%s}, {%s}, %s},\n" % (oracle.OP_INDEX[mode], ", ".join(hexf(v) for v in fcb),
                   ", ".join(hexf(v) for v in fcs), ", ".join(hexf(v) for v in want), "true" if exact else "false"))
    out.append("};\n\nstatic const int kMixCount = %d;\nstatic const int kMixExactCount = %d;\n" % (len(rows), mix_exact))
    out.append("\n// B10: ClipColor a rounding off the ray C = L, where the literal Lum(C) - n or x - Lum(C) in doubles is 0: each\n"
               "// triple mixed with itself (cb = cs = c) in every non-separable mode.  A discontinuity the grid refuses; the\n"
               "// inputs are exact, and an evaluation in S11's order (from the differences C - n) is within the tolerance.\n")
    out.append("struct VNearRay {\n  int mode;\n  double c[3];\n  double want[3];\n};\n\nstatic const VNearRay kNearRay[] = {\n")
    near = 0
    for mode in MODES:
        if mode in oracle.SEPARABLE:
            continue
        for t in NEAR_RAY:
            ft = [F(v) for v in t]
            want = [oracle.sat(v) for v in oracle.mix(mode, ft, ft, oracle.Exactness())]
            out.append("    {%d, {%s}, {%s}},\n" % (oracle.OP_INDEX[mode], ", ".join(hexf(v) for v in ft),
                       ", ".join(hexf(v) for v in want)))
            near += 1
    out.append("};\n\nstatic const int kNearRayCount = %d;\n" % near)
    out.append("\nstatic const VCase kCases[] = {\n")
    for n, c in enumerate(CASES):
        got, inexact = evaluate_case(c)
        exact = (not inexact) and not converts(c) and all(is_dyadic_double(v) for v in got)
        out.append("    {\"%s\", %s, %s, {%s}, {%s}, %s, %d, kItems%d, {%s}, %d, %s},\n" % (
            c["name"], "true" if exact else "false", space_cxx(c["target"]),
            ", ".join(hexf(F(v)) for v in c["backdrop"]), ", ".join(hexf(F(v)) for v in c["fg"][0]),
            space_cxx(c["fg"][1]), len(c["items"]), n, ", ".join(hexf(v) for v in got),
            len(c["palette"]) if c["palette"] is not None else -1,
            ("kPalette%d" % n) if c["palette"] else "nullptr"))
    out.append("};\n\nstatic const int kCaseCount = %d;\nstatic const int kExactCaseCount = %d;\n\n" % (len(CASES), exact_count))
    out.append("}  // namespace composite_vectors\n\n#endif\n")
    return "".join(out)


# --- T1 to T7: the four combinations of isolated and knockout (tests/group_vectors.h) ---------------------------------
def tgroup(isolation, knockout, opacity=1.0, op="src-over", blend=TARGET):
    g = group(opacity, op, blend)
    g["isolation"] = isolation
    g["knockout"] = knockout
    return g


GROUP_SCENES = [
    ("two half-transparent fills that cover the pixel", lambda i, k: [tgroup(i, k), fill((1.0, 0.0, 0.0, 0.5)),
                                                                      fill((0.0, 0.0, 1.0, 0.5)), pop()], BACKDROP, SRGB),
    ("fills of partial shape (coverage 1/2 and 1/4)", lambda i, k: [tgroup(i, k), fill((1.0, 0.5, 0.0, 1.0), 0.5),
                                                                     fill((0.0, 0.25, 1.0, 0.75), 0.25), pop()], BACKDROP, SRGB),
    ("a multiply group inside sees the backdrop only when not isolated", lambda i, k: [
        tgroup(i, k), fill((0.5, 1.0, 0.25, 0.5))] + blended("multiply", (1.0, 0.5, 0.5), 1.0) + [pop()],
     (0.25, 0.125, 0.5, 0.5), SRGB),
    ("a screen group of opacity 1/2 holding a difference group and a fill", lambda i, k: [
        tgroup(i, k, 0.5, "screen"), fill((0.25, 0.75, 0.5, 0.75), 0.5)] + blended("difference", (1.0, 0.25, 0.0), 0.5, 0.5) +
        [pop()], (0.5, 0.25, 0.125, 0.75), SRGB),
    ("a clip inside the group weighs the shape of the fills it holds", lambda i, k: [
        tgroup(i, k), fill((1.0, 1.0, 0.0, 1.0), 1.0), clip(0.5), fill((0.0, 0.5, 1.0, 0.5), 1.0), unclip(), pop()],
     BACKDROP, SRGB),
    ("a knockout child inside the group (the child's elements knock out to its own initial backdrop)", lambda i, k: [
        tgroup(i, k), fill((0.0, 1.0, 0.0, 0.5)), tgroup(0, 1), fill((1.0, 0.0, 0.0, 0.5), 0.5),
        fill((0.0, 0.0, 1.0, 0.5), 0.5), pop(), pop()], BACKDROP, SRGB),
    ("a non-isolated child inside the group, blending against what the group gives it", lambda i, k: [
        tgroup(i, k), fill((0.0, 0.5, 1.0, 0.75)), tgroup(1, 0), fill((1.0, 0.0, 0.0, 0.5))] + blended("hard-light", (0.75, 0.75, 0.25), 1.0) +
        [pop(), pop()], (0.75, 0.25, 0.25, 1.0), SRGB),
    ("an unbounded child (src) inside the group acts on its whole clip region", lambda i, k: [
        tgroup(i, k), fill((1.0, 0.0, 0.0, 1.0)), group(0.5, "src"), fill((0.0, 0.0, 1.0, 0.5), 0.5), pop(), pop()],
     BACKDROP, SRGB),
    ("an empty group", lambda i, k: [tgroup(i, k, 0.5, "multiply"), pop()], BACKDROP, SRGB),
    ("a non-dyadic alpha and luminosity inside", lambda i, k: [tgroup(i, k), fill((0.25, 0.75, 0.5, 0.6))] +
        blended("luminosity", (0.9, 0.1, 0.3), 0.7) + [pop()], (0.3, 0.2, 0.1, 0.6), SRGB),
    ("a group in linear light over an sRGB target (the initial backdrop converted in)", lambda i, k: [
        tgroup(i, k, 1.0, "src-over", LIN), fill((1.0, 0.5, 0.25, 0.5), 1.0, SRGB)] + blended("multiply", (0.5, 0.5, 1.0), 1.0, space=SRGB) +
        [pop()], (0.25, 0.25, 0.5, 0.5), SRGB),
    ("out of range inside, under color-burn and color-dodge", lambda i, k: [tgroup(i, k), fill((1.5, -0.5, 0.25, 1.0), 0.5)] +
        blended("color-burn", (-0.25, 0.5, 2.0), 1.0) + blended("color-dodge", (1.5, 0.5, -1.0), 0.5) + [pop()],
     (0.5, 1.5, -0.25, 1.0), SRGB),
    ("nested: the same combination twice", lambda i, k: [tgroup(i, k), fill((0.5, 0.0, 1.0, 0.5), 0.5), tgroup(i, k),
                                                         fill((1.0, 0.5, 0.0, 0.5), 0.5)] + blended("exclusion", (0.25, 1.0, 0.5), 0.75) +
        [pop(), pop()], BACKDROP, SRGB),
]
GROUP_CASES = []
for title, build, backdrop, target in GROUP_SCENES:
    for iso, ko in ((0, 0), (1, 0), (0, 1), (1, 1)):
        name = "%s: %s, %s" % (title, "isolated" if iso == 0 else "non-isolated", "knockout" if ko else "not knockout")
        GROUP_CASES.append(dict(name=name, items=build(iso, ko), target=target, backdrop=backdrop, fg=FG, palette=None))


def evaluate_general_case(c):
    items = [dict(it) for it in c["items"]]
    cov = [it["cov"] for it in c["items"]]
    return oracle.evaluate_general(items, cov, c["fg"], c["target"], c["backdrop"], c["palette"])


def check_default_is_general():
    """T6 over every case of tests/composite_vectors.h: the general model with every group isolated and not knockout
    gives the same exact rationals, so the default combination's vectors are those, bit for bit."""
    for c in CASES:
        want, _ = evaluate_items(c)
        got, _ = evaluate_general_case(c)
        if want != got:
            sys.exit("gen-composite-vectors: T6 fails on %r: %s against %s" % (c["name"], got, want))
    for c in GROUP_CASES:
        if all(it.get("isolation", 0) == 0 and it.get("knockout", 0) == 0 for it in c["items"]):
            want, _ = evaluate_items(c)
            got, _ = evaluate_general_case(c)
            if want != got:
                sys.exit("gen-composite-vectors: T6 fails on %r" % c["name"])


def render_groups():
    check_default_is_general()
    out = []
    out.append("// Generated by tools/gen-composite-vectors.py --groups from tools/composite-oracle.py (exact rationals).\n")
    out.append("// Do not edit; run the generator.  Independent of the library's code.\n")
    out.append("// The general group of PDF's transparency model (T1 to T7 of shape-scene's documentation/design.md): each\n")
    out.append("// scene in the four combinations of isolated and knockout.  The isolated, non-knockout one is N5 to N9: the\n")
    out.append("// generator requires the general model to give every case of composite_vectors.h exactly, so those vectors\n")
    out.append("// are the default combination's, bit for bit.  No sink composites the other three yet (they are refused).\n")
    out.append("#ifndef GHOTI_IO_GSHSCENE_TEST_GROUP_VECTORS_H\n#define GHOTI_IO_GSHSCENE_TEST_GROUP_VECTORS_H\n\n")
    out.append("namespace group_vectors {\n\n")
    out.append("struct VSpace {\n  int primaries;\n  int transfer;\n  double gamma;\n};\n\n")
    out.append("// kind: 1 fill, 2 clip push, 3 clip pop, 4 group push, 5 group pop; a fill is solid.\n")
    out.append("// isolation: 0 isolated, 1 non-isolated; knockout: 0 or 1 (group push only).\n")
    out.append("struct VItem {\n  int kind;\n  double rgba[4];\n  VSpace space;\n  double opacity;\n  VSpace blend;\n"
               "  int composite;\n  int isolation;\n  int knockout;\n  double coverage;\n};\n\n")
    out.append("// exact: dyadic values, no conversion and no blend mode, so a double evaluator matches want bit for bit.\n")
    out.append("struct VCase {\n  const char * name;\n  bool exact;\n  VSpace target;\n  double backdrop[4];\n  int item_count;\n"
               "  const VItem * items;\n  double want[4];\n};\n\n")
    exact_count = 0
    rows = []
    for n, c in enumerate(GROUP_CASES):
        got, inexact = evaluate_general_case(c)
        blends = any(it["k"] == GROUP_PUSH and it["composite"] >= 13 for it in c["items"])
        exact = (not inexact) and not converts(c) and not blends and all(is_dyadic_double(v) for v in got)
        exact_count += exact
        out.append("static const VItem kItems%d[] = {\n" % n)
        for it in c["items"]:
            rgba = it.get("rgba", (0.0, 0.0, 0.0, 0.0))
            out.append("    {%d, {%s, %s, %s, %s}, %s, %s, %s, %d, %d, %d, %s},\n" % (
                it["k"], hexf(F(rgba[0])), hexf(F(rgba[1])), hexf(F(rgba[2])), hexf(F(rgba[3])),
                space_cxx(it.get("space", TARGET)), hexf(F(it.get("opacity", 1.0))), space_cxx(it.get("blend", TARGET)),
                it.get("composite", 0), it.get("isolation", 0), it.get("knockout", 0), hexf(F(it["cov"]))))
        out.append("};\n")
        rows.append("    {\"%s\", %s, %s, {%s}, %d, kItems%d, {%s}},\n" % (
            c["name"], "true" if exact else "false", space_cxx(c["target"]), ", ".join(hexf(F(v)) for v in c["backdrop"]),
            len(c["items"]), n, ", ".join(hexf(v) for v in got)))
    out.append("\nstatic const VCase kCases[] = {\n")
    out += rows
    out.append("};\n\nstatic const int kCaseCount = %d;\nstatic const int kExactCaseCount = %d;\n\n" % (len(GROUP_CASES), exact_count))
    out.append("}  // namespace group_vectors\n\n#endif\n")
    return "".join(out)



# --- MK1 to MK7: the soft mask (tests/mask_vectors.h) ---------------------------------------------------------------
ALPHA, LUMINANCE = oracle.MASK_ALPHA, oracle.MASK_LUMINANCE
OPACITY, SHAPE = oracle.ACTS_OPACITY, oracle.ACTS_SHAPE
IDENT = oracle.IDENTITY
W3C, REC709, LUMC, SPACE_Y = 0, 1, 2, 3


def mask(pic, mode=ALPHA, coeff=0, space=TARGET, rspace=TARGET, backdrop=(0.0, 0.0, 0.0), bspace=TARGET, tr=IDENT, acts=OPACITY):
    return dict(pic=pic, mode=mode, coeff=coeff, space=space, rspace=rspace, backdrop=backdrop, bspace=bspace, tr=tr, acts=acts)


def mgroup(m, isolation=0, knockout=0, opacity=1.0, op="src-over", blend=TARGET):
    g = tgroup(isolation, knockout, opacity, op, blend)
    g["mask"] = m
    return g


WHITE = fill((1.0, 1.0, 1.0, 1.0))
PIC_A = [fill((1.0, 0.5, 0.25, 0.75))]                                            # alpha 3/4
PIC_B = [fill((0.0, 0.5, 1.0, 1.0), 0.5), fill((1.0, 0.25, 0.0, 0.5), 0.25)]       # two partial fills, overlapping
PIC_E = []                                                                        # draws nothing
PIC_H = [fill((1.0, 0.5, 0.25, 0.75), 0.0)]                                       # a fill of coverage 0
PIC_N = [group(0.5), fill((0.25, 0.75, 0.5, 1.0)), fill((1.0, 0.0, 0.0, 0.5), 0.5), pop()]   # a group inside the picture
PIC_C = [clip(0.5), fill((0.5, 0.5, 1.0, 1.0)), unclip()]                          # clipped to half
PIC_D = [fill((0.25, 0.75, 0.5, 0.6), 0.7)]                                       # a non-dyadic alpha
PIC_T = [fill((0.0, 0.5, 1.0, 0.5))]                                              # translucent
PIC_O = [fill((1.5, -0.5, 0.25, 1.0))]                                            # out of range
PIC_G = [fill((0.5, 0.5, 0.5, 1.0))]                                              # opaque gray
PIC_R, PIC_GR, PIC_BL = [fill((1.0, 0.0, 0.0, 1.0))], [fill((0.0, 1.0, 0.0, 1.0))], [fill((0.0, 0.0, 1.0, 1.0))]
PICTURES = [("alpha 3/4", PIC_A), ("two partial fills", PIC_B), ("white", [WHITE]), ("empty", PIC_E), ("a fill of coverage 0", PIC_H),
            ("a group inside", PIC_N), ("a clip inside", PIC_C), ("a non-dyadic alpha", PIC_D)]

TRANSFERS = [
    ("identity", IDENT),
    ("exponential n=2", ("exp", 0.0, 1.0, 2.0)),
    ("exponential n=0.5", ("exp", 0.0, 1.0, 0.5)),
    ("exponential c0=1/4 c1=3/4 n=1", ("exp", 0.25, 0.75, 1.0)),
    ("exponential past [0,1]", ("exp", -0.5, 1.5, 1.0)),
    ("exponential n=0", ("exp", 0.25, 0.75, 0.0)),
    ("exponential reversed", ("exp", 1.0, 0.0, 1.0)),
    ("exponential n=3", ("exp", 0.0, 1.0, 3.0)),
    ("table [0,1]", ("table", [0.0, 1.0])),
    ("table of four", ("table", [0.0, 0.5, 0.5, 1.0])),
    ("table of three, a bump", ("table", [0.0, 1.0, 0.0])),
    ("table past [0,1]", ("table", [-0.5, 1.5, 0.25])),
    ("stitching of one piece", ("stitch", [(0.0, 1.0, 0.0, 1.0, ("exp", 0.0, 1.0, 2.0))])),
    ("stitching of two pieces", ("stitch", [(0.0, 0.5, 0.0, 1.0, IDENT), (0.5, 1.0, 0.25, 0.75, IDENT)])),
    ("stitching of three, mixed leaves", ("stitch", [(0.0, 0.25, 0.0, 1.0, ("exp", 0.5, 1.0, 1.0)), (0.25, 0.75, 1.0, 0.0, ("table", [0.0, 0.25, 1.0])),
                                                     (0.75, 1.0, 0.0, 1.0, ("exp", 0.0, 1.0, 2.0))])),
]
TR_BY_NAME = dict(TRANSFERS)

SPACE_CASES = []   # (name, space L, space R) for the luminance sweeps
MASK_CASES = []


def mcase(name, items, target=SRGB, backdrop=BACKDROP):
    MASK_CASES.append(dict(name=name, items=items, target=target, backdrop=backdrop, fg=FG, palette=None))


def probe(name, m, target=SRGB):
    """A scene whose answer is the mask's value: an opaque white fill through a masked group over an empty target gives
    (v, v, v, v)."""
    mcase(name, [mgroup(m), WHITE, pop()], target=target, backdrop=(0.0, 0.0, 0.0, 0.0))


# 1. The alpha mask: each picture through the identity, and the picture of alpha 3/4 through every transfer.
for pname, pic in PICTURES:
    probe("alpha mask of %s, identity" % pname, mask(pic))
    if pname not in ("alpha 3/4", "two partial fills"):   # those two go through every transfer below
        probe("alpha mask of %s, exponential n=2" % pname, mask(pic, tr=TR_BY_NAME["exponential n=2"]))
for tname, tr in TRANSFERS[1:]:
    probe("alpha mask of alpha 3/4, %s" % tname, mask(PIC_A, tr=tr))
    probe("alpha mask of two partial fills, %s" % tname, mask(PIC_B, tr=tr))

# 2. The luminance mask: every coefficient set over colored pictures, in R = L and R != L.
LUM_COLORS = [("orange at 3/4", PIC_A), ("opaque blue", PIC_BL), ("opaque red", PIC_R), ("opaque green", PIC_GR),
              ("translucent azure", PIC_T), ("two partial fills", PIC_B), ("out of range", PIC_O), ("a non-dyadic alpha", PIC_D),
              ("opaque gray", PIC_G), ("a group inside", PIC_N)]
for coeff, cname in ((W3C, "W3C"), (REC709, "REC709"), (LUMC, "LUM"), (SPACE_Y, "SPACE_Y")):
    for pname, pic in LUM_COLORS:
        probe("luminance %s of %s, black backdrop" % (cname, pname), mask(pic, LUMINANCE, coeff))
    for lname, l_, r_ in (("L = linear sRGB, R = sRGB", LIN, SRGB), ("L = P3, R = sRGB", P3, SRGB), ("L = sRGB, R = linear P3", SRGB, LINP3),
                          ("L = Adobe gamma, R = BT.2020 linear", ADOBE, BT2020L)):
        for pname, pic in (("orange at 3/4", PIC_A), ("translucent azure", PIC_T)):
            probe("luminance %s of %s, %s" % (cname, pname, lname), mask(pic, LUMINANCE, coeff, space=l_, rspace=r_))
# gray invariance, written as vectors: gray is gray under every set
for coeff, cname in ((W3C, "W3C"), (REC709, "REC709"), (LUMC, "LUM"), (SPACE_Y, "SPACE_Y")):
    for sp, sname in ((SRGB, "sRGB"), (LIN, "linear sRGB"), (P3, "P3")):
        probe("luminance %s of opaque gray in %s is gray" % (cname, sname), mask(PIC_G, LUMINANCE, coeff, space=sp, rspace=sp))

# 3. The backdrop: black, white, a color in L, a color in another space, out of range; a picture that covers little and nothing.
BACKDROPS = [("white", (1.0, 1.0, 1.0), TARGET), ("a color", (0.5, 0.25, 0.75), TARGET), ("a color given in P3", (0.5, 0.25, 0.75), P3),
             ("out of range", (2.0, -1.0, 0.5), TARGET), ("red", (1.0, 0.0, 0.0), TARGET)]
for bname, bk, bsp in BACKDROPS:
    for pname, pic in (("orange at 3/4", PIC_A), ("translucent azure", PIC_T), ("empty", PIC_E), ("two partial fills", PIC_B)):
        probe("luminance W3C of %s over a %s backdrop" % (pname, bname), mask(pic, LUMINANCE, W3C, backdrop=bk, bspace=bsp))
        probe("luminance LUM of %s over a %s backdrop, L = linear sRGB, R = sRGB" % (pname, bname),
              mask(pic, LUMINANCE, LUMC, space=LIN, rspace=SRGB, backdrop=bk, bspace=bsp))

# 4. A luminance mask through each transfer; the outside value (MK5) for every mode, transfer and backdrop.
for tname, tr in TRANSFERS[1:]:
    probe("luminance W3C of orange at 3/4, %s" % tname, mask(PIC_A, LUMINANCE, W3C, tr=tr))
for tname, tr in TRANSFERS:
    for pname, pic in (("empty", PIC_E), ("a fill of coverage 0", PIC_H)):
        probe("outside value, alpha mask of %s, %s" % (pname, tname), mask(pic, tr=tr))
    probe("outside value, luminance REC709 over a color backdrop, %s" % tname,
          mask(PIC_E, LUMINANCE, REC709, backdrop=(0.5, 0.25, 0.75), tr=tr))
    probe("outside value, luminance LUM over a color backdrop in P3, R != L, %s" % tname,
          mask(PIC_H, LUMINANCE, LUMC, space=LIN, rspace=SRGB, backdrop=(0.5, 0.25, 0.75), bspace=P3, tr=tr))

# 5. The transfer's edges: the picture's alpha at a stitching bound, at 0 and 1, on a table's knots, exponent extremes.
for x in (0.0, 0.25, 0.5, 0.75, 1.0, 0.3, 0.9999999999999999):
    for tname in ("stitching of two pieces", "stitching of three, mixed leaves", "table of four", "table of three, a bump"):
        probe("transfer edge: alpha %s through %s" % (x, tname), mask([fill((1.0, 1.0, 1.0, x))], tr=TR_BY_NAME[tname]))
for n in (0.0, 1.0, 0.5, 2.0, 65536.0):
    for x in (0.0, 0.5, 1.0):
        probe("transfer edge: alpha %s through the exponent %s" % (x, n), mask([fill((1.0, 1.0, 1.0, x))], tr=("exp", 0.0, 1.0, n)))

# 6. MK7: a masked group under all four parents, with the value acting on opacity and on shape, for the operators of every
# family: source-over, blend modes, bounded Porter-Duff operators, unbounded ones.
ACT_OPS = ["src-over", "multiply", "screen", "hue", "plus", "dest-over", "src-atop", "xor", "dest-out", "dest-in", "src", "src-in",
           "src-out", "clear"]
PARENTS = ((0, 0), (1, 0), (0, 1), (1, 1))
M_ALPHA = mask(PIC_A)                                                        # v = 3/4
M_LUM = mask(PIC_B, LUMINANCE, LUMC, tr=TR_BY_NAME["exponential n=2"])        # a luminance mask through a transfer


def pname_(i, k):
    return "%s, %s parent" % ("isolated" if i == 0 else "non-isolated", "knockout" if k else "not knockout")


def acts_items(i, k, op, m, acts, inner=(0, 0), opacity=0.75):
    return [tgroup(i, k), fill((0.0, 1.0, 0.0, 0.5), 0.5),
            mgroup(dict(m, acts=acts), inner[0], inner[1], opacity, op), fill((1.0, 0.0, 0.0, 0.5)),
            fill((0.0, 0.0, 1.0, 0.75), 0.5), pop(), pop()]


ACT_RESULTS = {}
for mname, m in (("alpha 3/4", M_ALPHA), ("luminance of two partial fills", M_LUM)):
    for op in ACT_OPS:
        if mname != "alpha 3/4" and op not in ("src-over", "multiply", "dest-in", "src"):
            continue
        for i, k in PARENTS:
            for acts, an in ((OPACITY, "OPACITY"), (SHAPE, "SHAPE")):
                mcase("%s mask acting on %s, %s group, %s" % (mname, an, op, pname_(i, k)), acts_items(i, k, op, m, acts))
                ACT_RESULTS[(mname, op, i, k, an)] = len(MASK_CASES) - 1

# 7. The masked group is itself non-isolated or knockout (T4, T5 compute its content first; the mask then acts on it).
for op in ("src-over", "multiply", "dest-in"):
    for ii, ik in PARENTS:
        for acts, an in ((OPACITY, "OPACITY"), (SHAPE, "SHAPE")):
            mcase("masked group %s, %s on %s" % (pname_(ii, ik), op, an), acts_items(0, 0, op, M_ALPHA, acts, inner=(ii, ik)))

# 8. The order: clip, mask, opacity, then the blend and the composite (a product, then T5).
for i, k in PARENTS:
    for acts, an in ((OPACITY, "OPACITY"), (SHAPE, "SHAPE")):
        mcase("clip 1/2, mask %s, opacity 3/4, multiply: %s" % (an, pname_(i, k)),
              [tgroup(i, k), fill((0.0, 1.0, 0.0, 0.5), 0.5), clip(0.5), mgroup(dict(M_ALPHA, acts=acts), 0, 0, 0.75, "multiply"),
               fill((1.0, 0.0, 0.0, 0.5)), fill((0.0, 0.0, 1.0, 0.75), 0.5), pop(), unclip(), pop()])

# 9. R is the enclosing layer's space when it is zero: a masked group inside a group blended in linear light.
for coeff, cname in ((W3C, "W3C"), (LUMC, "LUM")):
    for acts, an in ((OPACITY, "OPACITY"), (SHAPE, "SHAPE")):
        mcase("mask with R = the enclosing blend space (linear sRGB), %s, %s" % (cname, an),
              [tgroup(0, 0, 1.0, "src-over", LIN), fill((0.5, 0.25, 0.75, 0.75), 0.5, SRGB),
               mgroup(mask(PIC_B, LUMINANCE, coeff, acts=acts), 0, 0, 0.75, "src-over"), fill((1.0, 0.5, 0.0, 1.0), 0.75, SRGB), pop(), pop()])
        mcase("mask with R = sRGB inside a group blended in linear light, %s, %s" % (cname, an),
              [tgroup(0, 0, 1.0, "src-over", LIN), fill((0.5, 0.25, 0.75, 0.75), 0.5, SRGB),
               mgroup(mask(PIC_B, LUMINANCE, coeff, rspace=SRGB, acts=acts), 0, 0, 0.75, "src-over"), fill((1.0, 0.5, 0.0, 1.0), 0.75, SRGB),
               pop(), pop()])
# ... and the picture is composited in R: translucent sRGB colors overlapping in linear light are not the same as in sRGB.
PIC_X = [fill((0.8, 0.2, 0.4, 0.5), 1.0, SRGB), fill((0.1, 0.9, 0.3, 0.5), 1.0, SRGB), fill((0.9, 0.9, 0.1, 0.25), 0.5, SRGB)]
for coeff, cname in ((W3C, "W3C"), (SPACE_Y, "SPACE_Y")):
    probe("luminance %s of overlapping sRGB colors drawn in R = linear sRGB (enclosing sRGB)" % cname, mask(PIC_X, LUMINANCE, coeff, rspace=LIN))
    probe("luminance %s of overlapping sRGB colors drawn in R = sRGB, L = linear sRGB" % cname, mask(PIC_X, LUMINANCE, coeff, space=LIN, rspace=SRGB))
    mcase("luminance %s of overlapping sRGB colors with R the enclosing linear blend space" % cname,
          [tgroup(0, 0, 1.0, "src-over", LIN), mgroup(mask(PIC_X, LUMINANCE, coeff), 0, 0, 1.0, "src-over"), WHITE, pop(), pop()],
          backdrop=(0.0, 0.0, 0.0, 0.0))
probe("alpha of overlapping sRGB colors, R = linear sRGB, through the identity", mask(PIC_X, rspace=LIN))
mcase("a masked group in linear light over an sRGB target", [mgroup(mask(PIC_A, LUMINANCE, LUMC), 0, 0, 1.0, "src-over", LIN),
                                                            fill((1.0, 0.5, 0.25, 0.75), 0.75, SRGB), pop()])

# 10. A mask of a mask: the picture holds a masked group (the inner mask's R is the picture's, MK2), to depth two.
INNER = mask([fill((1.0, 1.0, 1.0, 0.5))], tr=TR_BY_NAME["exponential n=2"])
INNER_L = mask(PIC_B, LUMINANCE, LUMC, space=LIN)
for acts, an in ((OPACITY, "OPACITY"), (SHAPE, "SHAPE")):
    pic2 = [mgroup(dict(INNER, acts=acts), 0, 0, 1.0), fill((1.0, 0.5, 0.25, 0.75)), pop()]
    mcase("mask of a mask: an alpha mask whose picture holds an alpha-masked group (%s)" % an,
          [mgroup(mask(pic2)), WHITE, pop()], backdrop=(0.0, 0.0, 0.0, 0.0))
    pic3 = [mgroup(dict(INNER_L, acts=acts), 0, 0, 1.0), fill((0.25, 0.75, 0.5, 1.0)), pop()]
    mcase("mask of a mask: a luminance mask whose picture holds a luminance-masked group (%s)" % an,
          [mgroup(mask(pic3, LUMINANCE, W3C, backdrop=(0.5, 0.5, 0.5))), WHITE, pop()], backdrop=(0.0, 0.0, 0.0, 0.0))
pic4 = [mgroup(INNER, 0, 0, 1.0), fill((1.0, 0.5, 0.25, 0.75)), pop()]
pic5 = [mgroup(mask(pic4), 0, 0, 1.0), fill((0.5, 0.75, 1.0, 1.0)), pop()]
mcase("mask of a mask of a mask (depth three)", [mgroup(mask(pic5, LUMINANCE, REC709)), WHITE, pop()], backdrop=(0.0, 0.0, 0.0, 0.0))

# 11. A target that is not sRGB, and a root that is not empty.
mcase("a masked group over a half-transparent linear target", [mgroup(mask(PIC_T, LUMINANCE, LUMC), 0, 0, 1.0, "multiply"),
                                                               fill((1.0, 0.5, 0.25, 1.0), 0.75), pop()], target=LIN, backdrop=(0.125, 0.0625, 0.25, 0.5))
mcase("a masked group of opacity 0", [mgroup(M_ALPHA, 0, 0, 0.0, "src-over"), fill((1.0, 0.5, 0.25, 1.0)), pop()])
mcase("a masked group of opacity 0 with an unbounded operator, acting on SHAPE",
      [mgroup(dict(M_ALPHA, acts=SHAPE), 0, 0, 0.0, "src"), fill((1.0, 0.5, 0.25, 1.0)), pop()])
mcase("an empty masked group, unbounded, on SHAPE", [mgroup(dict(M_ALPHA, acts=SHAPE), 0, 0, 1.0, "dest-in"), pop()])
mcase("an empty masked group, unbounded, on OPACITY", [mgroup(M_ALPHA, 0, 0, 1.0, "dest-in"), pop()])


# --- the table of masks, and the generator's own checks ---------------------------------------------------------------
def canon(x):
    if isinstance(x, dict):
        return tuple(sorted((k, canon(v)) for k, v in x.items()))
    if isinstance(x, (list, tuple)):
        return tuple(canon(v) for v in x)
    return x


class MaskTable:
    """Every mask a case or a table mentions, once, children before the masks that hold them."""

    def __init__(self):
        self.index = {}
        self.masks = []

    def add(self, m):
        for it in m["pic"]:
            if it.get("mask") is not None:
                self.add(it["mask"])
        key = canon(m)
        if key not in self.index:
            self.index[key] = len(self.masks)
            self.masks.append(m)
        return self.index[key]

    def of(self, m):
        return self.index[canon(m)]


def walk_masks(items):
    for it in items:
        if it.get("mask") is not None:
            yield it["mask"]


def evaluate_mask_case(c):
    items = [dict(it) for it in c["items"]]
    return oracle.evaluate_general(items, [it["cov"] for it in items], c["fg"], c["target"], c["backdrop"], c["palette"])


def mask_tree(m):
    yield m
    for child in walk_masks(m["pic"]):
        yield from mask_tree(child)


def transfer_exact(tr):
    """Whether a double evaluation of the transfer is exact on dyadic inputs: the identity, a table, a stitching whose pieces
    are of a power-of-two width over such leaves.  (A power is evaluated by gmath_pow and is never marked exact.)"""
    if tr[0] in ("identity", "table"):
        return True
    if tr[0] == "stitch":
        return all(F(hi - lo).numerator == 1 and (F(hi - lo).denominator & (F(hi - lo).denominator - 1)) == 0 and leaf[0] != "exp"
                   for lo, hi, e0, e1, leaf in tr[1])
    return False


def mask_case_exact(c, got, inexact):
    for it in c["items"]:
        for top in walk_masks([it]):
            for m in mask_tree(top):
                if m["mode"] != ALPHA or not transfer_exact(m["tr"]):
                    return False
                if any(sp != TARGET for sp in (m["space"], m["bspace"])):
                    return False
                if any(p.get("k") == FILL and not oracle.is_target(p.get("space", TARGET)) for p in m["pic"]):
                    return False
                if any(p.get("k") == GROUP_PUSH and not oracle.is_target(p["blend"]) for p in m["pic"]):
                    return False
    blends = any(it["k"] == GROUP_PUSH and it["composite"] >= 13 for it in c["items"])
    return (not inexact) and not converts(c) and not blends and all(is_dyadic_double(v) for v in got) and not any(
        m["rspace"] != TARGET for top in walk_masks(c["items"]) for m in mask_tree(top))


def check_distinguished():
    """The vectors distinguish OPACITY from SHAPE where MK7 says they differ, and only there: under a knockout parent for
    source-over and the blend modes, and under every parent for an unbounded operator; a bounded Porter-Duff operator
    is affine in the source, so it agrees under a parent that is not knockout."""
    pdf_ops = oracle.PDF_OPS
    differs = 0
    for (mname, op, i, k, an), n in ACT_RESULTS.items():
        if an != "OPACITY":
            continue
        a, _ = evaluate_mask_case(MASK_CASES[n])
        b, _ = evaluate_mask_case(MASK_CASES[ACT_RESULTS[(mname, op, i, k, "SHAPE")]])
        expect = k == 1 or oracle.is_unbounded(op)
        if (a != b) != expect:
            sys.exit("gen-composite-vectors: OPACITY and SHAPE %s for %s under (%d, %d), %s" % (
                "agree where they should differ" if expect else "differ where they should agree", op, i, k, mname))
        differs += expect
    if differs < 20:
        sys.exit("gen-composite-vectors: too few vectors tell OPACITY from SHAPE (%d)" % differs)


def check_mask_probes():
    """Every probe's answer is (v, v, v, v) with v in [0, 1], and the family sweeps are not all one number."""
    values = set()
    for c in MASK_CASES:
        if len(c["items"]) >= 3 and c["items"][0].get("mask") is not None and c["items"][1] is WHITE and c["backdrop"] == (0.0, 0.0, 0.0, 0.0):
            got, _ = evaluate_mask_case(c)
            if not (got[0] == got[1] == got[2] == got[3] and 0 <= got[0] <= 1):
                sys.exit("gen-composite-vectors: the probe %r is not a value in [0, 1]: %s" % (c["name"], got))
            values.add(got[0])
    if len(values) < 60:
        sys.exit("gen-composite-vectors: the probes take only %d distinct values" % len(values))


SAMPLE_XS = [-0.25, 0.0, 0.0625, 0.125, 0.25, 0.375, 0.5, 0.625, 0.75, 0.875, 1.0, 1.25, 0.3, 0.7, 0.3333333333333333, 0.9999999999999999]
RESOLVE_ENCLOSING = [SRGB, LIN, P3, ADOBE]


def transfer_rows(table):
    rows = []
    for tname, tr in TRANSFERS:
        m = mask(PIC_E, tr=tr)
        n = table.add(m)
        for x in SAMPLE_XS:
            ex = oracle.Exactness()
            want = oracle.clamp01(oracle.transfer_raw(tr, oracle.clamp01(F(x)), ex))
            exact = transfer_exact(tr) and not ex.inexact and is_dyadic_double(want) and F(x) == F(float(x)) and F(x).denominator <= 2 ** 20
            rows.append((n, x, want, exact))
    return rows


def resolve_rows(table):
    rows = []
    masks = [("W3C", mask(PIC_E, LUMINANCE, W3C)), ("REC709", mask(PIC_E, LUMINANCE, REC709)), ("LUM", mask(PIC_E, LUMINANCE, LUMC))]
    for sp in (SRGB, LIN, P3, LINP3, ADOBE, BT2020L):
        masks.append(("SPACE_Y", mask(PIC_E, LUMINANCE, SPACE_Y, space=sp)))
    masks.append(("SPACE_Y, R zero", mask(PIC_E, LUMINANCE, SPACE_Y)))
    masks.append(("R explicit", mask(PIC_E, LUMINANCE, W3C, rspace=P3, bspace=ADOBE)))
    masks.append(("L explicit, B", mask(PIC_E, LUMINANCE, REC709, space=LIN, rspace=SRGB, bspace=P3)))
    for label, m in masks:
        n = table.add(m)
        for enc in RESOLVE_ENCLOSING:
            r = enc if oracle.is_target(m["rspace"]) else m["rspace"]
            l_ = r if oracle.is_target(m["space"]) else m["space"]
            b = l_ if oracle.is_target(m["bspace"]) else m["bspace"]
            k = oracle.coefficients(m["coeff"], l_)
            rows.append((n, enc, [float(v) for v in k], r, l_, b, m["coeff"] != SPACE_Y))
    return rows


def render_masks():
    for c in MASK_CASES:
        if c["name"] in ("",):
            sys.exit("unnamed case")
    names = [c["name"] for c in MASK_CASES]
    if len(set(names)) != len(names):
        dup = sorted(n for n in set(names) if names.count(n) > 1)
        sys.exit("gen-composite-vectors: duplicate mask case names: %s" % dup[:3])
    check_distinguished()
    check_mask_probes()
    table = MaskTable()
    for c in MASK_CASES:
        for m in walk_masks(c["items"]):
            table.add(m)
    answers = transfer_rows(table)
    resolves = resolve_rows(table)
    out = []
    out.append("// Generated by tools/gen-composite-vectors.py --masks from tools/composite-oracle.py (exact rationals).\n")
    out.append("// Do not edit; run the generator.  Independent of the library's code.\n")
    out.append("// The soft mask (MK1 to MK7 of shape-scene's documentation/design.md): a masked group's mask value from its picture's\n")
    out.append("// rendered layer (alpha or luminance, the coefficient sets, the backdrop, the transfer, the outside value), and the value\n")
    out.append("// substituted into the opacity or the shape of the group's composite, under the four isolated and knockout parents and\n")
    out.append("// the operators of every family.  Plus the known answers of gshscene_mask_transfer and gshscene_mask_resolve.\n")
    out.append("#ifndef GHOTI_IO_GSHSCENE_TEST_MASK_VECTORS_H\n#define GHOTI_IO_GSHSCENE_TEST_MASK_VECTORS_H\n\n")
    out.append("namespace mask_vectors {\n\n")
    out.append("struct VSpace {\n  int primaries;\n  int transfer;\n  double gamma;\n};\n\n")
    out.append("// kind: 1 fill, 2 clip push, 3 clip pop, 4 group push, 5 group pop; a fill is solid.  isolation: 0 isolated, 1 not;\n")
    out.append("// knockout: 0 or 1 (group push only).  mask: an index into kMasks, or -1 (group push only).  coverage: the shape the\n")
    out.append("// test gives the fill or the clip push.\n")
    out.append("struct VItem {\n  int kind;\n  double rgba[4];\n  VSpace space;\n  double opacity;\n  VSpace blend;\n  int composite;\n"
               "  int isolation;\n  int knockout;\n  double coverage;\n  int mask;\n};\n\n")
    out.append("// MK4: kind 0 identity, 1 exponential (c0, c1, n), 2 table (samples), 3 stitching (pieces; a piece's leaf is\n")
    out.append("// 0, 1 or 2 with the leaf's own fields).\n")
    out.append("struct VPiece {\n  double lo;\n  double hi;\n  double e0;\n  double e1;\n  int leaf_kind;\n  double c0;\n  double c1;\n  double n;\n"
               "  int sample_count;\n  const double * samples;\n};\n\n")
    out.append("struct VTransfer {\n  int kind;\n  double c0;\n  double c1;\n  double n;\n  int sample_count;\n  const double * samples;\n"
               "  int piece_count;\n  const VPiece * pieces;\n};\n\n")
    out.append("// MK1: mode 1 alpha, 2 luminance; coefficients 0 W3C, 1 REC709, 2 LUM, 3 SPACE_Y; acts_on 0 opacity, 1 shape.  The picture\n")
    out.append("// is the items (each with its coverage); a masked group inside it names a mask of a lower index.\n")
    out.append("struct VMask {\n  int mode;\n  int coefficients;\n  VSpace space;\n  VSpace render_space;\n  double backdrop[3];\n  VSpace backdrop_space;\n"
               "  int acts_on;\n  VTransfer transfer;\n  int item_count;\n  const VItem * items;\n};\n\n")

    def item_line(it):
        rgba = it.get("rgba", (0.0, 0.0, 0.0, 0.0))
        mi = table.of(it["mask"]) if it.get("mask") is not None else -1
        return "    {%d, {%s, %s, %s, %s}, %s, %s, %s, %d, %d, %d, %s, %d},\n" % (
            it["k"], hexf(F(rgba[0])), hexf(F(rgba[1])), hexf(F(rgba[2])), hexf(F(rgba[3])),
            space_cxx(it.get("space", TARGET)), hexf(F(it.get("opacity", 1.0))), space_cxx(it.get("blend", TARGET)),
            it.get("composite", 0), it.get("isolation", 0), it.get("knockout", 0), hexf(F(it["cov"])), mi)

    def samples_decl(name, samples):
        for v in samples:
            if F(float(v)) != F(v):
                sys.exit("gen-composite-vectors: transfer sample %r is not a double" % (v,))
        return "static const double %s[] = {%s};\n" % (name, ", ".join(hexf(F(v)) for v in samples))

    def transfer_init(n, tr):
        """Declarations and the VTransfer initializer text of mask @p n's transfer."""
        decl = ""
        kind = tr[0]
        if kind == "identity":
            return decl, "{0, 0x0.0p+0, 0x0.0p+0, 0x0.0p+0, 0, nullptr, 0, nullptr}"
        if kind == "exp":
            return decl, "{1, %s, %s, %s, 0, nullptr, 0, nullptr}" % tuple(hexf(F(v)) for v in tr[1:4])
        if kind == "table":
            decl += samples_decl("kSamples%d" % n, tr[1])
            return decl, "{2, 0x0.0p+0, 0x0.0p+0, 0x0.0p+0, %d, kSamples%d, 0, nullptr}" % (len(tr[1]), n)
        pieces = []
        for j, (lo, hi, e0, e1, leaf) in enumerate(tr[1]):
            lk = {"identity": 0, "exp": 1, "table": 2}[leaf[0]]
            c0 = c1 = nn = "0x0.0p+0"
            cnt, ref = 0, "nullptr"
            if leaf[0] == "exp":
                c0, c1, nn = (hexf(F(v)) for v in leaf[1:4])
            elif leaf[0] == "table":
                decl += samples_decl("kSamples%d_%d" % (n, j), leaf[1])
                cnt, ref = len(leaf[1]), "kSamples%d_%d" % (n, j)
            pieces.append("    {%s, %s, %s, %s, %d, %s, %s, %s, %d, %s},\n" % (
                hexf(F(lo)), hexf(F(hi)), hexf(F(e0)), hexf(F(e1)), lk, c0, c1, nn, cnt, ref))
        decl += "static const VPiece kPieces%d[] = {\n%s};\n" % (n, "".join(pieces))
        return decl, "{3, 0x0.0p+0, 0x0.0p+0, 0x0.0p+0, 0, nullptr, %d, kPieces%d}" % (len(tr[1]), n)

    for n, m in enumerate(table.masks):
        out.append("static const VItem kMaskItems%d[] = {\n" % n)
        if not m["pic"]:
            out.append("    {0, {0x0.0p+0, 0x0.0p+0, 0x0.0p+0, 0x0.0p+0}, {0, 0, 0x0.0p+0}, 0x0.0p+0, {0, 0, 0x0.0p+0}, 0, 0, 0, 0x0.0p+0, -1},\n")
        for it in m["pic"]:
            out.append(item_line(it))
        out.append("};\n")
        decl, init = transfer_init(n, m["tr"])
        out.append(decl)
        out.append("static const VMask kMask%d = {%d, %d, %s, %s, {%s}, %s, %d, %s, %d, kMaskItems%d};\n" % (
            n, m["mode"], m["coeff"], space_cxx(m["space"]), space_cxx(m["rspace"]), ", ".join(hexf(F(v)) for v in m["backdrop"]),
            space_cxx(m["bspace"]), m["acts"], init, len(m["pic"]), n))
    out.append("\nstatic const VMask * const kMasks[] = {%s};\nstatic const int kMaskCount = %d;\n\n" % (
        ", ".join("&kMask%d" % n for n in range(len(table.masks))), len(table.masks)))

    out.append("// exact: dyadic values, no conversion, no blend mode, alpha masks through the identity, a table or a stitching of\n")
    out.append("// power-of-two pieces: a double evaluator matches want bit for bit.\n")
    out.append("struct VCase {\n  const char * name;\n  bool exact;\n  VSpace target;\n  double backdrop[4];\n  int item_count;\n"
               "  const VItem * items;\n  double want[4];\n};\n\n")
    rows = []
    exact_count = 0
    for n, c in enumerate(MASK_CASES):
        got, inexact = evaluate_mask_case(c)
        exact = mask_case_exact(c, got, inexact)
        exact_count += exact
        out.append("static const VItem kItems%d[] = {\n" % n)
        for it in c["items"]:
            out.append(item_line(it))
        out.append("};\n")
        rows.append("    {\"%s\", %s, %s, {%s}, %d, kItems%d, {%s}},\n" % (
            c["name"], "true" if exact else "false", space_cxx(c["target"]), ", ".join(hexf(F(v)) for v in c["backdrop"]),
            len(c["items"]), n, ", ".join(hexf(v) for v in got)))
    out.append("\nstatic const VCase kCases[] = {\n")
    out += rows
    out.append("};\n\nstatic const int kCaseCount = %d;\nstatic const int kExactCaseCount = %d;\n\n" % (len(MASK_CASES), exact_count))

    out.append("// gshscene_mask_transfer of kMasks[mask] at x (MK4, M1-D5): the input clamped, the transfer, the output clamped.\n")
    out.append("struct VTransferAnswer {\n  int mask;\n  double x;\n  double want;\n  bool exact;\n};\n\nstatic const VTransferAnswer kTransferAnswers[] = {\n")
    exact_t = 0
    for n, x, want, exact in answers:
        out.append("    {%d, %s, %s, %s},\n" % (n, hexf(F(x)), hexf(want), "true" if exact else "false"))
        exact_t += exact
    out.append("};\n\nstatic const int kTransferAnswerCount = %d;\nstatic const int kExactTransferAnswerCount = %d;\n\n" % (len(answers), exact_t))
    out.append("// gshscene_mask_resolve of kMasks[mask] for a layer of space enclosing (MK3): the coefficients as doubles (the nearest\n")
    out.append("// double of each exact rational; exact: for SPACE_Y within GSHSCENE_RESOLVE_TOLERANCE), and the three spaces.\n")
    out.append("struct VResolve {\n  int mask;\n  VSpace enclosing;\n  double k[3];\n  bool exact;\n  VSpace render_space;\n  VSpace luminance_space;\n"
               "  VSpace backdrop_space;\n};\n\nstatic const VResolve kResolves[] = {\n")
    for n, enc, k, r, l_, b, exact in resolves:
        out.append("    {%d, %s, {%s}, %s, %s, %s, %s},\n" % (n, space_cxx(enc), ", ".join(hexf(F(v)) for v in k),
                   "true" if exact else "false", space_cxx(r), space_cxx(l_), space_cxx(b)))
    out.append("};\n\nstatic const int kResolveCount = %d;\n\n" % len(resolves))
    out.append("}  // namespace mask_vectors\n\n#endif\n")
    return "".join(out)


if __name__ == "__main__":
    args = sys.argv[1:]
    while "--plant" in args:
        k = args.index("--plant")
        name = args[k + 1] if k + 1 < len(args) else ""
        if name not in oracle.PLANT_NAMES:
            sys.exit("gen-composite-vectors: --plant takes one of: " + ", ".join(oracle.PLANT_NAMES))
        oracle.PLANTS.add(name)
        del args[k:k + 2]
    if "--masks" in args:
        text = render_masks()
        if "--check" in args:
            path = os.path.join(HERE, "..", "tests", "mask_vectors.h")
            with open(path) as f:
                if f.read() != text:
                    sys.exit("gen-composite-vectors: tests/mask_vectors.h is not what the generator writes")
            print("gen-composite-vectors: tests/mask_vectors.h is current")
        else:
            sys.stdout.write(text)
    elif "--check" in args:
        for name, text in (("composite_vectors.h", render()), ("group_vectors.h", render_groups())):
            path = os.path.join(HERE, "..", "tests", name)
            with open(path) as f:
                if f.read() != text:
                    sys.exit("gen-composite-vectors: tests/%s is not what the generator writes" % name)
            print("gen-composite-vectors: tests/%s is current" % name)
    elif "--groups" in args:
        sys.stdout.write(render_groups())
    else:
        sys.stdout.write(render())
