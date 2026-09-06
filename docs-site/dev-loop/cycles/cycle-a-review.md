# Cycle A review decisions (orchestrator, 2026-09-05)

Plan: `cycle-a-plan.md`. Verdict: approved as designed.

Accepted deliberately:
- Corrupt `/mode_state.bin` boots SAFE_MODE / SYSTEM_FAULT with one `StatePersistenceFailure("load-corrupt")` (MM0012). Consequence: the first boot of this image over a legacy 12-byte file lands in SAFE once; with `loadSwitchTurnOn` unwired (issue #7) faces stay OFF after `EXIT_SAFE_MODE`. Ops note required in the ModeManager sdd; recommend deleting the three legacy files before the upgrade reboot.
- `GET_BOOT_COUNT` no longer rewrites the boot-count file (flash wear only).
- Authenticate first boot no longer writes 0; new path `/sequence_number.bin`; the two-instance shared-file exposure stays as today but is now CRC-detected instead of silently adopted.
- OPEN_ERROR/READ_ERROR keep today's behaviour (not treated as missing), unlike TelemetryGate; justified because `exists()` already separates absent from unopenable.

Corrections made during review: ledger struct size 12 bytes not 7; issue number in the coder brief (#7, not #6).
Deferred to hardware: DH-L2-11 power-cut, issue #1 on-board first boot, MM0007 warm reset.
