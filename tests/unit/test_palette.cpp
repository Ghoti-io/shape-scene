/**
 * @file
 *
 * The palette (story 14c; N14, paint.h, resource.h, fixed.h): the palette resource and its integer constructor; the
 * palette parameter of a list and of a version (hash, equality, pictures, merge, diff and damage); the palette paint,
 * its one resolution and the planning pass's refusal of a missing entry; and the float-free recording calls (the
 * design map of each op, the fixed paint that lowers 0xFFFF to the foreground, and the truncation that makes a
 * lowering atomic).
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "test_helpers.h"

#include "../alloc_tracker.h"
#include "../glyph_test_provider.h"
#include "../scene_fixtures.h"
#include "../scene_helpers.h"

#include <cmath>
#include <cstring>
#include <functional>
#include <vector>

using namespace scene;

namespace {

const double kNaN = std::nan("");

GSHSCENE_Color color(double r, double g, double b, double a, GSHSCENE_Color_Space sp = GSHSCENE_Color_Space{}) {
  GSHSCENE_Color c = {{r, g, b, a}, sp};
  return c;
}

const GSHSCENE_Resource * palette_of(GSHSCENE_Store * store, std::vector<GSHSCENE_Color> colors) {
  const GSHSCENE_Resource * r = nullptr;
  EXPECT_EQ(gshscene_store_intern_palette(store, colors.size(), colors.empty() ? nullptr : colors.data(), &r),
      GSHSCENE_OK);
  return r;
}

GSHSCENE_Paint entry_paint(uint32_t entry, double alpha = 1.0) {
  GSHSCENE_Paint p{};
  p.kind = GSHSCENE_PAINT_PALETTE;
  p.entry = entry;
  p.rgba[3] = alpha;
  return p;
}

uint64_t bits_of(double x) {
  uint64_t b;
  std::memcpy(&b, &x, sizeof b);
  return b;
}

bool same_color(const GSHSCENE_Color & a, const GSHSCENE_Color & b) {
  for (int k = 0; k < 4; k++) {
    if (bits_of(a.rgba[k]) != bits_of(b.rgba[k])) {
      return false;
    }
  }
  return a.space.primaries == b.space.primaries && a.space.transfer == b.space.transfer &&
         bits_of(a.space.gamma) == bits_of(b.space.gamma);
}

/** A store, a bound face of the test provider and what the test interns, released at the end. */
struct Faces {
  GSHSCENE_Test_Provider tp{};
  StoreHolder s;
  std::vector<const GSHSCENE_Resource *> held;
  explicit Faces(uint32_t upem = 2048) {
    tp.upem = upem;
  }
  ~Faces() {
    for (const GSHSCENE_Resource * r : held) {
      gshscene_resource_release(r);
    }
  }
  const GSHSCENE_Resource * keep(const GSHSCENE_Resource * r) {
    held.push_back(r);
    return r;
  }
  const GSHSCENE_Resource * face(int bound = 1) {
    if (bound) {
      return keep(gshscene_tp_face(s.store, &tp, 1, 0x77, 0));
    }
    GSHSCENE_Face_Desc desc{};
    desc.font_key = 0x78;
    const GSHSCENE_Resource * r = nullptr;
    EXPECT_EQ(gshscene_store_intern_face(s.store, &desc, &r), GSHSCENE_OK);
    return keep(r);
  }
  /** A one-glyph run of @p f at @p size, its glyph at (x, y). */
  const GSHSCENE_Resource * run(const GSHSCENE_Resource * f, double size, double x, double y) {
    return keep(intern_font_run(s.store, f, size, {GSHSCENE_Glyph{3, x, y, nullptr}}));
  }
};

GSHSCENE_Fixed_Op op(uint32_t kind, uint32_t bits, std::vector<int32_t> v) {
  GSHSCENE_Fixed_Op o{};
  o.kind = kind;
  o.bits = bits;
  for (size_t i = 0; i < v.size(); i++) {
    o.v[i] = v[i];
  }
  return o;
}

const GSHSCENE_Paint_Fixed kOpaqueEntry0 = {GSHSCENE_FIXED_PAINT_PALETTE, 0, 1, 0};

/** The map of the one fill a fresh list records with @p ops on @p run (asserting that it is recorded). */
GSHGEOM_Map item_map(GSHSCENE_Store * store, const GSHSCENE_Resource * run, const std::vector<GSHSCENE_Fixed_Op> & ops) {
  ListHolder l(store);
  GSHGEOM_Map out;
  gshgeom_map_identity(2, &out);
  EXPECT_EQ(gshscene_list_fill_fixed(l.list, run, ops.empty() ? nullptr : ops.data(), ops.size(), GSHGEOM_FILL_NONZERO,
                &kOpaqueEntry0, 0),
      GSHSCENE_OK);
  const GSHSCENE_Item * item = nullptr;
  if (gshscene_list_item(l.list, 0, &item) == GSHSCENE_OK) {
    out = item->map;
  }
  return out;
}

GSHGEOM_Point apply(const GSHGEOM_Map & m, double x, double y) {
  GSHGEOM_Point p{x, y, 0.0};
  GSHGEOM_Point q{};
  EXPECT_EQ(gshgeom_map_apply_point(&m, p, &q), GSHGEOM_OK);
  return q;
}

}  // namespace

// --- the palette resource ---------------------------------------------------------------------------------------------

TEST(Palette, InternsByContentAndReadsBack) {
  StoreHolder s;
  std::vector<GSHSCENE_Color> colors = {color(1, 0.5, 0.25, 1, srgb_space()), color(0, 2, -0.5, 0.5),
      color(0.125, 0, 1, 0, linear_srgb_space())};
  Ref a(palette_of(s.store, colors));
  Ref b(palette_of(s.store, colors));
  ASSERT_NE(a.get(), nullptr);
  EXPECT_EQ(a.get(), b.get()) << "equal content is one handle";
  EXPECT_EQ(gshscene_resource_kind(a.get()), GSHSCENE_RESOURCE_PALETTE);
  EXPECT_EQ(gshscene_resource_palette_count(a.get()), 3u);
  for (size_t i = 0; i < colors.size(); i++) {
    GSHSCENE_Color got;
    ASSERT_EQ(gshscene_resource_palette_color(a.get(), i, &got), GSHSCENE_OK);
    EXPECT_TRUE(same_color(got, colors[i])) << i;
  }
  GSHSCENE_Color got;
  EXPECT_EQ(gshscene_resource_palette_color(a.get(), 3, &got), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_resource_palette_color(nullptr, 0, &got), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_resource_palette_color(a.get(), 0, nullptr), GSHSCENE_ERR_INVALID);
  // A difference in any entry, any component, the space or the count is another palette.
  std::vector<std::vector<GSHSCENE_Color>> others = {
      {colors[0], colors[1]},
      {colors[0], colors[1], colors[2], colors[2]},
      {colors[1], colors[0], colors[2]},
      {color(1, 0.5, 0.25, 1, linear_srgb_space()), colors[1], colors[2]},
      {colors[0], colors[1], color(0.125, 0, 1, 0.5, linear_srgb_space())},
      {color(1, 0.5, 0.25, 1, srgb_space()), color(0, 2, -0.5, 0.5, space_of(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_GAMMA, 2.2)), colors[2]},
  };
  for (size_t i = 0; i < others.size(); i++) {
    Ref o(palette_of(s.store, others[i]));
    EXPECT_NE(o.get(), a.get()) << i;
    EXPECT_NE(gshscene_resource_hash(o.get()), gshscene_resource_hash(a.get())) << i;
  }
  // An empty palette is legal: every entry of it is missing.
  Ref empty(palette_of(s.store, {}));
  EXPECT_EQ(gshscene_resource_palette_count(empty.get()), 0u);
  // Readers of another kind answer nothing, whatever its own fields hold (a picture's depth is where a count is).
  Ref sq(intern_square(s.store));
  Ref face(intern_face(s.store));
  Ref run(intern_font_run(s.store, face.get(), 12.0, {GSHSCENE_Glyph{3, 1.0, 2.0, nullptr}}));
  ListHolder inner(s.store);
  ASSERT_EQ(gshscene_list_fill(inner.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(inner.list), GSHSCENE_OK);
  Ref picture;
  ASSERT_EQ(gshscene_store_intern_picture(s.store, inner.list, &picture.resource), GSHSCENE_OK);
  for (const GSHSCENE_Resource * other : {sq.get(), face.get(), run.get(), picture.get()}) {
    EXPECT_EQ(gshscene_resource_palette_count(other), 0u) << gshscene_resource_kind(other);
    EXPECT_EQ(gshscene_resource_palette_color(other, 0, &got), GSHSCENE_ERR_INVALID) << gshscene_resource_kind(other);
  }
  EXPECT_EQ(gshscene_resource_palette_count(nullptr), 0u);
}

TEST(Palette, EqualityIsOnTheEntriesWhereTheHashesCollide) {
  // Every content in one bucket: equality alone tells a palette from its prefix, its extension and one that differs
  // in an entry or only in its last one, in either order of interning.
  GSHSCENE_Color a = color(1, 0, 0, 1);
  GSHSCENE_Color b = color(0, 1, 0, 1);
  GSHSCENE_Color c = color(0, 0, 1, 1);
  std::vector<std::vector<GSHSCENE_Color>> contents = {{a}, {a, b}, {a, c}, {b}, {b, a}, {}};
  const size_t n = contents.size();
  for (int order = 0; order < 2; order++) {
    StoreHolder s(nullptr, nullptr, all_zero);
    std::vector<const GSHSCENE_Resource *> made(n, nullptr);
    for (size_t k = 0; k < n; k++) {
      size_t i = order ? n - 1 - k : k;
      made[i] = palette_of(s.store, contents[i]);
    }
    for (size_t i = 0; i < n; i++) {
      for (size_t j = i + 1; j < n; j++) {
        EXPECT_NE(made[i], made[j]) << order << ": " << i << " and " << j;
      }
      const GSHSCENE_Resource * again = palette_of(s.store, contents[i]);
      EXPECT_EQ(again, made[i]) << order << ": equal content gets its own handle back, " << i;
      gshscene_resource_release(again);
    }
    GSHSCENE_Store_Stats st;
    ASSERT_EQ(gshscene_store_stats(s.store, &st), GSHSCENE_OK);
    EXPECT_GT(st.collisions, 0u) << "the planted collision must have happened";
    for (const GSHSCENE_Resource * r : made) {
      gshscene_resource_release(r);
    }
  }
}

TEST(Palette, TheRgba8ConstructorIsCOver255InSrgb) {
  StoreHolder s;
  std::vector<uint8_t> bytes;
  for (int c = 0; c < 256; c++) {
    bytes.push_back((uint8_t)c);
  }
  Ref p;
  ASSERT_EQ(gshscene_store_intern_palette_rgba8(s.store, 64, bytes.data(), &p.resource), GSHSCENE_OK);
  ASSERT_EQ(gshscene_resource_palette_count(p.get()), 64u);
  for (size_t i = 0; i < 64; i++) {
    GSHSCENE_Color got;
    ASSERT_EQ(gshscene_resource_palette_color(p.get(), i, &got), GSHSCENE_OK);
    for (int k = 0; k < 4; k++) {
      volatile double num = (double)bytes[4 * i + (size_t)k];
      EXPECT_EQ(bits_of(got.rgba[k]), bits_of(num / 255.0)) << i << " " << k;
    }
    EXPECT_EQ(got.space.primaries, GCOL_PRIMARIES_SRGB);
    EXPECT_EQ(got.space.transfer, GCOL_TRANSFER_SRGB);
    EXPECT_EQ(bits_of(got.space.gamma), 0u);
  }
  // 255 is exactly 1, 0 exactly 0: an opaque entry is opaque.
  GSHSCENE_Color last;
  ASSERT_EQ(gshscene_resource_palette_color(p.get(), 63, &last), GSHSCENE_OK);
  EXPECT_EQ(last.rgba[3], 1.0);
  // It is the palette interned from those doubles.
  std::vector<GSHSCENE_Color> same;
  for (size_t i = 0; i < 64; i++) {
    GSHSCENE_Color c;
    gshscene_resource_palette_color(p.get(), i, &c);
    same.push_back(c);
  }
  Ref q(palette_of(s.store, same));
  EXPECT_EQ(q.get(), p.get());
  // Its own refusals.
  Ref r;
  EXPECT_EQ(gshscene_store_intern_palette_rgba8(nullptr, 1, bytes.data(), &r.resource), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_store_intern_palette_rgba8(s.store, 1, nullptr, &r.resource), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_store_intern_palette_rgba8(s.store, 1, bytes.data(), nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_store_intern_palette_rgba8(s.store, GSHSCENE_PALETTE_MAX_ENTRIES + 1u, bytes.data(), &r.resource),
      GSHSCENE_ERR_LIMIT)
      << "the count is checked before the bytes are read";
  EXPECT_EQ(r.get(), nullptr);
  std::vector<uint8_t> most(4u * GSHSCENE_PALETTE_MAX_ENTRIES, 0x80);
  Ref largest;
  ASSERT_EQ(gshscene_store_intern_palette_rgba8(s.store, GSHSCENE_PALETTE_MAX_ENTRIES, most.data(), &largest.resource),
      GSHSCENE_OK)
      << "the most entries";
  EXPECT_EQ(gshscene_resource_palette_count(largest.get()), (size_t)GSHSCENE_PALETTE_MAX_ENTRIES);
  ASSERT_EQ(gshscene_store_intern_palette_rgba8(s.store, 0, nullptr, &r.resource), GSHSCENE_OK);
  EXPECT_EQ(gshscene_resource_palette_count(r.get()), 0u);
}

TEST(Palette, BadColorsAndCountsAreRefusedAndBuildNothing) {
  StoreHolder s;
  GSHSCENE_Store_Stats before;
  ASSERT_EQ(gshscene_store_stats(s.store, &before), GSHSCENE_OK);
  const GSHSCENE_Resource * r = nullptr;
  GSHSCENE_Color good = color(0.5, 0.5, 0.5, 1);
  EXPECT_EQ(gshscene_store_intern_palette(nullptr, 1, &good, &r), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_store_intern_palette(s.store, 1, &good, nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_store_intern_palette(s.store, 1, nullptr, &r), GSHSCENE_ERR_INVALID);
  for (GSHSCENE_Color bad : {color(kNaN, 0, 0, 1), color(0, INFINITY, 0, 1), color(0, 0, 0, 1.5), color(0, 0, 0, -0.25),
           color(0, 0, 0, kNaN), color(0, 0, 0, 1, space_of(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_UNKNOWN))}) {
    GSHSCENE_Color two[2] = {good, bad};
    EXPECT_EQ(gshscene_store_intern_palette(s.store, 2, two, &r), GSHSCENE_ERR_INVALID);
  }
  GSHSCENE_Color pq = color(0, 0, 0, 1, space_of(GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_PQ));
  EXPECT_EQ(gshscene_store_intern_palette(s.store, 1, &pq, &r), GSHSCENE_ERR_UNSUPPORTED);
  std::vector<GSHSCENE_Color> many(GSHSCENE_PALETTE_MAX_ENTRIES + 1u, good);
  EXPECT_EQ(gshscene_store_intern_palette(s.store, many.size(), many.data(), &r), GSHSCENE_ERR_LIMIT);
  EXPECT_EQ(r, nullptr);
  GSHSCENE_Store_Stats after;
  ASSERT_EQ(gshscene_store_stats(s.store, &after), GSHSCENE_OK);
  EXPECT_EQ(after.created, before.created);
  EXPECT_EQ(after.live, 0u);
  // The largest palette is one entry short of 0x10000, so that its last entry is 0xFFFE.
  many.pop_back();
  Ref big(palette_of(s.store, many));
  EXPECT_EQ(gshscene_resource_palette_count(big.get()), (size_t)GSHSCENE_PALETTE_MAX_ENTRIES);
  EXPECT_EQ(GSHSCENE_PALETTE_MAX_ENTRIES, GSHSCENE_PALETTE_ENTRY_MAX + 1u);
  EXPECT_EQ(GSHSCENE_PALETTE_ENTRY_MAX, 0xFFFEu) << "0xFFFF is a font's foreground, never an entry";
}

TEST(Palette, ItsBytesAreAccountedAndTheLimitIsExact) {
  StoreHolder s;
  std::vector<GSHSCENE_Color> colors(5, color(0, 0, 0, 1));
  Ref p(palette_of(s.store, colors));
  EXPECT_EQ(gshscene_resource_bytes(p.get()), GSHSCENE_BYTES_BASE + 5 * GSHSCENE_BYTES_PER_COLOR);
  GSHSCENE_Limits limits;
  gshscene_limits_default(&limits);
  limits.max_bytes = GSHSCENE_BYTES_BASE + 5 * GSHSCENE_BYTES_PER_COLOR;
  StoreHolder tight(&limits);
  const GSHSCENE_Resource * r = nullptr;
  std::vector<GSHSCENE_Color> six(6, color(0, 0, 0, 1));
  EXPECT_EQ(gshscene_store_intern_palette(tight.store, 6, six.data(), &r), GSHSCENE_ERR_LIMIT);
  ASSERT_EQ(gshscene_store_intern_palette(tight.store, 5, colors.data(), &r), GSHSCENE_OK);
  gshscene_resource_release(r);
  GSHSCENE_Store_Stats st;
  ASSERT_EQ(gshscene_store_stats(tight.store, &st), GSHSCENE_OK);
  EXPECT_EQ(st.bytes, 0u) << "the last release gives the bytes back";
}

TEST(Palette, AnAllocationFailureAtEveryCallLeavesNothing) {
  std::vector<GSHSCENE_Color> colors = {color(1, 0, 0, 1), color(0, 1, 0, 1)};
  std::vector<uint8_t> bytes = {1, 2, 3, 4, 5, 6, 7, 8};
  for (int which = 0; which < 2; which++) {
    bool succeeded = false;
    for (long k = 0; !succeeded && k < 64; k++) {
      tracker::Tracker t;
      GSHSCENE_Allocator alloc = tracker::allocator_for(&t);
      {
        StoreHolder s(nullptr, &alloc);
        ASSERT_NE(s.store, nullptr);
        t.fail_from = t.calls + k;
        const GSHSCENE_Resource * r = nullptr;
        GSHSCENE_Result res = which ? gshscene_store_intern_palette_rgba8(s.store, 2, bytes.data(), &r)
                                    : gshscene_store_intern_palette(s.store, 2, colors.data(), &r);
        t.fail_from = -1;
        if (res == GSHSCENE_OK) {
          succeeded = true;
          gshscene_resource_release(r);
        } else {
          EXPECT_EQ(res, GSHSCENE_ERR_OOM) << which << " " << k;
          EXPECT_EQ(r, nullptr);
          GSHSCENE_Store_Stats st;
          ASSERT_EQ(gshscene_store_stats(s.store, &st), GSHSCENE_OK);
          EXPECT_EQ(st.live, 0u);
        }
      }
      EXPECT_TRUE(t.live.empty()) << which << " " << k << ": leaked";
    }
    EXPECT_TRUE(succeeded) << which;
  }
}

// --- the palette paint and its resolution (N14) -----------------------------------------------------------------------

TEST(PalettePaint, TheOneResolutionOfEachKind) {
  StoreHolder s;
  Ref p(palette_of(s.store, {color(1, 0.5, 0, 0.5, srgb_space()), color(0, 0, 1, 1)}));
  GSHSCENE_Color fg = color(0.25, 0.5, 0.75, 0.5, linear_srgb_space());
  GSHSCENE_Color out;
  GSHSCENE_Paint solid_paint = solid(0.1, 0.2, 0.3, 0.75);
  solid_paint.space = srgb_space();
  ASSERT_EQ(gshscene_paint_resolve(&solid_paint, &fg, p.get(), &out), GSHSCENE_OK);
  EXPECT_TRUE(same_color(out, color(0.1, 0.2, 0.3, 0.75, srgb_space())));
  GSHSCENE_Paint fgp = solid(0, 0, 0, 0.5);
  fgp.kind = GSHSCENE_PAINT_FOREGROUND;
  ASSERT_EQ(gshscene_paint_resolve(&fgp, &fg, p.get(), &out), GSHSCENE_OK);
  EXPECT_TRUE(same_color(out, color(0.25, 0.5, 0.75, 0.25, linear_srgb_space())));
  GSHSCENE_Paint e0 = entry_paint(0, 0.5);
  ASSERT_EQ(gshscene_paint_resolve(&e0, &fg, p.get(), &out), GSHSCENE_OK);
  EXPECT_TRUE(same_color(out, color(1, 0.5, 0, 0.25, srgb_space()))) << "the entry's alpha times the paint's";
  GSHSCENE_Paint e1 = entry_paint(1, 1.0);
  ASSERT_EQ(gshscene_paint_resolve(&e1, &fg, p.get(), &out), GSHSCENE_OK);
  EXPECT_TRUE(same_color(out, color(0, 0, 1, 1)));
  // Missing: past the count, or no palette at all.
  std::memset(&out, 0xAB, sizeof out);
  GSHSCENE_Color untouched = out;
  GSHSCENE_Paint e2 = entry_paint(2);
  EXPECT_EQ(gshscene_paint_resolve(&e2, &fg, p.get(), &out), GSHSCENE_ERR_REFUSED);
  EXPECT_EQ(gshscene_paint_resolve(&e0, &fg, nullptr, &out), GSHSCENE_ERR_REFUSED);
  EXPECT_EQ(std::memcmp(&out, &untouched, sizeof out), 0) << "written only on success";
  // Bad arguments.
  Ref sq(intern_square(s.store));
  EXPECT_EQ(gshscene_paint_resolve(&e0, &fg, sq.get(), &out), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_paint_resolve(nullptr, &fg, p.get(), &out), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_paint_resolve(&e0, nullptr, p.get(), &out), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_paint_resolve(&e0, &fg, p.get(), nullptr), GSHSCENE_ERR_INVALID);
  GSHSCENE_Paint g = solid(0, 0, 0, 1);
  g.kind = GSHSCENE_PAINT_GRADIENT;
  EXPECT_EQ(gshscene_paint_resolve(&g, &fg, p.get(), &out), GSHSCENE_ERR_INVALID);
  g.kind = GSHSCENE_PAINT_KIND_COUNT;
  EXPECT_EQ(gshscene_paint_resolve(&g, &fg, p.get(), &out), GSHSCENE_ERR_INVALID);
}

TEST(PalettePaint, AListTakesEntriesUpTo0xFFFEAndNoKindPastTheLast) {
  StoreHolder s;
  Ref sq(intern_square(s.store));
  ListHolder l(s.store);
  GSHSCENE_Paint last = entry_paint(0xFFFEu);
  EXPECT_EQ(gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &last, 0, 0), GSHSCENE_OK);
  GSHSCENE_Paint foreground = entry_paint(0xFFFFu);
  EXPECT_EQ(gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &foreground, 0, 0), GSHSCENE_ERR_INVALID)
      << "0xFFFF is lowered to the foreground by the fixed paint, and refused by the double one";
  for (uint32_t kind : {(uint32_t)GSHSCENE_PAINT_KIND_COUNT, 9u, 0xFFFFFFFFu}) {
    GSHSCENE_Paint unknown = solid(0, 0, 0, 1);
    unknown.kind = (GSHSCENE_Paint_Kind)kind;
    EXPECT_EQ(gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &unknown, 0, 0), GSHSCENE_ERR_INVALID)
        << kind;
  }
  EXPECT_EQ(gshscene_list_count(l.list), 1u);
}

TEST(PalettePaint, TheDispositionRefusesAMissingEntryAndOnlyThat) {
  StoreHolder s;
  Ref p(palette_of(s.store, {color(1, 0, 0, 1), color(0, 1, 0, 1)}));
  Ref sq(intern_square(s.store));
  GSHSCENE_Sink_Caps caps{};
  caps.item_kinds = ~UINT32_C(0);
  caps.composites = ~UINT32_C(0);
  caps.flags = ~UINT32_C(0);
  GSHSCENE_Item fill{};
  fill.kind = GSHSCENE_ITEM_FILL;
  fill.resource = sq.get();
  fill.paint = entry_paint(1);
  EXPECT_EQ(gshscene_caps_disposition(&caps, &fill, nullptr, p.get()).entry_class, 0);
  fill.paint = entry_paint(2);
  GSHSCENE_Disposition d = gshscene_caps_disposition(&caps, &fill, nullptr, p.get());
  EXPECT_EQ(d.entry_class, GSHSCENE_ENTRY_REFUSED);
  EXPECT_EQ(d.reason, GSHSCENE_REASON_PALETTE_ENTRY);
  fill.paint = entry_paint(0);
  EXPECT_EQ(gshscene_caps_disposition(&caps, &fill, nullptr, nullptr).reason, GSHSCENE_REASON_PALETTE_ENTRY)
      << "no palette: every entry is missing";
  // Only a palette paint of a fill is read: a solid or foreground fill and a clip push under no palette are drawn.
  fill.paint = solid(1, 0, 0, 1);
  EXPECT_EQ(gshscene_caps_disposition(&caps, &fill, nullptr, nullptr).entry_class, 0);
  fill.paint.kind = GSHSCENE_PAINT_FOREGROUND;
  EXPECT_EQ(gshscene_caps_disposition(&caps, &fill, nullptr, nullptr).entry_class, 0);
  // ...and the palette argument is not read for them, even one that is no palette.
  EXPECT_EQ(gshscene_caps_disposition(&caps, &fill, nullptr, sq.get()).entry_class, 0);
  fill.paint = solid(1, 0, 0, 1);
  EXPECT_EQ(gshscene_caps_disposition(&caps, &fill, nullptr, sq.get()).entry_class, 0);
  GSHSCENE_Item clip = fill;
  clip.kind = GSHSCENE_ITEM_CLIP_PUSH;
  clip.paint = entry_paint(9); // a clip has no paint; whatever its field says is not read
  EXPECT_EQ(gshscene_caps_disposition(&caps, &clip, nullptr, nullptr).entry_class, 0);
  // A kind the caps do not draw is refused for that first.
  caps.item_kinds = 0;
  fill.paint = entry_paint(5);
  EXPECT_EQ(gshscene_caps_disposition(&caps, &fill, nullptr, p.get()).reason, GSHSCENE_REASON_ITEM_KIND);
}

// --- the palette parameter of a list ----------------------------------------------------------------------------------

TEST(PaletteParameter, AListSetsReadsRetainsAndResetsIt) {
  StoreHolder s;
  StoreHolder other;
  Ref p(palette_of(s.store, {color(1, 0, 0, 1)}));
  Ref q(palette_of(s.store, {color(0, 1, 0, 1)}));
  Ref elsewhere(palette_of(other.store, {color(1, 0, 0, 1)}));
  Ref sq(intern_square(s.store));
  ListHolder l(s.store);
  EXPECT_EQ(gshscene_list_palette(l.list), nullptr);
  EXPECT_EQ(gshscene_list_palette(nullptr), nullptr);
  size_t refs = gshscene_resource_refcount(p.get());
  ASSERT_EQ(gshscene_list_set_palette(l.list, p.get()), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_palette(l.list), p.get());
  EXPECT_EQ(gshscene_resource_refcount(p.get()), refs + 1);
  ASSERT_EQ(gshscene_list_set_palette(l.list, p.get()), GSHSCENE_OK);
  EXPECT_EQ(gshscene_resource_refcount(p.get()), refs + 1) << "setting it again holds one reference";
  ASSERT_EQ(gshscene_list_set_palette(l.list, q.get()), GSHSCENE_OK);
  EXPECT_EQ(gshscene_resource_refcount(p.get()), refs);
  // Refused, changing nothing.
  EXPECT_EQ(gshscene_list_set_palette(l.list, sq.get()), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_set_palette(l.list, elsewhere.get()), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_set_palette(nullptr, p.get()), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_palette(l.list), q.get());
  ASSERT_EQ(gshscene_list_set_palette(l.list, nullptr), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_palette(l.list), nullptr);
  ASSERT_EQ(gshscene_list_set_palette(l.list, p.get()), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_set_palette(l.list, q.get()), GSHSCENE_ERR_INVALID) << "a closed list";
  ASSERT_EQ(gshscene_list_reset(l.list), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_palette(l.list), nullptr) << "a reset drops it";
  EXPECT_EQ(gshscene_resource_refcount(p.get()), refs);
  ASSERT_EQ(gshscene_list_set_palette(l.list, p.get()), GSHSCENE_OK);
  gshscene_list_destroy(l.list);
  l.list = nullptr;
  EXPECT_EQ(gshscene_resource_refcount(p.get()), refs) << "a destroy releases it";
}

TEST(PaletteParameter, ItIsInTheHashAndTheEqualityAndAPictureHasNone) {
  StoreHolder s;
  Ref p(palette_of(s.store, {color(1, 0, 0, 1)}));
  Ref q(palette_of(s.store, {color(0, 1, 0, 1)}));
  Ref sq(intern_square(s.store));
  GSHSCENE_Paint e0 = entry_paint(0);
  auto make = [&](const GSHSCENE_Resource * palette) {
    GSHSCENE_List * l = nullptr;
    gshscene_list_create(nullptr, nullptr, s.store, &l);
    EXPECT_EQ(gshscene_list_fill(l, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &e0, 0, 0), GSHSCENE_OK);
    EXPECT_EQ(gshscene_list_set_palette(l, palette), GSHSCENE_OK);
    EXPECT_EQ(gshscene_list_close(l), GSHSCENE_OK);
    return l;
  };
  GSHSCENE_List * none = make(nullptr);
  GSHSCENE_List * with_p = make(p.get());
  GSHSCENE_List * with_p2 = make(p.get());
  GSHSCENE_List * with_q = make(q.get());
  EXPECT_NE(gshscene_list_hash(none), gshscene_list_hash(with_p));
  EXPECT_NE(gshscene_list_hash(with_p), gshscene_list_hash(with_q));
  EXPECT_EQ(gshscene_list_hash(with_p), gshscene_list_hash(with_p2));
  EXPECT_TRUE(gshscene_list_equal(with_p, with_p2));
  EXPECT_FALSE(gshscene_list_equal(with_p, with_q));
  EXPECT_FALSE(gshscene_list_equal(none, with_p));
  // A picture resolves its palette paints against the list that draws it: it has no palette of its own.
  const GSHSCENE_Resource * pic = nullptr;
  EXPECT_EQ(gshscene_store_intern_picture(s.store, with_p, &pic), GSHSCENE_ERR_INVALID);
  ASSERT_EQ(gshscene_store_intern_picture(s.store, none, &pic), GSHSCENE_OK);
  gshscene_resource_release(pic);
  // An entry's number is hashed and compared.
  GSHSCENE_List * other_entry = nullptr;
  gshscene_list_create(nullptr, nullptr, s.store, &other_entry);
  GSHSCENE_Paint e1 = entry_paint(1);
  ASSERT_EQ(gshscene_list_fill(other_entry, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &e1, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(other_entry), GSHSCENE_OK);
  EXPECT_NE(gshscene_list_hash(none), gshscene_list_hash(other_entry));
  EXPECT_FALSE(gshscene_list_equal(none, other_entry));
  for (GSHSCENE_List * l : {none, with_p, with_p2, with_q, other_entry}) {
    gshscene_list_destroy(l);
  }
}

// --- the float-free calls (fixed.h) -----------------------------------------------------------------------------------

TEST(Fixed, TheFixedConstantsAreThePaintKinds) {
  EXPECT_EQ(GSHSCENE_FIXED_PAINT_FOREGROUND, (uint32_t)GSHSCENE_PAINT_FOREGROUND);
  EXPECT_EQ(GSHSCENE_FIXED_PAINT_PALETTE, (uint32_t)GSHSCENE_PAINT_PALETTE);
  EXPECT_EQ(GSHSCENE_FIXED_ENTRY_FOREGROUND, GSHSCENE_PALETTE_ENTRY_MAX + 1u);
  EXPECT_EQ((int)GSHSCENE_FIXED_AFFINE, 0);
  EXPECT_EQ((int)GSHSCENE_FIXED_TRANSLATE, 1);
  EXPECT_EQ((int)GSHSCENE_FIXED_SCALE, 2);
  EXPECT_EQ((int)GSHSCENE_FIXED_ROTATE, 3);
  EXPECT_EQ((int)GSHSCENE_FIXED_SKEW, 4);
  EXPECT_EQ((int)GSHSCENE_FIXED_OP_COUNT, 5);
}

TEST(Fixed, ThePaintLowersFFFFToTheForegroundAndKeepsEveryOtherEntry) {
  StoreHolder s;
  Ref sq(intern_square(s.store));
  ListHolder l(s.store);
  struct Case {
    GSHSCENE_Paint_Fixed paint;
    GSHSCENE_Paint_Kind kind;
    uint32_t entry;
    double alpha;
  } cases[] = {
      {{GSHSCENE_FIXED_PAINT_PALETTE, 0, 16384, 14}, GSHSCENE_PAINT_PALETTE, 0, 1.0},
      {{GSHSCENE_FIXED_PAINT_PALETTE, 0xFFFEu, 8192, 14}, GSHSCENE_PAINT_PALETTE, 0xFFFEu, 0.5},
      {{GSHSCENE_FIXED_PAINT_PALETTE, 0xFFFFu, 4096, 14}, GSHSCENE_PAINT_FOREGROUND, 0, 0.25},
      {{GSHSCENE_FIXED_PAINT_FOREGROUND, 0, 1, 0}, GSHSCENE_PAINT_FOREGROUND, 0, 1.0},
      {{GSHSCENE_FIXED_PAINT_PALETTE, 7, 0, 30}, GSHSCENE_PAINT_PALETTE, 7, 0.0},
      {{GSHSCENE_FIXED_PAINT_PALETTE, 7, 1 << 30, 30}, GSHSCENE_PAINT_PALETTE, 7, 1.0},
      {{GSHSCENE_FIXED_PAINT_PALETTE, 7, 3, 2}, GSHSCENE_PAINT_PALETTE, 7, 0.75},
  };
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    ASSERT_EQ(gshscene_list_fill_fixed(l.list, sq.get(), nullptr, 0, GSHGEOM_FILL_EVEN_ODD, &cases[i].paint, -2),
        GSHSCENE_OK)
        << i;
    const GSHSCENE_Item * item = nullptr;
    ASSERT_EQ(gshscene_list_item(l.list, i, &item), GSHSCENE_OK);
    EXPECT_EQ(item->paint.kind, cases[i].kind) << i;
    EXPECT_EQ(item->paint.entry, cases[i].entry) << i;
    EXPECT_EQ(item->paint.rgba[3], cases[i].alpha) << i;
    EXPECT_EQ(item->paint.rgba[0], 0.0) << i;
    EXPECT_EQ(item->fill_rule, GSHGEOM_FILL_EVEN_ODD);
    EXPECT_EQ(item->layer, -2);
    EXPECT_EQ(bits_of(item->depth), 0u);
    GSHGEOM_Map identity;
    gshgeom_map_identity(2, &identity);
    EXPECT_EQ(gshgeom_map_hash(&item->map), gshgeom_map_hash(&identity)) << "no ops: the identity";
  }
  // Refused, recording nothing.
  size_t count = gshscene_list_count(l.list);
  GSHSCENE_Paint_Fixed bad[] = {
      {GSHSCENE_FIXED_PAINT_PALETTE, 0x10000u, 1, 0},      // past 0xFFFF
      {GSHSCENE_FIXED_PAINT_PALETTE, 0, 2, 0},             // alpha above 1
      {GSHSCENE_FIXED_PAINT_PALETTE, 0, -1, 0},            // alpha below 0
      {GSHSCENE_FIXED_PAINT_PALETTE, 0, 1, 31},            // too many bits
      {GSHSCENE_FIXED_PAINT_PALETTE, 0, 0, 32},            // ...even for an alpha of zero
      {GSHSCENE_FIXED_PAINT_PALETTE, 0, 0, 63},
      {GSHSCENE_FIXED_PAINT_FOREGROUND, 1, 1, 0},          // a foreground with an entry
      {GSHSCENE_PAINT_SOLID, 0, 1, 0},                     // not a kind it takes
      {GSHSCENE_PAINT_GRADIENT, 0, 1, 0},
      {9, 0, 1, 0},
  };
  for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
    EXPECT_EQ(gshscene_list_fill_fixed(l.list, sq.get(), nullptr, 0, GSHGEOM_FILL_NONZERO, &bad[i], 0),
        GSHSCENE_ERR_INVALID)
        << i;
  }
  EXPECT_EQ(gshscene_list_fill_fixed(l.list, sq.get(), nullptr, 0, GSHGEOM_FILL_NONZERO, nullptr, 0), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_fill_fixed(nullptr, sq.get(), nullptr, 0, GSHGEOM_FILL_NONZERO, &kOpaqueEntry0, 0),
      GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_fill_fixed(l.list, nullptr, nullptr, 0, GSHGEOM_FILL_NONZERO, &kOpaqueEntry0, 0),
      GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_fill_fixed(l.list, sq.get(), nullptr, 0, (GSHGEOM_Fill_Rule)7, &kOpaqueEntry0, 0),
      GSHSCENE_ERR_INVALID)
      << "the list's own rules";
  EXPECT_EQ(gshscene_list_count(l.list), count);
}

TEST(Fixed, BadOpsAndResourcesAreInvalidAndRecordNothing) {
  Faces f;
  const GSHSCENE_Resource * face = f.face();
  const GSHSCENE_Resource * run = f.run(face, 16.0, 10.0, 20.0);
  const GSHSCENE_Resource * unbound = f.run(f.face(0), 16.0, 10.0, 20.0);
  const GSHSCENE_Resource * empty = f.keep(intern_font_run(f.s.store, face, 16.0, {}));
  const GSHSCENE_Resource * sq = f.keep(intern_square(f.s.store));
  GSHSCENE_Fixed_Op good = op(GSHSCENE_FIXED_TRANSLATE, 0, {1, 2});
  ListHolder l(f.s.store);
  auto fill = [&](const GSHSCENE_Resource * r, std::vector<GSHSCENE_Fixed_Op> ops) {
    return gshscene_list_fill_fixed(l.list, r, ops.data(), ops.size(), GSHGEOM_FILL_NONZERO, &kOpaqueEntry0, 0);
  };
  auto clip = [&](const GSHSCENE_Resource * r, std::vector<GSHSCENE_Fixed_Op> ops) {
    return gshscene_list_clip_push_fixed(l.list, r, ops.data(), ops.size(), GSHGEOM_FILL_NONZERO, 0);
  };
  const GSHSCENE_Result INV = GSHSCENE_ERR_INVALID;
  for (auto call : {0, 1}) {
    auto rec = [&](const GSHSCENE_Resource * r, std::vector<GSHSCENE_Fixed_Op> ops) {
      return call ? clip(r, ops) : fill(r, ops);
    };
    EXPECT_EQ(rec(sq, {good}), INV) << "a path has no design space";
    EXPECT_EQ(rec(unbound, {good}), INV) << "an unbound face states no upem";
    EXPECT_EQ(rec(empty, {good}), INV) << "a run with no glyph has no glyph to be about";
    EXPECT_EQ(rec(run, {op(GSHSCENE_FIXED_OP_COUNT, 0, {})}), INV);
    EXPECT_EQ(rec(run, {op(GSHSCENE_FIXED_TRANSLATE, 31, {1, 2})}), INV);
    EXPECT_EQ(rec(run, {op(GSHSCENE_FIXED_TRANSLATE, 0, {1, 2, 3})}), INV) << "an unused value is zero";
    EXPECT_EQ(rec(run, {op(GSHSCENE_FIXED_SCALE, 0, {1, 1, 0, 0, 5})}), INV);
    EXPECT_EQ(rec(run, {op(GSHSCENE_FIXED_ROTATE, 0, {1, 0, 0, 1})}), INV);
    EXPECT_EQ(rec(run, {op(GSHSCENE_FIXED_SKEW, 0, {1, 0, 0, 0, 1})}), INV);
    EXPECT_EQ(rec(run, {op(GSHSCENE_FIXED_SKEW, 2, {1, 0})}), INV) << "a skew of a quarter turn is infinite";
    EXPECT_EQ(rec(run, {op(GSHSCENE_FIXED_SKEW, 2, {0, -1})}), INV);
    EXPECT_EQ(rec(run, {good, op(GSHSCENE_FIXED_AFFINE, 0, {1, 0, 0, 1, 0, 0}), op(7, 0, {})}), INV);
    {
      size_t before = gshscene_list_count(l.list);
      EXPECT_EQ(rec(run, {op(GSHSCENE_FIXED_TRANSLATE, GSHSCENE_FIXED_MAX_BITS, {1, -1})}), GSHSCENE_OK)
          << "the most fraction bits";
      if (call) {
        ASSERT_EQ(gshscene_list_clip_pop(l.list), GSHSCENE_OK);
      }
      ASSERT_EQ(gshscene_list_truncate(l.list, before), GSHSCENE_OK);
    }
    std::vector<GSHSCENE_Fixed_Op> many(GSHSCENE_FIXED_MAX_OPS + 1u, good);
    EXPECT_EQ(rec(run, many), GSHSCENE_ERR_LIMIT);
    many.pop_back();
    size_t before = gshscene_list_count(l.list);
    EXPECT_EQ(rec(run, many), GSHSCENE_OK) << "the most ops";
    if (call) {
      ASSERT_EQ(gshscene_list_clip_pop(l.list), GSHSCENE_OK);
    }
    ASSERT_EQ(gshscene_list_truncate(l.list, before), GSHSCENE_OK);
    // A map past shape-geometry's bound is a limit.
    std::vector<GSHSCENE_Fixed_Op> huge(GSHSCENE_FIXED_MAX_OPS, op(GSHSCENE_FIXED_SCALE, 0, {1 << 30, 1 << 30}));
    EXPECT_EQ(rec(run, huge), GSHSCENE_ERR_LIMIT);
    EXPECT_EQ(call ? gshscene_list_clip_push_fixed(l.list, run, nullptr, 1, GSHGEOM_FILL_NONZERO, 0)
                   : gshscene_list_fill_fixed(l.list, run, nullptr, 1, GSHGEOM_FILL_NONZERO, &kOpaqueEntry0, 0),
        INV)
        << "a count without ops";
  }
  EXPECT_EQ(gshscene_list_count(l.list), 0u);
  ASSERT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
  EXPECT_EQ(fill(run, {}), INV) << "a closed list";
  EXPECT_EQ(clip(run, {}), INV);
  EXPECT_EQ(gshscene_list_group_push_fixed(l.list, 0, 0), INV);
  StoreHolder other;
  ListHolder elsewhere(other.store);
  EXPECT_EQ(gshscene_list_fill_fixed(elsewhere.list, run, nullptr, 0, GSHGEOM_FILL_NONZERO, &kOpaqueEntry0, 0), INV);
  EXPECT_EQ(gshscene_list_clip_push_fixed(elsewhere.list, run, nullptr, 0, GSHGEOM_FILL_NONZERO, 0), INV);
  EXPECT_EQ(gshscene_list_clip_push_fixed(nullptr, run, nullptr, 0, GSHGEOM_FILL_NONZERO, 0), INV);
  EXPECT_EQ(gshscene_list_clip_push_fixed(elsewhere.list, nullptr, nullptr, 0, GSHGEOM_FILL_NONZERO, 0), INV);
}

TEST(Fixed, EachOpPlacesTheDesignOutlineAsItsMapSays) {
  // A run at 20.48 model units an em on a 2048-unit face (s = 1/100), its glyph at (10, 20): a design point p is
  // drawn at g + s (p.x, -p.y).  Under a design map T the item map must draw it at g + s (T(p).x, -T(p).y).
  Faces f;
  const GSHSCENE_Resource * face = f.face();
  const double size = 20.48;
  const double s = size / 2048.0;
  const double gx = 10.0;
  const double gy = 20.0;
  const GSHSCENE_Resource * run = f.run(face, size, gx, gy);
  struct Case {
    const char * name;
    std::vector<GSHSCENE_Fixed_Op> ops;
    double m[4]; // T's linear part, row major, in design units, y up
    double t[2];
  } cases[] = {
      {"affine", {op(GSHSCENE_FIXED_AFFINE, 16, {65536, 32768, -16384, 65536, 100 << 16, -(200 << 16)})},
          {1.0, -0.25, 0.5, 1.0}, {100, -200}},
      {"translate", {op(GSHSCENE_FIXED_TRANSLATE, 0, {-30, 40})}, {1, 0, 0, 1}, {-30, 40}},
      {"scale about a center", {op(GSHSCENE_FIXED_SCALE, 14, {12288, 20480, 300 << 14, 0})}, {0.75, 0, 0, 1.25},
          {300 - 0.75 * 300, 0}},
      {"rotate a quarter turn", {op(GSHSCENE_FIXED_ROTATE, 15, {8192, 0, 0})}, {0, -1, 1, 0}, {0, 0}},
      {"rotate a half turn about a center", {op(GSHSCENE_FIXED_ROTATE, 15, {16384, 50 << 15, -(50 << 15)})},
          {-1, 0, 0, -1}, {100, -100}},
      {"skew in x by an eighth turn", {op(GSHSCENE_FIXED_SKEW, 16, {8192, 0})}, {1, -1, 0, 1}, {0, 0}},
      {"skew in y by an eighth turn about a center", {op(GSHSCENE_FIXED_SKEW, 16, {0, 8192, 100 << 16, 200 << 16})},
          {1, 0, 1, 1}, {0, -100}},
      {"a chain: translate then scale (the scale applied first)",
          {op(GSHSCENE_FIXED_TRANSLATE, 0, {5, 7}), op(GSHSCENE_FIXED_SCALE, 0, {2, 3, 0, 0})}, {2, 0, 0, 3}, {5, 7}},
  };
  const double pts[4][2] = {{0, 0}, {100, 0}, {0, 100}, {-37, 512}};
  for (const Case & c : cases) {
    GSHGEOM_Map m = item_map(f.s.store, run, c.ops);
    for (const auto & p : pts) {
      double tx = c.m[0] * p[0] + c.m[1] * p[1] + c.t[0];
      double ty = c.m[2] * p[0] + c.m[3] * p[1] + c.t[1];
      GSHGEOM_Point got = apply(m, gx + s * p[0], gy - s * p[1]);
      EXPECT_NEAR(got.x, gx + s * tx, 1e-9) << c.name << " at " << p[0] << ", " << p[1];
      EXPECT_NEAR(got.y, gy - s * ty, 1e-9) << c.name << " at " << p[0] << ", " << p[1];
    }
  }
}

TEST(Fixed, TheItemMapIsTheStatedChainBitForBit) {
  Faces f(1000);
  const GSHSCENE_Resource * face = f.face();
  const double size = 13.0;
  const double gx = 3.25;
  const double gy = -7.5;
  const GSHSCENE_Resource * run = f.run(face, size, gx, gy);
  // T is shape-geometry's composition of the ops' maps; the item map is the chain fixed.h states.
  std::vector<GSHSCENE_Fixed_Op> ops = {op(GSHSCENE_FIXED_ROTATE, 15, {5000, 17, -3}),
      op(GSHSCENE_FIXED_SKEW, 16, {3000, -2000, 1, 2}), op(GSHSCENE_FIXED_SCALE, 14, {20000, 9000, 40, 50}),
      op(GSHSCENE_FIXED_AFFINE, 16, {70000, -3000, 1000, 60000, 123456, -654321})};
  GSHGEOM_Map maps[4];
  {
    GSHGEOM_Map r, a, b, k;
    ASSERT_EQ(gshgeom_map_rotate(GSHGEOM_ANGLE_TURNS, 5000.0 / 32768.0, &r), GSHGEOM_OK);
    gshgeom_map_translate(2, 17.0 / 32768.0, -3.0 / 32768.0, 0, &a);
    gshgeom_map_translate(2, -17.0 / 32768.0, 3.0 / 32768.0, 0, &b);
    GSHGEOM_Map chain[3] = {a, r, b};
    ASSERT_EQ(gshgeom_map_compose_chain(chain, 3, nullptr, &maps[0]), GSHGEOM_OK);
    GSHGEOM_Map rx, ry;
    gshgeom_map_rotate(GSHGEOM_ANGLE_TURNS, 3000.0 / 65536.0, &rx);
    gshgeom_map_rotate(GSHGEOM_ANGLE_TURNS, -2000.0 / 65536.0, &ry);
    const double km[4] = {1.0, -(rx.m[1][0] / rx.m[0][0]), ry.m[1][0] / ry.m[0][0], 1.0};
    const double zero[2] = {0, 0};
    gshgeom_map_from_matrix(2, km, zero, &k);
    gshgeom_map_translate(2, 1.0 / 65536.0, 2.0 / 65536.0, 0, &a);
    gshgeom_map_translate(2, -1.0 / 65536.0, -2.0 / 65536.0, 0, &b);
    GSHGEOM_Map chain2[3] = {a, k, b};
    ASSERT_EQ(gshgeom_map_compose_chain(chain2, 3, nullptr, &maps[1]), GSHGEOM_OK);
    const double sm[4] = {20000.0 / 16384.0, 0, 0, 9000.0 / 16384.0};
    gshgeom_map_from_matrix(2, sm, zero, &k);
    gshgeom_map_translate(2, 40.0 / 16384.0, 50.0 / 16384.0, 0, &a);
    gshgeom_map_translate(2, -40.0 / 16384.0, -50.0 / 16384.0, 0, &b);
    GSHGEOM_Map chain3[3] = {a, k, b};
    ASSERT_EQ(gshgeom_map_compose_chain(chain3, 3, nullptr, &maps[2]), GSHGEOM_OK);
    const double am[4] = {70000.0 / 65536.0, 1000.0 / 65536.0, -3000.0 / 65536.0, 60000.0 / 65536.0};
    const double at[2] = {123456.0 / 65536.0, -654321.0 / 65536.0};
    gshgeom_map_from_matrix(2, am, at, &maps[3]);
  }
  GSHGEOM_Map t;
  ASSERT_EQ(gshgeom_map_compose_chain(maps, 4, nullptr, &t), GSHGEOM_OK);
  const double s = size / 1000.0;
  GSHGEOM_Map chain[5];
  gshgeom_map_translate(2, gx, gy, 0, &chain[0]);
  gshgeom_map_flip_y(2, &chain[1]);
  const double lm[4] = {t.m[0][0], t.m[0][1], t.m[1][0], t.m[1][1]};
  const double lt[2] = {s * t.t[0], s * t.t[1]};
  gshgeom_map_from_matrix(2, lm, lt, &chain[2]);
  gshgeom_map_flip_y(2, &chain[3]);
  gshgeom_map_translate(2, -gx, -gy, 0, &chain[4]);
  GSHGEOM_Map want;
  ASSERT_EQ(gshgeom_map_compose_chain(chain, 5, nullptr, &want), GSHGEOM_OK);
  GSHGEOM_Map got = item_map(f.s.store, run, ops);
  EXPECT_EQ(gshgeom_map_hash(&got), gshgeom_map_hash(&want));
  {
    ListHolder l(f.s.store);
    ASSERT_EQ(gshscene_list_clip_push_fixed(l.list, run, ops.data(), ops.size(), GSHGEOM_FILL_NONZERO, 0), GSHSCENE_OK);
    const GSHSCENE_Item * clip = nullptr;
    ASSERT_EQ(gshscene_list_item(l.list, 0, &clip), GSHSCENE_OK);
    EXPECT_EQ(gshgeom_map_hash(&clip->map), gshgeom_map_hash(&want)) << "a clip push is placed as a fill is";
  }
  for (int i = 0; i < 2; i++) {
    EXPECT_EQ(bits_of(got.t[i]), bits_of(want.t[i])) << i;
    for (int j = 0; j < 2; j++) {
      EXPECT_EQ(bits_of(got.m[i][j]), bits_of(want.m[i][j])) << i << j;
    }
  }
}

TEST(Fixed, ALinearPartThatIsTheIdentityIsATranslationAndKeepsTheFastTier) {
  Faces f;
  const GSHSCENE_Resource * face = f.face();
  const GSHSCENE_Resource * run = f.run(face, 16.0, 3.0, 9.0);
  // Two translations, and a rotation by a whole turn (exactly the identity): a translation by s b, y flipped.
  std::vector<GSHSCENE_Fixed_Op> ops = {op(GSHSCENE_FIXED_TRANSLATE, 0, {256, -128}),
      op(GSHSCENE_FIXED_ROTATE, 0, {1, 0, 0}), op(GSHSCENE_FIXED_TRANSLATE, 1, {256, 0})};
  GSHGEOM_Map got = item_map(f.s.store, run, ops);
  GSHGEOM_Map want;
  const double s = 16.0 / 2048.0;
  gshgeom_map_translate(2, s * 384.0, -(s * -128.0), 0, &want);
  EXPECT_EQ(gshgeom_map_hash(&got), gshgeom_map_hash(&want));
  EXPECT_EQ(got.kind, (uint32_t)GSHGEOM_MAP_TRANSLATES) << "no flip, no rotation in the tag: the run stays fast";
  GSHSCENE_Glyph_Route route;
  ASSERT_EQ(gshscene_glyph_route(run, &got, GSHSCENE_TARGET_RASTER, &route), GSHSCENE_OK);
  EXPECT_EQ(route.route, GSHSCENE_GLYPH_FAST);
  // A rotation that is not the identity takes the general route.
  GSHGEOM_Map turned = item_map(f.s.store, run, {op(GSHSCENE_FIXED_ROTATE, 3, {1, 0, 0})});
  ASSERT_EQ(gshscene_glyph_route(run, &turned, GSHSCENE_TARGET_RASTER, &route), GSHSCENE_OK);
  EXPECT_EQ(route.route, GSHSCENE_GLYPH_GENERAL);
}

TEST(Fixed, TheGroupIsTheOperatorAndNothingElse) {
  StoreHolder s;
  ListHolder l(s.store);
  for (uint32_t c = 0; c < GSHSCENE_COMPOSITE_COUNT; c++) {
    ASSERT_EQ(gshscene_list_group_push_fixed(l.list, c, (int32_t)c - 3), GSHSCENE_OK) << c;
    ASSERT_EQ(gshscene_list_group_pop(l.list), GSHSCENE_OK);
    const GSHSCENE_Item * item = nullptr;
    ASSERT_EQ(gshscene_list_item(l.list, 2 * c, &item), GSHSCENE_OK);
    EXPECT_EQ(item->group.composite, c);
    EXPECT_EQ(item->group.opacity, 1.0);
    EXPECT_EQ(item->group.ordering, (uint32_t)GSHSCENE_ORDER_FLAT);
    EXPECT_TRUE(gshscene_color_space_is_target(&item->group.blend_space));
    EXPECT_EQ(item->layer, (int32_t)c - 3);
  }
  EXPECT_EQ(gshscene_list_group_push_fixed(l.list, GSHSCENE_COMPOSITE_COUNT, 0), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_group_push_fixed(nullptr, 0, 0), GSHSCENE_ERR_INVALID);
}

// --- truncate ----------------------------------------------------------------------------------------------------------

TEST(Truncate, EveryPrefixIsTheListThatRecordedOnlyIt) {
  StoreHolder s;
  Ref sq(intern_square(s.store));
  Ref p2(intern_fixture(s.store));
  // A picture to instance (its expansion and depth count), and a depth group (non-flat: close has an order to make).
  ListHolder inner(s.store);
  ASSERT_EQ(gshscene_list_fill(inner.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(inner.list), GSHSCENE_OK);
  Ref picture;
  ASSERT_EQ(gshscene_store_intern_picture(s.store, inner.list, &picture.resource), GSHSCENE_OK);
  GSHSCENE_Group depth = make_group(1.0, GSHSCENE_ORDER_DEPTH);
  // Each step records one item on a list.
  std::vector<std::function<GSHSCENE_Result(GSHSCENE_List *)>> steps = {
      [&](GSHSCENE_List * l) { return gshscene_list_fill(l, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 1, 2.0); },
      [&](GSHSCENE_List * l) { return gshscene_list_group_push(l, &depth, 0, 0); },
      [&](GSHSCENE_List * l) { return gshscene_list_clip_push(l, p2.get(), nullptr, GSHGEOM_FILL_EVEN_ODD, 0, 1.0); },
      [&](GSHSCENE_List * l) { return gshscene_list_instance(l, picture.get(), nullptr, 0, 3.0); },
      [&](GSHSCENE_List * l) { return gshscene_list_clip_pop(l); },
      [&](GSHSCENE_List * l) { return gshscene_list_fill(l, p2.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 2, 1.0); },
      [&](GSHSCENE_List * l) { return gshscene_list_group_push(l, nullptr, 0, 0); },
      [&](GSHSCENE_List * l) { return gshscene_list_instance(l, picture.get(), nullptr, 0, 0); },
      [&](GSHSCENE_List * l) { return gshscene_list_group_pop(l); },
      [&](GSHSCENE_List * l) { return gshscene_list_group_pop(l); },
  };
  // The rest of the list a truncated one goes on to record, so that the state after the truncation is used.
  auto finish = [&](GSHSCENE_List * l) {
    while (gshscene_list_close(l) == GSHSCENE_ERR_INVALID) {
      if (gshscene_list_group_pop(l) != GSHSCENE_OK) {
        EXPECT_EQ(gshscene_list_clip_pop(l), GSHSCENE_OK);
      }
    }
  };
  for (size_t k = 0; k <= steps.size(); k++) {
    ListHolder full(s.store);
    for (auto & step : steps) {
      ASSERT_EQ(step(full.list), GSHSCENE_OK);
    }
    size_t refs = gshscene_resource_refcount(picture.get());
    ASSERT_EQ(gshscene_list_truncate(full.list, k), GSHSCENE_OK) << k;
    size_t instances_dropped = (k <= 3 ? 1u : 0u) + (k <= 7 ? 1u : 0u);
    EXPECT_EQ(gshscene_resource_refcount(picture.get()), refs - instances_dropped) << k << ": released";
    ListHolder prefix(s.store);
    for (size_t i = 0; i < k; i++) {
      ASSERT_EQ(steps[i](prefix.list), GSHSCENE_OK);
    }
    EXPECT_EQ(gshscene_list_count(full.list), k);
    EXPECT_EQ(gshscene_list_expansion(full.list), gshscene_list_expansion(prefix.list)) << k;
    EXPECT_EQ(gshscene_list_hash(full.list), gshscene_list_hash(prefix.list)) << k;
    // The same item is valid next (a pop where a push is open, and only then), and the two close alike.
    for (auto & l : {full.list, prefix.list}) {
      GSHSCENE_Paint e = entry_paint(0);
      ASSERT_EQ(gshscene_list_fill(l, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &e, 0, 0.5), GSHSCENE_OK);
    }
    EXPECT_EQ(gshscene_list_group_pop(full.list) == GSHSCENE_OK, gshscene_list_group_pop(prefix.list) == GSHSCENE_OK) << k;
    EXPECT_EQ(gshscene_list_clip_pop(full.list) == GSHSCENE_OK, gshscene_list_clip_pop(prefix.list) == GSHSCENE_OK) << k;
    finish(full.list);
    finish(prefix.list);
    EXPECT_TRUE(gshscene_list_equal(full.list, prefix.list)) << k;
    EXPECT_EQ(gshscene_list_tie_count(full.list), gshscene_list_tie_count(prefix.list)) << k;
    EXPECT_EQ(gshscene_list_hash(full.list), gshscene_list_hash(prefix.list)) << k;
  }
}

TEST(Truncate, TheOpenPushesAndTheDeepestPictureAreCountedAgain) {
  StoreHolder s;
  Ref sq(intern_square(s.store));
  // Pictures one and two deep.
  ListHolder inner(s.store);
  ASSERT_EQ(gshscene_list_fill(inner.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(inner.list), GSHSCENE_OK);
  Ref one;
  ASSERT_EQ(gshscene_store_intern_picture(s.store, inner.list, &one.resource), GSHSCENE_OK);
  ListHolder outer(s.store);
  ASSERT_EQ(gshscene_list_instance(outer.list, one.get(), nullptr, 0, 0), GSHSCENE_OK);
  ASSERT_EQ(gshscene_list_close(outer.list), GSHSCENE_OK);
  Ref two;
  ASSERT_EQ(gshscene_store_intern_picture(s.store, outer.list, &two.resource), GSHSCENE_OK);
  // A clip, the deeper picture, a group, the shallower one; truncated, then closed and made a picture: its depth is one
  // more than the deepest picture the kept items instance.
  auto picture_depth_after = [&](size_t keep) -> size_t {
    ListHolder l(s.store);
    EXPECT_EQ(gshscene_list_clip_push(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, 0, 0), GSHSCENE_OK);
    EXPECT_EQ(gshscene_list_instance(l.list, two.get(), nullptr, 0, 0), GSHSCENE_OK);
    EXPECT_EQ(gshscene_list_group_push(l.list, nullptr, 0, 0), GSHSCENE_OK);
    EXPECT_EQ(gshscene_list_instance(l.list, one.get(), nullptr, 0, 0), GSHSCENE_OK);
    EXPECT_EQ(gshscene_list_truncate(l.list, keep), GSHSCENE_OK);
    if (keep >= 3) {
      EXPECT_EQ(gshscene_list_group_pop(l.list), GSHSCENE_OK);
    }
    // One push is open here, and only the clip.
    EXPECT_EQ(gshscene_list_group_pop(l.list), GSHSCENE_ERR_INVALID) << keep;
    EXPECT_EQ(gshscene_list_close(l.list), GSHSCENE_ERR_INVALID) << keep;
    EXPECT_EQ(gshscene_list_clip_pop(l.list), GSHSCENE_OK) << keep;
    EXPECT_EQ(gshscene_list_close(l.list), GSHSCENE_OK) << keep;
    Ref made;
    EXPECT_EQ(gshscene_store_intern_picture(s.store, l.list, &made.resource), GSHSCENE_OK) << keep;
    return gshscene_resource_picture_depth(made.get());
  };
  EXPECT_EQ(picture_depth_after(4), 3u);
  EXPECT_EQ(picture_depth_after(3), 3u) << "the deeper picture kept";
  EXPECT_EQ(picture_depth_after(1), 1u) << "no picture kept";
}

TEST(Truncate, ItsArgumentsAreChecked) {
  StoreHolder s;
  Ref sq(intern_square(s.store));
  ListHolder l(s.store);
  ASSERT_EQ(gshscene_list_fill(l.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_truncate(nullptr, 0), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_list_truncate(l.list, 2), GSHSCENE_ERR_INVALID) << "past the count";
  EXPECT_EQ(gshscene_list_truncate(l.list, 1), GSHSCENE_OK) << "to itself: nothing";
  EXPECT_EQ(gshscene_list_count(l.list), 1u);
  ASSERT_EQ(gshscene_list_close(l.list), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_truncate(l.list, 0), GSHSCENE_ERR_INVALID) << "a closed list";
  EXPECT_EQ(gshscene_list_count(l.list), 1u);
}

// --- the palette of a version (version.h, edit.h, diff.h) -------------------------------------------------------------

namespace {

struct Doc {
  SceneHolder s;
  Ref sq;
  Ref p;
  Ref q;
  Doc() : sq(intern_square(s.store())) {
    p.resource = palette_of(s.store(), {color(1, 0, 0, 1), color(0, 1, 0, 1)});
    q.resource = palette_of(s.store(), {color(0, 0, 1, 1)});
  }
  GSHSCENE_Node shape_at(double x, double y, int entry = -1) {
    GSHSCENE_Node n = shape_node(sq.get());
    n.map = translate(x, y);
    if (entry >= 0) {
      n.paint = entry_paint((uint32_t)entry);
    }
    return n;
  }
  VersionRef current() {
    VersionRef v;
    EXPECT_EQ(gshscene_scene_current(s.scene, &v.version), GSHSCENE_OK);
    return v;
  }
};

GSHSCENE_Damage damage_of(const GSHSCENE_Version * a, const GSHSCENE_Version * b, int * changed, size_t * count) {
  GSHSCENE_Damage d;
  std::memset(&d, 0xAB, sizeof d);
  GSHSCENE_Diff * diff = nullptr;
  EXPECT_EQ(gshscene_diff(a, b, nullptr, nullptr, &diff), GSHSCENE_OK);
  EXPECT_EQ(gshscene_diff_damage(diff, &d), GSHSCENE_OK);
  *changed = gshscene_diff_palette_changed(diff);
  *count = gshscene_diff_count(diff);
  gshscene_diff_destroy(diff);
  return d;
}

}  // namespace

TEST(VersionPalette, AnEditSetsItAndItsVersionAndListCarryIt) {
  Doc d;
  VersionRef v0 = d.current();
  EXPECT_EQ(gshscene_version_palette(v0.get()), nullptr);
  EXPECT_EQ(gshscene_version_palette(nullptr), nullptr);
  EditHolder e(d.s);
  EXPECT_EQ(gshscene_edit_palette(e.edit), nullptr);
  add(e.edit, GSHSCENE_ROOT_ID, d.shape_at(0, 0, 1));
  ASSERT_EQ(gshscene_edit_set_palette(e.edit, d.p.get()), GSHSCENE_OK);
  EXPECT_EQ(gshscene_edit_palette(e.edit), d.p.get());
  EXPECT_EQ(gshscene_edit_palette(nullptr), nullptr);
  // Refused, changing nothing.
  EXPECT_EQ(gshscene_edit_set_palette(e.edit, d.sq.get()), GSHSCENE_ERR_INVALID);
  StoreHolder other;
  Ref elsewhere(palette_of(other.store, {color(1, 0, 0, 1)}));
  EXPECT_EQ(gshscene_edit_set_palette(e.edit, elsewhere.get()), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_edit_set_palette(nullptr, d.p.get()), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_edit_palette(e.edit), d.p.get());
  VersionRef v1;
  publish(d.s, e, &v1);
  EXPECT_EQ(gshscene_version_palette(v1.get()), d.p.get());
  EXPECT_EQ(gshscene_edit_set_palette(e.edit, d.q.get()), GSHSCENE_ERR_INVALID) << "a spent edit";
  const GSHSCENE_List * list = nullptr;
  ASSERT_EQ(gshscene_version_list(v1.get(), &list), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_palette(list), d.p.get()) << "the version's list carries its palette";
  // Only the palette changed: a new version, another hash.
  EditHolder e2(d.s);
  ASSERT_EQ(gshscene_edit_set_palette(e2.edit, d.q.get()), GSHSCENE_OK);
  VersionRef v2;
  publish(d.s, e2, &v2);
  EXPECT_EQ(gshscene_version_serial(v2.get()), gshscene_version_serial(v1.get()) + 1);
  EXPECT_NE(gshscene_version_hash(v2.get()), gshscene_version_hash(v1.get()));
  // Setting what the base has publishes nothing new.
  EditHolder e3(d.s);
  ASSERT_EQ(gshscene_edit_set_palette(e3.edit, d.q.get()), GSHSCENE_OK);
  VersionRef v3;
  publish(d.s, e3, &v3);
  EXPECT_EQ(v3.get(), v2.get());
  // Back to none.
  EditHolder e4(d.s);
  ASSERT_EQ(gshscene_edit_set_palette(e4.edit, nullptr), GSHSCENE_OK);
  EXPECT_EQ(gshscene_edit_palette(e4.edit), nullptr);
  VersionRef v4;
  publish(d.s, e4, &v4);
  EXPECT_EQ(gshscene_version_palette(v4.get()), nullptr);
  EXPECT_NE(gshscene_version_hash(v4.get()), gshscene_version_hash(v2.get()));
  // A destroyed edit that held one releases it.
  size_t refs = gshscene_resource_refcount(d.p.get());
  {
    EditHolder held(d.s);
    ASSERT_EQ(gshscene_edit_set_palette(held.edit, d.p.get()), GSHSCENE_OK);
    ASSERT_EQ(gshscene_edit_set_palette(held.edit, d.p.get()), GSHSCENE_OK);
    EXPECT_EQ(gshscene_resource_refcount(d.p.get()), refs + 1);
  }
  EXPECT_EQ(gshscene_resource_refcount(d.p.get()), refs);
}

TEST(VersionPalette, AVersionHoldsItsPaletteAndASpentEditDoesNot) {
  Doc d;
  const size_t refs = gshscene_resource_refcount(d.p.get());
  EditHolder e(d.s);
  add(e.edit, GSHSCENE_ROOT_ID, d.shape_at(0, 0, 1));
  ASSERT_EQ(gshscene_edit_set_palette(e.edit, d.p.get()), GSHSCENE_OK);
  VersionRef v1;
  publish(d.s, e, &v1);
  // The edit is spent but alive: what holds the palette now is the version (and its list).
  {
    EditHolder later(d.s);
    EXPECT_EQ(gshscene_edit_palette(later.edit), d.p.get()) << "an edit that sets none sees its base's";
    ASSERT_EQ(gshscene_edit_set_palette(later.edit, d.q.get()), GSHSCENE_OK);
    publish(d.s, later);
    EXPECT_EQ(gshscene_edit_palette(later.edit), d.p.get()) << "a spent edit sets nothing: it sees its base's again";
  }
  EXPECT_GT(gshscene_resource_refcount(d.p.get()), refs) << "the version still held is made with its own reference";
  v1 = VersionRef();
  EXPECT_EQ(gshscene_resource_refcount(d.p.get()), refs)
      << "once no version holds it, neither the spent edit nor the released version does";
}

TEST(VersionPalette, AChangedEntryIsAChangedNode) {
  Doc d;
  VersionRef v1;
  uint64_t id = 0;
  {
    EditHolder e(d.s);
    id = add(e.edit, GSHSCENE_ROOT_ID, d.shape_at(0, 0, 0));
    ASSERT_EQ(gshscene_edit_set_palette(e.edit, d.p.get()), GSHSCENE_OK);
    publish(d.s, e, &v1);
  }
  VersionRef v2;
  {
    EditHolder e(d.s);
    GSHSCENE_Node changed = d.shape_at(0, 0, 1);
    ASSERT_EQ(gshscene_edit_set(e.edit, id, &changed), GSHSCENE_OK);
    publish(d.s, e, &v2);
  }
  EXPECT_NE(v2.get(), v1.get()) << "only the entry changed: a new version";
  EXPECT_NE(gshscene_version_hash(v2.get()), gshscene_version_hash(v1.get()));
  int changed = -1;
  size_t count = 0;
  GSHSCENE_Damage r = damage_of(v1.get(), v2.get(), &changed, &count);
  EXPECT_EQ(changed, 0);
  EXPECT_EQ(count, 1u) << "the node differs";
  EXPECT_EQ(r.empty, 0);
}

TEST(VersionPalette, TwoEditsThatSetItConflictAtTheRootAndAStaleOneToo) {
  Doc d;
  VersionRef base = d.current();
  {
    EditHolder a(d.s, base.get()), b(d.s, base.get());
    ASSERT_EQ(gshscene_edit_set_palette(a.edit, d.p.get()), GSHSCENE_OK);
    ASSERT_EQ(gshscene_edit_set_palette(b.edit, d.q.get()), GSHSCENE_OK);
    GSHSCENE_Edit * both[2] = {a.edit, b.edit};
    ProblemsRef problems;
    const GSHSCENE_Version * out = nullptr;
    EXPECT_EQ(gshscene_scene_merge(d.s.scene, both, 2, &problems.problems, &out), GSHSCENE_ERR_REFUSED);
    EXPECT_EQ(problems.ids(GSHSCENE_PROBLEM_PARAMETER), (std::vector<uint64_t>{GSHSCENE_ROOT_ID}));
  }
  {
    // One sets the palette, the other adds a shape: they merge, in either order, to one hash.
    uint64_t hashes[2];
    for (int order = 0; order < 2; order++) {
      Doc e;
      VersionRef b0 = e.current();
      EditHolder x(e.s, b0.get()), y(e.s, b0.get());
      ASSERT_EQ(gshscene_edit_set_palette(x.edit, e.p.get()), GSHSCENE_OK);
      add(y.edit, GSHSCENE_ROOT_ID, e.shape_at(0, 0, 0));
      GSHSCENE_Edit * edits[2] = {order ? y.edit : x.edit, order ? x.edit : y.edit};
      const GSHSCENE_Version * out = nullptr;
      ASSERT_EQ(gshscene_scene_merge(e.s.scene, edits, 2, nullptr, &out), GSHSCENE_OK);
      VersionRef v(out);
      EXPECT_EQ(gshscene_version_palette(v.get()), e.p.get());
      hashes[order] = gshscene_version_hash(v.get());
    }
    EXPECT_EQ(hashes[0], hashes[1]);
  }
  // A publish since the base set it: an edit of the old base that sets it is refused; one that adds is not.
  EditHolder late(d.s, base.get()), later(d.s, base.get()), same(d.s, base.get());
  {
    EditHolder first(d.s, base.get());
    ASSERT_EQ(gshscene_edit_set_palette(first.edit, d.p.get()), GSHSCENE_OK);
    publish(d.s, first);
  }
  ASSERT_EQ(gshscene_edit_set_palette(late.edit, d.q.get()), GSHSCENE_OK);
  ProblemsRef problems;
  EXPECT_EQ(gshscene_scene_publish(d.s.scene, late.edit, &problems.problems, nullptr), GSHSCENE_ERR_REFUSED);
  EXPECT_EQ(problems.ids(GSHSCENE_PROBLEM_PARAMETER), (std::vector<uint64_t>{GSHSCENE_ROOT_ID}));
  add(later.edit, GSHSCENE_ROOT_ID, d.shape_at(5, 5, 1));
  const GSHSCENE_Version * out = nullptr;
  ASSERT_EQ(gshscene_scene_publish(d.s.scene, later.edit, nullptr, &out), GSHSCENE_OK);
  VersionRef v(out);
  EXPECT_EQ(gshscene_version_palette(v.get()), d.p.get()) << "the palette of the publish since the base is kept";
  // Setting what its own base had (none) changes nothing, so it conflicts with nothing: the publish is empty.
  ASSERT_EQ(gshscene_edit_set_palette(same.edit, nullptr), GSHSCENE_OK);
  out = nullptr;
  ASSERT_EQ(gshscene_scene_publish(d.s.scene, same.edit, nullptr, &out), GSHSCENE_OK);
  VersionRef w(out);
  EXPECT_EQ(gshscene_version_palette(w.get()), d.p.get());
}

TEST(VersionPalette, AChangedPaletteDamagesExactlyTheShapesThatPaintFromIt) {
  Doc d;
  VersionRef v1;
  {
    EditHolder e(d.s);
    add(e.edit, GSHSCENE_ROOT_ID, d.shape_at(0, 0));         // solid: not damaged
    add(e.edit, GSHSCENE_ROOT_ID, d.shape_at(20, 0, 0));     // palette
    add(e.edit, GSHSCENE_ROOT_ID, d.shape_at(40, 10, 5));    // palette, a missing entry: damaged all the same
    add(e.edit, GSHSCENE_ROOT_ID, d.shape_at(100, 100));     // solid
    ASSERT_EQ(gshscene_edit_set_palette(e.edit, d.p.get()), GSHSCENE_OK);
    publish(d.s, e, &v1);
  }
  VersionRef v2;
  {
    EditHolder e(d.s);
    ASSERT_EQ(gshscene_edit_set_palette(e.edit, d.q.get()), GSHSCENE_OK);
    publish(d.s, e, &v2);
  }
  int changed = -1;
  size_t count = 99;
  GSHSCENE_Damage r = damage_of(v1.get(), v2.get(), &changed, &count);
  EXPECT_EQ(changed, 1);
  EXPECT_EQ(count, 0u) << "no node differs";
  EXPECT_EQ(r.unbounded, 0);
  EXPECT_EQ(r.empty, 0);
  EXPECT_EQ(r.rect.x0, 19);
  EXPECT_EQ(r.rect.y0, -1);
  EXPECT_EQ(r.rect.x1, 45);
  EXPECT_EQ(r.rect.y1, 15);
  GSHSCENE_Damage same = damage_of(v2.get(), v2.get(), &changed, &count);
  EXPECT_EQ(changed, 0);
  EXPECT_EQ(same.empty, 1);
  // A foreground change does not damage the palette shapes, nor a palette change the foreground's.
  VersionRef v3;
  {
    EditHolder e(d.s);
    GSHSCENE_Color red = color(1, 0, 0, 1);
    ASSERT_EQ(gshscene_edit_set_foreground(e.edit, &red), GSHSCENE_OK);
    publish(d.s, e, &v3);
  }
  GSHSCENE_Damage fg = damage_of(v2.get(), v3.get(), &changed, &count);
  EXPECT_EQ(changed, 0);
  EXPECT_EQ(fg.empty, 1);
  EXPECT_EQ(gshscene_diff_palette_changed(nullptr), 0);
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
