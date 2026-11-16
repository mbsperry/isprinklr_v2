# Phase 1 hardware test

First run of the v2 Hunter driver against the real Pro-C. Everything here ships
over OTA and is driven from the Mac; only step 8 needs the basement.

**Status: run 2026-11-15, passed.** Results are under [Record](#record) at the
end. The driver is also host-tested (frames byte-identical to v1's, pulse timing
asserted).

The system is winterized, so a commanded zone clicks a dry valve and nothing
waters. Stand near the Pro-C (or have its display in view) for steps 2–7; the
display is the only way to see what it received, because the SmartPort line is
write-only.

Command blocks contain no `#` lines, so they paste cleanly into zsh.

## What to watch for

The one serious failure mode is a frame the Pro-C decodes as something other
than what was sent: the wrong zone, or a different duration. v2 sends exactly
v1's bytes, so the remaining risk is timing. v2 sends each bit with interrupts
off (v1 did not), and plays the frame from its own task instead of the web
handler. If the Pro-C shows the wrong zone or duration at any step, stop and
send a stop for every installed zone.

## 1. Deploy

```bash
export PATH="$HOME/.local/bin:$PATH"
cd firmware
pio test -e native
pio run -e ethernet
curl -F "firmware=@.pio/build/ethernet/firmware.bin" http://$ISPRINKLR_DEVICE/api/update
sleep 20
curl -s http://$ISPRINKLR_DEVICE/api/status | jq '.firmware_version, .phase, .boot.healthy, .hunter_bus, .zones'
```

Expect `0.2.0-phase1`, phase `"1"`, `healthy: true`, `hunter_bus.ready: true`,
`frames_sent: 0`, and `zones.commanded_zone: null`.

Check the Pro-C display now too. Driving GPIO 18 at boot must not have started
anything.

## 2. Start zone 1 for 2 minutes

```bash
curl -s -H 'Content-Type: application/json' -d '{"zone":1,"duration_minutes":2}' http://$ISPRINKLR_DEVICE/api/zone/start | jq
sleep 2
curl -s http://$ISPRINKLR_DEVICE/api/status | jq '.hunter_bus, .zones'
```

Expect HTTP 202 with `"ok": true`. Within a second or so the Pro-C should show
zone 1 running with about 2 minutes left. In status, `frames_sent: 1` and
`last_frame_us` close to 645250 (the nominal frame length; a few ms over is
normal).

## 3. Stop it

```bash
curl -s -H 'Content-Type: application/json' -d '{}' http://$ISPRINKLR_DEVICE/api/zone/stop | jq
```

Expect 202 naming zone 1, and the Pro-C back to idle.

## 4. A zone above 12

Zones 1–12 and 13–48 use different header bits. Repeat steps 2–3 with the
highest zone the Pro-C actually has, if that is 13 or more. If it has 12 or
fewer, this path stays untested.

```bash
curl -s -H 'Content-Type: application/json' -d '{"zone":13,"duration_minutes":2}' http://$ISPRINKLR_DEVICE/api/zone/start | jq
```

```bash
curl -s -H 'Content-Type: application/json' -d '{}' http://$ISPRINKLR_DEVICE/api/zone/stop | jq
```

## 5. Let a run end on its own

The safety property everything depends on: the Pro-C ends a run by itself.

```bash
curl -s -H 'Content-Type: application/json' -d '{"zone":1,"duration_minutes":1}' http://$ISPRINKLR_DEVICE/api/zone/start | jq
sleep 75
curl -s http://$ISPRINKLR_DEVICE/api/status | jq '.zones'
```

The Pro-C should go idle after a minute with no stop sent, and `commanded_zone`
should be back to `null`.

## 6. Start one zone while another runs

Unverified behaviour that Phase 4's sequencing depends on: does a start for zone
B replace a running zone A, or queue behind it, or run both?

```bash
curl -s -H 'Content-Type: application/json' -d '{"zone":1,"duration_minutes":3}' http://$ISPRINKLR_DEVICE/api/zone/start | jq
sleep 10
curl -s -H 'Content-Type: application/json' -d '{"zone":2,"duration_minutes":3}' http://$ISPRINKLR_DEVICE/api/zone/start | jq
```

Note what the Pro-C shows, then stop both:

```bash
curl -s -H 'Content-Type: application/json' -d '{"zone":1}' http://$ISPRINKLR_DEVICE/api/zone/stop | jq
curl -s -H 'Content-Type: application/json' -d '{"zone":2}' http://$ISPRINKLR_DEVICE/api/zone/stop | jq
```

## 7. Bad requests are refused without touching the bus

```bash
curl -s -H 'Content-Type: application/json' -d '{"zone":1,"duration_minutes":0}' http://$ISPRINKLR_DEVICE/api/zone/start | jq
curl -s -H 'Content-Type: application/json' -d '{"zone":1,"duration_seconds":600}' http://$ISPRINKLR_DEVICE/api/zone/start | jq
curl -s -d 'zone=1' http://$ISPRINKLR_DEVICE/api/zone/start | jq
curl -s http://$ISPRINKLR_DEVICE/api/status | jq '.hunter_bus.frames_sent'
```

Expect 400, 400, 415, and `frames_sent` unchanged from before this step.

## 7b. Zone commands and OTA exclude each other

Start a zone, then immediately try an upload while the frame is still going
out. Run this from `firmware/` after step 1's build.

```bash
curl -s -H 'Content-Type: application/json' -d '{"zone":1,"duration_minutes":1}' http://$ISPRINKLR_DEVICE/api/zone/start ; curl -s -F "firmware=@.pio/build/ethernet/firmware.bin" http://$ISPRINKLR_DEVICE/api/update | jq
```

Expect the upload to return 503 `"error": "busy"` with nothing flashed. Timing
dependent: if the frame finished first, the upload succeeds and the device
reboots, which is also fine. Then stop zone 1.

```bash
curl -s -H 'Content-Type: application/json' -d '{"zone":1}' http://$ISPRINKLR_DEVICE/api/zone/stop | jq
```

## 8. Optional: ESP power loss mid-run

Needs the basement. The board is PoE powered, so unplugging its Ethernet cable
cuts power.

```bash
curl -s -H 'Content-Type: application/json' -d '{"zone":1,"duration_minutes":2}' http://$ISPRINKLR_DEVICE/api/zone/start | jq
```

Unplug the cable while the Pro-C shows zone 1 running. The Pro-C should carry
on and end the run at 2 minutes. Plug back in, and after it boots confirm status
shows `commanded_zone: null` (the ESP forgot, as designed) and the Pro-C did not
restart anything at boot.

## Record

Run 2026-11-15 with firmware `0.2.0-phase1` (merged PR #3), Matt watching the
Pro-C display, commands sent from the Mac. 10 zones installed.

| Step | Result |
|---|---|
| 1. Deploy | OTA accepted; booted healthy on `app0`; bus ready, 0 frames. Display idle after the reboot. |
| 2. Start zone 1, 2 min | Display: zone 1, 2 minutes. Frame 646.8 ms (nominal 645.25). |
| 3. Stop | Display idle. |
| 4. Zone above 12 | **Not tested**: only 10 zones installed. The 13–48 header bits stay verified only against v1's bytes. |
| 5. Run ends on its own | 1-minute run went idle at about one minute with no stop sent. |
| 6. Start B while A runs | **Zone 2 replaced zone 1.** Display showed zone 2 for 3 minutes; no queuing. Recorded in open decision 6. |
| 7. Bad requests | 400 for zero minutes, `duration_seconds`, zone 49, string zone; 415 without JSON content type; 409 for a bare stop when idle. `frames_sent` unchanged. |
| 7b. Zone vs OTA | Upload sent while a start frame was going out: 503 `busy`, nothing flashed. |
| 8. ESP power loss mid-run | Started zone 1 for 2 min, then unplugged the PoE cable. The Pro-C kept running zone 1 and went idle on its own. The ESP came back with `reset_reason: power_on`, healthy, `commanded_zone: null`, and `frames_sent: 0`, so it sent nothing at boot. |

**Pro-C behaviour when the ESP loses power (observed by Matt, 2026-11-15).**
The Pro-C's watering icon blinks steadily while a zone runs with the ESP
powered. With the ESP unplugged mid-run, it blinks irregularly. On plugging the
ESP back in, the Pro-C briefly showed `ERR`, went back to blinking steadily, and
finished the zone normally. Not understood yet. A likely explanation: the REM pin
is wired straight to GPIO 18 with no resistor. Unpowered, the ESP stops holding
the line LOW and its protection diodes load it, so the Pro-C sees an undriven,
noisy REM line. At power-up the boot transient on the pin reaches the Pro-C as
an invalid frame, so it shows `ERR` and ignores it, which is the outcome we
wanted. Nothing was misread as a command. **Repeat step 8 in spring with the
water on**, and watch that the valve really closes on time. If the irregular
blink ever matters, a series resistor (around 1 kΩ) between GPIO 18 and REM would
limit current into an unpowered pin. That needs testing against the Pro-C before
it's fitted.

**Frame stretch under network load.** Frames sent while a firmware upload was
streaming in took 778–804 ms instead of ~646 ms. Frames at other times were
646–647 ms. The stretch has to fall outside the interrupts-off bit pairs: in the
yielding reset pulse or the gaps between bits. A deliberate check (zone 3,
5 minutes, sent under the same load) decoded exactly on the Pro-C, so the
stretch is tolerated. Worth keeping in mind if a frame is ever misread; the fix
would be keeping interrupts off across the whole data section, or RMT.
