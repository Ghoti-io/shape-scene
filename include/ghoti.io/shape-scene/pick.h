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
 * @file pick.h
 *
 * Picking: which items of a list, and so which nodes of a version, a point of the device lattice lies on (PK1 to PK12).
 *
 * A pick answers "what is under this point" for the pixels a render draws, without a sink: it places every shape of a closed
 * list under the maps the sinks place it under, flattens it the way they do (the same integer flattener, the same tolerance),
 * and decides the point against it with shape-geometry's exact hit test (`hit.h`, HT1 to HT6).  It reads the list and the
 * resources it names and changes nothing; a closed list and a version are immutable, so any number of picks may run at once.
 *
 * - **PK1 The point.**  A point is a 26.6 device point (`hit.h`, HT1) and the target is the map from the list's space to the
 *   device (a zero-filled map is the identity), as a sink's target is.  The center of pixel `(x, y)` is `(64 x + 32, 64 y + 32)`.
 *   The units of the target are pixels: a stroke's width in the style's own unit is the one the sinks use at a unit of 1.
 * - **PK2 What is tested.**  The geometry of each item, in paint order: a fill's shape (a path; the glyphs of a run, from outlines
 *   or from a font by the route a raster sink takes, `glyph.h`; or the plane, which is every point), a stroke's region (its
 *   expansion, SK5: the body, together with the aligned part where the clip part selects it; a fill painted with a hatch is the stroke
 *   of its lines, HX13, and not its shape), under every clip in force.  The
 *   clips in force at an item are every clip push not yet popped, in the list and in the lists of the instances that
 *   enclose it (N9 makes a group's own clip, pushed before the group, apply to its result).  A clip is a region (a path or a run
 *   under its rule; a union, CU1, with each member intersected with its inner clips, CI1) and an instance is its picture's items
 *   under the instance's map, composed as a sink composes it.  Paint, opacity, blend mode, soft mask and isolation do not
 *   take part (PK8), nor does the antialias mode: the region is the area of the shape.
 * - **PK3 Three answers and an unknown.**  A point is, against a region, *inside* (it is in the region, by the rule, and on no
 *   edge), *outside* (it is not), or *on the boundary* (it lies exactly on an edge of one of the contours, HT3, which decides
 *   nothing about the region: a caller that wants the closed region takes it as inside, the open region as outside).  An item
 *   whose region cannot be formed here (PK9) has the answer *undecided*.  Regions combine by the following, which are
 *   sound for every input and exact except at the edges: the region of an intersection is outside when any part is outside,
 *   otherwise undecided when any part is, otherwise inside when all are inside, else on the boundary; of a union, inside when
 *   any part is inside, otherwise undecided when any part is, otherwise outside when all are, else on the boundary;
 *   a complement exchanges inside and outside.
 * - **PK4 The hits.**  The result lists every item whose answer is inside, on the boundary or undecided, **topmost first**: the
 *   reverse of paint order, so entry 0 is the item painted last.  Items whose answer is outside are not listed.  Each entry
 *   has its **chain**, the key of a report entry (report.h): the first index is an item of the list and each further index an
 *   item of the picture of the instance the previous one names.  For a version, ::gshscene_pick_resolve gives the chain's
 *   node and the instance path (the IDs of the instances it was reached through, then the node).
 * - **PK5 Order.**  The list is in paint order (it is closed), so the order is the list's: the reverse of the order in which a
 *   sink composites the items.  Where a sink's draw of two items is not ordered by the list (a depth tie, N11) the list
 *   has already ordered them, so the pick agrees with it.
 * - **PK6 Instances.**  An instance is entered through its picture, under its map, with the clips and groups as they stand;
 *   a picture's own pushes are balanced.
 * - **PK7 Placeholders** draw nothing (PH1) and are never hit.
 * - **PK8 Visibility.**  By default every fill and stroke is tested whatever its paint, so that a transparent shape is
 *   pickable (an invisible hit area).  With ::GSHSCENE_PICK_VISIBLE a fill or stroke of a solid paint with alpha 0, and every
 *   item inside a group of opacity 0, is not.
 * - **PK9 What is undecided.**  A run from a font whose route a raster sink refuses, or a glyph of one that the provider could not
 *   make (`glyph.h`'s refusals); a shape that does not fit the geometry's limits (a coordinate that does not round into
 *   26.6, or more points than `max_points`).  Each is listed as ::GSHSCENE_PICK_UNDECIDED with a ::GSHSCENE_Pick_Why, never
 *   skipped, and never the reason a pick fails.  A pick fails (::GSHSCENE_ERR_OOM, or an invalid argument) before
 *   it returns anything.
 * - **PK10 Exactness.**  Everything is decided in integers (HT3, HT4), so a result is the same on every target and run.  The
 *   point is tested against the polylines a sink fills, so for a pixel center away from every edge the answer is the one
 *   the sink's whole-or-nothing coverage (AA2) gives, which is the agreement the hit test promises.
 * - **PK11 Memory.**  Everything the pick allocates, the entries included, comes from the allocator given and is freed by
 *   ::gshscene_pick_destroy; the list and the version are only read.  A pick of a list of `n` items in `h` hits with
 *   chains of length at most `d` takes memory in `O(n_clips + h d)` plus the geometry of one item at a time.
 * - **PK12 Truncation.**  `max_hits` keeps the topmost entries; ::gshscene_pick_total says how many there were.
 */

#ifndef GHOTI_IO_GSHSCENE_PICK_H
#define GHOTI_IO_GSHSCENE_PICK_H

#include <ghoti.io/shape-scene/allocator.h>
#include <ghoti.io/shape-scene/core.h>
#include <ghoti.io/shape-scene/list.h>
#include <ghoti.io/shape-scene/macros.h>
#include <ghoti.io/shape-scene/types.h>

#include <ghoti.io/shape-geometry/device.h>
#include <ghoti.io/shape-geometry/map.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The result of a pick.  Opaque; free it with ::gshscene_pick_destroy. */
typedef struct GSHSCENE_Pick GSHSCENE_Pick;

/** @brief Flag of ::GSHSCENE_Pick_Params.flags: leave out what a render would not make visible (PK8). */
#define GSHSCENE_PICK_VISIBLE ((uint32_t)1)

/** @brief What an entry says of the point (PK3).  The values are pinned. */
typedef enum GSHSCENE_Pick_State {
  GSHSCENE_PICK_BOUNDARY = 1,  ///< On an edge of a contour of the item's region or of a clip in force.
  GSHSCENE_PICK_INSIDE = 2,    ///< In the item's region and in every clip in force, on no edge.
  GSHSCENE_PICK_UNDECIDED = 3, ///< The item's region could not be formed here (PK9); see ::GSHSCENE_Pick_Entry.why.
  GSHSCENE_PICK_STATE_COUNT    ///< Closes the enum.
} GSHSCENE_Pick_State;

/** @brief Why an entry is undecided (PK9).  The values are pinned.  Zero for a decided entry. */
typedef enum GSHSCENE_Pick_Why {
  GSHSCENE_PICK_WHY_NONE = 0,  ///< Decided.
  GSHSCENE_PICK_WHY_GLYPH = 1, ///< A run from a font whose route is refused, or a glyph its provider could not make.
  GSHSCENE_PICK_WHY_LIMIT = 2, ///< The region does not fit the geometry's limits.
  GSHSCENE_PICK_WHY_COUNT      ///< Closes the enum.
} GSHSCENE_Pick_Why;

/** @brief What to ask (PK1). */
typedef struct GSHSCENE_Pick_Params {
  GSHGEOM_Map target;          ///< The map from the list's space to the device pixels, 2D; a zero-filled map (dimension 0) is the identity.
  GSHGEOM_Device_Point point;  ///< The point, in 26.6 device units.
  uint32_t flags;              ///< ::GSHSCENE_PICK_VISIBLE or 0; any other bit is ::GSHSCENE_ERR_INVALID.
  size_t max_hits;             ///< The most entries kept, the topmost (PK12); 0 keeps them all.
  uint8_t _reserved[8];        ///< Zero.
} GSHSCENE_Pick_Params;

/** @brief One entry, as read back (PK4). */
typedef struct GSHSCENE_Pick_Entry {
  GSHSCENE_Pick_State state;  ///< What the point is against the item (never outside).
  GSHSCENE_Pick_Why why;      ///< For an undecided entry, why; else ::GSHSCENE_PICK_WHY_NONE.
  GSHSCENE_Item_Kind kind;    ///< The item: ::GSHSCENE_ITEM_FILL or ::GSHSCENE_ITEM_STROKE.
  const size_t * chain;       ///< The key (PK4); valid until the pick is destroyed.
  size_t chain_count;         ///< Its length, at least 1.
} GSHSCENE_Pick_Entry;

/**
 * @brief Pick a closed list (PK1 to PK12).
 *
 * @param alloc  The allocator for the result and the scratch; NULL for the default.
 * @param list   A closed list; it and the resources it names are only read.
 * @param params What to ask; its map must be valid and 2D.
 * @param out    The pick; written only on success.
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID (a NULL argument, an open list, an invalid or 3D map, an unknown flag or a
 *         non-zero reserved byte); ::GSHSCENE_ERR_OOM.
 */
GSHSCENE_API GSHSCENE_Result gshscene_pick_list(const GSHSCENE_Allocator * alloc, const GSHSCENE_List * list,
    const GSHSCENE_Pick_Params * params, GSHSCENE_Pick ** out);

/**
 * @brief Pick a version's list (it is built on first use, version.h).
 *
 * As ::gshscene_pick_list over ::gshscene_version_list; the result resolves against the same version with
 * ::gshscene_pick_resolve.  ::GSHSCENE_ERR_LIMIT when the list cannot be built (version.h).
 */
GSHSCENE_API GSHSCENE_Result gshscene_pick_version(const GSHSCENE_Allocator * alloc, const GSHSCENE_Version * version,
    const GSHSCENE_Pick_Params * params, GSHSCENE_Pick ** out);

/** @brief Free a pick; NULL is ignored. */
GSHSCENE_API void gshscene_pick_destroy(GSHSCENE_Pick * pick);

/** @brief How many entries the pick holds (at most `max_hits` when that is not 0); 0 for NULL. */
GSHSCENE_API size_t gshscene_pick_count(const GSHSCENE_Pick * pick);

/** @brief How many items had a hit, before `max_hits` kept the topmost of them (PK12); 0 for NULL. */
GSHSCENE_API size_t gshscene_pick_total(const GSHSCENE_Pick * pick);

/**
 * @brief Read entry @p index, topmost first (PK4).
 *
 * @return ::GSHSCENE_OK or ::GSHSCENE_ERR_INVALID (NULL, out of range).  @p out is written only on success.
 */
GSHSCENE_API GSHSCENE_Result gshscene_pick_entry(const GSHSCENE_Pick * pick, size_t index, GSHSCENE_Pick_Entry * out);

/**
 * @brief The node and the instance path of entry @p index, for the version the pick was made on (PK4).
 *
 * As ::gshscene_report_resolve: the path is the IDs of the instance nodes the item was reached through and then the
 * item's node, `chain_count` in all.
 *
 * @param out_node  The node; written only on success.
 * @param out_path  Room for the path: at least the entry's `chain_count` IDs; written only on success.
 * @param capacity  Its length.
 * @param out_count The path's length; written only on success.
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID (NULL, out of range, a capacity below the chain, or a chain that is not an
 *         item of this version's list and its pictures).
 */
GSHSCENE_API GSHSCENE_Result gshscene_pick_resolve(const GSHSCENE_Pick * pick, const GSHSCENE_Version * version,
    size_t index, uint64_t * out_node, uint64_t * out_path, size_t capacity, size_t * out_count);

/** @brief Name a state ("boundary", "inside", "undecided"), or "unknown pick state"; never NULL. */
GSHSCENE_API const char * gshscene_pick_state_string(GSHSCENE_Pick_State state);

/** @brief Name a reason ("none", "glyph", "limit"), or "unknown pick reason"; never NULL. */
GSHSCENE_API const char * gshscene_pick_why_string(GSHSCENE_Pick_Why why);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GSHSCENE_PICK_H */
