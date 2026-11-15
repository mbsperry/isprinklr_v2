#pragma once

#include <Arduino.h>

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

namespace config {

// Hunter Pro-C SmartPort / REM line. Same pin as v1. Zone and duration bounds
// live in lib/hunter/src/hunter_limits.h, not here.
constexpr int kSmartPortPin = 18;

// W5500 Ethernet, SPI3. Fixed by the Waveshare ESP32-S3-ETH layout.
constexpr int kEthCsPin = 14;
constexpr int kEthIntPin = 10;
constexpr int kEthRstPin = 9;
constexpr int kEthMisoPin = 12;
constexpr int kEthMosiPin = 11;
constexpr int kEthSclkPin = 13;
constexpr int kEthAddr = 1;

// Note: kEthRstPin occupies GPIO 9, one of the Arduino default I2C pins (8/9).
// Any future I2C peripheral needs Wire.begin() remapped.

constexpr uint32_t kNetworkTimeoutMs = 30000;

// Consecutive boots that fail to reach a healthy state before the boot guard
// reverts to the other OTA slot.
constexpr uint32_t kMaxFailedBoots = 3;

// Grace period between answering an OTA request and rebooting, so the HTTP
// response actually reaches the client.
constexpr uint32_t kRestartDelayMs = 1500;

}  // namespace config
