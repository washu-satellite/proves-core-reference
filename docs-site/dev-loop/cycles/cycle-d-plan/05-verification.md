# 05 — Verification

## Host gate: `VERIFY_ENV=host scripts/verify.sh` (from `$R`, quoted path)
Expected deltas vs. the run before D:
- Unit tests: +3 executables `test_FaultManager_FaultTable`, `test_FaultManager_Component`, `test_Watchdog_FaultReport`, all PASSED; extended
  `test_ThermalManager_Thresholds` and `test_ModeManager_VoltageDebounce` PASSED with every pre-existing case unmodified.
- Pre-commit: expect one Failed run from clang-format reformatting new files, then pass (ledger). Codespell: avoid "static objects"; cpplint: `#ifndef` guards,
  120 col, no unknown NOLINT categories.
- Int collect: `fault_manager_test.py` collects (4 tests, 1 skipped); reported DEFERRED under host, not unverified.
- RTM (`generate_rtm.py --env host`): rows `FaultManager-1..7,-9` ✅ via unit tests; `FaultManager-8` ⏸ deferred (int); `FD-L2-01/05/09` gain a
  ⏸ deferred link; `FD-L2-06`/`MS-L2-04` linked to the skipped test (check how the script renders skipped — if it warns, drop the marker and keep the
  docstring). Zero RTM warnings (all claimed IDs exist — §E step 23 ran first). `MM0009` keeps its ✅.
- Result line must read PASS with "unverified: (none)".

## Target (clean-path copy, CLAUDE.md commands)
1. rsync (excludes as documented; **never** rsync `fprime-venv`). 2. `fprime-util generate` (new fpp types). 3. `fprime-util build`.
4. Dictionary `build-artifacts/zephyr/fprime-zephyr-deployment/dict/ReferenceDeploymentTopologyDictionary.json` (use `fprime-venv/bin/python3`, bare
   `python3` is broken): `commands` = previous + 10, `parameters` = previous + 4, `telemetryChannels` = previous + 13, `events` = previous + 6;
   `telemetryPacketSets` contains packet id 9 with all 13 `ReferenceDeployment.faultManager.*` channels; four `*.faultOut` ports do not appear in
   the dictionary (ports are not listed) — confirm instead by grepping the generated `ReferenceDeploymentTopologyAc.cpp` for `faultIn` (6 connections).
5. Record FLASH/RAM from the build log next to the Cycle A baseline (685824 B / 340080 B) in 07 → ledger.
6. `fpp-check`-level trap: if `fpp-to-dict` fails, a channel is missing from the packet set (§D step 17).

## Deferred (board / flatsat) — written, not run
- `fault_manager_test.py` on the rig; `RgMaxTime` before/after; 24 h shadow soak; the per-trigger authority flip (06).
