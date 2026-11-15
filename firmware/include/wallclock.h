// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <ArduinoJson.h>

// The I/O edge of timekeeping: starts SNTP and records sync events. Validity
// and local-time logic live in lib/timekeeping, which is host-tested.
namespace wallclock {

// Sets the timezone and starts SNTP polling. Safe to call before the network is
// up; SNTP retries until a server answers. Call once in setup().
void begin();

// True once SNTP has synced since boot (see timekeeping::isTimeValid for the
// policy). Schedule execution (Phase 4) must not run while this is false.
bool timeValid();

// The "time" object for GET /api/status.
void describe(JsonObject obj);

}  // namespace wallclock
