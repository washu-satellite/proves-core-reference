# Components::TaskGate

Passive component providing the generic `ENABLE_TASK(task)` /
`DISABLE_TASK(task)` pair of SC-L2-07. It is interposed between
`rateGroup1Hz.RateGroupMemberOut` and five stateless sensor-poll components,
one port pair per task. A tick for a disabled task is dropped and counted; a
tick for an enabled task is forwarded unchanged, with its call order intact.

One gate component was chosen over per-component `ENABLED` parameters: it is
one dictionary change, one test binary, one host stub, and it matches the CDR's
"taskId, enabled tags, ENABLE_TASK(taskId)/DISABLE_TASK(taskId)" wording.

Both commands are `sync` on a passive component, so the new state is latched in
the calling thread before the next 1 Hz tick — "effect within one cycle". State
is RAM-only and deliberately so: every task is enabled after a reset, which is
the safe direction, and no gate can silently survive a reboot.

## Gated tasks

The `SchedTask` enum ordinal **is** the `schedIn`/`schedOut` port index, so the
enum and the port arrays cannot drift apart.

| SchedTask | Port index | 1 Hz member slot | Destination |
|---|---|---|---|
| IMU | 0 | `RateGroupMemberOut[6]` | `imuManager.run` |
| POWER_MONITOR | 1 | `RateGroupMemberOut[15]` | `powerMonitor.run` |
| ADCS | 2 | `RateGroupMemberOut[17]` | `adcs.run` |
| THERMAL | 3 | `RateGroupMemberOut[18]` | `thermalManager.run` |
| FS_SPACE | 4 | `RateGroupMemberOut[10]` | `fsSpace.run` |

All five are pure sensor-poll / telemetry handlers with no actuator state
(`ImuManager.cpp:43-55`, `PowerMonitor.cpp:28-38`, `ADCS.cpp:24-31`,
`ThermalManager.cpp:24-`, `FsSpace.cpp:27-35`). Gating one stops its channels
updating and nothing else: `DetumbleManager` reads the IMU through its own
ports (`topology.fpp:352-354`), not via `imuManager.run`, and `ModeManager`
samples battery voltage directly (`topology.fpp:458`).

## Tasks deliberately NOT gateable

`SchedTask` has exactly the five members above, so commanding any other task is
a **compile-time impossibility** — there is no runtime rejection path to test,
and no opcode a ground operator could send.

| Not gated | Why |
|---|---|
| `watchdog.run` | Has its own START/STOP; gating it is a reboot in ~26 s (`Watchdog.cpp:25-35,69-75`) |
| `modeManager.run` | Safety authority; must keep running in every mode |
| `startupManager.run` | Boot sequencing |
| `burnwire.schedIn`, `antennaDeployer.schedIn` | Timed burns — a gate could leave a burn energised |
| `detumbleManager.run` | 50 Hz actuator control loop |
| `telemetryDelay.runIn` | Already gated by `TelemetryGate` (SET_TRANSMIT_STATE) |
| `comQueue.run`, `commsBufferManager.schedIn`, `authenticationRouter.run`, `$health.Run`, `fileDownlink.Run`, `payloadBufferManager.schedIn` | Comms and health plumbing; gating loses the command path itself |
| every 10 Hz member | Drivers, sequencers, aggregator |

## Commands

| Command | Kind | Description |
|---|---|---|
| ENABLE_TASK(task: SchedTask) | sync | Re-enable a gated task; idempotent, always responds OK |
| DISABLE_TASK(task: SchedTask) | sync | Stop a task being scheduled; idempotent, always responds OK |

## Telemetry

| Channel | Type | Description |
|---|---|---|
| TasksEnabledMask | U32 | Bit i set when task i is enabled. Default 0x1F (all five enabled) |
| GatedRuns | U32 | Cumulative count of ticks dropped across all tasks since boot |

Both channels are carried in the `HealthAuxiliary` packet (id 4, group 5). The
boot values (mask 0x1F, GatedRuns 0) are published once on the first tick, so
ground can read the default without first sending a command.

## Events

| Event | Severity | Description |
|---|---|---|
| TaskEnabled(task) | activity high | A task was enabled |
| TaskDisabled(task) | activity high | A task was disabled |

## Inspection records

| ID | Clause | Evidence | Date |
|---|---|---|---|
| SC-L2-06 | Rate-group thread priorities are ordered rateGroup50Hz(1) < rateGroup10Hz(2) < rateGroup1Hz(3) | `ReferenceDeployment/Top/instances.fpp:31-44` — `priority 1`, `priority 2`, `priority 3` on the three `Svc.ActiveRateGroup` instances | 2026-09-05 |
| SC-L2-06 | modeManager(4) <= tlmSend(6) | `instances.fpp:46-49` modeManager `priority 4`; `project/config/CdhCoreConfig.fpp:19-25` `tlmSend = 6` (also cmdDisp 4, $health 5, events 6) | 2026-09-05 |
| SC-L2-06 | Lower number == higher priority, as the criterion assumes | `lib/fprime-zephyr/fprime-zephyr/Os/Task.cpp:36-48` passes `arguments.m_priority` straight to `k_thread_create`, capping it at 14 (Zephyr preemptible priorities 0..14, lower runs first) | 2026-09-05 |

Supporting: `project/config/ComCcsdsConfig.fpp:18-21` puts aggregator at 7 and
comQueue at 8, i.e. below every rate group and below modeManager, so no comms
thread can preempt the scheduler.

## Requirements

Pass criteria are decided before testing; edit with `scripts/req.py`, not by hand.

| Name | Description | Method | Level | Pass Criteria | Status | Reason |
|---|---|---|---|---|---|---|
|TaskGate-1|By default every task shall be enabled and every scheduler tick forwarded on its matching output port|Unit Test|Unit|One tick into each of the 5 ports yields exactly 5 schedOut calls, port index preserved and context unchanged; TasksEnabledMask reads 0x1F|||
|TaskGate-2|DISABLE_TASK(task) shall stop that task being scheduled from the very next tick, affecting no other task|Unit Test|Unit|After DISABLE_TASK: zero schedOut calls on that port and one per tick on every other port; response OK; exactly one TaskDisabled event naming that task|||
|TaskGate-3|ENABLE_TASK(task) shall restore forwarding on that task's port from the very next tick|Unit Test|Unit|After ENABLE_TASK on a gated task: the next tick is forwarded on that port; exactly one TaskEnabled event; TasksEnabledMask returns to 0x1F|||
|TaskGate-4|GatedRuns shall equal the exact number of dropped ticks and TasksEnabledMask shall reflect every command|Unit Test|Unit|With 2 of 5 tasks gated over 3 cycles: GatedRuns is 6 and schedOut was called 9 times; the mask reads 0x1B, 0x0B and 0x0F after the successive commands|||
|TaskGate-5|ENABLE_TASK and DISABLE_TASK shall be idempotent and always return OK|Unit Test|Unit|Enabling an already-enabled task and disabling an already-disabled task both respond OK and leave the mask and gating behaviour consistent with the requested state|||

## Change Log
| Date | Description |
|---| --- |
|Sep 2026| Initial version (SC-L2-07 generic ENABLE_TASK/DISABLE_TASK; SC-L2-06 inspection record) |
