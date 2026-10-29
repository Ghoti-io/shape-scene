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
 * The mask resource (mask.h, MK1 to MK4, MK9, MK13): the intern matrix, equality by content, the order of the refusals, the
 * limits at their bounds and one past, the hash and dump against the independent generator, the transfer evaluated against
 * hand-computed values, the coefficient sets and what a mask needs of a sink.
 */

#include "scene_fixtures.h"

#include "../known_answers.h"
#include "../alloc_tracker.h"

#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <utility>
#include <vector>

using namespace scene;

namespace {

const double kNaN = std::numeric_limits<double>::quiet_NaN();
const double kInf = std::numeric_limits<double>::infinity();

/** The picture fixture of tools/gen-scene-vectors.py (known::kPictureHash). */
const GSHSCENE_Resource * fixture_picture(GSHSCENE_Store * store, const GSHSCENE_Resource * sq, const GSHSCENE_Resource * p2) {
  GSHGEOM_Map tr = translate(1.5, -2.25);
  GSHSCENE_List * inner = nullptr;
  gshscene_list_create(nullptr, nullptr, store, &inner);
  gshscene_list_fill(inner, sq, nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0);
  GSHSCENE_Group half = make_group(0.5);
  gshscene_list_group_push(inner, &half, 0, 0);
  GSHSCENE_Paint orange = solid(1.0, 0.5, 0.25, 0.75);
  gshscene_list_fill(inner, p2, &tr, GSHGEOM_FILL_EVEN_ODD, &orange, 2, 0);
  gshscene_list_group_pop(inner);
  gshscene_list_close(inner);
  const GSHSCENE_Resource * picture = nullptr;
  gshscene_store_intern_picture(store, inner, &picture);
  gshscene_list_destroy(inner);
  return picture;
}

/** A store with the fixture picture. */
struct World {
  StoreHolder s;
  explicit World(GSHSCENE_Index_Hash_Fn index_hash = nullptr) : s(nullptr, nullptr, index_hash) {}
  Ref sq{intern_square(s.store)};
  Ref p2{intern_fixture(s.store)};
  Ref pic{fixture_picture(s.store, sq.get(), p2.get())};
  GSHSCENE_Mask alpha() const {
    GSHSCENE_Mask m;
    std::memset(&m, 0, sizeof m);
    m.picture = pic.get();
    m.mode = GSHSCENE_MASK_ALPHA;
    return m;
  }
  GSHSCENE_Mask luminance() const {
    GSHSCENE_Mask m = alpha();
    m.mode = GSHSCENE_MASK_LUMINANCE;
    return m;
  }
  GSHSCENE_Result intern(const GSHSCENE_Mask & m, const GSHSCENE_Resource ** out = nullptr) {
    const GSHSCENE_Resource * r = nullptr;
    GSHSCENE_Result result = gshscene_store_intern_mask(s.store, &m, &r);
    if (out) {
      *out = r;
    } else {
      gshscene_resource_release(r);
    }
    return result;
  }
};

/** A stitching of @p count equal pieces of the identity, the boundaries `i / count` (exact for a power of two). */
struct Stitch {
  std::vector<double> bounds;
  std::vector<GSHSCENE_Transfer_Piece> pieces;
  explicit Stitch(size_t count) {
    for (size_t i = 0; i <= count; i++) {
      bounds.push_back(i == count ? 1.0 : (double)i / (double)count);
    }
    for (size_t i = 0; i < count; i++) {
      GSHSCENE_Transfer_Piece p;
      std::memset(&p, 0, sizeof p);
      p.lo = bounds[i];
      p.hi = bounds[i + 1];
      p.e0 = 0.0;
      p.e1 = 1.0;
      p.leaf_kind = GSHSCENE_TRANSFER_IDENTITY;
      pieces.push_back(p);
    }
  }
  void apply(GSHSCENE_Transfer * t) const {
    std::memset(t, 0, sizeof *t);
    t->kind = GSHSCENE_TRANSFER_STITCHING;
    t->piece_count = pieces.size();
    t->pieces = pieces.data();
  }
};

GSHSCENE_Transfer_Piece piece(double lo, double hi, double e0, double e1) {
  GSHSCENE_Transfer_Piece p;
  std::memset(&p, 0, sizeof p);
  p.lo = lo;
  p.hi = hi;
  p.e0 = e0;
  p.e1 = e1;
  p.leaf_kind = GSHSCENE_TRANSFER_IDENTITY;
  return p;
}

uint64_t bits(double x) {
  uint64_t b;
  std::memcpy(&b, &x, sizeof b);
  return b;
}

/** The transfer of a mask that has only it, at @p x. */
double transfer_at(World & w, const GSHSCENE_Transfer & t, double x) {
  GSHSCENE_Mask m = w.alpha();
  m.transfer = t;
  const GSHSCENE_Resource * r = nullptr;
  EXPECT_EQ(w.intern(m, &r), GSHSCENE_OK);
  Ref hold(r);
  double through_mask = gshscene_mask_transfer(r, x);
  EXPECT_EQ(bits(through_mask), bits(gshscene_transfer_eval(&t, x))) << "a mask and a bare transfer evaluate alike";
  return through_mask;
}

}  // namespace

// --- MK1: the kind, the known answers ---------------------------------------------------------------------------------

TEST(Mask, IsKindElevenAndNamed) {
  EXPECT_EQ((int)GSHSCENE_RESOURCE_MASK, 11);
  EXPECT_STREQ(gshscene_resource_kind_string(GSHSCENE_RESOURCE_MASK), "mask");
  EXPECT_EQ((int)GSHSCENE_MASK_ALPHA, 1);
  EXPECT_EQ((int)GSHSCENE_MASK_LUMINANCE, 2);
  EXPECT_EQ((int)GSHSCENE_MASK_COEFF_W3C, 0);
  EXPECT_EQ((int)GSHSCENE_MASK_COEFF_REC709, 1);
  EXPECT_EQ((int)GSHSCENE_MASK_COEFF_LUM, 2);
  EXPECT_EQ((int)GSHSCENE_MASK_COEFF_SPACE_Y, 3);
  EXPECT_EQ((int)GSHSCENE_MASK_ACTS_ON_OPACITY, 0);
  EXPECT_EQ((int)GSHSCENE_MASK_ACTS_ON_SHAPE, 1);
  EXPECT_EQ((int)GSHSCENE_TRANSFER_IDENTITY, 0);
  EXPECT_EQ((int)GSHSCENE_TRANSFER_EXPONENTIAL, 1);
  EXPECT_EQ((int)GSHSCENE_TRANSFER_TABLE, 2);
  EXPECT_EQ((int)GSHSCENE_TRANSFER_STITCHING, 3);
  EXPECT_EQ(GSHSCENE_MASK_MAX_PIECES, 256u);
  EXPECT_EQ(GSHSCENE_MASK_MAX_SAMPLES, 4096u);
}

TEST(Mask, HashAndDumpAreWhatTheIndependentGeneratorSays) {
  World w;
  ASSERT_EQ(gshscene_resource_hash(w.pic.get()), known::kPictureHash);
  const char * const dumps[] = {known::kMaskDump0, known::kMaskDump1, known::kMaskDump2, known::kMaskDump3,
      known::kMaskDump4, known::kMaskDump5};
  for (int i = 0; i < 6; i++) {
    Ref mask(known_mask(w.s.store, w.pic.get(), i));
    ASSERT_NE(mask.get(), nullptr) << i;
    EXPECT_EQ(gshscene_resource_kind(mask.get()), GSHSCENE_RESOURCE_MASK);
    EXPECT_EQ(gshscene_resource_hash(mask.get()), known::kMaskHashes[i]) << i;
    EXPECT_EQ(dump_resource(mask.get()), dumps[i]) << i;
  }
}

TEST(Mask, TheDumpNamesThePictureByHashAndNeverItsItems) {
  World w;
  Ref mask(known_mask(w.s.store, w.pic.get(), 0));
  std::string dump = dump_resource(mask.get());
  EXPECT_NE(dump.find("picture=5949280bf106409b"), std::string::npos);
  EXPECT_EQ(dump.find("gshscene.list"), std::string::npos);
  EXPECT_EQ(dump.find("\nG "), std::string::npos);
}

// --- MK1: equality is content ----------------------------------------------------------------------------------------------

TEST(Mask, EqualContentIsOneHandleAndNearlyEqualContentIsNot) {
  World w;
  GSHSCENE_Mask base = w.luminance();
  base.coefficients = GSHSCENE_MASK_COEFF_LUM;
  base.space = linear_srgb_space();
  base.render_space = srgb_space();
  base.backdrop[0] = 0.25;
  base.backdrop[1] = 0.5;
  base.backdrop[2] = 0.75;
  base.backdrop_space = srgb_space();
  static const double samples[3] = {0.0, 0.5, 1.0};
  base.transfer.kind = GSHSCENE_TRANSFER_TABLE;
  base.transfer.sample_count = 3;
  base.transfer.samples = samples;
  const GSHSCENE_Resource * a = nullptr;
  const GSHSCENE_Resource * b = nullptr;
  ASSERT_EQ(w.intern(base, &a), GSHSCENE_OK);
  ASSERT_EQ(w.intern(base, &b), GSHSCENE_OK);
  EXPECT_EQ(a, b) << "equal content is one handle";
  GSHSCENE_Store_Stats stats;
  ASSERT_EQ(gshscene_store_stats(w.s.store, &stats), GSHSCENE_OK);
  EXPECT_GE(stats.hits, 1u);
  // The same content from other memory (the arrays are copied, not kept).
  double again[3] = {0.0, 0.5, 1.0};
  GSHSCENE_Mask copy = base;
  copy.transfer.samples = again;
  const GSHSCENE_Resource * c = nullptr;
  ASSERT_EQ(w.intern(copy, &c), GSHSCENE_OK);
  EXPECT_EQ(a, c);
  GSHSCENE_Mask read;
  ASSERT_EQ(gshscene_resource_mask(a, &read), GSHSCENE_OK);
  EXPECT_NE(read.transfer.samples, again);
  EXPECT_NE(read.transfer.samples, samples);
  // One difference at a time: each is a different handle with a different hash.
  struct Variant {
    const char * what;
    GSHSCENE_Mask m;
    std::vector<double> samples;
  };
  std::vector<Variant> variants;
  auto add = [&](const char * what, auto change) {
    Variant v{what, base, {samples[0], samples[1], samples[2]}};
    change(v);
    if (v.m.transfer.kind == GSHSCENE_TRANSFER_TABLE) {
      v.m.transfer.samples = v.samples.data();
    }
    variants.push_back(std::move(v));
  };
  add("one bit of a sample", [](Variant & v) {
    uint64_t b = bits(0.5) + 1;
    std::memcpy(&v.samples[1], &b, sizeof b);
  });
  add("the last sample", [](Variant & v) { v.samples[2] = 0.75; });
  add("a sample count", [](Variant & v) {
    v.samples.push_back(1.0);
    v.m.transfer.sample_count = 4;
  });
  add("acts_on", [](Variant & v) { v.m.acts_on = GSHSCENE_MASK_ACTS_ON_SHAPE; });
  add("the coefficients", [](Variant & v) { v.m.coefficients = GSHSCENE_MASK_COEFF_REC709; });
  add("the mode", [](Variant & v) {
    v.m.mode = GSHSCENE_MASK_ALPHA;
    v.m.coefficients = 0;
    v.m.space = GSHSCENE_Color_Space{};
    v.m.backdrop[0] = v.m.backdrop[1] = v.m.backdrop[2] = 0.0;
    v.m.backdrop_space = GSHSCENE_Color_Space{};
  });
  add("the luminance space", [](Variant & v) { v.m.space = srgb_space(); });
  add("the render space", [](Variant & v) { v.m.render_space = linear_srgb_space(); });
  add("a gamma", [](Variant & v) { v.m.space = space_of(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_GAMMA, 2.2); });
  add("a backdrop component", [](Variant & v) { v.m.backdrop[2] = 0.5; });
  add("the backdrop's space", [](Variant & v) { v.m.backdrop_space = linear_srgb_space(); });
  add("the transfer kind", [](Variant & v) {
    v.m.transfer = GSHSCENE_Transfer{};
    v.m.transfer.kind = GSHSCENE_TRANSFER_EXPONENTIAL;
    v.m.transfer.c1 = 1.0;
    v.m.transfer.n = 1.0;
  });
  std::vector<const GSHSCENE_Resource *> handles;
  std::vector<Ref> hold;
  for (const Variant & v : variants) {
    const GSHSCENE_Resource * r = nullptr;
    ASSERT_EQ(w.intern(v.m, &r), GSHSCENE_OK) << v.what;
    hold.emplace_back(r);
    EXPECT_NE(r, a) << v.what;
    EXPECT_NE(gshscene_resource_hash(r), gshscene_resource_hash(a)) << v.what;
    handles.push_back(r);
  }
  for (size_t i = 0; i < handles.size(); i++) {
    for (size_t j = i + 1; j < handles.size(); j++) {
      EXPECT_NE(handles[i], handles[j]) << variants[i].what << " / " << variants[j].what;
    }
  }
  // The picture is part of the content.
  Ref other_pic(intern_square(w.s.store, 7.0));
  GSHSCENE_List * l = nullptr;
  ASSERT_EQ(gshscene_list_create(nullptr, nullptr, w.s.store, &l), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(l, other_pic.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  const GSHSCENE_Resource * second_picture = nullptr;
  ASSERT_EQ(gshscene_store_intern_picture(w.s.store, l, &second_picture), GSHSCENE_OK);
  gshscene_list_destroy(l);
  Ref second(second_picture);
  GSHSCENE_Mask on_other = base;
  on_other.picture = second.get();
  const GSHSCENE_Resource * d = nullptr;
  ASSERT_EQ(w.intern(on_other, &d), GSHSCENE_OK);
  EXPECT_NE(d, a);
  gshscene_resource_release(d);
  gshscene_resource_release(a);
  gshscene_resource_release(b);
  gshscene_resource_release(c);
}

TEST(Mask, NegativeZeroIsSpelledAsPositiveZero) {
  World w;
  GSHSCENE_Mask plain = w.luminance();
  GSHSCENE_Mask negative = w.luminance();
  negative.backdrop[0] = -0.0;
  negative.backdrop[1] = -0.0;
  negative.backdrop[2] = -0.0;
  const GSHSCENE_Resource * a = nullptr;
  const GSHSCENE_Resource * b = nullptr;
  ASSERT_EQ(w.intern(plain, &a), GSHSCENE_OK);
  ASSERT_EQ(w.intern(negative, &b), GSHSCENE_OK);
  EXPECT_EQ(a, b);
  GSHSCENE_Mask read;
  ASSERT_EQ(gshscene_resource_mask(b, &read), GSHSCENE_OK);
  EXPECT_EQ(bits(read.backdrop[0]), 0u);
  gshscene_resource_release(a);
  gshscene_resource_release(b);
  // The same in a transfer, and the other way round (the second intern sees the first's spelling).
  static const double minus_zero[2] = {-0.0, 1.0};
  static const double plus_zero[2] = {0.0, 1.0};
  GSHSCENE_Mask t1 = w.alpha();
  t1.transfer.kind = GSHSCENE_TRANSFER_TABLE;
  t1.transfer.sample_count = 2;
  t1.transfer.samples = minus_zero;
  GSHSCENE_Mask t2 = t1;
  t2.transfer.samples = plus_zero;
  ASSERT_EQ(w.intern(t1, &a), GSHSCENE_OK);
  ASSERT_EQ(w.intern(t2, &b), GSHSCENE_OK);
  EXPECT_EQ(a, b);
  ASSERT_EQ(gshscene_resource_mask(a, &read), GSHSCENE_OK);
  EXPECT_EQ(bits(read.transfer.samples[0]), 0u);
  gshscene_resource_release(a);
  gshscene_resource_release(b);
  GSHSCENE_Mask e1 = w.alpha();
  e1.transfer.kind = GSHSCENE_TRANSFER_EXPONENTIAL;
  e1.transfer.c0 = -0.0;
  e1.transfer.c1 = 1.0;
  e1.transfer.n = 2.0;
  GSHSCENE_Mask e2 = e1;
  e2.transfer.c0 = 0.0;
  ASSERT_EQ(w.intern(e2, &a), GSHSCENE_OK);
  ASSERT_EQ(w.intern(e1, &b), GSHSCENE_OK);
  EXPECT_EQ(a, b);
  gshscene_resource_release(a);
  gshscene_resource_release(b);
}

namespace {

/** A mask with the arrays its transfer points at, copyable: a change is made to the copy and bound again. */
struct Owned {
  GSHSCENE_Mask m{};
  std::vector<double> samples;
  std::vector<GSHSCENE_Transfer_Piece> pieces;
  std::vector<std::vector<double>> piece_samples;
  Owned() = default;
  Owned(const Owned & o) : m(o.m), samples(o.samples), pieces(o.pieces), piece_samples(o.piece_samples) { bind(); }
  Owned & operator=(const Owned & o) {
    m = o.m;
    samples = o.samples;
    pieces = o.pieces;
    piece_samples = o.piece_samples;
    bind();
    return *this;
  }
  void bind() {
    m.transfer.sample_count = samples.size();
    m.transfer.samples = samples.empty() ? nullptr : samples.data();
    m.transfer.piece_count = pieces.size();
    m.transfer.pieces = pieces.empty() ? nullptr : pieces.data();
    piece_samples.resize(pieces.size());
    for (size_t i = 0; i < pieces.size(); i++) {
      pieces[i].sample_count = piece_samples[i].size();
      pieces[i].samples = piece_samples[i].empty() ? nullptr : piece_samples[i].data();
    }
  }
};

typedef std::pair<const char *, std::function<void(Owned &)>> Change;

/** @p base is one handle however it is spelled, and each change (one field) is another handle, whatever the hash says. */
void expect_each_change_is_another_mask(World & w, const Owned & base, const std::vector<Change> & changes) {
  const GSHSCENE_Resource * a = nullptr;
  ASSERT_EQ(w.intern(base.m, &a), GSHSCENE_OK);
  Ref hold(a);
  Owned same = base;  // the same content from other memory
  const GSHSCENE_Resource * again = nullptr;
  ASSERT_EQ(w.intern(same.m, &again), GSHSCENE_OK);
  Ref hold_again(again);
  EXPECT_EQ(again, a) << "equal content is one handle even when every hash collides";
  std::vector<Ref> held;
  std::vector<const GSHSCENE_Resource *> seen = {a};
  for (const Change & c : changes) {
    Owned v = base;
    c.second(v);
    v.bind();
    const GSHSCENE_Resource * r = nullptr;
    ASSERT_EQ(w.intern(v.m, &r), GSHSCENE_OK) << c.first;
    held.emplace_back(r);
    for (const GSHSCENE_Resource * earlier : seen) {
      EXPECT_NE(r, earlier) << c.first;
    }
    seen.push_back(r);
  }
}

}  // namespace

// MK1: a hash never decides equality.  With every handle's index forced to the same value the store compares content alone, so
// each field of the content (and each field of the transfer, its pieces and their leaves) must be compared on its own.
TEST(Mask, EqualityIsDecidedFieldByFieldWhenEveryIndexCollides) {
  World w(all_zero);
  Ref second_square(intern_square(w.s.store, 7.0));
  GSHSCENE_List * l = nullptr;
  ASSERT_EQ(gshscene_list_create(nullptr, nullptr, w.s.store, &l), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_fill(l, second_square.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0.0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  const GSHSCENE_Resource * raw_picture = nullptr;
  ASSERT_EQ(gshscene_store_intern_picture(w.s.store, l, &raw_picture), GSHSCENE_OK);
  gshscene_list_destroy(l);
  Ref second_picture(raw_picture);
  // The fields every mask has, and a table.
  Owned a;
  a.m = w.luminance();
  a.m.coefficients = GSHSCENE_MASK_COEFF_LUM;
  a.m.space = linear_srgb_space();
  a.m.render_space = srgb_space();
  a.m.backdrop[0] = 0.25;
  a.m.backdrop[1] = 0.5;
  a.m.backdrop[2] = 0.75;
  a.m.backdrop_space = srgb_space();
  a.m.transfer.kind = GSHSCENE_TRANSFER_TABLE;
  a.samples = {0.0, 0.5, 1.0};
  a.bind();
  expect_each_change_is_another_mask(w, a, {
      {"the picture", [&](Owned & v) { v.m.picture = second_picture.get(); }},
      {"the mode", [](Owned & v) {
         v.m.mode = GSHSCENE_MASK_ALPHA;
         v.m.coefficients = 0;
         v.m.space = GSHSCENE_Color_Space{};
         v.m.backdrop[0] = v.m.backdrop[1] = v.m.backdrop[2] = 0.0;
         v.m.backdrop_space = GSHSCENE_Color_Space{};
       }},
      {"the coefficients", [](Owned & v) { v.m.coefficients = GSHSCENE_MASK_COEFF_SPACE_Y; }},
      {"acts_on", [](Owned & v) { v.m.acts_on = GSHSCENE_MASK_ACTS_ON_SHAPE; }},
      {"the luminance space", [](Owned & v) { v.m.space = srgb_space(); }},
      {"the render space", [](Owned & v) { v.m.render_space = linear_srgb_space(); }},
      {"the backdrop's space", [](Owned & v) { v.m.backdrop_space = linear_srgb_space(); }},
      {"backdrop red", [](Owned & v) { v.m.backdrop[0] = 0.125; }},
      {"backdrop green", [](Owned & v) { v.m.backdrop[1] = 0.125; }},
      {"backdrop blue", [](Owned & v) { v.m.backdrop[2] = 0.125; }},
      {"the transfer kind (identity)", [](Owned & v) {
         v.m.transfer.kind = GSHSCENE_TRANSFER_IDENTITY;
         v.samples.clear();
       }},
      {"the transfer kind (exponential)", [](Owned & v) {
         v.m.transfer.kind = GSHSCENE_TRANSFER_EXPONENTIAL;
         v.m.transfer.c1 = 0.5;
         v.m.transfer.n = 0.5;
         v.samples.clear();
       }},
      {"a sample added", [](Owned & v) { v.samples.push_back(1.0); }},
      {"a sample removed", [](Owned & v) { v.samples.pop_back(); }},
      {"the first sample", [](Owned & v) { v.samples[0] = 0.125; }},
      {"the middle sample", [](Owned & v) { v.samples[1] = 0.625; }},
      {"the last sample", [](Owned & v) { v.samples[2] = 0.875; }},
  });
  // Pairs that differ in one field only, with nothing else to tell them apart: the mode (an alpha mask has no color to differ
  // in), the transfer's kind (an identity and an exponential of all zeros) and a leaf's kind.
  Owned plain;
  plain.m = w.alpha();
  plain.bind();
  expect_each_change_is_another_mask(w, plain, {
      {"the mode alone", [](Owned & v) { v.m.mode = GSHSCENE_MASK_LUMINANCE; }},
      {"the transfer's kind alone", [](Owned & v) { v.m.transfer.kind = GSHSCENE_TRANSFER_EXPONENTIAL; }},
  });
  Owned one_piece;
  one_piece.m = w.alpha();
  one_piece.m.transfer.kind = GSHSCENE_TRANSFER_STITCHING;
  one_piece.pieces = {piece(0.0, 1.0, 0.0, 1.0)};
  one_piece.bind();
  expect_each_change_is_another_mask(w, one_piece, {
      {"a leaf's kind alone", [](Owned & v) { v.pieces[0].leaf_kind = GSHSCENE_TRANSFER_EXPONENTIAL; }},
      {"a piece's count of samples", [](Owned & v) {
         v.pieces[0].leaf_kind = GSHSCENE_TRANSFER_TABLE;
         v.piece_samples[0] = {0.0, 1.0};
       }},
  });
  // An exponential.
  Owned b;
  b.m = w.alpha();
  b.m.transfer.kind = GSHSCENE_TRANSFER_EXPONENTIAL;
  b.m.transfer.c0 = 0.125;
  b.m.transfer.c1 = 0.875;
  b.m.transfer.n = 2.0;
  b.bind();
  expect_each_change_is_another_mask(w, b, {
      {"c0", [](Owned & v) { v.m.transfer.c0 = 0.25; }},
      {"c1", [](Owned & v) { v.m.transfer.c1 = 0.75; }},
      {"the exponent", [](Owned & v) { v.m.transfer.n = 3.0; }},
  });
  // A stitching of two pieces: an exponential and a table.
  Owned c;
  c.m = w.alpha();
  c.m.transfer.kind = GSHSCENE_TRANSFER_STITCHING;
  GSHSCENE_Transfer_Piece first = piece(0.0, 0.5, 0.25, 0.75);
  first.leaf_kind = GSHSCENE_TRANSFER_EXPONENTIAL;
  first.c0 = 0.125;
  first.c1 = 0.875;
  first.n = 2.0;
  GSHSCENE_Transfer_Piece last = piece(0.5, 1.0, 0.0, 1.0);
  last.leaf_kind = GSHSCENE_TRANSFER_TABLE;
  c.pieces = {first, last};
  c.piece_samples = {{}, {0.25, 0.5, 1.0}};
  c.bind();
  expect_each_change_is_another_mask(w, c, {
      {"a piece added", [](Owned & v) {
         v.pieces[1].hi = 0.75;
         GSHSCENE_Transfer_Piece extra = piece(0.75, 1.0, 0.0, 1.0);
         v.pieces.push_back(extra);
         v.piece_samples.push_back({});
       }},
      {"a bound between pieces", [](Owned & v) {
         v.pieces[0].hi = 0.25;
         v.pieces[1].lo = 0.25;
       }},
      {"the first piece's first encode", [](Owned & v) { v.pieces[0].e0 = 0.5; }},
      {"the first piece's last encode", [](Owned & v) { v.pieces[0].e1 = 1.0; }},
      {"the last piece's first encode", [](Owned & v) { v.pieces[1].e0 = 0.125; }},
      {"the last piece's last encode", [](Owned & v) { v.pieces[1].e1 = 0.875; }},
      {"the first piece's leaf kind", [](Owned & v) {
         v.pieces[0].leaf_kind = GSHSCENE_TRANSFER_IDENTITY;
         v.pieces[0].c0 = v.pieces[0].c1 = v.pieces[0].n = 0.0;
       }},
      {"the last piece's leaf kind", [](Owned & v) {
         v.pieces[1].leaf_kind = GSHSCENE_TRANSFER_EXPONENTIAL;
         v.pieces[1].c1 = 1.0;
         v.pieces[1].n = 1.0;
         v.piece_samples[1].clear();
       }},
      {"a leaf's c0", [](Owned & v) { v.pieces[0].c0 = 0.25; }},
      {"a leaf's c1", [](Owned & v) { v.pieces[0].c1 = 0.75; }},
      {"a leaf's exponent", [](Owned & v) { v.pieces[0].n = 3.0; }},
      {"a leaf's sample added", [](Owned & v) { v.piece_samples[1].push_back(1.0); }},
      {"a leaf's first sample", [](Owned & v) { v.piece_samples[1][0] = 0.125; }},
      {"a leaf's middle sample", [](Owned & v) { v.piece_samples[1][1] = 0.625; }},
      {"a leaf's last sample", [](Owned & v) { v.piece_samples[1][2] = 0.875; }},
  });
}

// MK1, MK13: minus zero is spelled plus zero, wherever it stands and whichever spelling is interned first: what is stored has
// no sign on a zero, and the two spellings are one handle.
TEST(Mask, EveryNegativeZeroIsStoredAsPositiveZeroWhicheverSpellingComesFirst) {
  for (int negative_first = 1; negative_first >= 0; negative_first--) {
    World w;
    const double nz = -0.0;
    auto spelled = [&](bool negative) {
      const double z = negative ? nz : 0.0;
      std::vector<Owned> masks(4);
      masks[0].m = w.luminance();
      masks[0].m.backdrop[0] = masks[0].m.backdrop[1] = masks[0].m.backdrop[2] = z;
      masks[1].m = w.alpha();
      masks[1].m.transfer.kind = GSHSCENE_TRANSFER_EXPONENTIAL;
      masks[1].m.transfer.c0 = z;
      masks[1].m.transfer.c1 = z;
      masks[1].m.transfer.n = z;
      masks[2].m = w.alpha();
      masks[2].m.transfer.kind = GSHSCENE_TRANSFER_TABLE;
      masks[2].samples = {z, z, 1.0};
      masks[3].m = w.alpha();
      masks[3].m.transfer.kind = GSHSCENE_TRANSFER_STITCHING;
      GSHSCENE_Transfer_Piece p0 = piece(z, 0.5, z, z);
      p0.leaf_kind = GSHSCENE_TRANSFER_EXPONENTIAL;
      p0.c0 = z;
      p0.c1 = z;
      p0.n = z;
      GSHSCENE_Transfer_Piece p1 = piece(0.5, 1.0, 1.0, z);
      p1.leaf_kind = GSHSCENE_TRANSFER_TABLE;
      masks[3].pieces = {p0, p1};
      masks[3].piece_samples = {{}, {z, z}};
      for (Owned & o : masks) {
        o.bind();
      }
      return masks;
    };
    std::vector<Owned> first = spelled(negative_first != 0), second = spelled(negative_first == 0);
    std::vector<Ref> held;
    for (size_t i = 0; i < first.size(); i++) {
      const GSHSCENE_Resource * a = nullptr;
      const GSHSCENE_Resource * b = nullptr;
      ASSERT_EQ(w.intern(first[i].m, &a), GSHSCENE_OK);
      ASSERT_EQ(w.intern(second[i].m, &b), GSHSCENE_OK);
      held.emplace_back(a);
      held.emplace_back(b);
      EXPECT_EQ(a, b) << "mask " << i << ", the negative spelling " << (negative_first ? "first" : "second");
      GSHSCENE_Mask read;
      ASSERT_EQ(gshscene_resource_mask(a, &read), GSHSCENE_OK);
      for (int k = 0; k < 3; k++) {
        EXPECT_EQ(bits(read.backdrop[k]), 0u);
      }
      if (i == 1) {
        EXPECT_EQ(bits(read.transfer.c0), 0u);
        EXPECT_EQ(bits(read.transfer.c1), 0u);
        EXPECT_EQ(bits(read.transfer.n), 0u);
      }
      if (i == 2) {
        EXPECT_EQ(bits(read.transfer.samples[0]), 0u);
        EXPECT_EQ(bits(read.transfer.samples[1]), 0u);
      }
      if (i == 3) {
        const GSHSCENE_Transfer_Piece & p = read.transfer.pieces[0];
        const GSHSCENE_Transfer_Piece & q = read.transfer.pieces[1];
        EXPECT_EQ(bits(p.lo), 0u);
        EXPECT_EQ(bits(p.e0), 0u);
        EXPECT_EQ(bits(p.e1), 0u);
        EXPECT_EQ(bits(p.c0), 0u);
        EXPECT_EQ(bits(p.c1), 0u);
        EXPECT_EQ(bits(p.n), 0u);
        EXPECT_EQ(bits(q.e1), 0u);
        EXPECT_EQ(bits(q.samples[0]), 0u);
        EXPECT_EQ(bits(q.samples[1]), 0u);
      }
    }
  }
}

// MK1: what a mask allocates for its transfer (the pieces, the samples) is freed with its last release, whichever of them it has.
TEST(Mask, TheLastReleaseFreesTheTransfersArrays) {
  tracker::Tracker t;
  GSHSCENE_Allocator alloc = tracker::allocator_for(&t);
  StoreHolder s(nullptr, &alloc);
  Ref sq(intern_square(s.store));
  Ref p2(intern_fixture(s.store));
  Ref pic(fixture_picture(s.store, sq.get(), p2.get()));
  size_t before = t.live.size();
  static const double samples[3] = {0.0, 0.5, 1.0};
  GSHSCENE_Mask base{};
  base.picture = pic.get();
  base.mode = GSHSCENE_MASK_ALPHA;
  GSHSCENE_Mask tabled = base;
  tabled.transfer.kind = GSHSCENE_TRANSFER_TABLE;
  tabled.transfer.sample_count = 3;
  tabled.transfer.samples = samples;
  GSHSCENE_Transfer_Piece leaf_table = piece(0.0, 0.5, 0.0, 1.0);
  leaf_table.leaf_kind = GSHSCENE_TRANSFER_TABLE;
  leaf_table.sample_count = 3;
  leaf_table.samples = samples;
  GSHSCENE_Transfer_Piece pieces[2] = {leaf_table, piece(0.5, 1.0, 0.0, 1.0)};
  GSHSCENE_Mask stitched = base;
  stitched.transfer.kind = GSHSCENE_TRANSFER_STITCHING;
  stitched.transfer.piece_count = 2;
  stitched.transfer.pieces = pieces;
  GSHSCENE_Mask identity = base;
  for (const GSHSCENE_Mask * m : {&tabled, &stitched, &identity}) {
    const GSHSCENE_Resource * r = nullptr;
    ASSERT_EQ(gshscene_store_intern_mask(s.store, m, &r), GSHSCENE_OK);
    EXPECT_GT(t.live.size(), before);
    gshscene_resource_release(r);
    EXPECT_EQ(t.live.size(), before) << "the mask, its arrays and its hold on the picture are gone";
  }
}

TEST(Mask, ReadBackIsTheContentAndTheMaskHoldsItsPicture) {
  World w;
  size_t before = gshscene_resource_refcount(w.pic.get());
  Ref mask(known_mask(w.s.store, w.pic.get(), 3));
  ASSERT_NE(mask.get(), nullptr);
  EXPECT_EQ(gshscene_resource_refcount(w.pic.get()), before + 1) << "the mask retains its picture";
  GSHSCENE_Mask m;
  ASSERT_EQ(gshscene_resource_mask(mask.get(), &m), GSHSCENE_OK);
  EXPECT_EQ(m.picture, w.pic.get());
  EXPECT_EQ(m.mode, (uint32_t)GSHSCENE_MASK_ALPHA);
  EXPECT_EQ(m.transfer.kind, (uint32_t)GSHSCENE_TRANSFER_STITCHING);
  ASSERT_EQ(m.transfer.piece_count, 3u);
  EXPECT_EQ(m.transfer.pieces[1].leaf_kind, (uint32_t)GSHSCENE_TRANSFER_EXPONENTIAL);
  EXPECT_EQ(m.transfer.pieces[1].n, 2.0);
  ASSERT_EQ(m.transfer.pieces[2].sample_count, 3u);
  EXPECT_EQ(m.transfer.pieces[2].samples[1], 0.5);
  GSHSCENE_Mask none;
  EXPECT_EQ(gshscene_resource_mask(w.pic.get(), &none), GSHSCENE_ERR_INVALID) << "not a mask";
  EXPECT_EQ(gshscene_resource_mask(nullptr, &none), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_resource_mask(mask.get(), nullptr), GSHSCENE_ERR_INVALID);
  mask = Ref();
  EXPECT_EQ(gshscene_resource_refcount(w.pic.get()), before) << "and releases it with the mask";
}

TEST(Mask, ABackingStoreHoldsNothingAfterTheLastRelease) {
  World w;
  GSHSCENE_Store_Stats before;
  ASSERT_EQ(gshscene_store_stats(w.s.store, &before), GSHSCENE_OK);
  for (int i = 0; i < 6; i++) {
    Ref mask(known_mask(w.s.store, w.pic.get(), i));
    ASSERT_NE(mask.get(), nullptr);
  }
  GSHSCENE_Store_Stats after;
  ASSERT_EQ(gshscene_store_stats(w.s.store, &after), GSHSCENE_OK);
  EXPECT_EQ(after.live, before.live);
  EXPECT_EQ(after.bytes, before.bytes);
  EXPECT_EQ(after.created, before.created + 6);
}

// --- MK1: refusals and their order ------------------------------------------------------------------------------------

TEST(Mask, EveryInvalidFieldIsRefused) {
  World w;
  StoreHolder other;
  Ref other_sq(intern_square(other.store));
  Ref other_p2(intern_fixture(other.store));
  Ref other_pic(fixture_picture(other.store, other_sq.get(), other_p2.get()));
  const GSHSCENE_Resource * unused = nullptr;
  EXPECT_EQ(gshscene_store_intern_mask(nullptr, nullptr, &unused), GSHSCENE_ERR_INVALID);
  GSHSCENE_Mask ok = w.alpha();
  EXPECT_EQ(gshscene_store_intern_mask(w.s.store, nullptr, &unused), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_store_intern_mask(w.s.store, &ok, nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(unused, nullptr) << "nothing is written on a failure";
  struct Case {
    const char * what;
    GSHSCENE_Mask m;
  };
  std::vector<Case> cases;
  auto bad = [&](const char * what, auto change, bool luminance = false) {
    Case c{what, luminance ? w.luminance() : w.alpha()};
    change(c.m);
    cases.push_back(c);
  };
  bad("no picture", [](GSHSCENE_Mask & m) { m.picture = nullptr; });
  bad("a path for the picture", [&](GSHSCENE_Mask & m) { m.picture = w.sq.get(); });
  bad("a picture of another store", [&](GSHSCENE_Mask & m) { m.picture = other_pic.get(); });
  bad("mode zero (the front-end must say)", [](GSHSCENE_Mask & m) { m.mode = 0; });
  bad("mode past the enum", [](GSHSCENE_Mask & m) { m.mode = GSHSCENE_MASK_MODE_COUNT; });
  bad("coefficients past the enum", [](GSHSCENE_Mask & m) { m.coefficients = GSHSCENE_MASK_COEFF_COUNT; }, true);
  bad("acts_on past the enum", [](GSHSCENE_Mask & m) { m.acts_on = GSHSCENE_MASK_ACTS_ON_COUNT; });
  bad("a coefficient set in alpha mode", [](GSHSCENE_Mask & m) { m.coefficients = GSHSCENE_MASK_COEFF_LUM; });
  bad("a luminance space in alpha mode", [](GSHSCENE_Mask & m) { m.space = srgb_space(); });
  bad("a backdrop in alpha mode", [](GSHSCENE_Mask & m) { m.backdrop[1] = 0.5; });
  bad("a backdrop space in alpha mode", [](GSHSCENE_Mask & m) { m.backdrop_space = srgb_space(); });
  bad("a gamut with no transfer", [](GSHSCENE_Mask & m) { m.render_space.primaries = GCOL_PRIMARIES_SRGB; });
  bad("a gamma where none belongs", [](GSHSCENE_Mask & m) { m.render_space = space_of(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_SRGB, 2.2); });
  bad("a NaN backdrop", [](GSHSCENE_Mask & m) { m.backdrop[0] = kNaN; }, true);
  bad("an infinite backdrop", [](GSHSCENE_Mask & m) { m.backdrop[2] = kInf; }, true);
  bad("a transfer kind past the enum", [](GSHSCENE_Mask & m) { m.transfer.kind = GSHSCENE_TRANSFER_KIND_COUNT; });
  bad("identity with a stray exponent", [](GSHSCENE_Mask & m) { m.transfer.n = 1.0; });
  bad("identity with a stray constant", [](GSHSCENE_Mask & m) { m.transfer.c0 = 0.5; });
  bad("identity with a sample count", [](GSHSCENE_Mask & m) { m.transfer.sample_count = 2; });
  static const double stray_samples[2] = {0.0, 1.0};
  bad("a backdrop (red) in alpha mode", [](GSHSCENE_Mask & m) { m.backdrop[0] = 0.5; });
  bad("a backdrop (blue) in alpha mode", [](GSHSCENE_Mask & m) { m.backdrop[2] = 0.5; });
  bad("identity with a stray upper constant", [](GSHSCENE_Mask & m) { m.transfer.c1 = 0.5; });
  bad("identity with a samples array and no count", [](GSHSCENE_Mask & m) { m.transfer.samples = stray_samples; });
  bad("an exponential with a samples array and no count", [](GSHSCENE_Mask & m) {
    m.transfer.kind = GSHSCENE_TRANSFER_EXPONENTIAL;
    m.transfer.n = 1.0;
    m.transfer.samples = stray_samples;
  });
  bad("an exponential with a sample count and no array", [](GSHSCENE_Mask & m) {
    m.transfer.kind = GSHSCENE_TRANSFER_EXPONENTIAL;
    m.transfer.n = 1.0;
    m.transfer.sample_count = 2;
  });
  bad("a table with a stray constant", [](GSHSCENE_Mask & m) {
    m.transfer.kind = GSHSCENE_TRANSFER_TABLE;
    m.transfer.sample_count = 2;
    m.transfer.samples = stray_samples;
    m.transfer.c0 = 0.5;
  });
  bad("a table with a stray upper constant", [](GSHSCENE_Mask & m) {
    m.transfer.kind = GSHSCENE_TRANSFER_TABLE;
    m.transfer.sample_count = 2;
    m.transfer.samples = stray_samples;
    m.transfer.c1 = 0.5;
  });
  for (const Case & c : cases) {
    const GSHSCENE_Resource * r = nullptr;
    EXPECT_EQ(gshscene_store_intern_mask(w.s.store, &c.m, &r), GSHSCENE_ERR_INVALID) << c.what;
    EXPECT_EQ(r, nullptr) << c.what;
  }
  GSHSCENE_Store_Stats stats;
  ASSERT_EQ(gshscene_store_stats(w.s.store, &stats), GSHSCENE_OK);
  EXPECT_EQ(stats.created, 3u) << "the fixture's three, and no refused mask ever got an id";
}

TEST(Mask, EveryInvalidTransferIsRefused) {
  World w;
  static const double two[2] = {0.0, 1.0};
  static const double nan_table[3] = {0.0, kNaN, 1.0};
  static const double one[1] = {0.5};
  struct Case {
    const char * what;
    GSHSCENE_Transfer t;
    std::vector<GSHSCENE_Transfer_Piece> pieces;
  };
  std::vector<Case> cases;
  auto exp_of = [](double c0, double c1, double n) {
    GSHSCENE_Transfer t{};
    t.kind = GSHSCENE_TRANSFER_EXPONENTIAL;
    t.c0 = c0;
    t.c1 = c1;
    t.n = n;
    return t;
  };
  auto table_of = [](size_t count, const double * samples) {
    GSHSCENE_Transfer t{};
    t.kind = GSHSCENE_TRANSFER_TABLE;
    t.sample_count = count;
    t.samples = samples;
    return t;
  };
  auto stitch_of = [](std::vector<GSHSCENE_Transfer_Piece> pieces) {
    GSHSCENE_Transfer t{};
    t.kind = GSHSCENE_TRANSFER_STITCHING;
    return std::make_pair(t, pieces);
  };
  cases.push_back({"a negative exponent", exp_of(0, 1, -1.0), {}});
  cases.push_back({"a NaN exponent", exp_of(0, 1, kNaN), {}});
  cases.push_back({"an infinite exponent", exp_of(0, 1, kInf), {}});
  cases.push_back({"a NaN constant", exp_of(kNaN, 1, 1), {}});
  cases.push_back({"an infinite constant", exp_of(0, kInf, 1), {}});
  {
    Case c{"an exponential with samples", exp_of(0, 1, 1), {}};
    c.t.sample_count = 2;
    c.t.samples = two;
    cases.push_back(c);
  }
  cases.push_back({"a table of no samples", table_of(0, nullptr), {}});
  cases.push_back({"a table of one sample", table_of(1, one), {}});
  cases.push_back({"a table with no array", table_of(2, nullptr), {}});
  cases.push_back({"a table with a NaN sample", table_of(3, nan_table), {}});
  {
    Case c{"a table with a stray exponent", table_of(2, two), {}};
    c.t.n = 1.0;
    cases.push_back(c);
  }
  {
    Case c{"a table with pieces", table_of(2, two), {piece(0, 1, 0, 1)}};
    c.t.piece_count = 1;
    cases.push_back(c);
  }
  auto add_stitch = [&](const char * what, std::vector<GSHSCENE_Transfer_Piece> pieces) {
    auto s = stitch_of(pieces);
    cases.push_back({what, s.first, s.second});
  };
  add_stitch("a stitching of no pieces", {});
  add_stitch("a first piece not starting at 0", {piece(0.125, 1.0, 0, 1)});
  add_stitch("a last piece not ending at 1", {piece(0.0, 0.875, 0, 1)});
  add_stitch("a gap", {piece(0.0, 0.25, 0, 1), piece(0.5, 1.0, 0, 1)});
  add_stitch("an overlap", {piece(0.0, 0.5, 0, 1), piece(0.25, 1.0, 0, 1)});
  add_stitch("an empty piece", {piece(0.0, 0.0, 0, 1), piece(0.0, 1.0, 0, 1)});
  add_stitch("a reversed piece", {piece(0.0, 0.5, 0, 1), piece(0.5, 0.25, 0, 1), piece(0.25, 1.0, 0, 1)});
  add_stitch("an encode below 0", {piece(0.0, 1.0, -0.125, 1)});
  add_stitch("an encode above 1", {piece(0.0, 1.0, 0, 1.125)});
  add_stitch("a NaN bound", {piece(0.0, kNaN, 0, 1)});
  add_stitch("an infinite encode", {piece(0.0, 1.0, 0, kInf)});
  {
    GSHSCENE_Transfer_Piece p = piece(0.0, 1.0, 0, 1);
    p.leaf_kind = GSHSCENE_TRANSFER_STITCHING;
    add_stitch("a nested stitching (the front-end flattens)", {p});
    p.leaf_kind = GSHSCENE_TRANSFER_KIND_COUNT;
    add_stitch("a leaf kind past the enum", {p});
    p = piece(0.0, 1.0, 0, 1);
    p.leaf_kind = GSHSCENE_TRANSFER_EXPONENTIAL;
    p.n = -1.0;
    add_stitch("a leaf exponent below 0", {p});
    p.n = 1.0;
    p.sample_count = 2;
    p.samples = two;
    add_stitch("an exponential leaf with samples", {p});
    p = piece(0.0, 1.0, 0, 1);
    p.leaf_kind = GSHSCENE_TRANSFER_TABLE;
    p.sample_count = 1;
    p.samples = one;
    add_stitch("a table leaf of one sample", {p});
    p.sample_count = 3;
    p.samples = nan_table;
    add_stitch("a table leaf with a NaN", {p});
    p = piece(0.0, 1.0, 0, 1);
    p.c0 = 0.5;
    add_stitch("an identity leaf with a stray constant", {p});
  }
  {
    auto s = stitch_of({piece(0.0, 1.0, 0, 1)});
    s.first.n = 1.0;
    cases.push_back({"a stitching with a top-level exponent", s.first, s.second});
    s = stitch_of({piece(0.0, 1.0, 0, 1)});
    s.first.sample_count = 2;
    s.first.samples = two;
    cases.push_back({"a stitching with top-level samples", s.first, s.second});
  }
  for (Case & c : cases) {
    GSHSCENE_Mask m = w.alpha();
    m.transfer = c.t;
    if (c.t.kind == GSHSCENE_TRANSFER_STITCHING) {
      m.transfer.piece_count = c.pieces.size();
      m.transfer.pieces = c.pieces.empty() ? nullptr : c.pieces.data();
    }
    EXPECT_EQ(w.intern(m), GSHSCENE_ERR_INVALID) << c.what;
  }
  // A non-stitching transfer with a pieces array is invalid too.
  GSHSCENE_Transfer_Piece stray = piece(0.0, 1.0, 0, 1);
  GSHSCENE_Mask m = w.alpha();
  m.transfer.pieces = &stray;
  EXPECT_EQ(w.intern(m), GSHSCENE_ERR_INVALID) << "an identity with pieces";
  m.transfer.pieces = nullptr;
  m.transfer.piece_count = 1;
  EXPECT_EQ(w.intern(m), GSHSCENE_ERR_INVALID) << "an identity with a piece count";
}

TEST(Mask, UnsupportedComesAfterInvalidAndBeforeLimit) {
  World w;
  GSHSCENE_Color_Space prophoto = space_of(GCOL_PRIMARIES_PROPHOTO, GCOL_TRANSFER_SRGB);
  GSHSCENE_Mask m = w.luminance();
  m.space = prophoto;
  EXPECT_EQ(w.intern(m), GSHSCENE_ERR_UNSUPPORTED) << "a luminance space this library does not carry";
  m = w.luminance();
  m.render_space = prophoto;
  EXPECT_EQ(w.intern(m), GSHSCENE_ERR_UNSUPPORTED);
  m = w.luminance();
  m.backdrop_space = prophoto;
  EXPECT_EQ(w.intern(m), GSHSCENE_ERR_UNSUPPORTED);
  m = w.alpha();
  m.render_space = prophoto;
  EXPECT_EQ(w.intern(m), GSHSCENE_ERR_UNSUPPORTED) << "alpha takes a render space too";
  // Unsupported with a limit: unsupported.
  m = w.luminance();
  m.space = prophoto;
  m.backdrop[0] = 65537.0;
  EXPECT_EQ(w.intern(m), GSHSCENE_ERR_UNSUPPORTED);
  m.transfer.kind = GSHSCENE_TRANSFER_EXPONENTIAL;
  m.transfer.n = 70000.0;
  EXPECT_EQ(w.intern(m), GSHSCENE_ERR_UNSUPPORTED);
  // Unsupported with an invalid field: invalid.
  m.backdrop[1] = kNaN;
  EXPECT_EQ(w.intern(m), GSHSCENE_ERR_INVALID);
  m = w.luminance();
  m.space = prophoto;
  m.picture = nullptr;
  EXPECT_EQ(w.intern(m), GSHSCENE_ERR_INVALID);
  m = w.luminance();
  m.space = prophoto;
  m.transfer.kind = GSHSCENE_TRANSFER_TABLE;
  m.transfer.sample_count = 1;
  static const double one[1] = {0.5};
  m.transfer.samples = one;
  EXPECT_EQ(w.intern(m), GSHSCENE_ERR_INVALID) << "an invalid transfer beats an unsupported space";
}

// --- MK12, M1-D8: the bounds, at the bound and one past it ------------------------------------------------------------------

TEST(Mask, TheBackdropBoundIsTwoToTheSixteenInclusive) {
  World w;
  for (double v : {65536.0, -65536.0}) {
    GSHSCENE_Mask m = w.luminance();
    m.backdrop[0] = v;
    m.backdrop[1] = -v;
    m.backdrop[2] = v;
    EXPECT_EQ(w.intern(m), GSHSCENE_OK) << v;
    for (int c = 0; c < 3; c++) {
      m = w.luminance();
      m.backdrop[c] = std::nextafter(v, v * 2);
      EXPECT_EQ(w.intern(m), GSHSCENE_ERR_LIMIT) << v << " component " << c;
    }
  }
}

TEST(Mask, TheExponentBoundIsTwoToTheSixteenInclusive) {
  World w;
  GSHSCENE_Mask m = w.alpha();
  m.transfer.kind = GSHSCENE_TRANSFER_EXPONENTIAL;
  m.transfer.c1 = 1.0;
  m.transfer.n = 65536.0;
  EXPECT_EQ(w.intern(m), GSHSCENE_OK);
  m.transfer.n = std::nextafter(65536.0, 1e9);
  EXPECT_EQ(w.intern(m), GSHSCENE_ERR_LIMIT);
  m.transfer.n = 0.0;
  EXPECT_EQ(w.intern(m), GSHSCENE_OK) << "n = 0 is allowed";
  // The same in a leaf.
  Stitch st(1);
  st.pieces[0].leaf_kind = GSHSCENE_TRANSFER_EXPONENTIAL;
  st.pieces[0].c1 = 1.0;
  st.pieces[0].n = 65536.0;
  m = w.alpha();
  st.apply(&m.transfer);
  EXPECT_EQ(w.intern(m), GSHSCENE_OK);
  st.pieces[0].n = std::nextafter(65536.0, 1e9);
  EXPECT_EQ(w.intern(m), GSHSCENE_ERR_LIMIT);
}

TEST(Mask, TwoHundredFiftySixPiecesAreEnoughAndTheNextIsNot) {
  World w;
  for (size_t count : {size_t(1), size_t(255), size_t(256), size_t(257), size_t(300)}) {
    Stitch st(count);
    GSHSCENE_Mask m = w.alpha();
    st.apply(&m.transfer);
    GSHSCENE_Result want = count <= GSHSCENE_MASK_MAX_PIECES ? GSHSCENE_OK : GSHSCENE_ERR_LIMIT;
    EXPECT_EQ(w.intern(m), want) << count;
  }
}

TEST(Mask, FortyNinetySixSamplesAreEnoughAndTheNextIsNot) {
  World w;
  std::vector<double> samples(4097);
  for (size_t i = 0; i < samples.size(); i++) {
    samples[i] = (double)i / 4096.0;
  }
  for (size_t count : {size_t(2), size_t(4095), size_t(4096), size_t(4097)}) {
    GSHSCENE_Mask m = w.alpha();
    m.transfer.kind = GSHSCENE_TRANSFER_TABLE;
    m.transfer.sample_count = count;
    m.transfer.samples = samples.data();
    GSHSCENE_Result want = count <= GSHSCENE_MASK_MAX_SAMPLES ? GSHSCENE_OK : GSHSCENE_ERR_LIMIT;
    EXPECT_EQ(w.intern(m), want) << count;
  }
}

TEST(Mask, TheSampleBoundCountsEveryTableOfAStitchingTogether) {
  World w;
  std::vector<double> samples(5000, 0.5);
  for (size_t second : {size_t(2048), size_t(2049)}) {
    Stitch st(2);
    for (GSHSCENE_Transfer_Piece & p : st.pieces) {
      p.leaf_kind = GSHSCENE_TRANSFER_TABLE;
    }
    st.pieces[0].sample_count = 2048;
    st.pieces[0].samples = samples.data();
    st.pieces[1].sample_count = second;
    st.pieces[1].samples = samples.data();
    GSHSCENE_Mask m = w.alpha();
    st.apply(&m.transfer);
    EXPECT_EQ(w.intern(m), second == 2048 ? GSHSCENE_OK : GSHSCENE_ERR_LIMIT) << second;
  }
}

TEST(Mask, ALimitIsDecidedAfterEveryInvalidField) {
  World w;
  std::vector<double> samples(4097, 0.5);
  samples[4096] = kNaN;
  GSHSCENE_Mask m = w.alpha();
  m.transfer.kind = GSHSCENE_TRANSFER_TABLE;
  m.transfer.sample_count = 4097;
  m.transfer.samples = samples.data();
  EXPECT_EQ(w.intern(m), GSHSCENE_ERR_INVALID) << "too many samples, one of them NaN";
  Stitch st(257);
  st.pieces[256].e1 = 2.0;
  m = w.alpha();
  st.apply(&m.transfer);
  EXPECT_EQ(w.intern(m), GSHSCENE_ERR_INVALID) << "too many pieces, the last invalid";
  m = w.luminance();
  m.backdrop[0] = 70000.0;
  m.coefficients = GSHSCENE_MASK_COEFF_COUNT;
  EXPECT_EQ(w.intern(m), GSHSCENE_ERR_INVALID) << "a backdrop past the bound and an invalid coefficient set";
  m = w.luminance();
  m.backdrop[0] = 70000.0;
  m.transfer.kind = GSHSCENE_TRANSFER_EXPONENTIAL;
  m.transfer.n = -1.0;
  EXPECT_EQ(w.intern(m), GSHSCENE_ERR_INVALID);
}

TEST(Mask, TheStoresLimitsApplyAndAMaskIsAccountedByItsArrays) {
  World w;
  GSHSCENE_Store_Stats before;
  ASSERT_EQ(gshscene_store_stats(w.s.store, &before), GSHSCENE_OK);
  Ref plain(known_mask(w.s.store, w.pic.get(), 0));
  EXPECT_EQ(gshscene_resource_bytes(plain.get()), GSHSCENE_BYTES_BASE);
  Ref stitched(known_mask(w.s.store, w.pic.get(), 3));
  EXPECT_EQ(gshscene_resource_bytes(stitched.get()),
      GSHSCENE_BYTES_BASE + 3 * GSHSCENE_BYTES_PER_TRANSFER_PIECE + 3 * GSHSCENE_BYTES_PER_TRANSFER_SAMPLE);
  Ref tabled(known_mask(w.s.store, w.pic.get(), 2));
  EXPECT_EQ(gshscene_resource_bytes(tabled.get()), GSHSCENE_BYTES_BASE + 3 * GSHSCENE_BYTES_PER_TRANSFER_SAMPLE);
  GSHSCENE_Store_Stats after;
  ASSERT_EQ(gshscene_store_stats(w.s.store, &after), GSHSCENE_OK);
  EXPECT_EQ(after.bytes - before.bytes, gshscene_resource_bytes(plain.get()) + gshscene_resource_bytes(stitched.get()) +
                                              gshscene_resource_bytes(tabled.get()));
  // A store whose byte limit is one byte short of the fixture and a mask refuses the mask.
  GSHSCENE_Limits limits;
  gshscene_limits_default(&limits);
  StoreHolder tight;
  {
    StoreHolder probe;
    Ref sq(intern_square(probe.store));
    Ref p2(intern_fixture(probe.store));
    Ref pic(fixture_picture(probe.store, sq.get(), p2.get()));
    GSHSCENE_Store_Stats s;
    ASSERT_EQ(gshscene_store_stats(probe.store, &s), GSHSCENE_OK);
    limits.max_bytes = (size_t)s.bytes + GSHSCENE_BYTES_BASE - 1;
  }
  StoreHolder small(&limits);
  Ref sq(intern_square(small.store));
  Ref p2(intern_fixture(small.store));
  Ref pic(fixture_picture(small.store, sq.get(), p2.get()));
  GSHSCENE_Mask m;
  std::memset(&m, 0, sizeof m);
  m.picture = pic.get();
  m.mode = GSHSCENE_MASK_ALPHA;
  const GSHSCENE_Resource * r = nullptr;
  EXPECT_EQ(gshscene_store_intern_mask(small.store, &m, &r), GSHSCENE_ERR_LIMIT);
  EXPECT_EQ(r, nullptr);
}

// --- MK4: the transfer, against hand-computed values -----------------------------------------------------------------------

TEST(MaskTransfer, IdentityClampsItsInputAndANaNIsZero) {
  World w;
  GSHSCENE_Transfer id{};
  for (double x : {0.0, 0.125, 0.5, 1.0}) {
    EXPECT_EQ(transfer_at(w, id, x), x);
  }
  EXPECT_EQ(transfer_at(w, id, -3.0), 0.0);
  EXPECT_EQ(transfer_at(w, id, 7.0), 1.0);
  EXPECT_EQ(transfer_at(w, id, -kInf), 0.0);
  EXPECT_EQ(transfer_at(w, id, kInf), 1.0);
  EXPECT_EQ(transfer_at(w, id, kNaN), 0.0);
  EXPECT_EQ(bits(transfer_at(w, id, -0.0)), 0u) << "minus zero clamps to plus zero";
  // A transfer that is not in a mask is evaluated as it stands: a stitching with no pieces has nothing to select.
  GSHSCENE_Transfer empty_stitching{};
  empty_stitching.kind = GSHSCENE_TRANSFER_STITCHING;
  EXPECT_EQ(gshscene_transfer_eval(&empty_stitching, 0.25), 0.25);
  EXPECT_EQ(gshscene_transfer_eval(nullptr, 7.0), 1.0) << "no transfer is the identity, clamped";
  EXPECT_EQ(gshscene_mask_transfer(nullptr, 0.5), 0.0) << "no mask, no value";
  EXPECT_EQ(gshscene_mask_transfer(w.pic.get(), 0.5), 0.0) << "not a mask";
}

TEST(MaskTransfer, ExponentialIsC0PlusXToTheNTimesTheDifference) {
  World w;
  GSHSCENE_Transfer t{};
  t.kind = GSHSCENE_TRANSFER_EXPONENTIAL;
  t.c0 = 0.25;
  t.c1 = 0.75;
  t.n = 1.0;
  EXPECT_EQ(transfer_at(w, t, 0.0), 0.25);
  EXPECT_EQ(transfer_at(w, t, 0.5), 0.5);
  EXPECT_EQ(transfer_at(w, t, 1.0), 0.75);
  t.n = 2.0;
  EXPECT_EQ(transfer_at(w, t, 0.0), 0.25);
  EXPECT_NEAR(transfer_at(w, t, 0.5), 0.375, 1e-15);
  EXPECT_EQ(transfer_at(w, t, 1.0), 0.75);
  t.n = 0.5;
  EXPECT_NEAR(transfer_at(w, t, 0.25), 0.5, 1e-15) << "0.25 + sqrt(0.25) * 0.5";
  EXPECT_NEAR(transfer_at(w, t, 0.0625), 0.25 + 0.25 * 0.5, 1e-15);
  // n = 0: x^0 is 1 for every x, 0^0 included.
  t.n = 0.0;
  for (double x : {0.0, 0.5, 1.0}) {
    EXPECT_EQ(transfer_at(w, t, x), 0.75) << x;
  }
  // The output is clamped.
  t.c0 = -1.0;
  t.c1 = 2.0;
  t.n = 1.0;
  EXPECT_EQ(transfer_at(w, t, 0.0), 0.0);
  EXPECT_EQ(transfer_at(w, t, 1.0), 1.0);
  EXPECT_EQ(transfer_at(w, t, 0.5), 0.5);
  EXPECT_EQ(transfer_at(w, t, 0.25), 0.0) << "-1 + 0.75 is -0.25: clamped";
  // n = 1, c0 = 0, c1 = 1 is the identity.
  t.c0 = 0.0;
  t.c1 = 1.0;
  for (double x : {0.0, 0.1, 0.5, 0.9, 1.0}) {
    EXPECT_EQ(transfer_at(w, t, x), x) << x;
  }
  // A large exponent: the power underflows toward 0.
  t.n = 65536.0;
  EXPECT_EQ(transfer_at(w, t, 0.5), 0.0);
  EXPECT_EQ(transfer_at(w, t, 1.0), 1.0);
  // An exponential whose difference overflows, at 0: the product with 0 is not a NaN.
  t.c0 = -1e308;
  t.c1 = 1e308;
  t.n = 1.0;
  double v = transfer_at(w, t, 0.0);
  EXPECT_FALSE(std::isnan(v));
}

TEST(MaskTransfer, TableIsLinearOnAnEquallySpacedGrid) {
  World w;
  static const double up_down[3] = {0.0, 1.0, 0.0};
  GSHSCENE_Transfer t{};
  t.kind = GSHSCENE_TRANSFER_TABLE;
  t.sample_count = 3;
  t.samples = up_down;
  EXPECT_EQ(transfer_at(w, t, 0.0), 0.0);
  EXPECT_EQ(transfer_at(w, t, 0.25), 0.5);
  EXPECT_EQ(transfer_at(w, t, 0.5), 1.0);
  EXPECT_EQ(transfer_at(w, t, 0.75), 0.5);
  EXPECT_EQ(transfer_at(w, t, 1.0), 0.0) << "x = 1 is the last interval's end, not past the table";
  static const double two[2] = {0.2, 0.8};
  t.sample_count = 2;
  t.samples = two;
  EXPECT_NEAR(transfer_at(w, t, 0.5), 0.5, 1e-15);
  EXPECT_EQ(transfer_at(w, t, 0.0), 0.2);
  EXPECT_EQ(transfer_at(w, t, 1.0), 0.8);
  // A 2-sample table [0, 1] is the identity.
  static const double ident[2] = {0.0, 1.0};
  t.samples = ident;
  for (double x : {0.0, 0.1, 0.3, 0.5, 0.9, 1.0}) {
    EXPECT_EQ(transfer_at(w, t, x), x) << x;
  }
  // Many samples: the last interval of a 5-sample table.
  static const double five[5] = {0.0, 0.1, 0.2, 0.3, 0.9};
  t.sample_count = 5;
  t.samples = five;
  EXPECT_NEAR(transfer_at(w, t, 0.875), 0.3 + 0.5 * 0.6, 1e-15);
  EXPECT_NEAR(transfer_at(w, t, 1.0), 0.9, 1e-15) << "0.3 + 1 * (0.9 - 0.3), to a rounding";
  // Samples outside [0, 1] are clamped on the way out.
  static const double wide[2] = {-2.0, 3.0};
  t.sample_count = 2;
  t.samples = wide;
  EXPECT_EQ(transfer_at(w, t, 0.0), 0.0);
  EXPECT_EQ(transfer_at(w, t, 1.0), 1.0);
  EXPECT_EQ(transfer_at(w, t, 0.5), 0.5);
}

TEST(MaskTransfer, StitchingTakesTheUpperPieceAtABoundAndTheLastTakesOne) {
  World w;
  // Two identity pieces: the first rises over [0, 1/2), the second falls over [1/2, 1].
  Stitch st(2);
  st.pieces[0].e0 = 0.0;
  st.pieces[0].e1 = 1.0;
  st.pieces[1].e0 = 1.0;
  st.pieces[1].e1 = 0.0;
  GSHSCENE_Transfer t;
  st.apply(&t);
  EXPECT_EQ(transfer_at(w, t, 0.0), 0.0);
  EXPECT_EQ(transfer_at(w, t, 0.25), 0.5);
  EXPECT_NEAR(transfer_at(w, t, std::nextafter(0.5, 0.0)), 1.0, 1e-15) << "just below the bound the first piece is at its end";
  EXPECT_EQ(transfer_at(w, t, 0.5), 1.0) << "at the bound the second piece starts, at its e0 = 1";
  EXPECT_EQ(transfer_at(w, t, 0.75), 0.5);
  EXPECT_EQ(transfer_at(w, t, 1.0), 0.0) << "the last piece takes x = 1";
  // The bound distinguishes the two pieces: the first at its end is 1, the second at its start is 1 too; use a jump.
  st.pieces[1].e0 = 0.0;
  st.pieces[1].e1 = 0.5;
  st.apply(&t);
  EXPECT_EQ(transfer_at(w, t, 0.5), 0.0) << "the upper piece at the bound (the first would say 1)";
  EXPECT_NEAR(transfer_at(w, t, std::nextafter(0.5, 0.0)), 1.0, 1e-15);
  EXPECT_EQ(transfer_at(w, t, 1.0), 0.5);
  // A stitching of one piece is its leaf.
  Stitch one(1);
  one.pieces[0].leaf_kind = GSHSCENE_TRANSFER_EXPONENTIAL;
  one.pieces[0].c0 = 0.125;
  one.pieces[0].c1 = 0.875;
  one.pieces[0].n = 1.0;
  GSHSCENE_Transfer leaf{};
  leaf.kind = GSHSCENE_TRANSFER_EXPONENTIAL;
  leaf.c0 = 0.125;
  leaf.c1 = 0.875;
  leaf.n = 1.0;
  one.apply(&t);
  for (double x : {0.0, 0.1, 0.5, 0.75, 1.0}) {
    EXPECT_EQ(transfer_at(w, t, x), transfer_at(w, leaf, x)) << x;
  }
  // The fixture's three pieces: an identity, an exponential and a table.
  Ref mask(known_mask(w.s.store, w.pic.get(), 3));
  EXPECT_NEAR(gshscene_mask_transfer(mask.get(), 0.1), 0.4, 1e-15);
  EXPECT_EQ(gshscene_mask_transfer(mask.get(), 0.0), 0.0);
  EXPECT_NEAR(gshscene_mask_transfer(mask.get(), 0.25), 0.1 + 1.0 * 0.8 - 0.0 * 0.8 - 0.0, 1e-15) << "piece 1 at its start: x' = 1, 0.1 + 0.8";
  EXPECT_NEAR(gshscene_mask_transfer(mask.get(), 0.5), 0.1 + 0.25 * 0.8, 1e-15) << "x' = 0.5, 0.1 + 0.25 * 0.8";
  EXPECT_NEAR(gshscene_mask_transfer(mask.get(), 0.75), 0.0, 1e-15) << "piece 2 at its start: x' = 0, the table's first sample";
  EXPECT_NEAR(gshscene_mask_transfer(mask.get(), 0.875), 0.5, 1e-15) << "x' = 0.5, the table's middle";
  EXPECT_EQ(gshscene_mask_transfer(mask.get(), 1.0), 1.0) << "the last piece at 1: x' = 1, the table's last sample";
}

TEST(MaskTransfer, ABinarySearchOverManyPiecesFindsTheRightPiece) {
  World w;
  Stitch st(256);
  GSHSCENE_Transfer t;
  st.apply(&t);
  for (GSHSCENE_Transfer_Piece & p : st.pieces) {
    p.e0 = 0.0;
    p.e1 = 1.0;
  }
  // Each piece maps its own interval onto [0, 1]: the sawtooth, 256 teeth.
  for (size_t i = 0; i < 256; i++) {
    double lo = (double)i / 256.0;
    EXPECT_EQ(transfer_at(w, t, lo), 0.0) << i << ": the upper piece at a bound";
    EXPECT_NEAR(transfer_at(w, t, lo + 0.5 / 256.0), 0.5, 1e-12) << i;
  }
  EXPECT_EQ(transfer_at(w, t, 1.0), 1.0);
}

// --- MK3: the coefficient sets -------------------------------------------------------------------------------------------------

TEST(MaskCoefficients, TheFixedSetsAreTheNearestDoublesOfTheirRationalsAndSumToOne) {
  World w;
  struct Set {
    int coefficients;
    long num[3];
    long den;
  };
  const Set sets[] = {
      {GSHSCENE_MASK_COEFF_W3C, {2125, 7154, 721}, 10000},
      {GSHSCENE_MASK_COEFF_REC709, {2126, 7152, 722}, 10000},
      {GSHSCENE_MASK_COEFF_LUM, {30, 59, 11}, 100},
  };
  for (const Set & set : sets) {
    EXPECT_EQ(set.num[0] + set.num[1] + set.num[2], set.den) << "the numerators sum to the denominator, exactly";
    GSHSCENE_Mask m = w.luminance();
    m.coefficients = (uint32_t)set.coefficients;
    Ref mask;
    {
      const GSHSCENE_Resource * r = nullptr;
      ASSERT_EQ(w.intern(m, &r), GSHSCENE_OK);
      mask = Ref(r);
    }
    GSHSCENE_Mask_Resolved r;
    GSHSCENE_Color_Space enclosing = srgb_space();
    ASSERT_EQ(gshscene_mask_resolve(mask.get(), &enclosing, &r), GSHSCENE_OK);
    for (int c = 0; c < 3; c++) {
      EXPECT_EQ(bits(r.k[c]), bits((double)set.num[c] / (double)set.den)) << set.coefficients << " " << c;
    }
    EXPECT_NEAR(r.k[0] + r.k[1] + r.k[2], 1.0, 1e-15);
    // A gray has its own luminance in the reals; in doubles to a rounding.
    for (double g : {0.0, 0.25, 0.5, 1.0}) {
      EXPECT_NEAR(r.k[0] * g + r.k[1] * g + r.k[2] * g, g, 1e-15);
    }
  }
}

TEST(MaskCoefficients, SpaceYIsTheYRowOfTheLuminanceSpacesMatrixAndSumsToOne) {
  World w;
  struct Gamut {
    GCOL_Primaries primaries;
    double want[3];
  };
  const Gamut gamuts[] = {
      {GCOL_PRIMARIES_SRGB, {0.2126, 0.7152, 0.0722}},
      {GCOL_PRIMARIES_DISPLAY_P3, {0.2290, 0.6917, 0.0793}},
      {GCOL_PRIMARIES_BT2020, {0.2627, 0.6780, 0.0593}},
      {GCOL_PRIMARIES_ADOBE_RGB, {0.2974, 0.6273, 0.0753}},
  };
  for (const Gamut & g : gamuts) {
    GSHSCENE_Mask m = w.luminance();
    m.coefficients = GSHSCENE_MASK_COEFF_SPACE_Y;
    m.space = space_of(g.primaries, GCOL_TRANSFER_LINEAR);
    const GSHSCENE_Resource * raw = nullptr;
    ASSERT_EQ(w.intern(m, &raw), GSHSCENE_OK);
    Ref mask(raw);
    GSHSCENE_Mask_Resolved r;
    GSHSCENE_Color_Space enclosing = srgb_space();
    ASSERT_EQ(gshscene_mask_resolve(mask.get(), &enclosing, &r), GSHSCENE_OK);
    EXPECT_NEAR(r.k[0] + r.k[1] + r.k[2], 1.0, GSHSCENE_RESOLVE_TOLERANCE) << g.primaries;
    for (int c = 0; c < 3; c++) {
      EXPECT_NEAR(r.k[c], g.want[c], 2e-3) << g.primaries << " " << c;
    }
  }
  // The row follows the luminance space, resolved: L = R = the enclosing space when both are zero.
  GSHSCENE_Mask m = w.luminance();
  m.coefficients = GSHSCENE_MASK_COEFF_SPACE_Y;
  const GSHSCENE_Resource * raw = nullptr;
  ASSERT_EQ(w.intern(m, &raw), GSHSCENE_OK);
  Ref mask(raw);
  GSHSCENE_Color_Space p3 = space_of(GCOL_PRIMARIES_DISPLAY_P3, GCOL_TRANSFER_SRGB);
  GSHSCENE_Mask_Resolved r;
  ASSERT_EQ(gshscene_mask_resolve(mask.get(), &p3, &r), GSHSCENE_OK);
  EXPECT_NEAR(r.k[0], 0.2290, 2e-3);
  EXPECT_EQ(r.luminance_space.primaries, GCOL_PRIMARIES_DISPLAY_P3);
}

TEST(MaskCoefficients, TheSpacesResolveAsMK2Says) {
  World w;
  GSHSCENE_Color_Space enclosing = space_of(GCOL_PRIMARIES_DISPLAY_P3, GCOL_TRANSFER_LINEAR);
  GSHSCENE_Mask m = w.luminance();
  const GSHSCENE_Resource * raw = nullptr;
  ASSERT_EQ(w.intern(m, &raw), GSHSCENE_OK);
  Ref plain(raw);
  GSHSCENE_Mask_Resolved r;
  ASSERT_EQ(gshscene_mask_resolve(plain.get(), &enclosing, &r), GSHSCENE_OK);
  EXPECT_EQ(r.render_space.primaries, GCOL_PRIMARIES_DISPLAY_P3) << "R is the enclosing space when zero";
  EXPECT_EQ(r.luminance_space.primaries, GCOL_PRIMARIES_DISPLAY_P3) << "L is R when zero";
  EXPECT_EQ(r.backdrop_space.primaries, GCOL_PRIMARIES_DISPLAY_P3) << "the backdrop's space is L when zero";
  EXPECT_EQ(gshscene_mask_resolve(plain.get(), nullptr, &r), GSHSCENE_ERR_INVALID) << "a luminance mask with no space to resolve in";
  GSHSCENE_Color_Space zero{};
  EXPECT_EQ(gshscene_mask_resolve(plain.get(), &zero, &r), GSHSCENE_ERR_INVALID);
  m.render_space = srgb_space();
  m.space = linear_srgb_space();
  ASSERT_EQ(w.intern(m, &raw), GSHSCENE_OK);
  Ref named(raw);
  ASSERT_EQ(gshscene_mask_resolve(named.get(), nullptr, &r), GSHSCENE_OK);
  EXPECT_EQ(r.render_space.transfer, GCOL_TRANSFER_SRGB);
  EXPECT_EQ(r.luminance_space.transfer, GCOL_TRANSFER_LINEAR);
  EXPECT_EQ(r.backdrop_space.transfer, GCOL_TRANSFER_LINEAR);
  // Alpha has no color: nothing to resolve but a named render space.
  ASSERT_EQ(w.intern(w.alpha(), &raw), GSHSCENE_OK);
  Ref alpha(raw);
  ASSERT_EQ(gshscene_mask_resolve(alpha.get(), nullptr, &r), GSHSCENE_OK);
  EXPECT_EQ(r.k[0], 0.0);
  EXPECT_EQ(gshscene_mask_resolve(nullptr, &enclosing, &r), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_mask_resolve(w.pic.get(), &enclosing, &r), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_mask_resolve(alpha.get(), &enclosing, nullptr), GSHSCENE_ERR_INVALID);
}

// --- MK9: what a mask needs --------------------------------------------------------------------------------------------------

TEST(MaskRequired, EachFeatureIsItsOwnBit) {
  World w;
  EXPECT_EQ(gshscene_mask_required(nullptr), 0u);
  EXPECT_EQ(gshscene_mask_required(w.pic.get()), 0u);
  EXPECT_EQ(GSHSCENE_MASK_NATIVE_ALL, 0xfffu);
  auto required = [&](const GSHSCENE_Mask & m) {
    const GSHSCENE_Resource * raw = nullptr;
    EXPECT_EQ(w.intern(m, &raw), GSHSCENE_OK);
    Ref hold(raw);
    return gshscene_mask_required(raw);
  };
  EXPECT_EQ(required(w.alpha()), (uint32_t)GSHSCENE_MASK_NATIVE_ALPHA);
  EXPECT_EQ(required(w.luminance()), (uint32_t)(GSHSCENE_MASK_NATIVE_LUMINANCE | GSHSCENE_MASK_NATIVE_COEFF_W3C));
  GSHSCENE_Mask m = w.luminance();
  m.coefficients = GSHSCENE_MASK_COEFF_REC709;
  EXPECT_EQ(required(m), (uint32_t)(GSHSCENE_MASK_NATIVE_LUMINANCE | GSHSCENE_MASK_NATIVE_COEFF_REC709));
  m.coefficients = GSHSCENE_MASK_COEFF_LUM;
  EXPECT_EQ(required(m), (uint32_t)(GSHSCENE_MASK_NATIVE_LUMINANCE | GSHSCENE_MASK_NATIVE_COEFF_LUM));
  m.coefficients = GSHSCENE_MASK_COEFF_SPACE_Y;
  EXPECT_EQ(required(m), (uint32_t)(GSHSCENE_MASK_NATIVE_LUMINANCE | GSHSCENE_MASK_NATIVE_COEFF_SPACE_Y));
  m = w.luminance();
  m.backdrop[1] = 0.5;
  EXPECT_EQ(required(m), (uint32_t)(GSHSCENE_MASK_NATIVE_LUMINANCE | GSHSCENE_MASK_NATIVE_COEFF_W3C | GSHSCENE_MASK_NATIVE_BACKDROP));
  for (int component = 0; component < 3; component++) {
    m = w.luminance();
    m.backdrop[component] = 0.5;
    EXPECT_EQ(required(m), (uint32_t)(GSHSCENE_MASK_NATIVE_LUMINANCE | GSHSCENE_MASK_NATIVE_COEFF_W3C | GSHSCENE_MASK_NATIVE_BACKDROP))
        << "a backdrop with only component " << component << " lit";
  }
  m = w.luminance();
  m.backdrop_space = linear_srgb_space();
  EXPECT_EQ(required(m), (uint32_t)(GSHSCENE_MASK_NATIVE_LUMINANCE | GSHSCENE_MASK_NATIVE_COEFF_W3C | GSHSCENE_MASK_NATIVE_SPACES))
      << "a backdrop space alone";
  m = w.luminance();
  m.space = linear_srgb_space();
  EXPECT_EQ(required(m), (uint32_t)(GSHSCENE_MASK_NATIVE_LUMINANCE | GSHSCENE_MASK_NATIVE_COEFF_W3C | GSHSCENE_MASK_NATIVE_SPACES));
  m = w.alpha();
  m.render_space = linear_srgb_space();
  EXPECT_EQ(required(m), (uint32_t)(GSHSCENE_MASK_NATIVE_ALPHA | GSHSCENE_MASK_NATIVE_SPACES));
  m = w.alpha();
  m.acts_on = GSHSCENE_MASK_ACTS_ON_SHAPE;
  EXPECT_EQ(required(m), (uint32_t)(GSHSCENE_MASK_NATIVE_ALPHA | GSHSCENE_MASK_NATIVE_ACTS_ON_SHAPE));
  m = w.alpha();
  m.transfer.kind = GSHSCENE_TRANSFER_EXPONENTIAL;
  m.transfer.c1 = 1.0;
  m.transfer.n = 2.0;
  EXPECT_EQ(required(m), (uint32_t)(GSHSCENE_MASK_NATIVE_ALPHA | GSHSCENE_MASK_NATIVE_TRANSFER_EXPONENTIAL));
  Ref table(known_mask(w.s.store, w.pic.get(), 2));
  EXPECT_EQ(gshscene_mask_required(table.get()),
      (uint32_t)(GSHSCENE_MASK_NATIVE_LUMINANCE | GSHSCENE_MASK_NATIVE_COEFF_LUM | GSHSCENE_MASK_NATIVE_TRANSFER_TABLE));
  Ref stitched(known_mask(w.s.store, w.pic.get(), 3));
  EXPECT_EQ(gshscene_mask_required(stitched.get()),
      (uint32_t)(GSHSCENE_MASK_NATIVE_ALPHA | GSHSCENE_MASK_NATIVE_TRANSFER_STITCHING | GSHSCENE_MASK_NATIVE_SPACES));
  Ref full(known_mask(w.s.store, w.pic.get(), 1));
  EXPECT_EQ(gshscene_mask_required(full.get()),
      (uint32_t)(GSHSCENE_MASK_NATIVE_LUMINANCE | GSHSCENE_MASK_NATIVE_COEFF_W3C | GSHSCENE_MASK_NATIVE_BACKDROP |
          GSHSCENE_MASK_NATIVE_TRANSFER_EXPONENTIAL | GSHSCENE_MASK_NATIVE_SPACES | GSHSCENE_MASK_NATIVE_ACTS_ON_SHAPE));
}

// MK9: the bits a sink states its features in are pinned.
TEST(MaskRequired, TheFeatureBitsArePinned) {
  EXPECT_EQ(GSHSCENE_MASK_NATIVE_ALPHA, 1u << 0);
  EXPECT_EQ(GSHSCENE_MASK_NATIVE_LUMINANCE, 1u << 1);
  EXPECT_EQ(GSHSCENE_MASK_NATIVE_BACKDROP, 1u << 2);
  EXPECT_EQ(GSHSCENE_MASK_NATIVE_TRANSFER_EXPONENTIAL, 1u << 3);
  EXPECT_EQ(GSHSCENE_MASK_NATIVE_TRANSFER_TABLE, 1u << 4);
  EXPECT_EQ(GSHSCENE_MASK_NATIVE_TRANSFER_STITCHING, 1u << 5);
  EXPECT_EQ(GSHSCENE_MASK_NATIVE_COEFF_W3C, 1u << 6);
  EXPECT_EQ(GSHSCENE_MASK_NATIVE_COEFF_REC709, 1u << 7);
  EXPECT_EQ(GSHSCENE_MASK_NATIVE_COEFF_LUM, 1u << 8);
  EXPECT_EQ(GSHSCENE_MASK_NATIVE_COEFF_SPACE_Y, 1u << 9);
  EXPECT_EQ(GSHSCENE_MASK_NATIVE_SPACES, 1u << 10);
  EXPECT_EQ(GSHSCENE_MASK_NATIVE_ACTS_ON_SHAPE, 1u << 11);
  EXPECT_EQ(GSHSCENE_MASK_NATIVE_ALL, (1u << 12) - 1u);
}

// --- MK12: the picture depth counts a mask exactly as it counts an instance ---------------------------------------------

namespace {

/** The picture at the top of a chain @p levels deep, each level holding the one below as an instance or as a mask; or 0 at the first refusal. */
GSHSCENE_Result chain_of(GSHSCENE_Store * store, const GSHSCENE_Resource * sq, int levels, bool through_mask,
    const GSHSCENE_Resource ** out) {
  const GSHSCENE_Resource * below = nullptr;
  for (int level = 0; level < levels; level++) {
    GSHSCENE_List * l = nullptr;
    EXPECT_EQ(gshscene_list_create(nullptr, nullptr, store, &l), GSHSCENE_OK);
    if (below) {
      if (through_mask) {
        GSHSCENE_Mask m;
        std::memset(&m, 0, sizeof m);
        m.picture = below;
        m.mode = GSHSCENE_MASK_ALPHA;
        const GSHSCENE_Resource * mask = nullptr;
        EXPECT_EQ(gshscene_store_intern_mask(store, &m, &mask), GSHSCENE_OK);
        GSHSCENE_Group g = make_group(1.0);
        g.soft_mask = mask;
        EXPECT_EQ(gshscene_list_group_push(l, &g, 0, 0), GSHSCENE_OK) << "the list holds a group of any mask depth";
        gshscene_list_fill(l, sq, nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0);
        gshscene_list_group_pop(l);
        gshscene_resource_release(mask);
      } else {
        EXPECT_EQ(gshscene_list_instance(l, below, nullptr, 0, 0), GSHSCENE_OK);
      }
    } else {
      gshscene_list_fill(l, sq, nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0);
    }
    gshscene_list_close(l);
    const GSHSCENE_Resource * picture = nullptr;
    GSHSCENE_Result r = gshscene_store_intern_picture(store, l, &picture);
    gshscene_list_destroy(l);
    gshscene_resource_release(below);
    if (r != GSHSCENE_OK) {
      return r;
    }
    below = picture;
  }
  *out = below;
  return GSHSCENE_OK;
}

}  // namespace

TEST(MaskLimits, ThePictureDepthCountsAMaskExactlyAsAnInstanceAtTheBoundAndOnePast) {
  for (size_t bound : {1u, 2u, 3u}) {
    GSHSCENE_Limits limits;
    gshscene_limits_default(&limits);
    limits.max_picture_depth = bound;
    for (bool through_mask : {false, true}) {
      StoreHolder s(&limits);
      Ref sq(intern_square(s.store));
      const GSHSCENE_Resource * top = nullptr;
      // A chain of `bound` pictures is `bound` deep: allowed; one more is past the bound.
      ASSERT_EQ(chain_of(s.store, sq.get(), (int)bound, through_mask, &top), GSHSCENE_OK) << bound << " " << through_mask;
      Ref hold(top);
      EXPECT_EQ(gshscene_resource_picture_depth(top), bound) << "a mask is one level, as an instance is";
      const GSHSCENE_Resource * past = nullptr;
      EXPECT_EQ(chain_of(s.store, sq.get(), (int)bound + 1, through_mask, &past), GSHSCENE_ERR_LIMIT)
          << bound << (through_mask ? " through masks" : " through instances") << ": one past the bound";
      EXPECT_EQ(past, nullptr);
    }
  }
}

TEST(MaskLimits, AMaskOfAMaskCountsBothLevelsInTheListThatHoldsIt) {
  // A list holding a group whose mask's picture holds a group with a mask, and so on three deep: its picture is one level
  // more, counted through every mask (the planning pass sizes its walk from it).
  StoreHolder s;
  Ref sq(intern_square(s.store));
  const GSHSCENE_Resource * top = nullptr;
  ASSERT_EQ(chain_of(s.store, sq.get(), 3, true, &top), GSHSCENE_OK);
  Ref hold(top);
  GSHSCENE_List * l = nullptr;
  ASSERT_EQ(gshscene_list_create(nullptr, nullptr, s.store, &l), GSHSCENE_OK);
  GSHSCENE_Mask m;
  std::memset(&m, 0, sizeof m);
  m.picture = top;
  m.mode = GSHSCENE_MASK_ALPHA;
  const GSHSCENE_Resource * mask = nullptr;
  ASSERT_EQ(gshscene_store_intern_mask(s.store, &m, &mask), GSHSCENE_OK);
  Ref mask_hold(mask);
  GSHSCENE_Group g = make_group(1.0);
  g.soft_mask = mask;
  ASSERT_EQ(gshscene_list_group_push(l, &g, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_group_pop(l), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l), GSHSCENE_OK);
  const GSHSCENE_Resource * picture = nullptr;
  ASSERT_EQ(gshscene_store_intern_picture(s.store, l, &picture), GSHSCENE_OK);
  Ref picture_hold(picture);
  EXPECT_EQ(gshscene_resource_picture_depth(picture), 4u) << "the list holds a mask of depth 3: it is one level more";
  gshscene_list_destroy(l);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
