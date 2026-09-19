# Components::DriverBoardHandler

Passive component that owns the SCALAR driver board (STM32L031, S6) over a
`Drv.ByteStreamDriver` on uart1. It sends one host frame per 1 Hz tick
(HEARTBEAT, or HK_REQUEST every `HK_INTERVAL_S`), supervises the link, exposes
ARM / DISARM / PULSE / ABORT / PING / GET_STATUS, and turns the board's
housekeeping into the `PayloadHousekeeping` telemetry channels. The wire
protocol is the pure codec `Components/DriverBoardProtocol` (its `docs/sdd.md`
is the spec both processors implement); the link and arming logic is the pure
module `DriverBoardLink` in this directory. The component itself is thin: port
handlers call the codec and the link module.

Safety by structure: boots DISARMED and LINK_DOWN; PULSE needs ARMED; ARM needs
LINK_UP and `modeManager.getMode` != SAFE_MODE and completes only on the
board's ACK(ARM, 0); silence on the link for `LINK_TIMEOUT_MS`, SAFE_MODE, a
board FAULT frame or an HK state of FAULT all force DISARMED. The STM32 has its
own 3 s host-timeout failsafe in the spec, not host-configurable. With no board
attached the handler writes one DISARM after the driver is ready and then one
7-byte HK_REQUEST per second into an open TX pin, never raises the link, and
emits no event.

This component lands **unwired** (Cycle E row E4). Row E5 adds the
`driverBoardUart`, `driverBoardBufferManager` and `driverBoardHandler`
instances, the `connections DriverBoard` block, rate-group slots 50 Hz[1],
10 Hz[5], 1 Hz[12], and the packet. Until then nothing instantiates it.

## Locking rule, and why

`uartRecv` is a **guarded** input port: the framework holds the component lock
for the whole handler. `run`, `uartReady` and every command are **sync** (a
`sync command` takes no guard in F´; only `guarded command` does) and bracket
their state work with the explicit `this->lock()` / `this->unLock()` around
state mutation only.

**No frame is ever sent, and the mode is never polled, while the lock is
held.** `run` polls `getMode` and reads the time first, then locks, decides,
builds the frame into a stack-local 39-byte array, unlocks, and only then calls
`uartSend`. Every command does the same. `uartRecv` never calls `uartSend` at
all: a received frame that wants a DISARM sent (a board fault while ARMED)
sets a flag in the link module and the next `run` tick sends it in place of
that tick's heartbeat.

Reason: `ZephyrUartDriver.schedIn` and `$send` are both guarded on the driver's
mutex. The 50 Hz thread (priority 1) holds driver-lock and enters handler-lock
through `uartRecv`; a 1 Hz `run` (priority 3) holding handler-lock and calling
`$send` would take driver-lock — an ABBA inversion that deadlocks the first
time bytes arrive during a tick, only with a board attached and invisible on
the host. The host stub counts `uartSend`/`getMode` calls made while the lock
is held and `test_DriverBoardHandler_Component.cpp` asserts zero in every
test.

`uartRecvReturn` and `sampleOut` **are** called from inside the guarded
`uartRecv` handler. That is the same thread that already holds the driver's
mutex (the driver calls `$recv` from inside its guarded `schedIn`), so
`recvReturnIn` re-enters that mutex recursively (Zephyr `k_mutex` is
reentrant), exactly as `Svc.FrameAccumulator.dataIn` returns buffers to
`comDriver` on the existing uplink path. No other thread is involved, so there
is no inversion. `sampleOut` reaches a passive consumer on this same thread
(A9).

## Link state machine (`DriverBoardLink`, pure, host-tested)

```
                 valid board frame                  silence > LINK_TIMEOUT_MS
   LINK_DOWN ─────────────────────► LINK_UP ─────────────────────────► LINK_DOWN
   (DISARMED forced)                                     (DISARMED forced, LinkLost, LinkTimeouts++)

   DISARMED ──ARM sent + ACK(ARM, 0)──► ARMED ──PULSE──► (board reports PULSING in HK) ──► ARMED
   any ──DISARM / ABORT / SAFE_MODE / LINK_DOWN / FAULT frame with flags != 0 / HK.state == FAULT──► DISARMED
```

Inputs per 1 Hz tick: `nowMs` (from `getTime()`, a wrapping millisecond
counter), the effective `LINK_TIMEOUT_MS`, the SAFE_MODE poll, and
`RunInterval::due(HK_INTERVAL_S)`. Outputs: the transitions that happened and
the one host frame to send: a pending DISARM takes the slot, otherwise
HK_REQUEST when the cadence is due, else HEARTBEAT carrying `nowMs`.

Silence is measured on the tick only, with strict "greater than": a frame that
arrived exactly one timeout ago still counts. With the default 1000 ms the
link drops on the second silent tick after the last frame (detection latency
1–2 s). Every fully decoded board frame (ACK, HK, PONG, SAMPLE, FAULT) stamps
`lastValidFrameMs` in `uartRecv`; a frame with a known TYPE but the wrong
payload length, an unknown TYPE, or a host-type frame (TX/RX loopback) does
not. The first frame after `uartReady` is a DISARM sent from the ready handler
itself, so a board that is present acknowledges it and the link is up before
the first tick.

ARM: the command sends ARM(0xA5) and responds OK when the frame is sent (a sync
command cannot wait for the ACK). ARMED follows only on ACK(ARM, 0) while the
ARM is pending and the link is up; ACK(ARM, status != 0) emits
`CommandRefused(ARM, BOARD_REFUSED)` and the state stays DISARMED; an
unsolicited ACK never arms. A pending ARM is cancelled by DISARM, ABORT or link
loss. Automatic disarms (LINK_LOST, SAFE_MODE, BOARD_FAULT) emit `Disarmed`
only when leaving ARMED, so a missing board is quiet; DISARM and ABORT emit it
on every command, for the record.

## Buffer ownership

Every buffer arriving on `uartRecv` leaves exactly once on `uartRecvReturn`
before the handler returns, whatever its status or content (an empty buffer, a
buffer the driver flagged as failed, garbage, a truncated frame). Only a buffer
with status `OP_OK` and a non-zero size is parsed. The driver deallocates it to
`driverBoardBufferManager`. TX frames are stack-local arrays in the calling
handler, built under the lock and sent after unlock; `$send` is synchronous and
the driver copies byte-by-byte, so the array is free on return. At most one
frame per tick per direction in this cycle, so no TX queue.

## Received frames

| TYPE | Effect |
|---|---|
| ACK | ARM pending: status 0 → `Armed`, DriverState ARMED; else `CommandRefused(ARM, BOARD_REFUSED)`. PULSE with status != 0 → `PulseRefused(status)` and `CommandRefused(PULSE, BOARD_REFUSED)`. Other ACKs are valid frames and nothing more. |
| HK (23 B) | Writes CoilCurrent0-2 (mA / 1000), CoilTemperature0-1 (0.1 °C / 10), PwmDuty0-2, FaultFlags, BoardUptime, DriverState (the board's state byte mapped; a byte outside 1..4 → UNKNOWN). HK state FAULT while ARMED → `Disarmed(BOARD_FAULT)` and a DISARM on the next tick. |
| PONG | FirmwareVersion, `PongReceived(fw, proto)`. If this PONG raises the link, `LinkUp` carries its version; otherwise `LinkUp` carries the last known version, 0 if none. |
| SAMPLE | SamplesReceived++; forwarded on `sampleOut` only when that port is connected (A9). |
| FAULT | `BoardFault(flags, value)`, FaultFlags; flags != 0 while ARMED → `Disarmed(BOARD_FAULT)`, DriverState DISARMED, DISARM on the next tick. |
| host-type (0x01..0x0A) | Counted by the parser as received, ignored (the HP-15 loopback smoke test relies on this: FramesRejected stays 0, LinkState stays DOWN). |
| unknown TYPE, or known TYPE with a wrong LEN | FramesRejected++, `FrameRejected(4)` / `FrameRejected(5)`. Parser rejections (1 SYNC, 2 LENGTH, 3 CRC) are counted once per stretch of undeliverable bytes and logged once each. |

## Parameters

RAM-only until `PRM_SAVE_FILE` (persistence is gated, plan 07). Validation is
fallback-to-default in `parameterUpdated` (Cycle B pattern): an INVALID/UNINIT
read or an out-of-range value leaves the fpp default in force and emits one
`ParameterRejected(id)` (throttle 5). The effective value is telemetered on
every update and once on the first tick. Because `loadParameters()` at boot
does not call `parameterUpdated`, the first `run` tick re-reads all five, so a
value saved to `/prmDb.dat` takes effect after a reboot.

| Parameter | Id | Type | Range | Default | Effect |
|---|---|---|---|---|---|
| PULSE_DURATION_MS | 0 | U16 | 50..5000 | 2000 | PULSE frame field |
| PULSE_DUTY_PCT | 1 | U8 | 0..100 | 50 | PULSE frame field |
| PULSE_CHANNEL_MASK | 2 | U8 | 0x01..0x07 | 0x07 | PULSE frame field |
| LINK_TIMEOUT_MS | 3 | U16 | 100..10000 | 1000 | host supervision |
| HK_INTERVAL_S | 4 | U8 | 1..60 | 1 | HK_REQUEST cadence; HEARTBEAT fills the other ticks |

## Commands

All `sync`. Response OK, EXECUTION_ERROR on a precondition refusal (with a
`CommandRefused` event), VALIDATION_ERROR on a bad argument.

| Command | Args | Precondition | Effect |
|---|---|---|---|
| ARM | — | LINK_UP, mode != SAFE_MODE | send ARM(0xA5); OK = sent; ARMED only on ACK(ARM, 0) |
| DISARM | — | — | DISARMED immediately, `Disarmed(COMMAND)`; DISARM frame if the driver is ready |
| PULSE | polarityMask: U8 (bits 0-2, else VALIDATION_ERROR) | ARMED | send PULSE(PULSE_DURATION_MS, PULSE_DUTY_PCT, PULSE_CHANNEL_MASK, polarityMask); PulsesCommanded++; `PulseStarted` |
| ABORT | — | — | DISARMED immediately, `Disarmed(ABORT)`; ABORT frame if the driver is ready |
| PING | — | driver ready | send PING; `PongReceived` on the reply |
| GET_STATUS | — | — | `StatusReport(link, driverState, flags, version, uptime)` |

## Telemetry

All 22 channels are carried in the `PayloadHousekeeping` packet (id 23,
group 3), added by row E5.

| Channel | Type | Source |
|---|---|---|
| CoilCurrent0, 1, 2 | F32 A | HK currentMa / 1000 |
| CoilTemperature0, 1 | F32 °C | HK tempDeciC / 10 |
| PwmDuty0, 1, 2 | I8 % | HK |
| DriverState | DriverState (U8) | local transitions; HK state byte |
| FaultFlags | U8 | HK / FAULT |
| LinkState | LinkState (U8) | supervision |
| BoardUptime | U32 ms | HK |
| FramesReceived | U32 | parser accepted count |
| FramesRejected | U16 | parser rejections + unknown TYPE + bad payload length (saturating) |
| SamplesReceived | U32 | SAMPLE frames parsed |
| LinkTimeouts | U16 | UP → DOWN transitions |
| FirmwareVersion | U16 | last PONG |
| PulsesCommanded | U16 | PULSE frames sent |
| PulseDurationMs, PulseDutyPct, LinkTimeoutMs, HkIntervalS | U16, U8, U16, U8 | effective parameter values |

## Events

| Event | Severity | When |
|---|---|---|
| LinkUp(version) | activity high | first valid board frame after LINK_DOWN |
| LinkLost(silentMs) | warning high | silence > LINK_TIMEOUT_MS on a tick, once per loss |
| FrameRejected(reason) | warning low, throttle 5 | 1 SYNC, 2 LENGTH, 3 CRC (parser), 4 unknown TYPE, 5 payload length mismatch |
| Armed() | activity high | ACK(ARM, 0) |
| Disarmed(reason) | activity high | COMMAND, LINK_LOST, SAFE_MODE, BOARD_FAULT, ABORT |
| PulseStarted(durationMs, dutyPct, channelMask, polarityMask) | activity high | PULSE frame sent |
| PulseRefused(status) | warning low | ACK(PULSE, status != 0) |
| CommandRefused(cmd, reason) | warning low | LINK_DOWN, NOT_ARMED, SAFE_MODE, DRIVER_NOT_READY, BOARD_REFUSED; `cmd` is the wire TYPE |
| BoardFault(flags, value) | warning high | FAULT frame |
| PongReceived(fwVersion, protoVersion) | activity low | PONG |
| StatusReport(link, driverState, flags, version, uptime) | activity low | GET_STATUS (`state` is an fpp keyword, hence `driverState`) |
| ParameterRejected(paramId) | warning low, throttle 5 | parameter fell back to its default |

## Limits and open items

- One host frame per tick per direction; a pending DISARM replaces that tick's
  heartbeat (it is a valid host frame, so the board's failsafe timer is still
  reset). A SAFE_MODE disarm or a board-fault disarm therefore delays one
  HK_REQUEST/HEARTBEAT by a tick.
- Link-loss detection has one-tick resolution: with the default 1000 ms the
  link drops 1–2 s after the last frame. The STM32's 3 s failsafe is the safety
  path (spec).
- `DriverState` mixes two sources on purpose: local transitions write
  DISARMED/ARMED, HK writes what the board reports. An HK state of DISARMED
  while the handler is ARMED (a board that rebooted silently) is not acted on;
  the next PULSE is refused by the board and reported as `PulseRefused`.
- `FramesRejected` saturates at 65535; `FramesReceived` and `SamplesReceived`
  wrap with the parser's U32 counters.
- The host stub does not model event throttles; the -8 test therefore checks
  one rejection per fresh component.
- STREAM_START / STREAM_STOP, TIME_SYNC and the `sampleOut` consumer are A9.
  FaultManager is not wired (plan 07: the U8 mask is full).

## Requirements

Pass criteria are decided before testing; edit with `scripts/req.py`, not by hand.

| Name | Description | Method | Level | Pass Criteria | Status | Reason |
|---|---|---|---|---|---|---|
|DriverBoardHandler-1|Boots DISARMED with LINK_DOWN; first frame after `uartReady` is DISARM; then exactly one host frame per 1 Hz tick (HK_REQUEST when `RunInterval::due(HK_INTERVAL_S)`, else HEARTBEAT)|Unit Test|Unit|After `uartReady` and 3 ticks with default HK_INTERVAL_S = 1: frames on `uartSend` are DISARM, HK_REQUEST, HK_REQUEST, HK_REQUEST; with HK_INTERVAL_S = 3: DISARM, HK_REQUEST, HEARTBEAT, HEARTBEAT; state DISARMED, no events|||
|DriverBoardHandler-2|Link comes up on the first valid board frame and drops after `LINK_TIMEOUT_MS` of silence|Unit Test|Unit|PONG at tick 1 → LinkUp event, LinkState UP; no frames for ceil(timeout/1000)+1 ticks → LinkLost once, LinkState DOWN; a second silent tick emits nothing more|||
|DriverBoardHandler-3|ARM requires LINK_UP and mode != SAFE_MODE, and completes only on board ACK|Unit Test|Unit|ARM with link down → CommandRefused(LINK_DOWN), response EXECUTION_ERROR; with link up and `getMode` = SAFE_MODE → CommandRefused(SAFE_MODE); with link up in NORMAL → ARM frame sent and command response OK (the response means "sent", not "armed"); on ACK(ARM, OK) → Armed event, DriverState ARMED; on ACK(ARM, status != 0) → CommandRefused(BOARD_REFUSED), state stays DISARMED|||
|DriverBoardHandler-4|PULSE uses the current parameters and requires ARMED|Unit Test|Unit|PULSE while DISARMED → CommandRefused(NOT_ARMED); while ARMED → one PULSE frame whose fields equal PULSE_DURATION_MS / PULSE_DUTY_PCT / PULSE_CHANNEL_MASK, PulseStarted event|||
|DriverBoardHandler-5|Link loss disarms|Unit Test|Unit|ARMED then silence past timeout → LinkLost, Disarmed(LINK_LOST), DriverState DISARMED, and the next PULSE is refused|||
|DriverBoardHandler-6|SAFE_MODE disarms|Unit Test|Unit|ARMED, `getMode` returns SAFE_MODE on the next tick → DISARM frame sent, Disarmed(SAFE_MODE)|||
|DriverBoardHandler-7|Housekeeping frames update telemetry|Unit Test|Unit|An HK frame with current {1500, -200, 0} mA, temp {251, 300}, duty {50, -50, 0}, state, flags → CoilCurrent0 = 1.5 F32, CoilTemperature0 = 25.1, PwmDuty1 = -50, DriverState, FaultFlags written once each|||
|DriverBoardHandler-8|Parameter validation falls back to default|Unit Test|Unit|PULSE_DURATION_MS 0 or 6000, PULSE_DUTY_PCT 101, LINK_TIMEOUT_MS 50, HK_INTERVAL_S 0 or 61, or INVALID → default value in effect, one ParameterRejected event each (throttle 5)|||
|DriverBoardHandler-9|Every received buffer is returned to the driver|Unit Test|Unit|For N `uartRecv` calls, N `uartRecvReturn` calls with the same buffer objects, regardless of content|||

## Change Log
| Date | Description |
|---| --- |
|Sep 2026| Initial version (Cycle E row E4): component, DriverBoardLink, host tests; lands unwired |
