// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#include "log_ring.h"

#include <cstring>

namespace logging {
namespace {

// Copies src into dst (size n, n >= 4), marking truncation with "...".
void copyTruncated(char* dst, std::size_t n, const char* src) {
  if (src == nullptr) src = "";
  const std::size_t len = std::strlen(src);
  if (len < n) {
    std::memcpy(dst, src, len + 1);
    return;
  }
  std::memcpy(dst, src, n - 4);
  std::memcpy(dst + n - 4, "...", 4);
}

}  // namespace

const char* toString(Level level) {
  switch (level) {
    case Level::kError: return "error";
    case Level::kWarn: return "warn";
    case Level::kInfo: return "info";
    case Level::kDebug: return "debug";
  }
  return "info";
}

bool parseLevel(const char* text, Level* out) {
  if (text == nullptr) return false;
  static const Level kAll[] = {Level::kError, Level::kWarn, Level::kInfo,
                               Level::kDebug};
  for (Level l : kAll) {
    if (std::strcmp(text, toString(l)) == 0) {
      *out = l;
      return true;
    }
  }
  return false;
}

LogRing::LogRing(std::size_t capacity) : slots_(capacity == 0 ? 1 : capacity) {}

uint32_t LogRing::push(Level level, const char* module, const char* message,
                       uint32_t uptimeMs, uint32_t epoch) {
  LogEntry& e = slots_[head_];
  e.seq = nextSeq_++;
  e.uptimeMs = uptimeMs;
  e.epoch = epoch;
  e.level = level;
  copyTruncated(e.module, sizeof(e.module), module);
  copyTruncated(e.message, sizeof(e.message), message);
  head_ = (head_ + 1) % slots_.size();
  if (count_ < slots_.size()) ++count_;
  return e.seq;
}

uint32_t LogRing::oldestSeq() const { return nextSeq_ - count_; }

uint32_t LogRing::since(uint32_t afterSeq, Level maxLevel, std::size_t limit,
                        std::vector<LogEntry>* out) const {
  out->clear();
  const uint32_t oldest = oldestSeq();
  const uint32_t missed = afterSeq + 1 < oldest ? oldest - (afterSeq + 1) : 0;
  const uint32_t first = afterSeq + 1 > oldest ? afterSeq + 1 : oldest;
  const std::size_t start = (head_ + slots_.size() - count_) % slots_.size();
  for (uint32_t seq = first; seq < nextSeq_ && out->size() < limit; ++seq) {
    const LogEntry& e = slots_[(start + (seq - oldest)) % slots_.size()];
    if (static_cast<uint8_t>(e.level) <= static_cast<uint8_t>(maxLevel)) {
      out->push_back(e);
    }
  }
  return missed;
}

}  // namespace logging
