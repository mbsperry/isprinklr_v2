// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#include "wallclock.h"

#include <Arduino.h>
#include <atomic>
#include <esp_sntp.h>
#include <time.h>

#include "config.h"
#include "timekeeping.h"

namespace wallclock {
namespace {

std::atomic<uint32_t> g_syncCount{0};
std::atomic<uint32_t> g_lastSyncMs{0};

// configTzTime and the status reader both use this; it changes only from the
// async web task (config PUT) and is read from that same task.
std::string g_timezone;

timekeeping::SyncState syncState() {
  return timekeeping::SyncState{g_syncCount.load(), g_lastSyncMs.load()};
}

// Runs in the lwIP task on every successful sync, including the hourly ones.
void onSync(struct timeval*) {
  g_lastSyncMs = millis();
  const uint32_t n = ++g_syncCount;
  if (n == 1) {
    const time_t now = time(nullptr);
    Serial.printf("[time] first sync: %s\n",
                  timekeeping::formatLocalIso8601(timekeeping::toLocal(now))
                      .c_str());
  }
}

}  // namespace

void begin(const std::string& timezone) {
  g_timezone = timezone;
  esp_sntp_set_time_sync_notification_cb(onSync);

  // configTzTime keeps the server pointers, so they must be static strings.
  // lwIP moves to the next server when one stops answering.
  configTzTime(g_timezone.c_str(), config::kNtpServers[0],
               config::kNtpServers[1], config::kNtpServers[2]);

  Serial.printf("[time] SNTP started: %s, %s, %s; TZ %s\n",
                config::kNtpServers[0], config::kNtpServers[1],
                config::kNtpServers[2], g_timezone.c_str());
}

void setTimezone(const std::string& timezone) {
  if (timezone == g_timezone) return;
  g_timezone = timezone;
  timekeeping::applyTimezone(g_timezone.c_str());
  Serial.printf("[time] timezone now %s\n", g_timezone.c_str());
}

bool localNow(timekeeping::LocalTime* out) {
  const time_t now = time(nullptr);
  if (!timekeeping::isTimeValid(syncState(), now)) return false;
  *out = timekeeping::toLocal(now);
  return true;
}

bool timeValid() { return timekeeping::isTimeValid(syncState(), time(nullptr)); }

void describe(JsonObject obj) {
  const time_t now = time(nullptr);
  const timekeeping::SyncState sync = syncState();
  const bool valid = timekeeping::isTimeValid(sync, now);

  obj["valid"] = valid;
  obj["timezone"] = g_timezone;
  obj["sync_count"] = sync.syncCount;

  if (sync.syncCount > 0) {
    obj["last_sync_ago_seconds"] = (millis() - sync.lastSyncMs) / 1000;
  } else {
    obj["last_sync_ago_seconds"] = nullptr;
  }

  if (valid) {
    const timekeeping::LocalTime local = timekeeping::toLocal(now);
    obj["utc"] = timekeeping::formatUtcIso8601(now);
    obj["local"] = timekeeping::formatLocalIso8601(local);
    obj["dst"] = local.dst;
  } else {
    obj["utc"] = nullptr;
    obj["local"] = nullptr;
  }

  // Reachability is lwIP's 8-bit history of recent polls per server; non-zero
  // means that server answered at least once in the last eight tries.
  JsonArray servers = obj["servers"].to<JsonArray>();
  for (uint8_t i = 0; i < 3; ++i) {
    JsonObject s = servers.add<JsonObject>();
    s["name"] = config::kNtpServers[i];
    s["reachable"] = esp_sntp_getreachability(i) != 0;
  }
}

}  // namespace wallclock
