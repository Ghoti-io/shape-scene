#!/bin/sh
# SPDX-License-Identifier: LGPL-3.0-only
#
# Fail unless the hash a probe prints is the committed one (CAP-13).
#
# Usage: check-corpus-hash.sh PROBE HASH_FILE
#
# PROBE is tools/corpus-hash.c built for this host (or a planted variant);
# HASH_FILE is tests/corpus/scene.hash, whose `hash` and `count` lines are
# the committed values, and whose `stroke-hash` and `stroke-count` lines are
# the stroke section's (strokes story S1), a hash of its own.  No value may be empty, so a probe that crashed, or a
# hash file that was emptied, fails rather than comparing nothing with nothing.
probe="${1:?usage: check-corpus-hash.sh PROBE HASH_FILE}"
file="${2:?usage: check-corpus-hash.sh PROBE HASH_FILE}"

got="$("$probe")" || { printf 'check-corpus-hash: the probe failed\n' >&2; exit 1; }
got_hash=$(printf '%s\n' "$got" | awk '$1=="shape-scene.hash"{print $2}')
got_count=$(printf '%s\n' "$got" | awk '$1=="shape-scene.count"{print $2}')
got_shash=$(printf '%s\n' "$got" | awk '$1=="shape-scene.stroke-hash"{print $2}')
got_scount=$(printf '%s\n' "$got" | awk '$1=="shape-scene.stroke-count"{print $2}')
want_hash=$(awk '$1=="hash"{print $2}' "$file" 2>/dev/null)
want_count=$(awk '$1=="count"{print $2}' "$file" 2>/dev/null)
want_shash=$(awk '$1=="stroke-hash"{print $2}' "$file" 2>/dev/null)
want_scount=$(awk '$1=="stroke-count"{print $2}' "$file" 2>/dev/null)
if [ -z "$want_hash" ] || [ -z "$want_count" ] || [ -z "$got_hash" ] || [ -z "$got_count" ] ||
   [ -z "$want_shash" ] || [ -z "$want_scount" ] || [ -z "$got_shash" ] || [ -z "$got_scount" ]; then
  printf 'check-corpus-hash: nothing to compare (probe %s/%s and strokes %s/%s, committed %s/%s and %s/%s in %s)\n' \
    "$got_hash" "$got_count" "$got_shash" "$got_scount" "$want_hash" "$want_count" "$want_shash" "$want_scount" "$file" >&2
  exit 1
fi
if [ "$got_hash" != "$want_hash" ] || [ "$got_count" != "$want_count" ]; then
  printf 'check-corpus-hash: FAIL: %s over %s results, committed %s over %s\n' \
    "$got_hash" "$got_count" "$want_hash" "$want_count" >&2
  exit 1
fi
if [ "$got_shash" != "$want_shash" ] || [ "$got_scount" != "$want_scount" ]; then
  printf 'check-corpus-hash: FAIL: strokes %s over %s results, committed %s over %s\n' \
    "$got_shash" "$got_scount" "$want_shash" "$want_scount" >&2
  exit 1
fi
printf 'check-corpus-hash: ok, %s over %s results; strokes %s over %s\n' "$got_hash" "$got_count" "$got_shash" "$got_scount"
