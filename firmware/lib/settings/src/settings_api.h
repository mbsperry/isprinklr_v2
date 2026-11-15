// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <ArduinoJson.h>

#include "api_response.h"
#include "settings_store.h"
#include "timekeeping.h"

// Route logic for stored settings. Each PUT replaces one section, validates
// the whole result, and persists it; on any failure nothing changes. Every
// success response carries settings_hash.
//
//   GET  /api/schedules          {schedules, active, enabled, settings_hash}
//   PUT  /api/schedules          {"schedules": [...]}
//   PUT  /api/schedule/active    {"name": "Fall"}   (null or "" clears it)
//   GET  /api/schedule/enabled   {enabled, settings_hash}
//   PUT  /api/schedule/enabled   {"enabled": true}
//   GET  /api/zones              {zones, settings_hash}
//   PUT  /api/zones              {"zones": [...]}
//   GET  /api/config             {start_hour, start_minute, timezone, settings_hash}
//   PUT  /api/config             all three fields
namespace settings_api {

using Response = api::Response;

Response getSchedules(const SettingsStore& store);
Response putSchedules(SettingsStore& store, JsonVariantConst body);
Response putActive(SettingsStore& store, JsonVariantConst body);
Response getEnabled(const SettingsStore& store);
Response putEnabled(SettingsStore& store, JsonVariantConst body);
Response getZones(const SettingsStore& store);
Response putZones(SettingsStore& store, JsonVariantConst body);
Response getConfig(const SettingsStore& store);
Response putConfig(SettingsStore& store, JsonVariantConst body);

// The "settings" object in GET /api/status. With a valid local date, also lists
// what the active schedule would run that day (Phase 4 does the running).
void describe(const SettingsStore& store, const timekeeping::LocalTime* today,
              JsonObject out);

}  // namespace settings_api
