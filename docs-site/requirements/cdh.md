# CDH System Requirements

System-level Command and Data Handling requirements, transcribed from the
SCALAR CDH Critical Design Review (CDR) deck. L1 requirements (CDH-*) are the
subsystem-level requirements; each L2 group decomposes them for a CDH
sub-module.

Every requirement carries a **Method** (how it is verified: Unit Test,
Integration Test, Analysis, Inspection, Demonstration), a **Level** (where it
is proven: Unit, Board, Subsystem, Flatsat, Environmental), **Pass Criteria**
(the measurable pass/fail condition — must be decided *before* testing; the
traceability matrix flags any requirement tested without criteria), a
**Status**, and a **Reason** for failures. Status currently holds the
implementation assessment presented at CDR (Met / Partial / Not met); once a
test declares `verifies("<ID>")`, the matrix shows the automated result
instead.

Do not hand-edit these tables — use the terminal tool:

```
scripts/req.py list --group CDH          # see everything
scripts/req.py show CH-L2-15             # one requirement in full
scripts/req.py set CH-L2-15 --criteria "..." --level Flatsat
scripts/req.py add --group "CDH L1 Requirements" --id CDH-32 --description "..."
make rtm                                 # regenerate the matrix
```

These tables are parsed by `scripts/generate_rtm.py` and appear in the
[Requirements Matrix](../requirements-matrix.md).

## CDH L1 Requirements

| Name | Description | Method | Level | Pass Criteria | Status | Reason |
|---|---|---|---|---|---|---|
|CDH-1|EGSE shall be capable of command and control of the satellite without using the radio.|Demonstration|Board|Over UART with lora.TRANSMIT DISABLED: CMD_NO_OP acked OpCodeCompleted within 10 s and startupManager.BootCount received within 45 s|||
|CDH-2|EGSE shall be capable of satellite thermal monitoring.|Demonstration|Board|Via GDS: tmp112Face0Manager.GetTemperature returns Temperature within 5 s and picoTempManager.GetPicoTemperature returns PicoTemperature within 5 s, both in -40..125 C|||
|CDH-3|Shall be capable of ceasing transmission on command.|Integration Test|Board|Radio link: after lora.TRANSMIT DISABLED ack, zero packets of any type (telemetry, events, acks) received over RF for 70 s; after TRANSMIT ENABLED an event or telemetry item arrives within 45 s|||
|CDH-4|The system shall be capable of monitoring thermal data.|Integration Test|Board|With face0 switch ON and tlmSend level 5: tmp112Face0Manager.Temperature and picoTempManager.PicoTemperature each update at least once per 45 s with values in -40..125 C|||
|CDH-5|The system shall be capable of collecting ADCS telemetry at a configurable interval.|Integration Test|Board|After a collection-interval parameter is set to N s (1..60), imuManager.MagneticField updates are spaced N +/-1 s over 5 updates [no such parameter exists: TM-L2-02 Not met]|||
|CDH-6|The system shall be capable of monitoring magnetometer data.|Integration Test|Board|imuManager.GET_MAGNETIC_FIELD returns MagneticFieldData within 3 s with at least one non-zero axis; MagneticField channel updates at least once per 45 s at tlmSend level 5|||
|CDH-7|The system may be capable of measuring coarse attitude using photodiodes.|Demonstration|Board|[may] With one face illuminated, that face's VisibleLight channel exceeds every other face by [TBD by Mission Ops: illumination ratio] within 2 s (adcs.visibleLightGet[6])|||
|CDH-8|The system shall be capable of buffering data for later transmission.|Integration Test|Board|Radio link: with lora.TRANSMIT DISABLED, 3 commands sent over RF yield no downlink; after TRANSMIT ENABLED all 3 OpCodeCompleted events arrive within 45 s (comQueue events depth 50)|||
|CDH-9|The system shall be capable of basic health monitoring of all hardware, temperatures, currents, and voltages.|Integration Test|Board|Over 70 s at tlmSend level 5: zero HLTH_PING_WARN/HLTH_PING_LATE events; ina219Sys and ina219Sol Voltage and Current, all 10 temperature channels and modeManager.CurrentMode each update at least once|||
|CDH-10|The system shall maintain and transition between operational states.|Integration Test|Board|FORCE_SAFE_MODE from NORMAL -> GET_CURRENT_MODE reports SAFE_MODE within 5 s; EXIT_SAFE_MODE -> NORMAL within 5 s; safe-mode reason reads GROUND_COMMAND then NONE|||
|CDH-11|The system shall support entry into safe state upon detection of fault.|Integration Test|Board|For each wired fault (command loss >= COMM_LOSS_TIME; bus voltage < 6.7 V for 10 consecutive 1 Hz samples): EnteringSafeMode within 2 s of detection and GET_CURRENT_MODE = SAFE_MODE|||
|CDH-12|The system shall support prioritisation of stored data for downlink.|Inspection|Unit|comQueue priorities configured events(0) > file(1) > tlm(2) (ComCcsdsConfig.fpp:31-33) and per-packet priority tags commandable [tags not implemented: DH-L2-06 Partial]|||
|CDH-13|The system shall be capable of scheduling time-based and event-based tasks.|Integration Test|Board|A sequence with relative tag R00:00:05 executes CMD_NO_OP_STRING 5 +/-1 s after CS_RUN (event timestamps); rtcManager.ALARM_SET for now+5 s emits AlarmTriggered within 10 s|||
|CDH-14|The system shall manage onboard data storage for telemetry and housekeeping data.|Integration Test|Board|fsSpace.FreeSpace and TotalSpace update at least once per 45 s with 0 < FreeSpace <= TotalSpace (level 5); after uplinking a 4 KB file FreeSpace decreases by >= 4 KB within 45 s|||
|CDH-15|The system shall detect and flag fault conditions.|Integration Test|Board|Each injected fault produces a WARNING event within 2 s: FACE_TEMP_UPPER_THRESHOLD set to 0 -> TemperatureAboveThreshold; COMM_LOSS_TIME expiry -> CommandLossFound; face0 switch OFF then GetTemperature -> DeviceNotReady|||
|CDH-16|The system shall log fault events for later retrieval.|Integration Test|Board|A WARNING_HI event raised before a COLD_RESET is retrievable after reboot from an on-board log via fileDownlink within 60 s [no on-board event log exists today]|||
|CDH-17|The spacecraft shall support on-orbit upload, activation, and execution of reconfigurable controls-payload algorithms.|Demonstration|Flatsat|A payload algorithm image uplinked via FileUplink (FileReceived) is activated by command and the payload reports the new algorithm id in telemetry within 60 s [payload not interfaced]|||
|CDH-18|Spacecraft design should track current and voltage measurements for independent solar panels, battery, power distribution buses, and individual components.|Integration Test|Board|ina219Sys and ina219Sol Voltage and Current channels each update at least once per 45 s (level 5); per-panel, battery, bus and per-component channels [only 2 INA219 fitted: TBD by Mission Ops: sensor list]|||
|CDH-19|The system shall be capable of taking input from a received command.|Integration Test|Board|CMD_NO_OP_STRING with argument Hello World! is acked OpCodeCompleted within 10 s and NoOpStringReceived echoes the argument, over the active uplink (UART job and radio job)|||
|CDH-20|The system may be capable of uploading new algorithms to reconfigure the controls payload.|Demonstration|Flatsat|[may] Same as CDH-17 plus: an image with a bad signature/CRC is rejected and the previous algorithm stays active [not implemented]|||
|CDH-21|The system shall be capable of detumbling.|Integration Test|Environmental|In a Helmholtz cage from an initial rate >= 8 deg/s, angular rate falls below 5 deg/s (DEADBAND thresholds) within [TBD by Mission Ops: minutes] with detumbleManager in AUTO|||
|CDH-22|The system shall be capable of stabilising orientation.|Analysis|Environmental|After detumble the angular rate stays below 5 deg/s for [TBD by Mission Ops: duration]; B-dot damps rate only, no orientation hold exists (Analysis)|||
|CDH-23|After detumble, the spacecraft may maintain coarse attitude stability with a steady-state pointing error (of #).|Analysis|Environmental|[may] Simulated or measured steady-state pointing error <= [TBD by Mission Ops: degrees] over [TBD by Mission Ops: duration] after detumble|||
|CDH-24|The system may be capable of pointing in a specified direction.|Demonstration|Environmental|[may] Commanded pointing direction reached within [TBD by Mission Ops: degrees] [not implemented]|||
|CDH-25|The system may be capable of measuring the settle time.|Analysis|Environmental|[may] Settle time from slew command to error below threshold is telemetered with 1 s resolution [not implemented]|||
|CDH-26|The system may be capable of accurately reaching the specified point.|Demonstration|Environmental|[may] Final pointing error <= [TBD by Mission Ops: degrees] on 3 consecutive commanded targets [not implemented]|||
|CDH-27|The system may downlink controls algorithm telemetry at least every 2 days.|Demonstration|Board|[may] detumbleManager Mode and coil telemetry received at least once in every 48 h window of a run >= 96 h (at P = 30 s this is >= 5760 receipts per window)|||
|CDH-28|The system shall be capable of detecting or recovering from unstable control behaviour on-board.|Integration Test|Flatsat|With injected rate divergence (rate rising over 3 consecutive 50 Hz samples while torquing) detumbleManager stops torquing and emits a warning within [TBD by Mission Ops: s] [not implemented]|||
|CDH-29|The system may be capable of holding the specified control input.|Demonstration|Environmental|[may] Commanded coil current held within [TBD by Mission Ops: percent] for [TBD by Mission Ops: duration] [not implemented]|||
|CDH-30|The system may be capable of pointing in a direction specified by input from a received command.|Demonstration|Environmental|[may] Pointing direction from a ground command reached per CDH-24 [not implemented]|||
|CDH-31|Temperature sensors shall be installed on each critical component within the spacecraft and functional when the satellite is powered on.|Inspection|Board|Schematic/BOM shows a temperature sensor on each critical component [list TBD by Mission Ops]; after boot each of the 9 TMP112 and the pico sensor returns a Temperature event within 5 s of its Get command|||

## CDH Command Handling (CH-L2)

Traced L1 requirements: CDH-3, CDH-19.

| Name | Description | Method | Level | Pass Criteria | Status | Reason |
|---|---|---|---|---|---|---|
|CH-L2-01|System shall receive command data from all supported interfaces (LoRa, UART).|Integration Test|Board|CMD_NO_OP acked OpCodeCompleted within 10 s over UART (integration-uart job) and over LoRa (integration-radio job); zero tcDeframer InvalidCrc events during the test|CDR: Met||
|CH-L2-02|System shall identify and extract complete CCSDS telecommand packets from incoming streams.|Integration Test|Board|10 consecutive commands each acked within 10 s with zero tcDeframer InvalidFrameLength/InvalidCrc/InvalidSpacecraftId and zero spacePacketDeframer InvalidLength events|CDR: Met||
|CH-L2-03|System shall reassemble fragmented command frames prior to processing.|Integration Test|Board|A CMD_NO_OP_STRING with a 40-char argument (frame larger than one 10 Hz UART read chunk) is acked and echoed intact; zero frameAccumulator FrameDetectionSizeError events|CDR: Met||
|CH-L2-04|System shall validate command packet structure (header fields, length, opcode format).|Inspection|Unit|TcDeframer rejects bad SCID, length > available, bad VCID and bad CRC with the matching event and no forward (TcDeframer.cpp:60-101); SpacePacketDeframer rejects bad length (InvalidLength)|CDR: Met||
|CH-L2-05|System shall verify command integrity using CRC and/or authentication mechanisms.|Integration Test|Board|A non-bypass command framed with a stale sequence number is rejected: SequenceNumberOutOfWindow within 5 s and no OpCodeDispatched; the next correctly numbered command is accepted|CDR: Met||
|CH-L2-06|System shall discard malformed, incomplete, or invalid command packets.|Integration Test|Board|For the rejected packet of CH-L2-05: RejectedPacketsCount increases by 1 (level 5) and no OpCodeDispatched/OpCodeCompleted for that opcode within 5 s|CDR: Met||
|CH-L2-07|System shall deserialize command arguments into internal representations.|Integration Test|Board|CMD_NO_OP_STRING argument is echoed byte-exact in NoOpStringReceived; modeManager.GET_CURRENT_MODE returns CurrentModeReading with a valid SystemMode value|CDR: Met||
|CH-L2-08|System shall forward validated commands to the command dispatcher.|Integration Test|Board|Every accepted command produces cmdDisp.OpCodeDispatched then OpCodeCompleted within 10 s|CDR: Met||
|CH-L2-09|System shall queue commands when immediate dispatch is not possible.|Integration Test|Board|5 commands sent back-to-back without waiting all complete (5 OpCodeCompleted within 15 s) with zero TooManyCommands/CommandDroppedQueueOverflow (cmdDisp queue depth 10)|CDR: Met||
|CH-L2-10|System shall route commands to target components based on opcode.|Integration Test|Board|Commands addressed to 3 different components (cmdDisp, modeManager, startupManager) each produce that component's response event within 5 s|CDR: Met||
|CH-L2-11|System shall support time-tagged command execution via scheduler.|Integration Test|Board|Sequence with R00:00:05 CMD_NO_OP_STRING: NoOpStringReceived 5 +/-1 s after the CS_RUN OpCodeCompleted (FSW timestamps), then CS_SequenceComplete|CDR: Met||
|CH-L2-12|System shall support commands to enable and disable telemetry transmission.|Integration Test|Board|telemetryGate.SET_TRANSMIT_STATE and lora.TRANSMIT each ack OK within 10 s for ENABLED and DISABLED (TransmitStateSet event; no LoRa error event)|CDR: Met|Implemented for the radio (LoRa TRANSMIT modes); TelemetryGate adds the telemetry-side gate|
|CH-L2-13|System shall cease all scheduled downlink transmissions upon valid disable command.|Integration Test|Board|Radio link: zero packets of any kind (channelized telemetry, events, command acks) received over RF in the 70 s after the lora.TRANSMIT DISABLED ack|CDR: Met||
|CH-L2-14|System shall resume transmission upon valid enable command.|Integration Test|Board|Radio link: after lora.TRANSMIT ENABLED at least one event and one telemetry item arrive within 45 s; after telemetryGate ENABLED channelized telemetry arrives within 45 s|CDR: Met||
|CH-L2-15|System shall cease transmission within one scheduler cycle after command execution.|Integration Test|Board|After the TransmitStateSet(DISABLED) event at FSW time T: zero channelized telemetry with FSW timestamp > T+1 s received in the following 70 s (UART)|CDR: Not met|No one-cycle guarantee existed at CDR; TelemetryGate (port branch) latches state synchronously, effective next tick|
|CH-L2-16|System shall maintain a transmission enable/disable state.|Integration Test|Board|After DISABLED for 70 s then ENABLED: GatedTicks increased by >= 2 and TransmitState reads ENABLED within 45 s (level 5); state survives restart per TelemetryGate-4 at unit|CDR: Met||
|CH-L2-17|System shall inhibit telemetry scheduling when transmission is disabled.|Unit Test|Unit|Over N DISABLED ticks zero runOut calls and GatedTicks == N; ENABLED forwards every tick unchanged (TelemetryGate-2, -3, -8)|CDR: Not met|No telemetry-scheduling inhibit existed at CDR; TelemetryGate (port branch) drops the TlmChan tick while DISABLED|
|CH-L2-18|System shall use fixed or bounded buffers for command reception and processing.|Inspection|Unit|All uplink buffers static: frameAccumulator 1024 B, comms pool 5x1024 B + file pool 5x1024 B per link, cmdDisp queue 10, comQueue depths 50/1/1; no new/malloc in Authenticate, AuthenticationRouter|CDR: Met||
|CH-L2-19|System shall reject commands exceeding maximum supported size.|Inspection|Unit|A frame declaring length > available bytes is rejected with InvalidFrameLength and dropped (TcDeframer.cpp:71-73); accumulation beyond 1024 B raises FrameDetectionSizeError and drops|CDR: Met||
|CH-L2-20|System shall be capable of correcting X number of bit flips if flagged by CRC deframer.|Unit Test|Unit|For every single-bit flip (N=1) in a valid TC frame the corrector restores the frame and it is forwarded; frames with > N errors are rejected [N TBD by Mission Ops; not implemented]|CDR: Not met|Bit-flip correction not implemented; planned if deframer reports CRC mismatch|

## CDH Telemetry Acquisition and Monitoring (TM-L2)

Traced L1 requirements: CDH-4, CDH-5, CDH-6.

| Name | Description | Method | Level | Pass Criteria | Status | Reason |
|---|---|---|---|---|---|---|
|TM-L2-01|System shall collect telemetry from all registered subsystem sources.|Integration Test|Board|Within 70 s at level 5 at least one update from each source: imuManager (3 channels), tmp112 x9, pico, ina219Sys/Sol, powerMonitor, fsSpace, startupManager, modeManager, lora, rateGroups x3; payload [not integrated]|CDR: Partial|Payload data not yet received into the telemetry pipeline|
|TM-L2-02|System shall support configurable telemetry collection intervals.|Integration Test|Board|After a source's COLLECTION_INTERVAL_S is set to N s (1..60), that source's channel updates are spaced N +/-1 s over 5 consecutive updates (telemetryDelay.DIVIDER 0, packet level 3)|CDR: Not met|Subsystem sampling loops are tied to fixed frequencies; not configurable at runtime|
|TM-L2-03|System shall timestamp all collected telemetry.|Integration Test|Board|Every telemetry item received over 70 s carries a non-zero FSW time and per-channel timestamps are non-decreasing|CDR: Met||
|TM-L2-04|System shall package telemetry into structured data records.|Inspection|Unit|Every channel is packed by Svc.TlmPacketizer into the packets of ReferenceDeploymentPackets.fppi (id + time + values) and the GDS decodes all packets against the dictionary with zero decode errors|CDR: Met||
|TM-L2-05|System shall support monitoring of thermal data.|Integration Test|Board|With face switches ON and level 5: Temperature for each TMP112 (face0-3,5; batt1-4) and picoTempManager.PicoTemperature update at least once per 45 s with value in -40..125 C|CDR: Met||
|TM-L2-06|System shall support monitoring of magnetometer data.|Integration Test|Board|imuManager.MagneticField updates at least once per 45 s (level 5) with at least one non-zero axis; GET_MAGNETIC_FIELD returns MagneticFieldData within 3 s|CDR: Met||
|TM-L2-07|System shall collect electrical health data (voltage, current).|Integration Test|Board|ina219Sys and ina219Sol Voltage and Current update at least once per 45 s (level 5); sys voltage > 6.7 V and < 9 V on the bench; powerMonitor.TotalPowerConsumption increases across 45 s|CDR: Met||
|TM-L2-08|System shall detect out-of-range telemetry values and flag them.|Unit Test|Unit|Face/battery temperature outside [lower, upper] (defaults -40/60 and 5/60 C) raises TemperatureBelow/AboveThreshold once, re-armed only after 3 C hysteresis; voltage/current thresholds [TBD by Mission Ops]|CDR: Partial|Device faults are flagged, but desired ranges/thresholds are not defined so values are not range-checked|
|TM-L2-09|System shall support enabling/disabling telemetry streams.|Integration Test|Board|SET_TRANSMIT_STATE DISABLED stops the whole channelized stream within one period; per-subsystem stream enable/disable command [not implemented: TBD design]|CDR: Partial|Stream output can be gated as a whole; no runtime control per subsystem stream|

## CDH Storage Management (DH-L2)

Traced L1 requirements: CDH-8, CDH-12, CDH-14, CDH-16, CDH-27.

| Name | Description | Method | Level | Pass Criteria | Status | Reason |
|---|---|---|---|---|---|---|
|DH-L2-01|System shall buffer telemetry data prior to downlink.|Integration Test|Board|Radio link: events generated while lora.TRANSMIT is DISABLED (3 OpCodeCompleted) are delivered within 45 s of TRANSMIT ENABLED; the latest telemetry packet is delivered on the next run|CDR: Met||
|DH-L2-02|System shall allocate and manage memory for telemetry buffers.|Integration Test|Board|ComCcsdsLora/Uart commsBufferManager.NoBuffs and payloadBufferManager.NoBuffs stay 0 over 70 s (level 5); pool sizes are static (ComCcsdsConfig.fpp:38-42)|CDR: Met||
|DH-L2-03|System shall support configuration of telemetry storage buffer sizes.|Inspection|Unit|After a buffer-size parameter is set by command, the reported capacity equals the new size on the next period [not implemented: sizes are compile-time constants]|CDR: Partial|Svc::BufferManager/ComQueue are configured once at boot (lib); no project-owned telemetry buffer; needs TelemetryStore design|
|DH-L2-04|System shall validate requested buffer sizes against available memory.|Unit Test|Unit|A requested buffer size larger than the configured pool is rejected with VALIDATION_ERROR and the previous size is retained [not implemented]|CDR: Partial|Svc::BufferManager/ComQueue are configured once at boot (lib); no project-owned telemetry buffer; needs TelemetryStore design|
|DH-L2-05|System shall apply buffer configuration changes only after successful command processing.|Integration Test|Board|The new buffer size takes effect only after the set command returns OK; a command that returns an error leaves the old size in force [not implemented]|CDR: Not met|Svc::BufferManager/ComQueue are configured once at boot (lib); no project-owned telemetry buffer; needs TelemetryStore design|
|DH-L2-06|System shall prioritize stored data based on configurable priority levels.|Inspection|Unit|comQueue serves EVENTS(0) before FILE(1) before TLM(2) when all are non-empty (ComCcsdsConfig.fpp:31-33); commandable per-packet priority tags [not implemented]|CDR: Partial|Packet context header has no priority field; only coarse {events, file, tlm} queue priorities|
|DH-L2-07|System shall support selective downlink of stored data.|Integration Test|Board|fileDownlink.SendFile of a 4 KB file previously uplinked completes (FileSent event) within 60 s and the downlinked file is byte-identical to the source|CDR: Met||
|DH-L2-08|System shall retain telemetry for a configurable duration.|Integration Test|Board|Telemetry retained on board for a commanded duration D: a record older than D is absent and one younger than D is present on retrieval [not implemented: no on-board telemetry store]|CDR: Not met|No on-board telemetry store (TlmPacketizer latest-value only, comQueue tlm depth 1, no DataProducts); needs TelemetryStore design|
|DH-L2-09|System shall delete or overwrite data when storage capacity is exceeded.|Inspection|Unit|When a comQueue is full the next packet is dropped and exactly one QueueOverflow event is emitted for that queue (latched, ComQueue.cpp:257-265); FreeSpace never reaches 0 in a 70 s run|CDR: Partial||
|DH-L2-10|System shall implement buffer overflow handling with defined data discard policy.|Inspection|Unit|Discard policy documented as drop-newest with a latched QueueOverflow warning (Svc/ComQueue/ComQueue.cpp:257-265); depths events 50, tlm 1, file 1|CDR: Partial|Discard policy not explicitly defined|
|DH-L2-11|System shall ensure data integrity during storage.|Integration Test|Board|PersistedRecord-1..7 pass at unit; PersistedRecord-5 power-cut test: zero silent wrong reads over >= 20 cycles; every persisted consumer (MM0011/12, AUTH013, REQ-SM-008, TelemetryGate-9) linked|CDR: Partial|SD writes are a known corruption source (risk 2); decomposed into PersistedRecord-1..7 plus consumer adoption MM0011-MM0012, AUTH013, REQ-SM-008, TelemetryGate-9 (Components/PersistedRecord/docs/sdd.md)|
|DH-L2-12|System shall log fault events for later retrieval.|Integration Test|Board|Every WARNING/FATAL event is appended to an on-board log and the log is retrievable by fileDownlink after reboot [no on-board event log exists today]|CDR: Partial|Logging does not guarantee a complete record of all fault occurrences|
|DH-L2-13|System shall support scheduled downlink of stored telemetry.|Integration Test|Board|startupManager.BootCount is received at least once per 45 s and consecutive receipts are spaced 30 +/-5 s over 3 periods|CDR: Met||
|DH-L2-14|System shall support concurrent read/write access without corruption.|Unit Test|Unit|PersistedRecord-4 atomic temp+rename passes; each state file is written only from its owning component's execution context (Inspection)|CDR: Met||

## CDH Scheduling (SC-L2)

Traced L1 requirements: CDH-13.

| Name | Description | Method | Level | Pass Criteria | Status | Reason |
|---|---|---|---|---|---|---|
|SC-L2-01|System shall execute periodic tasks at defined rates.|Integration Test|Board|Over 70 s at level 5: rateGroup50Hz/10Hz/1Hz RgCycleSlips == 0 and RgMaxTime < period (20/100/1000 ms); BootCount received every period|CDR: Met||
|SC-L2-02|System shall support event-driven task execution.|Unit Test|Unit|A command handler executes on receipt with zero scheduler ticks: SET_TRANSMIT_STATE emits TransmitStateSet and responds OK before any runIn tick|CDR: Met||
|SC-L2-03|System shall schedule time-tagged command execution.|Integration Test|Board|Relative-tagged command executes 5 +/-1 s after CS_RUN (CH-L2-11); absolute tags [not exercised]|CDR: Met||
|SC-L2-04|System shall coordinate telemetry collection tasks.|Integration Test|Board|Over 70 s the 1 Hz group (imu, thermal, power, adcs, mode, watchdog) shows RgMaxTime < 1000 ms and RgCycleSlips == 0 while all its channels update|CDR: Partial|Telemetry calls serialized sequentially; coordination immature, overflow/jitter possible if collection runs long|
|SC-L2-05|System shall provide deterministic execution timing.|Analysis|Board|Over 10 min: RgCycleSlips == 0 for all groups and RgMaxTime <= [TBD by Mission Ops: headroom percent] of the period (interim: RgMaxTime < period)|CDR: Partial|No guarantee of deterministic execution; only a monitoring component exists|
|SC-L2-06|System shall prioritise critical tasks.|Inspection|Unit|instances.fpp priorities rateGroup50Hz(1) < rateGroup10Hz(2) < rateGroup1Hz(3) (lower = higher) and modeManager(4) <= tlmSend(6)|CDR: Partial||
|SC-L2-07|System shall support enabling/disabling scheduled tasks.|Integration Test|Board|Each schedulable task has a command that stops and restarts it with effect within one cycle (watchdog START/STOP, telemetryGate SET_TRANSMIT_STATE); generic ENABLE_TASK/DISABLE_TASK(taskId) [not implemented]|CDR: Partial|Rate-group wiring is always on; no generic command to disable arbitrary scheduled component runs|

## CDH Mode Management (MS-L2)

Traced L1 requirements: CDH-10, CDH-11.

| Name | Description | Method | Level | Pass Criteria | Status | Reason |
|---|---|---|---|---|---|---|
|MS-L2-01|System shall maintain operational modes defined by CONOPS.|Integration Test|Board|GET_CURRENT_MODE reports each CONOPS mode {SAFE_MODE, NORMAL, STANDBY, CALIBRATION, EXPERIMENT} after its entry command [only SAFE_MODE and NORMAL exist]|CDR: Partial|Only SAFE and NORMAL exist; STANDBY, CALIBRATION, EXPERIMENT not implemented|
|MS-L2-02|System shall support transitions between modes.|Integration Test|Board|FORCE_SAFE_MODE from NORMAL -> SAFE_MODE readback within 5 s and EXIT_SAFE_MODE -> NORMAL within 5 s (implemented pair); other pairs inherit MS-L2-01|CDR: Partial|Transitions exist only between the two implemented modes|
|MS-L2-03|System shall enforce mode-dependent behavior.|Integration Test|Board|On FORCE_SAFE_MODE detumbleManager.Mode telemetry reads DISABLED within 45 s (level 5); on EXIT_SAFE_MODE it returns to the OPERATING_MODE parameter within 45 s|CDR: Partial||
|MS-L2-04|System shall enter safe mode upon critical faults.|Integration Test|Board|Command loss >= COMM_LOSS_TIME -> CommandLossFound + EnteringSafeMode within 2 s; bus voltage < 6.7 V for 10 consecutive 1 Hz samples -> AutoSafeModeEntry(LOW_BATTERY)|CDR: Met||
|MS-L2-05|System shall restrict subsystem operations based on mode.|Integration Test|Board|Within 5 s of FORCE_SAFE_MODE every load switch (face0-5, payloadPower, payloadBattery) reads OFF via GET_IS_ON; payload switches remain OFF after EXIT_SAFE_MODE|CDR: Partial||
|MS-L2-06|System shall broadcast mode changes.|Integration Test|Board|Every mode change emits EnteringSafeMode or ExitingSafeMode within 2 s and calls modeChanged (detumbleManager reacts per MS-L2-03)|CDR: Met||
|MS-L2-07|System shall maintain persistent record of current mode.|Integration Test|Board|After FORCE_SAFE_MODE then WARM_RESET: GET_CURRENT_MODE = SAFE_MODE with reason GROUND_COMMAND and no UnintendedRebootDetected; MM0011/12 (CRC-protected state) pass at unit|CDR: Partial|Persisted state lacks integrity fields; hardening decomposed into MM0011-MM0012 (PersistedRecord with CRC+version; corrupt state boots SAFE)|
|MS-L2-08|System shall support autonomous mode transitions based on state of health.|Unit Test|Unit|With injected voltage < 6.7 V for 10 consecutive run ticks the component enters SAFE_MODE(LOW_BATTERY); 9 ticks do not; > 8.0 V for 10 ticks exits; exactly 8.0 V does not|CDR: Partial||
|MS-L2-09|System shall be able to process manual override commands as necessary.|Integration Test|Board|FORCE_SAFE_MODE acked OK from NORMAL; EXIT_SAFE_MODE acked OK and NORMAL within 5 s for entry reasons GROUND_COMMAND (automated), LOW_BATTERY and SYSTEM_FAULT (procedure)|CDR: Partial||

## CDH FDIR (FD-L2)

Traced L1 requirements: CDH-15, CDH-16, CDH-28.

| Name | Description | Method | Level | Pass Criteria | Status | Reason |
|---|---|---|---|---|---|---|
|FD-L2-01|System shall detect fault conditions from telemetry and subsystem reports.|Integration Test|Board|Thermal: face/battery temperature outside thresholds -> TemperatureAbove/BelowThreshold within 2 s; voltage < 6.7 V for 10 s -> AutoSafeModeEntry; device fault -> DeviceNotReady within 2 s|CDR: Partial|Detection coverage and instrumentation not widely implemented across subsystems|
|FD-L2-02|System shall classify faults based on severity levels.|Inspection|Unit|Every fault event in Components/*.fpp carries a severity (warning low/high or fatal) and FATAL routes events.FatalAnnounce -> fatalHandler -> watchdog.stop (CdhCore.fpp:67, topology.fpp:495)|CDR: Met||
|FD-L2-03|System shall flag detected faults.|Unit Test|Unit|Each detected fault produces a WARNING event within one evaluation: measured for TemperatureAboveThreshold (unit + board) and CommandLossFound (board)|CDR: Met||
|FD-L2-04|System shall log all fault events with timestamps.|Integration Test|Board|Every fault event received carries an FSW timestamp within 5 s of GDS receipt; retention in an on-board log [not implemented]|CDR: Partial|Logging does not guarantee a complete record of all fault occurrences|
|FD-L2-05|System shall trigger recovery actions based on fault type.|Integration Test|Board|Per fault type the mapped action occurs: watchdog stall -> reboot (BootCount +1 within 60 s); command loss -> safe mode then reboot; low battery -> safe mode + load switches OFF|CDR: Partial|No unified critical-fault authority ensuring consistent behavior across pathways|
|FD-L2-06|System shall initiate safe mode upon critical fault detection.|Integration Test|Board|Same as MS-L2-04: EnteringSafeMode within 2 s of critical-fault detection and GET_CURRENT_MODE = SAFE_MODE|CDR: Partial|Safe-mode entry triggered by watchdog stall only; other critical faults not routed|
|FD-L2-07|System shall detect unstable control behavior in ADCS.|Integration Test|Flatsat|Rate divergence over 3 consecutive control samples while torquing is flagged by a warning within [TBD by Mission Ops: s] [not implemented]|CDR: Partial|No comprehensive system-level health component; status only exposed via telemetry|
|FD-L2-08|System shall attempt recovery from transient faults.|Demonstration|Board|A single failed LoRa send is retried by loraRetry and delivered; one failed I2C sensor read does not stop the next 1 Hz cycle (next channel update within 2 s)|CDR: Partial|Transient fault handling limited|
|FD-L2-09|System shall provide fault status to telemetry system.|Integration Test|Board|modeManager.CurrentMode, SafeModeEntryCount, CurrentSafeModeReason and authenticate RejectedPacketsCount are received at least once per 45 s (level 5)|CDR: Partial||

## CDH ADCS Enablement (ADCS-L2)

Traced L1 requirements: CDH-21, CDH-22 (the CDR slide's traced list repeats the
Storage Management line — CDH-8, 12, 14, 16, 27 — which appears to be a
copy-paste carryover; the ADCS content maps to CDH-21 through CDH-30).

| Name | Description | Method | Level | Pass Criteria | Status | Reason |
|---|---|---|---|---|---|---|
|ADCS-L2-01|System shall issue commands to initiate detumble operations.|Demonstration|Board|SET_MODE AUTO acked OK and Mode channel AUTO within 45 s; with the board rotated above 8 deg/s at least one coil Start (drv2605 power rise >= 0.3 W) within 2 s [manual rotation]|CDR: Met||
|ADCS-L2-02|System shall support attitude stabilization routines.|Unit Test|Unit|B-dot moment equals -gain x dB/dt (5-point difference) on single- and multi-axis ramps; StrategySelector returns IDLE below 5, BDOT in 5..720, HYSTERESIS above 720 deg/s with 5/8 deadband|CDR: Met|B-dot detumble implemented using magnetorquers and magnetometers|
|ADCS-L2-03|System may maintain coarse attitude stability.|Analysis|Environmental|[may] Coarse attitude held within [TBD by Mission Ops: degrees] after detumble [not implemented]|CDR: Not met|"May" requirement; no control logic beyond B-dot detumble implemented|
|ADCS-L2-04|System may collect and store attitude telemetry.|Integration Test|Board|[may] An attitude estimate channel is written each cycle and stored on board [not implemented; imu channels only]|CDR: Not met|"May" requirement; not implemented|
|ADCS-L2-05|System may schedule ADCS control loops.|Inspection|Unit|[may] detumbleManager runs in the 50 Hz group (topology.fpp:252); further control loops [not implemented]|CDR: Not met|"May" requirement; not implemented, requires nonlinear control logic|
|ADCS-L2-06|System may support downlink of control telemetry.|Integration Test|Board|[may] detumbleManager.Mode and coil parameter channels are received at least once per 45 s at level 6|CDR: Not met|"May" requirement; not implemented|
