// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#include <unity.h>

#include <ArduinoJson.h>

#include <string>
#include <vector>

#include "sequencer.h"
#include "zone_api.h"
#include "zone_controller.h"

namespace {

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

JsonDocument g_request;

JsonVariantConst parse(const char* json) {
  g_request.clear();
  TEST_ASSERT_FALSE(deserializeJson(g_request, json));
  return g_request.as<JsonVariantConst>();
}

std::string str(JsonVariantConst v) { return v.as<std::string>(); }

}  // namespace

void setUp() {}
void tearDown() {}

void test_start_queues_manual_run() {
  FakePort p;
  seq::Sequencer s(p);
  const auto r = zone_api::start(s, parse(R"({"zone":3,"duration_minutes":10})"));
  TEST_ASSERT_EQUAL(202, r.status);
  TEST_ASSERT_TRUE(r.body["ok"].as<bool>());
  TEST_ASSERT_EQUAL(3, r.body["zone"].as<int>());
  TEST_ASSERT_EQUAL(10, r.body["duration_minutes"].as<int>());
  TEST_ASSERT_EQUAL(0, r.body["queued_behind"].as<int>());
  s.tick(0, 0);
  TEST_ASSERT_EQUAL_STRING("start 3 10", p.log.at(0).c_str());
}

void test_second_start_queues_behind() {
  FakePort p;
  seq::Sequencer s(p);
  zone_api::start(s, parse(R"({"zone":3,"duration_minutes":10})"));
  s.tick(0, 0);
  const auto r = zone_api::start(s, parse(R"({"zone":4,"duration_minutes":5})"));
  TEST_ASSERT_EQUAL(202, r.status);
  TEST_ASSERT_EQUAL(1, r.body["queued_behind"].as<int>());
  TEST_ASSERT_EQUAL_UINT(1, p.log.size());  // zone 3 not preempted
}

void test_start_rejects_non_integer_values() {
  FakePort p;
  seq::Sequencer s(p);
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
    const auto r = zone_api::start(s, parse(body));
    TEST_ASSERT_EQUAL_MESSAGE(400, r.status, body);
    TEST_ASSERT_FALSE(r.body["ok"].as<bool>());
  }
  TEST_ASSERT_EQUAL_UINT(0, s.entries().size());
}

void test_start_rejects_seconds_field() {
  FakePort p;
  seq::Sequencer s(p);
  const auto r = zone_api::start(s, parse(R"({"zone":3,"duration_seconds":600})"));
  TEST_ASSERT_EQUAL(400, r.status);
  TEST_ASSERT_EQUAL_STRING("invalid_duration", str(r.body["error"]).c_str());
}

void test_start_out_of_range_messages_name_bounds() {
  FakePort p;
  seq::Sequencer s(p);
  auto r = zone_api::start(s, parse(R"({"zone":49,"duration_minutes":10})"));
  TEST_ASSERT_EQUAL(400, r.status);
  TEST_ASSERT_EQUAL_STRING("zone must be an integer 1-48", str(r.body["message"]).c_str());
  r = zone_api::start(s, parse(R"({"zone":1,"duration_minutes":0})"));
  TEST_ASSERT_EQUAL_STRING("duration_minutes must be an integer 1-240",
                           str(r.body["message"]).c_str());
  r = zone_api::start(s, parse(R"({"zone":1,"duration_minutes":241})"));
  TEST_ASSERT_EQUAL(400, r.status);
}

void test_start_huge_number_rejected_not_truncated() {
  FakePort p;
  seq::Sequencer s(p);
  const auto r = zone_api::start(s, parse(R"({"zone":4294967297,"duration_minutes":10})"));
  TEST_ASSERT_EQUAL(400, r.status);
}

void test_start_non_object_body() {
  FakePort p;
  seq::Sequencer s(p);
  TEST_ASSERT_EQUAL(400, zone_api::start(s, parse("[3,10]")).status);
  TEST_ASSERT_EQUAL(400, zone_api::start(s, parse("3")).status);
}

void test_start_queue_full_is_503() {
  FakePort p;
  seq::Sequencer s(p);
  for (std::size_t i = 0; i < seq::kMaxRunEntries; ++i) s.add(1, 1, seq::Origin::kManual);
  const auto r = zone_api::start(s, parse(R"({"zone":3,"duration_minutes":10})"));
  TEST_ASSERT_EQUAL(503, r.status);
  TEST_ASSERT_EQUAL_STRING("queue_full", str(r.body["error"]).c_str());
}

void test_stop_cancels_run() {
  FakePort p;
  seq::Sequencer s(p);
  zone_api::start(s, parse(R"({"zone":4,"duration_minutes":10})"));
  zone_api::start(s, parse(R"({"zone":5,"duration_minutes":10})"));
  s.tick(0, 0);
  const auto r = zone_api::stop(s, parse("{}"), 100);
  TEST_ASSERT_EQUAL(202, r.status);
  TEST_ASSERT_EQUAL_STRING("cancel", str(r.body["action"]).c_str());
  TEST_ASSERT_EQUAL(4, r.body["zone"].as<int>());
  TEST_ASSERT_TRUE(s.idle());
  TEST_ASSERT_EQUAL_STRING("stop 4", p.log.back().c_str());
}

void test_stop_with_queued_but_not_started_cancels() {
  FakePort p;
  seq::Sequencer s(p);
  zone_api::start(s, parse(R"({"zone":4,"duration_minutes":10})"));
  const auto r = zone_api::stop(s, parse("{}"), 0);
  TEST_ASSERT_EQUAL(202, r.status);
  TEST_ASSERT_TRUE(r.body["zone"].isNull());
  s.tick(0, 0);
  TEST_ASSERT_EQUAL_UINT(0, p.log.size());
}

void test_stop_nothing_running_is_409() {
  FakePort p;
  seq::Sequencer s(p);
  const auto r = zone_api::stop(s, parse("{}"), 0);
  TEST_ASSERT_EQUAL(409, r.status);
  TEST_ASSERT_EQUAL_UINT(0, p.log.size());
}

void test_stop_explicit_zone() {
  FakePort p;
  seq::Sequencer s(p);
  const auto r = zone_api::stop(s, parse(R"({"zone":9})"), 0);
  TEST_ASSERT_EQUAL(202, r.status);
  TEST_ASSERT_EQUAL(9, r.body["zone"].as<int>());
  TEST_ASSERT_EQUAL_STRING("stop 9", p.log.at(0).c_str());
}

void test_stop_explicit_zone_invalid() {
  FakePort p;
  seq::Sequencer s(p);
  TEST_ASSERT_EQUAL(400, zone_api::stop(s, parse(R"({"zone":0})"), 0).status);
  TEST_ASSERT_EQUAL(400, zone_api::stop(s, parse(R"({"zone":"9"})"), 0).status);
  TEST_ASSERT_EQUAL_UINT(0, p.log.size());
}

void test_skip() {
  FakePort p;
  seq::Sequencer s(p);
  TEST_ASSERT_EQUAL(409, zone_api::skip(s, 0).status);
  zone_api::start(s, parse(R"({"zone":4,"duration_minutes":10})"));
  zone_api::start(s, parse(R"({"zone":5,"duration_minutes":10})"));
  s.tick(0, 0);
  const auto r = zone_api::skip(s, 100);
  TEST_ASSERT_EQUAL(202, r.status);
  TEST_ASSERT_EQUAL(4, r.body["zone"].as<int>());
  TEST_ASSERT_EQUAL_STRING("skipped", seq::toString(s.entries()[0].outcome));
}

void test_describe_idle_and_running() {
  ZoneController zc([](const hunter::Frame&) { return true; });
  JsonDocument doc;
  zone_api::describe(zc, 0, doc.to<JsonObject>());
  TEST_ASSERT_TRUE(doc["commanded_zone"].isNull());

  zc.start(2, 3, 1000);
  doc.clear();
  zone_api::describe(zc, 1000 + 500, doc.to<JsonObject>());
  TEST_ASSERT_EQUAL(2, doc["commanded_zone"].as<int>());
  TEST_ASSERT_EQUAL(3, doc["duration_minutes"].as<int>());
  TEST_ASSERT_EQUAL(180, doc["remaining_seconds"].as<int>());
}

void test_describe_run() {
  FakePort p;
  seq::Sequencer s(p);
  std::vector<seq::Entry> run{{1, 2, seq::Origin::kSchedule}, {2, 3, seq::Origin::kSchedule}};
  s.addRun("Fall", run);
  s.tick(0, 1800000000);
  JsonDocument doc;
  zone_api::describeRun(s, 30000, doc.to<JsonObject>());
  TEST_ASSERT_EQUAL_STRING("running", str(doc["state"]).c_str());
  TEST_ASSERT_EQUAL_STRING("Fall", str(doc["schedule"]).c_str());
  TEST_ASSERT_EQUAL(0, doc["current_index"].as<int>());
  TEST_ASSERT_EQUAL(90, doc["remaining_seconds"].as<int>());
  TEST_ASSERT_EQUAL_UINT(2, doc["entries"].size());
  TEST_ASSERT_EQUAL_STRING("running", str(doc["entries"][0]["outcome"]).c_str());
  TEST_ASSERT_EQUAL_STRING("pending", str(doc["entries"][1]["outcome"]).c_str());
  TEST_ASSERT_EQUAL_STRING("schedule", str(doc["entries"][1]["origin"]).c_str());
  TEST_ASSERT_EQUAL_UINT32(1800000000u, doc["entries"][0]["started_epoch"].as<uint32_t>());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_start_queues_manual_run);
  RUN_TEST(test_second_start_queues_behind);
  RUN_TEST(test_start_rejects_non_integer_values);
  RUN_TEST(test_start_rejects_seconds_field);
  RUN_TEST(test_start_out_of_range_messages_name_bounds);
  RUN_TEST(test_start_huge_number_rejected_not_truncated);
  RUN_TEST(test_start_non_object_body);
  RUN_TEST(test_start_queue_full_is_503);
  RUN_TEST(test_stop_cancels_run);
  RUN_TEST(test_stop_with_queued_but_not_started_cancels);
  RUN_TEST(test_stop_nothing_running_is_409);
  RUN_TEST(test_stop_explicit_zone);
  RUN_TEST(test_stop_explicit_zone_invalid);
  RUN_TEST(test_skip);
  RUN_TEST(test_describe_idle_and_running);
  RUN_TEST(test_describe_run);
  return UNITY_END();
}
