# Cycle E plan — DriverBoardHandler and the driver-board wire protocol (index)

Repo `$R` = `/Users/jesse-cm/Documents/Documents - Jesse's Mac/scalar-softwarestack/proves-core-reference`, `main` @ cab7439. Paths below are relative to `$R/PROVESFlightControllerReference/` unless they start with `$R/`, `lib/` or `docs-site/`. Planned 2026-09-17 against the SCALAR model (`~/scalar`, not-done.md A3/A4, conflicts C-23/C-27, capture Q11-Q15).

**Governing rule for this cycle (Jesse, 2026-09-16/17):** reuse before rewrite; copy an existing *pattern* rather than widening an existing component to serve a second purpose; any modification must keep the modified thing doing one job; generalise on the second real use, not the first. Restructuring to fit SCALAR's needs is in scope when the plan names what it replaces and why. "Do not hurt" means *no parallel implementation and no unstated override of an existing design*, not "no behaviour change".

**What this cycle delivers:** the RP2350 side of the payload link — a component that owns the SCALAR driver board (STM32L031, S6) over uart1, the parameters and commands the team decided to expose, its telemetry packet, and a pure protocol codec that is the single spec both processors implement. It does **not** deliver STM32 firmware (A1) or burst capture (A9); it delivers the hooks they need.

| File | Read it when |
|---|---|
| `01-scope.md` | deciding which requirement rows this cycle claims and which it leaves |
| `02-protocol.md` | implementing either end of the link — **the wire spec**; the STM32 firmware owner reads only this |
| `03-design.md` | implementing the F' component, its state machine, parameters, commands, telemetry, events |
| `04-harm-table.md` | reviewing what existing behaviour could change (answer: the camera path and rate groups, and it does not) |
| `05-files.md` | coding — file-by-file steps in order, with `req.py` commands |
| `06-verification.md` | running the gate — exact `verify.sh`, target-compile and dictionary expectations |
| `07-followups.md` | non-goals, open risks, and the three decisions this cycle deliberately leaves to the team |
| `08-findings.md` | verified facts, file:line, for the ledger merge |

**Normative (tests derive from these only):** `01-scope.md`, `02-protocol.md`, the interface tables in `03-design.md` §3.3-3.4 and §3.5's transitions, `04-harm-table.md`, `06-verification.md`, `07-followups.md`. **Advisory (methods; the coder may deviate if every normative result holds):** `03-design.md` §3.1-3.2, §3.6-3.9, `05-files.md`. Constraints that look like methods are restated as results in 04: no new thread (RAM < 70 %, 50 Hz tick unchanged), unwired-then-wired (reverting E5 alone restores today's image), no lib edits (`git diff --stat lib/` empty).

Summary in 12 lines:
1. New `Zephyr.ZephyrUartDriver` instance `driverBoardUart` on **uart1** (already enabled in the devicetree and already passed into the topology as `state.peripheralUart2`, unused). No devicetree change.
2. New passive component `Components.DriverBoardHandler` wired **directly** to that driver with the same `ByteStreamDriver` pattern as `comDriver`. `PayloadCom` is **not** reused: it sends a camera-specific ACK string on every received buffer (`PayloadCom.cpp:75-79`), so it is a camera transport, not a generic one; widening it would give it a second reason to change.
3. New dedicated `Svc.BufferManager` instance `driverBoardBufferManager` (4 x 128 B) so the camera's 2 x 4 KB pool is untouched.
4. New F'-free codec `Components/DriverBoardProtocol/` (framing, CRC-16, message pack/unpack, byte-at-a-time parser with resync) — host-tested, and the executable form of `02-protocol.md`.
5. **Second use of `crc16Ccitt`**: extracted from `TcFrameCorrectorCodec` into `Components/Crc16/Crc16.hpp`; TcFrameCorrector now includes it. Its tests must stay green unmodified.
6. **Second use of `RunInterval`** (Cycle B) for the housekeeping request cadence.
7. Five parameters (pulse duration, duty, channel mask, link timeout, housekeeping interval), six commands (ARM, DISARM, PULSE, ABORT, PING, GET_STATUS), ~15 channels in one new packet `PayloadHousekeeping id 23`, ~10 events.
8. Safety by structure: boots DISARMED; a pulse is refused unless the link is up and the handler is armed; ARM is refused in SAFE_MODE (polled from `modeManager.getMode`, no ModeManager edit); link silence for `LINK_TIMEOUT_MS` drops to LINK_DOWN and DISARMED; the STM32 has its own 3 s heartbeat failsafe in the spec (C-27).
9. FaultManager is **not** wired this cycle: `FaultType` values double as bits of a U8 mask and all eight are taken. Link loss is an event + telemetry + local action now; the `faultOut` hook is a follow-up after the mask is widened to U16 (07).
10. `MAX_PACKETIZER_PACKETS` 22 → 24 (id 23 this cycle, id 24 reserved). `CMD_DISPATCHER_DISPATCH_TABLE_SIZE` 400 → 512 now, so A8/A9 do not each trip it. **Persistence is gated (2026-09-18):** every parameter in this cycle is designed RAM-only with flight-safe defaults. `PRMDB_NUM_DB_ENTRIES` is raised only after the bench check in 07 passes, or PROVES confirms PRM_SAVE_FILE works on the V5e. Nothing in the component changes either way; persistence is two ground commands.
11. Scheduling: `driverBoardUart.schedIn` on **rateGroup50Hz slot 1** (free) because the driver drains at most 64 B per tick (`SERIAL_BUFFER_SIZE`), and 10 Hz caps the link at 640 B/s, below the A9 stream. `driverBoardHandler.run` on **rateGroup1Hz slot 12** (free), deliberately **not** through TaskGate.
12. Board test `test/int/driver_board_test.py` written and deferred (Flatsat level: needs the board and STM32 firmware answering the spec).
