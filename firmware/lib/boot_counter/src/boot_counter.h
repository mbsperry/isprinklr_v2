// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

#include "kv_store.h"

// The decision half of the boot guard (src/boot_guard.cpp does the partition
// switch). Counts boots that never reached a healthy state; once the count
// reaches the threshold, says to revert.
class BootCounter {
 public:
  enum class Decision { kContinue, kRevert };

  BootCounter(KvStore& store, uint32_t maxFailedBoots)
      : store_(store), max_(maxFailedBoots) {}

  // Call once per boot, before anything that can fail. On kRevert the stored
  // count has already been reset, so a revert that cannot happen does not loop.
  Decision onBoot();

  // Clears the stored count. Idempotent; only the first call writes.
  void markHealthy();

  // Unhealthy boots before this one (0 after a revert decision).
  uint32_t failedBoots() const { return failed_; }
  bool healthy() const { return healthy_; }

  // The store refused a write, so the guard cannot count.
  bool storeFailed() const { return storeFailed_; }

 private:
  KvStore& store_;
  uint32_t max_;
  uint32_t failed_ = 0;
  bool healthy_ = false;
  bool storeFailed_ = false;
};
