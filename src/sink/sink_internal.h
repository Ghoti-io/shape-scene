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
 * What the contract layer (sink.c), the report (report.c) and the test sink
 * (dump_sink.c) share.  Internal: not installed.
 */

#ifndef GHOTI_IO_GSHSCENE_SINK_INTERNAL_H
#define GHOTI_IO_GSHSCENE_SINK_INTERNAL_H

#include <ghoti.io/shape-scene/macros.h>

#include <ghoti.io/shape-scene/dump_sink.h>
#include <ghoti.io/shape-scene/report.h>
#include <ghoti.io/shape-scene/sink.h>

#include <ghoti.io/cutil/atomic.h>
#include <ghoti.io/cutil/mutex.h>

#include <stddef.h>
#include <stdint.h>

/** One stored entry; its chain is `count` indices at `chain` in the report's pool. */
typedef struct GSHSCENE_Report_Item {
  uint32_t entry_class;
  uint32_t reason;
  size_t chain;       /* offset in the pool */
  size_t chain_count;
  size_t second;
  double depth;
  size_t seq;         /* the order it was added in: the last key */
  const size_t * key; /* the chain, set for the sort only */
} GSHSCENE_Report_Item;

struct GSHSCENE_Report {
  GSHSCENE_Allocator alloc;
  size_t max_entries;
  GSHSCENE_Report_Item * items;
  size_t count;
  size_t capacity;
  size_t * pool;           /* every stored chain, and the failure's */
  size_t pool_used;
  size_t pool_capacity;
  GSHSCENE_Report_Summary summary; /* failure.chain is NULL here: see failure_chain */
  size_t failure_chain;    /* offset in the pool */
  void * scratch;          /* the planning pass's walk memory */
  size_t scratch_bytes;
  GCU_Atomic_Int busy;     /* a render is using it */
};

struct GSHSCENE_Sink {
  GSHSCENE_Allocator alloc;
  GSHSCENE_Sink_Ops ops;
  void * impl;
  uintptr_t creator;  /* the creating thread's token */
  GCU_MUTEX_T lock;   /* the busy table */
  size_t max_jobs;
  size_t in_flight;
  const void ** busy; /* max_jobs surfaces */
};

/**
 * What a refusal skips (S6): a refused push its whole block, up to its matching pop; a refused instance its picture.
 * `level` is the chain length of the refused item (0: nothing is being skipped).
 */
typedef struct GSHSCENE_Skip {
  size_t level;
  size_t depth;  /* pushes open inside the block, at its level */
  int instance;  /* the refused item is an instance */
} GSHSCENE_Skip;

/** Start skipping after a refused @p item at chain length @p count (a fill or a pop skips nothing more). */
static inline void gshscene_skip_begin(GSHSCENE_Skip * skip, const GSHSCENE_Item * item, size_t count) {
  if (item->kind == GSHSCENE_ITEM_CLIP_PUSH || item->kind == GSHSCENE_ITEM_GROUP_PUSH ||
      item->kind == GSHSCENE_ITEM_INSTANCE) {
    skip->level = count;
    skip->depth = 0;
    skip->instance = item->kind == GSHSCENE_ITEM_INSTANCE;
  }
}

/** Whether the unit @p item at chain length @p count is inside what is being skipped (and step past it). */
static inline int gshscene_skip_step(GSHSCENE_Skip * skip, const GSHSCENE_Item * item, size_t count) {
  if (!skip->level) {
    return 0;
  }
  if (count > skip->level) {
    return 1;
  }
  if (skip->instance || count < skip->level) {
    skip->level = 0;
    return 0;
  }
  if (item->kind == GSHSCENE_ITEM_CLIP_PUSH || item->kind == GSHSCENE_ITEM_GROUP_PUSH) {
    skip->depth++;
  } else if (item->kind == GSHSCENE_ITEM_CLIP_POP || item->kind == GSHSCENE_ITEM_GROUP_POP) {
    if (skip->depth == 0) {
      skip->level = 0; /* the matching pop: skipped, and the block is over */
    } else {
      skip->depth--;
    }
  }
  return 1;
}

/** A token for the calling thread: distinct for threads alive at the same time. */
uintptr_t gshscene_thread_token(void);

/** Empty the report (not its busy flag), keeping its storage. */
void gshscene_report_clear(GSHSCENE_Report * report);

/** Add an entry (validated by the caller); counts past the cap. */
GSHSCENE_Result gshscene_report_add(GSHSCENE_Report * report, GSHSCENE_Entry_Class entry_class,
    GSHSCENE_Reason reason, const size_t * chain, size_t chain_count, size_t second, double depth);

/** Make room for @p bytes of scratch; NULL on failure. */
void * gshscene_report_scratch(GSHSCENE_Report * report, size_t bytes);

/** Sort the entries and write the summary's result and failure (its chain copied into the pool). */
void gshscene_report_seal(GSHSCENE_Report * report, GSHSCENE_Result result, const size_t * chain,
    size_t chain_count, const char * reason);

/** Whether @p sink is a test sink (dump_sink.c). */
int gshscene_dump_sink_is(const GSHSCENE_Sink * sink);

#endif /* GHOTI_IO_GSHSCENE_SINK_INTERNAL_H */
