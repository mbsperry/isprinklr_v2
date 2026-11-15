// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <ArduinoJson.h>

#include <cstdint>

#include "api_response.h"
#include "sequencer.h"
#include "zone_controller.h"

// The HTTP-shaped half of zone control: request bodies in, status code and JSON
// body out. Route handlers in src/web.cpp do nothing but call these, so the
// validation and response shapes are host-tested.
//
// Units: every duration field is named for its unit. The API speaks whole
// minutes because the SmartPort protocol does; v1 accepted seconds and silently
// truncated 30 s to a stop command.
namespace zone_api {

using Response = api::Response;

// Since Phase 4 both go through the sequencer, the only owner of zone commands.

// POST /api/zone/start   {"zone": 3, "duration_minutes": 10}
// Queues a manual run. If a run is in progress it goes on the end; it never
// cuts in. 202 with "queued_behind": how many entries are ahead of it.
Response start(seq::Sequencer& sequencer, JsonVariantConst request);

// POST /api/zone/stop    {} or {"zone": 3}
// {} cancels the run: stops the running zone and drops everything queued.
// {"zone": N} stops N; if N is the running zone that is also a cancel.
// Never refused for being busy.
Response stop(seq::Sequencer& sequencer, JsonVariantConst request,
              uint32_t nowMs);

// POST /api/run/skip    {}
// Skips the zone the run is on (or about to start) and moves to the next.
// 409 if nothing is running or queued.
Response skip(seq::Sequencer& sequencer, uint32_t nowMs);

// The "zones" object in GET /api/status: the zone last commanded and its
// countdown, the same shape for manual and scheduled runs.
void describe(const ZoneController& zones, uint32_t nowMs, JsonObject out);

// The "run" object in GET /api/status: the sequencer's state and the current
// (or last) run, entry by entry.
void describeRun(const seq::Sequencer& sequencer, uint32_t nowMs,
                 JsonObject out);

}  // namespace zone_api
