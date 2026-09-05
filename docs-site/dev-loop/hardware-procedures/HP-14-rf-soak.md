# HP-14 96 h RF soak: controls telemetry downlink and command-loss timer reset (T3 Bench-RF, long duration)

| Field | Value |
|---|---|
| Tier | T3 Bench-RF (HP-13 layout left running unattended) |
| Hardware | As HP-13; GDS-B logging all RF telemetry to CSV (`tlm_sampler` fixture pattern, `TLM_SAMPLE_LOG`) |
| Image / flash | Current image, NORMAL, BootCount N0 |
| Preconditions | `CdhCore.tlmSend.SET_LEVEL 5` (`detumbleManager.Mode` is packet group 3) sent over RF; `lora.TRANSMIT ENABLED` with `downlinkDelay.DIVIDER` 20; `COMM_LOSS_TIME` at default 259200 s (72 h) |
| Restore | `SET_LEVEL 1`; TRANSMIT per rig default |
| Destructive | No (if step 2 is skipped the board WILL enter safe mode and reboot at 72 h) |
| Duration | 96 h unattended + 30 min analysis |

## Procedure
1. Start the CSV logger at T0. Observable: `detumbleManager.Mode` and coil telemetry present in the first 45 s.
2. Every 24 h (at T0+24, +48, +72 h) send one `CMD_NO_OP` over RF from GDS-B. Observable: ack <= 10 s; `RD.ComCcsdsLora.authenticationRouter.GET_COMMAND_LOSS_DATA` reports a command-loss start no older than the last command.
3. At T0+96 h stop. Split the CSV into windows [0,48) and [48,96) h. Observable: in each window `detumbleManager.Mode` and coil channels received >= 1 (expected ~5760 at the 30 s period); no `CommandLossFound`, no `EnteringSafeMode`, BootCount == N0 throughout.
4. Restore per header.

## Criteria
| ID | Criterion | Automated | Evidence |
|---|---|---|---|
| CDH-27 | detumbleManager Mode and coil telemetry received >= 1 in every 48 h window of a >= 96 h run (>= 5760 receipts per window at P = 30 s) | manual (Demonstration) | CSV per-window counts; BootCount trace |

## Why this verifies it
- CDH-27: the observable is receipt on the ground over the real downlink at the flight cadence; two complete 48 h windows are the minimum that satisfies "every 48 h window" of a 96 h run without extrapolation. Counting receipts (not just presence) documents margin against the 1-per-48 h requirement.
- Added evidence, not a claim: step 2 exercises the command-loss timer's positive path (an RF command resets it), which HP-07/HP-10 could not, because there the timer was allowed to expire. If `CommandLossFound` never fires over 96 h with commands every 24 h, CDH-11's "reset on command" behaviour is shown; record it against CDH-11 as supplementary.

## Known traps
- Default COMM_LOSS_TIME is 72 h: without step 2 the LoRa router forces safe mode and stops watchdog petting at 72 h, rebooting the board and voiding the second window.
- The gate (HP-05) and mode (HP-06) states are persisted; confirm ENABLED and NORMAL before T0.
- `tlm_sampler` writes to cwd by default; set `TLM_SAMPLE_LOG` to a path with >= 1 GB free.
