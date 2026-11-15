// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#include "zone_api.h"

#include <optional>
#include <string>
#include <vector>

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
  // Only a zone actually running is reported; between zones the index
  // already points at the next one, which never started.
  if (current &&
      sequencer.entries()[*current].outcome == seq::Outcome::kRunning) {
    r.body["zone"] = sequencer.entries()[*current].zone;
  } else {
    r.body["zone"] = nullptr;
  }
  sequencer.cancel(nowMs);
  return r;
}

namespace {

std::size_t entriesAhead(const seq::Sequencer& sequencer) {
  std::size_t ahead = 0;
  for (const seq::Entry& e : sequencer.entries()) {
    ahead += e.outcome == seq::Outcome::kPending ||
             e.outcome == seq::Outcome::kRunning;
  }
  return ahead;
}

Response queued(const char* action, const std::string& name,
                const std::vector<seq::Entry>& entries, std::size_t ahead) {
  Response r{202, JsonDocument()};
  r.body["ok"] = true;
  r.body["action"] = action;
  if (name.empty()) {
    r.body["schedule"] = nullptr;
  } else {
    r.body["schedule"] = name;
  }
  JsonArray zones = r.body["zones"].to<JsonArray>();
  for (const seq::Entry& e : entries) zones.add(e.zone);
  r.body["queued_behind"] = ahead;
  return r;
}

Response full() {
  return error(503, "queue_full",
               "the run would exceed " + std::to_string(seq::kMaxRunEntries) +
                   " entries; nothing queued");
}

}  // namespace

Response runSchedule(seq::Sequencer& sequencer, const settings::Settings& s,
                     JsonVariantConst request) {
  if (!request.is<JsonObjectConst>() || !request["name"].is<const char*>()) {
    return error(400, "invalid_body", "expected {\"name\": \"<schedule>\"}");
  }
  const std::string name = request["name"].as<const char*>();
  const settings::Schedule* schedule = settings::findSchedule(s, name);
  if (schedule == nullptr) {
    return error(404, "unknown_schedule", "no schedule named '" + name + "'");
  }

  std::vector<seq::Entry> entries;
  for (const settings::ScheduleItem& item : schedule->items) {
    if (item.days.kind == settings::DayPattern::Kind::kNone) continue;
    entries.push_back(seq::Entry{item.zone, item.durationMinutes, seq::Origin::kManual});
  }
  if (entries.empty()) {
    return error(409, "nothing_to_run",
                 "every zone in '" + name + "' is NONE");
  }

  const std::size_t ahead = entriesAhead(sequencer);
  if (!sequencer.addRun(name, entries, seq::Origin::kManual)) return full();
  return queued("run_schedule", name, entries, ahead);
}

Response runItems(seq::Sequencer& sequencer, JsonVariantConst request) {
  if (!request.is<JsonObjectConst>() || !request["items"].is<JsonArrayConst>()) {
    return error(400, "invalid_body",
                 "expected {\"items\": [{\"zone\": N, \"duration_minutes\": M}, ...]}");
  }
  std::vector<seq::Entry> entries;
  std::size_t i = 0;
  for (JsonVariantConst item : request["items"].as<JsonArrayConst>()) {
    const std::string where = "items[" + std::to_string(i++) + "]: ";
    long zone = 0, minutes = 0;
    if (!item.is<JsonObjectConst>() || !readInt(item["zone"], &zone) ||
        !hunter::isValidZone(zone)) {
      return error(400, "invalid_zone", where + zoneMessage());
    }
    if (!readInt(item["duration_minutes"], &minutes) ||
        !hunter::isValidRunMinutes(minutes)) {
      return error(400, "invalid_duration", where + durationMessage());
    }
    entries.push_back(seq::Entry{static_cast<int>(zone), static_cast<int>(minutes),
                                 seq::Origin::kManual});
  }
  if (entries.empty()) {
    return error(400, "invalid_body", "items is empty");
  }

  const std::size_t ahead = entriesAhead(sequencer);
  if (!sequencer.addRun("", entries, seq::Origin::kManual)) return full();
  return queued("run", "", entries, ahead);
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
