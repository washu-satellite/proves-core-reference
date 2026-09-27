"""
burnwire_deploy2_test.py:

Board test for the second burn channel, burnwireDeploy2 (Cycle L, L3 / R3.3):
a Burnwire instance driving DEPLOY2 through gpioDeploy2 on gpioSet[0] only.

Hardware: a DUMMY LOAD on J24 in place of any deployable. This test fires the
DEPLOY2 channel for real, so it is skipped unless PROVES_J24_DUMMY_LOAD=1 is set
in the environment by the operator who fitted the load. Markers follow
burnwire_test.py (UART link, battery powered).
"""

import os
import time

import pytest
from common import proves_send_and_assert_command
from fprime_gds.common.data_types.event_data import EventData
from fprime_gds.common.testing_fw.api import IntegrationTestAPI

pytestmark = [
    pytest.mark.uart_only,
    pytest.mark.requires_battery,
    pytest.mark.skipif(
        os.environ.get("PROVES_J24_DUMMY_LOAD") != "1",
        reason="fires DEPLOY2: needs a dummy load on J24 and PROVES_J24_DUMMY_LOAD=1",
    ),
]

burnwireDeploy2 = "ReferenceDeployment.burnwireDeploy2"
ina219SysManager = "ReferenceDeployment.ina219SysManager"

# R3.3 bound for SetBurnwireState after START_BURNWIRE / STOP_BURNWIRE.
STATE_EVENT_TIMEOUT_S = 2

# Time for the load current to show on the system INA219 after the ON event:
# the GPIO goes HIGH on the first 1 Hz schedIn tick after START (<= 1 s).
POWER_SETTLE_S = 1

# Minimum rise of system power over the pre-START reading. Same margin
# drv2605_test.py uses to call a face actuator "on"; the J24 dummy load must be
# chosen to draw clearly more than this from the battery bus.
POWER_RISE_MIN_W = 0.3

# SAFETY_TIMER default, Burnwire.fpp "param SAFETY_TIMER: U32 default 10",
# unchanged by R3.2. schedIn runs at 1 Hz: the first tick after START counts 1
# and drives HIGH, the tick that counts 10 stops the burn, so OFF follows ON by
# 9..10 s of flight time. Half a tick below and one tick above absorb the event
# timestamp granularity and rate-group jitter.
SAFETY_TIMER_S = 10
SAFETY_OFF_MIN_S = (SAFETY_TIMER_S - 1) - 0.5
SAFETY_OFF_MAX_S = SAFETY_TIMER_S + 1.0

# Ground wait for the safety-timer OFF: the timer plus margin for downlink.
SAFETY_WAIT_S = SAFETY_TIMER_S + 5


def _fsw_seconds(fsw_time) -> float:
    """Flight-software timestamp in seconds."""
    if hasattr(fsw_time, "get_float"):
        return float(fsw_time.get_float())
    return float(fsw_time.seconds) + float(getattr(fsw_time, "useconds", 0)) / 1e6


def get_system_power(fprime_test_api: IntegrationTestAPI) -> float:
    """System bus power in W via GetPower / PowerReading."""
    fprime_test_api.clear_histories()
    proves_send_and_assert_command(fprime_test_api, f"{ina219SysManager}.GetPower")
    result: EventData = fprime_test_api.assert_event(
        f"{ina219SysManager}.PowerReading", timeout=5
    )
    return result.args[0].val


def stop_burnwire_deploy2(fprime_test_api: IntegrationTestAPI) -> None:
    proves_send_and_assert_command(fprime_test_api, f"{burnwireDeploy2}.STOP_BURNWIRE")
    fprime_test_api.assert_event(
        f"{burnwireDeploy2}.SetBurnwireState", "OFF", timeout=STATE_EVENT_TIMEOUT_S
    )


@pytest.fixture(autouse=True)
def deploy2_stopped(fprime_test_api: IntegrationTestAPI, start_gds):
    """DEPLOY2 stopped before and after each test, so a failure cannot leave
    the channel burning."""
    stop_burnwire_deploy2(fprime_test_api)
    yield
    stop_burnwire_deploy2(fprime_test_api)


@pytest.mark.verifies("BW-009")
def test_01_start_and_stop_deploy2(fprime_test_api: IntegrationTestAPI, start_gds):
    """START turns DEPLOY2 on (power rises), STOP turns it off with an end count."""
    baseline_w = get_system_power(fprime_test_api)

    proves_send_and_assert_command(fprime_test_api, f"{burnwireDeploy2}.START_BURNWIRE")
    try:
        fprime_test_api.assert_event(
            f"{burnwireDeploy2}.SetBurnwireState", "ON", timeout=STATE_EVENT_TIMEOUT_S
        )
        time.sleep(POWER_SETTLE_S)
        burning_w = get_system_power(fprime_test_api)
        assert burning_w >= baseline_w + POWER_RISE_MIN_W, (
            f"System power with DEPLOY2 on ({burning_w} W) must exceed the "
            f"pre-START reading ({baseline_w} W) by {POWER_RISE_MIN_W} W"
        )
    finally:
        proves_send_and_assert_command(
            fprime_test_api, f"{burnwireDeploy2}.STOP_BURNWIRE"
        )

    fprime_test_api.assert_event(
        f"{burnwireDeploy2}.SetBurnwireState", "OFF", timeout=STATE_EVENT_TIMEOUT_S
    )
    fprime_test_api.assert_event(
        f"{burnwireDeploy2}.BurnwireEndCount", timeout=STATE_EVENT_TIMEOUT_S
    )


@pytest.mark.verifies("BW-009")
def test_02_safety_timer_stops_deploy2(fprime_test_api: IntegrationTestAPI, start_gds):
    """Without STOP, the safety timer turns DEPLOY2 off 10 s after START."""
    proves_send_and_assert_command(fprime_test_api, f"{burnwireDeploy2}.START_BURNWIRE")
    on_event = fprime_test_api.assert_event(
        f"{burnwireDeploy2}.SetBurnwireState", "ON", timeout=STATE_EVENT_TIMEOUT_S
    )
    off_event = fprime_test_api.assert_event(
        f"{burnwireDeploy2}.SetBurnwireState", "OFF", timeout=SAFETY_WAIT_S
    )
    burn_s = _fsw_seconds(off_event.get_time()) - _fsw_seconds(on_event.get_time())
    assert SAFETY_OFF_MIN_S <= burn_s <= SAFETY_OFF_MAX_S, (
        f"Safety timer must stop DEPLOY2 {SAFETY_TIMER_S} s after START; OFF came "
        f"{burn_s:.2f} s after ON (window {SAFETY_OFF_MIN_S}..{SAFETY_OFF_MAX_S} s)"
    )
    fprime_test_api.assert_event(
        f"{burnwireDeploy2}.BurnwireEndCount", timeout=STATE_EVENT_TIMEOUT_S
    )
