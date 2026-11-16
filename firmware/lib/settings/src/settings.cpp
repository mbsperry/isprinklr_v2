// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settings.h"

#include <set>

#include "hunter_limits.h"
#include "settings_limits.h"

namespace settings {
namespace {

bool validName(const std::string& name) {
  return !name.empty() && name.size() <= kMaxNameLength;
}

std::string str(long n) { return std::to_string(n); }

}  // namespace

bool validate(const Settings& s, std::string* error) {
  std::set<int> zoneNumbers;
  for (const Zone& z : s.zones) {
    if (!hunter::isValidZone(z.zone)) {
      *error = "zones: zone " + str(z.zone) + " outside " +
               str(hunter::kMinZone) + "-" + str(hunter::kMaxZone);
      return false;
    }
    if (!zoneNumbers.insert(z.zone).second) {
      *error = "zones: zone " + str(z.zone) + " listed twice";
      return false;
    }
    if (!validName(z.name)) {
      *error = "zones: zone " + str(z.zone) + " name must be 1-" +
               str(kMaxNameLength) + " bytes";
      return false;
    }
  }

  if (s.schedules.size() > kMaxSchedules) {
    *error = "schedules: at most " + str(kMaxSchedules) + " allowed";
    return false;
  }

  std::set<std::string> names;
  for (const Schedule& sch : s.schedules) {
    if (!validName(sch.name)) {
      *error = "schedules: name must be 1-" + str(kMaxNameLength) + " bytes";
      return false;
    }
    if (!names.insert(sch.name).second) {
      *error = "schedules: '" + sch.name + "' listed twice";
      return false;
    }
    std::set<int> used;
    for (const ScheduleItem& item : sch.items) {
      const std::string where = "schedule '" + sch.name + "' zone " + str(item.zone);
      if (!zoneNumbers.count(item.zone)) {
        *error = where + ": not a configured zone";
        return false;
      }
      if (!used.insert(item.zone).second) {
        *error = where + ": listed twice";
        return false;
      }
      if (!hunter::isValidRunMinutes(item.durationMinutes)) {
        *error = where + ": duration_minutes must be " +
                 str(hunter::kMinRunMinutes) + "-" + str(hunter::kMaxRunMinutes);
        return false;
      }
    }
  }

  if (!s.activeSchedule.empty() && !findSchedule(s, s.activeSchedule)) {
    *error = "active: no schedule named '" + s.activeSchedule + "'";
    return false;
  }

  const Config& c = s.config;
  if (c.startHour < 0 || c.startHour > 23 || c.startMinute < 0 ||
      c.startMinute > 59) {
    *error = "config: start time must be 00:00-23:59";
    return false;
  }
  if (c.timezone.empty() || c.timezone.size() > kMaxTimezoneLength) {
    *error = "config: timezone must be 1-" + str(kMaxTimezoneLength) + " bytes";
    return false;
  }
  uint8_t ip[4];
  if (!c.logHost.empty() && !parseIpv4(c.logHost, ip)) {
    *error = "config: log_host must be empty or an IPv4 address like 192.0.2.21";
    return false;
  }
  if (c.logPort < 1 || c.logPort > 65535) {
    *error = "config: log_port must be 1-65535";
    return false;
  }

  return true;
}

bool parseIpv4(const std::string& text, uint8_t out[4]) {
  std::size_t pos = 0;
  for (int part = 0; part < 4; ++part) {
    if (part > 0) {
      if (pos >= text.size() || text[pos] != '.') return false;
      ++pos;
    }
    const std::size_t begin = pos;
    int value = 0;
    while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9' &&
           pos - begin < 3) {
      value = value * 10 + (text[pos] - '0');
      ++pos;
    }
    const std::size_t digits = pos - begin;
    if (digits == 0 || value > 255 || (digits > 1 && text[begin] == '0')) {
      return false;
    }
    out[part] = static_cast<uint8_t>(value);
  }
  return pos == text.size();
}

const Schedule* findSchedule(const Settings& s, const std::string& name) {
  for (const Schedule& sch : s.schedules) {
    if (sch.name == name) return &sch;
  }
  return nullptr;
}

std::vector<PlannedRun> runsForDate(const Schedule& schedule,
                                    const timekeeping::LocalTime& date) {
  std::vector<PlannedRun> runs;
  for (const ScheduleItem& item : schedule.items) {
    if (runsOn(item.days, date.year, date.month, date.day, date.weekday)) {
      runs.push_back(PlannedRun{item.zone, item.durationMinutes});
    }
  }
  return runs;
}

}  // namespace settings
