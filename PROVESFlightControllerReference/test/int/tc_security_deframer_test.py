"""
tc_security_deframer_test.py:

Integration test for the TcSecurityDeframer's persisted anti-replay sequence
number (CH-L2-05, board clause: the last accepted sequence number survives a
commanded reboot, so a replayed frame is still rejected after a reset).

How the observable is reached. Every accepted frame persists its sequence
number through the deframer's PersistedRecord (TcSecurityDeframer.cpp,
``writeSequenceNumber``), the same path SET_SEQ_NUM uses, so the record on
disk at reset time holds the sequence number of the WARM_RESET frame itself:
the reset command is the last frame the UART instance accepts before the
reboot, and its write lands after any SET_SEQ_NUM the test could send. Both
deframer instances (``ComCcsdsLora``, ``ComCcsdsUart``) load that one shared
record in ``configure()`` at boot. The UART instance then advances on every
frame the test sends, but the LoRa instance sees no frames on a UART-only run,
so ``ComCcsdsLora.tcSecurityDeframer`` reports exactly the value restored
from disk: GET_SEQ_NUM answers with ``SequenceNumberGet`` within a command
round trip and ``CurrentSequenceNumber`` is telemetered at boot and carried in
the Beacon packet.

Marked ``uart_only`` because the reboot severs the RF link
(mode_manager_test.py::test_safe_09 is the reset exemplar).
"""

from datetime import datetime

import pytest
from common import proves_send_and_assert_command
from fprime_gds.common.data_types.event_data import EventData
from fprime_gds.common.models.serialize.time_type import TimeType
from fprime_gds.common.testing_fw.api import IntegrationTestAPI

pytestmark = [pytest.mark.uart_only]

uartDeframer = "ComCcsdsUart.tcSecurityDeframer"
loraDeframer = "ComCcsdsLora.tcSecurityDeframer"
resetManager = "ReferenceDeployment.resetManager"

# Command round trip on the RP2350 (mode_manager_test.py MODE_READBACK_S).
READBACK_S = 5

# FrameworkVersion is the first event after a reset (reset_manager_test.py:38-43).
REBOOT_EVENT_S = 15

# The Beacon packet (ReferenceDeploymentPackets.fppi, holds
# CurrentSequenceNumber) is downlinked once per TlmPacketizer cycle, about 30 s
# (telemetryDelay divides the 1 Hz tick by 29); one period plus margin, as in
# telemetry_gate_test.py ONE_PERIOD_WINDOW_S.
TLM_WINDOW_S = 45

# Frames the ground sends between the pre-reset readback and the reboot: the
# WARM_RESET frame itself (send_command, no retry), so the persisted number is
# exactly one ahead of the readback. Allow for proves_send_and_assert_command
# retrying the readback (common.py _DEFAULT_RETRIES) by bounding rather than
# equating: any value in (before, before + RESET_FRAME_MARGIN] was persisted
# by the reset frame; 0 or a stale value means the record was lost.
RESET_FRAME_MARGIN = 3


def _get_seq_num(fprime_test_api: IntegrationTestAPI, deframer: str) -> int:
    """Read one deframer instance's runtime counter via GET_SEQ_NUM."""
    fprime_test_api.clear_histories()
    proves_send_and_assert_command(fprime_test_api, f"{deframer}.GET_SEQ_NUM")
    evt: EventData = fprime_test_api.assert_event(
        f"{deframer}.SequenceNumberGet", timeout=READBACK_S
    )
    return int(evt.args[0].val)


@pytest.fixture(autouse=True)
def link_still_commands(fprime_test_api: IntegrationTestAPI, start_gds):
    """Prove the UART link accepts commands after the test, so a reboot that
    lost the record (and left the ground's counter out of window) fails here
    rather than as a cascade of unrelated failures in later tests."""
    yield
    _get_seq_num(fprime_test_api, uartDeframer)


@pytest.mark.verifies("CH-L2-05")
def test_01_sequence_number_survives_warm_reset(
    fprime_test_api: IntegrationTestAPI, start_gds
):
    """The last accepted sequence number is restored from the PersistedRecord
    after WARM_RESET: the LoRa instance reports it via SequenceNumberGet and
    CurrentSequenceNumber, and it is ahead of the pre-reset value, not 0."""
    before = _get_seq_num(fprime_test_api, uartDeframer)

    start: TimeType = TimeType().set_datetime(
        datetime.now(), time_base=TimeType.TimeBase("TB_DONT_CARE")
    )
    fprime_test_api.clear_histories()
    fprime_test_api.send_command(f"{resetManager}.WARM_RESET")
    fprime_test_api.assert_event(
        "CdhCore.version.FrameworkVersion", start=start, timeout=REBOOT_EVENT_S
    )

    # The LoRa instance loaded the shared record at boot and has accepted no
    # frame since, so its counter is the persisted value.
    after = _get_seq_num(fprime_test_api, loraDeframer)
    assert before < after <= before + RESET_FRAME_MARGIN, (
        f"Sequence number must survive the reset as the reset frame's number "
        f"(read {before} before, expected in ({before}, "
        f"{before + RESET_FRAME_MARGIN}]), got {after}"
    )

    # The channel written by configure() at boot agrees with the command.
    result = fprime_test_api.await_telemetry(
        f"{loraDeframer}.CurrentSequenceNumber", start="NOW", timeout=TLM_WINDOW_S
    )
    assert result is not None, (
        f"CurrentSequenceNumber not downlinked within {TLM_WINDOW_S} s of the reboot"
    )
    assert int(result.get_val()) == after, (
        f"CurrentSequenceNumber must carry the restored value {after}, "
        f"got {result.get_val()}"
    )
