## Findings (verified facts, file:line) — not already in the ledger; merge under "Cycle D planner findings"
- Health FATAL chain: `HLTH_PING_LATE` is FATAL after `FATAL` missed pings (`lib/fprime/Svc/Health/HealthComponentImpl.cpp:119-123`) →
  `events.FatalAnnounce -> fatalHandler.FatalReceive` (`lib/fprime/Svc/Subtopologies/CdhCore/CdhCore.fpp:66-67`) → project `Components/FatalHandler/
  FatalHandler.cpp:27-30` logs and calls `stopWatchdog_out` → `topology.fpp:495 -> watchdog.stop`. Ping entries WARN 3 / FATAL 5 for the three rate
  groups, cmdSeq/payloadSeq/safeModeSeq and FileHandling components (`Top/ReferenceDeploymentTopologyDefs.hpp:60-98`); no project component is pinged.
- `watchdog.stop` already fans in from two outputs (`topology.fpp:297` router `reset_watchdog`, `:495` fatalHandler) — precedent for a third.
- Router command-loss action (`Components/AuthenticationRouter/AuthenticationRouter.cpp:44-51` `CallSafeMode`): `reset_watchdog_out` (guarded by
  `isConnected`), `update_command_loss_start(true)`, `SetSafeMode_out(EXTERNAL_REQUEST)` — reason is EXTERNAL_REQUEST, not LORA; `m_safeModeCalled`
  latch (`:151-153`) makes it once per boot. The router is `module Svc` (`Svc.AuthenticationRouter`, `ComCcsdsLora/ComCcsds.fpp:83`) and includes
  `<zephyr/drivers/rtc.h>` (`AuthenticationRouter.cpp:19`) → not host-buildable.
- ModeManager `run` is a sync port (`ModeManager.fpp:38`) → voltage debounce executes on the rateGroup1Hz thread; `forceSafeMode` is async (`:45`) →
  ModeManager thread. `isFault = !valid || voltage < entry` (`ModeManager.cpp:81`); `valid` is false only when `voltageGet` is unconnected
  (`:497-509`); an INA219 `DeviceNotReady` returns 0 V (`Drv/Ina219Manager/Ina219Manager.cpp:69-72`) and therefore counts as low battery.
- 1 Hz group: index 12 unused (`topology.fpp:271-290`); `ActiveRateGroupOutputPorts = 25` (`P/project/config/AcConstants.fpp:7`; lib default 10,
  `lib/fprime/default/config/AcConstants.fpp:7`) → indices 20-24 free; members are called in index order.
- Generated component bases expose protected `virtual void lock()/unLock()` and a private `Os::Mutex m_guardedPortMutex` (copy build,
  `AuthenticateComponentAc.hpp:870-877,1074`); project guarded-port precedent `Components/Authenticate/Authenticate.fpp:52`; `Os::Mutex` has
  `lock()/unLock()/unlock()` and `Os::ScopeLock` (`lib/fprime/Os/Mutex.hpp:62-64,80`).
- Packet id 9 is free (`ReferenceDeploymentPackets.fppi` uses 1-8, 10-22); "group" in a packet line is the SET_LEVEL level; `FW_COM_BUFFER_MAX_SIZE = 233`
  (`P/project/config/FpConstants.fpp:20`).
- Precedents: `param ARMED: bool default true` (`StartupManager.fpp:49`); enum-returning port `GetSystemMode -> SystemMode` (`ModeManager.fpp:23`).
- `scripts/req.py` discovers component groups only from a heading matching `^##\s+Requirements\s*$` (`req.py:150`), group = component dir name
  (`:147`); `add` needs an existing table (`:273-277`). Watchdog's sdd uses `## 2. Requirements` (`Components/Watchdog/docs/sdd.md:7`) and
  AuthenticationRouter's table is a legacy 4-column one → neither accepts `req.py add`.
- Clean-path copy is at 93d28b5; its dictionary has 339 commands / 89 params / 194 channels / 662 events / 0 records / 0 containers.
  `CMD_DISPATCHER_DISPATCH_TABLE_SIZE` is still 350 in the working tree; Cycle B's +8 opcodes are uncommitted (`git status`: ThermalManager
  `.fpp/.cpp/.hpp`, `Components/RunInterval/RunInterval.hpp`, ADCS/ImuManager/PowerMonitor sdds modified/untracked at 78a5d24).
- `Watchdog.hpp:12-14` includes `<atomic>` and `Fw/Types/OnEnumAc.hpp`; `Watchdog.cpp:9` includes `config/FpConfig.hpp`; no Zephyr includes → host-buildable
  with two stub headers. `Watchdog.cpp:69-75` `STOP_WATCHDOG` calls `prepareForReboot_out` before `stop_handler`; `start_handler` re-arms (`:37-46`).
- ThermalManager/ModeManager recorder stubs have no `faultOut`; the ModeManager stub's `isConnected_*` flags (`ModeManagerComponentAc.hpp:158-176`) are the
  pattern for a `faultOutConnected` default-false flag so existing tests stay unchanged.
- `test_ThermalManager_Thresholds.cpp:81,124,160,185` claims TM-L2-08/FD-L2-03; `test_ModeManager_VoltageDebounce.cpp:122` claims MM0009/MS-L2-08.
- Docs plumbing lines: `mkdocs.yml:72` (Watchdog nav entry), `Makefile:101` (Watchdog cp), `:124` (PersistedRecord cp); `docs-site/components/Watchdog.md` exists.
- `loraRetry` is `Svc.ComRetry` (`Top/instances.fpp:220`; wiring `topology.fpp:207-214`) — FD-L2-08 clause 1 is library behaviour already present.
- `Svc.Health` (`CdhCore.$health`) is a *queued* component drained by `Run` on rateGroup1Hz[2] (`CdhCore.fpp:18-33`, `topology.fpp:274`) — the
  alternative "queued FaultManager" shape has a precedent if the guarded design is ever revisited.
