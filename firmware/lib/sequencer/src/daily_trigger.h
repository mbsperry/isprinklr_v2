// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

#include "settings.h"
#include "timekeeping.h"

// Whether today's scheduled run should start now. Pure; the device calls it
// from the loop and persists the last-run day (see LastRunStore).
//
// The run is due once per local date, from the configured start time until
// kStartWindowMinutes later. The window lets a device that was rebooting or
// waiting for time at 07:00 still run at 07:20, without watering at midday
// after a long outage. The last-run day is recorded when the run starts, so a
// reboot after that never starts it twice (the reboot policy decides what
// happens to the rest of an interrupted run).
//
// DST: on the spring-forward day a start inside the skipped hour fires at the
// first minute after it; on the fall-back day the repeated hour cannot fire
// twice because the date has already been recorded.
namespace trigger {

constexpr int kStartWindowMinutes = 60;

enum class Decision : uint8_t {
  kDue,
  kTimeInvalid,
  kDisabled,
  kNoActiveSchedule,
  kAlreadyRan,     // today's date is the recorded last-run day
  kBeforeStart,
  kWindowMissed,   // later than start + window; today is skipped
};

// lastRunDay: timekeeping::daysSinceEpoch of the last run's local date, or -1.
Decision decide(bool timeValid, const timekeeping::LocalTime& now,
                const settings::Settings& s, long lastRunDay);

long localDay(const timekeeping::LocalTime& t);

const char* toString(Decision d);

}  // namespace trigger
