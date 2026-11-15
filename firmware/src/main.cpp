// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

// iSprinklr v2 — Phase 4
//
// Network, status, OTA, boot guard (Phase 0); manual zone control over the
// Hunter SmartPort line (Phase 1); SNTP time with a validity flag (Phase 2);
// zones, schedules and config stored in NVS (Phase 3); the sequencer and daily
// trigger that run them (Phase 4). Log export ships over OTA later.

#include <Arduino.h>

#include "boot_guard.h"
#include "config.h"
#include "hunter_bus.h"
#include "net.h"
#include "settings_service.h"
#include "wallclock.h"
#include "web.h"
#include "sequencer_service.h"

namespace {

constexpr uint32_t kLinkCheckIntervalMs = 10000;
uint32_t g_lastLinkCheck = 0;


}  // namespace

void setup() {
  // First: hold the REM line LOW. Until this runs the pin is in its reset state,
  // and a HIGH on REM is the start of a Hunter reset pulse.
  hunter_bus::begin(config::kSmartPortPin);

  Serial.begin(115200);
  delay(200);  // let USB CDC enumerate before the first log line
  Serial.printf("\n[main] iSprinklr v2 firmware %s (phase 4)\n", FW_VERSION);

  // Before anything that can fail: if the last few boots never reached a healthy
  // state, this reverts to the previous OTA slot and does not return.
  boot_guard::begin();

  // Settings before the network: the timezone comes from them, and the web
  // server must not serve settings routes before they are loaded.
  settings_service::begin();

  if (!net::begin()) {
    // Deliberately not fatal. The web server still starts, so a link that comes
    // up late leaves the device reachable without a reboot. If the network never
    // arrives, markHealthy() is never called and the boot guard eventually
    // reverts to the previous image.
    Serial.println("[main] network unavailable at startup");
  }

  // After network bring-up was attempted, but not conditional on it: SNTP
  // keeps polling until a server answers.
  wallclock::begin(settings_service::with(
      [](SettingsStore& s) { return s.current().config.timezone; }));

  sequencer_service::begin();
  web::begin();

  if (net::isUp()) {
    boot_guard::markHealthy();
    Serial.printf("[main] ready at http://%s/api/status\n",
                  net::localIP().toString().c_str());
  }
}

void loop() {
  web::tick();
  sequencer_service::tick();

  const uint32_t now = millis();
  if (now - g_lastLinkCheck >= kLinkCheckIntervalMs) {
    g_lastLinkCheck = now;

    if (net::isUp()) {
      // Covers a link that came up after setup() finished.
      boot_guard::markHealthy();
    } else {
      net::reconnect();
    }
  }

  delay(100);
}
