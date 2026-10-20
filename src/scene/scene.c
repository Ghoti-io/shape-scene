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
 * The scene: its core (allocator, limits, store, the ID counter and the current
 * version), and publish and merge (scene.h).
 *
 * A publish runs under the scene's lock from the first look at the current
 * version to its replacement, so two publishes are never interleaved.  What it
 * builds (the result's node array, its key array) is held in temporaries and
 * released on any failure; nothing of the scene or the edits changes until the
 * new version has been made.
 */

#include <ghoti.io/shape-scene/macros.h>

#include "internal.h"

#include <stdlib.h>
#include <string.h>

struct GSHSCENE_Scene {
  GSHSCENE_Core * core;
};

/* --- create and destroy -------------------------------------------------------------------------- */

GSHSCENE_Result gshscene_scene_create(const GSHSCENE_Allocator * alloc, const GSHSCENE_Limits * limits,
    GSHSCENE_Store * store, GSHSCENE_Scene ** out) {
  if (!out) {
    return GSHSCENE_ERR_INVALID;
  }
  if (!alloc) {
    alloc = gshscene_allocator_default();
  }
  GSHSCENE_Scene * scene = gshscene_calloc(alloc, 1, sizeof *scene);
  GSHSCENE_Core * core = gshscene_calloc(alloc, 1, sizeof *core);
  if (!scene || !core) {
    gshscene_free(alloc, scene);
    gshscene_free(alloc, core);
    return GSHSCENE_ERR_OOM;
  }
  core->alloc = *alloc;
  gshscene_limits_effective(limits, &core->limits);
  gcu_atomic_size_init(&core->refs, 1);
  core->next_id = GSHSCENE_ROOT_ID + 1;
  if (GCU_MUTEX_CREATE(core->lock) != 0) {
    gshscene_free(alloc, scene);
    gshscene_free(alloc, core);
    return GSHSCENE_ERR_INTERNAL;
  }
  GSHSCENE_Result result = GSHSCENE_OK;
  if (store) {
    gshscene_store_ref(store);
    core->store = store;
  } else {
    result = gshscene_store_create(&core->alloc, &core->limits, &core->store);
  }
  GSHSCENE_Node root_props;
  gshscene_node_init(GSHSCENE_NODE_GROUP, &root_props);
  GSHSCENE_Node_Rec * root = NULL;
  GSHSCENE_Node_Rec ** nodes = NULL;
  if (result == GSHSCENE_OK) {
    root = gshscene_node_new(core, GSHSCENE_ROOT_ID, 0, &root_props);
    nodes = gshscene_alloc(&core->alloc, sizeof *nodes);
    if (!root || !nodes) {
      result = GSHSCENE_ERR_OOM;
    }
  }
  if (result == GSHSCENE_OK) {
    nodes[0] = root;
    GSHSCENE_Color foreground;
    gshscene_color_init(&foreground);
    result = gshscene_version_make(core, 0, nodes, 1, NULL, &foreground, NULL, &core->current);
  }
  if (result != GSHSCENE_OK) {
    gshscene_free(&core->alloc, nodes);
    gshscene_node_release(root);
    GSHSCENE_Allocator copy = core->alloc;
    gshscene_free(&copy, scene);
    if (core->store) {
      gshscene_store_destroy(core->store);
    }
    GCU_MUTEX_DESTROY(core->lock);
    gshscene_free(&copy, core);
    return result;
  }
  gshscene_node_release(root); /* the version holds it now */
  scene->core = core;
  *out = scene;
  return GSHSCENE_OK;
}

void gshscene_scene_destroy(GSHSCENE_Scene * scene) {
  if (!scene) {
    return;
  }
  GSHSCENE_Core * core = scene->core;
  GSHSCENE_Allocator alloc = core->alloc;
  GCU_MUTEX_LOCK(core->lock);
  const struct GSHSCENE_Version * current = core->current;
  core->current = NULL;
  GCU_MUTEX_UNLOCK(core->lock);
  gshscene_version_release(current);
  gshscene_free(&alloc, scene);
  gshscene_core_unref(core);
}

GSHSCENE_Store * gshscene_scene_store(const GSHSCENE_Scene * scene) {
  return scene ? scene->core->store : NULL;
}

GSHSCENE_Result gshscene_scene_current(GSHSCENE_Scene * scene, const GSHSCENE_Version ** out) {
  if (!scene || !out) {
    return GSHSCENE_ERR_INVALID;
  }
  GCU_MUTEX_LOCK(scene->core->lock);
  *out = gshscene_version_retain(scene->core->current);
  GCU_MUTEX_UNLOCK(scene->core->lock);
  return GSHSCENE_OK;
}

GSHSCENE_Result gshscene_scene_open_edit(GSHSCENE_Scene * scene, const GSHSCENE_Version * base, GSHSCENE_Edit ** out) {
  if (!scene || !out) {
    return GSHSCENE_ERR_INVALID;
  }
  GSHSCENE_Core * core = scene->core;
  if (base && base->core != core) {
    return GSHSCENE_ERR_INVALID;
  }
  const GSHSCENE_Version * held = base;
  if (!held) {
    GCU_MUTEX_LOCK(core->lock);
    held = gshscene_version_retain(core->current);
    GCU_MUTEX_UNLOCK(core->lock);
  }
  GSHSCENE_Result result = gshscene_edit_create(core, held, out);
  if (!base) {
    gshscene_version_release(held);
  }
  return result;
}

/* --- plans --------------------------------------------------------------------------------------- */

/* What one edit changes, normalised: the entries that equal the base's are dropped. */
typedef struct Plan {
  GSHSCENE_Edit * edit;
  GSHSCENE_Change * ch; /* ascending by id */
  size_t ch_n;
  GSHSCENE_Key_Change * kc; /* ascending by key */
  size_t kc_n;
  int fg; /* the edit sets the foreground to something its base does not have */
  int pal; /* the edit sets the palette to another than its base's */
} Plan;

static int color_equal(const GSHSCENE_Color * a, const GSHSCENE_Color * b) {
  for (int i = 0; i < 4; i++) {
    if (gshgeom_f64_bits(a->rgba[i]) != gshgeom_f64_bits(b->rgba[i])) {
      return 0;
    }
  }
  return gshscene_space_equal(&a->space, &b->space);
}

static int change_compare(const void * pa, const void * pb) {
  const GSHSCENE_Change * a = pa;
  const GSHSCENE_Change * b = pb;
  return a->id < b->id ? -1 : (a->id > b->id ? 1 : 0);
}

static int key_change_compare(const void * pa, const void * pb) {
  return gshscene_key_compare(&((const GSHSCENE_Key_Change *)pa)->key, &((const GSHSCENE_Key_Change *)pb)->key);
}

static void plan_free(const GSHSCENE_Allocator * alloc, Plan * plan) {
  gshscene_free(alloc, plan->ch);
  gshscene_free(alloc, plan->kc);
}

static GSHSCENE_Result plan_make(const GSHSCENE_Allocator * alloc, GSHSCENE_Edit * edit, Plan * plan) {
  const struct GSHSCENE_Version * base = edit->base;
  memset(plan, 0, sizeof *plan);
  plan->edit = edit;
  if (edit->ch_n) {
    plan->ch = gshscene_alloc(alloc, edit->ch_n * sizeof *plan->ch);
    if (!plan->ch) {
      return GSHSCENE_ERR_OOM;
    }
  }
  for (size_t i = 0; i < edit->ch_n; i++) {
    const GSHSCENE_Node_Rec * was = gshscene_version_find(base, edit->ch[i].id);
    const GSHSCENE_Node_Rec * now = edit->ch[i].node;
    int differs = now ? (!was || !gshscene_node_equal(was, now)) : was != NULL;
    if (differs) {
      plan->ch[plan->ch_n++] = edit->ch[i];
    }
  }
  if (plan->ch_n > 1) {
    qsort(plan->ch, plan->ch_n, sizeof *plan->ch, change_compare);
  }
  if (edit->kc_n) {
    plan->kc = gshscene_alloc(alloc, edit->kc_n * sizeof *plan->kc);
    if (!plan->kc) {
      gshscene_free(alloc, plan->ch);
      plan->ch = NULL;
      return GSHSCENE_ERR_OOM;
    }
  }
  for (size_t i = 0; i < edit->kc_n; i++) {
    if (edit->kc[i].id != gshscene_keys_find(base->keys, &edit->kc[i].key)) {
      plan->kc[plan->kc_n++] = edit->kc[i];
    }
  }
  if (plan->kc_n > 1) {
    qsort(plan->kc, plan->kc_n, sizeof *plan->kc, key_change_compare);
  }
  plan->fg = edit->fg_set && !color_equal(&edit->fg, &base->foreground);
  plan->pal = edit->pal_set && edit->pal != base->palette;
  return GSHSCENE_OK;
}

/* --- conflicts ------------------------------------------------------------------------------------ */

typedef struct Conflicts {
  GSHSCENE_Problem_List list;
  GSHSCENE_Result oom;
  const GSHSCENE_Allocator * alloc;
} Conflicts;

static void conflict(Conflicts * c, uint64_t id, GSHSCENE_Problem_Reason reason) {
  if (id != 0) {
    gshscene_problems_add(&c->list, c->alloc, id, reason, &c->oom);
  }
}

/* The ids two ascending change arrays share. */
static void overlap_changes(Conflicts * c, const GSHSCENE_Change * a, size_t an, const GSHSCENE_Change * b, size_t bn) {
  size_t i = 0;
  size_t j = 0;
  while (i < an && j < bn) {
    if (a[i].id == b[j].id) {
      conflict(c, a[i].id, GSHSCENE_PROBLEM_OVERLAP);
      i++;
      j++;
    } else if (a[i].id < b[j].id) {
      i++;
    } else {
      j++;
    }
  }
}

/* The ids a plan shares with what other publishes changed. */
static void overlap_diff(
    Conflicts * c, const GSHSCENE_Change * a, size_t an, const GSHSCENE_Diff_Entry * d, size_t dn) {
  size_t i = 0;
  size_t j = 0;
  while (i < an && j < dn) {
    if (a[i].id == d[j].id) {
      conflict(c, a[i].id, GSHSCENE_PROBLEM_OVERLAP);
      i++;
      j++;
    } else if (a[i].id < d[j].id) {
      i++;
    } else {
      j++;
    }
  }
}

/* A key that two edits touch, or that an edit touches and a publish since its base changed. */
static void key_conflicts(Conflicts * c, const Plan * plans, size_t n, const struct GSHSCENE_Version * current) {
  for (size_t i = 0; i < n; i++) {
    for (size_t k = 0; k < plans[i].kc_n; k++) {
      const GSHSCENE_Key_Change * mine = &plans[i].kc[k];
      uint64_t then = gshscene_keys_find(plans[i].edit->base->keys, &mine->key);
      uint64_t now = gshscene_keys_find(current->keys, &mine->key);
      if (now != then) {
        conflict(c, now, GSHSCENE_PROBLEM_KEY_CONFLICT);
        conflict(c, mine->id, GSHSCENE_PROBLEM_KEY_CONFLICT);
      }
      for (size_t j = i + 1; j < n; j++) {
        for (size_t m = 0; m < plans[j].kc_n; m++) {
          if (gshscene_key_compare(&plans[j].kc[m].key, &mine->key) == 0) {
            conflict(c, mine->id, GSHSCENE_PROBLEM_KEY_CONFLICT);
            conflict(c, plans[j].kc[m].id, GSHSCENE_PROBLEM_KEY_CONFLICT);
          }
        }
      }
    }
  }
}

/* The version's parameters: two plans that both set the foreground, or one that does where a publish since its base
 * did; likewise the palette (N14). */
static void parameter_conflicts(Conflicts * c, const Plan * plans, size_t n, const struct GSHSCENE_Version * current) {
  for (size_t i = 0; i < n; i++) {
    if (plans[i].fg) {
      int clash = !color_equal(&plans[i].edit->base->foreground, &current->foreground);
      for (size_t j = i + 1; j < n && !clash; j++) {
        clash = plans[j].fg;
      }
      if (clash) {
        conflict(c, GSHSCENE_ROOT_ID, GSHSCENE_PROBLEM_PARAMETER);
      }
    }
    if (plans[i].pal) {
      int clash = plans[i].edit->base->palette != current->palette;
      for (size_t j = i + 1; j < n && !clash; j++) {
        clash = plans[j].pal;
      }
      if (clash) {
        conflict(c, GSHSCENE_ROOT_ID, GSHSCENE_PROBLEM_PARAMETER);
      }
    }
  }
}

/* --- the result ----------------------------------------------------------------------------------- */

/* The new key array: the current one with every plan's changes applied.  NULL with *oom unset: no keys at all. */
static GSHSCENE_Keys * keys_merged(const GSHSCENE_Allocator * alloc, const struct GSHSCENE_Version * current,
    const Plan * plans, size_t n, GSHSCENE_Result * result) {
  size_t changes = 0;
  for (size_t i = 0; i < n; i++) {
    changes += plans[i].kc_n;
  }
  size_t have = current->keys ? current->keys->count : 0;
  if (changes == 0) {
    if (current->keys) {
      gshscene_keys_ref(current->keys);
    }
    return current->keys;
  }
  GSHSCENE_Key_Change * all = gshscene_alloc(alloc, changes * sizeof *all);
  if (!all) {
    *result = GSHSCENE_ERR_OOM;
    return NULL;
  }
  size_t at = 0;
  for (size_t i = 0; i < n; i++) {
    if (plans[i].kc_n) {
      memcpy(&all[at], plans[i].kc, plans[i].kc_n * sizeof *all);
    }
    at += plans[i].kc_n;
  }
  qsort(all, changes, sizeof *all, key_change_compare);
  if (have + changes > (SIZE_MAX - sizeof(GSHSCENE_Keys)) / sizeof(GSHSCENE_Key_Entry)) {
    gshscene_free(alloc, all);
    *result = GSHSCENE_ERR_OOM;
    return NULL;
  }
  GSHSCENE_Keys * keys = gshscene_alloc(alloc, sizeof *keys + (have + changes) * sizeof(GSHSCENE_Key_Entry));
  if (!keys) {
    gshscene_free(alloc, all);
    *result = GSHSCENE_ERR_OOM;
    return NULL;
  }
  gcu_atomic_size_init(&keys->refs, 1);
  keys->alloc = *alloc;
  size_t out = 0;
  size_t i = 0;
  size_t j = 0;
  while (i < have || j < changes) {
    int take_current;
    if (i >= have) {
      take_current = 0;
    } else if (j >= changes) {
      take_current = 1;
    } else {
      int c = gshscene_key_compare(&current->keys->entries[i].key, &all[j].key);
      if (c == 0) { /* the key is changed: the plan's binding replaces the current one */
        i++;
        continue;
      }
      take_current = c < 0;
    }
    if (take_current) {
      keys->entries[out++] = current->keys->entries[i++];
    } else {
      if (all[j].id != 0) {
        keys->entries[out].key = all[j].key;
        keys->entries[out].id = all[j].id;
        out++;
      }
      j++;
    }
  }
  keys->count = out;
  gshscene_free(alloc, all);
  if (out == 0) {
    gshscene_keys_unref(keys);
    return NULL;
  }
  return keys;
}

/* Merge the current nodes with every plan's changes.  The result array is sized for the most it can hold. */
static GSHSCENE_Result nodes_merged(const GSHSCENE_Allocator * alloc, const struct GSHSCENE_Version * current,
    const Plan * plans, size_t n, GSHSCENE_Node_Rec *** out, size_t * out_count) {
  size_t changes = 0;
  for (size_t i = 0; i < n; i++) {
    changes += plans[i].ch_n;
  }
  GSHSCENE_Change * all = NULL;
  if (changes) {
    all = gshscene_alloc(alloc, changes * sizeof *all);
    if (!all) {
      return GSHSCENE_ERR_OOM;
    }
    size_t at = 0;
    for (size_t i = 0; i < n; i++) {
      if (plans[i].ch_n) {
        memcpy(&all[at], plans[i].ch, plans[i].ch_n * sizeof *all);
      }
      at += plans[i].ch_n;
    }
    qsort(all, changes, sizeof *all, change_compare);
  }
  size_t room = current->count + changes;
  GSHSCENE_Node_Rec ** result = room > SIZE_MAX / sizeof *result ? NULL : gshscene_alloc(alloc, room * sizeof *result);
  if (!result) {
    gshscene_free(alloc, all);
    return GSHSCENE_ERR_OOM;
  }
  size_t count = 0;
  size_t i = 0;
  size_t j = 0;
  GSHSCENE_Result status = GSHSCENE_OK;
  while ((i < current->count || j < changes) && status == GSHSCENE_OK) {
    if (j < changes && j + 1 < changes && all[j].id == all[j + 1].id) {
      status = GSHSCENE_ERR_INTERNAL; /* two edits changed one id: the overlap check should have refused it */
    } else if (j < changes && (i >= current->count || all[j].id < current->nodes[i]->id)) {
      if (all[j].node) {
        result[count++] = all[j].node;
      } else {
        status = GSHSCENE_ERR_INTERNAL; /* a removal of a node the current version does not have */
      }
      j++;
    } else if (j < changes && all[j].id == current->nodes[i]->id) {
      if (all[j].node) {
        result[count++] = all[j].node;
      }
      i++;
      j++;
    } else {
      result[count++] = current->nodes[i++];
    }
  }
  gshscene_free(alloc, all);
  if (status != GSHSCENE_OK) {
    gshscene_free(alloc, result);
    return status;
  }
  *out = result;
  *out_count = count;
  return GSHSCENE_OK;
}

/* --- publish --------------------------------------------------------------------------------------- */

static GSHSCENE_Result publish_locked(GSHSCENE_Core * core, GSHSCENE_Edit * const * edits, size_t n,
    GSHSCENE_Problems ** problems, const GSHSCENE_Version ** out_version) {
  const GSHSCENE_Allocator * alloc = &core->alloc;
  const struct GSHSCENE_Version * current = core->current;
  const struct GSHSCENE_Version * base = edits[0]->base;
  if (!current || base->number > current->number) {
    return GSHSCENE_ERR_INVALID;
  }
  Plan * plans = gshscene_calloc(alloc, n, sizeof *plans);
  if (!plans) {
    return GSHSCENE_ERR_OOM;
  }
  GSHSCENE_Result result = GSHSCENE_OK;
  size_t made = 0;
  size_t touched = 0;
  for (size_t i = 0; i < n && result == GSHSCENE_OK; i++) {
    result = plan_make(alloc, edits[i], &plans[i]);
    if (result == GSHSCENE_OK) {
      made++;
      touched += plans[i].ch_n + plans[i].kc_n + (plans[i].fg ? 1u : 0u) + (plans[i].pal ? 1u : 0u);
    }
  }
  GSHSCENE_Diff_Entry * since = NULL;
  size_t since_n = 0;
  GSHSCENE_Node_Rec ** nodes = NULL;
  size_t node_n = 0;
  GSHSCENE_Keys * keys = NULL;
  const GSHSCENE_Version * version = NULL;
  GSHSCENE_Problems * found = NULL;

  if (result == GSHSCENE_OK && touched == 0) {
    /* Nothing to publish: the current version is the answer. */
    if (out_version) {
      *out_version = gshscene_version_retain(current);
    }
    goto done;
  }
  if (result == GSHSCENE_OK) {
    Conflicts conflicts;
    memset(&conflicts, 0, sizeof conflicts);
    conflicts.alloc = alloc;
    if (base != current) {
      result = gshscene_diff_entries(base, current, &since, &since_n);
    }
    if (result == GSHSCENE_OK) {
      for (size_t i = 0; i < n; i++) {
        for (size_t j = i + 1; j < n; j++) {
          overlap_changes(&conflicts, plans[i].ch, plans[i].ch_n, plans[j].ch, plans[j].ch_n);
        }
        overlap_diff(&conflicts, plans[i].ch, plans[i].ch_n, since, since_n);
      }
      key_conflicts(&conflicts, plans, n, current);
      parameter_conflicts(&conflicts, plans, n, current);
      if (conflicts.oom != GSHSCENE_OK) {
        result = conflicts.oom;
        gshscene_free(alloc, conflicts.list.items);
      } else if (conflicts.list.count) {
        result = gshscene_problems_from_list(&conflicts.list, alloc, &found);
        if (result == GSHSCENE_OK) {
          result = GSHSCENE_ERR_REFUSED;
        }
        goto done;
      } else {
        gshscene_free(alloc, conflicts.list.items);
      }
    }
  }
  if (result == GSHSCENE_OK) {
    result = nodes_merged(alloc, current, plans, n, &nodes, &node_n);
  }
  if (result == GSHSCENE_OK) {
    GSHSCENE_Result keyed = GSHSCENE_OK;
    keys = keys_merged(alloc, current, plans, n, &keyed);
    result = keyed;
  }
  if (result == GSHSCENE_OK) {
    result = gshscene_validate(core, nodes, node_n, &found);
  }
  if (result == GSHSCENE_OK) {
    GSHSCENE_Color foreground = current->foreground;
    const GSHSCENE_Resource * palette = current->palette;
    for (size_t i = 0; i < n; i++) {
      if (plans[i].fg) {
        foreground = plans[i].edit->fg;
      }
      if (plans[i].pal) {
        palette = plans[i].edit->pal;
      }
    }
    result = gshscene_version_make(core, core->versions + 1, nodes, node_n, keys, &foreground, palette, &version);
  }
  if (result == GSHSCENE_OK) {
    /* Commit: nothing below fails. */
    nodes = NULL; /* the version owns the array */
    core->versions++;
    const struct GSHSCENE_Version * old = core->current;
    core->current = version;
    for (size_t i = 0; i < n; i++) {
      gshscene_edit_spend(edits[i]);
    }
    if (out_version) {
      *out_version = gshscene_version_retain(version);
    }
    gshscene_version_release(old);
  }

done:
  gshscene_free(alloc, since);
  gshscene_free(alloc, nodes);
  gshscene_keys_unref(keys);
  for (size_t i = 0; i < made; i++) {
    plan_free(alloc, &plans[i]);
  }
  gshscene_free(alloc, plans);
  if (found) {
    if (problems) {
      *problems = found;
    } else {
      gshscene_problems_destroy(found);
    }
  }
  return result;
}

GSHSCENE_Result gshscene_scene_merge(GSHSCENE_Scene * scene, GSHSCENE_Edit * const * edits, size_t count,
    GSHSCENE_Problems ** problems, const GSHSCENE_Version ** out_version) {
  if (!scene || !edits || count == 0) {
    return GSHSCENE_ERR_INVALID;
  }
  GSHSCENE_Core * core = scene->core;
  for (size_t i = 0; i < count; i++) {
    if (!edits[i] || edits[i]->spent || edits[i]->core != core || edits[i]->base != edits[0]->base) {
      return GSHSCENE_ERR_INVALID;
    }
    for (size_t j = 0; j < i; j++) {
      if (edits[j] == edits[i]) {
        return GSHSCENE_ERR_INVALID;
      }
    }
  }
  GCU_MUTEX_LOCK(core->lock);
  GSHSCENE_Result result = publish_locked(core, edits, count, problems, out_version);
  GCU_MUTEX_UNLOCK(core->lock);
  return result;
}

GSHSCENE_Result gshscene_scene_publish(GSHSCENE_Scene * scene, GSHSCENE_Edit * edit, GSHSCENE_Problems ** problems,
    const GSHSCENE_Version ** out_version) {
  return gshscene_scene_merge(scene, &edit, 1, problems, out_version);
}
