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
"""Write tests/gradient_vectors.h: the known answers of the gradient definition (G1 to G13; color-paint story P1).

Two tables, both from tools/gradient-oracle.py:

- **Points** (`kGradientCases`): a gradient (kind, geometry, unit, extend, premultiplied, stops already in its
  interpolation space), a chain of 2D maps (the target's first, then the instances', the item's, the gradient's
  last), a target point and the paint's alpha; the answer is whether anything is drawn (and why not), `t`, and the
  straight color.  Every kind, extend and both interpolation forms, a cone whose start circle is not inside the end
  circle, negative radii, offsets outside [0, 1] and equal offsets, one and no stops, every degenerate case of G10,
  and a zero determinant in each place of the chain (G11).  Points closer than 1e-30 (in `t`) to a discontinuity
  (a stop, a period boundary, the cone's edge, the sweep's cut) are rejected, and the generator says how many.
- **Conversions** (`kResolveCases`): a straight color from one space into another (N2, N3, G3), the answer the
  exact conversion rounded to the nearest double; what gshscene_gradient_resolve is measured against.

A case is `exact` when the oracle's answer is rational and every quantity a double evaluation of G6 to G9 would
form along the way is a double, so a careful evaluator matches it bit for bit; the others are within 1e-12.
tests/unit/test_gradient.cpp checks tests/ref_composite.h's evaluator and the library against both; shape-cpu
(story P2) is measured against the points (it is installed as <ghoti.io/shape-scene/testing/gradient_vectors.h>).

    python3 tools/gen-gradient-vectors.py > tests/gradient_vectors.h
    python3 tools/gen-gradient-vectors.py --check
"""
import importlib.util
import os
import sys
from fractions import Fraction as F

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("gradient_oracle", os.path.join(HERE, "gradient-oracle.py"))
oracle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(oracle)

LINEAR, RADIAL, SWEEP = oracle.LINEAR, oracle.RADIAL, oracle.SWEEP
PAD, REPEAT, REFLECT = oracle.PAD, oracle.REPEAT, oracle.REFLECT
RADIANS, DEGREES, GRADIANS, TURNS = oracle.RADIANS, oracle.DEGREES, oracle.GRADIANS, oracle.TURNS
REASON = {"": 0, "outside": 1, "degenerate": 2, "not-invertible": 3}

ID = (1, 0, 0, 1, 0, 0)


def is_double(x):
    x = F(x)
    return F(float(x)) == x


def hexd(x):
    return float(F(x)).hex()


# --- the gradients ---------------------------------------------------------------------------------------------------
S2 = [(0.0, (1.0, 0.0, 0.0, 1.0)), (1.0, (0.0, 0.0, 1.0, 1.0))]
S3 = [(0.0, (1.0, 0.0, 0.0, 1.0)), (0.5, (0.0, 1.0, 0.0, 0.5)), (1.0, (0.0, 0.0, 1.0, 1.0))]
SEQ = [(0.75, (0.0, 0.0, 1.0, 1.0)), (0.25, (1.0, 0.0, 0.0, 0.25)), (0.25, (0.0, 1.0, 0.0, 1.0)),
       (-0.5, (0.5, 0.5, 0.5, 0.0))]                             # unsorted, two at 0.25, one below 0
SOUT = [(-1.0, (0.25, 0.5, 0.75, 1.0)), (2.0, (0.75, 0.5, 0.25, 0.5))]   # offsets outside [0, 1]
S1 = [(0.5, (0.25, 0.75, 0.5, 0.5))]
S0 = []
SFLAT = [(0.5, (1.0, 0.0, 0.0, 1.0)), (0.5, (0.0, 0.0, 1.0, 1.0))]
STRANS = [(0.0, (1.0, 0.0, 0.0, 1.0)), (1.0, (0.0, 0.0, 1.0, 0.0))]  # a transparent end: premultiplied differs

GRADIENTS = [
    # name, kind, geom, unit, stops
    ("lin_x", LINEAR, (0, 0, 8, 0, 0, 0), 0, S3),
    ("lin_skew", LINEAR, (1, 2, 5, -1, 0, 0), 0, S2),
    ("lin_iso", LINEAR, (0, 0, 8, 0, 1, 3), 0, SEQ),             # COLRv1's rotation point: d not perpendicular
    ("lin_out", LINEAR, (-2, 0, 2, 0, 0, 0), 0, SOUT),
    ("lin_trans", LINEAR, (0, 0, 16, 0, 0, 0), 0, STRANS),
    ("lin_one", LINEAR, (0, 0, 4, 4, 0, 0), 0, S1),
    ("lin_none", LINEAR, (0, 0, 4, 4, 0, 0), 0, S0),
    ("lin_flat", LINEAR, (0, 0, 8, 0, 0, 0), 0, SFLAT),
    ("rad_nested", RADIAL, (1, 0, 1, 0, 0, 10), 0, S3),          # start circle inside the end circle
    ("rad_cone", RADIAL, (0, 0, 1, 10, 0, 3), 0, S2),            # start circle outside: a cone
    ("rad_tangent", RADIAL, (2, 0, 2, 0, 0, 4), 0, SEQ),         # start circle tangent inside
    ("rad_strip", RADIAL, (0, 0, 2, 8, 0, 2), 0, S3),            # equal radii: a strip
    ("rad_negative", RADIAL, (0, 0, -1, 4, 0, 1), 0, S2),        # a negative radius (COLRv1, variations)
    ("rad_focal", RADIAL, (3, 1, 0, 0, 0, 6), 0, STRANS),        # a zero start radius: SVG's focal point
    ("rad_concentric", RADIAL, (2, 1, 1, 2, 1, 7), 0, SEQ),      # one center: SVG's and CSS's common case
    ("sw_deg", SWEEP, (1, 1, 0, 360), DEGREES, S3),
    ("sw_rad", SWEEP, (0, 0, 0.5, 2.5), RADIANS, SEQ),
    ("sw_turn", SWEEP, (-2, 3, 0.125, 0.625), TURNS, S2),
    ("sw_grad", SWEEP, (0, 0, 350, 50), GRADIANS, SOUT),         # a1 < a0
    ("sw_wide", SWEEP, (0, 0, -90, 450), DEGREES, STRANS),       # angles outside one turn
    ("sw_step", SWEEP, (0, 0, 135, 135), DEGREES, S3),           # a0 = a1: a step under pad
    # degenerate (G10): every extend
    ("deg_point", LINEAR, (2, 3, 2, 3, 0, 0), 0, S2),
    ("deg_parallel", LINEAR, (0, 0, 2, 1, 4, 2), 0, S2),
    ("deg_circles", RADIAL, (1, 1, 2, 1, 1, 2), 0, S2),
]

MAPS = {
    "id": ID,
    "translate": (1, 0, 0, 1, 3, -2),
    "scale2": (2, 0, 0, 2, 0, 0),
    "half": (0.5, 0, 0, 0.5, 1, 1),
    "flip": (1, 0, 0, -1, 0, 32),
    "rotate": (0.6, -0.8, 0.8, 0.6, 5, 7),
    "skew": (1, 0.25, 0, 1, 0, 0),
    "zero_x": (0, 0, 0, 1, 0, 0),
    "rank1": (1, 2, 2, 4, 0, 0),
    "tiny": (2.0 ** -600, 0, 0, 2.0 ** -600, 0, 0),
}

# chains: (name, [target, instances..., item, gradient])
CHAINS = [
    ("plain", ["id", "id", "id"]),
    ("moved", ["translate", "scale2", "half"]),
    ("flipped", ["flip", "id", "id"]),
    ("rotated", ["id", "rotate", "skew"]),
    ("instanced", ["translate", "scale2", "half", "id"]),
]
SINGULAR = [
    ("zero_target", ["zero_x", "id", "id"]),
    ("zero_instance", ["id", "rank1", "id", "id"]),
    ("zero_item", ["id", "zero_x", "id"]),
    ("zero_gradient", ["id", "id", "zero_x"]),
    ("tiny_is_invertible", ["id", "tiny", "id"]),
]

POINTS = [(0, 0), (1, 0), (3, 1), (5, 2), (7, 7), (-3, 4), (12, -5), (-9, -9), (2.5, 6.25), (20, 3), (-17, 1),
          (0.125, -0.375)]


def target_point(chain, p):
    """The target point of gradient-space point @p p under @p chain (so the samples land inside the gradient)."""
    m = MAPS[chain[0]]
    m = tuple(F(v) for v in m)
    for name in chain[1:]:
        m = oracle.compose(m, tuple(F(v) for v in MAPS[name]))
    x, y = F(p[0]), F(p[1])
    return (m[0] * x + m[1] * y + m[4], m[2] * x + m[3] * y + m[5])


def cases():
    out = []
    rejected = 0
    for gname, kind, geom, unit, stops in GRADIENTS:
        extends = (PAD, REPEAT, REFLECT)
        premuls = (0, 1) if gname in ("lin_x", "lin_trans", "rad_focal", "sw_wide", "lin_iso") else (0,)
        chains = CHAINS if gname in ("lin_skew", "rad_cone", "sw_turn") else CHAINS[:2]
        if gname == "lin_x":
            chains = CHAINS + SINGULAR
        for extend in extends:
            for premul in premuls:
                for cname, chain in chains:
                    points = POINTS if cname in ("plain", "rotated") else POINTS[::2]
                    for i, p in enumerate(points):
                        # Sample in gradient space, scaled to the gradient's size, and carried to the target.
                        q = target_point(chain, p)
                        alpha = (1.0, 0.5, 0.75)[i % 3]
                        case = dict(kind=kind, geom=geom, unit=unit, extend=extend, premul=premul, stops=stops,
                                    chain=[MAPS[n] for n in chain], q=q, alpha=alpha)
                        try:
                            r = oracle.evaluate(case)
                        except oracle.Undecided:
                            rejected += 1
                            continue
                        if r["drawn"] and r["margin"] is not None and r["margin"] < oracle.D("1e-30"):
                            rejected += 1
                            continue
                        if not all(is_double(c) for c in q):
                            rejected += 1
                            continue
                        exact = r["exact"] and kind == LINEAR and all(
                            is_double(v) for v in ([r["t"]] if r["t"] is not None else []) + r["rgba"])
                        if exact and kind == LINEAR:
                            exact = all(n in ("id", "translate", "scale2", "half", "flip") for n in chain)
                        name = "%s/%s/e%d/p%d/%d" % (gname, cname, extend, premul, i)
                        out.append(dict(name=name, case=case, r=r, exact=exact))
    return out, rejected


SPACES = [(1, 2, 0.0), (1, 1, 0.0), (3, 2, 0.0), (3, 3, 2.2), (2, 3, 2.19921875), (4, 1, 0.0)]
COLORS = [(0.0, 0.0, 0.0), (1.0, 1.0, 1.0), (0.25, 0.5, 0.75), (0.03125, 0.001, 0.9), (1.25, -0.125, 0.5),
          (0.5, 0.5, 0.5), (0.04043, 0.003125, 0.0031)]  # the last just below each sRGB knee


# N1's saturation (Corey, 2026-10-21): stops at the bound 2^16 whose conversion reaches 2^64 at the decode, the
# matrix and the encode; the other components stay inside.  (gamma 10: decode is the 10th power; gamma 0.1: encode is.)
K16 = 2.0 ** 16
SATURATING = [
    ((1, 3, 10.0), (1, 3, 0.1), (K16, -K16, 0.5)),       # decode and encode
    ((1, 3, 10.0), (1, 3, 2.0), (K16, -K16, 0.5)),       # the decode, then a gamma-2 encode
    ((4, 3, 10.0), (1, 3, 10.0), (K16, -K16, -K16)),     # the matrix (BT.2020 to sRGB), then a gamma-10 encode
    ((1, 1, 0.0), (1, 3, 0.1), (K16, -K16, 1.0)),        # the encode of a linear stop
    ((1, 1, 0.0), (1, 1, 0.0), (K16, -K16, 0.0)),        # the same space: copied, never saturated
]


def resolve_cases():
    out = []
    for a in SPACES:
        for b in SPACES:
            for rgb in COLORS:
                got, exact = oracle.convert(rgb, a, b)
                out.append(dict(a=a, b=b, rgb=rgb, want=got, exact=exact))
    for a, b, rgb in SATURATING:
        got, exact = oracle.convert(rgb, a, b)
        out.append(dict(a=a, b=b, rgb=rgb, want=got, exact=exact))
    return out


def render():
    pts, rejected = cases()
    res = resolve_cases()
    o = []
    o.append("// Generated by tools/gen-gradient-vectors.py from tools/gradient-oracle.py (exact rationals, Q(sqrt D),\n")
    o.append("// and a bounded decimal angle).  Do not edit; run the generator.  Independent of the library's code.\n")
    o.append("// %d points; %d sample points rejected as within 1e-30 of a discontinuity or undecidable.\n" % (
        len(pts), rejected))
    o.append("#ifndef GHOTI_IO_GSHSCENE_TEST_GRADIENT_VECTORS_H\n#define GHOTI_IO_GSHSCENE_TEST_GRADIENT_VECTORS_H\n\n")
    o.append("namespace gradient_vectors {\n\n")
    o.append("// A stop in the gradient's interpolation space: offset and straight rgba.\n")
    o.append("struct VStop {\n  double offset;\n  double rgba[4];\n};\n\n")
    o.append("// A 2D map: x' = m[0] x + m[1] y + m[4], y' = m[2] x + m[3] y + m[5].\n")
    o.append("struct VMap {\n  double m[6];\n};\n\n")
    o.append("// kind 1 linear, 2 radial, 3 sweep; geom as GSHSCENE_Gradient (linear p0 p1 d; radial c0 r0 c1 r1; sweep\n"
             "// center a0 a1 0 0); chain[0] the target's map, chain[chain_count - 1] the gradient's.  reason: 0 drawn,\n"
             "// 1 outside the cone (G8), 2 degenerate (G10), 3 not invertible (G11).  exact: a double evaluator\n"
             "// matches bit for bit; else within 1e-12.  rgba: straight, in the interpolation space, alpha scaled.\n")
    o.append("struct VCase {\n  const char * name;\n  int kind;\n  double geom[6];\n  int unit;\n  int extend;\n"
             "  int premultiplied;\n  int stop_count;\n  const VStop * stops;\n  int chain_count;\n"
             "  const VMap * chain;\n  double q[2];\n  double alpha;\n  int drawn;\n  int reason;\n  double t;\n"
             "  double rgba[4];\n  bool exact;\n};\n\n")
    stop_sets = {}
    chain_sets = {}
    for c in pts:
        key = tuple(c["case"]["stops"])
        if key not in stop_sets:
            stop_sets[key] = "kStops%d" % len(stop_sets)
            o.append("static const VStop %s[] = {%s};\n" % (stop_sets[key], ", ".join(
                "{%s, {%s}}" % (hexd(off), ", ".join(hexd(v) for v in rgba)) for off, rgba in key) or "{0, {0, 0, 0, 0}}"))
        ckey = tuple(tuple(m) for m in c["case"]["chain"])
        if ckey not in chain_sets:
            chain_sets[ckey] = "kChain%d" % len(chain_sets)
            o.append("static const VMap %s[] = {%s};\n" % (chain_sets[ckey], ", ".join(
                "{{%s}}" % ", ".join(hexd(v) for v in m) for m in ckey)))
    o.append("\nstatic const VCase kGradientCases[] = {\n")
    for c in pts:
        k = c["case"]
        r = c["r"]
        geom = list(k["geom"]) + [0] * (6 - len(k["geom"]))
        o.append("    {\"%s\", %d, {%s}, %d, %d, %d, %d, %s, %d, %s, {%s, %s}, %s, %d, %d, %s, {%s}, %s},\n" % (
            c["name"], k["kind"], ", ".join(hexd(v) for v in geom), k["unit"], k["extend"], k["premul"],
            len(k["stops"]), stop_sets[tuple(k["stops"])], len(k["chain"]), chain_sets[tuple(tuple(m) for m in k["chain"])],
            hexd(k["q"][0]), hexd(k["q"][1]), hexd(k["alpha"]), r["drawn"], REASON[r["reason"]],
            hexd(r["t"]) if r["t"] is not None else "0.0", ", ".join(hexd(v) for v in r["rgba"]),
            "true" if c["exact"] else "false"))
    o.append("};\n\nstatic const int kGradientCaseCount = %d;\n\n" % len(pts))
    o.append("// A conversion (G3): straight rgb from space `from` into space `to` (primaries, transfer, gamma).\n")
    o.append("struct VResolve {\n  int from[2];\n  double from_gamma;\n  int to[2];\n  double to_gamma;\n"
             "  double rgb[3];\n  double want[3];\n  bool exact;\n};\n\n")
    o.append("static const VResolve kResolveCases[] = {\n")
    for c in res:
        o.append("    {{%d, %d}, %s, {%d, %d}, %s, {%s}, {%s}, %s},\n" % (
            c["a"][0], c["a"][1], hexd(c["a"][2]), c["b"][0], c["b"][1], hexd(c["b"][2]),
            ", ".join(hexd(v) for v in c["rgb"]), ", ".join(hexd(v) for v in c["want"]),
            "true" if c["exact"] else "false"))
    o.append("};\n\nstatic const int kResolveCaseCount = %d;\n\n" % len(res))
    o.append("}  // namespace gradient_vectors\n\n#endif\n")
    return "".join(o)


if __name__ == "__main__":
    text = render()
    if len(sys.argv) > 1 and sys.argv[1] == "--check":
        path = os.path.join(HERE, "..", "tests", "gradient_vectors.h")
        with open(path) as f:
            if f.read() != text:
                sys.exit("gen-gradient-vectors: tests/gradient_vectors.h is not what the generator writes")
        print("gen-gradient-vectors: tests/gradient_vectors.h is current")
    else:
        sys.stdout.write(text)
