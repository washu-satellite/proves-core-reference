# D-003 — Upstream sync choices and the persistence rule (2026-09-19)

**Decisions (Jesse), for merging upstream PROVES (a477893b, F´ 4.3.0, Zephyr 4.4.x) into the fork:**
1. Authentication: take upstream's `TcSecurityDeframer` + `ProvesRouter`; port the fork's two hooks (PersistedRecord-backed
   sequence-number store; FaultManager's command-loss producer on ModeManager's command-loss path).
2. Persistence follows consequence, not origin: safety/regulatory value with no plausibility test → atomic write + PersistedRecord
   checksum (quiescence timer, mode state, transmit state, sequence number); telemetry-only value with a plausibility test →
   upstream's atomic write + believability check (boot count); bulk records → per-record CRC, drop bad; RAM → nothing. The
   normative text lives in `Components/PersistedRecord/docs/sdd.md`.
3. Merge commit, not rebase; take upstream for everything the fork never touched; discard the two stray `lib/fprime` edits.
4. Bit-flip (SEU) protection for RAM values is a separate later item (ROADMAP item 8).
5. Sync before you build: every cycle starts with the upstream check (ROADMAP rule 1).

**Why.** The fork diverged since June; upstream deleted the two auth components Cycles A/D modified, fixed boot-count torn
writes independently, and restructured the packetizer. Analysis: `cycles/upstream-sync-analysis.md`; plan: `cycles/cycle-f-plan/`.

**Consumers.** `ROADMAP.md` rules 1 and 6; `Components/PersistedRecord/docs/sdd.md`; `CLAUDE.md` Traps.
