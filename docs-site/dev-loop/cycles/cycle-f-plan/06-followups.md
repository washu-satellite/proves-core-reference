# 06 — Follow-ups, open risks, and questions for the owner

## 1. A8 RAM guard, now that #467 applies

`commit-plan-E-A8-A9.md` A8-3 gates on "RAM ≤ 70 % (else A8 tlm ring → 16 slots)". After F1 the baseline moves twice: #467 frees 24 KiB of mbedTLS heap (arena the BufferManagers draw from at setup), and the 4.3.0 packetizer at 256 channels / 24 packets costs ≈ 27.6 KiB static plus tree nodes. Decision for the orchestrator after F1's measurement: (a) if F1 RAM ≤ 66 %, A8 keeps its planned ring sizes and the 70 % gate; (b) if 66 % < RAM ≤ 70 %, A8-3's constants row pre-shrinks the telemetry ring to 16 slots; (c) if RAM > 70 %, F1 stops (harm table H11) and the owner decides. Also: A8's +14 channels and A9's `BurstStatus` packet exceed the 13-channel headroom of 256 — A8's constants row raises `MAX_PACKETIZER_CHANNELS` (each channel ≈ 4 + 24 × 4 + 8 B in the entry table + one tree node, about 130 B) rather than this cycle over-provisioning; the DataRecorder design (`design/stored-data/02-design.md`, `04-plan.md`) must also be re-read against the rewritten Zephyr `Os::Directory` (fprime-zephyr b14101dd) before A8-2.

## 2. Bit-flip protection (owner, future item)

The persistence rule (F5 §4) covers torn writes and corruption at rest by CRC, not single-event upsets in RAM. Candidate follow-up: periodic re-validation of `m_state` against the record in ModeManager/TelemetryGate, and/or ECC-style redundancy for the mode byte. Not in this cycle; recorded so it is not lost.

## 3. CH-L2-05 has no test after F1 (owner question)

Decision 7 retires `authentication_test.py`. Its observable survives upstream: a stale sequence number produces `SequenceNumberInvalid` (`TcSecurityDeframer.fpp:52`) and `provesRouter.RejectedPackets` stays flat only for non-bypass opcodes. Proposal: re-target the test (channel `ComCcsds*.tcSecurityDeframer.CurrentSequenceNumber`, event `SequenceNumberInvalid`, the same GDS plugin file trick, `Framing/src/authenticate_plugin.py` now upstream's) instead of deleting it, so CH-L2-05 stays claimed and deferred. If the owner prefers retirement, CH-L2-05's status becomes "unverified: test retired at sync" in the matrix. The plan defaults to **retire in F1** (as decided) and lists the re-target as a follow-up row F6 if approved.

## 4. Requirement-ID renumbering (owner confirmation)

`01-scope.md` §2 renumbers upstream's `MM0011` → `MM0013` and `REQ-SM-008..011` → `REQ-SM-009..012` because our IDs are already claimed. Consequence: our tables and upstream's disagree on those IDs for good; the sync note in each sdd Change Log records the mapping. Alternative (not recommended): renumber ours, which would touch 8 + 11 test claims and the matrix history.

## 5. Two transmit inhibits, unwired (analysis §7.7)

TelemetryGate's persisted tx state and upstream's `enableTransmit/disableTransmit` on the LoRa driver (#443) are independent; with `DEFAULT_STARTUP_VALUE 0` the countdown never fires, so nothing changes at flight. A later row may wire `startupManager.enableTransmit` through the gate or drop one mechanism; needs the owner's view on which is the authority.

## 6. RtcManager `TIMEBASE` (#455) and `throttle_amateurs.seq` (#453)

Taken as theirs; no fork overlap. `TB_SC_TIME` default changes the time base of every timestamp compared with the fork's previous builds (dc3e0aff already did that on the upstream side). The int tests upstream adjusted (`rtc_test.py`) come with it. Check that `tools/` YAMCS fixes (`apply-yamcs-*.py`) are not needed by our `yamcs/` config — not verified here.

## 7. Environment residue

After F0 the build copy cannot build d4fda377 any more (Zephyr 4.4.1 tree, 4.3.0 venv). If F1 is reverted for a long time, the old `west.yml` must be re-applied in the copy. Also `~/zephyr-sdk-1.0.1` vs `0.17.4`: F0 records which one `SDK_VERSION` selects.

## 8. Not verified by this plan (state, do not assume)

- Whether `pytest 9.0.3` collects our int tests without new deprecation errors (`filterwarnings` in `pytest.ini` may need an entry) — F0's collect gate tells.
- Whether upstream's `conftest.py` hardware-skip logic interacts with our `flatsat` marker (`driver_board_test.py`) — collect gate tells; semantics unchanged (skip markers only).
- The exact F1 command/param/event counts — recorded, then explained against the F0 baseline (`05-verification.md` §6).
- Whether `make docs-sync` succeeds for real at this path (dry run does) — F1 gate tells; fallback defined in F0 §6.

## 9. Found during implementation (F1 59e87a89, F2 30080ec2, F3 f26d9e1d; recorded by F5)

- §1 resolved: post-F3 RAM is 331376 B (62.23 %), case (a): A8 keeps its 32-slot rings and the 70 % guard. The channel limit stays open (244 of 256; A8 raises `MAX_PACKETIZER_CHANNELS`).
- §8 resolved: pytest 9.0.3 collects the int tests; `slow` marker added to `pytest.ini`; `make docs-sync` works at this path (the hook reports Failed only on the run that rewrites a copy; the rerun is clean) — no `SKIP` fallback was needed.
- F1: at F´ 4.3.0 a `register_fprime_library` with no `DEPENDS` gets no project include path (`DriverBoardProtocol` needed `DEPENDS Fw_Types`); fpp 3.3.0 renders a `dictionary constant` as `ComCfg::SpacecraftId` with no `FppConstant_` wrapper; `test_ModeManager_VoltageDebounce.cpp` had 3 more `init(0)` sites beyond amendment 3's 12; the 4.3.0 dictionary keys the packet set as `telemetryPacketSets[0].members` / `omitted`; the first post-sync boot logs one `BootCountCorrupted` (old `SBC1` record read as a huge count) then count 1 (amendment 4); a 4.1.x `/prmDb.dat` fails the 4.3.0 CRC header so saved parameters revert once (none are saved today).
- `AuthDefaultKey.h` regeneration: the gitignored `Components/TcSecurityDeframer/AuthDefaultKey.h` is required by the target build and read by the GDS plugin (`Framing/src/authenticate_plugin.py:35`); `scripts/generate_auth_key_header.py` makes it; the old key under `Components/Authenticate/` went with that directory, so boards on the old image keep the old key until reflashed. Owner item: decide where the flight key lives and how the ground copy is kept in sync.
- F2: `SequenceNumberReadFailed` (id 14) and `SequenceNumberWriteFailed` (id 8) remain declared in `TcSecurityDeframer.fpp` for dictionary stability but are no longer emitted; both deframer instances share one record path and one `.tmp` (pre-existing, cycle-f-review amendment 12).
- F3: action-order difference under CLAIMED — ModeManager's own path runs sequence → `enterSafeMode(COMMAND_LOSS)` → `stopWatchdog`; FaultManager's CLAIMED path runs `stopWatchdog` → `forceSafeMode(COMMAND_LOSS)` (async hop on ModeManager). Shipped disposition is OBSERVED, so no flight change; a CLAIMED rollout must accept or align the order.
- Requirement text fixed in F5: `FaultManager-4` criterion now says `forceSafeMode(COMMAND_LOSS)` (amendment 8); `FD-L2-09` names `provesRouter.RejectedPackets`.
- SCALAR model: per-packet channel counts for the 23 packets (and the `members`/`omitted` split) are pending the model's Phase 3 re-read (`01-scope.md` §4), outside this repo.
