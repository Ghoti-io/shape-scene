# Design

**Status:** the waist (resources, lists, the immediate-mode recording path, hash and dump), the
retained scene above it (versions, edits, identity, merge, diff and damage), and what a scene means when
it is drawn (color, paint, composition and order, below) are implemented. The sink contract is the next step.

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

The gradient and image resource kinds, a gradient paint and a palette paint are named and refused with
`ERR_UNSUPPORTED`; their values are pinned because they are hashed. A story that gives one a meaning adds a
constructor or loosens one check. A palette paint stays refused because AD-23 says only that the palette
index is a scene parameter, not whose table it selects in, how the table reaches the scene, or what a
missing entry is; that is settled with the font's CPAL. Also refused with `ERR_UNSUPPORTED` are ProPhoto
(D50: converting to it needs a chromatic adaptation), the parametric, BT.1886, PQ and HLG transfers and ICC
profiles, and every blend mode (multiply, screen, ...): the composite operator has room for them.

## What a scene means: paint, color and composition (normative)

This section is the contract with every sink (`shape-cpu` now, a GPU sink later). It is written so that two
independent implementations agree, and it is checked by an independent reference: `tools/composite-oracle.py`
implements it in exact rational arithmetic, `tools/gen-composite-vectors.py` commits its answers as
`tests/composite_vectors.h`, and `tests/ref_composite.h` is a deliberately simple double-precision evaluator
of a closed list that `tests/unit/test_composite.cpp` requires to agree with the oracle (bit for bit where
the values are dyadic and no space changes, within 1e-13 where a transfer function or a matrix is
evaluated). A sink's own test is measured against the same table.

**What is defined and what is not.** Everything below is stated in real arithmetic: every value is an exact
real number computed from the doubles given, with no rounding and no clamping unless the text says so. A
sink's finite precision, its coverage definition (AD-12: 8-bit exact area for `shape-cpu`) and its tolerance
(AD-18) are its own and are settled with the sink contract; what is fixed here is what the answer is, so a
sink's error can be measured. The scene itself never evaluates a color: `color`'s transfer functions are
libm's `pow`, `exp` and `log`, which AD-22 forbids in the shape family, so converting a color between spaces
is the sink's, with `math`'s `gmath_pow`, against the formulas below.

**N1 Color space.** A space is `(primaries, transfer, gamma)`, held as `GSHSCENE_Color_Space` (`GCOL_*`
enums from `color`). The zero value is the target's space. Otherwise `primaries` is one of four gamuts, all
with the D65 white `(0.3127, 0.3290)` (CIE xy): sRGB `(0.64, 0.33) (0.30, 0.60) (0.15, 0.06)`; Adobe RGB
`(0.64, 0.33) (0.21, 0.71) (0.15, 0.06)`; Display P3 `(0.680, 0.320) (0.265, 0.690) (0.150, 0.060)`; BT.2020
`(0.708, 0.292) (0.170, 0.797) (0.131, 0.046)`; `transfer` is linear, sRGB or a plain gamma, and `gamma` is 0
except for the gamma transfer, where it is finite and in `[0.1, 10]`. These are `color`'s own table (a test
compares them), so every matrix below is exactly rational. `gshscene_color_space_from_info` maps a
`GCOL_Color_Info` that names one of these (and carries no ICC profile).

**N2 Transfer.** `decode` takes an encoded component to linear light, `encode` is its inverse, on real values
of any sign. Linear: the identity. sRGB: decode `x / 12.92` for `x <= 0.04045`, else `((x + 0.055) / 1.055)^2.4`;
encode `12.92 y` for `y <= 0.0031308`, else `1.055 y^(1/2.4) - 0.055` (the linear segment continues below zero,
as in `color`). Gamma `g`: decode `sign(x) |x|^g`, encode `sign(y) |y|^(1/g)`. A test compares these with
`color`'s evaluator on a grid, below zero included.

**N3 Conversion.** For a gamut with primaries `c = r, g, b` and white `W`, let `X_c = x_c / y_c`, `Y_c = 1`,
`Z_c = (1 - x_c - y_c) / y_c`; solve `[X_r X_g X_b; 1 1 1; Z_r Z_g Z_b] S = W` and let the matrix `M` from linear
RGB to XYZ have the columns `S_c (X_c, 1, Z_c)`. A straight color `(r, g, b)` goes from space `A` to space `B`
as `encode_B(M_B^-1 M_A decode_A(r, g, b))`; it is untouched (exactly, not recomputed) when the spaces are the
same, and when only the transfers differ the two matrices cancel and are not applied. Alpha is never converted. A
premultiplied value `(R, G, B, A)` is converted by un-premultiplying (`c = C / A`; for `A = 0` the result is
`(0, 0, 0, 0)`), converting the straight color, and premultiplying again.

**N4 Composite operators.** On premultiplied values, source `S = (Cs, As)` and backdrop `D = (Cd, Ad)`,
`op(S, D) = (Fa Cs + Fb Cd, Fa As + Fb Ad)` with `(Fa, Fb)`: `CLEAR (0, 0)`, `SRC (1, 0)`, `DEST (0, 1)`,
`SRC_OVER (1, 1-As)`, `DEST_OVER (1-Ad, 1)`, `SRC_IN (Ad, 0)`, `DEST_IN (0, As)`, `SRC_OUT (1-Ad, 0)`,
`DEST_OUT (0, 1-As)`, `SRC_ATOP (Ad, 1-As)`, `DEST_ATOP (1-Ad, As)`, `XOR (1-Ad, 1-As)`; and `PLUS`, which is
`min(1, Cs + Cd)` for each color channel and `min(1, As + Ad)` (nothing below is clamped, and `PLUS` clamps
from above only). An operator is **bounded** if `op(0, D) = D` for every `D` (`DEST`, `SRC_OVER`,
`DEST_OVER`, `DEST_OUT`, `SRC_ATOP`, `XOR`, `PLUS`) and **unbounded** otherwise (`CLEAR`, `SRC`, `SRC_IN`,
`DEST_IN`, `SRC_OUT`, `DEST_ATOP`: they change the backdrop where the source is empty).

**N5 The primitive.** `apply(D, S, m, op) = (1 - m) D + m op(S, D)`, componentwise on premultiplied values, for a
coverage `m` in `[0, 1]`. Everything composited is one `apply`.

**N6 Layers and the root.** Compositing happens in a layer: a premultiplied value per device pixel in a blend
space. The root layer is the target: its blend space is the target's and its initial value is what the target
held. A group has a layer of its own that starts as `(0, 0, 0, 0)` in the group's blend space, which is its
`blend_space` if nonzero and else the blend space of the group that encloses it. The items between a group's
push and pop are applied to its layer; at its pop it is composited onto the enclosing layer (N8).

**N7 Fill.** A fill is applied to the layer of the group it is in. Its source is the paint's straight color
`(r, g, b, a)` converted (N3) from the paint's space (the target's if zero) to the layer's blend space and
premultiplied, `S = (a c, a)`. Its coverage is `k c_clip`: `k` in `[0, 1]` is the coverage of the path or glyph
run under its map by its fill rule (the sink's own definition, here an input) and `c_clip` the product of the
clips in force (N9). Its operator is `SRC_OVER`, so `apply` gives `L (1 - m As) + m S`.

**N8 Group composite.** At the pop of a group with opacity `o`, operator `op`, layer `G` in space `Bg`, onto the
enclosing layer `P` in space `Bp`: `S = o convert(G, Bg -> Bp)` (premultiplied; scaling and converting commute) and
`P' = apply(P, S, m, op)`, where `m` is the product of the clips that were in force in the enclosing group when the
group was pushed (1 if none). If `op` is unbounded the group affects every pixel with `m > 0`, including those
where `G` is empty.

**N9 Clip.** A clip push contributes the coverage `c` of its region (a path or glyph run under its map by its
rule, again the sink's coverage). The clips in force are those pushed and not popped within the same group (the
root counts as one); a group push does not take the clips outside it into its own layer, they multiply once, at
its composite (N8). A clip around fills multiplies each fill's coverage (AD-12: it "multiplies the coverage of
what it clips"), so two overlapping fills under a partial clip are each weighted by it; put the content in a
group where a clip must weigh a result once. The two differ only at a pixel of partial clip coverage, and the
committed vectors show them side by side.

**N10 The foreground.** The foreground is a color (straight doubles and a space) that belongs to a version
(`gshscene_edit_set_foreground`, `gshscene_version_foreground`) and to a list (`gshscene_list_set_foreground`); the
list a version emits carries the version's. A `GSHSCENE_PAINT_FOREGROUND` paint is the foreground with its alpha
multiplied by the paint's: straight `(fr, fg, fb, fa a)` in the foreground's space. Its own `r, g, b` are zero and
its space is zero (one spelling). The default is opaque black in the target's space. It is `currentColor`, never
baked into a fill, so an icon drawn in ten colors is one picture; a picture's `FOREGROUND` fills resolve against the
foreground of the list that draws it, and a picture itself holds the default.

**N11 Order.** The units of a sequence are its fills, instances, group blocks and clip blocks (push to matching
pop); a sequence is the items directly inside the root, a group or a clip block. A unit's key is the layer and
depth of its first item. The sequence is painted in the order its context gives, the context being that of the
group that encloses the sequence (the root's is flat; a clip block has the context of the sequence it is in; a
picture's own top level is flat). Flat: declared order, keys ignored. Overlay: ascending layer, equal layers in
declared order, so no tie is recorded. Depth: descending depth (the farther unit first, so the nearer is on top);
two units are tied when their depths are equal as real numbers (`-0` equals `0`); a tie is broken by ascending
layer, then by ascending instance path (`gshscene_instance_path_compare`; siblings' paths differ in the last element,
the node ID; a recorded list has no paths and declared order stands in), and every pair of adjacent tied units is
recorded with the depth and what broke it (`LAYER`, `PATH`, `DECLARED`). AD-10's "when declared order and computed depth
disagree, the scene's declaration wins" is read as: the context is declared, so depth never reorders a group that
did not declare depth, nor across a nesting boundary (a nested group, clip block or picture is one unit of its
parent's sequence). Depth is a declared per-node number while 3D maps are refused; when a 3D map exists the depth is
computed from it by an exact predicate and this is its 2D special case (a Deferred row says so).

**N12 Where the order is made.** `gshscene_list_close` puts a recorded list in paint order (it permutes the units of
every sequence, rewrites the items and records the ties), so the list a sink sees is in paint order and a sink never
sorts. A version emits its list in paint order directly: the one walk orders the children of a group by its context,
hidden children taking no part, and a group that sits in a non-flat sequence is emitted as a group push so that it stays
one unit of that sequence and carries its own context. The two lists are equal (`gshscene_list_equal`) for the same
drawing. The ties are an annotation for a report: they are in the dump and in neither the hash nor the equality.

**N13 Damage.** An occurrence of a group whose operator is unbounded that lies in a damaged subtree makes the damage
unbounded (a sink redraws the whole target); a changed foreground damages the occurrences of the shapes that paint with
it and nothing else; a change of anything else a node carries damages its subtree, as before.

**Not decided here, and where it is recorded.** Blend modes (multiply, screen, ...), the other color spaces, the
palette paint, gradients, the depth predicate for 3D maps, and the merge of adds to one parent are Deferred rows in the
spine; `notes/shape/merge-adds-to-one-parent.md` assesses the last.

## The retained scene

The retained scene is a builder above the waist (AD-1, AD-9): it emits lists over interned resources and
nothing below it knows it exists. Five pieces, one header each: `version.h`, `edit.h`, `scene.h`, `diff.h`
and, for the numbers, `core.h`.

**A version** is immutable, reference counted and shareable. It is a sorted array of node pointers, so
nodes are shared between versions by reference count and the index is the version's own. A node is a
group (children in draw order, a map, an opacity, a blend space, a composite operator, an ordering context, a
layer and a depth, an optional clip), a shape (a path or glyph run under a map, a rule, a paint, a layer and a
depth) or an instance (another node of the version, drawn again).
Any node may be hidden, which is how a definition is written: drawn only through an instance. The hash of
a version is over the foreground and the nodes in ID order and nothing else, so it does not depend on the version's number,
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

