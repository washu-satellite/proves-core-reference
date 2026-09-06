# HP-01 Command path over UART (T1 Desk-USB)

| Field | Value |
|---|---|
| Tier | T1 Desk-USB: one v5e flight controller, GDS over UART, no radio |
| Hardware | FC board, USB/UART, optional Pico Debug Probe |
| Image / flash | Current `bootable.signed.hex`; fresh boot (BootCount noted) |
| Preconditions | Sequence number synced (`sync_sequence_number_test.py`); `lora.TRANSMIT DISABLED` (step 2); `CdhCore.tlmSend.SET_LEVEL 5` (RejectedPacketsCount is not in the Beacon packet); `telemetryDelay.DIVIDER` default 29 |
| Restore | `SET_LEVEL 1`; nothing else changes persistent state |
| Destructive | No |
| Duration | ~10 min automated, ~20 min manual |

Prefix `RD.` = `ReferenceDeployment.`. Windows: ack 10 s (`proves_send_and_assert_command`), event 5 s, one packetizer period + margin 45 s.

## Procedure
1. Run `sync_sequence_number_test.py` first. Observable: its pass. Window: n/a.
2. `RD.lora.TRANSMIT DISABLED`. Observable: OpCodeCompleted over UART <= 10 s.
3. `RD.cmdDisp.CMD_NO_OP`. Observable: `cmdDisp.OpCodeDispatched` then `OpCodeCompleted` <= 10 s; `startupManager.BootCount` received <= 45 s (Beacon packet, level 1 suffices).
4. `RD.cmdDisp.CMD_NO_OP_STRING "Hello World!"`. Observable: `NoOpStringReceived` echo byte-exact, OpCodeCompleted <= 10 s.
5. `RD.modeManager.GET_CURRENT_MODE`. Observable: `CurrentModeReading` with a value in the SystemMode enum <= 5 s.
6. 10 x `CMD_NO_OP` sequentially. Observable: each acked <= 10 s; zero `tcDeframer.InvalidFrameLength/InvalidCrc/InvalidSpacecraftId`, zero `spacePacketDeframer.InvalidLength` over the whole step.
7. `CMD_NO_OP_STRING` with a 40-char argument. Observable: echoed intact, acked <= 10 s; zero `frameAccumulator.FrameDetectionSizeError`.
8. 5 x `CMD_NO_OP` sent back-to-back without waiting. Observable: 5 `OpCodeCompleted` <= 15 s; zero `TooManyCommands` / `CommandDroppedQueueOverflow`.
9. `cmdDisp.CMD_NO_OP`, `modeManager.GET_CURRENT_MODE`, `startupManager.GET_BOOT_COUNT`. Observable: each component's own response event <= 5 s.
10. Send one non-bypass command framed with a stale sequence number (manual: no automated fixture on this branch, see the note below). Observable: `authenticate.SequenceNumberOutOfWindow` <= 5 s; no `OpCodeDispatched` for that opcode <= 5 s; `authenticate.RejectedPacketsCount` +1 <= 45 s; the next correctly numbered command is acked <= 10 s.
11. `SET_LEVEL 1`.

## Criteria
| ID | Criterion (as in matrix unless noted) | Automated | Evidence |
|---|---|---|---|
| CDH-1 | Over UART with TRANSMIT DISABLED: CMD_NO_OP acked <= 10 s and BootCount received <= 45 s | command_path_test.py::test_05_uart_command_and_telemetry_without_radio | Step 2-3 event log, BootCount receipt time |
| CDH-19 | CMD_NO_OP_STRING "Hello World!" acked <= 10 s and echoed, over UART (LoRa job in HP-13) | command_path_test.py::test_01_no_op_string_round_trip | Step 4 |
| CH-L2-02 | 10 commands acked <= 10 s each; zero deframer error events | command_path_test.py::test_02_ten_commands_no_deframer_errors | Step 6 event log filtered on deframer events |
| CH-L2-03 | 40-char argument echoed intact; zero FrameDetectionSizeError | command_path_test.py::test_02_ten_commands_no_deframer_errors | Step 7 |
| CH-L2-05 | Stale sequence rejected: SequenceNumberOutOfWindow <= 5 s, no OpCodeDispatched; next command accepted | manual (see note) | Step 10 |
| CH-L2-06 | RejectedPacketsCount +1 (level 5); no dispatch/completion for that opcode <= 5 s | manual (see note) | Step 10 channel history |
| CH-L2-07 | Echo byte-exact; GET_CURRENT_MODE returns a valid SystemMode | command_path_test.py::test_01_no_op_string_round_trip | Steps 4-5 |
| CH-L2-08 | Every accepted command: OpCodeDispatched then OpCodeCompleted <= 10 s | command_path_test.py::test_01_no_op_string_round_trip | Steps 3-9 event order |
| CH-L2-09 | 5 back-to-back commands all complete <= 15 s; zero queue-overflow events | command_path_test.py::test_03_burst_of_five_commands | Step 8 |
| CH-L2-10 | Commands to 3 components each answered <= 5 s | command_path_test.py::test_04_routing_to_three_components | Step 9 |

> Note on step 10 and CH-L2-05/06: the automated fixture that drove this step targeted the `Components/Authenticate` uplink stack, which this branch's base replaced with `Components/TcSecurityDeframer`. The step is manual until a fixture is written against TcSecurityDeframer, and the event/channel names above (`authenticate.SequenceNumberOutOfWindow`, `authenticate.RejectedPacketsCount`) still name the old component, so re-derive them from `Components/TcSecurityDeframer/TcSecurityDeframer.fpp` before running it.

## Why this verifies it
- CDH-1: the observable is ground command/telemetry with the radio provably disabled (step 2 ack precedes step 3), on the same UART path EGSE uses; oracle is the GDS, not FSW counters.
- CDH-19, CH-L2-07: byte-exact echo proves the argument survived framing, authentication and deserialisation; the mode readback adds a typed (enum) argument-free command. LoRa uplink half is added by HP-13 step 1.
- CH-L2-02/03: negative path is "no error event" while forcing the fragmentation case (40-char frame exceeds one 10 Hz UART read chunk). Deframer/accumulator events are emitted by library components, independent of the command handler.
- CH-L2-05/06: the negative path is provoked (stale sequence number); rejection is shown both by the absence of dispatch and by an independent counter (RejectedPacketsCount).
- CH-L2-08/09/10: dispatch ordering, queue behaviour under burst and opcode routing are each observed by events from the component that acted, not from the sender.
- Remainder: CH-L2-01 (both interfaces) lives in HP-13; UART half is steps 3 and 6.

## Known traps
- `SequenceNumberOutOfWindow` is `throttle 2`: step 10 may be run at most twice per boot; reboot before a rerun.
- Default packet level 1 downlinks only the Beacon; `RejectedPacketsCount` needs level 5 and is sent on change, so allow one full period (45 s).
- `command_path_test.py::test_05` is `uart_only`; never run this group from a radio GDS.
- Do not `PRM_SAVE_FILE`; nothing in this group should persist.
