#include "hunter_pulses.h"

namespace hunter {
namespace {

void push(PulseTrain* t, bool high, uint32_t us) {
  if (t->count < kMaxPulses) t->pulses[t->count++] = Pulse{high, us};
}

void pushBit(PulseTrain* t, bool one) {
  push(t, true, one ? timing::kLongUs : timing::kShortUs);
  push(t, false, one ? timing::kShortUs : timing::kLongUs);
}

}  // namespace

uint32_t PulseTrain::totalUs() const {
  uint32_t sum = 0;
  for (std::size_t i = 0; i < count; ++i) sum += pulses[i].durationUs;
  return sum;
}

void encodePulses(const Frame& frame, PulseTrain* out) {
  out->count = 0;
  push(out, true, timing::kResetHighUs);
  push(out, false, timing::kResetLowUs);
  push(out, true, timing::kStartHighUs);
  push(out, false, timing::kShortUs);

  const std::size_t n = frame.length <= Frame::kMaxBytes ? frame.length
                                                          : Frame::kMaxBytes;
  for (std::size_t i = 0; i < n; ++i) {
    for (int bit = 7; bit >= 0; --bit) {
      pushBit(out, (frame.bytes[i] >> bit) & 1);
    }
  }

  if (frame.trailingOneBit) pushBit(out, true);
  pushBit(out, false);  // stop pulse
}

}  // namespace hunter
