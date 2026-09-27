# 04 — Findings (verified facts, file:line) — 2026-09-27, tree 6296ef32 + frozen working set

§A holds facts established while planning (for the ledger merge after Cycles I-L commit). §B lists every claim in the brief
or the design found wrong or incomplete, with the decision this plan takes. §C lists follow-ups outside the cycle.

## A. Verified facts
1. 1 Hz group: `check_capacity.py --dictionary none` → `rateGroup1Hz: 18/25 used, 7 free (max index 20)`; `T/topology.fpp:330`
   `RateGroupMemberOut[20] -> faultManager.run`, comment `:329` says slot 19 is free; `AcConstants.fpp:7` = 25.
2. Limits: `FpConstants.fpp:22` `FW_COM_BUFFER_MAX_SIZE = 227`; `TlmPacketizerCfg.hpp:19` 24 packets, `:21-22` 256 channels;
   `CommandDispatcherImplCfg.hpp:16` 512. Capacity today: 23/24 packets, 244/256 channels (174 + 70). `check_packet_set.py:61`
   and `check_capacity.py:75` warn at > 90 %, so 258/288 (89.6 %) is OK and 260/288 would WARN.
3. Splitters: `ComSplitter.fpp:7,10` (`sync comIn`, `comOut: [5]`); `ComSplitter.cpp:24-35` copies and calls every connected
   output in index order with context 0. Generated `ReferenceDeploymentTopologyAc.cpp:1309-1324` (build copy): LoRa = 0,
   UART = 1 on both splitters. The commented S-band lines `topology.fpp:156,161`, if revived unindexed, would take index 3.
4. Producers' threads: `TlmPacketizer` is active with `async input port Run` (`TlmPacketizer.fpp:3,37`) → `tlmIn` runs on
   tlmSend's thread. `EventManager.LogRecv` is `sync` (`EventManager.fpp:47`) but calls `loqQueue_internalInterfaceInvoke`
   (`EventManager.cpp:58`), handled on its own thread (`:68`) where `PktSend_out` is called (`:88`) → `evtIn` runs on
   EventManager's thread.
5. Packet wire layout: telemetry = descriptor U16 (`FW_PACKET_PACKETIZED_TLM = 4`, `project/config/ComCfg.fpp:40`) + packet id
   U16 + 11-byte time at a fixed offset + members at their maximum serialized size (`TlmPacketizer.cpp:69-70,104,381-385`);
   event = descriptor (`FW_PACKET_LOG = 2`, `ComCfg.fpp:38`) + id U32 + time + args without a length prefix (`LogPacket.cpp:19-36`).
   Dictionary aliases: `FwPacketDescriptorType` U16, `FwTlmPacketizeIdType` U16, `FwEventIdType` U32, `FwSizeType` U64,
   `FwSizeStoreType` U16, time base U16, context U8 (build-copy dictionary `typeDefinitions`).
6. Dictionary (build copy, `v1.2.0-82-ge1eced14-dirty`): 387 commands, 106 parameters, 244 channels, 697 events;
   `telemetryPacketSets[0]` has keys `name`, `members` (23 packet objects `{name,id,group,members}`), `omitted` (70 names).
7. Instances: highest base id `0x1007F000` (`instances.fpp:278`); subtopology bases `0x01000000` (CdhCore), `0x02000000`,
   `0x21/22/23000000` (ComCcsds), `0x05000000` (FileHandling), `0xA0000000` (Update) — `0x10080000` is free.
8. `FileSystem` packet `ReferenceDeploymentPackets.fppi:205-208`; `fsSpace` channels are `FwSizeType` = U64 (`FsSpace.fpp:10,13`);
   FsSpace calls `getFreeSpace("/prmDb.dat", ...)` (`FsSpace.cpp:30`). The fstab mount point is `/` (`v5.dtsi:18-25`).
9. PersistedRecord: `crc32` `PersistedRecordCodec.hpp:69` (reflected 0xEDB88320, table-free); `FORMAT_VERSION = 1` is a codec
   constant (`:29`), payload ≤ 64 B (`:42`); `load` `PersistedRecordFile.hpp:42`, `store` `:59`; lib `DEPENDS Os`
   (`PersistedRecord/CMakeLists.txt`). TelemetryGate pattern: `TelemetryGate.cpp:13-33` (paths, magic in anonymous namespace).
10. Zephyr `Os::Directory` (`fprime-zephyr/Os/Directory.cpp`): `open` with `CREATE_*` does one `fs_mkdir` (no parents)
    (`:21-45`); `rewind` returns `NOT_SUPPORTED` (`:47-49`); `read` returns the entry name (files and sub-directories alike)
    or `NO_MORE_FILES` (`:51-67`). Generic `Directory::getFileCount` and `readDirectory` rewind first (`lib/fprime/Os/Directory.cpp`),
    so both fail on Zephyr.
11. Zephyr `Os::File` modes (`fprime-zephyr/Os/File.cpp:58-71`): `OPEN_WRITE` = create + write *without* truncate or append
    (writes at offset 0), `OPEN_CREATE` always truncates (the `OverwriteType` ternary yields 0 either way, `:68`), `OPEN_APPEND`
    = create + write + append. FatFs open-file slots `FS_FATFS_NUM_FILES` default 4 and dirs 4
    (`zephyr/subsys/fs/Kconfig.fatfs:72-90`), not overridden in `prj.conf`.
12. `Os::FileSystem` API (`lib/fprime/Os/FileSystem.hpp`): `removeFile :225`, `getFreeSpace :247`, `exists :280`,
    `createDirectory(path, errorIfAlreadyExists = false) :308`, `getFileSize :355`.
13. `Svc.SendFileRequest(sourceFileName: string size 100, destFileName, offset: U32, length: U32) -> Svc.SendFileResponse`
    (`FileDownlinkPorts.fpp:12-28`); `fileDownlink.SendFile` is `guarded` and non-blocking (queue send `NONBLOCKING`,
    `FileDownlink.cpp:107-137`); exported as `FileHandling.fileDownlinkSendFile` (`FileHandling.fpp:62`); not connected today.
14. Host fakes: `support/Os/File.hpp` supports only `OPEN_READ`/`OPEN_CREATE` (others → `INVALID_MODE`), `FileSystem.hpp` only
    `rename`/`exists`, `Mutex.hpp` is a no-op depth counter; there is no `Fw::ComBuffer` stub and `support/config/FppConstantsAc.hpp`
    defines only `ComCfg::SpacecraftId`. Host binaries today: 30.
15. `req.py`: component group = directory name (`scripts/req.py:146-160`); `add` needs an existing group with ≥ 1 row (`:271-278`,
    `:125-126`); levels `unit, board, subsystem, flatsat, environmental` (`:62`). `generate_rtm.py` links `scripts/tests` markers
    via `--script-junit` (`verify.sh:168`).
16. Current requirement rows: DH-L2-03 is `Inspection / Unit`; DH-L2-04 `Unit Test / Unit`; DH-L2-05/08/12, CDH-16 Board;
    DH-L2-13's criterion is the BootCount cadence (unrelated to stored data); DH-L2-09/10 criteria name comQueue only.
17. Last target build (Cycle J review `cycle-j-review.md:66-67`): FLASH 726656 B / 1044144 B (69.59 %), RAM 331376 B / 532480 B (62.23 %).
18. There is no port that provides the boot count (`StartupManager.fpp:5-35` exposes none); `NUM_TASKS` is 5/5, so the recorder
    cannot sit behind `taskGate` without widening `SchedTask`.

## B. Claims found wrong or incomplete (and what this plan does)
1. **Design §4 "segment header (16 B)"**: the listed fields (4+1+1+2+4+4+4) are 20 bytes. → 20-byte header (01 §5.2).
2. **Design §5 "record version bumped so a two-stream record loads as BAD_VERSION"**: impossible; the PersistedRecord version
   is the codec's fixed `FORMAT_VERSION` (A.9). → a `layout` byte and `streamCount` in the payload (01 §5.5); A9-2 bumps `layout`.
3. **Design §7.2 per-pass sequence "SendFile of the newest segment"**: a `.seq` file holds literal arguments and cannot name a
   segment whose number is only known on board. → 9th command `DOWNLINK_NEWEST` + `sendFileOut` port (01 §3, §11). This is also
   what makes the budget's "+9 commands" come out (the design lists eight).
4. **Design §8 "taskGate can disable its 1 Hz tick"**: not without a new `SchedTask` (A.18). → wired directly; non-goal.
5. **Design header "boot count u16"**: no source (A.18). → `0xFFFF`, reserved.
6. **04-plan budget "segment write staging 0 (ring bytes are written in place)"**: records need their CRC appended and one
   contiguous write, so a stage is required. → `STAGE_BYTES` 4096 (+4 KB RAM).
7. **04-plan budget "+7 events"**: the design names six; this plan has eight (adds `DirectoryScanFailed`, `ConfigApplied`,
   the latter so DH-L2-03's "reported" value is observable).
8. **Design §3 "CAPACITY validated against TotalSpace at boot"**: a boot has no command to reject. → validated on `SET_CAPACITY`
   only; load checks the lower bound only.
9. **Design §3 "close it and open the next one" / §4 per-boot files**: holding segments open would take 2 of 4 FatFs file
   slots permanently (A.11). → open-append-close within the tick.
10. **Brief/design "`Os::Directory` has `rewind`"**: present in the interface but `NOT_SUPPORTED` on Zephyr (A.10). → never used.
11. **04-plan harm table "memcpy of ≤ 233 B"** and `cycle-sequencing-E-A8-A9.md:38` "233 B": stale; 227 (A.2).
12. **`cycle-sequencing-E-A8-A9.md:27` "`dataRecorder.run`"**: the design's port name is `schedIn`; this plan uses `schedIn`.
13. **Brief "`MAX_PACKETIZER_CHANNELS = 256` (`:21`)"**: the declaration starts at `:21`, the value is on `:22`.
14. **Brief: baseline status is 39 lines**: by the end of planning the tree had 45 other lines (plus this plan): `Makefile`,
    `HP-11-...md`, `hardware-procedures/README.md` modified and `scripts/make_bench_sequence.py`,
    `scripts/tests/test_make_bench_sequence.py`, `sequences/bench_startup.seq` untracked — another cycle is live in the tree.
    A8-5 and A8-6 touch two of those files (README §Hazards).
15. **Brief "A8-0 design half done on `main`"**: the design edits are uncommitted working-tree changes; A8-0 commits them.
16. **Commit plan rule 4 (constants in their own commit) vs brief (raise in the same commit as the channels)**: this plan follows
    the brief, so one `git revert` of A8-3 restores both the limit and the channel set together.
17. `P/Components/CMakeLists.txt` is only mostly alphabetical (`ProvesRouter` sits after `AntennaDeployer`); DataRecorder still
    goes after `ComDelay/`.
18. **DH-L2-03 method**: the brief lists it as Inspection/Unit, which is today's row; this plan changes it to Unit Test (02 §3).

## C. Follow-ups (not this cycle)
- DH-L2-01/CDH-8 clause "a segment written before a pass is downlinkable during it", DH-L2-11 (segment CRC + HP-12), DH-L2-13
  and CDH-27 (per-pass sequence), CDH-14 (recorder channels), FD-L2-04 retention, DH-L2-09/10 (drop-oldest ring, delete-oldest
  disk): criteria updates via `req.py` in a later docs cycle, once the board run exists.
- `hardware-procedures/README.md` and ledger entries for this cycle (after Cycles I-L commit).
- A boot-count port on StartupManager, if the header's reserved field should carry it.
