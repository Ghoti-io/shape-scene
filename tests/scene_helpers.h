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
 * Builders and holders shared by the resource, list and dump tests.
 */

#ifndef GHOTI_IO_GSHSCENE_TEST_SCENE_HELPERS_H
#define GHOTI_IO_GSHSCENE_TEST_SCENE_HELPERS_H

#include "test_helpers.h"

#include <ghoti.io/shape-geometry/shape-geometry.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace scene {

/** An index hash that sends every resource to one bucket: with it the store decides by equality alone. */
inline uint64_t all_zero(uint64_t, void *) {
  return 0;
}

inline GSHGEOM_Point P(double x, double y, double z = 0.0) {
  return GSHGEOM_Point{x, y, z};
}

/** A path made with the default limits; the test destroys it. */
inline GSHGEOM_Path * new_path(int dim = 2) {
  GSHGEOM_Path * path = nullptr;
  if (gshgeom_path_create(nullptr, nullptr, dim, &path) != GSHGEOM_OK) {
    return nullptr;
  }
  return path;
}

/** M (x0, 0) L (x0 + 4, 0) L (x0 + 4, 4) L (x0, 4) Z: the square of tools/gen-scene-vectors.py at x0 = 0. */
inline GSHGEOM_Path * square_path(double x0 = 0.0) {
  GSHGEOM_Path * p = new_path();
  gshgeom_path_move_to(p, P(x0, 0));
  gshgeom_path_line_to(p, P(x0 + 4, 0));
  gshgeom_path_line_to(p, P(x0 + 4, 4));
  gshgeom_path_line_to(p, P(x0, 4));
  gshgeom_path_close(p);
  return p;
}

/** The fixture of tools/gen-scene-vectors.py: every verb. */
inline GSHGEOM_Path * fixture_path() {
  GSHGEOM_Path * p = new_path();
  const double w3[3] = {1.0, 0.5, 2.0};
  const double w4[4] = {1.0, 2.0, 0.5, 4.0};
  gshgeom_path_move_to(p, P(1.5, -2.25));
  gshgeom_path_line_to(p, P(3.0, 4.0));
  gshgeom_path_quadratic_to(p, P(5.0, 6.5), P(7.0, 8.0));
  gshgeom_path_cubic_to(p, P(9.0, 10.0), P(11.25, 12.0), P(13.0, 14.0));
  gshgeom_path_rational_quadratic_to(p, P(15.0, 16.0), P(17.0, 18.0), w3);
  gshgeom_path_rational_cubic_to(p, P(19.0, 20.0), P(21.0, 22.0), P(23.0, 24.1), w4);
  gshgeom_path_close(p);
  return p;
}

/** A path of @p n line elements (a move and n - 1 lines). */
inline GSHGEOM_Path * long_path(size_t n) {
  GSHGEOM_Path * p = new_path();
  gshgeom_path_move_to(p, P(0, 0));
  for (size_t i = 1; i < n; i++) {
    gshgeom_path_line_to(p, P((double)i, (double)(i % 7)));
  }
  return p;
}

/** Owns a path. */
struct PathHolder {
  GSHGEOM_Path * path;
  explicit PathHolder(GSHGEOM_Path * p) : path(p) {}
  ~PathHolder() { gshgeom_path_destroy(path); }
  PathHolder(const PathHolder &) = delete;
  PathHolder & operator=(const PathHolder &) = delete;
};

/** Owns a reference to a resource. */
struct Ref {
  const GSHSCENE_Resource * resource = nullptr;
  Ref() = default;
  explicit Ref(const GSHSCENE_Resource * r) : resource(r) {}
  ~Ref() { gshscene_resource_release(resource); }
  Ref(const Ref &) = delete;
  Ref & operator=(const Ref &) = delete;
  Ref(Ref && other) noexcept : resource(other.resource) { other.resource = nullptr; }
  Ref & operator=(Ref && other) noexcept {
    if (this != &other) {
      gshscene_resource_release(resource);
      resource = other.resource;
      other.resource = nullptr;
    }
    return *this;
  }
  const GSHSCENE_Resource * get() const { return resource; }
};

/** Owns a store (the creator's reference). */
struct StoreHolder {
  GSHSCENE_Store * store = nullptr;
  explicit StoreHolder(const GSHSCENE_Limits * limits = nullptr, const GSHSCENE_Allocator * alloc = nullptr,
      GSHSCENE_Index_Hash_Fn index_hash = nullptr) {
    if (gshscene_store_create_with_index_hash(alloc, limits, index_hash, nullptr, &store) != GSHSCENE_OK) {
      store = nullptr;
    }
  }
  ~StoreHolder() { gshscene_store_destroy(store); }
  StoreHolder(const StoreHolder &) = delete;
  StoreHolder & operator=(const StoreHolder &) = delete;
};

/** Owns a list. */
struct ListHolder {
  GSHSCENE_List * list = nullptr;
  explicit ListHolder(GSHSCENE_Store * store, const GSHSCENE_Limits * limits = nullptr,
      const GSHSCENE_Allocator * alloc = nullptr) {
    if (gshscene_list_create(alloc, limits, store, &list) != GSHSCENE_OK) {
      list = nullptr;
    }
  }
  ~ListHolder() { gshscene_list_destroy(list); }
  ListHolder(const ListHolder &) = delete;
  ListHolder & operator=(const ListHolder &) = delete;
};

/** Intern a path and hold the reference. */
inline const GSHSCENE_Resource * intern(GSHSCENE_Store * store, const GSHGEOM_Path * path) {
  const GSHSCENE_Resource * r = nullptr;
  return gshscene_store_intern_path(store, path, &r) == GSHSCENE_OK ? r : nullptr;
}

/** Intern the square at x0. */
inline const GSHSCENE_Resource * intern_square(GSHSCENE_Store * store, double x0 = 0.0) {
  GSHGEOM_Path * p = square_path(x0);
  const GSHSCENE_Resource * r = intern(store, p);
  gshgeom_path_destroy(p);
  return r;
}

inline const GSHSCENE_Resource * intern_fixture(GSHSCENE_Store * store) {
  GSHGEOM_Path * p = fixture_path();
  const GSHSCENE_Resource * r = intern(store, p);
  gshgeom_path_destroy(p);
  return r;
}

/** The face of tools/gen-scene-vectors.py: the key, face index 2 and the coordinates 8192 and -4096, unbound. */
inline const GSHSCENE_Resource * intern_face(GSHSCENE_Store * store, uint64_t key = UINT64_C(0x1122334455667788),
    uint32_t index = 2, int16_t c0 = 8192, int16_t c1 = -4096) {
  int16_t coords[2] = {c0, c1};
  GSHSCENE_Face_Desc desc;
  std::memset(&desc, 0, sizeof desc);
  desc.font_key = key;
  desc.face_index = index;
  desc.coord_count = 2;
  desc.coords = coords;
  const GSHSCENE_Resource * r = nullptr;
  return gshscene_store_intern_face(store, &desc, &r) == GSHSCENE_OK ? r : nullptr;
}

/** A glyph run from the face @p face (retained by the caller), the given glyphs, size and mode. */
inline const GSHSCENE_Resource * intern_font_run(GSHSCENE_Store * store, const GSHSCENE_Resource * face, double size,
    const std::vector<GSHSCENE_Glyph> & glyphs, uint32_t hint_mode = 0, uint32_t hint_version = 0) {
  GSHSCENE_Glyph_Run run;
  std::memset(&run, 0, sizeof run);
  run.source = GSHSCENE_GLYPHS_FROM_FONT;
  run.face = face;
  run.size = size;
  run.hint_mode = hint_mode;
  run.hint_version = hint_version;
  run.glyph_count = glyphs.size();
  run.glyphs = glyphs.empty() ? nullptr : glyphs.data();
  const GSHSCENE_Resource * r = nullptr;
  return gshscene_store_intern_glyph_run(store, &run, &r) == GSHSCENE_OK ? r : nullptr;
}

inline GSHGEOM_Map translate(double tx, double ty) {
  GSHGEOM_Map m;
  gshgeom_map_translate(2, tx, ty, 0.0, &m);
  return m;
}

inline GSHSCENE_Paint solid(double r, double g, double b, double a) {
  GSHSCENE_Paint paint;
  std::memset(&paint, 0, sizeof paint);
  paint.kind = GSHSCENE_PAINT_SOLID;
  paint.rgba[0] = r;
  paint.rgba[1] = g;
  paint.rgba[2] = b;
  paint.rgba[3] = a;
  return paint;
}

/** A group with the given properties and the defaults of the rest (the enclosing blend space, flat, source-over). */
inline GSHSCENE_Group make_group(double opacity, uint32_t ordering = GSHSCENE_ORDER_FLAT,
    uint32_t composite = GSHSCENE_COMPOSITE_SRC_OVER) {
  GSHSCENE_Group g;
  std::memset(&g, 0, sizeof g);
  g.opacity = opacity;
  g.ordering = ordering;
  g.composite = composite;
  return g;
}

/** A color space of the given gamut and transfer. */
inline GSHSCENE_Color_Space space_of(GCOL_Primaries primaries, GCOL_Transfer transfer, double gamma = 0.0) {
  GSHSCENE_Color_Space s;
  std::memset(&s, 0, sizeof s);
  s.primaries = primaries;
  s.transfer = transfer;
  s.gamma = gamma;
  return s;
}

inline GSHSCENE_Color_Space srgb_space() {
  return space_of(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_SRGB);
}

inline GSHSCENE_Color_Space linear_srgb_space() {
  return space_of(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_LINEAR);
}

/** What a dump function writes, read back from a temporary file. */
template <typename Dump, typename Object>
std::string dump_of(Dump dump, const Object * object) {
  FILE * f = std::tmpfile();
  if (!f) {
    return "<no tmpfile>";
  }
  if ((int)dump(object, f) != 0) {  // GSHSCENE_OK and GSHGEOM_OK are both 0
    std::fclose(f);
    return "<dump failed>";
  }
  std::rewind(f);
  std::string text;
  char buffer[4096];
  size_t n;
  while ((n = std::fread(buffer, 1, sizeof buffer, f)) > 0) {
    text.append(buffer, n);
  }
  std::fclose(f);
  return text;
}

inline std::string dump_resource(const GSHSCENE_Resource * r) {
  return dump_of(gshscene_resource_dump, r);
}

inline std::string dump_list(const GSHSCENE_List * l) {
  return dump_of(gshscene_list_dump, l);
}

}  // namespace scene

#endif /* GHOTI_IO_GSHSCENE_TEST_SCENE_HELPERS_H */
