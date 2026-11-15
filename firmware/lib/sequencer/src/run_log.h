// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "daily_trigger.h"
#include "log_level.h"
#include "sequencer.h"
#include "settings.h"
#include "timekeeping.h"

// The log lines a run produces: what was queued, each zone's start and how it
// ended (ran its full time, cut short by skip or cancel, or failed), whether
// it was scheduled or manual, and why a scheduled run did not start. Pure, so
// the wording is host-tested; src/sequencer_service.cpp sends them to the log.
namespace run_log {

struct Line {
  logging::Level level;
  std::string message;
};

// "35 s", "10 min", "4 min 12 s".
std::string formatDuration(uint32_t ms);

// runName and entries are the sequencer's, read when the event fires.
Line describe(const seq::Event& event, const std::string& runName,
              const std::vector<seq::Entry>& entries);

// Watches the daily trigger's decisions and reports the ones worth keeping:
//   - once per local date, at the start time, why the scheduled run did not
//     start (scheduler off, no active schedule);
//   - a missed start window, when it happens;
//   - every other change of decision, at debug level.
class TriggerReporter {
 public:
  std::vector<Line> observe(trigger::Decision decision, bool timeValid,
                            const timekeeping::LocalTime& now,
                            const settings::Settings& s);

 private:
  bool haveLast_ = false;
  trigger::Decision last_ = trigger::Decision::kTimeInvalid;
  long reportedDay_ = -1;
};

}  // namespace run_log
