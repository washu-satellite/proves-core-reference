# Dev loop artifacts

Each cycle of the requirement → test → implement → verify → post-mortem loop leaves its artifacts here and each step is committed separately:

| Step | Artifact | Commit scope |
|---|---|---|
| Plan | `cycles/cycle-<x>-plan.md` (planner output, with a Findings section) | `docs(dev-loop)` |
| Review | `cycles/cycle-<x>-review.md` (orchestrator harm review + coder amendments) | `docs(dev-loop)` |
| Implement | production code | `feat(<Component>)` / `fix(...)` |
| Tests | host/int tests and stubs | `test(...)` |
| Verify | regenerated matrix, ledger merge, sdd Change Log | `docs(requirements)` / `docs(dev-loop)` |

Rules and traps live in `../../CLAUDE.md`; verified facts in `../dev-loop-findings.md`; the gate is `scripts/verify.sh`.
