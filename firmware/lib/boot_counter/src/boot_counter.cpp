#include "boot_counter.h"

namespace {
constexpr char kNamespace[] = "bootguard";
constexpr char kKeyFails[] = "fails";
}  // namespace

BootCounter::Decision BootCounter::onBoot() {
  uint32_t previous = 0;
  store_.getU32(kNamespace, kKeyFails, &previous);  // absent means 0

  if (previous >= max_) {
    // Clear before reverting either way: the reverted image gets a clean slate,
    // and if the revert is impossible we must not take this branch forever.
    storeFailed_ = !store_.putU32(kNamespace, kKeyFails, 0);
    failed_ = 0;
    return Decision::kRevert;
  }

  failed_ = previous;
  storeFailed_ = !store_.putU32(kNamespace, kKeyFails, previous + 1);
  return Decision::kContinue;
}

void BootCounter::markHealthy() {
  if (healthy_) return;
  healthy_ = true;
  if (!store_.putU32(kNamespace, kKeyFails, 0)) storeFailed_ = true;
}
