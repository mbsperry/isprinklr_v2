#pragma once

#include <string>

#include "kv_store.h"
#include "settings.h"

// Holds the current settings in memory and persists them as one NVS blob.
// Every change goes through replace(): validate, serialize, size-check,
// persist, and only then swap the in-memory copy, so a rejected or failed
// write leaves both memory and flash as they were.
//
// Not thread-safe; src/settings_service.cpp serialises access.
class SettingsStore {
 public:
  enum class LoadState { kNotLoaded, kEmpty, kLoaded, kCorrupt };
  enum class SaveResult { kOk, kInvalid, kTooLarge, kStorageFailed };

  explicit SettingsStore(KvStore& kv);

  // Reads the stored document. On kEmpty or kCorrupt the defaults stay in
  // place (no zones, no schedules, disabled), so a bad blob can never cause
  // watering. A corrupt blob is left in flash untouched until the next
  // successful replace().
  LoadState load();

  LoadState loadState() const { return loadState_; }
  const std::string& loadError() const { return loadError_; }

  const settings::Settings& current() const { return current_; }
  const std::string& hash() const { return hash_; }

  SaveResult replace(const settings::Settings& next, std::string* error);

 private:
  KvStore& kv_;
  settings::Settings current_;
  std::string hash_;
  LoadState loadState_ = LoadState::kNotLoaded;
  std::string loadError_;
};

const char* toString(SettingsStore::LoadState state);
