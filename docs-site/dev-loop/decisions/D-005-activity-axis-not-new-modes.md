# D-005 — No new `SystemMode` values; activities on a separate axis (2026-09-19)

**Decision (Jesse).** `SystemMode` keeps only SAFE_MODE and NORMAL as PROVES built them; ModeManager is never extended with
CONOPS modes. CONOPS activities (NONE, CALIBRATION, EXPERIMENT; STANDBY dropped) live in a new, small, RAM-only
`ActivityManager` on its own axis. All switching (payload rail, PING, `detumbleManager.SET_MODE` stand-down, ARM, task-gate
bits) happens inside uplinkable sequence files `activity_<x>_enter|exit.seq`; no new port on TaskGate, DetumbleManager or
ModeManager. One coupling rule: SAFE_MODE forces NONE. B-dot vs STM32 arbitration is `SET_MODE` in the sequence, not a mode
overlay. **Staged as a brief for a separate implementer; the dev loop does not build it.**

**Why.** New modes inside the existing two-state machine (~600 lines, ~30 branch sites) would be as entangled as SAFE; the
separate axis carries no safety, persistence or reboot logic. An earlier proposal (a "mode layer" with per-mode masks) and a
relayed "decided against" were both replaced by this.

**Home of the design:** `design/activity-axis.md`. **Consumers.** `ROADMAP.md` item 6; `design/parameter-policy.md`; requirement
`MS-L2-01`; `hardware-procedures/HP-06-modes.md` when the activity exists.
