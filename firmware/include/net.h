#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <IPAddress.h>

// Network bring-up. Which transport is compiled in is decided by NET_USE_ETHERNET
// from the build environment, not at runtime — v1 carried both stacks in every
// build and chose with a mode flag, which doubled the surface for no benefit on a
// device that is permanently wired.
namespace net {

// Starts the configured transport and waits up to config::kNetworkTimeoutMs for
// an address. Returns true if an address was obtained.
bool begin();

// True when the interface currently holds a non-zero address.
bool isUp();

// Attempts to re-establish a dropped link. Safe to call repeatedly.
void reconnect();

IPAddress localIP();

// "Ethernet" or "WiFi".
const char* transport();

// Fills obj with transport-specific details for the status endpoint.
void describe(JsonObject obj);

}  // namespace net
