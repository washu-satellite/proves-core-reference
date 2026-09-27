# Stored data design: saving data that is not immediately downlinked (index)

> Status 2026-09-27: design authoritative for A8 (ROADMAP item 4). Written at cab7439; **re-baselined 2026-09-27 against
> `main` @ 6296ef32** (after the 2026-09 upstream sync to F´ 4.3.0 / fprime-zephyr b14101dd): 1 Hz slot 21, opcode baseline 387 of
> 512, `FW_COM_BUFFER_MAX_SIZE` 227, the 4 GB soldered SD NAND, the burst stream for A9, the `MAX_PACKETIZER_CHANNELS` raise, the
> rewritten `Os::Directory`, and the dictionary keys. The commit table below and the `:NN` line numbers in `01-current-state.md`
> are history as of cab7439. Budgets and ownership: `../../cycles/cycle-sequencing-E-A8-A9.md`; commit rows: `../../cycles/commit-plan-E-A8-A9.md`.
> Still owed by A8-0, not done here: the `req.py` rows (DataRecorder-1..9, criteria for DH-L2-03/04/05/08/12 and CDH-16).

Branch `feat/persisted-record` @ cab7439, written 2026-09-06. Paths are relative to the repo root; `P` = `PROVESFlightControllerReference`.
Read `CLAUDE.md` first. This is a design, not a cycle plan: it decides *how* the flight software keeps data on board until a
ground pass can take it, and maps that decision onto the CDR Storage Management rows (DH-L2-01..14, CDH-8/12/14/16/27).

## One-paragraph summary

Data that cannot go down right away falls into five tiers, and each tier gets its own mechanism. Critical state (mode, boot
count, auth sequence number, telemetry transmit state) is already handled by `Components/PersistedRecord` (CRC-32, temp +
rename). Bulk payload files already go through `Svc.FileDownlink`. The tiers with **no mechanism today** are (a) telemetry
history, (b) a retrievable fault/event log and (c) the burst records BurstCapture (A9) will produce around torque pulses. This
design adds one passive project component, `DataRecorder`, that taps the existing telemetry and event splitters (and, from A9,
accepts burst records on a third stream), keeps each stream in a static RAM ring, flushes to CRC-framed segment files on the
soldered 4 GB SD NAND at a controlled interval, enforces retention by age and capacity, and exposes the segments to the existing
file-downlink path. Configuration is commanded with validate-then-commit and persisted as a PersistedRecord. F´ Data Products and
`Svc.ComLogger` were considered and rejected for this board (see `02-design.md` §7).

## Did the recent commits change anything about this?

Yes, but only at the "critical state" tier; nothing in the last 25 commits stores telemetry or events for later.

| Commit | What it did for stored data |
|---|---|
| 69e4b75 `feat(PersistedRecord)` | Added the CRC-32 + atomic-replace record store and moved TelemetryGate's tx state onto it |
| fbf1fa0 / c991d62 (Cycle A) | ModeManager, Authenticate, StartupManager now persist their state as PersistedRecords |
| 7ff33f4 / 3e9484c (Cycle B) | Configurable collection intervals only. The plan explicitly left DH-L2-03/04/05/08 "not implemented" and recommended a follow-on `TelemetryStore` component (`cycles/cycle-b-plan.md:18`, `cycle-b-review.md:3`). This document is that follow-on |
| 3d8db9e | Inspection records for DH-L2-06/09/10: comQueue priority order and drop-newest policy are now documented as the current behaviour |
| cda9160 / 8f295c9 | Hardware procedures: HP-12 (power-cut persistence), HP-13 (RF-silence buffering); DH-L2-05/08/12 listed as "not implemented, cannot be exercised" (`hardware-procedures/README.md:73-77,140`) |
| 458d119 (Cycle D FaultManager) | Emits fault events through the normal event path; persists nothing. Its events will be captured by the event stream designed here |

The findings ledger still holds: "No on-board telemetry store: dictionary `records`=0, `containers`=0"
(`docs-site/dev-loop-findings.md:116`).

## Files

| File | Holds | Read when |
|---|---|---|
| `01-current-state.md` | What the flight software does with data today: storage medium, the three downlink queues, what is kept and what is lost, with file:line pointers | Before questioning any design choice |
| `02-design.md` | The `DataRecorder` component: tiers, streams, RAM ring, segment file format, flush and retention policy, commands, telemetry, failure behaviour, alternatives rejected | Before writing any code |
| `03-requirements-map.md` | Each DH-L2 / CDH row: what the design does for it, the verification method and level, and which phase closes it | When editing `cdh.md` with `req.py` or writing `verifies` strings |
| `04-plan.md` | Phased delivery (each phase is one dev-loop cycle with its own commits), memory and dictionary budget, open decisions | When scheduling the next cycle |
