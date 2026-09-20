# HP-12 Power-cut during persisted-record write (T2 Desk-PSU, destructive)

| Field | Value |
|---|---|
| Tier | T2 Desk-PSU: PSU output switched by script (`korad_control.py -d /dev/ttyPWR --output 0/1`, as in `.github/actions/flash-firmware/action.yml`) |
| Hardware | FC board, UART GDS, scriptable PSU, host script that sends a command and cuts power after a randomised delay |
| Image / flash | Current image; filesystem formatted once before the run (`format_filesystem_test.py`) |
| Preconditions | `SET_LEVEL 5` after each boot (readback of `telemetryGate.TransmitState`); `telemetryGate` ENABLED and mode NORMAL at start; BootCount N0 |
| Restore | PSU ON at 8.0 V; `telemetryGate.SET_TRANSMIT_STATE ENABLED`; `EXIT_SAFE_MODE`; `SET_LEVEL 1` |
| Destructive | Yes: >= 20 power cuts |
| Duration | ~40 min scripted (20 cycles x ~2 min) |

Records under test: TelemetryGate tx-state record (`SET_TRANSMIT_STATE` writes it; corruption event `StateFileCorrupt`, write failure `StateFileWriteFailure`) and ModeManager mode record (`FORCE_SAFE_MODE`/`EXIT_SAFE_MODE`; MM0011/12).

## Procedure
1. Record the current pair (TransmitState S, mode M). Choose the write command that flips one of them (alternate gate and mode records across cycles).
2. Send the command; on its OK ack start a timer and cut PSU output after d ms, d drawn uniformly from 0..500 ms (covers open, write, flush, rename of `PersistedRecordFile.cpp`); log d. Every 5th cycle use d = 0 (cut on ack) and d = 500 (cut after the write is surely complete).
3. Wait 3 s; PSU output 1. Observable: first post-boot event <= 15 s; BootCount +1.
4. `SET_LEVEL 5`; read `telemetryGate.TransmitState` <= 45 s and `GET_CURRENT_MODE` / `GET_SAFE_MODE_REASON` <= 5 s; collect any `StateFileCorrupt` / `StateFileWriteFailure` / ModeManager corruption event within 5 s of boot.
5. Judge the cycle: PASS if the read-back value is the old value, the new value, or a corruption event was emitted with the documented fallback value; FAIL if any other value appears or a corruption is reported without an event.
6. Repeat for >= 20 cycles (>= 10 per record type). Tabulate d, outcome (old/new/corrupt), boot count.
7. Restore per header.

## Criteria
| ID | Criterion | Automated | Evidence |
|---|---|---|---|
| PersistedRecord-5 | Over repeated power-cycle-during-write iterations every post-reboot load returns old, new, or a detected-corrupt status; zero silent wrong reads over >= 20 cycles | manual (scripted host loop; no int test) | Step 6 table; GDS event log per boot |
| DH-L2-11 | PersistedRecord-1..7 pass at unit; PersistedRecord-5 power-cut: zero silent wrong reads >= 20 cycles; every persisted consumer (MM0011/12, AUTH013, REQ-SM-008, TelemetryGate-9) linked | unit tests (host) + this procedure + RTM link check | Unit run log; step 6 table; RTM rows |

## Why this verifies it
- PersistedRecord-5: the requirement's observable is the post-loss load result on the target filesystem, which only exists with real flash, real FAT `rename` semantics and a real power cut; the host fake cannot show it. Randomised d, with forced end-points, sweeps the vulnerable window rather than sampling one phase. The oracle is the ground's record of what was commanded (old/new pair) against the read-back, independent of the codec under test; a corruption *event* is required, so a silent fallback counts as a failure.
- DH-L2-11: composite; this group supplies the only clause not provable on the host. The link check is a matrix query, not a bench step.
- Remainder: Authenticate and StartupManager records are not toggled here; if their write paths differ from the two exercised, add them to the alternation.

## Known traps
- Zephyr FAT `rename` unlinks the destination first (not atomic on target) and `Os::File::write(WAIT)` discards flush status: this is exactly the failure class targeted; do not shorten d's range.
- Zephyr `Os::File::open` returns `OTHER_ERROR` for a missing file; first-boot detection uses `FileSystem::exists()`. A missing record after a cut must be reported as corrupt/absent, not read as zeros.
- The gate record left DISABLED silences telemetry after boot: check `TransmitState` at level 5 each cycle and re-enable before the next.
- A cut during a mode write can leave SAFE_MODE with faces OFF; `EXIT_SAFE_MODE` and `TURN_ON` before thermal groups.
