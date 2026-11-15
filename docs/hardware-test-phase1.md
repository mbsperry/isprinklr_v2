# Phase 1 hardware test

First run of the v2 Hunter driver against the real Pro-C. Everything here ships
over OTA and is driven from the Mac; only step 8 needs the basement.

**Status: not yet run.** The driver is host-tested (frames byte-identical to
v1's, pulse timing asserted) and compiles for both device envs. Nothing has been
sent to the Pro-C from v2 yet.

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
curl -F "firmware=@.pio/build/ethernet/firmware.bin" http://192.168.88.54/api/update
sleep 20
curl -s http://192.168.88.54/api/status | jq '.firmware_version, .phase, .boot.healthy, .hunter_bus, .zones'
```

Expect `0.2.0-phase1`, phase `"1"`, `healthy: true`, `hunter_bus.ready: true`,
`frames_sent: 0`, and `zones.commanded_zone: null`.

Check the Pro-C display now too. Driving GPIO 18 at boot must not have started
anything.

## 2. Start zone 1 for 2 minutes

```bash
curl -s -H 'Content-Type: application/json' -d '{"zone":1,"duration_minutes":2}' http://192.168.88.54/api/zone/start | jq
sleep 2
curl -s http://192.168.88.54/api/status | jq '.hunter_bus, .zones'
```

Expect HTTP 202 with `"ok": true`. Within a second or so the Pro-C should show
zone 1 running with about 2 minutes left. In status, `frames_sent: 1` and
`last_frame_us` close to 645250 (the nominal frame length; a few ms over is
normal).

## 3. Stop it

```bash
curl -s -H 'Content-Type: application/json' -d '{}' http://192.168.88.54/api/zone/stop | jq
```

Expect 202 naming zone 1, and the Pro-C back to idle.

## 4. A zone above 12

Zones 1–12 and 13–48 use different header bits. Repeat steps 2–3 with the
highest zone the Pro-C actually has, if that is 13 or more. If it has 12 or
fewer, this path stays untested.

```bash
curl -s -H 'Content-Type: application/json' -d '{"zone":13,"duration_minutes":2}' http://192.168.88.54/api/zone/start | jq
```

```bash
curl -s -H 'Content-Type: application/json' -d '{}' http://192.168.88.54/api/zone/stop | jq
```

## 5. Let a run end on its own

The safety property everything depends on: the Pro-C ends a run by itself.

```bash
curl -s -H 'Content-Type: application/json' -d '{"zone":1,"duration_minutes":1}' http://192.168.88.54/api/zone/start | jq
sleep 75
curl -s http://192.168.88.54/api/status | jq '.zones'
```

The Pro-C should go idle after a minute with no stop sent, and `commanded_zone`
should be back to `null`.

## 6. Start one zone while another runs

Unverified behaviour that Phase 4's sequencing depends on: does a start for zone
B replace a running zone A, or queue behind it, or run both?

```bash
curl -s -H 'Content-Type: application/json' -d '{"zone":1,"duration_minutes":3}' http://192.168.88.54/api/zone/start | jq
sleep 10
curl -s -H 'Content-Type: application/json' -d '{"zone":2,"duration_minutes":3}' http://192.168.88.54/api/zone/start | jq
```

Note what the Pro-C shows, then stop both:

```bash
curl -s -H 'Content-Type: application/json' -d '{"zone":1}' http://192.168.88.54/api/zone/stop | jq
curl -s -H 'Content-Type: application/json' -d '{"zone":2}' http://192.168.88.54/api/zone/stop | jq
```

## 7. Bad requests are refused without touching the bus

```bash
curl -s -H 'Content-Type: application/json' -d '{"zone":1,"duration_minutes":0}' http://192.168.88.54/api/zone/start | jq
curl -s -H 'Content-Type: application/json' -d '{"zone":1,"duration_seconds":600}' http://192.168.88.54/api/zone/start | jq
curl -s -d 'zone=1' http://192.168.88.54/api/zone/start | jq
curl -s http://192.168.88.54/api/status | jq '.hunter_bus.frames_sent'
```

Expect 400, 400, 415, and `frames_sent` unchanged from before this step.

## 7b. Zone commands and OTA exclude each other

Start a zone, then immediately try an upload while the frame is still going
out. Run this from `firmware/` after step 1's build.

```bash
curl -s -H 'Content-Type: application/json' -d '{"zone":1,"duration_minutes":1}' http://192.168.88.54/api/zone/start ; curl -s -F "firmware=@.pio/build/ethernet/firmware.bin" http://192.168.88.54/api/update | jq
```

Expect the upload to return 503 `"error": "busy"` with nothing flashed. Timing
dependent: if the frame finished first, the upload succeeds and the device
reboots, which is also fine. Then stop zone 1.

```bash
curl -s -H 'Content-Type: application/json' -d '{"zone":1}' http://192.168.88.54/api/zone/stop | jq
```

## 8. Optional: ESP power loss mid-run

Needs the basement. The board is PoE powered, so unplugging its Ethernet cable
cuts power.

```bash
curl -s -H 'Content-Type: application/json' -d '{"zone":1,"duration_minutes":2}' http://192.168.88.54/api/zone/start | jq
```

Unplug the cable while the Pro-C shows zone 1 running. The Pro-C should carry
on and end the run at 2 minutes. Plug back in, and after it boots confirm status
shows `commanded_zone: null` (the ESP forgot, as designed) and the Pro-C did not
restart anything at boot.

## Record

Write the outcome of steps 4, 6 and 8 into this file, then flip the status line
at the top. Step 6's answer goes into the Phase 4 design.
