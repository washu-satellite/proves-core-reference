# 01 — Normative: results the tests are written from

Every result here is observable on the host through the recorder stub (`DataRecorderComponentAc.hpp`) and the Os fakes, or
from a command line / file diff. Names and numbers taken from `design/stored-data/02-design.md` are used verbatim; everything
marked **[decided]** is where the design is silent or wrong (reasons in `04-findings.md` §B). All multi-byte fields in files
this component writes are **little-endian**; the `Fw::Com` bytes inside records are F´'s own **big-endian** serialization.

## 1. Component

### 1.1 Shape
`Components.DataRecorder`, `passive component` (no thread, no queue; the `.fpp` contains no `async`). Directory `P/Components/DataRecorder/`.

| Port | Kind / type | Notes |
|---|---|---|
| `tlmIn` | `guarded input port: Fw.Com` | telemetry packets (stream TLM) |
| `evtIn` | `guarded input port: Fw.Com` | event packets (stream EVT) |
| `schedIn` | `sync input port: Svc.Sched` | 1 Hz tick; all periodic file work |
| `sendFileOut` | `output port: Svc.SendFileRequest` **[decided]** | used only by `DOWNLINK_NEWEST` |
| standard | `time get`, `command reg/recv/resp`, `event`, `text event`, `telemetry` | no parameter ports (config is a PersistedRecord) |

A9-2 adds `burstIn: Fw.Com` and enumerator `BURST = 2`; A8 declares neither.

### 1.2 Wiring (A8-3), written verbatim in a new `connections DataRecorder { }` block of `T/topology.fpp`
```
comSplitterTelemetry.comOut[2] -> dataRecorder.tlmIn
comSplitterEvents.comOut[2] -> dataRecorder.evtIn
rateGroup1Hz.RateGroupMemberOut[21] -> dataRecorder.schedIn
dataRecorder.sendFileOut -> FileHandling.fileDownlink.SendFile
```
The splitter taps are written **indexed** (`[2]`). The existing unindexed LoRa/UART connections (`topology.fpp:154-155,159-160`)
keep 0 and 1: in the generated `ReferenceDeploymentTopologyAc.cpp`, `set_comOut_OutputPort(0, ComCcsdsLora...)` and
`(1, ComCcsdsUart...)` are unchanged for both splitters, and index 2 is the recorder (ComSplitter calls outputs in index order,
`lib/fprime/Svc/ComSplitter/ComSplitter.cpp:30-35`, so both downlink queues get each packet before the recorder does).

## 2. Types (module `Components`)
- `enum RecorderStream : U8 { TLM = 0, EVT = 1 }` (A9-2 appends `BURST = 2`). Segment header `stream` byte = this value.
- `enum ConfigField : U8 { RING_SLOTS = 0, FLUSH_RECORDS = 1, FLUSH_INTERVAL_S = 2, CAPACITY_BYTES = 3, RETENTION_S = 4 }` **[decided]**
- `enum DeleteReason : U8 { CAPACITY = 0, AGE = 1, COMMAND = 2 }`

## 3. Commands (all `sync`, in this order; opcodes base + 0..8)

| Command (args) | Accepted range | Rejection |
|---|---|---|
| `SET_RING_SLOTS(stream: RecorderStream, slots: U8)` | `1 ≤ slots ≤ 32` and `slots ≥ flushRecords(stream)` | `ConfigRejected(RING_SLOTS, slots, 32)` |
| `SET_CAPACITY(stream, bytes: U32)` | `SEGMENT_MAX_BYTES(stream) ≤ bytes ≤ L`, `L = min(2^32−1, total − RESERVE_BYTES − capacity(other stream))` from `Os::FileSystem::getFreeSpace("/", total, free)`; `L = 0` if that call fails or the difference is negative | `ConfigRejected(CAPACITY_BYTES, bytes, L)` |
| `SET_RETENTION_S(stream, seconds: U32)` | `60 ≤ seconds ≤ 2592000` | `ConfigRejected(RETENTION_S, seconds, 2592000)` |
| `SET_FLUSH(stream, records: U8, intervalS: U16)` | `1 ≤ records ≤ ringSlots(stream)`, then `1 ≤ intervalS ≤ 3600` | first failing field: `(FLUSH_RECORDS, records, ringSlots)` or `(FLUSH_INTERVAL_S, intervalS, 3600)` |
| `SAVE_CONFIG()` | — | `EXECUTION_ERROR` when `PersistedRecord::store` ≠ OK (no event) |
| `LIST_SEGMENTS(stream, fromSeq: U32)` **[fromSeq decided]** | — | `BUSY` while that stream's boot scan is incomplete |
| `DELETE_SEGMENT(stream, seq: U32)` | the file exists and is not the open segment | `VALIDATION_ERROR`; `removeFile` failure → `EXECUTION_ERROR`; `BUSY` while scanning |
| `CLOSE_SEGMENT(stream)` | — | `BUSY` while scanning; flush write failure → `EXECUTION_ERROR` |
| `DOWNLINK_NEWEST(stream)` **[decided, 9th command]** | — | `BUSY` while scanning; close failed, no segment on disk, or `sendFileOut` status ≠ `STATUS_OK` → `EXECUTION_ERROR` |

- A rejected SET returns `VALIDATION_ERROR`, emits exactly one `ConfigRejected`, and changes **no** configuration value and no
  ring content. An accepted SET returns `OK` and emits one `ConfigApplied(stream, field, value)` per field (SET_FLUSH: two).
  Values are in force from the next `schedIn`; `SET_RING_SLOTS` shrinks the ring at once, dropping the surplus oldest records
  into `RingDropped`. Nothing is persisted until `SAVE_CONFIG`.
- The first config command before the first `schedIn` loads `/rec/config.bin` first (§5.5), so a later tick never overwrites it.
- `LIST_SEGMENTS`: `OK`; one `SegmentInfo` per present segment with `seq ≥ max(fromSeq, oldest seq)`, ascending, stopping after
  16 events, 64 sequence numbers probed, or `seq ≥ nextSeq`.
- `CLOSE_SEGMENT`: if the ring is non-empty, one flush batch (§7.3) is written regardless of the flush trigger; afterwards no
  segment is open (the next record opens a new one). `OK` also when there was nothing to do.
- `DOWNLINK_NEWEST`: `CLOSE_SEGMENT`, then `sendFileOut(0, P, P, 0, 0)` with `P` the path of the highest-numbered segment of the
  stream; on `STATUS_OK`: `OK` and one `SegmentInfo` for that segment.

## 4. Telemetry and events

Channels, all `U32`, 7 per stream, names `Tlm<X>` / `Evt<X>` **[prefix decided]**, `X` ∈ `RecordsStored` (records accepted
into the ring since boot), `RecordsOnDisk` (records written to segments since boot **[decided meaning]**), `BytesOnDisk`
(sum of the stream's segment file sizes), `SegmentsOnDisk`, `OldestRecordAgeS` (`max(0, now.s − open seconds of the oldest
segment)`; 0 when none or its header is invalid), `RingDropped` (cumulative), `WriteFailures` (cumulative failed attempts).
All 14 are written on every `schedIn`, including while scanning. In A8-3 all 14 go into `packet FileSystem id 5 group 5`
after `fsSpace.TotalSpace`, in the order `Tlm*` then `Evt*`, each in the order above.

| Event (args) | Severity | Throttle |
|---|---|---|
| `SegmentOpened(stream, seq: U32)` | ACTIVITY_LO | — |
| `SegmentWriteFailed(stream, status: U32)` | WARNING_HI | 5 |
| `SegmentDeleted(stream, seq: U32, reason: DeleteReason)` | ACTIVITY_LO | — |
| `ConfigRejected(field: ConfigField, value: U32, max: U32)` | WARNING_LO | — |
| `ConfigCorrupt(status: U8)` **[arg decided]** | WARNING_HI | 5 |
| `SegmentInfo(stream, seq: U32, bytes: U32, openTimeS: U32)` | ACTIVITY_HI | — |
| `DirectoryScanFailed(stream, status: U32)` **[decided]** | WARNING_HI | 5 |
| `ConfigApplied(stream, field: ConfigField, value: U32)` **[decided]** | ACTIVITY_HI | — |

`status` in `SegmentWriteFailed` is the ordinal of the failing `Os::File` / `Os::FileSystem` status; a short write reports
`Os::File::BAD_SIZE`. `ConfigCorrupt.status` is the `PersistedRecord::Status` ordinal, or 255 when the record is CRC-valid but its payload fails §5.5.

## 5. Files

### 5.1 Layout
`/rec/` (created on the first tick), `/rec/tlm/`, `/rec/evt/` (A9: `/rec/burst/`), `/rec/config.bin` + temp `/rec/config.tmp`.
Segment name: 8 decimal digits + `.bin`, e.g. `/rec/tlm/00000042.bin`. Only names matching `^[0-9]{8}\.bin$` are segments;
every other entry is ignored and never deleted. The recorder creates or changes no path outside `/rec/`.

### 5.2 Segment header (20 bytes) **[size decided: the design's "16 B" does not hold its own fields]**
| Offset | Size | Field |
|---|---|---|
| 0 | 4 | magic `53 45 47 31` ("SEG1") |
| 4 | 1 | version = 1 |
| 5 | 1 | stream (`RecorderStream`) |
| 6 | 2 | boot count = `0xFFFF` (no boot-count source is wired; reserved) **[decided]** |
| 8 | 4 | open time seconds (`getTime()` when the segment is created) |
| 12 | 4 | open time microseconds |
| 16 | 4 | CRC-32 over bytes 0..15 |

### 5.3 Record (repeated after the header)
`len u16` (1..227 = `FW_COM_BUFFER_MAX_SIZE`) | `len` bytes (the `Fw::ComBuffer` exactly as received) | `CRC-32 u32` over the
`len` field and the bytes. CRC-32 is `Components::PersistedRecord::crc32` (IEEE, reflected, init/xorout `0xFFFFFFFF`;
`crc32("123456789") = 0xCBF43926`). Segments are append-only; a reader stops at the first record that fails.

### 5.4 Golden segment (57 bytes; used by the C++ codec test and the Python reader test)
Header, stream TLM, open time 1000 s 5 µs: `53 45 47 31 01 00 FF FF E8 03 00 00 05 00 00 00 E5 D8 63 6F` (CRC `0x6F63D8E5`).
Record, a 31-byte `FileSystem` packet (descriptor `0x0004`, packet id 5, time base 2 context 0, 1000 s 5 µs,
FreeSpace U64 = 4000000000, TotalSpace U64 = 4294967296):
`1F 00 00 04 00 05 00 02 00 00 00 03 E8 00 00 00 05 00 00 00 00 EE 6B 28 00 00 00 00 01 00 00 00 00 37 92 21 A6` (CRC `0xA6219237`).

### 5.5 Configuration record
`PersistedRecord` (`store`/`load`, `PersistedRecordFile.hpp:42,59`) at `/rec/config.bin`, temp `/rec/config.tmp`, magic
`44 52 43 31` ("DRC1"). Payload 26 bytes: `layout u8 = 1`, `streamCount u8 = 2`, then per stream in order TLM, EVT:
`ringSlots u8, flushRecords u8, flushIntervalS u16, capacityBytes u32, retentionS u32`. Defaults encode to
`01 02 | 20 10 3C 00 00 00 80 00 80 3A 09 00 | 20 08 0A 00 00 00 20 00 00 8D 27 00`. Load outcomes: `MISSING` → defaults, no
event; any other non-OK status, or `len ≠ 26`, `layout ≠ 1`, `streamCount ≠ 2`, a value outside §3's ranges (capacity checked
only against its lower bound) or `flushRecords > ringSlots` → defaults and exactly one `ConfigCorrupt`. The version of *this
payload* is `layout`; `PersistedRecord::FORMAT_VERSION` is the codec's and stays 1. A9-2 bumps `layout` to 2 (36 bytes).

## 6. Compile-time constants (`P/Components/DataRecorder/DataRecorderCfg.hpp`, namespace `Components::DataRecorderCfg`)

| Constant | TLM | EVT | Commandable |
|---|---|---|---|
| ring slots (static max `RING_SLOTS_MAX` = 32) | 32 | 32 | `SET_RING_SLOTS` |
| `FLUSH_RECORDS` | 16 | 8 | `SET_FLUSH` |
| `FLUSH_INTERVAL_S` (max 3600) | 60 | 10 | `SET_FLUSH` |
| `SEGMENT_MAX_BYTES` | 32768 | 16384 | no |
| `SEGMENT_MAX_S` | 3600 | 3600 | no |
| `CAPACITY_BYTES` | 8388608 | 2097152 | `SET_CAPACITY` |
| `RETENTION_S` (60..2592000) | 604800 | 2592000 | `SET_RETENTION_S` |

Shared **[decided]**: `RESERVE_BYTES` = 16777216, `STAGE_BYTES` = 4096, `SCAN_BUDGET` = 32, `LIST_MAX_EVENTS` = 16,
`LIST_MAX_PROBES` = 64. Ring slot payload capacity is `FW_COM_BUFFER_MAX_SIZE` (227 at this tree), never a literal.

## 7. Behaviour

**7.1 Inputs.** `tlmIn`/`evtIn` copy a buffer of 1..227 bytes into that stream's ring (full → drop the oldest, `RingDropped` +1)
and return; a 0-byte buffer is ignored. They make no `Os::` call, no port call and emit nothing.

**7.2 Tick.** Each `schedIn`: (a) on the first tick, load the config (unless a command already did) and start both scans;
(b) per stream TLM then EVT: if scanning, one scan step (§7.6); else flush step (§7.3), rotation (§7.4), retention step (§7.5);
(c) write the 14 channels. Time is `getTime().getSeconds()` taken on that tick.

**7.3 Flush.** Due iff `count > 0` and (`count ≥ flushRecords` or `ticksSinceFlush ≥ flushIntervalS`); `ticksSinceFlush` (0 at boot)
is incremented at the start of every flush step, before the trigger is evaluated, and reset to 0 after a successful write, so
a record pushed right after a flush is written on the `flushIntervalS`-th tick after it. A flush writes the oldest whole records, in order, that fit in
`STAGE_BYTES` (including a 20-byte header when the segment is new) and keep the segment ≤ `SEGMENT_MAX_BYTES`, with **one**
`Os::File::write` and **one** explicit `flush()`, opening the file with `OPEN_APPEND` and closing it before the tick returns.
If no segment is open, a new one is created with number `nextSeq` (skipping any number whose file exists) and
`SegmentOpened` is emitted after the successful write. If the oldest record does not fit the open segment, that segment is
closed and a new one is started in the same write. Success: the written records leave the ring; `RecordsOnDisk`,
`BytesOnDisk`, `SegmentsOnDisk` update.

**7.4 Rotation.** After the flush step, a segment open for ≥ `SEGMENT_MAX_S` ticks is closed. No file exists for a segment
until its first successful write; a quiet boot creates no file. A segment is never reopened after a reboot or a close.

**7.5 Retention.** At most one deletion per stream per tick, never of the open segment. First, if `BytesOnDisk > capacity`:
delete the oldest (`CAPACITY`). Otherwise the oldest segment S is expired (`AGE`) iff: S's header is invalid; or the next
present segment T has a valid header and `now − T.openSeconds > retentionS`; or T's header is invalid and
`now − S.openSeconds > retentionS + SEGMENT_MAX_S`. The newest segment on disk is never deleted for age. Negative
differences count as 0. A deletion emits `SegmentDeleted`; a `removeFile` failure counts in `WriteFailures`, emits
`SegmentWriteFailed`, and is retried next tick.

**7.6 Boot scan.** `createDirectory("/rec")`, `createDirectory("/rec/<s>")`, `Os::Directory::open(READ)`, then at most
`SCAN_BUDGET` entries per stream per tick (the directory stays open between ticks; `rewind` is never used). Each segment
name adds 1 to `SegmentsOnDisk` and its `getFileSize` to `BytesOnDisk`. At `NO_MORE_FILES`: close, read the headers of the
oldest and second-oldest, `nextSeq = max + 1` (1 when empty). Entry order does not matter. Any other failure:
`DirectoryScanFailed(stream, status)`, counts reset, the scan restarts next tick. While scanning, the ring fills (and drops
oldest) and nothing is written or deleted for that stream.

**7.7 Write failure.** Open, write, short write or flush failure: the records stay in the ring, `WriteFailures` +1,
`SegmentWriteFailed` (throttled 5), the segment is closed. The next attempt starts a new segment; `nextSeq` advances past the
failed name only if a file of that name exists afterwards (counted in `SegmentsOnDisk`/`BytesOnDisk`). No existing file is
ever truncated. Recovered records may be written twice (duplicates, never loss). Producers are unaffected.

**7.8 Bounds and locking.** Per stream per `schedIn`: ≤ 1 `write`, ≤ 1 `flush`, ≤ 1 `removeFile`, ≤ 3 file opens, ≤ `SCAN_BUDGET`
directory reads + `exists`/`getFileSize` calls. No `Os::File` is open when any handler returns. No event, telemetry, command
response or `sendFileOut` call, and no `Os::` call, happens while the component lock (the guarded-port mutex) is held.

## 8. F´-free API (tests call these names)
`SegmentCodec.hpp`, namespace `Components::SegmentCodec`: `HEADER_SIZE = 20`, `RECORD_OVERHEAD = 6`, `MAX_PAYLOAD =
FW_COM_BUFFER_MAX_SIZE`, `VERSION = 1`; `enum class Status : uint8_t { OK, END, TRUNCATED, BAD_LENGTH, BAD_CRC, BAD_MAGIC,
BAD_VERSION }`; `struct Header { uint8_t stream; uint16_t bootCount; uint32_t openSeconds; uint32_t openUseconds; }`;
`uint32_t encodeHeader(const Header&, uint8_t* out, uint32_t outCap)` (20, or 0 if `outCap < 20`); `Status decodeHeader(const
uint8_t* buf, uint32_t len, Header& out)` (checks in order: `len < 20` TRUNCATED, magic, CRC, version); `uint32_t
encodeRecord(const uint8_t* payload, uint16_t len, uint8_t* out, uint32_t outCap)` (`len + 6`, or 0 for `len` 0 or > 227 or no
room); `Status decodeRecord(const uint8_t* buf, uint32_t len, const uint8_t*& payload, uint16_t& payloadLen, uint32_t& consumed)`
(in order: `len == 0` END; `len < 2` TRUNCATED; declared 0 or > 227 BAD_LENGTH; `len < declared + 6` TRUNCATED; CRC BAD_CRC).
`PacketRing.hpp`: `template <uint16_t SLOTS, uint16_t SLOT_BYTES> class PacketRing` with `bool push(const uint8_t*, uint16_t)`
(false and nothing counted for null, 0 or > `SLOT_BYTES`; drops the oldest when full), `count()`, `capacity()`,
`bool setCapacity(uint16_t)` (1..`SLOTS`; drops surplus oldest, counted), `bool peek(uint16_t i, const uint8_t*&, uint16_t&)`
(`i = 0` oldest), `void pop(uint16_t n)`, `uint32_t pushed()`, `uint32_t dropped()`; and free function
`bool flushDue(uint16_t count, uint16_t flushRecords, uint32_t ticksSinceFlush, uint16_t flushIntervalS)` implementing §7.3's trigger.

## 9. Host fakes and stub (observable behaviour the test author builds on)
- **`UT/support/Os/Directory.hpp` (new):** `Os::Directory` with the `Status` and `OpenMode` enums of `lib/fprime/Os/Directory.hpp:21-40`.
  `open(path, READ)` → `DOESNT_EXIST` unless `path` is in the fake's `directories` set; `CREATE_IF_MISSING` adds it.
  `read(buf, size)` yields, one per call, the base name of each file in `files` and each directory in `directories` whose path
  is `<path>/<name>` with no further `/`, in lexicographic order (reversed when `reverseDirectoryOrder`), then `NO_MORE_FILES`;
  `NOT_OPENED` when closed. `rewind()` → `NOT_SUPPORTED` (as on Zephyr). Injections: `failDirectoryOpen`, `dirReadFailAt` (0-based entry index, −1 off).
- **`UT/support/Os/File.hpp` / `FileSystem.hpp` (additions only; existing modes, flags and results unchanged):** `OPEN_APPEND` (creates
  if missing, keeps content, writes append; `failOpenCreate` also fails it); `openHandles` (files open now); `opCounts[key]` and an
  `onOperation(key)` hook for keys `open read write flush close exists rename removeFile getFileSize getFreeSpace createDirectory
  dirOpen dirRead`; `Os::FileSystem::removeFile` (`DOESNT_EXIST`, `failRemove`), `getFileSize`, `getFreeSpace` (`totalBytes`
  default 4294967296, free = total − sum of file sizes, `failGetFreeSpace`), `createDirectory(path, errorIfAlreadyExists = false)`
  (parent must be `/` or in `directories`, else `DOESNT_EXIST`; `failCreateDirectory`).
- **`UT/support/Fw/Com/ComBuffer.hpp` (new):** `Fw::ComBuffer` with capacity 227, `setBuff`, `getBuffAddr`, `getSize`, `getCapacity`.
- **Recorder stub:** records every `tlmWrite_*` value, every event with its arguments, every command response, every
  `sendFileOut` call (src, dst, offset, length) and returns a settable `SendFileResponse`; a settable time for `getTime()`;
  `tlmIn_handlerBase`/`evtIn_handlerBase` do lock → handler → unLock; `lockDepth` and a flag `portCallWhileLocked`.

## 10. Ground reader — `tools/recorder_reader.py` (standard library only)
`$PY tools/recorder_reader.py --dictionary DICT.json [--output OUT.csv] SEGMENT [SEGMENT ...]`. CSV to `--output` or stdout, header
`segment,record,stream,kind,id,name,time_base,time_context,seconds,useconds,value`; `record` is 0-based per segment. Telemetry
(descriptor 4): packet id (`FwTlmPacketizeIdType`), 11-byte time, then each member of `telemetryPacketSets[0].members[id]` in
listed order, each occupying its maximum serialized size (a string: 2-byte length + its `size`); one row per member, `kind`
= `tlm`. Event (descriptor 2): id (`FwEventIdType`), time, arguments in `formalParams` order; one row, `kind` = `evt`, `value` =
`name=value` pairs joined by `;`. Other descriptors or unknown ids: one row `kind` = `unknown`, `value` = hex. Widths come from
the dictionary's alias `typeDefinitions`; values: integers decimal, floats `repr`, bools `true`/`false`, enums by name,
arrays/structs as compact JSON. Stderr, one line per segment: `<path>: <n> records, stop=<END|TRUNCATED|BAD_LENGTH|BAD_CRC>`.
Exit 0 all segments end clean; 1 any stopped early (rows before the stop are still written); 2 unreadable file/dictionary or
invalid header (one `error: ...` line on stderr).

## 11. Per-pass sequence — `sequences/pass_recorder.seq`
```
R00:00:00 ReferenceDeployment.dataRecorder.DOWNLINK_NEWEST, EVT
R00:00:02 ReferenceDeployment.dataRecorder.DOWNLINK_NEWEST, TLM
```
plus a leading comment naming DH-L2-13 / CDH-27. Events first (smaller, higher value).

## 12. Harm table (existing behaviour → the observation that proves it unchanged)
| Existing behaviour | Observation |
|---|---|
| Live downlink of telemetry and events | generated TopologyAc: splitter indices 0/1 unchanged (§1.2); `ComCcsds_CdhCore` block not in `git diff` |
| 1 Hz members 0..20 | `topology.fpp` diff adds lines only; `check_capacity`: `rateGroup1Hz: 19/25 used, 6 free (max index 21) — OK` |
| Other packets | every packet except `FileSystem` byte-identical in the `.fppi` diff; `FileSystem` keeps `fsSpace.FreeSpace`, `TotalSpace` first; its size 87 B ≤ 227 |
| Producers (`tlmSend`, `events`) | §7.1: 100 input calls change no fake counter and no file; with every write failing they still return |
| Persisted records, PrmDb, uplinked files | the fake holds no created/changed path outside `/rec/` after any test; the real `/prmDb.dat` path is untouched |
| FatFs file slots (`FS_FATFS_NUM_FILES` default 4) | `openHandles == 0` after every handler return |
| Existing host tests and shared fakes | the 30 existing binaries pass unmodified, same names; the fakes' diff adds lines only |
| Framework | `git diff --stat -- lib/` empty on every row |
| Rollback | reverting A8-3 alone restores today's dictionary counts, packet set and `check_capacity` output (README §Rows) |
| Passive, no heap | `.fpp` has `passive component` and no `async`; no `new`, `malloc` or `std::` container in `P/Components/DataRecorder/*` (DataRecorder-7) |

## 13. Verification expectations
- Host: binaries 30 → 32; script tests + those in `test_recorder_reader.py`; gate PASS each row.
- A8-2 target: builds; dictionary identical (387 / 106 / 244 / 697 / 23 packets).
- A8-3 target: commands 396 (+9), parameters 106, channels 258 (+14; 188 in packets + 70 omitted, of 288), events 705 (+8),
  packets 23; `check_capacity.py --dictionary none` shows `MAX_PACKETIZER_CHANNELS: 258/288 used, 30 free (188 in packets +
  70 omitted) — OK`. RAM ≤ 372736 B (70.0 % of 532480), expected ≈ 356 KB (+22..28 KB from 331376 B); FLASH ≤ 783108 B
  (75.0 % of 1044144), expected +12..18 KB from 726656 B. Measured values go in the review.

## 14. Non-goals
Inline mode / `DOWNLINK_GROUP_MASK` (A8-7); recording during TelemetryGate DISABLED (open decision 1: the `tlm` stream records
only what `tlmSend` emits); the `burst` stream (A9-2); a boot-count source for the header; changing DH-L2-01/07/09/10/11/13/14,
CDH-8/14/27 or FD-L2-04 texts; a TaskGate entry for the recorder (`NUM_TASKS` 5/5); `RESERVE_BYTES` tuning from a board.
