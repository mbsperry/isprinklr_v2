// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <ArduinoJson.h>

#include <cstdint>
#include <vector>

#include "api_response.h"
#include "log_ring.h"

// GET /api/logs on the device: the RAM ring, newest lines last.
//
//   ?since=<seq>   only lines after this sequence number (default 0: all)
//   ?level=warn    only this level or more severe (default debug: all)
//   ?limit=<n>     at most n lines, 1-200 (default 200)
//
// Response: {"ok": true, "capacity": 200, "oldest_seq": 12, "last_seq": 211,
//            "missed": 0, "entries": [{"seq": 12, "uptime_ms": 81234,
//            "time": "2026-11-15T22:40:20Z", "level": "info",
//            "module": "sched", "message": "..."}]}
// "time" is null for lines logged before the clock was valid. "missed" counts
// lines after `since` that were already overwritten. To follow the log, pass
// the last seq you saw as `since`.
namespace logs_api {

constexpr std::size_t kMaxLimit = 200;

struct Query {
  uint32_t since = 0;
  logging::Level maxLevel = logging::Level::kDebug;
  std::size_t limit = kMaxLimit;
};

// Parameters as received; nullptr when absent. False (with a 400 in *bad) if
// any is malformed.
bool parseQuery(const char* since, const char* level, const char* limit,
                Query* out, api::Response* bad);

// What the handler copies out of the ring under its lock, so the JSON is
// built without holding it.
struct Snapshot {
  std::vector<logging::LogEntry> entries;
  uint32_t missed = 0;
  uint32_t oldestSeq = 0;
  uint32_t lastSeq = 0;
  std::size_t capacity = 0;
};

Snapshot take(const logging::LogRing& ring, const Query& q);

api::Response render(const Snapshot& snap);

}  // namespace logs_api
