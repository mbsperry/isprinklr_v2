// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settings_service.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "device_log.h"
#include "nvs_kv_store.h"

namespace settings_service {
namespace {

SemaphoreHandle_t g_mutex = nullptr;

}  // namespace

SettingsStore& store() {
  static SettingsStore s(nvsStore());
  return s;
}

void lock() { xSemaphoreTake(g_mutex, portMAX_DELAY); }
void unlock() { xSemaphoreGive(g_mutex); }

void begin() {
  g_mutex = xSemaphoreCreateMutex();
  with([](SettingsStore& s) {
    const SettingsStore::LoadState state = s.load();
    LOGI("settings", "%s, hash %s, %u zones, %u schedules, active '%s', %s",
                  toString(state), s.hash().c_str(),
                  static_cast<unsigned>(s.current().zones.size()),
                  static_cast<unsigned>(s.current().schedules.size()),
                  s.current().activeSchedule.c_str(),
                  s.current().enabled ? "enabled" : "disabled");
    if (state == SettingsStore::LoadState::kCorrupt) {
      LOGE("settings", "stored document rejected: %s",
                    s.loadError().c_str());
    }
    return 0;
  });
}

}  // namespace settings_service
