// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#include "syslog_format.h"

#include <cstdio>

#include "timekeeping.h"

namespace logging {
namespace {

// RFC 5424 header fields are printable US-ASCII without spaces; "-" if empty.
std::string headerField(const char* text) {
  std::string out;
  for (const char* p = text; p != nullptr && *p != '\0'; ++p) {
    const unsigned char c = static_cast<unsigned char>(*p);
    out += (c >= 33 && c <= 126) ? static_cast<char>(c) : '_';
  }
  return out.empty() ? "-" : out;
}

}  // namespace

std::string formatSyslog(const LogEntry& entry, const char* hostname,
                         uint32_t bootId) {
  const int pri = kSyslogFacility * 8 + static_cast<int>(entry.level);
  const std::string timestamp =
      entry.epoch >= timekeeping::kMinPlausibleEpoch
          ? timekeeping::formatUtcIso8601(static_cast<std::time_t>(entry.epoch))
          : "-";
  char sd[80];
  std::snprintf(sd, sizeof(sd), "[%s seq=\"%u\" up=\"%u\" boot=\"%08x\"]",
                kSyslogSdId, static_cast<unsigned>(entry.seq),
                static_cast<unsigned>(entry.uptimeMs),
                static_cast<unsigned>(bootId));

  std::string out = "<" + std::to_string(pri) + ">1 " + timestamp + " " +
                    headerField(hostname) + " " + kSyslogAppName + " - " +
                    headerField(entry.module) + " " + sd + " ";
  // The message is free text; only line breaks would confuse a reader.
  for (const char* p = entry.message; *p != '\0'; ++p) {
    out += (*p == '\n' || *p == '\r') ? ' ' : *p;
  }
  return out;
}

}  // namespace logging
