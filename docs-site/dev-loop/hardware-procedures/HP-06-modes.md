# HP-06 Mode transitions and mode-dependent behaviour (T1 Desk-USB)

| Field | Value |
|---|---|
| Tier | T1 Desk-USB |
| Hardware | FC board, UART GDS; load switches read back via GPIO (real loads: T5 evidence only) |
| Image / flash | Current image; start in NORMAL (`GET_CURRENT_MODE`) |
| Preconditions | `CdhCore.tlmSend.SET_LEVEL 5` (`detumbleManager.Mode` is packet group 3); `detumbleManager` OPERATING_MODE parameter noted; face switches ON |
| Restore | `EXIT_SAFE_MODE`; `TURN_ON` every face switch by hand (`loadSwitchTurnOn` unwired); `SET_LEVEL 1` |
| Destructive | No (load switches cycle) |
| Duration | ~6 min automated, ~12 min manual |

Windows (from `mode_manager_test.py`): mode readback 5 s; event 2 s; detumble telemetry 45 s; MM0004 no-auto-exit window 13 s (10 s debounce + 3).

## Procedure
1. `SET_LEVEL 5`; `GET_CURRENT_MODE` -> NORMAL. Record `detumbleManager.Mode` (should equal OPERATING_MODE) <= 45 s.
2. `RD.modeManager.FORCE_SAFE_MODE`. Observable: OK; `ManualSafeModeEntry` and `EnteringSafeMode(GROUND_COMMAND)` <= 2 s; `GET_CURRENT_MODE` -> SAFE_MODE <= 5 s; `GET_SAFE_MODE_REASON` -> GROUND_COMMAND.
3. Within 5 s of step 2: `GET_IS_ON` on face0-5, payloadPower, payloadBattery. Observable: all OFF.
4. `detumbleManager.Mode` reads DISABLED <= 45 s.
5. Wait 13 s with no commands. Observable: no `AutoSafeModeExit` event.
6. `EXIT_SAFE_MODE`. Observable: OK; `ExitingSafeMode` <= 2 s; `GET_CURRENT_MODE` -> NORMAL <= 5 s; `GET_SAFE_MODE_REASON` -> NONE; `detumbleManager.Mode` returns to OPERATING_MODE <= 45 s; payload switches still OFF via `GET_IS_ON`.
7. For the CONOPS modes STANDBY/CALIBRATION/EXPERIMENT: record "no entry command exists" (MS-L2-01 partial).
8. Restore per header; `GET_IS_ON` faces -> ON.

## Criteria
| ID | Criterion | Automated | Evidence |
|---|---|---|---|
| CDH-10 | FORCE_SAFE_MODE -> SAFE_MODE <= 5 s; EXIT -> NORMAL <= 5 s; reason GROUND_COMMAND then NONE | mode_manager_test.py::test_safe_03_exit_clears_reason | Steps 2, 6 |
| MS-L2-02 | Same pair within 5 s each | mode_manager_test.py::test_safe_03_exit_clears_reason | Steps 2, 6 |
| MM0001 | GET_CURRENT_MODE SAFE_MODE after FORCE, NORMAL after EXIT, <= 5 s | mode_manager_test.py::test_safe_03_exit_clears_reason | Steps 2, 6 |
| MM0002 | ManualSafeModeEntry + EnteringSafeMode(Ground command) <= 2 s; reason GROUND_COMMAND | mode_manager_test.py::test_safe_02_ground_command_sets_reason | Step 2 |
| MM0004 | No AutoSafeModeExit within 13 s for reason GROUND_COMMAND; EXIT -> NORMAL <= 5 s (voltage-recovery exit: HP-10 step A) | mode_manager_test.py::test_safe_04_no_auto_recovery_for_ground_command | Steps 5-6 |
| MS-L2-05 | <= 5 s of FORCE: all 8 switches OFF via GET_IS_ON; payload switches stay OFF after EXIT | mode_manager_test.py::test_safe_11_safe_mode_turns_off_load_switches | Steps 3, 6 |
| MM0005 | Board clause: every switch OFF via GET_IS_ON <= 5 s (unit: 8 loadSwitchTurnOff calls, passing) | test_ModeManager_VoltageDebounce (unit) ; mode_manager_test.py::test_safe_10 | Step 3 |
| MS-L2-03 | detumbleManager.Mode DISABLED <= 45 s on FORCE; back to OPERATING_MODE <= 45 s on EXIT | mode_manager_test.py::test_safe_12_detumble_disabled_in_safe_mode | Steps 4, 6 |
| MS-L2-06 | Every mode change emits EnteringSafeMode / ExitingSafeMode <= 2 s and modeChanged reaches detumbleManager | mode_manager_test.py::test_safe_02 ; ::test_safe_11 | Steps 2, 4, 6 |
| MS-L2-01 | Only SAFE_MODE and NORMAL are reachable (steps 2, 6); STANDBY/CALIBRATION/EXPERIMENT: README (Mission Ops) | mode_manager_test.py::test_safe_03 (the two existing modes) | Steps 2, 6, 7 |

## Why this verifies it
- CDH-10, MS-L2-02, MM0001/02: the observable is the mode readback and the reason readback via separate query commands, not the transition command's own ack.
- MM0004: negative path (no automatic exit for GROUND_COMMAND) is observed over a window longer than the 10-sample debounce, so a debounce-driven auto-exit would have shown. The voltage-recovery exit path is provoked only where voltage can be driven (HP-10).
- MS-L2-05, MM0005: `GET_IS_ON` is answered by the LoadSwitch component reading its GPIO, independent of ModeManager. It shows the commanded state, not load current: real-load evidence is T5 (Flatsat), noted as remainder.
- MS-L2-03/06: detumbleManager's own Mode channel is the downstream oracle for the broadcast; 45 s covers one packetizer run.
- MS-L2-01: only the implemented pair can be tested; the criterion as written cannot pass until Mission Ops defines the other modes.

## Known traps
- `EXIT_SAFE_MODE` returns OK from NORMAL (`ModeManager.cpp` ~226): its ack is not a negative-path oracle; use readbacks.
- `loadSwitchTurnOn` is unwired (`topology.fpp:474-481`): faces stay OFF after EXIT (MM0006 false today). Restore by hand or HP-02/04/08 fail afterwards.
- `recover_from_safe_mode` fixture runs only with `--with-radio`; on UART the group must do its own EXIT.
- `EnteringSafeMode` reason text is "Ground command" in the event; the enum is GROUND_COMMAND(3).
