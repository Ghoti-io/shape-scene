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
 * The cross-target probe (CAP-13, CAP-16, CAP-12, AD-17): a fixed set of paths,
 * glyph runs, lists and pictures, interned and recorded through the public API,
 * their hashes, counts and the store's counters folded into one hash in
 * shape-geometry's spelling.  Every target must print the committed value in
 * tests/corpus/scene.hash: x86-64, i686 (SSE), arm64, big-endian and Windows.
 * Since story 11 it also builds a retained scene (nodes, a key map, instances of hidden definitions, a merge of
 * sibling edits, a diff with its damage through a general map) and folds in its version hashes, IDs, emitted lists,
 * pictures, occurrences with their composed maps and the damage rectangles.
 * Since story 13a every list is also rendered through the test sink (dump_sink.h) on a stream in millimetres under a
 * scale and on a 32 by 32 raster, by a sink that draws everything and by a narrow one under strict, and the results,
 * the output hashes (the dump's bytes: every box a double through a composed map), the report hashes and the
 * stamped pixels are folded in.
 * Since story 14a the glyph runs from a font name faces bound to the test provider (tests/glyph_test_provider.h),
 * so the renders go through both routes, and a glyph section folds in each route's decision, the fast tier's device
 * paths and the general route's under the snapped map (which must be the same paths: AD-5's gate, checked here on
 * every target) and test-sink renders at integer, fractional and rotated sizes.
 * Since story 14c a palette section folds in palettes from 8-bit and double entries (`c / 255` is a division every
 * target must round alike), lists and versions with a palette parameter and palette fills (some entries missing,
 * which every sink refuses), the item maps of the float-free design maps (rotations and skews through shape-geometry's
 * rotation, composed by the one composer), their renders and a diff of a palette change.
 * Since color-paint story P3 a blend section folds in lists of groups of every blend mode and of PDF's group fields
 * (the fields spelled after the composite byte), their renders by test sinks that refuse and that record the fields,
 * and a version of such group nodes, its hash and its emitted list.  Every section before it folds what it did.
 * Since color-paint story P1 a gradient section folds in gradients of every kind, extend and flag (geometry and
 * offsets that are not exact in binary, stops of every paint kind in random spaces), their hashes, the stops the one
 * conversion gives at draw time (gshscene_gradient_resolve: `math`'s `gmath_pow` and the gamut matrices, which every
 * target must round alike), what the notice predicate says of each under composed maps, the float-free constructor's
 * gradients (a run's scale is a division), and lists of gradient fills rendered by the test sinks (whose dumps carry
 * the resolved stops and the notices).
 * It is built by `make check-corpus-hash` on the host and by
 * suite/tools/xarch/shape.sh and suite/tools/xwin/shape.sh for the others.
 *
 * What it can see: byte order in a hash (the spelling is written from shifts), a
 * 32-bit `size_t` or `long` where a 64-bit count was assumed, a sign or enum
 * width that depends on the host, and, through the composed maps (each entry of
 * the product is a sum of two products of inexact doubles), x87 excess
 * precision and a fused multiply-add.  The input is integers over 16 and over
 * 1000, from a fixed generator.
 *
 * -DSHAPE_SCENE_PLANT_BIT flips one bit of one result before it is folded in: the
 * planted differing bit that the gate has to catch on the target it is built for.
 */

#include <ghoti.io/shape-scene/shape-scene.h>
#include <ghoti.io/shape-geometry/shape-geometry.h>

#include <ghoti.io/image/raster.h>

#include "../tests/glyph_test_provider.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PATHS 64
#define MAX_PICTURES 16

static GSHGEOM_Hash total;
static unsigned long count;

static uint64_t rng_state = UINT64_C(0x9E3779B97F4A7C15);

/* xorshift64*: the same integers everywhere. */
static uint64_t next(void) {
  rng_state ^= rng_state >> 12;
  rng_state ^= rng_state << 25;
  rng_state ^= rng_state >> 27;
  return rng_state * UINT64_C(2685821657736338717);
}

static unsigned between(unsigned lo, unsigned hi) {
  return lo + (unsigned)(next() % (uint64_t)(hi - lo + 1));
}

/* A coordinate exact in binary: an integer over 16. */
static double coord(void) {
  return (double)((int)between(0, 8000) - 4000) / 16.0;
}

/* Every random draw is its own statement: the order in which the arguments of a call, or the
 * members of an initializer, are evaluated is up to the compiler, and a target's compiler may
 * choose differently. */
static GSHGEOM_Point point(void) {
  GSHGEOM_Point p;
  p.x = coord();
  p.y = coord();
  p.z = coord();
  return p;
}

static void fail(const char * what) {
  fprintf(stderr, "corpus-hash: %s\n", what);
  exit(1);
}

static void feed(uint64_t value) {
#ifdef SHAPE_SCENE_PLANT_BIT
  if (count == 17) {
    value ^= 1u;
  }
#endif
  gshgeom_hash_u64(&total, value);
  count++;
}

static void check(GSHSCENE_Result r, const char * what) {
  if (r != GSHSCENE_OK) {
    fprintf(stderr, "corpus-hash: %s: %s\n", what, gshscene_result_string(r));
    exit(1);
  }
}

static GSHGEOM_Path * random_path(int dim) {
  GSHGEOM_Path * path = NULL;
  if (gshgeom_path_create(NULL, NULL, dim, &path) != GSHGEOM_OK) {
    fail("path create");
  }
  static const double weights[4] = {0.5, 1.0, 2.0, 4.0};
  if (gshgeom_path_move_to(path, point()) != GSHGEOM_OK) {
    fail("move");
  }
  unsigned n = between(1, 12);
  for (unsigned i = 0; i < n; i++) {
    GSHGEOM_Result r = GSHGEOM_OK;
    double w[4];
    for (int k = 0; k < 4; k++) {
      w[k] = weights[next() & 3];
    }
    GSHGEOM_Point a, b, c;
    unsigned verb = between(0, 6);
    a = point();
    b = point();
    c = point();
    switch (verb) {
      case 0:
        r = gshgeom_path_line_to(path, a);
        break;
      case 1:
        r = gshgeom_path_quadratic_to(path, a, b);
        break;
      case 2:
        r = gshgeom_path_cubic_to(path, a, b, c);
        break;
      case 3:
        r = gshgeom_path_rational_quadratic_to(path, a, b, w);
        break;
      case 4:
        r = gshgeom_path_rational_cubic_to(path, a, b, c, w);
        break;
      case 5:
        r = gshgeom_path_close(path);
        break;
      default:
        r = gshgeom_path_move_to(path, a);
        break;
    }
    if (r != GSHGEOM_OK) {
      fail("path verb");
    }
  }
  return path;
}

/* A matrix entry that is not exact in binary: an integer over 1000. */
static double entry(void) {
  return (double)((int)between(0, 2000) - 1000) / 1000.0;
}

/* The composition of two general maps: each entry of the product is a sum of two products of inexact
 * doubles, which an extended-precision register (x87) or a fused multiply-add rounds differently. */
static GSHGEOM_Map random_map(void) {
  double ma[4], mb[4], ta[2], tb[2];
  for (int i = 0; i < 4; i++) {
    ma[i] = entry();
  }
  for (int i = 0; i < 4; i++) {
    mb[i] = entry();
  }
  ta[0] = entry();
  ta[1] = entry();
  tb[0] = entry();
  tb[1] = entry();
  GSHGEOM_Map a, b, composed;
  if (gshgeom_map_from_matrix(2, ma, ta, &a) != GSHGEOM_OK || gshgeom_map_from_matrix(2, mb, tb, &b) != GSHGEOM_OK ||
      gshgeom_map_compose(&a, &b, &composed) != GSHGEOM_OK) {
    fail("map");
  }
  return composed;
}

/* A color space of the table: the target's, and the gamuts with each transfer (story 12). */
static GSHSCENE_Color_Space random_space(void) {
  static const double gammas[3] = {1.8, 2.2, 2.4};
  GSHSCENE_Color_Space s;
  memset(&s, 0, sizeof s);
  unsigned pick = between(0, 5);
  if (pick == 0) {
    return s; /* the target's */
  }
  static const GCOL_Primaries gamuts[4] = {GCOL_PRIMARIES_SRGB, GCOL_PRIMARIES_ADOBE_RGB, GCOL_PRIMARIES_DISPLAY_P3,
      GCOL_PRIMARIES_BT2020};
  s.primaries = gamuts[between(0, 3)];
  unsigned transfer = between(0, 2);
  if (transfer == 0) {
    s.transfer = GCOL_TRANSFER_LINEAR;
  } else if (transfer == 1) {
    s.transfer = GCOL_TRANSFER_SRGB;
  } else {
    s.transfer = GCOL_TRANSFER_GAMMA;
    s.gamma = gammas[between(0, 2)];
  }
  return s;
}

static GSHSCENE_Paint random_paint(void) {
  static const double levels[6] = {0.0, 0.25, 0.5, 1.0, 2.0, -0.5};
  GSHSCENE_Paint p;
  memset(&p, 0, sizeof p);
  p.kind = GSHSCENE_PAINT_SOLID;
  for (int i = 0; i < 3; i++) {
    p.rgba[i] = levels[between(0, 5)];
  }
  p.rgba[3] = levels[between(0, 3)];
  p.space = random_space();
  if (between(0, 9) == 0) { /* currentColor: the alpha multiplier is the paint's, the color the version's */
    p.kind = GSHSCENE_PAINT_FOREGROUND;
    p.rgba[0] = 0.0;
    p.rgba[1] = 0.0;
    p.rgba[2] = 0.0;
    memset(&p.space, 0, sizeof p.space);
  }
  return p;
}

static GSHSCENE_Color random_foreground(void) {
  static const double levels[5] = {0.0, 0.25, 0.5, 1.0, 2.0};
  GSHSCENE_Color c;
  memset(&c, 0, sizeof c);
  for (int i = 0; i < 3; i++) {
    c.rgba[i] = levels[between(0, 4)];
  }
  c.rgba[3] = levels[between(0, 3)];
  c.space = random_space();
  return c;
}

/* A depth from a small table, so that ties are common. */
static double random_depth(void) {
  return (double)((int)between(0, 4)) * 0.5 - 1.0;
}

static GSHSCENE_Group random_group_props(void) {
  static const double opacity[4] = {1.0, 1.0, 0.5, 0.25};
  GSHSCENE_Group g;
  memset(&g, 0, sizeof g);
  g.opacity = opacity[between(0, 3)];
  g.blend_space = random_space();
  g.ordering = between(0, 2);
  g.composite = between(0, (unsigned)GSHSCENE_COMPOSITE_PLUS); /* the Porter-Duff draws, pinned: the blend section draws the modes */
  return g;
}

static void feed_color(const GSHSCENE_Color * c) {
  for (int i = 0; i < 4; i++) {
    feed(gshgeom_f64_bits(c->rgba[i]));
  }
  feed((uint64_t)c->space.primaries);
  feed((uint64_t)c->space.transfer);
  feed(gshgeom_f64_bits(c->space.gamma));
}

static void feed_ties(const GSHSCENE_List * list) {
  size_t n = gshscene_list_tie_count(list);
  feed((uint64_t)n);
  for (size_t i = 0; i < n; i++) {
    const GSHSCENE_Tie * tie = NULL;
    check(gshscene_list_tie(list, i, &tie), "tie");
    feed((uint64_t)tie->first);
    feed((uint64_t)tie->second);
    feed(gshgeom_f64_bits(tie->depth));
    feed((uint64_t)tie->reason);
  }
}


/* --- the retained scene (story 11) ------------------------------------------------------------------ */

#define SCENE_PATHS 12
#define SCENE_GROUPS 6
#define SCENE_DEFS 3

typedef struct Tree {
  GSHSCENE_Scene * scene;
  const GSHSCENE_Resource * paths[SCENE_PATHS];
  const GSHSCENE_Resource * runs[2];
  uint64_t groups[SCENE_GROUPS];
  uint64_t defs[SCENE_DEFS];
  uint64_t shapes[64];
  size_t shape_count;
  uint64_t instances[16];
  size_t instance_count;
} Tree;

static void check_id(GSHSCENE_Result r, const char * what) {
  check(r, what);
}

static GSHSCENE_Node random_shape(Tree * t) {
  GSHSCENE_Node n;
  gshscene_node_init(GSHSCENE_NODE_SHAPE, &n);
  unsigned pick_path = between(0, SCENE_PATHS - 1);
  unsigned pick_run = between(0, 3);
  n.resource = (pick_run == 0) ? t->runs[next() & 1] : t->paths[pick_path];
  n.map = random_map();
  n.paint = random_paint();
  n.fill_rule = (next() & 1) ? GSHGEOM_FILL_EVEN_ODD : GSHGEOM_FILL_NONZERO;
  n.layer = (int32_t)between(0, 6) - 3;
  n.depth = random_depth();
  return n;
}

static GSHSCENE_Node random_group(Tree * t) {
  GSHSCENE_Node n;
  gshscene_node_init(GSHSCENE_NODE_GROUP, &n);
  GSHSCENE_Group props = random_group_props();
  n.opacity = props.opacity;
  n.blend_space = props.blend_space;
  n.ordering = props.ordering;
  n.composite = props.composite;
  n.map = random_map();
  n.layer = (int32_t)between(0, 4);
  n.depth = random_depth();
  if (between(0, 2) == 0) {
    n.clip = t->paths[between(0, SCENE_PATHS - 1)];
    n.clip_map = random_map();
    n.clip_rule = (next() & 1) ? GSHGEOM_FILL_EVEN_ODD : GSHGEOM_FILL_NONZERO;
  }
  return n;
}

static uint64_t add_node(GSHSCENE_Edit * e, uint64_t parent, const GSHSCENE_Node * n, const GSHSCENE_Key * key) {
  uint64_t id = 0;
  check_id(gshscene_edit_add(e, parent, GSHSCENE_APPEND, n, key, &id), "edit add");
  return id;
}

static void feed_version(const GSHSCENE_Version * v) {
  feed(gshscene_version_hash(v));
  feed((uint64_t)gshscene_version_node_count(v));
  feed(gshscene_version_serial(v));
  feed((uint64_t)gshscene_version_key_count(v));
  const GSHSCENE_List * list = NULL;
  check(gshscene_version_list(v, &list), "version list");
  feed(gshscene_list_hash(list));
  feed((uint64_t)gshscene_list_count(list));
  feed((uint64_t)gshscene_list_expansion(list));
  feed_ties(list);
  GSHSCENE_Color fg;
  check(gshscene_version_foreground(v, &fg), "version foreground");
  feed_color(&fg);
}

typedef struct Fold {
  uint64_t occurrences;
} Fold;

static int fold_visit(void * ctx, const GSHSCENE_Visit * visit) {
  Fold * f = ctx;
  f->occurrences++;
  feed(gshscene_instance_path_key(visit->path, visit->path_count));
  feed(gshgeom_map_hash(&visit->map));
  return 0;
}

static void feed_diff(const GSHSCENE_Version * a, const GSHSCENE_Version * b, const GSHGEOM_Map * map) {
  GSHSCENE_Diff * d = NULL;
  check(gshscene_diff(a, b, map, NULL, &d), "diff");
  feed((uint64_t)gshscene_diff_count(d));
  for (size_t i = 0; i < gshscene_diff_count(d); i++) {
    uint64_t id;
    GSHSCENE_Diff_Kind kind;
    check(gshscene_diff_get(d, i, &id, &kind), "diff get");
    feed(id);
    feed((uint64_t)kind);
  }
  feed((uint64_t)gshscene_diff_foreground_changed(d));
  feed((uint64_t)gshscene_diff_palette_changed(d));
  GSHSCENE_Damage damage;
  check(gshscene_diff_damage(d, &damage), "damage");
  feed((uint64_t)damage.unbounded);
  feed((uint64_t)damage.empty);
  feed((uint64_t)damage.rect.x0);
  feed((uint64_t)damage.rect.y0);
  feed((uint64_t)damage.rect.x1);
  feed((uint64_t)damage.rect.y1);
  gshscene_diff_destroy(d);
}

/* --- the test sink (story 13a) ------------------------------------------------------------------- */

static GSHSCENE_Result count_write(void * ctx, const void * bytes, size_t n) {
  (void)bytes;
  *(uint64_t *)ctx += n;
  return GSHSCENE_OK;
}

static GSHSCENE_Result feed_job(GSHSCENE_Sink * sink, const GSHSCENE_Render_Request * request) {
  GSHSCENE_Job job;
  memset(&job, 0, sizeof job);
  GSHSCENE_Result r = gshscene_sink_begin(sink, request, &job);
  GSHSCENE_Job_Status st;
  check(gshscene_job_poll(&job, &st), "poll");
  if (st.state != GSHSCENE_JOB_DONE) {
    fail("a render of the in-line test sink did not complete in begin");
  }
  uint64_t out = 0;
  check(gshscene_dump_sink_output_hash(&job, &out), "output hash");
  feed((uint64_t)r);
  feed(out);
  feed(gshscene_report_hash(request->report));
  feed((uint64_t)st.units_processed);
  feed((uint64_t)st.units_done);
  return r;
}

static void feed_render(const GSHSCENE_List * list) {
  GSHSCENE_Dump_Sink_Config full;
  gshscene_dump_sink_config_default(&full);
  GSHSCENE_Dump_Sink_Config narrow = full;
  GSHSCENE_Sink_Caps * both[2] = {&narrow.raster_caps, &narrow.stream_caps};
  for (int i = 0; i < 2; i++) {
    both[i]->flags &= ~(GSHSCENE_CAP_CLIP_AA | GSHSCENE_CAP_BLEND_SPACES | GSHSCENE_CAP_GLYPHS_FROM_FONT);
    both[i]->composites &= ~((1u << GSHSCENE_COMPOSITE_XOR) | (1u << GSHSCENE_COMPOSITE_PLUS));
  }
  narrow.max_extent = 2000.0;
  GSHSCENE_Sink * sinks[2] = {NULL, NULL};
  check(gshscene_dump_sink_create(NULL, NULL, &full, &sinks[0]), "test sink");
  check(gshscene_dump_sink_create(NULL, NULL, &narrow, &sinks[1]), "narrow test sink");
  GSHSCENE_Report * report = NULL;
  check(gshscene_report_create(NULL, NULL, &report), "report");
  for (int s = 0; s < 2; s++) {
    uint64_t written = 0;
    GSHSCENE_Writer writer = {count_write, &written};
    GSHSCENE_Target stream;
    memset(&stream, 0, sizeof stream);
    stream.kind = GSHSCENE_TARGET_STREAM;
    stream.surface = &writer;
    stream.unit = GSHSCENE_UNIT_MILLIMETER;
    if (gshgeom_map_scale(2, 0.25, 0.375, 1.0, &stream.map) != GSHGEOM_OK) {
      fail("scale");
    }
    GSHSCENE_Render_Request request;
    memset(&request, 0, sizeof request);
    request.list = list;
    request.target = &stream;
    request.report = report;
    request.flags = s ? GSHSCENE_RENDER_STRICT : 0;
    feed_job(sinks[s], &request);
    feed(written);

    GIMG_Raster * raster = NULL;
    if (gimg_raster_create(32, 32, &GIMG_PIXEL_RGBA8, GIMG_RASTER_OWNED, NULL, 0, &raster) != GIMG_OK) {
      fail("raster");
    }
    GSHSCENE_Target pixels;
    memset(&pixels, 0, sizeof pixels);
    pixels.kind = GSHSCENE_TARGET_RASTER;
    pixels.surface = raster;
    GSHSCENE_Rect damage = {3, 5, 29, 30};
    pixels.damage = s ? &damage : NULL;
    request.target = &pixels;
    feed_job(sinks[s], &request);
    GSHGEOM_Hash h;
    gshgeom_hash_init(&h);
    const unsigned char * px = gimg_raster_pixels_const(raster);
    for (uint32_t y = 0; y < 32; y++) {
      gshgeom_hash_bytes(&h, px + y * gimg_raster_stride_bytes(raster), 32u * 4u);
    }
    feed(gshgeom_hash_value(&h));
    gimg_raster_destroy(raster);
  }
  gshscene_report_destroy(report);
  check(gshscene_sink_destroy(sinks[0]), "destroy");
  check(gshscene_sink_destroy(sinks[1]), "destroy");
}

static const GSHSCENE_Resource * test_face(GSHSCENE_Store * store, unsigned which, int16_t coord);

static void scene_section(void) {
  Tree t;
  memset(&t, 0, sizeof t);
  check(gshscene_scene_create(NULL, NULL, NULL, &t.scene), "scene");
  GSHSCENE_Store * store = gshscene_scene_store(t.scene);
  for (unsigned i = 0; i < SCENE_PATHS; i++) {
    GSHGEOM_Path * path = random_path(2);
    check(gshscene_store_intern_path(store, path, &t.paths[i]), "scene path");
    gshgeom_path_destroy(path);
  }
  {
    GSHSCENE_Glyph_Run run;
    GSHSCENE_Glyph glyphs[4];
    memset(&run, 0, sizeof run);
    const GSHSCENE_Resource * face = test_face(store, 0, 0);
    run.source = GSHSCENE_GLYPHS_FROM_FONT;
    run.face = face;
    run.size = 14.0;
    run.glyph_count = 4;
    for (size_t g = 0; g < 4; g++) {
      glyphs[g].id = (uint32_t)between(0, 3);
      glyphs[g].x = coord();
      glyphs[g].y = coord();
      glyphs[g].outline = NULL;
    }
    run.glyphs = glyphs;
    check(gshscene_store_intern_glyph_run(store, &run, &t.runs[0]), "scene font run");
    gshscene_resource_release(face);
    memset(&run, 0, sizeof run);
    run.source = GSHSCENE_GLYPHS_FROM_OUTLINES;
    run.glyph_count = 3;
    for (size_t g = 0; g < 3; g++) {
      glyphs[g].id = (uint32_t)g;
      glyphs[g].x = coord();
      glyphs[g].y = coord();
      glyphs[g].outline = t.paths[between(0, SCENE_PATHS - 1)];
    }
    run.glyphs = glyphs;
    check(gshscene_store_intern_glyph_run(store, &run, &t.runs[1]), "scene outline run");
  }

  /* Version 1: groups, shapes (some hidden), three hidden definitions, instances of them. */
  GSHSCENE_Edit * e = NULL;
  check(gshscene_scene_open_edit(t.scene, NULL, &e), "open edit");
  for (unsigned g = 0; g < SCENE_GROUPS; g++) {
    GSHSCENE_Node n = random_group(&t);
    GSHSCENE_Key key = {0x53434e45u, g};
    t.groups[g] = add_node(e, GSHSCENE_ROOT_ID, &n, &key);
  }
  for (unsigned d = 0; d < SCENE_DEFS; d++) {
    GSHSCENE_Node n = random_group(&t);
    n.flags = GSHSCENE_NODE_HIDDEN;
    t.defs[d] = add_node(e, GSHSCENE_ROOT_ID, &n, NULL);
    for (unsigned k = 0; k < 3; k++) {
      GSHSCENE_Node s = random_shape(&t);
      add_node(e, t.defs[d], &s, NULL);
    }
  }
  for (unsigned i = 0; i < 40; i++) {
    GSHSCENE_Node n = random_shape(&t);
    if (between(0, 9) == 0) {
      n.flags = GSHSCENE_NODE_HIDDEN;
    }
    unsigned parent = between(0, SCENE_GROUPS - 1);
    t.shapes[t.shape_count++] = add_node(e, t.groups[parent], &n, NULL);
  }
  for (unsigned i = 0; i < 12; i++) {
    GSHSCENE_Node n;
    gshscene_node_init(GSHSCENE_NODE_INSTANCE, &n);
    n.target = t.defs[between(0, SCENE_DEFS - 1)];
    n.map = random_map();
    n.layer = (int32_t)between(0, 4);
    n.depth = random_depth();
    unsigned parent = between(0, SCENE_GROUPS - 1);
    t.instances[t.instance_count++] = add_node(e, t.groups[parent], &n, NULL);
  }
  {
    GSHSCENE_Color fg = random_foreground();
    check(gshscene_edit_set_foreground(e, &fg), "set foreground");
  }
  const GSHSCENE_Version * v1 = NULL;
  check(gshscene_scene_publish(t.scene, e, NULL, &v1), "publish 1");
  gshscene_edit_destroy(e);
  feed_version(v1);
  for (unsigned d = 0; d < SCENE_DEFS; d++) {
    const GSHSCENE_Resource * pic = NULL;
    GSHSCENE_Result r = gshscene_version_picture(v1, t.defs[d], &pic);
    feed(r == GSHSCENE_OK ? gshscene_resource_hash(pic) : 0);
  }

  /* Two sibling edits on version 1 in disjoint groups, merged. */
  GSHSCENE_Edit * ea = NULL;
  GSHSCENE_Edit * eb = NULL;
  check(gshscene_scene_open_edit(t.scene, v1, &ea), "open a");
  check(gshscene_scene_open_edit(t.scene, v1, &eb), "open b");
  for (unsigned i = 0; i < 4; i++) {
    GSHSCENE_Node n = random_shape(&t);
    add_node(ea, t.groups[0], &n, NULL);
    GSHSCENE_Node m = random_shape(&t);
    add_node(eb, t.groups[3], &m, NULL);
  }
  {
    GSHSCENE_Node_Info info;
    GSHSCENE_Node g = random_group(&t);
    check(gshscene_edit_node(ea, t.groups[1], &info), "read group");
    g.flags = info.node.flags;
    check(gshscene_edit_set(ea, t.groups[1], &g), "set group");
    GSHSCENE_Node s = random_shape(&t);
    check(gshscene_edit_set(eb, t.shapes[between(0, 39)], &s), "set shape");
  }
  {
    GSHSCENE_Color fg = random_foreground();
    check(gshscene_edit_set_foreground(eb, &fg), "set foreground b");
  }
  GSHSCENE_Problems * problems = NULL;
  GSHSCENE_Edit * both[2] = {eb, ea};
  const GSHSCENE_Version * v2 = NULL;
  GSHSCENE_Result merged = gshscene_scene_merge(t.scene, both, 2, &problems, &v2);
  feed((uint64_t)merged);
  if (merged == GSHSCENE_ERR_REFUSED) {
    /* The set of a shape of group 1 or 0 can overlap an edit's group: count what is named, and publish what is left. */
    feed((uint64_t)gshscene_problems_count(problems));
    for (size_t i = 0; i < gshscene_problems_count(problems); i++) {
      uint64_t id;
      GSHSCENE_Problem_Reason reason;
      check(gshscene_problems_get(problems, i, &id, &reason), "problem");
      feed(id);
      feed((uint64_t)reason);
    }
    gshscene_problems_destroy(problems);
    check(gshscene_scene_publish(t.scene, ea, NULL, &v2), "publish a alone");
  } else {
    check(merged, "merge");
  }
  gshscene_edit_destroy(ea);
  gshscene_edit_destroy(eb);
  feed_version(v2);

  /* Visit every occurrence of version 2, and diff version 1 against it through a general map. */
  Fold fold = {0};
  if (gshscene_version_visit(v2, fold_visit, &fold) != 0) {
    fail("visit");
  }
  feed(fold.occurrences);
  GSHGEOM_Map device = random_map();
  feed_diff(v1, v2, &device);
  feed_diff(v1, v2, NULL);

  gshscene_version_release(v2);
  gshscene_version_release(v1);
  gshscene_resource_release(t.runs[0]);
  gshscene_resource_release(t.runs[1]);
  for (unsigned i = 0; i < SCENE_PATHS; i++) {
    gshscene_resource_release(t.paths[i]);
  }
  GSHSCENE_Store_Stats stats;
  check(gshscene_store_stats(store, &stats), "scene stats");
  feed(stats.bytes);
  feed(stats.created);
  gshscene_scene_destroy(t.scene);
}

/* --- glyphs (story 14a) -------------------------------------------------------------------------------- */

static GSHSCENE_Test_Provider provider_program = {2048u, GSHSCENE_FACE_HINTING, 0, 0, 0, {0}, {0}, {0}, {0}, {0}, {0}, {0}};
static GSHSCENE_Test_Provider provider_plain = {1000u, 0, 0, 0, 0, {0}, {0}, {0}, {0}, {0}, {0}, {0}};

/** A face of the test provider: @p which 0 (2048 units, a program, fast), 1 (1000, plain, fast), 2 (2048, no fast). */
static const GSHSCENE_Resource * test_face(GSHSCENE_Store * store, unsigned which, int16_t coord) {
  GSHSCENE_Test_Provider * tp = which == 1 ? &provider_plain : &provider_program;
  const GSHSCENE_Resource * face = gshscene_tp_face(store, tp, which != 2, 0x51u + which, coord);
  if (!face) {
    fail("test face");
  }
  return face;
}

/** Every glyph of @p run under @p map: its route, and on the fast tier its path and the general route's under the snapped map. */
static void feed_routes(const GSHSCENE_Resource * run, const GSHGEOM_Map * map) {
  GSHSCENE_Glyph_Route route;
  check(gshscene_glyph_route(run, map, GSHSCENE_TARGET_RASTER, &route), "route");
  feed(route.route);
  feed(route.entry_class);
  feed(route.reason);
  feed(route.ppem);
  feed((uint64_t)(uint32_t)route.scale);
  GSHSCENE_Glyph_Run r;
  check(gshscene_resource_glyph_run(run, &r), "run");
  for (size_t i = 0; i < r.glyph_count; i++) {
    GSHSCENE_Glyph_Note note;
    if (route.route == GSHSCENE_GLYPH_FAST) {
      GSHSCENE_Glyph_Origin origin;
      GSHGEOM_Device_Path * fast = NULL;
      check(gshscene_glyph_origin(run, map, i, &origin), "origin");
      check(gshscene_glyph_device_path(run, &route, i, &origin, NULL, NULL, &fast, &note), "fast tier");
      feed(note.entry_class);
      feed(note.reason);
      feed(note.status);
      feed(gshgeom_device_path_hash(fast));
      GSHGEOM_Path * outline = NULL;
      GSHSCENE_Glyph_Note general_note;
      check(gshscene_glyph_outline(run, i, NULL, NULL, &outline, &general_note), "outline");
      if (fast && outline && r.hint_mode == GSHSCENE_HINT_NONE) {
        GSHGEOM_Map snap;
        GSHGEOM_Device_Path * general = NULL;
        check(gshscene_glyph_snap_map(&route, &origin, &snap), "snap");
        if (gshgeom_device_path_from_model(outline, &snap, NULL, NULL, &general) != GSHGEOM_OK ||
            gshgeom_device_path_hash(general) != gshgeom_device_path_hash(fast)) {
          fail("AD-5: the fast tier with hinting off is not the general route under the snapped map");
        }
        gshgeom_device_path_destroy(general);
      }
      gshgeom_path_destroy(outline);
      gshgeom_device_path_destroy(fast);
    } else {
      GSHGEOM_Map glyph_map;
      GSHGEOM_Path * outline = NULL;
      check(gshscene_glyph_map(run, map, i, &glyph_map), "glyph map");
      check(gshscene_glyph_outline(run, i, NULL, NULL, &outline, &note), "outline");
      feed(note.entry_class);
      feed(note.reason);
      feed(gshgeom_map_hash(&glyph_map));
      feed(gshgeom_path_hash(outline));
      gshgeom_path_destroy(outline);
    }
  }
}

static void glyph_section(void) {
  GSHSCENE_Store * store = NULL;
  check(gshscene_store_create(NULL, NULL, &store), "glyph store");
  static const double sizes[5] = {16.0, 8.0, 13.0, 12.5, 40.0 / 3.0};
  static const int16_t coords[3] = {0, 8192, -16384};
  for (unsigned f = 0; f < 3; f++) {
    for (unsigned c = 0; c < 3; c++) {
      const GSHSCENE_Resource * face = test_face(store, f, coords[c]);
      feed(gshscene_resource_hash(face));
      for (unsigned mode = 0; mode < GSHSCENE_HINT_MODE_COUNT; mode++) {
        GSHSCENE_Glyph glyphs[8];
        for (uint32_t g = 0; g < 8; g++) {
          glyphs[g].id = g;
          glyphs[g].x = (double)(g * 11u) + (double)between(0, 63) / 64.0 - 3.0; /* 26.6 positions, either sign */
          glyphs[g].y = 20.0 + (double)between(0, 127) / 128.0;
          glyphs[g].outline = NULL;
        }
        GSHSCENE_Glyph_Run run;
        memset(&run, 0, sizeof run);
        run.source = GSHSCENE_GLYPHS_FROM_FONT;
        run.face = face;
        run.size = sizes[(f + c + mode) % 5];
        run.hint_mode = mode;
        run.glyph_count = 8;
        run.glyphs = glyphs;
        const GSHSCENE_Resource * r = NULL;
        check(gshscene_store_intern_glyph_run(store, &run, &r), "glyph run");
        feed(gshscene_resource_hash(r));
        /* Grid-aligned (a scale and a translation), rotated, and through a general map. */
        GSHGEOM_Map maps[3];
        GSHGEOM_Map t;
        gshgeom_map_scale(2, 1.0 + (double)(mode % 2), 1.0 + (double)(mode % 2), 1.0, &maps[0]);
        gshgeom_map_translate(2, 0.3125, -1.75, 0.0, &t);
        gshgeom_map_compose(&t, &maps[0], &maps[0]);
        gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, 30.0, &maps[1]);
        maps[2] = random_map();
        for (unsigned m = 0; m < 3; m++) {
          feed_routes(r, &maps[m]);
        }
        /* And rendered through the test sink, in a list under the grid-aligned map and the rotation. */
        GSHSCENE_List * list = NULL;
        check(gshscene_list_create(NULL, NULL, store, &list), "glyph list");
        GSHSCENE_Paint black = random_paint();
        check(gshscene_list_fill(list, r, &maps[0], GSHGEOM_FILL_NONZERO, &black, 0, 0.0), "glyph fill");
        check(gshscene_list_fill(list, r, &maps[1], GSHGEOM_FILL_NONZERO, &black, 0, 0.0), "glyph fill");
        check(gshscene_list_close(list), "glyph close");
        feed(gshscene_list_hash(list));
        feed_render(list);
        gshscene_list_destroy(list);
        gshscene_resource_release(r);
      }
      gshscene_resource_release(face);
    }
  }
  gshscene_store_destroy(store);
}

/* --- palettes and the float-free calls (story 14c) --------------------------------------------------- */

static void palette_section(void) {
  GSHSCENE_Store * store = NULL;
  check(gshscene_store_create(NULL, NULL, &store), "palette store");
  const GSHSCENE_Resource * palettes[6];
  for (unsigned p = 0; p < 6; p++) {
    uint8_t bytes[4u * 12u];
    unsigned n = between(0, 12);
    for (unsigned i = 0; i < 4u * n; i++) {
      bytes[i] = (uint8_t)between(0, 255);
    }
    check(gshscene_store_intern_palette_rgba8(store, n, bytes, &palettes[p]), "rgba8 palette");
    feed(gshscene_resource_hash(palettes[p]));
    feed((uint64_t)gshscene_resource_palette_count(palettes[p]));
    for (size_t i = 0; i < n; i++) {
      GSHSCENE_Color c;
      check(gshscene_resource_palette_color(palettes[p], i, &c), "palette color");
      feed_color(&c);
    }
  }
  {
    GSHSCENE_Color colors[3];
    for (int i = 0; i < 3; i++) {
      colors[i] = random_foreground();
    }
    const GSHSCENE_Resource * d = NULL;
    check(gshscene_store_intern_palette(store, 3, colors, &d), "double palette");
    feed(gshscene_resource_hash(d));
    gshscene_resource_release(d);
  }
  /* Runs from a face of the test provider, at fractional sizes, for the design maps. */
  const GSHSCENE_Resource * face = test_face(store, (unsigned)between(0, 1), 0);
  for (unsigned l = 0; l < 12; l++) {
    GSHSCENE_List * list = NULL;
    check(gshscene_list_create(NULL, NULL, store, &list), "palette list");
    const GSHSCENE_Resource * palette = palettes[between(0, 5)];
    if (l % 4 != 3) {
      check(gshscene_list_set_palette(list, palette), "set palette");
    }
    unsigned n = between(1, 8);
    for (unsigned i = 0; i < n; i++) {
      /* One draw per statement: the order an initializer's or a call's operands are evaluated in is unspecified, and
       * targets differ in it, so two draws in one expression would differ by target, not by arithmetic. */
      GSHSCENE_Glyph_Fixed g;
      g.id = between(0, 9);
      g.x = (int32_t)between(0, 4096) - 2048;
      g.y = (int32_t)between(0, 4096) - 2048;
      const int32_t size = (int32_t)between(64, 2048);
      const uint32_t hint_mode = between(0, 1);
      const GSHSCENE_Resource * run = NULL;
      check(gshscene_store_intern_glyph_run_fixed(store, face, size, hint_mode, 0, 6u, 1, &g,
                &run), "fixed run");
      GSHSCENE_Fixed_Op ops[4];
      size_t op_count = between(0, 4);
      memset(ops, 0, sizeof ops);
      for (size_t k = 0; k < op_count; k++) {
        ops[k].kind = between(0, GSHSCENE_FIXED_OP_COUNT - 1);
        ops[k].bits = between(0, 16);
        static const unsigned used[GSHSCENE_FIXED_OP_COUNT] = {6u, 2u, 4u, 3u, 4u};
        for (unsigned v = 0; v < used[ops[k].kind]; v++) {
          ops[k].v[v] = (int32_t)between(0, 65536) - 32768;
        }
        if (ops[k].kind == GSHSCENE_FIXED_SKEW) {
          ops[k].bits = 16u; /* angles below a quarter turn: a skew of a quarter turn is infinite */
          ops[k].v[0] /= 4;
          ops[k].v[1] /= 4;
        }
      }
      GSHSCENE_Paint_Fixed paint;
      paint.kind = between(2, 3);
      paint.entry = between(0, 13);
      paint.alpha = (int32_t)between(0, 16384);
      paint.bits = 14u;
      if (paint.kind == GSHSCENE_FIXED_PAINT_FOREGROUND) {
        paint.entry = 0;
      } else if (paint.entry == 13) {
        paint.entry = GSHSCENE_FIXED_ENTRY_FOREGROUND;
      }
      GSHSCENE_Result r = (i % 3 == 2) ? gshscene_list_clip_push_fixed(list, run, ops, op_count, GSHGEOM_FILL_NONZERO, 0)
                                       : gshscene_list_fill_fixed(list, run, ops, op_count, GSHGEOM_FILL_NONZERO, &paint, 0);
      feed((uint64_t)r);
      if (r == GSHSCENE_OK && i % 3 == 2) {
        check(gshscene_list_group_push_fixed(list, between(0, GSHSCENE_COMPOSITE_PLUS), 0), "fixed group"); /* pinned, as above */
        check(gshscene_list_fill_fixed(list, run, NULL, 0, GSHGEOM_FILL_NONZERO, &paint, 0), "fixed fill");
        check(gshscene_list_group_pop(list), "fixed group pop");
        check(gshscene_list_clip_pop(list), "fixed clip pop");
      }
      gshscene_resource_release(run);
    }
    size_t count = gshscene_list_count(list);
    for (size_t i = 0; i < count; i++) {
      const GSHSCENE_Item * item = NULL;
      check(gshscene_list_item(list, i, &item), "palette item");
      feed(gshgeom_map_hash(&item->map));
      feed((uint64_t)item->paint.kind);
      feed((uint64_t)item->paint.entry);
    }
    check(gshscene_list_truncate(list, count / 2 + (count % 2)), "truncate");
    check(gshscene_list_truncate(list, gshscene_list_count(list)), "truncate to itself");
    while (gshscene_list_close(list) == GSHSCENE_ERR_INVALID) { /* a truncation inside a block: close what is open */
      if (gshscene_list_group_pop(list) != GSHSCENE_OK) {
        check(gshscene_list_clip_pop(list), "close the clip");
      }
    }
    feed(gshscene_list_hash(list));
    feed((uint64_t)gshscene_list_count(list));
    feed_render(list);
    gshscene_list_destroy(list);
  }
  /* A version whose palette changes: its hash, its list and the diff's damage of the palette shapes. */
  {
    GSHSCENE_Scene * scene = NULL;
    check(gshscene_scene_create(NULL, NULL, store, &scene), "palette scene");
    GSHSCENE_Edit * e = NULL;
    check(gshscene_scene_open_edit(scene, NULL, &e), "palette edit");
    for (unsigned i = 0; i < 4; i++) {
      GSHSCENE_Glyph_Fixed g = {i, (int32_t)(i * 640u), 1280};
      const GSHSCENE_Resource * run = NULL;
      check(gshscene_store_intern_glyph_run_fixed(store, face, 1024, 1, 0, 6u, 1, &g, &run), "scene run");
      GSHSCENE_Node n;
      gshscene_node_init(GSHSCENE_NODE_SHAPE, &n);
      n.resource = run;
      n.paint.kind = (i % 2) ? GSHSCENE_PAINT_PALETTE : GSHSCENE_PAINT_SOLID;
      n.paint.entry = (i % 2) ? i : 0;
      add_node(e, GSHSCENE_ROOT_ID, &n, NULL);
      gshscene_resource_release(run);
    }
    check(gshscene_edit_set_palette(e, palettes[0]), "edit palette");
    const GSHSCENE_Version * a = NULL;
    check(gshscene_scene_publish(scene, e, NULL, &a), "publish palette");
    gshscene_edit_destroy(e);
    check(gshscene_scene_open_edit(scene, NULL, &e), "palette edit 2");
    check(gshscene_edit_set_palette(e, palettes[1]), "edit palette 2");
    const GSHSCENE_Version * b = NULL;
    check(gshscene_scene_publish(scene, e, NULL, &b), "publish palette 2");
    gshscene_edit_destroy(e);
    feed_version(a);
    feed_version(b);
    GSHGEOM_Map device;
    gshgeom_map_scale(2, 0.5, 0.5, 1.0, &device);
    feed_diff(a, b, &device);
    const GSHSCENE_List * vl = NULL;
    check(gshscene_version_list(b, &vl), "palette version list");
    feed_render(vl);
    gshscene_version_release(a);
    gshscene_version_release(b);
    gshscene_scene_destroy(scene);
  }
  gshscene_resource_release(face);
  for (unsigned p = 0; p < 6; p++) {
    gshscene_resource_release(palettes[p]);
  }
  gshscene_store_destroy(store);
}

/* --- gradients (color-paint story P1) ----------------------------------------------------------------- */

static void feed_double(double x) {
  feed(gshgeom_f64_bits(x));
}

static const GSHSCENE_Resource * random_gradient(GSHSCENE_Store * store) {
  GSHSCENE_Gradient g;
  memset(&g, 0, sizeof g);
  g.kind = between(1, 3);
  double v[6];
  for (int i = 0; i < 6; i++) {
    v[i] = entry() * 8.0;
  }
  unsigned degenerate = between(0, 7);
  if (g.kind == GSHSCENE_GRADIENT_LINEAR) {
    g.p0[0] = v[0];
    g.p0[1] = v[1];
    g.p1[0] = degenerate == 0 ? v[0] : v[2];
    g.p1[1] = degenerate == 0 ? v[1] : v[3];
    if (between(0, 2) == 0) {
      g.d[0] = v[4];
      g.d[1] = v[5];
    }
  } else if (g.kind == GSHSCENE_GRADIENT_RADIAL) {
    g.c0[0] = v[0];
    g.c0[1] = v[1];
    g.r0 = v[2];
    g.c1[0] = degenerate == 0 ? v[0] : v[3];
    g.c1[1] = degenerate == 0 ? v[1] : v[4];
    g.r1 = degenerate == 0 ? v[2] : v[5];
  } else {
    g.center[0] = v[0];
    g.center[1] = v[1];
    g.angle_unit = between(0, 3);
    g.a0 = v[2];
    g.a1 = degenerate == 0 ? v[2] : v[3];
  }
  g.map = random_map();
  g.extend = between(0, 2);
  g.premultiplied = between(0, 1);
  g.space = random_space();
  GSHSCENE_Gradient_Stop stops[6];
  memset(stops, 0, sizeof stops);
  g.stop_count = between(0, 6);
  for (size_t i = 0; i < g.stop_count; i++) {
    stops[i].offset = entry() * 1.5;
    unsigned k = between(0, 5);
    if (k == 0) {
      stops[i].color.kind = GSHSCENE_PAINT_FOREGROUND;
      stops[i].color.rgba[3] = entry() < 0 ? 0.5 : 1.0;
    } else if (k == 1) {
      stops[i].color.kind = GSHSCENE_PAINT_PALETTE;
      stops[i].color.entry = between(0, 2);
      stops[i].color.rgba[3] = 0.75;
    } else {
      stops[i].color.kind = GSHSCENE_PAINT_SOLID;
      stops[i].color.rgba[0] = entry();
      stops[i].color.rgba[1] = entry() + 0.5;
      stops[i].color.rgba[2] = entry() * 2.0;
      stops[i].color.rgba[3] = (entry() + 1.0) / 2.0;
      stops[i].color.space = random_space();
      if (gshscene_color_space_is_target(&stops[i].color.space) && !gshscene_color_space_is_target(&g.space)) {
        stops[i].color.space = g.space; /* a target-space solid only in a target-space gradient (G3) */
      }
    }
  }
  g.stops = stops;
  const GSHSCENE_Resource * r = NULL;
  check(gshscene_store_intern_gradient(store, &g, &r), "intern gradient");
  return r;
}

static void gradient_section(void) {
  GSHSCENE_Store * store = NULL;
  check(gshscene_store_create(NULL, NULL, &store), "gradient store");
  static const uint8_t entries[12] = {255, 128, 0, 255, 10, 20, 30, 40, 1, 2, 254, 128};
  const GSHSCENE_Resource * palette = NULL;
  check(gshscene_store_intern_palette_rgba8(store, 3, entries, &palette), "gradient palette");
  const GSHSCENE_Resource * gradients[24];
  static const GSHSCENE_Color_Space targets[3] = {{GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_SRGB, 0.0},
      {GCOL_PRIMARIES_DISPLAY_P3, GCOL_TRANSFER_LINEAR, 0.0}, {GCOL_PRIMARIES_BT2020, GCOL_TRANSFER_GAMMA, 2.4}};
  for (unsigned i = 0; i < 24; i++) {
    gradients[i] = random_gradient(store);
    feed(gshscene_resource_hash(gradients[i]));
    GSHSCENE_Gradient back;
    check(gshscene_resource_gradient(gradients[i], &back), "read gradient");
    GSHSCENE_Color fg = random_foreground();
    for (unsigned t = 0; t < 3; t++) {
      GSHSCENE_Resolved_Stop out[6];
      GSHSCENE_Color_Space into;
      memset(&into, 0, sizeof into);
      GSHSCENE_Result r = gshscene_gradient_resolve(gradients[i], &fg, palette, &targets[t], &into, out, 6);
      feed((uint64_t)r);
      for (size_t k = 0; r == GSHSCENE_OK && k < back.stop_count; k++) {
        feed_double(out[k].offset);
        for (int c = 0; c < 4; c++) {
          feed_double(out[k].rgba[c]);
        }
      }
      feed((uint64_t)into.primaries);
    }
    /* The notice under maps composed from inexact entries, and under a map that is exactly singular. */
    GSHSCENE_Item item;
    memset(&item, 0, sizeof item);
    item.kind = GSHSCENE_ITEM_FILL;
    item.map = random_map();
    item.paint.kind = GSHSCENE_PAINT_GRADIENT;
    item.paint.gradient = gradients[i];
    GSHGEOM_Map instances[2];
    instances[0] = random_map();
    instances[1] = random_map();
    GSHGEOM_Map target = random_map();
    feed((uint64_t)gshscene_gradient_notice(&item, instances, 2, &target));
    double flat[4] = {0.0, 0.0, 0.0, 0.0}; /* rank one: its second column zero */
    flat[0] = entry();
    flat[2] = entry();
    const double t0[2] = {0.0, 0.0};
    if (gshgeom_map_from_matrix(2, flat, t0, &item.map) != GSHGEOM_OK) {
      fail("flat map");
    }
    feed((uint64_t)gshscene_gradient_notice(&item, instances, 2, &target));
  }
  /* The float-free constructor on a run of a 2048-unit face at a size that is not a power of two. */
  const GSHSCENE_Resource * face = test_face(store, 0, 0);
  GSHSCENE_Glyph glyph = {3, 0.0, 0.0, NULL};
  glyph.x = coord();
  glyph.y = coord();
  GSHSCENE_Glyph_Run run;
  memset(&run, 0, sizeof run);
  run.source = GSHSCENE_GLYPHS_FROM_FONT;
  run.face = face;
  run.size = 13.37;
  run.glyph_count = 1;
  run.glyphs = &glyph;
  const GSHSCENE_Resource * glyphs = NULL;
  check(gshscene_store_intern_glyph_run(store, &run, &glyphs), "gradient run");
  for (unsigned i = 0; i < 6; i++) {
    GSHSCENE_Gradient_Fixed f;
    memset(&f, 0, sizeof f);
    f.kind = 1u + i % 3u;
    f.extend = between(0, 2);
    f.bits = between(0, 14);
    for (int k = 0; k < (f.kind == GSHSCENE_FIXED_GRADIENT_SWEEP ? 4 : 6); k++) {
      f.v[k] = (int32_t)between(0, 65535) - 32768;
    }
    if (f.kind == GSHSCENE_FIXED_GRADIENT_RADIAL) {
      f.v[2] = (int32_t)between(0, 65535);
      f.v[5] = (int32_t)between(0, 65535);
    }
    f.primaries = GCOL_PRIMARIES_SRGB;
    f.transfer = GCOL_TRANSFER_LINEAR;
    f.premultiplied = 1;
    GSHSCENE_Fixed_Op op;
    memset(&op, 0, sizeof op);
    op.kind = GSHSCENE_FIXED_ROTATE;
    op.bits = 16;
    op.v[0] = (int32_t)between(0, 65535);
    f.ops = &op;
    f.op_count = i % 2;
    GSHSCENE_Gradient_Stop_Fixed fs[2];
    memset(fs, 0, sizeof fs);
    fs[0].bits = 14;
    fs[0].color.kind = GSHSCENE_FIXED_PAINT_PALETTE;
    fs[0].color.entry = between(0, 2);
    fs[0].color.alpha = 1;
    fs[1].offset = 16384;
    fs[1].bits = 14;
    fs[1].color.kind = GSHSCENE_FIXED_PAINT_PALETTE;
    fs[1].color.entry = 0xFFFFu;
    fs[1].color.alpha = (int32_t)between(0, 16384);
    fs[1].color.bits = 14;
    f.stop_count = 2;
    f.stops = fs;
    const GSHSCENE_Resource * g = NULL;
    check(gshscene_store_intern_gradient_fixed(store, glyphs, &f, &g), "fixed gradient");
    feed(gshscene_resource_hash(g));
    GSHSCENE_Gradient back;
    check(gshscene_resource_gradient(g, &back), "read fixed gradient");
    feed(gshgeom_map_hash(&back.map));
    gshscene_resource_release(g);
  }
  /* Lists of gradient fills (with solids, a palette and a foreground), rendered by both test sinks. */
  for (unsigned l = 0; l < 8; l++) {
    GSHSCENE_List * list = NULL;
    check(gshscene_list_create(NULL, NULL, store, &list), "gradient list");
    GSHSCENE_Color fg = random_foreground();
    check(gshscene_list_set_foreground(list, &fg), "gradient list foreground");
    if (l % 2 == 0) {
      check(gshscene_list_set_palette(list, palette), "gradient list palette");
    }
    GSHGEOM_Path * path = random_path(2);
    const GSHSCENE_Resource * shape = NULL;
    check(gshscene_store_intern_path(store, path, &shape), "gradient path");
    gshgeom_path_destroy(path);
    unsigned n = between(1, 8);
    for (unsigned i = 0; i < n; i++) {
      GSHSCENE_Paint p;
      memset(&p, 0, sizeof p);
      p.kind = GSHSCENE_PAINT_GRADIENT;
      p.gradient = gradients[between(0, 23)];
      p.rgba[3] = (entry() + 1.0) / 2.0;
      GSHGEOM_Map map = random_map();
      check(gshscene_list_fill(list, shape, &map, GSHGEOM_FILL_NONZERO, &p, 0, 0.0), "gradient fill");
    }
    check(gshscene_list_close(list), "gradient close");
    feed(gshscene_list_hash(list));
    feed_render(list);
    gshscene_list_destroy(list);
    gshscene_resource_release(shape);
  }
  gshscene_resource_release(glyphs);
  gshscene_resource_release(face);
  for (unsigned i = 0; i < 24; i++) {
    gshscene_resource_release(gradients[i]);
  }
  gshscene_resource_release(palette);
  gshscene_store_destroy(store);
}

/*
 * Color-paint story P3: blend modes and PDF's group fields.  Lists of groups of every blend mode, some non-isolated,
 * knockout or masked by a picture (the fields spelled in the hash after the composite byte), their hashes and counts,
 * renders by the test sinks of feed_render (which refuse the fields) and by one that claims them (which records them);
 * a version of group nodes with the fields, its hash and its emitted list.
 */
static void feed_render_claiming(const GSHSCENE_List * list) {
  GSHSCENE_Dump_Sink_Config claims;
  gshscene_dump_sink_config_default(&claims);
  claims.raster_caps.flags |= GSHSCENE_CAP_GROUP_NON_ISOLATED | GSHSCENE_CAP_GROUP_KNOCKOUT | GSHSCENE_CAP_GROUP_SOFT_MASK;
  claims.stream_caps.flags = claims.raster_caps.flags & ~GSHSCENE_CAP_READBACK;
  GSHSCENE_Sink * sink = NULL;
  check(gshscene_dump_sink_create(NULL, NULL, &claims, &sink), "claiming test sink");
  GSHSCENE_Report * report = NULL;
  check(gshscene_report_create(NULL, NULL, &report), "report");
  uint64_t written = 0;
  GSHSCENE_Writer writer = {count_write, &written};
  GSHSCENE_Target stream;
  memset(&stream, 0, sizeof stream);
  stream.kind = GSHSCENE_TARGET_STREAM;
  stream.surface = &writer;
  stream.unit = GSHSCENE_UNIT_MILLIMETER;
  GSHSCENE_Render_Request request;
  memset(&request, 0, sizeof request);
  request.list = list;
  request.target = &stream;
  request.report = report;
  feed_job(sink, &request);
  feed(written);
  gshscene_report_destroy(report);
  check(gshscene_sink_destroy(sink), "destroy");
}

static void blend_section(void) {
  GSHSCENE_Store * store = NULL;
  check(gshscene_store_create(NULL, NULL, &store), "blend store");
  const GSHSCENE_Resource * paths[6];
  for (unsigned i = 0; i < 6; i++) {
    GSHGEOM_Path * path = random_path(2);
    check(gshscene_store_intern_path(store, path, &paths[i]), "blend path");
    gshgeom_path_destroy(path);
  }
  GSHSCENE_List * mask_list = NULL;
  check(gshscene_list_create(NULL, NULL, store, &mask_list), "mask list");
  GSHSCENE_Paint white = random_paint();
  check(gshscene_list_fill(mask_list, paths[0], NULL, GSHGEOM_FILL_NONZERO, &white, 0, 0.0), "mask fill");
  check(gshscene_list_close(mask_list), "mask close");
  const GSHSCENE_Resource * mask = NULL;
  check(gshscene_store_intern_picture(store, mask_list, &mask), "mask picture");
  gshscene_list_destroy(mask_list);
  feed(gshscene_resource_hash(mask));
  for (unsigned l = 0; l < 12; l++) {
    GSHSCENE_List * list = NULL;
    check(gshscene_list_create(NULL, NULL, store, &list), "blend list");
    GSHSCENE_Paint back = random_paint();
    check(gshscene_list_fill(list, paths[between(0, 5)], NULL, GSHGEOM_FILL_NONZERO, &back, 0, 0.0), "backdrop");
    for (unsigned g = 0; g < 6; g++) {
      GSHSCENE_Group group;
      memset(&group, 0, sizeof group);
      group.opacity = between(0, 4) / 4.0;
      group.composite = GSHSCENE_COMPOSITE_SCREEN + between(0, 14);
      group.blend_space = random_space();
      if (l % 3 == 1) {
        group.isolation = between(0, 1);
        group.knockout = between(0, 1);
      }
      if (l % 3 == 2 && between(0, 1)) {
        group.soft_mask = mask;
      }
      check(gshscene_list_group_push(list, &group, (int32_t)g, 0.0), "blend group");
      GSHSCENE_Paint paint = random_paint();
      check(gshscene_list_fill(list, paths[between(0, 5)], NULL, GSHGEOM_FILL_NONZERO, &paint, 0, 0.0), "blend fill");
      check(gshscene_list_group_pop(list), "blend pop");
    }
    check(gshscene_list_close(list), "blend close");
    feed(gshscene_list_hash(list));
    feed((uint64_t)gshscene_list_count(list));
    feed_render(list);
    feed_render_claiming(list);
    gshscene_list_destroy(list);
  }
  /* A version: group nodes of blend modes and the fields, emitted. */
  GSHSCENE_Scene * scene = NULL;
  check(gshscene_scene_create(NULL, NULL, store, &scene), "blend scene");
  GSHSCENE_Edit * edit = NULL;
  check(gshscene_scene_open_edit(scene, NULL, &edit), "blend edit");
  for (unsigned i = 0; i < 4; i++) {
    GSHSCENE_Node n;
    gshscene_node_init(GSHSCENE_NODE_GROUP, &n);
    n.composite = GSHSCENE_COMPOSITE_SCREEN + between(0, 14);
    n.isolation = i & 1u;
    n.knockout = (i >> 1) & 1u;
    n.soft_mask = i == 3 ? mask : NULL;
    uint64_t id = 0;
    check(gshscene_edit_add(edit, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, &n, NULL, &id), "blend node");
    GSHSCENE_Node shape;
    gshscene_node_init(GSHSCENE_NODE_SHAPE, &shape);
    shape.resource = paths[i];
    shape.paint = random_paint();
    check(gshscene_edit_add(edit, id, GSHSCENE_APPEND, &shape, NULL, NULL), "blend shape");
  }
  const GSHSCENE_Version * version = NULL;
  check(gshscene_scene_publish(scene, edit, NULL, &version), "blend publish");
  feed(gshscene_version_hash(version));
  const GSHSCENE_List * emitted = NULL;
  check(gshscene_version_list(version, &emitted), "blend emit");
  feed(gshscene_list_hash(emitted));
  feed_render(emitted);
  gshscene_version_release(version);
  gshscene_edit_destroy(edit);
  gshscene_scene_destroy(scene);
  gshscene_resource_release(mask);
  for (unsigned i = 0; i < 6; i++) {
    gshscene_resource_release(paths[i]);
  }
  gshscene_store_destroy(store);
}

int main(void) {
  gshgeom_hash_init(&total);
  GSHSCENE_Store * store = NULL;
  check(gshscene_store_create(NULL, NULL, &store), "store");

  /* Paths, each interned twice. */
  const GSHSCENE_Resource * paths[MAX_PATHS];
  size_t path_count = 0;
  for (unsigned i = 0; i < MAX_PATHS; i++) {
    GSHGEOM_Path * path = random_path((i % 5 == 4) ? 3 : 2);
    const GSHSCENE_Resource * a = NULL;
    const GSHSCENE_Resource * b = NULL;
    check(gshscene_store_intern_path(store, path, &a), "intern path");
    check(gshscene_store_intern_path(store, path, &b), "intern path again");
    feed(gshscene_resource_hash(a));
    feed(a == b);
    feed((uint64_t)gshscene_resource_kind(a));
    gshscene_resource_release(b);
    gshgeom_path_destroy(path);
    paths[path_count++] = a;
  }

  /* Glyph runs: from a font, and from outlines of the paths above. */
  const GSHSCENE_Resource * runs[24];
  size_t run_count = 0;
  for (unsigned i = 0; i < 24; i++) {
    static const double sizes[5] = {8.0, 10.5, 12.0, 16.0, 24.25};
    GSHSCENE_Glyph glyphs[20];
    GSHSCENE_Glyph_Run run;
    memset(&run, 0, sizeof run);
    const GSHSCENE_Resource * face = NULL;
    if (i % 2 == 0) {
      /* One draw per statement: the order in which a call's arguments are evaluated is the target's. */
      unsigned which = between(0, 2);
      int16_t axis = (i % 4 == 0) ? 0 : (int16_t)((int)between(0, 32768) - 16384);
      face = test_face(store, which, axis);
      run.source = GSHSCENE_GLYPHS_FROM_FONT;
      run.face = face;
      run.size = sizes[between(0, 4)];
      run.hint_mode = between(0, 2);
      run.hint_version = between(0, 2);
      run.glyph_count = between(0, 20);
      for (size_t g = 0; g < run.glyph_count; g++) {
        glyphs[g].id = (uint32_t)between(0, 9);
        glyphs[g].x = coord();
        glyphs[g].y = coord();
        glyphs[g].outline = NULL;
      }
    } else {
      run.source = GSHSCENE_GLYPHS_FROM_OUTLINES;
      run.glyph_count = between(0, 6);
      for (size_t g = 0; g < run.glyph_count; g++) {
        glyphs[g].id = (uint32_t)g;
        glyphs[g].x = coord();
        glyphs[g].y = coord();
        glyphs[g].outline = paths[between(0, MAX_PATHS - 1)];
      }
    }
    run.glyphs = glyphs;
    const GSHSCENE_Resource * r = NULL;
    check(gshscene_store_intern_glyph_run(store, &run, &r), "intern run");
    gshscene_resource_release(face); /* the run holds its own */
    feed(gshscene_resource_hash(r));
    feed((uint64_t)run.glyph_count);
    runs[run_count++] = r;
  }

  /* Lists of every item kind, some frozen into pictures that later lists instance. */
  const GSHSCENE_Resource * pictures[MAX_PICTURES];
  size_t picture_count = 0;
  for (unsigned l = 0; l < 40; l++) {
    GSHSCENE_List * list = NULL;
    check(gshscene_list_create(NULL, NULL, store, &list), "list");
    {
      /* A list frozen into a picture has the default foreground: a picture has none of its own. */
      GSHSCENE_Color fg = random_foreground();
      if (l % 3 != 2) {
        check(gshscene_list_set_foreground(list, &fg), "list foreground");
      }
    }
    uint8_t pushes[8];
    size_t depth = 0;
    unsigned n = between(1, 30);
    for (unsigned i = 0; i < n; i++) {
      GSHGEOM_Map map = random_map();
      GSHSCENE_Paint paint = random_paint();
      unsigned pick_path = between(0, MAX_PATHS - 1);
      unsigned pick_run = between(0, 23);
      const GSHSCENE_Resource * res = (next() & 3) ? paths[pick_path] : runs[pick_run];
      GSHGEOM_Fill_Rule rule = (next() & 1) ? GSHGEOM_FILL_EVEN_ODD : GSHGEOM_FILL_NONZERO;
      int32_t layer = (int32_t)between(0, 10) - 5;
      int use_map = (next() & 3) != 0;
      unsigned pick_picture = picture_count > 0 ? between(0, (unsigned)picture_count - 1) : 0;
      GSHSCENE_Group props = random_group_props();
      double item_depth = random_depth();
      switch (between(0, 5)) {
        case 0:
        case 1:
          check(gshscene_list_fill(list, res, use_map ? &map : NULL, rule, &paint, layer, item_depth), "fill");
          break;
        case 2:
          if (depth < sizeof pushes) {
            check(gshscene_list_clip_push(list, res, &map, rule, layer, item_depth), "clip push");
            pushes[depth++] = 1;
          }
          break;
        case 3:
          if (depth < sizeof pushes) {
            check(gshscene_list_group_push(list, &props, layer, item_depth), "group push");
            pushes[depth++] = 2;
          }
          break;
        case 4:
          if (depth > 0) {
            check(pushes[--depth] == 1 ? gshscene_list_clip_pop(list) : gshscene_list_group_pop(list), "pop");
          }
          break;
        default:
          if (picture_count > 0) {
            check(gshscene_list_instance(list, pictures[pick_picture], &map, layer, item_depth), "instance");
          }
          break;
      }
    }
    while (depth > 0) {
      check(pushes[--depth] == 1 ? gshscene_list_clip_pop(list) : gshscene_list_group_pop(list), "final pop");
    }
    check(gshscene_list_close(list), "close");
    feed(gshscene_list_hash(list));
    feed((uint64_t)gshscene_list_count(list));
    feed((uint64_t)gshscene_list_expansion(list));
    feed_ties(list);
    feed_render(list);
    if (l % 3 == 2 && picture_count < MAX_PICTURES) {
      const GSHSCENE_Resource * picture = NULL;
      check(gshscene_store_intern_picture(store, list, &picture), "picture");
      feed(gshscene_resource_hash(picture));
      feed((uint64_t)gshscene_resource_picture_depth(picture));
      pictures[picture_count++] = picture;
    }
    gshscene_list_destroy(list);
  }

  GSHSCENE_Store_Stats stats;
  check(gshscene_store_stats(store, &stats), "stats");
  feed(stats.live);
  feed(stats.created);
  feed(stats.hits);
  feed(stats.collisions);
  feed(stats.buckets);

  for (size_t i = 0; i < picture_count; i++) {
    gshscene_resource_release(pictures[i]);
  }
  for (size_t i = 0; i < run_count; i++) {
    gshscene_resource_release(runs[i]);
  }
  for (size_t i = 0; i < path_count; i++) {
    gshscene_resource_release(paths[i]);
  }
  check(gshscene_store_stats(store, &stats), "stats");
  if (stats.live != 0) {
    fail("a resource is still live after every handle was released");
  }
  gshscene_store_destroy(store);

  scene_section();
  glyph_section();
  palette_section();
  gradient_section();
  blend_section();

  printf("shape-scene.hash %016" PRIx64 "\n", gshgeom_hash_value(&total));
  printf("shape-scene.count %lu\n", count);
  return 0;
}
