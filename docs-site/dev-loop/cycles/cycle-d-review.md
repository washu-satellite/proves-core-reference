# Cycle D review decisions (orchestrator, 2026-09-05) — apply as coder amendments

Plan: `cycle-d-plan/` (README index). Verdict: approved as designed. Runs after Cycle C.

Why it is safe by default: `AUTHORITY_ENABLED=false` and `AUTHORITY_MASK=0` are a double gate; `FaultTable::claims()` is false for every type, so `faultIn` always returns OBSERVED and `run_handler` never reaches an action port. Every producer's existing block executes verbatim when the disposition is OBSERVED or the port is unconnected. `run_handler` locks, ticks, snapshots, unlocks, and only then acts, so the `stopWatchdog → watchdog.faultOut → faultIn` path cannot deadlock on the guarded mutex.

Amendments for the coder:
1. Shadow gate is non-negotiable: any code path that calls `forceSafeMode_out` or `stopWatchdog_out` must be reachable only through `claims()`; the `FaultManager-1` test must cover defaults, INVALID and UNINIT params and every report pattern with zero action calls.
2. Producers: exactly one guarded `faultOut_out` call each, placed beside (never replacing) the existing event or action; `if (!claimed)` wraps only the action lines the plan names (ModeManager counter/entry block; router `reset_watchdog_out` and `SetSafeMode_out`); bookkeeping stays unconditional. Existing ThermalManager and ModeManager host tests must pass unmodified with the stubs' `faultOutConnected` defaulting to false.
3. Packetizer capacity: the new `packet Faults id 9` brings the packet count to 22 = `MAX_PACKETIZER_PACKETS` (`project/config/TlmPacketizerCfg.hpp:19`). Record this in the FaultManager sdd and the ledger; do not add any other packet. All 13 new channels go into `Faults`, none into `Health`.
4. Dispatch table: Cycle B raises `CMD_DISPATCHER_DISPATCH_TABLE_SIZE` to 400; D adds 10 entries (357 total). If B's raise is not present when D starts, apply it (idempotent).
5. Rate group: `faultManager.run` at `rateGroup1Hz` index 20, after all producers, so decisions land in the same tick; no existing member moves.
6. `req.py add` for FaultManager-1..9 only after creating the sdd's `## Requirements` table header; FD-L2 rows are Board-level: claim them only from the deferred `fault_manager_test.py`, never from host tests. The command-loss board test stays `skip` (destructive) with no `verifies`.
7. Known authority-phase delta (LOW_BATTERY via the manager emits `ExternalFaultDetected` instead of `AutoSafeModeEntry`) is documented in the sdd as a follow-up decision, not resolved in this cycle.
8. `fprime-util generate` before build in the clean-path copy; dictionary check: +2 commands, +4 params, +13 channels, +6 events, +1 packet; `git diff --stat lib/` empty.
9. Read discipline, format new C++ before the gate, Findings section in the report, no commits.
