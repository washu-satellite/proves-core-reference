# HP-11 Electrical telemetry, power monitor and actuators (T2 Desk-PSU)

| Field | Value |
|---|---|
| Tier | T2 Desk-PSU (CI `integration-uart` rig layout; all tests here are `uart_only`) |
| Hardware | FC board with 2 INA219, DRV2605 coil drivers with coils (or dummy loads), burnwire heater with NO deployment wire fitted (or dummy resistor), UART GDS, PSU 8.0 V with current limit sized for burnwire |
| Image / flash | Current image, NORMAL |
| Preconditions | `SET_LEVEL 5`; `telemetryDelay.DIVIDER_PRM_SET 0` (1 s power resolution); `detumbleManager.SET_MODE DISABLED`; antennaDeployer params at defaults noted |
| Restore | `DIVIDER_PRM_SET 29`; `SET_LEVEL 1`; `antennaDeployer` `MAX_DEPLOY_ATTEMPTS`/`RETRY_DELAY` back to defaults; detumbleManager to OPERATING_MODE; verify `STOP_BURNWIRE` acked |
| Destructive | Yes (burnwire and coils energised; brownout possible on a weak supply) |
| Duration | ~15 min automated, ~25 min manual |

## Procedure
1. Capture 45 s. Observable: `ina219Sys/ina219Sol Voltage, Current` each >= 1 update; sys voltage > 6.7 and < 9 V; `powerMonitor.TotalPowerConsumption` strictly increases across the window (TM-L2-07, CDH-18 clause 1).
2. `RD.powerMonitor.GET_TOTAL_POWER` twice, 10 s apart. Observable: `TotalPowerConsumptionReading` > 0, second strictly greater (PWR-MON-REQ-001/004).
3. Baseline `ina219Sys` power P0 (V x I over 5 s). `RD.drv2605Face0Manager.START 127`. Observable: power >= P0 + 0.3 W within 1 s (DIVIDER 0); `STOP` acked OK, power returns to ~P0 (DRV-008).
4. `RD.detumbleManager.SET_MODE AUTO`. Observable: OK; `detumbleManager.Mode` AUTO <= 45 s. Rotate the board by hand faster than 8 deg/s about one axis (one turn in < 45 s; watch `imuManager` gyro channel > 8). Observable: >= 1 coil Start (any `drv2605FaceNManager`) with power rise >= 0.3 W within 2 s of rate exceeding DEADBAND_UPPER (8.0) (ADCS-L2-01). Stop rotating; `SET_MODE DISABLED`.
5. `RD.burnwire.START_BURNWIRE`. Observable: `SetBurnwireState(ON)` <= 2 s; sys power > 1 W. `STOP_BURNWIRE`: `SetBurnwireState(OFF)` <= 2 s and `BurnwireEndCount` (BW-002, BW-003).
6. `antennaDeployer` `MAX_DEPLOY_ATTEMPTS_PRM_SET 1`, burn 1 s (per `antenna_deployer_test.py:43-52`). `DEPLOY`. Observable: `DeployAttempt(1)` <= 5 s; `SetBurnwireState` ON then OFF; `DeployFinish(FAILED, 1)` (no distance sensor) (AD0002, AD0005).
7. `MAX_DEPLOY_ATTEMPTS_PRM_SET 3`, `RETRY_DELAY` 1 s. `DEPLOY`. Observable: `DeployAttempt` 1, 2, 3 each followed by ON/OFF; `DeployFinish` attempts == 3 (AD0003, AD0005).
8. DEPLOY2 (`burnwireDeploy2`, TPS4H160 OUT3 on J24 pins 1-2). Fit a DUMMY LOAD on J24, never a deployment wire, and set `PROVES_J24_DUMMY_LOAD=1` in the test environment (the test is skipped without it). Baseline `ina219Sys` power P0. `RD.burnwireDeploy2.START_BURNWIRE`. Observable: `burnwireDeploy2.SetBurnwireState(ON)` <= 2 s; sys power >= P0 + 0.3 W. `RD.burnwireDeploy2.STOP_BURNWIRE`: `SetBurnwireState(OFF)` <= 2 s and `BurnwireEndCount`. Then `START_BURNWIRE` without STOP: `SetBurnwireState(OFF)` 8.5-11 s (FSW event time) after the ON event (`SAFETY_TIMER` 10 s) (BW-009).
9. CHARGE (`powerMonitor.Charging`, LT3652 `~CHRG` on MCP23017 GPB7). Battery fitted; a bench supply on VSOLAR set above the pack voltage, switched by the shell commands in `PROVES_VSOLAR_ON_CMD` / `PROVES_VSOLAR_OFF_CMD` (the test is skipped unless both are set). Supply OFF, wait 3 s, `CdhCore.tlmSend.SET_LEVEL 2`. Supply ON. Observable: `powerMonitor.ChargeStateChanged(ON)` <= 2 s, no second `ChargeStateChanged` in the next 5 s, `powerMonitor.Charging` downlinks ON within 45 s. Supply OFF. Observable: `ChargeStateChanged(OFF)` <= 2 s, exactly one, `Charging` OFF (PWR-MON-REQ-012). The automated test sets packet level 1 on exit.
10. Restore per header.

## Criteria
| ID | Criterion | Automated | Evidence |
|---|---|---|---|
| TM-L2-07 | ina219 Sys/Sol V and I >= 1 per 45 s; 6.7 < sys V < 9; TotalPowerConsumption increases across 45 s | telemetry_sources_test.py::test_04_electrical_channels | Step 1 |
| CDH-18 | Clause 1 as TM-L2-07; per-panel/battery/bus/component sensors: only 2 INA219 fitted, list TBD (README) | telemetry_sources_test.py::test_04 (clause 1) | Step 1 |
| PWR-MON-REQ-001 | GET_TOTAL_POWER > 0; second reading 10 s later strictly greater | power_monitor_test.py::test_02_total_power_consumption | Step 2 |
| PWR-MON-REQ-004 | Same observable (requires a system power request each 1 Hz run) | power_monitor_test.py::test_02_total_power_consumption | Step 2 |
| DRV-008 | START 127 raises ina219Sys power >= 0.3 W within 1 s over baseline; STOP OK | drv2605_test.py::test_01_magnetorquer_power_draw | Step 3 |
| ADCS-L2-01 | SET_MODE AUTO OK, Mode AUTO <= 45 s; rotation > 8 deg/s -> coil Start (power >= 0.3 W) within 2 s | manual (Demonstration) | Step 4 gyro channel + power trace |
| BW-002 | START -> ON <= 2 s and power > 1 W; STOP -> OFF <= 2 s and BurnwireEndCount | burnwire_test.py::test_01_start_and_stop_burnwire | Step 5 |
| BW-003 | ON/OFF events <= 2 s of START/STOP | burnwire_test.py::test_01_start_and_stop_burnwire | Step 5 |
| AD0002 | attempts=1, burn 1 s: DeployAttempt(1) <= 5 s, ON then OFF, DeployFinish(FAILED, 1) | antenna_deployer_test.py::test_deploy_without_distance_sensor | Step 6 |
| AD0003 | attempts=3, delay 1 s: DeployAttempt 1,2,3 each with ON/OFF | antenna_deployer_test.py::test_multiple_deploy_attempts | Step 7 |
| AD0005 | DeployFinish attempts == number of DeployAttempt events (1 and 3) | antenna_deployer_test.py (both) | Steps 6-7 |
| BW-009 | Dummy load on J24: START -> ON <= 2 s and sys power >= 0.3 W over the pre-START reading; STOP -> OFF <= 2 s and BurnwireEndCount; unstopped START -> OFF 8.5-11 s after ON | burnwire_deploy2_test.py::test_01_start_and_stop_deploy2 ; burnwire_deploy2_test.py::test_02_safety_timer_stops_deploy2 | Step 8 |
| PWR-MON-REQ-012 | VSOLAR supply on -> ChargeStateChanged(ON) <= 2 s, Charging ON; off -> ChargeStateChanged(OFF) <= 2 s, Charging OFF; exactly one event per transition | charge_status_test.py::test_01_charging_follows_vsolar_supply | Step 9 |

## Why this verifies it
- TM-L2-07 / PWR-MON: values are bounded by the known PSU setting (independent oracle) and the accumulator's monotonic growth ties powerMonitor to a per-cycle measurement rather than a cached value.
- DRV-008, BW-002, ADCS-L2-01: the actuator effect is measured on the power rail by the INA219, not by the actuator's own event; the burnwire's > 1 W and the coil's >= 0.3 W are physically distinct from idle. For ADCS-L2-01 the stimulus (rate > 8 deg/s) is read from the IMU channel so the coil Start is tied to the deadband, not to the mode command.
- BW-003, AD0002/03/05: event ordering and counts are observed from burnwire (a separate component from antennaDeployer), so the deployer's attempt count is cross-checked against actual burn cycles.
- CDH-18 clause 2 needs Mission Ops' sensor list.
- BW-009: the DEPLOY2 load current is measured by `ina219Sys`, independent of `burnwireDeploy2`'s own events; the safety-timer bound is timed from the FSW event timestamps.
- PWR-MON-REQ-012: the stimulus (VSOLAR supply switched by the operator's command) is outside the flight software; the charger IC drives `~CHRG`, so `Charging` following it ties the channel to the pin's real polarity (active low in the devicetree).

## Known traps
- `burnwire`/`antenna_deployer`/`drv2605` tests are `uart_only` and brownout-prone: keep the PSU current limit above the burnwire draw or HP-10's low-battery path fires mid-test (`recover_from_safe_mode` only runs with `--with-radio`).
- DIVIDER 0 is required for the 1 s / 2 s power-rise windows; restore 29.
- `detumbleManager` reads the IMU at 50 Hz; in AUTO it will torque whenever the rate exceeds 8 deg/s, including during handling. Set DISABLED before steps 3, 5-7.
- Parameters are RAM-only until `PRM_SAVE_FILE`; do not save attempts=3.
- Step 8 fires DEPLOY2 for real: J24 must carry a dummy load, never a deployable; `PROVES_J24_DUMMY_LOAD=1` is the operator's statement that it does.
- Step 9's supply must be above the pack voltage or the LT3652 never charges and `Charging` stays OFF; a full pack can also end charging early (`~CHRG` releases at C/10).
- The flight `startup.seq` fires `antennaDeployer.DEPLOY` 45 min after every boot (and sets detumble AUTO at +5 min): select the bench sequence first, see [Bench preconditions](README.md#bench-preconditions).
