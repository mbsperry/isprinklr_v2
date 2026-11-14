# Migration plan

Phases ship independently. The v1 system keeps watering throughout: `isprinklr_api`
stays authoritative until Phase 3 lands and is verified on real hardware.

Phase 0 is the only phase requiring physical access to the device.

---

## Phase 0 — OTA repartition

One USB trip. Do this **before** schedules live in NVS: the repartition erases
NVS, and right now there is nothing in it worth keeping (WiFi credentials come
from build flags, not NVS).

- Custom partition table: ~20 KB NVS / 8 KB otadata / 4 MB app0 / 4 MB app1 /
  ~7 MB LittleFS. The v1 board used `huge_app.csv` — a single app slot, so OTA was
  impossible. Set via `board_build.partitions` in `platformio.ini`, which overrides
  whatever the board JSON declares.
- Firmware upload endpoint on the existing async web server, using `Update.h`.
  **Not `espota`** — that is the ArduinoOTA protocol (UDP 3232 + mDNS) whose
  behaviour over Ethernet rather than WiFi is unverified on this board. An HTTP
  POST rides the path already known to work.
- Pin the platform. Try `espressif32@7.1.3` (ships Arduino-ESP32 3.3.12; v1's
  `ETH.begin(ETH_PHY_W5500, ...)` is already 3.x API). Fall back to a pinned
  pioarduino release tag if anything snags. Either way, pin it — v1 pointed at a
  floating `stable` zip URL.
- Vendor the Waveshare ESP32-S3-ETH board definition in `firmware/boards/`.
  Upstream still has no definition for this board, in either the official platform
  or pioarduino.

**Bench verification before reinstalling.** The point of this trip is to never
need USB again, so prove it on the desk:

1. New partition table boots; Ethernet comes up; status endpoint responds.
2. A firmware upload over HTTP succeeds and the new build boots.
3. Do that twice, exercising both app slots (app0 → app1 → app0).
4. Write an NVS key, power-cycle, read it back.

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

**This is the cutover.** Once verified, the ESP is authoritative.

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
