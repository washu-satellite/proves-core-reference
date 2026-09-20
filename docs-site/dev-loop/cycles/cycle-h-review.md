# Cycle H review — capacity audit (plan a82e36df, tree 6b22f72d)

**Verdict: Ready, with amendments 1–6 below applied by the test-author and coder.** Every load-bearing claim in
`cycle-h-plan/01-normative.md` and `04-findings.md` was checked against source on 2026-09-19: PrmDb overflow drops with
`PrmDbFull` and never asserts (`PrmDbImpl.cpp:96-113`, `:583-592`); the dispatcher asserts on the (LIMIT+1)th opcode
(`CommandDispatcherImpl.cpp:35`); the build copy's dictionary at 6b22f72d has 387 distinct opcodes, 106 parameter ids,
244 channels, `projectVersion v1.2.0-65-g6b22f72d`; `FaultType` tops out at `ADCS_UNSTABLE = 8` against
`MAX_FAULT_TYPE = 8` and a `U8` mask; TaskGate indices are symbolic; rate groups use 2 / 14 / 20 slots with max indices
1 / 13 / 20; `faultIn` uses 0, 1, 3; `req.py` cannot seed a group; `generate_rtm.py` links only two directories and
counts a skipped pytest case as passed; `check_packet_set.py` standalone prints the six lines in `04-findings.md` §10.

## Deliberate deviations accepted
- `PRMDB_NUM_DB_ENTRIES` is INFO, never FAIL (not a boot overflow). The roadmap's "fails on each" is amended to
  "fails on each boot-assert constant".
- The audit is a new aggregator importing the packet module, not a bigger `check_packet_set.py`. Accepted on the
  modularity rule; the packet script's standalone output is pinned byte-for-byte.
- `verify.sh` gains `VERIFY_BUILD_DIR`, `VERIFY_STAGES`, `VERIFY_NESTED`. Accepted as the smallest way to make the
  clean-build result testable without recursion; the `stages: ... (partial run — not a gate result)` label is mandatory.
- No flight code, `.fpp`, `project/config` or `lib/` change: confirmed as the harm table's first line.

## Amendments (normative; override the plan where they differ)

1. **Tests never assert the tree's numbers.** Every "the current tree prints 23/24 …" clause in `01-normative.md` §2
   and in the `AUDIT-1..8` criteria is replaced by: *"on the current tree the line is present, well-formed, and its
   status is not FAIL; exit 0"*. The numbers go in this review (table below) and in the ledger, not in a test. Reason:
   rollback trigger (a) reverts the last commit when a previously green test goes red; a test pinned to 244 channels
   would force a false revert of the first A8 row that adds a channel. Overflow, WARN and INFO behaviour is proven on
   doctored fixtures only.
2. **No test depends on `~/scalar-build`.** `--dictionary auto` is tested with `--build-copy <tmp>` holding a synthetic
   dictionary (the repo tree has no dictionary, `04-findings.md` §4), and with an empty `<tmp>` for the SKIP path. The
   "build copy's dictionary prints 387/512" clause is deleted from AUDIT-3 and AUDIT-4 (covered by amendment 1).
3. **AUDIT-9 loses its last clause.** "the full default run on the current tree prints result: PASS with unverified
   (none)" cannot be asserted from inside the gate (nested runs are refused by design). It is Stage 5 evidence,
   recorded in this review's §Gate, not a test assertion. The row keeps its first three clauses.
4. **Skipped is not passed.** The `generate_rtm.py` extension must treat a pytest `<skipped>` case as *not* passed for
   the script-junit source (today's `parse_junit` would count it as green, `04-findings.md` §14). Add this to the
   normative results: a claiming script test that is skipped shows `⚠️ Unit (no result)`, not `✅`.
5. **Matrix harm line.** Add to the harm table: the diff of `docs-site/requirements-matrix.md` after the row is
   limited to the header counts and the nine new `AUDIT-*` rows; every existing row's cells are unchanged.
6. **Interface names are fixed by `01-normative.md` §1.** Flags, defaults, line grammar (including the U+2014 dash),
   status words, line order and exit codes are the contract the tests are written to; the coder builds to them and
   reports any mismatch as a plan defect rather than editing a test.

## Numbers on the tree at 6b22f72d (reference, not test input)
| Line | Used / limit | Status |
|---|---|---|
| MAX_PACKETIZER_PACKETS | 23 / 24 | WARN |
| MAX_PACKETIZER_CHANNELS | 244 / 256 (174 + 70) | WARN |
| CMD_DISPATCHER_DISPATCH_TABLE_SIZE | 387 / 512 | OK (dictionary present) / SKIP |
| PRMDB_NUM_DB_ENTRIES | 106 / 25 (lib default) | INFO / SKIP |
| ActiveRateGroupOutputPorts[50Hz, 10Hz, 1Hz] | 2, 14, 20 / 25 | OK |
| FaultInPorts | 3 / 4 | OK |
| NUM_TASKS | 5 / 5 | OK |
| MAX_FAULT_TYPE | 8 / 8 (U8) | WARN |

## Tests (Stage 3b) — written 2026-09-19 from `01-normative.md`, `02-requirements.md` and this review only
44 cases (41 in `test_check_capacity.py` claiming AUDIT-1..8 plus one unclaimed grammar test; 3 in `test_verify_sh.py`
claiming AUDIT-9). Reviewed test-by-test against the criterion clauses: each claimed clause has one assertion, no test
reads the tree's numbers, no test depends on `~/scalar-build`, the packet-script byte pin is in AUDIT-2. Expected state
before the coder: 1 failed at the first test (`scripts/check_capacity.py: No such file or directory`).

Author's findings, resolved here:
- AUDIT-3 "L opcodes give OK" contradicted §1.4 (USED ≥ 0.9·LIMIT is WARN). **§1.4 wins:** at L the line is WARN; the
  test asserts status ∈ {OK, WARN} and exit 0. Plan wording amended by this note.
- AUDIT-4's `(project override|lib default)` contained a bare `|`, which `req.py` splits on; entered as
  `(project override or lib default)`. The script's DETAIL text is `project override` or `lib default` as planned.
- `pytest.ini` is at the repo root (not under `PROVESFlightControllerReference/`) and already registers `verifies`;
  the conftest registration is redundant and harmless.
- Amendment 4 (a skipped script test shows as not passed in the matrix) is not assertable from results because
  `generate_rtm.py` has no output-path option. **It stays a coder requirement, verified at Stage 5 by inspection and
  by running the parser on a junit with a `<skipped>` case.**
- The `build-fprime-automatic-zephyr*/**` search root of `--dictionary auto` and the no-WARN case for a matching HEAD
  hash are exercised at Stage 5 on the build copy, not by a test.

Hashes (`shasum -a 256`), the contract the coder inherits; the same command must reproduce them at Stage 5:
```
8c779d5d4b586eedf3f0dce5ef83055ec03d431bd1bc96395534b713588a8604  scripts/tests/conftest.py
b6222a4cff61886c1871d4c96e0c6b95c49a85a814f4235dd5bd42c8e27ba781  scripts/tests/test_check_capacity.py
616071dc706970bb4c68af80f7ebfe21ec650fbb21b31fcb64225e36917f54cc  scripts/tests/test_verify_sh.py
1d3d6620fc269f842ad0f25a9ddec47493ac22e891f81cca0a3b6c391bed4d98  docs-site/requirements/tooling.md
```
Commit order for this cycle: tests and rows first (the gate stays green because `verify.sh` does not yet run
`scripts/tests`), then the implementation row. Git then shows the tests predating the code.

## Gate — Stage 5, 2026-09-19, run by the reviewer from `rm -rf build-gtest`
`result: PASS`, `unverified: (none)`; 30 host binaries PASSED; `44 passed in 36.68s`; matrix 322 / 140 / 84; wall 1:15.6
(today's gate ~45 s → +30 s, inside the +90 s bound). Test hashes reproduced exactly; `git diff --stat 6b22f72d -- lib/
PROVESFlightControllerReference/` empty; matrix diff = header + AUDIT group + nine rows. Implementation commit 6a1589b2
(coder: Opus 5), tests commit 26041de4 (author: Fable 5.1) — the tests predate the code in history.

Correction to the reference table above: the planner counted commented-out S-band connections (`topology.fpp:289,297,
298,304,307`). Live connections are 50 Hz 2 (max index 1), 10 Hz **11** (max 13), 1 Hz **18** (max 20); free 23 / 14 / 7.
Coder deviations accepted: `mkdir -p "$BUILD"` at the top; missing `--script-junit` silent; skipped script case → `None`
(no result), not `False`. Finding carried to the ledger: `generate_rtm.py` drops a marker separated from its `def` by a
multi-line decorator (one AUDIT-3 test unlinked; row still linked by six others).
