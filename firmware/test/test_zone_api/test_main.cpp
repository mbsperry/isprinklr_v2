// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#include <unity.h>

#include <ArduinoJson.h>

#include <string>
#include <vector>

#include "zone_api.h"
#include "zone_controller.h"

namespace {

std::vector<hunter::Frame> g_sent;
bool g_busBusy = false;

bool sink(const hunter::Frame& f) {
  if (g_busBusy) return false;
  g_sent.push_back(f);
  return true;
}

JsonDocument g_request;

JsonVariantConst parse(const char* json) {
  g_request.clear();
  TEST_ASSERT_FALSE(deserializeJson(g_request, json));
  return g_request.as<JsonVariantConst>();
}

std::string str(JsonVariantConst v) { return v.as<std::string>(); }

}  // namespace

void setUp() {
  g_sent.clear();
  g_busBusy = false;
}
void tearDown() {}

void test_start_ok() {
  ZoneController zc(sink);
  const auto r = zone_api::start(zc, parse(R"({"zone":3,"duration_minutes":10})"), 0);
  TEST_ASSERT_EQUAL(202, r.status);
  TEST_ASSERT_TRUE(r.body["ok"].as<bool>());
  TEST_ASSERT_EQUAL(3, r.body["zone"].as<int>());
  TEST_ASSERT_EQUAL(10, r.body["duration_minutes"].as<int>());
  TEST_ASSERT_EQUAL_UINT(1, g_sent.size());
}

void test_start_rejects_non_integer_values() {
  ZoneController zc(sink);
  const char* bad[] = {
      R"({"zone":"3","duration_minutes":10})",
      R"({"zone":3.5,"duration_minutes":10})",
      R"({"zone":3,"duration_minutes":10.0})",
      R"({"zone":3,"duration_minutes":"10"})",
      R"({"zone":3,"duration_minutes":true})",
      R"({"zone":null,"duration_minutes":10})",
      R"({"duration_minutes":10})",
      R"({"zone":3})",
  };
  for (const char* body : bad) {
    const auto r = zone_api::start(zc, parse(body), 0);
    TEST_ASSERT_EQUAL_MESSAGE(400, r.status, body);
    TEST_ASSERT_FALSE(r.body["ok"].as<bool>());
  }
  TEST_ASSERT_EQUAL_UINT(0, g_sent.size());
}

void test_start_rejects_seconds_field() {
  // v1 spoke seconds. A client still sending duration_seconds must fail loudly.
  ZoneController zc(sink);
  const auto r = zone_api::start(zc, parse(R"({"zone":3,"duration_seconds":600})"), 0);
  TEST_ASSERT_EQUAL(400, r.status);
  TEST_ASSERT_EQUAL_STRING("invalid_duration", str(r.body["error"]).c_str());
}

void test_start_out_of_range_messages_name_bounds() {
  ZoneController zc(sink);
  auto r = zone_api::start(zc, parse(R"({"zone":49,"duration_minutes":10})"), 0);
  TEST_ASSERT_EQUAL(400, r.status);
  TEST_ASSERT_EQUAL_STRING("invalid_zone", str(r.body["error"]).c_str());
  TEST_ASSERT_EQUAL_STRING("zone must be an integer 1-48", str(r.body["message"]).c_str());

  r = zone_api::start(zc, parse(R"({"zone":1,"duration_minutes":0})"), 0);
  TEST_ASSERT_EQUAL(400, r.status);
  TEST_ASSERT_EQUAL_STRING("duration_minutes must be an integer 1-240",
                           str(r.body["message"]).c_str());

  r = zone_api::start(zc, parse(R"({"zone":1,"duration_minutes":241})"), 0);
  TEST_ASSERT_EQUAL(400, r.status);
}

void test_start_huge_number_rejected_not_truncated() {
  // 4294967297 would truncate to 1 through a 32-bit cast.
  ZoneController zc(sink);
  const auto r = zone_api::start(zc, parse(R"({"zone":4294967297,"duration_minutes":10})"), 0);
  TEST_ASSERT_EQUAL(400, r.status);
  TEST_ASSERT_EQUAL_UINT(0, g_sent.size());
}

void test_start_non_object_body() {
  ZoneController zc(sink);
  TEST_ASSERT_EQUAL(400, zone_api::start(zc, parse("[3,10]"), 0).status);
  TEST_ASSERT_EQUAL(400, zone_api::start(zc, parse("3"), 0).status);
}

void test_start_bus_busy_is_503() {
  ZoneController zc(sink);
  g_busBusy = true;
  const auto r = zone_api::start(zc, parse(R"({"zone":3,"duration_minutes":10})"), 0);
  TEST_ASSERT_EQUAL(503, r.status);
  TEST_ASSERT_EQUAL_STRING("bus_busy", str(r.body["error"]).c_str());
}

void test_stop_active_reports_zone() {
  ZoneController zc(sink);
  zc.start(4, 10, 0);
  const auto r = zone_api::stop(zc, parse("{}"), 100);
  TEST_ASSERT_EQUAL(202, r.status);
  TEST_ASSERT_EQUAL(4, r.body["zone"].as<int>());
  TEST_ASSERT_EQUAL_STRING("stop", str(r.body["action"]).c_str());
}

void test_stop_nothing_active_is_409() {
  ZoneController zc(sink);
  const auto r = zone_api::stop(zc, parse("{}"), 0);
  TEST_ASSERT_EQUAL(409, r.status);
  TEST_ASSERT_EQUAL_UINT(0, g_sent.size());
}

void test_stop_explicit_zone() {
  ZoneController zc(sink);
  const auto r = zone_api::stop(zc, parse(R"({"zone":9})"), 0);
  TEST_ASSERT_EQUAL(202, r.status);
  TEST_ASSERT_EQUAL(9, r.body["zone"].as<int>());
}

void test_stop_explicit_zone_invalid() {
  ZoneController zc(sink);
  TEST_ASSERT_EQUAL(400, zone_api::stop(zc, parse(R"({"zone":0})"), 0).status);
  TEST_ASSERT_EQUAL(400, zone_api::stop(zc, parse(R"({"zone":"9"})"), 0).status);
  TEST_ASSERT_EQUAL_UINT(0, g_sent.size());
}

void test_describe_idle_and_running() {
  ZoneController zc(sink);
  JsonDocument doc;
  zone_api::describe(zc, 0, doc.to<JsonObject>());
  TEST_ASSERT_TRUE(doc["commanded_zone"].isNull());

  zc.start(2, 3, 1000);
  doc.clear();
  zone_api::describe(zc, 1000 + 500, doc.to<JsonObject>());
  TEST_ASSERT_EQUAL(2, doc["commanded_zone"].as<int>());
  TEST_ASSERT_EQUAL(3, doc["duration_minutes"].as<int>());
  TEST_ASSERT_EQUAL(180, doc["remaining_seconds"].as<int>());  // rounds up
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_start_ok);
  RUN_TEST(test_start_rejects_non_integer_values);
  RUN_TEST(test_start_rejects_seconds_field);
  RUN_TEST(test_start_out_of_range_messages_name_bounds);
  RUN_TEST(test_start_huge_number_rejected_not_truncated);
  RUN_TEST(test_start_non_object_body);
  RUN_TEST(test_start_bus_busy_is_503);
  RUN_TEST(test_stop_active_reports_zone);
  RUN_TEST(test_stop_nothing_active_is_409);
  RUN_TEST(test_stop_explicit_zone);
  RUN_TEST(test_stop_explicit_zone_invalid);
  RUN_TEST(test_describe_idle_and_running);
  return UNITY_END();
}
