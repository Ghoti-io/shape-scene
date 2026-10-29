/**
 * @file
 *
 * Runs from a font (story 14a; glyph_provider.h, glyph.h): the face resource, its binding and its lifetime; the
 * snapped scale; the route, rule by rule; the origin; the two routes' maps; AD-5's gate at the waist (the fast tier
 * with hinting off is the general route under the snapped map, path for path, over ppem 8 to 72, two upems, the
 * fractions and the instances of the test provider); the pinned ties; what each provider answer makes of a glyph;
 * the integer run constructor; and the damage of a run from a bound face.
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
#include <vector>

using namespace scene;

namespace {

/** A test provider and a store; faces and runs held until the end. */
struct Glyphs {
  GSHSCENE_Test_Provider tp{};
  StoreHolder s;
  std::vector<const GSHSCENE_Resource *> held;
  explicit Glyphs(uint32_t upem = 2048, uint32_t flags = GSHSCENE_FACE_HINTING) {
    tp.upem = upem;
    tp.flags = flags;
  }
  ~Glyphs() {
    for (const GSHSCENE_Resource * r : held) {
      gshscene_resource_release(r);
    }
  }
  /** A face; its key (0: 0x51 with the fast tier, 0x151 without) keeps a face of each kind its own content. */
  const GSHSCENE_Resource * face(int fast = 1, int16_t coord = 0, uint64_t key = 0) {
    const GSHSCENE_Resource * f = gshscene_tp_face(s.store, &tp, fast, key ? key : (fast ? 0x51u : 0x151u), coord);
    held.push_back(f);
    return f;
  }
  const GSHSCENE_Resource * run(const GSHSCENE_Resource * f, std::vector<GSHSCENE_Glyph> glyphs, double size,
      uint32_t mode = GSHSCENE_HINT_NONE, uint32_t version = 0) {
    const GSHSCENE_Resource * r = intern_font_run(s.store, f, size, glyphs, mode, version);
    held.push_back(r);
    return r;
  }
};

GSHGEOM_Map ident() {
  GSHGEOM_Map m;
  gshgeom_map_identity(2, &m);
  return m;
}

GSHGEOM_Map rotation(double degrees) {
  GSHGEOM_Map m;
  gshgeom_map_rotate(GSHGEOM_ANGLE_DEGREES, degrees, &m);
  return m;
}

GSHGEOM_Map compose(const GSHGEOM_Map & outer, const GSHGEOM_Map & inner) {
  GSHGEOM_Map m;
  EXPECT_EQ(gshgeom_map_compose(&outer, &inner, &m), GSHGEOM_OK);
  return m;
}

GSHSCENE_Glyph_Route route_of(const GSHSCENE_Resource * run, const GSHGEOM_Map & map,
    uint32_t kind = GSHSCENE_TARGET_RASTER) {
  GSHSCENE_Glyph_Route r;
  std::memset(&r, 0xAB, sizeof r);
  EXPECT_EQ(gshscene_glyph_route(run, &map, kind, &r), GSHSCENE_OK);
  return r;
}

void expect_route(const GSHSCENE_Glyph_Route & r, uint32_t route, uint32_t entry_class, uint32_t reason,
    const char * what) {
  EXPECT_EQ(r.route, route) << what;
  EXPECT_EQ(r.entry_class, entry_class) << what;
  EXPECT_EQ(r.reason, reason) << what;
}

/** The elements of a device path, flattened to numbers, for comparison. */
std::vector<int64_t> elements(const GSHGEOM_Device_Path * p) {
  std::vector<int64_t> out;
  for (size_t i = 0; i < gshgeom_device_path_element_count(p); i++) {
    GSHGEOM_Device_Element e;
    EXPECT_EQ(gshgeom_device_path_element(p, i, &e), GSHGEOM_OK);
    out.push_back(e.verb);
    for (unsigned k = 0; k < e.count; k++) {
      out.push_back(e.p[k].x);
      out.push_back(e.p[k].y);
    }
  }
  return out;
}

}  // namespace

// --- the face ------------------------------------------------------------------------------------------------

TEST(Face, TheKindsAndConstantsArePinned) {
  EXPECT_EQ((int)GSHSCENE_HINT_FONT, 0);
  EXPECT_EQ((int)GSHSCENE_HINT_NONE, 1);
  EXPECT_EQ((int)GSHSCENE_HINT_FONT_STRICT, 2);
  EXPECT_EQ((int)GSHSCENE_HINT_MODE_COUNT, 3);
  EXPECT_EQ((int)GSHSCENE_HINT_STATUS_HINTED, 0);
  EXPECT_EQ((int)GSHSCENE_HINT_STATUS_NO_PROGRAM, 1);
  EXPECT_EQ((int)GSHSCENE_HINT_STATUS_PROGRAM_FAILED, 2);
  EXPECT_EQ((int)GSHSCENE_HINT_STATUS_WITH_ERRORS, 3);
  EXPECT_EQ(GSHSCENE_GLYPH_MAX_PPEM, 4096u);
  EXPECT_EQ(GSHSCENE_FACE_UPEM_MIN, 16u);
  EXPECT_EQ(GSHSCENE_FACE_UPEM_MAX, 16384u);
  EXPECT_EQ(GSHSCENE_FACE_MAX_AXES, 64u);
  EXPECT_EQ(GSHSCENE_F2DOT14_ONE, 16384);
  EXPECT_EQ(GSHSCENE_GLYPH_OUTLINE_BITS, 6u);
  EXPECT_EQ(GSHSCENE_GLYPH_PROVIDER_VERSION, 1u);
  EXPECT_EQ(GSHSCENE_FACE_HINTING, 1u);
  EXPECT_EQ(GSHSCENE_FACE_COLOR, 2u);
  EXPECT_EQ((int)GSHSCENE_GLYPH_GENERAL, 0);
  EXPECT_EQ((int)GSHSCENE_GLYPH_FAST, 1);
  EXPECT_EQ((int)GSHSCENE_REASON_HINT_ROUTE, 12);
  EXPECT_EQ((int)GSHSCENE_REASON_HINT_SIZE, 13);
  EXPECT_EQ((int)GSHSCENE_REASON_HINT_NO_PROGRAM, 14);
  EXPECT_EQ((int)GSHSCENE_REASON_HINT_PROGRAM_FAILED, 15);
  EXPECT_EQ((int)GSHSCENE_REASON_HINT_WITH_ERRORS, 16);
  EXPECT_EQ((int)GSHSCENE_REASON_HINT_STRICT_ROUTE, 17);
  EXPECT_EQ((int)GSHSCENE_REASON_GLYPH_UNAVAILABLE, 18);
  EXPECT_EQ((int)GSHSCENE_REASON_FACE_UNBOUND, 19);
  EXPECT_EQ((int)GSHSCENE_REASON_PALETTE_ENTRY, 20);
  EXPECT_EQ((int)GSHSCENE_REASON_COUNT, 36)
      << "S19 (color-paint story P3) added three refusals of PDF's group fields, SK11 and SK8 (strokes story S1) two, "
         "S20's refusal of a stroked run (strokes story S4) one, the placeholder (color-paint story P5, PH4) one, and S21's image "
         "refusal and two approximations (masks-images-patterns story I2) three, and M1's refusal of a mask feature one";
  EXPECT_STREQ(gshscene_reason_string(GSHSCENE_REASON_HINT_ROUTE), "hint-route");
  EXPECT_STREQ(gshscene_reason_string(GSHSCENE_REASON_HINT_SIZE), "hint-size");
  EXPECT_STREQ(gshscene_reason_string(GSHSCENE_REASON_HINT_NO_PROGRAM), "hint-no-program");
  EXPECT_STREQ(gshscene_reason_string(GSHSCENE_REASON_HINT_PROGRAM_FAILED), "hint-program-failed");
  EXPECT_STREQ(gshscene_reason_string(GSHSCENE_REASON_HINT_WITH_ERRORS), "hint-with-errors");
  EXPECT_STREQ(gshscene_reason_string(GSHSCENE_REASON_HINT_STRICT_ROUTE), "hint-strict-route");
  EXPECT_STREQ(gshscene_reason_string(GSHSCENE_REASON_GLYPH_UNAVAILABLE), "glyph-unavailable");
  EXPECT_STREQ(gshscene_reason_string(GSHSCENE_REASON_FACE_UNBOUND), "face-unbound");
}

TEST(Face, EqualContentIsOneHandleAndTrailingZerosAreTheDefault) {
  Glyphs g;
  int16_t a[3] = {100, 0, 0};
  int16_t b[1] = {100};
  GSHSCENE_Face_Desc d{};
  d.font_key = 7;
  d.coord_count = 3;
  d.coords = a;
  Ref fa;
  Ref fb;
  ASSERT_EQ(gshscene_store_intern_face(g.s.store, &d, &fa.resource), GSHSCENE_OK);
  d.coord_count = 1;
  d.coords = b;
  ASSERT_EQ(gshscene_store_intern_face(g.s.store, &d, &fb.resource), GSHSCENE_OK);
  EXPECT_EQ(fa.get(), fb.get()) << "[100, 0, 0] and [100] are one instance";
  GSHSCENE_Face view;
  ASSERT_EQ(gshscene_resource_face(fa.get(), &view), GSHSCENE_OK);
  EXPECT_EQ(view.font_key, 7u);
  EXPECT_EQ(view.coord_count, 1u);
  EXPECT_EQ(view.coords[0], 100);
  // The default instance: no coordinates and all zeros are one face.
  int16_t zeros[2] = {0, 0};
  d.coord_count = 2;
  d.coords = zeros;
  Ref z1;
  Ref z2;
  ASSERT_EQ(gshscene_store_intern_face(g.s.store, &d, &z1.resource), GSHSCENE_OK);
  d.coord_count = 0;
  d.coords = nullptr;
  ASSERT_EQ(gshscene_store_intern_face(g.s.store, &d, &z2.resource), GSHSCENE_OK);
  EXPECT_EQ(z1.get(), z2.get());
  ASSERT_EQ(gshscene_resource_face(z1.get(), &view), GSHSCENE_OK);
  EXPECT_EQ(view.coord_count, 0u);
  EXPECT_EQ(view.coords, nullptr);
  // Each field of the content separates.
  int16_t c[1] = {101};
  d.coord_count = 1;
  d.coords = c;
  Ref other_coord;
  ASSERT_EQ(gshscene_store_intern_face(g.s.store, &d, &other_coord.resource), GSHSCENE_OK);
  EXPECT_NE(other_coord.get(), fa.get());
  d.coords = b;
  d.face_index = 1;
  Ref other_index;
  ASSERT_EQ(gshscene_store_intern_face(g.s.store, &d, &other_index.resource), GSHSCENE_OK);
  EXPECT_NE(other_index.get(), fa.get());
  d.face_index = 0;
  d.font_key = 8;
  Ref other_key;
  ASSERT_EQ(gshscene_store_intern_face(g.s.store, &d, &other_key.resource), GSHSCENE_OK);
  EXPECT_NE(other_key.get(), fa.get());
  EXPECT_EQ(gshscene_resource_kind(fa.get()), GSHSCENE_RESOURCE_FACE);
  EXPECT_EQ(gshscene_resource_bytes(fa.get()), 128u + 8u);
}

TEST(Face, TheCountOfCoordinatesIsContentEvenWhereTheHashesCollide) {
  // Every content in one bucket: equality alone tells [100] from [100, 5], in either order.
  for (int order = 0; order < 2; order++) {
    StoreHolder s(nullptr, nullptr, all_zero);
    int16_t one[1] = {100};
    int16_t two[2] = {100, 5};
    GSHSCENE_Face_Desc d{};
    d.font_key = 7;
    Ref a;
    Ref b;
    d.coord_count = order ? 2 : 1;
    d.coords = order ? two : one;
    ASSERT_EQ(gshscene_store_intern_face(s.store, &d, &a.resource), GSHSCENE_OK);
    d.coord_count = order ? 1 : 2;
    d.coords = order ? one : two;
    ASSERT_EQ(gshscene_store_intern_face(s.store, &d, &b.resource), GSHSCENE_OK);
    EXPECT_NE(a.get(), b.get()) << order;
    int16_t other[2] = {100, 6};
    d.coords = other;
    d.coord_count = 2;
    Ref c;
    ASSERT_EQ(gshscene_store_intern_face(s.store, &d, &c.resource), GSHSCENE_OK);
    EXPECT_NE(c.get(), a.get());
    EXPECT_NE(c.get(), b.get());
  }
}

TEST(Face, EveryBadDescriptionIsRefusedAndBuildsNothing) {
  Glyphs g;
  GSHSCENE_Store_Stats before;
  ASSERT_EQ(gshscene_store_stats(g.s.store, &before), GSHSCENE_OK);
  const GSHSCENE_Resource * r = nullptr;
  GSHSCENE_Face_Desc d{};
  d.font_key = 1;
  EXPECT_EQ(gshscene_store_intern_face(nullptr, &d, &r), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_store_intern_face(g.s.store, nullptr, &r), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_store_intern_face(g.s.store, &d, nullptr), GSHSCENE_ERR_INVALID);
  d.font_key = 0;
  EXPECT_EQ(gshscene_store_intern_face(g.s.store, &d, &r), GSHSCENE_ERR_INVALID) << "a zero key";
  d.font_key = 1;
  d.coord_count = 1;
  EXPECT_EQ(gshscene_store_intern_face(g.s.store, &d, &r), GSHSCENE_ERR_INVALID) << "a count with no array";
  int16_t over[1] = {16385};
  int16_t under[1] = {-16385};
  int16_t edge[2] = {16384, -16384};
  d.coords = over;
  EXPECT_EQ(gshscene_store_intern_face(g.s.store, &d, &r), GSHSCENE_ERR_INVALID) << "above 1.0";
  d.coords = under;
  EXPECT_EQ(gshscene_store_intern_face(g.s.store, &d, &r), GSHSCENE_ERR_INVALID) << "below -1.0";
  std::vector<int16_t> many(GSHSCENE_FACE_MAX_AXES + 1, 1);
  d.coords = many.data();
  d.coord_count = GSHSCENE_FACE_MAX_AXES + 1;
  EXPECT_EQ(gshscene_store_intern_face(g.s.store, &d, &r), GSHSCENE_ERR_LIMIT);
  // An ops table missing a required op, or of another version.
  d.coord_count = 0;
  d.coords = nullptr;
  for (int which = 0; which < 4; which++) {
    GSHSCENE_Glyph_Provider ops = *gshscene_tp_ops(1);
    if (which == 0) {
      ops.face_info = nullptr;
    } else if (which == 1) {
      ops.glyph_outline = nullptr;
    } else if (which == 2) {
      ops.glyph_box = nullptr;
    } else {
      ops.version = 2;
    }
    d.provider = &ops;
    d.ctx = &g.tp;
    EXPECT_EQ(gshscene_store_intern_face(g.s.store, &d, &r), GSHSCENE_ERR_INVALID) << which;
  }
  // face_info's failure fails the intern; information past its bounds is invalid.
  d.provider = gshscene_tp_ops(1);
  g.tp.fail_info = 1;
  EXPECT_EQ(gshscene_store_intern_face(g.s.store, &d, &r), GSHSCENE_ERR_CORRUPT);
  g.tp.fail_info = 0;
  for (uint32_t upem : {15u, 16385u}) {
    g.tp.upem = upem;
    EXPECT_EQ(gshscene_store_intern_face(g.s.store, &d, &r), GSHSCENE_ERR_INVALID) << upem;
  }
  g.tp.upem = 2048;
  g.tp.max_ppem = GSHSCENE_GLYPH_MAX_PPEM + 1;
  EXPECT_EQ(gshscene_store_intern_face(g.s.store, &d, &r), GSHSCENE_ERR_INVALID) << "max ppem";
  g.tp.max_ppem = 0;
  g.tp.flags = 4;
  EXPECT_EQ(gshscene_store_intern_face(g.s.store, &d, &r), GSHSCENE_ERR_INVALID) << "an undefined flag";
  g.tp.flags = GSHSCENE_FACE_HINTING;
  EXPECT_EQ(r, nullptr);
  EXPECT_EQ(gcu_atomic_size_load(&g.tp.retains), 0u) << "a refused face retains nothing";
  GSHSCENE_Store_Stats after;
  ASSERT_EQ(gshscene_store_stats(g.s.store, &after), GSHSCENE_OK);
  EXPECT_EQ(after.live, before.live);
  // The edges of every bound are kept.
  d.provider = nullptr;
  d.coords = edge;
  d.coord_count = 2;
  Ref ok;
  EXPECT_EQ(gshscene_store_intern_face(g.s.store, &d, &ok.resource), GSHSCENE_OK);
  d.coords = many.data();
  d.coord_count = GSHSCENE_FACE_MAX_AXES;
  Ref ok64;
  EXPECT_EQ(gshscene_store_intern_face(g.s.store, &d, &ok64.resource), GSHSCENE_OK);
  d.coord_count = 0;
  d.coords = nullptr;
  d.provider = gshscene_tp_ops(1);
  for (uint32_t upem : {16u, 16384u}) {
    g.tp.upem = upem;
    d.font_key = 100 + upem;
    Ref bound;
    EXPECT_EQ(gshscene_store_intern_face(g.s.store, &d, &bound.resource), GSHSCENE_OK) << upem;
  }
}

TEST(Face, TheBindingIsRetainedOnceAndReleasedOnceAtTheLastRelease) {
  GSHSCENE_Test_Provider tp{};
  tp.upem = 1000;
  {
    StoreHolder s;
    const GSHSCENE_Resource * a = gshscene_tp_face(s.store, &tp, 1, 9, 0);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(gcu_atomic_size_load(&tp.retains), 1u);
    EXPECT_EQ(gcu_atomic_size_load(&tp.info_calls), 1u);
    const GSHSCENE_Resource * b = gshscene_tp_face(s.store, &tp, 1, 9, 0);
    EXPECT_EQ(a, b) << "an intern of equal content is a hit";
    EXPECT_EQ(gcu_atomic_size_load(&tp.retains), 1u) << "and its binding is not retained again";
    EXPECT_EQ(gcu_atomic_size_load(&tp.info_calls), 1u) << "nor asked again";
    // A run holds the face.
    const GSHSCENE_Resource * run = intern_font_run(s.store, a, 10.0, {{1, 0, 0, nullptr}});
    ASSERT_NE(run, nullptr);
    gshscene_resource_release(a);
    gshscene_resource_release(b);
    EXPECT_EQ(gcu_atomic_size_load(&tp.releases), 0u) << "the run still holds the face";
    gshscene_resource_release(run);
    EXPECT_EQ(gcu_atomic_size_load(&tp.releases), 1u);
  }
  EXPECT_EQ(gcu_atomic_size_load(&tp.releases), 1u);
  // The accessors.
  StoreHolder s;
  Ref bound(gshscene_tp_face(s.store, &tp, 0, 10, 0));
  GSHSCENE_Face_Info info;
  ASSERT_EQ(gshscene_face_info(bound.get(), &info), GSHSCENE_OK);
  EXPECT_EQ(info.upem, 1000u);
  EXPECT_STREQ(info.family_name, "Ghoti Test");
  EXPECT_STREQ(info.postscript_name, "GhotiTest-Regular");
  const GSHSCENE_Glyph_Provider * ops = nullptr;
  void * ctx = nullptr;
  ASSERT_EQ(gshscene_face_binding(bound.get(), &ops, &ctx), GSHSCENE_OK);
  EXPECT_EQ(ctx, &tp);
  EXPECT_EQ(ops->glyph_device_path, nullptr) << "bound without the fast tier";
  EXPECT_NE(dump_resource(bound.get()).find("\nbound=1 upem=1000 flags=0\n"), std::string::npos) << dump_resource(bound.get());
  tp.flags = GSHSCENE_FACE_HINTING;
  Ref hinting(gshscene_tp_face(s.store, &tp, 1, 11, 0));
  EXPECT_NE(dump_resource(hinting.get()).find("\nbound=1 upem=1000 flags=1\n"), std::string::npos);
  tp.flags = 0;
  Ref unbound(intern_face(s.store));
  EXPECT_EQ(gshscene_face_info(unbound.get(), &info), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_face_binding(unbound.get(), &ops, &ctx), GSHSCENE_ERR_INVALID);
  Ref path(intern_square(s.store));
  GSHSCENE_Face view;
  EXPECT_EQ(gshscene_resource_face(path.get(), &view), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_resource_face(nullptr, &view), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_resource_face(bound.get(), nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_face_info(path.get(), &info), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_face_info(bound.get(), nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_face_binding(path.get(), &ops, &ctx), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_face_binding(bound.get(), nullptr, &ctx), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_face_binding(bound.get(), &ops, nullptr), GSHSCENE_ERR_INVALID);
  // A face is not drawable: a list refuses it.
  ListHolder l(s.store);
  EXPECT_EQ(gshscene_list_fill(l.list, bound.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 0, 0), GSHSCENE_ERR_INVALID);
}

TEST(Face, AnAllocationFailureAtEveryCallLeavesNothingAndNoBinding) {
  for (long k = 0;; k++) {
    tracker::Tracker t;
    GSHSCENE_Allocator a = tracker::allocator_for(&t);
    GSHSCENE_Test_Provider tp{};
    GSHSCENE_Store * store = nullptr;
    ASSERT_EQ(gshscene_store_create(&a, nullptr, &store), GSHSCENE_OK);
    int16_t coords[2] = {5, 6};
    GSHSCENE_Face_Desc d{};
    d.font_key = 3;
    d.coord_count = 2;
    d.coords = coords;
    d.provider = gshscene_tp_ops(1);
    d.ctx = &tp;
    t.fail_from = t.calls + k;
    const GSHSCENE_Resource * r = nullptr;
    GSHSCENE_Result rc = gshscene_store_intern_face(store, &d, &r);
    t.fail_from = -1;
    if (rc == GSHSCENE_OK) {
      EXPECT_EQ(gcu_atomic_size_load(&tp.retains), 1u);
      gshscene_resource_release(r);
      EXPECT_EQ(gcu_atomic_size_load(&tp.releases), 1u);
      gshscene_store_destroy(store);
      EXPECT_TRUE(t.live.empty());
      EXPECT_GT(k, 0) << "the first call can fail";
      break;
    }
    EXPECT_EQ(rc, GSHSCENE_ERR_OOM) << k;
    EXPECT_EQ(gcu_atomic_size_load(&tp.retains), gcu_atomic_size_load(&tp.releases)) << k;
    gshscene_store_destroy(store);
    EXPECT_TRUE(t.live.empty()) << k;
  }
}

// --- the scale and the integer run ----------------------------------------------------------------------------

TEST(GlyphScale, IsRoundOfPpemTimes65536OverUpem) {
  int32_t s = 0;
  ASSERT_EQ(gshscene_glyph_scale(16, 2048, &s), GSHSCENE_OK);
  EXPECT_EQ(s, 512);
  ASSERT_EQ(gshscene_glyph_scale(16, 1000, &s), GSHSCENE_OK);
  EXPECT_EQ(s, 1049) << "1048.576 rounds up";
  ASSERT_EQ(gshscene_glyph_scale(13, 1000, &s), GSHSCENE_OK);
  EXPECT_EQ(s, 852) << "851.968 rounds up";
  ASSERT_EQ(gshscene_glyph_scale(7, 1000, &s), GSHSCENE_OK);
  EXPECT_EQ(s, 459) << "458.752 rounds up";
  ASSERT_EQ(gshscene_glyph_scale(1, 3000, &s), GSHSCENE_OK);
  EXPECT_EQ(s, 22) << "21.845 rounds up";
  ASSERT_EQ(gshscene_glyph_scale(1, 16384, &s), GSHSCENE_OK);
  EXPECT_EQ(s, 4);
  ASSERT_EQ(gshscene_glyph_scale(4096, 16, &s), GSHSCENE_OK);
  EXPECT_EQ(s, 1 << 24);
  // Against a reference in the reals, everywhere: never more than half a unit off, and below a half rounds down.
  for (uint32_t upem = 16; upem <= 16384; upem += 37) {
    for (uint32_t ppem : {1u, 2u, 9u, 12u, 13u, 16u, 72u, 1000u, 4096u}) {
      ASSERT_EQ(gshscene_glyph_scale(ppem, upem, &s), GSHSCENE_OK);
      int64_t num = (int64_t)ppem * 65536;
      int64_t q = num / upem;
      int64_t rem = num % upem;
      int64_t want = 2 * rem >= (int64_t)upem ? q + 1 : q;
      ASSERT_EQ(s, want) << ppem << "/" << upem;
    }
  }
  EXPECT_EQ(gshscene_glyph_scale(0, 2048, &s), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_glyph_scale(4097, 2048, &s), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_glyph_scale(16, 15, &s), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_glyph_scale(16, 16385, &s), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_glyph_scale(16, 2048, nullptr), GSHSCENE_ERR_INVALID);
}

TEST(GlyphRunFixed, IsTheDoubleRunConvertedExactly) {
  Glyphs g;
  const GSHSCENE_Resource * f = g.face();
  GSHSCENE_Glyph_Fixed fixed[3] = {{1, 64 * 3 + 16, -64 * 20 - 1}, {2, 0, 0}, {7, INT32_MAX, INT32_MIN}};
  Ref a;
  ASSERT_EQ(gshscene_store_intern_glyph_run_fixed(g.s.store, f, 16 * 64 + 32, GSHSCENE_HINT_FONT_STRICT, 40, 6, 3, fixed,
                &a.resource),
      GSHSCENE_OK);
  Ref b(intern_font_run(g.s.store, f, 16.5,
      {{1, 3.25, -20.015625, nullptr}, {2, 0.0, 0.0, nullptr}, {7, 2147483647.0 / 64.0, -2147483648.0 / 64.0, nullptr}},
      GSHSCENE_HINT_FONT_STRICT, 40));
  EXPECT_EQ(a.get(), b.get()) << "the same content: one handle";
  Ref c;
  ASSERT_EQ(gshscene_store_intern_glyph_run_fixed(g.s.store, f, 16, 0, 0, 0, 1, fixed + 1, &c.resource), GSHSCENE_OK);
  Ref d(intern_font_run(g.s.store, f, 16.0, {{2, 0.0, 0.0, nullptr}}));
  GSHSCENE_Glyph_Run run;
  ASSERT_EQ(gshscene_resource_glyph_run(c.get(), &run), GSHSCENE_OK);
  EXPECT_EQ(run.size, 16.0);
  EXPECT_EQ(run.hint_mode, 0u);
  Ref e;
  ASSERT_EQ(gshscene_store_intern_glyph_run_fixed(g.s.store, f, 1, 0, 0, 16, 0, nullptr, &e.resource), GSHSCENE_OK);
  ASSERT_EQ(gshscene_resource_glyph_run(e.get(), &run), GSHSCENE_OK);
  EXPECT_EQ(run.size, 1.0 / 65536.0) << "16 fraction bits";
  const GSHSCENE_Resource * r = nullptr;
  EXPECT_EQ(gshscene_store_intern_glyph_run_fixed(g.s.store, f, 16, 0, 0, 17, 1, fixed, &r), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_store_intern_glyph_run_fixed(g.s.store, f, 0, 0, 0, 6, 1, fixed, &r), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_store_intern_glyph_run_fixed(g.s.store, f, -64, 0, 0, 6, 1, fixed, &r), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_store_intern_glyph_run_fixed(g.s.store, f, 64, 0, 0, 6, 1, nullptr, &r), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_store_intern_glyph_run_fixed(g.s.store, f, 64, 3, 0, 6, 1, fixed, &r), GSHSCENE_ERR_INVALID)
      << "a mode outside the enum";
  EXPECT_EQ(gshscene_store_intern_glyph_run_fixed(nullptr, f, 64, 0, 0, 6, 1, fixed, &r), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_store_intern_glyph_run_fixed(g.s.store, f, 64, 0, 0, 6, 1, fixed, nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_store_intern_glyph_run_fixed(g.s.store, nullptr, 64, 0, 0, 6, 1, fixed, &r), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(r, nullptr);
}

// --- the route -------------------------------------------------------------------------------------------------

TEST(GlyphRoute, EachRuleInItsOrder) {
  Glyphs g;
  std::vector<GSHSCENE_Glyph> one = {{1, 0, 0, nullptr}};
  const GSHSCENE_Resource * fast = g.face(1);
  const GSHSCENE_Resource * slow = g.face(0);
  Ref unbound(intern_face(g.s.store));
  GSHGEOM_Map id = ident();
  const uint32_t R = GSHSCENE_ENTRY_REFUSED;
  const uint32_t H = GSHSCENE_ENTRY_HINT_STATUS;
  // 1: unbound, whatever the mode and the target.
  for (uint32_t mode = 0; mode < GSHSCENE_HINT_MODE_COUNT; mode++) {
    for (uint32_t kind : {(uint32_t)GSHSCENE_TARGET_RASTER, (uint32_t)GSHSCENE_TARGET_STREAM}) {
      expect_route(route_of(g.run(unbound.get(), one, 16, mode), id, kind), GSHSCENE_GLYPH_GENERAL, R,
          GSHSCENE_REASON_FACE_UNBOUND, "unbound");
    }
  }
  // 2: a stream: general, nothing.
  GSHSCENE_Glyph_Route r = route_of(g.run(fast, one, 16, GSHSCENE_HINT_FONT_STRICT), rotation(30), GSHSCENE_TARGET_STREAM);
  expect_route(r, GSHSCENE_GLYPH_GENERAL, 0, 0, "a stream, strict and rotated");
  EXPECT_EQ(r.upem, 2048u);
  // 3: no fast tier.
  expect_route(route_of(g.run(slow, one, 16, GSHSCENE_HINT_FONT), id), GSHSCENE_GLYPH_GENERAL, H,
      GSHSCENE_REASON_HINT_NO_PROGRAM, "no fast tier");
  expect_route(route_of(g.run(slow, one, 16, GSHSCENE_HINT_NONE), id), GSHSCENE_GLYPH_GENERAL, 0, 0, "no fast tier, NONE");
  // 4: the predicate.
  expect_route(route_of(g.run(fast, one, 16, GSHSCENE_HINT_FONT), rotation(30)), GSHSCENE_GLYPH_GENERAL, H,
      GSHSCENE_REASON_HINT_ROUTE, "rotated");
  expect_route(route_of(g.run(fast, one, 16, GSHSCENE_HINT_FONT_STRICT), rotation(30)), GSHSCENE_GLYPH_GENERAL, R,
      GSHSCENE_REASON_HINT_STRICT_ROUTE, "rotated, strict");
  expect_route(route_of(g.run(fast, one, 16, GSHSCENE_HINT_NONE), rotation(30)), GSHSCENE_GLYPH_GENERAL, 0, 0,
      "rotated, NONE");
  GSHGEOM_Map flipped;
  gshgeom_map_scale(2, 1.0, -1.0, 1.0, &flipped);
  expect_route(route_of(g.run(fast, one, 16, GSHSCENE_HINT_FONT), flipped), GSHSCENE_GLYPH_GENERAL, H,
      GSHSCENE_REASON_HINT_ROUTE, "flipped");
  GSHGEOM_Map canonical;
  gshgeom_map_flip_y(2, &canonical);
  expect_route(route_of(g.run(fast, one, 16, GSHSCENE_HINT_FONT), canonical), GSHSCENE_GLYPH_GENERAL, H,
      GSHSCENE_REASON_HINT_ROUTE, "the canonical flip on an item: glyphs upside down");
  GSHGEOM_Map tagged = compose(rotation(-90), rotation(90));
  expect_route(route_of(g.run(fast, one, 16, GSHSCENE_HINT_FONT), tagged), GSHSCENE_GLYPH_GENERAL, H,
      GSHSCENE_REASON_HINT_ROUTE, "a quarter turn and its inverse: the identity, tagged");
  GSHGEOM_Map skew;
  const double sm[4] = {1.0, 0.25, 0.0, 1.0};
  const double st[2] = {0.0, 0.0};
  gshgeom_map_from_matrix(2, sm, st, &skew);
  expect_route(route_of(g.run(fast, one, 16, GSHSCENE_HINT_FONT), skew), GSHSCENE_GLYPH_GENERAL, H,
      GSHSCENE_REASON_HINT_ROUTE, "skewed");
  // 5: the size.
  expect_route(route_of(g.run(fast, one, 40.0 / 3.0, GSHSCENE_HINT_FONT), id), GSHSCENE_GLYPH_GENERAL, H,
      GSHSCENE_REASON_HINT_SIZE, "13.333");
  expect_route(route_of(g.run(fast, one, 40.0 / 3.0, GSHSCENE_HINT_FONT_STRICT), id), GSHSCENE_GLYPH_GENERAL, R,
      GSHSCENE_REASON_HINT_STRICT_ROUTE, "13.333, strict");
  expect_route(route_of(g.run(fast, one, 40.0 / 3.0, GSHSCENE_HINT_NONE), id), GSHSCENE_GLYPH_GENERAL, 0, 0,
      "13.333, NONE");
  GSHGEOM_Map three;
  gshgeom_map_scale(2, 3.0, 3.0, 1.0, &three);
  ASSERT_EQ(1.0 / 3.0 * 3.0, 1.0) << "the rounded product is an integer";
  expect_route(route_of(g.run(fast, one, 1.0 / 3.0, GSHSCENE_HINT_FONT), three), GSHSCENE_GLYPH_GENERAL, H,
      GSHSCENE_REASON_HINT_SIZE, "a product that only rounds to an integer");
  expect_route(route_of(g.run(fast, one, 0.5, GSHSCENE_HINT_FONT), id), GSHSCENE_GLYPH_GENERAL, H,
      GSHSCENE_REASON_HINT_SIZE, "below 1");
  // Both factors with low halves: (1 - 2^-40)(1 + 2^-40) = 1 - 2^-80 rounds to 1, and only the product of the two low
  // halves (2^-80) says so.
  GSHGEOM_Map wide;
  const double up = 1.0 + 0x1p-40;
  gshgeom_map_scale(2, up, up, 1.0, &wide);
  ASSERT_EQ((1.0 - 0x1p-40) * up, 1.0);
  expect_route(route_of(g.run(fast, one, 1.0 - 0x1p-40, GSHSCENE_HINT_FONT), wide), GSHSCENE_GLYPH_GENERAL, H,
      GSHSCENE_REASON_HINT_SIZE, "a product inexact by 2^-80");
  expect_route(route_of(g.run(fast, one, 4097, GSHSCENE_HINT_FONT), id), GSHSCENE_GLYPH_GENERAL, H,
      GSHSCENE_REASON_HINT_SIZE, "above 4096");
  r = route_of(g.run(fast, one, 4096, GSHSCENE_HINT_FONT), id);
  expect_route(r, GSHSCENE_GLYPH_FAST, 0, 0, "4096");
  r = route_of(g.run(fast, one, 1, GSHSCENE_HINT_FONT), id);
  expect_route(r, GSHSCENE_GLYPH_FAST, 0, 0, "1");
  // 6: the fast tier, at the map's scale times the size.
  GSHGEOM_Map placed;
  GSHGEOM_Map scale;
  gshgeom_map_scale(2, 0.75, 0.75, 1.0, &scale);
  gshgeom_map_translate(2, 3.3, -7.1, 0.0, &placed);
  r = route_of(g.run(fast, one, 16, GSHSCENE_HINT_FONT), compose(placed, scale));
  expect_route(r, GSHSCENE_GLYPH_FAST, 0, 0, "16 at 0.75");
  EXPECT_EQ(r.ppem, 12u);
  EXPECT_EQ(r.scale, 384);
  EXPECT_EQ(r.upem, 2048u);
  EXPECT_EQ(r.hint_mode, (uint32_t)GSHSCENE_HINT_FONT);
  EXPECT_EQ(r.face_flags, GSHSCENE_FACE_HINTING);
  // A face that states a smaller max_ppem.
  g.tp.max_ppem = 20;
  const GSHSCENE_Resource * capped = g.face(1, 0, 0x52);
  g.tp.max_ppem = 0;
  expect_route(route_of(g.run(capped, one, 20, GSHSCENE_HINT_FONT), id), GSHSCENE_GLYPH_FAST, 0, 0, "at max_ppem");
  expect_route(route_of(g.run(capped, one, 21, GSHSCENE_HINT_FONT), id), GSHSCENE_GLYPH_GENERAL, H,
      GSHSCENE_REASON_HINT_SIZE, "past max_ppem");
  // A face without a program: fast, unhinted, one entry unless NONE.
  Glyphs plain(1000, 0);
  const GSHSCENE_Resource * pf = plain.face(1);
  expect_route(route_of(plain.run(pf, one, 16, GSHSCENE_HINT_FONT), id), GSHSCENE_GLYPH_FAST, H,
      GSHSCENE_REASON_HINT_NO_PROGRAM, "no program");
  expect_route(route_of(plain.run(pf, one, 16, GSHSCENE_HINT_FONT_STRICT), id), GSHSCENE_GLYPH_FAST, H,
      GSHSCENE_REASON_HINT_NO_PROGRAM, "no program, strict: unhinted is not refused");
  expect_route(route_of(plain.run(pf, one, 16, GSHSCENE_HINT_NONE), id), GSHSCENE_GLYPH_FAST, 0, 0, "no program, NONE");
}

TEST(GlyphRoute, RefusesWhatIsNotARunFromAFace) {
  Glyphs g;
  Ref sq(intern_square(g.s.store));
  GSHSCENE_Glyph_Run outlines{};
  outlines.source = GSHSCENE_GLYPHS_FROM_OUTLINES;
  GSHSCENE_Glyph og = {1, 0, 0, sq.get()};
  outlines.glyph_count = 1;
  outlines.glyphs = &og;
  Ref orun;
  ASSERT_EQ(gshscene_store_intern_glyph_run(g.s.store, &outlines, &orun.resource), GSHSCENE_OK);
  GSHGEOM_Map id = ident();
  GSHSCENE_Glyph_Route r;
  EXPECT_EQ(gshscene_glyph_route(orun.get(), &id, GSHSCENE_TARGET_RASTER, &r), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_glyph_route(sq.get(), &id, GSHSCENE_TARGET_RASTER, &r), GSHSCENE_ERR_INVALID);
  const GSHSCENE_Resource * run = g.run(g.face(), {{1, 0, 0, nullptr}}, 16);
  EXPECT_EQ(gshscene_glyph_route(nullptr, &id, GSHSCENE_TARGET_RASTER, &r), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_glyph_route(run, nullptr, GSHSCENE_TARGET_RASTER, &r), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_glyph_route(run, &id, GSHSCENE_TARGET_RASTER, nullptr), GSHSCENE_ERR_INVALID);
  GSHGEOM_Map three_d;
  gshgeom_map_identity(3, &three_d);
  EXPECT_EQ(gshscene_glyph_route(run, &three_d, GSHSCENE_TARGET_RASTER, &r), GSHSCENE_ERR_INVALID);
  GSHGEOM_Map bad = id;
  bad.m[0][0] = std::nan("");
  EXPECT_EQ(gshscene_glyph_route(run, &bad, GSHSCENE_TARGET_RASTER, &r), GSHSCENE_ERR_INVALID);
}

// --- the origin and the maps -----------------------------------------------------------------------------------

TEST(GlyphOrigin, IsTheRoundedOriginSplitByAFloorOfEitherSign) {
  Glyphs g;
  const GSHSCENE_Resource * run =
      g.run(g.face(), {{1, -0.3, 2.0 + 63.0 / 64.0, nullptr}, {1, 1.0 / 128.0, -1.0 / 128.0 - 1.0, nullptr},
                          {1, 3e10, 0, nullptr}}, 16);
  GSHGEOM_Map id = ident();
  GSHSCENE_Glyph_Origin o;
  ASSERT_EQ(gshscene_glyph_origin(run, &id, 0, &o), GSHSCENE_OK);
  EXPECT_EQ(o.x, -19);   // floor(-19.2 + 0.5)
  EXPECT_EQ(o.px, -1);
  EXPECT_EQ(o.fx, 45u);
  EXPECT_EQ(o.y, 191);
  EXPECT_EQ(o.py, 2);
  EXPECT_EQ(o.fy, 63u);
  ASSERT_EQ(gshscene_glyph_origin(run, &id, 1, &o), GSHSCENE_OK);
  EXPECT_EQ(o.x, 1) << "half up: 0.5 goes to 1";
  EXPECT_EQ(o.px, 0);
  EXPECT_EQ(o.fx, 1u);
  EXPECT_EQ(o.y, -64) << "half up: -64.5 goes to -64";
  EXPECT_EQ(o.py, -1);
  EXPECT_EQ(o.fy, 0u);
  EXPECT_EQ(gshscene_glyph_origin(run, &id, 2, &o), GSHSCENE_ERR_LIMIT) << "past the 26.6 range";
  EXPECT_EQ(gshscene_glyph_origin(run, &id, 3, &o), GSHSCENE_ERR_INVALID) << "past the run";
  EXPECT_EQ(gshscene_glyph_origin(run, nullptr, 0, &o), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_glyph_origin(run, &id, 0, nullptr), GSHSCENE_ERR_INVALID);
}

TEST(GlyphMap, IsTheMapThenThePositionThenTheDesignScale) {
  Glyphs g;
  const GSHSCENE_Resource * run = g.run(g.face(), {{1, 5.0, 7.0, nullptr}}, 16);
  GSHGEOM_Map item = compose(rotation(30), ident());
  GSHGEOM_Map m;
  ASSERT_EQ(gshscene_glyph_map(run, &item, 0, &m), GSHSCENE_OK);
  GSHGEOM_Map t;
  GSHGEOM_Map d;
  gshgeom_map_translate(2, 5.0, 7.0, 0.0, &t);
  gshgeom_map_design_to_model(16.0 / 2048.0, 0.0, 0.0, &d);
  GSHGEOM_Map want = compose(compose(item, t), d);
  EXPECT_EQ(gshgeom_map_hash(&m), gshgeom_map_hash(&want));
  EXPECT_EQ(gshscene_glyph_map(run, &item, 1, &m), GSHSCENE_ERR_INVALID);
  Ref unbound(intern_face(g.s.store));
  const GSHSCENE_Resource * urun = g.run(unbound.get(), {{1, 0, 0, nullptr}}, 16);
  EXPECT_EQ(gshscene_glyph_map(urun, &item, 0, &m), GSHSCENE_ERR_INVALID);
  // A scale that underflows to zero is a limit, not an invalid map.
  const GSHSCENE_Resource * tiny = g.run(g.face(), {{1, 0, 0, nullptr}}, 0x1p-1070);
  EXPECT_EQ(gshscene_glyph_map(tiny, &item, 0, &m), GSHSCENE_ERR_LIMIT);
  // The snapped map: the 16.16 scale and the rounded origin, nothing else.
  GSHSCENE_Glyph_Route r = route_of(run, ident());
  ASSERT_EQ(r.route, (uint32_t)GSHSCENE_GLYPH_FAST);
  GSHSCENE_Glyph_Origin o{};
  o.x = 320;
  o.y = -449;
  GSHGEOM_Map snap;
  ASSERT_EQ(gshscene_glyph_snap_map(&r, &o, &snap), GSHSCENE_OK);
  EXPECT_EQ(snap.m[0][0], 512.0 / 65536.0);
  EXPECT_EQ(snap.m[1][1], -512.0 / 65536.0);
  EXPECT_EQ(snap.m[0][1], 0.0);
  EXPECT_EQ(snap.m[1][0], 0.0);
  EXPECT_EQ(snap.t[0], 5.0);
  EXPECT_EQ(snap.t[1], -449.0 / 64.0);
  GSHSCENE_Glyph_Route general = route_of(run, rotation(30));
  EXPECT_EQ(gshscene_glyph_snap_map(&general, &o, &snap), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_glyph_snap_map(nullptr, &o, &snap), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_glyph_snap_map(&r, nullptr, &snap), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_glyph_snap_map(&r, &o, nullptr), GSHSCENE_ERR_INVALID);
}

// --- AD-5's gate at the waist ---------------------------------------------------------------------------------------

/**
 * The fast tier with hinting off equals the general route under the snapped map: device path for device path, every
 * outline glyph of the test provider, ppem 8 to 72, a 2048-unit face (every odd unit a tie at 16 ppem) and a 1000-unit
 * one, origin fractions {0, 1, 16, 33, 63} on each axis, and two instances.
 */
TEST(GlyphGate, TheFastTierWithHintingOffIsTheGeneralRouteUnderTheSnappedMap) {
  const uint32_t fractions[] = {0, 1, 16, 33, 63};
  size_t compared = 0;
  for (uint32_t upem : {2048u, 1000u}) {
    Glyphs g(upem, GSHSCENE_FACE_HINTING);
    for (int16_t coord : {(int16_t)0, (int16_t)-9001}) {
      const GSHSCENE_Resource * face = g.face(1, coord);
      for (uint32_t ppem = 8; ppem <= 72; ppem++) {
        for (uint32_t gid : {1u, 2u, 3u, 5u, 6u, 7u}) {
          for (uint32_t fx : fractions) {
            for (uint32_t fy : fractions) {
              // A glyph at 3 + fx/64, 40 + fy/64 under the identity, at an integer size: the fast tier.
              const GSHSCENE_Resource * run =
                  g.run(face, {{gid, 3.0 + fx / 64.0, 40.0 + fy / 64.0, nullptr}}, (double)ppem, GSHSCENE_HINT_NONE);
              GSHGEOM_Map id = ident();
              GSHSCENE_Glyph_Route r = route_of(run, id);
              ASSERT_EQ(r.route, (uint32_t)GSHSCENE_GLYPH_FAST);
              GSHSCENE_Glyph_Origin o;
              ASSERT_EQ(gshscene_glyph_origin(run, &id, 0, &o), GSHSCENE_OK);
              ASSERT_EQ(o.fx, fx);
              ASSERT_EQ(o.fy, fy);
              GSHGEOM_Device_Path * fast = nullptr;
              GSHSCENE_Glyph_Note note;
              ASSERT_EQ(gshscene_glyph_device_path(run, &r, 0, &o, nullptr, nullptr, &fast, &note), GSHSCENE_OK);
              ASSERT_NE(fast, nullptr);
              EXPECT_EQ(note.entry_class, 0u);
              GSHGEOM_Path * outline = nullptr;
              ASSERT_EQ(gshscene_glyph_outline(run, 0, nullptr, nullptr, &outline, &note), GSHSCENE_OK);
              GSHGEOM_Map snap;
              ASSERT_EQ(gshscene_glyph_snap_map(&r, &o, &snap), GSHSCENE_OK);
              GSHGEOM_Device_Path * general = nullptr;
              ASSERT_EQ(gshgeom_device_path_from_model(outline, &snap, nullptr, nullptr, &general), GSHGEOM_OK);
              ASSERT_EQ(elements(fast), elements(general))
                  << "upem " << upem << " ppem " << ppem << " glyph " << gid << " fraction " << fx << "," << fy;
              ASSERT_EQ(gshgeom_device_path_hash(fast), gshgeom_device_path_hash(general));
              compared++;
              gshgeom_device_path_destroy(fast);
              gshgeom_device_path_destroy(general);
              gshgeom_path_destroy(outline);
              gshscene_resource_release(run);
              g.held.pop_back();
            }
          }
        }
      }
    }
  }
  EXPECT_EQ(compared, 2u * 2u * 65u * 6u * 25u);
}

TEST(GlyphGate, HintingIsWhatSeparatesTheRoutes) {
  Glyphs g;
  const GSHSCENE_Resource * face = g.face(1);
  const GSHSCENE_Resource * run = g.run(face, {{1, 3.25, 40.0, nullptr}}, 16, GSHSCENE_HINT_FONT);
  GSHGEOM_Map id = ident();
  GSHSCENE_Glyph_Route r = route_of(run, id);
  GSHSCENE_Glyph_Origin o;
  ASSERT_EQ(gshscene_glyph_origin(run, &id, 0, &o), GSHSCENE_OK);
  GSHGEOM_Device_Path * hinted = nullptr;
  GSHSCENE_Glyph_Note note;
  ASSERT_EQ(gshscene_glyph_device_path(run, &r, 0, &o, nullptr, nullptr, &hinted, &note), GSHSCENE_OK);
  EXPECT_EQ(note.status, (uint32_t)GSHSCENE_HINT_STATUS_HINTED);
  EXPECT_EQ(note.entry_class, 0u) << "HINTED gives no entry";
  GSHGEOM_Path * outline = nullptr;
  ASSERT_EQ(gshscene_glyph_outline(run, 0, nullptr, nullptr, &outline, &note), GSHSCENE_OK);
  GSHGEOM_Map snap;
  ASSERT_EQ(gshscene_glyph_snap_map(&r, &o, &snap), GSHSCENE_OK);
  GSHGEOM_Device_Path * general = nullptr;
  ASSERT_EQ(gshgeom_device_path_from_model(outline, &snap, nullptr, nullptr, &general), GSHGEOM_OK);
  EXPECT_NE(elements(hinted), elements(general)) << "hinting reaches the waist";
  gshgeom_device_path_destroy(hinted);
  gshgeom_device_path_destroy(general);
  gshgeom_path_destroy(outline);
}

TEST(GlyphGate, TheTiesArePinned) {
  // 2048 units at 16 ppem: S = 512, and the rectangle's corner (3, 3) units is a tie on both axes.  Half up in y down:
  // x = floor((192 * 512 + 2^15) / 2^16) = 2; y = floor((-192 * 512 + 2^15) / 2^16) = -1.
  Glyphs g;
  const GSHSCENE_Resource * run = g.run(g.face(1), {{1, 0.0, 0.0, nullptr}}, 16, GSHSCENE_HINT_NONE);
  GSHGEOM_Map id = ident();
  GSHSCENE_Glyph_Route r = route_of(run, id);
  ASSERT_EQ(r.scale, 512);
  GSHSCENE_Glyph_Origin o;
  ASSERT_EQ(gshscene_glyph_origin(run, &id, 0, &o), GSHSCENE_OK);
  GSHGEOM_Device_Path * p = nullptr;
  GSHSCENE_Glyph_Note note;
  ASSERT_EQ(gshscene_glyph_device_path(run, &r, 0, &o, nullptr, nullptr, &p, &note), GSHSCENE_OK);
  std::vector<int64_t> e = elements(p);
  // M (2, -1), L (511, -1): 1021 * 64 * 512 / 2^16 = 510.5 -> 511, L (511, -750): -750.5 -> -750, L (2, -750), Z.
  std::vector<int64_t> want = {0, 2, -1, 1, 511, -1, 1, 511, -750, 1, 2, -750, 4, 2, -1};
  ASSERT_GE(e.size(), want.size());
  EXPECT_EQ(std::vector<int64_t>(e.begin(), e.begin() + (long)want.size()), want);
  gshgeom_device_path_destroy(p);
}

// --- what a provider's answer makes of a glyph ------------------------------------------------------------------------

TEST(GlyphNote, EachAnswerAndEachMode) {
  Glyphs g;
  Glyphs plain(2048, 0);
  GSHGEOM_Map id = ident();
  struct Case {
    Glyphs * g;
    uint32_t glyph;
    uint32_t mode;
    uint32_t entry_class;
    uint32_t reason;
    bool path;
  } cases[] = {
      {&g, 1, GSHSCENE_HINT_FONT, 0, 0, true},
      {&g, 5, GSHSCENE_HINT_FONT, GSHSCENE_ENTRY_HINT_STATUS, GSHSCENE_REASON_HINT_PROGRAM_FAILED, true},
      {&g, 5, GSHSCENE_HINT_FONT_STRICT, GSHSCENE_ENTRY_REFUSED, GSHSCENE_REASON_HINT_PROGRAM_FAILED, false},
      {&g, 5, GSHSCENE_HINT_NONE, 0, 0, true},
      {&g, 6, GSHSCENE_HINT_FONT, GSHSCENE_ENTRY_HINT_STATUS, GSHSCENE_REASON_HINT_WITH_ERRORS, true},
      {&g, 6, GSHSCENE_HINT_NONE, 0, 0, true},
      {&g, 7, GSHSCENE_HINT_FONT, GSHSCENE_ENTRY_HINT_STATUS, GSHSCENE_REASON_HINT_NO_PROGRAM, true},
      {&g, 7, GSHSCENE_HINT_NONE, 0, 0, true},
      {&plain, 7, GSHSCENE_HINT_FONT, 0, 0, true},
      {&plain, 1, GSHSCENE_HINT_FONT_STRICT, 0, 0, true},
      {&g, 4, GSHSCENE_HINT_FONT, GSHSCENE_ENTRY_REFUSED, GSHSCENE_REASON_GLYPH_UNAVAILABLE, false},
      {&g, 4, GSHSCENE_HINT_NONE, GSHSCENE_ENTRY_REFUSED, GSHSCENE_REASON_GLYPH_UNAVAILABLE, false},
      {&g, 9, GSHSCENE_HINT_FONT, GSHSCENE_ENTRY_REFUSED, GSHSCENE_REASON_GLYPH_UNAVAILABLE, false},
      {&g, 0, GSHSCENE_HINT_FONT, 0, 0, true},
  };
  for (const Case & c : cases) {
    const GSHSCENE_Resource * run = c.g->run(c.g->face(1), {{c.glyph, 1.0, 1.0, nullptr}}, 16, c.mode);
    GSHSCENE_Glyph_Route r = route_of(run, id);
    ASSERT_EQ(r.route, (uint32_t)GSHSCENE_GLYPH_FAST);
    GSHSCENE_Glyph_Origin o;
    ASSERT_EQ(gshscene_glyph_origin(run, &id, 0, &o), GSHSCENE_OK);
    GSHGEOM_Device_Path * p = nullptr;
    GSHSCENE_Glyph_Note note;
    std::memset(&note, 0xAB, sizeof note);
    ASSERT_EQ(gshscene_glyph_device_path(run, &r, 0, &o, nullptr, nullptr, &p, &note), GSHSCENE_OK);
    EXPECT_EQ(note.entry_class, c.entry_class) << c.glyph << " mode " << c.mode;
    EXPECT_EQ(note.reason, c.reason) << c.glyph << " mode " << c.mode;
    EXPECT_EQ(p != nullptr, c.path) << c.glyph << " mode " << c.mode;
    gshgeom_device_path_destroy(p);
  }
  // The general route: an outline the provider cannot give is refused, the rest are not.
  for (uint32_t glyph : {1u, 4u, 9u}) {
    const GSHSCENE_Resource * run = g.run(g.face(1), {{glyph, 1.0, 1.0, nullptr}}, 16);
    GSHGEOM_Path * p = nullptr;
    GSHSCENE_Glyph_Note note;
    ASSERT_EQ(gshscene_glyph_outline(run, 0, nullptr, nullptr, &p, &note), GSHSCENE_OK);
    EXPECT_EQ(note.entry_class, glyph == 1 ? 0u : (uint32_t)GSHSCENE_ENTRY_REFUSED) << glyph;
    EXPECT_EQ(note.reason, glyph == 1 ? 0u : (uint32_t)GSHSCENE_REASON_GLYPH_UNAVAILABLE) << glyph;
    EXPECT_EQ(p != nullptr, glyph == 1);
    gshgeom_path_destroy(p);
  }
}

TEST(GlyphNote, ALimitIsTheSinksOwnAndOutOfMemoryStopsTheRender) {
  Glyphs g;
  GSHGEOM_Map id = ident();
  const GSHSCENE_Resource * run = g.run(g.face(1), {{8, 1.0, 1.0, nullptr}}, 16, GSHSCENE_HINT_NONE);
  GSHSCENE_Glyph_Route r = route_of(run, id);
  GSHSCENE_Glyph_Origin o;
  ASSERT_EQ(gshscene_glyph_origin(run, &id, 0, &o), GSHSCENE_OK);
  GSHGEOM_Limits small;
  gshgeom_limits_default(&small);
  small.max_elements = 100;
  GSHGEOM_Device_Path * p = nullptr;
  GSHSCENE_Glyph_Note note;
  ASSERT_EQ(gshscene_glyph_device_path(run, &r, 0, &o, nullptr, &small, &p, &note), GSHSCENE_OK);
  EXPECT_EQ(p, nullptr);
  EXPECT_EQ(note.entry_class, (uint32_t)GSHSCENE_ENTRY_REFUSED);
  EXPECT_EQ(note.reason, (uint32_t)GSHSCENE_REASON_RESOURCE_LIMIT);
  GSHGEOM_Path * outline = nullptr;
  ASSERT_EQ(gshscene_glyph_outline(run, 0, nullptr, &small, &outline, &note), GSHSCENE_OK);
  EXPECT_EQ(outline, nullptr);
  EXPECT_EQ(note.reason, (uint32_t)GSHSCENE_REASON_RESOURCE_LIMIT);
  // A whole pixel that moves a point past the 26.6 range.
  const GSHSCENE_Resource * far = g.run(g.face(1), {{1, 33554000.0, 1.0, nullptr}}, 4096, GSHSCENE_HINT_NONE);
  GSHSCENE_Glyph_Route fr = route_of(far, id);
  ASSERT_EQ(gshscene_glyph_origin(far, &id, 0, &o), GSHSCENE_OK);
  ASSERT_EQ(gshscene_glyph_device_path(far, &fr, 0, &o, nullptr, nullptr, &p, &note), GSHSCENE_OK);
  EXPECT_EQ(p, nullptr);
  EXPECT_EQ(note.reason, (uint32_t)GSHSCENE_REASON_RESOURCE_LIMIT);
  // Every allocation can fail: out of memory, never a note.
  const GSHSCENE_Resource * one = g.run(g.face(1), {{2, 1.0, 1.0, nullptr}}, 16, GSHSCENE_HINT_NONE);
  GSHSCENE_Glyph_Route or1 = route_of(one, id);
  ASSERT_EQ(gshscene_glyph_origin(one, &id, 0, &o), GSHSCENE_OK);
  bool reached = false;
  for (long k = 0; k < 64 && !reached; k++) {
    tracker::Tracker t;
    GSHGEOM_Allocator a = tracker::allocator_for(&t);
    t.fail_from = t.calls + k;
    GSHSCENE_Result rc = gshscene_glyph_device_path(one, &or1, 0, &o, &a, nullptr, &p, &note);
    t.fail_from = -1;
    if (rc == GSHSCENE_OK) {
      reached = p != nullptr;
      EXPECT_EQ(note.entry_class, 0u) << k;
      gshgeom_device_path_destroy(p);
    } else {
      EXPECT_EQ(rc, GSHSCENE_ERR_OOM) << k;
      EXPECT_EQ(p, nullptr);
    }
    EXPECT_TRUE(t.live.empty()) << k;
  }
  EXPECT_TRUE(reached);
  // The arguments.
  EXPECT_EQ(gshscene_glyph_device_path(one, nullptr, 0, &o, nullptr, nullptr, &p, &note), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_glyph_device_path(one, &or1, 1, &o, nullptr, nullptr, &p, &note), GSHSCENE_ERR_INVALID);
  GSHSCENE_Glyph_Origin bad = o;
  bad.fx = 64;
  EXPECT_EQ(gshscene_glyph_device_path(one, &or1, 0, &bad, nullptr, nullptr, &p, &note), GSHSCENE_ERR_INVALID);
  bad = o;
  bad.fy = 64;
  EXPECT_EQ(gshscene_glyph_device_path(one, &or1, 0, &bad, nullptr, nullptr, &p, &note), GSHSCENE_ERR_INVALID);
  GSHSCENE_Glyph_Route general = route_of(one, rotation(30));
  EXPECT_EQ(gshscene_glyph_device_path(one, &general, 0, &o, nullptr, nullptr, &p, &note), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_glyph_outline(one, 1, nullptr, nullptr, &outline, &note), GSHSCENE_ERR_INVALID);
  GSHSCENE_Glyph_Box box;
  EXPECT_EQ(gshscene_glyph_box(one, 1, &box), GSHSCENE_ERR_INVALID);
  ASSERT_EQ(gshscene_glyph_box(one, 0, &box), GSHSCENE_OK);
  EXPECT_EQ(box.empty, 0u);
  EXPECT_EQ(box.x_min, 64 * 275);
  EXPECT_EQ(box.x_max, 64 * 1771);
  EXPECT_EQ(box.y_min, 0);
  EXPECT_EQ(box.y_max, 64 * 1501);
}

namespace {

/** The test provider's fast tier, answering the status its context names past the test provider's. */
uint32_t g_status = 0;
GSHSCENE_Result status_device_path(void * ctx, const GSHSCENE_Face * face, const GSHSCENE_Glyph_Request * rq,
    GSHGEOM_Device_Path * out, GSHSCENE_Glyph_Answer * answer) {
  GSHSCENE_Result r = gshscene_tp_device_path(ctx, face, rq, out, answer);
  answer->status = g_status;
  return r;
}

}  // namespace

TEST(GlyphNote, TheRequestCarriesTheRunsVersionAndTheSnappedScale) {
  Glyphs g;
  GSHGEOM_Map id = ident();
  const GSHSCENE_Resource * run = g.run(g.face(1), {{1, 1.0, 1.0, nullptr}}, 16, GSHSCENE_HINT_FONT, 7);
  GSHSCENE_Glyph_Route r = route_of(run, id);
  ASSERT_EQ(r.route, (uint32_t)GSHSCENE_GLYPH_FAST);
  GSHSCENE_Glyph_Origin o;
  ASSERT_EQ(gshscene_glyph_origin(run, &id, 0, &o), GSHSCENE_OK);
  GSHGEOM_Device_Path * p = nullptr;
  GSHSCENE_Glyph_Note note;
  ASSERT_EQ(gshscene_glyph_device_path(run, &r, 0, &o, nullptr, nullptr, &p, &note), GSHSCENE_OK);
  ASSERT_NE(p, nullptr);
  gshgeom_device_path_destroy(p);
  EXPECT_EQ(gcu_atomic_size_load(&g.tp.last_hint_version), 7u) << "passed unread to the provider";
  // The provider checks the scale against its ppem: a route whose scale is not the seam's is refused by it.
  GSHSCENE_Glyph_Route off = r;
  off.scale += 1;
  ASSERT_EQ(gshscene_glyph_device_path(run, &off, 0, &o, nullptr, nullptr, &p, &note), GSHSCENE_OK);
  EXPECT_EQ(p, nullptr);
  EXPECT_EQ(note.entry_class, (uint32_t)GSHSCENE_ENTRY_REFUSED);
  EXPECT_EQ(note.reason, (uint32_t)GSHSCENE_REASON_GLYPH_UNAVAILABLE);
}

TEST(GlyphNote, AStatusPastTheEnumRefusesTheGlyph) {
  Glyphs g;
  GSHSCENE_Glyph_Provider ops = *gshscene_tp_ops(1);
  ops.glyph_device_path = status_device_path;
  GSHSCENE_Face_Desc desc;
  std::memset(&desc, 0, sizeof desc);
  desc.font_key = 0x777;
  desc.provider = &ops;
  desc.ctx = &g.tp;
  const GSHSCENE_Resource * face = nullptr;
  ASSERT_EQ(gshscene_store_intern_face(g.s.store, &desc, &face), GSHSCENE_OK);
  g.held.push_back(face);
  GSHGEOM_Map id = ident();
  for (uint32_t mode : {(uint32_t)GSHSCENE_HINT_FONT, (uint32_t)GSHSCENE_HINT_NONE}) {
    const GSHSCENE_Resource * run = g.run(face, {{1, 1.0, 1.0, nullptr}}, 16, mode);
    GSHSCENE_Glyph_Route r = route_of(run, id);
    ASSERT_EQ(r.route, (uint32_t)GSHSCENE_GLYPH_FAST);
    GSHSCENE_Glyph_Origin o;
    ASSERT_EQ(gshscene_glyph_origin(run, &id, 0, &o), GSHSCENE_OK);
    for (uint32_t status : {(uint32_t)GSHSCENE_HINT_STATUS_COUNT - 1u, (uint32_t)GSHSCENE_HINT_STATUS_COUNT, 0xFFFFFFFFu}) {
      g_status = status;
      tracker::Tracker t;
      GSHGEOM_Allocator a = tracker::allocator_for(&t);
      GSHGEOM_Device_Path * p = nullptr;
      GSHSCENE_Glyph_Note note;
      ASSERT_EQ(gshscene_glyph_device_path(run, &r, 0, &o, &a, nullptr, &p, &note), GSHSCENE_OK);
      bool known = status < (uint32_t)GSHSCENE_HINT_STATUS_COUNT;
      EXPECT_EQ(p != nullptr, known) << status << " mode " << mode;
      EXPECT_EQ(note.entry_class, known ? (mode == GSHSCENE_HINT_NONE ? 0u : (uint32_t)GSHSCENE_ENTRY_HINT_STATUS)
          : (uint32_t)GSHSCENE_ENTRY_REFUSED) << status << " mode " << mode;
      EXPECT_EQ(note.reason, known ? (mode == GSHSCENE_HINT_NONE ? 0u : (uint32_t)GSHSCENE_REASON_HINT_WITH_ERRORS)
          : (uint32_t)GSHSCENE_REASON_GLYPH_UNAVAILABLE) << status << " mode " << mode;
      gshgeom_device_path_destroy(p);
      EXPECT_TRUE(t.live.empty()) << status << " mode " << mode << ": a refused glyph's path is freed";
    }
  }
  g_status = 0;
}

TEST(GlyphNote, ARefusedOrFailedGlyphLeavesNothingAllocated) {
  Glyphs g;
  GSHGEOM_Map id = ident();
  // The fast tier: a glyph whose program fails under strict (its path is refused) and a corrupt glyph.
  for (uint32_t glyph : {5u, 4u}) {
    const GSHSCENE_Resource * run = g.run(g.face(1), {{glyph, 1.0, 1.0, nullptr}}, 16, GSHSCENE_HINT_FONT_STRICT);
    GSHSCENE_Glyph_Route r = route_of(run, id);
    GSHSCENE_Glyph_Origin o;
    ASSERT_EQ(gshscene_glyph_origin(run, &id, 0, &o), GSHSCENE_OK);
    tracker::Tracker t;
    GSHGEOM_Allocator a = tracker::allocator_for(&t);
    GSHGEOM_Device_Path * p = nullptr;
    GSHSCENE_Glyph_Note note;
    ASSERT_EQ(gshscene_glyph_device_path(run, &r, 0, &o, &a, nullptr, &p, &note), GSHSCENE_OK);
    EXPECT_EQ(p, nullptr);
    EXPECT_EQ(note.entry_class, (uint32_t)GSHSCENE_ENTRY_REFUSED) << glyph;
    EXPECT_GT(t.calls, 0) << "the relative path was made";
    EXPECT_TRUE(t.live.empty()) << glyph;
  }
  // The general route: a corrupt glyph's outline.
  const GSHSCENE_Resource * run = g.run(g.face(1), {{4, 1.0, 1.0, nullptr}}, 16);
  tracker::Tracker t;
  GSHGEOM_Allocator a = tracker::allocator_for(&t);
  GSHGEOM_Path * p = nullptr;
  GSHSCENE_Glyph_Note note;
  ASSERT_EQ(gshscene_glyph_outline(run, 0, &a, nullptr, &p, &note), GSHSCENE_OK);
  EXPECT_EQ(p, nullptr);
  EXPECT_GT(t.calls, 0);
  EXPECT_TRUE(t.live.empty());
}

// --- damage --------------------------------------------------------------------------------------------------------

TEST(GlyphDamage, ARunFromABoundFaceIsBoundedByWhatThePixelSinkDraws) {
  GSHSCENE_Test_Provider tp{};
  tp.upem = 2048;
  SceneHolder s;
  Ref face(gshscene_tp_face(s.store(), &tp, 1, 0x51, 0));
  Ref run(intern_font_run(s.store(), face.get(), 16, {{1, 0.0, 0.0, nullptr}}, GSHSCENE_HINT_NONE));
  VersionRef v0;
  EXPECT_EQ(gshscene_scene_current(s.scene, &v0.version), GSHSCENE_OK);
  VersionRef v1;
  {
    EditHolder e(s);
    GSHSCENE_Node n = shape_node(run.get());
    n.map = translate(10.25, 30.0);
    add(e.edit, GSHSCENE_ROOT_ID, n);
    publish(s, e, &v1);
  }
  GSHSCENE_Diff * d = nullptr;
  ASSERT_EQ(gshscene_diff(v0.get(), v1.get(), nullptr, nullptr, &d), GSHSCENE_OK);
  GSHSCENE_Damage damage;
  ASSERT_EQ(gshscene_diff_damage(d, &damage), GSHSCENE_OK);
  gshscene_diff_destroy(d);
  // The fast tier: x 658/64 .. 1167/64, y 1170/64 .. 1919/64 (TheTiesArePinned's path, moved), out by 1, rounded out.
  EXPECT_EQ(damage.unbounded, 0);
  EXPECT_EQ(damage.empty, 0);
  EXPECT_EQ(damage.rect.x0, 9);
  EXPECT_EQ(damage.rect.y0, 17);
  EXPECT_EQ(damage.rect.x1, 20);
  EXPECT_EQ(damage.rect.y1, 31);
  // The general route (a quarter turn): the design box under the glyph's map, out by 1.
  VersionRef v2;
  {
    EditHolder e(s);
    GSHSCENE_Node n = shape_node(run.get());
    GSHGEOM_Map q = rotation(90);
    GSHGEOM_Map t = translate(40.0, 0.0);
    n.map = compose(t, q);
    add(e.edit, GSHSCENE_ROOT_ID, n);
    publish(s, e, &v2);
  }
  ASSERT_EQ(gshscene_diff(v1.get(), v2.get(), nullptr, nullptr, &d), GSHSCENE_OK); // v2 adds the turned one
  ASSERT_EQ(gshscene_diff_damage(d, &damage), GSHSCENE_OK);
  gshscene_diff_destroy(d);
  // (3, 3)..(1021, 1501) units at 16 / 2048, flipped, turned: x' = 40 - y, y' = x.
  const double k = 16.0 / 2048.0;
  EXPECT_EQ(damage.unbounded, 0);
  EXPECT_EQ(damage.rect.x0, (int64_t)std::floor(40.0 + 3 * k - 1));
  EXPECT_EQ(damage.rect.x1, (int64_t)std::ceil(40.0 + 1501 * k + 1));
  EXPECT_EQ(damage.rect.y0, (int64_t)std::floor(3 * k - 1));
  EXPECT_EQ(damage.rect.y1, (int64_t)std::ceil(1021 * k + 1));
}

namespace {

/** The damage of adding one node drawing @p run under @p map to an empty scene. */
GSHSCENE_Damage damage_of_adding(SceneHolder & s, const GSHSCENE_Resource * run, const GSHGEOM_Map & map) {
  GSHSCENE_Damage damage;
  std::memset(&damage, 0, sizeof damage);
  VersionRef v0;
  EXPECT_EQ(gshscene_scene_current(s.scene, &v0.version), GSHSCENE_OK);
  VersionRef v1;
  {
    EditHolder e(s);
    GSHSCENE_Node n = shape_node(run);
    n.map = map;
    add(e.edit, GSHSCENE_ROOT_ID, n);
    publish(s, e, &v1);
  }
  GSHSCENE_Diff * d = nullptr;
  EXPECT_EQ(gshscene_diff(v0.get(), v1.get(), nullptr, nullptr, &d), GSHSCENE_OK);
  EXPECT_EQ(gshscene_diff_damage(d, &damage), GSHSCENE_OK);
  gshscene_diff_destroy(d);
  return damage;
}

}  // namespace

TEST(GlyphDamage, AGlyphPastTheRangeIsUnboundedAndAnUnavailableOneAddsNothing) {
  GSHSCENE_Test_Provider tp{};
  tp.upem = 2048;
  // The fast tier: an origin past 26.6.
  {
    SceneHolder s;
    Ref face(gshscene_tp_face(s.store(), &tp, 1, 0x51, 0));
    Ref run(intern_font_run(s.store(), face.get(), 16, {{1, 1.0e12, 0.0, nullptr}}, GSHSCENE_HINT_NONE));
    EXPECT_EQ(damage_of_adding(s, run.get(), translate(0.0, 0.0)).unbounded, 1);
  }
  // The general route: a design scale that underflows to zero.
  {
    SceneHolder s;
    Ref face(gshscene_tp_face(s.store(), &tp, 1, 0x51, 0));
    Ref run(intern_font_run(s.store(), face.get(), 5.0e-324, {{1, 0.0, 0.0, nullptr}}, GSHSCENE_HINT_NONE));
    ASSERT_NE(run.get(), nullptr);
    EXPECT_EQ(damage_of_adding(s, run.get(), rotation(90)).unbounded, 1);
  }
  // The general route: a glyph the provider has no box for adds nothing, wherever it is.
  GSHSCENE_Damage alone;
  GSHSCENE_Damage with;
  {
    SceneHolder s;
    Ref face(gshscene_tp_face(s.store(), &tp, 1, 0x51, 0));
    Ref run(intern_font_run(s.store(), face.get(), 16, {{1, 0.0, 0.0, nullptr}}, GSHSCENE_HINT_NONE));
    alone = damage_of_adding(s, run.get(), rotation(90));
  }
  {
    SceneHolder s;
    Ref face(gshscene_tp_face(s.store(), &tp, 1, 0x51, 0));
    Ref run(intern_font_run(s.store(), face.get(), 16, {{1, 0.0, 0.0, nullptr}, {4, 200.0, 300.0, nullptr}},
        GSHSCENE_HINT_NONE));
    with = damage_of_adding(s, run.get(), rotation(90));
  }
  EXPECT_EQ(alone.unbounded, 0);
  EXPECT_EQ(alone.empty, 0);
  EXPECT_EQ(with.unbounded, 0);
  EXPECT_EQ(with.rect.x0, alone.rect.x0);
  EXPECT_EQ(with.rect.y0, alone.rect.y0);
  EXPECT_EQ(with.rect.x1, alone.rect.x1);
  EXPECT_EQ(with.rect.y1, alone.rect.y1);
}

TEST(GlyphDamage, TheFastTiersPathsComeFromTheScenesAllocatorAndAreFreed) {
  tracker::Tracker t;
  GSHSCENE_Allocator a = tracker::allocator_for(&t);
  {
    GSHSCENE_Test_Provider tp{};
    tp.upem = 2048;
    SceneHolder s(nullptr, &a);
    Ref face(gshscene_tp_face(s.store(), &tp, 1, 0x51, 0));
    std::vector<GSHSCENE_Glyph> glyphs;
    for (int i = 0; i < 50; i++) {
      glyphs.push_back(GSHSCENE_Glyph{2, 3.0 * i, 20.0, nullptr});
    }
    Ref run(intern_font_run(s.store(), face.get(), 16, glyphs, GSHSCENE_HINT_NONE));
    VersionRef v0;
    ASSERT_EQ(gshscene_scene_current(s.scene, &v0.version), GSHSCENE_OK);
    VersionRef v1;
    {
      EditHolder e(s);
      add(e.edit, GSHSCENE_ROOT_ID, shape_node(run.get()));
      publish(s, e, &v1);
    }
    size_t live = t.live.size();
    long calls = t.calls;
    GSHSCENE_Diff * d = nullptr;
    ASSERT_EQ(gshscene_diff(v0.get(), v1.get(), nullptr, nullptr, &d), GSHSCENE_OK);
    gshscene_diff_destroy(d);
    EXPECT_GE(t.calls - calls, 50) << "a device path per glyph, from the scene's allocator";
    EXPECT_EQ(t.live.size(), live) << "and each one freed";
    EXPECT_EQ(gcu_atomic_size_load(&tp.fast_calls), 50u);
  }
  EXPECT_TRUE(t.live.empty());
}

// --- ST1: the centerline of a run (strokes story S5) ---------------------------------------------------------------

namespace {

/** A point of a test glyph (26.6 design units) mapped by @p m, restated here from the provider's tables. */
GSHGEOM_Point mapped(const GSHGEOM_Map & m, int32_t x, int32_t y) {
  GSHGEOM_Point in{(double)x / 64.0, (double)y / 64.0, 0.0};
  GSHGEOM_Point out{};
  EXPECT_EQ(gshgeom_map_apply_point(&m, in, &out), GSHGEOM_OK);
  return out;
}

/**
 * ST1 restated: glyph @p glyph of the test provider placed at (@p x, @p y) in a font run of @p size, appended to @p out
 * element for element under `translate(x, y) . design_to_model(size / upem)`, the points mapped one by one.
 */
void restate_font_glyph(GSHGEOM_Path * out, const GSHSCENE_Test_Provider & tp, uint32_t glyph, double x, double y,
    double size, double upem = 2048.0) {
  GSHGEOM_Map t;
  GSHGEOM_Map d;
  GSHGEOM_Map m;
  ASSERT_EQ(gshgeom_map_translate(2, x, y, 0.0, &t), GSHGEOM_OK);
  ASSERT_EQ(gshgeom_map_design_to_model(size / upem, 0.0, 0.0, &d), GSHGEOM_OK);
  ASSERT_EQ(gshgeom_map_compose(&t, &d, &m), GSHGEOM_OK);
  size_t n = 0;
  gshscene_tp_element(&tp, glyph, 0, &n);
  for (size_t i = 0; i < n; i++) {
    size_t unused = 0;
    GSHSCENE_TP_Element e = gshscene_tp_element(&tp, glyph, i, &unused);
    switch (e.verb) {
      case 'M':
        ASSERT_EQ(gshgeom_path_move_to(out, mapped(m, e.p[0], e.p[1])), GSHGEOM_OK);
        break;
      case 'L':
        ASSERT_EQ(gshgeom_path_line_to(out, mapped(m, e.p[0], e.p[1])), GSHGEOM_OK);
        break;
      case 'Q':
        ASSERT_EQ(gshgeom_path_quadratic_to(out, mapped(m, e.p[0], e.p[1]), mapped(m, e.p[2], e.p[3])), GSHGEOM_OK);
        break;
      case 'C':
        ASSERT_EQ(gshgeom_path_cubic_to(out, mapped(m, e.p[0], e.p[1]), mapped(m, e.p[2], e.p[3]),
                      mapped(m, e.p[4], e.p[5])),
            GSHGEOM_OK);
        break;
      default:
        ASSERT_EQ(gshgeom_path_close(out), GSHGEOM_OK);
        break;
    }
  }
}

struct Centerline {
  GSHGEOM_Path * path = nullptr;
  std::vector<GSHSCENE_Glyph_Note> notes;
  GSHSCENE_Result result = GSHSCENE_OK;
  ~Centerline() { gshgeom_path_destroy(path); }
};

/** ST1's function on @p run, with a note for each glyph. */
void centerline_of(const GSHSCENE_Resource * run, Centerline * c, const GSHGEOM_Limits * limits = nullptr,
    const GSHGEOM_Allocator * alloc = nullptr) {
  GSHSCENE_Glyph_Run info;
  ASSERT_EQ(gshscene_resource_glyph_run(run, &info), GSHSCENE_OK);
  c->notes.assign(info.glyph_count, GSHSCENE_Glyph_Note{7, 7, 7}); // overwritten, every entry
  c->result = gshscene_glyph_run_centerline(run, alloc, limits, &c->path, c->notes.data());
}

uint64_t hash_of(const GSHGEOM_Path * p) {
  return gshgeom_path_hash(p);
}

}  // namespace

TEST(GlyphCenterline, IsTheOutlinesPlacedElementForElementInTheOneOrder) {
  Glyphs g;
  GSHGEOM_Path * want = nullptr;
  ASSERT_EQ(gshgeom_path_create(nullptr, nullptr, 2, &want), GSHGEOM_OK);
  // Lines (the rectangle), quadratics (the ring), cubics (the curl) and a zigzag, at fractional positions and a
  // fractional size: the y flip, the scale and the translation are each in the bits.
  struct Place { uint32_t glyph; double x, y; };
  const Place places[] = {{1, 0.0, 0.0}, {2, 31.25, -4.5}, {3, -7.75, 19.0}, {8, 100.5, 3.25}, {1, 0.0, 0.0}};
  std::vector<GSHSCENE_Glyph> glyphs;
  for (const Place & pl : places) {
    glyphs.push_back(GSHSCENE_Glyph{pl.glyph, pl.x, pl.y, nullptr});
    restate_font_glyph(want, g.tp, pl.glyph, pl.x, pl.y, 13.5);
  }
  const GSHSCENE_Resource * run = g.run(g.face(), glyphs, 13.5, GSHSCENE_HINT_NONE);
  Centerline c;
  centerline_of(run, &c);
  ASSERT_EQ(c.result, GSHSCENE_OK);
  ASSERT_NE(c.path, nullptr);
  EXPECT_EQ(gshgeom_path_dimension(c.path), 2);
  EXPECT_EQ(gshgeom_path_element_count(c.path), gshgeom_path_element_count(want));
  EXPECT_EQ(hash_of(c.path), hash_of(want)) << "bit for bit, element for element";
  for (const GSHSCENE_Glyph_Note & n : c.notes) {
    EXPECT_EQ(n.entry_class, 0u) << "a glyph that was added has no note";
    EXPECT_EQ(n.reason, 0u);
  }
  // The same bits whatever the hinting request: ST3 (the general route's outline is all a stroke reads).
  for (uint32_t mode : {(uint32_t)GSHSCENE_HINT_FONT, (uint32_t)GSHSCENE_HINT_FONT_STRICT}) {
    const GSHSCENE_Resource * hinted = g.run(g.face(), glyphs, 13.5, mode, 77);
    Centerline h;
    centerline_of(hinted, &h);
    ASSERT_EQ(h.result, GSHSCENE_OK);
    EXPECT_EQ(hash_of(h.path), hash_of(want)) << "mode " << mode;
  }
  EXPECT_EQ(gcu_atomic_size_load(&g.tp.fast_calls), 0u) << "a stroke makes no fast-tier call";
  gshgeom_path_destroy(want);
}

TEST(GlyphCenterline, AGlyphWithNoOutlineAddsNothingAndAGlyphTheProviderCannotGiveHasANote) {
  Glyphs g;
  // 0 is a space, 4 corrupt, 9 unavailable (ERR_INVALID); 5, 6 and 7 are hinting cases with the rectangle's outline.
  const std::vector<GSHSCENE_Glyph> glyphs = {{1, 0.0, 0.0, nullptr}, {0, 20.0, 0.0, nullptr}, {4, 40.0, 0.0, nullptr},
      {2, 60.0, 0.0, nullptr}, {9, 80.0, 0.0, nullptr}, {5, 100.0, 0.0, nullptr}, {6, 120.0, 0.0, nullptr},
      {7, 140.0, 0.0, nullptr}};
  const GSHSCENE_Resource * run = g.run(g.face(), glyphs, 16.0, GSHSCENE_HINT_FONT_STRICT);
  Centerline c;
  centerline_of(run, &c);
  ASSERT_EQ(c.result, GSHSCENE_OK) << "a refused glyph is a note, never an error";
  GSHGEOM_Path * want = nullptr;
  ASSERT_EQ(gshgeom_path_create(nullptr, nullptr, 2, &want), GSHGEOM_OK);
  restate_font_glyph(want, g.tp, 1, 0.0, 0.0, 16.0);
  restate_font_glyph(want, g.tp, 2, 60.0, 0.0, 16.0);
  restate_font_glyph(want, g.tp, 5, 100.0, 0.0, 16.0);
  restate_font_glyph(want, g.tp, 6, 120.0, 0.0, 16.0);
  restate_font_glyph(want, g.tp, 7, 140.0, 0.0, 16.0);
  EXPECT_EQ(hash_of(c.path), hash_of(want)) << "the others are stroked";
  gshgeom_path_destroy(want);
  const uint32_t refused[] = {0, 0, 1, 0, 1, 0, 0, 0};
  for (size_t i = 0; i < c.notes.size(); i++) {
    if (refused[i]) {
      EXPECT_EQ(c.notes[i].entry_class, (uint32_t)GSHSCENE_ENTRY_REFUSED) << i;
      EXPECT_EQ(c.notes[i].reason, (uint32_t)GSHSCENE_REASON_GLYPH_UNAVAILABLE) << i;
    } else {
      EXPECT_EQ(c.notes[i].entry_class, 0u) << i;
      EXPECT_EQ(c.notes[i].reason, 0u) << i;
    }
  }
  // A run of spaces, and an empty run: an empty path, no error, no note.
  const GSHSCENE_Resource * spaces = g.run(g.face(), {{0, 0.0, 0.0, nullptr}, {0, 9.0, 0.0, nullptr}}, 16.0);
  Centerline sp;
  centerline_of(spaces, &sp);
  ASSERT_EQ(sp.result, GSHSCENE_OK);
  EXPECT_EQ(gshgeom_path_element_count(sp.path), 0u);
  const GSHSCENE_Resource * empty = g.run(g.face(), {}, 16.0);
  Centerline em;
  centerline_of(empty, &em);
  ASSERT_EQ(em.result, GSHSCENE_OK);
  EXPECT_EQ(gshgeom_path_element_count(em.path), 0u);
  // Notes are optional.
  GSHGEOM_Path * p = nullptr;
  EXPECT_EQ(gshscene_glyph_run_centerline(run, nullptr, nullptr, &p, nullptr), GSHSCENE_OK);
  gshgeom_path_destroy(p);
}

TEST(GlyphCenterline, AGlyphIsAddedWholeOrNotAtAll) {
  Glyphs g;
  // A scale of 2^40 per design unit puts the curl (glyph 3, x from -101 to 1301 units) over 2^100 at x = 2^100 - 6 * 2^47
  // for its right half only: its first elements are in range and a later one is not.
  const double edge = 0x1p100 - 6.0 * 0x1p47;
  const GSHSCENE_Resource * run = g.run(
      g.face(), {{1, 0.0, 0.0, nullptr}, {3, edge, 0.0, nullptr}, {2, 40.0, 0.0, nullptr}}, 2048.0 * 0x1p40);
  ASSERT_NE(run, nullptr);
  Centerline c;
  centerline_of(run, &c);
  ASSERT_EQ(c.result, GSHSCENE_OK);
  EXPECT_EQ(c.notes[0].entry_class, 0u);
  EXPECT_EQ(c.notes[1].entry_class, (uint32_t)GSHSCENE_ENTRY_REFUSED);
  EXPECT_EQ(c.notes[1].reason, (uint32_t)GSHSCENE_REASON_RESOURCE_LIMIT);
  EXPECT_EQ(c.notes[2].entry_class, 0u);
  GSHGEOM_Path * want = nullptr;
  ASSERT_EQ(gshgeom_path_create(nullptr, nullptr, 2, &want), GSHGEOM_OK);
  restate_font_glyph(want, g.tp, 1, 0.0, 0.0, 2048.0 * 0x1p40);
  restate_font_glyph(want, g.tp, 2, 40.0, 0.0, 2048.0 * 0x1p40);
  EXPECT_EQ(hash_of(c.path), hash_of(want)) << "nothing of the glyph past the range, not a prefix of it";
  gshgeom_path_destroy(want);
  // A glyph map that cannot be formed (a scale that underflows) is the same note.
  const GSHSCENE_Resource * tiny = g.run(g.face(), {{1, 0.0, 0.0, nullptr}}, 0x1p-1070);
  Centerline t;
  centerline_of(tiny, &t);
  ASSERT_EQ(t.result, GSHSCENE_OK);
  EXPECT_EQ(t.notes[0].reason, (uint32_t)GSHSCENE_REASON_RESOURCE_LIMIT);
  EXPECT_EQ(gshgeom_path_element_count(t.path), 0u);
}

TEST(GlyphCenterline, ThePathPastTheLimitsIsTheRunsLimitAndAGlyphPastThemIsANote) {
  Glyphs g;
  const GSHSCENE_Resource * two = g.run(g.face(), {{2, 0.0, 0.0, nullptr}, {2, 50.0, 0.0, nullptr}}, 16.0);
  GSHGEOM_Limits limits;
  gshgeom_limits_default(&limits);
  limits.max_elements = 20; // the ring is 12 elements: one glyph fits, two do not
  Centerline c;
  centerline_of(two, &c, &limits);
  EXPECT_EQ(c.result, GSHSCENE_ERR_LIMIT) << "a run is one region: never stroked in part";
  EXPECT_EQ(c.path, nullptr);
  limits.max_elements = 24;
  Centerline ok;
  centerline_of(two, &ok, &limits);
  EXPECT_EQ(ok.result, GSHSCENE_OK);
  EXPECT_EQ(gshgeom_path_element_count(ok.path), 24u);
  // A glyph whose own outline is past the limit is that glyph's note, as a fill's glyph is (the rectangle is ten
  // elements, the ring twelve): the run is then an empty path, and no error.
  limits.max_elements = 8;
  const GSHSCENE_Resource * mixed = g.run(g.face(), {{1, 0.0, 0.0, nullptr}, {2, 50.0, 0.0, nullptr}}, 16.0);
  Centerline m;
  centerline_of(mixed, &m, &limits);
  ASSERT_EQ(m.result, GSHSCENE_OK);
  for (const GSHSCENE_Glyph_Note & n : m.notes) {
    EXPECT_EQ(n.entry_class, (uint32_t)GSHSCENE_ENTRY_REFUSED);
    EXPECT_EQ(n.reason, (uint32_t)GSHSCENE_REASON_RESOURCE_LIMIT);
  }
  EXPECT_EQ(gshgeom_path_element_count(m.path), 0u);
}

TEST(GlyphCenterline, ARunFromOutlinesIsItsPathsUnderTheTranslation) {
  StoreHolder s;
  // A path with every verb, open subpaths included, and a rational conic: nothing is closed that was not.
  GSHGEOM_Path * a = new_path();
  gshgeom_path_move_to(a, P(1.5, 2.25));
  gshgeom_path_line_to(a, P(4.0, 2.25));
  gshgeom_path_quadratic_to(a, P(5.0, 3.0), P(4.5, 6.0));
  gshgeom_path_cubic_to(a, P(4.0, 8.0), P(2.0, 8.5), P(1.0, 7.0));
  gshgeom_path_close(a);
  gshgeom_path_move_to(a, P(10.0, 10.0));
  const double w3[3] = {1.0, 0.70710678118654757, 1.0};
  gshgeom_path_rational_quadratic_to(a, P(14.0, 10.0), P(14.0, 14.0), w3);
  const double w4[4] = {1.0, 2.0, 0.5, 1.0};
  gshgeom_path_rational_cubic_to(a, P(15.0, 17.0), P(11.0, 19.0), P(10.0, 15.0), w4);
  Ref pa(scene::intern(s.store, a));
  GSHGEOM_Path * b = new_path();
  gshgeom_path_move_to(b, P(0.0, 0.0));
  gshgeom_path_line_to(b, P(3.0, 1.0));
  Ref pb(scene::intern(s.store, b));
  GSHGEOM_Path * none = new_path();
  Ref pn(scene::intern(s.store, none));
  const double xs[4] = {5.5, -3.0, 0.0, 17.125};
  GSHSCENE_Glyph glyphs[4] = {{1, xs[0], 1.0, pa.get()}, {2, xs[1], 2.5, pb.get()}, {3, xs[2], 0.0, pn.get()},
      {4, xs[3], -8.0, pa.get()}};
  GSHSCENE_Glyph_Run run{};
  run.source = GSHSCENE_GLYPHS_FROM_OUTLINES;
  run.glyph_count = 4;
  run.glyphs = glyphs;
  const GSHSCENE_Resource * r = nullptr;
  ASSERT_EQ(gshscene_store_intern_glyph_run(s.store, &run, &r), GSHSCENE_OK);
  Ref text(r);
  Centerline c;
  centerline_of(text.get(), &c);
  ASSERT_EQ(c.result, GSHSCENE_OK);
  // The restatement: read each outline back and place it with the library's own map, point by point.
  GSHGEOM_Path * want = new_path();
  for (const GSHSCENE_Glyph & gl : glyphs) {
    GSHGEOM_Map t;
    ASSERT_EQ(gshgeom_map_translate(2, gl.x, gl.y, 0.0, &t), GSHGEOM_OK);
    const GSHGEOM_Path * src = gshscene_resource_path(gl.outline);
    for (size_t i = 0; i < gshgeom_path_element_count(src); i++) {
      GSHGEOM_Element e;
      ASSERT_EQ(gshgeom_path_element(src, i, &e), GSHGEOM_OK);
      GSHGEOM_Curve m;
      if (e.verb == GSHGEOM_VERB_CLOSE) {
        ASSERT_EQ(gshgeom_path_close(want), GSHGEOM_OK);
        continue;
      }
      ASSERT_EQ(gshgeom_map_apply_curve(&t, &e.curve, &m), GSHGEOM_OK);
      switch (e.verb) {
        case GSHGEOM_VERB_MOVE: gshgeom_path_move_to(want, m.p[0]); break;
        case GSHGEOM_VERB_LINE: gshgeom_path_line_to(want, m.p[1]); break;
        case GSHGEOM_VERB_QUADRATIC: gshgeom_path_quadratic_to(want, m.p[1], m.p[2]); break;
        case GSHGEOM_VERB_CUBIC: gshgeom_path_cubic_to(want, m.p[1], m.p[2], m.p[3]); break;
        case GSHGEOM_VERB_RATIONAL_QUADRATIC: gshgeom_path_rational_quadratic_to(want, m.p[1], m.p[2], m.w); break;
        default: gshgeom_path_rational_cubic_to(want, m.p[1], m.p[2], m.p[3], m.w); break;
      }
    }
  }
  EXPECT_EQ(hash_of(c.path), hash_of(want));
  // The weights are kept (the hash covers them, but say it): the conic's middle weight reads back.
  bool conic = false;
  for (size_t i = 0; i < gshgeom_path_element_count(c.path); i++) {
    GSHGEOM_Element e;
    ASSERT_EQ(gshgeom_path_element(c.path, i, &e), GSHGEOM_OK);
    if (e.verb == GSHGEOM_VERB_RATIONAL_QUADRATIC && e.curve.w[1] == 0.70710678118654757) {
      conic = true;
    }
  }
  EXPECT_TRUE(conic);
  // The first glyph's last subpath is open, and stays open in P (no close was added).
  GSHGEOM_Element last;
  ASSERT_EQ(gshgeom_path_element(c.path, gshgeom_path_element_count(c.path) - 1, &last), GSHGEOM_OK);
  EXPECT_EQ(last.verb, GSHGEOM_VERB_RATIONAL_CUBIC);
  for (const GSHSCENE_Glyph_Note & n : c.notes) {
    EXPECT_EQ(n.entry_class, 0u);
  }
  gshgeom_path_destroy(want);
  gshgeom_path_destroy(a);
  gshgeom_path_destroy(b);
  gshgeom_path_destroy(none);
}

TEST(GlyphCenterline, RefusesWhatIsNotARunItCanStrokeAndAnOutlineOfThreeDimensions) {
  Glyphs g;
  Ref sq(intern_square(g.s.store));
  GSHGEOM_Path * out = nullptr;
  EXPECT_EQ(gshscene_glyph_run_centerline(nullptr, nullptr, nullptr, &out, nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_glyph_run_centerline(sq.get(), nullptr, nullptr, &out, nullptr), GSHSCENE_ERR_INVALID);
  const GSHSCENE_Resource * run = g.run(g.face(), {{1, 0.0, 0.0, nullptr}}, 16.0);
  EXPECT_EQ(gshscene_glyph_run_centerline(run, nullptr, nullptr, nullptr, nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(out, nullptr);
  Ref unbound(intern_face(g.s.store));
  const GSHSCENE_Resource * urun = g.run(unbound.get(), {{1, 0.0, 0.0, nullptr}}, 16.0);
  EXPECT_EQ(gshscene_glyph_run_centerline(urun, nullptr, nullptr, &out, nullptr), GSHSCENE_ERR_INVALID)
      << "a face with no provider has no outline; the plan refuses the stroke before a sink gets here";
  EXPECT_EQ(out, nullptr);
  GSHGEOM_Path * three = new_path(3);
  gshgeom_path_move_to(three, P(0, 0, 1));
  gshgeom_path_line_to(three, P(1, 1, 2));
  Ref p3(scene::intern(g.s.store, three));
  gshgeom_path_destroy(three);
  GSHSCENE_Glyph gl[1] = {{1, 0.0, 0.0, p3.get()}};
  GSHSCENE_Glyph_Run r3{};
  r3.source = GSHSCENE_GLYPHS_FROM_OUTLINES;
  r3.glyph_count = 1;
  r3.glyphs = gl;
  const GSHSCENE_Resource * h = nullptr;
  ASSERT_EQ(gshscene_store_intern_glyph_run(g.s.store, &r3, &h), GSHSCENE_OK);
  Ref run3(h);
  EXPECT_EQ(gshscene_glyph_run_centerline(run3.get(), nullptr, nullptr, &out, nullptr), GSHSCENE_ERR_UNSUPPORTED);
  EXPECT_EQ(out, nullptr);
}

TEST(GlyphCenterline, EveryAllocationCanFailAndNothingIsLeftBehind) {
  Glyphs g;
  const GSHSCENE_Resource * run =
      g.run(g.face(), {{2, 0.0, 0.0, nullptr}, {4, 10.0, 0.0, nullptr}, {3, 30.0, 4.0, nullptr}}, 14.0);
  long total = 0;
  {
    tracker::Tracker t;
    GSHGEOM_Allocator a = tracker::allocator_for(&t);
    Centerline c;
    centerline_of(run, &c, nullptr, &a);
    ASSERT_EQ(c.result, GSHSCENE_OK);
    total = t.calls;
    EXPECT_GT(total, 3);
    gshgeom_path_destroy(c.path);
    c.path = nullptr;
    EXPECT_TRUE(t.live.empty());
  }
  for (long fail = 0; fail < total; fail++) {
    tracker::Tracker t;
    t.fail_from = fail;
    t.persistent = true;
    GSHGEOM_Allocator a = tracker::allocator_for(&t);
    Centerline c;
    centerline_of(run, &c, nullptr, &a);
    // A provider's outline that cannot be allocated is the render's out of memory too (never a note).
    EXPECT_EQ(c.result, GSHSCENE_ERR_OOM) << fail;
    EXPECT_EQ(c.path, nullptr) << fail;
    EXPECT_TRUE(t.live.empty()) << fail;
  }
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
