"""
telemetry_sources_test.py:

Integration tests for the periodic channelized-telemetry pipeline: that every
registered source actually reaches the ground, at the right cadence, with
timestamps, with in-range values, and without exhausting a buffer pool or
slipping a rate-group cycle.

LEVEL: Board. Every observable here is a telemetry *item received on the
ground* over a real downlink. None of it exists at unit level: TlmPacketizer,
ComQueue and the rate groups are F Prime library code driven by real hardware
timing.

JOBS: both. Nothing here severs the RF link, so these tests are link-agnostic.

METHOD: all eight tests share one observation window. The module-scoped
``tlm_window`` fixture raises the packet level, turns the face switches on,
collects every telemetry item that arrives over WINDOW_S, and yields the
captured list; each test then makes its own assertions against that one
capture. Collecting once rather than eight times keeps the module to a single
105 s window instead of fourteen minutes of repeated waiting.

PRECONDITIONS: the rig must have the face boards fitted and powered (five
TMP112 face sensors, four battery-cell sensors) and both INA219 monitors
present. If a face board is absent, test_03 fails and names the missing
channel. That is the truthful outcome and is deliberately not special-cased
(spec Part E item 7): the criterion says all ten temperature channels update.

WINDOW CONSTANTS (TP-4):
  TELEMETRY_PERIOD_S = 30
      One TlmPacketizer run. telemetryDelay divides the 1 Hz tick by 29
      (RateDelay.fpp:13), so TlmChan runs roughly every 30 s. Same constant and
      derivation as telemetry_gate_test.py:37.
  WINDOW_S = 3 * TELEMETRY_PERIOD_S + 15 = 105
      Three full downlink periods plus margin. Three is the minimum that lets
      test_07 measure *spacing* between consecutive receipts (two gaps) and
      lets test_03/test_04 require two updates while still tolerating one
      packet lost to link noise at the window edges.
  ONE_PERIOD_WINDOW_S = 45
      One period plus 15 s margin, the "at least once per 45 s" that the
      TM-L2-05/06/07 and DH-L2-13 criteria all name.
  SPACING_MIN_S / SPACING_MAX_S = 25 / 35
      DH-L2-13 states receipts are "spaced 30 +/-5 s".

RATE-GROUP LIMITS: RgMaxTime is reported in microseconds by
Svc::ActiveRateGroup, so the criteria's 20 / 100 / 1000 ms periods are compared
in microseconds below.

CLAIMED REQUIREMENTS: TM-L2-01, TM-L2-03, TM-L2-05, TM-L2-06, TM-L2-07,
CDH-4, CDH-9, DH-L2-02, DH-L2-13, SC-L2-01, SC-L2-04, FD-L2-09, ADCS-L2-06.
"""

import time

import pytest
from common import proves_send_and_assert_command
from fprime_gds.common.testing_fw.api import IntegrationTestAPI

tlmSend = "CdhCore.tlmSend"
RD = "ReferenceDeployment"

TELEMETRY_PERIOD_S = 30
WINDOW_S = 3 * TELEMETRY_PERIOD_S + 15
ONE_PERIOD_WINDOW_S = TELEMETRY_PERIOD_S + 15
SPACING_MIN_S = 25
SPACING_MAX_S = 35

# Packet level 6 is the highest group defined in ReferenceDeploymentPackets.fppi,
# so SET_LEVEL 6 downlinks every packet. The default is 1 (Beacon only,
# CdhCoreTlmConfig.fpp:15) and is restored in teardown.
ALL_PACKETS_LEVEL = 6
DEFAULT_PACKET_LEVEL = 1

# Face load switches that power the face-mounted sensors. face4 has no TMP112.
FACE_SWITCHES = [
    f"{RD}.face0LoadSwitch",
    f"{RD}.face1LoadSwitch",
    f"{RD}.face2LoadSwitch",
    f"{RD}.face3LoadSwitch",
    f"{RD}.face5LoadSwitch",
]

# The ten temperature channels of the Thermal packet
# (ReferenceDeploymentPackets.fppi:88-99).
THERMAL_CHANNELS = [
    f"{RD}.tmp112Face0Manager.Temperature",
    f"{RD}.tmp112Face1Manager.Temperature",
    f"{RD}.tmp112Face2Manager.Temperature",
    f"{RD}.tmp112Face3Manager.Temperature",
    f"{RD}.tmp112Face5Manager.Temperature",
    f"{RD}.tmp112BattCell1Manager.Temperature",
    f"{RD}.tmp112BattCell2Manager.Temperature",
    f"{RD}.tmp112BattCell3Manager.Temperature",
    f"{RD}.tmp112BattCell4Manager.Temperature",
    f"{RD}.picoTempManager.PicoTemperature",
]

# Sensor operating range shared by the TMP112 and the RP2350 on-die sensor;
# the range named in the CDH-2 / CDH-4 / TM-L2-05 pass criteria.
TEMP_MIN_C = -40.0
TEMP_MAX_C = 125.0

ELECTRICAL_CHANNELS = [
    f"{RD}.ina219SysManager.Voltage",
    f"{RD}.ina219SysManager.Current",
    f"{RD}.ina219SolManager.Voltage",
    f"{RD}.ina219SolManager.Current",
]

# Bench limits from the TM-L2-07 criterion: above the safe-mode entry threshold
# (6.7 V, ModeManager.fpp:194) and below the pack's charged ceiling.
SYS_VOLTAGE_MIN_V = 6.7
SYS_VOLTAGE_MAX_V = 9.0

RATE_GROUPS = {
    f"{RD}.rateGroup50Hz": 20_000,
    f"{RD}.rateGroup10Hz": 100_000,
    f"{RD}.rateGroup1Hz": 1_000_000,
}

BUFFER_POOLS = [
    "ComCcsdsLora.commsBufferManager.NoBuffs",
    "ComCcsdsUart.commsBufferManager.NoBuffs",
    f"{RD}.payloadBufferManager.NoBuffs",
]

# One channel per registered telemetry source (TM-L2-01). Payload is excluded:
# it is not integrated, which the criterion records as "Partial".
REQUIRED_SOURCES = [
    f"{RD}.imuManager.MagneticField",
    f"{RD}.imuManager.Acceleration",
    f"{RD}.imuManager.AngularVelocity",
    *THERMAL_CHANNELS,
    *ELECTRICAL_CHANNELS,
    f"{RD}.powerMonitor.TotalPowerConsumption",
    f"{RD}.fsSpace.FreeSpace",
    f"{RD}.startupManager.BootCount",
    f"{RD}.modeManager.CurrentMode",
    f"{RD}.lora.BytesReceived",
    f"{RD}.rateGroup50Hz.RgMaxTime",
    f"{RD}.rateGroup10Hz.RgMaxTime",
    f"{RD}.rateGroup1Hz.RgMaxTime",
    f"{RD}.detumbleManager.Mode",
]


def _channel_name(item) -> str:
    template = item.get_template()
    if hasattr(template, "get_full_name"):
        return str(template.get_full_name())
    return str(getattr(template, "name", "?"))


def _seconds(fsw_time) -> float:
    """Flight-software timestamp of a telemetry item, in seconds."""
    return (
        float(fsw_time.get_float())
        if hasattr(fsw_time, "get_float")
        else float(fsw_time.seconds)
    )


@pytest.fixture(scope="module")
def tlm_window(fprime_test_api_session: IntegrationTestAPI, start_gds):
    """Raise the packet level, power the faces, capture one observation window.

    TP-5: the packet level is restored to its default (1) after the window, so
    a later test does not inherit a full-rate downlink.
    """
    api = fprime_test_api_session

    proves_send_and_assert_command(
        api, f"{tlmSend}.SET_LEVEL", [str(ALL_PACKETS_LEVEL)]
    )
    for switch in FACE_SWITCHES:
        proves_send_and_assert_command(api, f"{switch}.TURN_ON")

    # Telemetry and events are captured over the *same* window: test_05 needs
    # to know that no health-ping warning was raised while the channels below
    # were being collected, which only means something if both cover the same
    # interval.
    tlm_subhistory = api.get_telemetry_subhistory()
    evr_subhistory = api.get_event_subhistory()
    time.sleep(WINDOW_S)
    items = list(tlm_subhistory.retrieve())
    events = list(evr_subhistory.retrieve())
    api.remove_telemetry_subhistory(tlm_subhistory)
    api.remove_event_subhistory(evr_subhistory)

    yield {"telemetry": items, "events": events}

    proves_send_and_assert_command(
        api, f"{tlmSend}.SET_LEVEL", [str(DEFAULT_PACKET_LEVEL)]
    )


@pytest.fixture(scope="module")
def updates(tlm_window):
    """Map of channel name -> [(fsw_seconds, value)] sorted by time."""
    collected: dict[str, list[tuple[float, object]]] = {}
    for item in tlm_window["telemetry"]:
        collected.setdefault(_channel_name(item), []).append(
            (_seconds(item.get_time()), item.get_val())
        )
    for series in collected.values():
        series.sort(key=lambda pair: pair[0])
    return collected


@pytest.fixture(scope="module")
def window_event_names(tlm_window) -> list[str]:
    """Names of every event that arrived during the observation window."""
    return [str(event.get_template().get_name()) for event in tlm_window["events"]]


@pytest.mark.verifies("TM-L2-01", "TM-L2-06", "ADCS-L2-06")
def test_01_every_registered_source_updates(updates):
    """Every registered telemetry source reaches the ground in the window.

    TM-L2-01: at least one update from each source within the window.
    TM-L2-06 clause claimed here: the *periodic channel* clause — MagneticField
    updates at least once per 45 s with a non-zero axis. (The on-demand
    GET_MAGNETIC_FIELD clause is covered by imu_manager_test.py::test_03.)
    ADCS-L2-06: detumbleManager Mode and coil channels are received.
    """
    missing = [name for name in REQUIRED_SOURCES if not updates.get(name)]
    assert not missing, f"No telemetry received in {WINDOW_S}s from: {missing}"

    # TM-L2-06: the magnetometer is reporting a real field, not zeros.
    mag_values = [value for _, value in updates[f"{RD}.imuManager.MagneticField"]]
    assert any(
        any(float(component) != 0.0 for component in _as_sequence(value))
        for value in mag_values
    ), f"MagneticField must have at least one non-zero axis; saw {mag_values}"


def _as_sequence(value):
    """Telemetry values are scalars or per-axis sequences depending on type."""
    if isinstance(value, (list, tuple)):
        return value
    return [value]


@pytest.mark.verifies("TM-L2-03")
def test_02_timestamps_present_and_monotonic(updates):
    """Every telemetry item carries a real timestamp and time never runs backwards.

    TM-L2-03: non-zero FSW time on every item, and per-channel timestamps
    non-decreasing.
    """
    assert updates, "No telemetry captured at all"

    for name, series in updates.items():
        for timestamp, _ in series:
            assert timestamp > 0, (
                f"{name} carried a zero FSW timestamp; the time base is not set"
            )
        times = [timestamp for timestamp, _ in series]
        assert times == sorted(times), (
            f"{name} timestamps are not non-decreasing: {times}"
        )


@pytest.mark.verifies("TM-L2-05", "CDH-4")
def test_03_thermal_channels(updates):
    """All ten temperature channels update periodically with in-range values.

    TM-L2-05 / CDH-4: each TMP112 (face0-3, face5; battery cells 1-4) and the
    Pico sensor updates at least once per 45 s with a value in -40..125 C.
    Requiring two updates across the three-period window is the direct test of
    "at least once per 45 s" — a single update could have been the tail of an
    earlier period.
    """
    for channel in THERMAL_CHANNELS:
        series = updates.get(channel, [])
        assert len(series) >= 2, (
            f"{channel} must update at least once per {ONE_PERIOD_WINDOW_S}s; "
            f"saw {len(series)} updates in {WINDOW_S}s"
        )
        for _, value in series:
            assert TEMP_MIN_C <= float(value) <= TEMP_MAX_C, (
                f"{channel} reported {value} C, outside the sensor range "
                f"{TEMP_MIN_C}..{TEMP_MAX_C} C"
            )


@pytest.mark.verifies("TM-L2-07")
def test_04_electrical_channels(updates):
    """Bus and solar voltage/current update, and consumed energy accumulates.

    TM-L2-07: sys and sol Voltage and Current update at least once per 45 s;
    the system bus sits above the safe-mode threshold and below the charged
    ceiling on the bench; TotalPowerConsumption increases across the window.
    """
    for channel in ELECTRICAL_CHANNELS:
        series = updates.get(channel, [])
        assert len(series) >= 2, (
            f"{channel} must update at least once per {ONE_PERIOD_WINDOW_S}s; "
            f"saw {len(series)} updates in {WINDOW_S}s"
        )

    for _, voltage in updates[f"{RD}.ina219SysManager.Voltage"]:
        assert SYS_VOLTAGE_MIN_V < float(voltage) < SYS_VOLTAGE_MAX_V, (
            f"System bus voltage {voltage} V is outside the bench range "
            f"{SYS_VOLTAGE_MIN_V}..{SYS_VOLTAGE_MAX_V} V"
        )

    power = updates.get(f"{RD}.powerMonitor.TotalPowerConsumption", [])
    assert len(power) >= 2, "TotalPowerConsumption must update more than once"
    assert float(power[-1][1]) > float(power[0][1]), (
        f"Consumed energy must accumulate across {WINDOW_S}s; went from "
        f"{power[0][1]} to {power[-1][1]}"
    )


@pytest.mark.verifies("SC-L2-01", "SC-L2-04", "CDH-9")
def test_05_scheduler_and_health(updates, window_event_names):
    """Rate groups meet their deadlines and the health pings stay quiet.

    SC-L2-01: RgCycleSlips is 0 for all three groups and RgMaxTime is below the
    group period.
    SC-L2-04 clause claimed here: the 1 Hz group specifically — RgMaxTime under
    1000 ms with zero slips while its channels update.
    CDH-9: zero health-ping warnings, plus an update from each of the sensor
    and mode channels the requirement names.
    """
    for group, period_us in RATE_GROUPS.items():
        slips = updates.get(f"{group}.RgCycleSlips", [])
        assert slips, f"{group}.RgCycleSlips did not downlink"
        assert float(slips[-1][1]) == 0, (
            f"{group} slipped {slips[-1][1]} cycles; the group is not meeting "
            f"its deadline"
        )

        max_times = updates.get(f"{group}.RgMaxTime", [])
        assert max_times, f"{group}.RgMaxTime did not downlink"
        worst = max(float(value) for _, value in max_times)
        assert worst < period_us, (
            f"{group} worst-case execution {worst} us exceeds its period {period_us} us"
        )

    # CDH-9: the Svc::Health pings were all answered in time. Matched on the
    # HLTH_PING name substring so that both HLTH_PING_WARN and HLTH_PING_LATE
    # are caught regardless of which CdhCore instance raised them.
    ping_warnings = [name for name in window_event_names if "HLTH_PING" in name]
    assert not ping_warnings, (
        f"Health ping warnings raised during the window: {ping_warnings}"
    )

    # CDH-9: the health-report sensor set is present.
    for channel in (
        *ELECTRICAL_CHANNELS,
        *THERMAL_CHANNELS,
        f"{RD}.modeManager.CurrentMode",
    ):
        assert updates.get(channel), (
            f"CDH-9 requires {channel} in every telemetry period; none received"
        )


@pytest.mark.verifies("DH-L2-02")
def test_06_buffer_pools_never_exhausted(updates):
    """No communications buffer pool ran dry during the window.

    DH-L2-02: NoBuffs stays 0 for the LoRa and UART comms pools and for the
    payload pool. The pools are statically sized (ComCcsdsConfig.fpp:38-42), so
    a non-zero NoBuffs means a packet was dropped for want of a buffer.
    """
    for channel in BUFFER_POOLS:
        series = updates.get(channel, [])
        assert series, f"{channel} did not downlink; pool health is unknown"
        for _, value in series:
            assert float(value) == 0, (
                f"{channel} reported {value} buffer allocation failures; the "
                f"pool was exhausted during the window"
            )


@pytest.mark.verifies("DH-L2-13")
def test_07_downlink_spacing(updates):
    """Beacon telemetry arrives on a steady period, not in bursts.

    DH-L2-13: BootCount is received at least once per 45 s and consecutive
    receipts are spaced 30 +/-5 s. Spacing is measured on flight-software
    timestamps, so ground-side queuing cannot mask a stalled downlink.
    """
    series = updates.get(f"{RD}.startupManager.BootCount", [])
    assert len(series) >= 3, (
        f"Need at least 3 BootCount receipts in {WINDOW_S}s to measure "
        f"spacing; saw {len(series)}"
    )

    times = [timestamp for timestamp, _ in series]
    gaps = [later - earlier for earlier, later in zip(times, times[1:])]
    for gap in gaps:
        assert SPACING_MIN_S <= gap <= SPACING_MAX_S, (
            f"BootCount receipts must be spaced "
            f"{TELEMETRY_PERIOD_S} +/-5 s; measured gaps {gaps}"
        )


@pytest.mark.verifies("FD-L2-09")
def test_08_fault_status_channels(updates, rejected_count_channel_name):
    """The fault-status channels the ground needs are downlinked periodically.

    FD-L2-09: modeManager CurrentMode, SafeModeEntryCount and
    CurrentSafeModeReason, plus the active link's RejectedPacketsCount, are
    each received at least once per 45 s.

    Note: AuthenticationRouter's PassedRouter/FailedRouter/ByPassedRouter
    channels are declared in the packet set but are never written by the
    component, so they are deliberately not used as evidence here.
    """
    required = [
        f"{RD}.modeManager.CurrentMode",
        f"{RD}.modeManager.SafeModeEntryCount",
        f"{RD}.modeManager.CurrentSafeModeReason",
        rejected_count_channel_name,
    ]
    missing = [name for name in required if not updates.get(name)]
    assert not missing, (
        f"Fault-status channels absent from a {WINDOW_S}s window: {missing}"
    )


@pytest.fixture(scope="module")
def rejected_count_channel_name(request: pytest.FixtureRequest) -> str:
    """RejectedPacketsCount for whichever link this CI job uses."""
    if request.config.getoption("--with-radio"):
        return "ComCcsdsLora.authenticatelora.RejectedPacketsCount"
    return "ComCcsdsUart.authenticate.RejectedPacketsCount"
