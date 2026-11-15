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

## 3. Log retention and storage format

**Blocks:** Phase 5/6 server work.

How long to keep logs, and in what form. v1 used a 1 MB rotating file with
line-based filtering by module, level, and date range. Flat files are simple and
greppable; SQLite makes filtering cheaper and the query code smaller. Volume is
tiny either way.

## 4. Frontend: how much to keep

**Blocks:** Phase 6.

v1's frontend is ~1,000 lines across 15 files, on React Query + React Bootstrap.
The `use_v2_api` branch matches the current API shape and is a reasonable
reference for endpoint contracts. Open question is whether component structure
and styling carry over or get redone.

## 5. History preservation

**Blocks:** nothing; cheaper to decide before the first real commit.

Copying v1 files in loses their `git log`. `git subtree add --prefix=<dir>`
preserves it at the cost of a messier history. For a declared rewrite, copying is
probably fine, with the v1 repos kept as archives.

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
| Auth / TLS | None; router VPN is the boundary |
| Python API | Replaced by a Node service |
| nginx | Dropped; Node serves the frontend |
| Repo structure | Monorepo: firmware, web, server, docs |
| Frontend serving | Server-side, not from the ESP |
| Build system | PlatformIO, platform version pinned |
| Firmware updates | OTA over HTTP; `espota` rejected |
| Editor / IDE | CLI-driven; no VS Code requirement |
| Hardware count | One board; ESP stays installed in the basement |
| OTA failure recovery | App-level boot guard, not IDF rollback |
| Phase 0 / v1 compatibility | Not required — system is winterized |
| Platform | pioarduino `55.03.311` (Arduino-ESP32 3.3.11); official registry 7.1.3 is Arduino 2.x |
| Board memory type | `qio_opi` — confirmed on hardware, 8 MB PSRAM visible |
| Device address | 192.168.88.54, static reservation on the router |
| Reboot mid-run | Resume within 60 minutes of when the next zone was due, else skip (decision 1) |
| Commands during a run | One queue; manual appends; cancel and skip; manual not gated (decision 6) |
| `EO` day pattern | Strict alternation by days since 1970-01-01; not v1's day-of-year parity (2026-11-15) |
| Settings storage | One compact NVS document, 4 KB cap, all-or-nothing writes, FNV-1a `settings_hash` |
| Deploy path | `POST /api/update` over the LAN; USB only for recovery |
