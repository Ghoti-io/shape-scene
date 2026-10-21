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
       "FILL_FIXED", "TRUNCATE", "EDIT_PALETTE"]
OP = {name: i for i, name in enumerate(OPS)}

BIASED = {
    "FILL": 5, "CLIP_PUSH": 3, "CLIP_POP": 3, "GROUP_PUSH": 3, "GROUP_POP": 3, "INSTANCE": 4, "CLOSE": 1, "FREEZE": 3,
    "LIST_CREATE": 1, "INTERN_PATH": 3, "INTERN_RUN": 1, "INTERN_OUTLINES": 1, "OPEN_EDIT": 7, "ADD": 22, "SET": 5,
    "REMOVE": 3, "MOVE": 3, "UPSERT": 5, "REMOVE_KEY": 1, "PUBLISH": 9, "MERGE": 6, "HOLD": 5, "DIFF": 5, "EMIT": 4,
    "VISIT": 1, "DUMP": 1, "SCENE_INTERN": 2, "RESET": 1, "DESTROY_EDIT": 1, "SCENE_DESTROY": 1, "SCENE_CREATE": 1,
    "FILL_EX": 5, "CLIP_PUSH_EX": 2, "GROUP_PUSH_EX": 4, "LIST_FOREGROUND": 1, "NODE_PROPS": 5, "EDIT_FOREGROUND": 2,
    "LIST_PALETTE": 2, "FILL_FIXED": 4, "TRUNCATE": 2, "EDIT_PALETTE": 2,
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
