# Gnc::Adcs::AttitudeDetermination

TRIAD: deterministic attitude determination from a pair of vector observations, a measured sun direction and a measured magnetic field direction, each matched against a modeled reference.

Passive with guarded inputs.

## Requirements

| Name | Description | Method | Level | Pass Criteria | Status | Reason |
|---|---|---|---|---|---|---|
|ATT0001|The AttitudeDetermination shall latch each vector input on arrival and evaluate only on a rate group tick|Unit Test|Host|Five arrivals on `magBodyIn` between two `run` ticks produce exactly one `attitudeOut` invocation, carrying the most recent sample|||
|ATT0002|The AttitudeDetermination shall publish on every evaluation, including every rejection|Unit Test|Host|`ASSERT_from_attitudeOut_SIZE(1)` after a tick on each of: all inputs stale, frame mismatch, colinear, geometry mismatch, degenerate, and fully valid|||
|ATT0003|The AttitudeDetermination shall hold the last valid quaternion in the published solution when a solve is rejected|Unit Test|Host|After a valid solve, a tick that rejects publishes the same `q` as the previous cycle with `valid = false` and a non-OK status|||
|ATT0004|The AttitudeDetermination shall discard a body sample older than `MAX_BODY_AGE_MS`|Unit Test|Host|With the clock advanced 600 ms past a sun body sample and the parameter at its 500 default, the tick reports `SUN_UNAVAILABLE` and emits `SampleStale` naming `sunBody` with the measured age|||
|ATT0005|The AttitudeDetermination shall discard a reference sample older than `MAX_REF_AGE_MS`|Unit Test|Host|With the clock advanced 3500 ms past a magRef sample, the tick reports `REFERENCE_UNAVAILABLE` and emits `SampleStale` naming `magRef`|||
|ATT0006|The AttitudeDetermination shall fall back to cycle counting when a sample age cannot be differenced|Unit Test|Host|With the latch stamped `TB_NONE` or a mismatched time base against the current time, `UsingCycleFallback` is true, `SampleStale` reports age 0, and the sample is discarded after `MAX_SAMPLE_AGE_CYCLES` ticks|||
|ATT0007|The AttitudeDetermination shall treat a negative wall clock difference as an unknown age rather than a fresh sample|Unit Test|Host|A latch stamped later than the current time sets `ageKnown = false` and routes to the cycle fallback, not to a zero age|||
|ATT0008|The AttitudeDetermination shall reject a reference sample tagged with an unexpected frame|Unit Test|Host|A `sunRefIn` sample tagged `FrameId::J2000` with `REFERENCE_FRAME` at its TEME default reports `FRAME_MISMATCH` and emits `FrameMismatch` naming the input, the frame received, and the frame required|||
|ATT0009|The AttitudeDetermination shall reject a solve whose observations are closer than `MIN_SEPARATION_DEG` to parallel|Unit Test|Host|Body vectors 3 deg apart with the parameter at its 5.0 default report `BODY_COLINEAR` and emit `VectorsColinear` with the measured separation|||
|ATT0010|The AttitudeDetermination shall reject a solve whose observations are closer than `MIN_SEPARATION_DEG` to antiparallel|Unit Test|Host|Body vectors 178 deg apart report `BODY_COLINEAR`; the gate is on `sin(separation)`, not the angle|||
|ATT0011|The AttitudeDetermination shall reject a solve when the body and reference pair separations disagree|Unit Test|Host|Body pair at 40 deg against a reference pair at 50 deg, with `MAX_GEOMETRY_ERR_DEG` at its 5.0 default, reports `GEOMETRY_MISMATCH` and emits the event with the residual|||
|ATT0012|The AttitudeDetermination shall reject a solve on a zero-length or non-finite input vector|Unit Test|Host|A zero vector, a NaN component, and an Inf component each report `DEGENERATE_INPUT`|||
|ATT0013|TRIAD shall recover a known attitude exactly from noise-free observations|Unit Test|Host|Rotating reference vectors by a known quaternion and solving reproduces that quaternion within 1e-5 deg, for at least 100 random attitudes|||
|ATT0014|TRIAD shall satisfy the primary observation exactly when the secondary is corrupted|Unit Test|Host|With 10 deg of error injected into the secondary observation, the angle between the rotated primary reference and the measured primary stays below 1e-5 deg|||
|ATT0015|The published quaternion shall rotate the reference frame to the body frame|Unit Test|Host|For a known attitude, `q * refVector` reproduces the body vector and `q.conjugate() * refVector` does not. The convention is checked explicitly, not implied by the fixture|||
|ATT0016|The published quaternion shall use scalar-last storage with identity `[0,0,0,1]`|Unit Test|Host|A no-rotation case publishes `AttQuat` equal to `[0,0,0,1]` within 1e-6|||
|ATT0017|The AttitudeDetermination shall maintain quaternion sign continuity across a scalar-component zero crossing|Unit Test|Host|Stepping an attitude through the `w = 0` boundary in 0.5 deg increments, the dot product of consecutive published quaternions stays positive at every step|||
|ATT0018|The AttitudeDetermination shall select the primary observation from `PRIMARY_VECTOR`|Unit Test|Host|With the parameter set to `SUN`, ATT0014 holds for the sun leg; set to `MAG`, it holds for the magnetic leg|||
|ATT0019|The AttitudeDetermination shall fall back to the FPP-declared defaults for every parameter that does not read VALID|Unit Test|Host|With no parameter set, which is the condition that flies under `NullPrmDb`, `currentConfig()` yields `PRIMARY_VECTOR = MAG`, `REFERENCE_FRAME = TEME`, 5.0 deg separation, 5.0 deg geometry error, 500 ms body age, 3000 ms reference age, 20 cycles|||
|ATT0020|The AttitudeDetermination shall emit SolutionLost and SolutionRestored only on transitions|Unit Test|Host|Valid, then five rejecting ticks, then valid: exactly one `SolutionLost` carrying the first rejecting status, and exactly one `SolutionRestored`|||
|ATT0021|The AttitudeDetermination shall stamp the published solution with the epoch of the oldest contributing input|Unit Test|Host|With four latches at known differing stamps, the published `stamp` equals the oldest of them|||
|ATT0022|The AttitudeDetermination shall report the age of the oldest input used|Unit Test|Host|`OldestInputAgeMs` equals the largest known age among the four latches and is written on every tick, including rejections|||
|ATT0023|The AttitudeDetermination shall force an evaluation on command using the latched samples|Unit Test|Host|`SOLVE_NOW` returns `OK` and produces one `attitudeOut` invocation with no `run` tick|||
|ATT0024|The AttitudeDetermination shall verify the solver against a built-in known case on command|Integration Test|Board|`SELF_TEST` emits `SelfTestResult` with `passed = true` and an error under 0.001 deg on the RP2350, confirming the FPU and compiler flags|||
|ATT0025|The SELF_TEST command shall not disturb latched flight data|Unit Test|Host|Running `SELF_TEST` between two ticks leaves the published solution of the second tick identical to a run without it|||
|ATT0026|The AttitudeDetermination shall produce solutions from the live GNC chain|Integration Test|Board|With a TLE loaded and the magnetometer and sun sensors active, `Status` reports `OK` and `SolutionCount` increments at the rate group rate; `SeparationDeg` tracks the orbit geometry|||
|ATT0027|The AttitudeDetermination shall degrade rather than stop when observation geometry becomes poor|Integration Test|Board|Over one orbit the ground observes `SeparationDeg` passing below 5 deg, `Status` changing to `BODY_COLINEAR`, `SolutionLost` emitted once, and recovery with `SolutionRestored` as the geometry opens up|||

## Port Descriptions

| Name | Type | Description |
|---|---|---|
|sunBodyIn|Gnc.VectorSampleSend (guarded)|Measured sun direction, body frame, from the sun sensors|
|magBodyIn|Gnc.VectorSampleSend (guarded)|Measured magnetic field direction, body frame, from the magnetometer|
|sunRefIn|Gnc.VectorSampleSend (guarded)|Modelled sun direction, inertial frame, from SolarEphemeris|
|magRefIn|Gnc.VectorSampleSend (guarded)|Modelled magnetic field direction, inertial frame, from MagneticFieldModel|
|run|Svc.Sched (guarded)|Rate group tick. One tick is one TRIAD evaluation.|
|attitudeOut|AttitudeSolutionSend|Publishes the attitude estimate to the controller or estimator|
|cmdIn / cmdRegOut / cmdResponseOut|Fw.Cmd*|Command dispatch|
|logOut / logTextOut|Fw.Log*|Events|
|tlmOut|Fw.Tlm|Telemetry|
|timeCaller|Fw.Time|Staleness judgement and stamping|
|prmGetOut / prmSetOut|Fw.Prm*|Parameters|

## Parameters

| Name | Type | Default | Description |
|---|---|---|---|
|PRIMARY_VECTOR|PrimaryVector|MAG|Which observation TRIAD satisfies exactly|
|REFERENCE_FRAME|Gnc.FrameId|TEME|The inertial frame reference vectors must be tagged with|
|MIN_SEPARATION_DEG|F32|5.0|Reject a solve if the observations are separated by less than this, or more than 180 minus this|
|MAX_GEOMETRY_ERR_DEG|F32|5.0|Reject if body-pair and reference-pair separations disagree by more than this|
|MAX_BODY_AGE_MS|U32|500|Discard a sensor sample older than this|
|MAX_REF_AGE_MS|U32|3000|Discard a model sample older than this|
|MAX_SAMPLE_AGE_CYCLES|U32|20|Fallback age limit in rate group cycles, used only when a timestamp cannot be differenced|

## Commands

| Name | Opcode | Description |
|---|---|---|
|SOLVE_NOW|0x10|Force one TRIAD evaluation immediately using the latched samples|
|SELF_TEST|0x11|Run TRIAD against a built-in synthetic case with a known answer|

## Events

| Name | ID | Severity | Throttle | Description |
|---|---|---|---|---|
|SolutionLost|0x00|warning high|-|TRIAD stopped producing solutions|
|SolutionRestored|0x01|activity high|-|Solutions resumed|
|VectorsColinear|0x02|warning low|5|Observation geometry too close to parallel or antiparallel|
|GeometryMismatch|0x03|warning high|5|Measured and modelled geometry disagree|
|SampleStale|0x04|warning low|5|An input has not been refreshed recently enough|
|FrameMismatch|0x05|warning high|3|A producer tagged a sample with an unexpected frame|
|SelfTestResult|0x06|activity high|-|Self test result|

## Telemetry

| Name | ID | Type | Description |
|---|---|---|---|
|AttQuat|0x00|Quatf|Attitude estimate, `[x,y,z,w]`, reference frame to body frame|
|Status|0x01|TriadStatus|Result of the most recent evaluation|
|SeparationDeg|0x02|F32|Angle between the two observations|
|GeometryErrDeg|0x03|F32|Body-versus-reference angle disagreement|
|SolutionCount|0x04|U32|Count of successful solves|
|SolveRejectCount|0x05|U32|Count of rejected solves|
|OldestInputAgeMs|0x06|U32|Age of the oldest input used in the last attempt|
|UsingCycleFallback|0x07|bool|True when staleness is judged by cycle count|

## Component States

| Name | Description |
|---|---|
|m_sunBody, m_magBody, m_sunRef, m_magRef|`LatchedSample`. Most recent sample on each input, with its stamp, frame, validity and cycle age.|
|m_lastQuat|Previous solution, used for sign continuity and republished when a solve is rejected.|
|m_lastSolveValid|Previous validity, for edge-triggered `SolutionLost` and `SolutionRestored`.|
|m_solutionCount, m_rejectCount|Cumulative counters.|

## Design

### Framework and logic split

### Diagrams

Add sequence and state diagrams here.

## Unit Tests

| Name | Description | Output | Coverage |
|---|---|---|---|

## Change Log

| Date | Description |
|---|---|
| TBD | Initial draft |
