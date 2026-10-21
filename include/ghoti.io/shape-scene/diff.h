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
 * @file diff.h
 *
 * The one diff of two versions, and conservative damage (AD-9, AD-11, CAP-7).
 *
 * ### The changed IDs
 *
 * ::gshscene_diff lists, ascending, every ID present in one version only
 * (::GSHSCENE_DIFF_ADDED, ::GSHSCENE_DIFF_REMOVED) and every ID present in both
 * whose node differs in content (::GSHSCENE_DIFF_CHANGED): its kind, flags, parent,
 * key, properties or children.  The two versions must be of one scene.
 *
 * ### The damage
 *
 * The **damage** covers every device pixel whose colour could differ between
 * drawing the first version and the second through a map, drawn as a painter
 * would.  A sink may enlarge it and never shrink it.  It is computed over
 * occurrences (version.h), in both versions:
 *
 * - a node whose own properties changed (flags, map, layer, depth, opacity, blend
 *   space, composite operator, ordering, clip, resource, rule, paint, target, style)
 *   damages every occurrence of its whole subtree; a changed key or a changed
 *   parent alone damages nothing by itself;
 * - a changed foreground damages every occurrence of every shape and stroke that
 *   paints with it (N10, and a gradient paint with a foreground stop, G3), and
 *   nothing else; a changed palette, every occurrence of every shape and stroke that
 *   paints from it (N14, and a gradient paint with a palette stop), and nothing else;
 * - an occurrence of a group whose composite operator is unbounded (N4: it changes
 *   the backdrop where it has no content) that lies in a damaged subtree makes the
 *   damage unbounded;
 * - a group whose own properties are equal and whose children differ damages the
 *   occurrences of the children added and removed, and of every child both
 *   versions have if their order differs;
 * - an instance that reaches a changed node reaches it in the walk, so every
 *   instance of a changed symbol is damaged;
 * - the bounds of a shape occurrence are the exact bounds of its path, the box
 *   of its four corners under the composed map and the map, outset by the AA
 *   support and rounded outward to whole pixels; of a glyph run from outlines,
 *   the union over its glyphs; of a run from a font (story 14a, glyph.h), as a
 *   pixel sink draws it: on the fast tier the control box of each glyph's
 *   device path from the face's provider (hinting included), on the general
 *   route each glyph's design box under its map, and nothing for a glyph the
 *   provider refuses; the bounds of a stroke occurrence are SK10's
 *   (stroke.h, ::gshscene_stroke_bounds): its centerline's box in model space
 *   (a run from a font by the general route, each glyph's design box under its
 *   map, SK7) outset by the style's conservative reach, locally or in pixels of
 *   the map for a device width, then outset by the AA support and rounded outward.
 *   The clip of a group is not intersected in.
 *
 * Where no bound exists the damage is **unbounded** and a sink redraws the whole
 * target: a run whose face is unbound, a 3D path, a map that cannot be applied,
 * a result past the range of the integers.
 */

#ifndef GHOTI_IO_GSHSCENE_DIFF_H
#define GHOTI_IO_GSHSCENE_DIFF_H

#include <ghoti.io/shape-scene/allocator.h>
#include <ghoti.io/shape-scene/core.h>
#include <ghoti.io/shape-scene/macros.h>
#include <ghoti.io/shape-scene/types.h>
#include <ghoti.io/shape-scene/version.h>

#include <ghoti.io/shape-geometry/map.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The declared outset of a fill, in device pixels on every side: the support of the default anti-aliasing filter
 * as a damage rule needs it (the box filter touches only the pixels its path touches, so this is slack: the
 * outset the damage rule declares, which a wider filter widens).
 */
#define GSHSCENE_AA_SUPPORT_DEFAULT 1.0

/** @brief How an ID changed.  The values are pinned. */
typedef enum GSHSCENE_Diff_Kind {
  GSHSCENE_DIFF_ADDED = 1,   ///< In the second version only.
  GSHSCENE_DIFF_REMOVED = 2, ///< In the first version only.
  GSHSCENE_DIFF_CHANGED = 3, ///< In both, with different content.
  GSHSCENE_DIFF_KIND_COUNT   ///< Closes the enum.
} GSHSCENE_Diff_Kind;

/** @brief A rectangle of whole device pixels, half open: `x0 <= x < x1`, `y0 <= y < y1`. */
typedef struct GSHSCENE_Rect {
  int64_t x0; ///< Left edge, included.
  int64_t y0; ///< Top edge, included.
  int64_t x1; ///< Right edge, excluded.
  int64_t y1; ///< Bottom edge, excluded.
} GSHSCENE_Rect;

/** @brief The damage of a diff. */
typedef struct GSHSCENE_Damage {
  int unbounded;       ///< Nonzero: no bound exists; redraw the whole target.  Then @p empty and @p rect are zero.
  int empty;           ///< Nonzero: no pixel can differ.  Then @p rect is zero.
  GSHSCENE_Rect rect;  ///< Otherwise the rectangle that covers every pixel that can differ.
} GSHSCENE_Damage;

/** @brief Options of the damage computation. */
typedef struct GSHSCENE_Damage_Options {
  double aa_support; ///< Device pixels of outset on every side: finite, at least 0.  ::GSHSCENE_AA_SUPPORT_DEFAULT when options are NULL.
} GSHSCENE_Damage_Options;

/**
 * @brief The difference between two versions of one scene.
 *
 * @param a The earlier version.
 * @param b The later version.
 * @param map The sink's map from the scene's space to device space (2D), or NULL for the identity.
 * @param options The options, or NULL for the defaults.
 * @param out The diff; written only on success.  Free it with ::gshscene_diff_destroy.
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID (NULL, versions of different scenes, an invalid map, a negative or
 *   non-finite support); ::GSHSCENE_ERR_OOM.  A 3D map is not an error: the damage is unbounded.
 */
GSHSCENE_API GSHSCENE_Result gshscene_diff(const GSHSCENE_Version * a, const GSHSCENE_Version * b,
    const GSHGEOM_Map * map, const GSHSCENE_Damage_Options * options, GSHSCENE_Diff ** out);

/** @brief How many IDs differ; 0 for NULL. */
GSHSCENE_API size_t gshscene_diff_count(const GSHSCENE_Diff * diff);

/**
 * @brief Changed ID @p index, ascending.
 *
 * @return ::GSHSCENE_OK or ::GSHSCENE_ERR_INVALID (NULL, out of range).
 */
GSHSCENE_API GSHSCENE_Result gshscene_diff_get(
    const GSHSCENE_Diff * diff, size_t index, uint64_t * out_id, GSHSCENE_Diff_Kind * out_kind);

/**
 * @brief Whether the two versions' foregrounds differ (N10); 0 for NULL.
 *
 * Their nodes may be equal: the shapes that paint with the foreground are then in the damage and not in the changed IDs.
 */
GSHSCENE_API int gshscene_diff_foreground_changed(const GSHSCENE_Diff * diff);

/**
 * @brief Whether the two versions' palettes differ (N14: another handle, or one and none); 0 for NULL.
 *
 * Their nodes may be equal: the shapes that paint from the palette are then in the damage and not in the changed IDs.
 */
GSHSCENE_API int gshscene_diff_palette_changed(const GSHSCENE_Diff * diff);

/** @brief The damage; ::GSHSCENE_ERR_INVALID for NULL. */
GSHSCENE_API GSHSCENE_Result gshscene_diff_damage(const GSHSCENE_Diff * diff, GSHSCENE_Damage * out);

/** @brief Free the diff.  NULL is ignored. */
GSHSCENE_API void gshscene_diff_destroy(GSHSCENE_Diff * diff);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GSHSCENE_DIFF_H */
