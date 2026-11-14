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
the React UI and collects fire-and-forget UDP log lines from the ESP; it holds no
authority over watering. Remote access is handled by the router's VPN, so there is
no auth layer in the application.

See [`docs/architecture.md`](docs/architecture.md) for the detail and
[`docs/open-decisions.md`](docs/open-decisions.md) for what is still unsettled.

## Status

Scaffolding. No implementation yet. See
[`docs/migration-plan.md`](docs/migration-plan.md) for the phase order.

## Hardware

- Waveshare ESP32-S3-ETH (ESP32-S3R8, 16 MB flash, 8 MB octal PSRAM, W5500
  Ethernet over SPI), powered over PoE.
- Hunter Pro-C irrigation controller, driven over its REM port via the Hunter
  SmartPort protocol.
- GPIO 18 to REM; see `firmware/` for the full pin map once it exists.

## License

Not yet chosen — see [`docs/open-decisions.md`](docs/open-decisions.md). The v1
repositories disagree (firmware GPL-3.0, frontend MIT), and the GPL is likely
load-bearing for the firmware because the Hunter SmartPort implementation derives
from GPL-licensed work.
