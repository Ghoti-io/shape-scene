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
 * The edit: a working view over a base version (edit.h).
 *
 * The edit records its changes as a set of nodes keyed by ID (a node, or
 * NULL for a removal) and a set of key bindings.  A node a call changes is
 * first copied into the set (a private, mutable clone that only the edit holds)
 * and then changed; the base is never touched.  Every operation reserves what it
 * will need before it changes anything, so a failure leaves the edit as it was.
 */

#include <ghoti.io/shape-scene/macros.h>

#include "internal.h"

#include <stdlib.h>
#include <string.h>

/* --- the sets ----------------------------------------------------------------------------- */

static uint64_t ch_hash(const void * owner, size_t entry) {
  return gshscene_mix64(((const GSHSCENE_Edit *)owner)->ch[entry].id);
}

static int ch_match(const void * owner, size_t entry, const void * key) {
  return ((const GSHSCENE_Edit *)owner)->ch[entry].id == *(const uint64_t *)key;
}

static uint64_t key_hash_of(const GSHSCENE_Key * key) {
  return gshscene_mix64(key->domain ^ gshscene_mix64(key->value));
}

static uint64_t kc_hash(const void * owner, size_t entry) {
  return key_hash_of(&((const GSHSCENE_Edit *)owner)->kc[entry].key);
}

static int kc_match(const void * owner, size_t entry, const void * key) {
  return gshscene_key_compare(&((const GSHSCENE_Edit *)owner)->kc[entry].key, (const GSHSCENE_Key *)key) == 0;
}

static size_t ch_find(const GSHSCENE_Edit * edit, uint64_t id) {
  return gshscene_index_find(&edit->ch_idx, gshscene_mix64(id), edit, ch_match, &id);
}

static size_t kc_find(const GSHSCENE_Edit * edit, const GSHSCENE_Key * key) {
  return gshscene_index_find(&edit->kc_idx, key_hash_of(key), edit, kc_match, key);
}

/* Room for @p extra more changes of nodes, and of keys. */
static GSHSCENE_Result ch_reserve(GSHSCENE_Edit * edit, size_t extra) {
  if (extra > SIZE_MAX / 4 - edit->ch_n) {
    return GSHSCENE_ERR_OOM;
  }
  size_t need = edit->ch_n + extra;
  if (need > edit->ch_cap) {
    size_t cap = edit->ch_cap ? edit->ch_cap : 8;
    while (cap < need) {
      cap *= 2;
    }
    if (cap > SIZE_MAX / sizeof *edit->ch) {
      return GSHSCENE_ERR_OOM;
    }
    GSHSCENE_Change * grown = gshscene_alloc(&edit->core->alloc, cap * sizeof *grown);
    if (!grown) {
      return GSHSCENE_ERR_OOM;
    }
    if (edit->ch_n) {
      memcpy(grown, edit->ch, edit->ch_n * sizeof *grown);
    }
    gshscene_free(&edit->core->alloc, edit->ch);
    edit->ch = grown;
    edit->ch_cap = cap;
  }
  return gshscene_index_reserve(&edit->ch_idx, &edit->core->alloc, edit->ch_n, need, edit, ch_hash);
}

static GSHSCENE_Result kc_reserve(GSHSCENE_Edit * edit, size_t extra) {
  if (extra > SIZE_MAX / 4 - edit->kc_n) {
    return GSHSCENE_ERR_OOM;
  }
  size_t need = edit->kc_n + extra;
  if (need > edit->kc_cap) {
    size_t cap = edit->kc_cap ? edit->kc_cap : 8;
    while (cap < need) {
      cap *= 2;
    }
    if (cap > SIZE_MAX / sizeof *edit->kc) {
      return GSHSCENE_ERR_OOM;
    }
    GSHSCENE_Key_Change * grown = gshscene_alloc(&edit->core->alloc, cap * sizeof *grown);
    if (!grown) {
      return GSHSCENE_ERR_OOM;
    }
    if (edit->kc_n) {
      memcpy(grown, edit->kc, edit->kc_n * sizeof *grown);
    }
    gshscene_free(&edit->core->alloc, edit->kc);
    edit->kc = grown;
    edit->kc_cap = cap;
  }
  return gshscene_index_reserve(&edit->kc_idx, &edit->core->alloc, edit->kc_n, need, edit, kc_hash);
}

/* Record the node (NULL: removed) for @p id; room is reserved.  The set takes the caller's reference on @p node and
 * drops the reference it held on what it replaces. */
static void ch_set(GSHSCENE_Edit * edit, uint64_t id, GSHSCENE_Node_Rec * node) {
  size_t at = ch_find(edit, id);
  if (at != GSHSCENE_NONE) {
    gshscene_node_release(edit->ch[at].node);
    edit->ch[at].node = node;
    return;
  }
  edit->ch[edit->ch_n].id = id;
  edit->ch[edit->ch_n].node = node;
  gshscene_index_insert(&edit->ch_idx, gshscene_mix64(id), edit->ch_n);
  edit->ch_n++;
}

/* Bind (id nonzero) or unbind (id 0) a key; room is reserved. */
static void kc_set(GSHSCENE_Edit * edit, const GSHSCENE_Key * key, uint64_t id) {
  size_t at = kc_find(edit, key);
  if (at != GSHSCENE_NONE) {
    edit->kc[at].id = id;
    return;
  }
  edit->kc[edit->kc_n].key = *key;
  edit->kc[edit->kc_n].id = id;
  gshscene_index_insert(&edit->kc_idx, key_hash_of(key), edit->kc_n);
  edit->kc_n++;
}

/* --- the view ------------------------------------------------------------------------------- */

static GSHSCENE_Node_Rec * view_find(const GSHSCENE_Edit * edit, uint64_t id) {
  size_t at = ch_find(edit, id);
  if (at != GSHSCENE_NONE) {
    return edit->ch[at].node;
  }
  return gshscene_version_find(edit->base, id);
}

static uint64_t view_key(const GSHSCENE_Edit * edit, const GSHSCENE_Key * key) {
  size_t at = kc_find(edit, key);
  if (at != GSHSCENE_NONE) {
    return edit->kc[at].id;
  }
  return gshscene_keys_find(edit->base->keys, key);
}

/* A private, mutable copy of a node of the view, recorded in the set; the same copy on a second call. */
static GSHSCENE_Result touch(GSHSCENE_Edit * edit, uint64_t id, GSHSCENE_Node_Rec ** out) {
  size_t at = ch_find(edit, id);
  if (at != GSHSCENE_NONE) {
    if (!edit->ch[at].node) {
      return GSHSCENE_ERR_INTERNAL;
    }
    *out = edit->ch[at].node;
    return GSHSCENE_OK;
  }
  const GSHSCENE_Node_Rec * base = gshscene_version_find(edit->base, id);
  if (!base) {
    return GSHSCENE_ERR_INTERNAL;
  }
  GSHSCENE_Result result = ch_reserve(edit, 1);
  if (result != GSHSCENE_OK) {
    return result;
  }
  GSHSCENE_Node_Rec * clone = gshscene_node_clone(base);
  if (!clone) {
    return GSHSCENE_ERR_OOM;
  }
  ch_set(edit, id, clone);
  *out = clone;
  return GSHSCENE_OK;
}

static uint64_t take_id(GSHSCENE_Core * core) {
  GCU_MUTEX_LOCK(core->lock);
  uint64_t id = core->next_id++;
  GCU_MUTEX_UNLOCK(core->lock);
  return id;
}

/* --- open and destroy ------------------------------------------------------------------------ */

GSHSCENE_Result gshscene_edit_create(
    GSHSCENE_Core * core, const struct GSHSCENE_Version * base, GSHSCENE_Edit ** out) {
  GSHSCENE_Edit * edit = gshscene_calloc(&core->alloc, 1, sizeof *edit);
  if (!edit) {
    return GSHSCENE_ERR_OOM;
  }
  edit->core = core;
  gshscene_core_ref(core);
  edit->base = gshscene_version_retain(base);
  edit->view_count = base->count;
  *out = edit;
  return GSHSCENE_OK;
}

static void edit_release_changes(GSHSCENE_Edit * edit) {
  for (size_t i = 0; i < edit->ch_n; i++) {
    gshscene_node_release(edit->ch[i].node);
  }
  edit->ch_n = 0;
  edit->kc_n = 0;
  /* The index would still name the released entries. */
  if (edit->ch_idx.slot) {
    memset(edit->ch_idx.slot, 0, edit->ch_idx.cap * sizeof *edit->ch_idx.slot);
  }
  if (edit->kc_idx.slot) {
    memset(edit->kc_idx.slot, 0, edit->kc_idx.cap * sizeof *edit->kc_idx.slot);
  }
}

void gshscene_edit_destroy(GSHSCENE_Edit * edit) {
  if (!edit) {
    return;
  }
  GSHSCENE_Core * core = edit->core;
  GSHSCENE_Allocator alloc = core->alloc;
  edit_release_changes(edit);
  gshscene_free(&alloc, edit->ch);
  gshscene_index_free(&edit->ch_idx, &alloc);
  gshscene_free(&alloc, edit->kc);
  gshscene_index_free(&edit->kc_idx, &alloc);
  gshscene_version_release(edit->base);
  gshscene_free(&alloc, edit);
  gshscene_core_unref(core);
}

/* The edit's changes are spent: published into a version. */
void gshscene_edit_spend(GSHSCENE_Edit * edit) {
  edit_release_changes(edit);
  edit->fg_set = 0;
  edit->spent = 1;
}

GSHSCENE_Result gshscene_edit_set_foreground(GSHSCENE_Edit * edit, const GSHSCENE_Color * color) {
  if (!edit || edit->spent || !color) {
    return GSHSCENE_ERR_INVALID;
  }
  GSHSCENE_Result result = gshscene_color_check(color);
  if (result != GSHSCENE_OK) {
    return result;
  }
  edit->fg = *color;
  edit->fg_set = 1;
  return GSHSCENE_OK;
}

GSHSCENE_Result gshscene_edit_foreground(const GSHSCENE_Edit * edit, GSHSCENE_Color * out) {
  if (!edit || !out) {
    return GSHSCENE_ERR_INVALID;
  }
  *out = edit->fg_set ? edit->fg : edit->base->foreground;
  return GSHSCENE_OK;
}

const GSHSCENE_Version * gshscene_edit_base(const GSHSCENE_Edit * edit) {
  return edit ? edit->base : NULL;
}

GSHSCENE_Store * gshscene_edit_store(const GSHSCENE_Edit * edit) {
  return edit ? edit->core->store : NULL;
}

size_t gshscene_edit_node_count(const GSHSCENE_Edit * edit) {
  return edit ? edit->view_count : 0;
}

/* --- reading ---------------------------------------------------------------------------------- */

GSHSCENE_Result gshscene_edit_node(const GSHSCENE_Edit * edit, uint64_t id, GSHSCENE_Node_Info * out) {
  if (!edit || !out) {
    return GSHSCENE_ERR_INVALID;
  }
  const GSHSCENE_Node_Rec * node = view_find(edit, id);
  if (!node) {
    return GSHSCENE_ERR_INVALID;
  }
  memset(out, 0, sizeof *out);
  out->id = node->id;
  out->parent = node->parent;
  out->has_key = node->has_key;
  if (node->has_key) {
    out->key = node->key;
  }
  out->child_count = node->child_count;
  out->node = node->p;
  return GSHSCENE_OK;
}

GSHSCENE_Result gshscene_edit_child(const GSHSCENE_Edit * edit, uint64_t id, size_t index, uint64_t * out_id) {
  if (!edit || !out_id) {
    return GSHSCENE_ERR_INVALID;
  }
  const GSHSCENE_Node_Rec * node = view_find(edit, id);
  if (!node || node->p.kind != GSHSCENE_NODE_GROUP || index >= node->child_count) {
    return GSHSCENE_ERR_INVALID;
  }
  *out_id = node->children[index];
  return GSHSCENE_OK;
}

GSHSCENE_Result gshscene_edit_lookup_key(const GSHSCENE_Edit * edit, const GSHSCENE_Key * key, uint64_t * out_id) {
  if (!edit || !key || !out_id) {
    return GSHSCENE_ERR_INVALID;
  }
  uint64_t id = view_key(edit, key);
  if (id == 0) {
    return GSHSCENE_ERR_INVALID;
  }
  *out_id = id;
  return GSHSCENE_OK;
}

/* --- add ---------------------------------------------------------------------------------------- */

GSHSCENE_Result gshscene_edit_add(GSHSCENE_Edit * edit, uint64_t parent, size_t position, const GSHSCENE_Node * props,
    const GSHSCENE_Key * key, uint64_t * out_id) {
  if (!edit || !props || edit->spent) {
    return GSHSCENE_ERR_INVALID;
  }
  GSHSCENE_Node canonical;
  GSHSCENE_Result result = gshscene_node_canonical(edit->core->store, props, &canonical);
  if (result != GSHSCENE_OK) {
    return result;
  }
  const GSHSCENE_Node_Rec * parent_node = view_find(edit, parent);
  if (!parent_node || parent_node->p.kind != GSHSCENE_NODE_GROUP) {
    return GSHSCENE_ERR_INVALID;
  }
  if (position != GSHSCENE_APPEND && position > parent_node->child_count) {
    return GSHSCENE_ERR_INVALID;
  }
  if (key && view_key(edit, key) != 0) {
    return GSHSCENE_ERR_INVALID;
  }
  if (edit->view_count >= edit->core->limits.max_nodes) {
    return GSHSCENE_ERR_LIMIT;
  }
  /* Everything that can fail, before anything changes. */
  result = ch_reserve(edit, 2);
  if (result == GSHSCENE_OK && key) {
    result = kc_reserve(edit, 1);
  }
  if (result != GSHSCENE_OK) {
    return result;
  }
  GSHSCENE_Node_Rec * node = gshscene_node_new(edit->core, 0, parent, &canonical);
  if (!node) {
    return GSHSCENE_ERR_OOM;
  }
  GSHSCENE_Node_Rec * parent_copy = NULL;
  result = touch(edit, parent, &parent_copy);
  if (result == GSHSCENE_OK) {
    result = gshscene_node_reserve_children(parent_copy, 1);
  }
  if (result != GSHSCENE_OK) {
    gshscene_node_release(node);
    return result;
  }
  /* Commit: nothing below can fail. */
  size_t at = position == GSHSCENE_APPEND ? parent_copy->child_count : position;
  node->id = take_id(edit->core);
  if (key) {
    node->has_key = 1;
    node->key = *key;
    kc_set(edit, key, node->id);
  }
  gshscene_node_insert_child(parent_copy, at, node->id);
  ch_set(edit, node->id, node);
  edit->view_count++;
  if (out_id) {
    *out_id = node->id;
  }
  return GSHSCENE_OK;
}

/* --- set ----------------------------------------------------------------------------------------- */

GSHSCENE_Result gshscene_edit_set(GSHSCENE_Edit * edit, uint64_t id, const GSHSCENE_Node * props) {
  if (!edit || !props || edit->spent) {
    return GSHSCENE_ERR_INVALID;
  }
  GSHSCENE_Node canonical;
  GSHSCENE_Result result = gshscene_node_canonical(edit->core->store, props, &canonical);
  if (result != GSHSCENE_OK) {
    return result;
  }
  const GSHSCENE_Node_Rec * node = view_find(edit, id);
  if (!node || node->p.kind != canonical.kind) {
    return GSHSCENE_ERR_INVALID;
  }
  if (gshscene_props_equal(&node->p, &canonical)) {
    return GSHSCENE_OK;
  }
  GSHSCENE_Node_Rec * copy = NULL;
  result = touch(edit, id, &copy);
  if (result != GSHSCENE_OK) {
    return result;
  }
  gshscene_node_assign(copy, &canonical);
  return GSHSCENE_OK;
}

/* --- remove ----------------------------------------------------------------------------------------- */

GSHSCENE_Result gshscene_edit_remove(GSHSCENE_Edit * edit, uint64_t id) {
  if (!edit || edit->spent || id == GSHSCENE_ROOT_ID) {
    return GSHSCENE_ERR_INVALID;
  }
  const GSHSCENE_Node_Rec * node = view_find(edit, id);
  if (!node || !view_find(edit, node->parent)) {
    return GSHSCENE_ERR_INVALID;
  }
  const GSHSCENE_Allocator * alloc = &edit->core->alloc;
  /* The subtree, breadth first, in an array that is its own queue. */
  size_t cap = 16;
  size_t n = 1;
  uint64_t * ids = gshscene_alloc(alloc, cap * sizeof *ids);
  if (!ids) {
    return GSHSCENE_ERR_OOM;
  }
  ids[0] = id;
  size_t keyed = 0;
  for (size_t i = 0; i < n; i++) {
    const GSHSCENE_Node_Rec * at = view_find(edit, ids[i]);
    if (!at) {
      gshscene_free(alloc, ids);
      return GSHSCENE_ERR_INTERNAL;
    }
    keyed += at->has_key ? 1u : 0u;
    if (at->child_count > cap - n) {
      size_t grown_cap = cap;
      while (grown_cap - n < at->child_count) {
        grown_cap *= 2;
      }
      uint64_t * grown = grown_cap > SIZE_MAX / sizeof *grown ? NULL : gshscene_alloc(alloc, grown_cap * sizeof *grown);
      if (!grown) {
        gshscene_free(alloc, ids);
        return GSHSCENE_ERR_OOM;
      }
      memcpy(grown, ids, n * sizeof *grown);
      gshscene_free(alloc, ids);
      ids = grown;
      cap = grown_cap;
    }
    if (at->child_count) {
      memcpy(&ids[n], at->children, at->child_count * sizeof *ids);
    }
    n += at->child_count;
  }
  GSHSCENE_Result result = ch_reserve(edit, n + 1);
  if (result == GSHSCENE_OK && keyed) {
    result = kc_reserve(edit, keyed);
  }
  GSHSCENE_Node_Rec * parent_copy = NULL;
  if (result == GSHSCENE_OK) {
    result = touch(edit, node->parent, &parent_copy);
  }
  if (result != GSHSCENE_OK) {
    gshscene_free(alloc, ids);
    return result;
  }
  /* Commit. */
  gshscene_node_remove_child(parent_copy, id);
  for (size_t i = 0; i < n; i++) {
    const GSHSCENE_Node_Rec * at = view_find(edit, ids[i]);
    if (at->has_key) {
      GSHSCENE_Key key = at->key;
      kc_set(edit, &key, 0);
    }
    ch_set(edit, ids[i], NULL);
  }
  edit->view_count -= n;
  gshscene_free(alloc, ids);
  return GSHSCENE_OK;
}

/* --- move --------------------------------------------------------------------------------------------- */

GSHSCENE_Result gshscene_edit_move(GSHSCENE_Edit * edit, uint64_t id, uint64_t new_parent, size_t position) {
  if (!edit || edit->spent || id == GSHSCENE_ROOT_ID) {
    return GSHSCENE_ERR_INVALID;
  }
  const GSHSCENE_Node_Rec * node = view_find(edit, id);
  const GSHSCENE_Node_Rec * target = view_find(edit, new_parent);
  if (!node || !target || target->p.kind != GSHSCENE_NODE_GROUP) {
    return GSHSCENE_ERR_INVALID;
  }
  const GSHSCENE_Node_Rec * old_parent = view_find(edit, node->parent);
  if (!old_parent) {
    return GSHSCENE_ERR_INTERNAL;
  }
  /* Not into its own subtree: walk up from the new parent. */
  uint64_t up = new_parent;
  for (size_t steps = 0; up != 0; steps++) {
    if (up == id || steps > edit->view_count) {
      return GSHSCENE_ERR_INVALID;
    }
    const GSHSCENE_Node_Rec * above = view_find(edit, up);
    if (!above) {
      return GSHSCENE_ERR_INTERNAL;
    }
    up = above->parent;
  }
  int same = new_parent == node->parent;
  size_t after = target->child_count - (same ? 1u : 0u);
  if (position != GSHSCENE_APPEND && position > after) {
    return GSHSCENE_ERR_INVALID;
  }
  size_t at = position == GSHSCENE_APPEND ? after : position;
  if (same) {
    size_t now = 0;
    while (now < old_parent->child_count && old_parent->children[now] != id) {
      now++;
    }
    if (now == at) {
      return GSHSCENE_OK;
    }
  }
  GSHSCENE_Result result = ch_reserve(edit, 3);
  GSHSCENE_Node_Rec * moved = NULL;
  GSHSCENE_Node_Rec * from = NULL;
  GSHSCENE_Node_Rec * to = NULL;
  if (result == GSHSCENE_OK) {
    result = touch(edit, id, &moved);
  }
  if (result == GSHSCENE_OK) {
    result = touch(edit, node->parent, &from);
  }
  if (result == GSHSCENE_OK) {
    result = touch(edit, new_parent, &to);
  }
  if (result == GSHSCENE_OK && !same) {
    result = gshscene_node_reserve_children(to, 1);
  }
  if (result != GSHSCENE_OK) {
    return result;
  }
  gshscene_node_remove_child(from, id);
  gshscene_node_insert_child(to, at, id);
  moved->parent = new_parent;
  return GSHSCENE_OK;
}

/* --- upsert and remove by key ----------------------------------------------------------------------- */

GSHSCENE_Result gshscene_edit_upsert(GSHSCENE_Edit * edit, const GSHSCENE_Key * key, uint64_t parent, size_t position,
    const GSHSCENE_Node * props, uint64_t * out_id) {
  if (!edit || !key || !props || edit->spent) {
    return GSHSCENE_ERR_INVALID;
  }
  uint64_t bound = view_key(edit, key);
  if (bound == 0) {
    return gshscene_edit_add(edit, parent, position, props, key, out_id);
  }
  GSHSCENE_Result result = gshscene_edit_set(edit, bound, props);
  if (result == GSHSCENE_OK && out_id) {
    *out_id = bound;
  }
  return result;
}

GSHSCENE_Result gshscene_edit_remove_key(GSHSCENE_Edit * edit, const GSHSCENE_Key * key) {
  if (!edit || !key || edit->spent) {
    return GSHSCENE_ERR_INVALID;
  }
  uint64_t bound = view_key(edit, key);
  if (bound == 0) {
    return GSHSCENE_ERR_INVALID;
  }
  return gshscene_edit_remove(edit, bound);
}
