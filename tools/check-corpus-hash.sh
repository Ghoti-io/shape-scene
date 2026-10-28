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
# the stroke section's (strokes story S1), a hash of its own, and whose `plane-hash` and `plane-count` lines are the
# plane section's (color-paint story P4), another, and whose `placeholder-hash` and `placeholder-count` lines are the
# placeholder section's, a fourth, and whose `image-hash` and `image-count` lines are the image section's (masks-images-patterns
# story I1), a fifth.  No value may be empty, so a probe that crashed, or a
# hash file that was emptied, fails rather than comparing nothing with nothing.
probe="${1:?usage: check-corpus-hash.sh PROBE HASH_FILE}"
file="${2:?usage: check-corpus-hash.sh PROBE HASH_FILE}"

got="$("$probe")" || { printf 'check-corpus-hash: the probe failed\n' >&2; exit 1; }
got_hash=$(printf '%s\n' "$got" | awk '$1=="shape-scene.hash"{print $2}')
got_count=$(printf '%s\n' "$got" | awk '$1=="shape-scene.count"{print $2}')
got_shash=$(printf '%s\n' "$got" | awk '$1=="shape-scene.stroke-hash"{print $2}')
got_scount=$(printf '%s\n' "$got" | awk '$1=="shape-scene.stroke-count"{print $2}')
got_phash=$(printf '%s\n' "$got" | awk '$1=="shape-scene.plane-hash"{print $2}')
got_pcount=$(printf '%s\n' "$got" | awk '$1=="shape-scene.plane-count"{print $2}')
want_phash=$(awk '$1=="plane-hash"{print $2}' "$file" 2>/dev/null)
want_pcount=$(awk '$1=="plane-count"{print $2}' "$file" 2>/dev/null)
got_hhash=$(printf '%s\n' "$got" | awk '$1=="shape-scene.placeholder-hash"{print $2}')
got_hcount=$(printf '%s\n' "$got" | awk '$1=="shape-scene.placeholder-count"{print $2}')
want_hhash=$(awk '$1=="placeholder-hash"{print $2}' "$file" 2>/dev/null)
want_hcount=$(awk '$1=="placeholder-count"{print $2}' "$file" 2>/dev/null)
got_ihash=$(printf '%s\n' "$got" | awk '$1=="shape-scene.image-hash"{print $2}')
got_icount=$(printf '%s\n' "$got" | awk '$1=="shape-scene.image-count"{print $2}')
want_ihash=$(awk '$1=="image-hash"{print $2}' "$file" 2>/dev/null)
want_icount=$(awk '$1=="image-count"{print $2}' "$file" 2>/dev/null)
want_hash=$(awk '$1=="hash"{print $2}' "$file" 2>/dev/null)
want_count=$(awk '$1=="count"{print $2}' "$file" 2>/dev/null)
want_shash=$(awk '$1=="stroke-hash"{print $2}' "$file" 2>/dev/null)
want_scount=$(awk '$1=="stroke-count"{print $2}' "$file" 2>/dev/null)
if [ -z "$want_hash" ] || [ -z "$want_count" ] || [ -z "$got_hash" ] || [ -z "$got_count" ] ||
   [ -z "$want_shash" ] || [ -z "$want_scount" ] || [ -z "$got_shash" ] || [ -z "$got_scount" ] ||
   [ -z "$want_phash" ] || [ -z "$want_pcount" ] || [ -z "$got_phash" ] || [ -z "$got_pcount" ] ||
   [ -z "$want_hhash" ] || [ -z "$want_hcount" ] || [ -z "$got_hhash" ] || [ -z "$got_hcount" ] ||
   [ -z "$want_ihash" ] || [ -z "$want_icount" ] || [ -z "$got_ihash" ] || [ -z "$got_icount" ]; then
  printf 'check-corpus-hash: nothing to compare (probe %s/%s, strokes %s/%s, planes %s/%s, placeholders %s/%s and images %s/%s; committed %s/%s, %s/%s, %s/%s, %s/%s and %s/%s in %s)\n' \
    "$got_hash" "$got_count" "$got_shash" "$got_scount" "$got_phash" "$got_pcount" "$got_hhash" "$got_hcount" "$got_ihash" "$got_icount" \
    "$want_hash" "$want_count" "$want_shash" "$want_scount" "$want_phash" "$want_pcount" "$want_hhash" "$want_hcount" "$want_ihash" "$want_icount" "$file" >&2
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
if [ "$got_phash" != "$want_phash" ] || [ "$got_pcount" != "$want_pcount" ]; then
  printf 'check-corpus-hash: FAIL: planes %s over %s results, committed %s over %s\n' \
    "$got_phash" "$got_pcount" "$want_phash" "$want_pcount" >&2
  exit 1
fi
if [ "$got_hhash" != "$want_hhash" ] || [ "$got_hcount" != "$want_hcount" ]; then
  printf 'check-corpus-hash: FAIL: placeholders %s over %s results, committed %s over %s\n' \
    "$got_hhash" "$got_hcount" "$want_hhash" "$want_hcount" >&2
  exit 1
fi
if [ "$got_ihash" != "$want_ihash" ] || [ "$got_icount" != "$want_icount" ]; then
  printf 'check-corpus-hash: FAIL: images %s over %s results, committed %s over %s\n' \
    "$got_ihash" "$got_icount" "$want_ihash" "$want_icount" >&2
  exit 1
fi
printf 'check-corpus-hash: ok, %s over %s results; strokes %s over %s; planes %s over %s; placeholders %s over %s; images %s over %s\n' "$got_hash" "$got_count" "$got_shash" "$got_scount" "$got_phash" "$got_pcount" "$got_hhash" "$got_hcount" "$got_ihash" "$got_icount"
