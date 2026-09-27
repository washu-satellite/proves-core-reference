# Cycle J plan — defects C-08 (DRV2605 mux channel) and C-11 (flash size) (index)

Branch `main` @ e1eced14, with the Cycle I working set present and uncommitted in the tree (see §Hazards). Plan only:
no code, tests or commits were made while writing it. Repo `$R` =
`/Users/jesse-cm/Documents/Documents - Jesse's Mac/scalar-softwarestack/proves-core-reference`; `$PY` =
`fprime-venv/bin/python3`; `P` = `PROVESFlightControllerReference`; `B` = `boards/bronco_space`. Paths are relative to `$R`.

**What this cycle delivers.** Two one-line-class flight fixes and the host-side check that would have caught both and keeps
them fixed: `scripts/check_hardware_consistency.py` (two lines, same exit-code and `RESULT:` conventions as
`check_capacity.py` / `check_docs.py`), its script tests, and requirement rows `HWC-1..2`. The check is enforced by the gate
through the `script-tests` stage (its current-tree tests are red until the fixes land); printing its lines in the `audit`
stage is a one-line `verify.sh` follow-up deliberately deferred until Cycle I commits (§Hazards).

Defect records (model repo `~/scalar`, `generated/conflicts.md`): **C-08** — all five `drv2605FaceNManager.configure(...)`
calls pass `state.muxChannel0Device` while the devicetree puts `faceN_drv2605` on mux channels 0, 1, 2, 3, 5. **C-11** —
`&flash0 { reg = <0x10000000 DT_SIZE_M(4)>; }` while the fitted part (U11, W25Q128JVS, S8 netlist) is 16 MiB and the
partition table already ends at 16 MiB, so the 12 MiB `storage_partition` is outside the declared device.

| File | Read it when |
|---|---|
| `01-normative.md` | writing the tests — the script's command line, exit codes, line grammar, the exact rule definitions and the fixture that makes each FAIL; the two flight results; harm table; non-goals |
| `02-requirements.md` | adding rows `HWC-1..2` — the seed file and the verbatim `req.py add` command |
| `03-advisory.md` | coding — module layout, regexes, fixture shape, the target-build check, open risks |
| `04-findings.md` | merging into the ledger — verified facts (file:line), including what the defect records overstate |

**Normative (tests derive from these only):** `01-normative.md`, `02-requirements.md`. **Advisory:** `03-advisory.md`.

## Hazards (read before touching the tree)
- **Cycle I is complete, green and uncommitted** in `$R` (its review's Gate section, 2026-09-26). Its working set:
  modified `docs-site/dev-loop-findings.md`, `docs-site/dev-loop/README.md`, `docs-site/dev-loop/ROADMAP.md`,
  `docs-site/dev-loop/cycles/cycle-i-review.md`, `docs-site/dev-loop/decisions/README.md`, `docs-site/dev-loop/design/activity-axis.md`,
  `docs-site/dev-loop/design/parameter-policy.md`, `docs-site/requirements-matrix.md`, `docs-site/requirements/tooling.md`,
  `scripts/verify.sh`; untracked `scripts/check_docs.py`, `scripts/doc_status.py`, `scripts/tests/test_check_docs.py`,
  `scripts/tests/test_doc_status.py`, `scripts/tests/test_verify_sh_docs.py`, `docs-site/dev-loop/STATUS.md`,
  `docs-site/dev-loop/decisions/D-006-baseline-cadence-home-startup-seq.md`, `docs-site/dev-loop/design/driver-board-firmware.md`.
  **Nothing in this cycle edits any of those paths.** `tooling.md` in particular is hash-pinned by Cycle I's review, which is
  why `HWC` rows go in a new file. `verify.sh` is not edited for the same reason.
- Generated artifacts the gate rewrites — `docs-site/requirements-matrix.md` and `docs-site/dev-loop/STATUS.md` — will carry
  both cycles' deltas until Cycle I commits. That is expected; they are generated, not authored.
- Between Stage 3b and Stage 4 the two current-tree tests are red, so a full gate run is red for that window. Do not run
  Cycle I's commit in that window.
- Never edit inside `~/scalar-build/proves-core-reference` (the rsync build copy); the target build runs there per `CLAUDE.md`.
