# Phase 0 runbook

Repartition the ESP32 and install OTA capability. One USB session; everything
after this goes over the network.

**The firmware in `firmware/` has never been compiled.** Step 1 is a build, and it
happens at your desk, not in the basement.

---

## Before you go downstairs

### 1. Toolchain

```bash
uv tool install platformio          # 6.2.0+ required under uv
cd firmware
pio run -e ethernet
```

The first build downloads ~1–2 GB (Xtensa toolchain, Arduino framework,
libraries). Do this on wifi at your desk.

Expect to fix something. Points of risk, in rough order of likelihood:

- **Platform.** Pinned to pioarduino `55.03.311` (Arduino-ESP32 3.3.11). The
  official `espressif32@7.1.3` ships Arduino 2.0.17 and cannot compile the W5500
  call. Do not go back to a floating URL.
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

```bash
pio run -e ethernet -t erase
```

Required: partition offsets move, so stale NVS would be read at the wrong
address. Nothing of value is lost — Phase 0 stores no schedules, and WiFi
credentials come from build flags.

### 5. Flash

```bash
pio run -e ethernet -t upload
pio device monitor
```

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
curl -s http://<ip>/api/status | jq
```

Check:

- `boot.running_partition` is `app0`
- `boot.update_partition` is `app1`
- `boot.healthy` is `true`, `boot.failed_boots` is `0`
- `network.up` is `true`, with a sane IP, gateway, and link speed
- `time_valid` is `false` and `schedule_source` is `"none"` — correct for Phase 0

### 7. OTA round trip

Change something visible — bump `FW_VERSION` in `platformio.ini` — then:

```bash
pio run -e ethernet
curl -F "firmware=@.pio/build/ethernet/firmware.bin" http://<ip>/api/update
```

Expect `{"ok":true,"message":"update accepted, rebooting"}`, then the device
reboots. Confirm the new version:

```bash
sleep 20
curl -s http://<ip>/api/status | jq '.firmware_version, .boot.running_partition'
```

`running_partition` should now read `app1`.

### 8. Do step 7 again

This is the step people skip. It exercises `app1` → `app0` and proves both slots
work. A layout where only one direction works looks fine until the day it
doesn't.

### 9. Optional: prove the boot guard

Only worth doing while you have USB access, because it is the one test that can
leave the device unreachable.

Build an image that reaches `web::begin()` but never calls `markHealthy()` — e.g.
temporarily comment out both call sites in `main.cpp` — push it over OTA, then
power-cycle three times. On the third boot the log should show:

```
[boot] 3 consecutive unhealthy boots
[boot] reverting to app0 (0.1.0-phase0)
```

and come back on the previous image. Then re-flash the real firmware.

If you skip this, the guard is untested code. Your call — you have USB access, so
the downside is a trip downstairs rather than a dead controller.

### 10. Leave

Reinstall, confirm `/api/status` answers from upstairs, and note the IP.

---

## Afterwards

Deployment for the rest of the winter:

```bash
cd firmware
pio run -e ethernet
curl -F "firmware=@.pio/build/ethernet/firmware.bin" http://<ip>/api/update
```

If an update ever bricks reachability, the guard reverts after three failed
boots. If that also fails, it's a USB trip — which is why Phase 0 is as small as
it is.

## What Phase 0 deliberately does not do

- No zone control. The Pro-C is wired but untouched. v1's firmware remains
  flashable if you need to drive a zone before Phase 2.
- No SNTP, no NVS schedule store, no UDP logging, no LittleFS content. The
  `littlefs` partition is created but unused.
- No authentication on `/api/update`. Consistent with the rest of the system;
  the router VPN is the boundary. Anyone on the LAN can flash this device.
