# Cycle index (history; the roadmap holds only what is next)

| Cycle | Dates | What | Plan | Review | Landed |
|---|---|---|---|---|---|
| A | 2026-09-04 | PersistedRecord adoption: ModeManager, Authenticate, StartupManager state | `cycle-a-plan.md` | `cycle-a-review.md` | fbf1fa0, c991d62 |
| B | 2026-09-05 | Per-source `COLLECTION_INTERVAL_S` (TM-L2-02); four storage rows Blocked → `design/stored-data/` | `cycle-b-plan.md` | `cycle-b-review.md` | 7ff33f4, 3e9484c |
| C | 2026-09-05 | TcFrameCorrector (single-bit uplink correction), TaskGate | `cycle-c-plan.md` | `cycle-c-review.md` | see review |
| D | 2026-09-05 | FaultManager (shadow mode), Faults packet | `cycle-d-plan/` | `cycle-d-review.md` | 458d119 |
| E | 2026-09-18/19 | Driver-board payload link: uart1, DriverBoardProtocol, DriverBoardHandler, PayloadHousekeeping packet, HP-15 | `cycle-e-plan/` | `cycle-e-review.md` | e08685af … d71fc8fd (E1–E8) |
| F | 2026-09-19 | Upstream sync (F´ 4.3.0, Zephyr 4.4.1) per [D-003](../decisions/D-003-upstream-sync-and-persistence-rule.md) | `cycle-f-plan/` | `cycle-f-review.md` | 59e87a89 merge; f878774c, 30080ec2, f26d9e1d, 135d5af1 |
| G | 2026-09-19 | A10: `parametersLoaded()` in five components | `cycle-g-plan.md` | `cycle-g-review.md` | 7658b28e |
| H | 2026-09-19 | Zephyr 4.4.2 sync row; capacity audit `scripts/check_capacity.py`; clean host build; first cycle under [D-002](../decisions/D-002-results-first-tests-before-code.md) | `cycle-h-plan/` | `cycle-h-review.md` | 6b22f72d, 26041de4 (tests), 6a1589b2 (code) |
| — | 2026-09-19 | PR #10 (E–H) merged to `main` | — | — | 42ea3d9c |
| I | 2026-09-20 | Documentation lint `scripts/check_docs.py` (DOCS-1..7) and generated `STATUS.md`; plan and review landed, implementation pending | `cycle-i-plan/` | `cycle-i-review.md` | c873d9a5 (plan), e1eced14 (review) |
| J | 2026-09-26/27 | Defects C-08 (each DRV2605 face manager on its own mux channel) and C-11 (`flash0` declared 16 MiB); hardware-consistency checker `scripts/check_hardware_consistency.py` (HWC-1, HWC-2) | `cycle-j-plan/` | `cycle-j-review.md` | 9459020c, 6296ef32 (fixes); 57aae379 (tooling) |
| K | 2026-09-27 | Bench startup sequence `sequences/bench_startup.seq` generated from `startup.seq` by `scripts/make_bench_sequence.py` (`make bench-sequence`), selectable per board with `STARTUP_SEQUENCE_FILE`; HP README §Bench preconditions | `cycle-k-plan.md` | `cycle-k-review.md` | 7228ba85; 57aae379 (tests, BENCH-1..2) |
| M | 2026-09-27 | A8 DataRecorder: codec + ring, passive component, activation (slot 21, splitter tap 2, `FileSystem` channels, `MAX_PACKETIZER_CHANNELS` 288), reader + pass sequence, board test + HP-12/13; run in the `feat/data-recorder` worktree; standing rule 3 waived by Jesse | `cycle-m-plan/` | `cycle-m-review.md` | d38f683e, 8c6889cb, 14d516cc, 59a7f7c9, 89f50ade, 5b956d6e, 9dbbb5d7, 8198693e |

Other files here: `upstream-sync-analysis.md` (input to F), `cycle-sequencing-E-A8-A9.md` (ownership map for A8/A9, still
authoritative), `commit-plan-E-A8-A9.md` (commit granularity for A8/A9; its E and F sections are history recorded above).
