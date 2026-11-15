// SPDX-FileCopyrightText: 2026 Matt Sperry
// SPDX-License-Identifier: GPL-3.0-or-later

#include "device_log.h"

#include <Arduino.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <lwip/sockets.h>
#include <time.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>

#include "log_ring.h"
#include "net.h"
#include "settings.h"
#include "syslog_format.h"
#include "wallclock.h"

namespace device_log {
namespace {

constexpr std::size_t kRingLines = 200;
constexpr char kHostname[] = "isprinklr";
// Lines per pass under the lock; the task loops until it has caught up.
constexpr std::size_t kExportBatch = 16;
// Lowest application priority: it runs only when nothing else wants the CPU.
constexpr UBaseType_t kExportPriority = 1;
constexpr uint32_t kExportStackBytes = 6144;
constexpr uint32_t kExportIdleWaitMs = 1000;

SemaphoreHandle_t g_mutex = nullptr;
logging::LogRing g_ring(kRingLines);
uint32_t g_bootId = 0;
TaskHandle_t g_exportTask = nullptr;

// Guarded by g_mutex.
uint32_t g_sinkAddr = 0;  // network byte order; 0 = no sink
uint16_t g_sinkPort = 0;
std::string g_sinkText;
uint32_t g_exportedSeq = 0;  // last seq handed to sendto()
uint32_t g_sent = 0;
uint32_t g_sendErrors = 0;
uint32_t g_lost = 0;  // overwritten in the ring before they could be sent

struct Lock {
  Lock() {
    if (g_mutex != nullptr) xSemaphoreTake(g_mutex, portMAX_DELAY);
  }
  ~Lock() {
    if (g_mutex != nullptr) xSemaphoreGive(g_mutex);
  }
};

uint32_t epochNow() {
  return wallclock::timeValid() ? static_cast<uint32_t>(time(nullptr)) : 0;
}

void exportTask(void*) {
  int sock = -1;
  std::vector<logging::LogEntry> batch;
  batch.reserve(kExportBatch);

  for (;;) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kExportIdleWaitMs));
    if (!net::isUp()) continue;

    if (sock < 0) {
      sock = lwip_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
      if (sock < 0) continue;
      const int flags = lwip_fcntl(sock, F_GETFL, 0);
      lwip_fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    }

    for (;;) {
      sockaddr_in to = {};
      {
        Lock lock;
        if (g_sinkAddr == 0) break;
        to.sin_family = AF_INET;
        to.sin_port = htons(g_sinkPort);
        to.sin_addr.s_addr = g_sinkAddr;
        g_lost += g_ring.since(g_exportedSeq, Level::kDebug, kExportBatch, &batch);
        if (batch.empty()) break;
        g_exportedSeq = batch.back().seq;
      }

      uint32_t sent = 0, errors = 0;
      for (const logging::LogEntry& e : batch) {
        const std::string line = logging::formatSyslog(e, kHostname, g_bootId);
        const int n = lwip_sendto(sock, line.data(), line.size(), MSG_DONTWAIT,
                                  reinterpret_cast<const sockaddr*>(&to),
                                  sizeof(to));
        // No retry, by design. The line is still in the ring and /api/logs.
        if (n < 0) {
          ++errors;
        } else {
          ++sent;
        }
      }
      Lock lock;
      g_sent += sent;
      g_sendErrors += errors;
    }
  }
}

}  // namespace

void begin() {
  if (g_mutex == nullptr) g_mutex = xSemaphoreCreateMutex();
  g_bootId = esp_random();
}

void startExport() {
  if (g_exportTask != nullptr) return;
  if (xTaskCreate(exportTask, "log_export", kExportStackBytes, nullptr,
                  kExportPriority, &g_exportTask) != pdPASS) {
    g_exportTask = nullptr;
    write(Level::kError, "log", "export task not created; UDP export is off");
  }
}

void write(Level level, const char* module, const char* message) {
  const uint32_t uptime = millis();
  const uint32_t epoch = epochNow();
  {
    Lock lock;
    g_ring.push(level, module, message, uptime, epoch);
  }
  const char* tag = level == Level::kError  ? "E: "
                    : level == Level::kWarn ? "W: "
                                            : "";
  Serial.printf("[%s] %s%s\n", module, tag, message);
  if (g_exportTask != nullptr) xTaskNotifyGive(g_exportTask);
}

void log(Level level, const char* module, const char* format, ...) {
  char buf[logging::kMessageBytes + 32];
  va_list args;
  va_start(args, format);
  vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);
  write(level, module, buf);
}

void setSink(const std::string& host, int port) {
  uint8_t ip[4] = {};
  uint32_t addr = 0;
  if (!host.empty() && settings::parseIpv4(host, ip)) {
    addr = static_cast<uint32_t>(ip[0]) | static_cast<uint32_t>(ip[1]) << 8 |
           static_cast<uint32_t>(ip[2]) << 16 | static_cast<uint32_t>(ip[3]) << 24;
  }
  {
    Lock lock;
    // A newly set sink gets what the ring still holds; anything overwritten
    // while there was no sink is not counted as lost.
    if (g_sinkAddr == 0 && g_exportedSeq + 1 < g_ring.oldestSeq()) {
      g_exportedSeq = g_ring.oldestSeq() - 1;
    }
    g_sinkAddr = addr;
    g_sinkPort = static_cast<uint16_t>(port);
    g_sinkText = addr != 0 ? host + ":" + std::to_string(port) : "";
  }
  if (addr != 0) {
    log(Level::kInfo, "log", "UDP export to %s:%d", host.c_str(), port);
  } else {
    write(Level::kInfo, "log", "UDP export off (no log_host set)");
  }
}

logs_api::Snapshot snapshot(const logs_api::Query& q) {
  Lock lock;
  return logs_api::take(g_ring, q);
}

void describe(JsonObject out) {
  Lock lock;
  out["capacity"] = g_ring.capacity();
  out["last_seq"] = g_ring.lastSeq();
  if (g_sinkAddr != 0) {
    out["sink"] = g_sinkText;
  } else {
    out["sink"] = nullptr;
  }
  out["sent"] = g_sent;
  out["send_errors"] = g_sendErrors;
  out["lost"] = g_lost;
  char boot[9];
  snprintf(boot, sizeof(boot), "%08x", static_cast<unsigned>(g_bootId));
  out["boot_id"] = boot;
}

}  // namespace device_log
