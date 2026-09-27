# 02 — Per-file resolution rules (normative)

Hunk lines are `trial:` line numbers in the scratch worktree (`grep -n '^<<<<<<<\|^=======\|^>>>>>>>' <file>`). "Theirs" = `proves-origin/main` a477893b, "ours" = d4fda377. Every rule below is the result the merged file must show; the coder resolves in place and removes the markers.

## A. Deleted components (modify/delete and add/unmerged)

| Path (status) | Rule |
|---|---|
| `Components/Authenticate/{Authenticate.cpp,.hpp,docs/sdd.md}` (UD), `Authenticate.fpp`, `CMakeLists.txt`, `SequenceNumberStore.{cpp,hpp}` (D) | **delete the directory** (`git rm -r`). F2 restores `SequenceNumberStore.*` from `d4fda377:PROVESFlightControllerReference/Components/Authenticate/` into `Components/TcSecurityDeframer/`. |
| `Components/AuthenticationRouter/*` (UD/D) | **delete the directory**. |
| `Components/TcSecurityDeframer/SequenceNumberStore.{cpp,hpp}` (AU — git's directory-rename detection moved our files here) | **delete in F1** (`git rm`); F2 re-adds them deliberately with the deframer's includes and path parameter. |
| `Components/TcSecurityDeframer/CMakeLists.txt` (UU, trial:21-29) | **theirs** entirely: SOURCES `Authenticator.cpp`, `TcSecurityDeframer.cpp`, `Parser.cpp`, `Validator.cpp`; DEPENDS `kernel` only. The `PersistedRecord` DEPENDS line that the rename carried in (trial:33) is removed in F1 and re-added in F2 with `SequenceNumberStore.cpp`. |
| `docs-site/components/{Authenticate,AuthenticationRouter}.md` (UD) | **delete**. |
| `test/int/authentication_test.py`, `test/unit-tests/test_Authenticate_SequenceNumberStore.cpp`, `docs-site` nav entries | **delete** (F1); see `06-followups.md` §3 for CH-L2-05. |

## B. Conflicted files kept (UU)

| File | Hunk (trial) | Rule |
|---|---|---|
| `Components/CMakeLists.txt` | 31-37 | **both**: keep `TaskGate`, `TcFrameCorrector`, `TelemetryGate` and add `TcSecurityDeframer`, alphabetical. `ProvesRouter` (trial:6) is already present; no `Authenticate`/`AuthenticationRouter` line remains. |
| `ModeManager/ModeManager.cpp` | 13-17 | **both** includes (`PersistedRecordFile.hpp` and `ModeManager.hpp`), deduplicated. |
| | 544-578 | **both** functions: `reportLowBattery` (ours) and `commandLossCheck` (theirs), in that order. |
| | trial:29 (clean) | `MAX_SAFE_MODE_REASON = 5` → **6** (accept `COMMAND_LOSS = 6`, `ModeManager.fpp` trial:17). Nothing else in `loadState()` changes. |
| | trial:61-62 (clean) | keep: `restorePersistentState() { loadState(); }`; no `init()` override anywhere in the file (verified: none in the trial merge). |
| `ModeManager/ModeManager.fpp` | 80-89 | **both** ports: `faultOut: Components.FaultReport` and `stopWatchdog: Fw.Signal`. |
| `ModeManager/ModeManager.hpp` | 10-16 | **theirs** includes added to ours (`Os/File.hpp`, `Os/Mutex.hpp`, `Fw/Time/Time.hpp`). |
| | 162-171 | **both** declarations (`reportLowBattery`, `commandLossCheck`). |
| `ModeManager/docs/sdd.md` | 6-35 | **ours** table (7 columns, MM0001-MM0012); upstream's MM0011 row enters as **MM0013** via `req.py add` (`01-scope.md` §2). |
| | 95-116 | **both**: upstream's `stopWatchdog` port row *and* our `faultOut` row + "Fault reporting" section. |
| | 298-309 | **both**: our Change Log rows plus upstream's two design bullets (command loss ownership, mutex), plus a dated row "2026-09 sync: command-loss timer moved here from AuthenticationRouter (1af2a0c5); MAX_SAFE_MODE_REASON 6". |
| `StartupManager/StartupManager.cpp` | 9-17 | **both** include sets (`<cstdio>`, `PersistedRecordFile.hpp`, `Os/File.hpp`, `Os/FileSystem.hpp`, `HardCodedStartup.h`). |
| | 115-203 | **theirs** (read/write templates, `MAX_PLAUSIBLE_BOOT_COUNT`). |
| | 210-248 **and the clean remainder 249-265** | **theirs**: `get_boot_count` must equal `proves-origin/main:…/StartupManager.cpp:113-142` byte for byte after the markers go; the ours-only tail (trial:250-265, `encodeU64LE`/`PersistedRecord::store` of `BOOT_COUNT_MAGIC`) is deleted with it. Remove the now-unused `BOOT_COUNT_MAGIC`, `BOOT_COUNT_PAYLOAD_SIZE`, `encodeU64LE`, `decodeU64LE` (ours `StartupManager.cpp:20-24,45-57`). `update_quiescence_start` (trial:283-327) stays **ours** (PersistedRecord `SQS1`). `run_handler` (trial:428-449, clean) is already upstream's with the retry. |
| `StartupManager/StartupManager.hpp` | 35-51 | **theirs** doc comment for `get_boot_count`; the quiescence comment (trial:66-81) stays ours. |
| `StartupManager/docs/sdd.md` | 11-55 | **both**: upstream's feature list (1-4) *and* our "Persisted files" section reduced to the quiescence row (boot count now described by upstream's "Boot count persistence" text, which the merge keeps). |
| | 136-141 | **theirs** (adds `TRANSMIT_ENABLE_TICKS`; drop our `.tmp` note on `BOOT_COUNT_FILE`). |
| | 153-161 | **theirs** event rows for `CurrentBootCount`, `BootCountUpdateFailure`, `BootCountCorrupted`; **ours** for `QuiescenceFileInitFailure`. |
| | 175-210 | **ours** table; upstream's REQ-SM-008..011 enter as **REQ-SM-009..012** via `req.py add`; REQ-SM-008 re-worded via `req.py set`. Change Log: both, plus a sync row. |
| `Top/ReferenceDeploymentPackets.fppi` | 39-56, 68-102, 120-143, 196-214 | **theirs** syntax (unqualified names) **plus our fork channels** in the same packets: `imuManager.CollectionIntervalS`, `powerMonitor.CollectionIntervalS`, `adcs.CollectionIntervalS`, `thermalManager.CollectionIntervalS`, `telemetryGate.TransmitState`, `telemetryGate.GatedTicks`. |
| | 233-260 | **theirs** `CdhCore.tlmSend.SectionEnabled` (replaces `SendLevel`, which does not exist at 4.3.0) **plus ours**: `taskGate.TasksEnabledMask`, `taskGate.GatedRuns`, `tcFrameCorrector.CorrectedFrames`, `tcFrameCorrector.UncorrectableFrames`, and the whole `packet Faults id 9 group 5` (13 `faultManager.*` channels). The stale "22 packets" comment (trial:241-243) is dropped. |
| | 422-432 | **both**: the five `driverBoardBufferManager.*` omit lines (unqualified) and `CdhCore.tlmSend.GroupConfigs`. |
| | clean parts | `packet PayloadHousekeeping id 23 group 3` (22 `driverBoardHandler.*` channels) and `packet Security id 6` are already present unqualified; our `packet Authenticate id 6` must **not** survive (ids must be unique). Result: 23 packets, 173 + 70 = 243 distinct channels (`05-verification.md` §3). |
| `Top/topology.fpp` | 323-336 | **ours** (taskGate splices on slots 17/18, `faultManager.run` on 20) **minus** `rateGroup1Hz.RateGroupMemberOut[19] -> ComCcsdsLora.authenticationRouter.run` and the comment's "authenticationRouter[19]" clause; slot 19 stays free. |
| | trial:548 (clean) | **delete** `ComCcsdsLora.authenticationRouter.faultOut -> faultManager.faultIn[2]`; `faultIn[2]` stays unconnected (F3 does not re-wire it — the report travels on `modeManager.faultOut -> faultManager.faultIn[1]`, trial:547, because `FaultManager::faultIn_handler` ignores `portNum`, `FaultManager.cpp:136`). |
| | trial:511-515 (clean) | keep theirs: `provesRouter.packetRouted -> modeManager.packetRouted` ×2, `modeManager.stopWatchdog -> watchdog.stop`. |
| `project/config/TlmPacketizerCfg.hpp` | 19-40 | **theirs** structure with `MAX_PACKETIZER_PACKETS = 24` and `MAX_PACKETIZER_CHANNELS = 256`; comment corrected to "distinct channels named in ReferenceDeploymentPackets.fppi, packets **and** omit block (TlmPacketizer.cpp:86-87,148-149); checked by scripts/check_packet_set.py". |
| `test/int/mode_manager_test.py` | 38-42 | **theirs** (`from datetime import datetime, timezone`). |
| | 558-877 | **both**: our block (trial:559-797, `test_safe_09`-`11`) then theirs (trial:799-876) with its function renamed `test_safe_12_command_loss_triggers_safe_mode_and_reboot` and `@pytest.mark.verifies("MM0013")` added; add `slow` to `pytest.ini` markers (upstream uses it, no side defines it). |
| `test/int/power_monitor_test.py` | 7-10, 17-24 | **both** (`import time`, `ACCUMULATION_GAP_S`, `pytestmark = [pytest.mark.requires_battery]`). |
| `test/int/watchdog_test.py` | 90-94 | **both** decorators. |
| `test/unit-tests/CMakeLists.txt` | 80-284 | **both**: all our libraries (trial:82-268) minus `authenticate_sequence_store` (re-added in F2 as `tc_security_deframer_sequence_store`), plus theirs (`find_path(PSA_CRYPTO_H psa/crypto.h)`, `find_library(mbedcrypto)`, `FATAL_ERROR` guards, `security_deframer_authenticator` link). |
| | 303-324 | **both** target lists minus `authenticate_sequence_store`, plus `proves_router_bypasser`. Add the new fakes (`05-verification.md` §fakes). |
| `docs-site/components/{ModeManager,StartupManager,ThermalManager}.md` | all | **regenerate** from the resolved sdd (`cp`, as `make docs-sync` does; ThermalManager's sdd is untouched upstream — `git diff --stat 638f8e2f proves-origin/main -- Components/ThermalManager/` is empty). |
| `mkdocs.yml` | 111-120 | **both**: upstream's two Security entries, our Requirements section. |
| `pytest.ini` | 7-14 | **both** marker sets, plus `slow`. |

## C. Clean merges that still need a hand edit in F1

| File | Edit |
|---|---|
| `TelemetryGate.hpp:52`, `TelemetryGate.cpp` definition | `Components::TelemetryTxState txState` → `const Components::TelemetryTxState& txState` |
| `TaskGate.hpp:52,62`, `TaskGate.cpp` definitions | `Components::SchedTask task` → `const Components::SchedTask& task` |
| `test/unit-tests/support/…/TelemetryGateComponentAc.hpp:54`, `TaskGateComponentAc.hpp:67-68` | same signatures in the recorder stubs |
| `test/unit-tests/test_ModeManager_StatePersistence.cpp:106,153,180,229,254,275,292,311` | `init(0)` followed by `restorePersistentState()` |
| `test/int/telemetry_sources_test.py:431-432` | channel names → `ComCcsdsLora.provesRouter.RejectedPackets`, `ComCcsdsUart.provesRouter.RejectedPackets` |
| `test/int/fault_manager_test.py:195-215` (docstring) | producer-2 procedure re-worded in F3, not F1 |
| `Components/FaultTypes/FaultTypes.fpp:22,59-62` | comment only: `AUTH_ROUTER = 2` marked "retired 2026-09 sync; value kept", `faultIn[2]` marked free |
| `lib/fprime`, `lib/fprime-zephyr`, `lib/fprime-extras` | pointers from theirs; `git -C lib/fprime checkout -- requirements.txt Svc/FrameAccumulator/FrameAccumulator.cpp` **before** the merge (owner decision 3), then `git submodule update --init --recursive` after it. `git diff --stat lib/` must be empty at the F1 gate. |

## D. Take theirs, no review needed (70 paths the fork never touched)

`prj.conf`, `west.yml`, `boards/**` (5 files), `project/config/{AcConstants,CdhCoreConfig,ComCcsdsConfig,ComCfg,FileHandlingConfig,FpConstants,PlatformCfg,TlmPacketizerCfg}.fpp`, `project/config/FpConfig.h`, `project/config/CMakeLists.txt`, `Top/system.fpp`, `Top/CMakeLists.txt`, `Main.cpp`, `ComCcsds{Lora,Uart,Sband}/ComCcsds.fpp`, `Components/{DetumbleManager,Drv/RtcManager,SBand}/*`, `Components/StartupManager/{HardCodedStartup.h,StartupManager.fpp}`, `.github/workflows/ci.yaml`, `.pre-commit-config.yaml`, `.gitignore`, `.vscode/launch.json`, `AGENTS.md`, `README.md`, `Framing/src/authenticate_plugin.py`, `Makefile` (clean merge; `docs-sync` already lists no Authenticate copies), `makelib/zephyr.mk`, `lib/west-commands/uv.py`, `patches/fprime-gds-version.patch` (deleted), `requirements.txt`, `scripts/{check_console_disabled,generate_auth_key_header}.py`, `sequences/{lose_time,throttle_amateurs}.seq`, `tools/**`, `test/day-in-the-life/*`, `test/int/{conftest,radio_test,sync_sequence_number_test,veml6031_test}.py`, `docs-site/{index,components/DetumbleManager,components/RtcManager,components/ProvesRouter,components/TcSecurityDeframer}.md`. Full list: `comm -13 <(git diff --name-only 638f8e2f HEAD | sort) <(git diff --name-only 638f8e2f proves-origin/main | sort)`.
