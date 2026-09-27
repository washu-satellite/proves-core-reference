# HP-10 Fault detection to safe mode with a driven bus (T2 Desk-PSU, destructive)

| Field | Value |
|---|---|
| Tier | T2 Desk-PSU: T1 plus programmable supply on the bus the `ina219Sys` measures (CI rig: Korad on `/dev/ttyPWR`, `korad_control.py --output 0/1`; voltage set via the same script, outside this repo) |
| Hardware | FC board, UART GDS, bench PSU 8.0 V nominal, current limit per rig |
| Image / flash | Current image, NORMAL, BootCount N0 |
| Preconditions | `SET_LEVEL 5`; `telemetryDelay.DIVIDER_PRM_SET 0` for 1 s voltage resolution; face switches ON; thresholds default; uptime > 60 s before step B |
| Restore | PSU 8.0 V output ON; `EXIT_SAFE_MODE`; faces `TURN_ON`; `FACE_TEMP_UPPER_THRESHOLD_PRM_SET 60`; `DIVIDER_PRM_SET 29`; `SET_LEVEL 1`; `START_WATCHDOG` if needed |
| Destructive | Yes: brownout, 3 reboots, power cut |
| Duration | ~25 min manual |

Constants (`ModeManager.fpp` ~194-200): entry < 6.7 V, recovery > 8.0 V, debounce 10 consecutive 1 Hz samples. Windows: detect + event 12 s (10 s debounce + 2); mode readback 5 s; reboot -> BootCount +1 <= 60 s.

## Procedure
A. Low battery. PSU 8.0 -> 6.5 V at time T0. Observable: `ina219Sys.Voltage` < 6.7 within 2 s; `AutoSafeModeEntry(LOW_BATTERY)` and `EnteringSafeMode` at T0 + 10..12 s (not before T0 + 9 s); `GET_CURRENT_MODE` SAFE_MODE <= 5 s; all 8 switches OFF via `GET_IS_ON` <= 5 s; reason LOW_BATTERY. Then PSU 7.5 V (above entry, below recovery): no `AutoSafeModeExit` in 13 s; `EXIT_SAFE_MODE` acked OK, NORMAL <= 5 s (MS-L2-09 LOW_BATTERY). Re-enter at 6.5 V, then PSU 8.2 V: `AutoSafeModeExit` <= 12 s (MM0004 voltage-recovery evidence). Faces `TURN_ON`.
B. Command loss (uptime > 60 s, no LoRa command this boot). `RD.ComCcsdsLora.authenticationRouter.COMM_LOSS_TIME_PRM_SET 60`. Observable: `CommandLossFound` <= 2 s; `EnteringSafeMode(EXTERNAL_REQUEST)` <= 2 s; SAFE_MODE readback <= 5 s; reboot: BootCount N0+1 <= 60 s.
C. Watchdog stall. `RD.watchdog.STOP_WATCHDOG`. Observable: `WatchdogStop` <= 2 s; BootCount +1 <= 60 s.
D. Unintended reboot. PSU output 0 for 3 s, then 1. Observable: `UnintendedRebootDetected` within 5 s of first post-boot event; if FSW enters safe mode with reason SYSTEM_FAULT (enum comment `ModeManager.fpp:13`): `EXIT_SAFE_MODE` acked OK, NORMAL <= 5 s (MS-L2-09 SYSTEM_FAULT). If it does not, record "SYSTEM_FAULT entry not provokable" for Mission Ops.
E. Thermal and device clauses. `FACE_TEMP_UPPER_THRESHOLD_PRM_SET 0` -> `TemperatureAboveThreshold` <= 2 s; restore 60. `face0LoadSwitch.TURN_OFF` + `GetTemperature` -> `DeviceNotReady` <= 2 s; `TURN_ON`.
F. Restore per header.

## Criteria
| ID | Criterion | Automated | Evidence |
|---|---|---|---|
| CDH-11 | Each wired fault (command loss >= COMM_LOSS_TIME; bus < 6.7 V for 10 x 1 Hz samples): EnteringSafeMode <= 2 s of detection and SAFE_MODE readback | manual | Steps A, B event FSW times and PSU log |
| MS-L2-04 | Command loss -> CommandLossFound + EnteringSafeMode <= 2 s; < 6.7 V x 10 samples -> AutoSafeModeEntry(LOW_BATTERY) | manual | Steps A, B |
| FD-L2-06 | Same as MS-L2-04 | manual | Steps A, B |
| FD-L2-05 | watchdog stall -> reboot (BootCount +1 <= 60 s); command loss -> safe mode then reboot; low battery -> safe mode + switches OFF | watchdog_test.py::test_03_system_reboots_without_watchdog (stall) ; manual (others) | Steps A, B, C |
| FD-L2-01 | Thermal -> event <= 2 s; < 6.7 V for 10 s -> AutoSafeModeEntry; device fault -> DeviceNotReady <= 2 s | thermal_threshold_test.py::test_01 (thermal) ; manual | Steps A, E |
| MS-L2-09 | FORCE/EXIT for GROUND_COMMAND (HP-06); EXIT acked OK and NORMAL <= 5 s for LOW_BATTERY and SYSTEM_FAULT | safe_mode_test.py::test_safe_03 (GROUND_COMMAND) ; manual | Steps A, D |

## Why this verifies it
- CDH-11 / MS-L2-04 / FD-L2-06 / FD-L2-01(voltage): the fault is real (bus actually below 6.7 V, measured by the INA219 the FSW uses and logged by the PSU as an independent oracle). The 9 s lower bound on the entry time shows the 10-sample debounce, not a single-sample trip. Holding 7.5 V for the manual exit isolates the command from auto-recovery; the 8.2 V step then shows auto-recovery is the voltage path.
- Command loss: silence on LoRa is genuine (T2 has no radio); the 60 s parameter makes the timer observable. Timer reset by real RF traffic is HP-14.
- FD-L2-05: each mapped action is observed by a component other than the detector (BootCount from startupManager, GET_IS_ON from LoadSwitch).
- MS-L2-09: the override is proven only if the entry reason differs from GROUND_COMMAND, hence the driven-bus and power-cut entries.

## Known traps
- `ModeManager.run` is a sync port on the 1 Hz thread: debounce is exactly 10 ticks; a PSU ramp slower than 1 V/s smears T0.
- The PSU must feed the rail `ina219Sys` measures; USB-powered boards never see < 6.7 V.
- Command-loss timer lives only in the LoRa router; UART/S-band never time out. `m_safeModeCalled` latches: one trial per boot.
- Low-voltage safe mode turns faces OFF and `loadSwitchTurnOn` is unwired; restore by hand.
- The router's CallSafeMode also stops watchdog petting: expect a reboot ~26 s after step B, then RAM-only params revert.
