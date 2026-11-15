// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

// Persistent key-value storage, behind an interface so logic that persists state
// can be host-tested against MemoryKvStore. The device implementation is
// src/nvs_kv_store.cpp (NVS via Preferences, serialised by a mutex).
//
// Deliberately minimal: Phase 1 only needs counters. Phase 3 adds what schedules
// need (blobs or strings) when there is a caller to design them against.
class KvStore {
 public:
  virtual ~KvStore() = default;

  // Returns false if the key is absent or the store could not be read; *out is
  // then unchanged.
  virtual bool getU32(const char* ns, const char* key, uint32_t* out) = 0;

  // Returns false if the write did not persist.
  virtual bool putU32(const char* ns, const char* key, uint32_t value) = 0;
};
