# Commit plan for E → A8 → A9: one module per commit, each independently revertible

Rules that make rollback clean:
1. **Every commit builds and passes the host gate on its own** (`VERIFY_ENV=host scripts/verify.sh`); commits that touch `.fpp`,
   topology or `project/config` also pass the target compile and the dictionary check before they land.
2. **Code before wiring.** A new component lands *unwired* (registered in CMake, compiled, host-tested) and is activated by a
   separate topology commit. Reverting the topology commit removes it from the image without touching its code.
3. **Dependencies point backward only.** Revert in reverse order inside a cycle. Nothing later is needed by anything earlier.
4. **Constants in their own commit,** first in the cycle that consumes them, so they are the last thing reverted.
5. **One branch per cycle, merged in order, never squashed,** so the granularity below survives into `main`.
6. After a cycle's activation commit passes its bench procedure, **tag** it (`e-bench-ok`, `a8-bench-ok`, `a9-bench-ok`).
7. The SCALAR model repo gets one commit per cycle stage that changed a modelled fact (`model(payload): ...`), referencing the
   flight-software commit hash.

Commit message form: `type(Scope): summary` per CLAUDE.md. `feat` = production code, `test` = tests/stubs, `build` = config or
CMake, `docs(requirements)` = sdd tables via `req.py` + matrix regen, `docs(dev-loop)` = plans/reviews/ledger, `ops` = sequences.

---

## Cycle E — DriverBoardHandler (branch `feat/driver-board`)

| # | Commit | Contents | Gate | Revert consequence |
|---|---|---|---|---|
| E1 | `build(config): raise packetizer to 24 packets and dispatch table to 512` | `TlmPacketizerCfg.hpp:19`, `CommandDispatcherImplCfg.hpp:14` | target compile; RAM delta recorded | none by itself; **must be the last reverted** (E5's packet id 23 needs it) |
| E2 | `feat(Crc16): extract CRC-16/CCITT from TcFrameCorrector` | `Components/Crc16/Crc16.hpp`; `TcFrameCorrectorCodec.cpp` calls it; forwarder kept | `test_TcFrameCorrector_Codec` unmodified + green; `test_Crc16` | standalone; E3 depends on it |
| E3 | `feat(DriverBoardProtocol): wire codec and message pack/unpack` | `Components/DriverBoardProtocol/*`, sdd with the spec, `test_DriverBoardProtocol_Codec.cpp`, `req.py add` DriverBoardProtocol-1..6 | host gate | standalone; E4 depends on it |
| E4 | `feat(DriverBoardHandler): component, link state machine, host tests (unwired)` | `Components/DriverBoardHandler/*` incl. `.fpp`, `DriverBoardLink.*`, sdd; stub `DriverBoardHandlerComponentAc.hpp`, `Fw/Buffer` stub, `DriverBoardFake.hpp`; `test_DriverBoardLink.cpp`, `test_DriverBoardHandler_Component.cpp`; `req.py add` DriverBoardHandler-1..9 | host gate; target compile (fpp) | standalone; not in the image yet |
| E5 | `feat(topology): payload link on uart1 — driver, buffer pool, handler, PayloadHousekeeping packet` | `instances.fpp` (+3), `topology.fpp` (connections, slots 50 Hz[1], 10 Hz[5], 1 Hz[12], `getMode`), `ReferenceDeploymentTopology.cpp` (`driverBoardUart.configure`), `ReferenceDeploymentPackets.fppi` (id 23) | target compile; dictionary +6 cmds +10 prm cmds +5 prm +22 ch +12 ev +1 pkt; `git diff --stat lib/` empty | **kill switch**: image no longer talks to uart1; code stays |
| E6 | `test(int): driver_board_test.py and HP-15 (deferred, flatsat)` | `test/int/driver_board_test.py`, `pytest.ini` `flatsat` marker, `hardware-procedures/HP-15-driver-board-link.md`, `req.py add` DriverBoardHandler-10 | int collect + ruff | standalone |
| E7 | `docs(requirements): Cycle E matrix, PayloadCom scope note, reasons, ledger merge` | `req.py set` TM-L2-01/CDH-17/CDH-20 reasons; PayloadCom sdd sentence; `docs-site/components/*` copies; `mkdocs.yml`; matrix regen; ledger from `cycle-e-plan/08` | RTM regenerates without warnings | standalone |
| E8 | `ops(sequences): payload_on / payload_off` | `sequences/payload_on.seq`, `payload_off.seq` | seqgen compiles | standalone |

Model repo after E5: `model(payload): DriverBoardHandler landed at <hash>; packetExists true; integratedIntoFlightSoftware true`.

## A8 — DataRecorder (branch `feat/data-recorder`), after E is merged

| # | Commit | Contents | Gate | Revert consequence |
|---|---|---|---|---|
| A8-0 | `docs(dev-loop): stored-data design Phase 0 — slot 21, opcode baseline, 4 GB card, burst stream` | fixes to `design/stored-data/*` listed in `cycle-sequencing-E-A8-A9.md`; `req.py add` DataRecorder-1..9; criteria/reasons for DH-L2-03/04/05/08/12, CDH-16 | RTM regen | standalone |
| A8-1 | `feat(DataRecorder): SegmentCodec and PacketRing (F'-free) with host tests` | `Components/DataRecorder/SegmentCodec.*`, `PacketRing.hpp`, `test_DataRecorder_Codec.cpp` (byte layout, every single-byte corruption, truncation, drop-oldest, flush trigger) | host gate | standalone; A8-2 depends |
| A8-2 | `feat(DataRecorder): component with config PersistedRecord (unwired)` | `DataRecorder.fpp/.hpp/.cpp`, sdd; `Os::Directory` fake; recorder stub; `test_DataRecorder_Component.cpp` | host gate; target compile | standalone; not in the image |
| A8-3 | `feat(topology): DataRecorder tap on both splitters, 1 Hz slot 21, channels in FileSystem packet` | `instances.fpp`, `topology.fpp` (`comSplitterTelemetry.comOut[2]`, `comSplitterEvents.comOut[2]`, slot 21), packet set (+7 channels into `FileSystem`, no new packet) | target compile; dictionary +9 cmds +14 ch +7 ev; RAM ≤ 70 % | **kill switch**: recording stops; live downlink path identical to before |
| A8-4 | `tools(recorder): recorder_reader.py and per-pass downlink sequence` | `tools/recorder_reader.py`, `sequences/pass_recorder.seq` | ruff; reader round-trips a host-generated segment | standalone |
| A8-5 | `test(int): data_recorder_test.py; HP-12 segment power-cut loop; HP-13 extension` | int test, hardware procedures | int collect + ruff | standalone |
| A8-6 | `docs(requirements): A8 matrix, ledger, component page` | matrix regen, `docs-site/components/DataRecorder.md`, nav | RTM | standalone |
| A8-7 *(decision-gated, separate branch)* | `feat(DataRecorder): inline mode with DOWNLINK_GROUP_MASK` | Phase 4 of the design; only after open decisions 1-2 | — | independent of A8-1..6 |

Model repo after A8-3: `model(data): DataRecorder landed at <hash>; OnboardStorage.retentionTime set; not-done A8 closed`.

## A9 — BurstCapture (branch `feat/burst-capture`), after A8 is merged

| # | Commit | Contents | Gate | Revert consequence |
|---|---|---|---|---|
| A9-0 | `docs(requirements): BurstCapture rows; ADCS-L2-04/06 criteria` | `req.py add` BurstCapture-1..N; criteria with the 10 Hz / 60 s / 5 s pre-trigger defaults | RTM | standalone |
| A9-1 | `feat(DriverBoardHandler): STREAM_START/STOP commands, sample forwarding, TEST_INJECT_SAMPLE (build-flag)` | handler `.fpp/.cpp` append; `DriverBoardFake.hpp` stream mode; tests | host gate; target compile; E's tests unmodified | standalone; A9-4 depends |
| A9-2 | `feat(DataRecorder): third stream "burst" (/rec/burst/, 32 slots, flush 8 / 5 s)` | `DataRecorder.fpp` stream enum +1, config record +1 entry (version bump handled by BAD_VERSION → defaults), tests | host gate; A8's tests unmodified | standalone; A9-4 depends |
| A9-3 | `feat(BurstCapture): record codec (44 B), 5-record batching, trigger state machine, IMU join (unwired)` | `Components/BurstCapture/*`, `BurstRecordCodec.*` (F'-free), sdd, stub, tests (trigger modes, pre-trigger window, batching, timestamp alignment from TIME_SYNC) | host gate; target compile | standalone; not in the image |
| A9-4 | `feat(topology): BurstCapture — sampleOut, IMU ports, recorder stream 2, 50 Hz slot 2, 1 Hz slot 22, BurstStatus packet id 24` | `instances.fpp`, `topology.fpp`, packet set | target compile; dictionary; RAM ≤ 70 % (else A8 tlm ring → 16 slots in a `build(config)` commit *before* this one) | **kill switch**: no capture; handler still counts samples |
| A9-5 | `test(int): burst_capture_test.py (loopback inject, then flatsat); HP-16` | int test, procedure with the J18 9↔10 jumper variant | int collect + ruff | standalone |
| A9-6 | `docs(requirements): A9 matrix, ledger, component page; not-done A9 minimal path closed` | matrix regen | RTM | standalone |

Model repo after A9-4: `model(data): BurstCapture landed at <hash>; PayloadBurstRecord.captureExists true; sampleRate/windowDuration set`.

---

## Rollback recipes

- **A cycle misbehaves on the bench:** `git revert <activation commit>` (E5 / A8-3 / A9-4). The image drops the feature; all
  code, tests and docs stay for the fix. Re-apply with `git revert` of the revert once fixed.
- **A module is wrong in principle:** revert its commit and everything after it in that cycle, in reverse order. Because each
  cycle is one branch merged without squash, `git log --first-parent` shows the cycle boundaries and `git revert -m 1 <merge>`
  drops a whole cycle in one step if needed.
- **The constants commit (E1) is never reverted alone.** Reverting it while E5 is present makes the packetizer assert at init
  (23 packets > 22) and the dispatcher table overflow. Revert E5 first.
- **Never `--no-verify`, never rewrite history after a bench tag.** Fixes go on top as new commits so the tagged state remains
  reproducible.

## What the loop's `cdh-cycle` skill changes to support this

Stage 5 currently commits in three groups (`feat`, `test`, `docs`). For these cycles, Stage 5 commits **per table row above**,
running the gate before each. The planner brief names the row list; the coder reports per row; the orchestrator commits per
row. Nothing else in the skill changes.
