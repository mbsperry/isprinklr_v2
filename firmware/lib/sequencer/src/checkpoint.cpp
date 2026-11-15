// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#include "checkpoint.h"

#include <ArduinoJson.h>

namespace recovery {

Checkpoint capture(const seq::Sequencer& s, uint32_t nowEpoch) {
  Checkpoint c;
  c.inProgress = s.runInProgress();
  if (!c.inProgress) return c;
  c.runName = s.runName();
  c.entries = s.entries();
  // First entry not yet finished: the one running, starting, or next.
  c.index = 0;
  while (c.index < c.entries.size() &&
         c.entries[c.index].outcome != seq::Outcome::kRunning &&
         c.entries[c.index].outcome != seq::Outcome::kPending) {
    ++c.index;
  }
  c.savedEpoch = nowEpoch;
  return c;
}

std::string serialize(const Checkpoint& c) {
  JsonDocument doc;
  if (c.inProgress) {
    doc["n"] = c.runName;
    doc["i"] = c.index;
    doc["t"] = c.savedEpoch;
    JsonArray entries = doc["e"].to<JsonArray>();
    for (const seq::Entry& e : c.entries) {
      JsonArray a = entries.add<JsonArray>();
      a.add(e.zone);
      a.add(e.durationMinutes);
      a.add(static_cast<int>(e.origin));
      a.add(static_cast<int>(e.outcome));
      a.add(e.startedEpoch);
    }
  } else {
    doc.to<JsonObject>();
  }
  std::string out;
  serializeJson(doc, out);
  return out;
}

bool parse(const std::string& text, Checkpoint* out) {
  JsonDocument doc;
  if (deserializeJson(doc, text) || !doc.is<JsonObject>()) return false;
  Checkpoint c;
  if (doc["e"].isNull()) {
    *out = c;  // idle
    return true;
  }
  if (!doc["n"].is<const char*>() || !doc["i"].is<uint32_t>() ||
      !doc["t"].is<uint32_t>() || !doc["e"].is<JsonArray>()) {
    return false;
  }
  c.inProgress = true;
  c.runName = doc["n"].as<const char*>();
  c.index = doc["i"].as<uint32_t>();
  c.savedEpoch = doc["t"].as<uint32_t>();
  for (JsonArrayConst a : doc["e"].as<JsonArrayConst>()) {
    if (a.size() != 5 || !a[0].is<int>() || !a[1].is<int>() ||
        !a[2].is<int>() || !a[3].is<int>() || !a[4].is<uint32_t>()) {
      return false;
    }
    const int origin = a[2].as<int>();
    const int outcome = a[3].as<int>();
    if (origin < 0 || origin > 1 || outcome < 0 || outcome > 5) return false;
    seq::Entry e{a[0].as<int>(), a[1].as<int>(), static_cast<seq::Origin>(origin)};
    e.outcome = static_cast<seq::Outcome>(outcome);
    e.startedEpoch = a[4].as<uint32_t>();
    c.entries.push_back(e);
  }
  if (c.entries.size() > seq::kMaxRunEntries || c.index > c.entries.size()) {
    return false;
  }
  *out = std::move(c);
  return true;
}

Plan plan(const Checkpoint& c, uint32_t nowEpoch, uint32_t windowSeconds) {
  Plan p;
  if (!c.inProgress || c.index >= c.entries.size()) return p;

  bool anyPending = false;
  for (std::size_t i = c.index; i < c.entries.size(); ++i) {
    anyPending |= c.entries[i].outcome == seq::Outcome::kPending;
  }

  // When the next zone was due: the end of the running zone if we know when
  // it started, else the save time.
  uint32_t nextDue = c.savedEpoch;
  const seq::Entry& cur = c.entries[c.index];
  if (cur.outcome == seq::Outcome::kRunning && cur.startedEpoch != 0) {
    const uint32_t end =
        cur.startedEpoch + static_cast<uint32_t>(cur.durationMinutes) * 60u;
    if (nowEpoch < end) p.runningRemainingMs = (end - nowEpoch) * 1000u;
    nextDue = end;
  }

  if (!anyPending) {
    // Only the running zone was left; it finishes on the Pro-C regardless.
    p.action = Plan::Action::kResume;
    return p;
  }

  if (nextDue == 0) {
    p.action = Plan::Action::kSkip;  // no idea when this was
    return p;
  }

  p.lateSeconds = nowEpoch > nextDue ? nowEpoch - nextDue : 0;
  p.action = p.lateSeconds <= windowSeconds ? Plan::Action::kResume
                                            : Plan::Action::kSkip;
  return p;
}

const char* toString(Plan::Action a) {
  switch (a) {
    case Plan::Action::kNothing: return "nothing";
    case Plan::Action::kResume: return "resumed";
    case Plan::Action::kSkip: return "skipped";
  }
  return "unknown";
}

}  // namespace recovery
