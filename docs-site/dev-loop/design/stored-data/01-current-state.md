# 01: Current state: what happens to data that is not downlinked now

Verified against the working tree at cab7439. Line numbers drift; grep the symbol before trusting `:NN`.

## Storage medium

- One SD card on SPI0 (`boards/bronco_space/proves_flight_control_board_v5/proves_flight_control_board_v5.dtsi:135-150`, node `sdhc0`, disk name `SD`, 24 MHz). The LoRa radio is on SPI1 (`:153-160`), so SD traffic does not share a bus with the radio.
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
| Channelized telemetry | `tlmSend` is `Svc.TlmPacketizer` (project override `P/project/config/CdhCoreTlmConfig.fpp`), 22 packets (`Top/ReferenceDeploymentPackets.fppi`, at `MAX_PACKETIZER_PACKETS`), run every ~30 s via `telemetryDelay` ÷29 and gated by `TelemetryGate` (`topology.fpp:182`) | Latest value only; TELEMETRY queue depth 1; default packet level 1 sends only `Beacon` | Everything above level 1, and everything produced between passes, is gone. When `TelemetryGate` is DISABLED the run tick is withheld, so no packets are even produced |
| Events (incl. FaultManager, Health, QueueOverflow) | `Svc.EventManager`, time-tagged at emit (`EventManager.cpp:111-114`) | 50 deep in RAM per link; nothing on disk; lost on reset | CDH-16, DH-L2-12, FD-L2-04 retention clause unmet |
| Critical state | ModeManager, Authenticate, StartupManager, TelemetryGate | `Components/PersistedRecord`: magic + version + length + payload (≤ 64 B) + CRC-32, temp + flush + rename, temp fallback on load | Done; DH-L2-11/14 largely covered |
| Parameters | `Svc.PrmDb` on `/prmDb.dat` (`FileHandling.fpp:36`) | RAM until `PRM_SAVE_FILE`; no integrity check; `PRMDB_NUM_DB_ENTRIES` is the F´ default 25 (no project override under `P/project/config/`) against 98 parameters in the dictionary | Not suitable for recorder configuration (see 02 §5) |
| Files (camera, uplinked files) | `Svc.FileDownlink` (`P/project/config/FileHandlingConfig.fpp:29-31`: 1 s cycle, 1 s cooldown, queue depth 3) | Stored on SD, sent by `SendFile`, byte-exact (DH-L2-07 criterion) | Works; this is the retrieval path the design reuses |
| Firmware images | `Components/FlashWorker` | Update only | Out of scope |

## Headroom that the design consumes

- 1 Hz rate group: 20 of 25 member slots used (`topology.fpp:271-295`, `P/project/config/AcConstants.fpp:7`).
- Command dispatch table: 400 (`CommandDispatcherImplCfg.hpp:14`) against 361 opcodes.
- Packetizer: 22 of 22 packets; a new packet needs `TlmPacketizerCfg.hpp:19` raised (ledger line 185).
- Target after Cycle D: FLASH 715104 B (68.49 %), RAM 342976 B (64.41 %) of the RP2350's 520 KB.
- `FW_COM_BUFFER_MAX_SIZE` = 233 (`P/project/config/FpConstants.fpp:20`): the largest telemetry or event packet the recorder must hold per slot.
- `Os::Directory`, `Os::FileSystem::removeFile`, `rename`, `exists`, `getFreeSpace` are implemented for Zephyr (`lib/fprime-zephyr/fprime-zephyr/Os/`). Host fakes exist for `File` and `FileSystem` only (`P/test/unit-tests/support/Os/`); a `Directory` fake is new work.
