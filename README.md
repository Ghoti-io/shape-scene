# Ghoti.io Shape Scene

The shape family waist: resources and lists, and the versioned scene above them. It is part of the
[Ghoti.io suite](https://github.com/Ghoti-io/suite).

## What is implemented

The waist: interned immutable resources, transient lists, and the immediate-mode recording path
(no IDs, no versions, no scene).

- **Resources** (`resource.h`). A store interns content, by hash and then by equality, so equal
  content gets one reference-counted handle. Kinds: a model path, a glyph run (glyphs named by
  font key and glyph id, or given as path outlines), and a picture (a closed list, frozen, which an
  instance draws under a map). Gradients and images are named and reserved. Each handle has a
  64-bit id that is never reused, and a release-listener API lets a sink's cache drop an entry
  when the last reference goes. A store and its handles are thread-safe.
- **Lists** (`list.h`). An arena-backed sequence of fills, clips, groups and instances, with a
  map, a fill rule, a solid paint and a declared layer on the items that take them. The maker
  owns it and frees it in one call; `reset` reuses the arena for the next frame.
- **Hash and dump** of every resource and list, in `shape-geometry`'s spelling (FNV-1a 64 over
  little-endian fields), the same on every target.
- **Limits** on everything a caller can make unbounded, each asserted by its bound.

`examples/frame.c` records a frame. The retained scene (versions, edits, identity, diff) is built
above this layer in a later step, and a sink takes only a closed list and its resources.

## Building

```bash
make test PREFIX="$PWD/.local"
make install PREFIX="$PWD/.local"
```

Dependencies are found by pkg-config and nothing else; a missing one is an
error that names the fix. The library this one needs: shape-geometry (`color` and `image` return with
the paint and raster-target steps).

Determinism is a requirement, not a hope: every `double` stage builds with
`-ffp-contract=off` and no x87, and elementary functions come from `math`, not
libm. `make check-gates` shows each of those rules failing on a planted
violation.

## License

LGPL-3.0-only. See `COPYING` and `COPYING.LESSER`. Patches are not being
accepted at this time; see `CONTRIBUTING.md`.
