#pragma once

#include <map>
#include <string>

#include "kv_store.h"

// In-memory KvStore for host tests. failWrites simulates an NVS write failure.
class MemoryKvStore : public KvStore {
 public:
  bool failWrites = false;

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

 private:
  static std::string join(const char* ns, const char* key) {
    return std::string(ns) + "/" + key;
  }

  std::map<std::string, uint32_t> values_;
};
