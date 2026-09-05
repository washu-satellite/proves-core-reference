"""
collection_interval_test.py:

Integration tests for the per-source telemetry collection interval
(``COLLECTION_INTERVAL_S``) added in Cycle B. Each 1 Hz sensor source
(thermalManager, powerMonitor, adcs, imuManager) gates its ``run`` handler on
this parameter, so the channels that source writes update every N seconds
instead of every second. An out-of-range or invalid value falls back to the 1 s
default and raises ``CollectionIntervalRejected``.

thermalManager is the source exercised here because its channels sit in a
single packet (``Thermal`` id 12, group 3) whose members are written by nothing
else, so packet spacing tracks source spacing exactly.

GROUND CONFIGURATION THIS MODULE CHANGES, AND RESTORES:
  * ``CdhCore.tlmSend.SET_LEVEL 3`` — the Thermal packet is group 3 and the
    flight default packet level is 1, so nothing but the Beacon downlinks until
    the level is raised. Level 3, not 6, is used to keep the extra link load to
    the packets these assertions actually read. Restored to 1.
  * ``ReferenceDeployment.telemetryDelay.DIVIDER_PRM_SET 0`` — the packetizer
    normally runs once every 30 ticks (DIVIDER 29), which is far coarser than
    the intervals under test. At DIVIDER 0 it runs every tick, so a packet is
    emitted as soon as a member channel changes and downlink spacing reflects
    collection spacing. Restored to 29.
  * ``thermalManager.COLLECTION_INTERVAL_S_PRM_SET`` — restored to 1.
The autouse ``restore_collection_state`` fixture performs all three restores
even when a test fails, so a mid-test failure cannot leave the satellite
downlinking group-3 telemetry every tick or sampling once a minute.

Both changes raise downlink volume for the duration of the module (roughly a
minute), which is why a UART run is preferred; no RF path is severed, so
``uart_only`` is deliberately NOT applied — commanding works on either link.

MARKER CHOICE (uart_only): nothing here touches ``lora.TRANSMIT`` or resets a
radio, and command acks/events are unaffected by packet level, so the module is
link-agnostic in the same sense as ``telemetry_gate_test.py``.
"""

import time

import pytest
from common import proves_send_and_assert_command
from fprime_gds.common.testing_fw.api import IntegrationTestAPI

thermalManager = "ReferenceDeployment.thermalManager"
telemetryDelay = "ReferenceDeployment.telemetryDelay"
tlmSend = "CdhCore.tlmSend"

# A channel written by a driver port handler that only thermalManager.run
# invokes, so its update spacing is exactly the collection spacing.
TEMPERATURE_CHANNEL = "ReferenceDeployment.tmp112Face0Manager.Temperature"
INTERVAL_CHANNEL = f"{thermalManager}.CollectionIntervalS"
REJECTED_EVENT = f"{thermalManager}.CollectionIntervalRejected"

# Values under test. TEST_INTERVAL_S is inside the accepted 1..60 range;
# REJECTED_INTERVAL_S is below it.
TEST_INTERVAL_S = 5
DEFAULT_INTERVAL_S = 1
REJECTED_INTERVAL_S = 0

# TM-L2-02 criterion: "spaced N +/-1 s over 5 consecutive updates".
SPACING_TOLERANCE_S = 1
CONSECUTIVE_UPDATES = 5

# Packet level: the Thermal packet is group 3 (ReferenceDeploymentPackets.fppi).
THERMAL_PACKET_LEVEL = 3
DEFAULT_PACKET_LEVEL = 1

# telemetryDelay.DIVIDER: 0 runs the packetizer every tick, 29 is the flight
# default (RateDelay period = DIVIDER + 1 ticks).
FAST_DIVIDER = 0
DEFAULT_DIVIDER = 29

# Enough wall time to observe CONSECUTIVE_UPDATES updates at TEST_INTERVAL_S,
# plus a packetizer cycle of margin at each end.
CAPTURE_WINDOW_S = TEST_INTERVAL_S * (CONSECUTIVE_UPDATES + 1) + 10

# One update per second at the default interval, so a short window suffices.
DEFAULT_CAPTURE_WINDOW_S = 12

# Telemetry read timeout: at FAST_DIVIDER the packet follows the write within a
# tick, but the value is "update on change" so it may take one interval to be
# re-sent after a change.
TELEMETRY_TIMEOUT_S = 30


def _fsw_seconds(fsw_time) -> float:
    """Flight-software timestamp in seconds."""
    if hasattr(fsw_time, "get_float"):
        return float(fsw_time.get_float())
    return float(fsw_time.seconds) + float(getattr(fsw_time, "useconds", 0)) / 1e6


def _set_interval(fprime_test_api: IntegrationTestAPI, interval_s: int) -> None:
    """Store a collection interval on thermalManager (RAM only, no PRM_SAVE)."""
    proves_send_and_assert_command(
        fprime_test_api,
        f"{thermalManager}.COLLECTION_INTERVAL_S_PRM_SET",
        [str(interval_s)],
    )


def _await_interval_telemetry(fprime_test_api: IntegrationTestAPI):
    """Read the effective interval thermalManager reports."""
    return fprime_test_api.await_telemetry(
        INTERVAL_CHANNEL, start="NOW", timeout=TELEMETRY_TIMEOUT_S
    )


def _spacings(items) -> list[float]:
    """Gaps in flight-software seconds between consecutive telemetry items."""
    stamps = sorted(_fsw_seconds(item.get_time()) for item in items)
    return [b - a for a, b in zip(stamps, stamps[1:])]


@pytest.fixture(autouse=True)
def restore_collection_state(fprime_test_api: IntegrationTestAPI, start_gds):
    """Raise the packet level and the packetizer rate for the test, then put
    the collection interval, the packetizer divider and the packet level back
    to their flight defaults however the test ends."""
    proves_send_and_assert_command(
        fprime_test_api, f"{tlmSend}.SET_LEVEL", [str(THERMAL_PACKET_LEVEL)]
    )
    proves_send_and_assert_command(
        fprime_test_api,
        f"{telemetryDelay}.DIVIDER_PRM_SET",
        [str(FAST_DIVIDER)],
    )
    try:
        yield
    finally:
        _set_interval(fprime_test_api, DEFAULT_INTERVAL_S)
        proves_send_and_assert_command(
            fprime_test_api,
            f"{telemetryDelay}.DIVIDER_PRM_SET",
            [str(DEFAULT_DIVIDER)],
        )
        proves_send_and_assert_command(
            fprime_test_api, f"{tlmSend}.SET_LEVEL", [str(DEFAULT_PACKET_LEVEL)]
        )


@pytest.mark.verifies("TM-L2-02", "ThermalManager-1")
def test_01_interval_spaces_channel_updates(
    fprime_test_api: IntegrationTestAPI, start_gds
):
    """Setting COLLECTION_INTERVAL_S to N spaces that source's updates N s apart.

    TM-L2-02 criterion: "After a source's COLLECTION_INTERVAL_S is set to N s
    (1..60), that source's channel updates are spaced N +/-1 s over 5
    consecutive updates (telemetryDelay.DIVIDER 0, packet level 3)".

    Spacing is measured on flight-software timestamps, not ground arrival
    times, so ground-side queueing cannot turn a correct schedule into a
    failure or hide an incorrect one.
    """
    _set_interval(fprime_test_api, TEST_INTERVAL_S)

    reported = _await_interval_telemetry(fprime_test_api)
    assert reported is not None, (
        f"{INTERVAL_CHANNEL} did not downlink within {TELEMETRY_TIMEOUT_S}s "
        f"after setting the interval to {TEST_INTERVAL_S}s"
    )
    assert int(reported.get_val()) == TEST_INTERVAL_S, (
        f"thermalManager must report the interval it is running at; expected "
        f"{TEST_INTERVAL_S}, got {reported.get_val()}"
    )

    subhistory = fprime_test_api.get_telemetry_subhistory()
    time.sleep(CAPTURE_WINDOW_S)
    produced = [
        item
        for item in subhistory.retrieve()
        if str(item.get_template().get_full_name()) == TEMPERATURE_CHANNEL
    ]
    fprime_test_api.remove_telemetry_subhistory(subhistory)

    assert len(produced) >= CONSECUTIVE_UPDATES, (
        f"Expected at least {CONSECUTIVE_UPDATES} {TEMPERATURE_CHANNEL} updates "
        f"in {CAPTURE_WINDOW_S}s at a {TEST_INTERVAL_S}s interval, got "
        f"{len(produced)}"
    )

    gaps = _spacings(produced[:CONSECUTIVE_UPDATES])
    out_of_band = [
        gap for gap in gaps if abs(gap - TEST_INTERVAL_S) > SPACING_TOLERANCE_S
    ]
    assert not out_of_band, (
        f"{TEMPERATURE_CHANNEL} updates must be spaced {TEST_INTERVAL_S} "
        f"+/-{SPACING_TOLERANCE_S}s over {CONSECUTIVE_UPDATES} consecutive "
        f"updates; out-of-band gaps: {out_of_band} (all gaps: {gaps})"
    )


@pytest.mark.verifies("ThermalManager-2")
def test_02_out_of_range_interval_is_rejected(
    fprime_test_api: IntegrationTestAPI, start_gds
):
    """An interval of 0 is rejected: the event fires and 1 s stays in force.

    ThermalManager-2 criterion: "An interval of 0 or >60 or an INVALID param
    yields effective 1 s, one CollectionIntervalRejected event, and
    CollectionIntervalS telemetry 1". The generated PRM_SET always acks OK, so
    the rejection is observable only as the event plus the telemetry value.
    """
    fprime_test_api.clear_histories()
    _set_interval(fprime_test_api, REJECTED_INTERVAL_S)

    rejected = fprime_test_api.assert_event(REJECTED_EVENT, timeout=10)
    assert rejected is not None, (
        f"Setting the interval to {REJECTED_INTERVAL_S} must raise {REJECTED_EVENT}"
    )

    reported = _await_interval_telemetry(fprime_test_api)
    assert reported is not None, (
        f"{INTERVAL_CHANNEL} did not downlink within {TELEMETRY_TIMEOUT_S}s "
        f"after the rejected set"
    )
    assert int(reported.get_val()) == DEFAULT_INTERVAL_S, (
        f"A rejected interval must leave {DEFAULT_INTERVAL_S}s in force; "
        f"{INTERVAL_CHANNEL} reads {reported.get_val()}"
    )


@pytest.mark.verifies("TM-L2-02", "ThermalManager-1")
def test_03_default_interval_updates_every_second(
    fprime_test_api: IntegrationTestAPI, start_gds
):
    """Back at the 1 s default, updates are spaced no more than 2 s apart.

    This is the "default equals today" half of TM-L2-02: restoring the
    parameter to 1 must restore the pre-Cycle-B every-tick behaviour, so a
    configured interval can never be left latched by an earlier test.
    """
    _set_interval(fprime_test_api, DEFAULT_INTERVAL_S)

    reported = _await_interval_telemetry(fprime_test_api)
    assert reported is not None, (
        f"{INTERVAL_CHANNEL} did not downlink within {TELEMETRY_TIMEOUT_S}s "
        f"after restoring the default interval"
    )
    assert int(reported.get_val()) == DEFAULT_INTERVAL_S

    subhistory = fprime_test_api.get_telemetry_subhistory()
    time.sleep(DEFAULT_CAPTURE_WINDOW_S)
    produced = [
        item
        for item in subhistory.retrieve()
        if str(item.get_template().get_full_name()) == TEMPERATURE_CHANNEL
    ]
    fprime_test_api.remove_telemetry_subhistory(subhistory)

    assert len(produced) >= CONSECUTIVE_UPDATES, (
        f"Expected at least {CONSECUTIVE_UPDATES} {TEMPERATURE_CHANNEL} updates "
        f"in {DEFAULT_CAPTURE_WINDOW_S}s at the default interval, got "
        f"{len(produced)}"
    )

    gaps = _spacings(produced[:CONSECUTIVE_UPDATES])
    slow = [gap for gap in gaps if gap > DEFAULT_INTERVAL_S + SPACING_TOLERANCE_S]
    assert not slow, (
        f"At the default interval {TEMPERATURE_CHANNEL} must update at least "
        f"every {DEFAULT_INTERVAL_S + SPACING_TOLERANCE_S}s; slow gaps: {slow} "
        f"(all gaps: {gaps})"
    )
