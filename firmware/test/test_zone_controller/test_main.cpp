// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#include <unity.h>

#include <cstring>
#include <vector>

#include "hunter_frame.h"
#include "hunter_limits.h"
#include "zone_controller.h"

using Result = ZoneController::Result;

namespace {

std::vector<hunter::Frame> g_sent;
bool g_busBusy = false;

bool sink(const hunter::Frame& f) {
  if (g_busBusy) return false;
  g_sent.push_back(f);
  return true;
}

bool sameFrame(const hunter::Frame& a, const std::optional<hunter::Frame>& b) {
  return b && a.length == b->length && memcmp(a.bytes, b->bytes, a.length) == 0;
}

constexpr uint32_t kMinute = 60000;

}  // namespace

void setUp() {
  g_sent.clear();
  g_busBusy = false;
}
void tearDown() {}

void test_start_sends_start_frame_and_records_run() {
  ZoneController zc(sink);
  TEST_ASSERT_EQUAL(Result::kOk, zc.start(3, 10, 1000));
  TEST_ASSERT_EQUAL_UINT(1, g_sent.size());
  TEST_ASSERT_TRUE(sameFrame(g_sent[0], hunter::buildStartFrame(3, 10)));

  const auto run = zc.active(1000);
  TEST_ASSERT_TRUE(run.has_value());
  TEST_ASSERT_EQUAL(3, run->zone);
  TEST_ASSERT_EQUAL(10, run->durationMinutes);
}

void test_start_rejects_bad_input_without_sending() {
  ZoneController zc(sink);
  TEST_ASSERT_EQUAL(Result::kInvalidZone, zc.start(0, 10, 0));
  TEST_ASSERT_EQUAL(Result::kInvalidZone, zc.start(hunter::kMaxZone + 1, 10, 0));
  TEST_ASSERT_EQUAL(Result::kInvalidDuration, zc.start(1, 0, 0));
  TEST_ASSERT_EQUAL(Result::kInvalidDuration, zc.start(1, hunter::kMaxRunMinutes + 1, 0));
  TEST_ASSERT_EQUAL_UINT(0, g_sent.size());
  TEST_ASSERT_FALSE(zc.active(0).has_value());
}

void test_busy_bus_leaves_state_unchanged() {
  ZoneController zc(sink);
  zc.start(2, 5, 0);
  g_busBusy = true;
  TEST_ASSERT_EQUAL(Result::kBusBusy, zc.start(4, 5, 10));
  TEST_ASSERT_EQUAL(2, zc.active(10)->zone);
  TEST_ASSERT_EQUAL(Result::kBusBusy, zc.stop(std::nullopt, 20));
  TEST_ASSERT_EQUAL(2, zc.active(20)->zone);
}

void test_run_expires_after_duration() {
  ZoneController zc(sink);
  zc.start(1, 2, 5000);
  TEST_ASSERT_TRUE(zc.active(5000 + 2 * kMinute - 1).has_value());
  TEST_ASSERT_FALSE(zc.active(5000 + 2 * kMinute).has_value());
}

void test_remaining_ms() {
  ZoneController zc(sink);
  TEST_ASSERT_EQUAL_UINT32(0, zc.remainingMs(0));
  zc.start(1, 2, 1000);
  TEST_ASSERT_EQUAL_UINT32(2 * kMinute, zc.remainingMs(1000));
  TEST_ASSERT_EQUAL_UINT32(2 * kMinute - 30000, zc.remainingMs(31000));
  TEST_ASSERT_EQUAL_UINT32(0, zc.remainingMs(1000 + 2 * kMinute));
}

void test_expiry_survives_millis_wrap() {
  ZoneController zc(sink);
  const uint32_t start = 0xFFFFFFFFu - 30000;  // 30 s before wrap
  zc.start(1, 1, start);
  TEST_ASSERT_TRUE(zc.active(start + 59000).has_value());  // wrapped
  TEST_ASSERT_FALSE(zc.active(start + 60000).has_value());
}

void test_max_duration_does_not_overflow() {
  ZoneController zc(sink);
  zc.start(1, hunter::kMaxRunMinutes, 0);
  TEST_ASSERT_EQUAL_UINT32(240u * kMinute, zc.remainingMs(0));
}

void test_stop_without_zone_stops_active() {
  ZoneController zc(sink);
  zc.start(5, 10, 0);
  TEST_ASSERT_EQUAL(Result::kOk, zc.stop(std::nullopt, 100));
  TEST_ASSERT_EQUAL_UINT(2, g_sent.size());
  TEST_ASSERT_TRUE(sameFrame(g_sent[1], hunter::buildStopFrame(5)));
  TEST_ASSERT_FALSE(zc.active(100).has_value());
}

void test_stop_without_zone_and_nothing_active_is_refused() {
  ZoneController zc(sink);
  TEST_ASSERT_EQUAL(Result::kNoActiveZone, zc.stop(std::nullopt, 0));
  TEST_ASSERT_EQUAL_UINT(0, g_sent.size());
}

void test_stop_after_expiry_without_zone_is_refused() {
  ZoneController zc(sink);
  zc.start(5, 1, 0);
  TEST_ASSERT_EQUAL(Result::kNoActiveZone, zc.stop(std::nullopt, kMinute));
}

void test_stop_explicit_zone_not_known_running_still_sends() {
  // After a reboot the controller has no record, but the Pro-C may still be
  // running a zone. Stopping it by number must work.
  ZoneController zc(sink);
  TEST_ASSERT_EQUAL(Result::kOk, zc.stop(7, 0));
  TEST_ASSERT_EQUAL_UINT(1, g_sent.size());
  TEST_ASSERT_TRUE(sameFrame(g_sent[0], hunter::buildStopFrame(7)));
}

void test_stop_other_zone_keeps_active_record() {
  ZoneController zc(sink);
  zc.start(5, 10, 0);
  TEST_ASSERT_EQUAL(Result::kOk, zc.stop(6, 0));
  TEST_ASSERT_EQUAL(5, zc.active(0)->zone);
}

void test_stop_rejects_invalid_zone() {
  ZoneController zc(sink);
  TEST_ASSERT_EQUAL(Result::kInvalidZone, zc.stop(0, 0));
  TEST_ASSERT_EQUAL(Result::kInvalidZone, zc.stop(49, 0));
  TEST_ASSERT_EQUAL_UINT(0, g_sent.size());
}

void test_start_replaces_record() {
  ZoneController zc(sink);
  zc.start(1, 10, 0);
  zc.start(2, 3, 1000);
  TEST_ASSERT_EQUAL(2, zc.active(1000)->zone);
  TEST_ASSERT_EQUAL_UINT(2, g_sent.size());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_start_sends_start_frame_and_records_run);
  RUN_TEST(test_start_rejects_bad_input_without_sending);
  RUN_TEST(test_busy_bus_leaves_state_unchanged);
  RUN_TEST(test_run_expires_after_duration);
  RUN_TEST(test_remaining_ms);
  RUN_TEST(test_expiry_survives_millis_wrap);
  RUN_TEST(test_max_duration_does_not_overflow);
  RUN_TEST(test_stop_without_zone_stops_active);
  RUN_TEST(test_stop_without_zone_and_nothing_active_is_refused);
  RUN_TEST(test_stop_after_expiry_without_zone_is_refused);
  RUN_TEST(test_stop_explicit_zone_not_known_running_still_sends);
  RUN_TEST(test_stop_other_zone_keeps_active_record);
  RUN_TEST(test_stop_rejects_invalid_zone);
  RUN_TEST(test_start_replaces_record);
  return UNITY_END();
}
