/**
 * @file
 *
 * The contract layer (sink.h): what it does for every sink, seen through a
 * mock sink whose every operation the test controls: making a sink, the caps
 * it states, a sink that fails to begin or to wait, completion later, the
 * helpers a sink calls, the disposition predicate, the expansion walk, and the
 * calls that name no target.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "sink_conformance.h"

#include <set>
#include <thread>

using namespace sinkconf;

namespace {

/** A sink whose behaviour the test sets. */
struct Mock {
  GSHSCENE_Sink_Caps caps{};
  GSHSCENE_Result caps_result = GSHSCENE_OK;
  enum Begin { COMPLETE, FAIL, FAIL_AFTER_COMPLETING, LATER } begin = COMPLETE;
  GSHSCENE_Result begin_result = GSHSCENE_OK;
  GSHSCENE_Result workspace_result = GSHSCENE_OK;
  size_t workspace = 64;
  GSHSCENE_Result wait_result = GSHSCENE_OK;
  bool wait_completes = true;
  GSHSCENE_Job * pending = nullptr;
  int begins = 0;
  int prepares = 0;
  int evicts = 0;
  int destroyed = 0;
  uint64_t statics = 0;
};

Mock * as(void * impl) {
  return (Mock *)impl;
}

GSHSCENE_Result m_caps(void * impl, const GSHSCENE_Target *, GSHSCENE_Sink_Caps * out) {
  *out = as(impl)->caps;
  return as(impl)->caps_result;
}
GSHSCENE_Result m_workspace(void * impl, const GSHSCENE_Render_Request *, size_t * out) {
  *out = as(impl)->workspace;
  return as(impl)->workspace_result;
}
GSHSCENE_Result m_begin(void * impl, GSHSCENE_Job * job) {
  Mock * m = as(impl);
  m->begins++;
  switch (m->begin) {
    case Mock::COMPLETE:
      gshscene_job_unit(job, 1, 1);
      gshscene_job_complete(job, m->begin_result, nullptr, 0, nullptr);
      return GSHSCENE_OK;
    case Mock::FAIL:
      return m->begin_result;
    case Mock::FAIL_AFTER_COMPLETING:
      gshscene_job_complete(job, GSHSCENE_OK, nullptr, 0, nullptr);
      return GSHSCENE_ERR_INTERNAL;
    case Mock::LATER:
      m->pending = job;
      return GSHSCENE_OK;
  }
  return GSHSCENE_ERR_INTERNAL;
}
GSHSCENE_Result m_wait(void * impl, GSHSCENE_Job * job) {
  Mock * m = as(impl);
  if (m->wait_completes) {
    gshscene_job_complete(job, GSHSCENE_OK, nullptr, 0, nullptr);
  }
  return m->wait_result;
}
GSHSCENE_Result m_prepare(void * impl, const GSHSCENE_List *, const GSHSCENE_Target *) {
  as(impl)->prepares++;
  return GSHSCENE_OK;
}
GSHSCENE_Result m_evict(void * impl, uint64_t) {
  as(impl)->evicts++;
  return GSHSCENE_OK;
}
uint64_t m_static(void * impl) {
  return as(impl)->statics;
}
void m_destroy(void * impl) {
  as(impl)->destroyed++;
}

GSHSCENE_Sink_Ops mock_ops(bool affine = false) {
  GSHSCENE_Sink_Ops ops{};
  ops.name = "mock";
  ops.thread_affine = affine;
  ops.caps = m_caps;
  ops.workspace_bytes = m_workspace;
  ops.begin = m_begin;
  ops.wait = m_wait;
  ops.prepare = m_prepare;
  ops.evict = m_evict;
  ops.static_bytes = m_static;
  ops.destroy = m_destroy;
  return ops;
}

GSHSCENE_Sink_Caps blocking_caps() {
  GSHSCENE_Sink_Caps c{};
  c.item_kinds = 0x7e;
  c.composites = 0x1fff;
  c.flags = GSHSCENE_CAP_CAN_BLOCK | GSHSCENE_CAP_CALLER_MEMORY | GSHSCENE_CAP_PREPARE;
  c.cancel_unit = GSHSCENE_CANCEL_BETWEEN_UNITS;
  c.cancel_work_bound = 1;
  c.work_measure = "units";
  return c;
}

struct MockSink {
  Mock mock;
  GSHSCENE_Sink * sink = nullptr;
  explicit MockSink(bool affine = false, const GSHSCENE_Limits * limits = nullptr) {
    mock.caps = blocking_caps();
    if (affine) {
      mock.caps.flags |= GSHSCENE_CAP_THREAD_AFFINE;
    }
    GSHSCENE_Sink_Ops ops = mock_ops(affine);
    EXPECT_EQ(gshscene_sink_create(nullptr, limits, &ops, &mock, &sink), GSHSCENE_OK);
  }
  ~MockSink() {
    if (mock.pending) {
      gshscene_job_complete(mock.pending, GSHSCENE_OK, nullptr, 0, nullptr);
    }
    EXPECT_EQ(gshscene_sink_destroy(sink), GSHSCENE_OK);
    EXPECT_EQ(mock.destroyed, 1);
  }
};

} // namespace

TEST(Sink, CreateValidatesTheOpsAndCanFailToAllocate) {
  Mock m;
  GSHSCENE_Sink * sink = nullptr;
  GSHSCENE_Sink_Ops ops = mock_ops();
  EXPECT_EQ(gshscene_sink_create(nullptr, nullptr, nullptr, &m, &sink), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_sink_create(nullptr, nullptr, &ops, &m, nullptr), GSHSCENE_ERR_INVALID);
  for (int field = 0; field < 4; field++) {
    GSHSCENE_Sink_Ops bad = ops;
    if (field == 0) bad.name = nullptr;
    if (field == 1) bad.caps = nullptr;
    if (field == 2) bad.begin = nullptr;
    if (field == 3) bad.destroy = nullptr;
    EXPECT_EQ(gshscene_sink_create(nullptr, nullptr, &bad, &m, &sink), GSHSCENE_ERR_INVALID) << field;
  }
  GSHSCENE_Limits huge{};
  huge.max_jobs = SIZE_MAX;
  EXPECT_EQ(gshscene_sink_create(nullptr, &huge, &ops, &m, &sink), GSHSCENE_ERR_OOM);
  for (long fail = 0; fail < 3; fail++) {
    tracker::Tracker t;
    t.fail_from = fail;
    GSHSCENE_Allocator a = tracker::allocator_for(&t);
    GSHSCENE_Result r = gshscene_sink_create(&a, nullptr, &ops, &m, &sink);
    if (r == GSHSCENE_OK) {
      EXPECT_EQ(gshscene_sink_destroy(sink), GSHSCENE_OK);
    } else {
      EXPECT_EQ(r, GSHSCENE_ERR_OOM);
    }
    EXPECT_TRUE(t.live.empty());
  }
  EXPECT_EQ(m.destroyed, 1) << "a failed create leaves the implementation to the caller";
  EXPECT_EQ(gshscene_sink_destroy(nullptr), GSHSCENE_OK);
  EXPECT_STREQ(gshscene_sink_name(nullptr), "");
}

// Caps a sink states wrongly are its own invariant failing: ERR_INTERNAL, not something passed on.
TEST(Sink, CapsThatBreakTheContractAreInternal) {
  MockSink ms;
  auto surface = make_stream();
  GSHSCENE_Sink_Caps out{};
  ASSERT_EQ(gshscene_sink_caps(ms.sink, &surface->target, &out), GSHSCENE_OK);
  EXPECT_EQ(out.flags, ms.mock.caps.flags);
  EXPECT_EQ(gshscene_sink_caps(ms.sink, nullptr, &out), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_sink_caps(ms.sink, &surface->target, nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_sink_caps(nullptr, &surface->target, &out), GSHSCENE_ERR_INVALID);
  GSHSCENE_Sink_Caps good = ms.mock.caps;
  std::vector<std::function<void(GSHSCENE_Sink_Caps &)>> bad = {
      [](GSHSCENE_Sink_Caps & c) { c.flags |= 1u << 9; },
      [](GSHSCENE_Sink_Caps & c) { c.cancel_unit = 0; },
      [](GSHSCENE_Sink_Caps & c) { c.cancel_unit = GSHSCENE_CANCEL_UNIT_COUNT; },
      [](GSHSCENE_Sink_Caps & c) { c.work_measure = nullptr; },
      [](GSHSCENE_Sink_Caps & c) { c.flags |= GSHSCENE_CAP_THREAD_AFFINE; }, // the ops say not affine
  };
  for (size_t i = 0; i < bad.size(); i++) {
    ms.mock.caps = good;
    bad[i](ms.mock.caps);
    EXPECT_EQ(gshscene_sink_caps(ms.sink, &surface->target, &out), GSHSCENE_ERR_INTERNAL) << i;
  }
  ms.mock.caps = good;
  ms.mock.caps_result = GSHSCENE_ERR_UNSUPPORTED;
  EXPECT_EQ(gshscene_sink_caps(ms.sink, &surface->target, &out), GSHSCENE_ERR_UNSUPPORTED);
  ms.mock.caps_result = GSHSCENE_OK;
}

// A sink that fails to begin: the contract completes the job with its result, and calls on_complete once.
TEST(Sink, ABeginThatFailsCompletesTheJob) {
  MockSink ms;
  Scene s;
  const GSHSCENE_List * list = basic(s);
  auto surface = make_stream();
  ReportRef report;
  ms.mock.begin = Mock::FAIL;
  ms.mock.begin_result = GSHSCENE_ERR_OOM;
  Outcome o = run(nullptr, ms.sink, request_for(list, *surface, report.report));
  EXPECT_EQ(o.result, GSHSCENE_ERR_OOM);
  EXPECT_EQ(o.callbacks, 1);
  EXPECT_EQ(summary(report.report).result, GSHSCENE_ERR_OOM);
  EXPECT_STRNE(summary(report.report).failure.reason, "");
  // One that completes and still returns an error: completed once, with its own result.
  ms.mock.begin = Mock::FAIL_AFTER_COMPLETING;
  o = run(nullptr, ms.sink, request_for(list, *surface, report.report));
  EXPECT_EQ(o.result, GSHSCENE_OK);
  EXPECT_EQ(o.callbacks, 1);
  // One that completes with an error.
  ms.mock.begin = Mock::COMPLETE;
  ms.mock.begin_result = GSHSCENE_ERR_IO;
  GSHSCENE_Render_Request req = request_for(list, *surface, report.report);
  EXPECT_EQ(gshscene_sink_render(ms.sink, &req), GSHSCENE_ERR_IO);
  ms.mock.begin_result = GSHSCENE_OK;
  EXPECT_EQ(gshscene_sink_render(ms.sink, &req), GSHSCENE_OK);
  EXPECT_EQ(gshscene_sink_render(ms.sink, nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_sink_render(nullptr, &req), GSHSCENE_ERR_INVALID);
  req.target = nullptr;
  EXPECT_EQ(gshscene_sink_render(ms.sink, &req), GSHSCENE_ERR_INVALID);
}

// Completion later: poll says running; wait completes it; a wait that fails without completing completes it with that
// failure; a blocking sink with no wait and a running job is the sink's bug.
TEST(Sink, CompletionLaterAndWait) {
  MockSink ms;
  Scene s;
  const GSHSCENE_List * list = basic(s);
  auto surface = make_stream();
  ReportRef report;
  ms.mock.begin = Mock::LATER;
  GSHSCENE_Render_Request req = request_for(list, *surface, report.report);
  GSHSCENE_Job job{};
  ASSERT_EQ(gshscene_sink_begin(ms.sink, &req, &job), GSHSCENE_OK);
  GSHSCENE_Job_Status st{};
  ASSERT_EQ(gshscene_job_poll(&job, &st), GSHSCENE_OK);
  EXPECT_EQ(st.state, GSHSCENE_JOB_RUNNING);
  EXPECT_EQ(st.units_total, gshscene_list_expansion(list));
  EXPECT_EQ(gshscene_job_poll(&job, nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_sink_wait(nullptr, &job), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_sink_wait(ms.sink, nullptr), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_sink_wait(ms.sink, &job), GSHSCENE_OK);
  ms.mock.pending = nullptr;
  gshscene_job_poll(&job, &st);
  EXPECT_EQ(st.state, GSHSCENE_JOB_DONE);
  // A job of another sink, and a job never begun.
  MockSink other;
  EXPECT_EQ(gshscene_sink_wait(other.sink, &job), GSHSCENE_ERR_INVALID);
  GSHSCENE_Job idle{};
  EXPECT_EQ(gshscene_sink_wait(ms.sink, &idle), GSHSCENE_ERR_INVALID);
  // A failing wait.
  GSHSCENE_Job j2{};
  ASSERT_EQ(gshscene_sink_begin(ms.sink, &req, &j2), GSHSCENE_OK);
  ms.mock.wait_completes = false;
  ms.mock.wait_result = GSHSCENE_ERR_IO;
  EXPECT_EQ(gshscene_sink_wait(ms.sink, &j2), GSHSCENE_ERR_IO);
  ms.mock.pending = nullptr;
  gshscene_job_poll(&j2, &st);
  EXPECT_EQ(st.state, GSHSCENE_JOB_DONE);
  EXPECT_EQ(st.result, GSHSCENE_ERR_IO);
  // A blocking sink without a wait.
  Mock m;
  m.caps = blocking_caps();
  m.begin = Mock::LATER;
  GSHSCENE_Sink_Ops ops = mock_ops();
  ops.wait = nullptr;
  GSHSCENE_Sink * nowait = nullptr;
  ASSERT_EQ(gshscene_sink_create(nullptr, nullptr, &ops, &m, &nowait), GSHSCENE_OK);
  GSHSCENE_Job j3{};
  ReportRef r3;
  GSHSCENE_Render_Request req3 = request_for(list, *surface, r3.report);
  ASSERT_EQ(gshscene_sink_begin(nowait, &req3, &j3), GSHSCENE_OK);
  EXPECT_EQ(gshscene_sink_wait(nowait, &j3), GSHSCENE_ERR_INTERNAL);
  gshscene_job_complete(&j3, GSHSCENE_OK, nullptr, 0, nullptr);
  gshscene_job_complete(&j3, GSHSCENE_ERR_IO, nullptr, 0, nullptr); // a second completion is ignored
  gshscene_job_poll(&j3, &st);
  EXPECT_EQ(st.result, GSHSCENE_OK);
  EXPECT_EQ(gshscene_sink_destroy(nowait), GSHSCENE_OK);
}

// The helpers a sink calls: the request and caps of the job, its per-job state and value, the report entries it may
// add (validated), the work and progress counters, and NULL everywhere.
TEST(Sink, TheJobHelpers) {
  EXPECT_EQ(gshscene_job_request(nullptr), nullptr);
  EXPECT_EQ(gshscene_job_caps(nullptr), nullptr);
  EXPECT_EQ(gshscene_job_target_space(nullptr), nullptr);
  EXPECT_EQ(gshscene_job_impl_state(nullptr), nullptr);
  EXPECT_EQ(gshscene_job_impl_value(nullptr), 0u);
  gshscene_job_set_impl_state(nullptr, nullptr);
  gshscene_job_set_impl_value(nullptr, 1);
  gshscene_job_work(nullptr, 1);
  EXPECT_EQ(gshscene_job_checkpoint(nullptr), 0);
  EXPECT_EQ(gshscene_job_unit(nullptr, 1, 1), 0);
  gshscene_job_set_frame_bytes(nullptr, 1);
  gshscene_job_complete(nullptr, GSHSCENE_OK, nullptr, 0, nullptr);
  EXPECT_EQ(gshscene_job_report(nullptr, GSHSCENE_ENTRY_REFUSED, GSHSCENE_REASON_ITEM_KIND, nullptr, 0), GSHSCENE_ERR_INVALID);

  MockSink ms;
  Scene s;
  const GSHSCENE_List * list = basic(s);
  auto surface = make_stream();
  ReportRef report;
  ms.mock.begin = Mock::LATER;
  GSHSCENE_Render_Request req = request_for(list, *surface, report.report);
  GSHSCENE_Job job{};
  ASSERT_EQ(gshscene_sink_begin(ms.sink, &req, &job), GSHSCENE_OK);
  const GSHSCENE_Render_Request * r = gshscene_job_request(&job);
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->list, list);
  EXPECT_NE(r->target, &surface->target) << "the target is the job's copy";
  EXPECT_EQ(r->target->surface, surface->target.surface);
  EXPECT_EQ(r->target->map.dim, 2) << "the zero map became the identity";
  EXPECT_EQ(gshscene_job_caps(&job)->flags, ms.mock.caps.flags);
  const GSHSCENE_Color_Space * space = gshscene_job_target_space(&job);
  EXPECT_EQ(space->primaries, GCOL_PRIMARIES_SRGB);
  EXPECT_EQ(space->transfer, GCOL_TRANSFER_SRGB);
  int marker = 0;
  gshscene_job_set_impl_state(&job, &marker);
  EXPECT_EQ(gshscene_job_impl_state(&job), &marker);
  gshscene_job_set_impl_value(&job, 42);
  EXPECT_EQ(gshscene_job_impl_value(&job), 42u);
  size_t chain[2] = {3, 1};
  EXPECT_EQ(gshscene_job_report(&job, (GSHSCENE_Entry_Class)0, GSHSCENE_REASON_ITEM_KIND, chain, 2), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_job_report(&job, GSHSCENE_ENTRY_CLASS_COUNT, GSHSCENE_REASON_ITEM_KIND, chain, 2), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_job_report(&job, GSHSCENE_ENTRY_REFUSED, (GSHSCENE_Reason)0, chain, 2), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_job_report(&job, GSHSCENE_ENTRY_REFUSED, GSHSCENE_REASON_COUNT, chain, 2), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_job_report(&job, GSHSCENE_ENTRY_REFUSED, GSHSCENE_REASON_ITEM_KIND, nullptr, 2), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_job_report(&job, GSHSCENE_ENTRY_HINT_STATUS, GSHSCENE_REASON_TIE_DECLARED, chain, 2), GSHSCENE_OK);
  EXPECT_EQ(gshscene_job_report(&job, GSHSCENE_ENTRY_APPROXIMATED, GSHSCENE_REASON_ITEM_KIND, nullptr, 0), GSHSCENE_OK);
  // Work: the most between two checks, and what is left at completion counts.
  gshscene_job_work(&job, 3);
  EXPECT_EQ(gshscene_job_checkpoint(&job), 0);
  gshscene_job_work(&job, 2);
  EXPECT_EQ(gshscene_job_checkpoint(&job), 0);
  gshscene_job_work(&job, UINT64_MAX);
  gshscene_job_work(&job, 5); // saturates
  GSHSCENE_Job_Status st{};
  gshscene_job_poll(&job, &st);
  EXPECT_EQ(st.work_max, 3u);
  gshscene_job_set_frame_bytes(&job, 10);
  gshscene_job_set_frame_bytes(&job, 4); // the peak is kept
  EXPECT_EQ(gshscene_job_unit(&job, 7, 5), 0);
  gshscene_job_poll(&job, &st);
  EXPECT_EQ(st.units_processed, 7u);
  EXPECT_EQ(st.units_done, 5u);
  ms.mock.statics = 99;
  gshscene_job_complete(&job, GSHSCENE_ERR_REFUSED, chain, 2, "a reason");
  ms.mock.pending = nullptr;
  GSHSCENE_Report_Summary sum = summary(report.report);
  EXPECT_EQ(sum.work_max, UINT64_MAX);
  EXPECT_EQ(sum.frame_bytes, 10u);
  EXPECT_EQ(sum.static_bytes, 99u);
  EXPECT_EQ(sum.units_processed, 7u);
  EXPECT_EQ(sum.units_done, 5u);
  ASSERT_EQ(sum.failure.chain_count, 2u);
  EXPECT_EQ(sum.failure.chain[0], 3u);
  EXPECT_EQ(sum.failure.chain[1], 1u);
  EXPECT_STREQ(sum.failure.reason, "a reason");
  auto e = entries(report.report);
  ASSERT_GE(e.size(), 2u);
  EXPECT_EQ(e[0].chain_count, 0u) << "the render-level entry first";
  // A before-submit sink's unit calls progress and does not check.
  ms.mock.caps.cancel_unit = GSHSCENE_CANCEL_BEFORE_SUBMIT;
  GSHSCENE_Cancel cancel;
  gshscene_cancel_init(&cancel);
  req.cancel = &cancel;
  GSHSCENE_Job j2{};
  ASSERT_EQ(gshscene_sink_begin(ms.sink, &req, &j2), GSHSCENE_OK);
  gshscene_cancel_set(&cancel);
  EXPECT_EQ(gshscene_job_unit(&j2, 1, 1), 0);
  EXPECT_EQ(gshscene_job_checkpoint(&j2), 1);
  gshscene_job_complete(&j2, GSHSCENE_OK, nullptr, 0, nullptr);
  ms.mock.pending = nullptr;
}

// Caller memory: a sink without the cap, or without the op, refuses it; the query passes on the sink's failure.
TEST(Sink, TheWorkspaceQuery) {
  MockSink ms;
  Scene s;
  const GSHSCENE_List * list = basic(s);
  auto surface = make_stream();
  ReportRef report;
  GSHSCENE_Render_Request req = request_for(list, *surface, report.report);
  size_t need = 0;
  EXPECT_EQ(gshscene_sink_workspace_bytes(ms.sink, &req, &need), GSHSCENE_OK);
  EXPECT_EQ(need, 64u);
  EXPECT_EQ(gshscene_sink_workspace_bytes(nullptr, &req, &need), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_sink_workspace_bytes(ms.sink, nullptr, &need), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_sink_workspace_bytes(ms.sink, &req, nullptr), GSHSCENE_ERR_INVALID);
  GSHSCENE_Target bad = surface->target;
  bad.unit = 0;
  GSHSCENE_Render_Request r2 = req;
  r2.target = &bad;
  EXPECT_EQ(gshscene_sink_workspace_bytes(ms.sink, &r2, &need), GSHSCENE_ERR_INVALID);
  ms.mock.workspace_result = GSHSCENE_ERR_OOM;
  EXPECT_EQ(gshscene_sink_workspace_bytes(ms.sink, &req, &need), GSHSCENE_ERR_OOM);
  req.flags = GSHSCENE_RENDER_CALLER_MEMORY;
  std::vector<unsigned char> ws(64);
  req.workspace = ws.data();
  req.workspace_bytes = ws.size();
  Outcome o = run(nullptr, ms.sink, req);
  EXPECT_EQ(o.result, GSHSCENE_ERR_OOM) << "a workspace that cannot be sized fails the render";
  EXPECT_EQ(o.callbacks, 1);
  ms.mock.workspace_result = GSHSCENE_OK;
  EXPECT_EQ(run(nullptr, ms.sink, req).result, GSHSCENE_OK);
  ms.mock.caps.flags &= ~GSHSCENE_CAP_CALLER_MEMORY;
  EXPECT_EQ(gshscene_sink_workspace_bytes(ms.sink, &req, &need), GSHSCENE_ERR_REFUSED);
  EXPECT_EQ(run(nullptr, ms.sink, req).result, GSHSCENE_ERR_REFUSED);
  // The cap and no op.
  Mock m;
  m.caps = blocking_caps();
  GSHSCENE_Sink_Ops ops = mock_ops();
  ops.workspace_bytes = nullptr;
  GSHSCENE_Sink * noop = nullptr;
  ASSERT_EQ(gshscene_sink_create(nullptr, nullptr, &ops, &m, &noop), GSHSCENE_OK);
  EXPECT_EQ(gshscene_sink_workspace_bytes(noop, &req, &need), GSHSCENE_ERR_REFUSED);
  ReportRef r3;
  req.report = r3.report;
  EXPECT_EQ(run(nullptr, noop, req).result, GSHSCENE_ERR_REFUSED);
  EXPECT_EQ(gshscene_sink_destroy(noop), GSHSCENE_OK);
}

// Prepare, evict and static bytes through the contract: without the cap nothing is called; with NULL ops nothing is
// held; the calls that name no target keep an affine sink's thread.
TEST(Sink, PrepareEvictAndStaticBytes) {
  Scene s;
  const GSHSCENE_List * list = basic(s);
  GSHSCENE_List * open = s.open();
  auto surface = make_stream();
  {
    MockSink ms;
    EXPECT_EQ(gshscene_sink_prepare(ms.sink, list, &surface->target), GSHSCENE_OK);
    EXPECT_EQ(ms.mock.prepares, 1);
    ms.mock.caps.flags &= ~GSHSCENE_CAP_PREPARE;
    EXPECT_EQ(gshscene_sink_prepare(ms.sink, list, &surface->target), GSHSCENE_OK);
    EXPECT_EQ(ms.mock.prepares, 1) << "a sink without PREPARE is not asked";
    EXPECT_EQ(gshscene_sink_prepare(nullptr, list, &surface->target), GSHSCENE_ERR_INVALID);
    EXPECT_EQ(gshscene_sink_prepare(ms.sink, nullptr, &surface->target), GSHSCENE_ERR_INVALID);
    EXPECT_EQ(gshscene_sink_prepare(ms.sink, open, &surface->target), GSHSCENE_ERR_INVALID);
    EXPECT_EQ(gshscene_sink_prepare(ms.sink, list, nullptr), GSHSCENE_ERR_INVALID);
    GSHSCENE_Target bad = surface->target;
    bad.kind = GSHSCENE_TARGET_GPU_TEXTURE;
    EXPECT_EQ(gshscene_sink_prepare(ms.sink, list, &bad), GSHSCENE_ERR_UNSUPPORTED);
    EXPECT_EQ(gshscene_sink_evict(ms.sink, 7), GSHSCENE_OK);
    EXPECT_EQ(ms.mock.evicts, 1);
    EXPECT_EQ(gshscene_sink_evict(nullptr, 7), GSHSCENE_ERR_INVALID);
    ms.mock.statics = 12;
    uint64_t bytes = 0;
    EXPECT_EQ(gshscene_sink_static_bytes(ms.sink, &bytes), GSHSCENE_OK);
    EXPECT_EQ(bytes, 12u);
    EXPECT_EQ(gshscene_sink_static_bytes(nullptr, &bytes), GSHSCENE_ERR_INVALID);
    EXPECT_EQ(gshscene_sink_static_bytes(ms.sink, nullptr), GSHSCENE_ERR_INVALID);
  }
  {
    Mock m;
    m.caps = blocking_caps();
    GSHSCENE_Sink_Ops ops = mock_ops();
    ops.prepare = nullptr;
    ops.evict = nullptr;
    ops.static_bytes = nullptr;
    GSHSCENE_Sink * bare = nullptr;
    ASSERT_EQ(gshscene_sink_create(nullptr, nullptr, &ops, &m, &bare), GSHSCENE_OK);
    EXPECT_EQ(gshscene_sink_prepare(bare, list, &surface->target), GSHSCENE_OK);
    EXPECT_EQ(gshscene_sink_evict(bare, 1), GSHSCENE_OK);
    uint64_t bytes = 5;
    EXPECT_EQ(gshscene_sink_static_bytes(bare, &bytes), GSHSCENE_OK);
    EXPECT_EQ(bytes, 0u);
    ReportRef report;
    GSHSCENE_Render_Request req = request_for(list, *surface, report.report);
    EXPECT_EQ(gshscene_sink_render(bare, &req), GSHSCENE_OK);
    EXPECT_EQ(summary(report.report).static_bytes, 0u);
    EXPECT_EQ(gshscene_sink_destroy(bare), GSHSCENE_OK);
  }
  {
    MockSink affine(true);
    GSHSCENE_Result evicted = GSHSCENE_ERR_INTERNAL;
    GSHSCENE_Result prepared = GSHSCENE_ERR_INTERNAL;
    std::thread t([&] {
      evicted = gshscene_sink_evict(affine.sink, 1);
      prepared = gshscene_sink_prepare(affine.sink, list, &surface->target);
    });
    t.join();
    EXPECT_EQ(evicted, GSHSCENE_ERR_INVALID);
    EXPECT_EQ(prepared, GSHSCENE_ERR_INVALID);
    EXPECT_EQ(affine.mock.evicts, 0);
    EXPECT_EQ(affine.mock.prepares, 0);
  }
}

// Readback through the contract: the cap, the op, the job's state and sink, the kind, the raster's size and format.
TEST(Sink, ReadbackIsChecked) {
  Scene s;
  const GSHSCENE_List * list = basic(s);
  GSHSCENE_Sink * sink = nullptr;
  ASSERT_EQ(gshscene_dump_sink_create(nullptr, nullptr, nullptr, &sink), GSHSCENE_OK);
  auto raster = make_raster(GIMG_ALPHA_PREMULTIPLIED);
  ReportRef report;
  GSHSCENE_Render_Request req = request_for(list, *raster, report.report);
  GSHSCENE_Job job{};
  ASSERT_EQ(gshscene_sink_begin(sink, &req, &job), GSHSCENE_OK);
  GIMG_Raster * out = nullptr;
  ASSERT_EQ(gimg_raster_create(64, 64, gimg_raster_format(raster->raster), GIMG_RASTER_OWNED, nullptr, 0, &out), GIMG_OK);
  EXPECT_EQ(gshscene_sink_readback(sink, &job, out), GSHSCENE_OK);
  EXPECT_EQ(gshscene_sink_readback(nullptr, &job, out), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_sink_readback(sink, nullptr, out), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_sink_readback(sink, &job, nullptr), GSHSCENE_ERR_INVALID);
  GIMG_Raster * straight = nullptr;
  ASSERT_EQ(gimg_raster_create(64, 64, &GIMG_PIXEL_RGBA8, GIMG_RASTER_OWNED, nullptr, 0, &straight), GIMG_OK);
  EXPECT_EQ(gshscene_sink_readback(sink, &job, straight), GSHSCENE_ERR_INVALID) << "another alpha form is another format";
  GIMG_Raster * taller = nullptr;
  ASSERT_EQ(gimg_raster_create(64, 65, gimg_raster_format(raster->raster), GIMG_RASTER_OWNED, nullptr, 0, &taller), GIMG_OK);
  EXPECT_EQ(gshscene_sink_readback(sink, &job, taller), GSHSCENE_ERR_INVALID);
  GSHSCENE_Sink * other = nullptr;
  ASSERT_EQ(gshscene_dump_sink_create(nullptr, nullptr, nullptr, &other), GSHSCENE_OK);
  EXPECT_EQ(gshscene_sink_readback(other, &job, out), GSHSCENE_ERR_INVALID);
  GSHSCENE_Job idle{};
  EXPECT_EQ(gshscene_sink_readback(sink, &idle, out), GSHSCENE_ERR_INVALID);
  gimg_raster_destroy(straight);
  gimg_raster_destroy(taller);
  gimg_raster_destroy(out);
  EXPECT_EQ(gshscene_sink_destroy(other), GSHSCENE_OK);
  EXPECT_EQ(gshscene_sink_destroy(sink), GSHSCENE_OK);
  // A sink whose caps claim readback and which has no op: unsupported.
  Mock m;
  m.caps = blocking_caps();
  m.caps.flags |= GSHSCENE_CAP_READBACK;
  GSHSCENE_Sink_Ops ops = mock_ops();
  GSHSCENE_Sink * mock = nullptr;
  ASSERT_EQ(gshscene_sink_create(nullptr, nullptr, &ops, &m, &mock), GSHSCENE_OK);
  GSHSCENE_Job j2{};
  ReportRef r2;
  GSHSCENE_Render_Request req2 = request_for(list, *raster, r2.report);
  ASSERT_EQ(gshscene_sink_begin(mock, &req2, &j2), GSHSCENE_OK);
  GIMG_Raster * o2 = nullptr;
  gimg_raster_create(64, 64, gimg_raster_format(raster->raster), GIMG_RASTER_OWNED, nullptr, 0, &o2);
  EXPECT_EQ(gshscene_sink_readback(mock, &j2, o2), GSHSCENE_ERR_UNSUPPORTED);
  gimg_raster_destroy(o2);
  EXPECT_EQ(gshscene_sink_destroy(mock), GSHSCENE_OK);
}

// The one predicate (S6), branch by branch, in its stated order.
TEST(Sink, TheDispositionPredicate) {
  Scene s;
  GSHSCENE_Sink_Caps caps = blocking_caps();
  caps.flags |= GSHSCENE_CAP_CLIP_AA | GSHSCENE_CAP_BLEND_SPACES | GSHSCENE_CAP_GLYPHS_FROM_FONT;
  GSHSCENE_Color_Space srgb{GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_SRGB, 0.0};
  GSHSCENE_Item fill{};
  fill.kind = GSHSCENE_ITEM_FILL;
  fill.resource = s.sq(0, 0, 1);
  GSHSCENE_Item font = fill;
  font.resource = s.font_run();
  GSHSCENE_Item clip = fill;
  clip.kind = GSHSCENE_ITEM_CLIP_PUSH;
  GSHSCENE_Item group{};
  group.kind = GSHSCENE_ITEM_GROUP_PUSH;
  group.group.opacity = 1.0;
  GSHSCENE_Item pop{};
  pop.kind = GSHSCENE_ITEM_GROUP_POP;
  auto d = [&](const GSHSCENE_Item & item) { return gshscene_caps_disposition(&caps, &item, &srgb); };
  EXPECT_EQ(d(fill).entry_class, 0);
  EXPECT_EQ(d(font).entry_class, 0);
  EXPECT_EQ(d(clip).entry_class, 0);
  EXPECT_EQ(d(group).entry_class, 0);
  EXPECT_EQ(d(pop).entry_class, 0);
  EXPECT_EQ(gshscene_caps_disposition(nullptr, &fill, &srgb).entry_class, 0);
  EXPECT_EQ(gshscene_caps_disposition(&caps, nullptr, &srgb).entry_class, 0);
  // Kinds.
  for (uint32_t k = GSHSCENE_ITEM_FILL; k < GSHSCENE_ITEM_KIND_COUNT; k++) {
    GSHSCENE_Sink_Caps c = caps;
    c.item_kinds &= ~(1u << k);
    GSHSCENE_Item item = fill;
    item.kind = (GSHSCENE_Item_Kind)k;
    GSHSCENE_Disposition x = gshscene_caps_disposition(&c, &item, &srgb);
    EXPECT_EQ(x.entry_class, GSHSCENE_ENTRY_REFUSED) << k;
    EXPECT_EQ(x.reason, GSHSCENE_REASON_ITEM_KIND) << k;
    item.kind = (GSHSCENE_Item_Kind)(k == GSHSCENE_ITEM_FILL ? GSHSCENE_ITEM_GROUP_POP : GSHSCENE_ITEM_FILL);
    EXPECT_NE(gshscene_caps_disposition(&c, &item, &srgb).reason, GSHSCENE_REASON_ITEM_KIND) << k;
  }
  GSHSCENE_Item odd = fill;
  odd.kind = (GSHSCENE_Item_Kind)40;
  EXPECT_EQ(d(odd).entry_class, GSHSCENE_ENTRY_REFUSED);
  // Glyphs from a font: fills and clips, not without the flag.
  caps.flags &= ~GSHSCENE_CAP_GLYPHS_FROM_FONT;
  EXPECT_EQ(d(font).reason, GSHSCENE_REASON_GLYPH_SOURCE);
  GSHSCENE_Item font_clip = font;
  font_clip.kind = GSHSCENE_ITEM_CLIP_PUSH;
  EXPECT_EQ(d(font_clip).reason, GSHSCENE_REASON_GLYPH_SOURCE);
  EXPECT_EQ(d(fill).entry_class, 0) << "a path is not a glyph run";
  // Composites.
  for (uint32_t op = 0; op < GSHSCENE_COMPOSITE_COUNT; op++) {
    GSHSCENE_Sink_Caps c = caps;
    c.composites = 0x1fff & ~(1u << op);
    GSHSCENE_Item g = group;
    g.group.composite = op;
    EXPECT_EQ(gshscene_caps_disposition(&c, &g, &srgb).reason, GSHSCENE_REASON_COMPOSITE) << op;
    g.group.composite = (op + 1) % GSHSCENE_COMPOSITE_COUNT;
    EXPECT_EQ(gshscene_caps_disposition(&c, &g, &srgb).entry_class, 0) << op;
  }
  GSHSCENE_Item g40 = group;
  g40.group.composite = 40;
  EXPECT_EQ(d(g40).reason, GSHSCENE_REASON_COMPOSITE);
  // Clips without antialiasing; a refusal comes first.
  caps.flags &= ~GSHSCENE_CAP_CLIP_AA;
  EXPECT_EQ(d(clip).entry_class, GSHSCENE_ENTRY_APPROXIMATED);
  EXPECT_EQ(d(clip).reason, GSHSCENE_REASON_CLIP_ALIASED);
  EXPECT_EQ(d(font_clip).reason, GSHSCENE_REASON_GLYPH_SOURCE);
  EXPECT_EQ(d(fill).entry_class, 0);
  // Blend spaces: a stated space other than the target's, without the flag.
  caps.flags &= ~GSHSCENE_CAP_BLEND_SPACES;
  EXPECT_EQ(d(group).entry_class, 0) << "zero inherits";
  GSHSCENE_Item lin = group;
  lin.group.blend_space = GSHSCENE_Color_Space{GCOL_PRIMARIES_SRGB, GCOL_TRANSFER_LINEAR, 0.0};
  EXPECT_EQ(d(lin).reason, GSHSCENE_REASON_BLEND_SPACE);
  GSHSCENE_Item same = group;
  same.group.blend_space = srgb;
  EXPECT_EQ(d(same).entry_class, 0) << "the target's own space";
  EXPECT_EQ(gshscene_caps_disposition(&caps, &same, nullptr).reason, GSHSCENE_REASON_BLEND_SPACE);
  caps.flags |= GSHSCENE_CAP_BLEND_SPACES;
  EXPECT_EQ(d(lin).entry_class, 0);
}

TEST(Sink, PixelKinds) {
  EXPECT_TRUE(gshscene_target_kind_has_pixels(GSHSCENE_TARGET_RASTER));
  EXPECT_TRUE(gshscene_target_kind_has_pixels(GSHSCENE_TARGET_GPU_TEXTURE));
  EXPECT_TRUE(gshscene_target_kind_has_pixels(GSHSCENE_TARGET_GPU_SURFACE));
  EXPECT_FALSE(gshscene_target_kind_has_pixels(GSHSCENE_TARGET_STREAM));
  EXPECT_FALSE(gshscene_target_kind_has_pixels((GSHSCENE_Target_Kind)0));
  EXPECT_FALSE(gshscene_target_kind_has_pixels(GSHSCENE_TARGET_KIND_COUNT));
  EXPECT_EQ((int)GSHSCENE_TARGET_RASTER, 1);
  EXPECT_EQ((int)GSHSCENE_TARGET_STREAM, 2);
  EXPECT_EQ((int)GSHSCENE_TARGET_GPU_TEXTURE, 3);
  EXPECT_EQ((int)GSHSCENE_TARGET_GPU_SURFACE, 4);
  EXPECT_EQ((int)GSHSCENE_UNIT_PIXEL, 1);
  EXPECT_EQ((int)GSHSCENE_UNIT_MILLIMETER, 2);
  EXPECT_EQ((int)GSHSCENE_UNIT_POINT, 3);
  EXPECT_EQ((int)GSHSCENE_UNIT_INCH, 4);
  EXPECT_EQ((int)GSHSCENE_ENCODING_HARDWARE_SRGB, 1);
  EXPECT_EQ(GSHSCENE_TARGET_SEAMLESS, 1u);
  EXPECT_EQ(GSHSCENE_RENDER_STRICT, 1u);
  EXPECT_EQ(GSHSCENE_RENDER_CALLER_MEMORY, 2u);
  EXPECT_EQ(GSHSCENE_CAP_FLAGS_ALL, 0xffu);
  EXPECT_EQ((int)GSHSCENE_CANCEL_BETWEEN_UNITS, 1);
  EXPECT_EQ((int)GSHSCENE_CANCEL_BEFORE_SUBMIT, 2);
}

// The expansion walk: every unit in paint order with its chain, pictures in pictures, an empty picture.
TEST(Sink, TheExpansionWalk) {
  Scene s;
  GSHSCENE_Paint red = solid(1, 0, 0);
  GSHSCENE_List * empty = s.open();
  gshscene_list_close(empty);
  const GSHSCENE_Resource * nothing = nullptr;
  gshscene_store_intern_picture(s.store, empty, &nothing);
  s.held.push_back(nothing);
  GSHSCENE_List * inner = s.open();
  gshscene_list_fill(inner, s.sq(0, 0, 1), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0);
  gshscene_list_instance(inner, nothing, nullptr, 0, 0.0);
  gshscene_list_close(inner);
  const GSHSCENE_Resource * pin = nullptr;
  gshscene_store_intern_picture(s.store, inner, &pin);
  s.held.push_back(pin);
  GSHSCENE_List * outer = s.open();
  gshscene_list_instance(outer, pin, nullptr, 0, 0.0);
  gshscene_list_fill(outer, s.sq(0, 0, 2), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0);
  gshscene_list_close(outer);
  const GSHSCENE_Resource * pout = nullptr;
  gshscene_store_intern_picture(s.store, outer, &pout);
  s.held.push_back(pout);
  GSHSCENE_List * l = s.open();
  gshscene_list_instance(l, pout, nullptr, 0, 0.0);
  gshscene_list_instance(l, nothing, nullptr, 0, 0.0);
  gshscene_list_fill(l, s.sq(0, 0, 3), nullptr, GSHGEOM_FILL_NONZERO, &red, 0, 0.0);
  gshscene_list_close(l);
  size_t bytes = gshscene_expansion_bytes(l);
  ASSERT_GT(bytes, 0u);
  EXPECT_EQ(gshscene_expansion_bytes(nullptr), 0u);
  std::vector<unsigned char> memory(bytes);
  GSHSCENE_Expansion walk;
  EXPECT_EQ(gshscene_expansion_init(&walk, l, memory.data(), bytes - 1), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_expansion_init(nullptr, l, memory.data(), bytes), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_expansion_init(&walk, nullptr, memory.data(), bytes), GSHSCENE_ERR_INVALID);
  EXPECT_EQ(gshscene_expansion_init(&walk, l, nullptr, bytes), GSHSCENE_ERR_INVALID);
  GSHSCENE_List * open = s.open();
  EXPECT_EQ(gshscene_expansion_init(&walk, open, memory.data(), bytes), GSHSCENE_ERR_INVALID);
  ASSERT_EQ(gshscene_expansion_init(&walk, l, memory.data(), bytes), GSHSCENE_OK);
  std::vector<std::vector<size_t>> chains;
  std::vector<int> kinds;
  const GSHSCENE_Item * item;
  const size_t * chain;
  size_t count;
  EXPECT_EQ(gshscene_expansion_next(&walk, nullptr, &chain, &count), 0);
  EXPECT_EQ(gshscene_expansion_next(nullptr, &item, &chain, &count), 0);
  while (gshscene_expansion_next(&walk, &item, &chain, &count)) {
    chains.emplace_back(chain, chain + count);
    kinds.push_back(item->kind);
  }
  std::vector<std::vector<size_t>> want = {{0}, {0, 0}, {0, 0, 0}, {0, 0, 1}, {0, 1}, {1}, {2}};
  EXPECT_EQ(chains, want);
  EXPECT_EQ(kinds, (std::vector<int>{6, 6, 1, 6, 1, 6, 1}));
  EXPECT_EQ(chains.size(), gshscene_list_expansion(l));
  EXPECT_EQ(gshscene_expansion_next(&walk, &item, &chain, &count), 0) << "a finished walk stays finished";
}

// The busy guard is per surface: two surfaces in flight on one sink are fine; the slot is given back at completion.
TEST(Sink, TheBusyGuardIsPerSurface) {
  GSHSCENE_Limits limits{};
  limits.max_jobs = 2;
  MockSink ms(false, &limits);
  ms.mock.begin = Mock::LATER;
  Scene s;
  const GSHSCENE_List * list = basic(s);
  auto a = make_stream();
  auto b = make_stream();
  ReportRef ra;
  ReportRef rb;
  ReportRef rc;
  GSHSCENE_Render_Request qa = request_for(list, *a, ra.report);
  GSHSCENE_Render_Request qb = request_for(list, *b, rb.report);
  GSHSCENE_Render_Request qc = request_for(list, *a, rc.report);
  GSHSCENE_Job ja{};
  GSHSCENE_Job jb{};
  GSHSCENE_Job jc{};
  ASSERT_EQ(gshscene_sink_begin(ms.sink, &qa, &ja), GSHSCENE_OK);
  EXPECT_EQ(gshscene_sink_begin(ms.sink, &qc, &jc), GSHSCENE_ERR_INVALID) << "a's surface is busy";
  GSHSCENE_Job_Status st{};
  gshscene_job_poll(&jc, &st);
  EXPECT_EQ(st.state, GSHSCENE_JOB_IDLE);
  EXPECT_EQ(gshscene_report_reset(rc.report), GSHSCENE_OK) << "a refused begin leaves the report free";
  ASSERT_EQ(gshscene_sink_begin(ms.sink, &qb, &jb), GSHSCENE_OK);
  EXPECT_EQ(gshscene_sink_destroy(ms.sink), GSHSCENE_ERR_INVALID) << "jobs in flight";
  gshscene_job_complete(&ja, GSHSCENE_OK, nullptr, 0, nullptr);
  ASSERT_EQ(gshscene_sink_begin(ms.sink, &qc, &jc), GSHSCENE_OK) << "a's surface was given back";
  gshscene_job_complete(&jb, GSHSCENE_OK, nullptr, 0, nullptr);
  ms.mock.pending = nullptr;
  gshscene_job_complete(&jc, GSHSCENE_OK, nullptr, 0, nullptr);
}

// The bounds of the predicate's shifts: a kind or an operator of 32 is refused, never shifted.
TEST(Sink, TheShiftBoundsOfThePredicate) {
  Scene s;
  GSHSCENE_Sink_Caps caps = blocking_caps();
  caps.item_kinds = 0xffffffffu;
  caps.composites = 0xffffffffu;
  GSHSCENE_Item item{};
  item.kind = (GSHSCENE_Item_Kind)32;
  item.resource = s.sq(0, 0, 1);
  EXPECT_EQ(gshscene_caps_disposition(&caps, &item, nullptr).reason, GSHSCENE_REASON_ITEM_KIND);
  item.kind = (GSHSCENE_Item_Kind)31;
  EXPECT_EQ(gshscene_caps_disposition(&caps, &item, nullptr).entry_class, 0);
  GSHSCENE_Item g{};
  g.kind = GSHSCENE_ITEM_GROUP_PUSH;
  g.group.opacity = 1.0;
  g.group.composite = 32;
  EXPECT_EQ(gshscene_caps_disposition(&caps, &g, nullptr).reason, GSHSCENE_REASON_COMPOSITE);
  g.group.composite = 31;
  EXPECT_EQ(gshscene_caps_disposition(&caps, &g, nullptr).entry_class, 0);
}

// A map is the identity only when every field is zero; explicit extents at the surface's edges are in it.
TEST(Sink, TheZeroMapAndExplicitExtents) {
  MockSink ms;
  auto raster = make_raster();
  GSHSCENE_Sink_Caps caps{};
  GSHSCENE_Target t = raster->target;
  t.map.kind = 1; // dim 0 with a kind: neither zero nor valid
  EXPECT_EQ(gshscene_sink_caps(ms.sink, &t, &caps), GSHSCENE_ERR_INVALID);
  t = raster->target;
  t.map.t[1] = 1.0;
  EXPECT_EQ(gshscene_sink_caps(ms.sink, &t, &caps), GSHSCENE_ERR_INVALID);
  t = raster->target;
  t.map.m[0][1] = 1.0;
  EXPECT_EQ(gshscene_sink_caps(ms.sink, &t, &caps), GSHSCENE_ERR_INVALID);
  for (GSHSCENE_Rect e : {GSHSCENE_Rect{0, 0, 64, 64}, GSHSCENE_Rect{63, 63, 64, 64}, GSHSCENE_Rect{0, 5, 1, 6}}) {
    t = raster->target;
    t.extent = e;
    EXPECT_EQ(gshscene_sink_caps(ms.sink, &t, &caps), GSHSCENE_OK) << e.x0 << " " << e.y0 << " " << e.x1 << " " << e.y1;
  }
  for (GSHSCENE_Rect e : {GSHSCENE_Rect{8, 8, 9, 8}, GSHSCENE_Rect{8, 8, 9, 7}, GSHSCENE_Rect{0, -1, 4, 4},
           GSHSCENE_Rect{0, 0, 4, 65}, GSHSCENE_Rect{9, 0, 8, 4}}) {
    t = raster->target;
    t.extent = e;
    EXPECT_EQ(gshscene_sink_caps(ms.sink, &t, &caps), GSHSCENE_ERR_INVALID) << e.x0 << " " << e.y0 << " " << e.x1 << " " << e.y1;
  }
}

// Every failure the contract layer decides says why, in the same words each time.
TEST(Sink, EveryFailureOfTheContractSaysWhyInWords) {
  Scene s;
  const GSHSCENE_List * list = basic(s);
  auto reason = [](const ReportRef & r) { return std::string(summary(r.report).failure.reason); };
  {
    MockSink ms;
    auto surface = make_stream();
    ReportRef report;
    GSHSCENE_Render_Request req = request_for(list, *surface, report.report);
    ms.mock.begin = Mock::FAIL;
    ms.mock.begin_result = GSHSCENE_ERR_OOM;
    run(nullptr, ms.sink, req);
    EXPECT_EQ(reason(report), "the sink refused to begin");
    ms.mock.begin = Mock::LATER;
    ms.mock.wait_completes = false;
    ms.mock.wait_result = GSHSCENE_ERR_IO;
    EXPECT_EQ(gshscene_sink_render(ms.sink, &req), GSHSCENE_ERR_IO);
    ms.mock.pending = nullptr;
    EXPECT_EQ(reason(report), "the wait failed");
    ms.mock.begin = Mock::COMPLETE;
    ms.mock.begin_result = GSHSCENE_OK;
    ms.mock.workspace_result = GSHSCENE_ERR_OOM;
    req.flags = GSHSCENE_RENDER_CALLER_MEMORY;
    std::vector<unsigned char> ws(8);
    req.workspace = ws.data();
    req.workspace_bytes = ws.size();
    EXPECT_EQ(gshscene_sink_render(ms.sink, &req), GSHSCENE_ERR_OOM);
    EXPECT_EQ(reason(report), "the workspace could not be sized");
    ms.mock.workspace_result = GSHSCENE_OK;
    EXPECT_EQ(gshscene_sink_render(ms.sink, &req), GSHSCENE_ERR_LIMIT);
    EXPECT_EQ(reason(report), "the workspace is smaller than the render needs");
    ms.mock.caps.flags &= ~GSHSCENE_CAP_CALLER_MEMORY;
    EXPECT_EQ(gshscene_sink_render(ms.sink, &req), GSHSCENE_ERR_REFUSED);
    EXPECT_EQ(reason(report), "the sink cannot take its memory from a workspace");
    req.flags = 0;
    surface->target.flags = GSHSCENE_TARGET_SEAMLESS;
    EXPECT_EQ(gshscene_sink_render(ms.sink, &req), GSHSCENE_ERR_REFUSED);
    EXPECT_EQ(reason(report), "the conflation flag is reserved (AD-29)");
    surface->target.flags = 0;
    GSHSCENE_Cancel cancel;
    gshscene_cancel_init(&cancel);
    gshscene_cancel_set(&cancel);
    req.cancel = &cancel;
    EXPECT_EQ(gshscene_sink_render(ms.sink, &req), GSHSCENE_ERR_CANCELLED);
    EXPECT_EQ(reason(report), "cancelled before the first unit");
    req.cancel = nullptr;
    EXPECT_EQ(gshscene_sink_render(ms.sink, &req), GSHSCENE_OK);
    EXPECT_EQ(reason(report), "");
  }
  {
    GSHSCENE_Dump_Sink_Config c;
    gshscene_dump_sink_config_default(&c);
    c.stream_caps.flags &= ~GSHSCENE_CAP_GLYPHS_FROM_FONT;
    GSHSCENE_Sink * sink = nullptr;
    ASSERT_EQ(gshscene_dump_sink_create(nullptr, nullptr, &c, &sink), GSHSCENE_OK);
    GSHSCENE_Reason why;
    const GSHSCENE_List * refused = refusable(s, c.stream_caps, &why);
    auto surface = make_stream();
    ReportRef report;
    GSHSCENE_Render_Request req = request_for(refused, *surface, report.report);
    req.flags = GSHSCENE_RENDER_STRICT;
    EXPECT_EQ(gshscene_sink_render(sink, &req), GSHSCENE_ERR_REFUSED);
    EXPECT_EQ(reason(report), "strict: the plan holds a refusal");
    // The planning pass failing: its first allocation (the walk's memory) refused.
    tracker::Tracker t;
    GSHSCENE_Allocator a = tracker::allocator_for(&t);
    ReportRef fresh(&a);
    req.report = fresh.report;
    req.flags = 0;
    t.fail_from = t.calls;
    EXPECT_EQ(gshscene_sink_render(sink, &req), GSHSCENE_ERR_OOM);
    t.fail_from = -1;
    EXPECT_EQ(reason(fresh), "the planning pass failed");
    GSHSCENE_Limits limits{};
    limits.max_report_entries = 1;
    ReportRef small(nullptr, &limits);
    req.list = list;
    req.report = small.report;
    EXPECT_EQ(gshscene_sink_render(sink, &req), GSHSCENE_ERR_LIMIT);
    EXPECT_EQ(reason(small), "the report's entry cap was passed");
    EXPECT_EQ(gshscene_sink_destroy(sink), GSHSCENE_OK);
  }
}

int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(SinkConformance);
