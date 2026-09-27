# Cycle K review — bench startup sequence (plan `cycle-k-plan.md`, tree fix/c08-c11 @ 6296ef32 + Cycle I/J working sets)

**Verdict: Ready, with amendments 1–3.** Self-reviewed by the orchestrator against the tree on 2026-09-27 (no second
reviewer in this session; recorded). Load-bearing claims checked at source: the four hazard commands and the comma /
no-comma `TRANSMIT` forms (`sequences/startup.seq:7,8,17,19,20,22,23`), the unconditional dispatch (`StartupManager.cpp:383`),
`ARMED`'s scope (`:426-434`), `DEFAULT_STARTUP_VALUE == 0` (`test_StartupManager_Persistence.cpp:592`), the `sequence`
Makefile target (`:427-433`).

## Deliberate behaviour changes accepted
None in flight. The bench file is opt-in via a saved parameter; the flight file and its default path are untouched.

## Amendments (normative; override the plan where they differ)
1. Tests on the real tree assert only the exit code and the one stdout line of `--check`; every count and every line of
   text is proven on fixtures. The current-tree `--check` test is red at Stage 3b (no bench file yet) and green at Stage 4.
2. Interface fixed by plan §1.1–1.3 (flags, exit codes, the two header lines byte for byte, ASCII hyphen). Coder
   mismatches are plan defects, not test edits.
3. Frozen paths: everything Cycle I and Cycle J left uncommitted (see `cycle-j-plan/README.md` §Hazards plus
   `scripts/check_hardware_consistency.py`, `scripts/tests/test_check_hardware_consistency.py`,
   `docs-site/requirements/hardware-consistency.md`). `verify.sh` not edited. The Makefile hunk is the one exception the
   plan names.

## Tests (Stage 3b) — pinned after the test author reports
_(hashes recorded here by the orchestrator before Stage 4)_
4. **Command token stops at a comma** (author's finding, 2026-09-27): §1.2's "next whitespace-delimited token" would
   read `SET_MODE,` on `startup.seq:22-23`; the fixture rows and the dropped-line list require the comma excluded. The
   plan text is amended; the tests follow the fixtures. `wrote <path>` prints the `--output` string as given; `--check`
   prints nothing on exit 0 (both unspecified by the plan, not asserted by the tests — the coder follows this line).

Pinned 2026-09-27 after the Stage 3b report (test author: Opus under the `cdh-test-author` contract; 9 tests, all red on
the tree — script absent; ruff check/format clean; `git status` gained exactly these two paths; every frozen file's hash
unchanged; the author verified the tests against a throwaway implementation in its scratchpad, 8 passed + the
current-tree test red as intended, nothing entered the tree):

| File | sha256 |
|---|---|
| `scripts/tests/test_make_bench_sequence.py` | `ebadee896e1eb5a3bc1aad52edc47f074168333957ef622ea32ddf9e84a7dbff` |
| `docs-site/requirements/bench.md` | `29acf13b3d1a4cf0bcb8e48026060cac0d4345925aee37f9d80746a89725e2d5` |

Tree note: at 10:25 on 2026-09-27 another session modified `docs-site/dev-loop/design/stored-data/*`,
`cycles/commit-plan-E-A8-A9.md` and `cycles/cycle-sequencing-E-A8-A9.md` (before this cycle's Stage 3b baseline). Those
paths are disjoint from Cycle K's and are treated as pre-existing; the coder is told not to stop on further churn there.
5. **Trailing blank lines are dropped** (coder's finding, 2026-09-27, Stage 4 stopped at gate 3): the flight file's
   dropped lines 17–23 sit among blank lines 15, 16, 18, 21, so "blank lines are kept" produced four trailing `\n`, which
   `end-of-file-fixer` (`.pre-commit-config.yaml:6`) strips — the gate was red on pre-commit and any commit would have made
   `--check` stale. Interior blank lines stay; blanks after the last non-blank line are dropped; the file ends in one `\n`.
   Plan §1.2 and §1.4 amended; BENCH-1's criterion updated via `req.py set`; a Stage 3b fixture added by the test author
   before the coder changes the script (D-002). Also recorded: `make` targets cannot run at `$R` (`CLAUDE.md:31`), so the
   `make bench-sequence` result is proven in the build copy or with the coder's `make -o fprime-venv … UV_RUN=` workaround;
   and selecting the bench file with `PRM_SAVE_FILE` occupies one of the 25 saved parameter slots (`PRMDB_NUM_DB_ENTRIES`)
   while the board is in bench mode.

**Re-pinned 2026-09-27 after amendment 5** (test author resumed: one test added, `test_bench1_trailing_blank_lines_dropped`,
the nine earlier tests byte-identical; BENCH-1 criterion updated via `req.py set`; 9 passed / 1 failed against the
coder's pre-amendment script, as intended). These supersede the table above:

| File | sha256 |
|---|---|
| `scripts/tests/test_make_bench_sequence.py` | `031811b49a4fc0e22ec9c8f3075ae78d61aefbc90c9734e0e1ea75b3a326b41f` |
| `docs-site/requirements/bench.md` | `f4c735d5f04a4f381d6c8b110ac6d49444be5e5b21af460e57453aa127d30063` |

## Stage 4 / Stage 5 — 2026-09-27, tree fix/c08-c11 @ 6296ef32 + Cycle I/J/K working sets
Coder: Opus under the `cdh-coder` contract; stopped once at gate 3 on the trailing-blank defect (amendment 5), resumed
after the test author's fixture landed, one-line change. Orchestrator re-ran everything from a clean `build-gtest`:
pinned hashes unchanged; frozen files unchanged except the planned Makefile hunk; `git status` delta vs the Stage 3b
baseline is exactly Makefile, the two HP docs, the script, the generated sequence and the two Stage 3b files.
`sequences/bench_startup.seq`: 15 lines, header byte-exact, no `antennaDeployer.DEPLOY` / `lora.TRANSMIT ENABLED` /
`modeManager.EXIT_SAFE_MODE` / `detumbleManager.SET_MODE` in the body, `lora.TRANSMIT, DISABLED` and the five
`SET_ID_FILTER` lines kept, the interior blank kept, ends in exactly one `\n`; pre-commit hooks run on the file alone all
pass and leave it unchanged; `--check` exit 0; `sequences/startup.seq` byte-identical. Tests 10/10; ruff clean. Gate:
`result: PASS`, `unverified: (none)`; script tests 173 (+10); host 30/30; matrix 336 / 154 / 98 (+2, BENCH rows). seqgen
in the build copy (independent run): 263 bytes, CRC 0xB8D976A7 — blank lines compile to nothing, so amendment 5 changed
the text file only. No target build (no flight code changed).

Not verified here: the on-board selection (uplink to `/seq/bench_startup.bin`, whether `fileUplink` creates `/seq`, that the
saved `STARTUP_SEQUENCE_FILE` loads before the first tick); `StartupSequenceFailed` on a fresh filesystem; `make bench-sequence`
at a clean path (the target is proven only by the coder's `make -o fprime-venv … UV_RUN=` workaround); CI not run.

## Follow-ups
1. Bench milestone (ROADMAP row 3): select the bench sequence first, per the HP README §Bench preconditions.
2. Q20 (flight `EXIT_SAFE_MODE` every boot) is untouched and still Jesse's decision; the bench file just avoids it.
3. If a board must run bench mode for long, the saved `STARTUP_SEQUENCE_FILE` occupies one of 25 `PrmDb` slots.
