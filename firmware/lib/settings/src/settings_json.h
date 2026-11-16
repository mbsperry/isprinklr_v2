// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <ArduinoJson.h>

#include <cstdint>
#include <string>
#include <vector>

#include "settings.h"

// JSON shapes for settings in the HTTP API, plus the compact NVS storage
// encoding. Parsers are strict about types (integers must be JSON integers,
// strings must be strings) and ignore unknown keys. They check shape only;
// cross-field rules are settings::validate().
//
//   zone      {"zone": 1, "name": "Walkway Beds"}
//   schedule  {"name": "Fall", "items": [{"zone": 1, "days": "TU:F",
//                                         "duration_minutes": 20}]}
//   config    {"start_hour": 7, "start_minute": 0,
//              "timezone": "PST8PDT,M3.2.0,M11.1.0", "log_host": "",
//              "log_port": 5514, "log_level": "debug"}
namespace settings {

bool parseZones(JsonVariantConst array, std::vector<Zone>* out,
                std::string* error);
bool parseSchedules(JsonVariantConst array, std::vector<Schedule>* out,
                    std::string* error);
bool parseConfig(JsonVariantConst object, Config* out, std::string* error);

void writeZones(const std::vector<Zone>& zones, JsonArray out);
void writeSchedules(const std::vector<Schedule>& schedules, JsonArray out);
void writeConfig(const Config& config, JsonObject out);

// The whole document in its compact storage form, canonical (fixed order, day
// patterns normalised), so equal settings always serialize to equal bytes.
std::string serializeDocument(const Settings& s);

// Parses and validates a stored document.
bool parseDocument(const std::string& text, Settings* out, std::string* error);

// 8 hex digits, FNV-1a over serializeDocument(). Reported in status so the
// server can tell whether its backup matches the device.
std::string documentHash(const std::string& serialized);

}  // namespace settings
