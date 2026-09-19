# 03 — Design

## 3.1 Topology shape (pattern: `comDriver` ↔ `ComCcsdsUart.comStub`, `topology.fpp:242-251,264`)

```
rateGroup50Hz[1] ─► driverBoardUart.schedIn          (Zephyr.ZephyrUartDriver, dev = state.peripheralUart2, 115200)
driverBoardUart.allocate/deallocate ◄─► driverBoardBufferManager   (Svc.BufferManager, 4 x 128 B, manager id 2)
driverBoardUart.$recv ─► driverBoardHandler.uartRecv
driverBoardHandler.uartRecvReturn ─► driverBoardUart.recvReturnIn
driverBoardHandler.uartSend ─► driverBoardUart.$send
driverBoardUart.ready ─► driverBoardHandler.uartReady
rateGroup1Hz[12] ─► driverBoardHandler.run            (NOT via TaskGate: link supervision is not gateable)
driverBoardHandler.getMode ─► modeManager.getMode      (poll; no ModeManager edit — see 3.6)
rateGroup1Hz[?]  ─► driverBoardBufferManager.schedIn  (telemetry only; use slot 12 for the handler and add the manager to the 10 Hz group slot 5 if a 1 Hz slot is wanted later — decision: bufferManager.schedIn on rateGroup10Hz[5], which is free)
```
Why 50 Hz for the driver: `ZephyrUartDriver::schedIn_handler` drains at most `SERIAL_BUFFER_SIZE` = 64 B per tick (`lib/fprime-zephyr/.../ZephyrUartDriver.hpp:25`, `.cpp:92-100`). At 10 Hz that caps the link at 640 B/s; the A9 stream at 50 Hz x 13 B SAMPLE (+7 framing) = 1000 B/s. At 50 Hz the cap is 3200 B/s. The ring buffer is 1024 B (`.hpp:17`), one second of stream. `detumbleManager.run` is slot 0 on that group; a 64-byte drain after it is microseconds. Nothing is changed for the camera driver on 10 Hz slot 4.

Why a dedicated BufferManager: the camera pool is 2 x 4 KB (`instances.fpp:141-143`) and `CameraHandler` is active, so it can hold one buffer across ticks; sharing would make a camera image transfer starve the driver-board link, and the driver allocates on every tick even when the ring is empty (`.cpp:92-97`). Four 128-B buffers cost 512 B + bookkeeping and isolate the two paths completely. `bins[0]` of the new manager = {128, 4}. Allocator: `ComCcsds::Allocation::memAllocator`, as the camera pool does.

Why not `PayloadCom`: it emits `"<MOISES>\n"` on every received buffer (`Components/PayloadCom/PayloadCom.cpp:75-79`). That is camera protocol living in the transport layer. Reusing it means either sending that string to the STM32 or adding a per-instance "no ACK" switch, which gives PayloadCom a second reason to change. Copying the `comDriver` direct-wiring pattern instead reuses the `ByteStreamDriver` interface and touches no existing component. PayloadCom's sdd should say it is camera-specific (05, step 12).

## 3.2 Component `Components.DriverBoardHandler` (passive)

Passive and synchronous: `uartRecv` runs on the 50 Hz thread, `run` on the 1 Hz thread, commands on the command-dispatcher thread. **Locking rule (review amendment 2, normative):** `uartRecv` is `guarded`; `run`, `uartReady` and every command are `sync` (a `sync command` takes NO guard in F' — only `guarded command` does) and protect handler state with explicit `this->lock()` / `this->unLock()` around state mutation only. **No output port is ever invoked while the lock is held**: frames to send are built into a stack-local buffer under the lock, and `uartSend_out` is called after `unLock()`. Reason: `ZephyrUartDriver.schedIn` and `$send` are both `guarded`; the 50 Hz thread (priority 1) enters driver-lock → handler-lock via recv, and a 1 Hz `run` holding handler-lock → driver-lock via `$send` is an ABBA deadlock the first time a board answers. The host stub asserts zero output-port calls while locked (FaultManager `actionWhileLocked` pattern). No queue, no thread, no heap.

Members: `DriverBoardProtocol::Parser m_parser` (≤ 64 B), `DriverBoardLink m_link` (pure state machine, 3.5), `RunInterval m_hkInterval` (Cycle B helper), cached effective parameters, counters. TX frames are stack-local (≤ 40 B), never a shared member.

## 3.3 Interface (`DriverBoardHandler.fpp`)

Ports
```
guarded input port uartRecv: Drv.ByteStreamData        @ from driverBoardUart.$recv
output port uartRecvReturn: Fw.BufferSend               @ every received buffer goes back, always
output port uartSend: Drv.ByteStreamSend                @ synchronous; caller retains the buffer
sync input port uartReady: Drv.ByteStreamReady          @ driver ready; sets m_link.driverReady
guarded input port run: Svc.Sched                       @ 1 Hz: heartbeat/HK request, supervision, mode poll, telemetry
output port getMode: Components.GetSystemMode           @ -> modeManager.getMode
output port sampleOut: Components.PayloadSample         @ one call per SAMPLE frame; unconnected this cycle (A9 hook)
```
`Components.PayloadSample` is a new port type declared in `DriverBoardHandler.fpp`: `port PayloadSample(boardTimeMs: U32, currentMa: I16[3] as three args, dutyPct: I8[3] as three args)`. The handler invokes it from `uartRecv_handler` for every parsed SAMPLE frame **only if the port is connected** (`isConnected_sampleOut_OutputPort()`), so this cycle's build drops samples at zero cost and counts them in `SamplesReceived` (U32, added to the packet: +4 B → 52 B values, 67 B wire, 73 B in frame). The A9 burst-capture component connects to it; it is passive and runs on the same 50 Hz thread, appending to its RAM ring; the recorder (A8, active) drains that ring on its own thread. Keeping the port on the handler rather than parsing SAMPLE frames in A9 keeps one owner for the wire protocol.
plus time, cmd reg/recv/resp, event, text event, telemetry, param get/set.

Parameters (defaults = the team's 2026-09-17 decisions; validation = fallback-to-default in `parameterUpdated`, Cycle B pattern, `ComDelay.cpp:22-35`)
| Param | Type | Range | Default | Effect |
|---|---|---|---|---|
| PULSE_DURATION_MS | U16 | 50..5000 | 2000 | PULSE frame field |
| PULSE_DUTY_PCT | U8 | 0..100 | 50 | PULSE frame field |
| PULSE_CHANNEL_MASK | U8 | 0x01..0x07 | 0x07 | PULSE frame field |
| LINK_TIMEOUT_MS | U16 | 100..10000 | 1000 | host supervision (3.5) |
| HK_INTERVAL_S | U8 | 1..60 | 1 | HK_REQUEST cadence; HEARTBEAT fills the other ticks |

Commands (all `sync`; response codes: OK, EXECUTION_ERROR on refusal, VALIDATION_ERROR on bad argument)
| Command | Args | Precondition | Effect |
|---|---|---|---|
| ARM | — | LINK_UP and mode != SAFE_MODE | send ARM(0xA5); ARMED only on ACK(OK) |
| DISARM | — | — | send DISARM; state DISARMED immediately (do not wait) |
| PULSE | polarityMask: U8 (bits 0-2) | ARMED | send PULSE(params, polarityMask); PulseStarted |
| ABORT | — | — | send ABORT; state DISARMED; Disarmed(ABORT) |
| PING | — | driver ready | send PING; PongReceived on reply |
| GET_STATUS | — | — | event StatusReport(link, state, flags, version, uptime) |

Telemetry (types chosen for packet size; enums are `: U8`)
| Channel | Type | Source |
|---|---|---|
| CoilCurrent0, 1, 2 | F32 A | HK currentMa / 1000 |
| CoilTemperature0, 1 | F32 °C | HK tempDeciC / 10 |
| PwmDuty0, 1, 2 | I8 % | HK |
| DriverState | enum U8 {DISARMED, ARMED, PULSING, FAULT, UNKNOWN} | HK state, or UNKNOWN when LINK_DOWN |
| FaultFlags | U8 | HK / FAULT |
| LinkState | enum U8 {DOWN, UP} | supervision |
| BoardUptime | U32 ms | HK |
| FramesReceived | U32 | parser |
| FramesRejected | U16 | parser (CRC, LEN, truncated, unknown type) |
| SamplesReceived | U32 | SAMPLE frames parsed (forwarded on sampleOut when connected) |
| LinkTimeouts | U16 | supervision transitions UP→DOWN |
| FirmwareVersion | U16 | PONG |
| PulsesCommanded | U16 | PULSE frames sent |
| PulseDurationMs, PulseDutyPct, LinkTimeoutMs, HkIntervalS | U16, U8, U16, U8 | effective parameter values, written on change (Cycle B pattern) |

Events (throttles chosen so a missing board is quiet)
LinkUp(version: U16) · LinkLost(silentMs: U32) · FrameRejected(reason: U8) throttle 5 · Armed() · Disarmed(reason: enum {COMMAND, LINK_LOST, SAFE_MODE, BOARD_FAULT, ABORT}) · PulseStarted(durationMs, dutyPct, channelMask, polarityMask) · PulseRefused(reason) · CommandRefused(cmd: U8, reason: enum {LINK_DOWN, NOT_ARMED, SAFE_MODE, DRIVER_NOT_READY, BOARD_REFUSED}) · BoardFault(flags: U8, value: I16) · PongReceived(fwVersion, protoVersion) · StatusReport(...) · ParameterRejected(paramId: U32) throttle 5.

## 3.4 The payload housekeeping packet (compatible with the existing downlink stack)

How a packet travels here: `Svc.TlmPacketizer` (project override, `CdhCoreTlmConfig.fpp`) keeps the latest value of every channel and, on each `telemetryDelay` tick (every 30 s by default), emits each packet whose members changed (`PACKET_UPDATE_ON_CHANGE`, `TlmPacketizerCfg.hpp:40`) and whose **group ≤ the current send level**. The packet is one com buffer: `[2 B descriptor][2 B packet id][11 B Fw::Time][channel values back-to-back, no per-channel ids]`. The comQueue hands it to the CCSDS stack, which wraps it in a 6-byte Space Packet header and aggregates packets into a fixed **248-byte TM frame** (`ComCfg.fpp:15-18`, aggregation area 233 B). LoRa carries up to 252 B per radio packet (`LoRa.hpp:19`). Hard limit for one packet: `FW_COM_BUFFER_MAX_SIZE` = 233 B (`FpConstants.fpp:20`), so **≤ 218 B of channel values**.

Declaration (`ReferenceDeployment/Top/ReferenceDeploymentPackets.fppi`, after `Detumble id 15 group 3`):
```
packet PayloadHousekeeping id 23 group 3 {
  ReferenceDeployment.driverBoardHandler.LinkState
  ReferenceDeployment.driverBoardHandler.DriverState
  ReferenceDeployment.driverBoardHandler.FaultFlags
  ReferenceDeployment.driverBoardHandler.CoilCurrent0
  ReferenceDeployment.driverBoardHandler.CoilCurrent1
  ReferenceDeployment.driverBoardHandler.CoilCurrent2
  ReferenceDeployment.driverBoardHandler.CoilTemperature0
  ReferenceDeployment.driverBoardHandler.CoilTemperature1
  ReferenceDeployment.driverBoardHandler.PwmDuty0
  ReferenceDeployment.driverBoardHandler.PwmDuty1
  ReferenceDeployment.driverBoardHandler.PwmDuty2
  ReferenceDeployment.driverBoardHandler.BoardUptime
  ReferenceDeployment.driverBoardHandler.FirmwareVersion
  ReferenceDeployment.driverBoardHandler.PulsesCommanded
  ReferenceDeployment.driverBoardHandler.FramesReceived
  ReferenceDeployment.driverBoardHandler.FramesRejected
  ReferenceDeployment.driverBoardHandler.LinkTimeouts
  ReferenceDeployment.driverBoardHandler.SamplesReceived
  ReferenceDeployment.driverBoardHandler.PulseDurationMs
  ReferenceDeployment.driverBoardHandler.PulseDutyPct
  ReferenceDeployment.driverBoardHandler.LinkTimeoutMs
  ReferenceDeployment.driverBoardHandler.HkIntervalS
}
```
Size: values 1+1+1 + 12 + 8 + 3 + 4 + 2 + 2 + 4 + 2 + 2 + 4 + 2+1+2+1 = **52 B**; on the wire **67 B** (15 B header); inside a TM frame **73 B** (Space Packet header), so three of them fit one 248-B frame alongside other group-3 packets. Upper bound at the 30 s tick: 67 x 2880 = **193 KB/day** if it changed every tick; in practice the board is unpowered most of the time, LinkState/DriverState/counters are static, and on-change mode sends nothing. The SCALAR model's `PayloadHousekeepingPacket` (28 B / 43 B) is updated to this layout by this plan (05, step 14).

Group 3 because Thermal, LoadSwitches and Detumble live there: "level 3" is the housekeeping tier ops raise to during a pass; the Beacon (level 1) stays untouched. Ordering puts the three 1-byte state fields first so a truncated dump still reads state. No channel is left out of a packet (the `fpp-to-dict` trap, CLAUDE.md). `MAX_PACKETIZER_PACKETS` 22 → **24** (`TlmPacketizerCfg.hpp:19`): id 23 here, id 24 reserved for the A9 burst/stream packet so that cycle does not touch the constant again. The GDS and YAMCS dictionaries regenerate from the same fppi, so no ground change beyond rebuilding the dictionary.

## 3.5 Link state machine (pure module `DriverBoardLink`, host-tested)

```
                 valid board frame                     silence > LINK_TIMEOUT_MS
   LINK_DOWN ────────────────────────► LINK_UP ─────────────────────────────► LINK_DOWN
   (DISARMED forced)                                                           (DISARMED forced, LinkLost, LinkTimeouts++)

   DISARMED ──ARM cmd + ACK(OK)──► ARMED ──PULSE──► (board reports PULSING via HK) ──► ARMED
   any ──DISARM / ABORT / SAFE_MODE / LINK_DOWN / HK.state==FAULT──► DISARMED (Disarmed(reason))
```
Inputs per 1 Hz tick: `nowMs`, `lastValidFrameMs`, `mode`. Outputs: which host frame to send (HEARTBEAT or HK_REQUEST by `RunInterval::due(HK_INTERVAL_S)`), transitions, events. Silence is measured with `getTime()` on the 1 Hz tick and stamped on every valid frame in `uartRecv`, so the resolution is one tick; with the default 1000 ms timeout the effective detection latency is 1–2 s. Documented in the sdd; the STM32 failsafe (3 s) is the safety path.

## 3.6 Mode coupling without touching ModeManager

`ModeManager.modeChanged` is an output port array of width 1, wired to DetumbleManager (`topology.fpp:386`). Widening it is a two-line ModeManager change, but this cycle does not need it: `modeManager.getMode` is a sync input port with no fan-in limit, and ModeManager already **depowers the payload rails on safe-mode entry** (`loadSwitchTurnOff[6..7]`, `topology.fpp:506-507`), so the board is physically off before the handler's next tick. The handler polls `getMode` each tick and, if SAFE_MODE while ARMED, sends DISARM and emits Disarmed(SAFE_MODE) for the record. When the mode layer is built, switch to the `systemModeChanged` subscription and widen the array to 2 (07).

## 3.7 Buffer ownership

`uartRecv` parses every byte of the received buffer into `m_parser`, then **always** returns the buffer via `uartRecvReturn` before returning (DriverBoardHandler-9). The driver deallocates to the buffer manager. TX uses a stack-local 40-byte array in the calling handler, built under the lock and sent after unlock; `$send` is synchronous and the driver copies byte-by-byte with `uart_poll_out` (`.cpp:111`), so the array is free on return. One frame per tick per direction at most in this cycle, so no TX queue.

## 3.8 Codec `Components/DriverBoardProtocol/` (F'-free)

Files: `DriverBoardProtocol.hpp/.cpp` (framing + parser), `DriverBoardMessages.hpp` (POD structs and pack/unpack per message type), both including only `<cstdint>`, `<cstddef>` and `Components/Crc16/Crc16.hpp`.
API (no STL, no heap, no exceptions):
- `constexpr uint8_t SYNC0 = 0x5C, SYNC1 = 0xA1; constexpr size_t MAX_PAYLOAD = 32, MAX_FRAME = 39; constexpr uint8_t PROTOCOL_VERSION = 1;`
- `enum class Type : uint8_t { ... }` per 02; `enum class Reject : uint8_t { NONE, BAD_LEN, BAD_CRC, TRUNCATED, UNKNOWN_TYPE }`.
- `size_t encode(Type, uint8_t seq, const uint8_t* payload, uint8_t len, uint8_t* out, size_t outCap)` → bytes written or 0.
- `class Parser { bool feed(uint8_t byte); const Frame& frame() const; const Stats& stats() const; void reset(); }` where `Frame{type, seq, len, payload[32]}` and `Stats{accepted, rejected, lastReject, seqGaps}`. State machine: SYNC0 → SYNC1 → TYPE → SEQ → LEN → PAYLOAD → CRC_HI → CRC_LO; any impossible byte restarts at SYNC0 (and re-examines the byte as a possible SYNC0, so back-to-back frames and garbage-then-frame both work).
- `DriverBoardMessages.hpp`: `struct Hk { int16_t currentMa[3]; int16_t tempDeciC[2]; int8_t dutyPct[3]; uint8_t state, faultFlags; uint32_t uptimeMs, boardTickMs; }` with `bool unpackHk(const Frame&, Hk&)`; `packPulse(...)`, `packHeartbeat(...)`, `unpackAck`, `unpackPong`, `unpackFault`, `unpackSample` (present, unused by the handler this cycle).
Little-endian on the wire (both cores are little-endian; the code still packs byte-wise so the host build on any machine is correct). CRC big-endian to match `Crc16::ccitt`'s existing FECF convention.

## 3.9 `Components/Crc16/Crc16.hpp` — the second-use extraction

`TcFrameCorrectorCodec.cpp` implements CRC-16/CCITT-FALSE bitwise with no table (`TcFrameCorrectorCodec.hpp:67-75`). Move that function to a header-only `Crc16::ccitt(const uint8_t*, uint32_t len)` (keep the existing `uint32_t` length type), include it from `TcFrameCorrectorCodec.cpp`, delete only the CRC body; `stepSyndrome` and the corrector's constants stay in `TcFrameCorrectorCodec.cpp` because `correctSingleBit` uses them. `test_TcFrameCorrector_Codec.cpp` must pass **unmodified**; add `test_Crc16.cpp` with the CCITT-FALSE check value 0x29B1 for "123456789". The STM32 firmware will carry its own C copy of the same 10 lines; the spec names the polynomial so both match.
