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
 * The control of check-publish-types: the application's scene opens an edit and publishes it.
 * This must compile.
 */

#include <ghoti.io/shape-scene/scene.h>

GSHSCENE_Result types_control(GSHSCENE_Scene * scene);

GSHSCENE_Result types_control(GSHSCENE_Scene * scene) {
  GSHSCENE_Edit * edit = NULL;
  GSHSCENE_Result result = gshscene_scene_open_edit(scene, NULL, &edit);
  if (result != GSHSCENE_OK) {
    return result;
  }
  result = gshscene_scene_publish(scene, edit, NULL, NULL);
  gshscene_edit_destroy(edit);
  return result;
}
