// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#include "day_pattern.h"

#include <cctype>

#include "timekeeping.h"

namespace settings {
namespace {

struct Token {
  const char* text;
  int weekday;  // 0 = Sunday
};

// Monday-first, the order formatDayPattern() emits.
constexpr Token kWeekdayTokens[] = {{"M", 1},  {"TU", 2}, {"W", 3}, {"TH", 4},
                                    {"F", 5},  {"SA", 6}, {"SU", 0}};

std::string upper(const std::string& s) {
  std::string out = s;
  for (char& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return out;
}

}  // namespace

bool parseDayPattern(const std::string& text, DayPattern* out,
                     std::string* error) {
  const std::string t = upper(text);
  if (t.empty()) {
    *error = "days is empty";
    return false;
  }

  if (t == "ALL" || t == "NONE" || t == "EO") {
    out->weekdays = 0;
    out->kind = t == "ALL"    ? DayPattern::Kind::kAll
                : t == "NONE" ? DayPattern::Kind::kNone
                              : DayPattern::Kind::kEveryOther;
    return true;
  }

  uint8_t mask = 0;
  std::size_t start = 0;
  while (true) {
    const std::size_t end = t.find(':', start);
    const std::string token =
        t.substr(start, end == std::string::npos ? std::string::npos : end - start);

    if (token == "ALL" || token == "NONE" || token == "EO") {
      *error = "days: " + token + " cannot be combined with other days";
      return false;
    }

    int weekday = -1;
    for (const Token& w : kWeekdayTokens) {
      if (token == w.text) weekday = w.weekday;
    }
    if (weekday < 0) {
      *error = "days: unknown day '" + token +
               "' (use M, TU, W, TH, F, SA, SU, ALL, NONE or EO)";
      return false;
    }

    const uint8_t bit = static_cast<uint8_t>(1u << weekday);
    if (mask & bit) {
      *error = "days: " + token + " listed twice";
      return false;
    }
    mask |= bit;

    if (end == std::string::npos) break;
    start = end + 1;
  }

  out->kind = DayPattern::Kind::kWeekdays;
  out->weekdays = mask;
  return true;
}

std::string formatDayPattern(const DayPattern& pattern) {
  switch (pattern.kind) {
    case DayPattern::Kind::kAll: return "ALL";
    case DayPattern::Kind::kNone: return "NONE";
    case DayPattern::Kind::kEveryOther: return "EO";
    case DayPattern::Kind::kWeekdays: break;
  }
  std::string out;
  for (const Token& w : kWeekdayTokens) {
    if (pattern.weekdays & (1u << w.weekday)) {
      if (!out.empty()) out += ':';
      out += w.text;
    }
  }
  return out.empty() ? "NONE" : out;
}

bool runsOn(const DayPattern& pattern, int year, int month, int day,
            int weekday) {
  switch (pattern.kind) {
    case DayPattern::Kind::kAll: return true;
    case DayPattern::Kind::kNone: return false;
    case DayPattern::Kind::kEveryOther:
      return timekeeping::daysSinceEpoch(year, month, day) % 2 == 0;
    case DayPattern::Kind::kWeekdays:
      return weekday >= 0 && weekday <= 6 && (pattern.weekdays & (1u << weekday));
  }
  return false;
}

}  // namespace settings
