# Phase 5 hardware test

Run 2026-11-15 on the installed ESP32 with firmware `0.6.0-phase5`, installed
by OTA (boot `c72c4971`, running partition `app1`). Driven from the Mac
(192.168.88.21) over the LAN. The system is winterized, so zones clicked dry
valves, and the scheduler stayed off throughout.

The log sink was `server/` run by hand on the Mac (`npm start`) with a scratch
SQLite file, not a deployed server. It was stopped after the test.

## Boot and the RAM ring

After the OTA reboot, `GET /api/logs` returned every line from boot, starting
at seq 1 (`hunter: bus ready on GPIO 18`). Lines logged before the first SNTP
sync have `"time": null`; the first sync came 7.5 s after boot, from the
router. `missed` stayed 0: the ring held all 54 lines of the test (capacity
200).

## UDP export

`PUT /api/config` with `log_host` 192.168.88.21 and port 5514 turned export on
at once, with no reboot (`log: UDP export to 192.168.88.21:5514`). The export
task then sent the backlog from seq 1, so the server got the boot lines too.
Lines without a device time were stored with `time_source` `estimated` (from
the line's uptime and the first timed line), the rest as `device`.

| Check | Result |
|---|---|
| Lines in the server's SQLite vs the ring | seq 1-40 all present, in order |
| Device `logs.sent` / `send_errors` / `lost` | 40 / 0 / 0 |
| Server restarted mid-test | Picked up its existing rows (40) and kept receiving |
| A line logged while the server was down (seq 41) | Missing on the server, still in the device ring |
| `log_host` set back to `""` | `logs.sink` null, export stopped, `settings_hash` back to `78a2614c` |

The seq 41 gap is the design working as intended: UDP is fire-and-forget, the
device does not know or care that nobody was listening, and the ring on the
device is where to look for such a gap. Server-side gaps are visible as missing
`seq` numbers within one `boot`.

## Runs as log lines

Two manual runs, both logged end to end on the server.

The earlier session queued zones 1, 2, 3 at 1 min each with `POST /api/run`,
skipped zone 2 after 16 s, and cancelled during zone 3 after 11 s:

```
run    queued 3 zones (manual); starting now
run    zone 1 started for 1 min (manual, 1 of 3)
run    zone 1 finished: ran its full 1 min (manual)
run    zone 2 started for 1 min (manual, 2 of 3)
run    zone 2 cut short by skip after 16 s of 1 min (manual)
run    zone 3 started for 1 min (manual, 3 of 3)
run    zone 3 cut short by cancel after 11 s of 1 min (manual)
run    manual run cancelled: 1 completed, 1 skipped, 1 cut short
```

During that run the start time was set to 14:55, inside the run, to check the
scheduler-off report. It logged once, at 14:55: `sched: scheduled run at 14:55
not started: the scheduler is off`.

This session then ran zone 1 for 1 min with `POST /api/zone/start`:

```
run    queued zone 1 for 1 min (manual); starting now
web    POST /api/zone/start -> 202
run    zone 1 started for 1 min (manual, 1 of 1)
hunter frame sent, 15 bytes, 646236 us (nominal 645234 us)
run    zone 1 finished: ran its full 1 min (manual)
run    manual run finished: 1 completed
hunter frame sent, 15 bytes, 646887 us (nominal 645234 us)
```

Refused requests log at `warn` with the reason, for example `POST
/api/zone/start refused, 400 invalid_zone: zone must be an integer 1-48`.
`frames_dropped` stayed 0.

## Status page

`GET /` returned the compiled-in status page (200, 4.4 KB, `text/html`). Its
data comes from `/api/status` and `/api/logs`, both checked above. Nobody
looked at it in a browser on the device during this test.

## Settings afterwards

Start time back to 07:00, `log_host` empty, scheduler disabled, active
schedule Late Fall. `settings_hash` `78a2614c`, the value before Phase 4 and 5
testing.

## Not tested on hardware

- Export over Wi-Fi (the `wifi` build compiles; the device uses Ethernet).
- A ring overflow with export on, which is what `lost` counts.
- The server's nightly prune against real data older than 12 months
  (host-tested only).
- The status page's Skip and Cancel buttons.
