#include "hunter_frame.h"

#include "hunter_limits.h"

namespace hunter {
namespace {

// Base zone frame, before zone and duration fields are written into it.
constexpr uint8_t kZoneFrameTemplate[15] = {0xff, 0x00, 0x00, 0x00, 0x10,
                                            0x00, 0x00, 0x04, 0x00, 0x00,
                                            0x01, 0x00, 0x01, 0xb8, 0x3f};

// Writes the low `width` bits of `value` into the frame starting at bit `pos`,
// least-significant bit first. Bit positions count from the MSB of byte 0, so
// the field's LSB lands at the earliest bit sent on the wire.
void setField(Frame& f, unsigned pos, unsigned value, unsigned width) {
  for (unsigned i = 0; i < width; ++i, ++pos) {
    const uint8_t mask = static_cast<uint8_t>(0x80u >> (pos % 8));
    if (value & (1u << i)) {
      f.bytes[pos / 8] |= mask;
    } else {
      f.bytes[pos / 8] &= static_cast<uint8_t>(~mask);
    }
  }
}

// Field layout from v1. Each zone-derived value and each duration nibble appears
// three times; presumably redundancy the receiver checks.
Frame encodeZoneFrame(int zone, int minutes) {
  Frame f;
  for (std::size_t i = 0; i < sizeof(kZoneFrameTemplate); ++i) {
    f.bytes[i] = kZoneFrameTemplate[i];
  }
  f.length = sizeof(kZoneFrameTemplate);
  f.trailingOneBit = true;

  const unsigned z = static_cast<unsigned>(zone);
  const unsigned m = static_cast<unsigned>(minutes);

  setField(f, 9, z > 12 ? 0x1 : 0x2, 2);

  setField(f, 23, z + 0x17, 7);
  setField(f, 36, z + 0x17, 7);
  setField(f, 49, z + 0x23, 7);
  setField(f, 62, z + 0x23, 7);
  setField(f, 75, z + 0x2f, 7);
  setField(f, 88, z + 0x2f, 7);

  setField(f, 31, m, 4);
  setField(f, 44, m >> 4, 4);
  setField(f, 57, m, 4);
  setField(f, 70, m >> 4, 4);
  setField(f, 83, m, 4);
  setField(f, 96, m >> 4, 4);

  setField(f, 109, z - 1, 4);
  return f;
}

}  // namespace

std::optional<Frame> buildStartFrame(int zone, int minutes) {
  if (!isValidZone(zone) || !isValidRunMinutes(minutes)) return std::nullopt;
  return encodeZoneFrame(zone, minutes);
}

std::optional<Frame> buildStopFrame(int zone) {
  if (!isValidZone(zone)) return std::nullopt;
  return encodeZoneFrame(zone, 0);
}

}  // namespace hunter
