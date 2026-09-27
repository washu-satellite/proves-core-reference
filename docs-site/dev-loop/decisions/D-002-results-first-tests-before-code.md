# D-002 — Results-first plans; tests written before code by a separate agent (2026-09-17, extended 2026-09-19)

**Decision (Jesse).** Plans state observable results, never implementation methods; tests derive from the results. On
2026-09-19: the tests for a cycle row are written **before any implementation exists**, by an agent that sees only the plan's
normative sections and the review; their SHA-256 hashes are pinned in the cycle review; the coder receives them read-only and
may not edit, skip or loosen them. A test that cannot be written from results alone is a plan defect.

**Why.** The earlier "test-first" wording let one coder write test and code in one sitting, so tests followed the method.
Cycle H (2026-09-19) was the first cycle run this way: tests commit 26041de4 predates code commit 6a1589b2.

**Consumers.** `.claude/skills/cdh-cycle/SKILL.md` Stage 3b; `.claude/agents/cdh-test-author.md`, `cdh-coder.md`;
`ROADMAP.md` standing rule 4.
