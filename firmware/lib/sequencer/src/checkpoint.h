// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "sequencer.h"

// Reboot recovery. The device saves a checkpoint of the run on every sequencer
// change; after a reboot, once the clock is valid, recovery decides whether to
// carry on with the remaining zones (open decision 1).
//
// The zone that was running is never restarted: the Pro-C was told a bounded
// duration and finishes it on its own. Recovery only decides about the zones
// that had not started.
namespace recovery {

struct Checkpoint {
  bool inProgress = false;   // false: nothing to recover
  std::string runName;
  std::vector<seq::Entry> entries;
  uint32_t index = 0;        // entry running, starting, or next after padding
  uint32_t savedEpoch = 0;   // wall clock at save, 0 if unknown
};

Checkpoint capture(const seq::Sequencer& s, uint32_t nowEpoch);

// Compact JSON for NVS: {"n":"Fall","i":1,"t":1794755582,
//   "e":[[zone,minutes,origin,outcome,startedEpoch],...]}. An idle sequencer
// serializes to "{}".
std::string serialize(const Checkpoint& c);
bool parse(const std::string& text, Checkpoint* out);

struct Plan {
  enum class Action : uint8_t { kNothing, kResume, kSkip };
  Action action = Action::kNothing;
  // How long the zone that was running still has on the Pro-C. Resume waits
  // this out before moving on.
  uint32_t runningRemainingMs = 0;
  // Seconds between when the next zone was due and now.
  uint32_t lateSeconds = 0;
};

// Resume if the ESP is back within windowSeconds of when the next zone was
// due, otherwise skip. A window of 0 always skips; UINT32_MAX always resumes.
// Without a valid start time for the running zone, the save time stands in.
Plan plan(const Checkpoint& c, uint32_t nowEpoch, uint32_t windowSeconds);

const char* toString(Plan::Action a);

}  // namespace recovery
