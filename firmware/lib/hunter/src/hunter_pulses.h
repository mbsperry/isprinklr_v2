#pragma once

#include <cstddef>
#include <cstdint>

#include "hunter_frame.h"

// Converts a Frame into the exact high/low pulse train for the REM line. Pure, so
// the timing is asserted in host tests rather than discovered on a scope.
//
// Line idles LOW. Sequence:
//   reset   HIGH 325 ms, LOW 65 ms
//   start   HIGH 900 us, LOW 208 us
//   each bit, MSB first:  1 = HIGH 1875 / LOW 208,  0 = HIGH 208 / LOW 1875
//   trailing 1 bit (zone frames only)
//   stop    a 0 bit
// Timings are v1's, which drove this Pro-C successfully.
namespace hunter {

namespace timing {
constexpr uint32_t kResetHighUs = 325000;
constexpr uint32_t kResetLowUs = 65000;
constexpr uint32_t kStartHighUs = 900;
constexpr uint32_t kShortUs = 208;
constexpr uint32_t kLongUs = 1875;
}  // namespace timing

struct Pulse {
  bool high;
  uint32_t durationUs;
};

// reset(2) + start(2) + 2 per bit for 15 bytes, the trailing bit, and the stop bit.
constexpr std::size_t kMaxPulses = 4 + 2 * (Frame::kMaxBytes * 8 + 2);

struct PulseTrain {
  Pulse pulses[kMaxPulses];
  std::size_t count = 0;

  uint32_t totalUs() const;
};

// Fixed-size output so the bus task never allocates.
void encodePulses(const Frame& frame, PulseTrain* out);

}  // namespace hunter
