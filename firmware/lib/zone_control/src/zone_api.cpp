// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#include "zone_api.h"

#include <optional>
#include <string>

#include "hunter_limits.h"

namespace zone_api {
namespace {

using api::error;

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

Response start(seq::Sequencer& sequencer, JsonVariantConst request) {
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

  // A finished run holds only completed/interrupted/failed entries, so this
  // is 0 when idle.
  std::size_t ahead = 0;
  for (const seq::Entry& e : sequencer.entries()) {
    ahead += e.outcome == seq::Outcome::kPending ||
             e.outcome == seq::Outcome::kRunning;
  }

  if (!sequencer.add(static_cast<int>(zone), static_cast<int>(minutes),
                     seq::Origin::kManual)) {
    return error(503, "queue_full",
                 "the run already holds " +
                     std::to_string(seq::kMaxRunEntries) +
                     " entries; nothing changed");
  }

  Response r{202, JsonDocument()};
  r.body["ok"] = true;
  r.body["action"] = "start";
  r.body["zone"] = zone;
  r.body["duration_minutes"] = minutes;
  r.body["queued_behind"] = ahead;
  return r;
}

Response stop(seq::Sequencer& sequencer, JsonVariantConst request,
              uint32_t nowMs) {
  if (!request.is<JsonObjectConst>()) {
    return error(400, "invalid_body", "expected a JSON object; {} is fine");
  }

  JsonVariantConst zoneField = request["zone"];
  if (!zoneField.isNull()) {
    long z = 0;
    if (!readInt(zoneField, &z) || !hunter::isValidZone(z)) {
      return error(400, "invalid_zone", zoneMessage());
    }
    sequencer.stopZone(static_cast<int>(z), nowMs);
    Response r{202, JsonDocument()};
    r.body["ok"] = true;
    r.body["action"] = "stop";
    r.body["zone"] = z;
    return r;
  }

  const auto current = sequencer.currentIndex();
  bool queued = false;
  for (const seq::Entry& e : sequencer.entries()) {
    queued |= e.outcome == seq::Outcome::kPending;
  }
  if (!current && !queued) {
    return error(409, "no_active_zone",
                 "nothing is running or queued; pass {\"zone\": N} to stop "
                 "a zone started elsewhere");
  }

  Response r{202, JsonDocument()};
  r.body["ok"] = true;
  r.body["action"] = "cancel";
  if (current) {
    r.body["zone"] = sequencer.entries()[*current].zone;
  } else {
    r.body["zone"] = nullptr;
  }
  sequencer.cancel(nowMs);
  return r;
}

Response skip(seq::Sequencer& sequencer, uint32_t nowMs) {
  const auto current = sequencer.currentIndex();
  std::optional<int> zone;
  if (current) zone = sequencer.entries()[*current].zone;
  if (!sequencer.skip(nowMs)) {
    return error(409, "nothing_to_skip", "nothing is running or queued");
  }
  Response r{202, JsonDocument()};
  r.body["ok"] = true;
  r.body["action"] = "skip";
  if (zone) {
    r.body["zone"] = *zone;
  } else {
    r.body["zone"] = nullptr;
  }
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

void describeRun(const seq::Sequencer& sequencer, uint32_t nowMs,
                 JsonObject out) {
  out["state"] = seq::toString(sequencer.state());
  if (sequencer.runName().empty()) {
    out["schedule"] = nullptr;
  } else {
    out["schedule"] = sequencer.runName();
  }
  const auto current = sequencer.currentIndex();
  if (current) {
    out["current_index"] = *current;
  } else {
    out["current_index"] = nullptr;
  }
  if (sequencer.state() == seq::Sequencer::State::kRunning) {
    out["remaining_seconds"] = (sequencer.currentRemainingMs(nowMs) + 999) / 1000;
  }
  JsonArray entries = out["entries"].to<JsonArray>();
  for (const seq::Entry& e : sequencer.entries()) {
    JsonObject o = entries.add<JsonObject>();
    o["zone"] = e.zone;
    o["duration_minutes"] = e.durationMinutes;
    o["origin"] = seq::toString(e.origin);
    o["outcome"] = seq::toString(e.outcome);
    if (e.startedEpoch != 0) o["started_epoch"] = e.startedEpoch;
  }
}

}  // namespace zone_api
