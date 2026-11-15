// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <ArduinoJson.h>

#include <string>

// What every host-tested route handler returns: an HTTP status and a JSON body.
// src/web.cpp turns it into an AsyncWebServer response and does nothing else.
namespace api {

struct Response {
  int status;
  JsonDocument body;
};

// {"ok": false, "error": code, "message": message}
inline Response error(int status, const char* code, const std::string& message) {
  Response r{status, JsonDocument()};
  r.body["ok"] = false;
  r.body["error"] = code;
  r.body["message"] = message;
  return r;
}

}  // namespace api
