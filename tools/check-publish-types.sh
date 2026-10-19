#!/bin/sh
# SPDX-License-Identifier: LGPL-3.0-only
#
# Copyright (C) 2026 Corey Pennycuff
#
# This file is part of Ghoti.io Shape Scene.
#
# Ghoti.io Shape Scene is free software: you can redistribute it and/or modify it
# under the terms of the GNU Lesser General Public License version 3 as
# published by the Free Software Foundation.
#
# "No front-end or driver can publish" (AD-9, CAP-11), enforced by what a caller can
# hold.  Publishing takes the application's GSHSCENE_Scene; an edit is what a
# front-end is given; no function returns a scene from an edit or a version.  The C
# language cannot hide a function, so the gate compiles three probes (never links):
#
#   tests/types/control.c      a scene publishes an edit: must compile
#   tests/types/no_publish.c   includes only edit.h and calls publish: must not compile
#                              (the name is not declared there)
#   tests/types/edit_as_scene.c  gives publish an edit where the scene goes: must not
#                              compile (an incompatible pointer type, an error under -Werror)
#
# and then proves itself: in a copy of the headers where GSHSCENE_Edit is the same
# type as GSHSCENE_Scene (the capability collapsed), edit_as_scene.c must compile; and
# in a copy where edit.h includes scene.h, no_publish.c must compile.  A probe that
# fails to compile for any other reason (a typo) does not count: the diagnostic must
# name the expected cause.
#
# Environment: CC, CFLAGS, INCLUDE (the Makefile's).

set -u

: "${CC:?CC is not set (run it with make check-publish-types)}"
: "${CFLAGS-}"
: "${INCLUDE:?INCLUDE is not set}"

dir="$(cd "$(dirname "$0")/.." && pwd)"
work="$(mktemp -d "${TMPDIR:-/tmp}/publish-types.XXXXXX")"
trap 'rm -rf "$work"' EXIT
rc=0

compile() { # probe-file include-dir-override... -> prints compiler output, returns status
  probe="$1"
  shift
  # shellcheck disable=SC2086
  $CC $CFLAGS "$@" $INCLUDE -fsyntax-only -Wno-error=unused-function "$probe" 2>&1
}

expect_ok() {
  out="$(compile "$@")"
  status=$?
  if [ $status -ne 0 ]; then
    printf 'check-publish-types: FAIL: %s should compile and does not:\n%s\n' "$1" "$out" >&2
    rc=1
  fi
}

expect_fail() { # probe, pattern, [-I...]
  probe="$1"
  pattern="$2"
  shift 2
  out="$(compile "$probe" "$@")"
  status=$?
  if [ $status -eq 0 ]; then
    printf 'check-publish-types: FAIL: %s compiled, and an edit can publish\n' "$probe" >&2
    rc=1
  elif ! printf '%s\n' "$out" | grep -q "$pattern"; then
    printf 'check-publish-types: FAIL: %s failed to compile, but not for the expected reason (%s):\n%s\n' "$probe" "$pattern" "$out" >&2
    rc=1
  fi
}

# The three probes against the real headers.
expect_ok "$dir/tests/types/control.c" -I "$dir/include"
expect_fail "$dir/tests/types/no_publish.c" "gshscene_scene_publish" -I "$dir/include"
expect_fail "$dir/tests/types/edit_as_scene.c" "incompatible-pointer-types\|incompatible pointer\|passing argument" -I "$dir/include"

# The gate proves itself: collapse the capability and the probe that was refused compiles.
mkdir -p "$work/collapsed" "$work/leaky"
cp -r "$dir/include" "$work/collapsed/include"
cp -r "$dir/include" "$work/leaky/include"
python3 - "$work/collapsed/include/ghoti.io/shape-scene/types.h" <<'PY'
import sys
p = sys.argv[1]
s = open(p).read()
old = "typedef struct GSHSCENE_Edit GSHSCENE_Edit;"
assert s.count(old) == 1
open(p, "w").write(s.replace(old, "typedef struct GSHSCENE_Scene GSHSCENE_Edit;"))
PY
python3 - "$work/leaky/include/ghoti.io/shape-scene/edit.h" <<'PY'
import sys
p = sys.argv[1]
s = open(p).read()
old = "#include <ghoti.io/shape-scene/version.h>\n"
assert s.count(old) == 1
open(p, "w").write(s.replace(old, old + "#include <ghoti.io/shape-scene/scene.h>\n"))
PY
out="$(compile "$dir/tests/types/edit_as_scene.c" -I "$work/collapsed/include")"
if [ $? -ne 0 ]; then
  printf 'check-publish-types: FAIL: with GSHSCENE_Edit collapsed into GSHSCENE_Scene the probe still does not compile, so it was not the type that refused it:\n%s\n' "$out" >&2
  rc=1
fi
out="$(compile "$dir/tests/types/no_publish.c" -I "$work/leaky/include")"
if [ $? -ne 0 ]; then
  printf 'check-publish-types: FAIL: with scene.h reachable from edit.h the probe still does not compile, so it was not the headers that refused it:\n%s\n' "$out" >&2
  rc=1
fi

if [ $rc -eq 0 ]; then
  printf 'check-publish-types: ok: a scene publishes; edit.h offers no publish; an edit cannot stand for the scene; both violations compile when planted\n'
fi
exit $rc
