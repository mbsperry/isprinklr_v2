// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#include <unity.h>

#include <ArduinoJson.h>

#include <string>
#include <vector>

#include "log_ring.h"
#include "logs_api.h"
#include "syslog_format.h"

using logging::Level;
using logging::LogEntry;
using logging::LogRing;

namespace {
// 2026-11-15T22:40:20Z
constexpr uint32_t kEpoch = 1794782420;
}  // namespace

void setUp() {}
void tearDown() {}

void test_empty_ring() {
  LogRing r(4);
  std::vector<LogEntry> out;
  TEST_ASSERT_EQUAL_UINT32(0, r.lastSeq());
  TEST_ASSERT_EQUAL_UINT32(1, r.oldestSeq());
  TEST_ASSERT_EQUAL_UINT32(0, r.since(0, Level::kDebug, 10, &out));
  TEST_ASSERT_EQUAL_UINT(0, out.size());
}

void test_push_and_read_in_order() {
  LogRing r(4);
  TEST_ASSERT_EQUAL_UINT32(1, r.push(Level::kInfo, "a", "one", 10, 0));
  TEST_ASSERT_EQUAL_UINT32(2, r.push(Level::kWarn, "b", "two", 20, kEpoch));
  std::vector<LogEntry> out;
  r.since(0, Level::kDebug, 10, &out);
  TEST_ASSERT_EQUAL_UINT(2, out.size());
  TEST_ASSERT_EQUAL_STRING("one", out[0].message);
  TEST_ASSERT_EQUAL_STRING("b", out[1].module);
  TEST_ASSERT_EQUAL_UINT32(20, out[1].uptimeMs);
  TEST_ASSERT_EQUAL_UINT32(kEpoch, out[1].epoch);

  r.since(1, Level::kDebug, 10, &out);
  TEST_ASSERT_EQUAL_UINT(1, out.size());
  TEST_ASSERT_EQUAL_UINT32(2, out[0].seq);
  r.since(2, Level::kDebug, 10, &out);
  TEST_ASSERT_EQUAL_UINT(0, out.size());
}

void test_overwrites_oldest_and_counts_missed() {
  LogRing r(3);
  for (int i = 1; i <= 7; ++i) {
    r.push(Level::kInfo, "m", std::to_string(i).c_str(), i, 0);
  }
  TEST_ASSERT_EQUAL_UINT32(7, r.lastSeq());
  TEST_ASSERT_EQUAL_UINT32(5, r.oldestSeq());
  TEST_ASSERT_EQUAL_UINT(3, r.size());

  std::vector<LogEntry> out;
  // Lines 2, 3 and 4 were overwritten before this reader saw them.
  TEST_ASSERT_EQUAL_UINT32(3, r.since(1, Level::kDebug, 10, &out));
  TEST_ASSERT_EQUAL_UINT(3, out.size());
  TEST_ASSERT_EQUAL_STRING("5", out[0].message);
  TEST_ASSERT_EQUAL_STRING("7", out[2].message);
  TEST_ASSERT_EQUAL_UINT32(0, r.since(5, Level::kDebug, 10, &out));
  TEST_ASSERT_EQUAL_UINT(2, out.size());
}

void test_limit_and_level_filter() {
  LogRing r(10);
  r.push(Level::kDebug, "m", "d", 0, 0);
  r.push(Level::kInfo, "m", "i", 0, 0);
  r.push(Level::kWarn, "m", "w", 0, 0);
  r.push(Level::kError, "m", "e", 0, 0);
  std::vector<LogEntry> out;
  r.since(0, Level::kWarn, 10, &out);
  TEST_ASSERT_EQUAL_UINT(2, out.size());
  TEST_ASSERT_EQUAL_STRING("w", out[0].message);
  TEST_ASSERT_EQUAL_STRING("e", out[1].message);
  r.since(0, Level::kDebug, 2, &out);
  TEST_ASSERT_EQUAL_UINT(2, out.size());
  TEST_ASSERT_EQUAL_STRING("i", out[1].message);
}

void test_truncates_long_text() {
  LogRing r(2);
  const std::string longMessage(500, 'x');
  r.push(Level::kInfo, "averyverylongmodule", longMessage.c_str(), 0, 0);
  std::vector<LogEntry> out;
  r.since(0, Level::kDebug, 1, &out);
  const std::string msg = out[0].message;
  TEST_ASSERT_EQUAL_UINT(logging::kMessageBytes - 1, msg.size());
  TEST_ASSERT_EQUAL_STRING("...", msg.substr(msg.size() - 3).c_str());
  TEST_ASSERT_EQUAL_UINT(logging::kModuleBytes - 1, std::string(out[0].module).size());
}

void test_levels_parse_and_print() {
  Level l;
  TEST_ASSERT_TRUE(logging::parseLevel("warn", &l));
  TEST_ASSERT_EQUAL(Level::kWarn, l);
  TEST_ASSERT_FALSE(logging::parseLevel("warning", &l));
  TEST_ASSERT_FALSE(logging::parseLevel(nullptr, &l));
  TEST_ASSERT_EQUAL_STRING("error", logging::toString(Level::kError));
}

void test_syslog_line_with_valid_clock() {
  LogEntry e;
  e.seq = 42;
  e.uptimeMs = 81234;
  e.epoch = kEpoch;
  e.level = Level::kInfo;
  std::snprintf(e.module, sizeof(e.module), "run");
  std::snprintf(e.message, sizeof(e.message), "zone 3 started for 10 min");
  TEST_ASSERT_EQUAL_STRING(
      "<134>1 2026-11-15T22:40:20Z isprinklr isprinklr - run "
      "[isp@32473 seq=\"42\" up=\"81234\" boot=\"1a2b3c4d\"] zone 3 started for 10 min",
      logging::formatSyslog(e, "isprinklr", 0x1a2b3c4d).c_str());
}

void test_syslog_line_before_clock_sync_and_odd_text() {
  LogEntry e;
  e.seq = 1;
  e.level = Level::kError;
  std::snprintf(e.module, sizeof(e.module), "a b");
  std::snprintf(e.message, sizeof(e.message), "line1\nline2");
  TEST_ASSERT_EQUAL_STRING(
      "<131>1 - isprinklr isprinklr - a_b "
      "[isp@32473 seq=\"1\" up=\"0\" boot=\"00000000\"] line1 line2",
      logging::formatSyslog(e, "isprinklr", 0).c_str());
  e.module[0] = '\0';
  const std::string line = logging::formatSyslog(e, "", 0);
  TEST_ASSERT_EQUAL_STRING("<131>1 - - isprinklr - - [", line.substr(0, 26).c_str());
}

void test_logs_query_parsing() {
  logs_api::Query q;
  api::Response bad{0, JsonDocument()};
  TEST_ASSERT_TRUE(logs_api::parseQuery(nullptr, nullptr, nullptr, &q, &bad));
  TEST_ASSERT_EQUAL_UINT32(0, q.since);
  TEST_ASSERT_EQUAL(Level::kDebug, q.maxLevel);
  TEST_ASSERT_EQUAL_UINT(200, q.limit);

  TEST_ASSERT_TRUE(logs_api::parseQuery("17", "error", "5", &q, &bad));
  TEST_ASSERT_EQUAL_UINT32(17, q.since);
  TEST_ASSERT_EQUAL(Level::kError, q.maxLevel);
  TEST_ASSERT_EQUAL_UINT(5, q.limit);

  const char* badSince[] = {"", "-1", "1x", "x", "99999999999"};
  for (const char* s : badSince) {
    TEST_ASSERT_FALSE_MESSAGE(logs_api::parseQuery(s, nullptr, nullptr, &q, &bad), s);
    TEST_ASSERT_EQUAL(400, bad.status);
  }
  TEST_ASSERT_FALSE(logs_api::parseQuery(nullptr, "verbose", nullptr, &q, &bad));
  TEST_ASSERT_FALSE(logs_api::parseQuery(nullptr, nullptr, "0", &q, &bad));
  TEST_ASSERT_FALSE(logs_api::parseQuery(nullptr, nullptr, "201", &q, &bad));
  TEST_ASSERT_EQUAL_STRING("invalid_query", bad.body["error"].as<const char*>());
}

void test_logs_response_shape() {
  LogRing r(2);
  r.push(Level::kInfo, "main", "boot", 5, 0);
  r.push(Level::kWarn, "web", "refused", 6, kEpoch);
  r.push(Level::kInfo, "run", "zone 1 started", 7, kEpoch);

  logs_api::Query q;
  logs_api::Snapshot snap = logs_api::take(r, q);
  const api::Response resp = logs_api::render(snap);
  snap.entries.clear();  // the body must own its strings
  TEST_ASSERT_EQUAL(200, resp.status);
  TEST_ASSERT_TRUE(resp.body["ok"].as<bool>());
  TEST_ASSERT_EQUAL(2, resp.body["capacity"].as<int>());
  TEST_ASSERT_EQUAL(2, resp.body["oldest_seq"].as<int>());
  TEST_ASSERT_EQUAL(3, resp.body["last_seq"].as<int>());
  TEST_ASSERT_EQUAL(1, resp.body["missed"].as<int>());
  JsonArrayConst entries = resp.body["entries"].as<JsonArrayConst>();
  TEST_ASSERT_EQUAL_UINT(2, entries.size());
  TEST_ASSERT_EQUAL_STRING("warn", entries[0]["level"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("web", entries[0]["module"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("2026-11-15T22:40:20Z", entries[0]["time"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("zone 1 started", entries[1]["message"].as<const char*>());

  LogRing early(2);
  early.push(Level::kInfo, "main", "boot", 5, 0);
  const api::Response e = logs_api::render(logs_api::take(early, q));
  TEST_ASSERT_TRUE(e.body["entries"][0]["time"].isNull());
  TEST_ASSERT_EQUAL(5, e.body["entries"][0]["uptime_ms"].as<int>());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_empty_ring);
  RUN_TEST(test_push_and_read_in_order);
  RUN_TEST(test_overwrites_oldest_and_counts_missed);
  RUN_TEST(test_limit_and_level_filter);
  RUN_TEST(test_truncates_long_text);
  RUN_TEST(test_levels_parse_and_print);
  RUN_TEST(test_syslog_line_with_valid_clock);
  RUN_TEST(test_syslog_line_before_clock_sync_and_odd_text);
  RUN_TEST(test_logs_query_parsing);
  RUN_TEST(test_logs_response_shape);
  return UNITY_END();
}
