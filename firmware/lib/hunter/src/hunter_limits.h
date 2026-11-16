// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

// The single source of zone and duration bounds. Request validation, the frame
// builder, and the tests all read these; nothing else may hard-code a limit.
//
// v1 had three disagreeing sets (protocol 1-48 / 0-240, firmware 1-20 / 0-120,
// API 0-7200 seconds).
namespace hunter {

constexpr int kMinZone = 1;

// What the SmartPort protocol can address. Only the frame builder uses this;
// it keeps zones 13-48 (a different header) buildable and tested.
constexpr int kMaxProtocolZone = 48;

// What the firmware accepts everywhere else: settings, the zone API, the zone
// controller. 10 matches the installed Pro-C, and it is what keeps the stored
// settings document inside its 4 KB NVS cap: with 10 zones, every schedule slot
// full and max-length names it is about 3.7 KB, while 48 zones would need about
// 16 KB, more than the NVS partition can hold twice. Raising this means moving
// settings out of NVS first (docs/open-decisions.md, decision 7).
constexpr int kMaxZone = 10;
static_assert(kMaxZone <= kMaxProtocolZone, "kMaxZone beyond the protocol");

// A run is always bounded and never zero: zero minutes on the wire is the stop
// command, and is reachable only through buildStopFrame().
constexpr int kMinRunMinutes = 1;
constexpr int kMaxRunMinutes = 240;

constexpr bool isValidZone(long zone) {
  return zone >= kMinZone && zone <= kMaxZone;
}

constexpr bool isValidProtocolZone(long zone) {
  return zone >= kMinZone && zone <= kMaxProtocolZone;
}

constexpr bool isValidRunMinutes(long minutes) {
  return minutes >= kMinRunMinutes && minutes <= kMaxRunMinutes;
}

}  // namespace hunter
