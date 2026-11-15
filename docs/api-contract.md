# API contract

**Status: partly implemented.** `/api/status`, `/api/zone/start`,
`/api/zone/stop`, and `/api/update` exist as of Phase 1, and the `time` fields
of `/api/status` as of Phase 2, and the schedule, zone and config endpoints as
of Phase 3; their shapes below are
the implemented ones. The rest is the shape to design against, and this file
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
| POST | `/api/schedule/run` | Run a schedule now. |
| GET | `/api/zones` | Zone numbers and names. *Implemented.* |
| PUT | `/api/zones` | Replace the zone list. *Implemented.* |
| GET | `/api/logs` | The RAM ring buffer. |
| GET/PUT | `/api/config` | Device config: schedule time, timezone. *Implemented.* Log sink address comes with Phase 5. |
| POST | `/api/update` | Firmware upload (OTA). *Implemented.* |

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
Bounds come from `firmware/lib/hunter/src/hunter_limits.h`: zones 1–48,
`duration_minutes` 1–240. Values must be JSON integers; strings, floats
(including `10.0`), booleans and null are rejected, never coerced.

`POST /api/zone/start` — `{"zone": 3, "duration_minutes": 10}`

`POST /api/zone/stop` — `{}` stops the zone this device last started, if its run
has not elapsed. `{"zone": 3}` stops that zone whether or not the device thinks
it is running, which is how to stop a run that outlived an ESP reboot.

| Status | Body | Meaning |
|---|---|---|
| 202 | `{"ok": true, "action": "start", "zone": 3, "duration_minutes": 10}` | Frame queued for the bus. Sent within about a second. |
| 400 | `{"ok": false, "error": "invalid_zone" \| "invalid_duration" \| "invalid_body", "message": "..."}` | Nothing sent. `message` names the bounds. |
| 409 | `{"ok": false, "error": "no_active_zone", ...}` | Stop with no zone given and nothing known to be running. |
| 503 | `{"ok": false, "error": "bus_busy", ...}` | Bus queue full (4 frames, ~0.65 s each). Nothing changed; retry. |
| 503 | `{"ok": false, "error": "update_in_progress", ...}` | A firmware upload is running or the device is about to reboot into one. Nothing sent. |

Zone commands and firmware updates exclude each other so a reboot can never
cut a frame mid-bit. `POST /api/update` returns 503 `"error": "busy"` (nothing
flashed) if a frame is on the wire or queued, or another upload is live. An
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

Phase 2 adds `time_valid` (top level) and:

```json
"time": {"valid": true, "timezone": "PST8PDT,M3.2.0,M11.1.0", "sync_count": 3,
         "last_sync_ago_seconds": 1204,
         "utc": "2026-11-15T15:27:45Z", "local": "2026-11-15T07:27:45-08:00",
         "dst": false,
         "servers": [{"name": "192.168.88.1", "reachable": false},
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
                      "timezone": "PST8PDT,M3.2.0,M11.1.0"}
```

GETs return the same shapes, plus `"ok": true` and `settings_hash`.
`GET /api/schedules` also returns `active` and `enabled`.

Rules, from `firmware/lib/settings/src/settings.cpp`:

- **Zones:** numbers 1–48, unique; names 1–32 bytes.
- **Schedules:** at most 10; names 1–32 bytes, unique.
- **Items:** each zone must be a configured zone, at most once per schedule, with
  `duration_minutes` 1–240. Item order is run order.
- **Active:** must name an existing schedule. A PUT that would delete the
  active schedule, or remove a zone a schedule uses, is refused.
- **Config:** start time 00:00–23:59; timezone is a POSIX TZ string, 1–63 bytes.
- **Size:** the stored document is capped at 4 KB (413 `too_large`). It is
  stored compactly, so the real data (5 schedules × 8 zones) takes under 1 KB.

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

## Node service

| Method | Path | Purpose |
|---|---|---|
| GET | `/api/logs` | Log history, filterable by level, module, and time range. |
| GET | `/api/backups` | Available schedule backups. |
| GET | `/api/backups/:timestamp` | One backup, for restoring by hand. |
| — | (static) | The built frontend. |

Inbound from the ESP: UDP syslog. Not an HTTP endpoint, deliberately — see
`architecture.md`.

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
