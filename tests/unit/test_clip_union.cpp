/**
 * @file
 *
 * The clip union resource (clip_union.h, CU6 to CU11, CU14): interning, the validity order, the canonical set, the limit, the
 * hash and dump restated from the text, accounting, the clip push and its one-member collapse, a group's clip, and an allocation
 * failure at every call.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "../scene_fixtures.h"
#include "../scene_helpers.h"
#include "../alloc_tracker.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <tuple>
#include <vector>

using namespace scene;

namespace {

GSHSCENE_Clip_Member member(const GSHSCENE_Resource * r, GSHGEOM_Fill_Rule rule = GSHGEOM_FILL_NONZERO,
    const GSHGEOM_Map * map = nullptr) {
  GSHSCENE_Clip_Member m;
  std::memset(&m, 0, sizeof m);
  m.resource = r;
  m.rule = rule;
  if (map) {
    m.map = *map;
  }
  return m;
}

GSHGEOM_Map translated(double x, double y) {
  GSHGEOM_Map m;
  gshgeom_map_translate(2, x, y, 0, &m);
  return m;
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

std::string list_dump(const GSHSCENE_List * l) {
  char * buf = nullptr;
  size_t size = 0;
  FILE * f = open_memstream(&buf, &size);
  EXPECT_EQ(gshscene_list_dump(l, f), GSHSCENE_OK);
  fclose(f);
  std::string s(buf, size);
  free(buf);
  return s;
}

const GSHSCENE_Resource * union_of(GSHSCENE_Store * store, std::vector<GSHSCENE_Clip_Member> m, GSHSCENE_Result * result = nullptr) {
  const GSHSCENE_Resource * r = nullptr;
  GSHSCENE_Result got = gshscene_store_intern_clip_union(store, m.data(), m.size(), &r);
  if (result) {
    *result = got;
  }
  return got == GSHSCENE_OK ? r : nullptr;
}

}  // namespace

TEST(ClipUnion, KindAndStringAndCapsArePinned) {
  EXPECT_EQ((int)GSHSCENE_RESOURCE_CLIP_UNION, 14);
  EXPECT_EQ((int)GSHSCENE_RESOURCE_KIND_COUNT, 15);
  EXPECT_STREQ(gshscene_resource_kind_string(GSHSCENE_RESOURCE_CLIP_UNION), "clip-union");
  EXPECT_EQ(GSHSCENE_CAP_CLIP_UNION, 0x20000u);
  EXPECT_EQ(GSHSCENE_CAP_CLIP_UNION_GENERAL, 0x40000u);
  EXPECT_EQ(GSHSCENE_CAP_FLAGS_ALL, 0x3fffffu);
  EXPECT_EQ((int)GSHSCENE_REASON_CLIP_UNION, 41);
  EXPECT_EQ((int)GSHSCENE_REASON_UNION_OVERLAP, 42);
  EXPECT_EQ((int)GSHSCENE_REASON_COUNT, 45);
  EXPECT_STREQ(gshscene_reason_string(GSHSCENE_REASON_CLIP_UNION), "clip-union");
  EXPECT_STREQ(gshscene_reason_string(GSHSCENE_REASON_UNION_OVERLAP), "union-overlap");
  EXPECT_EQ((int)GSHSCENE_DUMP_RECORD_CLIP_UNION, 4);
  EXPECT_EQ((int)GSHSCENE_DUMP_RECORDS_ALL, 7);
  EXPECT_EQ(GSHSCENE_LIMITS_DEFAULT_MAX_CLIP_MEMBERS, (size_t)256);
  EXPECT_EQ(GSHSCENE_CLIP_MEMBERS_CEILING, (size_t)1024);
}

TEST(ClipUnion, AnyOrderAndRepetitionIsOneHandleInCanonicalOrder) {
  StoreHolder s;
  Ref a(intern_square(s.store, 0)), b(intern_square(s.store, 10)), c(intern_square(s.store, 20));
  Ref u1(union_of(s.store, {member(a.get()), member(b.get()), member(c.get())}));
  Ref u2(union_of(s.store, {member(c.get()), member(a.get()), member(b.get())}));
  Ref u3(union_of(s.store, {member(b.get()), member(b.get()), member(c.get()), member(a.get()), member(a.get())}));
  ASSERT_NE(u1.get(), nullptr);
  EXPECT_EQ(u1.get(), u2.get());
  EXPECT_EQ(u1.get(), u3.get());
  EXPECT_EQ(gshscene_resource_clip_union_count(u1.get()), 3u);
  // canonical order: ascending by the resource's content hash (maps equal, rules equal)
  uint64_t last = 0;
  for (size_t i = 0; i < 3; i++) {
    GSHSCENE_Clip_Member m;
    ASSERT_EQ(gshscene_resource_clip_union_member(u1.get(), i, &m), GSHSCENE_OK);
    uint64_t h = gshscene_resource_hash(m.resource);
    if (i) {
      EXPECT_LT(last, h);
    }
    last = h;
  }
  // a different rule, or a different map, is a different member
  Ref u4(union_of(s.store, {member(a.get(), GSHGEOM_FILL_EVEN_ODD), member(b.get()), member(c.get())}));
  EXPECT_NE(u4.get(), u1.get());
  GSHGEOM_Map t = translated(1, 0);
  Ref u5(union_of(s.store, {member(a.get(), GSHGEOM_FILL_NONZERO, &t), member(b.get()), member(c.get())}));
  EXPECT_NE(u5.get(), u1.get());
  // the same resource under two maps and two rules: four members
  GSHGEOM_Map id;
  gshgeom_map_identity(2, &id);
  Ref u6(union_of(s.store, {member(a.get()), member(a.get(), GSHGEOM_FILL_EVEN_ODD), member(a.get(), GSHGEOM_FILL_NONZERO, &t),
      member(a.get(), GSHGEOM_FILL_NONZERO, &id)}));
  EXPECT_EQ(gshscene_resource_clip_union_count(u6.get()), 3u) << "the zero map and the identity are one member";
  // a one-member list is a legitimate resource of one member
  Ref u7(union_of(s.store, {member(a.get())}));
  ASSERT_NE(u7.get(), nullptr);
  EXPECT_EQ(gshscene_resource_clip_union_count(u7.get()), 1u);
  // minus zero and plus zero differ
  GSHGEOM_Map mz = translated(0.0, 0.0), pz = translated(0.0, 0.0);
  mz.t[0] = -0.0;
  Ref u8(union_of(s.store, {member(a.get(), GSHGEOM_FILL_NONZERO, &mz)}));
  Ref u9(union_of(s.store, {member(a.get(), GSHGEOM_FILL_NONZERO, &pz)}));
  EXPECT_NE(u8.get(), u9.get());
}

TEST(ClipUnion, ValidityIsDecidedInTheOrderTheHeaderStates) {
  StoreHolder s, other;
  Ref a(intern_square(s.store, 0)), foreign(intern_square(other.store, 0));
  const GSHSCENE_Resource * out = nullptr;
  GSHSCENE_Clip_Member ok = member(a.get());
  EXPECT_EQ(gshscene_store_intern_clip_union(nullptr, &ok, 1, &out), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_store_intern_clip_union(s.store, nullptr, 1, &out), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_store_intern_clip_union(s.store, &ok, 1, nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_store_intern_clip_union(s.store, &ok, 0, &out), GSHSCENE_ERR_INVALID) << "the empty list";
  GSHSCENE_Result r;
  union_of(s.store, {member(nullptr)}, &r);
  EXPECT_EQ(r, GSHSCENE_ERR_INVALID);
  union_of(s.store, {member(foreign.get())}, &r);
  EXPECT_EQ(r, GSHSCENE_ERR_INVALID) << "another store's resource";
  Ref plane;
  ASSERT_EQ(gshscene_store_intern_plane(s.store, &plane.resource), GSHSCENE_OK);
  union_of(s.store, {member(plane.get())}, &r);
  EXPECT_EQ(r, GSHSCENE_ERR_INVALID) << "a plane";
  Ref inner(union_of(s.store, {member(a.get())}));
  union_of(s.store, {member(inner.get())}, &r);
  EXPECT_EQ(r, GSHSCENE_ERR_INVALID) << "unions do not nest";
  union_of(s.store, {member(a.get(), (GSHGEOM_Fill_Rule)7)}, &r);
  EXPECT_EQ(r, GSHSCENE_ERR_INVALID) << "a rule outside the enumeration";
  GSHGEOM_Map bad = translated(0, 0);
  bad.m[0][0] = std::nan("");
  union_of(s.store, {member(a.get(), GSHGEOM_FILL_NONZERO, &bad)}, &r);
  EXPECT_EQ(r, GSHSCENE_ERR_INVALID) << "an invalid map";
  GSHGEOM_Map three;
  gshgeom_map_identity(3, &three);
  union_of(s.store, {member(a.get(), GSHGEOM_FILL_NONZERO, &three)}, &r);
  EXPECT_EQ(r, GSHSCENE_ERR_UNSUPPORTED) << "a 3D map";
  // the order: a validity failure after an unsupported member still wins
  union_of(s.store, {member(a.get(), GSHGEOM_FILL_NONZERO, &three), member(nullptr)}, &r);
  EXPECT_EQ(r, GSHSCENE_ERR_INVALID);
  // a glyph run from a font is unsupported; from outlines it is a member
  Ref face(intern_face(s.store));
  Ref font_run(intern_font_run(s.store, face.get(), 12.0, {{1, 0, 0, nullptr}}));
  ASSERT_NE(font_run.get(), nullptr);
  union_of(s.store, {member(font_run.get())}, &r);
  EXPECT_EQ(r, GSHSCENE_ERR_UNSUPPORTED);
  GSHSCENE_Glyph g{7, 1.0, 2.0, a.get()};
  GSHSCENE_Glyph_Run run;
  std::memset(&run, 0, sizeof run);
  run.source = GSHSCENE_GLYPHS_FROM_OUTLINES;
  run.glyph_count = 1;
  run.glyphs = &g;
  Ref outline_run;
  ASSERT_EQ(gshscene_store_intern_glyph_run(s.store, &run, &outline_run.resource), GSHSCENE_OK);
  Ref u(union_of(s.store, {member(outline_run.get()), member(a.get())}, &r));
  EXPECT_EQ(r, GSHSCENE_OK);
  EXPECT_EQ(gshscene_resource_clip_union_count(u.get()), 2u);
  // a failing call leaves the store and the references as they were
  GSHSCENE_Store_Stats before, after;
  gshscene_store_stats(s.store, &before);
  size_t refs = gshscene_resource_refcount(a.get());
  union_of(s.store, {member(a.get()), member(plane.get())}, &r);
  gshscene_store_stats(s.store, &after);
  EXPECT_EQ(before.live, after.live);
  EXPECT_EQ(gshscene_resource_refcount(a.get()), refs);
}

TEST(ClipUnion, TheMemberLimitPassesAtTheCapAndFailsOnePast) {
  GSHSCENE_Limits limits{};
  limits.max_clip_members = 5;
  StoreHolder s(&limits);
  std::vector<Ref> sq;
  std::vector<GSHSCENE_Clip_Member> m;
  for (int i = 0; i < 6; i++) {
    sq.emplace_back(intern_square(s.store, 10.0 * i));
    m.push_back(member(sq.back().get()));
  }
  const GSHSCENE_Resource * out = nullptr;
  EXPECT_EQ(gshscene_store_intern_clip_union(s.store, m.data(), 5, &out), GSHSCENE_OK);
  gshscene_resource_release(out);
  EXPECT_EQ(gshscene_store_intern_clip_union(s.store, m.data(), 6, &out), GSHSCENE_ERR_LIMIT);
  // the count counted is the one given, before duplicates are removed
  std::vector<GSHSCENE_Clip_Member> dup(6, m[0]);
  EXPECT_EQ(gshscene_store_intern_clip_union(s.store, dup.data(), 6, &out), GSHSCENE_ERR_LIMIT);
  // the ceiling: exactly passes at creation, one more is invalid
  GSHSCENE_Limits ceil{};
  ceil.max_clip_members = GSHSCENE_CLIP_MEMBERS_CEILING;
  GSHSCENE_Store * st = nullptr;
  EXPECT_EQ(gshscene_store_create(nullptr, &ceil, &st), GSHSCENE_OK);
  gshscene_store_destroy(st);
  ceil.max_clip_members = GSHSCENE_CLIP_MEMBERS_CEILING + 1;
  EXPECT_EQ(gshscene_store_create(nullptr, &ceil, &st), GSHSCENE_ERR_INVALID);
  // the default is 256 and 0 is the default
  StoreHolder d;
  std::vector<Ref> many;
  std::vector<GSHSCENE_Clip_Member> big;
  for (int i = 0; i < 257; i++) {
    many.emplace_back(intern_square(d.store, 10.0 * i));
    big.push_back(member(many.back().get()));
  }
  EXPECT_EQ(gshscene_store_intern_clip_union(d.store, big.data(), 256, &out), GSHSCENE_OK);
  gshscene_resource_release(out);
  EXPECT_EQ(gshscene_store_intern_clip_union(d.store, big.data(), 257, &out), GSHSCENE_ERR_LIMIT);
}

TEST(ClipUnion, HashAndDumpAreRestatedFromTheText) {
  StoreHolder s;
  Ref a(intern_square(s.store, 0)), b(intern_square(s.store, 10));
  GSHGEOM_Map t = translated(2, 3);
  Ref u(union_of(s.store, {member(b.get(), GSHGEOM_FILL_EVEN_ODD, &t), member(a.get())}));
  ASSERT_NE(u.get(), nullptr);
  // the members in canonical order, each with the keys the text names
  struct K {
    uint64_t res, map;
    uint8_t rule;
    const GSHSCENE_Resource * r;
  };
  GSHGEOM_Map id;
  gshgeom_map_identity(2, &id);
  std::vector<K> ks = {{gshscene_resource_hash(b.get()), gshgeom_map_hash(&t), 1, b.get()},
      {gshscene_resource_hash(a.get()), gshgeom_map_hash(&id), 0, a.get()}};
  std::sort(ks.begin(), ks.end(), [](const K & x, const K & y) {
    return std::tie(x.res, x.map, x.rule) < std::tie(y.res, y.map, y.rule);
  });
  GSHGEOM_Hash h;
  gshgeom_hash_init(&h);
  gshgeom_hash_u32(&h, 1u);
  gshgeom_hash_u32(&h, 14u);
  gshgeom_hash_u64(&h, 2u);
  for (const K & k : ks) {
    gshgeom_hash_u64(&h, k.res);
    gshgeom_hash_u64(&h, k.map);
    gshgeom_hash_u8(&h, k.rule);
  }
  EXPECT_EQ(gshscene_resource_hash(u.get()), gshgeom_hash_value(&h));
  char want[512];
  std::string expect;
  snprintf(want, sizeof want, "members 2\nm 0 res=path:%016llx map=%016llx rule=%u\nm 1 res=path:%016llx map=%016llx rule=%u\n",
      (unsigned long long)ks[0].res, (unsigned long long)ks[0].map, ks[0].rule, (unsigned long long)ks[1].res,
      (unsigned long long)ks[1].map, ks[1].rule);
  std::string d = dump_of(u.get());
  EXPECT_NE(d.find(want), std::string::npos) << d;
  EXPECT_NE(d.find("clip-union"), std::string::npos);
}

TEST(ClipUnion, EachFieldOfTheContentChangesTheHash) {
  StoreHolder s;
  Ref a(intern_square(s.store, 0)), b(intern_square(s.store, 10)), c(intern_square(s.store, 20));
  GSHGEOM_Map t = translated(1, 0), t2 = translated(2, 0);
  std::vector<std::vector<GSHSCENE_Clip_Member>> variants = {
      {member(a.get()), member(b.get())},
      {member(a.get()), member(b.get()), member(c.get())},                       // the count
      {member(a.get()), member(c.get())},                                        // a resource
      {member(a.get(), GSHGEOM_FILL_EVEN_ODD), member(b.get())},                 // a rule
      {member(a.get(), GSHGEOM_FILL_NONZERO, &t), member(b.get())},              // a map
      {member(a.get(), GSHGEOM_FILL_NONZERO, &t2), member(b.get())},             // another map
      {member(a.get())},                                                         // fewer
  };
  std::vector<uint64_t> hashes;
  for (auto & v : variants) {
    Ref u(union_of(s.store, v));
    ASSERT_NE(u.get(), nullptr);
    hashes.push_back(gshscene_resource_hash(u.get()));
  }
  std::sort(hashes.begin(), hashes.end());
  EXPECT_EQ(std::adjacent_find(hashes.begin(), hashes.end()), hashes.end());
}

TEST(ClipUnion, AccountedBytesAreTheBaseAndTheMembers) {
  StoreHolder s;
  Ref a(intern_square(s.store, 0)), b(intern_square(s.store, 10)), c(intern_square(s.store, 20));
  GSHSCENE_Store_Stats before, after;
  gshscene_store_stats(s.store, &before);
  Ref u(union_of(s.store, {member(a.get()), member(b.get()), member(c.get())}));
  gshscene_store_stats(s.store, &after);
  EXPECT_EQ(after.bytes - before.bytes, GSHSCENE_BYTES_BASE + 3 * GSHSCENE_BYTES_PER_CLIP_MEMBER);
  EXPECT_GE(GSHSCENE_BYTES_PER_CLIP_MEMBER, sizeof(GSHSCENE_Clip_Member));
  // the store really takes no more than it accounts: a counting allocator sees the member array
  tracker::Tracker t;
  GSHSCENE_Allocator alloc = tracker::allocator_for(&t);
  GSHSCENE_Store * st = nullptr;
  ASSERT_EQ(gshscene_store_create(&alloc, nullptr, &st), GSHSCENE_OK);
  Ref a2(intern_square(st, 0)), b2(intern_square(st, 10));
  size_t live_before = t.live.size();
  Ref u2(union_of(st, {member(a2.get()), member(b2.get())}));
  EXPECT_GT(t.live.size(), live_before);
  u2 = Ref();
  a2 = Ref();
  b2 = Ref();
  gshscene_store_destroy(st);
  EXPECT_TRUE(t.live.empty());
}

TEST(ClipUnion, EveryAllocationOfAnInternCanFail) {
  for (long fail = 0; fail < 64; fail++) {
    tracker::Tracker t;
    GSHSCENE_Allocator alloc = tracker::allocator_for(&t);
    GSHSCENE_Store * store = nullptr;
    ASSERT_EQ(gshscene_store_create(&alloc, nullptr, &store), GSHSCENE_OK);
    Ref a(intern_square(store, 0)), b(intern_square(store, 10));
    size_t refs = gshscene_resource_refcount(a.get());
    t.calls = 0;
    t.fail_from = fail;
    GSHSCENE_Clip_Member m[2] = {member(a.get()), member(b.get())};
    const GSHSCENE_Resource * r = nullptr;
    GSHSCENE_Result result = gshscene_store_intern_clip_union(store, m, 2, &r);
    t.fail_from = -1;
    if (result == GSHSCENE_OK) {
      EXPECT_FALSE(t.failed) << fail;
      gshscene_resource_release(r);
    } else {
      EXPECT_EQ(result, GSHSCENE_ERR_OOM) << fail;
      EXPECT_TRUE(t.failed) << fail;
      EXPECT_EQ(gshscene_resource_refcount(a.get()), refs) << "no reference taken on failure, " << fail;
    }
    a = Ref();
    b = Ref();
    gshscene_store_destroy(store);
    EXPECT_TRUE(t.live.empty()) << fail;
    if (result == GSHSCENE_OK) {
      break;
    }
  }
}

TEST(ClipUnion, AClipPushTakesAUnionAndStoresTheRuleAsNonzero) {
  StoreHolder s;
  Ref a(intern_square(s.store, 0)), b(intern_square(s.store, 10));
  Ref u(union_of(s.store, {member(a.get()), member(b.get(), GSHGEOM_FILL_EVEN_ODD)}));
  ListHolder l1(s.store), l2(s.store);
  ASSERT_EQ(gshscene_list_clip_push(l1.list, u.get(), nullptr, GSHGEOM_FILL_NONZERO, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_clip_pop(l1.list), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_clip_push(l2.list, u.get(), nullptr, GSHGEOM_FILL_EVEN_ODD, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_clip_pop(l2.list), GSHSCENE_OK);
  gshscene_list_close(l1.list);
  gshscene_list_close(l2.list);
  EXPECT_TRUE(gshscene_list_equal(l1.list, l2.list)) << "the item's rule is ignored: one spelling";
  EXPECT_EQ(gshscene_list_hash(l1.list), gshscene_list_hash(l2.list));
  std::string d = list_dump(l1.list);
  char want[128];
  snprintf(want, sizeof want, "C res=clip-union:%016llx", (unsigned long long)gshscene_resource_hash(u.get()));
  EXPECT_NE(d.find(want), std::string::npos) << d;
  EXPECT_NE(d.find(" rule=0 "), std::string::npos) << d;
  // a union is the resource of a clip push and of nothing else
  ListHolder l3(s.store);
  EXPECT_EQ(gshscene_list_fill(l3.list, u.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_instance(l3.list, u.get(), nullptr, 0, 0), GSHSCENE_ERR_INVALID);
}

TEST(ClipUnion, AUnionOfOneMemberIsRecordedAsTheSingleClip) {
  StoreHolder s;
  Ref a(intern_square(s.store, 0));
  GSHGEOM_Map mm = translated(3, 4), im;
  gshgeom_map_scale(2, 2, 2, 1, &im);
  Ref u(union_of(s.store, {member(a.get(), GSHGEOM_FILL_EVEN_ODD, &mm)}));
  ListHolder via_union(s.store), plain(s.store), plain_identity(s.store);
  ASSERT_EQ(gshscene_list_clip_push(via_union.list, u.get(), &im, GSHGEOM_FILL_NONZERO, 2, 1.5), GSHSCENE_OK);
  gshscene_list_clip_pop(via_union.list);
  GSHGEOM_Map composed;
  ASSERT_EQ(gshgeom_map_compose(&im, &mm, &composed), GSHGEOM_OK);
  ASSERT_EQ(gshscene_list_clip_push(plain.list, a.get(), &composed, GSHGEOM_FILL_EVEN_ODD, 2, 1.5), GSHSCENE_OK);
  gshscene_list_clip_pop(plain.list);
  gshscene_list_close(via_union.list);
  gshscene_list_close(plain.list);
  EXPECT_TRUE(gshscene_list_equal(via_union.list, plain.list));
  EXPECT_EQ(gshscene_list_hash(via_union.list), gshscene_list_hash(plain.list));
  EXPECT_EQ(list_dump(via_union.list), list_dump(plain.list));
  const GSHSCENE_Item * item = nullptr;
  ASSERT_EQ(gshscene_list_item(via_union.list, 0, &item), GSHSCENE_OK);
  EXPECT_EQ(item->resource, a.get()) << "the list never contains a one-member union";
  // with an identity item map the member's map is copied bit for bit
  ASSERT_EQ(gshscene_list_clip_push(plain_identity.list, u.get(), nullptr, GSHGEOM_FILL_NONZERO, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_item(plain_identity.list, 0, &item), GSHSCENE_OK);
  EXPECT_EQ(std::memcmp(&item->map.m, &mm.m, sizeof mm.m), 0);
  EXPECT_EQ(std::memcmp(&item->map.t, &mm.t, sizeof mm.t), 0);
  EXPECT_EQ(item->fill_rule, GSHGEOM_FILL_EVEN_ODD);
}

TEST(ClipUnion, TheMemberItemIsTheSingleClipOfTheMember) {
  StoreHolder s;
  Ref a(intern_square(s.store, 0)), b(intern_square(s.store, 10));
  GSHGEOM_Map mb = translated(1, 1), item_map;
  gshgeom_map_scale(2, 3, 3, 1, &item_map);
  Ref u(union_of(s.store, {member(a.get()), member(b.get(), GSHGEOM_FILL_EVEN_ODD, &mb)}));
  ListHolder l(s.store);
  ASSERT_EQ(gshscene_list_clip_push(l.list, u.get(), &item_map, GSHGEOM_FILL_NONZERO, 5, 2.0), GSHSCENE_OK);
  const GSHSCENE_Item * item = nullptr;
  ASSERT_EQ(gshscene_list_item(l.list, 0, &item), GSHSCENE_OK);
  for (size_t i = 0; i < 2; i++) {
    GSHSCENE_Clip_Member m;
    gshscene_resource_clip_union_member(u.get(), i, &m);
    GSHSCENE_Item out;
    ASSERT_EQ(gshscene_clip_union_member_item(item, i, &out), GSHSCENE_OK);
    EXPECT_EQ(out.kind, GSHSCENE_ITEM_CLIP_PUSH);
    EXPECT_EQ(out.resource, m.resource);
    EXPECT_EQ(out.fill_rule, m.rule);
    EXPECT_EQ(out.layer, 5);
    EXPECT_EQ(out.depth, 2.0);
    GSHGEOM_Map want;
    ASSERT_EQ(gshgeom_map_compose(&item_map, &m.map, &want), GSHGEOM_OK);
    EXPECT_EQ(gshgeom_map_hash(&out.map), gshgeom_map_hash(&want)) << "item map after member map, once";
  }
  GSHSCENE_Item out;
  EXPECT_EQ(gshscene_clip_union_member_item(item, 2, &out), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_clip_union_member_item(nullptr, 0, &out), GSHSCENE_ERR_INVALID);
}


/** A retained group whose clip is @p clip under @p clip_map; the list the scene emits. */
struct GroupDoc {
  SceneHolder s;
  VersionRef v;
  uint64_t group = 0;
  GroupDoc(const GSHSCENE_Resource * clip, const GSHGEOM_Map & group_map, const GSHGEOM_Map & clip_map, GSHGEOM_Fill_Rule rule,
      const GSHSCENE_Resource * shape, GSHSCENE_Store * store = nullptr) : s(nullptr, nullptr, store) {
    EditHolder e(s);
    GSHSCENE_Node gn = group_node(1.0, 2);
    gn.map = group_map;
    gn.clip = clip;
    gn.clip_map = clip_map;
    gn.clip_rule = rule;
    group = add(e.edit, GSHSCENE_ROOT_ID, gn);
    add(e.edit, group, shape_node(shape, 3));
    publish(s, e, &v);
  }
};

TEST(ClipUnionScene, AGroupsClipMayBeAUnionAndEmissionRecordsItAsAClipPush) {
  SceneHolder base;
  GSHSCENE_Store * store = base.store();
  Ref a(intern_square(store, 0)), b(intern_square(store, 10)), shape(intern_square(store, 20));
  Ref u(union_of(store, {member(a.get()), member(b.get(), GSHGEOM_FILL_EVEN_ODD)}));
  GSHGEOM_Map gm = translated(1, 2), cm = translated(0.5, 0.25);
  // A union is a region of a clip and of nothing else: a shape or a stroke node that names one is refused.
  {
    EditHolder e(base);
    uint64_t id = 0;
    GSHSCENE_Node shape_of_union = shape_node(u.get(), 0);
    EXPECT_EQ(gshscene_edit_add(e.edit, GSHSCENE_ROOT_ID, GSHSCENE_APPEND, &shape_of_union, nullptr, &id), GSHSCENE_ERR_INVALID);
  }
  // The rule given to a union clip is ignored (the members carry theirs): two spellings are one node.
  GroupDoc even(u.get(), gm, cm, GSHGEOM_FILL_EVEN_ODD, shape.get(), store);
  GroupDoc nonzero(u.get(), gm, cm, GSHGEOM_FILL_NONZERO, shape.get(), store);
  const GSHSCENE_List * l1 = nullptr;
  const GSHSCENE_List * l2 = nullptr;
  ASSERT_EQ(gshscene_version_list(even.v.get(), &l1), GSHSCENE_OK);
  ASSERT_EQ(gshscene_version_list(nonzero.v.get(), &l2), GSHSCENE_OK);
  EXPECT_TRUE(gshscene_list_equal(l1, l2));
  EXPECT_EQ(gshscene_version_hash(even.v.get()), gshscene_version_hash(nonzero.v.get())) << "the node stores a union clip's rule as nonzero (CU11)";
  // The same list recorded by hand: the clip push of the union under group map . clip map, the group's layer.
  ListHolder want(store);
  GSHGEOM_Map composed;
  ASSERT_EQ(gshgeom_map_compose(&gm, &cm, &composed), GSHGEOM_OK);
  ASSERT_EQ(gshscene_list_clip_push(want.list, u.get(), &composed, GSHGEOM_FILL_NONZERO, 2, 0), GSHSCENE_OK);
  GSHSCENE_Paint black = solid(0, 0, 0, 1);
  ASSERT_EQ(gshscene_list_fill(want.list, shape.get(), &gm, GSHGEOM_FILL_NONZERO, &black, 3, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_clip_pop(want.list), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(want.list), GSHSCENE_OK);
  EXPECT_TRUE(gshscene_list_equal(l1, want.list)) << list_dump(l1) << "\n---\n" << list_dump(want.list);
  EXPECT_EQ(gshscene_list_hash(l1), gshscene_list_hash(want.list));
}

TEST(ClipUnionScene, AUnionOfOneMemberAsAGroupsClipEmitsTheSingleClipButTheNodesDiffer) {
  SceneHolder base;
  GSHSCENE_Store * store = base.store();
  Ref a(intern_square(store, 0)), shape(intern_square(store, 20));
  GSHGEOM_Map mm = translated(3, 4), gm = translated(1, 2), cm = translated(0.5, 0.25);
  Ref u(union_of(store, {member(a.get(), GSHGEOM_FILL_EVEN_ODD, &mm)}));
  GroupDoc via(u.get(), gm, cm, GSHGEOM_FILL_NONZERO, shape.get(), store);
  GSHGEOM_Map placed;
  ASSERT_EQ(gshgeom_map_compose(&cm, &mm, &placed), GSHGEOM_OK);
  GroupDoc plain(a.get(), gm, placed, GSHGEOM_FILL_EVEN_ODD, shape.get(), store);
  const GSHSCENE_List * l1 = nullptr;
  const GSHSCENE_List * l2 = nullptr;
  ASSERT_EQ(gshscene_version_list(via.v.get(), &l1), GSHSCENE_OK);
  ASSERT_EQ(gshscene_version_list(plain.v.get(), &l2), GSHSCENE_OK);
  EXPECT_TRUE(gshscene_list_equal(l1, l2)) << "equal emitted lists (CU10)";
  EXPECT_EQ(gshscene_list_hash(l1), gshscene_list_hash(l2));
  EXPECT_NE(gshscene_version_hash(via.v.get()), gshscene_version_hash(plain.v.get())) << "the nodes differ (CU11)";
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
