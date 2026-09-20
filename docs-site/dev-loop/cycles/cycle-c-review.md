# Cycle C review decisions (orchestrator, 2026-09-05) — apply as coder amendments

Plan: `cdh-cycle-c-plan.md`. Verdict: approved. Runs AFTER Cycle B (B raises the dispatch table to 400; C adds 2 commands).

Highest-risk change of the run: a new passive component in the LoRa uplink path ahead of the frame accumulator. Accepted because it is pass-through by default (`CORRECTION_ENABLED=false`), forwards the same buffer object with no copy when disabled, and the plan pins both the byte-identity and the 1-in/1-out ownership invariants with host tests. Board smoke (`command_path_test.py`) is the deferred proof.

Amendments for the coder:
1. Keep `CORRECTION_ENABLED` default false. Do not flip it. The sdd must say the flight setting is off until the board procedure runs.
2. The corrector's flight code must contain no candidate-loop × CRC (O(8·len) syndrome walk only); the brute-force search lives in the test oracle. Reviewer will grep for it.
3. Ownership: every `dataIn` buffer is forwarded exactly once or returned exactly once; `dataReturnIn` forwards the same buffer to `dataReturnOut`. Tests must cover disabled, corrected, and uncorrectable paths for both directions.
4. TaskGate: mask default 0x1F (all enabled); excluded tasks list from plan §2b stands (watchdog, modeManager, startupManager, burnwire, antennaDeployer, detumbleManager, telemetryDelay, comms/health, all 10 Hz). Any attempt to gate an excluded task must be a compile-time impossibility (enum has only the 5 members), not a runtime check.
5. New telemetry channels go into `HealthAuxiliary`, never `Health` (TlmPacketizer asserts packet size at init). Verify the packet-set edit is in the same commit.
6. `req.py` usage allowed exactly as plan §4G (adds for new component IDs, `--status "Inspected 2026-09-05 ..." --reason` for DH-L2-06/09/10 and SC-L2-06 only after re-reading the cited lines, criteria annotation for CH-L2-20 and SC-L2-07). Nothing else.
7. Target compile requires `fprime-util generate` first in the clean-path copy (new components); dictionary check: +2 instances, +2 commands, +1 param, +4 events, +4 channels; `git diff --stat lib/` must be empty.
8. Commit in two logical groups if the hooks allow: `feat(TcFrameCorrector)` and `feat(TaskGate)` + inspection records; otherwise one commit with both scopes named.
9. Read discipline and Findings section as in the Cycle A brief.
