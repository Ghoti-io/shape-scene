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
  cs-          (story 12) the color space: its validity rules and every bound of
               the gamma, the zero space, the mapping from a ``color`` description
               (each ICC field, the tolerance, the gamma), the default color
  orc-, tst-   (story 12) the reference and the evaluator of the compositing
               definition: each factor of each operator, the clamp of PLUS, the
               primitive's two terms, every step of a fill and of a group's
               composite (the clips, the space of the paint and of the group, the
               opacity, the operator, the foreground's alpha, color and space), the
               conversion (identity, decode, matrices and their order, encode, the
               un-premultiply), every constant of both transfers and of the four
               gamuts and the white point, and the order of a sequence (the
               direction of depth, the layer, stability, the ties).  An ``orc``
               plant must fail the oracle's selftest or its vectors against the
               committed ones; a ``tst`` plant recompiles the tests that include
               tests/ref_composite.h with the planted copy ahead
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
  - (story 12) in the order: the ``nonflat`` counter's reset by ``list_clear`` (a
    list reused after a reset with a stale count only does the sort's bookkeeping
    on a list that needs none), the frame bookkeeping that only moves memory
    (``base``, ``count``), and the comparator's third key (a tie of depth *and*
    layer is broken by the stable sort's declared order, not by a comparison, so
    a swapped tie in the comparator cannot show)
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

  gh-, gr-, cv-, gp-, gs-, gf-, gorc-, gtst-, and the rp- and ds- plants of
  gradients (color-paint story P1)
               the gradient resource and paint (G1 to G13): the pinned values
               and constants, every validity rule of the resource and its
               canonical form, the stable sort, the hash field by field and
               equality field by field (a planted collision makes equality the
               only witness), the bytes, the stops' conversion at draw time
               (gshscene_gradient_resolve: the interpolation space, the target's,
               the palette's bound, the alpha) and every constant of N2 and N3 in
               src/paint/convert.c, the predicate of what draws nothing (each
               degenerate case, each place of the chain, the order), the paint's
               references in a list and a node, its hash and dump, damage, the
               planning pass's refusal, approximation and notice (S17), the test
               sink's records, the float-free constructor's conversions; and the
               gradient oracle (``gorc``: its selftest or the vectors it writes)
               and the evaluator (``gtst``).  src/resource/gradient.c and
               src/paint/convert.c are also mutated by the generator (``bg-``,
               ``bv-``), and the files the story changed were swept again on its
               tree in ghoti-build:gcc16 (2026-10-21): 3278 candidates of
               src/list/list.c and fixed.c, src/resource/resource.c and
               gradient.c, src/paint/convert.c, src/scene/node.c, version.c and
               diff.c, src/sink/sink.c, dump_sink.c and report.c; 2472 caught
               (they replace those files' entries in scene-plants.json and
               sink-plants.json), 516 that do not compile dropped, 290 survivors.
               The survivors on the story's own lines are of the kinds listed for
               the earlier stories: a loop bound moved one past an array's end
               (the sort's passes, the geometry and matrix loops of convert.c),
               which ASan sees and a result does not; a free or a release deleted
               (``gshscene_gradient_free``, a node's gradient, the fixed
               constructor's scratch), a leak; the per-call checks of a dump that
               the final ``ferror`` makes redundant, and a width of a double's
               bits that no fixture has a leading zero in; the ``SIZE_MAX``
               overflow guards and ``gk >= 32`` (a kind is 1 to 3); terms of the
               test sink's workspace smaller than its slack; ``x < 0`` against
               ``x <= 0`` in the signed power (zero returns first) and ``entry + 1
               > need`` against ``>=`` (the same assignment); ``&&`` against
               ``||`` where only a fill can name a gradient; and the sRGB knees'
               ``<=`` against ``<`` (no vector lies exactly on a knee).  Hand
               plants dropped as nothing can see them: the resolve tolerance and
               the fixed sweep's constant (no library code reads them where a test
               can tell), the fixed fill's alpha range (the list's own check
               refuses the same values) and the vectors' rejection margin (no
               sample lies between 1e-300 and 1e-30 of a discontinuity)

  g14-         G14 (color-paint story P2): each bound's comparison (inclusive,
               missing, either side), the zero exemption, the absolute value,
               each place a bound is read (the geometry, the map's linear part
               and its translation, the offsets, the stop count) and the order
               after the validity checks, and the three constants the library
               reads.  The sampling domain's constant has no plant here: the
               library does not read it (shape-cpu does, and plants it).
               gradient.c was swept again (``bg-``, 365 candidates on the P2
               tree in ghoti-build:gcc16): 279 caught (they replace the file's
               entries in scene-plants.json), 58 that do not compile, 28
               survivors, all of P1's kinds above but two on G14's lines: the
               limit loop's bound one past the array (ASan's, not a result's) and
               ``x < 0`` against ``x <= 0`` in the absolute value (zero is
               exempt either way)

  n1-, tst-n1-, orc-n1-, gorc-n1-
               N1's bound and saturation (Corey, 2026-10-21): the bound's two
               comparisons (inclusive, missing, either side) and the components it
               reads, each entry point that reads it (the color check, a fill, a
               shape node's paint, a palette, a solid stop) and its order after the
               validity checks there, the two constants, the clamp's two sides, and
               each point the saturation is named in the resolve (decode, matrix,
               encode), the test evaluator (those, the un-premultiply and its guard,
               the apply, G4) and both oracles.  The oracle's own copy of the bound
               (``COMPONENT_MAX``) has no plant: only its selftest reads it, where
               any bound up to 2^64 saturates the same.  Two older plants were
               dropped as the sweep of the changed files found them equivalent:
               the evaluator's alpha-0 branch of a premultiplied conversion (with
               the saturation an alpha of 0 un-premultiplies to 2^64 and
               premultiplies back to 0, the same answer), and a resource dump's
               run format written as the resource format (both have been 1
               since every format was reset).  The changed files' generated
               plants were swept again (cs-, bv-, bg-, br-, bl-; 787 candidates
               in ghoti-build:gcc16): 152 do not compile, 580 are caught (they
               replace those files' entries in scene-plants.json) and 55
               survive, none on a changed line, the kinds listed below.

  orc-b-, tst-b-, orc-t-, tst-t-
               Color-paint story P3: in the oracle and the test evaluator, a defect
               in each blend mode (a branch, a constant, overlay's exchanged
               arguments, min for max), in Lum's coefficients, in ClipColor (the
               entry x against a recomputed one, the equal-component case, the
               low step's factor), in SetSat's order and scale, in each
               non-separable mode's arguments, in B9's saturation and the capped
               quotient, in B2's As Ab term, its alpha and its boundedness, in the
               un-premultiply and in B5's space (the oracle evaluating at the
               group's own space); and in the general model of T1 to T7 (the
               knockout backdrop, the group alpha's backdrop, the group shape's
               union, the removal of the initial backdrop and its alpha, a group
               element's shape for PDF's operators and for the others, the
               non-isolated initial backdrop and its conversion, the blend term,
               the alpha).  The story's library lines (the names, the group
               fields' validation, references, hash, dump, nodes, the planning
               pass's refusals) moved most lines of list.c, node.c, version.c,
               validate.c, sink.c, dump_sink.c and report.c: the generated plants
               on lines it left as they were were carried to their new numbers,
               and the 108 candidates of the lines it changed were swept in
               ghoti-build:gcc16: 14 do not compile, 91 are caught (added to
               scene-plants.json and sink-plants.json; three of them, a group
               node's soft mask not retained, not released, and not counted in
               the picture nesting at publish, after the tests that catch them
               were written) and 3 survive, each equivalent: two ``fprintf(...) <
               0`` against ``<= 0`` in the version dump (the text is never empty)
               and ``>`` against ``>=`` where the deepest picture is kept (equal
               depths store the same number).

  sk-, rp-, ds-, cn-, rule-, and the sink's hdr- and core- plants (story 13a)
               the sink contract: one named plant per S-rule (``rule-S1`` to
               ``rule-S15``, each failing the conformance suite or the contract's
               own tests), its pinned values and constants, and every generated
               mutation of src/sink the sweep caught (tools/sink-plants.json, from
               ``gen-scene-plants.py --sink``: 1444 candidates on story 14a's
               final tree, 1108 caught, 220 that do not compile dropped).  The 116
               survivors are of these kinds, none a result a caller can see (those
               on story 14a's lines of src/sink are listed with story 14a below).
               Story 16's decision on S9 (2026-10-21: a short caller-memory
               workspace refused without allocating, the query asking again for
               an exact need) changed src/sink/sink.c, swept again: 601
               candidates, 111 that do not compile, 462 caught (they replace
               sink.c's in tools/sink-plants.json; the query's cleared
               ``workspace_bytes`` was caught once the mock sink checked it), and
               28 survivors, of the kinds below and none on the decision's lines:

  - the locks of the sink's busy table and of the test sink's pending runs and
    cache (``GCU_MUTEX_LOCK``/``DESTROY`` deleted), and a loop bound moved one
    past the end of a table allocated to its full size (a read of a NULL slot):
    TSan and ASan see them, a result does not
  - frees on paths no test can reach: a cutil mutex that cannot fail to be made,
    a walk that cannot fail to start on memory sized for it; and the
    ``SIZE_MAX / sizeof`` overflow guards (2^61 jobs)
  - ties made unequal or equal where the two sides agree: ``max``/``min`` of
    equal values (box, rectangle, work and frame peaks), the report's growth to
    its cap, a sort key that two entries never share (one item is refused or
    approximated once, a unit is the first of at most one tie), and the
    order-of-addition key of entries equal in every field
  - stores the next statement or the allocator already made: a ``memset`` of caps
    every sink fills, the job's per-sink fields at ``begin`` (a sink sets them
    before it reads them), a box's corners when it is empty (never read), the
    walk's pending step at its end, the failure chain's room in the pool
  - guards a valid input cannot reach: an encoding past the enum (the kind's own
    check refuses it), a raster of zero width (``image`` refuses to make one),
    the walk's level guard (it is sized for the deepest picture), the empty
    rectangle met with the area inside ``stamp`` (every caller has met it with a
    clip inside the area already), the skipped unit's ``X`` text (the refusal
    that began the skip left the same text in the line), the low clamp of a box
    edge (the conversion of a value below the ``int32_t`` range gives
    ``INT32_MIN`` on every target, which is the clamp)
  - the workspace's terms (``Run``, the walk, the stop chain): each is smaller
    than the 320-byte line buffer at the end of the carving, which no record
    fills, so a missing term moves no write past the end (the conformance suite's
    canary is 4096 bytes after the workspace)
  - ``LINE_BYTES`` (no plant): GCC 16's ``-Wformat-truncation`` refuses, at
    compile time, any size below the longest record, and a larger one changes
    nothing
  - the 16-digit width of a hash that has no leading zero digit in any fixture
    (an instance's picture, a report's own hash)
  - ``r.target = &copy`` in the workspace query: the test sink's need does not
    depend on the target; resolving past the chain (a read of the pool's next
    index into the caller's path), which only ASan sees

  gl-, gh-, the rs- face and run plants, and rule-S6-glyph-* (story 14a)
               runs from a font: the face resource (its content, hash, dump,
               binding and lifetime), the run's face handle and hint version, the
               seam's pinned values and constants (gh-, in glyph_provider.h,
               report.h and resource.h), the route rule by rule, the snapped
               scale and map, the origin, the provider's answers made notes, the
               test sink's glyph records and scratch, the plan's routes through
               instances, and the damage of a run from a bound face; and every
               generated mutation of src/glyph/glyph.c and of the story's lines of
               src/sink and src/scene/diff.c the sweep caught.  On story 14a's
               final tree src/glyph/glyph.c gave 228 candidates: 155 caught
               (tools/sink-plants.json), 56 that do not compile, 17 survivors;
               src/scene/diff.c gave 288: 215 caught (tools/scene-plants.json), 44
               that do not compile, 29 survivors (the 26 outside the story's lines
               are of the kinds listed for df- above).  The survivors on the
               story's lines, by kind:

  - stores the next statement makes or nothing reads: the route's ``GENERAL``
    written over the zeroed route and again by ``general_for``; the ``memset``
    of the grid, the request, the box and a note where every field is then set
    or only read on success (``gshgeom_map_grid_aligned`` and the providers
    write all of theirs); a note's class and reason cleared before
    ``build_failure`` sets them; the test sink's box corners and note zeroed
    over a zeroed record; the scratch's ``used = 0`` at init over a run that
    is ``memset`` already
  - unreachable: ``gshgeom_map_grid_aligned`` failing on a map the route has
    checked; ``from_geometry``'s default; a device verb past the enum in
    ``moved``; a shape that is neither a path nor a glyph run in the damage
    walk; the scratch's ``malloc_fn`` (shape-geometry allocates only through
    ``calloc`` and ``realloc``)
  - loop bounds one past the end where the next read is refused or unread: a
    device element past the count (the accessor refuses it), a point past a
    verb's count (zero, and only the verb's points are used; ``j < 3`` already
    holds), the glyph box past the run (``gshscene_glyph_box`` refuses it)
  - ties: a point exactly at ``INT32_MIN`` or ``INT32_MAX`` after the whole
    pixel in ``moved``; a scratch block that fills the scratch to the byte; the
    copy's shorter length when the two are equal; ``calloc``'s overflow guard
    (a product past ``SIZE_MAX``)
  - ``r == OK && path`` for ``||``, and the destroy of a path in the glyph
    scratch: a path is set only on success, and the scratch frees by reset

  pal-, pal8-, res-, tr-, fx-, vp-, ep-, sp-, em-, the df- palette plants, and
  the orc- and tst- palette plants (story 14c)
               the palette (N14) and the float-free calls (fixed.h): the
               palette resource's hash, equality, copy, intern, its 8-bit
               constructor (the divisor, the channel, the space), readers and
               dump; the palette parameter of a list (set, retain, release,
               reset, read) and of a version (made, released, hashed, dumped,
               set by an edit, planned, merged, emitted, diffed); the paint's
               entry in the list's rules, hash, equality and dump; the one
               resolution and the planning pass's refusal of a missing entry;
               truncation, field by field; the design map, op by op (each
               value's place, each sign, each center, the tangent, the scale,
               the flips, the identity's translation) and the fixed paint
               (0xFFFF to the foreground, the alpha's bounds); and the
               reference's and the evaluator's resolution of an entry.  Not
               planted, because the call they guard repeats them: the fixed
               paint's alpha below zero and entry past 0xFFFF and the fixed
               fill's closed list (``gshscene_list_fill`` refuses each), the
               design map's font source (a run of another source has no face
               to read a upem from) and its quarter-turn tangent (the infinite
               skew fails ``gshgeom_map_from_matrix``), the palette
               constructor's NULL colors (``gshscene_color_check`` refuses a
               NULL color), the reference's entry-past-the-count test (its
               ``palette_color`` call refuses it), and the dump's check of the
               entry's write (the final ``ferror``, as above); nor the design
               map's return on a failed op, whose mutant composes a map never
               written (a read of uninitialised memory, which no result and no
               sanitizer here shows).  The generated mutations of the files the
               story changed were swept again on its final tree (the files of
               scene-plants.json and sink-plants.json that it touched, every
               candidate of each): src/scene, src/list (fixed.c new) and
               src/resource/resource.c gave 2048 candidates, 1619 caught, 251
               that do not compile, 178 survivors; src/sink 1449, 1111 caught,
               220 that do not compile, 118 survivors.  The survivors are of the
               kinds listed above; the ten on the story's own lines are a
               dump's ``< 0`` made ``<= 0`` (``fprintf`` of a non-empty text
               never returns 0), the default arms of ``from_geometry`` and
               ``op_map`` (every geometry error a valid op can meet is a limit,
               and the kind is checked first), the fixed group's
               ``GSHSCENE_ORDER_FLAT`` and the disposition's initialisation of
               the foreground it does not read (stores the ``memset`` made or
               nothing reads), truncation's reset of ``nonflat`` (bookkeeping,
               as for ``list_clear``) and a tie of picture depths

  ph1- to ph8- (color-paint story P5): the placeholder at the waist.  The pinned values
               of the item, node and reason kinds and of the registry's source and codes
               and their names; the record call's rules one by one (a zero source or
               code, an approximated flag past 1, a non-finite or inverted box, the sign
               of a zero, a 3D map, the layer); equality, hash and dump field by field;
               the float-free call (the box's scale, the frame, the ops without a frame,
               the bits); the planning pass (the class from the flag, the decision made
               before the caps and not from ``item_kinds``, the reason, ``second`` as
               ``source << 16 | code``, the entry skipped, written only under strict or
               keyed on the damage, a refusal counted or an approximation counted
               toward strict); the test sink's record (its letter, the pair, the box,
               the class, never a stamp); and the node (its canonical form, equality,
               hash, name, emission with its box, map, flag and origin, the dump
               letter, and the damage a placeholder does not make).  Written with the
               story and not swept: the sweep is in the consolidated pass
  sk1- to sk13-, s20-, rule-S20-, ds-s20-, and the bk- plants (strokes story S1)
               the stroke at the waist: the style's pinned values and bounds
               (stroke.h, list.h, version.h, resource.h, report.h, sink.h), its
               canonical form field by field (SK3: the sign of zero, an odd array
               doubled, a negative or all-zero array made solid, each field that
               does not apply reset), SK2's order (validity, then the profile,
               then the bounds), the hash's format, kind and order and the bytes
               per dash, SK5's pixel sizes, SK10's reach factor by factor (the
               miter, the miter-clip and arcs ``L + 1``, the square cap's 3/2,
               the half width, alignment's doubling, the pixel of a device width,
               the outward rounding and its upward step), SK9's order and each map
               of its chain, SK11's tables and anisotropy test (the reflection,
               the target, the instances, a degenerate subpath under a butt cap),
               S20 in the planning pass (the expand flag, the native bits, the
               chain, the detail and its reason, a stroke's paint checked as a
               fill's, the notice, the caps' validation) and in the test sink
               (what it expands, a style recorded once per render, its default
               caps, the notice, the unit).  ``GSHSCENE_STROKE_DASHES_MAX`` has
               no plant: SK3 doubles every odd array, so a canonical count is
               even and 65535 decides exactly what 65534 would.  The story's
               lines were swept on its tree in ghoti-build:gcc16 (2026-10-21):
               src/resource/stroke.c whole (``bk-``, new in tools/gen-scene-plants.py)
               and the lines it changed of list.c, resource.c, node.c, version.c,
               diff.c, emit.c, gradient.c, sink.c, dump_sink.c and report.c, 596
               candidates: 101 do not compile, 438 are caught (added to
               scene-plants.json and sink-plants.json; 44 of them after tests the
               first sweep showed missing: a run's glyphs as a centerline, a
               device outset past the largest double, a refusal while drawing
               under strict and in the record, an extent passed in one direction,
               a style recorded once, an empty and a gradient stroke's records,
               a zero dash's digits, and SK11's composed map, now zeroed before it
               is formed so that a map not written reads as no map rather than as
               the stack), and the 57 survivors are of the kinds above:
               stores the ``memset`` before them made (SK1's defaults that are
               zero, the canonical form's resets, the boxes), ties (``<`` against
               ``<=`` in a minimum, a maximum, the square cap's 3/2 and the
               element limit at 65535), the returned kind of a centerline that
               has no box (``UNSUPPORTED`` against ``INVALID``: every caller makes
               either unbounded), a dump's ``< 0`` against ``<= 0``, hex widths of
               values with no leading zero digit (a recorded style's width and
               miter limit are at least 2^-64 and 1, a hash), a loop bound one
               past the canonical dashes (ASan's), ``-0`` against ``0`` in an
               absolute value, a node's style release deleted (a leak), ``&&``
               against ``||`` where the other operand decides alike (a painted
               item's gradient, which canonical paint leaves NULL, and
               ``paint_gradient``'s, which the compiler folds), a leaf's skip
               reason, and the test sink's stroke count for its styles array,
               which precedes the glyph scratch that no stroke test fills to its
               end
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
# is how a changed constant disagrees with its pinned value); "tst" (a replacement
# in a header only the tests include, tests/ref_composite.h: the named tests are
# recompiled with it ahead on the include path); "orc" (a replacement in
# tools/composite-oracle.py: its selftest, or the vectors it writes against the
# committed ones, must fail).

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
    ('rs-hash-version-width', 'src', RS, '  gshgeom_hash_u32(h, kind_format(kind));', '  gshgeom_hash_u16(h, kind_format(kind));', D),
    # Every format is 1 until the suite's first release, so a run read with the resource format is the same
    # stream; raising the run's own constant is what shows the run reads it.
    ('rs-hash-run-format', 'src', RS, '#define RUN_FORMAT_VERSION 1u', '#define RUN_FORMAT_VERSION 2u', D),
    ('rs-hash-kind-width', 'src', RS, '  gshgeom_hash_u32(h, (uint32_t)kind);', '  gshgeom_hash_u8(h, (uint8_t)kind);', D),
    ('rs-hash-kind-first', 'src', RS, '  gshgeom_hash_u32(h, kind_format(kind));\n  gshgeom_hash_u32(h, (uint32_t)kind);', '  gshgeom_hash_u32(h, (uint32_t)kind);\n  gshgeom_hash_u32(h, kind_format(kind));', D),
    ('rs-hash-path-width', 'src', RS, '  gshgeom_hash_u64(&h, gshgeom_path_hash(path));', '  gshgeom_hash_u32(&h, (uint32_t)gshgeom_path_hash(path));', D),
    ('rs-hash-path-kind', 'src', RS, '  hash_begin(&h, GSHSCENE_RESOURCE_PATH);', '  hash_begin(&h, GSHSCENE_RESOURCE_PICTURE);', D),
    ('rs-hash-run-kind', 'src', RS, '  hash_begin(&h, GSHSCENE_RESOURCE_GLYPH_RUN);', '  hash_begin(&h, GSHSCENE_RESOURCE_PATH);', D),
    ('rs-hash-picture-kind', 'src', RS, '  hash_begin(&h, GSHSCENE_RESOURCE_PICTURE);', '  hash_begin(&h, GSHSCENE_RESOURCE_PATH);', D),
    ('rs-hash-run-source-width', 'src', RS, '  gshgeom_hash_u32(&h, (uint32_t)run->source);', '  gshgeom_hash_u8(&h, (uint8_t)run->source);', D),
    ('rs-hash-run-face-hash-width', 'src', RS, '  gshgeom_hash_u64(&h, outline_hash(run->face));', '  gshgeom_hash_u32(&h, (uint32_t)outline_hash(run->face));', D),
    ('rs-hash-run-face-dropped', 'src', RS, '  gshgeom_hash_u64(&h, outline_hash(run->face));\n', '', D),
    ('rs-hash-run-version-width', 'src', RS, '  gshgeom_hash_u32(&h, run->hint_version);', '  gshgeom_hash_u16(&h, (uint16_t)run->hint_version);', D),
    ('rs-hash-run-size', 'src', RS, '  gshgeom_hash_f64(&h, run->size);', '  gshgeom_hash_f64(&h, run->size + 1.0);', D),
    ('rs-hash-run-hint-width', 'src', RS, '  gshgeom_hash_u32(&h, run->hint_mode);', '  gshgeom_hash_u8(&h, (uint8_t)run->hint_mode);', D),
    ('rs-hash-run-version-dropped', 'src', RS, '  gshgeom_hash_u32(&h, run->hint_version);\n', '', D),
    ('rs-hash-run-count-width', 'src', RS, '  gshgeom_hash_u64(&h, (uint64_t)run->glyph_count);', '  gshgeom_hash_u32(&h, (uint32_t)run->glyph_count);', D),
    ('rs-hash-run-order', 'src', RS, '  gshgeom_hash_u32(&h, run->hint_mode);\n  gshgeom_hash_u32(&h, run->hint_version);', '  gshgeom_hash_u32(&h, run->hint_version);\n  gshgeom_hash_u32(&h, run->hint_mode);', D),
    ('rs-hash-face-kind', 'src', RS, '  hash_begin(&h, GSHSCENE_RESOURCE_FACE);', '  hash_begin(&h, GSHSCENE_RESOURCE_PATH);', D),
    ('rs-hash-face-key-width', 'src', RS, '  gshgeom_hash_u64(&h, font_key);', '  gshgeom_hash_u32(&h, (uint32_t)font_key);', D),
    ('rs-hash-face-index-dropped', 'src', RS, '  gshgeom_hash_u32(&h, face_index);\n', '  (void)face_index;\n', D),
    ('rs-hash-face-count-dropped', 'src', RS, '  gshgeom_hash_u32(&h, count);\n', '', D),
    ('rs-hash-face-coord-unsigned', 'src', RS, '    gshgeom_hash_u32(&h, (uint32_t)(int32_t)coords[i]);', '    gshgeom_hash_u32(&h, (uint32_t)(uint16_t)coords[i]);', D),
    ('rs-hash-face-last-coord', 'src', RS, '  for (uint32_t i = 0; i < count; i++) {\n    gshgeom_hash_u32(&h, (uint32_t)(int32_t)coords[i]);', '  for (uint32_t i = 0; i + 1 < count; i++) {\n    gshgeom_hash_u32(&h, (uint32_t)(int32_t)coords[i]);', D),
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
    ('rs-run-eq-face', 'src', RS, 'a->face != b->face ||', '0 ||', R),
    ('rs-run-eq-version', 'src', RS, 'a->hint_version != b->hint_version ||', '0 ||', R),
    ('rs-run-eq-size', 'src', RS, 'a->face != b->face || gshgeom_f64_bits(a->size) != gshgeom_f64_bits(b->size) ||', 'a->face != b->face || 0 ||', R),
    ('rs-run-eq-hint', 'src', RS, 'a->hint_mode != b->hint_mode ||', '0 ||', R),
    ('rs-match-face-always', 'src', RS, '      if (have->font_key != want->font_key || have->face_index != want->face_index ||\n          have->coord_count != want->coord_count) {\n        return 0;\n      }', '', R),
    ('rs-match-face-key', 'src', RS, 'have->font_key != want->font_key || have->face_index', '0 || have->face_index', R),
    ('rs-match-face-index', 'src', RS, 'have->face_index != want->face_index ||', '0 ||', R),
    ('rs-match-face-count', 'src', RS, '          have->coord_count != want->coord_count) {', '          0) {', R + ['testGlyph']),
    ('rs-match-face-coords', 'src', RS, '        if (have->coords[i] != want->coords[i]) {', '        if (0) {', R),
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
    ('rs-run-face-null', 'src', RS, '    if (!run->face || run->face->store != store', '    if (run->face->store != store', R),
    ('rs-run-face-store', 'src', RS, '!run->face || run->face->store != store || run->face->kind != GSHSCENE_RESOURCE_FACE ||', '!run->face || run->face->kind != GSHSCENE_RESOURCE_FACE ||', R),
    ('rs-run-face-kind', 'src', RS, 'run->face->store != store || run->face->kind != GSHSCENE_RESOURCE_FACE ||', 'run->face->store != store ||', R),
    ('rs-run-hint-mode-range', 'src', RS, '        run->hint_mode >= (uint32_t)GSHSCENE_HINT_MODE_COUNT) {', '        run->hint_mode > (uint32_t)GSHSCENE_HINT_MODE_COUNT) {', R),
    ('rs-run-hint-mode-unchecked', 'src', RS, ' ||\n        run->hint_mode >= (uint32_t)GSHSCENE_HINT_MODE_COUNT) {', ') {', R),
    ('rs-run-size-positive', 'src', RS, '|| !(run->size > 0.0) ||', '|| (run->size < 0.0) ||', R),
    ('rs-run-size-limit', 'src', RS, '|| run->size > GSHGEOM_COORD_LIMIT ||', '|| run->size >= GSHGEOM_COORD_LIMIT ||', R),
    ('rs-run-size-limit-unchecked', 'src', RS, ' || run->size > GSHGEOM_COORD_LIMIT ||', ' ||', R),
    ('rs-run-outline-face', 'src', RS, '  } else if (run->face || gshgeom_f64_bits(run->size) != 0', '  } else if (gshgeom_f64_bits(run->size) != 0', R),
    ('rs-run-outline-version', 'src', RS, 'run->hint_mode != 0 || run->hint_version != 0) {', 'run->hint_mode != 0) {', R),
    ('rs-run-outline-size', 'src', RS, 'run->face || gshgeom_f64_bits(run->size) != 0 ||', 'run->face || 0 ||', R),
    ('rs-run-outline-size-numeric', 'src', RS, 'run->face || gshgeom_f64_bits(run->size) != 0 ||', 'run->face || run->size != 0.0 ||', R),
    ('rs-run-outline-hint', 'src', RS, ' || run->hint_mode != 0 || run->hint_version != 0) {', ' || run->hint_version != 0) {', R),
    ('rs-run-face-retained', 'src', RS, '  gshscene_resource_retain(run->face);\n', '', R),
    ('rs-run-face-released', 'src', RS, '      gshscene_resource_release(resource->u.glyphs.run.face);\n', '', R + ['testGlyph']),
    ('rs-run-face-kept', 'src', RS, '  resource->u.glyphs.run.face = run->face;\n', '', R),
    ('rs-name-face', 'src', RS, '      return "face";', '      return "fact";', R + D),
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
    ('rs-dump-run-line', 'src', RS, '"run source=%u face=%016llx size=%016llx hint=%u version=%u glyphs=%llu\\n"', '"run source=%u face=%016llx size=%016llx hint=%u version=%u count=%llu\\n"', D),
    ('rs-dump-run-face-width', 'src', RS, '"run source=%u face=%016llx size', '"run source=%u face=%llx size', D),
    ('rs-dump-run-size-width', 'src', RS, 'size=%016llx hint=%u', 'size=%llx hint=%u', D),
    ('rs-dump-face-line', 'src', RS, '"face font=%016llx index=%u coords=%u"', '"face font=%016llx index=%u axes=%u"', D),
    ('rs-dump-face-key-width', 'src', RS, '"face font=%016llx index', '"face font=%llx index', D),
    ('rs-dump-face-coord-sign', 'src', RS, '        if (fprintf(out, " %d", (int)face->coords[i]) < 0) {', '        if (fprintf(out, " %u", (unsigned)(uint16_t)face->coords[i]) < 0) {', D),
    ('rs-dump-face-binding', 'src', RS, '"\\nbound=%d upem=%u flags=%u\\n"', '"\\nbound=%d flags=%u upem=%u\\n"', D + ['testGlyph']),
    ('rs-dump-glyph-line', 'src', RS, '"g id=%u x=%016llx y=%016llx outline=%016llx\\n"', '"g id=%u x=%016llx y=%016llx outline=%llx\\n"', D),
    ('rs-dump-glyph-x-width', 'src', RS, '"g id=%u x=%016llx y=%016llx', '"g id=%u x=%llx y=%016llx', D),
    ('rs-dump-glyph-y-width', 'src', RS, 'y=%016llx outline', 'y=%llx outline', D),
    ('rs-dump-final-error-flag', 'src', RS, '  return (fflush(out) == 0 && !ferror(out)) ? GSHSCENE_OK : GSHSCENE_ERR_IO;', '  return fflush(out) == 0 ? GSHSCENE_OK : GSHSCENE_ERR_IO;', D),
    ('rs-dump-final-flush', 'src', RS, '  return (fflush(out) == 0 && !ferror(out)) ? GSHSCENE_OK : GSHSCENE_ERR_IO;', '  return !ferror(out) ? GSHSCENE_OK : GSHSCENE_ERR_IO;', D),
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
    ('ls-reset-null', 'src', LS, 'GSHSCENE_Result gshscene_list_reset(GSHSCENE_List * list) {\n  if (!list) {\n    return GSHSCENE_ERR_INVALID;\n  }\n  list_clear(list);', 'GSHSCENE_Result gshscene_list_reset(GSHSCENE_List * list) {\n  list_clear(list);', L),
    ('ls-destroy-null', 'src', LS, 'void gshscene_list_destroy(GSHSCENE_List * list) {\n  if (!list) {\n    return;\n  }', 'void gshscene_list_destroy(GSHSCENE_List * list) {', L),
    ('ls-destroy-items', 'src', LS, '  items_release(list);\n  gshscene_resource_release(list->palette);\n  chunks_free(list, list->head, NULL);', '  gshscene_resource_release(list->palette);\n  chunks_free(list, list->head, NULL);', L),
    ('ls-destroy-store-ref', 'src', LS, '  gshscene_free(&alloc, list);\n  gshscene_store_unref(store);', '  gshscene_free(&alloc, list);\n  (void)store;', L),
    ('ls-destroy-list', 'src', LS, '  gshscene_free(&alloc, list);\n  gshscene_store_unref(store);', '  (void)alloc;\n  gshscene_store_unref(store);', L),
    ('ls-item-at-q', 'src', LS, '  size_t q = index / GSHSCENE_SEGMENT_FIRST + 1;', '  size_t q = index / GSHSCENE_SEGMENT_FIRST;', L),
    ('ls-item-at-loop', 'src', LS, '  while ((q >> (s + 1)) != 0) {', '  while ((q >> s) != 0) {', L),
    ('ls-item-at-base', 'src', LS, '  size_t base = GSHSCENE_SEGMENT_FIRST * (((size_t)1 << s) - 1);', '  size_t base = GSHSCENE_SEGMENT_FIRST * ((size_t)1 << s);', L),
    ('ls-items-release-all', 'src', LS, '  for (size_t i = 0; i < list->count; i++) {\n    item_release(item_at(list, i));', '  for (size_t i = 1; i < list->count; i++) {\n    item_release(item_at(list, i));', L),
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
    ('ls-append-no-count', 'src', LS, '  list->count++;\n  list->expansion = total;', '  list->expansion = total;', L),
    ('ls-append-no-expansion', 'src', LS, '  list->expansion = total;\n', '', L),
    ('ls-append-picture-depth-compare', 'src', LS, '  if (item_picture_depth(item) > list->picture_depth) {', '  if (item_picture_depth(item) < list->picture_depth) {', R),
    ('ls-append-picture-depth-store', 'src', LS, '    list->picture_depth = item_picture_depth(item);', '    list->picture_depth = 0;', R),
    ('ls-append-picture-depth-kind', 'src', LS, '  if (item->kind == GSHSCENE_ITEM_INSTANCE) {\n    return item->resource->u.picture.depth;', '  if (item->resource) {\n    return item->resource->u.picture.depth;', L + R),
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
    ('ls-paint-null', 'src', LS, '  if (!paint) {\n    return GSHSCENE_OK;\n  }\n  if (paint->kind != GSHSCENE_PAINT_SOLID &&', '  if (paint->kind != GSHSCENE_PAINT_SOLID &&', L),
    ('ls-paint-alpha-low', 'src', LS, '  if (!(paint->rgba[3] >= 0.0 && paint->rgba[3] <= 1.0)) {', '  if (!(paint->rgba[3] > 0.0 && paint->rgba[3] <= 1.0)) {', L),
    ('ls-paint-alpha-high', 'src', LS, '  if (!(paint->rgba[3] >= 0.0 && paint->rgba[3] <= 1.0)) {', '  if (!(paint->rgba[3] >= 0.0 && paint->rgba[3] < 1.0)) {', L),
    ('ls-paint-alpha-unchecked', 'src', LS, '  if (!(paint->rgba[3] >= 0.0 && paint->rgba[3] <= 1.0)) {', '  if (0) {', L),
    ('ls-paint-copy', 'src', LS, '  for (int i = 0; i < 4; i++) {\n    out->rgba[i] = paint->rgba[i];', '  for (int i = 0; i < 3; i++) {\n    out->rgba[i] = paint->rgba[i];', L),
    ('ls-group-null', 'src', LS, '  if (!group) {\n    return GSHSCENE_OK;\n  }\n  if (!gshscene_finite(group->opacity)', '  if (!gshscene_finite(group->opacity)', L),
    ('ls-group-low', 'src', LS, '!(group->opacity >= 0.0 && group->opacity <= 1.0)) {', '!(group->opacity > 0.0 && group->opacity <= 1.0)) {', L),
    ('ls-group-high', 'src', LS, '!(group->opacity >= 0.0 && group->opacity <= 1.0)) {', '!(group->opacity >= 0.0 && group->opacity < 1.0)) {', L),
    ('ls-stack-depth-inclusive', 'src', LS, '  if (list->depth >= list->limits.max_depth) {', '  if (list->depth > list->limits.max_depth) {', L),
    ('ls-stack-depth-ignored', 'src', LS, '  if (list->depth >= list->limits.max_depth) {\n    return GSHSCENE_ERR_LIMIT;\n  }\n  if (list->depth < list->stack_capacity) {', '  if (list->depth < list->stack_capacity) {', L),
    ('ls-stack-capacity-check', 'src', LS, '  if (list->depth < list->stack_capacity) {\n    return GSHSCENE_OK;\n  }', '', L),
    ('ls-stack-alloc-error', 'src', LS, '  GSHSCENE_Result result = arena_alloc(list, capacity, &memory);\n  if (result != GSHSCENE_OK) {\n    return result;\n  }', '  GSHSCENE_Result result = arena_alloc(list, capacity, &memory);\n  (void)result;', L),
    ('ls-stack-copy', 'src', LS, '  if (list->depth) {\n    memcpy(memory, list->stack, list->depth);\n  }', '', L),
    ('ls-stack-installed', 'src', LS, '  list->stack = (uint8_t *)memory;\n  list->stack_capacity = capacity;', '  list->stack_capacity = capacity;', L),
    ('ls-fill-resource-check', 'src', LS, '  GSHSCENE_Result result = check_resource(list, resource, TAKES_PATH | TAKES_RUN);\n  if (result == GSHSCENE_OK) {\n    result = canonical_map(map, &item.map);\n  }\n  if (result == GSHSCENE_OK) {\n    result = check_rule(rule);\n  }\n  if (result == GSHSCENE_OK) {\n    result = canonical_paint(list->store, paint, &item.paint);', '  GSHSCENE_Result result = GSHSCENE_OK;\n  if (result == GSHSCENE_OK) {\n    result = canonical_map(map, &item.map);\n  }\n  if (result == GSHSCENE_OK) {\n    result = check_rule(rule);\n  }\n  if (result == GSHSCENE_OK) {\n    result = canonical_paint(list->store, paint, &item.paint);', L),
    ('ls-fill-map-check', 'src', LS, '  if (result == GSHSCENE_OK) {\n    result = canonical_map(map, &item.map);\n  }\n  if (result == GSHSCENE_OK) {\n    result = check_rule(rule);\n  }\n  if (result == GSHSCENE_OK) {\n    result = canonical_paint', '  if (result == GSHSCENE_OK) {\n    canonical_map(map, &item.map);\n  }\n  if (result == GSHSCENE_OK) {\n    result = check_rule(rule);\n  }\n  if (result == GSHSCENE_OK) {\n    result = canonical_paint', L),
    ('ls-fill-rule-check', 'src', LS, '  if (result == GSHSCENE_OK) {\n    result = check_rule(rule);\n  }\n  if (result == GSHSCENE_OK) {\n    result = canonical_paint', '  if (result == GSHSCENE_OK) {\n    check_rule(rule);\n  }\n  if (result == GSHSCENE_OK) {\n    result = canonical_paint', L),
    ('ls-fill-paint-check', 'src', LS, '    result = check_rule(rule);\n  }\n  if (result == GSHSCENE_OK) {\n    result = canonical_paint(list->store, paint, &item.paint);\n  }', '    result = check_rule(rule);\n  }\n  if (result == GSHSCENE_OK) {\n    canonical_paint(list->store, paint, &item.paint);\n  }', L),
    ('ls-fill-resource', 'src', LS, '  item.resource = resource;\n  item.fill_rule = rule;\n  item.layer = layer;\n  return list_append(list, &item);\n}\n\n/* SK4', '  item.fill_rule = rule;\n  item.layer = layer;\n  return list_append(list, &item);\n}\n\n/* SK4', L),
    ('ls-fill-rule', 'src', LS, '  item.resource = resource;\n  item.fill_rule = rule;\n  item.layer = layer;\n  return list_append(list, &item);\n}\n\n/* SK4', '  item.resource = resource;\n  item.layer = layer;\n  return list_append(list, &item);\n}\n\n/* SK4', L),
    ('ls-fill-layer', 'src', LS, '  item.resource = resource;\n  item.fill_rule = rule;\n  item.layer = layer;\n  return list_append(list, &item);\n}\n\n/* SK4', '  item.resource = resource;\n  item.fill_rule = rule;\n  (void)layer;\n  return list_append(list, &item);\n}\n\n/* SK4', L),
    ('ls-clip-resource-check', 'src', LS, '  item_init(&item, GSHSCENE_ITEM_CLIP_PUSH);\n  GSHSCENE_Result result = check_resource(list, resource, TAKES_PATH | TAKES_RUN);', '  item_init(&item, GSHSCENE_ITEM_CLIP_PUSH);\n  GSHSCENE_Result result = GSHSCENE_OK;', L),
    ('ls-clip-push-kind', 'src', LS, '    list->stack[list->depth++] = PUSH_CLIP;', '    list->stack[list->depth++] = PUSH_GROUP;', L),
    ('ls-clip-push-uncounted', 'src', LS, '    list->stack[list->depth++] = PUSH_CLIP;', '    list->stack[list->depth] = PUSH_CLIP;', L),
    ('ls-pop-null', 'src', LS, '  if (!list || list->closed || list->depth == 0 || list->stack[list->depth - 1] != push) {', '  if (list->closed || list->depth == 0 || list->stack[list->depth - 1] != push) {', L),
    ('ls-pop-empty', 'src', LS, '  if (!list || list->closed || list->depth == 0 || list->stack[list->depth - 1] != push) {', '  if (!list || list->closed || list->stack[list->depth - 1] != push) {', L),
    ('ls-pop-kind', 'src', LS, '  if (!list || list->closed || list->depth == 0 || list->stack[list->depth - 1] != push) {', '  (void)push;\n  if (!list || list->closed || list->depth == 0) {', L),
    ('ls-pop-depth', 'src', LS, '  if (result == GSHSCENE_OK) {\n    list->depth--;\n  }', '', L),
    ('ls-pop-item-kind', 'src', LS, '  return pop(list, PUSH_CLIP, GSHSCENE_ITEM_CLIP_POP);', '  return pop(list, PUSH_CLIP, GSHSCENE_ITEM_GROUP_POP);', L),
    ('ls-group-pop-item-kind', 'src', LS, '  return pop(list, PUSH_GROUP, GSHSCENE_ITEM_GROUP_POP);', '  return pop(list, PUSH_GROUP, GSHSCENE_ITEM_CLIP_POP);', L),
    ('ls-group-push-null-list', 'src', LS, 'GSHSCENE_Result gshscene_list_group_push(GSHSCENE_List * list, const GSHSCENE_Group * group, int32_t layer, double depth) {\n  if (!list || list->closed) {', 'GSHSCENE_Result gshscene_list_group_push(GSHSCENE_List * list, const GSHSCENE_Group * group, int32_t layer, double depth) {\n  if (list->closed) {', L),
    ('ls-group-push-closed', 'src', LS, 'GSHSCENE_Result gshscene_list_group_push(GSHSCENE_List * list, const GSHSCENE_Group * group, int32_t layer, double depth) {\n  if (!list || list->closed) {', 'GSHSCENE_Result gshscene_list_group_push(GSHSCENE_List * list, const GSHSCENE_Group * group, int32_t layer, double depth) {\n  if (!list) {', L),
    ('ls-group-push-layer', 'src', LS, '  item.layer = layer;\n  result = list_append(list, &item);\n  if (result == GSHSCENE_OK) {\n    list->stack[list->depth++] = PUSH_GROUP;', '  (void)layer;\n  result = list_append(list, &item);\n  if (result == GSHSCENE_OK) {\n    list->stack[list->depth++] = PUSH_GROUP;', L),
    ('ls-group-push-kind', 'src', LS, '    list->stack[list->depth++] = PUSH_GROUP;', '    list->stack[list->depth++] = PUSH_CLIP;', L),
    ('ls-instance-resource-check', 'src', LS, '  GSHSCENE_Result result = check_resource(list, picture, TAKES_PICTURE);', '  GSHSCENE_Result result = GSHSCENE_OK;', L),
    ('ls-instance-resource', 'src', LS, '  item.resource = picture;\n  item.layer = layer;', '  item.layer = layer;', L),
    ('ls-instance-layer', 'src', LS, '  item.resource = picture;\n  item.layer = layer;', '  item.resource = picture;\n  (void)layer;', L),
    ('ls-is-closed-null', 'src', LS, '  return list ? list->closed : 0;', '  return list->closed;', L),
    ('ls-count-null', 'src', LS, '  return list ? list->count : 0;', '  return list->count;', L),
    ('ls-item-null', 'src', LS, '  if (!list || !out || index >= list->count) {', '  if (!out || index >= list->count) {', L),
    ('ls-item-out-null', 'src', LS, '  if (!list || !out || index >= list->count) {', '  if (!list || index >= list->count) {', L),
    ('ls-item-index', 'src', LS, '  if (!list || !out || index >= list->count) {', '  if (!list || !out || index > list->count) {', L),
    ('ls-expansion-null', 'src', LS, '  return list ? list->expansion : 0;', '  return list->expansion;', L),
    ('ls-bytes-null', 'src', LS, '  return list ? list->bytes : 0;', '  return list->bytes;', L),
    ('ls-copy-limits', 'src', LS, 'gshscene_list_create_effective(alloc, &source->limits, source->store, &copy);', 'gshscene_list_create_effective(alloc, &(GSHSCENE_Limits){0}, source->store, &copy);', R),
    ('ls-copy-last-item', 'src', LS, '  for (size_t i = 0; result == GSHSCENE_OK && i < source->count; i++) {\n    result = list_append(copy, item_at(source, i));', '  for (size_t i = 0; result == GSHSCENE_OK && i + 1 < source->count; i++) {\n    result = list_append(copy, item_at(source, i));', R),
    ('ls-copy-closed', 'src', LS, '  copy->closed = 1;\n  *out = copy;', '  *out = copy;', R),
    ('ls-eq-map-dim', 'src', LS, '  if (a->dim != b->dim || a->kind != b->kind) {', '  if (a->kind != b->kind) {', L),
    ('ls-eq-map-kind', 'src', LS, '  if (a->dim != b->dim || a->kind != b->kind) {', '  if (a->dim != b->dim) {', L),
    ('ls-eq-map-rows', 'src', LS, '  for (int i = 0; i < a->dim; i++) {\n    for (int j = 0; j < a->dim; j++) {\n      if (gshgeom_f64_bits(a->m[i][j]) != gshgeom_f64_bits(b->m[i][j])) {', '  for (int i = 0; i < 2; i++) {\n    for (int j = 0; j < a->dim; j++) {\n      if (gshgeom_f64_bits(a->m[i][j]) != gshgeom_f64_bits(b->m[i][j])) {', L),
    ('ls-eq-map-cols', 'src', LS, '    for (int j = 0; j < a->dim; j++) {\n      if (gshgeom_f64_bits(a->m[i][j]) != gshgeom_f64_bits(b->m[i][j])) {', '    for (int j = 0; j < 2; j++) {\n      if (gshgeom_f64_bits(a->m[i][j]) != gshgeom_f64_bits(b->m[i][j])) {', L),
    ('ls-eq-map-matrix', 'src', LS, '      if (gshgeom_f64_bits(a->m[i][j]) != gshgeom_f64_bits(b->m[i][j])) {\n        return 0;\n      }\n', '', L),
    ('ls-eq-map-translation', 'src', LS, '    if (gshgeom_f64_bits(a->t[i]) != gshgeom_f64_bits(b->t[i])) {\n      return 0;\n    }\n', '', L),
    ('ls-eq-map-translation-numeric', 'src', LS, '    if (gshgeom_f64_bits(a->t[i]) != gshgeom_f64_bits(b->t[i])) {', '    if (a->t[i] != b->t[i]) {', L),
    ('ls-eq-resource', 'src', LS, 'a->resource != b->resource || a->style', 'a->style', L),
    ('ls-eq-map', 'src', LS, '!map_equal(&a->map, &b->map) ||\n      a->fill_rule', 'a->fill_rule', L),
    ('ls-eq-rgba', 'src', LS, '    if (gshgeom_f64_bits(a->paint.rgba[i]) != gshgeom_f64_bits(b->paint.rgba[i])) {\n      return 0;\n    }\n', '', L),
    ('ls-eq-rgba-numeric', 'src', LS, '    if (gshgeom_f64_bits(a->paint.rgba[i]) != gshgeom_f64_bits(b->paint.rgba[i])) {', '    if (a->paint.rgba[i] != b->paint.rgba[i]) {', L),
    ('ls-eq-rgba-loop', 'src', LS, '  for (int i = 0; i < 4; i++) {\n    if (gshgeom_f64_bits(a->paint.rgba[i])', '  for (int i = 0; i < 3; i++) {\n    if (gshgeom_f64_bits(a->paint.rgba[i])', L),
    ('ls-eq-opacity-numeric', 'src', LS, '  return gshgeom_f64_bits(a->group.opacity) == gshgeom_f64_bits(b->group.opacity) &&', '  return a->group.opacity == b->group.opacity &&', L),
    ('ls-eq-items-ignored', 'src', LS, '    if (!item_equal(item_at(a, i), item_at(b, i))) {\n      return 0;\n    }\n', '', L),
    ('ls-hash-null', 'src', LS, '  if (!list) {\n    return gshgeom_hash_value(&h);\n  }\n  gshgeom_hash_u32(&h, LIST_FORMAT_VERSION);', '  gshgeom_hash_u32(&h, LIST_FORMAT_VERSION);', L),
    ('ls-hash-version-width', 'src', LS, '  gshgeom_hash_u32(&h, LIST_FORMAT_VERSION);', '  gshgeom_hash_u16(&h, LIST_FORMAT_VERSION);', D),
    ('ls-hash-kind-width', 'src', LS, '  gshgeom_hash_u32(&h, LIST_HASH_KIND);', '  gshgeom_hash_u16(&h, (uint16_t)LIST_HASH_KIND);', D),
    ('ls-hash-count-width', 'src', LS, '  gshgeom_hash_u64(&h, (uint64_t)list->count);', '  gshgeom_hash_u32(&h, (uint32_t)list->count);', D),
    ('ls-hash-item-kind-width', 'src', LS, '    gshgeom_hash_u8(&h, (uint8_t)item->kind);', '    gshgeom_hash_u16(&h, (uint16_t)item->kind);', D),
    ('ls-hash-resource-width', 'src', LS, '  gshgeom_hash_u64(h, gshscene_resource_hash(item->resource));', '  gshgeom_hash_u32(h, (uint32_t)gshscene_resource_hash(item->resource));', D),
    ('ls-hash-map-width', 'src', LS, '  gshgeom_hash_u64(h, gshgeom_map_hash(&item->map));', '  gshgeom_hash_u32(h, (uint32_t)gshgeom_map_hash(&item->map));', D),
    ('ls-hash-resource-map-order', 'src', LS, '  gshgeom_hash_u64(h, gshscene_resource_hash(item->resource));\n  gshgeom_hash_u64(h, gshgeom_map_hash(&item->map));', '  gshgeom_hash_u64(h, gshgeom_map_hash(&item->map));\n  gshgeom_hash_u64(h, gshscene_resource_hash(item->resource));', D),
    ('ls-hash-fill-rule-width', 'src', LS, '        gshgeom_hash_u8(&h, (uint8_t)item->fill_rule);\n        gshscene_paint_hash(&h, &item->paint);', '        gshgeom_hash_u32(&h, (uint32_t)item->fill_rule);\n        gshscene_paint_hash(&h, &item->paint);', D),
    ('ls-hash-paint-kind-width', 'src', LS, '  gshgeom_hash_u8(h, (uint8_t)paint->kind);', '  gshgeom_hash_u32(h, (uint32_t)paint->kind);', D),
    ('ls-hash-rgba-loop', 'src', LS, '  for (int k = 0; k < 4; k++) {\n    gshgeom_hash_f64(h, paint->rgba[k]);', '  for (int k = 0; k < 3; k++) {\n    gshgeom_hash_f64(h, paint->rgba[k]);', D),
    ('ls-hash-clip-rule-width', 'src', LS, '      case GSHSCENE_ITEM_CLIP_PUSH:\n        hash_resource_and_map(&h, item);\n        gshgeom_hash_u8(&h, (uint8_t)item->fill_rule);', '      case GSHSCENE_ITEM_CLIP_PUSH:\n        hash_resource_and_map(&h, item);\n        gshgeom_hash_u32(&h, (uint32_t)item->fill_rule);', D),
    ('ls-hash-opacity', 'src', LS, '        gshgeom_hash_f64(&h, item->group.opacity);', '        gshgeom_hash_f64(&h, item->group.opacity + 1.0);', D),
    ('ls-hash-ordering-width', 'src', LS, '        gshgeom_hash_u32(&h, item->group.ordering);', '        gshgeom_hash_u8(&h, (uint8_t)item->group.ordering);', D),
    ('ls-hash-group-layer-width', 'src', LS, '        gshgeom_hash_u32(&h, item->group.ordering);\n        gshgeom_hash_i32(&h, item->layer);', '        gshgeom_hash_u32(&h, item->group.ordering);\n        gshgeom_hash_i64(&h, item->layer);', D),
    ('ls-hash-instance-layer-width', 'src', LS, '      case GSHSCENE_ITEM_INSTANCE:\n        hash_resource_and_map(&h, item);\n        gshgeom_hash_i32(&h, item->layer);', '      case GSHSCENE_ITEM_INSTANCE:\n        hash_resource_and_map(&h, item);\n        gshgeom_hash_i64(&h, item->layer);', D),
    ('ls-hash-instance-resource', 'src', LS, '      case GSHSCENE_ITEM_INSTANCE:\n        hash_resource_and_map(&h, item);', '      case GSHSCENE_ITEM_INSTANCE:\n        gshgeom_hash_u64(&h, gshgeom_map_hash(&item->map));', D),
    ('ls-hash-pop-fields', 'src', LS, '      default:\n        break;\n    }\n  }\n  return gshgeom_hash_value(&h);', '      default:\n        gshgeom_hash_u8(&h, 0);\n        break;\n    }\n  }\n  return gshgeom_hash_value(&h);', D),
    ('ls-dump-resource-width', 'src', LS, '" res=%s:%016llx map=%016llx"', '" res=%s:%llx map=%016llx"', D),
    ('ls-dump-map-width', 'src', LS, '" res=%s:%016llx map=%016llx"', '" res=%s:%016llx map=%llx"', D),
    ('ls-dump-fill-tag', 'src', LS, "        written = fputc('F', out) == EOF ? -1 : dump_resource_and_map(out, item);", "        written = fputc('f', out) == EOF ? -1 : dump_resource_and_map(out, item);", D),
    ('ls-dump-fill-red-width', 'src', LS, '" paint=%u %016llx %016llx', '" paint=%u %llx %016llx', D),
    ('ls-dump-fill-components', 'src', LS, 'HEX(gshgeom_f64_bits(paint->rgba[1])), HEX(gshgeom_f64_bits(paint->rgba[2])),', 'HEX(gshgeom_f64_bits(paint->rgba[2])), HEX(gshgeom_f64_bits(paint->rgba[1])),', D),
    ('ls-dump-clip-tag', 'src', LS, "        written = fputc('C', out) == EOF ? -1 : dump_resource_and_map(out, item);", "        written = fputc('K', out) == EOF ? -1 : dump_resource_and_map(out, item);", D),
    ('ls-dump-clip-rule', 'src', LS, '        written = fputc(\'C\', out) == EOF ? -1 : dump_resource_and_map(out, item);\n        if (written >= 0) {\n          written = fprintf(out, " rule=%u", (unsigned)item->fill_rule);', '        written = fputc(\'C\', out) == EOF ? -1 : dump_resource_and_map(out, item);\n        if (written >= 0) {\n          written = fprintf(out, " rule=%u", 0u);', D),
    ('ls-dump-clip-pop-tag', 'src', LS, "        written = fputc('c', out) == EOF ? -1 : 0;", "        written = fputc('p', out) == EOF ? -1 : 0;", D),
    ('ls-dump-group-pop-tag', 'src', LS, "        written = fputc('g', out) == EOF ? -1 : 0;", "        written = fputc('p', out) == EOF ? -1 : 0;", D),
    ('ls-dump-instance-tag', 'src', LS, "        written = fputc('I', out) == EOF ? -1 : dump_resource_and_map(out, item);", "        written = fputc('i', out) == EOF ? -1 : dump_resource_and_map(out, item);", D),
    ('ls-dump-final-error-flag', 'src', LS, '  return (fflush(out) == 0 && !ferror(out)) ? GSHSCENE_OK : GSHSCENE_ERR_IO;', '  return fflush(out) == 0 ? GSHSCENE_OK : GSHSCENE_ERR_IO;', D),
    ('ls-dump-final-flush', 'src', LS, '  return (fflush(out) == 0 && !ferror(out)) ? GSHSCENE_OK : GSHSCENE_ERR_IO;', '  return !ferror(out) ? GSHSCENE_OK : GSHSCENE_ERR_IO;', D),
    ('hdr-kind-path', 'hdr', HR, '  GSHSCENE_RESOURCE_PATH = 1,      ///< A model path.', '  GSHSCENE_RESOURCE_PATH = 19,      ///< A model path.', R + D),
    ('hdr-kind-glyph-run', 'hdr', HR, '  GSHSCENE_RESOURCE_GLYPH_RUN = 2, ///< Glyphs, by font and id or as outlines.', '  GSHSCENE_RESOURCE_GLYPH_RUN = 19, ///< Glyphs, by font and id or as outlines.', R + D),
    ('hdr-kind-picture', 'hdr', HR, '  GSHSCENE_RESOURCE_PICTURE = 3,   ///< A closed list, frozen: what an instance draws.', '  GSHSCENE_RESOURCE_PICTURE = 10,   ///< A closed list, frozen: what an instance draws.', R + D),
    ('hdr-kind-gradient', 'hdr', HR, '  GSHSCENE_RESOURCE_GRADIENT = 4,  ///< A gradient', '  GSHSCENE_RESOURCE_GRADIENT = 11,  ///< A gradient', R),
    ('hdr-kind-image', 'hdr', HR, '  GSHSCENE_RESOURCE_IMAGE = 5,     ///< Reserved for the raster stories: no constructor yet.', '  GSHSCENE_RESOURCE_IMAGE = 12,     ///< Reserved for the raster stories: no constructor yet.', R),
    ('hdr-source-font', 'hdr', HR, '  GSHSCENE_GLYPHS_FROM_FONT = 0,', '  GSHSCENE_GLYPHS_FROM_FONT = 1,', D + R),
    ('hdr-item-fill', 'hdr', HL, '  GSHSCENE_ITEM_FILL = 1,       ///< Fill a resource under a map with a paint.', '  GSHSCENE_ITEM_FILL = 17,       ///< Fill a resource under a map with a paint.', L + D),
    ('hdr-item-clip-push', 'hdr', HL, "  GSHSCENE_ITEM_CLIP_PUSH = 2,  ///< Intersect the clip with a resource's region.", "  GSHSCENE_ITEM_CLIP_PUSH = 8,  ///< Intersect the clip with a resource's region.", L + D),
    ('hdr-item-clip-pop', 'hdr', HL, '  GSHSCENE_ITEM_CLIP_POP = 3,   ///< End the innermost clip.', '  GSHSCENE_ITEM_CLIP_POP = 9,   ///< End the innermost clip.', L + D),
    ('hdr-item-group-push', 'hdr', HL, '  GSHSCENE_ITEM_GROUP_PUSH = 4, ///< Begin an opacity group.', '  GSHSCENE_ITEM_GROUP_PUSH = 10, ///< Begin an opacity group.', L + D),
    ('hdr-item-group-pop', 'hdr', HL, '  GSHSCENE_ITEM_GROUP_POP = 5,  ///< End the innermost group and composite it.', '  GSHSCENE_ITEM_GROUP_POP = 11,  ///< End the innermost group and composite it.', L + D),
    ('hdr-item-instance', 'hdr', HL, '  GSHSCENE_ITEM_INSTANCE = 6,   ///< Draw a picture under a map.', '  GSHSCENE_ITEM_INSTANCE = 12,   ///< Draw a picture under a map.', L + D),
    ('hdr-paint-foreground', 'hdr', HP, '  GSHSCENE_PAINT_FOREGROUND = 2,', '  GSHSCENE_PAINT_FOREGROUND = 6,', L + ["testColor"]),
    ('hdr-paint-palette', 'hdr', HP, '  GSHSCENE_PAINT_PALETTE = 3,', '  GSHSCENE_PAINT_PALETTE = 7,', L + ['testColor']),
    ('hdr-paint-gradient', 'hdr', HP, '  GSHSCENE_PAINT_GRADIENT = 1,\n', '  GSHSCENE_PAINT_GRADIENT = 4,\n', L + ['testGradient']),
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
    ('s-reason-cycle', 'hdr', HSC, 'GSHSCENE_PROBLEM_CYCLE = 4,', 'GSHSCENE_PROBLEM_CYCLE = 15,', ["testScene"]),
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
    ('vs-hash-kind', 'src', IH, '#define VERSION_HASH_KIND UINT32_C(0x4e454353)', '#define VERSION_HASH_KIND UINT32_C(0x4e454354)', ["testScene_dump"]),
    ('df-range-limit-high', 'src', DF, '#define RANGE_LIMIT 4.0e18', '#define RANGE_LIMIT 4.0e30', ["testDiff"]),
    ('df-range-limit-low', 'src', DF, '#define RANGE_LIMIT 4.0e18', '#define RANGE_LIMIT 4.0e3', ["testDiff"]),
]


# --- story 12: paint, color and composition ------------------------------------------------------------------------
# What changed in the list's sources (the paint rules, the group rules, the record calls, close and the order, the hash
# and the dump) is planted here by hand; the retained scene's new lines are generated (tools/gen-scene-plants.py).
CS = "src/paint/color_space.c"
CL = ["testColor"]
O = ["testOrder"]
CP = ["testComposite"]
PS = ["testPaint_scene"]
LD = ["testList", "testDump"]
LC = ["testList", "testColor"]

PLANTS += [
    # the format and the pinned values
    ('ls-format-version', 'src', LS, '#define LIST_FORMAT_VERSION 1u', '#define LIST_FORMAT_VERSION 2u', D),
    ('hdr-ordering-flat', 'hdr', HL, '  GSHSCENE_ORDER_FLAT = 0,', '  GSHSCENE_ORDER_FLAT = 3,', L + O),
    ('hdr-ordering-depth', 'hdr', HL, '  GSHSCENE_ORDER_DEPTH = 1,', '  GSHSCENE_ORDER_DEPTH = 4,', O),
    ('hdr-ordering-overlay', 'hdr', HL, '  GSHSCENE_ORDER_OVERLAY = 2,', '  GSHSCENE_ORDER_OVERLAY = 5,', O),
    ('hdr-paint-solid', 'hdr', HP, '  GSHSCENE_PAINT_SOLID = 0,', '  GSHSCENE_PAINT_SOLID = 5,', L + D + ['testColor']),
    ('hdr-tie-layer', 'hdr', HL, '  GSHSCENE_TIE_LAYER = 1,', '  GSHSCENE_TIE_LAYER = 21,', O),
    ('hdr-tie-path', 'hdr', HL, '  GSHSCENE_TIE_PATH = 2,', '  GSHSCENE_TIE_PATH = 22,', O),
    ('hdr-tie-declared', 'hdr', HL, '  GSHSCENE_TIE_DECLARED = 3,', '  GSHSCENE_TIE_DECLARED = 23,', O),
    ('hdr-problem-parameter', 'hdr', "include/ghoti.io/shape-scene/scene.h", '  GSHSCENE_PROBLEM_PARAMETER = 5,', '  GSHSCENE_PROBLEM_PARAMETER = 25,', PS),
    ('vs-format-version', 'src', IH, '#define VERSION_FORMAT_VERSION 1u', '#define VERSION_FORMAT_VERSION 2u', ["testScene_dump"]),
    ('cs-gamma-min', 'src', CS, '#define GAMMA_MIN 0.1', '#define GAMMA_MIN 0.2', CL),
    ('cs-gamma-max', 'src', CS, '#define GAMMA_MAX 10.0', '#define GAMMA_MAX 9.0', CL),
]
for _op, _name in enumerate(["SRC_OVER", "CLEAR", "SRC", "DEST", "DEST_OVER", "SRC_IN", "DEST_IN", "SRC_OUT", "DEST_OUT",
                             "SRC_ATOP", "DEST_ATOP", "XOR", "PLUS"]):
    PLANTS.append(('hdr-composite-%s' % _name.lower().replace("_", "-"), 'hdr', HL,
                   '  GSHSCENE_COMPOSITE_%s = %d,' % (_name, _op), '  GSHSCENE_COMPOSITE_%s = %d,' % (_name, 100 + _op), CP + CL))

PLANTS += [
    # create, clear and destroy
    ('ls-create-store', 'src', LS, '  list->store = store;\n', '', L),
    ('ls-create-no-store-ref', 'src', LS, '  gshscene_store_ref(store);\n  *out = list;', '  *out = list;', L),
    ('ls-create-foreground-default', 'src', LS, '  list->store = store;\n  gshscene_color_init(&list->foreground);', '  list->store = store;', CL),
    ('ls-clear-picture-depth', 'src', LS, '  list->picture_depth = 0;\n  list->nonflat = 0;', '  list->nonflat = 0;', L),
    ('ls-clear-foreground', 'src', LS, '  gshscene_color_init(&list->foreground);\n  gshscene_free(&list->alloc, list->ties);', '  gshscene_free(&list->alloc, list->ties);', CL + O),
    ('ls-clear-ties', 'src', LS, '  list->ties = NULL;\n  list->tie_count = 0;\n  list->tie_capacity = 0;\n  list->closed = 0;', '  list->tie_capacity = 0;\n  list->closed = 0;', O),
    ('ls-clear-closed', 'src', LS, '  list->tie_capacity = 0;\n  list->closed = 0;', '  list->tie_capacity = 0;', L),
    ('ls-destroy-standard-chunks', 'src', LS, '  chunks_free(list, list->head, NULL);\n  chunks_free(list, list->big, NULL);\n  gshscene_free(&list->alloc, list->ties);', '  chunks_free(list, list->big, NULL);\n  gshscene_free(&list->alloc, list->ties);', L),
    ('ls-destroy-big-chunks', 'src', LS, '  chunks_free(list, list->head, NULL);\n  chunks_free(list, list->big, NULL);\n  gshscene_free(&list->alloc, list->ties);', '  chunks_free(list, list->head, NULL);\n  gshscene_free(&list->alloc, list->ties);', L),
    ('ls-destroy-ties', 'src', LS, '  gshscene_free(&list->alloc, list->ties);\n  GSHSCENE_Store * store = list->store;', '  GSHSCENE_Store * store = list->store;', O),
    ('ls-append-no-retain', 'src', LS, '  gshscene_resource_retain(item->resource);\n  gshscene_resource_retain(item->paint.gradient);', '  gshscene_resource_retain(item->paint.gradient);', L),
    ('ls-append-no-note', 'src', LS, '  note_item(list, item);\n  list->count++;', '  list->count++;', O),
    # the paint
    ('ls-paint-gradient', 'src', LS, '  if (paint->kind == GSHSCENE_PAINT_GRADIENT\n          ? (!paint->gradient ||', '  if (paint->kind == GSHSCENE_PAINT_GRADIENT\n          ? (0 ||', L),
    ('ls-paint-palette', 'src', LS, 'paint->kind != GSHSCENE_PAINT_PALETTE && paint->kind != GSHSCENE_PAINT_GRADIENT) {', '1 && paint->kind != GSHSCENE_PAINT_GRADIENT) {', L + CL),
    ('ls-paint-entry-palette-only', 'src', LS, 'paint->entry > GSHSCENE_PALETTE_ENTRY_MAX : paint->entry != 0) {', 'paint->entry > GSHSCENE_PALETTE_ENTRY_MAX : 0) {', CL),
    ('ls-paint-entry-bound', 'src', LS, 'paint->entry > GSHSCENE_PALETTE_ENTRY_MAX : paint->entry != 0) {', 'paint->entry > 0xFFFFu : paint->entry != 0) {', CL + ['testNode']),
    ('ls-paint-entry-bound-tie', 'src', LS, 'paint->entry > GSHSCENE_PALETTE_ENTRY_MAX : paint->entry != 0) {', 'paint->entry >= GSHSCENE_PALETTE_ENTRY_MAX : paint->entry != 0) {', CL + ['testNode', 'testDump']),
    ('ls-paint-entry-kind', 'src', LS, '  if (paint->kind == GSHSCENE_PAINT_PALETTE ? paint->entry', '  if (paint->kind == GSHSCENE_PAINT_FOREGROUND ? paint->entry', CL),
    ('ls-paint-entry-copied', 'src', LS, '    out->entry = paint->entry;\n', '', CL + D + ['testPalette']),
    ('ls-paint-solid-only', 'src', LS, '  if (paint->kind != GSHSCENE_PAINT_SOLID && paint->kind != GSHSCENE_PAINT_FOREGROUND &&', '  if (paint->kind != GSHSCENE_PAINT_SOLID && 0 &&', CL + ['testPalette']),
    ('ls-paint-kind', 'src', LS, '  if (paint->kind != GSHSCENE_PAINT_SOLID && paint->kind != GSHSCENE_PAINT_FOREGROUND &&\n      paint->kind != GSHSCENE_PAINT_PALETTE && paint->kind != GSHSCENE_PAINT_GRADIENT) {', '  if (0) {', L + ['testPalette']),
    ('ls-paint-gradient-pointer', 'src', LS, '          : paint->gradient != NULL) {', '          : 0) {', L + CL),
    ('ls-paint-foreground-red', 'src', LS, 'if (gshgeom_f64_bits(paint->rgba[0]) != 0 || gshgeom_f64_bits(paint->rgba[1]) != 0 ||', 'if (gshgeom_f64_bits(paint->rgba[1]) != 0 ||', CL),
    ('ls-paint-foreground-green', 'src', LS, 'if (gshgeom_f64_bits(paint->rgba[0]) != 0 || gshgeom_f64_bits(paint->rgba[1]) != 0 ||', 'if (gshgeom_f64_bits(paint->rgba[0]) != 0 ||', CL),
    ('ls-paint-foreground-blue', 'src', LS, '        gshgeom_f64_bits(paint->rgba[2]) != 0 || !gshscene_color_space_is_target(&paint->space)) {', '        !gshscene_color_space_is_target(&paint->space)) {', CL),
    ('ls-paint-foreground-space', 'src', LS, '        gshgeom_f64_bits(paint->rgba[2]) != 0 || !gshscene_color_space_is_target(&paint->space)) {', '        gshgeom_f64_bits(paint->rgba[2]) != 0) {', CL),
    ('ls-paint-foreground-kind', 'src', LS, '    out->kind = paint->kind;\n', '', CL + L),
    ('ls-paint-nonsolid-branch', 'src', LS, '  if (paint->kind != GSHSCENE_PAINT_SOLID) {\n    /* One spelling', '  if (paint->kind == GSHSCENE_PAINT_FOREGROUND) {\n    /* One spelling', CL + ['testPalette']),
    ('ls-paint-foreground-alpha', 'src', LS, '    out->rgba[3] = paint->rgba[3];\n    out->entry', '    out->entry', CL),
    ('ls-paint-space-check', 'src', LS, '  GSHSCENE_Result result = gshscene_color_space_check(&paint->space);\n  if (result != GSHSCENE_OK) {\n    return result;\n  }\n  for (int i = 0; i < 4; i++) {\n    out->rgba[i] = paint->rgba[i];', '  for (int i = 0; i < 4; i++) {\n    out->rgba[i] = paint->rgba[i];', L),
    ('ls-paint-space-copy', 'src', LS, '  out->space = paint->space;\n  return GSHSCENE_OK;', '  return GSHSCENE_OK;', CL),
    ('ls-depth-finite', 'src', LS, '  if (!gshscene_finite(depth)) {\n    return GSHSCENE_ERR_INVALID;\n  }\n  *out = depth == 0.0 ? 0.0 : depth;', '  *out = depth == 0.0 ? 0.0 : depth;', L + PS),
    ('ls-depth-negative-zero', 'src', LS, '  *out = depth == 0.0 ? 0.0 : depth;', '  *out = depth;', O),
    # the group
    ('ls-group-default-opacity', 'src', LS, '  out->opacity = 1.0;\n  out->ordering = GSHSCENE_ORDER_FLAT;', '  out->opacity = 0.0;\n  out->ordering = GSHSCENE_ORDER_FLAT;', L),
    ('ls-group-ordering-range', 'src', LS, 'group->ordering >= (uint32_t)GSHSCENE_ORDERING_COUNT || group->composite', 'group->ordering > (uint32_t)GSHSCENE_ORDERING_COUNT || group->composite', L),
    ('ls-group-composite-range', 'src', LS, '|| group->composite >= (uint32_t)GSHSCENE_COMPOSITE_COUNT) {', '|| group->composite > (uint32_t)GSHSCENE_COMPOSITE_COUNT) {', L),
    ('ls-group-ordering-unchecked', 'src', LS, '  if (group->ordering >= (uint32_t)GSHSCENE_ORDERING_COUNT || group->composite >= (uint32_t)GSHSCENE_COMPOSITE_COUNT) {', '  if (group->composite >= (uint32_t)GSHSCENE_COMPOSITE_COUNT) {', L),
    ('ls-group-composite-unchecked', 'src', LS, '  if (group->ordering >= (uint32_t)GSHSCENE_ORDERING_COUNT || group->composite >= (uint32_t)GSHSCENE_COMPOSITE_COUNT) {', '  if (group->ordering >= (uint32_t)GSHSCENE_ORDERING_COUNT) {', L),
    ('ls-group-blend-check', 'src', LS, '  GSHSCENE_Result result = gshscene_color_space_check(&group->blend_space);\n  if (result != GSHSCENE_OK) {\n    return result;\n  }\n  out->opacity = group->opacity;', '  out->opacity = group->opacity;', L),
    ('ls-group-opacity-copy', 'src', LS, '  out->opacity = group->opacity;\n  out->blend_space = group->blend_space;', '  out->blend_space = group->blend_space;', L + CP),
    ('ls-group-blend-copy', 'src', LS, '  out->blend_space = group->blend_space;\n  out->ordering = group->ordering;', '  out->ordering = group->ordering;', LD),
    ('ls-group-ordering-copy', 'src', LS, '  out->ordering = group->ordering;\n  out->composite = group->composite;\n  out->isolation', '  out->composite = group->composite;\n  out->isolation', O),
    ('ls-group-composite-copy', 'src', LS, '  out->composite = group->composite;\n  out->isolation', '  out->isolation', CP + LD),
    # the record calls
    ('ls-fill-null-list', 'src', LS, 'const GSHGEOM_Map * map, GSHGEOM_Fill_Rule rule, const GSHSCENE_Paint * paint, int32_t layer, double depth) {\n  if (!list || list->closed) {', 'const GSHGEOM_Map * map, GSHGEOM_Fill_Rule rule, const GSHSCENE_Paint * paint, int32_t layer, double depth) {\n  if (list->closed) {', L),
    ('ls-fill-closed', 'src', LS, 'const GSHGEOM_Map * map, GSHGEOM_Fill_Rule rule, const GSHSCENE_Paint * paint, int32_t layer, double depth) {\n  if (!list || list->closed) {', 'const GSHGEOM_Map * map, GSHGEOM_Fill_Rule rule, const GSHSCENE_Paint * paint, int32_t layer, double depth) {\n  if (!list) {', L),
    ('ls-fill-depth-check', 'src', LS, '  if (result == GSHSCENE_OK) {\n    result = gshscene_list_canonical_depth(depth, &item.depth);\n  }\n  if (result == GSHSCENE_OK && !gshscene_color_bounded(item.paint.rgba)) {', '(void)depth;\n  if (result == GSHSCENE_OK && !gshscene_color_bounded(item.paint.rgba)) {', L),
    ('ls-clip-null-list', 'src', LS, 'const GSHGEOM_Map * map, GSHGEOM_Fill_Rule rule, int32_t layer, double depth) {\n  if (!list || list->closed) {', 'const GSHGEOM_Map * map, GSHGEOM_Fill_Rule rule, int32_t layer, double depth) {\n  if (list->closed) {', L),
    ('ls-clip-closed', 'src', LS, 'const GSHGEOM_Map * map, GSHGEOM_Fill_Rule rule, int32_t layer, double depth) {\n  if (!list || list->closed) {', 'const GSHGEOM_Map * map, GSHGEOM_Fill_Rule rule, int32_t layer, double depth) {\n  if (!list) {', L),
    ('ls-clip-rule-check', 'src', LS, '  if (result == GSHSCENE_OK) {\n    result = check_rule(rule);\n  }\n  if (result == GSHSCENE_OK) {\n    result = gshscene_list_canonical_depth(depth, &item.depth);\n  }\n  if (result == GSHSCENE_OK) {\n    result = stack_reserve(list);', '  if (result == GSHSCENE_OK) {\n    check_rule(rule);\n  }\n  if (result == GSHSCENE_OK) {\n    result = gshscene_list_canonical_depth(depth, &item.depth);\n  }\n  if (result == GSHSCENE_OK) {\n    result = stack_reserve(list);', L),
    ('ls-clip-depth-check', 'src', LS, '  if (result == GSHSCENE_OK) {\n    result = gshscene_list_canonical_depth(depth, &item.depth);\n  }\n  if (result == GSHSCENE_OK) {\n    result = stack_reserve(list);\n  }\n  if (result != GSHSCENE_OK) {\n    return result;\n  }\n  item.resource = resource;', '(void)depth;\n  if (result == GSHSCENE_OK) {\n    result = stack_reserve(list);\n  }\n  if (result != GSHSCENE_OK) {\n    return result;\n  }\n  item.resource = resource;', L),
    ('ls-clip-stack', 'src', LS, '  if (result == GSHSCENE_OK) {\n    result = stack_reserve(list);\n  }\n  if (result != GSHSCENE_OK) {\n    return result;\n  }\n  item.resource = resource;\n  item.fill_rule = rule;\n  item.layer = layer;\n  result = list_append(list, &item);', '  if (result != GSHSCENE_OK) {\n    return result;\n  }\n  item.resource = resource;\n  item.fill_rule = rule;\n  item.layer = layer;\n  result = list_append(list, &item);', L),
    ('ls-clip-resource', 'src', LS, '  item.resource = resource;\n  item.fill_rule = rule;\n  item.layer = layer;\n  result = list_append(list, &item);', '  item.fill_rule = rule;\n  item.layer = layer;\n  result = list_append(list, &item);', L),
    ('ls-clip-rule', 'src', LS, '  item.resource = resource;\n  item.fill_rule = rule;\n  item.layer = layer;\n  result = list_append(list, &item);', '  item.resource = resource;\n  item.layer = layer;\n  result = list_append(list, &item);', L),
    ('ls-clip-layer', 'src', LS, '  item.resource = resource;\n  item.fill_rule = rule;\n  item.layer = layer;\n  result = list_append(list, &item);', '(void)layer;\n  item.resource = resource;\n  item.fill_rule = rule;\n  result = list_append(list, &item);', LD),
    ('ls-group-push-check', 'src', LS, '  GSHSCENE_Result result = canonical_group(list->store, group, &item.group);\n  if (result == GSHSCENE_OK) {\n    result = gshscene_list_canonical_depth(depth, &item.depth);\n  }', '  GSHSCENE_Result result = GSHSCENE_OK;\n  canonical_group(list->store, group, &item.group);\n  if (result == GSHSCENE_OK) {\n    result = gshscene_list_canonical_depth(depth, &item.depth);\n  }', L),
    ('ls-group-push-depth-check', 'src', LS, '  if (result == GSHSCENE_OK) {\n    result = gshscene_list_canonical_depth(depth, &item.depth);\n  }\n  if (result == GSHSCENE_OK) {\n    result = stack_reserve(list);\n  }\n  if (result != GSHSCENE_OK) {\n    return result;\n  }\n  item.layer = layer;', '(void)depth;\n  if (result == GSHSCENE_OK) {\n    result = stack_reserve(list);\n  }\n  if (result != GSHSCENE_OK) {\n    return result;\n  }\n  item.layer = layer;', L),
    ('ls-group-push-reserve', 'src', LS, '  if (result == GSHSCENE_OK) {\n    result = stack_reserve(list);\n  }\n  if (result != GSHSCENE_OK) {\n    return result;\n  }\n  item.layer = layer;', '  if (result != GSHSCENE_OK) {\n    return result;\n  }\n  item.layer = layer;', L),
    ('ls-instance-null-list', 'src', LS, 'GSHSCENE_List * list, const GSHSCENE_Resource * picture, const GSHGEOM_Map * map, int32_t layer, double depth) {\n  if (!list || list->closed) {', 'GSHSCENE_List * list, const GSHSCENE_Resource * picture, const GSHGEOM_Map * map, int32_t layer, double depth) {\n  if (list->closed) {', L),
    ('ls-instance-closed', 'src', LS, 'GSHSCENE_List * list, const GSHSCENE_Resource * picture, const GSHGEOM_Map * map, int32_t layer, double depth) {\n  if (!list || list->closed) {', 'GSHSCENE_List * list, const GSHSCENE_Resource * picture, const GSHGEOM_Map * map, int32_t layer, double depth) {\n  if (!list) {', L),
    ('ls-instance-map-check', 'src', LS, '  if (result == GSHSCENE_OK) {\n    result = canonical_map(map, &item.map);\n  }\n  if (result == GSHSCENE_OK) {\n    result = gshscene_list_canonical_depth(depth, &item.depth);\n  }\n  if (result != GSHSCENE_OK) {\n    return result;\n  }\n  item.resource = picture;', '  if (result == GSHSCENE_OK) {\n    canonical_map(map, &item.map);\n  }\n  if (result == GSHSCENE_OK) {\n    result = gshscene_list_canonical_depth(depth, &item.depth);\n  }\n  if (result != GSHSCENE_OK) {\n    return result;\n  }\n  item.resource = picture;', L),
    ('ls-instance-depth-check', 'src', LS, '  if (result == GSHSCENE_OK) {\n    result = gshscene_list_canonical_depth(depth, &item.depth);\n  }\n  if (result != GSHSCENE_OK) {\n    return result;\n  }\n  item.resource = picture;', '(void)depth;\n  if (result != GSHSCENE_OK) {\n    return result;\n  }\n  item.resource = picture;', L),
    # the foreground
    ('ls-foreground-set-closed', 'src', LS, 'GSHSCENE_Result gshscene_list_set_foreground(GSHSCENE_List * list, const GSHSCENE_Color * color) {\n  if (!list || list->closed) {', 'GSHSCENE_Result gshscene_list_set_foreground(GSHSCENE_List * list, const GSHSCENE_Color * color) {\n  if (!list) {', CL),
    ('ls-foreground-set-null-list', 'src', LS, 'GSHSCENE_Result gshscene_list_set_foreground(GSHSCENE_List * list, const GSHSCENE_Color * color) {\n  if (!list || list->closed) {', 'GSHSCENE_Result gshscene_list_set_foreground(GSHSCENE_List * list, const GSHSCENE_Color * color) {\n  if (list->closed) {', CL),
    ('ls-foreground-set-check', 'src', LS, '    GSHSCENE_Result result = gshscene_color_check(color);\n    if (result != GSHSCENE_OK) {\n      return result;\n    }\n    value = *color;', '    value = *color;', CL),
    ('ls-foreground-set-null-default', 'src', LS, '  GSHSCENE_Color value;\n  gshscene_color_init(&value);\n  if (color) {', '  GSHSCENE_Color value;\n  memset(&value, 0, sizeof value);\n  if (color) {', CL),
    ('ls-foreground-set-store', 'src', LS, '  list->foreground = value;\n  return GSHSCENE_OK;', '  return GSHSCENE_OK;', CL),
    ('ls-foreground-get-null', 'src', LS, 'GSHSCENE_Result gshscene_list_foreground(const GSHSCENE_List * list, GSHSCENE_Color * out) {\n  if (!list || !out) {', 'GSHSCENE_Result gshscene_list_foreground(const GSHSCENE_List * list, GSHSCENE_Color * out) {\n  if (!out) {', CL),
    ('ls-foreground-get-value', 'src', LS, '  *out = list->foreground;\n  return GSHSCENE_OK;', '  return GSHSCENE_OK;', CL),
    # composite operators
    ('ls-composite-unbounded-clear', 'src', LS, '    case GSHSCENE_COMPOSITE_CLEAR:\n    case GSHSCENE_COMPOSITE_SRC:\n    case GSHSCENE_COMPOSITE_SRC_IN:', '    case GSHSCENE_COMPOSITE_SRC:\n    case GSHSCENE_COMPOSITE_SRC_IN:', CP),
    ('ls-composite-unbounded-src', 'src', LS, '    case GSHSCENE_COMPOSITE_CLEAR:\n    case GSHSCENE_COMPOSITE_SRC:\n    case GSHSCENE_COMPOSITE_SRC_IN:', '    case GSHSCENE_COMPOSITE_CLEAR:\n    case GSHSCENE_COMPOSITE_SRC_IN:', CP),
    ('ls-composite-unbounded-src-in', 'src', LS, '    case GSHSCENE_COMPOSITE_SRC:\n    case GSHSCENE_COMPOSITE_SRC_IN:\n    case GSHSCENE_COMPOSITE_DEST_IN:', '    case GSHSCENE_COMPOSITE_SRC:\n    case GSHSCENE_COMPOSITE_DEST_IN:', CP),
    ('ls-composite-unbounded-dest-in', 'src', LS, '    case GSHSCENE_COMPOSITE_SRC_IN:\n    case GSHSCENE_COMPOSITE_DEST_IN:\n    case GSHSCENE_COMPOSITE_SRC_OUT:', '    case GSHSCENE_COMPOSITE_SRC_IN:\n    case GSHSCENE_COMPOSITE_SRC_OUT:', CP),
    ('ls-composite-unbounded-src-out', 'src', LS, '    case GSHSCENE_COMPOSITE_DEST_IN:\n    case GSHSCENE_COMPOSITE_SRC_OUT:\n    case GSHSCENE_COMPOSITE_DEST_ATOP:', '    case GSHSCENE_COMPOSITE_DEST_IN:\n    case GSHSCENE_COMPOSITE_DEST_ATOP:', CP),
    ('ls-composite-unbounded-dest-atop', 'src', LS, '    case GSHSCENE_COMPOSITE_SRC_OUT:\n    case GSHSCENE_COMPOSITE_DEST_ATOP:\n      return 1;', '    case GSHSCENE_COMPOSITE_SRC_OUT:\n      return 1;', CP),
    ('ls-composite-unbounded-all', 'src', LS, '    case GSHSCENE_COMPOSITE_DEST_ATOP:\n      return 1;\n    default:\n      return 0;', '    case GSHSCENE_COMPOSITE_DEST_ATOP:\n      return 1;\n    default:\n      return 1;', CP),
    ('ls-composite-string-unknown', 'src', LS, '  return "unknown composite";', '  return "";', CP),
    # the order
    ('ls-order-depth-far-first', 'src', LS, '    if (depth_a > depth_b) {\n      return -1; /* the farther unit is painted first */\n    }', '    if (depth_a < depth_b) {\n      return -1; /* the farther unit is painted first */\n    }', O),
    ('ls-order-depth-near-last', 'src', LS, '    if (depth_a < depth_b) {\n      return 1;\n    }\n    *tied = 1;', '    if (depth_a > depth_b) {\n      return 1;\n    }\n    *tied = 1;', O),
    ('ls-order-layer-ascending', 'src', LS, '    return layer_a < layer_b ? -1 : 1;', '    return layer_a < layer_b ? 1 : -1;', O),
    ('ls-order-stable', 'src', LS, 'if (unit_before(list, context, &units[j], &units[i], &tied) < 0) {', 'if (unit_before(list, context, &units[j], &units[i], &tied) <= 0) {', O),
    ('ls-order-depth-tie-reason', 'src', LS, 'tie->reason = before->layer != item->layer ? GSHSCENE_TIE_LAYER : GSHSCENE_TIE_DECLARED;', 'tie->reason = before->layer != item->layer ? GSHSCENE_TIE_DECLARED : GSHSCENE_TIE_LAYER;', O),
    ('ls-order-tie-first', 'src', LS, '        tie->first = f->prev_pos;', '        tie->first = pos;', O),
    ('ls-order-tie-second', 'src', LS, '        tie->second = pos;', '        tie->second = f->prev_pos;', O),
    ('ls-order-tie-depth', 'src', LS, '        tie->depth = item->depth;', '        tie->depth = 0.0;', O),
    ('ls-order-prev-pos', 'src', LS, '    f->prev_pos = pos;\n    s->out[out_n++] = u.start;', '    s->out[out_n++] = u.start;', O),
    ('ls-order-inner-context', 'src', LS, '      uint32_t inner = item->kind == GSHSCENE_ITEM_GROUP_PUSH ? item->group.ordering : f->context;', '      uint32_t inner = f->context;', O),
    ('ls-order-clip-inherits', 'src', LS, '      uint32_t inner = item->kind == GSHSCENE_ITEM_GROUP_PUSH ? item->group.ordering : f->context;', '      uint32_t inner = item->kind == GSHSCENE_ITEM_GROUP_PUSH ? item->group.ordering : GSHSCENE_ORDER_FLAT;', O),
    ('ls-order-pop-emitted', 'src', LS, '      if (f->closing != SIZE_MAX) {\n        s->out[out_n++] = f->closing;\n      }', '', O),
    ('ls-order-root-flat', 'src', LS, '  frame_open(list, s, &frame_count, &unit_top, 0, n, GSHSCENE_ORDER_FLAT, SIZE_MAX);', '  frame_open(list, s, &frame_count, &unit_top, 0, n, GSHSCENE_ORDER_DEPTH, SIZE_MAX);', O),
    ('ls-order-permute-hold', 'src', LS, '    *item_at(list, j) = hold;\n    out[j] = j;\n  }', '    (void)hold;\n    out[j] = j;\n  }', O),
    ('ls-order-matching-pop', 'src', LS, '      s->match[s->open[--open_n]] = i;', '      s->match[s->open[--open_n]] = i - 1;', O),
    ('ls-order-ties-installed', 'src', LS, '  list->ties = ties;\n  list->tie_count = tie_n;\n  list->tie_capacity = tie_n;', '  list->ties = NULL;\n  list->tie_count = 0;\n  list->tie_capacity = 0;', O),
    ('ls-close-null', 'src', LS, 'GSHSCENE_Result gshscene_list_close(GSHSCENE_List * list) {\n  if (!list || list->closed || list->depth != 0) {', 'GSHSCENE_Result gshscene_list_close(GSHSCENE_List * list) {\n  if (list->closed || list->depth != 0) {', L),
    ('ls-close-twice', 'src', LS, 'GSHSCENE_Result gshscene_list_close(GSHSCENE_List * list) {\n  if (!list || list->closed || list->depth != 0) {', 'GSHSCENE_Result gshscene_list_close(GSHSCENE_List * list) {\n  if (!list || list->depth != 0) {', L),
    ('ls-close-open-push', 'src', LS, 'GSHSCENE_Result gshscene_list_close(GSHSCENE_List * list) {\n  if (!list || list->closed || list->depth != 0) {', 'GSHSCENE_Result gshscene_list_close(GSHSCENE_List * list) {\n  if (!list || list->closed) {', L),
    ('ls-close-seals', 'src', LS, '  list->closed = 1;\n  return GSHSCENE_OK;\n}\n\nstatic int tie_compare', '  return GSHSCENE_OK;\n}\n\nstatic int tie_compare', L),
    ('ls-close-resolve', 'src', LS, '  GSHSCENE_Result result = list_resolve(list);\n  if (result != GSHSCENE_OK) {\n    return result;\n  }\n  list->closed = 1;\n  return GSHSCENE_OK;\n}\n\nstatic int tie_compare', '  list->closed = 1;\n  return GSHSCENE_OK;\n}\n\nstatic int tie_compare', O),
    ('ls-close-resolve-error', 'src', LS, '  GSHSCENE_Result result = list_resolve(list);\n  if (result != GSHSCENE_OK) {\n    return result;\n  }\n  list->closed = 1;\n  return GSHSCENE_OK;\n}\n\nstatic int tie_compare', '  GSHSCENE_Result result = list_resolve(list);\n  (void)result;\n  list->closed = 1;\n  return GSHSCENE_OK;\n}\n\nstatic int tie_compare', O),
    ('ls-close-ordered-sort', 'src', LS, '  if (list->tie_count > 1) {\n    qsort(list->ties, list->tie_count, sizeof *list->ties, tie_compare);\n  }', '', O),
    ('ls-close-ordered-seals', 'src', LS, '    qsort(list->ties, list->tie_count, sizeof *list->ties, tie_compare);\n  }\n  list->closed = 1;', '    qsort(list->ties, list->tie_count, sizeof *list->ties, tie_compare);\n  }', ["testEmit", "testVersion"]),
    ('ls-tie-compare-direction', 'src', LS, '  return a->second < b->second ? -1 : (a->second > b->second ? 1 : 0);', '  return a->second < b->second ? 1 : (a->second > b->second ? -1 : 0);', O),
    ('ls-tie-add-first', 'src', LS, '  tie->first = first;\n  tie->second = second;\n  tie->depth = depth;', '(void)first;\n  tie->first = second;\n  tie->second = second;\n  tie->depth = depth;', O),
    ('ls-tie-add-depth', 'src', LS, '  tie->second = second;\n  tie->depth = depth;\n  tie->reason = reason;', '(void)depth;\n  tie->second = second;\n  tie->reason = reason;', O),
    ('ls-tie-add-reason', 'src', LS, '  tie->depth = depth;\n  tie->reason = reason;\n  return GSHSCENE_OK;', '(void)reason;\n  tie->depth = depth;\n  return GSHSCENE_OK;', O),
    ('ls-tie-add-grow-copy', 'src', LS, '    if (list->tie_count) {\n      memcpy(grown, list->ties, list->tie_count * sizeof *grown);\n    }', '', O),
    ('ls-tie-read-range', 'src', LS, '  if (!list || !out || !list->closed || index >= list->tie_count) {', '  if (!list || !out || !list->closed || index > list->tie_count) {', O),
    ('ls-copy-failure-leaks', 'src', LS, '  if (result != GSHSCENE_OK) {\n    gshscene_list_destroy(copy);\n    return result;\n  }\n  copy->closed = 1;', '  if (result != GSHSCENE_OK) {\n    return result;\n  }\n  copy->closed = 1;', ["testResource"]),
    ('ls-copy-ties', 'src', LS, '  for (size_t i = 0; result == GSHSCENE_OK && i < source->tie_count; i++) {\n    const GSHSCENE_Tie * t = &source->ties[i];\n    result = gshscene_list_add_tie(copy, t->first, t->second, t->depth, t->reason);\n  }\n', '', O),
    ('ls-picture-foreground', 'src', RS, '  if (!gshscene_color_is_default(&list->foreground) || list->palette) {', '  if (list->palette) {', CL),
    ('ls-picture-palette', 'src', RS, '  if (!gshscene_color_is_default(&list->foreground) || list->palette) {', '  if (!gshscene_color_is_default(&list->foreground)) {', ['testPalette']),
    # equality
    ('ls-eq-rule', 'src', LS, '      a->fill_rule != b->fill_rule || a->layer != b->layer ||', '      a->layer != b->layer ||', L),
    ('ls-eq-layer', 'src', LS, '      a->fill_rule != b->fill_rule || a->layer != b->layer ||', '      a->fill_rule != b->fill_rule ||', L),
    ('ls-eq-depth', 'src', LS, '      gshgeom_f64_bits(a->depth) != gshgeom_f64_bits(b->depth)) {', '      0) {', O + L),
    ('ls-eq-opacity', 'src', LS, '  return gshgeom_f64_bits(a->group.opacity) == gshgeom_f64_bits(b->group.opacity) &&', '  return 1 &&', L),
    ('ls-eq-blend', 'src', LS, '         gshscene_space_equal(&a->group.blend_space, &b->group.blend_space) &&', '         1 &&', CL),
    ('ls-eq-composite', 'src', LS, '         a->group.composite == b->group.composite && a->group.ordering == b->group.ordering &&', '         a->group.ordering == b->group.ordering &&', L + O + CP),
    ('ls-eq-ordering', 'src', LS, '         a->group.composite == b->group.composite && a->group.ordering == b->group.ordering &&', '         a->group.composite == b->group.composite &&', L + O),
    ('ls-eq-paint-kind', 'src', LS, '  if (a->paint.kind != b->paint.kind || !gshscene_space_equal(&a->paint.space, &b->paint.space) ||', '  if (!gshscene_space_equal(&a->paint.space, &b->paint.space) ||', CL + O),
    ('ls-eq-paint-space', 'src', LS, '  if (a->paint.kind != b->paint.kind || !gshscene_space_equal(&a->paint.space, &b->paint.space) ||', '  if (a->paint.kind != b->paint.kind ||', CL),
    ('ls-eq-null-a', 'src', LS, '  if (!a || !b || a->count != b->count || !gshscene_color_equal(&a->foreground, &b->foreground) || a->palette != b->palette) {', '  if (!b || a->count != b->count || !gshscene_color_equal(&a->foreground, &b->foreground) || a->palette != b->palette) {', L),
    ('ls-eq-null-b', 'src', LS, '  if (!a || !b || a->count != b->count || !gshscene_color_equal(&a->foreground, &b->foreground) || a->palette != b->palette) {', '  if (!a || a->count != b->count || !gshscene_color_equal(&a->foreground, &b->foreground) || a->palette != b->palette) {', L),
    ('ls-eq-count', 'src', LS, '  if (!a || !b || a->count != b->count || !gshscene_color_equal(&a->foreground, &b->foreground) || a->palette != b->palette) {', '  if (!a || !b || !gshscene_color_equal(&a->foreground, &b->foreground) || a->palette != b->palette) {', L),
    ('ls-eq-foreground', 'src', LS, '  if (!a || !b || a->count != b->count || !gshscene_color_equal(&a->foreground, &b->foreground) || a->palette != b->palette) {', '  if (!a || !b || a->count != b->count || a->palette != b->palette) {', CL),
    ('ls-eq-palette', 'src', LS, '  if (!a || !b || a->count != b->count || !gshscene_color_equal(&a->foreground, &b->foreground) || a->palette != b->palette) {', '  if (!a || !b || a->count != b->count || !gshscene_color_equal(&a->foreground, &b->foreground)) {', ['testPalette']),
    ('ls-eq-color-rgba', 'src', LS, '    if (gshgeom_f64_bits(a->rgba[i]) != gshgeom_f64_bits(b->rgba[i])) {\n      return 0;\n    }\n  }\n  return gshscene_space_equal(&a->space, &b->space);', '  }\n  return gshscene_space_equal(&a->space, &b->space);', CL),
    ('ls-eq-color-space', 'src', LS, '  return gshscene_space_equal(&a->space, &b->space);\n}\n\nstatic int item_equal', '  return 1;\n}\n\nstatic int item_equal', CL),
    ('ls-eq-space-primaries', 'src', LS, '  return a->primaries == b->primaries && a->transfer == b->transfer &&', '  return a->transfer == b->transfer &&', CL),
    ('ls-eq-space-transfer', 'src', LS, '  return a->primaries == b->primaries && a->transfer == b->transfer &&', '  return a->primaries == b->primaries &&', CL),
    ('ls-eq-space-gamma', 'src', LS, '         gshgeom_f64_bits(a->gamma) == gshgeom_f64_bits(b->gamma);', '         1;', CL),
    # hash
    ('ls-hash-foreground', 'src', LS, '  gshscene_color_hash(&h, &list->foreground);\n  gshgeom_hash_u64(&h, list->palette', '  gshgeom_hash_u64(&h, list->palette', D + CL),
    ('ls-hash-palette', 'src', LS, '  gshgeom_hash_u64(&h, list->palette ? gshscene_resource_hash(list->palette) : 0);\n  gshgeom_hash_u64(&h, (uint64_t)list->count);', '  gshgeom_hash_u64(&h, (uint64_t)list->count);', D + ['testPalette']),
    ('ls-hash-palette-content', 'src', LS, '  gshgeom_hash_u64(&h, list->palette ? gshscene_resource_hash(list->palette) : 0);', '  gshgeom_hash_u64(&h, list->palette ? 1u : 0);', D + ['testPalette']),
    ('ls-hash-fill-space', 'src', LS, '  gshscene_space_hash(h, &paint->space);\n  gshgeom_hash_u32(h, paint->entry);', '  gshgeom_hash_u32(h, paint->entry);', D),
    ('ls-hash-fill-entry', 'src', LS, '  gshgeom_hash_u32(h, paint->entry);\n', '', D + ["testPalette"]),
    ('ls-hash-fill-entry-width', 'src', LS, '  gshgeom_hash_u32(h, paint->entry);\n', '  gshgeom_hash_u64(h, paint->entry);\n', D),
    ('ls-hash-fill-layer-width', 'src', LS, '        gshgeom_hash_u8(&h, (uint8_t)item->fill_rule);\n        gshscene_paint_hash(&h, &item->paint);\n        gshgeom_hash_i32(&h, item->layer);', '        gshgeom_hash_u8(&h, (uint8_t)item->fill_rule);\n        gshscene_paint_hash(&h, &item->paint);\n        gshgeom_hash_i64(&h, item->layer);', D),
    ('ls-hash-fill-depth', 'src', LS, '        gshgeom_hash_i32(&h, item->layer);\n        gshgeom_hash_f64(&h, item->depth);\n        break;\n      case GSHSCENE_ITEM_CLIP_PUSH:', '        gshgeom_hash_i32(&h, item->layer);\n        break;\n      case GSHSCENE_ITEM_CLIP_PUSH:', D),
    ('ls-hash-clip-layer', 'src', LS, '        gshgeom_hash_u8(&h, (uint8_t)item->fill_rule);\n        gshgeom_hash_i32(&h, item->layer);\n        gshgeom_hash_f64(&h, item->depth);\n        break;\n      case GSHSCENE_ITEM_GROUP_PUSH:', '        gshgeom_hash_u8(&h, (uint8_t)item->fill_rule);\n        gshgeom_hash_f64(&h, item->depth);\n        break;\n      case GSHSCENE_ITEM_GROUP_PUSH:', D),
    ('ls-hash-clip-depth', 'src', LS, '        gshgeom_hash_u8(&h, (uint8_t)item->fill_rule);\n        gshgeom_hash_i32(&h, item->layer);\n        gshgeom_hash_f64(&h, item->depth);\n        break;\n      case GSHSCENE_ITEM_GROUP_PUSH:', '        gshgeom_hash_u8(&h, (uint8_t)item->fill_rule);\n        gshgeom_hash_i32(&h, item->layer);\n        break;\n      case GSHSCENE_ITEM_GROUP_PUSH:', D),
    ('ls-hash-group-blend', 'src', LS, '        gshscene_space_hash(&h, &item->group.blend_space);\n        gshscene_group_hash_composite(', '        gshscene_group_hash_composite(', D),
    ('ls-hash-group-composite-width', 'src', LS, '    gshgeom_hash_u8(h, (uint8_t)composite); /* the spelling', '    gshgeom_hash_u32(h, (uint32_t)composite); /* the spelling', D),
    ('ls-hash-group-composite', 'src', LS, '        gshscene_group_hash_composite(&h, item->group.composite, item->group.isolation, item->group.knockout,\n            item->group.soft_mask);\n', '', D),
    ('ls-hash-group-ordering-width', 'src', LS, '        gshgeom_hash_u32(&h, item->group.ordering);\n        gshgeom_hash_i32(&h, item->layer);', '        gshgeom_hash_u8(&h, (uint8_t)item->group.ordering);\n        gshgeom_hash_i32(&h, item->layer);', D),
    ('ls-hash-group-depth', 'src', LS, '        gshgeom_hash_i32(&h, item->layer);\n        gshgeom_hash_f64(&h, item->depth);\n        break;\n      case GSHSCENE_ITEM_INSTANCE:', '        gshgeom_hash_i32(&h, item->layer);\n        break;\n      case GSHSCENE_ITEM_INSTANCE:', D),
    ('ls-hash-instance-layer', 'src', LS, '      case GSHSCENE_ITEM_INSTANCE:\n        hash_resource_and_map(&h, item);\n        gshgeom_hash_i32(&h, item->layer);\n        gshgeom_hash_f64(&h, item->depth);\n        break;', '      case GSHSCENE_ITEM_INSTANCE:\n        hash_resource_and_map(&h, item);\n        gshgeom_hash_f64(&h, item->depth);\n        break;', D),
    ('ls-hash-instance-depth', 'src', LS, '      case GSHSCENE_ITEM_INSTANCE:\n        hash_resource_and_map(&h, item);\n        gshgeom_hash_i32(&h, item->layer);\n        gshgeom_hash_f64(&h, item->depth);\n        break;', '      case GSHSCENE_ITEM_INSTANCE:\n        hash_resource_and_map(&h, item);\n        gshgeom_hash_i32(&h, item->layer);\n        break;', D),
    ('ls-space-hash-primaries-width', 'src', LS, '  gshgeom_hash_u8(h, (uint8_t)space->primaries);', '  gshgeom_hash_u32(h, (uint32_t)space->primaries);', D),
    ('ls-space-hash-transfer-width', 'src', LS, '  gshgeom_hash_u8(h, (uint8_t)space->transfer);', '  gshgeom_hash_u32(h, (uint32_t)space->transfer);', D),
    ('ls-space-hash-order', 'src', LS, '  gshgeom_hash_u8(h, (uint8_t)space->primaries);\n  gshgeom_hash_u8(h, (uint8_t)space->transfer);', '  gshgeom_hash_u8(h, (uint8_t)space->transfer);\n  gshgeom_hash_u8(h, (uint8_t)space->primaries);', D),
    ('ls-space-hash-gamma', 'src', LS, '  gshgeom_hash_f64(h, space->gamma);', '', D),
    ('ls-color-hash-components', 'src', LS, '  for (int k = 0; k < 4; k++) {\n    gshgeom_hash_f64(h, color->rgba[k]);\n  }\n  gshscene_space_hash(h, &color->space);', '  for (int k = 0; k < 3; k++) {\n    gshgeom_hash_f64(h, color->rgba[k]);\n  }\n  gshscene_space_hash(h, &color->space);', D),
    ('ls-color-hash-space', 'src', LS, '  gshscene_space_hash(h, &color->space);', '', D),
    # dump
    ('ls-dump-null-list', 'src', LS, '  if (!list || !out) {\n    return GSHSCENE_ERR_INVALID;\n  }\n  /* The counts are printed', '  if (!out) {\n    return GSHSCENE_ERR_INVALID;\n  }\n  /* The counts are printed', D),
    ('ls-dump-null-out', 'src', LS, '  if (!list || !out) {\n    return GSHSCENE_ERR_INVALID;\n  }\n  /* The counts are printed', '  if (!list) {\n    return GSHSCENE_ERR_INVALID;\n  }\n  /* The counts are printed', D),
    ('ls-dump-header', 'src', LS, '"gshscene.list %u items=%llu ties=%llu fg=', '"gshscene.list %u count=%llu ties=%llu fg=', D),
    ('ls-dump-header-ties', 'src', LS, '"gshscene.list %u items=%llu ties=%llu fg=', '"gshscene.list %u items=%llu tie=%llu fg=', D),
    ('ls-dump-header-ties-value', 'src', LS, 'HEX(list->count), HEX(list->closed ? list->tie_count : 0),', 'HEX(list->count), HEX(0),', D),
    ('ls-dump-header-hash-width', 'src', LS, '" palette=%016llx hash=%016llx\\n"', '" palette=%016llx hash=%llx\\n"', D),
    ('ls-dump-header-palette-width', 'src', LS, '" palette=%016llx hash=%016llx\\n"', '" palette=%llx hash=%016llx\\n"', D),
    ('ls-dump-header-palette-hash', 'src', LS, 'HEX(list->palette ? gshscene_resource_hash(list->palette) : 0),', 'HEX(list->palette ? 1u : 0),', D),
    ('ls-dump-entry', 'src', LS, '" entry=%u gradient=%016llx", (unsigned)paint->entry,', '" entry=%u gradient=%016llx", 0u,', D),
    ('ls-dump-header-foreground', 'src', LS, '      gshscene_space_dump(out, &fg->space) < 0 ||\n', '', D),
    ('ls-dump-space-width', 'src', LS, '"%u:%u:%016llx"', '"%u:%u:%llx"', D),
    ('ls-dump-space-order', 'src', LS, '(unsigned)space->primaries, (unsigned)space->transfer,', '(unsigned)space->transfer, (unsigned)space->primaries,', D),
    ('ls-dump-fill-line', 'src', LS, '" paint=%u %016llx %016llx %016llx %016llx space="', '" kind=%u %016llx %016llx %016llx %016llx space="', D),
    ('ls-dump-fill-alpha-width', 'src', LS, '%016llx %016llx %016llx %016llx space="', '%016llx %016llx %016llx %llx space="', D),
    ('ls-dump-fill-space', 'src', LS, '    written = gshscene_space_dump(out, &paint->space);\n', '    written = 0;\n', D),
    ('ls-dump-group-line', 'src', LS, '"G opacity=%016llx blend="', '"G opacity=%llx blend="', D),
    ('ls-dump-group-blend', 'src', LS, '          written = gshscene_space_dump(out, &item->group.blend_space);\n', '          written = 0;\n', D),
    ('ls-dump-group-order', 'src', LS, '" composite=%u order=%u", (unsigned)item->group.composite, (unsigned)item->group.ordering);', '" composite=%u order=%u", (unsigned)item->group.ordering, (unsigned)item->group.composite);', D),
    ('ls-dump-layer-depth', 'src', LS, '" layer=%lld depth=%016llx", (long long)item->layer, HEX(gshgeom_f64_bits(item->depth)));', '" layer=%lld depth=%llx", (long long)item->layer, HEX(gshgeom_f64_bits(item->depth)));', D),
    ('ls-dump-layer', 'src', LS, '" layer=%lld depth=%016llx", (long long)item->layer,', '" layer=%lld depth=%016llx", 0LL,', D),
    ('ls-dump-tie-line', 'src', LS, '"T first=%llu second=%llu depth=%016llx reason=%u\\n"', '"U first=%llu second=%llu depth=%016llx reason=%u\\n"', D),
    ('ls-dump-tie-width', 'src', LS, '"T first=%llu second=%llu depth=%016llx reason=%u\\n"', '"T first=%llu second=%llu depth=%llx reason=%u\\n"', D),
    ('ls-dump-tie-order', 'src', LS, 'HEX(tie->first), HEX(tie->second),', 'HEX(tie->second), HEX(tie->first),', D),
    ('ls-dump-ties-guard', 'src', LS, '  if (list->closed) {\n    for (size_t i = 0; i < list->tie_count; i++) {', '  if (!list->closed) {\n    for (size_t i = 0; i < list->tie_count; i++) {', D),
    # the color space and color functions
    ('cs-target-gamma', 'src', CS, '         gshgeom_f64_bits(space->gamma) == 0;', '         1;', CL),
    ('cs-check-null', 'src', CS, '  if (!space) {\n    return GSHSCENE_ERR_INVALID;\n  }\n  if ((unsigned)space->primaries', '  if ((unsigned)space->primaries', CL),
    ('cs-check-gamma-only-gamma', 'src', CS, '  } else if (gshgeom_f64_bits(space->gamma) != 0) {', '  } else if (0) {', CL),
    ('cs-check-partial', 'src', CS, '  if (space->primaries == GCOL_PRIMARIES_UNKNOWN || space->transfer == GCOL_TRANSFER_UNKNOWN) {', '  if (space->primaries == GCOL_PRIMARIES_UNKNOWN && space->transfer == GCOL_TRANSFER_UNKNOWN) {', CL),
    ('cs-from-info-icc-bytes', 'src', CS, '  if (info->icc_bytes != NULL || info->icc_size != 0 || info->icc_linked_path != NULL) {', '  if (info->icc_size != 0 || info->icc_linked_path != NULL) {', CL),
    ('cs-from-info-icc-size', 'src', CS, '  if (info->icc_bytes != NULL || info->icc_size != 0 || info->icc_linked_path != NULL) {', '  if (info->icc_bytes != NULL || info->icc_linked_path != NULL) {', CL),
    ('cs-from-info-icc-path', 'src', CS, '  if (info->icc_bytes != NULL || info->icc_size != 0 || info->icc_linked_path != NULL) {', '  if (info->icc_bytes != NULL || info->icc_size != 0) {', CL),
    ('cs-from-info-gamma', 'src', CS, '  space.gamma = info->transfer == GCOL_TRANSFER_GAMMA ? info->gamma_value : 0.0;', '  space.gamma = info->gamma_value;', CL),
    ('cs-from-info-tolerance', 'src', CS, 'gcol_gamut_identify(&info->gamut, GCOL_GAMUT_TOLERANCE_DEFAULT)', 'gcol_gamut_identify(&info->gamut, 1.0e-9)', CL),
    ('cs-color-default-alpha', 'src', CS, '  color->rgba[3] = 1.0;', '  color->rgba[3] = 0.0;', CL),
    ('cs-color-alpha-range', 'src', CS, '  if (!(color->rgba[3] >= 0.0 && color->rgba[3] <= 1.0)) {', '  if (!(color->rgba[3] >= 0.0 && color->rgba[3] < 1.0)) {', CL),
    ('cs-color-default-check', 'src', CS, '  return gshscene_color_space_is_target(&color->space);\n}', '  return 1;\n}', CL),
]


# The reference and the evaluator (story 12): the oracle (kind orc: its selftest or the vectors it writes must fail) and the
# test-only evaluator (kind tst: the tests are recompiled with the planted header).
PLANTS += [
    ('orc-clear', 'orc', 'tools/composite-oracle.py', '    if op == "clear":\n        return ZERO, ZERO', '    if op == "clear":\n        return ONE, ZERO', ['selftest', 'vectors']),
    ('orc-src', 'orc', 'tools/composite-oracle.py', '    if op == "src":\n        return ONE, ZERO', '    if op == "src":\n        return ONE, ONE', ['selftest', 'vectors']),
    ('orc-dest', 'orc', 'tools/composite-oracle.py', '    if op == "dest":\n        return ZERO, ONE', '    if op == "dest":\n        return ONE, ONE', ['selftest', 'vectors']),
    ('orc-src-over', 'orc', 'tools/composite-oracle.py', 'return ONE, ONE - sa\n    if op == "dest-over":', 'return ONE, ONE - da\n    if op == "dest-over":', ['selftest', 'vectors']),
    ('orc-dest-over', 'orc', 'tools/composite-oracle.py', 'return ONE - da, ONE\n    if op == "src-in":', 'return ONE - sa, ONE\n    if op == "src-in":', ['selftest', 'vectors']),
    ('orc-src-in', 'orc', 'tools/composite-oracle.py', 'return da, ZERO\n    if op == "dest-in":', 'return sa, ZERO\n    if op == "dest-in":', ['selftest', 'vectors']),
    ('orc-dest-in', 'orc', 'tools/composite-oracle.py', 'return ZERO, sa\n    if op == "src-out":', 'return ZERO, da\n    if op == "src-out":', ['selftest', 'vectors']),
    ('orc-src-out', 'orc', 'tools/composite-oracle.py', 'return ONE - da, ZERO\n    if op == "dest-out":', 'return ONE - sa, ZERO\n    if op == "dest-out":', ['selftest', 'vectors']),
    ('orc-dest-out', 'orc', 'tools/composite-oracle.py', 'return ZERO, ONE - sa\n    if op == "src-atop":', 'return ZERO, ONE - da\n    if op == "src-atop":', ['selftest', 'vectors']),
    ('orc-src-atop', 'orc', 'tools/composite-oracle.py', 'return da, ONE - sa\n    if op == "dest-atop":', 'return sa, ONE - sa\n    if op == "dest-atop":', ['selftest', 'vectors']),
    ('orc-dest-atop', 'orc', 'tools/composite-oracle.py', 'return ONE - da, sa\n    if op == "xor":', 'return ONE - da, da\n    if op == "xor":', ['selftest', 'vectors']),
    ('orc-xor', 'orc', 'tools/composite-oracle.py', 'return ONE - da, ONE - sa\n    raise ValueError(op)', 'return ONE - sa, ONE - sa\n    raise ValueError(op)', ['selftest', 'vectors']),
    ('orc-plus-clamp', 'orc', 'tools/composite-oracle.py', 'return [min(ONE, s[i] + d[i]) for i in range(4)]', 'return [s[i] + d[i] for i in range(4)]', ['selftest', 'vectors']),
    ('orc-apply-coverage-source', 'orc', 'tools/composite-oracle.py', 'return [sat((ONE - m) * d[i] + m * o[i]) for i in range(4)]', 'return [sat((ONE - m) * d[i] + o[i]) for i in range(4)]', ['selftest', 'vectors']),
    ('orc-apply-coverage-backdrop', 'orc', 'tools/composite-oracle.py', 'return [sat((ONE - m) * d[i] + m * o[i]) for i in range(4)]', 'return [sat(d[i] + m * o[i]) for i in range(4)]', ['selftest', 'vectors']),
    ('orc-premultiply', 'orc', 'tools/composite-oracle.py', 'return [rgb[0] * a, rgb[1] * a, rgb[2] * a, a]', 'return [rgb[0] * a, rgb[1] * a, rgb[2], a]', ['selftest', 'vectors']),
    ('orc-fill-clips', 'orc', 'tools/composite-oracle.py', 'm = cov[i] * product(top.clips)', 'm = cov[i]', ['selftest', 'vectors']),
    ('orc-fill-convert', 'orc', 'tools/composite-oracle.py', 'source = premultiply(convert(rgb, space, top.space, ex), alpha)', 'source = premultiply(rgb, alpha)', ['selftest', 'vectors']),
    ('orc-fill-foreground-alpha', 'orc', 'tools/composite-oracle.py', 'alpha = F(fg[0][3]) * F(it["rgba"][3])', 'alpha = F(fg[0][3])', ['selftest', 'vectors']),
    ('orc-fill-foreground-color', 'orc', 'tools/composite-oracle.py', 'rgb = [F(v) for v in fg[0][:3]]', 'rgb = [F(v) for v in it["rgba"][:3]]', ['selftest', 'vectors']),
    ('orc-fill-foreground-space', 'orc', 'tools/composite-oracle.py', 'space = target if is_target(fg[1]) else fg[1]', 'space = target', ['selftest', 'vectors']),
    ('orc-fill-paint-space', 'orc', 'tools/composite-oracle.py', 'space = target if is_target(it["space"]) else it["space"]', 'space = it["space"] if not is_target(it["space"]) else top.space', ['selftest', 'vectors']),
    ('orc-clip-push', 'orc', 'tools/composite-oracle.py', 'top.clips.append(cov[i])', 'top.clips.append(ONE)', ['selftest', 'vectors']),
    ('orc-group-outer-clip', 'orc', 'tools/composite-oracle.py', 'outer=product(top.clips)))', 'outer=ONE))', ['selftest', 'vectors']),
    ('orc-group-blend-inherit', 'orc', 'tools/composite-oracle.py', 'space = top.space if is_target(it["blend"]) else it["blend"]\n            stack.append', 'space = target if is_target(it["blend"]) else it["blend"]\n            stack.append', ['selftest', 'vectors']),
    ('orc-group-layer-start', 'orc', 'tools/composite-oracle.py', 'stack.append(Frame([ZERO] * 4, space,', 'stack.append(Frame(list(top.layer), space,', ['selftest', 'vectors']),
    ('orc-group-convert', 'orc', 'tools/composite-oracle.py', 'moved = convert_premultiplied(done.layer, done.space, parent.space, ex)', 'moved = list(done.layer)', ['selftest', 'vectors']),
    ('orc-group-opacity', 'orc', 'tools/composite-oracle.py', 'source = [opacity * v for v in moved]', 'source = list(moved)', ['selftest', 'vectors']),
    ('orc-group-operator', 'orc', 'tools/composite-oracle.py', 'op = OPS[done.group["composite"]]', 'op = "src-over"', ['selftest', 'vectors']),
    ('orc-group-coverage', 'orc', 'tools/composite-oracle.py', 'parent.layer = apply(parent.layer, source, done.outer, op, ex)', 'parent.layer = apply(parent.layer, source, ONE, op, ex)', ['selftest', 'vectors']),
    ('orc-convert-identity', 'orc', 'tools/composite-oracle.py', '    if a == b:\n        return list(rgb)\n    # N1', '    # N1', ['selftest', 'vectors']),
    ('orc-convert-decode', 'orc', 'tools/composite-oracle.py', 'lin = [sat(decode(a, v, ex)) for v in rgb]', 'lin = [sat(v) for v in rgb]', ['selftest', 'vectors']),
    ('orc-convert-encode', 'orc', 'tools/composite-oracle.py', 'return [sat(encode(b, v, ex)) for v in lin]', 'return [sat(v) for v in lin]', ['selftest', 'vectors']),
    ('orc-convert-matrix-skip', 'orc', 'tools/composite-oracle.py', '    if a[0] != b[0]:\n        lin = [sat(v) for v in matvec', '    if False:\n        lin = [sat(v) for v in matvec', ['selftest', 'vectors']),
    ('orc-convert-matrix-order', 'orc', 'tools/composite-oracle.py', 'matvec(MATRIX_INVERSE[b[0]], matvec(MATRIX[a[0]], lin))', 'matvec(MATRIX_INVERSE[a[0]], matvec(MATRIX[b[0]], lin))', ['selftest', 'vectors']),
    ('orc-premultiplied-conversion-zero', 'orc', 'tools/composite-oracle.py', 'if alpha == 0:\n        return [ZERO, ZERO, ZERO, ZERO]', 'if alpha == 0:\n        return [ZERO, ZERO, ZERO, ONE]', ['selftest', 'vectors']),
    ('orc-premultiplied-unpremultiply', 'orc', 'tools/composite-oracle.py', 'straight = [sat(px[i] / alpha) for i in range(3)]', 'straight = [sat(px[i]) for i in range(3)]', ['selftest', 'vectors']),
    ('orc-srgb-threshold', 'orc', 'tools/composite-oracle.py', 'if x <= F("0.04045"):', 'if x <= F("0.04046"):', ['selftest', 'vectors']),
    ('orc-srgb-slope', 'orc', 'tools/composite-oracle.py', 'return x / F("12.92")', 'return x / F("12.9")', ['selftest', 'vectors']),
    ('orc-srgb-offset', 'orc', 'tools/composite-oracle.py', '(x + F("0.055")) / F("1.055"), F("2.4"), ex)', '(x + F("0.056")) / F("1.055"), F("2.4"), ex)', ['selftest', 'vectors']),
    ('orc-srgb-scale', 'orc', 'tools/composite-oracle.py', '(x + F("0.055")) / F("1.055"), F("2.4"), ex)', '(x + F("0.055")) / F("1.06"), F("2.4"), ex)', ['selftest', 'vectors']),
    ('orc-srgb-exponent', 'orc', 'tools/composite-oracle.py', '(x + F("0.055")) / F("1.055"), F("2.4"), ex)', '(x + F("0.055")) / F("1.055"), F("2.3"), ex)', ['selftest', 'vectors']),
    ('orc-srgb-encode-threshold', 'orc', 'tools/composite-oracle.py', 'if y <= F("0.0031308"):', 'if y <= F("0.0031309"):', ['selftest', 'vectors']),
    ('orc-srgb-encode-slope', 'orc', 'tools/composite-oracle.py', 'return y * F("12.92")', 'return y * F("12.9")', ['selftest', 'vectors']),
    ('orc-srgb-encode-scale', 'orc', 'tools/composite-oracle.py', 'return F("1.055") * power(y, ONE / F("2.4"), ex) - F("0.055")', 'return F("1.05") * power(y, ONE / F("2.4"), ex) - F("0.055")', ['selftest', 'vectors']),
    ('orc-srgb-encode-offset', 'orc', 'tools/composite-oracle.py', 'return F("1.055") * power(y, ONE / F("2.4"), ex) - F("0.055")', 'return F("1.055") * power(y, ONE / F("2.4"), ex) - F("0.05")', ['selftest', 'vectors']),
    ('orc-srgb-encode-exponent', 'orc', 'tools/composite-oracle.py', 'power(y, ONE / F("2.4"), ex)', 'power(y, ONE / F("2.2"), ex)', ['selftest', 'vectors']),
    ('orc-gamma-sign', 'orc', 'tools/composite-oracle.py', 'return sign(x) * power(abs(x), g, ex)', 'return power(abs(x), g, ex)', ['selftest', 'vectors']),
    ('orc-gamma-encode-sign', 'orc', 'tools/composite-oracle.py', 'return sign(y) * F(to_dec(abs(y)) ** (Decimal(1) / to_dec(g))) if not (g == 1) else y', 'return F(to_dec(abs(y)) ** (Decimal(1) / to_dec(g))) if not (g == 1) else y', ['selftest', 'vectors']),
    ('orc-gamut-srgb-green', 'orc', 'tools/composite-oracle.py', '1: ((F("0.64"), F("0.33")), (F("0.30"), F("0.60")), (F("0.15"), F("0.06"))),', '1: ((F("0.64"), F("0.33")), (F("0.31"), F("0.60")), (F("0.15"), F("0.06"))),', ['selftest', 'vectors']),
    ('orc-gamut-adobe-green', 'orc', 'tools/composite-oracle.py', '(F("0.21"), F("0.71"))', '(F("0.20"), F("0.71"))', ['selftest', 'vectors']),
    ('orc-gamut-p3-red', 'orc', 'tools/composite-oracle.py', '3: ((F("0.680"), F("0.320"))', '3: ((F("0.681"), F("0.320"))', ['selftest', 'vectors']),
    ('orc-gamut-bt2020-blue', 'orc', 'tools/composite-oracle.py', '(F("0.131"), F("0.046"))', '(F("0.131"), F("0.047"))', ['selftest', 'vectors']),
    ('orc-white-point', 'orc', 'tools/composite-oracle.py', 'WHITE_D65 = (F("0.3127"), F("0.3290"))', 'WHITE_D65 = (F("0.3127"), F("0.3291"))', ['selftest', 'vectors']),
    ('orc-matrix-scale', 'orc', 'tools/composite-oracle.py', 'return [[s[c] * cols[c][r] for c in range(3)] for r in range(3)]', 'return [[cols[c][r] for c in range(3)] for r in range(3)]', ['selftest', 'vectors']),
    ('orc-matrix-sign', 'orc', 'tools/composite-oracle.py', 'cof[i][j] = (-1) ** (i + j) * minor', 'cof[i][j] = minor', ['selftest', 'vectors']),
    ('orc-matrix-white-y', 'orc', 'tools/composite-oracle.py', 'cols.append((x / y, ONE, (ONE - x - y) / y))', 'cols.append((x / y, ONE, (ONE - x) / y))', ['selftest', 'vectors']),
    ('orc-order-depth-direction', 'orc', 'tools/composite-oracle.py', 'indexed.sort(key=lambda e: (-items[e[1][0]]["depth"], items[e[1][0]]["layer"], e[0]))', 'indexed.sort(key=lambda e: (items[e[1][0]]["depth"], items[e[1][0]]["layer"], e[0]))', ['selftest', 'vectors']),
    ('orc-order-depth-layer', 'orc', 'tools/composite-oracle.py', 'indexed.sort(key=lambda e: (-items[e[1][0]]["depth"], items[e[1][0]]["layer"], e[0]))', 'indexed.sort(key=lambda e: (-items[e[1][0]]["depth"], -items[e[1][0]]["layer"], e[0]))', ['selftest', 'vectors']),
    ('orc-order-depth-stable', 'orc', 'tools/composite-oracle.py', 'indexed.sort(key=lambda e: (-items[e[1][0]]["depth"], items[e[1][0]]["layer"], e[0]))', 'indexed.sort(key=lambda e: (-items[e[1][0]]["depth"], items[e[1][0]]["layer"], -e[0]))', ['selftest', 'vectors']),
    ('orc-order-overlay-layer', 'orc', 'tools/composite-oracle.py', 'indexed.sort(key=lambda e: (items[e[1][0]]["layer"], e[0]))', 'indexed.sort(key=lambda e: (-items[e[1][0]]["layer"], e[0]))', ['selftest', 'vectors']),
    ('orc-order-overlay-stable', 'orc', 'tools/composite-oracle.py', 'indexed.sort(key=lambda e: (items[e[1][0]]["layer"], e[0]))', 'indexed.sort(key=lambda e: (items[e[1][0]]["layer"], -e[0]))', ['selftest', 'vectors']),
    ('orc-order-tie-reason', 'orc', 'tools/composite-oracle.py', 'reason = 1 if before["layer"] != after["layer"] else 3', 'reason = 3 if before["layer"] != after["layer"] else 1', ['selftest', 'vectors']),
    ('orc-order-inner-context', 'orc', 'tools/composite-oracle.py', 'inner = items[start]["order"] if items[start]["k"] == GROUP_PUSH else context', 'inner = ORDER_FLAT', ['selftest', 'vectors']),
    ('orc-order-tie-depth', 'orc', 'tools/composite-oracle.py', 'if before["depth"] == after["depth"]:', 'if before["depth"] >= after["depth"]:', ['selftest', 'vectors']),
    ('orc-unbounded-probe', 'orc', 'tools/composite-oracle.py', 'return operator(op, zero, probe) != probe', 'return operator(op, probe, zero) != probe', ['selftest', 'vectors']),
    # B1 to B12 (color-paint story P3): each mode, Lum, ClipColor, SetSat, B9's saturation and cap, B2's terms, B3, B5.
    ('orc-b-screen', 'orc', 'tools/composite-oracle.py', '    return b + s - b * s\n', '    return b + s + b * s\n', ['selftest', 'vectors']),
    ('orc-b-overlay-exchanged', 'orc', 'tools/composite-oracle.py', '"overlay": lambda b, s, ex: mix_hard_light(s, b),', '"overlay": lambda b, s, ex: mix_hard_light(b, s),', ['selftest', 'vectors']),
    ('orc-b-darken-max', 'orc', 'tools/composite-oracle.py', '"darken": lambda b, s, ex: min(b, s),', '"darken": lambda b, s, ex: max(b, s),', ['selftest', 'vectors']),
    ('orc-b-lighten-min', 'orc', 'tools/composite-oracle.py', '"lighten": lambda b, s, ex: max(b, s),', '"lighten": lambda b, s, ex: min(b, s),', ['selftest', 'vectors']),
    ('orc-b-dodge-zero', 'orc', 'tools/composite-oracle.py', '    if b == 0:\n        return ZERO\n    if s == 1:', '    if b == 0:\n        return ONE\n    if s == 1:', ['selftest', 'vectors']),
    ('orc-b-dodge-one', 'orc', 'tools/composite-oracle.py', '    if s == 1:\n        return ONE\n    return min(ONE, b / (ONE - s))', '    if s == 1:\n        return ZERO\n    return min(ONE, b / (ONE - s))', ['selftest', 'vectors']),
    ('orc-b-dodge-clamp', 'orc', 'tools/composite-oracle.py', 'return min(ONE, b / (ONE - s))  # B10', 'return b / (ONE - s)  # B10', ['selftest', 'vectors']),
    ('orc-b-burn-one', 'orc', 'tools/composite-oracle.py', '    if b == 1:\n        return ONE\n    if s == 0:\n        return ZERO\n    return ONE - min(ONE, (ONE - b) / s)', '    if b == 1:\n        return ZERO\n    if s == 0:\n        return ZERO\n    return ONE - min(ONE, (ONE - b) / s)', ['selftest', 'vectors']),
    ('orc-b-burn-quotient', 'orc', 'tools/composite-oracle.py', 'return ONE - min(ONE, (ONE - b) / s)  # B10', 'return ONE - min(ONE, b / s)  # B10', ['selftest', 'vectors']),
    ('orc-b-hard-light-double', 'orc', 'tools/composite-oracle.py', 'return mix_multiply(b, 2 * s) if s <= HALF', 'return mix_multiply(b, s) if s <= HALF', ['selftest', 'vectors']),
    ('orc-b-hard-light-screen', 'orc', 'tools/composite-oracle.py', 'else mix_screen(b, 2 * s - 1)', 'else mix_screen(b, 2 * s)', ['selftest', 'vectors']),
    ('orc-b-soft-light-low', 'orc', 'tools/composite-oracle.py', '        return b - (1 - 2 * s) * b * (1 - b)\n', '        return b - (1 - s) * b * (1 - b)\n', ['selftest', 'vectors']),
    ('orc-b-soft-light-d-constant', 'orc', 'tools/composite-oracle.py', 'dee = ((16 * b - 12) * b + 4) * b  # B10', 'dee = ((16 * b - 11) * b + 4) * b  # B10', ['selftest', 'vectors']),
    ('orc-b-soft-light-root', 'orc', 'tools/composite-oracle.py', '        dee = root(b, ex)  # b > 1/4', '        dee = b  # b > 1/4', ['selftest', 'vectors']),
    ('orc-b-soft-light-high', 'orc', 'tools/composite-oracle.py', '    return b + (2 * s - 1) * (dee - b)\n', '    return b + (2 * s - 1) * (dee + b)\n', ['selftest', 'vectors']),
    ('orc-b-difference-abs', 'orc', 'tools/composite-oracle.py', '"difference": lambda b, s, ex: abs(b - s),', '"difference": lambda b, s, ex: b - s,', ['selftest', 'vectors']),
    ('orc-b-exclusion', 'orc', 'tools/composite-oracle.py', '"exclusion": lambda b, s, ex: b + s - 2 * b * s,', '"exclusion": lambda b, s, ex: b + s - b * s,', ['selftest', 'vectors']),
    ('orc-b-multiply', 'orc', 'tools/composite-oracle.py', 'def mix_multiply(b, s):\n    return b * s', 'def mix_multiply(b, s):\n    return b * s * s', ['selftest', 'vectors']),
    ('orc-b-lum-coefficient', 'orc', 'tools/composite-oracle.py', 'LUM = (F(3, 10), F(59, 100), F(11, 100))', 'LUM = (F(3, 10), F(59, 100), F(1, 10))', ['selftest', 'vectors']),
    ('orc-b-clip-recomputed-x', 'orc', 'tools/composite-oracle.py', '    if x > 1 and n != x:\n        out = [el + (v - el) * (1 - el) / (x - el) for v in out]', '    if max(out) > 1 and n != x:\n        out = [el + (v - el) * (1 - el) / (max(out) - el) for v in out]', ['selftest', 'vectors']),
    ('orc-b-clip-equal', 'orc', 'tools/composite-oracle.py', '    if n < 0 and n != x:\n', '    if n < 0:\n', ['selftest', 'vectors']),
    ('orc-b-clip-low-factor', 'orc', 'tools/composite-oracle.py', 'out = [el + (v - el) * el / (el - n) for v in out]', 'out = [el + (v - el) / (el - n) for v in out]', ['selftest', 'vectors']),
    ('orc-b-setsat-order', 'orc', 'tools/composite-oracle.py', '    imin, imid, imax = order if order is not None else sorted(range(3), key=lambda i: c[i])', '    imin, imax, imid = order if order is not None else sorted(range(3), key=lambda i: c[i])', ['selftest', 'vectors']),
    ('orc-b-setsat-scale', 'orc', 'tools/composite-oracle.py', '        out[imax] = s\n', '        out[imax] = s / 2\n', ['selftest', 'vectors']),
    ('orc-b-hue', 'orc', 'tools/composite-oracle.py', 'return set_lum(set_sat(cs, saturation(cb)), lum(cb))', 'return set_lum(set_sat(cs, saturation(cs)), lum(cb))', ['selftest', 'vectors']),
    ('orc-b-saturation', 'orc', 'tools/composite-oracle.py', 'return set_lum(set_sat(cb, saturation(cs)), lum(cb))', 'return set_lum(set_sat(cb, saturation(cs)), lum(cs))', ['selftest', 'vectors']),
    ('orc-b-color', 'orc', 'tools/composite-oracle.py', 'return set_lum(cs, lum(cb))', 'return set_lum(cb, lum(cs))', ['selftest', 'vectors']),
    ('orc-b-luminosity', 'orc', 'tools/composite-oracle.py', 'return set_lum(cb, lum(cs))', 'return set_lum(cb, lum(cb))', ['selftest', 'vectors']),
    ('orc-b-saturation-after-mix', 'orc', 'tools/composite-oracle.py', 'b = [sat(v) for v in mix(mode, straight(d), straight(s), ex)]  # B9', 'b = mix(mode, straight(d), straight(s), ex)  # B9', ['selftest', 'vectors']),
    ('orc-b-cap', 'orc', 'tools/composite-oracle.py', 'QUOTIENT_CAP = F(2) ** 66', 'QUOTIENT_CAP = F(2) ** 60', ['selftest', 'vectors']),
    ('orc-b-term-alphas', 'orc', 'tools/composite-oracle.py', 'color = [s[i] * (ONE - da) + d[i] * (ONE - sa) + sa * da * b[i] for i in range(3)]', 'color = [s[i] * (ONE - da) + d[i] * (ONE - sa) + sa * b[i] for i in range(3)]', ['selftest', 'vectors']),
    ('orc-b-alpha', 'orc', 'tools/composite-oracle.py', '    return color + [sa + da * (ONE - sa)]', '    return color + [sa + da]', ['selftest', 'vectors']),
    ('orc-b-bounded', 'orc', 'tools/composite-oracle.py', 'color = [s[i] * (ONE - da) + d[i] * (ONE - sa) + sa', 'color = [s[i] * (ONE - da) + d[i] * (ONE - sa) * da + sa', ['selftest', 'vectors']),
    ('orc-b-unpremultiply', 'orc', 'tools/composite-oracle.py', '    return [sat(px[i] / a) if a != 0 else ZERO for i in range(3)]', '    return [sat(px[i]) if a != 0 else ZERO for i in range(3)]', ['selftest', 'vectors']),
    ('orc-b-space-of-the-group', 'orc', 'tools/composite-oracle.py', '            parent.layer = apply(parent.layer, source, done.outer, op, ex)', '            parent.layer = (convert_premultiplied(apply(convert_premultiplied(parent.layer, parent.space, done.space, ex), [opacity * v for v in done.layer], done.outer, op, ex), done.space, parent.space, ex) if op in BLEND_INDEX else apply(parent.layer, source, done.outer, op, ex))', ['selftest', 'vectors']),
    ('tst-clear', 'tst', 'tests/ref_composite.h', 'case GSHSCENE_COMPOSITE_CLEAR: fa = 0; fb = 0; break;', 'case GSHSCENE_COMPOSITE_CLEAR: fa = 1; fb = 0; break;', ['testComposite']),
    ('tst-src', 'tst', 'tests/ref_composite.h', 'case GSHSCENE_COMPOSITE_SRC: fa = 1; fb = 0; break;', 'case GSHSCENE_COMPOSITE_SRC: fa = 1; fb = 1; break;', ['testComposite']),
    ('tst-dest', 'tst', 'tests/ref_composite.h', 'case GSHSCENE_COMPOSITE_DEST: fa = 0; fb = 1; break;', 'case GSHSCENE_COMPOSITE_DEST: fa = 1; fb = 1; break;', ['testComposite']),
    ('tst-src-over', 'tst', 'tests/ref_composite.h', 'case GSHSCENE_COMPOSITE_SRC_OVER: fa = 1; fb = 1 - sa; break;', 'case GSHSCENE_COMPOSITE_SRC_OVER: fa = 1; fb = 1 - da; break;', ['testComposite']),
    ('tst-dest-over', 'tst', 'tests/ref_composite.h', 'case GSHSCENE_COMPOSITE_DEST_OVER: fa = 1 - da; fb = 1; break;', 'case GSHSCENE_COMPOSITE_DEST_OVER: fa = 1 - sa; fb = 1; break;', ['testComposite']),
    ('tst-src-in', 'tst', 'tests/ref_composite.h', 'case GSHSCENE_COMPOSITE_SRC_IN: fa = da; fb = 0; break;', 'case GSHSCENE_COMPOSITE_SRC_IN: fa = sa; fb = 0; break;', ['testComposite']),
    ('tst-dest-in', 'tst', 'tests/ref_composite.h', 'case GSHSCENE_COMPOSITE_DEST_IN: fa = 0; fb = sa; break;', 'case GSHSCENE_COMPOSITE_DEST_IN: fa = 0; fb = da; break;', ['testComposite']),
    ('tst-src-out', 'tst', 'tests/ref_composite.h', 'case GSHSCENE_COMPOSITE_SRC_OUT: fa = 1 - da; fb = 0; break;', 'case GSHSCENE_COMPOSITE_SRC_OUT: fa = 1 - sa; fb = 0; break;', ['testComposite']),
    ('tst-dest-out', 'tst', 'tests/ref_composite.h', 'case GSHSCENE_COMPOSITE_DEST_OUT: fa = 0; fb = 1 - sa; break;', 'case GSHSCENE_COMPOSITE_DEST_OUT: fa = 0; fb = 1 - da; break;', ['testComposite']),
    ('tst-src-atop', 'tst', 'tests/ref_composite.h', 'case GSHSCENE_COMPOSITE_SRC_ATOP: fa = da; fb = 1 - sa; break;', 'case GSHSCENE_COMPOSITE_SRC_ATOP: fa = sa; fb = 1 - sa; break;', ['testComposite']),
    ('tst-dest-atop', 'tst', 'tests/ref_composite.h', 'case GSHSCENE_COMPOSITE_DEST_ATOP: fa = 1 - da; fb = sa; break;', 'case GSHSCENE_COMPOSITE_DEST_ATOP: fa = 1 - da; fb = da; break;', ['testComposite']),
    ('tst-xor', 'tst', 'tests/ref_composite.h', 'case GSHSCENE_COMPOSITE_XOR: fa = 1 - da; fb = 1 - sa; break;', 'case GSHSCENE_COMPOSITE_XOR: fa = 1 - sa; fb = 1 - sa; break;', ['testComposite']),
    ('tst-plus-clamp', 'tst', 'tests/ref_composite.h', 'composite == GSHSCENE_COMPOSITE_PLUS ? std::min(1.0, s.v[i] + d.v[i]) : fa * s.v[i] + fb * d.v[i]', 'composite == GSHSCENE_COMPOSITE_PLUS ? s.v[i] + d.v[i] : fa * s.v[i] + fb * d.v[i]', ['testComposite']),
    ('tst-apply-source', 'tst', 'tests/ref_composite.h', 'out.v[i] = sat((1 - m) * d.v[i] + m * o.v[i]);', 'out.v[i] = sat((1 - m) * d.v[i] + o.v[i]);', ['testComposite']),
    ('tst-apply-backdrop', 'tst', 'tests/ref_composite.h', 'out.v[i] = sat((1 - m) * d.v[i] + m * o.v[i]);', 'out.v[i] = sat(d.v[i] + m * o.v[i]);', ['testComposite']),
    ('tst-fill-clips', 'tst', 'tests/ref_composite.h', 'top.layer = apply(top.layer, s, coverage[i] * product(top.clips), GSHSCENE_COMPOSITE_SRC_OVER);', 'top.layer = apply(top.layer, s, coverage[i], GSHSCENE_COMPOSITE_SRC_OVER);', ['testComposite']),
    ('tst-fill-operator', 'tst', 'tests/ref_composite.h', 'top.layer = apply(top.layer, s, coverage[i] * product(top.clips), GSHSCENE_COMPOSITE_SRC_OVER);', 'top.layer = apply(top.layer, s, coverage[i] * product(top.clips), GSHSCENE_COMPOSITE_SRC);', ['testComposite']),
    ('tst-foreground-alpha', 'tst', 'tests/ref_composite.h', 'alpha = fgc.rgba[3] * item->paint.rgba[3];', 'alpha = fgc.rgba[3];', ['testComposite']),
    ('tst-foreground-color', 'tst', 'tests/ref_composite.h', 'rgb[k] = fgc.rgba[k];', 'rgb[k] = item->paint.rgba[k];', ['testComposite']),
    ('tst-foreground-space', 'tst', 'tests/ref_composite.h', 'space = from(fgc.space);', 'space = target;', ['testComposite']),
    ('tst-paint-target-space', 'tst', 'tests/ref_composite.h', '        if (is_target(space)) {\n          space = target;\n        }\n        double c[3];\n        convert(rgb,', '        double c[3];\n        convert(rgb,', ['testComposite']),
    ('tst-fill-convert', 'tst', 'tests/ref_composite.h', '        convert(rgb, space, top.space, c);', '        c[0] = rgb[0]; c[1] = rgb[1]; c[2] = rgb[2];', ['testComposite']),
    ('tst-clip-push', 'tst', 'tests/ref_composite.h', '      case GSHSCENE_ITEM_CLIP_PUSH:\n        top.clips.push_back(coverage[i]);\n        break;\n      case GSHSCENE_ITEM_CLIP_POP:\n        top.clips.pop_back();\n        break;\n      case GSHSCENE_ITEM_GROUP_PUSH: {\n        Frame f;', '      case GSHSCENE_ITEM_CLIP_PUSH:\n        top.clips.push_back(1.0);\n        break;\n      case GSHSCENE_ITEM_CLIP_POP:\n        top.clips.pop_back();\n        break;\n      case GSHSCENE_ITEM_GROUP_PUSH: {\n        Frame f;', ['testComposite']),
    ('tst-group-blend-inherit', 'tst', 'tests/ref_composite.h', 'f.space = is_target(blend) ? top.space : blend;', 'f.space = is_target(blend) ? target : blend;', ['testComposite']),
    ('tst-group-outer', 'tst', 'tests/ref_composite.h', 'f.outer = product(top.clips);', 'f.outer = 1.0;', ['testComposite']),
    ('tst-group-layer', 'tst', 'tests/ref_composite.h', 'f.layer = Px{{0, 0, 0, 0}};', 'f.layer = top.layer;', ['testComposite']),
    ('tst-group-convert', 'tst', 'tests/ref_composite.h', 'Px moved = convert_premultiplied(done.layer, done.space, parent.space);', 'Px moved = done.layer;', ['testComposite']),
    ('tst-group-opacity', 'tst', 'tests/ref_composite.h', 'done.group.opacity * moved.v[3]}};', 'moved.v[3]}};', ['testComposite']),
    ('tst-group-opacity-color', 'tst', 'tests/ref_composite.h', '        Px s = {{done.group.opacity * moved.v[0],', '        Px s = {{moved.v[0],', ['testComposite']),
    ('tst-group-operator', 'tst', 'tests/ref_composite.h', 'parent.layer = apply(parent.layer, s, done.outer, (int)done.group.composite);', 'parent.layer = apply(parent.layer, s, done.outer, GSHSCENE_COMPOSITE_SRC_OVER);', ['testComposite']),
    ('tst-group-coverage', 'tst', 'tests/ref_composite.h', 'parent.layer = apply(parent.layer, s, done.outer, (int)done.group.composite);', 'parent.layer = apply(parent.layer, s, 1.0, (int)done.group.composite);', ['testComposite']),
    ('tst-convert-same', 'tst', 'tests/ref_composite.h', '  if (same(a, b)) {\n    for (int i = 0; i < 3; i++) {\n      out[i] = in[i];', '  if (false) {\n    for (int i = 0; i < 3; i++) {\n      out[i] = in[i];', ['testComposite']),
    ('tst-convert-decode', 'tst', 'tests/ref_composite.h', 'double lin[3] = {sat(decode(a, in[0])), sat(decode(a, in[1])), sat(decode(a, in[2]))};', 'double lin[3] = {sat(in[0]), sat(in[1]), sat(in[2])};', ['testComposite']),
    ('tst-convert-encode', 'tst', 'tests/ref_composite.h', '    out[i] = sat(encode(b, lin[i]));', '    out[i] = sat(lin[i]);', ['testComposite']),
    ('tst-convert-matrix-skip', 'tst', 'tests/ref_composite.h', '  if (a.primaries != b.primaries) {\n    Mat ma, mb, mbi;', '  if (false) {\n    Mat ma, mb, mbi;', ['testComposite']),
    ('tst-convert-matrix-order', 'tst', 'tests/ref_composite.h', '    rgb_to_xyz(a.primaries, ma);\n    rgb_to_xyz(b.primaries, mb);', '    rgb_to_xyz(b.primaries, ma);\n    rgb_to_xyz(a.primaries, mb);', ['testComposite']),
    ('tst-srgb-threshold', 'tst', 'tests/ref_composite.h', 'return x <= 0.04045 ? x / 12.92', 'return x <= 0.04046 ? x / 12.92', ['testComposite']),
    ('tst-srgb-slope', 'tst', 'tests/ref_composite.h', 'x / 12.92 : gmath_pow', 'x / 12.9 : gmath_pow', ['testComposite']),
    ('tst-srgb-offset', 'tst', 'tests/ref_composite.h', 'gmath_pow((x + 0.055) / 1.055, 2.4)', 'gmath_pow((x + 0.056) / 1.055, 2.4)', ['testComposite']),
    ('tst-srgb-exponent', 'tst', 'tests/ref_composite.h', 'gmath_pow((x + 0.055) / 1.055, 2.4)', 'gmath_pow((x + 0.055) / 1.055, 2.3)', ['testComposite']),
    ('tst-srgb-encode-threshold', 'tst', 'tests/ref_composite.h', 'return y <= 0.0031308 ? y * 12.92', 'return y <= 0.0031309 ? y * 12.92', ['testComposite']),
    ('tst-srgb-encode-slope', 'tst', 'tests/ref_composite.h', 'y * 12.92 : 1.055', 'y * 12.9 : 1.055', ['testComposite']),
    ('tst-srgb-encode-scale', 'tst', 'tests/ref_composite.h', '1.055 * gmath_pow(y, 1.0 / 2.4) - 0.055', '1.05 * gmath_pow(y, 1.0 / 2.4) - 0.055', ['testComposite']),
    ('tst-srgb-encode-offset', 'tst', 'tests/ref_composite.h', '1.055 * gmath_pow(y, 1.0 / 2.4) - 0.055', '1.055 * gmath_pow(y, 1.0 / 2.4) - 0.05', ['testComposite']),
    ('tst-gamma-sign', 'tst', 'tests/ref_composite.h', 'return x == 0.0 ? 0.0 : sign_of(x) * gmath_pow(x < 0 ? -x : x, s.gamma);', 'return x == 0.0 ? 0.0 : gmath_pow(x < 0 ? -x : x, s.gamma);', ['testComposite']),
    ('tst-gamma-encode', 'tst', 'tests/ref_composite.h', 'sign_of(y) * gmath_pow(y < 0 ? -y : y, 1.0 / s.gamma);', 'sign_of(y) * gmath_pow(y < 0 ? -y : y, s.gamma);', ['testComposite']),
    ('tst-gamut-srgb', 'tst', 'tests/ref_composite.h', 'out[0] = {0.64, 0.33}; out[1] = {0.30, 0.60}; out[2] = {0.15, 0.06};', 'out[0] = {0.64, 0.33}; out[1] = {0.31, 0.60}; out[2] = {0.15, 0.06};', ['testComposite']),
    ('tst-gamut-adobe', 'tst', 'tests/ref_composite.h', 'out[0] = {0.64, 0.33}; out[1] = {0.21, 0.71}; out[2] = {0.15, 0.06};', 'out[0] = {0.64, 0.33}; out[1] = {0.20, 0.71}; out[2] = {0.15, 0.06};', ['testComposite']),
    ('tst-gamut-p3', 'tst', 'tests/ref_composite.h', 'out[0] = {0.680, 0.320}; out[1] = {0.265, 0.690}; out[2] = {0.150, 0.060};', 'out[0] = {0.681, 0.320}; out[1] = {0.265, 0.690}; out[2] = {0.150, 0.060};', ['testComposite']),
    ('tst-gamut-bt2020', 'tst', 'tests/ref_composite.h', 'out[0] = {0.708, 0.292}; out[1] = {0.170, 0.797}; out[2] = {0.131, 0.046};', 'out[0] = {0.708, 0.292}; out[1] = {0.170, 0.797}; out[2] = {0.131, 0.047};', ['testComposite']),
    ('tst-white', 'tst', 'tests/ref_composite.h', 'const double wx = 0.3127, wy = 0.3290;\n  const double white[3]', 'const double wx = 0.3127, wy = 0.3291;\n  const double white[3]', ['testComposite']),
    ('tst-matrix-scale', 'tst', 'tests/ref_composite.h', '      out[r][k] = s[k] * p[r][k];', '      out[r][k] = p[r][k];', ['testComposite']),
    ('tst-matrix-z', 'tst', 'tests/ref_composite.h', 'p[2][i] = (1.0 - c[i].x - c[i].y) / c[i].y;', 'p[2][i] = (1.0 - c[i].x) / c[i].y;', ['testComposite']),
    ('tst-inverse-cofactor', 'tst', 'tests/ref_composite.h', 'out[i][j] = (m[r0][c0] * m[r1][c1] - m[r0][c1] * m[r1][c0]) / d;', 'out[i][j] = (m[r0][c0] * m[r1][c1] + m[r0][c1] * m[r1][c0]) / d;', ['testComposite']),

]

# The sink contract (story 13a): its constants and pinned values, and one named plant per S-rule that a violation of
# the rule in the contract layer or the test sink must fail the conformance suite on.  The branches of src/sink are
# also mutated one by one by the generator (tools/sink-plants.json, below).
SK = "src/sink/sink.c"
RP = "src/sink/report.c"
DS = "src/sink/dump_sink.c"
HS = "include/ghoti.io/shape-scene/sink.h"
HRP = "include/ghoti.io/shape-scene/report.h"
SINKT = ["testDump_sink", "testSink", "testReport"]
PLANTS += [
    ('core-string-cancelled', 'src', CO, '      return "cancelled";', '      return "refused";', C),
    ('core-default-report-entries', 'src', CO, 'limits->max_report_entries = GSHSCENE_LIMITS_DEFAULT_MAX_REPORT_ENTRIES;', 'limits->max_report_entries = GSHSCENE_LIMITS_DEFAULT_MAX_REPORT_ENTRIES + 1;', C),
    ('core-default-jobs', 'src', CO, 'limits->max_jobs = GSHSCENE_LIMITS_DEFAULT_MAX_JOBS;', 'limits->max_jobs = GSHSCENE_LIMITS_DEFAULT_MAX_JOBS - 1;', C),
    ('hdr-result-cancelled-order', 'hdr', HC, '  GSHSCENE_ERR_REFUSED,\n  /** The render\'s cancel token was set (sink.h, S7): the target holds the finished work, a prefix of paint order. */\n  GSHSCENE_ERR_CANCELLED,', '  GSHSCENE_ERR_CANCELLED,\n  GSHSCENE_ERR_REFUSED,', C),
    ('hdr-default-report-entries', 'hdr', HC, '#define GSHSCENE_LIMITS_DEFAULT_MAX_REPORT_ENTRIES ((size_t)1 << 16)', '#define GSHSCENE_LIMITS_DEFAULT_MAX_REPORT_ENTRIES ((size_t)1 << 15)', C),
    ('hdr-default-jobs', 'hdr', HC, '#define GSHSCENE_LIMITS_DEFAULT_MAX_JOBS ((size_t)64)', '#define GSHSCENE_LIMITS_DEFAULT_MAX_JOBS ((size_t)63)', C),
    ('hdr-entry-classes', 'hdr', HRP, '  GSHSCENE_ENTRY_APPROXIMATED = 2,', '  GSHSCENE_ENTRY_APPROXIMATED = 7,', ['testReport']),
    ('hdr-reasons', 'hdr', HRP, '  GSHSCENE_REASON_SEAMLESS = 7,', '  GSHSCENE_REASON_SEAMLESS = 37,', ['testReport']),
    ('hdr-target-kinds', 'hdr', HS, '  GSHSCENE_TARGET_STREAM = 2,', '  GSHSCENE_TARGET_STREAM = 5,', SINKT),
    ('hdr-seamless', 'hdr', HS, '#define GSHSCENE_TARGET_SEAMLESS (UINT32_C(1) << 0)', '#define GSHSCENE_TARGET_SEAMLESS (UINT32_C(1) << 3)', SINKT),
    ('hdr-render-strict', 'hdr', HS, '#define GSHSCENE_RENDER_STRICT (UINT32_C(1) << 0)', '#define GSHSCENE_RENDER_STRICT (UINT32_C(1) << 2)', SINKT),
    ('hdr-cap-flags', 'hdr', HS, '#define GSHSCENE_CAP_FLAGS_ALL ((UINT32_C(1) << 14) - 1)', '#define GSHSCENE_CAP_FLAGS_ALL ((UINT32_C(1) << 13) - 1)', SINKT),
    ('hdr-cancel-units', 'hdr', HS, '  GSHSCENE_CANCEL_BEFORE_SUBMIT = 2,', '  GSHSCENE_CANCEL_BEFORE_SUBMIT = 3,', SINKT),
    ('rp-tag', 'src', RP, '#define REPORT_TAG UINT32_C(0x54525052)', '#define REPORT_TAG UINT32_C(0x54525053)', ['testDump_sink']),
    ('rp-format', 'src', RP, '#define REPORT_FORMAT 1u', '#define REPORT_FORMAT 2u', ['testDump_sink']),
    ('ds-format', 'src', DS, '#define DUMP_FORMAT 1u', '#define DUMP_FORMAT 2u', ['testDump_sink']),
    ('ds-edge-high', 'src', DS, '#define EDGE_HIGH 2147483647.0', '#define EDGE_HIGH 16.0', ['testDump_sink']),
    ('ds-edge-low', 'src', DS, '#define EDGE_LOW (-2147483648.0)', '#define EDGE_LOW (16.0)', ['testDump_sink']),
    # One per rule.
    ('rule-S1-flags', 'src', SK, '(request->flags & ~GSHSCENE_RENDER_FLAGS_ALL) ||', '', SINKT),
    ('rule-S1-reserved', 'src', SK, '      !bytes_zero(request->_reserved, sizeof request->_reserved) ||', '', SINKT),
    ('rule-S2-caps-checked', 'src', SK, '  if ((c.flags & ~GSHSCENE_CAP_FLAGS_ALL) || (c.stroke_native', '  if (0 || (c.stroke_native', ['testSink']),
    ('rule-S3-busy', 'src', SK, '    if (sink->busy[i] == surface) {\n      result = GSHSCENE_ERR_INVALID;', '    if (0) {\n      result = GSHSCENE_ERR_INVALID;', SINKT),
    ('rule-S3-max-jobs', 'src', SK, '    if (free_slot == sink->max_jobs) {\n      result = GSHSCENE_ERR_LIMIT;', '    if (0) {\n      result = GSHSCENE_ERR_LIMIT;', SINKT),
    ('rule-S3-done-before-callback', 'src', SK, '  if (job->request.on_complete) {\n    job->request.on_complete(job->request.on_complete_ctx, job, result);\n  }\n  gcu_atomic_int_store(&job->state, GSHSCENE_JOB_DONE);', '  gcu_atomic_int_store(&job->state, GSHSCENE_JOB_DONE);\n  if (job->request.on_complete) {\n    job->request.on_complete(job->request.on_complete_ctx, job, result);\n  }', SINKT),
    ('rule-S3-report-busy', 'src', SK, '  if (!gcu_atomic_int_compare_exchange(&request->report->busy, &idle, 1)) {', '  if (!gcu_atomic_int_compare_exchange(&request->report->busy, &idle, 0)) {', SINKT),
    ('rule-S4-damage', 'src', DS, '    run->area = target->damage ? rect_meet(target->extent, *target->damage) : target->extent;', '    run->area = target->extent;', SINKT),
    ('rule-S4-pixel-fields', 'src', SK, '        target->encoding != GSHSCENE_ENCODING_SOFTWARE || !rect_zero(&target->extent) || target->damage) {', '        target->encoding != GSHSCENE_ENCODING_SOFTWARE) {', SINKT),
    ('rule-S4-undescribed-srgb', 'src', SK, '    resolved.transfer = GCOL_TRANSFER_SRGB; /* the gamma', '    resolved.transfer = GCOL_TRANSFER_LINEAR; /* the gamma', SINKT),
    ('rule-S5-sorted', 'src', RP, '  if (report->count > 1) {\n    qsort(', '  if (report->count > 1000000) {\n    qsort(', SINKT),
    ('rule-S5-cap', 'src', RP, '  if (result == GSHSCENE_OK && report->summary.overflowed) {', '  if (0) {', SINKT),
    ('rule-S6-strict', 'src', SK, '  if (refused && (request->flags & GSHSCENE_RENDER_STRICT)) {', '  if (0) {', SINKT),
    ('rule-S6-skip-block', 'src', SK, '      (*refused)++;\n      gshscene_skip_begin(&skip, item, count);', '      (*refused)++;', SINKT),
    ('rule-S6-ties', 'src', SK, '  result = plan_ties(report, list, key, 1); /* plan_ties writes key[0] */', '  result = GSHSCENE_OK;', SINKT),
    ('rule-S7-first-check', 'src', SK, '  if (gshscene_job_checkpoint(job)) {\n    return end_early(job, GSHSCENE_ERR_CANCELLED', '  if (0) {\n    return end_early(job, GSHSCENE_ERR_CANCELLED', SINKT),
    ('rule-S7-unit-check', 'src', SK, '  if (job->caps.cancel_unit == GSHSCENE_CANCEL_BETWEEN_UNITS) {\n    return gshscene_job_checkpoint(job);', '  if (job->caps.cancel_unit == GSHSCENE_CANCEL_BEFORE_SUBMIT) {\n    return gshscene_job_checkpoint(job);', SINKT),
    ('rule-S7-progress-first', 'src', SK, '  if (job->request.progress) {\n    job->request.progress(job->request.progress_ctx, job, units_processed);\n  }\n  if (job->caps.cancel_unit', '  int planted = job->caps.cancel_unit == GSHSCENE_CANCEL_BETWEEN_UNITS && gshscene_job_checkpoint(job);\n  if (job->request.progress) {\n    job->request.progress(job->request.progress_ctx, job, units_processed);\n  }\n  if (job->caps.cancel_unit == GSHSCENE_CANCEL_BETWEEN_UNITS) {\n    return planted;\n  }\n  if (job->caps.cancel_unit', SINKT),
    ('rule-S7-work-max', 'src', SK, '  if (job->work > job->work_max) {\n    job->work_max = job->work;\n  }\n  job->work = 0;\n  return gshscene_cancel_is_set', '  job->work = 0;\n  return gshscene_cancel_is_set', SINKT),
    ('rule-S7-open-group-done', 'src', DS, '    if (run->groups_open == 0) {\n      run->done = run->processed;', '    if (1) {\n      run->done = run->processed;', SINKT),
    ('rule-S7-open-group-stamped', 'src', DS, '  Block * g = innermost_group(run);\n  if (!g) {\n    stamp(run, r, mark, failed);\n    return;\n  }', '  Block * g = NULL;\n  if (!g) {\n    stamp(run, r, mark, failed);\n    return;\n  }', SINKT),
    ('rule-S8-seamless', 'src', SK, '  if (copy.flags & GSHSCENE_TARGET_SEAMLESS) {', '  if (0) {', SINKT),
    ('rule-S9-workspace-size', 'src', SK, '    if (request->workspace_bytes < need) {', '    if (request->workspace_bytes + 1 < need) {', SINKT),
    ('rule-S9-caller-memory-cap', 'src', SK, '    if (!(caps.flags & GSHSCENE_CAP_CALLER_MEMORY) || !sink->ops.workspace_bytes) {', '    if (!sink->ops.workspace_bytes) {', SINKT),
    ('rule-S9-allocates', 'src', DS, '  if (request->flags & GSHSCENE_RENDER_CALLER_MEMORY) {\n    base = request->workspace;', '  if (0) {\n    base = request->workspace;', SINKT),
    # 2026-10-21 (story 16's decision): a short workspace is a limit with no allocation and no need stated.  The old
    # contract stated the need the sink measured; a query that took a sink's bound for the need; a render that sized
    # its workspace without the flag (where a sink may allocate); and the test sink allocating to size a render.
    ('rule-S9-short-states-need', 'src', SK, '    if (request->workspace_bytes < need) {\n      return end_early(job, GSHSCENE_ERR_LIMIT,', '    request->report->summary.workspace_needed = need;\n    if (request->workspace_bytes < need) {\n      return end_early(job, GSHSCENE_ERR_LIMIT,', SINKT),
    ('rule-S9-query-takes-the-bound', 'src', SK, '    if (bytes <= r.workspace_bytes) {', '    if (1) {', SINKT),
    ('rule-S9-render-sizes-freely', 'src', SK, '    result = sink->ops.workspace_bytes(sink->impl, &job->request, &need);', '    GSHSCENE_Render_Request planted = job->request;\n    planted.flags &= ~GSHSCENE_RENDER_CALLER_MEMORY;\n    result = sink->ops.workspace_bytes(sink->impl, &planted, &need);', SINKT),
    ('rule-S9-sizing-allocates', 'src', DS, '  (void)impl;\n  size_t levels;\n  size_t blocks;', '  Dump_Sink * planted = impl;\n  gshscene_free(&planted->alloc, gshscene_alloc(&planted->alloc, 8));\n  size_t levels;\n  size_t blocks;', SINKT),
    # S4: the test sink's one limit (max_extent) met over the area drawn rather than the item's box, as shape-cpu's
    # cell cap was before 2026-10-21.
    ('rule-S4-limit-in-damage', 'src', DS, '(box.x1 - box.x0 > limit || box.y1 - box.y0 > limit)', '(run->job->target.damage ? (box.x1 < (double)run->area.x1 ? box.x1 : (double)run->area.x1) - (box.x0 > (double)run->area.x0 ? box.x0 : (double)run->area.x0) > limit : (box.x1 - box.x0 > limit || box.y1 - box.y0 > limit))', ['testDump_sink']),
    ('rule-S10-evict-keeps', 'src', DS, '  GCU_MUTEX_UNLOCK(ds->lock);\n  gshscene_resource_release(released);', '  GCU_MUTEX_UNLOCK(ds->lock);', SINKT),
    ('rule-S10-destroy-keeps', 'src', DS, '  for (size_t i = 0; i < ds->cache_count; i++) {\n    gshscene_resource_release(ds->cache[i].resource);\n  }', '', SINKT),
    ('rule-S13-wait-blocks', 'src', SK, '  if (!(job->caps.flags & GSHSCENE_CAP_CAN_BLOCK)) {\n    return GSHSCENE_ERR_UNSUPPORTED;\n  }\n  if (!thread_ok(sink, &job->caps))', '  if (!thread_ok(sink, &job->caps))', SINKT),
    ('rule-S13-render-blocks', 'src', SK, '  if (!(caps.flags & GSHSCENE_CAP_CAN_BLOCK)) {\n    return GSHSCENE_ERR_UNSUPPORTED;\n  }\n  GSHSCENE_Job job;', '  GSHSCENE_Job job;', SINKT),
    ('rule-S14-affinity', 'src', SK, '  return !(caps->flags & GSHSCENE_CAP_THREAD_AFFINE) || gshscene_thread_token() == sink->creator;', '  return sink != NULL || caps != NULL;', SINKT),
    ('rule-S15-readback-format', 'src', SK, '      !want || !have || !format_equal(want, have)) {', '      !want || !have) {', SINKT),
    ('rule-S15-readback-cap', 'src', SK, '  if (!(job->caps.flags & GSHSCENE_CAP_READBACK) || !sink->ops.readback) {', '  if (!sink->ops.readback) {', SINKT),
]

# Story 14a: runs from a font.  The seam's constants (gh-), glyph.c's arithmetic and the rules of the route
# (gl-), the plan's routes and the test sink's glyph records (rule-S6-glyph-, ds-glyph-).  The branches of
# src/glyph/glyph.c are also mutated one by one by the generator (tools/sink-plants.json).
# Story 14c: the palette (N14) and the float-free calls (fixed.h).
FX = "src/list/fixed.c"
HF = "include/ghoti.io/shape-scene/fixed.h"
ED = "src/scene/edit.c"
SC = "src/scene/scene.c"
EM = "src/scene/emit.c"
ND = "src/scene/node.c"
PT = ["testPalette"]
PTD = ["testPalette", "testDump"]
PLANTS += [
    # the constants and pinned values
    ('pal-hdr-kind', 'hdr', HR, '  GSHSCENE_RESOURCE_PALETTE = 7,', '  GSHSCENE_RESOURCE_PALETTE = 18,', R),
    ('pal-hdr-max-entries', 'hdr', HR, '#define GSHSCENE_PALETTE_MAX_ENTRIES 65535u', '#define GSHSCENE_PALETTE_MAX_ENTRIES 65536u', PT),
    ('pal-hdr-bytes-per-color', 'hdr', HR, '#define GSHSCENE_BYTES_PER_COLOR ((size_t)48)', '#define GSHSCENE_BYTES_PER_COLOR ((size_t)40)', PT),
    ('pal-hdr-entry-max', 'hdr', HP, '#define GSHSCENE_PALETTE_ENTRY_MAX 0xFFFEu', '#define GSHSCENE_PALETTE_ENTRY_MAX 0xFFFFu', PT),
    ('pal-hdr-reason', 'hdr', HRP, '  GSHSCENE_REASON_PALETTE_ENTRY = 20,', '  GSHSCENE_REASON_PALETTE_ENTRY = 40,', ["testReport", "testGlyph"]),
    ('pal-hdr-fixed-foreground', 'hdr', HF, '#define GSHSCENE_FIXED_PAINT_FOREGROUND 2u', '#define GSHSCENE_FIXED_PAINT_FOREGROUND 3u', PT),
    ('pal-hdr-fixed-palette', 'hdr', HF, '#define GSHSCENE_FIXED_PAINT_PALETTE 3u', '#define GSHSCENE_FIXED_PAINT_PALETTE 4u', PT),
    ('pal-hdr-fixed-entry-fg', 'hdr', HF, '#define GSHSCENE_FIXED_ENTRY_FOREGROUND 0xFFFFu', '#define GSHSCENE_FIXED_ENTRY_FOREGROUND 0xFFFEu', PT),
    ('pal-hdr-fixed-skew', 'hdr', HF, '  GSHSCENE_FIXED_SKEW = 4,', '  GSHSCENE_FIXED_SKEW = 5,', PT),
    ('pal-hdr-fixed-max-ops', 'hdr', HF, '#define GSHSCENE_FIXED_MAX_OPS 64u', '#define GSHSCENE_FIXED_MAX_OPS 65u', PT),
    ('pal-hdr-fixed-max-bits', 'hdr', HF, '#define GSHSCENE_FIXED_MAX_BITS 30u', '#define GSHSCENE_FIXED_MAX_BITS 31u', PT),
    ('pal-reason-string', 'src', RP, '      return "palette-entry";', '      return "palette-entri";', ["testReport"]),
    ('pal-kind-string', 'src', RS, '      return "palette";', '      return "palettes";', R + PTD),
    # the palette resource: hash, equality, copy, intern, the 8-bit constructor, readers, dump
    ('pal-hash-count', 'src', RS, '  gshgeom_hash_u64(&h, (uint64_t)count);\n  for (size_t i = 0; i < count; i++) {\n    gshscene_color_hash(&h, &colors[i]);', '  for (size_t i = 0; i < count; i++) {\n    gshscene_color_hash(&h, &colors[i]);', D),
    ('pal-hash-colors', 'src', RS, '    gshscene_color_hash(&h, &colors[i]);\n', '    (void)colors[i];\n', D + PT),
    ('pal-hash-kind', 'src', RS, '  hash_begin(&h, GSHSCENE_RESOURCE_PALETTE);', '  hash_begin(&h, GSHSCENE_RESOURCE_FACE);', D),
    ('pal-match-count', 'src', RS, '      if (resource->u.palette.count != want->count) {', '      if (0) {', PT),
    ('pal-match-colors', 'src', RS, '        if (!gshscene_color_equal(&resource->u.palette.colors[i], &want->colors[i])) {', '        if (0) {', PT + ["testStore"]),
    ('pal-match-loop', 'src', RS, '      for (size_t i = 0; i < want->count; i++) {\n        if (!gshscene_color_equal', '      for (size_t i = 0; i + 1 < want->count; i++) {\n        if (!gshscene_color_equal', PT),
    ('pal-free-colors', 'src', RS, '    case GSHSCENE_RESOURCE_PALETTE:\n      gshscene_free(alloc, resource->u.palette.colors);', '    case GSHSCENE_RESOURCE_PALETTE:\n      (void)alloc;', PT),
    ('pal-build-copy', 'src', RS, '    memcpy(resource->u.palette.colors, src->colors, src->count * sizeof(GSHSCENE_Color));', '    memset(resource->u.palette.colors, 0, src->count * sizeof(GSHSCENE_Color));', PT),
    ('pal-build-count', 'src', RS, '  resource->u.palette.count = src->count;', '  resource->u.palette.count = 0;', PT),
    ('pal-build-oom', 'src', RS, '    if (!resource->u.palette.colors) {\n      gshscene_store_discard_resource(resource);\n      return GSHSCENE_ERR_OOM;', '    if (0) {\n      gshscene_store_discard_resource(resource);\n      return GSHSCENE_ERR_OOM;', PT),
    ('pal-intern-limit', 'src', RS, '  if (count > GSHSCENE_PALETTE_MAX_ENTRIES) {\n    return GSHSCENE_ERR_LIMIT;\n  }\n  for (size_t i = 0; i < count; i++) {', '  if (count > GSHSCENE_PALETTE_MAX_ENTRIES + 1u) {\n    return GSHSCENE_ERR_LIMIT;\n  }\n  for (size_t i = 0; i < count; i++) {', PT),
    ('pal-intern-check', 'src', RS, '    GSHSCENE_Result checked = gshscene_color_check_valid(&colors[i]);\n    if (checked != GSHSCENE_OK) {\n      return checked;', '    GSHSCENE_Result checked = gshscene_color_check_valid(&colors[i]);\n    if (0) {\n      return checked;', PT),
    ('pal-intern-bytes', 'src', RS, 'gshscene_sat_mul(count, GSHSCENE_BYTES_PER_COLOR)', 'gshscene_sat_mul(count, GSHSCENE_BYTES_PER_AXIS)', PT),
    ('pal8-null', 'src', RS, '  if (!store || !out || (count && !rgba)) {', '  if (!store || !out) {', PT),
    ('pal8-limit', 'src', RS, '  if (count > GSHSCENE_PALETTE_MAX_ENTRIES) {\n    return GSHSCENE_ERR_LIMIT;\n  }\n  GSHSCENE_Color * colors = NULL;', '  GSHSCENE_Color * colors = NULL;', PT),
    ('pal8-divisor', 'src', RS, '(double)(int32_t)rgba[4u * i + (size_t)k] / 255.0;', '(double)(int32_t)rgba[4u * i + (size_t)k] / 256.0;', PTD),
    ('pal8-channel', 'src', RS, '(double)(int32_t)rgba[4u * i + (size_t)k] / 255.0;', '(double)(int32_t)rgba[4u * i] / 255.0;', PTD),
    ('pal8-primaries', 'src', RS, '    colors[i].space.primaries = GCOL_PRIMARIES_SRGB;\n', '', PTD),
    ('pal8-transfer', 'src', RS, '    colors[i].space.transfer = GCOL_TRANSFER_SRGB;', '    colors[i].space.transfer = GCOL_TRANSFER_LINEAR;', PTD),
    ('pal8-free', 'src', RS, '  GSHSCENE_Result result = gshscene_store_intern_palette(store, count, colors, out);\n  gshscene_free(&store->alloc, colors);', '  GSHSCENE_Result result = gshscene_store_intern_palette(store, count, colors, out);', PT),
    ('pal-count-kind', 'src', RS, '  return resource && resource->kind == GSHSCENE_RESOURCE_PALETTE ? resource->u.palette.count : 0;', '  return resource ? resource->u.palette.count : 0;', PT),
    ('pal-color-index', 'src', RS, 'resource->kind != GSHSCENE_RESOURCE_PALETTE || index >= resource->u.palette.count) {', 'resource->kind != GSHSCENE_RESOURCE_PALETTE || index > resource->u.palette.count) {', PT),
    ('pal-color-kind', 'src', RS, 'if (!resource || !out || resource->kind != GSHSCENE_RESOURCE_PALETTE || index', 'if (!resource || !out || index', PT),
    ('pal-dump-entries', 'src', RS, '      if (fprintf(out, "palette entries=%llu\\n", HEX(count)) < 0) {', '      if (fprintf(out, "palette entries=%llu\\n", HEX(count + 1u)) < 0) {', D),
    ('pal-dump-io', 'src', RS, '            gshscene_space_dump(out, &c->space) < 0 || fputc(\'\\n\', out) == EOF) {', '            gshscene_space_dump(out, &c->space) < 0) {', D),
    # the parameter of a list, the resolution and the planning pass
    ('pal-set-closed', 'src', LS, '  if (!list || list->closed ||\n      (palette && (palette->store', '  if (!list ||\n      (palette && (palette->store', PT),
    ('pal-set-store', 'src', LS, '(palette && (palette->store != list->store || palette->kind != GSHSCENE_RESOURCE_PALETTE))) {', '(palette && (palette->kind != GSHSCENE_RESOURCE_PALETTE))) {', PT),
    ('pal-set-kind', 'src', LS, '(palette && (palette->store != list->store || palette->kind != GSHSCENE_RESOURCE_PALETTE))) {', '(palette && (palette->store != list->store))) {', PT),
    ('pal-set-retain', 'src', LS, '  gshscene_resource_retain(palette);\n  gshscene_resource_release(list->palette);\n  list->palette = palette;', '  gshscene_resource_release(list->palette);\n  list->palette = palette;', PT),
    ('pal-set-release', 'src', LS, '  gshscene_resource_retain(palette);\n  gshscene_resource_release(list->palette);\n  list->palette = palette;', '  gshscene_resource_retain(palette);\n  list->palette = palette;', PT),
    ('pal-clear', 'src', LS, '  gshscene_resource_release(list->palette);\n  list->palette = NULL;\n  chunks_free', '  chunks_free', PT),
    ('pal-get', 'src', LS, '  return list ? list->palette : NULL;', '  (void)list;\n  return NULL;', PT + ["testDump_sink"]),
    ('res-null', 'src', LS, '  if (!paint || !foreground || !out || (palette && palette->kind != GSHSCENE_RESOURCE_PALETTE)) {', '  if (!paint || !foreground || !out) {', PT),
    ('res-missing-count', 'src', LS, '      if (!palette || paint->entry >= palette->u.palette.count) {', '      if (!palette || paint->entry > palette->u.palette.count) {', PT + ["testDump_sink"]),
    ('res-missing-none', 'src', LS, '      if (!palette || paint->entry >= palette->u.palette.count) {', '      if (paint->entry >= palette->u.palette.count) {', PT),
    ('res-entry-alpha', 'src', LS, '      c.rgba[3] = c.rgba[3] * paint->rgba[3];', '      c.rgba[3] = paint->rgba[3];', PT),
    ('res-entry-index', 'src', LS, '      c = palette->u.palette.colors[paint->entry];', '      c = palette->u.palette.colors[0];', PT),
    ('res-fg-alpha', 'src', LS, '      c.rgba[3] = foreground->rgba[3] * paint->rgba[3];', '      c.rgba[3] = foreground->rgba[3];', PT),
    ('res-solid-space', 'src', LS, '      memcpy(c.rgba, paint->rgba, sizeof c.rgba);\n      c.space = paint->space;', '      memcpy(c.rgba, paint->rgba, sizeof c.rgba);\n      memset(&c.space, 0, sizeof c.space);', PT),
    ('res-default', 'src', LS, '    default:\n      return GSHSCENE_ERR_INVALID;\n  }\n  *out = c;', '    default:\n      break;\n  }\n  *out = c;', PT),
    ('res-out', 'src', LS, '  *out = c;\n  return GSHSCENE_OK;\n}', '  return GSHSCENE_OK;\n}', PT),
    ('pal-plan-fill-only', 'src', SK, '  if (painted && item->paint.kind == GSHSCENE_PAINT_PALETTE) {', '  if (item->paint.kind == GSHSCENE_PAINT_PALETTE) {', PT),
    ('pal-plan-kind', 'src', SK, '  if (painted && item->paint.kind == GSHSCENE_PAINT_PALETTE) {', '  if (painted) {', PT + ["testDump_sink"]),
    ('pal-plan-reason', 'src', SK, 'palette, &resolved) != GSHSCENE_OK) {\n      d.entry_class = GSHSCENE_ENTRY_REFUSED;\n      d.reason = GSHSCENE_REASON_PALETTE_ENTRY;', 'palette, &resolved) != GSHSCENE_OK) {\n      d.entry_class = GSHSCENE_ENTRY_REFUSED;\n      d.reason = GSHSCENE_REASON_ITEM_KIND;', PT + ["testDump_sink"]),
    ('pal-plan-refuse', 'src', SK, '    if (gshscene_paint_resolve(&item->paint, &unused, palette, &resolved) != GSHSCENE_OK) {', '    if (0 && gshscene_paint_resolve(&item->paint, &unused, palette, &resolved) != GSHSCENE_OK) {', PT + ["testDump_sink"]),
    ('pal-plan-list', 'src', SK, '&job->caps, item, maps, count - 1, &job->target.map, &job->target_space, gshscene_list_palette(list));', '&job->caps, item, maps, count - 1, &job->target.map, &job->target_space, NULL);', ["testDump_sink"]),
    ('pal-test-sink-list', 'src', DS, '&run->job->target.map,\n      &run->job->target_space, gshscene_list_palette(run->job->request.list));', '&run->job->target.map,\n      &run->job->target_space, NULL);', ["testDump_sink"]),
    # truncate
    ('tr-closed', 'src', LS, '  if (!list || list->closed || count > list->count) {', '  if (!list || count > list->count) {', PT),
    ('tr-count', 'src', LS, '  if (!list || list->closed || count > list->count) {', '  if (!list || list->closed || count > list->count + 1u) {', PT),
    ('tr-release', 'src', LS, '  for (size_t i = count; i < list->count; i++) {\n    item_release(item_at(list, i));\n  }', '', PT),
    ('tr-set-count', 'src', LS, '  list->count = count;\n  /* What recording', '  /* What recording', PT),
    ('tr-depth', 'src', LS, '  list->expansion = 0;\n  list->depth = 0;\n  for (size_t i = 0; i < count; i++) {', '  list->expansion = 0;\n  for (size_t i = 0; i < count; i++) {', PT),
    ('tr-expansion', 'src', LS, '  list->expansion = 0;\n  list->depth = 0;\n  for (size_t i = 0; i < count; i++) {', '  list->depth = 0;\n  for (size_t i = 0; i < count; i++) {', PT),
    ('tr-nonflat', 'src', LS, '    note_item(list, item);\n    int instance', '    int instance', PT),
    ('tr-picture-depth', 'src', LS, '    list->picture_depth = picture > list->picture_depth ? picture : list->picture_depth;', '    (void)picture;', PT + ["testResource"]),
    ('tr-instance-weight', 'src', LS, 'instance ? saturating_add(1u, gshscene_list_expansion(gshscene_resource_picture(item->resource))) : 1u);', '(instance && 0) ? 0u : 1u);', PT),
    ('tr-push', 'src', LS, '      list->stack[list->depth++] = item->kind == GSHSCENE_ITEM_CLIP_PUSH ? PUSH_CLIP : PUSH_GROUP;', '      list->stack[list->depth++] = PUSH_GROUP;', PT),
    ('tr-pop', 'src', LS, '    } else if (item->kind == GSHSCENE_ITEM_CLIP_POP || item->kind == GSHSCENE_ITEM_GROUP_POP) {\n      list->depth--;', '    } else if (item->kind == GSHSCENE_ITEM_CLIP_POP) {\n      list->depth--;', PT),
    # fixed.c
    ('fx-op-values-affine', 'src', FX, '{6u, 2u, 4u, 3u, 4u};', '{5u, 2u, 4u, 3u, 4u};', PT),
    ('fx-op-values-translate', 'src', FX, '{6u, 2u, 4u, 3u, 4u};', '{6u, 3u, 4u, 3u, 4u};', PT),
    ('fx-op-values-scale', 'src', FX, '{6u, 2u, 4u, 3u, 4u};', '{6u, 2u, 5u, 3u, 4u};', PT),
    ('fx-op-values-rotate', 'src', FX, '{6u, 2u, 4u, 3u, 4u};', '{6u, 2u, 4u, 4u, 4u};', PT),
    ('fx-op-values-skew', 'src', FX, '{6u, 2u, 4u, 3u, 4u};', '{6u, 2u, 4u, 3u, 5u};', PT),
    ('fx-value-bits', 'src', FX, '  return (double)v / (double)((int32_t)1 << bits);', '  return (double)v / (double)((int32_t)1 << (bits / 2u));', PT),
    ('fx-geometry-limit', 'src', FX, '    case GSHGEOM_ERR_LIMIT:\n      return GSHSCENE_ERR_LIMIT;', '    case GSHGEOM_ERR_LIMIT:\n      return GSHSCENE_ERR_INVALID;', PT),
    ('fx-about-sign', 'src', FX, '    r = gshgeom_map_translate(2, -cx, -cy, 0.0, &chain[2]);', '    r = gshgeom_map_translate(2, cx, -cy, 0.0, &chain[2]);', PT),
    ('fx-about-order', 'src', FX, 'cx, cy, 0.0, &chain[0]);\n  if (r == GSHGEOM_OK) {\n    r = gshgeom_map_translate(2, -cx, -cy, 0.0, &chain[2]);', 'cx, cy, 0.0, &chain[2]);\n  if (r == GSHGEOM_OK) {\n    r = gshgeom_map_translate(2, -cx, -cy, 0.0, &chain[0]);', PT),
    ('fx-tangent', 'src', FX, '  *out = r.m[1][0] / r.m[0][0];', '  *out = r.m[1][0];', PT),
    ('fx-op-kind', 'src', FX, '  if (op->kind >= (uint32_t)GSHSCENE_FIXED_OP_COUNT || op->bits > GSHSCENE_FIXED_MAX_BITS) {', '  if (op->kind > (uint32_t)GSHSCENE_FIXED_OP_COUNT || op->bits > GSHSCENE_FIXED_MAX_BITS) {', PT),
    ('fx-op-bits', 'src', FX, '  if (op->kind >= (uint32_t)GSHSCENE_FIXED_OP_COUNT || op->bits > GSHSCENE_FIXED_MAX_BITS) {', '  if (op->kind >= (uint32_t)GSHSCENE_FIXED_OP_COUNT) {', PT),
    ('fx-unused-values', 'src', FX, '    if (op->v[i] != 0) {\n      return GSHSCENE_ERR_INVALID; /* one spelling', '    if (0) {\n      return GSHSCENE_ERR_INVALID; /* one spelling', PT),
    ('fx-affine-order', 'src', FX, '      const double m[4] = {v[0], v[2], v[1], v[3]};', '      const double m[4] = {v[0], v[1], v[2], v[3]};', PT),
    ('fx-affine-translation', 'src', FX, '      const double t[2] = {v[4], v[5]};', '      const double t[2] = {v[5], v[4]};', PT),
    ('fx-translate', 'src', FX, '      return from_geometry(gshgeom_map_translate(2, v[0], v[1], 0.0, out));', '      return from_geometry(gshgeom_map_translate(2, v[1], v[0], 0.0, out));', PT),
    ('fx-scale-axes', 'src', FX, '      const double m[4] = {v[0], 0.0, 0.0, v[1]};', '      const double m[4] = {v[1], 0.0, 0.0, v[0]};', PT),
    ('fx-scale-center', 'src', FX, '      return about(&core, v[2], v[3], out);\n    }\n    case GSHSCENE_FIXED_ROTATE:', '      return about(&core, 0.0, 0.0, out);\n    }\n    case GSHSCENE_FIXED_ROTATE:', PT),
    ('fx-rotate-unit', 'src', FX, '      g = gshgeom_map_rotate(GSHGEOM_ANGLE_TURNS, v[0], &core);', '      g = gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, v[0], &core);', PT),
    ('fx-rotate-center', 'src', FX, '      return about(&core, v[1], v[2], out);', '      return about(&core, v[2], v[1], out);', PT),
    ('fx-skew-x-sign', 'src', FX, '      const double m[4] = {1.0, -tx, ty, 1.0};', '      const double m[4] = {1.0, tx, ty, 1.0};', PT),
    ('fx-skew-y-sign', 'src', FX, '      const double m[4] = {1.0, -tx, ty, 1.0};', '      const double m[4] = {1.0, -tx, -ty, 1.0};', PT),
    ('fx-skew-axes', 'src', FX, '      const double m[4] = {1.0, -tx, ty, 1.0};', '      const double m[4] = {1.0, ty, -tx, 1.0};', PT),
    ('fx-skew-y-angle', 'src', FX, '        r = tangent(v[1], &ty);', '        r = tangent(v[0], &ty);', PT),
    ('fx-skew-y-failure', 'src', FX, '      if (r == GSHSCENE_OK) {\n        r = tangent(v[1], &ty);', '      if (0) {\n        r = tangent(v[1], &ty);', PT),
    ('fx-skew-center', 'src', FX, '      return about(&core, v[2], v[3], out);\n    }\n    default:', '      return about(&core, v[3], v[2], out);\n    }\n    default:', PT),
    ('fx-map-none', 'src', FX, '  if (count == 0) {\n    return from_geometry(gshgeom_map_identity(2, out));', '  if (count == 0 && 0) {\n    return from_geometry(gshgeom_map_identity(2, out));', PT),
    ('fx-map-null', 'src', FX, '  if (count && !ops) {\n    return GSHSCENE_ERR_INVALID;\n  }\n  return count >', '  if (count && !ops && 0) {\n    return GSHSCENE_ERR_INVALID;\n  }\n  return count >', PT),
    ('fx-map-max', 'src', FX, '  return count > GSHSCENE_FIXED_MAX_OPS ? GSHSCENE_ERR_LIMIT : GSHSCENE_OK;', '  return count >= GSHSCENE_FIXED_MAX_OPS ? GSHSCENE_ERR_LIMIT : GSHSCENE_OK;', PT),
    ('fx-map-glyphs', 'src', FX, '      run->glyph_count == 0 || gshscene_face_info(run->face, &info) != GSHSCENE_OK) {', '      gshscene_face_info(run->face, &info) != GSHSCENE_OK) {', PT),
    ('fx-map-scale', 'src', FX, '  *scale = run->size / (double)(int32_t)info.upem;', '  *scale = run->size / 2048.0;', PT),
    ('fx-map-bx', 'src', FX, '  const double bx = s * t.t[0];', '  const double bx = t.t[0];', PT),
    ('fx-map-by', 'src', FX, '  const double by = s * t.t[1];', '  const double by = s * t.t[0];', PT),
    ('fx-map-identity-test', 'src', FX, '  if (t.m[0][0] == 1.0 && t.m[0][1] == 0.0 && t.m[1][0] == 0.0 && t.m[1][1] == 1.0) {', '  if (t.m[0][0] == 1.0 && t.m[1][1] == 1.0) {', PT),
    ('fx-map-identity-never', 'src', FX, '  if (t.m[0][0] == 1.0 && t.m[0][1] == 0.0 && t.m[1][0] == 0.0 && t.m[1][1] == 1.0) {', '  if (0) {', PT),
    ('fx-map-translate-flip', 'src', FX, '    return from_geometry(gshgeom_map_translate(2, bx, -by, 0.0, out));', '    return from_geometry(gshgeom_map_translate(2, bx, by, 0.0, out));', PT),
    ('fx-map-glyph-origin', 'src', FX, '  const double gx = run.glyphs[0].x;', '  const double gx = 0.0;', PT),
    ('fx-map-first-flip', 'src', FX, '    g = gshgeom_map_flip_y(2, &chain[1]);', '    g = gshgeom_map_identity(2, &chain[1]);', PT),
    ('fx-map-second-flip', 'src', FX, '    g = gshgeom_map_flip_y(2, &chain[3]);', '    g = gshgeom_map_identity(2, &chain[3]);', PT),
    ('fx-map-back', 'src', FX, '    g = gshgeom_map_translate(2, -gx, -gy, 0.0, &chain[4]);', '    g = gshgeom_map_translate(2, -gx, gy, 0.0, &chain[4]);', PT),
    ('fx-paint-bits', 'src', FX, '  if (!fixed || fixed->bits > GSHSCENE_FIXED_MAX_BITS || fixed->alpha < 0 ||', '  if (!fixed || fixed->alpha < 0 ||', PT),
    ('fx-paint-above-one', 'src', FX, '      fixed->alpha > ((int32_t)1 << fixed->bits)) {', '      fixed->alpha > ((int32_t)2 << fixed->bits)) {', PT),
    ('fx-paint-ffff', 'src', FX, '    if (fixed->entry == GSHSCENE_FIXED_ENTRY_FOREGROUND) {\n      out->kind = GSHSCENE_PAINT_FOREGROUND;', '    if (0) {\n      out->kind = GSHSCENE_PAINT_FOREGROUND;', PT),
    ('fx-paint-entry', 'src', FX, '    out->entry = fixed->entry;\n', '', PT),
    ('fx-paint-fg-entry', 'src', FX, '  if (fixed->kind == GSHSCENE_FIXED_PAINT_FOREGROUND && fixed->entry == 0) {', '  if (fixed->kind == GSHSCENE_FIXED_PAINT_FOREGROUND) {', PT),
    ('fx-paint-fg-kind', 'src', FX, '  if (fixed->kind == GSHSCENE_FIXED_PAINT_FOREGROUND && fixed->entry == 0) {', '  if (fixed->entry == 0) {', PT),
    ('fx-paint-alpha', 'src', FX, '  out->rgba[3] = fixed_value(fixed->alpha, fixed->bits);', '  out->rgba[3] = 1.0;', PT),
    ('fx-fill-paint-failure', 'src', FX, '  if (r == GSHSCENE_OK) {\n    r = design_map(resource, ops, op_count, &map);\n  }', '  r = design_map(resource, ops, op_count, &map);', PT),
    ('fx-clip-map', 'src', FX, '  return r == GSHSCENE_OK ? gshscene_list_clip_push(list, resource, &map, rule, layer, 0.0) : r;', '  return r == GSHSCENE_OK ? gshscene_list_clip_push(list, resource, NULL, rule, layer, 0.0) : r;', PT),
    ('fx-group-opacity', 'src', FX, '  group.opacity = 1.0;', '  group.opacity = 0.5;', PT),
    ('fx-group-composite', 'src', FX, '  group.composite = composite;', '  (void)composite;\n  group.composite = 0;', PT),
    # the retained scene
    ('vp-make-retain', 'src', VS, '  version->palette = gshscene_resource_retain(palette);', '  version->palette = palette;', PT),
    ('vp-release', 'src', VS, '  gshscene_resource_release(version->palette);\n', '', PT),
    ('vp-get', 'src', VS, '  return version ? version->palette : NULL;', '  (void)version;\n  return NULL;', PT),
    ('vp-hash', 'src', VS, '  gshgeom_hash_u64(&h, version->palette ? gshscene_resource_hash(version->palette) : 0);\n', '', ["testScene_dump"] + PT),
    ('vp-dump-palette', 'src', VS, '          HEX(version->palette ? gshscene_resource_hash(version->palette) : 0), HEX(gshscene_version_hash(version))) < 0) {', '          HEX((uint64_t)0), HEX(gshscene_version_hash(version))) < 0) {', ["testScene_dump"]),
    ('vp-dump-entry', 'src', VS, '" entry=%u gradient=%016llx layer=%lld depth=%016llx", (unsigned)p->paint.entry,', '" entry=%u gradient=%016llx layer=%lld depth=%016llx", 0u,', ["testScene_dump"]),
    ('vp-node-hash-entry', 'src', ND, '      gshscene_paint_hash(h, &p->paint);\n', '      gshscene_paint_hash(h, &p->paint);\n      gshgeom_hash_u32(h, 0);\n', ["testScene_dump"]),
    ('vp-node-equal-entry', 'src', ND, ' ||\n          a->paint.entry != b->paint.entry) {', ') {', PT + ["testDiff", "testEdit"]),
    ('ep-set-spent', 'src', ED, '  if (!edit || edit->spent ||\n      (palette &&', '  if (!edit ||\n      (palette &&', PT),
    ('ep-set-kind', 'src', ED, '      (palette && (gshscene_resource_kind(palette) != GSHSCENE_RESOURCE_PALETTE ||', '      (palette && (0 ||', PT),
    ('ep-set-store', 'src', ED, '                      palette->store != edit->core->store))) {', '                      0))) {', PT),
    ('ep-set-flag', 'src', ED, '  edit->pal = palette;\n  edit->pal_set = 1;', '  edit->pal = palette;', PT),
    ('ep-set-retain', 'src', ED, '  gshscene_resource_retain(palette);\n  gshscene_resource_release(edit->pal);', '  gshscene_resource_release(edit->pal);', PT),
    ('ep-view', 'src', ED, '  return edit->pal_set ? edit->pal : edit->base->palette;', '  return edit->pal;', PT),
    ('ep-spend', 'src', ED, '  gshscene_resource_release(edit->pal);\n  edit->pal = NULL;\n  edit->pal_set = 0;', '  edit->pal_set = 0;', PT),
    ('ep-destroy', 'src', ED, '  gshscene_resource_release(edit->pal);\n  gshscene_free(&alloc, edit);', '  gshscene_free(&alloc, edit);', PT),
    ('sp-plan', 'src', SC, '  plan->pal = edit->pal_set && edit->pal != base->palette;', '  plan->pal = edit->pal_set;', PT),
    ('sp-plan-set', 'src', SC, '  plan->pal = edit->pal_set && edit->pal != base->palette;', '  plan->pal = edit->pal != base->palette;', PT),
    ('sp-clash-stale', 'src', SC, '      int clash = plans[i].edit->base->palette != current->palette;', '      int clash = 0;', PT),
    ('sp-clash-pair', 'src', SC, '        clash = plans[j].pal;', '        clash = 0;', PT),
    ('sp-clash-report', 'src', SC, '    if (plans[i].pal) {\n      int clash', '    if (0) {\n      int clash', PT),
    ('sp-touched', 'src', SC, ' + (plans[i].pal ? 1u : 0u);', ';', PT),
    ('sp-result', 'src', SC, '        palette = plans[i].edit->pal;', '        palette = NULL;', PT),
    ('sp-result-default', 'src', SC, '    const GSHSCENE_Resource * palette = current->palette;', '    const GSHSCENE_Resource * palette = NULL;', PT),
    ('em-palette', 'src', EM, '    result = gshscene_list_set_palette(sink.list, version->palette);', '    result = gshscene_list_set_palette(sink.list, NULL);', PT),
    ('df-palette-changed', 'src', DF, '  diff->palette_changed = a->palette != b->palette;', '  diff->palette_changed = 0;', PT + ["testDiff"]),
    ('df-palette-damage', 'src', DF, ' ||\n      (d->palette && paints_from_palette(&shape->p.paint))) {', ') {', PT),
    ('df-palette-kind', 'src', DF, '  return paint->kind == GSHSCENE_PAINT_PALETTE ||', '  return paint->kind == GSHSCENE_PAINT_FOREGROUND ||', PT),
    ('df-palette-empty', 'src', DF, '  if (n == 0 && !foreground && !palette) {', '  if (n == 0 && !foreground) {', PT),
    ('df-palette-pass', 'src', DF, '  d.palette = palette;\n', '', PT),
    ('df-palette-get', 'src', DF, '  return diff ? diff->palette_changed : 0;', '  (void)diff;\n  return 0;', PT),
    # the reference and the evaluator (N14)
    ('orc-palette-missing', 'orc', 'tools/composite-oracle.py', '                if palette is None or it["entry"] >= len(palette):', '                if palette is None:', ['selftest', 'vectors']),
    ('orc-palette-alpha', 'orc', 'tools/composite-oracle.py', '                alpha = F(entry[0][3]) * F(it["rgba"][3])', '                alpha = F(entry[0][3])', ['selftest', 'vectors']),
    ('orc-palette-space', 'orc', 'tools/composite-oracle.py', '                space = target if is_target(entry[1]) else entry[1]', '                space = target', ['selftest', 'vectors']),
    ('tst-palette-alpha', 'tst', 'tests/ref_composite.h', '          alpha = e.rgba[3] * item->paint.rgba[3];', '          alpha = e.rgba[3];', CP),
    ('tst-palette-space', 'tst', 'tests/ref_composite.h', '          space = from(e.space);', '          space = Space{0, 0, 0.0};', CP),
]

GL = "src/glyph/glyph.c"
HG = "include/ghoti.io/shape-scene/glyph_provider.h"
HGL = "include/ghoti.io/shape-scene/glyph.h"
GLT = ["testGlyph", "testDump_sink"]
PLANTS += [
    ('gh-mode-none', 'hdr', HG, '  GSHSCENE_HINT_NONE = 1,', '  GSHSCENE_HINT_NONE = 3,', ['testGlyph']),
    ('gh-mode-strict', 'hdr', HG, '  GSHSCENE_HINT_FONT_STRICT = 2,', '  GSHSCENE_HINT_FONT_STRICT = 4,', ['testGlyph']),
    ('gh-status-failed', 'hdr', HG, '  GSHSCENE_HINT_STATUS_PROGRAM_FAILED = 2,', '  GSHSCENE_HINT_STATUS_PROGRAM_FAILED = 5,', GLT),
    ('gh-status-errors', 'hdr', HG, '  GSHSCENE_HINT_STATUS_WITH_ERRORS = 3,', '  GSHSCENE_HINT_STATUS_WITH_ERRORS = 6,', GLT),
    ('gh-max-ppem', 'hdr', HG, '#define GSHSCENE_GLYPH_MAX_PPEM 4096u', '#define GSHSCENE_GLYPH_MAX_PPEM 4095u', ['testGlyph']),
    ('gh-upem-min', 'hdr', HG, '#define GSHSCENE_FACE_UPEM_MIN 16u', '#define GSHSCENE_FACE_UPEM_MIN 17u', ['testGlyph']),
    ('gh-upem-max', 'hdr', HG, '#define GSHSCENE_FACE_UPEM_MAX 16384u', '#define GSHSCENE_FACE_UPEM_MAX 16383u', ['testGlyph']),
    ('gh-max-axes', 'hdr', HG, '#define GSHSCENE_FACE_MAX_AXES 64u', '#define GSHSCENE_FACE_MAX_AXES 63u', ['testGlyph']),
    ('gh-f2dot14-one', 'hdr', HG, '#define GSHSCENE_F2DOT14_ONE 16384', '#define GSHSCENE_F2DOT14_ONE 16383', ['testGlyph']),
    ('gh-provider-version', 'hdr', HG, '#define GSHSCENE_GLYPH_PROVIDER_VERSION 1u', '#define GSHSCENE_GLYPH_PROVIDER_VERSION 2u', ['testGlyph']),
    ('gh-face-hinting', 'hdr', HG, '#define GSHSCENE_FACE_HINTING (UINT32_C(1) << 0)', '#define GSHSCENE_FACE_HINTING (UINT32_C(1) << 2)', GLT),
    ('gh-face-flags-all', 'hdr', HG, '#define GSHSCENE_FACE_FLAGS_ALL (GSHSCENE_FACE_HINTING | GSHSCENE_FACE_COLOR)', '#define GSHSCENE_FACE_FLAGS_ALL (GSHSCENE_FACE_HINTING | GSHSCENE_FACE_COLOR | 4u)', ['testGlyph']),
    ('gh-route-fast', 'hdr', HGL, '  GSHSCENE_GLYPH_FAST = 1,', '  GSHSCENE_GLYPH_FAST = 2,', ['testGlyph']),
    ('gh-reason-route', 'hdr', HRP, '  GSHSCENE_REASON_HINT_ROUTE = 12,', '  GSHSCENE_REASON_HINT_ROUTE = 31,', ['testGlyph', 'testReport', 'testDump_sink']),
    ('gh-reason-unbound', 'hdr', HRP, '  GSHSCENE_REASON_FACE_UNBOUND = 19,', '  GSHSCENE_REASON_FACE_UNBOUND = 39,', ['testGlyph', 'testReport']),
    ('gh-kind-face', 'hdr', HR, '  GSHSCENE_RESOURCE_FACE = 6,', '  GSHSCENE_RESOURCE_FACE = 19,', ['testResource', 'testDump']),
    ('gh-bytes-per-axis', 'hdr', HR, '#define GSHSCENE_BYTES_PER_AXIS ((size_t)8)', '#define GSHSCENE_BYTES_PER_AXIS ((size_t)9)', ['testBytes']),
    ('gl-split-constant', 'src', GL, '  double c = 134217729.0 * a; /* 2^27 + 1 */', '  double c = 1.0 * a; /* 2^27 + 1 */', ['testGlyph']),
    ('gl-dekker-low-term', 'src', GL, '  double err = ((ah * bh - p) + ah * bl + al * bh) + al * bl;', '  double err = ((ah * bh - p) + ah * bl + al * bh);', ['testGlyph']),
    ('gl-exactness-unchecked', 'src', GL, ' || !product_exact(size, grid.scale, ppem)) {', ') {', ['testGlyph']),
    ('gl-scale-shift', 'src', GL, '  uint64_t twice = ((uint64_t)ppem << 17) + upem;', '  uint64_t twice = ((uint64_t)ppem << 16) + upem;', GLT),
    ('gl-scale-truncates', 'src', GL, '  uint64_t twice = ((uint64_t)ppem << 17) + upem;', '  uint64_t twice = ((uint64_t)ppem << 17);', ['testGlyph']),
    ('gl-scale-denominator', 'src', GL, '  *out = (int32_t)(twice / (2u * (uint64_t)upem));', '  *out = (int32_t)(twice / (uint64_t)upem);', GLT),
    ('gl-max-ppem-default', 'src', GL, 'info->max_ppem ? info->max_ppem : GSHSCENE_GLYPH_MAX_PPEM', 'info->max_ppem', GLT),
    ('gl-route-flip-ignored', 'src', GL, '  if (!aligned || grid.flip_y) {', '  if (!aligned) {', ['testGlyph']),
    ('gl-route-integer-unchecked', 'src', GL, ' || (double)(int32_t)ppem != ppem ||', ' ||', ['testGlyph']),
    ('gl-route-no-program-entry', 'src', GL, '  if (!none && !(info->flags & GSHSCENE_FACE_HINTING)) {', '  if (0) {', ['testGlyph']),
    ('gl-route-stream-hinted', 'src', GL, '  if (!gshscene_target_kind_has_pixels((GSHSCENE_Target_Kind)target_kind)) {', '  if (!gshscene_target_kind_has_pixels((GSHSCENE_Target_Kind)target_kind) && 0) {', GLT),
    ('gl-route-no-fast-op', 'src', GL, '  if (!face->u.face.ops.glyph_device_path) {\n    if (!none) {', '  if (0) {\n    if (!none) {', ['testGlyph']),
    ('gl-strict-not-refused', 'src', GL, '  if (r->hint_mode == GSHSCENE_HINT_FONT_STRICT) {\n    r->entry_class = GSHSCENE_ENTRY_REFUSED;', '  if (0) {\n    r->entry_class = GSHSCENE_ENTRY_REFUSED;', ['testGlyph']),
    ('gl-map-design-scale', 'src', GL, 'run->u.glyphs.run.size / (double)(int32_t)face->u.face.info.upem', 'run->u.glyphs.run.size / (double)(int32_t)(face->u.face.info.upem + 1u)', GLT),
    ('gl-map-order', 'src', GL, '    r = gshgeom_map_compose(map, &translate, &placed);\n  }\n  if (r == GSHGEOM_OK) {\n    r = gshgeom_map_compose(&placed, &design, out);', '    r = gshgeom_map_compose(map, &design, &placed);\n  }\n  if (r == GSHGEOM_OK) {\n    r = gshgeom_map_compose(&placed, &translate, out);', GLT),
    ('gl-origin-negative-fraction', 'src', GL, '  int64_t fx = ((int64_t)d.x % 64 + 64) % 64;', '  int64_t fx = (int64_t)d.x % 64;', ['testGlyph']),
    ('gl-origin-pixel-truncates', 'src', GL, '  out->px = (int32_t)(((int64_t)d.x - fx) / 64);', '  out->px = (int32_t)((int64_t)d.x / 64);', ['testGlyph']),
    ('gl-origin-y-from-x', 'src', GL, '  int64_t fy = ((int64_t)d.y % 64 + 64) % 64;', '  int64_t fy = ((int64_t)d.x % 64 + 64) % 64;', GLT),
    ('gl-snap-scale', 'src', GL, '  double s = (double)route->scale / 65536.0;', '  double s = (double)route->scale / 65535.0;', ['testGlyph']),
    ('gl-snap-no-flip', 'src', GL, '  const double m[4] = {s, 0.0, 0.0, -s};', '  const double m[4] = {s, 0.0, 0.0, s};', ['testGlyph']),
    ('gl-snap-origin', 'src', GL, '  const double t[2] = {(double)origin->x / 64.0, (double)origin->y / 64.0};', '  const double t[2] = {(double)origin->x / 64.0, (double)origin->x / 64.0};', ['testGlyph']),
    ('gl-moved-whole-pixel', 'src', GL, '    g = moved(relative, (int64_t)origin->px * 64, (int64_t)origin->py * 64, absolute);', '    g = moved(relative, (int64_t)origin->px * 64, (int64_t)origin->px * 64, absolute);', GLT),
    ('gl-moved-range', 'src', GL, '      if (x < INT32_MIN || x > INT32_MAX || y < INT32_MIN || y > INT32_MAX) {', '      if (0) {', ['testGlyph']),
    ('gl-request-fraction', 'src', GL, '  request.frac_y = origin->fy;', '  request.frac_y = origin->fx;', GLT),
    ('gl-request-scale', 'src', GL, '  request.scale = route->scale;', '  request.scale = route->scale + 1;', GLT),
    ('gl-request-mode', 'src', GL, '  request.hint_mode = run->u.glyphs.run.hint_mode;', '  request.hint_mode = GSHSCENE_HINT_FONT;', GLT),
    ('gl-note-no-program-of-a-plain-face', 'src', GL, '      if (route->face_flags & GSHSCENE_FACE_HINTING) {', '      if (1) {', ['testGlyph']),
    ('gl-note-none-reports', 'src', GL, '  if (route->hint_mode == GSHSCENE_HINT_NONE) {\n    return;\n  }', '', GLT),
    ('gl-note-limit-is-unavailable', 'src', GL, '  if (result == GSHSCENE_ERR_LIMIT) {\n    note_set(note, GSHSCENE_ENTRY_REFUSED, GSHSCENE_REASON_RESOURCE_LIMIT);', '  if (0) {\n    note_set(note, GSHSCENE_ENTRY_REFUSED, GSHSCENE_REASON_RESOURCE_LIMIT);', ['testGlyph']),
    ('gl-note-oom-is-a-refusal', 'src', GL, '  if (result == GSHSCENE_ERR_OOM) {\n    return result;\n  }\n  if (result == GSHSCENE_ERR_LIMIT)', '  if (result == GSHSCENE_ERR_LIMIT)', ['testGlyph']),
    ('rule-S6-glyph-route', 'src', SK, '    if (plan_route(job, item, maps, count, &route) && route.entry_class) {', '    if (0) {', GLT + ['testSink']),
    ('rule-S6-glyph-instance-maps', 'src', SK, '    if (item->kind == GSHSCENE_ITEM_INSTANCE) {\n      maps[count - 1] = item->map;', '    if (item->kind == GSHSCENE_ITEM_INSTANCE) {', GLT),
    ('rule-S6-glyph-target-map', 'src', SK, '  if (gshgeom_map_compose_chain(maps, count, &job->target.map, &composed) != GSHGEOM_OK) {', '  if (gshgeom_map_compose_chain(maps, count, NULL, &composed) != GSHGEOM_OK) {', GLT),
    ('rule-S6-glyph-refusal-skips', 'src', SK, '      if (route.entry_class == GSHSCENE_ENTRY_REFUSED) {\n        (*refused)++;', '      if (0) {\n        (*refused)++;', GLT),
    ('rule-S6-glyph-entry-second', 'src', SK, '  return gshscene_report_add(job->request.report, entry_class, reason, chain, chain_count, glyph, 0.0);', '  return gshscene_report_add(job->request.report, entry_class, reason, chain, chain_count, glyph * 0u, 0.0);', GLT),
    ('ds-glyph-scratch', 'src', DS, '#define GLYPH_BYTES ((size_t)65536)', '#define GLYPH_BYTES ((size_t)4096)', ['testDump_sink']),
    ('ds-glyph-route-refusal-drawn', 'src', DS, '      if (routed && route.entry_class == GSHSCENE_ENTRY_REFUSED) {', '      if (0) {', ['testDump_sink']),
    ('ds-glyph-records-skipped', 'src', DS, '      if (routed) {\n        GSHSCENE_Result r = glyph_records(', '      if (0) {\n        GSHSCENE_Result r = glyph_records(', ['testDump_sink']),
    ('ds-glyph-box-size', 'src', DS, '  double s = run->size / (double)(int32_t)info.upem / 64.0;', '  double s = run->size / (double)(int32_t)info.upem / 32.0;', ['testDump_sink']),
    ('ds-glyph-general-map-hashed', 'src', DS, '        gshgeom_hash_u64(&h, gshgeom_map_hash(&glyph_map));\n', '', ['testDump_sink']),
    ('ds-glyph-strict-goes-on', 'src', DS, '      if (note.entry_class == GSHSCENE_ENTRY_REFUSED && (run->job->request.flags & GSHSCENE_RENDER_STRICT)) {', '      if (0) {', ['testDump_sink']),
    ('ds-glyph-arena-reset', 'src', DS, '    arena_reset(&run->glyphs);\n', '', ['testDump_sink']),
    ('df-font-run-unbounded', 'src', "src/scene/diff.c", '  if (run.source == GSHSCENE_GLYPHS_FROM_FONT) {\n    return add_font_run(d, resource, run.glyph_count, &composed);\n  }', '  if (run.source == GSHSCENE_GLYPHS_FROM_FONT) {\n    d->unbounded = 1;\n    return GSHSCENE_OK;\n  }', ['testGlyph', 'testDiff']),
    ('df-font-fast-points-scaled', 'src', "src/scene/diff.c", '            add_point(d, (double)e.p[j].x / 64.0, (double)e.p[j].y / 64.0);', '            add_point(d, (double)e.p[j].x / 32.0, (double)e.p[j].y / 64.0);', ['testGlyph']),
    ('df-font-general-box-units', 'src', "src/scene/diff.c", '    add_box(d, &map, (double)box.x_min / 64.0, (double)box.y_min / 64.0, (double)box.x_max / 64.0,', '    add_box(d, &map, (double)box.x_min, (double)box.y_min / 64.0, (double)box.x_max / 64.0,', ['testGlyph']),
]

# Color-paint story P1: gradients (G1 to G13), the one conversion of stops (G3), the notices (G10, G11), S17.
GR = "src/resource/gradient.c"
CV = "src/paint/convert.c"
GT = ["testGradient"]
GTD = ["testGradient", "testDump_sink"]
GO = "tools/gradient-oracle.py"
GG = "tools/gen-gradient-vectors.py"
RC = "tests/ref_composite.h"
PLANTS += [
    # the pinned values and constants
    ('gh-kind-radial', 'hdr', HP, '  GSHSCENE_GRADIENT_RADIAL = 2,', '  GSHSCENE_GRADIENT_RADIAL = 5,', GT),
    ('gh-kind-sweep', 'hdr', HP, '  GSHSCENE_GRADIENT_SWEEP = 3,', '  GSHSCENE_GRADIENT_SWEEP = 4,', GT),
    ('gh-extend-reflect', 'hdr', HP, '  GSHSCENE_EXTEND_REFLECT = 2,', '  GSHSCENE_EXTEND_REFLECT = 3,', GT),
    ('gh-bytes-per-stop', 'hdr', HR, '#define GSHSCENE_BYTES_PER_STOP ((size_t)96)', '#define GSHSCENE_BYTES_PER_STOP ((size_t)64)', GT),
    ('gh-notice-class', 'hdr', HRP, '  GSHSCENE_ENTRY_NOTICE = 5,', '  GSHSCENE_ENTRY_NOTICE = 6,', ["testReport"]),
    ('gh-reason-gradient', 'hdr', HRP, '  GSHSCENE_REASON_GRADIENT = 21,', '  GSHSCENE_REASON_GRADIENT = 40,', ["testReport"]),
    ('gh-reason-space', 'hdr', HRP, '  GSHSCENE_REASON_INTERPOLATION_SPACE = 22,', '  GSHSCENE_REASON_INTERPOLATION_SPACE = 41,', ["testReport"]),
    ('gh-reason-invertible', 'hdr', HRP, '  GSHSCENE_REASON_MAP_NOT_INVERTIBLE = 23,', '  GSHSCENE_REASON_MAP_NOT_INVERTIBLE = 42,', ["testReport"]),
    ('gh-reason-degenerate', 'hdr', HRP, '  GSHSCENE_REASON_GRADIENT_DEGENERATE = 24,', '  GSHSCENE_REASON_GRADIENT_DEGENERATE = 44,', ["testReport"]),
    ('gh-cap-spaces', 'hdr', HS, '#define GSHSCENE_CAP_GRADIENT_SPACES (UINT32_C(1) << 8)', '#define GSHSCENE_CAP_GRADIENT_SPACES (UINT32_C(1) << 7)', SINKT),
    ('gh-fixed-radial', 'hdr', HF, '#define GSHSCENE_FIXED_GRADIENT_RADIAL 2u', '#define GSHSCENE_FIXED_GRADIENT_RADIAL 3u', GT),
    # G1, G2: the resource's rules, its canonical form and its sort
    ('gr-format', 'src', GR, '#define GRADIENT_FORMAT_VERSION 1u', '#define GRADIENT_FORMAT_VERSION 2u', GT),
    ('gr-zero', 'src', GR, '  return x == 0.0 ? 0.0 : x;', '  return x;', GT),
    ('gr-linear-other', 'src', GR, '      if (!all_finite(linear, 6) || !all_zero_bits(radial, 6) ||', '      if (!all_finite(linear, 6) ||', GT),
    ('gr-linear-finite', 'src', GR, '      if (!all_finite(linear, 6) || !all_zero_bits(radial, 6) ||', '      if (!all_finite(linear, 5) || !all_zero_bits(radial, 6) ||', GT),
    ('gr-linear-unit', 'src', GR, '!all_zero_bits(sweep, 4) || in->angle_unit != 0) {\n        return GSHSCENE_ERR_INVALID;\n      }\n      for (int i = 0; i < 2; i++) {\n        out->p0', '!all_zero_bits(sweep, 4)) {\n        return GSHSCENE_ERR_INVALID;\n      }\n      for (int i = 0; i < 2; i++) {\n        out->p0', GT),
    ('gr-radial-finite', 'src', GR, '      if (!all_finite(radial, 6) ||', '      if (!all_finite(radial, 2) ||', GT),
    ('gr-sweep-finite', 'src', GR, '      if (!all_finite(sweep, 4) ||', '      if (!all_finite(sweep, 3) ||', GT),
    ('gr-sweep-unit', 'src', GR, '          in->angle_unit >= (uint32_t)GSHGEOM_ANGLE_UNIT_COUNT) {', '          in->angle_unit > (uint32_t)GSHGEOM_ANGLE_UNIT_COUNT) {', GT),
    ('gr-sweep-keeps-unit', 'src', GR, '      out->angle_unit = in->angle_unit;\n', '', GT),
    ('gr-extend', 'src', GR, '  if (in->extend >= (uint32_t)GSHSCENE_EXTEND_COUNT || in->premultiplied > 1u) {', '  if (in->extend > (uint32_t)GSHSCENE_EXTEND_COUNT || in->premultiplied > 1u) {', GT),
    ('gr-premul', 'src', GR, '  if (in->extend >= (uint32_t)GSHSCENE_EXTEND_COUNT || in->premultiplied > 1u) {', '  if (in->extend >= (uint32_t)GSHSCENE_EXTEND_COUNT || in->premultiplied > 2u) {', GT),
    ('gr-map-dim', 'src', GR, '  if (in->map.dim != 2 || gshscene_list_canonical_map', '  if (gshscene_list_canonical_map', GT),
    ('gr-stop-gradient', 'src', GR, '    if (!gshscene_finite(s->offset) || s->color.kind == GSHSCENE_PAINT_GRADIENT) {', '    if (!gshscene_finite(s->offset)) {', GT),
    ('gr-stop-finite', 'src', GR, '    if (!gshscene_finite(s->offset) || s->color.kind == GSHSCENE_PAINT_GRADIENT) {', '    if (s->color.kind == GSHSCENE_PAINT_GRADIENT) {', GT),
    ('gr-stop-target', 'src', GR, '    if (stops[i].color.kind == GSHSCENE_PAINT_SOLID && !target &&', '    if (stops[i].color.kind == GSHSCENE_PAINT_SOLID && 0 && !target &&', GT),
    ('gr-stop-zero', 'src', GR, '    stops[i].offset = canonical_zero(s->offset);', '    stops[i].offset = s->offset;', GT),
    ('gr-sort-stable', 'src', GR, '        scratch[k++] = stops[b].offset < stops[a].offset ? stops[b++] : stops[a++];', '        scratch[k++] = stops[b].offset <= stops[a].offset ? stops[b++] : stops[a++];', GT),
    ('gr-sort-pass', 'src', GR, '    memcpy(stops, scratch, count * sizeof *stops);\n  }\n}', '  }\n}', GT),
    ('gr-limit', 'src', GR, '  if (count > store->limits.max_resource_elements || count > GSHSCENE_GRADIENT_STOPS_MAX) {', '  if (count >= store->limits.max_resource_elements || count > GSHSCENE_GRADIENT_STOPS_MAX) {', GT),
    # G14 (story P2): the limits, each bound and each place it is read
    ('g14-stops-unchecked', 'src', GR, ' || count > GSHSCENE_GRADIENT_STOPS_MAX) {', ') {', GT),
    ('g14-stops-inclusive', 'src', GR, ' || count > GSHSCENE_GRADIENT_STOPS_MAX) {', ' || count >= GSHSCENE_GRADIENT_STOPS_MAX) {', GT),
    ('g14-unchecked', 'src', GR, '  if (result == GSHSCENE_OK && !limited(&g)) {', '  if (0) {', GT),
    ('g14-invalid-second', 'src', GR, '  GSHSCENE_Result result = canonical(store, gradient, &g, stops);\n  if (result == GSHSCENE_OK && !limited(&g)) {', '  GSHSCENE_Result result = canonical(store, gradient, &g, stops);\n  if (!limited(&g)) {', GT),
    ('g14-min-inclusive', 'src', GR, 'm >= GSHSCENE_GRADIENT_VALUE_MIN', 'm > GSHSCENE_GRADIENT_VALUE_MIN', GT),
    ('g14-max-inclusive', 'src', GR, 'm <= GSHSCENE_GRADIENT_VALUE_MAX', 'm < GSHSCENE_GRADIENT_VALUE_MAX', GT),
    ('g14-min-unchecked', 'src', GR, '(m >= GSHSCENE_GRADIENT_VALUE_MIN && m <= GSHSCENE_GRADIENT_VALUE_MAX)', '(m <= GSHSCENE_GRADIENT_VALUE_MAX)', GT),
    ('g14-max-unchecked', 'src', GR, '(m >= GSHSCENE_GRADIENT_VALUE_MIN && m <= GSHSCENE_GRADIENT_VALUE_MAX)', '(m >= GSHSCENE_GRADIENT_VALUE_MIN)', GT),
    ('g14-zero-refused', 'src', GR, '  return m == 0.0 || (m >= GSHSCENE_GRADIENT_VALUE_MIN', '  return (m >= GSHSCENE_GRADIENT_VALUE_MIN', GT),
    ('g14-negative', 'src', GR, '  double m = x < 0.0 ? -x : x;', '  double m = x;', GT),
    ('g14-geometry-short', 'src', GR, '  if (!all_within(values, 16) || !all_within(map, 6)) {', '  if (!all_within(values, 15) || !all_within(map, 6)) {', GT),
    ('g14-map-unchecked', 'src', GR, '  if (!all_within(values, 16) || !all_within(map, 6)) {', '  if (!all_within(values, 16) || !all_within(map, 0)) {', GT),
    ('g14-map-translation', 'src', GR, '  if (!all_within(values, 16) || !all_within(map, 6)) {', '  if (!all_within(values, 16) || !all_within(map, 4)) {', GT),
    ('g14-offsets-unchecked', 'src', GR, '    if (!within(g->stops[i].offset)) {\n      return 0;\n    }\n', '', GT),
    ('g14-h-max', 'hdr', HP, '#define GSHSCENE_GRADIENT_VALUE_MAX 0x1p64', '#define GSHSCENE_GRADIENT_VALUE_MAX 0x1p65', GT),
    ('g14-h-min', 'hdr', HP, '#define GSHSCENE_GRADIENT_VALUE_MIN 0x1p-64', '#define GSHSCENE_GRADIENT_VALUE_MIN 0x1p-63', GT),
    ('g14-h-stops', 'hdr', HP, '#define GSHSCENE_GRADIENT_STOPS_MAX ((size_t)65535)', '#define GSHSCENE_GRADIENT_STOPS_MAX ((size_t)65536)', GT),
    ('gr-bytes', 'src', GR, 'gshscene_sat_mul(count, GSHSCENE_BYTES_PER_STOP)', 'gshscene_sat_mul(count, GSHSCENE_BYTES_PER_COLOR)', GT),
    ('gr-null-stops', 'src', GR, '  if (!store || !gradient || !out || (gradient->stop_count && !gradient->stops)) {', '  if (!store || !gradient || !out) {', GT),
    # the hash, field by field, and equality
    ('gr-hash-kind-extend', 'src', GR, '  gshgeom_hash_u8(&h, (uint8_t)g->kind);\n  gshgeom_hash_u8(&h, (uint8_t)g->extend);', '  gshgeom_hash_u8(&h, (uint8_t)g->extend);\n  gshgeom_hash_u8(&h, (uint8_t)g->kind);', GT),
    ('gr-hash-premul', 'src', GR, '  gshgeom_hash_u8(&h, (uint8_t)g->premultiplied);\n', '', GT),
    ('gr-hash-map', 'src', GR, '  gshgeom_hash_u64(&h, gshgeom_map_hash(&g->map));\n', '', GT),
    ('gr-hash-d', 'src', GR, '      gshgeom_hash_f64(&h, g->d[0]);\n      gshgeom_hash_f64(&h, g->d[1]);\n', '      gshgeom_hash_f64(&h, g->d[1]);\n      gshgeom_hash_f64(&h, g->d[0]);\n', GT),
    ('gr-hash-radii', 'src', GR, '      gshgeom_hash_f64(&h, g->r0);\n', '', GT),
    ('gr-hash-unit', 'src', GR, '      gshgeom_hash_u8(&h, (uint8_t)g->angle_unit);\n', '', GT),
    ('gr-hash-count', 'src', GR, '  gshgeom_hash_u64(&h, (uint64_t)g->stop_count);\n', '', GT),
    ('gr-hash-offset', 'src', GR, '    gshgeom_hash_f64(&h, g->stops[i].offset);\n', '', GT),
    ('gr-hash-entry', 'src', GR, '  gshgeom_hash_u32(h, paint->entry);\n', '', GT),
    ('gr-match-unit', 'src', GR, '      a->angle_unit != b->angle_unit || ', '      ', GT),
    ('gr-match-map', 'src', GR, ' || !map_same(&a->map, &b->map) ||', ' ||', GT),
    ('gr-match-map-tag', 'src', GR, '  if (a->dim != b->dim || a->kind != b->kind) {\n    return 0;\n  }\n  for (int i = 0; i < 2; i++) {', '  if (a->dim != b->dim) {\n    return 0;\n  }\n  for (int i = 0; i < 2; i++) {', GT),
    ('gr-match-geometry', 'src', GR, '  for (int i = 0; i < 16; i++) {', '  for (int i = 0; i < 15; i++) {', GT),
    ('gr-match-offset', 'src', GR, '    if (!same_bits(a->stops[i].offset, b->stops[i].offset) || !paint_same', '    if (!paint_same', GT),
    ('gr-match-space', 'src', GR, '  if (a->kind != b->kind || a->entry != b->entry || !gshscene_space_equal(&a->space, &b->space)) {', '  if (a->kind != b->kind || a->entry != b->entry) {', GT),
    ('gr-foreground', 'src', GR, '      resource->u.gradient.foreground = 1;', '      resource->u.gradient.foreground = 0;', GT),
    ('gr-palette-need', 'src', GR, 'p->entry + 1u > resource->u.gradient.palette_need) {', 'p->entry > resource->u.gradient.palette_need) {', GT),
    ('gr-read-kind', 'src', GR, '  if (!resource || !out || resource->kind != GSHSCENE_RESOURCE_GRADIENT) {', '  if (!resource || !out) {', GT),
    ('gr-dump-stops', 'src', GR, ' map=%016llx stops=%llu\\n", (unsigned)g->premultiplied, HEX(gshgeom_map_hash(&g->map)),\n          HEX(g->stop_count))', ' map=%016llx stops=%llu\\n", (unsigned)g->premultiplied, HEX(gshgeom_map_hash(&g->map)),\n          HEX(g->stop_count + 1u))', GT),
    ('gr-dump-r1', 'src', GR, '        w = fprintf(out, " r1=%016llx", HEX(gshgeom_f64_bits(g->r1)));', '        w = fprintf(out, " r1=%016llx", HEX(gshgeom_f64_bits(g->r0)));', GT),
    # G3: the stops at draw time
    ('gr-resolve-need', 'src', GR, 'gradient->u.gradient.palette_need > palette->u.palette.count)) {', 'gradient->u.gradient.palette_need >= palette->u.palette.count)) {', GT),
    ('gr-resolve-none', 'src', GR, '  if (gradient->u.gradient.palette_need && (!palette ||', '  if (gradient->u.gradient.palette_need && (', GT),
    ('gr-resolve-into', 'src', GR, '  const GSHSCENE_Color_Space * into = gshscene_color_space_is_target(&g->space) ? target_space : &g->space;', '  const GSHSCENE_Color_Space * into = &g->space;', GT),
    ('gr-resolve-from', 'src', GR, 'gshscene_color_space_is_target(&c.space) ? target_space : &c.space;', '&c.space;', GT),
    ('gr-resolve-alpha', 'src', GR, '    out[i].rgba[3] = c.rgba[3];\n', '', GT),
    ('gr-resolve-offset', 'src', GR, '    out[i].offset = g->stops[i].offset;\n', '', GT),
    ('gr-resolve-zero-target', 'src', GR, ' || gshscene_color_space_is_target(target_space) ||', ' ||', GT),
    ('gr-resolve-capacity', 'src', GR, '  if (capacity < g->stop_count || (g->stop_count && !out)) {', '  if (capacity + 1u < g->stop_count || (g->stop_count && !out)) {', GT),
    ('gr-resolve-space', 'src', GR, '    *out_space = *into;', '    *out_space = *target_space;', GT),
    # G10, G11: what draws nothing
    ('gr-singular-zero', 'src', GR, '  return map->dim != 0 && gshgeom_orient2d(o, a, b, &sign) == GSHGEOM_OK && sign == 0;', '  return map->dim != 0 && gshgeom_orient2d(o, a, b, &sign) == GSHGEOM_OK && sign <= 0;', GT),
    ('gr-singular-dim', 'src', GR, '  return map->dim != 0 && gshgeom_orient2d', '  return gshgeom_orient2d', GT),
    ('gr-singular-column', 'src', GR, '  GSHGEOM_Point b = {map->m[0][1], map->m[1][1], 0.0};', '  GSHGEOM_Point b = {map->m[0][1], map->m[0][0], 0.0};', GT),
    ('gr-linear-point', 'src', GR, '        if (g->p0[0] == g->p1[0] && g->p0[1] == g->p1[1]) {', '        if (g->p0[0] == g->p1[0]) {', GT),
    ('gr-linear-orient', 'src', GR, '      GSHGEOM_Point c = {g->p0[0], g->p0[1], 1.0};', '      GSHGEOM_Point c = {g->p0[0], g->p0[1], 0.0};', GT),
    ('gr-linear-direction', 'src', GR, '      GSHGEOM_Point d = {g->d[0], g->d[1], 0.0};', '      GSHGEOM_Point d = {g->d[1], g->d[0], 0.0};', GT),
    ('gr-radial-radii', 'src', GR, ' && g->r0 == g->r1) {', ') {', GT),
    ('gr-radial-centers', 'src', GR, '      if (g->c0[0] == g->c1[0] && g->c0[1] == g->c1[1] && g->r0 == g->r1) {', '      if (g->c0[0] == g->c1[0] && g->r0 == g->r1) {', GT),
    ('gr-sweep-pad', 'src', GR, '      if (repeats && g->a0 == g->a1) {', '      if (g->a0 == g->a1) {', GT),
    ('gr-period-pad', 'src', GR, '  return repeats && g->stop_count >= 2 &&', '  return g->stop_count >= 2 &&', GT),
    ('gr-period-count', 'src', GR, '  return repeats && g->stop_count >= 2 &&', '  return repeats && g->stop_count >= 1 &&', GT),
    ('gr-notice-order', 'src', GR, '  if (not_invertible) {\n    return GSHSCENE_REASON_MAP_NOT_INVERTIBLE;', '  if (not_invertible && !gshscene_gradient_degenerate(g)) {\n    return GSHSCENE_REASON_MAP_NOT_INVERTIBLE;', GT),
    ('gr-notice-target', 'src', GR, '(target_map && gshscene_map_singular(target_map)) ||', '(target_map && 0) ||', GT),
    ('gr-notice-item', 'src', GR, 'gshscene_map_singular(&item->map) ||\n      gshscene_map_singular(&g->map);', 'gshscene_map_singular(&g->map);', GT),
    ('gr-notice-gradient-map', 'src', GR, 'gshscene_map_singular(&item->map) ||\n      gshscene_map_singular(&g->map);', 'gshscene_map_singular(&item->map);', GT),
    ('gr-notice-instances', 'src', GR, '  for (size_t i = 0; i < instance_count && !not_invertible; i++) {', '  for (size_t i = 1; i < instance_count && !not_invertible; i++) {', GT),
    ('gr-notice-fill-only', 'src', GR, '  if (!item || item->kind != GSHSCENE_ITEM_FILL || item->paint.kind', '  if (!item || item->paint.kind', GT),
    # N2, N3 in doubles (convert.c), measured against the oracle's conversions
    ('cv-white', 'src', CV, '#define WHITE_X 0.3127', '#define WHITE_X 0.3128', GT),
    ('cv-srgb-red', 'src', CV, '{{0.64, 0.33}, {0.30, 0.60}, {0.15, 0.06}};\n  static const double ADOBE', '{{0.64, 0.33}, {0.30, 0.61}, {0.15, 0.06}};\n  static const double ADOBE', GT),
    ('cv-adobe', 'src', CV, '{{0.64, 0.33}, {0.21, 0.71}, {0.15, 0.06}}', '{{0.64, 0.33}, {0.21, 0.72}, {0.15, 0.06}}', GT),
    ('cv-p3', 'src', CV, '{0.265, 0.690}', '{0.265, 0.691}', GT),
    ('cv-bt2020', 'src', CV, '{{0.708, 0.292}', '{{0.708, 0.293}', GT),
    ('cv-decode-knee', 'src', CV, '#define SRGB_DECODE_KNEE 0.04045', '#define SRGB_DECODE_KNEE 0.0404', GT),
    ('cv-encode-knee', 'src', CV, '#define SRGB_ENCODE_KNEE 0.0031308', '#define SRGB_ENCODE_KNEE 0.00312', GT),
    ('cv-slope', 'src', CV, '#define SRGB_SLOPE 12.92', '#define SRGB_SLOPE 12.9', GT),
    ('cv-offset', 'src', CV, '#define SRGB_OFFSET 0.055', '#define SRGB_OFFSET 0.056', GT),
    ('cv-exponent', 'src', CV, '#define SRGB_EXPONENT 2.4', '#define SRGB_EXPONENT 2.5', GT),
    ('cv-gamma-encode', 'src', CV, '      return signed_pow(y, 1.0 / space->gamma);', '      return signed_pow(y, space->gamma);', GT),
    ('cv-signed', 'src', CV, '  return x < 0.0 ? -gmath_pow(-x, e) : gmath_pow(x, e);', '  return gmath_pow(x < 0.0 ? -x : x, e);', GT),
    ('cv-same-space', 'src', CV, '  if (gshscene_space_equal(from, to)) {', '  if (0) {', GT),
    ('cv-matrix-order', 'src', CV, '    rgb_to_xyz(from->primaries, ma);\n    rgb_to_xyz(to->primaries, mb);', '    rgb_to_xyz(to->primaries, ma);\n    rgb_to_xyz(from->primaries, mb);', GT),
    ('cv-matrix-skip', 'src', CV, '  if (from->primaries != to->primaries) {', '  if (from->primaries == to->primaries) {', GT),
    ('cv-inverse', 'src', CV, '      out[i][j] = (m[r0][c0] * m[r1][c1] - m[r0][c1] * m[r1][c0]) / d;', '      out[j][i] = (m[r0][c0] * m[r1][c1] - m[r0][c1] * m[r1][c0]) / d;', GT),
    ('cv-scale-columns', 'src', CV, '      out[r][c] = s[c] * p[r][c];', '      out[r][c] = s[r] * p[r][c];', GT),
    ('cv-z', 'src', CV, '    p[2][c] = (1.0 - xy[c][0] - xy[c][1]) / xy[c][1];', '    p[2][c] = (1.0 - xy[c][0]) / xy[c][1];', GT),
    # the paint in a list and a scene
    ('gp-list-release', 'src', LS, '  gshscene_resource_release(item->paint.gradient);\n  gshscene_resource_release(item->group.soft_mask);', '  gshscene_resource_release(item->group.soft_mask);', GT),
    ('gp-truncate-release', 'src', LS, '  for (size_t i = count; i < list->count; i++) {\n    item_release(item_at(list, i));\n  }\n  list->count = count;', '  for (size_t i = count; i < list->count; i++) {\n    gshscene_resource_release(item_at(list, i)->resource);\n  }\n  list->count = count;', GT),
    ('gp-hash', 'src', LS, '  gshgeom_hash_u64(h, paint->gradient ? gshscene_resource_hash(paint->gradient) : 0);\n', '', GT + ["testDump"]),
    ('gp-dump', 'src', LS, 'HEX(paint->gradient ? gshscene_resource_hash(paint->gradient) : 0));', 'HEX(paint->gradient ? gshscene_resource_id(paint->gradient) : 0));', GT + ["testDump"]),
    ('gp-equal', 'src', LS, '      a->paint.gradient != b->paint.gradient || a->paint.entry != b->paint.entry) {', '      a->paint.entry != b->paint.entry) {', GT),
    ('gp-store', 'src', LS, '(!paint->gradient || paint->gradient->store != store || paint->gradient->kind', '(!paint->gradient || (store && 0) || paint->gradient->kind', GT),
    ('gp-kind', 'src', LS, ' || paint->gradient->kind != GSHSCENE_RESOURCE_GRADIENT)', ')', GT),
    ('gp-keep', 'src', LS, '    out->gradient = paint->gradient;\n', '', GT),
    ('gp-node-retain', 'src', ND, '  gshscene_resource_retain(p->paint.gradient);\n', '', GT),
    ('gp-version-dump', 'src', VS, '              HEX(p->paint.gradient ? gshscene_resource_hash(p->paint.gradient) : 0), (long long)p->layer,', '              HEX((uint64_t)0), (long long)p->layer,', GT),
    ('gp-diff-foreground', 'src', DF, '(paint->kind == GSHSCENE_PAINT_GRADIENT && paint->gradient->u.gradient.foreground);', '0;', GT),
    ('gp-diff-palette', 'src', DF, '(paint->kind == GSHSCENE_PAINT_GRADIENT && paint->gradient->u.gradient.palette_need != 0);', '0;', GT),
    # S17: the plan, the report and the test sink
    ('gs-disp-kind', 'src', SK, '    if (gk >= 32 || !(caps->gradient_kinds & (UINT32_C(1) << gk))) {', '    if (gk >= 32) {', GTD),
    ('gs-disp-reason', 'src', SK, '      d.reason = GSHSCENE_REASON_GRADIENT;', '      d.reason = GSHSCENE_REASON_ITEM_KIND;', GTD),
    ('gs-disp-palette', 'src', SK, '    if (need && (!palette || palette->kind != GSHSCENE_RESOURCE_PALETTE || need > palette->u.palette.count)) {', '    if (need && (!palette || palette->kind != GSHSCENE_RESOURCE_PALETTE || need >= palette->u.palette.count)) {', GTD),
    ('gs-disp-no-palette', 'src', SK, '    if (need && (!palette || palette->kind', '    if (need && palette && (palette->kind', GTD),
    ('gs-disp-space-flag', 'src', SK, '  if (gradient && !(caps->flags & GSHSCENE_CAP_GRADIENT_SPACES) &&', '  if (gradient &&', GTD),
    ('gs-disp-space-target', 'src', SK, '      !gshscene_color_space_is_target(&gradient->u.gradient.g.space) &&\n      !(target_space && gshscene_space_equal(&gradient->u.gradient.g.space, target_space))) {', '      !gshscene_color_space_is_target(&gradient->u.gradient.g.space)) {', GTD),
    ('gs-disp-space-zero', 'src', SK, '      !gshscene_color_space_is_target(&gradient->u.gradient.g.space) &&\n', '', GTD),
    ('gs-disp-space-reason', 'src', SK, '    d.reason = GSHSCENE_REASON_INTERPOLATION_SPACE;', '    d.reason = GSHSCENE_REASON_BLEND_SPACE;', GTD),
    ('gs-plan-notice', 'src', SK, '    if (notice) {\n      result = gshscene_report_add', '    if (0) {\n      result = gshscene_report_add', GTD),
    ('gs-plan-class', 'src', SK, 'gshscene_report_add(report, GSHSCENE_ENTRY_NOTICE, notice,', 'gshscene_report_add(report, GSHSCENE_ENTRY_APPROXIMATED, notice,', GTD),
    ('gs-plan-instances', 'src', SK, 'gshscene_gradient_notice(item, maps, count - 1, &job->target.map);', 'gshscene_gradient_notice(item, maps, 0, &job->target.map);', GTD),
    ('gs-plan-target', 'src', SK, 'gshscene_gradient_notice(item, maps, count - 1, &job->target.map);', 'gshscene_gradient_notice(item, maps, count - 1, NULL);', GTD),
    ('rp-class-notice', 'src', RP, '      return "notice";', '      return "notices";', ["testReport"]),
    ('rp-reason-gradient', 'src', RP, '      return "gradient";', '      return "gradients";', ["testReport"]),
    ('rp-reason-space', 'src', RP, '      return "interpolation-space";', '      return "interpolation";', ["testReport"]),
    ('rp-reason-invertible', 'src', RP, '      return "map-not-invertible";', '      return "not-invertible";', ["testReport"]),
    ('rp-reason-degenerate', 'src', RP, '      return "gradient-degenerate";', '      return "degenerate";', ["testReport"]),
    ('ds-notice', 'src', DS, '      if (notice) {\n        snprintf', '      if (0) {\n        snprintf', GTD),
    ('ds-notice-class', 'src', DS, '(int)GSHSCENE_ENTRY_NOTICE, (int)notice);\n        emit_text(run, run->line, &failed);\n        return failed;\n      }\n', '(int)GSHSCENE_ENTRY_REFUSED, (int)notice);\n        emit_text(run, run->line, &failed);\n        return failed;\n      }\n', GTD),
    ('ds-notice-instances', 'src', DS, 'gshscene_gradient_notice(item, run->maps, count - 1, &run->job->target.map);', 'gshscene_gradient_notice(item, run->maps, 0, &run->job->target.map);', GTD),
    ('ds-record-once', 'src', DS, '    if (run->gradients[i] == id) {', '    if (0) {', GTD),
    ('ds-record-remember', 'src', DS, '  run->gradients[run->gradient_count++] = id;\n', '', GTD),
    ('ds-record-id', 'src', DS, 'item->kind == GSHSCENE_ITEM_FILL && item->paint.kind == GSHSCENE_PAINT_GRADIENT) {\n        snprintf(run->line, LINE_BYTES, " grad=%" PRIu64, gshscene_resource_id(item->paint.gradient));', 'item->kind == GSHSCENE_ITEM_FILL && item->paint.kind == GSHSCENE_PAINT_GRADIENT) {\n        snprintf(run->line, LINE_BYTES, " grad=%" PRIu64, gshscene_resource_hash(item->paint.gradient));', GTD),
    ('ds-record-fill-only', 'src', DS, '      if (item->kind == GSHSCENE_ITEM_FILL && item->paint.kind == GSHSCENE_PAINT_GRADIENT) {\n        GSHSCENE_Result r = gradient_record', '      if (0) {\n        GSHSCENE_Result r = gradient_record', GTD),
    ('ds-record-foreground', 'src', DS, '  gshscene_list_foreground(list, &foreground);\n', '  gshscene_color_init(&foreground);\n', GTD),
    ('ds-record-stops', 'src', DS, '        gshgeom_f64_bits(s->rgba[2]), gshgeom_f64_bits(s->rgba[3]));', '        gshgeom_f64_bits(s->rgba[2]), gshgeom_f64_bits(s->rgba[2]));', GTD),
    ('ds-need-stops', 'src', DS, '      *stops = n > *stops ? n : *stops;', '      *stops = n;', GTD),
    ('ds-need-pictures', 'src', DS, '      gradient_need(gshscene_resource_picture(item->resource), fills, stops, strokes);', '      (void)0;', GTD),
    ('ds-caps-kinds', 'src', DS, '  for (uint32_t k = GSHSCENE_GRADIENT_LINEAR; k < GSHSCENE_GRADIENT_KIND_COUNT; k++) {', '  for (uint32_t k = GSHSCENE_GRADIENT_RADIAL; k < GSHSCENE_GRADIENT_KIND_COUNT; k++) {', GTD),
    ('ds-caps-spaces', 'src', DS, ' | GSHSCENE_CAP_GRADIENT_SPACES | GSHSCENE_CAP_PREPARE |', ' | GSHSCENE_CAP_PREPARE |', GTD),
    ('ds-caps-valid', 'src', DS, ' && !(caps->gradient_kinds & ~kinds) &&', ' && (kinds || 1) &&', GTD),
    ('ds-caps-valid-bit0', 'src', DS, ' - 1u) & ~UINT32_C(1);', ' - 1u);', GTD),
    # the float-free gradient (fixed.h)
    ('gf-d', 'src', FX, '      g.d[0] = ((double)v[4] - (double)v[0]) / unit;', '      g.d[0] = (double)v[4] / unit;', GT),
    ('gf-dy', 'src', FX, '      g.d[1] = ((double)v[5] - (double)v[1]) / unit;', '      g.d[1] = ((double)v[5] - (double)v[0]) / unit;', GT),
    ('gf-sweep-scale', 'src', FX, '      g.a0 = (double)v[2] * 180.0 / unit;', '      g.a0 = (double)v[2] * 360.0 / unit;', GT),
    ('gf-sweep-a1', 'src', FX, '      g.a1 = (double)v[3] * 180.0 / unit;', '      g.a1 = (double)v[2] * 180.0 / unit;', GT),
    ('gf-sweep-unit', 'src', FX, '      g.angle_unit = GSHGEOM_ANGLE_DEGREES;', '      g.angle_unit = GSHGEOM_ANGLE_TURNS;', GT),
    ('gf-radius', 'src', FX, '      g.r1 = fixed_value(v[5], fixed->bits);', '      g.r1 = fixed_value(v[5], 0u);', GT),
    ('gf-values', 'src', FX, 'GRADIENT_VALUES[4] = {0u, 6u, 6u, 4u};', 'GRADIENT_VALUES[4] = {0u, 6u, 6u, 6u};', GT),
    ('gf-transfer', 'src', FX, ' && fixed->transfer != (uint32_t)GCOL_TRANSFER_SRGB) {', ') {', GT),
    ('gf-placement', 'src', FX, 'gshgeom_map_design_to_model(s, run.glyphs[0].x, run.glyphs[0].y, &maps[0]);', 'gshgeom_map_design_to_model(s, 0.0, 0.0, &maps[0]);', GT),
    ('gf-ops', 'src', FX, '  checked = ops_maps(ops, count, maps + 1);', '  checked = ops_maps(ops, 0, maps + 1);', GT),
    ('gf-stop-bits', 'src', FX, '    stops[i].offset = fixed_value(s->offset, s->bits);', '    stops[i].offset = fixed_value(s->offset, 0u);', GT),
    ('gf-store', 'src', FX, ' || run->store != store || fixed->kind', ' || fixed->kind', GT),
    ('gf-fill-alpha', 'src', FX, '  p.rgba[3] = fixed_value(alpha, bits);', '  p.rgba[3] = 1.0;', GT),
    # the oracle (G4 to G11, N3): its selftest or the vectors it writes
    ('gorc-reflect', 'orc', GO, '                t = o_first + period - (-mp if neg else mp)', '                t = o_first + period + (-mp if neg else mp)', ['gselftest', 'gvectors']),
    ('gorc-repeat', 'orc', GO, '            t = t - k * period if not isinstance(t, D) else t - D(k) * dec(period)', '            t = t - k * 2 * period if not isinstance(t, D) else t - D(k) * dec(period)', ['gselftest', 'gvectors']),
    ('gorc-pad', 'orc', GO, '            return interpolant(stops[0][1] if lt(t, o_first) else stops[-1][1], premul)\n        if extend == REPEAT:', '            return interpolant(stops[-1][1] if lt(t, o_first) else stops[0][1], premul)\n        if extend == REPEAT:', ['gselftest', 'gvectors']),
    ('gorc-period-zero', 'orc', GO, '        if extend != PAD:\n            return None\n        return interpolant', '        if extend == REFLECT:\n            return None\n        return interpolant', ['gselftest', 'gvectors']),
    ('gorc-equal-offsets', 'orc', GO, '    while j < len(stops) and not lt(t, stops[j][0]):', '    while j < len(stops) and lt(stops[j][0], t):', ['gselftest', 'gvectors']),
    ('gorc-premul', 'orc', GO, '    return [a * r, a * g, a * b, a] if premul else [r, g, b, a]', '    return [r, g, b, a]', ['gselftest', 'gvectors']),
    ('gorc-unpremul', 'orc', GO, '        vals = [ZERO] * 4 if a == 0 else [sat(vals[0] / a), sat(vals[1] / a), sat(vals[2] / a), a]', '        vals = [ZERO] * 4 if a == 0 else [sat(vals[0]), sat(vals[1]), sat(vals[2]), a]', ['gselftest', 'gvectors']),
    ('gorc-alpha', 'orc', GO, '    vals[3] = vals[3] * alpha', '    vals[3] = vals[3]', ['gvectors']),
    ('gorc-normal', 'orc', GO, '        nx, ny = -dy, dx', '        nx, ny = dy, dx', ['gselftest', 'gvectors']),
    ('gorc-root', 'orc', GO, '        hi = Q(b / a, ONE / a, disc) if a > 0 else Q(b / a, -ONE / a, disc)', '        hi = Q(b / a, -ONE / a, disc) if a > 0 else Q(b / a, ONE / a, disc)', ['gselftest', 'gvectors']),
    ('gorc-radius', 'orc', GO, '        if rw.sign() > 0:', '        if rw.sign() >= 0:', ['gselftest', 'gvectors']),
    ('gorc-equal-circles', 'orc', GO, '    if c0x == c1x and c0y == c1y and r0 == r1:', '    if c0x == c1x and c0y == c1y:', ['gselftest', 'gvectors']),
    ('gorc-quarter', 'orc', GO, '        return F(1, 4) if y > 0 else F(3, 4)', '        return F(3, 4) if y > 0 else F(1, 4)', ['gselftest', 'gvectors']),
    ('gorc-atan', 'orc', GO, '            x = x / (1 + (1 + x * x).sqrt())', '            x = x / (1 + (1 + x * x).sqrt() * D("1.0000000001"))', ['gselftest', 'gvectors']),
    ('gorc-pi', 'orc', GO, '        value = 16 * atan_inv(5) - 4 * atan_inv(239)', '        value = 16 * atan_inv(5) - 4 * atan_inv(238)', ['gselftest', 'gvectors']),
    ('gorc-step', 'orc', GO, "            below = (dec(theta) < dec(a0)) if isinstance(theta, D) else theta < a0", "            below = (dec(theta) <= dec(a0)) if isinstance(theta, D) else theta <= a0", ['gselftest', 'gvectors']),
    ('gorc-singular', 'orc', GO, '        if det(m) == 0:', '        if det(m) == 0 and m[0] == 0:', ['gselftest', 'gvectors']),
    ('gorc-inverse', 'orc', GO, '    return ((m[3] * x - m[1] * y) / dd, (-m[2] * x + m[0] * y) / dd)', '    return ((m[3] * x - m[2] * y) / dd, (-m[1] * x + m[0] * y) / dd)', ['gvectors']),
    ('gorc-decode', 'orc', GO, '        return power((x + F("0.055")) / F("1.055"), F("2.4")), False', '        return power((x + F("0.055")) / F("1.055"), F("2.2")), False', ['gselftest', 'gvectors']),
    ('gorc-white', 'orc', GO, 'WHITE = (F("0.3127"), F("0.3290"))', 'WHITE = (F("0.3127"), F("0.3291"))', ['gselftest', 'gvectors']),
    # the evaluator (tests/ref_composite.h): its agreement with the vectors
    ('gtst-reflect', 'tst', RC, '      t = o0 + period - std::fabs(m - period);', '      t = o0 + period + std::fabs(m - period);', GT),
    ('gtst-repeat', 'tst', RC, '      t = o0 + (t - o0 - std::floor((t - o0) / period) * period);', '      t = o0 + (t - o0 - std::floor((t - o0) / period));', GT),
    ('gtst-pad', 'tst', RC, '      g_interpolant(t < o0 ? stops.front() : stops.back(), premul, out);\n      return true;\n    }\n    if (extend == GSHSCENE_EXTEND_REPEAT) {', '      g_interpolant(t < o0 ? stops.back() : stops.front(), premul, out);\n      return true;\n    }\n    if (extend == GSHSCENE_EXTEND_REPEAT) {', GT),
    ('gtst-equal-offsets', 'tst', RC, '  while (j < stops.size() && !(t < stops[j].offset)) {', '  while (j < stops.size() && stops[j].offset < t) {', GT),
    ('gtst-premul', 'tst', RC, '    out[k] = premul ? s.rgba[k] * a : s.rgba[k];', '    out[k] = premul ? s.rgba[k] : s.rgba[k] * a;', GT),
    ('gtst-normal', 'tst', RC, '      nx = -geom[5];', '      nx = geom[5];', GT),
    ('gtst-root', 'tst', RC, '        if (hi < lo) {\n          std::swap(hi, lo);', '        if (hi > lo) {\n          std::swap(hi, lo);', GT),
    ('gtst-radius', 'tst', RC, '      if (r0 + w[i] * dr > 0.0) {', '      if (r0 + w[i] * dr >= -1.0) {', GT),
    ('gtst-sweep-unit', 'tst', RC, '    double per_turn = unit == GSHGEOM_ANGLE_DEGREES ? 360.0 : unit == GSHGEOM_ANGLE_GRADIANS ? 400.0', '    double per_turn = unit == GSHGEOM_ANGLE_DEGREES ? 360.0 : unit == GSHGEOM_ANGLE_GRADIANS ? 360.0', GT),
    ('gtst-sweep-wrap', 'tst', RC, '      if (rad < 0) {\n        rad += two_pi;', '      if (rad < 0) {\n        rad += two_pi / 2;', GT),
    ('gtst-unmap', 'tst', RC, '  x = (d * u - b * v) / det / s;', '  x = (d * u - c * v) / det / s;', GT),
    ('gtst-alpha', 'tst', RC, '  r.rgba[3] = r.rgba[3] * alpha;', '  r.rgba[3] = r.rgba[3] * (alpha > 2.0 ? alpha : 1.0);', GT),
]

# N1's bound and saturation (Corey, 2026-10-21; color-paint story P2's change log): each place the bound is read, its
# comparisons and its order after the validity checks, each point of the saturation in the library (the resolve), the
# test evaluator and both oracles, the clamp itself and the two constants.
IH_ = "src/internal.h"
CV_ = "src/paint/convert.c"
CB = ["testColor_bound"]
CBG = ["testColor_bound", "testGradient"]
PLANTS += [
    ('n1-check-unbounded', 'src', CS, '  return gshscene_color_bounded(color->rgba) ? GSHSCENE_OK : GSHSCENE_ERR_LIMIT;', '  return GSHSCENE_OK;', CB),
    ('n1-check-first', 'src', CS, '  GSHSCENE_Result result = gshscene_color_check_valid(color);', '  GSHSCENE_Result result = color && !gshscene_color_bounded(color->rgba) ? GSHSCENE_ERR_LIMIT : gshscene_color_check_valid(color);', CB),
    ('n1-bounded-upper', 'src', IH_, ' && rgba[i] <= GSHSCENE_COLOR_COMPONENT_MAX)', ' && rgba[i] < GSHSCENE_COLOR_COMPONENT_MAX)', CB),
    ('n1-bounded-lower', 'src', IH_, '(rgba[i] >= -GSHSCENE_COLOR_COMPONENT_MAX &&', '(rgba[i] > -GSHSCENE_COLOR_COMPONENT_MAX &&', CB),
    ('n1-bounded-upper-unchecked', 'src', IH_, '(rgba[i] >= -GSHSCENE_COLOR_COMPONENT_MAX && rgba[i] <= GSHSCENE_COLOR_COMPONENT_MAX)', '(rgba[i] >= -GSHSCENE_COLOR_COMPONENT_MAX)', CB),
    ('n1-bounded-lower-unchecked', 'src', IH_, '(rgba[i] >= -GSHSCENE_COLOR_COMPONENT_MAX && rgba[i] <= GSHSCENE_COLOR_COMPONENT_MAX)', '(rgba[i] <= GSHSCENE_COLOR_COMPONENT_MAX)', CB),
    ('n1-bounded-components', 'src', IH_, '  for (int i = 0; i < 3; i++) {\n    if (!(rgba[i] >= -GSHSCENE_COLOR_COMPONENT_MAX', '  for (int i = 0; i < 2; i++) {\n    if (!(rgba[i] >= -GSHSCENE_COLOR_COMPONENT_MAX', CB),
    ('n1-sat-upper', 'src', IH_, '  return x > GSHSCENE_COLOR_SATURATION ? GSHSCENE_COLOR_SATURATION\n', '  return x > GSHSCENE_COLOR_SATURATION ? x\n', CBG),
    ('n1-sat-lower', 'src', IH_, '      : x < -GSHSCENE_COLOR_SATURATION ? -GSHSCENE_COLOR_SATURATION : x;', '      : x < -GSHSCENE_COLOR_SATURATION ? x : x;', CBG),
    ('n1-h-component', 'hdr', HP, '#define GSHSCENE_COLOR_COMPONENT_MAX 0x1p16', '#define GSHSCENE_COLOR_COMPONENT_MAX 0x1p17', CB),
    ('n1-h-saturation', 'hdr', HP, '#define GSHSCENE_COLOR_SATURATION 0x1p64', '#define GSHSCENE_COLOR_SATURATION 0x1p65', CBG),
    ('n1-fill-unchecked', 'src', LS, '  if (result == GSHSCENE_OK && !gshscene_color_bounded(item.paint.rgba)) {\n    result = GSHSCENE_ERR_LIMIT; /* N1, after every validity check */', '  if (0) {\n    result = GSHSCENE_ERR_LIMIT; /* N1, after every validity check */', CB),
    ('n1-fill-first', 'src', LS, '  if (result == GSHSCENE_OK && !gshscene_color_bounded(item.paint.rgba)) {\n    result = GSHSCENE_ERR_LIMIT; /* N1, after every validity check */', '  if (!gshscene_color_bounded(item.paint.rgba)) {\n    result = GSHSCENE_ERR_LIMIT; /* N1, after every validity check */', CB),
    ('n1-node-unchecked', 'src', LS, '  return gshscene_color_bounded(out->rgba) ? GSHSCENE_OK : GSHSCENE_ERR_LIMIT;', '  return GSHSCENE_OK;', CB),
    ('n1-node-first', 'src', LS, 'GSHSCENE_Paint * out) {\n  GSHSCENE_Result result = canonical_paint(store, paint, out);', 'GSHSCENE_Paint * out) {\n  if (paint && !gshscene_color_bounded(paint->rgba)) {\n    return GSHSCENE_ERR_LIMIT;\n  }\n  GSHSCENE_Result result = canonical_paint(store, paint, out);', CB),
    ('n1-palette-unchecked', 'src', RS, '    if (!gshscene_color_bounded(colors[i].rgba)) {\n      return GSHSCENE_ERR_LIMIT;', '    if (0) {\n      return GSHSCENE_ERR_LIMIT;', CB),
    ('n1-palette-first', 'src', RS, '    GSHSCENE_Result checked = gshscene_color_check_valid(&colors[i]);', '    GSHSCENE_Result checked = gshscene_color_check(&colors[i]);', CB),
    ('n1-stop-unchecked', 'src', GR, '    if (!gshscene_color_bounded(g->stops[i].color.rgba)) {', '    if (0) {', CB),
    ('n1-stop-first', 'src', GR, '    result = gshscene_list_canonical_paint_valid(store, &s->color, &stops[i].color);', '    result = gshscene_list_canonical_paint(store, &s->color, &stops[i].color);', CB),
    ('n1-resolve-decode', 'src', CV_, '    lin[i] = gshscene_saturate(decode(from, in[i]));', '    lin[i] = decode(from, in[i]);', CBG),
    ('n1-resolve-matrix', 'src', CV_, '    for (int i = 0; i < 3; i++) {\n      lin[i] = gshscene_saturate(lin[i]);', '    for (int i = 0; i < 0; i++) {\n      lin[i] = gshscene_saturate(lin[i]);', CBG),
    ('n1-resolve-encode', 'src', CV_, '    out[i] = gshscene_saturate(encode(to, lin[i]));', '    out[i] = encode(to, lin[i]);', CBG),
    # the test evaluator (tests/ref_composite.h), each point and the clamp
    ('tst-n1-decode', 'tst', RC, '{sat(decode(a, in[0])), sat(decode(a, in[1])), sat(decode(a, in[2]))}', '{decode(a, in[0]), decode(a, in[1]), decode(a, in[2])}', ['testComposite', 'testColor_bound']),
    ('tst-n1-matrix', 'tst', RC, '      lin[i] = sat(moved[i]);', '      lin[i] = moved[i];', ['testComposite', 'testColor_bound']),
    ('tst-n1-encode', 'tst', RC, '    out[i] = sat(encode(b, lin[i]));', '    out[i] = encode(b, lin[i]);', ['testComposite', 'testColor_bound']),
    ('tst-n1-unpremultiply', 'tst', RC, '{unpremultiply(p.v[0], p.v[3]), unpremultiply(p.v[1], p.v[3]), unpremultiply(p.v[2], p.v[3])}', '{p.v[0] / p.v[3], p.v[1] / p.v[3], p.v[2] / p.v[3]}', ['testComposite', 'testColor_bound']),
    ('tst-n1-unpremultiply-guard', 'tst', RC, '  if ((c < 0 ? -c : c) >= k * m) {', '  if ((c < 0 ? -c : c) >= k * m && 0) {', ['testColor_bound', 'testComposite']),
    ('tst-n1-apply', 'tst', RC, '    out.v[i] = sat((1 - m) * d.v[i] + m * o.v[i]);', '    out.v[i] = (1 - m) * d.v[i] + m * o.v[i];', ['testComposite', 'testColor_bound']),
    ('tst-n1-g4', 'tst', RC, '        r.rgba[k] = unpremultiply(r.rgba[k], a);', '        r.rgba[k] = r.rgba[k] / a;', ['testColor_bound', 'testGradient']),
    ('tst-n1-bound', 'tst', RC, 'inline double sat(double x) {\n  const double k = 0x1p64;', 'inline double sat(double x) {\n  const double k = 0x1p63;', ['testComposite', 'testColor_bound']),
    # B1 to B12 in the evaluator (color-paint story P3).
    ('tst-b-screen', 'tst', 'tests/ref_composite.h', '  return b + s - b * s;\n}', '  return b + s + b * s;\n}', ['testComposite']),
    ('tst-b-overlay-exchanged', 'tst', 'tests/ref_composite.h', 'case GSHSCENE_COMPOSITE_OVERLAY: return mix_hard_light(s, b);', 'case GSHSCENE_COMPOSITE_OVERLAY: return mix_hard_light(b, s);', ['testComposite']),
    ('tst-b-darken', 'tst', 'tests/ref_composite.h', 'case GSHSCENE_COMPOSITE_DARKEN: return b < s ? b : s;', 'case GSHSCENE_COMPOSITE_DARKEN: return b > s ? b : s;', ['testComposite']),
    ('tst-b-lighten', 'tst', 'tests/ref_composite.h', 'case GSHSCENE_COMPOSITE_LIGHTEN: return b > s ? b : s;', 'case GSHSCENE_COMPOSITE_LIGHTEN: return b < s ? b : s;', ['testComposite']),
    ('tst-b-dodge-zero', 'tst', 'tests/ref_composite.h', '      if (b == 0.0) return 0.0;', '      if (b == 0.0) return 1.0;', ['testComposite']),
    ('tst-b-dodge-clamp', 'tst', 'tests/ref_composite.h', '      return q < 1.0 ? q : 1.0;', '      return q;', ['testComposite']),
    ('tst-b-burn-one', 'tst', 'tests/ref_composite.h', '      if (b == 1.0) return 1.0;', '      if (b == 1.0) return 0.0;', ['testComposite']),
    ('tst-b-burn-quotient', 'tst', 'tests/ref_composite.h', '      double q = quotient(1.0 - b, s);', '      double q = quotient(b, s);', ['testComposite']),
    ('tst-b-hard-light', 'tst', 'tests/ref_composite.h', 'return s <= 0.5 ? b * (2.0 * s) : mix_screen(b, 2.0 * s - 1.0);', 'return s <= 0.5 ? b * s : mix_screen(b, 2.0 * s - 1.0);', ['testComposite']),
    ('tst-b-soft-light-low', 'tst', 'tests/ref_composite.h', 'if (s <= 0.5) return b - (1.0 - 2.0 * s) * b * (1.0 - b);', 'if (s <= 0.5) return b - (1.0 - s) * b * (1.0 - b);', ['testComposite']),
    ('tst-b-soft-light-d', 'tst', 'tests/ref_composite.h', '((16.0 * b - 12.0) * b + 4.0) * b', '((16.0 * b - 11.0) * b + 4.0) * b', ['testComposite']),
    ('tst-b-soft-light-root', 'tst', 'tests/ref_composite.h', ': gmath_sqrt(b);', ': b;', ['testComposite']),
    ('tst-b-difference', 'tst', 'tests/ref_composite.h', 'case GSHSCENE_COMPOSITE_DIFFERENCE: return b - s < 0 ? s - b : b - s;', 'case GSHSCENE_COMPOSITE_DIFFERENCE: return b - s;', ['testComposite']),
    ('tst-b-exclusion', 'tst', 'tests/ref_composite.h', 'default: return b + s - 2.0 * b * s; /* exclusion */', 'default: return b + s - b * s; /* exclusion */', ['testComposite']),
    ('tst-b-multiply', 'tst', 'tests/ref_composite.h', 'case GSHSCENE_COMPOSITE_MULTIPLY: return b * s;', 'case GSHSCENE_COMPOSITE_MULTIPLY: return b;', ['testComposite']),
    ('tst-b-lum', 'tst', 'tests/ref_composite.h', 'return 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2];', 'return 0.3 * c[0] + 0.59 * c[1] + 0.1 * c[2];', ['testComposite']),
    ('tst-b-clip-literal-x', 'tst', 'tests/ref_composite.h', '  double xl = x - n - ln;', '  double xl = x - l;', ['testComposite']),
    ('tst-b-clip-literal-low', 'tst', 'tests/ref_composite.h', '  double ln = lum(d);', '  double ln = l - n;', ['testComposite']),
    ('tst-b-clip-equal', 'tst', 'tests/ref_composite.h', '  if (n < 0.0 && ln > 0.0) {', '  if (n < 0.0) {', ['testComposite']),
    ('tst-b-setsat-order', 'tst', 'tests/ref_composite.h', '  if (c[i[1]] > c[i[2]]) std::swap(i[1], i[2]);\n', '', ['testComposite']),
    ('tst-b-hue', 'tst', 'tests/ref_composite.h', '      set_sat(c, sat_of(cb));\n      set_lum(c, lum(cb));', '      set_sat(c, sat_of(cs));\n      set_lum(c, lum(cb));', ['testComposite']),
    ('tst-b-saturation-after-mix', 'tst', 'tests/ref_composite.h', '  for (int k = 0; k < 3; k++) out[k] = sat(c[k]);', '  for (int k = 0; k < 3; k++) out[k] = c[k];', ['testComposite']),
    ('tst-b-cap', 'tst', 'tests/ref_composite.h', '  const double cap = 0x1p66;', '  const double cap = 0x1p60;', ['testComposite']),
    ('tst-b-term-alphas', 'tst', 'tests/ref_composite.h', '+ sa * da * b[k];', '+ sa * b[k];', ['testComposite']),
    ('tst-b-alpha', 'tst', 'tests/ref_composite.h', '  out.v[3] = sa + da * (1.0 - sa);\n  return out;', '  out.v[3] = sa + da;\n  return out;', ['testComposite']),
    ('tst-b-bounded', 'tst', 'tests/ref_composite.h', 'out.v[k] = s.v[k] * (1.0 - da) + d.v[k] * (1.0 - sa) + sa', 'out.v[k] = s.v[k] * (1.0 - da) + d.v[k] * (1.0 - sa) * da + sa', ['testComposite']),
    ('tst-b-is-blend', 'tst', 'tests/ref_composite.h', 'return composite >= GSHSCENE_COMPOSITE_SCREEN && composite <= GSHSCENE_COMPOSITE_LUMINOSITY;', 'return composite >= GSHSCENE_COMPOSITE_SCREEN && composite < GSHSCENE_COMPOSITE_LUMINOSITY;', ['testComposite']),
    # T1 to T7 in the oracle and the evaluator (color-paint story P3).
    ('orc-t-knockout-backdrop', 'orc', 'tools/composite-oracle.py', '    bk = frame.initial if frame.knockout else frame.layer', '    bk = frame.layer', ['selftest', 'vectors']),
    ('orc-t-group-alpha-backdrop', 'orc', 'tools/composite-oracle.py', '    agb = ZERO if frame.knockout else frame.alpha_g', '    agb = frame.alpha_g', ['selftest', 'vectors']),
    ('orc-t-shape-union', 'orc', 'tools/composite-oracle.py', '    frame.shape_g = frame.shape_g + f - frame.shape_g * f', '    frame.shape_g = f', ['selftest', 'vectors']),
    ('orc-t-removal', 'orc', 'tools/composite-oracle.py', 'own = [sat(done.layer[i] - (ONE - ag) * done.initial[i]) for i in range(3)] + [ag]', 'own = [sat(done.layer[i] - done.initial[i]) for i in range(3)] + [ag]', ['selftest', 'vectors']),
    ('orc-t-own-alpha', 'orc', 'tools/composite-oracle.py', 'own = [sat(done.layer[i] - (ONE - ag) * done.initial[i]) for i in range(3)] + [ag]', 'own = [sat(done.layer[i] - (ONE - ag) * done.initial[i]) for i in range(3)] + [done.layer[3]]', ['selftest', 'vectors']),
    ('orc-t-element-shape', 'orc', 'tools/composite-oracle.py', '                f = done.outer * done.shape_g\n', '                f = done.outer\n', ['selftest', 'vectors']),
    ('orc-t-initial-converted', 'orc', 'tools/composite-oracle.py', 'frame.layer = convert_premultiplied(seen, top.space, space, ex)', 'frame.layer = list(seen)', ['selftest', 'vectors']),
    ('orc-t-initial-knockout-parent', 'orc', 'tools/composite-oracle.py', 'seen = top.initial if top.knockout else top.layer', 'seen = top.layer', ['selftest', 'vectors']),
    ('orc-t-blend-term', 'orc', 'tools/composite-oracle.py', 'term = [a[i] * (ONE - bk[3]) + qa * bk[3] * b[i] for i in range(3)]', 'term = [a[i] + qa * bk[3] * b[i] for i in range(3)]', ['selftest', 'vectors']),
    ('orc-t-alpha', 'orc', 'tools/composite-oracle.py', '            sat((ONE - f) * old[3] + (f - qa) * bk[3] + qa)]', '            sat((ONE - f) * old[3] + qa)]', ['selftest', 'vectors']),
    ('orc-t-other-operators-shape', 'orc', 'tools/composite-oracle.py', '                element(parent, done.outer, op, [opacity * v for v in moved], ex)', '                element(parent, done.outer * done.shape_g, op, [opacity * v for v in moved], ex)', ['selftest', 'vectors']),
    ('tst-t-knockout-backdrop', 'tst', 'tests/ref_composite.h', '  const Px & bk = frame.group.knockout ? frame.initial : frame.layer;', '  const Px & bk = frame.layer;', ['testComposite']),
    ('tst-t-group-alpha-backdrop', 'tst', 'tests/ref_composite.h', '  double agb = frame.group.knockout ? 0.0 : frame.alpha_g;', '  double agb = frame.alpha_g;', ['testComposite']),
    ('tst-t-removal', 'tst', 'tests/ref_composite.h', 'own.v[k] = sat(done.layer.v[k] - (1.0 - done.alpha_g) * done.initial.v[k]); /* T5 */', 'own.v[k] = sat(done.layer.v[k] - done.initial.v[k]); /* T5 */', ['testComposite']),
    ('tst-t-element-shape', 'tst', 'tests/ref_composite.h', 'element(parent, done.outer * done.shape_g, composite,', 'element(parent, done.outer, composite,', ['testComposite']),
    ('tst-t-initial', 'tst', 'tests/ref_composite.h', '          g.layer = convert_premultiplied(top.group.knockout ? top.initial : top.layer, top.space, g.space);', '          g.layer = top.layer;', ['testComposite']),
    ('tst-t-shape-union', 'tst', 'tests/ref_composite.h', '  frame.shape_g = frame.shape_g + f - frame.shape_g * f;', '  frame.shape_g = f;', ['testComposite']),
    # the composite oracle and the gradient oracle
    ('orc-n1-upper', 'orc', 'tools/composite-oracle.py', '    return SATURATION if x > SATURATION else -SATURATION', '    return x if x > SATURATION else -SATURATION', ['selftest', 'vectors']),
    ('orc-n1-lower', 'orc', 'tools/composite-oracle.py', 'else -SATURATION if x < -SATURATION else x', 'else x if x < -SATURATION else x', ['selftest', 'vectors']),
    ('orc-n1-bound', 'orc', 'tools/composite-oracle.py', 'SATURATION = F(2) ** 64', 'SATURATION = F(2) ** 63', ['selftest', 'vectors']),
    ('orc-n1-decode', 'orc', 'tools/composite-oracle.py', 'lin = [sat(decode(a, v, ex)) for v in rgb]', 'lin = [decode(a, v, ex) for v in rgb]', ['selftest', 'vectors']),
    ('orc-n1-matrix', 'orc', 'tools/composite-oracle.py', 'lin = [sat(v) for v in matvec(', 'lin = [v for v in matvec(', ['selftest', 'vectors']),
    ('orc-n1-encode', 'orc', 'tools/composite-oracle.py', 'return [sat(encode(b, v, ex)) for v in lin]', 'return [encode(b, v, ex) for v in lin]', ['selftest', 'vectors']),
    ('orc-n1-unpremultiply', 'orc', 'tools/composite-oracle.py', 'straight = [sat(px[i] / alpha) for i in range(3)]', 'straight = [px[i] / alpha for i in range(3)]', ['selftest', 'vectors']),
    ('orc-n1-apply', 'orc', 'tools/composite-oracle.py', 'return [sat((ONE - m) * d[i] + m * o[i]) for i in range(4)]', 'return [(ONE - m) * d[i] + m * o[i] for i in range(4)]', ['selftest', 'vectors']),
    ('gorc-n1-upper', 'orc', GO, '    return SATURATION if x > SATURATION else -SATURATION', '    return x if x > SATURATION else -SATURATION', ['gselftest', 'gvectors']),
    ('gorc-n1-bound', 'orc', GO, 'SATURATION = F(2) ** 64', 'SATURATION = F(2) ** 63', ['gselftest', 'gvectors']),
    ('gorc-n1-decode', 'orc', GO, '        lin.append(sat(v))', '        lin.append(v)', ['gselftest', 'gvectors']),
    ('gorc-n1-matrix', 'orc', GO, '        lin = [sat(v) for v in solve3(mb, xyz)]', '        lin = solve3(mb, xyz)', ['gselftest', 'gvectors']),
    ('gorc-n1-encode', 'orc', GO, '        out.append(sat(v))', '        out.append(v)', ['gselftest', 'gvectors']),
    ('gorc-n1-g4', 'orc', GO, '[sat(vals[0] / a), sat(vals[1] / a), sat(vals[2] / a), a]', '[vals[0] / a, vals[1] / a, vals[2] / a, a]', ['gselftest', 'gvectors']),
]

# Strokes story S1: the stroke style (SK1 to SK3, SK12), the item and node (SK4, SK13), the bounds (SK5, SK10),
# what draws nothing (SK8, SK9), the features required and the plan's refusal (SK11, S20), and the test sink's
# records.  src/resource/stroke.c is also mutated one by one by the generator (``bk-``, tools/scene-plants.json).
SKC = "src/resource/stroke.c"
HSK = "include/ghoti.io/shape-scene/stroke.h"
HVS = "include/ghoti.io/shape-scene/version.h"
K = ["testStroke"]
KD = ["testStroke", "testDiff"]
KS = ["testStroke", "testDump_sink", "testSink"]
PLANTS += [
    # the pinned values and constants (stroke.h, list.h, version.h, resource.h, report.h, sink.h)
    ('sk1-h-value-min', 'hdr', HSK, '#define GSHSCENE_STROKE_VALUE_MIN 0x1p-64', '#define GSHSCENE_STROKE_VALUE_MIN 0x1p-63', K),
    ('sk1-h-value-max', 'hdr', HSK, '#define GSHSCENE_STROKE_VALUE_MAX 0x1p64', '#define GSHSCENE_STROKE_VALUE_MAX 0x1p63', K),
    ('sk1-h-miter-max', 'hdr', HSK, '#define GSHSCENE_STROKE_MITER_LIMIT_MAX 0x1p16', '#define GSHSCENE_STROKE_MITER_LIMIT_MAX 0x1p15', K),
    ('sk1-h-miter-default', 'hdr', HSK, '#define GSHSCENE_STROKE_MITER_LIMIT_DEFAULT 4.0', '#define GSHSCENE_STROKE_MITER_LIMIT_DEFAULT 10.0', K),
    ('sk1-h-join-arcs', 'hdr', HSK, '  GSHSCENE_STROKE_JOIN_ARCS = 4,', '  GSHSCENE_STROKE_JOIN_ARCS = 5,', K),
    ('sk1-h-cap-square', 'hdr', HSK, '  GSHSCENE_STROKE_CAP_SQUARE = 2,', '  GSHSCENE_STROKE_CAP_SQUARE = 3,', K),
    ('sk1-h-width-device', 'hdr', HSK, '  GSHSCENE_STROKE_WIDTH_DEVICE = 1,', '  GSHSCENE_STROKE_WIDTH_DEVICE = 2,', K),
    ('sk1-h-degenerate-omit', 'hdr', HSK, '  GSHSCENE_STROKE_DEGENERATE_OMIT = 1,', '  GSHSCENE_STROKE_DEGENERATE_OMIT = 2,', K),
    ('sk1-h-dash-path', 'hdr', HSK, '  GSHSCENE_STROKE_DASH_PATH = 1,', '  GSHSCENE_STROKE_DASH_PATH = 2,', K),
    ('sk1-h-align-outside', 'hdr', HSK, '  GSHSCENE_STROKE_ALIGN_OUTSIDE = 2,', '  GSHSCENE_STROKE_ALIGN_OUTSIDE = 3,', K),
    ('sk11-h-native-round', 'hdr', HSK, '#define GSHSCENE_STROKE_NATIVE_JOIN_ROUND (UINT32_C(1) << 1)', '#define GSHSCENE_STROKE_NATIVE_JOIN_ROUND (UINT32_C(1) << 2)', K),
    ('sk11-h-native-anisotropic', 'hdr', HSK, '#define GSHSCENE_STROKE_NATIVE_WIDTH_LOCAL_ANISOTROPIC (UINT32_C(1) << 9)', '#define GSHSCENE_STROKE_NATIVE_WIDTH_LOCAL_ANISOTROPIC (UINT32_C(1) << 8)', K),
    ('sk11-h-native-degenerate', 'hdr', HSK, '#define GSHSCENE_STROKE_NATIVE_DEGENERATE_DRAW (UINT32_C(1) << 15)', '#define GSHSCENE_STROKE_NATIVE_DEGENERATE_DRAW (UINT32_C(1) << 14)', K),
    ('sk11-h-native-all', 'hdr', HSK, '#define GSHSCENE_STROKE_NATIVE_ALL ((UINT32_C(1) << 16) - 1)', '#define GSHSCENE_STROKE_NATIVE_ALL ((UINT32_C(1) << 15) - 1)', ["testDump_sink", "testSink", "testStroke"]),
    ('sk4-h-item-kind', 'hdr', HL, '  GSHSCENE_ITEM_STROKE = 7,', '  GSHSCENE_ITEM_STROKE = 8,', ["testStroke", "testList"]),
    ('sk13-h-node-kind', 'hdr', HVS, '  GSHSCENE_NODE_STROKE = 4,', '  GSHSCENE_NODE_STROKE = 5,', ["testStroke", "testNode"]),
    ('sk1-h-resource-kind', 'hdr', HR, '  GSHSCENE_RESOURCE_STROKE_STYLE = 8,', '  GSHSCENE_RESOURCE_STROKE_STYLE = 9,', ["testStroke", "testResource"]),
    ('sk12-h-bytes-per-dash', 'hdr', HR, '#define GSHSCENE_BYTES_PER_DASH ((size_t)16)', '#define GSHSCENE_BYTES_PER_DASH ((size_t)8)', ["testStroke", "testBytes"]),
    ('sk11-h-reasons-exchanged', 'hdr', HRP, '  GSHSCENE_REASON_STROKE_STYLE = 28,    ///< Refused, per stroke: a feature of its style the sink neither emits nor expands; `second` names the missing ones (stroke.h, SK11).\n  GSHSCENE_REASON_STROKE_ZERO_WIDTH = 29, ///< Notice, per stroke: its width is 0, so it draws nothing (stroke.h, SK8).', '  GSHSCENE_REASON_STROKE_ZERO_WIDTH = 28, ///< Notice, per stroke: its width is 0, so it draws nothing (stroke.h, SK8).\n  GSHSCENE_REASON_STROKE_STYLE = 29,    ///< Refused, per stroke: a feature of its style the sink neither emits nor expands; `second` names the missing ones (stroke.h, SK11).', ["testReport", "testStroke"]),
    ('s20-h-cap-expand', 'hdr', HS, '#define GSHSCENE_CAP_STROKE_EXPAND (UINT32_C(1) << 12)', '#define GSHSCENE_CAP_STROKE_EXPAND (UINT32_C(1) << 11)', ["testSink", "testStroke"]),
    ('s20-h-cap-flags-all', 'hdr', HS, '#define GSHSCENE_CAP_FLAGS_ALL ((UINT32_C(1) << 14) - 1)', '#define GSHSCENE_CAP_FLAGS_ALL ((UINT32_C(1) << 13) - 1)', ["testDump_sink", "testSink", "testStroke"]),
    # SK1 to SK3 in the library: the format, the canonical form, the hash's order
    ('sk12-format', 'src', SKC, '#define STROKE_FORMAT_VERSION 1u', '#define STROKE_FORMAT_VERSION 2u', K),
    ('sk3-zero-sign', 'src', SKC, '  return x == 0.0 ? 0.0 : x;', '  return x;', K),
    ('sk3-odd-not-doubled', 'src', SKC, '  return in->dash_count % 2u ? gshscene_sat_mul(in->dash_count, 2u) : in->dash_count;', '  return in->dash_count;', K),
    ('sk3-miter-kept', 'src', SKC, '  out->miter_limit = join_reads_miter(in->join) ? in->miter_limit : GSHSCENE_STROKE_MITER_LIMIT_DEFAULT;', '  out->miter_limit = in->miter_limit;', K),
    ('sk3-degenerate-kept-under-butt', 'src', SKC, '  out->degenerate = in->cap == (uint32_t)GSHSCENE_STROKE_CAP_BUTT ? (uint32_t)GSHSCENE_STROKE_DEGENERATE_DRAW : in->degenerate;', '  out->degenerate = in->degenerate;', K),
    ('sk3-degenerate-reset-under-round', 'src', SKC, 'in->cap == (uint32_t)GSHSCENE_STROKE_CAP_BUTT ? (uint32_t)GSHSCENE_STROKE_DEGENERATE_DRAW : in->degenerate;', 'in->cap != (uint32_t)GSHSCENE_STROKE_CAP_SQUARE ? (uint32_t)GSHSCENE_STROKE_DEGENERATE_DRAW : in->degenerate;', K),
    ('sk3-rule-kept', 'src', SKC, '  out->alignment_rule = in->alignment == (uint32_t)GSHSCENE_STROKE_ALIGN_CENTER ? GSHGEOM_FILL_NONZERO : in->alignment_rule;', '  out->alignment_rule = in->alignment_rule;', K),
    ('sk3-offset-kept', 'src', SKC, '    out->dash_offset = 0.0;\n    out->dash_scope = GSHSCENE_STROKE_DASH_PER_SUBPATH;', '    out->dash_offset = canonical_zero(in->dash_offset);\n    out->dash_scope = in->dash_scope;', K),
    ('sk3-negative-kept', 'src', SKC, '    if (in->dashes[i] < 0.0) {\n      return 0; /* a negative length: solid */\n    }', '', K),
    ('sk3-zeros-kept', 'src', SKC, '  if (!any) {\n    return 0; /* none, or every length zero: solid */\n  }', '', K),
    ('sk2-bounds-on-canonical', 'src', SKC, '  if (!bounded(style) || count > most) {', '  if (count > most) {', K),
    ('sk2-limit-elements', 'src', SKC, '  size_t most = store->limits.max_resource_elements < GSHSCENE_STROKE_DASHES_MAX ? store->limits.max_resource_elements\n                                                                                 : GSHSCENE_STROKE_DASHES_MAX;', '  size_t most = GSHSCENE_STROKE_DASHES_MAX;', K),
    ('sk2-profile-accepted', 'src', SKC, '    return GSHSCENE_ERR_UNSUPPORTED; /* D8: reserved until a profile is defined */', '    return GSHSCENE_OK; /* D8: reserved until a profile is defined */', K),
    ('sk2-profile-first', 'src', SKC, '  if (in->width < 0.0) {\n    return GSHSCENE_ERR_INVALID;\n  }\n  if (in->width_profile) {', '  if (in->width_profile) {\n    return GSHSCENE_ERR_UNSUPPORTED;\n  }\n  if (in->width < 0.0) {\n    return GSHSCENE_ERR_INVALID;\n  }\n  if (in->width_profile) {', K),
    ('sk12-hash-order', 'src', SKC, '  gshgeom_hash_u8(&h, (uint8_t)s->cap);\n  gshgeom_hash_u8(&h, (uint8_t)s->degenerate);', '  gshgeom_hash_u8(&h, (uint8_t)s->degenerate);\n  gshgeom_hash_u8(&h, (uint8_t)s->cap);', K),
    ('sk12-hash-kind', 'src', SKC, '  gshgeom_hash_u32(&h, (uint32_t)GSHSCENE_RESOURCE_STROKE_STYLE);', '  gshgeom_hash_u32(&h, (uint32_t)GSHSCENE_RESOURCE_GRADIENT);', K),
    ('sk12-bytes-per-dash', 'src', SKC, 'gshscene_sat_mul(count, GSHSCENE_BYTES_PER_DASH)', 'gshscene_sat_mul(count, GSHSCENE_BYTES_PER_DASH / 2)', ["testStroke", "testBytes"]),
    # SK5 and SK10: the pixel sizes and the reach
    ('sk5-millimetre', 'src', SKC, '      return 25.4 / 96.0;', '      return 25.4 / 72.0;', KS),
    ('sk5-point', 'src', SKC, '      return 72.0 / 96.0;', '      return 72.0 / 72.0;', KS),
    ('sk5-inch', 'src', SKC, '      return 1.0 / 96.0;', '      return 1.0 / 72.0;', KS),
    ('sk10-miter-clip-plus-one', 'src', SKC, '    f = s->miter_limit + 1.0;', '    f = s->miter_limit;', KD),
    ('sk10-miter', 'src', SKC, '    f = s->miter_limit; /* the miter', '    f = 1.0; /* the miter', KD),
    ('sk10-square-cap', 'src', SKC, '    f = 1.5; /* a square cap', '    f = 1.0; /* a square cap', KD),
    ('sk10-half-width', 'src', SKC, '  return (s->width * 0.5) * f * a * pixel * STROKE_ROUND_UP;', '  return (s->width * 0.25) * f * a * pixel * STROKE_ROUND_UP;', KD),
    ('sk10-alignment-doubled', 'src', SKC, ' ? 1.0 : 2.0;\n  return (s->width', ' ? 1.0 : 1.0;\n  return (s->width', KD),
    ('sk10-pixel', 'src', SKC, '  return (s->width * 0.5) * f * a * pixel * STROKE_ROUND_UP;', '  return (s->width * 0.5) * f * a * (pixel > 0.0 ? 1.0 : 0.0) * STROKE_ROUND_UP;', KS),
    ('sk10-round-up', 'src', SKC, '#define STROKE_ROUND_UP (1.0 + 0x1p-50)', '#define STROKE_ROUND_UP (1.0 - 0x1p-50)', KD),
    ('sk10-add-up-nearest', 'src', SKC, '  return e > 0.0 ? next_up(s) : s;', '  return e > 0.0 ? s : s;', KD),
    ('sk10-next-up-sign', 'src', SKC, '  bits = x > 0.0 ? bits + 1u : bits - 1u;', '  bits = bits + 1u;', KD),
    ('sk10-device-before-map', 'src', SKC, '  if (!device) {\n    /* Local', '  if (1) {\n    /* Local', KS),
    # SK8, SK9, SK11: the notice's order and the features
    ('sk9-notice-order', 'src', SKC, '  if (not_invertible) {\n    return GSHSCENE_REASON_MAP_NOT_INVERTIBLE;\n  }\n  if (item->style->u.stroke.s.width == 0.0) {\n    return GSHSCENE_REASON_STROKE_ZERO_WIDTH; /* SK8 */\n  }', '  if (item->style->u.stroke.s.width == 0.0) {\n    return GSHSCENE_REASON_STROKE_ZERO_WIDTH; /* SK8 */\n  }\n  if (not_invertible) {\n    return GSHSCENE_REASON_MAP_NOT_INVERTIBLE;\n  }', K),
    ('sk9-notice-gradient-map', 'src', SKC, '      (gradient && gshscene_map_singular(&gradient->u.gradient.g.map));', '      0;', K),
    ('sk9-notice-instances', 'src', SKC, '    not_invertible = gshscene_map_singular(&instance_maps[i]);', '    (void)instance_maps;', K),
    ('sk11-joins-swapped', 'src', SKC, '{GSHSCENE_STROKE_NATIVE_JOIN_MITER,\n      GSHSCENE_STROKE_NATIVE_JOIN_ROUND,', '{GSHSCENE_STROKE_NATIVE_JOIN_ROUND,\n      GSHSCENE_STROKE_NATIVE_JOIN_MITER,', K),
    ('sk11-caps-swapped', 'src', SKC, '{GSHSCENE_STROKE_NATIVE_CAP_BUTT,\n      GSHSCENE_STROKE_NATIVE_CAP_ROUND,', '{GSHSCENE_STROKE_NATIVE_CAP_ROUND,\n      GSHSCENE_STROKE_NATIVE_CAP_BUTT,', K),
    ('sk11-reflection-anisotropic', 'src', SKC, '  return (a == d && b == -c) || (a == -d && b == c);', '  return a == d && b == -c;', K),
    ('sk11-target-ignored', 'src', SKC, '  if (r == GSHGEOM_OK && target_map && target_map->dim != 0) {', '  if (0) {', K),
    ('sk11-instances-ignored', 'src', SKC, '  GSHGEOM_Map acc = instance_count ? instance_maps[0] : *item_map;', '  GSHGEOM_Map acc = *item_map;\n  instance_count = 0;', K),
    ('sk11-degenerate-butt', 'src', SKC, ' && s->cap != (uint32_t)GSHSCENE_STROKE_CAP_BUTT) {', ') {', K),
    # S20 in the planning pass and the test sink
    ('rule-S20-expand-ignored', 'src', SK, '  if (item->kind == GSHSCENE_ITEM_STROKE && !(caps->flags & GSHSCENE_CAP_STROKE_EXPAND)) {', '  if (item->kind == GSHSCENE_ITEM_STROKE) {', KS),
    ('rule-S20-native-ignored', 'src', SK, '        gshscene_stroke_required(item, instance_maps, instance_count, target_map) & ~caps->stroke_native;', '        gshscene_stroke_required(item, instance_maps, instance_count, target_map);', KS),
    ('rule-S20-no-chain', 'src', SK, '        gshscene_stroke_required(item, instance_maps, instance_count, target_map) & ~caps->stroke_native;', '        gshscene_stroke_required(item, instance_maps, instance_count & 0u, target_map) & ~caps->stroke_native;', KS),
    ('rule-S20-missing-unnamed', 'src', SK, '      d.detail = missing;', '      d.detail = 1;', KS),
    ('rule-S20-reason', 'src', SK, '      d.reason = GSHSCENE_REASON_STROKE_STYLE;', '      d.reason = GSHSCENE_REASON_ITEM_KIND;', KS),
    ('rule-S20-paint-unchecked', 'src', SK, '  int painted = item->kind == GSHSCENE_ITEM_FILL || item->kind == GSHSCENE_ITEM_STROKE;', '  int painted = item->kind == GSHSCENE_ITEM_FILL;', KS + ["testGradient", "testPalette"]),
    ('rule-S20-notice-as-gradient', 'src', SK, '    GSHSCENE_Reason notice = item->kind == GSHSCENE_ITEM_STROKE\n', '    GSHSCENE_Reason notice = 0\n', KS),
    ('rule-S20-detail-dropped', 'src', SK, 'chain, count, (size_t)d.detail, 0.0);', 'chain, count, 0, 0.0);', KS),
    ('rule-S20-caps-unchecked', 'src', SK, ' || (c.stroke_native & ~GSHSCENE_STROKE_NATIVE_ALL)', '', ["testSink", "testStroke"]),
    ('ds-s20-expand-all', 'src', DS, '      gshscene_stroke_required(item, run->maps, count - 1, &run->job->target.map) & ~run->job->caps.stroke_native;', '      gshscene_stroke_required(item, run->maps, count - 1, &run->job->target.map);', ["testDump_sink"]),
    ('ds-s20-style-every-time', 'src', DS, '    if (run->styles[i] == id) {\n      return GSHSCENE_OK;', '    if (0 && run->styles[i] == id) {\n      return GSHSCENE_OK;', ["testDump_sink"]),
    ('ds-s20-native-default', 'src', DS, '  caps->stroke_native = GSHSCENE_STROKE_NATIVE_ALL; /* every stroke feature', '  caps->stroke_native = 0; /* every stroke feature', ["testDump_sink", "testStroke"]),
    ('ds-s20-no-notice', 'src', DS, '  GSHSCENE_Reason notice = gshscene_stroke_notice(item, run->maps, count - 1, &run->job->target.map);', '  GSHSCENE_Reason notice = (GSHSCENE_Reason)0;', ["testDump_sink"]),
    ('ds-s20-unit', 'src', DS, '&map, run->job->target.unit, &b) != GSHSCENE_OK;', '&map, GSHSCENE_UNIT_PIXEL, &b) != GSHSCENE_OK;', ["testDump_sink"]),

    # strokes story S4: SK9's composed map (E6), the conversion to shape-geometry's parameters row by row (SC2) and the
    # refusal of a stroked run (F5, S20)
    ('s20-h-cap-stroke-runs', 'hdr', HS, '#define GSHSCENE_CAP_STROKE_RUNS (UINT32_C(1) << 13)', '#define GSHSCENE_CAP_STROKE_RUNS (UINT32_C(1) << 12)', ["testSink", "testStroke"]),
    ('s20-h-reason-centerline', 'hdr', HRP, '  GSHSCENE_REASON_STROKE_CENTERLINE = 30,', '  GSHSCENE_REASON_STROKE_CENTERLINE = 29,', ["testReport", "testStroke"]),
    ('s20-centerline-reason-name', 'src', RP, '      return "stroke-centerline";', '      return "stroke-centerlines";', ["testReport"]),
    ('s20-centerline-no-refusal', 'src', SK, '  if (item->kind == GSHSCENE_ITEM_STROKE && !(caps->flags & GSHSCENE_CAP_STROKE_RUNS) && item->resource &&', '  if (0 && item->kind == GSHSCENE_ITEM_STROKE && !(caps->flags & GSHSCENE_CAP_STROKE_RUNS) && item->resource &&', K),
    ('s20-centerline-flag-ignored', 'src', SK, '  if (item->kind == GSHSCENE_ITEM_STROKE && !(caps->flags & GSHSCENE_CAP_STROKE_RUNS) && item->resource &&', '  if (item->kind == GSHSCENE_ITEM_STROKE && item->resource &&', K),
    ('s20-centerline-path-refused', 'src', SK, '      item->resource->kind == GSHSCENE_RESOURCE_GLYPH_RUN &&\n      !gshscene_stroke_notice(', '      item->resource->kind != GSHSCENE_RESOURCE_GLYPH_RUN &&\n      !gshscene_stroke_notice(', K),
    ('s20-centerline-nothing-refused', 'src', SK, '      item->resource->kind == GSHSCENE_RESOURCE_GLYPH_RUN &&\n      !gshscene_stroke_notice(item, instance_maps, instance_count, target_map)) {', '      item->resource->kind == GSHSCENE_RESOURCE_GLYPH_RUN) {', K),
    ('s20-centerline-reason', 'src', SK, '    d.reason = GSHSCENE_REASON_STROKE_CENTERLINE;', '    d.reason = GSHSCENE_REASON_STROKE_STYLE;', K),
    ('s20-centerline-class', 'src', SK, '    d.entry_class = GSHSCENE_ENTRY_REFUSED;\n    d.reason = GSHSCENE_REASON_STROKE_CENTERLINE;', '    d.entry_class = GSHSCENE_ENTRY_APPROXIMATED;\n    d.reason = GSHSCENE_REASON_STROKE_CENTERLINE;', K),
    ('ds-s20-default-stroke-runs', 'src', DS, '      GSHSCENE_CAP_STROKE_RUNS |', '      0 |', ["testDump_sink"]),
    ('sk9-composed-ignored', 'src', SKC, '  if (!not_invertible) {\n    GSHGEOM_Map composed;', '  if (0) {\n    GSHGEOM_Map composed;', K),
    ('sk9-composed-sign', 'src', SKC, 'gshgeom_stroke_map_invertible(&composed, &invertible) == GSHGEOM_OK && !invertible;', 'gshgeom_stroke_map_invertible(&composed, &invertible) == GSHGEOM_OK && invertible;', K),
    ('sk9-composed-unformable', 'src', SKC, '    not_invertible = compose_item(instance_maps, instance_count, &item->map, target_map, &composed) == GSHGEOM_OK &&', '    not_invertible = compose_item(instance_maps, instance_count, &item->map, target_map, &composed) != GSHGEOM_OK ||', K),
    ('sk9-composed-without-target', 'src', SKC, '    not_invertible = compose_item(instance_maps, instance_count, &item->map, target_map, &composed) == GSHGEOM_OK &&', '    not_invertible = compose_item(instance_maps, instance_count, &item->map, NULL, &composed) == GSHGEOM_OK &&', K),
    ('sk9-composed-without-instances', 'src', SKC, '    not_invertible = compose_item(instance_maps, instance_count, &item->map, target_map, &composed) == GSHGEOM_OK &&', '    not_invertible = compose_item(instance_maps, 0, &item->map, target_map, &composed) == GSHGEOM_OK &&', K),
    ('sc2-width', 'src', SKC, '  g.width = s->width;', '  g.width = 1.0;', K),
    ('sc2-width-space', 'src', SKC, '  g.width_space = s->width_space;', '  g.width_space = 0;', K),
    ('sc2-join', 'src', SKC, '  g.join = s->join;', '  g.join = 0;', K),
    ('sc2-miter-limit', 'src', SKC, '  g.miter_limit = s->miter_limit;', '  g.miter_limit = 4.0;', K),
    ('sc2-cap', 'src', SKC, '  g.cap = s->cap;', '  g.cap = 0;', K),
    ('sc2-degenerate', 'src', SKC, '  g.degenerate = s->degenerate;', '  g.degenerate = 0;', K),
    ('sc2-dash-count', 'src', SKC, '  g.dash_count = s->dash_count;', '  g.dash_count = s->dash_count / 2;', K),
    ('sc2-dashes', 'src', SKC, '  g.dashes = s->dashes; /* into the resource: valid while a handle is held (S10) */', '  g.dashes = s->dash_count ? s->dashes + 1 : NULL;', K),
    ('sc2-dash-offset', 'src', SKC, '  g.dash_offset = s->dash_offset;', '  g.dash_offset = 0.0;', K),
    ('sc2-dash-scope', 'src', SKC, '  g.dash_scope = s->dash_scope;', '  g.dash_scope = 0;', K),
    ('sc2-alignment', 'src', SKC, '  g.alignment = s->alignment;', '  g.alignment = 0;', K),
    ('sc2-alignment-rule', 'src', SKC, '  g.alignment_rule = s->alignment_rule;', '  g.alignment_rule = GSHGEOM_FILL_NONZERO;', K),
    ('sc2-check-dropped', 'src', SKC, '  if (gshgeom_stroke_check(&g) != GSHGEOM_OK) {', '  if (0) {', K),
    # strokes story S4 (SC10): the test sink's outline= field
    ('ds-s20-outline-pixel-scale', 'src', DS, '        gshgeom_map_scale(2, 1.0 / size, 1.0 / size, 1.0, &scale) != GSHGEOM_OK ||', '        gshgeom_map_scale(2, size, size, 1.0, &scale) != GSHGEOM_OK ||', ["testDump_sink"]),
    ('ds-s20-outline-not-canonical', 'src', DS, '  if (gshgeom_stroke_expand(path, &params, &canon, &run->glyphs.self, NULL, &outline) != GSHGEOM_OK) {', '  if (gshgeom_stroke_expand(path, &params, &pixels, &run->glyphs.self, NULL, &outline) != GSHGEOM_OK) {', ["testDump_sink"]),
    ('ds-s20-outline-unmapped', 'src', DS, '  GSHGEOM_Map pixels = *map;', '  GSHGEOM_Map pixels;\n  gshgeom_map_identity(2, &pixels);', ["testDump_sink"]),
    ('ds-s20-outline-limit-ignored', 'src', DS, '  if (gshgeom_stroke_expand(path, &params, &canon, &run->glyphs.self, NULL, &outline) != GSHGEOM_OK) {\n    return GSHSCENE_ERR_LIMIT;', '  if (gshgeom_stroke_expand(path, &params, &canon, &run->glyphs.self, NULL, &outline) != GSHGEOM_OK) {\n    return GSHSCENE_ERR_INTERNAL;', ["testDump_sink"]),
    ('ds-s20-outline-run-spelled', 'src', DS, '    snprintf(outline_text, sizeof outline_text, "-");', '    snprintf(outline_text, sizeof outline_text, "0");', ["testDump_sink"]),
    ('ds-s20-outline-hash-not-written', 'src', DS, '    snprintf(outline_text, sizeof outline_text, "%016" PRIx64, outline_hash);', '    snprintf(outline_text, sizeof outline_text, "%016" PRIx64, (uint64_t)0);', ["testDump_sink"]),
    ('sc2-status-of-bad-style', 'src', SKC, '    return GSHSCENE_ERR_INTERNAL; /* SK2 equals SG2: a style the store holds passes */', '    return GSHSCENE_ERR_INVALID;', K),
]

# color-paint story P4 (PL1 to PL10): the plane, the design-space rectangle clip and the group in a stated space.
# Written with the story; the sweep is deferred to the consolidated pass (the strokes plan's deferred-verification).
PLN = ["testPlane"]

PLANTS += [
    ('pl1-h-kind', 'hdr', HR, '  GSHSCENE_RESOURCE_PLANE = 9,', '  GSHSCENE_RESOURCE_PLANE = 10,', ["testPlane", "testResource"]),
    ('pl1-hash-kind', 'src', RS, '  hash_begin(&h, GSHSCENE_RESOURCE_PLANE);\n  return gshgeom_hash_value(&h);', '  hash_begin(&h, GSHSCENE_RESOURCE_PICTURE);\n  return gshgeom_hash_value(&h);', PLN),
    ('pl1-two-planes', 'src', RS, '      return 1; /* no content: any plane of the store is this one */', '      return 0; /* no content: any plane of the store is this one */', PLN),
    ('pl1-bytes', 'src', RS, '  GSHSCENE_Probe probe = {GSHSCENE_RESOURCE_PLANE, plane_content_hash(), GSHSCENE_BYTES_BASE, NULL, build_plane};', '  GSHSCENE_Probe probe = {GSHSCENE_RESOURCE_PLANE, plane_content_hash(), 0, NULL, build_plane};', PLN),
    ('pl3-clip-takes-plane', 'src', LS, '''  GSHSCENE_Result result = check_resource(list, resource, TAKES_PATH | TAKES_RUN);
  if (result == GSHSCENE_OK) {
    result = canonical_map(map, &item.map);
  }
  if (result == GSHSCENE_OK) {
    result = check_rule(rule);
  }
  if (result == GSHSCENE_OK) {
    result = gshscene_list_canonical_depth(depth, &item.depth);
  }
  if (result == GSHSCENE_OK) {
    result = stack_reserve(list);
  }''', '''  GSHSCENE_Result result = check_resource(list, resource, TAKES_PATH | TAKES_RUN | TAKES_PLANE);
  if (result == GSHSCENE_OK) {
    result = canonical_map(map, &item.map);
  }
  if (result == GSHSCENE_OK) {
    result = check_rule(rule);
  }
  if (result == GSHSCENE_OK) {
    result = gshscene_list_canonical_depth(depth, &item.depth);
  }
  if (result == GSHSCENE_OK) {
    result = stack_reserve(list);
  }''', PLN),
    ('pl3-fill-refuses-plane', 'src', LS, '  GSHSCENE_Result result = check_resource(list, resource, TAKES_PATH | TAKES_RUN | TAKES_PLANE);', '  GSHSCENE_Result result = check_resource(list, resource, TAKES_PATH | TAKES_RUN);', PLN),
    ('pl2-3d-plane-map', 'src', LS, '  if (result == GSHSCENE_OK && resource->kind == GSHSCENE_RESOURCE_PLANE && item.map.dim != 2) {', '  if (result == GSHSCENE_OK && resource->kind == GSHSCENE_RESOURCE_PLANE && item.map.dim != 3) {', PLN),
    ('pl5-node-takes-plane', 'src', "src/scene/node.c", '  if (resource->kind != GSHSCENE_RESOURCE_PATH && resource->kind != GSHSCENE_RESOURCE_GLYPH_RUN) {', '  if (resource->kind != GSHSCENE_RESOURCE_PATH && resource->kind != GSHSCENE_RESOURCE_GLYPH_RUN && resource->kind != GSHSCENE_RESOURCE_PLANE) {', PLN),
    ('pl6-sink-box-empty', 'src', DS, '        emit_text(run, "*\\n", &failed); /* PL6: unbounded, not empty */', '        emit_text(run, "-\\n", &failed); /* PL6: unbounded, not empty */', PLN),
    ('pl6-sink-extent-applies', 'src', DS, '        limit = 0.0;\n      }', '      }', PLN),
    ('pl6-sink-stamps-nothing', 'src', DS, '      GSHSCENE_Rect r = plane ? run->area : (box.empty', '      GSHSCENE_Rect r = plane ? (GSHSCENE_Rect){0, 0, 0, 0} : (box.empty', PLN),
    ('pl4-fixed-plane-needs-no-frame', 'src', FX, '      (op_count && !frame)) {', '      0) {', PLN),
    ('pl-fixed-plane-wrong-kind', 'src', FX, '|| plane->kind != GSHSCENE_RESOURCE_PLANE ||', '||', PLN),
    ('pl-rect-space', 'src', FX, '    checked = from_geometry(gshgeom_map_compose_chain(chain, 2, NULL, out));', '    checked = from_geometry(gshgeom_map_compose_chain(chain + 1, 1, NULL, out));', PLN + ["testPlaceholder"]),
    ('pl-rect-width', 'src', FX, '    g = gshgeom_path_line_to_fixed(path, x1, y1, bits);', '    g = gshgeom_path_line_to_fixed(path, x0, y1, bits);', PLN),
    ('pl-rect-rounds', 'src', FX, '    g = gshgeom_path_move_to_fixed(path, x0, y0, bits);', '    g = gshgeom_path_move_to_fixed(path, x0 / 2 * 2, y0, bits);', PLN),
    ('pl-rect-inverted', 'src', FX, ' || x0 > x1 ||\n      y0 > y1) {', ' ||\n      0) {', PLN),
    ('pl-space-dropped', 'src', FX, '  group.blend_space.primaries = (GCOL_Primaries)primaries;\n  group.blend_space.transfer = (GCOL_Transfer)transfer;', '', PLN),
    ('pl-space-transfer-gamma', 'src', FX, '(transfer != (uint32_t)GCOL_TRANSFER_LINEAR && transfer != (uint32_t)GCOL_TRANSFER_SRGB)', '(transfer > (uint32_t)GCOL_TRANSFER_GAMMA)', PLN),
]

# The generated plants (tools/gen-scene-plants.py): the mutations of src/scene
# the sweep was seen to catch.  PLANT_EXTRA_JSON names another file to run
# in addition, which is how a regenerated candidate list is triaged.
import json
for _file in [str(ROOT / "tools" / "scene-plants.json"), str(ROOT / "tools" / "sink-plants.json")] + (
        [os.environ["PLANT_EXTRA_JSON"]] if os.environ.get("PLANT_EXTRA_JSON") else []):
    with open(_file) as _f:
        PLANTS += [tuple(x) for x in json.load(_f)]



# color-paint story P5 (PH1 to PH8): the placeholder item and node, the planning pass's entry for it, the test sink's
# record.  Written with the story; the sweep is deferred to the consolidated pass (the strokes plan's
# deferred-verification).  Every needle was checked to occur once; the shape-cpu half is in its own check-planted.py.
PH = ["testPlaceholder"]
PHS = ["testPlaceholder", "testDump_sink", "testSink"]
HVP = "include/ghoti.io/shape-scene/version.h"
PLANTS += [
    # the pinned values (list.h, version.h, report.h)
    ('ph1-h-item-kind', 'hdr', HL, '  GSHSCENE_ITEM_PLACEHOLDER = 8,', '  GSHSCENE_ITEM_PLACEHOLDER = 9,', PH + ["testStroke"]),
    ('ph7-h-node-kind', 'hdr', HVP, '  GSHSCENE_NODE_PLACEHOLDER = 5,', '  GSHSCENE_NODE_PLACEHOLDER = 6,', PH + ["testNode"]),
    ('ph4-h-reason', 'hdr', HRP, '  GSHSCENE_REASON_PLACEHOLDER = 31,', '  GSHSCENE_REASON_PLACEHOLDER = 32,', PH + ["testReport"]),
    ('ph4-h-source-colr', 'hdr', HRP, '#define GSHSCENE_PLACEHOLDER_SOURCE_COLR 1u', '#define GSHSCENE_PLACEHOLDER_SOURCE_COLR 2u', ["testReport"]),
    ('ph4-h-code-unbounded', 'hdr', HRP, '#define GSHSCENE_PLACEHOLDER_COLR_UNBOUNDED 1u', '#define GSHSCENE_PLACEHOLDER_COLR_UNBOUNDED 2u', ["testReport"]),
    ('ph4-h-code-walk-limit', 'hdr', HRP, '#define GSHSCENE_PLACEHOLDER_COLR_WALK_LIMIT 3u', '#define GSHSCENE_PLACEHOLDER_COLR_WALK_LIMIT 4u', ["testReport"]),
    ('ph4-reason-name', 'src', RP, '      return "placeholder";', '      return "placeholders";', ["testReport"]),
    ('ph4-pair-name', 'src', RP, '        return "colr:no-record";', '        return "colr:no_record";', ["testReport"]),
    # PH1, PH2: the record call
    ('ph2-source-zero', 'src', LS, '  if (source == 0 || code == 0 || approximated > 1u) {', '  if (code == 0 || approximated > 1u) {', PH),
    ('ph2-code-zero', 'src', LS, '  if (source == 0 || code == 0 || approximated > 1u) {', '  if (source == 0 || approximated > 1u) {', PH),
    ('ph2-approximated-range', 'src', LS, '  if (source == 0 || code == 0 || approximated > 1u) {', '  if (source == 0 || code == 0) {', PH),
    ('ph2-box-finite', 'src', LS, '      if (!gshscene_finite(box[i])) {', '      if (0) {', PH),
    ('ph2-box-inverted-x', 'src', LS, '    if (box[0] > box[2] || box[1] > box[3]) {', '    if (box[1] > box[3]) {', PH),
    ('ph2-box-inverted-y', 'src', LS, '    if (box[0] > box[2] || box[1] > box[3]) {', '    if (box[0] > box[2]) {', PH),
    ('ph2-box-negative-zero', 'src', LS, '      out->box[i] = box[i] == 0.0 ? 0.0 : box[i]; /* -0 is stored as +0, as a depth is */', '      out->box[i] = box[i];', PH),
    ('ph2-box-no-flag', 'src', LS, '    out->has_box = 1;', '    out->has_box = 0;', PH),
    ('ph2-planar', 'src', LS, '  if (result == GSHSCENE_OK && item.map.dim != 2) {\n    result = GSHSCENE_ERR_UNSUPPORTED; /* PH1: a placeholder is planar, as a stroke is */', '  if (0) {\n    result = GSHSCENE_ERR_UNSUPPORTED; /* PH1: a placeholder is planar, as a stroke is */', PH),
    ('ph2-layer-dropped', 'src', LS, '  item.layer = layer;\n  return list_append(list, &item);\n}\n\nGSHSCENE_Result gshscene_list_clip_push(', '  return list_append(list, &item);\n}\n\nGSHSCENE_Result gshscene_list_clip_push(', PH),
    # PH1: equality, hash and dump
    ('ph1-equal-source', 'src', LS, '  if (a->placeholder.source != b->placeholder.source || a->placeholder.code != b->placeholder.code ||', '  if (a->placeholder.code != b->placeholder.code ||', PH),
    ('ph1-equal-code', 'src', LS, '  if (a->placeholder.source != b->placeholder.source || a->placeholder.code != b->placeholder.code ||', '  if (a->placeholder.source != b->placeholder.source ||', PH),
    ('ph1-equal-approximated', 'src', LS, '      a->placeholder.approximated != b->placeholder.approximated || a->placeholder.has_box != b->placeholder.has_box) {\n    return 0;\n  }\n  for (int i = 0; i < 4; i++) {\n    if (gshgeom_f64_bits(a->placeholder.box[i])', '      a->placeholder.has_box != b->placeholder.has_box) {\n    return 0;\n  }\n  for (int i = 0; i < 4; i++) {\n    if (gshgeom_f64_bits(a->placeholder.box[i])', PH),
    ('ph1-equal-box', 'src', LS, '  for (int i = 0; i < 4; i++) {\n    if (gshgeom_f64_bits(a->placeholder.box[i])', '  for (int i = 0; i < 3; i++) {\n    if (gshgeom_f64_bits(a->placeholder.box[i])', PH),
    ('ph1-hash-source', 'src', LS, '  gshgeom_hash_u16(h, p->source);', '  gshgeom_hash_u16(h, p->code);', PH),
    ('ph1-hash-code', 'src', LS, '  gshgeom_hash_u16(h, p->code);', '  gshgeom_hash_u16(h, p->source);', PH),
    ('ph1-hash-approximated', 'src', LS, '  gshgeom_hash_u8(h, (uint8_t)p->approximated);\n', '', PH),
    ('ph1-hash-has-box', 'src', LS, '  gshgeom_hash_u8(h, (uint8_t)p->has_box);\n', '', PH),
    ('ph1-hash-box', 'src', LS, '    for (int k = 0; k < 4; k++) {\n      gshgeom_hash_f64(h, p->box[k]);', '    for (int k = 0; k < 3; k++) {\n      gshgeom_hash_f64(h, p->box[k]);', PH),
    ('ph1-hash-map', 'src', LS, '        gshscene_placeholder_hash(&h, &item->placeholder);\n        gshgeom_hash_u64(&h, gshgeom_map_hash(&item->map));', '        gshscene_placeholder_hash(&h, &item->placeholder);', PH),
    ('ph1-hash-layer', 'src', LS, '        gshscene_placeholder_hash(&h, &item->placeholder);\n        gshgeom_hash_u64(&h, gshgeom_map_hash(&item->map));\n        gshgeom_hash_i32(&h, item->layer);', '        gshscene_placeholder_hash(&h, &item->placeholder);\n        gshgeom_hash_u64(&h, gshgeom_map_hash(&item->map));', PH),
    ('ph1-dump-letter', 'src', LS, "        written = fputc('P', out) == EOF ? -1 : gshscene_placeholder_dump(out, &item->placeholder);", "        written = fputc('p', out) == EOF ? -1 : gshscene_placeholder_dump(out, &item->placeholder);", PH),
    ('ph1-dump-approx-name', 'src', LS, '" source=%u code=%u approx=%u box="', '" source=%u code=%u approximated=%u box="', PH),
    ('ph1-dump-no-box', 'src', LS, "    return fputc('-', out) == EOF ? -1 : 0;\n  }\n  return fprintf(out, \"%016llx %016llx %016llx %016llx\", HEX(gshgeom_f64_bits(p->box[0]))", "    return fputc('0', out) == EOF ? -1 : 0;\n  }\n  return fprintf(out, \"%016llx %016llx %016llx %016llx\", HEX(gshgeom_f64_bits(p->box[0]))", PH),
    ('ph2-fixed-bits-unchecked', 'src', FX, '(box_design && bits > GSHSCENE_FIXED_MAX_BITS)) {', '0) {', PH),
    ('ph2-fixed-box-scaled', 'src', FX, '      box[i] = fixed_value(box_design[i], bits);', '      box[i] = (double)box_design[i];', PH),
    ('ph2-fixed-ops-without-frame', 'src', FX, '(!frame && op_count != 0) ||', '', PH),
    ('ph2-fixed-no-frame-map', 'src', FX, '  GSHSCENE_Result r = frame ? frame_map(frame, ops, op_count, &map) : from_geometry(gshgeom_map_identity(2, &map));', '  GSHSCENE_Result r = from_geometry(gshgeom_map_identity(2, &map));', PH),
    # PH3 to PH6: the planning pass and the test sink
    ('ph3-class-swapped', 'src', SK, '    d.entry_class = item->placeholder.approximated ? GSHSCENE_ENTRY_APPROXIMATED : GSHSCENE_ENTRY_REFUSED;', '    d.entry_class = item->placeholder.approximated ? GSHSCENE_ENTRY_REFUSED : GSHSCENE_ENTRY_APPROXIMATED;', PHS),
    ('ph3-decided-from-item-kinds', 'src', SK, '  if (item->kind == GSHSCENE_ITEM_PLACEHOLDER) {\n    d.entry_class', '  if (item->kind == GSHSCENE_ITEM_PLACEHOLDER && (caps->item_kinds & (UINT32_C(1) << GSHSCENE_ITEM_PLACEHOLDER))) {\n    d.entry_class', PHS),
    ('ph3-decided-after-the-caps', 'src', SK, '  if (item->kind == GSHSCENE_ITEM_PLACEHOLDER) {\n    d.entry_class', '  if (item->kind == GSHSCENE_ITEM_PLACEHOLDER && caps->flags != 0) {\n    d.entry_class', PHS),
    ('ph4-reason', 'src', SK, '    d.reason = GSHSCENE_REASON_PLACEHOLDER;', '    d.reason = GSHSCENE_REASON_ITEM_KIND;', PHS),
    ('ph4-second-dropped', 'src', SK, '    d.detail = ((uint64_t)item->placeholder.source << 16) | (uint64_t)item->placeholder.code; /* PH4 */', '    d.detail = 0;', PHS),
    ('ph4-second-shift', 'src', SK, '    d.detail = ((uint64_t)item->placeholder.source << 16) | (uint64_t)item->placeholder.code; /* PH4 */', '    d.detail = ((uint64_t)item->placeholder.source << 8) | (uint64_t)item->placeholder.code; /* PH4 */', PHS),
    ('ph4-second-code-only', 'src', SK, '    d.detail = ((uint64_t)item->placeholder.source << 16) | (uint64_t)item->placeholder.code; /* PH4 */', '    d.detail = (uint64_t)item->placeholder.code;', PHS),
    ('ph6-entry-skipped', 'src', SK, '    if (d.entry_class) {\n      /* A stroke refused for its style', '    if (d.entry_class && d.reason != GSHSCENE_REASON_PLACEHOLDER) {\n      /* A stroke refused for its style', PHS),
    ('ph6-entry-only-under-strict', 'src', SK, '    if (d.entry_class) {\n      /* A stroke refused for its style', '    if (d.entry_class && (d.reason != GSHSCENE_REASON_PLACEHOLDER || (job->request.flags & GSHSCENE_RENDER_STRICT))) {\n      /* A stroke refused for its style', PHS),
    ('ph6-entry-keyed-on-damage', 'src', SK, '    if (d.entry_class) {\n      /* A stroke refused for its style', '    if (d.entry_class && (d.reason != GSHSCENE_REASON_PLACEHOLDER || !job->target.damage)) {\n      /* A stroke refused for its style', PHS),
    ('ph5-refusal-not-counted', 'src', SK, '    if (d.entry_class == GSHSCENE_ENTRY_REFUSED) {\n      (*refused)++;', '    if (d.entry_class == GSHSCENE_ENTRY_REFUSED && d.reason != GSHSCENE_REASON_PLACEHOLDER) {\n      (*refused)++;', PHS),
    ('ph5-approximated-counted', 'src', SK, '    if (d.entry_class == GSHSCENE_ENTRY_REFUSED) {\n      (*refused)++;', '    if (d.entry_class == GSHSCENE_ENTRY_REFUSED || d.reason == GSHSCENE_REASON_PLACEHOLDER) {\n      (*refused)++;', PHS),
    ('ph8-test-sink-record-letter', 'src', DS, '  snprintf(run->line, LINE_BYTES, " P class=%d reason=%d source=%u code=%u approx=%u map=", (int)d.entry_class, (int)d.reason,', '  snprintf(run->line, LINE_BYTES, " Q class=%d reason=%d source=%u code=%u approx=%u map=", (int)d.entry_class, (int)d.reason,', ["testDump_sink"]),
    ('ph8-test-sink-source-code-swapped', 'src', DS, '      (unsigned)p->source, (unsigned)p->code, (unsigned)p->approximated);', '      (unsigned)p->code, (unsigned)p->source, (unsigned)p->approximated);', ["testDump_sink"]),
    ('ph8-test-sink-box-edge', 'src', DS, '  model.x1 = p->box[2];', '  model.x1 = p->box[0];', ["testDump_sink"]),
    ('ph8-test-sink-class-from-flag', 'src', DS, '  snprintf(run->line, LINE_BYTES, " P class=%d reason=%d source=%u code=%u approx=%u map=", (int)d.entry_class, (int)d.reason,', '  snprintf(run->line, LINE_BYTES, " P class=%d reason=%d source=%u code=%u approx=%u map=", (int)GSHSCENE_ENTRY_REFUSED, (int)d.reason,', ["testDump_sink"]),
    ('ph8-test-sink-stamps', 'src', DS, '    return placeholder_unit(run, item, d, count, failed); /* PH6: the plan wrote the entry; this is the record */', '    placeholder_unit(run, item, d, count, failed);\n    run->block_count += 0;\n    draw_rect(run, run->area, mark_of(chain, count), &failed);\n    return failed;', ["testDump_sink"]),
    # PH7: the node
    ('ph7-node-canonical-box', 'src', ND, '      if (result == GSHSCENE_OK && !in->placeholder.has_box) {', '      if (0) {', PH),
    ('ph7-node-equal-source', 'src', ND, '      if (a->placeholder.source != b->placeholder.source || a->placeholder.code != b->placeholder.code ||\n          a->placeholder.approximated', '      if (a->placeholder.code != b->placeholder.code ||\n          a->placeholder.approximated', PH),
    ('ph7-node-equal-approximated', 'src', ND, '          a->placeholder.approximated != b->placeholder.approximated || a->placeholder.has_box != b->placeholder.has_box) {\n        return 0;\n      }\n      for (int i = 0; i < 4; i++) {', '          a->placeholder.has_box != b->placeholder.has_box) {\n        return 0;\n      }\n      for (int i = 0; i < 4; i++) {', PH),
    ('ph7-node-equal-box', 'src', ND, '      for (int i = 0; i < 4; i++) {\n        if (gshgeom_f64_bits(a->placeholder.box[i]) != gshgeom_f64_bits(b->placeholder.box[i])) {', '      for (int i = 0; i < 3; i++) {\n        if (gshgeom_f64_bits(a->placeholder.box[i]) != gshgeom_f64_bits(b->placeholder.box[i])) {', PH),
    ('ph7-node-hash-fields', 'src', ND, '      gshscene_placeholder_hash(h, &p->placeholder);\n      gshgeom_hash_u64(h, gshgeom_map_hash(&p->map));', '      gshgeom_hash_u64(h, gshgeom_map_hash(&p->map));', PH),
    ('ph7-node-hash-depth', 'src', ND, '      gshscene_placeholder_hash(h, &p->placeholder);\n      gshgeom_hash_u64(h, gshgeom_map_hash(&p->map));\n      gshgeom_hash_i32(h, p->layer);\n      gshgeom_hash_f64(h, p->depth);', '      gshscene_placeholder_hash(h, &p->placeholder);\n      gshgeom_hash_u64(h, gshgeom_map_hash(&p->map));\n      gshgeom_hash_i32(h, p->layer);', PH),
    ('ph7-node-kind-name', 'src', ND, '      return "placeholder";', '      return "placeholders";', ["testNode"]),
    ('ph7-emit-box', 'src', EM, '        ph->has_box ? ph->box : NULL, map, shape->p.layer, shape->p.depth);', '        NULL, map, shape->p.layer, shape->p.depth);', PH),
    ('ph7-emit-uncomposed', 'src', EM, '        ph->has_box ? ph->box : NULL, map, shape->p.layer, shape->p.depth);', '        ph->has_box ? ph->box : NULL, NULL, shape->p.layer, shape->p.depth);', PH),
    ('ph7-emit-approximated', 'src', EM, '    GSHSCENE_Result placed = gshscene_list_placeholder(s->list, ph->source, ph->code, ph->approximated,', '    GSHSCENE_Result placed = gshscene_list_placeholder(s->list, ph->source, ph->code, 0,', PH),
    ('ph7-emit-origin', 'src', EM, '    return placed == GSHSCENE_OK ? sink_origin(s, shape->id) : placed;', '    return placed;', PH),
    ('ph7-version-dump-letter', 'src', VS, "      if (put_common(out, 'P', node) < 0 || gshscene_placeholder_dump(out, &p->placeholder) < 0 ||", "      if (put_common(out, 'Q', node) < 0 || gshscene_placeholder_dump(out, &p->placeholder) < 0 ||", PH),
    ('ph7-damage-draws-nothing', 'src', DF, '  if (shape->p.kind == GSHSCENE_NODE_PLACEHOLDER) {\n    return GSHSCENE_OK; /* PH7', '  if (0) {\n    return GSHSCENE_OK; /* PH7', PH),
]


def run(cmd, shell=False, env_extra=None, cwd=None, timeout=None, limits=None):
    environment = dict(os.environ)
    if env_extra:
        environment.update(env_extra)
    try:
        return subprocess.run(cmd, shell=shell, capture_output=True, text=True,
                              env=environment, cwd=cwd or ROOT, timeout=timeout, preexec_fn=limits)
    except subprocess.TimeoutExpired:
        return subprocess.CompletedProcess(cmd, 124, "", "timed out after %ss" % timeout)


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
    # A planted defect can make the probe loop: it is bounded like a test (the sink stories' walks made this real).
    c = run(["tools/check-corpus-hash.sh", str(exe), "tests/corpus/scene.hash"],
            env_extra={"LD_LIBRARY_PATH": LD_PATH}, timeout=180, limits=_limits)
    return c.returncode, (c.stdout + c.stderr).strip()


def replay_run(objs, d, target="record"):
    """A fuzz replay (fuzz_record or fuzz_render) over its committed corpus, built against the planted objects."""
    exe = d / ("replay-" + target)
    cmd = "%s -o %s tests/fuzz/fuzz_%s.c tests/fuzz/replay_main.c %s %s" % (
        " ".join(CC + PROBE_CFLAGS), exe, target, " ".join(objs), PROBE_LIBS)
    r = run(cmd, shell=True)
    if r.returncode != 0:
        raise RuntimeError("cannot build the replay:\n%s" % r.stderr)
    inputs = sorted(str(p) for p in (ROOT / "tests" / "fuzz" / "corpus" / target).rglob("*") if p.is_file())
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
    elif kind == "tst":
        # A change to a test-only header (the evaluator): the tests that include it are recompiled with the planted copy
        # ahead on the include path, and linked against the unplanted library.
        tinc = d / "tinc"
        tinc.mkdir()
        (tinc / Path(fname).name).write_text(apply((ROOT / fname).read_text(), old, new, pid))
        caught_by = []
        for test in tests:
            snake = re.sub(r"(?<!^)(?=[A-Z])", "_", test[len("test"):]).lower()
            obj = d / (test + ".o")
            r = run(shlex.split(CXX) + ["-I", str(tinc)] + shlex.split(CXX_INCLUDE) +
                    ["-c", str(ROOT / "tests" / "unit" / ("test_%s.cpp" % snake)), "-o", str(obj)])
            if r.returncode != 0:
                raise RuntimeError("plant %s does not compile:\n%s" % (pid, r.stderr))
            exe = d / test
            cmd = "%s -o %s %s %s %s" % (CXX, exe, obj, " ".join(control_objs), LDFLAGS)
            r = run(cmd, shell=True)
            if r.returncode != 0:
                raise RuntimeError("cannot link %s:\n%s" % (test, r.stderr))
            rc, failing = run_test(exe)
            if rc != 0:
                caught_by.append((test, failing[:1]))
                break
        return pid, caught_by
    elif kind == "orc":
        # A change to the oracle: its own selftest, or the vectors it writes, must disagree with the committed ones.
        tools = d / "tools"
        tools.mkdir()
        for name in ("composite-oracle.py", "gen-composite-vectors.py", "gen-scene-vectors.py", "gradient-oracle.py",
                     "gen-gradient-vectors.py"):
            shutil.copy(ROOT / "tools" / name, tools / name)
        (d / "tests").mkdir()
        for name in ("composite_vectors.h", "gradient_vectors.h", "group_vectors.h"):
            shutil.copy(ROOT / "tests" / name, d / "tests" / name)
        planted = tools / Path(fname).name
        planted.write_text(apply(planted.read_text(), old, new, pid))
        caught_by = []
        for test in tests:
            # selftest and vectors: the composite oracle's; gselftest and gvectors: the gradient oracle's (P1).
            args = ["--selftest"] if test in ("selftest", "gselftest") else ["--check"]
            script = {"selftest": "composite-oracle.py", "vectors": "gen-composite-vectors.py",
                      "gselftest": "gradient-oracle.py", "gvectors": "gen-gradient-vectors.py"}[test]
            r = run([sys.executable, str(tools / script)] + args)
            if r.returncode != 0:
                caught_by.append((test, [(r.stdout + r.stderr).strip().splitlines()[-1][:80] if (r.stdout + r.stderr).strip() else "crash"]))
                break
        return pid, caught_by
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
        elif test == "fuzz-render":
            rc, msg = replay_run(objs, d, "render")
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
    for target in ("record", "render"):
        rc, msg = replay_run(control, WORK, target)
        if rc != 0:
            print("FAIL  control: the %s fuzz replay fails on the unplanted build: %s" % (target, msg))
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
        for done, fut in enumerate(concurrent.futures.as_completed(futures), 1):
            p = futures[fut]
            try:
                results[p[0]] = fut.result()[1]
            except Exception as e:  # a plant that cannot be built is a defect of the harness
                results[p[0]] = e
            # Progress, as each plant ends, so a run cut short still says how far it got and what it found.
            r = results[p[0]]
            sys.stderr.write("[%d/%d] %s %s\n" % (done, len(plants), p[0],
                             "error" if isinstance(r, Exception) else ("caught" if r else "NOT CAUGHT")))
            sys.stderr.flush()
            if r and not isinstance(r, Exception):
                shutil.rmtree(WORK / p[0], ignore_errors=True)
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
