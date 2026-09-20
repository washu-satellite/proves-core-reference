# 07 — Non-goals, open risks, decisions left to the team

## Non-goals (this cycle)
- STM32 firmware (A1). The `DriverBoardFake` in the host tests is the reference behaviour it must match.
- Burst/stream consumption (A9): message types 0x08-0x0A and 0x88 are specified and parseable; the handler parses SAMPLE frames, counts them, and emits them on `sampleOut`, which is left unconnected this cycle. The burst component that connects to it, joins samples with IMU data, owns the RAM ring, and the packet id 24 are the next cycle. STREAM_START/STOP commands on the handler are also deferred to A9 so this cycle cannot start a stream nothing consumes.
- FaultManager integration: `FaultType` values double as bits in a U8 mask (`FaultTypes.fpp` header comment) and 1..8 are taken. Adding `PAYLOAD_LINK_LOSS = 9` requires widening `AUTHORITY_MASK`/`ActiveFaults` to U16 and `FaultInPorts` 4 → 5. Do it as its own small cycle; until then link loss is event + telemetry + local disarm, which is exactly the shadow-mode contract every other producer already follows.
- Mode-layer subscription (`systemModeChanged`, array width 2) — polling `getMode` suffices while ModeManager depowers the rails on safe-mode entry.
- Payload power switching from the handler — stays with sequences and ModeManager.
- A reflash path for the STM32 (CDH-17/20).

## Persistence gate (added 2026-09-18)
No test upstream or in this fork has ever executed `PRM_SAVE` + `FileHandling.prmDb.PRM_SAVE_FILE` + reset + read-back on a V5e (`git grep PRM_SAVE proves-origin/main -- test` is empty; the fork's two mentions are deliberate avoidance). Jesse is asking PROVES (2026-09-18). Until either PROVES confirms or HP-07 gains and passes these two steps — (a) set `thermalManager.FACE_TEMP_UPPER_THRESHOLD` to a distinctive value, `PRM_SAVE`, `PRM_SAVE_FILE`, `WARM_RESET`, read back changed; (b) the same with `COLD_RESET` issued 100 ms after `PRM_SAVE_FILE`, then confirm the board boots with either the old or the new value and never a corrupt one — every parameter in this plan is RAM-only, defaults are the flight values, and the CONOPS must not assume a set survives a reset. If (b) corrupts the file, wrap `/prmDb.dat` handling in `PersistedRecord` (its intended use). Nothing in `DriverBoardHandler` changes in either outcome.

## Open risks
1. `PRMDB_NUM_DB_ENTRIES` RAM cost is unmeasured; the plan says 128 with 64 as fallback. Decide from the build's memory lines, not from this page.
2. 50 Hz drain shares a rate group with DetumbleManager. If a future STM32 stream ever exceeds 64 B per 20 ms (3.2 KB/s) the ring buffer will overflow silently (`ring_buf_put` failure is not counted, `ZephyrUartDriver.cpp:75`). Add a ring-overflow counter to the driver upstream, or keep streams ≤ 50 Hz x 20 B. Note for A9.
3. One-tick supervision resolution means a 1000 ms timeout detects loss in 1-2 s. Acceptable because the board's own 3 s failsafe is the safety path; write that dependency into the sdd so nobody lowers the STM32 constant below the host's worst case.
4. Unknown board state after a host reboot: the STM32 may still be ARMED from before the reboot. Mitigation in this cycle: the handler sends DISARM as its first frame after `uartReady`, before any HEARTBEAT. Add to DriverBoardHandler-1's criterion ("first frame is DISARM, then HEARTBEATs").
5. Two writers on uart1 pins: none today; the J3 aux header shares uart0, not uart1 (S8), so the GDS cable cannot collide with the payload link. Keep it that way.

## Decisions for the team (not blocking the cycle; defaults are taken from the 2026-09-17 parameter list)
- Pulse default 2000 ms (identification, Q15) vs 320 ms (detumble-like). Plan default 2000.
- Whether `PULSE` should refuse when `HK.faultFlags != 0`. Plan: refuse (BOARD_REFUSED) — conservative.
- Whether housekeeping belongs in group 3 or the beacon set. Plan: group 3.

## Found during implementation (E2-E5, 2026-09-18/19)
- **Saved parameters are not applied at boot in six components.** The generated `loadParameters()` fills the component's parameter storage from `/prmDb.dat` but never calls `parameterUpdated()`, so any component that caches its effective value only in `parameterUpdated` runs on its constructor default after a reboot however the file was saved: ThermalManager, ADCS, PowerMonitor and ImuManager (`COLLECTION_INTERVAL_S`), FaultManager, ComDelay. DriverBoardHandler refreshes its cache on its first `run` tick and is not affected. Own follow-up cycle: one small change per component (re-read on the first tick, or `paramGet_*` in the constructor after `loadParameters`), a host test each that pins "value in storage at construction is the value in effect on tick 1", and HP-15 part C (persistence gate) rerun against one of the six once fixed. Until then the persistence gate above can only be read through a component that reads its parameter live (ThermalManager's thresholds do; its interval does not).
- **Hash buckets 242 of 256.** After E5 the packet set names 242 distinct channels (`scripts/check_packet_set.py` warns above 90 %); `Svc.BufferManager` instances alone bring 5 channels + 2 events each. A8 (DataRecorder, +14 channels) must raise `TLMPACKETIZER_HASH_BUCKETS` again in its constants commit, at about 200 B RAM per bucket.
