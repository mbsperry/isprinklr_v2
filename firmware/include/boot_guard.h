// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

// Recovery from a firmware update that flashes successfully but then cannot be
// reached over the network.
//
// A corrupt or interrupted upload is already safe: Update.h verifies the image
// and leaves the running firmware untouched. The dangerous case is firmware that
// boots and then fails to bring up the network or the web server, because there
// is no remaining path to push a fix.
//
// ESP-IDF's own rollback does not help here. It is enabled by default but uses
// CONFIG_BOOTLOADER_APP_ROLLBACK_CONFIRM_ON_STARTUP, which marks the image valid
// after component init and immediately before app_main — i.e. before any of this
// firmware's own code runs. App-controlled confirmation needs an sdkconfig change,
// which the Arduino framework's precompiled libraries do not permit.
//
// So the check lives here: count boots in NVS, clear the count once the device is
// demonstrably healthy, and if the count reaches a threshold, point the bootloader
// back at the other OTA slot.
namespace boot_guard {

// Call early in setup(), before network bring-up. Increments the boot counter and,
// if the threshold is exceeded, switches the boot partition and restarts. Does not
// return in that case.
void begin();

// Call once the device is serving requests on the network. Clears the counter so
// the running image is considered good.
void markHealthy();

// Consecutive boots that did not reach markHealthy(), as of this boot.
uint32_t failedBoots();

bool isHealthy();

// Fills obj with boot/partition state for the status endpoint.
void describe(JsonObject obj);

}  // namespace boot_guard
