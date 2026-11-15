// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <ArduinoJson.h>

#include <utility>

#include "sequencer.h"
#include "zone_controller.h"

// The device's sequencer: the one owner of zone commands. Holds the
// ZoneController (frame building, countdown) and the Sequencer (queue, order,
// stop-pad-start) behind one mutex. The loop task ticks it and fires the daily
// trigger; web handlers queue and cancel through with().
//
// Lock order: never hold this and the settings lock at the same time.
namespace sequencer_service {

void begin();

// Call from loop(). Advances the run, and about once a second checks whether
// today's scheduled run is due.
void tick();

void lock();
void unlock();
seq::Sequencer& sequencer();
ZoneController& zones();

// True when nothing is running, queued, or waiting on a stop retry.
bool idle();

template <typename F>
auto with(F&& fn) -> decltype(fn(sequencer())) {
  struct Guard {
    Guard() { lock(); }
    ~Guard() { unlock(); }
  } guard;
  return std::forward<F>(fn)(sequencer());
}

// The "zones", "run" and "trigger" objects for GET /api/status.
void describe(JsonObject status);

}  // namespace sequencer_service
