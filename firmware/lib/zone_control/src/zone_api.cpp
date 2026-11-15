// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#include "zone_api.h"

#include <string>

#include "hunter_limits.h"

namespace zone_api {
namespace {

using api::error;

int statusFor(ZoneController::Result result) {
  switch (result) {
    case ZoneController::Result::kOk: return 202;
    case ZoneController::Result::kInvalidZone:
    case ZoneController::Result::kInvalidDuration: return 400;
    case ZoneController::Result::kNoActiveZone: return 409;
    case ZoneController::Result::kBusBusy: return 503;
  }
  return 500;
}

// Strict: the field must be a JSON integer. Strings, floats (including 10.0),
// booleans and null are rejected rather than coerced.
bool readInt(JsonVariantConst v, long* out) {
  if (!v.is<long>()) return false;
  *out = v.as<long>();
  return true;
}

std::string zoneMessage() {
  return "zone must be an integer " + std::to_string(hunter::kMinZone) + "-" +
         std::to_string(hunter::kMaxZone);
}

std::string durationMessage() {
  return "duration_minutes must be an integer " +
         std::to_string(hunter::kMinRunMinutes) + "-" +
         std::to_string(hunter::kMaxRunMinutes);
}

}  // namespace

Response start(ZoneController& zones, JsonVariantConst request,
               uint32_t nowMs) {
  if (!request.is<JsonObjectConst>()) {
    return error(400, "invalid_body", "expected a JSON object");
  }

  long zone = 0;
  if (!readInt(request["zone"], &zone) || !hunter::isValidZone(zone)) {
    return error(400, "invalid_zone", zoneMessage());
  }

  long minutes = 0;
  if (!readInt(request["duration_minutes"], &minutes) ||
      !hunter::isValidRunMinutes(minutes)) {
    return error(400, "invalid_duration", durationMessage());
  }

  const auto result =
      zones.start(static_cast<int>(zone), static_cast<int>(minutes), nowMs);
  if (result != ZoneController::Result::kOk) {
    return error(statusFor(result), toString(result),
                 "command not sent; nothing changed");
  }

  Response r{202, JsonDocument()};
  r.body["ok"] = true;
  r.body["action"] = "start";
  r.body["zone"] = zone;
  r.body["duration_minutes"] = minutes;
  return r;
}

Response stop(ZoneController& zones, JsonVariantConst request,
              uint32_t nowMs) {
  if (!request.is<JsonObjectConst>()) {
    return error(400, "invalid_body", "expected a JSON object; {} is fine");
  }

  std::optional<int> zone;
  JsonVariantConst zoneField = request["zone"];
  if (!zoneField.isNull()) {
    long z = 0;
    if (!readInt(zoneField, &z) || !hunter::isValidZone(z)) {
      return error(400, "invalid_zone", zoneMessage());
    }
    zone = static_cast<int>(z);
  }

  // Resolved here, before stop() clears the record, for the response body.
  std::optional<int> target = zone;
  if (!target) {
    if (const auto current = zones.active(nowMs)) target = current->zone;
  }

  const auto result = zones.stop(zone, nowMs);
  if (result == ZoneController::Result::kNoActiveZone) {
    return error(409, toString(result),
                 "no zone is known to be running; pass {\"zone\": N}");
  }
  if (result != ZoneController::Result::kOk) {
    return error(statusFor(result), toString(result),
                 "command not sent; nothing changed");
  }

  Response r{202, JsonDocument()};
  r.body["ok"] = true;
  r.body["action"] = "stop";
  r.body["zone"] = *target;
  return r;
}

void describe(const ZoneController& zones, uint32_t nowMs, JsonObject out) {
  // Named "commanded" because the ESP cannot read the Pro-C's state back.
  const auto run = zones.active(nowMs);
  if (!run) {
    out["commanded_zone"] = nullptr;
    return;
  }
  out["commanded_zone"] = run->zone;
  out["duration_minutes"] = run->durationMinutes;
  out["remaining_seconds"] = (zones.remainingMs(nowMs) + 999) / 1000;
}

}  // namespace zone_api
