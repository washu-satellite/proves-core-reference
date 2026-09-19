"""
power_monitor_test.py:

Integration tests for the Power Monitor component.
"""

import time

import pytest
from common import proves_send_and_assert_command
from fprime_gds.common.data_types.event_data import EventData
from fprime_gds.common.testing_fw.api import IntegrationTestAPI

# The powerMonitor run port fires on the 1 Hz rate group (topology.fpp), so a
# 10 s gap between the two readings spans ~10 accumulation cycles. Ten seconds
# is the interval named in the PWR-MON-REQ-001/004 pass criteria.
ACCUMULATION_GAP_S = 10

pytestmark = [pytest.mark.requires_battery]

ina219SysManager = "ReferenceDeployment.ina219SysManager"
ina219SolManager = "ReferenceDeployment.ina219SolManager"
powerMonitor = "ReferenceDeployment.powerMonitor"


def get_voltage(fprime_test_api: IntegrationTestAPI, manager: str) -> float:
    """Helper function to get voltage via command and event"""
    fprime_test_api.clear_histories()

    proves_send_and_assert_command(
        fprime_test_api,
        f"{manager}.GetVoltage",
    )

    result: EventData = fprime_test_api.assert_event(
        f"{manager}.VoltageReading", timeout=3
    )

    return result.args[0].val


def get_current(fprime_test_api: IntegrationTestAPI, manager: str) -> float:
    """Helper function to get current via command and event"""
    fprime_test_api.clear_histories()

    proves_send_and_assert_command(
        fprime_test_api,
        f"{manager}.GetCurrent",
    )

    result: EventData = fprime_test_api.assert_event(
        f"{manager}.CurrentReading", timeout=3
    )

    return result.args[0].val


def get_power(fprime_test_api: IntegrationTestAPI, manager: str) -> float:
    """Helper function to get power via command and event"""
    fprime_test_api.clear_histories()

    proves_send_and_assert_command(
        fprime_test_api,
        f"{manager}.GetPower",
    )

    result: EventData = fprime_test_api.assert_event(
        f"{manager}.PowerReading", timeout=3
    )

    return result.args[0].val


def get_total_power_consumption(fprime_test_api: IntegrationTestAPI) -> float:
    """Helper function to get total power consumption via command and event"""
    fprime_test_api.clear_histories()

    proves_send_and_assert_command(
        fprime_test_api,
        f"{powerMonitor}.GET_TOTAL_POWER",
    )

    result: EventData = fprime_test_api.assert_event(
        f"{powerMonitor}.TotalPowerConsumptionReading", timeout=3
    )

    return result.args[0].val


def test_01_power_manager_readings(fprime_test_api: IntegrationTestAPI, start_gds):
    """Test that we can get power readings from INA219 managers"""

    # Get system power readings
    sys_voltage = get_voltage(fprime_test_api, ina219SysManager)
    sys_current = get_current(fprime_test_api, ina219SysManager)
    _ = get_power(fprime_test_api, ina219SysManager)

    # Get solar power readings
    sol_voltage = get_voltage(fprime_test_api, ina219SolManager)
    _ = get_current(fprime_test_api, ina219SolManager)
    _ = get_power(fprime_test_api, ina219SolManager)

    # TODO: Fix the power readings once INA219 power calculation is verified
    assert sys_voltage != 0, "System voltage reading should be non-zero"
    assert sys_current != 0, "System current reading should be non-zero"
    # assert sys_power != 0, "System power reading should be non-zero"
    assert sol_voltage != 0, "Solar voltage reading should be non-zero"
    # Solar current can be 0.0 in valid scenarios (no sunlight, etc.)
    # Existence is already verified by get_current() above
    # assert sol_current != 0, "Solar current reading should be non-zero"
    # assert sol_power != 0, "Solar power reading should be non-zero"


@pytest.mark.verifies("PWR-MON-REQ-001", "PWR-MON-REQ-004")
def test_02_total_power_consumption(fprime_test_api: IntegrationTestAPI, start_gds):
    """Test that TotalPowerConsumption is being updated.

    PWR-MON-REQ-001: GET_TOTAL_POWER returns a reading > 0.
    PWR-MON-REQ-004: a second reading 10 s later is strictly greater, which is
    only possible if a system power request runs on each 1 Hz cycle.
    Each requirement has its own assertion below (TP-9).
    """

    first = get_total_power_consumption(fprime_test_api)

    # PWR-MON-REQ-001: a reading exists and is positive.
    assert first > 0, f"Total power consumption should be > 0, got {first}"

    time.sleep(ACCUMULATION_GAP_S)
    second = get_total_power_consumption(fprime_test_api)

    # PWR-MON-REQ-004: the accumulator advanced over the gap.
    assert second > first, (
        f"Total power consumption should strictly increase over "
        f"{ACCUMULATION_GAP_S}s (run port accumulating each 1 Hz cycle), "
        f"but went from {first} to {second}"
    )
