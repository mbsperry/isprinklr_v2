#pragma once

#include <ArduinoJson.h>

#include "hunter_frame.h"

// The I/O edge of the Hunter driver: owns the REM pin and plays frames onto it
// from a dedicated FreeRTOS task.
//
// One frame takes about 0.65 s on the wire (390 ms reset plus ~2.1 ms per bit).
// v1 did that inside the async HTTP handler, stalling the web server for the
// duration. Here callers only enqueue; the bus task does the waiting.
namespace hunter_bus {

// Drives the pin LOW (idle) and starts the bus task. Call once, early in setup().
void begin(int pin);

// Queues a frame for transmission without blocking. Returns false if the queue
// is full or begin() has not run. Frames go out in the order queued.
bool enqueue(const hunter::Frame& frame);

// Counters and state for GET /api/status.
void describe(JsonObject obj);

}  // namespace hunter_bus
