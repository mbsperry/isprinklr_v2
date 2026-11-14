#pragma once

#include <Arduino.h>

// Phase 0 HTTP surface:
//   GET  /api/status   device state, enough to diagnose a bad update remotely
//   POST /api/update   firmware upload (multipart), then reboot
//
// Zone control and schedule endpoints are deliberately absent. They arrive over
// OTA once the device is installed; see docs/api-contract.md for the planned
// shapes.
namespace web {

void begin();

// Reboots pending from an accepted firmware upload are deferred so the HTTP
// response can be flushed first. Call from loop().
void tick();

}  // namespace web
