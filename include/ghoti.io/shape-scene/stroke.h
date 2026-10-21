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
 * @file stroke.h
 *
 * Strokes at the waist (strokes story S1): a stroke is a **centerline** (a path or
 * a glyph run) plus an explicit **style**, an interned resource, and a paint.
 * There is no mode flag: every difference between the formats that stroke (SVG,
 * Canvas 2D, PDF, CSS painting, DXF) is a field of the style with a stated
 * default, and a front end sets each field to match its format.  What a stroke
 * **covers** is defined once, geometrically, by `shape-geometry` (its stroke
 * expansion); this library says how a stroke is spelled, hashed, ordered, planned
 * and reported, and evaluates no stroke geometry.
 *
 * The rules are numbered SK1 to SK14 and are normative; tests, planted defects,
 * the independent generators and documentation/design.md cite them.  All of them
 * are in real arithmetic, as N1 to N14 are.
 *
 * ### The rules (normative)
 *
 * **SK1 The style.**  A ::GSHSCENE_Stroke_Style is `{width, width_space, join,
 * miter_limit, cap, degenerate, dashes, dash_offset, dash_scope, alignment,
 * alignment_rule, width_profile}`.  The values are pinned (they are hashed):
 * ::GSHSCENE_Width_Space `LOCAL 0`, `DEVICE 1`; ::GSHSCENE_Join `MITER 0`,
 * `ROUND 1`, `BEVEL 2` (PDF's `j`, so a PDF lowering is the identity), `MITER_CLIP
 * 3`, `ARCS 4`; ::GSHSCENE_Cap `BUTT 0`, `ROUND 1`, `SQUARE 2` (PDF's `J`);
 * ::GSHSCENE_Degenerate `DRAW 0` (SVG 2's shape for a subpath of zero length,
 * oriented along local `+x`), `OMIT 1`; ::GSHSCENE_Dash_Scope `PER_SUBPATH 0`
 * (the pattern restarts at each subpath, SVG 2), `PATH 1`; ::GSHSCENE_Alignment
 * `CENTER 0`, `INSIDE 1`, `OUTSIDE 2`; `alignment_rule` a `GSHGEOM_Fill_Rule`
 * (nonzero 0 by default).  ::gshscene_stroke_style_init writes the defaults: width
 * 1, local, miter, miter limit 4 (SVG's), butt, draw, no dashes, offset 0, per
 * subpath, center, nonzero, no width profile.  A zero-filled style is **not**
 * valid (its miter limit 0 is out of range); callers start from `init`.  The
 * miter limit is the ratio of the miter's length to the width, `1 / sin(θ / 2)`
 * for an angle `θ` between the segments; past it (strictly greater) a miter
 * becomes a bevel, and a miter-clip or arcs join is clipped at `miter_limit *
 * width / 2` from the vertex (SVG 2; `shape-geometry` states it in full).
 *
 * **SK2 Validity and bounds.**  Checked on the style as given, in this order:
 * every double finite, every enumeration in its range, `width >= 0`, and dashes
 * given when counted, else ::GSHSCENE_ERR_INVALID; a non-NULL width profile,
 * ::GSHSCENE_ERR_UNSUPPORTED (reserved, D8); then ::GSHSCENE_ERR_LIMIT, decided
 * after every other check: `width` 0 or of magnitude in `[2^-64, 2^64]`
 * (::GSHSCENE_STROKE_VALUE_MIN, ::GSHSCENE_STROKE_VALUE_MAX, G14's band);
 * `miter_limit` in `[1, 2^16]` (::GSHSCENE_STROKE_MITER_LIMIT_MAX); each dash
 * (by magnitude, so a negative one is bounded too) and `dash_offset` 0 or of
 * magnitude in `[2^-64, 2^64]`; at most `min(max_resource_elements,
 * GSHSCENE_STROKE_DASHES_MAX)` (65535) dashes **after** SK3 has doubled an odd
 * array.  The bounds are checked before SK3 resets a field (a miter limit of 0
 * under a round join is still out of range).  `-0` is stored as `+0`.
 *
 * **SK3 Canonical form.**  The store interns the canonical form, and the hash, the
 * dump, equality and ::gshscene_resource_stroke_style see only it, so equal
 * drawings are one handle and one hash.  (a) **Dashes:** an array with any
 * negative length, or whose lengths are all zero, is stored as no dashes (a solid
 * stroke); an odd-length array is stored repeated once (`[a, b, c]` as `[a, b, c,
 * a, b, c]`).  (b) **Fields that do not apply** take their default: with no
 * dashes, `dash_offset` 0 and `dash_scope` `PER_SUBPATH`; with a join other than
 * `MITER`, `MITER_CLIP` and `ARCS`, `miter_limit` 4; with `CENTER` alignment,
 * `alignment_rule` nonzero.  (c) Nothing else is rewritten: the offset is kept as
 * given (not reduced modulo the period, which is the dash arithmetic's), and width
 * 0 is kept: it draws nothing.
 *
 * **SK4 The stroke item.**  ::gshscene_list_stroke records ::GSHSCENE_ITEM_STROKE
 * (7): a centerline, a path or a glyph run of the list's store (a 3D path, or a run
 * whose outline is a 3D path, is ::GSHSCENE_ERR_UNSUPPORTED: the stroke is planar,
 * and so a 3D item map is unsupported too, as 3D node maps are); a style of the
 * list's store; a map and a paint as a fill's (any paint kind, with N1's bound and
 * the palette and gradient rules unchanged); a layer and a depth.  Its `fill_rule`
 * is nonzero and its `group` zero (the alignment rule is the style's).  The item's
 * `style` names the style (NULL for every other kind), and the list holds a
 * reference on the centerline and on the style.  A stroke is **one unit** of its
 * sequence (N11) and **one element** of a knockout group (T3): its region is one
 * union.
 *
 * **SK5 What a stroke draws.**  N7 with the stroke's **covered region** `R` in
 * place of the shape: `apply(L, S, k * c, SRC_OVER)` with `k` the coverage of `R`
 * (exact area for a raster sink, AD-12) and `S` the paint as N7, N10, N14 and G12
 * give it.  `R` is defined once, by `shape-geometry`: the body within half the
 * width of the centerline, with joins and caps; local width built in the
 * centerline's model space and then mapped; device width built in **target
 * space** around the mapped centerline (a pixel kind's pixels; for a stream the
 * CSS reference pixel, 1/96 inch, in the target's stated unit:
 * ::gshscene_stroke_pixel_size); dashes cut before the body; inside and outside
 * alignment a centered stroke of twice the width clipped to the centerline's fill
 * region under `alignment_rule`, or to its complement, closed subpaths only (an
 * open subpath is centered); one union over the item.  Orientation (which side is
 * left) is AD-6's, stated by `shape-geometry`, never here.
 *
 * **SK6 Paint.**  A stroke's paint is sampled in the item's **local space** for
 * both width spaces: G6's chain `(T I_1 ... I_k M G)^-1`, unchanged.  A front end
 * that wants SVG's `objectBoundingBox` on a stroke folds the geometry's box
 * (without the stroke, as SVG states) into `G`, as it does for a fill.  Every
 * G-rule applies with `R` as the shape.
 *
 * **SK7 A glyph run as centerline.**  A stroke of a run always takes the general,
 * unhinted route: the planning pass does not call ::gshscene_glyph_route for a
 * stroke, writes no `HINT_STATUS` entry for it and never refuses it for hinting (a
 * `FONT_STRICT` run included).  A run from a font needs `GLYPHS_FROM_FONT` as a
 * fill does (`GLYPH_SOURCE`).
 *
 * **SK10 Bounds and damage.**  A stroke occurrence's bounds are conservative:
 * with `h = width / 2 * max(1, f_join, f_cap) * a`, where `f_join` is
 * `miter_limit` for `MITER` (the miter's tip is at most that far, in half widths,
 * from its vertex), `miter_limit + 1` for `MITER_CLIP` and `ARCS` (a clipped
 * corner lies `miter_limit` half widths along the bisector and up to one half
 * width across it, so at most `sqrt(miter_limit^2 + 1)` half widths away), and 1
 * otherwise; `f_cap` is `3/2` for `SQUARE` (a rational bound above `sqrt 2`) and
 * 1 otherwise; and `a` is 2 for `INSIDE` and `OUTSIDE` and 1 for `CENTER`:
 * **local** width outsets the centerline's model box by `h` and maps it by the
 * composed map; **device** width maps the box and outsets it by `h` in target
 * units (`h` times ::gshscene_stroke_pixel_size).  The damage then adds N13's AA
 * support and rounds outward, as for a fill.  Dashes never enlarge it.  A run from
 * an unbound face is unbounded.  `h` is formed with its roundings covered (the
 * product scaled by `1 + 2^-50`) and each outset rounded outward, so the bound is
 * never below the real one; mapping the box's corners is a fill's.  N13 is
 * extended: a changed stroke node damages its bounds in both versions, and a
 * changed foreground or palette damages the strokes that paint with it.
 * ::gshscene_stroke_bounds is the one evaluation.
 *
 * **SK12 Hash and dump.**  Every format stays 1, and every existing hash and dump
 * is unchanged: no list, version or resource from before strokes holds one.  The
 * **style** (resource kind 8): `u32` 1, `u32` 8, `f64` width, `u8` width space,
 * `u8` join, `f64` miter limit, `u8` cap, `u8` degenerate, `u8` alignment, `u8`
 * alignment rule, `u8` dash scope, `f64` dash offset, `u64` dash count, `f64` each
 * dash, `u64` the width profile's content hash (0 for none).  Its dump is `stroke
 * width=<hex> space=N join=N miter=<hex> cap=N degenerate=N align=N rule=N
 * scope=N offset=<hex> dashes=N profile=<hash>` and a line `d <hex>` per dash.
 * The **list item** and the **version node** are spelled in list.h and version.h.
 *
 * **SK13 The retained scene.**  ::GSHSCENE_NODE_STROKE (4): a centerline, a style,
 * a map, a paint, a layer and a depth, set by an edit like any node (an upsert by
 * key as for a shape), emitted as one stroke item with its placement composed,
 * visited, diffed and damaged (SK10).  One SVG element with a fill and a stroke is
 * two nodes, or two items, in paint order (SK14).
 *
 * **SK14 Fill and stroke of one element.**  Two items: the fill, then the stroke,
 * in the order the format's paint order gives (SVG's `paint-order`; markers are
 * items of their own).  A front end that needs them composited as one (an
 * element's opacity) wraps them in a group, as now.
 *
 */

#ifndef GHOTI_IO_GSHSCENE_STROKE_H
#define GHOTI_IO_GSHSCENE_STROKE_H

#include <ghoti.io/shape-scene/core.h>
#include <ghoti.io/shape-scene/list.h>
#include <ghoti.io/shape-scene/macros.h>
#include <ghoti.io/shape-scene/resource.h>
#include <ghoti.io/shape-scene/types.h>

#include <ghoti.io/shape-geometry/curve.h>
#include <ghoti.io/shape-geometry/fill.h>
#include <ghoti.io/shape-geometry/map.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The space a stroke's width is in (SK1, SK5).  The values are pinned (they are hashed). */
typedef enum GSHSCENE_Width_Space {
  GSHSCENE_STROKE_WIDTH_LOCAL = 0,  ///< The centerline's model space: the map applies (a non-uniform one makes an elliptical pen).
  GSHSCENE_STROKE_WIDTH_DEVICE = 1, ///< Target space: pixels, or CSS pixels on a stream (SVG's `non-scaling-stroke`, a hairline).
  GSHSCENE_STROKE_WIDTH_SPACE_COUNT ///< Closes the enum.
} GSHSCENE_Width_Space;

/** @brief How two segments meet (SK1).  The values are pinned: PDF's `j`, then SVG 2's two. */
typedef enum GSHSCENE_Join {
  GSHSCENE_STROKE_JOIN_MITER = 0,      ///< The outer edges extended to meet; a bevel past the miter limit.
  GSHSCENE_STROKE_JOIN_ROUND = 1,      ///< A circular arc.
  GSHSCENE_STROKE_JOIN_BEVEL = 2,      ///< The outer corners joined by a line.
  GSHSCENE_STROKE_JOIN_MITER_CLIP = 3, ///< A miter, clipped at the miter limit instead of beveled (SVG 2).
  GSHSCENE_STROKE_JOIN_ARCS = 4,       ///< SVG 2's arcs join, clipped at the miter limit.
  GSHSCENE_STROKE_JOIN_COUNT           ///< Closes the enum.
} GSHSCENE_Join;

/** @brief How an open subpath ends (SK1).  The values are pinned: PDF's `J`. */
typedef enum GSHSCENE_Cap {
  GSHSCENE_STROKE_CAP_BUTT = 0,   ///< Nothing past the end.
  GSHSCENE_STROKE_CAP_ROUND = 1,  ///< A half disc.
  GSHSCENE_STROKE_CAP_SQUARE = 2, ///< A half square.
  GSHSCENE_STROKE_CAP_COUNT       ///< Closes the enum.
} GSHSCENE_Cap;

/** @brief What a subpath of zero length draws (SK1, D4).  The values are pinned. */
typedef enum GSHSCENE_Degenerate {
  GSHSCENE_STROKE_DEGENERATE_DRAW = 0, ///< SVG 2's shape: the cap's dot, along local `+x` (nothing with a butt cap).
  GSHSCENE_STROKE_DEGENERATE_OMIT = 1, ///< Nothing.
  GSHSCENE_STROKE_DEGENERATE_COUNT     ///< Closes the enum.
} GSHSCENE_Degenerate;

/** @brief Where a dash pattern restarts (SK1).  The values are pinned. */
typedef enum GSHSCENE_Dash_Scope {
  GSHSCENE_STROKE_DASH_PER_SUBPATH = 0, ///< At each subpath (SVG 2).
  GSHSCENE_STROKE_DASH_PATH = 1,        ///< Once, continuing across subpaths.
  GSHSCENE_STROKE_DASH_SCOPE_COUNT      ///< Closes the enum.
} GSHSCENE_Dash_Scope;

/** @brief Which side of the centerline the stroke lies on (SK1, D6).  The values are pinned. */
typedef enum GSHSCENE_Alignment {
  GSHSCENE_STROKE_ALIGN_CENTER = 0,  ///< Centered.
  GSHSCENE_STROKE_ALIGN_INSIDE = 1,  ///< Inside a closed subpath's fill region (by `alignment_rule`).
  GSHSCENE_STROKE_ALIGN_OUTSIDE = 2, ///< Outside it.
  GSHSCENE_STROKE_ALIGN_COUNT        ///< Closes the enum.
} GSHSCENE_Alignment;

/**
 * @brief A stroke style (SK1): what ::gshscene_store_intern_stroke_style takes and
 *   ::gshscene_resource_stroke_style gives back.  Start from ::gshscene_stroke_style_init.
 */
typedef struct GSHSCENE_Stroke_Style {
  double width;                   ///< At least 0; 0 or of magnitude in `[2^-64, 2^64]` (SK2).  0 draws nothing (SK8).
  uint32_t width_space;           ///< A ::GSHSCENE_Width_Space.
  uint32_t join;                  ///< A ::GSHSCENE_Join.
  double miter_limit;             ///< In `[1, 2^16]`; 4 by default.  Read only by the miter, miter-clip and arcs joins.
  uint32_t cap;                   ///< A ::GSHSCENE_Cap.
  uint32_t degenerate;            ///< A ::GSHSCENE_Degenerate.
  size_t dash_count;              ///< How many dash lengths; 0 is a solid stroke.
  /** The dash lengths, alternately on and off; NULL only when @p dash_count is 0.  Read back: the canonical array, in the resource. */
  const double * dashes;
  double dash_offset;             ///< Where in the pattern the stroke starts; read only with dashes.
  uint32_t dash_scope;            ///< A ::GSHSCENE_Dash_Scope; read only with dashes.
  uint32_t alignment;             ///< A ::GSHSCENE_Alignment.
  GSHGEOM_Fill_Rule alignment_rule; ///< The rule of the region inside and outside alignment clip to (D10); read only off center.
  const GSHSCENE_Resource * width_profile; ///< Reserved (D8): NULL; anything else is ::GSHSCENE_ERR_UNSUPPORTED.
} GSHSCENE_Stroke_Style;

/** @name SK2's bounds
 * @{ */
#define GSHSCENE_STROKE_VALUE_MIN 0x1p-64       ///< The smallest magnitude of a nonzero width, dash or offset.
#define GSHSCENE_STROKE_VALUE_MAX 0x1p64        ///< The largest magnitude of a width, dash or offset.
#define GSHSCENE_STROKE_MITER_LIMIT_MAX 0x1p16  ///< The largest miter limit (the smallest is 1).
#define GSHSCENE_STROKE_MITER_LIMIT_DEFAULT 4.0 ///< SVG's default miter limit, and SK3's value where it does not apply.
#define GSHSCENE_STROKE_DASHES_MAX ((size_t)65535) ///< The most dashes after an odd array is doubled (also the store's `max_resource_elements`).
/** @} */

/** @brief Fill @p style with SK1's defaults.  NULL is ignored. */
GSHSCENE_API void gshscene_stroke_style_init(GSHSCENE_Stroke_Style * style);

/**
 * @brief Intern a stroke style (SK1 to SK3): checked as given, then put in its canonical form, which is what the
 *   store holds and the handle reads back.
 *
 * @param style The style; its dashes are copied (the canonical array).
 * @param out The handle, with a reference for the caller; written only on success.
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID (NULL, a non-finite double, an enumeration out of range, a negative
 *   width, dashes counted but not given); ::GSHSCENE_ERR_UNSUPPORTED (a width profile, D8); ::GSHSCENE_ERR_LIMIT
 *   (SK2's bounds, or the store's own limits); ::GSHSCENE_ERR_OOM.
 */
GSHSCENE_API GSHSCENE_Result gshscene_store_intern_stroke_style(GSHSCENE_Store * store,
    const GSHSCENE_Stroke_Style * style, const GSHSCENE_Resource ** out);

/**
 * @brief Read a stroke style back, in its canonical form (SK3).
 *
 * @param out Written only on success; `dashes` points into the resource and is valid while a handle is held.
 * @return ::GSHSCENE_OK or ::GSHSCENE_ERR_INVALID (NULL, not a stroke style).
 */
GSHSCENE_API GSHSCENE_Result gshscene_resource_stroke_style(const GSHSCENE_Resource * resource,
    GSHSCENE_Stroke_Style * out);

/**
 * @brief The target units in one device pixel for a target of unit @p unit (SK5, K8): 1 for a pixel kind's (0) and
 *   for pixels, and the CSS reference pixel, 1/96 inch, in the others: `25.4 / 96` millimetres, `0.75` points,
 *   `1 / 96` inch (the doubles nearest).  0 for a value outside ::GSHSCENE_Unit (sink.h).
 */
GSHSCENE_API double gshscene_stroke_pixel_size(uint32_t unit);

/**
 * @brief SK10: a conservative box of a stroke in target units, from its centerline's box in model space.
 *
 * @param style A stroke style resource.
 * @param centerline The centerline's box in its model space (a path's bounds; a run's glyph boxes placed); an empty
 *   box gives an empty result.
 * @param composed The composed map, model space to target units (AD-24: `gshgeom_map_compose_chain` of the
 *   instances' maps and the item's, with the target's last).
 * @param unit The target's ::GSHSCENE_Unit (for a device width: ::gshscene_stroke_pixel_size).
 * @param out The box; written only on success.
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID (NULL, not a style, an invalid or 3D map, a unit out of range);
 *   ::GSHSCENE_ERR_LIMIT (a corner the map cannot take, or a result that is not finite: the bound is unknown).
 */
GSHSCENE_API GSHSCENE_Result gshscene_stroke_bounds(const GSHSCENE_Resource * style, const GSHGEOM_Bounds * centerline,
    const GSHGEOM_Map * composed, uint32_t unit, GSHGEOM_Bounds * out);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GSHSCENE_STROKE_H */
