// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// The one owner of zone commands once Phase 4 is in. Schedule runs and manual
// starts are both entries in one queue; the sequencer starts them one at a time:
//
//   start(A) ... A's duration elapses ... stop(A), pad, start(B) ...
//
// It always sends an explicit stop before the next start, so it never depends
// on the Pro-C replacing one zone with another (which it does; see
// hardware-test-phase1.md step 6). Each zone is still started for a bounded
// duration, so if the ESP dies mid-zone the Pro-C closes the valve on time.
//
// Pure: time comes in through tick(), zone commands go out through ZonePort.
// Not thread-safe; the device serialises access (src/sequencer_service.cpp).
namespace seq {

enum class Origin : uint8_t { kSchedule, kManual };

enum class Outcome : uint8_t {
  kPending,      // queued, not started
  kRunning,      // started, duration not yet elapsed
  kCompleted,    // ran its full duration
  kInterrupted,  // cancelled, or never started because the run was cancelled
  kFailed,       // the bus would not take the start command
  kSkipped,      // skipped by request; the run went on to the next zone
};

struct Entry {
  int zone;
  int durationMinutes;
  Origin origin;
  Outcome outcome = Outcome::kPending;
  uint32_t startedEpoch = 0;  // wall clock at start, 0 if unknown
};

// Sends zone commands. Returns false if the command could not be queued for
// the bus; the sequencer retries.
class ZonePort {
 public:
  virtual ~ZonePort() = default;
  virtual bool start(int zone, int durationMinutes) = 0;
  virtual bool stop(int zone) = 0;
};

struct Timing {
  uint32_t padMs = 5000;    // between stop(A) and start(B)
  uint32_t retryMs = 1000;  // between attempts when the bus is busy
  uint32_t maxAttempts = 5;
};

// Cap on entries in one run, completed ones included. A full schedule is at
// most 48 entries (one per zone); the rest leaves room for manual additions.
constexpr std::size_t kMaxRunEntries = 64;

class Sequencer {
 public:
  enum class State : uint8_t { kIdle, kStarting, kRunning, kPadding };

  explicit Sequencer(ZonePort& port, Timing timing = Timing());

  // Appends to the queue. Starts on the next tick if idle. False if the run is
  // full. Bounds are not checked here; callers validate first.
  bool add(int zone, int durationMinutes, Origin origin);

  // A batch, all or nothing: false (and nothing added) if it does not fit.
  // scheduleName, if set, is reported in status (first name wins when a run
  // already in progress gets more entries). origin applies to every entry.
  bool addRun(const std::string& scheduleName, const std::vector<Entry>& entries,
              Origin origin = Origin::kSchedule);

  // Stops the running zone, marks it and everything queued interrupted, and
  // goes idle. Never refused; a stop the bus will not take is retried.
  void cancel(uint32_t nowMs);

  // Skips the zone this run is on: stops it if it is running, marks it
  // skipped, and moves on to the next one after the usual pad. During the pad
  // between zones it skips the zone about to start. False if nothing is
  // running or queued.
  bool skip(uint32_t nowMs);

  // Stops one zone. If it is the zone this run is on, that is cancel();
  // otherwise just a stop, for a zone started at the Pro-C or before a reboot.
  void stopZone(int zone, uint32_t nowMs);

  // Advances the state machine. nowEpoch is 0 while the clock is invalid.
  void tick(uint32_t nowMs, uint32_t nowEpoch);

  State state() const { return state_; }
  // Nothing running, nothing queued, no stop being retried. Entries added
  // since the last tick count as queued.
  bool idle() const {
    return state_ == State::kIdle && !pendingStop_ &&
           (runOver_ || index_ >= entries_.size());
  }

  // The run in progress (or the last one, once idle), oldest entry first.
  const std::vector<Entry>& entries() const { return entries_; }
  std::optional<std::size_t> currentIndex() const;
  const std::string& runName() const { return runName_; }

  // Milliseconds left in the running zone, 0 if none.
  uint32_t currentRemainingMs(uint32_t nowMs) const;

  // Bumped on every change worth persisting (start, stop, add, cancel).
  uint32_t revision() const { return revision_; }

  // True while entries_ is a run still in progress (not finished/cancelled).
  bool runInProgress() const { return !runOver_; }

  // Reboot recovery: reinstates the unfinished part of an interrupted run.
  // entries[index] is the zone that was running or about to start. If the
  // Pro-C is still running it (runningRemainingMs > 0), the sequencer waits
  // that long and then stops it as usual; it does not restart it. Otherwise
  // it pads and starts entries[index]. Only valid while idle.
  void resume(const std::string& runName, std::vector<Entry> entries,
              std::size_t index, uint32_t runningRemainingMs, uint32_t nowMs);

  // Reboot recovery, skip case: shows the interrupted run in status without
  // running any of it.
  void showInterrupted(const std::string& runName, std::vector<Entry> entries);

 private:
  void beginRunIfIdle();
  void startCurrent(uint32_t nowMs, uint32_t nowEpoch);
  void finishCurrent(uint32_t nowMs);
  void advance(uint32_t nowMs);
  void interruptRemaining();
  void retryPendingStop(uint32_t nowMs);

  ZonePort& port_;
  Timing timing_;
  State state_ = State::kIdle;
  std::vector<Entry> entries_;
  std::string runName_;
  bool runOver_ = true;  // entries_ holds a finished run
  std::size_t index_ = 0;
  uint32_t phaseStartMs_ = 0;  // when the current state was entered
  uint32_t attempts_ = 0;
  uint32_t lastAttemptMs_ = 0;
  std::optional<int> pendingStop_;
  uint32_t pendingStopAttempts_ = 0;
  uint32_t pendingStopLastMs_ = 0;
  uint32_t revision_ = 0;
};

const char* toString(Sequencer::State state);
const char* toString(Outcome outcome);
const char* toString(Origin origin);

}  // namespace seq
