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
 * The three opaque types of the waist, declared together so that the headers
 * that name one in a signature need not include the one that defines it.
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

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GSHSCENE_TYPES_H */
