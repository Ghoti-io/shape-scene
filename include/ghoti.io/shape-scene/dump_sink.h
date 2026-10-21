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
 * @file dump_sink.h
 *
 * The test sink (story 13a, AD-11's second backend): a ::GSHSCENE_Sink that
 * composites nothing, writes an AD-21 dump of every unit it goes through, and
 * stamps one marker rectangle per drawn unit into a raster target, so that the
 * contract's statements about a target (strict leaves it untouched, cancel leaves
 * a prefix, damage leaves the outside alone) can be seen to hold or to break.
 * Its capabilities are configurable, so every path of the contract is reachable:
 * refusals and approximations, a sink that cannot block and completes when it is
 * stepped, a sink with thread affinity, a sink that can cancel only before
 * submission, a sink without readback or caller memory, and a writer (`STREAM`)
 * target.  It is independent of shape-cpu: it shares only `shape-geometry`'s maps
 * and bounds.
 *
 * ### What it does
 *
 * Targets: an RGBA 8-bit interleaved raster (either alpha form) and a stream;
 * which of the two it accepts, and its caps for each, are the configuration's.
 * It walks the list's expansion in paint order (sink.h's ::GSHSCENE_Expansion).  For
 * each unit it decides with ::gshscene_caps_disposition (a refused push or
 * instance skips its block, as S6 says), and for a fill or clip it composes the
 * maps of the enclosing instances, the item's and the target's
 * (`gshgeom_map_compose_chain`) and maps the four corners of the resource's
 * model-space box: its **box**, in target units.  The box of a path is its
 * bounds; of a run from outlines, the union of its outlines' bounds moved by each
 * glyph's position; of a run from a font, the union of its glyphs' design boxes
 * (the provider's `glyph_box`) placed by each glyph's position at `size / upem`
 * (a glyph whose box cannot be had adds nothing).  An item whose box is wider or
 * taller than `max_extent` (the sink's one resource limit) is refused while
 * drawing (`RESOURCE_LIMIT`).
 *
 * **A run from a font** (story 14a) is decided with glyph.h's route, as the plan
 * decided it: a run the route refuses is skipped (the plan reported it).  Each
 * glyph then goes to its face's provider by its route, `glyph_device_path` on the
 * fast tier and `glyph_outline` on the general route, and the provider's answer
 * makes glyph.h's per-glyph entry; a refused glyph under strict stops the render
 * at the item (S6).  A glyph's provider paths take at most 64 KiB of scratch,
 * carved from the render's memory (so `CALLER_MEMORY` holds); past it the glyph is
 * refused (`RESOURCE_LIMIT`).
 *
 * **Stamps** (pixel targets only).  A fill outside every group stamps its box,
 * rounded out to whole pixels and clipped by the clips in force, the extent and
 * the damage, with four bytes of its **mark** (the low 32 bits of FNV-1a 64 over
 * `u64` chain length and `u64` each index, little-endian: R, G, B, A).  Inside a
 * group nothing is stamped: the group gathers the union of its fills' clipped
 * boxes and, at its pop, stamps that union (clipped by the clips in force at its
 * push) with its push's mark, or hands it to the group around it.  So a group is
 * one rectangle, and a group still open when a render stops is never stamped (S7).
 *
 * ### The dump (format 1)
 *
 * Written to a stream target, or to `log` for a raster target, one line per
 * record: `gshscene.dump-sink 1 kind=N unit=N units=T list=<16 hex digits>`; then,
 * for each unit, `U <chain> <record>` where the record is
 * `F res=<kind>:<hash> map=<hash> rule=N box=<x0> <y0> <x1> <y1>` (a fill; a
 * gradient fill has ` grad=<id>`, the gradient's resource id, before `box=`),
 * `C res=<kind>:<hash> map=<hash> rule=N box=<x0> <y0> <x1> <y1>` (a clip push),
 * `c` (a clip pop), `G opacity=<hex> composite=N` (a group push), `g` (a group pop),
 * `I res=picture:<hash>` (an instance), or `X class=N reason=N` (a unit not
 * drawn: refused, or inside a refused block, or a gradient fill that draws
 * nothing, class 5, `NOTICE`, which is not stamped; sink.h, S17); after a
 * gradient fill's record, the first time the render meets that gradient, its
 * record `R <id> <hash> space=<space> stops=N` (the interpolation space as
 * `P:T:<gamma hex>`) and one line `r <offset> <r> <g> <b> <a>` per stop as
 * `gshscene_gradient_resolve` gives it (each double's bits; the request's list's
 * foreground and palette, the target's resolved space): once per gradient per
 * render, by id, as a writer emits one; for a fill or clip of a run from a
 * font, after its record, one line per glyph, `Y <index> f <hash> status=N` (the
 * fast tier: `gshgeom_device_path_hash` of the device path, the whole pixel
 * added, and the provider's hint status), `Y <index> g <hash> status=0` (the
 * general route: FNV-1a 64 of `u64` `gshgeom_path_hash` of the outline and `u64`
 * `gshgeom_map_hash` of the glyph's map) or `Y <index> x reason=N` (a refused
 * glyph); for a pixel target, after the unit,
 * `S <x0> <y0> <x1> <y1> <mark>` for each stamp it makes (decimal pixel edges,
 * the mark as 8 hexadecimal digits); and last `E result=N processed=P done=D`.  A
 * chain is its indices in decimal joined by `.`; a hash is 16 hexadecimal digits
 * and a `double` the 16 hexadecimal digits of its bit pattern; `map` is
 * `gshgeom_map_hash` of the composed map.  The **output hash** is FNV-1a 64 of the
 * dump's bytes: the test sink's output is a text document, so its spelling is its
 * text.
 *
 * ### Units and cancel
 *
 * Each unit is one unit of work (`work_measure` "units").  In-line, a render
 * completes inside `begin`.  Deferred (::GSHSCENE_DUMP_SINK_DEFERRED), `begin`
 * returns with the job running and ::gshscene_dump_sink_step goes through up to
 * `step_units` units of every running job, the way an event loop turns; `wait`
 * steps until the job is done when the caps say it can block.
 */

#ifndef GHOTI_IO_GSHSCENE_DUMP_SINK_H
#define GHOTI_IO_GSHSCENE_DUMP_SINK_H

#include <ghoti.io/shape-scene/allocator.h>
#include <ghoti.io/shape-scene/core.h>
#include <ghoti.io/shape-scene/macros.h>
#include <ghoti.io/shape-scene/sink.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief When a job of the test sink completes. */
typedef enum GSHSCENE_Dump_Sink_Completion {
  GSHSCENE_DUMP_SINK_INLINE = 0,   ///< Inside `begin`.
  GSHSCENE_DUMP_SINK_DEFERRED = 1, ///< When stepped (::gshscene_dump_sink_step) or waited for.
  GSHSCENE_DUMP_SINK_COMPLETION_COUNT
} GSHSCENE_Dump_Sink_Completion;

/** @brief The test sink's configuration.  Start from ::gshscene_dump_sink_config_default. */
typedef struct GSHSCENE_Dump_Sink_Config {
  GSHSCENE_Sink_Caps raster_caps; ///< Its caps for an RGBA 8-bit raster target.
  GSHSCENE_Sink_Caps stream_caps; ///< Its caps for a stream target.
  uint32_t targets;               ///< Bit `1 << kind` for each target kind it accepts (raster, stream).
  uint32_t completion;            ///< A ::GSHSCENE_Dump_Sink_Completion.
  size_t step_units;              ///< Deferred: units a step goes through per job; 0 is all of them.
  /** The resource limit: a fill or clip whose box is wider or taller than this, in target units, is refused while drawing.  0 is none. */
  double max_extent;
  /** For a raster target, where the dump goes; NULL for nowhere (the output hash is still kept).  Not owned. */
  const GSHSCENE_Writer * log;
  uint8_t _reserved[16];          ///< Zero.
} GSHSCENE_Dump_Sink_Config;

/**
 * @brief The default configuration: both targets; for each, every item kind, operator and gradient kind, the flags
 *   `CAN_BLOCK`, `READBACK` (raster only), `CALLER_MEMORY`, `GLYPHS_FROM_FONT`, `CLIP_AA`, `BLEND_SPACES`,
 *   `GRADIENT_SPACES` and `PREPARE`; cancel
 *   between units with a work bound of 1 unit; in-line; no extent limit; no log.  NULL is ignored.
 */
GSHSCENE_API void gshscene_dump_sink_config_default(GSHSCENE_Dump_Sink_Config * config);

/**
 * @brief Make a test sink.
 *
 * @param alloc Its allocator (every allocation of a render that is not `CALLER_MEMORY`), or NULL for the default.
 * @param limits `max_jobs` (sink.h); NULL for the defaults.
 * @param config The configuration (copied), or NULL for the default.
 * @param out The sink; written only on success.  Destroy it with ::gshscene_sink_destroy.
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID (NULL @p out; caps with undefined bits (flags or gradient kinds), a cancel unit outside its
 *   enum, a NULL work measure, nonzero reserved bytes; `CAN_BLOCK` and `THREAD_AFFINE` differing between the two
 *   caps; an unknown target bit or completion; a negative or non-finite `max_extent`; an in-line sink that cannot
 *   block); ::GSHSCENE_ERR_OOM.
 */
GSHSCENE_API GSHSCENE_Result gshscene_dump_sink_create(const GSHSCENE_Allocator * alloc,
    const GSHSCENE_Limits * limits, const GSHSCENE_Dump_Sink_Config * config, GSHSCENE_Sink ** out);

/**
 * @brief Turn the event loop of a deferred test sink once: up to `step_units` units of every running job.
 *
 * Jobs complete here (and their `on_complete` runs here).  Call it from the creating thread of an affine sink.
 *
 * @param out_running The jobs still running afterwards, or NULL.
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID (NULL, not a test sink, the wrong thread).
 */
GSHSCENE_API GSHSCENE_Result gshscene_dump_sink_step(GSHSCENE_Sink * sink, size_t * out_running);

/**
 * @brief The output hash of a completed job of a test sink (see the file comment).
 *
 * @return ::GSHSCENE_OK; ::GSHSCENE_ERR_INVALID (NULL, not done, not a test sink's job).
 */
GSHSCENE_API GSHSCENE_Result gshscene_dump_sink_output_hash(const GSHSCENE_Job * job, uint64_t * out);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GSHSCENE_DUMP_SINK_H */
