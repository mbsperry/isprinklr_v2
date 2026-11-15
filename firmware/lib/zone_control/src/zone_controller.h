#pragma once

#include <cstdint>
#include <functional>
#include <optional>

#include "hunter_frame.h"

// Zone command logic, independent of HTTP and of the bus. Validates a request,
// builds the frame, hands it to a sink, and remembers what it last commanded.
//
// The SmartPort line is write-only: the ESP cannot ask the Pro-C what it is
// doing. "Active" here means "the last run this controller commanded and whose
// duration has not yet elapsed", nothing more. A zone started at the Pro-C's own
// dial, or one that outlived an ESP reboot, is invisible.
//
// Not thread-safe. Today only the async web task calls it. The Phase 4 scheduler
// will be a second caller and must add a mutex or route through one task.
class ZoneController {
 public:
  // Accepts a frame for transmission. Returns false if it could not be queued
  // (bus busy); the controller then leaves its state unchanged.
  using FrameSink = std::function<bool(const hunter::Frame&)>;

  enum class Result {
    kOk,
    kInvalidZone,
    kInvalidDuration,
    kNoActiveZone,  // stop with no zone given and nothing commanded
    kBusBusy,
  };

  struct Run {
    int zone;
    int durationMinutes;
    uint32_t startedMs;
  };

  explicit ZoneController(FrameSink sink) : sink_(std::move(sink)) {}

  // Starting a zone while another is commanded does not stop the first one
  // explicitly; the controller just records the new run. Whether the Pro-C
  // replaces or queues the earlier manual run is not verified (see
  // docs/hardware-test-phase1.md).
  Result start(int zone, int durationMinutes, uint32_t nowMs);

  // Stops `zone`, or the active zone if none is given. A zone is accepted even
  // when this controller has no record of it running, so a run that predates a
  // reboot can still be stopped.
  Result stop(std::optional<int> zone, uint32_t nowMs);

  std::optional<Run> active(uint32_t nowMs) const;

  // Milliseconds left in the active run, or 0.
  uint32_t remainingMs(uint32_t nowMs) const;

 private:
  FrameSink sink_;
  std::optional<Run> run_;
};

const char* toString(ZoneController::Result result);
