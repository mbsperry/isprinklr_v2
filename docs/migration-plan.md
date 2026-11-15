# Migration plan

The system is winterized — shut down and blown out, with no watering for 5–6
months. That removes the constraint this plan was originally built around: there
is no live system to keep running, so phases need not preserve v1 behaviour and
there is no cutover to stage. Phases are ordered by what depends on what, not by
risk to the lawn.

The real deadline is first watering in spring. The real risk is arriving there
with untested firmware.

The ESP32 stays installed, wired to the Pro-C. Phase 0 is the only phase needing
physical access; everything after it ships over OTA. USB access is awkward but
not hard, so an occasional reflash is an acceptable fallback rather than a
disaster.

---

## Phase 0 — OTA repartition

**Complete (2026-11-14).** One USB trip, then OTA verified in both directions and
the boot guard proven on hardware. Device at 192.168.88.54. See
[`phase0-runbook.md`](phase0-runbook.md) for the install record, lessons learned,
and the routine OTA update procedure.

Do this **before** schedules live in NVS: the repartition erases NVS, and right
now there is nothing in it worth keeping (WiFi credentials come from build flags,
not NVS).

Because nothing is watering, Phase 0 firmware does **not** need to stay
API-compatible with v1's Python API. It is a clean v2 build: network, status,
OTA, boot guard. Nothing else.

- Custom partition table: ~20 KB NVS / 8 KB otadata / 4 MB app0 / 4 MB app1 /
  ~7 MB LittleFS. The v1 board used `huge_app.csv` — a single app slot, so OTA was
  impossible. Set via `board_build.partitions` in `platformio.ini`, which overrides
  whatever the board JSON declares.
- Firmware upload endpoint on the existing async web server, using `Update.h`.
  **Not `espota`** — that is the ArduinoOTA protocol (UDP 3232 + mDNS) whose
  behaviour over Ethernet rather than WiFi is unverified on this board. An HTTP
  POST rides the path already known to work.
- Pin the platform to a pioarduino release tag (currently `55.03.311`, Arduino-ESP32
  3.3.11 / ESP-IDF 5.5.5). The official registry `espressif32@7.1.3` was tried and
  ships Arduino-ESP32 2.0.17, where `ETH_PHY_W5500` and `SPI3_HOST` do not exist;
  v1's `ETH.begin(ETH_PHY_W5500, ...)` is 3.x API. Either way, pin it — v1 pointed
  at a floating `stable` zip URL.
- Vendor the Waveshare ESP32-S3-ETH board definition in `firmware/boards/`.
  Upstream still has no definition for this board, in either the official platform
  or pioarduino.

Plus a boot guard: a counter in NVS, cleared once the device is serving on the
network, that reverts to the previous OTA slot after three unhealthy boots. This
covers the one failure OTA cannot — firmware that flashes and boots but is
unreachable. ESP-IDF's own rollback does not help, because its default
`CONFIG_BOOTLOADER_APP_ROLLBACK_CONFIRM_ON_STARTUP` confirms the image before
`app_main` runs, and app-controlled confirmation needs an sdkconfig change the
Arduino framework does not allow.

## Phase 1 — Test harness

Built alongside the migration, not after. The seams it needs are the seams the
migration needs anyway.

- `[env:native]` with Unity. `lib_compat_mode = off` to override a `strict`
  setting inherited from `[env]`.
- Split frame construction from I/O in the Hunter driver: a pure
  `buildStartFrame(zone, minutes)` returning bytes, plus a separate bus write.
  Assert exact frames for zones 1, 12, 13, 48 and the zero-minute stop case.
  v1 welded these together and the constructor called `pinMode()`, so none of it
  was constructible on a host.
- Inject the Hunter controller into the web server instead of owning it by value;
  lift logic out of the route lambdas. v1 had validation, dispatch, and JSON
  formatting inline in handlers, reachable only over HTTP.
- Storage behind an interface, fake-backed in tests.
- CI: host tests on every push, plus a compile of both device environments.

Tier 2 (on-device tests via `pio test`) and Tier 3 (HTTP contract tests) come
later and need no extra hardware — though a spare devkit is convenient, since
on-device tests reboot and commandeer the board.

## Phase 2 — Time

- SNTP via `configTime()`, timezone string for DST.
- `time_valid` state that blocks execution and appears in the status response.
- No hardware RTC. The internal RTC carries short outages; it does not survive a
  power cycle.

## Phase 3 — Schedules in NVS

- NVS namespace for schedules, active schedule name, zone names, on/off, config.
- Schedule read/write endpoints, plus a version counter or hash in status so the
  server can detect drift.
- Port validation and day-pattern matching from v1's `schedule_util.py`:
  `ALL`, `NONE`, `EO`, and colon-joined day lists like `M:W:F`. Host-tested,
  including month-end even/odd boundaries.
- All NVS access funnelled through one task or behind a mutex — async handlers
  and the main loop both touch it, and NVS is not thread-safe.

With no live v1 system to migrate from, this is not a cutover — just the point
at which the device becomes self-sufficient.

## Phase 4 — Scheduling engine

- Daily trigger at a configured hour/minute; a persisted last-run date makes it
  idempotent across reboots.
- Sequencing state machine with inter-zone padding, driven off the async task.
  v1's bus write blocks for roughly 640 ms (a 325 ms + 65 ms reset pulse, then
  120 bits at ~2.08 ms each) and in v1 that ran inside an async HTTP handler.
  Acceptable when the device does nothing else; not acceptable once it is also
  running a schedule.
- Checkpoint `{schedule, zone_index, started_epoch, expected_end}` at each
  transition. On boot: read reset reason → sync time → reconcile.
- Reconciliation policy is an open decision — see `open-decisions.md`.
- Queue semantics (cancel, add during a run) are open decision 6. Design notes
  from the ESPHome comparison: [`esphome-sprinkler-lessons.md`](esphome-sprinkler-lessons.md).

## Phase 5 — Logging

- RAM ring buffer, ~200 lines, exposed over HTTP. No flash writes. Works when the
  network does not.
- UDP syslog export, fire-and-forget.
- Minimal status page from LittleFS as the fallback UI.

## Phase 6 — Server and frontend

- Node service: static hosting for the frontend, UDP log sink, nightly schedule
  backup. Replaces both FastAPI and nginx.
- Frontend rebuilt on Vite. v1 used `react-scripts` 5 (CRA, unmaintained).
  Start from v1's `use_v2_api` branch for endpoint shapes rather than its stale
  `main`, which still calls endpoints that no longer exist.
- Two base URLs in config: the ESP for control, the server for logs.

## Decommissioning v1

Keep `isprinklr_esp`, `isprinklr_api`, and `iSprinklr_react` as archives. Nothing
is deleted until v2 has run a full watering season — or at minimum, several weeks
including a deliberate power-cut test.
