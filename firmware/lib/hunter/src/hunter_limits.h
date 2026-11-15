// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

// The single source of zone and duration bounds. Request validation, the frame
// builder, and the tests all read these; nothing else may hard-code a limit.
//
// v1 had three disagreeing sets (protocol 1-48 / 0-240, firmware 1-20 / 0-120,
// API 0-7200 seconds). These are the SmartPort protocol limits. A per-site zone
// count, if one is wanted, belongs in device config (Phase 3) and narrows these;
// it never widens them.
namespace hunter {

constexpr int kMinZone = 1;
constexpr int kMaxZone = 48;

// A run is always bounded and never zero: zero minutes on the wire is the stop
// command, and is reachable only through buildStopFrame().
constexpr int kMinRunMinutes = 1;
constexpr int kMaxRunMinutes = 240;

constexpr bool isValidZone(long zone) {
  return zone >= kMinZone && zone <= kMaxZone;
}

constexpr bool isValidRunMinutes(long minutes) {
  return minutes >= kMinRunMinutes && minutes <= kMaxRunMinutes;
}

}  // namespace hunter
