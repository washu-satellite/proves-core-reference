# HP-13 RF cease/resume, buffering and LoRa command path (T3 Bench-RF)

| Field | Value |
|---|---|
| Tier | T3 Bench-RF: CI `integration-radio` layout; a second v5 board runs the CircuitPython LoRa passthrough (`code-lora.py`) |
| Hardware | FC board on UART (GDS-A: commands, acks, UART telemetry); passthrough board on USB CDC (GDS-B on `LORA_PASSTHROUGH_TTY` for RF commands/telemetry, plus a raw serial byte logger as in `lora_passthrough_test.py`) |
| Image / flash | Current image, NORMAL |
| Preconditions | Sequence number synced on GDS-B; `RD.downlinkDelay.DIVIDER_PRM_SET 20` sent while TRANSMIT is DISABLED, then `RD.lora.TRANSMIT ENABLED` (`conftest.py:_enable_radio` order); packet level 1 (Beacon) is enough; comQueue events depth 50 |
| Restore | `lora.TRANSMIT` to the rig's session default (CI leaves DISABLED); `downlinkDelay.DIVIDER` default; `telemetryGate` ENABLED |
| Destructive | No |
| Duration | ~10 min |

Windows: RF silence 70 s (2 packetizer periods + 10 s); resume 45 s; ack 10 s.

## Procedure
1. GDS-B (RF): `CMD_NO_OP`. Observable: `OpCodeCompleted` received over RF <= 10 s; zero `tcDeframer.InvalidCrc` (CH-L2-01 LoRa half; UART half is HP-01 steps 3, 6). Also `CMD_NO_OP_STRING "Hello World!"` echoed over RF (CDH-19 radio job evidence).
2. GDS-A (UART): `RD.lora.TRANSMIT DISABLED`. Observable: OK over UART <= 10 s at time Td. Start the raw byte logger on the passthrough CDC port.
3. 70 s from Td. Observable on GDS-B and the byte logger: zero packets of any type (telemetry, events, acks) and zero non-noise bytes (CDH-3, CH-L2-13).
4. During step 3, GDS-B sends 3 x `CMD_NO_OP` over RF (uplink receive is unaffected). Observable: no downlink for any of them during the window (CDH-8). GDS-A confirms each was dispatched (`OpCodeDispatched` over UART).
5. GDS-A: `RD.lora.TRANSMIT ENABLED`. Observable on GDS-B: >= 1 event and >= 1 telemetry item <= 45 s (CH-L2-14, CDH-3 clause 2); the 3 `OpCodeCompleted` from step 4 <= 45 s (CDH-8, DH-L2-01); a Beacon packet with the latest `BootCount` on the next packetizer run (DH-L2-01 clause 2).
6. GDS-B: `telemetryGate.SET_TRANSMIT_STATE DISABLED` then `ENABLED`. Observable: `TransmitStateSet` events over RF; channelized telemetry resumes over RF <= 45 s after ENABLED (CH-L2-14 clause 2).
7. Restore per header.

## Criteria
| ID | Criterion | Automated | Evidence |
|---|---|---|---|
| CDH-3 | After TRANSMIT DISABLED ack: zero packets of any type over RF for 70 s; after ENABLED an event or telemetry item <= 45 s | manual (no int test; candidate new `radio_test.py` case, must not be `uart_only`) | Steps 3, 5; byte-logger file |
| CH-L2-13 | Zero packets of any kind over RF in the 70 s after the DISABLED ack | manual | Step 3 |
| CH-L2-14 | After TRANSMIT ENABLED >= 1 event and >= 1 telemetry item <= 45 s; after telemetryGate ENABLED channelized telemetry <= 45 s | manual | Steps 5-6 |
| CDH-8 | 3 RF commands during DISABLED yield no downlink; all 3 OpCodeCompleted <= 45 s after ENABLED (events depth 50) | manual | Steps 4-5 GDS-B event log |
| DH-L2-01 | Events generated while DISABLED (3 OpCodeCompleted) delivered <= 45 s of ENABLED; latest telemetry packet on next run | manual | Step 5 |
| CH-L2-01 | CMD_NO_OP acked <= 10 s over UART (HP-01) and over LoRa; zero InvalidCrc | command_path_test.py::test_01_no_op_string_round_trip (runs in both CI jobs) | Step 1 + HP-01 step 3 |

## Why this verifies it
- CDH-3 / CH-L2-13: the requirement is RF silence, not channelized-telemetry silence; the passthrough's raw byte count is an oracle outside the FSW and catches events and acks that the gate (HP-05) would let through. Two full packetizer periods rule out a between-runs gap. The DISABLE ack is taken over UART because it cannot arrive over RF (that absence is itself part of the evidence).
- CDH-8 / DH-L2-01: buffering is shown by *later delivery* of events created during silence, with UART dispatch proving they were generated then, not re-sent on request. Depth 50 is respected (3 << 50). The tlm queue depth is 1, so only the latest packet is expected, matching the criterion.
- CH-L2-14: resume is measured on the RF observer within 45 s of the UART-side ack.
- CH-L2-01 / CDH-19: the same command/echo pair over the second interface completes the "all supported interfaces" scope.

## Known traps
- `downlinkDelay.DIVIDER` is latched on TRANSMIT ENABLED: set it while DISABLED or the ~30 s default gap breaks the 45 s windows.
- ComQueue drops the *new* message on overflow and logs `QueueOverflow` once (then throttled); `startup.seq` filters that event: keep buffered events well under 50 and count acks, not overflow events.
- Command-loss timer runs on the LoRa router: keep an RF command at least every COMM_LOSS_TIME (default 72 h); irrelevant here but not in HP-14.
- `uart_only` tests (resets, TRANSMIT toggles in tests) must run from GDS-A only; the CI radio job excludes them.
- `recover_from_safe_mode` runs with `--with-radio`; a partial file upload can trigger safe mode mid-session.
