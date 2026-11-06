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
 * @file clip_union.h
 *
 * The clip union: the region that is the union of several shapes, named by a clip push and by a retained group's clip like any
 * region.  Rules CU1 to CU17 (the story's numbering; this file states each one it enforces).
 *
 * ### What a union is (CU1)
 *
 * A **member** is `{resource, map, rule}`: a path or a glyph run from outlines, a 2D map and a fill rule.  The region of a member is
 * the set of points its rule selects from the member's closed device contours under the composed chain (the instance maps, the
 * item's map, the member's map, the target's), exactly as a single clip push's region is.  The region of a **clip union** is the
 * set-theoretic union of its members' regions (SVG 2 and CSS Masking: the clipping path of a `<clipPath>` is the union of its
 * children's geometry, each with its own `clip-rule` and `transform`; ISO 32000-1 text rendering modes 4 to 7 accumulate the glyph
 * shapes of a text object as a union that `ET` applies as one `W`).  A member's rule is its own.
 *
 * ### The coverage (CU2, CU3)
 *
 * Coverage is exact area of the region in the pixel, quantized once.  For a pixel `P` let `a_i(P)` be member `i`'s area in units of
 * 1/8192 of a pixel (`0 <= a_i <= 8192`, reduced by the member's rule as a fill's accumulated winding area is).  For members whose
 * device boxes have pairwise disjoint interiors the coverage byte is `min(255, floor(min(8192, sum a_i) / 32))`.  A sink that claims
 * ::GSHSCENE_CAP_CLIP_UNION_GENERAL draws any union: the members are partitioned into groups, the connected components of "the interiors
 * of the two members' device boxes meet"; the pixel's area is the sum over the groups, held at 8192, of the group's area (for a group of one
 * member its own `a_i`; for a group of several the floor of the exact area, in the same units, of the union of the members' regions, each
 * region the points where the member's own rule holds at the member's own winding), and the byte is `min(255, floor(area / 32))`.  That is
 * the fast tier's value wherever the fast tier applies, since every member is then a group of its own.  A region is the one the scan
 * converter deposits (its edges cut at the pixel boundaries, every cut point rounded to the lattice).
 * Quantization happens once, on the sum.  No sink lowers a union as an alpha mask and no sink forms its coverage from the members'
 * 8-bit coverages: two abutting halves give 255 at the shared pixel where a mask of the same shapes gives 192.
 * A member with no segment, a zero-area contour or a singular map has an empty region and contributes 0; a union all of whose
 * members contribute 0 is the empty region and clips everything away.
 *
 * ### The resource (CU6 to CU9)
 *
 * ::GSHSCENE_RESOURCE_CLIP_UNION (kind 14, string `"clip-union"`) is interned like any resource.  It is a **region, not a shape**: the
 * resource of a clip push and of a retained group's clip, and of nothing else (a fill, a stroke's centerline, a glyph run's member,
 * a retained shape or stroke node, a picture item and another union's member that names one is `ERR_INVALID`; unions do not
 * nest, because nested clipping is intersection and union is associative).
 *
 * ::gshscene_store_intern_clip_union decides in this order (`ERR_INVALID` unless stated): a NULL `members` or `out`; `count` 0 (an
 * empty region has a spelling already, a clip push of an empty path); for each member in the order given, a NULL resource or one
 * of another store, a kind other than path or glyph run, an invalid map, a rule outside the enumeration; then `ERR_UNSUPPORTED`
 * for a member map that is 3D and for a glyph run from a font (the per-glyph report entries of a run name `(chain, glyph index)`
 * and have no encoding for a member); then `ERR_LIMIT` for `count` over ::GSHSCENE_Limits.max_clip_members or the store's
 * limits; then `ERR_OOM`.  A failing call leaves the store as it was and takes no reference.  A zero-filled `map` (dimension 0) is
 * the identity.
 *
 * **Canonical form: an unordered set, one spelling (CU8).**  Two calls that name the same members in any order, with any
 * repetition, return the same handle.  The members are stored sorted ascending by `(u64 content hash of the resource, u64
 * gshgeom_map_hash of the canonical map, u8 rule)` and, only for members equal in all three but not identical (a 64-bit
 * collision), by the resource id and then the map's bits, with duplicates (the same resource handle, bit-equal maps, the same
 * rule) removed.  `-0` and `+0` differ, as in the hash.  The sort key is a function of content, so the order is the same on every
 * target and every run.
 *
 * **Hash, equality, dump, accounting (CU9).**  The content hash is, after `u32` the format (1) and `u32` the kind (14), `u64` the
 * canonical member count, then for each member `u64` the resource's content hash, `u64` the map's hash, `u8` the rule.  Equality
 * compares content member by member in canonical order (a hash alone never decides).  The dump is the resource header and the lines
 * `members N` and, per member, `m <index> res=<kind>:<hash> map=<hash> rule=<N>`.  Accounted bytes: ::GSHSCENE_BYTES_BASE plus
 * ::GSHSCENE_BYTES_PER_CLIP_MEMBER for each canonical member.  The count limit is ::GSHSCENE_Limits.max_clip_members (default
 * ::GSHSCENE_LIMITS_DEFAULT_MAX_CLIP_MEMBERS, ceiling ::GSHSCENE_CLIP_MEMBERS_CEILING; a value over the ceiling is `ERR_INVALID` at
 * the creation of the store); the count counted is the one given, before duplicates are removed.
 *
 * ### The clip push of a union (CU10, CU11)
 *
 * ::gshscene_list_clip_push takes a union resource of the list's store.  The item's `map` places the union as a whole (the
 * members' maps compose under it).  The item's `rule` is checked as any rule is and **stored as nonzero** (the members carry theirs).
 * A union with exactly one member is recorded as the single clip push of that member (`map` = the item's map composed with the
 * member's, `rule` = the member's), so a list never contains a one-member union item and a sink never sees one.  A retained
 * group's `clip` may name a union (its `clip_rule` is stored as nonzero) and emission records it through the same call.
 *
 * ### The one predicate (CU14, CU16, CU17)
 *
 * ::gshscene_clip_union_member_item makes the single clip item of one member; every consumer takes a union's members through it.
 * ::gshscene_clip_union_disjoint decides whether the members are provably disjoint: members `i` and `j` are disjoint iff the
 * **interiors** of their device boxes (::gshgeom_device_box, in 1/64 pixel, under the chain of the instance maps, the member
 * item's map and the target's) do not meet.  Touching boxes are disjoint: the regions lie in the closed boxes (Lemma CU15), so
 * they meet in a null set and the areas add exactly.  A member whose box cannot be formed is unprovable.  The test never says
 * *disjoint* for regions that meet in a set of positive area; it may say *overlap* for regions that do not (boxes are coarser than
 * regions), which is a refusal and never a wrong picture.  Pairs are examined `(i, j)`, `i < j`, `i` ascending and `j`
 * ascending, and the walk stops at the first pair that is not disjoint; an unprovable member is its pair `(i, i)`, examined when
 * `i` is reached (a pair whose second member is unprovable is not examined: that member's own turn comes later).  The number of
 * box comparisons made is at most `n (n - 1) / 2`.  The function allocates nothing and is a function of its arguments.
 */

#ifndef GHOTI_IO_GSHSCENE_CLIP_UNION_H
#define GHOTI_IO_GSHSCENE_CLIP_UNION_H

#include <ghoti.io/shape-scene/core.h>
#include <ghoti.io/shape-scene/list.h>
#include <ghoti.io/shape-scene/macros.h>
#include <ghoti.io/shape-scene/resource.h>
#include <ghoti.io/shape-scene/types.h>

#include <ghoti.io/shape-geometry/device.h>
#include <ghoti.io/shape-geometry/fill.h>
#include <ghoti.io/shape-geometry/map.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** One member of a clip union (CU1). */
typedef struct GSHSCENE_Clip_Member {
  const GSHSCENE_Resource * resource; ///< A path, or a glyph run from outlines, of the store.
  GSHGEOM_Map map;                    ///< The member's own map, 2D; a zero-filled map (dimension 0) is the identity.
  GSHGEOM_Fill_Rule rule;             ///< The member's own fill rule.
} GSHSCENE_Clip_Member;

/** The verdict of ::gshscene_clip_union_disjoint (CU17). */
typedef struct GSHSCENE_Union_Verdict {
  int disjoint;     ///< 1 when every pair is provably disjoint.
  uint64_t tests;   ///< Box comparisons made, at most `n (n - 1) / 2`.
  size_t first;     ///< When not disjoint: the canonical index `i` of the pair (or of the unprovable member).
  size_t second;    ///< The canonical index `j` of the pair; equal to `first` for an unprovable member.
} GSHSCENE_Union_Verdict;

/**
 * @brief Intern a clip union (CU6 to CU9).
 *
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID, ::GSHSCENE_ERR_UNSUPPORTED, ::GSHSCENE_ERR_LIMIT or ::GSHSCENE_ERR_OOM in the
 *         order the file comment states.
 */
GSHSCENE_API GSHSCENE_Result gshscene_store_intern_clip_union(GSHSCENE_Store * store, const GSHSCENE_Clip_Member * members,
    size_t count, const GSHSCENE_Resource ** out);

/** @brief The number of (canonical, distinct) members of a clip union; 0 for NULL or any other resource. */
GSHSCENE_API size_t gshscene_resource_clip_union_count(const GSHSCENE_Resource * resource);

/**
 * @brief Member @p index in canonical order, with the canonical map.
 *
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID for NULL, another kind of resource or an index past the count.
 */
GSHSCENE_API GSHSCENE_Result gshscene_resource_clip_union_member(const GSHSCENE_Resource * resource, size_t index,
    GSHSCENE_Clip_Member * out);

/**
 * @brief The single clip item of member @p index of a union item (CU14).
 *
 * @p item is a clip push whose resource is a clip union.  The result is a clip push of the member's resource with `map` the
 * composition of the item's map after the member's, the member's rule, and the item's layer and depth.  The planning pass, the
 * predicate and every sink take a union's members through this one function and then treat each as a plain clip item.
 *
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID for NULL, an item that is not a union clip push or an index past the count;
 *         ::GSHSCENE_ERR_LIMIT when the composition leaves the map bound.
 */
GSHSCENE_API GSHSCENE_Result gshscene_clip_union_member_item(const GSHSCENE_Item * item, size_t index, GSHSCENE_Item * out);

/**
 * @brief Whether the members of the union clip push @p item are provably disjoint under the chain (CU16, CU17).
 *
 * @param instance_maps The instance maps root to leaf (may be NULL when @p instance_count is 0).
 * @param target_map The target's map, applied last; NULL or zero-filled is the identity.
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID for NULL or an item that is not a union clip push.
 */
GSHSCENE_API GSHSCENE_Result gshscene_clip_union_disjoint(const GSHSCENE_Item * item, const GSHGEOM_Map * instance_maps,
    size_t instance_count, const GSHGEOM_Map * target_map, GSHSCENE_Union_Verdict * verdict);

/**
 * @brief The device box of member @p index (CU15, CU16) under the chain of the instance maps, the member item's map and the target's:
 *        the same box the predicate compares, for a sink that sizes its work from it (a glyph run's box is the hull of its glyphs').
 *
 * @return ::GSHSCENE_OK (with `empty` set for a member that draws nothing); ::GSHSCENE_ERR_INVALID for NULL, an item that is
 *         not a union clip push or an index past the count; ::GSHSCENE_ERR_LIMIT when the box cannot be formed (the member is
 *         unprovable).
 */
GSHSCENE_API GSHSCENE_Result gshscene_clip_union_member_box(const GSHSCENE_Item * item, size_t index,
    const GSHGEOM_Map * instance_maps, size_t instance_count, const GSHGEOM_Map * target_map, GSHGEOM_Device_Box * out);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GSHSCENE_CLIP_UNION_H */
