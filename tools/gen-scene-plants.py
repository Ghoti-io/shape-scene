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
"""Operator-level mutation generator for the retained scene's planted defects.

Emits, as JSON on stdout, every candidate mutation of the scene sources as the
tuples tools/check-planted.py takes: (id, 'srcl', file, 'LINE:FRAGMENT',
REPLACEMENT, tests).  The candidates are the relational and logical operators
flipped, each condition negated, each statement deleted, the hash widths
swapped, a letter of each output string changed and a returned status swapped.
It is a generator, not the gate: tools/scene-plants.json holds the candidates
the tests were seen to catch (the sweep's survivors are the equivalent or
unreachable mutants check-planted.py's docstring lists, and compile failures
are dropped), and it is generated from the sources as they stood, so a source
edit that moves a line means regenerating and re-running the sweep:

  python3 tools/gen-scene-plants.py > /tmp/candidates.json
  PLANT_EXTRA_JSON=/tmp/candidates.json make check-planted PREFIX=...

then keep the lines the sweep reports as ok.

With --sink it writes the candidates of the sink contract's sources (story 13a, src/sink; and story 14a's
src/glyph) instead, for tools/sink-plants.json:

  python3 tools/gen-scene-plants.py --sink > /tmp/sink-candidates.json
  PLANT_EXTRA_JSON=/tmp/sink-candidates.json PLANT_ONLY='^(sk|rp|ds|cn)-' make check-planted PREFIX=..."""
import json, re, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXCL = {}

FILES = {
    "src/paint/color_space.c": "cs",
    "src/scene/node.c": "nd",
    "src/scene/edit.c": "ed",
    "src/scene/scene.c": "sc",
    "src/scene/version.c": "vs",
    "src/scene/walk.c": "wk",
    "src/scene/validate.c": "va",
    "src/scene/emit.c": "em",
    "src/scene/diff.c": "df",
    "src/list/fixed.c": "lf",
    "src/resource/gradient.c": "bg",
    "src/paint/convert.c": "bv",
}
LINE_FILTER = {
    "src/store/store.c": r"bytes|fits\(",
    "src/resource/resource.c": r"BYTES|bytes|sat_|palette|rgba|colors|PALETTE|gradient|GRADIENT",
    "src/list/list.c": r"max_bytes|max_nodes|max_node_depth|NODE_DEPTH_CEILING|gshscene_list_canonical|gshscene_list_check_rule|return canonical_|return check_rule"
    # story 12: the order, the ties, the foreground, the composite helpers and the new equality
    r"|unit|frame|scratch|\btie|ties|nonflat|composite|foreground|context|space_equal|color_equal|open_n|out_n|prev_pos|closing|match\["
    # story 14c: the palette, its paint and its resolution, the entry, truncation
    r"|palette|PALETTE|entry|truncate|instance \?|picture >|list->count = count|list->depth|list->expansion =|resolve"
    # color-paint story P1: the gradient paint, its references, hash and dump
    r"|gradient|GRADIENT",
    "src/core/core.c": r"max_bytes|max_nodes|max_node_depth|REFUSED|refused",
}
EXTRA = {
    "src/store/store.c": "bs",
    "src/resource/resource.c": "br",
    "src/list/list.c": "bl",
    "src/core/core.c": "bc",
}
TESTS_EXTRA = ["testBytes", "testCore", "testStore", "testScene_limits", "testOrder", "testList", "testColor", "testDump",
               "testComposite", "testPaint_scene", "testEmit", "testPalette", "testGradient"]
TESTS_BY_FILE = {
    "src/paint/color_space.c": ["testColor", "testList", "testNode", "testPaint_scene", "testScene_dump"],
    "src/scene/node.c": ["testNode", "testEdit", "testScene", "testScene_dump", "testVersion", "testPaint_scene", "testOrder", "testEmit", "testPalette", "testGradient"],
    "src/scene/edit.c": ["testEdit", "testScene", "testNode", "testPaint_scene", "fuzz-replay", "testPalette"],
    "src/scene/scene.c": ["testScene", "testEdit", "testScene_limits", "testDiff", "testEmit", "testPaint_scene", "fuzz-replay", "testPalette"],
    "src/scene/version.c": ["testVersion", "testScene_dump", "testScene", "testEmit", "testDiff", "testPaint_scene", "testOrder", "testPalette", "testGradient"],
    "src/scene/walk.c": ["testVersion", "testEmit", "testDiff", "testScene_limits", "testOrder"],
    "src/scene/validate.c": ["testScene", "testScene_limits", "testEdit", "testPaint_scene", "fuzz-replay"],
    "src/scene/emit.c": ["testEmit", "testScene_limits", "testVersion", "testDiff", "testOrder", "testPaint_scene", "testPalette"],
    "src/scene/diff.c": ["testDiff", "testScene", "testVersion", "testPaint_scene", "testGlyph", "testPalette", "testGradient"],
    "src/list/fixed.c": ["testPalette", "testGradient", "fuzz-replay"],
    "src/resource/gradient.c": ["testGradient", "testDump", "testDump_sink", "fuzz-replay", "fuzz-render", "probe"],
    "src/paint/convert.c": ["testGradient", "probe"],
}

SINK_FILES = {
    "src/sink/sink.c": "sk",
    "src/sink/report.c": "rp",
    "src/sink/dump_sink.c": "ds",
    "src/sink/cancel.c": "cn",
    "src/glyph/glyph.c": "gl",
}
SINK_TESTS = ["testDump_sink", "testSink", "testReport", "testGlyph", "testPalette", "testGradient", "fuzz-render", "probe"]

REL = [("==", "!="), ("!=", "=="), ("<=", "<"), (">=", ">"), ("<", "<="), (">", ">=")]


def strip_strings(line):
    """Blank out string literals and comments so operators inside them are not found."""
    out = []
    i = 0
    in_s = None
    while i < len(line):
        c = line[i]
        if in_s:
            out.append(" ")
            if c == "\\":
                out.append(" ")
                i += 1
            elif c == in_s:
                in_s = None
        elif c in "\"'":
            in_s = c
            out.append(" ")
        elif line.startswith("/*", i):
            j = line.find("*/", i)
            j = len(line) if j < 0 else j + 2
            out.append(" " * (j - i))
            i = j - 1
        elif line.startswith("//", i):
            out.append(" " * (len(line) - i))
            break
        else:
            out.append(c)
        i += 1
    return "".join(out)


def unique_fragment(line, a, b, new_text):
    """Expand [a, b) of line until its text occurs once in the line; returns (fragment, replacement)."""
    lo, hi = a, b
    while True:
        frag = line[lo:hi]
        if line.count(frag) == 1:
            return frag, line[lo:a] + new_text + line[b:hi]
        if lo == 0 and hi == len(line):
            return None
        lo = max(0, lo - 3)
        hi = min(len(line), hi + 3)


def candidates(path):
    text = (ROOT / path).read_text().split("\n")
    flt = re.compile(LINE_FILTER[path]) if path in LINE_FILTER else None
    out = []
    in_comment = False
    for no, raw in enumerate(text, 1):
        stripped = raw.strip()
        if in_comment:
            if "*/" in raw:
                in_comment = False
            continue
        if stripped.startswith("/*") and "*/" not in stripped:
            in_comment = True
            continue
        if not stripped or stripped.startswith(("/*", "*", "//", "#")):
            continue
        if "GSHSCENE_ERR_INTERNAL" in raw:
            continue
        if flt and not flt.search(raw):
            continue
        code = strip_strings(raw)
        # relational and logical operators
        for m in re.finditer(r"(==|!=|<=|>=|&&|\|\||<<|>>|->|<|>)", code):
            op = m.group(1)
            if op in ("<<", ">>", "->"):
                continue
            if op in ("<", ">") and (code[m.start() - 1:m.start()] in ("<", ">", "-") or code[m.end():m.end() + 1] in ("=", "<", ">")):
                continue
            if op in ("<", ">") and "#include" in raw:
                continue
            if op == "&&":
                new = "||"
            elif op == "||":
                new = "&&"
            else:
                new = dict(REL)[op]
            f = unique_fragment(raw, m.start(), m.end(), new)
            if f:
                out.append((no, "op%s" % op, f[0], f[1]))
        # negate a condition
        for kw in ("if (", "while ("):
            idx = code.find(kw)
            if idx >= 0 and (code[:idx].strip() in ("", "}", "} else")):
                start = idx + len(kw)
                depth = 1
                j = start
                while j < len(code) and depth:
                    if code[j] == "(":
                        depth += 1
                    elif code[j] == ")":
                        depth -= 1
                    j += 1
                if depth == 0:
                    cond = raw[start:j - 1]
                    f = unique_fragment(raw, start - 1, j, "(!(" + cond + "))")
                    if f:
                        out.append((no, "neg", f[0], f[1]))
        # delete a statement
        s = stripped
        if s.endswith(";") and not s.startswith(("return", "break", "continue", "goto", "case ", "default", "typedef", "static ", "const ", "int ", "size_t ", "uint", "GSHSCENE_", "GSHGEOM_", "struct ", "unsigned ", "double ", "void ", "char ", "else", "}", "{")) :
            if re.match(r"^[A-Za-z_][A-Za-z0-9_>.\[\]\-*() ]*(\(|=|\+\+|--|\+=|-=)", s) or s.endswith("++;") or s.endswith("--;"):
                indent = raw[: len(raw) - len(raw.lstrip())]
                f = unique_fragment(raw, len(indent), len(raw), ";")
                if f:
                    out.append((no, "del", f[0], f[1]))
        # hash width: a field written with another width
        for old_fn, new_fn in (("gshgeom_hash_u8(", "gshgeom_hash_u32("), ("gshgeom_hash_u32(", "gshgeom_hash_u64("), ("gshgeom_hash_u64(", "gshgeom_hash_u32(")):
            idx = raw.find(old_fn)
            if idx >= 0:
                f = unique_fragment(raw, idx, idx + len(old_fn), new_fn)
                if f:
                    out.append((no, "hw" + old_fn[len("gshgeom_hash_"):-1], f[0], f[1]))
        # a string literal that is output text or a name: its first letter changed
        if path in STRING_FILES:
            for m in re.finditer(r'"([A-Za-z][^"\\]*)"', raw):
                lit = m.group(1)
                if lit.startswith(("/dev", "%")) and False:
                    continue
                a = m.start(1)
                ch = raw[a]
                new_ch = "Z" if ch != "Z" else "Y"
                f = unique_fragment(raw, a, a + 1, new_ch)
                if f:
                    out.append((no, "str", f[0], f[1]))
            for m in re.finditer(r"%016llx|%016|%08" if path in SINK_FILES else r"%016llx", raw):
                f = unique_fragment(raw, m.start(), m.end(), "%llx" if m.group(0) == "%016llx" else "%")
                if f:
                    out.append((no, "wd", f[0], f[1]))
        # swap returned results
        m = re.search(r"return (GSHSCENE_ERR_(INVALID|LIMIT|UNSUPPORTED));", raw)
        if m:
            swap = {"INVALID": "LIMIT", "LIMIT": "INVALID", "UNSUPPORTED": "INVALID"}[m.group(2)]
            f = unique_fragment(raw, m.start(1), m.end(1), "GSHSCENE_ERR_" + swap)
            if f:
                out.append((no, "ret", f[0], f[1]))
    return out


STRING_FILES = {"src/scene/version.c", "src/scene/validate.c", "src/scene/node.c", "src/list/list.c",
                "src/sink/report.c", "src/sink/dump_sink.c", "src/sink/sink.c", "src/resource/gradient.c"}


def main():
    plants = []
    if "--sink" in sys.argv:
        allfiles = dict(SINK_FILES)
        for path in SINK_FILES:
            TESTS_BY_FILE[path] = SINK_TESTS
    else:
        allfiles = dict(FILES)
        allfiles.update(EXTRA)
    for path, tag in allfiles.items():
        seen = {}
        for no, kind, frag, new in candidates(path):
            k = (no, kind)
            seen[k] = seen.get(k, 0) + 1
            pid = "%s-%d-%s%s" % (tag, no, {"op==": "eq", "op!=": "ne", "op<=": "le", "op>=": "ge", "op<": "lt", "op>": "gt", "op&&": "and", "op||": "or"}.get(kind, kind), "" if seen[k] == 1 else str(seen[k]))
            if pid in EXCL:
                continue
            plants.append((pid, "srcl", path, "%d:%s" % (no, frag), new, TESTS_BY_FILE.get(path, TESTS_EXTRA)))
    json.dump(plants, sys.stdout)


main()
