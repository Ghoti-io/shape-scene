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
 * One frame, recorded in immediate mode: no IDs, no versions, no scene.
 *
 * It interns a path and a run of glyphs once (the second intern of equal content
 * returns the same handle and copies nothing), freezes a small drawing into a
 * picture, records a list that fills, clips, groups and instances, closes it and
 * prints its dump and hash.  A sink would take the closed list and the resources
 * it names; here the list is dumped and freed in one call, and the frame loop's
 * reset is shown.
 */

#include <ghoti.io/shape-scene/shape-scene.h>

#include <inttypes.h>
#include <stdio.h>

static int check(GSHSCENE_Result r, const char * what) {
  if (r != GSHSCENE_OK) {
    fprintf(stderr, "frame: %s: %s\n", what, gshscene_result_string(r));
    return 1;
  }
  return 0;
}

int main(void) {
  int failed = 0;
  GSHSCENE_Store * store = NULL;
  failed |= check(gshscene_store_create(NULL, NULL, &store), "store");

  /* A square, as a model path. */
  GSHGEOM_Path * path = NULL;
  gshgeom_path_create(NULL, NULL, 2, &path);
  GSHGEOM_Point corners[4] = {{0, 0, 0}, {40, 0, 0}, {40, 40, 0}, {0, 40, 0}};
  gshgeom_path_move_to(path, corners[0]);
  for (int i = 1; i < 4; i++) {
    gshgeom_path_line_to(path, corners[i]);
  }
  gshgeom_path_close(path);
  const GSHSCENE_Resource * square = NULL;
  const GSHSCENE_Resource * again = NULL;
  failed |= check(gshscene_store_intern_path(store, path, &square), "intern path");
  failed |= check(gshscene_store_intern_path(store, path, &again), "intern path again");
  printf("equal content, one handle: %s\n", square == again ? "yes" : "NO");
  failed |= (square != again);
  gshscene_resource_release(again);
  gshgeom_path_destroy(path);

  /* A run of glyphs: a face (a font's key, its index in a collection, its variation) and glyph ids.  This face is
   * bound to no glyph path provider (a font binds its own faces), so a sink refuses the run; glyph_provider.h. */
  GSHSCENE_Face_Desc face_desc = {0x1122334455667788u, 0, 0, NULL, NULL, NULL};
  const GSHSCENE_Resource * face = NULL;
  failed |= check(gshscene_store_intern_face(store, &face_desc, &face), "intern face");
  GSHSCENE_Glyph glyphs[3] = {{36, 0.0, 0.0, NULL}, {37, 9.5, 0.0, NULL}, {38, 19.0, 0.0, NULL}};
  GSHSCENE_Glyph_Run run = {GSHSCENE_GLYPHS_FROM_FONT, face, 16.0, 0, 0, 3, glyphs};
  const GSHSCENE_Resource * text = NULL;
  failed |= check(gshscene_store_intern_glyph_run(store, &run, &text), "intern glyph run");
  gshscene_resource_release(face); /* the run holds its own reference */

  /* A picture: a closed list that instances draw under a map (what SVG's use and symbol lower to). */
  GSHSCENE_List * badge = NULL;
  failed |= check(gshscene_list_create(NULL, NULL, store, &badge), "badge list");
  GSHSCENE_Paint red = {.kind = GSHSCENE_PAINT_SOLID, .rgba = {0.8, 0.1, 0.1, 1.0}};
  failed |= check(gshscene_list_fill(badge, square, NULL, GSHGEOM_FILL_NONZERO, &red, 0, 0), "badge fill");
  failed |= check(gshscene_list_close(badge), "badge close");
  const GSHSCENE_Resource * badge_picture = NULL;
  failed |= check(gshscene_store_intern_picture(store, badge, &badge_picture), "freeze badge");
  gshscene_list_destroy(badge);

  /* The frame: a list owned by the caller, reset for the next frame. */
  GSHSCENE_List * list = NULL;
  failed |= check(gshscene_list_create(NULL, NULL, store, &list), "list");
  for (int frame = 0; frame < 2; frame++) {
    GSHGEOM_Map place;
    gshgeom_map_translate(2, 10.0 * (frame + 1), 20.0, 0.0, &place);
    GSHSCENE_Paint blue = {.kind = GSHSCENE_PAINT_SOLID, .rgba = {0.1, 0.2, 0.9, 1.0}};
    /* currentColor: the ink is the list's foreground, so changing the foreground changes the ink without re-recording. */
    GSHSCENE_Paint ink = {.kind = GSHSCENE_PAINT_FOREGROUND, .rgba = {0.0, 0.0, 0.0, 1.0}};
    GSHSCENE_Color foreground = {.rgba = {0.05, 0.1, 0.4, 1.0},
        .space = {.primaries = GCOL_PRIMARIES_SRGB, .transfer = GCOL_TRANSFER_SRGB}};
    failed |= check(gshscene_list_set_foreground(list, &foreground), "foreground");
    /* The group orders its content by depth, far to near: the text (depth 3) is painted before the badge (depth 2). */
    GSHSCENE_Group half = {.opacity = 0.5, .ordering = GSHSCENE_ORDER_DEPTH};
    failed |= check(gshscene_list_clip_push(list, square, &place, GSHGEOM_FILL_NONZERO, 0, 0), "clip");
    failed |= check(gshscene_list_fill(list, square, &place, GSHGEOM_FILL_NONZERO, &blue, 0, 0), "fill");
    failed |= check(gshscene_list_group_push(list, &half, 1, 0), "group");
    failed |= check(gshscene_list_instance(list, badge_picture, &place, 1, 2.0), "instance");
    failed |= check(gshscene_list_fill(list, text, &place, GSHGEOM_FILL_NONZERO, &ink, 2, 3.0), "text");
    failed |= check(gshscene_list_group_pop(list), "group pop");
    failed |= check(gshscene_list_clip_pop(list), "clip pop");
    failed |= check(gshscene_list_close(list), "close");
    printf("frame %d: %zu items, expansion %zu, hash %016" PRIx64 "\n", frame, gshscene_list_count(list),
        gshscene_list_expansion(list), gshscene_list_hash(list));
    if (frame == 0) {
      failed |= check(gshscene_list_dump(list, stdout), "dump");
      failed |= check(gshscene_list_reset(list), "reset");
    }
  }
  gshscene_list_destroy(list);

  gshscene_resource_release(badge_picture);
  gshscene_resource_release(text);
  gshscene_resource_release(square);
  GSHSCENE_Store_Stats stats;
  gshscene_store_stats(store, &stats);
  printf("resources still live after every release: %" PRIu64 "\n", stats.live);
  failed |= (stats.live != 0);
  gshscene_store_destroy(store);
  return failed;
}
