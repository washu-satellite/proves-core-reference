---
name: cdh-cycle
description: Run one cycle of the SCALAR CDH dev loop for a named requirement package (requirement → tests → implementation → verify → post-mortem), with a commit after every step. Use when asked to implement a package, a set of requirement IDs, or "the next cycle".
---

# cdh-cycle — one loop iteration, one package

You are the orchestrator. Agents do the reading and writing; you brief, review, verify, and commit. Rules below are not advice; a step that skips a gate is a failed step.

## Stage 0 — environment
Run `VERIFY_ENV=<host|desk|rig> scripts/verify.sh`; it must PASS with `unverified: (none)` before anything starts. Record the executable levels line. Board-level rows are *deferred* in host, never "passing" and never "failing".

## Stage 1 — Ready gate
For each requirement ID in the package: `fprime-venv/bin/python3 scripts/req.py show <ID>`. Ready = Method, Level, quantified Pass Criteria, no bare `TBD`. A bracketed `[TBD by Mission Ops: …]` inside a criterion means the row is NOT Ready: list it for the user and drop it from the package. Do not write tests or code for a row that is not Ready.

## Stage 2 — plan (agent: `cdh-planner`)
Brief = package IDs + pointers (CLAUDE.md, `docs-site/dev-loop-findings.md`, the exact files from the ledger) + the two governing rules (must not hurt the current system; Stage 0 environment). Plan goes to `docs-site/dev-loop/cycles/cycle-<x>-plan.md` (directory with README if > 150 lines) and MUST end with `## Findings`. **Commit it** (`docs(dev-loop): Cycle <x> plan`).

## Stage 3 — harm review (you)
Read the plan's harm table and design sections only. Verify every load-bearing claim against source (grep, bounded reads). Write `cycle-<x>-review.md`: verdict, deliberate behaviour changes accepted, amendments numbered. **Commit it.** Merge the plan's Findings into the ledger now (you are the ledger's only writer).

## Stage 4 — implement (agent: `cdh-coder`)
Single writer: nothing else touches the tree or the index until the coder reports. Brief = plan path + review path + hard constraints + gates + report format (see the agent file). Never let the coder commit.

## Stage 5 — verify (you)
1. Format new C++ (`fprime-venv/lib/python3.13/site-packages/clang_format/data/bin/clang-format -i`) and run `scripts/verify.sh` from a clean `build-gtest` yourself; do not trust the coder's summary.
2. Spot-check: scope (`git status` shows only planned paths; no `lib/`, no unplanned `.fpp`), defaults equal today, every host test claims only Unit-level IDs (`req.py show` the Level of each claimed ID), requirement tables untouched except via `req.py`.
3. Target compile from the clean-path copy when production code changed (procedure in CLAUDE.md); record FLASH/RAM and dictionary counts.
4. CI: after the cycle's commits land, push the branch (if the user has authorised pushes for this branch) and poll the run until `lint` and `unit-test` finish; a red job is a Stage 4 failure. If pushing is not authorised, write "CI not run" into the report's unverified list — never let the local gate stand in for CI silently. (On this fork `build`/`integration-*` queue forever; only lint and unit-test are evidence.)
5. Commit in groups with `git reset` between them: `feat(<scopes>)`, `test(unit-tests)`/`test(int)`, `docs(requirements)` (sdds, matrix, ledger merge of the coder's Findings), `build(...)` if tooling changed. Hook failures: fix, `git reset`, redo the group; never `--no-verify`.

## Stage 6 — post-mortem (you)
Every item from the coder's "could not verify" and "Findings" becomes at least one of: a regression test, a GitHub issue labelled `wrong-assumption` or `environment`, a CLAUDE.md Traps line, a ledger line, or a diff to these skill/agent files. Wrong assumptions found anywhere are corrected in every note that carried them, in the same turn. Append a dated entry to the user's brain record if one exists for the project.

## Stop conditions
Stop and report (do not improvise) when: a gate cannot run (environment), a row turns out not Ready, the coder reports a failure outside plan scope, or a change would alter default flight behaviour that the plan did not declare.
