# 01: Current state: what happens to data that is not downlinked now

Verified against the working tree at cab7439; the bullets marked **(2026-09-27)** were re-checked against `main` @ 6296ef32 after the
upstream sync. Line numbers drift; grep the symbol before trusting `:NN`.

## Storage medium

- One soldered SD NAND on SPI0 (`boards/bronco_space/proves_flight_control_board_v5/proves_flight_control_board_v5.dtsi:135-150`, node `sdhc0`, disk name `SD`, 24 MHz; the flight target is `proves_flight_control_board_v5e`, which includes this dtsi). **(2026-09-27)** It is not a card: S8 shows U2 ZDSD04GLGEAG, about **4 GB** by part number, no socket. The LoRa radio is on SPI1 (`:153-160`), so SD traffic does not share a bus with the UHF radio; the S-band module (E28-2G4M27S) *is* on SPI0 with the SD NAND (model C-31), unwired today, so SD flushes and S-band transfers would contend if S-band is ever driven.
- Filesystem is FAT/exFAT (ELM FatFs) auto-mounted from the devicetree fstab, formatted on first mount (`prj.conf:65-73`); `Components/FsFormat` can re-format by command.
- Free/total space is reported by `Components/FsSpace` at 1 Hz via `Os::FileSystem::getFreeSpace` (`FsSpace.cpp:30`), channels `FreeSpace`/`TotalSpace` in the `FileSystem` packet.
- Time comes from `rtcManager` (`Top/topology.fpp:134`), so stored records can carry a real time tag.
- The CDR risk register rates SD reliability as risk 2 ("SD writes are a frequent source of corruption; you lose data products and logs") and prescribes the mitigation this design follows: atomic writes for critical files, CRC+version on every critical record, high-rate logs in RAM buffers with controlled flush intervals (CDR deck, Risk Mitigation Plan).

## The three downlink queues

Every byte that leaves the vehicle goes through one `Svc.ComQueue` per link (LoRa and UART; S-band wiring commented out):

| Queue | Fed by | Depth | Priority | Where configured |
|---|---|---|---|---|
| EVENTS | `CdhCore.events.PktSend` → `comSplitterEvents` | 50 | 0 (highest) | `P/project/config/ComCcsdsConfig.fpp:24-34`, applied in `ComCcsdsLora/ComCcsds.fpp:14-25` |
| FILE | `FileHandling.fileDownlink` → `downlinkRepeater` | 1 | 1 | same |
| TELEMETRY | `CdhCore.tlmSend.PktSend` → `comSplitterTelemetry` | 1 | 2 | same |

Overflow policy is drop-newest with one latched `QueueOverflow` warning per queue (`lib/fprime/Svc/ComQueue/ComQueue.cpp:246-271`).
Both splitters are `Svc.ComSplitter` with five outputs (`ComSplitter.fpp:10`) of which two are wired, so each has three spare taps.

## What is kept, what is lost

| Data | Producer | Today | Consequence |
|---|---|---|---|
| Channelized telemetry | `tlmSend` is `Svc.TlmPacketizer` (project override `P/project/config/CdhCoreTlmConfig.fpp`), 23 packets of `MAX_PACKETIZER_PACKETS` = 24 **(2026-09-27**; 22 of 22 at cab7439) (`Top/ReferenceDeploymentPackets.fppi`), run every ~30 s via `telemetryDelay` ÷29 and gated by `TelemetryGate` (`topology.fpp:182`) | Latest value only; TELEMETRY queue depth 1; default packet level 1 sends only `Beacon` | Everything above level 1, and everything produced between passes, is gone. When `TelemetryGate` is DISABLED the run tick is withheld, so no packets are even produced |
| Events (incl. FaultManager, Health, QueueOverflow) | `Svc.EventManager`, time-tagged at emit (`EventManager.cpp:111-114`) | 50 deep in RAM per link; nothing on disk; lost on reset | CDH-16, DH-L2-12, FD-L2-04 retention clause unmet |
| Critical state | ModeManager, Authenticate, StartupManager, TelemetryGate | `Components/PersistedRecord`: magic + version + length + payload (≤ 64 B) + CRC-32, temp + flush + rename, temp fallback on load | Done; DH-L2-11/14 largely covered |
| Parameters | `Svc.PrmDb` on `/prmDb.dat` (`FileHandling.fpp:36`) | RAM until `PRM_SAVE_FILE`; no integrity check; `PRMDB_NUM_DB_ENTRIES` is the F´ default 25 (no project override under `P/project/config/`) against 106 parameters in the dictionary **(2026-09-27**; 98 at cab7439; persistence gate still open, `cycle-e-plan/07`) | Not suitable for recorder configuration (see 02 §5) |
| Files (camera, uplinked files) | `Svc.FileDownlink` (`P/project/config/FileHandlingConfig.fpp:29-31`: 1 s cycle, 1 s cooldown, queue depth 3) | Stored on SD, sent by `SendFile`, byte-exact (DH-L2-07 criterion) | Works; this is the retrieval path the design reuses |
| Firmware images | `Components/FlashWorker` | Update only | Out of scope |

## Headroom that the design consumes

All re-checked **2026-09-27** at `main` @ 6296ef32 (`scripts/check_capacity.py --dictionary none`; dictionary from the build copy at e1eced14):

- 1 Hz rate group: 18 of 25 member slots used, highest index 20 (`faultManager.run`, `topology.fpp:330`; `AcConstants.fpp:7`). Slot 19 was freed by the sync (the command-loss router was retired) and slot 21 is the first index above the highest used; **the recorder takes slot 21** so it runs after `faultManager`. (At cab7439: 20 of 25 used, and the design said slot 20, which is now `faultManager.run` from Cycle D.)
- Command dispatch table: 512 (`CommandDispatcherImplCfg.hpp:16`, raised by Cycle E) against 387 opcodes.
- Packetizer: 23 of `MAX_PACKETIZER_PACKETS` = 24 (`TlmPacketizerCfg.hpp:19`); id 24 is the last free and A9's `BurstStatus` takes it, so A8 adds **no packet** (its channels go into `FileSystem`).
- Channels: 244 of `MAX_PACKETIZER_CHANNELS` = 256 (`TlmPacketizerCfg.hpp:21`; packets **and** the omit block, one `RedBlackTreeMap` since F´ 4.3.0; `TLMPACKETIZER_HASH_BUCKETS` no longer exists). A8 adds ≥ 14 channels, so **A8's constants row raises it to at least 288**; `check_packet_set.py` reads the constant from the header and warns at 90 %.
- Target after Cycle F (measured, `feat/upstream-sync` @ f26d9e1d): FLASH 725984 B (69.53 % of the 1 MB slot), RAM 331376 B (62.23 %) of the RP2350's 520 KB. (Cycle D baseline was 715104 B / 342976 B.)
- `FW_COM_BUFFER_MAX_SIZE` = **227** (`P/project/config/FpConstants.fpp:22`; was 233: the sync tied it to `TmPayloadCapacity(240) − SppOverhead(13)`): the largest telemetry or event packet the recorder must hold per slot.
- `Os::Directory` for Zephyr was rewritten in fprime-zephyr b14101dd (`lib/fprime-zephyr/fprime-zephyr/Os/Directory.hpp`: `ZephyrDirectory : DirectoryInterface` with `open(path, OpenMode)`, `isOpen`, `rewind`, `read(fileNameBuffer, buffSize)` → `NO_MORE_FILES`, `close`, over `fs_dir_t`; `Directory.cpp` changed 85 lines against the pre-sync 31399714). `Os::FileSystem::removeFile`, `rename`, `exists`, `getFreeSpace` are unchanged in role. Host fakes exist for `File`, `FileSystem` and `Mutex` only (`P/test/unit-tests/support/Os/`); a `Directory` fake against the b14101dd interface is new work in A8-2.
- Dictionary keys (F´ 4.3.0 format, `dictionarySpecVersion` 1.0.0): opcodes are `commands[].opcode`, parameters `parameters[]`, and the packet set is `telemetryPacketSets[0].members` / `.omitted` (there is no `packets` key). `scripts/check_capacity.py --dictionary <path>` reads `commands` and `parameters`; channel counts come from `scripts/check_packet_set.py` parsing the `.fppi`. Anything A8 adds that reads the dictionary's packet set uses `members`/`omitted`.
