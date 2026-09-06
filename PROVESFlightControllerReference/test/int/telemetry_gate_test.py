"""
telemetry_gate_test.py:

Integration tests for the TelemetryGate component, which owns a persisted
telemetry-transmission enable/disable state and gates the scheduler tick that
drives TlmChan (CdhCore.tlmSend.Run). When DISABLED, all channelized telemetry
downlink ceases within one scheduler cycle; events and command acknowledgements
continue to flow normally.

MARKER CHOICE (uart_only): this test deliberately does NOT use
``pytest.mark.uart_only``. That marker is for tests that sever the RF
*command/event* link (radio resets, ``lora.TRANSMIT`` toggling) so that uplink
commands can no longer be delivered over the air. TelemetryGate only gates
*channelized telemetry*: while DISABLED, telemetry stops on ALL links (LoRa and
UART alike), but events and command acks keep downlinking on every link, and
``lora.TRANSMIT`` (RF-level gating) is untouched. Because
``proves_send_and_assert_command`` relies on command acks/events rather than
telemetry, uplink commanding still works over the radio for the whole test.
The test is therefore link-agnostic and safe to run on both UART and RF.
"""

import time

import pytest
from common import proves_send_and_assert_command
from fprime_gds.common.testing_fw.api import IntegrationTestAPI

telemetryGate = "ReferenceDeployment.telemetryGate"

# A channel that TlmChan downlinks steadily under normal operation: StartupManager
# writes BootCount every 1 Hz run, and it is downlinked on each TlmChan cycle.
# Any channelized telemetry works here because gating happens upstream of TlmChan;
# BootCount is chosen because it is always present and continuously updated.
TELEMETRY_CHANNEL = "ReferenceDeployment.startupManager.BootCount"

# telemetryDelay divides the 1 Hz tick by 29, so TlmChan runs roughly every 30 s.
TELEMETRY_PERIOD_S = 30

# One period plus margin: long enough to observe at least one downlink cycle.
ONE_PERIOD_WINDOW_S = TELEMETRY_PERIOD_S + 15

# Just over two periods: long enough to prove telemetry has actually ceased
# (rather than merely being between cycles) while DISABLED.
DISABLED_WINDOW_S = 2 * TELEMETRY_PERIOD_S + 10


def _set_state(fprime_test_api: IntegrationTestAPI, state: str) -> None:
    """Send SET_TRANSMIT_STATE and assert the TransmitStateSet event fires."""
    proves_send_and_assert_command(
        fprime_test_api,
        f"{telemetryGate}.SET_TRANSMIT_STATE",
        [state],
    )
    fprime_test_api.assert_event(f"{telemetryGate}.TransmitStateSet", timeout=5)


@pytest.fixture(autouse=True)
def ensure_enabled(fprime_test_api: IntegrationTestAPI, start_gds):
    """Leave telemetry transmission ENABLED before and after each test so a
    failure mid-test cannot leave the satellite silent for later tests."""
    _set_state(fprime_test_api, "ENABLED")
    yield
    _set_state(fprime_test_api, "ENABLED")


@pytest.mark.verifies("TelemetryGate-3")
def test_01_telemetry_flows_when_enabled(
    fprime_test_api: IntegrationTestAPI, start_gds
):
    """Baseline: with transmission ENABLED, channelized telemetry is downlinked."""
    fprime_test_api.clear_histories()
    result = fprime_test_api.await_telemetry(
        TELEMETRY_CHANNEL, start="NOW", timeout=ONE_PERIOD_WINDOW_S
    )
    assert result is not None, (
        f"Expected {TELEMETRY_CHANNEL} updates while telemetry transmission is "
        f"ENABLED, but none arrived within {ONE_PERIOD_WINDOW_S}s"
    )


@pytest.mark.verifies("TelemetryGate-1", "TelemetryGate-2")
def test_02_disable_ceases_then_enable_resumes_telemetry(
    fprime_test_api: IntegrationTestAPI, start_gds
):
    """Disabling ceases channelized telemetry; re-enabling resumes it."""
    # Confirm telemetry is flowing before we disable.
    fprime_test_api.clear_histories()
    assert (
        fprime_test_api.await_telemetry(
            TELEMETRY_CHANNEL, start="NOW", timeout=ONE_PERIOD_WINDOW_S
        )
        is not None
    ), "Precondition failed: telemetry not flowing before disable"

    # Disable telemetry transmission; the TransmitStateSet event is asserted here.
    _set_state(fprime_test_api, "DISABLED")

    # Allow the in-flight TlmChan cycle to drain, then start a clean observation
    # window. clear_histories() drops any telemetry queued before the disable.
    time.sleep(TELEMETRY_PERIOD_S)
    fprime_test_api.clear_histories()

    # No channelized telemetry should arrive for the observation window. Events
    # and command acks are unaffected, so only the telemetry channel is checked.
    stale = fprime_test_api.await_telemetry(
        TELEMETRY_CHANNEL, start="NOW", timeout=DISABLED_WINDOW_S
    )
    assert stale is None, (
        f"Expected NO {TELEMETRY_CHANNEL} updates while telemetry transmission "
        f"is DISABLED, but received: {stale}"
    )

    # Re-enable and confirm telemetry resumes.
    _set_state(fprime_test_api, "ENABLED")
    fprime_test_api.clear_histories()
    resumed = fprime_test_api.await_telemetry(
        TELEMETRY_CHANNEL, start="NOW", timeout=ONE_PERIOD_WINDOW_S
    )
    assert resumed is not None, (
        f"Expected {TELEMETRY_CHANNEL} updates to resume after re-enabling "
        f"telemetry transmission, but none arrived within {ONE_PERIOD_WINDOW_S}s"
    )
