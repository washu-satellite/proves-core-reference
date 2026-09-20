"""
task_gate_test.py:

Integration tests for the TaskGate component, which interposes on the five
1 Hz sensor-poll members of rateGroup1Hz and provides the generic
ENABLE_TASK(task) / DISABLE_TASK(task) pair required by SC-L2-07.

MARKER CHOICE (uart_only): this test deliberately does NOT use
``pytest.mark.uart_only``. TaskGate only gates sensor polling; it never touches
the command path, the event path, ``lora.TRANSMIT`` or ``CdhCore.tlmSend``, so
commanding and acks keep working over RF for the whole test.

PACKET LEVEL: ``taskGate.TasksEnabledMask`` and ``taskGate.GatedRuns`` live in
the ``HealthAuxiliary`` packet (ReferenceDeploymentPackets.fppi, id 4, group 5),
so ``CdhCore.tlmSend.SET_LEVEL 5`` is commanded before they can be read and the
level is restored to 1 afterwards.
"""

import time

import pytest
from common import proves_send_and_assert_command
from fprime_gds.common.testing_fw.api import IntegrationTestAPI

taskGate = "ReferenceDeployment.taskGate"
tlmSend = "CdhCore.tlmSend"

# Every gateable task, so the fixture can restore the 0x1F default whatever a
# test left behind.
ALL_TASKS = ["IMU", "POWER_MONITOR", "ADCS", "THERMAL", "FS_SPACE"]

# Bit i of TasksEnabledMask is task i; IMU is ordinal 0.
MASK_ALL_ENABLED = 0x1F
MASK_IMU_DISABLED = 0x1E

GATE_PACKET_LEVEL = 5
DEFAULT_PACKET_LEVEL = 1

# telemetryDelay divides the 1 Hz tick by 29, so a TlmChan cycle is ~30 s; one
# cycle plus margin is long enough to observe a HealthAuxiliary downlink.
ONE_PERIOD_WINDOW_S = 45

# Long enough to span two TlmChan cycles with the task gated.
GATED_WINDOW_S = 70

# A gated 1 Hz task drops far more ticks than this over GATED_WINDOW_S; the
# bound is deliberately loose so link jitter cannot fail the test.
GATED_RUNS_MIN = 2


def _enable(fprime_test_api: IntegrationTestAPI, task: str) -> None:
    proves_send_and_assert_command(fprime_test_api, f"{taskGate}.ENABLE_TASK", [task])


def _disable(fprime_test_api: IntegrationTestAPI, task: str) -> None:
    proves_send_and_assert_command(fprime_test_api, f"{taskGate}.DISABLE_TASK", [task])


def _read(fprime_test_api: IntegrationTestAPI, channel: str) -> int:
    """Await one downlink of a TaskGate channel and return it as an integer."""
    item = fprime_test_api.await_telemetry(
        f"{taskGate}.{channel}", start="NOW", timeout=ONE_PERIOD_WINDOW_S
    )
    assert item is not None, (
        f"{channel} did not downlink within {ONE_PERIOD_WINDOW_S}s at packet "
        f"level {GATE_PACKET_LEVEL}"
    )
    return int(item.get_val())


@pytest.fixture(autouse=True)
def ensure_all_enabled(fprime_test_api: IntegrationTestAPI, start_gds):
    """Leave every scheduled task ENABLED before and after each test so a
    failure mid-test cannot leave a sensor silently unpolled for later tests."""
    for task in ALL_TASKS:
        _enable(fprime_test_api, task)
    yield
    for task in ALL_TASKS:
        _enable(fprime_test_api, task)


@pytest.mark.verifies("SC-L2-07")
def test_01_disable_and_enable_a_scheduled_task(
    fprime_test_api: IntegrationTestAPI, start_gds
):
    """DISABLE_TASK stops a scheduled task and ENABLE_TASK restarts it.

    SC-L2-07's generic clause: ``taskGate.DISABLE_TASK(IMU)`` acks, emits
    TaskDisabled, and the gated-run counter advances (the 1 Hz tick is being
    dropped at the gate); ``taskGate.ENABLE_TASK(IMU)`` acks, emits TaskEnabled,
    and the counter stops advancing because the tick is forwarded again.

    Reading GatedRuns rather than the absence of IMU telemetry is what makes
    this a test of the gate: it observes the dropped tick at the component that
    dropped it, and the frozen counter afterwards proves the restart took.
    """
    proves_send_and_assert_command(
        fprime_test_api, f"{tlmSend}.SET_LEVEL", [str(GATE_PACKET_LEVEL)]
    )
    try:
        before = _read(fprime_test_api, "GatedRuns")

        _disable(fprime_test_api, "IMU")
        fprime_test_api.assert_event(f"{taskGate}.TaskDisabled", timeout=5)

        time.sleep(GATED_WINDOW_S)
        fprime_test_api.clear_histories()
        gated = _read(fprime_test_api, "GatedRuns")
        assert gated >= before + GATED_RUNS_MIN, (
            f"GatedRuns must count every dropped tick: over {GATED_WINDOW_S}s "
            f"with IMU disabled it went from {before} to {gated}"
        )
        assert _read(fprime_test_api, "TasksEnabledMask") == MASK_IMU_DISABLED, (
            f"With only IMU (bit 0) disabled the mask must read {MASK_IMU_DISABLED:#x}"
        )

        _enable(fprime_test_api, "IMU")
        fprime_test_api.assert_event(f"{taskGate}.TaskEnabled", timeout=5)

        fprime_test_api.clear_histories()
        assert _read(fprime_test_api, "TasksEnabledMask") == MASK_ALL_ENABLED, (
            f"TasksEnabledMask must return to {MASK_ALL_ENABLED:#x} after "
            "ENABLE_TASK(IMU)"
        )

        # The counter must now be frozen: two consecutive downlinks, a full
        # TlmChan cycle apart, must report the same total.
        resumed = _read(fprime_test_api, "GatedRuns")
        time.sleep(GATED_WINDOW_S)
        fprime_test_api.clear_histories()
        settled = _read(fprime_test_api, "GatedRuns")
        assert settled == resumed, (
            f"GatedRuns must stop advancing once IMU is re-enabled: it read "
            f"{resumed} and then {settled} a further {GATED_WINDOW_S}s later"
        )
    finally:
        proves_send_and_assert_command(
            fprime_test_api, f"{tlmSend}.SET_LEVEL", [str(DEFAULT_PACKET_LEVEL)]
        )
