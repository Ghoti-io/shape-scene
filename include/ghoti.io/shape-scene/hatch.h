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
 * @file hatch.h
 *
 * The hatch: families of parallel lines trimmed to a fill's region, dashed and stroked, and the paint that fills with it.
 * Rules HX1 to HX3, HX9, HX14 and HX15 live here (the geometry, HX4 to HX12, is `shape-geometry`'s hatch.h).  A **hatch
 * resource** is a hatch of CAD drawings (the DXF HATCH entity's pattern definition lines) as plain data; a **fill** names it by
 * a paint of kind ::GSHSCENE_PAINT_HATCH, and its region is the fill's own path (or glyph run).  A hatch is geometry and not a
 * raster pattern: a plotter follows the lines, a CAD writer emits the entity, and a pixel sink draws the expansion.
 *
 * ### The resource (HX1)
 *
 * `{angle_unit, nesting, style, map, ink, family_count, families}`.  A **family** is a pattern definition line: `{angle, origin
 * (x, y), offset_along (delta-x), offset_across (delta-y), dash_count, dashes}` (DXF groups 53, 43 and 44, 45 and 46, 79, 49):
 * the lines have the angle, pass through the origin, are `offset_across` apart and slide `offset_along` along themselves from
 * line to line; a positive dash length is a pen-down dash, a negative one a space and zero a dot; a family with no dashes is
 * solid.  `angle_unit` is a ::GSHGEOM_Angle_Unit (DXF's is degrees).  `style` is a stroke style resource of the store (width,
 * width space and cap are the pen; a style with dashes or an alignment other than center is refused).  `map` takes **hatch
 * space** to the local space of the item.  `ink` is a paint of kind solid, foreground or palette.  `nesting` is
 * ::GSHSCENE_HATCH_NESTING_RULE (the fill's rule), ::GSHSCENE_HATCH_NESTING_OUTER or ::GSHSCENE_HATCH_NESTING_IGNORE (DXF
 * group 75's other two styles).  Start from ::gshscene_hatch_init.
 *
 * ### Validity, bounds, canonical form (HX2)
 *
 * Checked in this order: every double finite and every enumeration in range, the style a stroke style of this store, at least one
 * family, dashes given when counted, the map valid and 2D, the ink a solid, foreground or palette paint by a gradient stop's rules,
 * every family's elements holding a pen-down element and a length (`INVALID`); a 3D map (`UNSUPPORTED`); the style with dashes, or
 * with an alignment other than center (`INVALID`); then the bounds (`LIMIT`): the families at most `min(max_resource_elements,
 * ::GSHSCENE_HATCH_FAMILIES_MAX)`, each family's dashes at most `min(max_resource_elements, 65535)`, and every origin, offset, dash and
 * entry of the map zero or of magnitude in `[2^-64, 2^64]`, each angle at most `2^40` in its unit, a solid ink within N1's bound.
 * `-0` is `+0`.  The elements are kept as given (they are not merged: a CAD round trip returns the file's elements) and equal
 * content is one handle.  An `offset_across` of 0 is allowed here and is HX9's notice.  The join, miter limit and `degenerate`
 * of the style are carried and unused.  The hash is, after resource.h's two leading fields, `u8` unit, `u8` nesting, `u64` the
 * style's content hash, `u64` `gshgeom_map_hash` of the map, the ink (`u8` kind, four `f64`, its space and `u32` entry), `u64`
 * the family count and per family five `f64`, `u64` the dash count and each dash.  The dump is a line `hatch unit=N nesting=N
 * style=<hash> map=<hash> ink=<kind r g b a space entry> families=N` and a line `family angle=<hex> origin=<hex> <hex> along=<hex>
 * across=<hex> dashes=N` followed on that line by ` <hex>` for each dash, every double as 16 hex digits.
 *
 * ### The paint (HX3)
 *
 * A ::GSHSCENE_PAINT_HATCH paint names a hatch resource of the list's store; `rgba[0..2]` are zero, `rgba[3]` is the **alpha
 * multiplier**, the space and the entry are zero.  It is allowed on a **fill** and in a pattern's tile; on a **stroke** it is
 * `UNSUPPORTED` at record, and on a gradient stop or a tint it is never.  The hatch space reaches the target by `C = T I_1 ..
 * I_k M H`.  A hatch fill is composited as N7 with the hatch's covered region (the stroke of its trimmed, dashed lines, one union)
 * in place of the shape, and the ink in place of the paint, times the multiplier: the fill's own coverage is **not** multiplied
 * in; the item's path or run gives the trim region and nothing else.
 *
 * ### What draws nothing (HX9)
 *
 * ::gshscene_hatch_notice, in this order: `MAP_NOT_INVERTIBLE` when some map of the chain (the target's, an instance's, the
 * item's, the hatch's) has a linear part of determinant exactly zero, each tested alone; `STROKE_ZERO_WIDTH` for a style of width 0;
 * `HATCH_DEGENERATE` when any family has an `offset_across` of 0 (that family draws nothing, the others are drawn).  An ink of alpha
 * zero, a region with no interior and a hatch whose lines miss it draw nothing and are no notice.
 *
 * ### Bounds (HX15)
 *
 * A trimmed line is stroked, so a cap at its end reaches outside the region: a hatch fill's bounds are the region's bounds outset
 * exactly as a stroke's (stroke.h, SK10) by `h = width / 2 max(1, f_cap)` without any join, in local units mapped by the composed
 * map for a local width and in pixels for a device width.  ::gshscene_hatch_bounds is the one evaluation.
 *
 * ### Sinks (HX14)
 *
 * A sink that draws hatches has ::GSHSCENE_CAP_HATCH; without it the planning pass refuses a hatch fill, `HATCH`, on every sink.
 * A sink that strokes the pieces checks the style's required features as SK11 does and refuses what it cannot express with
 * `STROKE_STYLE`.
 */
#ifndef GHOTI_IO_GSHSCENE_HATCH_H
#define GHOTI_IO_GSHSCENE_HATCH_H

#include <ghoti.io/shape-scene/core.h>
#include <ghoti.io/shape-scene/list.h>
#include <ghoti.io/shape-scene/macros.h>
#include <ghoti.io/shape-scene/paint.h>
#include <ghoti.io/shape-scene/report.h>
#include <ghoti.io/shape-scene/resource.h>
#include <ghoti.io/shape-scene/types.h>

#include <ghoti.io/shape-geometry/map.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** How loops nest (HX1, DXF group 75).  The values are pinned (they are hashed). */
typedef enum GSHSCENE_Hatch_Nesting {
  GSHSCENE_HATCH_NESTING_RULE = 0,   ///< The fill's rule.
  GSHSCENE_HATCH_NESTING_OUTER = 1,  ///< Only the outermost ring.
  GSHSCENE_HATCH_NESTING_IGNORE = 2, ///< Everything inside the outer loops.
  GSHSCENE_HATCH_NESTING_COUNT       ///< Closes the enum.
} GSHSCENE_Hatch_Nesting;

/** The most families of a hatch (HX2). */
#define GSHSCENE_HATCH_FAMILIES_MAX ((size_t)256)

/** One family (HX1): a pattern definition line. */
typedef struct GSHSCENE_Hatch_Family {
  double angle;          ///< From +x toward +y of hatch space, in the hatch's unit.
  double origin_x;       ///< A point of line 0, in hatch space.
  double origin_y;
  double offset_along;   ///< delta-x: the shift of each line's dashes along it, line to line.
  double offset_across;  ///< delta-y: the spacing of the lines; 0 draws nothing (HX9).
  size_t dash_count;     ///< The elements (DXF group 49); 0 is solid.
  const double * dashes; ///< Positive a dash, negative a space, zero a dot.  NULL only when @p dash_count is 0.  Read back: the resource's copy.
} GSHSCENE_Hatch_Family;

/** A hatch (HX1).  Start from ::gshscene_hatch_init. */
typedef struct GSHSCENE_Hatch {
  uint32_t angle_unit;                    ///< A ::GSHGEOM_Angle_Unit.
  uint32_t nesting;                       ///< A ::GSHSCENE_Hatch_Nesting.
  const GSHSCENE_Resource * style;        ///< A stroke style of the store; required.
  GSHGEOM_Map map;                        ///< Hatch space to the item's local space; 2D.
  GSHSCENE_Paint ink;                     ///< A solid, foreground or palette paint.
  size_t family_count;                    ///< At least 1.
  const GSHSCENE_Hatch_Family * families; ///< Read back: the resource's copy.
} GSHSCENE_Hatch;

/** @brief Fill @p hatch with HX1's defaults: degrees, the identity map, rule nesting, opaque black ink, no families and no style.  NULL is ignored. */
GSHSCENE_API void gshscene_hatch_init(GSHSCENE_Hatch * hatch);

/**
 * @brief Intern a hatch (HX1, HX2): checked as given and put in its canonical form; equal content is one handle.  The hatch retains
 *   its style.  @p out receives a borrowed reference as the other interns' do.
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID, ::GSHSCENE_ERR_UNSUPPORTED or ::GSHSCENE_ERR_LIMIT in HX2's order; ::GSHSCENE_ERR_OOM.
 */
GSHSCENE_API GSHSCENE_Result gshscene_store_intern_hatch(GSHSCENE_Store * store, const GSHSCENE_Hatch * hatch,
    const GSHSCENE_Resource ** out);

/** @brief The canonical hatch of a hatch resource; its families and their elements point into the resource.  ::GSHSCENE_ERR_INVALID for another kind. */
GSHSCENE_API GSHSCENE_Result gshscene_resource_hatch(const GSHSCENE_Resource * resource, GSHSCENE_Hatch * out);

/**
 * @brief HX9: whether a hatch fill draws nothing, and why: the planning pass's `NOTICE` and every sink's one predicate.
 *
 * @param item           A fill painted with a hatch.
 * @param instance_maps  The instance maps root to leaf, @p instance_count of them.
 * @param target_map     The target's map, or NULL.
 * @return ::GSHSCENE_REASON_MAP_NOT_INVERTIBLE, ::GSHSCENE_REASON_STROKE_ZERO_WIDTH, ::GSHSCENE_REASON_HATCH_DEGENERATE or 0.
 */
GSHSCENE_API GSHSCENE_Reason gshscene_hatch_notice(const GSHSCENE_Item * item, const GSHGEOM_Map * instance_maps,
    size_t instance_count, const GSHGEOM_Map * target_map);

/**
 * @brief HX15: a conservative box of a hatch fill in target units, from its region's box in the item's local space.
 *
 * The region's box outset as SK10 outsets a stroke's, without a join (a hatched line has none).  The arguments and the result are
 * those of ::gshscene_stroke_bounds with the hatch's style.
 */
GSHSCENE_API GSHSCENE_Result gshscene_hatch_bounds(const GSHSCENE_Resource * style, const GSHGEOM_Bounds * region,
    const GSHGEOM_Map * composed, uint32_t unit, GSHGEOM_Bounds * out);

/**
 * @brief The SK11 features a hatch fill requires of a sink that strokes its pieces (the cap, the width space, an anisotropic local
 *   width); 0 when the fill draws nothing (HX9).  Arguments as ::gshscene_stroke_required.
 */
GSHSCENE_API uint32_t gshscene_hatch_required(const GSHSCENE_Item * item, const GSHGEOM_Map * instance_maps,
    size_t instance_count, const GSHGEOM_Map * target_map);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GSHSCENE_HATCH_H */
