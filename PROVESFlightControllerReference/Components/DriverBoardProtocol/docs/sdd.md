# Components::DriverBoardProtocol

Pure wire codec for the SCALAR driver-board link: the RP2350 flight controller
(uart1, J18 pins 9/10) talks to the STM32L031 driver board (S6) over a small
framed byte protocol. This module is the host side of that protocol and the
single normative spec for it; the STM32 firmware (A1, not in this repo)
implements the other end from the same section below. Nothing here touches
F Prime: `DriverBoardProtocol.hpp/.cpp` (framing, byte-at-a-time parser,
counters) and `DriverBoardMessages.hpp` (one POD struct and pack/unpack pair
per message type, plus the host-side unit conversions) include only
`<cstdint>`, `<cstddef>`, `<cstring>` and `Components/Crc16/Crc16.hpp`. The
F Prime component that owns the UART, the parameters, the commands and the
telemetry is `Components::DriverBoardHandler` (Cycle E row E4); it calls this
codec and nothing else parses the link.

Pattern: "pure codec beside a thin component", as `TcFrameCorrectorCodec` and
`PersistedRecordCodec`. The codec is linked into the host gtest binary
directly (`test/unit-tests/test_DriverBoardProtocol_Codec.cpp`).

## Wire protocol (normative)

The section between the two marker lines is copied verbatim from
`docs-site/dev-loop/cycles/cycle-e-plan/02-protocol.md` (branch
`feat/driver-board`, 2026-09-18). Edit the plan file and re-copy; do not edit
here. Check with
`diff <(sed -n '/^<!-- BEGIN 02-protocol.md -->$/,/^<!-- END 02-protocol.md -->$/p' docs/sdd.md | sed '1d;$d') docs-site/dev-loop/cycles/cycle-e-plan/02-protocol.md`.

<!-- BEGIN 02-protocol.md -->
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
<!-- END 02-protocol.md -->

### Errata found while implementing (the STM32 owner must read these)

- **HK payload is 23 bytes, not 22.** The HK field list (I16[3] + I16[2] +
  I8[3] + U8 + U8 + U32 + U32) sums to 6+4+3+1+1+4+4 = 23. The codec
  implements the field list; `HK_LEN = 23`. The "22 bytes" figure in the
  table above is wrong and stays there only because the section is verbatim.
- **Resync rescans the bytes already taken into a rejected frame.** "Discards
  to the next 0x5C" is implemented as: drop the leading byte of the buffered
  candidate and hunt again from the byte after it, so bytes that a truncated
  or length-corrupted frame swallowed are re-examined and a following valid
  frame inside them is still delivered. A receiver that instead clears its
  whole buffer on rejection loses the frame that arrived immediately after a
  truncation; the host-side tests (DriverBoardProtocol-3) require delivery.
- **One rejection is counted per contiguous stretch of undeliverable bytes**,
  not per byte and not per SYNC candidate inside that stretch: a corrupt
  frame, the garbage between two frames, and a false SYNC whose candidate
  fails on LEN or CRC each add one to `Stats::rejected`. The counter resets
  its "stretch" on the next accepted frame.
- **The parser is type-agnostic.** Any CRC-valid frame with LEN <= 32 is
  delivered whatever its TYPE; the "unknown types are counted and dropped"
  rule of the Versioning section is the caller's job (`isKnownType()` in
  `DriverBoardMessages.hpp` supports it). SEQ is tracked per parser instance,
  i.e. per direction, as the spec requires.

## API summary

`namespace Components::DriverBoardProtocol` (`DriverBoardProtocol.hpp`):

| Item | Meaning |
|---|---|
| `SYNC0`, `SYNC1`, `HEADER_SIZE` (5), `CRC_SIZE` (2), `MAX_PAYLOAD` (32), `MIN_FRAME` (7), `MAX_FRAME` (39), `BOARD_TO_HOST_BIT` (0x80) | framing constants from the spec |
| `enum class Type : uint8_t` | every TYPE code in the two message tables |
| `enum class Reject : uint8_t { NONE, SYNC, LENGTH, CRC }` | why the last candidate was dropped |
| `struct Frame { type, seq, len, payload[MAX_PAYLOAD] }` | one decoded frame, by value |
| `struct Stats { accepted, rejected, seqGaps, lastReject }` | per-parser counters; `seqGaps` counts accepted frames whose SEQ is not previous+1 (mod 256) |
| `size_t encode(uint8_t type, uint8_t seq, const uint8_t* payload, uint8_t len, uint8_t* out, size_t outCapacity)` | writes SYNC TYPE SEQ LEN PAYLOAD CRC; returns the frame length or 0 if `len > MAX_PAYLOAD` or `out` is too small |
| `class Parser` | `bool feed(uint8_t)` returns true exactly when a frame became available; `Frame frame() const` copies it out (valid until the next `feed`); `const Stats& stats() const`; `void reset()` clears the buffer and the SEQ history, keeps the counters; `void resetStats()` |

`DriverBoardMessages.hpp` adds, per message type `X` in the spec, a POD struct
`X` (host-order integers), `X_LEN`, `uint8_t pack(const X&, uint8_t* payload)`
(writes little-endian bytes one at a time, returns `X_LEN`), and
`bool unpack(const Frame&, X&)` (false unless `frame.type == Type::X` and
`frame.len == X_LEN`). `template <class M> size_t encodeMessage(const M&, uint8_t seq, uint8_t* out, size_t cap)`
packs and frames in one call. Payload-less messages (HK_REQUEST, DISARM, ABORT,
PING, STREAM_STOP) have empty structs so the handler uses one code path.
Constants: `ARM_MAGIC` (0xA5), `PROTOCOL_VERSION` (1), `enum class AckStatus`,
`enum class BoardState`, `FAULT_*` bit masks, `STREAM_RATES_HZ`. Host-side
conversions: `currentAmps(int16_t mA)`, `temperatureC(int16_t deciC)`,
`dutyPercent(int8_t)`; `isKnownType(uint8_t)`.

## Limits

- Payload length 0..32; frame 7..39 bytes; `sizeof(Parser) <= 64`
  (one 39-byte buffer, a length, a discard flag, SEQ history and four
  counters). No heap, no static objects, no exceptions, no STL.
- `feed()` is O(1) amortised: a rejection triggers a rescan of at most 38
  buffered bytes, and each rescan step drops at least one byte.
- Counters are `uint32_t` and wrap; the handler publishes them as telemetry.
- `Frame::payload` beyond `len` is zero-filled by `frame()`.
- The parser does not validate TYPE, SEQ or the payload contents; the message
  layer validates TYPE and LEN, and the handler validates argument ranges.

## Requirements

Pass criteria are decided before testing; edit with `scripts/req.py`, not by hand.

| Name | Description | Method | Level | Pass Criteria | Status | Reason |
|---|---|---|---|---|---|---|
|DriverBoardProtocol-1|Encode produces `SYNC TYPE SEQ LEN PAYLOAD CRC` with CRC-16/CCITT-FALSE over TYPE..PAYLOAD|Unit Test|Unit|Round-trip of every message type is byte-identical; CRC equals `Crc16::ccitt` of the covered bytes|||
|DriverBoardProtocol-2|Parser accepts one valid frame per SYNC..CRC sequence|Unit Test|Unit|Feeding a valid frame byte-wise makes `feed()` return true exactly once, on the last CRC byte, and `frame()` then holds the same type, seq, len and payload|||
|DriverBoardProtocol-3|Parser rejects and resynchronises|Unit Test|Unit|A corrupt CRC, a LEN > 32, or a truncated frame yields no frame and one counted rejection; the next valid frame after arbitrary garbage is still delivered|||
|DriverBoardProtocol-4|Fixed memory|Unit Test|Unit|Parser holds at most one 39-byte frame; no heap; `sizeof(Parser) <= 64`|||
|DriverBoardProtocol-5|Wire units are integers|Unit Test|Unit|HK and SAMPLE payloads carry mA, 0.1 °C, percent and ms as integers; host-side conversion to F32 is exact for the ranges in 02|||
|DriverBoardProtocol-6|Sequence continuity is observable|Unit Test|Unit|Two accepted frames with seq 5 then 7 leave `stats().accepted == 2` and `stats().seqGaps == 1`; seq 7 then 8 leaves `seqGaps` unchanged|||

## Change Log
| Date | Description |
|---| --- |
|Sep 2026| Initial version (Cycle E row E3): codec, message pack/unpack, spec copy |
