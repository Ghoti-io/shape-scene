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
 * @file pattern.h
 *
 * The pattern: a picture repeated on a lattice, and the paint that fills with it.  Rules PT1 to PT14.  A **pattern
 * resource** names a **tile**, a closed picture (a frozen list) in **pattern space** (y down), a rectangular **cell**
 * of that space, the lattice **steps**, the repetition along each axis, and the map from pattern space to the local
 * space of the item that paints with it; a fill or a stroke names it by a paint of kind ::GSHSCENE_PAINT_PATTERN.  SVG's
 * `<pattern>`, a PDF tiling pattern, CSS `background-repeat` (`space` included) and a Canvas pattern are all this.  No
 * sink caches or resamples the tile as the definition: the definition is the lattice of copies of the picture, and a
 * sink that draws it another way is gated against that definition.
 *
 * ### The resource (PT1)
 *
 * `{tile, cell, x_step, y_step, repeat_x, repeat_y, map, has_tint, tint}`.  The `tile` is a picture resource of the same
 * store, which the pattern retains.  The `cell` is the rectangle `[x, x + w] x [y, y + h]`.  `x_step` and `y_step` are the
 * lattice spacing: the copy `(i, j)` is the tile translated by `(i x_step, j y_step)` and has the cell
 * `[x + i x_step, x + i x_step + w] x [y + j y_step, y + j y_step + h]`.  A step equal to the size abuts the cells, a larger
 * step leaves a gap (CSS `space`), a smaller one overlaps them (PDF's `XStep` and `YStep` may be smaller than the cell).
 * `repeat_x` and `repeat_y` are 0 or 1: 0 draws only index 0 on that axis (CSS `repeat-y`, `no-repeat`).  The `map` takes
 * pattern space to the item's local space, valid and 2D.  An empty tint is a **colored** pattern; a `has_tint` of 1 and a
 * `tint` (a solid, foreground or palette paint) make it **uncolored** (PDF's `PaintType` 2).  The zero value of every field
 * is invalid but for the ones PT2 names, so a caller starts from ::gshscene_pattern_init.
 *
 * ### Validity and bounds (PT2)
 *
 * Checked in this order: every double finite and every enumeration in range, the tile a picture of this store, the size
 * and the steps not negative, the map valid, the tint given exactly when `has_tint` is 1 (`INVALID`); a 3D map
 * (`UNSUPPORTED`); then the bounds (`LIMIT`): each of `x, y, w, h, x_step, y_step` and every entry of the map zero or of
 * magnitude in `[2^-64, 2^64]`, a solid tint's components within N1's bound, and the tile's picture depth plus one at most
 * `max_picture_depth`.  Zero is allowed for a size or a step and is PT10's notice, never refused at record.  A negative
 * size or step is invalid: a front end that meets one takes its magnitude, which is the same lattice.
 *
 * ### Canonical form, hash and dump (PT3)
 *
 * Equal content is one handle; `-0` is stored as `+0`; with `has_tint` 0 the tint is the zero paint.  **A step on an axis whose
 * repeat is 0 is never read (the one copy has no neighbour to overlap, and the notice and the selector look at repeating axes
 * only), so the store keeps the size there, `w` for `x_step` and `h` for `y_step`: two patterns that differ only in such a step
 * are one handle with one hash, and a read-back pattern shows the size.**  The step is still checked as an input (finite, not
 * negative) and the bound is decided on what is kept.  After resource.h's two
 * leading fields the hash is: `u64` the tile's content hash, `f64` `x, y, w, h, x_step, y_step`, `u8` `repeat_x`, `u8`
 * `repeat_y`, `u64` `gshgeom_map_hash` of the map, `u8` `has_tint` and, with a tint, `u8` its kind, four `f64`, its space
 * and `u32` its entry.  The dump is `pattern cell=<x> <y> <w> <h> step=<xs> <ys> repeat=<rx><ry> map=<hash> tile=<hash>
 * tint=<-|kind r g b a space entry>`, every `double` as 16 hexadecimal digits, and the tile's own dump follows.
 *
 * ### The paint and its chain (PT4)
 *
 * A fill or stroke with a ::GSHSCENE_PAINT_PATTERN paint, whose chain is the target map `T`, the instance maps
 * `I_1 .. I_k`, the item map `M` and the pattern's map `G`, takes **pattern space** to the target by
 * `C = T I_1 .. I_k M G` (`gshgeom_map_compose_chain`).  A fill is N7 over the **lattice picture** (below) at the pixel,
 * premultiplied in the layer's space and multiplied by the paint's alpha multiplier; the lattice picture is the area
 * average over the pixel of the picture rendered into an isolated layer.
 *
 * ### The lattice (PT5)
 *
 * The index `(i, j)` is an integer, and the translation is its product with the step: nothing accumulates, so the
 * position of copy `(i, j)` carries no error from the copies before it.  ::gshscene_pattern_cell is the one definition.
 * **Shared edges are shared**: when `x_step` equals `w`, the right edge of cell `i` and the left edge of cell `i + 1` are the
 * same double (likewise `y_step` and `h`).  The copies of a use are those whose mapped cell meets the use's device bounds;
 * they are painted in **lattice order**, increasing `j` then increasing `i`.
 *
 * ### Cells that do not overlap (PT6)
 *
 * When `x_step >= w` and `y_step >= h` (::gshscene_pattern_overlaps is 0) each item of the tile, an expanded picture's items
 * included, is **one element** for all copies: its shape at a pixel is the sum of the copies' coverages, each restricted
 * to its cell and to the clips in force, and its source is the coverage-weighted mean of the copies' sources.  A cell-filling
 * background therefore covers every pixel across a cell edge exactly once, where compositing the copies one after the
 * other would leave `1 - (1 - a)(1 - b)` at the seam.  A paint of a unit moves with its copy (the chain of its paint gets
 * the copy's translation), a clip block is the union of its copies' restricted regions, and a group is one group over all
 * copies.
 *
 * ### Cells that overlap (PT7)
 *
 * Otherwise the copies are composited in lattice order, later over earlier: with every blend of the tile Normal, each copy
 * is an isolated group of the tile clipped to its cell by the **geometric** clip of `shape-geometry` (the polygon cut, never a
 * multiplied coverage: that would make the isolated per-copy form differ from the direct one and leave a seam), composited with
 * SRC_OVER; a tile that holds another blend mode is
 * one non-isolated, non-knockout group over the lattice layer in which the copies are drawn in that order.  The order of
 * overlapping copies is this library's choice, not a claim about any format.
 *
 * ### The tile (PT8, PT9)
 *
 * The lattice layer starts transparent in the layer's blend space and does not depend on the backdrop.  A foreground or
 * palette paint in the tile resolves against the list that draws the pattern.  A colored pattern gives the lattice picture
 * times the paint's alpha multiplier; an uncolored one gives the tint, with the lattice picture's alpha: the tile's colors
 * are evaluated and discarded.
 *
 * ### What draws nothing (PT10)
 *
 * A fill whose chain has a map of exactly zero determinant (the target's, an instance's, the item's, the pattern's, and in a
 * tile the maps of every enclosing pattern) draws nothing and is noticed, `MAP_NOT_INVERTIBLE`; so does a pattern with a
 * size or a step of zero, `PATTERN_DEGENERATE`.  An empty tile draws nothing and is no notice.  ::gshscene_pattern_notice is
 * the one predicate.
 *
 * ### Nesting and bounds (PT11, PT12)
 *
 * A pattern inside a tile is evaluated per outer copy, with the outer translation composed into its chain.  A pattern's
 * picture depth is its tile's, so a chain of nested patterns is bounded by `max_picture_depth` where a list is closed into a
 * picture, at intern, and in a retained scene.  A pattern adds no outset to a fill's or stroke's bounds.  A changed
 * pattern or tile damages the nodes that paint with it; a changed foreground or palette damages those whose tile names it.
 *
 * ### Sinks (PT13, PT14)
 *
 * A sink says it draws patterns with ::GSHSCENE_CAP_PATTERN; without it the planning pass refuses a unit painted with a pattern
 * (`PATTERN`).  With it, a unit inside the tile that the sink refuses is reported with the chain extended by the unit's index
 * in the tile, and refuses the unit painted with the pattern as well: a pattern with a missing part is not the pattern.
 * The sink's own limits (copies per render, the lattice layer's size) are its resource limits.
 */

#ifndef GHOTI_IO_GSHSCENE_PATTERN_H
#define GHOTI_IO_GSHSCENE_PATTERN_H

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

/** The largest lattice index a copy's cell is computed for: `2^53 - 1`, so that it and its successor are exact doubles (PT5). */
#define GSHSCENE_PATTERN_INDEX_MAX (((int64_t)1 << 53) - 1)

/** @brief The content of a pattern (PT1). */
typedef struct GSHSCENE_Pattern {
  const GSHSCENE_Resource * tile; ///< A picture resource of the same store; retained by the pattern.
  double x;                       ///< The cell's left edge in pattern space.
  double y;                       ///< The cell's top edge.
  double w;                       ///< The cell's width: finite, not negative.
  double h;                       ///< The cell's height: finite, not negative.
  double x_step;                  ///< The lattice spacing along x: finite, not negative; kept as `w` when `repeat_x` is 0 (PT3).
  double y_step;                  ///< The lattice spacing along y: finite, not negative; kept as `h` when `repeat_y` is 0 (PT3).
  uint32_t repeat_x;              ///< 1 repeats along x; 0 draws index 0 only.
  uint32_t repeat_y;              ///< Likewise along y.
  /** From pattern space to the item's local space: a valid 2D map (a zero-filled map is refused; use the identity). */
  GSHGEOM_Map map;
  uint32_t has_tint;              ///< 1: the pattern is uncolored and takes `tint`; 0: colored.
  /** A `SOLID`, `FOREGROUND` or `PALETTE` paint, meaningful only with `has_tint` 1 (and the zero paint otherwise); copied. */
  GSHSCENE_Paint tint;
} GSHSCENE_Pattern;

/** Fill @p pattern with the defaults: no tile, the cell `(0, 0, 1, 1)`, both steps 1, both repeats 1, the identity map, no tint.  NULL is ignored. */
GSHSCENE_API void gshscene_pattern_init(GSHSCENE_Pattern * pattern);

/**
 * @brief Intern a pattern (PT1, PT2): its content is copied, `-0` stored as `+0`, the tint zeroed when there is none.
 *
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID (NULL, a tile that is not a picture of this store, a non-finite double, a
 *   negative size or step, a repeat other than 0 and 1, a map that is not a valid map, a tint beside a `has_tint` of 0 or none
 *   beside 1, a tint that is not solid, foreground or palette or is invalid as a fill's paint is);
 *   ::GSHSCENE_ERR_UNSUPPORTED (a 3D map, a reserved space); ::GSHSCENE_ERR_LIMIT (a value or a map entry past its bound, a
 *   solid tint component past N1's, a tile too deep to be nested once more, or the store's limits, decided after every
 *   validity check); ::GSHSCENE_ERR_OOM.
 */
GSHSCENE_API GSHSCENE_Result gshscene_store_intern_pattern(GSHSCENE_Store * store, const GSHSCENE_Pattern * pattern,
    const GSHSCENE_Resource ** out);

/**
 * @brief Read a pattern back.
 *
 * @param out The content as stored; `tile` points into the resource and is valid while a handle is held.  Written only on success.
 * @return ::GSHSCENE_OK or ::GSHSCENE_ERR_INVALID (NULL, not a pattern).
 */
GSHSCENE_API GSHSCENE_Result gshscene_resource_pattern(const GSHSCENE_Resource * resource, GSHSCENE_Pattern * out);

/**
 * @brief The cell of the copy `(i, j)` in pattern space (PT5): `out = {left, top, right, bottom}`.
 *
 * `left = x + i x_step` and `top = y + j y_step`, each one product of the integer index and the step and one sum.  The
 * right edge is `x + (i + 1) x_step` when `x_step` equals `w` (so that it is the very double the next cell's left edge is)
 * and `left + w` otherwise; likewise the bottom edge.  Nothing is accumulated.  An index on an axis that does not repeat
 * is not refused here: restricting to index 0 is the caller's.
 *
 * @param i The index along x, in `[-::GSHSCENE_PATTERN_INDEX_MAX, ::GSHSCENE_PATTERN_INDEX_MAX]`.
 * @param j The index along y, likewise.
 * @param out Four doubles; written only on success.
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID (NULL, an index out of range).
 */
GSHSCENE_API GSHSCENE_Result gshscene_pattern_cell(const GSHSCENE_Pattern * pattern, int64_t i, int64_t j, double out[4]);

/**
 * @brief Whether the cells overlap (PT7): `x_step < w` on an axis that repeats in x, or `y_step < h` on one that repeats in y, by exact comparison of the stored doubles.  0
 *   selects PT6 and 1 PT7.  0 for NULL.
 */
GSHSCENE_API int gshscene_pattern_overlaps(const GSHSCENE_Pattern * pattern);

/**
 * @brief Why a fill or stroke painted with a pattern draws nothing (PT10), or 0.
 *
 * ::GSHSCENE_REASON_MAP_NOT_INVERTIBLE exactly when some map of the chain, @p target_map, each of @p instance_maps
 * (which, for an item in a tile, include the item map and the map of every enclosing pattern), the item's map and the
 * pattern's, has a 2x2 linear part of exactly zero determinant, decided by `gshgeom_orient2d`'s exact sign on that map's own
 * doubles with no composition formed; otherwise ::GSHSCENE_REASON_PATTERN_DEGENERATE when `w`, `h`, `x_step` or `y_step` is 0.
 * ::gshscene_paint_notice and ::gshscene_stroke_notice call it: the planning pass and every sink get one answer.
 *
 * @param item A fill or a stroke; anything else, or one whose paint is not a pattern, gives 0.
 * @param instance_maps The maps of the enclosing instances, root to leaf; may be NULL when @p instance_count is 0.
 * @param instance_count How many.
 * @param target_map The target's map, or NULL for the identity.
 */
GSHSCENE_API GSHSCENE_Reason gshscene_pattern_notice(const GSHSCENE_Item * item, const GSHGEOM_Map * instance_maps,
    size_t instance_count, const GSHGEOM_Map * target_map);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GSHSCENE_PATTERN_H */
