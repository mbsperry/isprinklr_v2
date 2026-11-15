// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#include <unity.h>

#include <string>
#include <vector>

#include "sequencer.h"

using seq::Origin;
using seq::Outcome;
using seq::Sequencer;
using State = seq::Sequencer::State;

namespace {

constexpr uint32_t kMin = 60000;

struct FakePort : seq::ZonePort {
  std::vector<std::string> log;
  int failStarts = 0;  // next N starts report busy
  int failStops = 0;

  bool start(int zone, int minutes) override {
    if (failStarts > 0) {
      --failStarts;
      log.push_back("busy-start " + std::to_string(zone));
      return false;
    }
    log.push_back("start " + std::to_string(zone) + " " + std::to_string(minutes));
    return true;
  }
  bool stop(int zone) override {
    if (failStops > 0) {
      --failStops;
      log.push_back("busy-stop " + std::to_string(zone));
      return false;
    }
    log.push_back("stop " + std::to_string(zone));
    return true;
  }
};

seq::Timing timing() {
  seq::Timing t;
  t.padMs = 5000;
  t.retryMs = 1000;
  t.maxAttempts = 3;
  return t;
}

std::vector<seq::Entry> run(std::initializer_list<std::pair<int, int>> zones) {
  std::vector<seq::Entry> out;
  for (const auto& z : zones) out.push_back(seq::Entry{z.first, z.second, Origin::kSchedule});
  return out;
}

void assertLog(const FakePort& p, std::initializer_list<const char*> want) {
  TEST_ASSERT_EQUAL_UINT(want.size(), p.log.size());
  std::size_t i = 0;
  for (const char* w : want) {
    TEST_ASSERT_EQUAL_STRING(w, p.log[i++].c_str());
  }
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_idle_does_nothing() {
  FakePort p;
  Sequencer s(p, timing());
  s.tick(0, 0);
  TEST_ASSERT_TRUE(s.idle());
  TEST_ASSERT_EQUAL_UINT(0, p.log.size());
}

void test_runs_zones_in_order_with_stop_pad_start() {
  FakePort p;
  Sequencer s(p, timing());
  s.addRun("Fall", run({{3, 2}, {1, 1}}));
  s.tick(0, 1000);
  TEST_ASSERT_EQUAL(State::kRunning, s.state());
  TEST_ASSERT_EQUAL_UINT32(2 * kMin, s.currentRemainingMs(0));

  s.tick(2 * kMin - 1, 0);
  TEST_ASSERT_EQUAL(State::kRunning, s.state());
  s.tick(2 * kMin, 0);
  TEST_ASSERT_EQUAL(State::kPadding, s.state());
  s.tick(2 * kMin + 4999, 0);
  TEST_ASSERT_EQUAL(State::kPadding, s.state());
  s.tick(2 * kMin + 5000, 0);
  TEST_ASSERT_EQUAL(State::kRunning, s.state());
  s.tick(2 * kMin + 5000 + kMin, 0);
  TEST_ASSERT_TRUE(s.idle());

  assertLog(p, {"start 3 2", "stop 3", "start 1 1", "stop 1"});
  TEST_ASSERT_EQUAL(Outcome::kCompleted, s.entries()[0].outcome);
  TEST_ASSERT_EQUAL(Outcome::kCompleted, s.entries()[1].outcome);
  TEST_ASSERT_EQUAL_UINT32(1000, s.entries()[0].startedEpoch);
  TEST_ASSERT_EQUAL_STRING("Fall", s.runName().c_str());
}

void test_manual_start_appends_to_running_schedule() {
  FakePort p;
  Sequencer s(p, timing());
  s.addRun("Fall", run({{3, 1}}));
  s.tick(0, 0);
  TEST_ASSERT_TRUE(s.add(7, 2, Origin::kManual));
  TEST_ASSERT_EQUAL(3, s.entries()[*s.currentIndex()].zone);  // not preempted
  s.tick(kMin, 0);
  s.tick(kMin + 5000, 0);
  TEST_ASSERT_EQUAL(7, s.entries()[*s.currentIndex()].zone);
  TEST_ASSERT_EQUAL(Origin::kManual, s.entries()[1].origin);
  assertLog(p, {"start 3 1", "stop 3", "start 7 2"});
}

void test_not_idle_once_something_is_queued() {
  // Before the first tick the state is still kIdle, but the queue is not
  // empty; OTA must not see this as idle.
  FakePort p;
  Sequencer s(p, timing());
  s.add(5, 3, Origin::kManual);
  TEST_ASSERT_FALSE(s.idle());
  s.cancel(0);
  TEST_ASSERT_TRUE(s.idle());
}

void test_not_idle_while_a_stop_is_retried() {
  FakePort p;
  p.failStops = 1;
  Sequencer s(p, timing());
  s.stopZone(4, 0);
  TEST_ASSERT_FALSE(s.idle());
  s.tick(1000, 0);
  TEST_ASSERT_TRUE(s.idle());
}

void test_manual_start_when_idle_runs_immediately() {
  FakePort p;
  Sequencer s(p, timing());
  s.add(5, 3, Origin::kManual);
  s.tick(10, 0);
  assertLog(p, {"start 5 3"});
  TEST_ASSERT_TRUE(s.runName().empty());
}

void test_cancel_stops_current_and_interrupts_rest() {
  FakePort p;
  Sequencer s(p, timing());
  s.addRun("Fall", run({{1, 5}, {2, 5}, {3, 5}}));
  s.tick(0, 0);
  s.cancel(1000);
  TEST_ASSERT_TRUE(s.idle());
  assertLog(p, {"start 1 5", "stop 1"});
  for (const auto& e : s.entries()) TEST_ASSERT_EQUAL(Outcome::kInterrupted, e.outcome);
  s.tick(10 * kMin, 0);
  TEST_ASSERT_EQUAL_UINT(2, p.log.size());  // nothing restarts
}

void test_cancel_during_padding_sends_no_extra_stop() {
  FakePort p;
  Sequencer s(p, timing());
  s.addRun("Fall", run({{1, 1}, {2, 1}}));
  s.tick(0, 0);
  s.tick(kMin, 0);  // stop 1, padding
  s.cancel(kMin + 100);
  assertLog(p, {"start 1 1", "stop 1"});
  TEST_ASSERT_EQUAL(Outcome::kCompleted, s.entries()[0].outcome);
  TEST_ASSERT_EQUAL(Outcome::kInterrupted, s.entries()[1].outcome);
}

void test_cancel_when_idle_is_harmless() {
  FakePort p;
  Sequencer s(p, timing());
  s.cancel(0);
  TEST_ASSERT_TRUE(s.idle());
  TEST_ASSERT_EQUAL_UINT(0, p.log.size());
}

void test_new_entries_after_a_finished_run_start_a_new_run() {
  FakePort p;
  Sequencer s(p, timing());
  s.addRun("Fall", run({{1, 1}}));
  s.tick(0, 0);
  s.tick(kMin, 0);
  TEST_ASSERT_TRUE(s.idle());
  TEST_ASSERT_EQUAL_UINT(1, s.entries().size());  // last run still visible
  s.add(2, 1, Origin::kManual);
  TEST_ASSERT_EQUAL_UINT(1, s.entries().size());
  TEST_ASSERT_TRUE(s.runName().empty());
}

void test_busy_bus_start_is_retried() {
  FakePort p;
  p.failStarts = 2;
  Sequencer s(p, timing());
  s.add(4, 1, Origin::kManual);
  s.tick(0, 0);
  s.tick(500, 0);   // too soon to retry
  s.tick(1000, 0);
  TEST_ASSERT_EQUAL(State::kStarting, s.state());
  s.tick(2000, 0);
  TEST_ASSERT_EQUAL(State::kRunning, s.state());
  assertLog(p, {"busy-start 4", "busy-start 4", "start 4 1"});
}

void test_start_that_never_succeeds_abandons_run() {
  FakePort p;
  p.failStarts = 100;
  Sequencer s(p, timing());
  s.addRun("Fall", run({{1, 1}, {2, 1}}));
  for (uint32_t t = 0; t <= 5000; t += 100) s.tick(t, 0);
  TEST_ASSERT_TRUE(s.idle());
  TEST_ASSERT_EQUAL(Outcome::kFailed, s.entries()[0].outcome);
  TEST_ASSERT_EQUAL(Outcome::kInterrupted, s.entries()[1].outcome);
  TEST_ASSERT_EQUAL_UINT(3, p.log.size());  // three attempts, then nothing
}

void test_next_start_waits_for_a_retried_stop() {
  FakePort p;
  Sequencer s(p, timing());
  s.addRun("Fall", run({{1, 1}, {2, 1}}));
  s.tick(0, 0);
  p.failStops = 2;
  s.tick(kMin, 0);              // busy-stop 1
  s.tick(kMin + 1000, 0);       // busy-stop 1
  s.tick(kMin + 2000, 0);       // stop 1
  s.tick(kMin + 5000, 0);       // pad elapsed -> start 2
  assertLog(p, {"start 1 1", "busy-stop 1", "busy-stop 1", "stop 1", "start 2 1"});
}

void test_stop_that_never_succeeds_is_given_up() {
  // Safe because the Pro-C ends the zone itself.
  FakePort p;
  Sequencer s(p, timing());
  s.addRun("Fall", run({{1, 1}, {2, 1}}));
  s.tick(0, 0);
  p.failStops = 100;
  for (uint32_t t = kMin; t <= kMin + 10000; t += 100) s.tick(t, 0);
  TEST_ASSERT_EQUAL(State::kRunning, s.state());
  TEST_ASSERT_EQUAL(2, s.entries()[*s.currentIndex()].zone);
}

void test_skip_running_zone_moves_to_next() {
  FakePort p;
  Sequencer s(p, timing());
  s.addRun("Fall", run({{1, 10}, {2, 10}, {3, 10}}));
  s.tick(0, 0);
  TEST_ASSERT_TRUE(s.skip(1000));
  TEST_ASSERT_EQUAL(Outcome::kSkipped, s.entries()[0].outcome);
  TEST_ASSERT_EQUAL(State::kPadding, s.state());
  s.tick(6000, 0);
  assertLog(p, {"start 1 10", "stop 1", "start 2 10"});
  TEST_ASSERT_EQUAL(Outcome::kPending, s.entries()[2].outcome);
}

void test_skip_during_pad_skips_the_next_zone() {
  FakePort p;
  Sequencer s(p, timing());
  s.addRun("Fall", run({{1, 1}, {2, 10}, {3, 10}}));
  s.tick(0, 0);
  s.tick(kMin, 0);  // zone 1 done, padding before 2
  TEST_ASSERT_TRUE(s.skip(kMin + 100));
  TEST_ASSERT_EQUAL(Outcome::kSkipped, s.entries()[1].outcome);
  s.tick(kMin + 5100, 0);
  assertLog(p, {"start 1 1", "stop 1", "start 3 10"});
}

void test_skip_last_zone_ends_run() {
  FakePort p;
  Sequencer s(p, timing());
  s.addRun("Fall", run({{1, 10}}));
  s.tick(0, 0);
  TEST_ASSERT_TRUE(s.skip(10));
  TEST_ASSERT_TRUE(s.idle());
  TEST_ASSERT_FALSE(s.skip(20));
}

void test_skip_when_idle_is_refused() {
  FakePort p;
  Sequencer s(p, timing());
  TEST_ASSERT_FALSE(s.skip(0));
  TEST_ASSERT_EQUAL_UINT(0, p.log.size());
}

void test_stop_zone_matching_current_cancels() {
  FakePort p;
  Sequencer s(p, timing());
  s.addRun("Fall", run({{1, 5}, {2, 5}}));
  s.tick(0, 0);
  s.stopZone(1, 10);
  TEST_ASSERT_TRUE(s.idle());
  TEST_ASSERT_EQUAL(Outcome::kInterrupted, s.entries()[1].outcome);
}

void test_stop_other_zone_does_not_disturb_run() {
  FakePort p;
  Sequencer s(p, timing());
  s.addRun("Fall", run({{1, 5}}));
  s.tick(0, 0);
  s.stopZone(6, 10);
  TEST_ASSERT_EQUAL(State::kRunning, s.state());
  assertLog(p, {"start 1 5", "stop 6"});
}

void test_run_entry_cap() {
  FakePort p;
  Sequencer s(p, timing());
  for (std::size_t i = 0; i < seq::kMaxRunEntries; ++i) {
    TEST_ASSERT_TRUE(s.add(1, 1, Origin::kManual));
  }
  TEST_ASSERT_FALSE(s.add(1, 1, Origin::kManual));
}

void test_timing_survives_millis_wrap() {
  FakePort p;
  Sequencer s(p, timing());
  const uint32_t t0 = 0xFFFFFFFFu - 1000;
  s.add(1, 1, Origin::kManual);
  s.tick(t0, 0);
  s.tick(t0 + kMin - 1, 0);
  TEST_ASSERT_EQUAL(State::kRunning, s.state());
  s.tick(t0 + kMin, 0);
  TEST_ASSERT_TRUE(s.idle());
}

void test_revision_bumps_on_changes() {
  FakePort p;
  Sequencer s(p, timing());
  const uint32_t r0 = s.revision();
  s.add(1, 1, Origin::kManual);
  TEST_ASSERT_TRUE(s.revision() > r0);
  const uint32_t r1 = s.revision();
  s.tick(0, 0);
  TEST_ASSERT_TRUE(s.revision() > r1);
  const uint32_t r2 = s.revision();
  s.tick(100, 0);
  TEST_ASSERT_EQUAL_UINT32(r2, s.revision());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_idle_does_nothing);
  RUN_TEST(test_runs_zones_in_order_with_stop_pad_start);
  RUN_TEST(test_manual_start_appends_to_running_schedule);
  RUN_TEST(test_not_idle_once_something_is_queued);
  RUN_TEST(test_not_idle_while_a_stop_is_retried);
  RUN_TEST(test_manual_start_when_idle_runs_immediately);
  RUN_TEST(test_cancel_stops_current_and_interrupts_rest);
  RUN_TEST(test_cancel_during_padding_sends_no_extra_stop);
  RUN_TEST(test_cancel_when_idle_is_harmless);
  RUN_TEST(test_new_entries_after_a_finished_run_start_a_new_run);
  RUN_TEST(test_busy_bus_start_is_retried);
  RUN_TEST(test_start_that_never_succeeds_abandons_run);
  RUN_TEST(test_next_start_waits_for_a_retried_stop);
  RUN_TEST(test_stop_that_never_succeeds_is_given_up);
  RUN_TEST(test_skip_running_zone_moves_to_next);
  RUN_TEST(test_skip_during_pad_skips_the_next_zone);
  RUN_TEST(test_skip_last_zone_ends_run);
  RUN_TEST(test_skip_when_idle_is_refused);
  RUN_TEST(test_stop_zone_matching_current_cancels);
  RUN_TEST(test_stop_other_zone_does_not_disturb_run);
  RUN_TEST(test_run_entry_cap);
  RUN_TEST(test_timing_survives_millis_wrap);
  RUN_TEST(test_revision_bumps_on_changes);
  return UNITY_END();
}
