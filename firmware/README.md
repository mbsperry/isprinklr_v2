# firmware

ESP32-S3 firmware. PlatformIO project.

Nothing here yet — see [`../docs/migration-plan.md`](../docs/migration-plan.md).
Phase 0 (partition table, OTA endpoint, pinned platform, vendored board
definition) lands here first.

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

## Planned environments

| Env | Purpose |
|---|---|
| `ethernet` | Default build. Wired. |
| `wifi` | Fallback build. |
| `native` | Host unit tests (Unity). No hardware. |

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
