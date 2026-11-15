# Open decisions

Things deliberately left unsettled. Each notes what blocks on it.

---

## 1. Reboot-mid-sequence policy

**Blocks:** Phase 4.

If the ESP reboots partway through a watering cycle, the running zone still closes
(the Pro-C holds the timer), but the remaining zones are pending. What should
happen?

- **Resume** the remaining zones. Risk: after a two-hour outage, watering restarts
  in full sun.
- **Skip** to the next scheduled day. Risk: zones silently get no water.
- **Resume within a window** — resume only if the interruption was shorter than
  some threshold, else skip. More code, needs a threshold.

There is no safe default, which is why it is here rather than decided. A partial
cycle should be visible in logs and status regardless of which is chosen.

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

## 6. Commands during a schedule run

**Blocks:** Phase 4.

Direction from Matt (2026-11-15): a schedule run is a queue. Cancelling a run is
required. Adding a zone to a running queue is wanted if it stays cheap.

Hardware fact (Phase 1 test, 2026-11-15): **the Pro-C does not queue.** A start
for zone B while zone A is running replaces A immediately. So any queue has to
live on the ESP, and a manual start during a run must go through the sequencer,
never straight to the bus, or it silently cuts the running zone short.

Proposed shape, not yet built:

- The sequencer owns one bounded queue of `{zone, duration_minutes, origin}`,
  capped at 48 entries. A schedule run fills it.
- A manual start during a run appends to the end of the queue instead of
  preempting it.
- **Cancel** sends stop for the running zone, clears the queue, and marks the
  remaining zones interrupted in status. Cancel is never refused.
- **Skip** (optional) stops the running zone and advances.
- The Phase 4 checkpoint stores the remaining queue entries rather than
  `{schedule, zone_index}`, so the queue survives a reboot under whatever policy
  decision 1 settles on.

Background and the rest of the ESPHome comparison:
[`esphome-sprinkler-lessons.md`](esphome-sprinkler-lessons.md).

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
| Deploy path | `POST /api/update` over the LAN; USB only for recovery |
