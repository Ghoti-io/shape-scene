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
 * Picking (pick.h, PK1 to PK12): the answers against fills, clips, groups, instances, unions with inner clips and strokes; the order and
 * the truncation; the resolution to nodes of a version; the arguments; and a randomized comparison with a reference that knows only
 * rectangles on the 26.6 lattice (every region exact, so the reference is the definition).
 */

#include "scene_fixtures.h"

#include "../alloc_tracker.h"

#include <cmath>
#include <cstdlib>
#include <functional>
#include <random>
#include <tuple>

using namespace scene;

namespace {

struct PickHolder {
  GSHSCENE_Pick * pick = nullptr;
  PickHolder() = default;
  ~PickHolder() { gshscene_pick_destroy(pick); }
  PickHolder(const PickHolder &) = delete;
  PickHolder & operator=(const PickHolder &) = delete;
};

GSHGEOM_Map identity() {
  GSHGEOM_Map m;
  gshgeom_map_identity(2, &m);
  return m;
}

GSHGEOM_Map scaled(double sx, double sy) {
  GSHGEOM_Map m;
  gshgeom_map_scale(2, sx, sy, 1.0, &m);
  return m;
}

GSHGEOM_Path * rect(double x0, double y0, double x1, double y1) {
  GSHGEOM_Path * p = new_path();
  gshgeom_path_move_to(p, P(x0, y0));
  gshgeom_path_line_to(p, P(x1, y0));
  gshgeom_path_line_to(p, P(x1, y1));
  gshgeom_path_line_to(p, P(x0, y1));
  gshgeom_path_close(p);
  return p;
}

Ref rect_res(GSHSCENE_Store * s, double x0, double y0, double x1, double y1) {
  GSHGEOM_Path * p = rect(x0, y0, x1, y1);
  Ref r(intern(s, p));
  gshgeom_path_destroy(p);
  return r;
}

GSHGEOM_Device_Point at(double x, double y) {
  GSHGEOM_Device_Point p;
  p.x = (int32_t)std::lround(x * 64.0);
  p.y = (int32_t)std::lround(y * 64.0);
  return p;
}

GSHSCENE_Pick_Params ask(double x, double y, uint32_t flags = 0, size_t max_hits = 0) {
  GSHSCENE_Pick_Params q;
  std::memset(&q, 0, sizeof q);
  q.point = at(x, y);
  q.flags = flags;
  q.max_hits = max_hits;
  return q;
}

/** A closed list over @p store; the test fills it first. */
struct Recorded {
  ListHolder h;
  explicit Recorded(GSHSCENE_Store * store) : h(store) {}
  GSHSCENE_List * list() const { return h.list; }
  void fill(const GSHSCENE_Resource * r, const GSHGEOM_Map & m = identity(), double alpha = 1.0,
      GSHGEOM_Fill_Rule rule = GSHGEOM_FILL_NONZERO) {
    GSHSCENE_Paint paint = solid(0, 0, 0, alpha);
    ASSERT_EQ(gshscene_list_fill(list(), r, &m, rule, &paint, 0, 0.0), GSHSCENE_OK);
  }
  void clip(const GSHSCENE_Resource * r, const GSHGEOM_Map & m = identity(), GSHGEOM_Fill_Rule rule = GSHGEOM_FILL_NONZERO) {
    ASSERT_EQ(gshscene_list_clip_push(list(), r, &m, rule, 0, 0.0), GSHSCENE_OK);
  }
  void pop() { ASSERT_EQ(gshscene_list_clip_pop(list()), GSHSCENE_OK); }
  void group(double opacity) {
    GSHSCENE_Group g = make_group(opacity);
    ASSERT_EQ(gshscene_list_group_push(list(), &g, 0, 0.0), GSHSCENE_OK);
  }
  void end_group() { ASSERT_EQ(gshscene_list_group_pop(list()), GSHSCENE_OK); }
  void instance(const GSHSCENE_Resource * picture, const GSHGEOM_Map & m = identity()) {
    ASSERT_EQ(gshscene_list_instance(list(), picture, &m, 0, 0.0), GSHSCENE_OK);
  }
  void stroke(const GSHSCENE_Resource * centerline, const GSHSCENE_Resource * style, const GSHGEOM_Map & m = identity()) {
    GSHSCENE_Paint paint = solid(0, 0, 0, 1);
    ASSERT_EQ(gshscene_list_stroke(list(), centerline, style, &m, &paint, 0, 0.0), GSHSCENE_OK);
  }
  void close() { ASSERT_EQ(gshscene_list_close(list()), GSHSCENE_OK); }
};

/** The items an entry names, as `chain` strings, topmost first, with its state. */
std::string summarize(const GSHSCENE_Pick * pick) {
  std::string out;
  for (size_t i = 0; i < gshscene_pick_count(pick); i++) {
    GSHSCENE_Pick_Entry e;
    EXPECT_EQ(gshscene_pick_entry(pick, i, &e), GSHSCENE_OK);
    if (i) {
      out += " ";
    }
    out += gshscene_pick_state_string(e.state);
    out += ":";
    for (size_t k = 0; k < e.chain_count; k++) {
      out += (k ? "." : "") + std::to_string(e.chain[k]);
    }
  }
  return out;
}

std::string pick_list(const GSHSCENE_List * list, const GSHSCENE_Pick_Params & q) {
  PickHolder p;
  EXPECT_EQ(gshscene_pick_list(nullptr, list, &q, &p.pick), GSHSCENE_OK);
  return summarize(p.pick);
}

GSHSCENE_Pick_Why why_of(const GSHSCENE_List * list, const GSHSCENE_Pick_Params & q, size_t index = 0) {
  PickHolder p;
  EXPECT_EQ(gshscene_pick_list(nullptr, list, &q, &p.pick), GSHSCENE_OK);
  GSHSCENE_Pick_Entry e;
  EXPECT_EQ(gshscene_pick_entry(p.pick, index, &e), GSHSCENE_OK);
  return e.why;
}

}  // namespace

TEST(Pick, AFillIsInsideOnItsBoundaryOrNotListed) {
  StoreHolder sh;
  Recorded l(sh.store);
  Ref sq = rect_res(sh.store, 10, 10, 20, 20);
  l.fill(sq.get());
  l.close();
  EXPECT_EQ(pick_list(l.list(), ask(15, 15)), "inside:0");
  EXPECT_EQ(pick_list(l.list(), ask(10, 15)), "boundary:0") << "the left edge";
  EXPECT_EQ(pick_list(l.list(), ask(20, 20)), "boundary:0") << "a corner";
  EXPECT_EQ(pick_list(l.list(), ask(9.984375, 15)), "") << "one unit outside";
  EXPECT_EQ(pick_list(l.list(), ask(20.015625, 15)), "");
  EXPECT_EQ(pick_list(l.list(), ask(15, 25)), "");
}

TEST(Pick, TheTopmostIsFirstAndMaxHitsKeepsTheTopmost) {
  StoreHolder sh;
  Recorded l(sh.store);
  Ref a = rect_res(sh.store, 0, 0, 30, 30);
  Ref b = rect_res(sh.store, 10, 10, 40, 40);
  Ref c = rect_res(sh.store, 20, 20, 50, 50);
  l.fill(a.get());
  l.fill(b.get());
  l.fill(c.get());
  l.close();
  EXPECT_EQ(pick_list(l.list(), ask(25, 25)), "inside:2 inside:1 inside:0");
  EXPECT_EQ(pick_list(l.list(), ask(15, 15)), "inside:1 inside:0");
  EXPECT_EQ(pick_list(l.list(), ask(25, 25, 0, 2)), "inside:2 inside:1") << "PK12: the topmost two";
  PickHolder p;
  GSHSCENE_Pick_Params q = ask(25, 25, 0, 1);
  ASSERT_EQ(gshscene_pick_list(nullptr, l.list(), &q, &p.pick), GSHSCENE_OK);
  EXPECT_EQ(gshscene_pick_count(p.pick), 1u);
  EXPECT_EQ(gshscene_pick_total(p.pick), 3u);
  GSHSCENE_Pick_Entry e;
  EXPECT_EQ(gshscene_pick_entry(p.pick, 1, &e), GSHSCENE_ERR_INVALID);
}

TEST(Pick, TheEvenOddRuleLeavesAHoleAndNonzeroDoesNot) {
  StoreHolder sh;
  GSHGEOM_Path * path = rect(0, 0, 40, 40);
  gshgeom_path_move_to(path, P(10, 10));
  gshgeom_path_line_to(path, P(30, 10));
  gshgeom_path_line_to(path, P(30, 30));
  gshgeom_path_line_to(path, P(10, 30));
  gshgeom_path_close(path);  // the same direction: winding 2 inside the inner square
  Ref two(intern(sh.store, path));
  gshgeom_path_destroy(path);
  Recorded eo(sh.store);
  eo.fill(two.get(), identity(), 1.0, GSHGEOM_FILL_EVEN_ODD);
  eo.close();
  Recorded nz(sh.store);
  nz.fill(two.get());
  nz.close();
  EXPECT_EQ(pick_list(eo.list(), ask(20, 20)), "") << "winding 2 is even";
  EXPECT_EQ(pick_list(eo.list(), ask(5, 5)), "inside:0");
  EXPECT_EQ(pick_list(nz.list(), ask(20, 20)), "inside:0");
}

TEST(Pick, ATargetMapPlacesTheShapes) {
  StoreHolder sh;
  Recorded l(sh.store);
  Ref sq = rect_res(sh.store, 0, 0, 10, 10);
  l.fill(sq.get(), translate(5, 5));  // 5..15 in the list's space
  l.close();
  GSHSCENE_Pick_Params q = ask(25, 25);  // device (25,25) is list (12.5, 12.5) under a 2x target
  q.target = scaled(2, 2);
  EXPECT_EQ(pick_list(l.list(), q), "inside:0");
  q.point = at(35, 25);  // list (17.5, 12.5): outside
  EXPECT_EQ(pick_list(l.list(), q), "");
  q.point = at(30, 25);  // list x = 15: the edge
  EXPECT_EQ(pick_list(l.list(), q), "boundary:0");
}

TEST(Pick, AClipLimitsWhatFollowsUntilItIsPopped) {
  StoreHolder sh;
  Recorded l(sh.store);
  Ref big = rect_res(sh.store, 0, 0, 100, 100);
  Ref win = rect_res(sh.store, 10, 10, 30, 30);
  l.fill(big.get());             // 0
  l.clip(win.get());             // 1
  l.fill(big.get());             // 2
  l.pop();                       // 3
  l.fill(big.get());             // 4
  l.close();
  EXPECT_EQ(pick_list(l.list(), ask(20, 20)), "inside:4 inside:2 inside:0");
  EXPECT_EQ(pick_list(l.list(), ask(50, 50)), "inside:4 inside:0") << "outside the clip: item 2 is not hit";
  EXPECT_EQ(pick_list(l.list(), ask(10, 20)), "inside:4 boundary:2 inside:0") << "on the clip's edge";
}

TEST(Pick, NestedClipsIntersectAndAGroupsClipAppliesToItsContent) {
  StoreHolder sh;
  Recorded l(sh.store);
  Ref big = rect_res(sh.store, 0, 0, 100, 100);
  Ref a = rect_res(sh.store, 0, 0, 60, 60);
  Ref b = rect_res(sh.store, 40, 40, 100, 100);
  l.clip(a.get());
  l.group(0.5);
  l.clip(b.get());
  l.fill(big.get());  // 3
  l.pop();
  l.end_group();
  l.pop();
  l.close();
  EXPECT_EQ(pick_list(l.list(), ask(50, 50)), "inside:3");
  EXPECT_EQ(pick_list(l.list(), ask(20, 20)), "") << "outside the inner clip";
  EXPECT_EQ(pick_list(l.list(), ask(80, 80)), "") << "outside the outer clip";
}

TEST(Pick, VisibleLeavesOutATransparentPaintAndAGroupOfOpacityZero) {
  StoreHolder sh;
  Recorded l(sh.store);
  Ref sq = rect_res(sh.store, 0, 0, 10, 10);
  l.fill(sq.get(), identity(), 0.0);  // 0: transparent
  l.group(0.0);                       // 1
  l.fill(sq.get());                   // 2
  l.end_group();                      // 3
  l.group(1.0);                       // 4
  l.fill(sq.get());                   // 5
  l.end_group();                      // 6
  l.close();
  EXPECT_EQ(pick_list(l.list(), ask(5, 5)), "inside:5 inside:2 inside:0") << "PK8: by default every shape is pickable";
  EXPECT_EQ(pick_list(l.list(), ask(5, 5, GSHSCENE_PICK_VISIBLE)), "inside:5");
}

TEST(Pick, AnInstanceIsEnteredThroughItsPictureAndTheChainSaysSo) {
  StoreHolder sh;
  Ref a = rect_res(sh.store, 0, 0, 10, 10);
  Ref b = rect_res(sh.store, 20, 0, 30, 10);
  Recorded inner(sh.store);
  inner.fill(a.get());
  inner.fill(b.get());
  inner.close();
  const GSHSCENE_Resource * pic = nullptr;
  ASSERT_EQ(gshscene_store_intern_picture(sh.store, inner.list(), &pic), GSHSCENE_OK);
  Ref picture(pic);
  Recorded outer(sh.store);
  outer.instance(picture.get(), translate(100, 100));  // 0
  outer.fill(a.get());                                 // 1
  outer.instance(picture.get(), translate(0, 50));     // 2
  outer.close();
  EXPECT_EQ(pick_list(outer.list(), ask(105, 105)), "inside:0.0");
  EXPECT_EQ(pick_list(outer.list(), ask(125, 105)), "inside:0.1");
  EXPECT_EQ(pick_list(outer.list(), ask(5, 5)), "inside:1");
  EXPECT_EQ(pick_list(outer.list(), ask(25, 55)), "inside:2.1");
  EXPECT_EQ(pick_list(outer.list(), ask(5, 5, 0, 0)), "inside:1");
}

TEST(Pick, AClipOfTheOuterListAppliesInsideAnInstanceUnderTheMapsOfBothLevels) {
  StoreHolder sh;
  Ref big = rect_res(sh.store, 0, 0, 100, 100);
  Recorded inner(sh.store);
  inner.fill(big.get());
  inner.close();
  const GSHSCENE_Resource * pic = nullptr;
  ASSERT_EQ(gshscene_store_intern_picture(sh.store, inner.list(), &pic), GSHSCENE_OK);
  Ref picture(pic);
  Ref win = rect_res(sh.store, 0, 0, 20, 20);
  Recorded outer(sh.store);
  outer.clip(win.get(), translate(50, 0));              // the region 50..70 x 0..20
  outer.instance(picture.get(), translate(40, 0));      // the square at 40..140
  outer.pop();
  outer.close();
  EXPECT_EQ(pick_list(outer.list(), ask(60, 10)), "inside:1.0");
  EXPECT_EQ(pick_list(outer.list(), ask(45, 10)), "") << "in the picture, outside the clip";
  EXPECT_EQ(pick_list(outer.list(), ask(60, 30)), "");
}

TEST(Pick, AnInstanceInsideAHiddenGroupIsSkipped) {
  StoreHolder sh;
  Ref a = rect_res(sh.store, 0, 0, 10, 10);
  Recorded inner(sh.store);
  inner.fill(a.get());
  inner.close();
  const GSHSCENE_Resource * pic = nullptr;
  ASSERT_EQ(gshscene_store_intern_picture(sh.store, inner.list(), &pic), GSHSCENE_OK);
  Ref picture(pic);
  Recorded outer(sh.store);
  outer.group(0.0);
  outer.instance(picture.get());
  outer.end_group();
  outer.close();
  EXPECT_EQ(pick_list(outer.list(), ask(5, 5)), "inside:1.0");
  EXPECT_EQ(pick_list(outer.list(), ask(5, 5, GSHSCENE_PICK_VISIBLE)), "");
}

TEST(Pick, AUnionClipIsTheUnionOfItsMembersEachMetWithItsInnerClips) {
  StoreHolder sh;
  Ref big = rect_res(sh.store, 0, 0, 100, 100);
  Ref m1 = rect_res(sh.store, 0, 0, 30, 30);
  Ref m2 = rect_res(sh.store, 50, 50, 90, 90);
  Ref cut = rect_res(sh.store, 70, 0, 100, 100);  // met with m2: leaves 70..90 x 50..90
  GSHSCENE_Clip_Inner inner;
  std::memset(&inner, 0, sizeof inner);
  inner.resource = cut.get();
  GSHSCENE_Clip_Member members[2];
  std::memset(members, 0, sizeof members);
  members[0].resource = m1.get();
  members[1].resource = m2.get();
  members[1].inner_count = 1;
  members[1].inner = &inner;
  const GSHSCENE_Resource * u = nullptr;
  ASSERT_EQ(gshscene_store_intern_clip_union(sh.store, members, 2, &u), GSHSCENE_OK);
  Ref uni(u);
  Recorded l(sh.store);
  l.clip(uni.get());
  l.fill(big.get());
  l.pop();
  l.close();
  EXPECT_EQ(pick_list(l.list(), ask(10, 10)), "inside:1") << "the plain member";
  EXPECT_EQ(pick_list(l.list(), ask(80, 70)), "inside:1") << "the clipped member where its inner clip holds";
  EXPECT_EQ(pick_list(l.list(), ask(60, 60)), "") << "the clipped member's own region, outside its inner clip";
  EXPECT_EQ(pick_list(l.list(), ask(40, 40)), "") << "in neither";
  EXPECT_EQ(pick_list(l.list(), ask(70, 60)), "boundary:1") << "on the inner clip's edge";
  /* a point on the boundary of one member and inside another is inside the union */
  EXPECT_EQ(pick_list(l.list(), ask(30, 20)), "boundary:1");
}

TEST(Pick, AStrokeIsItsBodyAndNothingBeyondItsCaps) {
  StoreHolder sh;
  GSHGEOM_Path * line = new_path();
  gshgeom_path_move_to(line, P(10, 20));
  gshgeom_path_line_to(line, P(40, 20));
  Ref center(intern(sh.store, line));
  gshgeom_path_destroy(line);
  GSHSCENE_Stroke_Style st;
  gshscene_stroke_style_init(&st);
  st.width = 6;
  const GSHSCENE_Resource * s = nullptr;
  ASSERT_EQ(gshscene_store_intern_stroke_style(sh.store, &st, &s), GSHSCENE_OK);
  Ref butt(s);
  st.cap = GSHSCENE_STROKE_CAP_SQUARE;
  ASSERT_EQ(gshscene_store_intern_stroke_style(sh.store, &st, &s), GSHSCENE_OK);
  Ref square(s);
  Recorded lb(sh.store);
  lb.stroke(center.get(), butt.get());
  lb.close();
  Recorded ls(sh.store);
  ls.stroke(center.get(), square.get());
  ls.close();
  EXPECT_EQ(pick_list(lb.list(), ask(25, 20)), "inside:0");
  EXPECT_EQ(pick_list(lb.list(), ask(25, 22.5)), "inside:0");
  EXPECT_EQ(pick_list(lb.list(), ask(25, 23)), "boundary:0") << "the side, half a width from the line";
  EXPECT_EQ(pick_list(lb.list(), ask(25, 24)), "");
  EXPECT_EQ(pick_list(lb.list(), ask(9, 20)), "") << "a butt cap does not reach past the end";
  EXPECT_EQ(pick_list(ls.list(), ask(9, 20)), "inside:0") << "a square cap does";
  EXPECT_EQ(pick_list(ls.list(), ask(6, 20)), "");
  /* under a map: the stroke is of the map's pixels */
  GSHSCENE_Pick_Params q = ask(50, 40);
  q.target = scaled(2, 2);
  EXPECT_EQ(pick_list(lb.list(), q), "inside:0");
  q.point = at(50, 47);  // list y 23.5: past the half width 3
  EXPECT_EQ(pick_list(lb.list(), q), "");
}

TEST(Pick, AStrokeAlignedInsideIsABandInsideTheShape) {
  StoreHolder sh;
  Ref sq = rect_res(sh.store, 10, 10, 50, 50);
  GSHSCENE_Stroke_Style st;
  gshscene_stroke_style_init(&st);
  st.width = 4;
  st.alignment = GSHSCENE_STROKE_ALIGN_INSIDE;
  const GSHSCENE_Resource * s = nullptr;
  ASSERT_EQ(gshscene_store_intern_stroke_style(sh.store, &st, &s), GSHSCENE_OK);
  Ref inside(s);
  Recorded l(sh.store);
  l.stroke(sq.get(), inside.get());
  l.close();
  EXPECT_EQ(pick_list(l.list(), ask(11, 30)), "inside:0") << "in the band";
  EXPECT_EQ(pick_list(l.list(), ask(30, 30)), "") << "the middle of the shape";
  EXPECT_EQ(pick_list(l.list(), ask(8, 30)), "") << "outside the shape";
}

TEST(Pick, AFillPaintedWithAHatchIsItsLinesTrimmedToTheShape) {
  StoreHolder sh;
  Ref region = rect_res(sh.store, 0, 0, 20, 20);
  GSHSCENE_Stroke_Style st;
  gshscene_stroke_style_init(&st);
  st.width = 1.0;
  const GSHSCENE_Resource * s = nullptr;
  ASSERT_EQ(gshscene_store_intern_stroke_style(sh.store, &st, &s), GSHSCENE_OK);
  Ref pen(s);
  GSHSCENE_Hatch_Family fam;
  std::memset(&fam, 0, sizeof fam);
  fam.offset_across = 5.0;  // horizontal lines at y = 0, 5, 10, 15, 20
  GSHSCENE_Hatch h;
  gshscene_hatch_init(&h);
  h.style = pen.get();
  h.family_count = 1;
  h.families = &fam;
  const GSHSCENE_Resource * hr = nullptr;
  ASSERT_EQ(gshscene_store_intern_hatch(sh.store, &h, &hr), GSHSCENE_OK);
  Ref hatch(hr);
  Recorded l(sh.store);
  GSHSCENE_Paint paint;
  std::memset(&paint, 0, sizeof paint);
  paint.kind = GSHSCENE_PAINT_HATCH;
  paint.resource = hatch.get();
  paint.rgba[3] = 1.0;
  const GSHGEOM_Map m = identity();
  ASSERT_EQ(gshscene_list_fill(l.list(), region.get(), &m, GSHGEOM_FILL_NONZERO, &paint, 0, 0.0), GSHSCENE_OK);
  l.close();
  EXPECT_EQ(pick_list(l.list(), ask(10, 10)), "inside:0") << "on a line";
  EXPECT_EQ(pick_list(l.list(), ask(10, 12.5)), "") << "between lines, inside the shape: not drawn, so not hit";
  EXPECT_EQ(pick_list(l.list(), ask(30, 10)), "") << "on a line's extension, outside the shape";
  EXPECT_EQ(pick_list(l.list(), ask(10, 10.5)), "boundary:0") << "the line's edge";
  PickHolder p;
  GSHSCENE_Pick_Params q = ask(10, 10);
  ASSERT_EQ(gshscene_pick_list(nullptr, l.list(), &q, &p.pick), GSHSCENE_OK);
  GSHSCENE_Pick_Entry e;
  ASSERT_EQ(gshscene_pick_entry(p.pick, 0, &e), GSHSCENE_OK);
  EXPECT_EQ(e.kind, GSHSCENE_ITEM_FILL) << "listed as the fill it is";
}

TEST(Pick, APlaneIsEverywhereTheClipsAllow) {
  StoreHolder sh;
  const GSHSCENE_Resource * pl = nullptr;
  ASSERT_EQ(gshscene_store_intern_plane(sh.store, &pl), GSHSCENE_OK);
  Ref plane(pl);
  Ref win = rect_res(sh.store, 0, 0, 10, 10);
  Recorded l(sh.store);
  l.fill(plane.get());
  l.clip(win.get());
  l.fill(plane.get());
  l.pop();
  l.close();
  EXPECT_EQ(pick_list(l.list(), ask(1000, 1000)), "inside:0");
  EXPECT_EQ(pick_list(l.list(), ask(5, 5)), "inside:2 inside:0");
}

TEST(Pick, APlaceholderDrawsNothingAndIsNeverHit) {
  StoreHolder sh;
  Recorded l(sh.store);
  const double box[4] = {0, 0, 10, 10};
  ASSERT_EQ(gshscene_list_placeholder(l.list(), 1, 1, 0, box, nullptr, 0, 0.0), GSHSCENE_OK);
  l.close();
  EXPECT_EQ(pick_list(l.list(), ask(5, 5)), "");
}

TEST(Pick, TheArgumentsAreChecked) {
  StoreHolder sh;
  Recorded open(sh.store);
  Recorded closed(sh.store);
  closed.close();
  PickHolder p;
  GSHSCENE_Pick_Params q = ask(1, 1);
  EXPECT_EQ(gshscene_pick_list(nullptr, nullptr, &q, &p.pick), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_pick_list(nullptr, closed.list(), nullptr, &p.pick), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_pick_list(nullptr, closed.list(), &q, nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_pick_list(nullptr, open.list(), &q, &p.pick), GSHSCENE_ERR_INVALID) << "an open list";
  GSHSCENE_Pick_Params bad = q;
  bad.flags = 2;
  EXPECT_EQ(gshscene_pick_list(nullptr, closed.list(), &bad, &p.pick), GSHSCENE_ERR_INVALID) << "an unknown flag";
  bad = q;
  bad._reserved[3] = 1;
  EXPECT_EQ(gshscene_pick_list(nullptr, closed.list(), &bad, &p.pick), GSHSCENE_ERR_INVALID);
  bad = q;
  gshgeom_map_identity(3, &bad.target);
  EXPECT_EQ(gshscene_pick_list(nullptr, closed.list(), &bad, &p.pick), GSHSCENE_ERR_INVALID) << "a 3D target";
  EXPECT_EQ(p.pick, nullptr) << "nothing is written on failure";
  EXPECT_EQ(gshscene_pick_version(nullptr, nullptr, &q, &p.pick), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_pick_count(nullptr), 0u);
  EXPECT_EQ(gshscene_pick_total(nullptr), 0u);
  gshscene_pick_destroy(nullptr);
  EXPECT_STREQ(gshscene_pick_state_string(GSHSCENE_PICK_INSIDE), "inside");
  EXPECT_STREQ(gshscene_pick_state_string((GSHSCENE_Pick_State)99), "unknown pick state");
  EXPECT_STREQ(gshscene_pick_why_string(GSHSCENE_PICK_WHY_GLYPH), "glyph");
  EXPECT_STREQ(gshscene_pick_why_string((GSHSCENE_Pick_Why)99), "unknown pick reason");
}

TEST(Pick, AVersionResolvesToNodesAndInstancePaths) {
  SceneHolder s;
  Ref sq(intern_square(s.store()));  // 0..4
  EditHolder e(s);
  GSHSCENE_Node top = group_node();
  const uint64_t g = add(e.edit, GSHSCENE_ROOT_ID, top);
  const uint64_t shape = add(e.edit, g, shape_node(sq.get()));
  const uint64_t inst = add(e.edit, GSHSCENE_ROOT_ID, instance_node(g));
  GSHSCENE_Node placed = instance_node(g);
  gshgeom_map_translate(2, 100, 0, 0, &placed.map);
  const uint64_t inst2 = add(e.edit, GSHSCENE_ROOT_ID, placed);
  VersionRef v;
  publish(s, e, &v);
  (void)inst;
  PickHolder p;
  GSHSCENE_Pick_Params q = ask(2, 2);
  ASSERT_EQ(gshscene_pick_version(nullptr, v.get(), &q, &p.pick), GSHSCENE_OK);
  ASSERT_GE(gshscene_pick_count(p.pick), 2u);
  uint64_t node = 0;
  uint64_t path[8];
  size_t n = 0;
  /* Every entry's node is the shape, reached directly or through an instance; the instance path ends at it. */
  bool saw_direct = false;
  bool saw_instance = false;
  for (size_t i = 0; i < gshscene_pick_count(p.pick); i++) {
    ASSERT_EQ(gshscene_pick_resolve(p.pick, v.get(), i, &node, path, 8, &n), GSHSCENE_OK);
    EXPECT_EQ(node, shape);
    EXPECT_EQ(path[n - 1], shape);
    saw_direct |= n == 1;
    saw_instance |= n == 2 && path[0] == inst;
  }
  EXPECT_TRUE(saw_instance) << "the first instance of the group, at the origin";
  EXPECT_TRUE(saw_direct) << "and the group's own shape, drawn where it stands";
  EXPECT_EQ(gshscene_pick_count(p.pick), 2u) << "the second instance is placed at 100";
  ASSERT_EQ(gshscene_pick_resolve(p.pick, v.get(), 0, &node, path, 8, &n), GSHSCENE_OK);
  EXPECT_EQ(n, 2u) << "topmost first: the instance is the later child of the root";
  /* The placed one */
  PickHolder p2;
  q = ask(102, 2);
  ASSERT_EQ(gshscene_pick_version(nullptr, v.get(), &q, &p2.pick), GSHSCENE_OK);
  ASSERT_EQ(gshscene_pick_count(p2.pick), 1u);
  ASSERT_EQ(gshscene_pick_resolve(p2.pick, v.get(), 0, &node, path, 8, &n), GSHSCENE_OK);
  EXPECT_EQ(n, 2u);
  EXPECT_EQ(path[0], inst2);
  EXPECT_EQ(path[1], shape);
  /* the capacity and the range */
  EXPECT_EQ(gshscene_pick_resolve(p2.pick, v.get(), 0, &node, path, 1, &n), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_pick_resolve(p2.pick, v.get(), 1, &node, path, 8, &n), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_pick_resolve(p2.pick, nullptr, 0, &node, path, 8, &n), GSHSCENE_ERR_INVALID);
}

TEST(Pick, EveryFailingAllocationIsAnErrorAndLeavesNothing) {
  StoreHolder sh;
  Recorded l(sh.store);
  Ref a = rect_res(sh.store, 0, 0, 30, 30);
  Ref b = rect_res(sh.store, 10, 10, 40, 40);
  l.clip(a.get());
  l.fill(b.get());
  l.pop();
  l.close();
  const GSHSCENE_Pick_Params q = ask(20, 20);
  tracker::Tracker t;
  GSHSCENE_Allocator alloc = tracker::allocator_for(&t);
  bool saw_ok = false;
  for (long k = 0; k < 200 && !saw_ok; k++) {
    t.persistent = false;
    t.failed = false;
    t.fail_from = t.calls + k;
    GSHSCENE_Pick * pick = nullptr;
    GSHSCENE_Result r = gshscene_pick_list(&alloc, l.list(), &q, &pick);
    t.fail_from = -1;
    if (r == GSHSCENE_OK) {
      saw_ok = true;
      EXPECT_FALSE(t.failed);
      EXPECT_EQ(gshscene_pick_count(pick), 1u);
      gshscene_pick_destroy(pick);
    } else {
      EXPECT_EQ(r, GSHSCENE_ERR_OOM) << "allocation " << k;
      EXPECT_EQ(pick, nullptr);
    }
    EXPECT_TRUE(t.live.empty()) << "allocation " << k << ": " << t.live.size() << " blocks leaked";
  }
  EXPECT_TRUE(saw_ok);
}

TEST(Pick, ARunFromOutlinesAddsTheWindingsOfItsGlyphsAndAnUnboundFontRunIsUndecided) {
  StoreHolder sh;
  Ref g1 = rect_res(sh.store, 0, 0, 10, 10);
  Ref g2 = rect_res(sh.store, 0, 0, 10, 10);
  GSHSCENE_Glyph glyphs[2];
  std::memset(glyphs, 0, sizeof glyphs);
  glyphs[0].id = 1;
  glyphs[0].outline = g1.get();
  glyphs[1].id = 2;
  glyphs[1].x = 30;
  glyphs[1].outline = g1.get();
  GSHSCENE_Glyph_Run run;
  std::memset(&run, 0, sizeof run);
  run.source = GSHSCENE_GLYPHS_FROM_OUTLINES;
  run.glyph_count = 2;
  run.glyphs = glyphs;
  const GSHSCENE_Resource * r = nullptr;
  ASSERT_EQ(gshscene_store_intern_glyph_run(sh.store, &run, &r), GSHSCENE_OK);
  Ref outlines(r);
  Recorded l(sh.store);
  l.fill(outlines.get());
  l.close();
  EXPECT_EQ(pick_list(l.list(), ask(5, 5)), "inside:0");
  EXPECT_EQ(pick_list(l.list(), ask(35, 5)), "inside:0") << "the second glyph, moved by its position";
  EXPECT_EQ(pick_list(l.list(), ask(20, 5)), "");
  /* two glyphs at one place wind twice: even-odd leaves nothing, nonzero fills */
  glyphs[1].x = 0;
  ASSERT_EQ(gshscene_store_intern_glyph_run(sh.store, &run, &r), GSHSCENE_OK);
  Ref doubled(r);
  Recorded eo(sh.store);
  eo.fill(doubled.get(), identity(), 1.0, GSHGEOM_FILL_EVEN_ODD);
  eo.close();
  EXPECT_EQ(pick_list(eo.list(), ask(5, 5)), "") << "AA2: the windings of a run's glyphs add";
  /* a run from a font whose face is not bound has no route a sink takes: it cannot be decided, and says why */
  Ref face(intern_face(sh.store));
  GSHSCENE_Glyph fg;
  std::memset(&fg, 0, sizeof fg);
  fg.id = 7;
  Ref font(intern_font_run(sh.store, face.get(), 16.0, {fg}));
  ASSERT_NE(font.get(), nullptr);
  Recorded lf(sh.store);
  lf.fill(font.get());
  lf.close();
  EXPECT_EQ(pick_list(lf.list(), ask(5, 5)), "undecided:0");
  EXPECT_EQ(why_of(lf.list(), ask(5, 5)), GSHSCENE_PICK_WHY_GLYPH);
}

/* ---- the randomized comparison with a reference that knows only rectangles ------------------------------------------------------- */

namespace {

struct Rect {
  int64_t x0, y0, x1, y1;  // 26.6
};

/** The reference answer for a point against a rectangle: 2 inside, 1 on the boundary, 0 outside. */
int against(const Rect & r, int64_t x, int64_t y) {
  if (x < r.x0 || x > r.x1 || y < r.y0 || y > r.y1) {
    return 0;
  }
  return (x == r.x0 || x == r.x1 || y == r.y0 || y == r.y1) ? 1 : 2;
}

int meet(int a, int b) {
  return a == 0 || b == 0 ? 0 : (a < b ? a : b);
}

/** One recorded op of the random list. */
struct Op {
  int kind;  // 0 fill, 1 clip push, 2 clip pop, 3 group push, 4 group pop, 5 instance
  Rect rect;
  int64_t dx, dy;  // 26.6 translation of the item
  bool hidden;     // a group of opacity 0
  bool clear;      // a fill of alpha 0
  int picture;     // an instance's picture
};

}  // namespace

TEST(Pick, ARandomListAgreesWithTheRectangleReference) {
  std::mt19937 rng(20261113);
  auto pick_int = [&](int lo, int hi) { return (int)(lo + rng() % (unsigned)(hi - lo + 1)); };
  int stats_hits = 0;
  int stats_chains = 0;
  int stats_boundary = 0;
  for (int round = 0; round < 500; round++) {
    StoreHolder sh;
    /* A few pictures of plain fills and clips, then a list that uses them. */
    std::vector<std::vector<Op>> pictures(3);
    std::vector<Ref> pic_refs;
    auto make_rect = [&]() {
      Rect r;
      const int x0 = pick_int(0, 18);
      const int y0 = pick_int(0, 18);
      r.x0 = x0 * 64;
      r.y0 = y0 * 64;
      r.x1 = (x0 + pick_int(1, 14)) * 64;
      r.y1 = (y0 + pick_int(1, 14)) * 64;
      return r;
    };
    std::vector<Ref> rect_refs;
    auto res_of = [&](const Rect & r) {
      rect_refs.push_back(rect_res(sh.store, (double)r.x0 / 64, (double)r.y0 / 64, (double)r.x1 / 64, (double)r.y1 / 64));
      return rect_refs.back().get();
    };
    auto body = [&](Recorded & rec, std::vector<Op> & ops, int length, bool allow_instance) {
      int clips = 0;
      int groups = 0;
      std::vector<int> stack;  // 1 clip, 2 group
      for (int i = 0; i < length; i++) {
        Op op;
        std::memset(&op, 0, sizeof op);
        int roll = pick_int(0, allow_instance ? 9 : 8);
        op.rect = make_rect();
        op.dx = pick_int(-3, 3) * 64 * pick_int(0, 1);
        op.dy = pick_int(-3, 3) * 64 * pick_int(0, 1);
        const GSHGEOM_Map m = translate((double)op.dx / 64, (double)op.dy / 64);
        if (roll <= 3) {
          op.kind = 0;
          op.clear = pick_int(0, 5) == 0;
          rec.fill(res_of(op.rect), m, op.clear ? 0.0 : 1.0);
        } else if (roll <= 5 && clips < 3) {
          op.kind = 1;
          rec.clip(res_of(op.rect), m);
          stack.push_back(1);
          clips++;
        } else if (roll == 6 && groups < 2) {
          op.kind = 3;
          op.hidden = pick_int(0, 3) == 0;
          rec.group(op.hidden ? 0.0 : 0.5);
          stack.push_back(2);
          groups++;
        } else if (roll == 9 && allow_instance && !pic_refs.empty()) {
          op.kind = 5;
          op.picture = pick_int(0, (int)pic_refs.size() - 1);
          rec.instance(pic_refs[(size_t)op.picture].get(), m);
        } else if (!stack.empty()) {
          if (stack.back() == 1) {
            op.kind = 2;
            rec.pop();
            clips--;
          } else {
            op.kind = 4;
            rec.end_group();
            groups--;
          }
          stack.pop_back();
        } else {
          continue;
        }
        ops.push_back(op);
      }
      while (!stack.empty()) {
        Op op;
        std::memset(&op, 0, sizeof op);
        if (stack.back() == 1) {
          op.kind = 2;
          rec.pop();
        } else {
          op.kind = 4;
          rec.end_group();
        }
        stack.pop_back();
        ops.push_back(op);
      }
    };
    for (int pi = 0; pi < 3; pi++) {
      Recorded rec(sh.store);
      body(rec, pictures[(size_t)pi], pick_int(1, 8), false);
      rec.close();
      const GSHSCENE_Resource * pic = nullptr;
      ASSERT_EQ(gshscene_store_intern_picture(sh.store, rec.list(), &pic), GSHSCENE_OK);
      pic_refs.emplace_back(pic);
    }
    Recorded top(sh.store);
    std::vector<Op> ops;
    body(top, ops, pick_int(3, 24), true);
    top.close();
    /* The reference: the same walk over the ops, with the instance maps summed. */
    for (int trial = 0; trial < 12; trial++) {
      const bool visible = pick_int(0, 1) == 1;
      int64_t px = pick_int(0, 34 * 4) * 16;
      int64_t py = pick_int(0, 34 * 4) * 16;
      if (trial % 3 == 0) {
        px = (px / 64) * 64;  // on a pixel edge: boundaries are common
        py = (py / 64) * 64;
      }
      std::string expect;
      /* Items found, in paint order, with the chain and the answer. */
      struct Found {
        int state;
        std::string chain;
      };
      std::vector<Found> found;
      std::function<void(const std::vector<Op> &, int64_t, int64_t, std::vector<std::tuple<Rect, int64_t, int64_t>> &, int, const std::string &, int &)>
          run;
      std::vector<std::tuple<Rect, int64_t, int64_t>> clips;  // rect, the shift of its list's level, unused
      run = [&](const std::vector<Op> & list, int64_t sx, int64_t sy, std::vector<std::tuple<Rect, int64_t, int64_t>> & cs, int level,
                 const std::string & prefix, int & hid) {
        std::vector<int> groups;
        size_t clip_base = cs.size();
        for (size_t i = 0; i < list.size(); i++) {
          const Op & op = list[i];
          const std::string chain = prefix + (prefix.empty() ? "" : ".") + std::to_string(i);
          switch (op.kind) {
            case 0: {
              if (hid || (visible && op.clear)) {
                break;
              }
              int st = against(op.rect, px - sx - op.dx, py - sy - op.dy);
              for (const auto & c : cs) {
                st = meet(st, against(std::get<0>(c), px - std::get<1>(c) - 0, py - std::get<2>(c) - 0));
              }
              if (st) {
                found.push_back({st, chain});
              }
              break;
            }
            case 1: {
              /* A clip's rectangle is stored already moved by its own offset and its level's shift. */
              Rect r = op.rect;
              r.x0 += op.dx;
              r.x1 += op.dx;
              r.y0 += op.dy;
              r.y1 += op.dy;
              cs.emplace_back(r, sx, sy);
              break;
            }
            case 2:
              if (cs.size() > clip_base) {
                cs.pop_back();
              }
              break;
            case 3:
              groups.push_back(visible && op.hidden ? 1 : 0);
              hid += groups.back();
              break;
            case 4:
              if (!groups.empty()) {
                hid -= groups.back();
                groups.pop_back();
              }
              break;
            case 5:
              if (!hid) {
                run(pictures[(size_t)op.picture], sx + op.dx, sy + op.dy, cs, level + 1, chain, hid);
              }
              break;
            default:
              break;
          }
        }
        while (cs.size() > clip_base) {
          cs.pop_back();
        }
        while (!groups.empty()) {
          hid -= groups.back();
          groups.pop_back();
        }
      };
      int hid = 0;
      run(ops, 0, 0, clips, 0, "", hid);
      std::string want;
      for (size_t k = found.size(); k-- > 0;) {
        if (!want.empty()) {
          want += " ";
        }
        want += std::string(found[k].state == 2 ? "inside:" : "boundary:") + found[k].chain;
      }
      GSHSCENE_Pick_Params q;
      std::memset(&q, 0, sizeof q);
      q.point.x = (int32_t)px;
      q.point.y = (int32_t)py;
      q.flags = visible ? GSHSCENE_PICK_VISIBLE : 0;
      EXPECT_EQ(pick_list(top.list(), q), want) << "round " << round << " trial " << trial << " point " << px << "," << py;
      /* The comparison must have seen what it is for: hits, instance chains, boundaries and clipped-away items. */
      stats_hits += !want.empty();
      stats_chains += want.find('.') != std::string::npos;
      stats_boundary += want.find("boundary") != std::string::npos;
    }
  }
  EXPECT_GT(stats_hits, 600) << stats_hits;
  EXPECT_GT(stats_chains, 60) << stats_chains << "points that land in an instance's picture";
  EXPECT_GT(stats_boundary, 60) << stats_boundary << "points on an edge";
}


int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
