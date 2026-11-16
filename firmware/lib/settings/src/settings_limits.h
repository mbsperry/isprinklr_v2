// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>

#include "log_level.h"

// Bounds for stored settings. Zone numbers and run durations come from
// hunter_limits.h; these are the ones specific to schedules and config.
namespace settings {

constexpr std::size_t kMaxSchedules = 10;
constexpr std::size_t kMaxNameLength = 32;  // schedule and zone names, bytes
constexpr std::size_t kMaxTimezoneLength = 63;

// UDP syslog. Not 514: the server need not run as root to receive it.
constexpr int kDefaultLogPort = 5514;

// Record everything by default; debug lines are rare (bus retries, scheduler
// state changes), and this is what Phase 5 shipped with.
constexpr logging::Level kDefaultLogLevel = logging::Level::kDebug;

// Serialized settings document cap. The NVS partition is 20 KB (5 pages, one
// kept free for garbage collection), and NVS writes a new blob before erasing
// the old, so the document must fit twice with room to spare. The stored form
// is compact (settings_json.cpp); v1's real data takes under 1 KB.
constexpr std::size_t kMaxDocumentBytes = 4096;

}  // namespace settings
