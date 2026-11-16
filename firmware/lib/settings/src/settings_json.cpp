// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settings_json.h"

#include <cstdio>

namespace settings {
namespace {

constexpr int kFormatVersion = 2;

bool readInt(JsonVariantConst v, const char* field, const std::string& where,
             long* out, std::string* error) {
  if (!v.is<long>()) {
    *error = where + field + " must be an integer";
    return false;
  }
  *out = v.as<long>();
  return true;
}

bool readString(JsonVariantConst v, const char* field, const std::string& where,
                std::string* out, std::string* error) {
  if (!v.is<const char*>()) {
    *error = where + field + " must be a string";
    return false;
  }
  *out = v.as<const char*>();
  return true;
}

// Values outside int range are reported as out-of-range by validate(); clamp so
// the cast cannot wrap into range.
int clampInt(long v) {
  if (v > 100000) return 100000;
  if (v < -100000) return -100000;
  return static_cast<int>(v);
}

}  // namespace

bool parseZones(JsonVariantConst array, std::vector<Zone>* out,
                std::string* error) {
  if (!array.is<JsonArrayConst>()) {
    *error = "zones must be an array";
    return false;
  }
  std::vector<Zone> zones;
  std::size_t i = 0;
  for (JsonVariantConst v : array.as<JsonArrayConst>()) {
    const std::string where = "zones[" + std::to_string(i++) + "].";
    if (!v.is<JsonObjectConst>()) {
      *error = where.substr(0, where.size() - 1) + " must be an object";
      return false;
    }
    long zone;
    Zone z;
    if (!readInt(v["zone"], "zone", where, &zone, error)) return false;
    if (!readString(v["name"], "name", where, &z.name, error)) return false;
    z.zone = clampInt(zone);
    zones.push_back(std::move(z));
  }
  *out = std::move(zones);
  return true;
}

bool parseSchedules(JsonVariantConst array, std::vector<Schedule>* out,
                    std::string* error) {
  if (!array.is<JsonArrayConst>()) {
    *error = "schedules must be an array";
    return false;
  }
  std::vector<Schedule> schedules;
  std::size_t i = 0;
  for (JsonVariantConst v : array.as<JsonArrayConst>()) {
    const std::string where = "schedules[" + std::to_string(i++) + "].";
    if (!v.is<JsonObjectConst>()) {
      *error = where.substr(0, where.size() - 1) + " must be an object";
      return false;
    }
    Schedule sch;
    if (!readString(v["name"], "name", where, &sch.name, error)) return false;

    JsonVariantConst items = v["items"];
    if (!items.is<JsonArrayConst>()) {
      *error = where + "items must be an array";
      return false;
    }
    std::size_t j = 0;
    for (JsonVariantConst it : items.as<JsonArrayConst>()) {
      const std::string iw = where + "items[" + std::to_string(j++) + "].";
      if (!it.is<JsonObjectConst>()) {
        *error = iw.substr(0, iw.size() - 1) + " must be an object";
        return false;
      }
      long zone, minutes;
      std::string days;
      if (!readInt(it["zone"], "zone", iw, &zone, error)) return false;
      if (!readString(it["days"], "days", iw, &days, error)) return false;
      if (!readInt(it["duration_minutes"], "duration_minutes", iw, &minutes,
                   error)) {
        return false;
      }
      ScheduleItem item;
      std::string dayError;
      if (!parseDayPattern(days, &item.days, &dayError)) {
        *error = iw + dayError;
        return false;
      }
      item.zone = clampInt(zone);
      item.durationMinutes = clampInt(minutes);
      sch.items.push_back(item);
    }
    schedules.push_back(std::move(sch));
  }
  *out = std::move(schedules);
  return true;
}

bool parseConfig(JsonVariantConst object, Config* out, std::string* error) {
  if (!object.is<JsonObjectConst>()) {
    *error = "config must be an object";
    return false;
  }
  long hour, minute;
  // Starts from the current config, so the optional fields keep their values
  // when a request leaves them out.
  Config c = *out;
  if (!readInt(object["start_hour"], "start_hour", "", &hour, error)) return false;
  if (!readInt(object["start_minute"], "start_minute", "", &minute, error)) {
    return false;
  }
  if (!readString(object["timezone"], "timezone", "", &c.timezone, error)) {
    return false;
  }
  if (!object["log_host"].isNull() &&
      !readString(object["log_host"], "log_host", "", &c.logHost, error)) {
    return false;
  }
  if (!object["log_port"].isNull()) {
    long port;
    if (!readInt(object["log_port"], "log_port", "", &port, error)) return false;
    c.logPort = clampInt(port);
  }
  if (!object["log_level"].isNull()) {
    JsonVariantConst level = object["log_level"];
    if (!level.is<const char*>() ||
        !logging::parseLevel(level.as<const char*>(), &c.logLevel)) {
      *error = "log_level must be one of \"error\", \"warn\", \"info\", \"debug\"";
      return false;
    }
  }
  c.startHour = clampInt(hour);
  c.startMinute = clampInt(minute);
  *out = std::move(c);
  return true;
}

void writeZones(const std::vector<Zone>& zones, JsonArray out) {
  for (const Zone& z : zones) {
    JsonObject o = out.add<JsonObject>();
    o["zone"] = z.zone;
    o["name"] = z.name;
  }
}

void writeSchedules(const std::vector<Schedule>& schedules, JsonArray out) {
  for (const Schedule& sch : schedules) {
    JsonObject o = out.add<JsonObject>();
    o["name"] = sch.name;
    JsonArray items = o["items"].to<JsonArray>();
    for (const ScheduleItem& item : sch.items) {
      JsonObject i = items.add<JsonObject>();
      i["zone"] = item.zone;
      i["days"] = formatDayPattern(item.days);
      i["duration_minutes"] = item.durationMinutes;
    }
  }
}

void writeConfig(const Config& config, JsonObject out) {
  out["start_hour"] = config.startHour;
  out["start_minute"] = config.startMinute;
  out["timezone"] = config.timezone;
  out["log_host"] = config.logHost;
  out["log_port"] = config.logPort;
  out["log_level"] = logging::toString(config.logLevel);
}

// Storage format: compact arrays instead of the API's keyed objects, about a
// third of the size, because NVS space is tight (see settings_limits.h).
//   {"f":2,"z":[[1,"Walkway Beds"],...],
//    "s":[["Fall",[[1,"TU:F",30],...]],...],
//    "a":"Fall","e":false,"c":[7,0,"PST8PDT,M3.2.0,M11.1.0"]}
// The log sink is appended to "c" ([...,"192.0.2.21",5514]) only when it is
// set, so a document written before Phase 5 serializes, and hashes, the same.
// The log level follows it ([...,"192.0.2.21",5514,"info"]) only when it is
// not the default, for the same reason.
std::string serializeDocument(const Settings& s) {
  JsonDocument doc;
  doc["f"] = kFormatVersion;
  JsonArray zones = doc["z"].to<JsonArray>();
  for (const Zone& z : s.zones) {
    JsonArray a = zones.add<JsonArray>();
    a.add(z.zone);
    a.add(z.name);
  }
  JsonArray schedules = doc["s"].to<JsonArray>();
  for (const Schedule& sch : s.schedules) {
    JsonArray a = schedules.add<JsonArray>();
    a.add(sch.name);
    JsonArray items = a.add<JsonArray>();
    for (const ScheduleItem& item : sch.items) {
      JsonArray i = items.add<JsonArray>();
      i.add(item.zone);
      i.add(formatDayPattern(item.days));
      i.add(item.durationMinutes);
    }
  }
  doc["a"] = s.activeSchedule;
  doc["e"] = s.enabled;
  JsonArray c = doc["c"].to<JsonArray>();
  c.add(s.config.startHour);
  c.add(s.config.startMinute);
  c.add(s.config.timezone);
  const bool levelSet = s.config.logLevel != kDefaultLogLevel;
  if (levelSet || !s.config.logHost.empty() ||
      s.config.logPort != kDefaultLogPort) {
    c.add(s.config.logHost);
    c.add(s.config.logPort);
  }
  if (levelSet) c.add(logging::toString(s.config.logLevel));
  std::string out;
  serializeJson(doc, out);
  return out;
}

bool parseDocument(const std::string& text, Settings* out, std::string* error) {
  JsonDocument doc;
  if (deserializeJson(doc, text)) {
    *error = "stored settings are not valid JSON";
    return false;
  }
  if (!doc["f"].is<int>() || doc["f"].as<int>() != kFormatVersion) {
    *error = "stored settings have an unknown format";
    return false;
  }

  const auto fail = [error](const char* what) {
    *error = std::string("stored settings: bad ") + what;
    return false;
  };

  Settings s;
  for (JsonVariantConst z : doc["z"].as<JsonArrayConst>()) {
    if (!z[0].is<int>() || !z[1].is<const char*>()) return fail("zone");
    s.zones.push_back(Zone{z[0].as<int>(), z[1].as<const char*>()});
  }
  for (JsonVariantConst sv : doc["s"].as<JsonArrayConst>()) {
    if (!sv[0].is<const char*>() || !sv[1].is<JsonArrayConst>()) {
      return fail("schedule");
    }
    Schedule sch;
    sch.name = sv[0].as<const char*>();
    for (JsonVariantConst iv : sv[1].as<JsonArrayConst>()) {
      ScheduleItem item;
      std::string dayError;
      if (!iv[0].is<int>() || !iv[1].is<const char*>() || !iv[2].is<int>() ||
          !parseDayPattern(iv[1].as<const char*>(), &item.days, &dayError)) {
        return fail("schedule item");
      }
      item.zone = iv[0].as<int>();
      item.durationMinutes = iv[2].as<int>();
      sch.items.push_back(item);
    }
    s.schedules.push_back(std::move(sch));
  }
  if (!doc["a"].is<const char*>() || !doc["e"].is<bool>()) return fail("flags");
  s.activeSchedule = doc["a"].as<const char*>();
  s.enabled = doc["e"].as<bool>();
  JsonVariantConst c = doc["c"];
  if (!c[0].is<int>() || !c[1].is<int>() || !c[2].is<const char*>()) {
    return fail("config");
  }
  s.config.startHour = c[0].as<int>();
  s.config.startMinute = c[1].as<int>();
  s.config.timezone = c[2].as<const char*>();
  if (c.size() > 3) {
    if (!c[3].is<const char*>() || !c[4].is<int>()) return fail("config");
    s.config.logHost = c[3].as<const char*>();
    s.config.logPort = c[4].as<int>();
  }
  if (c.size() > 5) {
    if (!c[5].is<const char*>() ||
        !logging::parseLevel(c[5].as<const char*>(), &s.config.logLevel)) {
      return fail("config");
    }
  }

  if (!validate(s, error)) return false;
  *out = std::move(s);
  return true;
}

std::string documentHash(const std::string& serialized) {
  uint32_t h = 2166136261u;
  for (unsigned char c : serialized) {
    h ^= c;
    h *= 16777619u;
  }
  char buf[9];
  std::snprintf(buf, sizeof(buf), "%08x", static_cast<unsigned>(h));
  return buf;
}

}  // namespace settings
