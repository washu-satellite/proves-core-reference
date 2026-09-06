# Cycle A plan: ModeManager, Authenticate, StartupManager onto PersistedRecord

Repo `$R` = `/Users/jesse-cm/Documents/Documents - Jesse's Mac/scalar-softwarestack/proves-core-reference`, branch `feat/persisted-record` @ 96a0ed7. `C` = `$R/PROVESFlightControllerReference/Components`, `UT` = `$R/PROVESFlightControllerReference/test/unit-tests`. Python: `fprime-venv/bin/python3`. Governing rule: default flight behaviour unchanged; every new failure mode falls back to today's behaviour or to what the requirement names; existing 8 host binaries stay green. No `.fpp` edits anywhere in this cycle (all events reused; no new channels/params).

Shared conventions (all three components): payloads are explicit little-endian byte packing into a `U8[]` (no struct reinterpret); temp path = target path with `.tmp` extension; `PersistedRecord::load(path, temp, magic, out, cap, len)` / `store(path, temp, magic, payload, len)` exactly as `C/TelemetryGate/TelemetryGate.cpp:101-136`; a valid decode is additionally checked for `payloadLen == expected` and field ranges (a CRC-valid record with bad fields is "corrupt"). `BAD_VERSION` is a corrupt class today and the hook for a future migration (codec checks it after CRC, `C/PersistedRecord/PersistedRecordCodec.hpp:84-87`).

## 1. Per-component layout and status mapping

### ModeManager (`C/ModeManager`)
- Magic `"MMS1"`. Path `/mode_state.bin` (keep, `ModeManager.hpp:165`), temp `/mode_state.tmp` (new constant beside it). Payload 7 bytes: `[0]` mode U8 (1=SAFE_MODE, 2=NORMAL), `[1..4]` safeModeEntryCount U32 LE, `[5]` safeModeReason U8 (0..5, `ModeManager.fpp:10-17`), `[6]` cleanShutdown U8 (0/1). Field validation on OK: len==7, mode in {1,2}, reason <= 5, clean in {0,1} (today reason/clean are restored unvalidated, `ModeManager.cpp:266,271`; MM0012 says out-of-range fields are a validation failure).
- Load mapping (in `loadState()`, `ModeManager.cpp:247-342`), event = `StatePersistenceFailure(op, status)` (`ModeManager.fpp:117-123`, op string <= 20 chars, status I32 = `static_cast<I32>(PersistedRecord::Status)`):

| Status | Behaviour | Event |
|---|---|---|
| OK + fields valid | restore mode/count/reason; `unintendedReboot = (clean==0 && mode==NORMAL)` as today (`:271`); SAFE -> `turnOffNonCriticalComponents()` + EnteringSafeMode("State restored..."), NORMAL -> `turnOnComponents()` | none |
| MISSING (first boot; also a temp-only fallback that is not fully valid) | NORMAL, count 0, reason NONE, `turnOnComponents()` | none (fixes issue #1: `exists()` classification lives in `PersistedRecordFile.cpp:31-37`) |
| TRUNCATED, BAD_MAGIC, BAD_LENGTH, BAD_CRC, BAD_VERSION, OK-with-bad-fields | count 0, reason NONE, then `enterSafeMode(SYSTEM_FAULT)` (`:376`, which turns switches off, emits EnteringSafeMode, tlm, modeChanged, and stores) | exactly one `StatePersistenceFailure("load-corrupt", status)` emitted BEFORE `enterSafeMode` |
| OPEN_ERROR, READ_ERROR (file present, unreadable) | NORMAL defaults, `turnOnComponents()` — today's behaviour (`:300-308, 318-322`), not a validation failure so MM0012's SAFE rule does not apply; deliberately NOT the TelemetryGate "treat OPEN_ERROR as missing" shortcut (`TelemetryGate.cpp:108-114`) because PersistedRecordFile already separates absent from unopenable via `exists()`, and silencing a present-but-unopenable state file would hide a storage fault the operator sees today | one `StatePersistenceFailure("load-open"/"load-read", status)` |
| INVALID_ARGUMENT | impossible (constant args); fold into the read-error row | same |

- After the switch: keep the trailing `saveState()` (`:341`) that arms unintended-reboot detection (clean=0). `saveState()` (`:344-374`) becomes `store(...)` with clean=0; failure -> `StatePersistenceFailure("save-store", status)` (today two ops "save-open"/"save-write"; one op string now). `prepareForReboot_handler` (`:168-203`) becomes the same encode with clean=1 + `store`; failure -> `StatePersistenceFailure("shutdown-store", status)`. Event count on the normal paths is unchanged (zero).

### Authenticate (`C/Authenticate`)
- Magic `"ASN1"`. New path `/sequence_number.bin`, temp `/sequence_number.tmp` (replaces `"//sequence_number.txt"`, `Authenticate.cpp:21`; see §3). Payload 4 bytes: U32 LE sequence number. Validation: len==4 only (every U32 is legal).
- Persistence moves into a new F´-free helper `C/Authenticate/SequenceNumberStore.hpp/.cpp` (namespace `Components::SequenceNumberStore`, includes only `<cstdint>` + `PersistedRecordFile.hpp`): `enum class LoadResult { LOADED, FIRST_BOOT, CORRUPT }`; `LoadResult load(U32& value)` (value = stored on LOADED, 0 otherwise); `bool store(U32 value)`; path constants live here. Reason: `Authenticate.cpp:9` includes `<psa/crypto.h>` (mbedTLS PSA) and `Fw::Buffer`/`ComCfg::FrameContext`, so compiling the component on the host is not worth a recorder stub; the helper carries the whole decision so the component's mapping is one `if`.
- Mapping inside the helper; the component emits the existing `FileOpenError(error: U32, filename: string size 64)` (`Authenticate.fpp:36`, declared, never emitted today, `throttle 2`) with `error = static_cast<U32>(status)` and `filename = "/sequence_number.bin"`:

| Status | Helper result / value | Component action + event |
|---|---|---|
| OK, len==4 | LOADED, stored value | use value; none |
| MISSING | FIRST_BOOT, 0 | baseline 0; no event; NO write-back (today writes 0 back, `:63-65`; the write is pointless — 0 is the default — and it was a boot-time double-writer race, see §3) |
| TRUNCATED/BAD_MAGIC/BAD_LENGTH/BAD_CRC/BAD_VERSION/OK-with-len!=4 | CORRUPT, 0 | baseline 0; one `FileOpenError`; then `store(0)` so the corrupt file stops re-warning on every `GET_SEQ_NUM` re-read (`get_SequenceNumber` re-reads the file per command, `:385-388`) — this mirrors today's write-back of 0 on failure |
| OPEN_ERROR/READ_ERROR | CORRUPT, 0 (present-but-unreadable file is a storage fault; today it silently became 0) | same as corrupt row |
| store() failure (any of OPEN/WRITE/SYNC/RENAME) | false | one `FileOpenError(status, path)`; RAM value stands (today the write status is discarded, `:110`) |

### StartupManager (`C/StartupManager`)
- Boot count: magic `"SBC1"`, payload 8 bytes U64 LE (FwSizeType widened to U64 on disk so the format is target-independent). Quiescence start: magic `"SQS1"`, payload 11 bytes: `[0..1]` timeBase U16 LE, `[2]` context U8, `[3..6]` seconds U32 LE, `[7..10]` useconds U32 LE (mirrors `Fw::Time::SERIALIZED_SIZE`, `$R/lib/fprime/Fw/Time/Time.hpp:16`). Validation on OK: len match; quiescence useconds < 1e6 (keeps the eb2df70 boot-loop guard, `StartupManager.cpp:133-139`).
- Paths stay the params `BOOT_COUNT_FILE` `/boot_count.bin`, `QUIESCENCE_START_FILE` `/quiescence_start.bin` (`StartupManager.fpp:55,61`); temp path derived at runtime: `snprintf(tmp, sizeof tmp, "%s.tmp", path)` into a `char[64]` local (`/boot_count.bin.tmp`; LFN is already required by `quiescence_start.bin`); an overflowed temp path is treated as a store failure.
- `get_boot_count(increment)` (`:104-123`), event `BootCountUpdateFailure` (`fpp:33`):

| Status | Behaviour | Event |
|---|---|---|
| OK, len==8 | count = value | none |
| MISSING | count = 0 (first boot, as today) | none |
| corrupt class or OPEN/READ_ERROR | count = 0 ("boot count 0" per REQ-SM-008) | exactly one `BootCountUpdateFailure` |
| then | `count = FW_MAX(1, increment ? count+1 : count)` as today; if `increment`: `store`; store failure -> one more `BootCountUpdateFailure` (today's `:119-121`) | |

  `increment=false` (GET_BOOT_COUNT, `:228-232`) reads but no longer rewrites the file: today's rewrite of an identical value on every command is unobservable flash wear; the command's event/response are unchanged. (Keeping the rewrite is acceptable if the reviewer prefers zero delta.)
- `update_quiescence_start()` (`:125-149`), event `QuiescenceFileInitFailure` (`fpp:37`):

| Status | Behaviour | Event |
|---|---|---|
| OK, len==11, useconds < 1e6 | time = stored | none |
| MISSING | time = `getTime()`, `store` | none unless store fails (today `:142-146`) |
| corrupt class, OPEN/READ_ERROR, or useconds >= 1e6 | time = `getTime()` ("quiescence restarted"), `store` | exactly one `QuiescenceFileInitFailure` for the corruption; a second one only if the store also fails |

## 2. Legacy files on the first boot of the new image
- `/mode_state.bin` legacy = `sizeof(PersistentState)` = 12 bytes on ARM and host (U8, 3 pad, U32, U8, U8, 2 pad — NOT 7; the ledger line saying "raw 7-byte struct" is wrong). 12 >= OVERHEAD(11) and bytes 0-3 are `{mode, pad, pad, pad}` -> `BAD_MAGIC` -> boot SAFE / SYSTEM_FAULT, switches off, one `StatePersistenceFailure("load-corrupt", 2)` + `EnteringSafeMode`; safe-mode entry count restarts at 1. Operator: `EXIT_SAFE_MODE` once; the record is rewritten in the new format on that boot. Lost: the old entry count (telemetry only) and the old clean flag.
- `//sequence_number.txt` (4-byte big-endian U32 from `FileHelper`, binary despite `.txt`) is never read again (new path) and is orphaned (4 bytes of flash). New image: MISSING -> baseline 0, no event. Ground ahead of the satellite by < `SEQ_NUM_WINDOW` (50000) is accepted (`:238-239`), so the resync is silent; if the coder keeps the old path instead, the 4-byte file is TRUNCATED -> one `FileOpenError` and the same baseline 0.
- `/boot_count.bin` legacy = 8 (or 4) bytes big-endian -> TRUNCATED -> one `BootCountUpdateFailure`, `BootCount` telemetry restarts at 1. `/quiescence_start.bin` legacy = 11 bytes -> BAD_MAGIC -> one `QuiescenceFileInitFailure`, quiescence restarts at the upgrade boot: with `ARMED` true (default, not persistable via NullPrmDb) the startup sequence's `WAIT_FOR_QUIESCENCE` waits the full `QUIESCENCE_TIME` (45 min) once more.
- Migration: recommend against. A legacy blob has no magic, version or CRC, so a "migration" is a guess (an 8-byte file is indistinguishable from a torn new record); the counts are telemetry-only, the quiescence re-wait is a one-time 45-min cost, and a SAFE boot after a software update is conservative. Operational alternative with zero code: remove the three files (FileManager remove command, or `format_filesystem`) before rebooting into the new image, which makes the upgrade a clean first boot.

## 3. Authenticate: shared file, two writers, double slash
- Three instances are declared (`ComCcsdsLora/ComCcsds.fpp:101`, `ComCcsdsUart/ComCcsds.fpp:113`, `ComCcsdsSband/ComCcsds.fpp:101`) but only LoRa and UART are built (`ReferenceDeployment/Top/topology.fpp:19-22` comments out S-band). Both call `store` on the same target and the same `.tmp`; `dataIn` is `guarded` per instance only, and the two deframers run in different contexts, so two accepted packets on both links within one ~ms write window can interleave the temp file. Today's exposure is identical (both `open(OVERWRITE)` the same path). Difference after the change: an interleaved temp is caught by the CRC on the next load (baseline 0 + one warning + resync via `SET_SEQ_NUM`) instead of being silently adopted. Decision: accept, document in the sdd; per-instance temp names (from `init(instance)`) are a cheap follow-up if a board test ever shows a collision. Not addressed (pre-existing): each instance keeps its own RAM `sequenceNumber` and only the last writer's value is on disk; `SET_SEQ_NUM` writes the file but never updates RAM (`:401-407`).
- Double slash: FatFS tolerates redundant separators, so `//sequence_number.txt` and `/sequence_number.txt` are the same entry on target; the single-slash form is the project convention (`TelemetryGate.cpp:14-16`). Since the on-disk format changes anyway, use the new single-slash `.bin` name and leave the old file unread (no spurious first-boot warning, no delete I/O).

## 4. Harm table

| Existing behaviour | After | Notes |
|---|---|---|
| MM: NORMAL/SAFE restore, unintended-reboot detection, load-switch re-assertion at boot | identical for a valid record | tests `StateFileRoundTripsAcrossCleanRestart` / `UncleanRestartFromNormal...` (`UT/test_ModeManager_VoltageDebounce.cpp:310-351`) run two component lives with no raw seeding and pass unchanged |
| MM: corrupt file boots NORMAL + 1 event | boots SAFE/SYSTEM_FAULT + 1 event | the change MM0012 mandates; only reachable with a damaged file |
| MM: first boot on Zephyr emits a spurious `StatePersistenceFailure` (issue #1) | silent | required by MM0012 text |
| MM: one raw write per boot (clean=0) + raw write on every mode change + raw write in prepareForReboot | same count of stores, each now temp+flush+rename (2 file ops + rename) | the unintended-reboot boot path stores twice (enterSafeMode `:425` then `:341`), as today |
| MM: state written as 12-byte struct | 18-byte record | flash wear delta negligible |
| Auth: first boot writes 0 | no write | GET_SEQ_NUM still reports 0 |
| Auth: every accepted packet writes the file (`:373`) | same, atomic | `SET_SEQ_NUM` same |
| Auth: read failure silently -> 0 | -> 0 + one `FileOpenError` | new event only on a damaged/unreadable file |
| SM: boot count rewritten every boot | same (by design) | GET_BOOT_COUNT no longer rewrites |
| SM: read failure -> count 1 / quiescence now, silently | same defaults + one warning only for a present-but-invalid file; MISSING stays silent | |
| SM: FW_ASSERT on deserialize (`:57`) | gone (no serializer) | removes an assert path |
| Any: store failure | previous record intact and still valid (PersistedRecord-4) | today a failed overwrite leaves a truncated file |
| Dictionaries / topology / packets | unchanged | no `.fpp` edits |
| Stack | +MAX_RECORD_SIZE(75)+read slack per call inside PersistedRecord, +64 B temp-path local in StartupManager | acceptable on RP2350 |

## 5. File-by-file steps

Production (target build), in this order:
1. `C/ModeManager/ModeManager.hpp`: drop `#include <Os/File.hpp>` (`:7`); replace `struct PersistentState` (`:143-148`) with `static constexpr uint16_t STATE_PAYLOAD_SIZE = 7` and a `U8` magic array; add `STATE_TEMP_PATH = "/mode_state.tmp"` beside `:165`; add private helpers `void encodeState(U8 clean, U8* out) const` and `bool storeState(U8 clean, const char* op)` (returns false after emitting the failure event); keep `loadState()`/`saveState()` declarations (`:106,109`). Update the "State Persistence" comment.
2. `C/ModeManager/ModeManager.cpp`: include `PersistedRecordFile.hpp`; rewrite `prepareForReboot_handler` (`:168-203`) -> `PreparingForReboot` event + `storeState(1, "shutdown-store")`; rewrite `loadState` (`:247-342`) per §1 table, keeping the trailing `saveState()`; rewrite `saveState` (`:344-374`) -> `storeState(0, "save-store")`. `enterSafeMode/exitSafeMode/exitSafeModeAutomatic` (`:425,449,474`) untouched. Remove `<cstring>` if unused.
3. `C/ModeManager/CMakeLists.txt`: add `DEPENDS PROVESFlightControllerReference_Components_PersistedRecord` (pattern `C/TelemetryGate/CMakeLists.txt`).
4. `C/Authenticate/SequenceNumberStore.hpp/.cpp` (new, F´-free): constants, `load`, `store` per §1. Guard style `#ifndef Components_Authenticate_SequenceNumberStore_HPP` (cpplint).
5. `C/Authenticate/Authenticate.cpp`: delete `SEQUENCE_NUMBER_PATH` (`:21`) and the FileHelper include (`:11`); `readSequenceNumber()` (`:60-67`) -> helper `load`, CORRUPT -> `log_WARNING_HI_FileOpenError(status, path)` + `store(0)`; `writeSequenceNumber()` (`:108-112`) -> helper `store`, failure -> `FileOpenError`; drop the `filepath` parameter from both (call sites `:53,373,386,403`).
6. `C/Authenticate/Authenticate.hpp`: remove FileHelper and `Os/File.hpp` includes (`:7,9`) and the unused `Os::File m_sequenceNumberFile` (`:116`); update the two declarations (`:54,57`).
7. `C/Authenticate/CMakeLists.txt`: add `SequenceNumberStore.cpp` to SOURCES; replace `FprimeExtras_Utilities_FileHelper` with `PROVESFlightControllerReference_Components_PersistedRecord` in DEPENDS (grep confirms FileHelper is used nowhere else in the component).
8. `C/StartupManager/StartupManager.cpp`: replace `#include "Os/File.hpp"` (`:9`) with `PersistedRecordFile.hpp`; delete `read<T>`/`write<T>` (`:39-102`); add file-local `encodeU64LE/decodeU64LE`, `encodeTime/decodeTime`, `bool tempPathFor(const char* path, char* out, size_t cap)`; rewrite `get_boot_count` (`:104-123`) and `update_quiescence_start` (`:125-149`) per §1. `run_handler`, commands untouched. `StartupManager.hpp`: update the two doc comments (`:33-51`); `Status` enum (`:18-21`) may stay.
9. `C/StartupManager/CMakeLists.txt`: add `DEPENDS PROVESFlightControllerReference_Components_PersistedRecord`.

Host test support (`UT/support`):
10. `FpTypesStub.hpp`: add `typedef int64_t I64; typedef uint64_t U64;` and `#ifndef FW_MAX ... #endif` (from `$R/lib/fprime/Fw/Types/BasicTypes.h:91`), needed by StartupManager.
11. New `support/Fw/Time/Time.hpp`: `enum class TimeBase { TB_NONE, TB_PROC_TIME, TB_WORKSTATION_TIME }` (values as `$R/lib/fprime/Fw/Time/Time.fpp`), `Fw::Time` with the ctors/getters/`add`/`operator<=` used at `StartupManager.cpp:130-215`, `Fw::TimeIntervalValue{get_seconds,get_useconds}`, `Fw::TimeValue(base, ctx, s, us)`.
12. New `support/zephyr/drivers/rtc.h`: empty header plus `inline uint32_t k_uptime_seconds()` returning a test-settable value (`StartupManager.cpp:10,152`). Note in the file header that it is a fake, per README's no-Zephyr rule.
13. New `support/PROVESFlightControllerReference/Components/StartupManager/StartupManagerComponentAc.hpp` recorder (model: `support/.../ModeManager/ModeManagerComponentAc.hpp`): pure-virtual handlers `run_handler, completeSequence_handler, sequenceStarted_handler, WAIT_FOR_QUIESCENCE_cmdHandler, GET_BOOT_COUNT_cmdHandler`; settable `paramGet_ARMED/QUIESCENCE_TIME/QUIESCENCE_START_FILE/STARTUP_SEQUENCE_FILE/BOOT_COUNT_FILE` (defaults `fpp:49-61`); settable `getTime()`; recorders for `runSequence_out`, `cmdResponse_out`, `tlmWrite_BootCount`, `tlmWrite_QuiescenceEndTime`, and the five events `CurrentBootCount(I64)`, `BootCountUpdateFailure`, `QuiescenceFileInitFailure`, `StartupSequenceFinished`, `StartupSequenceFailed(CmdResponse)` (`fpp:29-46`).
14. `UT/CMakeLists.txt`: `target_link_libraries(mode_manager_component PUBLIC persisted_record_file)` after `:97`; add `authenticate_sequence_store` (SequenceNumberStore.cpp, include dirs support + root, links persisted_record_file) and `startup_manager_component` (StartupManager.cpp, same dirs, links persisted_record_file); append both to the per-test link list (`:107-118`).

Tests (`UT`), each `RecordProperty("verifies", ...)`; corruption loops follow `UT/test_TelemetryGate_Component.cpp:239-276` (reseed pristine, corrupt one byte over all 255 other values / resize to each shorter length, fresh component each iteration, assert exactly one event):
15. `UT/test_ModeManager_VoltageDebounce.cpp`: in `StateFileRoundTripsAcrossCleanRestart` (`:310`) add `RecordProperty("verifies","MM0011")` and reword the "Unclaimed by design" header (`:22-30, 302-308`): MM0007 stays unclaimed, MM0011's "commanded state round-trips across a component restart" is exactly this test. Leave the second test unclaimed (it is MM0008 regression cover). No seeding changes needed.
16. New `UT/test_ModeManager_StatePersistence.cpp` (fixture resets the fake FS; seeds a pristine record by running one life + `prepareForReboot_handler`; helper decodes `/mode_state.bin` with `PR::decode` + `"MMS1"`):
   - MM0011: `StateFileDecodesWithSharedCodecAndDocumentedLayout`; `PrepareForRebootStoresCleanFlagAndLeavesNoTemp`; `ModeChangeStoreFailureKeepsPreviousRecordAndEmitsOneEvent` (failRename).
   - MM0012: `NoFileBootsNormalWithZeroEvents` (and the file is created with clean=0); `EmptyFileBootsSafeWithOneEvent`; `EverySingleByteCorruptionBootsSafeWithSystemFaultAndOneEvent`; `EveryTruncationLengthBootsSafeWithSystemFaultAndOneEvent`; `WrongMagicBootsSafe`, `WrongVersionWithValidCrcBootsSafe` (patch byte 4, recompute with `PR::crc32`); `OutOfRangeFieldsInCrcValidRecordBootSafe` (mode 0/3, reason 6, clean 2 via `PR::encode`); `LegacyRawStructFileBootsSafeWithOneEvent` (12-byte layout from §2). Each asserts `currentMode==SAFE_MODE`, reported reason `SYSTEM_FAULT`, `eventsStatePersistenceFailure.size()==1`, and that the rewritten file decodes OK.
17. New `UT/test_Authenticate_SequenceNumberStore.cpp` (AUTH013): `ValidFileRoundTripsStoredValue` (incl. 0, 1, 0xFFFFFFFF); `NoFileIsFirstBootBaselineZeroAndWritesNothing`; `EverySingleByteCorruptionIsCorruptExactlyOnceWithBaselineZero`; `EveryTruncationLengthIsCorruptWithBaselineZero`; `WrongMagicAndWrongVersionAreCorrupt`; `StoreFailureKeepsPreviousRecord` (failRename/failFlush); `LegacyFourByteFileAtOldPathIsIgnored` (documents the orphan). Plan text must state that "exactly one warning event" is evidenced as exactly one CORRUPT result per load plus the one-line mapping in `Authenticate.cpp` checked by the target compile.
18. New `UT/test_StartupManager_Persistence.cpp` (REQ-SM-008), driving the public `get_boot_count(true/false)` / `update_quiescence_start()` and `run_handler` through the base: `FirstBootWithNoFilesCountsOneAndWritesQuiescenceWithoutEvents`; `BootCountRoundTripsAndIncrementsAcrossRestarts`; `QuiescenceStartRoundTripsAndIsNotRewritten`; `EverySingleByteCorruptionOfBootCountWarnsOnceAndRestartsAtOne`; `EveryTruncationOfBootCountWarnsOnceAndRestartsAtOne`; `EverySingleByteCorruptionOfQuiescenceWarnsOnceAndRestartsNow`; `EveryTruncationOfQuiescenceWarnsOnceAndRestartsNow`; `UsecondsOutOfRangeInValidRecordIsCorrupt`; `StoreFailureWarnsAndKeepsPreviousRecord`; `GetBootCountCommandReadsWithoutRewriting`; `LegacyFilesWarnOnceEachAndApplyDefaults`.

Docs (prose only; never the `## Requirements` tables, which `scripts/req.py` owns):
19. `C/ModeManager/docs/sdd.md` "State Persistence" (`:119-125`): record layout, magic, temp path, status table from §1, legacy note from §2, issue #1 closure; add a `## Change Log` (`| Date | Description |`, none exists) with a Sep 2026 row.
20. `C/Authenticate/docs/sdd.md`: add a "Sequence Number Persistence" subsection near `## Events` (`:178`), fix the `FileOpenError` row (`:189`) to the fpp signature `(error: U32, filename)` and say it is now emitted for a corrupt/unreadable/unstorable sequence file; two-writer note from §3; add `## Change Log` (none exists).
21. `C/StartupManager/docs/sdd.md`: Purpose/Boot Counting (`:10`), Parameters (`:74`) mention `.tmp`, Events rows (`:86-87`) now also cover corrupt files, REQ-SM-007 prose untouched; Change Log row (`:115-118`, `| Date | Author | Description |`).
22. `C/PersistedRecord/docs/sdd.md`: Motivation table (`:13-19`) rows for the three files -> "Migrated (MM0011/12, AUTH013, REQ-SM-008)", `/sequence_number.txt` -> `/sequence_number.bin`; Consumers section unchanged; Change Log row.
23. Optional: `cp` each edited sdd to `docs-site/components/<Name>.md` (Makefile `docs-sync` cannot run at this path; the four copies exist).

## 6. Verification
- `VERIFY_ENV=host scripts/verify.sh` from `$R`: "host unit tests" lists 11 binaries (8 today + `test_ModeManager_StatePersistence`, `test_Authenticate_SequenceNumberStore`, `test_StartupManager_Persistence`), every line `PASSED`; int collect/lint unchanged; pre-commit hooks pass (watch cpplint guards on the new headers, codespell); RTM line goes from `75 linked / 24 passing / 51 deferred` to `79 linked / 28 passing / 51 deferred` (MM0011, MM0012, AUTH013, REQ-SM-008 all have criteria, so no "linked but no criteria" warning); `DH-L2-11` and `MS-L2-07` stay deferred/CDR-Partial (Board). Result `PASS`, unverified `(none)`.
- Target compile: `rsync` then `fprime-util build` in `~/scalar-build/proves-core-reference` per CLAUDE.md. Expected: links; flash grows by a few hundred bytes (PersistedRecord already linked via TelemetryGate; the FileHelper template instantiations drop out of Authenticate). Failure modes to watch: DEPENDS token spelling, `snprintf` include (`<cstdio>`), `I32` cast of the `enum class` status.
- Deferred to hardware: DH-L2-11 / PersistedRecord-5 power-cut cycles; issue #1's on-board first-boot check (zero `StatePersistenceFailure`, NORMAL after `format_filesystem`); MM0007/MS-L2-07 warm-reset persistence (`safe_mode_test.py::test_safe_09`); the LoRa+UART simultaneous-uplink collision.

## 7. Non-goals and open risks
- Non-goals: any `.fpp`/dictionary change; per-instance temp paths for Authenticate; migrating legacy blobs; fixing `SET_SEQ_NUM` not updating RAM (`Authenticate.cpp:401-407`) or the per-instance RAM divergence; revisiting TelemetryGate's OPEN_ERROR-as-missing choice; the `//seq/startup.bin` comparison in `StartupManager.cpp:169`; docs-site nav.
- Risks: (1) the upgrade boot lands in SAFE mode once and re-waits quiescence once (§2) — call out in the commit message; (2) AUTH013 evidence sits in the helper, not the component (§5.17); (3) `FileOpenError` is `throttle 2` so a persistently unstorable file goes quiet after two events per boot (same class as `SequenceNumberOutOfWindow`); (4) the FAT rename window (PersistedRecord-5) is unchanged and board-only; (5) `FwSizeType` width on target unverified — the 8-byte LE payload is width-independent by construction, but the coder must cast through U64 both ways.

## Findings (verified facts, file:line)
- WRONG in ledger: ModeManager's raw struct is 12 bytes on disk, not 7 — `sizeof(PersistentState)` with alignment padding (`C/ModeManager/ModeManager.hpp:143-148`, written via `sizeof` at `ModeManager.cpp:191,361`); legacy file therefore hits BAD_MAGIC, not TRUNCATED.
- Ledger precision: three Authenticate instances are declared (`ComCcsdsLora/ComCcsds.fpp:101`, `ComCcsdsUart/ComCcsds.fpp:113`, `ComCcsdsSband/ComCcsds.fpp:101`) but S-band is commented out of `ReferenceDeployment/Top/topology.fpp:22`, so two are built.
- Brief said the two ModeManager restart tests "seed raw-struct files": they do not; both run two component lives (`UT/test_ModeManager_VoltageDebounce.cpp:310-351`) and are format-agnostic.
- `ModeManager::init` calls `loadState()` at `ModeManager.cpp:37-40`; `saveState()` callers: `:341` (end of loadState), `:425` (enterSafeMode), `:449` (exitSafeMode), `:474` (exitSafeModeAutomatic); `prepareForReboot_handler` `:168-203`.
- `StatePersistenceFailure(operation: string size 20, status: I32)` `C/ModeManager/ModeManager.fpp:117-123`; today's op strings: load-corrupt, load-read, load-open, save-open, save-write, shutdown-open, shutdown-write.
- ModeManager restores `safeModeReason` and `cleanShutdown` without range checks `ModeManager.cpp:266,271`.
- Existing ModeManager recorder stub already records `StatePersistenceFailure{op,status}` (`UT/support/.../ModeManager/ModeManagerComponentAc.hpp:72,151,232`) and `UnintendedRebootDetected` (`:147`); `init()` is a no-op (`:86-89`).
- Authenticate includes `<psa/crypto.h>` (`Authenticate.cpp:9`), FileHelper (`:11`, `Authenticate.hpp:7`), and `AuthDefaultKey.h` (`:16`, checked-in copy exists, generated by `$R/scripts/generate_auth_key_header.py`); host compile of the component is impractical.
- Authenticate file I/O: `readSequenceNumber` `:60-67`, `writeSequenceNumber` `:108-112`, callers `:53` (init), `:373` (accepted packet), `:386` (`get_SequenceNumber`, re-reads the file per GET_SEQ_NUM), `:403` (SET_SEQ_NUM, writes file only, RAM `sequenceNumber` untouched). `Os::File m_sequenceNumberFile` `Authenticate.hpp:116` is unused.
- `FileOpenError(error: U32, filename: string size 64)` warning high, throttle 2, declared `Authenticate.fpp:36`, never emitted in `Authenticate.cpp`; the sdd Events table (`docs/sdd.md:189`) lists a stale one-arg signature.
- `FileHelper` is only used by Authenticate in the project tree; `readFromFile` maps short reads to `BAD_SIZE` (`$R/lib/fprime-extras/FprimeExtras/Utilities/FileHelper/FileHelper.hpp:206-224`) and serializes big-endian.
- StartupManager: includes `<zephyr/drivers/rtc.h>` `:10` and calls `k_uptime_seconds()` `:152`; `get_boot_count` `:104-123` writes the file even when `increment=false` (GET_BOOT_COUNT `:228-232`); `update_quiescence_start` `:125-149`; `run_handler` first-call block `:181-189`; events `StartupManager.fpp:29-46`; params with defaults `:49-61`; `get_boot_count`/`update_quiescence_start` are public (`StartupManager.hpp:41,51`).
- `FW_MAX` is `$R/lib/fprime/Fw/Types/BasicTypes.h:91`; `Fw::Time::SERIALIZED_SIZE` = base(2)+context(1)+4+4 = 11 (`$R/lib/fprime/Fw/Time/Time.hpp:16`); `TimeBase` values in `$R/lib/fprime/Fw/Time/Time.fpp`.
- Host stubs lack `I64`/`U64`/`FW_MAX` (`UT/support/FpTypesStub.hpp:15-26`); no `Fw/Time` stub exists; no Authenticate or StartupManager recorder stub exists (`UT/support/PROVESFlightControllerReference/Components/` has ModeManager, TelemetryGate, ThermalManager).
- Host `Os::File` fake: OPEN_READ never fails except DOESNT_EXIST, so OPEN_ERROR/READ_ERROR paths are untestable on host (`UT/support/Os/File.hpp:73-93`); fault flags are failOpenCreate/failWrite/partialWrite/failFlush/failRename (`:29-33`).
- `mode_manager_component` does not yet link `persisted_record_file` (`UT/CMakeLists.txt:91-97`); TelemetryGate does (`:76`).
- Component CMake dependency token for the helper is `PROVESFlightControllerReference_Components_PersistedRecord` (`C/TelemetryGate/CMakeLists.txt`).
- `PersistedRecord::load` returns MISSING (not a corruption status) when the target is absent and the temp is anything but fully valid (`C/PersistedRecord/PersistedRecordFile.cpp:63-73`); an empty (0-byte) present file decodes TRUNCATED.
- RTM at 96a0ed7: 262 requirements, 75 linked, 24 passing, 51 deferred (`$R/docs-site/requirements-matrix.md:16`); MM0011/MM0012/AUTH013/REQ-SM-008 rows have criteria and no tests (`:403-404,217,479`).
- `scripts/verify.sh` lists host binaries by globbing `build-gtest/test_*` and greps the RTM "linked to automated" line (`$R/scripts/verify.sh:38-46,86-90`); 8 `test_*.cpp` exist today.
- Makefile `docs-sync` copies the four sdd files to `docs-site/components/{ModeManager,StartupManager,PersistedRecord,Authenticate}.md` (`$R/Makefile:98,99,124,126`); `make` cannot run at this path.
- GitHub issue #1 asks for a board test `@pytest.mark.verifies("MM0012")` on a fresh filesystem; that is Board-level and stays deferred.
- `ModeManager.hpp:7` includes `<Os/File.hpp>` (angle form); ModeManager sdd and Authenticate sdd have no Change Log section; StartupManager's uses `| Date | Author | Description |` (`docs/sdd.md:117`).
