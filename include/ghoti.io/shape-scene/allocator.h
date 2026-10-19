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
 * @file allocator.h
 *
 * Allocator abstraction for the Ghoti.io Shape Scene library: the
 * `shape-geometry` allocator type (which is cutil's `GCU_Allocator`) under a
 * local name.  A `NULL` allocator argument means the default.  Everything this
 * library allocates for a caller goes through the allocator it was given and is
 * freed with the matching `_destroy`.
 */

#ifndef GHOTI_IO_GSHSCENE_ALLOCATOR_H
#define GHOTI_IO_GSHSCENE_ALLOCATOR_H

#include <ghoti.io/shape-scene/macros.h>

#include <ghoti.io/shape-geometry/allocator.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Allocator interface used by the library (cutil's, through shape-geometry). */
typedef GSHGEOM_Allocator GSHSCENE_Allocator;

/**
 * @brief The process-global default allocator.
 *
 * @return cutil's default allocator.  Never NULL.
 */
GSHSCENE_API const GSHSCENE_Allocator * gshscene_allocator_default(void);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GSHSCENE_ALLOCATOR_H */
