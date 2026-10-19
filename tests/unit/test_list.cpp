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
 * The list: recording, reading back, limits, the arena, reset and destroy,
 * equality and allocation failure (list.h, AD-27, CAP-16).
 */

#include "scene_helpers.h"

#include "../alloc_tracker.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <vector>

using namespace scene;

namespace {

const double kNaN = std::numeric_limits<double>::quiet_NaN();
const double kInf = std::numeric_limits<double>::infinity();

const GSHSCENE_Item * item_of(const GSHSCENE_List * list, size_t i) {
  const GSHSCENE_Item * item = nullptr;
  return gshscene_list_item(list, i, &item) == GSHSCENE_OK ? item : nullptr;
}

uint64_t identity_hash() {
  GSHGEOM_Map m;
  gshgeom_map_identity(2, &m);
  return gshgeom_map_hash(&m);
}

/** What a failing record call must not have changed. */
struct Snapshot {
  size_t count, expansion, bytes_not_smaller, refs;
  Snapshot(const GSHSCENE_List * list, const GSHSCENE_Resource * r)
      : count(gshscene_list_count(list)), expansion(gshscene_list_expansion(list)),
        bytes_not_smaller(gshscene_list_bytes(list)), refs(gshscene_resource_refcount(r)) {}
};

}  // namespace

// --- creating ----------------------------------------------------------------------

TEST(ListCreate, RejectsNullArguments) {
  StoreHolder s;
  GSHSCENE_List * list = nullptr;
  EXPECT_EQ(gshscene_list_create(nullptr, nullptr, nullptr, &list), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_create(nullptr, nullptr, s.store, nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(list, nullptr);
  gshscene_list_destroy(nullptr);
  EXPECT_EQ(gshscene_list_reset(nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_count(nullptr), 0u);
  EXPECT_EQ(gshscene_list_expansion(nullptr), 0u);
  EXPECT_EQ(gshscene_list_bytes(nullptr), 0u);
  EXPECT_EQ(gshscene_list_is_closed(nullptr), 0);
  EXPECT_EQ(gshscene_list_hash(nullptr), GSHGEOM_FNV_OFFSET_BASIS);
}

TEST(ListCreate, AnEmptyListIsOpenCloseSealsItAndRecordingAfterCloseIsRefused) {
  StoreHolder s;
  Ref sq(intern_square(s.store));
  ListHolder l(s.store);
  ASSERT_NE(l.list, nullptr);
  EXPECT_EQ(gshscene_list_count(l.list), 0u);
  EXPECT_EQ(gshscene_list_is_closed(l.list), 0);
  EXPECT_EQ(gshscene_list_bytes(l.list), 0u) << "nothing is allocated until the first item";
  GSHSCENE_List * inner = nullptr;
  ASSERT_EQ(gshscene_list_create(nullptr, nullptr, s.store, &inner), GSHSCENE_OK);
  gshscene_list_close(inner);
  Ref picture;
  ASSERT_EQ(gshscene_store_intern_picture(s.store, inner, &picture.resource), GSHSCENE_OK);
  gshscene_list_destroy(inner);
  ASSERT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_is_closed(l.list), 1);
  EXPECT_EQ(gshscene_list_instance(l.list, picture.get(), nullptr, 0, 0), GSHSCENE_ERR_INVALID) << "a valid instance, on a closed list";
  EXPECT_EQ(gshscene_list_close(l.list), GSHSCENE_ERR_INVALID) << "twice";
  EXPECT_EQ(gshscene_list_close(nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_clip_push(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, 0, 0), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_group_push(l.list, nullptr, 0, 0), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_instance(l.list, sq.get(), nullptr, 0, 0), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_clip_pop(l.list), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_group_pop(l.list), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_resource_refcount(sq.get()), 1u);
  EXPECT_EQ(gshscene_list_count(l.list), 0u);
}

TEST(ListCreate, TheListKeepsItsStoreAliveAfterTheCreatorLetsGo) {
  tracker::Tracker t;
  GSHSCENE_Allocator a = tracker::allocator_for(&t);
  GSHSCENE_Store * store = nullptr;
  ASSERT_EQ(gshscene_store_create(&a, nullptr, &store), GSHSCENE_OK);
  GSHSCENE_List * list = nullptr;
  ASSERT_EQ(gshscene_list_create(nullptr, nullptr, store, &list), GSHSCENE_OK);
  gshscene_store_destroy(store);
  EXPECT_FALSE(t.live.empty());
  gshscene_list_destroy(list);
  EXPECT_TRUE(t.live.empty());
}

// --- recording every kind -------------------------------------------------------------

namespace {

/** The list of tools/gen-scene-vectors.py, less the pictures: every kind but an instance, then an instance. */
struct Recorded {
  StoreHolder s;
  const GSHSCENE_Resource * sq, * p2, * picture;
  GSHSCENE_List * list = nullptr;
  Recorded() {
    sq = intern_square(s.store);
    p2 = intern_fixture(s.store);
    GSHSCENE_List * inner = nullptr;
    gshscene_list_create(nullptr, nullptr, s.store, &inner);
    gshscene_list_fill(inner, sq, nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0);
    gshscene_list_close(inner);
    gshscene_store_intern_picture(s.store, inner, &picture);
    gshscene_list_destroy(inner);
    gshscene_list_create(nullptr, nullptr, s.store, &list);
    GSHGEOM_Map design;
    gshgeom_map_design_to_model(2.5, 10, 20, &design);
    GSHGEOM_Map tr = translate(1.5, -2.25);
    GSHSCENE_Paint paint = solid(0.25, 0.5, 0.75, 1.0);
    GSHSCENE_Group group = make_group(0.5);
    gshscene_list_fill(list, sq, &design, GSHGEOM_FILL_NONZERO, &paint, -3, 0);
    gshscene_list_clip_push(list, p2, &tr, GSHGEOM_FILL_EVEN_ODD, 0, 0);
    gshscene_list_group_push(list, &group, 7, 0);
    gshscene_list_instance(list, picture, &tr, 1, 0);
    gshscene_list_group_pop(list);
    gshscene_list_clip_pop(list);
  }
  ~Recorded() {
    gshscene_list_destroy(list);
    gshscene_resource_release(picture);
    gshscene_resource_release(p2);
    gshscene_resource_release(sq);
  }
};

}  // namespace

TEST(ListRecord, EveryKindReadsBackInOrderWithCanonicalFields) {
  Recorded r;
  ASSERT_EQ(gshscene_list_count(r.list), 6u);
  uint64_t id = identity_hash();

  const GSHSCENE_Item * fill = item_of(r.list, 0);
  ASSERT_NE(fill, nullptr);
  EXPECT_EQ(fill->kind, GSHSCENE_ITEM_FILL);
  EXPECT_EQ(fill->resource, r.sq);
  GSHGEOM_Map design;
  gshgeom_map_design_to_model(2.5, 10, 20, &design);
  EXPECT_EQ(gshgeom_map_hash(&fill->map), gshgeom_map_hash(&design));
  EXPECT_EQ(fill->fill_rule, GSHGEOM_FILL_NONZERO);
  EXPECT_EQ(fill->paint.kind, GSHSCENE_PAINT_SOLID);
  EXPECT_EQ(fill->paint.rgba[0], 0.25);
  EXPECT_EQ(fill->paint.rgba[1], 0.5);
  EXPECT_EQ(fill->paint.rgba[2], 0.75);
  EXPECT_EQ(fill->paint.rgba[3], 1.0);
  EXPECT_TRUE(gshscene_color_space_is_target(&fill->paint.space));
  EXPECT_EQ(fill->paint.gradient, nullptr);
  EXPECT_EQ(fill->layer, -3);
  EXPECT_EQ(fill->group.opacity, 0.0) << "a group field that does not apply is zero";

  const GSHSCENE_Item * clip = item_of(r.list, 1);
  EXPECT_EQ(clip->kind, GSHSCENE_ITEM_CLIP_PUSH);
  EXPECT_EQ(clip->resource, r.p2);
  EXPECT_EQ(clip->fill_rule, GSHGEOM_FILL_EVEN_ODD);
  EXPECT_EQ(clip->layer, 0);
  EXPECT_EQ(clip->paint.rgba[3], 0.0) << "a paint that does not apply is transparent black";

  const GSHSCENE_Item * group = item_of(r.list, 2);
  EXPECT_EQ(group->kind, GSHSCENE_ITEM_GROUP_PUSH);
  EXPECT_EQ(group->resource, nullptr);
  EXPECT_EQ(group->group.opacity, 0.5);
  EXPECT_TRUE(gshscene_color_space_is_target(&group->group.blend_space));
  EXPECT_EQ(group->group.composite, (uint32_t)GSHSCENE_COMPOSITE_SRC_OVER);
  EXPECT_EQ(group->group.ordering, (uint32_t)GSHSCENE_ORDER_FLAT);
  EXPECT_EQ(group->layer, 7);
  EXPECT_EQ(gshgeom_map_hash(&group->map), id) << "the identity where no map applies";

  const GSHSCENE_Item * inst = item_of(r.list, 3);
  EXPECT_EQ(inst->kind, GSHSCENE_ITEM_INSTANCE);
  EXPECT_EQ(inst->resource, r.picture);
  EXPECT_EQ(inst->layer, 1);
  EXPECT_EQ(inst->fill_rule, GSHGEOM_FILL_NONZERO);

  const GSHSCENE_Item * gpop = item_of(r.list, 4);
  EXPECT_EQ(gpop->kind, GSHSCENE_ITEM_GROUP_POP);
  EXPECT_EQ(gpop->resource, nullptr);
  const GSHSCENE_Item * cpop = item_of(r.list, 5);
  EXPECT_EQ(cpop->kind, GSHSCENE_ITEM_CLIP_POP);
  EXPECT_EQ(cpop->resource, nullptr);
  EXPECT_EQ(gshgeom_map_hash(&cpop->map), id);

  EXPECT_EQ(item_of(r.list, 6), nullptr);
  const GSHSCENE_Item * out = nullptr;
  EXPECT_EQ(gshscene_list_item(r.list, 6, &out), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_item(nullptr, 0, &out), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_item(r.list, 0, nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_close(r.list), GSHSCENE_OK);
}

TEST(ListRecord, ANullMapIsTheTwoDimensionalIdentityAndANullPaintIsOpaqueBlack) {
  StoreHolder s;
  Ref sq(intern_square(s.store));
  ListHolder l(s.store);
  ASSERT_EQ(gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0), GSHSCENE_OK);
  const GSHSCENE_Item * item = item_of(l.list, 0);
  EXPECT_EQ(item->map.dim, 2);
  EXPECT_EQ(item->map.kind, 0u);
  EXPECT_EQ(gshgeom_map_hash(&item->map), identity_hash());
  EXPECT_EQ(item->paint.kind, GSHSCENE_PAINT_SOLID);
  EXPECT_EQ(item->paint.rgba[0], 0.0);
  EXPECT_EQ(item->paint.rgba[3], 1.0);
}

TEST(ListRecord, ATwoDimensionalMapKeepsOnlyTheEntriesItUses) {
  StoreHolder s;
  Ref sq(intern_square(s.store));
  ListHolder l(s.store);
  GSHGEOM_Map clean = translate(3, 4);
  GSHGEOM_Map dirty = clean;
  dirty.m[2][2] = 7.0;  // ignored on input in 2D
  dirty.m[0][2] = 5.0;
  dirty.t[2] = 9.0;
  ASSERT_EQ(gshscene_list_fill(l.list, sq.get(), &dirty, GSHGEOM_FILL_NONZERO, nullptr, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(l.list, sq.get(), &clean, GSHGEOM_FILL_NONZERO, nullptr, 0, 0), GSHSCENE_OK);
  const GSHSCENE_Item * a = item_of(l.list, 0);
  EXPECT_EQ(a->map.m[2][2], 1.0);
  EXPECT_EQ(a->map.m[0][2], 0.0);
  EXPECT_EQ(a->map.t[2], 0.0);
  EXPECT_EQ(gshgeom_map_hash(&a->map), gshgeom_map_hash(&clean));
  // And the two lists' items are equal, as the library's equality sees them.
  ASSERT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_hash(l.list), gshscene_list_hash(l.list));
}

TEST(ListRecord, AThreeDimensionalMapIsAcceptedAndKept) {
  StoreHolder s;
  Ref sq(intern_square(s.store));
  ListHolder l(s.store);
  GSHGEOM_Map m, scale, composed;
  ASSERT_EQ(gshgeom_map_translate(3, 1, 2, 3, &m), GSHGEOM_OK);
  ASSERT_EQ(gshgeom_map_scale(3, 2, 3, 5, &scale), GSHGEOM_OK);
  ASSERT_EQ(gshgeom_map_compose(&m, &scale, &composed), GSHGEOM_OK);
  composed.m[0][2] = 0.5;  // an entry in every cell of the matrix
  composed.m[2][0] = 0.25;
  composed.m[1][2] = 0.125;
  composed.m[2][1] = 0.0625;
  ASSERT_EQ(gshgeom_map_check(&composed), GSHGEOM_OK);
  ASSERT_EQ(gshscene_list_fill(l.list, sq.get(), &composed, GSHGEOM_FILL_NONZERO, nullptr, 0, 0), GSHSCENE_OK);
  const GSHSCENE_Item * item = item_of(l.list, 0);
  EXPECT_EQ(item->map.dim, 3);
  for (int i = 0; i < 3; i++) {
    EXPECT_EQ(item->map.t[i], composed.t[i]) << i;
    for (int j = 0; j < 3; j++) {
      EXPECT_EQ(item->map.m[i][j], composed.m[i][j]) << i << "," << j;
    }
  }
}

TEST(ListRecord, TheListHoldsOneReferenceForEachItemThatNamesAResource) {
  StoreHolder s;
  Ref sq(intern_square(s.store));
  GSHSCENE_List * list = nullptr;
  ASSERT_EQ(gshscene_list_create(nullptr, nullptr, s.store, &list), GSHSCENE_OK);
  EXPECT_EQ(gshscene_resource_refcount(sq.get()), 1u);
  gshscene_list_fill(list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0);
  gshscene_list_fill(list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 1, 0);
  EXPECT_EQ(gshscene_resource_refcount(sq.get()), 3u);
  gshscene_list_clip_push(list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, 0, 0);
  EXPECT_EQ(gshscene_resource_refcount(sq.get()), 4u);
  gshscene_list_group_push(list, nullptr, 0, 0);
  gshscene_list_group_pop(list);
  gshscene_list_clip_pop(list);
  EXPECT_EQ(gshscene_resource_refcount(sq.get()), 4u) << "pops and groups name no resource";
  gshscene_list_destroy(list);
  EXPECT_EQ(gshscene_resource_refcount(sq.get()), 1u);
}

TEST(ListRecord, ADroppedHandleStaysAliveWhileTheListNamesIt) {
  StoreHolder s;
  const GSHSCENE_Resource * sq = intern_square(s.store);
  ListHolder l(s.store);
  ASSERT_EQ(gshscene_list_fill(l.list, sq, nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0), GSHSCENE_OK);
  gshscene_resource_release(sq);  // the caller lets go; the list still names it
  GSHSCENE_Store_Stats stats;
  ASSERT_EQ(gshscene_store_stats(s.store, &stats), GSHSCENE_OK);
  EXPECT_EQ(stats.live, 1u);
  EXPECT_EQ(gshgeom_path_element_count(gshscene_resource_path(item_of(l.list, 0)->resource)), 5u);
}

// --- refusals ------------------------------------------------------------------------------

TEST(ListRecord, ANullGroupIsOpaqueFlatAndInTheTargetsBlendSpace) {
  StoreHolder s;
  ListHolder l(s.store);
  ASSERT_EQ(gshscene_list_group_push(l.list, nullptr, 0, 0), GSHSCENE_OK);
  const GSHSCENE_Item * item = item_of(l.list, 0);
  EXPECT_EQ(item->group.opacity, 1.0);
  EXPECT_TRUE(gshscene_color_space_is_target(&item->group.blend_space));
  EXPECT_EQ(item->group.composite, (uint32_t)GSHSCENE_COMPOSITE_SRC_OVER);
  EXPECT_EQ(item->group.ordering, (uint32_t)GSHSCENE_ORDER_FLAT);
}

TEST(ListRecord, EveryInvalidArgumentIsRefusedAndTheListIsAsItWas) {
  StoreHolder s, other;
  Ref sq(intern_square(s.store));
  Ref foreign(intern_square(other.store));
  GSHSCENE_Glyph_Run run;
  std::memset(&run, 0, sizeof run);
  run.source = GSHSCENE_GLYPHS_FROM_FONT;
  run.font_key = 1;
  run.size = 10.0;
  const GSHSCENE_Resource * runres = nullptr;
  ASSERT_EQ(gshscene_store_intern_glyph_run(s.store, &run, &runres), GSHSCENE_OK);
  Ref glyphs(runres);
  GSHSCENE_List * inner = nullptr;
  ASSERT_EQ(gshscene_list_create(nullptr, nullptr, s.store, &inner), GSHSCENE_OK);
  gshscene_list_close(inner);
  const GSHSCENE_Resource * picres = nullptr;
  ASSERT_EQ(gshscene_store_intern_picture(s.store, inner, &picres), GSHSCENE_OK);
  Ref picture(picres);
  gshscene_list_destroy(inner);

  ListHolder l(s.store);
  const GSHGEOM_Map good = translate(1, 1);
  GSHGEOM_Map nan_map = good;
  nan_map.t[0] = kNaN;
  GSHGEOM_Map big_map = good;
  big_map.m[0][0] = std::ldexp(1.0, 101);
  GSHGEOM_Map bad_dim = good;
  bad_dim.dim = 5;
  const GSHSCENE_Paint ok_paint = solid(0.1, 0.2, 0.3, 0.4);
  const GSHGEOM_Fill_Rule bad_rule = (GSHGEOM_Fill_Rule)2;

  auto fill = [&](const GSHSCENE_Resource * r, const GSHGEOM_Map * m, GSHGEOM_Fill_Rule rule, const GSHSCENE_Paint * p) {
    Snapshot before(l.list, sq.get());
    GSHSCENE_Result rc = gshscene_list_fill(l.list, r, m, rule, p, 0, 0);
    Snapshot after(l.list, sq.get());
    EXPECT_EQ(before.count, after.count);
    EXPECT_EQ(before.expansion, after.expansion);
    EXPECT_EQ(before.refs, after.refs);
    return rc;
  };
  const GSHSCENE_Result INV = GSHSCENE_ERR_INVALID, UNS = GSHSCENE_ERR_UNSUPPORTED;
  EXPECT_EQ(fill(nullptr, &good, GSHGEOM_FILL_NONZERO, &ok_paint), INV) << "no resource";
  EXPECT_EQ(fill(foreign.get(), &good, GSHGEOM_FILL_NONZERO, &ok_paint), INV) << "a resource of another store";
  EXPECT_EQ(fill(picture.get(), &good, GSHGEOM_FILL_NONZERO, &ok_paint), INV) << "a picture is drawn by an instance";
  EXPECT_EQ(fill(sq.get(), &nan_map, GSHGEOM_FILL_NONZERO, &ok_paint), INV) << "NaN in the map";
  EXPECT_EQ(fill(sq.get(), &big_map, GSHGEOM_FILL_NONZERO, &ok_paint), INV) << "an entry past the map limit";
  EXPECT_EQ(fill(sq.get(), &bad_dim, GSHGEOM_FILL_NONZERO, &ok_paint), INV) << "dimension 5";
  EXPECT_EQ(fill(sq.get(), &good, bad_rule, &ok_paint), INV) << "a rule outside the enum";
  EXPECT_EQ(fill(sq.get(), &good, (GSHGEOM_Fill_Rule)-1, &ok_paint), INV);
  for (int c = 0; c < 4; c++) {
    GSHSCENE_Paint p = ok_paint;
    p.rgba[c] = kNaN;
    EXPECT_EQ(fill(sq.get(), &good, GSHGEOM_FILL_NONZERO, &p), INV) << "NaN in component " << c;
    p.rgba[c] = kInf;
    EXPECT_EQ(fill(sq.get(), &good, GSHGEOM_FILL_NONZERO, &p), INV) << "infinity in component " << c;
  }
  {
    GSHSCENE_Paint p = ok_paint;
    p.rgba[3] = 1.5;
    EXPECT_EQ(fill(sq.get(), &good, GSHGEOM_FILL_NONZERO, &p), INV) << "alpha above 1";
    p.rgba[3] = -0.25;
    EXPECT_EQ(fill(sq.get(), &good, GSHGEOM_FILL_NONZERO, &p), INV) << "alpha below 0";
    p = ok_paint;
    p.kind = GSHSCENE_PAINT_GRADIENT;
    EXPECT_EQ(fill(sq.get(), &good, GSHGEOM_FILL_NONZERO, &p), UNS) << "a gradient is reserved";
    p.kind = GSHSCENE_PAINT_FOREGROUND;
    EXPECT_EQ(fill(sq.get(), &good, GSHGEOM_FILL_NONZERO, &p), INV) << "a foreground paint's color is the version's: only its alpha is its own";
    p.kind = GSHSCENE_PAINT_PALETTE;
    EXPECT_EQ(fill(sq.get(), &good, GSHGEOM_FILL_NONZERO, &p), UNS) << "a palette entry is reserved";
    p.kind = GSHSCENE_PAINT_KIND_COUNT;
    EXPECT_EQ(fill(sq.get(), &good, GSHGEOM_FILL_NONZERO, &p), INV) << "the end of the enum";
    p.kind = (GSHSCENE_Paint_Kind)9;
    EXPECT_EQ(fill(sq.get(), &good, GSHGEOM_FILL_NONZERO, &p), INV) << "a paint kind outside the enum";
    p = ok_paint;
    p.space.primaries = GCOL_PRIMARIES_SRGB;
    EXPECT_EQ(fill(sq.get(), &good, GSHGEOM_FILL_NONZERO, &p), INV) << "a space given in part";
    p.space = space_of(GCOL_PRIMARIES_PROPHOTO, GCOL_TRANSFER_SRGB);
    EXPECT_EQ(fill(sq.get(), &good, GSHGEOM_FILL_NONZERO, &p), UNS) << "ProPhoto is D50: reserved";
    p.space = space_of(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_PQ);
    EXPECT_EQ(fill(sq.get(), &good, GSHGEOM_FILL_NONZERO, &p), UNS) << "PQ is reserved";
    p = ok_paint;
    p.gradient = sq.get();
    EXPECT_EQ(fill(sq.get(), &good, GSHGEOM_FILL_NONZERO, &p), INV) << "a solid paint names no gradient";
  }
  // The same rules for a clip push and an instance.
  EXPECT_EQ(gshscene_list_clip_push(l.list, nullptr, &good, GSHGEOM_FILL_NONZERO, 0, 0), INV);
  EXPECT_EQ(gshscene_list_clip_push(l.list, foreign.get(), &good, GSHGEOM_FILL_NONZERO, 0, 0), INV);
  EXPECT_EQ(gshscene_list_clip_push(l.list, picture.get(), &good, GSHGEOM_FILL_NONZERO, 0, 0), INV);
  EXPECT_EQ(gshscene_list_clip_push(l.list, sq.get(), &nan_map, GSHGEOM_FILL_NONZERO, 0, 0), INV);
  EXPECT_EQ(gshscene_list_clip_push(l.list, sq.get(), &good, bad_rule, 0, 0), INV);
  EXPECT_EQ(gshscene_list_instance(l.list, nullptr, &good, 0, 0), INV);
  EXPECT_EQ(gshscene_list_instance(l.list, sq.get(), &good, 0, 0), INV) << "an instance draws a picture";
  EXPECT_EQ(gshscene_list_instance(l.list, glyphs.get(), &good, 0, 0), INV);
  EXPECT_EQ(gshscene_list_instance(l.list, picture.get(), &nan_map, 0, 0), INV);
  EXPECT_EQ(gshscene_list_fill(nullptr, sq.get(), &good, GSHGEOM_FILL_NONZERO, nullptr, 0, 0), INV);
  EXPECT_EQ(gshscene_list_clip_push(nullptr, sq.get(), &good, GSHGEOM_FILL_NONZERO, 0, 0), INV);
  EXPECT_EQ(gshscene_list_instance(nullptr, picture.get(), &good, 0, 0), INV);
  EXPECT_EQ(gshscene_list_group_push(nullptr, nullptr, 0, 0), INV);
  EXPECT_EQ(gshscene_list_clip_pop(nullptr), INV);
  EXPECT_EQ(gshscene_list_group_pop(nullptr), INV);
  // Groups.
  for (double bad : {kNaN, kInf, -kInf, -0.001, 1.001, 2.0}) {
    GSHSCENE_Group g = make_group(bad);
    EXPECT_EQ(gshscene_list_group_push(l.list, &g, 0, 0), INV) << "opacity " << bad;
  }
  {
    GSHSCENE_Group g = make_group(0.5);
    g.blend_space = space_of(GCOL_PRIMARIES_PROPHOTO, GCOL_TRANSFER_SRGB);
    EXPECT_EQ(gshscene_list_group_push(l.list, &g, 0, 0), UNS) << "ProPhoto is reserved";
    g.blend_space = space_of(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_GAMMA, 0.0);
    EXPECT_EQ(gshscene_list_group_push(l.list, &g, 0, 0), INV) << "a gamma transfer without its exponent";
    g = make_group(0.5, 3);
    EXPECT_EQ(gshscene_list_group_push(l.list, &g, 0, 0), INV) << "an ordering outside the enum";
    g = make_group(0.5, GSHSCENE_ORDER_FLAT, GSHSCENE_COMPOSITE_COUNT);
    EXPECT_EQ(gshscene_list_group_push(l.list, &g, 0, 0), INV) << "a composite operator outside the enum";
    g = make_group(0.5);
    EXPECT_EQ(gshscene_list_group_push(l.list, &g, 0, kNaN), INV) << "a depth that is not finite";
    EXPECT_EQ(gshscene_list_group_push(l.list, &g, 0, kInf), INV);
  }
  EXPECT_EQ(gshscene_list_count(l.list), 0u) << "nothing recorded by a refused call";
  EXPECT_EQ(gshscene_resource_refcount(sq.get()), 1u);
  EXPECT_EQ(gshscene_list_close(l.list), GSHSCENE_OK) << "no push is left open by a refused push";
}

TEST(ListRecord, TheBoundariesOfEveryRangeAreAccepted) {
  StoreHolder s;
  Ref sq(intern_square(s.store));
  ListHolder l(s.store);
  for (double alpha : {0.0, 1.0, 0.5}) {
    GSHSCENE_Paint p = solid(0.0, 0.0, 0.0, alpha);
    EXPECT_EQ(gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 0), GSHSCENE_OK) << alpha;
  }
  GSHSCENE_Paint wide = solid(2.0, -0.5, 1e300, 1.0);  // outside the gamut is allowed
  EXPECT_EQ(gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_EVEN_ODD, &wide, 0, 0), GSHSCENE_OK);
  for (double opacity : {0.0, 1.0}) {
    GSHSCENE_Group g = make_group(opacity);
    EXPECT_EQ(gshscene_list_group_push(l.list, &g, 0, 0), GSHSCENE_OK) << opacity;
    EXPECT_EQ(gshscene_list_group_pop(l.list), GSHSCENE_OK);
  }
  for (int32_t layer : {INT32_MIN, -1, 0, 1, INT32_MAX}) {
    EXPECT_EQ(gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, layer, 0), GSHSCENE_OK);
    EXPECT_EQ(item_of(l.list, gshscene_list_count(l.list) - 1)->layer, layer);
  }
  EXPECT_EQ(item_of(l.list, 3)->paint.rgba[2], 1e300);
}

TEST(ListRecord, AGlyphRunCanBeFilledAndUsedAsAClip) {
  StoreHolder s;
  GSHSCENE_Glyph_Run run;
  std::memset(&run, 0, sizeof run);
  run.source = GSHSCENE_GLYPHS_FROM_FONT;
  run.font_key = 7;
  run.size = 16.0;
  const GSHSCENE_Resource * r = nullptr;
  ASSERT_EQ(gshscene_store_intern_glyph_run(s.store, &run, &r), GSHSCENE_OK);
  Ref ref(r);
  ListHolder l(s.store);
  EXPECT_EQ(gshscene_list_fill(l.list, r, nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_clip_push(l.list, r, nullptr, GSHGEOM_FILL_NONZERO, 0, 0), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_clip_pop(l.list), GSHSCENE_OK);
}

// --- nesting -------------------------------------------------------------------------------

TEST(ListNesting, APopMustMatchTheInnermostPushAndCloseNeedsAllOfThem) {
  StoreHolder s;
  Ref sq(intern_square(s.store));
  ListHolder l(s.store);
  EXPECT_EQ(gshscene_list_clip_pop(l.list), GSHSCENE_ERR_INVALID) << "nothing is open";
  EXPECT_EQ(gshscene_list_group_pop(l.list), GSHSCENE_ERR_INVALID);
  ASSERT_EQ(gshscene_list_clip_push(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_push(l.list, nullptr, 0, 0), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_clip_pop(l.list), GSHSCENE_ERR_INVALID) << "the group is the innermost";
  EXPECT_EQ(gshscene_list_close(l.list), GSHSCENE_ERR_INVALID) << "two pushes are open";
  EXPECT_EQ(gshscene_list_is_closed(l.list), 0) << "a failed close leaves the list open";
  EXPECT_EQ(gshscene_list_count(l.list), 2u);
  ASSERT_EQ(gshscene_list_group_pop(l.list), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_group_pop(l.list), GSHSCENE_ERR_INVALID) << "the clip is the innermost";
  ASSERT_EQ(gshscene_list_clip_pop(l.list), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
}

TEST(ListNesting, DepthGrowsPastTheInitialStackAndEveryPopStillMatches) {
  StoreHolder s;
  Ref sq(intern_square(s.store));
  ListHolder l(s.store);
  std::vector<int> kinds;
  for (int i = 0; i < 200; i++) {
    bool clip = (i % 3) != 0;
    kinds.push_back(clip);
    if (clip) {
      ASSERT_EQ(gshscene_list_clip_push(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, 0, 0), GSHSCENE_OK) << i;
    } else {
      ASSERT_EQ(gshscene_list_group_push(l.list, nullptr, 0, 0), GSHSCENE_OK) << i;
    }
  }
  for (int i = 0; i < 200; i++) {
    const GSHSCENE_Item * item = item_of(l.list, (size_t)i);
    ASSERT_NE(item, nullptr);
    EXPECT_EQ(item->kind, kinds[(size_t)i] ? GSHSCENE_ITEM_CLIP_PUSH : GSHSCENE_ITEM_GROUP_PUSH) << i;
    EXPECT_EQ(item->resource, kinds[(size_t)i] ? sq.get() : nullptr) << i;
  }
  for (int i = 199; i >= 0; i--) {
    bool clip = kinds[(size_t)i];
    // The wrong pop is refused, the right one accepted: the stack kept its order through every growth.
    EXPECT_EQ(clip ? gshscene_list_group_pop(l.list) : gshscene_list_clip_pop(l.list), GSHSCENE_ERR_INVALID) << i;
    ASSERT_EQ(clip ? gshscene_list_clip_pop(l.list) : gshscene_list_group_pop(l.list), GSHSCENE_OK) << i;
  }
  EXPECT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
}

TEST(ListNesting, MaxDepthAcceptsExactlyTheLimitOfOpenPushes) {
  GSHSCENE_Limits limits;
  gshscene_limits_default(&limits);
  limits.max_depth = 20;
  StoreHolder s;
  Ref sq(intern_square(s.store));
  ListHolder l(s.store, &limits);
  for (int i = 0; i < 20; i++) {
    GSHSCENE_Result rc = (i % 2) ? gshscene_list_clip_push(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, 0, 0)
                                 : gshscene_list_group_push(l.list, nullptr, 0, 0);
    ASSERT_EQ(rc, GSHSCENE_OK) << i;
  }
  size_t before = gshscene_list_count(l.list);
  EXPECT_EQ(gshscene_list_group_push(l.list, nullptr, 0, 0), GSHSCENE_ERR_LIMIT);
  EXPECT_EQ(gshscene_list_clip_push(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, 0, 0), GSHSCENE_ERR_LIMIT);
  EXPECT_EQ(gshscene_list_count(l.list), before);
  EXPECT_EQ(gshscene_resource_refcount(sq.get()), 11u) << "ten clips are held, the refused one is not";
  // A pop makes room for exactly one more.
  ASSERT_EQ(gshscene_list_clip_pop(l.list), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_clip_push(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, 0, 0), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_clip_push(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, 0, 0), GSHSCENE_ERR_LIMIT);
}

// --- limits --------------------------------------------------------------------------------

TEST(ListLimits, MaxListItemsAcceptsExactlyTheLimit) {
  GSHSCENE_Limits limits;
  gshscene_limits_default(&limits);
  limits.max_list_items = 100;
  StoreHolder s;
  Ref sq(intern_square(s.store));
  ListHolder l(s.store, &limits);
  for (int i = 0; i < 100; i++) {
    ASSERT_EQ(gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, i, 0), GSHSCENE_OK) << i;
  }
  Snapshot before(l.list, sq.get());
  EXPECT_EQ(gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 100, 0), GSHSCENE_ERR_LIMIT);
  EXPECT_EQ(gshscene_list_group_push(l.list, nullptr, 0, 0), GSHSCENE_ERR_LIMIT);
  EXPECT_EQ(gshscene_list_count(l.list), 100u);
  EXPECT_EQ(gshscene_resource_refcount(sq.get()), before.refs);
  EXPECT_EQ(gshscene_list_close(l.list), GSHSCENE_OK) << "a refused push is not left open";
}

TEST(ListLimits, ALimitAboveTheCeilingIsAcceptedAndTheListStillWorks) {
  GSHSCENE_Limits limits;
  gshscene_limits_default(&limits);
  limits.max_list_items = SIZE_MAX;
  limits.max_depth = SIZE_MAX;
  limits.max_expansion = SIZE_MAX;
  limits.max_list_bytes = SIZE_MAX;
  StoreHolder s;
  Ref sq(intern_square(s.store));
  ListHolder l(s.store, &limits);
  ASSERT_NE(l.list, nullptr);
  EXPECT_EQ(gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0), GSHSCENE_OK);
}

TEST(ListLimits, MaxListBytesAcceptsExactlyTheBytesTheRecordingTook) {
  StoreHolder s;
  Ref sq(intern_square(s.store));
  auto record = [&](GSHSCENE_List * list) -> int {
    for (int i = 0; i < 70; i++) {
      GSHSCENE_Result rc = gshscene_list_fill(list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, i, 0);
      if (rc != GSHSCENE_OK) {
        EXPECT_EQ(rc, GSHSCENE_ERR_LIMIT);
        return i;
      }
    }
    return 70;
  };
  size_t needed = 0;
  {
    ListHolder l(s.store);
    ASSERT_EQ(record(l.list), 70);
    needed = gshscene_list_bytes(l.list);
    EXPECT_GT(needed, 0u);
  }
  {
    GSHSCENE_Limits limits;
    gshscene_limits_default(&limits);
    limits.max_list_bytes = needed;
    ListHolder l(s.store, &limits);
    EXPECT_EQ(record(l.list), 70) << "exactly the bytes it needs";
    EXPECT_EQ(gshscene_list_bytes(l.list), needed);
  }
  {
    GSHSCENE_Limits limits;
    gshscene_limits_default(&limits);
    limits.max_list_bytes = needed - 1;
    ListHolder l(s.store, &limits);
    int got = record(l.list);
    EXPECT_LT(got, 70) << "one byte fewer is refused";
    EXPECT_EQ(gshscene_list_count(l.list), (size_t)got);
    EXPECT_LE(gshscene_list_bytes(l.list), needed - 1);
    EXPECT_EQ(gshscene_resource_refcount(sq.get()), (size_t)got + 1u);
  }
  {
    GSHSCENE_Limits limits;
    gshscene_limits_default(&limits);
    limits.max_list_bytes = 100;  // less than the first chunk
    ListHolder l(s.store, &limits);
    EXPECT_EQ(record(l.list), 0);
  }
}

TEST(ListLimits, ExpansionCountsAnInstanceAsOnePlusItsPicturesAndTheLimitIsExact) {
  StoreHolder s;
  Ref sq(intern_square(s.store));
  GSHSCENE_Limits limits;
  gshscene_limits_default(&limits);
  limits.max_expansion = 1000;
  // Level 1: one fill.  Level k + 1: ten instances of level k, so its expansion is 10 * (1 + E_k).
  GSHSCENE_List * l1 = nullptr;
  ASSERT_EQ(gshscene_list_create(nullptr, &limits, s.store, &l1), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(l1, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_expansion(l1), 1u);
  gshscene_list_close(l1);
  const GSHSCENE_Resource * level = nullptr;
  ASSERT_EQ(gshscene_store_intern_picture(s.store, l1, &level), GSHSCENE_OK);
  gshscene_list_destroy(l1);
  const size_t want[] = {1, 20, 210};
  for (int depth = 2; depth <= 3; depth++) {
    GSHSCENE_List * list = nullptr;
    ASSERT_EQ(gshscene_list_create(nullptr, &limits, s.store, &list), GSHSCENE_OK);
    for (int i = 0; i < 10; i++) {
      ASSERT_EQ(gshscene_list_instance(list, level, nullptr, i, 0), GSHSCENE_OK) << depth << "," << i;
    }
    EXPECT_EQ(gshscene_list_expansion(list), want[depth - 1]) << depth;
    EXPECT_EQ(gshscene_list_count(list), 10u);
    gshscene_list_close(list);
    const GSHSCENE_Resource * next = nullptr;
    ASSERT_EQ(gshscene_store_intern_picture(s.store, list, &next), GSHSCENE_OK);
    gshscene_list_destroy(list);
    gshscene_resource_release(level);
    level = next;
  }
  // Level 4 would be 10 * 211 = 2110: the fifth instance passes 1000 (4 * 211 = 844 fits, 5 * 211 = 1055 does not).
  GSHSCENE_List * bomb = nullptr;
  ASSERT_EQ(gshscene_list_create(nullptr, &limits, s.store, &bomb), GSHSCENE_OK);
  for (int i = 0; i < 4; i++) {
    ASSERT_EQ(gshscene_list_instance(bomb, level, nullptr, 0, 0), GSHSCENE_OK) << i;
  }
  EXPECT_EQ(gshscene_list_expansion(bomb), 844u);
  size_t refs = gshscene_resource_refcount(level);
  EXPECT_EQ(gshscene_list_instance(bomb, level, nullptr, 0, 0), GSHSCENE_ERR_LIMIT);
  EXPECT_EQ(gshscene_list_count(bomb), 4u);
  EXPECT_EQ(gshscene_list_expansion(bomb), 844u);
  EXPECT_EQ(gshscene_resource_refcount(level), refs);
  // A plain item still fits (845), up to exactly 1000.
  for (int i = 845; i <= 1000; i++) {
    ASSERT_EQ(gshscene_list_fill(bomb, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0), GSHSCENE_OK) << i;
  }
  EXPECT_EQ(gshscene_list_expansion(bomb), 1000u);
  EXPECT_EQ(gshscene_list_fill(bomb, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0), GSHSCENE_ERR_LIMIT);
  gshscene_list_destroy(bomb);
  gshscene_resource_release(level);
}

TEST(ListLimits, TheExpansionSaturatesInsteadOfWrapping) {
  GSHSCENE_Limits limits;
  gshscene_limits_default(&limits);
  limits.max_expansion = SIZE_MAX;
  StoreHolder s(&limits);
  Ref sq(intern_square(s.store));
  GSHSCENE_List * base = nullptr;
  ASSERT_EQ(gshscene_list_create(nullptr, &limits, s.store, &base), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(base, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0), GSHSCENE_OK);
  gshscene_list_close(base);
  const GSHSCENE_Resource * level = nullptr;
  ASSERT_EQ(gshscene_store_intern_picture(s.store, base, &level), GSHSCENE_OK);
  gshscene_list_destroy(base);
  size_t last = 1;
  bool saturated = false;
  // Ten instances of the level below at each level: the expansion is about 10^depth.
  for (int depth = 2; depth <= 30 && !saturated; depth++) {
    GSHSCENE_List * list = nullptr;
    ASSERT_EQ(gshscene_list_create(nullptr, &limits, s.store, &list), GSHSCENE_OK);
    for (int i = 0; i < 10; i++) {
      ASSERT_EQ(gshscene_list_instance(list, level, nullptr, 0, 0), GSHSCENE_OK) << depth;
    }
    size_t expansion = gshscene_list_expansion(list);
    EXPECT_GE(expansion, last) << "a wrapped sum would fall at depth " << depth;
    last = expansion;
    saturated = expansion == SIZE_MAX;
    gshscene_list_close(list);
    const GSHSCENE_Resource * next = nullptr;
    ASSERT_EQ(gshscene_store_intern_picture(s.store, list, &next), GSHSCENE_OK) << depth;
    gshscene_list_destroy(list);
    gshscene_resource_release(level);
    level = next;
  }
  EXPECT_TRUE(saturated) << "thirty levels of ten overflow any size_t";
  // At the largest limit a saturated count is not past it: one more instance is still accepted.
  GSHSCENE_List * more = nullptr;
  ASSERT_EQ(gshscene_list_create(nullptr, &limits, s.store, &more), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_instance(more, level, nullptr, 0, 0), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_expansion(more), SIZE_MAX);
  gshscene_list_destroy(more);
  gshscene_resource_release(level);
}

// --- the arena ---------------------------------------------------------------------------------

TEST(ListArena, AHundredThousandItemsReadBackAcrossEverySegmentBoundary) {
  StoreHolder s;
  Ref sq(intern_square(s.store));
  ListHolder l(s.store);
  const int n = 100000;
  const GSHSCENE_Item * first = nullptr;
  for (int i = 0; i < n; i++) {
    ASSERT_EQ(gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, i, 0), GSHSCENE_OK) << i;
    if (i == 0) {
      first = item_of(l.list, 0);
    }
  }
  EXPECT_EQ(first, item_of(l.list, 0)) << "an item never moves when the list grows";
  ASSERT_EQ(gshscene_list_count(l.list), (size_t)n);
  for (int i = 0; i < n; i++) {
    const GSHSCENE_Item * item = item_of(l.list, (size_t)i);
    ASSERT_NE(item, nullptr) << i;
    ASSERT_EQ(item->layer, i) << "index " << i;
  }
  EXPECT_EQ(gshscene_list_expansion(l.list), (size_t)n);
  EXPECT_EQ(gshscene_resource_refcount(sq.get()), (size_t)n + 1u);
}

TEST(ListArena, ADestroyFreesEveryChunkInOnePassAndReleasesEveryReference) {
  tracker::Tracker t;
  GSHSCENE_Allocator a = tracker::allocator_for(&t);
  StoreHolder s;
  Ref sq(intern_square(s.store));
  {
    ListHolder l(s.store, nullptr, &a);
    for (int i = 0; i < 100000; i++) {
      ASSERT_EQ(gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0), GSHSCENE_OK);
    }
    // One call for the list and one per chunk, not one per item.
    EXPECT_LE(t.calls, 24) << "the allocator is called per chunk";
    EXPECT_GT(t.calls, 1);
    EXPECT_EQ(gshscene_resource_refcount(sq.get()), 100001u);
  }
  EXPECT_TRUE(t.live.empty());
  EXPECT_EQ(gshscene_resource_refcount(sq.get()), 1u);
}

TEST(ListArena, ResetReleasesEverythingKeepsTheFirstChunkAndReopensTheList) {
  tracker::Tracker t;
  GSHSCENE_Allocator a = tracker::allocator_for(&t);
  StoreHolder s;
  Ref sq(intern_square(s.store));
  ListHolder l(s.store, nullptr, &a);
  auto record = [&]() {
    for (int i = 0; i < 10; i++) {
      ASSERT_EQ(gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, i, 0), GSHSCENE_OK);
    }
    ASSERT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
  };
  record();
  size_t bytes = gshscene_list_bytes(l.list);
  uint64_t first_hash = gshscene_list_hash(l.list);
  long calls = t.calls;
  EXPECT_EQ(gshscene_resource_refcount(sq.get()), 11u);
  ASSERT_EQ(gshscene_list_reset(l.list), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_count(l.list), 0u);
  EXPECT_EQ(gshscene_list_is_closed(l.list), 0);
  EXPECT_EQ(gshscene_list_expansion(l.list), 0u);
  EXPECT_EQ(gshscene_resource_refcount(sq.get()), 1u) << "a reset releases every reference";
  EXPECT_EQ(gshscene_list_bytes(l.list), bytes) << "the first chunk is kept";
  record();
  EXPECT_EQ(t.calls, calls) << "the second frame takes nothing from the allocator";
  EXPECT_EQ(gshscene_list_hash(l.list), first_hash);
  // Many frames in a row: the first chunk is reused from its start each time, not used up.
  for (int frame = 0; frame < 8; frame++) {
    ASSERT_EQ(gshscene_list_reset(l.list), GSHSCENE_OK);
    record();
    EXPECT_EQ(t.calls, calls) << "frame " << frame;
    EXPECT_EQ(gshscene_list_hash(l.list), first_hash) << "frame " << frame;
  }
  // A longer frame takes more chunks, and a reset gives them back.
  for (int round = 0; round < 3; round++) {
    ASSERT_EQ(gshscene_list_reset(l.list), GSHSCENE_OK);
    for (int i = 0; i < 5000; i++) {
      ASSERT_EQ(gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, i, 0), GSHSCENE_OK);
    }
  }
  size_t live_chunks = t.live.size();
  ASSERT_EQ(gshscene_list_reset(l.list), GSHSCENE_OK);
  EXPECT_LT(t.live.size(), live_chunks);
  EXPECT_EQ(t.live.size(), 2u) << "the list and its first chunk";
}

TEST(ListArena, EverySegmentStartsOnTheArenasAlignment) {
  StoreHolder s;
  Ref sq(intern_square(s.store));
  ListHolder l(s.store);
  size_t first = 0;
  for (int segment = 0; segment < 9; segment++) {
    // Segment k holds 16 << k items and starts at index 16 * (2^k - 1).
    size_t start = 16 * (((size_t)1 << segment) - 1);
    while (gshscene_list_count(l.list) <= start) {
      ASSERT_EQ(gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0), GSHSCENE_OK);
    }
    uintptr_t address = (uintptr_t)item_of(l.list, start);
    EXPECT_EQ(address % 16, 0u) << "segment " << segment << " at item " << start;
    (void)first;
  }
}

TEST(ListArena, AResetListStartsAgainWithNothingLeftOver) {
  StoreHolder s;
  Ref sq(intern_square(s.store));
  // A frame with a push left open, an instance, and enough items to take more than the first chunk.
  GSHSCENE_List * inner = nullptr;
  ASSERT_EQ(gshscene_list_create(nullptr, nullptr, s.store, &inner), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(inner, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0), GSHSCENE_OK);
  gshscene_list_close(inner);
  Ref picture;
  ASSERT_EQ(gshscene_store_intern_picture(s.store, inner, &picture.resource), GSHSCENE_OK);
  gshscene_list_destroy(inner);

  ListHolder l(s.store);
  ASSERT_EQ(gshscene_list_group_push(l.list, nullptr, 0, 0), GSHSCENE_OK);  // first: the stack is the arena's first request
  for (int i = 0; i < 200; i++) {
    ASSERT_EQ(gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, i, 0), GSHSCENE_OK);
  }
  ASSERT_EQ(gshscene_list_instance(l.list, picture.get(), nullptr, 0, 0), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_close(l.list), GSHSCENE_ERR_INVALID) << "the group is open";
  ASSERT_EQ(gshscene_list_reset(l.list), GSHSCENE_OK);
  // After the reset the open group is forgotten, and the instance is not remembered.
  ASSERT_EQ(gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_push(l.list, nullptr, 0, 0), GSHSCENE_OK);  // uses the stack again
  ASSERT_EQ(gshscene_list_group_pop(l.list), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l.list), GSHSCENE_OK) << "no push survives a reset";
  EXPECT_EQ(gshscene_list_expansion(l.list), 3u);
  ListHolder fresh(s.store);
  ASSERT_EQ(gshscene_list_fill(fresh.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_push(fresh.list, nullptr, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_pop(fresh.list), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(fresh.list), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_equal(l.list, fresh.list), 1) << "a reset list is the list it would have been new";
  // And a picture made of it is as deep as a picture of the fresh one: no instance depth is left behind.
  Ref frozen;
  ASSERT_EQ(gshscene_store_intern_picture(s.store, l.list, &frozen.resource), GSHSCENE_OK);
  EXPECT_EQ(gshscene_resource_picture_depth(frozen.get()), 1u);
}

TEST(ListArena, AnItemPointerIsStableUntilTheListIsReset) {
  StoreHolder s;
  Ref sq(intern_square(s.store));
  ListHolder l(s.store);
  ASSERT_EQ(gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 42, 0), GSHSCENE_OK);
  const GSHSCENE_Item * item = item_of(l.list, 0);
  for (int i = 0; i < 5000; i++) {
    ASSERT_EQ(gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0), GSHSCENE_OK);
  }
  EXPECT_EQ(item, item_of(l.list, 0));
  EXPECT_EQ(item->layer, 42);
}

// --- equality and hash -----------------------------------------------------------------------

namespace {

struct Variant {
  const GSHSCENE_Resource * res;
  double tx = 1.0;
  double sx = 1.0;
  bool three_d = false;
  double tz = 0.0;
  GSHGEOM_Fill_Rule rule = GSHGEOM_FILL_NONZERO;
  double rgba[4] = {0.1, 0.2, 0.3, 0.4};
  int32_t layer = 3;
  double opacity = 0.5;
  int32_t group_layer = 4;
  GSHGEOM_Fill_Rule clip_rule = GSHGEOM_FILL_EVEN_ODD;
  bool extra_item = false;
  bool swap_order = false;
  explicit Variant(const GSHSCENE_Resource * r) : res(r) {}
};

GSHSCENE_List * build(GSHSCENE_Store * store, const Variant & v) {
  GSHSCENE_List * l = nullptr;
  gshscene_list_create(nullptr, nullptr, store, &l);
  GSHGEOM_Map m = translate(v.tx, 2.0);
  if (v.sx != 1.0) {
    GSHGEOM_Map scale;
    gshgeom_map_scale(2, v.sx, v.sx, 1.0, &scale);
    gshgeom_map_compose(&m, &scale, &m);
  }
  if (v.three_d) {
    gshgeom_map_translate(3, v.tx, 2.0, v.tz, &m);
  }
  GSHSCENE_Paint p = solid(v.rgba[0], v.rgba[1], v.rgba[2], v.rgba[3]);
  GSHSCENE_Group g = make_group(v.opacity);
  if (v.swap_order) {
    gshscene_list_group_push(l, &g, v.group_layer, 0);
    gshscene_list_fill(l, v.res, &m, v.rule, &p, v.layer, 0);
    gshscene_list_group_pop(l);
  } else {
    gshscene_list_fill(l, v.res, &m, v.rule, &p, v.layer, 0);
    gshscene_list_group_push(l, &g, v.group_layer, 0);
    gshscene_list_group_pop(l);
  }
  gshscene_list_clip_push(l, v.res, &m, v.clip_rule, 0, 0);
  gshscene_list_clip_pop(l);
  if (v.extra_item) {
    gshscene_list_fill(l, v.res, &m, v.rule, &p, v.layer, 0);
  }
  gshscene_list_close(l);
  return l;
}

}  // namespace

TEST(ListEquality, EqualRecordingsAreEqualWithEqualHashesAndOneDifferenceSeparatesThem) {
  StoreHolder s;
  Ref sq(intern_square(s.store)), other(intern_square(s.store, 1.0));
  Variant base(sq.get());
  GSHSCENE_List * a = build(s.store, base);
  GSHSCENE_List * b = build(s.store, base);
  EXPECT_EQ(gshscene_list_equal(a, b), 1);
  EXPECT_EQ(gshscene_list_hash(a), gshscene_list_hash(b));
  EXPECT_EQ(gshscene_list_equal(a, a), 1);
  EXPECT_EQ(gshscene_list_equal(nullptr, a), 0);
  EXPECT_EQ(gshscene_list_equal(a, nullptr), 0);
  EXPECT_EQ(gshscene_list_equal(nullptr, nullptr), 0);

  std::vector<std::pair<const char *, Variant>> variants;
  auto add = [&](const char * what, auto mutate) {
    Variant v(sq.get());
    mutate(v);
    variants.emplace_back(what, v);
  };
  add("the resource", [&](Variant & v) { v.res = other.get(); });
  add("the translation of the map", [](Variant & v) { v.tx = 1.5; });
  add("the matrix of the map", [](Variant & v) { v.sx = 2.0; });
  add("the fill rule", [](Variant & v) { v.rule = GSHGEOM_FILL_EVEN_ODD; });
  add("the red", [](Variant & v) { v.rgba[0] = 0.11; });
  add("the green", [](Variant & v) { v.rgba[1] = 0.21; });
  add("the blue", [](Variant & v) { v.rgba[2] = 0.31; });
  add("the alpha", [](Variant & v) { v.rgba[3] = 0.41; });
  add("the fill layer", [](Variant & v) { v.layer = 4; });
  add("the group opacity", [](Variant & v) { v.opacity = 0.75; });
  add("the group layer", [](Variant & v) { v.group_layer = 5; });
  add("the clip rule", [](Variant & v) { v.clip_rule = GSHGEOM_FILL_NONZERO; });
  add("an extra item", [](Variant & v) { v.extra_item = true; });
  add("the order of the items", [](Variant & v) { v.swap_order = true; });
  std::set<uint64_t> hashes;
  hashes.insert(gshscene_list_hash(a));
  for (auto & [what, v] : variants) {
    GSHSCENE_List * c = build(s.store, v);
    EXPECT_EQ(gshscene_list_equal(a, c), 0) << what;
    EXPECT_NE(gshscene_list_hash(a), gshscene_list_hash(c)) << what;
    EXPECT_TRUE(hashes.insert(gshscene_list_hash(c)).second) << what << " collides with another variant";
    gshscene_list_destroy(c);
  }
  gshscene_list_destroy(a);
  gshscene_list_destroy(b);
}

TEST(ListEquality, TheDepthOfAMapIsPartOfItsEquality) {
  StoreHolder s;
  Ref sq(intern_square(s.store));
  Variant a3(sq.get()), b3(sq.get()), c3(sq.get());
  a3.three_d = b3.three_d = c3.three_d = true;
  a3.tz = 1.0;
  b3.tz = 1.0;
  c3.tz = 2.0;
  GSHSCENE_List * a = build(s.store, a3);
  GSHSCENE_List * b = build(s.store, b3);
  GSHSCENE_List * c = build(s.store, c3);
  EXPECT_EQ(gshscene_list_equal(a, b), 1);
  EXPECT_EQ(gshscene_list_equal(a, c), 0) << "maps differing only in the z of a 3D translation";
  EXPECT_NE(gshscene_list_hash(a), gshscene_list_hash(c));
  gshscene_list_destroy(a);
  gshscene_list_destroy(b);
  gshscene_list_destroy(c);
}

TEST(ListEquality, MapsDifferingInOneEntryOneSignOrOneDimensionAreDifferentMaps) {
  StoreHolder s;
  Ref sq(intern_square(s.store));
  auto list_of = [&](const GSHGEOM_Map & map) {
    GSHSCENE_List * l = nullptr;
    gshscene_list_create(nullptr, nullptr, s.store, &l);
    gshscene_list_fill(l, sq.get(), &map, GSHGEOM_FILL_NONZERO, nullptr, 0, 0);
    gshscene_list_close(l);
    return l;
  };
  auto differ = [&](const char * what, const GSHGEOM_Map & a, const GSHGEOM_Map & b) {
    GSHSCENE_List * la = list_of(a);
    GSHSCENE_List * lb = list_of(b);
    EXPECT_EQ(gshscene_list_equal(la, lb), 0) << what;
    EXPECT_EQ(gshscene_list_equal(lb, la), 0) << what << " (the other way)";
    EXPECT_NE(gshscene_list_hash(la), gshscene_list_hash(lb)) << what;
    gshscene_list_destroy(la);
    gshscene_list_destroy(lb);
  };
  GSHGEOM_Map s2, s3, t2, t3, tneg, tpos;
  gshgeom_map_scale(2, 2, 2, 1, &s2);
  gshgeom_map_scale(2, 3, 3, 1, &s3);
  differ("the same kind of map with another matrix", s2, s3);
  gshgeom_map_translate(2, 1, 2, 0, &t2);
  gshgeom_map_translate(3, 1, 2, 0, &t3);
  differ("a 2D and a 3D map with the same entries", t2, t3);
  differ("a 3D and a 2D map with the same entries", t3, t2);
  gshgeom_map_translate(2, 0.0, 2, 0, &tpos);
  tneg = tpos;
  tneg.t[0] = -0.0;  // the constructor writes +0.0; a hand-filled map may not
  ASSERT_EQ(gshgeom_map_check(&tneg), GSHGEOM_OK);
  differ("-0.0 and +0.0 in a translation", tneg, tpos);
  GSHGEOM_Map z2, z3;
  gshgeom_map_scale(3, 1, 1, 2, &z2);
  gshgeom_map_scale(3, 1, 1, 3, &z3);
  ASSERT_EQ(z2.kind, z3.kind);
  differ("3D maps differing only in the last diagonal entry", z2, z3);
  GSHGEOM_Map e1 = z2, e2 = z2;
  e1.m[0][2] = 0.5;
  e2.m[0][2] = 0.75;
  differ("3D maps differing only in an off-diagonal entry of the last column", e1, e2);
  e1 = z2;
  e2 = z2;
  e1.m[2][0] = 0.5;
  e2.m[2][0] = 0.75;
  differ("3D maps differing only in an off-diagonal entry of the last row", e1, e2);
}

TEST(ListEquality, TheSignOfAZeroOpacityIsPartOfTheGroup) {
  StoreHolder s;
  ListHolder a(s.store), b(s.store);
  GSHSCENE_Group pos = make_group(0.0), neg = make_group(-0.0);
  ASSERT_EQ(gshscene_list_group_push(a.list, &pos, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_push(b.list, &neg, 0, 0), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_equal(a.list, b.list), 0);
  EXPECT_NE(gshscene_list_hash(a.list), gshscene_list_hash(b.list));
}

TEST(ListEquality, TheKindTagOfAMapIsPartOfItsEqualityAndItsHash) {
  StoreHolder s;
  Ref sq(intern_square(s.store));
  GSHGEOM_Map plain;
  gshgeom_map_identity(2, &plain);
  GSHGEOM_Map tagged = plain;
  tagged.kind = GSHGEOM_MAP_QUARTER_TURNS;  // as a quarter turn followed by its inverse is tagged
  ASSERT_EQ(gshgeom_map_check(&tagged), GSHGEOM_OK);
  ListHolder a(s.store), b(s.store);
  ASSERT_EQ(gshscene_list_fill(a.list, sq.get(), &plain, GSHGEOM_FILL_NONZERO, nullptr, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(b.list, sq.get(), &tagged, GSHGEOM_FILL_NONZERO, nullptr, 0, 0), GSHSCENE_OK);
  EXPECT_EQ(item_of(b.list, 0)->map.kind, (uint32_t)GSHGEOM_MAP_QUARTER_TURNS) << "the tag is kept";
  EXPECT_EQ(gshscene_list_equal(a.list, b.list), 0);
  EXPECT_NE(gshscene_list_hash(a.list), gshscene_list_hash(b.list));
}

TEST(ListEquality, AnOpenListHashesAsItWillWhenClosedAndZeroSignsDiffer) {
  StoreHolder s;
  Ref sq(intern_square(s.store));
  ListHolder l(s.store);
  ASSERT_EQ(gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0), GSHSCENE_OK);
  uint64_t open = gshscene_list_hash(l.list);
  ASSERT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_hash(l.list), open);
  ListHolder z(s.store);
  GSHSCENE_Paint negzero = solid(-0.0, 0.0, 0.0, 1.0);
  ASSERT_EQ(gshscene_list_fill(z.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &negzero, 0, 0), GSHSCENE_OK);
  EXPECT_NE(gshscene_list_hash(z.list), open) << "-0.0 is not +0.0 in a hash";
  EXPECT_EQ(gshscene_list_equal(z.list, l.list), 0);
}

// --- allocation failure ---------------------------------------------------------------------------

TEST(ListAllocation, AFailureAtEveryCallLeavesTheListConsistentAndNothingLeaked) {
  GSHGEOM_Path * sqp = square_path();
  for (bool persistent : {false, true}) {
    for (long k = 0;; k++) {
      tracker::Tracker t;
      t.persistent = persistent;
      GSHSCENE_Allocator a = tracker::allocator_for(&t);
      StoreHolder s;
      const GSHSCENE_Resource * sq = intern(s.store, sqp);
      ASSERT_NE(sq, nullptr);
      t.fail_from = k;
      GSHSCENE_List * list = nullptr;
      GSHSCENE_Result rc = gshscene_list_create(&a, nullptr, s.store, &list);
      size_t items = 0, named = 0;
      auto step = [&](GSHSCENE_Result r, bool names) {
        if (rc == GSHSCENE_OK) {
          rc = r;
          if (rc == GSHSCENE_OK) {
            items++;
            named += names;
          }
        }
      };
      if (rc == GSHSCENE_OK) {
        // The group first: the stack is then the arena's first request.  Then 40 fills, which cross
        // two segment boundaries, then a clip and the pops.
        step(gshscene_list_group_push(list, nullptr, 0, 0), false);
        for (int i = 0; i < 40 && rc == GSHSCENE_OK; i++) {
          step(gshscene_list_fill(list, sq, nullptr, GSHGEOM_FILL_NONZERO, nullptr, i, 0), true);
        }
        if (rc == GSHSCENE_OK) {
          step(gshscene_list_clip_push(list, sq, nullptr, GSHGEOM_FILL_NONZERO, 0, 0), true);
        }
        if (rc == GSHSCENE_OK) {
          step(gshscene_list_clip_pop(list), false);
        }
        if (rc == GSHSCENE_OK) {
          step(gshscene_list_group_pop(list), false);
        }
        if (rc != GSHSCENE_OK) {
          EXPECT_EQ(rc, GSHSCENE_ERR_OOM) << "k=" << k;
        }
        EXPECT_EQ(gshscene_list_count(list), items) << "k=" << k;
        // The references are exactly the items that name the resource.
        EXPECT_EQ(gshscene_resource_refcount(sq), 1u + named) << "k=" << k;
        for (size_t i = 0; i < items; i++) {
          ASSERT_NE(item_of(list, i), nullptr);
        }
      } else {
        EXPECT_EQ(rc, GSHSCENE_ERR_OOM);
        EXPECT_EQ(list, nullptr);
      }
      t.fail_from = -1;
      gshscene_list_destroy(list);
      EXPECT_EQ(gshscene_resource_refcount(sq), 1u);
      gshscene_resource_release(sq);
      EXPECT_TRUE(t.live.empty()) << "k=" << k;
      if (!t.failed) {
        break;
      }
    }
  }
  gshgeom_path_destroy(sqp);
}


int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
