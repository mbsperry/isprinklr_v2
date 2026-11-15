// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#include <unity.h>

#include "daily_trigger.h"
#include "timekeeping.h"

using trigger::Decision;

namespace {

settings::Settings enabledSettings() {
  settings::Settings s;
  s.zones.push_back({1, "A"});
  settings::Schedule sch{"Fall", {}};
  settings::DayPattern all;
  all.kind = settings::DayPattern::Kind::kAll;
  sch.items.push_back({1, all, 10});
  s.schedules.push_back(sch);
  s.activeSchedule = "Fall";
  s.enabled = true;
  s.config.startHour = 7;
  s.config.startMinute = 0;
  return s;
}

timekeeping::LocalTime at(int y, int mo, int d, int h, int mi) {
  return timekeeping::LocalTime{y, mo, d, 0, h, mi, 0, false, -480};
}

}  // namespace

void setUp() { timekeeping::applyTimezone(timekeeping::kPacificTz); }
void tearDown() {}

void test_due_at_start_time() {
  const auto s = enabledSettings();
  TEST_ASSERT_EQUAL(Decision::kDue, trigger::decide(true, at(2026, 5, 1, 7, 0), s, -1));
}

void test_before_start() {
  const auto s = enabledSettings();
  TEST_ASSERT_EQUAL(Decision::kBeforeStart, trigger::decide(true, at(2026, 5, 1, 6, 59), s, -1));
}

void test_window_then_missed() {
  const auto s = enabledSettings();
  TEST_ASSERT_EQUAL(Decision::kDue, trigger::decide(true, at(2026, 5, 1, 7, 59), s, -1));
  TEST_ASSERT_EQUAL(Decision::kWindowMissed, trigger::decide(true, at(2026, 5, 1, 8, 0), s, -1));
}

void test_already_ran_today() {
  const auto s = enabledSettings();
  const auto now = at(2026, 5, 1, 7, 5);
  TEST_ASSERT_EQUAL(Decision::kAlreadyRan,
                    trigger::decide(true, now, s, trigger::localDay(now)));
  // Yesterday's run does not block today.
  TEST_ASSERT_EQUAL(Decision::kDue,
                    trigger::decide(true, now, s, trigger::localDay(now) - 1));
}

void test_gates() {
  auto s = enabledSettings();
  const auto now = at(2026, 5, 1, 7, 0);
  TEST_ASSERT_EQUAL(Decision::kTimeInvalid, trigger::decide(false, now, s, -1));
  s.enabled = false;
  TEST_ASSERT_EQUAL(Decision::kDisabled, trigger::decide(true, now, s, -1));
  s.enabled = true;
  s.activeSchedule.clear();
  TEST_ASSERT_EQUAL(Decision::kNoActiveSchedule, trigger::decide(true, now, s, -1));
}

void test_spring_forward_start_in_skipped_hour_still_fires() {
  // 2026-03-08: 02:00-02:59 does not exist. A 02:30 start sees 01:59 then 03:00.
  auto s = enabledSettings();
  s.config.startHour = 2;
  s.config.startMinute = 30;
  const auto before = timekeeping::toLocal(1772963999);   // 01:59:59 PST
  const auto after = timekeeping::toLocal(1772964000);    // 03:00:00 PDT
  TEST_ASSERT_EQUAL(Decision::kBeforeStart, trigger::decide(true, before, s, -1));
  TEST_ASSERT_EQUAL(Decision::kDue, trigger::decide(true, after, s, -1));
}

void test_fall_back_repeated_hour_runs_once() {
  // 2026-11-01: 01:00-01:59 happens twice. Start 01:30.
  auto s = enabledSettings();
  s.config.startHour = 1;
  s.config.startMinute = 30;
  const auto first = timekeeping::toLocal(1793521800);    // 01:30 PDT
  const auto second = timekeeping::toLocal(1793525400);   // 01:30 PST
  TEST_ASSERT_EQUAL(1, first.hour);
  TEST_ASSERT_EQUAL(1, second.hour);
  TEST_ASSERT_EQUAL(Decision::kDue, trigger::decide(true, first, s, -1));
  const long ran = trigger::localDay(first);
  TEST_ASSERT_EQUAL(Decision::kAlreadyRan, trigger::decide(true, second, s, ran));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_due_at_start_time);
  RUN_TEST(test_before_start);
  RUN_TEST(test_window_then_missed);
  RUN_TEST(test_already_ran_today);
  RUN_TEST(test_gates);
  RUN_TEST(test_spring_forward_start_in_skipped_hour_still_fires);
  RUN_TEST(test_fall_back_repeated_hour_runs_once);
  return UNITY_END();
}
