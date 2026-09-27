# Cycle H plan — capacity audit (index)

Branch `feat/upstream-sync` @ 6b22f72d, ROADMAP queue item 2. One row. Plan only: no code, no tests, no commit were
made while writing it. Repo `$R` = `/Users/jesse-cm/Documents/Documents - Jesse's Mac/scalar-softwarestack/proves-core-reference`;
paths below are relative to `$R` unless they start with `PROVESFlightControllerReference/` spelled out or `lib/`.

**What this row delivers.** One host-side gate script, `scripts/check_capacity.py`, that compares every fixed table
size the roadmap names against what the tree (and, when one exists, the built dictionary) actually uses, prints one
headroom line per constant, exits non-zero on overflow, and is the only capacity stage `scripts/verify.sh` runs. It
reuses `scripts/check_packet_set.py` by import (that script keeps its name, its lines and its standalone use).
`verify.sh` also starts the host build from an empty `build-gtest`, runs the new script tests, and feeds their results
to the traceability matrix.

| File | Read it when |
|---|---|
| `01-normative.md` | writing the tests — the script's command line, exit codes and output grammar; per-constant results; `verify.sh` results; harm table; non-goals |
| `02-requirements.md` | adding the `AUDIT-1..9` rows — the seed file and the verbatim `req.py add` commands |
| `03-advisory.md` | coding — why one aggregator plus the imported packet module, parsers, file-by-file steps, test layout, RTM extension |
| `04-findings.md` | merging into the ledger — verified facts with file:line, including the brief claims found wrong |

**Normative (tests derive from these only):** `01-normative.md` and `02-requirements.md`. **Advisory:** `03-advisory.md`.

Summary in 10 lines:
1. `scripts/check_capacity.py` covers nine lines: `MAX_PACKETIZER_PACKETS`, `MAX_PACKETIZER_CHANNELS`, `CMD_DISPATCHER_DISPATCH_TABLE_SIZE`, `PRMDB_NUM_DB_ENTRIES`, `ActiveRateGroupOutputPorts` (one line per rate-group instance, three today), `FaultInPorts`, `NUM_TASKS`, `MAX_FAULT_TYPE`.
2. Every input has a `--<name> PATH` override so a test can point the script at a doctored copy; synthetic overflow of each constant is a doctored copy, never an edit to the tree.
3. The dictionary question: the two dictionary-backed lines (dispatch table, PrmDb) read `commands[*].opcode` and `parameters[*].id` from a `*TopologyDictionary.json`; `--dictionary auto` (default) finds the newest one under the repo or the clean-path build copy, prints its `metadata.projectVersion` and warns when its commit is not the tree's HEAD; with no dictionary those two lines are `SKIP` (exit 0) and `verify.sh` lists them as deferred, the same way it treats board tests.
4. `PRMDB_NUM_DB_ENTRIES` (25, lib default, no project override) is not a boot overflow: the 26th distinct saved id is dropped with `PrmDbFull`. Its line is `INFO` when parameters exceed it (106 today) and never fails; this is the one deviation from "fails on each constant", stated with the reason in 01 §R4.
5. `ActiveRateGroupOutputPorts`, `FaultInPorts`, `NUM_TASKS`: `fpp-check` rejects an out-of-range or duplicate output index at the target build (verified), but that build never runs in this fork's CI, so the host script re-implements the range and duplicate checks and reports free slots. Symbolic indices (`Components.SchedTask.IMU`) are resolved from the enum.
6. `MAX_FAULT_TYPE` overflow is silent (`FaultTable.cpp:67-70` returns an empty mask): FAIL when an enumerator exceeds it or it exceeds the `AUTHORITY_MASK` type width; WARN while zero bits are free (true today).
7. `verify.sh`: `rm -rf` of the build dir before configure (+~30 s measured), audit stage replaces the packet stage, new `script-tests` stage, `VERIFY_STAGES` / `VERIFY_BUILD_DIR` knobs so the script tests can exercise the gate without recursion; a partial run is labelled and never reads as a gate PASS.
8. Requirements: new file `docs-site/requirements/tooling.md`, group `Verification Tooling (AUDIT)`, rows `AUDIT-1..9`, Unit / Unit Test; `generate_rtm.py` learns to link `scripts/tests/test_*.py` markers and read their junit, otherwise the rows would show "No automated test".
9. Harm: `git diff --stat -- lib/ PROVESFlightControllerReference/` empty; `check_packet_set.py` standalone output byte-identical; `VERIFY_ENV=host scripts/verify.sh` PASS on the current tree with the same section headers; gate runtime +≤ 90 s.
10. Non-goals: raising any limit (A8 raises `MAX_PACKETIZER_CHANNELS`), the bench milestone, a target build in CI, CI running the script tests, `HealthPingPorts` / `CmdDispatcherComponentCommandPorts` / `RateGroupDriverRateGroupPorts` (same fpp-loud class, follow-up).
