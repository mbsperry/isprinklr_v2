// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "log_level.h"

// The device's recent log lines, in RAM only (decision 3: the ESP writes no
// logs to flash). Fixed capacity, allocated once; the oldest line is
// overwritten when it is full.
//
// Every line gets a sequence number, counting from 1 since boot. Readers (the
// HTTP endpoint, the UDP exporter) ask for lines after the last number they
// saw, so a reader that falls behind can tell how many it missed.
//
// Not thread-safe; the device serialises access (src/device_log.cpp).
namespace logging {

constexpr std::size_t kModuleBytes = 12;    // including the terminator
constexpr std::size_t kMessageBytes = 160;  // including the terminator

struct LogEntry {
  uint32_t seq = 0;
  uint32_t uptimeMs = 0;
  uint32_t epoch = 0;  // UTC seconds, 0 while the clock is not valid
  Level level = Level::kInfo;
  char module[kModuleBytes] = {};
  char message[kMessageBytes] = {};
};

class LogRing {
 public:
  explicit LogRing(std::size_t capacity);

  // Copies module and message, truncating them to fit (a truncated message
  // ends in "..."). Returns the line's sequence number.
  uint32_t push(Level level, const char* module, const char* message,
                uint32_t uptimeMs, uint32_t epoch);

  // Lines with seq > afterSeq, oldest first, at most limit of them, and only
  // those at maxLevel or more severe. Returns how many lines between afterSeq
  // and the oldest one still held were already overwritten.
  uint32_t since(uint32_t afterSeq, Level maxLevel, std::size_t limit,
                 std::vector<LogEntry>* out) const;

  uint32_t lastSeq() const { return nextSeq_ - 1; }
  // Oldest seq still held; lastSeq() + 1 when empty.
  uint32_t oldestSeq() const;
  std::size_t capacity() const { return slots_.size(); }
  std::size_t size() const { return count_; }

 private:
  std::vector<LogEntry> slots_;
  std::size_t head_ = 0;  // next slot to write
  std::size_t count_ = 0;
  uint32_t nextSeq_ = 1;
};

}  // namespace logging
