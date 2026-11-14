#include "boot_guard.h"

#include <Preferences.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

#include "config.h"

namespace boot_guard {
namespace {

constexpr char kNamespace[] = "bootguard";
constexpr char kKeyFails[] = "fails";

Preferences prefs;
uint32_t g_failedBoots = 0;
bool g_healthy = false;

uint32_t readFails() {
  if (!prefs.begin(kNamespace, /*readOnly=*/true)) return 0;
  const uint32_t n = prefs.getUInt(kKeyFails, 0);
  prefs.end();
  return n;
}

void writeFails(uint32_t n) {
  if (!prefs.begin(kNamespace, /*readOnly=*/false)) {
    Serial.println("[boot] NVS open failed; boot guard inactive");
    return;
  }
  prefs.putUInt(kKeyFails, n);
  prefs.end();
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
  const uint32_t previous = readFails();
  g_failedBoots = previous;

  if (previous >= config::kMaxFailedBoots) {
    Serial.printf("[boot] %u consecutive unhealthy boots\n", previous);

    // Clear before restarting either way: if the revert works the new image gets
    // a clean slate, and if it does not, we must not loop on this branch forever.
    writeFails(0);

    if (revertToOtherSlot()) {
      delay(100);
      esp_restart();
    }

    Serial.println("[boot] cannot revert; continuing on current image");
    g_failedBoots = 0;
    return;
  }

  writeFails(previous + 1);
  Serial.printf("[boot] attempt %u of %u before revert\n", previous + 1,
                config::kMaxFailedBoots);
}

void markHealthy() {
  if (g_healthy) return;
  g_healthy = true;
  writeFails(0);

  // Harmless when IDF has already auto-confirmed at startup, and correct if a
  // future build ever switches to app-controlled confirmation.
  esp_ota_mark_app_valid_cancel_rollback();

  Serial.println("[boot] marked healthy");
}

uint32_t failedBoots() { return g_failedBoots; }

bool isHealthy() { return g_healthy; }

void describe(JsonObject obj) {
  obj["healthy"] = g_healthy;
  obj["failed_boots"] = g_failedBoots;
  obj["max_failed_boots"] = config::kMaxFailedBoots;

  const esp_partition_t* running = esp_ota_get_running_partition();
  if (running != nullptr) obj["running_partition"] = running->label;

  const esp_partition_t* other = esp_ota_get_next_update_partition(nullptr);
  if (other != nullptr) obj["update_partition"] = other->label;
}

}  // namespace boot_guard
