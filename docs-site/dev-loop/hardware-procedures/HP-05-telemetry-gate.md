# HP-05 Telemetry gate enable/disable over UART (T1 Desk-USB)

| Field | Value |
|---|---|
| Tier | T1 Desk-USB |
| Hardware | FC board, UART GDS |
| Image / flash | Current image; `telemetryGate.TransmitState` ENABLED at start (persisted record; check with level 5 readback) |
| Preconditions | `CdhCore.tlmSend.SET_LEVEL 5` (TransmitState/GatedTicks channels); `telemetryDelay.DIVIDER` 29 |
| Restore | `RD.telemetryGate.SET_TRANSMIT_STATE ENABLED` (state is persisted across reboot); `SET_LEVEL 1` |
| Destructive | No (but a missed restore silences telemetry after reboot) |
| Duration | ~8 min automated (two 70 s silence windows) |

Windows (from `telemetry_gate_test.py`): period 30 s; ONE_PERIOD 45 s; DISABLED window 70 s (2 periods + 10 s). Channel used: `startupManager.BootCount` (Beacon, always updated).

## Procedure
1. `SET_LEVEL 5`. Observable: ack; `telemetryGate.TransmitState` ENABLED <= 45 s.
2. Baseline 45 s: `BootCount` received >= 1 (TelemetryGate-3 flow evidence).
3. `RD.telemetryGate.SET_TRANSMIT_STATE DISABLED`. Observable: OK <= 10 s; `TransmitStateSet(DISABLED)` <= 5 s at FSW time T.
4. 70 s window: zero channelized telemetry items with FSW timestamp > T + 1 s (CH-L2-15, TelemetryGate-2, TM-L2-09 clause 1). Events and command acks must still arrive (send one `CMD_NO_OP` at 30 s and expect its ack).
5. `SET_TRANSMIT_STATE ENABLED`. Observable: OK <= 10 s; `TransmitStateSet(ENABLED)`; `BootCount` resumes <= 45 s; `GatedTicks` increased by >= 2 vs step 1 and `TransmitState` == ENABLED <= 45 s (CH-L2-16).
6. `RD.lora.TRANSMIT DISABLED` then `ENABLED`. Observable: each acked OK <= 10 s, no LoRa error event <= 5 s (CH-L2-12 lora half; RF silence itself is HP-13).
7. `SET_LEVEL 1`.

## Criteria
| ID | Criterion | Automated | Evidence |
|---|---|---|---|
| CH-L2-12 | telemetryGate.SET_TRANSMIT_STATE and lora.TRANSMIT each ack OK <= 10 s for ENABLED and DISABLED; TransmitStateSet event; no LoRa error | telemetry_gate_test.py::test_02_disable_ceases_then_enable_resumes_telemetry | Steps 3, 5, 6 acks |
| CH-L2-15 | After TransmitStateSet(DISABLED) at T: zero channelized telemetry with FSW time > T + 1 s in the next 70 s (UART) | telemetry_gate_test.py::test_03_disable_takes_effect_within_one_period | Step 4 channel history with FSW times |
| CH-L2-16 | After 70 s DISABLED then ENABLED: GatedTicks +>= 2, TransmitState ENABLED <= 45 s; restart persistence at unit (TelemetryGate-4) | telemetry_gate_test.py::test_04_gated_ticks_count_and_state_after_reenable | Step 5 |
| TM-L2-09 | Clause 1: DISABLED stops the whole channelized stream within one period. Clause 2 (per-subsystem streams) not implemented: README | telemetry_gate_test.py::test_03 (clause 1) | Step 4 |
| TelemetryGate-1 | Command OK; the very next tick is gated/forwarded per new state (no stale tick) | test_TelemetryGate_Component (unit, passing) ; telemetry_gate_test.py::test_02 | Unit log; step 4 first-item FSW time <= T + 1 s |
| TelemetryGate-2 | Zero runOut over N DISABLED ticks (unit); on hardware no telemetry packets after 1 cycle | test_TelemetryGate_Component::DisabledDropsTicksAndCountsThem ; telemetry_gate_test.py::test_02 | Unit log; step 4 |
| TelemetryGate-3 | Exactly one runOut per tick, context unchanged (unit); hardware: telemetry flows when ENABLED | test_TelemetryGate_Component::EnabledForwardsEveryTick ; telemetry_gate_test.py::test_01_telemetry_flows_when_enabled | Unit log; step 2 |

## Why this verifies it
- CH-L2-15 / TelemetryGate-2 / TM-L2-09(1): the requirement is "cease channelized telemetry within one scheduler cycle"; using FSW timestamps (not GDS receipt) separates "produced after T" from "in flight before T". The 70 s window exceeds two packetizer periods, so silence is not a between-runs gap. Events/acks arriving during the window (step 4) prove the scope is channelized telemetry only, not the link.
- CH-L2-16: `GatedTicks` is a counter kept by the gate itself; it is accepted because the requirement is the gate's own state; the independent oracle is the resumed `BootCount` receipt.
- CH-L2-12: acks over UART are the observable for "supports commands"; the RF-side effect belongs to HP-13.
- TelemetryGate-1/3: the observables (next tick, runOut count, unchanged context) exist only at unit level; the board evidence is indirect. README proposes Level Unit for both.

## Known traps
- The gate state is persisted (TelemetryGate-9): a DISABLED left behind survives reboot and every later telemetry group fails. Always run step 5; `ensure_enabled` fixture does this in the automated test.
- `tlmSend` is `Svc.TlmPacketizer` (not TlmChan as the sdd says): packets, not channels, are gated; the Beacon at level 1 is enough to see the effect.
- This group is not `uart_only` (it does not touch RF) but step 6 toggles `lora.TRANSMIT`: run step 6 last and only from UART.
