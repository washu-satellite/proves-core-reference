# 03 — Commit rows (Results = normative; How = advisory)

Every row builds and passes `VERIFY_ENV=host scripts/verify.sh` on its own; rows touching `.fpp`, topology or `project/config` also pass the target compile from the clean-path copy and the dictionary check. Revert in reverse order only. Branch: `feat/driver-board` (owner may rename to `sync/upstream-4.3.0`).

## F0 — `build(tooling): environment for the F' 4.3.0 / Zephyr 4.4.1 sync` (before the merge)

**Results.**
1. Build copy `~/scalar-build/proves-core-reference` builds pristine `proves-origin/main` a477893b: `fprime-util generate && fprime-util build` succeed with Zephyr v4.4.1 (`lib/zephyr-workspace/zephyr/VERSION` reads 4.4.1), modules per upstream `west.yml` (`tf-psa-crypto` present, `tinycrypt` absent), venv pins `fprime-tools 4.3.0, fprime-fpp 3.3.0, fprime-gds 4.3.0, fprime-fpy 0.5.1, pytest 9.0.3, spacepackets 0.32.0, fastcrc 0.3.6` (`lib/fprime/requirements.txt` at 7d8f579f), plus upstream's top-level `requirements.txt` (fprime-yamcs 0.1.3, fprime-xtce 0.1.2). Zephyr SDK: whichever `lib/zephyr-workspace/zephyr/SDK_VERSION` names (`makelib/zephyr.mk:11`); `~/zephyr-sdk-0.17.4` and `~/zephyr-sdk-1.0.1` are both installed.
2. **Upstream baseline recorded** in the ledger: FLASH, RAM, dictionary counts (commands, params, channels, events, packets) of a477893b. This is the reference the harm table compares against.
3. Source-tree `fprime-venv` carries the same pins (it runs `pytest --collect-only`, `ruff`, `req.py`, `generate_rtm.py`, `check_packet_set.py`; upstream's `conftest.py` and tests import fprime-gds 4.3.0). Today it holds 4.1.x (`07-findings.md`).
4. `scripts/check_packet_set.py` accepts either `TLMPACKETIZER_HASH_BUCKETS` (pre-merge) or `MAX_PACKETIZER_CHANNELS` (post-merge) as the channel limit and reports which one it used; its channel count includes packets and omit block (already true, `:70-74`); it accepts unqualified names (`qualify()`, `:58`, already true). Gate output before the merge is unchanged (23 packets ≤ 24, 241 ≤ 256).
5. The two stray `lib/fprime` edits are discarded (`git -C lib/fprime diff --stat` empty).
6. `docs-sync` hook: decision — **run it**. Evidence: `make -n docs-sync` at this path exits 0 and lists 38 relative-path `cp` lines (the "make fails here" note in `CLAUDE.md` is about targets that embed `$(CURDIR)`; the spurious targets `Documents`, `-`, `Jesse's` only warn). Verified for real at the F1 gate (`verify.sh` runs `pre-commit run --all-files`). Fallback if it fails: `export SKIP=docs-sync` in `scripts/verify.sh` and in the orchestrator's commit environment, and `verify.sh` regenerates the copies with the same `cp` list; the result either way is "every `docs-site/components/<X>.md` equals its `Components/<X>/docs/sdd.md`" (`05-verification.md` §5).

**Gate.** `verify.sh` PASS on the unmerged tree; upstream build in the copy succeeds; ledger line with the baseline numbers.
**Revert consequence.** Repo side: only `check_packet_set.py` (and `verify.sh` if the fallback was taken). Environment side is not revertible by `git`; the old toolchain is not needed again once F1 lands.
**How (advisory).** In the build copy: `git fetch proves-origin && git checkout --detach a477893b && git submodule update --init --recursive`, then upstream's own recipe (`make zephyr-workspace` / `west update` per `makelib/zephyr.mk:16`) — the copy's path is clean so `make` works there. Then `fprime-venv/bin/pip install -r lib/fprime/requirements.txt -r requirements.txt`. Read the `grounded-mutation` guidance before `west update`: it rewrites `lib/zephyr-workspace/**`. Later rsyncs from the source tree should add `--delete` (never `--delete-excluded`) so deleted components do not linger in the copy.

## F1 — `merge: proves-origin/main a477893b (F' 4.3.0, Zephyr 4.4.1, TcSecurityDeframer/ProvesRouter)` (merge commit)

**Results.**
1. `git log --merges -1` shows the merge of a477893b; every path in `02-resolutions.md` resolves as stated; no `<<<<<<<` anywhere; `git diff --stat lib/` empty after `git submodule update`.
2. **Minimum resolution set that makes this row build and pass the gate** (nothing less): (a) A-table deletions; (b) `Components/CMakeLists.txt` union; (c) ModeManager `.cpp/.fpp/.hpp` both-sides + `MAX_SAFE_MODE_REASON = 6`; (d) StartupManager theirs-for-boot-count, ours-for-quiescence, dead helpers removed; (e) the fppi in group syntax with 23 packets / 243 channels and `TlmPacketizerCfg.hpp` 24 / 256 — the target build needs both, the boot assert needs the second; (f) `topology.fpp` slot 19 and `faultIn[2]` lines removed; (g) `const&` on the three command handlers + their `.cpp` + the two stubs; (h) `test_ModeManager_StatePersistence.cpp` `restorePersistentState()` after `init(0)`; (i) host fakes for what the merged ModeManager/StartupManager now include (`Os/Mutex.hpp` with `ScopeLock`, `Fw/Serializable`-shaped `ExternalSerializeBuffer` for `FwSizeType` only, `Fw::TimeIntervalValue`, and the stubs' new ports/params/events listed in `05-verification.md` §fakes); (j) `unit-tests/CMakeLists.txt` union with the psa block; (k) `mode_manager_test.py` both blocks, `test_safe_12`, `slow` marker; `telemetry_sources_test.py:431`; (l) `pytest.ini`, `mkdocs.yml`, docs copies regenerated; (m) sdd tables via `req.py`: `add` MM0013, REQ-SM-009..012; `set` REQ-SM-008 text; boot-count host tests rewritten to claim REQ-SM-011/012.
3. **Deferred without breaking the build**: `faultManager.faultIn[2]` unconnected and `FaultCountCommandLoss` never increments (F3); sequence number persisted by upstream's `FileHelper` raw write at `//sequence_number.txt` (F2); `AUTH013` has no test (F2); CH-L2-05 has no test (`06-followups.md` §3); `fault_manager_test.py` docstring still names the router (F3); ledger/traps stale (F5).
4. Default flight behaviour after F1, stated as upstream's: command loss enters SAFE_MODE(COMMAND_LOSS) and stops the watchdog after `COMM_LOSS_TIME` (3 days) of no routed packet — **this is the one deliberate behaviour change the owner accepted (decision 4)**; before the sync the same action lived in `AuthenticationRouter` with the same default. Boot count: first post-sync boot reads the fork's `SBC1` record as a short read → count 1 (no event) — accepted (decision 2). All other defaults equal today's (`04-harm-table.md`).
5. Dictionary: 23 packets, 243 channels; commands/params/events recorded (not predicted) and compared with the F0 baseline: delta = fork's own surface (DriverBoardHandler +6 cmds +10 prm-cmds +5 prm +22 ch +12 ev, FaultManager, TelemetryGate, TaskGate, TcFrameCorrector, `Faults` packet) — see `05-verification.md` §3 for the arithmetic to check.

**Gate.** `verify.sh` PASS with `unverified: (none)`; target compile; dictionary counts; FLASH/RAM recorded; Cycle E tests byte-identical (`git diff d4fda377 -- test/unit-tests/test_DriverBoard*.cpp test_Crc16.cpp test_TcFrameCorrector*.cpp test/int/driver_board_test.py` empty).
**Revert consequence.** `git revert -m 1 <merge>` restores d4fda377's tree in one step (including Authenticate); the environment (F0) is left at 4.3.0, so a reverted tree no longer builds in the copy without re-running the old `west update` — say so in the commit body.
**How (advisory).** Resolve in the order of §2 so the build error list shrinks monotonically: CMake and deletions first, then fpp (run `fprime-util generate` in the copy early — 4.3.0 autocoder errors surface here), then `.hpp/.cpp`, then tests. Keep the resolved fppi diff against `proves-origin/main`'s version small: it should show only added lines (our channels and two packets).

## F2 — `feat(TcSecurityDeframer): persist the sequence number as a PersistedRecord`

**Results (interface).**
1. `SequenceNumberStore.{hpp,cpp}` live in `Components/TcSecurityDeframer/`, F'-free, taking the target path as an argument (temp = `<path>.tmp`), magic `ASN1`, 4-byte little-endian payload — the same record as at d4fda377.
2. `SEQ_NUM_FILE_PATH` default becomes `/sequence_number.bin` (was `//sequence_number.txt`); `SEQ_NUM_WINDOW` unchanged.
3. `TcSecurityDeframer::readSequenceNumber` / `writeSequenceNumber` (trial:190-219) use the store: MISSING → 0, no event, nothing written; CORRUPT → 0, exactly one new event `SequenceNumberRecordInvalid(status: U32) severity warning high id 15 throttle 2`, and the baseline is written back so it does not re-warn; store failure → existing `SequenceNumberWriteFailed` with `Os::FileStatus::OTHER_ERROR`; a valid record → its value, telemetered on `CurrentSequenceNumber` as today. The `DOESNT_EXIST` branch (trial:201) is gone (dead on Zephyr, `CLAUDE.md` trap 1).
4. Every accepted packet still persists the new number before `tlmWrite_CurrentSequenceNumber` (trial:82-87 unchanged in order); `SET_SEQ_NUM` still writes then sets (trial:130-147).
5. Tests: `test_TcSecurityDeframer_SequenceNumberStore.cpp` = the retired file with paths parameterised, 8 claims of `AUTH013`; host lib `tc_security_deframer_sequence_store`. Requirement text of `AUTH013` updated to name the parameter path. `TcSecurityDeframer/docs/sdd.md` gets the row and a "Persistence" paragraph; `PersistedRecord/docs/sdd.md` §Consumers lists TcSecurityDeframer.
6. Unchanged: `Makefile sync-sequence-number`, `sync_sequence_number_test.py`, `GET_SEQ_NUM`/`SET_SEQ_NUM` opcodes, both instances sharing one file.

**Gate.** host gate; target compile; dictionary +1 event ×2 instances, params unchanged in count.
**Revert consequence.** Standalone: reverting restores upstream's raw file at `//sequence_number.txt`; the ground resyncs through the 50000 window either way.

## F3 — `feat(ModeManager): report command loss to the FaultManager before acting`

**Results.**
1. In `commandLossCheck()` (trial:554-577), once the window expires and before `runSafeModeSequence()`, the component calls `faultOut_out(0, FaultType::COMMAND_LOSS, FaultSource::MODE_MANAGER, FaultSeverity::CRITICAL, <seconds since last packet>)` exactly once per loss episode (the debounce flag guards it as it guards the rest).
2. Disposition `OBSERVED` (shipped, and when `faultOut` is unconnected): the component proceeds exactly as upstream wrote it — `CommandLossDetected`, safe-mode sequence, `enterSafeMode(COMMAND_LOSS)`, `stopWatchdog_out`. Disposition `CLAIMED`: the component emits `CommandLossDetected` and returns; the FaultManager's own `forceSafeMode`/`stopWatchdog` outputs act (Cycle D contract, `FaultManager/docs/sdd.md`). No path stops the watchdog twice.
3. `faultManager.faultIn[2]` stays unconnected; `FaultInPorts` stays 4; `FaultSource::AUTH_ROUTER` keeps value 2 with a "retired" comment (no renumbering — `FaultCountCommandLoss` and the fault table key on `FaultType`, `FaultManager.cpp:129-136`).
4. Observables for FD-L2-01/05/09 producer 2 (Board, deferred): `faultManager.FaultConfirmed(COMMAND_LOSS)` in the same second as `modeManager.CommandLossDetected`; `FaultCountCommandLoss` +1; `ShadowActionsSuppressed` +1. `fault_manager_test.py:195-215` procedure re-worded: `PRM_SET COMM_LOSS_TIME` on `modeManager`, expect `modeManager.CommandLossDetected`.
5. Host test (Unit): the case in `01-scope.md` §2 last row — `faultOut` called once with the tuple above before `enterSafeMode`; OBSERVED → SAFE_MODE(COMMAND_LOSS) + one `stopWatchdog`; CLAIMED → neither. `MM0009/MM0010` tests unchanged.
6. sdd: ModeManager "Fault reporting" section gains the command-loss paragraph; `FaultManager/docs/sdd.md` producer table row 2 re-sourced.

**Gate.** host gate; target compile (no fpp change: `faultOut` exists); dictionary unchanged.
**Revert consequence.** Standalone: FaultManager loses producer 2 again; flight behaviour identical (shadow).

## F4 — folded

The fppi rewrite and the two constants are in F1 (the target build requires them); the checker retarget is in F0 (the host gate requires it on both sides of the merge). No separate row.

## F5 — `docs(dev-loop): Cycle F ledger, traps, requirement reasons, model re-read list`

**Results.**
1. `docs-site/dev-loop-findings.md`: every line in analysis §7.9 (17-21, 26, 28-31, 37, 47, 55, 66-69, 71, 73-75, 121-122, 130, 133, 142, 146, 156, 184, 196) gets an appended "— superseded by sync 2026-09 (F1 <hash>): <one clause>" note, not a deletion; `07-findings.md` merged as new lines; F0 baseline and F1 post-merge FLASH/RAM/dictionary lines added.
2. `CLAUDE.md`: trap 4 (`TLMPACKETIZER_HASH_BUCKETS`) rewritten to `MAX_PACKETIZER_CHANNELS` (packets **and** omit; `TlmPacketizer.cpp:86-87,148-149`); trap 3 (PrmDb) gains "4.3.0 file has a CRC header; a 4.1.1 file fails `PrmFileReadError`"; new traps: enum/struct **command** args are `const T&` in 4.3.0 handlers (port handlers were already so); `docs-sync` pre-commit hook runs `make docs-sync` (works here; `SKIP=docs-sync` only if F0 §6 fallback was taken); the "Persisted flight state" map row lists TcSecurityDeframer instead of Authenticate; the rsync line gains `--delete`.
3. `req.py set` reasons: FD-L2-09 criterion "authenticate RejectedPacketsCount" → "provesRouter.RejectedPackets"; CH-L2-05 criterion `SequenceNumberOutOfWindow` → `SequenceNumberInvalid` and reason "test retired at sync; see followups §3"; FD-L2-05 reason mentions the ModeManager path.
4. `Components/PersistedRecord/docs/sdd.md`: new normative section **"## Persistence rule"** (owner decision 2, verbatim): safety/regulatory value with no plausibility test → atomic write + PersistedRecord checksum; telemetry-only value with a plausibility test → atomic write + believability check (upstream #470 pattern); bulk records → per-record CRC; RAM → nothing. Table applying it: boot count (telemetry-only, plausible ≤ 1e6 → upstream pattern), quiescence start (safety: a bad value asserts in `Fw::Time::add` → PersistedRecord), ModeManager state, TelemetryGate tx state, sequence number (regulatory/safety → PersistedRecord), DataRecorder segments (bulk → per-record CRC, A8).
5. Matrix regenerated without warnings; `docs-site/components/*` equal their sdd; `mkdocs.yml` nav lists TcSecurityDeframer and ProvesRouter under Security.
6. Model re-read list (`01-scope.md` §4) copied into the cycle review for the orchestrator; the model repo commit is `model(all): upstream sync landed at <F1 hash>` after the re-read, outside this cycle's coder scope.

**Gate.** RTM regenerates without warnings; `verify.sh` PASS.
**Revert consequence.** Standalone.
