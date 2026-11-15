#pragma once

#include <Arduino.h>

#include "zone_controller.h"

// HTTP surface as of Phase 1:
//   GET  /api/status       device state, enough to diagnose a bad update remotely
//   POST /api/zone/start   {"zone": N, "duration_minutes": M}
//   POST /api/zone/stop    {} or {"zone": N}
//   POST /api/update       firmware upload (multipart), then reboot
//
// Shapes are in docs/api-contract.md. Schedule endpoints arrive in Phase 3.
namespace web {

// The controller is owned by the caller and must outlive the server.
void begin(ZoneController& zones);

// Reboots pending from an accepted firmware upload are deferred so the HTTP
// response can be flushed first. Call from loop().
void tick();

}  // namespace web
