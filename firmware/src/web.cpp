// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#include "web.h"

#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <ESPAsyncWebServer.h>
#include <Update.h>
#include <esp_chip_info.h>
#include <esp_system.h>

#include "boot_guard.h"
#include "config.h"
#include "hunter_bus.h"
#include "net.h"
#include "sequencer_service.h"
#include "settings_api.h"
#include "settings_service.h"
#include "wallclock.h"
#include "zone_api.h"

namespace web {
namespace {

AsyncWebServer server(80);

// Zone request bodies are a few dozen bytes. Settings bodies use the verbose
// API shape, several times the stored size; 16 KB covers anything that could
// fit in flash after compaction.
constexpr size_t kMaxZoneBodyBytes = 256;
constexpr size_t kMaxSettingsBodyBytes = 16384;

// Set when an accepted upload needs a reboot; checked by tick().
uint32_t g_restartAt = 0;

// Upload state. Touched only from the async web task (upload and zone
// handlers), never from loop(), so Update is never driven from two tasks.
bool g_uploadRejected = false;
// Set only when Update.end(true) accepted a complete image. An empty file
// field, or a request with no file at all, never reaches it, so it must not be
// reported as success (it was, until Phase 4 testing caught it).
bool g_uploadSucceeded = false;
uint32_t g_lastUploadChunkMs = 0;

// An update is in flight and has not stalled. Update.isRunning() alone stays
// true forever if the client drops mid-upload.
bool updateActive() {
  return Update.isRunning() &&
         millis() - g_lastUploadChunkMs < config::kUploadStallMs;
}

const char* resetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON: return "power_on";
    case ESP_RST_EXT: return "external_pin";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "interrupt_watchdog";
    case ESP_RST_TASK_WDT: return "task_watchdog";
    case ESP_RST_WDT: return "other_watchdog";
    case ESP_RST_DEEPSLEEP: return "deep_sleep_wake";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO: return "sdio";
    default: return "unknown";
  }
}

void handleStatus(AsyncWebServerRequest* request) {
  JsonDocument doc;

  doc["firmware_version"] = FW_VERSION;
  doc["phase"] = "4";
  doc["uptime_ms"] = millis();
  doc["reset_reason"] = resetReasonName(esp_reset_reason());

  // Top-level time_valid per the API contract; details under "time".
  doc["time_valid"] = wallclock::timeValid();
  JsonObject time = doc["time"].to<JsonObject>();
  wallclock::describe(time);

  // Settings live in NVS. settings_hash is what the server compares against
  // its backup to detect drift.
  doc["schedule_source"] = "nvs";
  timekeeping::LocalTime today;
  const bool haveToday = wallclock::localNow(&today);
  settings_service::with([&](SettingsStore& store) {
    doc["settings_hash"] = store.hash();
    JsonObject settings = doc["settings"].to<JsonObject>();
    settings_api::describe(store, haveToday ? &today : nullptr, settings);
    return 0;
  });

  sequencer_service::describe(doc.as<JsonObject>());

  JsonObject bus = doc["hunter_bus"].to<JsonObject>();
  hunter_bus::describe(bus);

  JsonObject boot = doc["boot"].to<JsonObject>();
  boot_guard::describe(boot);

  JsonObject network = doc["network"].to<JsonObject>();
  net::describe(network);

  JsonObject memory = doc["memory"].to<JsonObject>();
  memory["free_heap"] = esp_get_free_heap_size();
  memory["min_free_heap"] = esp_get_minimum_free_heap_size();

  JsonObject chip = doc["chip"].to<JsonObject>();
  esp_chip_info_t info;
  esp_chip_info(&info);
  chip["cores"] = info.cores;
  chip["revision"] = info.revision;
  chip["idf_version"] = esp_get_idf_version();

  String out;
  serializeJson(doc, out);
  request->send(200, "application/json", out);
}

void sendJson(AsyncWebServerRequest* request, int status,
              const JsonDocument& body) {
  String out;
  serializeJson(body, out);
  request->send(status, "application/json", out);
}

// Route handlers only adapt between AsyncWebServer and zone_api, which holds
// the validation and response shapes and is host-tested.
// For routes that need settings: copied under the settings lock before the
// sequencer lock is taken, so the two are never held together. Only the async
// web task touches it.
settings::Settings g_settingsSnapshot;

void addZoneRoute(const char* path,
                  zone_api::Response (*handler)(seq::Sequencer&,
                                                JsonVariantConst, uint32_t),
                  bool needsSettings = false) {
  auto* h = new AsyncCallbackJsonWebHandler(
      path, [handler, needsSettings](AsyncWebServerRequest* request,
                                     JsonVariant& json) {
        // An accepted update reboots shortly; a zone frame queued now could
        // be cut off mid-bit. Refuse rather than start something we may not
        // finish sending.
        if (updateActive() || g_restartAt != 0) {
          request->send(503, "application/json",
                        "{\"ok\":false,\"error\":\"update_in_progress\","
                        "\"message\":\"firmware update in progress; retry after reboot\"}");
          return;
        }
        if (needsSettings) {
          g_settingsSnapshot = settings_service::with(
              [](SettingsStore& st) { return st.current(); });
        }
        const uint32_t nowMs = millis();
        const zone_api::Response r = sequencer_service::with(
            [&](seq::Sequencer& s) { return handler(s, json, nowMs); });
        Serial.printf("[web] %s -> %d\n", request->url().c_str(), r.status);
        sendJson(request, r.status, r.body);
      });
  h->setMethod(HTTP_POST);
  h->setMaxContentLength(kMaxZoneBodyBytes);
  server.addHandler(h);
}

zone_api::Response startZone(seq::Sequencer& s, JsonVariantConst body,
                             uint32_t) {
  return zone_api::start(s, body);
}

zone_api::Response runItems(seq::Sequencer& s, JsonVariantConst body,
                            uint32_t) {
  return zone_api::runItems(s, body);
}

zone_api::Response runScheduleNow(seq::Sequencer& s, JsonVariantConst body,
                                  uint32_t) {
  return zone_api::runSchedule(s, g_settingsSnapshot, body);
}

void addSettingsGet(const char* path,
                    api::Response (*handler)(const SettingsStore&)) {
  server.on(path, HTTP_GET, [handler](AsyncWebServerRequest* request) {
    const api::Response r = settings_service::with(
        [handler](SettingsStore& store) { return handler(store); });
    sendJson(request, r.status, r.body);
  });
}

// after runs (under the settings lock) only when the PUT succeeded.
void addSettingsPut(const char* path,
                    api::Response (*handler)(SettingsStore&, JsonVariantConst),
                    void (*after)(const SettingsStore&) = nullptr) {
  auto* h = new AsyncCallbackJsonWebHandler(
      path, [handler, after](AsyncWebServerRequest* request, JsonVariant& json) {
        const api::Response r =
            settings_service::with([&](SettingsStore& store) {
              api::Response result = handler(store, json);
              if (result.status == 200 && after != nullptr) after(store);
              return result;
            });
        Serial.printf("[web] PUT %s -> %d\n", request->url().c_str(), r.status);
        sendJson(request, r.status, r.body);
      });
  h->setMethod(HTTP_PUT);
  h->setMaxContentLength(kMaxSettingsBodyBytes);
  server.addHandler(h);
}

void applyConfig(const SettingsStore& store) {
  wallclock::setTimezone(store.current().config.timezone);
}

bool isJsonOnlyPath(const String& url) {
  return url.startsWith("/api/zone/") || url == "/api/run" ||
         url == "/api/schedules" ||
         url.startsWith("/api/schedule/") || url == "/api/zones" ||
         url == "/api/config";
}

void sendUpdateResult(AsyncWebServerRequest* request) {
  if (g_uploadRejected) {
    g_uploadRejected = false;
    AsyncWebServerResponse* response = request->beginResponse(
        503, "application/json",
        "{\"ok\":false,\"error\":\"busy\",\"message\":\"a run, a zone "
        "command or another upload is in progress; nothing was flashed. Cancel "
        "the run (POST /api/zone/stop {}) or retry shortly\"}");
    response->addHeader("Connection", "close");
    request->send(response);
    return;
  }

  const bool ok = g_uploadSucceeded && !Update.hasError();
  g_uploadSucceeded = false;

  JsonDocument doc;
  doc["ok"] = ok;
  if (ok) {
    doc["message"] = "update accepted, rebooting";
  } else {
    doc["message"] = "update failed; running firmware unchanged";
    doc["error"] = Update.hasError() ? Update.errorString()
                                     : "no complete firmware image received";
  }

  String out;
  serializeJson(doc, out);

  AsyncWebServerResponse* response =
      request->beginResponse(ok ? 200 : 500, "application/json", out);
  response->addHeader("Connection", "close");
  request->send(response);

  if (ok) g_restartAt = millis() + config::kRestartDelayMs;
}

void handleUpload(AsyncWebServerRequest* request, const String& filename,
                  size_t index, uint8_t* data, size_t len, bool final) {
  if (index == 0) {
    g_lastUploadChunkMs = millis();
    g_uploadSucceeded = false;

    // Refuse to start while a frame is on the wire or queued: the reboot at the
    // end could otherwise land mid-frame. Zone routes are refused while an
    // update runs, so the bus cannot get busy again until this one finishes.
    // A second upload while one is live is refused too; a stalled one is
    // aborted and replaced.
    // A run in progress is refused too: rebooting mid-run is exactly the
    // case reboot recovery exists for, and there is no reason to cause it.
    g_uploadRejected = !hunter_bus::idle() || updateActive() ||
                       !sequencer_service::idle();
    if (g_uploadRejected) {
      Serial.println("[ota] refused: run in progress, bus busy, or another upload");
      return;
    }

    if (Update.isRunning()) {
      Serial.println("[ota] aborting stalled previous upload");
      Update.abort();
    }

    Serial.printf("[ota] receiving %s\n", filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
      Serial.printf("[ota] begin failed: %s\n", Update.errorString());
      return;
    }
  }

  if (g_uploadRejected) return;
  g_lastUploadChunkMs = millis();

  // A short write, a dropped connection, or a power cut leaves the running
  // firmware untouched — the boot partition only moves after Update.end(true)
  // verifies the image.
  if (len > 0 && Update.write(data, len) != len) {
    Serial.printf("[ota] write failed: %s\n", Update.errorString());
    Update.abort();
    return;
  }

  if (final) {
    if (index + len > 0 && Update.end(true)) {
      g_uploadSucceeded = true;
      Serial.printf("[ota] complete, %u bytes\n",
                    static_cast<unsigned>(index + len));
    } else {
      Serial.printf("[ota] end failed: %s\n", Update.errorString());
      // Release it now rather than leave zone commands blocked until the
      // stall timeout.
      if (Update.isRunning()) Update.abort();
    }
  }
}

}  // namespace

void begin() {
  server.on("/api/status", HTTP_GET, handleStatus);

  // curl -H "Content-Type: application/json" -d '{"zone":3,"duration_minutes":2}' http://<ip>/api/zone/start
  addZoneRoute("/api/zone/start", startZone);
  addZoneRoute("/api/zone/stop", zone_api::stop);

  // Reboot. Refused (409) while a run is in progress unless ?force=1, which
  // exists so reboot recovery can be tested without a power cut.
  server.on("/api/restart", HTTP_POST, [](AsyncWebServerRequest* request) {
    const bool force = request->hasParam("force") &&
                       request->getParam("force")->value() == "1";
    if (!force && !sequencer_service::idle()) {
      request->send(409, "application/json",
                    "{\"ok\":false,\"error\":\"run_in_progress\","
                    "\"message\":\"a run is in progress; cancel it or add ?force=1\"}");
      return;
    }
    Serial.printf("[web] restart requested%s\n", force ? " (forced)" : "");
    AsyncWebServerResponse* response = request->beginResponse(
        200, "application/json", "{\"ok\":true,\"message\":\"restarting\"}");
    response->addHeader("Connection", "close");
    request->send(response);
    g_restartAt = millis() + config::kRestartDelayMs;
  });

  // No body needed: curl -X POST http://<ip>/api/run/skip
  server.on("/api/run/skip", HTTP_POST, [](AsyncWebServerRequest* request) {
    const uint32_t nowMs = millis();
    const zone_api::Response r = sequencer_service::with(
        [nowMs](seq::Sequencer& s) { return zone_api::skip(s, nowMs); });
    Serial.printf("[web] /api/run/skip -> %d\n", r.status);
    sendJson(request, r.status, r.body);
  });

  // Run a saved schedule now, or an ad-hoc list. Both go through the
  // sequencer like manual starts; neither touches stored settings.
  // Registered after /api/run/skip: a plain-string route also matches its
  // sub-paths, so the more specific one must come first.
  addZoneRoute("/api/schedule/run", runScheduleNow, /*needsSettings=*/true);
  addZoneRoute("/api/run", runItems);

  addSettingsGet("/api/schedules", settings_api::getSchedules);
  addSettingsPut("/api/schedules", settings_api::putSchedules);
  addSettingsPut("/api/schedule/active", settings_api::putActive);
  addSettingsGet("/api/schedule/enabled", settings_api::getEnabled);
  addSettingsPut("/api/schedule/enabled", settings_api::putEnabled);
  addSettingsGet("/api/zones", settings_api::getZones);
  addSettingsPut("/api/zones", settings_api::putZones);
  addSettingsGet("/api/config", settings_api::getConfig);
  addSettingsPut("/api/config", settings_api::putConfig, applyConfig);

  // curl -F "firmware=@firmware.bin" http://<ip>/api/update
  server.on("/api/update", HTTP_POST, sendUpdateResult, handleUpload);

  server.onNotFound([](AsyncWebServerRequest* request) {
    // The JSON handlers only claim application/json requests, so a write
    // without that header lands here. Say why rather than 404.
    if (request->method() != HTTP_GET && isJsonOnlyPath(request->url())) {
      request->send(415, "application/json",
                    "{\"ok\":false,\"error\":\"unsupported_media_type\","
                    "\"message\":\"send the body with Content-Type: application/json\"}");
      return;
    }
    request->send(404, "application/json", "{\"error\":\"not found\"}");
  });

  server.begin();
  Serial.println("[web] listening on :80");
}

void tick() {
  // Never reboot mid-frame. An upload is refused unless the bus is idle and
  // zone routes are refused from then on, so this should already hold; the
  // check is the backstop.
  if (g_restartAt != 0 && millis() >= g_restartAt && hunter_bus::idle()) {
    Serial.println("[ota] restarting into new image");
    Serial.flush();
    esp_restart();
  }
}

}  // namespace web
