# 02 — Driver-board wire protocol (the spec both processors implement)

Link: UART, 8N1, **115200** baud (both flight UARTs already run at this rate; `Main.cpp:125-128`). Physical: RP2350 uart1 = GPIO4 TX1 / GPIO5 RX1 = **J18 pins 9 (TX1) / 10 (RX1)** on FC V5e (S8 netlist). Ground on J18 3/4/11/12. Board power: PAYLOAD_PWR (J18 1-2, AP22653, `payloadPowerLoadSwitch`) and PAYLOAD_BATT (J18 7-8, TPS2HB50, `payloadBatteryLoadSwitch`). The handler never switches power; sequences and ModeManager do.

Design constraints: the STM32L031K6 has 32 KB flash, 8 KB RAM, no FPU. So: fixed small frames, integer units on the wire, a byte-at-a-time parser with one frame of buffer, no escaping.

## Frame

```
offset  size  field
0       2     SYNC   = 0x5C 0xA1
2       1     TYPE   (table below; bit 7 set = board→host)
3       1     SEQ    per direction, wraps; receiver counts gaps, never rejects on SEQ
4       1     LEN    payload length, 0..32
5       LEN   PAYLOAD little-endian integers
5+LEN   2     CRC16  CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflect, xorout 0)
              over TYPE..PAYLOAD inclusive, big-endian on the wire (matches Crc16::ccitt / CCSDS FECF)
```
Max frame 39 bytes. Resync rule: a receiver in any state that sees a byte that cannot continue the current frame discards to the next 0x5C. A false SYNC inside a payload is caught by LEN > 32 or by CRC; both count as one rejection. Nothing is retransmitted at this layer; the host repeats a command if it gets no ACK within `LINK_TIMEOUT_MS`.

## Messages, host → board

| TYPE | Name | Payload | Board response |
|---|---|---|---|
| 0x01 | HEARTBEAT | U32 hostTickMs | none (resets the board's failsafe timer) |
| 0x02 | HK_REQUEST | — | HK |
| 0x03 | ARM | U8 magic = 0xA5 | ACK(0x03, status) |
| 0x04 | DISARM | — | ACK(0x04, OK) |
| 0x05 | PULSE | U16 durationMs (50..5000), U8 dutyPct (0..100), U8 channelMask (bits 0-2), U8 polarityMask (bits 0-2; 1 = reversed) | ACK(0x05, status) immediately; HK after completion |
| 0x06 | ABORT | — | ACK(0x06, OK); coils off within 1 ms |
| 0x07 | PING | — | PONG |
| 0x08 | TIME_SYNC | U32 hostSeconds, U32 hostMicroseconds | ACK; board records offset (A9) |
| 0x09 | STREAM_START | U8 rateHz (5/10/20/50) | ACK; SAMPLE frames follow (A9, not consumed this cycle) |
| 0x0A | STREAM_STOP | — | ACK |

## Messages, board → host

| TYPE | Name | Payload |
|---|---|---|
| 0x81 | ACK | U8 ackedType, U8 status (0 OK, 1 REFUSED_NOT_ARMED, 2 REFUSED_FAULT, 3 BAD_ARG, 4 BUSY) |
| 0x82 | HK | I16 currentMa[3], I16 tempDeciC[2], I8 dutyPct[3], U8 state (0 UNPOWERED-never sent, 1 DISARMED, 2 ARMED, 3 PULSING, 4 FAULT), U8 faultFlags, U32 uptimeMs, U32 boardTickMs — 22 bytes |
| 0x87 | PONG | U16 firmwareVersion, U8 protocolVersion (=1), U8 reserved |
| 0x88 | SAMPLE | U32 tMs (board clock), I16 currentMa[3], I8 dutyPct[3] — 13 bytes (A9) |
| 0x8F | FAULT | U8 faultFlags, I16 value |

`faultFlags` bits: 0 OVERCURRENT, 1 OVERTEMP, 2 HOST_TIMEOUT (board failsafe fired), 3 UNDERVOLTAGE, 4 SENSE_FAIL, 5-7 reserved.

## Timing and failsafe (C-27, Q11)

- Host sends HEARTBEAT or HK_REQUEST at least once per second (the 1 Hz tick always sends one of them).
- **Board failsafe:** if no valid host frame for **3000 ms**, coils off, state DISARMED, faultFlags HOST_TIMEOUT set, one FAULT frame. This constant lives in STM32 firmware, not in a host parameter, so it cannot be misconfigured from the ground.
- **Host supervision:** if no valid board frame for `LINK_TIMEOUT_MS` (default 1000), host state LINK_DOWN and DISARMED. The host does not need the board to confirm; the board's own failsafe will follow within 3 s.
- A PULSE longer than the heartbeat interval is legal: the board keeps the heartbeat timer during a pulse. A pulse is never extended by a late ACK.
- Two clocks: `boardTickMs` in HK/SAMPLE and `hostTickMs` in HEARTBEAT let either side estimate offset. Alignment for A9 is done with TIME_SYNC plus on-receipt stamping; this cycle only carries the fields.

## Streaming during an experiment (A9 uses this; this cycle only carries it)

- The board has 8 KB of RAM and cannot hold an experiment: a 60 s window at 50 Hz is ~3000 samples. It streams each SAMPLE as taken; the host stores.
- STREAM_START(rateHz) is accepted only when ARMED; the board sends SAMPLE at the requested rate until STREAM_STOP, DISARM, ABORT, a board fault, or its 3 s host-timeout failsafe. Stopping for any reason other than STREAM_STOP is reported with one FAULT or the next HK (state != PULSING/ARMED), never silently.
- During a stream every other rule still holds: the host still sends HEARTBEAT/HK_REQUEST at 1 Hz, the board still answers HK, and a PULSE mid-stream is an ordinary PULSE. SAMPLE frames are interleaved with HK/ACK frames on the same link; the parser treats them as independent frames.
- Budget: 50 Hz x (13 B payload + 7 B framing) = 1.0 KB/s on a 11.5 KB/s UART; the host driver drains 64 B per 20 ms tick = 3.2 KB/s, so the ceiling is ~160 samples/s with the current driver constant. Rates above 50 Hz are out of scope for version 1.
- Clocks: SAMPLE.tMs is the board clock. The host stamps each sample on receipt with its own time (one 20 ms tick of jitter) and uses the TIME_SYNC exchange (host sends its time; the board's next HK carries boardTickMs) to estimate the offset. Alignment to a few ms is the A9 acceptance criterion, not a version-1 protocol guarantee.
- If the link drops mid-stream the board disarms and stops within 3 s; the host's burst capture closes the record with what it has and marks it truncated.

## Units and conversion (host side)

current F32 A = currentMa / 1000; temperature F32 °C = tempDeciC / 10; duty I16 (telemetry) = dutyPct as signed percent (negative = reversed polarity). Wire ranges cover ±32 A and ±3276 °C, far beyond S6's 0.1 Ω sense path.

## Versioning

`protocolVersion` 1. Adding a message type is backward compatible (unknown types are counted and dropped). Changing a payload layout is a new protocol version and both firmwares change together.

## Why not reuse the camera protocol or CCSDS

The camera link is line-oriented ASCII with a fixed ACK string; CCSDS TC/TM framing is 15+ bytes of header per packet and assumes the ground segment. Neither fits an 8 KB microcontroller on a 20-byte housekeeping cadence. What **is** reused is the CRC and the "pure codec + thin component" pattern from TcFrameCorrector and PersistedRecord.
