"""
imu_manager_test.py:

Integration tests for the IMU Manager component.

Checks that the LSM6DSO (accelerometer, gyroscope) and LIS2MDL
(magnetometer) are present and producing physical plausible readings. 
Doesn't verify calibration or axis mappings.

  accelerometer  |a| = 9.81 m/s^2 at rest in any orientation
  gyroscope      |w| ~ 0 at rest
  magnetometer   |B| within Earth's magnetic field window
  all three      successive samples differ i.e. not a stale/cached value


Run the board flat and still on a bench.
"""

import math
import time

import pytest
from common import proves_send_and_assert_command
from fprime_gds.common.data_types.event_data import EventData
from fprime_gds.common.testing_fw.api import IntegrationTestAPI

imuManager = "ReferenceDeployment.imuManager"
detumbleManager = "ReferenceDeployment.detumbleManager"

# ----------------------------------------------------------------------
# Envelopes
#
# Deliberately wide to support uncalibrated sensors.
# ----------------------------------------------------------------------

#: Standard gravity. Any orientation at rest must produce this magnitude.
GRAVITY_MS2 = 9.80665

#: Tolerance on |a|. Wide enough for an uncalibrated LSM6DSO zero-g
#: offset (tens of mg) and a non-level bench, tight enough to reject the
#: two actual errors: reporting g instead of m/s^2 (1.0)
#: and a 2x scale error (19.6).
GRAVITY_TOL_MS2 = 1.5

#: Gyroscope magnitude at rest. The LSM6DSO zero-rate offset is a few
#: dps uncalibrated; 0.15 rad/s is about 8.6 dps, which accommodates that
#: without admitting real rotation.
GYRO_REST_MAX_RAD_S = 0.15

#: Earth's field is 0.25-0.65 G depending on location. 
# Buildings and iron-effects affect it too, so bound is widened.
# A tighter bound needs calibration.
FIELD_MIN_GAUSS = 0.15
FIELD_MAX_GAUSS = 1.2

#: Die temperature window. Catches a chip reporting perfect 0.0 (dead channel) or 
# an implausible value (wrong scale or offset in the driver).
DIE_TEMP_MIN_C = 0.0 # fake reading
DIE_TEMP_MAX_C = 60.0 # HOT!

#: Samples taken by the liveness test.
LIVENESS_SAMPLES = 5

#: Gap between liveness samples (s). 
# Longer than the slowest period possible for the accelerometer(12.5 Hz/80 ms), 
# so this guarantees a fresh sample.
LIVENESS_GAP_S = 0.3


# ----------------------------------------------------------------------
# Helpers
# ----------------------------------------------------------------------


def read_vector(
    fprime_test_api: IntegrationTestAPI,
    command: str,
    event: str,
    timeout: int = 3,
) -> tuple:
    """Command one reading and return it as an (x, y, z) tuple."""
    fprime_test_api.clear_histories()

    proves_send_and_assert_command(fprime_test_api, f"{imuManager}.{command}")

    result: EventData = fprime_test_api.assert_event(
        f"{imuManager}.{event}", timeout=timeout
    )

    return (
        float(result.args[0].val),
        float(result.args[1].val),
        float(result.args[2].val),
    )


def magnitude(vector: tuple) -> float:
    """Euclidean norm. Frame-independent, so it needs no axis convention."""
    return math.sqrt(sum(component * component for component in vector))


def collect_vectors(
    fprime_test_api: IntegrationTestAPI,
    command: str,
    event: str,
    count: int = LIVENESS_SAMPLES,
) -> list:
    """Take `count` readings spaced far enough apart to be independent."""
    samples = []
    for index in range(count):
        if index > 0:
            time.sleep(LIVENESS_GAP_S)
        samples.append(read_vector(fprime_test_api, command, event))
    return samples


def assert_no_event(
    fprime_test_api: IntegrationTestAPI, event: str, window_s: int = 5
) -> None:
    """Fail if `event` appears during a window.

    Asserts the absence of a sensor fault event
    """
    fprime_test_api.clear_histories()
    time.sleep(window_s)

    history = fprime_test_api.get_event_test_history().retrieve()
    matches = [
        item
        for item in history
        if event in str(item.get_template().get_full_name())
    ]

    assert not matches, (
        f"{len(matches)} {event} event(s) in {window_s}s. "
        f"First: {matches[0]}"
    )


@pytest.fixture
def magnetorquers_off(fprime_test_api: IntegrationTestAPI, start_gds):
    """Force the magnetorquer coils off for magnetometer |B| tests.
    """
    proves_send_and_assert_command(
        fprime_test_api, f"{detumbleManager}.SET_MODE", ["DISABLED"]
    )
    time.sleep(1)
    yield


# ----------------------------------------------------------------------
# Tests
# ----------------------------------------------------------------------


def test_01_devices_initialised(fprime_test_api: IntegrationTestAPI, start_gds):
    """Both parts passed driver init and are answering."""

    assert_no_event(fprime_test_api, "Lsm6dsoDeviceNotReady")
    assert_no_event(fprime_test_api, "Lis2mdlDeviceNotReady")


def test_02_no_bus_faults(fprime_test_api: IntegrationTestAPI, start_gds):
    """No sensor read is failing on the bus. """

    assert_no_event(fprime_test_api, "SensorReadFailed", window_s=10)


def test_03_accelerometer_reads_one_g(
    fprime_test_api: IntegrationTestAPI, start_gds
):
    """|a| = 9.81 m/s^2 at rest."""
    
    reading = read_vector(fprime_test_api, "GET_ACCELERATION", "AccelerationData")
    measured = magnitude(reading)

    assert abs(measured - GRAVITY_MS2) < GRAVITY_TOL_MS2, (
        f"|a| = {measured:.3f} m/s^2, expected {GRAVITY_MS2:.3f} "
        f"+/- {GRAVITY_TOL_MS2}. Reading: {reading}. "
        f"Near 1.0 means the value is in g rather than m/s^2; near "
        f"{2 * GRAVITY_MS2:.1f} means a 2x scale error; near 0 means the "
        f"sensor is not producing data."
    )


def test_04_gyroscope_at_rest(fprime_test_api: IntegrationTestAPI, start_gds):
    """|w| ~ 0 while the board is still. """

    reading = read_vector(
        fprime_test_api, "GET_ANGULAR_VELOCITY", "AngularVelocityData"
    )
    measured = magnitude(reading)

    assert measured < GYRO_REST_MAX_RAD_S, (
        f"|w| = {measured:.4f} rad/s at rest, expected under "
        f"{GYRO_REST_MAX_RAD_S}. Reading: {reading}. Either the board is "
        f"moving or the zero-rate offset is larger than an uncalibrated "
        f"LSM6DSO should show."
    )


def test_05_magnetometer_field_magnitude(
    fprime_test_api: IntegrationTestAPI, start_gds, magnetorquers_off
):
    """|B| within Earth's envelope."""

    reading = read_vector(
        fprime_test_api, "GET_MAGNETIC_FIELD", "MagneticFieldData"
    )
    measured = magnitude(reading)

    assert FIELD_MIN_GAUSS < measured < FIELD_MAX_GAUSS, (
        f"|B| = {measured:.4f} G, expected {FIELD_MIN_GAUSS}-"
        f"{FIELD_MAX_GAUSS} G. Reading: {reading}. Earth's field is "
        f"0.25-0.65 G. Check for magnets, that the magnetorquers are off, "
        f"and that all three axes are reporting."
    )


@pytest.mark.parametrize(
    "command,event,label",
    [
        ("GET_ACCELERATION", "AccelerationData", "accelerometer"),
        ("GET_ANGULAR_VELOCITY", "AngularVelocityData", "gyroscope"),
        ("GET_MAGNETIC_FIELD", "MagneticFieldData", "magnetometer"),
    ],
)
def test_06_readings_are_live_not_cached(
    fprime_test_api: IntegrationTestAPI, start_gds, command, event, label
):
    """Successive readings differ, so the sensor is actually sampling."""

    samples = collect_vectors(fprime_test_api, command, event)
    distinct = set(samples)

    assert len(distinct) >= 2, (
        f"All {len(samples)} {label} readings were bit-identical: "
        f"{samples[0]}. A live sensor's noise exceeds its LSB, so this "
        f"means the reading is not fresh. Check SensorReadFailed events "
        f"and the configured ODR."
    )


def test_07_die_temperature_plausible(
    fprime_test_api: IntegrationTestAPI, start_gds
):
    """The LSM6DSO on-die temperature is a plausible value"""

    fprime_test_api.clear_histories()

    fprime_test_api.send_command(f"{imuManager}.GET_DIE_TEMPERATURE")
    time.sleep(2)

    history = fprime_test_api.get_event_test_history().retrieve()
    unavailable = [
        item
        for item in history
        if "DieTemperatureUnavailable" in str(item.get_template().get_full_name())
    ]
    if unavailable:
        pytest.skip(
            "Die temperature channel is not in this build. Set "
            "CONFIG_LSM6DSO_ENABLE_TEMP=y to enable it."
        )

    result: EventData = fprime_test_api.assert_event(
        f"{imuManager}.DieTemperatureData", timeout=3
    )
    temperature_c = float(result.args[0].val)

    assert DIE_TEMP_MIN_C < temperature_c < DIE_TEMP_MAX_C, (
        f"Die temperature {temperature_c:.2f} C is outside "
        f"{DIE_TEMP_MIN_C}-{DIE_TEMP_MAX_C} C. A value near -273 means "
        f"the channel is returning a raw zero; a wild value means a "
        f"driver scale or offset problem."
    )


def test_08_telemetry_tick(
    fprime_test_api: IntegrationTestAPI, start_gds
):
    """All three channels update on their rate tick """

    for channel in ("Acceleration", "AngularVelocity", "MagneticField"):
        result = fprime_test_api.await_telemetry(
            f"{imuManager}.{channel}", timeout=10
        )
        assert result is not None, (
            f"No {channel} telemetry within 10 s. The component is not "
            f"being driven by the rate group, or the channel is missing "
            f"from the Imu telemetry packet."
        )


def test_09_sampling_frequency_parameter(
    fprime_test_api: IntegrationTestAPI, start_gds
):
    """
    A changed sampling frequency parameter reaches the sensor and telemetry.

    Restores the defaults before exit.
    """

    try:
        fprime_test_api.clear_histories()
        proves_send_and_assert_command(
            fprime_test_api,
            f"{imuManager}.MAGNETOMETER_SAMPLING_FREQUENCY_PRM_SET",
            ["SF_20Hz"],
        )

        result = fprime_test_api.await_telemetry(
            f"{imuManager}.MagnetometerSamplingFrequency", timeout=10
        )
        assert result is not None, "No MagnetometerSamplingFrequency telemetry"
        assert "SF_20Hz" in str(result.get_val()), (
            f"Parameter set to SF_20Hz but telemetry reports "
            f"{result.get_val()}"
        )

        assert_no_event(
            fprime_test_api, "MagnetometerSamplingFrequencyNotConfigured"
        )
    finally:
        proves_send_and_assert_command(
            fprime_test_api,
            f"{imuManager}.MAGNETOMETER_SAMPLING_FREQUENCY_PRM_SET",
            ["SF_100Hz"],
        )
