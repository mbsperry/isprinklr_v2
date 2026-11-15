// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-FileCopyrightText: 2020-2022 Eloi Codina Torras (HunterRoam, https://github.com/ecodina/hunter-wifi)
// SPDX-FileCopyrightText: Sebastien (https://github.com/seb821/OpenSprinkler-Firmware-Hunter)
// SPDX-FileCopyrightText: Dave Fleck
// SPDX-FileCopyrightText: Scott Shumate
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Adapted from the HunterRoam SmartPort library (GPL-3.0-or-later). See NOTICE.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

// Hunter SmartPort (REM line) frame construction. Pure: no pins, no timing, no
// Arduino. The bus writer turns a Frame into pulses (hunter_pulses.h) and plays
// them out (src/hunter_bus.cpp).
//
// The encoding is ported from v1's HunterRoam library, which derives from work by
// Scott Shumate, Dave Fleck, Sebastien (OpenSprinkler-Firmware-Hunter) and Eloi
// Codina Torras, and is GPL-licensed. The tests pin the output byte-for-byte to
// what v1's code produces, since v1's frames are the ones known to work on this
// Pro-C.
namespace hunter {

struct Frame {
  static constexpr std::size_t kMaxBytes = 15;

  uint8_t bytes[kMaxBytes] = {};
  std::size_t length = 0;  // bytes used, sent most-significant bit first
  bool trailingOneBit = false;  // zone frames carry one extra 1 bit after the bytes
};

// Run `zone` for `minutes`. Returns nullopt unless both are within the bounds in
// hunter_limits.h; in particular minutes == 0 is rejected here.
std::optional<Frame> buildStartFrame(int zone, int minutes);

// Stop `zone`. On the wire this is a start frame with a zero duration.
std::optional<Frame> buildStopFrame(int zone);

}  // namespace hunter
