# Hardware verification procedures for the deferred CDH rows (index)

Input: `scratchpad/deferred-rows.md` (106 rows). 87 rows are placed in 14 groups; 19 rows are unmeasurable as written (Needs Mission Ops). One file per group; read the group file for steps, criteria and the verification argument. `RD.` = `ReferenceDeployment.`. Repo is read-only for this plan; `req.py` commands below are listed, not run.

## Tier summary
| Tier | Groups | Rows | Hardware needed | Rough time |
|---|---|---|---|---|
| T1 Desk-USB | HP-01..HP-09 | 61 | one v5e FC, UART GDS, Pico Debug Probe optional; sensors fitted | ~2 h (HP-07 destructive: 3 reboots) |
| T2 Desk-PSU | HP-10, HP-11, HP-12 | 19 | T1 + scriptable bench PSU on the ina219Sys rail (CI Korad layout), coils/burnwire dummy loads | ~1.5 h |
| T3 Bench-RF | HP-13, HP-14 | 7 | T1 + passthrough board as RF observer/second GDS (`integration-radio` layout) | 10 min + 96 h soak |
| T4 Field-RF | none | 0 | no deferred row needs link margin or PER vs RSSI | - |
| T5 Flatsat | HP-15 | 4 | T1 + the STM32 driver board on J18 pins 9/10 and the payload rail, coils or dummy loads (Cycle E); part A of HP-15 (loopback jumper) is T1. Still unplaceable: algorithm upload (CDH-17/20, no STM32 reflash path), ADCS divergence (CDH-28, FD-L2-07); MS-L2-05 real-load evidence would be added here | ~20 min (part C destructive: 1 warm + 5 cold resets) |
| T6 Environmental | none | 0 | all Environmental rows carry [TBD by Mission Ops] values or are not implemented | - |

## Full mapping
| ID | Group | Method (final) | Level (final) | Automated test or manual |
|---|---|---|---|---|
| CDH-1 | HP-01 | Integration Test (was Demonstration) | Board | command_path_test.py::test_05_uart_command_and_telemetry_without_radio |
| CDH-2 | HP-02 | Integration Test (was Demonstration) | Board | pico_temp_test.py::test_01 ; tmp112_test.py::test_01 |
| CDH-3 | HP-13 | Integration Test | Board | manual |
| CDH-4 | HP-02 | Integration Test | Board | telemetry_sources_test.py::test_03_thermal_channels |
| CDH-5 | HP-04 | Integration Test | Board | manual (mirror collection_interval_test on imuManager) |
| CDH-6 | HP-02 | Integration Test | Board | imu_manager_test.py::test_03_get_magnetic_field |
| CDH-7 | Mission Ops | Demonstration | Board | - |
| CDH-8 | HP-13 | Integration Test | Board | manual |
| CDH-9 | HP-02 | Integration Test | Board | telemetry_sources_test.py::test_05_scheduler_and_health |
| CDH-10 | HP-06 | Integration Test | Board | safe_mode_test.py::test_safe_03_exit_clears_reason |
| CDH-11 | HP-10 | Integration Test | Board | manual |
| CDH-13 | HP-09 | Integration Test | Board | rtc_test.py::test_05 (alarm) ; manual (sequence) |
| CDH-14 | HP-09 | Integration Test | Board | manual |
| CDH-15 | HP-07 | Integration Test | Board | thermal_threshold_test.py::test_01 (thermal) ; manual |
| CDH-16 | Mission Ops | Integration Test | Board | - (no on-board event log) |
| CDH-17 | Mission Ops | Demonstration | Flatsat | - (payload not interfaced) |
| CDH-18 | HP-11 | Integration Test | Board | telemetry_sources_test.py::test_04_electrical_channels (clause 1) |
| CDH-19 | HP-01 | Integration Test | Board | command_path_test.py::test_01_no_op_string_round_trip (+HP-13 RF) |
| CDH-20 | Mission Ops | Demonstration | Flatsat | - |
| CDH-21 | Mission Ops | Integration Test | Environmental | - |
| CDH-22 | Mission Ops | Analysis | Environmental | - |
| CDH-23 | Mission Ops | Analysis | Environmental | - |
| CDH-24 | Mission Ops | Demonstration | Environmental | - |
| CDH-25 | Mission Ops | Analysis | Environmental | - |
| CDH-26 | Mission Ops | Demonstration | Environmental | - |
| CDH-27 | HP-14 ; HP-15 | Demonstration | Board | manual (96 h CSV) ; driver_board_test.py::test_03_hk_channels_update (flatsat; the packet exists, cadence is ops) |
| CDH-28 | Mission Ops | Integration Test | Flatsat | - |
| CDH-29 | Mission Ops | Demonstration | Environmental | - |
| CDH-30 | Mission Ops | Demonstration | Environmental | - |
| CDH-31 | HP-02 | Inspection, Integration Test (was Inspection) | Board | manual (functional clause); BOM list TBD |
| CH-L2-01 | HP-13 | Integration Test | Board | command_path_test.py::test_01 (both CI jobs) |
| CH-L2-02 | HP-01 | Integration Test | Board | command_path_test.py::test_02_ten_commands_no_deframer_errors |
| CH-L2-03 | HP-01 | Integration Test | Board | command_path_test.py::test_02_ten_commands_no_deframer_errors |
| CH-L2-05 | HP-01 | Integration Test | Board | authentication_test.py::test_01_stale_sequence_number_is_rejected |
| CH-L2-06 | HP-01 | Integration Test | Board | authentication_test.py::test_01_stale_sequence_number_is_rejected |
| CH-L2-07 | HP-01 | Integration Test | Board | command_path_test.py::test_01_no_op_string_round_trip |
| CH-L2-08 | HP-01 | Integration Test | Board | command_path_test.py::test_01_no_op_string_round_trip |
| CH-L2-09 | HP-01 | Integration Test | Board | command_path_test.py::test_03_burst_of_five_commands |
| CH-L2-10 | HP-01 | Integration Test | Board | command_path_test.py::test_04_routing_to_three_components |
| CH-L2-11 | HP-09 | Integration Test | Board | manual |
| CH-L2-12 | HP-05 | Integration Test | Board | telemetry_gate_test.py::test_02 |
| CH-L2-13 | HP-13 | Integration Test | Board | manual |
| CH-L2-14 | HP-13 | Integration Test | Board | manual |
| CH-L2-15 | HP-05 | Integration Test | Board | telemetry_gate_test.py::test_03_disable_takes_effect_within_one_period |
| CH-L2-16 | HP-05 | Integration Test | Board | telemetry_gate_test.py::test_04_gated_ticks_count_and_state_after_reenable |
| TM-L2-01 | HP-02 ; HP-15 (payload clause) | Integration Test | Board | telemetry_sources_test.py::test_01_every_registered_source_updates ; driver_board_test.py::test_03_hk_channels_update (flatsat) |
| TM-L2-02 | HP-04 | Integration Test | Board | collection_interval_test.py::test_01 ; ::test_03 |
| TM-L2-03 | HP-02 | Integration Test | Board | telemetry_sources_test.py::test_02_timestamps_present_and_monotonic |
| TM-L2-05 | HP-02 | Integration Test | Board | telemetry_sources_test.py::test_03_thermal_channels |
| TM-L2-06 | HP-02 | Integration Test | Board | imu_manager_test.py::test_03 ; telemetry_sources_test.py::test_01 |
| TM-L2-07 | HP-11 | Integration Test | Board | telemetry_sources_test.py::test_04_electrical_channels |
| TM-L2-08 | HP-08 | Unit Test | Unit | test_ThermalManager_Thresholds (passing) ; thermal_threshold_test.py::test_01/02 |
| TM-L2-09 | HP-05 | Integration Test | Board | telemetry_gate_test.py::test_03 (clause 1); clause 2 not implemented |
| DH-L2-01 | HP-13 | Integration Test | Board | manual |
| DH-L2-02 | HP-02 | Integration Test | Board | telemetry_sources_test.py::test_06_buffer_pools_never_exhausted |
| DH-L2-05 | Mission Ops | Integration Test | Board | - (not implemented) |
| DH-L2-07 | HP-09 | Integration Test | Board | manual |
| DH-L2-08 | Mission Ops | Integration Test | Board | - (no on-board telemetry store) |
| DH-L2-11 | HP-12 | Integration Test | Board | unit suite + manual power-cut loop |
| DH-L2-12 | Mission Ops | Integration Test | Board | - (no on-board event log) |
| DH-L2-13 | HP-02 | Integration Test | Board | telemetry_sources_test.py::test_07_downlink_spacing |
| SC-L2-01 | HP-03 | Integration Test | Board | telemetry_sources_test.py::test_05_scheduler_and_health |
| SC-L2-03 | HP-09 | Integration Test | Board | manual (relative tag); absolute tag remainder |
| SC-L2-04 | HP-03 | Integration Test | Board | telemetry_sources_test.py::test_05_scheduler_and_health |
| SC-L2-05 | HP-03 | Analysis | Board | manual (10 min capture); headroom % TBD |
| SC-L2-07 | HP-07 | Integration Test | Board | watchdog_test.py::test_01/test_03 ; telemetry_gate_test.py::test_02 ; generic task cmd not implemented |
| MS-L2-01 | HP-06 | Integration Test | Board | safe_mode_test.py::test_safe_03 (2 modes); other modes TBD |
| MS-L2-02 | HP-06 | Integration Test | Board | safe_mode_test.py::test_safe_03_exit_clears_reason |
| MS-L2-03 | HP-06 | Integration Test | Board | safe_mode_test.py::test_safe_11_detumble_disabled_in_safe_mode |
| MS-L2-04 | HP-10 | Integration Test | Board | manual |
| MS-L2-05 | HP-06 | Integration Test | Board | safe_mode_test.py::test_safe_10_safe_mode_turns_off_load_switches |
| MS-L2-06 | HP-06 | Integration Test | Board | safe_mode_test.py::test_safe_02 ; ::test_safe_11 |
| MS-L2-07 | HP-07 | Integration Test | Board | safe_mode_test.py::test_safe_09_mode_persists_across_warm_reset |
| MS-L2-09 | HP-10 | Integration Test | Board | safe_mode_test.py::test_safe_03 (GROUND_COMMAND) ; manual |
| FD-L2-01 | HP-10 | Integration Test | Board | thermal_threshold_test.py::test_01 (thermal) ; manual |
| FD-L2-03 | HP-08 | Unit Test | Unit | test_ThermalManager_Thresholds (passing) ; thermal_threshold_test.py::test_01 |
| FD-L2-04 | HP-02 | Integration Test | Board | manual (timestamp clause); retention not implemented |
| FD-L2-05 | HP-10 | Integration Test | Board | watchdog_test.py::test_03_system_reboots_without_watchdog ; manual |
| FD-L2-06 | HP-10 | Integration Test | Board | manual |
| FD-L2-07 | Mission Ops | Integration Test | Flatsat | - |
| FD-L2-08 | HP-08 | Analysis, Integration Test (was Demonstration) | Board | manual |
| FD-L2-09 | HP-02 | Integration Test | Board | telemetry_sources_test.py::test_08_fault_status_channels |
| ADCS-L2-01 | HP-11 | Demonstration | Board | manual |
| ADCS-L2-03 | Mission Ops | Analysis | Environmental | - |
| ADCS-L2-04 | Mission Ops | Integration Test | Board | - (not implemented) |
| ADCS-L2-06 | HP-02 ; HP-15 | Integration Test | Board | telemetry_sources_test.py::test_01_every_registered_source_updates ; driver_board_test.py::test_03_hk_channels_update (flatsat) |
| AD0002 | HP-11 | Integration Test | Board | antenna_deployer_test.py::test_deploy_without_distance_sensor |
| AD0003 | HP-11 | Integration Test | Board | antenna_deployer_test.py::test_multiple_deploy_attempts |
| AD0005 | HP-11 | Integration Test | Board | antenna_deployer_test.py (both) |
| BW-002 | HP-11 | Integration Test | Board | burnwire_test.py::test_01_start_and_stop_burnwire |
| BW-003 | HP-11 | Integration Test | Board | burnwire_test.py::test_01_start_and_stop_burnwire |
| DRV-008 | HP-11 | Integration Test | Board | drv2605_test.py::test_01_magnetorquer_power_draw |
| DriverBoardHandler-10 | HP-15 | Integration Test | Flatsat | driver_board_test.py::test_09_end_to_end (flatsat); steps B1-B10 |
| ImuManager-1 | HP-04 | Integration Test | Board | manual |
| MM0001 | HP-06 | Integration Test | Board | safe_mode_test.py::test_safe_03_exit_clears_reason |
| MM0002 | HP-06 | Integration Test | Board | safe_mode_test.py::test_safe_02_ground_command_sets_reason |
| MM0004 | HP-06 | Integration Test | Board | safe_mode_test.py::test_safe_04_no_auto_recovery_for_ground_command |
| MM0005 | HP-06 | Unit Test, Integration Test | Board | test_ModeManager_VoltageDebounce ; safe_mode_test.py::test_safe_10 |
| MM0007 | HP-07 | Integration Test | Board | safe_mode_test.py::test_safe_09_mode_persists_across_warm_reset |
| PersistedRecord-5 | HP-12 | Integration Test (was Test) | Board | manual (scripted power-cut loop) |
| PWR-MON-REQ-001 | HP-11 | Integration Test | Board | power_monitor_test.py::test_02_total_power_consumption |
| PWR-MON-REQ-004 | HP-11 | Integration Test | Board | power_monitor_test.py::test_02_total_power_consumption |
| TelemetryGate-1 | HP-05 | Unit Test (was Unit Test, Integration Test) | Unit (was Board) | test_TelemetryGate_Component (passing) ; telemetry_gate_test.py::test_02 as evidence |
| TelemetryGate-2 | HP-05 | Unit Test, Integration Test | Board | test_TelemetryGate_Component::DisabledDropsTicksAndCountsThem ; telemetry_gate_test.py::test_02 |
| TelemetryGate-3 | HP-05 | Unit Test (was Unit Test, Integration Test) | Unit (was Board) | test_TelemetryGate_Component::EnabledForwardsEveryTick ; telemetry_gate_test.py::test_01 as evidence |
| ThermalManager-1 | HP-04 | Unit Test | Unit | test_ThermalManager_CollectionInterval ; collection_interval_test.py::test_01/03 |
| ThermalManager-2 | HP-04 | Unit Test | Unit | test_ThermalManager_CollectionInterval ; collection_interval_test.py::test_02 |

## Proposed `req.py set` commands (Method/Level changes; run from `$R`, not run here)
```
fprime-venv/bin/python3 scripts/req.py set CDH-1 --method "Integration Test" --level Board
fprime-venv/bin/python3 scripts/req.py set CDH-2 --method "Integration Test" --level Board
fprime-venv/bin/python3 scripts/req.py set CDH-31 --method "Inspection, Integration Test" --level Board
fprime-venv/bin/python3 scripts/req.py set FD-L2-08 --method "Analysis, Integration Test" --level Board
fprime-venv/bin/python3 scripts/req.py set PersistedRecord-5 --method "Integration Test" --level Board
fprime-venv/bin/python3 scripts/req.py set TelemetryGate-1 --method "Unit Test" --level Unit
fprime-venv/bin/python3 scripts/req.py set TelemetryGate-3 --method "Unit Test" --level Unit
fprime-venv/bin/python3 scripts/req.py set CDH-5 --criteria "With detumbleManager DISABLED, after imuManager.COLLECTION_INTERVAL_S_PRM_SET N (1..60), imuManager.CollectionIntervalS reads N and MagneticField updates are spaced N +/-1 s over 5 consecutive updates (telemetryDelay.DIVIDER 0, level 6)"
```
Rationale: CDH-1/2 have numeric, automated criteria (Test, not Demonstration). TelemetryGate-1/3 observables (next tick, runOut count, context) exist only at unit; board tests are supplementary. FD-L2-08 clause 1 is library wiring (Analysis). CDH-5's bracketed note is stale (`ImuManager.fpp:86`).

## Needs Mission Ops (not placed in a procedure)
- TBD numeric value: CDH-7 (illumination ratio), CDH-21 (detumble minutes), CDH-22 (hold duration), CDH-23 (pointing error/duration), ADCS-L2-03 (degrees), CDH-28 and FD-L2-07 (divergence warning seconds; also not implemented).
- Not implemented, criterion cannot be exercised: CDH-24, CDH-25, CDH-26, CDH-29, CDH-30 (pointing/settle/coil hold, all "may"); CDH-17, CDH-20 (payload not interfaced); ADCS-L2-04 (no attitude estimate); DH-L2-05 (no buffer-size command); DH-L2-08 (no on-board telemetry store); CDH-16, DH-L2-12 (no on-board event log).
- Placed but with a Mission Ops remainder: CDH-18 (sensor list), CDH-31 (critical-component list), SC-L2-05 (headroom %), TM-L2-08 (voltage/current thresholds), MS-L2-01 (STANDBY/CALIBRATION/EXPERIMENT), TM-L2-09 and SC-L2-07 (per-subsystem/generic task commands: design TBD), FD-L2-04 (retention), MS-L2-09 (SYSTEM_FAULT entry if HP-10 step D shows it is not provokable).

## Recommended execution order
1. T1: HP-01 (command path) -> HP-02, HP-03 (sources, scheduler; same setup) -> HP-04 (interval) -> HP-08 (thresholds) -> HP-09 (files/seq/RTC) -> HP-05 (gate) -> HP-06 (modes) -> HP-07 (reboots, last: destructive, turns faces off). Unlocks 61 rows.
2. T2 and T3 in parallel on two rigs: T2 HP-11 (electrical/actuators) -> HP-10 (faults) -> HP-12 (power cut): 19 rows. T3 HP-13 (RF silence/buffering, 10 min) then start HP-14 (96 h soak): 7 rows.
3. T4: nothing in this set. T5: revisit MS-L2-05 with real loads and CDH-17/20 once the payload is interfaced. T6: only after Mission Ops supplies the values above.

## Recording results
The RTM (`make rtm` / `scripts/generate_rtm.py`) only sees automated tests via `@pytest.mark.verifies`; rows whose group says "manual" never change status by themselves. After a manual procedure, record the outcome per TP-7/TP-8: `fprime-venv/bin/python3 scripts/req.py set <ID> --status "Pass" --reason "HP-nn step k, <evidence file>, <date>, <operator>"` (or `--status "Fail" --reason ...`), keeping the evidence file (GDS event/channel export, PSU log, byte-logger capture, CSV) alongside the procedure ID. Criteria must already be set before a status is claimed; where a group tightened a criterion (CDH-5, CDH-31, FD-L2-08) set `--criteria` first.
