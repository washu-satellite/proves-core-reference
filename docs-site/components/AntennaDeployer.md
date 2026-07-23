# Components::AntennaDeployer

Component that deploys the antenna and activates the burnwire


## Requirements
Add requirements in the chart below

| Name | Description | Validation |
|---|---|---|
|AD0001|The Antenna Deployer shall attempt to redeploy the burnwire if the armed parameter is set| Unit Testing|
|AD0002|The antenna deployer shall attempt to deploy| Unit Testing|
|AD0003|The Antenna Deployer shall broadcast an event every time it tries to deploy | Unit Testing|
|AD0004|The Antenna Deployer shall broadcast an event when it successfully deploys | Unit Testing|
|AD0005|The Antenna Deployer shall carry a count of the amount of times it has tried to deploy attached to the Telemetry | Unit Testing|


## Usage Examples
### Startup sequence timing

The reference deployment does not deploy the antenna simply because power inhibits are removed. Antenna deployment is commanded by the startup command sequence. `StartupManager` dispatches `/seq/startup.bin` on its first 1 Hz run after boot, and `sequences/startup.seq` is the source file for that binary. If `/seq/startup.bin` is not present, or if a different startup sequence is installed, these timings do not apply.

Assuming a normal boot after power-inhibit removal and the current `sequences/startup.seq`, add the boot/startup latency to these sequence-relative times. `Main.cpp` intentionally waits 3 seconds before topology startup to allow USB CDC ACM initialization, and `StartupManager` then starts the sequence on a 1 Hz scheduler call.

| Approx. time from startup sequence start | Command/action | What turns on |
|---|---|---|
| 0:10 | Event filters are configured | No antenna deployment and no RF transmit |
| 45:10 | `ReferenceDeployment.antennaDeployer.DEPLOY` | Antenna burnwire deployment procedure starts |
| 45:10 | LoRa parameters are configured and `ReferenceDeployment.lora.TRANSMIT ENABLED` runs | LoRa transmit is allowed and may radiate as soon as an outbound packet reaches the LoRa driver |
| 45:10 | `ReferenceDeployment.modeManager.EXIT_SAFE_MODE` | Mode is set to normal; in the current topology the ModeManager load-switch turn-on ports are not connected |
| 50:10 | `ReferenceDeployment.detumbleManager.SET_MODE, AUTO` | Automatic detumbling is enabled; this may drive the magnetorquer outputs when the detumble state machine determines actuation is needed |
| 3:50:10 | `ReferenceDeployment.detumbleManager.SET_MODE, DISABLED` | Automatic detumbling is disabled and magnetorquers are forced off |

The 5-minute delay in the current startup sequence is the wait between `EXIT_SAFE_MODE` and enabling automatic detumbling. It does not deploy the antenna and it does not enable the radio.

### Physical deployment

```text
R00:45:00 ReferenceDeployment.antennaDeployer.DEPLOY
```

`DEPLOY` starts the deployer state machine and returns immediately after the first burn attempt is requested. The physical release is driven by the burnwire GPIOs. With default parameters, each burn attempt is held for 8 seconds, then the component waits 30 seconds before retrying, for up to 3 attempts. Therefore, after the `DEPLOY` command is accepted:

| Time after `DEPLOY` | Action |
|---|---|
| 0 seconds | First burn attempt is requested; burnwire GPIOs are commanded on at the next 1 Hz burnwire scheduler tick |
| ~8 seconds | First burn attempt stops |
| ~38 seconds | Second burn attempt starts if another attempt is needed |
| ~46 seconds | Second burn attempt stops |
| ~76 seconds | Third burn attempt starts if another attempt is needed |
| ~84 seconds | Third burn attempt stops and deployment procedure finishes |

The antenna is physically released when the burnwire succeeds in releasing the restraint. That may happen during the first 8-second burn window, or during a later retry if the first attempt does not release it. Software does not sense a deployment switch; after exhausting the configured attempts, the current implementation marks the antenna state as deployed even if the final result is `DEPLOY_RESULT_FAILED`, so mechanical verification must come from test observation or external instrumentation.

If a test changes the sequence so that `DEPLOY` occurs 5 minutes after power-inhibit removal, the antenna could physically release shortly after that 5-minute point. Integration procedures should have the vehicle in the deployer before the configured `DEPLOY` time.

If the persistent deployed flag is already set, `DEPLOY` logs `DeploymentAlreadyComplete`, no burn attempt runs, and the startup sequence continues to the radio commands.

### RF transmit timing

The LoRa radio is initialized at boot with transmit disabled:

```cpp
lora.start(state.loraDevice, Zephyr::TransmitState::DISABLED);
```

In `sequences/startup.seq`, LoRa transmit is disabled/configured and then enabled immediately after the `DEPLOY` command is accepted:

```text
R00:00:00 ReferenceDeployment.lora.TRANSMIT, DISABLED
...
R00:00:00 ReferenceDeployment.lora.TRANSMIT ENABLED
```

Because `DEPLOY` does not block until the burn attempts finish, LoRa may begin radiating queued downlink as soon as `TRANSMIT ENABLED` is processed and a packet is available. In the current sequence this is approximately 45 minutes 10 seconds after startup sequence start, plus boot/startup latency, not 5 minutes after power-inhibit removal.

If integration handling requires no RF before mechanical release, move `TRANSMIT ENABLED` later in `sequences/startup.seq`. Waiting at least 84 seconds after `DEPLOY` covers the default worst-case deployment attempt window.

### Diagrams
Add diagrams here

### Typical Usage
And the typical usage of the component here

## Class Diagram
Add a class diagram here

## Port Descriptions
| Name | Type | Description |
|------|------| ----------- |
|schedIn|Svc.Sched|Port receiving calls from the rate group|
|burnStart|Fw.Signal|Port signaling the burnwire component to start heating|
|burnStop|Fw.Signal|Port signaling the burnwire component to stop heating|

## Component States
Add component states in the chart below

| Name | Description |
|------|-------------|
|deploy_count|Keeps track of how many deploys happened |

## Sequence Diagrams
Add sequence diagrams here

## Parameters
| Name | Type | Default | Description |
|------|------|---------|-------------|
|RETRY_DELAY_SEC|U32|30|Delay (seconds) between burn attempts|
|MAX_DEPLOY_ATTEMPTS|U32|3|Maximum number of burn attempts before giving up|
|BURN_DURATION_SEC|U32|8|Duration (seconds) for which to hold each burn attempt before issuing STOP|
|DEPLOYED_STATE_FILE|string|"//antenna/antenna_deployer.bin"|File path for persistent deployment state (file exists = deployed)|


## Commands
| Name | Description |
| ---- | -----------  |
|DEPLOY|Starts deployment procedure|
|DEPLOY_STOP|Stops deployment procedure|


## Events
| Name | Description |
|------|------------|
|DeployAttempt|Emitted at the start of each deployment attempt|
|DeploySuccess|Emitted when the antenna deployment is considered successful|
|DeployFinish|Emitted when the deployment procedure finishes|


## Telemetry

| Name | Description |
|------|-------------|
|DeployCount|Reports the amount of time the antenna has tried to deploy|

## Unit Tests
Add unit test descriptions in the chart below

| Name | Description | Output | Coverage |
|---|---|---|---|


## Change Log

| Date | Description |
|------|-------------|
| TBD | Initial Draft |
