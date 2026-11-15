// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#include "nvs_kv_store.h"

#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace {

SemaphoreHandle_t mutex() {
  // Created on first use; the first call happens in setup(), before any other
  // task that touches storage exists, so this initialisation does not race.
  static SemaphoreHandle_t m = xSemaphoreCreateMutex();
  return m;
}

class Lock {
 public:
  Lock() { xSemaphoreTake(mutex(), portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(mutex()); }
};

}  // namespace

bool NvsKvStore::getU32(const char* ns, const char* key, uint32_t* out) {
  Lock lock;
  Preferences prefs;
  if (!prefs.begin(ns, /*readOnly=*/true)) return false;  // namespace absent
  const bool present = prefs.isKey(key);
  if (present) *out = prefs.getUInt(key, 0);
  prefs.end();
  return present;
}

bool NvsKvStore::putU32(const char* ns, const char* key, uint32_t value) {
  Lock lock;
  Preferences prefs;
  if (!prefs.begin(ns, /*readOnly=*/false)) return false;
  const bool ok = prefs.putUInt(key, value) == sizeof(uint32_t);
  prefs.end();
  return ok;
}

bool NvsKvStore::getBlob(const char* ns, const char* key, std::string* out) {
  Lock lock;
  Preferences prefs;
  if (!prefs.begin(ns, /*readOnly=*/true)) return false;
  bool ok = false;
  const size_t len = prefs.isKey(key) ? prefs.getBytesLength(key) : 0;
  if (len > 0) {
    std::string buf(len, '\0');
    ok = prefs.getBytes(key, &buf[0], len) == len;
    if (ok) *out = std::move(buf);
  }
  prefs.end();
  return ok;
}

bool NvsKvStore::putBlob(const char* ns, const char* key,
                         const std::string& value) {
  Lock lock;
  Preferences prefs;
  if (!prefs.begin(ns, /*readOnly=*/false)) return false;
  const bool ok =
      prefs.putBytes(key, value.data(), value.size()) == value.size();
  prefs.end();
  return ok;
}

NvsKvStore& nvsStore() {
  static NvsKvStore store;
  return store;
}
