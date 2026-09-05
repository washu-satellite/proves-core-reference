"""
command_path_test.py:

Integration tests for the ground-to-spacecraft command path: uplink framing,
deframing, authentication bypass, dispatch, execution and acknowledgement.

LEVEL: Board. The observable each test asserts on — a command acknowledgement
event returned over the active link, an echoed argument, an absence of
deframing errors — only exists once a real uplink carries a real CCSDS frame
into the flight software, so these are Board-level tests, not unit tests.

JOBS: both. Every test here runs in the integration-uart job and in the
integration-radio job, except test_05 which is marked ``uart_only`` because it
disables the RF transmitter.

PRECONDITIONS: the ``start_gds`` session fixture has proven the link with a
CMD_NO_OP. No face boards, sensors or power state are required.

WINDOW CONSTANTS (TP-4):
  ACK_S = 10
      The pass criteria for CH-L2-01/07/08 and CDH-19 all state "within 10 s".
      Taken from the criteria text, not from measured behaviour.
  BURST_ACK_S = 15
      CH-L2-09 states "5 OpCodeCompleted within 15 s".
  ROUTING_ACK_S = 5
      CH-L2-10 states "each produce that component's response event within 5 s".
  DEFRAMER_QUIET_S = 5
      Bounded negative window for "zero deframing errors". A deframing error is
      raised in the same 10 Hz comStub cycle (100 ms) that sees the bad frame,
      and the resulting event reaches the ground over the downlink framer,
      which the radio job runs at a 2 s cycle (downlinkDelay DIVIDER 20,
      conftest.py:_enable_radio). Two framer cycles plus margin: 2*2 + 1 = 5 s.
  TELEMETRY_S = 45
      CDH-1 states BootCount "received within 45 s"; one TlmPacketizer period
      (telemetryDelay DIVIDER 29 on the 1 Hz tick = 30 s) plus 15 s margin,
      the same one-period window telemetry_gate_test.py:40 derives.

CLAIMED REQUIREMENTS: CDH-19, CDH-1, CH-L2-01, CH-L2-02, CH-L2-03, CH-L2-07,
CH-L2-08, CH-L2-09, CH-L2-10. Clause claims are named in each docstring.
"""

import time

import pytest
from common import cmdDispatch, proves_send_and_assert_command
from fprime_gds.common.testing_fw.api import IntegrationTestAPI

modeManager = "ReferenceDeployment.modeManager"
startupManager = "ReferenceDeployment.startupManager"
lora = "ReferenceDeployment.lora"

ACK_S = 10
BURST_ACK_S = 15
ROUTING_ACK_S = 5
DEFRAMER_QUIET_S = 5
TELEMETRY_S = 45

# A NO_OP string long enough that the frame carrying it exceeds one 10 Hz UART
# read chunk, which is what CH-L2-03 is about.
LONG_ARG = "A" * 40

# Substrings identifying every uplink-path error event: TcDeframer
# (InvalidCrc / InvalidFrameLength / InvalidSpacecraftId / InvalidVcId),
# SpacePacketDeframer (InvalidLength) and FrameAccumulator
# (FrameDetectionSizeError). Matching on the event *name substring* rather than
# on fully-qualified names means a renamed or relocated event still trips the
# check instead of silently never matching — an exact-name await that never
# matches would pass vacuously.
DEFRAMER_ERROR_SUBSTRINGS = (
    "InvalidCrc",
    "InvalidFrameLength",
    "InvalidSpacecraftId",
    "InvalidVcId",
    "InvalidLength",
    "FrameDetectionSizeError",
)

# Dispatcher errors that CH-L2-09 requires to be absent under a command burst.
DISPATCH_ERROR_SUBSTRINGS = (
    "TooManyCommands",
    "CommandDroppedQueueOverflow",
)


def _event_names(fprime_test_api: IntegrationTestAPI) -> list[str]:
    """Names of every event currently in the test history."""
    return [
        str(e.get_template().get_name())
        for e in fprime_test_api.get_event_test_history()
    ]


def _assert_no_events_matching(
    fprime_test_api: IntegrationTestAPI, substrings: tuple[str, ...], context: str
) -> None:
    """Assert the event history holds no event whose name matches a substring."""
    names = _event_names(fprime_test_api)
    offenders = [n for n in names if any(s in n for s in substrings)]
    assert not offenders, f"{context}: unexpected error events {offenders}"


@pytest.fixture(autouse=True)
def clean_histories(fprime_test_api: IntegrationTestAPI, start_gds):
    """Start and finish each test with empty histories.

    The command path leaves no device state behind, so restoring "the pre-test
    state" (TP-5) here means leaving no stale events for the next test to
    mistake for its own.
    """
    fprime_test_api.clear_histories()
    yield
    fprime_test_api.clear_histories()


@pytest.mark.verifies("CDH-19", "CH-L2-01", "CH-L2-07", "CH-L2-08")
def test_01_no_op_string_round_trip(fprime_test_api: IntegrationTestAPI, start_gds):
    """A NO_OP_STRING is dispatched, executed, acknowledged and echoed intact.

    CH-L2-01 clause claimed here: the acknowledgement clause — CMD_NO_OP is
    acked within 10 s over whichever link this job runs (UART in the
    integration-uart job, LoRa in the integration-radio job) with zero
    tcDeframer InvalidCrc events. The criterion's "over UART *and* over LoRa"
    is satisfied by the two CI jobs together, not by a single run.
    CH-L2-07 clause claimed here: both clauses — the byte-exact echo and the
    modeManager.GET_CURRENT_MODE readback returning a valid SystemMode.
    """
    # CH-L2-01: a bare NO_OP is acknowledged.
    proves_send_and_assert_command(fprime_test_api, f"{cmdDispatch}.CMD_NO_OP")

    # CH-L2-08: dispatch precedes completion for an accepted command.
    fprime_test_api.assert_event_sequence(
        [
            f"{cmdDispatch}.OpCodeDispatched",
            f"{cmdDispatch}.OpCodeCompleted",
        ],
        timeout=ACK_S,
    )

    # CDH-19 / CH-L2-07: the argument is echoed back byte-exact.
    echo_arg = "Hello World!"
    proves_send_and_assert_command(
        fprime_test_api, f"{cmdDispatch}.CMD_NO_OP_STRING", [echo_arg]
    )
    echoed = fprime_test_api.assert_event(
        f"{cmdDispatch}.NoOpStringReceived", timeout=ACK_S
    )
    assert echoed.args[0].val == echo_arg, (
        f"NoOpStringReceived must echo the argument byte-exact; "
        f"sent {echo_arg!r}, received {echoed.args[0].val!r}"
    )

    # CH-L2-07 second clause: a command to another component returns a valid
    # SystemMode reading.
    proves_send_and_assert_command(fprime_test_api, f"{modeManager}.GET_CURRENT_MODE")
    mode = fprime_test_api.assert_event(
        f"{modeManager}.CurrentModeReading", timeout=ROUTING_ACK_S
    )
    assert str(mode.args[0].val) in ("SAFE_MODE", "NORMAL"), (
        f"CurrentModeReading must carry a valid SystemMode, got {mode.args[0].val!r}"
    )

    # CH-L2-01 second clause: no frame was mis-deframed along the way.
    _assert_no_events_matching(
        fprime_test_api, DEFRAMER_ERROR_SUBSTRINGS, "NO_OP round trip"
    )


@pytest.mark.verifies("CH-L2-02", "CH-L2-03")
def test_02_ten_commands_no_deframer_errors(
    fprime_test_api: IntegrationTestAPI, start_gds
):
    """Ten consecutive long-argument commands are each acked, with no framing errors.

    CH-L2-02: 10 consecutive commands each acked within 10 s with zero
    tcDeframer or spacePacketDeframer errors.
    CH-L2-03: each of those carries a 40-character argument, so every frame is
    larger than one 10 Hz UART read chunk and must be reassembled by the frame
    accumulator without a FrameDetectionSizeError; the echo proves the
    reassembly was correct, not merely error-free.
    """
    for i in range(10):
        # proves_send_and_assert_command clears histories per attempt, so each
        # iteration is checked for its own echo and its own framing errors.
        arg = f"{i:02d}{LONG_ARG[2:]}"
        assert len(arg) == 40
        proves_send_and_assert_command(
            fprime_test_api, f"{cmdDispatch}.CMD_NO_OP_STRING", [arg]
        )
        echoed = fprime_test_api.assert_event(
            f"{cmdDispatch}.NoOpStringReceived", timeout=ACK_S
        )
        assert echoed.args[0].val == arg, (
            f"Command {i}: 40-character argument must survive framing intact; "
            f"sent {arg!r}, received {echoed.args[0].val!r}"
        )
        _assert_no_events_matching(
            fprime_test_api, DEFRAMER_ERROR_SUBSTRINGS, f"command {i}"
        )

    # A final bounded quiet window: no deframing error trails the burst.
    fprime_test_api.clear_histories()
    time.sleep(DEFRAMER_QUIET_S)
    _assert_no_events_matching(
        fprime_test_api,
        DEFRAMER_ERROR_SUBSTRINGS,
        f"{DEFRAMER_QUIET_S}s quiet window after 10 commands",
    )


@pytest.mark.verifies("CH-L2-09")
def test_03_burst_of_five_commands(fprime_test_api: IntegrationTestAPI, start_gds):
    """Five commands sent back-to-back all complete without overflowing the queue.

    CH-L2-09: the dispatcher queue is 10 deep (CdhCoreConfig.fpp:6), so a burst
    of 5 unwaited commands must all complete within 15 s with no
    TooManyCommands and no CommandDroppedQueueOverflow.
    """
    fprime_test_api.clear_histories()

    for i in range(5):
        fprime_test_api.send_command(f"{cmdDispatch}.CMD_NO_OP_STRING", [f"burst-{i}"])

    deadline = time.time() + BURST_ACK_S
    completed = 0
    while time.time() < deadline:
        completed = sum(
            1 for n in _event_names(fprime_test_api) if "OpCodeCompleted" in n
        )
        if completed >= 5:
            break
        time.sleep(0.5)

    assert completed >= 5, (
        f"All 5 back-to-back commands must complete within {BURST_ACK_S}s "
        f"(dispatcher queue depth 10); saw {completed} OpCodeCompleted events"
    )
    _assert_no_events_matching(
        fprime_test_api, DISPATCH_ERROR_SUBSTRINGS, "5-command burst"
    )


@pytest.mark.verifies("CH-L2-10")
def test_04_routing_to_three_components(fprime_test_api: IntegrationTestAPI, start_gds):
    """Commands addressed to three different components each reach their target.

    CH-L2-10: each of cmdDisp, modeManager and startupManager produces its own
    response event within 5 s, which is only possible if the dispatcher routed
    each opcode to the right component.
    """
    proves_send_and_assert_command(fprime_test_api, f"{cmdDispatch}.CMD_NO_OP")
    assert (
        fprime_test_api.assert_event(
            f"{cmdDispatch}.OpCodeCompleted", timeout=ROUTING_ACK_S
        )
        is not None
    )

    proves_send_and_assert_command(fprime_test_api, f"{modeManager}.GET_CURRENT_MODE")
    assert (
        fprime_test_api.assert_event(
            f"{modeManager}.CurrentModeReading", timeout=ROUTING_ACK_S
        )
        is not None
    ), "modeManager must answer its own command"

    proves_send_and_assert_command(fprime_test_api, f"{startupManager}.GET_BOOT_COUNT")
    assert (
        fprime_test_api.assert_event(
            f"{startupManager}.CurrentBootCount", timeout=ROUTING_ACK_S
        )
        is not None
    ), "startupManager must answer its own command"


@pytest.mark.uart_only
@pytest.mark.verifies("CDH-1")
def test_05_uart_command_and_telemetry_without_radio(
    fprime_test_api: IntegrationTestAPI, start_gds
):
    """With the RF transmitter disabled, the wired EGSE link still commands and receives.

    CDH-1: over UART with lora.TRANSMIT DISABLED, CMD_NO_OP is acked within
    10 s and startupManager.BootCount is received within 45 s. Marked
    ``uart_only`` (TP-5) because it severs the RF downlink; on the radio job
    that would cut the link this test depends on.
    """
    proves_send_and_assert_command(fprime_test_api, f"{lora}.TRANSMIT", ["DISABLED"])

    proves_send_and_assert_command(fprime_test_api, f"{cmdDispatch}.CMD_NO_OP")
    assert (
        fprime_test_api.assert_event(f"{cmdDispatch}.OpCodeCompleted", timeout=ACK_S)
        is not None
    ), "The wired link must still carry command acknowledgements with RF off"

    fprime_test_api.clear_histories()
    boot_count = fprime_test_api.await_telemetry(
        f"{startupManager}.BootCount", start="NOW", timeout=TELEMETRY_S
    )
    assert boot_count is not None, (
        f"BootCount telemetry must arrive over the wired link within "
        f"{TELEMETRY_S}s while RF transmit is disabled"
    )
