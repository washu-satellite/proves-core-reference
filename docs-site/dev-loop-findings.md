# Dev-loop findings ledger

Verified facts about this codebase discovered while planning, coding, or verifying,
kept so no agent re-derives them. One line per fact, with the file:line it was
verified at and the date. Pointers, not prose. Facts that turn out wrong are
corrected here in the same turn (see CLAUDE.md rules). Traps that must never be
re-tripped are promoted to CLAUDE.md §Traps; the rest stay here.

Maintenance: every planner/coder/reviewer brief requires a "Findings" section;
the orchestrator merges it here at the end of each cycle. Delete a line when the
code it describes changes and the fact no longer holds.

## Persistence and OS layer (2026-09-04/05)
- Zephyr `Os::File::open` returns OTHER_ERROR for every failure incl. missing file; host fake returns DOESNT_EXIST. `lib/fprime-zephyr/fprime-zephyr/Os/File.cpp` ~77-81. Use `Os::FileSystem::exists()`.
- FAT rename unlinks the destination before renaming; not atomic. `lib/zephyr-workspace/zephyr/subsys/fs/fat_fs.c` `fatfs_rename`.
- `ZephyrFile::write(WAIT)` calls flush but discards its status. `lib/fprime-zephyr/fprime-zephyr/Os/File.cpp` ~252-254.
- `Os::FileSystem::rename/exists` exist in F´ 4.1.1 and are implemented for Zephyr. `lib/fprime/Os/FileSystem.hpp:237,280`; `lib/fprime-zephyr/fprime-zephyr/Os/FileSystem.cpp:36,96`.
- ModeManager persists a raw struct via reinterpret_cast (sizeof = 12 bytes with padding, not 7); validation is size + mode range; corrupt boots NORMAL today (MM0012 says SAFE). `Components/ModeManager/ModeManager.cpp` ~247-342; loadState calls saveState every boot to clear the clean flag.
- ModeManager `!= DOESNT_EXIST` first-boot guard is unreachable on Zephyr (issue #1). `ModeManager.cpp` ~319.
- Authenticate sequence-number path is `"//sequence_number.txt"` (double slash), no validation, non-OK read writes 0 back. `Components/Authenticate/Authenticate.cpp` ~21, 60-67, 108-112. Both Authenticate instances (LoRa, UART) write the same file.
- StartupManager `read<T>/write<T>` use `Fw::ExternalSerializeBuffer`; read failure on first boot is silent by design; quiescence file rejects useconds >= 1e6 (boot-loop fix eb2df70). `Components/StartupManager/StartupManager.cpp` ~39-149.
- CORRECTED 2026-09-05: the real `Svc.PrmDb` is wired as `FileHandling.prmDb` on `/prmDb.dat` (`ReferenceDeploymentTopology.cpp:69`) and read at boot (`lib/fprime/Svc/Subtopologies/FileHandling/FileHandling.fpp:39`); `PRM_SET` is RAM-only until `PRM_SAVE_FILE`, then persists. `Components/NullPrmDb` is compiled but not instantiated. `PRMDB_NUM_DB_ENTRIES` default 25 (`lib/fprime/Svc/PrmDb/PrmDbImpl.hpp:118`) bounds saved params; dictionary at 96a0ed7 has 339 commands / 89 params; `CMD_DISPATCHER_DISPATCH_TABLE_SIZE` = 350 (`project/config/CommandDispatcherImplCfg.hpp:14`).
- `tlmSend` is `Svc.TlmPacketizer` (project config override), not TlmChan as the TelemetryGate sdd says; packets are sent on change, which is why board telemetry-interval tests must lower `telemetryDelay.DIVIDER`.

## Comms and command path (2026-09-04/05)
- Command-loss timer lives only in the LoRa AuthenticationRouter; param is `COMM_LOSS_TIME` (default 259200 s), and `COMM_LOSS_TIME_START_FILE` is never used. UART/S-band routers never time out. `Components/AuthenticationRouter/AuthenticationRouter.fpp` ~94-97; `topology.fpp:466`.
- `reset_watchdog` STOPS petting (→ reboot ≈26 s), it does not kick. `Components/Watchdog/Watchdog.cpp` ~48,71.
- `CMD_NO_OP` and `GET_SEQ_NUM` bypass authentication. `Components/Authenticate/Authenticate.cpp` ~30-36.
- Authenticate HMAC is truncated to 16 bytes in code; AUTH05 sdd says 64 bits. `Authenticate.cpp` ~136-166.
- `SequenceNumberOutOfWindow` event is `throttle 2`. `Authenticate.fpp`.
- AuthenticationRouter channels `PassedRouter/FailedRouter/ByPassedRouter` are declared but never written; Authenticate `PacketTooShort` event never emitted.
- The GDS framer re-reads `Framing/src/sequence_number.bin` on every frame. `Framing/.../authenticate_plugin.py` ~129-142.
- comQueue depths: events 50, tlm 1, file 1; priority order EVENTS(0) < FILE(1) < TLM(2). `ComCcsdsLora/ComCcsdsConfig.fpp` ~31-33.
- Events and file downlink bypass TelemetryGate (only the TlmChan tick is gated). `topology.fpp:146-148,177-179,284`.

## Telemetry (2026-09-05)
- **Latent boot assert (found 2026-09-18, Cycle E review):** `TLMPACKETIZER_HASH_BUCKETS = 202` (`project/config/TlmPacketizerCfg.hpp:27`) but `ReferenceDeploymentPackets.fppi` names 214 distinct channels at cab7439 (201 before Cycle D's 13 `faultManager` channels). `Svc::TlmPacketizer::findBucket` (`lib/fprime/Svc/TlmPacketizer/TlmPacketizer.cpp`) asserts `free < BUCKETS` while populating both the packet lists and the ignore list; asserts are compiled in (`FpConfig.h:111`). The Cycle D image has not been flashed; it would assert in `setPacketList` at boot. Fixed by Cycle E row E1 (buckets → 256) with `scripts/check_packet_set.py` in `verify.sh` as the regression guard. The three hash constants are literals, not derived from `MAX_PACKETIZER_PACKETS`.
- Telemetry is `Svc.TlmPacketizer` at default packet level 1: only the Beacon packet downlinks until `CdhCore.tlmSend.SET_LEVEL` is raised. Every telemetry test must raise and restore the level.
- `WatchdogTransitions` and `CurrBuffs` are omitted channels in `ReferenceDeploymentPackets.fppi`.
- `telemetryDelay` (Utilities.RateDelay) divides the 1 Hz tick by 29 → TlmChan runs ~every 30 s. `instances.fpp` ~218; `test/int/telemetry_gate_test.py:37-44`.
- Every new `telemetry` channel must be added to `ReferenceDeploymentPackets.fppi` or `fpp-to-dict` fails the build (issue #5).
- `startup.seq` lines 1-5 ID-filter `QueueOverflow` and `RateGroupCycleSlip` events at boot, hiding them from the ground.

## Modes, faults, hardware (2026-09-05)
- Only SAFE_MODE and NORMAL exist; STANDBY/CALIBRATION/EXPERIMENT behaviours are "to be determined by Mission Operations" (CDR slide 16). MS-L2-01 is Not Ready until then.
- `loadSwitchTurnOn` is unwired, so faces stay OFF after EXIT_SAFE_MODE (MM0006 false today). Check `topology.fpp` before relying on MM0006.
- `EXIT_SAFE_MODE` returns OK from NORMAL. `ModeManager.cpp` ~226-230 vs fpp comment ~81. `safeModeReason` byte restored unvalidated, ~266.
- There is no on-board event log; CDH-16, DH-L2-12, FD-L2-04 cannot pass until one exists.
- ThermalManager wires 5 face + 4 battery + pico sensors (fpp comment says 11). `Components/ThermalManager/ThermalManager.cpp` ~25-48; thresholds `ThermalManager.fpp` ~7-16, hysteresis 3 C `.hpp:27`.
- Safe-mode entry 6.7 V / recovery 8.0 V / debounce 10 samples. `ModeManager.fpp` ~194-200.
- Board `proves_flight_control_board_v5e/rp2350a/m33` (settings.ini); flash 65.7% / RAM 64.1% at 96a0ed7.

## Build, test, CI (2026-09-04/05)
- Unit tests live in `PROVESFlightControllerReference/test/unit-tests/` (not `Components/*/test/ut`); fakes in `support/`; tests auto-globbed; helper libs listed explicitly in its CMakeLists.
- `-DBUILD_TESTING=ON` is not consumed by the unit-test CMake project (harmless).
- This fork has zero self-hosted runners: CI `build`/`integration-*` jobs queue 24 h then cancel. Only lint/unit-test/yamcs-build are real on the fork.
- Zephyr cannot configure at a path containing an apostrophe (`yaml.cmake` single-quoted Python literal) or spaces; firmware builds run from the clean-path copy `~/scalar-build/proves-core-reference` (CLAUDE.md).
- Pre-commit hooks run from the interpreter recorded in `.git/hooks/pre-commit` (`INSTALL_PYTHON`); `uvx` is not installed. Codespell flags the plural of "static" as a misspelling (write "static objects"); cpplint rejects unknown NOLINT categories.
- `ruff` 0.14.2 is in the venv; `pyflakes` and `cpplint` are not on PATH (cpplint runs via the hook).
- iCloud leaves duplicate files named `<name> 2` in the venv `bin/`; ignore them.

## Cycle A planner findings, merged 2026-09-05 (ModeManager / Authenticate / StartupManager persistence)
- Three Authenticate instances declared (`ComCcsdsLora/ComCcsds.fpp:101`, `ComCcsdsUart/ComCcsds.fpp:113`, `ComCcsdsSband/ComCcsds.fpp:101`); S-band is commented out of `topology.fpp:22`, so two are built and both write the same sequence file.
- `ModeManager::init` calls `loadState()` (`ModeManager.cpp:37-40`); `saveState()` callers: `:341` (end of loadState, clears clean flag), `:425` enterSafeMode, `:449` exitSafeMode, `:474` exitSafeModeAutomatic; `prepareForReboot_handler` `:168-203`.
- `StatePersistenceFailure(operation: string 20, status: I32)` `ModeManager.fpp:117-123`; op strings today: load-corrupt, load-read, load-open, save-open, save-write, shutdown-open, shutdown-write.
- ModeManager restores `safeModeReason`/`cleanShutdown` without range checks (`ModeManager.cpp:266,271`).
- ModeManager host recorder stub records `StatePersistenceFailure{op,status}` and `UnintendedRebootDetected`; its `init()` is a no-op (`test/unit-tests/support/.../ModeManager/ModeManagerComponentAc.hpp:72,86-89,147,151,232`).
- Authenticate includes `<psa/crypto.h>` (`Authenticate.cpp:9`) and `AuthDefaultKey.h` (`:16`): the component cannot be host-compiled; persistence logic must live in an F´-free helper to be unit-tested.
- Authenticate file I/O: `readSequenceNumber` `:60-67`, `writeSequenceNumber` `:108-112`; callers `:53` init, `:373` accepted packet, `:386` GET_SEQ_NUM (re-reads file per command), `:403` SET_SEQ_NUM (writes file only; RAM counter untouched — latent bug).
- `FileOpenError(error: U32, filename: string 64)` WARNING_HI throttle 2, declared `Authenticate.fpp:36`, never emitted before Cycle A; sdd Events row had a stale one-arg signature.
- `FileHelper` (`lib/fprime-extras/.../FileHelper.hpp:206-224`) serializes big-endian and maps short reads to BAD_SIZE; only Authenticate used it.
- StartupManager includes `<zephyr/drivers/rtc.h>` (`:10`) and calls `k_uptime_seconds()` (`:152`); host tests need a fake header. `get_boot_count` `:104-123` rewrote the file even for `increment=false` (GET_BOOT_COUNT `:228-232`) before Cycle A.
- `FW_MAX` is `lib/fprime/Fw/Types/BasicTypes.h:91`; `Fw::Time::SERIALIZED_SIZE` = 11 (`lib/fprime/Fw/Time/Time.hpp:16`); `TimeBase` values in `Fw/Time/Time.fpp`.
- Host `Os::File` fake: OPEN_READ fails only with DOESNT_EXIST, so OPEN_ERROR/READ_ERROR load paths are untestable on host (`test/unit-tests/support/Os/File.hpp:73-93`).
- `PersistedRecord::load` returns MISSING (not a corruption status) when the target is absent and the temp is not fully valid (`PersistedRecordFile.cpp:63-73`); an empty present file decodes TRUNCATED.
- `loadSwitchTurnOn[0..7]` connections are commented out in `topology.fpp:474-481`; only `loadSwitchTurnOff` is wired (`:483-490`). Issue #7.
- FatFS treats `//x` and `/x` as the same entry; project convention is a single leading slash.

## Cycle C planner findings, merged 2026-09-05 (uplink framing, scheduler, queues)
- `CcsdsTcFrameDetector::detect` verifies the TC FECF itself; a CRC-failing frame is NO_FRAME_DETECTED and FrameAccumulator rotates 1 byte silently — `lib/fprime/Svc/FrameAccumulator/FrameDetector/CcsdsTcFrameDetector.cpp:55-81`, `lib/fprime/Svc/FrameAccumulator/FrameAccumulator.cpp:170-177`. Contradicts the CDR/brief premise "deframer reports a CRC mismatch": `TcDeframer::InvalidCrc` (`lib/fprime/Svc/Ccsds/TcDeframer/TcDeframer.cpp:91-104`) is unreachable behind the accumulator.
- Detector header token = `(1 << TCSubfields::BypassFlagOffset(13)) | ComCfg::SpacecraftId(0x0044)` = 0x2044 — `CcsdsTcFrameDetector.hpp:45-46`, `lib/fprime/Svc/Ccsds/Types/Types.fpp:55-67`, `P/project/config/ComCfg.fpp:12`. Frame length field is bytes-1, mask 0x03FF — `TcDeframer.cpp:61`.
- TC CRC = CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, MSB-first, xorout 0), "123456789" → 0x29B1 — `lib/fprime/Svc/Ccsds/Utils/CRC16.hpp:16,43-49`, `lib/fprime/Utils/Hash/libcrc/lib_crc.c:132-144`.
- CRC-CCITT = (x+1)(x^15+x+1): single-bit syndromes unique, non-zero and disjoint from the 16 FECF-bit syndromes for 7/252/1024-byte frames; no 2-bit error maps to a single-bit syndrome (24-byte frame, all 18336 pairs); syndrome of the last data bit is 0x1021 and shifts by the 0x1021 LFSR per earlier bit; CRC is affine (crc(a^b)=crc(a)^crc(b)^crc(0)) — verified with venv python 2026-09-05.
- LoRa RX delivers exactly one radio packet per `Fw::Buffer` (payload after a 4-byte all-zero header), allocated from `commsBufferManager` and freed on `dataReturnIn` — `lib/fprime-zephyr/fprime-zephyr/Drv/LoRa/LoRa.cpp:158-177`, `P/project/config/LoRaCfg.hpp:11`; `MAX_PACKET_SIZE = 252` — `LoRa.hpp:19`.
- LoRa PHY CRC is OFF: `Radio.SetRxConfig(..., crcOn=false, ...)` — `lib/zephyr-workspace/zephyr/drivers/lora/loramac_node/sx12xx_common.c:357-360`; corrupted packets reach F´.
- LoRa RX callback runs on the Zephyr system workqueue (DIO1 `k_work`) — `sx12xx_common.c:100-113`, `loramac_node/sx126x.c:412-444`; everything from `lora.dataOut` to `cmdDisp`'s async queue executes there.
- Uplink ownership chain: `lora.dataOut → frameAccumulator.dataIn` (returns immediately via `dataReturnOut`, `FrameAccumulator.cpp:56-64`) → accumulator allocates its own frame buffer (`:142`) → `tcDeframer` (shrinks in place `:107-109`, returns on error) → `authenticatelora` → `spacePacketDeframer` → `authenticationRouter`; return ports chain back one hop each — `P/ComCcsdsLora/ComCcsds.fpp:170-186`, `Top/topology.fpp:203-204`.
- `Svc.Deframer` interface = guarded `dataIn`, output `dataOut`/`dataReturnOut`, sync `dataReturnIn` (all `Svc.ComDataWithContext`) — `lib/fprime/Svc/Interfaces/Deframer.fpp:1-15`; `Authenticate.fpp:51-61` declares the same four explicitly.
- UART uplink arrives as arbitrary chunks via `comDriver.$recv → comStub → frameAccumulator` at 10 Hz — `topology.fpp:238-239,256`, `P/ComCcsdsUart/ComCcsds.fpp:211-212`; no pre-accumulator frame boundary.
- ComQueue drop policy: `enqueue` NO_ROOM_LEFT drops the *new* message, logs `QueueOverflow` once per queue while `m_throttle` is set, throttle cleared when that queue next sends — `lib/fprime/Svc/ComQueue/ComQueue.cpp:246-272,369`; buffer-port async overflow returns the buffer — `:237-240`; queues sorted by priority in `configure` `:64-90`; `processQueue` sends first non-empty in priority order, round-robins equal priorities — `:333-386`.
- ComQueue is not host-buildable: `ComQueue.hpp:10-16` includes `Fw/Buffer`, `Fw/Com/ComBuffer`, `ComQueueComponentAc`, `Utils/Types/Queue`, `Os/Mutex`.
- Thread priorities: `CdhCoreConfig.fpp:19-24` cmdDisp 4 / health 5 / events 6 / tlmSend 6; `ComCcsdsConfig.fpp:18-21` aggregator 7 / comQueue 8; `instances.fpp:31-69` rate groups 1/2/3, modeManager 4, cmdSeq 12, payloadSeq/safeModeSeq/payload 13. Zephyr `Os::Task` passes the number straight to `k_thread_create`, capped at 14 (lower = higher) — `lib/fprime-zephyr/fprime-zephyr/Os/Task.cpp:36-48`.
- `Svc.ActiveRateGroup` has no per-member enable (`RateGroupMemberOut: [N] Sched` only) — `lib/fprime/Svc/ActiveRateGroup/ActiveRateGroup.fpp:15`. 1 Hz members and indices — `topology.fpp:271-290`; only `imuManager/fsSpace/powerMonitor/adcs/thermalManager` (idx 6/10/15/17/18) are stateless sensor polls (`ImuManager.cpp:43-55`, `FsSpace.cpp:27-35`, `PowerMonitor.cpp:28-38`, `ADCS.cpp:24-31`, `ThermalManager.cpp:24-`).
- `Watchdog::run_handler` pets only while `m_run`; `STOP_WATCHDOG` also fires `prepareForReboot` — `Watchdog.cpp:25-35,69-75`.
- `TlmPacketizer` FW_ASSERTs each packet ≤ `FW_COM_BUFFER_MAX_SIZE` at init (boot crash if exceeded) — `lib/fprime/Svc/TlmPacketizer/TlmPacketizer.cpp:88`; `HealthAuxiliary` (id 4) has 3 channels — `ReferenceDeploymentPackets.fppi:149-154`; `Health` (id 2) has 15 — `:119-135`.
- Enum constants are legal port indices in topology connections — `topology.fpp:147,152`.
- Highest instance base id in use is `picoTempManager` 0x10079000 — `instances.fpp:245`; ComCcsdsLora subtopology uses `BASE_ID_LORA + 0x0B000` last — `ComCcsds.fpp:112`.
- Host recorder stubs: `UT/support/PROVESFlightControllerReference/Components/{ModeManager,TelemetryGate,ThermalManager}/*ComponentAc.hpp`; `FpTypesStub.hpp` provides `CmdResponse/Success/ParamValid`; `Fw/Types/Assert.hpp` stub keeps only the condition; `Os/File.hpp`, `Os/FileSystem.hpp` fakes. `UT/CMakeLists.txt:13-118` lists helper libs explicitly and globs `test_*.cpp`.
- `req.py` refuses Pass/Fail without criteria (`scripts/req.py:206-213`) but accepts any other `--status` text; RTM shows `📋 <status>` only when no test is linked (`scripts/generate_rtm.py:250-253`); int tests are `⏸ deferred` under `--env host` (`:218-226`).
- Docs plumbing for a new component: `mkdocs.yml:66-93` nav, `Makefile:83-…` `docs-sync` cp list, `docs-site/components/<Name>.md` copies; `P/ComCcsdsLora/docs/sdd.md` is a 4-line stub with no headings/tables.
- `Framing/src/authenticate_plugin.py:129-167` adds the auth header/trailer only; LoRa packetisation is done by the CircuitPython passthrough board (`test/int/lora_passthrough_test.py:4-6`), not in this repo — one-frame-per-packet is an assumption.
- TlmPacketizer FW_ASSERTs each packet <= FW_COM_BUFFER_MAX_SIZE at init (boot crash if exceeded): lib/fprime/Svc/TlmPacketizer/TlmPacketizer.cpp:87. Health packet has 13 small channels at 96a0ed7, far below the bound.

## Cycle B planner findings, merged 2026-09-05 (parameters, telemetry sources)
- `dev-loop-findings.md` line "NullPrmDb does not persist F´ parameters" is misleading: NullPrmDb is compiled (`Components/CMakeLists.txt`) but not instanced; the topology wires the real `Svc.PrmDb` (`topology.fpp:132` `param connections instance FileHandling.prmDb`, `ReferenceDeploymentTopology.cpp:69` `configure("/prmDb.dat")`, `:118-120` `readParameters(); loadParameters();`), and the dictionary has `FileHandling.prmDb.PRM_SAVE_FILE/PRM_LOAD_FILE/PRM_COMMIT_STAGED`. What is true: `X_PRM_SET` is RAM-only until `PRM_SAVE` + `PRM_SAVE_FILE`; whether that file path works on the board is unverified.
- `telemetryDelay` is `Utilities.RateDelay` with `param DIVIDER: U8 default 29` and generated `DIVIDER_PRM_SET/_PRM_SAVE`; output every DIVIDER+1 ticks; INVALID/UNINIT falls back to 29; `DividerSet` event declared but never emitted. `lib/fprime-extras/FprimeExtras/Utilities/RateDelay/RateDelay.fpp:5,13,16`, `RateDelay.cpp:24-40`.
- `tlmSend` is `Svc.TlmPacketizer` via the project override `project/config/CdhCoreTlmConfig.fpp:4-16` (the lib default `lib/fprime/Svc/Subtopologies/CdhCore/CdhCoreConfig/CdhCoreTlmConfig.fpp:3` is TlmChan; TelemetryGate sdd/fpp comments say "TlmChan" — wrong name, same tick). `PACKET_UPDATE_MODE = PACKET_UPDATE_ON_CHANGE` (`project/config/TlmPacketizerCfg.hpp:40`); a packet is sent on Run only if a member channel updated and its level <= start level (`TlmPacketizer.cpp:341-342`); packet time = latest member write time (`:237,367`).
- `parameterUpdated(FwPrmIdType)` override exemplar: `Components/ComDelay/ComDelay.cpp:22-35`, `ComDelay.hpp:30`; ComDelay `DIVIDER` is U16 default 299 (`ComDelay.fpp:2,15`).
- Generated `X_PRM_SET` always acks OK; rejection with VALIDATION_ERROR requires a hand-written command (F´ codegen; confirmed by absence of any hook in ComDelay/ThermalManager param flow).
- 1 Hz sources and handlers: ThermalManager `run_handler` `ThermalManager.cpp:24-49`; ADCS `ADCS.cpp:24-31` (6 light ports, `ADCS.fpp:7`); PowerMonitor `PowerMonitor.cpp:28-45`, energy dt guard `dt_s < 10.0` at `:97,127`; ImuManager `ImuManager.cpp:43-58` (reads + ODR reconfigure check).
- Channel writers are the driver port handlers, not the managers: `Drv/Tmp112Manager/Tmp112Manager.cpp:78`, `Drv/Ina219Manager/Ina219Manager.cpp:45,63,81`, `Drv/Veml6031Manager/Veml6031Manager.cpp:79`, `Drv/PicoTempManager/PicoTempManager.cpp:35`, `ImuManager.cpp:81,108,159`. ThermalManager, ADCS declare no telemetry channels.
- DetumbleManager reads IMU ports at 50 Hz (`rateGroup50Hz` → `detumbleManager.run`, `topology.fpp:250`; `DetumbleManager.cpp:543,598`; wiring `topology.fpp:352-354`), so IMU channels update independently of `imuManager.run`.
- `PowerMonitor.fpp` has no param ports; `ADCS.fpp` has no command or param ports (`ADCS.fpp:12-26`).
- ImuManager is not host-buildable: `ImuManager.hpp:10-11` includes `<zephyr/device.h>`, `<zephyr/drivers/sensor.h>`.
- Host stubs present: `TelemetryGate`, `ThermalManager`, `ModeManager` only (`test/unit-tests/support/PROVESFlightControllerReference/Components/`); `FpTypesStub.hpp` has `Fw::CmdResponse{OK,INVALID_OPCODE,VALIDATION_ERROR,FORMAT_ERROR,EXECUTION_ERROR,BUSY}`, `Fw::Success`, `Fw::ParamValid{UNINIT,VALID,INVALID,DEFAULT}`, no `FwPrmIdType`, no `Fw::Time`.
- Library buffers are setup-once: `lib/fprime/Svc/BufferManager/BufferManagerComponentImpl.cpp:41-58` (`m_setup` guard), `lib/fprime/Svc/ComQueue/ComQueue.cpp:53` `configure()` from `configComponents` (`ComCcsdsLora/ComCcsds.fpp:6-31`); ComQueue channels report high-water marks, not capacity (`ComQueue.cpp:196-210`); BufferManager `TotalBuffs/CurrBuffs/HiBuffs/NoBuffs/EmptyBuffs` (`lib/fprime/Svc/BufferManager/Telemetry.fppi`). `payloadBufferManager` = 2 x 4 KB (`instances.fpp:134-154`); comms pools `project/config/ComCcsdsConfig.fpp:37-44`.
- No on-board telemetry store: dictionary `records`=0, `containers`=0; `FsSpace.fpp` only `FreeSpace/TotalSpace`; `FlashWorker.fpp` is firmware-update only; `PayloadCom.fpp` has no buffers/params.
- Command dispatch table: `CMD_DISPATCHER_DISPATCH_TABLE_SIZE = 350` (`project/config/CommandDispatcherImplCfg.hpp:14`); dictionary at 69e4b75 has 339 commands, 89 parameters.
- Packet ids in use: 1-8, 10-22 (`ReferenceDeploymentPackets.fppi`); `omit` block starts ~line 253; Beacon is the only level-1 packet.
- Board test helpers: `proves_send_and_assert_command(api, cmd, args, events, retries)` (`test/int/common.py:56`); markers `uart_only`, `verifies`, `sync_sequence_number`, `format_filesystem` (`$R/pytest.ini`); `_PRM_SET` usage exemplar `test/int/antenna_deployer_test.py:43-52`, `conftest.py:110`.
- `scripts/req.py add` requires `--group` = exact group name from `req.py list` (component sdd groups are the component names, e.g. `ThermalManager`, `PowerMonitor`, `ADCS`, `ImuManager`); ThermalManager/ADCS/ImuManager sdd Requirements tables are legacy 3-column (Name/Description/Validation) and get normalised on first tool edit; PowerMonitor already uses `PWR-MON-REQ-00x` ids.
- `docs-site/components/*.md` are `cp` copies of `Components/*/docs/sdd.md` (`$R/Makefile:84-95`); mkdocs nav lists ADCS/ImuManager/PowerMonitor/ThermalManager (`$R/mkdocs.yml:67,90-92`).
- `Components/RunInterval/` (planned) needs no CMake registration if header-only; project-root includes resolve via the F´ build's global include path (`TelemetryGate.cpp:9` includes `PersistedRecord` headers by project-root path; that lib is registered only because it has `.cpp` sources, `PersistedRecord/CMakeLists.txt`).

## Cycle A coder findings, merged 2026-09-05
- A `static constexpr U8 MAGIC[4]` class member passed by address to `PersistedRecord::load/store` is ODR-used and fails to link under the host build's C++14; keep magics in the `.cpp` anonymous namespace (`TelemetryGate.cpp:25` pattern).
- `StartupManager`'s constructor leaves `m_boot_count`, `m_waiting`, `m_stored_opcode`, `m_stored_sequence` uninitialized (`StartupManager.cpp:18`); flight relies on static storage zeroing. Issue #8.
- `log_WARNING_HI_FileOpenError` generated signature is `(U32, const Fw::StringBase&)`; `LogStringArg` derives from `StringBase`.
- `TimeBase` is a global-namespace generated class with `enum T` and `operator T()`; `FwTimeBaseStoreType = U16`, `FwTimeContextStoreType = U8` (`lib/fprime/default/config/FpConfig.fpp:79,92`, not overridden by the project) — 11-byte `Fw::Time` layout confirmed.
- `Authenticate.cpp` got `<cstring>`/`<cstdlib>` transitively via `FileHelper.hpp`; after removing FileHelper they must be included explicitly.
- `pre-commit run --all-files` visits tracked files only; untracked new sources are not formatted or linted by it (verify.sh now runs them via `--files`).
- clang-format hook reformats in place on the first run and reports Failed; a second run passes. Expect one failing gate run after any C++ edit unless files are formatted first.
- In the host fake FS, `files[PATH].clear()` makes a present zero-length file: `exists()` true, open OK, decode TRUNCATED — the "empty file is corruption, not first boot" case.
- Target build after Cycle A: FLASH 685824 B / 65.68 %, RAM 340080 B / 63.87 %.

## Cycle D planner findings, merged 2026-09-05 (fault paths, packetizer, rate groups)
- Health FATAL chain: `HLTH_PING_LATE` is FATAL after `FATAL` missed pings (`lib/fprime/Svc/Health/HealthComponentImpl.cpp:119-123`) →
- `watchdog.stop` already fans in from two outputs (`topology.fpp:297` router `reset_watchdog`, `:495` fatalHandler) — precedent for a third.
- Router command-loss action (`Components/AuthenticationRouter/AuthenticationRouter.cpp:44-51` `CallSafeMode`): `reset_watchdog_out` (guarded by
- ModeManager `run` is a sync port (`ModeManager.fpp:38`) → voltage debounce executes on the rateGroup1Hz thread; `forceSafeMode` is async (`:45`) →
- 1 Hz group: index 12 unused (`topology.fpp:271-290`); `ActiveRateGroupOutputPorts = 25` (`P/project/config/AcConstants.fpp:7`; lib default 10,
- Generated component bases expose protected `virtual void lock()/unLock()` and a private `Os::Mutex m_guardedPortMutex` (copy build,
- Packet id 9 is free (`ReferenceDeploymentPackets.fppi` uses 1-8, 10-22); "group" in a packet line is the SET_LEVEL level; `FW_COM_BUFFER_MAX_SIZE = 233`
- Precedents: `param ARMED: bool default true` (`StartupManager.fpp:49`); enum-returning port `GetSystemMode -> SystemMode` (`ModeManager.fpp:23`).
- `scripts/req.py` discovers component groups only from a heading matching `^##\s+Requirements\s*$` (`req.py:150`), group = component dir name
- Clean-path copy is at 93d28b5; its dictionary has 339 commands / 89 params / 194 channels / 662 events / 0 records / 0 containers.
- `Watchdog.hpp:12-14` includes `<atomic>` and `Fw/Types/OnEnumAc.hpp`; `Watchdog.cpp:9` includes `config/FpConfig.hpp`; no Zephyr includes → host-buildable
- ThermalManager/ModeManager recorder stubs have no `faultOut`; the ModeManager stub's `isConnected_*` flags (`ModeManagerComponentAc.hpp:158-176`) are the
- `test_ThermalManager_Thresholds.cpp:81,124,160,185` claims TM-L2-08/FD-L2-03; `test_ModeManager_VoltageDebounce.cpp:122` claims MM0009/MS-L2-08.
- Docs plumbing lines: `mkdocs.yml:72` (Watchdog nav entry), `Makefile:101` (Watchdog cp), `:124` (PersistedRecord cp); `docs-site/components/Watchdog.md` exists.
- `loraRetry` is `Svc.ComRetry` (`Top/instances.fpp:220`; wiring `topology.fpp:207-214`) — FD-L2-08 clause 1 is library behaviour already present.
- `Svc.Health` (`CdhCore.$health`) is a *queued* component drained by `Run` on rateGroup1Hz[2] (`CdhCore.fpp:18-33`, `topology.fpp:274`) — the
- New packet Faults id 9 (Cycle D) brings the packet count to 22 = MAX_PACKETIZER_PACKETS (project/config/TlmPacketizerCfg.hpp:19); any further packet needs the config raised.

## Cycle B coder findings, merged 2026-09-05
- PowerMonitor::updateGeneration never accumulates: updatePower stores m_lastUpdateTime_s before updateGeneration computes dt from it, so dt is always 0 and TotalPowerGenerated has always reported 0 (PowerMonitor.cpp ~142,163). Issue #9.
- Adding a port class (e.g. param get/set) to a component that lacked it needs a full fprime-util generate: the stale per-module fpp-to-cpp -i list lacks Fw/Prm/Prm.fpp and an incremental ninja run fails with 'symbol PrmGet is not defined'.
- -Wreorder: declare new component members last so declaration order matches the constructor initializer order; ARM warnings are visible in this project.
- fpp-to-dict can be run standalone from its ninja COMMAND line (~10 s) with --cmake-bin-dir redirected to $TMPDIR: cheap packet-set completeness and dictionary-count check without a Zephyr link.
- Dictionary after Cycle B: 347 commands, 93 params, 198 channels, 666 events.
- fprime_gds ChannelTemplate: get_full_name() returns comp.channel, get_name() the bare name; filter subhistories by get_full_name().
- Sandbox (2026-09-05): writes denied to .vscode/, .github/, .claude/, .gitmodules and ~/.cache/pre-commit; reads denied to every *.pem/*.key under $HOME, which breaks fprime-util (requests→certifi cacert.pem) and the Zephyr build (keys/proves.pem in ninja regen deps).

## Cycle C coder findings, merged 2026-09-05

- CRC-16/CCITT syndrome algebra, re-verified numerically in this cycle for frame lengths 7/16/24/64/252/1024: single-bit syndromes are unique and non-zero; the sixteen FECF-bit syndromes are exactly the powers of two and disjoint from every data-bit syndrome; over all 18336 two-bit patterns of a 24-byte frame none collides with a single-bit syndrome or with zero. The last data bit's syndrome is 0x1021, and each earlier bit's is `s = (s<<1) ^ (s & 0x8000 ? 0x1021 : 0)` — this is the same step as the CRC inner loop, so one helper serves both.
- An FPP `param` implies command ports: it autogenerates `<NAME>_PRM_SET` and `<NAME>_PRM_SAVE`, both of which appear in the dictionary's `commands` array. A component with one parameter and no explicit commands still needs `command reg/recv/resp` ports and still costs +2 commands. Cycle C's dictionary delta was therefore +4 commands, not the +2 the review predicted (`ReferenceDeployment.tcFrameCorrector.CORRECTION_ENABLED_PRM_SET`/`_PRM_SAVE` plus `taskGate.ENABLE_TASK`/`DISABLE_TASK`).
- `scripts/req.py` cannot seed an empty group: `parse_section_table` returns None when a `## Requirements` section holds no data rows (`scripts/req.py:123-127`), so `req.py add --group <New>` fails against a header-only table. A new component sdd must ship with one row already written before the tool can append the rest.
- cpplint rejects `member[getNum_<port>_InputPorts()]` as a variable-length array; the autocoded port-count accessor must be bound to a `static constexpr` named `k`+CamelCase (`kNumTasks`) and the array sized from that. Existing components escape this only because pre-commit lints changed files only.
- Enum-qualified port indices work in `topology.fpp` for a project enum, not just a subtopology one: `taskGate.schedIn[Components.SchedTask.IMU]` compiles and dictionary-generates.
- Host stubs for the two generated constant headers the frame detector reads are `support/Svc/Ccsds/Types/FppConstantsAc.hpp` and `support/config/FppConstantsAc.hpp`; the generated shape is `enum FppConstant_<Name> { <Name> = <value> };` inside the module namespace.
- Target after Cycle C: FLASH 699768 B (67.02 %), RAM 341968 B (64.22 %); dictionary 351 commands, 94 params, 202 channels, 670 events; `HealthAuxiliary` holds 7 channels.

## Loop tooling, 2026-09-05
- `.claude/agents/*.md` and `.claude/skills/*/SKILL.md` register at session start; an agent type added mid-session is not available to the Agent tool until the next session (fallback: general-purpose agent told to read the agent file first).
- `scripts/req.py` cannot seed an empty `## Requirements` table (`parse_section_table` returns None for a header-only table, `scripts/req.py:123-127`); hand-write one row, then `req.py add` the rest.

## Cycle D coder findings, 2026-09-05
- FPP reserved words bite formal parameter names, not just top-level identifiers: `action`, `active`, `type` and `severity` are all keywords, so an event argument called `action: FaultAction` fails `fpp-locate-defs` with "error: ) expected". Rename (`faultAction`, `activeMask`) or escape with `$`. The failure only appears at `fprime-util generate`, never on the host.
- `register_fprime_library` derives its module dependencies from `fpp-depend` (`build-.../<module>/fpp-cache/direct.txt`) and does **not** guard against cycles. Putting shared port/enum types in the same module as a component that depends on a producer creates one: the producers need `Components.FaultReport`, and FaultManager needs ModeManager's `ForceSafeModeWithReason`. CMake breaks the cycle by dropping an edge and the target build then fails with `fatal error: .../FaultReportPortAc.hpp: No such file or directory` while compiling the producer. Fix: a types-only module with no dependencies (`Components/FaultTypes/`, shape copied from `Components/Drv/Types/`). Cycle D's plan had the types inside `Components/FaultManager/`; that does not build.
- `register_fprime_module()` (the SOURCE_FILES form, used by AuthenticationRouter) tolerates what `register_fprime_library(AUTOCODER_INPUTS ...)` does not, which is why AuthenticationRouter has always used `Components.ForceSafeModeWithReason` with no DEPENDS.
- Generated FPP enum classes expose a public `T e` member plus `operator T()`, `operator==(T)` and `operator!=(T)` (`build-.../ModeManager/SafeModeReasonEnumAc.hpp:107-171`). Host stubs that mirror that exact shape let flight code use `.e` identically on host and target; the older stubs in `support/` use a private `m_value` with a `value()` accessor, so both shapes coexist.
- `Components/Watchdog/Watchdog.cpp` is host-buildable with three stub headers only: `support/Fw/Types/OnEnumAc.hpp` (`Fw::On`), `support/config/FpConfig.hpp` (forwards to `FpTypesStub.hpp`) and `Fw::Logic` added to `FpTypesStub.hpp`. Values are `OFF/ON = 0/1` and `LOW/HIGH = 0/1` (`lib/fprime/Fw/Types/Types.fpp:32-41`).
- codespell rejects the plural of "static" (it corrects to "statistics") anywhere in the tree, comments included; say "mutable global state" instead.
- `rsync --delete-excluded` into `~/scalar-build/proves-core-reference` DELETES the copy's `fprime-venv`, because the documented invocation excludes it. Use the plain `--exclude` form from CLAUDE.md and never add `--delete-excluded`. Recovery: `cp -a` the main repo's venv over and rewrite both stale prefixes in `fprime-venv/bin/*` shebangs — the main repo's console-script shebangs point at a third, older path (`/Users/jesse-cm/Documents/scalar-softwarestack/...`), which is why CLAUDE.md tells you to call `fprime-venv/bin/python3 <script>` rather than a console script.
- Target after Cycle D: FLASH 715104 B (68.49 %), RAM 342976 B (64.41 %); dictionary 361 commands, 98 params, 215 channels, 676 events; the packet set holds 22 packets, which is `MAX_PACKETIZER_PACKETS` (`project/config/TlmPacketizerCfg.hpp:19`) — the next packet needs that constant raised.
- iCloud Drive syncs this checkout (it lives under "Documents - Jesse's Mac") and drops conflict copies named `<name> 2.<ext>` next to files edited quickly in succession; they appear as untracked files, some are stale snapshots. Delete them; never commit them. Same artifact seen in the venv `bin/`.
