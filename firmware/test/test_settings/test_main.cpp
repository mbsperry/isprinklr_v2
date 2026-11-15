#include <unity.h>

#include <ArduinoJson.h>

#include <string>

#include "memory_kv_store.h"
#include "settings_api.h"
#include "settings_json.h"
#include "settings_limits.h"
#include "settings_store.h"
#include "v1_fixture.h"

using Load = SettingsStore::LoadState;

void setUp() {}
void tearDown() {}

namespace {

JsonDocument g_body;

JsonVariantConst body(const char* json) {
  g_body.clear();
  TEST_ASSERT_FALSE(deserializeJson(g_body, json));
  return g_body.as<JsonVariantConst>();
}

std::string str(JsonVariantConst v) { return v.as<std::string>(); }

// A store with the real v1 zones and schedules loaded through the API.
void loadV1(SettingsStore& store) {
  TEST_ASSERT_EQUAL(200, settings_api::putZones(store, body(kV1Zones)).status);
  const auto r = settings_api::putSchedules(store, body(kV1Schedules));
  TEST_ASSERT_EQUAL_MESSAGE(200, r.status, str(r.body["message"]).c_str());
}

}  // namespace

void test_fresh_store_is_empty_and_disabled() {
  MemoryKvStore kv;
  SettingsStore store(kv);
  TEST_ASSERT_EQUAL(Load::kEmpty, store.load());
  TEST_ASSERT_FALSE(store.current().enabled);
  TEST_ASSERT_EQUAL_UINT(0, store.current().schedules.size());
  TEST_ASSERT_EQUAL_UINT(8, store.hash().size());
}

void test_v1_data_imports_and_fits() {
  MemoryKvStore kv;
  SettingsStore store(kv);
  store.load();
  loadV1(store);
  TEST_ASSERT_EQUAL_UINT(8, store.current().zones.size());
  TEST_ASSERT_EQUAL_UINT(5, store.current().schedules.size());
  const std::string doc = settings::serializeDocument(store.current());
  printf("v1 document: %zu bytes\n", doc.size());
  TEST_ASSERT_TRUE(doc.size() < settings::kMaxDocumentBytes / 4);
}

void test_round_trip_through_flash() {
  MemoryKvStore kv;
  std::string hash;
  {
    SettingsStore store(kv);
    store.load();
    loadV1(store);
    TEST_ASSERT_EQUAL(200, settings_api::putActive(store, body(R"({"name":"Late Fall"})")).status);
    TEST_ASSERT_EQUAL(200, settings_api::putConfig(store, body(
        R"({"start_hour":6,"start_minute":30,"timezone":"PST8PDT,M3.2.0,M11.1.0"})")).status);
    hash = store.hash();
  }
  SettingsStore reloaded(kv);
  TEST_ASSERT_EQUAL(Load::kLoaded, reloaded.load());
  TEST_ASSERT_EQUAL_STRING("Late Fall", reloaded.current().activeSchedule.c_str());
  TEST_ASSERT_EQUAL(6, reloaded.current().config.startHour);
  TEST_ASSERT_EQUAL(30, reloaded.current().config.startMinute);
  TEST_ASSERT_EQUAL_STRING(hash.c_str(), reloaded.hash().c_str());
}

void test_hash_changes_with_content_and_not_with_formatting() {
  MemoryKvStore kv;
  SettingsStore store(kv);
  store.load();
  loadV1(store);
  const std::string before = store.hash();
  // Same schedules, different case and day order: canonical form is equal.
  settings_api::putSchedules(store, body(kV1Schedules));
  TEST_ASSERT_EQUAL_STRING(before.c_str(), store.hash().c_str());
  settings_api::putEnabled(store, body(R"({"enabled":true})"));
  TEST_ASSERT_NOT_EQUAL(0, before.compare(store.hash()));
}

void test_days_are_canonicalised() {
  MemoryKvStore kv;
  SettingsStore store(kv);
  store.load();
  loadV1(store);
  const auto r = settings_api::getSchedules(store);
  // v1 "Tu:F" comes back as "TU:F".
  TEST_ASSERT_EQUAL_STRING("TU:F", str(r.body["schedules"][0]["items"][0]["days"]).c_str());
  TEST_ASSERT_EQUAL(20, r.body["schedules"][0]["items"][0]["duration_minutes"].as<int>());
}

void test_schedule_with_unknown_zone_rejected_and_nothing_written() {
  MemoryKvStore kv;
  SettingsStore store(kv);
  store.load();
  loadV1(store);
  const int writes = kv.blobWrites;
  const std::string hash = store.hash();
  const auto r = settings_api::putSchedules(store, body(
      R"({"schedules":[{"name":"X","items":[{"zone":9,"days":"M","duration_minutes":10}]}]})"));
  TEST_ASSERT_EQUAL(400, r.status);
  TEST_ASSERT_EQUAL_STRING("invalid_settings", str(r.body["error"]).c_str());
  TEST_ASSERT_EQUAL(writes, kv.blobWrites);
  TEST_ASSERT_EQUAL_STRING(hash.c_str(), store.hash().c_str());
  TEST_ASSERT_EQUAL_UINT(5, store.current().schedules.size());
}

void test_item_validation() {
  MemoryKvStore kv;
  SettingsStore store(kv);
  store.load();
  settings_api::putZones(store, body(R"({"zones":[{"zone":1,"name":"A"},{"zone":2,"name":"B"}]})"));
  const char* bad[] = {
      R"({"schedules":[{"name":"S","items":[{"zone":1,"days":"M","duration_minutes":0}]}]})",
      R"({"schedules":[{"name":"S","items":[{"zone":1,"days":"M","duration_minutes":241}]}]})",
      R"({"schedules":[{"name":"S","items":[{"zone":1,"days":"M","duration_minutes":20.5}]}]})",
      R"({"schedules":[{"name":"S","items":[{"zone":1,"days":"M","duration":1200}]}]})",
      R"({"schedules":[{"name":"S","items":[{"zone":1,"days":"MO","duration_minutes":5}]}]})",
      R"({"schedules":[{"name":"S","items":[{"zone":1,"days":"M","duration_minutes":5},{"zone":1,"days":"W","duration_minutes":5}]}]})",
      R"({"schedules":[{"name":"","items":[]}]})",
      R"({"schedules":[{"name":"S","items":[]},{"name":"S","items":[]}]})",
      R"({"schedules":[{"name":"123456789012345678901234567890123","items":[]}]})",
      R"({"schedules":{}})",
      R"({"schedule":[]})",
  };
  for (const char* b : bad) {
    TEST_ASSERT_EQUAL_MESSAGE(400, settings_api::putSchedules(store, body(b)).status, b);
  }
  TEST_ASSERT_EQUAL_UINT(0, store.current().schedules.size());
}

void test_too_many_schedules() {
  MemoryKvStore kv;
  SettingsStore store(kv);
  store.load();
  std::string b = R"({"schedules":[)";
  for (std::size_t i = 0; i <= settings::kMaxSchedules; ++i) {
    if (i) b += ",";
    b += R"({"name":"S)" + std::to_string(i) + R"(","items":[]})";
  }
  b += "]}";
  TEST_ASSERT_EQUAL(400, settings_api::putSchedules(store, body(b.c_str())).status);
}

void test_oversize_document_is_413() {
  MemoryKvStore kv;
  SettingsStore store(kv);
  store.load();
  // 48 zones with 32-byte names, then 10 schedules each covering every zone.
  std::string zones = R"({"zones":[)";
  for (int z = 1; z <= 48; ++z) {
    if (z > 1) zones += ",";
    zones += R"({"zone":)" + std::to_string(z) + R"(,"name":"abcdefghijabcdefghijabcdefghij)" +
             (z < 10 ? "0" : "") + std::to_string(z) + R"("})";
  }
  zones += "]}";
  TEST_ASSERT_EQUAL(200, settings_api::putZones(store, body(zones.c_str())).status);
  std::string sch = R"({"schedules":[)";
  for (int s = 0; s < 10; ++s) {
    if (s) sch += ",";
    sch += R"({"name":"S)" + std::to_string(s) + R"(","items":[)";
    for (int z = 1; z <= 48; ++z) {
      if (z > 1) sch += ",";
      sch += R"({"zone":)" + std::to_string(z) + R"(,"days":"M:TU:W:TH:F","duration_minutes":10})";
    }
    sch += "]}";
  }
  sch += "]}";
  JsonDocument big;
  TEST_ASSERT_FALSE(deserializeJson(big, sch));
  const auto r = settings_api::putSchedules(store, big.as<JsonVariantConst>());
  TEST_ASSERT_EQUAL(413, r.status);
  TEST_ASSERT_EQUAL_UINT(0, store.current().schedules.size());
}

void test_active_must_exist_and_cannot_be_orphaned() {
  MemoryKvStore kv;
  SettingsStore store(kv);
  store.load();
  loadV1(store);
  TEST_ASSERT_EQUAL(400, settings_api::putActive(store, body(R"({"name":"Winter"})")).status);
  TEST_ASSERT_EQUAL(200, settings_api::putActive(store, body(R"({"name":"Fall"})")).status);
  // Replacing schedules without "Fall" would orphan the active name.
  TEST_ASSERT_EQUAL(400, settings_api::putSchedules(store, body(R"({"schedules":[]})")).status);
  TEST_ASSERT_EQUAL(200, settings_api::putActive(store, body(R"({"name":null})")).status);
  TEST_ASSERT_TRUE(store.current().activeSchedule.empty());
  TEST_ASSERT_EQUAL(200, settings_api::putSchedules(store, body(R"({"schedules":[]})")).status);
}

void test_zone_removal_blocked_while_scheduled() {
  MemoryKvStore kv;
  SettingsStore store(kv);
  store.load();
  loadV1(store);
  const auto r = settings_api::putZones(store, body(R"({"zones":[{"zone":1,"name":"A"}]})"));
  TEST_ASSERT_EQUAL(400, r.status);
  TEST_ASSERT_EQUAL_UINT(8, store.current().zones.size());
}

void test_zone_validation() {
  MemoryKvStore kv;
  SettingsStore store(kv);
  store.load();
  const char* bad[] = {
      R"({"zones":[{"zone":0,"name":"A"}]})",
      R"({"zones":[{"zone":49,"name":"A"}]})",
      R"({"zones":[{"zone":1,"name":""}]})",
      R"({"zones":[{"zone":1,"name":"A"},{"zone":1,"name":"B"}]})",
      R"({"zones":[{"zone":"1","name":"A"}]})",
      R"({"zones":[{"zone":1}]})",
  };
  for (const char* b : bad) {
    TEST_ASSERT_EQUAL_MESSAGE(400, settings_api::putZones(store, body(b)).status, b);
  }
}

void test_enabled_requires_boolean() {
  MemoryKvStore kv;
  SettingsStore store(kv);
  store.load();
  TEST_ASSERT_EQUAL(400, settings_api::putEnabled(store, body(R"({"enabled":1})")).status);
  TEST_ASSERT_EQUAL(400, settings_api::putEnabled(store, body(R"({"enabled":"true"})")).status);
  TEST_ASSERT_EQUAL(200, settings_api::putEnabled(store, body(R"({"enabled":true})")).status);
  TEST_ASSERT_TRUE(settings_api::getEnabled(store).body["enabled"].as<bool>());
}

void test_config_validation() {
  MemoryKvStore kv;
  SettingsStore store(kv);
  store.load();
  const char* bad[] = {
      R"({"start_hour":24,"start_minute":0,"timezone":"PST8PDT"})",
      R"({"start_hour":7,"start_minute":60,"timezone":"PST8PDT"})",
      R"({"start_hour":-1,"start_minute":0,"timezone":"PST8PDT"})",
      R"({"start_hour":7,"start_minute":0,"timezone":""})",
      R"({"start_hour":7,"start_minute":0})",
      R"({"start_hour":"7","start_minute":0,"timezone":"PST8PDT"})",
  };
  for (const char* b : bad) {
    TEST_ASSERT_EQUAL_MESSAGE(400, settings_api::putConfig(store, body(b)).status, b);
  }
  const auto r = settings_api::getConfig(store);
  TEST_ASSERT_EQUAL(7, r.body["start_hour"].as<int>());
  TEST_ASSERT_EQUAL_STRING(timekeeping::kPacificTz, str(r.body["timezone"]).c_str());
}

void test_storage_failure_changes_nothing() {
  MemoryKvStore kv;
  SettingsStore store(kv);
  store.load();
  loadV1(store);
  kv.failWrites = true;
  const auto r = settings_api::putEnabled(store, body(R"({"enabled":true})"));
  TEST_ASSERT_EQUAL(500, r.status);
  TEST_ASSERT_FALSE(store.current().enabled);
}

void test_wrong_format_version_is_corrupt() {
  MemoryKvStore kv;
  kv.putBlob("settings", "doc", R"({"f":1,"z":[],"s":[],"a":"","e":false,"c":[7,0,"PST8PDT"]})");
  SettingsStore store(kv);
  TEST_ASSERT_EQUAL(Load::kCorrupt, store.load());
}

void test_corrupt_blob_loads_safe_defaults() {
  MemoryKvStore kv;
  kv.putBlob("settings", "doc", "{not json");
  SettingsStore store(kv);
  TEST_ASSERT_EQUAL(Load::kCorrupt, store.load());
  TEST_ASSERT_FALSE(store.current().enabled);
  TEST_ASSERT_EQUAL_UINT(0, store.current().schedules.size());
  TEST_ASSERT_FALSE(store.loadError().empty());
}

void test_invalid_stored_document_is_corrupt() {
  // Well-formed JSON that breaks a rule (schedule names a missing zone).
  MemoryKvStore kv;
  kv.putBlob("settings", "doc",
             R"({"f":2,"z":[],"s":[["S",[[3,"M",5]]]],"a":"","e":true,"c":[7,0,"PST8PDT"]})");
  SettingsStore store(kv);
  TEST_ASSERT_EQUAL(Load::kCorrupt, store.load());
  TEST_ASSERT_FALSE(store.current().enabled);
}

void test_describe_lists_todays_runs() {
  MemoryKvStore kv;
  SettingsStore store(kv);
  store.load();
  loadV1(store);
  settings_api::putActive(store, body(R"({"name":"Late Fall"})"));
  // Late Fall: TH zones 1-4; TU:F zones 5 and 7. 2026-11-20 is a Friday.
  timekeeping::LocalTime friday{2026, 11, 20, 5, 6, 0, 0, false, -480};
  JsonDocument doc;
  settings_api::describe(store, &friday, doc.to<JsonObject>());
  TEST_ASSERT_EQUAL_STRING("loaded", str(doc["load"]).c_str());
  TEST_ASSERT_EQUAL_STRING("Late Fall", str(doc["active_schedule"]).c_str());
  TEST_ASSERT_EQUAL_STRING("07:00", str(doc["start_time"]).c_str());
  TEST_ASSERT_EQUAL_UINT(2, doc["today"].size());
  TEST_ASSERT_EQUAL(5, doc["today"][0]["zone"].as<int>());
  TEST_ASSERT_EQUAL(7, doc["today"][1]["zone"].as<int>());

  JsonDocument noTime;
  settings_api::describe(store, nullptr, noTime.to<JsonObject>());
  TEST_ASSERT_TRUE(noTime["today"].isNull());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_fresh_store_is_empty_and_disabled);
  RUN_TEST(test_v1_data_imports_and_fits);
  RUN_TEST(test_round_trip_through_flash);
  RUN_TEST(test_hash_changes_with_content_and_not_with_formatting);
  RUN_TEST(test_days_are_canonicalised);
  RUN_TEST(test_schedule_with_unknown_zone_rejected_and_nothing_written);
  RUN_TEST(test_item_validation);
  RUN_TEST(test_too_many_schedules);
  RUN_TEST(test_oversize_document_is_413);
  RUN_TEST(test_active_must_exist_and_cannot_be_orphaned);
  RUN_TEST(test_zone_removal_blocked_while_scheduled);
  RUN_TEST(test_zone_validation);
  RUN_TEST(test_enabled_requires_boolean);
  RUN_TEST(test_config_validation);
  RUN_TEST(test_storage_failure_changes_nothing);
  RUN_TEST(test_wrong_format_version_is_corrupt);
  RUN_TEST(test_corrupt_blob_loads_safe_defaults);
  RUN_TEST(test_invalid_stored_document_is_corrupt);
  RUN_TEST(test_describe_lists_todays_runs);
  return UNITY_END();
}
