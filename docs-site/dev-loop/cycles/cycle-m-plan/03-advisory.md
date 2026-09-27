# 03 — Advisory: design and method (the coder may deviate if every result in README §Rows, 01 and 02 holds)

## 1. File layout (`P/Components/DataRecorder/`)
| File | Holds |
|---|---|
| `SegmentCodec.hpp/.cpp` | 01 §8 codec; includes `<cstdint>`, `config/FppConstantsAc.hpp` (for `FW_COM_BUFFER_MAX_SIZE`) and `PersistedRecord/PersistedRecordCodec.hpp` (for `crc32`, D-001 reuse); magic in the `.cpp` anonymous namespace (ledger: `static constexpr U8 MAGIC[4]` members fail to link under C++14) |
| `PacketRing.hpp` | header-only template + `flushDue` (inline); slot = `{ uint16_t len; uint8_t bytes[SLOT_BYTES]; }`; plus `uint32_t oldestId()` (count of records ever removed or dropped) used by §3 |
| `DataRecorderCfg.hpp` | 01 §6 constants as `constexpr`; per-stream tables indexed by `RecorderStream` ordinal, sized `NUM_STREAMS = 2` (A9-2 makes it 3) |
| `DataRecorder.fpp` | 01 §1-§4; `enum RecorderStream : U8`, `ConfigField`, `DeleteReason` in `module Components` |
| `DataRecorder.hpp/.cpp` | per-stream `struct StreamState` (ring, scan state, open segment seq/bytes/ticks, oldest/second seq + open times + header validity, nextSeq, counters, config); `Os::Mutex m_fileLock`; `U8 m_stage[STAGE_BYTES]`; `Os::Directory m_dir[NUM_STREAMS]`; magics and paths in the anonymous namespace |
| `CMakeLists.txt` | below |
| `docs/sdd.md` | seeded in A8-0; A8-2 adds Overview, Ports, Commands, Telemetry, Events, File formats (link to cycle-m-plan 01 §5), Configuration, Failure behaviour — above `## Requirements` |

```cmake
register_fprime_library(
    AUTOCODER_INPUTS "${CMAKE_CURRENT_LIST_DIR}/DataRecorder.fpp"
    SOURCES "${CMAKE_CURRENT_LIST_DIR}/SegmentCodec.cpp" "${CMAKE_CURRENT_LIST_DIR}/DataRecorder.cpp"
    DEPENDS PROVESFlightControllerReference_Components_PersistedRecord Os Fw_Types
)
```
Traps honoured: `DEPENDS` listed (CLAUDE.md: a library with no DEPENDS gets no include path on 4.3.0; the host build does not
catch it). If the target compile cannot find `Svc/FileDownlinkPorts/...` or `Svc/Sched/...` headers, add `Svc_FileDownlinkPorts`
/ `Svc_Sched` to `DEPENDS`. Command handlers take `const Components::RecorderStream& stream` (F´ 4.3.0 `const T&` rule; a
by-value override fails on target only). No parameter reads anywhere (there are no parameters). `P/Components/CMakeLists.txt`:
`add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/DataRecorder/")` after `ComDelay/`.

## 2. Host build (`UT/CMakeLists.txt`, same shape as `persisted_record_file` `:88-98` and `telemetry_gate_component` `:112-121`)
```cmake
add_library(data_recorder_codec STATIC .../Components/DataRecorder/SegmentCodec.cpp)
target_include_directories(data_recorder_codec PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/support ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
target_link_libraries(data_recorder_codec PUBLIC persisted_record_codec)
add_library(data_recorder_component STATIC .../Components/DataRecorder/DataRecorder.cpp)   # A8-2
target_include_directories(data_recorder_component PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/support ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
target_link_libraries(data_recorder_component PUBLIC data_recorder_codec persisted_record_file)
```
and both names in the `target_link_libraries(${test_name} ...)` list (`UT/CMakeLists.txt:~292-321`). `support/` first so `Os/*.hpp`,
`config/FppConstantsAc.hpp` and the stub resolve to the fakes. Stub path
`UT/support/PROVESFlightControllerReference/Components/DataRecorder/DataRecorderComponentAc.hpp`: copy the shape of
`.../TelemetryGate/TelemetryGateComponentAc.hpp` (recording vectors, handlers as public pure virtuals) and the lock accounting
of `.../DriverBoardHandler/DriverBoardHandlerComponentAc.hpp:173-177,208-213,263-270`. It declares the three enums (as
TelemetryTxState is mirrored there), `Svc::SendFileStatus`/`SendFileResponse`, and uses the existing `Fw/Time`,
`Fw/Types/String.hpp` stubs and the new `Fw/Com/ComBuffer.hpp`. `support/config/FppConstantsAc.hpp` gains
`enum { FW_COM_BUFFER_MAX_SIZE = 227 };` at global scope (same form as the generated
`build-fprime-automatic-zephyr/F-Prime/default/config/FppConstantsAc.hpp:349`).

## 3. Algorithms
- **Two locks.** The guarded-port mutex (`lock()`/`unLock()`, taken by the generated `tlmIn`/`evtIn` wrappers) protects only
  the rings; hold it for memcpy-sized work and never across a port call or an `Os::` call. `m_fileLock` (`Os::Mutex`) serialises
  all file work between `schedIn` (1 Hz thread) and the file-touching commands (cmdDisp thread); order is always
  `m_fileLock` → component lock. Producers: `tlmIn` runs on `tlmSend`'s thread (TlmPacketizer is active, `Run` async,
  `TlmPacketizer.fpp:3,37`); `evtIn` runs on EventManager's thread (`LogRecv` is sync but enqueues to the internal `loqQueue`,
  `EventManager.cpp:58,68,88`), so neither ever runs on the recorder's own thread and an event the recorder logs cannot re-enter it.
- **Stage and reconcile.** Flush: under the component lock, copy (framing with `encodeRecord`) the oldest whole records that fit
  into `m_stage` and remember `firstId = ring.oldestId()` and `n`; unlock; open (`OPEN_APPEND`), one `write`, check the size,
  explicit `flush()` (CLAUDE.md: `write(WAIT)` discards the flush status), close. On success lock again and pop
  `max(0, firstId + n − ring.oldestId())` records — records the producer dropped meanwhile are already gone. `peek`+`pop` never
  hand out a pointer across the unlock.
- **Scan as a state machine** per stream: `DIRS → OPEN → READING → HEADERS → READY`, restart on error; `m_dir[s]` stays open
  across ticks while `READING` (`FS_FATFS_NUM_DIRS` default 4). Track `min1`, `min2`, `max`, count, byte sum. Never call
  `Directory::getFileCount`/`readDirectory`: both rewind, and Zephyr `rewind` is `NOT_SUPPORTED` (`fprime-zephyr/Os/Directory.cpp:47-49`).
- **Oldest bookkeeping without a RAM index.** Keep `oldestSeq/oldestOpen/oldestValid` and `nextSeq2/next2Open/next2Valid`.
  After deleting the oldest, promote the second and find the new second by `exists()` probes from `second + 1` towards
  `nextSeq − 1` (≤ `SCAN_BUDGET` probes per tick; resume next tick), then read its header. `getFileSize` before `removeFile`
  so `BytesOnDisk` stays exact. `DELETE_SEGMENT` of a middle segment only adjusts counts (and re-probes if it was the second).
- **Name skip.** Before creating segment `nextSeq`, `exists()`; if present, advance (bounded by `SCAN_BUDGET` per tick).
- **Time.** `getTime()` once per tick; age arithmetic in `I64`, clamp negatives to 0 (an RTC that lost time must not make
  older segments look expired).
- **Commands under `m_fileLock`.** `SAVE_CONFIG` creates `/rec` if missing, then `PersistedRecord::store("/rec/config.bin",
  "/rec/config.tmp", DRC1, payload, 26)`. `LIST_SEGMENTS` probes with `exists()`, `getFileSize`, and a 20-byte header read per
  hit (≤ 64 probes, ≤ 16 events). `DOWNLINK_NEWEST` builds the path with a fixed `char[24]` and `snprintf("%s/%08u.bin")`, then
  `Fw::String`.

## 4. Tests (shape only; the author writes from 01/02)
- `test_DataRecorder_Codec.cpp`: golden vector (01 §5.4) both directions; exhaustive single-byte flips and truncations over a
  3-record segment; `flushDue` truth table; ring fill/overflow/`setCapacity`/`peek` order.
- `test_DataRecorder_Component.cpp`: `Os::Test::resetFileSystem()` per test, a fixture that constructs the component, sets time,
  ticks until both scans report ready; helpers `push(stream, bytes)`, `tick(n)`, `lastTlm("TlmBytesOnDisk")`. Failure injection
  via the fake flags; `onOperation` hook asserting `stub.lockDepth == 0` for DataRecorder-3/§7.8; `reverseDirectoryOrder` for
  DataRecorder-11.
- `scripts/tests/test_recorder_reader.py`: builds the golden segment and a minimal dictionary JSON (keys `typeDefinitions`
  aliases for `FwPacketDescriptorType` U16, `FwTlmPacketizeIdType` U16, `FwEventIdType` U32, `FwTimeBaseStoreType` U16,
  `FwTimeContextStoreType` U8, `FwSizeStoreType` U16; `telemetryChannels`; `events`; `telemetryPacketSets[0].members`) in
  `tmp_path`, runs the script with `subprocess` like `test_check_capacity.py`; `@pytest.mark.verifies("DataRecorder-12")`.
- `P/test/int/data_recorder_test.py`: follow `telemetry_gate_test.py` (module docstring with marker choice, `proves_send_and_assert_command`,
  an autouse fixture restoring defaults: `SET_RETENTION_S` back to 604800 and `SAVE_CONFIG`). Tests: config apply/reject/persist
  (DH-L2-05), age retention with retention 60 s (DH-L2-08, `slow`), event survives COLD_RESET and decodes (DH-L2-12, CDH-16,
  `slow`; COLD_RESET as `reset_manager_test.py:24`). Downlinked files land in the GDS file-storage directory; the test finds the
  file after `FileSent` and runs the reader on it.
- HP-12 extension: cut power at random 0..500 ms after `CLOSE_SEGMENT EVT` while an event burst is flushing; after each of ≥ 20
  boots, `DOWNLINK_NEWEST EVT` + reader: exit 0 or 1, never a wrong record (every decoded row's time ≤ cut time), no
  `ConfigCorrupt`. HP-13 extension: during the 70 s TRANSMIT DISABLED window the 3 `OpCodeCompleted` events are also in the
  EVT segment retrieved after ENABLE by `DOWNLINK_NEWEST EVT` (DH-L2-01 clause 2 evidence, not claimed this cycle).

## 5. Topology edits (A8-3)
`instances.fpp` after `:278`: `  instance dataRecorder: Components.DataRecorder base id 0x10080000` (next free after
`driverBoardHandler` `0x1007F000`; subtopology bases are `0x01..`, `0x02..`, `0x05..`, `0x21-0x23..`, `0xA0..`, no clash).
`topology.fpp`: `instance dataRecorder` after `:95`; the block of 01 §1.2 after `connections DriverBoard { }` (`:552-572`),
with a comment that slot 21 must follow `faultManager.run` (slot 20). `TlmPacketizerCfg.hpp`: `256` → `288`, comment kept.
`.fppi` `:205-208`: the 14 `dataRecorder.*` lines after `fsSpace.TotalSpace`.

## 6. Target-compile procedure (A8-2 and A8-3; CLAUDE.md §Commands)
From `$R`: the rsync line in CLAUDE.md (all excludes, never `--delete-excluded`); then in `~/scalar-build/proves-core-reference`
with `VIRTUAL_ENV`, `PATH`, `ZEPHYR_SDK_INSTALL_DIR` exported: `fprime-util generate --force` (new `.fpp`) then `fprime-util build`.
Record the two `Memory region` lines (FLASH of 1044144 B, RAM of 532480 B). Dictionary counts:
`$PY -c "import json;d=json.load(open('<copy>/build-artifacts/zephyr/fprime-zephyr-deployment/dict/ReferenceDeploymentTopologyDictionary.json'));print(len(d['commands']),len(d['parameters']),len(d['telemetryChannels']),len(d['events']),len(d['telemetryPacketSets'][0]['members']))"`
and `scripts/check_capacity.py --dictionary <that path>` (dispatch 396/512 after A8-3). Check the generated
`build-fprime-automatic-zephyr/PROVESFlightControllerReference/ReferenceDeployment/Top/ReferenceDeploymentTopologyAc.cpp`
`set_comOut_OutputPort` lines (01 §1.2). Revert rehearsal: `git revert --no-commit <A8-3>` in a scratch worktree, rebuild,
compare counts, discard.

## 7. Open risks
1. SD write time per tick is unmeasured: bound is one ≤ 4 KB write + flush per stream per tick; watch `RateGroupCycleSlip` on
   the bench; the lever is `STAGE_BYTES`/`FLUSH_RECORDS` in a `build(config)` commit.
2. `fileDownlink` reading a segment that retention or `DELETE_SEGMENT` removes (FatFs file lock behaviour on unlink of an open
   file is unverified); retention only ever takes the oldest, `DOWNLINK_NEWEST` the newest.
3. Boot-count header field is `0xFFFF` until a source exists (adding a port to StartupManager was out of scope).
4. Only packets `tlmSend` emits are recorded (default packet level 1 = Beacon only; TelemetryGate DISABLED = nothing): open
   decisions 1-2 of `design/stored-data/04-plan.md`.
5. Event storms > 32 events between ticks drop the oldest (`EvtRingDropped`); evt flush 8 / 10 s.
6. `FileSystem` packet grows from 35 to 87 B when ops raise the level to 5 on LoRa.
7. Cycle L collision on base ids and the channel budget (README §Hazards).
8. SPI0 is shared with the unwired S-band module (C-31): revisit the flush bound if S-band is ever driven.
9. `getFreeSpace("/")` on the FAT mount: FsSpace uses `"/prmDb.dat"` (`FsSpace.cpp:30`); if `"/"` fails on target, use the same.
10. DataRecorder-12 puts a Python tool's row in a component group; acceptable because the reader is part of the component's
    deliverable (`02-design.md` §7.3), but a reviewer may prefer a tooling group.
