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
"""Show that the tests fail on planted defects (CAP-16, CAP-13).

A test that has never been seen to fail may measure nothing.  For each defect
below this builds the library's sources with one literal replacement applied
(the text must match exactly once and the replacement must change it), links
the tests that are meant to catch it against the result, and requires one of
them to fail.  The unplanted build is the control: every test must pass on it,
which is what makes a failure mean the defect and not the build.

A plant is a replacement in a source file (``src``) or in a public header
(``hdr``: the whole library is rebuilt with the changed header ahead of the
real one on the include path; the tests keep the real one, which is how a
changed constant disagrees with its pinned value).  The cross-target probe's
own plant is a flipped bit in one result.  Ids by file:

  core-, hdr-  the result strings and their order, the limits' defaults and the
               pinned values of every enumeration
  st-          the store: the table (its starting size, its growth, the masks,
               the chains), the lookup (index, kind, equality, the counters),
               the intern (the injectable hash, the double lookup, the limit,
               the ids, the links), the counts (retain, release, the store's
               own), the release listeners (add, remove, the copy-on-write
               array and its count), every accessor's NULL answer
  rs-          the resources: the hash spelling field by field (width, order,
               kind), path equality number by number, glyph-run equality field
               by field, the copy of a path and of a run and of a picture,
               every validity rule of a glyph run (each comparison of each
               bound), the picture's limits and depth, the accessors, the dump
               text and its widths, the stream error flag
  ls-          the list: the arena (the bump, the chunks, the limit, the first
               chunk a reset keeps, the big chunks), the segments and the
               addressing of an item, the effective limits, the append (each
               limit, the counts, the retained reference), the validation of
               every argument of every record call, the push stack, the pops
               and close, equality field by field, the hash spelling field by
               field, the dump text and its widths

  nd-, ed-, sc-, vs-, wk-, va-, em-, df-, bs-, br-, bl-, bc-
               the retained scene, generated rather than written: every
               comparison, condition, statement, hash width, output letter
               and returned status of src/scene and of the store's, resources'
               and list's byte and limit code, mutated once (tools/gen-scene-plants.py
               makes the candidates; tools/scene-plants.json holds the ones
               the tests were seen to catch).  A few hand-written plants for
               the constants stand beside them (VERSION_*, RANGE_LIMIT, the
               byte constants, the problem reasons)

What is NOT planted, and why.  A defect that no input can reach, or that
changes no result, cannot be seen by any test:

  - the INITIAL_BUCKETS and growth *thresholds* are planted (the tests pin 64,
    128 and 512); the table's load factor is only speed
  - ``try_retain``'s refusal to increment a count of zero: a resurrected resource
    needs two threads in a window of a few instructions; the thread tests run it
    under ASan and TSan (the final matrix), where it is a use after free, but a
    plant that needs a race to show is not a gate (the same is true of the
    release-listener array's count being taken before the lock is dropped)
  - the glyph source term of ``run_equal``: an outline run has to have a zero
    font key and a font run a nonzero one, so equal runs of different sources
    cannot exist; the term is the second line of a defence the validation makes
    the first
  - the first point of each segment in ``gshscene_path_equal`` (``k = 1``): it is
    the end of the segment before, which was compared already
  - ``ARENA_ALIGN``, the rounding of an arena request and ``CHUNK_PAYLOAD``: every
    request the library makes is a multiple of 16 (segments are 16 << k items of
    a size that is a multiple of 8, stacks are 16 << k bytes), so the rounding
    never changes a size, and a chunk's size is a policy that changes the count
    of allocator calls and no result; an unaligned item is seen only by UBSan
  - the guard that ``SIZE_MAX - ARENA_ALIGN`` is not passed, the segment-count
    and segment-size overflow guards, and the 2^30 ceiling of ``max_list_items``:
    reaching them takes 2^30 items or a ``size_t`` of 32 bits and a request of
    2 GiB; the tests assert instead that a limit of ``SIZE_MAX`` is accepted
  - the mutex's initialisation, which is cutil's and cannot fail (``GCU_MUTEX_INIT``)
  - the checks of each ``fprintf`` and ``fputc`` of a dump, one by one: the
    final ``ferror`` of the stream makes each of them redundant; the sweep of
    failing writes (``Dump.EveryWriteOfEveryDumpIsChecked``) is what holds the
    final check (planted), and it found that an unbuffered stream's ``fprintf``
    loses the failure's return value
  - the three terms of ``item_equal`` over a paint that today is always solid
    with color space 0 and no gradient, and the blend space and ordering of a
    group (always 0), and the swap of those two in the dump: story 12 gives
    them values and a test then
  - the ``kind`` term of ``item_equal``: two lists can differ only in the kind
    of an item where the item's other fields differ too, except for a clip pop
    against a group pop, and those cannot stand at the same place in lists that
    are balanced and equal before it
  - checks that a later one makes redundant: the finiteness of a glyph run's size
    (a NaN fails ``size > 0`` and an infinity fails the coordinate limit), of a
    group's opacity and of a paint's alpha (the range test fails a NaN), a NULL
    store or NULL list given to ``intern_picture`` (``list->store != store`` and
    ``is_closed(NULL)`` refuse them), a pop on a closed list (a list closes only
    at depth 0, so ``depth == 0`` refuses it), and the check of the path's
    own dump in ``gshscene_resource_dump`` (the sticky error flag, which the final test reads)
  - of the generated candidates, the ones that no test fails on, by kind:
    capacity and overflow guards that need 2^30 items; comparator ties between
    distinct keys (the order is total, so a tie cannot occur); the internals of
    the open-addressing indexes (a wrong probe step still finds the entry, only
    slower); the lines that free, set a pointer to NULL after freeing, or
    lock, memset and destroy (ASan or a race sees them, a result does not);
    the out-of-memory and ``GSHSCENE_ERR_INTERNAL`` arms; the per-call return
    checks of a dump that the final ``ferror`` makes redundant; ``qsort`` of an
    input that is already ordered; the depth memo, which the level guard makes
    redundant; the checks of an expansion that the check of its root dominates;
    and the equality at ``RANGE_LIMIT``, which no double lies on.  A candidate
    that does not compile is dropped rather than counted
  - the values of ``GSHSCENE_GLYPHS_FROM_OUTLINES`` and of the depth ordering
    context, and the ceiling constant: the library treats every other value as
    outlines or as unsupported alike, and the ceiling is 2^30 items
  - the growth policy of the push stack and of the arena's chunks (the size a
    stack grows to, its capacity bookkeeping, ``CHUNK_PAYLOAD``): a stack that
    grows too little writes into arena memory that is still free, which only a
    sanitizer that knew the arena would see
  - the numeric (instead of bitwise) comparison of a weight or of a font run's
    size: a weight is positive and a size is positive for a font run and +0.0 for
    an outline run, so the two comparisons cannot differ on a valid value; and
    the count of points a close compares, and the weights of a non-rational verb:
    a close's line is derived from the subpath, and a non-rational curve's weights
    are the same unused value in both
  - the ``kind`` argument of a MOVE in ``replay`` (``p[0]`` and ``p[1]`` of a
    move are the same point)
  - the ``fill_rule``, paint-kind and ``memset`` of ``item_init``: each is the
    zero the ``memset`` wrote already (a nonzero rule, kind or space is set
    afterwards by the caller)
"""

import concurrent.futures
import os
import re
import shlex
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def env(name, default=None):
    value = os.environ.get(name, default)
    if value is None:
        sys.exit("check-planted: %s is not set (run it with `make check-planted`)" % name)
    return value


CC = shlex.split(env("PLANT_CC"))
CFLAGS = shlex.split(env("PLANT_CFLAGS"))
PROBE_CFLAGS = shlex.split(env("PLANT_PROBE_CFLAGS"))
PROBE_LIBS = env("PLANT_PROBE_LIBS")
CXX = env("PLANT_CXX")
CXX_INCLUDE = env("PLANT_CXX_INCLUDE")
LDFLAGS = env("PLANT_LDFLAGS")
TESTS = env("PLANT_TESTS").split()
OBJ_DIR = Path(env("PLANT_OBJ_DIR"))
LD_PATH = env("PLANT_LD_PATH")
WORK = Path(env("PLANT_WORK"))
JOBS = int(env("PLANT_JOBS", "4"))
# PLANT_ONLY=<regex> runs only the plants whose id matches (and not the probe's
# own plants): for re-running the few that a change to a test touched.  The
# control always runs.  Unset, every plant runs, which is what `make test` wants.
ONLY = os.environ.get("PLANT_ONLY")
# PLANT_PROBE_ONLY=1: the control and the probe's own plants, no source plant
# (for re-running the probe's gate after a change to the probe alone).
PROBE_ONLY = bool(os.environ.get("PLANT_PROBE_ONLY"))


def test_object(test):
    """testCurve -> build/.../tests/test_curve.o"""
    snake = re.sub(r"(?<!^)(?=[A-Z])", "_", test[len("test"):]).lower()
    return OBJ_DIR / "tests" / ("test_%s.o" % snake)


def sources():
    return sorted(str(p.relative_to(ROOT)) for p in (ROOT / "src").rglob("*.c"))



# --- the plants -------------------------------------------------------------
# (id, kind, file, old, new, tests that must catch it)
# kind: "src" or "hdr" (the whole library is rebuilt with the changed header
# ahead of the real one on the include path; the tests keep the real one, which
# is how a changed constant disagrees with its pinned value).

CO = "src/core/core.c"
ST = "src/store/store.c"
RS = "src/resource/resource.c"
LS = "src/list/list.c"
HC = "include/ghoti.io/shape-scene/core.h"
HR = "include/ghoti.io/shape-scene/resource.h"
HL = "include/ghoti.io/shape-scene/list.h"
HP = "include/ghoti.io/shape-scene/paint.h"

C = ["testCore"]
S = ["testStore"]
R = ["testResource"]
L = ["testList"]
D = ["testDump"]

PLANTS = [
    ('core-limits-default-null', 'src', CO, 'void gshscene_limits_default(GSHSCENE_Limits * limits) {\n  if (!limits) {\n    return;\n  }', 'void gshscene_limits_default(GSHSCENE_Limits * limits) {', C),
    ('core-default-resources', 'src', CO, 'limits->max_resources = GSHSCENE_LIMITS_DEFAULT_MAX_RESOURCES;', 'limits->max_resources = GSHSCENE_LIMITS_DEFAULT_MAX_RESOURCES + 1;', C),
    ('core-default-elements', 'src', CO, 'limits->max_resource_elements = GSHSCENE_LIMITS_DEFAULT_MAX_RESOURCE_ELEMENTS;', 'limits->max_resource_elements = GSHSCENE_LIMITS_DEFAULT_MAX_RESOURCE_ELEMENTS + 1;', C),
    ('core-default-items', 'src', CO, 'limits->max_list_items = GSHSCENE_LIMITS_DEFAULT_MAX_LIST_ITEMS;', 'limits->max_list_items = GSHSCENE_LIMITS_DEFAULT_MAX_LIST_ITEMS + 1;', C),
    ('core-default-bytes', 'src', CO, 'limits->max_list_bytes = GSHSCENE_LIMITS_DEFAULT_MAX_LIST_BYTES;', 'limits->max_list_bytes = GSHSCENE_LIMITS_DEFAULT_MAX_LIST_BYTES + 1;', C),
    ('core-default-depth', 'src', CO, 'limits->max_depth = GSHSCENE_LIMITS_DEFAULT_MAX_DEPTH;', 'limits->max_depth = GSHSCENE_LIMITS_DEFAULT_MAX_DEPTH + 1;', C),
    ('core-default-picture-depth', 'src', CO, 'limits->max_picture_depth = GSHSCENE_LIMITS_DEFAULT_MAX_PICTURE_DEPTH;', 'limits->max_picture_depth = GSHSCENE_LIMITS_DEFAULT_MAX_PICTURE_DEPTH + 1;', C),
    ('core-default-expansion', 'src', CO, 'limits->max_expansion = GSHSCENE_LIMITS_DEFAULT_MAX_EXPANSION;', 'limits->max_expansion = GSHSCENE_LIMITS_DEFAULT_MAX_EXPANSION + 1;', C),
    ('core-default-reserved', 'src', CO, '  for (size_t i = 0; i < sizeof limits->_reserved; i++) {\n    limits->_reserved[i] = 0;\n  }', '', C),
    ('core-allocator-default-null', 'src', CO, '  return gshgeom_allocator_default();', '  return NULL;', C),
    ('core-string-ok-empty', 'src', CO, '      return "ok";', '      return "";', C),
    ('core-string-io-duplicate', 'src', CO, '      return "I/O error";', '      return "format error";', C),
    ('core-string-limit-duplicate', 'src', CO, '      return "limit exceeded";', '      return "out of memory";', C),
    ('core-string-unknown-null', 'src', CO, '  return "unknown result";', '  return NULL;', C),
    ('core-string-unknown-for-count', 'src', CO, '    case GSHSCENE_RESULT_COUNT:\n      break;\n  }\n  return "unknown result";', '    case GSHSCENE_RESULT_COUNT:\n      return "count";\n  }\n  return "unknown result";', C),
    ('hdr-result-order', 'hdr', HC, "  GSHSCENE_ERR_IO,          ///< A write to a caller's stream failed.\n  GSHSCENE_ERR_FORMAT,      ///< Not returned by this library.", "  GSHSCENE_ERR_FORMAT,      ///< Not returned by this library.\n  GSHSCENE_ERR_IO,          ///< A write to a caller's stream failed.", C),
    ('hdr-default-resources', 'hdr', HC, '#define GSHSCENE_LIMITS_DEFAULT_MAX_RESOURCES ((size_t)1 << 24)', '#define GSHSCENE_LIMITS_DEFAULT_MAX_RESOURCES ((size_t)1 << 23)', C),
    ('hdr-default-elements', 'hdr', HC, '#define GSHSCENE_LIMITS_DEFAULT_MAX_RESOURCE_ELEMENTS ((size_t)1 << 22)', '#define GSHSCENE_LIMITS_DEFAULT_MAX_RESOURCE_ELEMENTS ((size_t)1 << 21)', C),
    ('hdr-default-items', 'hdr', HC, '#define GSHSCENE_LIMITS_DEFAULT_MAX_LIST_ITEMS ((size_t)1 << 20)', '#define GSHSCENE_LIMITS_DEFAULT_MAX_LIST_ITEMS ((size_t)1 << 19)', C),
    ('hdr-default-bytes', 'hdr', HC, '#define GSHSCENE_LIMITS_DEFAULT_MAX_LIST_BYTES ((size_t)256 * 1024 * 1024)', '#define GSHSCENE_LIMITS_DEFAULT_MAX_LIST_BYTES ((size_t)128 * 1024 * 1024)', C),
    ('hdr-default-depth', 'hdr', HC, '#define GSHSCENE_LIMITS_DEFAULT_MAX_DEPTH ((size_t)256)', '#define GSHSCENE_LIMITS_DEFAULT_MAX_DEPTH ((size_t)255)', C),
    ('hdr-default-picture-depth', 'hdr', HC, '#define GSHSCENE_LIMITS_DEFAULT_MAX_PICTURE_DEPTH ((size_t)32)', '#define GSHSCENE_LIMITS_DEFAULT_MAX_PICTURE_DEPTH ((size_t)31)', C),
    ('hdr-default-expansion', 'hdr', HC, '#define GSHSCENE_LIMITS_DEFAULT_MAX_EXPANSION ((size_t)1 << 24)', '#define GSHSCENE_LIMITS_DEFAULT_MAX_EXPANSION ((size_t)1 << 23)', C),
    ('st-initial-buckets', 'src', ST, '#define INITIAL_BUCKETS 64u', '#define INITIAL_BUCKETS 32u', S),
    ('st-listeners-unref-keeps', 'src', ST, 'if (listeners && gcu_atomic_size_fetch_sub(&listeners->refs, 1) == 1) {', 'if (listeners && gcu_atomic_size_fetch_sub(&listeners->refs, 1) == 2) {', S),
    ('st-store-unref-last', 'src', ST, '  if (gcu_atomic_size_fetch_sub(&store->refs, 1) != 1) {\n    return;\n  }\n  GSHSCENE_Allocator alloc = store->alloc;', '  if (gcu_atomic_size_fetch_sub(&store->refs, 1) != 0) {\n    return;\n  }\n  GSHSCENE_Allocator alloc = store->alloc;', S),
    ('st-store-unref-keeps-buckets', 'src', ST, '  gshscene_free(&alloc, store->buckets);\n  GCU_MUTEX_DESTROY(store->lock);', '  GCU_MUTEX_DESTROY(store->lock);', S),
    ('st-store-unref-keeps-listeners', 'src', ST, '  listeners_unref(&alloc, store->listeners);\n  gshscene_free(&alloc, store->buckets);', '  gshscene_free(&alloc, store->buckets);', S),
    ('st-store-unref-keeps-store', 'src', ST, '  GCU_MUTEX_DESTROY(store->lock);\n  gshscene_free(&alloc, store);', '  GCU_MUTEX_DESTROY(store->lock);', S),
    ('st-create-null-out', 'src', ST, '  if (!out) {\n    return GSHSCENE_ERR_INVALID;\n  }\n  if (!alloc) {', '  if (!alloc) {', S),
    ('st-create-default-alloc', 'src', ST, '  if (!alloc) {\n    alloc = gshscene_allocator_default();\n  }\n  GSHSCENE_Store * store', '  GSHSCENE_Store * store', S),
    ('st-create-store-oom', 'src', ST, '  if (!store) {\n    return GSHSCENE_ERR_OOM;\n  }\n  store->alloc = *alloc;', '  store->alloc = *alloc;', S),
    ('st-create-buckets-oom-leaks-store', 'src', ST, '  if (!store->buckets) {\n    gshscene_free(&store->alloc, store);\n    return GSHSCENE_ERR_OOM;\n  }', '  if (!store->buckets) {\n    return GSHSCENE_ERR_OOM;\n  }', S),
    ('st-create-ignores-limits', 'src', ST, '  gshscene_limits_effective(limits, &store->limits);', '  (void)limits;\n  gshscene_limits_default(&store->limits);', S + R),
    ('st-create-index-fn', 'src', ST, '  store->index_fn = index_hash;\n', '  (void)index_hash;\n', S),
    ('st-create-index-ctx', 'src', ST, '  store->index_ctx = index_ctx;\n', '  (void)index_ctx;\n', S),
    ('st-create-refs-two', 'src', ST, '  gcu_atomic_size_init(&store->refs, 1);', '  gcu_atomic_size_init(&store->refs, 2);', S),
    ('st-create-bucket-count', 'src', ST, '  store->bucket_count = INITIAL_BUCKETS;\n', '  store->bucket_count = INITIAL_BUCKETS / 2;\n', S),
    ('st-destroy-null', 'src', ST, '  if (store) {\n    gshscene_store_unref(store);\n  }', '  gshscene_store_unref(store);', S),
    ('st-stats-null-checks', 'src', ST, '  if (!store || !out) {\n    return GSHSCENE_ERR_INVALID;\n  }\n  GSHSCENE_Store * mutable_store', '  GSHSCENE_Store * mutable_store', S),
    ('st-stats-live', 'src', ST, '  out->live = store->live;', '  out->live = 0;', S),
    ('st-stats-created', 'src', ST, '  out->created = store->next_id;', '  out->created = store->live;', S),
    ('st-stats-hits', 'src', ST, '  out->hits = store->hits;', '  out->hits = 0;', S),
    ('st-stats-collisions', 'src', ST, '  out->collisions = store->collisions;', '  out->collisions = 0;', S),
    ('st-stats-buckets', 'src', ST, '  out->buckets = store->bucket_count;', '  out->buckets = 0;', S),
    ('st-new-resource-no-kind', 'src', ST, '  resource->kind = kind;\n', '  (void)kind;\n', S + R),
    ('st-new-resource-refs-two', 'src', ST, '  gcu_atomic_size_init(&resource->refs, 1);\n  gshscene_store_ref(store);', '  gcu_atomic_size_init(&resource->refs, 2);\n  gshscene_store_ref(store);', S),
    ('st-new-resource-no-store-ref', 'src', ST, '  gcu_atomic_size_init(&resource->refs, 1);\n  gshscene_store_ref(store);', '  gcu_atomic_size_init(&resource->refs, 1);', S),
    ('st-new-resource-no-store-link', 'src', ST, '  resource->store = store;\n', '', S),
    ('st-discard-no-unref', 'src', ST, '  gshscene_free(&alloc, resource);\n  gshscene_store_unref(store);', '  gshscene_free(&alloc, resource);', S),
    ('st-discard-no-free', 'src', ST, '  gshscene_resource_free_content(resource);\n  gshscene_free(&alloc, resource);', '  gshscene_resource_free_content(resource);\n  (void)alloc;', S),
    ('st-discard-no-content', 'src', ST, '  gshscene_resource_free_content(resource);\n  gshscene_free(&alloc, resource);', '  gshscene_free(&alloc, resource);', S + R),
    ('st-retain-count-not-added', 'src', ST, '(&resource->refs, &count, count + 1)) {', '(&resource->refs, &count, count)) {', S),
    ('st-lookup-no-index-filter', 'src', ST, '    if (r->index != index) {\n      continue;\n    }\n', '', S),
    ('st-lookup-kind-or', 'src', ST, 'if (r->kind == probe->kind && gshscene_resource_matches(r, probe)) {', 'if (r->kind == probe->kind || gshscene_resource_matches(r, probe)) {', S + R),
    ('st-lookup-kind-only', 'src', ST, 'if (r->kind == probe->kind && gshscene_resource_matches(r, probe)) {', 'if (r->kind == probe->kind) {', S + R),
    ('st-lookup-always-equal', 'src', ST, 'if (r->kind == probe->kind && gshscene_resource_matches(r, probe)) {', 'if ((void)probe, 1) {', S + R),
    ('st-lookup-collision-uncounted', 'src', ST, '      store->collisions++;', '      store->collisions += 0;', S),
    ('st-lookup-hit-not-retained', 'src', ST, '      if (try_retain(r)) {\n        return r;\n      }', '      return r;', S),
    ('st-lookup-wrong-mask', 'src', ST, 'for (GSHSCENE_Resource * r = store->buckets[index & (store->bucket_count - 1)]; r; r = r->next) {', 'for (GSHSCENE_Resource * r = store->buckets[index & (store->bucket_count - 2)]; r; r = r->next) {', S),
    ('st-grow-factor', 'src', ST, '  size_t count = store->bucket_count * 2;', '  size_t count = store->bucket_count * 4;', S),
    ('st-grow-oom-unchecked', 'src', ST, '  if (!buckets) {\n    return;\n  }\n  for (size_t i = 0;', '  for (size_t i = 0;', S),
    ('st-grow-mask', 'src', ST, '      size_t slot = r->index & (count - 1);', '      size_t slot = r->index & (count - 2);', S),
    ('st-grow-count-not-stored', 'src', ST, '  store->buckets = buckets;\n  store->bucket_count = count;', '  store->buckets = buckets;', S),
    ('st-grow-old-table-leaks', 'src', ST, '  gshscene_free(&store->alloc, store->buckets);\n  store->buckets = buckets;', '  store->buckets = buckets;', S),
    ('st-grow-rehash-drops-chain', 'src', ST, '      r->next = buckets[slot];\n      buckets[slot] = r;', '      r->next = NULL;\n      buckets[slot] = r;', S),
    ('st-intern-no-index-hook', 'src', ST, '  uint64_t index = store->index_fn ? store->index_fn(probe->hash, store->index_ctx) : probe->hash;', '  uint64_t index = probe->hash;', S),
    ('st-intern-hook-no-ctx', 'src', ST, 'store->index_fn(probe->hash, store->index_ctx) : probe->hash;', 'store->index_fn(probe->hash, NULL) : probe->hash;', S),
    ('st-intern-first-hit-uncounted', 'src', ST, '  if (found) {\n    store->hits++;\n    GCU_MUTEX_UNLOCK(store->lock);\n    *out = found;', '  if (found) {\n    GCU_MUTEX_UNLOCK(store->lock);\n    *out = found;', S),
    ('st-intern-second-hit-uncounted', 'src', ST, '    store->hits++;\n    GCU_MUTEX_UNLOCK(store->lock);\n    gshscene_store_discard_resource(built);\n    *out = found;', '    GCU_MUTEX_UNLOCK(store->lock);\n    gshscene_store_discard_resource(built);\n    *out = found;', S),
    ('st-intern-no-second-lookup', 'src', ST, '  found = lookup(store, probe, index);\n  if (found) {\n    store->hits++;\n    GCU_MUTEX_UNLOCK(store->lock);\n    gshscene_store_discard_resource(built);', '  found = NULL;\n  if (found) {\n    store->hits++;\n    GCU_MUTEX_UNLOCK(store->lock);\n    gshscene_store_discard_resource(built);', S),
    ('st-intern-second-hit-leaks-built', 'src', ST, '    GCU_MUTEX_UNLOCK(store->lock);\n    gshscene_store_discard_resource(built);\n    *out = found;', '    GCU_MUTEX_UNLOCK(store->lock);\n    *out = found;', S),
    ('st-intern-limit-inclusive', 'src', ST, '  if (store->live >= store->limits.max_resources || !fits(store, probe->bytes)) {', '  if (store->live > store->limits.max_resources || !fits(store, probe->bytes)) {', S),
    ('st-intern-limit-ignored', 'src', ST, '  if (store->live >= store->limits.max_resources || !fits(store, probe->bytes)) {', '  if (0 || !fits(store, probe->bytes)) {', S),
    ('st-intern-limit-leaks-built', 'src', ST, '    GCU_MUTEX_UNLOCK(store->lock);\n    gshscene_store_discard_resource(built);\n    return GSHSCENE_ERR_LIMIT;', '    GCU_MUTEX_UNLOCK(store->lock);\n    return GSHSCENE_ERR_LIMIT;', S),
    ('st-intern-limit-wrong-code', 'src', ST, '    gshscene_store_discard_resource(built);\n    return GSHSCENE_ERR_LIMIT;', '    gshscene_store_discard_resource(built);\n    return GSHSCENE_ERR_OOM;', S),
    ('st-intern-id-from-zero', 'src', ST, '  built->id = ++store->next_id;', '  built->id = store->next_id++;', S),
    ('st-intern-no-link', 'src', ST, '  built->next = store->buckets[slot];\n', '', S),
    ('st-intern-no-live', 'src', ST, '  store->live++;\n', '', S),
    ('st-intern-grow-eager', 'src', ST, '  if (store->live > store->bucket_count) {\n    grow(store);\n  }', '  if (store->live >= store->bucket_count) {\n    grow(store);\n  }', S),
    ('st-intern-never-grows', 'src', ST, '  if (store->live > store->bucket_count) {\n    grow(store);\n  }', '', S),
    ('st-intern-build-error-ignored', 'src', ST, '  if (result != GSHSCENE_OK) {\n    return result;\n  }\n  built->hash = probe->hash;', '  (void)result;\n  built->hash = probe->hash;', S + R),
    ('st-intern-no-hash-store', 'src', ST, '  built->hash = probe->hash;\n', '', S + D),
    ('st-intern-no-index-store', 'src', ST, '  built->index = index;\n', '', S),
    ('st-retain-null-unchecked', 'src', ST, '  if (resource) {\n    gcu_atomic_size_fetch_add(&((GSHSCENE_Resource *)resource)->refs, 1);\n  }', '  gcu_atomic_size_fetch_add(&((GSHSCENE_Resource *)resource)->refs, 1);', S),
    ('st-release-null-unchecked', 'src', ST, '  if (!handle) {\n    return;\n  }\n  GSHSCENE_Resource * resource', '  GSHSCENE_Resource * resource', S),
    ('st-release-never-last', 'src', ST, '  if (gcu_atomic_size_fetch_sub(&resource->refs, 1) != 1) {', '  if (gcu_atomic_size_fetch_sub(&resource->refs, 1) != 0) {', S),
    ('st-release-unlinks-head', 'src', ST, '  while (*link != resource) {\n    link = &(*link)->next;\n  }', '', S),
    ('st-release-drops-chain', 'src', ST, '  *link = resource->next;\n  store->live--;', '  *link = NULL;\n  store->live--;', S),
    ('st-release-no-live-dec', 'src', ST, '  store->live--;\n', '', S),
    ('st-release-listeners-uncounted', 'src', ST, '  if (listeners) {\n    gcu_atomic_size_fetch_add(&listeners->refs, 1);\n  }\n  GCU_MUTEX_UNLOCK(store->lock);', '  GCU_MUTEX_UNLOCK(store->lock);', S),
    ('st-release-listener-ctx', 'src', ST, 'listeners->items[i].fn(listeners->items[i].ctx, resource->id, resource);', 'listeners->items[i].fn(NULL, resource->id, resource);', S),
    ('st-release-listener-id', 'src', ST, 'listeners->items[i].fn(listeners->items[i].ctx, resource->id, resource);', 'listeners->items[i].fn(listeners->items[i].ctx, resource->index, resource);', S),
    ('st-release-listeners-leak', 'src', ST, '    listeners_unref(&alloc, listeners);\n  }\n  gshscene_store_discard_resource(resource);', '  }\n  (void)alloc;\n  gshscene_store_discard_resource(resource);', S),
    ('st-release-no-discard', 'src', ST, '    listeners_unref(&alloc, listeners);\n  }\n  gshscene_store_discard_resource(resource);', '    listeners_unref(&alloc, listeners);\n  }', S),
    ('st-kind-null-unchecked', 'src', ST, '  return resource ? resource->kind : GSHSCENE_RESOURCE_KIND_COUNT;', '  return resource->kind;', S),
    ('st-kind-null-value', 'src', ST, '  return resource ? resource->kind : GSHSCENE_RESOURCE_KIND_COUNT;', '  return resource ? resource->kind : GSHSCENE_RESOURCE_PATH;', S),
    ('st-id-null-unchecked', 'src', ST, '  return resource ? resource->id : 0;', '  return resource->id;', S),
    ('st-id-null-value', 'src', ST, '  return resource ? resource->id : 0;', '  return resource ? resource->id : 1;', S),
    ('st-hash-null-value', 'src', ST, '  return resource ? resource->hash : GSHGEOM_FNV_OFFSET_BASIS;', '  return resource ? resource->hash : 0;', S),
    ('st-hash-null-unchecked', 'src', ST, '  return resource ? resource->hash : GSHGEOM_FNV_OFFSET_BASIS;', '  return resource->hash;', S),
    ('st-refcount-null-unchecked', 'src', ST, '  return resource ? gcu_atomic_size_load(&((GSHSCENE_Resource *)resource)->refs) : 0;', '  return gcu_atomic_size_load(&((GSHSCENE_Resource *)resource)->refs);', S),
    ('st-add-listener-no-store-check', 'src', ST, '  if (!store || !fn || !out_token) {\n    return GSHSCENE_ERR_INVALID;\n  }\n  GCU_MUTEX_LOCK(store->lock);\n  size_t old_count', '  if (!fn || !out_token) {\n    return GSHSCENE_ERR_INVALID;\n  }\n  GCU_MUTEX_LOCK(store->lock);\n  size_t old_count', S),
    ('st-add-listener-no-fn-check', 'src', ST, '  if (!store || !fn || !out_token) {', '  if (!store || !out_token) {', S),
    ('st-add-listener-no-out-check', 'src', ST, '  if (!store || !fn || !out_token) {', '  if (!store || !fn) {', S),
    ('st-add-listener-forgets-old', 'src', ST, '  size_t old_count = store->listeners ? store->listeners->count : 0;\n  GSHSCENE_Listeners * grown', '  size_t old_count = 0;\n  GSHSCENE_Listeners * grown', S),
    ('st-add-listener-oom-unchecked', 'src', ST, '  if (!grown) {\n    GCU_MUTEX_UNLOCK(store->lock);\n    return GSHSCENE_ERR_OOM;\n  }\n  if (old_count) {', '  if (old_count) {', S),
    ('st-add-listener-no-copy', 'src', ST, '  if (old_count) {\n    memcpy(grown->items, store->listeners->items, old_count * sizeof(GSHSCENE_Listener));\n  }', '', S),
    ('st-add-listener-token-from-zero', 'src', ST, '  uint64_t token = ++store->next_token;', '  uint64_t token = store->next_token++;', S),
    ('st-add-listener-no-fn', 'src', ST, '  grown->items[old_count].fn = fn;\n', '  grown->items[old_count].fn = NULL;\n', S),
    ('st-add-listener-no-ctx', 'src', ST, '  grown->items[old_count].ctx = ctx;\n', '  (void)ctx;\n  grown->items[old_count].ctx = NULL;\n', S),
    ('st-add-listener-token-field', 'src', ST, '  grown->items[old_count].token = token;\n', '  grown->items[old_count].token = 0;\n', S),
    ('st-add-listener-not-installed', 'src', ST, '  GSHSCENE_Listeners * old = store->listeners;\n  store->listeners = grown;\n  GCU_MUTEX_UNLOCK(store->lock);\n  listeners_unref(&store->alloc, old);', '  GSHSCENE_Listeners * old = store->listeners;\n  GCU_MUTEX_UNLOCK(store->lock);\n  listeners_unref(&store->alloc, old);', S),
    ('st-add-listener-old-leaks', 'src', ST, '  GCU_MUTEX_UNLOCK(store->lock);\n  listeners_unref(&store->alloc, old);\n  *out_token = token;', '  GCU_MUTEX_UNLOCK(store->lock);\n  (void)old;\n  *out_token = token;', S),
    ('st-remove-listener-null-unchecked', 'src', ST, 'GSHSCENE_Result gshscene_store_remove_release_listener(GSHSCENE_Store * store, uint64_t token) {\n  if (!store) {\n    return GSHSCENE_ERR_INVALID;\n  }', 'GSHSCENE_Result gshscene_store_remove_release_listener(GSHSCENE_Store * store, uint64_t token) {', S),
    ('st-remove-listener-unknown-ok', 'src', ST, '  if (at == count) {\n    GCU_MUTEX_UNLOCK(store->lock);\n    return GSHSCENE_ERR_INVALID;\n  }', '', S),
    ('st-remove-listener-scan-start', 'src', ST, '  size_t at = 0;\n  size_t count = old ? old->count : 0;', '  size_t at = 1;\n  size_t count = old ? old->count : 0;', S),
    ('st-remove-listener-keeps-last', 'src', ST, '  if (count > 1) {\n    shrunk = listeners_make(&store->alloc, count - 1);', '  if (count > 2) {\n    shrunk = listeners_make(&store->alloc, count - 1);', S),
    ('st-remove-listener-oom-unchecked', 'src', ST, '    if (!shrunk) {\n      GCU_MUTEX_UNLOCK(store->lock);\n      return GSHSCENE_ERR_OOM;\n    }\n', '', S),
    ('st-remove-listener-copies-removed', 'src', ST, '    memcpy(shrunk->items + at, old->items + at + 1, (count - at - 1) * sizeof(GSHSCENE_Listener));', '    memcpy(shrunk->items + at, old->items + at, (count - at - 1) * sizeof(GSHSCENE_Listener));', S),
    ('st-remove-listener-drops-head', 'src', ST, '    memcpy(shrunk->items, old->items, at * sizeof(GSHSCENE_Listener));', '    memcpy(shrunk->items, old->items + 1, at * sizeof(GSHSCENE_Listener));', S),
    ('st-remove-listener-not-installed', 'src', ST, '  store->listeners = shrunk;\n  GCU_MUTEX_UNLOCK(store->lock);\n  listeners_unref(&store->alloc, old);\n  return GSHSCENE_OK;', '  GCU_MUTEX_UNLOCK(store->lock);\n  listeners_unref(&store->alloc, old);\n  return GSHSCENE_OK;', S),
    ('st-remove-listener-old-leaks', 'src', ST, '  store->listeners = shrunk;\n  GCU_MUTEX_UNLOCK(store->lock);\n  listeners_unref(&store->alloc, old);', '  store->listeners = shrunk;\n  GCU_MUTEX_UNLOCK(store->lock);', S),
    ('rs-name-path', 'src', RS, '      return "path";', '      return "";', R),
    ('rs-name-glyph-run', 'src', RS, '      return "glyph-run";', '      return "glyphs";', R + D),
    ('rs-name-picture', 'src', RS, '      return "picture";', '      return "pic";', R + D),
    ('rs-name-gradient', 'src', RS, '      return "gradient";', '      return "";', R),
    ('rs-name-image', 'src', RS, '      return "image";', '      return "";', R),
    ('rs-name-unknown', 'src', RS, '  return "unknown resource kind";', '  return NULL;', R),
    ('rs-name-unknown-count', 'src', RS, '    case GSHSCENE_RESOURCE_KIND_COUNT:\n      break;\n  }\n  return "unknown resource kind";', '    case GSHSCENE_RESOURCE_KIND_COUNT:\n      return "count";\n  }\n  return "unknown resource kind";', R),
    ('rs-format-version', 'src', RS, '#define RESOURCE_FORMAT_VERSION 1u', '#define RESOURCE_FORMAT_VERSION 2u', D),
    ('rs-hash-version-width', 'src', RS, '  gshgeom_hash_u32(h, RESOURCE_FORMAT_VERSION);', '  gshgeom_hash_u16(h, RESOURCE_FORMAT_VERSION);', D),
    ('rs-hash-kind-width', 'src', RS, '  gshgeom_hash_u32(h, (uint32_t)kind);', '  gshgeom_hash_u8(h, (uint8_t)kind);', D),
    ('rs-hash-kind-first', 'src', RS, '  gshgeom_hash_u32(h, RESOURCE_FORMAT_VERSION);\n  gshgeom_hash_u32(h, (uint32_t)kind);', '  gshgeom_hash_u32(h, (uint32_t)kind);\n  gshgeom_hash_u32(h, RESOURCE_FORMAT_VERSION);', D),
    ('rs-hash-path-width', 'src', RS, '  gshgeom_hash_u64(&h, gshgeom_path_hash(path));', '  gshgeom_hash_u32(&h, (uint32_t)gshgeom_path_hash(path));', D),
    ('rs-hash-path-kind', 'src', RS, '  hash_begin(&h, GSHSCENE_RESOURCE_PATH);', '  hash_begin(&h, GSHSCENE_RESOURCE_PICTURE);', D),
    ('rs-hash-run-kind', 'src', RS, '  hash_begin(&h, GSHSCENE_RESOURCE_GLYPH_RUN);', '  hash_begin(&h, GSHSCENE_RESOURCE_PATH);', D),
    ('rs-hash-picture-kind', 'src', RS, '  hash_begin(&h, GSHSCENE_RESOURCE_PICTURE);', '  hash_begin(&h, GSHSCENE_RESOURCE_PATH);', D),
    ('rs-hash-run-source-width', 'src', RS, '  gshgeom_hash_u32(&h, (uint32_t)run->source);', '  gshgeom_hash_u8(&h, (uint8_t)run->source);', D),
    ('rs-hash-run-key-width', 'src', RS, '  gshgeom_hash_u64(&h, run->font_key);', '  gshgeom_hash_u32(&h, (uint32_t)run->font_key);', D),
    ('rs-hash-run-face-width', 'src', RS, '  gshgeom_hash_u32(&h, run->face_index);', '  gshgeom_hash_u16(&h, (uint16_t)run->face_index);', D),
    ('rs-hash-run-size', 'src', RS, '  gshgeom_hash_f64(&h, run->size);', '  gshgeom_hash_f64(&h, run->size + 1.0);', D),
    ('rs-hash-run-hint-width', 'src', RS, '  gshgeom_hash_u32(&h, run->hint_mode);', '  gshgeom_hash_u8(&h, (uint8_t)run->hint_mode);', D),
    ('rs-hash-run-var-width', 'src', RS, '  gshgeom_hash_u64(&h, run->variation_key);', '  gshgeom_hash_u32(&h, (uint32_t)run->variation_key);', D),
    ('rs-hash-run-count-width', 'src', RS, '  gshgeom_hash_u64(&h, (uint64_t)run->glyph_count);', '  gshgeom_hash_u32(&h, (uint32_t)run->glyph_count);', D),
    ('rs-hash-run-order', 'src', RS, '  gshgeom_hash_u64(&h, run->font_key);\n  gshgeom_hash_u32(&h, run->face_index);', '  gshgeom_hash_u32(&h, run->face_index);\n  gshgeom_hash_u64(&h, run->font_key);', D),
    ('rs-hash-glyph-id-width', 'src', RS, '    gshgeom_hash_u32(&h, glyphs[i].id);', '    gshgeom_hash_u16(&h, (uint16_t)glyphs[i].id);', D),
    ('rs-hash-glyph-x', 'src', RS, '    gshgeom_hash_f64(&h, glyphs[i].x);', '    gshgeom_hash_f64(&h, glyphs[i].y);', D),
    ('rs-hash-glyph-y', 'src', RS, '    gshgeom_hash_f64(&h, glyphs[i].y);', '    gshgeom_hash_f64(&h, glyphs[i].x);', D),
    ('rs-hash-glyph-outline-width', 'src', RS, '    gshgeom_hash_u64(&h, outline_hash(glyphs[i].outline));', '    gshgeom_hash_u32(&h, (uint32_t)outline_hash(glyphs[i].outline));', D),
    ('rs-hash-no-outline-value', 'src', RS, '  return outline ? gshscene_resource_hash(outline) : 0;', '  return outline ? gshscene_resource_hash(outline) : 1;', D),
    ('rs-hash-no-outline-is-null-hash', 'src', RS, '  return outline ? gshscene_resource_hash(outline) : 0;', '  return gshscene_resource_hash(outline);', D),
    ('rs-hash-picture-width', 'src', RS, '  gshgeom_hash_u64(&h, gshscene_list_hash(list));', '  gshgeom_hash_u32(&h, (uint32_t)gshscene_list_hash(list));', D),
    ('rs-eq-point-x', 'src', RS, 'return gshgeom_f64_bits(a->x) == gshgeom_f64_bits(b->x) && gshgeom_f64_bits(a->y) == gshgeom_f64_bits(b->y) &&', 'return a->x == b->x && gshgeom_f64_bits(a->y) == gshgeom_f64_bits(b->y) &&', R),
    ('rs-eq-point-y', 'src', RS, 'return gshgeom_f64_bits(a->x) == gshgeom_f64_bits(b->x) && gshgeom_f64_bits(a->y) == gshgeom_f64_bits(b->y) &&', 'return gshgeom_f64_bits(a->x) == gshgeom_f64_bits(b->x) && a->y == b->y &&', R),
    ('rs-eq-point-z', 'src', RS, '         gshgeom_f64_bits(a->z) == gshgeom_f64_bits(b->z);', '         a->z == b->z;', R),
    ('rs-eq-point-or', 'src', RS, 'return gshgeom_f64_bits(a->x) == gshgeom_f64_bits(b->x) && gshgeom_f64_bits(a->y) == gshgeom_f64_bits(b->y) &&\n         gshgeom_f64_bits(a->z) == gshgeom_f64_bits(b->z);', 'return gshgeom_f64_bits(a->x) == gshgeom_f64_bits(b->x) || gshgeom_f64_bits(a->y) == gshgeom_f64_bits(b->y) ||\n         gshgeom_f64_bits(a->z) == gshgeom_f64_bits(b->z);', R),
    ('rs-eq-point-x-ignored', 'src', RS, 'return gshgeom_f64_bits(a->x) == gshgeom_f64_bits(b->x) && gshgeom_f64_bits(a->y) == gshgeom_f64_bits(b->y) &&', 'return gshgeom_f64_bits(a->y) == gshgeom_f64_bits(b->y) &&', R),
    ('rs-eq-verb-points-quadratic', 'src', RS, '    case GSHGEOM_VERB_QUADRATIC:\n    case GSHGEOM_VERB_RATIONAL_QUADRATIC:\n      return 2;', '    case GSHGEOM_VERB_QUADRATIC:\n    case GSHGEOM_VERB_RATIONAL_QUADRATIC:\n      return 1;', R),
    ('rs-eq-verb-points-cubic', 'src', RS, '    case GSHGEOM_VERB_CUBIC:\n    case GSHGEOM_VERB_RATIONAL_CUBIC:\n      return 3;', '    case GSHGEOM_VERB_CUBIC:\n    case GSHGEOM_VERB_RATIONAL_CUBIC:\n      return 2;', R),
    ('rs-eq-verb-points-line', 'src', RS, '    case GSHGEOM_VERB_MOVE:\n    case GSHGEOM_VERB_LINE:\n      return 1;', '    case GSHGEOM_VERB_MOVE:\n    case GSHGEOM_VERB_LINE:\n      return 0;', R),
    ('rs-eq-weights-quadratic', 'src', RS, '  if (verb == GSHGEOM_VERB_RATIONAL_QUADRATIC) {\n    return 3;\n  }', '  if (verb == GSHGEOM_VERB_RATIONAL_QUADRATIC) {\n    return 2;\n  }', R),
    ('rs-eq-weights-cubic', 'src', RS, '  return verb == GSHGEOM_VERB_RATIONAL_CUBIC ? 4 : 0;', '  return verb == GSHGEOM_VERB_RATIONAL_CUBIC ? 3 : 0;', R),
    ('rs-eq-dimension', 'src', RS, '  if (gshgeom_path_dimension(a) != gshgeom_path_dimension(b)) {\n    return 0;\n  }\n', '', R + S),
    ('rs-eq-count', 'src', RS, '  if (count != gshgeom_path_element_count(b)) {\n    return 0;\n  }\n', '', R),
    ('rs-eq-verb', 'src', RS, '    if (ea.verb != eb.verb) {\n      return 0;\n    }\n', '', R),
    ('rs-eq-last-point', 'src', RS, '    for (size_t k = 1; k <= verb_points(ea.verb); k++) {', '    for (size_t k = 1; k < verb_points(ea.verb); k++) {', R),
    ('rs-eq-points-ignored', 'src', RS, '      if (!point_equal(&ea.curve.p[k], &eb.curve.p[k])) {\n        return 0;\n      }\n', '', R),
    ('rs-eq-last-weight', 'src', RS, '    for (size_t k = 0; k < verb_weights(ea.verb); k++) {', '    for (size_t k = 0; k + 1 < verb_weights(ea.verb); k++) {', R),
    ('rs-eq-weights-ignored', 'src', RS, '      if (gshgeom_f64_bits(ea.curve.w[k]) != gshgeom_f64_bits(eb.curve.w[k])) {\n        return 0;\n      }\n', '', R),
    ('rs-run-eq-key', 'src', RS, 'a->font_key != b->font_key ||', '0 ||', R),
    ('rs-run-eq-face', 'src', RS, 'a->face_index != b->face_index ||', '0 ||', R),
    ('rs-run-eq-size', 'src', RS, '      gshgeom_f64_bits(a->size) != gshgeom_f64_bits(b->size) || a->hint_mode != b->hint_mode ||', '      0 || a->hint_mode != b->hint_mode ||', R),
    ('rs-run-eq-hint', 'src', RS, 'a->hint_mode != b->hint_mode ||', '0 ||', R),
    ('rs-run-eq-variation', 'src', RS, 'a->variation_key != b->variation_key ||', '0 ||', R),
    ('rs-run-eq-count', 'src', RS, 'a->glyph_count != b->glyph_count) {', '0) {', R),
    ('rs-run-eq-glyph-id', 'src', RS, '    if (ga[i].id != gb[i].id ||', '    if (0 ||', R),
    ('rs-run-eq-glyph-x', 'src', RS, 'gshgeom_f64_bits(ga[i].x) != gshgeom_f64_bits(gb[i].x) ||', '0 ||', R),
    ('rs-run-eq-glyph-x-numeric', 'src', RS, 'gshgeom_f64_bits(ga[i].x) != gshgeom_f64_bits(gb[i].x) ||', 'ga[i].x != gb[i].x ||', R),
    ('rs-run-eq-glyph-y', 'src', RS, '        gshgeom_f64_bits(ga[i].y) != gshgeom_f64_bits(gb[i].y) || ga[i].outline != gb[i].outline) {', '        0 || ga[i].outline != gb[i].outline) {', R),
    ('rs-run-eq-glyph-outline', 'src', RS, 'gshgeom_f64_bits(ga[i].y) != gshgeom_f64_bits(gb[i].y) || ga[i].outline != gb[i].outline) {', 'gshgeom_f64_bits(ga[i].y) != gshgeom_f64_bits(gb[i].y) || 0) {', R),
    ('rs-match-path-always', 'src', RS, '      return gshscene_path_equal(resource->u.path, (const GSHGEOM_Path *)probe->source);', '      return 1;', R + S),
    ('rs-match-run-always', 'src', RS, '      return run_equal(&resource->u.glyphs.run, resource->u.glyphs.glyphs, run, run->glyphs);', '      (void)run;\n      return 1;', R),
    ('rs-match-picture-always', 'src', RS, '      return gshscene_list_equal(resource->u.picture.list, (const GSHSCENE_List *)probe->source);', '      return 1;', R),
    ('rs-free-path', 'src', RS, '    case GSHSCENE_RESOURCE_PATH:\n      gshgeom_path_destroy(resource->u.path);\n      break;', '    case GSHSCENE_RESOURCE_PATH:\n      break;', S + R),
    ('rs-free-run-outlines', 'src', RS, '        gshscene_resource_release(resource->u.glyphs.glyphs[i].outline);\n', '', R),
    ('rs-free-run-array', 'src', RS, '      gshscene_free(alloc, resource->u.glyphs.glyphs);\n', '      (void)alloc;\n', R),
    ('rs-free-picture', 'src', RS, '      gshscene_list_destroy(resource->u.picture.list);\n', '', R),
    ('rs-geom-limit-code', 'src', RS, '    case GSHGEOM_ERR_LIMIT:\n      return GSHSCENE_ERR_LIMIT;', '    case GSHGEOM_ERR_LIMIT:\n      return GSHSCENE_ERR_INVALID;', R),
    ('rs-geom-oom-code', 'src', RS, '    case GSHGEOM_ERR_OOM:\n      return GSHSCENE_ERR_OOM;', '    case GSHGEOM_ERR_OOM:\n      return GSHSCENE_ERR_LIMIT;', R + S),
    ('rs-replay-line', 'src', RS, '      return gshgeom_path_line_to(copy, c->p[1]);', '      return gshgeom_path_line_to(copy, c->p[0]);', R),
    ('rs-replay-quadratic', 'src', RS, '      return gshgeom_path_quadratic_to(copy, c->p[1], c->p[2]);', '      return gshgeom_path_quadratic_to(copy, c->p[2], c->p[1]);', R),
    ('rs-replay-cubic', 'src', RS, '      return gshgeom_path_cubic_to(copy, c->p[1], c->p[2], c->p[3]);', '      return gshgeom_path_cubic_to(copy, c->p[2], c->p[1], c->p[3]);', R),
    ('rs-replay-rational-quadratic', 'src', RS, '      return gshgeom_path_rational_quadratic_to(copy, c->p[1], c->p[2], c->w);', '      return gshgeom_path_rational_quadratic_to(copy, c->p[2], c->p[1], c->w);', R),
    ('rs-replay-rational-cubic', 'src', RS, '      return gshgeom_path_rational_cubic_to(copy, c->p[1], c->p[2], c->p[3], c->w);', '      return gshgeom_path_rational_cubic_to(copy, c->p[2], c->p[1], c->p[3], c->w);', R),
    ('rs-replay-close', 'src', RS, '      return gshgeom_path_close(copy);', '      return GSHGEOM_OK;', R),
    ('rs-build-path-oom', 'src', RS, '  GSHSCENE_Resource * resource = gshscene_store_new_resource(store, GSHSCENE_RESOURCE_PATH);\n  if (!resource) {\n    return GSHSCENE_ERR_OOM;\n  }', '  GSHSCENE_Resource * resource = gshscene_store_new_resource(store, GSHSCENE_RESOURCE_PATH);', S),
    ('rs-build-path-limit', 'src', RS, '  limits.max_elements = store->limits.max_resource_elements;\n', '', R),
    ('rs-build-path-dimension', 'src', RS, 'gshgeom_path_create(&store->alloc, &limits, gshgeom_path_dimension(path), &resource->u.path);', 'gshgeom_path_create(&store->alloc, &limits, 2, &resource->u.path);', R),
    ('rs-build-path-last-element', 'src', RS, '  for (size_t i = 0; result == GSHGEOM_OK && i < count; i++) {', '  for (size_t i = 0; result == GSHGEOM_OK && i + 1 < count; i++) {', R),
    ('rs-build-path-failure-leaks', 'src', RS, '  if (result != GSHGEOM_OK) {\n    gshscene_store_discard_resource(resource);\n    return from_geometry(result);\n  }', '  if (result != GSHGEOM_OK) {\n    return from_geometry(result);\n  }', R),
    ('rs-intern-path-null-store', 'src', RS, '  if (!store || !path || !out) {\n    return GSHSCENE_ERR_INVALID;\n  }\n  GSHSCENE_Probe probe = {GSHSCENE_RESOURCE_PATH', '  if (!path || !out) {\n    return GSHSCENE_ERR_INVALID;\n  }\n  GSHSCENE_Probe probe = {GSHSCENE_RESOURCE_PATH', S + R),
    ('rs-intern-path-null-path', 'src', RS, '  if (!store || !path || !out) {\n    return GSHSCENE_ERR_INVALID;\n  }\n  GSHSCENE_Probe probe = {GSHSCENE_RESOURCE_PATH', '  if (!store || !out) {\n    return GSHSCENE_ERR_INVALID;\n  }\n  GSHSCENE_Probe probe = {GSHSCENE_RESOURCE_PATH', S + R),
    ('rs-intern-path-null-out', 'src', RS, '  if (!store || !path || !out) {\n    return GSHSCENE_ERR_INVALID;\n  }\n  GSHSCENE_Probe probe = {GSHSCENE_RESOURCE_PATH', '  if (!store || !path) {\n    return GSHSCENE_ERR_INVALID;\n  }\n  GSHSCENE_Probe probe = {GSHSCENE_RESOURCE_PATH', S + R),
    ('rs-run-null-run', 'src', RS, '  if (!run || (run->glyph_count && !run->glyphs)) {', '  if (run->glyph_count && !run->glyphs) {', R),
    ('rs-run-null-glyphs', 'src', RS, '  if (!run || (run->glyph_count && !run->glyphs)) {', '  if (!run) {', R),
    ('rs-run-source-range', 'src', RS, '  if ((unsigned)run->source >= (unsigned)GSHSCENE_GLYPH_SOURCE_COUNT) {', '  if ((unsigned)run->source > (unsigned)GSHSCENE_GLYPH_SOURCE_COUNT) {', R),
    ('rs-run-source-unchecked', 'src', RS, '  if ((unsigned)run->source >= (unsigned)GSHSCENE_GLYPH_SOURCE_COUNT) {\n    return GSHSCENE_ERR_INVALID;\n  }\n', '', R),
    ('rs-run-font-key', 'src', RS, '    if (run->font_key == 0 || !gshscene_finite(run->size)', '    if (!gshscene_finite(run->size)', R),
    ('rs-run-size-positive', 'src', RS, '|| !(run->size > 0.0) ||', '|| (run->size < 0.0) ||', R),
    ('rs-run-size-limit', 'src', RS, '|| run->size > GSHGEOM_COORD_LIMIT) {', '|| run->size >= GSHGEOM_COORD_LIMIT) {', R),
    ('rs-run-size-limit-unchecked', 'src', RS, ' || run->size > GSHGEOM_COORD_LIMIT) {\n      return GSHSCENE_ERR_INVALID;', ') {\n      return GSHSCENE_ERR_INVALID;', R),
    ('rs-run-outline-key', 'src', RS, '  } else if (run->font_key != 0 || run->face_index != 0 ||', '  } else if (run->face_index != 0 ||', R),
    ('rs-run-outline-face', 'src', RS, 'run->font_key != 0 || run->face_index != 0 || gshgeom_f64_bits(run->size) != 0 ||', 'run->font_key != 0 || gshgeom_f64_bits(run->size) != 0 ||', R),
    ('rs-run-outline-size', 'src', RS, 'gshgeom_f64_bits(run->size) != 0 ||\n             run->hint_mode != 0', '0 ||\n             run->hint_mode != 0', R),
    ('rs-run-outline-size-numeric', 'src', RS, 'gshgeom_f64_bits(run->size) != 0 ||\n             run->hint_mode != 0', 'run->size != 0.0 ||\n             run->hint_mode != 0', R),
    ('rs-run-outline-hint', 'src', RS, 'run->hint_mode != 0 || run->variation_key != 0) {', 'run->variation_key != 0) {', R),
    ('rs-run-outline-variation', 'src', RS, 'run->hint_mode != 0 || run->variation_key != 0) {', 'run->hint_mode != 0) {', R),
    ('rs-run-count-inclusive', 'src', RS, '  if (run->glyph_count > store->limits.max_resource_elements || run->glyph_count > SIZE_MAX / sizeof(GSHSCENE_Glyph)) {', '  if (run->glyph_count >= store->limits.max_resource_elements || run->glyph_count > SIZE_MAX / sizeof(GSHSCENE_Glyph)) {', R),
    ('rs-run-count-limit-unchecked', 'src', RS, '  if (run->glyph_count > store->limits.max_resource_elements || run->glyph_count > SIZE_MAX / sizeof(GSHSCENE_Glyph)) {', '  if (run->glyph_count > SIZE_MAX / sizeof(GSHSCENE_Glyph)) {', R),
    ('rs-run-count-overflow-unchecked', 'src', RS, '  if (run->glyph_count > store->limits.max_resource_elements || run->glyph_count > SIZE_MAX / sizeof(GSHSCENE_Glyph)) {', '  if (run->glyph_count > store->limits.max_resource_elements) {', R),
    ('rs-run-x-finite', 'src', RS, '    if (!gshscene_finite(g->x) || !gshscene_finite(g->y) || g->x > GSHGEOM_COORD_LIMIT ||', '    if (!gshscene_finite(g->y) || g->x > GSHGEOM_COORD_LIMIT ||', R),
    ('rs-run-y-finite', 'src', RS, '    if (!gshscene_finite(g->x) || !gshscene_finite(g->y) || g->x > GSHGEOM_COORD_LIMIT ||', '    if (!gshscene_finite(g->x) || g->x > GSHGEOM_COORD_LIMIT ||', R),
    ('rs-run-x-high', 'src', RS, 'g->x > GSHGEOM_COORD_LIMIT ||\n        g->x < -GSHGEOM_COORD_LIMIT', 'g->x >= GSHGEOM_COORD_LIMIT ||\n        g->x < -GSHGEOM_COORD_LIMIT', R),
    ('rs-run-x-high-unchecked', 'src', RS, 'g->x > GSHGEOM_COORD_LIMIT ||\n        g->x < -GSHGEOM_COORD_LIMIT', '0 ||\n        g->x < -GSHGEOM_COORD_LIMIT', R),
    ('rs-run-x-low', 'src', RS, 'g->x < -GSHGEOM_COORD_LIMIT || g->y > GSHGEOM_COORD_LIMIT', 'g->x <= -GSHGEOM_COORD_LIMIT || g->y > GSHGEOM_COORD_LIMIT', R),
    ('rs-run-x-low-unchecked', 'src', RS, 'g->x < -GSHGEOM_COORD_LIMIT || g->y > GSHGEOM_COORD_LIMIT', '0 || g->y > GSHGEOM_COORD_LIMIT', R),
    ('rs-run-y-high', 'src', RS, 'g->y > GSHGEOM_COORD_LIMIT || g->y < -GSHGEOM_COORD_LIMIT) {', 'g->y >= GSHGEOM_COORD_LIMIT || g->y < -GSHGEOM_COORD_LIMIT) {', R),
    ('rs-run-y-high-unchecked', 'src', RS, 'g->y > GSHGEOM_COORD_LIMIT || g->y < -GSHGEOM_COORD_LIMIT) {', '0 || g->y < -GSHGEOM_COORD_LIMIT) {', R),
    ('rs-run-y-low', 'src', RS, 'g->y > GSHGEOM_COORD_LIMIT || g->y < -GSHGEOM_COORD_LIMIT) {', 'g->y > GSHGEOM_COORD_LIMIT || g->y <= -GSHGEOM_COORD_LIMIT) {', R),
    ('rs-run-y-low-unchecked', 'src', RS, 'g->y > GSHGEOM_COORD_LIMIT || g->y < -GSHGEOM_COORD_LIMIT) {', 'g->y > GSHGEOM_COORD_LIMIT || 0) {', R),
    ('rs-run-font-glyph-outline', 'src', RS, '      if (g->outline) {\n        return GSHSCENE_ERR_INVALID;\n      }\n', '', R),
    ('rs-run-outline-null', 'src', RS, '    } else if (!g->outline || g->outline->store != store || g->outline->kind != GSHSCENE_RESOURCE_PATH) {', '    } else if (g->outline->store != store || g->outline->kind != GSHSCENE_RESOURCE_PATH) {', R),
    ('rs-run-outline-foreign', 'src', RS, '    } else if (!g->outline || g->outline->store != store || g->outline->kind != GSHSCENE_RESOURCE_PATH) {', '    } else if (!g->outline || g->outline->kind != GSHSCENE_RESOURCE_PATH) {', R),
    ('rs-run-outline-kind', 'src', RS, '    } else if (!g->outline || g->outline->store != store || g->outline->kind != GSHSCENE_RESOURCE_PATH) {', '    } else if (!g->outline || g->outline->store != store) {', R),
    ('rs-build-run-oom', 'src', RS, '  GSHSCENE_Resource * resource = gshscene_store_new_resource(store, GSHSCENE_RESOURCE_GLYPH_RUN);\n  if (!resource) {\n    return GSHSCENE_ERR_OOM;\n  }', '  GSHSCENE_Resource * resource = gshscene_store_new_resource(store, GSHSCENE_RESOURCE_GLYPH_RUN);', R),
    ('rs-build-run-no-copy-of-fields', 'src', RS, '  resource->u.glyphs.run = *run;\n', '', R + D),
    ('rs-build-run-count-not-reset', 'src', RS, '  resource->u.glyphs.run.glyph_count = 0; /* counts only what is retained so far */\n', '', R),
    ('rs-build-run-array-oom', 'src', RS, '    if (!resource->u.glyphs.glyphs) {\n      gshscene_store_discard_resource(resource);\n      return GSHSCENE_ERR_OOM;\n    }\n', '', R),
    ('rs-build-run-array-oom-leaks', 'src', RS, '    if (!resource->u.glyphs.glyphs) {\n      gshscene_store_discard_resource(resource);\n      return GSHSCENE_ERR_OOM;\n    }', '    if (!resource->u.glyphs.glyphs) {\n      return GSHSCENE_ERR_OOM;\n    }', R),
    ('rs-build-run-no-memcpy', 'src', RS, '    memcpy(resource->u.glyphs.glyphs, run->glyphs, run->glyph_count * sizeof(GSHSCENE_Glyph));\n', '', R),
    ('rs-build-run-no-retain', 'src', RS, '      gshscene_resource_retain(run->glyphs[i].outline);\n', '', R),
    ('rs-build-run-count-not-set', 'src', RS, '  resource->u.glyphs.run.glyph_count = run->glyph_count;\n', '', R),
    ('rs-build-run-borrowed-array', 'src', RS, '  resource->u.glyphs.run.glyphs = resource->u.glyphs.glyphs;\n', '  resource->u.glyphs.run.glyphs = run->glyphs;\n', R),
    ('rs-intern-run-null-store', 'src', RS, '  if (!store || !out) {\n    return GSHSCENE_ERR_INVALID;\n  }\n  GSHSCENE_Result checked', '  if (!out) {\n    return GSHSCENE_ERR_INVALID;\n  }\n  GSHSCENE_Result checked', R),
    ('rs-intern-run-null-out', 'src', RS, '  if (!store || !out) {\n    return GSHSCENE_ERR_INVALID;\n  }\n  GSHSCENE_Result checked', '  if (!store) {\n    return GSHSCENE_ERR_INVALID;\n  }\n  GSHSCENE_Result checked', R),
    ('rs-intern-run-check-ignored', 'src', RS, '  if (checked != GSHSCENE_OK) {\n    return checked;\n  }\n', '  (void)checked;\n', R),
    ('rs-build-picture-oom', 'src', RS, '  GSHSCENE_Resource * resource = gshscene_store_new_resource(store, GSHSCENE_RESOURCE_PICTURE);\n  if (!resource) {\n    return GSHSCENE_ERR_OOM;\n  }', '  GSHSCENE_Resource * resource = gshscene_store_new_resource(store, GSHSCENE_RESOURCE_PICTURE);', R),
    ('rs-build-picture-failure-leaks', 'src', RS, '  if (result != GSHSCENE_OK) {\n    gshscene_store_discard_resource(resource);\n    return result;\n  }\n  resource->u.picture.depth', '  if (result != GSHSCENE_OK) {\n    return result;\n  }\n  resource->u.picture.depth', R),
    ('rs-build-picture-failure-unchecked', 'src', RS, '  if (result != GSHSCENE_OK) {\n    gshscene_store_discard_resource(resource);\n    return result;\n  }\n  resource->u.picture.depth', '  (void)result;\n  resource->u.picture.depth', R),
    ('rs-build-picture-depth', 'src', RS, '  resource->u.picture.depth = gshscene_list_picture_depth(list) + 1;', '  resource->u.picture.depth = gshscene_list_picture_depth(list);', R),
    ('rs-intern-picture-null-out', 'src', RS, '  if (!store || !list || !out || !gshscene_list_is_closed(list) || list->store != store) {', '  if (!store || !list || !gshscene_list_is_closed(list) || list->store != store) {', R),
    ('rs-intern-picture-open', 'src', RS, '  if (!store || !list || !out || !gshscene_list_is_closed(list) || list->store != store) {', '  if (!store || !list || !out || list->store != store) {', R),
    ('rs-intern-picture-foreign', 'src', RS, '  if (!store || !list || !out || !gshscene_list_is_closed(list) || list->store != store) {', '  if (!store || !list || !out || !gshscene_list_is_closed(list)) {', R),
    ('rs-intern-picture-count-inclusive', 'src', RS, '  if (gshscene_list_count(list) > store->limits.max_resource_elements ||', '  if (gshscene_list_count(list) >= store->limits.max_resource_elements ||', R),
    ('rs-intern-picture-count-unchecked', 'src', RS, '  if (gshscene_list_count(list) > store->limits.max_resource_elements ||\n      gshscene_list_expansion', '  if (\n      gshscene_list_expansion', R),
    ('rs-intern-picture-expansion-inclusive', 'src', RS, '      gshscene_list_expansion(list) > store->limits.max_expansion ||', '      gshscene_list_expansion(list) >= store->limits.max_expansion ||', R),
    ('rs-intern-picture-expansion-unchecked', 'src', RS, '      gshscene_list_expansion(list) > store->limits.max_expansion ||\n', '', R),
    ('rs-intern-picture-depth-inclusive', 'src', RS, '      gshscene_list_picture_depth(list) + 1 > store->limits.max_picture_depth) {', '      gshscene_list_picture_depth(list) + 1 >= store->limits.max_picture_depth) {', R),
    ('rs-intern-picture-depth-off-by-one', 'src', RS, '      gshscene_list_picture_depth(list) + 1 > store->limits.max_picture_depth) {', '      gshscene_list_picture_depth(list) > store->limits.max_picture_depth) {', R),
    ('rs-intern-picture-depth-unchecked', 'src', RS, '||\n      gshscene_list_picture_depth(list) + 1 > store->limits.max_picture_depth) {', ') {', R),
    ('rs-path-kind-unchecked', 'src', RS, '  return resource && resource->kind == GSHSCENE_RESOURCE_PATH ? resource->u.path : NULL;', '  return resource ? resource->u.path : NULL;', R),
    ('rs-path-null-unchecked', 'src', RS, '  return resource && resource->kind == GSHSCENE_RESOURCE_PATH ? resource->u.path : NULL;', '  return resource->kind == GSHSCENE_RESOURCE_PATH ? resource->u.path : NULL;', R),
    ('rs-run-null-resource', 'src', RS, '  if (!resource || !out || resource->kind != GSHSCENE_RESOURCE_GLYPH_RUN) {', '  if (!out || resource->kind != GSHSCENE_RESOURCE_GLYPH_RUN) {', R),
    ('rs-run-null-out', 'src', RS, '  if (!resource || !out || resource->kind != GSHSCENE_RESOURCE_GLYPH_RUN) {', '  if (!resource || resource->kind != GSHSCENE_RESOURCE_GLYPH_RUN) {', R),
    ('rs-run-kind-unchecked', 'src', RS, '  if (!resource || !out || resource->kind != GSHSCENE_RESOURCE_GLYPH_RUN) {', '  if (!resource || !out) {', R),
    ('rs-run-read-back', 'src', RS, '  *out = resource->u.glyphs.run;\n  return GSHSCENE_OK;', '  return GSHSCENE_OK;', R),
    ('rs-picture-kind-unchecked', 'src', RS, '  return resource && resource->kind == GSHSCENE_RESOURCE_PICTURE ? resource->u.picture.list : NULL;', '  return resource ? resource->u.picture.list : NULL;', R),
    ('rs-picture-null-unchecked', 'src', RS, '  return resource && resource->kind == GSHSCENE_RESOURCE_PICTURE ? resource->u.picture.list : NULL;', '  return resource->kind == GSHSCENE_RESOURCE_PICTURE ? resource->u.picture.list : NULL;', R),
    ('rs-depth-kind-unchecked', 'src', RS, '  return resource && resource->kind == GSHSCENE_RESOURCE_PICTURE ? resource->u.picture.depth : 0;', '  return resource ? resource->u.picture.depth : 0;', R),
    ('rs-depth-null-unchecked', 'src', RS, '  return resource && resource->kind == GSHSCENE_RESOURCE_PICTURE ? resource->u.picture.depth : 0;', '  return resource->kind == GSHSCENE_RESOURCE_PICTURE ? resource->u.picture.depth : 0;', R),
    ('rs-dump-null-resource', 'src', RS, '  if (!resource || !out) {\n    return GSHSCENE_ERR_INVALID;\n  }\n  if (fprintf(out, "gshscene.resource', '  if (!out) {\n    return GSHSCENE_ERR_INVALID;\n  }\n  if (fprintf(out, "gshscene.resource', D),
    ('rs-dump-null-out', 'src', RS, '  if (!resource || !out) {\n    return GSHSCENE_ERR_INVALID;\n  }\n  if (fprintf(out, "gshscene.resource', '  if (!resource) {\n    return GSHSCENE_ERR_INVALID;\n  }\n  if (fprintf(out, "gshscene.resource', D),
    ('rs-dump-header-name', 'src', RS, '"gshscene.resource %u kind=%s hash=%016llx\\n"', '"gshscene.res %u kind=%s hash=%016llx\\n"', D),
    ('rs-dump-header-width', 'src', RS, '"gshscene.resource %u kind=%s hash=%016llx\\n"', '"gshscene.resource %u kind=%s hash=%llx\\n"', D),
    ('rs-dump-run-line', 'src', RS, '"run source=%u font=%016llx face=%u size=%016llx hint=%u var=%016llx glyphs=%llu\\n"', '"run source=%u font=%016llx face=%u size=%016llx hint=%u var=%016llx count=%llu\\n"', D),
    ('rs-dump-run-key-width', 'src', RS, '"run source=%u font=%016llx face=%u', '"run source=%u font=%llx face=%u', D),
    ('rs-dump-run-size-width', 'src', RS, 'size=%016llx hint=%u', 'size=%llx hint=%u', D),
    ('rs-dump-run-var-width', 'src', RS, 'var=%016llx glyphs=%llu', 'var=%llx glyphs=%llu', D),
    ('rs-dump-glyph-line', 'src', RS, '"g id=%u x=%016llx y=%016llx outline=%016llx\\n"', '"g id=%u x=%016llx y=%016llx outline=%llx\\n"', D),
    ('rs-dump-glyph-x-width', 'src', RS, '"g id=%u x=%016llx y=%016llx', '"g id=%u x=%llx y=%016llx', D),
    ('rs-dump-glyph-y-width', 'src', RS, 'y=%016llx outline', 'y=%llx outline', D),
    ('rs-dump-final-error-flag', 'src', RS, '  return (fflush(out) == 0 && !ferror(out)) ? GSHSCENE_OK : GSHSCENE_ERR_IO;', '  return fflush(out) == 0 ? GSHSCENE_OK : GSHSCENE_ERR_IO;', D),
    ('rs-dump-final-flush', 'src', RS, '  return (fflush(out) == 0 && !ferror(out)) ? GSHSCENE_OK : GSHSCENE_ERR_IO;', '  return !ferror(out) ? GSHSCENE_OK : GSHSCENE_ERR_IO;', D),
    ('ls-format-version', 'src', LS, '#define LIST_FORMAT_VERSION 1u', '#define LIST_FORMAT_VERSION 2u', D),
    ('ls-hash-kind', 'src', LS, '#define LIST_HASH_KIND UINT32_C(0x5453494c)', '#define LIST_HASH_KIND UINT32_C(0x5453494d)', D),
    ('ls-push-codes', 'src', LS, '#define PUSH_GROUP 2u', '#define PUSH_GROUP 1u', L),
    ('ls-takes-picture', 'src', LS, '#define TAKES_PICTURE 4u', '#define TAKES_PICTURE 1u', L),
    ('ls-takes-run', 'src', LS, '#define TAKES_RUN 2u', '#define TAKES_RUN 4u', L),
    ('ls-saturating-add', 'src', LS, '  return a > SIZE_MAX - b ? SIZE_MAX : a + b;', '  return a + b;', L),
    ('ls-arena-no-head-check', 'src', LS, '  if (standard && chunk && chunk->capacity - chunk->used >= size) {', '  if (standard && chunk->capacity - chunk->used >= size) {', L),
    ('ls-arena-bump-not-counted', 'src', LS, '    chunk->used += size;\n    return GSHSCENE_OK;', '    return GSHSCENE_OK;', L),
    ('ls-arena-bump-from-start', 'src', LS, '    *out = (unsigned char *)chunk + CHUNK_HEADER + chunk->used;', '    *out = (unsigned char *)chunk + CHUNK_HEADER;', L),
    ('ls-arena-limit-inclusive', 'src', LS, '|| total > list->limits.max_list_bytes - list->bytes) {', '|| total >= list->limits.max_list_bytes - list->bytes) {', L),
    ('ls-arena-limit-ignored', 'src', LS, '  if (list->bytes > list->limits.max_list_bytes || total > list->limits.max_list_bytes - list->bytes) {', '  if (0) {', L),
    ('ls-arena-chunk-oom-unchecked', 'src', LS, '  chunk = gshscene_alloc(&list->alloc, total);\n  if (!chunk) {\n    return GSHSCENE_ERR_OOM;\n  }', '  chunk = gshscene_alloc(&list->alloc, total);', L),
    ('ls-arena-chunk-used', 'src', LS, '  chunk->capacity = payload;\n  chunk->used = size;', '  chunk->capacity = payload;\n  chunk->used = 0;', L),
    ('ls-arena-first-not-kept', 'src', LS, '    if (!list->first) {\n      list->first = chunk;\n    }', '', L),
    ('ls-arena-big-not-linked', 'src', LS, '    chunk->next = list->big;\n    list->big = chunk;', '    list->big = chunk;', L),
    ('ls-arena-bytes-not-counted', 'src', LS, '  list->bytes += total;\n', '', L),
    ('ls-arena-returns-header', 'src', LS, '  *out = (unsigned char *)chunk + CHUNK_HEADER;\n  return GSHSCENE_OK;\n}\n\nstatic void chunks_free', '  *out = (unsigned char *)chunk;\n  return GSHSCENE_OK;\n}\n\nstatic void chunks_free', L),
    ('ls-chunks-free-keep', 'src', LS, '    if (chunk != keep) {', '    if ((void)keep, 1) {', L),
    ('ls-chunks-free-nothing', 'src', LS, '    if (chunk != keep) {\n      gshscene_free(&list->alloc, chunk);\n    }', '    if (chunk != keep) {\n      (void)list;\n    }', L),
    ('ls-limits-null', 'src', LS, '  gshscene_limits_default(out);\n  if (!limits) {\n    return;\n  }', '  gshscene_limits_default(out);', L + S),
    ('ls-limits-resources', 'src', LS, '    out->max_resources = limits->max_resources;\n', '', S),
    ('ls-limits-elements', 'src', LS, '    out->max_resource_elements = limits->max_resource_elements;\n', '', R),
    ('ls-limits-items', 'src', LS, '    out->max_list_items = limits->max_list_items;\n', '', L),
    ('ls-limits-bytes', 'src', LS, '    out->max_list_bytes = limits->max_list_bytes;\n', '', L),
    ('ls-limits-depth', 'src', LS, '    out->max_depth = limits->max_depth;\n', '', L),
    ('ls-limits-picture-depth', 'src', LS, '    out->max_picture_depth = limits->max_picture_depth;\n', '', R),
    ('ls-limits-expansion', 'src', LS, '    out->max_expansion = limits->max_expansion;\n', '', L + R),
    ('ls-create-oom-unchecked', 'src', LS, '  GSHSCENE_List * list = gshscene_calloc(alloc, 1, sizeof *list);\n  if (!list) {\n    return GSHSCENE_ERR_OOM;\n  }', '  GSHSCENE_List * list = gshscene_calloc(alloc, 1, sizeof *list);', L),
    ('ls-create-alloc', 'src', LS, '  list->alloc = *alloc;\n', '', L),
    ('ls-create-limits', 'src', LS, '  list->limits = *effective;\n', '  (void)effective;\n', L),
    ('ls-create-store', 'src', LS, '  list->store = store;\n  gshscene_store_ref(store);', '  gshscene_store_ref(store);', L),
    ('ls-create-no-store-ref', 'src', LS, '  list->store = store;\n  gshscene_store_ref(store);', '  list->store = store;', L),
    ('ls-create-null-store', 'src', LS, '  if (!store || !out) {\n    return GSHSCENE_ERR_INVALID;\n  }\n  if (!alloc) {', '  if (!out) {\n    return GSHSCENE_ERR_INVALID;\n  }\n  if (!alloc) {', L),
    ('ls-create-null-out', 'src', LS, '  if (!store || !out) {\n    return GSHSCENE_ERR_INVALID;\n  }\n  if (!alloc) {', '  if (!store) {\n    return GSHSCENE_ERR_INVALID;\n  }\n  if (!alloc) {', L),
    ('ls-create-default-alloc', 'src', LS, '  if (!alloc) {\n    alloc = gshscene_allocator_default();\n  }\n  GSHSCENE_Limits effective;', '  GSHSCENE_Limits effective;', L),
    ('ls-clear-items', 'src', LS, 'static void list_clear(GSHSCENE_List * list) {\n  items_release(list);\n', 'static void list_clear(GSHSCENE_List * list) {\n', L),
    ('ls-clear-standard-chunks', 'src', LS, '  chunks_free(list, list->head, list->first);\n', '', L),
    ('ls-clear-big-chunks', 'src', LS, '  chunks_free(list, list->big, NULL);\n  list->big = NULL;\n  list->head = list->first;', '  list->big = NULL;\n  list->head = list->first;', L),
    ('ls-clear-big-pointer', 'src', LS, '  chunks_free(list, list->big, NULL);\n  list->big = NULL;\n  list->head = list->first;', '  chunks_free(list, list->big, NULL);\n  list->head = list->first;', L),
    ('ls-clear-head', 'src', LS, '  list->big = NULL;\n  list->head = list->first;\n  list->bytes = 0;', '  list->big = NULL;\n  list->bytes = 0;', L),
    ('ls-clear-bytes-zero', 'src', LS, '    list->bytes = CHUNK_HEADER + list->first->capacity;\n', '', L),
    ('ls-clear-first-used', 'src', LS, '    list->first->used = 0;\n', '', L),
    ('ls-clear-segment-count', 'src', LS, '  list->segment_count = 0;\n', '', L),
    ('ls-clear-capacity', 'src', LS, '  list->capacity = 0;\n', '', L),
    ('ls-clear-count', 'src', LS, '  list->count = 0;\n  list->stack = NULL;', '  list->stack = NULL;', L),
    ('ls-clear-stack', 'src', LS, '  list->stack = NULL;\n  list->stack_capacity = 0;\n', '', L),
    ('ls-clear-depth', 'src', LS, '  list->depth = 0;\n  list->expansion = 0;', '  list->expansion = 0;', L),
    ('ls-clear-expansion', 'src', LS, '  list->expansion = 0;\n  list->picture_depth = 0;', '  list->picture_depth = 0;', L),
    ('ls-clear-picture-depth', 'src', LS, '  list->picture_depth = 0;\n  list->closed = 0;', '  list->closed = 0;', L),
    ('ls-clear-closed', 'src', LS, '  list->picture_depth = 0;\n  list->closed = 0;', '  list->picture_depth = 0;', L),
    ('ls-reset-null', 'src', LS, 'GSHSCENE_Result gshscene_list_reset(GSHSCENE_List * list) {\n  if (!list) {\n    return GSHSCENE_ERR_INVALID;\n  }\n  list_clear(list);', 'GSHSCENE_Result gshscene_list_reset(GSHSCENE_List * list) {\n  list_clear(list);', L),
    ('ls-destroy-null', 'src', LS, 'void gshscene_list_destroy(GSHSCENE_List * list) {\n  if (!list) {\n    return;\n  }', 'void gshscene_list_destroy(GSHSCENE_List * list) {', L),
    ('ls-destroy-items', 'src', LS, '  items_release(list);\n  chunks_free(list, list->head, NULL);', '  chunks_free(list, list->head, NULL);', L),
    ('ls-destroy-standard-chunks', 'src', LS, '  chunks_free(list, list->head, NULL);\n  chunks_free(list, list->big, NULL);\n  GSHSCENE_Store * store', '  chunks_free(list, list->big, NULL);\n  GSHSCENE_Store * store', L),
    ('ls-destroy-big-chunks', 'src', LS, '  chunks_free(list, list->head, NULL);\n  chunks_free(list, list->big, NULL);\n  GSHSCENE_Store * store', '  chunks_free(list, list->head, NULL);\n  GSHSCENE_Store * store', L),
    ('ls-destroy-store-ref', 'src', LS, '  gshscene_free(&alloc, list);\n  gshscene_store_unref(store);', '  gshscene_free(&alloc, list);\n  (void)store;', L),
    ('ls-destroy-list', 'src', LS, '  gshscene_free(&alloc, list);\n  gshscene_store_unref(store);', '  (void)alloc;\n  gshscene_store_unref(store);', L),
    ('ls-item-at-q', 'src', LS, '  size_t q = index / GSHSCENE_SEGMENT_FIRST + 1;', '  size_t q = index / GSHSCENE_SEGMENT_FIRST;', L),
    ('ls-item-at-loop', 'src', LS, '  while ((q >> (s + 1)) != 0) {', '  while ((q >> s) != 0) {', L),
    ('ls-item-at-base', 'src', LS, '  size_t base = GSHSCENE_SEGMENT_FIRST * (((size_t)1 << s) - 1);', '  size_t base = GSHSCENE_SEGMENT_FIRST * ((size_t)1 << s);', L),
    ('ls-items-release-all', 'src', LS, '  for (size_t i = 0; i < list->count; i++) {\n    gshscene_resource_release(item_at(list, i)->resource);', '  for (size_t i = 1; i < list->count; i++) {\n    gshscene_resource_release(item_at(list, i)->resource);', L),
    ('ls-item-init-identity', 'src', LS, '  gshgeom_map_identity(2, &item->map);\n', '', L),
    ('ls-item-init-kind', 'src', LS, '  item->kind = kind;\n', '  (void)kind;\n', L),
    ('ls-segment-size', 'src', LS, '  size_t items = (size_t)GSHSCENE_SEGMENT_FIRST << s;', '  size_t items = (size_t)GSHSCENE_SEGMENT_FIRST << (s + 1);', L),
    ('ls-segment-alloc-error', 'src', LS, '  if (result != GSHSCENE_OK) {\n    return result;\n  }\n  list->segments[s] = (GSHSCENE_Item *)memory;', '  (void)result;\n  list->segments[s] = (GSHSCENE_Item *)memory;', L),
    ('ls-segment-not-recorded', 'src', LS, '  list->segments[s] = (GSHSCENE_Item *)memory;\n  list->segment_count++;', '  list->segment_count++;', L),
    ('ls-segment-count', 'src', LS, '  list->segment_count++;\n  list->capacity += items;', '  list->capacity += items;', L),
    ('ls-segment-capacity', 'src', LS, '  list->capacity += items;\n  return GSHSCENE_OK;', '  return GSHSCENE_OK;', L),
    ('ls-append-instance-weight', 'src', LS, '    add = saturating_add(1, gshscene_list_expansion(gshscene_resource_picture(item->resource)));', '    add = 1;', L),
    ('ls-append-instance-self', 'src', LS, '    add = saturating_add(1, gshscene_list_expansion(gshscene_resource_picture(item->resource)));', '    add = gshscene_list_expansion(gshscene_resource_picture(item->resource));', L),
    ('ls-append-items-inclusive', 'src', LS, '  if (list->count >= list->limits.max_list_items) {', '  if (list->count > list->limits.max_list_items) {', L),
    ('ls-append-items-ignored', 'src', LS, '  if (list->count >= list->limits.max_list_items) {', '  if (0) {', L),
    ('ls-append-expansion-inclusive', 'src', LS, '  if (total > list->limits.max_expansion) {', '  if (total >= list->limits.max_expansion) {', L),
    ('ls-append-expansion-ignored', 'src', LS, '  if (total > list->limits.max_expansion) {', '  if (0) {', L),
    ('ls-append-no-segment', 'src', LS, '  if (list->count == list->capacity) {\n    GSHSCENE_Result result = segment_alloc(list);\n    if (result != GSHSCENE_OK) {\n      return result;\n    }\n  }', '', L),
    ('ls-append-segment-error', 'src', LS, '    GSHSCENE_Result result = segment_alloc(list);\n    if (result != GSHSCENE_OK) {\n      return result;\n    }', '    segment_alloc(list);', L),
    ('ls-append-no-store', 'src', LS, '  *item_at(list, list->count) = *item;\n', '', L),
    ('ls-append-no-retain', 'src', LS, '  gshscene_resource_retain(item->resource);\n  list->count++;', '  list->count++;', L),
    ('ls-append-no-count', 'src', LS, '  list->count++;\n  list->expansion = total;', '  list->expansion = total;', L),
    ('ls-append-no-expansion', 'src', LS, '  list->expansion = total;\n', '', L),
    ('ls-append-picture-depth-compare', 'src', LS, 'item->resource->u.picture.depth > list->picture_depth) {', 'item->resource->u.picture.depth < list->picture_depth) {', R),
    ('ls-append-picture-depth-store', 'src', LS, '    list->picture_depth = item->resource->u.picture.depth;', '    list->picture_depth = 0;', R),
    ('ls-append-picture-depth-kind', 'src', LS, '  if (item->kind == GSHSCENE_ITEM_INSTANCE && item->resource->u.picture.depth > list->picture_depth) {', '  if (item->resource && item->resource->u.picture.depth > list->picture_depth) {', L + R),
    ('ls-check-resource-null', 'src', LS, '  if (!resource || resource->store != list->store) {\n    return GSHSCENE_ERR_INVALID;\n  }\n  if (resource->kind == GSHSCENE_RESOURCE_PATH', '  if (resource->store != list->store) {\n    return GSHSCENE_ERR_INVALID;\n  }\n  if (resource->kind == GSHSCENE_RESOURCE_PATH', L),
    ('ls-check-resource-store', 'src', LS, '  if (!resource || resource->store != list->store) {', '  (void)list;\n  if (!resource) {', L),
    ('ls-check-resource-path', 'src', LS, 'resource->kind == GSHSCENE_RESOURCE_PATH && (takes & TAKES_PATH)', 'resource->kind == GSHSCENE_RESOURCE_PATH', L),
    ('ls-check-resource-run', 'src', LS, 'resource->kind == GSHSCENE_RESOURCE_GLYPH_RUN && (takes & TAKES_RUN)', 'resource->kind == GSHSCENE_RESOURCE_GLYPH_RUN', L),
    ('ls-check-resource-picture', 'src', LS, 'resource->kind == GSHSCENE_RESOURCE_PICTURE && (takes & TAKES_PICTURE)', 'resource->kind == GSHSCENE_RESOURCE_PICTURE', L),
    ('ls-map-null', 'src', LS, '  if (!map) {\n    gshgeom_map_identity(2, out);\n    return GSHSCENE_OK;\n  }', '', L),
    ('ls-map-check', 'src', LS, '  if (gshgeom_map_check(map) != GSHGEOM_OK) {\n    return GSHSCENE_ERR_INVALID;\n  }\n  gshgeom_map_identity(map->dim, out);', '  gshgeom_map_identity(map->dim, out);', L),
    ('ls-map-dim', 'src', LS, '  gshgeom_map_identity(map->dim, out);\n  out->kind = map->kind;', '  gshgeom_map_identity(2, out);\n  out->kind = map->kind;', L),
    ('ls-map-kind', 'src', LS, '  out->kind = map->kind;\n', '', L + D),
    ('ls-map-rows', 'src', LS, '  for (int i = 0; i < map->dim; i++) {\n    for (int j = 0; j < map->dim; j++) {\n      out->m[i][j] = map->m[i][j];', '  for (int i = 0; i < 2; i++) {\n    for (int j = 0; j < map->dim; j++) {\n      out->m[i][j] = map->m[i][j];', L),
    ('ls-map-cols', 'src', LS, '    for (int j = 0; j < map->dim; j++) {\n      out->m[i][j] = map->m[i][j];', '    for (int j = 0; j < 2; j++) {\n      out->m[i][j] = map->m[i][j];', L),
    ('ls-map-translation', 'src', LS, '    out->t[i] = map->t[i];', '', L),
    ('ls-rule-range', 'src', LS, '  return (unsigned)rule < (unsigned)GSHGEOM_FILL_RULE_COUNT ? GSHSCENE_OK : GSHSCENE_ERR_INVALID;', '  return (unsigned)rule <= (unsigned)GSHGEOM_FILL_RULE_COUNT ? GSHSCENE_OK : GSHSCENE_ERR_INVALID;', L),
    ('ls-paint-default-alpha', 'src', LS, '  out->rgba[3] = 1.0;\n  if (!paint) {', '  if (!paint) {', L),
    ('ls-paint-null', 'src', LS, '  if (!paint) {\n    return GSHSCENE_OK;\n  }\n  if (paint->kind == GSHSCENE_PAINT_GRADIENT ||', '  if (paint->kind == GSHSCENE_PAINT_GRADIENT ||', L),
    ('ls-paint-gradient', 'src', LS, '  if (paint->kind == GSHSCENE_PAINT_GRADIENT || paint->kind == GSHSCENE_PAINT_FOREGROUND ||\n      paint->kind == GSHSCENE_PAINT_PALETTE) {', '  if (paint->kind == GSHSCENE_PAINT_FOREGROUND ||\n      paint->kind == GSHSCENE_PAINT_PALETTE) {', L),
    ('ls-paint-foreground', 'src', LS, '  if (paint->kind == GSHSCENE_PAINT_GRADIENT || paint->kind == GSHSCENE_PAINT_FOREGROUND ||\n      paint->kind == GSHSCENE_PAINT_PALETTE) {', '  if (paint->kind == GSHSCENE_PAINT_GRADIENT ||\n      paint->kind == GSHSCENE_PAINT_PALETTE) {', L),
    ('ls-paint-palette', 'src', LS, '  if (paint->kind == GSHSCENE_PAINT_GRADIENT || paint->kind == GSHSCENE_PAINT_FOREGROUND ||\n      paint->kind == GSHSCENE_PAINT_PALETTE) {', '  if (paint->kind == GSHSCENE_PAINT_GRADIENT || paint->kind == GSHSCENE_PAINT_FOREGROUND) {', L),
    ('ls-paint-kind', 'src', LS, '  if (paint->kind != GSHSCENE_PAINT_SOLID || paint->gradient) {', '  if (paint->gradient) {', L),
    ('ls-paint-gradient-pointer', 'src', LS, '  if (paint->kind != GSHSCENE_PAINT_SOLID || paint->gradient) {', '  if (paint->kind != GSHSCENE_PAINT_SOLID) {', L),
    ('ls-paint-alpha-low', 'src', LS, '  if (!(paint->rgba[3] >= 0.0 && paint->rgba[3] <= 1.0)) {', '  if (!(paint->rgba[3] > 0.0 && paint->rgba[3] <= 1.0)) {', L),
    ('ls-paint-alpha-high', 'src', LS, '  if (!(paint->rgba[3] >= 0.0 && paint->rgba[3] <= 1.0)) {', '  if (!(paint->rgba[3] >= 0.0 && paint->rgba[3] < 1.0)) {', L),
    ('ls-paint-alpha-unchecked', 'src', LS, '  if (!(paint->rgba[3] >= 0.0 && paint->rgba[3] <= 1.0)) {', '  if (0) {', L),
    ('ls-paint-space', 'src', LS, '  if (paint->color_space != 0) {\n    return GSHSCENE_ERR_UNSUPPORTED;\n  }\n', '', L),
    ('ls-paint-copy', 'src', LS, '  for (int i = 0; i < 4; i++) {\n    out->rgba[i] = paint->rgba[i];', '  for (int i = 0; i < 3; i++) {\n    out->rgba[i] = paint->rgba[i];', L),
    ('ls-group-default-opacity', 'src', LS, '  out->opacity = 1.0;\n  out->blend_space = 0;', '  out->opacity = 0.0;\n  out->blend_space = 0;', L),
    ('ls-group-null', 'src', LS, '  if (!group) {\n    return GSHSCENE_OK;\n  }\n  if (!gshscene_finite(group->opacity)', '  if (!gshscene_finite(group->opacity)', L),
    ('ls-group-low', 'src', LS, '!(group->opacity >= 0.0 && group->opacity <= 1.0)) {', '!(group->opacity > 0.0 && group->opacity <= 1.0)) {', L),
    ('ls-group-high', 'src', LS, '!(group->opacity >= 0.0 && group->opacity <= 1.0)) {', '!(group->opacity >= 0.0 && group->opacity < 1.0)) {', L),
    ('ls-group-ordering-range', 'src', LS, '  if (group->ordering >= (uint32_t)GSHSCENE_ORDERING_COUNT) {', '  if (group->ordering > (uint32_t)GSHSCENE_ORDERING_COUNT) {', L),
    ('ls-group-ordering-unchecked', 'src', LS, '  if (group->ordering >= (uint32_t)GSHSCENE_ORDERING_COUNT) {\n    return GSHSCENE_ERR_INVALID;\n  }\n', '', L),
    ('ls-group-blend', 'src', LS, '  if (group->blend_space != 0 || group->ordering != GSHSCENE_ORDER_FLAT) {', '  if (group->ordering != GSHSCENE_ORDER_FLAT) {', L),
    ('ls-group-ordering', 'src', LS, '  if (group->blend_space != 0 || group->ordering != GSHSCENE_ORDER_FLAT) {', '  if (group->blend_space != 0) {', L),
    ('ls-group-opacity-copy', 'src', LS, '  out->opacity = group->opacity;\n  return GSHSCENE_OK;', '  return GSHSCENE_OK;', L),
    ('ls-stack-depth-inclusive', 'src', LS, '  if (list->depth >= list->limits.max_depth) {', '  if (list->depth > list->limits.max_depth) {', L),
    ('ls-stack-depth-ignored', 'src', LS, '  if (list->depth >= list->limits.max_depth) {\n    return GSHSCENE_ERR_LIMIT;\n  }\n  if (list->depth < list->stack_capacity) {', '  if (list->depth < list->stack_capacity) {', L),
    ('ls-stack-capacity-check', 'src', LS, '  if (list->depth < list->stack_capacity) {\n    return GSHSCENE_OK;\n  }', '', L),
    ('ls-stack-alloc-error', 'src', LS, '  GSHSCENE_Result result = arena_alloc(list, capacity, &memory);\n  if (result != GSHSCENE_OK) {\n    return result;\n  }', '  GSHSCENE_Result result = arena_alloc(list, capacity, &memory);\n  (void)result;', L),
    ('ls-stack-copy', 'src', LS, '  if (list->depth) {\n    memcpy(memory, list->stack, list->depth);\n  }', '', L),
    ('ls-stack-installed', 'src', LS, '  list->stack = (uint8_t *)memory;\n  list->stack_capacity = capacity;', '  list->stack_capacity = capacity;', L),
    ('ls-fill-null-list', 'src', LS, '    const GSHGEOM_Map * map, GSHGEOM_Fill_Rule rule, const GSHSCENE_Paint * paint, int32_t layer) {\n  if (!list || list->closed) {', '    const GSHGEOM_Map * map, GSHGEOM_Fill_Rule rule, const GSHSCENE_Paint * paint, int32_t layer) {\n  if (list->closed) {', L),
    ('ls-fill-closed', 'src', LS, '    const GSHGEOM_Map * map, GSHGEOM_Fill_Rule rule, const GSHSCENE_Paint * paint, int32_t layer) {\n  if (!list || list->closed) {', '    const GSHGEOM_Map * map, GSHGEOM_Fill_Rule rule, const GSHSCENE_Paint * paint, int32_t layer) {\n  if (!list) {', L),
    ('ls-fill-resource-check', 'src', LS, '  GSHSCENE_Result result = check_resource(list, resource, TAKES_PATH | TAKES_RUN);\n  if (result == GSHSCENE_OK) {\n    result = canonical_map(map, &item.map);\n  }\n  if (result == GSHSCENE_OK) {\n    result = check_rule(rule);\n  }\n  if (result == GSHSCENE_OK) {\n    result = canonical_paint(paint, &item.paint);', '  GSHSCENE_Result result = GSHSCENE_OK;\n  if (result == GSHSCENE_OK) {\n    result = canonical_map(map, &item.map);\n  }\n  if (result == GSHSCENE_OK) {\n    result = check_rule(rule);\n  }\n  if (result == GSHSCENE_OK) {\n    result = canonical_paint(paint, &item.paint);', L),
    ('ls-fill-map-check', 'src', LS, '  if (result == GSHSCENE_OK) {\n    result = canonical_map(map, &item.map);\n  }\n  if (result == GSHSCENE_OK) {\n    result = check_rule(rule);\n  }\n  if (result == GSHSCENE_OK) {\n    result = canonical_paint', '  if (result == GSHSCENE_OK) {\n    canonical_map(map, &item.map);\n  }\n  if (result == GSHSCENE_OK) {\n    result = check_rule(rule);\n  }\n  if (result == GSHSCENE_OK) {\n    result = canonical_paint', L),
    ('ls-fill-rule-check', 'src', LS, '  if (result == GSHSCENE_OK) {\n    result = check_rule(rule);\n  }\n  if (result == GSHSCENE_OK) {\n    result = canonical_paint', '  if (result == GSHSCENE_OK) {\n    check_rule(rule);\n  }\n  if (result == GSHSCENE_OK) {\n    result = canonical_paint', L),
    ('ls-fill-paint-check', 'src', LS, '  if (result == GSHSCENE_OK) {\n    result = canonical_paint(paint, &item.paint);\n  }', '  if (result == GSHSCENE_OK) {\n    canonical_paint(paint, &item.paint);\n  }', L),
    ('ls-fill-resource', 'src', LS, '  item.resource = resource;\n  item.fill_rule = rule;\n  item.layer = layer;\n  return list_append(list, &item);\n}\n\nGSHSCENE_Result gshscene_list_clip_push', '  item.fill_rule = rule;\n  item.layer = layer;\n  return list_append(list, &item);\n}\n\nGSHSCENE_Result gshscene_list_clip_push', L),
    ('ls-fill-rule', 'src', LS, '  item.resource = resource;\n  item.fill_rule = rule;\n  item.layer = layer;\n  return list_append(list, &item);\n}\n\nGSHSCENE_Result gshscene_list_clip_push', '  item.resource = resource;\n  item.layer = layer;\n  return list_append(list, &item);\n}\n\nGSHSCENE_Result gshscene_list_clip_push', L),
    ('ls-fill-layer', 'src', LS, '  item.resource = resource;\n  item.fill_rule = rule;\n  item.layer = layer;\n  return list_append(list, &item);\n}\n\nGSHSCENE_Result gshscene_list_clip_push', '  item.resource = resource;\n  item.fill_rule = rule;\n  (void)layer;\n  return list_append(list, &item);\n}\n\nGSHSCENE_Result gshscene_list_clip_push', L),
    ('ls-clip-null-list', 'src', LS, '    const GSHGEOM_Map * map, GSHGEOM_Fill_Rule rule) {\n  if (!list || list->closed) {', '    const GSHGEOM_Map * map, GSHGEOM_Fill_Rule rule) {\n  if (list->closed) {', L),
    ('ls-clip-closed', 'src', LS, '    const GSHGEOM_Map * map, GSHGEOM_Fill_Rule rule) {\n  if (!list || list->closed) {', '    const GSHGEOM_Map * map, GSHGEOM_Fill_Rule rule) {\n  if (!list) {', L),
    ('ls-clip-resource-check', 'src', LS, '  item_init(&item, GSHSCENE_ITEM_CLIP_PUSH);\n  GSHSCENE_Result result = check_resource(list, resource, TAKES_PATH | TAKES_RUN);', '  item_init(&item, GSHSCENE_ITEM_CLIP_PUSH);\n  GSHSCENE_Result result = GSHSCENE_OK;', L),
    ('ls-clip-stack', 'src', LS, '  if (result == GSHSCENE_OK) {\n    result = stack_reserve(list);\n  }\n  if (result != GSHSCENE_OK) {\n    return result;\n  }\n  item.resource = resource;\n  item.fill_rule = rule;\n  result = list_append(list, &item);\n  if (result == GSHSCENE_OK) {\n    list->stack[list->depth++] = PUSH_CLIP;', '  if (result != GSHSCENE_OK) {\n    return result;\n  }\n  item.resource = resource;\n  item.fill_rule = rule;\n  result = list_append(list, &item);\n  if (result == GSHSCENE_OK) {\n    list->stack[list->depth++] = PUSH_CLIP;', L),
    ('ls-clip-rule-check', 'src', LS, '  if (result == GSHSCENE_OK) {\n    result = check_rule(rule);\n  }\n  if (result == GSHSCENE_OK) {\n    result = stack_reserve(list);', '  if (result == GSHSCENE_OK) {\n    check_rule(rule);\n  }\n  if (result == GSHSCENE_OK) {\n    result = stack_reserve(list);', L),
    ('ls-clip-resource', 'src', LS, '  item.resource = resource;\n  item.fill_rule = rule;\n  result = list_append(list, &item);', '  item.fill_rule = rule;\n  result = list_append(list, &item);', L),
    ('ls-clip-rule', 'src', LS, '  item.resource = resource;\n  item.fill_rule = rule;\n  result = list_append(list, &item);', '  item.resource = resource;\n  result = list_append(list, &item);', L),
    ('ls-clip-push-kind', 'src', LS, '    list->stack[list->depth++] = PUSH_CLIP;', '    list->stack[list->depth++] = PUSH_GROUP;', L),
    ('ls-clip-push-uncounted', 'src', LS, '    list->stack[list->depth++] = PUSH_CLIP;', '    list->stack[list->depth] = PUSH_CLIP;', L),
    ('ls-pop-null', 'src', LS, '  if (!list || list->closed || list->depth == 0 || list->stack[list->depth - 1] != push) {', '  if (list->closed || list->depth == 0 || list->stack[list->depth - 1] != push) {', L),
    ('ls-pop-empty', 'src', LS, '  if (!list || list->closed || list->depth == 0 || list->stack[list->depth - 1] != push) {', '  if (!list || list->closed || list->stack[list->depth - 1] != push) {', L),
    ('ls-pop-kind', 'src', LS, '  if (!list || list->closed || list->depth == 0 || list->stack[list->depth - 1] != push) {', '  (void)push;\n  if (!list || list->closed || list->depth == 0) {', L),
    ('ls-pop-depth', 'src', LS, '  if (result == GSHSCENE_OK) {\n    list->depth--;\n  }', '', L),
    ('ls-pop-item-kind', 'src', LS, '  return pop(list, PUSH_CLIP, GSHSCENE_ITEM_CLIP_POP);', '  return pop(list, PUSH_CLIP, GSHSCENE_ITEM_GROUP_POP);', L),
    ('ls-group-pop-item-kind', 'src', LS, '  return pop(list, PUSH_GROUP, GSHSCENE_ITEM_GROUP_POP);', '  return pop(list, PUSH_GROUP, GSHSCENE_ITEM_CLIP_POP);', L),
    ('ls-group-push-null-list', 'src', LS, 'GSHSCENE_Result gshscene_list_group_push(GSHSCENE_List * list, const GSHSCENE_Group * group, int32_t layer) {\n  if (!list || list->closed) {', 'GSHSCENE_Result gshscene_list_group_push(GSHSCENE_List * list, const GSHSCENE_Group * group, int32_t layer) {\n  if (list->closed) {', L),
    ('ls-group-push-closed', 'src', LS, 'GSHSCENE_Result gshscene_list_group_push(GSHSCENE_List * list, const GSHSCENE_Group * group, int32_t layer) {\n  if (!list || list->closed) {', 'GSHSCENE_Result gshscene_list_group_push(GSHSCENE_List * list, const GSHSCENE_Group * group, int32_t layer) {\n  if (!list) {', L),
    ('ls-group-push-check', 'src', LS, '  GSHSCENE_Result result = canonical_group(group, &item.group);\n  if (result == GSHSCENE_OK) {\n    result = stack_reserve(list);\n  }', '  GSHSCENE_Result result = GSHSCENE_OK;\n  canonical_group(group, &item.group);\n  if (result == GSHSCENE_OK) {\n    result = stack_reserve(list);\n  }', L),
    ('ls-group-push-reserve', 'src', LS, '  GSHSCENE_Result result = canonical_group(group, &item.group);\n  if (result == GSHSCENE_OK) {\n    result = stack_reserve(list);\n  }', '  GSHSCENE_Result result = canonical_group(group, &item.group);', L),
    ('ls-group-push-layer', 'src', LS, '  item.layer = layer;\n  result = list_append(list, &item);\n  if (result == GSHSCENE_OK) {\n    list->stack[list->depth++] = PUSH_GROUP;', '  (void)layer;\n  result = list_append(list, &item);\n  if (result == GSHSCENE_OK) {\n    list->stack[list->depth++] = PUSH_GROUP;', L),
    ('ls-group-push-kind', 'src', LS, '    list->stack[list->depth++] = PUSH_GROUP;', '    list->stack[list->depth++] = PUSH_CLIP;', L),
    ('ls-instance-null-list', 'src', LS, '    GSHSCENE_List * list, const GSHSCENE_Resource * picture, const GSHGEOM_Map * map, int32_t layer) {\n  if (!list || list->closed) {', '    GSHSCENE_List * list, const GSHSCENE_Resource * picture, const GSHGEOM_Map * map, int32_t layer) {\n  if (list->closed) {', L),
    ('ls-instance-closed', 'src', LS, '    GSHSCENE_List * list, const GSHSCENE_Resource * picture, const GSHGEOM_Map * map, int32_t layer) {\n  if (!list || list->closed) {', '    GSHSCENE_List * list, const GSHSCENE_Resource * picture, const GSHGEOM_Map * map, int32_t layer) {\n  if (!list) {', L),
    ('ls-instance-resource-check', 'src', LS, '  GSHSCENE_Result result = check_resource(list, picture, TAKES_PICTURE);', '  GSHSCENE_Result result = GSHSCENE_OK;', L),
    ('ls-instance-map-check', 'src', LS, '  if (result == GSHSCENE_OK) {\n    result = canonical_map(map, &item.map);\n  }\n  if (result != GSHSCENE_OK) {\n    return result;\n  }\n  item.resource = picture;', '  if (result == GSHSCENE_OK) {\n    canonical_map(map, &item.map);\n  }\n  if (result != GSHSCENE_OK) {\n    return result;\n  }\n  item.resource = picture;', L),
    ('ls-instance-resource', 'src', LS, '  item.resource = picture;\n  item.layer = layer;', '  item.layer = layer;', L),
    ('ls-instance-layer', 'src', LS, '  item.resource = picture;\n  item.layer = layer;', '  item.resource = picture;\n  (void)layer;', L),
    ('ls-close-null', 'src', LS, '  if (!list || list->closed || list->depth != 0) {', '  if (list->closed || list->depth != 0) {', L),
    ('ls-close-twice', 'src', LS, '  if (!list || list->closed || list->depth != 0) {', '  if (!list || list->depth != 0) {', L),
    ('ls-close-open-push', 'src', LS, '  if (!list || list->closed || list->depth != 0) {', '  if (!list || list->closed) {', L),
    ('ls-close-seals', 'src', LS, '  list->closed = 1;\n  return GSHSCENE_OK;\n}\n\n/* --- reading', '  return GSHSCENE_OK;\n}\n\n/* --- reading', L),
    ('ls-is-closed-null', 'src', LS, '  return list ? list->closed : 0;', '  return list->closed;', L),
    ('ls-count-null', 'src', LS, '  return list ? list->count : 0;', '  return list->count;', L),
    ('ls-item-null', 'src', LS, '  if (!list || !out || index >= list->count) {', '  if (!out || index >= list->count) {', L),
    ('ls-item-out-null', 'src', LS, '  if (!list || !out || index >= list->count) {', '  if (!list || index >= list->count) {', L),
    ('ls-item-index', 'src', LS, '  if (!list || !out || index >= list->count) {', '  if (!list || !out || index > list->count) {', L),
    ('ls-expansion-null', 'src', LS, '  return list ? list->expansion : 0;', '  return list->expansion;', L),
    ('ls-bytes-null', 'src', LS, '  return list ? list->bytes : 0;', '  return list->bytes;', L),
    ('ls-copy-limits', 'src', LS, 'gshscene_list_create_effective(alloc, &source->limits, source->store, &copy);', 'gshscene_list_create_effective(alloc, &(GSHSCENE_Limits){0}, source->store, &copy);', R),
    ('ls-copy-last-item', 'src', LS, '  for (size_t i = 0; result == GSHSCENE_OK && i < source->count; i++) {\n    result = list_append(copy, item_at(source, i));', '  for (size_t i = 0; result == GSHSCENE_OK && i + 1 < source->count; i++) {\n    result = list_append(copy, item_at(source, i));', R),
    ('ls-copy-failure-leaks', 'src', LS, '  if (result != GSHSCENE_OK) {\n    gshscene_list_destroy(copy);\n    return result;\n  }\n  copy->closed = 1;', '  if (result != GSHSCENE_OK) {\n    return result;\n  }\n  copy->closed = 1;', R),
    ('ls-copy-closed', 'src', LS, '  copy->closed = 1;\n  *out = copy;', '  *out = copy;', R),
    ('ls-eq-map-dim', 'src', LS, '  if (a->dim != b->dim || a->kind != b->kind) {', '  if (a->kind != b->kind) {', L),
    ('ls-eq-map-kind', 'src', LS, '  if (a->dim != b->dim || a->kind != b->kind) {', '  if (a->dim != b->dim) {', L),
    ('ls-eq-map-rows', 'src', LS, '  for (int i = 0; i < a->dim; i++) {\n    for (int j = 0; j < a->dim; j++) {\n      if (gshgeom_f64_bits(a->m[i][j]) != gshgeom_f64_bits(b->m[i][j])) {', '  for (int i = 0; i < 2; i++) {\n    for (int j = 0; j < a->dim; j++) {\n      if (gshgeom_f64_bits(a->m[i][j]) != gshgeom_f64_bits(b->m[i][j])) {', L),
    ('ls-eq-map-cols', 'src', LS, '    for (int j = 0; j < a->dim; j++) {\n      if (gshgeom_f64_bits(a->m[i][j]) != gshgeom_f64_bits(b->m[i][j])) {', '    for (int j = 0; j < 2; j++) {\n      if (gshgeom_f64_bits(a->m[i][j]) != gshgeom_f64_bits(b->m[i][j])) {', L),
    ('ls-eq-map-matrix', 'src', LS, '      if (gshgeom_f64_bits(a->m[i][j]) != gshgeom_f64_bits(b->m[i][j])) {\n        return 0;\n      }\n', '', L),
    ('ls-eq-map-translation', 'src', LS, '    if (gshgeom_f64_bits(a->t[i]) != gshgeom_f64_bits(b->t[i])) {\n      return 0;\n    }\n', '', L),
    ('ls-eq-map-translation-numeric', 'src', LS, '    if (gshgeom_f64_bits(a->t[i]) != gshgeom_f64_bits(b->t[i])) {', '    if (a->t[i] != b->t[i]) {', L),
    ('ls-eq-resource', 'src', LS, 'a->resource != b->resource || !map_equal', '!map_equal', L),
    ('ls-eq-map', 'src', LS, 'a->resource != b->resource || !map_equal(&a->map, &b->map) ||\n      a->fill_rule', 'a->resource != b->resource ||\n      a->fill_rule', L),
    ('ls-eq-rule', 'src', LS, '      a->fill_rule != b->fill_rule || a->layer != b->layer) {', '      a->layer != b->layer) {', L),
    ('ls-eq-layer', 'src', LS, '      a->fill_rule != b->fill_rule || a->layer != b->layer) {', '      a->fill_rule != b->fill_rule) {', L),
    ('ls-eq-rgba', 'src', LS, '    if (gshgeom_f64_bits(a->paint.rgba[i]) != gshgeom_f64_bits(b->paint.rgba[i])) {\n      return 0;\n    }\n', '', L),
    ('ls-eq-rgba-numeric', 'src', LS, '    if (gshgeom_f64_bits(a->paint.rgba[i]) != gshgeom_f64_bits(b->paint.rgba[i])) {', '    if (a->paint.rgba[i] != b->paint.rgba[i]) {', L),
    ('ls-eq-rgba-loop', 'src', LS, '  for (int i = 0; i < 4; i++) {\n    if (gshgeom_f64_bits(a->paint.rgba[i])', '  for (int i = 0; i < 3; i++) {\n    if (gshgeom_f64_bits(a->paint.rgba[i])', L),
    ('ls-eq-opacity', 'src', LS, '  return gshgeom_f64_bits(a->group.opacity) == gshgeom_f64_bits(b->group.opacity) &&\n         a->group.blend_space', '  return 1 &&\n         a->group.blend_space', L),
    ('ls-eq-opacity-numeric', 'src', LS, '  return gshgeom_f64_bits(a->group.opacity) == gshgeom_f64_bits(b->group.opacity) &&', '  return a->group.opacity == b->group.opacity &&', L),
    ('ls-eq-null-a', 'src', LS, '  if (!a || !b || a->count != b->count) {', '  if (!b || a->count != b->count) {', L),
    ('ls-eq-null-b', 'src', LS, '  if (!a || !b || a->count != b->count) {', '  if (!a || a->count != b->count) {', L),
    ('ls-eq-count', 'src', LS, '  if (!a || !b || a->count != b->count) {', '  if (!a || !b) {', L),
    ('ls-eq-items-ignored', 'src', LS, '    if (!item_equal(item_at(a, i), item_at(b, i))) {\n      return 0;\n    }\n', '', L),
    ('ls-hash-null', 'src', LS, '  if (!list) {\n    return gshgeom_hash_value(&h);\n  }\n  gshgeom_hash_u32(&h, LIST_FORMAT_VERSION);', '  gshgeom_hash_u32(&h, LIST_FORMAT_VERSION);', L),
    ('ls-hash-version-width', 'src', LS, '  gshgeom_hash_u32(&h, LIST_FORMAT_VERSION);', '  gshgeom_hash_u16(&h, LIST_FORMAT_VERSION);', D),
    ('ls-hash-kind-width', 'src', LS, '  gshgeom_hash_u32(&h, LIST_HASH_KIND);', '  gshgeom_hash_u16(&h, (uint16_t)LIST_HASH_KIND);', D),
    ('ls-hash-count-width', 'src', LS, '  gshgeom_hash_u64(&h, (uint64_t)list->count);', '  gshgeom_hash_u32(&h, (uint32_t)list->count);', D),
    ('ls-hash-item-kind-width', 'src', LS, '    gshgeom_hash_u8(&h, (uint8_t)item->kind);', '    gshgeom_hash_u16(&h, (uint16_t)item->kind);', D),
    ('ls-hash-resource-width', 'src', LS, '  gshgeom_hash_u64(h, gshscene_resource_hash(item->resource));', '  gshgeom_hash_u32(h, (uint32_t)gshscene_resource_hash(item->resource));', D),
    ('ls-hash-map-width', 'src', LS, '  gshgeom_hash_u64(h, gshgeom_map_hash(&item->map));', '  gshgeom_hash_u32(h, (uint32_t)gshgeom_map_hash(&item->map));', D),
    ('ls-hash-resource-map-order', 'src', LS, '  gshgeom_hash_u64(h, gshscene_resource_hash(item->resource));\n  gshgeom_hash_u64(h, gshgeom_map_hash(&item->map));', '  gshgeom_hash_u64(h, gshgeom_map_hash(&item->map));\n  gshgeom_hash_u64(h, gshscene_resource_hash(item->resource));', D),
    ('ls-hash-fill-rule-width', 'src', LS, '        hash_resource_and_map(&h, item);\n        gshgeom_hash_u8(&h, (uint8_t)item->fill_rule);\n        gshgeom_hash_u8(&h, (uint8_t)item->paint.kind);', '        hash_resource_and_map(&h, item);\n        gshgeom_hash_u32(&h, (uint32_t)item->fill_rule);\n        gshgeom_hash_u8(&h, (uint8_t)item->paint.kind);', D),
    ('ls-hash-paint-kind-width', 'src', LS, '        gshgeom_hash_u8(&h, (uint8_t)item->paint.kind);', '        gshgeom_hash_u32(&h, (uint32_t)item->paint.kind);', D),
    ('ls-hash-rgba-loop', 'src', LS, '        for (int k = 0; k < 4; k++) {\n          gshgeom_hash_f64(&h, item->paint.rgba[k]);', '        for (int k = 0; k < 3; k++) {\n          gshgeom_hash_f64(&h, item->paint.rgba[k]);', D),
    ('ls-hash-space-width', 'src', LS, '        gshgeom_hash_u32(&h, item->paint.color_space);', '        gshgeom_hash_u8(&h, (uint8_t)item->paint.color_space);', D),
    ('ls-hash-fill-layer-width', 'src', LS, '        gshgeom_hash_u32(&h, item->paint.color_space);\n        gshgeom_hash_i32(&h, item->layer);', '        gshgeom_hash_u32(&h, item->paint.color_space);\n        gshgeom_hash_i64(&h, item->layer);', D),
    ('ls-hash-clip-rule-width', 'src', LS, '      case GSHSCENE_ITEM_CLIP_PUSH:\n        hash_resource_and_map(&h, item);\n        gshgeom_hash_u8(&h, (uint8_t)item->fill_rule);', '      case GSHSCENE_ITEM_CLIP_PUSH:\n        hash_resource_and_map(&h, item);\n        gshgeom_hash_u32(&h, (uint32_t)item->fill_rule);', D),
    ('ls-hash-opacity', 'src', LS, '        gshgeom_hash_f64(&h, item->group.opacity);', '        gshgeom_hash_f64(&h, item->group.opacity + 1.0);', D),
    ('ls-hash-blend-width', 'src', LS, '        gshgeom_hash_u32(&h, item->group.blend_space);', '        gshgeom_hash_u8(&h, (uint8_t)item->group.blend_space);', D),
    ('ls-hash-ordering-width', 'src', LS, '        gshgeom_hash_u32(&h, item->group.ordering);', '        gshgeom_hash_u8(&h, (uint8_t)item->group.ordering);', D),
    ('ls-hash-group-layer-width', 'src', LS, '        gshgeom_hash_u32(&h, item->group.ordering);\n        gshgeom_hash_i32(&h, item->layer);', '        gshgeom_hash_u32(&h, item->group.ordering);\n        gshgeom_hash_i64(&h, item->layer);', D),
    ('ls-hash-instance-layer-width', 'src', LS, '      case GSHSCENE_ITEM_INSTANCE:\n        hash_resource_and_map(&h, item);\n        gshgeom_hash_i32(&h, item->layer);', '      case GSHSCENE_ITEM_INSTANCE:\n        hash_resource_and_map(&h, item);\n        gshgeom_hash_i64(&h, item->layer);', D),
    ('ls-hash-instance-resource', 'src', LS, '      case GSHSCENE_ITEM_INSTANCE:\n        hash_resource_and_map(&h, item);', '      case GSHSCENE_ITEM_INSTANCE:\n        gshgeom_hash_u64(&h, gshgeom_map_hash(&item->map));', D),
    ('ls-hash-pop-fields', 'src', LS, '      default:\n        break;\n    }\n  }\n  return gshgeom_hash_value(&h);', '      default:\n        gshgeom_hash_u8(&h, 0);\n        break;\n    }\n  }\n  return gshgeom_hash_value(&h);', D),
    ('ls-dump-null-list', 'src', LS, '  if (!list || !out) {\n    return GSHSCENE_ERR_INVALID;\n  }\n  /* The count is printed', '  if (!out) {\n    return GSHSCENE_ERR_INVALID;\n  }\n  /* The count is printed', D),
    ('ls-dump-null-out', 'src', LS, '  if (!list || !out) {\n    return GSHSCENE_ERR_INVALID;\n  }\n  /* The count is printed', '  if (!list) {\n    return GSHSCENE_ERR_INVALID;\n  }\n  /* The count is printed', D),
    ('ls-dump-header', 'src', LS, '"gshscene.list %u items=%llu hash=%016llx\\n"', '"gshscene.list %u count=%llu hash=%016llx\\n"', D),
    ('ls-dump-header-hash-width', 'src', LS, 'items=%llu hash=%016llx\\n"', 'items=%llu hash=%llx\\n"', D),
    ('ls-dump-resource-width', 'src', LS, '" res=%s:%016llx map=%016llx"', '" res=%s:%llx map=%016llx"', D),
    ('ls-dump-map-width', 'src', LS, '" res=%s:%016llx map=%016llx"', '" res=%s:%016llx map=%llx"', D),
    ('ls-dump-fill-tag', 'src', LS, "        written = fputc('F', out) == EOF ? -1 : dump_resource_and_map(out, item);", "        written = fputc('f', out) == EOF ? -1 : dump_resource_and_map(out, item);", D),
    ('ls-dump-fill-line', 'src', LS, '" rule=%u paint=%u %016llx %016llx %016llx %016llx space=%u layer=%lld"', '" rule=%u kind=%u %016llx %016llx %016llx %016llx space=%u layer=%lld"', D),
    ('ls-dump-fill-red-width', 'src', LS, '" rule=%u paint=%u %016llx %016llx', '" rule=%u paint=%u %llx %016llx', D),
    ('ls-dump-fill-alpha-width', 'src', LS, '%016llx %016llx space=%u', '%016llx %llx space=%u', D),
    ('ls-dump-fill-components', 'src', LS, 'HEX(gshgeom_f64_bits(item->paint.rgba[1])), HEX(gshgeom_f64_bits(item->paint.rgba[2])),', 'HEX(gshgeom_f64_bits(item->paint.rgba[2])), HEX(gshgeom_f64_bits(item->paint.rgba[1])),', D),
    ('ls-dump-clip-tag', 'src', LS, "        written = fputc('C', out) == EOF ? -1 : dump_resource_and_map(out, item);", "        written = fputc('K', out) == EOF ? -1 : dump_resource_and_map(out, item);", D),
    ('ls-dump-clip-rule', 'src', LS, '          written = fprintf(out, " rule=%u", (unsigned)item->fill_rule);', '          written = fprintf(out, " rule=%u", 0u);', D),
    ('ls-dump-clip-pop-tag', 'src', LS, "        written = fputc('c', out) == EOF ? -1 : 0;", "        written = fputc('p', out) == EOF ? -1 : 0;", D),
    ('ls-dump-group-line', 'src', LS, '"G opacity=%016llx blend=%u order=%u layer=%lld"', '"G opacity=%llx blend=%u order=%u layer=%lld"', D),
    ('ls-dump-group-pop-tag', 'src', LS, "        written = fputc('g', out) == EOF ? -1 : 0;", "        written = fputc('p', out) == EOF ? -1 : 0;", D),
    ('ls-dump-instance-tag', 'src', LS, "        written = fputc('I', out) == EOF ? -1 : dump_resource_and_map(out, item);", "        written = fputc('i', out) == EOF ? -1 : dump_resource_and_map(out, item);", D),
    ('ls-dump-instance-layer', 'src', LS, '          written = fprintf(out, " layer=%lld", (long long)item->layer);', '          written = fprintf(out, " layer=%lld", 0LL);', D),
    ('ls-dump-final-error-flag', 'src', LS, '  return (fflush(out) == 0 && !ferror(out)) ? GSHSCENE_OK : GSHSCENE_ERR_IO;', '  return fflush(out) == 0 ? GSHSCENE_OK : GSHSCENE_ERR_IO;', D),
    ('ls-dump-final-flush', 'src', LS, '  return (fflush(out) == 0 && !ferror(out)) ? GSHSCENE_OK : GSHSCENE_ERR_IO;', '  return !ferror(out) ? GSHSCENE_OK : GSHSCENE_ERR_IO;', D),
    ('hdr-kind-path', 'hdr', HR, '  GSHSCENE_RESOURCE_PATH = 1,      ///< A model path.', '  GSHSCENE_RESOURCE_PATH = 8,      ///< A model path.', R + D),
    ('hdr-kind-glyph-run', 'hdr', HR, '  GSHSCENE_RESOURCE_GLYPH_RUN = 2, ///< Glyphs, by font and id or as outlines.', '  GSHSCENE_RESOURCE_GLYPH_RUN = 9, ///< Glyphs, by font and id or as outlines.', R + D),
    ('hdr-kind-picture', 'hdr', HR, '  GSHSCENE_RESOURCE_PICTURE = 3,   ///< A closed list, frozen: what an instance draws.', '  GSHSCENE_RESOURCE_PICTURE = 10,   ///< A closed list, frozen: what an instance draws.', R + D),
    ('hdr-kind-gradient', 'hdr', HR, '  GSHSCENE_RESOURCE_GRADIENT = 4,  ///< Reserved for story 12: no constructor yet.', '  GSHSCENE_RESOURCE_GRADIENT = 11,  ///< Reserved for story 12: no constructor yet.', R),
    ('hdr-kind-image', 'hdr', HR, '  GSHSCENE_RESOURCE_IMAGE = 5,     ///< Reserved for the raster stories: no constructor yet.', '  GSHSCENE_RESOURCE_IMAGE = 12,     ///< Reserved for the raster stories: no constructor yet.', R),
    ('hdr-source-font', 'hdr', HR, '  GSHSCENE_GLYPHS_FROM_FONT = 0,', '  GSHSCENE_GLYPHS_FROM_FONT = 1,', D + R),
    ('hdr-item-fill', 'hdr', HL, '  GSHSCENE_ITEM_FILL = 1,       ///< Fill a resource under a map with a paint.', '  GSHSCENE_ITEM_FILL = 7,       ///< Fill a resource under a map with a paint.', L + D),
    ('hdr-item-clip-push', 'hdr', HL, "  GSHSCENE_ITEM_CLIP_PUSH = 2,  ///< Intersect the clip with a resource's region.", "  GSHSCENE_ITEM_CLIP_PUSH = 8,  ///< Intersect the clip with a resource's region.", L + D),
    ('hdr-item-clip-pop', 'hdr', HL, '  GSHSCENE_ITEM_CLIP_POP = 3,   ///< End the innermost clip.', '  GSHSCENE_ITEM_CLIP_POP = 9,   ///< End the innermost clip.', L + D),
    ('hdr-item-group-push', 'hdr', HL, '  GSHSCENE_ITEM_GROUP_PUSH = 4, ///< Begin an opacity group.', '  GSHSCENE_ITEM_GROUP_PUSH = 10, ///< Begin an opacity group.', L + D),
    ('hdr-item-group-pop', 'hdr', HL, '  GSHSCENE_ITEM_GROUP_POP = 5,  ///< End the innermost group and composite it.', '  GSHSCENE_ITEM_GROUP_POP = 11,  ///< End the innermost group and composite it.', L + D),
    ('hdr-item-instance', 'hdr', HL, '  GSHSCENE_ITEM_INSTANCE = 6,   ///< Draw a picture under a map.', '  GSHSCENE_ITEM_INSTANCE = 12,   ///< Draw a picture under a map.', L + D),
    ('hdr-ordering-flat', 'hdr', HL, '  GSHSCENE_ORDER_FLAT = 0,    ///< Declared order.', '  GSHSCENE_ORDER_FLAT = 3,    ///< Declared order.', L),
    ('hdr-paint-solid', 'hdr', HP, '  GSHSCENE_PAINT_SOLID = 0,    ///< One color.', '  GSHSCENE_PAINT_SOLID = 3,    ///< One color.', L + D),
    ('hdr-paint-foreground', 'hdr', HP, '  GSHSCENE_PAINT_FOREGROUND = 2,', '  GSHSCENE_PAINT_FOREGROUND = 6,', L),
    ('hdr-paint-palette', 'hdr', HP, '  GSHSCENE_PAINT_PALETTE = 3,', '  GSHSCENE_PAINT_PALETTE = 7,', L),
    ('hdr-paint-gradient', 'hdr', HP, '  GSHSCENE_PAINT_GRADIENT = 1, ///< Reserved:', '  GSHSCENE_PAINT_GRADIENT = 4, ///< Reserved:', L),
]

# --- story 11: the constants, enumerations and spellings that no operator-level mutation reaches ----------------------
HV = "include/ghoti.io/shape-scene/version.h"
HE = "include/ghoti.io/shape-scene/edit.h"
HSC = "include/ghoti.io/shape-scene/scene.h"
HDF = "include/ghoti.io/shape-scene/diff.h"
VS = "src/scene/version.c"
DF = "src/scene/diff.c"
IH = "src/scene/internal.h"

PLANTS += [
    ('v-kind-group', 'hdr', HV, 'GSHSCENE_NODE_GROUP = 1,', 'GSHSCENE_NODE_GROUP = 11,', ["testNode", "testScene_dump"]),
    ('v-kind-shape', 'hdr', HV, 'GSHSCENE_NODE_SHAPE = 2,', 'GSHSCENE_NODE_SHAPE = 12,', ["testNode", "testScene_dump"]),
    ('v-kind-instance', 'hdr', HV, 'GSHSCENE_NODE_INSTANCE = 3,', 'GSHSCENE_NODE_INSTANCE = 13,', ["testNode", "testScene_dump"]),
    ('v-hidden-flag', 'hdr', HV, '#define GSHSCENE_NODE_HIDDEN ((uint32_t)1)', '#define GSHSCENE_NODE_HIDDEN ((uint32_t)2)', ["testNode", "testScene_dump"]),
    ('v-root-id', 'hdr', HV, '#define GSHSCENE_ROOT_ID ((uint64_t)1)', '#define GSHSCENE_ROOT_ID ((uint64_t)2)', ["testScene", "testNode"]),
    ('e-append', 'hdr', HE, '#define GSHSCENE_APPEND ((size_t)-1)', '#define GSHSCENE_APPEND ((size_t)-2)', ["testEdit", "testScene"]),
    ('s-reason-overlap', 'hdr', HSC, 'GSHSCENE_PROBLEM_OVERLAP = 1,', 'GSHSCENE_PROBLEM_OVERLAP = 50,', ["testScene"]),
    ('s-reason-key', 'hdr', HSC, 'GSHSCENE_PROBLEM_KEY_CONFLICT = 2,', 'GSHSCENE_PROBLEM_KEY_CONFLICT = 51,', ["testScene"]),
    ('s-reason-dangling', 'hdr', HSC, 'GSHSCENE_PROBLEM_DANGLING_TARGET = 3,', 'GSHSCENE_PROBLEM_DANGLING_TARGET = 52,', ["testScene"]),
    ('s-reason-cycle', 'hdr', HSC, 'GSHSCENE_PROBLEM_CYCLE = 4,', 'GSHSCENE_PROBLEM_CYCLE = 5,', ["testScene"]),
    ('d-kind-added', 'hdr', HDF, 'GSHSCENE_DIFF_ADDED = 1,', 'GSHSCENE_DIFF_ADDED = 14,', ["testDiff", "testScene"]),
    ('d-kind-removed', 'hdr', HDF, 'GSHSCENE_DIFF_REMOVED = 2,', 'GSHSCENE_DIFF_REMOVED = 15,', ["testDiff", "testScene"]),
    ('d-kind-changed', 'hdr', HDF, 'GSHSCENE_DIFF_CHANGED = 3,', 'GSHSCENE_DIFF_CHANGED = 16,', ["testDiff", "testScene"]),
    ('d-aa-default', 'hdr', HDF, '#define GSHSCENE_AA_SUPPORT_DEFAULT 1.0', '#define GSHSCENE_AA_SUPPORT_DEFAULT 2.0', ["testDiff"]),
    ('c-refused-value', 'hdr', HC, '  GSHSCENE_ERR_REFUSED,', '  GSHSCENE_ERR_REFUSED = 20,', C),
    ('c-default-bytes', 'hdr', HC, '#define GSHSCENE_LIMITS_DEFAULT_MAX_BYTES ((size_t)1 << 30)', '#define GSHSCENE_LIMITS_DEFAULT_MAX_BYTES ((size_t)1 << 29)', C),
    ('c-default-nodes', 'hdr', HC, '#define GSHSCENE_LIMITS_DEFAULT_MAX_NODES ((size_t)1 << 22)', '#define GSHSCENE_LIMITS_DEFAULT_MAX_NODES ((size_t)1 << 21)', C),
    ('c-default-node-depth', 'hdr', HC, '#define GSHSCENE_LIMITS_DEFAULT_MAX_NODE_DEPTH ((size_t)256)', '#define GSHSCENE_LIMITS_DEFAULT_MAX_NODE_DEPTH ((size_t)255)', C + ["testScene_limits"]),
    ('c-depth-ceiling', 'hdr', HC, '#define GSHSCENE_NODE_DEPTH_CEILING ((size_t)1024)', '#define GSHSCENE_NODE_DEPTH_CEILING ((size_t)1025)', C + ["testScene_limits"]),
    ('r-bytes-base', 'hdr', HR, '#define GSHSCENE_BYTES_BASE ((size_t)128)', '#define GSHSCENE_BYTES_BASE ((size_t)129)', ["testBytes"]),
    ('r-bytes-path', 'hdr', HR, '#define GSHSCENE_BYTES_PER_PATH_ELEMENT ((size_t)160)', '#define GSHSCENE_BYTES_PER_PATH_ELEMENT ((size_t)161)', ["testBytes"]),
    ('r-bytes-glyph', 'hdr', HR, '#define GSHSCENE_BYTES_PER_GLYPH ((size_t)32)', '#define GSHSCENE_BYTES_PER_GLYPH ((size_t)33)', ["testBytes"]),
    ('r-bytes-item', 'hdr', HR, '#define GSHSCENE_BYTES_PER_ITEM ((size_t)256)', '#define GSHSCENE_BYTES_PER_ITEM ((size_t)257)', ["testBytes"]),
    ('rs-bytes-path-uses-glyph-figure', 'src', RS, 'GSHSCENE_BYTES_PER_PATH_ELEMENT)),\n      path, build_path};', 'GSHSCENE_BYTES_PER_GLYPH)),\n      path, build_path};', ["testBytes"]),
    ('rs-bytes-run-uses-item-figure', 'src', RS, 'GSHSCENE_BYTES_PER_GLYPH)),\n      run, build_run};', 'GSHSCENE_BYTES_PER_ITEM)),\n      run, build_run};', ["testBytes"]),
    ('rs-bytes-picture-uses-glyph-figure', 'src', RS, 'GSHSCENE_BYTES_PER_ITEM)),\n      list, build_picture};', 'GSHSCENE_BYTES_PER_GLYPH)),\n      list, build_picture};', ["testBytes"]),
    ('rs-bytes-path-no-base', 'src', RS, 'gshscene_sat_add(GSHSCENE_BYTES_BASE,\n          gshscene_sat_mul(gshgeom_path_element_count(path)', 'gshscene_sat_add(0,\n          gshscene_sat_mul(gshgeom_path_element_count(path)', ["testBytes"]),
    ('rs-bytes-run-no-base', 'src', RS, 'gshscene_sat_add(GSHSCENE_BYTES_BASE, gshscene_sat_mul(run->glyph_count', 'gshscene_sat_add(0, gshscene_sat_mul(run->glyph_count', ["testBytes"]),
    ('rs-bytes-picture-no-base', 'src', RS, 'gshscene_sat_add(GSHSCENE_BYTES_BASE, gshscene_sat_mul(gshscene_list_count(list)', 'gshscene_sat_add(0, gshscene_sat_mul(gshscene_list_count(list)', ["testBytes"]),
]
PLANTS += [
    ('vs-format-version', 'src', IH, '#define VERSION_FORMAT_VERSION 1u', '#define VERSION_FORMAT_VERSION 2u', ["testScene_dump"]),
    ('vs-hash-kind', 'src', IH, '#define VERSION_HASH_KIND UINT32_C(0x4e454353)', '#define VERSION_HASH_KIND UINT32_C(0x4e454354)', ["testScene_dump"]),
    ('df-range-limit-high', 'src', DF, '#define RANGE_LIMIT 4.0e18', '#define RANGE_LIMIT 4.0e30', ["testDiff"]),
    ('df-range-limit-low', 'src', DF, '#define RANGE_LIMIT 4.0e18', '#define RANGE_LIMIT 4.0e3', ["testDiff"]),
]


# The generated plants (tools/gen-scene-plants.py): the mutations of src/scene
# the sweep was seen to catch.  PLANT_EXTRA_JSON names another file to run
# in addition, which is how a regenerated candidate list is triaged.
import json
for _file in [str(ROOT / "tools" / "scene-plants.json")] + (
        [os.environ["PLANT_EXTRA_JSON"]] if os.environ.get("PLANT_EXTRA_JSON") else []):
    with open(_file) as _f:
        PLANTS += [tuple(x) for x in json.load(_f)]


def run(cmd, shell=False, env_extra=None, cwd=None):
    environment = dict(os.environ)
    if env_extra:
        environment.update(env_extra)
    return subprocess.run(cmd, shell=shell, capture_output=True, text=True,
                          env=environment, cwd=cwd or ROOT)


def compile_all(destdir, srcs, extra=(), pre=()):
    """Compile every source to destdir/<name>.o; return the object list."""
    destdir.mkdir(parents=True, exist_ok=True)
    objs = []
    for src in srcs:
        obj = destdir / (src.replace("/", "_") + ".o")
        r = run(CC + list(pre) + CFLAGS + list(extra) + ["-c", src, "-o", str(obj)])
        if r.returncode != 0:
            raise RuntimeError("cannot compile %s:\n%s" % (src, r.stderr))
        objs.append(str(obj))
    return objs


def link_test(test, objs, out):
    cmd = "%s -o %s %s %s %s" % (CXX, out, test_object(test), " ".join(objs), LDFLAGS)
    r = run(cmd, shell=True)
    if r.returncode != 0:
        raise RuntimeError("cannot link %s:\n%s" % (test, r.stderr))


def _limits():
    """A planted defect may loop or allocate without end; bound both."""
    import resource
    limit = int(float(os.environ.get("PLANT_AS_GB", "3")) * (1 << 30))
    resource.setrlimit(resource.RLIMIT_AS, (limit, limit))
    resource.setrlimit(resource.RLIMIT_CPU, (120, 120))


def run_test(exe):
    environment = dict(os.environ)
    environment["LD_LIBRARY_PATH"] = LD_PATH
    try:
        r = subprocess.run([str(exe), "--gtest_brief=1"], capture_output=True, text=True,
                           env=environment, cwd=ROOT, timeout=180, preexec_fn=_limits)
    except subprocess.TimeoutExpired:
        return 1, ["[  FAILED  ] (timed out)"]
    failing = [l for l in (r.stdout + r.stderr).splitlines() if l.startswith("[  FAILED  ]")]
    return r.returncode, failing


def probe_hash(objs, d, extra=()):
    exe = d / "corpus-hash"
    cmd = "%s -o %s tools/corpus-hash.c %s %s" % (
        " ".join(CC + PROBE_CFLAGS + list(extra)), exe, " ".join(objs), PROBE_LIBS)
    r = run(cmd, shell=True)
    if r.returncode != 0:
        raise RuntimeError("cannot build the probe:\n%s" % r.stderr)
    c = run(["tools/check-corpus-hash.sh", str(exe), "tests/corpus/scene.hash"],
            env_extra={"LD_LIBRARY_PATH": LD_PATH})
    return c.returncode, (c.stdout + c.stderr).strip()


def replay_run(objs, d):
    """The fuzz replay over the committed corpus, built against the planted objects."""
    exe = d / "replay"
    cmd = "%s -o %s tests/fuzz/fuzz_record.c tests/fuzz/replay_main.c %s %s" % (
        " ".join(CC + PROBE_CFLAGS), exe, " ".join(objs), PROBE_LIBS)
    r = run(cmd, shell=True)
    if r.returncode != 0:
        raise RuntimeError("cannot build the replay:\n%s" % r.stderr)
    inputs = sorted(str(p) for p in (ROOT / "tests" / "fuzz" / "corpus").rglob("*") if p.is_file())
    environment = dict(os.environ)
    environment["LD_LIBRARY_PATH"] = LD_PATH
    try:
        c = subprocess.run([str(exe)] + inputs, capture_output=True, text=True, env=environment, cwd=ROOT, timeout=180,
                           preexec_fn=_limits)
    except subprocess.TimeoutExpired:
        return 1, "[  FAILED  ] replay timed out"
    return c.returncode, "replay: " + (c.stdout + c.stderr).strip().splitlines()[-1] if (c.stdout + c.stderr).strip() else "replay: no output"


def apply_line(text, spec, new, label):
    """A plant addressed by line: spec is "LINE:FRAGMENT"; FRAGMENT must occur exactly once on that line."""
    number, fragment = spec.split(":", 1)
    lines = text.split("\n")
    line = lines[int(number) - 1]
    n = line.count(fragment)
    if n != 1:
        raise RuntimeError("plant %s: the fragment occurs %d times on line %s, not once:\n%s\n%s" % (label, n, number, fragment, line))
    if fragment == new:
        raise RuntimeError("plant %s: the replacement changes nothing" % label)
    lines[int(number) - 1] = line.replace(fragment, new)
    return "\n".join(lines)


def apply(text, old, new, label):
    n = text.count(old)
    if n != 1:
        raise RuntimeError("plant %s: the text to replace occurs %d times, not once:\n%s" % (label, n, old))
    if old == new:
        raise RuntimeError("plant %s: the replacement changes nothing" % label)
    return text.replace(old, new)


def do_plant(plant, control_objs):
    pid, kind, fname, old, new, tests = plant
    d = WORK / pid
    shutil.rmtree(d, ignore_errors=True)
    d.mkdir(parents=True)
    srcs = sources()
    if kind in ("src", "srcl"):
        original = (ROOT / fname).read_text()
        text = apply(original, old, new, pid) if kind == "src" else apply_line(original, old, new, pid)
        planted = d / "planted" / fname
        planted.parent.mkdir(parents=True, exist_ok=True)
        # A source may include a neighbour by a relative path ("../exact/exact.h"),
        # so the planted file sits in a copy of the whole tree.
        shutil.copytree(ROOT / "src", d / "planted" / "src", dirs_exist_ok=True)
        planted.write_text(text)
        if fname.endswith(".h"):
            # A private header: every source that includes it is rebuilt from the planted copy.
            objs = compile_all(d / "obj", [str(d / "planted" / s) for s in srcs])
        else:
            obj = d / "planted.o"
            r = run(CC + CFLAGS + ["-c", str(planted), "-o", str(obj)])
            if r.returncode != 0:
                raise RuntimeError("plant %s does not compile:\n%s" % (pid, r.stderr))
            objs = [str(obj) if o.endswith(fname.replace("/", "_") + ".o") else o for o in control_objs]
            assert str(obj) in objs, pid
    else:
        text = apply((ROOT / fname).read_text(), old, new, pid)
        hdr = d / "include" / Path(fname).relative_to("include")
        shutil.copytree(ROOT / "include", d / "include")
        hdr.write_text(text)
        objs = compile_all(d / "obj", srcs, pre=["-I" + str(d / "include")])
    caught_by = []
    for test in tests:
        if test == "fuzz-replay":
            rc, msg = replay_run(objs, d)
            failing = [msg]
        elif test == "probe":
            rc, msg = probe_hash(objs, d)
            failing = [msg]
        else:
            exe = d / test
            link_test(test, objs, exe)
            rc, failing = run_test(exe)
        if rc != 0:
            caught_by.append((test, failing[:1]))
            break  # one failing test is what a plant needs; the rest would only cost time
    return pid, caught_by


def main():
    shutil.rmtree(WORK, ignore_errors=True)
    WORK.mkdir(parents=True)
    srcs = sources()
    failures = 0

    # The control: everything passes.
    control = compile_all(WORK / "control", srcs)
    for test in TESTS:
        exe = WORK / ("control-" + test)
        link_test(test, control, exe)
        rc, failing = run_test(exe)
        if rc != 0:
            print("FAIL  control: %s fails on the unplanted build: %s" % (test, failing))
            return 1
    rc, msg = probe_hash(control, WORK)
    if rc != 0:
        print("FAIL  control: the probe does not print the committed hash: %s" % msg)
        return 1
    rc, msg = replay_run(control, WORK)
    if rc != 0:
        print("FAIL  control: the fuzz replay fails on the unplanted build: %s" % msg)
        return 1
    print("ok    control: %d tests, the probe and the fuzz replay pass on the unplanted build" % len(TESTS))

    ids = [p[0] for p in PLANTS]
    if len(set(ids)) != len(ids):
        print("FAIL  duplicate plant ids")
        return 1
    plants = [p for p in PLANTS if not ONLY or re.search(ONLY, p[0])]
    if PROBE_ONLY:
        plants = []
    if not plants and not PROBE_ONLY:
        print("FAIL  PLANT_ONLY matches no plant")
        return 1

    with concurrent.futures.ThreadPoolExecutor(max_workers=JOBS) as pool:
        futures = {pool.submit(do_plant, p, control): p for p in plants}
        results = {}
        for fut in concurrent.futures.as_completed(futures):
            p = futures[fut]
            try:
                results[p[0]] = fut.result()[1]
            except Exception as e:  # a plant that cannot be built is a defect of the harness
                results[p[0]] = e
    for p in plants:
        r = results[p[0]]
        if isinstance(r, Exception):
            print("FAIL  %-34s %s" % (p[0], r))
            failures += 1
        elif not r:
            print("FAIL  %-34s not caught by %s" % (p[0], " ".join(p[5])))
            failures += 1
        else:
            print("ok    %-34s caught by %s" % (p[0], ", ".join("%s (%s)" % (t, (f[0][14:70] if f else "crash")) for t, f in r)))

    if ONLY and not PROBE_ONLY:
        print("%d plants run (PLANT_ONLY=%s), %d not caught" % (len(plants), ONLY, failures))
        return 1 if failures else 0

    # The probe's own plants.
    (WORK / "bit").mkdir(parents=True, exist_ok=True)
    for name, flag in (("probe-planted-bit", "-DSHAPE_SCENE_PLANT_BIT"),):
        (WORK / name).mkdir(parents=True, exist_ok=True)
        rc, msg = probe_hash(control, WORK / name, extra=[flag])
        if rc == 0:
            print("FAIL  %s: the probe printed the committed hash with a bit flipped" % name)
            failures += 1
        else:
            print("ok    %s: %s" % (name, msg.splitlines()[-1]))

    print("%d plants, %d not caught" % (len(PLANTS) + 1, failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
