# API contract

**Status: proposed.** Nothing here is implemented. This is the shape to design
against; it will change as the firmware is built, and this file should change with
it.

Two hosts, not one:

- **The ESP32** — status, control, schedules, recent logs. Authoritative.
- **The Node service** — log history, schedule backups. Observational.

The frontend holds a base URL for each.

## ESP32

| Method | Path | Purpose |
|---|---|---|
| GET | `/api/status` | System state: active zone, time validity, current schedule, schedule version, uptime, reset reason, network, heap. |
| POST | `/api/zone/start` | Start one zone for a bounded duration. |
| POST | `/api/zone/stop` | Stop the active zone. |
| GET | `/api/schedules` | All stored schedules plus the active name. |
| PUT | `/api/schedules` | Replace stored schedules. Validated before commit. |
| PUT | `/api/schedule/active` | Set the active schedule. |
| GET/PUT | `/api/schedule/enabled` | The on/off flag. |
| POST | `/api/schedule/run` | Run a schedule now. |
| GET | `/api/zones` | Zone numbers and names. |
| PUT | `/api/zones` | Update zone names. |
| GET | `/api/logs` | The RAM ring buffer. |
| GET/PUT | `/api/config` | Device config: schedule time, timezone, log sink address. |
| POST | `/api/update` | Firmware upload (OTA). |

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
