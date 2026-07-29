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
### Summary for orbit and ground integration

The antenna does not deploy simply because the power RBF/ABF pin is removed or because the deployment switches are released. Removing the power inhibit allows the spacecraft to boot. Antenna deployment is commanded later by the startup command sequence.

The checked-in startup sequence source is `sequences/startup.seq`. The spacecraft runs the compiled and uploaded file `/seq/startup.bin`. For flight compliance and ground integration planning, verify that the uploaded `/seq/startup.bin` was generated from the same `sequences/startup.seq` source being reviewed. If the uploaded sequence differs, the uploaded sequence is the operational authority.

With the current checked-in sequence, antenna deployment is **not** one minute after power-on. It occurs about **45 minutes 10 seconds after the startup sequence begins**, plus boot/startup latency.

### Nominal in-orbit sequence after separation

This timeline assumes the spacecraft is released from the deployer, the deployment switches close, the power RBF/ABF inhibit has been removed, `/seq/startup.bin` is present, and `/seq/startup.bin` matches the current `sequences/startup.seq`.

| Approx. time from power available | Approx. time from startup sequence start | Event | Result |
|---|---|---|---|
| 0 seconds | N/A | Flight power becomes available after inhibits are removed and deployment switches allow power | Flight computer begins booting. There is no software delay before boot. |
| ~3 seconds | N/A | `Main.cpp` boot delay completes | The intentional 3-second delay allows USB CDC ACM initialization before topology startup. |
| ~3-4 seconds | 0 seconds | `StartupManager` dispatches `/seq/startup.bin` on its first 1 Hz run | The startup command sequence begins. |
| ~13-14 seconds | 0:10 | Event filters are configured | No antenna deployment and no RF transmit. |
| ~45 min 13-15 sec | 45:10 | `ReferenceDeployment.antennaDeployer.DEPLOY` executes | Antenna deployment procedure starts. |
| ~45 min 13-15 sec | 45:10 | LoRa parameters are configured and `ReferenceDeployment.lora.TRANSMIT ENABLED` executes | LoRa transmit is allowed. The antenna may begin radiating as soon as an outbound packet reaches the LoRa driver. |
| ~45 min 13-15 sec | 45:10 | `ReferenceDeployment.modeManager.EXIT_SAFE_MODE` executes | Mode is set to normal. In the current topology, the ModeManager load-switch turn-on ports are not connected. |
| ~50 min 13-15 sec | 50:10 | `ReferenceDeployment.detumbleManager.SET_MODE, AUTO` executes | Automatic detumbling is enabled. Magnetorquer outputs may be driven when the detumble state machine determines actuation is needed. |
| ~3 hr 50 min 13-15 sec | 3:50:10 | `ReferenceDeployment.detumbleManager.SET_MODE, DISABLED` executes | Automatic detumbling is disabled and magnetorquers are forced off. |

The 5-minute delay in the current startup sequence is the wait between `EXIT_SAFE_MODE` and enabling automatic detumbling. It does not deploy the antenna and it does not enable the radio.

If mission requirements or integration procedures refer to antenna deployment one minute after power-on, then the reviewed source and the expected operational sequence are inconsistent. Either the uploaded `/seq/startup.bin` differs from this source, or the sequence source should be updated and regenerated with `make sequence SEQ=startup`.

### Antenna physical deployment

The startup sequence command that starts deployment is:

```text
R00:45:00 ReferenceDeployment.antennaDeployer.DEPLOY
```

When `DEPLOY` is accepted:

1. The component checks the persistent deployment-state file at `//antenna/antenna_deployer.bin`.
2. If the state file says the antenna is already deployed, the component logs `DeploymentAlreadyComplete`, does not start the burnwire, and returns success to the command sequencer.
3. If the antenna is not marked deployed and the deployer is idle, the component starts burn attempt 1.
4. The AntennaDeployer sends `burnStart` to the Burnwire component.
5. The Burnwire component drives both burnwire GPIOs high on its next 1 Hz scheduler tick.
6. The burnwire heats the physical restraint. The antenna is physically deployed when the restraint releases.
7. The AntennaDeployer commands `burnStop` after the configured burn duration.
8. If retries remain, the AntennaDeployer waits the configured retry delay, then starts the next burn attempt.

Default deployment timing after `DEPLOY`:

| Time after `DEPLOY` | Action |
|---|---|
| 0 seconds | First burn attempt is requested. |
| 0-1 second | Burnwire GPIOs are commanded high on the next 1 Hz Burnwire scheduler tick. |
| ~8 seconds | First burn attempt is stopped by AntennaDeployer. |
| ~38 seconds | Second burn attempt starts if another attempt is needed. |
| ~46 seconds | Second burn attempt stops. |
| ~76 seconds | Third burn attempt starts if another attempt is needed. |
| ~84 seconds | Third burn attempt stops and the deployment procedure finishes. |

The antenna is physically deployed when the burnwire releases the restraint. This may occur during the first 8-second burn window or during a later retry. The software does not have a deployment switch or other sensor that verifies physical antenna motion.

The current implementation marks the antenna state file as deployed when the deployment procedure finishes with either `DEPLOY_RESULT_SUCCESS` or `DEPLOY_RESULT_FAILED`. Because there is no physical deployment sensor, ground or integration testing must verify mechanical release by observation or external instrumentation.

### Antenna radiation timing

The LoRa radio is initialized at boot with transmit disabled:

```cpp
lora.start(state.loraDevice, Zephyr::TransmitState::DISABLED);
```

The current startup sequence enables LoRa transmit immediately after the `DEPLOY` command is accepted:

```text
R00:00:00 ReferenceDeployment.lora.TRANSMIT, DISABLED
R00:00:00 ReferenceDeployment.lora.DATA_RATE_PRM_SET, SF_8
R00:00:00 ReferenceDeployment.downlinkDelay.DIVIDER_PRM_SET, 299
R00:00:00 ReferenceDeployment.telemetryDelay.DIVIDER_PRM_SET, 29
R00:00:00 ReferenceDeployment.lora.CODING_RATE_PRM_SET, CR_4_5
R00:00:00 ReferenceDeployment.lora.BANDWIDTH_RX_PRM_SET, BW_125_KHZ
R00:00:00 ReferenceDeployment.lora.BANDWIDTH_TX_PRM_SET, BW_125_KHZ
R00:00:00 ReferenceDeployment.lora.TRANSMIT ENABLED
```

`DEPLOY` does not block until all burn attempts finish. It returns after starting the deployment state machine. Therefore, with the current sequence, LoRa transmit may be enabled while the burnwire deployment procedure is still in progress.

In the current sequence, RF transmit is enabled at approximately the same sequence time as `DEPLOY`: about **45 minutes 10 seconds after startup sequence start**, plus boot/startup latency. The antenna may begin radiating as soon as both conditions are true:

1. `ReferenceDeployment.lora.TRANSMIT ENABLED` has been processed.
2. An outbound packet reaches the LoRa driver.

If mission or integration rules require no RF until after physical antenna release, move `TRANSMIT ENABLED` later in `sequences/startup.seq`. Waiting at least 84 seconds after `DEPLOY` covers the default worst-case burn attempt window, but physical release is still not sensed by software.

### Ground integration and how to stop deployment

For ground integration, do not assume the spacecraft is inactive after the RBF/ABF is removed. If the deployment switches are decompressed and power is available, the flight computer will boot immediately and may start `/seq/startup.bin`.

Recommended ground-safe approaches:

| Goal | Recommended approach | Notes |
|---|---|---|
| Prevent automatic antenna deployment during integration | Use a ground-safe startup sequence that omits `ReferenceDeployment.antennaDeployer.DEPLOY`, or do not install `/seq/startup.bin` during integration | This prevents the command sequencer from reaching the deploy command. |
| Keep the flight startup sequence but skip burnwire activation | Command `ReferenceDeployment.antennaDeployer.SET_DEPLOYMENT_STATE, true` before running the sequence | This makes later `DEPLOY` commands log `DeploymentAlreadyComplete` and skip burn attempts. Use only when appropriate for the test. |
| Stop an active antenna deployment state machine | Command `ReferenceDeployment.antennaDeployer.DEPLOY_STOP` | This is the primary software stop. It aborts the AntennaDeployer procedure and sends `burnStop` to the Burnwire component. |
| Force burnwire GPIOs off | Command `ReferenceDeployment.burnwire.STOP_BURNWIRE` | This directly drives burnwire GPIOs low, but it does not stop the AntennaDeployer state machine. Follow with `DEPLOY_STOP` or the deployer may retry later. |
| Stop future LoRa radiation | Command `ReferenceDeployment.lora.TRANSMIT, DISABLED`, or use a ground-safe startup sequence that never enables transmit | If the still-running sequence later executes `TRANSMIT ENABLED`, transmit will be enabled again. |
| Stop all software activity | Remove power through the verified hardware inhibit path | This is a hardware action, not a software stop. See the RBF/ABF note below. |

RBF/ABF and killswitch behavior is hardware-defined. The software repository does not prove the electrical path for the RBF/ABF pin or deployment switches. If replacing the RBF/ABF pin removes power from both the flight computer and the burnwire power path, then replacing it should stop burnwire heating by removing power. If it does not remove burnwire power, software cannot guarantee that replacing the RBF/ABF stops an active burn.

Replacing the RBF/ABF during an active burn does not let the software run `DEPLOY_STOP`, write a clean abort state, or log final events unless the flight computer remains powered long enough to process those actions. If power is removed before the deployment state file is marked deployed, a later reboot may allow the startup sequence to attempt deployment again.

The safest ground-integration configuration is:

1. Keep the RBF/ABF installed until the spacecraft is mechanically safe to power.
2. Use a ground-safe `/seq/startup.bin` with no antenna deploy and no RF enable, or remove `/seq/startup.bin`.
3. If the spacecraft must run the flight sequence on the ground, start a timer at power-on and keep the vehicle in a safe mechanical/RF configuration before the configured `DEPLOY` time.
4. Verify by hardware test whether replacing the RBF/ABF removes burnwire power.

### Timing policy

Current checked-in timing already gives about 45 minutes before antenna deployment, not one minute. If compliance or integration planning assumes only one minute, reconcile the flight sequence artifact before environmental or deployment testing.

If there is no mission requirement for a short delay, integration can be made easier by extending the relative delay before `ReferenceDeployment.antennaDeployer.DEPLOY` in `sequences/startup.seq`, then regenerating the binary with:

```bash
make sequence SEQ=startup
```

Any change to deployment delay or RF enable delay must be reviewed against orbital compliance requirements, because `TRANSMIT ENABLED` currently occurs immediately after `DEPLOY` is accepted.

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
