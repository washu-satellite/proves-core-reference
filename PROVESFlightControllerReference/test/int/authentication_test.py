"""
authentication_test.py:

Integration test for uplink command authentication: a replayed (stale
sequence number) command frame must be rejected, counted, and must not reach
the command dispatcher, while the next correctly numbered command is accepted.

LEVEL: Board. The observable — a rejection event raised by the flight
software's Authenticate component against a frame that actually crossed the
uplink, plus the RejectedPacketsCount channel on the downlink — only exists on
hardware with a real uplink.

JOBS: both. On the integration-uart job the active link is
``ComCcsdsUart.authenticate``; on the integration-radio job it is
``ComCcsdsLora.authenticatelora``. The test selects the channel by the
``--with-radio`` option.

HOW THE REPLAY IS PROVOKED: the GDS framing plugin re-reads
``Framing/src/sequence_number.bin`` on every frame it builds
(authenticate_plugin.py), so writing an older value into that file makes the
*next* uplink frame a genuine replay from the flight software's point of view.
Nothing on the spacecraft is modified.

WHY GET_BOOT_COUNT IS THE PROBE: CMD_NO_OP and GET_SEQ_NUM bypass
authentication entirely (Authenticate.cpp:30-36), so neither can demonstrate a
rejection. ``startupManager.GET_BOOT_COUNT`` is a normal authenticated command
with its own distinct response event, which makes "the command did not
execute" directly observable rather than inferred.

PRECONDITIONS AND CAVEATS:
  * cwd is the repo root (the same assumption sync_sequence_number_test.py:26
    makes) so that SEQ_FILE resolves.
  * The ``sync_sequence_number`` test must have run first, as it does under the
    default CI filter, so that the file and the flight-software counter agree
    before this test perturbs them.
  * OPEN ITEM (spec Part E item 5): ``sync_sequence_number_test`` syncs only
    the *LoRa* Authenticate instance, while the UART instance keeps its own
    in-RAM counter. If the UART job's counter is not in sync at entry, this
    test fails at its precondition rather than silently passing — which is the
    intended behaviour, but the sync gap should be closed before this test is
    relied on for UART evidence.
  * ``SequenceNumberOutOfWindow`` is declared ``throttle 2``
    (Authenticate.fpp:26). If two rejections already occurred earlier in the
    session the event will not be re-emitted and this test fails. That is a
    truthful failure of the evidence, not a flaky test.

WINDOW CONSTANTS (TP-4):
  REJECT_WINDOW_S = 5
      Bounded negative window for "the command did not execute". The uplink
      command path is sub-second end to end (10 Hz comStub, immediate
      dispatch); 5 s is the repo's standard event timeout and covers more than
      two full command-path cycles plus downlink framer latency.
  TLM_WINDOW_S = 45
      One TlmPacketizer period (telemetryDelay DIVIDER 29 on the 1 Hz tick =
      30 s) plus 15 s margin — the same one-period window
      telemetry_gate_test.py:40 derives. RejectedPacketsCount is in packet
      group 5, so tlmSend level 5 is commanded first.

CLAIMED REQUIREMENTS: CH-L2-05, CH-L2-06.
"""

import time
from pathlib import Path

import pytest
from common import proves_send_and_assert_command
from fprime_gds.common.testing_fw.api import IntegrationTestAPI

startupManager = "ReferenceDeployment.startupManager"
cmdDisp = "CdhCore.cmdDisp"
tlmSend = "CdhCore.tlmSend"

SEQ_FILE = Path("Framing/src/sequence_number.bin")

REJECT_WINDOW_S = 5
TLM_WINDOW_S = 45

# How far to wind the sequence number back. Any value below the accepted
# window works; 100 is far outside the 50000-wide window (Authenticate.fpp:63)
# in the *backwards* direction, which is what "replay" means.
SEQ_REWIND = 100

# Packet group holding the Authenticate channels (ReferenceDeploymentPackets.fppi:159).
AUTH_PACKET_LEVEL = 5
DEFAULT_PACKET_LEVEL = 1


def _read_seq() -> int:
    return int(SEQ_FILE.read_text(encoding="utf-8").strip())


def _write_seq(value: int) -> None:
    SEQ_FILE.write_text(str(value), encoding="utf-8")


def _await_event_named(
    fprime_test_api: IntegrationTestAPI, substring: str, timeout: float
) -> str | None:
    """Poll the event history for an event whose name contains ``substring``.

    Matching on a substring rather than a fully-qualified name keeps the test
    valid on either link (ComCcsdsUart.authenticate vs
    ComCcsdsLora.authenticatelora) and means a renamed or relocated event
    surfaces as a mismatch rather than as a silent non-match.
    """
    deadline = time.time() + timeout
    while True:
        for evt in fprime_test_api.get_event_test_history():
            name = str(evt.get_template().get_name())
            if substring in name:
                return name
        if time.time() >= deadline:
            return None
        time.sleep(0.25)


@pytest.fixture
def rejected_count_channel(request: pytest.FixtureRequest) -> str:
    """The RejectedPacketsCount channel of whichever link this job uses."""
    if request.config.getoption("--with-radio"):
        return "ComCcsdsLora.authenticatelora.RejectedPacketsCount"
    return "ComCcsdsUart.authenticate.RejectedPacketsCount"


@pytest.fixture(autouse=True)
def restore_sequence_file(fprime_test_api: IntegrationTestAPI, start_gds):
    """Restore the GDS sequence-number file and prove the link still works.

    TP-5: the test deliberately puts the ground station's framer out of step
    with the spacecraft. If that were left in place every subsequent test in
    the session would fail to command the board, so the original value is
    written back unconditionally and then *verified* with a real authenticated
    command. A failure here is raised loudly rather than swallowed, because a
    silently unrestored sequence number would turn into a cascade of unrelated
    failures later in the run.
    """
    original = _read_seq()
    yield
    _write_seq(original)
    proves_send_and_assert_command(fprime_test_api, f"{startupManager}.GET_BOOT_COUNT")


@pytest.fixture(autouse=True)
def authenticate_packet_level(fprime_test_api: IntegrationTestAPI, start_gds):
    """Raise the packet level so Authenticate channels downlink, then restore it.

    TlmPacketizer starts at level 1 (CdhCoreTlmConfig.fpp:15), which downlinks
    the Beacon packet only; RejectedPacketsCount lives in group 5.
    """
    proves_send_and_assert_command(
        fprime_test_api, f"{tlmSend}.SET_LEVEL", [str(AUTH_PACKET_LEVEL)]
    )
    yield
    proves_send_and_assert_command(
        fprime_test_api, f"{tlmSend}.SET_LEVEL", [str(DEFAULT_PACKET_LEVEL)]
    )


@pytest.mark.verifies("CH-L2-05", "CH-L2-06")
def test_01_stale_sequence_number_is_rejected(
    fprime_test_api: IntegrationTestAPI,
    start_gds,
    rejected_count_channel: str,
):
    """A replayed command frame is rejected, counted, and never dispatched.

    CH-L2-05: SequenceNumberOutOfWindow within 5 s and no OpCodeDispatched;
    the next correctly numbered command is accepted.
    CH-L2-06: RejectedPacketsCount increases by exactly 1 for that rejected
    packet, and no OpCodeDispatched/OpCodeCompleted appears for its opcode.
    """
    original_seq = _read_seq()

    before = fprime_test_api.await_telemetry(
        rejected_count_channel, start="NOW", timeout=TLM_WINDOW_S
    )
    assert before is not None, (
        f"Precondition failed: {rejected_count_channel} did not downlink within "
        f"{TLM_WINDOW_S}s at packet level {AUTH_PACKET_LEVEL}"
    )
    before_count = before.get_val()

    # Wind the ground framer's sequence number backwards so the next frame is a
    # replay from the spacecraft's point of view.
    _write_seq(max(original_seq - SEQ_REWIND, 0))

    fprime_test_api.clear_histories()
    # Raw send: the command is expected NOT to complete, so the retrying
    # assert-helper must not be used here.
    fprime_test_api.send_command(f"{startupManager}.GET_BOOT_COUNT")

    # CH-L2-05: the rejection is reported.
    rejection = _await_event_named(
        fprime_test_api, "SequenceNumberOutOfWindow", REJECT_WINDOW_S
    )
    assert rejection is not None, (
        f"A replayed frame must raise SequenceNumberOutOfWindow within "
        f"{REJECT_WINDOW_S}s; saw none"
    )

    # CH-L2-05 / CH-L2-06: the replayed command never reached the dispatcher,
    # and therefore never executed.
    assert (
        fprime_test_api.await_event(
            f"{cmdDisp}.OpCodeDispatched", timeout=REJECT_WINDOW_S
        )
        is None
    ), "A rejected frame must not be dispatched to any component"
    assert (
        fprime_test_api.await_event(
            f"{startupManager}.CurrentBootCount", timeout=REJECT_WINDOW_S
        )
        is None
    ), "A rejected GET_BOOT_COUNT must not produce a boot-count reading"

    # CH-L2-05 second clause: the link recovers for a correctly numbered frame.
    _write_seq(original_seq)
    proves_send_and_assert_command(fprime_test_api, f"{startupManager}.GET_BOOT_COUNT")
    assert (
        fprime_test_api.assert_event(
            f"{startupManager}.CurrentBootCount", timeout=REJECT_WINDOW_S
        )
        is not None
    ), "The next correctly numbered command must be accepted and executed"

    # CH-L2-06: exactly one packet was counted as rejected.
    after = fprime_test_api.await_telemetry(
        rejected_count_channel, start="NOW", timeout=TLM_WINDOW_S
    )
    assert after is not None, (
        f"{rejected_count_channel} did not downlink after the rejection"
    )
    assert after.get_val() == before_count + 1, (
        f"Exactly one packet should have been counted as rejected: "
        f"{rejected_count_channel} went from {before_count} to {after.get_val()}"
    )
