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
 * @file core.h
 *
 * Result codes, limits and the version this build reports.
 */

#ifndef GHOTI_IO_GSHSCENE_CORE_H
#define GHOTI_IO_GSHSCENE_CORE_H

#include <ghoti.io/shape-scene/macros.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Result of an operation.
 *
 * Zero is success.  ::GSHSCENE_RESULT_COUNT closes the enum so that a test can
 * check the string table is complete.  This is the suite's fixed vocabulary
 * (CONVENTIONS section 5) plus ::GSHSCENE_ERR_REFUSED, which AD-9 and AD-11
 * add (a merge of edits that overlap is refused, naming the IDs; a strict
 * render with a refused effect, and the conflation flag, sink.h), and
 * ::GSHSCENE_ERR_CANCELLED, which the sink contract adds (AD-11, AD-26).  Most
 * of the vocabulary is never returned by this library (it reads no bytes).
 */
typedef enum {
  GSHSCENE_OK = 0,          ///< The operation succeeded.
  GSHSCENE_ERR_IO,          ///< A write to a caller's stream failed.
  GSHSCENE_ERR_FORMAT,      ///< Not returned by this library.
  GSHSCENE_ERR_UNSUPPORTED, ///< A reserved value: a kind or a field that a later story gives meaning to.
  GSHSCENE_ERR_LIMIT,       ///< A stated cap was exceeded; nothing was truncated and the object is as it was.
  GSHSCENE_ERR_CORRUPT,     ///< Not returned by this library.
  GSHSCENE_ERR_OOM,         ///< The allocator returned NULL.
  GSHSCENE_ERR_INVALID,     ///< A caller-supplied argument is wrong, or the call is out of sequence.
  GSHSCENE_ERR_INTERNAL,    ///< The library's own invariant failed.
  /**
   * A well-formed request that the library declines to carry out: edits that touch the same IDs (AD-9), nothing
   * changed; a strict render with a refused effect or a render asking for the conflation flag (sink.h, S6, S8).
   */
  GSHSCENE_ERR_REFUSED,
  /** The render's cancel token was set (sink.h, S7): the target holds the finished work, a prefix of paint order. */
  GSHSCENE_ERR_CANCELLED,
  GSHSCENE_RESULT_COUNT
} GSHSCENE_Result;

/**
 * @brief Static description of a result code.
 *
 * The string is never NULL, never allocated, and never contains caller data.
 *
 * @param result The result code, including values outside the enum.
 * @return A static string.
 */
GSHSCENE_API const char * gshscene_result_string(GSHSCENE_Result result);

/** Default ::GSHSCENE_Limits.max_resources: 16 Mi live resources in a store. */
#define GSHSCENE_LIMITS_DEFAULT_MAX_RESOURCES ((size_t)1 << 24)

/** Default ::GSHSCENE_Limits.max_resource_elements: 4 Mi path elements, glyphs or picture items. */
#define GSHSCENE_LIMITS_DEFAULT_MAX_RESOURCE_ELEMENTS ((size_t)1 << 22)

/** Default ::GSHSCENE_Limits.max_list_items: 1 Mi items in a list. */
#define GSHSCENE_LIMITS_DEFAULT_MAX_LIST_ITEMS ((size_t)1 << 20)

/** The most items a list can hold whatever its limit says: 2^30. */
#define GSHSCENE_LIST_ITEMS_CEILING ((size_t)1 << 30)

/** Default ::GSHSCENE_Limits.max_list_bytes: 256 MiB of arena chunks per list. */
#define GSHSCENE_LIMITS_DEFAULT_MAX_LIST_BYTES ((size_t)256 * 1024 * 1024)

/** Default ::GSHSCENE_Limits.max_depth: 256 nested clips and groups. */
#define GSHSCENE_LIMITS_DEFAULT_MAX_DEPTH ((size_t)256)

/** Default ::GSHSCENE_Limits.max_picture_depth: 32 pictures nested in pictures. */
#define GSHSCENE_LIMITS_DEFAULT_MAX_PICTURE_DEPTH ((size_t)32)

/** Default ::GSHSCENE_Limits.max_expansion: 16 Mi items once every instance is expanded. */
#define GSHSCENE_LIMITS_DEFAULT_MAX_EXPANSION ((size_t)1 << 24)

/** Default ::GSHSCENE_Limits.max_bytes: 1 GiB of accounted resource bytes in a store (see resource.h). */
#define GSHSCENE_LIMITS_DEFAULT_MAX_BYTES ((size_t)1 << 30)

/** Default ::GSHSCENE_Limits.max_nodes: 4 Mi nodes in a version. */
#define GSHSCENE_LIMITS_DEFAULT_MAX_NODES ((size_t)1 << 22)

/** Default ::GSHSCENE_Limits.max_node_depth: 256 levels of nodes in a version. */
#define GSHSCENE_LIMITS_DEFAULT_MAX_NODE_DEPTH ((size_t)256)

/** The deepest a version's tree can be whatever its limit says, so that no walk recurses further: 1024. */
#define GSHSCENE_NODE_DEPTH_CEILING ((size_t)1024)

/** Default ::GSHSCENE_Limits.max_report_entries: 64 Ki entries stored in a report (report.h). */
#define GSHSCENE_LIMITS_DEFAULT_MAX_REPORT_ENTRIES ((size_t)1 << 16)

/** Default ::GSHSCENE_Limits.max_jobs: 64 renders in flight on one sink (sink.h, S3). */
#define GSHSCENE_LIMITS_DEFAULT_MAX_JOBS ((size_t)64)

/**
 * @brief Caps for the quantities a caller can make unbounded.
 *
 * A field of 0 means the default above.  NULL where a `const GSHSCENE_Limits *`
 * is taken means every default.  Exceeding a cap is ::GSHSCENE_ERR_LIMIT, never
 * a silent truncation, and the object is left as it was.  Each cap is checked
 * where its quantity grows, so exactly the cap passes and one more is refused.
 */
typedef struct GSHSCENE_Limits {
  size_t max_resources;         ///< Most live resources one store may hold.
  size_t max_resource_elements; ///< Most path elements, glyphs, or items of a picture's list one resource may hold.
  size_t max_list_items;        ///< Most items one list may hold (at most ::GSHSCENE_LIST_ITEMS_CEILING).
  size_t max_list_bytes;        ///< Most bytes of arena chunks one list may take from its allocator.
  size_t max_depth;             ///< Most clips and groups open at once in one list.
  size_t max_picture_depth;     ///< Most levels of pictures nested in pictures.
  /**
   * Most items a list is with every instance replaced by its picture's items.  For a retained scene
   * (version.h) the most occurrences of the root: every node once for each time it is drawn.
   */
  size_t max_expansion;
  size_t max_bytes;             ///< Most accounted bytes of live resources one store may hold (resource.h).
  size_t max_nodes;             ///< Most nodes one version may hold, the root included.
  /** Most levels of nodes in a version, an instance's target counting as its child (at most ::GSHSCENE_NODE_DEPTH_CEILING). */
  size_t max_node_depth;
  /** Most entries a report stores (report.h); past it the report counts, sets `overflowed`, and the render is ::GSHSCENE_ERR_LIMIT. */
  size_t max_report_entries;
  size_t max_jobs;              ///< Most renders in flight on one sink at once (sink.h, S3).
  uint8_t _reserved[8];         ///< Reserved; zero.
} GSHSCENE_Limits;

/**
 * @brief Fill @p limits with the built-in defaults.
 *
 * @param limits The structure to fill; NULL is ignored.
 */
GSHSCENE_API void gshscene_limits_default(GSHSCENE_Limits * limits);

/**
 * @brief This build's version, as the string the Makefile generated.
 *
 * @return A static string, never NULL.  "0.0.0", or "0.0.0-dev" when BRANCH
 *   was overridden.
 */
GSHSCENE_API const char * gshscene_version_string(void);

/**
 * @brief This build's version, packed as ::GSHSCENE_MAKE_VERSION packs it.
 *
 * @return `(major << 16) | (minor << 8) | patch`.
 */
GSHSCENE_API unsigned gshscene_version_number(void);

#ifdef __cplusplus
}
#endif

#endif /* GHOTI_IO_GSHSCENE_CORE_H */
