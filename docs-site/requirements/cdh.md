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
|CDH-1|EGSE shall be capable of command and control of the satellite without using the radio.|Demonstration|Board|TBD|||
|CDH-2|EGSE shall be capable of satellite thermal monitoring.|Demonstration|Board|TBD|||
|CDH-3|Shall be capable of ceasing transmission on command.|Test|Flatsat|TBD|||
|CDH-4|The system shall be capable of monitoring thermal data.|Test|Board|TBD|||
|CDH-5|The system shall be capable of collecting ADCS telemetry at a configurable interval.|Test|Board|TBD|||
|CDH-6|The system shall be capable of monitoring magnetometer data.|Test|Board|TBD|||
|CDH-7|The system may be capable of measuring coarse attitude using photodiodes.|Test|Flatsat|TBD|||
|CDH-8|The system shall be capable of buffering data for later transmission.|Test|Board|TBD|||
|CDH-9|The system shall be capable of basic health monitoring of all hardware, temperatures, currents, and voltages.|Test|Flatsat|TBD|||
|CDH-10|The system shall maintain and transition between operational states.|Test|Board|TBD|||
|CDH-11|The system shall support entry into safe state upon detection of fault.|Test|Flatsat|TBD|||
|CDH-12|The system shall support prioritisation of stored data for downlink.|Test|Board|TBD|||
|CDH-13|The system shall be capable of scheduling time-based and event-based tasks.|Test|Board|TBD|||
|CDH-14|The system shall manage onboard data storage for telemetry and housekeeping data.|Test|Board|TBD|||
|CDH-15|The system shall detect and flag fault conditions.|Test|Flatsat|TBD|||
|CDH-16|The system shall log fault events for later retrieval.|Test|Board|TBD|||
|CDH-17|The spacecraft shall support on-orbit upload, activation, and execution of reconfigurable controls-payload algorithms.|Demonstration|Flatsat|TBD|||
|CDH-18|Spacecraft design should track current and voltage measurements for independent solar panels, battery, power distribution buses, and individual components.|Test|Flatsat|TBD|||
|CDH-19|The system shall be capable of taking input from a received command.|Test|Board|TBD|||
|CDH-20|The system may be capable of uploading new algorithms to reconfigure the controls payload.|Demonstration|Flatsat|TBD|||
|CDH-21|The system shall be capable of detumbling.|Test|Environmental|TBD|||
|CDH-22|The system shall be capable of stabilising orientation.|Test|Environmental|TBD|||
|CDH-23|After detumble, the spacecraft may maintain coarse attitude stability with a steady-state pointing error (of #).|Analysis|Environmental|TBD|||
|CDH-24|The system may be capable of pointing in a specified direction.|Test|Environmental|TBD|||
|CDH-25|The system may be capable of measuring the settle time.|Test|Environmental|TBD|||
|CDH-26|The system may be capable of accurately reaching the specified point.|Test|Environmental|TBD|||
|CDH-27|The system may downlink controls algorithm telemetry at least every 2 days.|Test|Flatsat|TBD|||
|CDH-28|The system shall be capable of detecting or recovering from unstable control behaviour on-board.|Test|Flatsat|TBD|||
|CDH-29|The system may be capable of holding the specified control input.|Test|Environmental|TBD|||
|CDH-30|The system may be capable of pointing in a direction specified by input from a received command.|Test|Environmental|TBD|||
|CDH-31|Temperature sensors shall be installed on each critical component within the spacecraft and functional when the satellite is powered on.|Inspection|Board|TBD|||

## CDH Command Handling (CH-L2)

Traced L1 requirements: CDH-3, CDH-19.

| Name | Description | Method | Level | Pass Criteria | Status | Reason |
|---|---|---|---|---|---|---|
|CH-L2-01|System shall receive command data from all supported interfaces (LoRa, UART).|Integration Test|Board|TBD|CDR: Met||
|CH-L2-02|System shall identify and extract complete CCSDS telecommand packets from incoming streams.|Integration Test|Board|TBD|CDR: Met||
|CH-L2-03|System shall reassemble fragmented command frames prior to processing.|Integration Test|Board|TBD|CDR: Met||
|CH-L2-04|System shall validate command packet structure (header fields, length, opcode format).|Integration Test|Board|TBD|CDR: Met||
|CH-L2-05|System shall verify command integrity using CRC and/or authentication mechanisms.|Integration Test|Board|TBD|CDR: Met||
|CH-L2-06|System shall discard malformed, incomplete, or invalid command packets.|Integration Test|Board|TBD|CDR: Met||
|CH-L2-07|System shall deserialize command arguments into internal representations.|Integration Test|Board|TBD|CDR: Met||
|CH-L2-08|System shall forward validated commands to the command dispatcher.|Integration Test|Board|TBD|CDR: Met||
|CH-L2-09|System shall queue commands when immediate dispatch is not possible.|Unit Test|Unit|TBD|CDR: Met||
|CH-L2-10|System shall route commands to target components based on opcode.|Integration Test|Board|TBD|CDR: Met||
|CH-L2-11|System shall support time-tagged command execution via scheduler.|Integration Test|Board|TBD|CDR: Met||
|CH-L2-12|System shall support commands to enable and disable telemetry transmission.|Test|Board|TBD|CDR: Met|Implemented for the radio (LoRa TRANSMIT modes); TelemetryGate adds the telemetry-side gate|
|CH-L2-13|System shall cease all scheduled downlink transmissions upon valid disable command.|Test|Flatsat|TBD|CDR: Met||
|CH-L2-14|System shall resume transmission upon valid enable command.|Test|Flatsat|TBD|CDR: Met||
|CH-L2-15|System shall cease transmission within one scheduler cycle after command execution.|Test|Board|TBD|CDR: Not met|No one-cycle guarantee existed at CDR; TelemetryGate (port branch) latches state synchronously, effective next tick|
|CH-L2-16|System shall maintain a transmission enable/disable state.|Test|Board|TBD|CDR: Met||
|CH-L2-17|System shall inhibit telemetry scheduling when transmission is disabled.|Test|Board|TBD|CDR: Not met|No telemetry-scheduling inhibit existed at CDR; TelemetryGate (port branch) drops the TlmChan tick while DISABLED|
|CH-L2-18|System shall use fixed or bounded buffers for command reception and processing.|Analysis|Unit|TBD|CDR: Met||
|CH-L2-19|System shall reject commands exceeding maximum supported size.|Unit Test|Unit|TBD|CDR: Met||
|CH-L2-20|System shall be capable of correcting X number of bit flips if flagged by CRC deframer.|Unit Test|Unit|TBD|CDR: Not met|Bit-flip correction not implemented; planned if deframer reports CRC mismatch|

## CDH Telemetry Acquisition and Monitoring (TM-L2)

Traced L1 requirements: CDH-4, CDH-5, CDH-6.

| Name | Description | Method | Level | Pass Criteria | Status | Reason |
|---|---|---|---|---|---|---|
|TM-L2-01|System shall collect telemetry from all registered subsystem sources.|Integration Test|Board|TBD|CDR: Partial|Payload data not yet received into the telemetry pipeline|
|TM-L2-02|System shall support configurable telemetry collection intervals.|Test|Board|TBD|CDR: Not met|Subsystem sampling loops are tied to fixed frequencies; not configurable at runtime|
|TM-L2-03|System shall timestamp all collected telemetry.|Unit Test|Unit|TBD|CDR: Met||
|TM-L2-04|System shall package telemetry into structured data records.|Unit Test|Unit|TBD|CDR: Met||
|TM-L2-05|System shall support monitoring of thermal data.|Integration Test|Board|TBD|CDR: Met||
|TM-L2-06|System shall support monitoring of magnetometer data.|Integration Test|Board|TBD|CDR: Met||
|TM-L2-07|System shall collect electrical health data (voltage, current).|Integration Test|Board|TBD|CDR: Met||
|TM-L2-08|System shall detect out-of-range telemetry values and flag them.|Test|Flatsat|TBD|CDR: Partial|Device faults are flagged, but desired ranges/thresholds are not defined so values are not range-checked|
|TM-L2-09|System shall support enabling/disabling telemetry streams.|Test|Board|TBD|CDR: Partial|Stream output can be gated as a whole; no runtime control per subsystem stream|

## CDH Storage Management (DH-L2)

Traced L1 requirements: CDH-8, CDH-12, CDH-14, CDH-16, CDH-27.

| Name | Description | Method | Level | Pass Criteria | Status | Reason |
|---|---|---|---|---|---|---|
|DH-L2-01|System shall buffer telemetry data prior to downlink.|Unit Test|Unit|TBD|CDR: Met||
|DH-L2-02|System shall allocate and manage memory for telemetry buffers.|Unit Test|Unit|TBD|CDR: Met||
|DH-L2-03|System shall support configuration of telemetry storage buffer sizes.|Test|Board|TBD|CDR: Partial|Buffer sizes editable in source code only; not command-configurable at runtime|
|DH-L2-04|System shall validate requested buffer sizes against available memory.|Unit Test|Unit|TBD|CDR: Partial|No runtime buffer configuration exists to validate|
|DH-L2-05|System shall apply buffer configuration changes only after successful command processing.|Test|Board|TBD|CDR: Not met|Buffer configuration is not runtime command-configurable|
|DH-L2-06|System shall prioritize stored data based on configurable priority levels.|Test|Board|TBD|CDR: Partial|Packet context header has no priority field; only coarse {events, file, tlm} queue priorities|
|DH-L2-07|System shall support selective downlink of stored data.|Integration Test|Board|TBD|CDR: Met||
|DH-L2-08|System shall retain telemetry for a configurable duration.|Test|Board|TBD|CDR: Not met|Retention duration is not configurable|
|DH-L2-09|System shall delete or overwrite data when storage capacity is exceeded.|Test|Board|TBD|CDR: Partial||
|DH-L2-10|System shall implement buffer overflow handling with defined data discard policy.|Unit Test|Unit|TBD|CDR: Partial|Discard policy not explicitly defined|
|DH-L2-11|System shall ensure data integrity during storage.|Test|Environmental|TBD|CDR: Partial|SD writes are a known corruption source (risk 2); decomposed into PersistedRecord-1..7 plus consumer adoption MM0011-MM0012, AUTH013, REQ-SM-008, TelemetryGate-9 (Components/PersistedRecord/docs/sdd.md)|
|DH-L2-12|System shall log fault events for later retrieval.|Test|Board|TBD|CDR: Partial|Logging does not guarantee a complete record of all fault occurrences|
|DH-L2-13|System shall support scheduled downlink of stored telemetry.|Integration Test|Board|TBD|CDR: Met||
|DH-L2-14|System shall support concurrent read/write access without corruption.|Unit Test|Unit|TBD|CDR: Met||

## CDH Scheduling (SC-L2)

Traced L1 requirements: CDH-13.

| Name | Description | Method | Level | Pass Criteria | Status | Reason |
|---|---|---|---|---|---|---|
|SC-L2-01|System shall execute periodic tasks at defined rates.|Integration Test|Board|TBD|CDR: Met||
|SC-L2-02|System shall support event-driven task execution.|Unit Test|Unit|TBD|CDR: Met||
|SC-L2-03|System shall schedule time-tagged command execution.|Integration Test|Board|TBD|CDR: Met||
|SC-L2-04|System shall coordinate telemetry collection tasks.|Test|Board|TBD|CDR: Partial|Telemetry calls serialized sequentially; coordination immature, overflow/jitter possible if collection runs long|
|SC-L2-05|System shall provide deterministic execution timing.|Analysis|Board|TBD|CDR: Partial|No guarantee of deterministic execution; only a monitoring component exists|
|SC-L2-06|System shall prioritise critical tasks.|Test|Board|TBD|CDR: Partial||
|SC-L2-07|System shall support enabling/disabling scheduled tasks.|Test|Board|TBD|CDR: Partial|Rate-group wiring is always on; no generic command to disable arbitrary scheduled component runs|

## CDH Mode Management (MS-L2)

Traced L1 requirements: CDH-10, CDH-11.

| Name | Description | Method | Level | Pass Criteria | Status | Reason |
|---|---|---|---|---|---|---|
|MS-L2-01|System shall maintain operational modes defined by CONOPS.|Test|Flatsat|TBD|CDR: Partial|Only SAFE and NORMAL exist; STANDBY, CALIBRATION, EXPERIMENT not implemented|
|MS-L2-02|System shall support transitions between modes.|Test|Board|TBD|CDR: Partial|Transitions exist only between the two implemented modes|
|MS-L2-03|System shall enforce mode-dependent behavior.|Test|Flatsat|TBD|CDR: Partial||
|MS-L2-04|System shall enter safe mode upon critical faults.|Test|Board|TBD|CDR: Met||
|MS-L2-05|System shall restrict subsystem operations based on mode.|Test|Flatsat|TBD|CDR: Partial||
|MS-L2-06|System shall broadcast mode changes.|Unit Test|Unit|TBD|CDR: Met||
|MS-L2-07|System shall maintain persistent record of current mode.|Test|Board|TBD|CDR: Partial|Persisted state lacks integrity fields; hardening decomposed into MM0011-MM0012 (PersistedRecord with CRC+version; corrupt state boots SAFE)|
|MS-L2-08|System shall support autonomous mode transitions based on state of health.|Test|Flatsat|TBD|CDR: Partial||
|MS-L2-09|System shall be able to process manual override commands as necessary.|Test|Board|TBD|CDR: Partial||

## CDH FDIR (FD-L2)

Traced L1 requirements: CDH-15, CDH-16, CDH-28.

| Name | Description | Method | Level | Pass Criteria | Status | Reason |
|---|---|---|---|---|---|---|
|FD-L2-01|System shall detect fault conditions from telemetry and subsystem reports.|Test|Flatsat|TBD|CDR: Partial|Detection coverage and instrumentation not widely implemented across subsystems|
|FD-L2-02|System shall classify faults based on severity levels.|Unit Test|Unit|TBD|CDR: Met||
|FD-L2-03|System shall flag detected faults.|Unit Test|Unit|TBD|CDR: Met||
|FD-L2-04|System shall log all fault events with timestamps.|Test|Board|TBD|CDR: Partial|Logging does not guarantee a complete record of all fault occurrences|
|FD-L2-05|System shall trigger recovery actions based on fault type.|Test|Flatsat|TBD|CDR: Partial|No unified critical-fault authority ensuring consistent behavior across pathways|
|FD-L2-06|System shall initiate safe mode upon critical fault detection.|Test|Flatsat|TBD|CDR: Partial|Safe-mode entry triggered by watchdog stall only; other critical faults not routed|
|FD-L2-07|System shall detect unstable control behavior in ADCS.|Test|Flatsat|TBD|CDR: Partial|No comprehensive system-level health component; status only exposed via telemetry|
|FD-L2-08|System shall attempt recovery from transient faults.|Test|Flatsat|TBD|CDR: Partial|Transient fault handling limited|
|FD-L2-09|System shall provide fault status to telemetry system.|Unit Test|Unit|TBD|CDR: Partial||

## CDH ADCS Enablement (ADCS-L2)

Traced L1 requirements: CDH-21, CDH-22 (the CDR slide's traced list repeats the
Storage Management line — CDH-8, 12, 14, 16, 27 — which appears to be a
copy-paste carryover; the ADCS content maps to CDH-21 through CDH-30).

| Name | Description | Method | Level | Pass Criteria | Status | Reason |
|---|---|---|---|---|---|---|
|ADCS-L2-01|System shall issue commands to initiate detumble operations.|Integration Test|Board|TBD|CDR: Met||
|ADCS-L2-02|System shall support attitude stabilization routines.|Test|Environmental|TBD|CDR: Met|B-dot detumble implemented using magnetorquers and magnetometers|
|ADCS-L2-03|System may maintain coarse attitude stability.|Test|Environmental|TBD|CDR: Not met|"May" requirement; no control logic beyond B-dot detumble implemented|
|ADCS-L2-04|System may collect and store attitude telemetry.|Test|Board|TBD|CDR: Not met|"May" requirement; not implemented|
|ADCS-L2-05|System may schedule ADCS control loops.|Test|Board|TBD|CDR: Not met|"May" requirement; not implemented, requires nonlinear control logic|
|ADCS-L2-06|System may support downlink of control telemetry.|Test|Flatsat|TBD|CDR: Not met|"May" requirement; not implemented|
