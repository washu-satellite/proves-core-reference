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
   the path). `fprime-venv` and `.venv` are symlinks to that checkout's venvs. The host gate runs in the worktree as
   `PATH="/Users/jesse-cm/.cache/uv/archive-v0/7tw0Reg0rGT9KFKdw5ogI/bin:$PATH" VERIFY_ENV=host scripts/verify.sh`
   (that directory holds the `pre-commit` the git hook uses; do **not** put `fprime-venv/bin` on PATH — its `cmake` shim
   has a dead interpreter path and the host build then fails silently). Gate PASS recorded 2026-09-27 before Stage 3b. Target compile: rsync from the worktree to `~/scalar-build/proves-core-reference` with the CLAUDE.md
   exclude list **plus `--exclude 'lib/'` and `--exclude '.git'`** (the copy already holds the full `lib/`; this cycle never changes it; the worktree's `.git` is a file and the copy's a directory, so rsync exits 23 on it otherwise — coder finding).
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

## Amendments 9–11 (after Stage 3b; from the test author's report)
9. **Rows A8-1 and A8-2 are one coder pass and one gate, two commits.** Every `test_*.cpp` builds in one `cmake --build`, so
   `test_DataRecorder_Component.cpp` (A8-2's contract) breaks the host build until `DataRecorder.hpp` exists; A8-1 alone cannot
   be gated. The coder delivers both rows together; the orchestrator gates once (green) and then commits A8-1 (codec sources,
   codec test, its CMake library, `HOST_TEST_BINARIES` 30 → 31) and A8-2 (everything else, `HOST_TEST_BINARIES` → 32). Revert
   order stays A8-2 before A8-1.
10. **`scripts/tests/test_verify_sh.py:35` `HOST_TEST_BINARIES` is a count pin, not a recorder test.** The coder may change that
    one constant (30 → 31 in A8-1, → 32 in A8-2) and nothing else in that file; it is the single exception to the read-only
    rule on `test/` and `scripts/tests/`.
11. **Interface names the test author chose are final** (its report §3): `Components::PacketRing<SLOTS, SLOT_BYTES>` and
    `Components::flushDue` in `PacketRing.hpp`; `Components::DataRecorder(const char* compName)` with public base
    `DataRecorderComponentBase`, no init/configure call before the first `schedIn`; enums and `Svc::SendFileResponse` reached on
    the host only through the stub; `SET_FLUSH(stream, U8 records, U16 intervalS)`; `sendFileOut_out(FwIndexType, const
    Fw::StringBase&, const Fw::StringBase&, U32, U32)`; `tlmWrite_{Tlm,Evt}<X>(U32)`; `log_<SEV>_<Name>` with enum args by
    `const&`; `LIST_SEGMENTS(stream, 0xFFFFFFFF)` answers OK with no `SegmentInfo` once the scan is done (the scan-complete
    probe). The stub takes the lock in `tlmIn_handlerBase`/`evtIn_handlerBase` only; `schedIn` and commands lock themselves.
    Not asserted by any test (open for the coder, still bound by 01 §7): the exact rotation tick at `SEGMENT_MAX_S`,
    `OldestRecordAgeS`, the `ConfigCorrupt` status of a 27-byte payload, "≤ 3 file opens".

Not gated separately: the A8-0 requirements commit below lands while the tree holds the untracked Stage 3b tests, which
break the host build until A8-2 (the red window D-002 expects); its own checks are the pre-commit hooks and the RTM
regeneration (12 `DataRecorder` rows, DH-L2-03/04 Unit, DH-L2-05/08/12 and CDH-16 Board).

## Tests (Stage 3b) — pinned 2026-09-27
Test author: Opus, general-purpose agent under the `cdh-test-author` contract; reviewed by the orchestrator against the
criterion sentences of `02-requirements.md` (golden vector verbatim; age rule at +61 not +60; DH-L2-04's 17 rejections;
every host claim a Unit-level ID; `File.hpp`/`FileSystem.hpp` diffs add-only, 61/69 lines, 0 removed). 25 + 44 gtests,
15 pytest cases, 4 board tests (⏸). All red on the tree until Stage 4. The coder inherits every path below read-only
(amendment 10 excepted); any drift before a commit is a coder edit and the row goes back.

| sha256 | path (`P` = `PROVESFlightControllerReference`) |
|---|---|
| `07676c3d6afca43c7ffc83bfa0065af783c916365fbe8a0143ad0ba43fc3d8e5` | `P/test/unit-tests/test_DataRecorder_Codec.cpp` |
| `38dc69a5e219677057f1db01a9ecffbd290b89c2d4c67b864ff9aa95f78e98b9` | `P/test/unit-tests/test_DataRecorder_Component.cpp` (re-pinned after the Stage 4 test-defect fix at line 941: prefix check per 01 §7.7; was `abbdfba7…df68`) |
| `22deb3ee04ee32ee5f3ffac8430dc509fce74f26958ac760305221193acc5f8f` | `P/test/unit-tests/support/Os/Directory.hpp` |
| `8c4d69025f59fca7e1829b847f639bec34d400ba0a6cadeab66a24c8c71ea6dc` | `P/test/unit-tests/support/Os/File.hpp` |
| `fb5cc7d92f27c94c480c23d8cd478e52bd8bb63c7ca88691ff429618f36b4b35` | `P/test/unit-tests/support/Os/FileSystem.hpp` |
| `cb28fb8a4733cbbb3be26f56b8316b61e2341716a774fe6559c641f9f73ace14` | `P/test/unit-tests/support/Fw/Com/ComBuffer.hpp` |
| `3d806c171f8a40583d877d0333c262ef53d3a620deaec7280bae7082fb9a2008` | `P/test/unit-tests/support/PROVESFlightControllerReference/Components/DataRecorder/DataRecorderComponentAc.hpp` |
| `a1031a4307c4a257a051916c21649cfd01d167ea675d65bc20a1511b4594867e` | `scripts/tests/test_recorder_reader.py` |
| `f2e70f4fbbaee2d48a0b8e6d6e7246d1fbf391232a76ad67b4bc8c8c9fd5ee1c` | `scripts/tests/fixtures/recorder_reader_dictionary.json` |
| `841dec058248782ba59011eeee4563c92e08a0dc6ad681a4b4d412baa123daea` | `P/test/int/data_recorder_test.py` |
| `36633d77cc99e04387aa548ec704ec9177ad1544d99a8502d3ed6d14f418c1e3` | `P/Components/DataRecorder/docs/sdd.md` (`## Requirements` table; A8-2 adds body sections above it, table unchanged) |
