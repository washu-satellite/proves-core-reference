# Decision records (append-only)

One file per decision, dated, **never edited after the day it is written**. A change of mind is a new record that names the
one it supersedes; the old record gets a single `Superseded by D-nnn` line at the top and nothing else. Every other document
that depends on a decision **links to the record and does not paraphrase it** — a paraphrase is what goes stale.

Ownership of facts (each fact has exactly one home; other files link):

| Fact | Home |
|---|---|
| Why something was decided | `decisions/D-nnn-*.md` |
| What order work happens in, and which decisions are still owed | `ROADMAP.md` |
| What each cycle did, with commit hashes and gate evidence | `cycles/README.md` index → the cycle's review |
| Verified facts about the code (file:line) | `../dev-loop-findings.md` (append-only; a wrong line gets a `superseded` note) |
| Requirements, pass criteria, status | component `docs/sdd.md` tables and `../requirements/*.md`, via `scripts/req.py` only; matrix generated |
| Table-size headroom | `scripts/check_capacity.py` output (never typed into a document) |
| What is a parameter, a file, a constant | `design/parameter-policy.md` |
| Traps and commands for this checkout | `../../CLAUDE.md` |

| ID | Date | Decision |
|---|---|---|
| [D-001](D-001-do-not-hurt-means-reuse.md) | 2026-09-16 | "Do not hurt the current system" means reuse and justify overrides, not freeze behaviour |
| [D-002](D-002-results-first-tests-before-code.md) | 2026-09-17 / 19 | Plans state results; tests are written before code by a separate agent from results only |
| [D-003](D-003-upstream-sync-and-persistence-rule.md) | 2026-09-19 | Sync with upstream: auth from upstream plus two hooks; consequence-based persistence rule; merge not rebase |
| [D-004](D-004-parameter-policy-and-coil-geometry.md) | 2026-09-17 / 19 | Four bins for every value; coil geometry becomes compile-time |
| [D-005](D-005-activity-axis-not-new-modes.md) | 2026-09-19 | No new `SystemMode` values; activities on a separate RAM-only axis driven by sequence files |
