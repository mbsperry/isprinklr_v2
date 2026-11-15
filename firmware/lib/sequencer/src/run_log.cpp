// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#include "run_log.h"

#include <cstdio>

namespace run_log {
namespace {

using logging::Level;
using seq::Event;
using seq::Origin;
using seq::Outcome;

std::string minutes(int m) { return std::to_string(m) + " min"; }

std::string quoted(const std::string& name) { return "'" + name + "'"; }

// "scheduled, 'Fall'" or "manual". Only scheduled entries name the run: a
// manual zone appended to a scheduled run is still manual.
std::string entryOrigin(const seq::Entry& e, const std::string& runName) {
  if (e.origin == Origin::kSchedule) {
    return runName.empty() ? "scheduled" : "scheduled, " + quoted(runName);
  }
  return "manual";
}

std::string runLabel(const std::string& runName) {
  return runName.empty() ? "manual run" : "run " + quoted(runName);
}

std::string plural(std::size_t n, const char* one, const char* many) {
  return std::to_string(n) + " " + (n == 1 ? one : many);
}

std::string hhmm(int hour, int minute) {
  char buf[6];
  std::snprintf(buf, sizeof(buf), "%02d:%02d", hour % 24, minute % 60);
  return buf;
}

std::string zoneEnded(const Event& e, const std::string& runName) {
  const std::string zone = "zone " + std::to_string(e.entry.zone);
  const std::string origin = " (" + entryOrigin(e.entry, runName) + ")";
  const std::string of =
      formatDuration(e.ranMs) + " of " + minutes(e.entry.durationMinutes);
  switch (e.entry.outcome) {
    case Outcome::kCompleted:
      return zone + " finished: ran its full " +
             minutes(e.entry.durationMinutes) + origin;
    case Outcome::kSkipped:
      return e.ranMs > 0 ? zone + " cut short by skip after " + of + origin
                         : zone + " skipped before it started" + origin;
    case Outcome::kInterrupted:
      return zone + " cut short by cancel after " + of + origin;
    case Outcome::kFailed:
      return zone + " not started: the bus refused it " +
             plural(e.attempts, "time", "times") + "; abandoning the run" +
             origin;
    default:
      return zone + " ended: " + seq::toString(e.entry.outcome) + origin;
  }
}

std::string runEnded(const Event& e, const std::string& runName,
                     const std::vector<seq::Entry>& entries) {
  std::size_t completed = 0, skipped = 0, interrupted = 0, failed = 0;
  for (const seq::Entry& x : entries) {
    switch (x.outcome) {
      case Outcome::kCompleted: ++completed; break;
      case Outcome::kSkipped: ++skipped; break;
      case Outcome::kInterrupted: ++interrupted; break;
      case Outcome::kFailed: ++failed; break;
      default: break;
    }
  }
  const std::size_t notStarted = e.dropped < interrupted ? e.dropped : interrupted;
  const std::size_t cutShort = interrupted - notStarted;

  std::vector<std::string> parts;
  parts.push_back(std::to_string(completed) + " completed");
  if (skipped) parts.push_back(std::to_string(skipped) + " skipped");
  if (cutShort) parts.push_back(std::to_string(cutShort) + " cut short");
  if (failed) parts.push_back(std::to_string(failed) + " failed");
  if (notStarted) parts.push_back(std::to_string(notStarted) + " not started");
  std::string counts;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    counts += (i ? ", " : "") + parts[i];
  }

  const char* verb = e.runEnd == Event::RunEnd::kFinished    ? "finished"
                     : e.runEnd == Event::RunEnd::kCancelled ? "cancelled"
                                                             : "abandoned";
  return runLabel(runName) + " " + verb + ": " + counts;
}

}  // namespace

std::string formatDuration(uint32_t ms) {
  const uint32_t total = ms / 1000;
  const uint32_t m = total / 60;
  const uint32_t s = total % 60;
  if (m == 0) return std::to_string(s) + " s";
  if (s == 0) return std::to_string(m) + " min";
  return std::to_string(m) + " min " + std::to_string(s) + " s";
}

Line describe(const Event& e, const std::string& runName,
              const std::vector<seq::Entry>& entries) {
  switch (e.kind) {
    case Event::Kind::kQueued: {
      std::string what =
          e.count == 1 ? "zone " + std::to_string(e.entry.zone) + " for " +
                             minutes(e.entry.durationMinutes)
                       : plural(e.count, "zone", "zones");
      std::string origin = e.entry.origin == Origin::kSchedule
                               ? "scheduled"
                               : "manual";
      // A run's name is shown for a whole schedule, scheduled or run now; a
      // single manual zone appended to a named run is not part of it.
      if (!runName.empty() &&
          (e.entry.origin == Origin::kSchedule || e.count > 1)) {
        origin += ", " + quoted(runName);
      }
      const std::string when =
          e.ahead == 0 ? "starting now"
                       : plural(e.ahead, "zone", "zones") + " ahead";
      return {Level::kInfo, "queued " + what + " (" + origin + "); " + when};
    }
    case Event::Kind::kQueueFull:
      return {Level::kWarn,
              "refused to queue " + plural(e.count, "zone", "zones") +
                  ": the run is limited to " +
                  std::to_string(seq::kMaxRunEntries) + " entries"};
    case Event::Kind::kStartRetry:
      return {Level::kDebug, "bus busy; zone " + std::to_string(e.entry.zone) +
                                 " start attempt " + std::to_string(e.attempts) +
                                 " not taken, retrying"};
    case Event::Kind::kZoneStarted: {
      std::string msg = "zone " + std::to_string(e.entry.zone) +
                        " started for " + minutes(e.entry.durationMinutes) +
                        " (" + entryOrigin(e.entry, runName) + ", " +
                        std::to_string(e.index + 1) + " of " +
                        std::to_string(e.total) + ")";
      if (e.attempts > 1) {
        msg += " after " + std::to_string(e.attempts) + " attempts";
      }
      return {Level::kInfo, msg};
    }
    case Event::Kind::kZoneEnded:
      return {e.entry.outcome == Outcome::kFailed ? Level::kError : Level::kInfo,
              zoneEnded(e, runName)};
    case Event::Kind::kStopAbandoned:
      return {Level::kWarn, "stop for zone " + std::to_string(e.entry.zone) +
                                " not taken by the bus after " +
                                plural(e.attempts, "attempt", "attempts") +
                                "; the Pro-C ends it on its own timer"};
    case Event::Kind::kRunEnded:
      return {e.runEnd == Event::RunEnd::kFailed ? Level::kError : Level::kInfo,
              runEnded(e, runName, entries)};
  }
  return {Level::kInfo, "sequencer event"};
}

std::vector<Line> TriggerReporter::observe(trigger::Decision d, bool timeValid,
                                           const timekeeping::LocalTime& now,
                                           const settings::Settings& s) {
  using trigger::Decision;
  std::vector<Line> lines;
  const std::string start = hhmm(s.config.startHour, s.config.startMinute);

  if (!haveLast_ || d != last_) {
    if (d == Decision::kWindowMissed) {
      const int end = s.config.startHour * 60 + s.config.startMinute +
                      trigger::kStartWindowMinutes;
      lines.push_back({Level::kWarn,
                       "today's scheduled run did not start: its window (" +
                           start + " to " + hhmm(end / 60, end % 60) +
                           ") has passed"});
    } else {
      lines.push_back(
          {Level::kDebug, std::string("trigger: ") + trigger::toString(d)});
    }
    haveLast_ = true;
    last_ = d;
  }

  // At the start time, say once why nothing started. Only gates that hold
  // the run back at the start time are reported here; the time being invalid
  // shows up later as a missed window.
  if (timeValid && (d == Decision::kDisabled || d == Decision::kNoActiveSchedule)) {
    const int startMinute = s.config.startHour * 60 + s.config.startMinute;
    const int minute = now.minuteOfDay();
    const long day = trigger::localDay(now);
    if (minute >= startMinute &&
        minute < startMinute + trigger::kStartWindowMinutes &&
        day != reportedDay_) {
      reportedDay_ = day;
      if (d == Decision::kDisabled) {
        lines.push_back({Level::kInfo, "scheduled run at " + start +
                                           " not started: the scheduler is off"});
      } else {
        lines.push_back({Level::kWarn, "scheduled run at " + start +
                                           " not started: no active schedule"});
      }
    }
  }
  return lines;
}

}  // namespace run_log
