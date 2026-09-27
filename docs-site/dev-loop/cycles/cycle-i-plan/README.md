# Cycle I plan — documentation lint and generated status (index)

Branch `main` @ 6abcaabd, tooling only. Plan only: no code, tests or commits were made while writing it. Repo `$R` =
`/Users/jesse-cm/Documents/Documents - Jesse's Mac/scalar-softwarestack/proves-core-reference`; `$PY` =
`fprime-venv/bin/python3`; `DL` = `docs-site/dev-loop`. Paths are relative to `$R`.

**What this cycle delivers.** The gate enforces the ownership rule in `../../decisions/README.md` (one home per fact,
append-only decisions, no typed state): `scripts/check_docs.py` (seven rules, same command-line contract style as
`check_capacity.py`), `scripts/doc_status.py` (writes the only "current state" page, `DL/STATUS.md`, from generators),
a `docs` stage in `scripts/verify.sh`, and configure errors that are no longer swallowed by the host-test stage.

| File | Read it when |
|---|---|
| `01-normative.md` | writing the tests — both scripts' command lines, exit codes, line grammar, the exact rule definitions and the fixture that makes each FAIL; `verify.sh` results; harm table; non-goals |
| `02-requirements.md` | adding rows `DOCS-1..10` — the seed and the verbatim `req.py add` commands |
| `03-advisory.md` | coding — module layout, git queries, test fixture shape, file order, open risks |
| `04-findings.md` | merging into the ledger — verified facts (file:line) and the brief claims found wrong |

**Normative (tests derive from these only):** `01-normative.md`, `02-requirements.md`. **Advisory:** `03-advisory.md`.

Summary in 8 lines:
1. `check_docs.py` prints one `DOCS-n <slug>: <count> … — <STATUS>` line per rule, offenders indented below it, `RESULT:` last; exit 0/1/2; every input has a `--<name>` override so tests run on a synthetic tree under `tmp_path` (with a `git init` repo for rules 4–5).
2. Rule 1 must match extensionless copies: all 14 conflict copies in the tree today are directories or `Makefile 2`; the brief's `* 2.*` glob matches none of them.
3. Rule 4 checks only the `Landed` column of `DL/cycles/README.md` (17 hashes today, all ancestors). ROADMAP rows and D-records cite library/upstream commits (`b14101dd` is fprime-zephyr, not in this repo) and are exempt.
4. Rule 5 baseline = the D-record's content at the last commit of the day it was introduced; the only allowed later change is one `Superseded by D-nnn` line prepended.
5. `STATUS.md` is deterministic (capacity run with `--dictionary none`, HEAD's commit date, not wall-clock); `doc_status.py --check` ignores the `Generated at HEAD` line, so the committed file is never stale merely because committing it moved HEAD.
6. The RTM stage is the gate's *last* stage, so the `docs` stage (after `script-tests`, before `pre-commit`) runs the lint and `--check`; the `rtm` stage regenerates `STATUS.md` after the matrix and reports whether it changed.
7. `verify.sh` gains `VERIFY_UT_DIR` so a configure failure is provable from a synthetic CMake project; the last 20 lines of `configure.log` are printed on failure.
8. Harm: no diff under `lib/`, `PROVESFlightControllerReference/`, `check_capacity.py`, `check_packet_set.py`; the 44 existing script tests and 30 host binaries stay green; gate +≤ 15 s (both scripts measured-class: < 2 s).

Precondition (environment, not a tree change): delete the 14 untracked conflict copies (`04-findings.md` §1) before the row lands, or rule 1 fails the first gate run as designed.
