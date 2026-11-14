# Architecture

## Principle

The ESP32 is the system of record. Everything else is observational.

If the server is off, the network is down, or the frontend is broken, the lawn
still gets watered on schedule. The only thing lost is visibility.

This is the one invariant worth protecting at the cost of convenience elsewhere.

## Components

### ESP32-S3 (`firmware/`)

Owns:

- **Schedules** — stored in NVS, authoritative. Zone/day/duration triples plus
  the active schedule name, zone names, and the on/off flag.
- **Time** — SNTP, with a timezone string for DST. `time_valid` is an explicit
  state; schedule execution is blocked while it is false.
- **Execution** — the daily trigger, day-pattern matching, and the zone
  sequencing state machine.
- **Recovery** — a checkpoint in NVS at every zone transition, reconciled on
  boot against the reset reason and freshly synced time.
- **Control API** — HTTP over Ethernet, consumed by the frontend.
- **Diagnostics** — an in-RAM ring buffer of recent log lines, readable over
  HTTP, plus fire-and-forget UDP log export.

### Node service (`server/`)

Serves the built React frontend and collects logs. Holds no authority over
watering.

- Serves `web/`'s static build directly. No nginx — unnecessary at this scale.
- Listens for UDP syslog from the ESP and stores it with enough structure to
  filter by level, module, and time range.
- Pulls the ESP's schedule nightly and writes a timestamped JSON backup. This is
  the disaster-recovery story for a wiped or dead board.

### React frontend (`web/`)

Vite, rebuilt from scratch. Talks to **two** hosts: the ESP for status, control,
and schedule editing; the Node service for log history. Config carries two base
URLs, not one.

## Safety properties

**The Pro-C holds the zone timer, not the ESP.** The SmartPort `startZone(zone,
minutes)` command tells the Hunter controller to run a zone for a bounded number
of minutes, and the Pro-C counts down on its own. An ESP reboot, panic, or
brownout mid-zone still ends with the valve closing on time.

This is why the migration is safe to attempt at all, and it constrains the
design: **always command a real duration, never an open-ended "on."**

**The logger cannot block the controller.** Log export is UDP, fire-and-forget —
no connection, no ack, no timeout, no retry. A dead log sink is invisible to the
ESP. This rules out HTTP log shipping, whose failure path would stall the device
that is supposed to be opening valves.

**Schedule execution is idempotent across reboots.** A persisted last-run date
means a reboot after the daily trigger cannot cause a second watering cycle.

## Authority and drift

The ESP owns the schedule. The server caches and backs it up. One direction,
always.

v1's failure mode here was two authoritative copies that could silently
disagree. To make drift detectable, the ESP exposes a version counter or hash of
its stored schedule in its status response, so the server can notice a mismatch
rather than discovering it on a morning when nothing ran.

## Network and access

Everything runs on a trusted LAN. Remote access is the router's VPN, so there is
no auth, no TLS, and no reverse proxy in the application. This is a deliberate
choice that depends on the VPN; it is not a model for exposing any of this to the
internet.

## Deliberate non-goals

- **Serving the frontend from the ESP.** Frontend rebuilds are frequent, firmware
  flashes are not. A minimal status page on the device is the fallback for when
  the server is down; the real UI stays on the server.
- **Storing log history on the ESP.** Flash wear, and a filesystem the device does
  not otherwise need. The RAM ring buffer covers "what just happened"; the server
  covers history.
- **A battery-backed RTC.** SNTP plus the internal RTC is enough while the wired
  network is up. Revisit only if combined power-and-WAN outages prove to be a real
  pattern — and note GPIO 9 is the W5500 reset, so I²C would need remapping.
- **Auth.** See above. The VPN is the boundary.
