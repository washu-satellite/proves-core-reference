# Components::PersistedRecord

Shared persistence mechanism for critical records: a self-validating on-disk
format (magic + version + CRC) plus an atomic file-replace helper. This is the
generalization of TelemetryGate's `TxStateCodec`, and the implementation of the
CDR risk-2 mitigation ("use atomic writes for critical files, store each
critical record with CRC+version").

## Motivation

Three persisted-state files currently have weak or missing integrity
protection, so a corrupted SD/flash sector silently changes flight behavior:

| File | Owner | Current validation |
|---|---|---|
| `/mode_state.bin` | ModeManager | Range check on mode only; corrupt state boots NORMAL (CDR says SAFE) |
| `/sequence_number.txt` | Authenticate | None — corrupt file silently shifts the anti-replay baseline |
| `/boot_count.bin`, `/quiescence_start.bin` | StartupManager | None |
| `/tlm_tx_state.bin` | TelemetryGate | **Migrated.** Was `TxStateCodec` (magic + XOR byte), the pattern this component generalizes; now uses PersistedRecord (TelemetryGate-9) |

## Interface

Two headers, split so the codec stays host-testable:

- `PersistedRecordCodec.hpp/.cpp` — `crc32`, `encode`, `decode`. Pure logic;
  includes only `<cstdint>`, `<cstddef>` and `<cstring>`, no F Prime, Svc or
  Zephyr headers.
- `PersistedRecordFile.hpp/.cpp` — `load`, `store` over `Os::File` and
  `Os::FileSystem`. No `FW_ASSERT`, no logging, no `Fw::String`: every failure
  is returned as a status and the consuming component owns the event and
  command-response policy.

Both live in namespace `Components::PersistedRecord` as free functions.

## Record format

```
offset  size  field
0       4     magic          record-type identifier, unique per record
4       1     version        format version, bumped on layout change
5       2     length         payload length in bytes
7       n     payload        consumer-defined state
7+n     4     crc            CRC-32 over all preceding bytes
```

Multi-byte fields (`length`, `crc`) are little-endian. `crc` is CRC-32
(IEEE 802.3), reflected, polynomial `0xEDB88320`, initial value and final XOR
`0xFFFFFFFF`, computed bitwise so no lookup table is held in flash or RAM; it
covers bytes 0 through 6+n, i.e. the magic, version, length and payload.

Sizes are compile-time constants (PersistedRecord-7):

| Constant | Value | Meaning |
|---|---|---|
| `FORMAT_VERSION` | 1 | Written by every `encode`; required by `decode` |
| `MAGIC_SIZE` | 4 | Record-type prefix |
| `HEADER_SIZE` | 7 | magic + version + length |
| `CRC_SIZE` | 4 | Trailing checksum |
| `OVERHEAD` | 11 | `HEADER_SIZE + CRC_SIZE` |
| `MAX_PAYLOAD_SIZE` | 64 | Largest payload any consumer may persist |
| `MAX_RECORD_SIZE` | 75 | `OVERHEAD + MAX_PAYLOAD_SIZE`; bounds every stack buffer |

A payload larger than `MAX_PAYLOAD_SIZE` is rejected at encode time. Nothing on
the encode, decode, load or store path allocates.

### Decode order

`decode` applies its checks in a fixed order so each failure class maps to
exactly one status, and so the caller's payload buffer is written only once a
record is fully validated:

1. `bufLen < OVERHEAD` → `TRUNCATED`
2. magic mismatch → `BAD_MAGIC`
3. declared length > `MAX_PAYLOAD_SIZE` → `BAD_LENGTH`;
   `bufLen < OVERHEAD + declared` → `TRUNCATED`;
   `bufLen > OVERHEAD + declared` → `BAD_LENGTH` (trailing bytes are as
   suspect as missing ones)
4. CRC mismatch → `BAD_CRC`
5. unrecognized version → `BAD_VERSION`
6. declared length > caller capacity → `BAD_LENGTH`
7. copy payload, return `OK`

The version check deliberately runs *after* the CRC, so a `BAD_VERSION` record
is known to be otherwise intact and a consumer can safely migrate from it.

### Status values

| Status | Meaning |
|---|---|
| `OK` | Valid record; payload and length populated |
| `TRUNCATED` | Fewer bytes present than the header declares |
| `BAD_MAGIC` | Magic prefix does not match the expected record type |
| `BAD_LENGTH` | Declared length oversize, inconsistent, or beyond caller capacity |
| `BAD_CRC` | CRC-32 mismatch (any bit flip in magic, version, length or payload) |
| `BAD_VERSION` | CRC-valid record carrying an unrecognized format version |
| `MISSING` | Backing file absent — first boot, not a corruption |
| `INVALID_ARGUMENT` | Null pointer or oversize payload from the caller |
| `OPEN_ERROR` | File exists but could not be opened |
| `READ_ERROR` | Read failed partway through |
| `WRITE_ERROR` | Write failed or was short |
| `SYNC_ERROR` | Flush to media failed |
| `RENAME_ERROR` | Rename of the temporary file over the target failed |

## Atomic update

`store(path, tempPath, magic, payload, payloadLen)` runs a fixed sequence:

1. encode into a stack buffer of `MAX_RECORD_SIZE` bytes
2. `open(tempPath, OPEN_CREATE, OVERWRITE)` — truncates a stale temporary left
   by an earlier failed update; failure → `OPEN_ERROR`
3. `write(WAIT)` — a failed or short write → `WRITE_ERROR`
4. `flush()` — failure → `SYNC_ERROR`. This is checked explicitly because
   `Os::File::write(WAIT)` flushes but discards the flush status, so without it
   an unsynced record would be reported as a successful store
5. `close()`, then `Os::FileSystem::rename(tempPath, path)` — failure →
   `RENAME_ERROR`

The target is never opened on the write path, so a failure at any step leaves
the previous record intact and fully decodable. A failed update leaves the
temporary behind rather than spending extra I/O deleting it; step 2 truncates
it next time.

The temporary path is a constant supplied by the consumer: the target path with
a `.tmp` extension (e.g. `/tlm_tx_state.bin` and `/tlm_tx_state.tmp`).

`load(path, tempPath, ...)` reads and decodes the target. If the target is
`MISSING` and `tempPath` is non-null, it then reads the temporary as a
fallback — that is the record a previous store wrote and flushed but failed to
rename into place. A fully valid temporary is returned as `OK`; anything else
is reported as `MISSING`, i.e. the consumer's first-boot default. The fallback
is read-only, so boot performs no writes and costs no flash wear. A stale
temporary beside a *present* target is ignored entirely.

Note FAT/exFAT gives no journaling and rename atomicity across power loss is
not guaranteed by the filesystem — Zephyr's `fatfs_rename` unlinks the
destination before moving the entry, so the replace is not atomic on the target
and there is a window in which neither path holds the target name. The CRC is
what guarantees a torn write is *detected*; the temp-then-rename order plus the
load-time temporary fallback are what make the old or the new record the likely
survivor. PersistedRecord-5 covers exactly this window and is board-level: it is
verified by the hardware filesystem-resilience test, not by any host unit test.

## Consumers

ModeManager (MM0011–MM0012), Authenticate (AUTH013), StartupManager
(REQ-SM-008), TelemetryGate (TelemetryGate-9).

## Requirements

Pass criteria are decided before testing; edit with `scripts/req.py`, not by hand.

| Name | Description | Method | Level | Pass Criteria | Status | Reason |
|---|---|---|---|---|---|---|
|PersistedRecord-1|Every critical record shall be persisted as a self-validating blob: 4-byte record-type magic, 1-byte format version, 2-byte payload length, the payload, and a 32-bit CRC covering all preceding bytes|Unit Test, Inspection|Unit|Encoded blob matches the documented byte layout for representative payloads; CRC value matches an independent CRC-32 reference implementation|||
|PersistedRecord-2|Decoding shall detect any corruption of a stored record — every single-bit flip, every single-byte corruption, any truncation, and a mismatched record-type magic — returning a distinct error status per failure class and leaving the caller's payload buffer unmodified|Unit Test|Unit|For every single-byte corruption (all offsets, all values tested per offset) and every truncation length of a valid record: decode returns the matching non-OK status and the output payload is untouched; zero false accepts|||
|PersistedRecord-3|Decoding shall reject a record whose format version is unrecognized with a distinct status so consumers can apply migrations; encoding shall always write the current format version|Unit Test|Unit|A structurally valid record carrying version N+1 with a correct CRC decodes to the bad-version status with the payload untouched; every freshly encoded record carries the current version|||
|PersistedRecord-4|A record update shall be atomic: the new record is written in full to a temporary file, flushed to media, then renamed over the target, so the target path never holds a record that decodes as valid but mixes old and new content|Unit Test|Unit|With injected failure at each step (open, write, sync, rename): the target still holds the previous record and it decodes OK; a stale temporary file left by a failed update does not prevent a subsequent successful update|||
|PersistedRecord-5|After power loss at any point during a record update, a subsequent load shall yield the previous payload, the new payload, or a detected-corrupt status — never an undetected wrong payload|Test|Board|Across repeated power-cycle-during-write iterations of the filesystem resilience test, every post-reboot load returns the old payload, the new payload, or a corruption status; zero silent wrong reads|||
|PersistedRecord-6|Loading a record whose backing file is absent shall return a distinct missing-file status so consumers can apply first-boot defaults without raising corruption warnings|Unit Test|Unit|With no file present, load returns the missing status (not a corruption status) and the payload buffer is unmodified|||
|PersistedRecord-7|Encode, decode, load, and store shall operate on caller-provided or statically sized buffers with no dynamic memory allocation, and the maximum record size shall be a compile-time constant|Inspection|Unit|Code inspection finds no heap allocation on any encode, decode, load, or store path; a payload exceeding the compile-time maximum is rejected at encode time|||

## Change Log
| Date | Description |
|---| --- |
|Aug 2026| Requirements-first draft; no implementation yet |
|Sep 2026|Implemented codec and atomic file store; TelemetryGate migrated (TelemetryGate-9)|
