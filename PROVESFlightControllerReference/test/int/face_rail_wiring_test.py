"""
face_rail_wiring_test.py:

Board test for the face-rail to mux-channel wiring (Cycle L, L1 / R1.3): the
face devices on I2C mux channel 5 (tmp112Face5Manager, veml6031Face5Manager,
drv2605Face5Manager) are powered by face4LoadSwitch, and the devices on mux
channel 6 (veml6031Face6Manager, tmp112Face6Manager) by face5LoadSwitch. This
is the bench confirmation of the netlist inference that the channel-5
connector's rail is F4_PWR and the channel-6 connector's rail is F5_PWR.

Each test starts with every face switch OFF (autouse fixture) and turns ON
exactly one switch, so a device that answers can only be powered by that rail.

Hardware: face boards on the channel-5 and channel-6 connectors
(``requires_face``). DeviceNotReady is declared ``throttle 5`` in all three
manager .fpp files: run this module on a board that has not already emitted
five DeviceNotReady events from the channel-5 managers since boot, or the
not-ready observation of test_02 is suppressed by the throttle.
"""

import time

import pytest
from common import proves_send_and_assert_command
from fprime_gds.common.testing_fw.api import IntegrationTestAPI

pytestmark = [pytest.mark.requires_face]

RD = "ReferenceDeployment"

FACE_SWITCHES = [f"{RD}.face{i}LoadSwitch" for i in range(6)]
face4LoadSwitch = f"{RD}.face4LoadSwitch"
face5LoadSwitch = f"{RD}.face5LoadSwitch"

tmp112Face5Manager = f"{RD}.tmp112Face5Manager"
veml6031Face5Manager = f"{RD}.veml6031Face5Manager"
drv2605Face5Manager = f"{RD}.drv2605Face5Manager"
veml6031Face6Manager = f"{RD}.veml6031Face6Manager"
tmp112Face6Manager = f"{RD}.tmp112Face6Manager"

CHANNEL5_MANAGERS = [tmp112Face5Manager, veml6031Face5Manager, drv2605Face5Manager]

# R1.3 bound: every probed manager must answer within 45 s of its rail's
# TURN_ON. The window covers the rail settling and each manager bringing its
# device up on the first request after the switch reports ON.
FACE_ANSWER_WINDOW_S = 45

# Per-request event wait. A Temperature / VisibleLight event follows its
# Get command within one command round trip; 5 s is the Temperature timeout
# tmp112_test.py already uses for the same request.
REPLY_TIMEOUT_S = 5

# Pause between repeated requests inside FACE_ANSWER_WINDOW_S, so a request
# lost on the link is retried a few times inside the window.
RETRY_PAUSE_S = 2

# DRV2605 START amplitude, the value drv2605_test.py uses.
DRV2605_START_ARG = "127"


def _all_faces_off(fprime_test_api: IntegrationTestAPI) -> None:
    for switch in FACE_SWITCHES:
        proves_send_and_assert_command(fprime_test_api, f"{switch}.TURN_OFF")


@pytest.fixture(autouse=True)
def all_face_switches_off(fprime_test_api: IntegrationTestAPI, start_gds):
    """Every face switch OFF before and after each test, so a failure cannot
    leave a face rail powered for later tests."""
    _all_faces_off(fprime_test_api)
    yield
    try:
        proves_send_and_assert_command(fprime_test_api, f"{drv2605Face5Manager}.STOP")
    except AssertionError:
        pass  # not ready once its rail is off; STOP is only a safety net here
    _all_faces_off(fprime_test_api)


def _await_reply(
    fprime_test_api: IntegrationTestAPI,
    command: str,
    reply_event: str,
    deadline: float,
) -> bool:
    """Send ``command`` until ``reply_event`` arrives or ``deadline`` passes."""
    while time.monotonic() < deadline:
        fprime_test_api.clear_histories()
        fprime_test_api.send_command(command)
        if fprime_test_api.await_event(reply_event, timeout=REPLY_TIMEOUT_S):
            return True
        time.sleep(RETRY_PAUSE_S)
    return False


def _not_ready_events(fprime_test_api: IntegrationTestAPI, subhistory, managers):
    preds = {m: fprime_test_api.get_event_pred(f"{m}.DeviceNotReady") for m in managers}
    return [
        m for item in subhistory.retrieve() for m, pred in preds.items() if pred(item)
    ]


@pytest.mark.verifies("LoadSwitch-1")
def test_01_face4_switch_powers_mux_channel_5(
    fprime_test_api: IntegrationTestAPI, start_gds
):
    """face4LoadSwitch ON alone: the three channel-5 managers answer within
    45 s and none of them reports DeviceNotReady."""
    subhistory = fprime_test_api.get_event_subhistory()
    try:
        proves_send_and_assert_command(fprime_test_api, f"{face4LoadSwitch}.TURN_ON")
        deadline = time.monotonic() + FACE_ANSWER_WINDOW_S

        assert _await_reply(
            fprime_test_api,
            f"{tmp112Face5Manager}.GetTemperature",
            f"{tmp112Face5Manager}.Temperature",
            deadline,
        ), (
            f"tmp112Face5Manager gave no Temperature within {FACE_ANSWER_WINDOW_S}s "
            "of face4LoadSwitch ON"
        )
        assert _await_reply(
            fprime_test_api,
            f"{veml6031Face5Manager}.GetVisibleLight",
            f"{veml6031Face5Manager}.VisibleLight",
            deadline,
        ), (
            f"veml6031Face5Manager gave no VisibleLight within "
            f"{FACE_ANSWER_WINDOW_S}s of face4LoadSwitch ON"
        )
        # START acks OK (EXECUTION_ERROR when the device is not ready).
        proves_send_and_assert_command(
            fprime_test_api, f"{drv2605Face5Manager}.START", [DRV2605_START_ARG]
        )
        assert time.monotonic() < deadline, (
            f"drv2605Face5Manager START succeeded only after the "
            f"{FACE_ANSWER_WINDOW_S}s window"
        )
        proves_send_and_assert_command(fprime_test_api, f"{drv2605Face5Manager}.STOP")

        not_ready = _not_ready_events(fprime_test_api, subhistory, CHANNEL5_MANAGERS)
        assert not not_ready, (
            f"No channel-5 manager may report DeviceNotReady with face4LoadSwitch "
            f"ON; got it from {sorted(set(not_ready))}"
        )
    finally:
        fprime_test_api.remove_event_subhistory(subhistory)


@pytest.mark.verifies("LoadSwitch-1")
def test_02_face5_switch_powers_mux_channel_6_only(
    fprime_test_api: IntegrationTestAPI, start_gds
):
    """face5LoadSwitch ON alone: the two channel-6 managers answer within 45 s
    and each channel-5 manager reports DeviceNotReady."""
    proves_send_and_assert_command(fprime_test_api, f"{face5LoadSwitch}.TURN_ON")
    deadline = time.monotonic() + FACE_ANSWER_WINDOW_S

    assert _await_reply(
        fprime_test_api,
        f"{veml6031Face6Manager}.GetVisibleLight",
        f"{veml6031Face6Manager}.VisibleLight",
        deadline,
    ), (
        f"veml6031Face6Manager gave no VisibleLight within {FACE_ANSWER_WINDOW_S}s "
        "of face5LoadSwitch ON"
    )
    assert _await_reply(
        fprime_test_api,
        f"{tmp112Face6Manager}.GetTemperature",
        f"{tmp112Face6Manager}.Temperature",
        deadline,
    ), (
        f"tmp112Face6Manager gave no Temperature within {FACE_ANSWER_WINDOW_S}s "
        "of face5LoadSwitch ON"
    )

    probes = [
        (f"{tmp112Face5Manager}.GetTemperature", []),
        (f"{veml6031Face5Manager}.GetVisibleLight", []),
        (f"{drv2605Face5Manager}.START", [DRV2605_START_ARG]),
    ]
    for (command, args), manager in zip(probes, CHANNEL5_MANAGERS, strict=True):
        fprime_test_api.clear_histories()
        fprime_test_api.send_command(command, args)
        reported = fprime_test_api.await_event(
            f"{manager}.DeviceNotReady", timeout=REPLY_TIMEOUT_S
        )
        assert reported is not None, (
            f"{manager} must report DeviceNotReady with only face5LoadSwitch ON "
            f"(its rail, F4_PWR, is off); nothing within {REPLY_TIMEOUT_S}s"
        )
