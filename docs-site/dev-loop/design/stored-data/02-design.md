# 02: Design: the `DataRecorder` component

This is the component `cdh.md` rows DH-L2-03/04/05/08 call "TelemetryStore". It is named `DataRecorder` because it records
events as well as telemetry; the reason texts in `cdh.md` are updated with `req.py` in Phase 0.

## 1. Tiers and mechanisms

| Tier | Examples | Mechanism | Status |
|---|---|---|---|
| Critical state | current mode, boot count, auth sequence number, tx enable | `PersistedRecord` (exists) | done |
| Telemetry history | every packet `tlmSend` emits | `DataRecorder`, stream `tlm` | this design |
| Event / fault log | every event `events` emits, FaultManager included | `DataRecorder`, stream `evt` | this design |
| Burst records | 44 B coil-current / field / body-rate rows around a torque pulse, batched by `BurstCapture` (A9) | `DataRecorder`, stream `burst` (stream 2), producer A9 | designed here, delivered by A9-2 |
| Bulk files | camera images, uplinked files, recorder segments | `Svc.FileDownlink` (exists) | done, reused |

Rule: PersistedRecord stays for records ≤ 64 B that must survive as *one* value; the recorder is for append-only streams.
The two never share a file.

The `burst` stream (added 2026-09-27 from `cycle-sequencing-E-A8-A9.md`): A9 batches five 44-byte burst records into one
220-byte ring slot and pushes it through the same ring API (`burstIn: Fw.Com`, sync input, wired from `burstCapture.burstOut`);
it never writes files itself. A8 builds the stream enum and the config record with room for it (§5); A9-2 adds the enumerator,
the port and the third ring. Directory `/rec/burst/`, ring 32 slots, flush 8 records / 5 s (§3).

## 2. Component shape

`P/Components/DataRecorder/`: a **passive** component (no thread, no queue; RAM stays predictable), split like PersistedRecord
so the logic is host-testable without F´:

- `SegmentCodec.hpp/.cpp`: F´-free: frames one packet into a record, verifies one record, encodes/decodes the segment header.
  Reuses `Components::PersistedRecord::crc32` (table-free CRC-32, `PersistedRecordCodec.hpp`).
- `PacketRing.hpp`: F´-free fixed-capacity ring of `(len, bytes[FW_COM_BUFFER_MAX_SIZE])` slots, drop-oldest.
- `DataRecorder.fpp/.hpp/.cpp`: the component; all `Os::` calls live here, none in the sync input handlers.

Ports:

| Port | Kind | Wired from / to |
|---|---|---|
| `tlmIn: Fw.Com` | sync input | `comSplitterTelemetry.comOut[2]` (spare) |
| `evtIn: Fw.Com` | sync input | `comSplitterEvents.comOut[2]` (spare) |
| `schedIn: Svc.Sched` | sync input | `rateGroup1Hz.RateGroupMemberOut[21]` (slot 20 is `faultManager.run` since Cycle D; 21 is the first free index above it, so the recorder runs after the fault decision) |
| `burstIn: Fw.Com` | sync input | `burstCapture.burstOut` (A9-4; absent until then) |
| `getFreeSpace` | none | calls `Os::FileSystem::getFreeSpace` directly, as FsSpace does |
| cmd / tlm / log / time | standard | |

Both `Fw.Com` handlers do one thing: copy the buffer into that stream's ring and return. No SD I/O, no allocation, no
blocking, so the producers (`tlmSend`, `events`) see the same cost as an extra splitter output. Phase 1 wires the recorder as a
**tap**; the live downlink path is untouched (harm table in `04-plan.md`).

## 3. RAM ring → SD segment: the flush policy

On every 1 Hz `schedIn`, per stream:

1. If the ring holds ≥ `FLUSH_RECORDS` records **or** `FLUSH_INTERVAL_S` has elapsed since the last flush and the ring is not
   empty: drain the ring into the open segment with one `Os::File::write(WAIT)` of the framed bytes, then an explicit
   `flush()` (the WAIT write discards its flush status; ledger line 15). Failure → count `WriteFailures`, emit
   `SegmentWriteFailed` (throttled), keep the records in the ring and retry next tick. The ring never blocks on the SD.
2. If the open segment has reached `SEGMENT_MAX_BYTES` or `SEGMENT_MAX_S`: close it and open the next one.
3. Retention: while `bytesOnDisk > CAPACITY_BYTES` or the oldest segment's close time is older than `RETENTION_S`: delete the
   oldest segment (`Os::FileSystem::removeFile`), one per tick so a tick stays bounded.
4. Update channels (§6).

Defaults (compile-time constants in `DataRecorderCfg.hpp`, commandable within the static maxima in §5):

| Constant | tlm | evt | burst (A9-2) | Rationale |
|---|---|---|---|---|
| `RING_SLOTS` (max) | 32 | 32 | 32 | slot = `len u16` + 227 B (`FW_COM_BUFFER_MAX_SIZE`) ≈ 229 B; 64 slots ≈ 14.7 KB for A8, 96 ≈ 22 KB with A9, against the ~197 KB RAM headroom at 62.2 % |
| `FLUSH_RECORDS` | 16 | 8 | 8 | half a ring: a write in flight never starves the producer |
| `FLUSH_INTERVAL_S` | 60 | 10 | 5 | events are rare and more valuable per record; a burst window is 60 s and must land on disk before the next pulse |
| `SEGMENT_MAX_BYTES` | 32 KB | 16 KB | 32 KB | a segment is one `fileDownlink.SendFile`; ~1 s per 1 KB chunk on the current 1 s cycle; one 60 s burst at 10 Hz is ≈ 26 KB |
| `SEGMENT_MAX_S` | 3600 | 3600 | 3600 | bounds the loss of a torn tail to one hour |
| `CAPACITY_BYTES` | 8 MB | 2 MB | 8 MB | validated against `TotalSpace` at boot and on every set; the 4 GB SD NAND makes media a non-constraint, so retention is set by these values, not by the medium |
| `RETENTION_S` | 7 d | 30 d | 30 d | DH-L2-08 |

Overflow inside the ring (SD too slow or failed): **drop-oldest**, counted in `RingDropped`. This is deliberately the opposite
of comQueue's drop-newest (DH-L2-10): for a history the most recent sample is the most valuable, and the live link still has
the newest value. Both policies are documented as the "defined discard policy".

## 4. On-disk format

Directory per stream: `/rec/tlm/`, `/rec/evt/`, `/rec/burst/` (A9). Segment name is a zero-padded monotonic sequence number, `00000042.bin`; the
next number is `max + 1` from an `Os::Directory` scan at boot (`ZephyrDirectory::open(path, READ)` then `read()` until `NO_MORE_FILES`, fprime-zephyr b14101dd), so no counter needs persisting.

```
segment header (16 B)      magic "SEG1" | version u8 | stream u8 | boot count u16 | open time (secs u32, usecs u32) | crc32
record (repeated)          len u16 | Fw::Com bytes[len] | crc32 over len + bytes
```

- The `Fw::Com` bytes are exactly what the GDS already decodes on the live link (packet descriptor + id + time tag + values for
  telemetry, id + time tag + args for events), so the ground reader is the existing dictionary plus a 6-byte frame.
- Every record carries its own CRC. A reader stops at the first record that fails, so a power cut during a flush loses at most
  the last flush and never yields a wrong record (the same guarantee PersistedRecord-5 makes, at record granularity).
- Segments are append-only and never rewritten, so FAT's non-atomic rename (ledger line 15) is not on the path. The only
  rename-based file is the configuration record (§5), which uses PersistedRecord.
- Segment closes do not fsync the directory; a segment that exists but has no valid header is skipped by the boot scan and
  deleted on the next retention pass.

## 5. Configuration: validate, then commit, then persist

Commands (one per stream, `stream` argument `TLM | EVT`, plus `BURST` from A9-2):

| Command | Validation | Effect on success |
|---|---|---|
| `SET_RING_SLOTS(stream, n)` | `1 ≤ n ≤ RING_SLOTS` static max | ring logical capacity = n; surplus oldest records dropped |
| `SET_CAPACITY(stream, bytes)` | `SEGMENT_MAX_BYTES ≤ bytes ≤ TotalSpace − RESERVE_BYTES − other stream's capacity` | retention pass applies it on the next tick |
| `SET_RETENTION_S(stream, s)` | `60 ≤ s ≤ 30 d` | retention pass applies it on the next tick |
| `SET_FLUSH(stream, records, interval_s)` | `1 ≤ records ≤ ring slots`, `1 ≤ interval_s ≤ 3600` | next tick |
| `SAVE_CONFIG` | none | writes the config record |
| `LIST_SEGMENTS(stream)` | none | one `SegmentInfo` event per segment: name, bytes, open time |
| `DELETE_SEGMENT(stream, seq)` | segment exists and is not the open one | `removeFile` |
| `CLOSE_SEGMENT(stream)` | none | rotates now so the current data is downlinkable |

A failed validation returns `VALIDATION_ERROR` and changes nothing (DH-L2-04/05). The committed values are one
`PersistedRecord` (`/rec/config.bin`, magic `DRC1`, ≤ 64 B: per stream {ring u8, flush records u8, flush interval u16,
capacity u32, retention u32} = 12 B; 24 B for A8's two streams, 36 B once A9-2 adds `burst`, with the record version bumped
so a two-stream record loads as `BAD_VERSION` → defaults + `ConfigCorrupt`). PrmDb is not used because `/prmDb.dat` has no CRC
and the default 25-entry store already cannot hold the 106 dictionary parameters (01 §"What is kept"); this also keeps A8 off
the persistence gate (`cycle-sequencing-E-A8-A9.md` §Ownership).

## 6. Telemetry and events

Channels per stream, all `U32` unless noted, placed in the existing `FileSystem` packet (id 5, group 5) so
`MAX_PACKETIZER_PACKETS` need not change (23 of 24 used; id 24 is reserved for A9's `BurstStatus`). They do count against
`MAX_PACKETIZER_CHANNELS` (244 of 256 used), so A8's constants row raises that to at least 288 first: `RecordsStored`, `RecordsOnDisk`, `BytesOnDisk`, `SegmentsOnDisk`,
`OldestRecordAgeS`, `RingDropped`, `WriteFailures`. Every new channel is added to `ReferenceDeploymentPackets.fppi` in the same
change (CLAUDE.md trap).

Events: `SegmentOpened(stream, seq)` activity low; `SegmentWriteFailed(stream, status)` warning high, throttled 5;
`SegmentDeleted(stream, seq, reason: CAPACITY|AGE|COMMAND)` activity low; `ConfigRejected(field, value, max)` warning low;
`ConfigCorrupt` warning high (defaults applied, same pattern as `TelemetryGate.StateFileCorrupt`); `SegmentInfo(...)`.

## 7. Retrieval

1. **Bulk, selective (DH-L2-07):** `LIST_SEGMENTS`, then `FileHandling.fileDownlink.SendFile("/rec/tlm/00000042.bin", ...)`.
   Nothing new on board; FILE priority (1) already beats live telemetry (2) and yields to events (0).
2. **Scheduled (DH-L2-13, CDH-27):** a sequence in `sequences/` run by `cmdSeq` per pass: `CLOSE_SEGMENT`, `LIST_SEGMENTS`,
   `SendFile` of the newest segment. Ops can also pin one packet group into its own stream later (Phase 4).
3. **Ground:** `tools/recorder_reader.py` walks a downlinked segment, checks CRCs, and feeds each `Fw::Com` payload to the GDS
   decoders from the deployment dictionary, emitting CSV. This is part of the deliverable, not optional: without it the segments
   are opaque.
4. **Replay over the link** (re-injecting stored packets into the TELEMETRY queue) is not designed in: the queue is depth 1 and
   would drop, and file downlink already paces itself.

## 8. Behaviour on faults and resets

- Boot: scan both directories, pick next sequence numbers, sum bytes, apply retention once, open no segment until the first
  record arrives (no empty files, no flash wear on a quiet boot). Load `/rec/config.bin`; MISSING → defaults silently,
  anything else → defaults + `ConfigCorrupt`.
- SD absent or unmountable: every write fails; the ring keeps the newest `RING_SLOTS` records; `WriteFailures` climbs; the
  producers are unaffected. This is fail-operational by construction.
- Safe mode: the recorder keeps recording (events during safe mode are exactly what ops want back). `taskGate` can disable
  its 1 Hz tick if power demands it; the ring then just fills and drops oldest.
- TelemetryGate DISABLED: `tlmSend` does not run, so the `tlm` stream records nothing; the `evt` stream is unaffected. Recording
  during RF silence would require moving the gate from the run tick to the recorder's forward path, which changes the CH-L2-17
  criterion ("inhibit telemetry *scheduling*"). Left as the open decision in `04-plan.md`.

## 9. Alternatives considered

| Option | Why not |
|---|---|
| F´ Data Products (`Svc.DpManager` + `DpWriter` + `DpCatalog`) | Three active components (stacks + queues on 520 KB RAM), a container/record schema the project has never configured (`records`=0 in the dictionary, no `Dp*` files under `P/project/config/`), DpCatalog holds its catalog in RAM and scans directories; not host-testable in this repo. TlmPacketizer packets already are a record format |
| `Svc.ComLogger` on the event splitter | Active component; writes one packet per call with no RAM buffering, contradicting the CDR mitigation "high rate logs in RAM buffers with controlled flush intervals"; no CRC per record, no retention. Would be the fastest way to get *some* event log, but would then be replaced |
| `Svc.BufferLogger` | Same as ComLogger, plus a buffer-management dependency the passive design does not need |
| Bigger comQueue depths | RAM-only, lost on reset, still drop-newest, still one queue per link; does not address retention or retrieval |
| Growing PersistedRecord to hold streams | Its whole-record temp+rename would rewrite the stream on every update and wear the card |
