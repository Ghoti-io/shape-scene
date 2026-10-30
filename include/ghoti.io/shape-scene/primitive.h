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
 * @file primitive.h
 *
 * Named primitives: a path resource that says what the front-end meant.  Rules PR1 to PR13.
 *
 * A circle that reaches a sink as four arcs has forgotten it is a circle: a document writer that should emit `<circle>` or a PDF
 * `re`, a GPU sink with an analytic coverage for an ellipse or a rounded rectangle, a toolpath sink, each would recover the fact
 * from the geometry, which is pattern matching. A front-end that knows says so, by building the path with
 * ::gshscene_store_intern_primitive, and the resource then carries a **primitive tag** with the parameters it was built from.
 *
 * ### The tag (PR1)
 *
 * The resource kind stays ::GSHSCENE_RESOURCE_PATH. The tag is `{kind, flip_x, flip_y, parameters}`, set by
 * ::gshscene_store_intern_primitive and by nothing else: a resource is immutable and held by handle, so a tag cannot outlive an
 * edit, and there is no function that attaches one to an existing path or infers one from geometry (PR8). ::gshscene_store_intern_path
 * of exactly the same geometry gives an untagged handle, a different resource. ::gshscene_resource_primitive reads the tag back
 * (`NONE` for an untagged path). The parameters are in the path's local space: an item's map applies to a tagged path as to any
 * path, and a writer reads the tag and the item's map together (a circle under a similarity is a circle).
 *
 * ### What is built (PR2 to PR5, PR13)
 *
 * The path is built by shape-geometry's `shapes.h`, whose rules are the geometry's definition: lines and quarter-turn arcs, SVG 2's
 * start point and the positive direction (clockwise on a y-down screen), the Canvas `rect` sign cases as two flips for the rect and the
 * ellipse, radii that overlap refused on an exact sum, every coordinate the correctly rounded value of its expression. The waist
 * takes resolved radii and does not clamp or scale them: SVG clamps each radius to half its side and CSS and Canvas scale all corners
 * by one factor, and the front-end applies its own rule first.
 *
 * - **RECT** `{x, y, w, h}`, `w, h >= 0`: five elements.
 * - **ROUNDED_RECT** `{x, y, w, h, r[4]}`, the radii in the order top-left, top-right, bottom-right, bottom-left. A corner with a zero
 *   component is square and canonical `{0, 0}`; if every corner is square the primitive is a `RECT` and is tagged as one (equal
 *   geometry is equal content). Six to ten elements: the four lines are always present.
 * - **ELLIPSE** `{cx, cy, rx, ry}`, both radii positive: six elements. A circle is an ellipse with equal radii.
 *
 * `flip_x` and `flip_y` (each 0 or 1) are Canvas's four sign cases (see shapes.h). The direction is derived (negative iff exactly
 * one flip is set) and not stored. SVG uses no flip. **A flipped rounded rectangle is not defined yet** and is
 * ::GSHSCENE_ERR_UNSUPPORTED (the engines disagree on what Canvas `roundRect` does with a negative size; see shapes.h).
 *
 * ### Validity and the order of refusals (PR2)
 *
 * ::GSHSCENE_ERR_INVALID for a NULL, a kind that is `NONE` or unknown, a flip that is not 0 or 1, a non-finite number, a negative
 * width, height or radius component, an ellipse radius that is not positive, and radii that overlap on a side (decided exactly), in that
 * order; then ::GSHSCENE_ERR_UNSUPPORTED for a flipped rounded rectangle; then ::GSHSCENE_ERR_LIMIT for a coordinate the build would derive
 * past ::GSHGEOM_COORD_LIMIT, the store's element limit or its byte limit; then ::GSHSCENE_ERR_OOM.
 *
 * ### Content (PR7, PR12)
 *
 * The tag is content. The path resource's hash, for a tagged path only, follows the path's own hash with `u8` the kind, `u8` the flips
 * (bit 0 `flip_x`, bit 1 `flip_y`) and the canonical parameters as `f64` in field order (`RECT`: `x, y, w, h`; `ROUNDED_RECT`: those and the
 * eight radius components `rx, ry` in `TL TR BR BL` order; `ELLIPSE`: `cx, cy, rx, ry`). An untagged path hashes as it always did.
 * Equality compares the tag, the flips and the parameters bit for bit besides the path, so equal content is one handle, a parameter that
 * differs in one bit, a kind or a flip is another handle, and the same geometry untagged is a third. The dump gains, after the path's
 * text and for a tagged path only, a line `primitive kind=<name> flip_x=<0|1> flip_y=<0|1> <parameters>`, the parameters as 16
 * hexadecimal digits each, in the order the hash takes them.
 *
 * ### The tag never changes coverage (PR9)
 *
 * A sink draws a path resource by its path and ignores the tag; the tagged path and its untagged copy render bit for bit alike.
 * A sink that reads the tag (a document writer, an analytic route) does so for its own reasons and is gated against the path.
 */

#ifndef GHOTI_IO_GSHSCENE_PRIMITIVE_H
#define GHOTI_IO_GSHSCENE_PRIMITIVE_H

#include <ghoti.io/shape-geometry/shapes.h>
#include <ghoti.io/shape-scene/core.h>
#include <ghoti.io/shape-scene/macros.h>
#include <ghoti.io/shape-scene/resource.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The kind of a primitive tag. The values are pinned (they are hashed). */
typedef enum GSHSCENE_Primitive_Kind {
  GSHSCENE_PRIMITIVE_NONE = 0,         ///< No tag: a path.
  GSHSCENE_PRIMITIVE_RECT = 1,         ///< A rectangle.
  GSHSCENE_PRIMITIVE_ROUNDED_RECT = 2, ///< A rectangle with elliptical corners.
  GSHSCENE_PRIMITIVE_ELLIPSE = 3,      ///< An ellipse; a circle is one with equal radii.
  GSHSCENE_PRIMITIVE_KIND_COUNT        ///< Closes the enum.
} GSHSCENE_Primitive_Kind;

/**
 * @brief A primitive: its kind, its flips and the parameters it was built from.
 *
 * Only the fields of its kind are read; the others are ignored on input and zero on output.
 */
typedef struct GSHSCENE_Primitive {
  GSHSCENE_Primitive_Kind kind; ///< The kind.
  int flip_x;                   ///< 0 or 1: mirrored in the box's vertical axis (Canvas width negative).
  int flip_y;                   ///< 0 or 1: mirrored in the box's horizontal axis (Canvas height negative).
  double x, y, w, h;            ///< RECT and ROUNDED_RECT: the box's top-left corner and its size.
  GSHGEOM_Corner r[4];          ///< ROUNDED_RECT: top-left, top-right, bottom-right, bottom-left.
  double cx, cy, rx, ry;        ///< ELLIPSE: the centre and the radii.
} GSHSCENE_Primitive;

/**
 * @brief The name of a primitive kind: "none", "rect", "rounded-rect" or "ellipse"; "?" for any other value.
 *
 * @return A static string, never NULL.
 */
GSHSCENE_API const char * gshscene_primitive_kind_string(GSHSCENE_Primitive_Kind kind);

/**
 * @brief Intern the path a primitive names, tagged (PR1, PR2, PR12).
 *
 * The path is built from the parameters (shapes.h) and the tag is canonicalized (a rounded rectangle whose corners are all square is a
 * `RECT`, a square corner is `{0, 0}`). Equal content is one handle. This is the only function that sets a tag.
 *
 * @param store The store.
 * @param prim The primitive; not kept.
 * @param out The handle (a reference the caller releases); written only on success.
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID, ::GSHSCENE_ERR_UNSUPPORTED, ::GSHSCENE_ERR_LIMIT or ::GSHSCENE_ERR_OOM as above.
 */
GSHSCENE_API GSHSCENE_Result gshscene_store_intern_primitive(
    GSHSCENE_Store * store, const GSHSCENE_Primitive * prim, const GSHSCENE_Resource ** out);

/**
 * @brief The tag of a path resource (PR1).
 *
 * @param out The canonical primitive; kind `NONE` and everything else zero for an untagged path. Written only on success.
 * @return ::GSHSCENE_OK, or ::GSHSCENE_ERR_INVALID for a NULL or a resource that is not a path.
 */
GSHSCENE_API GSHSCENE_Result gshscene_resource_primitive(const GSHSCENE_Resource * resource, GSHSCENE_Primitive * out);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GSHSCENE_PRIMITIVE_H */
