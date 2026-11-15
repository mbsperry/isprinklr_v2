// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <utility>

#include "settings_store.h"

// The device's one SettingsStore, on NVS, behind a mutex. Today only the async
// web task touches it; the Phase 4 scheduler will read it from another task,
// so every access already goes through with().
namespace settings_service {

// Loads settings from NVS. Call once in setup(), before the web server starts.
void begin();

void lock();
void unlock();
SettingsStore& store();

// Runs fn(SettingsStore&) with exclusive access and returns its result.
template <typename F>
auto with(F&& fn) -> decltype(fn(store())) {
  struct Guard {
    Guard() { lock(); }
    ~Guard() { unlock(); }
  } guard;
  return std::forward<F>(fn)(store());
}

}  // namespace settings_service
