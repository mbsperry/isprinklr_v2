#include "web.h"

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <Update.h>
#include <esp_chip_info.h>
#include <esp_system.h>

#include "boot_guard.h"
#include "config.h"
#include "net.h"

namespace web {
namespace {

AsyncWebServer server(80);

// Set when an accepted upload needs a reboot; checked by tick().
uint32_t g_restartAt = 0;

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
  doc["phase"] = "0";
  doc["uptime_ms"] = millis();
  doc["reset_reason"] = resetReasonName(esp_reset_reason());

  // Phase 0 has no clock and no schedule store. Reported explicitly so a client
  // can tell "not implemented yet" from "implemented and broken".
  doc["time_valid"] = false;
  doc["schedule_source"] = "none";

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

void sendUpdateResult(AsyncWebServerRequest* request) {
  const bool ok = !Update.hasError();

  JsonDocument doc;
  doc["ok"] = ok;
  if (ok) {
    doc["message"] = "update accepted, rebooting";
  } else {
    doc["message"] = "update failed";
    doc["error"] = Update.errorString();
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
    Serial.printf("[ota] receiving %s\n", filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
      Serial.printf("[ota] begin failed: %s\n", Update.errorString());
      return;
    }
  }

  // A short write, a dropped connection, or a power cut leaves the running
  // firmware untouched — the boot partition only moves after Update.end(true)
  // verifies the image.
  if (len > 0 && Update.write(data, len) != len) {
    Serial.printf("[ota] write failed: %s\n", Update.errorString());
    Update.abort();
    return;
  }

  if (final) {
    if (Update.end(true)) {
      Serial.printf("[ota] complete, %u bytes\n",
                    static_cast<unsigned>(index + len));
    } else {
      Serial.printf("[ota] end failed: %s\n", Update.errorString());
    }
  }
}

}  // namespace

void begin() {
  server.on("/api/status", HTTP_GET, handleStatus);

  // curl -F "firmware=@firmware.bin" http://<ip>/api/update
  server.on("/api/update", HTTP_POST, sendUpdateResult, handleUpload);

  server.onNotFound([](AsyncWebServerRequest* request) {
    request->send(404, "application/json", "{\"error\":\"not found\"}");
  });

  server.begin();
  Serial.println("[web] listening on :80");
}

void tick() {
  if (g_restartAt != 0 && millis() >= g_restartAt) {
    Serial.println("[ota] restarting into new image");
    Serial.flush();
    esp_restart();
  }
}

}  // namespace web
