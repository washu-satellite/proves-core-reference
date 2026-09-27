"""
driver_board_test.py:

Integration tests for the DriverBoardHandler component: the 1 Hz supervised
serial link to the magnetorquer driver board (STM32) on uart1 (J18 pins 9/10).

Two hardware configurations, chosen by marker:

  * ``test_00_loopback_smoke`` needs no driver board: TX1 jumpered to RX1 on
    J18 (pin 9 to pin 10). The host receives its own HK_REQUEST frames, which
    the parser accepts (they are well-formed) and the handler ignores (they are
    host-type, not board-type). It is a Desk-USB tier test and carries no
    ``flatsat`` marker.
  * Every other test is marked ``flatsat`` (registered in the repo-root
    ``pytest.ini``): it needs the STM32 attached and answering the protocol in
    ``docs-site/dev-loop/cycles/cycle-e-plan/02-protocol.md``, powered from
    the payload rail (``payloadPowerLoadSwitch``). Run with ``-m flatsat``;
    the CI board runner has no driver board and must run ``-m "not flatsat"``.

The ten steps of DriverBoardHandler-10 (the criterion lists five clauses; the
procedure HP-15 splits them into ten observable steps, one test each here
and all ten in order in ``test_09_end_to_end``):

   1. ``payloadPowerLoadSwitch.TURN_ON``       -> LinkUp event
   2. LinkState UP, DriverState DISARMED        (first host frame is DISARM)
   3. PING                                      -> PongReceived within 2 s
   4. ARM                                       -> Armed, DriverState ARMED
   5. PULSE_DURATION_MS_PRM_SET 500             (RAM-only, restored)
   6. PULSE 0                                   -> PulseStarted(500, 50, 7, 0)
   7. CoilCurrent0..2 non-zero within 2 s of PulseStarted (FSW time)
   8. DISARM                                    -> Disarmed(COMMAND), DISARMED
   9. ``payloadPowerLoadSwitch.TURN_OFF``      -> LinkLost within
      LINK_TIMEOUT_MS + 1 s of silence, LinkState DOWN
  10. LinkTimeouts incremented by exactly one over the run

Telemetry setup, restored by the autouse ``restore_driver_board_state``
fixture however a test ends:
  * ``CdhCore.tlmSend.SET_LEVEL 3`` - the PayloadHousekeeping packet is group 3
    (``ReferenceDeploymentPackets.fppi``, packet id 23); at the flight level
    (1) none of its 22 channels is downlinked.
  * ``ReferenceDeployment.telemetryDelay.DIVIDER_PRM_SET 0`` - the packetizer
    runs every 1 Hz tick instead of every 30, so a channel write reaches the
    ground within about a tick instead of up to 30 s.
  * the five ``driverBoardHandler`` parameters back to their defaults, and
    DISARM, so a failed test cannot leave the board ARMED for the next one.

Reading the packet: the packetizer is PACKET_UPDATE_ON_CHANGE. While the board
answers HK, BoardUptime changes every second and the whole packet (all 22
channels) is downlinked every tick. With the board off nothing changes and no
packet is sent, so a state change seen once (LinkState DOWN after LinkLost) is
read by searching the history since the last command (``start=None``), never
by waiting for a fresh update (``start="NOW"``). ``proves_send_and_assert_command``
clears the histories before every send, so "since the last command" is exact.

MARKER CHOICE (uart_only): not used. Nothing here touches the RF link; all
observables are events and channels that flow on every link.
"""

import pytest
from common import proves_send_and_assert_command
from fprime_gds.common.testing_fw import predicates
from fprime_gds.common.testing_fw.api import IntegrationTestAPI

flatsat = pytest.mark.flatsat

driverBoardHandler = "ReferenceDeployment.driverBoardHandler"
payloadPower = "ReferenceDeployment.payloadPowerLoadSwitch"
tlmSend = "CdhCore.tlmSend"
telemetryDelay = "ReferenceDeployment.telemetryDelay"
cmdDisp = "CdhCore.cmdDisp"

# ------------------------------------------------------------------------------
# Flight defaults (DriverBoardHandler.fpp) restored on teardown
# ------------------------------------------------------------------------------
DEFAULT_PULSE_DURATION_MS = 2000
DEFAULT_PULSE_DUTY_PCT = 50
DEFAULT_PULSE_CHANNEL_MASK = 0x07
DEFAULT_LINK_TIMEOUT_MS = 1000
DEFAULT_HK_INTERVAL_S = 1

PAYLOAD_PACKET_LEVEL = 3
DEFAULT_PACKET_LEVEL = 1
FAST_DIVIDER = 0
DEFAULT_DIVIDER = 29

# ------------------------------------------------------------------------------
# Windows. Each is derived from a protocol or handler constant; none is a guess
# at how fast the ground link happens to be on the day.
# ------------------------------------------------------------------------------

# Ground-side delivery allowance on top of every flight-side window: at
# FAST_DIVIDER a channel write is packetized on the next 1 Hz tick, and UART
# downlink of a 67-byte packet plus the events around it is well under a
# second; five ticks absorb the packetizer tick, the 1 Hz supervision tick
# and command-ack round trips.
DELIVERY_MARGIN_S = 5

# Criterion: PING -> PongReceived within 2 s. Measured on flight-software
# timestamps between the PING's OpCodeCompleted (the handler sends the frame
# before it responds) and the PongReceived event.
PONG_WINDOW_S = 2

# Criterion: HK shows non-zero CoilCurrent on the masked channels within 2 s
# of PulseStarted. Measured on flight-software timestamps.
COIL_CURRENT_WINDOW_S = 2

# Criterion: LinkLost within LINK_TIMEOUT_MS + 1 s of the board going silent.
# The handler measures the silence itself and reports it as the event's
# silentMs argument; that argument is the observable, and the event must
# also reach the ground within the same window plus delivery.
LINK_LOST_MAX_SILENT_MS = DEFAULT_LINK_TIMEOUT_MS + 1000
LINK_LOST_WINDOW_S = LINK_LOST_MAX_SILENT_MS / 1000 + DELIVERY_MARGIN_S

# Link-up after power-on: the STM32 must boot (02-protocol gives no figure;
# BOARD_BOOT_S is the bench allowance and is the first thing to correct from
# the HP-15 log if it is wrong), then answer the next HK_REQUEST, which the
# host sends every HK_INTERVAL_S; the reply lands within one more tick.
BOARD_BOOT_S = 3
LINK_UP_WINDOW_S = BOARD_BOOT_S + DEFAULT_HK_INTERVAL_S + 1 + DELIVERY_MARGIN_S

# 01-scope for TM-L2-01 / CDH-27 / ADCS-L2-06: CoilCurrent0..2 update at
# least once per 45 s at level 3. With HK every second the board answers
# 45 times in that window; the window is the plan's, not derived here.
HK_UPDATE_WINDOW_S = 45

# Loopback: FramesReceived climbs by one per HK_REQUEST the host hears back,
# i.e. one per HK_INTERVAL_S. Two intervals plus delivery is enough to see it
# move at least once.
LOOPBACK_WINDOW_S = 2 * DEFAULT_HK_INTERVAL_S + DELIVERY_MARGIN_S

PULSE_TEST_DURATION_MS = 500
PULSE_POLARITY_MASK = 0

COIL_CURRENT_CHANNELS = [f"{driverBoardHandler}.CoilCurrent{i}" for i in range(3)]


class _EnumReads(predicates.predicate):
    """Value predicate for an enum channel: the decoded value ends with NAME
    (the GDS may render it bare or qualified with the enum type)."""

    def __init__(self, name: str):
        self.name = name.upper()

    def __call__(self, item):
        return str(item).upper().endswith(self.name)

    def __str__(self):
        return f"enum value reads {self.name}"


def _fsw_seconds(fsw_time) -> float:
    """Flight-software timestamp in seconds."""
    if hasattr(fsw_time, "get_float"):
        return float(fsw_time.get_float())
    return float(fsw_time.seconds) + float(getattr(fsw_time, "useconds", 0)) / 1e6


def _set_param(fprime_test_api: IntegrationTestAPI, name: str, value) -> None:
    proves_send_and_assert_command(
        fprime_test_api, f"{driverBoardHandler}.{name}_PRM_SET", [str(value)]
    )


def _restore_parameters(fprime_test_api: IntegrationTestAPI) -> None:
    _set_param(fprime_test_api, "PULSE_DURATION_MS", DEFAULT_PULSE_DURATION_MS)
    _set_param(fprime_test_api, "PULSE_DUTY_PCT", DEFAULT_PULSE_DUTY_PCT)
    _set_param(fprime_test_api, "PULSE_CHANNEL_MASK", DEFAULT_PULSE_CHANNEL_MASK)
    _set_param(fprime_test_api, "LINK_TIMEOUT_MS", DEFAULT_LINK_TIMEOUT_MS)
    _set_param(fprime_test_api, "HK_INTERVAL_S", DEFAULT_HK_INTERVAL_S)


def _await_fresh(fprime_test_api: IntegrationTestAPI, channel: str, timeout: float):
    """A new update of ``channel`` arriving after this call (a flowing packet)."""
    result = fprime_test_api.await_telemetry(channel, start="NOW", timeout=timeout)
    assert result is not None, f"{channel} did not downlink within {timeout}s"
    return result


def _read_since_command(
    fprime_test_api: IntegrationTestAPI,
    channel: str,
    value=None,
    timeout=DELIVERY_MARGIN_S,
):
    """An update of ``channel`` (matching ``value`` if given) in the history
    since the last command, awaiting up to ``timeout`` if it is not there yet."""
    result = fprime_test_api.await_telemetry(
        channel, value=value, start=None, timeout=timeout
    )
    assert result is not None, (
        f"{channel} {value if value is not None else ''} not seen within {timeout}s"
    )
    return result


def _assert_enum_channel(
    fprime_test_api: IntegrationTestAPI, channel: str, expected: str
) -> None:
    _read_since_command(fprime_test_api, channel, value=_EnumReads(expected))


def _completed_fsw_seconds(fprime_test_api: IntegrationTestAPI) -> float:
    """FSW time of the last command's completion.

    ``proves_send_and_assert_command`` clears the histories before it sends,
    so the first OpCodeCompleted in the history belongs to that command.
    """
    completed = fprime_test_api.assert_event(f"{cmdDisp}.OpCodeCompleted", timeout=10)
    return _fsw_seconds(completed.get_time())


def _power_on_and_await_link(fprime_test_api: IntegrationTestAPI) -> None:
    """Step 1: power the board; the host's HK_REQUEST cadence brings the link up."""
    proves_send_and_assert_command(fprime_test_api, f"{payloadPower}.TURN_ON")
    fprime_test_api.assert_event(
        f"{driverBoardHandler}.LinkUp", timeout=LINK_UP_WINDOW_S
    )


def _ping(fprime_test_api: IntegrationTestAPI) -> None:
    """Step 3: PING -> PongReceived within PONG_WINDOW_S of the command."""
    proves_send_and_assert_command(fprime_test_api, f"{driverBoardHandler}.PING")
    sent = _completed_fsw_seconds(fprime_test_api)
    pong = fprime_test_api.assert_event(
        f"{driverBoardHandler}.PongReceived", timeout=PONG_WINDOW_S + DELIVERY_MARGIN_S
    )
    elapsed = _fsw_seconds(pong.get_time()) - sent
    assert elapsed <= PONG_WINDOW_S, (
        f"PongReceived must follow PING within {PONG_WINDOW_S}s of FSW time, took {elapsed:.3f}s"
    )


def _arm(fprime_test_api: IntegrationTestAPI) -> None:
    """Step 4: ARM -> Armed on the board's ACK; DriverState ARMED."""
    proves_send_and_assert_command(fprime_test_api, f"{driverBoardHandler}.ARM")
    fprime_test_api.assert_event(
        f"{driverBoardHandler}.Armed", timeout=DELIVERY_MARGIN_S
    )
    _assert_enum_channel(fprime_test_api, f"{driverBoardHandler}.DriverState", "ARMED")


def _pulse(fprime_test_api: IntegrationTestAPI) -> float:
    """Steps 5-6: 500 ms pulse on the default mask; returns PulseStarted's FSW time."""
    _set_param(fprime_test_api, "PULSE_DURATION_MS", PULSE_TEST_DURATION_MS)
    proves_send_and_assert_command(
        fprime_test_api, f"{driverBoardHandler}.PULSE", [str(PULSE_POLARITY_MASK)]
    )
    started = fprime_test_api.assert_event(
        f"{driverBoardHandler}.PulseStarted",
        args=[
            PULSE_TEST_DURATION_MS,
            DEFAULT_PULSE_DUTY_PCT,
            DEFAULT_PULSE_CHANNEL_MASK,
            PULSE_POLARITY_MASK,
        ],
        timeout=DELIVERY_MARGIN_S,
    )
    return _fsw_seconds(started.get_time())


def _assert_coil_current(fprime_test_api: IntegrationTestAPI, pulse_fsw: float) -> None:
    """Step 7: every masked coil reports non-zero current within COIL_CURRENT_WINDOW_S."""
    for channel in COIL_CURRENT_CHANNELS:
        item = _read_since_command(
            fprime_test_api,
            channel,
            value=predicates.not_equal_to(0.0),
            timeout=COIL_CURRENT_WINDOW_S + DELIVERY_MARGIN_S,
        )
        elapsed = _fsw_seconds(item.get_time()) - pulse_fsw
        assert elapsed <= COIL_CURRENT_WINDOW_S, (
            f"{channel} must show current within {COIL_CURRENT_WINDOW_S}s of PulseStarted, "
            f"first non-zero sample came {elapsed:.3f}s after"
        )


def _disarm(fprime_test_api: IntegrationTestAPI) -> None:
    """Step 8: DISARM -> Disarmed(COMMAND); DriverState DISARMED."""
    proves_send_and_assert_command(fprime_test_api, f"{driverBoardHandler}.DISARM")
    fprime_test_api.assert_event(
        f"{driverBoardHandler}.Disarmed", args=["COMMAND"], timeout=DELIVERY_MARGIN_S
    )
    _assert_enum_channel(
        fprime_test_api, f"{driverBoardHandler}.DriverState", "DISARMED"
    )


def _power_off_and_await_lost(fprime_test_api: IntegrationTestAPI) -> None:
    """Step 9: power off -> LinkLost with silentMs <= LINK_TIMEOUT_MS + 1 s; LinkState DOWN."""
    proves_send_and_assert_command(fprime_test_api, f"{payloadPower}.TURN_OFF")
    lost = fprime_test_api.assert_event(
        f"{driverBoardHandler}.LinkLost", timeout=LINK_LOST_WINDOW_S
    )
    silent_ms = int(lost.get_args()[0].val)
    assert silent_ms <= LINK_LOST_MAX_SILENT_MS, (
        f"LinkLost must fire within LINK_TIMEOUT_MS + 1 s = {LINK_LOST_MAX_SILENT_MS} ms "
        f"of silence, handler reported {silent_ms} ms"
    )
    _assert_enum_channel(fprime_test_api, f"{driverBoardHandler}.LinkState", "DOWN")


@pytest.fixture(autouse=True)
def restore_driver_board_state(fprime_test_api: IntegrationTestAPI, start_gds):
    """Raise the packet level and the packetizer rate for the test, then put
    the handler parameters, the driver state, the divider and the packet level
    back to their flight defaults however the test ends."""
    proves_send_and_assert_command(
        fprime_test_api, f"{tlmSend}.SET_LEVEL", [str(PAYLOAD_PACKET_LEVEL)]
    )
    proves_send_and_assert_command(
        fprime_test_api, f"{telemetryDelay}.DIVIDER_PRM_SET", [str(FAST_DIVIDER)]
    )
    try:
        yield
    finally:
        # DISARM always responds OK: local state goes DISARMED whether or not
        # the link is up, so this is safe with the board off or absent.
        proves_send_and_assert_command(fprime_test_api, f"{driverBoardHandler}.DISARM")
        _restore_parameters(fprime_test_api)
        proves_send_and_assert_command(
            fprime_test_api, f"{telemetryDelay}.DIVIDER_PRM_SET", [str(DEFAULT_DIVIDER)]
        )
        proves_send_and_assert_command(
            fprime_test_api, f"{tlmSend}.SET_LEVEL", [str(DEFAULT_PACKET_LEVEL)]
        )


@pytest.fixture
def board_link(fprime_test_api: IntegrationTestAPI, start_gds):
    """Power the driver board and wait for the link; power it off afterwards
    (flatsat tests only; the loopback test must not request it)."""
    _power_on_and_await_link(fprime_test_api)
    try:
        yield
    finally:
        proves_send_and_assert_command(fprime_test_api, f"{payloadPower}.TURN_OFF")


# ==============================================================================
# Desk-USB tier: TX1 jumpered to RX1, no driver board, payload rail off
# ==============================================================================


def test_00_loopback_smoke(fprime_test_api: IntegrationTestAPI, start_gds):
    """With J18 pin 9 jumpered to pin 10 the host hears its own HK_REQUEST
    frames: the parser accepts them (FramesReceived climbs, FramesRejected
    stays 0) and the handler ignores them (LinkState stays DOWN, because a
    host-type frame is not a board frame). This is also the first real test
    of the lock discipline: bytes arrive on the 50 Hz thread while the 1 Hz
    thread sends, so a remaining lock inversion shows here without the STM32.
    """
    fprime_test_api.clear_histories()
    first = _await_fresh(
        fprime_test_api, f"{driverBoardHandler}.FramesReceived", LOOPBACK_WINDOW_S
    )
    before = int(first.get_val())
    later = fprime_test_api.await_telemetry(
        f"{driverBoardHandler}.FramesReceived",
        value=predicates.greater_than(before),
        start="NOW",
        timeout=LOOPBACK_WINDOW_S,
    )
    assert later is not None, (
        f"FramesReceived must climb while TX1 is looped to RX1 (one HK_REQUEST per "
        f"{DEFAULT_HK_INTERVAL_S}s), stayed at {before} for {LOOPBACK_WINDOW_S}s"
    )
    rejected = _await_fresh(
        fprime_test_api, f"{driverBoardHandler}.FramesRejected", DELIVERY_MARGIN_S
    )
    assert int(rejected.get_val()) == 0, (
        f"A looped-back host frame is well-formed and must not be rejected, "
        f"FramesRejected = {rejected.get_val()}"
    )
    link = _await_fresh(
        fprime_test_api, f"{driverBoardHandler}.LinkState", DELIVERY_MARGIN_S
    )
    assert _EnumReads("DOWN")(link.get_val()), (
        f"A host-type frame is not a board frame: LinkState must stay DOWN, got {link.get_val()!r}"
    )


# ==============================================================================
# Flatsat tier: STM32 driver board on J18 9/10, answering 02-protocol
# ==============================================================================


@flatsat
def test_01_power_on_brings_link_up(
    fprime_test_api: IntegrationTestAPI, start_gds, board_link
):
    """Steps 1-2: LinkUp after power-on (fixture), LinkState UP, DISARMED."""
    _assert_enum_channel(fprime_test_api, f"{driverBoardHandler}.LinkState", "UP")
    _assert_enum_channel(
        fprime_test_api, f"{driverBoardHandler}.DriverState", "DISARMED"
    )


@flatsat
def test_02_ping_is_answered_within_window(
    fprime_test_api: IntegrationTestAPI, start_gds, board_link
):
    """Step 3: PING -> PongReceived within 2 s; FirmwareVersion telemetry non-zero."""
    _ping(fprime_test_api)
    version = _read_since_command(
        fprime_test_api,
        f"{driverBoardHandler}.FirmwareVersion",
        value=predicates.not_equal_to(0),
    )
    assert int(version.get_val()) != 0, (
        "FirmwareVersion must carry the PONG's firmware version"
    )


@flatsat
@pytest.mark.verifies("TM-L2-01", "CDH-27", "ADCS-L2-06")
def test_03_hk_channels_update(
    fprime_test_api: IntegrationTestAPI, start_gds, board_link
):
    """The payload is a registered telemetry source: with the board answering
    HK_REQUEST, each CoilCurrent channel updates at least once per 45 s at
    packet level 3 (two consecutive fresh updates per channel, each within
    the window).

    TM-L2-01 clause claimed here: the *payload* source only ("payload [not
    integrated]" in the criterion becomes integrated by this packet). The
    other sources are covered by telemetry_sources_test.py::test_01.
    CDH-27 / ADCS-L2-06: the controls telemetry exists in a downlinkable packet
    (PayloadHousekeeping, group 3); the 2-day cadence is an ops setting.
    """
    for channel in COIL_CURRENT_CHANNELS:
        _await_fresh(fprime_test_api, channel, HK_UPDATE_WINDOW_S)
        _await_fresh(fprime_test_api, channel, HK_UPDATE_WINDOW_S)


@flatsat
def test_04_arm_is_acknowledged(
    fprime_test_api: IntegrationTestAPI, start_gds, board_link
):
    """Step 4: ARM -> Armed on the board's ACK; DriverState ARMED."""
    _arm(fprime_test_api)


@flatsat
def test_05_pulse_drives_masked_coils(
    fprime_test_api: IntegrationTestAPI, start_gds, board_link
):
    """Steps 5-7: a 500 ms pulse at the default duty and mask starts and shows
    non-zero current on every masked coil within 2 s."""
    _arm(fprime_test_api)
    pulse_fsw = _pulse(fprime_test_api)
    _assert_coil_current(fprime_test_api, pulse_fsw)


@flatsat
def test_06_disarm_is_immediate(
    fprime_test_api: IntegrationTestAPI, start_gds, board_link
):
    """Step 8: DISARM from ARMED -> Disarmed(COMMAND), DriverState DISARMED."""
    _arm(fprime_test_api)
    _disarm(fprime_test_api)


@flatsat
def test_07_power_off_loses_link(
    fprime_test_api: IntegrationTestAPI, start_gds, board_link
):
    """Step 9: powering the board off -> LinkLost within LINK_TIMEOUT_MS + 1 s
    of silence (the event's silentMs), LinkState DOWN."""
    _power_off_and_await_lost(fprime_test_api)


@flatsat
def test_08_link_loss_disarms(
    fprime_test_api: IntegrationTestAPI, start_gds, board_link
):
    """Step 9 while ARMED: link loss also disarms (Disarmed(LINK_LOST)) and the
    next PULSE is refused (CommandRefused). Board evidence for the unit-level
    DriverBoardHandler-5; claims nothing."""
    _arm(fprime_test_api)
    _power_off_and_await_lost(fprime_test_api)
    fprime_test_api.assert_event(
        f"{driverBoardHandler}.Disarmed", args=["LINK_LOST"], timeout=DELIVERY_MARGIN_S
    )
    _assert_enum_channel(
        fprime_test_api, f"{driverBoardHandler}.DriverState", "DISARMED"
    )
    fprime_test_api.clear_histories()
    fprime_test_api.send_command(
        f"{driverBoardHandler}.PULSE", [str(PULSE_POLARITY_MASK)]
    )
    fprime_test_api.assert_event(
        f"{driverBoardHandler}.CommandRefused", timeout=DELIVERY_MARGIN_S
    )


@flatsat
@pytest.mark.verifies("DriverBoardHandler-10")
def test_09_end_to_end(fprime_test_api: IntegrationTestAPI, start_gds):
    """All ten steps in order on one power cycle: the DriverBoardHandler-10
    criterion as a single run. Powers the board itself (no board_link fixture)
    so step 1 and step 9 are part of the measured sequence."""
    try:
        _power_on_and_await_link(fprime_test_api)  # 1
        _assert_enum_channel(
            fprime_test_api, f"{driverBoardHandler}.LinkState", "UP"
        )  # 2
        _assert_enum_channel(
            fprime_test_api, f"{driverBoardHandler}.DriverState", "DISARMED"
        )
        # Baseline for step 10, read while the packet flows (board answering).
        timeouts_before = int(
            _read_since_command(
                fprime_test_api, f"{driverBoardHandler}.LinkTimeouts"
            ).get_val()
        )
        _ping(fprime_test_api)  # 3
        _arm(fprime_test_api)  # 4
        pulse_fsw = _pulse(fprime_test_api)  # 5-6
        _assert_coil_current(fprime_test_api, pulse_fsw)  # 7
        _disarm(fprime_test_api)  # 8
        _power_off_and_await_lost(fprime_test_api)  # 9
        timeouts_after = int(  # 10: the link-loss packet is the last one sent
            _read_since_command(
                fprime_test_api,
                f"{driverBoardHandler}.LinkTimeouts",
                value=predicates.not_equal_to(timeouts_before),
            ).get_val()
        )
        assert timeouts_after == timeouts_before + 1, (
            f"One power-off must count exactly one link timeout, "
            f"LinkTimeouts went {timeouts_before} -> {timeouts_after}"
        )
    finally:
        proves_send_and_assert_command(fprime_test_api, f"{payloadPower}.TURN_OFF")
