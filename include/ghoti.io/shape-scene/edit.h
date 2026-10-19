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
 * - ::gshscene_edit_upsert finds the node a key is bound to in the edit's view
 *   and sets its properties, or adds a node under the given parent and binds
 *   the key: how a front-end lowers its document again and keeps the IDs.  An
 *   identical re-lowering touches nothing and publishes nothing.
 * - ::gshscene_edit_remove_key removes the node a key names.
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
 *   ::GSHSCENE_ERR_UNSUPPORTED (a 3D map); ::GSHSCENE_ERR_LIMIT (`max_nodes`); ::GSHSCENE_ERR_OOM.
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

/** @brief Remove the node a key names; ::GSHSCENE_ERR_INVALID if none. */
GSHSCENE_API GSHSCENE_Result gshscene_edit_remove_key(GSHSCENE_Edit * edit, const GSHSCENE_Key * key);

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
