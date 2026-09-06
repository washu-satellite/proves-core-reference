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


@pytest.mark.verifies("TelemetryGate-1", "TelemetryGate-2", "CH-L2-12")
def test_02_disable_ceases_then_enable_resumes_telemetry(
    fprime_test_api: IntegrationTestAPI, start_gds
):
    """Disabling ceases channelized telemetry; re-enabling resumes it.

    CH-L2-12 clause claimed here: the *telemetryGate.SET_TRANSMIT_STATE* clause
    only. ``_set_state`` asserts that SET_TRANSMIT_STATE acks and emits
    TransmitStateSet for DISABLED and again for ENABLED. The other clause of
    CH-L2-12 (``lora.TRANSMIT`` acking OK both ways with no LoRa error event)
    is an RF-level observable that this test never touches; it is covered by
    the radio-job RF-silence test (backlog item, spec D4-8).
    """
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


# ==============================================================================
# Automated tests added for CH-L2-15 / CH-L2-16
#
# Level: Board. Both observables — when telemetry stops relative to the latch,
# and the gate's own counters surviving a disable/enable cycle — exist only on
# the wire. The gating decision itself is covered at unit level by
# test_TelemetryGate_Component.cpp.
#
# Additional window constants (TP-4), on top of the module constants above:
#   LATCH_GRACE_S = 1
#       Items already queued in TlmChan when the command latches may still
#       drain afterwards. CH-L2-15's criterion allows exactly this by measuring
#       against T+1 s, where T is the FSW time of the TransmitStateSet event.
#   GATED_TICKS_MIN = 2
#       CH-L2-16 requires GatedTicks to increase by at least 2 across a
#       DISABLED_WINDOW_S (70 s) outage. At one TlmChan run per 30 s that
#       window spans at least two gated ticks.
#
# telemetryGate.TransmitState and GatedTicks are in the Health packet
# (ReferenceDeploymentPackets.fppi:133-134, group 5), so tlmSend level 5 is
# commanded before they can be read, and restored to 1 afterwards.
# ==============================================================================

LATCH_GRACE_S = 1
GATED_TICKS_MIN = 2

GATE_PACKET_LEVEL = 5
DEFAULT_PACKET_LEVEL = 1

tlmSend = "CdhCore.tlmSend"


def _fsw_seconds(fsw_time) -> float:
    """Flight-software timestamp in seconds."""
    if hasattr(fsw_time, "get_float"):
        return float(fsw_time.get_float())
    return float(fsw_time.seconds) + float(getattr(fsw_time, "useconds", 0)) / 1e6


@pytest.mark.verifies("CH-L2-15")
def test_03_disable_takes_effect_within_one_period(
    fprime_test_api: IntegrationTestAPI, start_gds
):
    """Channelized telemetry stops at the latch, not merely eventually.

    CH-L2-15: after the TransmitStateSet(DISABLED) event at flight-software
    time T, no channelized telemetry timestamped later than T+1 s arrives in
    the following 70 s.

    Measuring against the *flight-software* timestamp rather than ground
    arrival time is what makes this a test of when the gate closed, rather than
    a test of ground-side queueing: an item produced after the latch is a
    failure however late it reaches the ground.
    """
    # Precondition: telemetry is flowing before the latch, so that its absence
    # afterwards is a change and not the status quo.
    fprime_test_api.clear_histories()
    assert (
        fprime_test_api.await_telemetry(
            TELEMETRY_CHANNEL, start="NOW", timeout=ONE_PERIOD_WINDOW_S
        )
        is not None
    ), "Precondition failed: telemetry not flowing before disable"

    subhistory = fprime_test_api.get_telemetry_subhistory()
    proves_send_and_assert_command(
        fprime_test_api,
        f"{telemetryGate}.SET_TRANSMIT_STATE",
        ["DISABLED"],
    )
    latch = fprime_test_api.assert_event(f"{telemetryGate}.TransmitStateSet", timeout=5)
    latch_time = _fsw_seconds(latch.get_time())

    time.sleep(DISABLED_WINDOW_S)
    produced = list(subhistory.retrieve())
    fprime_test_api.remove_telemetry_subhistory(subhistory)

    late = [
        (str(item.get_template().get_name()), _fsw_seconds(item.get_time()))
        for item in produced
        if _fsw_seconds(item.get_time()) > latch_time + LATCH_GRACE_S
    ]
    assert not late, (
        f"No channelized telemetry may be produced more than {LATCH_GRACE_S}s "
        f"after the DISABLED latch at FSW time {latch_time}; got {late}"
    )

    # The fixture re-enables telemetry.


@pytest.mark.verifies("CH-L2-16")
def test_04_gated_ticks_count_and_state_after_reenable(
    fprime_test_api: IntegrationTestAPI, start_gds
):
    """The gate counts what it suppressed and reports its state on re-enable.

    CH-L2-16: after being DISABLED for 70 s and re-ENABLED, GatedTicks has
    increased by at least 2 and TransmitState reads ENABLED within 45 s.

    GatedTicks is itself channelized telemetry, so it cannot be observed while
    the gate is closed — which is the point: the counter has to survive the
    outage and be reported afterwards, not be sampled continuously.
    """
    proves_send_and_assert_command(
        fprime_test_api, f"{tlmSend}.SET_LEVEL", [str(GATE_PACKET_LEVEL)]
    )
    try:
        baseline = fprime_test_api.await_telemetry(
            f"{telemetryGate}.GatedTicks", start="NOW", timeout=ONE_PERIOD_WINDOW_S
        )
        assert baseline is not None, (
            f"Precondition failed: GatedTicks did not downlink within "
            f"{ONE_PERIOD_WINDOW_S}s at packet level {GATE_PACKET_LEVEL}"
        )
        before = int(baseline.get_val())

        _set_state(fprime_test_api, "DISABLED")
        time.sleep(DISABLED_WINDOW_S)
        _set_state(fprime_test_api, "ENABLED")

        fprime_test_api.clear_histories()
        after = fprime_test_api.await_telemetry(
            f"{telemetryGate}.GatedTicks", start="NOW", timeout=ONE_PERIOD_WINDOW_S
        )
        assert after is not None, "GatedTicks did not resume after re-enabling"
        assert int(after.get_val()) >= before + GATED_TICKS_MIN, (
            f"GatedTicks must count every suppressed run: a "
            f"{DISABLED_WINDOW_S}s outage spans at least {GATED_TICKS_MIN} "
            f"TlmChan runs, but the count went from {before} to {after.get_val()}"
        )

        state = fprime_test_api.await_telemetry(
            f"{telemetryGate}.TransmitState",
            start="NOW",
            timeout=ONE_PERIOD_WINDOW_S,
        )
        assert state is not None, "TransmitState did not downlink after re-enabling"
        assert str(state.get_val()).upper().endswith("ENABLED"), (
            f"TransmitState must read ENABLED after re-enabling, got {state.get_val()!r}"
        )
    finally:
        proves_send_and_assert_command(
            fprime_test_api, f"{tlmSend}.SET_LEVEL", [str(DEFAULT_PACKET_LEVEL)]
        )
