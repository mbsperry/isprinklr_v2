// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#include "daily_trigger.h"

namespace trigger {

long localDay(const timekeeping::LocalTime& t) {
  return timekeeping::daysSinceEpoch(t.year, t.month, t.day);
}

Decision decide(bool timeValid, const timekeeping::LocalTime& now,
                const settings::Settings& s, long lastRunDay) {
  if (!timeValid) return Decision::kTimeInvalid;
  if (!s.enabled) return Decision::kDisabled;
  if (s.activeSchedule.empty() || !settings::findSchedule(s, s.activeSchedule)) {
    return Decision::kNoActiveSchedule;
  }
  if (localDay(now) == lastRunDay) return Decision::kAlreadyRan;

  const int start = s.config.startHour * 60 + s.config.startMinute;
  const int minute = now.minuteOfDay();
  if (minute < start) return Decision::kBeforeStart;
  if (minute >= start + kStartWindowMinutes) return Decision::kWindowMissed;
  return Decision::kDue;
}

const char* toString(Decision d) {
  switch (d) {
    case Decision::kDue: return "due";
    case Decision::kTimeInvalid: return "time_invalid";
    case Decision::kDisabled: return "disabled";
    case Decision::kNoActiveSchedule: return "no_active_schedule";
    case Decision::kAlreadyRan: return "already_ran";
    case Decision::kBeforeStart: return "before_start";
    case Decision::kWindowMissed: return "window_missed";
  }
  return "unknown";
}

}  // namespace trigger
