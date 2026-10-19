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

static GSHSCENE_Paint random_paint(void) {
  static const double levels[6] = {0.0, 0.25, 0.5, 1.0, 2.0, -0.5};
  GSHSCENE_Paint p;
  memset(&p, 0, sizeof p);
  p.kind = GSHSCENE_PAINT_SOLID;
  for (int i = 0; i < 3; i++) {
    p.rgba[i] = levels[between(0, 5)];
  }
  p.rgba[3] = levels[between(0, 3)];
  return p;
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
  return n;
}

static GSHSCENE_Node random_group(Tree * t) {
  GSHSCENE_Node n;
  gshscene_node_init(GSHSCENE_NODE_GROUP, &n);
  static const double opacity[4] = {1.0, 1.0, 0.5, 0.25};
  n.opacity = opacity[between(0, 3)];
  n.map = random_map();
  n.layer = (int32_t)between(0, 4);
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
    run.source = GSHSCENE_GLYPHS_FROM_FONT;
    run.font_key = next() | 1u;
    run.size = 14.0;
    run.glyph_count = 4;
    for (size_t g = 0; g < 4; g++) {
      glyphs[g].id = (uint32_t)(next() & 0xffffu);
      glyphs[g].x = coord();
      glyphs[g].y = coord();
      glyphs[g].outline = NULL;
    }
    run.glyphs = glyphs;
    check(gshscene_store_intern_glyph_run(store, &run, &t.runs[0]), "scene font run");
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
    unsigned parent = between(0, SCENE_GROUPS - 1);
    t.instances[t.instance_count++] = add_node(e, t.groups[parent], &n, NULL);
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
    if (i % 2 == 0) {
      run.source = GSHSCENE_GLYPHS_FROM_FONT;
      run.font_key = next() | 1u;
      run.face_index = between(0, 3);
      run.size = sizes[between(0, 4)];
      run.hint_mode = between(0, 3);
      run.variation_key = (i % 4 == 0) ? 0 : next();
      run.glyph_count = between(0, 20);
      for (size_t g = 0; g < run.glyph_count; g++) {
        glyphs[g].id = (uint32_t)(next() & 0xffffffffu);
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
      unsigned opacity_steps = between(0, 4);
      switch (between(0, 5)) {
        case 0:
        case 1:
          check(gshscene_list_fill(list, res, use_map ? &map : NULL, rule, &paint, layer), "fill");
          break;
        case 2:
          if (depth < sizeof pushes) {
            check(gshscene_list_clip_push(list, res, &map, rule), "clip push");
            pushes[depth++] = 1;
          }
          break;
        case 3:
          if (depth < sizeof pushes) {
            GSHSCENE_Group g = {(double)opacity_steps / 4.0, 0, 0};
            check(gshscene_list_group_push(list, &g, layer), "group push");
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
            check(gshscene_list_instance(list, pictures[pick_picture], &map, layer),
                "instance");
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

  printf("shape-scene.hash %016" PRIx64 "\n", gshgeom_hash_value(&total));
  printf("shape-scene.count %lu\n", count);
  return 0;
}
