# Components::TelemetryGate

Passive component that gates scheduled telemetry downlink based on a persisted
enable/disable state. It sits between `telemetryDelay.runOut` and
`CdhCore.tlmSend.Run`: when DISABLED the scheduler tick is not forwarded, so
TlmChan stops running and all channelized telemetry ceases within one scheduler
cycle. Events, command acknowledgements, and file downlink are not gated.

The state persists to a flash file (`/tlm_tx_state.bin`) rather than as an F´
parameter so that it is CRC-protected and replaced atomically (parameters saved
with `PRM_SAVE_FILE` do persist, but `/prmDb.dat` has no integrity check). The record is written through the shared
`Components::PersistedRecord` mechanism: record-type magic `TGS2`, a one-byte
payload holding the `TelemetryTxState` ordinal, and a CRC-32 over magic,
version and payload. Updates are atomic — the record is written and flushed to
`/tlm_tx_state.tmp` and then renamed over the target — so a failure at any step
leaves the previously persisted state intact. A missing file defaults to
ENABLED silently (first boot), and a corrupt, truncated, wrong-version or
unreadable file defaults to ENABLED with a `StateFileCorrupt` warning
(fail-operational). A legacy 6-byte `TGS1` blob left by an older image is
shorter than the record overhead and is therefore reported as corrupt rather
than migrated; the next `SET_TRANSMIT_STATE` rewrites it in the new format.

## Usage Examples

Disable all channelized telemetry (events and command acks keep flowing):

```
ReferenceDeployment.telemetryGate.SET_TRANSMIT_STATE DISABLED
```

Re-enable:

```
ReferenceDeployment.telemetryGate.SET_TRANSMIT_STATE ENABLED
```

## Port Descriptions
| Name | Description |
|---|---|
|runIn|Rate schedule tick input (from telemetryDelay.runOut)|
|runOut|Rate schedule tick output (to CdhCore.tlmSend.Run); only forwarded while ENABLED|

## Commands
| Name | Description |
|---|---|
|SET_TRANSMIT_STATE|Set the telemetry transmission enable/disable state. Latches immediately (effective on the next scheduler tick) and persists to flash.|

## Telemetry
| Name | Description |
|---|---|
|TransmitState|Current telemetry transmission state|
|GatedTicks|Number of scheduler ticks gated (not forwarded) while disabled|

Note: while DISABLED, TlmChan does not run, so these channels do not downlink;
operators confirm gating through the `TransmitStateSet` event, which is not
gated.

## Events
| Name | Description |
|---|---|
|TransmitStateSet|Emitted when the telemetry transmission state is set|
|StateFileWriteFailure|Emitted when persisting the state to flash fails; the in-RAM state change still stands|
|StateFileCorrupt|Emitted when the persisted state file is corrupt; state defaults to ENABLED (fail-operational)|

## Requirements

Pass criteria are decided before testing; edit with `scripts/req.py`, not by hand.

| Name | Description | Method | Level | Pass Criteria | Status | Reason |
|---|---|---|---|---|---|---|
|TelemetryGate-1|SET_TRANSMIT_STATE shall latch the requested state immediately, effective on the next scheduler tick|Unit Test, Integration Test|Board|Command returns OK; the tick immediately following the command is gated/forwarded per the new state (no stale tick)|||
|TelemetryGate-2|While DISABLED, the component shall not forward the scheduler tick, ceasing all channelized telemetry within one scheduler cycle|Unit Test, Integration Test|Board|Zero runOut calls over N consecutive DISABLED ticks; on hardware, no telemetry packets received after 1 cycle|||
|TelemetryGate-3|While ENABLED, the component shall forward every scheduler tick unchanged|Unit Test, Integration Test|Board|Exactly one runOut call per tick with the context value unchanged|||
|TelemetryGate-4|The transmission state shall persist across reboot via a flash state file|Unit Test|Unit|A new component instance reading the same file restores the commanded state (DISABLED round-trips)|||
|TelemetryGate-5|A missing state file shall default to ENABLED without emitting a corruption event|Unit Test|Unit|State is ENABLED and zero StateFileCorrupt events with no file present|||
|TelemetryGate-6|A corrupt or truncated state file (including any single-byte corruption) shall be detected, default the state to ENABLED, and emit StateFileCorrupt exactly once|Unit Test|Unit|For every single-byte corruption and every truncation length: state ENABLED, exactly 1 StateFileCorrupt event|||
|TelemetryGate-7|A failure to persist the state shall emit StateFileWriteFailure and return EXECUTION_ERROR while the in-RAM state change stands|Unit Test|Unit|On injected open/write/partial-write failure: 1 StateFileWriteFailure event, EXECUTION_ERROR response, gating follows the newly commanded state|||
|TelemetryGate-8|Scheduler ticks gated while DISABLED shall be counted and reported in telemetry|Unit Test|Unit|GatedTicks telemetry equals the exact number of dropped ticks|||
|TelemetryGate-9|TelemetryGate shall persist its transmit state via the shared PersistedRecord mechanism, replacing the bespoke TxStateCodec, with TelemetryGate-4 through TelemetryGate-7 behavior unchanged|Unit Test|Unit|TelemetryGate-4 through TelemetryGate-7 unit tests pass against the shared codec; a legacy 6-byte TGS1-format file is handled per TelemetryGate-6 (state ENABLED, one StateFileCorrupt event) or migrated losslessly|||

## Change Log
| Date | Description |
|---| --- |
|Jul 2026| Initial version |
|Sep 2026|Migrated persistence to the shared PersistedRecord codec and atomic store; retired TxStateCodec (TelemetryGate-9)|
