"""
fault_manager_test.py:

Integration tests for the FaultManager component, which observes faults
reported by ThermalManager, ModeManager, the LoRa AuthenticationRouter and the
Watchdog, debounces them, and telemeters the result.

SHADOW MODE: the component ships with ``AUTHORITY_ENABLED`` false and
``AUTHORITY_MASK`` 0, so it never calls ``forceSafeMode`` or ``stopWatchdog``.
Every test below therefore observes today's recovery path unchanged and checks
that the FaultManager saw and reported the same fault. Enabling authority per
fault type is a later, separate campaign (Components/FaultManager/docs/sdd.md
"Follow-ups"); nothing here writes an authority parameter.

MARKER CHOICE (uart_only): none of these tests sever the RF command/event link.
``test_watchdog_stop_reports_then_reboots`` reboots the board, which drops the
link for the reboot window, but it does not disable the radio, so commanding
comes back on its own; it is safe on both UART and RF and is ordered last.

PACKET LEVEL: every faultManager channel lives in the ``Faults`` packet
(ReferenceDeploymentPackets.fppi, id 9, group 5), so ``CdhCore.tlmSend.SET_LEVEL
5`` is commanded before they can be read and the level is restored to 1
afterwards.
"""

import time

import pytest
from common import proves_send_and_assert_command
from fprime_gds.common.testing_fw.api import IntegrationTestAPI

faultManager = "ReferenceDeployment.faultManager"
thermalManager = "ReferenceDeployment.thermalManager"
startupManager = "ReferenceDeployment.startupManager"
watchdog = "ReferenceDeployment.watchdog"
tlmSend = "CdhCore.tlmSend"

# The Faults packet is group 5; the deployment runs at level 1 in normal ops.
FAULT_PACKET_LEVEL = 5
DEFAULT_PACKET_LEVEL = 1

# telemetryDelay divides the 1 Hz tick by 29, so a TlmChan cycle is ~30 s. One
# cycle plus margin is long enough to observe a Faults downlink; this is also
# the 45 s bound the FD-L2-09 criterion names.
ONE_PERIOD_WINDOW_S = 45

# FD-L2-01 requires the detection event "within 2 s" of the condition. The
# threshold sweep runs at 1 Hz, so one sweep plus one second of link margin.
DETECT_WINDOW_S = 2


# FD-L2-05 requires the reboot "within 60 s" of the watchdog stopping. The
# hardware watchdog resets the board about 26 s after petting stops; the bound
# is the requirement's, not the hardware's.
REBOOT_WINDOW_S = 60

# ThermalManager face upper threshold default, ThermalManager.fpp. The test
# drops it below any plausible ambient to force a crossing, then restores it.
FACE_TEMP_UPPER_DEFAULT_C = 60.0
FACE_TEMP_UPPER_FORCED_C = -50.0


def _read(fprime_test_api: IntegrationTestAPI, channel: str) -> int:
    """Await one downlink of a FaultManager channel and return it as an int."""
    item = fprime_test_api.await_telemetry(
        f"{faultManager}.{channel}", start="NOW", timeout=ONE_PERIOD_WINDOW_S
    )
    assert item is not None, (
        f"{channel} did not downlink within {ONE_PERIOD_WINDOW_S}s at packet "
        f"level {FAULT_PACKET_LEVEL}"
    )
    return int(item.get_val())


@pytest.fixture(autouse=True)
def restore_thermal_threshold_and_level(fprime_test_api: IntegrationTestAPI, start_gds):
    """Leave the face temperature threshold and the packet level at their
    defaults whatever a test does, so a failure mid-test cannot leave the
    satellite reporting a permanent thermal fault or downlinking at level 5."""
    yield
    proves_send_and_assert_command(
        fprime_test_api,
        f"{thermalManager}.FACE_TEMP_UPPER_THRESHOLD_PRM_SET",
        [FACE_TEMP_UPPER_DEFAULT_C],
    )
    proves_send_and_assert_command(
        fprime_test_api, f"{tlmSend}.SET_LEVEL", [str(DEFAULT_PACKET_LEVEL)]
    )


@pytest.mark.verifies("FD-L2-09", "FaultManager-8")
def test_faults_telemetry_level5(fprime_test_api: IntegrationTestAPI, start_gds):
    """The Faults packet is downlinked at telemetry level 5.

    FaultManager-8 clause: with tlmSend at level 5, FaultsDetected,
    ActiveFaults and AuthorityState arrive within 45 s. AuthorityState is also
    the shadow-mode witness: it must read 0, i.e. no fault type has been given
    recovery authority.
    """
    proves_send_and_assert_command(
        fprime_test_api, f"{tlmSend}.SET_LEVEL", [str(FAULT_PACKET_LEVEL)]
    )
    fprime_test_api.clear_histories()

    _read(fprime_test_api, "FaultsDetected")
    _read(fprime_test_api, "ActiveFaults")
    assert _read(fprime_test_api, "AuthorityState") == 0, (
        "AuthorityState must read 0: the FaultManager ships in shadow mode and "
        "no authority parameter is written by this campaign"
    )


@pytest.mark.verifies("FD-L2-01")
def test_thermal_report_shadow(fprime_test_api: IntegrationTestAPI, start_gds):
    """A thermal threshold crossing raises the existing event AND is confirmed
    by the FaultManager, with no action taken.

    FD-L2-01 clause: "Thermal: TemperatureAboveThreshold within 2 s". The
    threshold is dropped below ambient so the next 1 Hz sweep crosses it; both
    the existing ThermalManager event and the FaultManager's FaultConfirmed
    must follow. ShadowActionsSuppressed must NOT advance, because the thermal
    fault types carry no recovery action even under authority.
    """
    proves_send_and_assert_command(
        fprime_test_api, f"{tlmSend}.SET_LEVEL", [str(FAULT_PACKET_LEVEL)]
    )
    suppressed_before = _read(fprime_test_api, "ShadowActionsSuppressed")

    fprime_test_api.clear_histories()
    proves_send_and_assert_command(
        fprime_test_api,
        f"{thermalManager}.FACE_TEMP_UPPER_THRESHOLD_PRM_SET",
        [FACE_TEMP_UPPER_FORCED_C],
    )

    fprime_test_api.assert_event(
        f"{thermalManager}.TemperatureAboveThreshold", timeout=DETECT_WINDOW_S
    )
    fprime_test_api.assert_event(
        f"{faultManager}.FaultConfirmed", timeout=DETECT_WINDOW_S
    )

    fprime_test_api.clear_histories()
    assert _read(fprime_test_api, "FaultCountThermal") > 0, (
        "The thermal threshold crossing must be counted by the FaultManager"
    )
    assert _read(fprime_test_api, "ShadowActionsSuppressed") == suppressed_before, (
        "A thermal fault has no recovery action, so nothing may be suppressed"
    )


@pytest.mark.verifies("FD-L2-05")
def test_watchdog_stop_reports_then_reboots(
    fprime_test_api: IntegrationTestAPI, start_gds
):
    """STOP_WATCHDOG is reported as a fault and the board reboots.

    FD-L2-05 clause: "watchdog stall -> reboot (BootCount +1 within 60 s)".
    The FaultManager observes the stop (FaultConfirmed(WATCHDOG_STOPPED)) but
    takes no action: the reboot is the hardware watchdog, exactly as today.

    Destructive: this test reboots the board, so it is ordered last.
    """
    proves_send_and_assert_command(
        fprime_test_api, f"{tlmSend}.SET_LEVEL", [str(FAULT_PACKET_LEVEL)]
    )
    boot_item = fprime_test_api.await_telemetry(
        f"{startupManager}.BootCount", start="NOW", timeout=ONE_PERIOD_WINDOW_S
    )
    assert boot_item is not None, "BootCount must downlink before the reboot"
    boot_before = int(boot_item.get_val())

    fprime_test_api.clear_histories()
    proves_send_and_assert_command(fprime_test_api, f"{watchdog}.STOP_WATCHDOG")

    fprime_test_api.assert_event(
        f"{faultManager}.FaultConfirmed", timeout=DETECT_WINDOW_S
    )

    time.sleep(REBOOT_WINDOW_S)
    fprime_test_api.clear_histories()
    boot_after = fprime_test_api.await_telemetry(
        f"{startupManager}.BootCount", start="NOW", timeout=ONE_PERIOD_WINDOW_S
    )
    assert boot_after is not None, "The board must come back after the reboot"
    assert int(boot_after.get_val()) == boot_before + 1, (
        f"Stopping the watchdog must reboot the board within {REBOOT_WINDOW_S}s: "
        f"BootCount went from {boot_before} to {boot_after.get_val()}"
    )


@pytest.mark.skip(
    reason="destructive: reboots and persists SAFE_MODE; run manually on flatsat"
)
@pytest.mark.verifies("FD-L2-01", "FD-L2-05", "FD-L2-09")
def test_command_loss_enters_safe_mode(fprime_test_api: IntegrationTestAPI, start_gds):
    """Command loss enters safe mode and the FaultManager observes it (producer 2).

    Documented, not run in the automated campaign: forcing command loss means
    shortening COMM_LOSS_TIME on modeManager (the timer moved there from the
    retired AuthenticationRouter at the 2026-09 upstream sync) and then not
    commanding for that window, which enters safe mode, stops the watchdog,
    reboots the board and leaves persisted SAFE_MODE behind for every later
    test. Run it by hand on the flatsat, with a manual EXIT_SAFE_MODE afterwards.

    The ``verifies`` marker records which FD-L2 rows this procedure is the
    producer-2 evidence for; a skipped test is reported as deferred by
    scripts/generate_rtm.py, never as passing. The steps are:
      1. SET_LEVEL 5 and record ShadowActionsSuppressed and FaultCountCommandLoss.
      2. PRM_SET COMM_LOSS_TIME to a short window on modeManager.
      3. Stay silent for that window.
      4. Expect modeManager.CommandLossDetected and
         faultManager.FaultConfirmed(COMMAND_LOSS) in the same second, then
         modeManager.EnteringSafeMode within 2 s with
         GET_SAFE_MODE_REASON = COMMAND_LOSS, FaultCountCommandLoss advanced by
         exactly 1 and ShadowActionsSuppressed advanced by exactly 1 (the
         manager would have acted, but has no authority).
      5. After the reboot, EXIT_SAFE_MODE and restore COMM_LOSS_TIME.
    """
    pytest.skip("destructive: reboots and persists SAFE_MODE; run manually on flatsat")
