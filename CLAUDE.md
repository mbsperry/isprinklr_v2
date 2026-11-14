# iSprinklr v2

Irrigation controller: ESP32-S3 firmware plus a small Node service and React UI.
Monorepo. Read [`docs/architecture.md`](docs/architecture.md) before making design
decisions and [`docs/open-decisions.md`](docs/open-decisions.md) before settling
anything listed there.

## The invariant

**The ESP32 is the system of record; everything else is observational.** If the
server, the network, or the frontend is down, watering continues. Any change that
makes watering depend on the server is wrong, however convenient.

Two consequences that are easy to violate by accident:

1. **Always command the Hunter controller a bounded duration.** The Pro-C holds
   the zone timer, so an ESP crash mid-zone still closes the valve. Never issue an
   open-ended "on."
2. **Log export is UDP, fire-and-forget.** No HTTP, no retries, no timeouts. A log
   path that can block must never touch the controller.

## Layout

- `firmware/` — PlatformIO, ESP32-S3. Schedules, time, sequencing, control API.
- `web/` — React (Vite).
- `server/` — Node: static hosting, UDP log sink, schedule backup.
- `docs/` — architecture, migration plan, API contract, open decisions.

## Conventions

- Firmware: pure logic must be host-testable. Keep I/O at the edges — frame
  construction separate from bus writes, storage behind an interface, business
  logic out of HTTP route handlers. v1 failed this and had no unit tests as a
  result.
- Units: state them in field names. v1 mixed seconds and minutes across layers
  and silently truncated durations.
- Bounds (zone range, duration limits) live in one place, shared by validation and
  tests. v1 had three disagreeing sets.
- NVS is not thread-safe: all access through one task or behind a mutex.

## Context

v1 lives in `mbsperry/isprinklr_esp`, `mbsperry/isprinklr_api`, and
`mbsperry/iSprinklr_react`. Useful as reference, not as a template — the Python
API owned scheduling and is being removed, not ported.
