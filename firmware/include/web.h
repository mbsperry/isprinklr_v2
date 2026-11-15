// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <Arduino.h>

// HTTP surface: status, zone start/stop (through the sequencer), settings, and
// firmware upload. Shapes are in docs/api-contract.md.
namespace web {

void begin();

// Reboots pending from an accepted firmware upload are deferred so the HTTP
// response can be flushed first. Call from loop().
void tick();

}  // namespace web
