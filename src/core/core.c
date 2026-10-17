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
 * The version this build reports.
 */

#include <ghoti.io/shape-scene/macros.h>

#include <ghoti.io/shape-scene/core.h>
#include <ghoti.io/shape-scene/libver.h>

const char * gshscene_version_string(void) {
  return GSHSCENE_VERSION_STRING;
}

unsigned gshscene_version_number(void) {
  return GSHSCENE_VERSION_NUMBER;
}
