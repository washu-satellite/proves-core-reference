# Components::TelemetryGate

Passive component that gates scheduled telemetry downlink based on a persisted
enable/disable state. It sits between `telemetryDelay.runOut` and
`CdhCore.tlmSend.Run`: when DISABLED the scheduler tick is not forwarded, so
TlmChan stops running and all channelized telemetry ceases within one scheduler
cycle. Events, command acknowledgements, and file downlink are not gated.

The state persists to a flash file (`/tlm_tx_state.bin`) because NullPrmDb does
not persist fprime parameters. The persisted blob is protected by a magic
prefix and an integrity byte (see `TxStateCodec`); a missing file defaults to
ENABLED silently (first boot), and a corrupt or truncated file defaults to
ENABLED with a `StateFileCorrupt` warning (fail-operational).

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
| Name | Description | Validation |
|---|---|---|
|TelemetryGate-1|SET_TRANSMIT_STATE shall latch the requested state immediately, effective on the next scheduler tick|Unit Test, Integration Test|
|TelemetryGate-2|While DISABLED, the component shall not forward the scheduler tick, ceasing all channelized telemetry within one scheduler cycle|Unit Test, Integration Test|
|TelemetryGate-3|While ENABLED, the component shall forward every scheduler tick unchanged|Unit Test, Integration Test|
|TelemetryGate-4|The transmission state shall persist across reboot via a flash state file|Unit Test|
|TelemetryGate-5|A missing state file shall default to ENABLED without emitting a corruption event|Unit Test|
|TelemetryGate-6|A corrupt or truncated state file (including any single-byte corruption) shall be detected, default the state to ENABLED, and emit StateFileCorrupt exactly once|Unit Test|
|TelemetryGate-7|A failure to persist the state shall emit StateFileWriteFailure and return EXECUTION_ERROR while the in-RAM state change stands|Unit Test|
|TelemetryGate-8|Scheduler ticks gated while DISABLED shall be counted and reported in telemetry|Unit Test|

## Change Log
| Date | Description |
|---| --- |
|Jul 2026| Initial version |
