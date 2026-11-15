// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <map>
#include <string>

#include "kv_store.h"

// In-memory KvStore for host tests. failWrites simulates an NVS write failure;
// blobWrites counts successful blob writes so tests can assert a rejected
// request touched nothing.
class MemoryKvStore : public KvStore {
 public:
  bool failWrites = false;
  int blobWrites = 0;

  bool getU32(const char* ns, const char* key, uint32_t* out) override {
    const auto it = values_.find(join(ns, key));
    if (it == values_.end()) return false;
    *out = it->second;
    return true;
  }

  bool putU32(const char* ns, const char* key, uint32_t value) override {
    if (failWrites) return false;
    values_[join(ns, key)] = value;
    return true;
  }

  bool getBlob(const char* ns, const char* key, std::string* out) override {
    const auto it = blobs_.find(join(ns, key));
    if (it == blobs_.end()) return false;
    *out = it->second;
    return true;
  }

  bool putBlob(const char* ns, const char* key,
               const std::string& value) override {
    if (failWrites) return false;
    blobs_[join(ns, key)] = value;
    ++blobWrites;
    return true;
  }

 private:
  static std::string join(const char* ns, const char* key) {
    return std::string(ns) + "/" + key;
  }

  std::map<std::string, uint32_t> values_;
  std::map<std::string, std::string> blobs_;
};
