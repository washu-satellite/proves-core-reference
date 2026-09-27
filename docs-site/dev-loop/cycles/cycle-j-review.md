# Cycle J review — defects C-08 and C-11 (plan `cycle-j-plan/`, tree main @ e1eced14 + Cycle I working set)

**Verdict: Ready, with amendments 1–4.** Reviewed by the orchestrator against the tree on 2026-09-26 (no second reviewer
was available in this session; recorded as such). Load-bearing claims checked at source: the four wrong channels
(`ReferenceDeploymentTopology.cpp:181-184`), the devicetree channel of every `faceN_drv2605` (`v5.dtsi:231-410`), the
mux device's only use (`Drv2605Manager.cpp:153`), the partition end (`v5.dtsi:92-94`) against the `reg` (`:66`), and that
no other board file overrides `&flash0`.

## Deliberate behaviour changes accepted
- `MuxUnhealthy` for faces 1, 2, 3 and 5 will now report their own channel's readiness instead of channel 0's. No other
  flight behaviour changes; I2C routing was already correct (plan `04-findings.md` §1).
- The 12 MiB `storage_partition` becomes addressable by the flash driver. Nothing mounts it yet; that is a later row.

## Amendments (normative; override the plan where they differ)
1. **Tests on the real tree assert only status and exit code** (as Cycle H amendment 1 and Cycle I amendment 1); the
   numbers `17 managers` and `4 mismatched` are proven on doctored copies, never asserted against the tree. Exception, and
   the point of the cycle: the two current-tree tests assert `OK` and exit 0 — they are red at Stage 3b and green at Stage 4.
2. **Interface fixed by `01-normative.md` §1.1–1.4** (flags, defaults, line grammar with U+2014, exit codes, stderr on
   exit 2). Coder mismatches are plan defects, not test edits.
3. **Cycle I's paths are frozen for this cycle** (plan README §Hazards). `git status --short` before Stage 3b is the
   baseline; a diff outside `scripts/check_hardware_consistency.py`, `scripts/tests/test_check_hardware_consistency.py`,
   `docs-site/requirements/hardware-consistency.md`, the two flight lines, and the generated matrix/STATUS.md is a stop.
4. **`verify.sh` is not edited.** Enforcement is through the `script-tests` stage; the audit-stage echo of the two lines is
   a one-line follow-up after Cycle I commits (queue it in ROADMAP then, not now — ROADMAP is a Cycle I path).

## Tests (Stage 3b) — pinned after the test author reports
_(hashes recorded here by the orchestrator before Stage 4; the coder inherits these read-only)_

Pinned 2026-09-26 after the Stage 3b report (test author: Opus, general-purpose agent under the `cdh-test-author` contract),
reviewed by the orchestrator against the criterion sentences, not against code. 10 functions, 13 items, all red on the
tree (script absent); ruff check/format clean; `git status` gained exactly these two paths; every frozen file's hash unchanged.

| File | sha256 |
|---|---|
| `scripts/tests/test_check_hardware_consistency.py` | `fc0bdebd6e443af58d2ca477dfd8be7979892d2596ae61bf72b34324f4522f10` |
| `docs-site/requirements/hardware-consistency.md` | `caaa9920940dc6aea168bca5de2f69a45fda30b2b49c8066b817113356a85d08` |

The requirements file hash is after one orchestrator edit via `req.py set HWC-2 --criteria` (the author's copy was
`e5e386de…`): the plan's HWC-2 criterion said the `RESULT:` shape "matches check_capacity.py", whose line carries an
`info` count; `01-normative.md` §1.3 fixes the four-count shape of `check_docs.py`, which the tests assert. Plan defect,
author's finding; `02-requirements.md` Step 2 corrected to match.

Author's "not testable from results", accepted as-is (no test asserts them): the stderr wording on exit 2 beyond "exactly one
line, names `face3drv2605Device` where §1.5 does"; four exit-2 causes with no fixture (unreadable file, no configure call,
label in no mux block, no `&flash0`/`reg`). Additions the author made inside the criteria: a `DT_SIZE_K(16384)` literal case;
a control run in each exit-2 test (without it, Python's own exit 2 for a missing script satisfied the assertion).

Constraint the tests impose that the plan states only implicitly: stdout is **exactly** the §1.3 lines — no `source:` lines,
no blank lines — because the parser requires the HWC-2 and `RESULT:` lines to directly follow the offender block.

## Stage 4 / Stage 5 — 2026-09-26, tree e1eced14 + Cycle I working set + Cycle J
Coder: Opus, general-purpose agent under the `cdh-coder` contract. Orchestrator re-ran everything from a clean
`build-gtest` rather than trusting the report. Both pinned hashes unchanged before and after; every frozen file
(`verify.sh`, `check_capacity.py`, `conftest.py`, `tooling.md`, `topology.fpp`) unchanged; `git status` delta against the
Stage 3b baseline is exactly the two flight files and the new script; `git diff -U0` on flight paths is the four
`muxChannelNDevice` identifiers (`ReferenceDeploymentTopology.cpp:181-184`) and `DT_SIZE_M(4)` → `DT_SIZE_M(16)`
(`v5.dtsi:66`), nothing else.

Gate: `result: PASS`, `unverified: (none)`; script tests 163 (was 150, +13); host unit tests 30/30; matrix 334 / 152 / 96
(was 332 / 150 / 94 — the two HWC rows, both passing); docs lint OK with the DOCS-7 orphan WARN now listing the
cycle-j files (cleared by this cycle's row in `cycles/README.md` when it lands); pre-commit all hooks passed. Bare run:
`HWC-1 … 17 managers checked, 0 mismatched — OK`, `HWC-2 … reg 16777216 bytes, partitions end 16777216 bytes — OK`,
`RESULT: OK — 2 ok, 0 warn, 0 skip, 0 fail`, exit 0. Before the flight edits the same tests were 11 passed / 2 failed
(the two current-tree tests) — the results-first evidence.

Target build (build copy, rsync per CLAUDE.md, `fprime-util build`, no `generate --force` needed — the `.dtsi` edit
re-ran CMake by itself): FLASH 726656 B / 1044144 B (69.59 %), RAM 331376 B / 520 KB (62.23 %) — byte-identical to the
pre-change baseline built the same day; generated `zephyr.dts:256` now `reg = < 0x10000000 0x1000000 >` (was `0x400000`);
the five partition `reg` lines unchanged; dictionary 387 / 244 / 697 / 106 at `v1.2.0-82-ge1eced14-dirty`, identical
(no `.fpp` change). The FLASH *region* (1044144 B) is the slot, not `flash0`, so the app's link size cannot move with
this fix — as expected.

Not verified here (deferred, not failures): `MuxUnhealthy` now reporting the right face needs a bench with one mux
segment disconnected; the RP2350 flash driver reaching addresses above 4 MiB needs something to mount
`storage_partition`; the image was built, not flashed; CI not run (no push authorised).

## Follow-ups (recorded here because ROADMAP.md and the ledger are Cycle I paths, frozen until it commits)
1. `verify.sh` audit stage: echo `check_hardware_consistency.py`'s three lines next to `check_capacity.py`'s (one line;
   AUDIT-9's "contains every capacity line" assertion is unaffected). Enforcement already runs through `script-tests`.
2. Widen HWC-1's instance pattern to `tmp112BattCell[1-4]Manager` (`ReferenceDeploymentTopology.cpp:165-168`, all on
   `muxChannel4Device`, all correct today) — coder finding; the normative fixed the set to the three face kinds.
3. Filesystem for `storage_partition` (LittleFS vs FATFS on flash; `FsFormat` already takes a partition id).
4. Report C-08 upstream to Open-Source-Space-Foundation (inherited PROVES code).
5. Ledger lines for `04-findings.md` §1 (C-08 effect is a health check, not routing) and the coder's findings, and the
   Cycle J row in `cycles/README.md` — both after Cycle I's commit, in the same commit as this cycle's docs.
