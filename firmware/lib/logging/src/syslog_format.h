// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "log_ring.h"

// One log line as an RFC 5424 syslog message, for UDP export:
//
//   <134>1 2026-11-15T22:40:20Z isprinklr isprinklr - sched
//     [isp@32473 seq="42" up="81234" boot="1a2b3c4d"] zone 3 started ...
//
// (one line on the wire). Facility local0. MSGID is the module, which the
// server stores and filters on. TIMESTAMP is "-" while the device clock is not
// valid; the server then uses the time it received the line. The structured
// data carries the boot-relative order, so lines sent before the clock synced
// can still be placed. 32473 is the private enterprise number RFC 5612
// reserves for examples; we have no number of our own and need none on a LAN.
namespace logging {

constexpr int kSyslogFacility = 16;  // local0
constexpr char kSyslogAppName[] = "isprinklr";
constexpr char kSyslogSdId[] = "isp@32473";

std::string formatSyslog(const LogEntry& entry, const char* hostname,
                         uint32_t bootId);

}  // namespace logging
