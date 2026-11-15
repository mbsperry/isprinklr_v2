// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#include <unity.h>

#include <string>
#include <vector>

#include "checkpoint.h"
#include "sequencer.h"

using recovery::Checkpoint;
using recovery::Plan;
using seq::Origin;
using seq::Outcome;
using seq::Sequencer;

namespace {

constexpr uint32_t kMin = 60000;
constexpr uint32_t kT0 = 1794755582;  // 2026-11-15 07:13:02 PST
constexpr uint32_t kWindow = 3600;

struct FakePort : seq::ZonePort {
  std::vector<std::string> log;
  bool start(int zone, int minutes) override {
    log.push_back("start " + std::to_string(zone) + " " + std::to_string(minutes));
    return true;
  }
  bool stop(int zone) override {
    log.push_back("stop " + std::to_string(zone));
    return true;
  }
};

// A three-zone run, 10 minutes each, interrupted 4 minutes into zone 2.
Checkpoint midRun() {
  FakePort p;
  Sequencer s(p);
  std::vector<seq::Entry> run{{1, 10, Origin::kSchedule},
                              {2, 10, Origin::kSchedule},
                              {3, 10, Origin::kSchedule}};
  s.addRun("Fall", run);
  s.tick(0, kT0);
  s.tick(10 * kMin, kT0 + 600);
  s.tick(10 * kMin + 5000, kT0 + 605);  // zone 2 starts
  s.tick(14 * kMin, kT0 + 845);
  return recovery::capture(s, kT0 + 845);
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_idle_sequencer_has_nothing_to_recover() {
  FakePort p;
  Sequencer s(p);
  const Checkpoint c = recovery::capture(s, kT0);
  TEST_ASSERT_FALSE(c.inProgress);
  TEST_ASSERT_EQUAL_STRING("{}", recovery::serialize(c).c_str());
  TEST_ASSERT_EQUAL(Plan::Action::kNothing, recovery::plan(c, kT0, kWindow).action);
}

void test_capture_points_at_running_zone() {
  const Checkpoint c = midRun();
  TEST_ASSERT_TRUE(c.inProgress);
  TEST_ASSERT_EQUAL_UINT32(1, c.index);
  TEST_ASSERT_EQUAL(Outcome::kCompleted, c.entries[0].outcome);
  TEST_ASSERT_EQUAL(Outcome::kRunning, c.entries[1].outcome);
  TEST_ASSERT_EQUAL_UINT32(kT0 + 605, c.entries[1].startedEpoch);
}

void test_serialize_round_trip() {
  const Checkpoint c = midRun();
  const std::string text = recovery::serialize(c);
  Checkpoint back;
  TEST_ASSERT_TRUE(recovery::parse(text, &back));
  TEST_ASSERT_TRUE(back.inProgress);
  TEST_ASSERT_EQUAL_STRING("Fall", back.runName.c_str());
  TEST_ASSERT_EQUAL_UINT32(c.index, back.index);
  TEST_ASSERT_EQUAL_UINT32(c.savedEpoch, back.savedEpoch);
  TEST_ASSERT_EQUAL_UINT(3, back.entries.size());
  TEST_ASSERT_EQUAL(Outcome::kRunning, back.entries[1].outcome);
  TEST_ASSERT_EQUAL_UINT32(kT0 + 605, back.entries[1].startedEpoch);
  TEST_ASSERT_TRUE(text.size() < 200);
}

void test_parse_rejects_garbage() {
  Checkpoint c;
  TEST_ASSERT_FALSE(recovery::parse("not json", &c));
  TEST_ASSERT_FALSE(recovery::parse(R"({"n":"F","i":0,"t":1,"e":[[1,2,3]]})", &c));
  TEST_ASSERT_FALSE(recovery::parse(R"({"n":"F","i":0,"t":1,"e":[[1,2,9,0,0]]})", &c));
  TEST_ASSERT_FALSE(recovery::parse(R"({"n":"F","i":5,"t":1,"e":[[1,2,0,0,0]]})", &c));
}

void test_quick_reboot_resumes_and_waits_out_running_zone() {
  // Back 1 minute after the checkpoint: zone 2 has ~5 minutes left on the Pro-C.
  const Checkpoint c = midRun();
  const Plan p = recovery::plan(c, kT0 + 905, kWindow);
  TEST_ASSERT_EQUAL(Plan::Action::kResume, p.action);
  TEST_ASSERT_EQUAL_UINT32((kT0 + 1205 - (kT0 + 905)) * 1000, p.runningRemainingMs);

  FakePort port;
  Sequencer s(port);
  s.resume(c.runName, c.entries, c.index, p.runningRemainingMs, 0);
  TEST_ASSERT_EQUAL(Sequencer::State::kRunning, s.state());
  s.tick(5 * kMin - 1, 0);
  TEST_ASSERT_EQUAL_UINT(0, port.log.size());  // zone 2 not restarted
  s.tick(5 * kMin, 0);
  s.tick(5 * kMin + 5000, 0);
  TEST_ASSERT_EQUAL_UINT(2, port.log.size());
  TEST_ASSERT_EQUAL_STRING("stop 2", port.log[0].c_str());
  TEST_ASSERT_EQUAL_STRING("start 3 10", port.log[1].c_str());
}

void test_reboot_after_running_zone_ended_resumes_with_next() {
  // Back 20 minutes later: zone 2 ended at kT0+1205, zone 3 was due then.
  const Checkpoint c = midRun();
  const Plan p = recovery::plan(c, kT0 + 1205 + 300, kWindow);
  TEST_ASSERT_EQUAL(Plan::Action::kResume, p.action);
  TEST_ASSERT_EQUAL_UINT32(0, p.runningRemainingMs);
  TEST_ASSERT_EQUAL_UINT32(300, p.lateSeconds);

  FakePort port;
  Sequencer s(port);
  s.resume(c.runName, c.entries, c.index, 0, 0);
  TEST_ASSERT_EQUAL(Outcome::kCompleted, s.entries()[1].outcome);
  s.tick(5000, 0);
  TEST_ASSERT_EQUAL_STRING("start 3 10", port.log.at(0).c_str());
}

void test_long_outage_skips() {
  const Checkpoint c = midRun();
  const Plan p = recovery::plan(c, kT0 + 1205 + kWindow + 1, kWindow);
  TEST_ASSERT_EQUAL(Plan::Action::kSkip, p.action);

  FakePort port;
  Sequencer s(port);
  s.showInterrupted(c.runName, c.entries);
  TEST_ASSERT_TRUE(s.idle());
  TEST_ASSERT_EQUAL(Outcome::kInterrupted, s.entries()[2].outcome);
  s.tick(10 * kMin, 0);
  TEST_ASSERT_EQUAL_UINT(0, port.log.size());
}

void test_window_edges() {
  const Checkpoint c = midRun();
  const uint32_t due = kT0 + 1205;
  TEST_ASSERT_EQUAL(Plan::Action::kResume, recovery::plan(c, due + kWindow, kWindow).action);
  TEST_ASSERT_EQUAL(Plan::Action::kSkip, recovery::plan(c, due + kWindow + 1, kWindow).action);
  TEST_ASSERT_EQUAL(Plan::Action::kSkip, recovery::plan(c, due + 1, 0).action);
  TEST_ASSERT_EQUAL(Plan::Action::kResume, recovery::plan(c, due + 864000, UINT32_MAX).action);
}

void test_only_running_zone_left_needs_no_decision() {
  FakePort p;
  Sequencer s(p);
  std::vector<seq::Entry> run{{1, 10, Origin::kSchedule}};
  s.addRun("Fall", run);
  s.tick(0, kT0);
  const Checkpoint c = recovery::capture(s, kT0 + 60);
  // Even days later nothing is skipped: the zone finished on the Pro-C.
  const Plan pl = recovery::plan(c, kT0 + 864000, kWindow);
  TEST_ASSERT_EQUAL(Plan::Action::kResume, pl.action);
  Sequencer r(p);
  r.resume(c.runName, c.entries, c.index, pl.runningRemainingMs, 0);
  TEST_ASSERT_TRUE(r.idle());
}

void test_unknown_start_time_uses_save_time() {
  // A manual run started before the clock synced has startedEpoch 0.
  FakePort p;
  Sequencer s(p);
  s.add(1, 10, Origin::kManual);
  s.add(2, 10, Origin::kManual);
  s.tick(0, 0);
  Checkpoint c = recovery::capture(s, 0);
  TEST_ASSERT_EQUAL(Plan::Action::kSkip, recovery::plan(c, kT0, kWindow).action);
  c.savedEpoch = kT0;
  TEST_ASSERT_EQUAL(Plan::Action::kResume, recovery::plan(c, kT0 + 100, kWindow).action);
}

void test_padding_checkpoint_resumes_next_zone() {
  FakePort p;
  Sequencer s(p);
  std::vector<seq::Entry> run{{1, 1, Origin::kSchedule}, {2, 1, Origin::kSchedule}};
  s.addRun("Fall", run);
  s.tick(0, kT0);
  s.tick(kMin, kT0 + 60);  // stop 1, padding
  const Checkpoint c = recovery::capture(s, kT0 + 60);
  TEST_ASSERT_EQUAL_UINT32(1, c.index);
  TEST_ASSERT_EQUAL(Outcome::kPending, c.entries[1].outcome);
  const Plan pl = recovery::plan(c, kT0 + 90, kWindow);
  TEST_ASSERT_EQUAL(Plan::Action::kResume, pl.action);
  FakePort q;
  Sequencer r(q);
  r.resume(c.runName, c.entries, c.index, pl.runningRemainingMs, 0);
  r.tick(5000, 0);
  TEST_ASSERT_EQUAL_STRING("start 2 1", q.log.at(0).c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_idle_sequencer_has_nothing_to_recover);
  RUN_TEST(test_capture_points_at_running_zone);
  RUN_TEST(test_serialize_round_trip);
  RUN_TEST(test_parse_rejects_garbage);
  RUN_TEST(test_quick_reboot_resumes_and_waits_out_running_zone);
  RUN_TEST(test_reboot_after_running_zone_ended_resumes_with_next);
  RUN_TEST(test_long_outage_skips);
  RUN_TEST(test_window_edges);
  RUN_TEST(test_only_running_zone_left_needs_no_decision);
  RUN_TEST(test_unknown_start_time_uses_save_time);
  RUN_TEST(test_padding_checkpoint_resumes_next_zone);
  return UNITY_END();
}
