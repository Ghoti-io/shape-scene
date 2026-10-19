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
 * The version this build reports, the result strings, the default limits and
 * the default allocator.
 */

#include <ghoti.io/shape-scene/macros.h>

#include <ghoti.io/shape-scene/allocator.h>
#include <ghoti.io/shape-scene/core.h>
#include <ghoti.io/shape-scene/libver.h>

const char * gshscene_version_string(void) {
  return GSHSCENE_VERSION_STRING;
}

unsigned gshscene_version_number(void) {
  return GSHSCENE_VERSION_NUMBER;
}

const char * gshscene_result_string(GSHSCENE_Result result) {
  switch (result) {
    case GSHSCENE_OK:
      return "ok";
    case GSHSCENE_ERR_IO:
      return "I/O error";
    case GSHSCENE_ERR_FORMAT:
      return "format error";
    case GSHSCENE_ERR_UNSUPPORTED:
      return "unsupported";
    case GSHSCENE_ERR_LIMIT:
      return "limit exceeded";
    case GSHSCENE_ERR_CORRUPT:
      return "corrupt";
    case GSHSCENE_ERR_OOM:
      return "out of memory";
    case GSHSCENE_ERR_INVALID:
      return "invalid argument";
    case GSHSCENE_ERR_INTERNAL:
      return "internal error";
    case GSHSCENE_ERR_REFUSED:
      return "refused";
    case GSHSCENE_RESULT_COUNT:
      break;
  }
  return "unknown result";
}

void gshscene_limits_default(GSHSCENE_Limits * limits) {
  if (!limits) {
    return;
  }
  limits->max_resources = GSHSCENE_LIMITS_DEFAULT_MAX_RESOURCES;
  limits->max_resource_elements = GSHSCENE_LIMITS_DEFAULT_MAX_RESOURCE_ELEMENTS;
  limits->max_list_items = GSHSCENE_LIMITS_DEFAULT_MAX_LIST_ITEMS;
  limits->max_list_bytes = GSHSCENE_LIMITS_DEFAULT_MAX_LIST_BYTES;
  limits->max_depth = GSHSCENE_LIMITS_DEFAULT_MAX_DEPTH;
  limits->max_picture_depth = GSHSCENE_LIMITS_DEFAULT_MAX_PICTURE_DEPTH;
  limits->max_expansion = GSHSCENE_LIMITS_DEFAULT_MAX_EXPANSION;
  limits->max_bytes = GSHSCENE_LIMITS_DEFAULT_MAX_BYTES;
  limits->max_nodes = GSHSCENE_LIMITS_DEFAULT_MAX_NODES;
  limits->max_node_depth = GSHSCENE_LIMITS_DEFAULT_MAX_NODE_DEPTH;
  for (size_t i = 0; i < sizeof limits->_reserved; i++) {
    limits->_reserved[i] = 0;
  }
}

const GSHSCENE_Allocator * gshscene_allocator_default(void) {
  return gshgeom_allocator_default();
}
