# Design

**Status:** the waist (resources, lists, the immediate-mode recording path, hash and dump) and the
retained scene above it (versions, edits, identity, merge, diff and damage) are implemented. Paint and
composition, and the sink contract, are later steps.

The shape family waist: resources and lists, and the versioned scene above them.

The dependency rules and the determinism gates that bind this library are in the Makefile's header
comment and in `tools/`: `check-fp-contract`, `check-edges`, `check-libm` and `check-no-x87`, each shown
to fail on a planted violation by `make check-gates`. The plan is in the workspace, not in this
repository.

## Resources

A resource is immutable content, held by handle. `GSHSCENE_Store` interns by content: the table is
indexed by a hash (the content hash, or what an injectable function maps it to, which is how a test
plants a collision), and a candidate is the same resource only if its content is equal bit for bit.
A hash alone never decides. A miss builds the copy outside the store's lock and looks again before it
links, so two threads interning equal content get one handle. A handle is retained and released
atomically, and a lookup never takes a reference from a count of zero, so a resource whose last
release has begun cannot be resurrected. The id of a resource comes from a per-store counter and is
never reused; a sink's cache is keyed by it (a pointer can be reused by the allocator), and drops an
entry from the release listener. The store lives until its last resource is released.

Content is copied in: a path is replayed into a path of the store's allocator, a glyph run's array is
copied, a picture is a closed copy of a list. A resource that names others (a run of outlines, a
picture) holds a reference on each, so equality compares them by handle.

## Lists

A list is an arena (chunks from the maker's allocator) with the items in segments of 16, 32, 64, ...
so an item never moves and indexing is a shift. It holds one reference on every resource it names, so
a handle is never reused while a list references it, and frees the lot in one call. Clips and groups
nest and are checked at the record call; `close` checks that none is open and seals the list. Every
limit is checked where its quantity grows (`ERR_LIMIT`, the list as it was), including the expansion
count that bounds a picture of instances of pictures.

## Hash and dump

The spellings are in `resource.h` and `list.h`, field by field, in `shape-geometry`'s hash
(FNV-1a 64, little-endian fields). `tools/gen-scene-vectors.py` states them a second time with Python's
`struct`, and `tests/known_answers.h` is what it writes. `tools/corpus-hash.c` is the cross-target probe:
`suite/tools/xarch/shape.sh` and `xwin/shape.sh` check its hash on every target.

## Reserved

The gradient and image resource kinds, a gradient paint, a color space, a blend space and the depth and
overlay ordering contexts are named and refused with `ERR_UNSUPPORTED`; their values are pinned
because they are hashed. A story that gives one a meaning adds a constructor or loosens one check.

## The retained scene

The retained scene is a builder above the waist (AD-1, AD-9): it emits lists over interned resources and
nothing below it knows it exists. Five pieces, one header each: `version.h`, `edit.h`, `scene.h`, `diff.h`
and, for the numbers, `core.h`.

**A version** is immutable, reference counted and shareable. It is a sorted array of node pointers, so
nodes are shared between versions by reference count and the index is the version's own. A node is a
group (children in draw order, a map, an opacity, a layer, an optional clip), a shape (a path or glyph run
under a map, a rule, a solid paint, a layer) or an instance (another node of the version, drawn again).
Any node may be hidden, which is how a definition is written: drawn only through an instance. The hash of
a version is over the nodes in ID order and nothing else, so it does not depend on the version's number,
the allocator, a handle or the history that made it; `version.h` states the spelling and
`tools/gen-scene-vectors.py` states it again.

**An edit** is what a front-end or a driver is given. It is a working view over a base version: a node it
changes is first copied into the edit and then changed, so the base is never touched, and a call that
fails changes nothing and consumes no ID. A key (a domain and a value) is bound to a node, and an upsert by
key finds the node a front-end's earlier lowering made, so IDs survive re-lowering and an identical
re-lowering touches nothing and publishes nothing. **An edit cannot publish**: publishing takes the
application's scene, which no function returns from an edit or a version; `make check-publish-types`
compiles probes that must and must not compile, and a self-test that collapses the types.

**The scene** owns the ID counter (which is why IDs reproduce), the current version and the store. A
publish runs under its lock: it takes what each edit really changed (entries equal to the base's are
dropped), checks that the touched sets of sibling edits are disjoint and that what other publishes changed
since the edits' base does not overlap them, applies them to the current version, and validates the whole
result before anything changes: every instance's target exists, nothing is a cycle, and every structural
limit holds (the node count, the depth, the occurrences, the items of the list and of every instanced
node's picture, the push nesting, the picture nesting). The result of a merge does not depend on the order
of the edits because each node of it comes from one edit or the base.

**Emission** and the visitor and the damage are three consumers of one walk over a version's
occurrences, in draw order, with the placements composed the way a sink composes them. An instanced node
becomes a picture, interned once per target, so a symbol drawn a thousand times is one resource and a
thousand items; an origin table says which node each item came from, which is what a report is keyed by.

**Damage** is computed over occurrences in both versions: a node whose own properties changed damages its
whole subtree, a group whose children changed damages the children added and removed (and all of them if
their order changed), and an instance of a changed node is reached by the walk. Each shape occurrence's
bounds are its path's exact bounds mapped by the composed map, outset by the AA support and rounded outward.
Where bounds are unknowable (a glyph run named by a font, a 3D path, a map that cannot be applied) the
damage says unbounded. `tests/unit/test_diff.cpp` checks it against an oracle that shares none of its
structure: for random scenes and edits it requires every pixel whose ordered drawing records differ to lie
inside the damage.

**The store's total bytes** are bounded by `max_bytes`, accounted by a formula (`resource.h`) so the limit
means the same on every target.

Gaps that this design knows about and where they are recorded: the spine's Deferred table (a font as an
opaque key, fixed picture paints, intersection-only clips, per-glyph maps, affine and 2D maps, the linear
cost of publish and diff, the granularity of a merge, single-rectangle damage, what is met at emission).

