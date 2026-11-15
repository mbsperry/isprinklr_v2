# ESPHome sprinkler component: lessons for iSprinklr v2

Read-only audit, 2026-11-15. Source: `esphome/components/sprinkler` at esphome `dev` commit 373224c (sprinkler.h, sprinkler.cpp, `__init__.py`). Compared against isprinklr_v2 branch `claude/project-thread-hu55bd` (commit 0cc65da, PR 3; `ZoneController`, `hunter_bus` and `hardware-test-phase1.md` live there, not yet on main) and `docs/`. Nothing here was compiled or tested; no code was changed.

## How ESPHome does it, in brief

- **One owner.** Every action (start cycle, single valve, queue, next/prev, pause, resume) becomes a request to one state machine (`fsm_request_` / `fsm_kick_`). Nothing toggles a valve directly. Requests that arrive mid-transition sit in a one-slot `next_req_` and are picked up at the next transition, not rejected.
- **States:** `IDLE → ACTIVE → STOPPING → IDLE`, with `STARTING` only when a valve-open delay is configured. One state-machine timer drives transitions; a per-valve "operator" handles pump/valve ordering.
- **The ESP holds the timer.** A valve is a GPIO switch held on until the ESP turns it off. There is no hardware backstop.
- **Completion vs interruption.** A valve is marked complete only if its timer expired naturally. If it was cut short, it is logged as interrupted and not marked complete. Each run carries its origin: `USER`, `CYCLE` or `QUEUE`.
- **Standby.** A single switch that every start path checks (`start_full_cycle`, `start_single_valve`, `start_from_queue`, `resume`, `next/previous`), refusing with a warning log. A multiplier of 0 is refused the same way rather than running zero-length zones.
- **Manual preempts schedule.** `start_single_valve` turns off auto-advance, which cancels the rest of the cycle. `pause()` / `resume()` exist as the explicit alternative: pause stores the valve and remaining seconds, resume re-requests them.
- **Cycle/soak** is only approximated: `repeat` runs the whole cycle N times, and `divider` sets multiplier to 1/N plus repeat N. There is no per-zone soak timer.
- **Reboot and OTA.** `setup()` forces all valves and pumps off. Runtime state (active valve, queue, paused valve, repeat count) is RAM-only and lost. Config values (run durations, multiplier, repeat, enable switches) persist via restore; the controller switches default to `RESTORE_DEFAULT_OFF`, so a reboot never resumes watering. The component has no `on_shutdown`, so an OTA mid-run just reboots into "all off".
- **Failure handling:** essentially none. A switch `turn_on()` is assumed to work; there is no feedback path.
- **Time:** run timing uses rollover-safe `now - start` on `millis()`, same as our `ZoneController`.

## Lessons worth importing, by priority

### 1. Route every zone command through the sequencer (Phase 4, high)
ESPHome's single-owner FSM is its best idea. Today `/api/zone/start` calls `ZoneController` directly, and `zone_controller.h` already warns the Phase 4 scheduler will be a second caller. Make the Phase 4 sequencer the only thing that calls `ZoneController`; manual start/stop become requests to it. This resolves the thread-safety note by construction and gives one place to apply the gates in lesson 3. Requests that arrive while a stop frame or a padding gap is in flight should be held and applied at the next transition, like `next_req_`, rather than rejected.
**Lands in:** `migration-plan.md` Phase 4 bullet "Sequencing state machine"; later `web.cpp` zone routes.

### 2. Block OTA and restart while a sequence is running (Phase 4, high)
ESPHome reboots into "all off" because it can, since nothing persists. We have chosen to persist and reconcile, and an OTA mid-sequence is a reboot mid-sequence that we trigger ourselves. Extend design-review item 1 (409 while the bus is busy) to: `/api/update` and `/api/restart` return 409 while the sequencer is not idle, unless `?force=1`. Cheap, and it keeps the reconcile path for real crashes.
**Lands in:** `web.cpp` update/restart handlers; `migration-plan.md` Phase 4.

### 3. One "may we water?" gate, checked on every start path (Phase 2–4, high)
Copy the standby pattern: a single function checked by schedule start, manual start, and reconcile-resume, that returns a reason (`schedule_off`, `time_invalid`, `standby`, `zero_adjust`) and logs it. Our plan has `time_valid` and an on/off flag but does not say whether manual starts honour them; decide that explicitly (likely: manual ignores `time_valid` and the schedule flag, honours standby). Stop must never be gated.
**Lands in:** `architecture.md` Execution; Phase 2 `time_valid`, Phase 3 on/off flag.

### 4. Record completed vs interrupted, and origin, in the checkpoint (Phase 4, medium-high)
ESPHome marks a zone complete only if it ran its full time, and tags every run with its origin. Add `origin` (schedule/manual) and per-zone `completed` / `interrupted` to the Phase 4 checkpoint and to status. That is exactly what open decision 1 needs ("a partial cycle should be visible") and what reconcile needs to know which zones are left.
**Lands in:** Phase 4 checkpoint `{schedule, zone_index, started_epoch, expected_end}`; status response.

### 5. Settle "manual during a schedule" as an open decision (medium)
ESPHome's answer: manual start cancels the rest of the cycle; pause/resume is opt-in.

**Matt's direction (2026-11-15):** a schedule run is a queue. Cancelling a run is required; adding a zone to a running queue is wanted if it stays cheap. Proposed shape:
- The sequencer owns one bounded queue of `{zone, duration_minutes, origin}` (cap 48). A schedule run fills it; a manual start appends to the end instead of preempting. ESPHome's `queue_valve` works the same way.
- **Cancel** sends stop for the running zone, clears the queue, and marks the rest interrupted (lesson 4). Never gated.
- **Skip** (optional) stops the running zone and advances.
- The real cost of appending is in the checkpoint: it has to store the remaining queue entries, not `{schedule, zone_index}`. About 48 × 3 bytes in NVS, written at each transition.
**Lands in:** `open-decisions.md` new item; blocks Phase 4.

### 6. On reboot, put the Pro-C in a known state on purpose (Phase 4, medium)
ESPHome forces everything off in `setup()`. We cannot do that blindly, since the line is write-only and stop frames are per-zone and about 0.65 s each. But the checkpoint names the zone in flight, so reconcile can act on it: under "skip", send a stop for that zone; under "resume", do **not** re-start it (the Pro-C is still running it until `expected_end`), wait for `expected_end`, then continue with the next zone. Do not port "stop all 48 zones on boot".
**Lands in:** Phase 4 reconcile; open decision 1 options.

### 7. Explicit stop before the next start, independent of hardware-test step 6 (medium)
ESPHome makes overlap explicit (`valve_overlap` vs `valve_open_delay`). Our ESP sequencing clock and the Pro-C's countdown are two clocks, so zone B may be commanded while A is still finishing. Whatever step 6 shows, the sequencer should send stop(A), then pad, then start(B). That costs one extra frame and removes the dependence on undocumented Pro-C behaviour.
**Lands in:** Phase 4 "inter-zone padding"; `hardware-test-phase1.md` step 6 still worth running.

### 8. Design the failure path ESPHome does not have (Phase 4, medium)
ESPHome assumes switching always works. Our `enqueue()` can fail (`kBusBusy`), and `run_` is set on queue rather than on send (review item 7). Decide: on a failed enqueue mid-sequence, retry once after `hunter_bus::idle()`, then abort the sequence, mark remaining zones interrupted, and log. Consider moving the "commanded" timestamp to the frame-sent callback so `expected_end` is anchored to the wire, not the queue.
**Lands in:** `ZoneController::start` / `hunter_bus`; Phase 4 sequencer.

### 9. Report time remaining for the whole run (low)
ESPHome exposes `time_remaining_current_operation()` and total cycle time. Add `sequence_remaining_s` / `expected_end` to status; it is nearly free once lesson 4 exists.
**Lands in:** `api-contract.md` status.

### 10. Seasonal adjust as one number, later (low, defer)
ESPHome's multiplier is a cheap global seasonal adjust, and it refuses 0. If we add one, clamp the result to `hunter::kMaxRunMinutes`, round to whole minutes with a minimum of 1, and treat 0 as "don't water" through the gate in lesson 3. Not needed for spring.
**Lands in:** Phase 3 config, if ever.

## Deliberately skip

- **ESP-held valve timers / on-off switches.** The Pro-C holding a bounded duration is strictly safer; ESPHome's model leaves a valve open if the ESP hangs. Keep the invariant.
- **Pump and master-valve handling, start/stop delays, valve-open delay, overlap.** The Pro-C drives its own P/MV output per zone. Nothing to sequence on our side.
- **Multi-controller pump sharing (`add_controller`, `pump_in_use`).** One controller.
- **Seconds-based durations.** The wire is whole minutes; keep schedule durations in minutes so nothing is truncated (v1's units bug).
- **A 100-entry manual queue, next/previous with selection delay, reverse order, per-zone enable switches.** UI conveniences; our schedule triples already say which zones run.
- **Repeat/divider as cycle-and-soak.** Integer-minute rounding makes it lossy for us. If soak is wanted later, design a real per-zone soak.
- **Throwing away runtime state on reboot.** ESPHome's simplicity here is exactly the "skip" option of open decision 1 with no visibility. We already chose to checkpoint; keep that.
