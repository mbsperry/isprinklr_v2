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

## 2. License

**Blocks:** nothing immediately; should be settled before the repo goes public-facing.

The v1 repos disagree: firmware is GPL-3.0, frontend is MIT (and the MIT header
reads "Copyright (c) 2022 GitHub" — a leftover from the Codespaces template, not a
deliberate choice).

The GPL is probably load-bearing for the firmware: the Hunter SmartPort
implementation derives from GPL-licensed work by several authors. A monorepo can
carry per-directory licenses if a single one is unattractive.

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
