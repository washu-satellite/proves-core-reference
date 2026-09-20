# HP-03 Scheduler rate-group timing (T1 Desk-USB)

| Field | Value |
|---|---|
| Tier | T1 Desk-USB (same bench as HP-02; run back-to-back, no restore between) |
| Hardware | As HP-02 |
| Image / flash | Current image, NORMAL mode |
| Preconditions | `CdhCore.tlmSend.SET_LEVEL 6`; face switches ON; `telemetryDelay.DIVIDER` 29; no commands during capture |
| Restore | `SET_LEVEL 1` |
| Destructive | No |
| Duration | 105 s capture (SC-L2-01/04) + 10 min capture (SC-L2-05) |

## Procedure
1. `SET_LEVEL 6` (skip if HP-02 just ran). Observable: ack <= 10 s.
2. Capture 105 s. Observable per rate group (`rateGroup50Hz`, `rateGroup10Hz`, `rateGroup1Hz`): `RgCycleSlips` == 0 and `RgMaxTime` < period (20 / 100 / 1000 ms); `startupManager.BootCount` received in every 30 s period (SC-L2-01).
3. Same capture: 1 Hz members (imuManager, thermalManager, powerMonitor, adcs, modeManager, watchdog) each show >= 1 channel update while `rateGroup1Hz.RgMaxTime` < 1000 ms and `RgCycleSlips` == 0 (SC-L2-04).
4. Extend the capture to 10 min, no commands. Observable: `RgCycleSlips` == 0 for all three groups; max `RgMaxTime` per group recorded and expressed as a percentage of the period (SC-L2-05 interim: < 100 %).
5. `SET_LEVEL 1`.

## Criteria
| ID | Criterion | Automated | Evidence |
|---|---|---|---|
| SC-L2-01 | Over 70 s at level 5+: RgCycleSlips == 0 and RgMaxTime < period for 50/10/1 Hz; BootCount every period | telemetry_sources_test.py::test_05_scheduler_and_health | Step 2 channel min/max table |
| SC-L2-04 | Over 70 s the 1 Hz group: RgMaxTime < 1000 ms, RgCycleSlips == 0, all its member channels update | telemetry_sources_test.py::test_05_scheduler_and_health | Step 3 |
| SC-L2-05 | Over 10 min: RgCycleSlips == 0 all groups; RgMaxTime <= [TBD headroom %] (interim < period) | manual (Analysis of the 10 min capture) | Step 4 max RgMaxTime per group, as % of period |

## Why this verifies it
- SC-L2-01/04: `RgMaxTime`/`RgCycleSlips` are library counters kept by `Svc.ActiveRateGroup`, independent of the member components whose timing they measure; the BootCount cadence shows the 1 Hz chain actually drives downlink. Window covers >= 2 packetizer runs so the counters are seen updated.
- SC-L2-05: Method Analysis is right: the number is a worst-case ratio computed from the record; the 10 min window is 20 packetizer runs and > 500 1 Hz cycles. The headroom threshold is Mission Ops' number; until set, only the interim "< period" is a pass/fail.
- All three: the negative path (a slip) is not provoked; a slip during capture is a failure, so the test is sensitive to it.

## Known traps
- `RateGroupCycleSlip` events are ID-filtered by `startup.seq`; only the channels are trustworthy.
- `RgMaxTime` is a high-water mark: it never decreases within a boot, so a spike from a prior group (e.g. HP-06 safe-mode entry or a file uplink) inflates it. Reboot before this group or record BootCount and note the prior activity.
- At level 6 all packets downlink; on a slow link this itself loads the 1 Hz thread. Record which level was used.
