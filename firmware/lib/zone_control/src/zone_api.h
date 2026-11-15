// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <ArduinoJson.h>

#include <cstdint>

#include "zone_controller.h"

// The HTTP-shaped half of zone control: request bodies in, status code and JSON
// body out. Route handlers in src/web.cpp do nothing but call these, so the
// validation and response shapes are host-tested.
//
// Units: every duration field is named for its unit. The API speaks whole
// minutes because the SmartPort protocol does; v1 accepted seconds and silently
// truncated 30 s to a stop command.
namespace zone_api {

struct Response {
  int status;
  JsonDocument body;
};

// POST /api/zone/start   {"zone": 3, "duration_minutes": 10}
Response start(ZoneController& zones, JsonVariantConst request, uint32_t nowMs);

// POST /api/zone/stop    {} or {"zone": 3}
Response stop(ZoneController& zones, JsonVariantConst request, uint32_t nowMs);

// The "zones" object in GET /api/status.
void describe(const ZoneController& zones, uint32_t nowMs, JsonObject out);

}  // namespace zone_api
