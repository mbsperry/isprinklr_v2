// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "day_pattern.h"
#include "log_level.h"
#include "settings_limits.h"
#include "timekeeping.h"

// Everything the device persists about watering: zones, schedules, which one is
// active, the on/off switch, and device config. Stored as one document so a
// write is all-or-nothing (src/settings_service.cpp, lib/settings/settings_store).
namespace settings {

struct Zone {
  int zone;
  std::string name;
};

struct ScheduleItem {
  int zone;
  DayPattern days;
  int durationMinutes;
};

struct Schedule {
  std::string name;
  std::vector<ScheduleItem> items;  // run order
};

struct Config {
  int startHour = 7;  // local time the daily run starts (Phase 4)
  int startMinute = 0;
  std::string timezone = timekeeping::kPacificTz;  // POSIX TZ string
  // UDP syslog sink (Phase 5). An IPv4 literal, not a hostname: a DNS lookup
  // can block, and nothing on the log path may. Empty: no export.
  std::string logHost;
  int logPort = kDefaultLogPort;
  // Lines less severe than this are not recorded: not in the RAM ring, so not
  // in /api/logs or the UDP export either. Serial still prints every line.
  logging::Level logLevel = kDefaultLogLevel;
};

struct Settings {
  std::vector<Zone> zones;
  std::vector<Schedule> schedules;
  std::string activeSchedule;  // empty: none
  bool enabled = false;        // fresh devices never water on their own
  Config config;
};

// Checks every cross-field rule. On failure returns false with a message that
// names the offending field.
//   zones: 1-48, unique, names 1-32 bytes
//   schedules: at most 10, names 1-32 bytes and unique
//   items: zone must be a configured zone, at most once per schedule,
//          duration_minutes within hunter_limits.h
//   active: empty or the name of a schedule
//   config: 00:00-23:59, timezone 1-63 bytes, log_host empty or a dotted
//           IPv4 address, log_port 1-65535, log_level one of error/warn/info/debug
bool validate(const Settings& s, std::string* error);

// Dotted-quad IPv4 ("192.0.2.21"): four decimal parts, 0-255, no leading
// zeros, nothing else.
bool parseIpv4(const std::string& text, uint8_t out[4]);

const Schedule* findSchedule(const Settings& s, const std::string& name);

struct PlannedRun {
  int zone;
  int durationMinutes;
};

// The zones a schedule runs on this local date, in schedule order.
std::vector<PlannedRun> runsForDate(const Schedule& schedule,
                                    const timekeeping::LocalTime& date);

}  // namespace settings
