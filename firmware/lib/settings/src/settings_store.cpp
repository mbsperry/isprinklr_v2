// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settings_store.h"

#include "settings_json.h"
#include "settings_limits.h"

namespace {
constexpr char kNamespace[] = "settings";
constexpr char kKey[] = "doc";
}  // namespace

SettingsStore::SettingsStore(KvStore& kv) : kv_(kv) {
  hash_ = settings::documentHash(settings::serializeDocument(current_));
}

SettingsStore::LoadState SettingsStore::load() {
  std::string blob;
  if (!kv_.getBlob(kNamespace, kKey, &blob)) {
    loadState_ = LoadState::kEmpty;
    return loadState_;
  }

  settings::Settings parsed;
  if (!settings::parseDocument(blob, &parsed, &loadError_)) {
    loadState_ = LoadState::kCorrupt;
    return loadState_;
  }

  current_ = std::move(parsed);
  hash_ = settings::documentHash(settings::serializeDocument(current_));
  loadState_ = LoadState::kLoaded;
  return loadState_;
}

SettingsStore::SaveResult SettingsStore::replace(const settings::Settings& next,
                                                 std::string* error) {
  if (!settings::validate(next, error)) return SaveResult::kInvalid;

  const std::string doc = settings::serializeDocument(next);
  if (doc.size() > settings::kMaxDocumentBytes) {
    *error = "settings would take " + std::to_string(doc.size()) +
             " bytes; the limit is " +
             std::to_string(settings::kMaxDocumentBytes);
    return SaveResult::kTooLarge;
  }

  if (!kv_.putBlob(kNamespace, kKey, doc)) {
    *error = "could not write settings to flash; nothing changed";
    return SaveResult::kStorageFailed;
  }

  current_ = next;
  hash_ = settings::documentHash(doc);
  loadState_ = LoadState::kLoaded;
  loadError_.clear();
  return SaveResult::kOk;
}

const char* toString(SettingsStore::LoadState state) {
  switch (state) {
    case SettingsStore::LoadState::kNotLoaded: return "not_loaded";
    case SettingsStore::LoadState::kEmpty: return "empty";
    case SettingsStore::LoadState::kLoaded: return "loaded";
    case SettingsStore::LoadState::kCorrupt: return "corrupt";
  }
  return "unknown";
}
