// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sequencer.h"

namespace seq {
namespace {
constexpr uint32_t kMsPerMinute = 60u * 1000u;
}  // namespace

Sequencer::Sequencer(ZonePort& port, Timing timing)
    : port_(port), timing_(timing) {}

void Sequencer::emit(const Event& e) const {
  if (listener_ != nullptr) listener_->onEvent(e);
}

Event Sequencer::zoneEvent(Event::Kind kind) const {
  Event e;
  e.kind = kind;
  e.entry = entries_[index_];
  e.index = index_;
  e.total = entries_.size();
  e.attempts = attempts_;
  return e;
}

std::size_t Sequencer::unfinished() const {
  if (runOver_) return 0;
  std::size_t n = 0;
  for (const Entry& e : entries_) {
    if (e.outcome == Outcome::kPending || e.outcome == Outcome::kRunning) ++n;
  }
  return n;
}

void Sequencer::endRun(Event::RunEnd how, std::size_t dropped) {
  Event e;
  e.kind = Event::Kind::kRunEnded;
  e.total = entries_.size();
  e.runEnd = how;
  e.dropped = dropped;
  emit(e);
}

void Sequencer::beginRunIfIdle() {
  if (!runOver_) return;
  entries_.clear();
  runName_.clear();
  index_ = 0;
  runOver_ = false;
}

bool Sequencer::add(int zone, int durationMinutes, Origin origin) {
  const std::size_t ahead = unfinished();
  beginRunIfIdle();
  Event e;
  e.entry = Entry{zone, durationMinutes, origin};
  e.count = 1;
  if (entries_.size() >= kMaxRunEntries) {
    e.kind = Event::Kind::kQueueFull;
    emit(e);
    return false;
  }
  entries_.push_back(Entry{zone, durationMinutes, origin});
  ++revision_;
  e.kind = Event::Kind::kQueued;
  e.ahead = ahead;
  e.total = entries_.size();
  emit(e);
  return true;
}

bool Sequencer::addRun(const std::string& scheduleName,
                       const std::vector<Entry>& entries, Origin origin) {
  const std::size_t ahead = unfinished();
  beginRunIfIdle();
  Event ev;
  if (!entries.empty()) ev.entry = entries.front();  // named when it is one
  ev.entry.origin = origin;
  ev.count = entries.size();
  if (entries_.size() + entries.size() > kMaxRunEntries) {
    ev.kind = Event::Kind::kQueueFull;
    emit(ev);
    return false;
  }
  if (runName_.empty()) runName_ = scheduleName;
  for (Entry e : entries) {
    e.origin = origin;
    e.outcome = Outcome::kPending;
    e.startedEpoch = 0;
    entries_.push_back(e);
  }
  ++revision_;
  ev.kind = Event::Kind::kQueued;
  ev.ahead = ahead;
  ev.total = entries_.size();
  emit(ev);
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
  const bool wasRunning = state_ == State::kRunning;
  const bool hadRun = !runOver_;
  std::size_t dropped = 0;
  for (const Entry& e : entries_) {
    if (e.outcome == Outcome::kPending) ++dropped;
  }
  if (wasRunning) {
    // Reported before the outcome changes below; ranMs says how far it got.
    Event e = zoneEvent(Event::Kind::kZoneEnded);
    e.entry.outcome = Outcome::kInterrupted;
    e.ranMs = nowMs - phaseStartMs_;
    emit(e);
  }
  // In kStarting the entry is still pending, so it counts as never started.
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
  if (hadRun) endRun(Event::RunEnd::kCancelled, dropped);
}

bool Sequencer::skip(uint32_t nowMs) {
  if (runOver_ || index_ >= entries_.size()) return false;
  Entry& e = entries_[index_];
  Event ev = zoneEvent(Event::Kind::kZoneEnded);
  ev.entry.outcome = Outcome::kSkipped;
  if (state_ == State::kRunning) ev.ranMs = nowMs - phaseStartMs_;
  emit(ev);
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
  if (port_.stop(*pendingStop_)) {
    pendingStop_.reset();
  } else if (++pendingStopAttempts_ >= timing_.maxAttempts) {
    // Giving up is safe: every start carried a bounded duration.
    Event e;
    e.kind = Event::Kind::kStopAbandoned;
    e.entry.zone = *pendingStop_;
    e.attempts = pendingStopAttempts_;
    emit(e);
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
    emit(zoneEvent(Event::Kind::kZoneStarted));
    return;
  }

  if (attempts_ >= timing_.maxAttempts) {
    // The bus is not taking commands. Abandon the run rather than run zones
    // out of order or hours late.
    e.outcome = Outcome::kFailed;
    emit(zoneEvent(Event::Kind::kZoneEnded));
    std::size_t dropped = 0;
    for (Entry& rest : entries_) {
      if (rest.outcome == Outcome::kPending) {
        rest.outcome = Outcome::kInterrupted;
        ++dropped;
      }
    }
    state_ = State::kIdle;
    runOver_ = true;
    ++revision_;
    endRun(Event::RunEnd::kFailed, dropped);
    return;
  }
  emit(zoneEvent(Event::Kind::kStartRetry));
}

void Sequencer::finishCurrent(uint32_t nowMs) {
  Entry& e = entries_[index_];
  e.outcome = Outcome::kCompleted;
  {
    Event ev = zoneEvent(Event::Kind::kZoneEnded);
    ev.ranMs = nowMs - phaseStartMs_;
    emit(ev);
  }
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
    endRun(Event::RunEnd::kFinished, 0);
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
