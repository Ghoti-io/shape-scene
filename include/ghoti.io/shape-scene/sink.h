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
 * @file sink.h
 *
 * The sink contract (AD-11, story 13a): how a list and its resources are
 * rendered by a backend the caller does not need to know.  It is written for
 * every backend the family will have: the CPU reference (shape-cpu), GPU sinks
 * (Vulkan, WebGPU, WebGL), and writers whose target is a document (SVG and PDF
 * export, a plotter's or a machine's program).  The rules are numbered S1 to
 * S20; tests, plants and the conformance suite cite them, and
 * documentation/design.md has the same text.
 *
 * ### The rules (normative)
 *
 * **S1 Request.**  A render is a ::GSHSCENE_Render_Request.  The all-zero request
 * with a closed list, a target and a report is a whole-frame, best-effort,
 * uncancellable render, and ::gshscene_sink_render completes it in one call on
 * every sink that can block (S13).  Unknown flag bits and nonzero reserved bytes
 * are ::GSHSCENE_ERR_INVALID.
 *
 * **S2 Sink.**  A ::GSHSCENE_Sink is made by ::gshscene_sink_create from an ops
 * table and an implementation pointer; the caller holds the sink and does not
 * know which it is.  **Capabilities are per sink and target**
 * (::gshscene_sink_caps): what a sink draws depends on the target's kind,
 * format and device, and the contract layer asks before every render.
 *
 * **S3 Job.**  ::GSHSCENE_Job is the caller's storage, so a frame allocates
 * nothing for it.  ::gshscene_sink_begin starts a render; ::gshscene_job_poll
 * observes it and never blocks; ::gshscene_sink_wait blocks until it completes
 * (S13).  The sink ends a job with ::gshscene_job_complete, which seals the
 * report, releases the surface and calls the request's `on_complete` exactly
 * once, before the job reads as done.  From `begin` until completion the target's
 * surface belongs to the sink: a second `begin` on the same surface, on the same
 * sink, is ::GSHSCENE_ERR_INVALID (the busy guard), and at most `max_jobs`
 * (::GSHSCENE_Limits) renders are in flight on one sink (::GSHSCENE_ERR_LIMIT).  A
 * sink with no executor completes inside `begin`.
 *
 * **S4 Target.**  ::GSHSCENE_Target describes the surface; the signature never
 * names a raster.  Its **kind** is `RASTER` (a `GIMG_Raster`), `STREAM` (a
 * ::GSHSCENE_Writer: a document a writer sink emits), or a reserved GPU kind
 * (::GSHSCENE_ERR_UNSUPPORTED until the GPU sinks).  Its **map** takes list space to
 * *target units*: pixels for a pixel kind, and for a stream the unit the target
 * states (millimetres, points, inches or pixels; a stream must state one).
 * **Extent, AA filter and damage are pixel-kind fields** and must be zero for a
 * kind with no pixels.  **Orientation and encoding belong to the sink**: the map
 * is always model to target with y down (AD-6), a sink whose native framebuffer
 * is y-up flips internally, and no caller ever flips; `blend_space` zero takes
 * the surface's description (an undescribed raster, and a stream, are sRGB
 * primaries with the sRGB transfer), and `encoding` says whether stored values are encoded by the
 * sink or by the hardware (an sRGB-format texture written with linear values).
 * A sink composes maps root to leaf, the target map last (AD-24).  A render with
 * a damage rectangle draws exactly what a whole render draws inside it and
 * touches nothing outside.  So **whether an item is refused for one of the
 * sink's own limits (S6) does not depend on the damage**: a limit is met over
 * what the whole render would draw of the item, wherever the damage lies, and a
 * damage-limited render refuses (or, under strict, stops at) the same items as
 * the whole render.
 *
 * **S5 Report.**  One report shape for every sink; see report.h.
 *
 * **S6 Refusal and the planning pass.**  A sink never drops an effect silently:
 * an item is drawn as stated, drawn approximately with an `APPROXIMATED` entry,
 * or skipped with a `REFUSED` entry.  Before it calls the sink, the contract
 * layer walks the closed list and its pictures against the caps and writes every
 * refusal and approximation that is a property of the list and the caps
 * (::gshscene_caps_disposition is the one predicate, and a sink decides each item
 * with it too), the entry of each run from a font's route (::gshscene_glyph_route:
 * a run-level `HINT_STATUS`, or a refusal, D6), the `NOTICE` of each gradient
 * fill that draws nothing (S17) and of each stroke that draws nothing (S20), and
 * every depth tie of the list as `RESOLVED_AMBIGUITY`.  A
 * refused push or instance skips its whole block.  Best effort returns
 * ::GSHSCENE_OK.  With ::GSHSCENE_RENDER_STRICT a planned refusal is
 * ::GSHSCENE_ERR_REFUSED before the sink is called, so the target is untouched; a
 * refusal that arises while drawing (one of the sink's own resource limits) stops
 * the render at that unit with the finished prefix in the target, and is
 * ::GSHSCENE_ERR_REFUSED too.  `APPROXIMATED` and `NOTICE` never fail a render.
 *
 * **Placeholders (list.h, PH1; the rules PH3 to PH6).**  A placeholder is a unit no sink draws and every sink reports,
 * so the planning pass decides it from the list alone, before and independent of every caps test: `REFUSED` with
 * reason `PLACEHOLDER` when it is not marked approximated, `APPROXIMATED` with the same reason when it is (PH3).  No
 * `item_kinds` bit is needed or consulted.  The entry is the item's chain with `second` = `source << 16 | code`
 * (report.h, PH4); a placeholder inside a block the pass refuses is skipped with the block, as every item is, and the
 * block's entry stands.  A `REFUSED` placeholder under ::GSHSCENE_RENDER_STRICT is ::GSHSCENE_ERR_REFUSED before the
 * sink is called, the target untouched; best effort returns ::GSHSCENE_OK and draws everything else; an `APPROXIMATED`
 * one never fails a render (PH5).  A sink's `render` treats the item as drawn nothing and writes no entry of its own;
 * `static_bytes`, the work count and the cancel units count it as nothing (PH6).  Damage does not change whether it is
 * reported: a placeholder outside the damage rectangle is reported exactly as one inside it is, as a refusal does not
 * depend on the damage (S4).  A writer may leave a comment or a tagged empty element for it, its choice, stated in its
 * own documents; nothing requires it.
 *
 * **S7 Cancel.**  The token (cancel.h) is the caller's and is set from any
 * thread.  **The unit and the bound are the sink's, declared in its caps**:
 * ::GSHSCENE_CANCEL_BETWEEN_UNITS checks before the first unit and after each
 * unit's `progress` call, so a token set before or during the progress call of
 * unit k is seen before unit k + 1 begins; ::GSHSCENE_CANCEL_BEFORE_SUBMIT
 * checks once, before its work is submitted, and completes after that.
 * `cancel_work_bound` is the most work, in the measure the caps name, done
 * between two checks; the job counts it (`work_max`).  A cancelled render
 * returns ::GSHSCENE_ERR_CANCELLED with the finished prefix of paint order in the
 * target and no half-built group.
 *
 * **S8 Conflation.**  ::GSHSCENE_TARGET_SEAMLESS is refused with
 * ::GSHSCENE_ERR_REFUSED before the sink is called, with or without strict, with
 * one render-level entry (AD-29).
 *
 * **S9 Memory and static resources.**  With ::GSHSCENE_RENDER_CALLER_MEMORY the
 * sink allocates nothing during the render, sizing it included, and takes its
 * scratch from the workspace; ::gshscene_sink_workspace_bytes gives the exact
 * need.  A smaller workspace (or none) is ::GSHSCENE_ERR_LIMIT with the target
 * untouched and **no allocation at all**, by the sink or the report: the summary
 * states no need (`workspace_needed` 0), because a render does not allocate to
 * measure one, and the failure's reason names the query, which gives it (and may
 * allocate: it is not a render).  A sink without
 * the capability (device memory) refuses the flag, ::GSHSCENE_ERR_REFUSED with a
 * render-level entry.  ::gshscene_sink_prepare lets an engine upload or cache a
 * list's static resources before frames, keyed by resource id, released by
 * ::gshscene_sink_evict or by destroying the sink; the summary's `static_bytes` is
 * what the sink holds across frames and `frame_bytes` its peak scratch.
 *
 * **S10 Lifetime with work in flight.**  The caller keeps the list, its
 * resources, the target and its surface, the report, the workspace and the
 * cancel token alive and unmodified **until the job completes**, not merely until
 * `begin` returns.  A sink keeps no reference to a list or a resource past
 * completion, except a resource it was given by `prepare`, held until `evict` or
 * destroy.
 *
 * **S11 Arithmetic.**  The CPU reference's; shape-cpu's header (story 13b).
 * Whatever its precision, every sink applies N1's **saturation** (paint.h): each
 * decode, matrix result and encode of a conversion, every un-premultiply (a
 * group's conversion, G4's flag, a store to a straight target, evaluated without
 * dividing past the bound) and every `apply` is clamped to `[-2^64, 2^64]`
 * (::GSHSCENE_COLOR_SATURATION), at those points and no others, so every sink and
 * writer gives a value past `2^64` the same meaning; the conformance suite's
 * saturation cases and the composite vectors check it.  A writer that emits a
 * format whose consumer converts (SVG, PDF) states where that consumer departs.
 *
 * **S12 Determinism.**  A sink's output depends on the list, the target and the
 * request only: not on the thread, the allocator, the workspace's address or its
 * size beyond the ::GSHSCENE_ERR_LIMIT boundary.
 *
 * **S13 Completion without blocking.**  Waiting is optional.  A sink whose caps
 * lack ::GSHSCENE_CAP_CAN_BLOCK (a browser main thread, WebGL, WebGPU) returns
 * ::GSHSCENE_ERR_UNSUPPORTED from ::gshscene_sink_wait and ::gshscene_sink_render
 * before anything is begun, whether or not a job happens to be complete; its
 * completion is observed by ::gshscene_job_poll and by `on_complete`.
 *
 * **S14 Thread affinity.**  A sink whose caps say ::GSHSCENE_CAP_THREAD_AFFINE
 * (a GL context) takes `begin`, `poll` of its jobs, `wait`, `readback`, `prepare`,
 * `evict` and `destroy` only from the thread that created it; the contract layer
 * refuses a call from another thread with ::GSHSCENE_ERR_INVALID.  Without it
 * those calls may come from any thread, and renders on distinct surfaces may run
 * at once.  The cancel token, closed lists, resources and versions are usable from
 * any thread in every case.
 *
 * **S15 Readback.**  ::gshscene_sink_readback copies a completed render's pixels
 * of the extent into a caller raster of the extent's size and the target's
 * format, so a test can hash a GPU's output.  Optional
 * (::GSHSCENE_CAP_READBACK).
 *
 * **S16 Geometry reaches a sink in model space.**  The contract hands a sink the
 * list and its resources: model-space `double` geometry with curves intact.
 * Nothing here composes, flattens or rounds; each sink composes its maps
 * (`gshgeom_map_compose_chain`) and flattens with its own rule, or not at all (a
 * writer emits curves, a GPU sink may rasterize them).  A resource's id is the key
 * a writer dedupes by and a cache is keyed by.  The planning pass composes maps to
 * decide (a run from a font's route, glyph.h), and never hands composed geometry
 * to a sink.
 *
 * **S17 Gradients (paint.h, G1 to G13).**  A gradient reaches a sink by handle,
 * as every resource does, and a sink reads it with `gshscene_resource_gradient`.
 * The caps' `gradient_kinds` name the kinds it draws; a fill of another kind is
 * refused by the planning pass (`GRADIENT`), so a sink that has none, such as
 * one written before gradients, never draws one wrong: under strict the render
 * fails before the sink is called.  Without ::GSHSCENE_CAP_GRADIENT_SPACES a
 * stated interpolation space other than the target's is approximated
 * (`INTERPOLATION_SPACE`).  A gradient with a palette stop the palette in force
 * lacks is refused (`PALETTE_ENTRY`).  The planning pass writes a `NOTICE`
 * (`MAP_NOT_INVERTIBLE` before `GRADIENT_DEGENERATE`, one entry a fill) for each
 * gradient fill ::gshscene_paint_notice says draws nothing, and a sink skips
 * that fill's effect by the same predicate.  A sink converts the stops with
 * `gshscene_gradient_resolve`, against the request's list's foreground and
 * palette and the target's resolved space, **once per gradient per render** (by
 * resource id), and bakes what it needs from them itself; a writer emits a
 * gradient once per id and refers to it after.
 *
 * **S18 Blend modes (list.h, B1 to B14; color-paint story P3).**  A blend mode is a
 * group's composite operator (13 to 27).  A sink claims each one it composites
 * in its caps' `composites` (bit `1 << op`); a group whose mode it lacks is
 * refused by the planning pass (`COMPOSITE`), so it is never drawn as another
 * mode, and a sink claims an operator only once its compositor knows it.  A
 * writer spells a mode by its name (::gshscene_composite_string).  A blending
 * group in a stated space other than the target's, without
 * ::GSHSCENE_CAP_BLEND_SPACES, is approximated (`BLEND_SPACE`) as any group is.
 *
 * **S19 PDF's group fields (list.h, T1 to T7; color-paint story P3).**  A group
 * that is non-isolated, knockout or names a soft mask is composited only by a
 * sink whose caps say so (::GSHSCENE_CAP_GROUP_NON_ISOLATED,
 * ::GSHSCENE_CAP_GROUP_KNOCKOUT, ::GSHSCENE_CAP_GROUP_SOFT_MASK); otherwise the
 * planning pass refuses it (`GROUP_ISOLATION`, `GROUP_KNOCKOUT`, `SOFT_MASK`),
 * so it is never drawn as an isolated, non-knockout or unmasked group.  A sink that composites masks names in
 * `mask_native` the features of mask.h (MK9) it has; a masked group whose mask needs one it lacks
 * (::gshscene_mask_required, less `SPACES` when every space the mask names is the enclosing one) is refused after the
 * rest, `MASK_FEATURE`, the missing bits in the entry's `second`.  The planning pass enters the mask's picture
 * (::GSHSCENE_Expansion with masks entered) and plans its items with the chain `[... , i, j]`: `i` the group's push,
 * `j` the item in the picture; the expansion's count and the progress units are those of the list alone.  A
 * refused push skips its block, as every refused push does (S6).  The fields'
 * defaults are N5 to N9 exactly (T6), so a sink without the caps draws every
 * group that does not set them.
 *
 * **S20 Strokes (stroke.h, SK1 to SK14; strokes story S1).**  A stroke reaches a
 * sink by its centerline and style handles in model space (S16): nothing in the
 * contract expands, flattens or offsets it.  A sink that draws strokes has bit
 * `1 << 7` in `item_kinds` (else every stroke is refused, `ITEM_KIND`), names in
 * `stroke_native` each feature it emits as a stroke, and says with
 * ::GSHSCENE_CAP_STROKE_EXPAND that it can expand a stroke exactly
 * (`shape-geometry`'s expansion) and draw it as a fill.  A stroke whose required
 * features (::gshscene_stroke_required) are all native is drawn as a stroke; else,
 * with `STROKE_EXPAND`, it is expanded and drawn with no entry (it is drawn as
 * defined); else it is refused by the planning pass, `STROKE_STYLE`, the missing
 * features in the entry's `second`.  A stroke whose centerline is a glyph run is
 * drawn only by a sink whose caps say ::GSHSCENE_CAP_STROKE_RUNS (a run's glyphs
 * are one region, SG13, and the sink must unite them); else the planning pass refuses it,
 * `STROKE_CENTERLINE`, after the refusals every item can have (a run from an unbound
 * face is `FACE_UNBOUND` and one from a font needs `GLYPHS_FROM_FONT`, in that order)
 * and before the style's, so that strict fails before the target is touched and the
 * stroke is never drawn as nothing; a stroke that draws nothing (its notice) is not
 * refused for its face, its provider or its centerline (ST5).  Such a sink strokes the
 * path ::gshscene_glyph_run_centerline makes (ST1), which a pen follows or a writer
 * emits as a stroked path.  A sink reads the style with
 * ::gshscene_resource_stroke_style, decides each stroke with
 * ::gshscene_caps_disposition and skips by ::gshscene_stroke_notice (the planning
 * pass wrote the `NOTICE`: width 0, a chain that is not invertible, a degenerate
 * gradient paint), and never routes a run it strokes (SK7).  A writer emits a
 * native stroke as a stroke, deduped by the style's id, and expands only a stroke
 * whose required features it lacks; a toolpath sink follows the centerline and
 * refuses what its caps lack.  A device width on a stream is in CSS pixels, 1/96
 * inch in the target's unit (::gshscene_stroke_pixel_size).
 *
 * ### For sink implementations
 *
 * A sink library fills a ::GSHSCENE_Sink_Ops and calls ::gshscene_sink_create
 * from its own constructor.  Its `begin` reads the job's request
 * (::gshscene_job_request; the target there is the job's copy), walks the list
 * (::GSHSCENE_Expansion), decides each item with ::gshscene_caps_disposition, given the maps of the enclosing
 * instances and the target's (and a
 * run from a font with ::gshscene_glyph_route, skipping a run it refuses: the plan
 * reported it), adds the entries only it can find (::gshscene_job_report, and
 * ::gshscene_job_report_glyph for one glyph of a run), reports progress and
 * checks cancel at its declared points (::gshscene_job_unit,
 * ::gshscene_job_checkpoint), and ends the job with ::gshscene_job_complete,
 * inside `begin` or later.
 */

#ifndef GHOTI_IO_GSHSCENE_SINK_H
#define GHOTI_IO_GSHSCENE_SINK_H

#include <ghoti.io/shape-scene/allocator.h>
#include <ghoti.io/shape-scene/cancel.h>
#include <ghoti.io/shape-scene/core.h>
#include <ghoti.io/shape-scene/diff.h>
#include <ghoti.io/shape-scene/glyph.h>
#include <ghoti.io/shape-scene/list.h>
#include <ghoti.io/shape-scene/macros.h>
#include <ghoti.io/shape-scene/paint.h>
#include <ghoti.io/shape-scene/report.h>
#include <ghoti.io/shape-scene/stroke.h>
#include <ghoti.io/shape-scene/types.h>

#include <ghoti.io/cutil/atomic.h>
#include <ghoti.io/image/raster.h>
#include <ghoti.io/shape-geometry/map.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- the target (S4) ------------------------------------------------------------- */

/** @brief What a target is.  The values are pinned. */
typedef enum GSHSCENE_Target_Kind {
  GSHSCENE_TARGET_RASTER = 1,      ///< `surface` is a `GIMG_Raster *`; pixels.
  GSHSCENE_TARGET_STREAM = 2,      ///< `surface` is a ::GSHSCENE_Writer *: a document (SVG, PDF, G-code); no pixels.
  GSHSCENE_TARGET_GPU_TEXTURE = 3, ///< Reserved for the GPU sinks: ::GSHSCENE_ERR_UNSUPPORTED.
  GSHSCENE_TARGET_GPU_SURFACE = 4, ///< Reserved for the GPU sinks: ::GSHSCENE_ERR_UNSUPPORTED.
  GSHSCENE_TARGET_KIND_COUNT       ///< Closes the enum.
} GSHSCENE_Target_Kind;

/** @brief Nonzero if a target of @p kind has pixels (raster and the GPU kinds): extent, AA filter and damage apply. */
GSHSCENE_API int gshscene_target_kind_has_pixels(GSHSCENE_Target_Kind kind);

/** @brief The unit of a target's space.  The values are pinned. */
typedef enum GSHSCENE_Unit {
  GSHSCENE_UNIT_DEFAULT = 0,    ///< The kind's own: pixels for a pixel kind; not allowed for a stream.
  GSHSCENE_UNIT_PIXEL = 1,      ///< Pixels.
  GSHSCENE_UNIT_MILLIMETER = 2, ///< Millimetres.
  GSHSCENE_UNIT_POINT = 3,      ///< Points (1/72 inch).
  GSHSCENE_UNIT_INCH = 4,       ///< Inches.
  GSHSCENE_UNIT_COUNT           ///< Closes the enum.
} GSHSCENE_Unit;

/** @brief Who encodes the stored values (S4).  The values are pinned. */
typedef enum GSHSCENE_Encoding {
  GSHSCENE_ENCODING_SOFTWARE = 0,      ///< The sink writes values encoded in the blend space's transfer.
  GSHSCENE_ENCODING_HARDWARE_SRGB = 1, ///< The storage encodes sRGB itself: the sink writes linear values.
  GSHSCENE_ENCODING_COUNT              ///< Closes the enum.
} GSHSCENE_Encoding;

/** @brief The default antialiasing filter of a pixel target.  Only the box filter exists. */
typedef enum GSHSCENE_AA_Filter {
  GSHSCENE_AA_BOX = 0, ///< Exact area (AD-12).  Any other value is ::GSHSCENE_ERR_UNSUPPORTED.
  GSHSCENE_AA_FILTER_COUNT
} GSHSCENE_AA_Filter;

/** The conflation flag (AD-29, S8): seam-free abutting shapes.  Reserved: refused until it is solved. */
#define GSHSCENE_TARGET_SEAMLESS (UINT32_C(1) << 0)

/** Every target flag defined. */
#define GSHSCENE_TARGET_FLAGS_ALL GSHSCENE_TARGET_SEAMLESS

/**
 * @brief Where a stream target's bytes go.
 *
 * `write` takes every byte or fails: it returns ::GSHSCENE_OK, or another result (::GSHSCENE_ERR_IO for a failed
 * write), which ends the render with that result.  It is called from the thread that runs the render.
 */
typedef struct GSHSCENE_Writer {
  GSHSCENE_Result (*write)(void * ctx, const void * bytes, size_t count); ///< Not NULL.
  void * ctx;                                                            ///< Passed to it.
} GSHSCENE_Writer;

/** @brief A described target (S4).  Every field's zero is its default, except `kind` and `surface`. */
typedef struct GSHSCENE_Target {
  uint32_t kind;                    ///< A ::GSHSCENE_Target_Kind.
  uint32_t flags;                   ///< ::GSHSCENE_TARGET_SEAMLESS, or 0.
  void * surface;                   ///< RASTER: a `GIMG_Raster *`; STREAM: a `GSHSCENE_Writer *`.  Not NULL.
  /** List space to target units, y down.  A zero-filled map (`dim` 0) is the 2D identity. */
  GSHGEOM_Map map;
  uint32_t unit;                    ///< A ::GSHSCENE_Unit: 0 or PIXEL for a pixel kind; stated for a stream.
  uint32_t encoding;                ///< A ::GSHSCENE_Encoding.
  uint32_t aa;                      ///< A ::GSHSCENE_AA_Filter.  Pixel kinds only (0 elsewhere).
  /** The space the root group blends in (N6); zero takes the surface's description (an undescribed raster is sRGB). */
  GSHSCENE_Color_Space blend_space;
  /** Pixel kinds: the region drawn, inside the surface; all zero is the whole surface.  Else zero. */
  GSHSCENE_Rect extent;
  /** Pixel kinds: the damage (AD-9, diff.h), clipped to the extent; NULL is the whole extent.  Else NULL. */
  const GSHSCENE_Rect * damage;
  uint8_t _reserved[16];            ///< Zero.
} GSHSCENE_Target;

/* --- capabilities (S2) ----------------------------------------------------------- */

/** @name Capability flags
 * @{ */
#define GSHSCENE_CAP_CAN_BLOCK (UINT32_C(1) << 0)        ///< `wait` and the one-call render work (S13).
#define GSHSCENE_CAP_THREAD_AFFINE (UINT32_C(1) << 1)    ///< Calls only from the creating thread (S14).
#define GSHSCENE_CAP_READBACK (UINT32_C(1) << 2)         ///< ::gshscene_sink_readback works (S15).
#define GSHSCENE_CAP_CALLER_MEMORY (UINT32_C(1) << 3)    ///< ::GSHSCENE_RENDER_CALLER_MEMORY is honored (S9).
#define GSHSCENE_CAP_GLYPHS_FROM_FONT (UINT32_C(1) << 4) ///< Glyph runs from a font are drawn through their face's provider (glyph.h).
#define GSHSCENE_CAP_CLIP_AA (UINT32_C(1) << 5)          ///< Clips are antialiased (else each is approximated, AD-12).
#define GSHSCENE_CAP_BLEND_SPACES (UINT32_C(1) << 6)     ///< Groups blend in their stated space (else approximated, AD-23).
#define GSHSCENE_CAP_PREPARE (UINT32_C(1) << 7)          ///< `prepare` holds resources across frames (S9).
#define GSHSCENE_CAP_GRADIENT_SPACES (UINT32_C(1) << 8)  ///< Gradients interpolate in their stated space (else approximated, S17).
#define GSHSCENE_CAP_GROUP_NON_ISOLATED (UINT32_C(1) << 9) ///< Non-isolated groups are composited as T4 and T5 say (else refused, S19).
#define GSHSCENE_CAP_GROUP_KNOCKOUT (UINT32_C(1) << 10)  ///< Knockout groups are composited as T3 says (else refused, S19).
#define GSHSCENE_CAP_GROUP_SOFT_MASK (UINT32_C(1) << 11) ///< A group's soft mask is applied (T7; else refused, S19).
/** A stroke whose required features are not all native is expanded exactly and drawn as a fill (S20, SK11); else refused. */
#define GSHSCENE_CAP_STROKE_EXPAND (UINT32_C(1) << 12)
/** A stroke whose centerline is a glyph run is drawn (S20, SK7); without it the planning pass refuses one, `STROKE_CENTERLINE`. */
#define GSHSCENE_CAP_STROKE_RUNS (UINT32_C(1) << 13)
/** Image paints are filtered in their stated filter space (image_paint.h, IP5); without it a stated space is approximated, `IMAGE_FILTER_SPACE` (S21). */
#define GSHSCENE_CAP_IMAGE_FILTER_SPACES (UINT32_C(1) << 14)
#define GSHSCENE_CAP_FLAGS_ALL ((UINT32_C(1) << 15) - 1) ///< Every flag defined.
/** @} */

/**
 * @name Mask features (MK9)
 *
 * The features of a mask (mask.h) a sink composites, which `gshscene_mask_required` reports a mask as needing.  The values are
 * pinned.
 * @{
 */
#define GSHSCENE_MASK_NATIVE_ALPHA (UINT32_C(1) << 0)               ///< An alpha mask.
#define GSHSCENE_MASK_NATIVE_LUMINANCE (UINT32_C(1) << 1)           ///< A luminance mask.
#define GSHSCENE_MASK_NATIVE_BACKDROP (UINT32_C(1) << 2)            ///< A backdrop other than black.
#define GSHSCENE_MASK_NATIVE_TRANSFER_EXPONENTIAL (UINT32_C(1) << 3) ///< An exponential transfer.
#define GSHSCENE_MASK_NATIVE_TRANSFER_TABLE (UINT32_C(1) << 4)      ///< A table transfer.
#define GSHSCENE_MASK_NATIVE_TRANSFER_STITCHING (UINT32_C(1) << 5)  ///< A stitching transfer.
#define GSHSCENE_MASK_NATIVE_COEFF_W3C (UINT32_C(1) << 6)           ///< The `W3C` coefficients.
#define GSHSCENE_MASK_NATIVE_COEFF_REC709 (UINT32_C(1) << 7)        ///< The `REC709` coefficients.
#define GSHSCENE_MASK_NATIVE_COEFF_LUM (UINT32_C(1) << 8)           ///< The `LUM` coefficients.
#define GSHSCENE_MASK_NATIVE_COEFF_SPACE_Y (UINT32_C(1) << 9)       ///< The `SPACE_Y` coefficients.
#define GSHSCENE_MASK_NATIVE_SPACES (UINT32_C(1) << 10)             ///< A luminance, render or backdrop space other than the enclosing one (the planning pass drops it when they are the enclosing space).
#define GSHSCENE_MASK_NATIVE_ACTS_ON_SHAPE (UINT32_C(1) << 11)      ///< A mask acting on the shape rather than the opacity.
#define GSHSCENE_MASK_NATIVE_ALL ((UINT32_C(1) << 12) - 1)          ///< Every feature defined.
/** @} */

/** @brief Where a sink checks its cancel token (S7).  The values are pinned. */
typedef enum GSHSCENE_Cancel_Unit {
  GSHSCENE_CANCEL_BETWEEN_UNITS = 1, ///< Before the first unit and after each unit's progress call.
  GSHSCENE_CANCEL_BEFORE_SUBMIT = 2, ///< Once, before the work is submitted; the render completes after that.
  GSHSCENE_CANCEL_UNIT_COUNT         ///< Closes the enum.
} GSHSCENE_Cancel_Unit;

/** @brief What a sink does for one target (S2).  A sink fills it; the contract layer reads it. */
typedef struct GSHSCENE_Sink_Caps {
  uint32_t item_kinds;        ///< Bit `1 << kind` for each ::GSHSCENE_Item_Kind drawn.
  uint32_t composites;        ///< Bit `1 << op` for each ::GSHSCENE_Composite composited as stated.
  uint32_t flags;             ///< `GSHSCENE_CAP_*`.
  uint32_t cancel_unit;       ///< A ::GSHSCENE_Cancel_Unit.
  /** The most work between two cancel checks, in @p work_measure; `UINT64_MAX` when the sink states none (before-submit). */
  uint64_t cancel_work_bound;
  const char * work_measure;  ///< A static string naming the measure ("units", "cells+points+pixels"); not NULL.
  /** Bit `1 << kind` for each ::GSHSCENE_Gradient_Kind drawn (S17); 0, a sink that draws no gradient. */
  uint32_t gradient_kinds;
  /** The `GSHSCENE_STROKE_NATIVE_*` features a sink emits as a stroke (S20, stroke.h SK11); 0, none. */
  uint32_t stroke_native;
  /** Bit `1 << filter` for each ::GSHSCENE_Image_Filter drawn (S21, IP15); 0, none: a missing one is approximated, `IMAGE_FILTER`. */
  uint32_t image_filters;
  /** Bit `1 << extend` for each ::GSHSCENE_Image_Extend drawn; 0, a sink that draws no image paint (refused, `IMAGE`). */
  uint32_t image_extends;
  /** The `GSHSCENE_IMAGE_FORMAT_*` sample types, layouts and the alpha-only mark it reads; a missing one is refused, `IMAGE`. */
  uint32_t image_formats;
  /**
   * The `GSHSCENE_MASK_NATIVE_*` features of a soft mask (mask.h, MK9) a sink composites; 0, none.  A group's mask needing one
   * the sink lacks is refused, `MASK_FEATURE`, the missing bits in `second`; a sink without ::GSHSCENE_CAP_GROUP_SOFT_MASK
   * refuses every masked group, `SOFT_MASK`, whatever this says.
   */
  uint32_t mask_native;
  uint8_t _reserved[8];       ///< Zero.
} GSHSCENE_Sink_Caps;

/* --- the request (S1) and the job (S3) -------------------------------------------- */

/** Fail with ::GSHSCENE_ERR_REFUSED on any refused effect (S6). */
#define GSHSCENE_RENDER_STRICT (UINT32_C(1) << 0)
/** Allocate nothing; take scratch from the workspace (S9). */
#define GSHSCENE_RENDER_CALLER_MEMORY (UINT32_C(1) << 1)
/** Every render flag defined. */
#define GSHSCENE_RENDER_FLAGS_ALL (GSHSCENE_RENDER_STRICT | GSHSCENE_RENDER_CALLER_MEMORY)

typedef struct GSHSCENE_Job GSHSCENE_Job;

/**
 * @brief Called by the sink after each unit, before its cancel check (S7), on the thread running the render.
 *
 * It may read the job (::gshscene_job_poll), set the render's cancel token, and call ::gshscene_sink_begin (which
 * refuses this job's surface); it must not wait for, destroy or reuse this job.
 */
typedef void (*GSHSCENE_Progress_Fn)(void * ctx, const GSHSCENE_Job * job, size_t units_processed);

/**
 * @brief Called exactly once when a begun job completes, with its result (S3).
 *
 * The report is sealed and the surface released; the job reads as done when this returns.  It runs on the thread
 * that completed the job, inside the call that did (`begin` for a sink that completes inline); it must not wait for
 * or reuse this job.
 */
typedef void (*GSHSCENE_Complete_Fn)(void * ctx, GSHSCENE_Job * job, GSHSCENE_Result result);

/** @brief A render (S1).  Zero-fill it, then set the list, the target and the report. */
typedef struct GSHSCENE_Render_Request {
  const GSHSCENE_List * list;         ///< A closed list.  Kept alive and unmodified until completion (S10).
  const GSHSCENE_Target * target;     ///< The target; copied by `begin`, its surface kept alive until completion.
  GSHSCENE_Report * report;           ///< The report; reset by `begin`.  Read it after completion.
  GSHSCENE_Cancel * cancel;           ///< The token, or NULL for an uncancellable render.
  void * workspace;                   ///< `CALLER_MEMORY`: the scratch memory; else ignored.
  size_t workspace_bytes;             ///< Its size.
  uint32_t flags;                     ///< `GSHSCENE_RENDER_*`.
  GSHSCENE_Progress_Fn progress;      ///< Optional.
  void * progress_ctx;                ///< Passed to it.
  GSHSCENE_Complete_Fn on_complete;   ///< Optional.
  void * on_complete_ctx;             ///< Passed to it.
  uint8_t _reserved[16];              ///< Zero.
} GSHSCENE_Render_Request;

/** @brief Where a job is. */
typedef enum GSHSCENE_Job_State {
  GSHSCENE_JOB_IDLE = 0,    ///< Never begun, or `begin` refused it.
  GSHSCENE_JOB_RUNNING = 1, ///< Begun, not complete.
  GSHSCENE_JOB_DONE = 2     ///< Complete; `on_complete` has returned.
} GSHSCENE_Job_State;

/** @brief What ::gshscene_job_poll says. */
typedef struct GSHSCENE_Job_Status {
  GSHSCENE_Job_State state; ///< The state.
  GSHSCENE_Result result;   ///< When done, the render's result; else ::GSHSCENE_OK.
  size_t units_total;       ///< The list's expansion.
  size_t units_processed;   ///< Units gone through, in paint order.
  size_t units_done;        ///< Units whose effect is in the target.
  uint64_t work_max;        ///< The most work between two cancel checks so far (S7).
} GSHSCENE_Job_Status;

/**
 * @brief A render in flight: the caller's storage (S3).  Zero-fill it before `begin`; read it with the functions
 *   below.  Every field is private to the contract layer and the sink.
 */
struct GSHSCENE_Job {
  GSHSCENE_Sink * sink;              ///< Private.
  GSHSCENE_Render_Request request;   ///< Private: the request, its target pointing at @p target.
  GSHSCENE_Target target;            ///< Private: the target's copy.
  GSHSCENE_Sink_Caps caps;           ///< Private: the caps for the target.
  GSHSCENE_Color_Space target_space; ///< Private: the resolved blend space of the target.
  GCU_Atomic_Int state;              ///< Private.
  GCU_Atomic_Size units_processed;   ///< Private.
  GCU_Atomic_Size units_done;        ///< Private.
  size_t units_total;                ///< Private.
  uint64_t work;                     ///< Private: work since the last check.
  uint64_t work_max;                 ///< Private.
  uint64_t frame_bytes;              ///< Private.
  int result;                        ///< Private.
  int holds_surface;                 ///< Private.
  void * impl_state;                 ///< The sink's own per-job pointer (::gshscene_job_impl_state).
  uint64_t impl_value;               ///< The sink's own per-job value, kept after completion (::gshscene_job_impl_value).
};

/* --- the caller's API ------------------------------------------------------------- */

/** @brief The sink's name (its ops table's); "" for NULL. */
GSHSCENE_API const char * gshscene_sink_name(const GSHSCENE_Sink * sink);

/**
 * @brief The capabilities of @p sink for @p target (S2).
 *
 * @param out Written only on success.
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID (NULL, an invalid target: see S4); ::GSHSCENE_ERR_UNSUPPORTED (a
 *   reserved kind, or a kind or format the sink does not draw).
 */
GSHSCENE_API GSHSCENE_Result gshscene_sink_caps(
    const GSHSCENE_Sink * sink, const GSHSCENE_Target * target, GSHSCENE_Sink_Caps * out);

/**
 * @brief The exact workspace a `CALLER_MEMORY` render of @p request needs (S9).
 *
 * A workspace in @p request is sized first, without allocating; when it is too small (or there is none) the sink
 * measures the need with its own allocator, which this call, not being a render, may use.
 *
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID (as `begin` validates); ::GSHSCENE_ERR_UNSUPPORTED (as for caps);
 *   ::GSHSCENE_ERR_REFUSED (the sink cannot honor the flag for this target).
 */
GSHSCENE_API GSHSCENE_Result gshscene_sink_workspace_bytes(
    GSHSCENE_Sink * sink, const GSHSCENE_Render_Request * request, size_t * out);

/**
 * @brief Begin a render (S3).
 *
 * @param job Zero-filled (or a completed job, which is reused) caller storage, kept until the job is done.
 * @return ::GSHSCENE_OK when the job is pending or finished with ::GSHSCENE_OK.  Otherwise either a refusal to begin
 *   (the job stays idle, nothing was touched and `on_complete` is not called): ::GSHSCENE_ERR_INVALID (a bad request
 *   or target, a busy surface or report, a call from another thread on an affine sink), ::GSHSCENE_ERR_UNSUPPORTED
 *   (as for caps), ::GSHSCENE_ERR_LIMIT (`max_jobs`); or the result of a job that completed inside the call
 *   (the job is done and `on_complete` was called): ::GSHSCENE_ERR_REFUSED, ::GSHSCENE_ERR_CANCELLED,
 *   ::GSHSCENE_ERR_LIMIT, ::GSHSCENE_ERR_OOM, ::GSHSCENE_ERR_IO.  ::gshscene_job_poll tells the two apart.
 */
GSHSCENE_API GSHSCENE_Result gshscene_sink_begin(
    GSHSCENE_Sink * sink, const GSHSCENE_Render_Request * request, GSHSCENE_Job * job);

/**
 * @brief Where a job is; never blocks.
 *
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID (NULL; a job of an affine sink polled from another thread).
 */
GSHSCENE_API GSHSCENE_Result gshscene_job_poll(const GSHSCENE_Job * job, GSHSCENE_Job_Status * out);

/**
 * @brief Block until @p job completes (S13).
 *
 * @return The render's result; ::GSHSCENE_ERR_UNSUPPORTED when the sink cannot block for the job's target, whether
 *   or not the job is done; ::GSHSCENE_ERR_INVALID (NULL, a job never begun or of another sink, the wrong thread).
 */
GSHSCENE_API GSHSCENE_Result gshscene_sink_wait(GSHSCENE_Sink * sink, GSHSCENE_Job * job);

/**
 * @brief Begin and wait: the one call (S1, S13).
 *
 * @return The render's result, or what `begin` refused with; ::GSHSCENE_ERR_UNSUPPORTED before anything begins for a
 *   sink that cannot block.
 */
GSHSCENE_API GSHSCENE_Result gshscene_sink_render(GSHSCENE_Sink * sink, const GSHSCENE_Render_Request * request);

/**
 * @brief Copy a completed render's pixels of the extent into @p out (S15).
 *
 * @param out A raster of the extent's width and height and the target surface's pixel format.
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_UNSUPPORTED (no `READBACK`); ::GSHSCENE_ERR_INVALID (NULL, a job not done or
 *   of another sink, a target without pixels, a raster of another size or format, the wrong thread).
 */
GSHSCENE_API GSHSCENE_Result gshscene_sink_readback(GSHSCENE_Sink * sink, const GSHSCENE_Job * job, GIMG_Raster * out);

/**
 * @brief Upload or cache the static resources @p list names, for renders on targets like @p target (S9).
 *
 * The sink may hold each resource (by id) until ::gshscene_sink_evict or destroy.  A sink without `PREPARE` accepts
 * the call and holds nothing.
 *
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID (NULL, an open list, an invalid target, the wrong thread);
 *   ::GSHSCENE_ERR_UNSUPPORTED (as for caps); ::GSHSCENE_ERR_OOM.
 */
GSHSCENE_API GSHSCENE_Result gshscene_sink_prepare(
    GSHSCENE_Sink * sink, const GSHSCENE_List * list, const GSHSCENE_Target * target);

/** @brief Drop what the sink holds for the resource @p id (S9).  An id it holds nothing for is ::GSHSCENE_OK. */
GSHSCENE_API GSHSCENE_Result gshscene_sink_evict(GSHSCENE_Sink * sink, uint64_t id);

/** @brief The bytes the sink holds across frames (S9); ::GSHSCENE_ERR_INVALID for NULL.  Written only on success. */
GSHSCENE_API GSHSCENE_Result gshscene_sink_static_bytes(const GSHSCENE_Sink * sink, uint64_t * out);

/**
 * @brief Destroy the sink and release everything it holds.
 *
 * @return ::GSHSCENE_OK (NULL too); ::GSHSCENE_ERR_INVALID with nothing destroyed while a job is in flight, or from
 *   another thread on an affine sink.
 */
GSHSCENE_API GSHSCENE_Result gshscene_sink_destroy(GSHSCENE_Sink * sink);

/* --- for sink implementations -------------------------------------------------- */

/** @brief A sink's operations.  `wait`, `readback`, `prepare`, `evict` and `static_bytes` may be NULL. */
typedef struct GSHSCENE_Sink_Ops {
  const char * name; ///< A static string.
  /** Nonzero for a thread-affine sink (S14): its caps say ::GSHSCENE_CAP_THREAD_AFFINE for every target, and the calls that name no target (evict, destroy) are refused from another thread too. */
  int thread_affine;
  /** The caps for a target the contract layer has validated; ::GSHSCENE_ERR_UNSUPPORTED for a kind or format it does not draw. */
  GSHSCENE_Result (*caps)(void * impl, const GSHSCENE_Target * target, GSHSCENE_Sink_Caps * out);
  /**
   * The workspace a `CALLER_MEMORY` render needs (only called when the caps have `CALLER_MEMORY`).  With
   * ::GSHSCENE_RENDER_CALLER_MEMORY in the request's flags (a render's own sizing, and a query that brings a
   * workspace) it **allocates nothing** (S9): it sizes the render inside `request->workspace` (`workspace_bytes` of
   * it; none when NULL) and gives the exact need when that fits, or, when it does not, any need past
   * `workspace_bytes` (a bound: the contract layer then fails the render with ::GSHSCENE_ERR_LIMIT and asks again
   * for a query).  Without the flag (the query's second call, with no workspace) it gives the exact need and may
   * allocate.
   */
  GSHSCENE_Result (*workspace_bytes)(void * impl, const GSHSCENE_Render_Request * request, size_t * out);
  /**
   * Start the job (the contract layer has validated it, planned it and found nothing that stops it).  Complete it
   * with ::gshscene_job_complete now or later and return ::GSHSCENE_OK; or return another result without completing
   * it, and the contract layer completes it with that result.
   */
  GSHSCENE_Result (*begin)(void * impl, GSHSCENE_Job * job);
  /** Block until the job completes.  NULL for a sink that cannot block, or that always completes inside `begin`. */
  GSHSCENE_Result (*wait)(void * impl, GSHSCENE_Job * job);
  /** S15, for a done job on a pixel target and a raster the contract layer has checked.  NULL without `READBACK`. */
  GSHSCENE_Result (*readback)(void * impl, const GSHSCENE_Job * job, GIMG_Raster * out);
  /** S9.  NULL: nothing is held. */
  GSHSCENE_Result (*prepare)(void * impl, const GSHSCENE_List * list, const GSHSCENE_Target * target);
  /** S9.  NULL: nothing is held. */
  GSHSCENE_Result (*evict)(void * impl, uint64_t id);
  /** S9.  NULL: 0. */
  uint64_t (*static_bytes)(void * impl);
  /** Release the implementation (no job is in flight).  Not NULL. */
  void (*destroy)(void * impl);
} GSHSCENE_Sink_Ops;

/**
 * @brief Make a sink from an ops table (for a sink library's constructor).
 *
 * @param alloc The allocator of the sink object, or NULL for the default; copied.
 * @param limits Only `max_jobs` is read (0 is its default); NULL for the defaults.
 * @param ops The operations; copied.  `name`, `caps`, `begin` and `destroy` are required.
 * @param impl The implementation, passed to every operation.  On success the sink owns it (`destroy` releases it);
 *   on failure the caller still does.
 * @param out The sink; written only on success.  The calling thread is its creating thread (S14).
 * @return ::GSHSCENE_OK, ::GSHSCENE_ERR_INVALID or ::GSHSCENE_ERR_OOM.
 */
GSHSCENE_API GSHSCENE_Result gshscene_sink_create(const GSHSCENE_Allocator * alloc, const GSHSCENE_Limits * limits,
    const GSHSCENE_Sink_Ops * ops, void * impl, GSHSCENE_Sink ** out);

/** @brief The job's request; its `target` is the job's copy and its report the caller's.  NULL for NULL. */
GSHSCENE_API const GSHSCENE_Render_Request * gshscene_job_request(const GSHSCENE_Job * job);

/** @brief The caps the job was planned against.  NULL for NULL. */
GSHSCENE_API const GSHSCENE_Sink_Caps * gshscene_job_caps(const GSHSCENE_Job * job);

/** @brief The target's resolved blend space (never the zero space).  NULL for NULL. */
GSHSCENE_API const GSHSCENE_Color_Space * gshscene_job_target_space(const GSHSCENE_Job * job);

/** @brief The sink's per-job pointer; NULL until set. */
GSHSCENE_API void * gshscene_job_impl_state(const GSHSCENE_Job * job);

/** @brief Set the sink's per-job pointer. */
GSHSCENE_API void gshscene_job_set_impl_state(GSHSCENE_Job * job, void * state);

/** @brief The sink's per-job value (an output hash, say), kept after completion; 0 until set or for NULL. */
GSHSCENE_API uint64_t gshscene_job_impl_value(const GSHSCENE_Job * job);

/** @brief Set the sink's per-job value. */
GSHSCENE_API void gshscene_job_set_impl_value(GSHSCENE_Job * job, uint64_t value);

/** @brief Add @p amount to the work done since the last check, in the caps' measure (S7). */
GSHSCENE_API void gshscene_job_work(GSHSCENE_Job * job, uint64_t amount);

/**
 * @brief A unit is done: @p units_processed units have been gone through, @p units_done of them are in the target.
 *
 * Calls the request's `progress`.  Then, for a `BETWEEN_UNITS` sink, the cancel check (::gshscene_job_checkpoint).
 *
 * @return Nonzero if the render must stop now (cancelled), 0 to go on.
 */
GSHSCENE_API int gshscene_job_unit(GSHSCENE_Job * job, size_t units_processed, size_t units_done);

/** @brief A cancel check (S7): closes the work interval (`work_max`) and returns nonzero if the token is set. */
GSHSCENE_API int gshscene_job_checkpoint(GSHSCENE_Job * job);

/** @brief Record the render's peak scratch (S9). */
GSHSCENE_API void gshscene_job_set_frame_bytes(GSHSCENE_Job * job, uint64_t bytes);

/**
 * @brief Add an entry only the sink can find (a refusal while drawing, an approximation the caps cannot state).
 *
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID (NULL, a class or reason outside its enum, a NULL chain with a nonzero
 *   count); ::GSHSCENE_ERR_OOM.  Past the cap it counts and returns ::GSHSCENE_OK.
 */
GSHSCENE_API GSHSCENE_Result gshscene_job_report(GSHSCENE_Job * job, GSHSCENE_Entry_Class entry_class,
    GSHSCENE_Reason reason, const size_t * chain, size_t chain_count);

/**
 * @brief Add an entry about one glyph of a run (glyph.h): as ::gshscene_job_report, with the glyph's index in the
 *   run as the entry's `second`.
 */
GSHSCENE_API GSHSCENE_Result gshscene_job_report_glyph(GSHSCENE_Job * job, GSHSCENE_Entry_Class entry_class,
    GSHSCENE_Reason reason, const size_t * chain, size_t chain_count, size_t glyph);

/**
 * @brief Complete the job (S3): seal the report (sort it, write the summary and @p result, the failure's @p chain and
 *   static @p reason), release the surface, call `on_complete`, mark the job done.  Called once per begun job.
 *
 * @p result ::GSHSCENE_OK becomes ::GSHSCENE_ERR_LIMIT when the report overflowed.
 */
GSHSCENE_API void gshscene_job_complete(GSHSCENE_Job * job, GSHSCENE_Result result, const size_t * chain,
    size_t chain_count, const char * reason);

/** @brief What ::gshscene_caps_disposition decides for an item. */
typedef struct GSHSCENE_Disposition {
  GSHSCENE_Entry_Class entry_class; ///< 0 to draw as stated, `REFUSED` or `APPROXIMATED`.
  GSHSCENE_Reason reason;           ///< Why, when not 0.
  /** `STROKE_STYLE`: the `GSHSCENE_STROKE_NATIVE_*` bits of the features the sink lacks; `PLACEHOLDER`: `source << 16 | code` (PH4); the entry's `second`.  Else 0. */
  uint64_t detail;
} GSHSCENE_Disposition;

/**
 * @brief The one predicate of the planning pass (S6): what @p caps make of @p item, reached through the instances whose
 *   maps are @p instance_maps on a target of map @p target_map whose resolved blend space is @p target_space, under
 *   the palette @p palette (the request's list's, or NULL for none; N14).
 *
 * First, and independent of every caps test, a placeholder is `REFUSED` (`APPROXIMATED` when it is marked approximated)
 * with reason `PLACEHOLDER` and `source << 16 | code` in `detail` (PH3, PH4).  Then, in this order: a kind the caps do
 * not draw is refused (`ITEM_KIND`); a fill, clip or stroke of a glyph run from a
 * font without `GLYPHS_FROM_FONT` is refused (`GLYPH_SOURCE`); a gradient fill or stroke of a kind not in
 * `gradient_kinds` is refused (`GRADIENT`); a fill or stroke whose palette paint, or a stop of whose gradient, names an
 * entry @p palette does not have (::gshscene_paint_resolve, G3) is refused (`PALETTE_ENTRY`); a group whose operator
 * the caps do not composite is refused (`COMPOSITE`); a non-isolated group without `GROUP_NON_ISOLATED`, a knockout
 * group without `GROUP_KNOCKOUT` and a group with a soft mask without `GROUP_SOFT_MASK` are refused, in that order
 * (`GROUP_ISOLATION`, `GROUP_KNOCKOUT`, `SOFT_MASK`, S19), then a group whose mask needs a feature `mask_native` lacks
 * (`MASK_FEATURE`, the missing bits in `detail`, MK9); a stroke with a required feature
 * (::gshscene_stroke_required, which reads the chain) that `stroke_native` lacks, without `STROKE_EXPAND`, is refused
 * (`STROKE_STYLE`, the missing features in `detail`; S20, SK11), after every other refusal and before any
 * approximation; a clip push without `CLIP_AA` is approximated (`CLIP_ALIASED`); a group whose own blend space is
 * stated and differs from @p target_space, without `BLEND_SPACES`, is approximated (`BLEND_SPACE`); a gradient fill or
 * stroke whose interpolation space is stated and differs from @p target_space, without `GRADIENT_SPACES`, is
 * approximated (`INTERPOLATION_SPACE`).  A pop is drawn as its push was decided (the walk tells).  Anything else is
 * drawn.
 *
 * @param instance_maps The maps of the enclosing instances, root to leaf (as ::gshscene_stroke_notice takes them); may
 *   be NULL when @p instance_count is 0.  Only a stroke reads them.
 * @param target_map The target's map, or NULL for the identity.
 */
GSHSCENE_API GSHSCENE_Disposition gshscene_caps_disposition(const GSHSCENE_Sink_Caps * caps,
    const GSHSCENE_Item * item, const GSHGEOM_Map * instance_maps, size_t instance_count, const GSHGEOM_Map * target_map,
    const GSHSCENE_Color_Space * target_space, const GSHSCENE_Resource * palette);

/**
 * @brief Whether a gradient fill draws nothing (S17; paint.h, G10 and G11), and why.
 *
 * The one predicate of the planning pass's `NOTICE` and of every sink's skip.  G11 first: some map of the chain
 * (@p target_map, @p instance_maps root to leaf, the item's map, the gradient's map) has a 2x2 linear part whose
 * determinant is exactly zero (`gshgeom_orient2d`'s exact sign of the two columns); then G10: the gradient is
 * degenerate (linear with an exactly zero denominator, `gshgeom_orient3d`'s exact sign; radial with equal circles;
 * two or more stops at one offset under repeat or reflect; sweep with equal angles under repeat or reflect).
 *
 * @param item A fill; anything else, or a fill whose paint is not a gradient, draws as stated (0).
 * @param instance_maps The maps of the enclosing instances, root to leaf; may be NULL when @p instance_count is 0.
 * @param instance_count How many.
 * @param target_map The target's map, or NULL for the identity.
 * @return 0 when the fill draws as G6 to G9 say; ::GSHSCENE_REASON_MAP_NOT_INVERTIBLE; or
 *   ::GSHSCENE_REASON_GRADIENT_DEGENERATE.
 */
GSHSCENE_API GSHSCENE_Reason gshscene_paint_notice(const GSHSCENE_Item * item, const GSHGEOM_Map * instance_maps,
    size_t instance_count, const GSHGEOM_Map * target_map);

/**
 * @brief A walk over a closed list's expansion in paint order (S16): every item, an instance followed by its
 *   picture's items, with the chain of indices that keys it in a report.
 *
 * Its memory is the caller's (::gshscene_expansion_bytes), so a sink can walk from a workspace.  Fields are private.
 */
typedef struct GSHSCENE_Expansion {
  const GSHSCENE_List ** lists; ///< Private: the list at each level.
  size_t * chain;               ///< Private: the index at each level.
  size_t capacity;              ///< Private: the levels there is room for.
  size_t level;                 ///< Private: the levels in use (0 when done).
  int pending;                  ///< Private: how the next call steps past the last unit.
  int masks;                    ///< Private: the walk enters a masked group's mask picture (set by the planning pass only).
} GSHSCENE_Expansion;

/** @brief The bytes a walk of @p list needs; 0 for NULL. */
GSHSCENE_API size_t gshscene_expansion_bytes(const GSHSCENE_List * list);

/**
 * @brief Start a walk of a closed @p list in @p memory (at least ::gshscene_expansion_bytes, aligned for a pointer).
 *
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID (NULL, an open list, too little memory).
 */
GSHSCENE_API GSHSCENE_Result gshscene_expansion_init(
    GSHSCENE_Expansion * walk, const GSHSCENE_List * list, void * memory, size_t bytes);

/**
 * @brief The next unit.
 *
 * @param out_item The item.
 * @param out_chain Its chain; valid until the next call.
 * @param out_count The chain's length (1 for an item of the list itself).
 * @return 1 and the unit; 0 when the walk is over.
 */
GSHSCENE_API int gshscene_expansion_next(
    GSHSCENE_Expansion * walk, const GSHSCENE_Item ** out_item, const size_t ** out_chain, size_t * out_count);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GSHSCENE_SINK_H */
