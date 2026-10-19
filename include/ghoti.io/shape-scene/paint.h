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
 * @file paint.h
 *
 * The paint of a fill (AD-7, AD-23): solid color now, and the place for the
 * gradients of story 12.
 *
 * A scene color is straight (not premultiplied) `double` components, never an
 * 8-bit value (AD-23).  The color description from `color` that gives the
 * components their meaning arrives with story 12; until then the one color
 * space is the target's, and `color_space` is 0.  The paint is plain data and
 * is validated by the record call that takes it.
 */

#ifndef GHOTI_IO_GSHSCENE_PAINT_H
#define GHOTI_IO_GSHSCENE_PAINT_H

#include <ghoti.io/shape-scene/macros.h>
#include <ghoti.io/shape-scene/types.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief What a paint is.  The values are pinned (they are hashed). */
typedef enum GSHSCENE_Paint_Kind {
  GSHSCENE_PAINT_SOLID = 0,    ///< One color.
  GSHSCENE_PAINT_GRADIENT = 1, ///< Reserved: a gradient resource (story 12); ::GSHSCENE_ERR_UNSUPPORTED until then.
  /** Reserved: the scene's foreground color (`currentColor`), a parameter set per version and never baked at lowering (AD-23). */
  GSHSCENE_PAINT_FOREGROUND = 2,
  /** Reserved: an entry of the font's color palette, by index, with an alpha (a COLRv1 solid paint; AD-23). */
  GSHSCENE_PAINT_PALETTE = 3,
  GSHSCENE_PAINT_KIND_COUNT    ///< Closes the enum.
} GSHSCENE_Paint_Kind;

/** @brief A paint. */
typedef struct GSHSCENE_Paint {
  GSHSCENE_Paint_Kind kind; ///< The kind.
  /** Solid: straight `r, g, b, a`.  Each finite; `a` in `[0, 1]`; the others may leave `[0, 1]` (a color outside the gamut). */
  double rgba[4];
  /** The color space of the components: 0, the target's.  Any other value is ::GSHSCENE_ERR_UNSUPPORTED until story 12. */
  uint32_t color_space;
  /** Gradient: the gradient resource.  NULL for a solid paint (and required to be). */
  const GSHSCENE_Resource * gradient;
} GSHSCENE_Paint;

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GSHSCENE_PAINT_H */
