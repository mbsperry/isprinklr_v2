#pragma once

#include <ctime>
#include <cstdint>
#include <string>

// Wall-clock logic, independent of SNTP and of the network. The device side
// (src/clock.cpp) feeds in sync events; everything that decides whether the
// time can be trusted, or what the local date is, lives here and is host-tested.
//
// Policy: time is valid once SNTP has synced at least once since boot, and it
// stays valid until the next reboot even if later syncs fail. The ESP's system
// clock runs off the main crystal and drifts by seconds per day, not minutes, so
// a WAN outage must not stop watering (the architecture's invariant). A power
// cut does lose the time (no RTC battery), and then nothing runs until a sync.
namespace timekeeping {

// US Pacific, with DST rules: second Sunday in March to first Sunday in
// November, switching at 02:00 local. Phase 3 moves this into device config.
constexpr char kPacificTz[] = "PST8PDT,M3.2.0,M11.1.0";

// Any epoch earlier than this is a clock that was never set (the ESP boots at
// 1970). 2026-01-01T00:00:00Z.
constexpr std::time_t kMinPlausibleEpoch = 1767225600;

struct SyncState {
  uint32_t syncCount = 0;  // successful SNTP syncs since boot
  uint32_t lastSyncMs = 0;  // millis() at the most recent sync
};

bool isTimeValid(const SyncState& sync, std::time_t now);

// Sets the process timezone (TZ + tzset). Call once at startup, and before any
// toLocal(). Process-global: newlib and the host libc both work this way.
void applyTimezone(const char* posixTz);

struct LocalTime {
  int year;      // e.g. 2026
  int month;     // 1-12
  int day;       // 1-31
  int weekday;   // 0 = Sunday ... 6 = Saturday
  int hour;      // 0-23
  int minute;    // 0-59
  int second;    // 0-60
  bool dst;      // daylight saving in effect
  int utcOffsetMinutes;  // e.g. -480 for PST, -420 for PDT

  int minuteOfDay() const { return hour * 60 + minute; }
};

// Local time in the zone set by applyTimezone().
LocalTime toLocal(std::time_t epoch);

// "2026-11-15T07:13:02-08:00"
std::string formatLocalIso8601(const LocalTime& t);

// "2026-11-15T15:13:02Z"
std::string formatUtcIso8601(std::time_t epoch);

}  // namespace timekeeping
