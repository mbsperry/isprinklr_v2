// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#include <unity.h>

#include "hunter_frame.h"
#include "hunter_pulses.h"

using hunter::Pulse;
using hunter::PulseTrain;
namespace t = hunter::timing;

void setUp() {}
void tearDown() {}

namespace {

void assertPulse(const Pulse& p, bool high, uint32_t us) {
  TEST_ASSERT_EQUAL(high, p.high);
  TEST_ASSERT_EQUAL_UINT32(us, p.durationUs);
}

// Decodes the data bits back out of a train, starting after reset + start.
int decodeBit(const PulseTrain& train, std::size_t bitIndex) {
  const Pulse& h = train.pulses[4 + 2 * bitIndex];
  const Pulse& l = train.pulses[5 + 2 * bitIndex];
  if (h.high && !l.high && h.durationUs == t::kLongUs && l.durationUs == t::kShortUs) return 1;
  if (h.high && !l.high && h.durationUs == t::kShortUs && l.durationUs == t::kLongUs) return 0;
  return -1;
}

}  // namespace

void test_zone_frame_preamble() {
  PulseTrain train;
  hunter::encodePulses(*hunter::buildStartFrame(1, 5), &train);
  assertPulse(train.pulses[0], true, t::kResetHighUs);
  assertPulse(train.pulses[1], false, t::kResetLowUs);
  assertPulse(train.pulses[2], true, t::kStartHighUs);
  assertPulse(train.pulses[3], false, t::kShortUs);
}

void test_zone_frame_pulse_count() {
  PulseTrain train;
  hunter::encodePulses(*hunter::buildStartFrame(1, 5), &train);
  // 4 preamble + 2 x (120 data bits + trailing 1 + stop 0)
  TEST_ASSERT_EQUAL_UINT(4 + 2 * 122, train.count);
  TEST_ASSERT_EQUAL_UINT(hunter::kMaxPulses, train.count);
}

void test_bits_round_trip_msb_first() {
  const auto frame = *hunter::buildStartFrame(13, 10);
  PulseTrain train;
  hunter::encodePulses(frame, &train);
  for (std::size_t byte = 0; byte < frame.length; ++byte) {
    uint8_t value = 0;
    for (std::size_t bit = 0; bit < 8; ++bit) {
      const int b = decodeBit(train, byte * 8 + bit);
      TEST_ASSERT_TRUE(b == 0 || b == 1);
      value = static_cast<uint8_t>((value << 1) | b);
    }
    TEST_ASSERT_EQUAL_HEX8(frame.bytes[byte], value);
  }
}

void test_trailing_one_then_stop_zero() {
  PulseTrain train;
  hunter::encodePulses(*hunter::buildStopFrame(3), &train);
  TEST_ASSERT_EQUAL(1, decodeBit(train, 120));
  TEST_ASSERT_EQUAL(0, decodeBit(train, 121));
}

void test_no_trailing_bit_when_not_requested() {
  hunter::Frame frame;
  frame.length = 1;
  frame.bytes[0] = 0xA5;
  frame.trailingOneBit = false;
  PulseTrain train;
  hunter::encodePulses(frame, &train);
  TEST_ASSERT_EQUAL_UINT(4 + 2 * 9, train.count);
  TEST_ASSERT_EQUAL(0, decodeBit(train, 8));  // stop only
}

void test_levels_alternate_and_end_low() {
  PulseTrain train;
  hunter::encodePulses(*hunter::buildStartFrame(48, 240), &train);
  for (std::size_t i = 0; i < train.count; ++i) {
    TEST_ASSERT_EQUAL(i % 2 == 0, train.pulses[i].high);
  }
  TEST_ASSERT_FALSE(train.pulses[train.count - 1].high);
}

void test_total_duration_is_about_650ms() {
  // 390 ms reset + 1.108 ms start + 122 bits x 2.083 ms = 645.25 ms.
  PulseTrain train;
  hunter::encodePulses(*hunter::buildStartFrame(1, 5), &train);
  TEST_ASSERT_EQUAL_UINT32(325000 + 65000 + 900 + 208 + 122 * (1875 + 208),
                           train.totalUs());
}

void test_oversized_length_is_clamped() {
  hunter::Frame frame;
  frame.length = 99;  // corrupt
  PulseTrain train;
  hunter::encodePulses(frame, &train);
  TEST_ASSERT_TRUE(train.count <= hunter::kMaxPulses);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_zone_frame_preamble);
  RUN_TEST(test_zone_frame_pulse_count);
  RUN_TEST(test_bits_round_trip_msb_first);
  RUN_TEST(test_trailing_one_then_stop_zero);
  RUN_TEST(test_no_trailing_bit_when_not_requested);
  RUN_TEST(test_levels_alternate_and_end_low);
  RUN_TEST(test_total_duration_is_about_650ms);
  RUN_TEST(test_oversized_length_is_clamped);
  return UNITY_END();
}
