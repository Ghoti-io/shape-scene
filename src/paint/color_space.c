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
 * Color spaces and colors (paint.h): what is valid, the zero value as the
 * target's space, and the mapping from a `color` description.  Nothing here
 * evaluates a transfer function (AD-22: that is the sink's, with `math`).
 */

#include <ghoti.io/shape-scene/macros.h>

#include "../internal.h"

#include <ghoti.io/color/color.h>

/** The gamma range a plain exponent may have. */
#define GAMMA_MIN 0.1
#define GAMMA_MAX 10.0

int gshscene_color_space_is_target(const GSHSCENE_Color_Space * space) {
  return space && space->primaries == GCOL_PRIMARIES_UNKNOWN && space->transfer == GCOL_TRANSFER_UNKNOWN &&
         gshgeom_f64_bits(space->gamma) == 0;
}

GSHSCENE_Result gshscene_color_space_check(const GSHSCENE_Color_Space * space) {
  if (!space) {
    return GSHSCENE_ERR_INVALID;
  }
  if ((unsigned)space->primaries >= (unsigned)GCOL_PRIMARIES_COUNT ||
      (unsigned)space->transfer >= (unsigned)GCOL_TRANSFER_COUNT) {
    return GSHSCENE_ERR_INVALID;
  }
  if (gshscene_color_space_is_target(space)) {
    return GSHSCENE_OK;
  }
  /* Structure first: a space is wholly given or not at all, and one spelling per value. */
  if (space->primaries == GCOL_PRIMARIES_UNKNOWN || space->transfer == GCOL_TRANSFER_UNKNOWN) {
    return GSHSCENE_ERR_INVALID;
  }
  if (space->transfer == GCOL_TRANSFER_GAMMA) {
    if (!gshscene_finite(space->gamma) || !(space->gamma >= GAMMA_MIN && space->gamma <= GAMMA_MAX)) {
      return GSHSCENE_ERR_INVALID;
    }
  } else if (gshgeom_f64_bits(space->gamma) != 0) {
    return GSHSCENE_ERR_INVALID;
  }
  switch (space->primaries) {
    case GCOL_PRIMARIES_SRGB:
    case GCOL_PRIMARIES_ADOBE_RGB:
    case GCOL_PRIMARIES_DISPLAY_P3:
    case GCOL_PRIMARIES_BT2020:
      break;
    default: /* ProPhoto: D50 */
      return GSHSCENE_ERR_UNSUPPORTED;
  }
  switch (space->transfer) {
    case GCOL_TRANSFER_LINEAR:
    case GCOL_TRANSFER_SRGB:
    case GCOL_TRANSFER_GAMMA:
      return GSHSCENE_OK;
    default: /* parametric, BT.1886, PQ, HLG */
      return GSHSCENE_ERR_UNSUPPORTED;
  }
}

GSHSCENE_Result gshscene_color_space_from_info(const GCOL_Color_Info * info, GSHSCENE_Color_Space * out) {
  if (!info || !out) {
    return GSHSCENE_ERR_INVALID;
  }
  if (!info->primaries_stated || info->transfer == GCOL_TRANSFER_UNKNOWN) {
    return GSHSCENE_ERR_INVALID;
  }
  if (info->icc_bytes != NULL || info->icc_size != 0 || info->icc_linked_path != NULL) {
    return GSHSCENE_ERR_UNSUPPORTED;
  }
  GSHSCENE_Color_Space space;
  space.primaries = gcol_gamut_identify(&info->gamut, GCOL_GAMUT_TOLERANCE_DEFAULT);
  space.transfer = info->transfer;
  space.gamma = info->transfer == GCOL_TRANSFER_GAMMA ? info->gamma_value : 0.0;
  if (space.primaries == GCOL_PRIMARIES_UNKNOWN) {
    return GSHSCENE_ERR_UNSUPPORTED;
  }
  GSHSCENE_Result result = gshscene_color_space_check(&space);
  if (result == GSHSCENE_OK) {
    *out = space;
  }
  return result;
}

void gshscene_color_init(GSHSCENE_Color * color) {
  if (!color) {
    return;
  }
  color->rgba[0] = 0.0;
  color->rgba[1] = 0.0;
  color->rgba[2] = 0.0;
  color->rgba[3] = 1.0;
  color->space.primaries = GCOL_PRIMARIES_UNKNOWN;
  color->space.transfer = GCOL_TRANSFER_UNKNOWN;
  color->space.gamma = 0.0;
}

int gshscene_color_is_default(const GSHSCENE_Color * color) {
  GSHSCENE_Color def;
  gshscene_color_init(&def);
  for (int i = 0; i < 4; i++) {
    if (gshgeom_f64_bits(color->rgba[i]) != gshgeom_f64_bits(def.rgba[i])) {
      return 0;
    }
  }
  return gshscene_color_space_is_target(&color->space);
}

GSHSCENE_Result gshscene_color_check_valid(const GSHSCENE_Color * color) {
  if (!color) {
    return GSHSCENE_ERR_INVALID;
  }
  for (int i = 0; i < 4; i++) {
    if (!gshscene_finite(color->rgba[i])) {
      return GSHSCENE_ERR_INVALID;
    }
  }
  if (!(color->rgba[3] >= 0.0 && color->rgba[3] <= 1.0)) {
    return GSHSCENE_ERR_INVALID;
  }
  return gshscene_color_space_check(&color->space);
}

GSHSCENE_Result gshscene_color_check(const GSHSCENE_Color * color) {
  GSHSCENE_Result result = gshscene_color_check_valid(color);
  if (result != GSHSCENE_OK) {
    return result;
  }
  /* N1: the bound, after every validity check. */
  return gshscene_color_bounded(color->rgba) ? GSHSCENE_OK : GSHSCENE_ERR_LIMIT;
}
