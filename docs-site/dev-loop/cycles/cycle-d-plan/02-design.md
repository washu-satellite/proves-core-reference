# 02 — Design

## 2a. Shared types — `P/Components/FaultManager/FaultTypes.fpp` (module `Components`)
- `enum FaultType: U8 { NONE=0, FACE_TEMP_HIGH=1, FACE_TEMP_LOW=2, BATT_TEMP_HIGH=3, BATT_TEMP_LOW=4, LOW_BATTERY=5, COMMAND_LOSS=6, WATCHDOG_STOPPED=7, ADCS_UNSTABLE=8 }`
  (8 is the FD-L2-07 hook; bit in every mask = `1 << (type-1)`: 0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80).
- `enum FaultSource: U8 { THERMAL_MANAGER, MODE_MANAGER, AUTH_ROUTER, WATCHDOG, DETUMBLE_MANAGER }`
- `enum FaultSeverity: U8 { WARNING, CRITICAL }`; `enum FaultAction: U8 { NONE, SAFE_MODE, SAFE_MODE_AND_REBOOT }`
- `enum FaultDisposition: U8 { OBSERVED, CLAIMED }`
- `port FaultReport(type: FaultType, source: FaultSource, severity: FaultSeverity, value: F32) -> FaultDisposition`
  (enum-returning port precedent `ModeManager.fpp:23`; cross-module use precedent `AuthenticationRouter.fpp:49` uses `Components.ForceSafeModeWithReason` with no CMake DEPENDS).
- `constant FaultInPorts = 4` — index 0 thermalManager, 1 modeManager, 2 ComCcsdsLora.authenticationRouter, 3 watchdog (comment this in the fpp and in topology).

## 2b. Pure module — `FaultTable.{hpp,cpp}`, namespace `Components::FaultLogic`, `<cstdint>` only (discipline of `PersistedRecordCodec.cpp`)
- Mirror enums as plain `enum Type : uint8_t {...}` etc. with the same numeric values; `FaultManager.cpp` carries `static_assert`s that
  `Components::FaultType::LOW_BATTERY == FaultLogic::LOW_BATTERY` for every value (compile-time alignment; no runtime mapping).
- `struct Policy { uint8_t debounce; Action action; bool sampled; Severity severity; }`; `Policy defaultPolicy(Type)`:

  | Type | sampled | debounce | severity | action (= today's, see 03) |
  |---|---|---|---|---|
  | FACE/BATT_TEMP_HIGH/LOW | no (event) | 1 | WARNING | NONE |
  | LOW_BATTERY | yes (one report per 1 Hz sample) | 10 | CRITICAL | SAFE_MODE → reason LOW_BATTERY |
  | COMMAND_LOSS | no | 1 | CRITICAL | SAFE_MODE_AND_REBOOT → stopWatchdog, then forceSafeMode(EXTERNAL_REQUEST) |
  | WATCHDOG_STOPPED | no | 1 | CRITICAL | NONE (hardware reset already under way) |
  | ADCS_UNSTABLE | no | 1 | WARNING | NONE |
- `struct Report { Type type; Source source; float value; }`, `struct Decision { Type type; Action action; Source source; float value; }`,
  `struct Status { uint32_t reports; uint8_t consecutive; bool confirmed; bool pending; bool reportedThisTick; float lastValue; uint32_t lastTick; }`.
- `class FaultTable`: `setDebounce(Type, uint8_t n)` (0 → 1); `bool report(const Report&, uint32_t tick)` — `reports++`, `consecutive++`,
  `reportedThisTick = true`; when `consecutive >= debounce && !confirmed` → `confirmed = pending = true`, returns true (confirmation edge, once);
  `uint8_t tick(uint32_t tick, Decision* out, uint8_t max)` — for every type: if `pending` emit a Decision with `policy.action` and clear `pending`;
  then for **sampled** types with `!reportedThisTick` → `consecutive = 0; confirmed = false` (re-arm: mirrors `ModeManager.cpp:96-99` and the fact
  that ModeManager stops sampling once in SAFE_MODE, `:80-99`); clear `reportedThisTick`. Event types stay confirmed until `clear()`.
  `void clear()`; `const Status& status(Type) const`; `uint8_t activeMask() const`; `uint32_t totalReports()/totalConfirmed()`;
  `static bool claims(Type, bool authorityEnabled, uint8_t mask)` = `enabled && (mask & bit(type)) && defaultPolicy(type).action != NONE`.
  No side effects besides its own arrays; fixed-size `Status[9]`; no heap.

## 2c. Component — `Components.FaultManager`, **passive** (CDR says active; justification)
- Active would add a Zephyr thread (4 KB stack per `prj.conf`, RAM at 63.9 %), a priority slot, and an async queue that can overflow. The manager's
  periodic work is O(9) table scan per 1 Hz tick; intake must be callable from ≥4 threads (rateGroup1Hz: thermal/modeManager.run/router; modeManager
  thread: none this cycle; cmdDisp: `STOP_WATCHDOG`; EventManager: `fatalHandler → watchdog.stop`). A **guarded** `faultIn` (generated
  `m_guardedPortMutex`, `lock()/unLock()` are protected virtuals of every component base — see 07) gives thread safety with no thread, same pattern as
  `Authenticate.fpp:52`. Upgrade to active later = change `passive`→`active` and the instance line; nothing else.
- Ports: `guarded input port faultIn: [FaultInPorts] Components.FaultReport`; `sync input port run: Svc.Sched`;
  `output port forceSafeMode: Components.ForceSafeModeWithReason`; `output port stopWatchdog: Fw.Signal`. No reboot via ResetManager: today's
  reboot mechanism for command loss is "stop petting" (`AuthenticationRouter.cpp:45-47`), and FatalHandler uses the same (`FatalHandler.cpp:29`).
- Threading contract (deadlock proof): `faultIn_handler` runs under the guarded mutex and only touches the table + counters (no output-port calls
  except `tlmWrite` — TlmPacketizer never calls back). `run_handler` is `sync`: `this->lock(); n = table.tick(...); snapshot counters; this->unLock();`
  and only **then** executes actions. `stopWatchdog_out → watchdog.stop_handler → watchdog.faultOut → faultManager.faultIn` therefore takes a free
  mutex (same thread, lock released). Never call an output action port while holding the lock.
- Disposition contract: `faultIn` returns `CLAIMED` iff `FaultTable::claims(type, authorityEnabled, mask)`; producers keep their direct action
  unless CLAIMED. In shadow the return is always OBSERVED, so every producer executes exactly today's code.
- Params (4 → 8 opcodes): `AUTHORITY_ENABLED: bool default false` (precedent `StartupManager.fpp:49`); `AUTHORITY_MASK: U8 default 0`;
  `DEBOUNCE_LOW_BATTERY: U8 default 10`; `DEBOUNCE_THERMAL: U8 default 1`. Cached in `parameterUpdated(FwPrmIdType)` (exemplar `ComDelay.cpp:22-35`);
  INVALID/UNINIT → shadow (enabled=false, mask=0) and default debounces. Action map is compile-time (`defaultPolicy`) — no params for it this cycle
  (opcode budget, 03 §Dictionary); per-type authority is the mask.
- Commands (2): `CLEAR_FAULTS()` — lock, `clear()`, unlock, `FaultsCleared`, OK; `GET_FAULT_STATUS()` — snapshot under lock, `FaultStatusReport`, OK.
- Events (6): `FaultConfirmed(type: FaultType, source: FaultSource, value: F32)` WARNING_HI throttle 10;
  `FaultActionSuppressed(type: FaultType, action: FaultAction)` WARNING_LO throttle 10 ("shadow mode: would have …");
  `FaultActionTaken(type: FaultType, action: FaultAction)` WARNING_HI; `FaultsCleared()` ACTIVITY_HI;
  `FaultStatusReport(active: U8, detected: U32, confirmed: U32, authority: U8)` ACTIVITY_LO;
  `FaultAuthorityChanged(enabled: bool, mask: U8)` WARNING_HI (from `parameterUpdated` when the effective pair changes).
- Telemetry (13 channels, all `update on change`): `FaultsDetected: U32`, `FaultsConfirmed: U32`, `ActiveFaults: U8` (mask),
  `LastFaultType: FaultType`, `LastFaultSource: FaultSource`, `LastFaultValue: F32`, `ShadowActionsSuppressed: U32`, `ActionsTaken: U32`,
  `AuthorityState: U8` (0 = shadow; else the effective mask), `FaultCountThermal: U32`, `FaultCountLowBattery: U32`, `FaultCountCommandLoss: U32`,
  `FaultCountWatchdogStop: U32`. All go in a new `packet Faults id 9 group 5` (id 9 is free; ≤ 13×8 B ≪ `FW_COM_BUFFER_MAX_SIZE` 233).
- `run_handler` per tick: lock → `tick()` → unlock → for each Decision: `FaultConfirmed`; if `claims()` → execute (SAFE_MODE: `forceSafeMode_out(0,
  reason)`; SAFE_MODE_AND_REBOOT: `stopWatchdog_out(0)` then `forceSafeMode_out(0, EXTERNAL_REQUEST)`; each guarded by `isConnected_*`) +
  `FaultActionTaken`, else (action != NONE) `FaultActionSuppressed` + `ShadowActionsSuppressed++`. Then `tlmWrite` only for values that changed.
- Rate-group slot: **index 20** on `rateGroup1Hz` (free; `ActiveRateGroupOutputPorts = 25`, `P/project/config/AcConstants.fpp:7`), i.e. after
  modeManager[16], thermalManager[18], authenticationRouter[19], so a sample reported at tick t is decided at tick t — same second as today's
  `AutoSafeModeEntry`/`CommandLossFound`. Do not use the free index 12 (would add one tick of latency).

## 2d. Producers (one guarded call each; details and exact lines in 03/04)
| Producer | Where | Report | Uses disposition? |
|---|---|---|---|
| ThermalManager | beside each `log_WARNING_LO_Temperature{Above,Below}Threshold` | FACE/BATT × HIGH/LOW, value = temperature | no (action NONE) |
| ModeManager | first line of the `if (isFault)` block in `run_handler` | LOW_BATTERY, value = voltage (0 if invalid), one per low sample | yes: `if (!claimed)` around the existing counter/entry block |
| AuthenticationRouter (LoRa) | top of `CallSafeMode` | COMMAND_LOSS, value = 0 | yes: `if (!claimed)` around `reset_watchdog_out` and `SetSafeMode_out`; bookkeeping (`update_command_loss_start(true)`, `m_safeModeCalled`) unconditional |
| Watchdog | end of `stop_handler` | WATCHDOG_STOPPED, value = transitions | no (observation) |
All calls: `if (this->isConnected_faultOut_OutputPort(0)) { … }` (unconnected output ports assert; pattern `ModeManager.cpp:499`, `ResetManager.cpp:61`).
