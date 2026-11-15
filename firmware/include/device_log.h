// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <ArduinoJson.h>

#include <string>

#include "log_level.h"
#include "logs_api.h"

// The device log (Phase 5). Every line goes three places:
//   1. Serial, as before.
//   2. A RAM ring of the last 200 lines, readable at GET /api/logs. Nothing is
//      written to flash (decision 3).
//   3. UDP syslog to the server, if a sink is configured.
//
// Logging never waits on the network. log() only formats, copies into the
// ring under a short mutex, and pokes the export task without waiting. The
// export task, at the lowest priority, reads the ring and sends each new line
// once with a non-blocking sendto(): no connection, no ack, no retry. A line
// the network refuses is dropped. Lines logged while the link is down or no
// sink is set stay in the ring and go out once both are there, up to 200 of
// them; older ones are counted as lost.
//
// Safe from any task, but not from an ISR or with interrupts off.
namespace device_log {

using logging::Level;

// First thing in setup(), before anything logs.
void begin();

// After the network and settings are up: starts the export task.
void startExport();

void write(Level level, const char* module, const char* message);
void log(Level level, const char* module, const char* format, ...)
    __attribute__((format(printf, 3, 4)));

// host: IPv4 literal, or empty for no export. Takes effect on the next send.
void setSink(const std::string& host, int port);

// Copies out of the ring under its lock.
logs_api::Snapshot snapshot(const logs_api::Query& q);

// The "logs" object for GET /api/status.
void describe(JsonObject out);

}  // namespace device_log

#define LOGE(module, ...) device_log::log(device_log::Level::kError, module, __VA_ARGS__)
#define LOGW(module, ...) device_log::log(device_log::Level::kWarn, module, __VA_ARGS__)
#define LOGI(module, ...) device_log::log(device_log::Level::kInfo, module, __VA_ARGS__)
#define LOGD(module, ...) device_log::log(device_log::Level::kDebug, module, __VA_ARGS__)
