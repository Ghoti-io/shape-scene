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
 * The cancel token (cancel.h, S7): a cutil atomic.
 */

#include <ghoti.io/shape-scene/macros.h>

#include <ghoti.io/shape-scene/cancel.h>

void gshscene_cancel_init(GSHSCENE_Cancel * cancel) {
  if (cancel) {
    gcu_atomic_int_init(&cancel->flag, 0);
  }
}

void gshscene_cancel_set(GSHSCENE_Cancel * cancel) {
  if (cancel) {
    gcu_atomic_int_store(&cancel->flag, 1);
  }
}

int gshscene_cancel_is_set(const GSHSCENE_Cancel * cancel) {
  return cancel ? gcu_atomic_int_load(&cancel->flag) != 0 : 0;
}
