# Activity axis — design brief (staged for a separate implementer; not scheduled in the loop)

Decision, Jesse, 2026-09-19: SCALAR gets **no new values in `SystemMode`**. SAFE_MODE and NORMAL stay exactly as
PROVES built them (`Components/ModeManager`, ~600 lines, ~30 branch sites on those two names). CONOPS activities are a
**second, independent axis** held by a new small component, driven by uplinkable sequence files, RAM-only. This brief
states the results the component must produce and the existing pieces it reuses. Methods are the implementer's.

Read first: `CLAUDE.md` (Traps, Read discipline), `design/parameter-policy.md` (what is a parameter, a file, a
constant), `docs-site/dev-loop-findings.md` (grep `ModeManager`, `TaskGate`, `DetumbleManager`, `cmdSeq`).

## 1. The two axes and the one rule

| Axis | Values | Owner | Persisted | Answers |
|---|---|---|---|---|
| System mode | SAFE_MODE, NORMAL | ModeManager (unchanged) | yes (PersistedRecord) | "is the bus protecting itself?" |
| Activity | NONE, CALIBRATION, EXPERIMENT (extensible) | new `ActivityManager` | **no** — reboot lands in NONE | "what is the payload doing?" |

**The one rule:** while the system mode is SAFE_MODE the activity is NONE. Entering SAFE forces NONE; SET_ACTIVITY is
refused in SAFE. Nothing else couples the axes. ModeManager's own safe-mode behaviour (rails off, watchdog, sequence)
is untouched and still runs first.

An activity is **four pieces of data**: an entry sequence file, an exit sequence file, the set of activities it may be
entered from, and its name. All switching of tasks, rails and controllers happens **inside the sequence files**, using
commands that already exist. No task-gate mask port, no load-switch mask, no new port on any existing component.

## 2. Results (normative — tests are written from this section only)

- **R1 Enumeration.** `enum Activity: U8 { NONE = 0, CALIBRATION = 1, EXPERIMENT = 2 }` in `Components/ActivityTypes`
  (or the component's own fpp). Adding an activity = one enumerator + two sequence files + one transition-table row.
- **R2 Command.** `SET_ACTIVITY(activity: Activity)`, sync. Allowed transitions: NONE → CALIBRATION, NONE → EXPERIMENT,
  any → NONE. Every other request, and any request while the system mode is SAFE_MODE, is refused with
  `ActivityRejected(requested, current, reason)` (warning) and command response VALIDATION_ERROR; the activity and the
  telemetry channel are unchanged.
- **R3 Sequences.** Entering activity X dispatches `/seq/activity_<x>_enter.seq`; leaving it dispatches
  `/seq/activity_<x>_exit.seq` **before** the entry sequence of the next activity (NONE has no sequences). File names are
  fixed by convention, not parameters (files are uplinkable; the parameter budget is 25 saved). A missing file is
  reported by the sequencer's own event and the activity still changes (the ground sees both and fixes the file).
- **R4 Safe mode forces NONE.** Within one 1 Hz tick of the system mode becoming SAFE_MODE with activity ≠ NONE: the
  exit sequence of the current activity is dispatched, the activity becomes NONE, `ActivityChanged(from, NONE, reason
  SAFE_MODE)` is emitted. Commands in that exit sequence that fail because ModeManager already cut a rail produce at most
  the issuing component's warning event, never a fault (the implementer verifies this for `ABORT`/`DISARM` against
  `DriverBoardHandler`'s refusal path).
- **R5 RAM-only.** After any reset the activity is NONE, no exit sequence runs, no event about a previous activity is
  emitted. (The payload rail is off after reset regardless: ModeManager's boot behaviour.)
- **R6 Observability.** Telemetry `Activity: Activity` (update on change, in an existing packet, **not** Health);
  events `ActivityChanged(from, to, reason: {COMMAND, SAFE_MODE, SEQUENCE_FAILED})` (activity high) and
  `ActivityRejected`. `GET_ACTIVITY` command emitting the current value as an event.
- **R7 The two activities' sequences (delivered with the component, in `sequences/`):**
  - `activity_experiment_enter.seq`: `payloadPowerLoadSwitch.TURN_ON`; wait; `driverBoardHandler.PING`;
    `detumbleManager.SET_MODE(<stand-down value>)`; `driverBoardHandler.ARM`. (Extends `payload_on.seq`.)
  - `activity_experiment_exit.seq`: `driverBoardHandler.ABORT`; `driverBoardHandler.DISARM`;
    `detumbleManager.SET_MODE(<normal value>)`; `payloadPowerLoadSwitch.TURN_OFF`. (Extends `payload_off.seq`.)
  - `activity_calibration_enter/exit.seq`: the magnetometer ellipsoid-fit and dipole-identification procedure once
    controls defines it; until then a placeholder pair that only toggles `TaskGate` ADCS off/on, so the mechanism is
    testable.
  This is the B-dot vs STM32 arbitration: **`detumbleManager.SET_MODE` in the sequence, not a port.** Verify the
  `DetumbleMode` enumerators (`Components/DetumbleManager/DetumbleManager.fpp:44`) and pick the stand-down value.
- **R8 Harm (measurable).** Default flight behaviour unchanged: with no `SET_ACTIVITY` ever sent, every existing host
  test stays green, the safe-mode entry/exit transcripts on the bench are identical to today's, `SystemMode` enum and
  `ModeManager.cpp` have an empty diff, `DetumbleManager` and `TaskGate` have an empty diff. `scripts/check_capacity.py`
  stays exit 0: the new component costs 1–2 commands, 1 channel, 2–3 events, plus one `Svc.CmdSequencer` instance's own
  channels/events — check the channel headroom (12 free at 6a1589b2; A8 raises the limit) **before** adding the packet
  entry, and one 1 Hz slot (19 is free) and one 10 Hz slot for the sequencer's `schedIn`.

## 3. Reuse inventory (verified 2026-09-19 at 6a1589b2)

| Need | Existing piece | Pointer |
|---|---|---|
| Know the system mode | `Components.GetSystemMode` port; poll it on the 1 Hz tick like `driverBoardHandler.getMode` | `ModeManager.fpp:24,49`; `DriverBoardHandler.fpp:89`. `modeChanged` is a `[1]` array taken by DetumbleManager — do not widen it; polling is one tick late, which R4 allows |
| Run a sequence and learn it finished | ModeManager's own pattern: a dedicated `Svc.CmdSequencer` instance, `runSequence -> <seq>.seqRunIn`, `<seq>.seqDone -> completeSequence` | `topology.fpp:249-251` (`safeModeSeq`); `ModeManager.fpp:42,66` |
| Power the payload rail, ping | `payload_on.seq`, `payload_off.seq` (Cycle E) | `sequences/` |
| Stand B-dot down / up | `detumbleManager.SET_MODE(mode: DetumbleMode)` | `DetumbleManager.fpp:44` |
| Arm / abort the driver board | `driverBoardHandler.ARM / ABORT / DISARM` | `DriverBoardHandler.fpp:122-133` |
| Switch tasks | `taskGate.ENABLE_TASK / DISABLE_TASK` | `TaskGate.fpp:38-41` |
| Refusal + event pattern | `DriverBoardHandler` refusal events, `FaultManager` `parametersLoaded` if a parameter is ever added | ledger |

Known defect to fix in the same cycle (it makes "leave an activity" meaningless otherwise): ModeManager's eight
`loadSwitchTurnOn` ports are **unwired** (ledger issue 7), so nothing comes back on after safe mode today.

## 4. Non-goals

Persisting the activity; a task-mask port on TaskGate; STANDBY; a mode/activity overlay inside `FaultManager`'s policy
(a later row: FaultManager reads the activity through a get port and gains a PAYLOAD_ABORT action = force NONE);
scheduling changes; any edit under `lib/`.

## 5. Open items for the implementer (ask Jesse if unclear)

1. The `DetumbleMode` stand-down and normal values, and whether standing B-dot down during EXPERIMENT is acceptable
   for the minimum success level (the coils are shared: C-26).
2. Whether CALIBRATION ships now with placeholder sequences or waits for the controls procedure (Q15/Q16).
3. Sequencer instance: reuse `cmdSeq` (then `seqDone` routing conflicts with StartupManager) or add `activitySeq` as
   ModeManager did. The brief assumes the latter; count its dictionary cost.
4. Whether `SET_ACTIVITY` should be refused while a previous entry/exit sequence is still running (recommended: yes,
   `ActivityRejected(reason SEQUENCE_RUNNING)`), which needs the `completeSequence` hook.

## 6. Acceptance (derived from §2; write these before the code, per the loop's Stage 3b)

Host: R1–R6 with a sequencer stub and a mode stub (refusal table exhaustive over every (current, requested, mode)
triple; R4 forced-NONE; R5 reset). Board (deferred): experiment enter/exit transcript on the bench shows rail on →
PING → detumble stand-down → ARM, and the reverse; forcing SAFE mid-experiment shows ABORT and DISARM refusals at most
as warnings. Requirement rows: `MS-L2-01` (modes/activities) and `ADCS` arbitration rows from
`generated/requirements-not-verified.md` (SCALAR model repo, not this one) Tier 1; add `ACT-1..n` in a component sdd table via `req.py`.
