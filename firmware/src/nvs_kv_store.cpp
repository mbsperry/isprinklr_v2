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

NvsKvStore& nvsStore() {
  static NvsKvStore store;
  return store;
}
