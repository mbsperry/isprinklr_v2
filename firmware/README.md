# firmware

ESP32-S3 firmware. PlatformIO project.

**Phase 0 is installed (2026-11-14)** at 192.168.88.54. Ship changes over the
network with the [routine OTA update](../docs/phase0-runbook.md#routine-ota-update);
CI runs the host tests and builds both device envs on every firmware change.

**Phase 1** adds the Hunter SmartPort driver, manual zone start/stop over HTTP,
and host unit tests. It has not been run against the Pro-C yet; see
[`hardware-test-phase1.md`](../docs/hardware-test-phase1.md). No timekeeping or
schedule storage yet; those ship over OTA in Phases 2 and 3.

**Phase 2** adds SNTP time (US Pacific, automatic DST) and `time_valid` in
status. Running on the device since 2026-11-15.

### Router NTP

The firmware asks the MikroTik router (192.168.88.1) first, then
`pool.ntp.org`, then `time.google.com`. The router doesn't serve NTP today.
Enabling it lets the ESP get the time after a reboot during a WAN outage,
because the router keeps serving its own clock. On RouterOS 7, in the router's
terminal:

```
/system ntp client set enabled=yes servers=pool.ntp.org
/system ntp server set enabled=yes
```

RouterOS 6 needs the separate `ntp` package instead. Check it worked from the
Mac with `sntp 192.168.88.1`, then look for `"reachable": true` on the router in
`/api/status` within an hour.

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
| `lib/timekeeping/` | Time validity policy, local time and DST, ISO formatting |
| `src/wallclock.cpp` | SNTP start-up and sync tracking |
| `src/net.cpp` | Ethernet or WiFi bring-up, selected at compile time |
| `src/boot_guard.cpp` | OTA slot revert, driven by `BootCounter` |
| `src/web.cpp` | Routes: status, zone start/stop, firmware upload |
| `src/main.cpp` | Startup order and the link-check loop |
| `test/` | Unity host tests, one directory per suite |
