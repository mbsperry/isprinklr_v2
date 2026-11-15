// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

// The log lines a run produces, driven through the real Sequencer.

#include <unity.h>

#include <string>
#include <vector>

#include "run_log.h"
#include "sequencer.h"

using logging::Level;
using seq::Origin;
using seq::Sequencer;

namespace {

constexpr uint32_t kMin = 60000;

struct FakePort : seq::ZonePort {
  int failStarts = 0;
  int failStops = 0;
  bool start(int, int) override {
    if (failStarts > 0) {
      --failStarts;
      return false;
    }
    return true;
  }
  bool stop(int) override {
    if (failStops > 0) {
      --failStops;
      return false;
    }
    return true;
  }
};

struct Recorder : seq::EventListener {
  Sequencer* s = nullptr;
  std::vector<run_log::Line> lines;
  void onEvent(const seq::Event& e) override {
    lines.push_back(run_log::describe(e, s->runName(), s->entries()));
  }
  // Lines at info or worse, as "level: message".
  std::vector<std::string> visible() const {
    std::vector<std::string> out;
    for (const auto& l : lines) {
      if (l.level != Level::kDebug) {
        out.push_back(std::string(logging::toString(l.level)) + ": " + l.message);
      }
    }
    return out;
  }
};

struct Rig {
  FakePort port;
  Recorder rec;
  Sequencer s;
  Rig() : s(port, timing()) {
    rec.s = &s;
    s.setListener(&rec);
  }
  static seq::Timing timing() {
    seq::Timing t;
    t.padMs = 5000;
    t.retryMs = 1000;
    t.maxAttempts = 3;
    return t;
  }
};

std::vector<seq::Entry> run(std::initializer_list<std::pair<int, int>> zones) {
  std::vector<seq::Entry> out;
  for (const auto& z : zones) out.push_back(seq::Entry{z.first, z.second, Origin::kSchedule});
  return out;
}

void assertLines(const std::vector<std::string>& got,
                 std::initializer_list<const char*> want) {
  std::string all;
  for (const auto& g : got) all += "\n  " + g;
  TEST_ASSERT_EQUAL_UINT_MESSAGE(want.size(), got.size(), all.c_str());
  std::size_t i = 0;
  for (const char* w : want) {
    TEST_ASSERT_EQUAL_STRING_MESSAGE(w, got[i++].c_str(), all.c_str());
  }
}

settings::Settings settingsWith(bool enabled, const char* active) {
  settings::Settings s;
  s.zones.push_back({1, "A"});
  settings::Schedule sch{"Fall", {}};
  settings::DayPattern all;
  all.kind = settings::DayPattern::Kind::kAll;
  sch.items.push_back({1, all, 10});
  s.schedules.push_back(sch);
  s.activeSchedule = active;
  s.enabled = enabled;
  s.config.startHour = 7;
  s.config.startMinute = 0;
  return s;
}

timekeeping::LocalTime at(int d, int h, int mi) {
  return timekeeping::LocalTime{2026, 11, d, 0, h, mi, 0, false, -480};
}

std::vector<std::string> show(const std::vector<run_log::Line>& lines) {
  std::vector<std::string> out;
  for (const auto& l : lines) {
    out.push_back(std::string(logging::toString(l.level)) + ": " + l.message);
  }
  return out;
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_format_duration() {
  TEST_ASSERT_EQUAL_STRING("0 s", run_log::formatDuration(400).c_str());
  TEST_ASSERT_EQUAL_STRING("35 s", run_log::formatDuration(35000).c_str());
  TEST_ASSERT_EQUAL_STRING("10 min", run_log::formatDuration(10 * kMin).c_str());
  TEST_ASSERT_EQUAL_STRING("4 min 12 s", run_log::formatDuration(252000).c_str());
}

void test_scheduled_run_that_finishes() {
  Rig r;
  r.s.addRun("Fall", run({{1, 10}, {3, 5}}));
  r.s.tick(0, 0);
  r.s.tick(10 * kMin, 0);
  r.s.tick(10 * kMin + 5000, 0);
  r.s.tick(15 * kMin + 5000, 0);
  assertLines(r.rec.visible(),
              {"info: queued 2 zones (scheduled, 'Fall'); starting now",
               "info: zone 1 started for 10 min (scheduled, 'Fall', 1 of 2)",
               "info: zone 1 finished: ran its full 10 min (scheduled, 'Fall')",
               "info: zone 3 started for 5 min (scheduled, 'Fall', 2 of 2)",
               "info: zone 3 finished: ran its full 5 min (scheduled, 'Fall')",
               "info: run 'Fall' finished: 2 completed"});
}

void test_manual_zone_appended_to_a_scheduled_run() {
  Rig r;
  r.s.addRun("Fall", run({{1, 10}}));
  r.s.tick(0, 0);
  r.s.add(7, 2, Origin::kManual);
  r.s.tick(10 * kMin, 0);
  r.s.tick(10 * kMin + 5000, 0);
  r.s.tick(12 * kMin + 5000, 0);
  assertLines(r.rec.visible(),
              {"info: queued zone 1 for 10 min (scheduled, 'Fall'); starting now",
               "info: zone 1 started for 10 min (scheduled, 'Fall', 1 of 1)",
               "info: queued zone 7 for 2 min (manual); 1 zone ahead",
               "info: zone 1 finished: ran its full 10 min (scheduled, 'Fall')",
               "info: zone 7 started for 2 min (manual, 2 of 2)",
               "info: zone 7 finished: ran its full 2 min (manual)",
               "info: run 'Fall' finished: 2 completed"});
}

void test_skip_cuts_a_zone_short() {
  Rig r;
  r.s.addRun("Fall", run({{1, 10}, {3, 5}}));
  r.s.tick(0, 0);
  r.s.skip(4 * kMin + 12000);
  // During the pad, skip drops the next zone before it starts.
  r.s.skip(4 * kMin + 13000);
  assertLines(r.rec.visible(),
              {"info: queued 2 zones (scheduled, 'Fall'); starting now",
               "info: zone 1 started for 10 min (scheduled, 'Fall', 1 of 2)",
               "info: zone 1 cut short by skip after 4 min 12 s of 10 min (scheduled, 'Fall')",
               "info: zone 3 skipped before it started (scheduled, 'Fall')",
               "info: run 'Fall' finished: 0 completed, 2 skipped"});
}

void test_cancel_reports_what_was_cut_and_dropped() {
  Rig r;
  r.s.add(2, 3, Origin::kManual);
  r.s.add(4, 3, Origin::kManual);
  r.s.add(6, 3, Origin::kManual);
  r.s.tick(0, 0);
  r.s.tick(3 * kMin, 0);
  r.s.tick(3 * kMin + 5000, 0);
  r.s.cancel(3 * kMin + 5000 + 35000);
  assertLines(r.rec.visible(),
              {"info: queued zone 2 for 3 min (manual); starting now",
               "info: queued zone 4 for 3 min (manual); 1 zone ahead",
               "info: queued zone 6 for 3 min (manual); 2 zones ahead",
               "info: zone 2 started for 3 min (manual, 1 of 3)",
               "info: zone 2 finished: ran its full 3 min (manual)",
               "info: zone 4 started for 3 min (manual, 2 of 3)",
               "info: zone 4 cut short by cancel after 35 s of 3 min (manual)",
               "info: manual run cancelled: 1 completed, 1 cut short, 1 not started"});
  // Cancelling again with nothing running says nothing more.
  const std::size_t before = r.rec.lines.size();
  r.s.cancel(4 * kMin);
  TEST_ASSERT_EQUAL_UINT(before, r.rec.lines.size());
}

void test_cancel_during_pad_drops_without_cutting() {
  Rig r;
  r.s.addRun("Fall", run({{1, 1}, {3, 5}}));
  r.s.tick(0, 0);
  r.s.tick(kMin, 0);
  r.s.cancel(kMin + 1000);
  const auto lines = r.rec.visible();
  TEST_ASSERT_EQUAL_STRING("info: run 'Fall' cancelled: 1 completed, 1 not started",
                           lines.back().c_str());
}

void test_bus_failure_is_an_error() {
  Rig r;
  r.port.failStarts = 10;
  r.s.addRun("Fall", run({{1, 10}, {3, 5}}));
  r.s.tick(0, 0);
  r.s.tick(1000, 0);
  r.s.tick(2000, 0);
  assertLines(r.rec.visible(),
              {"info: queued 2 zones (scheduled, 'Fall'); starting now",
               "error: zone 1 not started: the bus refused it 3 times; abandoning the run (scheduled, 'Fall')",
               "error: run 'Fall' abandoned: 0 completed, 1 failed, 1 not started"});
  // The retries are there at debug level.
  TEST_ASSERT_EQUAL_UINT(5, r.rec.lines.size());
  TEST_ASSERT_EQUAL(Level::kDebug, r.rec.lines[1].level);
  TEST_ASSERT_EQUAL_STRING("bus busy; zone 1 start attempt 1 not taken, retrying",
                           r.rec.lines[1].message.c_str());
}

void test_retried_start_says_so() {
  Rig r;
  r.port.failStarts = 1;
  r.s.add(5, 4, Origin::kManual);
  r.s.tick(0, 0);
  r.s.tick(1000, 0);
  TEST_ASSERT_EQUAL_STRING("info: zone 5 started for 4 min (manual, 1 of 1) after 2 attempts",
                           r.rec.visible().back().c_str());
}

void test_abandoned_stop_is_a_warning() {
  Rig r;
  r.port.failStops = 10;
  r.s.add(5, 4, Origin::kManual);
  r.s.tick(0, 0);
  r.s.cancel(1000);
  r.s.tick(2000, 0);
  r.s.tick(3000, 0);
  TEST_ASSERT_EQUAL_STRING(
      "warn: stop for zone 5 not taken by the bus after 3 attempts; the Pro-C ends it on its own timer",
      r.rec.visible().back().c_str());
}

void test_full_queue_is_refused_with_a_warning() {
  Rig r;
  std::vector<seq::Entry> big;
  for (std::size_t i = 0; i < seq::kMaxRunEntries; ++i) big.push_back({1, 1, Origin::kSchedule});
  TEST_ASSERT_TRUE(r.s.addRun("Big", big));
  TEST_ASSERT_FALSE(r.s.add(2, 1, Origin::kManual));
  TEST_ASSERT_FALSE(r.s.addRun("More", run({{1, 1}, {2, 1}}), Origin::kManual));
  const auto lines = r.rec.visible();
  TEST_ASSERT_EQUAL_STRING("warn: refused to queue 1 zone: the run is limited to 64 entries",
                           lines[1].c_str());
  TEST_ASSERT_EQUAL_STRING("warn: refused to queue 2 zones: the run is limited to 64 entries",
                           lines[2].c_str());
}

void test_run_now_of_a_saved_schedule_is_manual() {
  Rig r;
  r.s.addRun("Catch up", run({{1, 10}, {2, 10}}), Origin::kManual);
  r.s.tick(0, 0);
  assertLines(r.rec.visible(),
              {"info: queued 2 zones (manual, 'Catch up'); starting now",
               "info: zone 1 started for 10 min (manual, 1 of 2)"});
}

void test_trigger_reports_scheduler_off_once_a_day_at_start_time() {
  run_log::TriggerReporter t;
  const auto off = settingsWith(false, "Fall");
  using D = trigger::Decision;
  // First observation: a debug line for the decision, nothing else.
  auto lines = show(t.observe(D::kDisabled, true, at(15, 6, 59), off));
  TEST_ASSERT_EQUAL_UINT(1, lines.size());
  TEST_ASSERT_EQUAL_STRING("debug: trigger: disabled", lines[0].c_str());
  // At 07:00 it says why nothing started, once.
  lines = show(t.observe(D::kDisabled, true, at(15, 7, 0), off));
  TEST_ASSERT_EQUAL_UINT(1, lines.size());
  TEST_ASSERT_EQUAL_STRING("info: scheduled run at 07:00 not started: the scheduler is off",
                           lines[0].c_str());
  TEST_ASSERT_EQUAL_UINT(0, t.observe(D::kDisabled, true, at(15, 7, 1), off).size());
  TEST_ASSERT_EQUAL_UINT(0, t.observe(D::kDisabled, true, at(15, 8, 30), off).size());
  // Next day, again once.
  TEST_ASSERT_EQUAL_UINT(1, t.observe(D::kDisabled, true, at(16, 7, 20), off).size());
  TEST_ASSERT_EQUAL_UINT(0, t.observe(D::kDisabled, true, at(16, 7, 21), off).size());
}

void test_trigger_reports_missing_schedule_and_missed_window() {
  run_log::TriggerReporter t;
  using D = trigger::Decision;
  const auto none = settingsWith(true, "");
  t.observe(D::kNoActiveSchedule, true, at(15, 6, 0), none);
  auto lines = show(t.observe(D::kNoActiveSchedule, true, at(15, 7, 5), none));
  TEST_ASSERT_EQUAL_STRING("warn: scheduled run at 07:00 not started: no active schedule",
                           lines.at(0).c_str());

  const auto on = settingsWith(true, "Fall");
  lines = show(t.observe(D::kWindowMissed, true, at(16, 9, 0), on));
  TEST_ASSERT_EQUAL_UINT(1, lines.size());
  TEST_ASSERT_EQUAL_STRING(
      "warn: today's scheduled run did not start: its window (07:00 to 08:00) has passed",
      lines[0].c_str());
  // Only on the change.
  TEST_ASSERT_EQUAL_UINT(0, t.observe(D::kWindowMissed, true, at(16, 9, 1), on).size());
  // An ordinary due/already-ran day only produces debug lines.
  for (const auto& l : t.observe(D::kAlreadyRan, true, at(17, 7, 30), on)) {
    TEST_ASSERT_EQUAL(Level::kDebug, l.level);
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_format_duration);
  RUN_TEST(test_scheduled_run_that_finishes);
  RUN_TEST(test_manual_zone_appended_to_a_scheduled_run);
  RUN_TEST(test_skip_cuts_a_zone_short);
  RUN_TEST(test_cancel_reports_what_was_cut_and_dropped);
  RUN_TEST(test_cancel_during_pad_drops_without_cutting);
  RUN_TEST(test_bus_failure_is_an_error);
  RUN_TEST(test_retried_start_says_so);
  RUN_TEST(test_abandoned_stop_is_a_warning);
  RUN_TEST(test_full_queue_is_refused_with_a_warning);
  RUN_TEST(test_run_now_of_a_saved_schedule_is_manual);
  RUN_TEST(test_trigger_reports_scheduler_off_once_a_day_at_start_time);
  RUN_TEST(test_trigger_reports_missing_schedule_and_missed_window);
  return UNITY_END();
}
