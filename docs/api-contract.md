# API contract

**Status: partly implemented.** `/api/status`, `/api/zone/start`,
`/api/zone/stop`, and `/api/update` exist as of Phase 1, and the `time` fields
of `/api/status` as of Phase 2, and the schedule, zone and config endpoints as
of Phase 3, and the run endpoints and status fields as of Phase 4, and the
logging endpoints of both hosts as of Phase 5, and the Node service's backups and static hosting as of Phase 6; their shapes below are the
implemented ones. The rest is the shape to design against, and this file
should change with the firmware.

Two hosts, not one:

- **The ESP32** — status, control, schedules, recent logs. Authoritative.
- **The Node service** — log history, schedule backups. Observational.

The frontend holds a base URL for each.

## ESP32

| Method | Path | Purpose |
|---|---|---|
| GET | `/api/status` | System state: active zone, time validity, current schedule, schedule version, uptime, reset reason, network, heap. |
| POST | `/api/zone/start` | Start one zone for a bounded duration. *Implemented.* |
| POST | `/api/zone/stop` | Stop the active zone, or a named one. *Implemented.* |
| GET | `/api/schedules` | All stored schedules plus the active name. *Implemented.* |
| PUT | `/api/schedules` | Replace stored schedules. Validated before commit. *Implemented.* |
| PUT | `/api/schedule/active` | Set the active schedule. *Implemented.* |
| GET/PUT | `/api/schedule/enabled` | The on/off flag. *Implemented.* |
| POST | `/api/schedule/run` | Run a saved schedule now. *Implemented (Phase 4).* |
| POST | `/api/run` | Run an ad-hoc list of zones now. *Implemented (Phase 4).* |
| GET | `/api/zones` | Zone numbers and names. *Implemented.* |
| PUT | `/api/zones` | Replace the zone list. *Implemented.* |
| GET | `/api/logs` | The RAM ring buffer. *Implemented (Phase 5).* |
| GET/PUT | `/api/config` | Device config: schedule time, timezone, log sink. *Implemented.* |
| GET | `/` | Fallback status page, compiled into the firmware. *Implemented (Phase 5).* |
| POST | `/api/update` | Firmware upload (OTA). *Implemented.* Refused while a run is in progress. |
| POST | `/api/run/skip` | Skip the zone the run is on. *Implemented (Phase 4).* |
| POST | `/api/restart` | Reboot. *Implemented (Phase 4).* 409 during a run unless `?force=1`. |

Notes:

- `/api/status` must carry **both** `time_valid` and a schedule version
  (counter or hash). The first gates execution; the second lets the server detect
  drift between its backup and the device.
- Durations: v1 mixed units — the frontend and API spoke seconds, the firmware
  and the SmartPort protocol speak minutes, and v1's API did an integer
  `seconds // 60` conversion, which silently turned a 30-second zone into a stop
  command. **Pick one unit, state it in every field name, and validate at the
  boundary.**
- Zone and duration bounds belong in one place, shared between validation and
  tests. The SmartPort protocol accepts zones 1–48 and 0–240 minutes; v1's
  firmware separately capped zones at 1–20 and minutes at 0–120, and the API
  capped durations at 7200 seconds. Three different limits for the same thing.

### Zone control (implemented)

Both endpoints take `Content-Type: application/json`; anything else gets 415.
Bounds come from `firmware/lib/hunter/src/hunter_limits.h`: zones 1–10,
`duration_minutes` 1–240. The protocol goes to 48, but the firmware accepts
10, the installed Pro-C's count, so the settings document fits its 4 KB cap
(decision 7 in `open-decisions.md`). Values must be JSON integers; strings, floats
(including `10.0`), booleans and null are rejected, never coerced.

Since Phase 4 both go through the sequencer, which owns all zone commands.

`POST /api/zone/start` — `{"zone": 3, "duration_minutes": 10}`. Queues a
manual run. If a schedule or another manual run is going, it goes on the end
and never cuts in. The response carries `"queued_behind": N`. Manual starts
ignore the schedule on/off flag and `time_valid`.

`POST /api/zone/stop`:
- `{}` cancels the run. It stops the running zone, and the zone plus
  everything still queued is marked `interrupted`. It is never refused for
  being busy.
- `{"zone": 3}` stops that zone. If it is the zone the run is on, that is a
  cancel too. Otherwise it is just a stop, which is how to stop a zone started
  at the Pro-C or before an ESP reboot.

`POST /api/schedule/run` — `{"name": "Catch up"}`. Runs every zone in a saved
schedule once, now, in schedule order, ignoring its day patterns. Zones marked
`NONE` are left out. It returns 202
`{"action": "run_schedule", "schedule": "Catch up", "zones": [1, 2, ...],
"queued_behind": 0}`, 404 `unknown_schedule`, or 409 `nothing_to_run` (every
zone is `NONE`).

`POST /api/run` — `{"items": [{"zone": 1, "duration_minutes": 20}, ...]}`. An
ad-hoc queue, run now, without saving a schedule. Every item is validated
first. If any is bad (400 naming `items[i]`), or the run would pass 64 entries
(503 `queue_full`), nothing is queued.

Both behave like manual starts. They ignore the on/off flag and `time_valid`,
join the end of any run in progress, and are cancelled or skipped the same
way. Their entries have `origin: "manual"`.

`POST /api/run/skip`, no body: stops the running zone, marks it `skipped`,
and moves on to the next after the usual pad. Between zones it skips the one
about to start. Returns 202 `{"action": "skip", "zone": N}`, or 409
`nothing_to_skip`.

| Status | Body | Meaning |
|---|---|---|
| 202 | `{"ok": true, "action": "start", "zone": 3, "duration_minutes": 10, "queued_behind": 0}` | Queued; starts within ~0.1 s if nothing is ahead of it. |
| 202 | `{"ok": true, "action": "cancel" \| "stop", "zone": 3}` | Stop sent (retried if the bus is busy). For a cancel between zones, `zone` is `null`. |
| 400 | `{"ok": false, "error": "invalid_zone" \| "invalid_duration" \| "invalid_body", "message": "..."}` | Nothing sent. `message` names the bounds. |
| 409 | `{"ok": false, "error": "no_active_zone", ...}` | `{}` stop with nothing running or queued. |
| 503 | `{"ok": false, "error": "queue_full", ...}` | The run already holds 64 entries. Nothing changed. |
| 503 | `{"ok": false, "error": "update_in_progress", ...}` | A firmware upload is running or the device is about to reboot into one. Nothing sent. |

Zone commands and firmware updates exclude each other, so a reboot can never
cut a frame mid-bit or interrupt a run on purpose. `POST /api/update` returns
503 `"error": "busy"` and flashes nothing in three cases: a run is in progress,
a frame is on the wire or queued, or another upload is live. An upload with no
complete image returns 500 and does not reboot. An
upload that sends nothing for 30 s counts as abandoned: it stops blocking zone
commands, and the next upload aborts it.

202, not 200: the bus write takes about 0.65 s and happens on its own task after
the response. The SmartPort line is write-only, so there is no acknowledgement
from the Pro-C to wait for anyway.

`GET /api/status` gains:

```json
"zones": {"commanded_zone": 3, "duration_minutes": 10, "remaining_seconds": 412},
"hunter_bus": {"pin": 18, "ready": true, "busy": false, "queued": 0,
               "frames_sent": 4, "frames_dropped": 0,
               "last_sent_ago_ms": 9120, "last_frame_us": 645310}
```

Phase 4 adds the run, the daily trigger, and reboot recovery:

```json
"run": {"state": "running", "schedule": "Fall", "current_index": 1,
        "remaining_seconds": 412,
        "entries": [{"zone": 1, "duration_minutes": 20, "origin": "schedule",
                     "outcome": "skipped", "started_epoch": 1794755582},
                    {"zone": 3, "duration_minutes": 20, "origin": "schedule",
                     "outcome": "running", "started_epoch": 1794755890},
                    {"zone": 7, "duration_minutes": 5, "origin": "manual",
                     "outcome": "pending"}]},
"trigger": {"last_decision": "already_ran", "window_minutes": 60,
            "last_run_day": 20407},
"recovery": {"result": "none", "window_minutes": 60}
```

- **`state`:** `idle`, `starting` (retrying a busy bus), `running`, or
  `padding` (the 5 s between stop(A) and start(B)).
- **`entries`:** the run in progress, or the last one once idle, so an
  interrupted run stays visible until the next one starts.
- **`outcome`:** `pending`, `running`, `completed`, `skipped`, `interrupted`
  (cancelled, or dropped by recovery), or `failed` (the bus refused the start
  five times; the run is abandoned).
- **`schedule`:** `null` for a purely manual run.
- **`zones`** (above) still carries the running zone's countdown, in the same
  shape for manual and scheduled runs.
- **`trigger.last_decision`:** `due`, `time_invalid`, `disabled`,
  `no_active_schedule`, `already_ran`, `before_start`, or `window_missed`.
  The run starts once per local date, between `start_time` and 60 minutes
  after it.
- **`last_run_day`:** days since 1970-01-01, recorded when a run starts.
- **`recovery.result`:**
  - `none`
  - `waiting_for_time`: an interrupted run was found at boot
  - `resumed` or `skipped`: per open decision 1, with `late_seconds`
  - `superseded`: a manual start came first
  - `checkpoint_unreadable`

Phase 2 adds `time_valid` (top level) and:

```json
"time": {"valid": true, "timezone": "PST8PDT,M3.2.0,M11.1.0", "sync_count": 3,
         "last_sync_ago_seconds": 1204,
         "utc": "2026-11-15T15:27:45Z", "local": "2026-11-15T07:27:45-08:00",
         "dst": false,
         "servers": [{"name": "192.0.2.1", "reachable": false},
                     {"name": "pool.ntp.org", "reachable": true},
                     {"name": "time.google.com", "reachable": false}]}
```

`utc`, `local` and `dst` are `null`/absent until the first sync. `reachable`
means the server answered at least once in lwIP's last eight polls; an unused
fallback shows `false`.

`commanded_zone` is `null` when idle. It is what this device last commanded and
has not yet timed out, not a reading from the Pro-C: a run started at the Pro-C
itself, or one from before a reboot, does not appear.

### Settings (implemented)

Zones, schedules, the active name, the on/off flag and config are stored as one
document. Every PUT replaces one section, then validates the whole document and
writes it to flash. If any step fails, nothing changes. Writes need
`Content-Type: application/json`; anything else gets 415. Every 200 response
carries `settings_hash`, also in `/api/status`, which changes exactly when the
stored content changes.

```json
PUT /api/zones       {"zones": [{"zone": 1, "name": "Walkway Beds"}, ...]}
PUT /api/schedules   {"schedules": [{"name": "Fall", "items": [
                        {"zone": 1, "days": "TU:F", "duration_minutes": 30}, ...]}]}
PUT /api/schedule/active   {"name": "Fall"}      null clears it
PUT /api/schedule/enabled  {"enabled": true}
PUT /api/config      {"start_hour": 7, "start_minute": 0,
                      "timezone": "PST8PDT,M3.2.0,M11.1.0",
                      "log_host": "192.0.2.21", "log_port": 5514,
                      "log_level": "info"}
```

GETs return the same shapes, plus `"ok": true` and `settings_hash`.
`GET /api/schedules` also returns `active` and `enabled`.

Rules, from `firmware/lib/settings/src/settings.cpp`:

- **Zones:** numbers 1–10, unique; names 1–32 bytes. See decision 7 in
  `open-decisions.md` for why not 48.
- **Schedules:** at most 10; names 1–32 bytes, unique.
- **Items:** each zone must be a configured zone, at most once per schedule, with
  `duration_minutes` 1–240. Item order is run order.
- **Active:** must name an existing schedule. A PUT that would delete the
  active schedule, or remove a zone a schedule uses, is refused.
- **Config:** start time 00:00–23:59; timezone is a POSIX TZ string, 1–63 bytes.
  `log_host` is empty (export off) or a dotted IPv4 address; `log_port` is
  1–65535, default 5514. `log_level` is `error`, `warn`, `info` or `debug`
  (lowercase; default `debug`), see [Log level](#log-level). `start_hour`,
  `start_minute` and `timezone` are required in a PUT; `log_host`, `log_port`
  and `log_level` are optional and keep their current values when left out. A
  change takes effect at once, with no reboot. At their defaults (export off,
  port 5514, level `debug`) they do not affect `settings_hash`, so a device
  that never set them hashes as before. Setting a non-default level changes
  the hash.
- **Size:** the stored document is capped at 4 KB (413 `too_large`; nothing is
  written). It is stored compactly: the real data (5 schedules × 8 zones) takes
  853 bytes, and the worst case allowed by the rules above (10 zones, 10
  schedules each running every zone, 32-byte names) about 3.7 KB. Only names
  full of characters JSON must escape (`"`, `\`, control characters) can push
  a valid document past the cap. Separately, a PUT body over 16 KB is refused by
  the web server with a bare 413 before it is parsed; at 10 zones the largest
  valid `PUT /api/schedules` body is about 6.7 KB.

Errors: 400 `invalid_body` (wrong shape or type) or `invalid_settings` (a rule
above, with a message naming it), 413 `too_large`, 500 `storage_failed`.

`/api/status` adds `schedule_source: "nvs"`, a top-level `settings_hash`, and:

```json
"settings": {"load": "loaded", "hash": "78a2614c", "zones": 8, "schedules": 5,
             "active_schedule": "Late Fall", "enabled": false,
             "start_time": "07:00", "today": [{"zone": 5, "duration_minutes": 20}]}
```

`load` is `empty` on a fresh device. It is `corrupt` (with `load_error`) if the
stored document fails to parse or validate. Either way the device runs on safe
defaults (nothing configured, disabled) until a successful write. `today` lists
what the active schedule runs on today's local date, and appears only while
`time_valid`.

### Logs (implemented)

The device keeps its last 200 log lines in RAM. It never writes logs to flash,
and the ring works whether or not the network is up.

```
GET /api/logs?since=<seq>&level=<error|warn|info|debug>&limit=<1-200>
```

All parameters are optional: `since` defaults to 0 (everything in the ring),
`level` to `debug` (all lines; `warn` means warn and error), `limit` to 200.

```json
{"ok": true, "capacity": 200, "oldest_seq": 12, "last_seq": 211, "missed": 0,
 "entries": [{"seq": 12, "uptime_ms": 81234, "time": "2026-11-15T22:40:20Z",
              "level": "info", "module": "run",
              "message": "zone 1 started for 1 min (manual, 1 of 3)"}]}
```

`seq` counts lines since boot. `time` is UTC, or null for lines logged before
the clock was valid. `missed` counts lines after `since` that were already
overwritten. To follow the log, pass the last `seq` you saw as `since`.
Malformed parameters get 400 `invalid_query`.

Modules: `main`, `boot`, `net`, `time`, `settings`, `sched`, `run`, `hunter`,
`web`, `ota`, `log`. Run lines (`run`) say what was queued, each zone's start, how it
ended (ran its full time, cut short by skip or cancel, failed), and whether it
was scheduled or manual. Once a day at the start time, `sched` says why a
scheduled run did not start, for example when the scheduler is off. Refused
requests log at `warn` with their reason.

<a id="log-level"></a>**Log level.** `log_level` in `/api/config` sets the least severe level the
device records. A line below it never enters the ring, so it is not in
`GET /api/logs` and is never exported; it still goes to the USB serial console.
`warn` records warnings and errors; `debug` (the default) records everything.
It is stored in NVS with the rest of the config and applies from the next line
logged. Debug lines are rare (bus retries, scheduler state changes), so
`debug` costs little; `info` is the level to pick to drop them. The `?level=`
query on `/api/logs` filters further within what was recorded. A UI can label
the four values Error, Warning, Info and Debug, but sends the lowercase names.

**UDP export.** When `log_host` is set, a low-priority task sends every line,
including the backlog still in the ring, as RFC 5424 syslog over UDP
(facility local0, MSGID is the module). It never waits on
the network and never retries: a line sent while nobody is listening is gone
from the server but still in the ring. The structured data carries the boot id,
`seq` and `uptime_ms`, so the server can spot gaps and place lines that had no
time.

`/api/status` adds:

```json
"logs": {"capacity": 200, "last_seq": 54, "sink": "192.0.2.21:5514",
         "level": "debug", "sent": 52, "send_errors": 0, "lost": 0,
         "boot_id": "c72c4971"}
```

`sink` is null when export is off. `level` is the recording level in effect. `lost` counts lines overwritten in the ring
before the export task could send them; lines logged while export was off are
not counted.

### Browser access (CORS)

The web UI is hosted by the Node server, a different origin from the device,
so the device answers CORS. A request whose `Origin` is on the local network
(localhost, 10/8, 172.16/12, 192.168/16, 169.254/16, a single-label host
name, or `.local`, `.lan`, `.home.arpa`) gets `Access-Control-Allow-Origin`
echoing it, and an `OPTIONS` preflight from one gets 204. Any other origin gets
no CORS headers, a 403 to its preflight, and a 403 `forbidden_origin` to any
non-GET request, including the bare POSTs a page could send without a
preflight. Requests with no `Origin` (curl, the Node server's backups) are
unaffected. The check is `cors::isAllowedOrigin` in
`firmware/lib/api/src/cors.h`, host-tested in `test/test_cors`.

## Node service

| Method | Path | Purpose |
|---|---|---|
| GET | `/api/logs` | Log history, filterable by level, module, and time range. *Implemented (Phase 5).* |
| GET | `/api/health` | Row count, UDP sink counters, last backup. *Implemented (Phase 5; `backup` Phase 6).* |
| GET | `/api/backups` | Available settings backups. *Implemented (Phase 6).* |
| GET | `/api/backups/:timestamp` | One backup, for restoring by hand; `latest` for the newest. *Implemented (Phase 6).* |
| GET | (anything else) | The built frontend. *Implemented (Phase 6).* |

Inbound from the ESP: UDP syslog. Not an HTTP endpoint, deliberately — see
`architecture.md`.

```
GET /api/logs?level=warn&module=run,sched&since=2026-11-01&until=...&limit=500&before_id=<id>
```

All optional. `level` is the least severe level to include. `module` takes a
comma list or repeats. `since` (inclusive) and `until` (exclusive) take ISO 8601
or epoch milliseconds. `limit` is 1–5000, default 500. Results are newest first; when
there are more, `next_before_id` is set, and passing it as `before_id` gets the
next page.

```json
{"ok": true, "next_before_id": null,
 "entries": [{"id": 52, "time": "2026-11-15T23:39:19.000Z",
              "time_source": "device", "received": "2026-11-15T23:39:19.012Z",
              "level": "debug", "module": "hunter",
              "message": "...", "host": "192.0.2.54", "boot": "c72c4971",
              "seq": 52, "uptime_ms": 2766354}]}
```

`time_source` is `device` (the ESP's clock), `estimated` (a line from before
the ESP's clock was valid, placed by its uptime once a timed line from the same
boot arrived), or `received` (the server's clock, when neither was possible).
Rows older than 12 months are deleted nightly at 03:30 local time.

**Backups.** Taken at startup and nightly at 03:45 local time, from the ESP's
`/api/zones`, `/api/schedules` and `/api/config` (read again if their
`settings_hash` values disagree, meaning a change landed mid-read), plus
`firmware_version` from `/api/status`.

```json
GET /api/backups
{"ok": true, "backups": [{"timestamp": "2026-11-16T11-45-00Z",
  "taken_at": "2026-11-16T11:45:00.463Z", "settings_hash": "78a2614c",
  "bytes": 7208}]}

GET /api/backups/2026-11-16T11-45-00Z
{"ok": true, "timestamp": "2026-11-16T11-45-00Z",
 "backup": {"format": "isprinklr-settings-backup", "format_version": 1,
   "taken_at": "...", "device": "http://192.0.2.10",
   "firmware_version": "0.6.0-phase5", "settings_hash": "78a2614c",
   "restore": [{"path": "/api/zones", "body": {"zones": [...]}},
               {"path": "/api/schedules", "body": {"schedules": [...]}},
               {"path": "/api/schedule/active", "body": {"name": "Late Fall"}},
               {"path": "/api/config", "body": {"start_hour": 7, ...}},
               {"path": "/api/schedule/enabled", "body": {"enabled": false}}]}}
```

Timestamps are UTC with `-` for `:`, newest first. `restore` holds each PUT
body exactly as the device takes it, in the order a blank board needs.
`/api/health` gains `"backup": {"last_attempt", "last_success",
"last_timestamp", "last_error"}`, or null when backups are off. Unknown
`/api/...` paths are JSON 404s; any other GET is the frontend, with
`index.html` for paths that name no file.

## Day patterns

Carried over from v1, which the frontend already understands:

- Weekdays: `M`, `TU`, `W`, `TH`, `F`, `SA`, `SU`, colon-joined (`M:W:F`).
- `ALL` — every day.
- `NONE` — never.
- `EO` — every other day, strictly alternating: runs on local dates whose day
  count since 1970-01-01 is even.

`ALL`, `NONE`, and `EO` are exclusive; they cannot be combined with named days.
Input is case-insensitive (v1 data has `Tu:F`); the device returns canonical
uppercase, Monday-first (`TU:F`).

`EO` deliberately differs from v1, which used day-of-year parity and so ran on
both Dec 31 and Jan 1 in non-leap years. Calendar-date parity would do the same
after every 31-day month. No v1 schedule used `EO`. The boundaries are host
tests in `firmware/test/test_day_pattern`.
