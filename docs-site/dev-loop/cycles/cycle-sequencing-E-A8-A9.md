# Sequencing Cycle E, A8 (DataRecorder) and A9 (BurstCapture) so they do not conflict

> Status 2026-09-20: E is done (see `README.md`); this file stays the **ownership map and slot/budget table** for A8 and A9. Schedule: `../ROADMAP.md`.

Written 2026-09-18 against `main` @ cab7439, `cycle-e-plan/`, and `../design/stored-data/`. Order: **E → A8 → A9.** Each is one
`cdh-cycle` run. A9 is a *producer* into A8, so it is small once A8 exists; building A9 first would duplicate A8's file writing
and cost 26 KB of RAM that A8 then makes redundant.

## Ownership (who owns what; nothing is shared-write)

| Thing | Owner | Others may |
|---|---|---|
| `Components/DriverBoardHandler`, `DriverBoardProtocol`, `Crc16`, the wire spec (`cycle-e-plan/02-protocol.md`) | E | A9 connects to `sampleOut`; A9 adds STREAM_START/STOP *commands* to the handler (an append, not a redesign) |
| `Components/DataRecorder` (`SegmentCodec`, `PacketRing`, segment format, retention, `/rec/` layout) | A8 | A9 writes into a third stream through the same ring API; never writes files itself |
| `Components/BurstCapture` (trigger logic, record layout, IMU join, timestamp alignment) | A9 | — |
| `test/unit-tests/support/DriverBoardFake.hpp` | E | A9 appends a stream mode |
| Project constants: `MAX_PACKETIZER_PACKETS` 22→24, `CMD_DISPATCHER_DISPATCH_TABLE_SIZE` 400→512 | **E, once** | A8/A9 must not touch them |
| `PRMDB_NUM_DB_ENTRIES` | nobody until the persistence gate passes (`cycle-e-plan/07`) | A8 persists its config via `PersistedRecord`, not PrmDb, so A8 is **not** gated |
| `instances.fpp`, `topology.fpp`, `ReferenceDeploymentPackets.fppi` | each cycle appends its own block | never edit another cycle's block |

## Scheduling slots (all groups are 25 wide, `AcConstants.fpp:7`)

| Group | Used at cab7439 | E | A8 | A9 |
|---|---|---|---|---|
| 50 Hz | 0 | 1 `driverBoardUart.schedIn` | — | 2 `burstCapture.sample` tick (reads IMU ports like DetumbleManager does) |
| 10 Hz | 0-4, 6-13 | 5 `driverBoardBufferManager.schedIn` | — | — |
| 1 Hz | 0-11, 13-20 | 12 `driverBoardHandler.run` | **21** `dataRecorder.run` (the design says 20; 20 is now `faultManager.run`, Cycle D) | 22 `burstCapture.run` (housekeeping/trigger state) |

## Data path after all three

```
STM32 ──uart1──► driverBoardUart ──► driverBoardHandler ──sampleOut──► burstCapture ──burstIn──► dataRecorder (stream 2 "burst")
                                                                          ▲ imuManager mag/gyro ports (50 Hz)          │ ring → /rec/burst/NNNNNNNN.bin
tlmSend ──comSplitterTelemetry[2]──────────────────────────────────────────────────────────────► dataRecorder (stream 0 "tlm")
events  ──comSplitterEvents[2]──────────────────────────────────────────────────────────────────► dataRecorder (stream 1 "evt")
                                                                                                    └─► FileHandling.fileDownlink.SendFile (exists)
```
A9 batches five 44-byte burst records into one 220-byte ring slot (A8's slots are `FW_COM_BUFFER_MAX_SIZE` = 233 B), so
A9 needs no ring of its own — only a 220-byte staging buffer. That removes the 26 KB "RAM-only ring" from A9 entirely.

## Budgets — measured after the upstream sync, Cycle F (2026-09-19, feat/upstream-sync @ f26d9e1d)

| Resource | After E (d71fc8fd) | After F3 (measured) | Note |
|---|---|---|---|
| RAM | 363528 B, 68.27 % | **331376 B, 62.23 %** | −32152 B: upstream #467 cut the mbedTLS static heap 32K → 8K and F´ 4.3.0 replaced the packetizer hash table with a `RedBlackTreeMap` + entry table at 256 channels / 24 packets |
| FLASH | 735532 B, 70.44 % | **725984 B, 69.53 %** of the 1 MB slot | −9548 B; upstream pristine is 674924 B, so the fork carries +51060 B |
| Opcodes | 377 of 512 | **387 of 512** | |
| Packets | 23 of 24 | **23 of 24** | id 24 is the last free |
| Channels (`MAX_PACKETIZER_CHANNELS`, packets **and** omit) | 242 of 256 hash buckets | **244 of 256** | A8 adds ≥ 14 channels and A9 a `BurstStatus` packet: **A8's constants row must raise `MAX_PACKETIZER_CHANNELS`** (about 130 B RAM per channel at 24 packets: 4 + 24 × 4 + 8 B entry plus a tree node); `check_packet_set.py` warns at 90 % |
| Parameters | 103, all RAM-only | **106, all RAM-only** | persistence gate open; a 4.1.x `/prmDb.dat` fails the 4.3.0 CRC header once (none saved today) |

**Consequence for A8:** the RAM guard question is moot at 62.2 %: A8's two 32-slot rings (+15 KB) plus a 32-channel raise (about +4 KB) land near 66 %, under the 70 % guard, so A8 keeps its planned ring sizes and the guard stays at 70 %. The open items are (1) the channel limit — `MAX_PACKETIZER_CHANNELS` 256 → at least 288 in A8's constants row, since `TLMPACKETIZER_HASH_BUCKETS` no longer exists; and (2) the A8 design's stale references in `design/stored-data/04-plan.md` and `02-design.md`, to fix in A8 Phase 0: 1 Hz slot 20 → 21 (slot 20 is `faultManager.run`, `topology.fpp:330`; slot 19, freed by the sync from `authenticationRouter.run`, is also open), the dictionary check must read the 4.3.0 keys `telemetryPacketSets[0].members` / `omitted` (not `packets`), and the `Os::Directory` fake must target the rewritten Zephyr API (`lib/fprime-zephyr/fprime-zephyr/Os/Directory.hpp` is new at fprime-zephyr b14101dd: `ZephyrDirectory` with `open/isOpen/rewind/read/close` over `fs_dir_t`; `Directory.cpp` changed 85 lines against the pre-sync 31399714).

## Budgets as originally planned (baseline 64.4 % RAM, 68.5 % FLASH, 357 opcodes, 22 packets) — superseded by the table above

| | E | A8 | A9 | After all three |
|---|---|---|---|---|
| RAM | +~2.5 KB (1 KB ring, 512 B pool, handler) | +~15 KB (2 rings × 32 × 235 B) | +~8 KB (third ring 32 × 235 B) + 0.5 KB | ≈ 69.5 % — under the 70 % guard **only because A9 shares A8's ring**; if it trips, A8's tlm ring goes to 16 slots |
| FLASH | +~15 KB | +~10 KB | +~6 KB | ≈ 71.5 % of the 1 MB slot |
| Opcodes | 373 (raise to 512) | 382 | ~390 | fine |
| Packets | 23 of 24 | 23 (A8 adds channels to `FileSystem`) | 24 (burst status) | 24 of 24 — next cycle raises again |
| Params | +5 (RAM-only) | 0 (PersistedRecord config) | +4 (RAM-only) | persistence gate decides when they may be saved |

## Requirement ownership (no double claims)

| Row | Claimed by |
|---|---|
| TM-L2-01 payload clause, CDH-17/20 reason text, DriverBoardHandler-*, DriverBoardProtocol-* | E |
| DH-L2-03/04/05/08/12, CDH-16, CDH-27 (scheduled per-pass downlink), DH-L2-13, DataRecorder-* | A8 |
| ADCS-L2-04 (collect and store attitude telemetry), ADCS-L2-06, BurstCapture-*, and the model's MO-2 minimal path | A9 |

## Stale lines in `design/stored-data/` to fix in A8 Phase 0 (do not fix them now)

- 1 Hz slot 20 → 21. Opcode baseline 361 → 373 (after E). "packet count unchanged" still true.
- Open decision 3 (card size): S8 resolved it — 4 GB soldered SD NAND; capacity defaults can be raised and `RESERVE_BYTES` set once `fsSpace.TotalSpace` is read on a board.
- Add stream 2 "burst" to the tier table with A9 as the producer, `/rec/burst/`, default ring 32 slots, flush 8 records / 5 s.

## One correction to earlier tutoring

The recorder design keeps `DataRecorder` **passive** and does its SD write on the 1 Hz tick, bounded to one `write` + `flush`
per tick and measured on target, with the 50 Hz group preempting it. That is a legitimate choice on this board: the cost is a
bounded delay to other 1 Hz members, never to the 50 Hz control loop, and it saves a thread. "SD writes must never run on a
rate-group thread" was too strong; the rule is *bounded, on the lowest-priority group, and never on the group that controls
attitude*. A9 therefore has no reason to add a thread either.

## What can be built with no hardware

E entirely (fake board). A8 entirely (Os fakes exist from Cycle A; add `Os::Directory`). A9 entirely (fake board stream mode
+ IMU stubs). What needs the board: E's HP-15, A8's HP-12 extension, A9's end-to-end pulse-and-capture. What needs the STM32
firmware (A1): real samples. Until A1 exists, A9's board test uses the loopback jumper on J18 9↔10 with the RP2350 sending
itself SAMPLE frames from a test command — worth adding to the handler as `TEST_INJECT_SAMPLE`, gated behind a build flag.
