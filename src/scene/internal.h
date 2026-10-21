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
 * The structures of the retained scene (nodes, versions, edits, the scene's core)
 * and the functions its source files share.  Internal: not installed.
 *
 * Ownership.  A node record is immutable once a version holds it; until then it
 * is owned by one edit (reference count 1) and mutable.  A version holds one
 * reference on each of its nodes and one on the scene's core; the core holds
 * the current version, and ::gshscene_scene_destroy releases it, which is what
 * keeps the two from holding each other.
 */

#ifndef GHOTI_IO_GSHSCENE_SCENE_INTERNAL_H
#define GHOTI_IO_GSHSCENE_SCENE_INTERNAL_H

#include <ghoti.io/shape-scene/macros.h>

#include "../internal.h"

#include <ghoti.io/shape-geometry/curve.h>

struct GSHSCENE_Version;

/** The format of a version's hash and dump. */
#define VERSION_FORMAT_VERSION 4u

/** The kind field of a version's hash: "SCEN" in little-endian ASCII. */
#define VERSION_HASH_KIND UINT32_C(0x4e454353)

/** A sentinel for "no index". */
#define GSHSCENE_NONE ((size_t)-1)

/* --- the open-addressing index over a dense array ---------------------------- */

typedef struct GSHSCENE_Index {
  size_t * slot; /* entry index + 1; 0 is empty */
  size_t cap;    /* a power of two, or 0 */
} GSHSCENE_Index;

typedef uint64_t (*GSHSCENE_Index_Hash)(const void * owner, size_t entry);
typedef int (*GSHSCENE_Index_Match)(const void * owner, size_t entry, const void * key);

/**
 * Make room for @p want entries (the owner holds @p have of them now) without a further allocation.  A failure leaves
 * the index as it was.
 */
GSHSCENE_Result gshscene_index_reserve(GSHSCENE_Index * index, const GSHSCENE_Allocator * alloc, size_t have,
    size_t want, const void * owner, GSHSCENE_Index_Hash hash);

/** The entry with this hash that @p match accepts, or ::GSHSCENE_NONE. */
size_t gshscene_index_find(const GSHSCENE_Index * index, uint64_t hash, const void * owner,
    GSHSCENE_Index_Match match, const void * key);

/** Insert entry number @p entry (the index has room: ::gshscene_index_reserve). */
void gshscene_index_insert(GSHSCENE_Index * index, uint64_t hash, size_t entry);

void gshscene_index_free(GSHSCENE_Index * index, const GSHSCENE_Allocator * alloc);

/** A finalising mix of a 64-bit number (not a hash that is ever stored or compared across runs). */
uint64_t gshscene_mix64(uint64_t x);

/* --- core, nodes, keys ------------------------------------------------------------ */

typedef struct GSHSCENE_Core {
  GCU_Atomic_Size refs; /* the scene handle, every version, every edit, every node */
  GSHSCENE_Allocator alloc;
  GSHSCENE_Limits limits; /* effective: no zero field */
  GSHSCENE_Store * store; /* a reference is held */
  GCU_MUTEX_T lock;     /* guards next_id, versions and current */
  uint64_t next_id;
  uint64_t versions;
  const struct GSHSCENE_Version * current;
} GSHSCENE_Core;

typedef struct GSHSCENE_Node_Rec {
  GCU_Atomic_Size refs;
  GSHSCENE_Core * core;
  uint64_t id;
  uint64_t parent;
  int has_key;
  GSHSCENE_Key key;
  GSHSCENE_Node p; /* canonical */
  uint64_t * children;
  size_t child_count;
  size_t child_cap;
} GSHSCENE_Node_Rec;

typedef struct GSHSCENE_Key_Entry {
  GSHSCENE_Key key;
  uint64_t id;
} GSHSCENE_Key_Entry;

/** A sorted, shared array of key bindings. */
typedef struct GSHSCENE_Keys {
  GCU_Atomic_Size refs;
  GSHSCENE_Allocator alloc;
  size_t count;
  GSHSCENE_Key_Entry entries[]; /* count of them, ascending by key */
} GSHSCENE_Keys;

struct GSHSCENE_Emission;

struct GSHSCENE_Version {
  GCU_Atomic_Size refs;
  GSHSCENE_Core * core;
  uint64_t number;
  GSHSCENE_Node_Rec ** nodes; /* ascending by id */
  size_t count;
  GSHSCENE_Keys * keys;       /* may be NULL for none */
  GSHSCENE_Color foreground;
  const GSHSCENE_Resource * palette; /* the palette parameter (N14), retained; NULL for none */
  GCU_MUTEX_T lock;         /* guards the emission */
  struct GSHSCENE_Emission * emission;
};

/** One recorded change of an edit: a node (NULL: removed). */
typedef struct GSHSCENE_Change {
  uint64_t id;
  GSHSCENE_Node_Rec * node;
} GSHSCENE_Change;

/** One recorded change of a key binding (id 0: unbound). */
typedef struct GSHSCENE_Key_Change {
  GSHSCENE_Key key;
  uint64_t id;
} GSHSCENE_Key_Change;

struct GSHSCENE_Edit {
  GSHSCENE_Core * core;
  const struct GSHSCENE_Version * base;
  int spent;
  GSHSCENE_Change * ch;
  size_t ch_n;
  size_t ch_cap;
  GSHSCENE_Index ch_idx;
  GSHSCENE_Key_Change * kc;
  size_t kc_n;
  size_t kc_cap;
  GSHSCENE_Index kc_idx;
  size_t view_count;
  int fg_set;             /* the edit has set the foreground (it may still equal the base's) */
  GSHSCENE_Color fg;
  int pal_set;            /* the edit has set the palette (it may still be the base's) */
  const GSHSCENE_Resource * pal; /* retained; NULL for none */
};

/** One problem found. */
typedef struct GSHSCENE_Problem {
  uint64_t id;
  GSHSCENE_Problem_Reason reason;
} GSHSCENE_Problem;

struct GSHSCENE_Problems {
  GSHSCENE_Allocator alloc;
  size_t count;
  GSHSCENE_Problem items[];
};

/** A growing list of problems being collected. */
typedef struct GSHSCENE_Problem_List {
  GSHSCENE_Problem * items;
  size_t count;
  size_t cap;
} GSHSCENE_Problem_List;

/* --- node.c ---------------------------------------------------------------------- */

void gshscene_core_ref(GSHSCENE_Core * core);
void gshscene_core_unref(GSHSCENE_Core * core);

/** A node record of kind @p props, canonical, with one reference, a reference on the core and on its resources.  NULL on OOM. */
GSHSCENE_Node_Rec * gshscene_node_new(GSHSCENE_Core * core, uint64_t id, uint64_t parent, const GSHSCENE_Node * canonical);
GSHSCENE_Node_Rec * gshscene_node_clone(const GSHSCENE_Node_Rec * node);
void gshscene_node_retain(GSHSCENE_Node_Rec * node);
void gshscene_node_release(GSHSCENE_Node_Rec * node);

/** Replace the properties (canonical) of a node that nothing else holds. */
void gshscene_node_assign(GSHSCENE_Node_Rec * node, const GSHSCENE_Node * canonical);

/**
 * Whether a group is emitted as a group push and pop when the sequence it is in has context @p context (N11):
 * its opacity is below 1, its operator is not source-over, its blend space is not zero, its ordering is not flat, or
 * @p context is not flat (it stays one unit of that sequence and carries its own context).
 */
int gshscene_group_pushes(const GSHSCENE_Node * group, uint32_t context);

/** Whether two canonical property sets are equal, bit for bit. */
int gshscene_props_equal(const GSHSCENE_Node * a, const GSHSCENE_Node * b);

/** Whether two nodes are equal: id, parent, key, properties and children. */
int gshscene_node_equal(const GSHSCENE_Node_Rec * a, const GSHSCENE_Node_Rec * b);

/** Validate @p in for a node of the scene's store and write its canonical form. */
GSHSCENE_Result gshscene_node_canonical(const GSHSCENE_Store * store, const GSHSCENE_Node * in, GSHSCENE_Node * out);

/** Add one node to a version's hash stream (the spelling in version.h). */
void gshscene_node_hash(GSHGEOM_Hash * h, const GSHSCENE_Node_Rec * node);

/** Insert @p child at @p position in the node's children (room is already reserved). */
void gshscene_node_insert_child(GSHSCENE_Node_Rec * node, size_t position, uint64_t child);

/** Remove the child with this id; returns its position or ::GSHSCENE_NONE. */
size_t gshscene_node_remove_child(GSHSCENE_Node_Rec * node, uint64_t child);

/** Room for @p extra more children.  A failure leaves the node as it was. */
GSHSCENE_Result gshscene_node_reserve_children(GSHSCENE_Node_Rec * node, size_t extra);

int gshscene_key_compare(const GSHSCENE_Key * a, const GSHSCENE_Key * b);

/* --- the list.c helpers the node validation shares ---------------------------------- */

GSHSCENE_Result gshscene_list_check_rule(GSHGEOM_Fill_Rule rule);

/* --- edit.c ----------------------------------------------------------------------- */

/** An edit over @p base (retained) of the core's scene, with nothing changed. */
GSHSCENE_Result gshscene_edit_create(GSHSCENE_Core * core, const struct GSHSCENE_Version * base, GSHSCENE_Edit ** out);

/** Release the edit's changes and mark it spent: they have been published. */
void gshscene_edit_spend(GSHSCENE_Edit * edit);

/* --- version.c --------------------------------------------------------------------- */

/** The node of this id in the version, or NULL. */
GSHSCENE_Node_Rec * gshscene_version_find(const struct GSHSCENE_Version * version, uint64_t id);

/** Index of the node of this id in a sorted array of nodes, or ::GSHSCENE_NONE. */
size_t gshscene_nodes_find(GSHSCENE_Node_Rec * const * nodes, size_t count, uint64_t id);

/** The key array's binding of a key: the node id, or 0. */
uint64_t gshscene_keys_find(const GSHSCENE_Keys * keys, const GSHSCENE_Key * key);

void gshscene_keys_ref(GSHSCENE_Keys * keys);
void gshscene_keys_unref(GSHSCENE_Keys * keys);

/**
 * A new version over @p nodes (the version retains each and takes the array), @p keys (retained), the foreground, the
 * palette (retained; NULL for none) and the core.
 */
GSHSCENE_Result gshscene_version_make(GSHSCENE_Core * core, uint64_t number, GSHSCENE_Node_Rec ** nodes,
    size_t count, GSHSCENE_Keys * keys, const GSHSCENE_Color * foreground, const GSHSCENE_Resource * palette,
    const struct GSHSCENE_Version ** out);

void gshscene_emission_free(struct GSHSCENE_Emission * emission, const GSHSCENE_Allocator * alloc);

/* --- the one walk ------------------------------------------------------------------ */

struct GSHSCENE_Walk;

typedef struct GSHSCENE_Walk_Ops {
  /** A group is entered: @p base is its parent's placement composed with its own map. */
  GSHSCENE_Result (*group_enter)(void * ctx, const struct GSHSCENE_Walk * walk, const GSHSCENE_Node_Rec * group,
      const GSHGEOM_Map * base);
  GSHSCENE_Result (*group_leave)(void * ctx, const struct GSHSCENE_Walk * walk, const GSHSCENE_Node_Rec * group);
  /** A shape: @p map is its parent's placement composed with its own. */
  GSHSCENE_Result (*shape)(void * ctx, const struct GSHSCENE_Walk * walk, const GSHSCENE_Node_Rec * shape,
      const GSHGEOM_Map * map);
  /** An instance: @p map likewise.  Set *descend to walk the target's content with this instance on the path. */
  GSHSCENE_Result (*instance)(void * ctx, const struct GSHSCENE_Walk * walk, const GSHSCENE_Node_Rec * instance,
      const GSHGEOM_Map * map, int * descend);
  /** After the target of an instance that was descended into has been walked. */
  GSHSCENE_Result (*instance_leave)(void * ctx, const struct GSHSCENE_Walk * walk, const GSHSCENE_Node_Rec * instance);
  /** Optional: where the consumer is (the number of items it has made), asked before and after each child of a depth group. */
  size_t (*position)(void * ctx);
  /**
   * Optional: two adjacent units of a depth sequence tied on depth, the units that began at positions @p first and
   * @p second (as ::position gave them).  Called after the later unit has been walked, so the ties are not in order.
   */
  GSHSCENE_Result (*tie)(void * ctx, const struct GSHSCENE_Walk * walk, size_t first, size_t second, double depth,
      GSHSCENE_Tie_Reason reason);
} GSHSCENE_Walk_Ops;

/** What the callbacks may read of the walk's state. */
typedef struct GSHSCENE_Walk {
  const struct GSHSCENE_Version * version;
  const GSHSCENE_Walk_Ops * ops;
  void * ctx;
  uint64_t * path;       /* the ids of the instances entered, then room for the node */
  size_t path_count;     /* instances entered */
  GSHGEOM_Map * chain;   /* their item maps */
  size_t chain_cap;
  uint32_t context;      /* while a group callback runs: the ordering context of the sequence the group is in */
} GSHSCENE_Walk;

/**
 * Walk @p node (and below) in draw order.  @p force draws the node although it is hidden (an instance's target, a
 * picture's content).  @p base is the placement of the walk's space.  The chain and path stacks are allocated here for
 * `max_node_depth + 2` entries.  Returns the first non-OK result of a callback or a composition.
 */
GSHSCENE_Result gshscene_walk(const struct GSHSCENE_Version * version, const GSHSCENE_Node_Rec * node, int force,
    const GSHGEOM_Map * base, const GSHSCENE_Walk_Ops * ops, void * ctx);

/** The composed placement of an item the walk is at: the instance maps of the chain, then @p item_map, then @p target. */
GSHSCENE_Result gshscene_walk_compose(const GSHSCENE_Walk * walk, const GSHGEOM_Map * item_map,
    const GSHGEOM_Map * target, GSHGEOM_Map * out);

/* --- validate.c -------------------------------------------------------------------- */

void gshscene_problems_add(GSHSCENE_Problem_List * list, const GSHSCENE_Allocator * alloc, uint64_t id,
    GSHSCENE_Problem_Reason reason, GSHSCENE_Result * oom);

/** A Problems object from a list (sorted, without duplicates); the list is freed.  OOM leaves *out NULL. */
GSHSCENE_Result gshscene_problems_from_list(
    GSHSCENE_Problem_List * list, const GSHSCENE_Allocator * alloc, GSHSCENE_Problems ** out);

/**
 * Validate a candidate version: every parent and child consistent, every instance target present, no cycle, every
 * structural limit.  ::GSHSCENE_OK; ::GSHSCENE_ERR_LIMIT; ::GSHSCENE_ERR_INVALID with @p problems filled;
 * ::GSHSCENE_ERR_OOM; ::GSHSCENE_ERR_INTERNAL for an inconsistency no valid edit can make.
 */
GSHSCENE_Result gshscene_validate(const GSHSCENE_Core * core, GSHSCENE_Node_Rec * const * nodes, size_t count,
    GSHSCENE_Problems ** problems);

/* --- diff.c ------------------------------------------------------------------------ */

typedef struct GSHSCENE_Diff_Entry {
  uint64_t id;
  GSHSCENE_Diff_Kind kind;
} GSHSCENE_Diff_Entry;

/** The ids that differ between two versions of one scene, ascending, allocated from the core's allocator. */
GSHSCENE_Result gshscene_diff_entries(const struct GSHSCENE_Version * a, const struct GSHSCENE_Version * b,
    GSHSCENE_Diff_Entry ** out, size_t * count);

#endif /* GHOTI_IO_GSHSCENE_SCENE_INTERNAL_H */
