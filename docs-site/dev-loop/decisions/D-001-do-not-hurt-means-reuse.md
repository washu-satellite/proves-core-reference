# D-001 — "Do not hurt the current system" means reuse, not freeze (2026-09-16, refined 2026-09-17)

**Decision (Jesse).** In agent briefs and loop rules, "do not hurt the current system" means two things: do not write new code
where code already exists (reuse, extend, adopt), and do not override a previous design decision without stating why. It does
**not** mean default flight behaviour must be unchanged or that only pass-through interposition is allowed. Strategic
restructuring to fit SCALAR is allowed. Reuse with modification is fine when the modification keeps modularity and does not
make the reused thing do too many things; prefer copying an existing *pattern* over widening a component for a second purpose.
Generalise on the second real use, not the first.

**Why.** Cycles A–D (2026-09-04/05) read the rule as "defaults unchanged, no new subsystems" and excluded every Tier 1
requirement (event log, telemetry store, CONOPS modes, ADCS) by design.

**Consumers.** `.claude/agents/cdh-planner.md` rule 1; `.claude/skills/cdh-cycle/SKILL.md`; `ROADMAP.md` standing rules.
