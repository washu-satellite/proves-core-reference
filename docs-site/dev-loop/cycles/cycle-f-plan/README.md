# Cycle F plan — upstream sync (`proves-origin/main` a477893b into `feat/driver-board` d4fda377) — index

Repo `$R` = `/Users/jesse-cm/Documents/Documents - Jesse's Mac/scalar-softwarestack/proves-core-reference`. Paths below are relative to `$R/PROVESFlightControllerReference/` unless they start with `$R/`, `lib/`, `docs-site/` or `scripts/`. `trial:` prefixes a line number in the read-only trial merge (`sync-trial` scratch worktree, HEAD d4fda377 merged with a477893b, 31 conflicted paths). Planned 2026-09-19 from `docs-site/dev-loop/cycles/upstream-sync-analysis.md` and the owner's eleven decisions of the same date, which this plan treats as fixed.

**Governing rules:** (1) must not hurt — the camera path, the DriverBoard link and every Cycle E test are unchanged and green after the sync; every behaviour change is one upstream made or the owner named; (2) Stage 0 — every row proves itself on the host gate plus a target compile from the clean-path build copy; board rows are deferred, never claimed by host tests.

| File | Read it when |
|---|---|
| `01-scope.md` | deciding which requirement rows the sync touches, which go deferred, and the new rows for the two ports (pass criteria from observables) |
| `02-resolutions.md` | resolving a conflicted file — **the per-file rule** (theirs / ours / merged-how) with trial-merge hunk lines |
| `03-rows.md` | committing — rows F0-F5, each with normative results, gate and revert consequence; the minimum set that makes the merge row build |
| `04-harm-table.md` | reviewing what could change and the measurement that proves it did not |
| `05-verification.md` | running the gates — exact commands, expected counts, what "deferred" covers in host |
| `06-followups.md` | after the cycle — A8 RAM guard, bit-flip protection, CH-L2-05 retarget, items the owner still has to answer |
| `07-findings.md` | merging verified facts (file:line) into the ledger; supersedes the analysis where they differ |

**Normative (tests, criteria and `req.py` rows derive from these only):** `01-scope.md`, `02-resolutions.md`, the *Results* column of every row in `03-rows.md`, `04-harm-table.md`, `05-verification.md`. **Advisory (methods; the coder may deviate if every normative result holds):** the *How* notes in `03-rows.md`, `06-followups.md` §rationale, host-fake layout in `05-verification.md` §fakes.

Summary in 12 lines:
1. One merge commit (`git merge proves-origin/main`), no rebase; theirs for the 70 paths the fork never touched (`prj.conf`, `west.yml`, `boards/`, `project/config/*` new files, `.github/`, `lib/*` pointers); the two stray uncommitted `lib/fprime` edits are discarded first.
2. `Authenticate` and `AuthenticationRouter` are deleted with all their files, tests and docs copies; upstream's `TcSecurityDeframer` + `ProvesRouter` replace them. Two hooks are ported afterwards: F2 (PersistedRecord sequence-number store into `TcSecurityDeframer`, `AUTH013` kept) and F3 (FaultManager command-loss producer re-sourced from ModeManager's `commandLossCheck()` through the existing `faultOut`, so FD-L2-01/05/09 keep their observable).
3. StartupManager takes upstream's boot-count code exactly (#470: flush, temp+rename, `MAX_PLAUSIBLE_BOOT_COUNT`, per-tick retry, `BootCountCorrupted`) and keeps our PersistedRecord quiescence file; the consequence-based persistence rule becomes a normative section of `Components/PersistedRecord/docs/sdd.md`.
4. ModeManager keeps our CRC state record, adopts upstream's `restorePersistentState()` call site (the `init()` override is already gone in the trial merge), raises `MAX_SAFE_MODE_REASON` 5 → 6, and carries both `faultOut` and `stopWatchdog`; FaultManager stays in shadow, upstream's command-loss path is the actor.
5. Packet set re-expressed in 4.2.2 group syntax: 23 packets (upstream's 21 incl. `Security id 6`, plus `Faults id 9 group 5` and `PayloadHousekeeping id 23 group 3`); 243 distinct channels = 173 in packets + 70 omitted (upstream 128 + 65; fork adds 45 + 5; `CdhCore.tlmSend.SendLevel` no longer exists at 4.3.0).
6. `MAX_PACKETIZER_CHANNELS` = 256 and `MAX_PACKETIZER_PACKETS` = 24: at 7d8f579f the channel limit bounds packet **and** omit channels (`Svc/TlmPacketizer/TlmPacketizer.cpp:86-87,148-149` insert into one `RedBlackTreeMap<…, MAX_PACKETIZER_CHANNELS>`), so upstream's 202 would assert at boot with 243. `scripts/check_packet_set.py` learns the new constant in F0 so the gate is green on both sides of the merge.
7. F' 4.3.0 forced edits: `TelemetryGate.hpp:52`, `TaskGate.hpp:52,62` (+ `.cpp` and the two recorder stubs) take enum command args by `const&`; no other fork handler is affected (verified by grep, `07-findings.md`).
8. Tests: our 257-line `safe_mode_test.py` extension merges into `mode_manager_test.py` (their `test_safe_09` becomes `test_safe_12`); `authentication_test.py` and `test_Authenticate_SequenceNumberStore.cpp` retire in F1 and come back in F2 as the deframer store's tests; `telemetry_sources_test.py:431` reads `provesRouter.RejectedPackets`; host build gains upstream's `psa/crypto.h` requirement, satisfied at `/opt/homebrew/include` by CMake's default prefix search.
9. F0 is an environment row: build copy to Zephyr v4.4.1 + modules via `west update`, both venvs to the 4.3.0 pins (fprime-tools/gds 4.3.0, fpp 3.3.0, pytest 9.0.3), upstream baseline FLASH/RAM/dictionary recorded from a pristine a477893b build, `docs-sync` hook verified (dry run passes at this path; `SKIP=docs-sync` is the fallback).
10. Requirement-ID collisions: upstream's `MM0011` (command loss) lands as `MM0013`; upstream's `REQ-SM-008..011` land as `REQ-SM-009..012`; our `REQ-SM-008` is re-worded to the quiescence file only. All via `req.py`.
11. PrmDb 4.3.0 CRC header: no saved parameters exist; accepted and noted in the ledger. Stale ledger/`CLAUDE.md` lines get dated "superseded by sync" notes; three new traps (`MAX_PACKETIZER_CHANNELS`, `const&` handler args, `docs-sync` hook).
12. Model (`~/scalar`): the 42 `src: S3` lines citing files upstream changed are listed in `01-scope.md` §model for the orchestrator's re-read after F1.
