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
"""Write tests/composite_vectors.h: the known answers of the compositing definition (N1 to N10).

Every case is a closed list in paint order (fills, clips and groups), the coverage
each fill and clip push is given, the foreground, the space and the initial value of
the root layer, and the exact answer the reference (tools/composite-oracle.py, exact
rationals) computes.  A case whose values are dyadic and that converts between no
spaces has an answer a double evaluator can match bit for bit ("exact"); the others
are compared within a tolerance because they evaluate a transfer function or a matrix.
tests/test_composite.cpp builds each list through the library's API and evaluates it
with tests/ref_composite.h; story 13's shape-cpu is measured against the same table.

    python3 tools/gen-composite-vectors.py > tests/composite_vectors.h
    python3 tools/gen-composite-vectors.py --check
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

# Every operator, opacity 1 and 1/2, over the half-transparent backdrop.
for op in oracle.OPS:
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


def evaluate_case(c):
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


if __name__ == "__main__":
    text = render()
    if len(sys.argv) > 1 and sys.argv[1] == "--check":
        path = os.path.join(HERE, "..", "tests", "composite_vectors.h")
        with open(path) as f:
            if f.read() != text:
                sys.exit("gen-composite-vectors: tests/composite_vectors.h is not what the generator writes")
        print("gen-composite-vectors: tests/composite_vectors.h is current")
    else:
        sys.stdout.write(text)
