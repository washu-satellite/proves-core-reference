# Gnc::Environment::OrbitPropagator

Head of the GNC chain. Reads the clock, builds every time scale, propagates the loaded TLE with SGP4, derives the ground track, and broadcasts a `Gnc::OrbitState` once per tick.

The only component in the GNC subsystem that reads the time port for computation.

## Requirements

| Name | Description | Method | Level | Pass Criteria | Status | Reason |
|---|---|---|---|---|---|---|
|ORB0001|The OrbitPropagator shall reject a time source whose time base is not wall clock|Unit Test|Host|With `setTestTime` at `TB_NONE` or `TB_PROC_TIME`, one `run` tick emits `TimeMissing`, publishes `OrbitState` with `validity = NO_TIME`, `timeUsable = false`, `positionUsable = false`, and writes no position telemetry|||
|ORB0002|The OrbitPropagator shall accept both `TB_WORKSTATION_TIME` and `TB_SC_TIME` as usable wall clock|Unit Test|Host|A tick at either time base with no TLE loaded reaches the `NO_TLE` path, not the `NO_TIME` path|||
|ORB0003|The OrbitPropagator shall emit an OrbitState on every tick, including every failure path|Unit Test|Host|`ASSERT_from_orbitOut_SIZE(1)` after each of: no time, no TLE, SGP4 error, stale TLE, fully valid|||
|ORB0004|The OrbitPropagator shall publish valid time products when no TLE is loaded|Unit Test|Host|With no TLE, `OrbitState` carries `timeUsable = true`, non-zero `jdUt1`, `jdTt`, `gmstRad`, and `positionUsable = false`; `TleMissing` is emitted|||
|ORB0005|The OrbitPropagator shall install an uplinked TLE that parses successfully|Integration Test|Board|`LOAD_TLE` with a current ISS element set returns `OK` and emits `TleAccepted` carrying the NORAD designator and an epoch JD within 30 days of the upload date|||
|ORB0006|The OrbitPropagator shall reject a malformed TLE without disturbing the loaded TLE|Unit Test|Host|After a valid load, `LOAD_TLE` with a corrupted checksum returns `VALIDATION_ERROR`, emits `TleRejected`, and the next tick still propagates the previously loaded element set|||
|ORB0007|The OrbitPropagator shall stop publishing position on command while continuing to publish time|Unit Test|Host|`CLEAR_TLE` returns `OK`, emits `TleCleared`; the next tick reports `NO_TLE` with `timeUsable = true` and `positionUsable = false`|||
|ORB0008|The OrbitPropagator shall report an SGP4 propagation failure with its error code and time from epoch|Unit Test|Host|A TLE driven to a decayed or non-convergent state emits `Sgp4Failure` with a non-zero code and the matching `minsFromEpoch`; `validity = PROP_ERROR`, `positionUsable = false`|||
|ORB0009|The OrbitPropagator shall report a TLE older than `MAX_TLE_AGE_DAYS` as stale while still publishing its position|Unit Test|Host|With the clock set 8 days past epoch and the parameter at its 7.0 default, `validity = STALE`, `positionUsable = true`, and `TleStale` is emitted with the age|||
|ORB0010|The OrbitPropagator shall compute GMST once per cycle and carry it in OrbitState|Inspection|Host|`gmst1982Rad` is called exactly once in `solveOrbit`; no downstream GNC component calls it. Confirmed by source grep across `Gnc/`|||
|ORB0011|The OrbitPropagator shall compute the geodetic ground track once per cycle and carry it in OrbitState|Inspection|Host|`ecefToGeodetic` is called exactly once in `solveOrbit`; `MagneticFieldModel` reads `altKm` from `OrbitState` rather than recomputing|||
|ORB0012|The OrbitPropagator shall fall back to the FPP-declared default for any parameter that does not read VALID|Unit Test|Host|With no parameter set (the `NullPrmDb` condition that flies), `currentConfig()` returns `leapSec = 37.0`, `dut1Sec = 0.0`, `maxTleAgeDays = 7.0`, `utcOffsetSec = 0.0`|||
|ORB0013|The OrbitPropagator shall apply `UTC_OFFSET_SEC` to the incoming POSIX seconds before deriving time scales|Unit Test|Host|Setting the parameter to 18.0 shifts `jdUt1` by exactly 18/86400 days relative to the same tick at 0.0|||
|ORB0014|The OrbitPropagator shall derive UT1 and TT from UTC to published accuracy|Oracle Test|Host|At 2000-01-01T12:00:00Z with `dut1 = 0`, `leapSec = 32`: `jdUt1` equals 2451545.0 within 1e-9 days and TT minus UT1 equals 64.184 s within 1e-6 s|||
|ORB0015|The OrbitPropagator shall produce SGP4 state vectors matching the reference implementation|Oracle Test|Host|Propagating the Vallado SGP4 verification TLE (catalog 00005) to +360 min reproduces the published position within 1 m and velocity within 1 mm/s|||
|ORB0016|The OrbitPropagator shall broadcast OrbitState to every connected output port instance|Unit Test|Host|With both `orbitOut` instances connected, one tick produces two invocations carrying identical payloads|||
|ORB0017|The OrbitPropagator shall report the wall time consumed by a tick including downstream fan-out|Integration Test|Board|`CycleUsec` is non-zero and the reported value falls when `magneticFieldModel` is disconnected from `orbitOut`, confirming it covers the synchronous consumers|||
|ORB0018|The OrbitPropagator shall complete one cycle within the rate group period on flight hardware|Integration Test|Board|At 1 Hz on the RP2350 with a loaded TLE and both consumers connected, `CycleUsec` stays below 200000 over a 10-minute run|||

## Port Descriptions

| Name | Type | Description |
|---|---|---|
|run|Svc.Sched|Rate group tick. One tick is one propagation cycle.|
|orbitOut|Gnc.OrbitStateSend `[2]`|Broadcasts the orbit state. Synchronous on this component's thread. Index 0 is the magnetic field model, index 1 the solar ephemeris.|
|cmdIn / cmdRegOut / cmdResponseOut|Fw.Cmd*|Command dispatch|
|logOut / logTextOut|Fw.Log*|Events|
|tlmOut|Fw.Tlm|Telemetry|
|timeCaller|Fw.Time|Clock read. The only computational clock read in GNC.|
|prmGetOut / prmSetOut|Fw.Prm*|Parameters|

## Parameters

| Name | Type | Default | Description |
|---|---|---|---|
|LEAP_SEC|F64|37.0|TAI minus UTC, seconds. Bump when a leap second is announced.|
|DUT1_SEC|F64|0.0|UT1 minus UTC, seconds, from IERS Bulletin A. Always under 0.9 s, so leaving it at 0 costs at most 0.004 deg of Earth rotation.|
|MAX_TLE_AGE_DAYS|F32|7.0|TLE age beyond which the state is reported STALE.|
|UTC_OFFSET_SEC|F64|0.0|Offset added to the `Fw::Time` seconds field to obtain UTC POSIX seconds. For a GPS-time or TAI-time source.|

## Commands

| Name | Opcode | Description |
|---|---|---|
|LOAD_TLE|0x10|Upload a two-line element set. Both lines must be exactly 69 characters.|
|CLEAR_TLE|0x11|Discard the current TLE. Time output continues.|

## Events

| Name | ID | Severity | Throttle | Description |
|---|---|---|---|---|
|TleAccepted|0x00|activity high|-|TLE parsed and propagator initialized|
|TleRejected|0x01|warning high|-|TLE rejected; previously loaded TLE untouched|
|Sgp4Failure|0x02|warning high|5|SGP4 propagation failed this tick|
|TleStale|0x03|warning low|3|TLE older than `MAX_TLE_AGE_DAYS`|
|TleMissing|0x04|warning low|3|No TLE loaded|
|TimeMissing|0x05|warning high|3|Time source reported invalid|
|TleCleared|0x06|activity high|-|TLE cleared by command|

## Telemetry

| Name | ID | Type | Description |
|---|---|---|---|
|PosTemeKm|0x00|Gnc.Vec3d|Spacecraft position, TEME, km|
|VelTemeKmS|0x01|Gnc.Vec3d|Spacecraft velocity, TEME, km/s|
|LatDeg|0x02|F32|Sub-satellite geodetic latitude|
|LonDeg|0x03|F32|Sub-satellite geodetic longitude|
|AltKm|0x04|F32|Geodetic altitude above the WGS-84 ellipsoid|
|TleAgeDays|0x05|F32|Age of the loaded TLE|
|Validity|0x06|Gnc.OrbitValidity|Validity of the current state|
|GmstDeg|0x07|F32|Greenwich Mean Sidereal Time|
|CycleUsec|0x08|U32|Wall time consumed by the last tick|

## Component States

| Name | Description |
|---|---|
|m_sgp4|`Sgp4Propagator`. Holds the parsed orbital record and its validity flag between cycles.|

## Design

### Framework and logic split

The component is the framework layer only: the time port, parameters, commands, telemetry, events, and the execution budget. All astrodynamics lives in `Sgp4Propagator.{hpp,cpp}`, which has no F Prime dependency and builds standalone on a workstation. That file follows the same pattern as `TriadSolver`: a config struct with flight-safe defaults, an output struct, a result enum mirroring the FPP `Gnc::OrbitValidity`, and one entry point.

```
run_handler
  ├─ acquireTime()       Fw::Time -> POSIX seconds, gated on time base   [framework]
  ├─ currentConfig()     parameters -> OrbitConfig                       [framework]
  ├─ solveOrbit()        POSIX seconds + TLE -> OrbitFix                 [logic]
  │    ├─ stage 1  time scales, GMST         (always runs)
  │    ├─ stage 2  SGP4 propagation          (needs a TLE)
  │    └─ stage 3  ground track              (needs stage 2)
  ├─ fillState()         OrbitFix -> Gnc::OrbitState                     [framework]
  ├─ publish()           telemetry                                        [framework]
  └─ emit()              broadcast, synchronous                           [framework]
```

The clock read stays on the framework side deliberately. Deciding whether an `Fw::TimeBase` means real time on this spacecraft is a judgement about the deployment, not about astrodynamics, so `solveOrbit` takes a plain `double` and the gate lives in `acquireTime`.

### Why time is centralized here

A bad timestamp silently corrupts every output downstream, and the sensitivities differ by orders of magnitude. A one-minute clock error is roughly 0.0007 deg of Sun direction, which is negligible, but 0.25 deg of Earth rotation and about 450 km of along-track position, which is not. Deriving time scales in more than one place is what produced the 16.1 deg Earth rotation error in the pre-refactor code, so GMST, the geodetic conversion, and the UT1/TT scales are computed exactly once and carried in `OrbitState`.

### Graceful degradation

The component degrades in steps rather than all at once, and each step is distinguishable downstream:

| Validity | timeUsable | positionUsable | Downstream effect |
|---|---|---|---|
|NO_TIME|false|false|Solar ephemeris suspends; magnetic field model republishes last field as invalid|
|NO_TLE|true|false|Solar ephemeris falls back to a geocentric Sun vector; magnetic field model has no position|
|PROP_ERROR|true|false|Same as NO_TLE|
|STALE|true|true|Full chain runs; ground is warned the solution is drifting|
|VALID|true|true|Nominal|

Every path emits, so a consumer always learns *why* it did not get a position rather than simply seeing nothing arrive.

### Execution budget

`orbitOut` fans out synchronously in index order on this component's thread, so `CycleUsec` measures the propagator plus the WMM evaluation plus the solar model — the whole GNC chain for that cycle. That is the intended reading, and it is the channel to watch when deciding whether 1 Hz is sustainable on the RP2350's single-precision FPU.

### Diagrams

Add sequence and state diagrams here.

## Unit Tests

| Name | Description | Output | Coverage |
|---|---|---|---|

## Change Log

| Date | Description |
|---|---|
| TBD | Initial draft |
