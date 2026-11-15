#pragma once

#include <string>
#include <vector>

#include "day_pattern.h"
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
//   config: 00:00-23:59, timezone 1-63 bytes
bool validate(const Settings& s, std::string* error);

const Schedule* findSchedule(const Settings& s, const std::string& name);

struct PlannedRun {
  int zone;
  int durationMinutes;
};

// The zones a schedule runs on this local date, in schedule order.
std::vector<PlannedRun> runsForDate(const Schedule& schedule,
                                    const timekeeping::LocalTime& date);

}  // namespace settings
