#include "settings_api.h"

#include <cstdio>

#include "settings_json.h"

namespace settings_api {
namespace {

using api::error;
using settings::Settings;

Response ok(const SettingsStore& store) {
  Response r{200, JsonDocument()};
  r.body["ok"] = true;
  r.body["settings_hash"] = store.hash();
  return r;
}

Response save(SettingsStore& store, const Settings& next) {
  std::string message;
  switch (store.replace(next, &message)) {
    case SettingsStore::SaveResult::kOk: return ok(store);
    case SettingsStore::SaveResult::kInvalid:
      return error(400, "invalid_settings", message);
    case SettingsStore::SaveResult::kTooLarge:
      return error(413, "too_large", message);
    case SettingsStore::SaveResult::kStorageFailed:
      return error(500, "storage_failed", message);
  }
  return error(500, "internal", "unexpected save result");
}

bool requireObject(JsonVariantConst body, Response* bad) {
  if (body.is<JsonObjectConst>()) return true;
  *bad = error(400, "invalid_body", "expected a JSON object");
  return false;
}

}  // namespace

Response getSchedules(const SettingsStore& store) {
  Response r = ok(store);
  const Settings& s = store.current();
  settings::writeSchedules(s.schedules, r.body["schedules"].to<JsonArray>());
  if (s.activeSchedule.empty()) {
    r.body["active"] = nullptr;
  } else {
    r.body["active"] = s.activeSchedule;
  }
  r.body["enabled"] = s.enabled;
  return r;
}

Response putSchedules(SettingsStore& store, JsonVariantConst body) {
  Response bad{0, JsonDocument()};
  if (!requireObject(body, &bad)) return bad;
  Settings next = store.current();
  std::string message;
  if (!settings::parseSchedules(body["schedules"], &next.schedules, &message)) {
    return error(400, "invalid_body", message);
  }
  Response r = save(store, next);
  return r.status == 200 ? getSchedules(store) : r;
}

Response putActive(SettingsStore& store, JsonVariantConst body) {
  Response bad{0, JsonDocument()};
  if (!requireObject(body, &bad)) return bad;
  JsonVariantConst name = body["name"];
  Settings next = store.current();
  if (name.isNull()) {
    next.activeSchedule.clear();
  } else if (name.is<const char*>()) {
    next.activeSchedule = name.as<const char*>();
  } else {
    return error(400, "invalid_body", "name must be a string or null");
  }
  Response r = save(store, next);
  return r.status == 200 ? getSchedules(store) : r;
}

Response getEnabled(const SettingsStore& store) {
  Response r = ok(store);
  r.body["enabled"] = store.current().enabled;
  return r;
}

Response putEnabled(SettingsStore& store, JsonVariantConst body) {
  Response bad{0, JsonDocument()};
  if (!requireObject(body, &bad)) return bad;
  if (!body["enabled"].is<bool>()) {
    return error(400, "invalid_body", "enabled must be true or false");
  }
  Settings next = store.current();
  next.enabled = body["enabled"].as<bool>();
  Response r = save(store, next);
  return r.status == 200 ? getEnabled(store) : r;
}

Response getZones(const SettingsStore& store) {
  Response r = ok(store);
  settings::writeZones(store.current().zones, r.body["zones"].to<JsonArray>());
  return r;
}

Response putZones(SettingsStore& store, JsonVariantConst body) {
  Response bad{0, JsonDocument()};
  if (!requireObject(body, &bad)) return bad;
  Settings next = store.current();
  std::string message;
  if (!settings::parseZones(body["zones"], &next.zones, &message)) {
    return error(400, "invalid_body", message);
  }
  Response r = save(store, next);
  return r.status == 200 ? getZones(store) : r;
}

Response getConfig(const SettingsStore& store) {
  Response r = ok(store);
  settings::writeConfig(store.current().config, r.body.as<JsonObject>());
  return r;
}

Response putConfig(SettingsStore& store, JsonVariantConst body) {
  Settings next = store.current();
  std::string message;
  if (!settings::parseConfig(body, &next.config, &message)) {
    return error(400, "invalid_body", message);
  }
  Response r = save(store, next);
  return r.status == 200 ? getConfig(store) : r;
}

void describe(const SettingsStore& store, const timekeeping::LocalTime* today,
              JsonObject out) {
  const Settings& s = store.current();
  out["load"] = toString(store.loadState());
  if (store.loadState() == SettingsStore::LoadState::kCorrupt) {
    out["load_error"] = store.loadError();
  }
  out["hash"] = store.hash();
  out["zones"] = s.zones.size();
  out["schedules"] = s.schedules.size();
  if (s.activeSchedule.empty()) {
    out["active_schedule"] = nullptr;
  } else {
    out["active_schedule"] = s.activeSchedule;
  }
  out["enabled"] = s.enabled;

  char start[6];
  std::snprintf(start, sizeof(start), "%02d:%02d", s.config.startHour,
                s.config.startMinute);
  out["start_time"] = start;

  const settings::Schedule* active = settings::findSchedule(s, s.activeSchedule);
  if (today != nullptr && active != nullptr) {
    JsonArray runs = out["today"].to<JsonArray>();
    for (const settings::PlannedRun& run : settings::runsForDate(*active, *today)) {
      JsonObject o = runs.add<JsonObject>();
      o["zone"] = run.zone;
      o["duration_minutes"] = run.durationMinutes;
    }
  }
}

}  // namespace settings_api
