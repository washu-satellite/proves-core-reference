# Cycle B review decisions (orchestrator, 2026-09-05) — apply as coder amendments

Plan: `cdh-cycle-b-plan.md`. Verdict: approved, TM-L2-02 only; DH-L2-03/04/05/08 stay "not implemented" (library buffers are setup-once; no on-board telemetry store) — report to user, propose TelemetryStore component as a later cycle.

Amendments for the coder:
1. Dispatch-table headroom: raise `CMD_DISPATCHER_DISPATCH_TABLE_SIZE` 350 → 400 in `PROVESFlightControllerReference/project/config/CommandDispatcherImplCfg.hpp:14` (project-owned config, not lib). Report the RAM delta from the target build's memory-region lines; must stay under 70% RAM.
2. The PrmDb correction: parameters CAN persist via PRM_SAVE_FILE (FileHandling subtopology reads /prmDb.dat at boot). Keep the plan's RAM-only stance (no PRM_SAVE in tests) but the sdd prose must say "persist with PRM_SAVE_FILE if desired", not "cannot persist". Do not touch NullPrmDb.
3. `req.py add` for the new component IDs (ThermalManager-1/2, ADCS-1/2, PWR-MON-REQ-008/009, ImuManager-1) is allowed; `req.py set TM-L2-02 --criteria` to drop the "[no such parameter]" tag is allowed; no other table edits.
4. Every new telemetry channel → packet-set line (plan §4 lists them); target compile mandatory; dictionary check: commands 339→347, params 89→93.
5. PowerMonitor dt-guard change is approved (identical at default; unit test must pin both interval 1 and 30).
6. Board test `collection_interval_test.py` written and collected only; docstring must state SET_LEVEL 3 and DIVIDER changes and their restore.
7. Read discipline and Findings section as in the Cycle A brief.
