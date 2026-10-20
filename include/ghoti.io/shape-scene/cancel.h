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
 * @file cancel.h
 *
 * The cancel token of a render (sink.h, S7).
 *
 * A ::GSHSCENE_Cancel is the caller's: it lives in the caller's memory, is
 * named by a render request, and is set by any thread, at any time, with
 * ::gshscene_cancel_set (a UI thread cancelling a render in flight, AD-11).  A
 * sink only reads it, at the points its capabilities declare, and never
 * clears it; the caller clears it with ::gshscene_cancel_init before reusing
 * it.  A zero-filled token is clear.  The flag is a cutil atomic, so a set on
 * one thread is seen by a check on another without a lock.
 */

#ifndef GHOTI_IO_GSHSCENE_CANCEL_H
#define GHOTI_IO_GSHSCENE_CANCEL_H

#include <ghoti.io/shape-scene/macros.h>

#include <ghoti.io/cutil/atomic.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief A cancel token: a flag any thread may set.  Zero-filled is clear. */
typedef struct GSHSCENE_Cancel {
  GCU_Atomic_Int flag; ///< Private: use the functions below.
} GSHSCENE_Cancel;

/** @brief Clear @p cancel (for a first use or a reuse).  Not while a render that names it is in flight.  NULL is ignored. */
GSHSCENE_API void gshscene_cancel_init(GSHSCENE_Cancel * cancel);

/** @brief Set @p cancel, from any thread.  Setting a set token does nothing more.  NULL is ignored. */
GSHSCENE_API void gshscene_cancel_set(GSHSCENE_Cancel * cancel);

/** @brief Nonzero if @p cancel is set; 0 for NULL (a render with no token cannot be cancelled). */
GSHSCENE_API int gshscene_cancel_is_set(const GSHSCENE_Cancel * cancel);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GSHSCENE_CANCEL_H */
