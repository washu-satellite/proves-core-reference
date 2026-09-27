# HP-02 Telemetry sources and health at full packet level (T1 Desk-USB)

| Field | Value |
|---|---|
| Tier | T1 Desk-USB (if the v5e does not run from USB alone, feed the bus from a fixed 8.0 V source; no voltage changes in this group) |
| Hardware | FC board with all fitted sensors (9 TMP112, pico temp, LSM6DSO/LIS2MDL, 2 INA219), UART GDS |
| Image / flash | Current image; mode NORMAL (`GET_CURRENT_MODE`) |
| Preconditions | Every face load switch ON (`RD.face<N>LoadSwitch.GET_IS_ON`, `TURN_ON` where OFF; run BEFORE HP-06/HP-07 which turn them off); `CdhCore.tlmSend.SET_LEVEL 6` (all packets); `telemetryDelay.DIVIDER` 29 (default) |
| Restore | `SET_LEVEL 1` |
| Destructive | No |
| Duration | ~5 min automated (105 s capture), ~10 min manual |

Windows (from `telemetry_sources_test.py`): period 30 s; ONE_PERIOD_WINDOW 45 s; WINDOW 105 s (3 periods + 15 s); spacing 25..35 s.

## Procedure
1. `GET_IS_ON` on each face switch; `TURN_ON` any that is OFF. Observable: `IsOn` response ON <= 5 s each.
2. `CdhCore.tlmSend.SET_LEVEL 6`. Observable: ack <= 10 s.
3. Capture all telemetry and events for 105 s (GDS channel history or `tlm_sampler` CSV). No commands during the window.
4. From the capture assert, per source: >= 1 update in 105 s (TM-L2-01); each of tmp112 face0-3,5 / batt1-4 `Temperature` and `picoTempManager.PicoTemperature` >= 1 update per 45 s with value in -40..125 C (TM-L2-05, CDH-4); `imuManager.MagneticField` >= 1 per 45 s with a non-zero axis (TM-L2-06, CDH-6); `ina219Sys/ina219Sol Voltage, Current`, all 10 temperature channels and `modeManager.CurrentMode` >= 1 update, zero `HLTH_PING_WARN/HLTH_PING_LATE` (CDH-9); consecutive `startupManager.BootCount` receipts spaced 30 +/-5 s over 3 periods (DH-L2-13); `commsBufferManager.NoBuffs` and `payloadBufferManager.NoBuffs` == 0 for both ComCcsds stacks (DH-L2-02); `modeManager.CurrentMode, SafeModeEntryCount, CurrentSafeModeReason`, `authenticate.RejectedPacketsCount` >= 1 per 45 s (FD-L2-09); `detumbleManager.Mode` and coil parameter channels >= 1 per 45 s (ADCS-L2-06); every item has non-zero FSW time and per-channel timestamps non-decreasing (TM-L2-03); every event's FSW timestamp within 5 s of GDS receipt time (FD-L2-04 clause 1).
5. `RD.tmp112Face0Manager.GetTemperature` and `RD.picoTempManager.GetPicoTemperature`. Observable: `Temperature` / `PicoTemperature` events <= 5 s, -40..125 C (CDH-2).
6. `GetTemperature` on each of the 9 TMP112 managers and `GetPicoTemperature`. Observable: each returns its Temperature event <= 5 s (CDH-31 functional clause).
7. `SET_LEVEL 1`.
8. Face rail wiring (needs face boards on the F4 connector J1 = mux channel 5 and the F5 connector J2 = mux channel 6; run on a board that has not already logged five `DeviceNotReady` from the channel-5 managers since boot, the event is `throttle 5`). `TURN_OFF` every face switch (`RD.face0..5LoadSwitch`). (a) `RD.face4LoadSwitch.TURN_ON` alone. Observable within 45 s: `tmp112Face5Manager.GetTemperature` gives a `Temperature` event, `veml6031Face5Manager.GetVisibleLight` a `VisibleLight` event, `drv2605Face5Manager.START` acks OK, and none of the three reports `DeviceNotReady`. (b) Every face switch OFF again, then `RD.face5LoadSwitch.TURN_ON` alone. Observable within 45 s: `veml6031Face6Manager` `VisibleLight` and `tmp112Face6Manager` `Temperature` events; `tmp112Face5Manager`, `veml6031Face5Manager` and `drv2605Face5Manager` each report `DeviceNotReady` (LoadSwitch-1). Turn every face switch back ON (step 1) before any later group.

## Criteria
| ID | Criterion | Automated | Evidence |
|---|---|---|---|
| TM-L2-01 | >= 1 update from each registered source in 70 s (105 s capture) | telemetry_sources_test.py::test_01_every_registered_source_updates | Step 3 capture, per-source first-receipt table |
| TM-L2-03 | Non-zero FSW time on every item; per-channel timestamps non-decreasing | telemetry_sources_test.py::test_02_timestamps_present_and_monotonic | Step 4 |
| TM-L2-05 | Each TMP112 and pico temperature >= 1 per 45 s, -40..125 C | telemetry_sources_test.py::test_03_thermal_channels | Step 4 |
| CDH-4 | face0 ON, level >= 5: face0 and pico temperatures >= 1 per 45 s in range | telemetry_sources_test.py::test_03_thermal_channels | Step 4 |
| TM-L2-06 | MagneticField >= 1 per 45 s, non-zero axis; GET_MAGNETIC_FIELD <= 3 s | imu_manager_test.py::test_03_get_magnetic_field ; telemetry_sources_test.py::test_01 | Step 4 + one GET_MAGNETIC_FIELD |
| CDH-6 | Same as TM-L2-06 | imu_manager_test.py::test_03_get_magnetic_field | Same |
| CDH-9 | 70 s: zero HLTH_PING_* events; ina219 x2, 10 temperatures, CurrentMode each update | telemetry_sources_test.py::test_05_scheduler_and_health | Step 4 |
| DH-L2-13 | BootCount >= 1 per 45 s, spacing 30 +/-5 s over 3 periods | telemetry_sources_test.py::test_07_downlink_spacing | Step 4 receipt times |
| DH-L2-02 | Both stacks' NoBuffs stay 0 over 70 s | telemetry_sources_test.py::test_06_buffer_pools_never_exhausted | Step 4 |
| FD-L2-09 | Mode/SafeModeEntryCount/CurrentSafeModeReason/RejectedPacketsCount >= 1 per 45 s | telemetry_sources_test.py::test_08_fault_status_channels | Step 4 |
| ADCS-L2-06 | detumbleManager.Mode and coil parameter channels >= 1 per 45 s at level 6 | telemetry_sources_test.py::test_01_every_registered_source_updates | Step 4 |
| FD-L2-04 | Clause 1 only: fault event FSW timestamp within 5 s of GDS receipt. Retention clause: no on-board log (see README) | manual | Step 4 event log with GDS receive times |
| CDH-2 | GetTemperature / GetPicoTemperature answer <= 5 s in -40..125 C | pico_temp_test.py::test_01_get_pico_temperature ; tmp112_test.py::test_01_get_temperature | Step 5 |
| CDH-31 | Functional clause: each of 9 TMP112 + pico returns Temperature <= 5 s of its Get. Schematic/BOM clause: Inspection, list TBD (README) | manual | Step 6 event log |
| LoadSwitch-1 | face4LoadSwitch alone: channel-5 managers (tmp112/veml6031/drv2605 Face5) answer within 45 s, no DeviceNotReady; face5LoadSwitch alone: veml6031Face6/tmp112Face6 answer within 45 s and the three channel-5 managers report DeviceNotReady | face_rail_wiring_test.py::test_01_face4_switch_powers_mux_channel_5 ; face_rail_wiring_test.py::test_02_face5_switch_powers_mux_channel_6_only | Step 8 |

## Why this verifies it
- TM-L2-01/05/06, CDH-4/6/9, FD-L2-09, ADCS-L2-06: the requirement's observable is "telemetry received on the ground"; a 105 s window at level 6 covers three packetizer runs, so a missing source is a real absence, not a phase effect. Values from real sensors (range check, non-zero axis) rule out placeholder writes.
- TM-L2-03, FD-L2-04(1): timestamps come from the FSW time source; comparing to GDS receipt time is an oracle outside the packetizer.
- DH-L2-13: spacing 30 +/-5 s over 3 periods measures the scheduled downlink cadence itself (RateDelay 29), not merely presence.
- DH-L2-02: NoBuffs is a library counter independent of the components that consume buffers; static pool sizes are cited from `ComCcsdsConfig.fpp`.
- CDH-2/31: per-sensor Get commands prove each device answers on its own bus address after power-on. CDH-31's Inspection clause is not covered here.
- TM-L2-07 / CDH-18 (electrical values) need a known bus voltage: HP-11.
- LoadSwitch-1: with exactly one face rail ON, a device that answers can only be powered by that rail, so the step confirms the netlist reading (F4 connector = mux channel 5, F5 connector = mux channel 6) on the bench rather than trusting the topology that encodes it.

## Known traps
- Level 1 = Beacon only; forgetting `SET_LEVEL 6` fails every non-Beacon assertion. Restore level 1 or later groups see extra link load.
- TlmPacketizer sends on change; a constant channel (e.g. `CurrentMode`) still re-sends each run, but `WatchdogTransitions`/`CurrBuffs` are omitted channels and never appear.
- Safe mode (HP-06/07/10) turns faces OFF and `loadSwitchTurnOn` is unwired: re-run step 1 after any safe-mode entry or thermal channels vanish.
- `startup.seq` ID-filters `QueueOverflow` and `RateGroupCycleSlip` events at boot; use the channels, not events, for health.
