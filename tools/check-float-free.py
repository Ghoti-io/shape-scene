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
"""Fail if a float-free header names a floating type (AD-5, CAP-10; story 14a).

The glyph path provider seam (include/ghoti.io/shape-scene/glyph_provider.h) is
included by `font`, whose headers name no floating type, so it must not name
one either, nor may any header of the suite it includes.  Each header given is
preprocessed alone by the compiler with the library's include flags, and every
token `float`, `double`, `_FloatN`, `_FloatNx`, `__float128`, `__fp16` and
`_DecimalN` (`long double` is a `double`) in a line that came from a header of
the suite (a path with a `ghoti.io/` directory, or under this library's
include/) is reported with its file and line.  The C library's own headers are
not the seam's and are not read: GCC's <stddef.h> names `long double` in
`max_align_t`.

The gate is seen to fail in every run: a planted header that includes the first
one and declares a `double` must be refused, naming the planted file; a control
that includes only <stddef.h> and <stdint.h> must pass (so neither a gate that
rejects everything nor one that reads the C library passes); and no header at
all is a failure.

    tools/check-float-free.py --cc "$(CC)" --flags "$(INCLUDE)" header...
"""
import argparse
import os
import re
import shlex
import subprocess
import sys
import tempfile

TOKEN = re.compile(r"\b(float|double|_Float(?:16|32|64|128)x?|__float128|__fp16|_Decimal(?:32|64|128))\b")
MARKER = re.compile(r'^#\s*(\d+)\s+"([^"]*)"')


def suite_file(path, roots):
    norm = os.path.normpath(path)
    if "/ghoti.io/" in norm.replace(os.sep, "/") + "/":
        return True
    return any(norm.startswith(r) for r in roots)


def scan(cc, flags, header, roots):
    """Every floating token in the suite's lines of @header's preprocessed text: [(file, line, token)]."""
    with tempfile.TemporaryDirectory() as tmp:
        unit = os.path.join(tmp, "unit.c")
        with open(unit, "w") as f:
            f.write('#include "%s"\n' % os.path.abspath(header))
        cmd = shlex.split(cc) + ["-E", "-std=c17"] + shlex.split(flags) + [unit]
        run = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if run.returncode != 0:
            raise SystemExit("check-float-free: %s does not preprocess:\n%s" % (header, run.stderr))
    found = []
    current, line = "", 0
    for text in run.stdout.splitlines():
        m = MARKER.match(text)
        if m:
            line, current = int(m.group(1)), m.group(2)
            continue
        if suite_file(current, roots):
            for t in TOKEN.findall(text):
                found.append((current, line, t))
        line += 1
    return found


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cc", default=os.environ.get("CC", "cc"))
    ap.add_argument("--flags", default="")
    ap.add_argument("headers", nargs="*")
    args = ap.parse_args()
    roots = [os.path.normpath(os.path.abspath("include"))]
    if not args.headers:
        sys.exit("check-float-free: no header given: a gate over nothing passes nothing")
    failures = 0
    for h in args.headers:
        found = scan(args.cc, args.flags, h, roots)
        for f, n, t in found:
            print("check-float-free: %s:%d: `%s` reaches %s" % (f, n, t, h), file=sys.stderr)
        failures += bool(found)
        if not found:
            print("check-float-free: %s names no floating type, nor does any suite header it includes" % h)
    # Seen to fail: a planted double in a suite header that includes the first; a control of the C library only.
    with tempfile.TemporaryDirectory() as tmp:
        planted_dir = os.path.join(tmp, "ghoti.io")
        os.makedirs(planted_dir)
        planted = os.path.join(planted_dir, "planted.h")
        with open(planted, "w") as f:
            f.write('#include "%s"\ntypedef double gshscene_planted_t;\n' % os.path.abspath(args.headers[0]))
        hits = scan(args.cc, args.flags, planted, roots)
        if not any(f.endswith("planted.h") and t == "double" for f, _, t in hits):
            print("check-float-free: the planted double was not refused, so this gate measures nothing", file=sys.stderr)
            failures += 1
        else:
            print("check-float-free: ok, the planted double is refused, naming ghoti.io/planted.h")
        control = os.path.join(tmp, "control.h")
        with open(control, "w") as f:
            f.write("#include <stddef.h>\n#include <stdint.h>\ntypedef size_t gshscene_control_t;\n")
        if scan(args.cc, args.flags, control, roots):
            print("check-float-free: the control (the C library alone) was refused, so the gate reads too much",
                  file=sys.stderr)
            failures += 1
        else:
            print("check-float-free: ok, the control passes (the C library's own headers are not read)")
    if failures:
        sys.exit("check-float-free: FAIL")


if __name__ == "__main__":
    main()
