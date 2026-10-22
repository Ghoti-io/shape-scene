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
 * The report (report.h, S5): entries keyed by chain, their order, the summary,
 * resolve, the hash and the dump.
 */

#include <ghoti.io/shape-scene/macros.h>

#include "../internal.h"
#include "sink_internal.h"

#include <ghoti.io/shape-scene/version.h>

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

/** The report hash's tag, `u32` after the format. */
#define REPORT_TAG UINT32_C(0x54525052)

/** The report's format, in its hash and its dump. */
#define REPORT_FORMAT 1u

const char * gshscene_entry_class_string(GSHSCENE_Entry_Class entry_class) {
  switch (entry_class) {
    case GSHSCENE_ENTRY_REFUSED:
      return "refused";
    case GSHSCENE_ENTRY_APPROXIMATED:
      return "approximated";
    case GSHSCENE_ENTRY_RESOLVED_AMBIGUITY:
      return "resolved-ambiguity";
    case GSHSCENE_ENTRY_HINT_STATUS:
      return "hint-status";
    case GSHSCENE_ENTRY_NOTICE:
      return "notice";
    case GSHSCENE_ENTRY_CLASS_COUNT:
      break;
  }
  return "unknown class";
}

const char * gshscene_reason_string(GSHSCENE_Reason reason) {
  switch (reason) {
    case GSHSCENE_REASON_ITEM_KIND:
      return "item-kind";
    case GSHSCENE_REASON_COMPOSITE:
      return "composite";
    case GSHSCENE_REASON_GLYPH_SOURCE:
      return "glyph-source";
    case GSHSCENE_REASON_BLEND_SPACE:
      return "blend-space";
    case GSHSCENE_REASON_CLIP_ALIASED:
      return "clip-aliased";
    case GSHSCENE_REASON_RESOURCE_LIMIT:
      return "resource-limit";
    case GSHSCENE_REASON_SEAMLESS:
      return "seamless";
    case GSHSCENE_REASON_CALLER_MEMORY:
      return "caller-memory";
    case GSHSCENE_REASON_TIE_LAYER:
      return "tie-layer";
    case GSHSCENE_REASON_TIE_PATH:
      return "tie-path";
    case GSHSCENE_REASON_TIE_DECLARED:
      return "tie-declared";
    case GSHSCENE_REASON_HINT_ROUTE:
      return "hint-route";
    case GSHSCENE_REASON_HINT_SIZE:
      return "hint-size";
    case GSHSCENE_REASON_HINT_NO_PROGRAM:
      return "hint-no-program";
    case GSHSCENE_REASON_HINT_PROGRAM_FAILED:
      return "hint-program-failed";
    case GSHSCENE_REASON_HINT_WITH_ERRORS:
      return "hint-with-errors";
    case GSHSCENE_REASON_HINT_STRICT_ROUTE:
      return "hint-strict-route";
    case GSHSCENE_REASON_GLYPH_UNAVAILABLE:
      return "glyph-unavailable";
    case GSHSCENE_REASON_FACE_UNBOUND:
      return "face-unbound";
    case GSHSCENE_REASON_PALETTE_ENTRY:
      return "palette-entry";
    case GSHSCENE_REASON_GRADIENT:
      return "gradient";
    case GSHSCENE_REASON_INTERPOLATION_SPACE:
      return "interpolation-space";
    case GSHSCENE_REASON_MAP_NOT_INVERTIBLE:
      return "map-not-invertible";
    case GSHSCENE_REASON_GRADIENT_DEGENERATE:
      return "gradient-degenerate";
    case GSHSCENE_REASON_GROUP_ISOLATION:
      return "group-isolation";
    case GSHSCENE_REASON_GROUP_KNOCKOUT:
      return "group-knockout";
    case GSHSCENE_REASON_SOFT_MASK:
      return "soft-mask";
    case GSHSCENE_REASON_STROKE_STYLE:
      return "stroke-style";
    case GSHSCENE_REASON_STROKE_ZERO_WIDTH:
      return "stroke-zero-width";
    case GSHSCENE_REASON_STROKE_CENTERLINE:
      return "stroke-centerline";
    case GSHSCENE_REASON_COUNT:
      break;
  }
  return "unknown reason";
}

GSHSCENE_Result gshscene_report_create(
    const GSHSCENE_Allocator * alloc, const GSHSCENE_Limits * limits, GSHSCENE_Report ** out) {
  if (!out) {
    return GSHSCENE_ERR_INVALID;
  }
  if (!alloc) {
    alloc = gshscene_allocator_default();
  }
  GSHSCENE_Report * report = gshscene_calloc(alloc, 1, sizeof *report);
  if (!report) {
    return GSHSCENE_ERR_OOM;
  }
  report->alloc = *alloc;
  report->max_entries = limits && limits->max_report_entries ? limits->max_report_entries
                                                             : GSHSCENE_LIMITS_DEFAULT_MAX_REPORT_ENTRIES;
  gshscene_report_clear(report);
  *out = report;
  return GSHSCENE_OK;
}

void gshscene_report_destroy(GSHSCENE_Report * report) {
  if (!report) {
    return;
  }
  GSHSCENE_Allocator alloc = report->alloc;
  gshscene_free(&alloc, report->items);
  gshscene_free(&alloc, report->pool);
  gshscene_free(&alloc, report->scratch);
  gshscene_free(&alloc, report);
}

void gshscene_report_clear(GSHSCENE_Report * report) {
  report->count = 0;
  report->pool_used = 0;
  memset(&report->summary, 0, sizeof report->summary); /* the failure's result is GSHSCENE_OK, 0 */
  report->summary.failure.reason = "";
}

GSHSCENE_Result gshscene_report_reset(GSHSCENE_Report * report) {
  if (!report || gcu_atomic_int_load(&report->busy)) {
    return GSHSCENE_ERR_INVALID;
  }
  gshscene_report_clear(report);
  return GSHSCENE_OK;
}

/** Room for @p more indices in the pool. */
static GSHSCENE_Result pool_reserve(GSHSCENE_Report * report, size_t more) {
  if (more <= report->pool_capacity - report->pool_used) {
    return GSHSCENE_OK;
  }
  size_t want = report->pool_capacity ? report->pool_capacity : 64;
  while (want - report->pool_used < more) {
    if (want > SIZE_MAX / 2 / sizeof(size_t)) {
      return GSHSCENE_ERR_OOM;
    }
    want *= 2;
  }
  size_t * pool = report->alloc.realloc_fn(report->alloc.ctx, report->pool, want * sizeof *pool);
  if (!pool) {
    return GSHSCENE_ERR_OOM;
  }
  report->pool = pool;
  report->pool_capacity = want;
  return GSHSCENE_OK;
}

GSHSCENE_Result gshscene_report_add(GSHSCENE_Report * report, GSHSCENE_Entry_Class entry_class,
    GSHSCENE_Reason reason, const size_t * chain, size_t chain_count, size_t second, double depth) {
  GSHSCENE_Report_Summary * s = &report->summary;
  if (report->count >= report->max_entries) {
    s->counted++;
    s->by_class[entry_class]++;
    s->overflowed = 1;
    return GSHSCENE_OK;
  }
  if (report->count == report->capacity) {
    size_t want = report->capacity ? report->capacity * 2 : 16;
    if (want > report->max_entries) {
      want = report->max_entries;
    }
    GSHSCENE_Report_Item * items =
        report->alloc.realloc_fn(report->alloc.ctx, report->items, want * sizeof *items);
    if (!items) {
      return GSHSCENE_ERR_OOM;
    }
    report->items = items;
    report->capacity = want;
  }
  GSHSCENE_Result result = pool_reserve(report, chain_count);
  if (result != GSHSCENE_OK) {
    return result;
  }
  GSHSCENE_Report_Item * item = &report->items[report->count];
  item->entry_class = (uint32_t)entry_class;
  item->reason = (uint32_t)reason;
  item->chain = report->pool_used;
  item->chain_count = chain_count;
  item->second = second;
  item->depth = depth;
  item->seq = report->count;
  if (chain_count) {
    memcpy(report->pool + report->pool_used, chain, chain_count * sizeof *chain);
  }
  report->pool_used += chain_count;
  report->count++;
  s->counted++;
  s->by_class[entry_class]++;
  return GSHSCENE_OK;
}

void * gshscene_report_scratch(GSHSCENE_Report * report, size_t bytes) {
  if (bytes <= report->scratch_bytes && report->scratch) {
    return report->scratch;
  }
  void * grown = report->alloc.realloc_fn(report->alloc.ctx, report->scratch, bytes ? bytes : 1);
  if (!grown) {
    return NULL;
  }
  report->scratch = grown;
  report->scratch_bytes = bytes;
  return grown;
}

/** Element by element, a prefix before its extensions. */
static int chain_compare(const size_t * a, size_t na, const size_t * b, size_t nb) {
  size_t n = na < nb ? na : nb;
  for (size_t i = 0; i < n; i++) {
    if (a[i] != b[i]) {
      return a[i] < b[i] ? -1 : 1;
    }
  }
  if (na != nb) {
    return na < nb ? -1 : 1;
  }
  return 0;
}

static int item_compare(const void * pa, const void * pb) {
  const GSHSCENE_Report_Item * a = pa;
  const GSHSCENE_Report_Item * b = pb;
  int c = chain_compare(a->key, a->chain_count, b->key, b->chain_count);
  if (c != 0) {
    return c;
  }
  if (a->entry_class != b->entry_class) {
    return a->entry_class < b->entry_class ? -1 : 1;
  }
  if (a->reason != b->reason) {
    return a->reason < b->reason ? -1 : 1;
  }
  if (a->second != b->second) {
    return a->second < b->second ? -1 : 1;
  }
  return a->seq < b->seq ? -1 : (a->seq > b->seq ? 1 : 0);
}

void gshscene_report_seal(GSHSCENE_Report * report, GSHSCENE_Result result, const size_t * chain,
    size_t chain_count, const char * reason) {
  for (size_t i = 0; i < report->count; i++) {
    report->items[i].key = report->items[i].chain_count ? report->pool + report->items[i].chain : NULL;
  }
  if (report->count > 1) {
    qsort(report->items, report->count, sizeof *report->items, item_compare);
  }
  if (result == GSHSCENE_OK && report->summary.overflowed) {
    result = GSHSCENE_ERR_LIMIT;
    if (!reason) {
      reason = "the report's entry cap was passed";
    }
  }
  report->summary.result = result;
  report->summary.failure.result = result;
  report->summary.failure.reason = reason ? reason : ""; /* the chain count is the 0 a render's clear left */
  if (result != GSHSCENE_OK && chain_count && pool_reserve(report, chain_count) == GSHSCENE_OK) {
    report->failure_chain = report->pool_used;
    memcpy(report->pool + report->pool_used, chain, chain_count * sizeof *chain);
    report->pool_used += chain_count;
    report->summary.failure.chain_count = chain_count;
  }
}

size_t gshscene_report_count(const GSHSCENE_Report * report) {
  return report ? report->count : 0;
}

GSHSCENE_Result gshscene_report_entry(const GSHSCENE_Report * report, size_t index, GSHSCENE_Report_Entry * out) {
  if (!report || !out || index >= report->count) {
    return GSHSCENE_ERR_INVALID;
  }
  const GSHSCENE_Report_Item * item = &report->items[index];
  out->entry_class = (GSHSCENE_Entry_Class)item->entry_class;
  out->reason = (GSHSCENE_Reason)item->reason;
  out->chain = item->chain_count ? report->pool + item->chain : NULL;
  out->chain_count = item->chain_count;
  out->second = item->second;
  out->depth = item->depth;
  return GSHSCENE_OK;
}

GSHSCENE_Result gshscene_report_summary(const GSHSCENE_Report * report, GSHSCENE_Report_Summary * out) {
  if (!report || !out) {
    return GSHSCENE_ERR_INVALID;
  }
  *out = report->summary;
  out->failure.chain = out->failure.chain_count ? report->pool + report->failure_chain : NULL;
  return GSHSCENE_OK;
}

GSHSCENE_Result gshscene_report_resolve(const GSHSCENE_Report * report, const GSHSCENE_Version * version,
    size_t index, uint64_t * out_node, uint64_t * out_path, size_t capacity, size_t * out_count) {
  if (!report || !version || !out_node || !out_path || !out_count || index >= report->count) {
    return GSHSCENE_ERR_INVALID;
  }
  const GSHSCENE_Report_Item * item = &report->items[index];
  if (item->chain_count == 0 || capacity < item->chain_count) {
    return GSHSCENE_ERR_INVALID;
  }
  const size_t * chain = report->pool + item->chain;
  uint64_t table = 0;
  for (size_t i = 0; i < item->chain_count; i++) {
    uint64_t node = 0;
    GSHSCENE_Result result = gshscene_version_item_node(version, table, chain[i], &node);
    if (result != GSHSCENE_OK) {
      return result;
    }
    if (i + 1 < item->chain_count) {
      GSHSCENE_Node_Info info;
      if (gshscene_version_node(version, node, &info) != GSHSCENE_OK || info.node.kind != GSHSCENE_NODE_INSTANCE) {
        return GSHSCENE_ERR_INVALID;
      }
      table = info.node.target;
    }
    out_path[i] = node;
  }
  *out_node = out_path[item->chain_count - 1];
  *out_count = item->chain_count;
  return GSHSCENE_OK;
}

uint64_t gshscene_report_hash(const GSHSCENE_Report * report) {
  GSHGEOM_Hash h;
  gshgeom_hash_init(&h);
  if (!report) {
    return gshgeom_hash_value(&h);
  }
  const GSHSCENE_Report_Summary * s = &report->summary;
  gshgeom_hash_u32(&h, REPORT_FORMAT);
  gshgeom_hash_u32(&h, REPORT_TAG);
  gshgeom_hash_u32(&h, (uint32_t)s->result);
  gshgeom_hash_u64(&h, (uint64_t)s->units_total);
  gshgeom_hash_u64(&h, (uint64_t)s->units_processed);
  gshgeom_hash_u64(&h, (uint64_t)s->units_done);
  gshgeom_hash_u64(&h, (uint64_t)s->counted);
  gshgeom_hash_u8(&h, (uint8_t)(s->overflowed != 0));
  for (int c = GSHSCENE_ENTRY_REFUSED; c < GSHSCENE_ENTRY_CLASS_COUNT; c++) {
    gshgeom_hash_u64(&h, (uint64_t)s->by_class[c]);
  }
  gshgeom_hash_u64(&h, s->static_bytes);
  gshgeom_hash_u64(&h, s->work_max);
  gshgeom_hash_u32(&h, (uint32_t)s->failure.result);
  gshgeom_hash_u64(&h, (uint64_t)s->failure.chain_count);
  for (size_t i = 0; i < s->failure.chain_count; i++) {
    gshgeom_hash_u64(&h, (uint64_t)report->pool[report->failure_chain + i]);
  }
  gshgeom_hash_u64(&h, (uint64_t)report->count);
  for (size_t i = 0; i < report->count; i++) {
    const GSHSCENE_Report_Item * item = &report->items[i];
    gshgeom_hash_u8(&h, (uint8_t)item->entry_class);
    gshgeom_hash_u32(&h, item->reason);
    gshgeom_hash_u64(&h, (uint64_t)item->chain_count);
    for (size_t k = 0; k < item->chain_count; k++) {
      gshgeom_hash_u64(&h, (uint64_t)report->pool[item->chain + k]);
    }
    gshgeom_hash_u64(&h, (uint64_t)item->second);
    gshgeom_hash_f64(&h, item->depth);
  }
  return gshgeom_hash_value(&h);
}

/** A chain as `a.b.c`, or `-`. */
static void chain_dump(FILE * out, const size_t * chain, size_t count) {
  if (count == 0) {
    fputc('-', out);
    return;
  }
  for (size_t i = 0; i < count; i++) {
    fprintf(out, i ? ".%" PRIu64 : "%" PRIu64, (uint64_t)chain[i]);
  }
}

GSHSCENE_Result gshscene_report_dump(const GSHSCENE_Report * report, FILE * out) {
  if (!report || !out) {
    return GSHSCENE_ERR_INVALID;
  }
  const GSHSCENE_Report_Summary * s = &report->summary;
  fprintf(out,
      "gshscene.report %u result=%d units=%" PRIu64 "/%" PRIu64 "/%" PRIu64 " entries=%" PRIu64 " counted=%" PRIu64
      " overflowed=%d static=%" PRIu64 " frame=%" PRIu64 " workspace=%" PRIu64 " work=%" PRIu64 " hash=%016" PRIx64
      "\n",
      REPORT_FORMAT, (int)s->result, (uint64_t)s->units_total, (uint64_t)s->units_processed,
      (uint64_t)s->units_done, (uint64_t)report->count, (uint64_t)s->counted, s->overflowed ? 1 : 0,
      s->static_bytes, s->frame_bytes, s->workspace_needed, s->work_max, gshscene_report_hash(report));
  if (s->result != GSHSCENE_OK) {
    fprintf(out, "failure result=%d chain=", (int)s->failure.result);
    chain_dump(out, report->pool + report->failure_chain, s->failure.chain_count);
    fprintf(out, " reason=%s\n", s->failure.reason);
  }
  for (size_t i = 0; i < report->count; i++) {
    const GSHSCENE_Report_Item * item = &report->items[i];
    fprintf(out, "E class=%u reason=%u chain=", (unsigned)item->entry_class, (unsigned)item->reason);
    chain_dump(out, report->pool + item->chain, item->chain_count);
    fprintf(out, " second=%" PRIu64 " depth=%016" PRIx64 "\n", (uint64_t)item->second, gshgeom_f64_bits(item->depth));
  }
  return (fflush(out) == 0 && !ferror(out)) ? GSHSCENE_OK : GSHSCENE_ERR_IO;
}
