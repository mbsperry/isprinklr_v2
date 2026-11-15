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

void test_cancel_between_zones_reports_no_zone() {
  FakePort p;
  seq::Sequencer s(p);
  zone_api::start(s, parse(R"({"zone":4,"duration_minutes":1})"));
  zone_api::start(s, parse(R"({"zone":5,"duration_minutes":1})"));
  s.tick(0, 0);
  s.tick(60000, 0);  // zone 4 done, padding before 5
  const auto r = zone_api::stop(s, parse("{}"), 60100);
  TEST_ASSERT_EQUAL(202, r.status);
  TEST_ASSERT_TRUE(r.body["zone"].isNull());
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

namespace {

settings::Settings sampleSettings() {
  settings::Settings st;
  for (int z = 1; z <= 3; ++z) st.zones.push_back({z, "Z" + std::to_string(z)});
  settings::DayPattern none, sun, all;
  std::string err;
  settings::parseDayPattern("NONE", &none, &err);
  settings::parseDayPattern("SU", &sun, &err);
  settings::parseDayPattern("ALL", &all, &err);
  st.schedules.push_back({"Catch up", {{3, sun, 30}, {1, none, 5}, {2, all, 20}}});
  st.schedules.push_back({"Off", {{1, none, 5}}});
  return st;
}

}  // namespace

void test_run_schedule_now_skips_none_keeps_order() {
  FakePort p;
  seq::Sequencer s(p);
  const auto r = zone_api::runSchedule(s, sampleSettings(), parse(R"({"name":"Catch up"})"));
  TEST_ASSERT_EQUAL(202, r.status);
  TEST_ASSERT_EQUAL_STRING("Catch up", str(r.body["schedule"]).c_str());
  TEST_ASSERT_EQUAL_UINT(2, r.body["zones"].size());
  TEST_ASSERT_EQUAL(3, r.body["zones"][0].as<int>());
  TEST_ASSERT_EQUAL(2, r.body["zones"][1].as<int>());
  TEST_ASSERT_EQUAL_STRING("manual", seq::toString(s.entries()[0].origin));
  s.tick(0, 0);
  TEST_ASSERT_EQUAL_STRING("start 3 30", p.log.at(0).c_str());
}

void test_run_schedule_errors() {
  FakePort p;
  seq::Sequencer s(p);
  const auto st = sampleSettings();
  TEST_ASSERT_EQUAL(404, zone_api::runSchedule(s, st, parse(R"({"name":"Nope"})")).status);
  TEST_ASSERT_EQUAL(409, zone_api::runSchedule(s, st, parse(R"({"name":"Off"})")).status);
  TEST_ASSERT_EQUAL(400, zone_api::runSchedule(s, st, parse(R"({"name":3})")).status);
  TEST_ASSERT_EQUAL(400, zone_api::runSchedule(s, st, parse("{}")).status);
  TEST_ASSERT_EQUAL_UINT(0, s.entries().size());
}

void test_run_items_queues_all_in_order() {
  FakePort p;
  seq::Sequencer s(p);
  const auto r = zone_api::runItems(s, parse(
      R"({"items":[{"zone":5,"duration_minutes":10},{"zone":1,"duration_minutes":15}]})"));
  TEST_ASSERT_EQUAL(202, r.status);
  TEST_ASSERT_TRUE(r.body["schedule"].isNull());
  TEST_ASSERT_EQUAL_UINT(2, s.entries().size());
  TEST_ASSERT_EQUAL(5, s.entries()[0].zone);
  TEST_ASSERT_EQUAL(15, s.entries()[1].durationMinutes);
}

void test_run_items_all_or_nothing() {
  FakePort p;
  seq::Sequencer s(p);
  const char* bad[] = {
      R"({"items":[{"zone":5,"duration_minutes":10},{"zone":49,"duration_minutes":10}]})",
      R"({"items":[{"zone":5,"duration_minutes":10},{"zone":2,"duration_minutes":0}]})",
      R"({"items":[{"zone":5,"duration_minutes":10},{"zone":2}]})",
      R"({"items":[5]})",
      R"({"items":[]})",
      R"({"zones":[1,2]})",
  };
  for (const char* b : bad) {
    TEST_ASSERT_EQUAL_MESSAGE(400, zone_api::runItems(s, parse(b)).status, b);
  }
  TEST_ASSERT_EQUAL_UINT(0, s.entries().size());
}

void test_run_items_that_do_not_fit_queue_nothing() {
  FakePort p;
  seq::Sequencer s(p);
  for (std::size_t i = 0; i + 1 < seq::kMaxRunEntries; ++i) s.add(1, 1, seq::Origin::kManual);
  const auto r = zone_api::runItems(s, parse(
      R"({"items":[{"zone":5,"duration_minutes":10},{"zone":1,"duration_minutes":15}]})"));
  TEST_ASSERT_EQUAL(503, r.status);
  TEST_ASSERT_EQUAL_UINT(seq::kMaxRunEntries - 1, s.entries().size());
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
  RUN_TEST(test_cancel_between_zones_reports_no_zone);
  RUN_TEST(test_stop_nothing_running_is_409);
  RUN_TEST(test_stop_explicit_zone);
  RUN_TEST(test_stop_explicit_zone_invalid);
  RUN_TEST(test_skip);
  RUN_TEST(test_run_schedule_now_skips_none_keeps_order);
  RUN_TEST(test_run_schedule_errors);
  RUN_TEST(test_run_items_queues_all_in_order);
  RUN_TEST(test_run_items_all_or_nothing);
  RUN_TEST(test_run_items_that_do_not_fit_queue_nothing);
  RUN_TEST(test_describe_idle_and_running);
  RUN_TEST(test_describe_run);
  return UNITY_END();
}
