# Cycle C plan — CDR "Data/Command Priority Component", host-provable slice

Repo `$R` = `/Users/jesse-cm/Documents/Documents - Jesse's Mac/scalar-softwarestack/proves-core-reference`, branch `feat/persisted-record` @ 96a0ed7.
Read `$R/CLAUDE.md` first (path has spaces + apostrophe: quote `$R`, no `make`, python = `fprime-venv/bin/python3`).
Governing rules: defaults identical to today; new in-path components are pass-through by default; no `lib/` edits;
Stage 0 is host-only (gtest, F´-free, recorder stubs); target compile only from the clean-path copy; board tests deferred.
Paths below are relative to `$R` unless absolute. `P` = `PROVESFlightControllerReference`. `UT` = `P/test/unit-tests`.

## 1. Scope decisions

| ID | Criterion (verbatim, `scripts/req.py show`) | Method/Level | Decision | Evidence this cycle |
|---|---|---|---|---|
| CH-L2-20 | "For every single-bit flip (N=1) in a valid TC frame the corrector restores the frame and it is forwarded; frames with > N errors are rejected [N TBD by Mission Ops; not implemented]" | Unit Test / Unit | **Implement** `Components.TcFrameCorrector` on the LoRa uplink | host gtest claims CH-L2-20 (Unit level = claimable by unit tests) |
| SC-L2-07 | "Each schedulable task has a command that stops and restarts it with effect within one cycle (watchdog START/STOP, telemetryGate SET_TRANSMIT_STATE); generic ENABLE_TASK/DISABLE_TASK(taskId) [not implemented]" | Integration Test / Board | **Implement** `Components.TaskGate` (ENABLE_TASK/DISABLE_TASK) for 5 sensor-poll tasks | host gtests claim `TaskGate-1..5` only; new int test claims SC-L2-07 (collected, deferred) |
| DH-L2-06 | "comQueue serves EVENTS(0) before FILE(1) before TLM(2) when all are non-empty (ComCcsdsConfig.fpp:31-33); commandable per-packet priority tags [not implemented]" | Inspection / Unit | **Inspection record** clause 1; clause 2 stays not implemented (needs a context-header priority field: `project/config/ComCfg.fpp:38-49` has none; out of scope) | sdd table + `req.py set --status "Inspected 2026-09-05"` |
| DH-L2-09 | "When a comQueue is full the next packet is dropped and exactly one QueueOverflow event is emitted for that queue (latched, ComQueue.cpp:257-265); FreeSpace never reaches 0 in a 70 s run" | Inspection / Unit | **Inspection record** clause 1; clause 2 is a board observation → deferred | same |
| DH-L2-10 | "Discard policy documented as drop-newest with a latched QueueOverflow warning (Svc/ComQueue/ComQueue.cpp:257-265); depths events 50, tlm 1, file 1" | Inspection / Unit | **Inspection record** (whole criterion) | same |
| SC-L2-06 | "instances.fpp priorities rateGroup50Hz(1) < rateGroup10Hz(2) < rateGroup1Hz(3) (lower = higher) and modeManager(4) <= tlmSend(6)" | Inspection / Unit | **Inspection record** (whole criterion) | same |
| SC-L2-05 | "[TBD by Mission Ops: headroom percent]" | Analysis / Board | **Blocked** — not touched | — |

Contradiction with the brief/CDR: "if the deframer reports a CRC mismatch" never happens on this path. `Svc::FrameDetectors::CcsdsTcFrameDetector::detect` already
verifies the FECF (`lib/fprime/Svc/FrameAccumulator/FrameDetector/CcsdsTcFrameDetector.cpp:55-81`) and returns NO_FRAME_DETECTED, so `FrameAccumulator::processRing`
silently rotates one byte (`lib/fprime/Svc/FrameAccumulator/FrameAccumulator.cpp:170-177`); `TcDeframer::InvalidCrc` (`TcDeframer.cpp:99-104`) is unreachable for
accumulator-fed frames. Therefore option (a) "between frameAccumulator and tcDeframer" can never see a corrupt frame. The corrector must sit **before** the accumulator,
on the raw LoRa packet, which is exactly one radio packet per buffer (`lib/fprime-zephyr/fprime-zephyr/Drv/LoRa/LoRa.cpp:162-177`). This needs no lib edit and no
detector replacement (option (b) rejected: replacing the detector weakens byte-wise resync on garbage).

## 2. Design

### 2a. `Components.TcFrameCorrector` = pure codec + thin passive component (LoRa link only)

Pure codec `P/Components/TcFrameCorrector/TcFrameCorrectorCodec.{hpp,cpp}`, namespace `Components::TcFrameCorrection`, `<cstdint>` only (same F´-free discipline as
`P/Components/PersistedRecord/PersistedRecordCodec.cpp`):
- `uint16_t crc16Ccitt(const uint8_t* data, uint32_t len)` — CRC-16/CCITT-FALSE: init 0xFFFF, poly 0x1021, MSB-first, xorout 0. Identical to the lib's
  `Svc::Ccsds::Utils::CRC16::compute` (`lib/fprime/Svc/Ccsds/Utils/CRC16.hpp:43-49`) which wraps `update_crc_ccitt` (`lib/fprime/Utils/Hash/libcrc/lib_crc.c:132-144`,
  table-driven `crc = (crc<<8) ^ tab[(crc>>8) ^ byte]`). Known vector "123456789" → 0x29B1 (verified with the venv python).
- `enum class Result { PASS_THROUGH, VALID, CORRECTED, UNCORRECTABLE }` and
  `Result correctSingleBit(uint8_t* frame, uint32_t len, uint16_t expectedToken, uint16_t& bitIndexOut)`; `constexpr uint32_t MIN_FRAME_BYTES = 7` (TCHeader 5 +
  TCTrailer 2, `lib/fprime/Svc/Ccsds/Types/Types.fpp:45-53`), `constexpr uint32_t MAX_FRAME_BYTES = 252` (`LoRa.hpp:19 MAX_PACKET_SIZE`).
  Algorithm (whole buffer is the frame; O(8·len) LFSR steps + 2 CRC passes, no allocation, no copy, in-place):
  1. `len < MIN || len > MAX` → PASS_THROUGH, bytes untouched.
  2. `S = crc16Ccitt(frame, len-2) ^ BE16(frame[len-2..len))`; `S == 0` → VALID (untouched).
  3. `S == 1<<k` (k in 0..15) → the flip is in the FECF: flip that bit of the trailer → CORRECTED (bitIndex = 8·(len-2) + (15-k)).
  4. Else walk `d = 0..8·(len-2)-1` with `s = 0x1021` (syndrome of the last data bit, verified) and `s = (s<<1) ^ (s&0x8000 ? 0x1021 : 0)` per step; when `s == S`,
     flip data bit `p = 8·(len-2)-1-d` (`frame[p/8] ^= 0x80 >> (p%8)`), then post-check: `BE16(frame[0..2]) == expectedToken` and
     `((BE16(frame[2..4]) & 0x03FF) + 1) == len` and `crc16Ccitt(frame, len-2) == BE16(trailer)`. Pass → CORRECTED(p); fail → un-flip → UNCORRECTABLE.
  5. No match → UNCORRECTABLE, untouched.
  Correctness facts (python-verified, see Findings): CRC-CCITT = (x+1)(x^15+x+1), so all single-bit syndromes are unique and non-zero for frames ≤ 4095 bytes,
  the 16 FECF-bit syndromes are disjoint from data-bit syndromes, and no 2-bit error maps onto a single-bit syndrome (even-parity factor) → 2-bit errors are always
  UNCORRECTABLE; 3+-bit errors may mis-correct but are then caught by the HMAC in `Authenticate`. The post-check (token 0x2044 + exact length) rejects mis-corrected
  garbage. The token is `(1 << TCSubfields::BypassFlagOffset(13)) | ComCfg::SpacecraftId(0x0044)` — same expression as `CcsdsTcFrameDetector.hpp:45-46`.
  Cost: ≤ 2 CRC passes over ≤ 250 bytes + ≤ 2000 LFSR steps ≈ tens of µs on the RP2350 — runs in the Zephyr system workqueue (LoRa RX callback context,
  `lib/zephyr-workspace/zephyr/drivers/lora/loramac_node/sx12xx_common.c:100-113`, `sx126x.c:412-444` `k_work`), so no brute-force loop is allowed in flight code.
  The brute force (8·len candidates × full CRC) is the **test oracle only**.

Thin component `P/Components/TcFrameCorrector/TcFrameCorrector.{fpp,hpp,cpp}` (`passive component TcFrameCorrector`, copy port block from
`P/Components/Authenticate/Authenticate.fpp:51-61`: guarded `dataIn`, output `dataOut`, output `dataReturnOut`, sync `dataReturnIn`, all `Svc.ComDataWithContext`):
- `param CORRECTION_ENABLED: bool default false`. **Default OFF this cycle**: rule "pass-through by default" is explicit; NullPrmDb means the default *is* the flight
  setting (Findings ledger), so flipping it to `true` is a deliberate one-line follow-up gated on the deferred board test (`radio_test`-style int test enabling it via
  `PRM_SET`). The Unit-level criterion is fully proven with OFF-by-default because host tests set the parameter. Recorded as an open item in §6.
- `dataIn_handler`: if param false → `dataOut_out(0, data, context)` unchanged (zero-copy, byte-identical). If true → `correctSingleBit(data.getData(),
  data.getSize(), TOKEN, bit)`; CORRECTED → `log_ACTIVITY_HI_FrameCorrected(bit, len)` + `tlmWrite_CorrectedFrames(++n)`; UNCORRECTABLE →
  `log_WARNING_LO_FrameUncorrectable(len) throttle 5` + `tlmWrite_UncorrectableFrames(++m)`; VALID/PASS_THROUGH → nothing. **Always** forward on `dataOut`
  (uncorrectable frames are forwarded unchanged so the accumulator drops them exactly as today).
- `dataReturnIn_handler` → `dataReturnOut_out(0, data, context)` (mirror `Authenticate.cpp:381-383`, `TcDeframer.cpp:114-116`). Invariant: every buffer in on
  `dataIn` leaves once on `dataOut`; every buffer in on `dataReturnIn` leaves once on `dataReturnOut`. The buffer is owned by `lora` (allocated in `LoRa::receive`,
  `LoRa.cpp:165`, freed on `lora.dataReturnIn` → `deallocate_out`, `LoRa.cpp:158-160`); in-place bit flips are legal (non-const `Fw::Buffer::getData()`).
- Events `FrameCorrected(bitIndex: U16, frameLength: U16) severity activity high`, `FrameUncorrectable(frameLength: U16) severity warning low throttle 5`;
  telemetry `CorrectedFrames: U32`, `UncorrectableFrames: U32`. Token constant from `Svc/Ccsds/Types/FppConstantsAc.hpp` + `config/FppConstantsAc.hpp`
  (includes as `CcsdsTcFrameDetector.cpp:9,14`), CMake `DEPENDS Svc_Ccsds_Types` (as `lib/fprime/Svc/Ccsds/TcDeframer/CMakeLists.txt`).
- UART link (`ComCcsdsUart`) is **not** covered: `comDriver` delivers arbitrary byte chunks (10 Hz `schedIn`), frame boundaries only exist after accumulation. Documented.

### 2b. `Components.TaskGate` — generic ENABLE_TASK/DISABLE_TASK(taskId)

One passive component `P/Components/TaskGate/TaskGate.{fpp,hpp,cpp}` (rather than per-component ENABLED params: one dictionary change, one test, one stub, and it
matches the CDR's "taskId, enabled tags, ENABLE_TASK(taskId)/DISABLE_TASK(taskId)"):
- `enum SchedTask { IMU = 0, POWER_MONITOR = 1, ADCS = 2, THERMAL = 3, FS_SPACE = 4 }` (doc comment: "ordinal == schedIn/schedOut port index"),
  `constant NUM_TASKS = 5`, `sync input port schedIn: [NUM_TASKS] Svc.Sched`, `output port schedOut: [NUM_TASKS] Svc.Sched`.
- `sync command ENABLE_TASK(task: SchedTask)`, `sync command DISABLE_TASK(task: SchedTask)` (pattern `Watchdog.fpp:5-10`, `TelemetryGate.fpp:24`); both idempotent,
  return OK, emit `TaskEnabled(task)` / `TaskDisabled(task)` (activity high), write `TasksEnabledMask: U32` (bit i = task i enabled; default 0x1F).
- `schedIn_handler(portNum, context)`: `if (m_enabled[portNum]) schedOut_out(portNum, context); else ++m_gated, tlmWrite_GatedRuns(m_gated)`. Passive + sync ⇒ a
  command latches before the next 1 Hz tick ("effect within one cycle"). State is RAM-only, defaults all ENABLED after reboot (safe direction).
- Gated tasks (all pure sensor-poll/telemetry, no actuator state): `imuManager.run` (`ImuManager.cpp:43-55`; DetumbleManager reads the IMU through its own ports,
  `topology.fpp:352-354`, not via `run`), `powerMonitor.run` (`PowerMonitor.cpp:28-38`; ModeManager samples voltage directly, `topology.fpp:458`), `adcs.run`
  (`ADCS.cpp:24-31`), `thermalManager.run` (`ThermalManager.cpp:24-…`, events only), `fsSpace.run` (`FsSpace.cpp:27-35`).
- Excluded (not wired through the gate, hence uncommandable by design): `watchdog.run` (own START/STOP; gating = reboot in ≈26 s), `modeManager.run` (safety
  authority), `startupManager.run` (boot sequencing), `burnwire.schedIn`/`antennaDeployer.schedIn` (timed burns — a gate could leave a burn on), `detumbleManager.run`
  (50 Hz actuator loop), `telemetryDelay.runIn` (already gated by TelemetryGate), `comQueue.run`/`commsBufferManager.schedIn`/`authenticationRouter.run`/
  `$health.Run`/`fileDownlink.Run`/`payloadBufferManager.schedIn` (comms and health plumbing), everything on the 10 Hz group (drivers, sequencers, aggregator).

### 2c. Inspection records (DH-L2-06/09/10, SC-L2-06)

Deliverable = a `## Inspection records` section (table: ID | clause | evidence file:line | date) in `P/ComCcsdsLora/docs/sdd.md` for DH-L2-06/09/10 and in
`P/Components/TaskGate/docs/sdd.md` (scheduler home) for SC-L2-06, plus `req.py set --status "Inspected 2026-09-05" --reason "<pointers>"` (spec Part E item 9;
`req.py` accepts any non-Pass/Fail status, `scripts/req.py:206-213`; the RTM renders it as `📋 …` when no test is linked, `scripts/generate_rtm.py:250-253`).
Host test feasibility: `Svc::ComQueue` is not host-buildable (`lib/fprime/Svc/ComQueue/ComQueue.hpp:10-16` pulls `Fw/Buffer`, `Fw/Com/ComBuffer`, the autocoded
base, `Os/Mutex`) → no behaviour test. The `.fpp` constants are not includable either (autocoded). A text tripwire gtest that greps the config files is feasible
and cheap; it pins the evidence but observes configuration, not service order, so it **must not carry `verifies`** (CLAUDE.md claim rule). Optional; listed in §4D.

## 3. Harm table

| Concern | Guarantee | Where proven |
|---|---|---|
| Uplink byte-identity when disabled | `dataIn_handler` forwards the same `Fw::Buffer` object, no copy, no byte touched | `test_TcFrameCorrector_Component.cpp` DisabledIsByteIdenticalPassThrough |
| Buffer ownership on the LoRa chain | 1 in → 1 out on both directions; `lora.dataReturnIn` still reached via `tcFrameCorrector.dataReturnOut` | same file, ReturnPathForwardsSameBufferAndContext; topology diff §4C |
| CPU bound in sysworkq context | O(8·len) LFSR + 2 CRC passes, `MAX_FRAME_BYTES = 252` cap, no stack buffers, no heap | codec design §2a; assert no loop over candidates × CRC in flight code (review item) |
| Uncorrectable/garbage packets | forwarded unchanged → accumulator drops byte-wise exactly as today | component test UncorrectableForwardedUnchanged |
| Default states | `CORRECTION_ENABLED=false`; TaskGate mask 0x1F (all enabled) | codec/component tests; TaskGate DefaultForwardsEveryPort |
| No `lib/` edits | corrector precedes the accumulator; detector untouched | `git diff --stat lib/` empty |
| Topology diff limited | `topology.fpp`: replace 2 lines (203-204) with 4 for the corrector; replace 5 lines (278,282,286,288,289) with 10 for TaskGate; nothing else moves | §4C |
| Telemetry packet size | 4 new U32 channels go into `HealthAuxiliary` (id 4, 3 channels today, `ReferenceDeploymentPackets.fppi:149-154`); `TlmPacketizer` FW_ASSERTs `packetLen <= FW_COM_BUFFER_MAX_SIZE` at init (`lib/fprime/Svc/TlmPacketizer/TlmPacketizer.cpp:88`) — an oversized packet is a boot crash, so never add to `Health` (id 2, 15 channels) | coder verifies with the clean-path build's dictionary |
| Dictionary drift | every new fpp item listed in §4F | fpp-to-dict in the clean-path build |

## 4. File-by-file steps

### 4A. TcFrameCorrector (new dir `P/Components/TcFrameCorrector/`)
1. `TcFrameCorrectorCodec.hpp/.cpp` — §2a functions; header comment cites CRC16.hpp:43-49 and the syndrome facts; `<cstdint>` only.
2. `TcFrameCorrector.fpp` — component per §2a (ports, param, 2 events, 2 channels, standard AC ports incl. `param get/set`, `command reg/recv/resp` are NOT needed
   unless commands are added — keep none; `time get`, `text event`, `event`, `telemetry`, `param get`, `param set`).
3. `TcFrameCorrector.hpp/.cpp` — `final` class (shape `TelemetryGate.hpp:9-13`); private `dataIn_handler`, `dataReturnIn_handler`; members `m_corrected`,
   `m_uncorrectable` (U32). Token: `static constexpr U16 EXPECTED_TOKEN = (1u << Svc::Ccsds::TCSubfields::BypassFlagOffset) | ComCfg::FppConstant_SpacecraftId::SpacecraftId;`.
4. `CMakeLists.txt` — `register_fprime_library(AUTOCODER_INPUTS TcFrameCorrector.fpp SOURCES TcFrameCorrector.cpp TcFrameCorrectorCodec.cpp DEPENDS Svc_Ccsds_Types)`.
5. `docs/sdd.md` — TelemetryGate sdd shape (`P/Components/TelemetryGate/docs/sdd.md:1-84`): purpose, placement (`lora.dataOut → tcFrameCorrector → frameAccumulator`),
   why it precedes the accumulator (§1 contradiction), algorithm + limits (N=1, ≤252 B, LoRa only, default OFF and why), ports/params/events/telemetry tables,
   `## Requirements` table created **only** via `req.py add --group TcFrameCorrector` after the sdd exists with an empty `## Requirements` heading — rows:
   TcFrameCorrector-1 (every single-bit flip of a valid frame is restored in place and forwarded; Unit Test/Unit; criteria "exhaustive over all 8·len bit
   positions for len ∈ {7,16,64,252}: result CORRECTED, bytes == original, bitIndex == flipped position"), -2 (2-bit errors rejected unchanged; "all C(8·len,2)
   pairs for len=24: UNCORRECTABLE and bytes unchanged"), -3 (disabled = byte-identical pass-through, zero events), -4 (ownership: each dataIn buffer forwarded
   exactly once, each dataReturnIn returned exactly once with the same context), -5 (frames outside [7,252] bytes or failing the post-check are forwarded
   unchanged), `## Change Log` row "Sep 2026 | Initial version (CH-L2-20, N=1)".
6. `P/Components/CMakeLists.txt` — insert `add_fprime_subdirectory("${CMAKE_CURRENT_LIST_DIR}/TcFrameCorrector/")` between StartupManager (l.27) and TelemetryGate (l.28)
   (alphabetical: TaskGate goes before TcFrameCorrector).

### 4B. TaskGate (new dir `P/Components/TaskGate/`)
1. `TaskGate.fpp` — §2b (enum, constant, port arrays, 2 commands, 2 events, 2 channels; standard AC ports incl. command reg/recv/resp; no params).
2. `TaskGate.hpp/.cpp` — `bool m_enabled[NUM_TASKS]` init true; `U32 m_gated`; `schedIn_handler`, `ENABLE_TASK_cmdHandler`, `DISABLE_TASK_cmdHandler` (guard
   `task.e < NUM_TASKS` with FW_ASSERT — dictionary validation already rejects other values), helper `maskTlm()`.
3. `CMakeLists.txt` — `register_fprime_library(AUTOCODER_INPUTS TaskGate.fpp SOURCES TaskGate.cpp)`.
4. `docs/sdd.md` — purpose, task table (enum → instance.port → topology line), excluded-task table with reasons (§2b), commands/events/telemetry, `## Inspection
   records` (SC-L2-06 evidence, §4G), `## Requirements` via `req.py add --group TaskGate`: TaskGate-1 (default forwards every port with context unchanged),
   -2 (DISABLE_TASK gates the very next tick on that port only; OK response; TaskDisabled event), -3 (ENABLE_TASK restores forwarding on the next tick),
   -4 (GatedRuns == exact number of gated ticks; TasksEnabledMask reflects each command), -5 (commands idempotent, always OK). All Unit Test/Unit. Change Log row.
5. `P/Components/CMakeLists.txt` — `add_fprime_subdirectory(".../TaskGate/")` after StartupManager (l.27).

### 4C. Deployment wiring (`P/ReferenceDeployment/Top/`)
1. `instances.fpp` — after `picoTempManager` (l.245): `instance taskGate: Components.TaskGate base id 0x1007A000` and
   `instance tcFrameCorrector: Components.TcFrameCorrector base id 0x1007B000` (passive, no phases).
2. `topology.fpp` — add `instance taskGate`, `instance tcFrameCorrector` to the instance list (after l.120). In `CommunicationsRadio` replace l.203-204 with:
   `lora.dataOut -> tcFrameCorrector.dataIn`, `tcFrameCorrector.dataOut -> ComCcsdsLora.frameAccumulator.dataIn`,
   `ComCcsdsLora.frameAccumulator.dataReturnOut -> tcFrameCorrector.dataReturnIn`, `tcFrameCorrector.dataReturnOut -> lora.dataReturnIn`.
   In `RateGroups` replace l.278 `[6] -> imuManager.run` with `[6] -> taskGate.schedIn[Components.SchedTask.IMU]` + `taskGate.schedOut[Components.SchedTask.IMU] -> imuManager.run`;
   likewise l.282 (`[10]` fsSpace → FS_SPACE), l.286 (`[15]` powerMonitor → POWER_MONITOR), l.288 (`[17]` adcs → ADCS), l.289 (`[18]` thermalManager → THERMAL).
   Enum-as-port-index syntax precedent: `topology.fpp:147`. `ComCcsdsLora/ComCcsds.fpp` and `ComCcsdsUart/ComCcsds.fpp` are not touched.
3. `ReferenceDeploymentPackets.fppi` — in `packet HealthAuxiliary` (l.149-154) add `ReferenceDeployment.taskGate.TasksEnabledMask`, `ReferenceDeployment.taskGate.GatedRuns`,
   `ReferenceDeployment.tcFrameCorrector.CorrectedFrames`, `ReferenceDeployment.tcFrameCorrector.UncorrectableFrames` (CLAUDE.md trap; nothing to `omit`).

### 4D. Host tests (`UT/`)
1. Stubs in `UT/support/`:
   - `PROVESFlightControllerReference/Components/TcFrameCorrector/TcFrameCorrectorComponentAc.hpp` — recorder base (pattern `…/TelemetryGate/TelemetryGateComponentAc.hpp`):
     defines minimal `Fw::Buffer` (`U8* getData()`, `SizeType getSize()`, `setData/setSize`, `isValid`) and `ComCfg::FrameContext` (POD with `comQueueIndex, apid,
     sequenceCount, vcId, authenticated` mirroring `project/config/ComCfg.fpp:38-49`); pure-virtual `dataIn_handler`, `dataReturnIn_handler`; recorders
     `dataOutCalls` (pointer, size, context copy), `dataReturnOutCalls`, `tlmCorrectedFrames`, `tlmUncorrectableFrames`, `eventsFrameCorrected` (bitIndex, len),
     `eventsFrameUncorrectable`; test-settable `bool correctionEnabled = false; Fw::ParamValid paramValidity = VALID;` behind `paramGet_CORRECTION_ENABLED(Fw::ParamValid&)`
     (pattern `ThermalManagerComponentAc.hpp:78-83`).
   - `Svc/Ccsds/Types/FppConstantsAc.hpp` — `namespace Svc::Ccsds::TCSubfields { constexpr U16 BypassFlagOffset = 13; FrameLengthMask = 0x03FF; }` (mirror `Types.fpp:55-67`).
   - `config/FppConstantsAc.hpp` — `namespace ComCfg { struct FppConstant_SpacecraftId { enum { SpacecraftId = 0x0044 }; }; }` (mirror `ComCfg.fpp:12`; generated
     shape used at `CcsdsTcFrameDetector.hpp:46`). Both include `../../FpTypesStub.hpp`-relative as the existing stubs do.
   - `PROVESFlightControllerReference/Components/TaskGate/TaskGateComponentAc.hpp` — enum stub `SchedTask` (shape of `TelemetryTxState` stub, plus `operator T()` as
     `ThermalManager_TempSensorType`), `static constexpr FwIndexType NUM_TASKS = 5`, pure-virtual `schedIn_handler`, `ENABLE_TASK_cmdHandler`, `DISABLE_TASK_cmdHandler`;
     recorders `schedOutCalls` (port, context), `tlmTasksEnabledMask`, `tlmGatedRuns`, `eventsTaskEnabled`, `eventsTaskDisabled`, `cmdResponses`.
2. `UT/CMakeLists.txt` — add libs `tc_frame_corrector_codec` (codec only, include `../../..`), `tc_frame_corrector_component` (TcFrameCorrector.cpp, include
   `support` first then `../../..`, link codec), `task_gate_component`; append all three to the `target_link_libraries` list (l.107-118).
3. `UT/test_TcFrameCorrector_Codec.cpp` — helper `makeFrame(len)`: token 0x2044, `vcIdAndLength = (len-1) & 0x3FF` (VC 0), seq 0, random payload, FECF computed by an
   **independent bitwise** CRC (not the codec's); tests: `Crc16KnownVector` (0x29B1); `ValidFrameUntouched`; `EverySingleBitFlipIsRestored` over len ∈ {7,16,64,252} —
   `RecordProperty("verifies","TcFrameCorrector-1,CH-L2-20")`; `BruteForceOracleAgreesWithSyndrome` (len 32: for each flip, the brute-force loop over all 8·len
   candidates finds exactly one CRC-passing candidate and it equals the codec's bitIndex); `EveryTwoBitErrorIsRejectedUnchanged` (len 24, all pairs) —
   `verifies TcFrameCorrector-2,CH-L2-20`; `TooShortTooLongAndPaddedFramesPassThroughUnchanged` (len 6, 253, and a valid 20-byte frame + 1 pad byte) — `TcFrameCorrector-5`;
   `WrongTokenAfterCorrectionIsRestored` (frame with token 0x2045, one flip) — `TcFrameCorrector-5`.
4. `UT/test_TcFrameCorrector_Component.cpp` — `DisabledIsByteIdenticalPassThrough` (corrupt frame in, same pointer/size/bytes out, 0 events/tlm) — `TcFrameCorrector-3`;
   `EnabledCorrectsAndForwards` (event bitIndex, tlm 1, bytes == original) — `TcFrameCorrector-1,CH-L2-20`; `UncorrectableForwardedUnchanged` (2-bit) — `TcFrameCorrector-2,-5`;
   `ReturnPathForwardsSameBufferAndContext` — `TcFrameCorrector-4`; `ParamInvalidBehavesAsDisabled` (paramValidity INVALID) — `TcFrameCorrector-3`.
5. `UT/test_TaskGate_Component.cpp` — `DefaultForwardsEveryPortWithContext` (`TaskGate-1`), `DisableGatesNextTickOnThatPortOnly` (`TaskGate-2`), `EnableRestoresNextTick`
   (`TaskGate-3`), `GatedRunsAndMaskAreExact` (`TaskGate-4`), `CommandsAreIdempotentAndReturnOk` (`TaskGate-5`). No system ID claims (SC-L2-07 is Board level).
6. Optional tripwire `UT/test_Inspection_ComConfig.cpp` (no `verifies`): reads `project/config/ComCcsdsConfig.fpp`, `Top/instances.fpp`, `project/config/CdhCoreConfig.fpp`
   via a `PROVES_REPO_ROOT` compile definition added in `UT/CMakeLists.txt`; asserts `events = 0`, `file = 1`, `tlm = 2`, depths `50/1/1`, priorities 1/2/3/4, `tlmSend = 6`.
   Skip if it costs more than 30 minutes.

### 4E. Docs plumbing
- `mkdocs.yml`: add `- Task Gate: components/TaskGate.md` under Core Components (after l.72 Watchdog) and `- TC Frame Corrector: components/TcFrameCorrector.md` under
  Communication Components (after l.83 Telemetry Gate).
- `Makefile` `docs-sync` (l.83-…): add the two `cp` lines next to their siblings (l.96 ComDelay, l.101 Watchdog). `make` does not run here: copy by hand with the same
  `cp` commands so `docs-site/components/TaskGate.md` and `TcFrameCorrector.md` exist.
- `P/ComCcsdsLora/docs/sdd.md` is a 4-line stub: append `## Inspection records` (DH-L2-06/09/10 rows, §4G evidence) and a `## Change Log`; do not add a `## Requirements`
  table there (keeps `req.py` group discovery unchanged).

### 4F. Dictionary delta (all new fpp items — every one changes the dictionary)
Components `Components.TcFrameCorrector`, `Components.TaskGate`; enum `Components.SchedTask`; constant `Components.NUM_TASKS`; instances `ReferenceDeployment.tcFrameCorrector`
(0x1007B000), `ReferenceDeployment.taskGate` (0x1007A000); commands `taskGate.ENABLE_TASK`, `taskGate.DISABLE_TASK`; param `tcFrameCorrector.CORRECTION_ENABLED`
(+ autogenerated `_PRM_SET/_PRM_SAVE` commands); events `tcFrameCorrector.FrameCorrected`, `.FrameUncorrectable`, `taskGate.TaskEnabled`, `.TaskDisabled`; channels
`tcFrameCorrector.CorrectedFrames`, `.UncorrectableFrames`, `taskGate.TasksEnabledMask`, `.GatedRuns`; packet `HealthAuxiliary` gains 4 entries.

### 4G. Requirements edits (`fprime-venv/bin/python3 scripts/req.py …`, never by hand)
- Inspection rows — apply ONLY after re-reading the cited lines yourself:
  - `set DH-L2-10 --status "Inspected 2026-09-05" --reason "drop-newest: ComQueue::enqueue lib/fprime/Svc/ComQueue/ComQueue.cpp:246-272 (enqueue fails NO_ROOM_LEFT l.257-258 → new message dropped, QueueOverflow logged once while m_throttle[queue] set l.259-262, throttle cleared when that queue next sends l.369); async buffer-port overflow returns the buffer l.237-240; depths events 50 / tlm 1 / file 1 project/config/ComCcsdsConfig.fpp:24-28"`
  - `set DH-L2-09 --status "Inspected 2026-09-05 (clause 1; clause 2 board-deferred)" --reason "same pointers as DH-L2-10; FreeSpace 70 s run needs a board"`
  - `set DH-L2-06 --status "Inspected 2026-09-05 (clause 1); clause 2 not implemented" --reason "priorities events 0 / file 1 / tlm 2 ComCcsdsConfig.fpp:30-34 applied in ComCcsdsLora/ComCcsds.fpp:14-24 (and UART mirror); ComQueue::configure sorts queues by priority ComQueue.cpp:64-90 and processQueue sends the first non-empty queue in that order, round-robin within equal priority ComQueue.cpp:333-386; no priority field in ComCfg.FrameContext project/config/ComCfg.fpp:38-49"`
  - `set SC-L2-06 --status "Inspected 2026-09-05" --reason "Top/instances.fpp:31-49 rateGroup50Hz 1, rateGroup10Hz 2, rateGroup1Hz 3, modeManager 4; project/config/CdhCoreConfig.fpp:19-24 cmdDisp 4, health 5, events 6, tlmSend 6; ComCcsdsConfig.fpp:18-21 aggregator 7, comQueue 8; Zephyr passes the number straight to k_thread_create (lower = higher, cap 14) lib/fprime-zephyr/fprime-zephyr/Os/Task.cpp:36-48"`
- Criteria text (measurable part unchanged, only the bracketed annotation):
  - `set CH-L2-20 --criteria "For every single-bit flip (N=1) in a valid TC frame the corrector restores the frame and it is forwarded; frames with > N errors are rejected [N=1 implemented on the LoRa link, CORRECTION_ENABLED default false; N>1 TBD by Mission Ops]"`
  - `set SC-L2-07 --criteria "Each schedulable task has a command that stops and restarts it with effect within one cycle (watchdog START/STOP, telemetryGate SET_TRANSMIT_STATE); generic taskGate.ENABLE_TASK/DISABLE_TASK(task) for IMU, POWER_MONITOR, ADCS, THERMAL, FS_SPACE"`
- Do not set Pass/Fail on any row; the RTM derives status from linked tests.

### 4H. Integration tests (collected + linted on host, deferred to the board)
- `P/test/int/task_gate_test.py` (exemplar `telemetry_gate_test.py`, helpers `common.proves_send_and_assert_command`): autouse fixture re-enables all tasks; test
  `@pytest.mark.verifies("SC-L2-07")`: `DISABLE_TASK IMU` → assert `TaskDisabled` event, then `taskGate.GatedRuns` increases within 3 s (raise packet level for
  `HealthAuxiliary` group 5 via `CdhCore.tlmSend.SET_LEVEL` and restore, per the Findings ledger); `ENABLE_TASK IMU` → `TaskEnabled` and GatedRuns stops increasing.
- `P/test/int/tc_frame_corrector_test.py` — sketch only, marked `@pytest.mark.skip("needs a corrupting ground framer")`, **no `verifies`** (spec TP-7: skipped tests are
  procedures, not evidence). Procedure: `PRM_SET CORRECTION_ENABLED true`, send a frame with one flipped bit from a patched framer, expect `FrameCorrected` + command ack.

## 5. Verification
1. `VERIFY_ENV=host scripts/verify.sh` — expected deltas: 3 new binaries `test_TcFrameCorrector_Codec`, `test_TcFrameCorrector_Component`, `test_TaskGate_Component` (+ optional
   `test_Inspection_ComConfig`) all PASSED; int collect gains 1-2 files; pre-commit clean (cpplint: `#ifndef` guards; codespell); RTM: CH-L2-20 → `✅ Unit (passing)`,
   SC-L2-07 → `⏸ Integration (deferred)`, DH-L2-06/09/10 and SC-L2-06 → `📋 Inspected 2026-09-05…`, zero warnings in `build-gtest/rtm-warnings.txt`.
2. Target compile from the clean-path copy (CLAUDE.md rsync + `fprime-util build`; run `fprime-util generate` first because new fpp modules were added). Then in that copy's
   `build-artifacts/` dictionary (`*TopologyDictionary.json`) confirm the §4F items exist and `HealthAuxiliary` lists 7 channels; check flash/RAM delta against 65.7 % / 64.1 %.
3. `git diff --stat -- lib/` is empty; `git diff -- P/ReferenceDeployment/Top/topology.fpp` shows only the §4C hunks.
4. `fprime-venv/bin/python3 scripts/req.py show <ID>` for the 6 rows matches §4G.

## 6. Non-goals, open risks, rows not implemented
- Not implemented: DH-L2-06 clause 2 (per-packet priority tags need a `FrameContext` field + ComQueue behaviour change in lib); SC-L2-05 (TBD number); N>1 correction;
  UART-link correction; persistence of TaskGate state (deliberately RAM-only: safe default is "all enabled").
- Open: flip `CORRECTION_ENABLED` default to `true` after the board procedure (§4H) passes — one-line fpp change + dictionary; record in the cycle retro.
- Risk: LoRa passthrough packetisation. The corrector assumes one TC frame per LoRa packet with no padding (`LoRa.cpp:162-171` forwards the whole payload after the
  4-byte header, `project/config/LoRaCfg.hpp:11`); a padded/multi-frame packet fails the length post-check and passes through unchanged (never harmful, just uncorrected).
- Risk: `FrameUncorrectable` spam from RF noise (PHY CRC is off, see Findings) — throttled 5, counter carries the rate; only emitted when enabled.
- Risk: enum-indexed port arrays in `topology.fpp` for a `Components.SchedTask` enum — precedent exists (`topology.fpp:147`) but from a subtopology enum; if fpp rejects the
  qualified name, fall back to numeric indices with a comment table.
- Risk: `guarded input port dataIn` takes the component mutex in the sysworkq thread; keep `dataIn_handler` allocation-free and short (it is).

## Findings (verified facts, file:line)
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
