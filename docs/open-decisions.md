# Open decisions

Things deliberately left unsettled. Each notes what blocks on it.

---

## 1. Reboot-mid-sequence policy — decided 2026-11-15

**Resume within a 60-minute window** (Matt, 2026-11-15). After a reboot
mid-run, once the clock is valid, the remaining zones run only if the ESP is
back within 60 minutes of when the next zone was due. Otherwise they are
skipped until the next scheduled day. The zone that was running is never
restarted: the Pro-C finishes it on its own, and the sequencer waits out its
remaining time before moving on. Either way the interrupted run shows in
status, entry by entry (`run`, `recovery`).

The window is `config::kRecoveryWindowSeconds`. The logic is
`firmware/lib/sequencer/src/checkpoint.cpp`, host-tested in
`test/test_recovery`. Firmware uploads and `/api/restart` are refused while a
run is in progress, so in practice this covers crashes and power cuts.

## 2. License — decided 2026-11-15

**Decision:** the whole repository is GPL-3.0-or-later (LICENSE, NOTICE, SPDX
headers on every source file).

The Hunter SmartPort driver derives from HunterRoam
([ecodina/hunter-wifi](https://github.com/ecodina/hunter-wifi), GPL-3.0-or-later),
which derives from [seb821/OpenSprinkler-Firmware-Hunter](https://github.com/seb821/OpenSprinkler-Firmware-Hunter)
(GPLv3, with "or any later version" in its source headers). Both LICENSE files
were checked directly. That obliges the firmware to be GPLv3 or later, and
one license for the whole repo is simpler than per-directory ones.
The frontend's v1 MIT license was a template leftover and isn't carried over.

## 3. Log retention and storage format — decided 2026-11-15

**SQLite, 12 months, pruned nightly** (Matt, 2026-11-15). The Node server
stores log lines in one SQLite table (time, level, module, message), indexed
for the level, module and time-range filters the frontend needs. A nightly job
deletes rows older than 12 months. This applies only to the server: the ESP
keeps its RAM ring buffer and writes no logs to flash.

Why: filtering becomes a query rather than hand-written line parsing, as v1
needed with its 1 MB rotating file, and a year of history covers the full
watering season required before v1 is decommissioned. Volume is tiny (about
50 MB a year even at 1,000 lines a day), so neither size nor flash wear
decides it. If the server ends up on an SD card, batch inserts.

## 4. Frontend: how much to keep — decided 2026-11-16

**Redone, not carried over** (built in Phase 6, PR 17). v1's frontend was
~1,000 lines across 15 files on React Query + React Bootstrap. v2's `web/` is
a new React + TypeScript app on Vite, mobile-first, with no UI library. v1's
`use_v2_api` branch was used only as a reference for endpoint shapes. The
screens follow Matt's brief: a friendly home page with a plain "connected"
line rather than controller stats, since the household uses it too; running
one zone as the main action; the scheduler on/off switch kept off the home
page and behind a confirm; and alerts that are a heading and buttons only.

## 5. History preservation — settled in practice

**Not preserved.** v2 was written fresh rather than copied or subtree-merged
from v1, so there was no history to carry. The v1 repos stay as archives (see
"Decommissioning v1" in `migration-plan.md`).

## 6. Commands during a schedule run — decided 2026-11-15

Built in Phase 4 as proposed (Matt, 2026-11-15):

- **One queue.** The sequencer owns a single queue (64 entries per run). A
  schedule run fills it, and a manual start goes on the end instead of
  preempting.
- **Cancel.** `POST /api/zone/stop {}` stops the running zone and drops the
  rest, marking them interrupted. It is never refused.
- **Skip** (Matt asked for it). `POST /api/run/skip` stops the running zone,
  marks it skipped, and moves on after the usual pad.
- **Manual starts are not gated.** They ignore the schedule on/off flag and
  `time_valid`.
- **Stop before start.** Between zones the sequencer always sends stop(A),
  waits 5 s, then start(B). The Pro-C would replace A anyway (hardware test
  step 6), but the sequence doesn't rely on it.
- **Checkpoint.** It stores the run's entries with their outcomes, not
  `{schedule, zone_index}`.

## 7. Zone count and settings size — decided 2026-11-16

The firmware accepts zones 1–10 (`hunter::kMaxZone`), not the SmartPort
protocol's 1–48 (Matt, 2026-11-16). The frame builder still covers 1–48
(`kMaxProtocolZone`), so zones 13–48 stay tested at the protocol level.

**Why.** Zones, schedules and config are one JSON document in NVS, capped at
4 KB (`settings_limits.h`). Measured by `test_settings_sizing`, with 32-byte
names, every schedule slot full, and every zone in every schedule:

| Zones | Stored document | `PUT /api/schedules` body |
|---|---|---|
| 10 | 3.7 KB | 6.7 KB |
| 16 | 5.7 KB | 10.4 KB |
| 23 | 8.0 KB | 14.7 KB |
| 48 | 16.3 KB | 30.2 KB |

The installed Pro-C has 10 zones, and at 10 the worst case fits. At 48, only one
full schedule fits. An over-size save is refused cleanly (413, nothing written),
so this was never a corruption risk, only saves that would fail as zones were
added.

The cap can't simply be raised. The NVS partition is 20 KB, about 16 KB usable,
and NVS writes the new blob before erasing the old one, so the document must fit
twice beside the checkpoint and boot counter: about 6 KB at most. Growing the
NVS partition changes the partition table, which needs a USB reflash and wipes
NVS. Splitting the document across NVS keys runs into the same total and loses
all-or-nothing writes.

**The fix if more zones are ever needed: move settings to LittleFS.** The
partition table already has a 7 MB `littlefs` partition that nothing uses, so
this ships by OTA with no repartition. Sketch:

- Store the same compact document as a file. Write `settings.tmp`, then rename
  it over `settings.json`; LittleFS renames are atomic and it is built to
  survive power loss, so writes stay all-or-nothing.
- On first boot, if the file is missing and the NVS blob exists, copy it across.
  Keep the NVS copy until the file has loaded cleanly once.
- Raise `kMaxZone`, `kMaxDocumentBytes`, and the 16 KB PUT body limit in
  `web.cpp` (about 32 KB for 48 zones). Keep all access under the settings mutex.
- Costs: a few KB of RAM for the mounted filesystem, about 30 KB peak for the
  largest request and response, a migration path, and a hardware test.

A binary encoding in NVS (about 3.5 KB at 48 zones × 10 schedules) was the other
option; it was rejected because a hand-written codec is harder to inspect and the
HTTP limit would still need raising.

---

## Settled

Recorded so they do not get relitigated.

| Decision | Outcome |
|---|---|
| Where scheduling lives | ESP32, authoritative |
| Schedule storage | NVS on the device |
| Time source | SNTP; no battery-backed RTC |
| Log transport | UDP, fire-and-forget; never HTTP |
| Log diagnostics on-device | RAM ring buffer, no flash |
| Auth / TLS | None in the app; remote access is a Cloudflare tunnel behind Cloudflare Access |
| Browser to controller | Same-origin: the Node server forwards `/esp/` to the ESP; ESP refuses non-GET from non-LAN origins |
| Python API | Replaced by a Node service |
| nginx | Dropped; Node serves the frontend |
| Repo structure | Monorepo: firmware, web, server, docs |
| Frontend serving | Server-side, not from the ESP |
| Frontend | Rebuilt on Vite + React + TypeScript, mobile-first (decision 4) |
| Server host | Small Linux container, systemd service; site settings in an env file outside the repo |
| Build settings | Router NTP address and Wi-Fi credentials in gitignored `firmware/secrets.ini` |
| Build system | PlatformIO, platform version pinned |
| Firmware updates | OTA over HTTP; `espota` rejected |
| Editor / IDE | CLI-driven; no VS Code requirement |
| Hardware count | One board; ESP stays installed in the basement |
| OTA failure recovery | App-level boot guard, not IDF rollback |
| Phase 0 / v1 compatibility | Not required — system is winterized |
| Platform | pioarduino `55.03.311` (Arduino-ESP32 3.3.11); official registry 7.1.3 is Arduino 2.x |
| Board memory type | `qio_opi` — confirmed on hardware, 8 MB PSRAM visible |
| Device address | Static reservation on the router; kept out of the repo |
| Reboot mid-run | Resume within 60 minutes of when the next zone was due, else skip (decision 1) |
| Server log storage | SQLite, 12 months, nightly prune (decision 3) |
| Commands during a run | One queue; manual appends; cancel and skip; manual not gated (decision 6) |
| `EO` day pattern | Strict alternation by days since 1970-01-01; not v1's day-of-year parity (2026-11-15) |
| Settings storage | One compact NVS document, 4 KB cap, all-or-nothing writes, FNV-1a `settings_hash` |
| Zone count | 1–10; LittleFS is the path to more (decision 7) |
| Deploy path | `POST /api/update` over the LAN; USB only for recovery |
