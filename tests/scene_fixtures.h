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
 * Builders and holders for the retained scene's tests (story 11).
 */

#ifndef GHOTI_IO_GSHSCENE_TEST_SCENE_FIXTURES_H
#define GHOTI_IO_GSHSCENE_TEST_SCENE_FIXTURES_H

#include "scene_helpers.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace scene {

/** Owns a scene (the application's handle). */
struct SceneHolder {
  GSHSCENE_Scene * scene = nullptr;
  explicit SceneHolder(const GSHSCENE_Limits * limits = nullptr, const GSHSCENE_Allocator * alloc = nullptr,
      GSHSCENE_Store * store = nullptr) {
    if (gshscene_scene_create(alloc, limits, store, &scene) != GSHSCENE_OK) {
      scene = nullptr;
    }
  }
  ~SceneHolder() { gshscene_scene_destroy(scene); }
  SceneHolder(const SceneHolder &) = delete;
  SceneHolder & operator=(const SceneHolder &) = delete;
  GSHSCENE_Store * store() const { return gshscene_scene_store(scene); }
};

/** Owns an edit (until it is published, or always: destroy after a publish is fine). */
struct EditHolder {
  GSHSCENE_Edit * edit = nullptr;
  explicit EditHolder(SceneHolder & s, const GSHSCENE_Version * base = nullptr) {
    if (gshscene_scene_open_edit(s.scene, base, &edit) != GSHSCENE_OK) {
      edit = nullptr;
    }
  }
  ~EditHolder() { gshscene_edit_destroy(edit); }
  EditHolder(const EditHolder &) = delete;
  EditHolder & operator=(const EditHolder &) = delete;
};

/** Owns a reference to a version. */
struct VersionRef {
  const GSHSCENE_Version * version = nullptr;
  VersionRef() = default;
  explicit VersionRef(const GSHSCENE_Version * v) : version(v) {}
  ~VersionRef() { gshscene_version_release(version); }
  VersionRef(const VersionRef &) = delete;
  VersionRef & operator=(const VersionRef &) = delete;
  VersionRef(VersionRef && other) noexcept : version(other.version) { other.version = nullptr; }
  VersionRef & operator=(VersionRef && other) noexcept {
    if (this != &other) {
      gshscene_version_release(version);
      version = other.version;
      other.version = nullptr;
    }
    return *this;
  }
  const GSHSCENE_Version * get() const { return version; }
  void reset(const GSHSCENE_Version * v = nullptr) {
    gshscene_version_release(version);
    version = v;
  }
};

/** Owns problems. */
struct ProblemsRef {
  GSHSCENE_Problems * problems = nullptr;
  ProblemsRef() = default;
  ~ProblemsRef() { gshscene_problems_destroy(problems); }
  ProblemsRef(const ProblemsRef &) = delete;
  ProblemsRef & operator=(const ProblemsRef &) = delete;
  /** The IDs, in order, of every problem of the reason (or of every reason: 0). */
  std::vector<uint64_t> ids(int reason = 0) const {
    std::vector<uint64_t> out;
    for (size_t i = 0; i < gshscene_problems_count(problems); i++) {
      uint64_t id;
      GSHSCENE_Problem_Reason r;
      EXPECT_EQ(gshscene_problems_get(problems, i, &id, &r), GSHSCENE_OK);
      if (reason == 0 || (int)r == reason) {
        out.push_back(id);
      }
    }
    return out;
  }
};

inline GSHSCENE_Key K(uint64_t domain, uint64_t value) {
  GSHSCENE_Key k;
  k.domain = domain;
  k.value = value;
  return k;
}

inline GSHSCENE_Node group_node(double opacity = 1.0, int32_t layer = 0) {
  GSHSCENE_Node n;
  gshscene_node_init(GSHSCENE_NODE_GROUP, &n);
  n.opacity = opacity;
  n.layer = layer;
  return n;
}

inline GSHSCENE_Node shape_node(const GSHSCENE_Resource * resource, int32_t layer = 0) {
  GSHSCENE_Node n;
  gshscene_node_init(GSHSCENE_NODE_SHAPE, &n);
  n.resource = resource;
  n.layer = layer;
  return n;
}

/** A stroke node (stroke.h, SK13): a centerline under a style, opaque black. */
inline GSHSCENE_Node stroke_node(const GSHSCENE_Resource * centerline, const GSHSCENE_Resource * style, int32_t layer = 0) {
  GSHSCENE_Node n;
  gshscene_node_init(GSHSCENE_NODE_STROKE, &n);
  n.resource = centerline;
  n.style = style;
  n.layer = layer;
  return n;
}

/** A placeholder node (list.h, PH1; version.h, PH7): a front end's source and code, approximated or not, with a box or none. */
inline GSHSCENE_Node placeholder_node(uint16_t source, uint16_t code, uint32_t approximated = 0,
    const double * box = nullptr, int32_t layer = 0) {
  GSHSCENE_Node n;
  gshscene_node_init(GSHSCENE_NODE_PLACEHOLDER, &n);
  n.placeholder.source = source;
  n.placeholder.code = code;
  n.placeholder.approximated = approximated;
  if (box) {
    n.placeholder.has_box = 1;
    for (int i = 0; i < 4; i++) {
      n.placeholder.box[i] = box[i];
    }
  }
  n.layer = layer;
  return n;
}

inline GSHSCENE_Node instance_node(uint64_t target, int32_t layer = 0) {
  GSHSCENE_Node n;
  gshscene_node_init(GSHSCENE_NODE_INSTANCE, &n);
  n.target = target;
  n.layer = layer;
  return n;
}

/** Add a node under @p parent, expecting success; returns its ID (0 on failure). */
inline uint64_t add(GSHSCENE_Edit * edit, uint64_t parent, const GSHSCENE_Node & node, const GSHSCENE_Key * key = nullptr,
    size_t position = GSHSCENE_APPEND) {
  uint64_t id = 0;
  EXPECT_EQ(gshscene_edit_add(edit, parent, position, &node, key, &id), GSHSCENE_OK);
  return id;
}

/** Publish an edit, expecting success; the version is held by @p out. */
inline void publish(SceneHolder & s, EditHolder & e, VersionRef * out = nullptr) {
  const GSHSCENE_Version * v = nullptr;
  ASSERT_EQ(gshscene_scene_publish(s.scene, e.edit, nullptr, &v), GSHSCENE_OK);
  if (out) {
    out->reset(v);
  } else {
    gshscene_version_release(v);
  }
}

inline GSHSCENE_Node_Info info_of(const GSHSCENE_Version * v, uint64_t id) {
  GSHSCENE_Node_Info info;
  std::memset(&info, 0, sizeof info);
  EXPECT_EQ(gshscene_version_node(v, id, &info), GSHSCENE_OK) << "node " << id;
  return info;
}

inline std::vector<uint64_t> children_of(const GSHSCENE_Version * v, uint64_t id) {
  GSHSCENE_Node_Info info = info_of(v, id);
  std::vector<uint64_t> out;
  for (size_t i = 0; i < info.child_count; i++) {
    uint64_t c = 0;
    EXPECT_EQ(gshscene_version_child(v, id, i, &c), GSHSCENE_OK);
    out.push_back(c);
  }
  return out;
}

inline std::string dump_version(const GSHSCENE_Version * v) {
  return dump_of(gshscene_version_dump, v);
}

/** One occurrence the visitor reported. */
struct Occurrence {
  uint64_t node;
  std::vector<uint64_t> path;
  GSHSCENE_Node_Kind kind;
  GSHGEOM_Map map;
  GSHSCENE_Node props;
};

inline int collect(void * ctx, const GSHSCENE_Visit * visit) {
  std::vector<Occurrence> * out = static_cast<std::vector<Occurrence> *>(ctx);
  Occurrence o;
  o.node = visit->node;
  o.path.assign(visit->path, visit->path + visit->path_count);
  o.kind = visit->kind;
  o.map = visit->map;
  o.props = *visit->props;
  out->push_back(o);
  return 0;
}

inline std::vector<Occurrence> occurrences(const GSHSCENE_Version * v) {
  std::vector<Occurrence> out;
  EXPECT_EQ(gshscene_version_visit(v, collect, &out), 0);
  return out;
}

}  // namespace scene

#endif /* GHOTI_IO_GSHSCENE_TEST_SCENE_FIXTURES_H */
