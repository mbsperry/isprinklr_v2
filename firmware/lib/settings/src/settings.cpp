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

  return true;
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
