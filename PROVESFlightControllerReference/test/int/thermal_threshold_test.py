"""
thermal_threshold_test.py:

Integration tests for thermal fault detection: a face temperature outside its
configured band raises a warning event on the board, once, promptly.

LEVEL: Board. The fault is provoked by moving the *threshold* under a live
sensor reading rather than by heating the board, so the whole chain — sensor
read on the 1 Hz rate group, threshold evaluation, event emission, downlink —
is exercised against real hardware. The decision logic alone is covered at
unit level by test_ThermalManager_Thresholds.cpp; this test is the board-level
evidence that the logic is actually wired to a real sensor and a real downlink.

JOBS: both. Nothing here severs the RF link.

PRECONDITIONS: the face0 board must be fitted and its load switch on, so that
tmp112Face0Manager returns a real reading. The fixture turns the switch on.

METHOD AND ORACLE: the thresholds are parameters (ThermalManager.fpp:7-16), so
the fault is injected by setting FACE_TEMP_UPPER_THRESHOLD below ambient (and,
in test_02, the lower threshold above ambient). The expected event and its
arguments come from the TM-L2-08 / FD-L2-01 / FD-L2-03 pass criteria; the
restored values are the parameter defaults those criteria quote.

WINDOW CONSTANTS (TP-4):
  DETECT_S = 3
      The criteria say the warning appears "within 2 s" of the fault. The
      thermal evaluation runs on the 1 Hz rate group, so the worst case is one
      full 1 s cycle after the parameter lands, plus downlink latency: 2 * 1 s
      + 1 s margin = 3 s.
  LATCH_QUIET_S = 5
      Bounded negative window for "the event does not repeat". The evaluation
      is a 1 Hz observable, so the window must span at least two evaluations to
      distinguish "latched" from "merely between cycles": 2 * 1 s plus margin,
      widened to 5 s to also cover the radio job's 2 s downlink framer cycle
      (conftest.py:_enable_radio) so a repeat event could not still be in
      flight when the window closes.

CLAIMED REQUIREMENTS: TM-L2-08, FD-L2-01, FD-L2-03, CDH-15. Clause claims are
named in each docstring.
"""

import time

import pytest
from common import proves_send_and_assert_command
from fprime_gds.common.testing_fw.api import IntegrationTestAPI

thermalManager = "ReferenceDeployment.thermalManager"
face0LoadSwitch = "ReferenceDeployment.face0LoadSwitch"

DETECT_S = 3
LATCH_QUIET_S = 5

# Parameter defaults, ThermalManager.fpp:7-16 — restored by the fixture.
FACE_UPPER_DEFAULT_C = 60.0
FACE_LOWER_DEFAULT_C = -40.0

# Fault-injection values: far enough outside a normal bench ambient (roughly
# 15-35 C) that the sensor reading is unambiguously on the wrong side of the
# threshold, whichever way the fault is being provoked.
FAULT_UPPER_C = 0.0
FAULT_LOWER_C = 100.0

# ThermalManager_TempSensorType.FACE, and the face0 sensor's port index.
SENSOR_TYPE_FACE = "FACE"
FACE0_INDEX = 0


def _event_names(fprime_test_api: IntegrationTestAPI) -> list[str]:
    return [
        str(e.get_template().get_name())
        for e in fprime_test_api.get_event_test_history()
    ]


def _count_events(fprime_test_api: IntegrationTestAPI, substring: str) -> int:
    return sum(1 for name in _event_names(fprime_test_api) if substring in name)


@pytest.fixture(autouse=True)
def restore_thresholds(fprime_test_api: IntegrationTestAPI, start_gds):
    """Power the face board, and restore both thresholds afterwards.

    TP-5: leaving a threshold at its fault-injection value would make every
    later test run against a spacecraft that believes it is overheating, so the
    defaults are written back unconditionally after each test.
    """
    proves_send_and_assert_command(fprime_test_api, f"{face0LoadSwitch}.TURN_ON")
    yield
    proves_send_and_assert_command(
        fprime_test_api,
        f"{thermalManager}.FACE_TEMP_UPPER_THRESHOLD_PRM_SET",
        [str(FACE_UPPER_DEFAULT_C)],
    )
    proves_send_and_assert_command(
        fprime_test_api,
        f"{thermalManager}.FACE_TEMP_LOWER_THRESHOLD_PRM_SET",
        [str(FACE_LOWER_DEFAULT_C)],
    )


@pytest.mark.verifies("TM-L2-08", "FD-L2-01", "FD-L2-03", "CDH-15")
def test_01_face_temperature_above_threshold_is_flagged(
    fprime_test_api: IntegrationTestAPI, start_gds
):
    """Dropping the upper threshold below ambient raises one warning, promptly.

    TM-L2-08: a face temperature outside the configured band raises
    TemperatureAboveThreshold once.
    FD-L2-03: the warning appears within one evaluation of the fault.
    FD-L2-01 clause claimed here: the *thermal* clause only — the voltage and
    device-fault clauses of FD-L2-01 are not exercised by this test.
    CDH-15 clause claimed here: the *FACE_TEMP_UPPER_THRESHOLD ->
    TemperatureAboveThreshold* clause only; the COMM_LOSS_TIME and
    DeviceNotReady clauses of CDH-15 are separate fault injections.
    """
    fprime_test_api.clear_histories()

    proves_send_and_assert_command(
        fprime_test_api,
        f"{thermalManager}.FACE_TEMP_UPPER_THRESHOLD_PRM_SET",
        [str(FAULT_UPPER_C)],
    )

    event = fprime_test_api.assert_event(
        f"{thermalManager}.TemperatureAboveThreshold", timeout=DETECT_S
    )
    assert event is not None

    # The event must identify which sensor faulted, and report a reading that
    # is genuinely above the threshold we set.
    assert str(event.args[0].val).upper().endswith(SENSOR_TYPE_FACE), (
        f"Fault must be reported against a FACE sensor, got {event.args[0].val!r}"
    )
    assert int(event.args[1].val) == FACE0_INDEX, (
        f"Fault must name sensor {FACE0_INDEX}, got {event.args[1].val}"
    )
    assert float(event.args[2].val) > FAULT_UPPER_C, (
        f"Reported temperature {event.args[2].val} C must actually exceed the "
        f"threshold {FAULT_UPPER_C} C that was set"
    )

    # TM-L2-08: the warning is latched, not repeated every cycle.
    before = _count_events(fprime_test_api, "TemperatureAboveThreshold")
    time.sleep(LATCH_QUIET_S)
    after = _count_events(fprime_test_api, "TemperatureAboveThreshold")
    assert after == before, (
        f"TemperatureAboveThreshold must be raised once and latched; "
        f"{after - before} further events arrived in {LATCH_QUIET_S}s"
    )

    # A high-side fault must not masquerade as a low-side one.
    assert _count_events(fprime_test_api, "TemperatureBelowThreshold") == 0, (
        "An above-threshold fault must not raise TemperatureBelowThreshold"
    )


@pytest.mark.verifies("TM-L2-08")
def test_02_face_temperature_below_threshold_is_flagged(
    fprime_test_api: IntegrationTestAPI, start_gds
):
    """Raising the lower threshold above ambient raises the low-side warning.

    TM-L2-08: the band is two-sided — a reading below the configured lower
    threshold raises TemperatureBelowThreshold, with the same
    identify-the-sensor detail as the high side.
    """
    fprime_test_api.clear_histories()

    proves_send_and_assert_command(
        fprime_test_api,
        f"{thermalManager}.FACE_TEMP_LOWER_THRESHOLD_PRM_SET",
        [str(FAULT_LOWER_C)],
    )

    event = fprime_test_api.assert_event(
        f"{thermalManager}.TemperatureBelowThreshold", timeout=DETECT_S
    )
    assert event is not None
    assert str(event.args[0].val).upper().endswith(SENSOR_TYPE_FACE), (
        f"Fault must be reported against a FACE sensor, got {event.args[0].val!r}"
    )
    assert int(event.args[1].val) == FACE0_INDEX, (
        f"Fault must name sensor {FACE0_INDEX}, got {event.args[1].val}"
    )
    assert float(event.args[2].val) < FAULT_LOWER_C, (
        f"Reported temperature {event.args[2].val} C must actually be below the "
        f"threshold {FAULT_LOWER_C} C that was set"
    )
