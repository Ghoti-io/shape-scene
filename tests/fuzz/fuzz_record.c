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
 * The recording API driven by call sequence (CAP-15, AD-19): one entry point for
 * libFuzzer (`make fuzz`) and for the replay gate (`make fuzz-replay`), which
 * feeds every committed input through it once.
 *
 * The first two bytes pick the limits, each from a short table of small values,
 * so that one input reaches every bound; the rest is a stream of calls: intern
 * resources, record a list (fill, clip, group, instance, close, reset, freeze to
 * a picture), and drive the retained scene (open an edit, add, set, upsert,
 * move, remove, publish, merge, diff, emit, visit, hash and dump).  After every
 * call the invariants are checked, and a violation is an `abort`:
 *
 * - a call that fails changed nothing (counts, expansion, the current version,
 *   its hash, the node count of an edit);
 * - a limit is asserted by its bound: a `GSHSCENE_ERR_LIMIT` is returned only
 *   where the bound is met, and after a success every quantity is within it
 *   (list items, push depth, expansion, picture depth, nodes, node depth, the
 *   occurrences of a version, its list's items, the store's resources and bytes);
 * - a published version's hash never changes while it is held;
 * - nothing leaks: at the end every object is released and a counting allocator
 *   says nothing is live.
 *
 * Story 12 adds six ops at the end of the table (a fill, a clip push and a group push
 * with the new fields, a list's foreground, a node's new properties, an edit's
 * foreground), so the older ops and the scripted inputs that use them are unchanged.
 * Every closed list is checked to be in paint order (N11) and its ties to be real.
 *
 * The op numbers below are mirrored by tools/gen-fuzz-corpus.py; the replay
 * also fails if the corpus never reaches an outcome (a limit, a refusal, an
 * invalid publish), which is what keeps a corpus from measuring nothing.
 */

#include <ghoti.io/shape-scene/shape-scene.h>

#include <ghoti.io/shape-geometry/shape-geometry.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond) \
  do { \
    if (!(cond)) { \
      fprintf(stderr, "fuzz_record: invariant failed: %s (line %d)\n", #cond, __LINE__); \
      abort(); \
    } \
  } while (0)

/* --- the op table (keep tools/gen-fuzz-corpus.py in step) ---------------------------------------- */

enum {
  OP_LIST_CREATE = 0, OP_FILL, OP_CLIP_PUSH, OP_CLIP_POP, OP_GROUP_PUSH, OP_GROUP_POP, OP_INSTANCE, OP_CLOSE, OP_RESET,
  OP_FREEZE, OP_LIST_DESTROY, OP_INTERN_PATH, OP_INTERN_RUN, OP_INTERN_OUTLINES, OP_RELEASE_RES, OP_SCENE_CREATE,
  OP_SCENE_DESTROY, OP_OPEN_EDIT, OP_DESTROY_EDIT, OP_ADD, OP_SET, OP_REMOVE, OP_MOVE, OP_UPSERT, OP_REMOVE_KEY,
  OP_PUBLISH, OP_MERGE, OP_HOLD, OP_RELEASE_VERSION, OP_DIFF, OP_EMIT, OP_VISIT, OP_DUMP, OP_SCENE_INTERN,
  OP_FILL_EX, OP_CLIP_PUSH_EX, OP_GROUP_PUSH_EX, OP_LIST_FOREGROUND, OP_NODE_PROPS, OP_EDIT_FOREGROUND, OP_LIST_PALETTE,
  OP_FILL_FIXED, OP_TRUNCATE, OP_EDIT_PALETTE, OP_COUNT
};

/* --- the outcomes the corpus must reach ------------------------------------------------------------ */

enum {
  OUT_LIST_LIMIT = 0, OUT_INTERN_LIMIT, OUT_EDIT_LIMIT, OUT_PUBLISH_LIMIT, OUT_PUBLISH_REFUSED, OUT_PUBLISH_INVALID,
  OUT_PUBLISH_OK, OUT_MERGE_OK, OUT_EMIT_OK, OUT_DIFF_OK, OUT_INSTANCE_PICTURE, OUT_LIST_TIES, OUT_FOREGROUND_VERSION,
  OUT_PARAMETER_REFUSED, OUT_EMIT_TIES, OUT_PALETTE_FILL, OUT_TRUNCATED, OUT_COUNT
};

static const char * const outcome_names[OUT_COUNT] = {"list limit", "intern limit", "edit limit", "publish limit",
    "publish refused", "publish invalid", "publish ok", "merge ok", "emit ok", "diff ok", "instance of a picture",
    "a closed list with ties", "a version whose foreground changed", "a merge refused over the foreground",
    "an emitted list with ties", "a palette fill recorded", "a list truncated"};

static unsigned long outcomes[OUT_COUNT];

/** Called by the replay after every input: the number of outcomes the corpus never reached. */
int gshscene_fuzz_unreached(void);

int gshscene_fuzz_unreached(void) {
  int missing = 0;
  for (int i = 0; i < OUT_COUNT; i++) {
    if (outcomes[i] == 0) {
      fprintf(stderr, "fuzz_record: the corpus never reached: %s\n", outcome_names[i]);
      missing++;
    }
  }
  return missing;
}

/* --- a counting allocator -------------------------------------------------------------------------- */

typedef struct Counter {
  long live;
} Counter;

static void * c_malloc(void * ctx, size_t n) {
  void * p = malloc(n ? n : 1);
  if (p) {
    ((Counter *)ctx)->live++;
  }
  return p;
}

static void * c_calloc(void * ctx, size_t k, size_t n) {
  void * p = calloc(k ? k : 1, n ? n : 1);
  if (p) {
    ((Counter *)ctx)->live++;
  }
  return p;
}

static void * c_realloc(void * ctx, void * old, size_t n) {
  void * p = realloc(old, n ? n : 1);
  if (p && !old) {
    ((Counter *)ctx)->live++;
  }
  return p;
}

static void c_free(void * ctx, void * p) {
  if (p) {
    ((Counter *)ctx)->live--;
  }
  free(p);
}

/* --- the state ---------------------------------------------------------------------------------------- */

#define POOL 8
#define EDITS 3
#define VERSIONS 4
#define IDS 24

typedef struct S {
  const uint8_t * p;
  size_t n;
  size_t at;
  Counter counter;
  GSHSCENE_Allocator alloc;
  GSHSCENE_Limits limits; /* every field explicit */
  GSHSCENE_Store * store;
  GSHSCENE_List * list;
  const GSHSCENE_Resource * res[POOL];     /* in the standalone store */
  const GSHSCENE_Resource * pictures[POOL];
  GSHSCENE_Scene * scene;
  unsigned scene_generation;
  const GSHSCENE_Resource * sres[POOL];    /* in the scene's store */
  GSHSCENE_Edit * edits[EDITS];
  unsigned edit_generation[EDITS];
  const GSHSCENE_Version * versions[VERSIONS];
  uint64_t version_hash[VERSIONS];
  unsigned version_generation[VERSIONS];
  uint64_t ids[IDS];
  unsigned id_count;
  FILE * null;
} S;

static unsigned arg(S * s) {
  return s->at < s->n ? s->p[s->at++] : 0;
}

static unsigned pick(S * s, unsigned n) {
  return arg(s) % n;
}

static GSHGEOM_Point P(double x, double y) {
  GSHGEOM_Point p;
  p.x = x;
  p.y = y;
  p.z = 0;
  return p;
}

/* A path out of a recipe number. */
static GSHGEOM_Path * make_path(S * s, unsigned recipe) {
  GSHGEOM_Path * path = NULL;
  int dim = recipe == 5 ? 3 : 2;
  if (gshgeom_path_create(&s->alloc, NULL, dim, &path) != GSHGEOM_OK) {
    return NULL;
  }
  switch (recipe % 6) {
    case 0: /* empty */
      break;
    case 1: /* a line */
      gshgeom_path_move_to(path, P(0, 0));
      gshgeom_path_line_to(path, P(4, 4));
      break;
    case 2: /* a square */
      gshgeom_path_move_to(path, P(0, 0));
      gshgeom_path_line_to(path, P(4, 0));
      gshgeom_path_line_to(path, P(4, 4));
      gshgeom_path_line_to(path, P(0, 4));
      gshgeom_path_close(path);
      break;
    case 3: /* a curve */
      gshgeom_path_move_to(path, P(1, 1));
      gshgeom_path_cubic_to(path, P(5, 0), P(6, 6), P(2, 7));
      gshgeom_path_quadratic_to(path, P(0, 3), P(1, 1));
      break;
    case 4: /* a long one */
      gshgeom_path_move_to(path, P(0, 0));
      for (int i = 1; i < 9; i++) {
        gshgeom_path_line_to(path, P(i, i % 3));
      }
      break;
    default: /* 3D */
      gshgeom_path_move_to(path, P(0, 0));
      gshgeom_path_line_to(path, P(1, 1));
      break;
  }
  return path;
}

static const GSHSCENE_Resource * intern_recipe(S * s, GSHSCENE_Store * store, unsigned recipe, GSHSCENE_Result * result) {
  GSHGEOM_Path * path = make_path(s, recipe);
  if (!path) {
    *result = GSHSCENE_ERR_OOM;
    return NULL;
  }
  const GSHSCENE_Resource * r = NULL;
  *result = gshscene_store_intern_path(store, path, &r);
  gshgeom_path_destroy(path);
  return r;
}

static GSHGEOM_Map pool_map(unsigned k, int * use) {
  GSHGEOM_Map m;
  *use = 1;
  switch (k % 6) {
    case 0:
      *use = 0; /* NULL: the identity */
      gshgeom_map_identity(2, &m);
      break;
    case 1:
      gshgeom_map_translate(2, 3, -2, 0, &m);
      break;
    case 2:
      gshgeom_map_scale(2, 2, 2, 1, &m);
      break;
    case 3:
      gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, 90, &m);
      break;
    case 4:
      gshgeom_map_identity(2, &m);
      break;
    default:
      gshgeom_map_translate(2, 40, 50, 0, &m);
      break;
  }
  return m;
}

static GSHSCENE_Paint pool_paint(unsigned k) {
  GSHSCENE_Paint paint;
  memset(&paint, 0, sizeof paint);
  paint.kind = GSHSCENE_PAINT_SOLID;
  paint.rgba[0] = (k % 3) * 0.5;
  paint.rgba[3] = (k % 5) == 4 ? 2.0 : 1.0 - (k % 2) * 0.5; /* an alpha of 2 is invalid */
  return paint;
}

/* Spaces of the table: the target's, the four gamuts with the three transfers, and values the library refuses. */
static GSHSCENE_Color_Space pool_space(unsigned k) {
  GSHSCENE_Color_Space sp;
  memset(&sp, 0, sizeof sp);
  switch (k % 10) {
    case 0:
      break;
    case 1:
      sp.primaries = GCOL_PRIMARIES_SRGB;
      sp.transfer = GCOL_TRANSFER_SRGB;
      break;
    case 2:
      sp.primaries = GCOL_PRIMARIES_SRGB;
      sp.transfer = GCOL_TRANSFER_LINEAR;
      break;
    case 3:
      sp.primaries = GCOL_PRIMARIES_DISPLAY_P3;
      sp.transfer = GCOL_TRANSFER_GAMMA;
      sp.gamma = 2.2;
      break;
    case 4:
      sp.primaries = GCOL_PRIMARIES_ADOBE_RGB;
      sp.transfer = GCOL_TRANSFER_LINEAR;
      break;
    case 5:
      sp.primaries = GCOL_PRIMARIES_BT2020;
      sp.transfer = GCOL_TRANSFER_SRGB;
      break;
    case 6:
      sp.primaries = GCOL_PRIMARIES_PROPHOTO; /* reserved */
      sp.transfer = GCOL_TRANSFER_SRGB;
      break;
    case 7:
      sp.primaries = GCOL_PRIMARIES_SRGB; /* a gamut and no transfer */
      break;
    case 8:
      sp.primaries = GCOL_PRIMARIES_SRGB;
      sp.transfer = GCOL_TRANSFER_GAMMA; /* a gamma without its exponent */
      break;
    default:
      sp.primaries = GCOL_PRIMARIES_BT2020;
      sp.transfer = GCOL_TRANSFER_LINEAR;
      break;
  }
  return sp;
}

static double pool_depth(unsigned k) {
  return (double)(k % 5) * 0.5 - 1.0;
}

static GSHSCENE_Color pool_color(S * s) {
  GSHSCENE_Color c;
  c.rgba[0] = pick(s, 5) * 0.5;
  c.rgba[1] = pick(s, 3) * 0.5;
  c.rgba[2] = pick(s, 2) * 1.0;
  unsigned a = pick(s, 6);
  c.rgba[3] = a == 5 ? 2.0 : a / 4.0; /* an alpha of 2 is invalid */
  c.space = pool_space(arg(s));
  return c;
}

/* --- list invariants ---------------------------------------------------------------------------------------- */

typedef struct ListSnap {
  size_t count;
  size_t expansion;
  int closed;
} ListSnap;

static ListSnap snap(const GSHSCENE_List * l) {
  ListSnap s = {gshscene_list_count(l), gshscene_list_expansion(l), gshscene_list_is_closed(l)};
  return s;
}

static void list_after(S * s, ListSnap before, GSHSCENE_Result r, int pushes, int is_instance) {
  const GSHSCENE_Limits * lim = &s->limits;
  CHECK(r != GSHSCENE_ERR_INTERNAL && r != GSHSCENE_ERR_OOM && r != GSHSCENE_ERR_CORRUPT && r != GSHSCENE_ERR_IO);
  size_t count = gshscene_list_count(s->list);
  if (r != GSHSCENE_OK) {
    CHECK(count == before.count);
    CHECK(gshscene_list_expansion(s->list) == before.expansion);
    CHECK(gshscene_list_is_closed(s->list) == before.closed);
    if (r == GSHSCENE_ERR_LIMIT) {
      outcomes[OUT_LIST_LIMIT]++;
      /* Asserted by the bound: items, push depth, or expansion is at its limit. */
      CHECK(count >= lim->max_list_items || (pushes && before.count >= 1) || is_instance || count + 1 > lim->max_expansion ||
            before.expansion + 1 > lim->max_expansion);
    }
  } else {
    CHECK(count == before.count + 1);
  }
  CHECK(count <= lim->max_list_items);
  CHECK(gshscene_list_expansion(s->list) <= lim->max_expansion);
}

/*
 * A closed list is in paint order (N11): in every sequence the units' keys follow the context of the group that encloses
 * it, and every tie is a real one: two items of equal depth, in order, naming the first items of adjacent units.
 */
typedef struct Seq {
  uint32_t context;
  int has_previous;
  double depth;
  int32_t layer;
} Seq;

static void check_closed_list(const GSHSCENE_List * list) {
  size_t n = gshscene_list_count(list);
  Seq * stack = malloc((n + 2) * sizeof *stack);
  CHECK(stack);
  size_t top = 0;
  memset(&stack[0], 0, sizeof stack[0]);
  for (size_t i = 0; i < n; i++) {
    const GSHSCENE_Item * item;
    CHECK(gshscene_list_item(list, i, &item) == GSHSCENE_OK);
    CHECK(isfinite(item->depth) && !(item->depth == 0.0 && signbit(item->depth)));
    if (item->kind == GSHSCENE_ITEM_CLIP_POP || item->kind == GSHSCENE_ITEM_GROUP_POP) {
      CHECK(top > 0);
      top--;
      continue;
    }
    Seq * seq = &stack[top];
    if (seq->has_previous) {
      if (seq->context == GSHSCENE_ORDER_DEPTH) {
        CHECK(item->depth < seq->depth || (item->depth == seq->depth && item->layer >= seq->layer));
      } else if (seq->context == GSHSCENE_ORDER_OVERLAY) {
        CHECK(item->layer >= seq->layer);
      }
    }
    seq->has_previous = 1;
    seq->depth = item->depth;
    seq->layer = item->layer;
    if (item->kind == GSHSCENE_ITEM_CLIP_PUSH || item->kind == GSHSCENE_ITEM_GROUP_PUSH) {
      Seq inner;
      memset(&inner, 0, sizeof inner);
      inner.context = item->kind == GSHSCENE_ITEM_GROUP_PUSH ? item->group.ordering : seq->context;
      stack[++top] = inner;
    }
  }
  CHECK(top == 0);
  free(stack);
  size_t ties = gshscene_list_tie_count(list);
  CHECK(ties <= n);
  size_t last = 0;
  for (size_t t = 0; t < ties; t++) {
    const GSHSCENE_Tie * tie;
    CHECK(gshscene_list_tie(list, t, &tie) == GSHSCENE_OK);
    CHECK(tie->first < tie->second && tie->second < n && (t == 0 || tie->second > last));
    last = tie->second;
    const GSHSCENE_Item * a;
    const GSHSCENE_Item * b;
    CHECK(gshscene_list_item(list, tie->first, &a) == GSHSCENE_OK && gshscene_list_item(list, tie->second, &b) == GSHSCENE_OK);
    CHECK(a->depth == tie->depth && b->depth == tie->depth);
    CHECK(tie->reason >= 1 && tie->reason < GSHSCENE_TIE_REASON_COUNT);
    CHECK(tie->reason != GSHSCENE_TIE_LAYER || a->layer != b->layer);
    CHECK(tie->reason == GSHSCENE_TIE_LAYER || a->layer == b->layer);
  }
}

/* --- ids -------------------------------------------------------------------------------------------------------- */

static void remember(S * s, uint64_t id) {
  if (id == 0) {
    return;
  }
  s->ids[s->id_count < IDS ? s->id_count++ : (unsigned)(id % IDS)] = id;
}

static uint64_t some_id(S * s) {
  unsigned k = arg(s);
  if (k % 8 == 0 || s->id_count == 0) {
    return GSHSCENE_ROOT_ID;
  }
  if (k % 8 == 1) {
    return 900 + k; /* one that is not there */
  }
  return s->ids[k % s->id_count];
}

/* --- version invariants ------------------------------------------------------------------------------------------ */

static size_t node_depth(const GSHSCENE_Version * v, uint64_t id, size_t level) {
  CHECK(level <= GSHSCENE_NODE_DEPTH_CEILING + 1);
  GSHSCENE_Node_Info info;
  CHECK(gshscene_version_node(v, id, &info) == GSHSCENE_OK);
  size_t best = 0;
  if (info.node.kind == GSHSCENE_NODE_GROUP) {
    for (size_t i = 0; i < info.child_count; i++) {
      uint64_t c;
      CHECK(gshscene_version_child(v, id, i, &c) == GSHSCENE_OK);
      size_t d = node_depth(v, c, level + 1);
      best = d > best ? d : best;
    }
  } else if (info.node.kind == GSHSCENE_NODE_INSTANCE) {
    best = node_depth(v, info.node.target, level + 1);
  }
  return best + 1;
}

typedef struct Count {
  size_t n;
  size_t bound;
} Count;

static int count_visit(void * ctx, const GSHSCENE_Visit * visit) {
  Count * c = ctx;
  CHECK(visit->path_count >= 1 && visit->path[visit->path_count - 1] == visit->node);
  CHECK(++c->n <= c->bound);
  return 0;
}

static void version_checks(S * s, const GSHSCENE_Version * v) {
  const GSHSCENE_Limits * lim = &s->limits;
  CHECK(gshscene_version_node_count(v) <= lim->max_nodes);
  CHECK(node_depth(v, GSHSCENE_ROOT_ID, 1) <= lim->max_node_depth);
  Count c = {0, lim->max_expansion};
  CHECK(gshscene_version_visit(v, count_visit, &c) == 0);
}

static void hold_check(S * s) {
  for (int i = 0; i < VERSIONS; i++) {
    if (s->versions[i]) {
      CHECK(gshscene_version_hash(s->versions[i]) == s->version_hash[i]);
    }
  }
}

static int hold(S * s, const GSHSCENE_Version * v) {
  for (int i = 0; i < VERSIONS; i++) {
    if (!s->versions[i]) {
      s->versions[i] = v;
      s->version_hash[i] = gshscene_version_hash(v);
      s->version_generation[i] = s->scene_generation;
      return 1;
    }
  }
  return 0;
}

static int has_edit(S * s, unsigned k) {
  return s->edits[k % EDITS] != NULL;
}

/* --- one op ----------------------------------------------------------------------------------------------------------- */

static void edit_props(S * s, GSHSCENE_Node * n) {
  unsigned kind = pick(s, 3);
  gshscene_node_init(kind == 0 ? GSHSCENE_NODE_GROUP : kind == 1 ? GSHSCENE_NODE_SHAPE : GSHSCENE_NODE_INSTANCE, n);
  int use;
  GSHGEOM_Map m = pool_map(arg(s), &use);
  n->map = m;
  n->layer = (int32_t)pick(s, 5) - 2;
  unsigned flags = pick(s, 6);
  n->flags = flags == 5 ? 2u : (flags == 4 ? GSHSCENE_NODE_HIDDEN : 0u); /* 2 is invalid */
  unsigned r = pick(s, POOL + 2);
  const GSHSCENE_Resource * res = r < POOL ? s->sres[r] : NULL;
  if (n->kind == GSHSCENE_NODE_GROUP) {
    unsigned o = pick(s, 6);
    n->opacity = o == 5 ? 2.0 : o / 4.0;
    if (arg(s) % 3 == 0) {
      n->clip = res;
    }
  } else if (n->kind == GSHSCENE_NODE_SHAPE) {
    n->resource = res;
    n->paint = pool_paint(arg(s));
    n->fill_rule = pick(s, 5) == 4 ? (GSHGEOM_Fill_Rule)9 : (pick(s, 2) ? GSHGEOM_FILL_EVEN_ODD : GSHGEOM_FILL_NONZERO);
  } else {
    n->target = some_id(s);
  }
}

static void op(S * s, unsigned code) {
  const GSHSCENE_Limits * lim = &s->limits;
  GSHSCENE_Result r;
  switch (code % OP_COUNT) {
    case OP_LIST_CREATE:
      if (s->list) {
        gshscene_list_destroy(s->list);
        s->list = NULL;
      }
      r = gshscene_list_create(&s->alloc, &s->limits, s->store, &s->list);
      CHECK(r == GSHSCENE_OK);
      break;
    case OP_FILL: {
      if (!s->list) {
        break;
      }
      ListSnap b = snap(s->list);
      int use;
      GSHGEOM_Map m = pool_map(arg(s), &use);
      GSHSCENE_Paint paint = pool_paint(arg(s));
      GSHGEOM_Fill_Rule rule = pick(s, 5) == 4 ? (GSHGEOM_Fill_Rule)7 : GSHGEOM_FILL_NONZERO;
      const GSHSCENE_Resource * target = s->res[pick(s, POOL)];
      int32_t layer = (int32_t)pick(s, 5) - 2;
      r = gshscene_list_fill(s->list, target, use ? &m : NULL, rule, &paint, layer, 0);
      list_after(s, b, r, 0, 0);
      break;
    }
    case OP_CLIP_PUSH: {
      if (!s->list) {
        break;
      }
      ListSnap b = snap(s->list);
      int use;
      GSHGEOM_Map m = pool_map(arg(s), &use);
      const GSHSCENE_Resource * region = s->res[pick(s, POOL)];
      r = gshscene_list_clip_push(s->list, region, use ? &m : NULL, GSHGEOM_FILL_EVEN_ODD, 0, 0);
      list_after(s, b, r, 1, 0);
      break;
    }
    case OP_CLIP_POP:
    case OP_GROUP_POP: {
      if (!s->list) {
        break;
      }
      ListSnap b = snap(s->list);
      r = code % OP_COUNT == OP_CLIP_POP ? gshscene_list_clip_pop(s->list) : gshscene_list_group_pop(s->list);
      list_after(s, b, r, 0, 0);
      break;
    }
    case OP_GROUP_PUSH: {
      if (!s->list) {
        break;
      }
      ListSnap b = snap(s->list);
      GSHSCENE_Group g = {.opacity = pick(s, 6) / 4.0};
      int32_t layer = (int32_t)pick(s, 5);
      r = gshscene_list_group_push(s->list, &g, layer, 0);
      list_after(s, b, r, 1, 0);
      break;
    }
    case OP_INSTANCE: {
      if (!s->list) {
        break;
      }
      ListSnap b = snap(s->list);
      int use;
      GSHGEOM_Map m = pool_map(arg(s), &use);
      const GSHSCENE_Resource * pic = s->pictures[pick(s, POOL)];
      r = gshscene_list_instance(s->list, pic, use ? &m : NULL, 0, 0);
      if (r == GSHSCENE_OK) {
        outcomes[OUT_INSTANCE_PICTURE]++;
      }
      list_after(s, b, r, 0, 1);
      break;
    }
    case OP_CLOSE:
      if (s->list) {
        ListSnap b = snap(s->list);
        r = gshscene_list_close(s->list);
        CHECK(r == GSHSCENE_OK || r == GSHSCENE_ERR_INVALID);
        CHECK(gshscene_list_count(s->list) == b.count);
        CHECK(gshscene_list_is_closed(s->list) == (r == GSHSCENE_OK ? 1 : b.closed));
        if (r == GSHSCENE_OK) {
          check_closed_list(s->list);
          outcomes[OUT_LIST_TIES] += gshscene_list_tie_count(s->list) > 0;
        }
      }
      break;
    case OP_RESET:
      if (s->list) {
        CHECK(gshscene_list_reset(s->list) == GSHSCENE_OK);
        CHECK(gshscene_list_count(s->list) == 0 && !gshscene_list_is_closed(s->list));
      }
      break;
    case OP_FREEZE: {
      if (!s->list) {
        break;
      }
      const GSHSCENE_Resource * pic = NULL;
      GSHSCENE_Store_Stats before;
      CHECK(gshscene_store_stats(s->store, &before) == GSHSCENE_OK);
      r = gshscene_store_intern_picture(s->store, s->list, &pic);
      CHECK(r == GSHSCENE_OK || r == GSHSCENE_ERR_INVALID || r == GSHSCENE_ERR_LIMIT);
      if (r == GSHSCENE_OK) {
        unsigned slot = pick(s, POOL);
        gshscene_resource_release(s->pictures[slot]);
        s->pictures[slot] = pic;
        CHECK(gshscene_resource_picture_depth(pic) <= lim->max_picture_depth);
      } else {
        CHECK(pic == NULL);
        if (r == GSHSCENE_ERR_LIMIT) {
          outcomes[OUT_INTERN_LIMIT]++;
        }
      }
      break;
    }
    case OP_LIST_DESTROY:
      gshscene_list_destroy(s->list);
      s->list = NULL;
      break;
    case OP_INTERN_PATH:
    case OP_SCENE_INTERN: {
      int in_scene = code % OP_COUNT == OP_SCENE_INTERN;
      if (in_scene && !s->scene) {
        break;
      }
      GSHSCENE_Store * store = in_scene ? gshscene_scene_store(s->scene) : s->store;
      const GSHSCENE_Resource ** pool = in_scene ? s->sres : s->res;
      unsigned slot = pick(s, POOL);
      GSHSCENE_Store_Stats before;
      CHECK(gshscene_store_stats(store, &before) == GSHSCENE_OK);
      const GSHSCENE_Resource * x = intern_recipe(s, store, arg(s), &r);
      CHECK(r == GSHSCENE_OK || r == GSHSCENE_ERR_LIMIT);
      if (r == GSHSCENE_OK) {
        gshscene_resource_release(pool[slot]);
        pool[slot] = x;
      } else {
        CHECK(x == NULL);
        outcomes[OUT_INTERN_LIMIT]++;
        GSHSCENE_Store_Stats after;
        CHECK(gshscene_store_stats(store, &after) == GSHSCENE_OK);
        CHECK(after.live == before.live && after.bytes == before.bytes && after.created == before.created);
      }
      GSHSCENE_Store_Stats now;
      CHECK(gshscene_store_stats(store, &now) == GSHSCENE_OK);
      CHECK(now.live <= lim->max_resources && now.bytes <= lim->max_bytes);
      break;
    }
    case OP_INTERN_RUN:
    case OP_INTERN_OUTLINES: {
      GSHSCENE_Store * store = (code % OP_COUNT == OP_INTERN_RUN) ? s->store : (s->scene ? gshscene_scene_store(s->scene) : s->store);
      const GSHSCENE_Resource ** pool = store == s->store ? s->res : s->sres;
      GSHSCENE_Glyph_Run run;
      memset(&run, 0, sizeof run);
      GSHSCENE_Glyph glyphs[3];
      memset(glyphs, 0, sizeof glyphs);
      run.glyph_count = pick(s, 4);
      run.glyphs = glyphs;
      const GSHSCENE_Resource * face = NULL;
      if (code % OP_COUNT == OP_INTERN_RUN) {
        /* A face, unbound, its key and coordinate from the one byte the key always took (story 14a), so the rest
         * of the input drives what it drove before. */
        unsigned key = arg(s);
        int16_t coord = (int16_t)((int)key * 128 - 16384);
        GSHSCENE_Face_Desc desc;
        memset(&desc, 0, sizeof desc);
        desc.font_key = 1 + key;
        desc.coord_count = 1;
        desc.coords = &coord;
        r = gshscene_store_intern_face(store, &desc, &face);
        CHECK(r == GSHSCENE_OK || r == GSHSCENE_ERR_LIMIT || r == GSHSCENE_ERR_OOM);
        run.source = GSHSCENE_GLYPHS_FROM_FONT;
        run.face = face;
        run.size = 10;
      } else {
        run.source = GSHSCENE_GLYPHS_FROM_OUTLINES;
      }
      for (size_t i = 0; i < run.glyph_count; i++) {
        glyphs[i].id = (uint32_t)i;
        glyphs[i].x = (double)i;
        glyphs[i].outline = run.source == GSHSCENE_GLYPHS_FROM_OUTLINES ? pool[pick(s, POOL)] : NULL;
      }
      const GSHSCENE_Resource * x = NULL;
      r = gshscene_store_intern_glyph_run(store, &run, &x);
      gshscene_resource_release(face); /* the run holds its own */
      CHECK(r == GSHSCENE_OK || r == GSHSCENE_ERR_LIMIT || r == GSHSCENE_ERR_INVALID);
      if (r == GSHSCENE_OK) {
        unsigned slot = pick(s, POOL);
        gshscene_resource_release(pool[slot]);
        pool[slot] = x;
      } else {
        CHECK(x == NULL);
        outcomes[OUT_INTERN_LIMIT] += r == GSHSCENE_ERR_LIMIT;
      }
      break;
    }
    case OP_RELEASE_RES: {
      unsigned slot = pick(s, POOL);
      gshscene_resource_release(s->res[slot]);
      s->res[slot] = NULL;
      if (arg(s) % 2) {
        gshscene_resource_release(s->sres[slot]);
        s->sres[slot] = NULL;
      }
      break;
    }
    case OP_SCENE_CREATE:
      if (!s->scene) {
        r = gshscene_scene_create(&s->alloc, &s->limits, NULL, &s->scene);
        CHECK(r == GSHSCENE_OK);
        s->scene_generation++;
        s->id_count = 0;
      }
      break;
    case OP_SCENE_DESTROY:
      if (s->scene) {
        for (int i = 0; i < POOL; i++) {
          gshscene_resource_release(s->sres[i]);
          s->sres[i] = NULL;
        }
        gshscene_scene_destroy(s->scene);
        s->scene = NULL;
      }
      break;
    case OP_OPEN_EDIT: {
      unsigned k = pick(s, EDITS);
      if (!s->scene || s->edits[k]) {
        break;
      }
      unsigned v = pick(s, VERSIONS + 1);
      const GSHSCENE_Version * base = v < VERSIONS ? s->versions[v] : NULL;
      GSHSCENE_Edit * e = NULL;
      r = gshscene_scene_open_edit(s->scene, base, &e);
      if (base && s->version_generation[v] != s->scene_generation) {
        CHECK(r == GSHSCENE_ERR_INVALID && e == NULL);
      } else {
        CHECK(r == GSHSCENE_OK && e);
        s->edits[k] = e;
        s->edit_generation[k] = s->scene_generation;
      }
      break;
    }
    case OP_DESTROY_EDIT: {
      unsigned k = pick(s, EDITS);
      gshscene_edit_destroy(s->edits[k]);
      s->edits[k] = NULL;
      break;
    }
    case OP_ADD:
    case OP_UPSERT: {
      unsigned k = pick(s, EDITS);
      if (!has_edit(s, k)) {
        break;
      }
      GSHSCENE_Edit * e = s->edits[k];
      GSHSCENE_Node n;
      uint64_t parent = some_id(s);
      unsigned posb = pick(s, 7);
      size_t pos = posb == 6 ? 1000 : (posb == 5 ? GSHSCENE_APPEND : posb);
      edit_props(s, &n);
      GSHSCENE_Key key;
      key.domain = 1 + pick(s, 2); /* every draw is its own statement: C leaves the order of an initializer's expressions open */
      key.value = pick(s, 6);
      size_t before = gshscene_edit_node_count(e);
      uint64_t id = 0;
      unsigned use_key = pick(s, 2);
      r = code % OP_COUNT == OP_ADD ? gshscene_edit_add(e, parent, pos, &n, use_key ? &key : NULL, &id)
                                    : gshscene_edit_upsert(e, &key, parent, pos, &n, &id);
      CHECK(r != GSHSCENE_ERR_INTERNAL && r != GSHSCENE_ERR_OOM);
      size_t after = gshscene_edit_node_count(e);
      if (r == GSHSCENE_OK) {
        remember(s, id);
        CHECK(after <= lim->max_nodes && after >= before);
        GSHSCENE_Node_Info info;
        CHECK(gshscene_edit_node(e, id, &info) == GSHSCENE_OK);
      } else {
        CHECK(after == before);
        if (r == GSHSCENE_ERR_LIMIT) {
          outcomes[OUT_EDIT_LIMIT]++;
          CHECK(before >= lim->max_nodes); /* asserted by the bound */
        }
      }
      break;
    }
    case OP_SET: {
      unsigned k = pick(s, EDITS);
      if (!has_edit(s, k)) {
        break;
      }
      GSHSCENE_Node n;
      edit_props(s, &n);
      size_t before = gshscene_edit_node_count(s->edits[k]);
      r = gshscene_edit_set(s->edits[k], some_id(s), &n);
      CHECK(r != GSHSCENE_ERR_INTERNAL && r != GSHSCENE_ERR_OOM);
      CHECK(gshscene_edit_node_count(s->edits[k]) == before);
      break;
    }
    case OP_REMOVE:
    case OP_REMOVE_KEY: {
      unsigned k = pick(s, EDITS);
      if (!has_edit(s, k)) {
        break;
      }
      size_t before = gshscene_edit_node_count(s->edits[k]);
      GSHSCENE_Key key;
      key.domain = 1 + pick(s, 2);
      key.value = pick(s, 6);
      uint64_t victim = some_id(s);
      r = code % OP_COUNT == OP_REMOVE ? gshscene_edit_remove(s->edits[k], victim)
                                       : gshscene_edit_remove_key(s->edits[k], &key);
      CHECK(r == GSHSCENE_OK || r == GSHSCENE_ERR_INVALID);
      size_t after = gshscene_edit_node_count(s->edits[k]);
      CHECK(r == GSHSCENE_OK ? after < before : after == before);
      break;
    }
    case OP_MOVE: {
      unsigned k = pick(s, EDITS);
      if (!has_edit(s, k)) {
        break;
      }
      size_t before = gshscene_edit_node_count(s->edits[k]);
      unsigned posb = pick(s, 5);
      uint64_t moved = some_id(s);
      uint64_t parent = some_id(s);
      r = gshscene_edit_move(s->edits[k], moved, parent, posb == 4 ? GSHSCENE_APPEND : posb);
      CHECK(r == GSHSCENE_OK || r == GSHSCENE_ERR_INVALID);
      CHECK(gshscene_edit_node_count(s->edits[k]) == before);
      break;
    }
    case OP_PUBLISH:
    case OP_MERGE: {
      unsigned k = pick(s, EDITS);
      if (!s->scene || !has_edit(s, k)) {
        break;
      }
      GSHSCENE_Edit * list[EDITS];
      size_t n = 0;
      list[n++] = s->edits[k];
      if (code % OP_COUNT == OP_MERGE) {
        for (unsigned j = 0; j < EDITS; j++) {
          if (j != k && s->edits[j] && pick(s, 2)) {
            list[n++] = s->edits[j];
          }
        }
      }
      const GSHSCENE_Version * before = NULL;
      CHECK(gshscene_scene_current(s->scene, &before) == GSHSCENE_OK);
      uint64_t hash_before = gshscene_version_hash(before);
      uint64_t serial_before = gshscene_version_serial(before);
      GSHSCENE_Problems * problems = NULL;
      const GSHSCENE_Version * out = NULL;
      r = gshscene_scene_merge(s->scene, list, n, &problems, &out);
      CHECK(r != GSHSCENE_ERR_INTERNAL && r != GSHSCENE_ERR_OOM);
      const GSHSCENE_Version * after = NULL;
      CHECK(gshscene_scene_current(s->scene, &after) == GSHSCENE_OK);
      if (r == GSHSCENE_OK) {
        CHECK(out == after && problems == NULL);
        if (after == before) {
          CHECK(gshscene_version_hash(after) == hash_before);
        } else {
          CHECK(gshscene_version_serial(after) == serial_before + 1);
          GSHSCENE_Color fg_before, fg_after;
          CHECK(gshscene_version_foreground(before, &fg_before) == GSHSCENE_OK);
          CHECK(gshscene_version_foreground(after, &fg_after) == GSHSCENE_OK);
          outcomes[OUT_FOREGROUND_VERSION] += memcmp(&fg_before, &fg_after, sizeof fg_before) != 0;
          outcomes[n > 1 ? OUT_MERGE_OK : OUT_PUBLISH_OK]++;
          outcomes[OUT_PUBLISH_OK] += n > 1;
          version_checks(s, after);
        }
        for (size_t i = 0; i < n; i++) {
          GSHSCENE_Node_Info info;
          (void)gshscene_edit_node(list[i], GSHSCENE_ROOT_ID, &info); /* a spent edit still reads */
        }
        const GSHSCENE_Version * keep = gshscene_version_retain(after);
        if (!hold(s, keep)) {
          gshscene_version_release(keep);
        }
      } else {
        CHECK(out == NULL);
        CHECK(after == before && gshscene_version_hash(after) == hash_before);
        if (r == GSHSCENE_ERR_LIMIT) {
          outcomes[OUT_PUBLISH_LIMIT]++;
        } else if (r == GSHSCENE_ERR_REFUSED) {
          outcomes[OUT_PUBLISH_REFUSED]++;
          CHECK(gshscene_problems_count(problems) > 0);
        } else if (r == GSHSCENE_ERR_INVALID) {
          if (problems) {
            outcomes[OUT_PUBLISH_INVALID]++;
            CHECK(gshscene_problems_count(problems) > 0);
          }
        }
        for (size_t i = 0; problems && i < gshscene_problems_count(problems); i++) {
          uint64_t id;
          GSHSCENE_Problem_Reason reason;
          CHECK(gshscene_problems_get(problems, i, &id, &reason) == GSHSCENE_OK && id != 0);
          CHECK((int)reason >= 1 && reason < GSHSCENE_PROBLEM_REASON_COUNT);
          if (reason == GSHSCENE_PROBLEM_PARAMETER) {
            CHECK(id == GSHSCENE_ROOT_ID && r == GSHSCENE_ERR_REFUSED);
            outcomes[OUT_PARAMETER_REFUSED]++;
          }
        }
      }
      gshscene_problems_destroy(problems);
      gshscene_version_release(out);
      gshscene_version_release(after);
      gshscene_version_release(before);
      break;
    }
    case OP_HOLD:
      if (s->scene) {
        const GSHSCENE_Version * cur = NULL;
        CHECK(gshscene_scene_current(s->scene, &cur) == GSHSCENE_OK);
        if (!hold(s, cur)) {
          gshscene_version_release(cur);
        }
      }
      break;
    case OP_RELEASE_VERSION: {
      unsigned k = pick(s, VERSIONS);
      gshscene_version_release(s->versions[k]);
      s->versions[k] = NULL;
      break;
    }
    case OP_DIFF: {
      unsigned a = pick(s, VERSIONS), b = pick(s, VERSIONS);
      if (!s->versions[a] || !s->versions[b]) {
        break;
      }
      int use;
      GSHGEOM_Map m = pool_map(arg(s), &use);
      GSHSCENE_Damage_Options opts = {(double)pick(s, 3)};
      GSHSCENE_Diff * d = NULL;
      r = gshscene_diff(s->versions[a], s->versions[b], use ? &m : NULL, pick(s, 2) ? &opts : NULL, &d);
      if (s->version_generation[a] != s->version_generation[b]) {
        CHECK(r == GSHSCENE_ERR_INVALID && d == NULL);
      } else {
        CHECK(r == GSHSCENE_OK && d);
        outcomes[OUT_DIFF_OK]++;
        GSHSCENE_Damage dmg;
        CHECK(gshscene_diff_damage(d, &dmg) == GSHSCENE_OK);
        CHECK(!(dmg.empty && dmg.unbounded));
        CHECK(dmg.unbounded || dmg.empty || (dmg.rect.x0 <= dmg.rect.x1 && dmg.rect.y0 <= dmg.rect.y1));
        uint64_t last = 0;
        for (size_t i = 0; i < gshscene_diff_count(d); i++) {
          uint64_t id;
          GSHSCENE_Diff_Kind kind;
          CHECK(gshscene_diff_get(d, i, &id, &kind) == GSHSCENE_OK && id > last && kind >= 1 && kind < GSHSCENE_DIFF_KIND_COUNT);
          last = id;
        }
        if (a == b || s->versions[a] == s->versions[b]) {
          CHECK(gshscene_diff_count(d) == 0 && dmg.empty && !gshscene_diff_foreground_changed(d));
        }
        GSHSCENE_Color fa, fb;
        CHECK(gshscene_version_foreground(s->versions[a], &fa) == GSHSCENE_OK);
        CHECK(gshscene_version_foreground(s->versions[b], &fb) == GSHSCENE_OK);
        CHECK(gshscene_diff_foreground_changed(d) == (memcmp(&fa, &fb, sizeof fa) != 0));
        gshscene_diff_destroy(d);
      }
      break;
    }
    case OP_EMIT: {
      unsigned k = pick(s, VERSIONS);
      const GSHSCENE_Version * v = s->versions[k];
      if (!v) {
        break;
      }
      const GSHSCENE_List * list = NULL;
      r = gshscene_version_list(v, &list);
      CHECK(r == GSHSCENE_OK || r == GSHSCENE_ERR_LIMIT);
      if (r == GSHSCENE_OK) {
        outcomes[OUT_EMIT_OK]++;
        CHECK(gshscene_list_is_closed(list) && gshscene_list_count(list) <= lim->max_list_items);
        check_closed_list(list);
        outcomes[OUT_EMIT_TIES] += gshscene_list_tie_count(list) > 0;
        for (size_t i = 0; i < gshscene_list_count(list); i++) {
          uint64_t node;
          CHECK(gshscene_version_item_node(v, 0, i, &node) == GSHSCENE_OK);
          GSHSCENE_Node_Info info;
          CHECK(gshscene_version_node(v, node, &info) == GSHSCENE_OK);
          const GSHSCENE_Item * item;
          CHECK(gshscene_list_item(list, i, &item) == GSHSCENE_OK);
          if (item->kind == GSHSCENE_ITEM_INSTANCE) {
            const GSHSCENE_Resource * pic = NULL;
            CHECK(gshscene_version_picture(v, info.node.target, &pic) == GSHSCENE_OK && pic == item->resource);
            CHECK(gshscene_resource_picture_depth(pic) <= lim->max_picture_depth);
          }
        }
      } else {
        CHECK(list == NULL);
      }
      break;
    }
    case OP_VISIT: {
      const GSHSCENE_Version * v = s->versions[pick(s, VERSIONS)];
      if (v) {
        Count c = {0, lim->max_expansion};
        CHECK(gshscene_version_visit(v, count_visit, &c) == 0);
      }
      break;
    }
    case OP_DUMP: {
      const GSHSCENE_Version * v = s->versions[pick(s, VERSIONS)];
      if (v) {
        CHECK(gshscene_version_dump(v, s->null) == GSHSCENE_OK);
      }
      if (s->list) {
        CHECK(gshscene_list_dump(s->list, s->null) == GSHSCENE_OK);
      }
      break;
    }
    case OP_FILL_EX: {
      if (!s->list) {
        break;
      }
      ListSnap b = snap(s->list);
      int use;
      GSHGEOM_Map m = pool_map(arg(s), &use);
      GSHSCENE_Paint paint = pool_paint(arg(s));
      unsigned kind = pick(s, 6);
      paint.kind = kind == 0 || kind == 1 ? GSHSCENE_PAINT_FOREGROUND
                   : kind == 2             ? GSHSCENE_PAINT_PALETTE
                   : kind == 3             ? GSHSCENE_PAINT_GRADIENT
                                           : GSHSCENE_PAINT_SOLID;
      if ((paint.kind == GSHSCENE_PAINT_FOREGROUND || paint.kind == GSHSCENE_PAINT_PALETTE) && pick(s, 4) != 0) {
        paint.rgba[0] = paint.rgba[1] = paint.rgba[2] = 0.0; /* mostly valid, sometimes not */
      }
      if (paint.kind == GSHSCENE_PAINT_PALETTE) {
        unsigned e = arg(s);
        paint.entry = e == 255 ? 0xFFFFu : e % 6; /* 0xFFFF is never an entry (N14) */
      }
      paint.space = pool_space(arg(s));
      const GSHSCENE_Resource * target = s->res[pick(s, POOL)];
      int32_t layer = (int32_t)pick(s, 4) - 1;
      double depth = pool_depth(arg(s));
      r = gshscene_list_fill(s->list, target, use ? &m : NULL, GSHGEOM_FILL_NONZERO, &paint, layer, depth);
      list_after(s, b, r, 0, 0);
      break;
    }
    case OP_CLIP_PUSH_EX: {
      if (!s->list) {
        break;
      }
      ListSnap b = snap(s->list);
      int use;
      GSHGEOM_Map m = pool_map(arg(s), &use);
      const GSHSCENE_Resource * region = s->res[pick(s, POOL)];
      int32_t layer = (int32_t)pick(s, 4) - 1;
      double depth = pool_depth(arg(s));
      r = gshscene_list_clip_push(s->list, region, use ? &m : NULL, GSHGEOM_FILL_NONZERO, layer, depth);
      list_after(s, b, r, 1, 0);
      break;
    }
    case OP_GROUP_PUSH_EX: {
      if (!s->list) {
        break;
      }
      ListSnap b = snap(s->list);
      GSHSCENE_Group g;
      memset(&g, 0, sizeof g);
      g.opacity = pick(s, 6) / 4.0;
      g.blend_space = pool_space(arg(s));
      g.ordering = pick(s, 4); /* 3 is outside the enum */
      g.composite = pick(s, (unsigned)GSHSCENE_COMPOSITE_COUNT + 1);
      int32_t layer = (int32_t)pick(s, 4) - 1;
      double depth = pool_depth(arg(s));
      r = gshscene_list_group_push(s->list, &g, layer, depth);
      list_after(s, b, r, 1, 0);
      break;
    }
    case OP_LIST_FOREGROUND: {
      if (!s->list) {
        break;
      }
      GSHSCENE_Color c = pool_color(s);
      GSHSCENE_Color before, after;
      CHECK(gshscene_list_foreground(s->list, &before) == GSHSCENE_OK);
      int closed = gshscene_list_is_closed(s->list);
      r = gshscene_list_set_foreground(s->list, &c);
      CHECK(r == GSHSCENE_OK || r == GSHSCENE_ERR_INVALID || r == GSHSCENE_ERR_UNSUPPORTED);
      CHECK(gshscene_list_foreground(s->list, &after) == GSHSCENE_OK);
      if (r == GSHSCENE_OK) {
        CHECK(!closed && memcmp(&after, &c, sizeof c) == 0);
      } else {
        CHECK(memcmp(&after, &before, sizeof before) == 0);
      }
      break;
    }
    case OP_NODE_PROPS: {
      unsigned k = pick(s, EDITS);
      if (!has_edit(s, k)) {
        break;
      }
      GSHSCENE_Edit * e = s->edits[k];
      uint64_t id = some_id(s);
      GSHSCENE_Node_Info info;
      unsigned depth_pick = arg(s);
      unsigned ordering = pick(s, 4);
      unsigned composite = pick(s, (unsigned)GSHSCENE_COMPOSITE_COUNT + 1);
      GSHSCENE_Color_Space space = pool_space(arg(s));
      unsigned paint_kind = pick(s, 3);
      if (gshscene_edit_node(e, id, &info) != GSHSCENE_OK) {
        break;
      }
      GSHSCENE_Node n = info.node;
      n.depth = pool_depth(depth_pick);
      if (n.kind == GSHSCENE_NODE_GROUP) {
        n.ordering = ordering;
        n.composite = composite;
        n.blend_space = space;
      } else if (n.kind == GSHSCENE_NODE_SHAPE) {
        if (paint_kind == 0) {
          n.paint.kind = GSHSCENE_PAINT_FOREGROUND;
          n.paint.rgba[0] = n.paint.rgba[1] = n.paint.rgba[2] = 0.0;
          memset(&n.paint.space, 0, sizeof n.paint.space);
        } else {
          n.paint.kind = GSHSCENE_PAINT_SOLID;
          n.paint.space = space;
        }
      }
      size_t before = gshscene_edit_node_count(e);
      r = gshscene_edit_set(e, id, &n);
      CHECK(r != GSHSCENE_ERR_INTERNAL && r != GSHSCENE_ERR_OOM);
      CHECK(gshscene_edit_node_count(e) == before);
      GSHSCENE_Node_Info now;
      CHECK(gshscene_edit_node(e, id, &now) == GSHSCENE_OK);
      if (r == GSHSCENE_OK) {
        CHECK(memcmp(&now.node.depth, &n.depth, sizeof n.depth) == 0 && now.node.ordering == n.ordering &&
              now.node.composite == n.composite);
      } else {
        CHECK(memcmp(&now.node.depth, &info.node.depth, sizeof now.node.depth) == 0 &&
              now.node.ordering == info.node.ordering && now.node.composite == info.node.composite);
      }
      break;
    }
    case OP_LIST_PALETTE: {
      /* N14: a palette of a few 8-bit entries (or none), set on the list; a refusal changes nothing. */
      if (!s->list) {
        break;
      }
      uint8_t bytes[4 * 4];
      unsigned count = pick(s, 5);
      for (unsigned i = 0; i < 4 * count; i++) {
        bytes[i] = (uint8_t)arg(s);
      }
      const GSHSCENE_Resource * palette = NULL;
      int none = pick(s, 4) == 0;
      if (!none) {
        r = gshscene_store_intern_palette_rgba8(s->store, count, bytes, &palette);
        CHECK(r == GSHSCENE_OK || r == GSHSCENE_ERR_LIMIT);
        if (r != GSHSCENE_OK) {
          outcomes[OUT_INTERN_LIMIT]++;
          break;
        }
        CHECK(gshscene_resource_palette_count(palette) == count);
      }
      const GSHSCENE_Resource * was = gshscene_list_palette(s->list);
      int closed = gshscene_list_is_closed(s->list);
      r = gshscene_list_set_palette(s->list, palette);
      CHECK(r == (closed ? GSHSCENE_ERR_INVALID : GSHSCENE_OK));
      CHECK(gshscene_list_palette(s->list) == (r == GSHSCENE_OK ? palette : was));
      gshscene_resource_release(palette); /* the list holds its own */
      break;
    }
    case OP_FILL_FIXED: {
      /* fixed.h: a fill or clip push with a design map and a fixed paint, palette entries of every kind. */
      if (!s->list) {
        break;
      }
      ListSnap b = snap(s->list);
      const GSHSCENE_Resource * target = s->res[pick(s, POOL)];
      GSHSCENE_Fixed_Op ops[3];
      memset(ops, 0, sizeof ops);
      size_t op_count = pick(s, 4);
      for (size_t k = 0; k < op_count; k++) {
        ops[k].kind = pick(s, GSHSCENE_FIXED_OP_COUNT + 1);
        ops[k].bits = pick(s, 33);
        for (int v = 0; v < 6; v++) {
          ops[k].v[v] = pick(s, 3) ? 0 : (int32_t)arg(s) - 128;
        }
      }
      GSHSCENE_Paint_Fixed paint = {0, 0, 0, 0}; /* one draw per statement: an initializer's order is unspecified */
      paint.kind = 2u + pick(s, 2);
      paint.alpha = (int32_t)pick(s, 20) - 2;
      paint.bits = pick(s, 4);
      unsigned e = arg(s);
      paint.entry = e == 255 ? 0xFFFFu : e == 254 ? 0x10000u : e % 6;
      int clip = pick(s, 4) == 0;
      r = clip ? gshscene_list_clip_push_fixed(s->list, target, ops, op_count, GSHGEOM_FILL_NONZERO, (int32_t)pick(s, 3))
               : gshscene_list_fill_fixed(s->list, target, ops, op_count, GSHGEOM_FILL_NONZERO, &paint, (int32_t)pick(s, 3));
      list_after(s, b, r, clip, 0);
      if (r == GSHSCENE_OK && !clip) {
        const GSHSCENE_Item * item;
        CHECK(gshscene_list_item(s->list, b.count, &item) == GSHSCENE_OK);
        CHECK(item->paint.kind == GSHSCENE_PAINT_PALETTE || item->paint.kind == GSHSCENE_PAINT_FOREGROUND);
        CHECK(item->paint.kind == GSHSCENE_PAINT_FOREGROUND ? item->paint.entry == 0
                                                            : item->paint.entry <= GSHSCENE_PALETTE_ENTRY_MAX);
        CHECK(paint.entry != 0xFFFFu || item->paint.kind == GSHSCENE_PAINT_FOREGROUND);
        outcomes[OUT_PALETTE_FILL] += item->paint.kind == GSHSCENE_PAINT_PALETTE;
      }
      break;
    }
    case OP_TRUNCATE: {
      if (!s->list) {
        break;
      }
      ListSnap b = snap(s->list);
      size_t to = b.count ? (size_t)pick(s, (unsigned)(b.count < 255 ? b.count + 1 : 255)) : 0;
      r = gshscene_list_truncate(s->list, to);
      CHECK(r == (b.closed ? GSHSCENE_ERR_INVALID : GSHSCENE_OK));
      CHECK(gshscene_list_count(s->list) == (r == GSHSCENE_OK ? to : b.count));
      CHECK(gshscene_list_expansion(s->list) <= b.expansion);
      outcomes[OUT_TRUNCATED] += r == GSHSCENE_OK && to < b.count;
      break;
    }
    case OP_EDIT_PALETTE: {
      unsigned k = pick(s, EDITS);
      if (!has_edit(s, k)) {
        break;
      }
      GSHSCENE_Store * store = gshscene_edit_store(s->edits[k]);
      uint8_t bytes[4] = {(uint8_t)arg(s), (uint8_t)arg(s), (uint8_t)arg(s), (uint8_t)arg(s)};
      const GSHSCENE_Resource * palette = NULL;
      if (pick(s, 3) != 0) {
        r = gshscene_store_intern_palette_rgba8(store, 1u + pick(s, 1), bytes, &palette);
        if (r != GSHSCENE_OK) {
          CHECK(r == GSHSCENE_ERR_LIMIT);
          break;
        }
      }
      const GSHSCENE_Resource * was = gshscene_edit_palette(s->edits[k]);
      r = gshscene_edit_set_palette(s->edits[k], palette);
      CHECK(r == GSHSCENE_OK || r == GSHSCENE_ERR_INVALID);
      CHECK(gshscene_edit_palette(s->edits[k]) == (r == GSHSCENE_OK ? palette : was));
      gshscene_resource_release(palette); /* the edit holds its own */
      break;
    }
    case OP_EDIT_FOREGROUND: {
      unsigned k = pick(s, EDITS);
      if (!has_edit(s, k)) {
        break;
      }
      GSHSCENE_Color c = pool_color(s);
      GSHSCENE_Color before, after;
      CHECK(gshscene_edit_foreground(s->edits[k], &before) == GSHSCENE_OK);
      r = gshscene_edit_set_foreground(s->edits[k], &c);
      CHECK(r == GSHSCENE_OK || r == GSHSCENE_ERR_INVALID || r == GSHSCENE_ERR_UNSUPPORTED);
      CHECK(gshscene_edit_foreground(s->edits[k], &after) == GSHSCENE_OK);
      CHECK(memcmp(&after, r == GSHSCENE_OK ? &c : &before, sizeof c) == 0);
      break;
    }
    default:
      break;
  }
}

/* --- the entry point -------------------------------------------------------------------------------------------------------- */

int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
  if (size < 2) {
    return 0;
  }
  S * s = calloc(1, sizeof *s);
  CHECK(s);
  s->p = data;
  s->n = size > 700 ? 700 : size;
  s->at = 2;
  s->alloc.ctx = &s->counter;
  s->alloc.malloc_fn = c_malloc;
  s->alloc.calloc_fn = c_calloc;
  s->alloc.realloc_fn = c_realloc;
  s->alloc.free_fn = c_free;
  s->null = fopen("/dev/null", "w");
  CHECK(s->null);
  unsigned b0 = data[0], b1 = data[1];
  gshscene_limits_default(&s->limits);
  static const size_t depth[4] = {1, 2, 3, 256};
  static const size_t items[4] = {3, 6, 12, (size_t)1 << 20};
  static const size_t pdepth[4] = {1, 2, 3, 32};
  static const size_t expansion[4] = {6, 20, 100, (size_t)1 << 24};
  static const size_t elements[4] = {3, 5, 8, (size_t)1 << 22};
  static const size_t nodes[4] = {4, 8, 16, (size_t)1 << 22};
  static const size_t ndepth[4] = {2, 3, 5, 256};
  s->limits.max_depth = depth[b0 & 3];
  s->limits.max_list_items = items[(b0 >> 2) & 3];
  s->limits.max_picture_depth = pdepth[(b0 >> 4) & 3];
  s->limits.max_expansion = expansion[(b0 >> 6) & 3];
  s->limits.max_resource_elements = elements[b1 & 3];
  s->limits.max_nodes = nodes[(b1 >> 2) & 3];
  s->limits.max_node_depth = ndepth[(b1 >> 4) & 3];
  switch ((b1 >> 6) & 3) {
    case 1:
      s->limits.max_bytes = 1500;
      break;
    case 2:
      s->limits.max_resources = 3;
      break;
    default:
      break;
  }
  CHECK(gshscene_store_create(&s->alloc, &s->limits, &s->store) == GSHSCENE_OK);
  CHECK(gshscene_list_create(&s->alloc, &s->limits, s->store, &s->list) == GSHSCENE_OK);
  /* Start with a scene and a few resources, so that short inputs reach something. */
  CHECK(gshscene_scene_create(&s->alloc, &s->limits, NULL, &s->scene) == GSHSCENE_OK);
  s->scene_generation = 1;
  GSHSCENE_Result r;
  for (unsigned i = 0; i < 3; i++) {
    s->res[i] = intern_recipe(s, s->store, 1 + i, &r);
    s->sres[i] = intern_recipe(s, gshscene_scene_store(s->scene), 1 + i, &r);
  }
  for (unsigned steps = 0; s->at < s->n && steps < 400; steps++) {
    op(s, arg(s));
    hold_check(s);
  }
  /* Everything back. */
  for (int i = 0; i < EDITS; i++) {
    gshscene_edit_destroy(s->edits[i]);
  }
  for (int i = 0; i < VERSIONS; i++) {
    gshscene_version_release(s->versions[i]);
  }
  gshscene_list_destroy(s->list);
  for (int i = 0; i < POOL; i++) {
    gshscene_resource_release(s->res[i]);
    gshscene_resource_release(s->pictures[i]);
    gshscene_resource_release(s->sres[i]);
  }
  gshscene_scene_destroy(s->scene);
  GSHSCENE_Store_Stats stats;
  CHECK(gshscene_store_stats(s->store, &stats) == GSHSCENE_OK);
  CHECK(stats.live == 0 && stats.bytes == 0);
  gshscene_store_destroy(s->store);
  fclose(s->null);
  CHECK(s->counter.live == 0);
  free(s);
  return 0;
}
