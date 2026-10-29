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
"""Show that the mask's known answers fail on planted defects (MK2 to MK7, CAP-16).

The defects of the oracle itself are planted by the oracle (`composite-oracle.py --selftest --plant NAME`, and
`gen-composite-vectors.py --masks --check --plant NAME`; `make check-composite` runs every one).  This runs the ones
that live in the test evaluator (``tst``: tests/ref_composite.h, recompiled into testComposite with the planted copy ahead)
and in the part of the library the known answers reach (``src``: the transfer and the coefficients of src/resource/mask.c),
with tools/check-planted.py's own machinery: the same plant tuples, the same control, the same rule that a plant must fail a
test and the unplanted build must pass.  The plants are listed here and not in that file's table, which is swept as a whole.

    make check-mask-plants
"""
import importlib.util
import os
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("check_planted", HERE / "check-planted.py")
cp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cp)

RC = "tests/ref_composite.h"
MK = "src/resource/mask.c"
T = ["testComposite"]

LUMINANCE_BLOCK = '''  Px c = convert_premultiplied(g, r_space, l_space);
  double backdrop[3] = {m.backdrop[0], m.backdrop[1], m.backdrop[2]}, b[3];
  convert(backdrop, b_space, l_space, b);
  double over[3];
  for (int k = 0; k < 3; k++) {
    over[k] = sat(c.v[k] + (1.0 - a) * b[k]);
  }
  double y = sat(res.k[0] * over[0] + res.k[1] * over[1] + res.k[2] * over[2]);'''

PLANTS = [
    # the evaluator (MK2, MK5, MK7)
    ('mk-tst-luminance-of-straight-color', 'tst', RC, '    over[k] = sat(c.v[k] + (1.0 - a) * b[k]);',
     '    over[k] = sat((a != 0.0 ? c.v[k] / a : 0.0) + (1.0 - a) * b[k]);', T),
    ('mk-tst-mask-after-blend', 'tst', RC, '          group_element(parent, done, moved, v, m.acts_on == GSHSCENE_MASK_ACTS_ON_SHAPE);',
     '          GFrame before = parent;\n          group_element(parent, done, moved, 1.0, false);\n'
     '          for (int k = 0; k < 4; k++) {\n            parent.layer.v[k] = (1.0 - v) * before.layer.v[k] + v * parent.layer.v[k];\n          }', T),
    ('mk-tst-tr0-as-zero-outside-alpha', 'tst', RC, '  if (m.mode == GSHSCENE_MASK_ALPHA) {\n    return clamp01(',
     '  if (m.mode == GSHSCENE_MASK_ALPHA) {\n    if (g.v[3] == 0.0) {\n      return 0.0;\n    }\n    return clamp01(', T),
    ('mk-tst-tr0-as-zero-outside-luminance', 'tst', RC, '  double y = sat(res.k[0] * over[0]',
     '  if (g.v[3] == 0.0) {\n    return 0.0;\n  }\n  double y = sat(res.k[0] * over[0]', T),
    ('mk-tst-shape-merged-into-opacity', 'tst', RC, 'group_element(parent, done, moved, v, m.acts_on == GSHSCENE_MASK_ACTS_ON_SHAPE);',
     'group_element(parent, done, moved, v, false);', T),
    ('mk-tst-acts-on-ignored', 'tst', RC, 'group_element(parent, done, moved, v, m.acts_on == GSHSCENE_MASK_ACTS_ON_SHAPE);',
     'group_element(parent, done, moved, v, true);', T),
    ('mk-tst-backdrop-before-conversion', 'tst', RC, LUMINANCE_BLOCK,
     '''  double backdrop[3] = {m.backdrop[0], m.backdrop[1], m.backdrop[2]}, b[3];
  convert(backdrop, b_space, r_space, b);
  double composed[3], over[3];
  for (int k = 0; k < 3; k++) {
    composed[k] = sat(g.v[k] + (1.0 - a) * b[k]);
  }
  convert(composed, r_space, l_space, over);
  double y = sat(res.k[0] * over[0] + res.k[1] * over[1] + res.k[2] * over[2]);''', T),
    ('mk-tst-coefficient-order', 'tst', RC, 'double y = sat(res.k[0] * over[0] + res.k[1] * over[1] + res.k[2] * over[2]);',
     'double y = sat(res.k[2] * over[0] + res.k[1] * over[1] + res.k[0] * over[2]);', T),
    ('mk-tst-opacity-into-shape-too', 'tst', RC, '    double ov = o * (shape ? 1.0 : v);', '    double ov = o * v;', T),
    ('mk-tst-pdf-shape-without-v', 'tst', RC, '    double f = done.outer * (shape ? v : 1.0) * done.shape_g;',
     '    double f = done.outer * done.shape_g;', T),
    ('mk-tst-picture-space', 'tst', RC, '  Px g = evaluate_general(picture, pictures->at(m.picture), r_space, Px{{0, 0, 0, 0}}, pictures);',
     '  Px g = evaluate_general(picture, pictures->at(m.picture), enclosing, Px{{0, 0, 0, 0}}, pictures);', T),
    # the library's transfer and coefficients (MK3, MK4), reached by the known answers
    ('mk-src-lum-is-rec709', 'src', MK, '      r.k[0] = 30.0 / 100.0;\n      r.k[1] = 59.0 / 100.0;\n      r.k[2] = 11.0 / 100.0;',
     '      r.k[0] = 2126.0 / 10000.0;\n      r.k[1] = 7152.0 / 10000.0;\n      r.k[2] = 722.0 / 10000.0;', T),
    ('mk-src-w3c-blue', 'src', MK, '      r.k[2] = 721.0 / 10000.0;', '      r.k[2] = 722.0 / 10000.0;', T),
    ('mk-src-backdrop-space-default', 'src', MK, '? r.luminance_space : m->backdrop_space;', '? r.render_space : m->backdrop_space;', T),
    ('mk-src-stitching-side', 'src', MK, '      if (x < t->pieces[mid].hi) {', '      if (x <= t->pieces[mid].hi) {', T),
    ('mk-src-exponent-inverted', 'src', MK, 'gmath_pow(x, leaf->n)', 'gmath_pow(x, 1.0 / leaf->n)', T),
    ('mk-src-table-last-interval', 'src', MK, '      if (i > m - 2) {\n        i = m - 2;', '      if (i > m - 3) {\n        i = m - 3;', T),
    ('mk-src-dropped-clamp', 'src', MK, '  return clamp01(y);\n}\n\ndouble gshscene_mask_transfer', '  return y;\n}\n\ndouble gshscene_mask_transfer', T),
    ('mk-src-input-clamp', 'src', MK, '  x = clamp01(x);\n  double y;', '  double y;', T),
    ('mk-src-zero-power', 'src', MK, 'leaf->n == 0.0 ? 1.0 : (x == 0.0 ? 0.0 : gmath_pow(x, leaf->n))', '(x == 0.0 ? 0.0 : gmath_pow(x, leaf->n))', T),
    ('mk-src-last-piece-at-one', 'src', MK, '    size_t hi = t->piece_count - 1;\n    while (lo < hi) {', '    size_t hi = t->piece_count - 1;\n    if (x == 1.0) {\n      hi = 0;\n    }\n    while (lo < hi) {', T),
]


def main():
    cp.shutil.rmtree(cp.WORK, ignore_errors=True)
    cp.WORK.mkdir(parents=True)
    srcs = cp.sources()
    control = cp.compile_all(cp.WORK / "control", srcs)
    for test in T:
        exe = cp.WORK / ("control-" + test)
        cp.link_test(test, control, exe)
        rc, failing = cp.run_test(exe)
        if rc != 0:
            print("FAIL  control: %s fails on the unplanted build: %s" % (test, failing))
            return 1
    print("ok    control: %s passes on the unplanted build" % ", ".join(T))
    failures = 0
    for plant in PLANTS:
        try:
            _, caught = cp.do_plant(plant, control)
        except Exception as e:  # a plant that cannot be built is a defect of the harness
            print("FAIL  %-40s %s" % (plant[0], e))
            failures += 1
            continue
        if not caught:
            print("FAIL  %-40s not caught by %s" % (plant[0], " ".join(plant[5])))
            failures += 1
        else:
            print("ok    %-40s caught by %s" % (plant[0], ", ".join("%s (%s)" % (t, (f[0][14:70] if f else "crash")) for t, f in caught)))
            cp.shutil.rmtree(cp.WORK / plant[0], ignore_errors=True)
    print("%d mask plants, %d not caught" % (len(PLANTS), failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
