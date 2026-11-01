# Gnc::Environment::SolarEphemeris

Solar direction, eclipse state and beta angle. Passive, driven by the arrival of `Gnc::OrbitState`.

Produces a usable sun vector as long as the OrbitState timestamp is valid.

## Requirements

| Name | Description | Method | Level | Pass Criteria | Status | Reason |
|---|---|---|---|---|---|---|
|SUN0001| SolarEphemeris shall evaluate once per arrival of an OrbitState and at no other time|Unit Test|Host|Three `orbitIn` invocations produce exactly three `solarOut` invocations; no evaluation occurs without an arrival|||
|SUN0002|The SolarEphemeris shall suspend evaluation when the OrbitState carries no usable time|Unit Test|Host|`orbitIn` with `timeUsable = false` emits `TimeMissing`, publishes `validity = NO_TIME`, `directionUsable = false`, `geometryUsable = false`, and writes no `SunUnitTeme` telemetry|||
|SUN0003|The SolarEphemeris shall publish a geocentric Sun direction when time is usable but position is not|Unit Test|Host|`orbitIn` with `timeUsable = true`, `positionUsable = false` emits `GeocentricFallback`, publishes `validity = GEOCENTRIC` with `directionUsable = true`, `geometryUsable = false`, and a unit-length `sunUnitTeme`|||
|SUN0004|The SolarEphemeris shall publish direction, illumination and beta angle when the OrbitState is fully usable|Unit Test|Host|`validity = VALID`, both usable flags true, `sunRangeKm` within [147.0e6, 152.5e6], `betaRad` within [-pi/2, pi/2]|||
|SUN0005|The SolarEphemeris shall not emit shadow or beta telemetry when geometry is unavailable|Unit Test|Host|On the `GEOCENTRIC` and `NO_TIME` paths, `ASSERT_TLM_BetaDeg_SIZE(0)` and `ASSERT_TLM_Illumination_SIZE(0)`|||
|SUN0006|The interpolated Sun direction shall agree with a direct model evaluation across a full segment|Unit Test|Host|Sweeping 0 to 60 s in 1 s steps at the default `segmentSec = 60`, the angle between the SLERP result and a fresh `sunTemeAt` evaluation stays below 1e-4 deg at every step|||
|SUN0007|The interpolation cache shall re-anchor when the clock steps backwards|Unit Test|Host|After seeding at t, an evaluation at t minus 10 s produces a direction matching a direct evaluation at t minus 10 s to within 1e-4 deg, confirming the segment was rebuilt rather than extrapolated|||
|SUN0008|The interpolation cache shall re-anchor when the evaluation time passes the segment end|Unit Test|Host|An evaluation at t plus 90 s with `segmentSec = 60` sets `prevJdUt1` to that instant; the fraction never saturates at 1.0|||
|SUN0009|The SolarEphemeris shall bypass interpolation when the segment length is zero or negative|Unit Test|Host|With `SUN_SEGMENT_SEC = 0`, two evaluations one second apart both match direct evaluations exactly and the cache stays unseeded|||
|SUN0010|The SolarEphemeris shall discard the interpolation cache on command|Unit Test|Host|`RESYNC_SUN` returns `OK` and clears `seeded`; the next evaluation rebuilds both segment endpoints|||
|SUN0011|The SolarEphemeris shall emit eclipse events only on an illumination transition|Unit Test|Host|Ten consecutive sunlit evaluations emit no eclipse events; one transition to umbra emits exactly one `EclipseEntry`; returning to sunlit emits exactly one `EclipseExit`|||
|SUN0012|The SolarEphemeris shall not emit an eclipse event on the first evaluation after boot|Unit Test|Host|The first `orbitIn` arrival, whatever the illumination, emits neither `EclipseEntry` nor `EclipseExit`|||
|SUN0013|The parallax correction shall stay below the solar model's own accuracy in LEO|Unit Test|Host|At 400 km altitude, the angle between the geocentric and spacecraft-centred Sun vectors is non-zero and below 0.0027 deg|||
|SUN0014|The solar direction shall match published ephemeris to the model's stated accuracy|Oracle Test|Host|Solar declination is +23.44 deg at the 2026 June solstice, -23.44 deg at the December solstice, and within 0.02 deg of zero at both equinoxes; Earth-Sun range is at its minimum in early January and maximum in early July|||
|SUN0015|The beta angle shall follow the stated sign convention|Oracle Test|Host|For an orbit whose angular momentum vector points toward the Sun, beta is +90 deg; reversing the velocity vector gives -90 deg; a Sun vector in the orbit plane gives 0 deg|||
|SUN0016|The SolarEphemeris shall fall back to the FPP-declared default when `SUN_SEGMENT_SEC` does not read VALID|Unit Test|Host|With no parameter set, `currentConfig()` returns `segmentSec = 60.0`|||
|SUN0017|The sunRefOut valid flag shall track directionUsable, not full validity|Unit Test|Host|On the `GEOCENTRIC` path the published `VectorSample` carries `valid = true` and `frame = TEME`, so attitude determination can still use it|||
|SUN0018|The SolarEphemeris shall track the eclipse cycle over a real orbit|Integration Test|Board|With a current LEO TLE loaded, over 100 minutes the ground observes at least one `EclipseEntry` and one `EclipseExit`, and `Illumination` reports `PENUMBRA` between the `UMBRA` and `SUNLIT` runs|||

## Port Descriptions

| Name | Type | Description |
|---|---|---|
|orbitIn|Gnc.OrbitStateSend (guarded)|Orbit state. Arrival drives one evaluation.|
|sunRefOut|Gnc.VectorSampleSend|Sun direction vector, TEME|
|solarOut|Gnc.SolarStateSend|Full solar geometry|
|cmdIn / cmdRegOut / cmdResponseOut|Fw.Cmd*|Command dispatch|
|logOut / logTextOut|Fw.Log*|Events|
|tlmOut|Fw.Tlm|Telemetry|
|timeCaller|Fw.Time|Event and telemetry stamping only, never computation|
|prmGetOut / prmSetOut|Fw.Prm*|Parameters|

## Parameters

| Name | Type | Default | Description |
|---|---|---|---|
|SUN_SEGMENT_SEC|F64|60.0|Length of a solar interpolation segment, seconds. Set 0 to disable interpolation and evaluate the model on every call.|

## Commands

| Name | Opcode | Description |
|---|---|---|
|RESYNC_SUN|0x10|Force a full recomputation on the next arrival, bypassing the interpolation cache.|

## Events

| Name | ID | Severity | Throttle | Description |
|---|---|---|---|---|
|EclipseEntry|0x00|activity low|-|Entered the Earth's shadow|
|EclipseExit|0x01|activity low|-|Exited the Earth's shadow|
|GeocentricFallback|0x02|warning low|3|Running without an orbit; shadow and beta unavailable|
|TimeMissing|0x03|warning high|3|Orbit state carried no usable time|

## Telemetry

| Name | ID | Type | Description |
|---|---|---|---|
|SunUnitTeme|0x00|Gnc.Vec3f|Unit vector toward the Sun, TEME|
|SunRangeKm|0x01|F64|Range to the Sun|
|BetaDeg|0x02|F32|Sun elevation above the orbit plane|
|Illumination|0x03|Gnc.IlluminationState|Current illumination state|
|GeometryUsable|0x04|bool|False when running without an orbit|
|Validity|0x05|Gnc.SolarValidity|Diagnostic reason for the current solar state|

## Component States

| Name | Description |
|---|---|
|m_sunSegment|`SunSegment`. Endpoints of the current interpolation segment, plus the `seeded` flag.|
|m_lastIllum|Previous illumination, for edge-triggered eclipse events.|
|m_illumSeeded|Suppresses an eclipse event on the first evaluation after boot.|

## Design

### Framework and logic split

The component is the framework layer: the parameter read, the interpolation cache's lifetime, eclipse edge detection, events and telemetry. All solar geometry lives in `SunModel.{hpp,cpp}`, which has no F Prime dependency.

The interpolation cache is *algorithm* state, so it lives in `SunSegment` — a caller-owned struct passed by reference, keeping `solveSolar` a pure function of its inputs. Eclipse edge detection is *publication* state and stays in the component. That division is what makes SUN0006 through SUN0010 testable without the F Prime harness.

### Two stages

The work splits by what it needs, and the split is the reason the component survives a missing TLE:

| Stage | Needs | Produces |
|---|---|---|
|1. Sun direction|A time only|`sunUnitTeme`, `sunRangeKm` |
|2. Geometry|A position as well|`illumination`, `betaRad`, parallax-corrected direction|

A caller with a clock but no orbit gets stage 1 and a `GEOCENTRIC` result. That is still usable for attitude determination: the parallax being skipped is at most 0.0027 deg in LEO, well under the solar model's own 0.01 deg. Shadow and beta genuinely cannot be computed without a position, so `geometryUsable` stays false rather than shipping a guessed number behind a single validity flag.

This is why `SolarState` carries two usability flags rather than one. Collapsing them would force the component either to suppress a perfectly good Sun vector or to publish a meaningless beta angle.

### Interpolation

Stage 1 is expensive relative to how fast the Sun moves. The solar model plus the nutation series is several dozen transcendentals in F64 on a soft-float path; the Sun sweeps about 0.00066 deg in 60 seconds. So the model is evaluated at both ends of a segment and the result great-circle interpolated across it, with the range interpolated linearly.

The cache re-anchors on three conditions: it holds no segment, the clock stepped backwards, or the evaluation ran off the segment end. Each is a separate requirement because each is a separate branch, and a backward clock step is a real scenario on a spacecraft whose RTC is corrected from the ground.

### Diagrams

Add sequence and state diagrams here.

## Change Log

| Date | Description |
|---|---|
| TBD | Initial draft |
