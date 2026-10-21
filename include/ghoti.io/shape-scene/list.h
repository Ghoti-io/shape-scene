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
 * | ::GSHSCENE_ITEM_FILL | path or glyph run | placement | yes | yes | layer, depth |
 * | ::GSHSCENE_ITEM_CLIP_PUSH | path or glyph run | placement | yes | | layer, depth |
 * | ::GSHSCENE_ITEM_CLIP_POP | | | | | |
 * | ::GSHSCENE_ITEM_GROUP_PUSH | | | | | opacity, blend space, composite, ordering, layer, depth |
 * | ::GSHSCENE_ITEM_GROUP_POP | | | | | |
 * | ::GSHSCENE_ITEM_INSTANCE | picture | placement | | | layer, depth |
 *
 * The **map** places the resource's own coordinates (the model space of its
 * path) in the space of the list; the sink composes the maps of the enclosing
 * instances and the target's last (AD-24, `gshgeom_map_compose_chain`).  A
 * **clip push** intersects the region with the clip in force until its pop; a
 * **group push** composites what follows, until its pop, as a unit (N8); both
 * nest, and an **instance** draws the items of a picture under its map, in order,
 * as if the picture's items stood in the list at that place (a picture's own
 * pushes are balanced, so it leaves the clip and group stacks as it found them).
 * `layer` is the declared layer of AD-10, an `int32`, and `depth` its declared
 * distance from the viewer, a finite `double` (`-0` is stored as `+0`); they
 * order units in a group that says so (N11).
 *
 * Each field that does not apply to an item's kind is zero (the identity for a
 * map), so an item has one spelling.
 *
 * ### What a list means (normative; the whole text is in documentation/design.md)
 *
 * All of it in real arithmetic, with no rounding or clamping unless said (a
 * sink's precision is its own; N1's saturation, paint.h, is said).  Values are premultiplied, in a **layer** in a
 * blend space.  **N4**: an operator maps source `S = (Cs, As)` and backdrop
 * `D = (Cd, Ad)` to `(Fa Cs + Fb Cd, Fa As + Fb Ad)` with `(Fa, Fb)` of
 * `CLEAR (0, 0)`, `SRC (1, 0)`, `DEST (0, 1)`, `SRC_OVER (1, 1-As)`,
 * `DEST_OVER (1-Ad, 1)`, `SRC_IN (Ad, 0)`, `DEST_IN (0, As)`, `SRC_OUT (1-Ad, 0)`,
 * `DEST_OUT (0, 1-As)`, `SRC_ATOP (Ad, 1-As)`, `DEST_ATOP (1-Ad, As)`,
 * `XOR (1-Ad, 1-As)`, and `PLUS`: `min(1, Cs + Cd)` per channel and
 * `min(1, As + Ad)`.  **N5**: `apply(D, S, m, op) = sat((1 - m) D + m op(S, D))`
 * for a coverage `m`, saturated at `2^64` (N1).  **N6**: the list's root layer is the target (its blend space is
 * the target's); a group's layer starts at `(0, 0, 0, 0)` in its own blend space
 * (its `blend_space`, or its enclosing group's if zero).  **N7**: a fill is
 * `apply(L, S, k * c, SRC_OVER)` where `S` is the paint's color converted to the
 * layer's space and premultiplied, `k` the coverage of the shape and `c` the
 * product of the clips in force.  **N8**: at a group's pop, `S` is
 * `opacity * convert(G)` and the result is `apply(P, S, m, composite)` with `m`
 * the product of the clips that were in force in the enclosing group at the push.
 * **N9**: the clips in force are those pushed and not popped within the same
 * group (the root counts as one); a group push does not take them into its own
 * layer.  **N10**: a ::GSHSCENE_PAINT_FOREGROUND fill uses the list's foreground
 * (::gshscene_list_set_foreground).  **N14**: a ::GSHSCENE_PAINT_PALETTE fill
 * uses an entry of the list's palette (::gshscene_list_set_palette), and one
 * whose entry is missing is refused.  **G1 to G13** (paint.h): a
 * ::GSHSCENE_PAINT_GRADIENT fill takes its color at each point from a gradient
 * resource.  **N11, N12**: the order is made by
 * ::gshscene_list_close, as below.
 *
 * ### Order and ties (N11, N12)
 *
 * A **sequence** is the items directly inside the root, a group or a clip
 * block; its **units** are its fills, instances, group blocks and clip blocks
 * (push to matching pop); a unit's key is the layer and depth of its first item.
 * The context of a sequence is the ordering of the group that encloses it (the
 * root's is flat; a clip block has the context of the sequence it is in):
 * **flat** paints in declared order; **overlay** by ascending layer, equal layers
 * in declared order; **depth** by descending depth (far first), equal depths
 * (exactly, as real numbers) broken by ascending layer and then by declared order
 * (a retained scene's emission breaks them by instance path).  ::gshscene_list_close puts the
 * list in paint order, rewriting the items, so a sink never sorts, and records
 * every pair of adjacent tied units as a ::GSHSCENE_Tie.  The ties are an
 * annotation for a report: they are in the dump and in neither the hash nor the
 * equality of a list.
 *
 * ### Lifetime
 *
 * The list holds one reference on each resource it names, and one on its
 * store.  ::gshscene_list_destroy releases them all and frees the arena in one
 * pass; ::gshscene_list_reset does the same and keeps the first chunk of the
 * arena, for a frame loop.  An item pointer from ::gshscene_list_item is valid
 * until the list is reset, destroyed or closed (a close may rewrite the items); a
 * list is used from one thread at a time, and a closed list is immutable and may
 * be read from many.
 *
 * ### The hash of a list (format 1)
 *
 * In the spelling of shape-geometry's hash.h: `u32` 1 (this format; 1 until
 * the suite's first release), `u32`
 * `0x5453494c`, the foreground (four `f64` and a space), `u64` the palette's
 * content hash (0 for none), `u64` the item count; then for each item a `u8`
 * kind (::GSHSCENE_Item_Kind, pinned) and:
 *
 * - **fill**: `u64` the resource's content hash, `u64` the map's hash
 *   (`gshgeom_map_hash`), `u8` the fill rule, the paint (`u8` kind, four `f64`, a
 *   space, `u32` entry, `u64` the gradient's content hash, 0 for another kind),
 *   `i32` layer, `f64` depth.
 * - **clip push**: `u64` resource hash, `u64` map hash, `u8` fill rule, `i32`
 *   layer, `f64` depth.
 * - **group push**: `f64` opacity, a space (the blend space), `u8` composite,
 *   `u32` ordering, `i32` layer, `f64` depth.
 * - **instance**: `u64` resource hash, `u64` map hash, `i32` layer, `f64` depth.
 * - **clip pop**, **group pop**: nothing.
 *
 * A **space** is `u8` primaries, `u8` transfer, `f64` gamma.  Handles, ids, the
 * allocator and the ties are not part of it.
 *
 * ### The dump
 *
 * `gshscene.list 1 items=N ties=T fg=<r> <g> <b> <a> <space> palette=<hash> hash=<16 hex digits>`
 * (the palette's content hash, 0 for none) and one line per item:
 * `F res=<kind>:<hash> map=<hash> rule=N paint=N <r> <g> <b> <a> space=<space> entry=E gradient=<hash> layer=N depth=<hex>`,
 * `C res=<kind>:<hash> map=<hash> rule=N layer=N depth=<hex>`, `c`,
 * `G opacity=<hex> blend=<space> composite=N order=N layer=N depth=<hex>`, `g`,
 * `I res=<kind>:<hash> map=<hash> layer=N depth=<hex>`, then one line per tie,
 * `T first=I second=J depth=<hex> reason=N`, with every `double` as the 16
 * hexadecimal digits of its bit pattern, every hash as 16 digits and a `<space>`
 * as `P:T:<gamma hex>` (primaries and transfer in decimal).
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
 * @brief The ordering context of a group (AD-10, N11): how the units of the sequence inside it are painted.
 */
typedef enum GSHSCENE_Ordering {
  GSHSCENE_ORDER_FLAT = 0,    ///< Declared order; layers and depths are ignored.
  GSHSCENE_ORDER_DEPTH = 1,   ///< Descending depth (far first); ties by ascending layer, then instance path or declared order.
  GSHSCENE_ORDER_OVERLAY = 2, ///< Ascending layer; equal layers in declared order.
  GSHSCENE_ORDERING_COUNT     ///< Closes the enum.
} GSHSCENE_Ordering;

/**
 * @brief How a group's result is composited onto what it is drawn over (N4).  The values are pinned (they are hashed).
 *
 * The twelve Porter-Duff operators and `PLUS`.  Blend modes (multiply, screen, ...) are not here: they are a later
 * addition and the values past the end are ::GSHSCENE_ERR_INVALID until then.
 */
typedef enum GSHSCENE_Composite {
  GSHSCENE_COMPOSITE_SRC_OVER = 0,  ///< `(Fa, Fb) = (1, 1-As)`: the default, and what a fill is.
  GSHSCENE_COMPOSITE_CLEAR = 1,     ///< `(0, 0)`.
  GSHSCENE_COMPOSITE_SRC = 2,       ///< `(1, 0)`.
  GSHSCENE_COMPOSITE_DEST = 3,      ///< `(0, 1)`.
  GSHSCENE_COMPOSITE_DEST_OVER = 4, ///< `(1-Ad, 1)`.
  GSHSCENE_COMPOSITE_SRC_IN = 5,    ///< `(Ad, 0)`.
  GSHSCENE_COMPOSITE_DEST_IN = 6,   ///< `(0, As)`.
  GSHSCENE_COMPOSITE_SRC_OUT = 7,   ///< `(1-Ad, 0)`.
  GSHSCENE_COMPOSITE_DEST_OUT = 8,  ///< `(0, 1-As)`.
  GSHSCENE_COMPOSITE_SRC_ATOP = 9,  ///< `(Ad, 1-As)`.
  GSHSCENE_COMPOSITE_DEST_ATOP = 10,///< `(1-Ad, As)`.
  GSHSCENE_COMPOSITE_XOR = 11,      ///< `(1-Ad, 1-As)`.
  GSHSCENE_COMPOSITE_PLUS = 12,     ///< `min(1, Cs + Cd)` per channel, `min(1, As + Ad)`.
  GSHSCENE_COMPOSITE_COUNT          ///< Closes the enum.
} GSHSCENE_Composite;

/** @brief Name an operator ("src-over", "clear", "src", "dest", "dest-over", "src-in", "dest-in", "src-out", "dest-out", "src-atop", "dest-atop", "xor", "plus"), or "unknown composite". */
GSHSCENE_API const char * gshscene_composite_string(GSHSCENE_Composite op);

/**
 * @brief Whether the operator changes the backdrop where the source is empty (N4): `CLEAR`, `SRC`, `SRC_IN`,
 *   `DEST_IN`, `SRC_OUT` and `DEST_ATOP`.  0 for the rest and for a value outside the enum.
 */
GSHSCENE_API int gshscene_composite_is_unbounded(GSHSCENE_Composite op);

/** @brief The properties of a group. */
typedef struct GSHSCENE_Group {
  double opacity;                   ///< Finite, in `[0, 1]`: the group's result is scaled by it before it is composited.
  GSHSCENE_Color_Space blend_space; ///< The space the group composites in; zero inherits the enclosing group's (the root's is the target's).
  uint32_t ordering;                ///< A ::GSHSCENE_Ordering: the context of the sequence inside the group.
  uint32_t composite;               ///< A ::GSHSCENE_Composite: how the result is composited onto the enclosing layer.
} GSHSCENE_Group;

/** @brief One entry of a list, as read back. */
typedef struct GSHSCENE_Item {
  GSHSCENE_Item_Kind kind;           ///< Which item.
  const GSHSCENE_Resource * resource; ///< Fill, clip push, instance: the resource (the list holds the reference); else NULL.
  GSHGEOM_Map map;                   ///< Fill, clip push, instance: the placement; else the 2D identity.
  GSHGEOM_Fill_Rule fill_rule;       ///< Fill, clip push: the rule; else nonzero.
  GSHSCENE_Paint paint;              ///< Fill: the paint; else solid transparent black.
  GSHSCENE_Group group;              ///< Group push: its properties; else zero.
  int32_t layer;                     ///< Fill, clip push, group push, instance: the declared layer; else 0.
  double depth;                      ///< The same items: the declared depth, finite; else 0.
} GSHSCENE_Item;

/** @brief What resolved a tie.  The values are pinned (they are dumped). */
typedef enum GSHSCENE_Tie_Reason {
  GSHSCENE_TIE_LAYER = 1,    ///< The units' layers differ: the lower layer is painted first.
  GSHSCENE_TIE_PATH = 2,     ///< The layers are equal and the instance paths differ (a retained scene's emission).
  GSHSCENE_TIE_DECLARED = 3, ///< Nothing but declared order did (a recorded list has no instance paths).
  GSHSCENE_TIE_REASON_COUNT  ///< Closes the enum.
} GSHSCENE_Tie_Reason;

/** @brief A resolved depth tie: two adjacent units of a depth sequence with equal depths (N11). */
typedef struct GSHSCENE_Tie {
  size_t first;              ///< The index of the first item of the unit painted first, in the list as closed.
  size_t second;             ///< The index of the first item of the unit painted after it.
  double depth;              ///< The depth they share.
  GSHSCENE_Tie_Reason reason; ///< What broke the tie.
} GSHSCENE_Tie;

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
 * leaves the list as it was and takes no reference.  A fill whose solid paint has a component past `2^16` in
 * magnitude is ::GSHSCENE_ERR_LIMIT (N1, paint.h), decided after every other argument's validity.
 *
 * @param list The list.
 * @param resource A path or glyph run of the list's store.
 * @param map The placement, or NULL for the 2D identity.
 * @param rule The fill rule.
 * @param paint The paint (solid, foreground, palette, or a gradient of the list's store, which the list retains), or
 *   NULL for opaque black.
 * @param layer The declared layer.
 * @param depth The declared depth: finite (else ::GSHSCENE_ERR_INVALID).
 */
GSHSCENE_API GSHSCENE_Result gshscene_list_fill(GSHSCENE_List * list, const GSHSCENE_Resource * resource,
    const GSHGEOM_Map * map, GSHGEOM_Fill_Rule rule, const GSHSCENE_Paint * paint, int32_t layer, double depth);

/** @brief Begin a clip: the region of a path or glyph run, under @p map, by @p rule; its layer and depth order it as a unit (N11). */
GSHSCENE_API GSHSCENE_Result gshscene_list_clip_push(GSHSCENE_List * list,
    const GSHSCENE_Resource * resource, const GSHGEOM_Map * map, GSHGEOM_Fill_Rule rule, int32_t layer, double depth);

/** @brief End the innermost clip; ::GSHSCENE_ERR_INVALID if the innermost open item is not one. */
GSHSCENE_API GSHSCENE_Result gshscene_list_clip_pop(GSHSCENE_List * list);

/**
 * @brief Begin a group.
 *
 * @param group Its properties, or NULL for opacity 1, the enclosing blend space, flat, source-over.
 * @param layer The declared layer.
 * @param depth The declared depth: finite.
 */
GSHSCENE_API GSHSCENE_Result gshscene_list_group_push(
    GSHSCENE_List * list, const GSHSCENE_Group * group, int32_t layer, double depth);

/** @brief End the innermost group; ::GSHSCENE_ERR_INVALID if the innermost open item is not one. */
GSHSCENE_API GSHSCENE_Result gshscene_list_group_pop(GSHSCENE_List * list);

/** @brief Draw a picture of the list's store under @p map (NULL: the 2D identity). */
GSHSCENE_API GSHSCENE_Result gshscene_list_instance(GSHSCENE_List * list,
    const GSHSCENE_Resource * picture, const GSHGEOM_Map * map, int32_t layer, double depth);

/**
 * @brief Set the list's foreground (N10), the color a ::GSHSCENE_PAINT_FOREGROUND fill resolves to.
 *
 * @param color The color, or NULL for the default (opaque black in the target's space); copied.
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID (NULL list, a closed list, an invalid color);
 *   ::GSHSCENE_ERR_UNSUPPORTED (a reserved space); ::GSHSCENE_ERR_LIMIT (a component past `2^16`, N1, after the
 *   validity checks).  A failing call changes nothing.
 */
GSHSCENE_API GSHSCENE_Result gshscene_list_set_foreground(GSHSCENE_List * list, const GSHSCENE_Color * color);

/** @brief The list's foreground; ::GSHSCENE_ERR_INVALID for NULL.  Written only on success. */
GSHSCENE_API GSHSCENE_Result gshscene_list_foreground(const GSHSCENE_List * list, GSHSCENE_Color * out);

/**
 * @brief Set the list's palette (N14), whose entries ::GSHSCENE_PAINT_PALETTE fills name.
 *
 * @param palette A palette resource of the list's store, which the list retains (releasing the one it had), or NULL
 *   for none.
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID (NULL list, a closed list, a resource that is not a palette of the
 *   list's store).  A failing call changes nothing.
 */
GSHSCENE_API GSHSCENE_Result gshscene_list_set_palette(GSHSCENE_List * list, const GSHSCENE_Resource * palette);

/** @brief The list's palette, or NULL for none and for a NULL list.  The list holds the reference. */
GSHSCENE_API const GSHSCENE_Resource * gshscene_list_palette(const GSHSCENE_List * list);

/**
 * @brief Seal the list, putting it in paint order (N11, N12).
 *
 * The units of every sequence are permuted as its context says, the items are rewritten and every
 * resolved depth tie is recorded.  The list a sink sees is in paint order.
 *
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID for NULL, a list already
 *   closed, or a clip or group still open (the list stays open); ::GSHSCENE_ERR_OOM (the list stays
 *   open and unchanged, and can be closed again).
 */
GSHSCENE_API GSHSCENE_Result gshscene_list_close(GSHSCENE_List * list);

/** @brief Nonzero if the list is closed; 0 for NULL. */
GSHSCENE_API int gshscene_list_is_closed(const GSHSCENE_List * list);

/** @brief How many items; 0 for NULL. */
GSHSCENE_API size_t gshscene_list_count(const GSHSCENE_List * list);

/** @brief How many depth ties the closed list recorded; 0 for NULL and for an open list. */
GSHSCENE_API size_t gshscene_list_tie_count(const GSHSCENE_List * list);

/**
 * @brief Read tie @p index, in ascending order of its second item.
 *
 * @param out The tie; written only on success; valid until the list is reset or destroyed.
 * @return ::GSHSCENE_OK or ::GSHSCENE_ERR_INVALID (NULL, out of range).
 */
GSHSCENE_API GSHSCENE_Result gshscene_list_tie(const GSHSCENE_List * list, size_t index, const GSHSCENE_Tie ** out);

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
 * @brief Whether two lists are the same: foreground, palette (by handle), then item for item, resources by handle, maps and doubles bit for bit (the ties are not compared).
 *
 * @return 1 if equal, 0 if not (or if either is NULL).
 */
GSHSCENE_API int gshscene_list_equal(const GSHSCENE_List * a, const GSHSCENE_List * b);

/** @brief The hash of the list (open or closed), as the file comment spells it; the offset basis for NULL.  The ties are not in it. */
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
