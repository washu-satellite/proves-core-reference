# 01 — Scope: requirement rows the sync touches (normative)

Status words: **keeps passing** = the claiming test is unchanged or only renamed; **deferred** = Board level, host gate reports it deferred as before; **re-targeted** = the test keeps its ID but reads a renamed observable; **retired** = test removed, ID keeps its criterion and loses its test link until the named row restores one; **new** = added by `req.py add` in the named row.

## 1. Rows whose test changes in F1 (merge)

| ID | Level | Today's test | After F1 | Why |
|---|---|---|---|---|
| MM0001, MM0002, MM0004, MM0005, MM0007, MS-L2-02/03/05/06/07, CDH-10 | Board | `test/int/safe_mode_test.py` | same functions in `test/int/mode_manager_test.py` — **deferred**, unchanged assertions | upstream renamed the file (1af2a0c5) |
| MM0011, MM0012 | Unit | `test_ModeManager_StatePersistence.cpp` (8 × `init(0)`) | **keeps passing** after each `init(0)` is followed by `restorePersistentState()` | upstream removed the `init()` override (a477893b); state now restores from the topology, `ReferenceDeploymentTopology.cpp:119` (trial) |
| MM0009, MM0010 | Unit | `test_ModeManager_VoltageDebounce.cpp` | **keeps passing**, unmodified | `reportLowBattery` path untouched |
| REQ-SM-008 | Unit | `test_StartupManager_Persistence.cpp` (11 tests) | re-worded to the **quiescence file only**; the 5 boot-count tests (lines 201, 252, 278, 394 and the boot half of 169/417) are rewritten against upstream's format and claim REQ-SM-011/012 (below) | owner decision 2: boot count is upstream's code exactly |
| AUTH013 | Unit | `test_Authenticate_SequenceNumberStore.cpp` | **retired in F1, restored in F2** as `test_TcSecurityDeframer_SequenceNumberStore.cpp` with the same 8 claims | store moves component |
| CH-L2-05 | Board | `authentication_test.py` | **retired in F1** (owner decision 7). Open item: its observable still exists upstream as `SequenceNumberInvalid` (`TcSecurityDeframer.fpp:52`); `06-followups.md` §3 proposes a re-target instead of a permanent retirement | Authenticate channels/events gone |
| FD-L2-09, FaultManager-8 | Board | `fault_manager_test.py:91` | **deferred**, unchanged (reads faultManager channels) | — |
| FD-L2-01 | Board | `fault_manager_test.py:113` | **deferred**, unchanged | producers 0/1/3 unchanged |
| FD-L2-05 | Board | `fault_manager_test.py:152`, `watchdog_test.py:90` | **deferred**, unchanged; the manual command-loss procedure at `fault_manager_test.py:195-215` is re-worded in F3 | criterion "command loss → safe mode then reboot" is now ModeManager's path (`ModeManager.cpp` trial:554-577) |
| TM-L2-* (telemetry sources) | Board | `telemetry_sources_test.py:431` | **re-targeted**: `ComCcsdsLora.provesRouter.RejectedPackets` / `ComCcsdsUart.provesRouter.RejectedPackets` | `ProvesRouter.cpp:34-40` counts packets rejected for missing authentication, the same observable |
| DriverBoardHandler-1..10, DriverBoardProtocol-1..6, Crc16, TelemetryGate-*, TaskGate-*, TcFrameCorrector-*, PersistedRecord-1..7 | Unit/Board | Cycle B-E tests | **keeps passing, unmodified** (harm table H3) | no upstream overlap |

## 2. Rows added or renumbered (F1 for the tables, tests as noted)

Upstream's sdd tables collide with IDs our tables already claim (`ModeManager/docs/sdd.md` trial:6-35, `StartupManager/docs/sdd.md` trial:175-210). Our IDs are already in the matrix and in test claims, so upstream's rows are renumbered on entry:

| New ID | From upstream | Description | Method / Level | Pass criteria (from observables) | Test |
|---|---|---|---|---|---|
| MM0013 | MM0011 | Enter safe mode with reason COMMAND_LOSS when no packet is routed within COMM_LOSS_TIME | Integration Test / Board | With COMM_LOSS_TIME set to T ≤ 60 s and no uplink for T: `CommandLossDetected` within T+2 s, then `EnteringSafeMode(COMMAND_LOSS)`, BootCount +1 within 60 s (watchdog stop), and after reboot GET_SAFE_MODE_REASON = COMMAND_LOSS | `mode_manager_test.py::test_safe_12_command_loss_triggers_safe_mode_and_reboot` (upstream's `test_safe_09`, renumbered) — deferred |
| REQ-SM-009 | REQ-SM-008 | Hard-coded transmit enable when `DEFAULT_STARTUP_VALUE == 1` | Inspection / Unit | `HardCodedStartup.h:9` reads 0 in this fork; row status Not applicable, reason "gate disabled by build constant" | none |
| REQ-SM-010 | REQ-SM-009 | No `enableTransmit` from the countdown when `DEFAULT_STARTUP_VALUE == 0` | Unit Test / Unit | 3000 run ticks with the fake: `enableTransmit` never called, no `HardcodedRadioEnable` | `test_StartupManager_Persistence.cpp` (new case) |
| REQ-SM-011 | REQ-SM-010 | Implausible boot count from a corrupt file is not propagated | Unit Test / Unit | A file holding a value > 1 000 000 or of the wrong length: exactly one `BootCountCorrupted(raw)` (or none for a short file) and the count reported on the first tick is 1 | rewritten boot-count tests |
| REQ-SM-012 | REQ-SM-011 | Boot count persisted atomically; failed increment retried until stored | Unit Test / Unit; Board part deferred | Host: with the fake refusing `open(OPEN_CREATE)` for N ticks, exactly one `BootCountUpdateFailure`, `.tmp` never renamed over the target, and on tick N+1 the target holds initial+1. Board: MM0013's test asserts BootCount == initial+1 across the watchdog reset | rewritten boot-count tests; `mode_manager_test.py::test_safe_12` |
| REQ-SM-008 (re-worded) | ours | The quiescence start time file is a PersistedRecord (magic `SQS1`, CRC) updated atomically; corrupt/truncated/out-of-range useconds → one `QuiescenceFileInitFailure` and restart from now; missing → silent first boot | Unit Test / Unit | unchanged criterion minus the boot-count clause | existing quiescence tests (lines 222, 295, 324, 342, 371) |
| AUTH013 (kept) | ours | Sequence number persisted as a PersistedRecord (magic `ASN1`) at `SEQ_NUM_FILE_PATH` | Unit Test / Unit | unchanged criterion; file path now the parameter's value (default `/sequence_number.bin`) | F2 |
| FaultManager producer 2 (FD-L2-01/05/09 reasons) | ours | Command-loss reports reach `faultIn` from ModeManager | Unit Test / Unit (component logic); Board rows above | Host: after COMM_LOSS_TIME ticks without `packetRouted`, `faultOut` is called exactly once with (`COMMAND_LOSS`, `MODE_MANAGER`, `CRITICAL`, counter) **before** `enterSafeMode`; with the stub answering OBSERVED the component still enters SAFE_MODE(COMMAND_LOSS) and calls `stopWatchdog` once; answering CLAIMED it does neither | F3: new case in `test_ModeManager_VoltageDebounce.cpp` or a new `test_ModeManager_CommandLoss.cpp` |

Table edits are made only with `scripts/req.py` (`add`, `set`); the prose parts of both sdd files take both sides (`02-resolutions.md`).

## 3. Non-goals

- No change to FaultManager authority (`AUTHORITY_ENABLED` false, mask 0) — Cycle D contract stands.
- No migration of on-disk files: `/boot_count.bin` written by this fork (magic `SBC1`) reads as implausible/short under upstream's raw format and restarts at 1; `//sequence_number.txt` and the fork's `/sequence_number.bin` both restart at 0 with one warning; `/prmDb.dat` has no saved parameters (owner decision 9). All three are ledger notes, not code.
- No rewrite of upstream's `TcSecurityDeframer` beyond the persistence seam (F2); its `DOESNT_EXIST` branch (`TcSecurityDeframer.cpp` trial:201) is dead on Zephyr and disappears with that seam.
- SCALAR model edits are not in the coder's scope (§4).

## 4. Model re-read list (`~/scalar`, orchestrator, after F1 lands)

`src: S3` claims citing files upstream rewrote (analysis §5, 42 lines): `interfaces/I2cTopology.sysml:22,37` (100 kHz becomes true only after #445), `:70,92,102,109,120`, `structure/Thermal.sysml:28,56`, `Avionics.sysml:197-199` (`mux_channel_*` names survive, `label` does not); `structure/FlightSoftware.sysml:59,74` (Zephyr v4.4.1), `:105` (fprime 7d8f579f = v4.3.0), `:183-184,235` (Authenticate/AuthenticationRouter → TcSecurityDeframer/ProvesRouter), `:264-266` (`packetCount` 23), `budgets/DataBudget.sysml:25-40,262` (per-packet channel counts from the F1 fppi: `Security id 6` replaces `Authenticate`, Beacon gains `tcSecurityDeframer.CurrentSequenceNumber` ×2). `structure/Payload.sysml:276` is already superseded by 387374b8 and unaffected.
