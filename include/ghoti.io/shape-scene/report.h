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
 * @file report.h
 *
 * The report of a render (sink.h, S5): what a sink did not do as stated, keyed
 * by list entry, and a summary.  One report shape for every sink (AD-11).
 *
 * ### Ownership
 *
 * The report is the caller's: made with ::gshscene_report_create from the
 * caller's allocator, named by a render request, and destroyed by the caller.
 * It survives a cancelled render and an `ERR_OOM` (a diagnostic allocated inside
 * the render is what is missing when it is most wanted), and a game reuses one
 * across frames: a render resets it, and a reset keeps the storage, so a steady
 * frame allocates nothing for it.  A report is used by one render at a time; a
 * second render naming a report in use is ::GSHSCENE_ERR_INVALID.  Read it after
 * the render completes.
 *
 * ### Entries
 *
 * Each entry has a **class** (::GSHSCENE_Entry_Class), a **reason**
 * (::GSHSCENE_Reason) and a **chain**: the index of the item in the closed list
 * the render was given, then, when the item is inside an instanced picture, the
 * index of the item in each picture's list, outermost first.  A chain of length
 * 0 is a render-level entry (the conflation flag, a refused workspace).  A
 * sink never sees a version (AD-11); ::gshscene_report_resolve turns a chain into
 * a node ID and an instance path with the version whose list was rendered.
 *
 * | Class | What it says |
 * | --- | --- |
 * | `REFUSED` | the item, and its block if it is a push or an instance, was skipped |
 * | `APPROXIMATED` | the item was drawn, differently from the definition (a clip without antialiasing, a group blended in another space) |
 * | `RESOLVED_AMBIGUITY` | a depth tie of the list (N11): `second` is the first item of the unit painted after, in the same list, `depth` the depth they share |
 * | `HINT_STATUS` | a run from a font not hinted as it asked (glyph.h, D7): one entry per run for a run-level cause, `second` 0; one per glyph for a glyph-level cause, `second` the glyph's index in the run |
 * | `NOTICE` | the item was drawn as defined, and the definition draws nothing there: a gradient fill whose chain is not invertible or whose gradient is degenerate (paint.h, G10, G11); a stroke whose chain is not invertible, whose width is 0 or whose gradient paint is degenerate (stroke.h, SK8, SK9).  It never fails a render, strict or not |
 *
 * A `REFUSED` entry of one glyph of a run (glyph.h) likewise has the glyph's
 * index in `second`; the rest of the run was drawn.  A `REFUSED` entry of a
 * stroke for its style (`STROKE_STYLE`, stroke.h, SK11) has in `second` the
 * `GSHSCENE_STROKE_NATIVE_*` bits of the features the sink lacks.
 *
 * **Order** (normative): entries are sorted by chain (element by element, a
 * prefix before its extensions, so render-level entries come first), then
 * class, then reason, then `second`; entries equal in all four keep the order
 * they were added in.  The order does not depend on the sink.
 *
 * **The cap.** At most `max_report_entries` (::GSHSCENE_Limits) entries are
 * stored.  Past it the report keeps counting (`counted`, `by_class`), sets
 * `overflowed`, stores nothing more, and a render that would have returned
 * ::GSHSCENE_OK returns ::GSHSCENE_ERR_LIMIT instead.  Nothing is dropped
 * silently and nothing is truncated without saying so.
 *
 * ### The summary
 *
 * ::GSHSCENE_Report_Summary: the render's result; the units (an item of the
 * list's expansion: an instance is one unit and so is each item of its
 * picture), total, processed and done (the units whose effect is in the
 * target); the entries counted and per class; whether the cap was passed; the
 * bytes the sink holds across frames (`static_bytes`, S9) and its peak scratch
 * for this render (`frame_bytes`); the workspace a `CALLER_MEMORY` render needed
 * (`workspace_needed`; 0 for a workspace too small, S9); the most work between two cancel checks (`work_max`,
 * S7); and the failure, if the render did not return ::GSHSCENE_OK: its result,
 * the chain of the unit it stopped at (empty when none) and a static reason.
 *
 * ### The hash of a report (format 1)
 *
 * FNV-1a 64 in the spelling of shape-geometry's hash.h: `u32` 1 (this format;
 * 1 until the suite's first release), `u32` `0x54525052`, `u32` the result, `u64` units
 * total, processed and done, `u64` counted, `u8` overflowed, `u64` each of
 * `by_class[1]` to `by_class[5]`,
 * `u64` static bytes and work max, `u32` the failure's result, `u64` its chain
 * length and `u64` each index; `u64` the entries stored, then for each entry in
 * order `u8` class, `u32` reason, `u64` chain length, `u64` each index, `u64`
 * second, `f64` depth.  Not hashed: the failure's reason text, and the frame
 * bytes and the workspace needed, which measure the host (the size of a pointer
 * is in them), not the render; the dump shows them.
 *
 * ### The dump
 *
 * `gshscene.report 1 result=N units=T/P/D entries=E counted=C overflowed=O static=N frame=N workspace=N work=N hash=<16 hex digits>`,
 * then, if the result is not 0, `failure result=N chain=<chain> reason=<text>`,
 * then one line per entry `E class=N reason=N chain=<chain> second=N depth=<16 hex digits>`;
 * a chain is its indices in decimal joined by `.`, or `-` when empty; numbers
 * are decimal and the depth is the 16 hexadecimal digits of its bit pattern.
 */

#ifndef GHOTI_IO_GSHSCENE_REPORT_H
#define GHOTI_IO_GSHSCENE_REPORT_H

#include <ghoti.io/shape-scene/allocator.h>
#include <ghoti.io/shape-scene/core.h>
#include <ghoti.io/shape-scene/macros.h>
#include <ghoti.io/shape-scene/types.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief What an entry says.  The values are pinned (they are hashed). */
typedef enum GSHSCENE_Entry_Class {
  GSHSCENE_ENTRY_REFUSED = 1,            ///< Skipped: not drawn.
  GSHSCENE_ENTRY_APPROXIMATED = 2,       ///< Drawn, not as the definition says.
  GSHSCENE_ENTRY_RESOLVED_AMBIGUITY = 3, ///< A depth tie the list resolved (N11).
  GSHSCENE_ENTRY_HINT_STATUS = 4,        ///< A run from a font not hinted as it asked (glyph.h).
  GSHSCENE_ENTRY_NOTICE = 5,             ///< Drawn as defined, and the definition draws nothing there (paint.h, G10, G11; stroke.h, SK8, SK9).
  GSHSCENE_ENTRY_CLASS_COUNT             ///< Closes the enum.
} GSHSCENE_Entry_Class;

/** @brief Why.  The values are pinned (they are hashed). */
typedef enum GSHSCENE_Reason {
  GSHSCENE_REASON_ITEM_KIND = 1,      ///< Refused: the sink does not draw this kind of item for this target.
  GSHSCENE_REASON_COMPOSITE = 2,      ///< Refused: the sink does not composite the group's operator.
  GSHSCENE_REASON_GLYPH_SOURCE = 3,   ///< Refused: glyphs named by a font, and the sink has no glyph path provider.
  GSHSCENE_REASON_BLEND_SPACE = 4,    ///< Approximated: the group's stated blend space is not the one the sink blends in.
  GSHSCENE_REASON_CLIP_ALIASED = 5,   ///< Approximated: a clip without antialiasing (AD-12).
  GSHSCENE_REASON_RESOURCE_LIMIT = 6, ///< Refused while drawing: one of the sink's own resource limits.
  GSHSCENE_REASON_SEAMLESS = 7,       ///< Refused, render-level: the conflation flag (AD-29, S8).
  GSHSCENE_REASON_CALLER_MEMORY = 8,  ///< Refused, render-level: the sink cannot take its memory from a workspace (S9).
  GSHSCENE_REASON_TIE_LAYER = 9,      ///< Ambiguity: the tie was broken by layer.
  GSHSCENE_REASON_TIE_PATH = 10,      ///< Ambiguity: the tie was broken by instance path.
  GSHSCENE_REASON_TIE_DECLARED = 11,  ///< Ambiguity: the tie was broken by declared order.
  GSHSCENE_REASON_HINT_ROUTE = 12,    ///< Hint status, per run: the map sends the run to the general route, unhinted.
  GSHSCENE_REASON_HINT_SIZE = 13,     ///< Hint status, per run: the device ppem is not an integer the fast tier takes.
  GSHSCENE_REASON_HINT_NO_PROGRAM = 14, ///< Hint status: the face (per run) or the glyph at this size (per glyph) has no program.
  GSHSCENE_REASON_HINT_PROGRAM_FAILED = 15, ///< Per glyph: the program failed (hint status), or failed under strict (refused).
  GSHSCENE_REASON_HINT_WITH_ERRORS = 16, ///< Hint status, per glyph: a tolerant program erred.
  GSHSCENE_REASON_HINT_STRICT_ROUTE = 17, ///< Refused, per run: a strict-hinted run whose map or size sends it to the general route (D6).
  GSHSCENE_REASON_GLYPH_UNAVAILABLE = 18, ///< Refused, per glyph: the provider cannot give the glyph.
  GSHSCENE_REASON_FACE_UNBOUND = 19,  ///< Refused, per run: the run's face has no glyph path provider.
  GSHSCENE_REASON_PALETTE_ENTRY = 20, ///< Refused, per fill: its palette paint, or a stop of its gradient, names an entry the palette in force does not have (N14, G3).
  GSHSCENE_REASON_GRADIENT = 21,      ///< Refused, per fill: the sink does not draw this kind of gradient (G13).
  GSHSCENE_REASON_INTERPOLATION_SPACE = 22, ///< Approximated, per fill: the gradient's stated interpolation space is not the one the sink interpolates in (G13).
  GSHSCENE_REASON_MAP_NOT_INVERTIBLE = 23,  ///< Notice, per fill: a map of the gradient fill's chain is not invertible, so it draws nothing (G11).
  GSHSCENE_REASON_GRADIENT_DEGENERATE = 24, ///< Notice, per fill: the gradient is degenerate, so it draws nothing (G10).
  GSHSCENE_REASON_GROUP_ISOLATION = 25, ///< Refused, per group: a non-isolated group, which the sink does not composite (T4, S19).
  GSHSCENE_REASON_GROUP_KNOCKOUT = 26,  ///< Refused, per group: a knockout group, which the sink does not composite (T3, S19).
  GSHSCENE_REASON_SOFT_MASK = 27,       ///< Refused, per group: a group with a soft mask, which the sink does not apply (T7, S19).
  GSHSCENE_REASON_STROKE_STYLE = 28,    ///< Refused, per stroke: a feature of its style the sink neither emits nor expands; `second` names the missing ones (stroke.h, SK11).
  GSHSCENE_REASON_STROKE_ZERO_WIDTH = 29, ///< Notice, per stroke: its width is 0, so it draws nothing (stroke.h, SK8).
  GSHSCENE_REASON_STROKE_CENTERLINE = 30, ///< Refused, per stroke: its centerline is a glyph run and the sink does not stroke runs (sink.h, S20).
  GSHSCENE_REASON_PLACEHOLDER = 31,   ///< Refused or approximated, per placeholder: a front end could not express a construct; `second` is `source << 16 | code` (sink.h, PH3, PH4).
  GSHSCENE_REASON_COUNT               ///< Closes the enum.
} GSHSCENE_Reason;

/**
 * @name Placeholder sources and codes (PH1, PH4)
 *
 * A placeholder (list.h) names a **source**, the front end that recorded it, and a **code**, that front end's own
 * reason; the entry the planning pass writes for it has `reason` ::GSHSCENE_REASON_PLACEHOLDER and `second` =
 * `source << 16 | code`.  The registry is additive: a front end takes the next source, and its codes are its own,
 * registered here with their names so that a reader of a report can print them without the front end.  Zero is never
 * a source or a code.  The values are pinned (they are hashed).
 * @{
 */
#define GSHSCENE_PLACEHOLDER_SOURCE_COLR 1u        ///< An OpenType `COLR` table's paint graph (font).
#define GSHSCENE_PLACEHOLDER_COLR_UNBOUNDED 1u     ///< `COLR`: the graph has no finite extent a bounding box could be drawn for (an unbounded paint under a format-2 or format-6 composite).
#define GSHSCENE_PLACEHOLDER_COLR_NO_RECORD 2u     ///< `COLR`: the glyph has no base glyph or paint record the lowering can use.
#define GSHSCENE_PLACEHOLDER_COLR_WALK_LIMIT 3u    ///< `COLR`: the graph is deeper or larger than the lowering's walk bound allows (a cycle among them).
/** @} */

/**
 * @brief Name a placeholder's source and code: "colr:unbounded", "colr:no-record", "colr:walk-limit", or "unknown" for
 *   a pair that is not registered.
 */
GSHSCENE_API const char * gshscene_placeholder_string(unsigned source, unsigned code);

/** @brief Name a class ("refused", "approximated", "resolved-ambiguity", "hint-status", "notice"), or "unknown class". */
GSHSCENE_API const char * gshscene_entry_class_string(GSHSCENE_Entry_Class entry_class);

/** @brief Name a reason ("item-kind", "composite", ...), or "unknown reason". */
GSHSCENE_API const char * gshscene_reason_string(GSHSCENE_Reason reason);

/** @brief One entry, as read back. */
typedef struct GSHSCENE_Report_Entry {
  GSHSCENE_Entry_Class entry_class; ///< The class.
  GSHSCENE_Reason reason;           ///< The reason.
  const size_t * chain;             ///< The key (see the file comment); valid until the report is reset or destroyed.  NULL when empty.
  size_t chain_count;               ///< Its length; 0 for a render-level entry.
  size_t second;                    ///< Ambiguity: the first item of the unit painted after, in the list of the chain's last index.  A glyph's entry: its index in the run.  A stroke refused for its style: the missing features' bits (SK11).  Else 0.
  double depth;                     ///< Ambiguity: the depth the two units share.  Else 0.
} GSHSCENE_Report_Entry;

/** @brief Why a render did not return ::GSHSCENE_OK (D3). */
typedef struct GSHSCENE_Failure {
  GSHSCENE_Result result;  ///< The render's result; ::GSHSCENE_OK when nothing failed.
  const size_t * chain;    ///< The unit it stopped at, if one; valid until the report is reset or destroyed.  NULL when empty.
  size_t chain_count;      ///< Its length; 0 when the failure is not about one unit.
  const char * reason;     ///< A static string, never NULL, never allocated.
} GSHSCENE_Failure;

/** @brief The summary of a render (see the file comment). */
typedef struct GSHSCENE_Report_Summary {
  GSHSCENE_Result result;    ///< What the render returned.
  size_t units_total;        ///< The units of the list's expansion.
  size_t units_processed;    ///< The units the sink went through, in paint order.
  size_t units_done;         ///< The units whose effect is in the target (a prefix of paint order; an open group's are not).
  size_t counted;            ///< Entries added, including those past the cap.
  size_t by_class[GSHSCENE_ENTRY_CLASS_COUNT]; ///< Entries added per class (index 0 unused), including those past the cap.
  int overflowed;            ///< Nonzero if an entry was not stored because of the cap.
  uint64_t static_bytes;     ///< What the sink holds across frames, by resource (S9).
  uint64_t frame_bytes;      ///< The sink's peak scratch for this render (S9).
  uint64_t workspace_needed; ///< The workspace a `CALLER_MEMORY` render needed (S9); 0 when not asked, and when the workspace was too small (a render does not allocate to measure it: ::gshscene_sink_workspace_bytes gives it).
  uint64_t work_max;         ///< The most work between two cancel checks, in the sink's measure (S7).
  GSHSCENE_Failure failure;  ///< Why it failed, if it did.
} GSHSCENE_Report_Summary;

/**
 * @brief Make an empty report.
 *
 * @param alloc The allocator, or NULL for the default; copied.
 * @param limits The limits (only `max_report_entries` is read; 0 is its default), or NULL for the defaults.
 * @param out The report; written only on success.  Free it with ::gshscene_report_destroy.
 * @return ::GSHSCENE_OK, ::GSHSCENE_ERR_INVALID (NULL @p out) or ::GSHSCENE_ERR_OOM.
 */
GSHSCENE_API GSHSCENE_Result gshscene_report_create(
    const GSHSCENE_Allocator * alloc, const GSHSCENE_Limits * limits, GSHSCENE_Report ** out);

/** @brief Free the report.  NULL is ignored.  Not while a render that names it is in flight. */
GSHSCENE_API void gshscene_report_destroy(GSHSCENE_Report * report);

/**
 * @brief Empty the report, keeping its storage.  A render does this itself when it begins.
 *
 * @return ::GSHSCENE_OK or ::GSHSCENE_ERR_INVALID (NULL, or in use by a render).
 */
GSHSCENE_API GSHSCENE_Result gshscene_report_reset(GSHSCENE_Report * report);

/** @brief The entries stored; 0 for NULL. */
GSHSCENE_API size_t gshscene_report_count(const GSHSCENE_Report * report);

/**
 * @brief Read entry @p index, in the report's order.
 *
 * @param out The entry; written only on success.
 * @return ::GSHSCENE_OK or ::GSHSCENE_ERR_INVALID (NULL, out of range).
 */
GSHSCENE_API GSHSCENE_Result gshscene_report_entry(
    const GSHSCENE_Report * report, size_t index, GSHSCENE_Report_Entry * out);

/** @brief The summary; ::GSHSCENE_ERR_INVALID for NULL.  Written only on success. */
GSHSCENE_API GSHSCENE_Result gshscene_report_summary(const GSHSCENE_Report * report, GSHSCENE_Report_Summary * out);

/**
 * @brief The node and instance path of entry @p index, for the list a version emitted.
 *
 * The chain's first index is an item of ::gshscene_version_list's list; each further index is an item of the
 * picture of the instance the previous index names.  The instance path is the IDs of those instance nodes, then
 * the node of the last item (version.h).
 *
 * @param version The version whose list was rendered.
 * @param out_node The node; written only on success.
 * @param out_path Room for the path: at least the entry's `chain_count` IDs; written only on success.
 * @param capacity Its length.
 * @param out_count The path's length (the entry's `chain_count`); written only on success.
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID (NULL, out of range, a render-level entry, a capacity below the
 *   chain, or a chain that is not an item of this version's list and its pictures).
 */
GSHSCENE_API GSHSCENE_Result gshscene_report_resolve(const GSHSCENE_Report * report,
    const GSHSCENE_Version * version, size_t index, uint64_t * out_node, uint64_t * out_path, size_t capacity,
    size_t * out_count);

/** @brief The hash of the report, as the file comment spells it; the offset basis for NULL. */
GSHSCENE_API uint64_t gshscene_report_hash(const GSHSCENE_Report * report);

/**
 * @brief Write the report as text (see the file comment).
 *
 * @return ::GSHSCENE_OK, ::GSHSCENE_ERR_INVALID (NULL) or ::GSHSCENE_ERR_IO.
 */
GSHSCENE_API GSHSCENE_Result gshscene_report_dump(const GSHSCENE_Report * report, FILE * out);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GSHSCENE_REPORT_H */
