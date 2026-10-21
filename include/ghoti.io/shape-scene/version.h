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
 * @file version.h
 *
 * The retained scene's immutable snapshot: a version (AD-9, CAP-7).
 *
 * ### The model
 *
 * A **version** is a tree of **nodes** with 64-bit IDs.  It never changes once
 * it exists, it is reference counted and may be shared by any thread, and it
 * outlives the ::GSHSCENE_Scene handle that made it.  Changes are made in an
 * **edit** (edit.h) opened on a version and published by the application's
 * scene (scene.h); a published edit makes a new version that shares every node
 * it did not touch.  The root is a group of ID ::GSHSCENE_ROOT_ID.
 *
 * A node is one of three kinds (::GSHSCENE_Node_Kind): a **group** (children in
 * draw order, a placement map, an opacity, a blend space, a composite operator, an
 * ordering context, a layer and a depth, an optional clip), a
 * **shape** (a path or glyph run under a map, with a fill rule, a paint, a layer
 * and a depth) and an **instance** (a node of the same version drawn again
 * under a map: what SVG `use` and `symbol` lower to).  Any node may be
 * **hidden** (::GSHSCENE_NODE_HIDDEN): it is not drawn in place, only where an
 * instance reaches it (what SVG `defs` lowers to).  An instance ignores the
 * hidden flag of its own target and honours the hidden flags below it.
 *
 * A **key** (::GSHSCENE_Key) is a front-end's own name for an element: a
 * domain (the front-end) and a value.  A version keeps a map from key to node,
 * so a front-end that lowers its document again finds the nodes it made
 * (edit.h).  A key is data, not identity: the node's ID is the identity.
 *
 * ### Occurrences and instance paths
 *
 * A node is drawn once in place (unless hidden) and once for every instance
 * that reaches it, and an **occurrence** is a node together with the chain of
 * instances it was reached through.  The **instance path** of an occurrence is
 * the IDs of those instances from the root, then the node's own ID (AD-9).  Two
 * occurrences of one node differ in their paths.  ::gshscene_instance_path_compare
 * orders paths totally (element by element, a prefix before its extensions),
 * which is the order AD-10's tie-break uses; ::gshscene_instance_path_key is a
 * 64-bit key for a report.  ::gshscene_version_visit visits every occurrence in
 * draw order.
 *
 * ### The foreground
 *
 * A version carries the **foreground** (N10, paint.h): a ::GSHSCENE_Color, opaque
 * black in the target's space until an edit sets it (::gshscene_edit_set_foreground).
 * It is what a ::GSHSCENE_PAINT_FOREGROUND shape resolves to, it belongs to the version
 * and not to any node, and the version's list carries it.  Changing it publishes a new
 * version whose diff reports it and whose damage is the shapes that paint with it
 * (a gradient shape with a foreground stop among them, G3).
 *
 * ### The palette
 *
 * A version carries the **palette** (N14, paint.h) the same way: a palette resource
 * of the scene's store, or none until an edit sets one (::gshscene_edit_set_palette).
 * A ::GSHSCENE_PAINT_PALETTE shape names an entry of it; the version's list carries
 * it; changing it publishes a new version whose diff reports it
 * (::gshscene_diff_palette_changed) and whose damage is the shapes that paint from it
 * (a gradient shape with a palette stop among them, G3).
 *
 * ### The emitted list
 *
 * ::gshscene_version_list builds, on the first call, the version's list over
 * interned resources (list.h), **in paint order** (N11, N12): the walk puts the
 * children of a group in the order its ordering context gives (flat: declared;
 * overlay: by layer; depth: far to near, ties by layer and then instance path),
 * hidden children taking no part, and records every resolved depth tie in the list.
 * Each shape is a fill; a group with a clip is a clip push and pop around its
 * content (the clip push carries the group's layer and depth); a group is a group
 * push and pop when its opacity is below 1, its composite operator is not
 * source-over, its blend space is not zero, its ordering is not flat, its
 * isolation, knockout or soft mask is not the default (T1), or the
 * sequence it is in is not flat (so that it stays one unit of that sequence and
 * carries its own context); a hidden node is skipped; an instance is an instance
 * item of the **picture** of its target (the target's own emission, placed at the
 * identity, interned once per target).  Every item carries its full placement
 * relative to its list: the maps of the groups above it, composed.  The list is
 * the version's, borrowed by the caller, valid while a reference to the version
 * is held (AD-9, AD-27), and equals (::gshscene_list_equal) the recorded list
 * that draws the same.  ::gshscene_version_item_node says which node an item
 * came from.
 *
 * ### The hash of a version (format 1)
 *
 * FNV-1a 64 in the spelling of shape-geometry's hash.h, over the foreground, the
 * palette and the nodes in ascending ID order and over nothing else: not the
 * version number, the allocator, a handle or the ID counter, so it reproduces run
 * to run and across targets.  The stream is `u32` 1 (this format; 1 until the suite's first release), `u32`
 * `0x4e454353`, the foreground (four `f64` and a space), `u64` the palette's content
 * hash (0 for none), `u64` the node count, then for each node: `u64` id, `u8` kind (::GSHSCENE_Node_Kind,
 * pinned), `u8` flags, `u64` parent (0 for the root), `u8` 1 if the node has a
 * key and then `u64` domain and `u64` value; and, by kind,
 * - **group**: `u64` the map's hash (`gshgeom_map_hash`), `f64` opacity, the blend
 *   space, `u8` composite (with PDF's fields after it as a list's group push spells
 *   them, list.h, when one is not the default), `u32` ordering, `i32` layer, `f64` depth, `u8` 1 if
 *   there is a clip and then `u64` the clip resource's content hash, `u64` the clip
 *   map's hash and `u8` the clip's fill rule, then `u64` the number of children and
 *   each child's `u64` ID in draw order;
 * - **shape**: `u64` the resource's content hash, `u64` the map's hash, `u8` the
 *   fill rule, `u8` the paint kind, four `f64` (the straight `r, g, b, a`), the
 *   paint's space, `u32` the paint's palette entry, `u64` the paint's gradient's content hash (0 for another
 *   kind), `i32` layer, `f64` depth;
 * - **instance**: `u64` the target's ID, `u64` the map's hash, `i32` layer, `f64` depth.
 *
 * A **space** is `u8` primaries, `u8` transfer, `f64` gamma.
 *
 * ### The dump
 *
 * `gshscene.version 1 nodes=N root=1 fg=<r> <g> <b> <a> <space> palette=<16 hex digits> hash=<16 hex digits>`
 * and one line per node in ascending ID order, tokens separated by one space, every
 * `double` as the 16 hexadecimal digits of its bit pattern, every hash as 16 digits,
 * every ID, layer, flag and rule in decimal, a `<space>` as `P:T:<gamma hex>`:
 *
 * - `G id=I parent=P flags=F key=K map=H opacity=X blend=<space> composite=N order=N layer=L depth=D clip=C children=LIST`
 *   (with ` isolation=N knockout=N mask=<hash>` after `order=N` when one of them is not the default)
 * - `S id=I parent=P flags=F key=K res=KIND:H map=H rule=R paint=PK r g b a space=<space> entry=E gradient=G layer=L depth=D`
 * - `I id=I parent=P flags=F key=K target=T map=H layer=L depth=D`
 *
 * where `K` is `-` or `DOMAIN:VALUE` as 16 digits each, `C` is `-` or
 * `KIND:RESOURCEHASH:MAPHASH:RULE`, and `LIST` is `-` or the child IDs separated
 * by commas.  The dump ends by checking the stream's error flag, so that a write
 * lost by an unbuffered stream is reported.
 */

#ifndef GHOTI_IO_GSHSCENE_VERSION_H
#define GHOTI_IO_GSHSCENE_VERSION_H

#include <ghoti.io/shape-scene/allocator.h>
#include <ghoti.io/shape-scene/core.h>
#include <ghoti.io/shape-scene/list.h>
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

/** The ID of the root group of every version.  IDs of other nodes start at 2; 0 is never an ID. */
#define GSHSCENE_ROOT_ID ((uint64_t)1)

/** @brief What a node is.  The values are pinned (they are hashed). */
typedef enum GSHSCENE_Node_Kind {
  GSHSCENE_NODE_GROUP = 1,    ///< Children in draw order, with a placement, an opacity and an optional clip.
  GSHSCENE_NODE_SHAPE = 2,    ///< A path or glyph run, filled.
  GSHSCENE_NODE_INSTANCE = 3, ///< Another node of the same version, drawn again.
  GSHSCENE_NODE_KIND_COUNT    ///< Closes the enum.
} GSHSCENE_Node_Kind;

/** @brief Name a node kind: "group", "shape", "instance", or "unknown node kind". */
GSHSCENE_API const char * gshscene_node_kind_string(GSHSCENE_Node_Kind kind);

/** A node flag: the node and its subtree are not drawn in place, only through an instance that reaches them. */
#define GSHSCENE_NODE_HIDDEN ((uint32_t)1)

/** @brief A front-end's key for an element: a domain (the front-end) and a value.  Equal keys name the same node. */
typedef struct GSHSCENE_Key {
  uint64_t domain; ///< The front-end's own number (for instance a hash of its name).
  uint64_t value;  ///< The element's identity in that front-end.
} GSHSCENE_Key;

/**
 * @brief The properties of one node: the input of an edit and the output of a read.
 *
 * Only the fields of the node's kind are used and the others must be as
 * ::gshscene_node_init sets them (zero, the identity, `opacity` 1), so that a
 * node has one spelling; a read always returns them so.  The resources are
 * handles of the scene's store: an edit retains what it is given, and a read
 * returns pointers valid while the version is held.
 */
typedef struct GSHSCENE_Node {
  GSHSCENE_Node_Kind kind;           ///< Which kind.
  uint32_t flags;                    ///< ::GSHSCENE_NODE_HIDDEN, or 0.
  /** Group: places its children (and its clip) in its parent's space.  Shape: places the path or run.  Instance: places the target's content.  2D. */
  GSHGEOM_Map map;
  int32_t layer;                     ///< The declared layer (AD-10); group, shape and instance.
  double depth;                      ///< The declared depth (AD-10): finite; `-0` is stored as `+0`.  Group, shape and instance.
  double opacity;                    ///< Group: finite, in `[0, 1]`; below 1 the group is composited as a unit.  Else 1.
  GSHSCENE_Color_Space blend_space;  ///< Group: the space it composites in; zero inherits the enclosing group's.  Else zero.
  uint32_t composite;                ///< Group: a ::GSHSCENE_Composite (N4, B1).  Else ::GSHSCENE_COMPOSITE_SRC_OVER.
  uint32_t ordering;                 ///< Group: a ::GSHSCENE_Ordering, the context of its children.  Else ::GSHSCENE_ORDER_FLAT.
  uint32_t isolation;                ///< Group: a ::GSHSCENE_Isolation (T1); isolated, 0, by default.  Else 0.
  uint32_t knockout;                 ///< Group: 0 or 1, a knockout group (T1).  Else 0.
  /** Group: the soft mask's slot (T1, T7): NULL, or a picture of the scene's store, which the node holds.  Else NULL. */
  const GSHSCENE_Resource * soft_mask;
  const GSHSCENE_Resource * clip;    ///< Group: a path or glyph run clipping the group, or NULL.  Else NULL.
  GSHGEOM_Map clip_map;              ///< Group with a clip: the clip's placement in the group's own space.  Else the identity.
  GSHGEOM_Fill_Rule clip_rule;       ///< Group with a clip: its rule.  Else nonzero.
  const GSHSCENE_Resource * resource; ///< Shape: the path or glyph run.  Else NULL.
  GSHGEOM_Fill_Rule fill_rule;       ///< Shape: the rule.  Else nonzero.
  GSHSCENE_Paint paint;              ///< Shape: the paint (solid, foreground, palette or gradient).  Else as ::gshscene_node_init leaves it.
  uint64_t target;                   ///< Instance: the ID of the node it draws.  Else 0.
} GSHSCENE_Node;

/**
 * @brief Fill @p node with the defaults of @p kind: the 2D identity maps, opacity 1,
 *   opaque black paint, nonzero rules, layer 0, no resource, no target.
 *
 * The caller then sets the fields its kind uses.  NULL is ignored.
 */
GSHSCENE_API void gshscene_node_init(GSHSCENE_Node_Kind kind, GSHSCENE_Node * node);

/** @brief A node as a read returns it. */
typedef struct GSHSCENE_Node_Info {
  uint64_t id;          ///< The node's ID.
  uint64_t parent;      ///< Its parent's ID; 0 for the root.
  int has_key;          ///< Nonzero if a key is bound to the node.
  GSHSCENE_Key key;     ///< The key, if bound; else zeros.
  size_t child_count;   ///< Group: the children; else 0.
  GSHSCENE_Node node;   ///< The properties.
} GSHSCENE_Node_Info;

/** @brief Take another reference.  @return @p version; NULL for NULL.  Only a version a reference is held to may be retained. */
GSHSCENE_API const GSHSCENE_Version * gshscene_version_retain(const GSHSCENE_Version * version);

/** @brief Drop a reference; the last one frees the version (its nodes it shares with others are kept).  NULL is ignored. */
GSHSCENE_API void gshscene_version_release(const GSHSCENE_Version * version);

/** @brief The version's serial number (not the library's: ::gshscene_version_number): 0 for a scene's first, one more for each version published after it; 0 for NULL. */
GSHSCENE_API uint64_t gshscene_version_serial(const GSHSCENE_Version * version);

/** @brief The nodes of the version, the root included; 0 for NULL. */
GSHSCENE_API size_t gshscene_version_node_count(const GSHSCENE_Version * version);

/**
 * @brief Read a node.
 *
 * @param id The node's ID.
 * @param out The node; written only on success.  Resource pointers in it are valid while the version is held.
 * @return ::GSHSCENE_OK or ::GSHSCENE_ERR_INVALID (NULL, no node of that ID).
 */
GSHSCENE_API GSHSCENE_Result gshscene_version_node(
    const GSHSCENE_Version * version, uint64_t id, GSHSCENE_Node_Info * out);

/**
 * @brief The ID of child @p index of a group.
 *
 * @return ::GSHSCENE_OK or ::GSHSCENE_ERR_INVALID (NULL, not a group, index out of range).
 */
GSHSCENE_API GSHSCENE_Result gshscene_version_child(
    const GSHSCENE_Version * version, uint64_t id, size_t index, uint64_t * out_id);

/** @brief The ID of the node a key is bound to; ::GSHSCENE_ERR_INVALID if none (or NULL). */
GSHSCENE_API GSHSCENE_Result gshscene_version_lookup_key(
    const GSHSCENE_Version * version, const GSHSCENE_Key * key, uint64_t * out_id);

/** @brief How many keys are bound; 0 for NULL. */
GSHSCENE_API size_t gshscene_version_key_count(const GSHSCENE_Version * version);

/** @brief The version's foreground (N10); ::GSHSCENE_ERR_INVALID for NULL.  Written only on success. */
GSHSCENE_API GSHSCENE_Result gshscene_version_foreground(const GSHSCENE_Version * version, GSHSCENE_Color * out);

/** @brief The version's palette (N14), or NULL for none and for a NULL version.  The version holds the reference. */
GSHSCENE_API const GSHSCENE_Resource * gshscene_version_palette(const GSHSCENE_Version * version);

/** @brief The hash of the version, as the file comment spells it; the offset basis for NULL. */
GSHSCENE_API uint64_t gshscene_version_hash(const GSHSCENE_Version * version);

/**
 * @brief Write the version as text (see the file comment).
 *
 * @return ::GSHSCENE_OK, ::GSHSCENE_ERR_INVALID (NULL) or ::GSHSCENE_ERR_IO.
 */
GSHSCENE_API GSHSCENE_Result gshscene_version_dump(const GSHSCENE_Version * version, FILE * out);

/**
 * @brief The version's list over interned resources, built on the first call and kept.
 *
 * @param out The closed list, borrowed: the version owns it and it is valid while a reference to the
 *   version is held.  Written only on success.
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID (NULL); ::GSHSCENE_ERR_LIMIT when the store's
 *   `max_resources` or `max_bytes` or the list's `max_list_bytes` is met (the structural limits were
 *   checked at publish); ::GSHSCENE_ERR_OOM.  A failure keeps nothing: the next call tries again.
 */
GSHSCENE_API GSHSCENE_Result gshscene_version_list(const GSHSCENE_Version * version, const GSHSCENE_List ** out);

/**
 * @brief Which node an item of an emitted list came from.
 *
 * @param table 0 for the version's own list; or the ID of an instanced node, for the items of its picture.
 * @param index The item.
 * @param out_node A shape's fill names the shape; a clip push and pop name their group, a group push and pop
 *   their group; an instance item names the instance node.
 * @return ::GSHSCENE_OK, ::GSHSCENE_ERR_INVALID (NULL, a table that is not 0 or an instanced node of this
 *   version, an index out of range) or what ::gshscene_version_list returns.
 */
GSHSCENE_API GSHSCENE_Result gshscene_version_item_node(
    const GSHSCENE_Version * version, uint64_t table, size_t index, uint64_t * out_node);

/**
 * @brief The picture of an instanced node.
 *
 * @param target The ID of a node some instance of the version draws.
 * @param out_picture The picture resource, borrowed (valid while the version is held).
 * @return ::GSHSCENE_OK, ::GSHSCENE_ERR_INVALID (NULL, not an instanced node) or what ::gshscene_version_list returns.
 */
GSHSCENE_API GSHSCENE_Result gshscene_version_picture(
    const GSHSCENE_Version * version, uint64_t target, const GSHSCENE_Resource ** out_picture);

/** @brief An occurrence, as the visitor receives it. */
typedef struct GSHSCENE_Visit {
  uint64_t node;                 ///< The node.
  const uint64_t * path;         ///< The instance path: instance IDs from the root, then @p node.  Valid during the call.
  size_t path_count;             ///< At least 1; `path[path_count - 1] == node`.
  GSHSCENE_Node_Kind kind;       ///< The node's kind.
  const GSHSCENE_Node * props;   ///< The node's properties, valid during the call.
  /**
   * The composed placement of the occurrence in the scene's space, formed as a sink forms it
   * (`gshgeom_map_compose_chain` over the maps of the instance items and the item's own): for a
   * group, the map its children are placed by (its parent's composed with its own).
   */
  GSHGEOM_Map map;
} GSHSCENE_Visit;

/** @brief Called for each occurrence; a nonzero return stops the visit and is returned by it. */
typedef int (*GSHSCENE_Visit_Fn)(void * ctx, const GSHSCENE_Visit * visit);

/**
 * @brief Visit every occurrence of the version in draw order.
 *
 * A group is visited before its children; a hidden node is skipped (not drawn in place); an instance is
 * visited and then the occurrences of its target's subtree, with the instance's ID added to their paths.
 * The count of occurrences is bounded by the `max_expansion` the version was published within.
 *
 * @return 0 if every occurrence was visited, a visitor's nonzero return, or -1 for NULL arguments or a
 *   failure to allocate or compose.
 */
GSHSCENE_API int gshscene_version_visit(const GSHSCENE_Version * version, GSHSCENE_Visit_Fn fn, void * ctx);

/**
 * @brief Compare two instance paths: element by element, a prefix before its extensions.
 *
 * @return negative, zero or positive as @p a is before, equal to or after @p b.
 */
GSHSCENE_API int gshscene_instance_path_compare(
    const uint64_t * a, size_t a_count, const uint64_t * b, size_t b_count);

/** @brief A 64-bit key of an instance path (FNV-1a 64 over `u64` count and the IDs), for a report. */
GSHSCENE_API uint64_t gshscene_instance_path_key(const uint64_t * path, size_t count);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GSHSCENE_VERSION_H */
