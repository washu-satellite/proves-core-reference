# HP-07 Reboot, mode persistence and fault flagging (T1 Desk-USB, destructive)

| Field | Value |
|---|---|
| Tier | T1 Desk-USB (`uart_only`: every step here severs an RF link) |
| Hardware | FC board, UART GDS; Pico Debug Probe recommended for console during reboots |
| Image / flash | Current image; NORMAL; BootCount N0 recorded |
| Preconditions | `SET_LEVEL 5`; `telemetryDelay.DIVIDER` 29; face0 switch ON; board uptime > 60 s before step 7 |
| Restore | `EXIT_SAFE_MODE`; `FACE_TEMP_UPPER_THRESHOLD_PRM_SET 60`; `START_WATCHDOG` (if not petting after boot); face switches `TURN_ON`; `SET_LEVEL 1`. Reboots clear RAM-only params (no `PRM_SAVE_FILE` in this group) |
| Destructive | Yes: 3 reboots, safe-mode entries |
| Duration | ~10 min automated, ~20 min manual |

Windows: mode readback 5 s; event 2 s; reboot event 15 s; no-UnintendedReboot window 5 s after first post-boot event; watchdog stop -> reboot ~26 s, BootCount +1 <= 60 s.

## Procedure
1. `FORCE_SAFE_MODE`; confirm SAFE_MODE / GROUND_COMMAND (as HP-06 step 2).
2. `RD.resetManager.WARM_RESET`. Observable: link drops; first post-boot event <= 15 s; BootCount == N0 + 1.
3. Within 5 s of first post-boot event: no `UnintendedRebootDetected`. `GET_CURRENT_MODE` -> SAFE_MODE, `GET_SAFE_MODE_REASON` -> GROUND_COMMAND <= 5 s (MS-L2-07, MM0007). Then `EXIT_SAFE_MODE`, faces `TURN_ON`.
4. `RD.thermalManager.FACE_TEMP_UPPER_THRESHOLD_PRM_SET 0`. Observable: `TemperatureAboveThreshold` WARNING <= 2 s (CDH-15 thermal). Restore 60.
5. `RD.face0LoadSwitch.TURN_OFF` then `RD.tmp112Face0Manager.GetTemperature`. Observable: `DeviceNotReady` WARNING <= 2 s (CDH-15 device). `TURN_ON`.
6. `RD.watchdog.STOP_WATCHDOG`. Observable: `WatchdogStop` <= 2 s; reboot; BootCount +1 <= 60 s. After boot `START_WATCHDOG` acked OK and no further reboot in 60 s (SC-L2-07 watchdog pair).
7. `RD.telemetryGate.SET_TRANSMIT_STATE DISABLED` then `ENABLED` (SC-L2-07 gate pair; timing evidence from HP-05).
8. `RD.ComCcsdsLora.authenticationRouter.COMM_LOSS_TIME_PRM_SET 60` (LoRa AuthenticationRouter instance) with uptime > 60 s and no LoRa command ever received this boot. Observable: `CommandLossFound` WARNING <= 2 s of the set taking effect (next 1 Hz run); `EnteringSafeMode(EXTERNAL_REQUEST)` <= 2 s; reboot (router stops watchdog petting) BootCount +1 <= 60 s (CDH-15 command-loss clause).
9. After boot: `EXIT_SAFE_MODE` if needed; restore per header.

## Criteria
| ID | Criterion | Automated | Evidence |
|---|---|---|---|
| MS-L2-07 | After FORCE_SAFE_MODE then WARM_RESET: SAFE_MODE with reason GROUND_COMMAND, no UnintendedRebootDetected | mode_manager_test.py::test_safe_10_mode_persists_across_warm_reset | Steps 2-3 |
| MM0007 | Same as MS-L2-07 | mode_manager_test.py::test_safe_10_mode_persists_across_warm_reset | Steps 2-3 |
| SC-L2-07 | Watchdog START/STOP and telemetryGate SET_TRANSMIT_STATE take effect within one cycle; generic ENABLE_TASK/DISABLE_TASK not implemented (README) | watchdog_test.py::test_01_stop_watchdog_command / test_03 (stop) ; telemetry_gate_test.py::test_02 (gate) | Steps 6-7 |
| CDH-15 | Each injected fault -> WARNING <= 2 s: threshold 0 -> TemperatureAboveThreshold; COMM_LOSS_TIME expiry -> CommandLossFound; face0 OFF + Get -> DeviceNotReady | thermal_threshold_test.py::test_01 (thermal clause only) ; manual for the other two | Steps 4, 5, 8 event log with FSW times |

## Why this verifies it
- MS-L2-07 / MM0007: the observable is the mode restored by a fresh boot, read through the query command; the negative check (no UnintendedRebootDetected) confirms the reset was recognised as commanded, so the persisted record, not a fault path, explains SAFE_MODE. The mode file is not a PersistedRecord on this branch, so there is no unit-level CRC/atomic-replace claim behind it; power-cut robustness of the records that are is HP-12.
- CDH-15: each fault is provoked (negative path) and each WARNING comes from a different component than the one commanded (thermalManager, tmp112 driver, LoRa router). Command loss is real: no LoRa command was ever received this boot, so the timer measures actual silence, and the 60 s period is small enough to be observed. Positive path (an RF command resets the timer) is HP-14.
- SC-L2-07: the "stop" observable for the watchdog is the reboot itself (BootCount), independent of the watchdog component; "start" is the absence of a reboot afterwards.

## Known traps
- `STOP_WATCHDOG` also fires `prepareForReboot`; `reset_watchdog` from the router STOPS petting (reboot ~26 s), it does not kick. Both reboot the board: GDS must reconnect.
- Command-loss timer exists only in the LoRa AuthenticationRouter (`COMM_LOSS_TIME`, default 259200 s); it latches `m_safeModeCalled` once per boot, so step 8 cannot be repeated without a reboot.
- `EnteringSafeMode` reason for command loss is EXTERNAL_REQUEST(4), not LORA or SYSTEM_FAULT.
- `PRM_SET` values vanish on reboot; that is what restores COMM_LOSS_TIME after step 8. Never `PRM_SAVE_FILE` with 60 s set.
- `UnintendedRebootDetected` is raised in init before the first 1 Hz tick; watch the event log from link-up.
