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
 * Color-paint story P2 adds two ops at the end: a gradient interned in the scene's store from the input (every kind,
 * extend and stop shape, maps singular and not, values at and past G14's bounds, G14 asserted by its bound), and a
 * shape node painted with one, whose emitted fills are checked.  N1's bound (Corey, 2026-10-21) adds one more: a
 * color component at, just past and far past `2^16`, through each entry point that records one (a fill, a list's and
 * an edit's foreground, a palette, a solid gradient stop, a shape node), asserted by the bound: past it is never
 * recorded, it is `ERR_LIMIT` only where nothing else is invalid, an invalid alpha or space is decided first, and a
 * failure changes nothing.
 *
 * Masks-images-patterns story I1 adds two ops: an image of every layout, sample type and alpha form (and CMYK, planar and
 * 10-bit rasters, which are refused), with strides, a space, flags, a take or a copy, hostile sizes over a small buffer and
 * sample values at and past the format's range, whose result class is predicted from the rules (IM2, IM3, IM4, IM10) and
 * whose descriptor, rows, accounted bytes and equality are read back; and an image named where a shape goes, which every
 * record call refuses.
 *
 * Story I2 adds one: an image paint from the input over an image of the pool (or a path, or nothing), its window, map,
 * edge modes, filters, filter space and tint chosen to be valid, invalid or past a bound, whose result class is predicted
 * from IP1 and whose content is read back, filled into the list and put through the recognition and the filter choice.
 *
 * Masks-images-patterns story M1 adds three: a mask interned from the input (every mode, coefficient set, space, backdrop,
 * transfer kind and acting-on, one field invalid or past a bound at a time), whose result class is predicted from MK1
 * (invalid before unsupported before a limit), whose canonical form, required features, handle, hash and dump are read back and
 * which is found again and told apart from its nearest neighbor; a group recorded with a mask from the pool and a placement
 * (MK6, MK13), whose item is read back and whose list is equal, with an equal hash and dump, to a second list recorded alike
 * and not equal to one that differs in the mask or the placement; and a group node of a scene of its own that is given a mask and
 * a placement, published, diffed (MK11) and emitted.  A freeze asserts MK12: a picture holding a masked group is one level
 * deeper than the mask's picture.
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
  OP_FILL_FIXED, OP_TRUNCATE, OP_EDIT_PALETTE, OP_INTERN_GRADIENT, OP_FILL_GRADIENT, OP_SCENE_GRADIENT,
  OP_GRADIENT_NODE, OP_COLOR_BOUND, OP_GROUP_PDF, OP_NODE_PDF, OP_INTERN_STYLE, OP_STROKE, OP_STROKE_NODE, OP_PLACEHOLDER,
  OP_PLACEHOLDER_NODE, OP_INTERN_IMAGE, OP_IMAGE_NOT_SHAPE, OP_IMAGE_PAINT, OP_INTERN_MASK,
  OP_GROUP_MASK, OP_NODE_MASK, OP_INTERN_PRIMITIVE, OP_COUNT
};

/* --- the outcomes the corpus must reach ------------------------------------------------------------ */

enum {
  OUT_LIST_LIMIT = 0, OUT_INTERN_LIMIT, OUT_EDIT_LIMIT, OUT_PUBLISH_LIMIT, OUT_PUBLISH_REFUSED, OUT_PUBLISH_INVALID,
  OUT_PUBLISH_OK, OUT_MERGE_OK, OUT_EMIT_OK, OUT_DIFF_OK, OUT_INSTANCE_PICTURE, OUT_LIST_TIES, OUT_FOREGROUND_VERSION,
  OUT_PARAMETER_REFUSED, OUT_EMIT_TIES, OUT_PALETTE_FILL, OUT_TRUNCATED, OUT_GRADIENT_FILL, OUT_GRADIENT_DEGENERATE,
  OUT_GRADIENT_REFUSED, OUT_GRADIENT_LIMIT, OUT_GRADIENT_NODE, OUT_GRADIENT_EMITTED, OUT_COLOR_LIMIT, OUT_COLOR_AT_BOUND,
  OUT_BLEND_GROUP, OUT_PDF_GROUP, OUT_PDF_INVALID, OUT_PDF_EMITTED, OUT_STYLE_INTERNED, OUT_STYLE_INVALID,
  OUT_STYLE_LIMIT, OUT_STYLE_DOUBLED, OUT_STYLE_SOLID, OUT_STYLE_UNSUPPORTED, OUT_STROKE_RECORDED,
  OUT_STROKE_UNSUPPORTED, OUT_STROKE_NODE, OUT_STROKE_EMITTED, OUT_PLACEHOLDER_RECORDED, OUT_PLACEHOLDER_INVALID,
  OUT_PLACEHOLDER_NODE, OUT_PLACEHOLDER_EMITTED, OUT_IMAGE_INTERNED, OUT_IMAGE_INVALID, OUT_IMAGE_UNSUPPORTED,
  OUT_IMAGE_LIMIT, OUT_IMAGE_BAD_SAMPLE, OUT_IMAGE_HIT, OUT_IMAGE_TAKEN, OUT_IMAGE_NOT_SHAPE, OUT_IMAGE_PAINT,
  OUT_IMAGE_PAINT_STENCIL, OUT_IMAGE_PAINT_INVALID, OUT_IMAGE_PAINT_LIMIT, OUT_IMAGE_PAINT_FILL, OUT_MASK_INTERNED,
  OUT_MASK_LUMINANCE, OUT_MASK_TRANSFER, OUT_MASK_INVALID, OUT_MASK_UNSUPPORTED, OUT_MASK_LIMIT, OUT_MASK_HIT, OUT_MASK_NEAR,
  OUT_MASK_GROUP, OUT_MASK_GROUP_INVALID, OUT_MASK_GROUP_UNSUPPORTED, OUT_MASK_LIST_EQUAL, OUT_MASK_DEPTH, OUT_MASK_DEPTH_LIMIT,
  OUT_MASK_NODE, OUT_MASK_NODE_INVALID, OUT_MASK_DAMAGE, OUT_MASK_NO_DAMAGE, OUT_MASK_EMITTED,
  OUT_PRIMITIVE_INTERNED, OUT_PRIMITIVE_FLIPPED, OUT_PRIMITIVE_CANONICAL, OUT_PRIMITIVE_INVALID, OUT_PRIMITIVE_REVERSED,
  OUT_PRIMITIVE_LIMIT, OUT_PRIMITIVE_HIT, OUT_PRIMITIVE_UNTAGGED, OUT_ARC_PATH, OUT_COUNT
};

static const char * const outcome_names[OUT_COUNT] = {"list limit", "intern limit", "edit limit", "publish limit",
    "publish refused", "publish invalid", "publish ok", "merge ok", "emit ok", "diff ok", "instance of a picture",
    "a closed list with ties", "a version whose foreground changed", "a merge refused over the foreground",
    "an emitted list with ties", "a palette fill recorded", "a list truncated", "a gradient fill recorded",
    "a degenerate gradient interned", "a gradient refused at intern",
    "a gradient past G14's bounds refused", "a shape node painted with a gradient", "an emitted list with a gradient fill",
    "a color past N1's bound refused", "a color at N1's bound recorded", "a blend-mode group recorded",
    "a group of PDF's fields recorded", "a group of PDF's fields refused as invalid", "a node of PDF's fields emitted",
    "a stroke style interned", "a stroke style refused as invalid", "a stroke style past SK2's bounds refused",
    "an odd dash array doubled", "a dash array made solid", "a width profile refused (reserved)",
    "a stroke recorded", "a 3D stroke refused as unsupported", "a stroke node", "an emitted list with a stroke",
    "a placeholder recorded", "a placeholder refused as invalid", "a placeholder node", "an emitted list with a placeholder",
    "an image interned", "an image refused as invalid", "an image refused as unsupported", "an image past IM10's bounds refused",
    "an image refused for a sample value", "an image found again", "an image taken", "an image refused where a shape goes", "an image paint interned", "a tinted image paint interned",
    "an image paint refused as invalid", "an image paint past IP1's bounds refused", "a fill with an image paint recorded",
    "a mask interned", "a luminance mask interned", "a mask with a transfer interned", "a mask refused as invalid",
    "a mask refused as unsupported", "a mask past MK1's or MK4's bounds refused", "a mask found again",
    "a mask told apart from its nearest neighbor", "a group recorded with a mask", "a group with a placement refused as invalid",
    "a group with a placement refused as unsupported", "two lists with a masked group, equal", "a picture holding a mask, one level deeper",
    "a masked group past the picture depth, refused at the freeze", "a group node with a mask", "a group node with a mask refused as invalid",
    "a changed mask or placement damaging the group's content", "an unchanged mask damaging nothing",
    "an emitted list with a masked group", "a primitive interned", "a flipped primitive interned",
    "a rounded rectangle made a rectangle by its square corners", "a primitive refused as invalid",
    "a rounded rectangle run the other way round interned", "a primitive past the store's limits refused",
    "a primitive found again", "the same geometry untagged, another handle", "a path with an arc interned"};

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
  const GSHSCENE_Resource * grads[4];      /* gradients, in the standalone store (color-paint story P1) */
  const GSHSCENE_Resource * sgrads[2];     /* gradients, in the scene's store (story P2) */
  const GSHSCENE_Resource * styles[4];     /* stroke styles, in the standalone store (strokes story S1) */
  const GSHSCENE_Resource * sstyles[2];    /* stroke styles, in the scene's store */
  const GSHSCENE_Resource * images[3];     /* images, in the standalone store (masks-images-patterns story I1) */
  const GSHSCENE_Resource * masks[4];      /* masks, in the standalone store (story M1) */
  uint64_t mask_hash[4];                   /* each mask's hash when it was made: a handle's hash never changes (MK13) */
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
  switch (recipe % 8) {
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
    case 6: { /* arcs: a circle by its centre form, then a chord */
      gshgeom_path_move_to(path, P(0, 0));
      GSHGEOM_Arc_Form f = {P(0, 4), P(4, 0), P(0, 4), GSHGEOM_ANGLE_DEGREES, -90.0, 270.0, GSHGEOM_ARC_CIRCULAR};
      gshgeom_path_arc_conjugate_to(path, &f, NULL);
      gshgeom_path_line_to(path, P(0, 0));
      break;
    }
    case 7: { /* a rounded rectangle built by shape-geometry, untagged */
      const GSHGEOM_Corner c[4] = {{1, 1}, {0.5, 1}, {1, 0.5}, {0, 0}};
      gshgeom_path_add_rounded_rect(path, 0, 0, 8, 6, c, 0, 0);
      break;
    }
    default: /* 3D */
      gshgeom_path_move_to(path, P(0, 0));
      gshgeom_path_line_to(path, P(1, 1));
      break;
  }
  return path;
}

/** An alpha mask over @p picture (a picture of @p store), or NULL where a limit refuses it: the soft-mask slot's value (MK1). */
static const GSHSCENE_Resource * mask_over(GSHSCENE_Store * store, const GSHSCENE_Resource * picture) {
  GSHSCENE_Mask m;
  memset(&m, 0, sizeof m);
  m.picture = picture;
  m.mode = GSHSCENE_MASK_ALPHA;
  const GSHSCENE_Resource * r = NULL;
  return gshscene_store_intern_mask(store, &m, &r) == GSHSCENE_OK ? r : NULL;
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
  for (int i = 0; i < 4; i++) {
    if (s->masks[i]) {
      CHECK(gshscene_resource_hash(s->masks[i]) == s->mask_hash[i]); /* MK13: a handle's hash never changes */
    }
  }
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

/* --- stroke styles from the input (strokes story S1: SK2's classes, SK3's canonical form) ----------------------------- */

/** A value of a style: zero of both signs, ordinary values, SK2's band edges and one step past each, and non-finite. */
static const double style_values[12] = {0.0, 1.0, 2.5, -0.0, 0x1p-64, 0x1p-65, 0x1p64, 0x1.0000000000001p64, -1.0,
    NAN, INFINITY, 3.0};

/** SK2's band, restated: zero, or a magnitude in [2^-64, 2^64]. */
static int style_within(double x) {
  double m = fabs(x);
  return m == 0.0 || (m >= 0x1p-64 && m <= 0x1p64);
}

/** SK3 (a), restated: the canonical dash count. */
static size_t style_dash_count(const double * dashes, size_t n) {
  int any = 0;
  for (size_t i = 0; i < n; i++) {
    if (dashes[i] < 0.0) {
      return 0;
    }
    any = any || dashes[i] != 0.0;
  }
  return !any ? 0 : (n % 2 ? 2 * n : n);
}

/** Whether @p x and @p y are the same double, bit for bit (so -0 is not 0). */
static int same_bits(double x, double y) {
  return memcmp(&x, &y, sizeof x) == 0;
}

/* --- masks (masks-images-patterns story M1: MK1 to MK6, MK9, MK11 to MK13) ---------------------------------------------- */

/** The whole of @p f as a malloc'd string (never the counting allocator's), closing it; NULL where it cannot be read. */
static char * slurp_close(FILE * f) {
  char * text = NULL;
  if (fflush(f) == 0 && fseek(f, 0, SEEK_END) == 0) {
    long n = ftell(f);
    if (n >= 0 && fseek(f, 0, SEEK_SET) == 0 && (text = malloc((size_t)n + 1)) != NULL) {
      size_t got = fread(text, 1, (size_t)n, f);
      text[got] = '\0';
    }
  }
  fclose(f);
  return text;
}

static char * resource_text(const GSHSCENE_Resource * r) {
  FILE * f = tmpfile();
  if (!f) {
    return NULL;
  }
  CHECK(gshscene_resource_dump(r, f) == GSHSCENE_OK);
  return slurp_close(f);
}

static char * list_text(const GSHSCENE_List * l) {
  FILE * f = tmpfile();
  if (!f) {
    return NULL;
  }
  CHECK(gshscene_list_dump(l, f) == GSHSCENE_OK);
  return slurp_close(f);
}

/** MK12 restated: the deepest picture a list names, as an instance or as a group's mask (0 for none). */
static size_t list_picture_depth(const GSHSCENE_List * list) {
  size_t depth = 0;
  for (size_t i = 0; i < gshscene_list_count(list); i++) {
    const GSHSCENE_Item * it;
    CHECK(gshscene_list_item(list, i, &it) == GSHSCENE_OK);
    size_t d = 0;
    if (it->kind == GSHSCENE_ITEM_INSTANCE) {
      d = gshscene_resource_picture_depth(it->resource);
    } else if (it->kind == GSHSCENE_ITEM_GROUP_PUSH && it->group.soft_mask) {
      GSHSCENE_Mask content;
      CHECK(gshscene_resource_mask(it->group.soft_mask, &content) == GSHSCENE_OK);
      d = gshscene_resource_picture_depth(content.picture);
    }
    depth = d > depth ? d : depth;
  }
  return depth;
}

/** A picture of one fill of a path recipe, made in @p store; NULL where a limit or an allocation refuses it. */
static const GSHSCENE_Resource * one_fill_picture(S * s, GSHSCENE_Store * store, unsigned recipe) {
  GSHGEOM_Path * path = make_path(s, recipe);
  const GSHSCENE_Resource * shape = NULL;
  const GSHSCENE_Resource * picture = NULL;
  GSHSCENE_List * l = NULL;
  if (path && gshscene_store_intern_path(store, path, &shape) == GSHSCENE_OK &&
      gshscene_list_create(&s->alloc, &s->limits, store, &l) == GSHSCENE_OK &&
      gshscene_list_fill(l, shape, NULL, GSHGEOM_FILL_NONZERO, NULL, 0, 0.0) == GSHSCENE_OK && gshscene_list_close(l) == GSHSCENE_OK &&
      gshscene_store_intern_picture(store, l, &picture) != GSHSCENE_OK) {
    picture = NULL;
  }
  gshscene_list_destroy(l);
  gshscene_resource_release(shape);
  gshgeom_path_destroy(path);
  return picture;
}

/** A space of a mask from a pick: 0 none, 1 to 3 carried, 4 one this library does not carry (ProPhoto), 5 half given. */
static GSHSCENE_Color_Space mask_space(unsigned sel, int * invalid, int * unsupported) {
  GSHSCENE_Color_Space sp;
  memset(&sp, 0, sizeof sp);
  switch (sel % 6) {
    case 1:
      sp.primaries = GCOL_PRIMARIES_SRGB;
      sp.transfer = GCOL_TRANSFER_LINEAR;
      break;
    case 2:
      sp.primaries = GCOL_PRIMARIES_DISPLAY_P3;
      sp.transfer = GCOL_TRANSFER_SRGB;
      break;
    case 3:
      sp.primaries = GCOL_PRIMARIES_ADOBE_RGB;
      sp.transfer = GCOL_TRANSFER_GAMMA;
      sp.gamma = 2.2;
      break;
    case 4:
      sp.primaries = GCOL_PRIMARIES_PROPHOTO;
      sp.transfer = GCOL_TRANSFER_LINEAR;
      *unsupported = 1;
      break;
    case 5:
      sp.primaries = GCOL_PRIMARIES_SRGB; /* a gamut with no transfer */
      *invalid = 1;
      break;
    default:
      break;
  }
  return sp;
}

static int space_is_zero(const GSHSCENE_Color_Space * sp) {
  return sp->primaries == GCOL_PRIMARIES_UNKNOWN && sp->transfer == GCOL_TRANSFER_UNKNOWN && same_bits(sp->gamma, 0.0);
}

static int space_same(const GSHSCENE_Color_Space * a, const GSHSCENE_Color_Space * b) {
  return a->primaries == b->primaries && a->transfer == b->transfer && same_bits(a->gamma, b->gamma);
}

#define MASK_BIG_PIECES 257
#define MASK_BIG_SAMPLES 4097

/** The features MK9 says a mask of these fields needs, restated. */
static uint32_t mask_features(const GSHSCENE_Mask * m) {
  uint32_t need = 0;
  if (m->mode == GSHSCENE_MASK_ALPHA) {
    need |= GSHSCENE_MASK_NATIVE_ALPHA;
  } else {
    need |= GSHSCENE_MASK_NATIVE_LUMINANCE;
    need |= GSHSCENE_MASK_NATIVE_COEFF_W3C << m->coefficients;
    if (m->backdrop[0] != 0.0 || m->backdrop[1] != 0.0 || m->backdrop[2] != 0.0) {
      need |= GSHSCENE_MASK_NATIVE_BACKDROP;
    }
  }
  need |= m->transfer.kind ? GSHSCENE_MASK_NATIVE_TRANSFER_EXPONENTIAL << (m->transfer.kind - 1) : 0u;
  if (!space_is_zero(&m->space) || !space_is_zero(&m->render_space) || !space_is_zero(&m->backdrop_space)) {
    need |= GSHSCENE_MASK_NATIVE_SPACES;
  }
  return need | (m->acts_on == GSHSCENE_MASK_ACTS_ON_SHAPE ? GSHSCENE_MASK_NATIVE_ACTS_ON_SHAPE : 0u);
}

/** Variant @p v of a small set of valid masks over @p picture (a picture of @p store). */
static const GSHSCENE_Resource * mask_variant(GSHSCENE_Store * store, const GSHSCENE_Resource * picture, unsigned v) {
  static const double table[3] = {0.0, 0.25, 1.0};
  GSHSCENE_Mask m;
  memset(&m, 0, sizeof m);
  m.picture = picture;
  m.mode = GSHSCENE_MASK_ALPHA;
  switch (v % 6) {
    case 1:
      m.mode = GSHSCENE_MASK_LUMINANCE;
      break;
    case 2:
      m.mode = GSHSCENE_MASK_LUMINANCE;
      m.coefficients = GSHSCENE_MASK_COEFF_REC709;
      m.space.primaries = GCOL_PRIMARIES_SRGB;
      m.space.transfer = GCOL_TRANSFER_LINEAR;
      m.backdrop[1] = 0.5;
      break;
    case 3:
      m.acts_on = GSHSCENE_MASK_ACTS_ON_SHAPE;
      m.transfer.kind = GSHSCENE_TRANSFER_TABLE;
      m.transfer.sample_count = 3;
      m.transfer.samples = table;
      break;
    case 4:
      m.transfer.kind = GSHSCENE_TRANSFER_EXPONENTIAL;
      m.transfer.c1 = 1.0;
      m.transfer.n = 2.0;
      break;
    case 5:
      m.mode = GSHSCENE_MASK_LUMINANCE;
      m.coefficients = GSHSCENE_MASK_COEFF_LUM;
      break;
    default:
      break;
  }
  const GSHSCENE_Resource * r = NULL;
  return gshscene_store_intern_mask(store, &m, &r) == GSHSCENE_OK ? r : NULL;
}

/** MK1 to MK4, MK9, MK13: a mask from the input, its result class predicted and its content read back. */
static void fuzz_mask(S * s) {
  const GSHSCENE_Limits * lim = &s->limits;
  unsigned psel = pick(s, 11), mode = pick(s, 4), coeff = pick(s, 5), acts = pick(s, 3);
  unsigned lsel = pick(s, 6), rsel = pick(s, 6), bsel = pick(s, 6), back = pick(s, 6), tsel = pick(s, 13);
  unsigned slot = pick(s, 4), clean = pick(s, 4);
  /* Three in four of the alpha masks are spelled as MK1 asks (no coefficient set, no luminance space, no backdrop). */
  if (clean && mode == GSHSCENE_MASK_ALPHA) {
    coeff = 0;
    lsel = 0;
    bsel = 0;
    back = 0;
  }
  const GSHSCENE_Resource * foreign = NULL;
  const GSHSCENE_Resource * pic = NULL;
  if (psel < POOL) {
    pic = s->pictures[psel];
  } else if (psel == POOL) {
    pic = s->res[0]; /* a path */
  } else if (psel == POOL + 1 && s->scene) {
    foreign = one_fill_picture(s, gshscene_scene_store(s->scene), 2); /* a picture of another store */
    pic = foreign;
  }
  int pic_ok = psel < POOL && pic != NULL;
  int invalid = !pic_ok, unsupported = 0, limit = 0;
  GSHSCENE_Mask m;
  memset(&m, 0, sizeof m);
  m.picture = pic;
  m.mode = mode;
  m.coefficients = coeff;
  m.acts_on = acts;
  m.space = mask_space(lsel, &invalid, &unsupported);
  m.render_space = mask_space(rsel, &invalid, &unsupported);
  m.backdrop_space = mask_space(bsel, &invalid, &unsupported);
  switch (back) {
    case 1:
      m.backdrop[0] = 0.25;
      m.backdrop[1] = 0.5;
      m.backdrop[2] = 1.0;
      break;
    case 2:
      m.backdrop[0] = 0x1p16;
      m.backdrop[1] = -0x1p16;
      break;
    case 3:
      m.backdrop[0] = 0x1.0000000000001p16;
      limit = 1;
      break;
    case 4:
      m.backdrop[0] = (double)NAN;
      invalid = 1;
      break;
    case 5:
      m.backdrop[0] = -0.0;
      break;
    default:
      break;
  }
  int backdrop_set = m.backdrop[0] != 0.0 || m.backdrop[1] != 0.0 || m.backdrop[2] != 0.0;
  GSHSCENE_Transfer_Piece pieces[MASK_BIG_PIECES];
  static const double two[2] = {0.0, 1.0};
  static const double one[1] = {0.5};
  static const double big[MASK_BIG_SAMPLES];
  GSHSCENE_Transfer * t = &m.transfer;
  switch (tsel) {
    case 2:
      t->kind = GSHSCENE_TRANSFER_EXPONENTIAL;
      t->c0 = 0.1;
      t->c1 = 0.9;
      t->n = 2.0;
      break;
    case 3:
      t->kind = GSHSCENE_TRANSFER_EXPONENTIAL;
      t->n = -1.0;
      invalid = 1;
      break;
    case 4:
      t->kind = GSHSCENE_TRANSFER_EXPONENTIAL;
      t->c1 = 1.0;
      t->n = 0x1p16;
      break;
    case 5:
      t->kind = GSHSCENE_TRANSFER_EXPONENTIAL;
      t->c1 = 1.0;
      t->n = 0x1.0000000000001p16;
      limit = 1;
      break;
    case 6:
      t->kind = GSHSCENE_TRANSFER_TABLE;
      t->sample_count = 2;
      t->samples = two;
      break;
    case 7:
      t->kind = GSHSCENE_TRANSFER_TABLE;
      t->sample_count = 1;
      t->samples = one;
      invalid = 1;
      break;
    case 8:
    case 9:
    case 10:
    case 11: {
      size_t k = tsel == 10 ? 257 : tsel == 11 ? 256 : 2;
      t->kind = GSHSCENE_TRANSFER_STITCHING;
      t->piece_count = k;
      t->pieces = pieces;
      memset(pieces, 0, sizeof pieces);
      for (size_t i = 0; i < k; i++) {
        pieces[i].lo = (double)i / (double)k;
        pieces[i].hi = (double)(i + 1) / (double)k;
        pieces[i].e0 = 0.0;
        pieces[i].e1 = 1.0;
        pieces[i].leaf_kind = i % 2 ? GSHSCENE_TRANSFER_EXPONENTIAL : GSHSCENE_TRANSFER_IDENTITY;
        pieces[i].c1 = i % 2 ? 1.0 : 0.0;
        pieces[i].n = i % 2 ? 0.5 : 0.0;
      }
      if (tsel == 9) {
        pieces[1].lo = 0.75; /* a gap */
        invalid = 1;
      }
      limit = limit || k > GSHSCENE_MASK_MAX_PIECES;
      break;
    }
    case 12:
      t->kind = GSHSCENE_TRANSFER_TABLE;
      t->sample_count = MASK_BIG_SAMPLES;
      t->samples = big;
      limit = 1;
      break;
    default:
      break;
  }
  int alpha = mode == GSHSCENE_MASK_ALPHA;
  invalid = invalid || mode == 0 || mode >= GSHSCENE_MASK_MODE_COUNT || coeff >= GSHSCENE_MASK_COEFF_COUNT ||
      acts >= GSHSCENE_MASK_ACTS_ON_COUNT ||
      (alpha && (coeff != 0 || lsel % 6 != 0 || bsel % 6 != 0 || backdrop_set));
  GSHSCENE_Result want = invalid ? GSHSCENE_ERR_INVALID : unsupported ? GSHSCENE_ERR_UNSUPPORTED : limit ? GSHSCENE_ERR_LIMIT : GSHSCENE_OK;
  const GSHSCENE_Resource * made = NULL;
  GSHSCENE_Store_Stats before, after;
  CHECK(gshscene_store_stats(s->store, &before) == GSHSCENE_OK);
  GSHSCENE_Result r = gshscene_store_intern_mask(s->store, &m, &made);
  CHECK(gshscene_store_stats(s->store, &after) == GSHSCENE_OK);
  CHECK(r != GSHSCENE_ERR_INTERNAL && r != GSHSCENE_ERR_OOM && r != GSHSCENE_ERR_CORRUPT && r != GSHSCENE_ERR_IO);
  if (want != GSHSCENE_OK) {
    CHECK(r == want); /* MK1: invalid, then unsupported, then a bound */
    outcomes[OUT_MASK_INVALID] += want == GSHSCENE_ERR_INVALID;
    outcomes[OUT_MASK_UNSUPPORTED] += want == GSHSCENE_ERR_UNSUPPORTED;
    outcomes[OUT_MASK_LIMIT] += want == GSHSCENE_ERR_LIMIT;
  } else if (r != GSHSCENE_OK) {
    CHECK(r == GSHSCENE_ERR_LIMIT); /* only the store's own limits refuse what MK1 accepts */
    /* A mask is accounted at the base plus its stitching pieces and its table's samples (MK4), so one whose transfer
     * has any costs more than the base: the bound is that cost, not the base alone.  (The samples of a piece's own
     * table are left out, which can only make the bound lower than the cost.) */
    size_t cost = GSHSCENE_BYTES_BASE;
    if (t->kind == GSHSCENE_TRANSFER_STITCHING) {
      cost += t->piece_count * GSHSCENE_BYTES_PER_TRANSFER_PIECE;
    } else if (t->kind == GSHSCENE_TRANSFER_TABLE) {
      cost += t->sample_count * GSHSCENE_BYTES_PER_TRANSFER_SAMPLE;
    }
    CHECK(before.live >= lim->max_resources || before.bytes + cost > lim->max_bytes);
    outcomes[OUT_MASK_LIMIT]++;
  }
  if (r != GSHSCENE_OK) {
    CHECK(made == NULL && before.live == after.live && before.bytes == after.bytes && before.created == after.created);
    gshscene_resource_release(foreign);
    return;
  }
  outcomes[OUT_MASK_INTERNED]++;
  outcomes[OUT_MASK_LUMINANCE] += mode == GSHSCENE_MASK_LUMINANCE;
  outcomes[OUT_MASK_TRANSFER] += t->kind != GSHSCENE_TRANSFER_IDENTITY;
  CHECK(gshscene_resource_kind(made) == GSHSCENE_RESOURCE_MASK);
  uint64_t hash = gshscene_resource_hash(made);
  CHECK(hash != 0);
  GSHSCENE_Mask back_m;
  CHECK(gshscene_resource_mask(made, &back_m) == GSHSCENE_OK);
  CHECK(back_m.picture == pic && back_m.mode == mode && back_m.coefficients == coeff && back_m.acts_on == acts);
  CHECK(space_same(&back_m.space, &m.space) && space_same(&back_m.render_space, &m.render_space) &&
        space_same(&back_m.backdrop_space, &m.backdrop_space));
  for (int i = 0; i < 3; i++) {
    CHECK(same_bits(back_m.backdrop[i], m.backdrop[i] == 0.0 ? 0.0 : m.backdrop[i])); /* -0 is spelled +0 */
  }
  CHECK(back_m.transfer.kind == t->kind && back_m.transfer.sample_count == t->sample_count &&
        back_m.transfer.piece_count == t->piece_count);
  CHECK(gshscene_mask_required(made) == mask_features(&back_m));
  /* The transfer is a number in [0, 1] whatever the input (MK2, MK4). */
  for (int i = 0; i < 5; i++) {
    double v = gshscene_mask_transfer(made, i / 4.0 * 1.5 - 0.25);
    CHECK(v >= 0.0 && v <= 1.0);
  }
  /* Found again: one handle (MK1), and the hash is the handle's (MK13). */
  const GSHSCENE_Resource * again = NULL;
  CHECK(gshscene_store_intern_mask(s->store, &m, &again) == GSHSCENE_OK && again == made);
  CHECK(gshscene_resource_hash(again) == hash);
  gshscene_resource_release(again);
  outcomes[OUT_MASK_HIT]++;
  /* The nearest neighbor, one field away (the acting-on), is another handle with another hash. */
  GSHSCENE_Mask near_m = m;
  near_m.acts_on = acts ^ 1u;
  const GSHSCENE_Resource * other = NULL;
  GSHSCENE_Result nr = gshscene_store_intern_mask(s->store, &near_m, &other);
  CHECK(nr == GSHSCENE_OK || nr == GSHSCENE_ERR_LIMIT);
  if (nr == GSHSCENE_OK) {
    CHECK(other != made && gshscene_resource_hash(other) != hash);
    GSHSCENE_Mask other_m;
    CHECK(gshscene_resource_mask(other, &other_m) == GSHSCENE_OK && other_m.acts_on == (acts ^ 1u));
    gshscene_resource_release(other);
    outcomes[OUT_MASK_NEAR]++;
  }
  /* The dump is the content's: twice the same, naming the picture by hash and never holding its items. */
  char * d0 = resource_text(made);
  char * d1 = resource_text(made);
  if (d0 && d1) {
    CHECK(strcmp(d0, d1) == 0 && strstr(d0, "picture=") != NULL && strstr(d0, "mode=") != NULL);
  }
  free(d0);
  free(d1);
  /* A mask in the pool is checked at every hold (hold_check): its hash is its content's, for as long as it is held. */
  CHECK(!s->masks[slot] || gshscene_resource_hash(s->masks[slot]) == s->mask_hash[slot]);
  gshscene_resource_release(s->masks[slot]);
  s->masks[slot] = made;
  s->mask_hash[slot] = hash;
  gshscene_resource_release(foreign);
}

/** A list of one masked group (and a fill in it), closed, in the standalone store; NULL where a limit refuses it. */
static GSHSCENE_List * masked_pair(S * s, const GSHSCENE_Group * g, const GSHSCENE_Resource * fill) {
  GSHSCENE_List * l = NULL;
  if (gshscene_list_create(&s->alloc, &s->limits, s->store, &l) != GSHSCENE_OK) {
    return NULL;
  }
  if (gshscene_list_group_push(l, g, 0, 0.0) != GSHSCENE_OK ||
      (fill && gshscene_list_fill(l, fill, NULL, GSHGEOM_FILL_NONZERO, NULL, 0, 0.0) != GSHSCENE_OK) ||
      gshscene_list_group_pop(l) != GSHSCENE_OK || gshscene_list_close(l) != GSHSCENE_OK) {
    gshscene_list_destroy(l);
    return NULL;
  }
  return l;
}

/** MK6, MK12, MK13: a group recorded with a mask of the pool and a placement; a list of it equal to another alike. */
static void fuzz_mask_group(S * s) {
  unsigned msel = pick(s, 5), psel = pick(s, 8), isol = pick(s, 2), ko = pick(s, 2), op = pick(s, 5);
  if (!s->list || gshscene_list_is_closed(s->list)) {
    return; /* a closed list refuses every push first, whatever the group (checked where lists close) */
  }
  const GSHSCENE_Resource * mask = msel < 4 ? s->masks[msel] : NULL;
  GSHSCENE_Group g;
  memset(&g, 0, sizeof g);
  g.opacity = op / 4.0;
  g.isolation = isol ? GSHSCENE_ISOLATION_NON_ISOLATED : GSHSCENE_ISOLATION_ISOLATED;
  g.knockout = (uint32_t)ko;
  g.soft_mask = mask;
  GSHGEOM_Map placement;
  gshgeom_map_identity(2, &placement);
  int placed = 1;
  GSHSCENE_Result want = GSHSCENE_OK;
  switch (psel) {
    case 0:
      placed = 0; /* the zero-filled map: the identity */
      break;
    case 1:
      break; /* an identity given */
    case 2:
      gshgeom_map_translate(2, 3.0, -2.0, 0.0, &placement);
      want = mask ? GSHSCENE_OK : GSHSCENE_ERR_INVALID; /* only a group with a mask places one */
      break;
    case 3:
      gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, 90.0, &placement);
      want = mask ? GSHSCENE_OK : GSHSCENE_ERR_INVALID;
      break;
    case 4:
      gshgeom_map_scale(2, 0.0, 1.0, 1.0, &placement); /* singular: legal (MK5) */
      want = mask ? GSHSCENE_OK : GSHSCENE_ERR_INVALID;
      break;
    case 5:
      placement.m[0][0] = (double)NAN;
      want = GSHSCENE_ERR_INVALID;
      break;
    case 6:
      gshgeom_map_identity(3, &placement); /* a mask is planar */
      want = mask ? GSHSCENE_ERR_UNSUPPORTED : GSHSCENE_ERR_INVALID;
      break;
    default:
      gshgeom_map_translate(2, 1.0, 1.0, 0.0, &placement);
      want = mask ? GSHSCENE_OK : GSHSCENE_ERR_INVALID;
      break;
  }
  if (placed) {
    g.soft_mask_map = placement;
  }
  ListSnap b = snap(s->list);
  GSHSCENE_Result r = gshscene_list_group_push(s->list, &g, 0, 0.0);
  if (want != GSHSCENE_OK) {
    CHECK(r == want);
    outcomes[OUT_MASK_GROUP_INVALID] += want == GSHSCENE_ERR_INVALID;
    outcomes[OUT_MASK_GROUP_UNSUPPORTED] += want == GSHSCENE_ERR_UNSUPPORTED;
    list_after(s, b, r, 1, 0);
    return;
  }
  list_after(s, b, r, 1, 0);
  if (r != GSHSCENE_OK) {
    return;
  }
  const GSHSCENE_Item * item = NULL;
  CHECK(gshscene_list_item(s->list, gshscene_list_count(s->list) - 1, &item) == GSHSCENE_OK);
  CHECK(item->group.soft_mask == mask);
  if (mask) {
    const GSHGEOM_Map * got = &item->group.soft_mask_map;
    CHECK(got->dim == 2);
    for (int i = 0; i < 2; i++) {
      CHECK(same_bits(got->t[i], placed ? placement.t[i] : 0.0));
      for (int j = 0; j < 2; j++) {
        CHECK(same_bits(got->m[i][j], placed ? placement.m[i][j] : (i == j ? 1.0 : 0.0)));
      }
    }
    /* MK12: a list holding a masked group is as deep as the mask's picture. */
    GSHSCENE_Mask content;
    CHECK(gshscene_resource_mask(mask, &content) == GSHSCENE_OK);
    CHECK(list_picture_depth(s->list) >= gshscene_resource_picture_depth(content.picture));
    outcomes[OUT_MASK_GROUP]++;
  }
  /* Equality by content (MK13): a second list of the same group is equal, with the same hash and the same dump; one with
   * another mask or another placement is not. */
  if (mask) {
    GSHSCENE_List * a = masked_pair(s, &g, s->res[0]);
    GSHSCENE_List * c = masked_pair(s, &g, s->res[0]);
    if (a && c) {
      CHECK(gshscene_list_equal(a, c) && gshscene_list_hash(a) == gshscene_list_hash(c));
      char * da = list_text(a);
      char * dc = list_text(c);
      if (da && dc) {
        CHECK(strcmp(da, dc) == 0 && strstr(da, "mask=") != NULL && strstr(da, "maskmap=") != NULL);
      }
      free(da);
      free(dc);
      outcomes[OUT_MASK_LIST_EQUAL]++;
      GSHSCENE_Group h = g;
      GSHGEOM_Map moved;
      gshgeom_map_translate(2, 7.5, 0.0, 0.0, &moved);
      h.soft_mask_map = moved;
      GSHSCENE_List * d = masked_pair(s, &h, s->res[0]);
      if (d) {
        CHECK(!gshscene_list_equal(a, d) && gshscene_list_hash(a) != gshscene_list_hash(d)); /* the placement is hashed and compared */
        gshscene_list_destroy(d);
      }
      for (unsigned k = 0; k < 4; k++) {
        if (s->masks[k] && s->masks[k] != mask) {
          h = g;
          h.soft_mask = s->masks[k];
          GSHSCENE_List * e = masked_pair(s, &h, s->res[0]);
          if (e) {
            CHECK(!gshscene_list_equal(a, e) && gshscene_list_hash(a) != gshscene_list_hash(e)); /* the mask is hashed and compared */
            gshscene_list_destroy(e);
          }
          break;
        }
      }
    }
    gshscene_list_destroy(a);
    gshscene_list_destroy(c);
  }
}

/** MK6, MK11, MK13: a group node of a scene of its own given a mask and a placement, published, diffed and emitted. */
static void fuzz_mask_node(S * s) {
  unsigned va = pick(s, 6), vb = pick(s, 6), psel = pick(s, 5), kind = pick(s, 3), mapsel = pick(s, 3);
  GSHSCENE_Scene * sc = NULL;
  if (gshscene_scene_create(&s->alloc, NULL, NULL, &sc) != GSHSCENE_OK) {
    return;
  }
  GSHSCENE_Store * store = gshscene_scene_store(sc);
  GSHSCENE_Result ignored;
  const GSHSCENE_Resource * picture = one_fill_picture(s, store, 2);
  const GSHSCENE_Resource * shape = intern_recipe(s, store, 2, &ignored);
  const GSHSCENE_Resource * ma = picture ? mask_variant(store, picture, va) : NULL;
  const GSHSCENE_Resource * mb = picture ? mask_variant(store, picture, vb + (vb == va ? 1u : 0u)) : NULL;
  GSHSCENE_Edit * e = NULL;
  GSHSCENE_Problems * problems = NULL;
  const GSHSCENE_Version * v[4] = {NULL, NULL, NULL, NULL};
  if (ma && mb && shape && gshscene_scene_open_edit(sc, NULL, &e) == GSHSCENE_OK) {
    GSHSCENE_Node g;
    gshscene_node_init(GSHSCENE_NODE_GROUP, &g);
    GSHGEOM_Map node_map;
    gshgeom_map_translate(2, mapsel * 10.0, 0.0, 0.0, &node_map);
    g.map = node_map;
    /* A placement with no mask is invalid (MK13); one with a mask is stored. */
    GSHSCENE_Node bad = g;
    gshgeom_map_translate(2, 2.0, 3.0, 0.0, &bad.soft_mask_map);
    uint64_t gid = 0, sid = 0;
    int built = gshscene_edit_add(e, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, &g, NULL, &gid) == GSHSCENE_OK;
    if (built) {
      CHECK(gshscene_edit_set(e, gid, &bad) == GSHSCENE_ERR_INVALID);
      outcomes[OUT_MASK_NODE_INVALID]++;
      GSHSCENE_Node sh;
      gshscene_node_init(GSHSCENE_NODE_SHAPE, &sh);
      sh.resource = shape;
      built = gshscene_edit_add(e, gid, GSHSCENE_APPEND, &sh, NULL, &sid) == GSHSCENE_OK;
    }
    GSHSCENE_Edit * one[1] = {e};
    const GSHSCENE_Version * out = NULL;
    if (built && gshscene_scene_merge(sc, one, 1, &problems, &out) == GSHSCENE_OK) {
      v[0] = out; /* a reference for the caller */
      /* Version 1 holds the mask ma placed by q; version 2 the same again (nothing changes); version 3 the mask mb. */
      GSHGEOM_Map q;
      gshgeom_map_identity(2, &q);
      switch (psel) {
        case 1: gshgeom_map_translate(2, 4.0, 1.0, 0.0, &q); break;
        case 2: gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, 90.0, &q); break;
        case 3: gshgeom_map_scale(2, 0.0, 1.0, 1.0, &q); break;
        default: break;
      }
      const GSHSCENE_Resource * masks[4] = {NULL, ma, ma, mb};
      for (int step = 1; step < 4; step++) {
        GSHSCENE_Edit * ed = NULL;
        if (gshscene_scene_open_edit(sc, v[step - 1], &ed) != GSHSCENE_OK) {
          break;
        }
        GSHSCENE_Node n = g;
        n.soft_mask = masks[step];
        n.soft_mask_map = q;
        n.isolation = kind == 1 ? GSHSCENE_ISOLATION_NON_ISOLATED : GSHSCENE_ISOLATION_ISOLATED;
        CHECK(gshscene_edit_set(ed, gid, &n) == GSHSCENE_OK);
        outcomes[OUT_MASK_NODE]++;
        GSHSCENE_Edit * list[1] = {ed};
        const GSHSCENE_Version * merged = NULL;
        GSHSCENE_Result mr = gshscene_scene_merge(sc, list, 1, &problems, &merged);
        gshscene_edit_destroy(ed);
        if (mr != GSHSCENE_OK) {
          break;
        }
        v[step] = merged;
      }
      for (int step = 1; step < 4 && v[step]; step++) {
        GSHSCENE_Diff * d = NULL;
        GSHSCENE_Damage dmg;
        CHECK(gshscene_diff(v[step - 1], v[step], NULL, NULL, &d) == GSHSCENE_OK);
        CHECK(gshscene_diff_damage(d, &dmg) == GSHSCENE_OK);
        if (step == 2) {
          /* The same handle and the same placement: nothing changed, nothing damaged (MK11). */
          CHECK(gshscene_diff_count(d) == 0 && dmg.empty && !dmg.unbounded);
          outcomes[OUT_MASK_NO_DAMAGE]++;
        } else {
          /* A mask added or swapped: a change of the group, and its content damaged (MK11). */
          CHECK(gshscene_diff_count(d) == 1 && !dmg.empty);
          uint64_t id;
          GSHSCENE_Diff_Kind dk;
          CHECK(gshscene_diff_get(d, 0, &id, &dk) == GSHSCENE_OK && id == gid);
          outcomes[OUT_MASK_DAMAGE]++;
        }
        gshscene_diff_destroy(d);
        /* The emitted list names the mask and the placement composed with the node's map (MK6). */
        const GSHSCENE_List * emitted = NULL;
        if (gshscene_version_list(v[step], &emitted) == GSHSCENE_OK) {
          int pushes = 0;
          for (size_t i = 0; i < gshscene_list_count(emitted); i++) {
            const GSHSCENE_Item * it;
            CHECK(gshscene_list_item(emitted, i, &it) == GSHSCENE_OK);
            if (it->kind == GSHSCENE_ITEM_GROUP_PUSH && it->group.soft_mask) {
              GSHGEOM_Map want;
              CHECK(it->group.soft_mask == masks[step]);
              CHECK(gshgeom_map_compose(&node_map, &q, &want) == GSHGEOM_OK);
              CHECK(gshgeom_map_hash(&want) == gshgeom_map_hash(&it->group.soft_mask_map));
              pushes++;
            }
          }
          CHECK(pushes == 1);
          outcomes[OUT_MASK_EMITTED]++;
        }
      }
    }
  }
  for (int step = 0; step < 4; step++) {
    gshscene_version_release(v[step]);
  }
  gshscene_problems_destroy(problems);
  gshscene_edit_destroy(e);
  gshscene_resource_release(ma);
  gshscene_resource_release(mb);
  gshscene_resource_release(shape);
  gshscene_resource_release(picture);
  gshscene_scene_destroy(sc);
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

/* --- gradients built from the input (story P2: every kind, extend and stop shape, maps singular and not, values at
 * and past G14's bounds) -------------------------------------------------------------------------------------------- */

/** A value of a gradient: small integers and fractions, G14's bounds and one step past each. */
static double gradient_value(unsigned b, int * beyond) {
  static const double table[16] = {0.0, 1.0, -1.0, 0.5, 3.0, -2.5, 7.0, 0.1, 0x1p64, -0x1p64, 0x1p-64, -0x1p-64,
      0x1.0000000000001p64, 0x1.fffffffffffffp-65, 1e-300, 12.0};
  double v = table[b % 16];
  double m = v < 0 ? -v : v;
  if (m != 0.0 && (m > 0x1p64 || m < 0x1p-64)) {
    *beyond = 1;
  }
  return v;
}

/**
 * Intern a gradient from the input into @p store.  G14 is asserted by its bound: a gradient none of whose values is past
 * the bounds is never refused with ERR_LIMIT for them (the store's own limits aside), and one with such a value never
 * interns.
 */
static GSHSCENE_Result fuzz_gradient(S * s, GSHSCENE_Store * store, const GSHSCENE_Resource ** out) {
  int beyond = 0;
  GSHSCENE_Gradient d;
  memset(&d, 0, sizeof d);
  d.kind = 1u + pick(s, 3);
  /* sparse values: most draw a small number, one byte in four draws from the whole table */
  double v[6];
  for (int k = 0; k < 6; k++) {
    unsigned b = arg(s);
    v[k] = b % 4 == 0 ? gradient_value(b / 4, &beyond) : (double)((int)(b % 13) - 6) / (b % 3 == 0 ? 1.0 : 2.0);
  }
  if (d.kind == GSHSCENE_GRADIENT_LINEAR) {
    d.p0[0] = v[0], d.p0[1] = v[1], d.p1[0] = v[2], d.p1[1] = v[3];
    if (pick(s, 3) == 0) {
      d.d[0] = v[4], d.d[1] = v[5];
    }
  } else if (d.kind == GSHSCENE_GRADIENT_RADIAL) {
    d.c0[0] = v[0], d.c0[1] = v[1], d.r0 = v[2], d.c1[0] = v[3], d.c1[1] = v[4], d.r1 = v[5];
  } else {
    d.center[0] = v[0], d.center[1] = v[1];
    d.angle_unit = pick(s, 4);
    double turn = d.angle_unit == GSHGEOM_ANGLE_DEGREES ? 45.0 : d.angle_unit == GSHGEOM_ANGLE_GRADIANS ? 50.0
                : d.angle_unit == GSHGEOM_ANGLE_TURNS ? 0.125 : 0.75;
    d.a0 = v[2] * turn, d.a1 = v[3] * turn;
  }
  d.extend = pick(s, 3);
  d.premultiplied = pick(s, 2);
  d.space = pool_space(arg(s) % 6); /* the target's, or a space every sink can carry */
  unsigned mk = pick(s, 8);
  if (mk == 6) {
    const double m[4] = {1.0, 2.0, 2.0, 4.0}; /* rank one: G11 */
    const double t[2] = {0.0, 0.0};
    gshgeom_map_from_matrix(2, m, t, &d.map);
  } else if (mk == 7) {
    const double m[4] = {gradient_value(arg(s), &beyond), 0.25, 0.0, 1.5};
    const double t[2] = {gradient_value(arg(s), &beyond), 1.0};
    if (gshgeom_map_from_matrix(2, m, t, &d.map) != GSHGEOM_OK) {
      gshgeom_map_identity(2, &d.map);
    }
  } else {
    int use;
    d.map = pool_map(mk, &use);
  }
  /* The stops: none, one, several (unsorted, equal offsets), parameter stops, or past the count. */
  static GSHSCENE_Gradient_Stop stops[6];
  memset(stops, 0, sizeof stops);
  unsigned n = pick(s, 7);
  for (unsigned i = 0; i < n; i++) {
    unsigned ob = arg(s);
    stops[i].offset = ob % 8 == 0 ? gradient_value(ob / 8, &beyond) : (double)(ob % 9) / 8.0 - 0.25;
    unsigned k = pick(s, 4);
    stops[i].color.kind = k < 2 ? GSHSCENE_PAINT_SOLID : k == 2 ? GSHSCENE_PAINT_FOREGROUND : GSHSCENE_PAINT_PALETTE;
    stops[i].color.rgba[3] = (double)pick(s, 5) / 4.0;
    if (stops[i].color.kind == GSHSCENE_PAINT_SOLID) {
      stops[i].color.rgba[0] = (double)pick(s, 3) / 2.0;
      stops[i].color.rgba[1] = (double)pick(s, 3) / 2.0;
      /* a solid stop in the target's space only where the gradient interpolates in it (G3) */
      stops[i].color.space = gshscene_color_space_is_target(&d.space) ? pool_space(arg(s) % 6) : pool_space(1 + arg(s) % 5);
    } else if (stops[i].color.kind == GSHSCENE_PAINT_PALETTE) {
      stops[i].color.entry = pick(s, 3);
    }
  }
  d.stops = n ? stops : NULL;
  d.stop_count = n;
  GSHSCENE_Gradient_Stop * many = NULL;
  if (pick(s, 64) == 0) {
    /* G14's stop count: the bound, or one past it */
    size_t count = GSHSCENE_GRADIENT_STOPS_MAX + pick(s, 2);
    many = calloc(count, sizeof *many);
    CHECK(many != NULL);
    for (size_t i = 0; i < count; i++) {
      many[i].offset = (double)(i % 5);
      many[i].color.kind = GSHSCENE_PAINT_FOREGROUND;
      many[i].color.rgba[3] = 1.0;
    }
    d.stops = many;
    d.stop_count = count;
  }
  /* G14's bound, from the values the gradient holds (the table's draws above may have gone unused or been scaled). */
  beyond = d.stop_count > GSHSCENE_GRADIENT_STOPS_MAX;
  const double held[22] = {d.p0[0], d.p0[1], d.p1[0], d.p1[1], d.d[0], d.d[1], d.c0[0], d.c0[1], d.r0, d.c1[0], d.c1[1],
      d.r1, d.center[0], d.center[1], d.a0, d.a1, d.map.m[0][0], d.map.m[0][1], d.map.m[1][0], d.map.m[1][1], d.map.t[0],
      d.map.t[1]};
  for (size_t i = 0; i < 22 + d.stop_count; i++) {
    double x = i < 22 ? held[i] : d.stops[i - 22].offset;
    double m = x < 0 ? -x : x;
    beyond |= m != 0.0 && (m > 0x1p64 || m < 0x1p-64);
  }
  GSHSCENE_Result r = gshscene_store_intern_gradient(store, &d, out);
  CHECK(r == GSHSCENE_OK || r == GSHSCENE_ERR_LIMIT || r == GSHSCENE_ERR_OOM);
  if (beyond) {
    CHECK(r != GSHSCENE_OK); /* G14: past a bound never interns */
    outcomes[OUT_GRADIENT_LIMIT] += r == GSHSCENE_ERR_LIMIT;
  }
  free(many);
  return r;
}

/* --- images (masks-images-patterns story I1) ------------------------------------------------------------------------- */

/** One sample of an image from the input's base byte, as its value (the bit pattern for a float). */
static uint32_t image_value(unsigned base, unsigned bits, int is_float, unsigned x, unsigned y, unsigned c) {
  if (is_float) {
    float f = (float)((base + 3u * x + y + 5u * c) % 61u) / 64.0f;
    uint32_t v;
    memcpy(&v, &f, sizeof v);
    return v;
  }
  if (bits == 8) {
    return (base * 13u + 7u * x + 13u * y + 29u * c) & 0xFFu;
  }
  if (bits == 12) {
    return (base * 53u + 37u * x + 101u * y + 263u * c) % 4096u;
  }
  return (base * 4099u + 257u * x + 911u * y + 4099u * c) & 0xFFFFu;
}

static void image_put(unsigned char * at, unsigned bits, uint32_t v) {
  if (bits <= 8) {
    *at = (unsigned char)v;
  } else if (bits <= 16) {
    uint16_t w = (uint16_t)v;
    memcpy(at, &w, sizeof w);
  } else {
    memcpy(at, &v, sizeof v);
  }
}

static GSHSCENE_Result fuzz_image(S * s, int take) {
  const GSHSCENE_Limits * lim = &s->limits;
  unsigned model = pick(s, 4);   /* gray, RGB, RGBA, CMYK */
  unsigned kind = pick(s, 6);    /* 8, 12, 16 bits, float, 10 bits, planar */
  unsigned wsel = pick(s, 7);
  unsigned hsel = pick(s, 5);
  unsigned pad = pick(s, 5);
  unsigned space_sel = pick(s, 10);
  unsigned flag_sel = pick(s, 6);
  unsigned reserved_sel = pick(s, 8);
  unsigned premult = pick(s, 2);
  unsigned bad_sel = pick(s, 10);
  unsigned hostile = pick(s, 20);
  unsigned base = arg(s);
  unsigned slot = pick(s, 3);
  static const unsigned widths[7] = {1, 2, 3, 63, 64, 65, 129};
  static const unsigned heights[5] = {1, 2, 64, 65, 3};
  static const unsigned hostile_w[4] = {65537, 1, 40000, 65536};
  static const unsigned hostile_h[4] = {1, 65537, 40000, 65537};
  unsigned w = widths[wsel];
  unsigned h = heights[hsel];
  if (hostile == 0) {
    w = hostile_w[wsel % 4];
    h = hostile_h[wsel % 4];
  }
  unsigned bits = kind == 0 || kind == 5 ? 8u : kind == 1 ? 12u : kind == 2 ? 16u : kind == 3 ? 32u : 10u;
  int is_float = kind == 3;
  if (kind == 5) {
    model = 1; /* planar RGB */
  }

  GIMG_Pixel_Format format;
  memset(&format, 0, sizeof format);
  unsigned layout;
  if (model == 3) {
    format = GIMG_PIXEL_CMYK8;
    layout = 4;
    bits = 8;
    is_float = 0;
  } else {
    layout = model == 0 ? 1u : model == 1 ? 3u : 4u;
    format.channel_model = model == 0 ? GIMG_CHANNEL_GRAY : model == 1 ? GIMG_CHANNEL_RGB : GIMG_CHANNEL_RGBA;
    format.channel_type = is_float ? GIMG_CHANNEL_FLOAT : GIMG_CHANNEL_UNORM;
    format.layout = kind == 5 ? GIMG_LAYOUT_PLANAR : GIMG_LAYOUT_INTERLEAVED;
    format.channel_count = (uint8_t)layout;
    for (unsigned c = 0; c < layout; c++) {
      format.bits_per_channel[c] = (uint8_t)bits;
    }
    format.alignment = 1;
    format.alpha_form = (uint8_t)(model == 2 ? (premult ? GIMG_ALPHA_PREMULTIPLIED : GIMG_ALPHA_STRAIGHT) : GIMG_ALPHA_NONE);
  }
  size_t sample_bytes = bits <= 8 ? 1u : bits <= 16 ? 2u : 4u;
  size_t row = (size_t)w * layout * sample_bytes;
  size_t stride = row + pad;

  GSHSCENE_Image_Params params;
  memset(&params, 0, sizeof params);
  params.space = pool_space(space_sel);
  params.flags = flag_sel == 2 ? GSHSCENE_IMAGE_ALPHA_ONLY : flag_sel == 3 ? 2u : flag_sel == 4 ? 0x100u : flag_sel == 5 ? 3u : 0u;
  params.reserved = reserved_sel == 0 ? 1u : 0u;

  /* The result class, predicted from the rules as IM2, IM4, IM10 and IM3 give them. */
  int alpha_only = params.flags & 1u;
  int space_partial = space_sel % 10 == 7 || space_sel % 10 == 8;
  int space_reserved = space_sel % 10 == 6;
  int space_zero = space_sel % 10 == 0;
  int borrowed = hostile == 0;
  int invalid = params.reserved != 0 || (params.flags & ~1u) != 0 || space_partial ||
      (alpha_only && (model != 0 || kind == 5 || !space_zero)) || (take && borrowed);
  int unsupported = !invalid && (model == 3 || kind == 4 || kind == 5 || space_reserved);
  uint64_t tiles = (uint64_t)((w + 63u) / 64u) * ((h + 63u) / 64u);
  uint64_t accounted = 128u + (uint64_t)w * h * layout * sample_bytes + tiles * 16u;
  int limit = !invalid && !unsupported &&
      (w > 65536u || h > 65536u || (uint64_t)w * h > ((uint64_t)1 << 28) || accounted > (uint64_t)lim->max_bytes);
  /* A sample value past its range, planted where the whole raster is read. */
  int bad = 0;
  if (!borrowed && model != 3 && kind <= 3) {
    if (kind == 1 && bad_sel == 1) {
      bad = 1;
    } else if (kind == 3 && bad_sel >= 1 && bad_sel <= 3 && !(model != 2 && bad_sel == 3)) {
      bad = 1;
    }
  }

  GIMG_Raster * raster = NULL;
  unsigned char hostile_buffer[64];
  memset(hostile_buffer, 0xEE, sizeof hostile_buffer);
  if (borrowed) {
    if (gimg_raster_create(w, h, &format, GIMG_RASTER_BORROWED, hostile_buffer, row, &raster) != GIMG_OK) {
      return GSHSCENE_ERR_INVALID;
    }
  } else {
    if (gimg_raster_create(w, h, &format, GIMG_RASTER_OWNED, NULL, stride, &raster) != GIMG_OK) {
      return GSHSCENE_ERR_INVALID;
    }
    unsigned char * pixels = (unsigned char *)gimg_raster_pixels(raster);
    memset(pixels, 0x6C, stride * h);
    for (unsigned y = 0; y < h; y++) {
      for (unsigned x = 0; x < w; x++) {
        for (unsigned c = 0; c < layout; c++) {
          image_put(pixels + (size_t)y * stride + ((size_t)x * layout + c) * sample_bytes, bits,
              image_value(base, bits, is_float, x, y, c));
        }
      }
    }
    if (bad) {
      unsigned char * last = pixels + (size_t)(h - 1) * stride + ((size_t)(w - 1) * layout + (layout - 1)) * sample_bytes;
      uint32_t v = kind == 1 ? 4096u : bad_sel == 1 ? 0x7FC00000u : bad_sel == 2 ? 0x7F800000u : 0x3FC00000u; /* 4096, NaN, +inf, 1.5 */
      image_put(last, bits, v);
    }
  }

  /* An equal raster for the second intern, made first: a take of content the store already holds destroys the raster. */
  GIMG_Raster * twin = NULL;
  if (!borrowed) {
    CHECK(gimg_raster_copy(raster, &twin) == GIMG_OK);
  }
  GSHSCENE_Store_Stats before;
  CHECK(gshscene_store_stats(s->store, &before) == GSHSCENE_OK);
  const GSHSCENE_Resource * g = NULL;
  GSHSCENE_Result r = take ? gshscene_store_intern_image_take(s->store, raster, &params, &g)
                           : gshscene_store_intern_image(s->store, raster, &params, &g);
  GSHSCENE_Store_Stats after;
  CHECK(gshscene_store_stats(s->store, &after) == GSHSCENE_OK);
  CHECK(r != GSHSCENE_ERR_INTERNAL && r != GSHSCENE_ERR_OOM && r != GSHSCENE_ERR_CORRUPT && r != GSHSCENE_ERR_IO);
  if (invalid) {
    CHECK(r == GSHSCENE_ERR_INVALID);
    outcomes[OUT_IMAGE_INVALID]++;
  } else if (unsupported) {
    CHECK(r == GSHSCENE_ERR_UNSUPPORTED);
    outcomes[OUT_IMAGE_UNSUPPORTED]++;
  } else if (limit) {
    CHECK(r == GSHSCENE_ERR_LIMIT);
    outcomes[OUT_IMAGE_LIMIT]++;
  } else if (bad) {
    CHECK(r == GSHSCENE_ERR_INVALID);
    outcomes[OUT_IMAGE_BAD_SAMPLE]++;
  } else if (r != GSHSCENE_OK) {
    /* Only the store's own limits refuse what the rules accept. */
    CHECK(r == GSHSCENE_ERR_LIMIT);
    CHECK(before.live >= lim->max_resources || accounted > (uint64_t)(lim->max_bytes - (size_t)before.bytes));
    outcomes[OUT_IMAGE_LIMIT]++;
  }
  if (r != GSHSCENE_OK) {
    /* A refusal changed nothing, and a refused take left the raster with its caller. */
    CHECK(g == NULL);
    CHECK(before.live == after.live && before.bytes == after.bytes && before.created == after.created);
    CHECK(gimg_raster_width(raster) == w && gimg_raster_height(raster) == h);
    gimg_raster_destroy(raster);
    gimg_raster_destroy(twin);
    return r;
  }
  outcomes[OUT_IMAGE_INTERNED]++;
  CHECK(gshscene_resource_kind(g) == GSHSCENE_RESOURCE_IMAGE);
  CHECK(gshscene_resource_bytes(g) == (size_t)accounted);
  GSHSCENE_Image_Info info;
  CHECK(gshscene_resource_image(g, &info) == GSHSCENE_OK);
  CHECK(info.width == w && info.height == h && info.layout == layout && info.bits == bits);
  CHECK(info.sample_type == (is_float ? 1u : 0u) && info.tile == 64u);
  CHECK(info.tiles_x == (w + 63u) / 64u && info.tiles_y == (h + 63u) / 64u);
  CHECK(info.alpha_only == (alpha_only ? 1u : 0u));
  CHECK(info.alpha_form == (model == 2 ? (premult ? 2u : 1u) : 0u));
  CHECK(info.space.primaries == (alpha_only ? GCOL_PRIMARIES_UNKNOWN : params.space.primaries));
  CHECK(gshscene_image_stride(g) >= row);
  for (unsigned y = 0; y < h; y++) {
    const unsigned char * got = (const unsigned char *)gshscene_image_row(g, y);
    CHECK(got != NULL);
    for (unsigned x = 0; x < w; x++) {
      for (unsigned c = 0; c < layout; c++) {
        unsigned char want[4] = {0, 0, 0, 0};
        image_put(want, bits, image_value(base, bits, is_float, x, y, c));
        CHECK(memcmp(got + ((size_t)x * layout + c) * sample_bytes, want, sample_bytes) == 0);
      }
    }
  }
  CHECK(gshscene_image_row(g, h) == NULL);
  CHECK(gshscene_image_tile_hash(g, info.tiles_x, 0) == 0 && gshscene_image_tile_hash(g, 0, info.tiles_y) == 0);
  CHECK(gshscene_resource_dump(g, s->null) == GSHSCENE_OK);
  int fresh = after.created == before.created + 1; /* a take of content already held hit, and destroyed the raster */

  /* Found again: a copy of what was interned is the same handle, and a take of an equal raster too. */
  GSHSCENE_Store_Stats mid;
  CHECK(gshscene_store_stats(s->store, &mid) == GSHSCENE_OK);
  CHECK(twin != NULL);
  const GSHSCENE_Resource * hit = NULL;
  r = take ? gshscene_store_intern_image_take(s->store, twin, &params, &hit)
           : gshscene_store_intern_image(s->store, twin, &params, &hit);
  CHECK(r == GSHSCENE_OK && hit == g);
  GSHSCENE_Store_Stats again;
  CHECK(gshscene_store_stats(s->store, &again) == GSHSCENE_OK);
  CHECK(again.live == mid.live && again.bytes == mid.bytes && again.created == mid.created && again.hits == mid.hits + 1);
  gshscene_resource_release(hit);
  if (!take) {
    gimg_raster_destroy(twin);
  }
  outcomes[OUT_IMAGE_HIT]++;
  if (take) {
    if (fresh) {
      CHECK(gshscene_image_row(g, 0) == gimg_raster_pixels_const(raster)); /* a take reads the raster in place */
    }
    outcomes[OUT_IMAGE_TAKEN]++;
  } else {
    gimg_raster_destroy(raster);
  }
  gshscene_resource_release(s->images[slot]);
  s->images[slot] = g;
  return GSHSCENE_OK;
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
      size_t list_depth = list_picture_depth(s->list);
      int deepest_is_mask = 0;
      for (size_t i = 0; i < gshscene_list_count(s->list); i++) {
        const GSHSCENE_Item * it;
        CHECK(gshscene_list_item(s->list, i, &it) == GSHSCENE_OK);
        if (it->kind == GSHSCENE_ITEM_GROUP_PUSH && it->group.soft_mask) {
          GSHSCENE_Mask content;
          CHECK(gshscene_resource_mask(it->group.soft_mask, &content) == GSHSCENE_OK);
          deepest_is_mask = deepest_is_mask || gshscene_resource_picture_depth(content.picture) == list_depth;
        }
      }
      r = gshscene_store_intern_picture(s->store, s->list, &pic);
      CHECK(r == GSHSCENE_OK || r == GSHSCENE_ERR_INVALID || r == GSHSCENE_ERR_LIMIT);
      /* MK12: a mask counts as an instance does, so a picture is one level deeper than the deepest picture it names, and the
       * bound is met when that passes max_picture_depth. */
      if (list_depth + 1 > lim->max_picture_depth) {
        CHECK(r != GSHSCENE_OK);
        outcomes[OUT_MASK_DEPTH_LIMIT] += deepest_is_mask && r == GSHSCENE_ERR_LIMIT;
      }
      if (r == GSHSCENE_OK) {
        CHECK(gshscene_resource_picture_depth(pic) == list_depth + 1);
        outcomes[OUT_MASK_DEPTH] += deepest_is_mask && list_depth > 0;
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
        for (size_t e = 0; e < gshgeom_path_element_count(gshscene_resource_path(x)); e++) {
          GSHGEOM_Verb verb;
          CHECK(gshgeom_path_verb(gshscene_resource_path(x), e, &verb) == GSHGEOM_OK);
          if (verb == GSHGEOM_VERB_ARC) {
            outcomes[OUT_ARC_PATH]++;
            break;
          }
        }
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
    case OP_INTERN_PRIMITIVE: {
      /* The tag (primitive.h): a primitive from the input interns, reads back, is found again, differs from its untagged copy, and a
       * refusal leaves the store as it was. */
      GSHSCENE_Store * store = s->store;
      unsigned slot = pick(s, POOL);
      GSHSCENE_Primitive want;
      memset(&want, 0, sizeof want);
      const unsigned kind_pick = arg(s);
      want.kind = (GSHSCENE_Primitive_Kind)(kind_pick % 16 == 0 ? (kind_pick >> 4) % 6 : 1 + kind_pick % 3);
      const unsigned flip_pick = arg(s);
      want.flip_x = flip_pick & 1;
      want.flip_y = (flip_pick >> 1) & 1;
      if (flip_pick & 0x10) {
        want.flip_x = (flip_pick & 0x20) ? -1 : 2;
      }
      double v[12];
      for (int i = 0; i < 12; i++) {
        const unsigned b = arg(s);
        static const double table[8] = {0.0, -0.0, 1.0, 2.0, 0.5, 1e300, 0x1p100, -1.0};
        v[i] = b < 8 ? table[b] : (double)(b - 8) / 4.0;
      }
      if ((flip_pick & 0x80) && kind_pick % 5 == 0) {
        v[0] = (double)NAN;
      }
      want.x = v[0];
      want.y = v[1];
      want.w = v[2];
      want.h = v[3];
      for (int i = 0; i < 4; i++) {
        want.r[i].rx = v[4 + i];
        want.r[i].ry = v[8 + i];
      }
      want.cx = v[0];
      want.cy = v[1];
      want.rx = v[2];
      want.ry = v[3];
      if (want.kind == GSHSCENE_PRIMITIVE_RECT || want.kind == GSHSCENE_PRIMITIVE_ELLIPSE) {
        memset(want.r, 0, sizeof want.r);
      }
      GSHSCENE_Store_Stats before;
      CHECK(gshscene_store_stats(store, &before) == GSHSCENE_OK);
      const GSHSCENE_Resource * x = NULL;
      r = gshscene_store_intern_primitive(store, &want, &x);
      CHECK(r == GSHSCENE_OK || r == GSHSCENE_ERR_INVALID || r == GSHSCENE_ERR_LIMIT);
      if (r != GSHSCENE_OK) {
        CHECK(x == NULL);
        GSHSCENE_Store_Stats after;
        CHECK(gshscene_store_stats(store, &after) == GSHSCENE_OK);
        CHECK(after.live == before.live && after.bytes == before.bytes && after.created == before.created);
        outcomes[OUT_PRIMITIVE_INVALID] += r == GSHSCENE_ERR_INVALID;
        outcomes[OUT_PRIMITIVE_LIMIT] += r == GSHSCENE_ERR_LIMIT;
        break;
      }
      GSHSCENE_Primitive got;
      CHECK(gshscene_resource_primitive(x, &got) == GSHSCENE_OK);
      CHECK(gshscene_resource_kind(x) == GSHSCENE_RESOURCE_PATH);
      CHECK(got.kind != GSHSCENE_PRIMITIVE_NONE && got.kind < GSHSCENE_PRIMITIVE_KIND_COUNT);
      CHECK(got.flip_x == want.flip_x && got.flip_y == want.flip_y);
      CHECK(want.kind == got.kind || (want.kind == GSHSCENE_PRIMITIVE_ROUNDED_RECT && got.kind == GSHSCENE_PRIMITIVE_RECT));
      outcomes[OUT_PRIMITIVE_CANONICAL] += want.kind != got.kind;
      CHECK(want.kind == got.kind || (!want.flip_x && !want.flip_y)); /* a flipped rounded rectangle never becomes a rect */
      outcomes[OUT_PRIMITIVE_REVERSED] += got.kind == GSHSCENE_PRIMITIVE_ROUNDED_RECT && got.flip_x != got.flip_y;
      outcomes[OUT_PRIMITIVE_FLIPPED] += got.flip_x || got.flip_y;
      const GSHGEOM_Path * path = gshscene_resource_path(x);
      const size_t elements = gshgeom_path_element_count(path);
      CHECK(got.kind == GSHSCENE_PRIMITIVE_RECT ? elements == 5 : (got.kind == GSHSCENE_PRIMITIVE_ELLIPSE ? elements == 6 : (elements >= 6 && elements <= 10)));
      /* found again: the same handle */
      const GSHSCENE_Resource * again = NULL;
      CHECK(gshscene_store_intern_primitive(store, &got, &again) == GSHSCENE_OK);
      CHECK(again == x);
      gshscene_resource_release(again);
      outcomes[OUT_PRIMITIVE_HIT]++;
      /* the same geometry untagged is another handle with another hash and no tag */
      GSHGEOM_Path * copy = NULL;
      CHECK(gshgeom_path_copy(path, &s->alloc, NULL, &copy) == GSHGEOM_OK);
      const GSHSCENE_Resource * plain = NULL;
      GSHSCENE_Result pr = gshscene_store_intern_path(store, copy, &plain);
      gshgeom_path_destroy(copy);
      CHECK(pr == GSHSCENE_OK || pr == GSHSCENE_ERR_LIMIT); /* the store may hold room for the tagged one only */
      if (pr == GSHSCENE_OK) {
        CHECK(plain != x && gshscene_resource_hash(plain) != gshscene_resource_hash(x));
        GSHSCENE_Primitive none;
        CHECK(gshscene_resource_primitive(plain, &none) == GSHSCENE_OK && none.kind == GSHSCENE_PRIMITIVE_NONE);
        gshscene_resource_release(plain);
        outcomes[OUT_PRIMITIVE_UNTAGGED]++;
      }
      outcomes[OUT_PRIMITIVE_INTERNED]++;
      gshscene_resource_release(s->res[slot]);
      s->res[slot] = x;
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
        for (int i = 0; i < 2; i++) {
          gshscene_resource_release(s->sgrads[i]);
          s->sgrads[i] = NULL;
          gshscene_resource_release(s->sstyles[i]);
          s->sstyles[i] = NULL;
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
          if (item->kind == GSHSCENE_ITEM_GROUP_PUSH && info.node.kind == GSHSCENE_NODE_GROUP) {
            /* T1: a group node's push carries its composite and PDF's fields as they are. */
            CHECK(item->group.composite == info.node.composite && item->group.isolation == info.node.isolation &&
                  item->group.knockout == info.node.knockout && item->group.soft_mask == info.node.soft_mask);
            outcomes[OUT_PDF_EMITTED] += item->group.isolation || item->group.knockout || item->group.soft_mask;
          }
          if (item->kind == GSHSCENE_ITEM_FILL && item->paint.kind == GSHSCENE_PAINT_GRADIENT) {
            /* A gradient node's fill (story P2): its paint names the node's gradient, and the notice predicate runs. */
            CHECK(info.node.paint.kind == GSHSCENE_PAINT_GRADIENT && item->paint.resource == info.node.paint.resource);
            GSHSCENE_Reason why = gshscene_paint_notice(item, NULL, 0, NULL);
            CHECK(why == 0 || why == GSHSCENE_REASON_GRADIENT_DEGENERATE || why == GSHSCENE_REASON_MAP_NOT_INVERTIBLE);
            outcomes[OUT_GRADIENT_EMITTED]++;
          }
          if (item->kind == GSHSCENE_ITEM_STROKE) {
            /* S1: a stroke node's item carries its centerline, style and paint as they are; the notice and the
             * features it requires are SK8's and SK11's. */
            CHECK(info.node.kind == GSHSCENE_NODE_STROKE && item->style == info.node.style &&
                  item->resource == info.node.resource);
            CHECK(gshscene_resource_kind(item->style) == GSHSCENE_RESOURCE_STROKE_STYLE);
            GSHSCENE_Reason why = gshscene_stroke_notice(item, NULL, 0, NULL);
            CHECK(why == 0 || why == GSHSCENE_REASON_STROKE_ZERO_WIDTH || why == GSHSCENE_REASON_MAP_NOT_INVERTIBLE);
            uint32_t need = gshscene_stroke_required(item, NULL, 0, NULL);
            CHECK((need & ~GSHSCENE_STROKE_NATIVE_ALL) == 0 && (why == 0 || need == 0));
            outcomes[OUT_STROKE_EMITTED]++;
          }
          if (item->kind == GSHSCENE_ITEM_PLACEHOLDER) {
            /* P5, PH7: a placeholder node's item carries its source, code, flag and box as they are, holds no resource. */
            CHECK(info.node.kind == GSHSCENE_NODE_PLACEHOLDER && item->resource == NULL &&
                  item->placeholder.source == info.node.placeholder.source &&
                  item->placeholder.code == info.node.placeholder.code &&
                  item->placeholder.approximated == info.node.placeholder.approximated &&
                  item->placeholder.has_box == info.node.placeholder.has_box);
            outcomes[OUT_PLACEHOLDER_EMITTED]++;
          }
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
    case OP_INTERN_GRADIENT: {
      /* G1, G2: a gradient of every kind, extend and flag from bytes, valid or not; read back, hashed, dumped, resolved,
       * and asked what it draws (G10).  Or, a quarter of the time, the float-free constructor on a run of the pool. */
      unsigned slot = pick(s, 4);
      GSHSCENE_Gradient_Stop stops[5];
      memset(stops, 0, sizeof stops);
      unsigned n = pick(s, 6);
      for (unsigned i = 0; i < n && i < 5; i++) {
        stops[i].offset = ((double)pick(s, 9) - 2.0) / 4.0; /* -0.5 to 1.5, repeats likely */
        unsigned k = pick(s, 5);
        stops[i].color.kind = k < 2 ? GSHSCENE_PAINT_SOLID : k == 2 ? GSHSCENE_PAINT_FOREGROUND
                              : k == 3 ? GSHSCENE_PAINT_PALETTE : GSHSCENE_PAINT_GRADIENT;
        stops[i].color.rgba[3] = (double)pick(s, 5) / 4.0;
        if (stops[i].color.kind == GSHSCENE_PAINT_SOLID) {
          stops[i].color.rgba[0] = (double)pick(s, 3) / 2.0;
          stops[i].color.space = pool_space(arg(s));
        } else if (stops[i].color.kind == GSHSCENE_PAINT_PALETTE) {
          stops[i].color.entry = pick(s, 4);
        }
      }
      const GSHSCENE_Resource * g = NULL;
      if (pick(s, 4) == 0) {
        GSHSCENE_Gradient_Fixed f;
        memset(&f, 0, sizeof f);
        f.kind = pick(s, 5);
        f.extend = pick(s, 4);
        f.bits = pick(s, 3);
        for (int v = 0; v < 6; v++) {
          f.v[v] = pick(s, 3) ? (int32_t)arg(s) - 128 : 0;
        }
        f.primaries = 1u + pick(s, 4);
        f.transfer = 1u + pick(s, 3);
        f.premultiplied = pick(s, 2);
        GSHSCENE_Gradient_Stop_Fixed fs[3];
        memset(fs, 0, sizeof fs);
        f.stop_count = pick(s, 4);
        for (size_t i = 0; i < f.stop_count; i++) {
          fs[i].offset = (int32_t)pick(s, 5);
          fs[i].bits = pick(s, 3);
          fs[i].color.kind = 2u + pick(s, 2);
          fs[i].color.alpha = (int32_t)pick(s, 3);
          unsigned e = arg(s);
          fs[i].color.entry = e == 255 ? 0xFFFFu : e % 4;
        }
        f.stops = fs;
        r = gshscene_store_intern_gradient_fixed(s->store, s->res[pick(s, POOL)], &f, &g);
      } else {
        GSHSCENE_Gradient d;
        memset(&d, 0, sizeof d);
        d.kind = pick(s, 5);
        double v[6];
        for (int k = 0; k < 6; k++) {
          unsigned b = arg(s);
          v[k] = b == 255 ? (pick(s, 2) ? 1e308 * 10.0 : 0.0 / 0.0) : ((double)(b % 9) - 4.0);
        }
        int other = pick(s, 8) == 0; /* sometimes a field of another kind is set */
        if (d.kind == GSHSCENE_GRADIENT_LINEAR || other) {
          d.p0[0] = v[0], d.p0[1] = v[1], d.p1[0] = v[2], d.p1[1] = v[3], d.d[0] = v[4], d.d[1] = v[5];
        }
        if (d.kind == GSHSCENE_GRADIENT_RADIAL) {
          d.c0[0] = v[0], d.c0[1] = v[1], d.r0 = v[2], d.c1[0] = v[3], d.c1[1] = v[4], d.r1 = v[5];
        }
        if (d.kind == GSHSCENE_GRADIENT_SWEEP) {
          d.center[0] = v[0], d.center[1] = v[1], d.a0 = v[2] * 45.0, d.a1 = v[3] * 45.0;
          d.angle_unit = pick(s, 5);
        }
        d.extend = pick(s, 4);
        d.premultiplied = pick(s, 3) == 2 ? 2 : pick(s, 2);
        d.space = pool_space(arg(s));
        int use;
        d.map = pool_map(arg(s), &use);
        if (!use) {
          gshgeom_map_identity(2, &d.map);
        }
        d.stop_count = n;
        d.stops = stops;
        r = gshscene_store_intern_gradient(s->store, &d, &g);
      }
      CHECK(r == GSHSCENE_OK || r == GSHSCENE_ERR_INVALID || r == GSHSCENE_ERR_UNSUPPORTED || r == GSHSCENE_ERR_LIMIT ||
            r == GSHSCENE_ERR_OOM);
      if (r != GSHSCENE_OK) {
        CHECK(g == NULL);
        outcomes[OUT_GRADIENT_REFUSED]++;
        outcomes[OUT_INTERN_LIMIT] += r == GSHSCENE_ERR_LIMIT;
        break;
      }
      GSHSCENE_Gradient back;
      CHECK(gshscene_resource_kind(g) == GSHSCENE_RESOURCE_GRADIENT);
      CHECK(gshscene_resource_gradient(g, &back) == GSHSCENE_OK);
      for (size_t i = 1; i < back.stop_count; i++) {
        CHECK(back.stops[i - 1].offset <= back.stops[i].offset);
      }
      CHECK(gshscene_resource_hash(g) == gshscene_resource_hash(g));
      CHECK(gshscene_resource_dump(g, s->null) == GSHSCENE_OK);
      /* The same content again is the same handle. */
      const GSHSCENE_Resource * again = NULL;
      if (gshscene_store_intern_gradient(s->store, &back, &again) == GSHSCENE_OK) {
        CHECK(again == g);
        gshscene_resource_release(again);
      }
      GSHSCENE_Color fg;
      gshscene_color_init(&fg);
      GSHSCENE_Color_Space target;
      memset(&target, 0, sizeof target);
      target.primaries = GCOL_PRIMARIES_SRGB;
      target.transfer = GCOL_TRANSFER_SRGB;
      GSHSCENE_Resolved_Stop out[5];
      r = gshscene_gradient_resolve(g, &fg, s->list ? gshscene_list_palette(s->list) : NULL, &target, NULL, out, 5);
      CHECK(r == GSHSCENE_OK || r == GSHSCENE_ERR_REFUSED);
      for (size_t i = 0; r == GSHSCENE_OK && i < back.stop_count; i++) {
        CHECK(out[i].offset == back.stops[i].offset);
        CHECK(out[i].rgba[3] >= 0.0 && out[i].rgba[3] <= 1.0);
      }
      GSHSCENE_Item item;
      memset(&item, 0, sizeof item);
      item.kind = GSHSCENE_ITEM_FILL;
      gshgeom_map_identity(2, &item.map);
      item.paint.kind = GSHSCENE_PAINT_GRADIENT;
      item.paint.resource = g;
      GSHSCENE_Reason why = gshscene_paint_notice(&item, NULL, 0, NULL);
      CHECK(why == 0 || why == GSHSCENE_REASON_GRADIENT_DEGENERATE || why == GSHSCENE_REASON_MAP_NOT_INVERTIBLE);
      outcomes[OUT_GRADIENT_DEGENERATE] += why == GSHSCENE_REASON_GRADIENT_DEGENERATE;
      gshscene_resource_release(s->grads[slot]);
      s->grads[slot] = g;
      break;
    }
    case OP_FILL_GRADIENT: {
      /* A fill with a gradient paint (G1): one of the pool, sometimes none or a path, an alpha in range or not. */
      if (!s->list) {
        break;
      }
      ListSnap b = snap(s->list);
      GSHSCENE_Paint paint;
      memset(&paint, 0, sizeof paint);
      paint.kind = GSHSCENE_PAINT_GRADIENT;
      unsigned k = pick(s, 6);
      paint.resource = k < 4 ? s->grads[k] : k == 4 ? s->res[0] : NULL;
      paint.rgba[3] = (double)pick(s, 6) / 4.0; /* up to 1.25: invalid above 1 */
      int use;
      GSHGEOM_Map m = pool_map(arg(s), &use);
      r = gshscene_list_fill(s->list, s->res[pick(s, POOL)], use ? &m : NULL, GSHGEOM_FILL_NONZERO, &paint, 0, 0);
      list_after(s, b, r, 0, 0);
      if (r == GSHSCENE_OK) {
        const GSHSCENE_Item * item;
        CHECK(gshscene_list_item(s->list, b.count, &item) == GSHSCENE_OK);
        CHECK(item->paint.kind == GSHSCENE_PAINT_GRADIENT && item->paint.resource == paint.resource);
        CHECK(gshscene_resource_kind(item->paint.resource) == GSHSCENE_RESOURCE_GRADIENT);
        outcomes[OUT_GRADIENT_FILL]++;
      }
      break;
    }
    case OP_SCENE_GRADIENT: {
      /* A gradient in the scene's store, for shape nodes (story P2). */
      if (!s->scene) {
        break;
      }
      unsigned slot = pick(s, 2);
      const GSHSCENE_Resource * g = NULL;
      if (fuzz_gradient(s, gshscene_scene_store(s->scene), &g) == GSHSCENE_OK) {
        gshscene_resource_release(s->sgrads[slot]);
        s->sgrads[slot] = g;
      } else {
        CHECK(g == NULL);
      }
      break;
    }
    case OP_GRADIENT_NODE: {
      /* A shape node painted with a gradient of the scene's store (or of the other store, or none: invalid). */
      unsigned k = pick(s, EDITS);
      if (!has_edit(s, k)) {
        break;
      }
      GSHSCENE_Edit * e = s->edits[k];
      GSHSCENE_Node n;
      gshscene_node_init(GSHSCENE_NODE_SHAPE, &n);
      int use;
      n.map = pool_map(arg(s), &use);
      n.resource = s->sres[pick(s, POOL)];
      n.paint.kind = GSHSCENE_PAINT_GRADIENT;
      unsigned which = pick(s, 4);
      n.paint.resource = which < 2 ? s->sgrads[which] : which == 2 ? s->grads[0] : NULL;
      n.paint.rgba[3] = (double)pick(s, 5) / 4.0;
      size_t before = gshscene_edit_node_count(e);
      uint64_t id = 0;
      if (pick(s, 3) == 0) {
        r = gshscene_edit_set(e, some_id(s), &n);
      } else {
        r = gshscene_edit_add(e, some_id(s), GSHSCENE_APPEND, &n, NULL, &id);
      }
      CHECK(r != GSHSCENE_ERR_INTERNAL && r != GSHSCENE_ERR_OOM);
      if (r == GSHSCENE_OK) {
        remember(s, id);
        CHECK(n.paint.resource && gshscene_resource_kind(n.paint.resource) == GSHSCENE_RESOURCE_GRADIENT);
        outcomes[OUT_GRADIENT_NODE]++;
      } else {
        CHECK(gshscene_edit_node_count(e) == before);
        if (r == GSHSCENE_ERR_LIMIT) {
          CHECK(before >= lim->max_nodes);
        }
      }
      break;
    }
    case OP_COLOR_BOUND: {
      /* N1: a component at or past 2^16 at each entry point that records one, asserted by the bound. */
      static const double values[8] = {0.5, 0x1p16, -0x1p16, 0x1.0000000000001p16, -0x1.0000000000001p16, 0x1p17, 0x1p64,
          -1e300};
      double v = values[pick(s, 8)];
      int past = !(v >= -0x1p16 && v <= 0x1p16);
      unsigned k = pick(s, 3);
      int bad_alpha = pick(s, 5) == 0;
      unsigned sp = arg(s) % 10;
      GSHSCENE_Color c;
      memset(&c, 0, sizeof c);
      c.rgba[0] = c.rgba[1] = c.rgba[2] = 0.25;
      c.rgba[k] = v;
      c.rgba[3] = bad_alpha ? 2.0 : 0.75;
      c.space = pool_space(sp);
      /* What the checks decide, in their order: the alpha and the space's validity, the reserved space, then N1. */
      GSHSCENE_Result want = bad_alpha || sp == 7 || sp == 8 ? GSHSCENE_ERR_INVALID
                             : sp == 6                       ? GSHSCENE_ERR_UNSUPPORTED
                             : past                          ? GSHSCENE_ERR_LIMIT
                                                             : GSHSCENE_OK;
      GSHSCENE_Paint paint;
      memset(&paint, 0, sizeof paint);
      paint.kind = GSHSCENE_PAINT_SOLID;
      memcpy(paint.rgba, c.rgba, sizeof paint.rgba);
      paint.space = c.space;
      unsigned where = pick(s, 6);
      if (where == 0) {
        /* A fill: other arguments may be invalid too, so a past value is never recorded and a LIMIT is the bound's. */
        if (!s->list) {
          break;
        }
        ListSnap b = snap(s->list);
        const GSHSCENE_Resource * target = s->res[pick(s, POOL)];
        r = gshscene_list_fill(s->list, target, NULL, GSHGEOM_FILL_NONZERO, &paint, 0, 0);
        CHECK(r != GSHSCENE_ERR_INTERNAL && r != GSHSCENE_ERR_OOM);
        if (want == GSHSCENE_ERR_INVALID) {
          CHECK(r == GSHSCENE_ERR_INVALID);
        }
        if (past) {
          CHECK(r != GSHSCENE_OK);
        } else {
          /* Not past N1's bound: a limit is the list's own, its items or its expansion (as list_after says). */
          CHECK(r != GSHSCENE_ERR_LIMIT || gshscene_list_count(s->list) >= lim->max_list_items ||
                gshscene_list_expansion(s->list) + 1 > lim->max_expansion);
        }
        if (r != GSHSCENE_OK) {
          CHECK(gshscene_list_count(s->list) == b.count && gshscene_list_expansion(s->list) == b.expansion);
        }
        outcomes[OUT_COLOR_LIMIT] += past && r == GSHSCENE_ERR_LIMIT;
        outcomes[OUT_COLOR_AT_BOUND] += r == GSHSCENE_OK && (v == 0x1p16 || v == -0x1p16);
      } else if (where == 1 || where == 2) {
        /* A list's or an edit's foreground: exactly what the checks decide (a closed list or a spent edit: invalid). */
        GSHSCENE_Color before, after;
        unsigned e = pick(s, EDITS);
        if (where == 1 ? !s->list : !has_edit(s, e)) {
          break;
        }
        if (where == 1) {
          CHECK(gshscene_list_foreground(s->list, &before) == GSHSCENE_OK);
          r = gshscene_list_set_foreground(s->list, &c);
          CHECK(gshscene_list_foreground(s->list, &after) == GSHSCENE_OK);
          CHECK(r == want || (r == GSHSCENE_ERR_INVALID && gshscene_list_is_closed(s->list)));
        } else {
          CHECK(gshscene_edit_foreground(s->edits[e], &before) == GSHSCENE_OK);
          r = gshscene_edit_set_foreground(s->edits[e], &c);
          CHECK(gshscene_edit_foreground(s->edits[e], &after) == GSHSCENE_OK);
          CHECK(r == want || r == GSHSCENE_ERR_INVALID);
        }
        CHECK(memcmp(&after, r == GSHSCENE_OK ? &c : &before, sizeof c) == 0);
        outcomes[OUT_COLOR_LIMIT] += past && r == GSHSCENE_ERR_LIMIT;
        outcomes[OUT_COLOR_AT_BOUND] += r == GSHSCENE_OK && (v == 0x1p16 || v == -0x1p16);
      } else if (where == 3) {
        /* A palette of two entries, the one under test first or second, the other valid. */
        GSHSCENE_Color entries[2];
        unsigned at = pick(s, 2);
        gshscene_color_init(&entries[0]);
        entries[1] = entries[0];
        entries[at] = c;
        const GSHSCENE_Resource * palette = NULL;
        r = gshscene_store_intern_palette(s->store, 2, entries, &palette);
        CHECK(r == want || ((r == GSHSCENE_ERR_LIMIT || r == GSHSCENE_ERR_OOM) && want == GSHSCENE_OK));
        CHECK((r == GSHSCENE_OK) == (palette != NULL));
        gshscene_resource_release(palette);
        outcomes[OUT_COLOR_LIMIT] += past && r == GSHSCENE_ERR_LIMIT;
      } else if (where == 4) {
        /* A gradient with the color as a solid stop (the gradient interpolates in the target's space, so any stop's
         * space is allowed), and a foreground stop. */
        GSHSCENE_Gradient g;
        memset(&g, 0, sizeof g);
        g.kind = GSHSCENE_GRADIENT_LINEAR;
        g.p1[0] = 4.0;
        gshgeom_map_identity(2, &g.map);
        GSHSCENE_Gradient_Stop stops[2];
        memset(stops, 0, sizeof stops);
        unsigned at = pick(s, 2);
        stops[at].offset = 0.0;
        stops[at].color = paint;
        stops[1 - at].offset = 1.0;
        stops[1 - at].color.kind = GSHSCENE_PAINT_FOREGROUND;
        stops[1 - at].color.rgba[3] = 1.0;
        g.stops = stops;
        g.stop_count = 2;
        const GSHSCENE_Resource * grad = NULL;
        r = gshscene_store_intern_gradient(s->store, &g, &grad);
        CHECK(r == want || ((r == GSHSCENE_ERR_LIMIT && lim->max_resource_elements < 2) || r == GSHSCENE_ERR_OOM ||
                               (r == GSHSCENE_ERR_LIMIT && want == GSHSCENE_OK)));
        if (past) {
          CHECK(r != GSHSCENE_OK);
        }
        CHECK((r == GSHSCENE_OK) == (grad != NULL));
        gshscene_resource_release(grad);
        outcomes[OUT_COLOR_LIMIT] += past && r == GSHSCENE_ERR_LIMIT;
      } else {
        /* A shape node, added or set: the resource or the parent may be invalid too. */
        unsigned e = pick(s, EDITS);
        if (!has_edit(s, e)) {
          break;
        }
        GSHSCENE_Node n;
        gshscene_node_init(GSHSCENE_NODE_SHAPE, &n);
        n.resource = s->sres[pick(s, POOL)];
        n.paint = paint;
        size_t before = gshscene_edit_node_count(s->edits[e]);
        uint64_t id = 0;
        if (pick(s, 3) == 0) {
          r = gshscene_edit_set(s->edits[e], some_id(s), &n);
        } else {
          r = gshscene_edit_add(s->edits[e], some_id(s), GSHSCENE_APPEND, &n, NULL, &id);
        }
        CHECK(r != GSHSCENE_ERR_INTERNAL && r != GSHSCENE_ERR_OOM);
        if (want == GSHSCENE_ERR_INVALID) {
          CHECK(r == GSHSCENE_ERR_INVALID);
        }
        if (past) {
          CHECK(r != GSHSCENE_OK);
        } else {
          CHECK(r != GSHSCENE_ERR_LIMIT || before >= lim->max_nodes);
        }
        if (r == GSHSCENE_OK) {
          remember(s, id);
        } else {
          CHECK(gshscene_edit_node_count(s->edits[e]) == before);
        }
        outcomes[OUT_COLOR_LIMIT] += past && r == GSHSCENE_ERR_LIMIT;
      }
      break;
    }
    case OP_GROUP_PDF: {
      /* B1, T1: a group push of any composite (one past the enum), any isolation and knockout (one past each), and a
       * soft mask that is none, a picture of the list's store, or a path (invalid). */
      if (!s->list) {
        break;
      }
      ListSnap b = snap(s->list);
      GSHSCENE_Group g;
      memset(&g, 0, sizeof g);
      g.opacity = pick(s, 5) / 4.0;
      g.composite = pick(s, (unsigned)GSHSCENE_COMPOSITE_COUNT + 1);
      g.isolation = pick(s, 3);
      g.knockout = pick(s, 3);
      unsigned mask = pick(s, 3);
      const GSHSCENE_Resource * held = mask == 1 ? mask_over(s->store, s->pictures[pick(s, POOL)]) : NULL;
      g.soft_mask = mask == 1 ? held : mask == 2 ? s->res[pick(s, POOL)] : NULL;
      int path_mask = g.soft_mask && gshscene_resource_kind(g.soft_mask) != GSHSCENE_RESOURCE_MASK;
      int invalid = g.composite >= GSHSCENE_COMPOSITE_COUNT || g.isolation > 1 || g.knockout > 1 || path_mask;
      r = gshscene_list_group_push(s->list, &g, 0, 0.0);
      gshscene_resource_release(held); /* the list holds its own */
      if (invalid) {
        CHECK(r == GSHSCENE_ERR_INVALID);
        outcomes[OUT_PDF_INVALID] += g.isolation > 1 || g.knockout > 1 || path_mask;
      }
      list_after(s, b, r, 1, 0);
      if (r == GSHSCENE_OK) {
        const GSHSCENE_Item * item = NULL;
        CHECK(gshscene_list_item(s->list, gshscene_list_count(s->list) - 1, &item) == GSHSCENE_OK);
        CHECK(item->group.composite == g.composite && item->group.isolation == g.isolation &&
              item->group.knockout == g.knockout && item->group.soft_mask == g.soft_mask);
        outcomes[OUT_BLEND_GROUP] += g.composite >= GSHSCENE_COMPOSITE_SCREEN;
        outcomes[OUT_PDF_GROUP] += g.isolation || g.knockout || g.soft_mask;
      }
      break;
    }
    case OP_NODE_PDF: {
      /* T1 on a group node: isolation and knockout (one past each), and a soft mask made in the scene's store (a
       * picture of one fill) or none. */
      unsigned k = pick(s, EDITS);
      if (!has_edit(s, k) || !s->scene) {
        break;
      }
      GSHSCENE_Edit * e = s->edits[k];
      uint64_t id = some_id(s);
      unsigned isolation = pick(s, 3);
      unsigned knockout = pick(s, 3);
      int masked = pick(s, 2);
      GSHSCENE_Node_Info info;
      if (gshscene_edit_node(e, id, &info) != GSHSCENE_OK || info.node.kind != GSHSCENE_NODE_GROUP) {
        break;
      }
      const GSHSCENE_Resource * mask = NULL;
      if (masked) {
        GSHSCENE_Store * store = gshscene_scene_store(s->scene);
        GSHGEOM_Path * path = make_path(s, 0);
        const GSHSCENE_Resource * shape = NULL;
        GSHSCENE_List * l = NULL;
        if (path && gshscene_store_intern_path(store, path, &shape) == GSHSCENE_OK &&
            gshscene_list_create(&s->alloc, &s->limits, store, &l) == GSHSCENE_OK &&
            gshscene_list_fill(l, shape, NULL, GSHGEOM_FILL_NONZERO, NULL, 0, 0.0) == GSHSCENE_OK &&
            gshscene_list_close(l) == GSHSCENE_OK) {
          const GSHSCENE_Resource * picture = NULL;
          if (gshscene_store_intern_picture(store, l, &picture) == GSHSCENE_OK) {
            mask = mask_over(store, picture);
            gshscene_resource_release(picture);
          }
        }
        gshscene_list_destroy(l);
        gshscene_resource_release(shape);
        gshgeom_path_destroy(path);
        if (!mask) {
          break; /* a limit or an allocation: nothing to set */
        }
      }
      GSHSCENE_Node n = info.node;
      n.isolation = isolation;
      n.knockout = knockout;
      n.soft_mask = mask;
      r = gshscene_edit_set(e, id, &n);
      CHECK(r != GSHSCENE_ERR_INTERNAL);
      if (isolation > 1 || knockout > 1) {
        CHECK(r == GSHSCENE_ERR_INVALID);
      }
      if (r == GSHSCENE_OK) {
        GSHSCENE_Node_Info now;
        CHECK(gshscene_edit_node(e, id, &now) == GSHSCENE_OK);
        CHECK(now.node.isolation == isolation && now.node.knockout == knockout && now.node.soft_mask == mask);
      }
      gshscene_resource_release(mask); /* the node holds its own */
      break;
    }
    case OP_INTERN_STYLE: {
      /* SK2, SK3: a style from the input, in either store; its result class predicted from the fields as given, and
       * the canonical form read back, restated here; the canonical form interned again is the same handle. */
      int in_scene = s->scene && pick(s, 2);
      GSHSCENE_Store * store = in_scene ? gshscene_scene_store(s->scene) : s->store;
      unsigned slot = pick(s, in_scene ? 2 : 4);
      static const double miters[5] = {1.0, 4.0, 0.5, 0x1p16, 0x1.0000000000001p16};
      GSHSCENE_Stroke_Style st;
      gshscene_stroke_style_init(&st);
      st.width = style_values[pick(s, 12)];
      st.width_space = pick(s, 3);
      st.join = pick(s, 6);
      st.miter_limit = miters[pick(s, 5)];
      st.cap = pick(s, 4);
      st.degenerate = pick(s, 3);
      st.dash_scope = pick(s, 3);
      st.alignment = pick(s, 4);
      st.alignment_rule = (GSHGEOM_Fill_Rule)pick(s, 3);
      double dashes[5];
      size_t n = pick(s, 6);
      for (size_t i = 0; i < n; i++) {
        dashes[i] = style_values[pick(s, 12)];
      }
      st.dash_count = n;
      st.dashes = (n && pick(s, 8) == 0) ? NULL : dashes;
      st.dash_offset = style_values[pick(s, 12)];
      st.width_profile = pick(s, 12) == 0 ? s->res[0] : NULL;
      int invalid = (n && !st.dashes) || !isfinite(st.width) || !isfinite(st.miter_limit) || !isfinite(st.dash_offset) ||
          st.width_space > 1 || st.join > 4 || st.cap > 2 || st.degenerate > 1 || st.dash_scope > 1 ||
          st.alignment > 2 || st.alignment_rule > 1 || st.width < 0.0;
      for (size_t i = 0; st.dashes && i < n; i++) {
        invalid = invalid || !isfinite(dashes[i]);
      }
      int unsupported = !invalid && st.width_profile != NULL;
      size_t count = st.dashes ? style_dash_count(dashes, n) : 0;
      size_t most = lim->max_resource_elements < 65535 ? lim->max_resource_elements : 65535;
      int beyond = !style_within(st.width) || !style_within(st.dash_offset) || !(st.miter_limit >= 1.0) ||
          !(st.miter_limit <= 0x1p16) || count > most;
      for (size_t i = 0; st.dashes && i < n; i++) {
        beyond = beyond || !style_within(dashes[i]);
      }
      const GSHSCENE_Resource * g = NULL;
      r = gshscene_store_intern_stroke_style(store, &st, &g);
      if (invalid) {
        CHECK(r == GSHSCENE_ERR_INVALID);
        outcomes[OUT_STYLE_INVALID]++;
      } else if (unsupported) {
        CHECK(r == GSHSCENE_ERR_UNSUPPORTED);
        outcomes[OUT_STYLE_UNSUPPORTED]++;
      } else if (beyond) {
        CHECK(r == GSHSCENE_ERR_LIMIT);
        outcomes[OUT_STYLE_LIMIT]++;
      } else {
        CHECK(r == GSHSCENE_OK || r == GSHSCENE_ERR_LIMIT); /* the store's own limits */
      }
      if (r != GSHSCENE_OK) {
        CHECK(g == NULL);
        outcomes[OUT_INTERN_LIMIT] += r == GSHSCENE_ERR_LIMIT;
        break;
      }
      outcomes[OUT_STYLE_INTERNED]++;
      outcomes[OUT_STYLE_DOUBLED] += n % 2 && count == 2 * n;
      outcomes[OUT_STYLE_SOLID] += n && count == 0;
      GSHSCENE_Stroke_Style back;
      CHECK(gshscene_resource_kind(g) == GSHSCENE_RESOURCE_STROKE_STYLE);
      CHECK(gshscene_resource_stroke_style(g, &back) == GSHSCENE_OK);
      int reads_miter = st.join == GSHSCENE_STROKE_JOIN_MITER || st.join == GSHSCENE_STROKE_JOIN_MITER_CLIP ||
          st.join == GSHSCENE_STROKE_JOIN_ARCS;
      CHECK(same_bits(back.width, st.width == 0.0 ? 0.0 : st.width));
      CHECK(back.width_space == st.width_space && back.join == st.join && back.cap == st.cap &&
            back.alignment == st.alignment);
      CHECK(back.degenerate == (st.cap == GSHSCENE_STROKE_CAP_BUTT ? (uint32_t)GSHSCENE_STROKE_DEGENERATE_DRAW : st.degenerate)); /* SK3 (b) */
      CHECK(same_bits(back.miter_limit, reads_miter ? st.miter_limit : 4.0));
      CHECK(back.alignment_rule == (st.alignment == GSHSCENE_STROKE_ALIGN_CENTER ? GSHGEOM_FILL_NONZERO : st.alignment_rule));
      CHECK(back.dash_count == count && (count == 0) == (back.dashes == NULL));
      for (size_t i = 0; i < count; i++) {
        double want = dashes[i % n];
        CHECK(same_bits(back.dashes[i], want == 0.0 ? 0.0 : want));
      }
      if (count) {
        CHECK(same_bits(back.dash_offset, st.dash_offset == 0.0 ? 0.0 : st.dash_offset) && back.dash_scope == st.dash_scope);
      } else {
        CHECK(same_bits(back.dash_offset, 0.0) && back.dash_scope == GSHSCENE_STROKE_DASH_PER_SUBPATH);
      }
      CHECK(back.width_profile == NULL);
      CHECK(gshscene_resource_dump(g, s->null) == GSHSCENE_OK);
      const GSHSCENE_Resource * again = NULL;
      if (gshscene_store_intern_stroke_style(store, &back, &again) == GSHSCENE_OK) {
        CHECK(again == g);
        gshscene_resource_release(again);
      }
      const GSHSCENE_Resource ** pool = in_scene ? s->sstyles : s->styles;
      gshscene_resource_release(pool[slot]);
      pool[slot] = g;
      break;
    }
    case OP_STROKE: {
      /* A stroke in the list (S1): a fresh centerline (recipe 5 is 3D), a style of the pool, of the other store or a
       * path (both invalid) or none, a planar map or a 3D one. */
      if (!s->list) {
        break;
      }
      unsigned recipe = pick(s, 6);
      GSHSCENE_Result made;
      const GSHSCENE_Resource * line = intern_recipe(s, s->store, recipe, &made);
      unsigned k = pick(s, 7);
      const GSHSCENE_Resource * style = k < 4 ? s->styles[k] : k == 4 ? s->sstyles[0] : k == 5 ? s->res[0] : NULL;
      int use;
      GSHGEOM_Map m = pool_map(arg(s), &use);
      int three = pick(s, 8) == 0;
      if (three) {
        gshgeom_map_identity(3, &m);
        use = 1;
      }
      GSHSCENE_Paint paint = pool_paint(arg(s));
      ListSnap b = snap(s->list);
      r = gshscene_list_stroke(s->list, line, style, use ? &m : NULL, &paint, 0, 0.0);
      list_after(s, b, r, 0, 0);
      int good_style = k < 4 && style != NULL;
      CHECK(r != GSHSCENE_ERR_UNSUPPORTED || three || recipe == 5);
      if (!good_style || !line) {
        CHECK(r != GSHSCENE_OK);
      }
      outcomes[OUT_STROKE_UNSUPPORTED] += r == GSHSCENE_ERR_UNSUPPORTED;
      if (r == GSHSCENE_OK) {
        CHECK(!three && recipe != 5);
        const GSHSCENE_Item * item;
        CHECK(gshscene_list_item(s->list, b.count, &item) == GSHSCENE_OK);
        CHECK(item->kind == GSHSCENE_ITEM_STROKE && item->style == style && item->resource == line);
        outcomes[OUT_STROKE_RECORDED]++;
      }
      gshscene_resource_release(line); /* the list holds its own */
      break;
    }
    case OP_STROKE_NODE: {
      /* A stroke node (S1): a centerline of the scene's store, a style of it, of the other store, or none. */
      unsigned k = pick(s, EDITS);
      if (!has_edit(s, k)) {
        break;
      }
      GSHSCENE_Edit * e = s->edits[k];
      GSHSCENE_Node n;
      gshscene_node_init(GSHSCENE_NODE_STROKE, &n);
      int use;
      n.map = pool_map(arg(s), &use);
      n.resource = s->sres[pick(s, POOL)];
      unsigned which = pick(s, 4);
      n.style = which < 2 ? s->sstyles[which] : which == 2 ? s->styles[0] : NULL;
      n.paint = pool_paint(arg(s));
      size_t before = gshscene_edit_node_count(e);
      uint64_t id = 0;
      if (pick(s, 3) == 0) {
        r = gshscene_edit_set(e, some_id(s), &n);
      } else {
        r = gshscene_edit_add(e, some_id(s), GSHSCENE_APPEND, &n, NULL, &id);
      }
      CHECK(r != GSHSCENE_ERR_INTERNAL && r != GSHSCENE_ERR_OOM);
      if (r == GSHSCENE_OK) {
        remember(s, id);
        CHECK(which < 2 && n.style && n.resource);
        outcomes[OUT_STROKE_NODE]++;
      } else {
        CHECK(gshscene_edit_node_count(e) == before);
        if (r == GSHSCENE_ERR_LIMIT) {
          CHECK(before >= lim->max_nodes);
        }
      }
      break;
    }
    case OP_PLACEHOLDER: {
      /* A placeholder in the list (P5, PH1, PH2): a source (0 is invalid) and a code (0 is invalid), the approximated
       * flag (2 is invalid), no box, a valid box, an inverted one or one with a NaN, a planar map or a 3D one. */
      if (!s->list) {
        break;
      }
      unsigned source = pick(s, 4);
      unsigned code = pick(s, 4);
      unsigned approx = pick(s, 3);
      unsigned shape = pick(s, 5);
      double box[4] = {1.0, 2.0, 5.0, 8.0};
      if (shape == 3) {
        box[0] = 9.0; /* inverted */
      } else if (shape == 4) {
        box[1] = NAN;
      }
      int use;
      GSHGEOM_Map m = pool_map(arg(s), &use);
      int three = pick(s, 8) == 0;
      if (three) {
        gshgeom_map_identity(3, &m);
        use = 1;
      }
      ListSnap b = snap(s->list);
      r = gshscene_list_placeholder(s->list, (uint16_t)source, (uint16_t)code, approx, shape == 0 ? NULL : box,
          use ? &m : NULL, 0, 0.0);
      list_after(s, b, r, 0, 0);
      int valid = source != 0 && code != 0 && approx <= 1 && shape <= 2 && !three;
      CHECK(r != GSHSCENE_ERR_UNSUPPORTED || three);
      if (!valid) {
        CHECK(r != GSHSCENE_OK);
        outcomes[OUT_PLACEHOLDER_INVALID] += r == GSHSCENE_ERR_INVALID || r == GSHSCENE_ERR_UNSUPPORTED;
      }
      if (r == GSHSCENE_OK) {
        CHECK(valid);
        const GSHSCENE_Item * item;
        CHECK(gshscene_list_item(s->list, b.count, &item) == GSHSCENE_OK);
        CHECK(item->kind == GSHSCENE_ITEM_PLACEHOLDER && item->resource == NULL && item->placeholder.source == source &&
              item->placeholder.code == code && item->placeholder.approximated == approx &&
              item->placeholder.has_box == (shape != 0));
        outcomes[OUT_PLACEHOLDER_RECORDED]++;
      }
      break;
    }
    case OP_PLACEHOLDER_NODE: {
      /* A placeholder node (P5, PH7): the same fields, in an edit. */
      unsigned k = pick(s, EDITS);
      if (!has_edit(s, k)) {
        break;
      }
      GSHSCENE_Edit * e = s->edits[k];
      GSHSCENE_Node n;
      gshscene_node_init(GSHSCENE_NODE_PLACEHOLDER, &n);
      unsigned source = pick(s, 4);
      unsigned code = pick(s, 4);
      unsigned approx = pick(s, 3);
      unsigned shape = pick(s, 4);
      n.placeholder.source = (uint16_t)source;
      n.placeholder.code = (uint16_t)code;
      n.placeholder.approximated = approx;
      if (shape != 0) {
        n.placeholder.has_box = 1;
        n.placeholder.box[0] = 0.0;
        n.placeholder.box[1] = 0.0;
        n.placeholder.box[2] = shape == 3 ? -1.0 : 4.0; /* inverted */
        n.placeholder.box[3] = 4.0;
      }
      int use;
      n.map = pool_map(arg(s), &use);
      size_t before = gshscene_edit_node_count(e);
      uint64_t id = 0;
      if (pick(s, 3) == 0) {
        r = gshscene_edit_set(e, some_id(s), &n);
      } else {
        r = gshscene_edit_add(e, some_id(s), GSHSCENE_APPEND, &n, NULL, &id);
      }
      CHECK(r != GSHSCENE_ERR_INTERNAL && r != GSHSCENE_ERR_OOM);
      if (r == GSHSCENE_OK) {
        remember(s, id);
        CHECK(source != 0 && code != 0 && approx <= 1 && shape <= 2);
        outcomes[OUT_PLACEHOLDER_NODE]++;
      } else {
        CHECK(gshscene_edit_node_count(e) == before);
        if (r == GSHSCENE_ERR_LIMIT) {
          CHECK(before >= lim->max_nodes);
        }
      }
      break;
    }
    case OP_INTERN_IMAGE:
      /* IM1 to IM10: an image from the input, by copy or by take; its result class predicted and its content read back. */
      (void)fuzz_image(s, pick(s, 2));
      break;
    case OP_IMAGE_NOT_SHAPE: {
      /* IM14: an image where a path, a run or a picture goes is refused by every record call, and nothing is recorded. */
      const GSHSCENE_Resource * img = s->images[pick(s, 3)];
      if (!s->list || !img) {
        break;
      }
      ListSnap b = snap(s->list);
      GSHSCENE_Paint paint = pool_paint(1);
      r = gshscene_list_fill(s->list, img, NULL, GSHGEOM_FILL_NONZERO, &paint, 0, 0);
      CHECK(r == GSHSCENE_ERR_INVALID);
      r = gshscene_list_clip_push(s->list, img, NULL, GSHGEOM_FILL_NONZERO, 0, 0);
      CHECK(r == GSHSCENE_ERR_INVALID);
      r = gshscene_list_instance(s->list, img, NULL, 0, 0);
      CHECK(r == GSHSCENE_ERR_INVALID);
      list_after(s, b, GSHSCENE_ERR_INVALID, 0, 0);
      outcomes[OUT_IMAGE_NOT_SHAPE]++;
      break;
    }
    case OP_IMAGE_PAINT: {
      /* IP1: an image paint over an image of the pool, or over a path, or over nothing.  The result class is predicted
       * from the rules: invalid before limit, and a bound last. */
      unsigned pick_image = pick(s, 5);
      unsigned wmode = pick(s, 5);
      unsigned wa = arg(s), wb = arg(s), wc = arg(s), wd = arg(s);
      unsigned mapsel = pick(s, 7);
      unsigned ex = pick(s, 6), ey = pick(s, 6), mag = pick(s, 5), mini = pick(s, 5);
      unsigned space_sel = pick(s, 2);
      unsigned tsel = pick(s, 5);
      unsigned where = pick(s, POOL);
      const GSHSCENE_Resource * img = pick_image < 3 ? s->images[pick_image] : pick_image == 3 ? s->res[0] : NULL;
      GSHSCENE_Image_Info info;
      memset(&info, 0, sizeof info);
      int is_image = img && gshscene_resource_kind(img) == GSHSCENE_RESOURCE_IMAGE &&
          gshscene_resource_image(img, &info) == GSHSCENE_OK;
      GSHSCENE_Image_Paint ip;
      gshscene_image_paint_init(&ip);
      ip.image = img;
      int window_ok = 1;
      if (wmode >= 2 && is_image) {
        uint32_t x0 = wa % info.width, y0 = wb % info.height;
        ip.window[0] = x0;
        ip.window[1] = y0;
        ip.window[2] = x0 + 1u + wc % (info.width - x0);
        ip.window[3] = y0 + 1u + wd % (info.height - y0);
        if (wmode == 3) {
          ip.window[2] = x0; /* empty */
          window_ok = 0;
        } else if (wmode == 4) {
          ip.window[3] = info.height + 1u; /* past the image */
          window_ok = 0;
        }
      }
      int map_invalid = 0;
      int map_past = 0;
      switch (mapsel) {
        case 1: gshgeom_map_scale(2, 3.0, 3.0, 1.0, &ip.map); break;
        case 2: gshgeom_map_scale(2, 0.25, 0.25, 1.0, &ip.map); break;
        case 3: gshgeom_map_scale(2, 0.0, 1.0, 1.0, &ip.map); break; /* singular: legal, and a notice at render */
        case 4: gshgeom_map_translate(2, 5.5, -2.0, 0.0, &ip.map); break;
        case 5: gshgeom_map_scale(2, 0x1p70, 0x1p70, 1.0, &ip.map); map_past = 1; break;
        case 6: ip.map.m[0][0] = (double)NAN; map_invalid = 1; break;
        default: break;
      }
      ip.extend_x = ex;
      ip.extend_y = ey;
      ip.magnify = mag;
      ip.minify = mini;
      if (space_sel) {
        ip.filter_space.primaries = GCOL_PRIMARIES_SRGB;
        ip.filter_space.transfer = GCOL_TRANSFER_LINEAR;
      }
      GSHSCENE_Paint tint;
      memset(&tint, 0, sizeof tint);
      int has_tint = tsel != 0;
      int tint_invalid = 0;
      int tint_past = 0;
      switch (tsel) {
        case 1: tint.kind = GSHSCENE_PAINT_SOLID; tint.rgba[0] = 0.25; tint.rgba[1] = 0.5; tint.rgba[2] = 0.75; tint.rgba[3] = 1.0; break;
        case 2: tint.kind = GSHSCENE_PAINT_FOREGROUND; tint.rgba[3] = 1.0; break;
        case 3: tint.kind = GSHSCENE_PAINT_SOLID; tint.rgba[0] = 0x1p17; tint.rgba[3] = 1.0; tint_past = 1; break;
        case 4: tint.kind = GSHSCENE_PAINT_GRADIENT; tint.rgba[3] = 1.0; tint_invalid = 1; break;
        default: break;
      }
      ip.tint = has_tint ? &tint : NULL;
      int stencil = is_image && info.alpha_only;
      int invalid = !is_image || !window_ok || map_invalid || ex >= 4 || ey >= 4 || mag >= 4 || mini >= 4 ||
          stencil != has_tint || tint_invalid;
      int past = !invalid && (map_past || (tint_past && has_tint));
      const GSHSCENE_Resource * made = NULL;
      GSHSCENE_Store_Stats before, after;
      CHECK(gshscene_store_stats(s->store, &before) == GSHSCENE_OK);
      r = gshscene_store_intern_image_paint(s->store, &ip, &made);
      CHECK(gshscene_store_stats(s->store, &after) == GSHSCENE_OK);
      CHECK(r != GSHSCENE_ERR_INTERNAL && r != GSHSCENE_ERR_OOM && r != GSHSCENE_ERR_CORRUPT && r != GSHSCENE_ERR_IO);
      if (invalid) {
        CHECK(r == GSHSCENE_ERR_INVALID);
        outcomes[OUT_IMAGE_PAINT_INVALID]++;
      } else if (past) {
        CHECK(r == GSHSCENE_ERR_LIMIT);
        outcomes[OUT_IMAGE_PAINT_LIMIT]++;
      } else if (r != GSHSCENE_OK) {
        CHECK(r == GSHSCENE_ERR_LIMIT); /* only the store's own limits refuse what IP1 accepts */
        CHECK(before.live >= lim->max_resources || before.bytes + GSHSCENE_BYTES_BASE > lim->max_bytes);
        outcomes[OUT_IMAGE_PAINT_LIMIT]++;
      }
      if (r != GSHSCENE_OK) {
        CHECK(made == NULL && before.live == after.live && before.bytes == after.bytes && before.created == after.created);
        break;
      }
      outcomes[OUT_IMAGE_PAINT]++;
      outcomes[OUT_IMAGE_PAINT_STENCIL] += stencil != 0;
      CHECK(gshscene_resource_kind(made) == GSHSCENE_RESOURCE_IMAGE_PAINT);
      GSHSCENE_Image_Paint back;
      CHECK(gshscene_resource_image_paint(made, &back) == GSHSCENE_OK);
      CHECK(back.image == img);
      CHECK(back.window[0] < back.window[2] && back.window[2] <= info.width && back.window[1] < back.window[3] &&
          back.window[3] <= info.height);
      if (wmode < 2) {
        CHECK(back.window[0] == 0 && back.window[1] == 0 && back.window[2] == info.width && back.window[3] == info.height);
      } else {
        CHECK(back.window[0] == ip.window[0] && back.window[1] == ip.window[1] && back.window[2] == ip.window[2] &&
            back.window[3] == ip.window[3]);
      }
      CHECK(back.extend_x == ex && back.extend_y == ey && back.magnify == mag && back.minify == mini);
      CHECK((back.tint != NULL) == stencil);
      if (stencil) {
        CHECK(back.filter_space.primaries == 0 && back.filter_space.transfer == 0);
      }
      CHECK(gshscene_resource_hash(made) != 0);
      const GSHSCENE_Resource * again = NULL;
      CHECK(gshscene_store_intern_image_paint(s->store, &ip, &again) == GSHSCENE_OK && again == made);
      gshscene_resource_release(again);
      if (s->list) {
        ListSnap b = snap(s->list);
        GSHSCENE_Paint paint;
        memset(&paint, 0, sizeof paint);
        paint.kind = GSHSCENE_PAINT_IMAGE;
        paint.resource = made;
        paint.rgba[3] = 1.0;
        r = gshscene_list_fill(s->list, s->res[where], NULL, GSHGEOM_FILL_NONZERO, &paint, 0, 0);
        list_after(s, b, r, 0, 0);
        if (r == GSHSCENE_OK) {
          const GSHSCENE_Item * item;
          CHECK(gshscene_list_item(s->list, b.count, &item) == GSHSCENE_OK);
          CHECK(item->paint.kind == GSHSCENE_PAINT_IMAGE && item->paint.resource == made);
          CHECK(gshscene_image_required(item) != 0);
          uint32_t filter = 99;
          GSHSCENE_Result fr = gshscene_image_filter_of(item, NULL, 0, NULL, &filter);
          CHECK((fr == GSHSCENE_OK && filter < (uint32_t)GSHSCENE_IMAGE_FILTER_COUNT) || fr == GSHSCENE_ERR_LIMIT);
          double quad[8];
          int drawn = gshscene_image_fill_is_draw(item, quad);
          CHECK(drawn == 0 || drawn == 1);
          (void)gshscene_paint_notice(item, NULL, 0, NULL);
          outcomes[OUT_IMAGE_PAINT_FILL]++;
        }
      }
      gshscene_resource_release(made);
      break;
    }
    case OP_INTERN_MASK:
      fuzz_mask(s);
      break;
    case OP_GROUP_MASK:
      fuzz_mask_group(s);
      break;
    case OP_NODE_MASK:
      fuzz_mask_node(s);
      break;
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
  for (int i = 0; i < 4; i++) {
    gshscene_resource_release(s->grads[i]);
  }
  for (int i = 0; i < 2; i++) {
    gshscene_resource_release(s->sgrads[i]);
    gshscene_resource_release(s->sstyles[i]);
  }
  for (int i = 0; i < 4; i++) {
    gshscene_resource_release(s->styles[i]);
  }
  for (int i = 0; i < 3; i++) {
    gshscene_resource_release(s->images[i]);
  }
  for (int i = 0; i < 4; i++) {
    CHECK(!s->masks[i] || gshscene_resource_hash(s->masks[i]) == s->mask_hash[i]);
    gshscene_resource_release(s->masks[i]);
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
