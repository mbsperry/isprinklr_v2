// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

namespace logging {

// Numbered as syslog severities (RFC 5424), so a lower number is more severe
// and "this level or worse" is value <= threshold.
enum class Level : uint8_t {
  kError = 3,
  kWarn = 4,
  kInfo = 6,
  kDebug = 7,
};

const char* toString(Level level);

// "error", "warn", "info", "debug". False for anything else.
bool parseLevel(const char* text, Level* out);

}  // namespace logging
