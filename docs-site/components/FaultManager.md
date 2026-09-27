# Components::FaultManager

## Introduction

`FaultManager` is the central fault detector for the flight controller. Producers report faults
through the `faultIn` port; the component debounces them, counts them, telemeters them and emits
events. It implements the CDR "Watchdog Logic / Fault Manager" block for requirements FD-L2-01,
FD-L2-05, FD-L2-06 and FD-L2-09.

**It ships in shadow mode.** `AUTHORITY_ENABLED` defaults to `false` and `AUTHORITY_MASK` to `0`,
so `FaultTable::claims()` is false for every fault type. That single gate means:

* `faultIn` always answers `OBSERVED`, so every producer keeps performing exactly the recovery it
  performs today;
* `run_handler` can never reach `forceSafeMode_out` or `stopWatchdog_out`.

Adding the component to the topology therefore cannot change flight behaviour. Recovery authority
is handed over one fault type at a time, later, by setting `AUTHORITY_ENABLED` and the type's bit
in `AUTHORITY_MASK`.

## Design

### Shadow mode and authority

```
producer ──faultIn──► FaultManager ──► FaultTable.report()   (count, debounce)
                            │
                            └─ claims(type, AUTHORITY_ENABLED, AUTHORITY_MASK)
                                   │                      │
                              false (default)          true
                                   │                      │
                            return OBSERVED         return CLAIMED
                            producer acts           producer stands down;
                            as it does today        run_handler acts on the
                                                    next 1 Hz tick
```

`claims()` is true only when all three hold: the master gate `AUTHORITY_ENABLED` is on, the type's
bit is set in `AUTHORITY_MASK` (bit = `1 << (FaultType - 1)`), and the type has a non-`NONE` action.
Any parameter that reads back `INVALID` or `UNINIT` falls back to the shadow-mode default, so a lost
or corrupt parameter database can only make the component quieter, never louder.

### Fault types, debounce and actions

The action column reproduces what the producer already does today, so enabling authority for a type
is a transfer of the same action, not a new one.

| Fault type | Bit | Source | Sampled | Debounce | Action |
|---|---|---|---|---|---|
| `FACE_TEMP_HIGH` | 0x01 | ThermalManager | no | `DEBOUNCE_THERMAL` (1) | none |
| `FACE_TEMP_LOW` | 0x02 | ThermalManager | no | `DEBOUNCE_THERMAL` (1) | none |
| `BATT_TEMP_HIGH` | 0x04 | ThermalManager | no | `DEBOUNCE_THERMAL` (1) | none |
| `BATT_TEMP_LOW` | 0x08 | ThermalManager | no | `DEBOUNCE_THERMAL` (1) | none |
| `LOW_BATTERY` | 0x10 | ModeManager | yes | `DEBOUNCE_LOW_BATTERY` (10) | `SAFE_MODE`, reason `LOW_BATTERY` |
| `COMMAND_LOSS` | 0x20 | ModeManager (since F3; the AuthenticationRouter that first owned it was retired by upstream 1af2a0c5) | no | 1 | `SAFE_MODE_AND_REBOOT`: `stopWatchdog`, then `forceSafeMode(COMMAND_LOSS)` |
| `WATCHDOG_STOPPED` | 0x40 | Watchdog | no | 1 | none (the hardware reset is already under way) |
| `ADCS_UNSTABLE` | 0x80 | (reserved) | no | 1 | none (FD-L2-07 hook; threshold TBD by Mission Ops) |

A *sampled* fault is one whose producer reports it once per 1 Hz sample while the condition holds,
so a tick with no report re-arms the debounce; this mirrors the counter reset in
`ModeManager::run_handler`. An unsampled fault is reported by a one-shot event and stays confirmed
until `CLEAR_FAULTS`.

### Threading contract

The component is **passive with a guarded intake**. `faultIn` is called from several threads (the
1 Hz rate group for ThermalManager and ModeManager, the latter under its `m_commandLossMutex` for
the command-loss report — safe, because this handler calls no output port; the command dispatcher
for `STOP_WATCHDOG`; the event manager for the FATAL path into `watchdog.stop`), while the periodic
work is an O(9) table scan. A thread and an async queue would cost a 4 KB stack and an overflow
mode for no benefit; the generated guarded-port mutex gives the thread safety instead. Upgrading to
an active component later is a one-word change in the FPP plus the instance line.

Two rules keep that safe:

1. `faultIn_handler` runs under the component mutex (taken by the generated base) and touches only
   the fault table. It calls no output port.
2. `run_handler` locks, ticks the table, snapshots the counters, unlocks, and only then performs
   actions. This matters because `stopWatchdog_out` reaches `Watchdog::stop_handler`, which reports
   `WATCHDOG_STOPPED` straight back into the guarded `faultIn` on the same thread; taking the mutex
   there while still holding it would deadlock.

### Why the types live in their own module

The shared FPP types (`FaultType`, `FaultSource`, `FaultSeverity`, `FaultAction`, `FaultDisposition`,
the `FaultReport` port and `FaultInPorts`) are in `Components/FaultTypes/`, a types-only CMake module
with no dependencies of its own — the same shape as `Components/Drv/Types/`.

They cannot live in `Components/FaultManager/`. The four producers need the `FaultReport` port type,
so they would depend on this component; this component needs ModeManager's
`ForceSafeModeWithReason`, so it depends on ModeManager. Putting the types here makes that a CMake
dependency cycle between the two modules, which `register_fprime_library` breaks by dropping an
edge — the target build then fails with a missing `FaultReportPortAc.hpp` because the autocoder
order is no longer constrained. Splitting the types out removes the cycle.

### Rate group placement

`faultManager.run` is `rateGroup1Hz.RateGroupMemberOut[20]`, after `modeManager` (16),
`taskGate.schedIn[THERMAL]` (18, which runs `thermalManager`; slot 19 belonged to the retired
`authenticationRouter`). Rate group members run in index order, so a
fault reported at tick *t* is decided at tick *t* — the same second in which the producer acts
today.

### Telemetry packet

The 13 channels form a new `packet Faults id 9 group 5`. After the 2026-09-19 upstream sync the
packet set holds 23 packets of `MAX_PACKETIZER_PACKETS` = 24 and names 244 of
`MAX_PACKETIZER_CHANNELS` = 256 distinct channels (packets and omit block together;
`PROVESFlightControllerReference/project/config/TlmPacketizerCfg.hpp:19,21`). One packet id (24) and
12 channels are left before either constant must be raised; `scripts/check_packet_set.py` in
`verify.sh` guards both.

## Port Descriptions

| Port | Kind | Type | Description |
|---|---|---|---|
| `faultIn` | guarded input, 4 slots | `Components.FaultReport` | Fault intake. Slot 0 thermalManager, 1 modeManager (`LOW_BATTERY` and, since F3, `COMMAND_LOSS`), 2 unconnected (retired: the command-loss router was removed by upstream 1af2a0c5), 3 watchdog |
| `run` | sync input | `Svc.Sched` | 1 Hz tick; drains confirmations and writes telemetry |
| `forceSafeMode` | output | `Components.ForceSafeModeWithReason` | Safe mode entry. Unreachable in shadow mode |
| `stopWatchdog` | output | `Fw.Signal` | Stops petting the watchdog (reboot in ~26 s). Unreachable in shadow mode |

## Commands

| Command | Description |
|---|---|
| `CLEAR_FAULTS` | Clears every recorded fault, confirmation and counter; emits `FaultsCleared` |
| `GET_FAULT_STATUS` | Emits `FaultStatusReport` with the active mask, totals and the effective authority |

## Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `AUTHORITY_ENABLED` | `bool` | `false` | Master authority gate. False keeps the component in shadow mode |
| `AUTHORITY_MASK` | `U8` | `0` | Per-type authority bits, `1 << (FaultType - 1)` |
| `DEBOUNCE_LOW_BATTERY` | `U8` | `10` | Consecutive low-voltage samples needed to confirm `LOW_BATTERY` |
| `DEBOUNCE_THERMAL` | `U8` | `1` | Consecutive reports needed to confirm a thermal threshold fault |

An `INVALID` or `UNINIT` read of any of these falls back to the value in the Default column.

Saved values are applied at boot through the F Prime 4.3.0 `parametersLoaded()` hook, before the
first tick (A10): once `AUTHORITY_ENABLED` / `AUTHORITY_MASK` have been saved with `PRM_SAVE_FILE`
they survive a reboot, so a reboot no longer returns FDIR to shadow mode (the intended end state of
the authority-enable procedure), and `FaultAuthorityChanged` is emitted once at boot when the saved
gate differs from the compiled default. With nothing saved the defaults above hold (FaultManager-1).

## Telemetry

All 13 channels are `update on change` and belong to the `Faults` packet (id 9, level 5).

| Channel | Type | Description |
|---|---|---|
| `FaultsDetected` | `U32` | Total fault reports received on `faultIn` |
| `FaultsConfirmed` | `U32` | Total confirmations (debounce thresholds met) |
| `ActiveFaults` | `U8` | Bitmask of currently confirmed fault types |
| `LastFaultType` | `FaultType` | Type of the most recently confirmed fault |
| `LastFaultSource` | `FaultSource` | Source of the most recently confirmed fault |
| `LastFaultValue` | `F32` | Value carried by the most recently confirmed fault |
| `ShadowActionsSuppressed` | `U32` | Recovery actions declined for lack of authority |
| `ActionsTaken` | `U32` | Recovery actions performed by this component |
| `AuthorityState` | `U8` | Effective authority mask; 0 means shadow mode |
| `FaultCountThermal` | `U32` | Reports for the four thermal threshold types |
| `FaultCountLowBattery` | `U32` | Reports for `LOW_BATTERY` |
| `FaultCountCommandLoss` | `U32` | Reports for `COMMAND_LOSS` |
| `FaultCountWatchdogStop` | `U32` | Reports for `WATCHDOG_STOPPED` |

## Events

| Event | Severity | Description |
|---|---|---|
| `FaultConfirmed` | warning high, throttle 10 | A fault type met its debounce threshold |
| `FaultActionSuppressed` | warning low, throttle 10 | A confirmed fault had an action, but this component has no authority for it |
| `FaultActionTaken` | warning high | A confirmed fault was acted on by this component |
| `FaultsCleared` | activity high | `CLEAR_FAULTS` completed |
| `FaultStatusReport` | activity low | Response to `GET_FAULT_STATUS` |
| `FaultAuthorityChanged` | warning high | The effective authority changed through a parameter update |

## Follow-ups

* **Event delta in the authority phase.** Once `LOW_BATTERY` authority is enabled, entry runs
  through `ModeManager::forceSafeMode_handler`, which logs `ExternalFaultDetected` rather than
  `AutoSafeModeEntry(LOW_BATTERY, voltage)`, and the entry lands one async queue hop later. Mode,
  reason, entry count, load switches, sequence and recovery are unchanged. Whether to add the
  voltage-bearing event back is deferred to the authority phase, not resolved in this cycle.
* **FD-L2-04 (timestamped fault log)** is out of scope: there is no on-board event log. The hook
  left behind is the per-type `Status{reports, lastTick, lastValue, lastSource}` in `FaultTable`
  plus `FaultStatusReport`; a history ring can be added inside the pure module without touching a
  producer.
* **FD-L2-07 (ADCS instability)** is out of scope pending a Mission Ops threshold.
  `FaultType.ADCS_UNSTABLE` and `FaultSource.DETUMBLE_MANAGER` are reserved; adding a producer is
  one `FaultInPorts` increment and one connection.

## Requirements

Pass criteria are decided before testing; edit with `scripts/req.py`, not by hand.

| Name | Description | Method | Level | Pass Criteria | Status | Reason |
|---|---|---|---|---|---|---|
|FaultManager-1|With the shipped parameter defaults, or any INVALID/UNINIT parameter read, FaultManager shall answer every fault report OBSERVED and shall never call forceSafeMode or stopWatchdog|Unit Test|Unit|Over every report pattern (each fault type, repeated past its debounce, in every source/severity combination) with defaults, with paramValidity INVALID and with UNINIT: zero forceSafeMode calls, zero stopWatchdog calls, and every faultIn return is OBSERVED|||
|FaultManager-2|FaultManager shall count every fault report and publish the detected, confirmed, active-mask and per-type counts on telemetry|Unit Test|Unit|After N reports of a given type: FaultsDetected == N, the type's FaultCount* channel == N, ActiveFaults carries the type's bit once confirmed, FaultsConfirmed counts confirmation edges, and each channel is written only when its value changed|||
|FaultManager-3|A sampled fault shall be confirmed only after DEBOUNCE_LOW_BATTERY consecutive reporting ticks, and a tick with no report shall re-arm the debounce|Unit Test|Unit|With debounce 10: no confirmation at the 9th consecutive report, exactly one confirmation edge at the 10th, no second edge while it stays reported; a tick with no report resets the count so 9 further reports do not confirm|||
|FaultManager-4|When granted authority for a fault type, FaultManager shall perform exactly the recovery action that the reporting component performs today|Unit Test|Unit|Authority on with bit 0x10: one forceSafeMode(LOW_BATTERY) after the LOW_BATTERY debounce. Bit 0x20: stopWatchdog then forceSafeMode(COMMAND_LOSS) in that order, once. WATCHDOG_STOPPED and the four thermal types: zero action calls at any mask|||
|FaultManager-5|faultIn shall return CLAIMED only when AUTHORITY_ENABLED is true, the fault type's bit is set in AUTHORITY_MASK, and the type has a non-NONE action|Unit Test|Unit|Truth table over enabled x mask-bit-set x action-is-NONE for all eight fault types: CLAIMED in exactly the enabled-and-bit-set-and-action-not-NONE cells, OBSERVED in every other cell|||
|FaultManager-6|Each producer shall emit exactly one fault report per trigger beside its existing behaviour, and shall behave identically when faultOut is unconnected or the disposition is OBSERVED|Unit Test|Unit|ThermalManager, ModeManager and Watchdog each: with faultOut connected and OBSERVED, exactly one report per trigger with the matching type, source and value AND the pre-existing events, counters and port calls unchanged; with faultOut unconnected, zero reports and the same pre-existing behaviour|||
|FaultManager-7|CLEAR_FAULTS shall clear every recorded fault and counter and GET_FAULT_STATUS shall report the current summary, both returning OK|Unit Test|Unit|After CLEAR_FAULTS: FaultsDetected, FaultsConfirmed and ActiveFaults are 0, one FaultsCleared event, response OK. GET_FAULT_STATUS emits one FaultStatusReport carrying the current active mask, totals and effective authority, response OK|||
|FaultManager-8|The Faults telemetry packet shall be downlinked at telemetry level 5|Integration Test|Board|With CdhCore.tlmSend at SET_LEVEL 5, FaultsDetected, ActiveFaults and AuthorityState are received within 45 s|||
|FaultManager-9|FaultManager shall never call an output action port while holding the guarded lock, and every lock shall be balanced by an unlock|Unit Test|Unit|Across every tested report and tick sequence, including the authority-enabled action paths: the recorder never observes a forceSafeMode or stopWatchdog call with lock depth greater than 0, and lock depth returns to 0 with no unlock underflow|||
|FaultManager-10|AUTHORITY_ENABLED, AUTHORITY_MASK and the debounce parameters saved in PrmDb shall be effective before the first run tick after boot, without any PRM_SET; an unusable saved value falls back to the shadow default|Unit Test|Unit|With AUTHORITY_ENABLED true and AUTHORITY_MASK 0x10 (LOW_BATTERY) VALID in the stub and no parameterUpdated call, after parametersLoaded() exactly one FaultAuthorityChanged(true, 0x10), AuthorityState telemetry after one tick reads 0x10, and a confirmed LOW_BATTERY is actioned (forceSafeMode called once) rather than OBSERVED; with paramValidity INVALID (host-stub case): no event and FaultManager-1 shadow behaviour; a parameterUpdated after boot applies as today with no duplicate event|||

## Change Log
| Date | Description |
|---| --- |
|Sep 2026| Initial version: shadow-mode fault detection, four producer hooks, Faults packet id 9 |
|2026-09 (F3)| Producer 2 re-sourced: `COMMAND_LOSS` now arrives from `ModeManager::commandLossCheck` on slot 1 (the AuthenticationRouter and slot 2 are retired, upstream 1af2a0c5); `reasonFor(COMMAND_LOSS)` maps to `SafeModeReason::COMMAND_LOSS` so a CLAIMED command loss persists the same reason as upstream's path |
|2026-09-19 (A10)| `parametersLoaded()` override applies saved `AUTHORITY_ENABLED` / `AUTHORITY_MASK` / debounces at boot; a reboot no longer returns FDIR to shadow mode once authority has been saved; one `FaultAuthorityChanged` at boot when the saved gate differs from the default (FaultManager-10) |
