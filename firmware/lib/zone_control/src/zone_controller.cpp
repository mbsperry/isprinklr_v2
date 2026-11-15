#include "zone_controller.h"

#include "hunter_limits.h"

namespace {

constexpr uint32_t kMsPerMinute = 60u * 1000u;

uint32_t runMs(const ZoneController::Run& run) {
  return static_cast<uint32_t>(run.durationMinutes) * kMsPerMinute;
}

}  // namespace

ZoneController::Result ZoneController::start(int zone, int durationMinutes,
                                             uint32_t nowMs) {
  if (!hunter::isValidZone(zone)) return Result::kInvalidZone;
  if (!hunter::isValidRunMinutes(durationMinutes)) {
    return Result::kInvalidDuration;
  }

  const auto frame = hunter::buildStartFrame(zone, durationMinutes);
  if (!frame) return Result::kInvalidDuration;  // unreachable after the checks
  if (!sink_(*frame)) return Result::kBusBusy;

  run_ = Run{zone, durationMinutes, nowMs};
  return Result::kOk;
}

ZoneController::Result ZoneController::stop(std::optional<int> zone,
                                            uint32_t nowMs) {
  int target;
  if (zone) {
    target = *zone;
  } else {
    const auto current = active(nowMs);
    if (!current) return Result::kNoActiveZone;
    target = current->zone;
  }

  if (!hunter::isValidZone(target)) return Result::kInvalidZone;

  const auto frame = hunter::buildStopFrame(target);
  if (!frame) return Result::kInvalidZone;
  if (!sink_(*frame)) return Result::kBusBusy;

  if (run_ && run_->zone == target) run_.reset();
  return Result::kOk;
}

std::optional<ZoneController::Run> ZoneController::active(
    uint32_t nowMs) const {
  if (!run_) return std::nullopt;
  // Unsigned subtraction stays correct across the 49-day millis() wrap.
  if (nowMs - run_->startedMs >= runMs(*run_)) return std::nullopt;
  return run_;
}

uint32_t ZoneController::remainingMs(uint32_t nowMs) const {
  const auto current = active(nowMs);
  if (!current) return 0;
  return runMs(*current) - (nowMs - current->startedMs);
}

const char* toString(ZoneController::Result result) {
  switch (result) {
    case ZoneController::Result::kOk: return "ok";
    case ZoneController::Result::kInvalidZone: return "invalid_zone";
    case ZoneController::Result::kInvalidDuration: return "invalid_duration";
    case ZoneController::Result::kNoActiveZone: return "no_active_zone";
    case ZoneController::Result::kBusBusy: return "bus_busy";
  }
  return "unknown";
}
