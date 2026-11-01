"""
imu_bringup_manual_test.py:

Prompted IMU tests. These need the board to be physically moved/rotated.

    run with: pytest test/int/imu_manual_test.py -s -m manual

Allows axis mapping for the IMU and Magnetometer.
"""

import math
import time

import pytest
from common import proves_send_and_assert_command
from fprime_gds.common.data_types.event_data import EventData
from fprime_gds.common.testing_fw.api import IntegrationTestAPI

pytestmark = pytest.mark.manual

imuManager = "ReferenceDeployment.imuManager"
detumbleManager = "ReferenceDeployment.detumbleManager"

GRAVITY_MS2 = 9.80665
GRAVITY_TOL_MS2 = 1.5

#: A face-down axis should report most of gravity acceleration.
DOMINANT_FRACTION = 0.85

#: The other two axes shouldn't report much acceleration from gravity. 
#: wide window because test is done by hand.
OFF_AXIS_FRACTION = 0.30

#: At rest should report under 0.15, hand rotation should clear this easily.
GYRO_ROTATION_MIN_RAD_S = 0.5

#: Magnet field should be >> Earth's field (0.25-0.65 gauss at surface)
MAGNET_RISE_FACTOR = 3.0

AXES = ("x", "y", "z")


# ----------------------------------------------------------------------
# Helpers
# ----------------------------------------------------------------------


def read_vector(
    fprime_test_api: IntegrationTestAPI, command: str, event: str
) -> tuple:
    """Command one reading and return it as an (x, y, z) tuple."""
    fprime_test_api.clear_histories()
    proves_send_and_assert_command(fprime_test_api, f"{imuManager}.{command}")
    result: EventData = fprime_test_api.assert_event(
        f"{imuManager}.{event}", timeout=3
    )
    return (
        float(result.args[0].val),
        float(result.args[1].val),
        float(result.args[2].val),
    )


def magnitude(vector: tuple) -> float:
    return math.sqrt(sum(component * component for component in vector))


def dominant_axis(vector: tuple) -> int:
    """Index of the largest-magnitude component."""
    return max(range(3), key=lambda index: abs(vector[index]))


def signed_axis_name(vector: tuple, index: int) -> str:
    """Render an axis and its sign, e.g. '-y'."""
    return f"{'-' if vector[index] < 0 else '+'}{AXES[index]}"


def prompt(message: str) -> None:
    """Ask the operator to do something, then wait."""
    print(f"\n\n>>> {message}")
    input(">>> Press Enter when ready... ")


def sample_peak(
    fprime_test_api: IntegrationTestAPI,
    command: str,
    event: str,
    duration_s: float,
) -> tuple:
    """Sample repeatedly and return the reading with the largest magnitude.

    Allows detection of peak reading during movement of the board.
    """
    best = (0.0, 0.0, 0.0)
    deadline = time.time() + duration_s

    while time.time() < deadline:
        reading = read_vector(fprime_test_api, command, event)
        if magnitude(reading) > magnitude(best):
            best = reading

    return best


@pytest.fixture(scope="module", autouse=True)
def magnetorquers_off(fprime_test_api_session: IntegrationTestAPI, start_gds):
    """ Force off magnetorquers """
    proves_send_and_assert_command(
        fprime_test_api_session, f"{detumbleManager}.SET_MODE", ["DISABLED"]
    )
    time.sleep(1)
    yield


# ----------------------------------------------------------------------
# Tests
# ----------------------------------------------------------------------


def test_01_accelerometer_axis_map(
    fprime_test_api: IntegrationTestAPI, start_gds
):
    """
    Reports which signed accelerometer axis points down for
    each face, allowing mapping between the board and sensor.

    Each orientation asserts:
      -|a| is ~9.81 (1g)
      - One axis dominates and the other two are near zero
    """
    faces = [
        "the +X face of the board (the X arrow) pointing DOWN",
        "the -X face pointing DOWN (flip from the previous orientation)",
        "the +Y face pointing DOWN",
        "the -Y face pointing DOWN",
        "the +Z face pointing DOWN (board flat, component side up)",
        "the -Z face pointing DOWN (board flat, component side down)",
    ]

    mapping = []

    for face in faces:
        prompt(f"Hold the board still with {face}")

        reading = read_vector(
            fprime_test_api, "GET_ACCELERATION", "AccelerationData"
        )
        measured = magnitude(reading)

        print(
            f"    a = ({reading[0]:+7.3f}, {reading[1]:+7.3f}, "
            f"{reading[2]:+7.3f})  |a| = {measured:6.3f} m/s^2"
        )

        assert abs(measured - GRAVITY_MS2) < GRAVITY_TOL_MS2, (
            f"|a| = {measured:.3f} m/s^2 with {face}. Expected "
            f"{GRAVITY_MS2:.3f}. A magnitude that is correct in some "
            f"orientations and collapses in others means the axis now "
            f"pointing down is dead."
        )

        index = dominant_axis(reading)
        dominant = abs(reading[index])

        assert dominant > DOMINANT_FRACTION * measured, (
            f"No axis dominates with {face}: {reading}. Gravity should "
            f"fall almost entirely on one axis. Either the board is not "
            f"square to vertical or two axes are cross-coupled."
        )

        for other in range(3):
            if other == index:
                continue
            assert abs(reading[other]) < OFF_AXIS_FRACTION * measured, (
                f"Axis {AXES[other]} reads {reading[other]:+.3f} with "
                f"{face}, which is too large for an off-axis component. "
                f"Full reading: {reading}."
            )

        mapping.append((face, signed_axis_name(reading, index)))

    print("\n\n    ACCELEROMETER AXIS MAP (record this)")
    print("    " + "-" * 60)
    for face, axis in mapping:
        print(f"    {axis:>3}  reads +g when {face}")

    down_axes = {axis.lstrip('+-') for _, axis in mapping}
    assert down_axes == {"x", "y", "z"}, (
        f"Six orientations exercised only axes {sorted(down_axes)}. Each "
        f"of x, y and z should have dominated for exactly two faces. "
        f"Mapping: {mapping}"
    )


def test_02_gyroscope_axis_map(fprime_test_api: IntegrationTestAPI, start_gds):
    """Finds which gyroscope axis responds to rotation about each board axis.

    The sign for each rotation direction is reported, 
    must map this to board clockwise/counterclockwise rotation.
    """
    rotations = [
        ("X", "roll the board about its X axis"),
        ("Y", "pitch the board about its Y axis"),
        ("Z", "yaw the board about its Z axis (spin it flat)"),
    ]

    mapping = []

    for label, instruction in rotations:
        prompt(
            f"Starting in 3 seconds, {instruction} steadily for about "
            f"5 seconds. Keep rotating until told to stop."
        )

        time.sleep(3)
        print("    Rotating... keep going")

        peak = sample_peak(
            fprime_test_api, "GET_ANGULAR_VELOCITY", "AngularVelocityData", 5.0
        )
        measured = magnitude(peak)

        print(
            f"    peak w = ({peak[0]:+7.3f}, {peak[1]:+7.3f}, "
            f"{peak[2]:+7.3f})  |w| = {measured:6.3f} rad/s"
        )

        assert measured > GYRO_ROTATION_MIN_RAD_S, (
            f"Peak |w| was only {measured:.3f} rad/s while rotating about "
            f"{label}. Expected over {GYRO_ROTATION_MIN_RAD_S}. Either "
            f"the rotation was too slow or the gyroscope is not "
            f"responding."
        )

        index = dominant_axis(peak)
        mapping.append((label, signed_axis_name(peak, index), measured))

    print("\n\n    GYROSCOPE AXIS MAP (record this)")
    print("    " + "-" * 60)
    for label, axis, peak in mapping:
        print(
            f"    rotation about board {label} -> gyro {axis:>3}  "
            f"(peak {peak:.2f} rad/s)"
        )

    responding = {axis.lstrip('+-') for _, axis, _ in mapping}
    assert responding == {"x", "y", "z"}, (
        f"Three rotations produced responses on only {sorted(responding)}. "
        f"Each board axis should drive a different gyroscope axis. Either "
        f"a channel is dead or the rotations were not about distinct "
        f"axes. Mapping: {mapping}"
    )


def test_03_magnetometer_responds_to_a_magnet(
    fprime_test_api: IntegrationTestAPI, start_gds
):
    """
    Checks that |B| rises sharply when a magnet is brought near and
    drops when the magnet is taken away.
    """
    prompt("Keep magnets away from the board")
    baseline = read_vector(
        fprime_test_api, "GET_MAGNETIC_FIELD", "MagneticFieldData"
    )
    baseline_magnitude = magnitude(baseline)
    print(
        f"    baseline B = ({baseline[0]:+7.4f}, {baseline[1]:+7.4f}, "
        f"{baseline[2]:+7.4f})  |B| = {baseline_magnitude:7.4f} G"
    )

    prompt(
        "Hold a magnet a few centimeters from the magnetometer"
    )
    peak = sample_peak(
        fprime_test_api, "GET_MAGNETIC_FIELD", "MagneticFieldData", 3.0
    )
    peak_magnitude = magnitude(peak)
    print(
        f"    with magnet B = ({peak[0]:+7.4f}, {peak[1]:+7.4f}, "
        f"{peak[2]:+7.4f})  |B| = {peak_magnitude:7.4f} G"
    )

    assert peak_magnitude > MAGNET_RISE_FACTOR * baseline_magnitude, (
        f"|B| rose only from {baseline_magnitude:.4f} to "
        f"{peak_magnitude:.4f} G, less than {MAGNET_RISE_FACTOR}x. The "
        f"magnetometer is not responding to the field. Check that the "
        f"magnet was close to the magnetometer, not just the board."
    )

    prompt("Move the magnet away")
    time.sleep(1)
    recovered = read_vector(
        fprime_test_api, "GET_MAGNETIC_FIELD", "MagneticFieldData"
    )
    recovered_magnitude = magnitude(recovered)
    print(
        f"    recovered B = ({recovered[0]:+7.4f}, {recovered[1]:+7.4f}, "
        f"{recovered[2]:+7.4f})  |B| = {recovered_magnitude:7.4f} G"
    )

    assert recovered_magnitude < 2.0 * baseline_magnitude, (
        f"|B| stayed at {recovered_magnitude:.4f} G after the magnet was "
        f"removed, against a {baseline_magnitude:.4f} G baseline. The "
        f"sensor may be saturated and latched, or the magnet "
        f"magnetized something on the board (hard-iron offset)."
    )


def test_04_magnetometer_magnitude_is_rotation_invariant(
    fprime_test_api: IntegrationTestAPI, start_gds
):
    """
    Checks that |B| stays roughly constant as the board is rotated.

    A changing |B| as the board rotates indicates a hard-iron offset.
    """
    orientations = [
        "flat, component side up",
        "flat, component side down",
        "on its +X edge",
        "on its +Y edge",
        "rotated 180 degrees from flat",
    ]

    magnitudes = []

    for orientation in orientations:
        prompt(f"Hold the board still: {orientation}")
        reading = read_vector(
            fprime_test_api, "GET_MAGNETIC_FIELD", "MagneticFieldData"
        )
        measured = magnitude(reading)
        magnitudes.append(measured)
        print(
            f"    B = ({reading[0]:+7.4f}, {reading[1]:+7.4f}, "
            f"{reading[2]:+7.4f})  |B| = {measured:7.4f} G"
        )

    smallest = min(magnitudes)
    largest = max(magnitudes)
    spread = largest - smallest
    mean = sum(magnitudes) / len(magnitudes)

    print("\n\n    MAGNETOMETER ROTATION INVARIANCE (record this)")
    print("    " + "-" * 60)
    print(f"    mean   |B| = {mean:.4f} G")
    print(f"    range      = {smallest:.4f} to {largest:.4f} G")
    print(f"    spread     = {spread:.4f} G")
    print(f"    implied hard-iron offset ~ {spread / 2.0:.4f} G")
    print(
        f"    as a fraction of mean      = {100.0 * spread / mean:.1f}%"
    )

    # A spread approaching the mean means an axis is dead or
    # the offset is larger than the field being measured, at which point
    # the readings carry no usable direction information.
    assert spread < 0.8 * mean, (
        f"|B| varied by {spread:.4f} G around a mean of {mean:.4f} G as "
        f"the board was rotated. A spread that large means either a dead "
        f"axis or a hard-iron offset comparable to Earth's field, and "
        f"the direction is not usable. Magnitudes: "
        f"{[f'{value:.4f}' for value in magnitudes]}"
    )

    if spread > 0.25 * mean:
        print(
            f"\n    NOTE: a {100.0 * spread / mean:.0f}% spread is a "
            f"significant hard-iron offset. Calibration needed.
        )
