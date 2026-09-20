# HP-08 Thermal threshold flagging and transient-fault continuation (T1 Desk-USB)

| Field | Value |
|---|---|
| Tier | T1 Desk-USB |
| Hardware | FC board with TMP112 faces fitted, UART GDS; room ambient Ta noted (from face0 Temperature) |
| Image / flash | Current image, NORMAL |
| Preconditions | Face switches ON; `SET_LEVEL 3`; `telemetryDelay.DIVIDER_PRM_SET 0` (2 s resolution needed); thresholds at defaults (-40/60 face, 5/60 batt) |
| Restore | `FACE_TEMP_UPPER_THRESHOLD_PRM_SET 60`, `FACE_TEMP_LOWER_THRESHOLD_PRM_SET -40`, batt thresholds 5/60; `DIVIDER_PRM_SET 29`; `SET_LEVEL 1`; face0 `TURN_ON` |
| Destructive | No |
| Duration | ~8 min |

## Procedure
1. Read `tmp112Face0Manager.Temperature` = Ta (steady over 10 s).
2. `FACE_TEMP_UPPER_THRESHOLD_PRM_SET (Ta - 1)`. Observable: exactly one `TemperatureAboveThreshold` per face <= 2 s; none more over the next 10 s (once).
3. `..._UPPER_THRESHOLD_PRM_SET (Ta + 2)` (inside the 3 C hysteresis). Observable: no event over 10 s (not re-armed).
4. `..._UPPER_THRESHOLD_PRM_SET (Ta + 5)` then `(Ta - 1)` again. Observable: `TemperatureAboveThreshold` once more <= 2 s (re-armed only after > 3 C clearance).
5. `FACE_TEMP_LOWER_THRESHOLD_PRM_SET (Ta + 1)`. Observable: one `TemperatureBelowThreshold` <= 2 s. Repeat the hysteresis pattern (steps 3-4 mirrored). Restore -40.
6. Repeat step 2 for `BATT_CELL_TEMP_UPPER_THRESHOLD` if battery sensors are fitted.
7. `RD.face0LoadSwitch.TURN_OFF`. Observable: face0 `Temperature` stops updating / `DeviceNotReady` on next sweep; face1 `Temperature` and `PicoTemperature` still update <= 2 s after each 1 Hz run (FD-L2-08 clause 2). `TURN_ON`.
8. Restore per header.

## Criteria
| ID | Criterion | Automated | Evidence |
|---|---|---|---|
| TM-L2-08 | Out-of-range temperature raises Above/Below once, re-armed only after 3 C hysteresis (Unit, passing); voltage/current thresholds [TBD] -> README | test_ThermalManager_Thresholds (4 cases) ; thermal_threshold_test.py::test_01 ; ::test_02 | Unit log; steps 2-5 event log |
| FD-L2-03 | Each detected fault -> WARNING within one evaluation (Unit, passing); board: TemperatureAboveThreshold <= 2 s; CommandLossFound: HP-07 step 8 | test_ThermalManager_Thresholds ; thermal_threshold_test.py::test_01 | Step 2 FSW event time vs parameter ack time |
| FD-L2-08 | Clause 1: a failed LoRa send is retried by loraRetry (Analysis: `Svc.ComRetry`, `instances.fpp:220`, `topology.fpp:207-214`). Clause 2: one failed I2C read does not stop the next 1 Hz cycle | manual | Step 7 channel timestamps; wiring citation |

## Why this verifies it
- TM-L2-08 / FD-L2-03: Level Unit is the authoritative proof (hysteresis and once-only are logic). The board steps add that the real parameter path and real sensor values drive the same logic, and they provoke the negative path three ways (above, below, inside hysteresis). Threshold moves are used instead of heating the board: the comparator sees the same inequality either way.
- FD-L2-08: clause 2 is observed by channels of *other* sensors continuing on schedule while one device faults, which is the requirement's observable. Clause 1 cannot be provoked on the bench without corrupting the radio driver; wiring inspection plus the ComRetry library test is the honest evidence, so README proposes Method "Analysis, Integration Test" wording via `--method`.
- Voltage/current thresholds do not exist in code and have no Mission Ops value: excluded.

## Known traps
- ThermalManager sweeps 5 face + 4 battery + pico (`ThermalManager.cpp` ~25-48), not 11; an unfitted sensor yields DeviceNotReady, not a threshold event.
- DIVIDER 0 is required for the 2 s windows; at 29 the event arrives on time but the channel evidence is 30 s coarse.
- Safe mode turns face switches OFF: run this group before HP-06/07 or re-enable faces first.
- Parameters are RAM-only until `PRM_SAVE_FILE`; do not save test thresholds.
