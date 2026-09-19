# Upstream sync analysis — `proves-origin/main` vs `feat/driver-board`

Fork point `638f8e2f` (2026-06-03). Ours: `d7518902` (48 commits). Upstream: `a477893b` (21 commits, 128 files). Both sides touched 34 files. "Textual conflict" below is `diff3 -m` on the three blobs (stricter than git's merge for adjacent hunks); "semantic" is what the merged result would do. Read-only analysis; nothing was merged.

## 1. Upstream commits since the fork point (oldest first)

| Commit | Date | Effect |
|---|---|---|
| 8726bfe4 #433 | 07-11 | Int-test fixes: `conftest.py` hardware-skip logic, `pytest.ini` markers `requires_face/antenna/battery/watchdog_jumper`, `prj.conf` console off, `scripts/check_console_disabled.py`. |
| 1cc18b29 #431 | 07-11 | `lib/west-commands/uv.py`, `makelib/zephyr.mk`: use project-local uv. |
| e5e0a1c7 #441 | 07-12 | CI: `git submodule foreach checkout -- . && clean -fd` reset step (also in `Makefile` `submodules`). |
| dc3e0aff #420 | 07-15 | RtcManager time base `TB_WORKSTATION_TIME` → `TB_SC_TIME` (`RtcManager.cpp`, sdd). |
| 65f8cb4d #446 | 07-16 | CI formats the flash FS before int tests; `tools/ci/detect-board-tty.sh`. |
| 1af2a0c5 #395 | 07-17 | **AuthenticationRouter deleted → `ProvesRouter`**; command-loss timer moved into ModeManager (`packetRouted`, `stopWatchdog`, `COMM_LOSS_TIME`, `SafeModeReason.COMMAND_LOSS = 6`); `safe_mode_test.py` renamed `mode_manager_test.py`; 12 channels dropped from the packet set. |
| 482611b2 #451 | 07-18 | `Makefile` `sync-sequence-number` target. |
| 04c9db85 #445 | 07-18 | `CONFIG_I2C_DW_CLOCK_SPEED=150` in v5c/v5d/v5e defconfigs (SCL was ~520 kHz, not 100 kHz). |
| e13330ce #413 | 07-19 | `requirements.txt`: fprime-yamcs 0.1.3, fprime-xtce 0.1.2. |
| b2437a9d #447 | 07-19 | **Authenticate deleted → `TcSecurityDeframer`** (Parser/Validator/Authenticator + host tests needing `psa/crypto.h`), `ProvesRouter/Bypasser`; `prj.conf` PSA HMAC flags; ci.yaml; `AuthDefaultKey.h` path moves. |
| 5c67bae2 #453 | 07-19 | `sequences/throttle_amateurs.seq`: `SET_ID_FILTER` on Joke0-15 and deframer/rate-group-slip events. Not a component. |
| 606eb0ab #421 | 07-19 | **Zephyr 4.3.0 → 4.4.1** (`west.yml`: tinycrypt → tf-psa-crypto, HAL bumps); `Main.cpp` `FIXED_PARTITION_ID` → `PARTITION_ID`; board dtsi: `mux_channel_*` lose `compatible`/`label`, ADC pinmux drops GP26; `tools/patch-zephyr-sdk-toolchain-download.py`. |
| 3d761fca #398 | 07-20 | **F′ 4.1.1 → 4.2.2**: `TlmPacketizerCfg.hpp` loses `TLMPACKETIZER_HASH_BUCKETS/NUM_TLM_HASH_SLOTS/HASH_MOD_VALUE`, gains `MAX_PACKETIZER_CHANNELS = 202`; new `TlmPacketizerCfg.fpp` (groups 1-6); `ReferenceDeploymentPackets.fppi` rewritten (unqualified names, `group N`); `patches/fprime-gds-version.patch` deleted. |
| c222dd8a #452 | 07-21 | Pre-commit hook `docs-sync` runs `make docs-sync` on any `docs/sdd.md` change. |
| 6944fcab #467 | 07-22 | `CONFIG_MBEDTLS_HEAP_SIZE` 32767 → 8192 (+24 KiB to libc malloc arena). |
| 24968faa #414 | 07-22 | `radio_test.py` CONTINUOUS_WAVE regression test; fprime-zephyr bump. |
| 87e40c93 #443 | 07-25 | **Hard-coded startup**: StartupManager ports renamed (`sequenceStarted` → `startupsequenceStarted`, `completeSequence` → `startupCompleteSequence`), new `safeMode*/payload*` sequence ports, `loraFirstStart`, `enableTransmit/disableTransmit`, `TRANSMIT_ENABLE_TICKS = 2800`, `HardCodedStartup.h` `DEFAULT_STARTUP_VALUE 0`; ModeManager `sequenceDoneNotify`; `sequences/lose_time.seq`. Needs fprime-zephyr `LoRa.fpp:84-90`. |
| c03b2067 #470 | 07-26 | Boot count: flush before close, `MAX_PLAUSIBLE_BOOT_COUNT = 1000000`, temp+rename `persist_boot_count`, retry each tick, `BootCountCorrupted` event; GET_BOOT_COUNT no longer writes. |
| 451c5bcb #455 | 08-02 | RtcManager `param TIMEBASE: Rtc.TimeBase` (TB_PROC_TIME=1 / TB_SC_TIME=3), `TimeBaseChanged` event, param ports. |
| ea143d0a #493 | 08-03 | DoL tests moved to `test/day-in-the-life/`, `DOL_SERIAL_PORT`. |
| a477893b #507 | 08-29 | **F′ 4.2.2 → 4.3.0** (`lib/fprime` 7d8f579f = v4.3.0, fprime-zephyr b14101dd, fprime-extras 033240c1): enum/struct command args by `const&`, `Fw::Buffer::advance`, `init()` override removed from ModeManager, `modeManager.restorePersistentState()` called from `ReferenceDeploymentTopology.cpp`, `system.fpp`, new `FpConfig.h`, `CdhCoreConfig.fpp`, `FileHandlingConfig.fpp`, `PlatformCfg.fpp`, `CommandDispatcherImplCfg.hpp` namespace, `ComCfg.fpp` FrameContext gains `saIndex/authenticated`. |

## 2. Files changed on both sides (34)

| File | Upstream | Ours | Textual | Semantic |
|---|---|---|---|---|
| `Components/Authenticate/{Authenticate.cpp,.hpp,docs/sdd.md}` | deleted (b2437a9d) | PersistedRecord-backed `SequenceNumberStore` (fbf1fa08) | modify/delete | Component gone; our store, host test `test_Authenticate_SequenceNumberStore.cpp`, `authentication_test.py` (CH-L2-05) target nothing upstream. |
| `Components/AuthenticationRouter/{.cpp,.fpp,docs/sdd.md}` | deleted (1af2a0c5) | `faultOut` port for FaultManager (458d119d) | modify/delete | Our FaultManager producer hook `faultIn[2]` (`topology.fpp:522`) loses its source; `fault_manager_test.py:207-210` reads `authenticationRouter.CommandLossFound`. |
| `Components/CMakeLists.txt` | −Authenticate −AuthenticationRouter +ProvesRouter +TcSecurityDeframer | +8 fork subdirs | conflict (1) | Trivial once the two deletions are accepted. |
| `ModeManager/ModeManager.cpp` | command-loss check, `restorePersistentState()` (a477893b) | PersistedRecord state, `reportLowBattery` (+151/−132) | clean | **Yes**: our `init()` calls `loadState()` (`ModeManager.cpp:54-56`), the pattern a477893b removed because it drives load switches before GPIO is open; upstream adds `SafeModeReason.COMMAND_LOSS = 6` while our on-disk layout validates `reason > MAX_SAFE_MODE_REASON (5)` (`ModeManager.cpp:24,288`). |
| `ModeManager/ModeManager.fpp` | `packetRouted`, `stopWatchdog`, `COMM_LOSS_TIME`, `CommandLossDetected`, `sequenceDoneNotify` | `faultOut` | conflict (1) | Upstream's `stopWatchdog` and our FaultManager both stop the watchdog for command loss. |
| `ModeManager/ModeManager.hpp` | `restorePersistentState()`, mutex, counters; `init` removed | `encodeState/storeState/reportLowBattery`, `STATE_TEMP_PATH` | conflict (2) | As above. |
| `ModeManager/docs/sdd.md`, `docs-site/components/ModeManager.md` | command-loss section | persistence + fault rows | conflict (3) | Requirement table edited on both sides; must be re-done with `scripts/req.py`. |
| `StartupManager/StartupManager.cpp` | #443 startup sequence + #470 hardening + 4.3.0 | PersistedRecord for boot count and quiescence (fbf1fa08) | conflict (5) | **Both implement the same fix** for the same failure (torn boot-count file); see §7.3. |
| `StartupManager/StartupManager.hpp` | ports, `persist_boot_count`, `m_boot_count_persisted` | `decode*`, record constants | conflict (4) | Same. |
| `StartupManager/docs/sdd.md`, `docs-site/components/StartupManager.md` | hard-coded startup + corruption rows | REQ-SM-008 etc. | conflict (4) | Requirement tables. |
| `Top/ReferenceDeploymentPackets.fppi` | full rewrite for 4.2.2 packet groups (+144/−145); `Security` packet; −Authenticate/Router channels | +61: `Faults` (id 9), `PayloadHousekeeping`, TelemetryGate/TaskGate/TcFrameCorrector/driverBoard channels | conflict (12) | Our additions must be re-expressed in the new syntax; see §7.1. |
| `Top/ReferenceDeploymentTopology.cpp` | `modeManager.restorePersistentState()` (+3) | `driverBoardUart.configure(...)` (+2) | clean | None. |
| `Top/topology.fpp` | provesRouter rewiring (`:153-166`), #443 wiring (`:211-229`, `:458-485`), `deployment topology` | telemetryGate splice (`:171-181`), faultManager/taskGate/driverBoard wiring (`:487-528`) | conflict (1) | `ComCcsdsLora.authenticationRouter.{faultOut,SetSafeMode,reset_watchdog}` (`:320,489,522`) have no upstream instance. |
| `project/config/CommandDispatcherImplCfg.hpp` | 4.3.0 `CmdDispatcherCfg` namespace (+16) | table 400 → 512 (e08685af) | clean | None. |
| `project/config/TlmPacketizerCfg.hpp` | buckets/slots/mod removed; `MAX_PACKETIZER_CHANNELS = 202`, `MAX_PACKETIZER_PACKETS = 22` | buckets 202 → 256, packets 22 → 24 | conflict (2) | Our E1 constant no longer exists; see §7.1. |
| `test/int/{burnwire,drv2605,rtc,tmp112}_test.py` | hardware-skip markers, #455 TIMEBASE test | `verifies` markers | clean | None. |
| `test/int/{power_monitor,watchdog}_test.py` | skip markers | `verifies` + extra asserts | conflict (2), (1) | Marker placement only. |
| `test/unit-tests/CMakeLists.txt` | +TcSecurityDeframer/Bypasser libs, `find_path(psa/crypto.h)` FATAL_ERROR | +212 lines of fork libs/tests | conflict (5) | Upstream host build now requires Mbed TLS PSA headers; ours is F′/crypto-free (`test/unit-tests/README.md`). |
| `docs-site/components/{Authenticate,AuthenticationRouter}.md` | deleted | edited | modify/delete | Regenerate via docs-sync. |
| `docs-site/components/ThermalManager.md` | re-synced copy (+56/−17) | our sdd changes | conflict (3) | Copy only; sdd itself untouched upstream. |
| `Makefile` | patch step removed, docs-sync paths, `check-console-disabled`, `sync-sequence-number` | docs-sync adds, `rtm` target | clean | `docs-sync` still lists our Authenticate/AuthenticationRouter copies after merge; delete those two lines. |
| `mkdocs.yml` | nav Authenticate → TcSecurityDeframer, ProvesRouter | +5 nav entries | conflict (1) | Nav only. |
| `pytest.ini` | +4 hardware markers | `verifies`, `flatsat` | conflict (1) | Markers only. |

## 3. Component dependencies and overlaps

| Ours | Depends on / overlaps upstream | State after sync |
|---|---|---|
| Authenticate (SequenceNumberStore) | Upstream deleted the component (b2437a9d); TcSecurityDeframer persists nothing per its file list (Parser/Validator/Authenticator only). | Orphaned. Sequence-number persistence would need re-homing into TcSecurityDeframer or dropped. `Authenticate.cpp:301,365` uses `setData(getData()+n)`, the pattern a477893b replaced with `advance()`; still legal at 4.3.0 (`Fw/Buffer/Buffer.cpp:125-129` asserts within capacity). |
| FaultManager (4 producers) | `faultIn[2]` ← `ComCcsdsLora.authenticationRouter.faultOut` (`topology.fpp:522`). Upstream moved command loss into ModeManager (`packetRouted`/`commandLossCheck`, 1af2a0c5) and added `stopWatchdog`. | Producer 2 has no source; ModeManager would need the hook instead. Two independent watchdog-stop paths for the same fault. |
| PersistedRecord (+ StartupManager/ModeManager adoption) | #470 gives StartupManager its own flush + temp/rename + plausibility check; a477893b gives ModeManager `restorePersistentState()` after wiring. | Duplicate mechanisms in StartupManager; ModeManager keeps our CRC record but must adopt the late-restore call site. Our `ModeManager::init` → `loadState()` (`ModeManager.cpp:54`) is the ordering bug upstream fixed. |
| TelemetryGate, TaskGate | `Svc.Sched` only (`TelemetryGate.fpp:17,20`, `TaskGate.fpp:32,35`). Command handlers take enums by value (`TelemetryGate.hpp:52`, `TaskGate.hpp:52,62`). | 4.3.0 generates `const T&` handler signatures (a477893b `RtcManager.hpp` diff; fprime-zephyr b76db2b) → compile error until changed. |
| DriverBoardHandler / DriverBoardProtocol / Crc16 | `Drv.ByteStreamData/Send/Ready` (`DriverBoardHandler.fpp:74-83`) exist at 4.3.0 (`Drv/ByteStreamDriverModel/ByteStreamDriverModel.fpp:14-25`); `Fw::Buffer` use is `getData/getSize` only (`DriverBoardHandler.cpp:102-103,444`). uart1 is `status = "okay"` in base dtsi on both sides (`v5.dtsi:128-130`). | No API break found. |
| TcFrameCorrector | Wired in `topology.fpp` only; upstream's `ComCfg.fpp` FrameContext gains `saIndex/authenticated` fields. | Compiles; context struct grows. |
| RunInterval, FaultTypes | Pure helpers. | None. |
| — | Upstream-only: `TcSecurityDeframer`, `ProvesRouter`, `HardCodedStartup.h`, RtcManager `TIMEBASE` (#455), throttle sequence (#453). | #455 and #453 do not overlap any fork component. |

## 4. Framework and Zephyr API impacts

`lib/fprime` f67b68fdb6 (v4.1.1-24) → 7d8f579f (v4.3.0, 2026-08-19): 547 commits. `lib/fprime-zephyr` 31399714 → b14101dd (73 commits). Zephyr v4.3.0 → v4.4.1. fprime-extras 982139f9 → 033240c1 ("Support F Prime 4.3.0", ComRetry removed 1d82b88 — `loraRetry` is `Svc.ComRetry` per ledger `:154`, not the extras one).

| Change (commit) | Our code affected | Effect |
|---|---|---|
| Enum/struct command args by `const&` (a477893b; fprime-zephyr b76db2b) | `TelemetryGate.hpp:52`, `TaskGate.hpp:52,62` | Override signature mismatch → build fails until edited. |
| `Fw::Buffer` offset/capacity + `advance()` (ceee420e54) | `Authenticate.cpp:301,365` | Works; pattern deprecated in practice. |
| TlmPacketizer red-black tree (d4d6dd1e85), parameter-driven tables (5116193a27), group control (6ed2378df4) | `TlmPacketizerCfg.hpp`, `scripts/check_packet_set.py:101-102,122` | `TLMPACKETIZER_HASH_BUCKETS` no longer exists; the boot-assert trap in `CLAUDE.md` and ledger `:37,:55,:196` describe removed code. New limit `MAX_PACKETIZER_CHANNELS = 202` ("non-omitted channels"). |
| PrmDb CRC (e9dfd3ae89), `Fw::ArrayMap` (f658612968), file name from subtopology (9fb2d4a099) | `ReferenceDeploymentTopology.cpp:69`, `CLAUDE.md` PrmDb trap | 4.3.0 reads a CRC at the file head (`PrmDbImpl.cpp:425-446` at 7d8f579f) → a `/prmDb.dat` saved by 4.1.1 firmware fails `PrmFileReadError CRC`; all `PRM_SAVE_FILE`d values revert to defaults on first boot after sync. |
| `Os::File::open` bounded overloads (8c7a030519), `FileSystem` copy fix (5f3e8ce90d), `File::readline` contract (bbbf778e2a) | `PersistedRecordFile.cpp:28,42,92,99,107,114` | Additive; our calls unchanged. |
| fprime-zephyr `Os/FileSystem.cpp` now maps `-errno` through `Os::Posix::errno_to_filesystem_status`; `Os/File.cpp` unchanged | `PersistedRecordFile.cpp:37,114` (`exists`, `rename`) | `File::open` OTHER_ERROR trap stays valid (File.cpp not in the diff); rename/remove status codes may change value. |
| `Os::Directory` Zephyr impl rewritten (fprime-zephyr `Os/Directory.cpp` 85 lines, `.hpp` new) | DataRecorder design (`docs-site/dev-loop/design/stored-data/02-design.md`, `04-plan.md`) | Design written against the pre-4.3 Directory API; re-check before A8/A9. |
| `Svc.PassiveRateGroup` cycle-time telemetry (48725b7535), `TlmChan` guard (041aee36e6) | `rateGroup*` channels in packet set | New channels appear in dictionary; count against `MAX_PACKETIZER_CHANNELS`. |
| `init()` override removed from ModeManager (a477893b); AntennaDeployer/Authenticate still override (`AntennaDeployer.hpp:28`, `Authenticate.hpp:29`) | `ModeManager.hpp:29` | Our override is the one upstream deleted. |
| `FpConfig.h` (`FW_DIRECT_PORT_CALLS`, `FW_ASSERTIONS_ALWAYS_ABORT 0`), `PlatformCfg.fpp` handle sizes, `CdhCoreConfig.fpp`, `FileHandlingConfig.fpp` (a477893b) | `project/config/` — we changed only two files | New required config files; take theirs. |
| Zephyr `bindings: tca954x: drop child compatible` (69a8ac6a020) | none (we did not touch `boards/`) | Explains the dtsi edit; see §5. |

## 5. Devicetree and board

- `proves_flight_control_board_v5.dtsi:208-470`: every `mux_channel_N` loses `compatible = "ti,tca9548a-channel"` and `label` (606eb0ab). Node names, `reg`, and the per-channel sensor children are unchanged. Required by Zephyr 4.4.1 (`ti,tca954x-base.yaml` rewrite, 69a8ac6a020).
- `proves_flight_control_board_v5-pinctrl.dtsi:57-63`: ADC group drops `ADC_CH0_P26` (GP26 is the TCA9548A reset line per model `I2cTopology.sysml:60`), keeps GP27.
- v5c/v5d/v5e `*_defconfig`: `CONFIG_I2C_DW_CLOCK_SPEED=150` (04c9db85). Before this, both I2C buses ran ~520 kHz SCL although the dtsi says `clock-frequency = <100000>`.
- uart0/uart1 nodes (`v5.dtsi:121-133`), `&flash0` partitions, `storage_partition`: untouched upstream. `Main.cpp` `FIXED_PARTITION_ID` → `PARTITION_ID` (606eb0ab) is a macro rename, same partition.
- We changed nothing under `boards/` (`git diff --stat 638f8e2f HEAD -- boards/` empty); no textual conflict.
- Model claims affected (all `src: S3`, `~/scalar/model`): `interfaces/I2cTopology.sysml:22,37` (100 kHz — was not the delivered bus speed before #445); `:70,92,102,109,120` and `structure/Thermal.sysml:28,56`, `Avionics.sysml:197-199` cite `mux_channel_*` nodes (names survive; `label` property does not); `structure/FlightSoftware.sysml:59,74` (Zephyr v4.3.0) and `:105` (`v3.1.1-986-gf67b68fdb`) become stale; `:183-184,235` list Authenticate/AuthenticationRouter as component types; `:264-266` `packetCount = 21`; `budgets/DataBudget.sysml:25-40,262` per-packet channel counts from the fork-point fppi; `structure/Payload.sysml:276` (uart1 declared, no driver instance) is already superseded by our 387374b8 and unaffected by upstream. 106 S3-sourced lines total; 42 cite files upstream changed (packets, dtsi, submodule versions).

## 6. Build and tooling

- `lib/fprime/requirements.txt`: fprime-tools 4.1.0 → 4.3.0, fprime-fpp 3.1.0 → 3.3.0, fprime-gds 4.1.0 → 4.3.0, new fprime-fpy 0.5.1, spacepackets, lark, fastcrc, crcmod; pytest 8.3.3 → 9.0.3. Top-level: fprime-yamcs 0.1.3, fprime-xtce 0.1.2.
- Local uncommitted edits in `lib/fprime` (`git -C lib/fprime diff --stat`): `requirements.txt` fprime-gds 4.1.0 → 4.1.1a2 (the content of upstream's deleted `patches/fprime-gds-version.patch`) and a two-line comment in `Svc/FrameAccumulator/FrameAccumulator.cpp`. Upstream's pins make the first moot; the second is noise. `lib/fprime-zephyr` is clean.
- `west.yml`: Zephyr v4.4.1, tinycrypt dropped, tf-psa-crypto added, HAL/mbedtls/mcuboot/picolibc bumped → full `west update`; the build copy's stray Zephyr tree (ledger `:54`) becomes the expected one.
- `prj.conf` (we never changed it): `CONFIG_CONSOLE/UART_CONSOLE/PRINTK=n`, `CONFIG_STD_CPP14=y`, PSA HMAC flags, mbedTLS heap 8192. Our components use no `printk`/`Os::Console` (grep empty).
- `Makefile`: `submodules` now runs `git submodule foreach 'git checkout -- . && git clean -fd'` — would discard the local `lib/fprime` edits. `check-console-disabled`, `sync-sequence-number`, `DoL_test` paths. `docs-sync` cp list conflicts only semantically.
- `.pre-commit-config.yaml`: new `docs-sync` hook runs `make docs-sync` — `make` fails at this checkout path (`CLAUDE.md` §Commands), so every sdd commit would fail the hook here until `scripts/verify.sh` or the hook is adapted.
- `.github/workflows/ci.yaml` (+161/−50): submodule reset, `check-console-disabled`, flash-format-before-tests, tty detection; hardware job `runs-on: deathstar`. Our only CI change is `deploy-docs.yml`.
- Host tests: upstream `test/unit-tests/CMakeLists.txt` adds `find_path(PSA_CRYPTO_H psa/crypto.h)` with `FATAL_ERROR` — ubuntu CI installs Mbed TLS; our host gate (`scripts/verify.sh`) would need it too.

## 7. Unintended consequences

1. **Packet set / packetizer limits.** Ours: 23 packets, 242 distinct channels (`scripts/check_packet_set.py` output; ledger `:201`). Upstream: 21 packets, 193 distinct channels (my count of `pk.theirs`; the brief's 159 was not reproduced — it may be the non-omitted count under `MAX_PACKETIZER_CHANNELS`). Fork additions: `driverBoardHandler` 22, `faultManager` 13, `driverBoardBufferManager` 5, `telemetryGate/taskGate/tcFrameCorrector` 2 each, packets `Faults`, `PayloadHousekeeping`, `Authenticate`; the 12 Authenticate/Router channels upstream removed are still in ours. Upstream never raised a bucket limit — it deleted the hash table (d4d6dd1e85) and set `MAX_PACKETIZER_CHANNELS = 202` (`TlmPacketizerCfg.hpp` at proves-origin/main). Our E1 fix (e08685af: 256 buckets) does not carry over; `check_packet_set.py:101-102` parses a constant that will not exist, and `MAX_PACKETIZER_PACKETS` reverts to 22 < our 23 packets. → Sync cycle must re-derive the limit (`MAX_PACKETIZER_CHANNELS ≥ 242 − Authenticate channels`), rewrite the fppi in group syntax, and re-target the checker.
2. **Two authentication stacks.** Our Authenticate/AuthenticationRouter changes (fbf1fa08, 458d119d) and their host/int tests sit on components upstream deleted; `ComCcsds*.fpp` instances become `tcSecurityDeframer`/`provesRouter` (1af2a0c5, b2437a9d). RTM rows CH-L2-05 (`authentication_test.py`), FD-L2-01/05/09 producer 2 (`fault_manager_test.py:207-210`) lose their observable. → Decide whether sequence-number persistence and the command-loss fault hook move into TcSecurityDeframer/ModeManager or are dropped.
3. **Boot-count hardening twice.** #470 (c03b2067) and our PersistedRecord adoption (fbf1fa08) fix the same torn-file failure in the same functions (`StartupManager.cpp` 5 conflicting hunks). Upstream: flush + temp/rename + plausibility cap + per-tick retry, raw `FwSizeType` payload. Ours: magic/version/CRC record + temp/rename, no retry, no cap. Neither is a superset. → Pick one on-disk format; a unit reboot with the other side's file reads as MISSING/corrupt either way.
4. **ModeManager restore ordering.** Upstream moved state restore out of `init()` into `restorePersistentState()` called after `connectComponents` (a477893b `ReferenceDeploymentTopology.cpp`); ours still restores in `init()` (`ModeManager.cpp:54-56`), which is the load-switch-before-GPIO bug they fixed. Also `COMMAND_LOSS = 6` exceeds our `MAX_SAFE_MODE_REASON = 5` (`ModeManager.cpp:24`) → a persisted command-loss reason is rejected as corrupt.
5. **PrmDb file format.** Every parameter saved on flight/desk units under 4.1.1 is unreadable by 4.3.0 (`PrmDbImpl.cpp:425-446` CRC header). DriverBoardHandler's `parameterUpdated` re-read on first tick (`DriverBoardHandler.cpp:142,327`) runs on defaults after the first post-sync boot until `PRM_SAVE_FILE` is re-issued.
6. **RAM.** #467 frees 24 KiB (32767 → 8192 mbedTLS heap) for the libc arena that `BufferManager` setup draws from; our post-E5 RAM is 363528 B / 68.27 % (ledger `:201`) with +20552 B from Cycle E. Whether 4.3.0 itself costs RAM is not measured here. → After sync, re-measure before deciding A8's ring buffer.
7. **Startup sequence assumptions.** #443 renames StartupManager's ports and adds a 2800-tick (46.7 min) hard-coded LoRa enable gated by `DEFAULT_STARTUP_VALUE 0`; #455 makes the time base a parameter defaulting to `TB_SC_TIME`. TelemetryGate's persisted tx state is independent of `enableTransmit/disableTransmit` on the LoRa driver — after sync there are two transmit inhibits (gate on TlmChan tick, driver-level enable) with no wiring between them. Our `payload_on/off.seq` use only `payloadPowerLoadSwitch` and `driverBoardHandler` commands; unaffected.
8. **Tests.** 10 fork-added int tests plus our 257-line extension of `safe_mode_test.py`, which upstream renamed to `mode_manager_test.py` (+127): rename/modify conflict, six requirement IDs (MM0001/2/4, MS-L2-03/05/07). `authentication_test.py`, `fault_manager_test.py`, `telemetry_sources_test.py:431` reference removed channels. Host: `test_Authenticate_SequenceNumberStore.cpp` orphaned; upstream host build needs `psa/crypto.h`.
9. **Ledger staleness** (`docs-site/dev-loop-findings.md`): lines 17-21, 26, 28-31, 37, 47, 55, 66-69, 71, 73-75, 121-122, 130, 133, 142, 146, 156, 184, 196 state facts about Authenticate/AuthenticationRouter/StartupManager/ModeManager internals, `TLMPACKETIZER_HASH_BUCKETS`, packet ids 1-22, `FileHelper`, dispatch table 350/400 — all against files upstream rewrote or deleted. `CLAUDE.md` traps 3-4 (PrmDb path, bucket assert) likewise.
10. **Tooling at this path.** Upstream's pre-commit `docs-sync` hook and `Makefile submodules` reset step assume `make` works and that `lib/fprime` is pristine; neither holds here (`CLAUDE.md` §Commands; §6).

## 8. Decisions the sync cycle must make

1. Authenticate + SequenceNumberStore: take theirs (TcSecurityDeframer) and re-home persistence, or keep ours and forgo upstream comms.
2. AuthenticationRouter `faultOut` → FaultManager: re-source from ModeManager's command-loss path, or drop producer 2 and its RTM rows.
3. StartupManager boot count: PersistedRecord format (ours) vs #470 raw+retry (theirs), or ours plus their retry/cap.
4. ModeManager: adopt `restorePersistentState()` call site (theirs) while keeping our record layout; raise `MAX_SAFE_MODE_REASON` to 6.
5. Packet set: rewrite `ReferenceDeploymentPackets.fppi` in group syntax with our packets; set `MAX_PACKETIZER_CHANNELS` and `MAX_PACKETIZER_PACKETS ≥ 23`; retarget `check_packet_set.py`.
6. TelemetryGate/TaskGate handler signatures → `const&` (forced by 4.3.0).
7. `prj.conf`, `west.yml`, `project/config/*` new files, `boards/`: take theirs (we changed none except two config constants).
8. `lib/fprime` local edits: discard (moot under 4.3.0 pins).
9. `safe_mode_test.py` vs `mode_manager_test.py`: merge our 257 lines into their renamed file or keep both.
10. Pre-commit `docs-sync` hook: disable locally or route through `scripts/verify.sh`.
11. Flight-unit `/prmDb.dat` and boot-count files: plan a re-`PRM_SAVE_FILE` and accept boot count reset, or write a one-shot migrator.
12. Ledger and `CLAUDE.md` traps listed in §7.9: mark superseded at sync time.
