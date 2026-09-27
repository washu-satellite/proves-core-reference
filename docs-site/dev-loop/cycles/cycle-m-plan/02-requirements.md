# 02 — Requirement rows (run by the test author at Stage 3b, before any code)

`req.py add` cannot create a group: it appends only to an existing table with at least one row (`scripts/req.py:271-278`;
`parse_section_table` returns no table for a header-only section, `:125-126`). A component's group is its directory name,
found by `load_tables` from `Components/<X>/docs/sdd.md` `## Requirements` (`:146-160`). So step 1 seeds the sdd with its
first row by hand, in the tool's exact column format; every later row goes in with the tool. Criteria contain no `|`.
Tests claim IDs with `RecordProperty("verifies", "ID,ID")` as the first statement (gtest) or
`@pytest.mark.verifies("ID")` (pytest). Host tests claim Unit-level IDs only.

## 1. Seed — create `PROVESFlightControllerReference/Components/DataRecorder/docs/sdd.md` with exactly this content
```markdown
# Components::DataRecorder

Passive component that records the telemetry and event packet streams into a static RAM ring per stream and flushes them to
CRC-framed segment files under `/rec/` on the SD NAND, with retention by age and capacity and retrieval through file downlink.
Design: `docs-site/dev-loop/design/stored-data/`. Interface and formats: `docs-site/dev-loop/cycles/cycle-m-plan/01-normative.md`.

## Requirements

Pass criteria are decided before testing; edit with `scripts/req.py`, not by hand.

| Name | Description | Method | Level | Pass Criteria | Status | Reason |
|---|---|---|---|---|---|---|
|DataRecorder-1|Each record shall be framed as len u16, bytes, crc32 and each segment shall start with a versioned header|Unit Test|Unit|encodeHeader writes the 20-byte header of cycle-m-plan 01-normative 5.2 and encodeRecord writes len u16 LE, the bytes and CRC-32 LE over len and bytes; the 57-byte golden segment of 01-normative 5.4 is reproduced byte for byte; a 227-byte payload encodes to 233 bytes; a 0-byte or 228-byte payload encodes to 0 bytes|||
```
Check: `$PY scripts/req.py show DataRecorder-1` prints `group: DataRecorder`. (A8-2 later adds the interface sections to this
sdd by hand above `## Requirements`; the table itself is edited only by the tool.)

## 2. Rows DataRecorder-2..12, verbatim
```
$PY scripts/req.py add --group DataRecorder --id DataRecorder-2 --method "Unit Test" --level Unit \
  --description "Decoding shall detect any single-byte corruption or truncation of a record and stop without yielding a wrong record" \
  --criteria "For the golden segment and a 3-record segment, every single-byte flip and every truncation length makes decodeRecord stop at or before the damaged record with TRUNCATED, BAD_LENGTH or BAD_CRC, and every record returned before the stop is byte-identical to the one encoded; every single-byte flip of the header makes decodeHeader return a status other than OK"
$PY scripts/req.py add --group DataRecorder --id DataRecorder-3 --method "Inspection, Unit Test" --level Unit \
  --description "The Fw.Com input handlers shall perform no file I/O and no allocation" \
  --criteria "100 calls each of tlmIn and evtIn leave every Os fake operation counter and every file unchanged and make no event, telemetry, command-response or sendFileOut call; the handler bodies contain no Os call, new, malloc or std container (inspection)"
$PY scripts/req.py add --group DataRecorder --id DataRecorder-4 --method "Unit Test" --level Unit \
  --description "Flush shall occur when the ring reaches the flush count or the flush interval elapses, whichever first" \
  --criteria "flushDue is false for count 0 and true exactly when count >= flushRecords or ticksSinceFlush >= flushIntervalS; with TLM defaults 15 records cause no write on the next tick and 16 cause exactly one write and one flush holding all 16 in push order; one EVT record is written on the 10th tick after the previous flush and not before"
$PY scripts/req.py add --group DataRecorder --id DataRecorder-5 --method "Unit Test" --level Unit \
  --description "The oldest segment shall be deleted when total bytes exceed capacity or its age exceeds retention" \
  --criteria "With BytesOnDisk above capacity each tick deletes exactly one oldest segment with SegmentDeleted reason CAPACITY until BytesOnDisk <= capacity; the oldest segment is deleted with reason AGE on the first tick at which the next segment's open time is more than retentionS old and not earlier; a segment with an invalid header is deleted when it is the oldest; the open segment and the newest segment are never deleted by retention"
$PY scripts/req.py add --group DataRecorder --id DataRecorder-6 --method "Unit Test" --level Unit \
  --description "A full ring shall drop the oldest record and count it" \
  --criteria "A ring of capacity n holding n records drops its oldest on the next push: count stays n, peek(0) returns the former second-oldest and dropped rises by 1; RingDropped rises by 1 per such push; SET_RING_SLOTS below the current count drops the surplus oldest records and adds them to RingDropped"
$PY scripts/req.py add --group DataRecorder --id DataRecorder-7 --method Inspection --level Unit \
  --description "All buffers shall be statically sized by compile-time maxima" \
  --criteria "Every buffer in Components/DataRecorder is a fixed-size member array sized by a DataRecorderCfg.hpp constant or FW_COM_BUFFER_MAX_SIZE; the sources contain no new, malloc or std container"
$PY scripts/req.py add --group DataRecorder --id DataRecorder-8 --method "Unit Test" --level Unit \
  --description "A configuration set outside the validated range shall be rejected with VALIDATION_ERROR and leave the previous value" \
  --criteria "Each SET command with an argument outside its range in cycle-m-plan 01-normative section 3, including SET_CAPACITY when getFreeSpace fails, returns VALIDATION_ERROR, emits exactly one ConfigRejected with that field, value and max, and leaves the ring contents and every configuration value unchanged as read back from the payload SAVE_CONFIG then writes"
$PY scripts/req.py add --group DataRecorder --id DataRecorder-9 --method "Unit Test" --level Unit \
  --description "A write failure shall not lose ring contents and shall not affect the producing component" \
  --criteria "With open, write, short write or flush failing, the ring keeps its records except drop-oldest on overflow, WriteFailures rises by 1 per failed attempt, SegmentWriteFailed is emitted and throttled after 5, and tlmIn and evtIn keep returning normally; after the fault clears the kept records are written in order to a segment with a new sequence number and no existing file is truncated"
$PY scripts/req.py add --group DataRecorder --id DataRecorder-10 --method "Unit Test" --level Unit \
  --description "The recorder configuration shall persist as a CRC-protected record and fall back to defaults when it is missing or invalid" \
  --criteria "SAVE_CONFIG writes /rec/config.bin as a PersistedRecord with magic DRC1 whose 26-byte payload matches cycle-m-plan 01-normative 5.5 (defaults encode to the listed bytes); a new instance applies it on its first tick; a missing file gives the defaults and no event; any single-byte corruption, truncation, wrong layout, wrong stream count or out-of-range value gives the defaults and exactly one ConfigCorrupt"
$PY scripts/req.py add --group DataRecorder --id DataRecorder-11 --method "Unit Test" --level Unit \
  --description "The boot scan shall account for existing segments and continue their numbering without overwriting any file" \
  --criteria "After the scan SegmentsOnDisk and BytesOnDisk equal the number and byte sum of the NNNNNNNN.bin files in the stream directory, other names are ignored and never deleted, at most 32 entries are read per stream per tick in either directory order, and the first segment created is numbered max+1 (1 when empty) and skips a number whose file already exists; a directory failure emits DirectoryScanFailed and the scan restarts next tick"
$PY scripts/req.py add --group DataRecorder --id DataRecorder-12 --method "Unit Test" --level Unit \
  --description "tools/recorder_reader.py shall decode downlinked segments into CSV using the deployment dictionary" \
  --criteria "Against a fixture dictionary the reader turns the golden segment into two tlm rows (fsSpace.FreeSpace 4000000000 and fsSpace.TotalSpace 4294967296 at 1000 s 5 us) with exit 0; a multi-record segment with one event row decodes each argument by name; a segment with a corrupted record keeps the rows before it and exits 1; an invalid header or an unreadable dictionary exits 2 with one error line"
```
DataRecorder-10..12 go beyond the design's nine rows **[decided]**: config persistence, the boot scan and the ground reader
had no row, and each is a result a test must pin.

## 3. System rows, verbatim (method and criteria changes are deliberate and called out)
```
$PY scripts/req.py set DH-L2-03 --method "Unit Test" \
  --criteria "dataRecorder.SET_RING_SLOTS with 1 <= n <= 32 returns OK and emits ConfigApplied(stream, RING_SLOTS, n); with segment writes failing, n + k records then pushed into that stream leave n in the ring and raise its RingDropped by k on the next tick; SET_CAPACITY with an in-range b returns OK, emits ConfigApplied(stream, CAPACITY_BYTES, b), and retention then deletes oldest segments until BytesOnDisk <= b" \
  --reason "DataRecorder design (docs-site/dev-loop/design/stored-data); method changed from Inspection to Unit Test because the sizes are now commanded"
$PY scripts/req.py set DH-L2-04 \
  --criteria "dataRecorder.SET_RING_SLOTS outside 1..32, SET_CAPACITY below SEGMENT_MAX_BYTES or above TotalSpace minus RESERVE_BYTES minus the other stream's capacity (or with getFreeSpace failing), SET_RETENTION_S outside 60..2592000 and SET_FLUSH outside its ranges each return VALIDATION_ERROR, emit one ConfigRejected, and leave every configuration value and the ring contents unchanged" \
  --reason "DataRecorder design (docs-site/dev-loop/design/stored-data)"
$PY scripts/req.py set DH-L2-05 \
  --criteria "On the board dataRecorder.SET_RETENTION_S TLM 3600 returns OK with ConfigApplied within 5 s; SET_RETENTION_S TLM 10 then returns VALIDATION_ERROR with ConfigRejected; after SAVE_CONFIG, /rec/config.bin downlinked by fileDownlink.SendFile decodes to TLM retention 3600" \
  --reason "DataRecorder design (docs-site/dev-loop/design/stored-data)"
$PY scripts/req.py set DH-L2-08 \
  --criteria "With dataRecorder.SET_RETENTION_S TLM 60: a TLM segment closed by CLOSE_SEGMENT and followed by a newer segment opened at t1 is deleted with SegmentDeleted reason AGE within 5 s after t1 + 60 s and is absent from LIST_SEGMENTS TLM, while the newer segment is still listed; deletion logic at unit level is DataRecorder-5" \
  --reason "DataRecorder design (docs-site/dev-loop/design/stored-data)"
$PY scripts/req.py set DH-L2-12 \
  --criteria "A WARNING_HI event raised on the board is found, with the same event id and time tag as the live event, by tools/recorder_reader.py in the EVT segment delivered by dataRecorder.DOWNLINK_NEWEST EVT within 60 s of the event; after a COLD_RESET the pre-reset EVT segment is still listed by LIST_SEGMENTS EVT and retrievable by fileDownlink.SendFile" \
  --reason "DataRecorder evt stream (docs-site/dev-loop/design/stored-data)"
$PY scripts/req.py set CDH-16 \
  --criteria "A WARNING_HI event raised before a COLD_RESET is retrievable after reboot: fileDownlink.SendFile of the EVT segment listed by dataRecorder.LIST_SEGMENTS EVT delivers it within 60 s, and tools/recorder_reader.py decodes that event with the same id and time tag" \
  --reason "DataRecorder evt stream (docs-site/dev-loop/design/stored-data)"
```
Status columns are left alone (they hold the CDR assessment). DH-L2-09/10/14 keep their texts: their criteria name comQueue
and PersistedRecord, not the recorder, so no test in this cycle claims them (follow-up in `04-findings.md` §C).

## 4. Claims
| Test | Claims |
|---|---|
| `UT/test_DataRecorder_Codec.cpp` (A8-1) | DataRecorder-1, -2, -4 (flushDue), -6 (PacketRing) |
| `UT/test_DataRecorder_Component.cpp` (A8-2) | DataRecorder-3, -4, -5, -6, -8, -9, -10, -11; DH-L2-03, DH-L2-04 |
| `scripts/tests/test_recorder_reader.py` (A8-4) | DataRecorder-12 |
| `P/test/int/data_recorder_test.py` (A8-5, ⏸ deferred) | DH-L2-05, DH-L2-08, DH-L2-12, CDH-16 |
| none (review inspection; the reviewer sets `--status "Inspected <date>"`) | DataRecorder-7, the inspection half of DataRecorder-3 |
