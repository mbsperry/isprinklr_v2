#pragma once

#include <cstdint>
#include <string>

// Day patterns, carried over from v1 so the frontend keeps working:
//   weekday lists  M, TU, W, TH, F, SA, SU joined with ':'  ("M:W:F")
//   ALL   every day
//   NONE  never
//   EO    every other day
// ALL, NONE and EO stand alone; they cannot be combined with weekdays.
//
// EO alternates strictly: it runs on local dates whose day count since
// 1970-01-01 is even. v1 used day-of-year parity, which runs twice in a row
// over Dec 31 -> Jan 1 in non-leap years; calendar-date parity would do the
// same after every 31-day month. No v1 schedule used EO, so nothing changes
// for existing data.
namespace settings {

struct DayPattern {
  enum class Kind : uint8_t { kWeekdays, kAll, kNone, kEveryOther };

  Kind kind = Kind::kNone;
  uint8_t weekdays = 0;  // bit (1 << weekday), weekday 0 = Sunday ... 6 = Saturday

  bool operator==(const DayPattern& o) const {
    return kind == o.kind && weekdays == o.weekdays;
  }
};

// Case-insensitive ("Tu:F" from v1 data is accepted). Rejects empty tokens,
// unknown tokens, duplicates, and ALL/NONE/EO combined with anything.
bool parseDayPattern(const std::string& text, DayPattern* out,
                     std::string* error);

// Canonical form: uppercase, weekdays in Monday-first order ("M:W:F").
std::string formatDayPattern(const DayPattern& pattern);

// Whether the pattern runs on this local date. weekday: 0 = Sunday.
bool runsOn(const DayPattern& pattern, int year, int month, int day,
            int weekday);

}  // namespace settings
