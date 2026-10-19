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
 * @file
 *
 * The one lock the store needs, and the atomics: internal, not installed.
 *
 * This is the same two primitives as cutil's mutex.h (a `pthread_mutex_t`; an
 * `SRWLOCK` on Windows), kept here so that the cross-target probes can build
 * these sources with nothing but a stand-in allocator header, as
 * shape-geometry's do.  Reference counts are C17 atomics.
 */

#ifndef GHOTI_IO_GSHSCENE_STORE_SYNC_H
#define GHOTI_IO_GSHSCENE_STORE_SYNC_H

#include <ghoti.io/shape-scene/macros.h>

#include <stdatomic.h>

#ifdef _WIN32
#include <windows.h>

typedef SRWLOCK GSHSCENE_Lock;

static inline int gshscene_lock_init(GSHSCENE_Lock * lock) {
  InitializeSRWLock(lock);
  return 0;
}
static inline void gshscene_lock_destroy(GSHSCENE_Lock * lock) {
  (void)lock;
}
static inline void gshscene_lock_acquire(GSHSCENE_Lock * lock) {
  AcquireSRWLockExclusive(lock);
}
static inline void gshscene_lock_release(GSHSCENE_Lock * lock) {
  ReleaseSRWLockExclusive(lock);
}
#else
#include <pthread.h>

typedef pthread_mutex_t GSHSCENE_Lock;

static inline int gshscene_lock_init(GSHSCENE_Lock * lock) {
  return pthread_mutex_init(lock, NULL);
}
static inline void gshscene_lock_destroy(GSHSCENE_Lock * lock) {
  pthread_mutex_destroy(lock);
}
static inline void gshscene_lock_acquire(GSHSCENE_Lock * lock) {
  pthread_mutex_lock(lock);
}
static inline void gshscene_lock_release(GSHSCENE_Lock * lock) {
  pthread_mutex_unlock(lock);
}
#endif

#endif /* GHOTI_IO_GSHSCENE_STORE_SYNC_H */
