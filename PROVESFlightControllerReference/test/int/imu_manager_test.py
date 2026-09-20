"""
imu_manager_test.py:

Integration tests for the IMU Manager component.
"""

import pytest
from common import proves_send_and_assert_command
from fprime_gds.common.data_types.event_data import EventData
from fprime_gds.common.testing_fw.api import IntegrationTestAPI

imuManager = "ReferenceDeployment.imuManager"


def test_01_get_acceleration(fprime_test_api: IntegrationTestAPI, start_gds):
    """Test that we can get Acceleration via command and event"""
    fprime_test_api.clear_histories()

    proves_send_and_assert_command(
        fprime_test_api,
        f"{imuManager}.GET_ACCELERATION",
    )

    result: EventData = fprime_test_api.assert_event(
        f"{imuManager}.AccelerationData", timeout=3
    )

    x = result.args[0].val
    y = result.args[1].val
    z = result.args[2].val

    assert any(val != 0 for val in (x, y, z)), (
        f"Acceleration reading should have at least one non-zero component: x={x}, y={y}, z={z}"
    )


def test_02_get_angular_velocity(fprime_test_api: IntegrationTestAPI, start_gds):
    """Test that we can get AngularVelocity via command and event"""
    fprime_test_api.clear_histories()

    proves_send_and_assert_command(
        fprime_test_api,
        f"{imuManager}.GET_ANGULAR_VELOCITY",
    )

    result: EventData = fprime_test_api.assert_event(
        f"{imuManager}.AngularVelocityData", timeout=3
    )

    x = result.args[0].val
    y = result.args[1].val
    z = result.args[2].val

    assert any(val != 0 for val in (x, y, z)), (
        f"AngularVelocity reading should have at least one non-zero component: x={x}, y={y}, z={z}"
    )


@pytest.mark.verifies("CDH-6", "TM-L2-06")
def test_03_get_magnetic_field(fprime_test_api: IntegrationTestAPI, start_gds):
    """Test that we can get MagneticField via command and event.

    Clause claimed here (for both CDH-6 and TM-L2-06): the *on-demand* clause —
    GET_MAGNETIC_FIELD returns MagneticFieldData within 3 s with at least one
    non-zero axis. The other clause of both requirements (the MagneticField
    *channel* updating at least once per 45 s at tlmSend level 5) is a periodic
    downlink observable this test never looks at; it is covered by the
    telemetry-sources test.
    """
    fprime_test_api.clear_histories()

    proves_send_and_assert_command(
        fprime_test_api,
        f"{imuManager}.GET_MAGNETIC_FIELD",
    )

    result: EventData = fprime_test_api.assert_event(
        f"{imuManager}.MagneticFieldData", timeout=3
    )

    x = result.args[0].val
    y = result.args[1].val
    z = result.args[2].val

    assert any(val != 0 for val in (x, y, z)), (
        f"MagneticField reading should have at least one non-zero component: x={x}, y={y}, z={z}"
    )
