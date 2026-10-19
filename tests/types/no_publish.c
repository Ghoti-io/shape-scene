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
 * A front-end's view: it includes edit.h only, and is given an edit.  It tries to publish.
 * This must not compile: the name is not declared by anything it can include.
 */

#include <ghoti.io/shape-scene/edit.h>

GSHSCENE_Result types_no_publish(GSHSCENE_Edit * edit);

GSHSCENE_Result types_no_publish(GSHSCENE_Edit * edit) {
  return gshscene_scene_publish(NULL, edit, NULL, NULL);
}
