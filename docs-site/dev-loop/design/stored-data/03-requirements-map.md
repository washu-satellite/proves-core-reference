# 03: Requirements map

Method and Level follow the repo rule: unit tests prove component logic, board tests prove system L2/L1 behaviour. Every
row below is edited only with `scripts/req.py`; the criteria here are the proposed texts, set in Phase 0 before any test claims
the ID. Component rows `DataRecorder-1..9` are added to `P/Components/DataRecorder/docs/sdd.md` the same way.

| Row | What the design does | Method / Level | Phase |
|---|---|---|---|
| DH-L2-01 buffer telemetry prior to downlink | Ring + segments hold every emitted packet until a pass; current criterion (events survive TRANSMIT DISABLED) stays as the board test, add "a segment written before a pass is downlinkable during it" | Integration / Board (HP-13) | 3 |
| DH-L2-02 allocate and manage memory | Rings are static arrays sized by compile-time maxima; no allocation on any path | Inspection + Unit (DataRecorder-7) | 2 |
| DH-L2-03 configurable buffer sizes | `SET_RING_SLOTS`, `SET_CAPACITY` | Unit: after a valid set, reported capacity equals the new value next tick | 2 |
| DH-L2-04 validate sizes against memory | Static ring max; capacity checked against `getFreeSpace` total minus reserve | Unit: oversize request → `VALIDATION_ERROR`, previous value retained | 2 |
| DH-L2-05 apply only after successful command | Validate-then-commit; nothing changes on error | Integration / Board: set OK → new value; set error → old value | 3 |
| DH-L2-06 configurable priority | Unchanged (comQueue order); clause 2 still not implemented, out of scope here | Inspection (as recorded 2026-09-05) |: |
| DH-L2-07 selective downlink | `LIST_SEGMENTS` + `fileDownlink.SendFile` | Integration / Board: a named segment arrives byte-identical | 3 |
| DH-L2-08 retain for configurable duration | `RETENTION_S` + age-based deletion | Integration / Board: record older than D absent, younger present; Unit for the deletion logic (DataRecorder-5) | 2 (unit), 3 (board) |
| DH-L2-09 delete/overwrite when full | Capacity pass deletes oldest segment; ring drops oldest | Unit (DataRecorder-5/6) + existing Inspection of comQueue | 2 |
| DH-L2-10 defined discard policy | Documented: ring drop-oldest, disk delete-oldest, comQueue drop-newest | Inspection | 2 |
| DH-L2-11 integrity during storage | CRC-32 per record and per header; reader stops at first bad record | Unit (DataRecorder-2) + HP-12 power-cut loop extended to segments | 2, 3 |
| DH-L2-12 / CDH-16 log fault events for later retrieval | `evt` stream captures every event packet incl. FaultManager; retrievable after reboot via SendFile | Integration / Board: WARNING_HI before COLD_RESET is in a downlinked `evt` segment within 60 s | 3 |
| DH-L2-13 scheduled downlink of stored telemetry | Per-pass sequence (`sequences/`) | Integration / Board: sequence delivers newest segment each pass | 3 |
| DH-L2-14 concurrent access without corruption | Rings are written from the producer's context and drained from the 1 Hz context under the component's guard; segments are written from one context only | Unit + Inspection | 2 |
| FD-L2-04 fault events with timestamps, retained | Event packets carry the EventManager time tag; retention as DH-L2-12 | Integration / Board | 3 |
| CDH-8 buffering for later transmission | As DH-L2-01 | Board | 3 |
| CDH-12 prioritisation of stored data | Unchanged | Inspection |: |
| CDH-14 manage onboard storage for telemetry and housekeeping | Capacity, retention, `BytesOnDisk`/`SegmentsOnDisk` next to `FsSpace` | Board (existing criterion + recorder channels) | 3 |
| CDH-27 controls telemetry at least every 2 days | Detumble packets are in the `tlm` stream; per-pass sequence | Demonstration / Board | 3 |
| ADCS-L2-04 collect and store attitude telemetry (may) | Stored once an attitude channel exists; recorder is the store |: | later |

Proposed component rows:

| ID | Requirement | Method / Level |
|---|---|---|
| DataRecorder-1 | Each record shall be framed as `len u16, bytes, crc32` and each segment shall start with a versioned header | Unit |
| DataRecorder-2 | Decoding shall detect any single-byte corruption or truncation of a record and stop without yielding a wrong record | Unit |
| DataRecorder-3 | The `Fw.Com` input handlers shall perform no file I/O and no allocation | Inspection, Unit |
| DataRecorder-4 | Flush shall occur when the ring reaches the flush count or the flush interval elapses, whichever first | Unit |
| DataRecorder-5 | The oldest segment shall be deleted when total bytes exceed capacity or its age exceeds retention | Unit |
| DataRecorder-6 | A full ring shall drop the oldest record and count it | Unit |
| DataRecorder-7 | All buffers shall be statically sized by compile-time maxima | Inspection |
| DataRecorder-8 | A configuration set outside the validated range shall be rejected with `VALIDATION_ERROR` and leave the previous value | Unit |
| DataRecorder-9 | A write failure shall not lose ring contents and shall not affect the producing component | Unit |
