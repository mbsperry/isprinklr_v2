// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <string>

// Persistent key-value storage, behind an interface so logic that persists state
// can be host-tested against MemoryKvStore. The device implementation is
// src/nvs_kv_store.cpp (NVS via Preferences, serialised by a mutex).
//
// Deliberately minimal: counters (boot guard) and byte blobs (the settings
// document, Phase 3).
class KvStore {
 public:
  virtual ~KvStore() = default;

  // Returns false if the key is absent or the store could not be read; *out is
  // then unchanged.
  virtual bool getU32(const char* ns, const char* key, uint32_t* out) = 0;

  // Returns false if the write did not persist.
  virtual bool putU32(const char* ns, const char* key, uint32_t value) = 0;

  // Blob read: false if absent or unreadable; *out is then unchanged.
  virtual bool getBlob(const char* ns, const char* key, std::string* out) = 0;

  // Blob write: false if it did not persist. On failure the previous value is
  // still readable (NVS writes the new blob before erasing the old one).
  virtual bool putBlob(const char* ns, const char* key,
                       const std::string& value) = 0;
};
