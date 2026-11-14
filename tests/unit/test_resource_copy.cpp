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
 * Copying a resource into another store (resource.h, CP1 to CP5): one resource of every kind, and a picture with every kind of item
 * naming resources of every kind, copied into a second store, where each must have the content hash and the dump of the original
 * and hold nothing of the first store; the same store, the destination's limits, and every failing allocation.
 */

#include "scene_fixtures.h"

#include "../image_fixtures.h"
#include "../glyph_test_provider.h"

#include "../alloc_tracker.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace scene;
using namespace imgfx;

namespace {

/** A source store with a resource of each of the fourteen kinds, a picture that names most of them, and what names the rest. */
struct World {
  StoreHolder src;
  std::vector<Ref> all;                        // every resource, in kind order, for the loop tests
  Ref path, tagged, run_outlines, face, run_font, picture, gradient, image, palette, style, plane, image_paint, mask, pattern, hatch,
      clip_union, width_profile, profiled_style;
  RasterHolder raster;

  explicit World(const GSHSCENE_Limits * limits = nullptr, const GSHSCENE_Allocator * alloc = nullptr)
      : src(limits, alloc), raster(make_raster(40, 33, kRgba8)) {
    GSHSCENE_Store * s = src.store;
    path = Ref(intern_fixture(s));
    GSHSCENE_Primitive prim;
    std::memset(&prim, 0, sizeof prim);
    prim.kind = GSHSCENE_PRIMITIVE_RECT;
    prim.x = 1;
    prim.y = 2;
    prim.w = 30;
    prim.h = 40;
    const GSHSCENE_Resource * r = nullptr;
    if (gshscene_store_intern_primitive(s, &prim, &r) == GSHSCENE_OK) {
      tagged = Ref(r);
    }
    /* a glyph run from outlines: two glyphs naming paths */
    Ref sq(intern_square(s));
    GSHSCENE_Glyph g[2] = {{7, 1.0, 2.0, sq.get()}, {9, 3.5, -1.25, path.get()}};
    GSHSCENE_Glyph_Run run;
    std::memset(&run, 0, sizeof run);
    run.source = GSHSCENE_GLYPHS_FROM_OUTLINES;
    run.glyph_count = 2;
    run.glyphs = g;
    if (gshscene_store_intern_glyph_run(s, &run, &r) == GSHSCENE_OK) {
      run_outlines = Ref(r);
    }
    face = Ref(intern_face(s));
    std::vector<GSHSCENE_Glyph> fg = {{3, 0.0, 0.0, nullptr}, {4, 10.5, 0.0, nullptr}};
    run_font = Ref(intern_font_run(s, face.get(), 16.0, fg, 1, 2));
    /* a gradient, a palette, a stroke style, the plane */
    GSHSCENE_Gradient_Stop stops[3];
    for (int i = 0; i < 3; i++) {
      std::memset(&stops[i], 0, sizeof stops[i]);
      stops[i].offset = i * 0.5;
      stops[i].color = solid(0.2 * i, 0.5, 1.0 - 0.3 * i, 1.0);
    }
    GSHSCENE_Gradient grad;
    std::memset(&grad, 0, sizeof grad);
    grad.kind = GSHSCENE_GRADIENT_LINEAR;
    grad.p0[0] = 0;
    grad.p0[1] = 0;
    grad.p1[0] = 10;
    grad.p1[1] = 5;
    gshgeom_map_identity(2, &grad.map);
    grad.stop_count = 3;
    grad.stops = stops;
    if (gshscene_store_intern_gradient(s, &grad, &r) == GSHSCENE_OK) {
      gradient = Ref(r);
    }
    GSHSCENE_Color colors[3];
    for (int i = 0; i < 3; i++) {
      gshscene_color_init(&colors[i]);
      colors[i].rgba[0] = 0.1 * i;
    }
    if (gshscene_store_intern_palette(s, 3, colors, &r) == GSHSCENE_OK) {
      palette = Ref(r);
    }
    GSHSCENE_Stroke_Style st;
    gshscene_stroke_style_init(&st);
    st.width = 3.5;
    static const double dashes[2] = {4.0, 2.0};
    st.dash_count = 2;
    st.dashes = dashes;
    if (gshscene_store_intern_stroke_style(s, &st, &r) == GSHSCENE_OK) {
      style = Ref(r);
    }
    if (gshscene_store_intern_plane(s, &r) == GSHSCENE_OK) {
      plane = Ref(r);
    }
    /* a width profile and a style that names it */
    const GSHSCENE_Width_Stop wstops[3] = {{0, 0.5}, {9, 2.0}, {9, 1.0}};
    if (gshscene_store_intern_width_profile(s, wstops, 3, &r) == GSHSCENE_OK) {
      width_profile = Ref(r);
    }
    GSHSCENE_Stroke_Style ps;
    gshscene_stroke_style_init(&ps);
    ps.width = 5.0;
    ps.join = GSHSCENE_STROKE_JOIN_ROUND;
    ps.cap = GSHSCENE_STROKE_CAP_ROUND;
    ps.width_profile = width_profile.get();
    if (gshscene_store_intern_stroke_style(s, &ps, &r) == GSHSCENE_OK) {
      profiled_style = Ref(r);
    }
    /* an image (and its paint) */
    if (gshscene_store_intern_image(s, raster.raster, nullptr, &r) == GSHSCENE_OK) {
      image = Ref(r);
    }
    GSHSCENE_Image_Paint ip;
    gshscene_image_paint_init(&ip);
    ip.image = image.get();
    gshgeom_map_identity(2, &ip.map);
    if (gshscene_store_intern_image_paint(s, &ip, &r) == GSHSCENE_OK) {
      image_paint = Ref(r);
    }
    /* a picture of a few fills, which a mask, a pattern and the rest name */
    {
      ListHolder lh(s);
      GSHSCENE_Paint pg = solid(0, 0, 0, 1);
      pg.kind = GSHSCENE_PAINT_GRADIENT;
      pg.resource = gradient.get();
      gshscene_list_fill(lh.list, sq.get(), nullptr, GSHGEOM_FILL_NONZERO, &pg, 0, 0);
      GSHGEOM_Map tm = translate(3, 4);
      GSHSCENE_Paint ps = solid(1, 0, 0, 0.5);
      gshscene_list_fill(lh.list, path.get(), &tm, GSHGEOM_FILL_EVEN_ODD, &ps, 2, 0);
      gshscene_list_close(lh.list);
      const GSHSCENE_Resource * pr = nullptr;
      if (gshscene_store_intern_picture(s, lh.list, &pr) == GSHSCENE_OK) {
        picture = Ref(pr);
      }
    }
    mask = Ref(known_mask(s, picture.get(), 1));
    GSHSCENE_Pattern pt;
    gshscene_pattern_init(&pt);
    pt.tile = picture.get();
    pt.w = 8;
    pt.h = 8;
    pt.x_step = 8;
    pt.y_step = 8;
    if (gshscene_store_intern_pattern(s, &pt, &r) == GSHSCENE_OK) {
      pattern = Ref(r);
    }
    GSHSCENE_Hatch_Family fam;
    std::memset(&fam, 0, sizeof fam);
    fam.angle = 45;
    fam.offset_across = 4;
    static const double hd[2] = {3.0, -1.0};
    fam.dash_count = 2;
    fam.dashes = hd;
    GSHSCENE_Stroke_Style hs;
    gshscene_stroke_style_init(&hs);
    const GSHSCENE_Resource * hstyle = nullptr;
    EXPECT_EQ(gshscene_store_intern_stroke_style(s, &hs, &hstyle), GSHSCENE_OK);
    GSHSCENE_Hatch h;
    gshscene_hatch_init(&h);
    h.style = hstyle;
    h.family_count = 1;
    h.families = &fam;
    if (gshscene_store_intern_hatch(s, &h, &r) == GSHSCENE_OK) {
      hatch = Ref(r);
    }
    gshscene_resource_release(hstyle);
    GSHSCENE_Clip_Member members[3];
    std::memset(members, 0, sizeof members);
    members[2].resource = run_font.get(); /* a glyph run from a font is a member (CU24): its face is copied with it */
    members[2].rule = GSHGEOM_FILL_NONZERO;
    members[0].resource = sq.get();
    members[0].rule = GSHGEOM_FILL_NONZERO;
    gshgeom_map_identity(2, &members[0].map);
    members[1].resource = run_outlines.get();
    members[1].rule = GSHGEOM_FILL_EVEN_ODD;
    members[1].map = translate(2, 2);
    if (gshscene_store_intern_clip_union(s, members, 3, &r) == GSHSCENE_OK) {
      clip_union = Ref(r);
    }
  }

  /** Pointers to the resources, in a fixed order, null-free once the constructor has succeeded. */
  std::vector<const GSHSCENE_Resource *> list() const {
    return {path.get(), tagged.get(), run_outlines.get(), face.get(), run_font.get(), picture.get(), gradient.get(), image.get(),
        palette.get(), style.get(), plane.get(), image_paint.get(), mask.get(), pattern.get(), hatch.get(), clip_union.get(), width_profile.get(), profiled_style.get()};
  }
};

/** A big picture: every kind of item and every kind of resource they name, in one closed list of @p s. */
Ref everything_picture(World & w, GSHSCENE_Store * s) {
  ListHolder lh(s);
  GSHSCENE_Paint pgrad = solid(0, 0, 0, 1);
  pgrad.kind = GSHSCENE_PAINT_GRADIENT;
  pgrad.resource = w.gradient.get();
  GSHSCENE_Paint ppat = solid(0, 0, 0, 0.75);
  ppat.kind = GSHSCENE_PAINT_PATTERN;
  ppat.resource = w.pattern.get();
  GSHSCENE_Paint pimg = solid(0, 0, 0, 1);
  pimg.kind = GSHSCENE_PAINT_IMAGE;
  pimg.resource = w.image_paint.get();
  GSHGEOM_Map tm = translate(5, 6);
  EXPECT_EQ(gshscene_list_fill_antialias(lh.list, w.path.get(), &tm, GSHGEOM_FILL_NONZERO, &pgrad, GSHSCENE_ANTIALIAS_NONE, 0, 0),
      GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_fill(lh.list, w.tagged.get(), nullptr, GSHGEOM_FILL_EVEN_ODD, &ppat, 1, 0), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_fill(lh.list, w.run_outlines.get(), nullptr, GSHGEOM_FILL_NONZERO, &pimg, 2, 0), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_fill(lh.list, w.plane.get(), nullptr, GSHGEOM_FILL_NONZERO, nullptr, 3, 0), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_stroke(lh.list, w.path.get(), w.style.get(), &tm, nullptr, 4, 0), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_clip_push(lh.list, w.clip_union.get(), nullptr, GSHGEOM_FILL_NONZERO, 5, 0), GSHSCENE_OK);
  GSHSCENE_Group g = make_group(0.5);
  g.soft_mask = w.mask.get();
  EXPECT_EQ(gshscene_list_group_push(lh.list, &g, 6, 0), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_instance(lh.list, w.picture.get(), &tm, 7, 0), GSHSCENE_OK);
  static const double box[4] = {1.0, 2.0, 30.0, 40.0};
  EXPECT_EQ(gshscene_list_placeholder(lh.list, 1, 1, 1, box, nullptr, 8, 0), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_group_pop(lh.list), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_clip_pop(lh.list), GSHSCENE_OK);
  EXPECT_EQ(gshscene_list_close(lh.list), GSHSCENE_OK);
  const GSHSCENE_Resource * r = nullptr;
  EXPECT_EQ(gshscene_store_intern_picture(s, lh.list, &r), GSHSCENE_OK);
  return Ref(r);
}

}  // namespace

TEST(ResourceCopy, TheWorldHasAResourceOfEveryKind) {
  World w;
  std::vector<GSHSCENE_Resource_Kind> kinds;
  for (const GSHSCENE_Resource * r : w.list()) {
    ASSERT_NE(r, nullptr);
    kinds.push_back(gshscene_resource_kind(r));
  }
  for (int k = (int)GSHSCENE_RESOURCE_PATH; k <= (int)GSHSCENE_RESOURCE_WIDTH_PROFILE; k++) {
    EXPECT_NE(std::find(kinds.begin(), kinds.end(), (GSHSCENE_Resource_Kind)k), kinds.end()) << "kind " << k;
  }
}

TEST(ResourceCopy, EveryKindCopiesToTheSameHashAndTheSameDump) {
  World w;
  StoreHolder dst;
  ASSERT_NE(dst.store, nullptr);
  for (const GSHSCENE_Resource * from : w.list()) {
    const GSHSCENE_Resource * copy = nullptr;
    ASSERT_EQ(gshscene_store_copy_resource(dst.store, from, &copy), GSHSCENE_OK) << gshscene_resource_kind_string(gshscene_resource_kind(from));
    Ref hold(copy);
    EXPECT_NE(copy, from);
    EXPECT_EQ(gshscene_resource_kind(copy), gshscene_resource_kind(from));
    EXPECT_EQ(gshscene_resource_hash(copy), gshscene_resource_hash(from)) << gshscene_resource_kind_string(gshscene_resource_kind(from));
    EXPECT_EQ(dump_resource(copy), dump_resource(from)) << gshscene_resource_kind_string(gshscene_resource_kind(from));
  }
}

TEST(ResourceCopy, ThePictureWithEveryItemKindAndEveryResourceItNamesCopiesWhole) {
  World w;
  Ref big = everything_picture(w, w.src.store);
  ASSERT_NE(big.get(), nullptr);
  StoreHolder dst;
  const GSHSCENE_Resource * copy = nullptr;
  ASSERT_EQ(gshscene_store_copy_resource(dst.store, big.get(), &copy), GSHSCENE_OK);
  Ref hold(copy);
  EXPECT_EQ(gshscene_resource_hash(copy), gshscene_resource_hash(big.get()));
  EXPECT_EQ(dump_resource(copy), dump_resource(big.get()));
  EXPECT_EQ(dump_list(gshscene_resource_picture(copy)), dump_list(gshscene_resource_picture(big.get())));
  EXPECT_EQ(gshscene_list_hash(gshscene_resource_picture(copy)), gshscene_list_hash(gshscene_resource_picture(big.get())));
  /* the copy holds nothing of the source: the source's store can go first */
}

TEST(ResourceCopy, TheCopyOutlivesTheStoreItCameFromAndTheCopiedStoreIsNotTheSource) {
  StoreHolder dst;
  Ref copy_of_big;
  uint64_t hash = 0;
  std::string dump;
  {
    World w;
    Ref big = everything_picture(w, w.src.store);
    hash = gshscene_resource_hash(big.get());
    dump = dump_resource(big.get());
    const GSHSCENE_Resource * copy = nullptr;
    ASSERT_EQ(gshscene_store_copy_resource(dst.store, big.get(), &copy), GSHSCENE_OK);
    copy_of_big = Ref(copy);
  }  // the source store and all of its resources are gone
  EXPECT_EQ(gshscene_resource_hash(copy_of_big.get()), hash);
  EXPECT_EQ(dump_resource(copy_of_big.get()), dump);
}

TEST(ResourceCopy, CopyingTwiceGivesTheSameHandleAndACopyOfACopyIsTheCopy) {
  World w;
  StoreHolder dst;
  for (const GSHSCENE_Resource * from : w.list()) {
    const GSHSCENE_Resource *a = nullptr, *b = nullptr, *c = nullptr;
    ASSERT_EQ(gshscene_store_copy_resource(dst.store, from, &a), GSHSCENE_OK);
    ASSERT_EQ(gshscene_store_copy_resource(dst.store, from, &b), GSHSCENE_OK);
    EXPECT_EQ(a, b) << "interned: equal content is one handle";
    ASSERT_EQ(gshscene_store_copy_resource(dst.store, a, &c), GSHSCENE_OK);
    EXPECT_EQ(a, c) << "CP3: a resource of the destination is its own copy";
    gshscene_resource_release(a);
    gshscene_resource_release(b);
    gshscene_resource_release(c);
  }
}

TEST(ResourceCopy, TheSameStoreReturnsTheResourceRetained) {
  World w;
  const size_t before = gshscene_resource_refcount(w.picture.get());
  const GSHSCENE_Resource * same = nullptr;
  ASSERT_EQ(gshscene_store_copy_resource(w.src.store, w.picture.get(), &same), GSHSCENE_OK);
  EXPECT_EQ(same, w.picture.get());
  EXPECT_EQ(gshscene_resource_refcount(same), before + 1);
  gshscene_resource_release(same);
}

TEST(ResourceCopy, TheArgumentsAreChecked) {
  World w;
  StoreHolder dst;
  const GSHSCENE_Resource * out = reinterpret_cast<const GSHSCENE_Resource *>(0x10);
  EXPECT_EQ(gshscene_store_copy_resource(nullptr, w.path.get(), &out), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_store_copy_resource(dst.store, nullptr, &out), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_store_copy_resource(dst.store, w.path.get(), nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(out, reinterpret_cast<const GSHSCENE_Resource *>(0x10)) << "written only on success";
}

TEST(ResourceCopy, ABoundFaceKeepsItsBinding) {
  StoreHolder src, dst;
  GSHSCENE_Test_Provider tp{};
  gshscene_tp_reset(&tp);
  Ref face(gshscene_tp_face(src.store, &tp, 1, UINT64_C(0x77), 0));
  ASSERT_NE(face.get(), nullptr);
  const GSHSCENE_Resource * copy = nullptr;
  ASSERT_EQ(gshscene_store_copy_resource(dst.store, face.get(), &copy), GSHSCENE_OK);
  Ref hold(copy);
  const GSHSCENE_Glyph_Provider *ops_a = nullptr, *ops_b = nullptr;
  void *ctx_a = nullptr, *ctx_b = nullptr;
  ASSERT_EQ(gshscene_face_binding(face.get(), &ops_a, &ctx_a), GSHSCENE_OK);
  ASSERT_EQ(gshscene_face_binding(copy, &ops_b, &ctx_b), GSHSCENE_OK) << "the copy is bound too";
  EXPECT_EQ(ctx_a, ctx_b);
  GSHSCENE_Face_Info ia, ib;
  ASSERT_EQ(gshscene_face_info(face.get(), &ia), GSHSCENE_OK);
  ASSERT_EQ(gshscene_face_info(copy, &ib), GSHSCENE_OK);
  EXPECT_EQ(ia.upem, ib.upem);
  EXPECT_EQ(ia.glyph_count, ib.glyph_count);
}

TEST(ResourceCopy, TheDestinationsLimitsApplyAndAFailedCopyKeepsNothing) {
  World w;
  GSHSCENE_Limits l;
  gshscene_limits_default(&l);
  l.max_bytes = 4096;  // the image alone is past it
  StoreHolder dst(&l);
  ASSERT_NE(dst.store, nullptr);
  GSHSCENE_Store_Stats before;
  gshscene_store_stats(dst.store, &before);
  const GSHSCENE_Resource * out = nullptr;
  EXPECT_EQ(gshscene_store_copy_resource(dst.store, w.image.get(), &out), GSHSCENE_ERR_LIMIT);
  /* an image paint names the image: the same limit, and the paint is not kept either */
  EXPECT_EQ(gshscene_store_copy_resource(dst.store, w.image_paint.get(), &out), GSHSCENE_ERR_LIMIT);
  GSHSCENE_Store_Stats after;
  gshscene_store_stats(dst.store, &after);
  EXPECT_EQ(after.live, before.live);
  EXPECT_EQ(after.bytes, before.bytes);
}

TEST(ResourceCopy, EveryFailingAllocationOfTheDestinationLeavesItAsItWasAndNothingLeaks) {
  World w;
  Ref big = everything_picture(w, w.src.store);
  tracker::Tracker t;
  GSHSCENE_Allocator a = tracker::allocator_for(&t);
  {
    StoreHolder dst(nullptr, &a);
    ASSERT_NE(dst.store, nullptr);
    bool succeeded = false;
    for (long k = 0; k < 4000 && !succeeded; k++) {
      GSHSCENE_Store_Stats before;
      gshscene_store_stats(dst.store, &before);
      t.persistent = false;
      t.failed = false;
      t.fail_from = t.calls + k;
      const GSHSCENE_Resource * out = nullptr;
      GSHSCENE_Result r = gshscene_store_copy_resource(dst.store, big.get(), &out);
      t.fail_from = -1;
      if (r == GSHSCENE_OK) {
        succeeded = true;
        gshscene_resource_release(out);
      } else {
        EXPECT_EQ(r, GSHSCENE_ERR_OOM) << "at allocation " << k;
        GSHSCENE_Store_Stats after;
        gshscene_store_stats(dst.store, &after);
        EXPECT_EQ(after.live, before.live) << "at allocation " << k;
        EXPECT_EQ(after.bytes, before.bytes) << "at allocation " << k;
      }
    }
    EXPECT_TRUE(succeeded);
  }
  EXPECT_TRUE(t.live.empty()) << t.live.size() << " blocks leaked";
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
