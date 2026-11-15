# Phase 4 hardware test

Run 2026-11-15 on the installed ESP32 with firmware `0.5.0-phase4`, driven from
the Mac over the LAN. The system is winterized, so zones clicked dry valves.
Nobody watched the Pro-C display for these runs. What the Pro-C does with each
frame was established in [Phase 1](hardware-test-phase1.md). This test checks
the sequencing, the trigger, and recovery, as seen in `/api/status` and the
frame counter.

## Manual queue

Zone 1 for 1 min, then zone 2 for 1 min, queued back to back.

| Time | Status |
|---|---|
| +3 s | zone 1 running, zone 2 pending; 1 frame |
| +61 s | `padding`; zone 1 completed; 2 frames (start 1, stop 1) |
| +64 s | zone 2 running; 3 frames |

A firmware upload during the run returned 503 `busy`, and nothing was flashed.

**Bug found and fixed:** an empty upload (`-F firmware=@/dev/null`) returned
`"ok": true` and rebooted the device. The cause was a Phase 0 bug in the upload
handler, plus `Sequencer::idle()` reporting idle before its first tick. The
manual run queued just before it was lost to the reboot, so this mattered.
After the fix, an empty upload returns 500 `no complete firmware image
received` and does not reboot.

## Scheduled run with skip and a forced reboot

A temporary schedule `Test` was added: zones 1, 2, 3, `ALL`, 2 min each. It was
made active, the start time was set a minute ahead, and the scheduler was
enabled. Afterwards the real settings were restored. `settings_hash` returned
to `78a2614c`, the value before the test.

| Time | Event | Status |
|---|---|---|
| 08:26:00 | trigger fired | `Test` running zone 1; trigger `already_ran` |
| 08:26:22 | `POST /api/run/skip` | 202, zone 1 `skipped`; `padding` |
| 08:26:30 | | zone 2 running |
| 08:26:55 | `POST /api/restart` | 409 `run_in_progress` |
| 08:26:55 | `POST /api/restart?force=1` | rebooted |
| 08:27:21 | after boot (23 s up) | recovery `resumed`; zone 2 `running` with 66 s left; **0 frames sent** (zone 2 was not restarted) |
| ~08:28:30 | zone 2's time ran out | stop 2, pad, start 3 |
| 08:30:36 | | idle; zone 1 `skipped`, zones 2 and 3 `completed`; 3 frames since boot |

One gap the run exposed: right after recovery the `zones` countdown was `null`.
The sequencer knew zone 2 was running, but the status countdown didn't. Fixed
with `ZoneController::adopt()`. On a repeat (manual zones 1 and 2, forced
reboot 20 s in), status 24 s after boot showed `commanded_zone: 1` with 74 s
left, recovery `resumed`, and 0 frames sent.

## Run now

- **Saved schedule.** `POST /api/schedule/run {"name": "Catch up"}` queued all
  8 zones in schedule order with their own durations, and zone 1 started at
  once. Skip moved it to padding. Cancel marked the rest `interrupted`.
- **Ad-hoc list.** `POST /api/run` with zones 4 and 6 at 1 min each queued
  both, and zone 4 started. A list with zone 60 in it returned 400 and queued
  nothing. An unknown schedule name returned 404.
- **Skip with a JSON body.** It reached the skip route, not the `/api/run`
  JSON handler. Plain-string routes match their sub-paths, so registration
  order matters.
- **Cancel between zones.** It reported `zone: 2`, the next zone, which had
  never started. It now reports `null`.

## Not covered on hardware

- **The skip side of recovery** (back more than 60 minutes late). It's
  host-tested only, and would need the ESP off for over an hour mid-run.
- **Recovery after a real power cut.** The clock is lost and recovery waits for
  SNTP. The forced restart above kept power, so the internal clock carried
  through.
- **The trigger's 60-minute catch-up window, and DST days.** Both are
  host-tested only.
- **A real watering run with the water on.** That waits for spring, along with
  the Phase 1 step 8 repeat.
