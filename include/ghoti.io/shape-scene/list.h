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
 * @file list.h
 *
 * The list: a transient, arena-backed sequence of draw items over interned
 * resources, owned and freed in one call by its maker (AD-27, CAP-16).  The
 * record calls are the immediate-mode path: no IDs, no versions, no scene
 * (AD-1, AD-9).  A sink consumes only a closed list and the resources it
 * names.
 *
 * ### Items
 *
 * | Kind | Resource | Map | Fill rule | Paint | Other |
 * | --- | --- | --- | --- | --- | --- |
 * | ::GSHSCENE_ITEM_FILL | path or glyph run | placement | yes | yes | layer |
 * | ::GSHSCENE_ITEM_CLIP_PUSH | path or glyph run | placement | yes | | |
 * | ::GSHSCENE_ITEM_CLIP_POP | | | | | |
 * | ::GSHSCENE_ITEM_GROUP_PUSH | | | | | opacity, blend space, ordering, layer |
 * | ::GSHSCENE_ITEM_GROUP_POP | | | | | |
 * | ::GSHSCENE_ITEM_INSTANCE | picture | placement | | | layer |
 *
 * The **map** places the resource's own coordinates (the model space of its
 * path) in the space of the list; the sink composes the maps of the enclosing
 * instances and the target's last (AD-24, `gshgeom_map_compose_chain`).  A
 * **clip push** intersects the region with the clip in force until its pop; a
 * **group push** composites what follows, until its pop, as a unit with the
 * opacity; both nest, and an **instance** draws the items of a picture under
 * its map, in order, as if the picture's items stood in the list at that place
 * (a picture's own pushes are balanced, so it leaves the clip and group stacks
 * as it found them).  `layer` is the declared layer of AD-10, an `int32`.
 *
 * Each field that does not apply to an item's kind is zero (the identity for a
 * map), so an item has one spelling.
 *
 * ### Lifetime
 *
 * The list holds one reference on each resource it names, and one on its
 * store.  ::gshscene_list_destroy releases them all and frees the arena in one
 * pass; ::gshscene_list_reset does the same and keeps the first chunk of the
 * arena, for a frame loop.  An item pointer from ::gshscene_list_item is valid
 * until the list is reset or destroyed.  A list is used from one thread at a
 * time; a closed list is immutable and may be read from many.
 *
 * ### The hash of a list (version 1)
 *
 * In the spelling of shape-geometry's hash.h: `u32` 1 (this format), `u32`
 * `0x5453494c`, `u64` the item count; then for each item a `u8` kind
 * (::GSHSCENE_Item_Kind, pinned) and:
 *
 * - **fill**: `u64` the resource's content hash, `u64` the map's hash
 *   (`gshgeom_map_hash`), `u8` the fill rule, the paint (`u8` kind, four `f64`,
 *   `u32` color space), `i32` layer.
 * - **clip push**: `u64` resource hash, `u64` map hash, `u8` fill rule.
 * - **group push**: `f64` opacity, `u32` blend space, `u32` ordering, `i32` layer.
 * - **instance**: `u64` resource hash, `u64` map hash, `i32` layer.
 * - **clip pop**, **group pop**: nothing.
 *
 * Handles, ids and the allocator are not part of it.
 *
 * ### The dump
 *
 * `gshscene.list 1 items=N hash=<16 hex digits>` and one line per item:
 * `F res=<kind>:<hash> map=<hash> rule=N paint=N <r> <g> <b> <a> space=N layer=N`,
 * `C res=<kind>:<hash> map=<hash> rule=N`, `c`, `G opacity=<hex> blend=N order=N
 * layer=N`, `g`, `I res=<kind>:<hash> map=<hash> layer=N`, with every `double`
 * as the 16 hexadecimal digits of its bit pattern and every hash as 16 digits.
 */

#ifndef GHOTI_IO_GSHSCENE_LIST_H
#define GHOTI_IO_GSHSCENE_LIST_H

#include <ghoti.io/shape-scene/allocator.h>
#include <ghoti.io/shape-scene/core.h>
#include <ghoti.io/shape-scene/macros.h>
#include <ghoti.io/shape-scene/paint.h>
#include <ghoti.io/shape-scene/resource.h>
#include <ghoti.io/shape-scene/types.h>

#include <ghoti.io/shape-geometry/fill.h>
#include <ghoti.io/shape-geometry/map.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief What an item is.  The values are pinned (they are hashed). */
typedef enum GSHSCENE_Item_Kind {
  GSHSCENE_ITEM_FILL = 1,       ///< Fill a resource under a map with a paint.
  GSHSCENE_ITEM_CLIP_PUSH = 2,  ///< Intersect the clip with a resource's region.
  GSHSCENE_ITEM_CLIP_POP = 3,   ///< End the innermost clip.
  GSHSCENE_ITEM_GROUP_PUSH = 4, ///< Begin an opacity group.
  GSHSCENE_ITEM_GROUP_POP = 5,  ///< End the innermost group and composite it.
  GSHSCENE_ITEM_INSTANCE = 6,   ///< Draw a picture under a map.
  GSHSCENE_ITEM_KIND_COUNT      ///< Closes the enum.
} GSHSCENE_Item_Kind;

/**
 * @brief The ordering context of a group (AD-10).
 *
 * Only flat is defined today; the others are reserved for the story that gives
 * them meaning and are ::GSHSCENE_ERR_UNSUPPORTED until then.
 */
typedef enum GSHSCENE_Ordering {
  GSHSCENE_ORDER_FLAT = 0,    ///< Declared order.
  GSHSCENE_ORDER_DEPTH = 1,   ///< Reserved: joins the 3D depth sort.
  GSHSCENE_ORDER_OVERLAY = 2, ///< Reserved.
  GSHSCENE_ORDERING_COUNT     ///< Closes the enum.
} GSHSCENE_Ordering;

/** @brief The properties of a group. */
typedef struct GSHSCENE_Group {
  double opacity;       ///< Finite, in `[0, 1]`.
  uint32_t blend_space; ///< 0, the target's blend space; anything else is ::GSHSCENE_ERR_UNSUPPORTED until story 12.
  uint32_t ordering;    ///< A ::GSHSCENE_Ordering; only ::GSHSCENE_ORDER_FLAT is accepted today.
} GSHSCENE_Group;

/** @brief One entry of a list, as read back. */
typedef struct GSHSCENE_Item {
  GSHSCENE_Item_Kind kind;           ///< Which item.
  const GSHSCENE_Resource * resource; ///< Fill, clip push, instance: the resource (the list holds the reference); else NULL.
  GSHGEOM_Map map;                   ///< Fill, clip push, instance: the placement; else the 2D identity.
  GSHGEOM_Fill_Rule fill_rule;       ///< Fill, clip push: the rule; else nonzero.
  GSHSCENE_Paint paint;              ///< Fill: the paint; else solid transparent black.
  GSHSCENE_Group group;              ///< Group push: its properties; else zero.
  int32_t layer;                     ///< Fill, group push, instance: the declared layer; else 0.
} GSHSCENE_Item;

/**
 * @brief Make an empty, open list for @p store.
 *
 * @param alloc The allocator for the list's arena, or NULL for the default;
 *   copied.  The caller owns the list and everything it allocates.
 * @param limits The limits, or NULL for the defaults; copied.
 * @param store The store whose resources the list may name; the list holds a reference.
 * @param out The list; written only on success.  Free it with ::gshscene_list_destroy.
 * @return ::GSHSCENE_OK, ::GSHSCENE_ERR_INVALID (NULL) or ::GSHSCENE_ERR_OOM.
 */
GSHSCENE_API GSHSCENE_Result gshscene_list_create(const GSHSCENE_Allocator * alloc,
    const GSHSCENE_Limits * limits, GSHSCENE_Store * store, GSHSCENE_List ** out);

/** @brief Release every reference, free the arena in one pass and free the list.  NULL is ignored. */
GSHSCENE_API void gshscene_list_destroy(GSHSCENE_List * list);

/**
 * @brief Release every reference and make the list empty and open, keeping its first chunk.
 *
 * @return ::GSHSCENE_OK or ::GSHSCENE_ERR_INVALID (NULL).
 */
GSHSCENE_API GSHSCENE_Result gshscene_list_reset(GSHSCENE_List * list);

/**
 * @brief Record a fill.
 *
 * Every record call returns ::GSHSCENE_ERR_INVALID for a NULL list, a closed list or an
 * argument the item's rules forbid (including a resource of the wrong kind or of another store, an
 * invalid map, a fill rule outside the enum), ::GSHSCENE_ERR_UNSUPPORTED for a reserved
 * value, ::GSHSCENE_ERR_LIMIT when a limit would pass and ::GSHSCENE_ERR_OOM; and a failing call
 * leaves the list as it was and takes no reference.
 *
 * @param list The list.
 * @param resource A path or glyph run of the list's store.
 * @param map The placement, or NULL for the 2D identity.
 * @param rule The fill rule.
 * @param paint The paint (solid), or NULL for opaque black.
 * @param layer The declared layer.
 */
GSHSCENE_API GSHSCENE_Result gshscene_list_fill(GSHSCENE_List * list, const GSHSCENE_Resource * resource,
    const GSHGEOM_Map * map, GSHGEOM_Fill_Rule rule, const GSHSCENE_Paint * paint, int32_t layer);

/** @brief Begin a clip: the region of a path or glyph run, under @p map, by @p rule. */
GSHSCENE_API GSHSCENE_Result gshscene_list_clip_push(GSHSCENE_List * list,
    const GSHSCENE_Resource * resource, const GSHGEOM_Map * map, GSHGEOM_Fill_Rule rule);

/** @brief End the innermost clip; ::GSHSCENE_ERR_INVALID if the innermost open item is not one. */
GSHSCENE_API GSHSCENE_Result gshscene_list_clip_pop(GSHSCENE_List * list);

/**
 * @brief Begin a group.
 *
 * @param group Its properties, or NULL for opacity 1, the target's blend space, flat.
 * @param layer The declared layer.
 */
GSHSCENE_API GSHSCENE_Result gshscene_list_group_push(
    GSHSCENE_List * list, const GSHSCENE_Group * group, int32_t layer);

/** @brief End the innermost group; ::GSHSCENE_ERR_INVALID if the innermost open item is not one. */
GSHSCENE_API GSHSCENE_Result gshscene_list_group_pop(GSHSCENE_List * list);

/** @brief Draw a picture of the list's store under @p map (NULL: the 2D identity). */
GSHSCENE_API GSHSCENE_Result gshscene_list_instance(GSHSCENE_List * list,
    const GSHSCENE_Resource * picture, const GSHGEOM_Map * map, int32_t layer);

/**
 * @brief Seal the list.
 *
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID for NULL, a list already
 *   closed, or a clip or group still open (the list stays open).
 */
GSHSCENE_API GSHSCENE_Result gshscene_list_close(GSHSCENE_List * list);

/** @brief Nonzero if the list is closed; 0 for NULL. */
GSHSCENE_API int gshscene_list_is_closed(const GSHSCENE_List * list);

/** @brief How many items; 0 for NULL. */
GSHSCENE_API size_t gshscene_list_count(const GSHSCENE_List * list);

/**
 * @brief Read item @p index.
 *
 * @param index From 0 to the count minus one.
 * @param out The item; written only on success; valid until the list is reset or destroyed.
 * @return ::GSHSCENE_OK or ::GSHSCENE_ERR_INVALID (NULL, out of range).
 */
GSHSCENE_API GSHSCENE_Result gshscene_list_item(
    const GSHSCENE_List * list, size_t index, const GSHSCENE_Item ** out);

/**
 * @brief The items the list would be with every instance replaced by its picture's items.
 *
 * An instance counts 1 plus its picture's expansion; saturating at the largest `size_t`.
 */
GSHSCENE_API size_t gshscene_list_expansion(const GSHSCENE_List * list);

/** @brief The bytes of arena chunks the list has taken from its allocator; 0 for NULL. */
GSHSCENE_API size_t gshscene_list_bytes(const GSHSCENE_List * list);

/**
 * @brief Whether two lists are the same: item for item, resources by handle, maps and doubles bit for bit.
 *
 * @return 1 if equal, 0 if not (or if either is NULL).
 */
GSHSCENE_API int gshscene_list_equal(const GSHSCENE_List * a, const GSHSCENE_List * b);

/** @brief The hash of the list (open or closed), as the file comment spells it; the offset basis for NULL. */
GSHSCENE_API uint64_t gshscene_list_hash(const GSHSCENE_List * list);

/**
 * @brief Write the list as text (see the file comment).
 *
 * @return ::GSHSCENE_OK, ::GSHSCENE_ERR_INVALID (NULL) or ::GSHSCENE_ERR_IO.
 */
GSHSCENE_API GSHSCENE_Result gshscene_list_dump(const GSHSCENE_List * list, FILE * out);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GSHSCENE_LIST_H */
