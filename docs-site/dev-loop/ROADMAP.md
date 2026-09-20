# SCALAR flight-software roadmap

Written 2026-09-19 after Cycle F. This is the one document that says what comes next and why; the cycle plans under
`cycles/` say how. When this file and an older "suggested order" disagree, this file wins.

State: branch `feat/upstream-sync` @ 135d5af1 = Cycle E (payload link) + Cycle F (synced with upstream PROVES a477893b,
F' 4.3.0, Zephyr 4.4.1). PR #10 open, lint + unit-test green on CI. Image: FLASH 69.5 %, RAM 62.2 %, 23/24 packets,
244/256 channels, 30 host binaries. **Nothing built since 2026-09-05 has run on a board.**

## Standing rules

1. **Sync before you build.** At the start of every cycle: `git fetch proves-origin` and compare; if upstream moved,
   the first row of the cycle is the sync (analysis → plan → merge row), not the feature. Cost of not doing it: Cycle F.
2. **Every cycle ends with a merge to the fork's `main`** once its PR's lint + unit-test are green, so `main` stays the
   real baseline instead of a July snapshot.
3. **A code cycle does not activate hardware-facing wiring until the previous image has booted on a board.** The bench
   milestone below is a gate for A8's activation row and everything after it.
4. **Results before methods.** Plans state observable results; tests are written by a separate agent from the normative sections alone, before any implementation exists (skill Stage 3b), and their hashes are pinned in the review so the coder cannot reshape them; coders may change methods.
   Per-row commits, gate before each, one coder in the tree at a time, revert on regression.
5. **Capacity constants are checked in the gate**, never assumed (`scripts/check_packet_set.py` today; the audit script
   below extends it). A firmware build succeeding says nothing about boot.
6. **Persistence follows the consequence rule** in `Components/PersistedRecord/docs/sdd.md`.
7. **Parameter vs file vs constant follows `design/parameter-policy.md`.** A plan that re-bins a value says so in its normative section.

## Ordered queue

| # | Item | Blocked on | Size | Done when |
|---|---|---|---|---|
| 1 | **DONE 2026-09-19 (7658b28e)** — A10 — parameters re-read at boot in ThermalManager, ADCS, PowerMonitor, ImuManager, FaultManager, ComDelay, and upstream's RtcManager and DetumbleManager (same defect; offer the fix upstream) (F' `loadParameters()` never calls `parameterUpdated()`) | nothing | 1 row, ≤ 1 day | host test per component: a database value present at boot is the effective value on tick 1 and appears in the effective-value channel |
| 2 | **Capacity audit** — also make `scripts/verify.sh` clear `build-gtest` before the host build (today it builds incrementally and can link a stale object after a `cp`-restored source; Cycle G finding). One gate script covering `MAX_PACKETIZER_PACKETS/CHANNELS`, `CMD_DISPATCHER_DISPATCH_TABLE_SIZE`, `PRMDB_NUM_DB_ENTRIES`, `ActiveRateGroupOutputPorts` per group, `FaultInPorts`, `FaultType` mask width, TaskGate slots, against the built dictionary and topology | nothing | 1 row, ≤ 1 day | script fails on a synthetic overflow of each constant; wired into `verify.sh` |
| 3 | **Bench milestone** — flash the synced image; **first-program the STM32** (v8 routes no SWD/BOOT0: bodge BOOT0 high via the R19 pad through reset, then program over J3 with a USB-serial adapter; C-32); boot (expect one `BootCountCorrupted`, count → 1); HP-15 loopback smoke on J18 9↔10; the persistence check (warm reset read-back; cold reset 100 ms after `PRM_SAVE_FILE`); record memory/dictionary from a live board; tag `f-bench-ok` | a V5e on a cable, an afternoon | — | HP-15 §A and §C pass; results in the ledger |
| 4 | **A8 — DataRecorder** (design in `design/stored-data/`): fix slot 20 → 21, dictionary key `members`, `Os::Directory` at fprime-zephyr b14101dd; raise `MAX_PACKETIZER_CHANNELS` (≈ +14 channels → 288); codec + ring rows first, activation row after milestone 3 | items 1-3 | 1 cycle, ~7 rows | segments written, downlinked, decoded on the bench; event before reset retrievable after |
| 5 | **A9 — BurstCapture**: consumes `driverBoardHandler.sampleOut`, batches into A8's third stream, STREAM_START/STOP on the handler, loopback inject for testing without the STM32 | A8; parameter defaults (see owed decisions) | 1 cycle, ~7 rows | a commanded burst lands as a segment file and decodes |
| 5b | **STM32 update path on the RP2350** — REBOOT_TO_BOOTLOADER protocol command on the handler + a small F' loader speaking AN3155 over uart1 (8E1 for the session), image staged on the SD NAND | A1 first image on the board | 1 short cycle | an image uplinked as a file is written to the STM32 and PONG reports the new version |
| 6 | **Mode layer** — mode table in ModeManager, generic set-mode, per-mode TaskGate mask, fault-policy mode axis, payload subscribes to mode changes | CONOPS decisions | 1 cycle | new modes definable by parameters + sequence files |
| 7 | **FaultManager authority** — flip from shadow to acting, per trigger, after the soak in `cycles/cycle-d-plan/06-followups.md` | milestone 3 + flatsat | 1 short cycle | producer-side actions removed |
| 8 | **Bit-flip (SEU) protection for RAM values** — scope which values, periodic sanity checks | design decision | later | — |

In parallel, not the loop's to do: **A1 STM32 firmware** (spec: `Components/DriverBoardProtocol/docs/sdd.md`; executable
reference: `test/unit-tests/support/DriverBoardFake.hpp`); the reflash path for the STM32 (SWD bit-bang now, BOOT0/NRST on
the next driver-board revision).

## Decisions owed (Ready-gate inputs)

| Decision | Needed by | Owner | Default if none |
|---|---|---|---|
| Pulse default: 2000 ms (identification, Q15) or 320 ms (detumble-like) | A9 | Jesse / controls | 2000 ms (shipped in E) |
| Burst trigger default: command-only vs on detumble events | A9 | Jesse | command-only |
| Burst sample rate and window | A9 | Jesse / controls | 10 Hz, 60 s |
| Magnetometer hard-iron offset: parameter (3 floats) or calibration file | A9 or IMU row | Jesse | parameter |
| Transmit power: stay compile-time (measure once) or ask upstream for a driver parameter | link budget C-29 | Jesse / comms | compile-time |
| HMAC key: restore the pre-sync key from elsewhere, or accept reflash of old-image boards | bench | Jesse | new key stands |
| Merge PR #10 to `main` now (rule 2) | before A10's PR | Jesse | — |
| Who owns A1 | A9's end-to-end test | Jesse | unassigned |
| Coil geometry: **decided 2026-09-19 — compile-time constants** (hardware values; frees ~29 of DetumbleManager's 34 parameters). Row for a later cycle; policy in `design/parameter-policy.md` | A9 or later | Jesse | — |

## Environment items (one-time)

- The repo lives in an iCloud-synced folder; conflict copies (`<name> 2.*`) appear inside the tree and the build dir.
  Move the checkout out of iCloud, or exclude the folder from sync.
- `~/scalar-build/proves-core-reference` is the only place the firmware builds (path with an apostrophe); it is at
  upstream's toolchain now (SDK 1.0.1, fprime-tools 4.3.0). Recipe in `CLAUDE.md`.
- The fork's CI has no hardware runner; build/integration jobs queue 24 h and cancel. Only lint and unit-test are evidence.

## What this replaces

- `~/scalar/generated/not-done.md` "Suggested order" (pre-Cycle E): superseded by the queue above.
- `cycles/cycle-sequencing-E-A8-A9.md` §"Order" and budgets: still valid for the A8/A9 ownership split; budgets there are
  post-sync. This file is the schedule; that file is the ownership map.
