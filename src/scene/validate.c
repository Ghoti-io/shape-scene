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
 * Validation of a candidate version at publish: the structure (every child is
 * the child of exactly one group and every node is reachable), the instances
 * (every target exists, no cycle) and the structural limits of AD-11.
 *
 * One depth-first walk over the nodes computes, for each node and memoised, the
 * quantities the limits bound: its depth, the items its content emits as a
 * list, its occurrences, the expansion a list of it would have, the push
 * nesting and the picture nesting.  The recursion is bounded by
 * `max_node_depth` (at most ::GSHSCENE_NODE_DEPTH_CEILING) because the limit is
 * tested on the way down, so a hostile chain cannot overflow the stack.
 */

#include <ghoti.io/shape-scene/macros.h>

#include "internal.h"

#include <stdlib.h>
#include <string.h>

/* --- problems ------------------------------------------------------------------------------ */

const char * gshscene_problem_reason_string(GSHSCENE_Problem_Reason reason) {
  switch (reason) {
    case GSHSCENE_PROBLEM_OVERLAP:
      return "overlap";
    case GSHSCENE_PROBLEM_KEY_CONFLICT:
      return "key-conflict";
    case GSHSCENE_PROBLEM_DANGLING_TARGET:
      return "dangling-target";
    case GSHSCENE_PROBLEM_CYCLE:
      return "cycle";
    case GSHSCENE_PROBLEM_PARAMETER:
      return "parameter";
    case GSHSCENE_PROBLEM_REASON_COUNT:
      break;
  }
  return "unknown problem";
}

void gshscene_problems_add(GSHSCENE_Problem_List * list, const GSHSCENE_Allocator * alloc, uint64_t id,
    GSHSCENE_Problem_Reason reason, GSHSCENE_Result * oom) {
  if (list->count == list->cap) {
    size_t cap = list->cap ? list->cap * 2 : 8;
    GSHSCENE_Problem * grown = gshscene_alloc(alloc, cap * sizeof *grown);
    if (!grown) {
      *oom = GSHSCENE_ERR_OOM;
      return;
    }
    if (list->count) {
      memcpy(grown, list->items, list->count * sizeof *grown);
    }
    gshscene_free(alloc, list->items);
    list->items = grown;
    list->cap = cap;
  }
  list->items[list->count].id = id;
  list->items[list->count].reason = reason;
  list->count++;
}

static int problem_compare(const void * pa, const void * pb) {
  const GSHSCENE_Problem * a = pa;
  const GSHSCENE_Problem * b = pb;
  if (a->id != b->id) {
    return a->id < b->id ? -1 : 1;
  }
  if (a->reason != b->reason) {
    return a->reason < b->reason ? -1 : 1;
  }
  return 0;
}

GSHSCENE_Result gshscene_problems_from_list(
    GSHSCENE_Problem_List * list, const GSHSCENE_Allocator * alloc, GSHSCENE_Problems ** out) {
  if (list->count > 1) {
    qsort(list->items, list->count, sizeof *list->items, problem_compare);
  }
  size_t unique = 0;
  for (size_t i = 0; i < list->count; i++) {
    if (unique == 0 || problem_compare(&list->items[unique - 1], &list->items[i]) != 0) {
      list->items[unique++] = list->items[i];
    }
  }
  GSHSCENE_Problems * problems = gshscene_alloc(alloc, sizeof *problems + unique * sizeof(GSHSCENE_Problem));
  if (!problems) {
    gshscene_free(alloc, list->items);
    list->items = NULL;
    return GSHSCENE_ERR_OOM;
  }
  problems->alloc = *alloc;
  problems->count = unique;
  if (unique) {
    memcpy(problems->items, list->items, unique * sizeof(GSHSCENE_Problem));
  }
  gshscene_free(alloc, list->items);
  list->items = NULL;
  *out = problems;
  return GSHSCENE_OK;
}

size_t gshscene_problems_count(const GSHSCENE_Problems * problems) {
  return problems ? problems->count : 0;
}

GSHSCENE_Result gshscene_problems_get(const GSHSCENE_Problems * problems, size_t index, uint64_t * out_id,
    GSHSCENE_Problem_Reason * out_reason) {
  if (!problems || !out_id || !out_reason || index >= problems->count) {
    return GSHSCENE_ERR_INVALID;
  }
  *out_id = problems->items[index].id;
  *out_reason = problems->items[index].reason;
  return GSHSCENE_OK;
}

void gshscene_problems_destroy(GSHSCENE_Problems * problems) {
  if (problems) {
    GSHSCENE_Allocator alloc = problems->alloc;
    gshscene_free(&alloc, problems);
  }
}

/* --- the checks ------------------------------------------------------------------------------- */

enum { UNSEEN = 0, VISITING = 1, DONE = 2 };

typedef struct Memo {
  size_t depth; /* levels of nodes below and including this one, an instance's target counting as its child */
  size_t items; /* items its content emits as a list (a hidden child counts for nothing) */
  size_t occ;   /* occurrences of it, with every instance expanded */
  size_t exp;   /* the expansion a list of its content has */
  size_t push;  /* the deepest push nesting of that list */
  size_t pic;   /* the deepest picture its instances name */
  size_t refs;  /* how many children lists name it */
  unsigned char color;
} Memo;

typedef struct Check {
  const GSHSCENE_Core * core;
  GSHSCENE_Node_Rec * const * nodes;
  size_t count;
  Memo * memo;
  GSHSCENE_Problem_List problems;
  GSHSCENE_Result oom;
  size_t reached;
} Check;

static size_t max_of(size_t a, size_t b) {
  return a > b ? a : b;
}

static GSHSCENE_Result visit(Check * c, size_t index, size_t level);

static int hidden(const GSHSCENE_Node_Rec * node) {
  return (node->p.flags & GSHSCENE_NODE_HIDDEN) != 0;
}

static GSHSCENE_Result visit_group(Check * c, size_t index, size_t level, Memo * m) {
  const GSHSCENE_Node_Rec * node = c->nodes[index];
  /* The most wrappers it can have: a group whose parent orders its children pushes unless it is an instance's
   * target, where the sequence is flat, so the count is an upper bound and never an under-count. */
  uint32_t context = GSHSCENE_ORDER_FLAT;
  if (node->parent != 0) {
    size_t at = gshscene_nodes_find(c->nodes, c->count, node->parent);
    if (at != GSHSCENE_NONE) {
      context = c->nodes[at]->p.ordering;
    }
  }
  size_t wrappers = (gshscene_group_pushes(&node->p, context) ? 1u : 0u) + (node->p.clip ? 1u : 0u);
  m->depth = 1;
  m->items = 2 * wrappers;
  m->exp = 2 * wrappers;
  m->occ = 1;
  /* T7: a soft mask is a picture the emitted list names, so it counts toward the nesting as an instance's does. */
  m->pic = node->p.soft_mask ? gshscene_resource_picture_depth(node->p.soft_mask) : 0;
  for (size_t i = 0; i < node->child_count; i++) {
    size_t j = gshscene_nodes_find(c->nodes, c->count, node->children[i]);
    if (j == GSHSCENE_NONE || c->nodes[j]->parent != node->id) {
      return GSHSCENE_ERR_INTERNAL;
    }
    c->memo[j].refs++;
    GSHSCENE_Result result = visit(c, j, level + 1);
    if (result != GSHSCENE_OK) {
      return result;
    }
    const Memo * child = &c->memo[j];
    m->depth = max_of(m->depth, gshscene_sat_add(1, child->depth));
    if (!hidden(c->nodes[j])) {
      m->items = gshscene_sat_add(m->items, child->items);
      m->occ = gshscene_sat_add(m->occ, child->occ);
      m->exp = gshscene_sat_add(m->exp, child->exp);
      m->push = max_of(m->push, child->push);
      m->pic = max_of(m->pic, child->pic);
    }
  }
  m->push = gshscene_sat_add(m->push, wrappers);
  return GSHSCENE_OK;
}

static GSHSCENE_Result visit_instance(Check * c, size_t index, size_t level, Memo * m) {
  const GSHSCENE_Node_Rec * node = c->nodes[index];
  const GSHSCENE_Limits * limits = &c->core->limits;
  m->depth = 1;
  m->items = 1;
  m->occ = 1;
  m->exp = 1;
  size_t t = gshscene_nodes_find(c->nodes, c->count, node->p.target);
  if (t == GSHSCENE_NONE) {
    gshscene_problems_add(&c->problems, &c->core->alloc, node->id, GSHSCENE_PROBLEM_DANGLING_TARGET, &c->oom);
    return GSHSCENE_OK;
  }
  if (c->memo[t].color == VISITING) {
    gshscene_problems_add(&c->problems, &c->core->alloc, node->id, GSHSCENE_PROBLEM_CYCLE, &c->oom);
    return GSHSCENE_OK;
  }
  GSHSCENE_Result result = visit(c, t, level + 1);
  if (result != GSHSCENE_OK) {
    return result;
  }
  const Memo * target = &c->memo[t];
  m->depth = gshscene_sat_add(1, target->depth);
  m->occ = gshscene_sat_add(1, target->occ);
  m->exp = gshscene_sat_add(1, target->exp);
  m->pic = gshscene_sat_add(1, target->pic);
  /* The target becomes a picture: a list of its own content, which has to fit the lists' limits. */
  if (m->pic > limits->max_picture_depth || target->items > limits->max_resource_elements ||
      target->items > limits->max_list_items || target->push > limits->max_depth ||
      target->exp > limits->max_expansion) {
    return GSHSCENE_ERR_LIMIT;
  }
  return GSHSCENE_OK;
}

static GSHSCENE_Result visit(Check * c, size_t index, size_t level) {
  Memo * m = &c->memo[index];
  if (m->color == DONE) {
    return GSHSCENE_OK;
  }
  if (m->color == VISITING) { /* a cycle through an instance's target, met again as a group's child: name it */
    gshscene_problems_add(&c->problems, &c->core->alloc, c->nodes[index]->id, GSHSCENE_PROBLEM_CYCLE, &c->oom);
    return GSHSCENE_OK;
  } else if (level > c->core->limits.max_node_depth) {
    return GSHSCENE_ERR_LIMIT;
  }
  m->color = VISITING;
  c->reached++;
  GSHSCENE_Result result;
  switch (c->nodes[index]->p.kind) {
    case GSHSCENE_NODE_GROUP:
      result = visit_group(c, index, level, m);
      break;
    case GSHSCENE_NODE_SHAPE:
    case GSHSCENE_NODE_STROKE: /* one item, as a shape */
      m->depth = 1;
      m->items = 1;
      m->occ = 1;
      m->exp = 1;
      result = GSHSCENE_OK;
      break;
    default:
      result = visit_instance(c, index, level, m);
      break;
  }
  if (result != GSHSCENE_OK) {
    return result;
  }
  if (m->depth > c->core->limits.max_node_depth) {
    return GSHSCENE_ERR_LIMIT;
  }
  m->color = DONE;
  return GSHSCENE_OK;
}

GSHSCENE_Result gshscene_validate(const GSHSCENE_Core * core, GSHSCENE_Node_Rec * const * nodes, size_t count,
    GSHSCENE_Problems ** problems) {
  const GSHSCENE_Limits * limits = &core->limits;
  if (count > limits->max_nodes) {
    return GSHSCENE_ERR_LIMIT;
  }
  if (count == 0 || nodes[0]->id != GSHSCENE_ROOT_ID || nodes[0]->parent != 0 ||
      nodes[0]->p.kind != GSHSCENE_NODE_GROUP) {
    return GSHSCENE_ERR_INTERNAL;
  }
  Check check;
  memset(&check, 0, sizeof check);
  check.core = core;
  check.nodes = nodes;
  check.count = count;
  check.memo = gshscene_calloc(&core->alloc, count, sizeof *check.memo);
  if (!check.memo) {
    return GSHSCENE_ERR_OOM;
  }
  GSHSCENE_Result result = visit(&check, 0, 1);
  if (result == GSHSCENE_OK && check.oom != GSHSCENE_OK) {
    result = check.oom;
  }
  if (result == GSHSCENE_OK && check.problems.count == 0) {
    const Memo * root = &check.memo[0];
    if (check.reached != count) {
      result = GSHSCENE_ERR_INTERNAL;
    } else if (root->occ > limits->max_expansion || root->exp > limits->max_expansion ||
               root->items > limits->max_list_items || root->push > limits->max_depth) {
      result = GSHSCENE_ERR_LIMIT;
    } else {
      for (size_t i = 1; i < count && result == GSHSCENE_OK; i++) {
        if (check.memo[i].refs != 1) {
          result = GSHSCENE_ERR_INTERNAL;
        }
      }
    }
  } else if (result == GSHSCENE_OK) {
    result = GSHSCENE_ERR_INVALID;
  }
  gshscene_free(&core->alloc, check.memo);
  if (result == GSHSCENE_ERR_INVALID) {
    GSHSCENE_Problems * made = NULL;
    GSHSCENE_Result built = gshscene_problems_from_list(&check.problems, &core->alloc, &made);
    if (built != GSHSCENE_OK) {
      return built;
    }
    if (problems) {
      *problems = made;
    } else {
      gshscene_problems_destroy(made);
    }
    return GSHSCENE_ERR_INVALID;
  }
  gshscene_free(&core->alloc, check.problems.items);
  return result;
}
