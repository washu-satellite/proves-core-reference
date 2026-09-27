"""
charge_status_test.py:

Board test for the battery-charge status (Cycle L, L4 / R4.4):
powerMonitor.Charging and the ChargeStateChanged event follow the LT3652
charger's ~CHRG output, read through gpioCharge on every 1 Hz PowerMonitor run.

Hardware: a bench supply on VSOLAR, set above the battery pack voltage, that the
test can switch. The operator provides two shell commands in the environment:

    PROVES_VSOLAR_ON_CMD   switches the VSOLAR supply output on
    PROVES_VSOLAR_OFF_CMD  switches it off

(e.g. a SCPI one-liner for the bench supply). Without both, the test is skipped.
"""

import os
import shlex
import subprocess
import time

import pytest
from common import proves_send_and_assert_command
from fprime_gds.common.testing_fw.api import IntegrationTestAPI

VSOLAR_ON_CMD = os.environ.get("PROVES_VSOLAR_ON_CMD", "")
VSOLAR_OFF_CMD = os.environ.get("PROVES_VSOLAR_OFF_CMD", "")

pytestmark = [
    pytest.mark.requires_battery,
    pytest.mark.skipif(
        not (VSOLAR_ON_CMD and VSOLAR_OFF_CMD),
        reason="needs a switchable VSOLAR supply: set PROVES_VSOLAR_ON_CMD and "
        "PROVES_VSOLAR_OFF_CMD",
    ),
]

powerMonitor = "ReferenceDeployment.powerMonitor"
tlmSend = "CdhCore.tlmSend"

# R4.4 bound from switching the supply to the ChargeStateChanged event on the
# ground: ~CHRG follows the charger input within milliseconds, PowerMonitor
# reads the pin on every 1 Hz run tick (<= 1 s), and an event downlinks over
# UART well inside the remaining second.
CHARGE_FOLLOW_S = 2

# After a transition, watch this long for a second ChargeStateChanged: five
# more 1 Hz reads of an unchanged pin must add no event ("one per transition").
SINGLE_EVENT_WINDOW_S = 5

# Before a transition, let the pin settle in the starting state.
SETTLE_S = 3

# powerMonitor.Charging is in packet PowerMonitor (id 11, group 2,
# ReferenceDeploymentPackets.fppi), downlinked only at tlmSend level >= 2.
# TlmChan runs about every 30 s (telemetry_gate_test.py TELEMETRY_PERIOD_S);
# one period plus margin sees at least one downlink of the channel.
CHARGE_PACKET_LEVEL = 2
DEFAULT_PACKET_LEVEL = 1
TELEMETRY_WINDOW_S = 30 + 15


def _supply(command: str) -> None:
    subprocess.run(shlex.split(command), check=True, timeout=30)


@pytest.fixture(autouse=True)
def supply_off_and_level_restored(fprime_test_api: IntegrationTestAPI, start_gds):
    """VSOLAR off and tlmSend at level 2 during the test; supply off and the
    default packet level restored afterwards, whatever happens."""
    _supply(VSOLAR_OFF_CMD)
    proves_send_and_assert_command(
        fprime_test_api, f"{tlmSend}.SET_LEVEL", [str(CHARGE_PACKET_LEVEL)]
    )
    yield
    try:
        _supply(VSOLAR_OFF_CMD)
    finally:
        proves_send_and_assert_command(
            fprime_test_api, f"{tlmSend}.SET_LEVEL", [str(DEFAULT_PACKET_LEVEL)]
        )


def _transition(fprime_test_api: IntegrationTestAPI, command: str, state: str) -> None:
    """Switch the supply and check one ChargeStateChanged(state) within
    CHARGE_FOLLOW_S, no second one after it, and Charging reading ``state``."""
    event_name = f"{powerMonitor}.ChargeStateChanged"
    time.sleep(SETTLE_S)
    fprime_test_api.clear_histories()
    subhistory = fprime_test_api.get_event_subhistory()
    try:
        _supply(command)
        fprime_test_api.assert_event(event_name, [state], timeout=CHARGE_FOLLOW_S)
        time.sleep(SINGLE_EVENT_WINDOW_S)
        pred = fprime_test_api.get_event_pred(event_name)
        changes = [item for item in subhistory.retrieve() if pred(item)]
        assert len(changes) == 1, (
            f"Exactly one ChargeStateChanged per transition to {state}; got "
            f"{[str(c.get_args()) for c in changes]}"
        )
    finally:
        fprime_test_api.remove_event_subhistory(subhistory)

    fprime_test_api.clear_histories()
    reading = fprime_test_api.await_telemetry(
        f"{powerMonitor}.Charging", start="NOW", timeout=TELEMETRY_WINDOW_S
    )
    assert reading is not None, (
        f"powerMonitor.Charging did not downlink within {TELEMETRY_WINDOW_S}s at "
        f"packet level {CHARGE_PACKET_LEVEL}"
    )
    assert str(reading.get_val()).upper().endswith(state), (
        f"powerMonitor.Charging must read {state}, got {reading.get_val()!r}"
    )


@pytest.mark.verifies("PWR-MON-REQ-012")
def test_01_charging_follows_vsolar_supply(
    fprime_test_api: IntegrationTestAPI, start_gds
):
    """Supply on: Charging ON within 2 s; supply off: OFF within 2 s; one
    ChargeStateChanged per transition."""
    _transition(fprime_test_api, VSOLAR_ON_CMD, "ON")
    _transition(fprime_test_api, VSOLAR_OFF_CMD, "OFF")
