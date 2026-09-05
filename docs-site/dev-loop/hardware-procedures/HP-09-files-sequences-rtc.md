# HP-09 File storage, command sequences and RTC alarm (T1 Desk-USB)

| Field | Value |
|---|---|
| Tier | T1 Desk-USB |
| Hardware | FC board, UART GDS; a 4096-byte random file `hp09_4k.bin` generated on the ground (`test/int/files/` holds only `no_op.seq`) |
| Image / flash | Current image, NORMAL, filesystem formatted at least once (`format_filesystem_test.py`) |
| Preconditions | `SET_LEVEL 5` (`fsSpace` channels); `telemetryDelay.DIVIDER` 29; RTC time set (rtc_test fixtures) |
| Restore | Remove uplinked files (`fileManager.RemoveFile`); `SET_LEVEL 1` |
| Destructive | No (a partial upload can trigger SafeModeSequenceFailed; EXIT_SAFE_MODE if so) |
| Duration | ~8 min |

## Procedure
1. `SET_LEVEL 5`; capture 45 s. Observable: `fsSpace.FreeSpace` and `TotalSpace` each >= 1 update, 0 < Free <= Total (CDH-14 clause 1). Record F0.
2. `fileUplink` `hp09_4k.bin` -> `/hp09_4k.bin`. Observable: `FileReceived` <= 60 s; `FreeSpace` <= F0 - 4096 within 45 s (CDH-14 clause 2).
3. `RD.fileDownlink.SendFile /hp09_4k.bin`. Observable: `FileSent` <= 60 s; `cmp` ground copy vs source: identical (DH-L2-07).
4. Write `hp09.seq` containing `R00:00:05 CMD_NO_OP_STRING "hp09"`; compile and uplink; `RD.cmdSeq.CS_RUN /hp09.bin`. Observable: `CS_RUN` OpCodeCompleted at FSW time Tr; `NoOpStringReceived("hp09")` at Tr + 5 +/-1 s (FSW timestamps); `CS_SequenceComplete` (CDH-13, CH-L2-11, SC-L2-03).
5. `RD.rtcManager.ALARM_SET` for now + 5 s (use `rtc_test.py::test_05` argument form). Observable: `AlarmTriggered` <= 10 s (CDH-13).
6. Restore per header.

## Criteria
| ID | Criterion | Automated | Evidence |
|---|---|---|---|
| CDH-13 | Relative-tag sequence executes 5 +/-1 s after CS_RUN; ALARM_SET now+5 -> AlarmTriggered <= 10 s | rtc_test.py::test_05_rtc_alarm_set_and_trigger (alarm) ; manual (sequence) | Steps 4-5 FSW event times |
| CH-L2-11 | NoOpStringReceived 5 +/-1 s after CS_RUN OpCodeCompleted (FSW times), then CS_SequenceComplete | manual | Step 4 |
| SC-L2-03 | Relative tag as CH-L2-11; absolute tags not exercised (needs RTC-derived FSW time: add after CDH-13 step 5 passes) | manual | Step 4; note absolute-tag remainder |
| CDH-14 | FreeSpace/TotalSpace >= 1 per 45 s, 0 < Free <= Total; 4 KB uplink drops FreeSpace >= 4 KB within 45 s | manual | Steps 1-2 channel values |
| DH-L2-07 | SendFile of a previously uplinked 4 KB file: FileSent <= 60 s; byte-identical | manual | Step 3 `cmp` output |

## Why this verifies it
- CDH-13 / CH-L2-11 / SC-L2-03: the observable is *when* the sequenced command ran, taken from FSW event timestamps so GDS latency cannot fake the 5 s. The oracle (NoOpStringReceived from cmdDisp) is a different component from cmdSeq. The alarm path uses the RTC driver's own event.
- CDH-14: FreeSpace is reported by fsSpace from the filesystem, independent of fileUplink; the 4 KB delta ties the report to a real write. Managing telemetry/housekeeping *storage* beyond files is not implemented (no records/containers in the dictionary) and is not claimed.
- DH-L2-07: byte comparison on the ground is the strongest oracle; "selective" is shown by naming one file among several present.
- Absolute-tag scheduling stays a remainder until the time base is verified against the RTC.

## Known traps
- `rtc_test.py` alarm tests are `uart_only`; FSW time base for sequences may be uptime (TB_PROC_TIME) rather than RTC; compare Tr and the NoOp event within the same time base.
- FAT `rename` is not atomic on target and `write(WAIT)` discards flush status: a partial upload can leave a stale file; check `FreeSpace` returns to F0 after RemoveFile.
- `fileUplink`'s FILE queue depth is 1 in comQueue; do not overlap uplink and downlink.
