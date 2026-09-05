# Cycle B plan — "Expose parameters to be configurable" (TM-L2-02, DH-L2-03/04/05/08)

Repo `$R` = `/Users/jesse-cm/Documents/Documents - Jesse's Mac/scalar-softwarestack/proves-core-reference`, branch `feat/persisted-record` @ 96a0ed7.
Clean-path build copy `~/scalar-build/proves-core-reference` (currently at 69e4b75; rsync per CLAUDE.md before building).
Paths below are relative to `$R/PROVESFlightControllerReference/` unless they start with `$R/`, `lib/` or `docs-site/`.
Governing rules: every new parameter defaults to today's compiled-in value; no `lib/` edits; no topology/rate-group edits; no heap; host-testable logic.

## 1. Scope decision per requirement

| ID | Decision | Criterion clause addressed | Evidence this cycle |
|---|---|---|---|
| TM-L2-02 | **Implement now** (per-source `COLLECTION_INTERVAL_S` parameter on the four 1 Hz sources: thermalManager, powerMonitor, adcs, imuManager) | "After a collection-interval parameter is set to N s (1..60), that source's channel updates are spaced N +/-1 s over 5 consecutive updates" | Unit: decimation + range fallback proven on host with recorder stubs (component IDs). Board: `test/int/collection_interval_test.py` written, DEFERRED (claims TM-L2-02) |
| DH-L2-03 | **Blocked** — the only telemetry storage buffers are `Svc::BufferManager` pools and `Svc::ComQueue` depths, both library code configured exactly once at boot (`lib/fprime/Svc/BufferManager/BufferManagerComponentImpl.cpp:41-58` `setup()` guarded by `m_setup`; `lib/fprime/Svc/ComQueue/ComQueue.cpp:53` `configure()` called from the `configComponents` phase, `ComCcsdsLora/ComCcsds.fpp:28`). No project component owns a telemetry buffer (CameraHandler's `m_lineBuffer[128]`/`m_protocolBuffer` are payload-protocol scratch, `Components/CameraHandler/CameraHandler.hpp:115,124`). | "reported capacity equals the new size" — capacity (count) is already reported by `TotalBuffs` (`lib/fprime/Svc/BufferManager/Telemetry.fppi:2`), but no size can be set | none; row stays "not implemented" |
| DH-L2-04 | **Blocked** for the same reason; a validation function with no caller cannot honestly claim "rejected with VALIDATION_ERROR and the previous size is retained" (repo rule: a test claims an ID only if it observes that ID's observable). Do NOT write dead policy code. | — | none |
| DH-L2-05 | **Blocked** (no runtime-configurable buffer to apply). The validate-then-commit pattern this row needs is exercised in this cycle by the interval parameter (fallback-to-default on invalid), so the coder pattern exists for the storage cycle. | — | none |
| DH-L2-08 | **Blocked** — there is no on-board telemetry store: TlmPacketizer keeps only the latest value per channel (`lib/fprime/Svc/TlmPacketizer/TlmPacketizer.cpp:328-370`), comQueue TLM depth is 1 (`project/config/ComCcsdsConfig.fpp:26`), no DataProducts (dictionary `records`=0, `containers`=0), FsSpace only reports free space (`Components/FsSpace/FsSpace.fpp`). A retention parameter has no consumer. | — | none |

Recommendation for the four DH rows: one follow-on design item, "project `TelemetryStore` component (static RAM ring of packets; `SET_CAPACITY`/`SET_RETENTION_S` commands with validate-then-commit; capacity/retention telemetry)". That single component would satisfy DH-L2-03/04/05/08 together. Out of scope here (memory budget: RAM 64.1% at 96a0ed7).

Persistence decision: **RAM-only** for `COLLECTION_INTERVAL_S`. `X_PRM_SET` latches into component RAM immediately (F´ generated code; same mechanism the int tests already use for `downlinkDelay.DIVIDER_PRM_SET`, `test/int/conftest.py:110`); reboot restores the compiled default 1 s, which is the safe direction. No criterion in this package demands reboot survival, so PersistedRecord is not used.

## 2. Design (TM-L2-02)

### 2.1 Why per-source decimation, not rate groups or `telemetryDelay`
- Rate-group membership is compile-time (`ReferenceDeployment/Top/topology.fpp:246-292`), not runtime — rejected.
- `telemetryDelay` (`Utilities.RateDelay`, `lib/fprime-extras/FprimeExtras/Utilities/RateDelay/RateDelay.fpp:13`) already has `param DIVIDER: U8 default 29` and a generated `ReferenceDeployment.telemetryDelay.DIVIDER_PRM_SET` (in the built dictionary). It sets the *downlink* period for every packet at once (period = DIVIDER+1 ticks, `RateDelay.cpp:24-40`), not a per-source collection interval, has no telemetry of the effective value, and lives in `lib/` — reused by the board test only (see 2.6), not modified.
- Sources sample inside their `run_handler` on the 1 Hz group (`topology.fpp:278,286,288,289`); the driver port handlers write the channels (`Components/Drv/Tmp112Manager/Tmp112Manager.cpp:78`, `Ina219Manager.cpp:45,63,81`, `Veml6031Manager.cpp:79`, `PicoTempManager.cpp:35`, `Components/ImuManager/ImuManager.cpp:81,108,159`). Decimating the four run handlers therefore decimates exactly "that source's channel updates". Host-testable, no topology change.

### 2.2 Shared helper (F´-free, header-only)
- `Components/RunInterval/RunInterval.hpp` (new dir, header only, no CMakeLists — the project root is already on the include path, cf. `Components/TelemetryGate/TelemetryGate.cpp:9`). `#ifndef` guard per `cpplint.cfg`.
- Class `Components::RunInterval`: `bool due(U8 intervalTicks)` — first call ever returns true (first tick runs, as today); afterwards returns true when ticks since the last true call >= intervalTicks. `intervalTicks` 0 is treated as 1. A decrease takes effect on the next tick; an increase extends the current gap. `static U8 effective(U8 requested, bool valid)` → requested if valid and 1 <= requested <= 60, else 1 (`DEFAULT_INTERVAL_S = 1`, `MAX_INTERVAL_S = 60` constexpr in the header).
- No heap, two integer members.

### 2.3 Per-component surface (identical in the four components)
| Component | `.fpp` param (id) | Default (= today) | Telemetry | Event | Extra ports needed |
|---|---|---|---|---|---|
| ThermalManager | `COLLECTION_INTERVAL_S: U8 default 1 id 4` | run every tick (`topology.fpp:289`, 1 Hz) | `CollectionIntervalS: U8 update on change` | `CollectionIntervalRejected(requested: U8) severity warning low format "..." throttle 5` | none (`ThermalManager.fpp:55-79` already has cmd/param/tlm ports) |
| PowerMonitor | `COLLECTION_INTERVAL_S: U8 default 1 id 0` | 1 Hz (`topology.fpp:286`) | same | same | add `param get port prmGetOut` / `param set port prmSetOut` (absent, `PowerMonitor.fpp:54-78`) |
| ADCS | `COLLECTION_INTERVAL_S: U8 default 1 id 0` | 1 Hz (`topology.fpp:288`) | same | same | add `command reg/recv/resp` ports AND `param get/set` ports (ADCS has neither, `ADCS.fpp:12-26`); generated `_PRM_SET/_PRM_SAVE` need the command ports |
| ImuManager | `COLLECTION_INTERVAL_S: U8 default 1 id 4` | 1 Hz (`topology.fpp:278`) | same | same | none (`ImuManager.fpp:149-178`) |

Range and validation rule: valid values 1..60 s. `X_PRM_SET` cannot return an error (generated handler always stores and acks OK), so validation is **fallback-to-default**: `effective = RunInterval::effective(paramGet_COLLECTION_INTERVAL_S(valid), valid)`; an out-of-range or INVALID/UNINIT value yields 1 s and one throttled `CollectionIntervalRejected` warning. Enforced in `parameterUpdated(FwPrmIdType id)` (override pattern: `Components/ComDelay/ComDelay.cpp:22-35`, decl `ComDelay.hpp:30`) which recomputes the cached effective value and emits the event; `run_handler` never reads the param directly on the hot path (cached `m_interval_s`, initialised to 1 in the constructor so a never-set param behaves as today; `loadParameters()` at boot (`ReferenceDeployment/Top/ReferenceDeploymentTopology.cpp:120`) triggers `parameterUpdated` and refreshes it).
"Takes effect only after OK" (DH-L2-05 pattern, applied here): the cached effective value changes only inside `parameterUpdated`, which the generated code calls after the store succeeds; a rejected value leaves the cache at the default rather than an arbitrary value.
Telemetry of the effective value: `tlmWrite_CollectionIntervalS(m_interval_s)` on every *executed* run (channel is `update on change`, so it does not add packet sends).

### 2.4 Per-component run-handler change
- ThermalManager `run_handler` (`ThermalManager.cpp:24-49`): early-return unless `m_interval.due(m_interval_s)`; otherwise unchanged body + tlm write. Threshold evaluation therefore also runs every N s (documented consequence).
- ADCS `run_handler` (`ADCS.cpp:24-31`): same gate.
- ImuManager `run_handler` (`ImuManager.cpp:43-58`): same gate around the three reads and the ODR reconfiguration check. Caveat: DetumbleManager reads the IMU ports directly at 50 Hz in its sensing/actuating states (`DetumbleManager.cpp:543,598` via `topology.fpp:352-354`), and those port handlers write the same channels, so the IMU interval governs the channels only while detumble is idle.
- PowerMonitor `run_handler` (`PowerMonitor.cpp:28-45`): same gate. **Required companion change**: the energy integrators drop any `dt_s >= 10.0` (`PowerMonitor.cpp:97,127`), so an interval >= 10 s would freeze `TotalPowerConsumption/Generated`. Replace the literal with `maxAccumulationDt(m_interval_s)` = `max(10.0, 2.0 * m_interval_s)`; at the default interval this is 10.0, identical to today.

### 2.5 Packet-set lines (`ReferenceDeployment/Top/ReferenceDeploymentPackets.fppi`)
Add each channel to the packet that already carries that source's data, so the effective interval downlinks with the data it governs:
- `ReferenceDeployment.thermalManager.CollectionIntervalS` → packet `Thermal id 12 group 3` (line ~88-99)
- `ReferenceDeployment.powerMonitor.CollectionIntervalS` → packet `PowerMonitor id 11 group 2` (line ~56-62)
- `ReferenceDeployment.adcs.CollectionIntervalS` → packet `LightSensor id 13 group 2` (line ~64-72)
- `ReferenceDeployment.imuManager.CollectionIntervalS` → packet `Imu id 7 group 2` (line ~38-46)

### 2.6 Board test design (written now, runs later)
`test/int/collection_interval_test.py`, exemplar pattern `test/int/telemetry_gate_test.py` + `telemetry_sources_test.py:163-195` (subhistory capture, SET_LEVEL restore). Fixture: `CdhCore.tlmSend.SET_LEVEL 3` (Thermal packet is group 3; do not use 6 to limit link load), `ReferenceDeployment.telemetryDelay.DIVIDER_PRM_SET 0` (packetizer runs every tick, so packet spacing tracks source spacing — TlmPacketizer sends a packet only when a member channel updated, `TlmPacketizer.cpp:341`, and stamps it with the latest channel write time, `:237`); teardown restores `DIVIDER_PRM_SET 29`, `SET_LEVEL 1`, `COLLECTION_INTERVAL_S_PRM_SET 1`. Tests: (1) set thermalManager interval 5, assert `thermalManager.CollectionIntervalS == 5`, then 5 consecutive `tmp112Face0Manager.Temperature` receipts spaced 4..6 s; (2) set 0 → `CollectionIntervalRejected` event and `CollectionIntervalS == 1`; (3) restore 1 → spacing <= 2 s. Marks: `@pytest.mark.verifies("TM-L2-02", "ThermalManager-1", "ThermalManager-2")`. No `uart_only` (nothing severs RF).

## 3. Harm table

| Item | Default-equals-today proof | Reboot | Memory / other |
|---|---|---|---|
| `RunInterval::due(1)` | returns true on every tick → run handlers execute the same call sequence as now (`ThermalManager.cpp:24-49`, `ADCS.cpp:24-31`, `PowerMonitor.cpp:28-45`, `ImuManager.cpp:43-58`); unit test `RunInterval.DefaultRunsEveryTick` pins it | n/a | 2 integer members per component, no heap |
| `COLLECTION_INTERVAL_S default 1` (x4) | `m_interval_s` constructor-initialised to 1; `parameterUpdated` with the default yields 1; INVALID/UNINIT → 1 (mirrors `RateDelay.cpp:34-36` fallback) | RAM-only: reboot restores 1 s (safe direction); `PRM_SAVE` is NOT required and NOT tested | 4 PrmDb-registered params; `X_PRM_SET/_PRM_SAVE` add 8 opcodes → 347 of `CMD_DISPATCHER_DISPATCH_TABLE_SIZE = 350` (`project/config/CommandDispatcherImplCfg.hpp:14`, dictionary has 339 today). Headroom 3 — flag to Mission Ops |
| PowerMonitor dt guard `max(10, 2*interval)` | interval 1 → 10.0, bit-identical to `PowerMonitor.cpp:97,127` | — | unit test pins accumulation at interval 1 and at 30 |
| `CollectionIntervalS` channels (x4) | `update on change`, written on executed runs only → first run writes once, then silent; +1 byte in packets 7/11/12/13 | — | none |
| `CollectionIntervalRejected` events | emitted only on an out-of-range/invalid set; throttle 5 | — | none |
| ADCS/PowerMonitor gain cmd/param ports | auto-wired by `command connections instance CdhCore.cmdDisp` / `param connections instance FileHandling.prmDb` (`topology.fpp:126,132`); no hand wiring | — | dictionary changes (accepted) |
| Not touched | `topology.fpp`, `instances.fpp`, rate groups, `lib/`, ComCcsds configs, PersistedRecord | — | — |
Behavioural consequence to document (not harm at default): with a long thermal interval, `TemperatureAbove/BelowThreshold` detection latency grows to N s (TM-L2-08 / FD-L2-03 criteria assume 1 evaluation per second only at the default).

## 4. File-by-file steps

Step 0 — requirements first (tool only, never hand-edit tables):
- `fprime-venv/bin/python3 scripts/req.py add --group ThermalManager --id ThermalManager-1 --description "run shall perform the sensor sweep every COLLECTION_INTERVAL_S seconds (1..60), default 1 s" --method "Unit Test" --level Unit --criteria "Over 12 ticks at interval 3 exactly 4 sweeps occur (ticks 1,4,7,10); at the default interval every tick sweeps"`
- `... add --group ThermalManager --id ThermalManager-2 ... --criteria "An interval of 0 or >60 or an INVALID param yields effective 1 s, one CollectionIntervalRejected event, and CollectionIntervalS telemetry 1"`
- Same pair for `ADCS-1/ADCS-2` (group `ADCS`), `PWR-MON-REQ-008/009` (group `PowerMonitor`; 009 additionally: "TotalPowerConsumption keeps accumulating at interval 30 s"), `ImuManager-1` (Board, Integration Test; no unit claim).
- `req.py set TM-L2-02 --criteria "After a source's COLLECTION_INTERVAL_S is set to N s (1..60), that source's channel updates are spaced N +/-1 s over 5 consecutive updates (telemetryDelay.DIVIDER 0, packet level 3)"` (drops the "[no such parameter exists]" tag; status untouched — the RTM sets it from tests).
- Optional, reason text only: `req.py set DH-L2-03 --reason "Svc::BufferManager/ComQueue are configured once at boot (lib); no project-owned telemetry buffer; needs TelemetryStore design"`; same for DH-L2-04/05; `DH-L2-08 --reason "No on-board telemetry store (TlmPacketizer latest-value only, comQueue tlm depth 1, no DataProducts); needs TelemetryStore design"`.

Step 1 — helper: `Components/RunInterval/RunInterval.hpp` (new; section 2.2).

Step 2 — ThermalManager
- `Components/ThermalManager/ThermalManager.fpp`: after line 16 add the param (id 4); add the telemetry channel and the event (before the "Standard AC Ports" block, ~line 50).
- `ThermalManager.hpp`: include `RunInterval.hpp`; declare `void parameterUpdated(FwPrmIdType id) override;`, members `RunInterval m_interval; U8 m_interval_s;`.
- `ThermalManager.cpp`: constructor initialises `m_interval_s(1)`; `parameterUpdated` (switch on `PARAMID_COLLECTION_INTERVAL_S`, default `FW_ASSERT(0)` as in ComDelay); gate at the top of `run_handler` (:24) and `tlmWrite_CollectionIntervalS` at its end.
- `Components/ThermalManager/docs/sdd.md`: Parameters table (+row), new "Telemetry" and "Events" rows, prose in Usage (§"Typical Usage" step 2), Change Log row "Sep 2026 | Added COLLECTION_INTERVAL_S (1..60 s, default 1) decimation of the sensor sweep; CollectionIntervalS telemetry; CollectionIntervalRejected event (ThermalManager-1/2)". Do not touch the Requirements table by hand.

Step 3 — ADCS: `ADCS.fpp` (param id 0, channel, event, plus the six standard cmd/param ports copied from `ThermalManager.fpp:63-79`); `ADCS.hpp/.cpp` as Step 2 around `run_handler` (`ADCS.cpp:24-31`); `docs/sdd.md` prose/Change Log (add "Parameters/Telemetry/Events" sections).

Step 4 — PowerMonitor: `PowerMonitor.fpp` (param id 0, channel, event, `param get/set` ports); `.hpp/.cpp` as Step 2 plus `maxAccumulationDt()` replacing the two `10.0` literals (`PowerMonitor.cpp:97,127`); `docs/sdd.md` (Parameters section new, Change Log).

Step 5 — ImuManager: `ImuManager.fpp` (param id 4 after :81, channel after :104, event in Events block); `.hpp/.cpp` gate around `run_handler` (`ImuManager.cpp:43-58`); `docs/sdd.md` Parameters table (:75-84), Telemetry table (:84-96), Change Log; note the detumble caveat (2.4).

Step 6 — packet set: four lines per 2.5 in `ReferenceDeployment/Top/ReferenceDeploymentPackets.fppi`.

Step 7 — host stubs (`test/unit-tests/support/PROVESFlightControllerReference/Components/...`); every stub mirrors exactly what the `.cpp` calls (grep `_out(`, `paramGet_`, `tlmWrite_`, `log_`, `parameterUpdated`):
- `ThermalManager/ThermalManagerComponentAc.hpp` (extend): `typedef U32 FwPrmIdType` if absent in `FpTypesStub.hpp`; `static constexpr FwPrmIdType PARAMID_COLLECTION_INTERVAL_S = 4;` public `U8 collectionIntervalS = 1;` served by `paramGet_COLLECTION_INTERVAL_S(Fw::ParamValid&)` using the existing `paramValidity`; `virtual void parameterUpdated(FwPrmIdType) = 0;` public so tests call it; recorders `std::vector<U8> tlmCollectionIntervalS; std::vector<U8> eventsCollectionIntervalRejected;`.
- `ADCS/ADCSComponentAc.hpp` (new, modelled on the ThermalManager stub): `getNum_visibleLightGet_OutputPorts()=6`, `visibleLightGet_out(portNum, Fw::Success&)` counting reads into `U32 visibleLightReads`, plus the param/tlm/event/parameterUpdated surface above.
- `PowerMonitor/PowerMonitorComponentAc.hpp` (new): six `*Get_out(FwIndexType)` returning test-set F64s and counting calls; `getTime()` returning a test-set `Fw::Time`; `tlmWrite_TotalPowerConsumption/Generated`, `log_ACTIVITY_LO_TotalPowerReset/TotalGenerationReset/TotalPowerConsumptionReading(F32)`, `cmdResponse_out`, three `*_cmdHandler` pure virtuals, plus the param/tlm/event surface.
- `support/Fw/Time/Time.hpp` (new stub, needed because `PowerMonitor.cpp:8` includes `<Fw/Time/Time.hpp>` and uses `getSeconds()/getUSeconds()`): minimal `Fw::Time(U32 s, U32 us)` with those two getters.
- ImuManager: no stub (hard Zephyr dependency: `ImuManager.hpp:10-11` `<zephyr/device.h>`, `<zephyr/drivers/sensor.h>`; board-only).

Step 8 — `test/unit-tests/CMakeLists.txt`: add `adcs_component` and `power_monitor_component` STATIC libs (copy the `thermal_manager_component` block, :78-86) and add both to `target_link_libraries` (:107-118). No lib needed for the header-only helper.

Step 9 — tests (`test/unit-tests/`, auto-globbed):
- `test_RunInterval.cpp`: `DefaultRunsEveryTick`, `FirstTickRuns`, `IntervalNRunsEveryN` (N=3 over 12 ticks → 4), `ZeroTreatedAsOne`, `DecreaseTakesEffectNextTick`, `IncreaseExtendsGap`, `EffectiveFallsBackOutOfRange` (0, 61, 255, INVALID, UNINIT → 1; 1 and 60 pass). `RecordProperty("verifies","ThermalManager-1,ADCS-1,PWR-MON-REQ-008")` only on the tests whose assertions are the decimation observable.
- `test_ThermalManager_CollectionInterval.cpp`: `verifies "ThermalManager-1"`: set `collectionIntervalS=3`, call `parameterUpdated(4)`, 12 ticks → `faceTempReads == 4*5`, `tlmCollectionIntervalS.back()==3`; `"ThermalManager-2"`: 0 / 61 / `paramValidity=INVALID` → 1 rejection event each, every tick sweeps, telemetry 1; `DefaultBehaviourUnchanged`: no param call, 5 ticks → 25 face reads, 20 batt, 5 pico (pins today's behaviour).
- `test_ADCS_CollectionInterval.cpp`: same shape, `ADCS-1/ADCS-2` (`visibleLightReads`).
- `test_PowerMonitor_CollectionInterval.cpp`: `PWR-MON-REQ-008` (reads decimated), `PWR-MON-REQ-009` (interval 30, clock advanced 30 s per executed run at 1 W → `TotalPowerConsumption` grows by ~8.33 mWh per run; interval 1 with 1 s steps grows by ~0.278 mWh per tick exactly as today, and a 12 s gap at interval 1 is still dropped).
- `test/int/collection_interval_test.py` per 2.6 (collected + ruff-linted on host, executed on the rig later).

Step 10 — docs: `docs-site/` component pages are Makefile copies (`$R/Makefile:84-95`; `make` is unusable at this path) — copy the four sdd.md files to `docs-site/components/<Name>.md` by hand with `cp`, then `fprime-venv/bin/python3 scripts/req.py rtm`.

## 5. Verification
- `VERIFY_ENV=host scripts/verify.sh` from `$R`: expect 4 new test binaries passing (`test_RunInterval`, `test_ThermalManager_CollectionInterval`, `test_ADCS_CollectionInterval`, `test_PowerMonitor_CollectionInterval`), existing 8 unchanged, int collect count +3 tests, ruff clean, pre-commit clean (codespell, cpplint header guards, clang-format 120 col), RTM regenerated with ThermalManager-1/2, ADCS-1/2, PWR-MON-REQ-008/009 Pass and TM-L2-02 / ImuManager-1 "deferred (board)". Board rows stay deferred, not unverified.
- Target compile (required: `.fpp` changed): rsync per CLAUDE.md, then `fprime-util build` in `~/scalar-build/proves-core-reference`. Failure modes to expect: fpp-to-dict missing packet line (Step 6), missing standard ports on ADCS/PowerMonitor (Step 3/4), `parameterUpdated` signature mismatch (`FwPrmIdType`), RAM/flash growth (compare against 65.7% / 64.1%).
- Dictionary check in the copy: `build-artifacts/zephyr/fprime-zephyr-deployment/dict/ReferenceDeploymentTopologyDictionary.json` — `parameters` gains 4 `*.COLLECTION_INTERVAL_S` (count 89 → 93), `commands` gains 8 `*_PRM_SET/_PRM_SAVE` (339 → 347), `telemetryChannels` gains 4 `*.CollectionIntervalS`, `events` gains 4 `*.CollectionIntervalRejected`; `telemetryPacketSets` shows the four channels in packets 7/11/12/13.

## 6. Non-goals, risks, rows left "not implemented"
Non-goals: editing `lib/` (RateDelay, BufferManager, ComQueue, TlmPacketizer); runtime resize of any library queue/pool; a telemetry store; persistence of the interval; rate-group/topology edits; CDH-5 (imuManager.MagneticField spacing) — the ImuManager param is delivered, but the board claim waits for a test that first parks DetumbleManager (its 50 Hz port reads rewrite the IMU channels).
Risks: dispatch-table headroom drops to 3 opcodes (350 limit) — the next parameter-bearing component must raise `CMD_DISPATCHER_DISPATCH_TABLE_SIZE`; `PRMDB_NUM_DB_ENTRIES` (F´ default 25, `lib/fprime/Svc/PrmDb/PrmDbImpl.hpp:118`) bounds how many params `PRM_SAVE` can hold — irrelevant to RAM-only use but worth a note to Mission Ops; long thermal intervals lengthen fault-detection latency; board test at `DIVIDER 0`/level 3 raises downlink volume for ~60 s (UART preferred).
Rows left "not implemented" for the report: DH-L2-03, DH-L2-04, DH-L2-05 (library buffers are setup-once, no project-owned telemetry buffer), DH-L2-08 (no on-board telemetry store). All four resolve with the proposed TelemetryStore component.

## Findings (verified facts, file:line)
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
