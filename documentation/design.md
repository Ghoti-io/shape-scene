# Design

**Status:** the waist (resources, lists, the immediate-mode recording path, hash and dump), the
retained scene above it (versions, edits, identity, merge, diff and damage), and what a scene means when
it is drawn (color, paint, composition and order, below) are implemented, and so is the sink contract
with its report and the test sink (story 13a). The first real sink, shape-cpu's, is story 13b. Runs from a
font reach the waist through a face resource and the glyph path provider seam (story 14a, below); `font`
implements the provider in story 14b. Gradients (color-paint story P1: the resource, the paint, G1 to G13, the one
conversion of their stops at draw time, the notices of what draws nothing) are implemented scene-side; shape-cpu draws
them in story P2. Blend modes (color-paint story P3: the fifteen modes of W3C Compositing and Blending Level 1 as
composite operators 13 to 27, B1 to B14) are implemented scene-side: the scene names, validates, hashes and plans them,
and a sink composites them. Strokes (strokes story S1: a centerline plus an interned style, SK1 to SK14, below) are being
added to the waist: the style resource, the stroke item and node, their bounds and damage, the sinks' capabilities for
them and the planning pass are implemented, and the test sink records them; shape-cpu refuses every stroke until it
draws them (strokes story S4).

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

A face (story 14a) is a resource too: a font's key, a face index and normalized variation coordinates as
F2.14 (trailing zeros dropped, so equal instances are one handle), with the provider's binding as a
non-content field: the first intern of a content builds the face, asks `face_info` once and retains the
binding, and the face's last release releases it once. A run from a font names a face handle, which it
holds. The formats are per kind, and every kind's is 1 until the suite's first release: a change before it changes
format 1 in place, so the face replacing the key left a glyph run's format at 1.

A palette (story 14c, N14) is a resource too: a table of at most 65,535 colors, each straight `double`s and a
space, interned by content. A font's CPAL reaches it through the float-free `gshscene_store_intern_palette_rgba8`
(fixed.h): each 8-bit component becomes `c / 255`, the double nearest (one correctly rounded division of two exact
integers, so every target gives the same bits; the probe checks it), and the space is sRGB, which is what CPAL
states.

A gradient (color-paint story P1, G1) is a resource too: a kind (linear, radial, sweep), its geometry in gradient
space, a 2D map into the item's local space, a color line of stops (an offset and a solid, foreground or palette paint),
an extend, an interpolation space and a premultiplied flag, interned by content with its stops sorted stably by offset
and `-0` stored as `+0`, so equal gradients are one handle and a writer or a GPU sink dedupes them by id. Nothing in it
is converted: the stops are kept as given and hashed as given, so a better `gmath_pow` changes pixels and never handles.

A stroke style (strokes story S1, SK1 to SK3) is a resource too: a width and its space, a join and its miter limit, a
cap, what a subpath of zero length draws, a dash array with its offset and scope, an alignment and its fill rule, and a
reserved width profile, interned in a canonical form (an all-zero or negative dash array is solid, an odd one is
doubled, a field that does not apply takes its default, `-0` is `+0`), so one drawing is one handle and one hash.

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
`suite/tools/xarch/shape.sh` and `xwin/shape.sh` check its hash on every target. Its stroke section (strokes story
S1: styles read back canonical, SK10's bounds under composed maps in every unit, the notices and features of strokes,
their renders by sinks that draw, expand and refuse them, a retained scene's diff of stroke nodes) folds into a
second hash and count, `stroke-hash` and `stroke-count` in `tests/corpus/scene.hash`, so that adding it left the
first unchanged; both pairs are checked.

## Recording without floats (fixed.h; story 14c)

A front end whose headers name no floating type (`font`, AD-5) records with `fixed.h`, which is float-free and
read by the same gate as `glyph_provider.h`: a fill or clip push of a run from a font with a **design map** (a chain
of integer ops: affine, translate, scale, rotate and skew in turns, each about a center, every value `v / 2^bits`), a
foreground or palette paint with a fixed alpha, a group push of an operator, the two pops, the count and
**truncation**. Each converts exactly and calls the list's own record call, so the rules and failures are the list's.

The design map is in the face's design units, y up, about the run's first glyph `g`, at the run's scale `s =
size / upem`. Each op's map is shape-geometry's (a rotation in turns, so quarter turns are exact; a skew's tangent is
the sine over the cosine of that rotation), the chain is `gshgeom_map_compose_chain` of them, and with `T = (A, b)`
the item map is `translate(s b_x, -(s b_y))` when `A` is exactly the identity, else the composer's chain of
`translate(g)`, `flip_y`, `(A, s b)`, `flip_y` and `translate(-g)`. No inverse is formed: `diag(s, -s) A diag(s,
-s)^-1` is `diag(1, -1) A diag(1, -1)`, which the two flips compute exactly. A translation stays a translation, so a
run that the design map only moves keeps the fast tier (D2's route).

A **palette entry of `0xFFFF`** (a font's foreground) becomes the foreground paint in the fixed paint, so every front
end lowers it alike; the `double` paint refuses it. **`gshscene_list_truncate`** drops items from a count on as if
they had not been recorded (references released, the open pushes, the expansion and the deepest picture counted
again from the items kept), so a front end that lowers one construct into several items leaves the list with all of
it or none.

## Reserved

The image resource kind is named and has no constructor; its value is pinned because it is hashed. A story that gives
it a meaning adds a constructor. (The palette paint, refused until story 14c, is N14 below; the gradient paint and
resource, reserved until color-paint story P1, are G1 to G13.) Also refused with `ERR_UNSUPPORTED` are ProPhoto
(D50: converting to it needs a chromatic adaptation), the parametric, BT.1886, PQ and HLG transfers and ICC
profiles, and a stroke style's **width profile** (variable width, D8 of the strokes spec): the slot is a resource
handle, hashed as `u64` 0 while it is empty, so that defining a profile fills a spelling that already exists.

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
sink's error can be measured. The scene converts one thing itself, a gradient's stops at draw time (G3), through
`math`'s `gmath_pow` in the one function every sink calls; every other conversion between spaces is the sink's, with
`math`, against the formulas below. `color`'s transfer functions are libm's `pow`, `exp` and `log`, which AD-22 forbids
in the shape family, and `check-libm` still judges this library.

**N1 Color space.** A space is `(primaries, transfer, gamma)`, held as `GSHSCENE_Color_Space` (`GCOL_*`
enums from `color`). The zero value is the target's space. Otherwise `primaries` is one of four gamuts, all
with the D65 white `(0.3127, 0.3290)` (CIE xy): sRGB `(0.64, 0.33) (0.30, 0.60) (0.15, 0.06)`; Adobe RGB
`(0.64, 0.33) (0.21, 0.71) (0.15, 0.06)`; Display P3 `(0.680, 0.320) (0.265, 0.690) (0.150, 0.060)`; BT.2020
`(0.708, 0.292) (0.170, 0.797) (0.131, 0.046)`; `transfer` is linear, sRGB or a plain gamma, and `gamma` is 0
except for the gamma transfer, where it is finite and in `[0.1, 10]`. These are `color`'s own table (a test
compares them), so every matrix below is exactly rational. `gshscene_color_space_from_info` maps a
`GCOL_Color_Info` that names one of these (and carries no ICC profile).

*The bound and the saturation (Corey, 2026-10-21; color-paint story P2's change log).* Every color component the
scene records is finite and of magnitude at most `2^16` (`GSHSCENE_COLOR_COMPONENT_MAX`): the `r, g, b` of a solid
paint (a fill's, a shape node's, a solid gradient stop's), of the foreground (a list's and an edit's) and of each
palette entry; an alpha is in `[0, 1]` already. Past it the record call is `ERR_LIMIT` (`gshscene_list_fill`,
`gshscene_edit_add` and `_set`, `gshscene_store_intern_gradient` with G14, `gshscene_list_set_foreground`,
`gshscene_edit_set_foreground`, `gshscene_store_intern_palette`, `gshscene_color_check`), decided after every check
that makes the call `ERR_INVALID` or `ERR_UNSUPPORTED`, as G14's are. Every value computed from them is **saturated**,
`sat(x) = min(max(x, -2^64), 2^64)` (`GSHSCENE_COLOR_SATURATION`), at exactly these points, each component on its own:
N2's decode and encode results within a conversion; N3's matrix result `M_B^-1 M_A v` (the product as one, however an
implementation forms it); every **un-premultiply** `C / A` (N3's premultiplied conversion, G4's flag 1, B2's `Cs` and
`Cb`, and a sink's store to a straight target); each component of a blend mode's mixing function `B(Cb, Cs)` before it
is weighted (B9); and N5's result, so every fill, every group composite (`PLUS` and the blend modes included) at every
depth.
The saturation is part of the definition, not a sink's choice: the oracles, the scene's conversion (G3) and every sink
apply it at the same points, so a value past `2^64` has one meaning on every sink. Content near `[0, 1]` does not reach
it: one conversion of a component of magnitude at most 1 is at most `2.33^10 < 2^13` (a matrix, then a gamma-0.1
encode); a component above about 1.6 does, through a gamma-10 decode and a gamma-0.1 encode (the 100th power), as do
long chains of conversions and accumulations under `PLUS`. Every committed hash and golden is the same with it.

Why every value stays finite. Call a value *bounded* when its magnitude is at most `2^64`. A recorded component,
alpha, opacity, coverage, clip and N4 factor is bounded, every saturated value is bounded, and the root layer starts
at what the target holds (finite; at most about `2^128` in a float target). Each step takes bounded values (and the
root's) to a finite result, which the next saturation makes bounded again:

| Step | Worst magnitude before its saturation |
| --- | --- |
| decode: gamma `g <= 10`, `sign(x) abs(x)^g`; sRGB `((x + 0.055) / 1.055)^2.4` or `x / 12.92` | `(2^64)^10 = 2^640`; `2^154`; below `2^64` |
| matrix: a row of `M_B^-1 M_A` sums to at most 2.33 in magnitude (formed through XYZ: 1.09 then 5.28) | `2^66` (`2^67`) |
| encode: `sign(y) abs(y)^(1/g)`, `1/g <= 10`; sRGB `1.055 y^(1/2.4) - 0.055` or `12.92 y` | `2^640`; `2^27`; `2^68` |
| un-premultiply `C / A`, `A` in `(0, 1]` (the root's: finite) | unbounded as `A` goes to 0: see below |
| premultiply, alpha times alpha, opacity, coverage: a factor in `[0, 1]` | bounded stays bounded |
| N4 `Fa Cs + Fb Cd`, `PLUS`'s `Cs + Cd`, N5 `(1 - m) D + m op`: factors in `[0, 1]`, `D` at most the root's | `2^130` |
| G4: an interpolation of bounded interpolants by a fraction in `[0, 1]` | bounded (a convex combination) |
| B7: a separable mode's polynomial on bounded `Cb`, `Cs` (soft-light's `16 Cb^3` is the largest) | `2^196` |
| B7: dodge's `Cb / (1 - Cs)`, burn's `(1 - Cb) / Cs` | unbounded in the reals: taken as `sign 2^66` past it (B9) |
| B8: `Lum`, `Sat`, `SetSat`, `SetLum`'s shift, ClipColor's two steps | `2^64`, `2^65`, `2^65`, `2^67`, below `2^140` |
| B2: `cs (1 - Ab) + cb (1 - As) + As Ab sat(B)`, every factor in `[0, 1]` | `3 * 2^64` |

So the worst step is a power, `2^640`, far below the largest double (just under `2^1024`). What made a bound on the
recorded values alone useless is that conversions chain: a gamma-10 decode followed by a gamma-0.1 encode is the
100th power, and nested groups and `PLUS` compound further. Saturating after each step means a power's argument is
always bounded, so no power is ever taken of an unsaturated power and no comparison of logarithms is needed: a sink
evaluates each step in doubles and clamps its double result. The one step whose exact value is unbounded is the
un-premultiply, and it is evaluated without forming it: when `|C| >= 2^64 |A|` the result is `sign(C / A) 2^64` and
nothing is divided (`2^64 |A|` is exact, a power-of-two scaling); otherwise `|C / A| < 2^64`. So no step forms an
infinity, and no NaN can arise. `2^16` is not what finiteness needs (`2^64` at record would do): it keeps every
recorded value `2^48` below the saturation, so that only a transfer's power, a matrix or an accumulation reaches it.

**N2 Transfer.** `decode` takes an encoded component to linear light, `encode` is its inverse, on real values
of any sign. Linear: the identity. sRGB: decode `x / 12.92` for `x <= 0.04045`, else `((x + 0.055) / 1.055)^2.4`;
encode `12.92 y` for `y <= 0.0031308`, else `1.055 y^(1/2.4) - 0.055` (the linear segment continues below zero,
as in `color`). Gamma `g`: decode `sign(x) |x|^g`, encode `sign(y) |y|^(1/g)`. A test compares these with
`color`'s evaluator on a grid, below zero included. Within a conversion each decode and each encode is saturated (N1).

**N3 Conversion.** For a gamut with primaries `c = r, g, b` and white `W`, let `X_c = x_c / y_c`, `Y_c = 1`,
`Z_c = (1 - x_c - y_c) / y_c`; solve `[X_r X_g X_b; 1 1 1; Z_r Z_g Z_b] S = W` and let the matrix `M` from linear
RGB to XYZ have the columns `S_c (X_c, 1, Z_c)`. A straight color `(r, g, b)` goes from space `A` to space `B`
as `encode_B(M_B^-1 M_A decode_A(r, g, b))`, saturated after each of the three (N1:
`sat(encode_B(sat(M_B^-1 M_A sat(decode_A(r, g, b)))))`, the middle one only when the gamuts differ); it is
untouched (exactly, not recomputed) when the spaces are the same, and when only the transfers differ the two matrices
cancel and are not applied. Alpha is never converted. A premultiplied value `(R, G, B, A)` is converted by
un-premultiplying (`c = sat(C / A)`, evaluated as N1 says; for `A = 0` the result is `(0, 0, 0, 0)`), converting the
straight color, and premultiplying again.

**N4 Composite operators.** On premultiplied values, source `S = (Cs, As)` and backdrop `D = (Cd, Ad)`,
`op(S, D) = (Fa Cs + Fb Cd, Fa As + Fb Ad)` with `(Fa, Fb)`: `CLEAR (0, 0)`, `SRC (1, 0)`, `DEST (0, 1)`,
`SRC_OVER (1, 1-As)`, `DEST_OVER (1-Ad, 1)`, `SRC_IN (Ad, 0)`, `DEST_IN (0, As)`, `SRC_OUT (1-Ad, 0)`,
`DEST_OUT (0, 1-As)`, `SRC_ATOP (Ad, 1-As)`, `DEST_ATOP (1-Ad, As)`, `XOR (1-Ad, 1-As)`; and `PLUS`, which is
`min(1, Cs + Cd)` for each color channel and `min(1, As + Ad)` (nothing below is clamped, and `PLUS` clamps
from above only). An operator is **bounded** if `op(0, D) = D` for every `D` (`DEST`, `SRC_OVER`,
`DEST_OVER`, `DEST_OUT`, `SRC_ATOP`, `XOR`, `PLUS`) and **unbounded** otherwise (`CLEAR`, `SRC`, `SRC_IN`,
`DEST_IN`, `SRC_OUT`, `DEST_ATOP`: they change the backdrop where the source is empty). The values 13 to 27 are the
blend modes, B1 to B14 below; each is bounded.

**N5 The primitive.** `apply(D, S, m, op) = sat((1 - m) D + m op(S, D))`, componentwise on premultiplied values, for a
coverage `m` in `[0, 1]`, saturated (N1). Everything composited is one `apply`, so every layer value is bounded.

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
`P' = apply(P, S, m, op)` (the conversion and the `apply` saturated, N1), where `m` is the product of the clips that
were in force in the enclosing group when the group was pushed (1 if none). If `op` is unbounded the group affects
every pixel with `m > 0`, including those where `G` is empty.

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
it and nothing else; a changed palette (N14) damages the occurrences of the shapes that paint from it and nothing
else; a change of anything else a node carries damages its subtree, as before.

**N14 Palette** (story 14c; AD-23's palette index). A **palette** is a resource: a table of colors, each a straight
color and a space as a foreground is. A version and a list carry a **palette parameter**, a palette handle or none,
beside the foreground; a version's list carries its version's; a picture has none (its palette paints resolve against
the list that draws it). A `PALETTE` paint names an **entry** in `[0, 0xFFFE]` with an alpha multiplier `a`: it is
the entry's color `(pr, pg, pb, pa)` with its alpha multiplied, straight `(pr, pg, pb, pa * a)` in the entry's
space; its `r, g, b` are zero and its space is zero (one spelling). `0xFFFF`, which a font uses for the foreground,
is never an entry: the float-free paint lowers it to `FOREGROUND`. An entry the palette in force does not have (no
palette, or an entry at or past its count) is **missing**: the fill is **refused by the planning pass**, reason
`PALETTE_ENTRY`, on every sink, and is never drawn in another color (transparent, the foreground or a default would
each be a color the author did not state). The one resolution is `gshscene_paint_resolve`, and the one disposition
(`gshscene_caps_disposition`) takes the palette, so a writer, a GPU sink and shape-cpu refuse the same fills. A
palette change is a new parameter, never a re-lowering (AD-23): a theme picks another handle.

**Not decided here.** The other color spaces, the depth predicate for 3D maps, and the merge of adds to one parent.

## Strokes (normative; strokes story S1)

A stroke is a **centerline** (a path or a glyph run) plus an explicit **style**, an interned resource, and a paint
(Corey, 2026-10-21: no mode flag; the waist carries the centerline and the style, and `shape-geometry` holds the one
normative expansion into the region a stroke covers). Every difference between the formats that stroke is a field of
the style with a stated default; a front end sets each field to match its format. In real arithmetic, as N1 to N14.
`stroke.h` has the same text; tests, plants and `tools/gen-scene-vectors.py` cite the rules by number.

**SK1 The style.** `{width, width_space, join, miter_limit, cap, degenerate, dashes, dash_offset, dash_scope,
alignment, alignment_rule, width_profile}`, with pinned values (they are hashed): width space `LOCAL 0`, `DEVICE 1`;
join `MITER 0`, `ROUND 1`, `BEVEL 2` (PDF's `j`, so its lowering is the identity), `MITER_CLIP 3`, `ARCS 4`; cap `BUTT
0`, `ROUND 1`, `SQUARE 2` (PDF's `J`); degenerate subpath `DRAW 0` (SVG 2's shape, its square along SVG 2's effective tangent there, the direction of the nearest segment with extent, and along local `+x` only when the whole path has zero length), `OMIT 1`; dash
scope `PER_SUBPATH 0` (SVG 2), `PATH 1`; alignment `CENTER 0`, `INSIDE 1`, `OUTSIDE 2`; the alignment rule a
`GSHGEOM_Fill_Rule` (nonzero by default, D10). `gshscene_stroke_style_init` writes the defaults: width 1, local, miter,
miter limit 4 (SVG's), butt, draw, no dashes, offset 0, per subpath, center, nonzero, no width profile. A zero-filled
style is not valid (its miter limit 0 is out of range). The miter limit is the ratio of the miter's length to the
width, `1 / sin(θ / 2)`; past it (strictly greater) a miter becomes a bevel, and a miter-clip or arcs join is clipped at
`miter_limit * width / 2` from the vertex (SVG 2; `shape-geometry` states it in full). The enumerations' names
(`GSHSCENE_STROKE_JOIN_MITER`, ...) keep the `STROKE_` infix so that a cap is never read as a capability flag.

**SK2 Validity and bounds.** On the style as given: a non-finite double, an enumeration out of range, a negative
width or dashes counted but not given are `ERR_INVALID`; a non-NULL width profile is `ERR_UNSUPPORTED` (reserved, D8);
then `ERR_LIMIT`, decided after every other check: the width 0 or of magnitude in `[2^-64, 2^64]` (G14's band); the
miter limit in `[1, 2^16]`; each dash (by magnitude, so a negative one too) and the offset 0 or of magnitude in
`[2^-64, 2^64]`; at most `min(max_resource_elements, 65535)` dashes after an odd array is doubled. The bounds are met
on the style as given, before SK3 resets a field. Model units range too widely for a `2^16` bound (a scene in microns),
the band is the one gradients already keep finite, and `2^16 * 2^64` keeps a miter's tip far below `shape-geometry`'s
`2^100` coordinate limit; a source miter limit above `2^16` is lowered with a stated loss (joins sharper than about
`2^-15` rad bevel).

**SK3 Canonical form.** The store interns the canonical form, and the hash, the dump, equality and the accessor see
only it, so equal drawings are one handle and one hash. (a) Dashes (D5): an array with a negative length, or whose
lengths are all zero, is no dashes (solid); an odd array is repeated once. (b) A field that does not apply takes its
default: with no dashes the offset 0 and the scope per subpath; with a join other than miter, miter-clip and arcs the
miter limit 4; with center alignment the rule nonzero. (c) Nothing else is rewritten: the offset is kept as given (not
reduced modulo the period, which is the dash arithmetic's) and width 0 is kept (SK8). The canonical form is where D5's
meaning becomes one spelling: if the store kept the arrays as given, a writer, a cache keyed by hash and the diff would
all see two strokes where the drawing has one.

**SK4 The stroke item.** `gshscene_list_stroke(list, centerline, style, map, paint, layer, depth)` records
`GSHSCENE_ITEM_STROKE` (7): a centerline that is a path or a glyph run of the list's store (a 3D path, or a run whose
outline is one, is `ERR_UNSUPPORTED`: the stroke is planar, and a 3D map likewise, as 3D node maps are); a style of the
list's store; a map and a paint as a fill's (any paint kind, N1's bound, the palette and gradient rules unchanged); a
layer and a depth. The item's `fill_rule` is nonzero and its `group` zero (the alignment rule is the style's), and its
`style` names the style (NULL on every other kind). The list holds a reference on the centerline and on the style. A
stroke is one unit of its sequence (N11) and one element of a knockout group (T3), because its region is one union.

**SK5 What a stroke draws.** N7 with the stroke's covered region `R` in place of the shape: `apply(L, S, k * c,
SRC_OVER)` with `k` the coverage of `R` (exact area for a raster sink, AD-12) and `S` the paint as N7, N10, N14 and
G12 give it. `R` is defined once, by `shape-geometry`: the body within half the width of the centerline with joins and
caps; a local width built in the centerline's model space and then mapped; a device width built in target space around
the mapped centerline (a pixel kind's pixels; on a stream the CSS reference pixel, 1/96 inch, in the target's stated
unit, `gshscene_stroke_pixel_size`, so a hairline means one pixel on a raster and in a PDF alike); dashes cut before
the body; inside and outside alignment a centered stroke of twice the width clipped to the centerline's fill region
under the alignment rule, or to its complement, closed subpaths only; one union over the item. Orientation is AD-6's,
stated by `shape-geometry` and never here.

**SK6 Paint.** A stroke's paint is sampled in the item's local space for both width spaces: G6's chain unchanged. A
front end that wants SVG's `objectBoundingBox` on a stroke folds the geometry's box (without the stroke, as SVG says)
into `G`, as it does for a fill.

**SK7 A glyph run as centerline.** A stroke of a run always takes the general, unhinted route: the planning pass does
not route it, writes no `HINT_STATUS` for it and never refuses it for hinting (a `FONT_STRICT` run included). A run from
a font needs `GLYPHS_FROM_FONT`, as a fill does.

**SK8 Width 0 and hairlines.** Width 0 has no special meaning: the region is empty and nothing is drawn, and the
planning pass writes a `NOTICE`, reason `STROKE_ZERO_WIDTH`, as G10 does for a degenerate gradient. A hairline is device
width 1 (on a stream, one CSS pixel in its unit). No cap, flag or reason names a hairline: a sink whose native thin
line departs from exact area beyond its tolerance expands the stroke or reports `APPROXIMATED`, and a PDF writer that
writes `0 w` for it reports `APPROXIMATED`; neither fails strict.

**SK9 Not invertible.** A stroke draws nothing, with a `NOTICE`, reason `MAP_NOT_INVERTIBLE`, exactly when some map of
its chain (`T`, `I_1` to `I_k`, `M`, and `G` for a gradient paint) has a 2x2 linear part whose determinant is exactly
zero (G11's condition), for both width spaces. It never fails strict. One predicate, `gshscene_stroke_notice`, is the
planning pass's and every sink's: `MAP_NOT_INVERTIBLE`, then `STROKE_ZERO_WIDTH`, then `GRADIENT_DEGENERATE`. A stroke
that draws nothing requires no feature of a sink (SK11): it is never refused for its style.

**SK10 Bounds and damage.** A stroke occurrence's bounds are conservative, from the style alone: `h = width / 2 *
max(1, f_join, f_cap) * a` with `f_join` the miter limit for a miter (its tip is `1 / sin(θ / 2)` half widths from the
vertex, at most the limit), the miter limit plus one for miter-clip and arcs (a clipped corner is the limit's half
widths along the bisector and up to one across it, so `sqrt(L^2 + 1) <= L + 1` away: the limit alone would be passed
by a sharp clipped corner), and 1 otherwise; `f_cap` 3/2 for a square cap (above `sqrt 2`: a square cap's corner, and a degenerate subpath's square in any
orientation its effective tangent gives it, is at most `sqrt 2` half widths from its point); `a` 2
for inside and outside. A local width outsets the centerline's model box by `h` and maps it; a device width maps the
box and outsets it by `h` in target units. `h` is formed with its five roundings covered (scaled by `1 + 2^-50`) and
each outset rounded outward (TwoSum's error decides), so the bound is never below the real one; the corners' mapping is
a fill's. Dashes never enlarge it, and width 0 bounds nothing. `gshscene_stroke_bounds` is the one evaluation. N13 is
extended: a changed stroke node damages its bounds in both versions, and a changed foreground or palette damages the
strokes that paint with it; a stroke of a run from an unbound face is unbounded. `tests/unit/test_diff.cpp`'s oracle
restates where a stroke can reach from the geometry of its polyline (each segment's body, a miter's tip within its
limit, a miter-clip's clipped corners, discs for round joins and caps, a square cap's corners, a cap at any point for a
dashed stroke, twice the width inside and outside), sharing nothing with that function, and requires every pixel it
finds drawn differently to lie inside the damage.

**SK11 Sinks (S20).** A sink that draws strokes has bit `1 << 7` in its caps' `item_kinds`; without it a stroke is
refused, `ITEM_KIND`, as any kind is (shape-cpu until strokes story S4). `GSHSCENE_Sink_Caps.stroke_native` (carved from
`_reserved`) names one bit per feature the sink emits as a stroke: `JOIN_MITER`, `JOIN_ROUND`, `JOIN_BEVEL`,
`JOIN_MITER_CLIP`, `JOIN_ARCS`, `CAP_BUTT`, `CAP_ROUND`, `CAP_SQUARE`, `WIDTH_LOCAL`, `WIDTH_LOCAL_ANISOTROPIC` (a
local width under a map that is not a similarity: the elliptical pen), `WIDTH_DEVICE`, `DASH`, `DASH_PATH_SCOPE`,
`ALIGN`, `ALIGN_EVEN_ODD`, `DEGENERATE_DRAW`; `GSHSCENE_CAP_STROKE_EXPAND` (flag bit 12) says the sink can expand a
stroke exactly and draw it as a fill. The required features of a stroke (`gshscene_stroke_required`) are those its
canonical style uses: its join and its cap always, because whether the centerline has a vertex or an end is not
decided from the style; the width space, with `WIDTH_LOCAL_ANISOTROPIC` when the composed map's linear part is not a
uniform scale times an orthogonal matrix (`[a b; -b a]` or `[a b; b -a]`, compared exactly on the map
`gshgeom_map_compose_chain` forms, which every sink forms, so a sink and the plan agree bit for bit; a chain the
composer cannot form requires it, which is the conservative answer); `DASH` with dashes and `DASH_PATH_SCOPE` with
the path's scope; `ALIGN` off center and `ALIGN_EVEN_ODD` with an even-odd rule; `DEGENERATE_DRAW` for `DRAW` with a
round or square cap (with a butt cap SVG 2 draws nothing for a subpath of zero length, so draw and omit agree). A
stroke that draws nothing (SK8, SK9) requires nothing. `gshscene_caps_disposition`, which now takes the chain (the
enclosing instances' maps and the target's) because anisotropy is a property of it, decides after every other refusal
and before any approximation (an approximation returns first and would hide a refusal): every required feature native,
the stroke is drawn as a stroke; else, with `STROKE_EXPAND`, it is drawn with no entry (the sink expands that stroke
alone, and an exact expansion is what was asked, not an approximation); else it is `REFUSED`, reason `STROKE_STYLE`,
the missing features in the disposition's `detail` and the entry's `second`. A run from a font still needs
`GLYPHS_FROM_FONT`, and a gradient paint the gradient rules, before the style is decided.

**SK12 Hash and dump (the style).** Every format stays 1, and no existing hash or dump changes: no resource from before
strokes is a style. Resource kind 8, format 1: `u32` 1, `u32` 8, `f64` width, `u8` width space, `u8` join, `f64` miter
limit, `u8` cap, `u8` degenerate, `u8` alignment, `u8` alignment rule, `u8` dash scope, `f64` dash offset, `u64` the
dash count, `f64` each dash (canonical), `u64` the width profile's content hash (0 for none). The dump's payload is
`stroke width=<hex> space=N join=N miter=<hex> cap=N degenerate=N align=N rule=N scope=N offset=<hex> dashes=N
profile=<hash>` and a line `d <hex>` per dash. A style costs `GSHSCENE_BYTES_PER_DASH` (16) accounted bytes a dash.
`tools/gen-scene-vectors.py` restates the canonical form, the hash and the dump from this text alone. The **list item**:
`u8` 7, `u64` the centerline's content hash, `u64` the style's, `u64` the map's hash, the paint as a fill spells it,
`i32` layer, `f64` depth; its dump line `K res=<kind>:<hash> style=<hash> map=<hash> paint=N <r> <g> <b> <a>
space=<space> entry=E gradient=<hash> layer=N depth=<hex>`. The **version node** (kind 4) is spelled as a shape with
`u64` the style's hash after the resource's, and its dump line is a shape's with `K` for `S` and ` style=<hash>` after
the resource.

**SK13 The retained scene.** `GSHSCENE_NODE_STROKE` (4): a centerline, a style, a map, a paint, a layer and a depth,
set by an edit like any node (an upsert by key as for a shape, so a re-lowering keeps its ID and an identical one
publishes nothing), emitted as one stroke item with its placement composed, visited, diffed and damaged (SK10). The
walk hands a stroke to the same leaf callback as a shape, which reads the kind.

**SK14 Fill and stroke of one element.** Two items, or two nodes: the fill, then the stroke, in the order the format's
paint order gives (SVG's `paint-order`; markers are items of their own). A front end that needs them composited as one
(an element's opacity) wraps them in a group, as now. Nothing new is defined: it is SVG's and PDF's semantics exactly.

## Blend modes (normative; color-paint story P3)

In real arithmetic, as N1 to N14. `Cb`, `Cs` are straight colors, `cb`, `cs` premultiplied, `Ab`, `As` alphas; per
channel unless the mode is non-separable. Each rule is cited by tests, plants, the oracle and the vectors.
`tools/composite-oracle.py` states them a second time in exact rationals, sharing no code with the library; its one
new irrational is soft-light's root, exact when its argument is the square of a rational and otherwise evaluated at 80
decimal digits (an error below 1e-70) and flagged inexact, as a transfer is. `tools/gen-composite-vectors.py` commits
a table of the mixing function alone (`kMix`: every branch of B7 and B8 on both sides of each threshold and at it,
out-of-range values, B9's poles and the corners at `+-2^64`) and cases of blending groups (every mode in four blend
spaces, at alphas 0, 1/2, 1 and a non-dyadic one, under coverage, opacity and clips, nested, at the root over a
non-opaque target, out of range and at B9's saturation) to `tests/composite_vectors.h`. A blend vector is **exact**
only where every step of the double evaluation is exact (STOP-2, Corey 2026-10-21): the generator walks S11's order of
operations (shape-cpu's `render.h`) on the exact values, carrying a bound on how far each double can be from them in
the standard model of rounding, so a non-separable mode (`Lum`'s constants are not doubles) and a root that is not of a
square are never exact. It refuses a sample whose bound passes an eighth of the tolerance (a cancellation at large
magnitude, such as screen's `2^64 + 1 - 2^64`, which no double evaluation in this order meets) or reaches a
discontinuity of B10 (dodge's and burn's poles, ClipColor's and SetSat's equal components), and says how many; the
corners at `+-2^64` are then asserted finite and saturated for every mode rather than equal. `tests/ref_composite.h`'s evaluator agrees: bit for bit where marked exact, within `1e-12` of
`max(1, |v|)` otherwise.

**B1 The operators.** `GSHSCENE_Composite` keeps 0 to 12 (N4) and gains, numbered as COLRv1's `CompositeMode`,
`SCREEN 13`, `OVERLAY 14`, `DARKEN 15`, `LIGHTEN 16`, `COLOR_DODGE 17`, `COLOR_BURN 18`, `HARD_LIGHT 19`,
`SOFT_LIGHT 20`, `DIFFERENCE 21`, `EXCLUSION 22`, `MULTIPLY 23` (the **separable** modes), and `HUE 24`,
`SATURATION 25`, `COLOR 26`, `LUMINOSITY 27` (the **non-separable** modes); `COUNT` is 28. A group push (`list.h`,
`fixed.h`'s `gshscene_list_group_push_fixed`) and a group node take any of the 28; 28 and above are `ERR_INVALID`.
`gshscene_composite_string` names them as W3C does (`"multiply"`, `"color-dodge"`, `"hue"`, ...). COLRv1's modes 13 to
27 are W3C's one to one (COLRv1 states that its modes are taken from W3C's, and restates no formula), so a COLRv1
lowering of them is the identity; COLRv1's `HSL_` prefix names W3C's non-separable functions, which are defined on RGB
triples through `Lum`, `Sat` and ClipColor, not through an HSL conversion.

**B2 The blend operator.** For a blend mode `X` with mixing function `B_X`, on premultiplied `S = (cs, As)` and
`D = (cb, Ab)`: `Cs = sat(cs / As)` and `Cb = sat(cb / Ab)` (N3's un-premultiply with N1's saturation; an alpha of 0
gives the color 0), and
`op_X(S, D) = (cs (1 - Ab) + cb (1 - As) + As Ab sat(B_X(Cb, Cs)),  As + Ab (1 - As))`.
Where `As Ab = 0` the third term is 0 and `B_X` is not evaluated. This is W3C's two steps multiplied out: its blending
step `Cs' = (1 - Ab) Cs + Ab B(Cb, Cs)` followed by its source-over `co = As Cs' + Ab Cb (1 - As)`, `Ao = As + Ab (1 -
As)`; with `cs = As Cs` and `cb = Ab Cb`, `co = As (1 - Ab) Cs + As Ab B + Ab Cb (1 - As) = cs (1 - Ab) + cb (1 - As) +
As Ab B`. `op_X` is used in N5's `apply(D, S, m, op_X) = sat((1 - m) D + m op_X(S, D))` unchanged, so a fill (never a
blend: N7's operator is `SRC_OVER`) and N8's group composite need no new rule.

**B3 Bounded.** Every blend mode is bounded (N4): `op_X(0, D) = D`, because `As = 0` leaves `cb`. N13's damage and
`gshscene_composite_is_unbounded` are unchanged for them (0). COLRv1 agrees.

**B4 Coverage and opacity.** For `k > 0`, `kS` has the same `Cs` as `S`, so `op_X(kS, D) = k op_X(S, D) + (1 - k) D`
(each term of B2 is linear in `(cs, As)` for a fixed `Cs`, and the `cb` term gives `(1 - k) cb`). So `apply(D, S, m,
op_X) = sat(op_X(m S, D))` for `m` in `[0, 1]`, and N8's opacity `o` scales `S` and leaves `Cs` alone: coverage and
opacity commute with blending, and a coverage-weighted blend of one item (Canvas's per-draw
`globalCompositeOperation`, CSS `mix-blend-mode` on one element) is exactly a group of one (D4). The oracle's
self-test checks it for every mode.

**B5 Where (STOP-1, Corey 2026-10-21).** At the pop of a group with a blend mode, `S = o convert(G, Bg -> Bp)` and
`P' = apply(P, S, m, op_X)`, all in `Bp`, the enclosing layer's blend space, as N8 does for every operator; the group's
own `blend_space` governs how its children composite with each other. This is PDF's model (an object blends in the
blending space of the group it is composited into) and CSS's (a stacking context blends into its parent). The formulas
apply to the RGB components of that space as they are (encoded or linear-light, any gamut); no conversion to HSL or
another representation is made. COLRv1 blends in linear light and CSS in sRGB-encoded values; each is a blend space
(AD-23) the front end chooses.

**B6 Isolation.** A group is by default an isolated group in W3C's sense: its layer starts at `(0, 0, 0, 0)` (N6), its
content composites within it, and the group as a whole is blended with its backdrop at its pop. (A non-isolated group,
and a knockout group, are T1 to T7 below: defined, and refused by every sink until it composites them.) COLRv1's `PaintComposite` lowers
to an outer group holding the backdrop's items, then an inner group with the mode holding the source's (COLRv1 renders
source and backdrop "into bitmaps" and composites them, which is that). Every CSS stacking context, and SVG 2's
`isolation`, lower to a group; an element that does not isolate gets no group, so its blending group composites onto
the layer holding everything painted before it.

**B7 Separable modes.** Exactly W3C's, each color channel on its own: `MULTIPLY` `Cb Cs`; `SCREEN` `Cb + Cs - Cb Cs`;
`OVERLAY` `HardLight(Cs, Cb)` (backdrop and source exchanged); `DARKEN` `min(Cb, Cs)`; `LIGHTEN` `max(Cb, Cs)`;
`COLOR_DODGE` `0` if `Cb = 0`, else `1` if `Cs = 1`, else `min(1, Cb / (1 - Cs))`; `COLOR_BURN` `1` if `Cb = 1`, else
`0` if `Cs = 0`, else `1 - min(1, (1 - Cb) / Cs)`; `HARD_LIGHT` `Multiply(Cb, 2 Cs)` if `Cs <= 1/2`, else `Screen(Cb, 2
Cs - 1)`; `SOFT_LIGHT` `Cb - (1 - 2 Cs) Cb (1 - Cb)` if `Cs <= 1/2`, else `Cb + (2 Cs - 1) (D(Cb) - Cb)`, with `D(Cb) =
((16 Cb - 12) Cb + 4) Cb` if `Cb <= 1/4`, else `sqrt(Cb)`; `DIFFERENCE` `|Cb - Cs|`; `EXCLUSION` `Cb + Cs - 2 Cb Cs`.
Every test (`=`, `<=`) is on the exact real values.

**B8 Non-separable modes.** Exactly W3C's, on the triple `C = (r, g, b)`: `Lum(C) = 3/10 r + 59/100 g + 11/100 b` (W3C's
`0.3, 0.59, 0.11` as the exact rationals; they sum to 1); `ClipColor(C)`: `L = Lum(C)`, `n = min(C)`, `x = max(C)`, all
three taken **on entry**; if `n < 0`, `C = L + (C - L) L / (L - n)`; then, with the same entry `x` and `L`, if `x > 1`,
`C = L + (C - L)(1 - L) / (x - L)` (W3C's pseudo-code read literally), each step only when the components are not all
equal (B10); `SetLum(C, l)`: `C + (l - Lum(C))`, then
ClipColor; `Sat(C) = max(C) - min(C)`; `SetSat(C, s)`: with `Cmax`, `Cmid`, `Cmin` the components holding the largest,
middle and smallest values on entry: if `Cmax > Cmin`, `Cmid = (Cmid - Cmin) s / (Cmax - Cmin)` and `Cmax = s`, else
`Cmid = Cmax = 0`; then `Cmin = 0`. `HUE` `SetLum(SetSat(Cs, Sat(Cb)), Lum(Cb))`; `SATURATION` `SetLum(SetSat(Cb,
Sat(Cs)), Lum(Cb))`; `COLOR` `SetLum(Cs, Lum(Cb))`; `LUMINOSITY` `SetLum(Cb, Lum(Cs))`.

*SetSat does not depend on how ties are named.* If two components are equal and called `Cmid` and `Cmax`, the formula
gives `Cmid = (Cmax - Cmin) s / (Cmax - Cmin) = s = Cmax`; if they are `Cmin` and `Cmid`, it gives `Cmid = 0 = Cmin`; if
all three are equal, every component is 0 either way. So both names get the same value, and any order that sorts the
components gives one answer; the oracle checks every order.

*ClipColor keeps `Lum`.* Each step maps `C` to `L + (C - L) k` for one real `k`, and `Lum` is linear with coefficients
summing to 1, so `Lum` of the result is `L + (L - L) k = L`: `Lum(SetLum(C, l)) = l` exactly, for every real input.

**B9 The saturation after `B`.** `sat` is applied to each component of `B_X(Cb, Cs)` before it is weighted (B2); it is
one point added to N1's list. With N1's other points every value of B2 is finite: its inputs are bounded by `2^64`, and
every intermediate of B7 and B8 is below `2^200` in magnitude (the table in N1; the worst is soft-light's `16 Cb^3 <=
2^196`). On the entry triple, ClipColor's quotient `(C - L) / (L - n)` lies in `[-1, 100/11]` and `(C - L) / (x - L)` in
`[-100/11, 1]`, because `L - n` and `x - L` are each at least `11/100 (x - n)`; when both steps apply (out of range
only), the second step's numerator is the first step's result, still below `2^70`, over `x - L >= 11/100`, so the result
is below `2^140`. The two quotients that are unbounded in the reals, dodge's `Cb / (1 - Cs)` and burn's `(1 - Cb) / Cs`,
are evaluated without forming them past the bound: when `|numerator| >= 2^66 |denominator|` the quotient is taken as
`sign 2^66`, and every later step (`min(1, .)`, `1 - .`, `sat`) then gives exactly what the real value gives (`2^66 |d|`
is a power-of-two scaling, so the comparison is exact). No infinity and no NaN can arise. The table's corners are
committed: every mode at `Cb, Cs` in `{-2^64, 0, 1, 2^64}`.

**B10 Out of range.** Components may be negative or above 1 (N1 bounds them by `2^16` at record and `2^64` after). B7
and B8 apply **literally to every real input**, and nothing is clamped to `[0, 1]` that W3C does not clamp.
Multiply, screen, overlay, hard-light, darken, lighten, difference and exclusion are polynomials, `min`, `max` or
`|.|`, defined everywhere. Color-dodge: with `Cb != 0` and `Cs != 1`, `q = Cb / (1 - Cs)` is a finite real of either
sign, and `min(1, q)` keeps a negative `q` (`Cs > 1` with `Cb > 0`, or `Cs < 1` with `Cb < 0`). Color-burn: with `Cb !=
1` and `Cs != 0`, `q = (1 - Cb) / Cs`, and `1 - min(1, q)` exceeds 1 when `q < 0`. Soft-light: the root is taken only
where `Cb > 1/4`, so its argument is always positive; no root of a negative arises anywhere, and `D`'s polynomial
covers every `Cb <= 1/4`, negatives included. ClipColor: `n <= L <= x` always (`Lum` is a convex combination), so `L - n
> 0` whenever `n < L`; in the one remaining case, `n < 0` with `n = L` (all three components equal `L`), `C - L = 0` and
the step leaves `C` unchanged, as it does for `x > 1` with `x = L`. This defines W3C's `0 / 0` form by the value of its
numerator's factor, the limit along the ray `C = L`. In the reals `n = L`, `x = L` and `n = x` are one condition, and it
is stated as `n = x` (all components equal), which a double evaluation decides exactly: `L` computed in doubles from
`0.3, 0.59, 0.11` can differ from an all-equal triple's components by a rounding, and a test of `n = L` would then take
the quotient of two roundings. For the same reason an evaluation in floating point forms ClipColor's offsets and
denominators from the differences `C - n`, which are exact near the ray: `L - n = Lum(C - n)`, `x - L = (x - n) - (L -
n)` and `C - L = (C - n) - (L - n)`, equal in the reals to the literal ones. `Lum(C - n)` is a sum of terms that are
not negative, so it is positive whenever the components are not all equal, while `Lum(C) - n` rounded can be 0 (or
negative) a rounding away from the ray, which takes the literal step to `0 / 0` (shape-cpu's fuzzer found it); and
the quotients `(C - L) / (L - n)`, whose real values are bounded (B9), are then quotients of accurate small numbers.
A step is taken only when its denominator, so formed, is positive, which is also how the all-equal form is decided
(every difference is 0). Off the ray the step does apply, so ClipColor is discontinuous there (out of range only):
the step takes `n` to 0 however near the triple is to grey. SetSat divides only when `Cmax > Cmin`. The modes'
discontinuities in the reals are where W3C puts them: dodge at `Cs = 1` approached from above with `Cb > 0` (out of
range), burn at `Cs = 0` approached from below with `Cb > 1` (out of range), and ClipColor's and SetSat's quotients
where components meet. They are kept, and the vectors test the exact point and each side.

**B11 Alpha.** The result's alpha is source-over's for every mode (B2), so the modes differ only in color. `PLUS`'s
clamp (N4) is not extended to them; nothing in B2 is clamped but by `sat`.

**B12 Normal.** There is no `NORMAL` value. W3C's `normal` (`B = Cs`) makes B2 `cs (1 - Ab) + cb (1 - As) + As Ab Cs =
cs + cb (1 - As)`, `SRC_OVER` exactly, so a front end lowers `normal` to `SRC_OVER`, or to no group (one spelling, one
hash). The oracle's self-test checks the identity.

**B13 Sinks.** A sink claims each mode it composites in `caps.composites` (bit `1 << op`; 28 values fit in 32 bits). One
it lacks is `REFUSED`, reason `COMPOSITE`, by the planning pass (S6, `gshscene_caps_disposition`), never drawn as
another mode; a sink claims an operator only once its compositor knows it (shape-cpu named its operators before this
enum grew, so that it never claimed a mode it would have drawn as `CLEAR`). A group blended in another space without
`BLEND_SPACES` is `APPROXIMATED` (`BLEND_SPACE`), as any group is. A writer spells the mode by its name. The test sink
records it (its group record's `composite=N`). No new reason, class or cap flag.

**B14 Hash and dump.** No spelling changes. The list's and version's `u8` composite holds 13 to 27, the dumps print the
number as before, and every format stays 1. No committed hash changes: no list before the modes could hold a value
past 12.

## Transparency groups (normative; PDF's model; color-paint story P3)

PDF's transparency model (ISO 32000-1, section 11) has two kinds of group the N-rules do not: a **non-isolated** group,
whose content composites with what was painted before the group, and a **knockout** group, each of whose elements
composites with the group's initial backdrop rather than with the elements before it; and a group may have a **soft
mask**. They are defined here in full so that a later sink composites them without a change to the waist. Every sink
refuses them until it does (S19). In real arithmetic, as N1 to N14. `tools/composite-oracle.py` states T1 to T7 a second
time in exact rationals and checks them against itself and against ISO 32000's own straight-color formulas (written there
independently, `pdf_literal`), and `tools/gen-composite-vectors.py --groups` commits known answers for the four
combinations of isolated and knockout, in range and with blend modes, as `tests/group_vectors.h` (installed for sinks);
`tests/ref_composite.h`'s `evaluate_general` agrees with them.

**T1 The fields.** A group (`GSHSCENE_Group`, a group node) has `isolation` (`ISOLATED` 0, the default, or
`NON_ISOLATED` 1), `knockout` (0, the default, or 1) and `soft_mask` (NULL, the default, or a picture of the list's
store, which the list or node holds; a reserved slot, T7). The defaults are every group that existed before the fields,
so formats stay 1: a default group's hash and dump are spelled exactly as before, and a group with a field that is not
the default sets bit 7 of its `u8` composite and writes `u8` flags (1 non-isolated, 2 knockout) and `u64` the mask's
content hash after it, and ` isolation=N knockout=N mask=<hash>` in its dump line (list.h, version.h). Equality compares
them, the mask by handle. A mask's picture counts toward the picture nesting as an instance's does; the walk of a list's
expansion does not enter it.

**T2 Shape and opacity are kept separate.** Each element painted into a layer has a **shape** `f` in `[0, 1]`, how much
of the pixel it covers: a fill's geometric coverage `k` times the clips in force (N7, N9) (and later a soft mask of
PDF's alpha-is-shape kind); and an **opacity source** `S`, premultiplied, whose alpha `q` is in `[0, 1]`: the paint's
alpha (N7, N10, N14, G12), a group's opacity (and later an opacity soft mask). The two are multiplied only where a formula
below multiplies them (`f q`). **The waist never merges them**: no item, field, record call or sink interface folds a
shape into an opacity or the reverse, because a knockout group needs the shape alone, and a later shape or opacity mask
must reach the right one.

**T3 The element step.** In a layer with value `L` and initial backdrop `L0` (T4), an element of shape `f`, operator
`op` and source composites onto its **backdrop** `Bk`, which is `L` (the layer as the elements before it left it) in a
group that is not knockout, and `L0` in a knockout group. For PDF's operators (source-over and the blend modes; W3C's
`normal` is source-over, B12), with `A = (a, Aa)` the source with both its shape and its opacity applied (`A = f S`, `Aa
= f q`):
`L' = sat((1 - f) L + (f - Aa) Bk + T)`, with `T = a` for source-over and `T = a (1 - Ab) + Aa Ab sat(B(Cb, Ca))`
(`Cb`, `Ca` un-premultiplied as B2 does) for a blend mode, and alpha `(1 - f) AL + (f - Aa) Ab + Aa`. For every other
operator, `L' = sat((1 - f) L + f op(S, Bk))` with `S` the opacity source alone. The layer's **group alpha** `Ag` (the
group's own coverage, without the initial backdrop) starts at 0 and steps alike, with 0 for its backdrop in a knockout
group: `Ag' = (1 - f) Ag + (f - Aa) Agb + Aa` for PDF's operators and `(1 - f) Ag + f alpha(op(S, (0, 0, 0, Agb)))` for
the others (an operator's alpha depends only on alphas), `Agb = Ag` (not knockout) or 0 (knockout). The **group shape**
`fg` starts at 0 and is `fg + f - fg f` after each element. This is PDF's formula multiplied out on premultiplied values:
PDF writes `Ai Ci = (1 - fs) Ai-1 Ci-1 + (fs - As) Ab Cb + As ((1 - Ab) Cs + Ab B(Cb, Cs))` and `Ai = (1 - fs) Ai-1 + (fs
- As) Ab + As`, with `(Cb, Ab)` the element's backdrop as above.

**T4 The initial backdrop.** An isolated group's layer starts at `(0, 0, 0, 0)` (N6). A non-isolated group's starts at
the backdrop it has as an element of its parent (T3: the parent's value, or the parent's initial backdrop when the parent
is knockout), converted (N3) from the parent's space into the group's. The root is the target and is neither.

**T5 The group's result.** At the pop, the group's own result takes the initial backdrop out: `G = sat(L - (1 - Ag) L0)`
on the color channels, with alpha `Ag` (for an isolated group `L0 = 0` and `G = L`). This is PDF's `C = Cn + (Cn - C0)
(A0 / Agn - A0)` on premultiplied values. `G` is converted to the parent's space (N3) and is an element of the parent: for
PDF's operators with shape `m fg` and `A = m o G` (PDF's group as an object: shape `fg`, opacity `Ag / fg`), and for every
other operator with shape `m` and `S = o G` (it acts on its whole clip region, N8, N13), where `m` is the product of the
clips in force at the push and `o` the opacity.

**T6 The N-rules are the default case.** With every group isolated and not knockout, `L0 = 0` and `Bk = L`, so T3 is N5's
`apply(L, S, f, op)` (for PDF's operators `(1 - f) L + (f - Aa) L + T = op(A, L)`, which is `apply` with `m = f` by B4);
`Ag` steps exactly as the layer's alpha does from 0, so `G = L`; and T5's element is N8's composite: for PDF's operators,
`(1 - m fg) P + (m fg - m o AG) P + T(m o G)` is `op(m o G, P)` = `apply(P, o G, m, op)` by B4, whatever `fg`; for the
others it is N8 word for word. A fill is N7. So N5 to N9 are T1 to T7 at the defaults exactly, and the generator requires
the general model to give every committed case of `composite_vectors.h` in exact rationals.

**T7 The soft mask (reserved).** The slot names a picture. What a mask means beyond that (PDF's alpha or luminosity soft
mask computed from a transparency group, its backdrop color, its transfer function, and whether it acts as shape or as
opacity, PDF's alpha-is-shape) is decided with the sink work that applies it; in T3 it will multiply the group's opacity
source (or its shape) at the group's pop. Until then no sink claims it.

**S19 Sinks.** A sink claims `GSHSCENE_CAP_GROUP_NON_ISOLATED`, `_KNOCKOUT` and `_SOFT_MASK` for what it composites;
without them the planning pass refuses the group (`GROUP_ISOLATION`, `GROUP_KNOCKOUT`, `SOFT_MASK`, in that order, after
`COMPOSITE`), so it is never drawn as a default group. shape-cpu and the test sink claim none of the three.

## Gradients (normative; color-paint story P1)

In real arithmetic, as N1 to N14. Each rule is in `paint.h` too; tests, plants, the oracle and the vectors cite them.
`tools/gradient-oracle.py` states them a second time, sharing no code with the library: exact rationals for linear,
exact arithmetic in Q(sqrt D) for radial (every comparison decided by signs), and a 110-digit `decimal` angle for sweep
with a stated bound (1e-60 on `t` for `|a1 - a0| >= 1e-6` turn); `tools/gen-gradient-vectors.py` commits its answers
as `tests/gradient_vectors.h` (installed for sink libraries), rejecting a sample point within 1e-30 of a
discontinuity, and `tests/ref_composite.h`'s evaluator agrees with them, bit for bit where the case is exact and within
1e-12 otherwise.

**G1 The resource.** `{kind, geometry, map, stops, extend, space, premultiplied}`: `kind` linear, radial or sweep; `map`
a valid 2D map from gradient space to the local space of the item that paints with it (before the item's map);
`space` an N1 space, the interpolation space (zero: the target's); `premultiplied` 0 or 1. Every double is finite.
Equal content is one handle; a hash never decides equality.

**G2 The color line.** `n` stops, `0 <= n <=` G14's bound, each an offset (any finite double) and a stop
color, sorted stably by offset when interned; hash, dump and equality see the sorted line. `o_first`, `o_last` and
`P = o_last - o_first` are of the sorted line.

**G3 Stop colors.** A stop color is a solid, foreground or palette paint by N7, N10 and N14's rules. Nothing is
converted at intern. At draw time every stop is resolved (a foreground or palette stop against the drawing list's
foreground and palette) and converted by N2/N3 into the interpolation space (`space`, or the target's resolved space
when zero; a stop's zero space is the target's too) by `gshscene_gradient_resolve`, which every sink calls once per
gradient per render; each component is within `GSHSCENE_RESOLVE_TOLERANCE * max(1, |c|)` (1e-13) of N3's exact value,
measured against the oracle (the worst measured is about 4.5e-15). A stop already in the interpolation space is copied
exactly. The conversion saturates as N3 says (N1), so a stop at the bound converted into a space whose transfer
raises it is `2^64` exactly. A missing palette entry refuses the fill (`PALETTE_ENTRY`). A solid stop in the target's
space is allowed only in a gradient whose space is zero.

**G4 The color function.** A stop `(r, g, b, a)`'s interpolant is `(a r, a g, a b, a)` (flag 1) or `(r, g, b, a)`
(flag 0). With no stops `C` is transparent black; with one, that stop's interpolant everywhere. Otherwise, among stops
of one offset the first is used below it and the last at and above it, and between adjacent distinct offsets `C` is the
linear interpolation of the interpolants; `C(o_last)` is the last stop's. The straight color is `C` (flag 0) or `C`
un-premultiplied (flag 1; alpha 0 gives `(0, 0, 0, 0)`), saturated as every un-premultiply is (N1). In real arithmetic
it is a weighted mean of bounded stop colors and never reaches `2^64`; a double evaluation can (an alpha a few
subnormal steps wide rounds by a large part of itself), and the saturation is what keeps it to the definition.

**G5 Extend.** Outside `[o_first, o_last]` with two or more stops: `PAD` takes the first stop below and the last
above; with `P > 0`, `REPEAT` takes `C(o_first + ((t - o_first) mod P))` and `REFLECT` `C(o_first + P - |((t -
o_first) mod 2P) - P|)`; with `P = 0`, `PAD` is G4's step and `REPEAT` and `REFLECT` draw nothing.

**G6 Where a point gets its color (AD-24).** For a fill whose chain is the target map `T`, the instance maps, the item
map `M` and the gradient map `G`, target point `q` takes the gradient-space value at `p = (T I ... M G)^-1 (q)`. A sink
samples where its own coverage definition says.

**G7 Linear.** `p0`, `p1` and an isoline direction `d` (zero: perpendicular to `p1 - p0`). With `n_d = (-d_y, d_x)`,
or `p1 - p0` when `d` is zero, `t = ((p - p0) . n_d) / ((p1 - p0) . n_d)`; an exactly zero denominator is degenerate.
COLRv1's three points lower to `d = p2 - p0` exactly, and Canvas's and SVG's two points to `d = 0`.

**G8 Radial: the two-circle cone.** With `c(w) = c0 + w (c1 - c0)` and `r(w) = r0 + w (r1 - r0)`, the value at `p` is
the color at the largest `w` with `r(w) > 0` and `|p - c(w)| = r(w)`; with none, `p` gets nothing. Negative radii are
COLRv1's and are read through `r(w) > 0`. Equal circles are degenerate. Canvas's and COLRv1's cones are this one; SVG
2 adopted it, and SVG 1.1's focal clamping is `vector`'s lowering.

**G9 Sweep (AD-6).** `theta(p)`, the angle of `p - c` in `[0, one turn)` from `+x` toward `+y` (0 at `p = c`), in the
source's unit; `t = (theta - a0) / (a1 - a0)`. With `a0 = a1`, `REPEAT` and `REFLECT` draw nothing and `PAD` is the
first stop below `a0` and the last at and above it.

**G10 Degenerate.** Never refused at record. G7's zero denominator, G8's equal circles and G9's `a0 = a1` under repeat or
reflect draw nothing whatever the stops; G5's `P = 0` with two or more stops under repeat or reflect draws nothing.

**G11 Not invertible (AD-24's open question, Corey's D7).** A gradient fill's chain is not invertible exactly when some
map of it (`T`, each instance map, `M`, `G`) has a 2x2 linear part whose determinant is exactly zero, by
`shape-geometry`'s exact orientation of that map's own doubles; no composition is formed, so a determinant too small for
a double is still nonzero. The fill draws nothing. (A retained scene's group maps are composed into the item maps of
the list it emits, and the list's chain is what G11 reads.)

**G12 The fill.** N7 with the straight color at `t` in the interpolation space, its alpha multiplied by the paint's
`rgba[3]`, converted to the layer's blend space and premultiplied; where G8, G10 or G11 says nothing, the source is
`(0, 0, 0, 0)`. A picture's foreground and palette stops resolve against the list that draws it.

**G13 Sinks.** By handle (`gshscene_resource_gradient`, `gshscene_gradient_resolve`); the scene bakes no ramp. The caps'
`gradient_kinds` name the kinds a sink draws; another kind is refused (`GRADIENT`) by the planning pass, so a sink
without gradients never draws one wrong. Without `GRADIENT_SPACES` a stated space other than the target's is
approximated (`INTERPOLATION_SPACE`). The planning pass reports G11, then G10, as one `NOTICE` per fill
(`MAP_NOT_INVERTIBLE`, `GRADIENT_DEGENERATE`) by the one predicate `gshscene_gradient_notice`, which a sink also skips
by; a notice never fails a render. N13 extends to gradients: a changed foreground damages the shapes whose gradient has
a foreground stop, and a changed palette those with a palette stop.

**G14 Limits (color-paint story P2, Corey 2026-10-21).** Every double of the geometry of the gradient's kind, every
entry of its map (the 2x2 linear part and the translation) and every offset is zero or of magnitude in `[2^-64, 2^64]`
(`GSHSCENE_GRADIENT_VALUE_MIN`, `_MAX`), and there are at most `min(max_resource_elements, 65535)` stops
(`GSHSCENE_GRADIENT_STOPS_MAX`, COLRv1's `uint16` count). Past either, intern is `ERR_LIMIT`; a non-finite value or
another invalid field is `ERR_INVALID`, decided first. The bounds are derived from G4 to G9 so that a double evaluation
stays finite over the **sampling domain**, gradient-space points with both coordinates in `[-2^128, 2^128]`
(`GSHSCENE_GRADIENT_DOMAIN`):

| Quantity | Bound |
| --- | --- |
| an allowed value | a multiple of `2^-116`; a nonzero difference of two in `[2^-116, 2^65]` |
| G7's denominator `(p1 - p0) . n_d` | at most `2^131`; nonzero, at least `2^-232` (a sum of products of multiples of `2^-116`) |
| G7's numerator `(p - p0) . n_d` | at most `2^195`, so `|t| <= 2^427` |
| G8's `a = |c1 - c0|^2 - (r1 - r0)^2` | at most `2^132`; nonzero, at least `2^-232` |
| G8's `b = (p - c0) . (c1 - c0) + r0 (r1 - r0)`, `c = |p - c0|^2 - r0^2` | at most `2^196` and `2^260` |
| G8's `b^2 - a c` and, with `a != 0`, the roots `(b +- sqrt(b^2 - a c)) / a` | at most `2^393` and `2^430` |
| G9's `t = (theta - a0) / (a1 - a0)` | at most `2^181` |
| G5's `t - o_first`, `P`, `2P`, `(t - o_first) / P` | at most `2^431`, `2^66`, `2^66` (nonzero `P` at least `2^-116`), `2^547` |
| G4's fraction `(t - o_i) / (o_j - o_i)` | in `[0, 1]` |

With `a = 0`, G8's one root `w = c / 2b` grows without bound only as `b` goes to zero, at the edge of the cone, where a
sink states what it does. A sink evaluates inside the domain, or refuses the fill as its own limit (S6). A solid stop's
color is bounded by N1 (`2^16`) and refused with these, `ERR_LIMIT` after every validity check; its conversion is
saturated (G3).

**Recording without floats.** `gshscene_store_intern_gradient_fixed` (fixed.h) takes COLRv1's numbers for a run from a
font: integer geometry over `2^bits` (linear's rotation point becomes `d = p2 - p0`, a sweep's half turns become `v *
180 / 2^bits` degrees, each exact in a double), palette and foreground stops, and a design map of ops; the gradient map
is `design_to_model(size / upem, g) . T` about the run's first glyph `g`, where a design point lands where the glyph's
outline puts it. `gshscene_list_fill_gradient_fixed` fills a run with a gradient and a fixed alpha.

## The retained scene

The retained scene is a builder above the waist (AD-1, AD-9): it emits lists over interned resources and
nothing below it knows it exists. Five pieces, one header each: `version.h`, `edit.h`, `scene.h`, `diff.h`
and, for the numbers, `core.h`.

**A version** is immutable, reference counted and shareable. It is a sorted array of node pointers, so
nodes are shared between versions by reference count and the index is the version's own. A node is a
group (children in draw order, a map, an opacity, a blend space, a composite operator, an ordering context, a
layer and a depth, an optional clip), a shape (a path or glyph run under a map, a rule, a paint, a layer and a
depth), a stroke (a centerline under a map with a stroke style, a paint, a layer and a depth; SK13) or an instance
(another node of the version, drawn again).
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
bounds are its path's exact bounds mapped by the composed map, outset by the AA support and rounded outward; a
stroke occurrence's are SK10's.
Where bounds are unknowable (a glyph run named by a font, a 3D path, a map that cannot be applied) the
damage says unbounded. `tests/unit/test_diff.cpp` checks it against an oracle that shares none of its
structure: for random scenes and edits it requires every pixel whose ordered drawing records differ to lie
inside the damage.

**The store's total bytes** are bounded by `max_bytes`, accounted by a formula (`resource.h`) so the limit
means the same on every target.

Gaps that this design knows about and where they are recorded: the spine's Deferred table (a font as an
opaque key, fixed picture paints, intersection-only clips, per-glyph maps, affine and 2D maps, the linear
cost of publish and diff, the granularity of a merge, single-rectangle damage, what is met at emission).

## The sink contract (normative)

How a closed list and its resources are rendered by a backend the caller does not need to know
(AD-11, story 13a). It is written for every backend the family will have: the CPU reference
(shape-cpu), GPU sinks (Vulkan, WebGPU, WebGL), and writers whose target is a document (SVG and PDF
export, a plotter's or a machine's program). `sink.h` has the same text; tests, plants and the
conformance suite (`tests/sink_conformance.h`) cite the rules by number.

- **S1 Request.** A render is a `GSHSCENE_Render_Request`. The all-zero request with a closed list, a
  target and a report is a whole-frame, best-effort, uncancellable render, and `gshscene_sink_render`
  completes it in one call on every sink that can block (S13). Unknown flag bits and nonzero reserved
  bytes are `ERR_INVALID`.
- **S2 Sink.** A sink is made by `gshscene_sink_create` from an ops table and an implementation
  pointer; the caller holds a `GSHSCENE_Sink *` and does not know which sink it is. Capabilities are
  per sink and target (`gshscene_sink_caps`): what a sink draws depends on the target's kind, format
  and device, and the contract layer asks before every render.
- **S3 Job.** `GSHSCENE_Job` is the caller's storage. `begin` starts a render, `poll` observes it and
  never blocks, `wait` blocks until it completes (S13). The sink ends a job with
  `gshscene_job_complete`, which seals the report, releases the surface and calls `on_complete`
  exactly once, before the job reads as done. From `begin` until completion the target's surface
  belongs to the sink: a second `begin` on it, on the same sink, is `ERR_INVALID`, and at most
  `max_jobs` renders are in flight on one sink (`ERR_LIMIT`). A sink with no executor completes
  inside `begin`.
- **S4 Target.** A target describes the surface; the signature never names a raster. Its kind is a
  raster, a stream (a document a writer emits) or a reserved GPU kind (`ERR_UNSUPPORTED`). Its map
  takes list space to target units: pixels for a pixel kind, and for a stream the unit the target
  states. Extent, AA filter and damage are pixel-kind fields, zero for a kind with no pixels.
  Orientation and encoding belong to the sink: the map is always model to target with y down (AD-6),
  a sink whose framebuffer is y-up flips internally, and no caller ever flips; `blend_space` zero
  takes the surface's description (an undescribed raster, and a document, are sRGB), and `encoding`
  says whether the stored values are encoded by the sink or by hardware. A sink composes maps root to
  leaf, the target's last (AD-24). Inside a damage rectangle a render equals the whole render, and
  outside it nothing is touched. So whether an item is refused for one of the sink's own limits does
  not depend on the damage: a limit is met over what the whole render would draw of the item, and a
  damage-limited render refuses (or, under strict, stops at) the same items as the whole render.
- **S5 Report.** One report shape for every sink: `report.h`.
- **S6 Refusal and the planning pass.** A sink never drops an effect silently: an item is drawn as
  stated, drawn approximately with an `APPROXIMATED` entry, or skipped with a `REFUSED` entry. The
  contract layer walks the list and its pictures against the caps before calling the sink, and
  writes every refusal and approximation the caps decide and every depth tie; one predicate,
  `gshscene_caps_disposition`, decides for the plan and for the sink. A refused push or instance
  skips its block. Best effort returns `OK`. Strict refuses a planned refusal with `ERR_REFUSED`
  before the sink is called, so the target is untouched; a refusal found while drawing (a sink's
  resource limit) stops the render there, with the finished prefix in the target, and is
  `ERR_REFUSED` too. An approximation never fails a render.
- **S7 Cancel.** The token (`cancel.h`) is the caller's, set from any thread. The unit and the bound
  are the sink's, declared in its caps: between units (a check before the first unit and after each
  unit's progress call, so a token set during the progress call of unit k is seen before unit k + 1),
  or once before submission (a GPU sink). `cancel_work_bound` is the most work, in the measure the
  caps name, between two checks; the job counts it. A cancelled render returns `ERR_CANCELLED` with a
  prefix of paint order in the target and no half-built group.
- **S8 Conflation.** `GSHSCENE_TARGET_SEAMLESS` is refused with `ERR_REFUSED` before the sink is
  called, with or without strict, with one render-level entry (AD-29).
- **S9 Memory and static resources.** With `CALLER_MEMORY` the sink allocates nothing during the
  render, sizing it included, and takes its scratch from the workspace, whose exact need
  `gshscene_sink_workspace_bytes` gives. Less (or none) is `ERR_LIMIT` with the target untouched and
  no allocation at all: the summary states no need (`workspace_needed` 0), since a render does not
  allocate to measure it, and the failure's reason names the query, which gives it (the query is not
  a render and may allocate). A sink's `workspace_bytes` op, called with the flag, sizes the render
  inside the workspace without allocating and gives the exact need or, when it does not fit, a bound
  past it; called without (the query's second call), it may allocate. A sink without the capability
  refuses the flag. `prepare` uploads or caches a list's static resources by id until `evict` or destroy; the
  summary states the static bytes and the frame's peak scratch.
- **S10 Lifetime with work in flight.** The caller keeps the list, its resources, the target, the
  report, the workspace and the token alive and unmodified until the job completes. A sink keeps no
  reference past completion except what `prepare` gave it.
- **S11 Arithmetic.** The CPU reference's: shape-cpu (story 13b).
- **S12 Determinism.** A sink's output depends on the list, the target and the request only.
- **S13 Completion without blocking.** A sink whose caps lack `CAN_BLOCK` (a browser's main thread)
  returns `ERR_UNSUPPORTED` from `wait` and from the one-call render before anything is begun; its
  completion is seen by `poll` and `on_complete`.
- **S14 Thread affinity.** A sink whose caps say `THREAD_AFFINE` (a GL context) takes `begin`, `poll`
  of its jobs, `wait`, `readback`, `prepare`, `evict` and `destroy` only from its creating thread, and
  the contract refuses a call from another with `ERR_INVALID`. The token, lists, resources and
  versions are usable from any thread in every case. (The calling thread is told apart by the address
  of a `_Thread_local` byte that is never written: no global state.)
- **S15 Readback.** An optional copy of a completed render's pixels of the extent into a caller
  raster, so a test can hash a GPU's output.
- **S16 Geometry reaches a sink in model space.** The contract hands a sink model-space `double`
  geometry with curves intact; nothing in `sink.h` composes, flattens or rounds. A writer emits
  curves and dedupes resources by id. The planning pass composes maps to decide (a run from a font's
  route), and never hands composed geometry to a sink.
- **S20 Strokes** (strokes story S1; SK1 to SK14 above). A stroke reaches a sink by its centerline and style handles in
  model space: nothing in the contract expands, flattens or offsets it. A sink reads the style with
  `gshscene_resource_stroke_style`, decides each stroke with `gshscene_caps_disposition` (SK11) and skips by
  `gshscene_stroke_notice` (SK9). A writer emits a native stroke as a stroke, deduped by the style's id, and expands
  only a stroke whose required features it lacks; a toolpath sink follows the centerline and refuses what its caps
  lack; a GPU sink later strokes natively within its tolerance or expands. A device width on a stream is in CSS pixels.
- **S17 Gradients** (color-paint story P1). A gradient reaches a sink by handle. A kind not in the caps'
  `gradient_kinds` is refused (`GRADIENT`) by the planning pass, so strict fails before the sink is called; without
  `GRADIENT_SPACES` a stated interpolation space other than the target's is approximated; a palette stop the palette
  lacks refuses the fill; a gradient fill that draws nothing (G10, G11) is a `NOTICE`, which never fails a render. A
  sink converts the stops with `gshscene_gradient_resolve` once per gradient per render, by id, against the request's
  list's foreground and palette and the target's resolved space; a writer emits a gradient once per id.

**The test sink** (`dump_sink.h`) is the second backend AD-11 asks for, independent of shape-cpu. It
composites nothing: it writes a dump of every unit (a box per fill and clip: the resource's model
bounds through the composed map, in target units) and stamps one marker rectangle per drawn unit
into an RGBA8 raster (a group is gathered and stamped once at its pop, so an open group is never
stamped). Its caps, its completion (in-line, or deferred to a step the way an event loop turns), its
affinity, its cancel unit and its one resource limit (`max_extent`) are configuration, so that every
path of the contract is reachable. `tests/sink_conformance.h` runs the same tests against each of its
configurations now and against shape-cpu in 13b. A gradient fill's record carries ` grad=<id>`, and the first time a
render meets a gradient the sink writes its record (`R <id> <hash> space=<space> stops=N` and `r <offset> <r> <g> <b>
<a>` per resolved stop, the doubles' bits); a fill that draws nothing is an `X class=5` record and is not stamped. A
stroke's record is `K res=<kind>:<hash> style=<id> map=<hash> box=<x0> <y0> <x1> <y1> expand=<bits> outline=-` (SK10's
box; `expand` the required features the caps do not emit, 0 for a native stroke; `outline` the expansion's hash once a
sink computes one), and the first time a render meets a style, its record `Q <id> <hash>` with the style's dump fields
and a `d` line per dash, once per style per render as a writer emits one; strokes are stamped by their box as fills are.
Besides its default (every stroke feature native), the conformance suite runs it in a **writer-like** configuration
(`STROKE_EXPAND`; native: miter, round and bevel joins, every cap, a local width under any map, dashes per subpath:
SVG 1.1's set) and a **toolpath-like** one (no expansion; native: a round join, round and butt caps, a local width
under a similarity and a device width, dashes), and `tools/gen-sink-vectors.py` restates all three renders of one
stroke list, SK10's box (the outward rounding in exact rationals) and SK11's features included.
`tools/gen-sink-vectors.py` restates the report's
and the test sink's spellings, and `tests/fuzz/fuzz_render.c` drives requests through it: since strokes story S1 with
strokes of eight styles (width 0, a device hairline, odd dashes, miter-clip, arcs with dashes across subpaths, inside
and outside alignment) of squares and runs under anisotropic and singular maps, and sinks that expand, emit a writer's
or a toolpath's set or none, or lack the stroke kind; `tests/fuzz/fuzz_record.c` interns styles of every field, its
result class predicted from SK2 and its canonical form restated from SK3, and records strokes and stroke nodes.


## Glyphs through the waist (normative; story 14a, AD-5)

**The seam** (`glyph_provider.h`) is float-free: no floating type in it or in a suite header it includes,
so that `font` can include it from a `GFNT_` header; `make check-float-free` preprocesses it and refuses a
floating token in any suite header (and is seen to refuse a planted `double` in every run). A provider is an
integer-only ops table bound to a face: `face_info` (upem, axes, flags, the largest ppem it hints, the glyph
count, and the identity a writer needs to reference the font rather than outline it: the family and
PostScript names, with the face's index, coordinates and key), `glyph_outline` (the unhinted outline in 26.6
design units, y up, given to a model path through shape-geometry's fixed-point builder), `glyph_device_path`
(the fast tier: the y-down 26.6 device path relative to the whole-pixel origin, the hint status and the
advance), `glyph_box` (the design box), and optional `face_data` (the font's bytes). Ops may be called from
any thread at once. A GPU sink that keeps signed-distance or MSDF atlases takes the unhinted outline, not the
device path: a distance field is resampled at every size and hinting belongs to one size.

**The route** (`glyph.h`, `gshscene_glyph_route`) is decided from the run, its item's composed map (model to
target, AD-24) and the target's kind, in this order: an unbound face is refused (`FACE_UNBOUND`); a target
without pixels is general with no entry; a binding without the fast tier is general (`HINT_NO_PROGRAM`
unless the mode is `NONE`); a map `gshgeom_map_grid_aligned` rejects, or accepts with the y flip, is general
(`HINT_ROUTE` under `FONT`, refused `HINT_STRICT_ROUTE` under `FONT_STRICT`, nothing under `NONE`); a device
ppem (the run's size times the map's scale, the product checked exact by Dekker's error term with
contraction off) that is not an integer in `[1, max_ppem]` is general likewise with `HINT_SIZE`; anything
else is the fast tier at that ppem and the snapped scale `round(ppem * 65536 / upem)`, half up, with
`HINT_NO_PROGRAM` when the face has no program. The planning pass writes the run-level entry and refuses a
run the route refuses before the sink is called, so a strict render leaves the target untouched; a sink
skips a refused run and never writes the run-level entry. A stream has no hinting and so no entry.

**The two routes.** General: the glyph's map is `map ∘ translate(x, y) ∘ design_to_model(size / upem)`
through the one composer, and the outline is rounded by `gshgeom_device_path_from_model` after the map.
Fast: the glyph's origin is rounded by AD-6's rule and split into a whole pixel and a fraction in `[0, 64)`;
the provider's path is relative to the whole pixel, which the sink adds as an exact integer. The provider's
unhinted points are normative: each 26.6 design point `(x, y)` of the outline, in order and with its verb,
becomes `(fx + gshgeom_scale_26_6(x, S, 0), fy + gshgeom_scale_26_6(y, S, 1))`. That is the general route
under the **snapped map** (`x' = (S / 65536) u + ox / 64`, `y' = -(S / 65536) v + oy / 64`) bit for bit, and
AD-5's gate is exactly that: with hinting off, `fast(map) == general(snap(map))`. `tests/unit/test_glyph.cpp`
compares the two paths element for element over every outline glyph of the test provider, ppem 8 to 72, a
2048-unit and a 1000-unit face, two instances and the fractions {0, 1, 16, 33, 63} on each axis (39,000
pairs); shape-cpu's gate compares the pixels.

**Per glyph** (D7): a provider's status makes a glyph-level `HINT_STATUS` entry with the glyph's index in
`second` (`HINT_PROGRAM_FAILED`, `HINT_WITH_ERRORS`, and `HINT_NO_PROGRAM` for a glyph the font does not hint
at this size on a face that has a program); a failing program under `FONT_STRICT`, a path past a limit, an
origin past 26.6 or a glyph the provider cannot give refuses that glyph (`HINT_PROGRAM_FAILED`,
`RESOURCE_LIMIT`, `GLYPH_UNAVAILABLE`), the rest of the run is drawn, and under render `STRICT` the render
stops at the item with the finished prefix. Mode `NONE` gives no `HINT_STATUS` entry.

**Caches** (D8, normative keys): an outline by `(face id, glyph)`; a device path by `(face id, glyph, ppem,
mode, version, fraction x, fraction y)`; never by run, so a reflowed paragraph's re-interned runs still hit.

**Damage** of a run from a bound face is what a pixel sink draws: the control boxes of the fast tier's
device paths, or each glyph's design box under its map, out by the AA support; an unbound face is
unbounded.

**The test sink** routes each run as the plan did, calls the provider by the route for every glyph, and
writes a `Y` record per glyph (the device path's hash, or the outline's and the glyph map's); a glyph's
provider paths take at most 64 KiB of the render's memory, so `CALLER_MEMORY` holds, and a glyph past it is
refused as the sink's own limit. `tests/glyph_test_provider.h` (installed with the conformance suite) is a
provider with no font file whose glyphs reach every path: ties, quadratics, cubics, a corrupt glyph, a failing
program, a program with errors, a glyph without instructions, a glyph of 3000 lines, and an axis.
`tools/gen-sink-vectors.py` restates the route, the snapped scale, the integer rounding, the test provider's
toy hinter and the records, and the conformance suite checks that every sink calls the provider by the route
and reports per D6 and D7.
