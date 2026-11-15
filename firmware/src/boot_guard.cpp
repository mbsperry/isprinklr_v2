#include "boot_guard.h"

#include <esp_ota_ops.h>
#include <esp_partition.h>

#include "boot_counter.h"
#include "config.h"
#include "nvs_kv_store.h"

namespace boot_guard {
namespace {

BootCounter& counter() {
  static BootCounter c(nvsStore(), config::kMaxFailedBoots);
  return c;
}

// Switches the boot partition to the inactive OTA slot. Returns false if there
// is nothing valid to fall back to (e.g. the first ever OTA-capable image, where
// the other slot is empty).
bool revertToOtherSlot() {
  const esp_partition_t* other = esp_ota_get_next_update_partition(nullptr);
  if (other == nullptr) {
    Serial.println("[boot] no alternate OTA slot");
    return false;
  }

  esp_app_desc_t desc;
  if (esp_ota_get_partition_description(other, &desc) != ESP_OK) {
    Serial.printf("[boot] alternate slot %s holds no valid app\n", other->label);
    return false;
  }

  const esp_err_t err = esp_ota_set_boot_partition(other);
  if (err != ESP_OK) {
    Serial.printf("[boot] set_boot_partition(%s) failed: %s\n", other->label,
                  esp_err_to_name(err));
    return false;
  }

  Serial.printf("[boot] reverting to %s (%s)\n", other->label, desc.version);
  return true;
}

}  // namespace

void begin() {
  const BootCounter::Decision decision = counter().onBoot();
  if (counter().storeFailed()) {
    Serial.println("[boot] NVS write failed; boot guard cannot count");
  }

  if (decision == BootCounter::Decision::kRevert) {
    Serial.printf("[boot] %u consecutive unhealthy boots\n",
                  config::kMaxFailedBoots);
    if (revertToOtherSlot()) {
      delay(100);
      esp_restart();
    }
    Serial.println("[boot] cannot revert; continuing on current image");
    return;
  }

  Serial.printf("[boot] attempt %u of %u before revert\n",
                counter().failedBoots() + 1, config::kMaxFailedBoots);
}

void markHealthy() {
  if (counter().healthy()) return;
  counter().markHealthy();

  // Harmless when IDF has already auto-confirmed at startup, and correct if a
  // future build ever switches to app-controlled confirmation.
  esp_ota_mark_app_valid_cancel_rollback();

  Serial.println("[boot] marked healthy");
}

uint32_t failedBoots() { return counter().failedBoots(); }

bool isHealthy() { return counter().healthy(); }

void describe(JsonObject obj) {
  obj["healthy"] = counter().healthy();
  obj["failed_boots"] = counter().failedBoots();
  obj["max_failed_boots"] = config::kMaxFailedBoots;
  obj["store_failed"] = counter().storeFailed();

  const esp_partition_t* running = esp_ota_get_running_partition();
  if (running != nullptr) obj["running_partition"] = running->label;

  const esp_partition_t* other = esp_ota_get_next_update_partition(nullptr);
  if (other != nullptr) obj["update_partition"] = other->label;
}

}  // namespace boot_guard
