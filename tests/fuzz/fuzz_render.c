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
 * The render request driven by bytes, through the test sink (story 13a, CAP-15,
 * AD-19): one entry point for libFuzzer and for the replay gate.
 *
 * Seven header bytes choose the test sink's caps (what it refuses, whether it
 * clips with antialiasing and blends in stated spaces, whether it can block,
 * when it can cancel, whether it takes caller memory, its affinity), the request
 * (strict, caller memory and the workspace's size against the need, a raster or
 * a stream target, the conflation flag, a damage, an extent), where the token is
 * set, the sink's extent limit and the report's cap, the write of the stream and
 * the allocation that fail, and how many units a deferred step goes through.  The
 * rest builds up to three pictures and a list (fills of paths, of glyph runs from
 * a face and from outlines, foreground fills, clips, groups with every operator,
 * blend space and ordering, instances), always balanced and closed.  After the
 * render the contract's invariants are checked, and a violation is an `abort`:
 *
 * - the result is one the contract allows for the request, and the report's
 *   summary says the same; its entries are in the stated order and its counts
 *   agree with them; the cap is a limit, never a silent truncation;
 * - units done never exceed units processed, nor those the total; the work
 *   between two checks is within the declared bound; a cancel at unit k stops
 *   after exactly k units on a between-units sink;
 * - strict with a refusal found by the plan, the conflation flag, a refused or
 *   too small workspace and a cancel before the start leave the target as it was;
 *   a raster's pixels outside the damage and the extent are never touched;
 * - nothing leaks: the sink and the report take every byte from a counting
 *   allocator (which also fails the chosen call), and it says nothing is live at
 *   the end.
 *
 * The replay fails if the corpus never reaches an outcome (each result, an
 * approximation, a tie, a deferred completion, a before-submit sink running past
 * its token): tools/gen-render-corpus.py writes it.
 */

#include <ghoti.io/shape-scene/shape-scene.h>

#include "../glyph_test_provider.h"

#include <ghoti.io/image/raster.h>
#include <ghoti.io/shape-geometry/shape-geometry.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond) \
  do { \
    if (!(cond)) { \
      fprintf(stderr, "fuzz_render: invariant failed: %s (line %d)\n", #cond, __LINE__); \
      abort(); \
    } \
  } while (0)

/* --- the outcomes the corpus must reach ----------------------------------------------------------- */

enum {
  OUT_OK = 0, OUT_STRICT_PLANNED, OUT_STRICT_DRAWING, OUT_CANCEL_START, OUT_CANCEL_MID, OUT_SUBMITTED_PAST_CANCEL,
  OUT_LIMIT_WORKSPACE, OUT_LIMIT_REPORT, OUT_IO, OUT_OOM, OUT_SEAMLESS, OUT_CALLER_MEMORY_REFUSED, OUT_APPROXIMATED,
  OUT_TIES, OUT_DEFERRED, OUT_INVALID, OUT_UNSUPPORTED, OUT_COUNT
};

static const char * const outcome_names[OUT_COUNT] = {"ok", "strict, refused by the plan", "strict, refused while drawing",
    "cancelled before the start", "cancelled between units", "a before-submit sink completing past its token",
    "a workspace too small", "the report's cap", "a failed write", "a failed allocation", "the conflation flag",
    "caller memory refused", "an approximation", "a tie", "a deferred completion", "an invalid request",
    "an unsupported target"};

static unsigned long outcomes[OUT_COUNT];

int gshscene_fuzz_unreached(void);

int gshscene_fuzz_unreached(void) {
  int missing = 0;
  for (int i = 0; i < OUT_COUNT; i++) {
    if (outcomes[i] == 0) {
      fprintf(stderr, "fuzz_render: the corpus never reached: %s\n", outcome_names[i]);
      missing++;
    }
  }
  return missing;
}

/* --- a counting allocator that can fail one call -------------------------------------------------- */

typedef struct Counter {
  long live;
  long calls;
  long fail_at; /* -1 never */
} Counter;

static int fails(Counter * c) {
  return c->fail_at >= 0 && c->calls++ == c->fail_at;
}

static void * c_malloc(void * ctx, size_t n) {
  Counter * c = ctx;
  if (fails(c)) {
    return NULL;
  }
  void * p = malloc(n ? n : 1);
  if (p) {
    c->live++;
  }
  return p;
}

static void * c_calloc(void * ctx, size_t k, size_t n) {
  Counter * c = ctx;
  if (fails(c)) {
    return NULL;
  }
  void * p = calloc(k ? k : 1, n ? n : 1);
  if (p) {
    c->live++;
  }
  return p;
}

static void * c_realloc(void * ctx, void * old, size_t n) {
  Counter * c = ctx;
  if (fails(c)) {
    return NULL;
  }
  void * p = realloc(old, n ? n : 1);
  if (p && !old) {
    c->live++;
  }
  return p;
}

static void c_free(void * ctx, void * p) {
  if (p) {
    ((Counter *)ctx)->live--;
  }
  free(p);
}

/* --- the input ---------------------------------------------------------------------------------------- */

typedef struct Input {
  const uint8_t * data;
  size_t size;
  size_t at;
} Input;

static uint8_t next(Input * in) {
  return in->at < in->size ? in->data[in->at++] : 0;
}

/* --- the stream target ---------------------------------------------------------------------------------- */

typedef struct Stream {
  long writes;
  long fail_at; /* -1 never */
  uint64_t hash;
} Stream;

static GSHSCENE_Result stream_write(void * ctx, const void * bytes, size_t count) {
  Stream * s = ctx;
  if (s->fail_at >= 0 && s->writes == s->fail_at) {
    s->writes++;
    return GSHSCENE_ERR_IO;
  }
  s->writes++;
  const unsigned char * b = bytes;
  for (size_t i = 0; i < count; i++) {
    s->hash = (s->hash ^ b[i]) * UINT64_C(0x100000001b3);
  }
  return GSHSCENE_OK;
}

/* --- the progress callback: set the token at unit k --------------------------------------------------------- */

typedef struct Progress {
  GSHSCENE_Cancel * cancel;
  size_t at; /* 0: never */
  size_t last;
} Progress;

static void on_progress(void * ctx, const GSHSCENE_Job * job, size_t units) {
  (void)job;
  Progress * p = ctx;
  CHECK(units == p->last + 1); /* one call per unit, in order */
  p->last = units;
  if (units == p->at) {
    gshscene_cancel_set(p->cancel);
  }
}

static int completions;

static void on_complete(void * ctx, GSHSCENE_Job * job, GSHSCENE_Result result) {
  (void)ctx;
  (void)job;
  (void)result;
  completions++;
}

/* --- building the scene ---------------------------------------------------------------------------------- */

#define MAX_HELD 256
#define PICTURES 3
#define OPS_MAX 160
#define SIDE 48

typedef struct Scene {
  GSHSCENE_Store * store;
  const GSHSCENE_Resource * held[MAX_HELD];
  size_t held_count;
  const GSHSCENE_Resource * pictures[PICTURES];
  size_t picture_count;
} Scene;

static const GSHSCENE_Resource * keep(Scene * s, const GSHSCENE_Resource * r) {
  if (r && s->held_count < MAX_HELD) {
    s->held[s->held_count++] = r;
    return r;
  }
  gshscene_resource_release(r);
  return NULL;
}

static const GSHSCENE_Resource * square(Scene * s, double x, double y, double size) {
  GSHGEOM_Path * p = NULL;
  if (gshgeom_path_create(NULL, NULL, 2, &p) != GSHGEOM_OK) {
    return NULL;
  }
  gshgeom_path_move_to(p, (GSHGEOM_Point){x, y, 0});
  gshgeom_path_line_to(p, (GSHGEOM_Point){x + size, y, 0});
  gshgeom_path_quadratic_to(p, (GSHGEOM_Point){x + size + 1, y + size / 2, 0}, (GSHGEOM_Point){x + size, y + size, 0});
  gshgeom_path_line_to(p, (GSHGEOM_Point){x, y + size, 0});
  gshgeom_path_close(p);
  const GSHSCENE_Resource * r = NULL;
  gshscene_store_intern_path(s->store, p, &r);
  gshgeom_path_destroy(p);
  return keep(s, r);
}

/**
 * The faces a run from a font names (story 14a): unbound, or bound to one of two test providers (a 2048-unit face
 * with a program, a 1000-unit one without), with the fast tier or without it, at one of three instances.  The
 * providers live for the process; their counters are not read.
 */
static GSHSCENE_Test_Provider provider_program = {2048u, GSHSCENE_FACE_HINTING, 0, 0, 0, {0}, {0}, {0}, {0}, {0}, {0}, {0}};
static GSHSCENE_Test_Provider provider_plain = {1000u, 0, 0, 0, 0, {0}, {0}, {0}, {0}, {0}, {0}, {0}};

static const GSHSCENE_Resource * face_from(Scene * s, uint8_t a) {
  int16_t coord = (int16_t)((a % 3) * 4096);
  GSHSCENE_Face_Desc desc;
  memset(&desc, 0, sizeof desc);
  desc.font_key = 0x77u + (a & 3u);
  desc.coord_count = 1;
  desc.coords = &coord;
  switch (a & 3u) {
    case 0:
      break; /* unbound */
    case 1:
      desc.provider = gshscene_tp_ops(1);
      desc.ctx = &provider_program;
      break;
    case 2:
      desc.provider = gshscene_tp_ops(0);
      desc.ctx = &provider_program;
      break;
    default:
      desc.provider = gshscene_tp_ops(1);
      desc.ctx = &provider_plain;
      break;
  }
  const GSHSCENE_Resource * face = NULL;
  gshscene_store_intern_face(s->store, &desc, &face);
  return keep(s, face);
}

static const GSHSCENE_Resource * run_from(Scene * s, int font, uint8_t a) {
  GSHSCENE_Glyph glyphs[2];
  memset(glyphs, 0, sizeof glyphs);
  GSHSCENE_Glyph_Run run;
  memset(&run, 0, sizeof run);
  glyphs[0].x = (double)(a % 20);
  glyphs[0].y = (double)(a % 30);
  glyphs[1].x = glyphs[0].x + 5;
  glyphs[1].y = glyphs[0].y;
  run.glyph_count = 2;
  run.glyphs = glyphs;
  if (font) {
    run.source = GSHSCENE_GLYPHS_FROM_FONT;
    run.face = face_from(s, a);
    if (!run.face) {
      return NULL;
    }
    run.size = 1.0 + (a % 7);
    run.hint_mode = (uint32_t)((a / 4u) % 3u);
    glyphs[0].id = (uint32_t)(a % 9u);
    glyphs[1].id = (uint32_t)((a / 9u) % 9u);
  } else {
    run.source = GSHSCENE_GLYPHS_FROM_OUTLINES;
    glyphs[0].outline = square(s, 0, 0, 2 + a % 5);
    glyphs[1].outline = square(s, 1, 1, 3);
    if (!glyphs[0].outline || !glyphs[1].outline) {
      return NULL;
    }
  }
  const GSHSCENE_Resource * r = NULL;
  gshscene_store_intern_glyph_run(s->store, &run, &r);
  return keep(s, r);
}

/** Build a list from the ops; balanced and closed; NULL when the input built nothing usable. */
static GSHSCENE_List * build(Scene * s, Input * in, int * ties) {
  GSHSCENE_List * list = NULL;
  if (gshscene_list_create(NULL, NULL, s->store, &list) != GSHSCENE_OK) {
    return NULL;
  }
  uint8_t stack[64];
  size_t depth = 0;
  size_t ops = 1 + next(in) % OPS_MAX;
  if (ops % 3 != 0) {
    /* N14: two entries, so that a palette fill of entry 2 is missing and refused by the plan. */
    static const uint8_t entries[8] = {255, 0, 0, 255, 0, 64, 255, 128};
    const GSHSCENE_Resource * palette = NULL;
    if (gshscene_store_intern_palette_rgba8(s->store, 2, entries, &palette) == GSHSCENE_OK) {
      gshscene_list_set_palette(list, keep(s, palette));
    }
  }
  for (size_t i = 0; i < ops && in->at < in->size; i++) {
    uint8_t op = next(in) % 10;
    uint8_t a = next(in);
    uint8_t b = next(in);
    uint8_t c = next(in);
    GSHSCENE_Paint paint;
    memset(&paint, 0, sizeof paint);
    paint.kind = GSHSCENE_PAINT_SOLID;
    paint.rgba[0] = (a & 1) ? 1.0 : 0.25;
    paint.rgba[3] = (b & 1) ? 1.0 : 0.5;
    int32_t layer = (int32_t)(c % 3);
    double d = (double)((c >> 2) % 3);
    switch (op) {
      case 0:
      case 9: {
        if (op == 9) {
          memset(&paint, 0, sizeof paint);
          paint.kind = (a & 2) ? GSHSCENE_PAINT_PALETTE : GSHSCENE_PAINT_FOREGROUND;
          paint.entry = (a & 2) ? (uint32_t)(b % 3) : 0;
          paint.rgba[3] = 0.75;
        }
        const GSHSCENE_Resource * r = square(s, a % SIDE, b % SIDE, 1 + c % 20);
        if (r) {
          gshscene_list_fill(list, r, NULL, (GSHGEOM_Fill_Rule)(a & 1), &paint, layer, d);
        }
        break;
      }
      case 1: {
        const GSHSCENE_Resource * r = square(s, a % SIDE, b % SIDE, 4 + c % 40);
        if (depth < sizeof stack && r && gshscene_list_clip_push(list, r, NULL, GSHGEOM_FILL_NONZERO, layer, d) == GSHSCENE_OK) {
          stack[depth++] = 1;
        }
        break;
      }
      case 2:
        if (depth && stack[depth - 1] == 1 && gshscene_list_clip_pop(list) == GSHSCENE_OK) {
          depth--;
        }
        break;
      case 3: {
        GSHSCENE_Group g;
        memset(&g, 0, sizeof g);
        g.opacity = (a % 3) / 2.0;
        g.composite = (uint32_t)(b % GSHSCENE_COMPOSITE_COUNT);
        g.ordering = (uint32_t)(c % 3);
        if (a % 4 == 1) {
          g.blend_space.primaries = GCOL_PRIMARIES_SRGB;
          g.blend_space.transfer = GCOL_TRANSFER_LINEAR;
        } else if (a % 4 == 2) {
          g.blend_space.primaries = GCOL_PRIMARIES_SRGB;
          g.blend_space.transfer = GCOL_TRANSFER_SRGB;
        }
        if (depth < sizeof stack && gshscene_list_group_push(list, &g, layer, d) == GSHSCENE_OK) {
          stack[depth++] = 2;
        }
        break;
      }
      case 4:
        if (depth && stack[depth - 1] == 2 && gshscene_list_group_pop(list) == GSHSCENE_OK) {
          depth--;
        }
        break;
      case 5:
        if (s->picture_count) {
          GSHGEOM_Map move;
          gshgeom_map_translate(2, (double)(a % 30), (double)(b % 30), 0.0, &move);
          gshscene_list_instance(list, s->pictures[c % s->picture_count], &move, layer, d);
        }
        break;
      case 6:
      case 7: {
        const GSHSCENE_Resource * r = run_from(s, op == 6, a);
        if (r) {
          gshscene_list_fill(list, r, NULL, GSHGEOM_FILL_NONZERO, &paint, layer, d);
        }
        break;
      }
      case 8:
        if (s->picture_count < PICTURES) {
          while (depth) {
            if (stack[--depth] == 1) {
              gshscene_list_clip_pop(list);
            } else {
              gshscene_list_group_pop(list);
            }
          }
          /* A picture has no palette of its own (N14): its palette fills resolve against the list that draws it. */
          const GSHSCENE_Resource * palette = gshscene_list_palette(list); /* held by the scene (keep) */
          gshscene_list_set_palette(list, NULL);
          const GSHSCENE_Resource * picture = NULL;
          if (gshscene_list_close(list) == GSHSCENE_OK &&
              gshscene_store_intern_picture(s->store, list, &picture) == GSHSCENE_OK) {
            if (keep(s, picture)) {
              s->pictures[s->picture_count++] = picture;
            }
          }
          gshscene_list_reset(list);
          gshscene_list_set_palette(list, palette);
        }
        break;
    }
  }
  while (depth) {
    if (stack[--depth] == 1) {
      gshscene_list_clip_pop(list);
    } else {
      gshscene_list_group_pop(list);
    }
  }
  if (gshscene_list_close(list) != GSHSCENE_OK) {
    gshscene_list_destroy(list);
    return NULL;
  }
  *ties = gshscene_list_tie_count(list) != 0;
  return list;
}

/* --- the checks after a render --------------------------------------------------------------------- */

static int chain_less_eq(const GSHSCENE_Report_Entry * a, const GSHSCENE_Report_Entry * b) {
  size_t n = a->chain_count < b->chain_count ? a->chain_count : b->chain_count;
  for (size_t i = 0; i < n; i++) {
    if (a->chain[i] != b->chain[i]) {
      return a->chain[i] < b->chain[i] ? 1 : 0;
    }
  }
  if (a->chain_count != b->chain_count) {
    return a->chain_count < b->chain_count;
  }
  if (a->entry_class != b->entry_class) {
    return a->entry_class < b->entry_class;
  }
  return a->reason <= b->reason;
}

static void check_report(const GSHSCENE_Report * report, size_t cap, GSHSCENE_Result result) {
  GSHSCENE_Report_Summary sum;
  CHECK(gshscene_report_summary(report, &sum) == GSHSCENE_OK);
  CHECK(sum.result == result);
  CHECK(sum.failure.result == result);
  CHECK(sum.failure.reason != NULL);
  size_t count = gshscene_report_count(report);
  CHECK(count <= cap);
  CHECK(sum.counted >= count);
  CHECK((sum.counted > count) == (sum.overflowed != 0));
  CHECK(!sum.overflowed || count == cap);
  size_t by[GSHSCENE_ENTRY_CLASS_COUNT] = {0, 0, 0, 0, 0};
  GSHSCENE_Report_Entry prev;
  memset(&prev, 0, sizeof prev);
  for (size_t i = 0; i < count; i++) {
    GSHSCENE_Report_Entry e;
    CHECK(gshscene_report_entry(report, i, &e) == GSHSCENE_OK);
    CHECK(e.entry_class >= GSHSCENE_ENTRY_REFUSED && e.entry_class < GSHSCENE_ENTRY_CLASS_COUNT);
    CHECK(e.reason >= GSHSCENE_REASON_ITEM_KIND && e.reason < GSHSCENE_REASON_COUNT);
    CHECK((e.chain_count == 0) == (e.chain == NULL));
    by[e.entry_class]++;
    if (i) {
      CHECK(chain_less_eq(&prev, &e));
    }
    prev = e;
  }
  size_t total = 0;
  for (int c = GSHSCENE_ENTRY_REFUSED; c < GSHSCENE_ENTRY_CLASS_COUNT; c++) {
    CHECK(sum.overflowed || sum.by_class[c] == by[c]);
    total += sum.by_class[c];
  }
  CHECK(total == sum.counted);
  CHECK(sum.units_done <= sum.units_processed);
  CHECK(sum.units_processed <= sum.units_total);
  if (sum.overflowed && result == GSHSCENE_ERR_LIMIT) {
    outcomes[OUT_LIMIT_REPORT]++;
  }
  if (sum.by_class[GSHSCENE_ENTRY_APPROXIMATED]) {
    outcomes[OUT_APPROXIMATED]++;
  }
  if (sum.by_class[GSHSCENE_ENTRY_RESOLVED_AMBIGUITY]) {
    outcomes[OUT_TIES]++;
  }
}

static uint64_t raster_hash(const GIMG_Raster * r) {
  uint64_t h = UINT64_C(0xcbf29ce484222325);
  const unsigned char * px = gimg_raster_pixels_const(r);
  for (uint32_t y = 0; y < gimg_raster_height(r); y++) {
    for (size_t i = 0; i < (size_t)gimg_raster_width(r) * 4u; i++) {
      h = (h ^ px[y * gimg_raster_stride_bytes(r) + i]) * UINT64_C(0x100000001b3);
    }
  }
  return h;
}

/* --- the entry point ---------------------------------------------------------------------------------- */

int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
  Input in = {data, size, 0};
  uint8_t h_caps = next(&in);
  uint8_t h_request = next(&in);
  uint8_t h_cancel = next(&in);
  uint8_t h_limits = next(&in);
  uint8_t h_write = next(&in);
  uint8_t h_alloc = next(&in);
  uint8_t h_step = next(&in);

  Counter counter = {0, 0, -1};
  GSHSCENE_Allocator alloc = {&counter, c_malloc, c_calloc, c_realloc, c_free};

  Scene scene;
  memset(&scene, 0, sizeof scene);
  if (gshscene_store_create(NULL, NULL, &scene.store) != GSHSCENE_OK) {
    return 0;
  }
  int ties = 0;
  GSHSCENE_List * list = build(&scene, &in, &ties);

  /* The sink. */
  GSHSCENE_Dump_Sink_Config config;
  gshscene_dump_sink_config_default(&config);
  static const double extents[6] = {0.0, 4.0, 7.5, 12.0, 30.0, 100.0};
  config.max_extent = extents[h_limits % 6];
  config.step_units = h_step % 5;
  GSHSCENE_Sink_Caps * both[2] = {&config.raster_caps, &config.stream_caps};
  for (int i = 0; i < 2; i++) {
    GSHSCENE_Sink_Caps * c = both[i];
    if (h_caps & 1) {
      c->flags &= ~GSHSCENE_CAP_GLYPHS_FROM_FONT;
    }
    if (h_caps & 2) {
      c->composites &= ~((1u << GSHSCENE_COMPOSITE_XOR) | (1u << GSHSCENE_COMPOSITE_CLEAR));
    }
    if (h_caps & 4) {
      c->flags &= ~GSHSCENE_CAP_CLIP_AA;
    }
    if (h_caps & 8) {
      c->flags &= ~GSHSCENE_CAP_BLEND_SPACES;
    }
    if (h_caps & 16) {
      c->flags &= ~GSHSCENE_CAP_CAN_BLOCK;
    }
    if (h_caps & 32) {
      c->cancel_unit = GSHSCENE_CANCEL_BEFORE_SUBMIT;
      c->cancel_work_bound = UINT64_MAX;
    }
    if (h_caps & 64) {
      c->flags &= ~GSHSCENE_CAP_CALLER_MEMORY;
    }
    if (h_caps & 128) {
      c->flags |= GSHSCENE_CAP_THREAD_AFFINE;
    }
  }
  if (h_caps & 16) {
    config.completion = GSHSCENE_DUMP_SINK_DEFERRED;
  }
  GSHSCENE_Sink * sink = NULL;
  CHECK(gshscene_dump_sink_create(&alloc, NULL, &config, &sink) == GSHSCENE_OK);

  /* The report. */
  static const size_t caps_table[4] = {0, 1, 2, 5};
  GSHSCENE_Limits limits;
  memset(&limits, 0, sizeof limits);
  limits.max_report_entries = caps_table[(h_limits >> 3) % 4];
  size_t cap = limits.max_report_entries ? limits.max_report_entries : GSHSCENE_LIMITS_DEFAULT_MAX_REPORT_ENTRIES;
  GSHSCENE_Report * report = NULL;
  CHECK(gshscene_report_create(&alloc, &limits, &report) == GSHSCENE_OK);

  /* The target. */
  GIMG_Raster * raster = NULL;
  CHECK(gimg_raster_create(SIDE, SIDE, &GIMG_PIXEL_RGBA8, GIMG_RASTER_OWNED, NULL, 0, &raster) == GIMG_OK);
  memset(gimg_raster_pixels(raster), 0x5a, gimg_raster_stride_bytes(raster) * SIDE);
  Stream stream = {0, h_write ? (long)h_write - 1 : -1, 0};
  GSHSCENE_Writer writer = {stream_write, &stream};
  GSHSCENE_Target target;
  memset(&target, 0, sizeof target);
  GSHSCENE_Rect damage = {(int64_t)(h_step % 20), (int64_t)(h_write % 20), 20 + (int64_t)(h_alloc % 40), 30};
  int stream_target = (h_request & 4) != 0;
  if (stream_target) {
    target.kind = GSHSCENE_TARGET_STREAM;
    target.surface = &writer;
    target.unit = 1 + (h_step % 4);
  } else {
    target.kind = GSHSCENE_TARGET_RASTER;
    target.surface = raster;
    if (h_request & 64) {
      target.damage = &damage;
    }
    if (h_request & 128) {
      target.extent = (GSHSCENE_Rect){4, 4, 40, 44};
    }
  }
  if (h_request & 8) {
    target.flags = GSHSCENE_TARGET_SEAMLESS;
  }
  if ((h_cancel & 0xf0) == 0xf0 && h_cancel != 0xff) {
    target.kind = GSHSCENE_TARGET_GPU_TEXTURE; /* reserved: unsupported */
  }

  GSHSCENE_Cancel cancel;
  gshscene_cancel_init(&cancel);
  Progress progress = {&cancel, 0, 0};
  if (h_cancel == 0xff) {
    gshscene_cancel_set(&cancel);
  } else if (h_cancel && h_cancel < 0xe0) {
    progress.at = h_cancel;
  }
  GSHSCENE_Render_Request request;
  memset(&request, 0, sizeof request);
  request.list = (h_cancel & 0xf0) == 0xe0 ? NULL : list; /* no list: invalid */
  request.target = &target;
  request.report = report;
  request.cancel = &cancel;
  request.progress = on_progress;
  request.progress_ctx = &progress;
  request.on_complete = on_complete;
  if (h_request & 1) {
    request.flags |= GSHSCENE_RENDER_STRICT;
  }
  unsigned char * workspace = NULL;
  size_t need = 0;
  if (h_request & 2) {
    request.flags |= GSHSCENE_RENDER_CALLER_MEMORY;
    if (list && gshscene_sink_workspace_bytes(sink, &request, &need) == GSHSCENE_OK) {
      size_t mode = (h_request >> 4) & 3;
      size_t bytes = mode == 1 ? need - 1 : (mode == 2 ? need + 7 : need);
      workspace = malloc(bytes + 1);
      request.workspace = workspace + 1;
      request.workspace_bytes = bytes;
    }
  }
  uint64_t before = raster_hash(raster);
  counter.calls = 0;
  counter.fail_at = h_alloc ? (long)h_alloc - 1 : -1;
  completions = 0;

  GSHSCENE_Job job;
  memset(&job, 0, sizeof job);
  GSHSCENE_Result begun = gshscene_sink_begin(sink, &request, &job);
  GSHSCENE_Job_Status st;
  CHECK(gshscene_job_poll(&job, &st) == GSHSCENE_OK);
  GSHSCENE_Result result = begun;
  if (st.state == GSHSCENE_JOB_IDLE) {
    CHECK(begun != GSHSCENE_OK);
    CHECK(completions == 0);
    CHECK(raster_hash(raster) == before);
    if (begun == GSHSCENE_ERR_INVALID) {
      outcomes[OUT_INVALID]++;
    } else if (begun == GSHSCENE_ERR_UNSUPPORTED) {
      outcomes[OUT_UNSUPPORTED]++;
    }
  } else {
    int turns = 0;
    while (st.state == GSHSCENE_JOB_RUNNING) {
      CHECK(gshscene_dump_sink_step(sink, NULL) == GSHSCENE_OK);
      CHECK(gshscene_job_poll(&job, &st) == GSHSCENE_OK);
      CHECK(++turns < 100000);
      outcomes[OUT_DEFERRED] += st.state == GSHSCENE_JOB_DONE;
    }
    result = st.result;
    CHECK(completions == 1);
    GSHSCENE_Sink_Caps caps;
    CHECK(gshscene_sink_caps(sink, &target, &caps) == GSHSCENE_OK);
    counter.fail_at = -1;
    check_report(report, cap, result);
    GSHSCENE_Report_Summary sum;
    gshscene_report_summary(report, &sum);
    CHECK(sum.work_max <= caps.cancel_work_bound);
    CHECK(st.units_processed == sum.units_processed && st.units_done == sum.units_done);
    int untouched = 0;
    switch (result) {
      case GSHSCENE_OK:
        outcomes[OUT_OK]++;
        CHECK(sum.units_processed == sum.units_total);
        CHECK(!(request.flags & GSHSCENE_RENDER_STRICT) || sum.by_class[GSHSCENE_ENTRY_REFUSED] == 0);
        if (progress.at && progress.at <= sum.units_total) {
          CHECK(caps.cancel_unit == GSHSCENE_CANCEL_BEFORE_SUBMIT);
          outcomes[OUT_SUBMITTED_PAST_CANCEL]++;
        }
        break;
      case GSHSCENE_ERR_REFUSED:
        CHECK(sum.by_class[GSHSCENE_ENTRY_REFUSED] >= 1);
        if (target.flags & GSHSCENE_TARGET_SEAMLESS) {
          outcomes[OUT_SEAMLESS]++;
          untouched = 1;
        } else if ((request.flags & GSHSCENE_RENDER_CALLER_MEMORY) && !(caps.flags & GSHSCENE_CAP_CALLER_MEMORY)) {
          outcomes[OUT_CALLER_MEMORY_REFUSED]++;
          untouched = 1;
        } else {
          CHECK(request.flags & GSHSCENE_RENDER_STRICT);
          if (sum.failure.chain_count) {
            outcomes[OUT_STRICT_DRAWING]++;
          } else {
            outcomes[OUT_STRICT_PLANNED]++;
            CHECK(sum.units_processed == 0);
            untouched = 1;
          }
        }
        break;
      case GSHSCENE_ERR_CANCELLED:
        if (sum.units_processed == 0 && h_cancel == 0xff) {
          outcomes[OUT_CANCEL_START]++;
          untouched = 1;
        } else {
          CHECK(caps.cancel_unit == GSHSCENE_CANCEL_BETWEEN_UNITS);
          CHECK(sum.units_processed == progress.at);
          outcomes[OUT_CANCEL_MID]++;
        }
        break;
      case GSHSCENE_ERR_LIMIT:
        if (sum.overflowed) {
          break;
        }
        CHECK(request.flags & GSHSCENE_RENDER_CALLER_MEMORY);
        CHECK(request.workspace_bytes < sum.workspace_needed);
        outcomes[OUT_LIMIT_WORKSPACE]++;
        untouched = 1;
        break;
      case GSHSCENE_ERR_IO:
        CHECK(stream_target);
        outcomes[OUT_IO]++;
        break;
      case GSHSCENE_ERR_OOM:
        CHECK(counter.fail_at >= 0 || h_alloc);
        outcomes[OUT_OOM]++;
        break;
      default:
        CHECK(!"an unexpected result");
    }
    if (untouched && !stream_target) {
      CHECK(raster_hash(raster) == before);
    }
    if (untouched && stream_target) {
      CHECK(stream.writes == 0);
    }
  }
  /* Outside the damage and the extent, the raster is as it was. */
  if (!stream_target) {
    const unsigned char * px = gimg_raster_pixels_const(raster);
    for (int64_t y = 0; y < SIDE; y++) {
      for (int64_t x = 0; x < SIDE; x++) {
        int inside = 1;
        if (target.damage) {
          inside = inside && x >= damage.x0 && x < damage.x1 && y >= damage.y0 && y < damage.y1;
        }
        if (h_request & 128) {
          inside = inside && x >= 4 && x < 40 && y >= 4 && y < 44;
        }
        if (!inside) {
          const unsigned char * p = px + (size_t)y * gimg_raster_stride_bytes(raster) + (size_t)x * 4u;
          CHECK(p[0] == 0x5a && p[1] == 0x5a && p[2] == 0x5a && p[3] == 0x5a);
        }
      }
    }
  }
  (void)ties;
  counter.fail_at = -1;
  CHECK(gshscene_sink_destroy(sink) == GSHSCENE_OK);
  gshscene_report_destroy(report);
  gimg_raster_destroy(raster);
  free(workspace);
  gshscene_list_destroy(list);
  for (size_t i = 0; i < scene.held_count; i++) {
    gshscene_resource_release(scene.held[i]);
  }
  gshscene_store_destroy(scene.store);
  CHECK(counter.live == 0);
  return 0;
}
