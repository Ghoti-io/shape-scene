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
 * The order of a list (N11, N12): a recorded list is put in paint order when it
 * is closed, a version's emitted list is made in that order, every resolved depth
 * tie is recorded, and the ties are an annotation (story 12).
 */

#include "scene_fixtures.h"

#include "../alloc_tracker.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <random>
#include <sstream>

using namespace scene;

namespace {

/** One entry of a declared-order description. */
struct E {
  char kind;         // 'f' fill, 'C' clip push, 'c' clip pop, 'G' group push, 'g' group pop
  int id = 0;        // a fill's identity, carried in its red component
  int32_t layer = 0;
  double depth = 0.0;
  uint32_t order = GSHSCENE_ORDER_FLAT;  // a group push's context
};

E fl(int id, int32_t layer = 0, double depth = 0.0) { return E{'f', id, layer, depth}; }
E gp(uint32_t order, int32_t layer = 0, double depth = 0.0) { return E{'G', 0, layer, depth, order}; }
E gop() { return E{'g'}; }
E cp(int32_t layer = 0, double depth = 0.0) { return E{'C', 0, layer, depth}; }
E cop() { return E{'c'}; }

struct Fix {
  StoreHolder s;
  Ref sq{intern_square(s.store)};
  Ref p2{intern_fixture(s.store)};
};

/** Record @p entries in @p list; the caller closes it. */
void record(GSHSCENE_List * list, Fix & f, const std::vector<E> & entries) {
  for (const E & e : entries) {
    switch (e.kind) {
      case 'f': {
        GSHSCENE_Paint p = solid((double)e.id, 0.0, 0.0, 1.0);
        ASSERT_EQ(gshscene_list_fill(list, f.sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &p, e.layer, e.depth), GSHSCENE_OK);
        break;
      }
      case 'C':
        ASSERT_EQ(gshscene_list_clip_push(list, f.p2.get(), nullptr, GSHGEOM_FILL_NONZERO, e.layer, e.depth),
            GSHSCENE_OK);
        break;
      case 'c':
        ASSERT_EQ(gshscene_list_clip_pop(list), GSHSCENE_OK);
        break;
      case 'G': {
        GSHSCENE_Group g = make_group(1.0, e.order);
        ASSERT_EQ(gshscene_list_group_push(list, &g, e.layer, e.depth), GSHSCENE_OK);
        break;
      }
      default:
        ASSERT_EQ(gshscene_list_group_pop(list), GSHSCENE_OK);
        break;
    }
  }
}

/** The closed list as a string: f<id> for a fill, C c G g for the pushes and pops, I for an instance. */
std::string shape_of(const GSHSCENE_List * list) {
  std::string out;
  for (size_t i = 0; i < gshscene_list_count(list); i++) {
    const GSHSCENE_Item * item = nullptr;
    EXPECT_EQ(gshscene_list_item(list, i, &item), GSHSCENE_OK);
    std::string token;
    switch (item->kind) {
      case GSHSCENE_ITEM_FILL:
        token = "f" + std::to_string((int)item->paint.rgba[0]);
        break;
      case GSHSCENE_ITEM_CLIP_PUSH:
        token = "C";
        break;
      case GSHSCENE_ITEM_CLIP_POP:
        token = "c";
        break;
      case GSHSCENE_ITEM_GROUP_PUSH:
        token = "G";
        break;
      case GSHSCENE_ITEM_GROUP_POP:
        token = "g";
        break;
      default:
        token = "I";
        break;
    }
    out += out.empty() ? token : " " + token;
  }
  return out;
}

struct TieRow {
  size_t first, second;
  double depth;
  int reason;
  bool operator==(const TieRow & o) const {
    return first == o.first && second == o.second && depth == o.depth && reason == o.reason;
  }
};

std::vector<TieRow> ties_of(const GSHSCENE_List * list) {
  std::vector<TieRow> out;
  for (size_t i = 0; i < gshscene_list_tie_count(list); i++) {
    const GSHSCENE_Tie * t = nullptr;
    EXPECT_EQ(gshscene_list_tie(list, i, &t), GSHSCENE_OK);
    out.push_back({t->first, t->second, t->depth, (int)t->reason});
  }
  return out;
}

std::string closed(Fix & f, const std::vector<E> & entries, std::vector<TieRow> * ties = nullptr) {
  ListHolder l(f.s.store);
  record(l.list, f, entries);
  EXPECT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
  if (ties) {
    *ties = ties_of(l.list);
  }
  return shape_of(l.list);
}

}  // namespace

// --- the three contexts -----------------------------------------------------------------------------------------

TEST(Order, FlatKeepsTheDeclaredOrderWhateverTheKeys) {
  Fix f;
  std::vector<TieRow> ties;
  // A flat group (and the root) ignores layers and depths, and records no ties.
  EXPECT_EQ(closed(f, {fl(1, 9, 5.0), fl(2, -9, -5.0), gp(GSHSCENE_ORDER_FLAT), fl(3, 5, 1.0), fl(4, -5, 2.0), gop()}, &ties),
      "f1 f2 G f3 f4 g");
  EXPECT_TRUE(ties.empty());
}

TEST(Order, OverlayIsByLayerAndEqualLayersKeepDeclaredOrder) {
  Fix f;
  std::vector<TieRow> ties;
  EXPECT_EQ(closed(f, {gp(GSHSCENE_ORDER_OVERLAY), fl(1, 3), fl(2, 1), fl(3, 3), fl(4, 2), fl(5, 1), fl(6, -7), gop()}, &ties),
      "G f6 f2 f5 f4 f1 f3 g");
  EXPECT_TRUE(ties.empty()) << "declared order resolves every overlay tie, so none is recorded";
  // Depth is not an overlay key.
  EXPECT_EQ(closed(f, {gp(GSHSCENE_ORDER_OVERLAY), fl(1, 0, 3.0), fl(2, 0, 1.0), fl(3, 0, 2.0), gop()}, &ties), "G f1 f2 f3 g");
}

TEST(Order, DepthIsFarToNearAndATieBreaksByLayerThenDeclaredOrder) {
  Fix f;
  std::vector<TieRow> ties;
  // Farther (larger depth) first.
  EXPECT_EQ(closed(f, {gp(GSHSCENE_ORDER_DEPTH), fl(1, 0, 1.0), fl(2, 0, 3.0), fl(3, 0, 2.0), gop()}, &ties), "G f2 f3 f1 g");
  EXPECT_TRUE(ties.empty());
  // Equal depth: ascending layer, then declared order.  Items: 0 G, 1..5 fills.
  EXPECT_EQ(closed(f, {gp(GSHSCENE_ORDER_DEPTH), fl(1, 2, 1.0), fl(2, 0, 1.0), fl(3, 1, 1.0), fl(4, 1, 1.0), fl(5, 0, 2.0), gop()},
                &ties),
      "G f5 f2 f3 f4 f1 g");
  // The order is G(0) f5(1) f2(2) f3(3) f4(4) f1(5) g(6): four ties among the depth-1 units.
  std::vector<TieRow> want = {{2, 3, 1.0, GSHSCENE_TIE_LAYER}, {3, 4, 1.0, GSHSCENE_TIE_DECLARED}, {4, 5, 1.0, GSHSCENE_TIE_LAYER}};
  EXPECT_EQ(ties, want);
}

TEST(Order, ARunOfThreeEqualDepthsRecordsTwoTiesAndNegativeZeroIsZero) {
  Fix f;
  std::vector<TieRow> ties;
  EXPECT_EQ(closed(f, {gp(GSHSCENE_ORDER_DEPTH), fl(1, 0, 0.0), fl(2, 0, -0.0), fl(3, 0, 0.0), gop()}, &ties), "G f1 f2 f3 g");
  std::vector<TieRow> want = {{1, 2, 0.0, GSHSCENE_TIE_DECLARED}, {2, 3, 0.0, GSHSCENE_TIE_DECLARED}};
  EXPECT_EQ(ties, want);
  // -0 is stored as +0 (one spelling), so the hash cannot tell them apart.
  ListHolder a(f.s.store), b(f.s.store);
  record(a.list, f, {fl(1, 0, -0.0)});
  record(b.list, f, {fl(1, 0, 0.0)});
  EXPECT_EQ(gshscene_list_hash(a.list), gshscene_list_hash(b.list));
  EXPECT_TRUE(gshscene_list_equal(a.list, b.list));
  const GSHSCENE_Item * item = nullptr;
  ASSERT_EQ(gshscene_list_item(a.list, 0, &item), GSHSCENE_OK);
  EXPECT_FALSE(std::signbit(item->depth));
}

TEST(Order, TheRootIsFlatAndCannotBeReordered) {
  Fix f;
  std::vector<TieRow> ties;
  EXPECT_EQ(closed(f, {fl(1, 5, 9.0), fl(2, 0, 1.0), fl(3, -5, 2.0)}, &ties), "f1 f2 f3");
  EXPECT_TRUE(ties.empty());
}

// --- units are opaque, contexts nest ----------------------------------------------------------------------------

TEST(Order, AGroupAndAClipBlockAreOneUnitOfTheirSequence) {
  Fix f;
  std::vector<TieRow> ties;
  // In a depth group: a flat group (depth 2), a clip block (depth 3) and a fill (depth 1).  The units move; what is inside
  // each stays as it was declared, a clip block taking the context of the sequence it is in (here depth).
  std::vector<E> e = {gp(GSHSCENE_ORDER_DEPTH),
      gp(GSHSCENE_ORDER_FLAT, 0, 2.0), fl(1, 9, 9.0), fl(2, 0, 0.0), gop(),
      cp(0, 3.0), fl(3, 0, 1.0), fl(4, 0, 5.0), cop(),
      fl(5, 0, 1.0), gop()};
  EXPECT_EQ(closed(f, e, &ties), "G C f4 f3 c G f1 f2 g f5 g");
  EXPECT_TRUE(ties.empty());
}

TEST(Order, ContextsNestAndAGroupDeclaresItsOwn) {
  Fix f;
  std::vector<TieRow> ties;
  // depth > overlay > flat: the outer sorts its units by depth, the overlay unit its fills by layer, the flat one keeps its own.
  std::vector<E> e = {gp(GSHSCENE_ORDER_DEPTH),
      gp(GSHSCENE_ORDER_OVERLAY, 0, 1.0), fl(1, 2), fl(2, 1), fl(3, 2), gop(),
      gp(GSHSCENE_ORDER_FLAT, 0, 4.0), fl(4, 2, 7.0), fl(5, 1, 8.0), gop(),
      fl(6, 0, 2.0), gop()};
  EXPECT_EQ(closed(f, e, &ties), "G G f4 f5 g f6 G f2 f1 f3 g g");
}

TEST(Order, ADepthGroupInsideAFlatGroupIsOnlyItsOwnSequence) {
  Fix f;
  std::vector<TieRow> ties;
  std::vector<E> e = {gp(GSHSCENE_ORDER_FLAT), fl(1, 0, 1.0), gp(GSHSCENE_ORDER_DEPTH), fl(2, 0, 1.0), fl(3, 0, 2.0), gop(),
      fl(4, 0, 0.0), gop()};
  EXPECT_EQ(closed(f, e, &ties), "G f1 G f3 f2 g f4 g");
}

TEST(Order, AnEmptyGroupAndAnEmptyClipAreUnits) {
  Fix f;
  std::vector<TieRow> ties;
  std::vector<E> e = {gp(GSHSCENE_ORDER_DEPTH), gp(GSHSCENE_ORDER_FLAT, 0, 5.0), gop(), cp(0, 4.0), cop(), fl(1, 0, 3.0), gop()};
  EXPECT_EQ(closed(f, e, &ties), "G G g C c f1 g");
}

TEST(Order, ATieIsBetweenAdjacentUnitsAndNamesTheirFirstItems) {
  Fix f;
  std::vector<TieRow> ties;
  // Two groups of equal depth: the tie names the group pushes (items 1 and 4), not what is inside.
  std::vector<E> e = {gp(GSHSCENE_ORDER_DEPTH), gp(GSHSCENE_ORDER_FLAT, 1, 2.0), fl(1), gop(), gp(GSHSCENE_ORDER_FLAT, 0, 2.0), fl(2),
      fl(3), gop(), gop()};
  EXPECT_EQ(closed(f, e, &ties), "G G f2 f3 g G f1 g g");
  std::vector<TieRow> want = {{1, 5, 2.0, GSHSCENE_TIE_LAYER}};
  EXPECT_EQ(ties, want);
}

// --- the list's own bookkeeping -------------------------------------------------------------------------------------

TEST(Order, TiesAreReadOnlyOfAClosedListAndAResetClearsThem) {
  Fix f;
  ListHolder l(f.s.store);
  record(l.list, f, {gp(GSHSCENE_ORDER_DEPTH), fl(1, 0, 1.0), fl(2, 0, 1.0), gop()});
  EXPECT_EQ(gshscene_list_tie_count(l.list), 0u) << "an open list has recorded none";
  const GSHSCENE_Tie * t = nullptr;
  EXPECT_EQ(gshscene_list_tie(l.list, 0, &t), GSHSCENE_ERR_INVALID);
  ASSERT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_tie_count(l.list), 1u);
  EXPECT_EQ(gshscene_list_tie(l.list, 0, &t), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_tie(l.list, 1, &t), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_tie(nullptr, 0, &t), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_tie(l.list, 0, nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_tie_count(nullptr), 0u);
  ASSERT_EQ(gshscene_list_reset(l.list), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_tie_count(l.list), 0u);
  record(l.list, f, {fl(1)});
  ASSERT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_tie_count(l.list), 0u) << "a reset list starts with none";
}

TEST(Order, TheTiesAreAnnotationAndInNeitherTheHashNorTheEquality) {
  Fix f;
  // The same paint order reached two ways: recorded sorted (no ties in a flat list) and recorded out of order and sorted by close.
  ListHolder sorted(f.s.store), declared(f.s.store);
  record(sorted.list, f, {gp(GSHSCENE_ORDER_DEPTH), fl(2, 0, 2.0), fl(1, 0, 1.0), fl(3, 0, 1.0), gop()});
  record(declared.list, f, {gp(GSHSCENE_ORDER_DEPTH), fl(3, 0, 1.0), fl(2, 0, 2.0), fl(1, 0, 1.0), gop()});
  ASSERT_EQ(gshscene_list_close(sorted.list), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(declared.list), GSHSCENE_OK);
  // The first has depth-1 units 1 then 3 (declared), the second 3 then 1: different paint orders, so not equal.
  EXPECT_EQ(shape_of(sorted.list), "G f2 f1 f3 g");
  EXPECT_EQ(shape_of(declared.list), "G f2 f3 f1 g");
  EXPECT_FALSE(gshscene_list_equal(sorted.list, declared.list));
  // A list and its copy as a picture keep the ties, and a list that differs only in them is equal and hashes the same.
  Ref picture;
  ASSERT_EQ(gshscene_store_intern_picture(f.s.store, sorted.list, &picture.resource), GSHSCENE_OK);
  const GSHSCENE_List * inner = gshscene_resource_picture(picture.get());
  EXPECT_EQ(ties_of(inner), ties_of(sorted.list));
  EXPECT_TRUE(gshscene_list_equal(inner, sorted.list));
  EXPECT_EQ(gshscene_list_hash(inner), gshscene_list_hash(sorted.list));
}

TEST(Order, AListThatNeedsNoOrderIsNotTouchedByClose) {
  Fix f;
  ListHolder l(f.s.store);
  record(l.list, f, {fl(3, 5, 1.0), cp(2, 3.0), fl(1, -1, 4.0), cop(), gp(GSHSCENE_ORDER_FLAT, 1, 2.0), fl(2), gop()});
  uint64_t before = gshscene_list_hash(l.list);
  ASSERT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_hash(l.list), before);
  EXPECT_EQ(shape_of(l.list), "f3 C f1 c G f2 g");
  EXPECT_EQ(gshscene_list_close(l.list), GSHSCENE_ERR_INVALID) << "already closed";
}

TEST(Order, CloseRefusesAnOpenPushAndLeavesTheListAsItWas) {
  Fix f;
  ListHolder l(f.s.store);
  record(l.list, f, {gp(GSHSCENE_ORDER_DEPTH), fl(2, 0, 1.0), fl(1, 0, 2.0)});
  EXPECT_EQ(gshscene_list_close(l.list), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_is_closed(l.list), 0);
  record(l.list, f, {gop()});
  EXPECT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
  EXPECT_EQ(shape_of(l.list), "G f1 f2 g");
}

// --- an allocation that fails in close -----------------------------------------------------------------------------

TEST(Order, AFailureAtEveryAllocationOfCloseLeavesTheListOpenUnchangedAndClosable) {
  std::vector<E> e = {fl(1), gp(GSHSCENE_ORDER_DEPTH), fl(2, 0, 1.0), fl(3, 1, 2.0), fl(4, 0, 2.0), fl(5, 0, 2.0), cp(0, 2.0), fl(6),
      cop(), gp(GSHSCENE_ORDER_OVERLAY), fl(7, 3), fl(8, 1), gop(), gop(), fl(9)};
  // How many allocations a clean close makes.
  long clean_calls = 0;
  std::string want;
  std::vector<TieRow> want_ties;
  {
    tracker::Tracker t;
    GSHSCENE_Allocator alloc = tracker::allocator_for(&t);
    Fix f;
    ListHolder l(f.s.store, nullptr, &alloc);
    record(l.list, f, e);
    long before = t.calls;
    ASSERT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
    clean_calls = t.calls - before;
    want = shape_of(l.list);
    want_ties = ties_of(l.list);
  }
  ASSERT_GT(clean_calls, 0);
  for (long k = 0; k < clean_calls; k++) {
    tracker::Tracker t;
    GSHSCENE_Allocator alloc = tracker::allocator_for(&t);
    {
      Fix f;
      ListHolder l(f.s.store, nullptr, &alloc);
      record(l.list, f, e);
      uint64_t hash = gshscene_list_hash(l.list);
      size_t count = gshscene_list_count(l.list);
      std::string declared = shape_of(l.list);
      t.fail_from = t.calls + k;
      EXPECT_EQ(gshscene_list_close(l.list), GSHSCENE_ERR_OOM) << "failure " << k;
      t.fail_from = -1;
      EXPECT_EQ(gshscene_list_is_closed(l.list), 0) << k;
      EXPECT_EQ(gshscene_list_hash(l.list), hash) << k;
      EXPECT_EQ(gshscene_list_count(l.list), count) << k;
      EXPECT_EQ(shape_of(l.list), declared) << k;
      ASSERT_EQ(gshscene_list_close(l.list), GSHSCENE_OK) << "closable again after failure " << k;
      EXPECT_EQ(shape_of(l.list), want) << k;
      EXPECT_EQ(ties_of(l.list), want_ties) << k;
    }
    EXPECT_TRUE(t.live.empty()) << "a leak after failure " << k;
  }
}

// --- a reference written another way --------------------------------------------------------------------------------

namespace {

/** The order of a declared-order description, by plain recursion over a tree: another reading of N11. */
struct Ref2 {
  struct Node {
    E head;
    std::vector<Node> inside;
    E tail;
    bool block = false;
  };
  static std::vector<Node> parse(const std::vector<E> & e, size_t & i, bool until_pop) {
    std::vector<Node> out;
    while (i < e.size()) {
      if (e[i].kind == 'c' || e[i].kind == 'g') {
        if (until_pop) {
          return out;
        }
        break;
      }
      Node n;
      n.head = e[i++];
      if (n.head.kind == 'C' || n.head.kind == 'G') {
        n.block = true;
        n.inside = parse(e, i, true);
        n.tail = e[i++];
      }
      out.push_back(std::move(n));
    }
    return out;
  }
  static void emit(std::vector<Node> seq, uint32_t context, std::vector<E> & out, std::vector<TieRow> & ties) {
    if (context == GSHSCENE_ORDER_DEPTH) {
      std::stable_sort(seq.begin(), seq.end(), [](const Node & a, const Node & b) {
        if (a.head.depth != b.head.depth) {
          return a.head.depth > b.head.depth;
        }
        return a.head.layer < b.head.layer;
      });
    } else if (context == GSHSCENE_ORDER_OVERLAY) {
      std::stable_sort(seq.begin(), seq.end(), [](const Node & a, const Node & b) { return a.head.layer < b.head.layer; });
    }
    const Node * previous = nullptr;
    size_t previous_at = 0;
    for (const Node & n : seq) {
      size_t at = out.size();
      if (context == GSHSCENE_ORDER_DEPTH && previous && previous->head.depth == n.head.depth) {
        ties.push_back({previous_at, at, n.head.depth,
            previous->head.layer != n.head.layer ? (int)GSHSCENE_TIE_LAYER : (int)GSHSCENE_TIE_DECLARED});
      }
      previous = &n;
      previous_at = at;
      out.push_back(n.head);
      if (n.block) {
        emit(n.inside, n.head.kind == 'G' ? n.head.order : context, out, ties);
        out.push_back(n.tail);
      }
    }
  }
};

std::string text_of(const std::vector<E> & e) {
  std::string out;
  for (const E & x : e) {
    if (!out.empty()) {
      out += ' ';
    }
    out += x.kind == 'f' ? "f" + std::to_string(x.id) : std::string(1, x.kind);
  }
  return out;
}

/** A random balanced description with small key ranges (so ties are common). */
std::vector<E> random_description(std::mt19937 & rng, int & next_id, int budget) {
  std::vector<E> out;
  std::function<void(int, int)> seq = [&](int n, int depth) {
    for (int i = 0; i < n; i++) {
      int pick = (int)(rng() % 8);
      int32_t layer = (int32_t)(rng() % 3) - 1;
      double d = (double)(rng() % 4) * 0.5 - 0.5;
      if (pick < 4 || depth >= 4) {
        out.push_back(fl(next_id++, layer, d));
      } else if (pick < 6) {
        out.push_back(gp((uint32_t)(rng() % 3), layer, d));
        seq((int)(rng() % 4), depth + 1);
        out.push_back(gop());
      } else {
        out.push_back(cp(layer, d));
        seq((int)(rng() % 3), depth + 1);
        out.push_back(cop());
      }
    }
  };
  seq(budget, 0);
  return out;
}

}  // namespace

TEST(Order, ManyRandomListsAgreeWithAReferenceWrittenAsRecursionOverATree) {
  Fix f;
  std::mt19937 rng(20261019);
  int total_ties = 0;
  for (int round = 0; round < 400; round++) {
    int next_id = 1;
    std::vector<E> declared = random_description(rng, next_id, 2 + (int)(rng() % 12));
    size_t i = 0;
    std::vector<Ref2::Node> tree = Ref2::parse(declared, i, false);
    ASSERT_EQ(i, declared.size());
    std::vector<E> want;
    std::vector<TieRow> want_ties;
    Ref2::emit(tree, GSHSCENE_ORDER_FLAT, want, want_ties);
    std::vector<TieRow> got_ties;
    std::string got = closed(f, declared, &got_ties);
    EXPECT_EQ(got, text_of(want)) << "round " << round << " from: " << text_of(declared);
    EXPECT_EQ(got_ties, want_ties) << "round " << round;
    total_ties += (int)got_ties.size();
  }
  EXPECT_GT(total_ties, 100) << "the descriptions are meant to produce ties";
}

TEST(Order, ALargeSequenceIsOrderedAndTheTiesAreInOrder) {
  Fix f;
  std::vector<E> e = {gp(GSHSCENE_ORDER_DEPTH)};
  std::mt19937 rng(7);
  for (int i = 0; i < 6000; i++) {
    e.push_back(fl(i + 1, (int32_t)(rng() % 3), (double)(rng() % 50)));
  }
  e.push_back(gop());
  std::vector<TieRow> ties;
  ListHolder l(f.s.store);
  record(l.list, f, e);
  ASSERT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
  double last_depth = 1e300;
  int32_t last_layer = -100;
  for (size_t i = 1; i <= 6000; i++) {
    const GSHSCENE_Item * item = nullptr;
    ASSERT_EQ(gshscene_list_item(l.list, i, &item), GSHSCENE_OK);
    EXPECT_TRUE(item->depth < last_depth || (item->depth == last_depth && item->layer >= last_layer)) << i;
    last_depth = item->depth;
    last_layer = item->layer;
  }
  ties = ties_of(l.list);
  EXPECT_GT(ties.size(), 5000u);
  for (size_t i = 1; i < ties.size(); i++) {
    EXPECT_LT(ties[i - 1].second, ties[i].second);
  }
}

// --- the retained scene: the walk orders, the emission records ------------------------------------------------------

namespace {

struct Scn {
  SceneHolder s;
  Ref sq{intern_square(s.store())};
  Ref p2{intern_fixture(s.store())};
};

GSHSCENE_Node shape_at(Scn & c, int id, int32_t layer, double depth) {
  GSHSCENE_Node n = shape_node(c.sq.get(), layer);
  n.depth = depth;
  n.paint = solid((double)id, 0, 0, 1);
  return n;
}

}  // namespace

TEST(OrderScene, ADepthGroupIsEmittedInPaintOrderWithItsTiesAndEqualsTheRecordedList) {
  Scn c;
  EditHolder e(c.s);
  GSHSCENE_Node g = group_node();
  g.ordering = GSHSCENE_ORDER_DEPTH;
  uint64_t gid = add(e.edit, GSHSCENE_ROOT_ID, g);
  // IDs ascend in creation order: a (depth 1), b (2, layer 1), c (2), d (2), e (3).
  add(e.edit, gid, shape_at(c, 1, 0, 1.0));
  add(e.edit, gid, shape_at(c, 2, 1, 2.0));
  add(e.edit, gid, shape_at(c, 3, 0, 2.0));
  add(e.edit, gid, shape_at(c, 4, 0, 2.0));
  add(e.edit, gid, shape_at(c, 5, 0, 3.0));
  VersionRef v;
  publish(c.s, e, &v);
  const GSHSCENE_List * list = nullptr;
  ASSERT_EQ(gshscene_version_list(v.get(), &list), GSHSCENE_OK);
  EXPECT_EQ(shape_of(list), "G f5 f3 f4 f2 f1 g");
  // The same drawn immediately, in the order the nodes were made, equals it; the ties differ only in their reason
  // (the scene broke the c-d tie by path where the recorded list had only the declared order).
  ListHolder want(c.s.store());
  GSHSCENE_Group dg = make_group(1.0, GSHSCENE_ORDER_DEPTH);
  ASSERT_EQ(gshscene_list_group_push(want.list, &dg, 0, 0.0), GSHSCENE_OK);
  const int ids[5] = {1, 2, 3, 4, 5};
  const int32_t layers[5] = {0, 1, 0, 0, 0};
  const double depths[5] = {1.0, 2.0, 2.0, 2.0, 3.0};
  for (int i = 0; i < 5; i++) {
    GSHSCENE_Paint p = solid((double)ids[i], 0, 0, 1);
    ASSERT_EQ(gshscene_list_fill(want.list, c.sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &p, layers[i], depths[i]), GSHSCENE_OK);
  }
  ASSERT_EQ(gshscene_list_group_pop(want.list), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(want.list), GSHSCENE_OK);
  EXPECT_TRUE(gshscene_list_equal(list, want.list));
  EXPECT_EQ(gshscene_list_hash(list), gshscene_list_hash(want.list));
  std::vector<TieRow> ties = ties_of(list);
  std::vector<TieRow> want_ties = {{2, 3, 2.0, GSHSCENE_TIE_PATH}, {3, 4, 2.0, GSHSCENE_TIE_LAYER}};
  EXPECT_EQ(ties, want_ties);
  std::vector<TieRow> recorded = ties_of(want.list);
  ASSERT_EQ(recorded.size(), 2u);
  EXPECT_EQ(recorded[0].first, ties[0].first);
  EXPECT_EQ(recorded[0].second, ties[0].second);
  EXPECT_EQ(recorded[0].reason, (int)GSHSCENE_TIE_DECLARED);
  EXPECT_EQ(recorded[1].reason, (int)GSHSCENE_TIE_LAYER);
}

TEST(OrderScene, TheTieBreakByInstancePathIsTheAscendingNodeIdOfSiblings) {
  Scn c;
  EditHolder e(c.s);
  GSHSCENE_Node g = group_node();
  g.ordering = GSHSCENE_ORDER_DEPTH;
  uint64_t gid = add(e.edit, GSHSCENE_ROOT_ID, g);
  // Created in an order unlike their position in the child list: the later ID is declared first.
  uint64_t first = add(e.edit, gid, shape_at(c, 1, 0, 1.0));
  uint64_t second = add(e.edit, gid, shape_at(c, 2, 0, 1.0), nullptr, 0);
  ASSERT_LT(first, second);
  VersionRef v;
  publish(c.s, e, &v);
  EXPECT_EQ(children_of(v.get(), gid), (std::vector<uint64_t>{second, first}));
  const GSHSCENE_List * list = nullptr;
  ASSERT_EQ(gshscene_version_list(v.get(), &list), GSHSCENE_OK);
  EXPECT_EQ(shape_of(list), "G f1 f2 g") << "the lower ID is painted first whatever the declared order";
  // And that is the path order of gshscene_instance_path_compare.
  const uint64_t a[] = {first};
  const uint64_t b[] = {second};
  EXPECT_LT(gshscene_instance_path_compare(a, 1, b, 1), 0);
  uint64_t node = 0;
  ASSERT_EQ(gshscene_version_item_node(v.get(), 0, 1, &node), GSHSCENE_OK);
  EXPECT_EQ(node, first);
  ASSERT_EQ(gshscene_version_item_node(v.get(), 0, 2, &node), GSHSCENE_OK);
  EXPECT_EQ(node, second);
}

TEST(OrderScene, TheVisitTheEmissionAndTheDamageSeeOneOrder) {
  Scn c;
  EditHolder e(c.s);
  GSHSCENE_Node g = group_node();
  g.ordering = GSHSCENE_ORDER_OVERLAY;
  uint64_t gid = add(e.edit, GSHSCENE_ROOT_ID, g);
  add(e.edit, gid, shape_at(c, 1, 3, 0));
  add(e.edit, gid, shape_at(c, 2, 1, 0));
  GSHSCENE_Node hidden = shape_at(c, 9, 0, 0);
  hidden.flags = GSHSCENE_NODE_HIDDEN;
  add(e.edit, gid, hidden);
  add(e.edit, gid, shape_at(c, 3, 2, 0));
  VersionRef v;
  publish(c.s, e, &v);
  std::vector<Occurrence> occ;
  ASSERT_EQ(gshscene_version_visit(v.get(), collect, &occ), 0);
  std::vector<int> visited;
  for (const Occurrence & o : occ) {
    if (o.kind == GSHSCENE_NODE_SHAPE) {
      visited.push_back((int)o.props.paint.rgba[0]);
    }
  }
  EXPECT_EQ(visited, (std::vector<int>{2, 3, 1})) << "overlay: by layer, the hidden node taking no part";
  const GSHSCENE_List * list = nullptr;
  ASSERT_EQ(gshscene_version_list(v.get(), &list), GSHSCENE_OK);
  EXPECT_EQ(shape_of(list), "G f2 f3 f1 g");
}

TEST(OrderScene, AHiddenNodeTakesNoPartInATie) {
  Scn c;
  EditHolder e(c.s);
  GSHSCENE_Node g = group_node();
  g.ordering = GSHSCENE_ORDER_DEPTH;
  uint64_t gid = add(e.edit, GSHSCENE_ROOT_ID, g);
  add(e.edit, gid, shape_at(c, 1, 0, 1.0));
  GSHSCENE_Node hidden = shape_at(c, 9, 0, 1.0);
  hidden.flags = GSHSCENE_NODE_HIDDEN;
  add(e.edit, gid, hidden);
  add(e.edit, gid, shape_at(c, 2, 0, 1.0));
  VersionRef v;
  publish(c.s, e, &v);
  const GSHSCENE_List * list = nullptr;
  ASSERT_EQ(gshscene_version_list(v.get(), &list), GSHSCENE_OK);
  EXPECT_EQ(shape_of(list), "G f1 f2 g");
  std::vector<TieRow> want = {{1, 2, 1.0, GSHSCENE_TIE_PATH}};
  EXPECT_EQ(ties_of(list), want) << "one tie between the two units that were drawn";
}

TEST(OrderScene, TheTiesOfNestedDepthGroupsComeInAscendingOrderOfTheirSecondItem) {
  Scn c;
  EditHolder e(c.s);
  GSHSCENE_Node g = group_node();
  g.ordering = GSHSCENE_ORDER_DEPTH;
  uint64_t gid = add(e.edit, GSHSCENE_ROOT_ID, g);
  GSHSCENE_Node inner = group_node();
  inner.ordering = GSHSCENE_ORDER_DEPTH;
  inner.depth = 2.0;
  uint64_t a = add(e.edit, gid, inner);
  uint64_t b = add(e.edit, gid, inner);
  add(e.edit, a, shape_at(c, 1, 0, 1.0));
  add(e.edit, a, shape_at(c, 2, 0, 1.0));
  add(e.edit, b, shape_at(c, 3, 0, 1.0));
  add(e.edit, b, shape_at(c, 4, 0, 1.0));
  VersionRef v;
  publish(c.s, e, &v);
  const GSHSCENE_List * list = nullptr;
  ASSERT_EQ(gshscene_version_list(v.get(), &list), GSHSCENE_OK);
  EXPECT_EQ(shape_of(list), "G G f1 f2 g G f3 f4 g g");
  // The tie of the two groups is found after the ties inside the second, and is still listed by its second item.
  std::vector<TieRow> want = {{2, 3, 1.0, GSHSCENE_TIE_PATH}, {1, 5, 2.0, GSHSCENE_TIE_PATH}, {6, 7, 1.0, GSHSCENE_TIE_PATH}};
  EXPECT_EQ(ties_of(list), want);
  // The recorded list that draws the same has the same ties in the same places (with the declared order as their reason).
  ListHolder rec(c.s.store());
  GSHSCENE_Group dg = make_group(1.0, GSHSCENE_ORDER_DEPTH);
  ASSERT_EQ(gshscene_list_group_push(rec.list, &dg, 0, 0.0), GSHSCENE_OK);
  for (int k = 0; k < 2; k++) {
    ASSERT_EQ(gshscene_list_group_push(rec.list, &dg, 0, 2.0), GSHSCENE_OK);
    for (int i = 0; i < 2; i++) {
      GSHSCENE_Paint p = solid((double)(2 * k + i + 1), 0, 0, 1);
      ASSERT_EQ(gshscene_list_fill(rec.list, c.sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &p, 0, 1.0), GSHSCENE_OK);
    }
    ASSERT_EQ(gshscene_list_group_pop(rec.list), GSHSCENE_OK);
  }
  ASSERT_EQ(gshscene_list_group_pop(rec.list), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(rec.list), GSHSCENE_OK);
  EXPECT_TRUE(gshscene_list_equal(list, rec.list));
  std::vector<TieRow> got = ties_of(rec.list);
  ASSERT_EQ(got.size(), 3u);
  for (size_t i = 0; i < 3; i++) {
    EXPECT_EQ(got[i].first, want[i].first);
    EXPECT_EQ(got[i].second, want[i].second);
    EXPECT_EQ(got[i].reason, (int)GSHSCENE_TIE_DECLARED);
  }
}

TEST(OrderScene, AContainerInAnOrderedGroupKeepsItsChildrenTogether) {
  Scn c;
  EditHolder e(c.s);
  GSHSCENE_Node g = group_node();
  g.ordering = GSHSCENE_ORDER_DEPTH;
  uint64_t gid = add(e.edit, GSHSCENE_ROOT_ID, g);
  GSHSCENE_Node box = group_node();  // a plain flat container at depth 2 holding two shapes of other depths
  box.depth = 2.0;
  uint64_t box_id = add(e.edit, gid, box);
  add(e.edit, box_id, shape_at(c, 1, 0, 9.0));
  add(e.edit, box_id, shape_at(c, 2, 0, -9.0));
  add(e.edit, gid, shape_at(c, 3, 0, 1.0));
  add(e.edit, gid, shape_at(c, 4, 0, 3.0));
  VersionRef v;
  publish(c.s, e, &v);
  const GSHSCENE_List * list = nullptr;
  ASSERT_EQ(gshscene_version_list(v.get(), &list), GSHSCENE_OK);
  // The container is one unit, so it is a group push in the list (a flat context of its own), and what is inside is not sorted.
  EXPECT_EQ(shape_of(list), "G f4 G f1 f2 g f3 g");
  // The recorded list that says the same is equal to it.
  Fix f;
  ListHolder want(c.s.store());
  GSHSCENE_Group dg = make_group(1.0, GSHSCENE_ORDER_DEPTH);
  GSHSCENE_Group flat = make_group(1.0);
  ASSERT_EQ(gshscene_list_group_push(want.list, &dg, 0, 0.0), GSHSCENE_OK);
  GSHSCENE_Paint p4 = solid(4, 0, 0, 1), p1 = solid(1, 0, 0, 1), p2 = solid(2, 0, 0, 1), p3 = solid(3, 0, 0, 1);
  ASSERT_EQ(gshscene_list_fill(want.list, c.sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &p3, 0, 1.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_push(want.list, &flat, 0, 2.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(want.list, c.sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &p1, 0, 9.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(want.list, c.sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &p2, 0, -9.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_pop(want.list), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(want.list, c.sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &p4, 0, 3.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_pop(want.list), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(want.list), GSHSCENE_OK);
  EXPECT_TRUE(gshscene_list_equal(list, want.list));
  EXPECT_EQ(gshscene_list_hash(list), gshscene_list_hash(want.list));
}

TEST(OrderScene, AFlatGroupInAFlatSequenceIsStillOnlyAContainerAsBefore) {
  Scn c;
  EditHolder e(c.s);
  uint64_t box = add(e.edit, GSHSCENE_ROOT_ID, group_node());
  add(e.edit, box, shape_at(c, 1, 5, 1.0));
  add(e.edit, box, shape_at(c, 2, -5, 2.0));
  VersionRef v;
  publish(c.s, e, &v);
  const GSHSCENE_List * list = nullptr;
  ASSERT_EQ(gshscene_version_list(v.get(), &list), GSHSCENE_OK);
  EXPECT_EQ(shape_of(list), "f1 f2") << "no push: nothing declared that a push would carry";
  EXPECT_EQ(gshscene_list_tie_count(list), 0u);
}

TEST(OrderScene, AnInstanceOfADepthSymbolSharesOnePictureWhoseItemsAreOrdered) {
  Scn c;
  EditHolder e(c.s);
  GSHSCENE_Node def = group_node();
  def.ordering = GSHSCENE_ORDER_DEPTH;
  def.flags = GSHSCENE_NODE_HIDDEN;
  uint64_t did = add(e.edit, GSHSCENE_ROOT_ID, def);
  add(e.edit, did, shape_at(c, 1, 0, 1.0));
  add(e.edit, did, shape_at(c, 2, 0, 5.0));
  add(e.edit, did, shape_at(c, 3, 0, 5.0));
  GSHSCENE_Node i1 = instance_node(did);
  i1.depth = 4.0;
  add(e.edit, GSHSCENE_ROOT_ID, i1);
  add(e.edit, GSHSCENE_ROOT_ID, instance_node(did));
  VersionRef v;
  publish(c.s, e, &v);
  const GSHSCENE_Resource * picture = nullptr;
  ASSERT_EQ(gshscene_version_picture(v.get(), did, &picture), GSHSCENE_OK);
  const GSHSCENE_List * inner = gshscene_resource_picture(picture);
  EXPECT_EQ(shape_of(inner), "G f2 f3 f1 g");
  std::vector<TieRow> want = {{1, 2, 5.0, GSHSCENE_TIE_PATH}};
  EXPECT_EQ(ties_of(inner), want);
  const GSHSCENE_List * top = nullptr;
  ASSERT_EQ(gshscene_version_list(v.get(), &top), GSHSCENE_OK);
  EXPECT_EQ(shape_of(top), "I I");
  const GSHSCENE_Item * a = nullptr;
  const GSHSCENE_Item * b = nullptr;
  ASSERT_EQ(gshscene_list_item(top, 0, &a), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_item(top, 1, &b), GSHSCENE_OK);
  EXPECT_EQ(a->resource, b->resource) << "one picture per target";
  EXPECT_EQ(a->depth, 4.0);
}

TEST(OrderScene, TheVersionsListCarriesTheVersionsForegroundAndAPictureDoesNot) {
  Scn c;
  EditHolder e(c.s);
  GSHSCENE_Color fg = {{0.25, 0.5, 0.75, 0.5}, srgb_space()};
  ASSERT_EQ(gshscene_edit_set_foreground(e.edit, &fg), GSHSCENE_OK);
  GSHSCENE_Node def = group_node();
  def.flags = GSHSCENE_NODE_HIDDEN;
  uint64_t did = add(e.edit, GSHSCENE_ROOT_ID, def);
  GSHSCENE_Node s = shape_at(c, 1, 0, 0);
  s.paint = solid(0, 0, 0, 0.5);
  s.paint.kind = GSHSCENE_PAINT_FOREGROUND;
  add(e.edit, did, s);
  add(e.edit, GSHSCENE_ROOT_ID, instance_node(did));
  VersionRef v;
  publish(c.s, e, &v);
  const GSHSCENE_List * list = nullptr;
  ASSERT_EQ(gshscene_version_list(v.get(), &list), GSHSCENE_OK);
  GSHSCENE_Color got;
  ASSERT_EQ(gshscene_list_foreground(list, &got), GSHSCENE_OK);
  EXPECT_EQ(got.rgba[0], 0.25);
  EXPECT_EQ(got.rgba[3], 0.5);
  EXPECT_EQ(got.space.transfer, GCOL_TRANSFER_SRGB);
  const GSHSCENE_Resource * picture = nullptr;
  ASSERT_EQ(gshscene_version_picture(v.get(), did, &picture), GSHSCENE_OK);
  GSHSCENE_Color pic_fg;
  ASSERT_EQ(gshscene_list_foreground(gshscene_resource_picture(picture), &pic_fg), GSHSCENE_OK);
  GSHSCENE_Color black;
  gshscene_color_init(&black);
  EXPECT_EQ(pic_fg.rgba[0], black.rgba[0]);
  EXPECT_EQ(pic_fg.rgba[3], 1.0) << "a picture has the default foreground: any list may draw it";
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
