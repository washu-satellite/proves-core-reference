# HP-15 Driver-board link: loopback smoke, end-to-end with the STM32, parameter persistence (T5 Flatsat; part A is T1 Desk-USB)

| Field | Value |
|---|---|
| Tier | T5 Flatsat for parts B and C (the STM32 driver board answering `cycle-e-plan/02-protocol.md`); part A (loopback) is T1 Desk-USB and needs no driver board |
| Hardware | FC V5e on UART GDS; driver board on J18: pin 9 = TX1 (GPIO4, host to board), pin 10 = RX1 (GPIO5, board to host), plus GND; board powered from the payload rail (`PAYLOAD_PWR`, J18 pins 1-2), switched by `RD.payloadPowerLoadSwitch`. Part A instead: a jumper between J18 pin 9 and pin 10, nothing on the payload rail. Part C: nothing on J18 is needed |
| Image / flash | Cycle E image (feat/driver-board at or after 387374b8: `driverBoardHandler` in the dictionary, packet `PayloadHousekeeping` id 23); NORMAL mode |
| Preconditions | `CdhCore.tlmSend.SET_LEVEL 3` (the packet is group 3); `RD.telemetryDelay.DIVIDER_PRM_SET 0`; the five `driverBoardHandler` parameters at defaults (2000 / 50 / 0x07 / 1000 / 1); payload rail OFF before part B |
| Restore | `RD.driverBoardHandler.DISARM`; `RD.payloadPowerLoadSwitch.TURN_OFF`; `PULSE_DURATION_MS_PRM_SET 2000`; `DIVIDER_PRM_SET 29`; `SET_LEVEL 1`. Part C additionally: `FACE_TEMP_UPPER_THRESHOLD_PRM_SET 60`, `FACE_TEMP_UPPER_THRESHOLD_PRM_SAVE`, `FileHandling.prmDb.PRM_SAVE_FILE` so the saved file holds the flight value again |
| Destructive | Part C: yes (one warm reset, one cold reset, and `/prmDb.dat` is rewritten). Parts A and B: no |
| Duration | A ~2 min; B ~5 min; C ~10 min |

Windows (all from `test/int/driver_board_test.py`, derived there): PONG 2 s; coil current 2 s after `PulseStarted`; `LinkLost` `silentMs` <= `LINK_TIMEOUT_MS` + 1000 = 2000 ms; link-up after power-on <= 3 s board boot + 1 s HK_REQUEST period + 1 s tick + 5 s delivery = 10 s; HK update at least once per 45 s; ground delivery margin 5 s on every flight-side window. The packetizer is on-change: while the board answers HK, `BoardUptime` changes every second and the whole 22-channel packet is downlinked every tick; with the board off (or the jumper out) nothing changes and no packet is sent, so read a one-off state change from the event log and the packet that follows it, not from "the next packet".

## Bench setup
1. FC on UART GDS, sequence number synced, `SET_LEVEL 3`, `DIVIDER_PRM_SET 0`. Confirm the dictionary lists `RD.driverBoardHandler.PING` (Cycle E image).
2. Part A only: jumper J18 pin 9 to pin 10. Nothing on the payload rail; leave `payloadPowerLoadSwitch` OFF.
3. Parts B and C: remove the jumper. Driver board TX to J18 pin 10, RX to J18 pin 9, GND to J18 GND, power from `PAYLOAD_PWR` (J18 1-2). Coils (or dummy loads on the three channels, S6 sense path 0.1 ohm) connected so a pulse draws measurable current. Payload rail OFF until step B1.
4. Ops sequences exist for the power cycle: `sequences/payload_on.seq` (`payloadPowerLoadSwitch.TURN_ON`, 2 s, `driverBoardHandler.PING`; B1 + B3) and `sequences/payload_off.seq` (`driverBoardHandler.DISARM`, 1 s, `payloadPowerLoadSwitch.TURN_OFF`; the restore). Compile with `fprime-seqgen sequences/<name>.seq -d <build-artifacts>/zephyr/fprime-zephyr-deployment` (`make sequence SEQ=<name>` where `make` works). This procedure sends the commands one at a time so each observable is timed against its own command.

## Part A: loopback smoke (no driver board) — `driver_board_test.py::test_00_loopback_smoke`
A1. With the jumper in, watch the `PayloadHousekeeping` channels. Observable: `RD.driverBoardHandler.FramesReceived` increases by one per second (the host hears its own 7-byte HK_REQUEST); `FramesRejected` stays 0; `LinkState` stays DOWN (a host-type frame is not a board frame: `DriverBoardHandler.cpp` `handleFrame`, `BOARD_TO_HOST_BIT` clear); no `LinkUp`, no `FrameRejected` event.
A2. Leave it running 60 s. Observable: `FramesReceived` climbs by about 60; no `RateGroupCycleSlip`; the 1 Hz and 50 Hz rate groups keep running (this is the first live test of the lock discipline in the review's amendment 2: bytes arrive on the 50 Hz thread while the 1 Hz thread sends; a deadlock shows as a stalled `FramesReceived` and a stalled `startupManager.BootCount`-bearing Beacon).
A3. `RD.driverBoardHandler.PING`. Observable: command OK (the frame is sent; the driver is ready); `FramesReceived` +1 extra; no `PongReceived` (the host does not answer PING). Remove the jumper.

## Part B: the ten steps of DriverBoardHandler-10 (STM32 attached) — `driver_board_test.py::test_01..test_09`
B1. `RD.payloadPowerLoadSwitch.TURN_ON`. Observable: `LinkUp(version)` <= 10 s (the host has been sending DISARM once then HK_REQUEST every second since boot; the board's first HK reply brings the link up).
B2. Observable: `LinkState` UP and `DriverState` DISARMED in the next packet (the host's first frame after `uartReady` was DISARM, so a board left ARMED by an earlier session is disarmed here).
B3. `RD.driverBoardHandler.PING`. Observable: `PongReceived(fwVersion, protoVersion 1)` <= 2 s after the command completes (FSW timestamps); `FirmwareVersion` non-zero in the next packet.
B4. `RD.driverBoardHandler.ARM`. Observable: command OK (means "sent"); `Armed` on the board's ACK(ARM, 0) <= 5 s; `DriverState` ARMED.
B5. `RD.driverBoardHandler.PULSE_DURATION_MS_PRM_SET 500` (RAM-only; restored at the end).
B6. `RD.driverBoardHandler.PULSE 0`. Observable: `PulseStarted(500, 50, 7, 0)`; `PulsesCommanded` +1.
B7. Observable: `CoilCurrent0`, `CoilCurrent1`, `CoilCurrent2` non-zero with FSW timestamps <= 2 s after `PulseStarted` (mask 0x07 drives all three). Record the three values and the bench ammeter reading for S6.
B8. `RD.driverBoardHandler.DISARM`. Observable: `Disarmed(COMMAND)` immediately (local state, before the ACK); `DriverState` DISARMED.
B9. `RD.payloadPowerLoadSwitch.TURN_OFF`. Observable: `LinkLost(silentMs)` with `silentMs` <= 2000 (the criterion's `LINK_TIMEOUT_MS` + 1 s); `LinkState` DOWN in the packet that follows; no further `LinkLost` afterwards (one per loss).
B10. Observable: `LinkTimeouts` is exactly one higher than in B2; `FramesRejected` unchanged from B2 (record it if not: a byte glitch at power-off is a finding for the STM32 firmware, not a failure of this step). Restore per header.

Variant B11 (board evidence for DriverBoardHandler-5, `test_08`): repeat B1-B4, then B9. Observable: `LinkLost`, then `Disarmed(LINK_LOST)`, `DriverState` DISARMED; a `PULSE 0` now yields `CommandRefused(5, NOT_ARMED)` and no frame.

## Part C: parameter persistence gate (07-followups "Persistence gate", asked of PROVES 2026-09-18)
Nothing in `driverBoardHandler` changes with either outcome; this decides whether any CONOPS may assume a `PRM_SET` survives a reset. Read-back observable: `thermalManager` reads `FACE_TEMP_UPPER_THRESHOLD` live on every evaluation (`ThermalManager.cpp:100-101`, `paramGet_` per tick, not cached), so a saved threshold below ambient makes `TemperatureAboveThreshold` fire within 2 s of boot with no command sent. Note the ambient face temperature Ta first (HP-08 step 1).

C-a. Warm reset:
1. `RD.thermalManager.FACE_TEMP_UPPER_THRESHOLD_PRM_SET (Ta - 5)`. Observable: `PrmIdUpdated` from `FileHandling.prmDb`; `TemperatureAboveThreshold` per face <= 2 s.
2. `RD.thermalManager.FACE_TEMP_UPPER_THRESHOLD_PRM_SAVE`, then `FileHandling.prmDb.PRM_SAVE_FILE`. Observable: `PrmFileSaveComplete(records)`.
3. `RD.resetManager.WARM_RESET`. Observable at boot: `PrmFileLoadComplete(records)` with the same record count; `TemperatureAboveThreshold` per face <= 2 s of the first thermal tick **without** re-sending step 1. Pass: the saved value is in effect after the reset. Fail: `TemperatureAboveThreshold` absent (threshold back at 60) or `PrmDbFileLoadFailed` / `PrmFileReadError`.
4. Evidence: `FileHandling.fileDownlink.SendFile "/prmDb.dat" "prmDb-warm.dat"`; keep the file.

C-b. Cold reset during the write:
5. `FACE_TEMP_UPPER_THRESHOLD_PRM_SET 60` then `_PRM_SAVE`, `PRM_SAVE_FILE`, wait for `PrmFileSaveComplete` (the file now holds 60 again).
6. `FACE_TEMP_UPPER_THRESHOLD_PRM_SET (Ta - 5)`, `_PRM_SAVE`, then `PRM_SAVE_FILE` immediately followed (100 ms, from a sequence or a scripted GDS send, not by hand) by `RD.resetManager.COLD_RESET`.
7. Observable at boot: either `PrmFileLoadComplete` with the old value in effect (no `TemperatureAboveThreshold` until commanded) **or** with the new value in effect (`TemperatureAboveThreshold` <= 2 s) — both pass. Fail: `PrmDbFileLoadFailed`, `PrmFileReadError`, a record count different from step 3, or a `PrmFileLoadComplete` followed by a threshold that is neither 60 nor Ta - 5 (probe with `FACE_TEMP_UPPER_THRESHOLD_PRM_SET` of the same value: `PrmIdUpdated` shows the previous value).
8. Evidence: `SendFile "/prmDb.dat" "prmDb-cold.dat"`. Repeat step 6-8 five times (the 100 ms lands at different points of the write). Restore per header.

Outcome routing (07-followups): both pass, PROVES confirms → parameters may be assumed persistent after `PRM_SAVE_FILE`. C-b fails (corrupt file) → wrap `/prmDb.dat` handling in `PersistedRecord` (its intended use). C-a fails → the FileHandling load path is not doing what `FileHandling.fpp:39` says; open an issue before any CONOPS relies on a saved parameter.

## Criteria
| ID | Criterion | Automated | Evidence |
|---|---|---|---|
| DriverBoardHandler-10 | PING → PongReceived within 2 s; ARM → Armed; PULSE 500 ms → PulseStarted then non-zero CoilCurrent on the masked channels within 2 s; DISARM → Disarmed; board off → LinkLost within LINK_TIMEOUT_MS + 1 s | `driver_board_test.py::test_09_end_to_end` (`-m flatsat`) | B1-B10 event/channel export |
| TM-L2-01 (payload clause) | CoilCurrent0..2 update at least once per 45 s at level 3 with the board answering | `driver_board_test.py::test_03_hk_channels_update` | B2-B3 packet log |
| CDH-27 | Controls telemetry exists in a downlinkable packet (PayloadHousekeeping, group 3); cadence is ops | same | same |
| ADCS-L2-06 | Control telemetry downlinked | same | same |
| Persistence gate (no ID; 07-followups) | C-a saved value in effect after WARM_RESET; C-b old or new value after COLD_RESET 100 ms after PRM_SAVE_FILE, never a corrupt file, 5 of 5 | manual | `prmDb-warm.dat`, `prmDb-cold.dat` x5, boot event logs |
| Loopback smoke (no ID; review amendment 2 bench check) | FramesReceived climbs, FramesRejected 0, LinkState DOWN, no stall in 60 s | `driver_board_test.py::test_00_loopback_smoke` (no `flatsat` marker) | A1-A3 |

## Why this verifies it
- DriverBoardHandler-10 is a system row: every observable (events, the 22-channel packet, the coil current) exists only with the STM32 answering. Host tests prove the handler against `DriverBoardFake`; this run proves the fake matched the firmware. Timing is measured on flight-software timestamps (event and channel times), so ground latency cannot pass or fail a step.
- TM-L2-01's "payload [not integrated]" clause becomes integrated by this packet; the 45 s window is the plan's (01-scope), and two consecutive updates per channel show a cadence, not a one-off.
- The loopback runs first because it needs no firmware and exercises the one path the host cannot: bytes arriving on the 50 Hz thread while the 1 Hz thread sends, the lock-inversion scenario of review amendment 2.
- Part C answers a question no PROVES test has answered (`git grep PRM_SAVE proves-origin/main -- test` is empty). ThermalManager is chosen as the read-back because it reads the threshold live each tick; a component that caches its parameters only in `parameterUpdated` (ThermalManager's own `COLLECTION_INTERVAL_S`, ADCS, PowerMonitor, ImuManager, FaultManager, ComDelay — generated `loadParameters()` never calls `parameterUpdated()`) would show its default after a reboot even when the file is intact, and would make a correct file look like a failed one.

## Known traps
- A 500 ms pulse against a 1 s HK_REQUEST cadence: B7 depends on the board's post-pulse HK ("HK after completion", 02-protocol) carrying the pulse's current, or on an HK_REQUEST landing inside the pulse (about one chance in two). If B7 fails with `PulseStarted` present and the ammeter showing current, the STM32 firmware's completion HK reports the post-pulse (zero) current: an A1 firmware finding, and the criterion should say which reading the completion HK carries.
- With the board off nothing in `PayloadHousekeeping` changes, so no packet is sent (on-change packetizer). `LinkState` DOWN after B9 arrives in exactly one packet; read the event log first.
- The board's own 3 s failsafe (02-protocol, not host-configurable) disarms it independently of the host; a `LinkLost` on the host side is therefore never the only protection, and the STM32 constant must never be lowered below the host's worst case (`LINK_TIMEOUT_MS` + one 1 Hz tick).
- `BOARD_BOOT_S = 3` in the test is a bench allowance, not a measured STM32 boot time; correct it from the B1 log.
- All five handler parameters are RAM-only until `PRM_SAVE_FILE`; do not save test values. Part C saves and restores `FACE_TEMP_UPPER_THRESHOLD` only.
- Part A must run with the driver board unplugged (a board on the same pins would answer the looped-back bytes as garbage).
- The CI board runner has no driver board: run `pytest -m "not flatsat"` there; `-m flatsat` only on this bench.
