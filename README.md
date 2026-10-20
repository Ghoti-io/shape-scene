# Ghoti.io Shape Scene

The shape family waist: resources and lists, and the versioned scene above them. It is part of the
[Ghoti.io suite](https://github.com/Ghoti-io/suite).

## What is implemented

The waist: interned immutable resources, transient lists, and the immediate-mode recording path
(no IDs, no versions, no scene); and above it, the retained scene.

- **Resources** (`resource.h`). A store interns content, by hash and then by equality, so equal
  content gets one reference-counted handle. Kinds: a model path, a glyph run (glyphs named by
  font key and glyph id, or given as path outlines), and a picture (a closed list, frozen, which an
  instance draws under a map). Gradients and images are named and reserved. Each handle has a
  64-bit id that is never reused, and a release-listener API lets a sink's cache drop an entry
  when the last reference goes. A store and its handles are thread-safe.
- **Lists** (`list.h`). An arena-backed sequence of fills, clips, groups and instances, with a
  map, a fill rule, a paint and a declared layer and depth on the items that take them. Closing
  a list puts it in paint order (flat, overlay or depth groups) and records every resolved depth
  tie. The maker owns it and frees it in one call; `reset` reuses the arena for the next frame.
- **Paint, color and composition** (`paint.h`, `list.h`; the normative text is in
  `documentation/design.md`). A solid paint is straight `double`s in a color space (four D65
  gamuts, three transfers, from `color`); `currentColor` is a foreground paint that resolves
  against the version's or list's foreground; a group has a blend space, an opacity, a Porter-Duff
  operator and an ordering context. An exact reference (`tools/composite-oracle.py`) and committed
  known answers define what two sinks must agree on.
- **Hash and dump** of every resource and list, in `shape-geometry`'s spelling (FNV-1a 64 over
  little-endian fields), the same on every target.
- **Limits** on everything a caller can make unbounded, each asserted by its bound, including the
  store's total bytes.

The retained scene (`version.h`, `edit.h`, `scene.h`, `diff.h`):

- **Versions** are immutable and shareable: a tree of groups, shapes and instances with 64-bit IDs,
  a key map from a front-end's keys to nodes, hidden nodes (definitions) and instances (`use`), each
  occurrence keyed by its instance path. A version emits a list over interned resources, where an
  instanced node is one picture, and hashes and dumps in the same spelling as the waist.
- **Edits** are what a front-end or driver fills and returns unpublished; an upsert by key keeps IDs
  when a document is lowered again. Only the application's **scene** publishes, alone or by merging
  sibling edits that touch disjoint IDs (overlaps are refused, naming the IDs).
- **Diff** names the added, removed and changed IDs and the conservative damage, in whole device
  pixels, for a sink's map.
- Structural limits (nodes, depth, occurrences, list items, push and picture nesting) are enforced
  at publish; `make fuzz-replay` replays a committed corpus through the recording API.

The sink contract (`sink.h`, `report.h`, `cancel.h`; the normative text, S1 to S16, is in
`documentation/design.md`):

- **A render** is a request (a closed list, a described target, a report, an optional cancel token,
  workspace, progress and completion callbacks) handed to a sink through one interface: `begin`,
  `poll` and an optional `wait`, with a one-call `gshscene_sink_render` for sinks that can block.
  Capabilities are asked per sink and target. Targets are rasters or streams (a document a writer
  sink emits, in units it states); GPU kinds are reserved.
- **The report** is the caller's, reused across frames: what was refused, approximated or resolved
  (depth ties), keyed by list entry and resolvable to node IDs and instance paths for a version's
  list, with a summary and a failure record; a dump and a hash in the family's spelling.
- **Strict, cancel, conflation, caller memory**: a planning pass in the contract layer finds every
  refusal the caps decide before the target is touched; cancel is cooperative at points each sink
  declares, within a bound it declares; the reserved seam-free flag is refused; a caller-supplied
  workspace makes a frame allocate nothing.
- **The test sink** (`dump_sink.h`) writes a dump of every unit and stamps marker rectangles, with
  configurable capabilities; one conformance suite (`tests/sink_conformance.h`) runs against it now
  and against every later sink.

`examples/frame.c` records a frame in immediate mode; `examples/retained.c` is an application, a
front-end and a driver; `examples/sink.c` renders a list through the sink contract with the test
sink.

## Building

```bash
make test PREFIX="$PWD/.local"
make install PREFIX="$PWD/.local"
```

Dependencies are found by pkg-config and nothing else; a missing one is an
error that names the fix. The libraries this one needs: cutil, color and shape-geometry (`image`
returns with the raster-target step).

Determinism is a requirement, not a hope: every `double` stage builds with
`-ffp-contract=off` and no x87, and elementary functions come from `math`, not
libm. `make check-gates` shows each of those rules failing on a planted
violation.

## License

LGPL-3.0-only. See `COPYING` and `COPYING.LESSER`. Patches are not being
accepted at this time; see `CONTRIBUTING.md`.
