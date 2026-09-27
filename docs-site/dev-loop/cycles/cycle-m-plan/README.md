# Cycle M plan — A8 DataRecorder, commit rows A8-0 .. A8-6 (index)

Branch `feat/data-recorder` @ 6296ef32 (= `main` e1eced14 + Cycle J's two fixes), with other cycles' work present and
uncommitted in the tree (§Hazards). Plan only: no code, tests or commits were made while writing it. `$R` =
`/Users/jesse-cm/scalar-wt/data-recorder` (git worktree of the Documents checkout; review amendment 1 — `lib/` submodules are empty there, read framework headers from the Documents checkout); `$PY` =
`fprime-venv/bin/python3`; `P` = `PROVESFlightControllerReference`; `T` = `P/ReferenceDeployment/Top`; `UT` =
`P/test/unit-tests`. Paths are relative to `$R`. Design input (not re-derived here): `docs-site/dev-loop/design/stored-data/`
(re-baselined 2026-09-27) and `../commit-plan-E-A8-A9.md` rows A8-0..A8-6. Where this plan and the design differ, this plan
wins and `04-findings.md` §B says why.

**Stage -1:** upstream count = 0 (no upstream sync in this cycle). **Stage 0:** `executable levels: Unit` — host gate
PASS; board rows get a `test/int` pytest, marked deferred (⏸) by `generate_rtm.py --env host`. A Zephyr target compile is
available from the clean-path build copy (`03-advisory.md` §6) and is a gate for A8-2 and A8-3.

**User decisions, 2026-09-27:** build A8-0..A8-6 **including A8-3** (ROADMAP standing rule 3 waived for this cycle; rollback
is `git revert` of A8-3). A8-7 (inline mode) out of scope. A9 out of scope, but A8 leaves room for stream 2 `burst`.

| File | Read it when |
|---|---|
| `01-normative.md` | writing tests: the component interface, file formats byte for byte (with a golden vector), behaviour rules, API names of the codec/ring, the host fakes' observable behaviour, the reader's CLI, the sequence, harm table, expected deltas |
| `02-requirements.md` | Stage 3b: the sdd seed, verbatim `req.py add` (DataRecorder-1..12) and `req.py set` (DH-L2-03/04/05/08/12, CDH-16) commands, and which test claims which ID |
| `03-advisory.md` | coding: file layout, CMake, stubs, locking and staging algorithm, scan and retention bookkeeping, target-compile procedure, open risks |
| `04-findings.md` | merging into the ledger later: verified facts (file:line) and every brief/design claim found wrong |

**Normative (tests derive from these only):** this README's §Rows, `01-normative.md`, `02-requirements.md`. **Advisory:** `03-advisory.md`.

## Rows (normative: files, gate, image effect, revert result)

Every row passes `VERIFY_ENV=host scripts/verify.sh` (`result: PASS`) on its own before its commit. Commit per row, in order.

**A8-0 `docs(requirements): DataRecorder rows and stored-data criteria (A8 Phase 0)`** — who: test author (req.py) + orchestrator (design files).
- Files: the five `docs-site/dev-loop/design/stored-data/*.md` and the two `cycles/*-E-A8-A9.md` (orchestrator's edits, already in the tree);
  new `P/Components/DataRecorder/docs/sdd.md` (seed, `02-requirements.md` §1); `docs-site/requirements/cdh.md` (via `req.py set`);
  `docs-site/requirements-matrix.md` (regenerated).
- Gate: host gate; `generate_rtm.py` prints no warnings. Image: no change. Revert: removes rows DataRecorder-1..12 and restores
  today's DH-L2-03/04/05/08/12 and CDH-16 texts.

**A8-1 `feat(DataRecorder): SegmentCodec and PacketRing (F'-free) with host tests`**
- Files: new `P/Components/DataRecorder/SegmentCodec.hpp`, `SegmentCodec.cpp`, `PacketRing.hpp`; new `UT/test_DataRecorder_Codec.cpp`;
  `UT/CMakeLists.txt` (+ `data_recorder_codec`); `UT/support/config/FppConstantsAc.hpp` (+ `FW_COM_BUFFER_MAX_SIZE = 227`, additive).
- Gate: host gate (host binaries 30 → 31, all pass). Image: no change (nothing registered with the F´ build). Revert: removes the
  codec and its test; A8-2 must be reverted first.

**A8-2 `feat(DataRecorder): component with config PersistedRecord (unwired)`**
- Files: new `P/Components/DataRecorder/{DataRecorder.fpp,DataRecorder.hpp,DataRecorder.cpp,DataRecorderCfg.hpp,CMakeLists.txt}`;
  `P/Components/CMakeLists.txt` (+1 line after `ComDelay/`); `P/Components/DataRecorder/docs/sdd.md` (body sections only, table
  untouched); new `UT/support/Os/Directory.hpp`, `UT/support/Fw/Com/ComBuffer.hpp`,
  `UT/support/PROVESFlightControllerReference/Components/DataRecorder/DataRecorderComponentAc.hpp`; `UT/support/Os/File.hpp`,
  `UT/support/Os/FileSystem.hpp` (additions only, `01-normative.md` §9); new `UT/test_DataRecorder_Component.cpp`; `UT/CMakeLists.txt`
  (+ `data_recorder_component`).
- Gate: host gate (binaries 31 → 32); target compile from the build copy after `fprime-util generate --force` succeeds;
  dictionary counts identical to today (387 commands / 106 parameters / 244 channels / 697 events / 23 packets).
- Image: no instance, so no behaviour change. Revert: removes component, fakes' additions and tests; A8-3 must be reverted first.

**A8-3 `feat(topology): DataRecorder tap on both splitters, 1 Hz slot 21, channels in FileSystem packet`** — the only activation commit.
- Files: `P/project/config/TlmPacketizerCfg.hpp:21-22` (`MAX_PACKETIZER_CHANNELS` 256 → 288, same commit, before the channels);
  `T/instances.fpp` (+ `instance dataRecorder: Components.DataRecorder base id 0x10080000` after `:278`); `T/topology.fpp`
  (+ `instance dataRecorder` after `:95`; + a new `connections DataRecorder { ... }` block, `01-normative.md` §1.2);
  `T/ReferenceDeploymentPackets.fppi` (14 channels appended to `packet FileSystem id 5 group 5`, after `fsSpace.TotalSpace`).
- Gate: host gate; target build (`generate --force`); dictionary deltas and RAM/FLASH ceilings of `01-normative.md` §13;
  `git diff --stat -- lib/` empty. Image: yes.
- Revert (`git revert <A8-3>` alone): dictionary back to 387/106/244/697/23, `FileSystem` back to its two members,
  `MAX_PACKETIZER_CHANNELS` back to 256, `check_capacity.py --dictionary none` output identical to today's; component, fakes and
  tests stay and stay green.

**A8-4 `tools(recorder): recorder_reader.py and per-pass downlink sequence`**
- Files: new `tools/recorder_reader.py`, `sequences/pass_recorder.seq`, `scripts/tests/test_recorder_reader.py`; `tools/README.md` (+1 section).
- Gate: host gate (script tests +N, all pass); `ruff check tools/recorder_reader.py scripts/tests/test_recorder_reader.py`;
  `fprime-seqgen` compiles the sequence against the build-copy dictionary of the A8-3 image. Image: no change. Revert: standalone.

**A8-5 `test(int): data_recorder_test.py; HP-12 segment power-cut loop; HP-13 extension`**
- Files: new `P/test/int/data_recorder_test.py`; `docs-site/dev-loop/hardware-procedures/HP-12-power-cut-persistence.md`,
  `HP-13-rf-silence-buffering.md`, `hardware-procedures/README.md` (rows DH-L2-05/08/12, CDH-16 at `:73-77` and the not-implemented
  list at `:141`).
- Gate: host gate (int collect + ruff are in it); the four board IDs show ⏸ deferred in the matrix. Image: no. Revert: standalone.

**A8-6 `docs(requirements): A8 matrix, component page, nav`**
- Files: `Makefile` (+ `cp .../DataRecorder/docs/sdd.md docs-site/components/DataRecorder.md` after `:118`); `mkdocs.yml`
  (+ `- Data Recorder: components/DataRecorder.md` as the first Storage Components entry, before `:105`); `docs-site/components/DataRecorder.md`
  (written by the docs-sync hook, never by hand); `docs-site/requirements-matrix.md` (regenerated).
- Gate: host gate; RTM without warnings; the docs-sync hook passes on its rerun. Image: no. Revert: standalone.
- The ledger merge the commit plan lists for A8-6 is **deferred**: `docs-site/dev-loop-findings.md` is frozen (§Hazards). The
  orchestrator merges `04-findings.md` after Cycles I-L commit.

## Hazards (read before touching the tree)

- **Frozen paths — nothing in this cycle edits, stages or commits them.** The baseline `git status --short` (39 lines,
  scratchpad `baseline-status.txt`) plus one path that appeared since: modified `docs-site/dev-loop-findings.md`,
  `docs-site/dev-loop/README.md`, `docs-site/dev-loop/ROADMAP.md`, `docs-site/dev-loop/cycles/cycle-i-review.md`,
  `docs-site/dev-loop/decisions/README.md`, `docs-site/dev-loop/design/activity-axis.md`, `docs-site/dev-loop/design/parameter-policy.md`,
  `docs-site/requirements-matrix.md` (generated, see below), `docs-site/requirements/tooling.md`, `scripts/verify.sh`; untracked
  `.claude/settings.local.json.bak`, `SCALAR Command and Data Handling CDR.pdf`, `docs-site/dev-loop/STATUS.md` (generated),
  `docs-site/dev-loop/cycles/cycle-j-plan/`, `cycle-j-review.md`, `cycle-k-plan.md`, `cycle-k-review.md`, `cycle-l-plan/`,
  `docs-site/dev-loop/decisions/D-006-baseline-cadence-home-startup-seq.md`, `docs-site/dev-loop/design/driver-board-firmware.md`,
  `docs-site/requirements/bench.md`, `docs-site/requirements/hardware-consistency.md`, `proves-architecture.html`,
  `scripts/check_docs.py`, `scripts/check_hardware_consistency.py`, `scripts/doc_status.py`, `scripts/tests/test_check_docs.py`,
  `scripts/tests/test_check_hardware_consistency.py`, `scripts/tests/test_doc_status.py`, `scripts/tests/test_verify_sh_docs.py`,
  `stored-data-design.html`, `telemetry-gate-final-changes.html`.
- **Appeared during planning (2026-09-27, another cycle — apparently Cycle K — is working in the tree):** modified `Makefile`,
  `docs-site/dev-loop/hardware-procedures/HP-11-electrical-actuators.md`, `docs-site/dev-loop/hardware-procedures/README.md`;
  untracked `scripts/make_bench_sequence.py`, `scripts/tests/test_make_bench_sequence.py`, `sequences/bench_startup.seq`. Frozen
  as well. **Conflict:** A8-5 edits `hardware-procedures/README.md` and A8-6 edits `Makefile`. Do those two edits only after that
  cycle has committed (or stage only this cycle's hunks with `git add -p`); never commit the other cycle's hunks.
  Re-take `git status --short` before Stage 3b and before each commit.
- **This cycle's own design edits** — the five `docs-site/dev-loop/design/stored-data/*.md` and
  `docs-site/dev-loop/cycles/commit-plan-E-A8-A9.md`, `cycle-sequencing-E-A8-A9.md` — are frozen for the coder and the test author;
  only the orchestrator edits and commits them (in A8-0).
- Generated files the gate rewrites (`docs-site/requirements-matrix.md`, `docs-site/dev-loop/STATUS.md`) carry every cycle's
  deltas; that is expected. Stage only the matrix, and only in A8-0 and A8-6.
- `lib/` is never edited. `~/scalar-build/proves-core-reference` is a one-way rsync build copy: build there, never edit there.
- **Cycle L (plan only, unapproved)** would also add instances with "the next free base id", 2 channels, 2 commands, ~5 events
  and 1 Hz slot 19. A8 claims base id `0x10080000` and slot 21. If L lands first, A8-3's absolute dictionary numbers shift by
  L's deltas (the deltas in `01-normative.md` §13 still hold) and the channel count becomes 260/288 (> 90 %: `check_capacity`
  WARN, not FAIL).
- This plan directory is not yet linked from `docs-site/dev-loop/README.md` (frozen): `check_docs.py` rule 7 reports it as an
  orphan (WARN only) until the orchestrator adds the cycle-index row.
