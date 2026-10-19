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
 * The retained scene (story 11): an application, a front-end and a driver.
 *
 * The application owns the scene and is the only one that publishes.  The
 * front-end lowers a small document into an edit it is given, by key, so that
 * lowering it again finds its nodes and keeps their IDs; it holds an edit and a
 * store and nothing that can publish.  The driver animates one node, also in an
 * edit.  The application merges the two sibling edits (they touch disjoint
 * nodes), refuses a pair that overlap (naming the IDs), diffs the versions for
 * the damage a sink would redraw, and takes the version's list over interned
 * resources, where a symbol drawn three times is one picture and three items.
 */

#include <ghoti.io/shape-scene/shape-scene.h>

#include <inttypes.h>
#include <stdio.h>

#define FRONT_END 0x44454d4fu /* the front-end's own name: the domain of its keys */

static int check(GSHSCENE_Result r, const char * what) {
  if (r != GSHSCENE_OK) {
    fprintf(stderr, "retained: %s: %s\n", what, gshscene_result_string(r));
    return 1;
  }
  return 0;
}

/* --- the front-end: it is given an edit, and returns it unpublished --------------------------------------- */

typedef struct Document {
  double badge_x;       /* where the badge sits */
  double badge_opacity; /* how faint the badge group is */
} Document;

static GSHSCENE_Node shape_of(const GSHSCENE_Resource * resource, double x, double y, int32_t layer) {
  GSHSCENE_Node node;
  gshscene_node_init(GSHSCENE_NODE_SHAPE, &node);
  node.resource = resource;
  node.layer = layer;
  gshgeom_map_translate(2, x, y, 0.0, &node.map);
  node.paint.rgba[0] = 0.8;
  node.paint.rgba[1] = 0.1;
  node.paint.rgba[2] = 0.1;
  return node;
}

/* Lower the document: every element is an upsert by its key, so a second call updates what the first made. */
static int lower(GSHSCENE_Edit * edit, const Document * doc) {
  int failed = 0;
  GSHSCENE_Store * store = gshscene_edit_store(edit);
  GSHGEOM_Path * path = NULL;
  gshgeom_path_create(NULL, NULL, 2, &path);
  GSHGEOM_Point corners[4] = {{0, 0, 0}, {20, 0, 0}, {20, 20, 0}, {0, 20, 0}};
  gshgeom_path_move_to(path, corners[0]);
  for (int i = 1; i < 4; i++) {
    gshgeom_path_line_to(path, corners[i]);
  }
  gshgeom_path_close(path);
  const GSHSCENE_Resource * square = NULL;
  failed |= check(gshscene_store_intern_path(store, path, &square), "intern");
  gshgeom_path_destroy(path);
  if (failed) {
    return failed;
  }

  /* A hidden symbol (SVG defs/symbol): drawn only through instances. */
  GSHSCENE_Key k_symbol = {FRONT_END, 1}, k_symbol_shape = {FRONT_END, 2}, k_badge = {FRONT_END, 3},
               k_use_a = {FRONT_END, 4}, k_use_b = {FRONT_END, 5}, k_use_c = {FRONT_END, 6};
  uint64_t symbol = 0, ignored = 0, badge = 0;
  GSHSCENE_Node def;
  gshscene_node_init(GSHSCENE_NODE_GROUP, &def);
  def.flags = GSHSCENE_NODE_HIDDEN;
  failed |= check(gshscene_edit_upsert(edit, &k_symbol, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, &def, &symbol), "symbol");
  GSHSCENE_Node shape = shape_of(square, 0, 0, 0);
  /* The symbol is an icon painted with currentColor: its paint is the version's foreground, not a color of its own. */
  shape.paint.kind = GSHSCENE_PAINT_FOREGROUND;
  shape.paint.rgba[0] = shape.paint.rgba[1] = shape.paint.rgba[2] = 0.0;
  failed |= check(gshscene_edit_upsert(edit, &k_symbol_shape, symbol, GSHSCENE_APPEND, &shape, &ignored), "symbol shape");

  /* A faint group holding the badge. */
  GSHSCENE_Node group;
  gshscene_node_init(GSHSCENE_NODE_GROUP, &group);
  group.opacity = doc->badge_opacity;
  failed |= check(gshscene_edit_upsert(edit, &k_badge, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, &group, &badge), "badge group");
  GSHSCENE_Node badge_shape = shape_of(square, doc->badge_x, 5, 1);
  GSHSCENE_Key k_badge_shape = {FRONT_END, 7};
  failed |= check(gshscene_edit_upsert(edit, &k_badge_shape, badge, GSHSCENE_APPEND, &badge_shape, &ignored), "badge shape");

  /* Three uses of the symbol (SVG use): instances of one node. */
  const GSHSCENE_Key * uses[3] = {&k_use_a, &k_use_b, &k_use_c};
  for (int i = 0; i < 3; i++) {
    GSHSCENE_Node use;
    gshscene_node_init(GSHSCENE_NODE_INSTANCE, &use);
    use.target = symbol;
    gshgeom_map_translate(2, 30.0 * (i + 1), 60.0, 0.0, &use.map);
    failed |= check(gshscene_edit_upsert(edit, uses[i], GSHSCENE_ROOT_ID, GSHSCENE_APPEND, &use, &ignored), "use");
  }
  gshscene_resource_release(square);
  return failed;
}

/* --- the driver: it moves one node, in an edit it is given -------------------------------------------------- */

static int animate(GSHSCENE_Edit * edit, uint64_t id, double dx) {
  GSHSCENE_Node_Info info;
  if (check(gshscene_edit_node(edit, id, &info), "driver read")) {
    return 1;
  }
  gshgeom_map_translate(2, dx, 0.0, 0.0, &info.node.map);
  return check(gshscene_edit_set(edit, id, &info.node), "driver set");
}

/* --- the application ----------------------------------------------------------------------------------------- */

static void print_damage(const GSHSCENE_Diff * diff) {
  GSHSCENE_Damage d;
  gshscene_diff_damage(diff, &d);
  if (d.unbounded) {
    printf("  damage: unbounded (redraw everything)\n");
  } else if (d.empty) {
    printf("  damage: none\n");
  } else {
    printf("  damage: x %" PRId64 "..%" PRId64 ", y %" PRId64 "..%" PRId64 "\n", d.rect.x0, d.rect.x1, d.rect.y0, d.rect.y1);
  }
}

int main(void) {
  int failed = 0;
  GSHSCENE_Scene * scene = NULL;
  failed |= check(gshscene_scene_create(NULL, NULL, NULL, &scene), "scene");

  /* The first version: the application opens an edit, hands it to the front-end, and publishes what comes back. */
  Document doc = {50.0, 0.5};
  GSHSCENE_Edit * edit = NULL;
  failed |= check(gshscene_scene_open_edit(scene, NULL, &edit), "open edit");
  failed |= lower(edit, &doc);
  const GSHSCENE_Version * v1 = NULL;
  failed |= check(gshscene_scene_publish(scene, edit, NULL, &v1), "publish");
  gshscene_edit_destroy(edit);
  printf("version %" PRIu64 ": %zu nodes, hash %016" PRIx64 "\n", gshscene_version_serial(v1), gshscene_version_node_count(v1),
      gshscene_version_hash(v1));

  /* Lowering the same document again changes nothing: the keys find the nodes, and no version is made. */
  failed |= check(gshscene_scene_open_edit(scene, NULL, &edit), "open edit");
  failed |= lower(edit, &doc);
  const GSHSCENE_Version * same = NULL;
  failed |= check(gshscene_scene_publish(scene, edit, NULL, &same), "publish again");
  gshscene_edit_destroy(edit);
  printf("the same document again: %s\n", same == v1 ? "no new version" : "A NEW VERSION");
  failed |= same != v1;
  gshscene_version_release(same);

  /* Two sibling edits on the first version: the front-end moves the badge, the driver moves a use. */
  GSHSCENE_Key k_use_a = {FRONT_END, 4};
  uint64_t use_a = 0;
  failed |= check(gshscene_version_lookup_key(v1, &k_use_a, &use_a), "find use");
  GSHSCENE_Edit * from_front_end = NULL;
  GSHSCENE_Edit * from_driver = NULL;
  failed |= check(gshscene_scene_open_edit(scene, v1, &from_front_end), "open");
  failed |= check(gshscene_scene_open_edit(scene, v1, &from_driver), "open");
  doc.badge_x = 70.0;
  failed |= lower(from_front_end, &doc);
  failed |= animate(from_driver, use_a, 12.0);
  GSHSCENE_Edit * siblings[2] = {from_front_end, from_driver};
  const GSHSCENE_Version * v2 = NULL;
  failed |= check(gshscene_scene_merge(scene, siblings, 2, NULL, &v2), "merge");
  printf("merged the front-end's and the driver's edits into version %" PRIu64 "\n", gshscene_version_serial(v2));

  /* What changed, and what a sink must redraw through its map (a scale of 2). */
  GSHGEOM_Map device;
  gshgeom_map_scale(2, 2.0, 2.0, 1.0, &device);
  GSHSCENE_Diff * diff = NULL;
  failed |= check(gshscene_diff(v1, v2, &device, NULL, &diff), "diff");
  printf("changed IDs:");
  for (size_t i = 0; i < gshscene_diff_count(diff); i++) {
    uint64_t id;
    GSHSCENE_Diff_Kind kind;
    gshscene_diff_get(diff, i, &id, &kind);
    printf(" %" PRIu64 "%s", id, kind == GSHSCENE_DIFF_CHANGED ? "" : (kind == GSHSCENE_DIFF_ADDED ? "+" : "-"));
  }
  printf("\n");
  print_damage(diff);
  gshscene_diff_destroy(diff);
  gshscene_edit_destroy(from_front_end);
  gshscene_edit_destroy(from_driver);

  /* The foreground is a parameter of a version: an edit sets it, and the damage is the shapes that paint with it. */
  GSHSCENE_Edit * recolor = NULL;
  failed |= check(gshscene_scene_open_edit(scene, v2, &recolor), "open");
  GSHSCENE_Color teal = {.rgba = {0.0, 0.5, 0.5, 1.0}};
  failed |= check(gshscene_edit_set_foreground(recolor, &teal), "foreground");
  const GSHSCENE_Version * v3 = NULL;
  failed |= check(gshscene_scene_publish(scene, recolor, NULL, &v3), "publish the foreground");
  gshscene_edit_destroy(recolor);
  GSHSCENE_Diff * recolored = NULL;
  failed |= check(gshscene_diff(v2, v3, &device, NULL, &recolored), "diff");
  printf("a new foreground: %zu changed IDs, foreground changed: %d\n", gshscene_diff_count(recolored),
      gshscene_diff_foreground_changed(recolored));
  print_damage(recolored);
  failed |= gshscene_diff_count(recolored) != 0 || !gshscene_diff_foreground_changed(recolored);
  gshscene_diff_destroy(recolored);
  gshscene_version_release(v3);

  /* Two edits that both touch one node are refused, naming it. */
  GSHSCENE_Edit * a = NULL;
  GSHSCENE_Edit * b = NULL;
  failed |= check(gshscene_scene_open_edit(scene, v2, &a), "open");
  failed |= check(gshscene_scene_open_edit(scene, v2, &b), "open");
  failed |= animate(a, use_a, 1.0);
  failed |= animate(b, use_a, 2.0);
  GSHSCENE_Edit * clash[2] = {a, b};
  GSHSCENE_Problems * problems = NULL;
  GSHSCENE_Result refused = gshscene_scene_merge(scene, clash, 2, &problems, NULL);
  printf("two edits of the same node: %s", gshscene_result_string(refused));
  for (size_t i = 0; i < gshscene_problems_count(problems); i++) {
    uint64_t id;
    GSHSCENE_Problem_Reason reason;
    gshscene_problems_get(problems, i, &id, &reason);
    printf(" (%s: %" PRIu64 ")", gshscene_problem_reason_string(reason), id);
  }
  printf("\n");
  failed |= refused != GSHSCENE_ERR_REFUSED;
  gshscene_problems_destroy(problems);
  gshscene_edit_destroy(a);
  gshscene_edit_destroy(b);

  /* The version's list over interned resources: the symbol is one picture, drawn by three instance items. */
  const GSHSCENE_List * list = NULL;
  failed |= check(gshscene_version_list(v2, &list), "list");
  printf("the list: %zu items, expansion %zu, hash %016" PRIx64 "\n", gshscene_list_count(list), gshscene_list_expansion(list),
      gshscene_list_hash(list));
  failed |= check(gshscene_list_dump(list, stdout), "dump");
  GSHSCENE_Key k_symbol = {FRONT_END, 1};
  uint64_t symbol = 0;
  failed |= check(gshscene_version_lookup_key(v2, &k_symbol, &symbol), "find symbol");
  const GSHSCENE_Resource * picture = NULL;
  failed |= check(gshscene_version_picture(v2, symbol, &picture), "picture");
  GSHSCENE_Store_Stats stats;
  gshscene_store_stats(gshscene_scene_store(scene), &stats);
  printf("live resources: %" PRIu64 " (the square, and the symbol's picture)\n", stats.live);

  gshscene_version_release(v2);
  gshscene_version_release(v1);
  gshscene_scene_destroy(scene);
  return failed;
}
