# HP-12 Power-cut during persisted-record and recorder-segment writes (T2 Desk-PSU, destructive)

| Field | Value |
|---|---|
| Tier | T2 Desk-PSU: PSU output switched by script (`korad_control.py -d /dev/ttyPWR --output 0/1`, as in `.github/actions/flash-firmware/action.yml`) |
| Hardware | FC board, UART GDS, scriptable PSU, host script that sends a command and cuts power after a randomised delay |
| Image / flash | Current image; filesystem formatted once before the run (`format_filesystem_test.py`) |
| Preconditions | `SET_LEVEL 5` after each boot (readback of `telemetryGate.TransmitState`); `telemetryGate` ENABLED and mode NORMAL at start; BootCount N0 |
| Restore | PSU ON at 8.0 V; `telemetryGate.SET_TRANSMIT_STATE ENABLED`; `EXIT_SAFE_MODE`; `SET_LEVEL 1`; Part B: `dataRecorder.SET_RETENTION_S TLM 604800`, `SET_FLUSH TLM 16 60`, `SAVE_CONFIG` (the board test's autouse fixture does the same) |
| Destructive | Yes: >= 20 power cuts in Part A and >= 20 in Part B; Part B also deletes TLM segments (retention 60 s) |
| Duration | Part A ~40 min scripted (20 cycles x ~2 min); Part B ~15 min board test + ~40 min loop |

Part A records under test: TelemetryGate tx-state record (`SET_TRANSMIT_STATE` writes it; corruption event `StateFileCorrupt`, write failure `StateFileWriteFailure`) and ModeManager mode record (`FORCE_SAFE_MODE`/`EXIT_SAFE_MODE`; MM0011/12).

## Part A: persisted records
1. Record the current pair (TransmitState S, mode M). Choose the write command that flips one of them (alternate gate and mode records across cycles).
2. Send the command; on its OK ack start a timer and cut PSU output after d ms, d drawn uniformly from 0..500 ms (covers open, write, flush, rename of `PersistedRecordFile.cpp`); log d. Every 5th cycle use d = 0 (cut on ack) and d = 500 (cut after the write is surely complete).
3. Wait 3 s; PSU output 1. Observable: first post-boot event <= 15 s; BootCount +1.
4. `SET_LEVEL 5`; read `telemetryGate.TransmitState` <= 45 s and `GET_CURRENT_MODE` / `GET_SAFE_MODE_REASON` <= 5 s; collect any `StateFileCorrupt` / `StateFileWriteFailure` / ModeManager corruption event within 5 s of boot.
5. Judge the cycle: PASS if the read-back value is the old value, the new value, or a corruption event was emitted with the documented fallback value; FAIL if any other value appears or a corruption is reported without an event.
6. Repeat for >= 20 cycles (>= 10 per record type). Tabulate d, outcome (old/new/corrupt), boot count.
7. Restore per header.

## Part B: DataRecorder segments (image with A8-3, `dataRecorder` wired)
Recorder under test: EVT segments `/rec/evt/NNNNNNNN.bin` (01-normative §5), written by one `write` + `flush` per 1 Hz tick or by `CLOSE_SEGMENT`; ground decoder `tools/recorder_reader.py` (§10) with the image's dictionary. Windows as in `data_recorder_test.py`: command 5 s, file delivery 60 s, reboot 15 s, boot scan 20 s.
B1. With the board up and the UART GDS, run `pytest PROVESFlightControllerReference/test/int/data_recorder_test.py` (all four tests; `slow` and `uart_only` included). Observable: 4 passed. Covers DH-L2-05 (test_01), DH-L2-08 (test_02), DH-L2-12 (test_03, test_04), CDH-16 (test_04). Keep the pytest log and the downlinked files.
B2. Loop setup: `SAVE_CONFIG` once (defaults); note `dataRecorder.EvtSegmentsOnDisk` and the highest EVT seq from `LIST_SEGMENTS EVT 0` (`SegmentInfo` events, 16 at most per call; repeat from the last seq).
B3. Each cycle, alternate the trigger. (a) Burst: 8 x `CMD_NO_OP` back to back (16 events, at least the EVT flush count 8, so the next 1 Hz tick writes them); cut PSU output d ms after the last ack, d uniform 0..1500 (covers the next tick's write and flush). (b) Close: after 3 x `CMD_NO_OP`, send `dataRecorder.CLOSE_SEGMENT EVT` and cut d ms after sending it, d uniform 0..500 (the write happens inside the command). Log d, the trigger and the FSW time tag of the last event received before the cut (t_cut).
B4. Wait 3 s; PSU output 1. Observable: first post-boot event <= 15 s; no `dataRecorder.ConfigCorrupt` and no `DirectoryScanFailed` within 20 s of boot.
B5. After 20 s (boot scan), `LIST_SEGMENTS EVT <seq from B2>`: every segment with `openTimeS` <= t_cut is a pre-cut segment. `fileDownlink.SendFile /rec/evt/NNNNNNNN.bin <dest>` each one; `FileSent` <= 60 s each.
B6. Run `recorder_reader.py --dictionary <image dict> <files>`. Judge the cycle: PASS if every file exits 0 or 1 (a file shorter than 20 bytes, cut during its first write, may exit 2 and counts as holding no records) and every decoded `evt` row has seconds <= t_cut + 1 and, when its id and time tag are in the GDS event log, the same arguments; FAIL on any other exit 2, a row after t_cut + 1, or a row whose arguments differ from the live event.
B7. Repeat >= 20 cycles (>= 10 per trigger). Tabulate d, trigger, per-file exit code and stop reason, rows, and the last pre-cut event id found / not found (loss of the last <= 1 s of events is expected; a wrong row is not). Restore per header.

## Criteria
| ID | Criterion | Automated | Evidence |
|---|---|---|---|
| PersistedRecord-5 | Over repeated power-cycle-during-write iterations every post-reboot load returns old, new, or a detected-corrupt status; zero silent wrong reads over >= 20 cycles | manual (scripted host loop; no int test) | Step 6 table; GDS event log per boot |
| DH-L2-11 | PersistedRecord-1..7 pass at unit; PersistedRecord-5 power-cut: zero silent wrong reads >= 20 cycles; every persisted consumer (MM0011/12, AUTH013, REQ-SM-008, TelemetryGate-9) linked | unit tests (host) + this procedure + RTM link check | Unit run log; step 6 table; RTM rows |
| DH-L2-05 | On the board dataRecorder.SET_RETENTION_S TLM 3600 returns OK with ConfigApplied within 5 s; SET_RETENTION_S TLM 10 then returns VALIDATION_ERROR with ConfigRejected; after SAVE_CONFIG, /rec/config.bin downlinked by fileDownlink.SendFile decodes to TLM retention 3600 | data_recorder_test.py::test_01_retention_is_commanded_validated_and_persisted | B1 pytest log; downlinked config file |
| DH-L2-08 | With SET_RETENTION_S TLM 60 a closed segment is deleted with SegmentDeleted reason AGE within 5 s after t1 + 60 s (t1 = newer segment opened) and absent from LIST_SEGMENTS TLM, the newer one still listed | data_recorder_test.py::test_02_closed_segment_is_deleted_for_age_after_the_next_opens (slow) | B1 pytest log |
| DH-L2-12 | A WARNING_HI event is found, same id and time tag, by recorder_reader.py in the EVT segment DOWNLINK_NEWEST EVT delivers within 60 s; after COLD_RESET the pre-reset segment is still listed and retrievable | data_recorder_test.py::test_03, ::test_04 (uart_only) ; B3-B7 supplementary | B1 pytest log; reader CSVs; B7 table |
| CDH-16 | A WARNING_HI event raised before a COLD_RESET is retrievable after reboot via LIST_SEGMENTS EVT and fileDownlink.SendFile within 60 s, decoded with the same id and time tag | data_recorder_test.py::test_04_evt_segment_survives_cold_reset_and_is_retrievable ; B3-B7 supplementary | B1 pytest log; B7 table |

## Why this verifies it
- PersistedRecord-5: the requirement's observable is the post-loss load result on the target filesystem, which only exists with real flash, real FAT `rename` semantics and a real power cut; the host fake cannot show it. Randomised d, with forced end-points, sweeps the vulnerable window rather than sampling one phase. The oracle is the ground's record of what was commanded (old/new pair) against the read-back, independent of the codec under test; a corruption *event* is required, so a silent fallback counts as a failure.
- DH-L2-11: composite; this group supplies the only clause not provable on the host. The link check is a matrix query, not a bench step.
- DH-L2-05 / DH-L2-08 / DH-L2-12 / CDH-16: the observables (command responses and events on the ground, a file read back through file downlink, a segment deleted on the target FAT by the 1 Hz tick, a segment surviving a reset) exist only on the board; the unit rows DataRecorder-3..11 prove the logic against a fake filesystem. The oracle for the stored event is the live event the GDS received (id and time tag), decoded by a ground tool that shares no code with the flight codec. A COLD_RESET is a commanded reboot; Part B's loop adds uncommanded power loss during the write, the case the segment format (per-record CRC, append-only, never truncated) is built for.
- Remainder: Authenticate and StartupManager records are not toggled here; if their write paths differ from the two exercised, add them to the alternation.

## Known traps
- Zephyr FAT `rename` unlinks the destination first (not atomic on target) and `Os::File::write(WAIT)` discards flush status: this is exactly the failure class targeted; do not shorten d's range.
- Zephyr `Os::File::open` returns `OTHER_ERROR` for a missing file; first-boot detection uses `FileSystem::exists()`. A missing record after a cut must be reported as corrupt/absent, not read as zeros.
- The gate record left DISABLED silences telemetry after boot: check `TransmitState` at level 5 each cycle and re-enable before the next.
- A cut during a mode write can leave SAFE_MODE with faces OFF; `EXIT_SAFE_MODE` and `TURN_ON` before thermal groups.
- Part B: the boot itself is recorded (boot events open a new EVT segment), so after a reboot `DOWNLINK_NEWEST EVT` returns a post-boot segment; fetch pre-cut segments by number (B5).
- Part B: the board test lowers TLM retention to 60 s; its autouse fixture restores and saves the defaults, but after an aborted run check `LIST_SEGMENTS`/`SET_RETENTION_S` before leaving the board.
- Part B: segments are decoded with the dictionary of the image that wrote them; the `FileSystem` packet layout changes between images.
