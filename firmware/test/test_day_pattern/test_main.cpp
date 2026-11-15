// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#include <unity.h>

#include <string>

#include "day_pattern.h"
#include "settings.h"
#include "timekeeping.h"

using settings::DayPattern;

void setUp() {}
void tearDown() {}

namespace {

DayPattern parse(const char* text) {
  DayPattern p;
  std::string err;
  TEST_ASSERT_TRUE_MESSAGE(settings::parseDayPattern(text, &p, &err), text);
  return p;
}

void assertRejected(const char* text) {
  DayPattern p;
  std::string err;
  TEST_ASSERT_FALSE_MESSAGE(settings::parseDayPattern(text, &p, &err), text);
  TEST_ASSERT_FALSE(err.empty());
}

// Weekday for a date via the epoch-day count (1970-01-01 was a Thursday).
int weekdayOf(int y, int m, int d) {
  const long days = timekeeping::daysSinceEpoch(y, m, d);
  return static_cast<int>(((days % 7) + 7 + 4) % 7);
}

bool runs(const DayPattern& p, int y, int m, int d) {
  return settings::runsOn(p, y, m, d, weekdayOf(y, m, d));
}

}  // namespace

void test_v1_mixed_case_tokens_parse() {
  // Real v1 data: "Tu:F", "Tu:Th:Sa", "M:W:F", "Su", "NONE".
  TEST_ASSERT_EQUAL_STRING("TU:F", settings::formatDayPattern(parse("Tu:F")).c_str());
  TEST_ASSERT_EQUAL_STRING("TU:TH:SA", settings::formatDayPattern(parse("Tu:Th:Sa")).c_str());
  TEST_ASSERT_EQUAL_STRING("M:W:F", settings::formatDayPattern(parse("M:W:F")).c_str());
  TEST_ASSERT_EQUAL_STRING("SU", settings::formatDayPattern(parse("Su")).c_str());
  TEST_ASSERT_EQUAL_STRING("NONE", settings::formatDayPattern(parse("NONE")).c_str());
}

void test_canonical_order_is_monday_first() {
  TEST_ASSERT_EQUAL_STRING("M:F:SU", settings::formatDayPattern(parse("SU:F:M")).c_str());
}

void test_specials() {
  TEST_ASSERT_EQUAL_STRING("ALL", settings::formatDayPattern(parse("all")).c_str());
  TEST_ASSERT_EQUAL_STRING("EO", settings::formatDayPattern(parse("eo")).c_str());
  TEST_ASSERT_TRUE(parse("NONE").kind == DayPattern::Kind::kNone);
}

void test_rejects_bad_patterns() {
  assertRejected("");
  assertRejected("MO");
  assertRejected("T");
  assertRejected("M:");
  assertRejected(":M");
  assertRejected("M::W");
  assertRejected("M:M");
  assertRejected("ALL:M");
  assertRejected("M:EO");
  assertRejected("NONE:ALL");
  assertRejected("M W");
}

void test_weekdays_match() {
  const DayPattern p = parse("TU:F");
  TEST_ASSERT_TRUE(runs(p, 2026, 11, 17));   // Tuesday
  TEST_ASSERT_TRUE(runs(p, 2026, 11, 20));   // Friday
  TEST_ASSERT_FALSE(runs(p, 2026, 11, 15));  // Sunday
  TEST_ASSERT_FALSE(runs(p, 2026, 11, 19));  // Thursday
}

void test_sunday_is_weekday_zero() {
  const DayPattern p = parse("SU");
  TEST_ASSERT_TRUE(settings::runsOn(p, 2026, 11, 15, 0));
  TEST_ASSERT_FALSE(settings::runsOn(p, 2026, 11, 15, 6));
}

void test_all_and_none() {
  TEST_ASSERT_TRUE(runs(parse("ALL"), 2026, 2, 28));
  TEST_ASSERT_FALSE(runs(parse("NONE"), 2026, 2, 28));
}

void test_eo_alternates_across_month_ends() {
  // Calendar parity would run twice over Jan 31 -> Feb 1, and day-of-year
  // parity (v1) over Dec 31 -> Jan 1. Strict alternation never repeats.
  const DayPattern eo = parse("EO");
  const int dates[][3] = {{2026, 1, 31}, {2026, 2, 1},  {2026, 2, 28},
                          {2026, 3, 1},  {2026, 12, 31}, {2027, 1, 1},
                          {2028, 2, 28}, {2028, 2, 29}, {2028, 3, 1}};
  for (const auto& d : dates) {
    int y = d[0], m = d[1], day = d[2];
    // Next day, the simple way.
    const long n = timekeeping::daysSinceEpoch(y, m, day);
    int ny = y, nm = m, nd = day + 1;
    if (timekeeping::daysSinceEpoch(ny, nm, nd) != n + 1) {
      nd = 1;
      if (++nm > 12) { nm = 1; ++ny; }
    }
    TEST_ASSERT_EQUAL_INT32(n + 1, timekeeping::daysSinceEpoch(ny, nm, nd));
    TEST_ASSERT_NOT_EQUAL(runs(eo, y, m, day), runs(eo, ny, nm, nd));
  }
}

void test_eo_runs_on_epoch_day() {
  TEST_ASSERT_TRUE(runs(parse("EO"), 1970, 1, 1));
  TEST_ASSERT_FALSE(runs(parse("EO"), 1970, 1, 2));
}

void test_runs_for_date_keeps_schedule_order() {
  settings::Schedule s{"Fall", {}};
  s.items.push_back({3, parse("M:W:F"), 20});
  s.items.push_back({1, parse("TU:F"), 30});
  s.items.push_back({8, parse("NONE"), 5});
  timekeeping::LocalTime friday{2026, 11, 20, 5, 7, 0, 0, false, -480};
  const auto r = settings::runsForDate(s, friday);
  TEST_ASSERT_EQUAL_UINT(2, r.size());
  TEST_ASSERT_EQUAL(3, r[0].zone);
  TEST_ASSERT_EQUAL(20, r[0].durationMinutes);
  TEST_ASSERT_EQUAL(1, r[1].zone);
  timekeeping::LocalTime sunday{2026, 11, 15, 0, 7, 0, 0, false, -480};
  TEST_ASSERT_EQUAL_UINT(0, settings::runsForDate(s, sunday).size());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_v1_mixed_case_tokens_parse);
  RUN_TEST(test_canonical_order_is_monday_first);
  RUN_TEST(test_specials);
  RUN_TEST(test_rejects_bad_patterns);
  RUN_TEST(test_weekdays_match);
  RUN_TEST(test_sunday_is_weekday_zero);
  RUN_TEST(test_all_and_none);
  RUN_TEST(test_eo_alternates_across_month_ends);
  RUN_TEST(test_eo_runs_on_epoch_day);
  RUN_TEST(test_runs_for_date_keeps_schedule_order);
  return UNITY_END();
}
