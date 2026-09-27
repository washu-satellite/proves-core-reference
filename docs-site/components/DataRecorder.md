# Components::DataRecorder

Passive component that records the telemetry and event packet streams into a static RAM ring per stream and flushes them to
CRC-framed segment files under `/rec/` on the SD NAND, with retention by age and capacity and retrieval through file downlink.
Design: `docs-site/dev-loop/design/stored-data/`. Interface and formats: `docs-site/dev-loop/cycles/cycle-m-plan/01-normative.md`.

## Overview

The recorder keeps two streams, `TLM` (telemetry packets) and `EVT` (event packets). Each packet that arrives on `tlmIn`
or `evtIn` is copied into that stream's RAM ring and nothing else happens on the producer's thread: no file I/O, no event,
no telemetry. All file work runs on the 1 Hz `schedIn` tick, per stream: the boot scan until it completes, then one flush
step, the rotation check and one retention step. The 14 channels are written at the end of every tick.

- **Boot scan.** Creates `/rec`, `/rec/tlm`, `/rec/evt` and reads the stream directory at most 32 entries per tick,
  counting `NNNNNNNN.bin` files and continuing their numbering at max + 1. Until it completes the stream answers `BUSY` to
  `LIST_SEGMENTS`, `DELETE_SEGMENT`, `CLOSE_SEGMENT` and `DOWNLINK_NEWEST`, and writes and deletes nothing.
- **Flush.** When the ring holds the flush count, or the flush interval has passed since the last write, the oldest whole
  records that fit in 4096 bytes are written to the open segment with one write and one explicit flush; the file is
  closed again before the tick returns. A new segment file exists only after its first successful write.
- **Rotation.** A segment open for 3600 ticks is closed; so is one whose next record would exceed its size maximum.
  A segment is never reopened, including after a reboot.
- **Retention.** At most one deletion per stream per tick, always of the oldest segment and never of the open one:
  first by capacity (bytes on disk above the stream's capacity), otherwise by age (the next segment was opened more than
  the retention ago, or the oldest header is invalid). The newest segment is never deleted for age.

Locking: the component lock (the guarded-port mutex) protects the rings only and is held for copies; no `Os` call, port
call, event, telemetry write or command response happens while it is held. A second mutex serialises file work and
configuration between `schedIn` and the commands.

## Ports

| Port | Kind | Use |
|---|---|---|
| `tlmIn` | guarded input `Fw.Com` | telemetry packets into the TLM ring |
| `evtIn` | guarded input `Fw.Com` | event packets into the EVT ring |
| `schedIn` | sync input `Svc.Sched` | 1 Hz tick; all file work |
| `sendFileOut` | output `Svc.SendFileRequest` | `DOWNLINK_NEWEST` only |
| standard | time, command, event, text event, telemetry | no parameter ports: the configuration is a PersistedRecord |

Unwired until the topology row A8-3 (splitter taps `comOut[2]`, rate group 1 Hz slot 21, `fileDownlink.SendFile`).

## Commands

All `sync`; every command takes `stream: RecorderStream` (`TLM`, `EVT`) except `SAVE_CONFIG`. Ranges and rejection
arguments are normative in `docs-site/dev-loop/cycles/cycle-m-plan/01-normative.md` §3.

| Command | Effect |
|---|---|
| `SET_RING_SLOTS(stream, slots)` | usable ring slots, 1..32 and not below the flush count; shrinks at once (surplus oldest dropped) |
| `SET_CAPACITY(stream, bytes)` | disk capacity, from the segment maximum up to free-space total − 16 MiB − the other stream's capacity |
| `SET_RETENTION_S(stream, seconds)` | retention, 60..2592000 s |
| `SET_FLUSH(stream, records, intervalS)` | flush count 1..ring slots, interval 1..3600 s |
| `SAVE_CONFIG()` | writes `/rec/config.bin`; until then SET values are RAM-only |
| `LIST_SEGMENTS(stream, fromSeq)` | one `SegmentInfo` per present segment from `fromSeq`, ascending, at most 16 |
| `DELETE_SEGMENT(stream, seq)` | deletes one closed segment |
| `CLOSE_SEGMENT(stream)` | writes one batch of the ring and closes the open segment |
| `DOWNLINK_NEWEST(stream)` | `CLOSE_SEGMENT`, then `sendFileOut` of the highest-numbered segment and one `SegmentInfo` |

A rejected SET answers `VALIDATION_ERROR`, emits one `ConfigRejected(field, value, max)` and changes nothing; an accepted
one answers `OK` and emits one `ConfigApplied` per field. The first configuration command before the first tick loads
`/rec/config.bin` first.

## Telemetry

Seven `U32` channels per stream, prefix `Tlm` or `Evt`: `RecordsStored` (accepted into the ring since boot),
`RecordsOnDisk` (written to segments since boot), `BytesOnDisk` and `SegmentsOnDisk` (the stream's segment files),
`OldestRecordAgeS` (seconds since the oldest segment was opened; 0 when none or its header is invalid), `RingDropped` and
`WriteFailures` (cumulative). All 14 are written on every tick, including while scanning.

## Events

| Event | Severity | When |
|---|---|---|
| `SegmentOpened(stream, seq)` | activity low | a new segment's first write succeeded |
| `SegmentWriteFailed(stream, status)` | warning high, throttle 5 | open, write, short write (`BAD_SIZE`), flush or retention delete failed |
| `SegmentDeleted(stream, seq, reason)` | activity low | deleted for `CAPACITY`, `AGE` or by `COMMAND` |
| `ConfigRejected(field, value, max)` | warning low | a SET out of range |
| `ConfigCorrupt(status)` | warning high, throttle 5 | `/rec/config.bin` invalid (PersistedRecord status; 255 = bad payload) |
| `SegmentInfo(stream, seq, bytes, openTimeS)` | activity high | `LIST_SEGMENTS`, `DOWNLINK_NEWEST` |
| `DirectoryScanFailed(stream, status)` | warning high, throttle 5 | the boot scan failed; it restarts next tick |
| `ConfigApplied(stream, field, value)` | activity high | a SET field accepted |

## File formats

Segments `/rec/<tlm|evt>/NNNNNNNN.bin`: a 20-byte header (magic `SEG1`, version, stream, boot count, open time, CRC-32)
followed by records (`len u16`, the `Fw::ComBuffer` bytes as received, CRC-32), little-endian framing. The byte layout and
the golden vector are in `docs-site/dev-loop/cycles/cycle-m-plan/01-normative.md` §5; the codec is `SegmentCodec.hpp`, the
ground decoder `tools/recorder_reader.py`. Only names matching `^[0-9]{8}\.bin$` are segments; other entries are ignored
and never deleted, and nothing outside `/rec/` is created or changed.

## Configuration

Compile-time sizes and defaults are in `DataRecorderCfg.hpp` (01-normative §6). The commandable values (ring slots, flush
count and interval, capacity, retention per stream) persist only through `SAVE_CONFIG`, as a `PersistedRecord`
(`Components/PersistedRecord`) at `/rec/config.bin`, magic `DRC1`, 26-byte payload with its own `layout` byte (01-normative
§5.5). The first tick loads it: a missing file gives the defaults silently; an invalid record or payload gives the defaults
and one `ConfigCorrupt`.

## Failure behaviour

- A failed open, write, short write or flush keeps the records in the ring (the full ring still drops its oldest), counts
  `WriteFailures`, emits `SegmentWriteFailed` and closes the segment; the next attempt starts a new segment number. No file
  is ever truncated; records of a failed attempt may be on disk twice after recovery (duplicates, never loss).
- A failed retention delete counts in `WriteFailures`, emits `SegmentWriteFailed` and is retried next tick.
- A directory failure during the boot scan emits `DirectoryScanFailed`, resets the counts and restarts the scan next tick.
- Producers are never affected: `tlmIn` and `evtIn` only copy into the ring.

## Requirements

Pass criteria are decided before testing; edit with `scripts/req.py`, not by hand.

| Name | Description | Method | Level | Pass Criteria | Status | Reason |
|---|---|---|---|---|---|---|
|DataRecorder-1|Each record shall be framed as len u16, bytes, crc32 and each segment shall start with a versioned header|Unit Test|Unit|encodeHeader writes the 20-byte header of cycle-m-plan 01-normative 5.2 and encodeRecord writes len u16 LE, the bytes and CRC-32 LE over len and bytes; the 57-byte golden segment of 01-normative 5.4 is reproduced byte for byte; a 227-byte payload encodes to 233 bytes; a 0-byte or 228-byte payload encodes to 0 bytes|||
|DataRecorder-2|Decoding shall detect any single-byte corruption or truncation of a record and stop without yielding a wrong record|Unit Test|Unit|For the golden segment and a 3-record segment, every single-byte flip and every truncation length makes decodeRecord stop at or before the damaged record with TRUNCATED, BAD_LENGTH or BAD_CRC, and every record returned before the stop is byte-identical to the one encoded; every single-byte flip of the header makes decodeHeader return a status other than OK|||
|DataRecorder-3|The Fw.Com input handlers shall perform no file I/O and no allocation|Inspection, Unit Test|Unit|100 calls each of tlmIn and evtIn leave every Os fake operation counter and every file unchanged and make no event, telemetry, command-response or sendFileOut call; the handler bodies contain no Os call, new, malloc or std container (inspection)|||
|DataRecorder-4|Flush shall occur when the ring reaches the flush count or the flush interval elapses, whichever first|Unit Test|Unit|flushDue is false for count 0 and true exactly when count >= flushRecords or ticksSinceFlush >= flushIntervalS; with TLM defaults 15 records cause no write on the next tick and 16 cause exactly one write and one flush holding all 16 in push order; one EVT record is written on the 10th tick after the previous flush and not before|||
|DataRecorder-5|The oldest segment shall be deleted when total bytes exceed capacity or its age exceeds retention|Unit Test|Unit|With BytesOnDisk above capacity each tick deletes exactly one oldest segment with SegmentDeleted reason CAPACITY until BytesOnDisk <= capacity; the oldest segment is deleted with reason AGE on the first tick at which the next segment's open time is more than retentionS old and not earlier; a segment with an invalid header is deleted when it is the oldest; the open segment and the newest segment are never deleted by retention|||
|DataRecorder-6|A full ring shall drop the oldest record and count it|Unit Test|Unit|A ring of capacity n holding n records drops its oldest on the next push: count stays n, peek(0) returns the former second-oldest and dropped rises by 1; RingDropped rises by 1 per such push; SET_RING_SLOTS below the current count drops the surplus oldest records and adds them to RingDropped|||
|DataRecorder-7|All buffers shall be statically sized by compile-time maxima|Inspection|Unit|Every buffer in Components/DataRecorder is a fixed-size member array sized by a DataRecorderCfg.hpp constant or FW_COM_BUFFER_MAX_SIZE; the sources contain no new, malloc or std container|||
|DataRecorder-8|A configuration set outside the validated range shall be rejected with VALIDATION_ERROR and leave the previous value|Unit Test|Unit|Each SET command with an argument outside its range in cycle-m-plan 01-normative section 3, including SET_CAPACITY when getFreeSpace fails, returns VALIDATION_ERROR, emits exactly one ConfigRejected with that field, value and max, and leaves the ring contents and every configuration value unchanged as read back from the payload SAVE_CONFIG then writes|||
|DataRecorder-9|A write failure shall not lose ring contents and shall not affect the producing component|Unit Test|Unit|With open, write, short write or flush failing, the ring keeps its records except drop-oldest on overflow, WriteFailures rises by 1 per failed attempt, SegmentWriteFailed is emitted and throttled after 5, and tlmIn and evtIn keep returning normally; after the fault clears the kept records are written in order to a segment with a new sequence number and no existing file is truncated|||
|DataRecorder-10|The recorder configuration shall persist as a CRC-protected record and fall back to defaults when it is missing or invalid|Unit Test|Unit|SAVE_CONFIG writes /rec/config.bin as a PersistedRecord with magic DRC1 whose 26-byte payload matches cycle-m-plan 01-normative 5.5 (defaults encode to the listed bytes); a new instance applies it on its first tick; a missing file gives the defaults and no event; any single-byte corruption, truncation, wrong layout, wrong stream count or out-of-range value gives the defaults and exactly one ConfigCorrupt|||
|DataRecorder-11|The boot scan shall account for existing segments and continue their numbering without overwriting any file|Unit Test|Unit|After the scan SegmentsOnDisk and BytesOnDisk equal the number and byte sum of the NNNNNNNN.bin files in the stream directory, other names are ignored and never deleted, at most 32 entries are read per stream per tick in either directory order, and the first segment created is numbered max+1 (1 when empty) and skips a number whose file already exists; a directory failure emits DirectoryScanFailed and the scan restarts next tick|||
|DataRecorder-12|tools/recorder_reader.py shall decode downlinked segments into CSV using the deployment dictionary|Unit Test|Unit|Against a fixture dictionary the reader turns the golden segment into two tlm rows (fsSpace.FreeSpace 4000000000 and fsSpace.TotalSpace 4294967296 at 1000 s 5 us) with exit 0; a multi-record segment with one event row decodes each argument by name; a segment with a corrupted record keeps the rows before it and exits 1; an invalid header or an unreadable dictionary exits 2 with one error line|||
