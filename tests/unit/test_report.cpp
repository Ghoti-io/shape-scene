/**
 * @file
 *
 * The report (report.h, S5): its strings, its life (create, reset that keeps
 * the storage, destroy), its order, its cap, resolve against a retained
 * scene, and its dump's every write checked.  Its hash and dump spelling
 * against the generator are in test_dump_sink.cpp (they need a render).
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "sink_conformance.h"
#include "scene_fixtures.h"

#include <set>
#include <string>

using namespace sinkconf;

namespace {

GSHSCENE_Sink * full_sink(const GSHSCENE_Allocator * alloc = nullptr) {
  GSHSCENE_Sink * sink = nullptr;
  EXPECT_EQ(gshscene_dump_sink_create(alloc, nullptr, nullptr, &sink), GSHSCENE_OK);
  return sink;
}

} // namespace

TEST(Report, EveryClassAndReasonHasItsOwnString) {
  std::set<std::string> seen;
  for (int c = GSHSCENE_ENTRY_REFUSED; c < GSHSCENE_ENTRY_CLASS_COUNT; c++) {
    EXPECT_TRUE(seen.insert(gshscene_entry_class_string((GSHSCENE_Entry_Class)c)).second) << c;
  }
  EXPECT_STREQ(gshscene_entry_class_string(GSHSCENE_ENTRY_REFUSED), "refused");
  EXPECT_STREQ(gshscene_entry_class_string(GSHSCENE_ENTRY_APPROXIMATED), "approximated");
  EXPECT_STREQ(gshscene_entry_class_string(GSHSCENE_ENTRY_RESOLVED_AMBIGUITY), "resolved-ambiguity");
  EXPECT_STREQ(gshscene_entry_class_string(GSHSCENE_ENTRY_HINT_STATUS), "hint-status");
  EXPECT_STREQ(gshscene_entry_class_string(GSHSCENE_ENTRY_NOTICE), "notice");
  EXPECT_STREQ(gshscene_entry_class_string((GSHSCENE_Entry_Class)0), "unknown class");
  EXPECT_STREQ(gshscene_entry_class_string(GSHSCENE_ENTRY_CLASS_COUNT), "unknown class");
  const char * names[] = {"item-kind", "composite", "glyph-source", "blend-space", "clip-aliased", "resource-limit",
      "seamless", "caller-memory", "tie-layer", "tie-path", "tie-declared", "hint-route", "hint-size",
      "hint-no-program", "hint-program-failed", "hint-with-errors", "hint-strict-route", "glyph-unavailable",
      "face-unbound", "palette-entry", "gradient", "interpolation-space", "map-not-invertible", "gradient-degenerate",
      "group-isolation", "group-knockout", "soft-mask", "stroke-style", "stroke-zero-width",
      "stroke-centerline"};
  ASSERT_EQ(sizeof names / sizeof names[0], (size_t)GSHSCENE_REASON_COUNT - 1);
  for (int r = GSHSCENE_REASON_ITEM_KIND; r < GSHSCENE_REASON_COUNT; r++) {
    EXPECT_STREQ(gshscene_reason_string((GSHSCENE_Reason)r), names[r - 1]) << r;
  }
  EXPECT_STREQ(gshscene_reason_string((GSHSCENE_Reason)0), "unknown reason");
  EXPECT_STREQ(gshscene_reason_string(GSHSCENE_REASON_COUNT), "unknown reason");
}

TEST(Report, ThePinnedValuesArePinned) {
  EXPECT_EQ((int)GSHSCENE_ENTRY_REFUSED, 1);
  EXPECT_EQ((int)GSHSCENE_ENTRY_APPROXIMATED, 2);
  EXPECT_EQ((int)GSHSCENE_ENTRY_RESOLVED_AMBIGUITY, 3);
  EXPECT_EQ((int)GSHSCENE_ENTRY_HINT_STATUS, 4);
  EXPECT_EQ((int)GSHSCENE_ENTRY_NOTICE, 5);
  EXPECT_EQ((int)GSHSCENE_ENTRY_CLASS_COUNT, 6);
  EXPECT_EQ((int)GSHSCENE_REASON_ITEM_KIND, 1);
  EXPECT_EQ((int)GSHSCENE_REASON_COMPOSITE, 2);
  EXPECT_EQ((int)GSHSCENE_REASON_GLYPH_SOURCE, 3);
  EXPECT_EQ((int)GSHSCENE_REASON_BLEND_SPACE, 4);
  EXPECT_EQ((int)GSHSCENE_REASON_CLIP_ALIASED, 5);
  EXPECT_EQ((int)GSHSCENE_REASON_RESOURCE_LIMIT, 6);
  EXPECT_EQ((int)GSHSCENE_REASON_SEAMLESS, 7);
  EXPECT_EQ((int)GSHSCENE_REASON_CALLER_MEMORY, 8);
  EXPECT_EQ((int)GSHSCENE_REASON_TIE_LAYER, 9);
  EXPECT_EQ((int)GSHSCENE_REASON_TIE_PATH, 10);
  EXPECT_EQ((int)GSHSCENE_REASON_TIE_DECLARED, 11);
  EXPECT_EQ((int)GSHSCENE_REASON_HINT_ROUTE, 12);
  EXPECT_EQ((int)GSHSCENE_REASON_FACE_UNBOUND, 19);
  EXPECT_EQ((int)GSHSCENE_REASON_PALETTE_ENTRY, 20);
  EXPECT_EQ((int)GSHSCENE_REASON_GRADIENT, 21);
  EXPECT_EQ((int)GSHSCENE_REASON_INTERPOLATION_SPACE, 22);
  EXPECT_EQ((int)GSHSCENE_REASON_MAP_NOT_INVERTIBLE, 23);
  EXPECT_EQ((int)GSHSCENE_REASON_GRADIENT_DEGENERATE, 24);
  EXPECT_EQ((int)GSHSCENE_REASON_GROUP_ISOLATION, 25);
  EXPECT_EQ((int)GSHSCENE_REASON_GROUP_KNOCKOUT, 26);
  EXPECT_EQ((int)GSHSCENE_REASON_SOFT_MASK, 27);
  EXPECT_EQ((int)GSHSCENE_REASON_STROKE_STYLE, 28);
  EXPECT_EQ((int)GSHSCENE_REASON_STROKE_ZERO_WIDTH, 29);
  EXPECT_EQ((int)GSHSCENE_REASON_STROKE_CENTERLINE, 30);
  EXPECT_EQ((int)GSHSCENE_REASON_COUNT, 31);
}

TEST(Report, CreateDestroyAndTheEmptyReport) {
  EXPECT_EQ(gshscene_report_create(nullptr, nullptr, nullptr), GSHSCENE_ERR_INVALID);
  GSHSCENE_Report * report = nullptr;
  ASSERT_EQ(gshscene_report_create(nullptr, nullptr, &report), GSHSCENE_OK);
  EXPECT_EQ(gshscene_report_count(report), 0u);
  GSHSCENE_Report_Summary s;
  std::memset(&s, 0xab, sizeof s);
  ASSERT_EQ(gshscene_report_summary(report, &s), GSHSCENE_OK);
  EXPECT_EQ(s.result, GSHSCENE_OK);
  EXPECT_EQ(s.counted, 0u);
  EXPECT_EQ(s.units_total, 0u);
  EXPECT_EQ(s.overflowed, 0);
  EXPECT_EQ(s.failure.result, GSHSCENE_OK);
  EXPECT_EQ(s.failure.chain, nullptr);
  EXPECT_EQ(s.failure.chain_count, 0u);
  ASSERT_NE(s.failure.reason, nullptr);
  EXPECT_STREQ(s.failure.reason, "");
  GSHSCENE_Report_Entry e{};
  EXPECT_EQ(gshscene_report_entry(report, 0, &e), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_report_entry(nullptr, 0, &e), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_report_entry(report, 0, nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_report_summary(nullptr, &s), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_report_summary(report, nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_report_count(nullptr), 0u);
  EXPECT_EQ(gshscene_report_reset(nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_report_reset(report), GSHSCENE_OK);
  EXPECT_EQ(gshscene_report_hash(nullptr), 0xcbf29ce484222325ull);
  EXPECT_NE(gshscene_report_hash(report), 0xcbf29ce484222325ull);
  EXPECT_EQ(gshscene_report_dump(nullptr, stdout), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_report_dump(report, nullptr), GSHSCENE_ERR_INVALID);
  gshscene_report_destroy(report);
  gshscene_report_destroy(nullptr);
  // Every allocation of create can fail, leaving nothing.
  for (long fail = 0; fail < 4; fail++) {
    tracker::Tracker t;
    t.fail_from = fail;
    GSHSCENE_Allocator a = tracker::allocator_for(&t);
    GSHSCENE_Report * r = nullptr;
    GSHSCENE_Result result = gshscene_report_create(&a, nullptr, &r);
    if (result == GSHSCENE_OK) {
      gshscene_report_destroy(r);
    } else {
      EXPECT_EQ(result, GSHSCENE_ERR_OOM);
      EXPECT_EQ(r, nullptr);
    }
    EXPECT_TRUE(t.live.empty());
  }
}

// A render resets the report; a reset keeps the storage, so the second frame allocates nothing for the report.
TEST(Report, ARenderResetsItAndTheStorageIsKept) {
  Scene s;
  const GSHSCENE_List * list = basic(s);
  tracker::Tracker t;
  GSHSCENE_Allocator a = tracker::allocator_for(&t);
  GSHSCENE_Sink * sink = full_sink();
  {
    ReportRef report(&a);
    auto surface = make_stream();
    GSHSCENE_Render_Request req = request_for(list, *surface, report.report);
    ASSERT_EQ(gshscene_sink_render(sink, &req), GSHSCENE_OK);
    uint64_t first = gshscene_report_hash(report.report);
    size_t count = gshscene_report_count(report.report);
    ASSERT_GT(count, 0u);
    long calls = t.calls;
    for (int frame = 0; frame < 50; frame++) {
      ASSERT_EQ(gshscene_sink_render(sink, &req), GSHSCENE_OK);
      EXPECT_EQ(gshscene_report_count(report.report), count) << "a render starts from an empty report";
      EXPECT_EQ(gshscene_report_hash(report.report), first);
    }
    EXPECT_EQ(t.calls, calls);
    ASSERT_EQ(gshscene_report_reset(report.report), GSHSCENE_OK);
    EXPECT_EQ(gshscene_report_count(report.report), 0u);
    EXPECT_EQ(summary(report.report).counted, 0u);
    EXPECT_EQ(t.calls, calls);
  }
  EXPECT_TRUE(t.live.empty());
  EXPECT_EQ(gshscene_sink_destroy(sink), GSHSCENE_OK);
}

// The order: by chain (render-level first, a prefix before its extensions), then class, then reason, then second;
// an entry the sink adds while drawing lands in place among the planned ones.
TEST(Report, EntriesAreInChainClassReasonOrder) {
  Scene s;
  GSHSCENE_Paint red = solid(1, 0, 0);
  GSHSCENE_Group depth{};
  depth.opacity = 1.0;
  depth.ordering = GSHSCENE_ORDER_DEPTH;
  GSHSCENE_List * pl = s.open();
  gshscene_list_group_push(pl, &depth, 0, 0.0);
  gshscene_list_fill(pl, s.sq(0, 0, 2), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 1.0);
  gshscene_list_fill(pl, s.sq(0, 0, 3), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 1.0);
  gshscene_list_group_pop(pl);
  gshscene_list_close(pl);
  const GSHSCENE_Resource * picture = nullptr;
  gshscene_store_intern_picture(s.store, pl, &picture);
  s.held.push_back(picture);
  GSHSCENE_List * l = s.open();
  gshscene_list_clip_push(l, s.sq(0, 0, 25), nullptr, GSHGEOM_FILL_NONZERO, 0, 0.0); // 0: approximated
  gshscene_list_fill(l, s.sq(0, 0, 40), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0); // 1: past the extent limit
  gshscene_list_clip_pop(l);
  gshscene_list_instance(l, picture, nullptr, 0, 0.0);                               // 3: its tie at 3.1
  gshscene_list_group_push(l, &depth, 0, 0.0);                                       // 4
  gshscene_list_fill(l, s.sq(0, 0, 2), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 1.0); // 5: a tie with 6
  gshscene_list_fill(l, s.sq(0, 0, 3), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 1.0);
  gshscene_list_group_pop(l);
  gshscene_list_close(l);
  GSHSCENE_Dump_Sink_Config c;
  gshscene_dump_sink_config_default(&c);
  c.stream_caps.flags &= ~GSHSCENE_CAP_CLIP_AA;
  c.max_extent = 30.0;
  GSHSCENE_Sink * sink = nullptr;
  ASSERT_EQ(gshscene_dump_sink_create(nullptr, nullptr, &c, &sink), GSHSCENE_OK);
  auto stream = make_stream();
  ReportRef report;
  GSHSCENE_Render_Request req = request_for(l, *stream, report.report);
  stream->target.flags = 0;
  ASSERT_EQ(gshscene_sink_render(sink, &req), GSHSCENE_OK);
  auto e = entries(report.report);
  ASSERT_EQ(e.size(), 4u);
  std::vector<std::vector<size_t>> chains;
  for (const auto & x : e) {
    chains.emplace_back(x.chain, x.chain + x.chain_count);
  }
  EXPECT_EQ(chains[0], (std::vector<size_t>{0}));
  EXPECT_EQ(e[0].entry_class, GSHSCENE_ENTRY_APPROXIMATED);
  EXPECT_EQ(chains[1], (std::vector<size_t>{1}));
  EXPECT_EQ(e[1].entry_class, GSHSCENE_ENTRY_REFUSED);
  EXPECT_EQ(e[1].reason, GSHSCENE_REASON_RESOURCE_LIMIT);
  EXPECT_EQ(chains[2], (std::vector<size_t>{3, 1}));
  EXPECT_EQ(e[2].entry_class, GSHSCENE_ENTRY_RESOLVED_AMBIGUITY);
  EXPECT_EQ(e[2].second, 2u);
  EXPECT_EQ(e[2].depth, 1.0);
  EXPECT_EQ(chains[3], (std::vector<size_t>{5}));
  EXPECT_EQ(e[3].second, 6u);
  EXPECT_EQ(e[3].reason, GSHSCENE_REASON_TIE_DECLARED);
  // The same list with the conflation flag: a render-level entry, first.
  stream->target.flags = GSHSCENE_TARGET_SEAMLESS;
  EXPECT_EQ(gshscene_sink_render(sink, &req), GSHSCENE_ERR_REFUSED);
  e = entries(report.report);
  ASSERT_EQ(e.size(), 1u);
  EXPECT_EQ(e[0].chain_count, 0u);
  EXPECT_EQ(e[0].chain, nullptr);
  EXPECT_EQ(gshscene_sink_destroy(sink), GSHSCENE_OK);
}

// The failure: its chain and its static reason, for a cancel and for a workspace too small.
TEST(Report, TheFailureSaysWhy) {
  Scene s;
  const GSHSCENE_List * list = basic(s);
  GSHSCENE_Sink * sink = full_sink();
  auto stream = make_stream();
  ReportRef report;
  GSHSCENE_Cancel cancel;
  gshscene_cancel_init(&cancel);
  gshscene_cancel_set(&cancel);
  GSHSCENE_Render_Request req = request_for(list, *stream, report.report);
  req.cancel = &cancel;
  EXPECT_EQ(gshscene_sink_render(sink, &req), GSHSCENE_ERR_CANCELLED);
  GSHSCENE_Report_Summary sum = summary(report.report);
  EXPECT_EQ(sum.failure.result, GSHSCENE_ERR_CANCELLED);
  EXPECT_EQ(sum.failure.chain_count, 0u);
  EXPECT_STRNE(sum.failure.reason, "");
  auto dump = [](const GSHSCENE_Report * r) {
    char * buffer = nullptr;
    size_t size = 0;
    FILE * f = open_memstream(&buffer, &size);
    EXPECT_EQ(gshscene_report_dump(r, f), GSHSCENE_OK);
    fclose(f);
    std::string out(buffer, size);
    free(buffer);
    return out;
  };
  EXPECT_NE(dump(report.report).find("\nfailure result=10 chain=- reason=cancelled before the first unit\n"), std::string::npos)
      << dump(report.report);
  stream->target.flags = GSHSCENE_TARGET_SEAMLESS;
  req.cancel = nullptr;
  EXPECT_EQ(gshscene_sink_render(sink, &req), GSHSCENE_ERR_REFUSED);
  EXPECT_NE(dump(report.report).find("\nE class=1 reason=7 chain=- second=0 depth=0000000000000000\n"), std::string::npos)
      << dump(report.report);
  stream->target.flags = 0;
  req.cancel = &cancel;
  gshscene_cancel_init(&cancel);
  EXPECT_FALSE(gshscene_cancel_is_set(&cancel));
  EXPECT_EQ(gshscene_sink_render(sink, &req), GSHSCENE_OK);
  sum = summary(report.report);
  EXPECT_EQ(sum.failure.result, GSHSCENE_OK);
  EXPECT_STREQ(sum.failure.reason, "");
  gshscene_cancel_init(nullptr);
  gshscene_cancel_set(nullptr);
  EXPECT_FALSE(gshscene_cancel_is_set(nullptr));
  EXPECT_EQ(gshscene_sink_destroy(sink), GSHSCENE_OK);
}

// Resolve: a chain of a version's list names its node and its instance path.
TEST(Report, ResolveNamesTheNodeAndTheInstancePath) {
  scene::SceneHolder sh;
  GSHSCENE_Store * store = sh.store();
  GSHGEOM_Path * p = square(0, 0, 4);
  const GSHSCENE_Resource * a = nullptr;
  const GSHSCENE_Resource * b = nullptr;
  gshscene_store_intern_path(store, p, &a);
  gshgeom_path_destroy(p);
  p = square(1, 1, 4);
  gshscene_store_intern_path(store, p, &b);
  gshgeom_path_destroy(p);
  scene::EditHolder edit(sh);
  GSHSCENE_Node g = scene::group_node();
  g.ordering = GSHSCENE_ORDER_DEPTH;
  g.flags = GSHSCENE_NODE_HIDDEN;
  uint64_t gid = scene::add(edit.edit, GSHSCENE_ROOT_ID, g);
  GSHSCENE_Node sa = scene::shape_node(a);
  sa.depth = 3.0;
  GSHSCENE_Node sb = scene::shape_node(b);
  sb.depth = 3.0;
  uint64_t aid = scene::add(edit.edit, gid, sa);
  uint64_t bid = scene::add(edit.edit, gid, sb);
  uint64_t iid = scene::add(edit.edit, GSHSCENE_ROOT_ID, scene::instance_node(gid));
  scene::add(edit.edit, GSHSCENE_ROOT_ID, scene::shape_node(a)); // item 1 of the version's list: a shape
  scene::VersionRef v;
  scene::publish(sh, edit, &v);
  gshscene_resource_release(a);
  gshscene_resource_release(b);
  const GSHSCENE_List * list = nullptr;
  ASSERT_EQ(gshscene_version_list(v.get(), &list), GSHSCENE_OK);
  GSHSCENE_Sink * sink = full_sink();
  auto stream = make_stream();
  ReportRef report;
  GSHSCENE_Render_Request req = request_for(list, *stream, report.report);
  ASSERT_EQ(gshscene_sink_render(sink, &req), GSHSCENE_OK);
  ASSERT_EQ(gshscene_report_count(report.report), 1u);
  GSHSCENE_Report_Entry e{};
  ASSERT_EQ(gshscene_report_entry(report.report, 0, &e), GSHSCENE_OK);
  ASSERT_EQ(e.chain_count, 2u);
  EXPECT_EQ(e.entry_class, GSHSCENE_ENTRY_RESOLVED_AMBIGUITY);
  uint64_t node = 0;
  uint64_t path[2] = {0, 0};
  size_t count = 0;
  ASSERT_EQ(gshscene_report_resolve(report.report, v.get(), 0, &node, path, 2, &count), GSHSCENE_OK);
  EXPECT_EQ(count, 2u);
  EXPECT_EQ(path[0], iid);
  EXPECT_TRUE(node == aid || node == bid);
  EXPECT_EQ(path[1], node);
  EXPECT_EQ(gshscene_report_resolve(report.report, v.get(), 0, &node, path, 1, &count), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_report_resolve(report.report, v.get(), 1, &node, path, 2, &count), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_report_resolve(report.report, nullptr, 0, &node, path, 2, &count), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_report_resolve(nullptr, v.get(), 0, &node, path, 2, &count), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_report_resolve(report.report, v.get(), 0, nullptr, path, 2, &count), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_report_resolve(report.report, v.get(), 0, &node, nullptr, 2, &count), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_report_resolve(report.report, v.get(), 0, &node, path, 2, nullptr), GSHSCENE_ERR_INVALID);
  // A render-level entry has no node.
  stream->target.flags = GSHSCENE_TARGET_SEAMLESS;
  EXPECT_EQ(gshscene_sink_render(sink, &req), GSHSCENE_ERR_REFUSED);
  EXPECT_EQ(gshscene_report_resolve(report.report, v.get(), 0, &node, path, 2, &count), GSHSCENE_ERR_INVALID);
  // A chain whose first item is not an instance of this version cannot be walked into.
  Scene other;
  GSHSCENE_Paint red = solid(1, 0, 0);
  GSHSCENE_List * pl = other.open();
  GSHSCENE_Group depth{};
  depth.opacity = 1.0;
  depth.ordering = GSHSCENE_ORDER_DEPTH;
  gshscene_list_group_push(pl, &depth, 0, 0.0);
  gshscene_list_fill(pl, other.sq(0, 0, 2), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 1.0);
  gshscene_list_fill(pl, other.sq(0, 0, 3), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 1.0);
  gshscene_list_group_pop(pl);
  gshscene_list_close(pl);
  const GSHSCENE_Resource * pic = nullptr;
  gshscene_store_intern_picture(other.store, pl, &pic);
  other.held.push_back(pic);
  GSHSCENE_List * ol = other.open();
  gshscene_list_fill(ol, other.sq(0, 0, 9), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0);
  gshscene_list_instance(ol, pic, nullptr, 0, 0.0);
  gshscene_list_close(ol);
  stream->target.flags = 0;
  req.list = ol;
  ASSERT_EQ(gshscene_sink_render(sink, &req), GSHSCENE_OK);
  ASSERT_EQ(gshscene_report_count(report.report), 1u);
  // Chain 1.1: item 1 of the version's list is a shape, not an instance: there is no picture to walk into.
  EXPECT_NE(gshscene_report_resolve(report.report, v.get(), 0, &node, path, 2, &count), GSHSCENE_OK);
  EXPECT_EQ(gshscene_sink_destroy(sink), GSHSCENE_OK);
}

#ifdef __GLIBC__
namespace {

struct Cookie {
  long fail_at;
  long writes;
};

ssize_t cookie_write(void * cookie, const char *, size_t size) {
  Cookie * c = (Cookie *)cookie;
  if (c->writes++ == c->fail_at) {
    return -1;
  }
  return (ssize_t)size;
}

GSHSCENE_Result dump_failing_at(const GSHSCENE_Report * report, long fail_at, long * writes, bool unbuffered) {
  Cookie cookie{fail_at, 0};
  cookie_io_functions_t functions = {nullptr, cookie_write, nullptr, nullptr};
  FILE * f = fopencookie(&cookie, "w", functions);
  if (unbuffered) {
    std::setvbuf(f, nullptr, _IONBF, 0);
  }
  GSHSCENE_Result r = gshscene_report_dump(report, f);
  std::fclose(f);
  *writes = cookie.writes;
  return r;
}

} // namespace

// Every write of the dump is checked, buffered and unbuffered, for a report with entries and a failure.
TEST(Report, EveryWriteOfTheDumpIsChecked) {
  Scene s;
  const GSHSCENE_List * list = basic(s);
  GSHSCENE_Dump_Sink_Config c;
  gshscene_dump_sink_config_default(&c);
  c.max_extent = 5.0;
  GSHSCENE_Sink * sink = nullptr;
  ASSERT_EQ(gshscene_dump_sink_create(nullptr, nullptr, &c, &sink), GSHSCENE_OK);
  auto stream = make_stream();
  ReportRef report;
  GSHSCENE_Render_Request req = request_for(list, *stream, report.report);
  req.flags = GSHSCENE_RENDER_STRICT;
  ASSERT_EQ(gshscene_sink_render(sink, &req), GSHSCENE_ERR_REFUSED);
  ASSERT_GT(gshscene_report_count(report.report), 1u);
  ASSERT_EQ(summary(report.report).failure.chain_count, 1u);
  for (bool unbuffered : {true, false}) {
    long total = 0;
    ASSERT_EQ(dump_failing_at(report.report, -1, &total, unbuffered), GSHSCENE_OK);
    ASSERT_GT(total, 0);
    for (long k = 0; k < total; k++) {
      long writes = 0;
      EXPECT_EQ(dump_failing_at(report.report, k, &writes, unbuffered), GSHSCENE_ERR_IO) << k << " " << unbuffered;
    }
  }
  EXPECT_EQ(gshscene_sink_destroy(sink), GSHSCENE_OK);
}
#endif

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(SinkConformance);

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
