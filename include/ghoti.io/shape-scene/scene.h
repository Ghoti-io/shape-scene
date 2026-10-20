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
 * @file scene.h
 *
 * The application's retained scene: the one object that publishes (AD-9, AD-14).
 *
 * The scene owns the per-scene ID counter (so IDs reproduce from run to run for
 * one call sequence), the **current** version, and the store the nodes' resources
 * are interned in.  The application opens an edit on a version, hands it to a
 * front-end or a driver, and publishes what comes back, alone
 * (::gshscene_scene_publish) or with its siblings (::gshscene_scene_merge).  A
 * front-end is given the edit and the version, never the scene.
 *
 * ### Merging
 *
 * **Edits opened on one version are siblings.**  The scene merges edits whose
 * touched sets (edit.h) are disjoint: each node of the result comes from exactly
 * one edit (or the base), so the result is the same whichever order the edits
 * are listed in, and publishing them one after the other gives the same version.
 * It **refuses** overlapping edits with ::GSHSCENE_ERR_REFUSED and a
 * ::GSHSCENE_Problems naming every ID that two touched sets share.  An edit
 * whose base is not the current version is checked the same way against what
 * the publishes since its base changed (so it merges on top of them, or is
 * refused naming the IDs).  Two edits that bind one key to different nodes are
 * refused, naming both nodes.  Nothing is published and the edits are as they
 * were.
 *
 * ### Validation at publish
 *
 * The merged result is validated as a whole before anything changes: every
 * instance's target exists in the result and there is no cycle through children
 * and instance targets (::GSHSCENE_ERR_INVALID, the instance named in the
 * problems); and every structural limit of AD-11 holds (::GSHSCENE_ERR_LIMIT):
 * `max_nodes`, `max_node_depth`, `max_expansion` (the occurrences of the root:
 * an instance of a node that holds instances of a node ... is refused at the
 * publish that passes the bound), `max_list_items` (of the version's list and of
 * each instanced node's picture), `max_depth` (the push nesting those lists
 * reach), `max_picture_depth` and `max_resource_elements` (a picture's items).
 * Each is asserted at `N` and `N + 1`.  The validation visits every node, so a
 * publish and a diff are linear in the version's node count.
 *
 * A success makes the new version current, spends the edits, and returns the
 * version with a reference for the caller.  An edit with nothing touched
 * publishes nothing and returns the current version.
 */

#ifndef GHOTI_IO_GSHSCENE_SCENE_H
#define GHOTI_IO_GSHSCENE_SCENE_H

#include <ghoti.io/shape-scene/allocator.h>
#include <ghoti.io/shape-scene/core.h>
#include <ghoti.io/shape-scene/edit.h>
#include <ghoti.io/shape-scene/macros.h>
#include <ghoti.io/shape-scene/types.h>
#include <ghoti.io/shape-scene/version.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief What is wrong with a publish.  The values are pinned. */
typedef enum GSHSCENE_Problem_Reason {
  GSHSCENE_PROBLEM_OVERLAP = 1,         ///< The ID is touched by two edits, or by an edit and a publish since its base.
  GSHSCENE_PROBLEM_KEY_CONFLICT = 2,    ///< The node is bound to a key that another node is bound to.
  GSHSCENE_PROBLEM_DANGLING_TARGET = 3, ///< The instance names a node the result does not have.
  GSHSCENE_PROBLEM_CYCLE = 4,           ///< The instance closes a cycle: its target contains it.
  /** Two edits (or an edit and a publish since its base) both set a version parameter, the foreground or the palette; the root's ID is named. */
  GSHSCENE_PROBLEM_PARAMETER = 5,
  GSHSCENE_PROBLEM_REASON_COUNT         ///< Closes the enum.
} GSHSCENE_Problem_Reason;

/** @brief Name a problem reason: "overlap", "key-conflict", "dangling-target", "cycle", or "unknown problem". */
GSHSCENE_API const char * gshscene_problem_reason_string(GSHSCENE_Problem_Reason reason);

/** @brief How many problems; 0 for NULL. */
GSHSCENE_API size_t gshscene_problems_count(const GSHSCENE_Problems * problems);

/**
 * @brief Problem @p index, in ascending order of ID (then reason).
 *
 * @return ::GSHSCENE_OK or ::GSHSCENE_ERR_INVALID (NULL, out of range).
 */
GSHSCENE_API GSHSCENE_Result gshscene_problems_get(const GSHSCENE_Problems * problems, size_t index,
    uint64_t * out_id, GSHSCENE_Problem_Reason * out_reason);

/** @brief Free the problems.  NULL is ignored. */
GSHSCENE_API void gshscene_problems_destroy(GSHSCENE_Problems * problems);

/**
 * @brief Make a scene: version 0, holding only the root group.
 *
 * @param alloc The allocator for every node, version and edit of the scene, or NULL for the default; copied.
 *   It must be thread-safe if versions or edits are used from several threads.
 * @param limits The limits (core.h), or NULL for the defaults; copied.
 * @param store The store the scene's resources live in, or NULL to make one (with @p alloc and @p limits).  A store
 *   given is retained, so it may be shared with immediate-mode lists.
 * @param out The scene; written only on success.  Free it with ::gshscene_scene_destroy.
 * @return ::GSHSCENE_OK, ::GSHSCENE_ERR_INVALID (NULL @p out) or ::GSHSCENE_ERR_OOM.
 */
GSHSCENE_API GSHSCENE_Result gshscene_scene_create(const GSHSCENE_Allocator * alloc,
    const GSHSCENE_Limits * limits, GSHSCENE_Store * store, GSHSCENE_Scene ** out);

/**
 * @brief Drop the application's reference.
 *
 * Versions and edits that exist stay valid (they hold what they need); nothing can be published any more.  NULL is ignored.
 */
GSHSCENE_API void gshscene_scene_destroy(GSHSCENE_Scene * scene);

/** @brief The scene's store, borrowed (the scene keeps it alive); NULL for NULL. */
GSHSCENE_API GSHSCENE_Store * gshscene_scene_store(const GSHSCENE_Scene * scene);

/**
 * @brief The current version.
 *
 * @param out The version, with a reference for the caller; written only on success.
 * @return ::GSHSCENE_OK or ::GSHSCENE_ERR_INVALID.
 */
GSHSCENE_API GSHSCENE_Result gshscene_scene_current(GSHSCENE_Scene * scene, const GSHSCENE_Version ** out);

/**
 * @brief Open an edit.
 *
 * @param base The version to open it on (one of this scene's), or NULL for the current one.
 * @param out The edit; written only on success.  Hand it to a front-end or driver; free it with ::gshscene_edit_destroy.
 * @return ::GSHSCENE_OK, ::GSHSCENE_ERR_INVALID (NULL, a version of another scene) or ::GSHSCENE_ERR_OOM.
 */
GSHSCENE_API GSHSCENE_Result gshscene_scene_open_edit(
    GSHSCENE_Scene * scene, const GSHSCENE_Version * base, GSHSCENE_Edit ** out);

/**
 * @brief Publish one edit.
 *
 * @param problems On ::GSHSCENE_ERR_REFUSED and ::GSHSCENE_ERR_INVALID (from validation), the problems found
 *   (free them with ::gshscene_problems_destroy); may be NULL.  Written only then.
 * @param out_version The new (or, for an empty edit, the current) version, with a reference for the caller; may be NULL.
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_REFUSED (overlap or key conflict); ::GSHSCENE_ERR_INVALID (NULL, an edit
 *   of another scene or already spent, a dangling target, a cycle); ::GSHSCENE_ERR_LIMIT; ::GSHSCENE_ERR_OOM.
 *   Anything but ::GSHSCENE_OK leaves the scene and the edit as they were.
 */
GSHSCENE_API GSHSCENE_Result gshscene_scene_publish(GSHSCENE_Scene * scene, GSHSCENE_Edit * edit,
    GSHSCENE_Problems ** problems, const GSHSCENE_Version ** out_version);

/**
 * @brief Publish several sibling edits as one version.
 *
 * The edits must share one base, which is the current version or one of its ancestors, and be distinct.  As
 * ::gshscene_scene_publish; the result does not depend on the order of @p edits.
 */
GSHSCENE_API GSHSCENE_Result gshscene_scene_merge(GSHSCENE_Scene * scene, GSHSCENE_Edit * const * edits,
    size_t count, GSHSCENE_Problems ** problems, const GSHSCENE_Version ** out_version);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GSHSCENE_SCENE_H */
