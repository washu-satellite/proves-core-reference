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

## Tests (Stage 3b) — filled in after the test-author reports
Hashes (`shasum -a 256`) of every file under `scripts/tests/` and `docs-site/requirements/tooling.md` are recorded here
before the coder starts; the same command must reproduce them at Stage 5.

## Gate — filled in at Stage 5
