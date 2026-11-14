// iSprinklr v2 — Phase 0
//
// Scope: get on the network, report status, accept firmware over HTTP, and
// recover from a bad update without a USB cable. Everything else (timekeeping,
// schedules in NVS, zone sequencing, log export) ships over OTA afterwards.
//
// The device is wired to a Hunter Pro-C but does not drive it in this phase.

#include <Arduino.h>

#include "boot_guard.h"
#include "config.h"
#include "net.h"
#include "web.h"

namespace {

constexpr uint32_t kLinkCheckIntervalMs = 10000;
uint32_t g_lastLinkCheck = 0;

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);  // let USB CDC enumerate before the first log line
  Serial.printf("\n[main] iSprinklr v2 firmware %s (phase 0)\n", FW_VERSION);

  // Before anything that can fail: if the last few boots never reached a healthy
  // state, this reverts to the previous OTA slot and does not return.
  boot_guard::begin();

  if (!net::begin()) {
    // Deliberately not fatal. The web server still starts, so a link that comes
    // up late leaves the device reachable without a reboot. If the network never
    // arrives, markHealthy() is never called and the boot guard eventually
    // reverts to the previous image.
    Serial.println("[main] network unavailable at startup");
  }

  web::begin();

  if (net::isUp()) {
    boot_guard::markHealthy();
    Serial.printf("[main] ready at http://%s/api/status\n",
                  net::localIP().toString().c_str());
  }
}

void loop() {
  web::tick();

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
