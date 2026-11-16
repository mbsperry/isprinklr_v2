# Phase 0 runbook

Repartition the ESP32 and install OTA capability. One USB session; everything
after this goes over the network.

**Status: completed 2026-11-14.** Flashed over USB, then verified on hardware:
W5500 up at 100 Mbps full duplex, PSRAM detected (~8.6 MB free heap), OTA in both
directions (app0 → app1 → app0), and the boot guard reverting a never-healthy
image after three boots. The device has a static address (a reservation
on the router). Commands below use `$ISPRINKLR_DEVICE` for it, so set
that in the shell first, for example in `~/.zshrc`. For day-to-day updates, skip to
[Routine OTA update](#routine-ota-update).

The steps below are kept as the record of the install and as the procedure if
the device ever needs a USB reflash.

Command blocks here contain no `#` comment lines, so they paste cleanly into
interactive zsh (which treats `#` as a command unless `setopt interactivecomments`
is set).

---

## Before you go downstairs

### 1. Toolchain

PlatformIO Core 6.2.0+ is required under uv. uv installs `pio` into
`~/.local/bin`, which is not on the default macOS PATH:

```bash
uv tool install platformio
export PATH="$HOME/.local/bin:$PATH"
cd firmware
pio run -e ethernet
```

Add the `export` line to `~/.zshrc` to make it permanent.

The first build downloads ~1–2 GB (Xtensa toolchain, Arduino framework,
libraries). Do this on wifi at your desk.

Expect to fix something. Points of risk, in rough order of likelihood:

- **Platform.** Pinned to pioarduino `55.03.311` (Arduino-ESP32 3.3.11, ESP-IDF
  5.5.5). The official registry `espressif32@7.1.3` was the original plan, but it
  ships Arduino 2.0.17 (IDF 4.4), where `ETH_PHY_W5500` and `SPI3_HOST` do not
  exist, so `net.cpp` fails to compile. Do not go back to a floating URL.
- **Library versions.** Pinned exactly (AsyncTCP 3.6.1, ESPAsyncWebServer 3.12.1,
  ArduinoJson 7.4.3). `pio pkg outdated` shows what's newer.
- CI (`.github/workflows/firmware.yml`) builds both envs on every firmware change,
  so a green check on the branch means it compiles.

Record the flash figure the build prints. The app slot is 4 MB; v1's binary was
built for a ~3 MB single slot, so there should be ample room.

### 2. Confirm the partition table took effect

```bash
pio run -e ethernet -t envdump | grep -i partition
```

Should name `partitions_ota.csv`, not `huge_app.csv`. The board JSON no longer
declares a partition table, so `platformio.ini` is the only source.

### 3. Physical prep

- **USB-C data cable**, tested on some other device. A charge-only cable presents
  as "no device found" and wastes an hour.
- Keep a **known-good v1 firmware binary** on the laptop as a rollback.
- Know the recovery gesture: **hold BOOT, tap RESET** to force download mode.
  Native-USB S3 boards occasionally need it.
- If the enclosure allows, **route a USB-C pigtail** somewhere reachable. Cheap
  now, annoying to retrofit.

---

## At the device

### 4. Erase

Find the serial port first (it appears as `/dev/cu.usbmodem…`), then erase:

```bash
pio device list
PORT=/dev/cu.usbmodemXXXX
pio run -e ethernet -t erase --upload-port $PORT
```

If erase or upload cannot connect, hold BOOT, tap RESET, release BOOT, and rerun.

Required: partition offsets move, so stale NVS would be read at the wrong
address. Nothing of value is lost — Phase 0 stores no schedules, and WiFi
credentials come from build flags.

### 5. Flash

```bash
pio run -e ethernet -t upload --upload-port $PORT
pio device monitor -p $PORT
```

The board uses native USB CDC, so the serial port drops on every reset and the
monitor reconnects a moment later. Lines printed in the first ~100 ms after a
reset are usually missed. If the monitor sits silent after upload, tap RESET.

Expect, in order:

```
[main] iSprinklr v2 firmware 0.1.0-phase0 (phase 0)
[boot] attempt 1 of 3 before revert
[net] starting Ethernet (W5500)
[net] up: Ethernet 192.168.x.x
[web] listening on :80
[boot] marked healthy
[main] ready at http://192.168.x.x/api/status
```

`[boot] marked healthy` is the line that matters. Without it the boot guard will
revert after three boots.

### 6. Verify status

```bash
curl -s http://$ISPRINKLR_DEVICE/api/status | jq
```

Check:

- `boot.running_partition` is `app0`
- `boot.update_partition` is `app1`
- `boot.healthy` is `true`, `boot.failed_boots` is `0`
- `network.up` is `true`, with a sane IP, gateway, and link speed
- `time_valid` is `false` and `schedule_source` is `"none"` — correct for Phase 0

### 7. OTA round trip

Change something visible by bumping `FW_VERSION` in `platformio.ini`, then build
and upload. Confirm the edit actually landed: a changed build flag forces a full
rebuild (15 s or more), so a 3-second build means the old version was rebuilt.

```bash
sed -i '' 's/0\.1\.0-phase0/0.1.1-phase0/' platformio.ini
grep FW_VERSION platformio.ini
pio run -e ethernet
curl -F "firmware=@.pio/build/ethernet/firmware.bin" http://$ISPRINKLR_DEVICE/api/update
```

Expect `{"ok":true,"message":"update accepted, rebooting"}`, then the device
reboots. Confirm the new version:

```bash
sleep 20
curl -s http://$ISPRINKLR_DEVICE/api/status | jq '.firmware_version, .boot.running_partition'
```

`running_partition` should now read `app1`.

### 8. Do step 7 again

This is the step people skip. It exercises `app1` → `app0` and proves both slots
work. A layout where only one direction works looks fine until the day it
doesn't.

### 9. Optional: prove the boot guard

Only worth doing while you have USB access, because it is the one test that can
leave the device unreachable.

Build an image that reaches `web::begin()` but never calls `markHealthy()`, push
it over OTA, and watch the monitor:

```bash
sed -i '' 's/boot_guard::markHealthy();/\/\/ &/' src/main.cpp
sed -i '' 's/0\.1\.1-phase0/0.1.2-noguard/' platformio.ini
pio run -e ethernet
curl -F "firmware=@.pio/build/ethernet/firmware.bin" http://$ISPRINKLR_DEVICE/api/update
pio device monitor -p $PORT
```

The reboot after the upload is unhealthy boot 1. Tap RESET three times, letting it
boot fully each time; on the third reset the guard switches slots and restarts
(`rst:0xc (RTC_SW_CPU_RST)`), and the previous image comes up healthy. The
`[boot] reverting to …` line is usually lost to the USB reconnect described in
step 5, so confirm with `/api/status` instead. Then restore the real firmware:

```bash
git checkout src/main.cpp platformio.ini
pio run -e ethernet
curl -F "firmware=@.pio/build/ethernet/firmware.bin" http://$ISPRINKLR_DEVICE/api/update
```

Result on 2026-11-14: `0.1.2-noguard` on app1 reverted to `0.1.1-phase0` on app0
as designed.

If you skip this, the guard is untested code. Your call — you have USB access, so
the downside is a trip downstairs rather than a dead controller.

### 10. Leave

Reinstall, confirm `/api/status` answers from upstairs, and note the IP. The
device has a static reservation on the router.

---

## Routine OTA update

How every firmware change ships from now on. From the repo root, on a machine on
the LAN or the router VPN:

```bash
export PATH="$HOME/.local/bin:$PATH"
cd firmware
pio run -e ethernet
curl -F "firmware=@.pio/build/ethernet/firmware.bin" http://$ISPRINKLR_DEVICE/api/update
sleep 20
curl -s http://$ISPRINKLR_DEVICE/api/status | jq '.firmware_version, .boot'
```

Expect `{"ok":true,...}` from the upload, then the new `firmware_version`,
`healthy: true`, and `running_partition` flipped to the other slot. The slots
alternate on every update, so either `app0` or `app1` is normal.

If the upload returns `"ok":false`, the running firmware is untouched; read the
`error` field. If the device stops answering after an update, wait: the boot guard
reverts after three unhealthy boots, and an unreachable image usually also fails
its network check quickly, but a power-cycle or two may be needed to get through
three boots. If that also fails, it is a USB trip — which is why Phase 0 is as
small as it is.

## Lessons from the install

- `espressif32@7.1.3` from the PlatformIO registry is Arduino-ESP32 2.0.17, not
  3.x. The firmware needs pioarduino; see the platform note in step 1.
- uv puts `pio` in `~/.local/bin`, not on the default PATH.
- Native USB CDC drops the serial port on every reset, so the first log lines
  after a reset are usually missed. Use `/api/status` as the source of truth.
- Check that a `FW_VERSION` bump actually saved before uploading; a fast build
  means nothing changed.
- Hardware facts confirmed: W5500 on SPI3 with the vendored pin map, 100 Mbps full
  duplex; `qio_opi` memory type correct (8 MB PSRAM visible); chip revision 2.
- Flash use at Phase 0: ethernet build 14.6% of the 4 MB slot, RAM 9.3%.

## What Phase 0 deliberately does not do

- No zone control. The Pro-C is wired but untouched. v1's firmware remains
  flashable if you need to drive a zone before Phase 2.
- No SNTP, no NVS schedule store, no UDP logging, no LittleFS content. The
  `littlefs` partition is created but unused.
- No authentication on `/api/update`. Consistent with the rest of the system;
  the router VPN is the boundary. Anyone on the LAN can flash this device.
