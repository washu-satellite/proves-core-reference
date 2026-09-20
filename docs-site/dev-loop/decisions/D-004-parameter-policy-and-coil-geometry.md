# D-004 — Four bins for every configurable value; coil geometry is compile-time (2026-09-17, coil decision 2026-09-19)

**Decision (Jesse).** Every value is exactly one of: a **parameter** (a number a CONOPS decision would move, with an existing
consumer), a **command argument** (one-shot), a **file** (tables and behaviours, uplinkable), or a **compile-time constant**
(anything the board or F´'s static allocation fixes, and hardware constants never set from orbit). Rate-group rates and
membership, buffer and queue sizes, pins, packet layouts, the sequence-number window and the capacity constants are compile-time
by decision. On 2026-09-19: the ~29 coil-geometry values on DetumbleManager become compile-time constants; the five B-dot tuning
values stay parameters. Burst rate and window are parameters (A9); only their defaults are owed.

**Why.** The parameter database saves 25 entries against 106 parameters; each parameter costs two opcodes; a CONOPS rewrite
should be uploads plus parameter saves, not a reflash.

**Home of the full table:** `design/parameter-policy.md` (kept current by every cycle that adds or re-bins a value).
**Consumers.** `ROADMAP.md` rule 7 and the owed-decisions table; A9 plan.
