# Design

**Status:** the waist (resources, lists, the immediate-mode recording path, hash and dump), the
retained scene above it (versions, edits, identity, merge, diff and damage), and what a scene means when
it is drawn (color, paint, composition and order, below) are implemented, and so is the sink contract
with its report and the test sink (story 13a). The first real sink, shape-cpu's, is story 13b. Runs from a
font reach the waist through a face resource and the glyph path provider seam (story 14a, below); `font`
implements the provider in story 14b.

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
holds. The formats are per kind: a glyph run's hash and dump are format 2 since the face replaced the key; a
path's, a picture's and a face's are 1.

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
  outside it nothing is touched.
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
  render and takes its scratch from the workspace, whose exact need `gshscene_sink_workspace_bytes`
  gives; less is `ERR_LIMIT` with the need in the summary; a sink without the capability refuses the
  flag. `prepare` uploads or caches a list's static resources by id until `evict` or destroy; the
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

**The test sink** (`dump_sink.h`) is the second backend AD-11 asks for, independent of shape-cpu. It
composites nothing: it writes a dump of every unit (a box per fill and clip: the resource's model
bounds through the composed map, in target units) and stamps one marker rectangle per drawn unit
into an RGBA8 raster (a group is gathered and stamped once at its pop, so an open group is never
stamped). Its caps, its completion (in-line, or deferred to a step the way an event loop turns), its
affinity, its cancel unit and its one resource limit (`max_extent`) are configuration, so that every
path of the contract is reachable. `tests/sink_conformance.h` runs the same tests against each of its
configurations now and against shape-cpu in 13b; `tools/gen-sink-vectors.py` restates the report's
and the test sink's spellings, and `tests/fuzz/fuzz_render.c` drives requests through it.


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
