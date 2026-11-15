// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sequencer.h"

namespace seq {
namespace {
constexpr uint32_t kMsPerMinute = 60u * 1000u;
}  // namespace

Sequencer::Sequencer(ZonePort& port, Timing timing)
    : port_(port), timing_(timing) {}

void Sequencer::beginRunIfIdle() {
  if (!runOver_) return;
  entries_.clear();
  runName_.clear();
  index_ = 0;
  runOver_ = false;
}

bool Sequencer::add(int zone, int durationMinutes, Origin origin) {
  beginRunIfIdle();
  if (entries_.size() >= kMaxRunEntries) return false;
  entries_.push_back(Entry{zone, durationMinutes, origin});
  ++revision_;
  return true;
}

bool Sequencer::addRun(const std::string& scheduleName,
                       const std::vector<Entry>& entries) {
  beginRunIfIdle();
  if (entries_.size() + entries.size() > kMaxRunEntries) return false;
  if (runName_.empty()) runName_ = scheduleName;
  for (Entry e : entries) {
    e.origin = Origin::kSchedule;
    e.outcome = Outcome::kPending;
    e.startedEpoch = 0;
    entries_.push_back(e);
  }
  ++revision_;
  return true;
}

std::optional<std::size_t> Sequencer::currentIndex() const {
  if (state_ == State::kIdle || index_ >= entries_.size()) return std::nullopt;
  return index_;
}

uint32_t Sequencer::currentRemainingMs(uint32_t nowMs) const {
  if (state_ != State::kRunning) return 0;
  const uint32_t total =
      static_cast<uint32_t>(entries_[index_].durationMinutes) * kMsPerMinute;
  const uint32_t elapsed = nowMs - phaseStartMs_;
  return elapsed >= total ? 0 : total - elapsed;
}

void Sequencer::interruptRemaining() {
  for (Entry& e : entries_) {
    if (e.outcome == Outcome::kPending || e.outcome == Outcome::kRunning) {
      e.outcome = Outcome::kInterrupted;
    }
  }
}

void Sequencer::cancel(uint32_t nowMs) {
  if (state_ == State::kRunning || state_ == State::kStarting) {
    // In kStarting the start may have reached the bus on an earlier attempt
    // that reported busy; stopping is harmless either way.
    const int zone = entries_[index_].zone;
    if (!port_.stop(zone)) {
      pendingStop_ = zone;
      pendingStopAttempts_ = 1;
      pendingStopLastMs_ = nowMs;
    }
  }
  interruptRemaining();
  state_ = State::kIdle;
  runOver_ = true;
  ++revision_;
}

bool Sequencer::skip(uint32_t nowMs) {
  if (runOver_ || index_ >= entries_.size()) return false;
  Entry& e = entries_[index_];
  if (state_ == State::kRunning || state_ == State::kStarting) {
    if (!port_.stop(e.zone)) {
      pendingStop_ = e.zone;
      pendingStopAttempts_ = 1;
      pendingStopLastMs_ = nowMs;
    }
  }
  e.outcome = Outcome::kSkipped;
  ++revision_;
  advance(nowMs);
  return true;
}

void Sequencer::stopZone(int zone, uint32_t nowMs) {
  if ((state_ == State::kRunning || state_ == State::kStarting) &&
      entries_[index_].zone == zone) {
    cancel(nowMs);
    return;
  }
  if (!port_.stop(zone)) {
    pendingStop_ = zone;
    pendingStopAttempts_ = 1;
    pendingStopLastMs_ = nowMs;
  }
}

void Sequencer::retryPendingStop(uint32_t nowMs) {
  if (!pendingStop_ || nowMs - pendingStopLastMs_ < timing_.retryMs) return;
  pendingStopLastMs_ = nowMs;
  if (port_.stop(*pendingStop_) ||
      ++pendingStopAttempts_ >= timing_.maxAttempts) {
    // Giving up is safe: every start carried a bounded duration.
    pendingStop_.reset();
  }
}

void Sequencer::startCurrent(uint32_t nowMs, uint32_t nowEpoch) {
  Entry& e = entries_[index_];
  if (attempts_ > 0 && nowMs - lastAttemptMs_ < timing_.retryMs) return;
  lastAttemptMs_ = nowMs;
  ++attempts_;

  if (port_.start(e.zone, e.durationMinutes)) {
    e.outcome = Outcome::kRunning;
    e.startedEpoch = nowEpoch;
    state_ = State::kRunning;
    phaseStartMs_ = nowMs;
    ++revision_;
    return;
  }

  if (attempts_ >= timing_.maxAttempts) {
    // The bus is not taking commands. Abandon the run rather than run zones
    // out of order or hours late.
    e.outcome = Outcome::kFailed;
    for (Entry& rest : entries_) {
      if (rest.outcome == Outcome::kPending) rest.outcome = Outcome::kInterrupted;
    }
    state_ = State::kIdle;
    runOver_ = true;
    ++revision_;
  }
}

void Sequencer::finishCurrent(uint32_t nowMs) {
  Entry& e = entries_[index_];
  e.outcome = Outcome::kCompleted;
  // The Pro-C ends the zone on its own at about this moment; the explicit stop
  // makes the hand-over to the next zone independent of its clock.
  if (!port_.stop(e.zone)) {
    pendingStop_ = e.zone;
    pendingStopAttempts_ = 1;
    pendingStopLastMs_ = nowMs;
  }
  ++revision_;
  advance(nowMs);
}

void Sequencer::advance(uint32_t nowMs) {
  ++index_;
  attempts_ = 0;
  if (index_ < entries_.size()) {
    state_ = State::kPadding;
    phaseStartMs_ = nowMs;
  } else {
    state_ = State::kIdle;
    runOver_ = true;
  }
}

void Sequencer::tick(uint32_t nowMs, uint32_t nowEpoch) {
  retryPendingStop(nowMs);

  switch (state_) {
    case State::kIdle:
      if (!runOver_ && index_ < entries_.size()) {
        state_ = State::kStarting;
        attempts_ = 0;
        startCurrent(nowMs, nowEpoch);
      }
      return;

    case State::kStarting:
      startCurrent(nowMs, nowEpoch);
      return;

    case State::kRunning: {
      const uint32_t total =
          static_cast<uint32_t>(entries_[index_].durationMinutes) * kMsPerMinute;
      if (nowMs - phaseStartMs_ >= total) finishCurrent(nowMs);
      return;
    }

    case State::kPadding:
      // Hold off while a stop is still being retried, so start(B) never
      // overtakes stop(A).
      if (!pendingStop_ && nowMs - phaseStartMs_ >= timing_.padMs) {
        state_ = State::kStarting;
        startCurrent(nowMs, nowEpoch);
      }
      return;
  }
}

void Sequencer::resume(const std::string& runName, std::vector<Entry> entries,
                       std::size_t index, uint32_t runningRemainingMs,
                       uint32_t nowMs) {
  entries_ = std::move(entries);
  runName_ = runName;
  index_ = index;
  runOver_ = index_ >= entries_.size();
  attempts_ = 0;
  ++revision_;
  if (runOver_) {
    state_ = State::kIdle;
    return;
  }

  Entry& e = entries_[index_];
  if (runningRemainingMs > 0 && e.outcome == Outcome::kRunning) {
    // Back-date the phase so the remaining time matches the Pro-C's.
    const uint32_t total = static_cast<uint32_t>(e.durationMinutes) * kMsPerMinute;
    const uint32_t remaining = runningRemainingMs < total ? runningRemainingMs : total;
    state_ = State::kRunning;
    phaseStartMs_ = nowMs - (total - remaining);
    return;
  }

  if (e.outcome == Outcome::kRunning) e.outcome = Outcome::kCompleted;
  // The zone at index finished (or never started); continue from the first
  // pending entry.
  while (index_ < entries_.size() && entries_[index_].outcome != Outcome::kPending) {
    ++index_;
  }
  if (index_ >= entries_.size()) {
    state_ = State::kIdle;
    runOver_ = true;
    return;
  }
  state_ = State::kPadding;
  phaseStartMs_ = nowMs;
}

void Sequencer::showInterrupted(const std::string& runName,
                                std::vector<Entry> entries) {
  entries_ = std::move(entries);
  runName_ = runName;
  for (Entry& e : entries_) {
    // A zone that was running has finished on the Pro-C by itself (or will).
    if (e.outcome == Outcome::kPending) e.outcome = Outcome::kInterrupted;
  }
  index_ = entries_.size();
  state_ = State::kIdle;
  runOver_ = true;
  ++revision_;
}

const char* toString(Sequencer::State state) {
  switch (state) {
    case Sequencer::State::kIdle: return "idle";
    case Sequencer::State::kStarting: return "starting";
    case Sequencer::State::kRunning: return "running";
    case Sequencer::State::kPadding: return "padding";
  }
  return "unknown";
}

const char* toString(Outcome outcome) {
  switch (outcome) {
    case Outcome::kPending: return "pending";
    case Outcome::kRunning: return "running";
    case Outcome::kCompleted: return "completed";
    case Outcome::kInterrupted: return "interrupted";
    case Outcome::kFailed: return "failed";
    case Outcome::kSkipped: return "skipped";
  }
  return "unknown";
}

const char* toString(Origin origin) {
  return origin == Origin::kSchedule ? "schedule" : "manual";
}

}  // namespace seq
