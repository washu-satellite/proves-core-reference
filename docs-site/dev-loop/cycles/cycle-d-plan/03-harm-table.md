# 03 — Harm table: defaults identical to today

Shadow default proof: `AUTHORITY_ENABLED` default false and `AUTHORITY_MASK` default 0 ⇒ `FaultTable::claims()` is false for every type ⇒ `faultIn`
returns OBSERVED and `run_handler` never reaches an output action port. Unit test `FaultManager-1` asserts zero `forceSafeMode_out`/`stopWatchdog_out`
calls over every report pattern with defaults, INVALID and UNINIT params. Producers: every `claimed` local is false when the port is unconnected
(host default) **or** when the manager returns OBSERVED, so the existing block runs verbatim.

## Per-trigger paths (lines that stay; HEAD 78a5d24 numbers — ThermalManager shifts with Cycle B, grep)
| Trigger | Today's path (unchanged lines) | D adds (shadow) | With authority bit set (Board phase) — action identical? |
|---|---|---|---|
| Watchdog stall / `STOP_WATCHDOG` / FATAL | `Watchdog.cpp:25-35` run pets only while `m_run`; `:48-55` stop sets `m_run=false`; `:69-75` cmd calls `prepareForReboot_out` then stop; `topology.fpp:297` router→stop, `:495` fatalHandler→stop; hardware reset ≈26 s | one guarded `faultOut_out(WATCHDOG_STOPPED…)` after `:51` | Action NONE; the manager never stops/starts the watchdog for this type. Identical. |
| Command loss (LoRa router) | `AuthenticationRouter.cpp:137-155` run: timer + `CommandLossFound` + `CallSafeMode()` + `m_safeModeCalled=true`; `:44-51` CallSafeMode: `reset_watchdog_out` (guarded), `update_command_loss_start(true)`, `SetSafeMode_out(EXTERNAL_REQUEST)`; `topology.fpp:466` SetSafeMode→modeManager.forceSafeMode, `:297` reset_watchdog→watchdog.stop | `claimed` computed at the top of CallSafeMode (false in shadow); `:45-47` and `:51` wrapped in `if (!claimed)`; `:49` unconditional | Manager executes `stopWatchdog_out` **then** `forceSafeMode_out(EXTERNAL_REQUEST)` — same two ports, same order, same reason; the watchdog's `WATCHDOG_STOPPED` report is additionally observed. Identical. |
| Low battery (ModeManager) | `ModeManager.cpp:63-131` run (sync, rateGroup1Hz thread): `:81` `isFault = !valid || voltage < entry`; `:83-95` counter, at ≥ debounce `runSafeModeSequence` + `AutoSafeModeEntry` + `enterSafeMode(LOW_BATTERY)`; `:96-99` reset; `:100-125` recovery; `:133-152` forceSafeMode_handler; `:378-…` enterSafeMode (load switches OFF `:484`) | first line inside `if (isFault)`: guarded report, `claimed` false; `:84-95` wrapped in `if (!claimed)` | Manager confirms at the 10th consecutive sample (same tick, slot 20) and calls `forceSafeMode_out(LOW_BATTERY)` → `forceSafeMode_handler` → `runSafeModeSequence` + `enterSafeMode(LOW_BATTERY)`. Mode, reason, entry count, load switches, sequence, recovery (LOW_BATTERY auto-exit `:106-118`) identical. **Delta**: event `ExternalFaultDetected` (`:137`) replaces `AutoSafeModeEntry(LOW_BATTERY, voltage)`, and `forceSafeMode` is async (ModeManager thread) so entry lands one queue hop later (ms). Record in 06; not a Cycle D behaviour (shadow). |
| Thermal thresholds | `ThermalManager.cpp` `evaluateTemperatureThreshold`: events at the two `log_WARNING_LO_*` lines, hysteresis `DEBOUNCE_ERROR` 3 C (`.hpp:27`) | one guarded `faultOut_out` beside each event | Action NONE by policy; identical. |
| Health ping late → FATAL | `HealthComponentImpl.cpp:123` FATAL → `CdhCore.fpp:67` → `FatalHandler.cpp:27-30` → `watchdog.stop` | observed only through the Watchdog report above | Identical. |
| Unintended reboot → SYSTEM_FAULT | `ModeManager.cpp:322,331` at boot | nothing (no producer this cycle) | Identical. |

## Timing (1 Hz group)
- One more member (`RateGroupMemberOut[20]`): `run_handler` = mutex lock, 9-entry scan, unlock, ≤2 port calls (none in shadow), ≤13 on-change
  `tlmWrite`. No I/O, no allocation, no float math beyond a copy. Members before it are untouched; index order keeps every existing member's slot.
- Producer cost: one guarded port call (mutex + ~20 instructions) at most once per tick per producer; ModeManager's call happens only while
  `isFault` (voltage low), i.e. never in nominal operation.
- Board check (deferred): `rateGroup1Hz.RgMaxTime` in the `Health` packet before/after.

## Memory / binary
- `FaultTable`: `Status[9]` ≈ 9×20 B + policies; no heap, no thread, no queue. Component: cached params + counters ≈ 64 B.
- Flash: one component + generated port/enum code — expect a few KB (record FLASH/RAM from the copy's build log in 05; baseline 685824 B / 340080 B after Cycle A).

## Dictionary / opcodes
- +2 commands, +4 params (×2 opcodes) = **+10 dispatch entries**; +13 channels; +6 events; +1 port type; +5 enums; +1 output port on four producers.
- Counts: copy dictionary at 93d28b5 = 339 commands / 89 params; Cycle B adds 8/4 (not landed) → 347/93; D → **357/97**.
  `CMD_DISPATCHER_DISPATCH_TABLE_SIZE` is still 350 (`P/project/config/CommandDispatcherImplCfg.hpp:14`): **raise to 400 if still 350** when D lands
  (idempotent with B's planned raise; cost ≈ 50 entries × ~8 B RAM). Verify in the copy's dictionary after generate.
- `PRMDB_NUM_DB_ENTRIES` = 25 bounds *saved* params only; shadow needs no `PRM_SAVE`. Flag again in 06 for the authority phase.

## Event traffic
- +1 `FaultConfirmed` WARNING_HI per confirmed fault (throttle 10) and +1 `FaultActionSuppressed` WARNING_LO per confirmed fault with a
  non-NONE action (throttle 10, only LOW_BATTERY/COMMAND_LOSS). Thermal crossings already throttle via hysteresis, so worst case doubles a rare
  event; comQueue events depth 50 (`ComCcsdsConfig.fpp:31`). `startup.seq` filters are untouched.

## Topology diff (complete list)
`Top/instances.fpp`: +1 passive instance line. `Top/topology.fpp`: +`instance faultManager`; +1 rate-group line; +1 `connections FaultManager {}`
block with 4 fan-in + 2 action lines. Existing lines `:297, :462-463, :466, :495` unchanged. Fan-in onto `watchdog.stop` and
`modeManager.forceSafeMode` follows the existing precedent (`watchdog.stop` already has two sources). No `lib/` edits anywhere.
