// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

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

// SNTP servers, tried in order; lwIP moves on when one stops answering.
//
// A LAN NTP server (the router) goes first when the build names one, as
// ntp_server in firmware/secrets.ini (gitignored; see secrets.example.ini).
// It is a build setting so the site's address stays out of git. The router
// keeps serving its own clock through a WAN outage, so a reboot during one can
// still get the time; without it, time stays invalid (and nothing waters)
// until the WAN is back. The build warns when it is unset.
#ifndef NTP_LOCAL_SERVER
#define NTP_LOCAL_SERVER ""
#endif
constexpr bool kHasLocalNtp = sizeof(NTP_LOCAL_SERVER) > 1;
constexpr const char* kNtpServers[3] = {
    kHasLocalNtp ? NTP_LOCAL_SERVER : "pool.ntp.org",
    kHasLocalNtp ? "pool.ntp.org" : "time.google.com",
    kHasLocalNtp ? "time.google.com" : "time.cloudflare.com"};

// Reboot recovery (open decision 1): after a reboot mid-run, carry on with
// the remaining zones only if the ESP is back within this long of when the
// next zone was due; otherwise skip them until the next scheduled day. The zone
// that was running always finishes, because the Pro-C holds its timer.
constexpr uint32_t kRecoveryWindowSeconds = 60 * 60;

// Consecutive boots that fail to reach a healthy state before the boot guard
// reverts to the other OTA slot.
constexpr uint32_t kMaxFailedBoots = 3;

// Grace period between answering an OTA request and rebooting, so the HTTP
// response actually reaches the client.
constexpr uint32_t kRestartDelayMs = 1500;

// An upload that has sent nothing for this long is treated as abandoned (client
// dropped mid-upload): it stops blocking zone commands, and the next upload
// aborts it before starting.
constexpr uint32_t kUploadStallMs = 30000;

}  // namespace config
