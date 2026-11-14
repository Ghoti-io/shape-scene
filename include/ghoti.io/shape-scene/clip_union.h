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
 * A **member** is `{resource, map, rule}`: a path or a glyph run (from outlines or from a font), a 2D map and a fill rule.  The region of a member is
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
 * for a member map that is 3D; then `ERR_LIMIT` for `count` over ::GSHSCENE_Limits.max_clip_members or the store's
 * limits; then `ERR_OOM`.  A failing call leaves the store as it was and takes no reference.  A zero-filled `map` (dimension 0) is
 * the identity.
 *
 * **Text in a union (CU24).**  A member that is a glyph run from a font is a member like another: its region is the glyphs' outlines as a plain
 * clip push of the same run draws them (the route and the provider are the sink's: glyph.h), under the member's rule and map.  Three things
 * are fixed by it.  The run is spelled by its resource's content hash, which has the face and the glyph ids in it (a run from outlines hashes
 * its outlines), so a font member and the outlines of the same glyphs are two different members and two different unions.  The entries
 * the sink makes for the run (the run-level ones the planning pass writes, the per-glyph ones the render writes) name the part by the
 * report entry's `member` (report.h), their chain being the clip push's.  And the member's box (CU16) cannot be formed from the store (the
 * glyph boxes are the provider's): a font member is **unprovable**, ::gshscene_clip_union_disjoint says `0` and names it `(i, i)`, and
 * ::gshscene_clip_union_member_box is `ERR_LIMIT` for it, so a sink that draws only disjoint unions refuses a union with a font member,
 * `UNION_OVERLAP`, and one that draws any union forms the box itself from the device outlines it sweeps (the hull of the flattened points).
 * A font run that is an inner clip leaves the member's box as it is (CI3).  A sink without ::GSHSCENE_CAP_GLYPHS_FROM_FONT refuses a push
 * with a font part, `GLYPH_SOURCE`, naming the first as `member`; a part whose face is not bound is refused `FACE_UNBOUND` and named
 * the same way.
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
 * ### A member that is clipped (CI1 to CI6)
 *
 * A member may carry **inner clips**: regions its own is intersected with, `{resource, map, rule}` each, a path or a glyph run
 * (SVG's `clip-path` on a child of a `<clipPath>`: the child is drawn where its own geometry and its clip both hold, and the clipPath is
 * the union of its children so drawn).
 *
 * - **CI1 The region.**  The region of a clipped member is the intersection of the region of its own contours and the region of every
 *   inner clip, each under its own rule and its own map, the maps placing each in the union's space as a member's own does (the inner
 *   clip's map is not composed with the member's).  The region of the union is the union of its members' regions.  A member whose
 *   region is empty contributes 0.
 * - **CI2 The coverage is the same area.**  The coverage of a pixel is the exact area of the union's region, quantized once (CU2, CU3),
 *   where a clipped member's region is the intersection above, as the region the scan converter deposits (UC2).
 * - **CI3 The box.**  A clipped member's box is the meet of the box of its own contours and of its inner clips', in the member
 *   box's terms (CU16): the region lies in each box, so in the meet.  An inner clip whose box cannot be formed is left out of the
 *   meet (the member's box is then larger, which is sound); a member whose own box cannot be formed is unprovable.
 * - **CI4 Never the fast tier.**  The fast tier sums the members' own areas, which for a clipped member is the area of its own
 *   contours and not of its region, so a union with a clipped member is not provably disjoint: ::gshscene_clip_union_disjoint says
 *   `0` and names the first clipped member as an unprovable one, `(i, i)`.  A group of one clipped member has the floor of the exact
 *   area of its region (CI2), not the fold of its own winding area; a group of several has the floor of the exact area of the union.
 * - **CI5 The capability.**  A sink that draws clipped members claims ::GSHSCENE_CAP_CLIP_UNION_INNER (which needs
 *   ::GSHSCENE_CAP_CLIP_UNION_GENERAL); without it the planning pass refuses a union clip push with a clipped member,
 *   `UNION_INNER`, naming the first as `i << 16 | i`, and the clip's block is skipped (S24).
 * - **CI6 Interning.**  The inner clips are checked as members are (`ERR_INVALID`, then `ERR_UNSUPPORTED` for a 3D map), counted with the members against ::GSHSCENE_Limits.max_clip_members (the count of members and of inner clips
 *   as given, `ERR_LIMIT`), and canonical: a member's inner clips are an unordered set, sorted by the member key
 *   `(content hash of the resource, hash of the canonical map, rule)` and, on a tie, by the resource id and the map's bits, with
 *   duplicates removed (the intersection of a region with itself is the region).  Members are then ordered by their own key
 *   and, equal in that, by their inner clips' keys in order (the count first), and two members with the same own key and the same
 *   inner clips are one.  A member with no inner clips hashes, dumps and is accounted exactly as before; a clipped member
 *   hashes `u64` the resource's hash, `u64` the map's hash, `u8` the rule with its high bit set, `u64` the inner count and, for
 *   each inner clip, `u64` the resource's hash, `u64` the map's hash, `u8` the rule; the dump gives its `m` line `inner=K` and
 *   one line `i <member> <index> res=<kind>:<hash> map=<hash> rule=<N>` for each inner clip; accounted bytes add
 *   ::GSHSCENE_BYTES_PER_CLIP_INNER for each.  A union of one member with inner clips stays a union item (CU10's unwrapping is for
 *   a member that has none).
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

/** One inner clip of a member (CI1): a region the member's own is intersected with. */
typedef struct GSHSCENE_Clip_Inner {
  const GSHSCENE_Resource * resource; ///< A path or a glyph run (CU24) of the store.
  GSHGEOM_Map map;                    ///< Its own map in the union's space, 2D; a zero-filled map (dimension 0) is the identity.
  GSHGEOM_Fill_Rule rule;             ///< Its own fill rule.
} GSHSCENE_Clip_Inner;

/** One member of a clip union (CU1). */
typedef struct GSHSCENE_Clip_Member {
  const GSHSCENE_Resource * resource; ///< A path or a glyph run (CU24) of the store.
  GSHGEOM_Map map;                    ///< The member's own map, 2D; a zero-filled map (dimension 0) is the identity.
  GSHGEOM_Fill_Rule rule;             ///< The member's own fill rule.
  size_t inner_count;                 ///< How many inner clips (CI1); 0 for a plain member.  Zero-filled members are plain.
  /** The inner clips, canonical order (CI6) on a read back and in any order to intern; NULL only with @p inner_count 0.  On a read back they are the resource's, valid while a handle is held. */
  const GSHSCENE_Clip_Inner * inner;
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
 * @brief The single clip item of inner clip @p inner of member @p member of a union item (CI1): as ::gshscene_clip_union_member_item makes a
 *        member's, with the inner clip's resource, map and rule.
 *
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID for NULL, an item that is not a union clip push, or an index past a count;
 *         ::GSHSCENE_ERR_LIMIT when the composition leaves the map bound.
 */
GSHSCENE_API GSHSCENE_Result gshscene_clip_union_inner_item(const GSHSCENE_Item * item, size_t member, size_t inner, GSHSCENE_Item * out);

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
 * @brief The device box of member @p index (CU15, CU16, CI3: the meet with its inner clips' boxes) under the chain of the instance maps, the member item's map and the target's:
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
