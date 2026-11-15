#include "hunter_bus.h"

#include <Arduino.h>
#include <driver/gpio.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "hunter_pulses.h"

namespace hunter_bus {
namespace {

constexpr UBaseType_t kQueueDepth = 4;
constexpr uint32_t kTaskStackBytes = 4096;

// Above the Arduino loop task (1) so loop() cannot stretch a pulse. Pinned to the
// app core, away from the network stack on core 0.
constexpr UBaseType_t kTaskPriority = 5;
constexpr BaseType_t kTaskCore = 1;

// Pulses at least this long are timed with delay(), which yields. Shorter ones
// busy-wait. Only the reset pulse pair is long enough to yield.
constexpr uint32_t kYieldThresholdUs = 10000;

int g_pin = -1;
QueueHandle_t g_queue = nullptr;
portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;

std::atomic<uint32_t> g_sent{0};
std::atomic<uint32_t> g_dropped{0};
std::atomic<bool> g_busy{false};
std::atomic<uint32_t> g_lastSentMs{0};
std::atomic<uint32_t> g_lastDurationUs{0};

// Plays the pulse train. Each data bit (a HIGH then a LOW, ~2.1 ms) runs with
// interrupts off on this core only, so nothing scheduled on this core can
// stretch a 208 us pulse into something that decodes as the other bit value.
// The other core keeps running. Between bits interrupts are re-enabled. The
// interrupt watchdog (300 ms) is far above 2.1 ms.
void play(const hunter::PulseTrain& train) {
  std::size_t i = 0;
  while (i < train.count) {
    const hunter::Pulse& p = train.pulses[i];

    if (p.durationUs >= kYieldThresholdUs) {
      digitalWrite(g_pin, p.high ? HIGH : LOW);
      delay(p.durationUs / 1000);
      ++i;
      continue;
    }

    // A short pulse and the one after it form one bit (or the start pulse and
    // its gap). Send them as a unit, interrupts off on this core only.
    portENTER_CRITICAL(&g_mux);
    digitalWrite(g_pin, p.high ? HIGH : LOW);
    delayMicroseconds(p.durationUs);
    if (i + 1 < train.count && train.pulses[i + 1].durationUs < kYieldThresholdUs) {
      const hunter::Pulse& q = train.pulses[i + 1];
      digitalWrite(g_pin, q.high ? HIGH : LOW);
      delayMicroseconds(q.durationUs);
      ++i;
    }
    portEXIT_CRITICAL(&g_mux);
    ++i;
  }

  digitalWrite(g_pin, LOW);  // idle
}

void busTask(void*) {
  // Static, not on the task stack: the pulse train alone is about 2 KB.
  static hunter::Frame frame;
  static hunter::PulseTrain train;

  for (;;) {
    if (xQueueReceive(g_queue, &frame, portMAX_DELAY) != pdTRUE) continue;

    g_busy = true;
    hunter::encodePulses(frame, &train);

    const int64_t start = esp_timer_get_time();
    play(train);
    const int64_t elapsed = esp_timer_get_time() - start;

    g_lastDurationUs = static_cast<uint32_t>(elapsed);
    g_lastSentMs = millis();
    ++g_sent;
    g_busy = false;

    Serial.printf("[hunter] frame sent, %u bytes, %lld us (nominal %u us)\n",
                  static_cast<unsigned>(frame.length), elapsed,
                  static_cast<unsigned>(train.totalUs()));
  }
}

}  // namespace

void begin(int pin) {
  if (g_queue != nullptr) return;
  g_pin = pin;

  // A HIGH on REM is the start of a reset pulse, so the line must never float
  // or glitch HIGH. Pull it down first, latch LOW in the output register via
  // IDF, then enable the output driver. Not digitalWrite() before pinMode():
  // in Arduino 3.x that may not set the level on a pin with no output config.
  // This covers only the moment setup() reaches here; from reset until then
  // the pin is in its hardware default state, which firmware cannot change.
  const gpio_num_t gpio = static_cast<gpio_num_t>(g_pin);
  gpio_set_pull_mode(gpio, GPIO_PULLDOWN_ONLY);
  gpio_set_level(gpio, 0);
  pinMode(g_pin, OUTPUT);
  digitalWrite(g_pin, LOW);

  g_queue = xQueueCreate(kQueueDepth, sizeof(hunter::Frame));
  if (g_queue == nullptr) {
    Serial.println("[hunter] queue allocation failed; bus disabled");
    return;
  }

  if (xTaskCreatePinnedToCore(busTask, "hunter_bus", kTaskStackBytes, nullptr,
                              kTaskPriority, nullptr, kTaskCore) != pdPASS) {
    Serial.println("[hunter] task creation failed; bus disabled");
    vQueueDelete(g_queue);
    g_queue = nullptr;
    return;
  }

  Serial.printf("[hunter] bus ready on GPIO %d\n", g_pin);
}

bool enqueue(const hunter::Frame& frame) {
  if (g_queue == nullptr || xQueueSend(g_queue, &frame, 0) != pdTRUE) {
    ++g_dropped;
    return false;
  }
  return true;
}

bool idle() {
  return !g_busy.load() &&
         (g_queue == nullptr || uxQueueMessagesWaiting(g_queue) == 0);
}

void describe(JsonObject obj) {
  obj["pin"] = g_pin;
  obj["ready"] = g_queue != nullptr;
  obj["busy"] = g_busy.load();
  obj["queued"] = g_queue != nullptr ? uxQueueMessagesWaiting(g_queue) : 0;
  obj["frames_sent"] = g_sent.load();
  obj["frames_dropped"] = g_dropped.load();
  const uint32_t last = g_lastSentMs.load();
  if (last != 0) {
    obj["last_sent_ago_ms"] = millis() - last;
    obj["last_frame_us"] = g_lastDurationUs.load();
  }
}

}  // namespace hunter_bus
