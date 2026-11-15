# API contract

**Status: partly implemented.** `/api/status`, `/api/zone/start`,
`/api/zone/stop`, and `/api/update` exist as of Phase 1; their shapes below are
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
| GET | `/api/schedules` | All stored schedules plus the active name. |
| PUT | `/api/schedules` | Replace stored schedules. Validated before commit. |
| PUT | `/api/schedule/active` | Set the active schedule. |
| GET/PUT | `/api/schedule/enabled` | The on/off flag. |
| POST | `/api/schedule/run` | Run a schedule now. |
| GET | `/api/zones` | Zone numbers and names. |
| PUT | `/api/zones` | Update zone names. |
| GET | `/api/logs` | The RAM ring buffer. |
| GET/PUT | `/api/config` | Device config: schedule time, timezone, log sink address. |
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

`commanded_zone` is `null` when idle. It is what this device last commanded and
has not yet timed out, not a reading from the Pro-C: a run started at the Pro-C
itself, or one from before a reboot, does not appear.

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
- `EO` — every other day, by calendar date parity.

`ALL`, `NONE`, and `EO` are exclusive; they cannot be combined with named days.
Month-end parity for `EO` is a documented test case, not an implementation detail
to rediscover.
