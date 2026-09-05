# Components::TcFrameCorrector

Passive component that repairs single-bit errors in uplinked CCSDS TC transfer
frames on the LoRa link (CH-L2-20, N=1). It is a byte-identical pass-through
until the `CORRECTION_ENABLED` parameter is set, and that parameter defaults to
**false** — the flight setting until the board procedure below has run.

## Placement, and why it precedes the frame accumulator

```
lora.dataOut -> tcFrameCorrector.dataIn
tcFrameCorrector.dataOut -> ComCcsdsLora.frameAccumulator.dataIn
ComCcsdsLora.frameAccumulator.dataReturnOut -> tcFrameCorrector.dataReturnIn
tcFrameCorrector.dataReturnOut -> lora.dataReturnIn
```

The obvious placement — between the accumulator and the deframer, acting on a
reported CRC mismatch — cannot work. `Svc::FrameDetectors::CcsdsTcFrameDetector::detect`
verifies the FECF itself (`lib/fprime/Svc/FrameAccumulator/FrameDetector/CcsdsTcFrameDetector.cpp:55-81`)
and returns `NO_FRAME_DETECTED` for a CRC-failing frame, whereupon
`FrameAccumulator::processRing` silently rotates the ring by one byte
(`lib/fprime/Svc/FrameAccumulator/FrameAccumulator.cpp:170-177`).
`TcDeframer::InvalidCrc` (`lib/fprime/Svc/Ccsds/TcDeframer/TcDeframer.cpp:99-104`)
is therefore unreachable for accumulator-fed frames: nothing downstream of the
accumulator ever sees a corrupt frame.

The corrector consequently sits on the raw LoRa packet, ahead of the
accumulator. That is sound here because LoRa RX delivers exactly one radio
packet per `Fw::Buffer` (`lib/fprime-zephyr/fprime-zephyr/Drv/LoRa/LoRa.cpp:162-177`),
so the whole buffer is one candidate frame. No `lib/` change and no detector
replacement is needed. Replacing the detector was rejected: it would weaken
byte-wise resynchronisation on garbage.

The UART link is **not** covered. `comDriver.$recv` delivers arbitrary byte
chunks at 10 Hz (`ComCcsdsUart/ComCcsds.fpp:211-212`) and frame boundaries only
exist after accumulation, so there is no single-frame buffer to correct.

## Algorithm

The pure codec (`TcFrameCorrectorCodec.hpp/.cpp`, namespace
`Components::TcFrameCorrection`) is F´-free — `<cstdint>` only — so host tests
link it directly.

1. `len < 7` or `len > 252` → `PASS_THROUGH`, bytes untouched. The bounds are
   header (5) + trailer (2) and the LoRa `MAX_PACKET_SIZE` (`LoRa.hpp:19`).
2. `S = crc16Ccitt(frame, len-2) XOR BE16(frame + len-2)`. `S == 0` → `VALID`,
   bytes untouched.
3. `S` a power of two → the flipped bit is in the FECF itself; flip it back →
   `CORRECTED`.
4. Otherwise walk the data-bit syndromes backwards from the last data bit,
   whose syndrome is the generator 0x1021, each earlier bit's being the
   previous multiplied by x modulo the generator. On a match, flip that bit and
   post-check: header token == 0x2044, declared length + 1 == len, FECF now
   valid. Pass → `CORRECTED`; fail → un-flip → `UNCORRECTABLE`.
5. No match → `UNCORRECTABLE`, bytes untouched.

The CRC is CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, MSB-first, xorout 0),
identical to `Svc::Ccsds::Utils::CRC16::compute` (`lib/fprime/Svc/Ccsds/Utils/CRC16.hpp:43-49`);
check value for "123456789" is 0x29B1. The expected token is
`(1 << TCSubfields::BypassFlagOffset) | ComCfg::SpacecraftId` = 0x2044, the same
expression the frame detector uses (`CcsdsTcFrameDetector.hpp:45-46`).

Correctness rests on CRC-CCITT factoring as (x+1)(x^15+x+1). Verified
numerically for frame lengths 7, 16, 24, 64, 252 and 1024: every single-bit
syndrome is unique and non-zero, the sixteen FECF-bit syndromes are exactly the
powers of two and disjoint from every data-bit syndrome, and no two-bit error
(all 18336 pairs at len 24) maps onto a single-bit syndrome or onto zero. So
two-bit errors are always `UNCORRECTABLE`. Three-or-more-bit errors can land on
a single-bit syndrome; the post-check rejects nearly all of them, and the
residue is rejected downstream by the HMAC in `Authenticate`.

### Cost, and why there is no candidate search

`dataIn_handler` runs in the Zephyr system workqueue — the LoRa DIO1 `k_work`
callback context (`lib/zephyr-workspace/zephyr/drivers/lora/loramac_node/sx12xx_common.c:100-113`,
`sx126x.c:412-444`). The syndrome walk is therefore O(8·len) 16-bit shifts plus
at most two CRC passes over ≤ 250 bytes — tens of microseconds on the RP2350 —
with no allocation, no stack frame buffer and no copy. The flight code contains
no loop over candidate flips re-running the CRC; that brute force exists only as
the oracle in `test_TcFrameCorrector_Codec.cpp`, where it is required to agree
with the walk on every single-bit error of a 32-byte frame.

## Buffer ownership

Every buffer arriving on `dataIn` leaves exactly once on `dataOut`, whatever the
outcome — including uncorrectable frames, which are forwarded unchanged so the
frame accumulator drops them byte-wise exactly as it does without this
component. Every buffer arriving on `dataReturnIn` leaves exactly once on
`dataReturnOut`, with the same context. The buffer belongs to `lora`, which
allocates it in `LoRa::receive` (`LoRa.cpp:165`) and frees it when ownership
returns on `lora.dataReturnIn` (`LoRa.cpp:158-160`); in-place bit flips are
legal because `Fw::Buffer::getData()` returns a non-const pointer.

When `CORRECTION_ENABLED` is false, or the parameter read is not `VALID` or
`DEFAULT`, the same `Fw::Buffer` object is forwarded with no copy and no byte
read or written, and no event or telemetry is emitted.

## Limits and open items

- N = 1. Two-bit errors are rejected, not repaired. N > 1 is TBD by Mission Ops.
- LoRa uplink only; the UART link is unprotected (see above).
- One TC frame per LoRa packet with no padding is an assumption. A padded or
  multi-frame packet fails the length post-check and passes through unchanged —
  never harmful, just uncorrected.
- `CORRECTION_ENABLED` stays **false** in flight until the deferred board
  procedure (`test/int/tc_frame_corrector_test.py`) has run against a ground
  framer that can inject a single-bit error. Flipping the default is a
  one-line change plus a dictionary update.
- The LoRa PHY CRC is off (`Radio.SetRxConfig(..., crcOn=false, ...)`,
  `sx12xx_common.c:357-360`), so corrupted packets do reach F´ and
  `FrameUncorrectable` can be driven by RF noise; it is throttled at 5 and the
  counter carries the rate. It is only emitted when correction is enabled.

## Port Descriptions

| Port | Kind | Type | Purpose |
|---|---|---|---|
| dataIn | guarded input | Svc.ComDataWithContext | Raw uplink packet from `lora.dataOut` |
| dataOut | output | Svc.ComDataWithContext | Packet (repaired or untouched) to the frame accumulator |
| dataReturnIn | sync input | Svc.ComDataWithContext | Ownership handed back by the frame accumulator |
| dataReturnOut | output | Svc.ComDataWithContext | Ownership handed back to `lora.dataReturnIn` |

## Parameters

| Parameter | Type | Default | Purpose |
|---|---|---|---|
| CORRECTION_ENABLED | bool | false | Enables in-place single-bit correction. False = byte-identical pass-through. |

## Telemetry

| Channel | Type | Description |
|---|---|---|
| CorrectedFrames | U32 | Cumulative count of frames repaired since boot |
| UncorrectableFrames | U32 | Cumulative count of frames whose errors could not be repaired since boot |

Both channels are carried in the `HealthAuxiliary` packet (id 4, group 5).

## Events

| Event | Severity | Description |
|---|---|---|
| FrameCorrected(bitIndex, frameLength) | activity high | A single-bit error was located and repaired in place |
| FrameUncorrectable(frameLength) | warning low, throttle 5 | The FECF did not check out and no single-bit error explains it |

## Requirements

Pass criteria are decided before testing; edit with `scripts/req.py`, not by hand.

| Name | Description | Method | Level | Pass Criteria | Status | Reason |
|---|---|---|---|---|---|---|
|TcFrameCorrector-1|Every single-bit flip of a valid TC frame shall be restored in place and the frame forwarded|Unit Test|Unit|Exhaustive over all 8*len bit positions for len in {7,16,64,252}: result CORRECTED, bytes equal the original, bitIndex equals the flipped position|||
|TcFrameCorrector-2|Errors of two or more bits shall be rejected and the frame left byte-for-byte unchanged|Unit Test|Unit|All C(8*len,2) two-bit patterns for len=24: result UNCORRECTABLE and the buffer identical to the corrupted input|||
|TcFrameCorrector-3|While CORRECTION_ENABLED is false, or the parameter read is not valid, the component shall be a byte-identical pass-through|Unit Test|Unit|The same Fw::Buffer pointer and size are forwarded on dataOut exactly once, no byte of the buffer changes, and zero events and zero telemetry writes are produced|||
|TcFrameCorrector-4|Buffer ownership shall be conserved: each dataIn buffer is forwarded exactly once on dataOut and each dataReturnIn buffer exactly once on dataReturnOut, with the frame context unchanged|Unit Test|Unit|For the disabled, corrected and uncorrectable paths: exactly 1 dataOut call with the same pointer/size/context per dataIn, and exactly 1 dataReturnOut call with the same pointer/size/context per dataReturnIn|||
|TcFrameCorrector-5|Frames shorter than 7 or longer than 252 bytes, and frames failing the header-token or declared-length post-check, shall be forwarded unchanged|Unit Test|Unit|len 6 and len 253 return PASS_THROUGH with bytes unchanged; a padded frame and a frame carrying a foreign token are not CORRECTED and their bytes are unchanged|||

## Change Log
| Date | Description |
|---| --- |
|Sep 2026| Initial version (CH-L2-20, N=1) |
