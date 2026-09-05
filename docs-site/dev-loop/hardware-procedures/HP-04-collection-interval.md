# HP-04 Configurable collection interval (T1 Desk-USB)

| Field | Value |
|---|---|
| Tier | T1 Desk-USB |
| Hardware | FC board, UART GDS |
| Image / flash | Current image, NORMAL mode |
| Preconditions | Face switches ON; `RD.telemetryDelay.DIVIDER_PRM_SET 0` (packetizer every tick); `CdhCore.tlmSend.SET_LEVEL 3` for thermal, `6` for IMU channels; `RD.detumbleManager.SET_MODE DISABLED` (see traps); parameters RAM-only (no `PRM_SAVE_FILE`) |
| Restore | `COLLECTION_INTERVAL_S_PRM_SET 1` on thermalManager and imuManager; `DIVIDER_PRM_SET 29`; `SET_LEVEL 1`; detumbleManager back to its OPERATING_MODE |
| Destructive | No |
| Duration | ~6 min automated, ~12 min manual |

Windows (from `collection_interval_test.py`): CAPTURE = N x (5 + 1) + 10 s for 5 consecutive updates at interval N; default-interval capture 12 s; telemetry read timeout 30 s.

## Procedure
1. `SET_LEVEL 3`, `DIVIDER_PRM_SET 0`. Observable: acks <= 10 s.
2. Capture 12 s at the default interval. Observable: `tmp112Face0Manager.Temperature` updates ~1 s apart (>= 8 in 12 s) (ThermalManager-1 default clause, TM-L2-02 default).
3. `RD.thermalManager.COLLECTION_INTERVAL_S_PRM_SET 3`. Observable: `thermalManager.CollectionIntervalS` reads 3 <= 30 s; over CAPTURE (28 s) 5 consecutive `Temperature` updates spaced 3 +/-1 s (TM-L2-02, ThermalManager-1).
4. `COLLECTION_INTERVAL_S_PRM_SET 0`, then `61`. Observable each: `CollectionIntervalRejected` event <= 5 s, `CollectionIntervalS` reads 1, updates return to ~1 s spacing (ThermalManager-2).
5. `SET_LEVEL 6`; `RD.detumbleManager.SET_MODE DISABLED`; `RD.imuManager.COLLECTION_INTERVAL_S_PRM_SET 5`. Observable: `imuManager.CollectionIntervalS` reads 5 <= 30 s; over 40 s 5 consecutive `imuManager.MagneticField` updates spaced 5 +/-1 s (ImuManager-1, CDH-5).
6. Restore per header; confirm `CollectionIntervalS` == 1 on both and `Temperature` spacing ~1 s.

## Criteria
| ID | Criterion | Automated | Evidence |
|---|---|---|---|
| TM-L2-02 | Source interval set to N (1..60): updates spaced N +/-1 s over 5 consecutive updates (DIVIDER 0, level 3) | collection_interval_test.py::test_01_interval_spaces_channel_updates ; ::test_03_default_interval_updates_every_second | Steps 2-3 update timestamps |
| ThermalManager-1 | 4 sweeps in 12 ticks at interval 3; every tick at default (Unit). Board evidence: step 3 spacing | test_ThermalManager_CollectionInterval (unit, passing) ; collection_interval_test.py::test_01/test_03 | Unit log + step 3 |
| ThermalManager-2 | 0 / >60 / INVALID -> effective 1 s, one CollectionIntervalRejected, CollectionIntervalS == 1 (Unit). Board evidence: step 4 | test_ThermalManager_CollectionInterval (unit, passing) ; collection_interval_test.py::test_02_out_of_range_interval_is_rejected | Unit log + step 4 |
| ImuManager-1 | detumble idle; after imuManager interval N: CollectionIntervalS == N and MagneticField spaced N +/-1 s over 5 updates | manual (no int test yet; mirror test_01 on imuManager) | Step 5 |
| CDH-5 | Same observable as ImuManager-1 (tightened from the matrix text, whose "[no such parameter exists]" note is stale: `ImuManager.fpp:86` declares `COLLECTION_INTERVAL_S`) | manual (as ImuManager-1) | Step 5 |

## Why this verifies it
- TM-L2-02 / ImuManager-1 / CDH-5: the observable is update spacing on the ground; with DIVIDER 0 the packetizer adds <= 1 s jitter, so N +/-1 s discriminates N from N+/-2 and from the 1 s default. Five consecutive gaps rule out a single lucky pair. Readback of `CollectionIntervalS` is an independent second observable of the parameter path.
- ThermalManager-1/2: Level Unit is correct and already passing; the board steps only show the same behaviour survives the real PrmDb and packetizer. The negative path (0, 61) is provoked in step 4 and must produce exactly one rejection event per set.
- CDH-5 remainder: the matrix says "ADCS telemetry"; the parameter exists on imuManager (the ADCS sensor source) and is what is measured. Update the criterion text once this passes.

## Known traps
- `DetumbleManager` reads the IMU ports at 50 Hz (`topology.fpp:250`) and IMU channels update independently of `imuManager.run`. If `MagneticField` keeps updating at ~1 s with detumble DISABLED, the channel is written by another path: then ImuManager-1/CDH-5 need a channel written only by `imuManager.run` (check the dictionary) and this must be recorded.
- DIVIDER 0 floods the link; never leave it set. RateDelay falls back to 29 on INVALID.
- `PRM_SET` is RAM-only; a `PRM_SAVE_FILE` here would persist interval 0/61 tests. Do not save.
- Thermal packet is group 3; IMU channels need level 6 (confirm in `ReferenceDeploymentPackets.fppi`).
