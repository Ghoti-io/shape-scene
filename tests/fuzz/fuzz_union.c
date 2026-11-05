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
 * A fuzz target for the clip union (clip_union.h; CU6 to CU17): random members, maps, rules, duplicates and orders interned, recorded
 * into a list and planned, with the invariants of the story checked on every input that is accepted.
 *
 * The input is read as bytes: a member count, then four bytes for each member (which path of a palette, which map, which rule, a
 * translation) and two bytes for an instance map and an order.  The palette holds rectangles on a quarter-pixel lattice (so an
 * independent integer box test is exact), an empty path, a zero-area line, a rectangle past the 26.6 range (unprovable), and a run of
 * glyph outlines.  Invariants: every outcome of the intern is one CU7 names; the same members in another order and doubled are one
 * handle; the canonical count is at most the count given; a union of one member is recorded as its single clip (the list equals the list
 * of that clip); the comparisons of the predicate are at most n (n - 1) / 2; and, when every member is a palette rectangle under a
 * translation or a scale of 2, the verdict equals the one an independent integer test of the boxes gives, and the planning pass refuses
 * under `CLIP_UNION` alone exactly when the verdict is overlap.  Built into libFuzzer by `make fuzz` and replayed over the committed
 * corpus by `make fuzz-replay`.
 */

#include <ghoti.io/shape-scene/shape-scene.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { OUT_INVALID, OUT_UNSUPPORTED, OUT_DISJOINT, OUT_OVERLAP, OUT_ONE, OUT_UNPROVABLE, OUT_COUNT };
static unsigned long g_outcome[OUT_COUNT];

int gshscene_fuzz_unreached(void) {
  int missing = 0;
  for (int i = 0; i < OUT_COUNT; i++) {
    if (g_outcome[i] == 0) {
      missing++;
    }
  }
  return missing;
}

typedef struct Reader {
  const uint8_t * p;
  size_t n;
} Reader;

static unsigned take(Reader * r) {
  if (r->n == 0) {
    return 0;
  }
  r->n--;
  return *r->p++;
}

static void bad(const char * what) {
  fprintf(stderr, "fuzz_union: invariant broken: %s\n", what);
  abort();
}

static GSHSCENE_Result path_of(GSHSCENE_Store * store, const double * c, size_t n, const GSHSCENE_Resource ** out) {
  GSHGEOM_Path * p = NULL;
  gshgeom_path_create(NULL, NULL, 2, &p);
  for (size_t i = 0; i < n; i++) {
    GSHGEOM_Point q = {c[2 * i], c[2 * i + 1], 0};
    if (i == 0) {
      gshgeom_path_move_to(p, q);
    } else {
      gshgeom_path_line_to(p, q);
    }
  }
  if (n > 2) {
    gshgeom_path_close(p);
  }
  GSHSCENE_Result r = gshscene_store_intern_path(store, p, out);
  gshgeom_path_destroy(p);
  return r;
}

/** An integer box in 1/64 pixel. */
typedef struct IBox {
  int64_t x0, y0, x1, y1;
  int empty;
} IBox;

int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
  Reader in = {data, size};
  GSHSCENE_Store * store = NULL;
  if (gshscene_store_create(NULL, NULL, &store) != GSHSCENE_OK) {
    return 0;
  }
  const GSHSCENE_Resource * palette[8] = {0};
  static const double rects[4][4] = {{0, 0, 4, 4}, {4, 0, 8, 4}, {12, 0, 16, 4}, {2, 2, 6, 6}};
  for (int k = 0; k < 4; k++) {
    const double c[8] = {rects[k][0], rects[k][1], rects[k][2], rects[k][1], rects[k][2], rects[k][3], rects[k][0], rects[k][3]};
    path_of(store, c, 4, &palette[k]);
  }
  GSHGEOM_Path * empty = NULL;
  gshgeom_path_create(NULL, NULL, 2, &empty);
  gshscene_store_intern_path(store, empty, &palette[4]);
  gshgeom_path_destroy(empty);
  const double huge[8] = {0, 0, 1e12, 0, 1e12, 4, 0, 4};
  path_of(store, huge, 4, &palette[5]);
  const double line[4] = {0, 0, 4, 0};
  path_of(store, line, 2, &palette[7]);
  GSHSCENE_Glyph g = {1, 0.0, 0.0, palette[0]};
  GSHSCENE_Glyph_Run run;
  memset(&run, 0, sizeof run);
  run.source = GSHSCENE_GLYPHS_FROM_OUTLINES;
  run.glyph_count = 1;
  run.glyphs = &g;
  gshscene_store_intern_glyph_run(store, &run, &palette[6]);

  const unsigned n = take(&in) % 10u;
  GSHSCENE_Clip_Member members[10];
  IBox boxes[10];
  int independent = 1;
  memset(members, 0, sizeof members);
  const unsigned kinds_and_maps_at = (unsigned)(in.n);
  (void)kinds_and_maps_at;
  unsigned kind_of[10], map_of[10], tx_of[10], ty_of[10];
  for (unsigned i = 0; i < n; i++) {
    const unsigned kind = take(&in) % 8u;
    const unsigned mapsel = take(&in) % 6u;
    const unsigned rule = take(&in) % 5u;
    const unsigned trans = take(&in);
    kind_of[i] = kind;
    map_of[i] = mapsel;
    tx_of[i] = trans & 15u;
    ty_of[i] = trans >> 4;
    members[i].resource = palette[kind];
    members[i].rule = (GSHGEOM_Fill_Rule)(rule == 4 ? 7 : (rule & 1u));
    const double tx = (double)(trans & 15u) / 4.0, ty = (double)(trans >> 4) / 4.0;
    switch (mapsel) {
      case 1: gshgeom_map_translate(2, tx, ty, 0, &members[i].map); break;
      case 2: gshgeom_map_scale(2, 2.0, 2.0, 1.0, &members[i].map); break;
      case 3: gshgeom_map_scale(2, 0.0, 0.0, 1.0, &members[i].map); break;
      case 4: gshgeom_map_identity(3, &members[i].map); break;
      case 5: gshgeom_map_identity(2, &members[i].map); members[i].map.t[0] = (double)NAN; break;
      default: break; /* zero-filled: the identity */
    }
    /* the independent box, for a palette rectangle under the identity, a translation or a scale of 2 */
    if (kind <= 3 && (mapsel <= 2) && members[i].rule <= 1) {
      const double s = mapsel == 2 ? 2.0 : 1.0, ox = mapsel == 1 ? tx : 0.0, oy = mapsel == 1 ? ty : 0.0;
      boxes[i].x0 = (int64_t)((rects[kind][0] * s + ox) * 64.0);
      boxes[i].y0 = (int64_t)((rects[kind][1] * s + oy) * 64.0);
      boxes[i].x1 = (int64_t)((rects[kind][2] * s + ox) * 64.0);
      boxes[i].y1 = (int64_t)((rects[kind][3] * s + oy) * 64.0);
      boxes[i].empty = 0;
    } else if (kind == 4 || kind == 7) {
      boxes[i].empty = 1; /* no region: disjoint from everything */
    } else {
      independent = 0;
    }
  }
  const unsigned inst_flag = take(&in) % 3u;
  const unsigned order = take(&in);

  const GSHSCENE_Resource * u = NULL;
  GSHSCENE_Result r = gshscene_store_intern_clip_union(store, members, n, &u);
  if (r == GSHSCENE_ERR_INVALID) {
    g_outcome[OUT_INVALID]++;
  } else if (r == GSHSCENE_ERR_UNSUPPORTED) {
    g_outcome[OUT_UNSUPPORTED]++;
  } else if (r != GSHSCENE_OK) { /* the member limit is the unit tests': ten members are far below it */
    bad("an outcome the intern does not name");
  } else {
    const size_t distinct = gshscene_resource_clip_union_count(u);
    if (distinct == 0 || distinct > n) {
      bad("the canonical count is not between 1 and the count given");
    }
    /* the same members, rotated by `order` and doubled, are one handle */
    GSHSCENE_Clip_Member again[10];
    for (unsigned i = 0; i < n; i++) {
      again[i] = members[(i + order) % n];
    }
    const GSHSCENE_Resource * u2 = NULL;
    if (gshscene_store_intern_clip_union(store, again, n, &u2) != GSHSCENE_OK || u2 != u) {
      bad("another order of the same members is not the same handle");
    }
    gshscene_resource_release(u2);

    GSHSCENE_List * list = NULL;
    gshscene_list_create(NULL, NULL, store, &list);
    GSHSCENE_Result pr = gshscene_list_clip_push(list, u, NULL, (GSHGEOM_Fill_Rule)(order & 1u), 0, 0.0);
    if (pr != GSHSCENE_OK && pr != GSHSCENE_ERR_LIMIT) {
      bad("a clip push of a union is refused");
    }
    gshscene_list_clip_pop(list);
    gshscene_list_close(list);
    const GSHSCENE_Item * item = NULL;
    if (pr == GSHSCENE_OK && gshscene_list_item(list, 0, &item) == GSHSCENE_OK) {
      if (distinct == 1) {
        g_outcome[OUT_ONE]++;
        if (gshscene_resource_kind(item->resource) == GSHSCENE_RESOURCE_CLIP_UNION) {
          bad("a one-member union is in the list");
        }
        GSHSCENE_Clip_Member m;
        gshscene_resource_clip_union_member(u, 0, &m);
        GSHSCENE_List * single = NULL;
        gshscene_list_create(NULL, NULL, store, &single);
        if (gshscene_list_clip_push(single, m.resource, &m.map, m.rule, 0, 0.0) == GSHSCENE_OK) {
          gshscene_list_clip_pop(single);
          gshscene_list_close(single);
          if (!gshscene_list_equal(list, single) || gshscene_list_hash(list) != gshscene_list_hash(single)) {
            bad("a one-member union is not the single clip");
          }
        }
        gshscene_list_destroy(single);
      } else {
        GSHGEOM_Map inst;
        gshgeom_map_translate(2, 1.5, 0.25, 0.0, &inst);
        GSHSCENE_Union_Verdict v;
        if (gshscene_clip_union_disjoint(item, inst_flag == 1 ? &inst : NULL, inst_flag == 1 ? 1 : 0, NULL, &v) != GSHSCENE_OK) {
          bad("the predicate refused a union item");
        }
        if ((uint64_t)distinct * (distinct - 1) / 2 < v.tests) {
          bad("the predicate made more than n (n - 1) / 2 comparisons");
        }
        GSHSCENE_Sink_Caps caps;
        memset(&caps, 0, sizeof caps);
        caps.item_kinds = UINT32_C(0xffffffff);
        caps.flags = GSHSCENE_CAP_CLIP_AA | GSHSCENE_CAP_CLIP_UNION;
        GSHSCENE_Disposition d = gshscene_caps_disposition(&caps, item, inst_flag == 1 ? &inst : NULL, inst_flag == 1 ? 1 : 0, NULL, NULL, NULL);
        const int refused = d.entry_class == GSHSCENE_ENTRY_REFUSED;
        if (refused != !v.disjoint || (refused && d.reason != GSHSCENE_REASON_UNION_OVERLAP) ||
            (refused && d.detail != (((uint64_t)v.first << 16) | v.second))) {
          bad("the disposition and the verdict disagree");
        }
        if (!v.disjoint && v.first == v.second) {
          g_outcome[OUT_UNPROVABLE]++;
        } else {
          g_outcome[v.disjoint ? OUT_DISJOINT : OUT_OVERLAP]++;
        }
        /* the independent integer test, when every member is a palette rectangle on the lattice and the union holds them all as given */
        if (independent && distinct == n) {
          int want = 1;
          const int64_t ix = inst_flag == 1 ? 96 : 0, iy = inst_flag == 1 ? 16 : 0; /* 1.5 and 0.25 pixels in 1/64 */
          for (unsigned a = 0; a < n && want; a++) {
            for (unsigned b = a + 1; b < n && want; b++) {
              if (boxes[a].empty || boxes[b].empty) {
                continue;
              }
              const int64_t lx = (boxes[a].x0 > boxes[b].x0 ? boxes[a].x0 : boxes[b].x0) + ix;
              const int64_t hx = (boxes[a].x1 < boxes[b].x1 ? boxes[a].x1 : boxes[b].x1) + ix;
              const int64_t ly = (boxes[a].y0 > boxes[b].y0 ? boxes[a].y0 : boxes[b].y0) + iy;
              const int64_t hy = (boxes[a].y1 < boxes[b].y1 ? boxes[a].y1 : boxes[b].y1) + iy;
              want = !(lx < hx && ly < hy);
            }
          }
          if (want != v.disjoint) {
            bad("the verdict differs from the independent integer box test");
          }
        }
      }
    }
    gshscene_list_destroy(list);
    gshscene_resource_release(u);
  }
  for (int i = 0; i < 8; i++) {
    gshscene_resource_release(palette[i]);
  }
  gshscene_store_destroy(store);
  (void)kind_of;
  (void)map_of;
  (void)tx_of;
  (void)ty_of;
  return 0;
}
