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

Other files here: `upstream-sync-analysis.md` (input to F), `cycle-sequencing-E-A8-A9.md` (ownership map for A8/A9, still
authoritative), `commit-plan-E-A8-A9.md` (commit granularity for A8/A9; its E and F sections are history recorded above).
