// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#include "logs_api.h"

#include <cerrno>
#include <cstdlib>

#include "timekeeping.h"

namespace logs_api {
namespace {

// Whole decimal number, no sign, no trailing junk.
bool parseUnsigned(const char* text, unsigned long max, unsigned long* out) {
  if (text == nullptr || *text < '0' || *text > '9') return false;
  errno = 0;
  char* end = nullptr;
  const unsigned long v = std::strtoul(text, &end, 10);
  if (errno != 0 || *end != '\0' || v > max) return false;
  *out = v;
  return true;
}

}  // namespace

bool parseQuery(const char* since, const char* level, const char* limit,
                Query* out, api::Response* bad) {
  Query q;
  unsigned long v = 0;
  if (since != nullptr) {
    if (!parseUnsigned(since, 0xFFFFFFFFul, &v)) {
      *bad = api::error(400, "invalid_query", "since must be a whole number");
      return false;
    }
    q.since = static_cast<uint32_t>(v);
  }
  if (level != nullptr && !logging::parseLevel(level, &q.maxLevel)) {
    *bad = api::error(400, "invalid_query",
                      "level must be error, warn, info or debug");
    return false;
  }
  if (limit != nullptr) {
    if (!parseUnsigned(limit, kMaxLimit, &v) || v == 0) {
      *bad = api::error(400, "invalid_query",
                        "limit must be 1-" + std::to_string(kMaxLimit));
      return false;
    }
    q.limit = v;
  }
  *out = q;
  return true;
}

Snapshot take(const logging::LogRing& ring, const Query& q) {
  Snapshot s;
  s.missed = ring.since(q.since, q.maxLevel, q.limit, &s.entries);
  s.oldestSeq = ring.oldestSeq();
  s.lastSeq = ring.lastSeq();
  s.capacity = ring.capacity();
  return s;
}

api::Response render(const Snapshot& snap) {
  api::Response r{200, JsonDocument()};
  r.body["ok"] = true;
  r.body["capacity"] = snap.capacity;
  r.body["oldest_seq"] = snap.oldestSeq;
  r.body["last_seq"] = snap.lastSeq;
  r.body["missed"] = snap.missed;
  JsonArray entries = r.body["entries"].to<JsonArray>();
  for (const logging::LogEntry& e : snap.entries) {
    JsonObject o = entries.add<JsonObject>();
    o["seq"] = e.seq;
    o["uptime_ms"] = e.uptimeMs;
    if (e.epoch >= timekeeping::kMinPlausibleEpoch) {
      o["time"] = timekeeping::formatUtcIso8601(static_cast<std::time_t>(e.epoch));
    } else {
      o["time"] = nullptr;
    }
    o["level"] = logging::toString(e.level);
    // As const char*, which ArduinoJson copies. A char array would be taken
    // for a string literal and stored by pointer, outliving the snapshot.
    o["module"] = static_cast<const char*>(e.module);
    o["message"] = static_cast<const char*>(e.message);
  }
  return r;
}

}  // namespace logs_api
