# Parameter policy — what is settable from the ground, what is a file, what is compiled in

Agreed with Jesse on 2026-09-17 (exposure plan) and 2026-09-19 (coil geometry). This is the standing rule every
plan applies when it decides whether a value becomes an F´ parameter, a command argument, an uplinkable file, or a
compile-time constant. When a plan deviates, it says so in its normative section and names the row here it changes.

## The four bins

| Bin | Use it for | Mechanism | Survives reboot? |
|---|---|---|---|
| **Parameter** | a *number* a CONOPS decision would move, with a named consumer that exists | `param` in the component `.fpp`, `PRM_SET` by command, range-checked with fallback to the compiled default, re-read at boot via `parametersLoaded()` | only after `PRM_SAVE_FILE`, and only the first `PRMDB_NUM_DB_ENTRIES` (25 today) saved ids — see the persistence gate in `ROADMAP.md` |
| **Command argument** | a one-shot choice for this invocation only | command arg (e.g. `STREAM_START(rateHz)` on the wire) | n/a |
| **File** | a *table* or a *behaviour*: gain schedules, soft-iron matrix, activity entry/exit sequences, TLE + epoch, the STM32 image | file uplink to the SD NAND, consumed by name | yes |
| **Compile-time constant** | anything fixed by the board or the framework's static allocation, and hardware constants that will never be set from orbit | `constant` / `static const` / devicetree; capacity constants are audited by `scripts/check_capacity.py` | yes, by reflash only |

Rules of thumb: a parameter without a consumer is dead code a test cannot claim; a parameter that is really a
9 KB table is the wrong tool; each parameter costs two command opcodes (`_PRM_SET`, `_PRM_SAVE`) against
`CMD_DISPATCHER_DISPATCH_TABLE_SIZE`; every value the ground can set needs a range check and a compiled fallback.

## Compile-time by decision (do not expose, even under CONOPS pressure)

| Value | Where | Why fixed | The runtime knob that replaces it |
|---|---|---|---|
| Rate-group rates (50 / 10 / 1 Hz) and membership | `topology.fpp`, `instances.fpp` | F´ rate groups are static; there is no runtime move | TaskGate `ENABLE_TASK`/`DISABLE_TASK` issued from an activity's sequence file (on/off per task); `COLLECTION_INTERVAL_S` per source (slower, whole seconds) |
| Buffer and queue sizes, BufferManager pools, ComQueue depths | `project/config/*`, `instances.fpp` | allocated once at boot, library `setup()` guarded | none; A8's recorder owns its own ring with a parameterised retention |
| Bus and pin assignments, baud rates | devicetree overlays, `Main.cpp` | hardware | none |
| Packet layouts (which channel in which packet) | `ReferenceDeploymentPackets.fppi` | packetizer table is static | packet section levels (`SET_LEVEL`) and the telemetry divider control *volume* |
| Sequence-number window (authenticated uplink) | `TcSecurityDeframer` config | security property, not a tuning knob | none |
| Capacity constants (`MAX_PACKETIZER_*`, dispatch table, `PRMDB_NUM_DB_ENTRIES`, port arrays, `MAX_FAULT_TYPE`) | `project/config`, component `.fpp` | static tables | none; raised by a cycle when the audit warns |
| Fault action map (type → action) | `FaultManager` `defaultPolicy` | opcode budget; a later row adds a PAYLOAD_ABORT action (force activity NONE), no mode overlay | debounce per type and `AUTHORITY_MASK` are parameters |
| **Coil geometry** (the ~29 per-coil area / turns / resistance values on DetumbleManager, of its 34 parameters) | `DetumbleManager` — **decision 2026-09-19: move to compile-time constants** | hardware constants that consumed a third of the saved-parameter budget and will never be set from orbit | B-dot gain, deadband, torque duration, cooldown and threshold stay parameters |
| Board-side allowed stream rates (5 / 10 / 20 / 50 Hz) | STM32 firmware (A1) | fixed timer options in version 1 | `STREAM_START(rateHz)` picks one; the host-side default rate is a parameter |

## Parameters (exists = in the dictionary at 6b22f72d; planned = named consumer on the roadmap)

| Level | Parameter | Consumer | State |
|---|---|---|---|
| all | B-dot gain, deadband, torque duration, cooldown, threshold | DetumbleManager | exists |
| all | safe-mode entry / recovery voltage, debounce, command-loss time | ModeManager, FaultManager | exists |
| all | fault debounce per type, `AUTHORITY_MASK` | FaultManager | exists |
| all | LoRa SF / CR / BW | lora | exists (C-30: set SF9 before range use) |
| all | telemetry divider, packet levels, four `COLLECTION_INTERVAL_S` | tlmSend, telemetryDelay, sensor managers | exists |
| all | thermal thresholds, IMU output data rates, light-sensor gain / integration, antenna and burnwire timings | respective managers | exists |
| minimum | `PULSE_DURATION_MS`, `PULSE_DUTY_PCT`, `PULSE_CHANNEL_MASK`, `LINK_TIMEOUT_MS`, `HK_INTERVAL_S` | DriverBoardHandler | exists (Cycle E) |
| minimum | burst sample rate and window | BurstCapture (A9) | planned; defaults owed (10 Hz, 60 s if none) |
| full | burst trigger source (command-only vs detumble events) | BurstCapture (A9) | planned; default command-only |
| full | recorder retention, flush interval, segment size | DataRecorder (A8) | planned |
| full | radio transmit power | lora (library; compile-time today) | decision owed (C-29) |
| full | magnetometer hard-iron offset (3 floats) | ImuManager | planned; parameter vs calibration file owed |
| extended | control-loop rate, controller enable | DriverBoardHandler / STM32 | planned |
| extended | *(none — activities carry no parameters)* the activity axis is data in sequence files: `activity_<x>_enter/exit.seq` per activity; decided 2026-09-19, brief in `design/activity-axis.md` | ActivityManager (new, separate from ModeManager) | staged for a separate implementer |
| extended | fault action PAYLOAD_ABORT (= force activity NONE); FaultManager reads the activity through a get port | FaultManager | later row, after the activity axis |

## Files, not parameters

LQR gain schedule (Q17), soft-iron matrix, **activity entry / exit sequences** (`sequences/activity_*.seq`: rail, ping,
B-dot stand-down, ARM and their reverse — the whole CONOPS switching lives here), TLE + epoch (Q16), STM32 firmware
image (roadmap 5b). `SystemMode` stays SAFE_MODE / NORMAL only; no CONOPS mode is ever added to it.

## Maintenance

A cycle that adds, removes or re-bins a value edits this file in its docs commit. The dictionary is the inventory;
this file is the policy. Per-component counts at 6b22f72d: detumbleManager 34, startupManager 6, modeManager 5,
imuManager 5, thermalManager 5, driverBoardHandler 5, lora 4, faultManager 4, tcSecurityDeframer 4, seven
light-sensor managers 3 each, antennaDeployer 3, the rest 1–2; total 106 against 25 savable.
