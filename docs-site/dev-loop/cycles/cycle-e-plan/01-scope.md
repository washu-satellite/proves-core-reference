# 01 — Scope per requirement

Criteria from `fprime-venv/bin/python3 scripts/req.py show <ID>` at cab7439. Unit tests claim only Unit-level IDs; system rows are claimed only by the deferred board test.

| ID | Text / criterion clause | Decision | Evidence this cycle |
|---|---|---|---|
| TM-L2-01 | collect telemetry from all registered sources — "payload [not integrated]" | **Partial → advance**: the payload becomes a registered source with its own packet | Board (deferred): `driver_board_test.py::test_03_hk_channels_update` — with the STM32 answering, `driverBoardHandler.CoilCurrent0..2` update at least once per 45 s at level 3 |
| CDH-27 | may downlink controls algorithm telemetry at least every 2 days | **Advance** (the channel set exists; cadence is ops) | Board (deferred): same test; the packet is in group 3 |
| ADCS-L2-06 | may support downlink of control telemetry | **Advance** | Board (deferred): same |
| CDH-17, CDH-20 | upload / activate payload algorithms | **Not this cycle.** Needs an STM32 reflash path (SWD bit-bang or BOOT0 on the next board rev). Leave "[payload not interfaced]" but change reason text to "link exists (Cycle E); reflash path absent" | none |
| CH-L2-15 style "commands are validated" rows | n/a | Handler commands validate arguments and state; claimed via component IDs only | Unit |
| PayloadCom-1..4 | camera transport | **Untouched.** Not claimed, not modified | none |
| New: DriverBoardHandler-1..9 | see below | **Implement** | Unit (host) with recorder stub + fake board |
| New: DriverBoardProtocol-1..6 | see below | **Implement** | Unit (host) |

## New component requirements (add with `req.py add --group DriverBoardHandler` / `--group DriverBoardProtocol`; Unit level unless stated)

| ID | Description | Pass criterion |
|---|---|---|
| DriverBoardProtocol-1 | Encode produces `SYNC TYPE SEQ LEN PAYLOAD CRC` with CRC-16/CCITT-FALSE over TYPE..PAYLOAD | Round-trip of every message type is byte-identical; CRC equals `Crc16::ccitt` of the covered bytes |
| DriverBoardProtocol-2 | Parser accepts one valid frame per SYNC..CRC sequence | Feeding a valid frame byte-wise makes `feed()` return true exactly once, on the last CRC byte, and `frame()` then holds the same type, seq, len and payload |
| DriverBoardProtocol-3 | Parser rejects and resynchronises | A corrupt CRC, a LEN > 32, or a truncated frame yields no frame and one counted rejection; the next valid frame after arbitrary garbage is still delivered |
| DriverBoardProtocol-4 | Fixed memory | Parser holds at most one 39-byte frame; no heap; `sizeof(Parser) <= 64` |
| DriverBoardProtocol-5 | Wire units are integers | HK and SAMPLE payloads carry mA, 0.1 °C, percent and ms as integers; host-side conversion to F32 is exact for the ranges in 02 |
| DriverBoardProtocol-6 | Sequence continuity is observable | Two accepted frames with seq 5 then 7 leave `stats().accepted == 2` and `stats().seqGaps == 1`; seq 7 then 8 leaves `seqGaps` unchanged |
| DriverBoardHandler-1 | Boots DISARMED with LINK_DOWN; first frame after `uartReady` is DISARM; then exactly one host frame per 1 Hz tick (HK_REQUEST when `RunInterval::due(HK_INTERVAL_S)`, else HEARTBEAT) | After `uartReady` and 3 ticks with default HK_INTERVAL_S = 1: frames on `uartSend` are DISARM, HK_REQUEST, HK_REQUEST, HK_REQUEST; with HK_INTERVAL_S = 3: DISARM, HK_REQUEST, HEARTBEAT, HEARTBEAT; state DISARMED, no events |
| DriverBoardHandler-2 | Link comes up on the first valid board frame and drops after `LINK_TIMEOUT_MS` of silence | PONG at tick 1 → LinkUp event, LinkState UP; no frames for ceil(timeout/1000)+1 ticks → LinkLost once, LinkState DOWN; a second silent tick emits nothing more |
| DriverBoardHandler-3 | ARM requires LINK_UP and mode != SAFE_MODE, and completes only on board ACK | ARM with link down → CommandRefused(LINK_DOWN), response EXECUTION_ERROR; with link up and `getMode` = SAFE_MODE → CommandRefused(SAFE_MODE); with link up in NORMAL → ARM frame sent and command response OK (the response means "sent", not "armed"); on ACK(ARM, OK) → Armed event, DriverState ARMED; on ACK(ARM, status != 0) → CommandRefused(BOARD_REFUSED), state stays DISARMED |
| DriverBoardHandler-4 | PULSE uses the current parameters and requires ARMED | PULSE while DISARMED → CommandRefused(NOT_ARMED); while ARMED → one PULSE frame whose fields equal PULSE_DURATION_MS / PULSE_DUTY_PCT / PULSE_CHANNEL_MASK, PulseStarted event |
| DriverBoardHandler-5 | Link loss disarms | ARMED then silence past timeout → LinkLost, Disarmed(LINK_LOST), DriverState DISARMED, and the next PULSE is refused |
| DriverBoardHandler-6 | SAFE_MODE disarms | ARMED, `getMode` returns SAFE_MODE on the next tick → DISARM frame sent, Disarmed(SAFE_MODE) |
| DriverBoardHandler-7 | Housekeeping frames update telemetry | An HK frame with current {1500, -200, 0} mA, temp {251, 300}, duty {50, -50, 0}, state, flags → CoilCurrent0 = 1.5 F32, CoilTemperature0 = 25.1, PwmDuty1 = -50, DriverState, FaultFlags written once each |
| DriverBoardHandler-8 | Parameter validation falls back to default | PULSE_DURATION_MS 0 or 6000, PULSE_DUTY_PCT 101, LINK_TIMEOUT_MS 50, HK_INTERVAL_S 0 or 61, or INVALID → default value in effect, one ParameterRejected event each (throttle 5) |
| DriverBoardHandler-9 | Every received buffer is returned to the driver | For N `uartRecv` calls, N `uartRecvReturn` calls with the same buffer objects, regardless of content |
| DriverBoardHandler-10 (Board, Flatsat, deferred) | End-to-end with the STM32 answering the spec | PING → PongReceived(version) within 2 s; ARM → Armed; PULSE 500 ms → PulseStarted then HK shows non-zero CoilCurrent on the masked channels within 2 s; DISARM → Disarmed; power the board off → LinkLost within LINK_TIMEOUT_MS + 1 s |

Claim placement (review amendment 7): DriverBoardHandler-2, -5 and -6 are claimed by `test_DriverBoardHandler_Component.cpp` (their observables are events and frames), not by the pure link test; the link test claims nothing and exists for coverage of transitions.

Rows deliberately **not** created: anything about burst/stream (A9), attitude (A2), algorithm upload (CDH-17/20).
