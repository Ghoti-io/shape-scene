/**
 * @file
 *
 * The disjointness predicate and the planning pass's use of it (clip_union.h CU12 to CU17; sink.h S24): touching is disjoint, a
 * positive overlap by one lattice unit is not, the first pair and the unprovable member are named in canonical indices, the
 * comparisons counted are at most n (n - 1) / 2, the chain of maps decides, and the reasons come in their order.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "../scene_fixtures.h"
#include "../scene_helpers.h"

#include <cmath>
#include <cstring>
#include <vector>

using namespace scene;

namespace {

/** A rectangle path [x0, x1] x [y0, y1]. */
GSHGEOM_Path * rect(double x0, double y0, double x1, double y1) {
  GSHGEOM_Path * p = new_path();
  gshgeom_path_move_to(p, P(x0, y0));
  gshgeom_path_line_to(p, P(x1, y0));
  gshgeom_path_line_to(p, P(x1, y1));
  gshgeom_path_line_to(p, P(x0, y1));
  gshgeom_path_close(p);
  return p;
}

/** Owns the resources and the list of one union clip push, so that the item outlives the call. */
struct Fixture {
  StoreHolder store;
  std::vector<Ref> refs;
  Ref u;
  std::unique_ptr<ListHolder> list;
  const GSHSCENE_Item * item = nullptr;
  GSHSCENE_Item manual;  // a one-member union is never in a list (CU10): the predicate is still defined for its item

  const GSHSCENE_Resource * add(double x0, double y0, double x1, double y1) {
    GSHGEOM_Path * p = rect(x0, y0, x1, y1);
    refs.emplace_back(intern(store.store, p));
    gshgeom_path_destroy(p);
    return refs.back().get();
  }

  bool make(std::vector<GSHSCENE_Clip_Member> members, const GSHGEOM_Map * item_map = nullptr) {
    if (gshscene_store_intern_clip_union(store.store, members.data(), members.size(), &u.resource) != GSHSCENE_OK) {
      return false;
    }
    list.reset(new ListHolder(store.store));
    if (gshscene_resource_clip_union_count(u.get()) == 1) {
      std::memset(&manual, 0, sizeof manual);
      manual.kind = GSHSCENE_ITEM_CLIP_PUSH;
      manual.resource = u.get();
      gshgeom_map_identity(2, &manual.map);
      item = &manual;
      return true;
    }
    if (gshscene_list_clip_push(list->list, u.get(), item_map, GSHGEOM_FILL_NONZERO, 0, 0) != GSHSCENE_OK) {
      return false;
    }
    return gshscene_list_item(list->list, 0, &item) == GSHSCENE_OK;
  }
};

GSHSCENE_Clip_Member mem(const GSHSCENE_Resource * r, GSHGEOM_Fill_Rule rule = GSHGEOM_FILL_NONZERO) {
  GSHSCENE_Clip_Member m;
  std::memset(&m, 0, sizeof m);
  m.resource = r;
  m.rule = rule;
  return m;
}

GSHSCENE_Union_Verdict verdict_of(const Fixture & f, const GSHGEOM_Map * inst = nullptr, size_t n = 0,
    const GSHGEOM_Map * target = nullptr) {
  GSHSCENE_Union_Verdict v;
  EXPECT_EQ(gshscene_clip_union_disjoint(f.item, inst, n, target, &v), GSHSCENE_OK);
  return v;
}

/** The index of the member whose resource is @p r in the canonical order. */
size_t canon_index(const Fixture & f, const GSHSCENE_Resource * r) {
  for (size_t i = 0; i < gshscene_resource_clip_union_count(f.u.get()); i++) {
    GSHSCENE_Clip_Member m;
    gshscene_resource_clip_union_member(f.u.get(), i, &m);
    if (m.resource == r) {
      return i;
    }
  }
  return (size_t)-1;
}

GSHSCENE_Sink_Caps caps_with(uint32_t flags) {
  GSHSCENE_Sink_Caps c;
  std::memset(&c, 0, sizeof c);
  for (uint32_t k = GSHSCENE_ITEM_FILL; k < GSHSCENE_ITEM_KIND_COUNT; k++) {
    c.item_kinds |= UINT32_C(1) << k;
  }
  c.flags = flags;
  return c;
}

}  // namespace

TEST(ClipUnionPlan, TouchingBoxesAreDisjointAndAPositiveOverlapIsNot) {
  Fixture f;
  const GSHSCENE_Resource * a = f.add(0, 0, 3.5, 4);
  const GSHSCENE_Resource * b = f.add(3.5, 0, 8, 4);  // shares the edge x = 3.5
  ASSERT_TRUE(f.make({mem(a), mem(b)}));
  GSHSCENE_Union_Verdict v = verdict_of(f);
  EXPECT_EQ(v.disjoint, 1);
  EXPECT_EQ(v.tests, 1u);
  // a shared corner
  Fixture g;
  ASSERT_TRUE(g.make({mem(g.add(0, 0, 2, 2)), mem(g.add(2, 2, 5, 5))}));
  EXPECT_EQ(verdict_of(g).disjoint, 1);
  // overlap by one lattice unit (1/64) on x
  Fixture h;
  const GSHSCENE_Resource * c = h.add(0, 0, 3.5, 4);
  const GSHSCENE_Resource * d = h.add(3.5 - 1.0 / 64, 0, 8, 4);
  ASSERT_TRUE(h.make({mem(c), mem(d)}));
  v = verdict_of(h);
  EXPECT_EQ(v.disjoint, 0);
  size_t ic = canon_index(h, c), id = canon_index(h, d);
  EXPECT_EQ(v.first, std::min(ic, id));
  EXPECT_EQ(v.second, std::max(ic, id));
  // a gap of one lattice unit is disjoint
  Fixture k;
  ASSERT_TRUE(k.make({mem(k.add(0, 0, 3.5, 4)), mem(k.add(3.5 + 1.0 / 64, 0, 8, 4))}));
  EXPECT_EQ(verdict_of(k).disjoint, 1);
  // overlap on y only is no overlap of interiors when x does not meet
  Fixture m;
  ASSERT_TRUE(m.make({mem(m.add(0, 0, 3, 4)), mem(m.add(5, 1, 8, 3))}));
  EXPECT_EQ(verdict_of(m).disjoint, 1);
  // abutting along y with the same x range: the interiors do not meet, whichever axis touches
  Fixture st;
  ASSERT_TRUE(st.make({mem(st.add(0, 0, 3, 4)), mem(st.add(0, 4, 3, 8))}));
  EXPECT_EQ(verdict_of(st).disjoint, 1);
  Fixture sx;
  ASSERT_TRUE(sx.make({mem(sx.add(0, 0, 3, 4)), mem(sx.add(3, 0, 6, 4))}));
  EXPECT_EQ(verdict_of(sx).disjoint, 1);
  // nested, and identical maps with different rules
  Fixture n;
  const GSHSCENE_Resource * outer = n.add(0, 0, 10, 10);
  ASSERT_TRUE(n.make({mem(outer), mem(n.add(2, 2, 4, 4))}));
  EXPECT_EQ(verdict_of(n).disjoint, 0);
  Fixture r;
  const GSHSCENE_Resource * same = r.add(0, 0, 10, 10);
  ASSERT_TRUE(r.make({mem(same, GSHGEOM_FILL_NONZERO), mem(same, GSHGEOM_FILL_EVEN_ODD)}));
  EXPECT_EQ(verdict_of(r).disjoint, 0);
}

TEST(ClipUnionPlan, ComparisonsAreCountedAtMostNChooseTwoAndTheWalkStopsAtTheFirstOverlap) {
  for (size_t n : {1u, 2u, 3u, 256u, 1024u}) {
    GSHSCENE_Limits limits{};
    limits.max_clip_members = 1024;
    Fixture f;
    f.store.~StoreHolder();
    new (&f.store) StoreHolder(&limits);
    std::vector<GSHSCENE_Clip_Member> members;
    for (size_t i = 0; i < n; i++) {
      members.push_back(mem(f.add(10.0 * (double)i, 0, 10.0 * (double)i + 4, 4)));
    }
    ASSERT_TRUE(f.make(members)) << n;
    GSHSCENE_Union_Verdict v = verdict_of(f);
    EXPECT_EQ(v.disjoint, 1) << n;
    EXPECT_EQ(v.tests, (uint64_t)n * (n - 1) / 2) << n;
  }
  // an overlap stops the walk: the first pair in canonical (i, j) order, with fewer tests than all
  Fixture f;
  std::vector<const GSHSCENE_Resource *> rs;
  for (int i = 0; i < 8; i++) {
    rs.push_back(f.add(10.0 * i, 0, 10.0 * i + 4, 4));
  }
  const GSHSCENE_Resource * clash = f.add(31, 1, 33, 3);  // overlaps rs[3] = [30, 34]
  std::vector<GSHSCENE_Clip_Member> members;
  for (auto r : rs) {
    members.push_back(mem(r));
  }
  members.push_back(mem(clash));
  ASSERT_TRUE(f.make(members));
  GSHSCENE_Union_Verdict v = verdict_of(f);
  EXPECT_EQ(v.disjoint, 0);
  size_t ia = canon_index(f, rs[3]), ib = canon_index(f, clash);
  // the first pair examined in (i ascending, j ascending) order among the failing pairs: only one pair fails
  EXPECT_EQ(v.first, std::min(ia, ib));
  EXPECT_EQ(v.second, std::max(ia, ib));
  EXPECT_LT(v.tests, 9u * 8u / 2u + 1u);
  EXPECT_GE(v.tests, 1u);
}

TEST(ClipUnionPlan, TheWalkStopsAtTheFirstFailingPairInCanonicalOrder) {
  Fixture f;
  ASSERT_TRUE(f.make({mem(f.add(0, 0, 10, 10)), mem(f.add(2, 2, 12, 12)), mem(f.add(4, 4, 14, 14))}));
  GSHSCENE_Union_Verdict v = verdict_of(f);
  EXPECT_EQ(v.disjoint, 0);
  EXPECT_EQ(v.first, 0u);
  EXPECT_EQ(v.second, 1u);
  EXPECT_EQ(v.tests, 1u) << "the first comparison fails, and nothing is compared after it";
}

TEST(ClipUnionPlan, TheChainOfMapsDecidesNotTheModelSpace) {
  Fixture f;
  ASSERT_TRUE(f.make({mem(f.add(0, 0, 1, 1)), mem(f.add(2, 0, 3, 1))}));
  EXPECT_EQ(verdict_of(f).disjoint, 1);
  // a shear makes the boxes overlap (the parallelograms do not): an instance map
  GSHGEOM_Map shear;
  const double m[4] = {1, 2, 0, 1}, t[2] = {0, 0};
  ASSERT_EQ(gshgeom_map_from_matrix(2, m, t, &shear), GSHGEOM_OK);
  EXPECT_EQ(verdict_of(f, &shear, 1).disjoint, 0) << "an instance map";
  EXPECT_EQ(verdict_of(f, nullptr, 0, &shear).disjoint, 0) << "the target map";
  // the item's own map
  Fixture g;
  ASSERT_TRUE(g.make({mem(g.add(0, 0, 1, 1)), mem(g.add(2, 0, 3, 1))}, &shear));
  EXPECT_EQ(verdict_of(g).disjoint, 0) << "the item's map";
  // the same through the member item
  GSHSCENE_Item mi;
  ASSERT_EQ(gshscene_clip_union_member_item(g.item, 0, &mi), GSHSCENE_OK);
  GSHGEOM_Device_Box b0;
  ASSERT_EQ(gshgeom_device_box(gshscene_resource_path(mi.resource), &mi.map, &b0), GSHGEOM_OK);
  EXPECT_NE(b0.x1, 64) << "sheared";
  // a scale to a tiny size keeps them disjoint; a singular map collapses them to no area: degenerate, disjoint
  GSHGEOM_Map flat;
  const double fm[4] = {0, 0, 0, 1}, ft[2] = {0, 0};
  ASSERT_EQ(gshgeom_map_from_matrix(2, fm, ft, &flat), GSHGEOM_OK);
  EXPECT_EQ(verdict_of(f, &flat, 1).disjoint, 1);
}

TEST(ClipUnionPlan, DegenerateMembersNeverCauseARefusalAndAnUnprovableOneIsNamed) {
  Fixture f;
  GSHGEOM_Path * empty = new_path();
  Ref e(intern(f.store.store, empty));
  gshgeom_path_destroy(empty);
  GSHGEOM_Path * line = new_path();
  gshgeom_path_move_to(line, P(1, 1));
  gshgeom_path_line_to(line, P(5, 1));
  Ref l(intern(f.store.store, line));
  gshgeom_path_destroy(line);
  const GSHSCENE_Resource * a = f.add(0, 0, 6, 6);
  ASSERT_TRUE(f.make({mem(e.get()), mem(l.get()), mem(a), mem(f.add(1, 1, 5, 5))}));
  GSHSCENE_Union_Verdict v = verdict_of(f);
  EXPECT_EQ(v.disjoint, 0) << "the two squares nest";
  size_t ia = canon_index(f, a);
  size_t ib = canon_index(f, f.refs.back().get());
  EXPECT_EQ(v.first, std::min(ia, ib));
  // the empty and the zero-width members alone: disjoint
  Fixture g;
  GSHGEOM_Path * e2 = new_path();
  Ref e2r(intern(g.store.store, e2));
  gshgeom_path_destroy(e2);
  GSHGEOM_Path * l2 = new_path();
  gshgeom_path_move_to(l2, P(1, 1));
  gshgeom_path_line_to(l2, P(5, 1));
  Ref l2r(intern(g.store.store, l2));
  gshgeom_path_destroy(l2);
  ASSERT_TRUE(g.make({mem(e2r.get()), mem(l2r.get()), mem(g.add(0, 0, 3, 3))}));
  EXPECT_EQ(verdict_of(g).disjoint, 1);
  // a member past 26.6 under the chain is unprovable: (i, i), whichever canonical index it has
  bool saw_nonzero = false;
  for (int k = 0; k < 8; k++) {
    Fixture w;
    const GSHSCENE_Resource * p1 = w.add(0, 0, 1, 1);
    const GSHSCENE_Resource * p2 = w.add(5, 0, 6, 1);
    const GSHSCENE_Resource * far = w.add(0, 0, 1e12 + k, 1 + k);
    ASSERT_TRUE(w.make({mem(p1), mem(p2), mem(far)}));
    v = verdict_of(w);
    EXPECT_EQ(v.disjoint, 0);
    size_t i = canon_index(w, far);
    EXPECT_EQ(v.first, i);
    EXPECT_EQ(v.second, i) << "an unprovable member is its pair (i, i)";
    saw_nonzero = saw_nonzero || i > 0;
  }
  EXPECT_TRUE(saw_nonzero);
}

TEST(ClipUnionPlan, AGlyphRunFromOutlinesHasTheHullOfItsGlyphs) {
  Fixture f;
  const GSHSCENE_Resource * g0 = f.add(0, 0, 2, 2);
  GSHSCENE_Glyph glyphs[2] = {{1, 0.0, 0.0, g0}, {2, 10.0, 0.0, g0}};
  GSHSCENE_Glyph_Run run;
  std::memset(&run, 0, sizeof run);
  run.source = GSHSCENE_GLYPHS_FROM_OUTLINES;
  run.glyph_count = 2;
  run.glyphs = glyphs;
  Ref r;
  ASSERT_EQ(gshscene_store_intern_glyph_run(f.store.store, &run, &r.resource), GSHSCENE_OK);
  // the hull of the run covers x in [0, 12]: a rect at 5..6 is inside the hull, so the pair overlaps (boxes are coarse)
  ASSERT_TRUE(f.make({mem(r.get()), mem(f.add(5, 0, 6, 2))}));
  EXPECT_EQ(verdict_of(f).disjoint, 0);
  Fixture g;
  const GSHSCENE_Resource * o = g.add(0, 0, 2, 2);
  GSHSCENE_Glyph gl[2] = {{1, 0.0, 0.0, o}, {2, 10.0, 0.0, o}};
  run.glyphs = gl;
  Ref r2;
  ASSERT_EQ(gshscene_store_intern_glyph_run(g.store.store, &run, &r2.resource), GSHSCENE_OK);
  ASSERT_TRUE(g.make({mem(r2.get()), mem(g.add(12, 0, 15, 2))}));
  EXPECT_EQ(verdict_of(g).disjoint, 1) << "touching the hull at x = 12";
}

TEST(ClipUnionPlan, TheDispositionRefusesInTheStatedOrder) {
  Fixture f;
  const GSHSCENE_Resource * a = f.add(0, 0, 4, 4);
  const GSHSCENE_Resource * b = f.add(3, 3, 8, 8);
  ASSERT_TRUE(f.make({mem(a), mem(b)}));
  const uint32_t base = GSHSCENE_CAP_CLIP_AA;
  GSHSCENE_Sink_Caps none = caps_with(base);
  GSHSCENE_Sink_Caps fast = caps_with(base | GSHSCENE_CAP_CLIP_UNION);
  GSHSCENE_Sink_Caps general = caps_with(base | GSHSCENE_CAP_CLIP_UNION | GSHSCENE_CAP_CLIP_UNION_GENERAL);
  GSHSCENE_Disposition d = gshscene_caps_disposition(&none, f.item, nullptr, 0, nullptr, nullptr, nullptr);
  EXPECT_EQ(d.entry_class, GSHSCENE_ENTRY_REFUSED);
  EXPECT_EQ(d.reason, GSHSCENE_REASON_CLIP_UNION);
  d = gshscene_caps_disposition(&fast, f.item, nullptr, 0, nullptr, nullptr, nullptr);
  EXPECT_EQ(d.entry_class, GSHSCENE_ENTRY_REFUSED);
  EXPECT_EQ(d.reason, GSHSCENE_REASON_UNION_OVERLAP);
  GSHSCENE_Union_Verdict v = verdict_of(f);
  EXPECT_EQ(d.detail, (v.first << 16) | v.second);
  // a pair whose first index is not 0, so that the encoding of both halves is seen
  bool saw_nonzero_first = false;
  for (int k = 0; k < 8; k++) {
    Fixture g;
    ASSERT_TRUE(g.make({mem(g.add(100 + 10 * k, 0, 104 + 10 * k, 4)), mem(g.add(0, 0, 4, 4)), mem(g.add(3, 3, 8, 8))}));
    GSHSCENE_Union_Verdict gv = verdict_of(g);
    GSHSCENE_Disposition gd = gshscene_caps_disposition(&fast, g.item, nullptr, 0, nullptr, nullptr, nullptr);
    EXPECT_EQ(gd.detail, (gv.first << 16) | gv.second);
    saw_nonzero_first = saw_nonzero_first || gv.first > 0;
  }
  EXPECT_TRUE(saw_nonzero_first);
  d = gshscene_caps_disposition(&general, f.item, nullptr, 0, nullptr, nullptr, nullptr);
  EXPECT_EQ(d.entry_class, 0);
  // GENERAL is not implied by CLIP_UNION
  GSHSCENE_Sink_Caps only_general = caps_with(base | GSHSCENE_CAP_CLIP_UNION_GENERAL);
  d = gshscene_caps_disposition(&only_general, f.item, nullptr, 0, nullptr, nullptr, nullptr);
  EXPECT_EQ(d.reason, GSHSCENE_REASON_CLIP_UNION) << "the disposition reads CLIP_UNION first; a sink with only GENERAL is invalid at creation";
  // a disjoint union is drawn by the fast tier; without CLIP_AA it is approximated once, after its refusals
  Fixture g;
  ASSERT_TRUE(g.make({mem(g.add(0, 0, 3, 3)), mem(g.add(3, 0, 6, 3))}));
  d = gshscene_caps_disposition(&fast, g.item, nullptr, 0, nullptr, nullptr, nullptr);
  EXPECT_EQ(d.entry_class, 0);
  GSHSCENE_Sink_Caps aliased = caps_with(GSHSCENE_CAP_CLIP_UNION);
  d = gshscene_caps_disposition(&aliased, g.item, nullptr, 0, nullptr, nullptr, nullptr);
  EXPECT_EQ(d.entry_class, GSHSCENE_ENTRY_APPROXIMATED);
  EXPECT_EQ(d.reason, GSHSCENE_REASON_CLIP_ALIASED);
  GSHSCENE_Sink_Caps neither = caps_with(0);
  d = gshscene_caps_disposition(&neither, f.item, nullptr, 0, nullptr, nullptr, nullptr);
  EXPECT_EQ(d.entry_class, GSHSCENE_ENTRY_REFUSED) << "a refusal comes before the approximation";
  EXPECT_EQ(d.reason, GSHSCENE_REASON_CLIP_UNION);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
