// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#include <unity.h>

#include "timekeeping.h"

using timekeeping::LocalTime;
using timekeeping::SyncState;
using timekeeping::toLocal;

void setUp() { timekeeping::applyTimezone(timekeeping::kPacificTz); }
void tearDown() {}

namespace {

// Epochs below were computed independently (Python zoneinfo, America/Los_Angeles).
constexpr std::time_t k2026Nov15_0713_PST = 1794755582;   // 07:13:02 PST
constexpr std::time_t k2026Jul04_1200_PDT = 1783191600;   // 12:00:00 PDT
constexpr std::time_t k2026Mar08_0159_PST = 1772963999;   // 01:59:59 PST, last second before spring forward
constexpr std::time_t k2026Mar08_0300_PDT = 1772964000;   // 03:00:00 PDT
constexpr std::time_t k2026Nov01_0159_PDT = 1793523599;   // 01:59:59 PDT, first pass through 01:xx
constexpr std::time_t k2026Nov01_0100_PST = 1793523600;   // 01:00:00 PST, second pass
constexpr std::time_t k2026Dec31_2359_PST = 1798790399;   // 23:59:59 PST
constexpr std::time_t k2027Jan01_0000_PST = 1798790400;   // next local day

void assertLocal(std::time_t epoch, int y, int mo, int d, int wd, int h, int mi,
                 int s, bool dst, int offset) {
  const LocalTime t = toLocal(epoch);
  TEST_ASSERT_EQUAL(y, t.year);
  TEST_ASSERT_EQUAL(mo, t.month);
  TEST_ASSERT_EQUAL(d, t.day);
  TEST_ASSERT_EQUAL(wd, t.weekday);
  TEST_ASSERT_EQUAL(h, t.hour);
  TEST_ASSERT_EQUAL(mi, t.minute);
  TEST_ASSERT_EQUAL(s, t.second);
  TEST_ASSERT_EQUAL(dst, t.dst);
  TEST_ASSERT_EQUAL(offset, t.utcOffsetMinutes);
}

}  // namespace

void test_invalid_before_first_sync() {
  SyncState s;
  TEST_ASSERT_FALSE(timekeeping::isTimeValid(s, k2026Nov15_0713_PST));
}

void test_valid_after_sync() {
  SyncState s{1, 1000};
  TEST_ASSERT_TRUE(timekeeping::isTimeValid(s, k2026Nov15_0713_PST));
}

void test_invalid_if_clock_implausible() {
  // Synced flag set but the clock reads 1970: never trust it.
  SyncState s{1, 1000};
  TEST_ASSERT_FALSE(timekeeping::isTimeValid(s, 0));
  TEST_ASSERT_FALSE(timekeeping::isTimeValid(s, timekeeping::kMinPlausibleEpoch - 1));
  TEST_ASSERT_TRUE(timekeeping::isTimeValid(s, timekeeping::kMinPlausibleEpoch));
}

void test_winter_pst() {
  assertLocal(k2026Nov15_0713_PST, 2026, 11, 15, 0, 7, 13, 2, false, -480);
}

void test_summer_pdt() {
  assertLocal(k2026Jul04_1200_PDT, 2026, 7, 4, 6, 12, 0, 0, true, -420);
}

void test_spring_forward_skips_2am() {
  assertLocal(k2026Mar08_0159_PST, 2026, 3, 8, 0, 1, 59, 59, false, -480);
  assertLocal(k2026Mar08_0300_PDT, 2026, 3, 8, 0, 3, 0, 0, true, -420);
}

void test_fall_back_repeats_1am() {
  assertLocal(k2026Nov01_0159_PDT, 2026, 11, 1, 0, 1, 59, 59, true, -420);
  assertLocal(k2026Nov01_0100_PST, 2026, 11, 1, 0, 1, 0, 0, false, -480);
}

void test_year_boundary_is_local_not_utc() {
  // 08:00 UTC on Jan 1 is still Dec 31 locally until then.
  assertLocal(k2026Dec31_2359_PST, 2026, 12, 31, 4, 23, 59, 59, false, -480);
  assertLocal(k2027Jan01_0000_PST, 2027, 1, 1, 5, 0, 0, 0, false, -480);
}

void test_minute_of_day() {
  TEST_ASSERT_EQUAL(7 * 60 + 13, toLocal(k2026Nov15_0713_PST).minuteOfDay());
}

void test_iso_formats() {
  TEST_ASSERT_EQUAL_STRING(
      "2026-11-15T07:13:02-08:00",
      timekeeping::formatLocalIso8601(toLocal(k2026Nov15_0713_PST)).c_str());
  TEST_ASSERT_EQUAL_STRING(
      "2026-07-04T12:00:00-07:00",
      timekeeping::formatLocalIso8601(toLocal(k2026Jul04_1200_PDT)).c_str());
  TEST_ASSERT_EQUAL_STRING(
      "2026-11-15T15:13:02Z",
      timekeeping::formatUtcIso8601(k2026Nov15_0713_PST).c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_invalid_before_first_sync);
  RUN_TEST(test_valid_after_sync);
  RUN_TEST(test_invalid_if_clock_implausible);
  RUN_TEST(test_winter_pst);
  RUN_TEST(test_summer_pdt);
  RUN_TEST(test_spring_forward_skips_2am);
  RUN_TEST(test_fall_back_repeats_1am);
  RUN_TEST(test_year_boundary_is_local_not_utc);
  RUN_TEST(test_minute_of_day);
  RUN_TEST(test_iso_formats);
  return UNITY_END();
}
