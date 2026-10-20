/*
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * Copyright (C) 2026 Corey Pennycuff
 *
 * This file is part of Ghoti.io Shape Scene.
 *
 * Ghoti.io Shape Scene is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * Ghoti.io Shape Scene is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file glyph.h
 *
 * A run from a font at render time (AD-5, story 14a): the route, the maps of
 * the two routes, and the calls a sink makes to the provider.  Every sink and
 * the planning pass decide with these functions, so the route is the same for
 * every sink and the caller never chooses it.  The seam itself (the ops, the
 * face, the sizes and the cache keys) is glyph_provider.h.
 *
 * ### The route (normative)
 *
 * ::gshscene_glyph_route takes the run and the **composed map of its item**
 * (the enclosing instances' maps, the item's and the target's, composed root to
 * leaf by `gshgeom_map_compose_chain`, AD-24: model to target units) and the
 * target's kind, and decides in this order:
 *
 * 1. The face is unbound: `REFUSED`, `FACE_UNBOUND` (whatever the mode).
 * 2. The target has no pixels (`STREAM`): `GENERAL`, no entry (hinting does not
 *    apply to a document).
 * 3. The binding has no `glyph_device_path`: `GENERAL`; under a mode other than
 *    `NONE`, a `HINT_STATUS` entry `HINT_NO_PROGRAM` (the face cannot be
 *    hinted).
 * 4. `gshgeom_map_grid_aligned` rejects the map, or accepts it with the y flip
 *    (glyphs mirrored upside down): `GENERAL`; `HINT_ROUTE` under `FONT`,
 *    `REFUSED` `HINT_STRICT_ROUTE` under `FONT_STRICT`, nothing under `NONE`.
 * 5. The device ppem, the run's size times the map's scale, is not an exact
 *    integer in `[1, max_ppem]` (the product is formed exactly, so a size of
 *    13.333 is refused even where the rounded product is an integer):
 *    `GENERAL`; `HINT_SIZE` under `FONT`, `REFUSED` `HINT_STRICT_ROUTE` under
 *    `FONT_STRICT`, nothing under `NONE` (D3).
 * 6. Otherwise `FAST`, at that ppem and ::gshscene_glyph_scale's scale; under
 *    a mode other than `NONE` a face without ::GSHSCENE_FACE_HINTING has a
 *    `HINT_STATUS` entry `HINT_NO_PROGRAM` (the run is drawn unhinted).
 *
 * The planning pass writes the entry of the route (a run-level one: its chain is
 * the item's, `second` 0) and refuses the item on `REFUSED`, so a sink never
 * writes it and skips a refused run.  The fast tiers (AD-13): shape-cpu and the
 * test sink take `FAST` wherever it is returned.
 *
 * ### The two routes
 *
 * **General** (::gshscene_glyph_map, ::gshscene_glyph_outline): the glyph's map
 * is `map ∘ translate(x, y) ∘ design_to_model(size / upem)` through the one
 * composer, and the outline in design units goes through it and is rounded by
 * `gshgeom_device_path_from_model`, after the map.  **Fast**
 * (::gshscene_glyph_origin, ::gshscene_glyph_device_path): the glyph's origin
 * `map(x, y)` is rounded by AD-6's rule to 26.6, split into whole pixels and a
 * fraction in `[0, 64)`, the provider gives the path relative to the whole
 * pixel, and the whole pixel is added as an exact integer.  The **snapped
 * map** (::gshscene_glyph_snap_map) is the general route's map with the scale
 * replaced by `scale / 65536` per design unit and the translation by the
 * rounded origin: with hinting off the fast tier is the general route under it,
 * bit for bit (D2).
 *
 * ### Per glyph (D7)
 *
 * What a provider says of one glyph becomes a ::GSHSCENE_Glyph_Note: nothing;
 * a `HINT_STATUS` entry (`HINT_PROGRAM_FAILED`, `HINT_WITH_ERRORS`, or
 * `HINT_NO_PROGRAM` for a glyph of a face with a program that the font does not
 * hint at this size), keyed by the item's chain with the glyph's index in
 * `second`; or a refusal of that glyph (`HINT_PROGRAM_FAILED` for a failing
 * program under `FONT_STRICT`, `RESOURCE_LIMIT` for a path past a limit or an
 * origin past the 26.6 range, `GLYPH_UNAVAILABLE` for anything else the provider
 * fails with), after which the rest of the run is drawn, and under
 * `GSHSCENE_RENDER_STRICT` the render stops at the item with the finished prefix
 * (S6).  Mode `NONE` gives no `HINT_STATUS` entry.
 */

#ifndef GHOTI_IO_GSHSCENE_GLYPH_H
#define GHOTI_IO_GSHSCENE_GLYPH_H

#include <ghoti.io/shape-scene/core.h>
#include <ghoti.io/shape-scene/glyph_provider.h>
#include <ghoti.io/shape-scene/macros.h>
#include <ghoti.io/shape-scene/types.h>

#include <ghoti.io/shape-geometry/allocator.h>
#include <ghoti.io/shape-geometry/core.h>
#include <ghoti.io/shape-geometry/map.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Which route a run takes.  The values are pinned. */
typedef enum GSHSCENE_Glyph_Route_Kind {
  GSHSCENE_GLYPH_GENERAL = 0, ///< The unhinted outline under the map, rounded after it.
  GSHSCENE_GLYPH_FAST = 1,    ///< The provider's device path at an integer ppem.
  GSHSCENE_GLYPH_ROUTE_COUNT  ///< Closes the enum.
} GSHSCENE_Glyph_Route_Kind;

/** @brief What ::gshscene_glyph_route decides for a run. */
typedef struct GSHSCENE_Glyph_Route {
  uint32_t route;       ///< A ::GSHSCENE_Glyph_Route_Kind.
  uint32_t entry_class; ///< The run-level entry: 0, `HINT_STATUS` or `REFUSED` (a ::GSHSCENE_Entry_Class).
  uint32_t reason;      ///< Its ::GSHSCENE_Reason; 0 with no entry.
  uint32_t ppem;        ///< `FAST`: the device ppem.  Else 0.
  int32_t scale;        ///< `FAST`: the snapped 16.16 scale.  Else 0.
  uint32_t upem;        ///< The face's upem; 0 for an unbound face.
  uint32_t hint_mode;   ///< The run's mode.
  uint32_t face_flags;  ///< The face's flags.
} GSHSCENE_Glyph_Route;

/**
 * @brief The route of a run from a font (see the file comment).
 *
 * @param run A glyph run from a font.
 * @param map Its item's composed map (model to target units), 2D.
 * @param target_kind The target's ::GSHSCENE_Target_Kind (sink.h).
 * @param out Written only on success.
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID (NULL, not a run from a font, an invalid map).
 */
GSHSCENE_API GSHSCENE_Result gshscene_glyph_route(const GSHSCENE_Resource * run, const GSHGEOM_Map * map,
    uint32_t target_kind, GSHSCENE_Glyph_Route * out);

/**
 * @brief The general route's map of glyph @p index: `map ∘ translate(x, y) ∘ design_to_model(size / upem)`.
 *
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID (NULL, not a run from a bound face, an index past the run);
 *   ::GSHSCENE_ERR_LIMIT (a composition past shape-geometry's bounds).
 */
GSHSCENE_API GSHSCENE_Result gshscene_glyph_map(
    const GSHSCENE_Resource * run, const GSHGEOM_Map * map, size_t index, GSHGEOM_Map * out);

/** @brief Where a glyph's origin falls in device space, for the fast tier. */
typedef struct GSHSCENE_Glyph_Origin {
  int32_t x;      ///< The origin in 26.6, AD-6's rounding of `map(x, y)`.
  int32_t y;      ///< Likewise, down.
  int32_t px;     ///< The whole pixel: `floor(x / 64)`.
  int32_t py;     ///< Likewise.
  uint32_t fx;    ///< The fraction, `x - 64 px`, in `[0, 64)`.
  uint32_t fy;    ///< Likewise.
} GSHSCENE_Glyph_Origin;

/**
 * @brief The fast tier's origin of glyph @p index under @p map.
 *
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID (NULL, not a run from a font, an index past the run, an invalid
 *   map); ::GSHSCENE_ERR_LIMIT (an origin past the 26.6 range).
 */
GSHSCENE_API GSHSCENE_Result gshscene_glyph_origin(
    const GSHSCENE_Resource * run, const GSHGEOM_Map * map, size_t index, GSHSCENE_Glyph_Origin * out);

/**
 * @brief The snapped map (D2): `x' = (scale / 65536) u + ox / 64`, `y' = -(scale / 65536) v + oy / 64` for a
 *   design point `(u, v)`, the general route's map the fast tier equals with hinting off.
 *
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID (NULL, a route that is not `FAST`).
 */
GSHSCENE_API GSHSCENE_Result gshscene_glyph_snap_map(
    const GSHSCENE_Glyph_Route * route, const GSHSCENE_Glyph_Origin * origin, GSHGEOM_Map * out);

/** @brief What became of one glyph (see the file comment). */
typedef struct GSHSCENE_Glyph_Note {
  uint32_t entry_class; ///< 0, `HINT_STATUS` or `REFUSED`.
  uint32_t reason;      ///< Its ::GSHSCENE_Reason; 0 with no entry.
  uint32_t status;      ///< The provider's ::GSHSCENE_Hint_Status (fast tier), else 0.
} GSHSCENE_Glyph_Note;

/**
 * @brief The fast tier for glyph @p index: the provider's device path, moved by the whole pixel.
 *
 * @param route The run's route (`FAST`).
 * @param origin The glyph's (::gshscene_glyph_origin).
 * @param alloc,limits The device paths' (NULL for the defaults).
 * @param out The path, which the caller destroys; NULL when the note refuses the glyph.
 * @param note What became of the glyph.
 * @return ::GSHSCENE_OK (with the note), ::GSHSCENE_ERR_INVALID (NULL, not `FAST`, an index past the run),
 *   ::GSHSCENE_ERR_OOM.
 */
GSHSCENE_API GSHSCENE_Result gshscene_glyph_device_path(const GSHSCENE_Resource * run,
    const GSHSCENE_Glyph_Route * route, size_t index, const GSHSCENE_Glyph_Origin * origin,
    const GSHGEOM_Allocator * alloc, const GSHGEOM_Limits * limits, GSHGEOM_Device_Path ** out,
    GSHSCENE_Glyph_Note * note);

/**
 * @brief The general route's outline of glyph @p index: a 2D model path in design units, y up.
 *
 * @param out The path, which the caller destroys; NULL when the note refuses the glyph.
 * @return As ::gshscene_glyph_device_path (the run's face must be bound).
 */
GSHSCENE_API GSHSCENE_Result gshscene_glyph_outline(const GSHSCENE_Resource * run, size_t index,
    const GSHGEOM_Allocator * alloc, const GSHGEOM_Limits * limits, GSHGEOM_Path ** out, GSHSCENE_Glyph_Note * note);

/**
 * @brief The design box of glyph @p index (the provider's `glyph_box`).
 *
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID (NULL, not a run from a bound face, an index past the run); what
 *   the provider failed with.
 */
GSHSCENE_API GSHSCENE_Result gshscene_glyph_box(const GSHSCENE_Resource * run, size_t index, GSHSCENE_Glyph_Box * out);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GSHSCENE_GLYPH_H */
