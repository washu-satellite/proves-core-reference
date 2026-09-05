# Components::StartupManager

## Overview

The StartupManager component manages boot counting, quiescence waiting periods, and automatic dispatch of startup sequences.

## Purpose

The StartupManager serves three primary functions:
1. Boot Counting: Tracks the number of system boots persistently across power cycles
2.  Implements a configurable waiting period (default 45 minutes) before allowing full system startup, useful for missions requiring initial stabilization
3. Automatically dispatches and monitors the execution of startup command sequences

### Persisted files

Both persisted files are PersistedRecords (see
`Components/PersistedRecord/docs/sdd.md`): self-validating records carrying a
record-type magic, a format version, a payload length and a CRC-32. Updates are
atomic — the record is written and flushed to `<path>.tmp`, then renamed over
the target — so a failure at any step leaves the previous record intact and
still valid (REQ-SM-008).

| File | Magic | Payload |
|---|---|---|
| `BOOT_COUNT_FILE` (`/boot_count.bin`) | `"SBC1"` | 8 bytes: the count as a little-endian U64, so the format does not depend on the target's `FwSizeType` width |
| `QUIESCENCE_START_FILE` (`/quiescence_start.bin`) | `"SQS1"` | 11 bytes, mirroring `Fw::Time::SERIALIZED_SIZE`: time base U16 LE, context U8, seconds U32 LE, useconds U32 LE |

A missing file is the first boot and stays silent: the boot count starts at 1
and the quiescence period starts at the current time, which is written.

A file that is present but fails validation — truncated, wrong magic, bad
length, bad CRC, an unrecognized format version, or unreadable — falls back to
the same defined default and emits exactly one warning: boot count 0 (raised to
1 by the minimum-of-one bump) with `BootCountUpdateFailure`, quiescence
restarted from now with `QuiescenceFileInitFailure`. A CRC-valid quiescence
record whose useconds field is outside `[0, 999999]` is treated as a validation
failure too, because `Fw::Time::add` would otherwise assert in a boot loop.

`GET_BOOT_COUNT` reads the file without rewriting it; only a counted boot
(`run` handler, first call) stores an incremented value.

Legacy files left by an earlier image carry no magic, version or CRC and are not
migrated. On the first boot of this image the 8-byte big-endian boot count is
shorter than the record overhead and the 11-byte big-endian time fails the magic
check, so each emits one warning, the boot count restarts at 1 and the
quiescence period is re-waited once (45 minutes with `ARMED` at its default).
Removing both files before the upgrade reboot makes the upgrade a clean first
boot instead.

## Usage Examples
Add usage examples here

### Diagrams
Add diagrams here

### Typical Usage

## How to Run

1. Choose the startup sequence from the sequences file. To update the .bin file run `make sequence SEQ=startup`
2. Upload the startup.bin file using uplink. Make sure its set in root in the cube as startup.bin
3. Restart the cube, it should do the startup sequence right away
4. To disable the startup sequence delete the sequence file. Use FileHandling.filemanager.RemoveFile to remove the startup.bin file

The StartupManager maintains internal state tracking its lifecycle:

| State | Description | Trigger |
|-------|-------------|---------|
| **Uninitialized** | Initial state before first `run` call. `m_boot_count == 0` | System initialization |
| **Initialized** | Boot count and quiescence start time have been loaded/set | First `run` call |
| **Waiting for Quiescence** | `m_waiting == true`, awaiting quiescence period expiration or disarm | `WAIT_FOR_QUIESCENCE` command received |
| **Running** | Normal operation, updating telemetry on each `run` call | Continuous after initialization |

**State Transitions:**

```
Uninitialized → Initialized (first run call)
Initialized → Waiting for Quiescence (WAIT_FOR_QUIESCENCE command)
Waiting for Quiescence → Running (quiescence period expires OR ARMED=false)
```

## Port Descriptions

| Port Name | Type | Direction | Description |
|-----------|------|-----------|-------------|
| `run` | `Svc.Sched` | Input (sync) | Scheduled execution port called by rate group. Manages boot initialization and quiescence monitoring |
| `runSequence` | `Svc.CmdSeqIn` | Output | Port for dispatching command sequences to the command sequencer |
| `completeSequence` | `Fw.CmdResponse` | Input (sync) | Receives completion status from the command sequencer after startup sequence execution |

## Component States

| State Variable | Type | Description |
|----------------|------|-------------|
| `m_boot_count` | `FwSizeType` | Current boot count. Zero indicates uninitialized state |
| `m_quiescence_start` | `Fw::Time` | Time when quiescence period started (mission epoch) |
| `m_waiting` | `std::atomic<bool>` | True when waiting for quiescence period to elapse |
| `m_stored_opcode` | `FwOpcodeType` | Opcode of pending `WAIT_FOR_QUIESCENCE` command |
| `m_stored_sequence` | `U32` | Sequence number of pending `WAIT_FOR_QUIESCENCE` command |

## Sequence Diagrams

## Parameters

| Name | Type | Default Value | Description |
|------|------|---------------|-------------|
| `ARMED` | `bool` | `true` | When true, system waits for quiescence period. When false, quiescence is bypassed |
| `QUIESCENCE_TIME` | `Fw.TimeIntervalValue` | `{seconds = 45 * 60, useconds = 0}` | Duration to wait for quiescence (45 minutes by default) |
| `QUIESCENCE_START_FILE` | `string` | `"/quiescence_start.bin"` | File path for storing the mission-wide quiescence start time. The atomic update stages through `<path>.tmp` |
| `STARTUP_SEQUENCE_FILE` | `string` | `"/startup.bin"` | Path to the command sequence file to run at startup |
| `BOOT_COUNT_FILE` | `string` | `"/boot_count.bin"` | File path for storing the boot count. The atomic update stages through `<path>.tmp` |

## Commands

| Name |  Description |
|------|-------------|
| `WAIT_FOR_QUIESCENCE` | Lets you start with opcode cmdseq and whether or not waiting |

## Events

| Name | Severity | Arguments | Description |
|------|----------|-----------|-------------|
| `BootCountUpdateFailure` | WARNING_LO | None | Emitted when the boot count file fails validation on load (corrupt, truncated, wrong magic or version, or unreadable), in which case the count restarts at 1; and when the incremented count cannot be stored, in which case it was raised in memory but not persisted. A missing file is a silent first boot |
| `QuiescenceFileInitFailure` | WARNING_LO | None | Emitted when the quiescence start time file fails validation on load (corrupt, truncated, wrong magic or version, unreadable, or a useconds field outside [0, 999999]), in which case quiescence restarts from the current time; and again if that current time cannot be stored. A missing file is a silent first boot |
| `StartupSequenceFinished` | ACTIVITY_LO | None | Emitted when the startup sequence completes successfully |
| `StartupSequenceFailed` | WARNING_LO | `response: Fw.CmdResponse` | Emitted when the startup sequence fails, includes the failure response code |

## Telemetry

| Name | Type | Update Policy | Description |
|------|------|---------------|-------------|
| `BootCount` | `FwSizeType` | Update on change | Current boot count. Increments on each system boot |
| `QuiescenceEndTime` | `Fw.TimeValue` | Update on change | Absolute time when the quiescence period will end. Updated on each `run` call |

## Requirements

| Name | Description | Method | Level | Pass Criteria | Status | Reason |
|---|---|---|---|---|---|---|
|REQ-SM-001|StartupManager shall track boot count across power cycles|Verification: Check that boot count increments on each boot via telemetry|||||
|REQ-SM-002|StartupManager shall support configurable quiescence waiting period|Verification: Confirm QUIESCENCE_TIME parameter affects wait duration|||||
|REQ-SM-003|StartupManager shall automatically dispatch startup sequence on first run call|inspection|||||
|REQ-SM-004|StartupManager shall allow disabling quiescence via `ARMED` parameter|Verification: Set `ARMED=false` and confirm `WAIT_FOR_QUIESCENCE` completes immediately|||||
|REQ-SM-005|StartupManager shall emit events for sequence completion status|Verification: Monitor events during sequence execution|||||
|REQ-SM-006|StartupManager shall update telemetry on each run cycle|Verification: Confirm `BootCount` and `QuiescenceEndTime` telemetry updates|||||
|REQ-SM-007|StartupManager shall handle file I/O errors gracefully|Verification: Remove file permissions and verify warning events are emitted|||||
|REQ-SM-008|The boot count and quiescence start time files shall be stored as PersistedRecords (magic, version, CRC) updated atomically; a corrupt or truncated file shall be detected, emit a warning event, and fall back to a defined default (boot count 0, quiescence restarted)|Unit Test|Unit|For every single-byte corruption and every truncation of each file: exactly one warning event and the defined default is applied; valid files round-trip their values|||


### Unit Tests


## Change Log

| Date | Author | Description |
|------|--------|-------------|
| 2026-09 | Cycle A | Boot count and quiescence start moved from raw big-endian serializations to PersistedRecords (magic `"SBC1"` / `"SQS1"`, CRC-32, atomic replace via `<path>.tmp`). A file that fails validation now warns once and applies the defined default; a missing file stays silent. `GET_BOOT_COUNT` no longer rewrites the file (REQ-SM-008). |
