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
 * @file types.h
 *
 * The opaque types of the library, declared together so that the headers that
 * name one in a signature need not include the one that defines it.  The first
 * three are the waist (story 10); the next five the retained scene above it
 * (story 11); the last two the sink contract (story 13a).
 */

#ifndef GHOTI_IO_GSHSCENE_TYPES_H
#define GHOTI_IO_GSHSCENE_TYPES_H

#include <ghoti.io/shape-scene/macros.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief A table that interns resources by content.  Opaque; thread-safe.  See resource.h. */
typedef struct GSHSCENE_Store GSHSCENE_Store;

/** @brief An immutable, interned piece of content, used by handle.  Opaque.  See resource.h. */
typedef struct GSHSCENE_Resource GSHSCENE_Resource;

/** @brief A transient, arena-backed sequence of draw items.  Opaque.  See list.h. */
typedef struct GSHSCENE_List GSHSCENE_List;

/** @brief An immutable, reference-counted snapshot of a retained scene.  Opaque; shareable across threads.  See version.h. */
typedef struct GSHSCENE_Version GSHSCENE_Version;

/** @brief An unpublished set of changes opened on a version: what a front-end or driver is given.  Opaque.  See edit.h. */
typedef struct GSHSCENE_Edit GSHSCENE_Edit;

/** @brief The application's retained scene: the one object that can publish.  Opaque.  See scene.h. */
typedef struct GSHSCENE_Scene GSHSCENE_Scene;

/** @brief What a refused or invalid publish found wrong, naming IDs.  Opaque.  See scene.h. */
typedef struct GSHSCENE_Problems GSHSCENE_Problems;

/** @brief The difference between two versions: changed IDs and damage.  Opaque.  See diff.h. */
typedef struct GSHSCENE_Diff GSHSCENE_Diff;

/** @brief What a render did not do as stated, and its summary: the caller's, reused across frames.  Opaque.  See report.h. */
typedef struct GSHSCENE_Report GSHSCENE_Report;

/** @brief A backend that renders lists: an ops table and its implementation.  Opaque.  See sink.h. */
typedef struct GSHSCENE_Sink GSHSCENE_Sink;

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GSHSCENE_TYPES_H */
