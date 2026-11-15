// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "kv_store.h"

// KvStore on NVS (Arduino Preferences). Every call takes one process-wide mutex:
// NVS is not thread-safe, and the async web task and the main loop both reach
// storage.
class NvsKvStore : public KvStore {
 public:
  bool getU32(const char* ns, const char* key, uint32_t* out) override;
  bool putU32(const char* ns, const char* key, uint32_t value) override;
};

// The one instance. All NVS access goes through it.
NvsKvStore& nvsStore();
