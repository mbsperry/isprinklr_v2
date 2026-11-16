# firmware

ESP32-S3 firmware. PlatformIO project.

**Phase 0 is installed (2026-11-14).** Ship changes over the
network with the [routine OTA update](../docs/phase0-runbook.md#routine-ota-update);
CI runs the host tests and builds both device envs on every firmware change.

**Phase 1** adds the Hunter SmartPort driver, manual zone start/stop over HTTP,
and host unit tests. It has not been run against the Pro-C yet; see
[`hardware-test-phase1.md`](../docs/hardware-test-phase1.md). No timekeeping or
schedule storage yet; those ship over OTA in Phases 2 and 3.

**Phase 2** adds SNTP time (US Pacific, automatic DST) and `time_valid` in
status. Running on the device since 2026-11-15.

**Phase 3** stores zones, schedules, the active schedule, the on/off switch and
config in NVS, with read/write endpoints. Running on the device since
2026-11-15, holding the data imported from v1.

**Phase 4** runs schedules. A sequencer owns every zone command, both
scheduled and manual. A daily trigger starts the active schedule. A checkpoint
in NVS lets a run recover after a reboot (resume within 60 minutes, else skip).
Cancel and skip work mid-run. Running on the device since 2026-11-15.

### Build settings

Site details stay out of this public repo. The router's address and the Wi-Fi
credentials live in `firmware/secrets.ini`, which is gitignored and which
`platformio.ini` reads through `extra_configs`. Each machine that builds
firmware needs its own copy. Start from the example and fill in `ntp_server`
(and the Wi-Fi keys, only for the `wifi` environment):

```bash
cp secrets.example.ini secrets.ini
```

A build without the file still succeeds, with empty values, so CI needs
nothing. A build with no `ntp_server` prints a loud warning; do not OTA that
image to the installed device. The values are compiled in, so after changing
one, rebuild and OTA.

### Router NTP

The firmware asks the LAN router first, then `pool.ntp.org`, then
`time.google.com`. The router's address is a build setting, kept out of git
in `firmware/secrets.ini` (see [Build settings](#build-settings)). Without it
the build warns and uses only public servers
(`pool.ntp.org`, `time.google.com`, `time.cloudflare.com`), so a reboot during
a WAN outage would leave the device with no valid time. The router's NTP server was enabled on
2026-11-15, so the ESP gets the time after a reboot even during a WAN outage,
because the router keeps serving its own clock. lwIP sticks with whichever
server answered last, so after a router outage it may stay on a fallback until
the next reboot. For reference, the RouterOS 7 setup was:

```
/system ntp client set enabled=yes servers=pool.ntp.org
/system ntp server set enabled=yes
```

Check it from the Mac with `sntp -d <router-ip>` (look for a stratum), and
in `/api/status` the router shows `"reachable": true`.

## Layout rule

`lib/` is pure C++ with no Arduino dependency: frame building, pulse encoding,
validation, decisions. `src/` is the I/O edges: pins, NVS, network, the web
server. Host tests in `test/` link `lib/` only, so anything worth testing goes
in `lib/`.

```
pio test -e native
```

## Target

Waveshare ESP32-S3-ETH. There is no upstream PlatformIO board definition for this
board in either the official `platform-espressif32` or the pioarduino fork, so one
is vendored in `boards/`.

- ESP32-S3R8: 16 MB flash, 8 MB octal PSRAM (`qio_opi`), 240 MHz, dual core
- W5500 Ethernet over SPI3 — CS 14, INT 10, RST 9, MISO 12, MOSI 11, SCLK 13
- Hunter SmartPort / REM line on GPIO 18
- Native USB CDC (VID/PID `0x303a`/`0x1001`) — no host driver needed
- PoE powered

Because GPIO 9 is the W5500 reset, the Arduino default I²C pins (8/9) are not
usable as-is; any I²C peripheral needs remapping.

## Environments

| Env | Purpose |
|---|---|
| `ethernet` | Default build. Wired. |
| `wifi` | Fallback build. |
| `native` | Host unit tests (Unity). Builds `lib/` and `test/` only. |

## Toolchain

PlatformIO Core, installed per machine rather than per project:

```
uv tool install platformio
```

Core 6.2.0 or newer — earlier versions fail under `uv`/`pipx`-managed installs
when fetching Python-based tool packages. Reproducibility comes from pinning the
platform version in `platformio.ini`, not from the Core version.

No VS Code requirement. `pio run -t compiledb` emits `compile_commands.json` for
clangd.

## Files

| Path | Purpose |
|---|---|
| `platformio.ini` | Pinned platform, two device envs plus `native`, partition table |
| `partitions_ota.csv` | Dual 4 MB app slots, NVS, 7 MB LittleFS, coredump |
| `boards/` | Vendored board definition — no upstream equivalent exists |
| `include/config.h` | Pin map, timeouts, boot-guard threshold |
| `lib/hunter/` | Zone/duration bounds, SmartPort frame builder, pulse encoder |
| `lib/zone_control/` | Zone command logic and the HTTP-shaped request/response layer |
| `lib/kv_store/` | Storage interface, plus the in-memory fake for tests |
| `lib/boot_counter/` | Boot-guard decision logic |
| `src/hunter_bus.cpp` | REM pin and the FreeRTOS task that plays frames onto it |
| `src/nvs_kv_store.cpp` | `KvStore` on NVS, behind a mutex |
| `lib/settings/` | Settings model, day patterns, validation, JSON shapes, NVS store, route logic |
| `lib/api/` | Shared response type for host-tested route logic |
| `src/settings_service.cpp` | The device's settings store behind a mutex |
| `lib/sequencer/` | Run queue (stop-pad-start, cancel, skip), daily trigger, checkpoint and recovery |
| `src/sequencer_service.cpp` | The device's sequencer behind a mutex; trigger, checkpoint saves, recovery |
| `lib/timekeeping/` | Time validity policy, local time and DST, ISO formatting |
| `src/wallclock.cpp` | SNTP start-up and sync tracking |
| `src/net.cpp` | Ethernet or WiFi bring-up, selected at compile time |
| `src/boot_guard.cpp` | OTA slot revert, driven by `BootCounter` |
| `src/web.cpp` | Routes: status, zone start/stop, firmware upload |
| `src/main.cpp` | Startup order and the link-check loop |
| `test/` | Unity host tests, one directory per suite |
