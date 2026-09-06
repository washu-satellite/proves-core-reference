# 06 — Non-goals, open risks, authority-enable procedure

## Non-goals (this cycle)
- No producer for `DeviceNotReady` (Ina219/PicoTemp/RtcManager/Drv2605) or for rate-group `RgCycleSlips`, `comQueue.QueueOverflow`,
  `commsBufferManager.NoBuffs`, Authenticate `RejectedPacketsCount`: F´ has no telemetry subscription; each would be a new `faultOut` in a driver or a
  poll port. Candidates for Cycle D+1 once the four report paths are proven on flatsat.
- No polling of voltage by the manager (would add a second INA219 read per second — CDR risk 1 bus contention); the ModeManager per-sample report gives
  the same debounce input with zero extra I2C.
- Action map is compile-time; no per-type action parameters (opcode budget). No ResetManager reboot path.
- No removal of the producers' direct actions — the `claimed` guard is the reroute hook; removal is a later cycle after flatsat evidence.
- FD-L2-04 (no on-board log) and FD-L2-07 (TBD) untouched beyond the hooks in 01.

## Open risks
1. Authority-mode event delta for LOW_BATTERY: `ExternalFaultDetected` instead of `AutoSafeModeEntry(reason, voltage)` (03). Options before flipping
   bit 0x10: (a) accept and update ground displays; (b) add an optional `voltage` to `ForceSafeModeWithReason` — touches `ModeManager.fpp` and the router;
   (c) keep ModeManager authoritative for LOW_BATTERY permanently and use the manager only as observer for it. Decide on flatsat data.
2. `forceSafeMode` is async → with authority the safe-mode entry lands on the ModeManager thread a queue hop later than today's inline
   `enterSafeMode` (ms, not s). Same as today's command-loss path, which already goes through this port.
3. Dispatch table 357 > 350 unless raised (04 §D step 21). `PRMDB_NUM_DB_ENTRIES` 25 bounds saved params — `AUTHORITY_*` must be among the saved
   ones in flight; audit which params are saved before the flip.
4. Router `faultOut` is only compile-verified (no host build). Its `claimed` guard must be reviewed by eye against 03.
5. Rate-group index ordering (slot 20 after producers) is a wiring invariant; a future reorder silently adds a tick of latency — the topology
   comment is the only guard. Consider a board assertion in the int test (FaultConfirmed second == AutoSafeModeEntry second).
6. Cycle B is uncommitted in the working tree; ThermalManager line numbers in this plan are HEAD numbers. Merge by grep, and rerun B's tests.
7. Throttled `FaultConfirmed`/`FaultActionSuppressed` (10) hide long fault storms from the ground; the counters in the `Faults` packet do not.

## Authority-enable procedure (flatsat, per trigger; each step reversible with `AUTHORITY_MASK_PRM_SET 0`)
1. Soak in shadow ≥ 24 h at telemetry level 5. Pair GDS logs: every `AutoSafeModeEntry`/`CommandLossFound`/`Temperature*Threshold`/`WatchdogStop`
   must have a `FaultConfirmed` of the matching type in the same second, and `ShadowActionsSuppressed` must equal the count of source actions
   (LOW_BATTERY + COMMAND_LOSS events). Any mismatch = a producer or debounce bug; do not proceed.
2. COMMAND_LOSS first (simplest, one-shot): `COMM_LOSS_TIME_PRM_SET` to ~60 s, `AUTHORITY_MASK_PRM_SET 0x20`, `AUTHORITY_ENABLED_PRM_SET true`.
   Expect `FaultActionTaken(COMMAND_LOSS, SAFE_MODE_AND_REBOOT)`, `WatchdogStop`, `FaultConfirmed(WATCHDOG_STOPPED)`, `EnteringSafeMode`,
   reboot ≈ 26 s, boot in SAFE_MODE reason EXTERNAL_REQUEST — identical to today except the extra manager events. Restore `COMM_LOSS_TIME`.
3. LOW_BATTERY: bench PSU ramp below 6.7 V for > 10 s with mask `0x30`; resolve risk 1 first. Verify load switches OFF, recovery > 8.0 V unchanged.
4. Persist: `FaultManager.AUTHORITY_*_PRM_SAVE` then `FileHandling.prmDb.PRM_SAVE_FILE`; confirm after `WARM_RESET` via `GET_FAULT_STATUS`
   (`authority` field) — this is the PrmDb-on-board check the ledger says is unverified.
5. Only after (2)-(4): a follow-up cycle may delete the producer-side direct actions and make the manager the "only FDIR authority" per the CDR.
