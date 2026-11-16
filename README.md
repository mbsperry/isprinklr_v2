# iSprinklr v2

Control software for a Hunter Pro-C irrigation system, built around a Waveshare
ESP32-S3-ETH controller.

This is a rewrite of the three-repo [isprinklr_esp][esp] / [isprinklr_api][api] /
[iSprinklr_react][react] setup into a single repository. The defining change:
**the ESP32 owns scheduling.** It decides what to water and when, and keeps doing
so with every other machine switched off. Nothing else in the system is required
for the lawn to get watered.

[esp]: https://github.com/mbsperry/isprinklr_esp
[api]: https://github.com/mbsperry/isprinklr_api
[react]: https://github.com/mbsperry/iSprinklr_react

## Why v2

In v1, a Python/FastAPI service held the schedule, ran an APScheduler job each
morning, and drove the ESP32 one zone at a time over HTTP — the API kept an
hours-long async task alive for the duration of a watering cycle. If that service
or its host went down mid-cycle, watering stopped partway through and nothing
reported it.

v2 inverts the dependency. The ESP32 is the system of record; the server is
observational.

## Layout

| Directory | Contents |
|---|---|
| [`firmware/`](firmware/) | ESP32-S3 firmware (PlatformIO). Schedules, timekeeping, zone sequencing, reboot recovery, control API. |
| [`web/`](web/) | React frontend (Vite). |
| [`server/`](server/) | Node service: serves the built frontend, receives logs over UDP, backs up the ESP's schedule. |
| [`docs/`](docs/) | Architecture, migration plan, API contract, open decisions. |

## Architecture in one paragraph

The ESP32 stores schedules in NVS, syncs time over SNTP, runs the daily schedule
itself, and checkpoints its position so a reboot mid-cycle is recoverable. It
commands the Pro-C with a bounded duration, so the irrigation controller's own
timer closes the valve even if the ESP vanishes. A Node service on the LAN serves
the React UI, forwards the UI's controller calls to the ESP, and collects
fire-and-forget UDP log lines from the ESP; it holds no authority over watering.
Remote access is a Cloudflare tunnel to the Node service, gated by Cloudflare
Access, so there is no auth layer in the application.

See [`docs/architecture.md`](docs/architecture.md) for the detail and
[`docs/open-decisions.md`](docs/open-decisions.md) for what is still unsettled.

## Status

Phases 0–6 are built and merged: OTA, the Hunter driver, time, schedules in NVS,
the scheduling engine, logging, and the Node server and React frontend. The
firmware runs on the installed board, and the server runs as a systemd service
on a small Linux container. The scheduler is off on the device until spring, and
v1 is kept as an archive until v2 has run a full watering season. See
[`docs/migration-plan.md`](docs/migration-plan.md) for each phase's status.

## Hardware

- Waveshare ESP32-S3-ETH (ESP32-S3R8, 16 MB flash, 8 MB octal PSRAM, W5500
  Ethernet over SPI), powered over PoE.
- Hunter Pro-C irrigation controller, driven over its REM port via the Hunter
  SmartPort protocol.
- GPIO 18 to REM; see [`firmware/README.md`](firmware/README.md#target) for the
  full pin map.

## License

Copyright (C) 2026 Matt Sperry. Licensed under the GNU General Public License,
version 3 or (at your option) any later version. See [`LICENSE`](LICENSE).

The Hunter SmartPort driver is adapted from the GPL-3.0-or-later
[HunterRoam](https://github.com/ecodina/hunter-wifi) library by Eloi Codina
Torras, which builds on work by Sebastien
([OpenSprinkler-Firmware-Hunter](https://github.com/seb821/OpenSprinkler-Firmware-Hunter)),
Dave Fleck and Scott Shumate. See [`NOTICE`](NOTICE) for attribution. Source files
carry [SPDX](https://spdx.dev/) headers.
