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
 * The union member that carries inner clips (clip_union.h, CI1 to CI6): interning and the order of the checks, the canonical form of the
 * inner clips and of the members, the hash and dump, the single clip items, the box, the predicate, the planning pass's refusal, the
 * list that keeps a clipped one-member union, and the copy into another store.
 */

#include "scene_helpers.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace scene;

namespace {

GSHGEOM_Path * rect(double x0, double y0, double x1, double y1) {
  GSHGEOM_Path * p = new_path();
  gshgeom_path_move_to(p, P(x0, y0));
  gshgeom_path_line_to(p, P(x1, y0));
  gshgeom_path_line_to(p, P(x1, y1));
  gshgeom_path_line_to(p, P(x0, y1));
  gshgeom_path_close(p);
  return p;
}

const GSHSCENE_Resource * rect_res(GSHSCENE_Store * s, double x0, double y0, double x1, double y1) {
  GSHGEOM_Path * p = rect(x0, y0, x1, y1);
  const GSHSCENE_Resource * r = intern(s, p);
  gshgeom_path_destroy(p);
  return r;
}

GSHGEOM_Map translated(double x, double y) {
  GSHGEOM_Map m;
  gshgeom_map_translate(2, x, y, 0, &m);
  return m;
}

GSHSCENE_Clip_Inner inner_of(const GSHSCENE_Resource * r, GSHGEOM_Fill_Rule rule = GSHGEOM_FILL_NONZERO, const GSHGEOM_Map * map = nullptr) {
  GSHSCENE_Clip_Inner in;
  std::memset(&in, 0, sizeof in);
  in.resource = r;
  in.rule = rule;
  if (map) {
    in.map = *map;
  }
  return in;
}

GSHSCENE_Clip_Member clipped(const GSHSCENE_Resource * r, const std::vector<GSHSCENE_Clip_Inner> & inner,
    GSHGEOM_Fill_Rule rule = GSHGEOM_FILL_NONZERO, const GSHGEOM_Map * map = nullptr) {
  GSHSCENE_Clip_Member m;
  std::memset(&m, 0, sizeof m);
  m.resource = r;
  m.rule = rule;
  if (map) {
    m.map = *map;
  }
  m.inner_count = inner.size();
  m.inner = inner.empty() ? nullptr : inner.data();
  return m;
}

GSHSCENE_Clip_Member plain(const GSHSCENE_Resource * r) {
  return clipped(r, {});
}

GSHSCENE_Result intern_union(GSHSCENE_Store * s, const std::vector<GSHSCENE_Clip_Member> & m, Ref * out = nullptr) {
  const GSHSCENE_Resource * r = nullptr;
  GSHSCENE_Result got = gshscene_store_intern_clip_union(s, m.data(), m.size(), &r);
  if (out && got == GSHSCENE_OK) {
    *out = Ref(r);
  } else if (got == GSHSCENE_OK) {
    gshscene_resource_release(r);
  }
  return got;
}

std::string dump_of(const GSHSCENE_Resource * r) {
  char * buf = nullptr;
  size_t size = 0;
  FILE * f = open_memstream(&buf, &size);
  EXPECT_EQ(gshscene_resource_dump(r, f), GSHSCENE_OK);
  fclose(f);
  std::string s(buf, size);
  free(buf);
  return s;
}

/** A union clip push of @p u in a list, the item read back. */
struct Pushed {
  std::unique_ptr<ListHolder> list;
  const GSHSCENE_Item * item = nullptr;
  Pushed(GSHSCENE_Store * s, const GSHSCENE_Resource * u, const GSHGEOM_Map * map = nullptr) : list(new ListHolder(s)) {
    EXPECT_EQ(gshscene_list_clip_push(list->list, u, map, GSHGEOM_FILL_NONZERO, 0, 0), GSHSCENE_OK);
    EXPECT_EQ(gshscene_list_item(list->list, 0, &item), GSHSCENE_OK);
  }
};

}  // namespace

TEST(ClipUnionInner, AClippedMemberReadsBackItsInnerClipsInCanonicalOrder) {
  StoreHolder s;
  Ref a(rect_res(s.store, 0, 0, 10, 10)), b(rect_res(s.store, 5, 5, 15, 15)), c(rect_res(s.store, 2, 2, 8, 20));
  Ref u;
  ASSERT_EQ(intern_union(s.store, {clipped(a.get(), {inner_of(b.get()), inner_of(c.get(), GSHGEOM_FILL_EVEN_ODD)})}, &u), GSHSCENE_OK);
  ASSERT_EQ(gshscene_resource_clip_union_count(u.get()), 1u);
  GSHSCENE_Clip_Member m;
  ASSERT_EQ(gshscene_resource_clip_union_member(u.get(), 0, &m), GSHSCENE_OK);
  EXPECT_EQ(m.resource, a.get());
  ASSERT_EQ(m.inner_count, 2u);
  ASSERT_NE(m.inner, nullptr);
  uint64_t h0 = gshscene_resource_hash(m.inner[0].resource), h1 = gshscene_resource_hash(m.inner[1].resource);
  EXPECT_LT(h0, h1) << "ascending by the resource's content hash";
  for (size_t k = 0; k < 2; k++) {
    EXPECT_EQ(m.inner[k].map.dim, 2) << "a zero map is the identity";
  }
  const GSHSCENE_Resource * with_even_odd = m.inner[0].resource == c.get() ? m.inner[0].resource : m.inner[1].resource;
  EXPECT_EQ(with_even_odd, c.get());
  /* a plain member reads back with none */
  Ref p;
  ASSERT_EQ(intern_union(s.store, {plain(a.get())}, &p), GSHSCENE_OK);
  ASSERT_EQ(gshscene_resource_clip_union_member(p.get(), 0, &m), GSHSCENE_OK);
  EXPECT_EQ(m.inner_count, 0u);
  EXPECT_EQ(m.inner, nullptr);
}

TEST(ClipUnionInner, TheInnerClipsOfAMemberAreAnUnorderedSetWithDuplicatesRemoved) {
  StoreHolder s;
  Ref a(rect_res(s.store, 0, 0, 10, 10)), b(rect_res(s.store, 5, 5, 15, 15)), c(rect_res(s.store, 2, 2, 8, 20));
  Ref u1, u2, u3, u4, u5;
  ASSERT_EQ(intern_union(s.store, {clipped(a.get(), {inner_of(b.get()), inner_of(c.get())})}, &u1), GSHSCENE_OK);
  ASSERT_EQ(intern_union(s.store, {clipped(a.get(), {inner_of(c.get()), inner_of(b.get())})}, &u2), GSHSCENE_OK);
  ASSERT_EQ(intern_union(s.store, {clipped(a.get(), {inner_of(c.get()), inner_of(b.get()), inner_of(c.get()), inner_of(b.get())})}, &u3), GSHSCENE_OK);
  EXPECT_EQ(u1.get(), u2.get());
  EXPECT_EQ(u1.get(), u3.get()) << "the intersection of a region with itself is the region";
  GSHSCENE_Clip_Member m;
  ASSERT_EQ(gshscene_resource_clip_union_member(u3.get(), 0, &m), GSHSCENE_OK);
  EXPECT_EQ(m.inner_count, 2u);
  /* one inner clip fewer, a different rule or a different map on one is a different union */
  ASSERT_EQ(intern_union(s.store, {clipped(a.get(), {inner_of(b.get())})}, &u4), GSHSCENE_OK);
  EXPECT_NE(u4.get(), u1.get());
  ASSERT_EQ(intern_union(s.store, {clipped(a.get(), {inner_of(b.get(), GSHGEOM_FILL_EVEN_ODD), inner_of(c.get())})}, &u5), GSHSCENE_OK);
  EXPECT_NE(u5.get(), u1.get());
  GSHGEOM_Map t = translated(1, 0);
  Ref u6;
  ASSERT_EQ(intern_union(s.store, {clipped(a.get(), {inner_of(b.get(), GSHGEOM_FILL_NONZERO, &t), inner_of(c.get())})}, &u6), GSHSCENE_OK);
  EXPECT_NE(u6.get(), u1.get());
  /* the same member plain or clipped is not the same union */
  Ref u7;
  ASSERT_EQ(intern_union(s.store, {plain(a.get())}, &u7), GSHSCENE_OK);
  EXPECT_NE(u7.get(), u4.get());
  EXPECT_NE(gshscene_resource_hash(u7.get()), gshscene_resource_hash(u4.get()));
}

TEST(ClipUnionInner, MembersEqualInTheirOwnKeyAreToldApartByTheirInnerClipsAndEqualOnesAreOne) {
  StoreHolder s;
  Ref a(rect_res(s.store, 0, 0, 10, 10)), b(rect_res(s.store, 5, 5, 15, 15)), c(rect_res(s.store, 2, 2, 8, 20));
  Ref u;
  ASSERT_EQ(intern_union(s.store, {clipped(a.get(), {inner_of(b.get())}), clipped(a.get(), {inner_of(c.get())}), plain(a.get())}, &u), GSHSCENE_OK);
  EXPECT_EQ(gshscene_resource_clip_union_count(u.get()), 3u) << "the same contours, three regions";
  Ref v;
  ASSERT_EQ(intern_union(s.store, {clipped(a.get(), {inner_of(c.get())}), plain(a.get()), clipped(a.get(), {inner_of(b.get())}),
      clipped(a.get(), {inner_of(b.get())})}, &v), GSHSCENE_OK);
  EXPECT_EQ(v.get(), u.get()) << "any order, and a repeated clipped member is one";
  /* the order is by the member's own key, then its inner clips' hash: a plain member (no inner clips, hash 0) first */
  GSHSCENE_Clip_Member m;
  ASSERT_EQ(gshscene_resource_clip_union_member(u.get(), 0, &m), GSHSCENE_OK);
  EXPECT_EQ(m.inner_count, 0u);
}

TEST(ClipUnionInner, TheChecksAreInTheOrderTheHeaderStatesForInnerClipsToo) {
  StoreHolder s, other;
  Ref a(rect_res(s.store, 0, 0, 10, 10)), foreign(rect_res(other.store, 0, 0, 10, 10));
  GSHGEOM_Path * sq = square_path();
  Ref fill_resource;  // a resource that is neither a path nor a glyph run: the plane
  ASSERT_EQ(gshscene_store_intern_plane(s.store, &fill_resource.resource), GSHSCENE_OK);
  gshgeom_path_destroy(sq);
  EXPECT_EQ(intern_union(s.store, {clipped(a.get(), {inner_of(nullptr)})}), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(intern_union(s.store, {clipped(a.get(), {inner_of(foreign.get())})}), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(intern_union(s.store, {clipped(a.get(), {inner_of(fill_resource.get())})}), GSHSCENE_ERR_INVALID);
  GSHSCENE_Clip_Inner bad_rule = inner_of(a.get());
  bad_rule.rule = static_cast<GSHGEOM_Fill_Rule>(7);
  EXPECT_EQ(intern_union(s.store, {clipped(a.get(), {bad_rule})}), GSHSCENE_ERR_INVALID);
  GSHSCENE_Clip_Member no_array = clipped(a.get(), {});
  no_array.inner_count = 2;
  EXPECT_EQ(intern_union(s.store, {no_array}), GSHSCENE_ERR_INVALID) << "a count with no array";
  /* an inner clip with a 3D map is unsupported, after every invalid thing */
  GSHGEOM_Map m3;
  gshgeom_map_identity(3, &m3);
  EXPECT_EQ(intern_union(s.store, {clipped(a.get(), {inner_of(a.get(), GSHGEOM_FILL_NONZERO, &m3)})}), GSHSCENE_ERR_UNSUPPORTED);
  EXPECT_EQ(intern_union(s.store, {clipped(a.get(), {inner_of(a.get(), GSHGEOM_FILL_NONZERO, &m3), inner_of(nullptr)})}), GSHSCENE_ERR_INVALID)
      << "invalid before unsupported, over every inner clip";
  /* an inner clip that is a glyph run from a font is one like another (CU24) */
  Ref face(intern_face(s.store));
  Ref run(intern_font_run(s.store, face.get(), 12.0, {{1, 0.0, 0.0, nullptr}}));
  EXPECT_EQ(intern_union(s.store, {clipped(a.get(), {inner_of(run.get())})}), GSHSCENE_OK);
  EXPECT_EQ(intern_union(s.store, {clipped(a.get(), {inner_of(run.get(), GSHGEOM_FILL_NONZERO, &m3)})}), GSHSCENE_ERR_UNSUPPORTED);
}

TEST(ClipUnionInner, MembersAndInnerClipsAreCountedTogetherAgainstTheLimit) {
  GSHSCENE_Limits l;
  gshscene_limits_default(&l);
  l.max_clip_members = 4;
  StoreHolder s(&l);
  Ref a(rect_res(s.store, 0, 0, 10, 10)), b(rect_res(s.store, 5, 5, 15, 15)), c(rect_res(s.store, 2, 2, 8, 20)), d(rect_res(s.store, 1, 1, 9, 9));
  EXPECT_EQ(intern_union(s.store, {clipped(a.get(), {inner_of(b.get()), inner_of(c.get()), inner_of(d.get())})}), GSHSCENE_OK) << "1 + 3 = 4";
  EXPECT_EQ(intern_union(s.store, {clipped(a.get(), {inner_of(b.get()), inner_of(c.get())}), plain(b.get()), plain(c.get())}), GSHSCENE_ERR_LIMIT)
      << "2 + 2 + 1 = 5 given";
  EXPECT_EQ(intern_union(s.store, {clipped(a.get(), {inner_of(b.get()), inner_of(b.get()), inner_of(b.get()), inner_of(b.get())})}), GSHSCENE_ERR_LIMIT)
      << "the count is what was given, before duplicates are removed";
}

TEST(ClipUnionInner, TheDumpAddsALinePerInnerClipAndAPlainMemberDumpsAsBefore) {
  StoreHolder s;
  Ref a(rect_res(s.store, 0, 0, 10, 10)), b(rect_res(s.store, 5, 5, 15, 15));
  Ref plain_union, clipped_union;
  ASSERT_EQ(intern_union(s.store, {plain(a.get())}, &plain_union), GSHSCENE_OK);
  ASSERT_EQ(intern_union(s.store, {clipped(a.get(), {inner_of(b.get(), GSHGEOM_FILL_EVEN_ODD)})}, &clipped_union), GSHSCENE_OK);
  const std::string p = dump_of(plain_union.get());
  EXPECT_NE(p.find("members 1\nm 0 res=path:"), std::string::npos) << p;
  EXPECT_EQ(p.find(" inner="), std::string::npos) << "a plain member's line has no inner count";
  EXPECT_EQ(p.find("\ni "), std::string::npos);
  const std::string c = dump_of(clipped_union.get());
  EXPECT_NE(c.find(" rule=0 inner=1\n"), std::string::npos) << c;
  EXPECT_NE(c.find("\ni 0 0 res=path:"), std::string::npos) << c;
  EXPECT_NE(c.find(" rule=1\n"), std::string::npos) << "the inner clip's own rule: " << c;
}

TEST(ClipUnionInner, TheHashIsTheFieldsAsTheHeaderSpellsThem) {
  StoreHolder s;
  Ref a(rect_res(s.store, 0, 0, 10, 10)), b(rect_res(s.store, 5, 5, 15, 15)), c(rect_res(s.store, 2, 2, 8, 20));
  GSHGEOM_Map id;
  gshgeom_map_identity(2, &id);
  GSHGEOM_Map t = translated(1, 0);
  Ref u;
  ASSERT_EQ(intern_union(s.store, {clipped(a.get(), {inner_of(b.get(), GSHGEOM_FILL_EVEN_ODD, &t), inner_of(c.get())}, GSHGEOM_FILL_EVEN_ODD)}, &u),
      GSHSCENE_OK);
  struct K {
    uint64_t res, map;
    uint8_t rule;
  };
  std::vector<K> inner = {{gshscene_resource_hash(b.get()), gshgeom_map_hash(&t), 1}, {gshscene_resource_hash(c.get()), gshgeom_map_hash(&id), 0}};
  std::sort(inner.begin(), inner.end(), [](const K & x, const K & y) {
    return x.res != y.res ? x.res < y.res : x.map != y.map ? x.map < y.map : x.rule < y.rule;
  });
  GSHGEOM_Hash h;
  gshgeom_hash_init(&h);
  gshgeom_hash_u32(&h, 1u);
  gshgeom_hash_u32(&h, 14u);
  gshgeom_hash_u64(&h, 1u);
  gshgeom_hash_u64(&h, gshscene_resource_hash(a.get()));
  gshgeom_hash_u64(&h, gshgeom_map_hash(&id));
  gshgeom_hash_u8(&h, (uint8_t)(1u | 0x80u));
  gshgeom_hash_u64(&h, 2u);
  for (const K & k : inner) {
    gshgeom_hash_u64(&h, k.res);
    gshgeom_hash_u64(&h, k.map);
    gshgeom_hash_u8(&h, k.rule);
  }
  EXPECT_EQ(gshscene_resource_hash(u.get()), gshgeom_hash_value(&h));
  /* every field of an inner clip changes it */
  std::vector<uint64_t> hashes{gshscene_resource_hash(u.get())};
  auto add = [&](std::vector<GSHSCENE_Clip_Inner> in, GSHGEOM_Fill_Rule rule) {
    Ref v;
    ASSERT_EQ(intern_union(s.store, {clipped(a.get(), in, rule)}, &v), GSHSCENE_OK);
    hashes.push_back(gshscene_resource_hash(v.get()));
  };
  add({inner_of(b.get(), GSHGEOM_FILL_EVEN_ODD, &t), inner_of(c.get())}, GSHGEOM_FILL_NONZERO);          // the member's rule
  add({inner_of(b.get(), GSHGEOM_FILL_NONZERO, &t), inner_of(c.get())}, GSHGEOM_FILL_EVEN_ODD);          // an inner rule
  add({inner_of(b.get(), GSHGEOM_FILL_EVEN_ODD), inner_of(c.get())}, GSHGEOM_FILL_EVEN_ODD);             // an inner map
  add({inner_of(b.get(), GSHGEOM_FILL_EVEN_ODD, &t), inner_of(a.get())}, GSHGEOM_FILL_EVEN_ODD);         // an inner resource
  add({inner_of(b.get(), GSHGEOM_FILL_EVEN_ODD, &t)}, GSHGEOM_FILL_EVEN_ODD);                            // an inner clip fewer
  std::sort(hashes.begin(), hashes.end());
  EXPECT_EQ(std::adjacent_find(hashes.begin(), hashes.end()), hashes.end());
}

TEST(ClipUnionInner, AccountedBytesAddAnInnerFigureForEachInnerClip) {
  StoreHolder s;
  Ref a(rect_res(s.store, 0, 0, 10, 10)), b(rect_res(s.store, 5, 5, 15, 15)), c(rect_res(s.store, 2, 2, 8, 20));
  EXPECT_GE(GSHSCENE_BYTES_PER_CLIP_INNER, sizeof(GSHSCENE_Clip_Inner)) << "at least the record the store keeps";
  EXPECT_GE(GSHSCENE_BYTES_PER_CLIP_MEMBER, sizeof(GSHSCENE_Clip_Member));
  GSHSCENE_Store_Stats before;
  gshscene_store_stats(s.store, &before);
  Ref u;
  ASSERT_EQ(intern_union(s.store, {clipped(a.get(), {inner_of(b.get()), inner_of(c.get())}), plain(b.get())}, &u), GSHSCENE_OK);
  GSHSCENE_Store_Stats after;
  gshscene_store_stats(s.store, &after);
  EXPECT_EQ(after.bytes - before.bytes, GSHSCENE_BYTES_BASE + 2 * GSHSCENE_BYTES_PER_CLIP_MEMBER + 2 * GSHSCENE_BYTES_PER_CLIP_INNER);
  u = Ref();
  GSHSCENE_Store_Stats gone;
  gshscene_store_stats(s.store, &gone);
  EXPECT_EQ(gone.bytes, before.bytes) << "the last release gives the bytes back";
}

TEST(ClipUnionInner, TheSingleClipItemsComposeTheItemsMapAfterTheirOwn) {
  StoreHolder s;
  Ref a(rect_res(s.store, 0, 0, 10, 10)), b(rect_res(s.store, 5, 5, 15, 15));
  GSHGEOM_Map own = translated(3, 4), inner_map = translated(10, 0), item_map = translated(100, 0);
  Ref u;
  ASSERT_EQ(intern_union(s.store, {clipped(a.get(), {inner_of(b.get(), GSHGEOM_FILL_EVEN_ODD, &inner_map)}, GSHGEOM_FILL_NONZERO, &own)}, &u), GSHSCENE_OK);
  Pushed push(s.store, u.get(), &item_map);
  GSHSCENE_Item mi, ii;
  ASSERT_EQ(gshscene_clip_union_member_item(push.item, 0, &mi), GSHSCENE_OK);
  ASSERT_EQ(gshscene_clip_union_inner_item(push.item, 0, 0, &ii), GSHSCENE_OK);
  EXPECT_EQ(mi.resource, a.get());
  EXPECT_EQ(ii.resource, b.get());
  EXPECT_EQ(ii.fill_rule, GSHGEOM_FILL_EVEN_ODD);
  GSHGEOM_Point o{0, 0, 0}, p;
  ASSERT_EQ(gshgeom_map_apply_point(&mi.map, o, &p), GSHGEOM_OK);
  EXPECT_EQ(p.x, 103.0);
  EXPECT_EQ(p.y, 4.0);
  ASSERT_EQ(gshgeom_map_apply_point(&ii.map, o, &p), GSHGEOM_OK);
  EXPECT_EQ(p.x, 110.0) << "the inner clip's map is not composed with the member's (CI1)";
  EXPECT_EQ(p.y, 0.0);
  GSHSCENE_Item none;
  EXPECT_EQ(gshscene_clip_union_inner_item(push.item, 0, 1, &none), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_clip_union_inner_item(push.item, 1, 0, &none), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_clip_union_inner_item(nullptr, 0, 0, &none), GSHSCENE_ERR_INVALID);
}

TEST(ClipUnionInner, AUnionOfOnePlainMemberIsTheSingleClipAndOneWithInnerClipsStaysAUnion) {
  StoreHolder s;
  Ref a(rect_res(s.store, 0, 0, 10, 10)), b(rect_res(s.store, 5, 5, 15, 15));
  Ref pu, cu;
  ASSERT_EQ(intern_union(s.store, {plain(a.get())}, &pu), GSHSCENE_OK);
  ASSERT_EQ(intern_union(s.store, {clipped(a.get(), {inner_of(b.get())})}, &cu), GSHSCENE_OK);
  Pushed p(s.store, pu.get()), c(s.store, cu.get());
  EXPECT_EQ(gshscene_resource_kind(p.item->resource), GSHSCENE_RESOURCE_PATH) << "CU10";
  EXPECT_EQ(gshscene_resource_kind(c.item->resource), GSHSCENE_RESOURCE_CLIP_UNION) << "CI6";
}

TEST(ClipUnionInner, TheBoxOfAClippedMemberIsTheMeetOfItsPartsBoxes) {
  StoreHolder s;
  Ref a(rect_res(s.store, 0, 0, 10, 10)), b(rect_res(s.store, 5, 5, 15, 15)), far_away(rect_res(s.store, 100, 100, 110, 110));
  Ref u, e;
  ASSERT_EQ(intern_union(s.store, {clipped(a.get(), {inner_of(b.get())})}, &u), GSHSCENE_OK);
  ASSERT_EQ(intern_union(s.store, {clipped(a.get(), {inner_of(far_away.get())})}, &e), GSHSCENE_OK);
  Pushed pu(s.store, u.get()), pe(s.store, e.get());
  GSHGEOM_Device_Box box;
  ASSERT_EQ(gshscene_clip_union_member_box(pu.item, 0, nullptr, 0, nullptr, &box), GSHSCENE_OK);
  EXPECT_EQ(box.empty, 0);
  EXPECT_EQ(box.x0, 5 * 64);
  EXPECT_EQ(box.y0, 5 * 64);
  EXPECT_EQ(box.x1, 10 * 64);
  EXPECT_EQ(box.y1, 10 * 64);
  ASSERT_EQ(gshscene_clip_union_member_box(pe.item, 0, nullptr, 0, nullptr, &box), GSHSCENE_OK);
  EXPECT_NE(box.empty, 0) << "parts whose boxes do not meet: an empty region";
  /* under a target map the same meet is formed in device space */
  GSHGEOM_Map t = translated(2, 0);
  ASSERT_EQ(gshscene_clip_union_member_box(pu.item, 0, nullptr, 0, &t, &box), GSHSCENE_OK);
  EXPECT_EQ(box.x0, 7 * 64);
  EXPECT_EQ(box.x1, 12 * 64);
}

TEST(ClipUnionInner, AClippedMemberIsNeverProvablyDisjointAndTheVerdictNamesTheFirst) {
  StoreHolder s;
  Ref a(rect_res(s.store, 0, 0, 10, 10)), b(rect_res(s.store, 5, 5, 15, 15)), far_away(rect_res(s.store, 100, 0, 110, 10));
  Ref plain_two, clipped_two, clipped_first;
  /* two plain members far apart: disjoint, as ever */
  ASSERT_EQ(intern_union(s.store, {plain(a.get()), plain(far_away.get())}, &plain_two), GSHSCENE_OK);
  ASSERT_EQ(intern_union(s.store, {clipped(a.get(), {inner_of(b.get())}), plain(far_away.get())}, &clipped_two), GSHSCENE_OK);
  Pushed p(s.store, plain_two.get()), c(s.store, clipped_two.get());
  GSHSCENE_Union_Verdict v;
  ASSERT_EQ(gshscene_clip_union_disjoint(p.item, nullptr, 0, nullptr, &v), GSHSCENE_OK);
  EXPECT_EQ(v.disjoint, 1);
  ASSERT_EQ(gshscene_clip_union_disjoint(c.item, nullptr, 0, nullptr, &v), GSHSCENE_OK);
  EXPECT_EQ(v.disjoint, 0) << "CI4: the boxes are far apart, but the fast tier cannot add a clipped member's area";
  EXPECT_EQ(v.first, v.second);
  GSHSCENE_Clip_Member m;
  ASSERT_EQ(gshscene_resource_clip_union_member(clipped_two.get(), v.first, &m), GSHSCENE_OK);
  EXPECT_EQ(m.inner_count, 1u) << "it names the clipped member, by its canonical index";
}

TEST(ClipUnionInner, APlanningPassWithoutTheCapabilityRefusesTheUnionAndNamesTheFirstClippedMember) {
  StoreHolder s;
  Ref a(rect_res(s.store, 0, 0, 10, 10)), b(rect_res(s.store, 5, 5, 15, 15)), c(rect_res(s.store, 20, 0, 30, 10));
  Ref u;
  ASSERT_EQ(intern_union(s.store, {plain(c.get()), clipped(a.get(), {inner_of(b.get())})}, &u), GSHSCENE_OK);
  Pushed p(s.store, u.get());
  auto caps_with = [](uint32_t flags) {
    GSHSCENE_Sink_Caps caps;
    std::memset(&caps, 0, sizeof caps);
    for (uint32_t k = GSHSCENE_ITEM_FILL; k < GSHSCENE_ITEM_KIND_COUNT; k++) {
      caps.item_kinds |= UINT32_C(1) << k;
    }
    caps.flags = flags;
    return caps;
  };
  const uint32_t base = GSHSCENE_CAP_CLIP_AA | GSHSCENE_CAP_CLIP_UNION | GSHSCENE_CAP_CLIP_UNION_GENERAL;
  GSHSCENE_Sink_Caps without = caps_with(base);
  GSHSCENE_Sink_Caps with = caps_with(base | GSHSCENE_CAP_CLIP_UNION_INNER);
  GSHSCENE_Disposition d = gshscene_caps_disposition(&without, p.item, nullptr, 0, nullptr, nullptr, nullptr);
  EXPECT_EQ(d.entry_class, GSHSCENE_ENTRY_REFUSED);
  EXPECT_EQ(d.reason, GSHSCENE_REASON_UNION_INNER);
  size_t first = (size_t)-1;
  for (size_t i = 0; i < 2; i++) {
    GSHSCENE_Clip_Member m;
    gshscene_resource_clip_union_member(u.get(), i, &m);
    if (m.inner_count && first == (size_t)-1) {
      first = i;
    }
  }
  EXPECT_EQ(d.detail, ((uint64_t)first << 16) | first);
  d = gshscene_caps_disposition(&with, p.item, nullptr, 0, nullptr, nullptr, nullptr);
  EXPECT_NE(d.entry_class, GSHSCENE_ENTRY_REFUSED);
  /* a union with no clipped member is not refused for lacking the capability */
  Ref plain_u;
  ASSERT_EQ(intern_union(s.store, {plain(a.get()), plain(c.get())}, &plain_u), GSHSCENE_OK);
  Pushed pp(s.store, plain_u.get());
  d = gshscene_caps_disposition(&without, pp.item, nullptr, 0, nullptr, nullptr, nullptr);
  EXPECT_NE(d.entry_class, GSHSCENE_ENTRY_REFUSED);
}

TEST(ClipUnionInner, TheCopyIntoAnotherStoreHasTheSameHashAndDumpAndHoldsNothingOfTheFirst) {
  StoreHolder dst;
  Ref copy;
  uint64_t hash = 0;
  std::string dump;
  {
    StoreHolder s;
    Ref a(rect_res(s.store, 0, 0, 10, 10)), b(rect_res(s.store, 5, 5, 15, 15)), c(rect_res(s.store, 2, 2, 8, 20));
    GSHGEOM_Map t = translated(1, 2);
    Ref u;
    ASSERT_EQ(intern_union(s.store, {clipped(a.get(), {inner_of(b.get(), GSHGEOM_FILL_EVEN_ODD, &t), inner_of(c.get())}, GSHGEOM_FILL_EVEN_ODD),
        plain(c.get())}, &u), GSHSCENE_OK);
    hash = gshscene_resource_hash(u.get());
    dump = dump_of(u.get());
    const GSHSCENE_Resource * out = nullptr;
    ASSERT_EQ(gshscene_store_copy_resource(dst.store, u.get(), &out), GSHSCENE_OK);
    copy = Ref(out);
  }
  EXPECT_EQ(gshscene_resource_hash(copy.get()), hash);
  EXPECT_EQ(dump_of(copy.get()), dump);
  GSHSCENE_Clip_Member m;
  for (size_t i = 0; i < gshscene_resource_clip_union_count(copy.get()); i++) {
    ASSERT_EQ(gshscene_resource_clip_union_member(copy.get(), i, &m), GSHSCENE_OK);
    for (size_t k = 0; k < m.inner_count; k++) {
      EXPECT_NE(m.inner[k].resource, nullptr);
    }
  }
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
