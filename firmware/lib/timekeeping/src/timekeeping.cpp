// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#include "timekeeping.h"

#include <cstdio>
#include <cstdlib>

namespace timekeeping {
namespace {

// Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's
// days_from_civil). Lets us compute the UTC offset without tm_gmtoff, which
// newlib lacks, or timegm(), which is not portable.
long daysFromCivil(long y, unsigned m, unsigned d) {
  y -= m <= 2;
  const long era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<long>(doe) - 719468;
}

// Seconds since the epoch, reading the broken-down time as if it were UTC.
long long asUtcSeconds(const std::tm& t) {
  const long days = daysFromCivil(t.tm_year + 1900L,
                                  static_cast<unsigned>(t.tm_mon + 1),
                                  static_cast<unsigned>(t.tm_mday));
  return days * 86400LL + t.tm_hour * 3600LL + t.tm_min * 60LL + t.tm_sec;
}

}  // namespace

bool isTimeValid(const SyncState& sync, std::time_t now) {
  return sync.syncCount > 0 && now >= kMinPlausibleEpoch;
}

void applyTimezone(const char* posixTz) {
  setenv("TZ", posixTz, 1);
  tzset();
}

LocalTime toLocal(std::time_t epoch) {
  std::tm local{};
  localtime_r(&epoch, &local);

  LocalTime t;
  t.year = local.tm_year + 1900;
  t.month = local.tm_mon + 1;
  t.day = local.tm_mday;
  t.weekday = local.tm_wday;
  t.hour = local.tm_hour;
  t.minute = local.tm_min;
  t.second = local.tm_sec;
  t.dst = local.tm_isdst > 0;
  t.utcOffsetMinutes =
      static_cast<int>((asUtcSeconds(local) - static_cast<long long>(epoch)) / 60);
  return t;
}

std::string formatLocalIso8601(const LocalTime& t) {
  const int offset = t.utcOffsetMinutes;
  const int absOffset = offset < 0 ? -offset : offset;
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d%c%02d:%02d",
                t.year, t.month, t.day, t.hour, t.minute, t.second,
                offset < 0 ? '-' : '+', absOffset / 60, absOffset % 60);
  return buf;
}

std::string formatUtcIso8601(std::time_t epoch) {
  std::tm utc{};
  gmtime_r(&epoch, &utc);
  char buf[24];
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday, utc.tm_hour,
                utc.tm_min, utc.tm_sec);
  return buf;
}

}  // namespace timekeeping
