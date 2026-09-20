---
name: cdh-cycle
description: Run one cycle of the SCALAR CDH dev loop for a named requirement package (requirement → plan → review → tests written from results only → implementation → verify → post-mortem), with a commit after every step. Use when asked to implement a package, a set of requirement IDs, or "the next cycle".
---

# cdh-cycle — one loop iteration, one package

You are the orchestrator. Agents do the reading and writing; you brief, review, verify, and commit. Rules below are not advice; a step that skips a gate is a failed step.

## Stage -1 — upstream check (ROADMAP rule 1)
`git fetch proves-origin && git log --oneline HEAD..proves-origin/main | wc -l`. If non-zero, the cycle's first rows are the sync (analysis, plan, merge row) before any feature row. Record the count in the plan's README.

## Stage 0 — environment
Run `VERIFY_ENV=<host|desk|rig> scripts/verify.sh`; it must PASS with `unverified: (none)` before anything starts. Record the executable levels line. Board-level rows are *deferred* in host, never "passing" and never "failing".

## Stage 1 — Ready gate
For each requirement ID in the package: `fprime-venv/bin/python3 scripts/req.py show <ID>`. Ready = Method, Level, quantified Pass Criteria, no bare `TBD`. A bracketed `[TBD by Mission Ops: …]` inside a criterion means the row is NOT Ready: list it for the user and drop it from the package. Do not write tests or code for a row that is not Ready.

## Stage 2 — plan (agent: `cdh-planner`)
Brief = package IDs + pointers (CLAUDE.md, `docs-site/dev-loop-findings.md`, the exact files from the ledger) + the two governing rules (must not hurt the current system; Stage 0 environment). Plan goes to `docs-site/dev-loop/cycles/cycle-<x>-plan.md` (directory with README if > 150 lines) and MUST end with `## Findings`. **Commit it** (`docs(dev-loop): Cycle <x> plan`).

## Stage 3 — harm review (you)
Read the plan's harm table and design sections only. Verify every load-bearing claim against source (grep, bounded reads). Write `cycle-<x>-review.md`: verdict, deliberate behaviour changes accepted, amendments numbered. **Commit it.** Merge the plan's Findings into the ledger now (you are the ledger's only writer).

## Stage 3b — tests (agent: `cdh-test-author`)
Tests are written **before any implementation exists and independently of the implementation method.** Brief = the plan's *normative* sections and the review only; never the advisory sections, never a design sketch, never a hint about how the coder will do it. The author writes every host test, board test and `req.py` row for the cycle's rows, compiling against the interface surface the normative section names (stubs under `test/unit-tests/support/` are theirs to add). Tests fail (or do not link) at this point; that is expected. You review each test against the pass-criterion sentence it claims, not against any code. Then record `shasum -a 256` of every test and support file in `cycle-<x>-review.md` §Tests; those hashes are the contract the coder inherits. A test that cannot be written from the normative section alone is a plan defect: back to Stage 2.

## Stage 4 — implement (agent: `cdh-coder`)
Single writer: nothing else touches the tree or the index until the coder reports. Brief = plan path + review path + the Stage 3b test paths (read-only) + hard constraints + gates + report format (see the agent file). The coder makes the tests pass; it does not edit them. A test that will not compile against the coder's interface is a plan defect the coder reports, not a test to fix. Never let the coder commit.

## Stage 5 — verify (you)
1. Format new C++ (`fprime-venv/lib/python3.13/site-packages/clang_format/data/bin/clang-format -i`) and run `scripts/verify.sh` from a clean `build-gtest` yourself; do not trust the coder's summary.
2. Spot-check: scope (`git status` shows only planned paths; no `lib/`, no unplanned `.fpp`), defaults equal today, every host test claims only Unit-level IDs (`req.py show` the Level of each claimed ID), requirement tables untouched except via `req.py`.
3. Target compile from the clean-path copy when production code changed (procedure in CLAUDE.md); record FLASH/RAM and dictionary counts.
4. CI: after the cycle's commits land, push the branch (if the user has authorised pushes for this branch) and poll the run until `lint` and `unit-test` finish; a red job is a Stage 4 failure. If pushing is not authorised, write "CI not run" into the report's unverified list — never let the local gate stand in for CI silently. (On this fork `build`/`integration-*` queue forever; only lint and unit-test are evidence.)
5. Commit **per module row** of the cycle's commit plan (see `docs-site/dev-loop/cycles/commit-plan-*.md`), not in three groups: each row is one commit containing its Stage 3b test and its implementation; before committing, re-run `shasum -a 256` on the test and support files and compare to the review's §Tests — any drift is a coder edit to a test: `git checkout` the test, send the row back; the gate (steps 1-3) runs green before every commit; a new component lands unwired and is activated by its own topology commit. One coder at a time in the working tree. Hook failures: fix, `git reset`, redo the row; never `--no-verify`.
6. Rollback triggers, applied before the next row starts: (a) any previously green test goes red → `git revert` the last commit immediately, no debugging on the branch; (b) the row's own gate is still red after three coder attempts → revert the row and send it back to Stage 2 as a criterion problem; (c) a board-level test that cannot run is *deferred*, never a failure. Revert in reverse row order only; the constants row of a cycle is reverted last.

## Stage 6 — post-mortem (you)
Every item from the coder's "could not verify" and "Findings" becomes at least one of: a regression test, a GitHub issue labelled `wrong-assumption` or `environment`, a CLAUDE.md Traps line, a ledger line, or a diff to these skill/agent files. Wrong assumptions found anywhere are corrected in every note that carried them, in the same turn. Append a dated entry to the user's brain record if one exists for the project.

## Stop conditions
Stop and report (do not improvise) when: a gate cannot run (environment), a row turns out not Ready, the coder reports a failure outside plan scope, or a change would alter default flight behaviour that the plan did not declare.
