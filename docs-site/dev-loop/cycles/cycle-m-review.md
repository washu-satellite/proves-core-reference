# Cycle M review — A8 DataRecorder (plan `cycle-m-plan/`, tree `feat/data-recorder` @ 6296ef32)

**Verdict: Ready, with amendments 1–8.** Reviewed by the orchestrator on 2026-09-27 (planner: Opus, general-purpose agent
under the `cdh-planner` contract; no second reviewer in this session; recorded). Load-bearing claims checked at source:
slot 21 free and slot 20 = `faultManager.run` (`topology.fpp:330`, `check_capacity` 18/25 max index 20); `FW_COM_BUFFER_MAX_SIZE
= 227` (`FpConstants.fpp:22`); channels 244/256 and packets 23/24 (`TlmPacketizerCfg.hpp:19,22`, `check_capacity`);
`ComSplitter.comOut: [5]` and the two unindexed taps per splitter (`ComSplitter.fpp:10`, `topology.fpp:153-161`);
`fileDownlink.SendFile` is a guarded `Svc.SendFileRequest` input (`FileDownlink.fpp:14`), unconnected today; base id
`0x10080000` absent from `instances.fpp` and every `ComCcsds.fpp`; `TlmPacketizer` and `EventManager` are `active`
(`TlmPacketizer.fpp:3,37`, `EventManager.fpp:4`); `ZephyrDirectory::rewind` returns `NOT_SUPPORTED`
(`fprime-zephyr/Os/Directory.cpp:47-49`); `FS_FATFS_NUM_FILES` default 4 (`Kconfig.fatfs:72-75`), not overridden;
`NUM_TASKS = 5` with five enumerators (`TaskGate.fpp:9-18`); `FileSystem` packet after A8-3 = 15 B header + 2×U64 + 14×U32
= 87 B ≤ 227. The 20-byte header arithmetic (4+1+1+2+4+4+4) is right; the design's "16 B" was wrong.

## Deliberate behaviour changes accepted (all land in A8-3 only; one `git revert` removes them)
- A third output on each splitter: the recorder receives every telemetry and event packet after the LoRa and UART queues.
- 1 Hz member 21 (`dataRecorder.schedIn`), after `faultManager.run`. At most one SD `write` + `flush` + `removeFile` per
  stream per tick; unmeasured on target (open risk 1).
- Dictionary: +9 commands, +14 channels, +8 events; `MAX_PACKETIZER_CHANNELS` 256 → 288 (≈ +4 KB RAM in the packetizer).
- `FileSystem` packet (id 5, group 5) grows from 35 B to 87 B on the wire when ops select level 5.
- Requirement text changes by `req.py` (A8-0): DH-L2-03 method Inspection → Unit Test; criteria of DH-L2-03/04/05/08/12 and
  CDH-16 now name the recorder. Status columns untouched.

## Amendments (normative; override the plan where they differ)
1. **The cycle runs in a git worktree, not the Documents checkout.** `$R` = `/Users/jesse-cm/scalar-wt/data-recorder`
   (branch `feat/data-recorder`; no spaces). Another session is live in the Documents tree (Cycle K), which breaks the
   single-writer rule there. In the worktree the four `lib/` submodules are **empty directories**: read framework headers from
   `/Users/jesse-cm/Documents/Documents - Jesse's Mac/scalar-softwarestack/proves-core-reference/lib/...` (read-only, quote
   the path). `fprime-venv` and `.venv` are symlinks to that checkout's venvs. The host gate (`VERIFY_ENV=host scripts/verify.sh`)
   runs in the worktree. Target compile: rsync from the worktree to `~/scalar-build/proves-core-reference` with the CLAUDE.md
   exclude list **plus `--exclude 'lib/'`** (the copy already holds the full `lib/`; this cycle never changes it).
2. **Frozen-path hazard replaced.** The worktree's baseline is: untracked `docs-site/dev-loop/cycles/cycle-m-plan/` and this
   file; modified `docs-site/dev-loop/design/stored-data/*.md` (5), `docs-site/dev-loop/cycles/commit-plan-E-A8-A9.md`,
   `cycle-sequencing-E-A8-A9.md`. Any other change in `git status` that an agent did not make is a stop. The plan README's
   list of Cycle I–L paths does not apply in the worktree (they are not there); A8-5 may edit
   `hardware-procedures/README.md` and A8-6 may edit `Makefile` directly. Conflicts with Cycle K are resolved at merge time.
3. **Interface is fixed by `01-normative.md` §1–§11 and `02-requirements.md`.** Names marked **[decided]** are final for this
   cycle. A coder mismatch is a plan defect reported back, never a test edit.
4. **Locking is normative, not advisory.** Every ring access from `schedIn` and from a command handler happens under the
   component lock (`lock()`/`unLock()`), and no `Os::` call, port call, event, telemetry write or command response happens
   while it is held (01 §7.8). The recorder stub records `portCallWhileLocked`; the component test asserts it stays false.
5. **Tests on the real tree assert results, not counts of the tree.** The codec test reproduces the golden vector byte for
   byte; the component test drives the stub and the Os fakes only; the reader test uses a fixture dictionary under
   `scripts/tests/`, never the build-copy dictionary.
6. **Row order and single writer.** Stage 3b writes every test for A8-1, A8-2, A8-4 and A8-5 and runs the A8-0 `req.py`
   commands in one pass; hashes are pinned below before any coder starts. Coders run one row at a time (A8-1, A8-2, A8-3,
   A8-4, A8-5, A8-6) in the worktree; the orchestrator gates and commits after each row. A8-3's target build and dictionary
   deltas (01 §13) are measured by the orchestrator before its commit.
7. **DataRecorder-12 stays in the `DataRecorder` group** (open risk 10 accepted): the reader is the component's deliverable.
8. **Board rows stay deferred.** `data_recorder_test.py` is collected and linted only; DH-L2-05/08/12 and CDH-16 show ⏸ in the
   matrix. Nothing in this cycle claims DH-L2-01/07/09/10/11/13/14, CDH-8/14/27 or FD-L2-04.

## Tests (Stage 3b) — pinned after the test author reports
_(hashes recorded here by the orchestrator before Stage 4; the coder inherits these read-only)_
