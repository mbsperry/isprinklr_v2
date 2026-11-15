// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sequencer_service.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <time.h>

#include <vector>

#include "checkpoint.h"
#include "config.h"
#include "daily_trigger.h"
#include "device_log.h"
#include "hunter_bus.h"
#include "nvs_kv_store.h"
#include "run_log.h"
#include "settings_service.h"
#include "wallclock.h"
#include "zone_api.h"

namespace sequencer_service {
namespace {

constexpr char kNamespace[] = "sched";
constexpr char kKeyLastRun[] = "lastrun";  // local day + 1; 0 = never
constexpr char kKeyCheckpoint[] = "ckpt";
constexpr uint32_t kTriggerCheckMs = 1000;

SemaphoreHandle_t g_mutex = nullptr;
ZoneController g_zones(hunter_bus::enqueue);

// Adapts ZoneController to the sequencer's port. Runs under the mutex.
class Port : public seq::ZonePort {
 public:
  bool start(int zone, int minutes) override {
    return g_zones.start(zone, minutes, millis()) == ZoneController::Result::kOk;
  }
  bool stop(int zone) override {
    return g_zones.stop(zone, millis()) == ZoneController::Result::kOk;
  }
};

Port g_port;
seq::Sequencer g_sequencer(g_port);

// Sends the sequencer's events to the log. Called under the sequencer lock;
// device_log::write only takes the log's own short lock and never waits on
// the network.
class RunLogger : public seq::EventListener {
 public:
  void onEvent(const seq::Event& e) override {
    const run_log::Line line =
        run_log::describe(e, g_sequencer.runName(), g_sequencer.entries());
    device_log::write(line.level, "run", line.message.c_str());
  }
};

RunLogger g_runLogger;
run_log::TriggerReporter g_triggerReporter;

long g_lastRunDay = -1;
uint32_t g_lastTriggerCheckMs = 0;
trigger::Decision g_lastDecision = trigger::Decision::kTimeInvalid;

// Reboot recovery: the checkpoint found at boot, applied once time is valid.
bool g_recoveryPending = false;
recovery::Checkpoint g_bootCheckpoint;
const char* g_recoveryResult = "none";
uint32_t g_recoveryLateSeconds = 0;

// Checkpoint persistence: written from the loop task when the run changes.
uint32_t g_savedRevision = 0;
std::string g_savedCheckpoint = "{}";

void loadCheckpoint() {
  std::string text;
  if (!nvsStore().getBlob(kNamespace, kKeyCheckpoint, &text)) return;
  g_savedCheckpoint = text;
  recovery::Checkpoint c;
  if (!recovery::parse(text, &c)) {
    LOGW("sched", "checkpoint unreadable; ignoring it");
    g_recoveryResult = "checkpoint_unreadable";
    return;
  }
  if (!c.inProgress) return;
  g_bootCheckpoint = std::move(c);
  g_recoveryPending = true;
  g_recoveryResult = "waiting_for_time";
  LOGI("sched", "interrupted run '%s' found; recovering once time is valid",
                g_bootCheckpoint.runName.c_str());
}

// Runs under the sequencer lock.
void saveCheckpointIfChanged(const seq::Sequencer& s, uint32_t epoch) {
  if (s.revision() == g_savedRevision) return;
  g_savedRevision = s.revision();
  const std::string text = recovery::serialize(recovery::capture(s, epoch));
  if (text == g_savedCheckpoint) return;
  if (nvsStore().putBlob(kNamespace, kKeyCheckpoint, text)) {
    g_savedCheckpoint = text;
  } else {
    LOGW("sched", "could not persist checkpoint");
  }
}

// Runs under the sequencer lock, once the clock is valid.
void applyRecovery(seq::Sequencer& s, uint32_t nowMs, uint32_t epoch) {
  g_recoveryPending = false;
  if (!s.idle()) {
    // Something was queued by hand before the clock synced; that wins.
    g_recoveryResult = "superseded";
    LOGW("sched", "recovery skipped: a new run already started");
    return;
  }
  const recovery::Plan p =
      recovery::plan(g_bootCheckpoint, epoch, config::kRecoveryWindowSeconds);
  g_recoveryResult = recovery::toString(p.action);
  g_recoveryLateSeconds = p.lateSeconds;
  if (p.action == recovery::Plan::Action::kResume) {
    s.resume(g_bootCheckpoint.runName, g_bootCheckpoint.entries,
             g_bootCheckpoint.index, p.runningRemainingMs, nowMs);
    // Keep the countdown in status for the zone the Pro-C is still running.
    const auto current = s.currentIndex();
    if (current && s.state() == seq::Sequencer::State::kRunning) {
      const seq::Entry& e = s.entries()[*current];
      const uint32_t total = static_cast<uint32_t>(e.durationMinutes) * 60000u;
      g_zones.adopt(e.zone, e.durationMinutes,
                    nowMs - (total - s.currentRemainingMs(nowMs)));
    }
  } else if (p.action == recovery::Plan::Action::kSkip) {
    s.showInterrupted(g_bootCheckpoint.runName, g_bootCheckpoint.entries);
  }
  // A skip means zones did not water today; worth more than an info line.
  device_log::log(p.action == recovery::Plan::Action::kSkip
                      ? device_log::Level::kWarn
                      : device_log::Level::kInfo,
                  "sched",
                  "recovery: %s run '%s' (%u s late, running zone %u s left)",
                g_recoveryResult, g_bootCheckpoint.runName.c_str(),
                p.lateSeconds, p.runningRemainingMs / 1000);
}

void loadLastRunDay() {
  uint32_t stored = 0;
  if (nvsStore().getU32(kNamespace, kKeyLastRun, &stored) && stored > 0) {
    g_lastRunDay = static_cast<long>(stored) - 1;
  }
}

void saveLastRunDay(long day) {
  g_lastRunDay = day;
  if (!nvsStore().putU32(kNamespace, kKeyLastRun, static_cast<uint32_t>(day + 1))) {
    LOGW("sched", "could not persist last-run day");
  }
}

// Decides under the settings lock, then queues under the sequencer lock;
// never both at once.
void checkTrigger() {
  timekeeping::LocalTime now;
  const bool timeValid = wallclock::localNow(&now);

  std::string name;
  std::vector<seq::Entry> entries;
  const trigger::Decision decision =
      settings_service::with([&](SettingsStore& store) {
        const settings::Settings& s = store.current();
        const trigger::Decision d =
            trigger::decide(timeValid, now, s, g_lastRunDay);
        for (const run_log::Line& line :
             g_triggerReporter.observe(d, timeValid, now, s)) {
          device_log::write(line.level, "sched", line.message.c_str());
        }
        if (d == trigger::Decision::kDue) {
          name = s.activeSchedule;
          for (const auto& run :
               settings::runsForDate(*settings::findSchedule(s, name), now)) {
            entries.push_back(
                seq::Entry{run.zone, run.durationMinutes, seq::Origin::kSchedule});
          }
        }
        return d;
      });

  g_lastDecision = decision;
  if (decision != trigger::Decision::kDue) return;

  // Recorded first: a reboot from here on must not start today's run again.
  saveLastRunDay(trigger::localDay(now));

  if (entries.empty()) {
    LOGI("sched", "'%s' has nothing to run today", name.c_str());
    return;
  }

  // The sequencer logs the outcome (queued, or refused because the run is
  // full) through RunLogger.
  LOGI("sched", "starting today's scheduled run '%s'", name.c_str());
  with([&](seq::Sequencer& s) { return s.addRun(name, entries); });
}

}  // namespace

seq::Sequencer& sequencer() { return g_sequencer; }
ZoneController& zones() { return g_zones; }

void lock() { xSemaphoreTake(g_mutex, portMAX_DELAY); }
void unlock() { xSemaphoreGive(g_mutex); }

void begin() {
  g_mutex = xSemaphoreCreateMutex();
  g_sequencer.setListener(&g_runLogger);
  loadLastRunDay();
  loadCheckpoint();
  LOGI("sched", "last run day %ld", g_lastRunDay);
}

void tick() {
  const uint32_t nowMs = millis();
  const uint32_t epoch =
      wallclock::timeValid() ? static_cast<uint32_t>(time(nullptr)) : 0;

  with([&](seq::Sequencer& s) {
    if (g_recoveryPending && epoch != 0) applyRecovery(s, nowMs, epoch);
    const uint32_t before = s.revision();
    s.tick(nowMs, epoch);
    if (s.revision() != before) {
      LOGD("sched", "%s", seq::toString(s.state()));
    }
    saveCheckpointIfChanged(s, epoch);
    return 0;
  });

  if (nowMs - g_lastTriggerCheckMs >= kTriggerCheckMs) {
    g_lastTriggerCheckMs = nowMs;
    checkTrigger();
  }
}

bool idle() {
  return with([](seq::Sequencer& s) { return s.idle(); });
}

void describe(JsonObject status) {
  const uint32_t nowMs = millis();
  with([&](seq::Sequencer& s) {
    JsonObject zonesObj = status["zones"].to<JsonObject>();
    zone_api::describe(g_zones, nowMs, zonesObj);
    JsonObject run = status["run"].to<JsonObject>();
    zone_api::describeRun(s, nowMs, run);
    return 0;
  });

  JsonObject trig = status["trigger"].to<JsonObject>();
  trig["last_decision"] = trigger::toString(g_lastDecision);
  trig["window_minutes"] = trigger::kStartWindowMinutes;
  JsonObject rec = status["recovery"].to<JsonObject>();
  rec["result"] = g_recoveryResult;
  rec["window_minutes"] = config::kRecoveryWindowSeconds / 60;
  if (g_recoveryLateSeconds != 0) rec["late_seconds"] = g_recoveryLateSeconds;
  if (g_lastRunDay >= 0) {
    trig["last_run_day"] = g_lastRunDay;
  } else {
    trig["last_run_day"] = nullptr;
  }
}

}  // namespace sequencer_service
