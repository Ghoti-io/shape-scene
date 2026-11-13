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
 * @file edit.h
 *
 * The edit: what a front-end or a driver is given, fills, and returns
 * unpublished (AD-7, AD-9, AD-14, CAP-11).
 *
 * An edit is opened by the application on a version (scene.h).  It keeps a
 * working view over that version, so a read through it shows its own changes.
 * **There is no publish in this header, and none that takes an edit alone**:
 * publishing needs the application's ::GSHSCENE_Scene, which no function
 * returns from an edit or a version.  A front-end that includes only this
 * header has nothing to publish with and nothing to ask for it with
 * (`make check-publish-types` compiles the probes that show it).
 *
 * ### Operations
 *
 * Each is atomic: a call that fails leaves the edit as it was and consumes no
 * ID.  Nodes are made with ::gshscene_node_init and filled in (version.h).
 * IDs are issued by the scene's counter, from 2, and never twice.
 *
 * - ::gshscene_edit_add puts a new node under a group, at a position (an index,
 *   or ::GSHSCENE_APPEND), and binds a key to it if one is given.
 * - ::gshscene_edit_set replaces a node's properties; the kind cannot change.
 *   Giving a node the properties it has changes nothing and touches nothing.
 * - ::gshscene_edit_remove removes a node and its whole subtree and unbinds
 *   their keys.  The root cannot be removed.
 * - ::gshscene_edit_move gives a node a new parent and position.  A node cannot
 *   move into its own subtree, and the root cannot move.
 * - A stroke (stroke.h, SK13) is a node like a shape: added, set and upserted by
 *   key through these calls, with its centerline, style and paint in its properties.
 * - ::gshscene_edit_upsert finds the node a key is bound to in the edit's view
 *   and sets its properties, or adds a node under the given parent and binds
 *   the key: how a front-end lowers its document again and keeps the IDs.  An
 *   identical re-lowering touches nothing and publishes nothing.
 * - ::gshscene_edit_remove_key removes the node a key names.
 * - ::gshscene_edit_import copies a subtree of another version in, with new IDs (IM1 to IM6).
 *
 * What an edit **touches**, which decides what merges (scene.h): the IDs it
 * changed, added or removed.  Adding or removing a child touches the parent; a
 * move touches the node and both parents; a removal touches every node of the
 * subtree.  A change that leaves a node equal to its base is not a touch.
 *
 * An instance's target is checked when the scene publishes, not when the
 * instance is added, so that an edit can add an instance and its target in
 * either order.
 *
 * An edit is used from one thread at a time.  The store it hands out is
 * thread-safe, as always.
 */

#ifndef GHOTI_IO_GSHSCENE_EDIT_H
#define GHOTI_IO_GSHSCENE_EDIT_H

#include <ghoti.io/shape-scene/core.h>
#include <ghoti.io/shape-scene/macros.h>
#include <ghoti.io/shape-scene/resource.h>
#include <ghoti.io/shape-scene/types.h>
#include <ghoti.io/shape-scene/version.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** A position that means "after the last child". */
#define GSHSCENE_APPEND ((size_t)-1)

/** @brief Free an edit that was not published (or one that was).  NULL is ignored. */
GSHSCENE_API void gshscene_edit_destroy(GSHSCENE_Edit * edit);

/** @brief The version the edit was opened on, borrowed (valid while the edit exists); NULL for NULL. */
GSHSCENE_API const GSHSCENE_Version * gshscene_edit_base(const GSHSCENE_Edit * edit);

/**
 * @brief The store of the scene, in which to intern the resources a node names; NULL for NULL.
 *
 * Borrowed: the edit keeps it alive; the caller does not destroy it.
 */
GSHSCENE_API GSHSCENE_Store * gshscene_edit_store(const GSHSCENE_Edit * edit);

/**
 * @brief Add a node.
 *
 * @param parent The ID of a group in the edit's view.
 * @param position An index from 0 to the group's child count, or ::GSHSCENE_APPEND.
 * @param props The node's properties (see ::GSHSCENE_Node); the kind is the node's.
 * @param key A key to bind to the node, or NULL.  A key already bound in the view is ::GSHSCENE_ERR_INVALID.
 * @param out_id The new node's ID; written only on success; may be NULL.
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID (NULL, an edit that is spent, no such group, a position out
 *   of range, properties the kind forbids, a resource of another store, a bound key);
 *   ::GSHSCENE_ERR_UNSUPPORTED (a 3D map; a stroke's 3D centerline, SK4); ::GSHSCENE_ERR_LIMIT (`max_nodes`; a shape's
 *   or a stroke's paint with a component past
 *   `2^16`, N1, decided after the properties' validity); ::GSHSCENE_ERR_OOM.
 */
GSHSCENE_API GSHSCENE_Result gshscene_edit_add(GSHSCENE_Edit * edit, uint64_t parent, size_t position,
    const GSHSCENE_Node * props, const GSHSCENE_Key * key, uint64_t * out_id);

/** @brief Replace the properties of a node (the kind must be the same).  Errors as ::gshscene_edit_add. */
GSHSCENE_API GSHSCENE_Result gshscene_edit_set(GSHSCENE_Edit * edit, uint64_t id, const GSHSCENE_Node * props);

/** @brief Remove a node and its subtree; the root cannot be removed (::GSHSCENE_ERR_INVALID). */
GSHSCENE_API GSHSCENE_Result gshscene_edit_remove(GSHSCENE_Edit * edit, uint64_t id);

/**
 * @brief Give a node a new parent and position.
 *
 * @param position An index from 0 to the new parent's child count after the node has left its old place
 *   (so for a move within one group, the count without it), or ::GSHSCENE_APPEND.
 * @return ::GSHSCENE_ERR_INVALID for the root, a node moved into its own subtree, or a parent that is not a group.
 */
GSHSCENE_API GSHSCENE_Result gshscene_edit_move(
    GSHSCENE_Edit * edit, uint64_t id, uint64_t new_parent, size_t position);

/**
 * @brief Set the node a key is bound to, or add one and bind it.
 *
 * @param parent, @p position Used only when the key is not bound (see ::gshscene_edit_add).
 * @param out_id The node's ID, bound or new; may be NULL.
 * @return As ::gshscene_edit_add; a bound node of a different kind than @p props is ::GSHSCENE_ERR_INVALID.
 */
GSHSCENE_API GSHSCENE_Result gshscene_edit_upsert(GSHSCENE_Edit * edit, const GSHSCENE_Key * key,
    uint64_t parent, size_t position, const GSHSCENE_Node * props, uint64_t * out_id);

/**
 * @brief Copy a subtree of another version into the edit, as new nodes with new IDs (IM1 to IM6).
 *
 * This is how content moves between scenes: an ID belongs to the scene that issued it, so nothing keeps its ID across the move.
 *
 * - **IM1 What is copied.**  The node @p source_node of @p source and every node below it, in draw order, each with the properties
 *   it has there (kind, flags, maps, layer, depth, opacity, composite, ordering, isolation, knockout, clips, masks, paint, rule,
 *   antialias, style, placeholder).  The copy of the node is added under @p parent at @p position as ::gshscene_edit_add adds a
 *   node, and the copies of its descendants keep their order under the copies of their parents.  Any node may be the source,
 *   the root included (its copy is then a group like any other).
 * - **IM2 New IDs.**  Every copy takes the next ID of the scene's counter, in the order above (the copy of the source node first),
 *   so the IDs an import gives are a function of the scene's counter and the subtree, and the same import into the same state
 *   gives the same IDs.
 * - **IM3 Keys are not carried.**  A front-end key is that front-end's data (AD-9) and names an element of its own document; the
 *   copies are bound to no key, and the front-end that owns the destination binds its own.
 * - **IM4 Resources are copied into the edit's store.**  A node names resources of a store, and the copies must name resources of the
 *   edit's: each resource a node names (its shape's or stroke's, a group's clip, soft mask, a stroke's style, a paint's) is
 *   copied with ::gshscene_store_copy_resource, which is the resource itself when it is already of the edit's store (two scenes that
 *   share a store, or a version of the edit's own scene) and an equal resource interned there otherwise, with everything it names.
 *   The foreground and the palette are the scene's, not the node's: a palette paint of a copy names an entry of the destination's
 *   palette.  A face keeps its binding (::gshscene_store_copy_resource, CP4), and the destination store's limits apply.
 * - **IM5 Instances.**  An instance's target is an ID of the source.  An instance whose target is inside the imported subtree
 *   points at the copy of that target; an instance whose target is outside it has nothing to point at in the destination and
 *   is ::GSHSCENE_ERR_INVALID (import the subtree that holds the target as well, under a group that holds both).
 * - **IM6 Atomic.**  A failing call leaves the edit's view as it was.  The IDs it issued before failing are spent, never reused
 *   (a gap in the numbering, which another edit of the scene may be drawing from at the same time), and are the one thing a
 *   failing call changes.
 *
 * @param parent The ID of a group in the edit's view.
 * @param position An index from 0 to the group's child count, or ::GSHSCENE_APPEND.
 * @param source A version, borrowed; it may be a version of the edit's own scene (a copy of content within one scene) or of any other
 *   scene, whatever its store.
 * @param source_node The ID in @p source of the node to copy.
 * @param out_id The ID of the copy of @p source_node; written only on success; may be NULL.
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID (NULL, a spent edit, no such node in @p source, no such group in the edit, a position out of range,
 *   an instance whose target is not in the subtree); ::GSHSCENE_ERR_LIMIT (`max_nodes`, the destination store's limits, or what
 *   ::gshscene_edit_add says of a paint); ::GSHSCENE_ERR_OOM.
 */
GSHSCENE_API GSHSCENE_Result gshscene_edit_import(GSHSCENE_Edit * edit, uint64_t parent, size_t position,
    const GSHSCENE_Version * source, uint64_t source_node, uint64_t * out_id);

/** @brief Remove the node a key names; ::GSHSCENE_ERR_INVALID if none. */
GSHSCENE_API GSHSCENE_Result gshscene_edit_remove_key(GSHSCENE_Edit * edit, const GSHSCENE_Key * key);

/**
 * @brief Set the foreground of the version this edit makes (N10): the color a ::GSHSCENE_PAINT_FOREGROUND shape resolves to.
 *
 * An edit that sets the foreground to what its base has changes nothing.  Two edits of one base that both set it
 * overlap (the root is named, with ::GSHSCENE_PROBLEM_PARAMETER).
 *
 * @param color The color; copied.
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID (NULL, a spent edit, an invalid color); ::GSHSCENE_ERR_UNSUPPORTED (a
 *   reserved color space); ::GSHSCENE_ERR_LIMIT (a component past `2^16`, N1, after the validity checks).  A failing
 *   call changes nothing.
 */
GSHSCENE_API GSHSCENE_Result gshscene_edit_set_foreground(GSHSCENE_Edit * edit, const GSHSCENE_Color * color);

/** @brief The foreground of the edit's view (its own, if set, else its base's); ::GSHSCENE_ERR_INVALID for NULL.  Written only on success. */
GSHSCENE_API GSHSCENE_Result gshscene_edit_foreground(const GSHSCENE_Edit * edit, GSHSCENE_Color * out);

/**
 * @brief Set the palette of the version this edit makes (N14): the table a ::GSHSCENE_PAINT_PALETTE shape's entry
 *   indexes.
 *
 * An edit that sets the palette its base has changes nothing.  Two edits of one base that both set it overlap (the
 * root is named, with ::GSHSCENE_PROBLEM_PARAMETER), as does one whose base's palette a publish since has changed.
 *
 * @param palette A palette resource of the scene's store (retained by the edit), or NULL for none.
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID (NULL edit, a spent edit, a resource that is not a palette of the
 *   scene's store).  A failing call changes nothing.
 */
GSHSCENE_API GSHSCENE_Result gshscene_edit_set_palette(GSHSCENE_Edit * edit, const GSHSCENE_Resource * palette);

/** @brief The palette of the edit's view (its own, if set, else its base's); NULL for none and for NULL. */
GSHSCENE_API const GSHSCENE_Resource * gshscene_edit_palette(const GSHSCENE_Edit * edit);

/** @brief Read a node of the edit's view (its base and its own changes).  As ::gshscene_version_node. */
GSHSCENE_API GSHSCENE_Result gshscene_edit_node(
    const GSHSCENE_Edit * edit, uint64_t id, GSHSCENE_Node_Info * out);

/** @brief The ID of child @p index of a group in the view.  As ::gshscene_version_child. */
GSHSCENE_API GSHSCENE_Result gshscene_edit_child(
    const GSHSCENE_Edit * edit, uint64_t id, size_t index, uint64_t * out_id);

/** @brief The ID of the node a key is bound to in the view; ::GSHSCENE_ERR_INVALID if none. */
GSHSCENE_API GSHSCENE_Result gshscene_edit_lookup_key(
    const GSHSCENE_Edit * edit, const GSHSCENE_Key * key, uint64_t * out_id);

/** @brief The nodes the view holds (the base's, plus added, minus removed); 0 for NULL. */
GSHSCENE_API size_t gshscene_edit_node_count(const GSHSCENE_Edit * edit);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GSHSCENE_EDIT_H */
