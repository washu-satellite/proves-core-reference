"""
safe_mode_test.py:

Integration tests for the ModeManager component (Normal <-> Safe mode transitions).

Tests cover:
- CurrentSafeModeReason verification via command/event
- Safe mode reason tracking (GROUND_COMMAND, LOW_BATTERY, SYSTEM_FAULT)
- Auto safe mode entry due to low voltage (manual test - requires voltage control)
- Auto safe mode exit/recovery when voltage > 8V (manual test - requires voltage control)
- Unintended reboot detection (manual test - requires reboot)

SafeModeReason Test Coverage:
| Reason           | Value | Test                    | Status                          |
|------------------|-------|-------------------------|---------------------------------|
| NONE             | 0     | test_safe_01, 03        | ✅ Automated                    |
| LOW_BATTERY      | 1     | test_safe_05, 06        | ⏸️ Manual (voltage control)     |
| SYSTEM_FAULT     | 2     | test_safe_07            | ⏸️ Manual (power cycle)         |
| GROUND_COMMAND   | 3     | test_safe_02, 04        | ✅ Automated                    |
| EXTERNAL_REQUEST | 4     | N/A                     | ❌ Internal port only           |
| LORA             | 5     | N/A                     | ❌ Internal port only           |

Note: EXTERNAL_REQUEST is triggered via forceSafeMode port (used by other components).
      LORA is triggered by LoRa driver when communication timeout/fault occurs.
      These cannot be tested via ground commands in integration tests.

Total: 8 tests (4 automated, 4 manual)

SafeModeReason enum values: NONE=0, LOW_BATTERY=1, SYSTEM_FAULT=2, GROUND_COMMAND=3, EXTERNAL_REQUEST=4, LORA=5
Mode enum values: SAFE_MODE=1, NORMAL=2
"""

import logging
import time
from datetime import datetime

import pytest
from common import proves_send_and_assert_command
from fprime_gds.common.data_types.event_data import EventData
from fprime_gds.common.models.serialize.time_type import TimeType
from fprime_gds.common.testing_fw.api import IntegrationTestAPI

logger = logging.getLogger(__name__)

component = "ReferenceDeployment.modeManager"

# Voltage thresholds (must match ModeManager.hpp constants)
SAFE_MODE_ENTRY_VOLTAGE = 6.7  # Volts - threshold for entering safe mode from NORMAL
SAFE_MODE_RECOVERY_VOLTAGE = 8.0  # Volts - threshold for auto-recovery from safe mode
SAFE_MODE_DEBOUNCE_SECONDS = 10  # Consecutive seconds for safe mode transitions

# SafeModeReason enum values (must match ModeManager.fpp)
SAFE_MODE_REASON_NONE = "NONE"
SAFE_MODE_REASON_LOW_BATTERY = "LOW_BATTERY"
SAFE_MODE_REASON_SYSTEM_FAULT = "SYSTEM_FAULT"
SAFE_MODE_REASON_GROUND_COMMAND = "GROUND_COMMAND"
SAFE_MODE_REASON_EXTERNAL_REQUEST = "EXTERNAL_REQUEST"
SAFE_MODE_REASON_LORA = "LORA"

# Mode enum values
MODE_SAFE_MODE = "SAFE_MODE"
MODE_NORMAL = "NORMAL"


def get_current_mode(fprime_test_api: IntegrationTestAPI) -> str:
    """Helper function to get current mode via command and event"""
    fprime_test_api.clear_histories()

    proves_send_and_assert_command(
        fprime_test_api,
        f"{component}.GET_CURRENT_MODE",
    )

    result: EventData = fprime_test_api.assert_event(
        f"{component}.CurrentModeReading", timeout=5
    )

    return result.args[0].val


def get_safe_mode_reason(fprime_test_api: IntegrationTestAPI) -> str:
    """Helper function to get safe mode reason via command and event"""
    fprime_test_api.clear_histories()

    proves_send_and_assert_command(
        fprime_test_api,
        f"{component}.GET_SAFE_MODE_REASON",
    )

    result: EventData = fprime_test_api.assert_event(
        f"{component}.CurrentSafeModeReasonReading", timeout=5
    )

    return result.args[0].val


@pytest.fixture(autouse=True)
def setup_and_teardown(fprime_test_api: IntegrationTestAPI, start_gds):
    """
    Setup before each test and cleanup after.
    Ensures clean NORMAL state with explicit verification.
    """
    # Check current mode before setup
    current_mode = get_current_mode(fprime_test_api)

    # If in SAFE_MODE, exit to NORMAL
    if current_mode == MODE_SAFE_MODE:
        logger.info("Setup: Component in SAFE_MODE, exiting to NORMAL...")

        # Get safe mode reason for diagnostics
        reason = get_safe_mode_reason(fprime_test_api)
        logger.info(f"Setup: Safe mode reason = {reason}")

        try:
            proves_send_and_assert_command(
                fprime_test_api,
                f"{component}.EXIT_SAFE_MODE",
                events=[f"{component}.ExitingSafeMode"],
            )
            logger.info("Setup: Successfully exited safe mode")
        except Exception as e:
            pytest.fail(
                f"Setup failed: Cannot exit safe mode (reason: {reason}). Error: {e}"
            )

    # Clear histories AFTER state initialization
    fprime_test_api.clear_histories()

    # Verify we're in NORMAL mode before test starts
    final_mode = get_current_mode(fprime_test_api)
    if final_mode != MODE_NORMAL:
        pytest.fail(f"Setup failed: Expected NORMAL mode, got {final_mode}")

    yield

    # Teardown: Return to NORMAL state for next test
    try:
        mode = get_current_mode(fprime_test_api)
        if mode == MODE_SAFE_MODE:
            proves_send_and_assert_command(
                fprime_test_api,
                f"{component}.EXIT_SAFE_MODE",
                events=[f"{component}.ExitingSafeMode"],
            )
            logger.info("Teardown: Exited safe mode")
    except Exception as e:
        logger.warning(f"Teardown: Could not return to NORMAL state: {e}")


# ==============================================================================
# SafeModeReason Tests
# ==============================================================================


def test_safe_01_initial_safe_mode_reason_is_none(
    fprime_test_api: IntegrationTestAPI, start_gds
):
    """
    Test that CurrentSafeModeReason is NONE when not in safe mode.
    """
    # Ensure we're in NORMAL mode
    mode = get_current_mode(fprime_test_api)

    if mode != MODE_NORMAL:
        # Not in NORMAL mode, skip test
        pytest.skip("Not in NORMAL mode - cannot verify initial reason")

    # Verify safe mode reason is NONE
    reason = get_safe_mode_reason(fprime_test_api)

    assert SAFE_MODE_REASON_NONE in str(reason).upper(), (
        f"Safe mode reason should be NONE, got {reason}"
    )


@pytest.mark.verifies("MM0002", "MS-L2-06")
def test_safe_02_ground_command_sets_reason(
    fprime_test_api: IntegrationTestAPI, start_gds
):
    """
    Test that FORCE_SAFE_MODE command sets reason to GROUND_COMMAND.
    Verifies:
    - CurrentSafeModeReason = GROUND_COMMAND after command
    - EnteringSafeMode event contains "Ground command"

    Precondition: Must start in NORMAL mode
    """
    # Verify precondition (defense in depth)
    mode = get_current_mode(fprime_test_api)
    if mode != MODE_NORMAL:
        pytest.fail(
            f"Test precondition failed: Must start in NORMAL mode, "
            f"currently in mode {mode}"
        )

    # Enter safe mode via ground command
    fprime_test_api.clear_histories()
    proves_send_and_assert_command(
        fprime_test_api,
        f"{component}.FORCE_SAFE_MODE",
        events=[f"{component}.ManualSafeModeEntry"],
    )
    time.sleep(2)

    # Verify EnteringSafeMode event mentions ground command
    # (Must check BEFORE calling proves_send_and_assert_command again, as it clears histories)
    events = fprime_test_api.get_event_test_history()
    entering_events = [
        e for e in events if "EnteringSafeMode" in str(e.get_template().get_name())
    ]
    assert len(entering_events) > 0, "Should have EnteringSafeMode event"
    assert "Ground command" in entering_events[-1].get_display_text(), (
        "EnteringSafeMode reason should mention Ground command"
    )

    # Verify safe mode reason is GROUND_COMMAND
    reason = get_safe_mode_reason(fprime_test_api)

    assert SAFE_MODE_REASON_GROUND_COMMAND in str(reason).upper(), (
        f"Safe mode reason should be GROUND_COMMAND, got {reason}"
    )


@pytest.mark.verifies("MM0001", "MS-L2-02", "CDH-10")
def test_safe_03_exit_clears_reason(fprime_test_api: IntegrationTestAPI, start_gds):
    """
    Test that EXIT_SAFE_MODE clears the safe mode reason to NONE.

    Verifies MM0001 / MS-L2-02 / CDH-10: FORCE_SAFE_MODE from NORMAL is read
    back as SAFE_MODE and EXIT_SAFE_MODE is read back as NORMAL, with the
    safe-mode reason reading GROUND_COMMAND then NONE.
    """
    # Enter safe mode
    proves_send_and_assert_command(fprime_test_api, f"{component}.FORCE_SAFE_MODE")
    time.sleep(2)

    # Verify in safe mode with reason set
    mode = get_current_mode(fprime_test_api)
    assert mode == MODE_SAFE_MODE, "Should be in SAFE_MODE"

    # Exit safe mode
    proves_send_and_assert_command(
        fprime_test_api,
        f"{component}.EXIT_SAFE_MODE",
        events=[f"{component}.ExitingSafeMode"],
    )
    time.sleep(2)

    # The criterion (MM0001 / MS-L2-02 / CDH-10) requires the NORMAL mode
    # readback itself, not just the exit event and the cleared reason.
    assert get_current_mode(fprime_test_api) == MODE_NORMAL, (
        "Should be back in NORMAL mode after EXIT_SAFE_MODE"
    )

    # Verify reason is cleared to NONE
    reason = get_safe_mode_reason(fprime_test_api)

    assert SAFE_MODE_REASON_NONE in str(reason).upper(), (
        f"Safe mode reason should be NONE after exit, got {reason}"
    )


@pytest.mark.verifies("MM0004")
def test_safe_04_no_auto_recovery_for_ground_command(
    fprime_test_api: IntegrationTestAPI, start_gds
):
    """
    Test that safe mode entered via GROUND_COMMAND does NOT auto-recover.
    Even if voltage is healthy, must use EXIT_SAFE_MODE command.
    Verifies:
    - Safe mode persists after debounce period + margin
    - No AutoSafeModeExit event is emitted
    """
    # Enter safe mode via ground command
    proves_send_and_assert_command(fprime_test_api, f"{component}.FORCE_SAFE_MODE")
    time.sleep(2)

    # Verify in safe mode with GROUND_COMMAND reason
    mode = get_current_mode(fprime_test_api)
    assert mode == MODE_SAFE_MODE, "Should be in SAFE_MODE"

    fprime_test_api.clear_histories()

    # Wait for debounce period + margin (should NOT auto-recover)
    logger.info(
        f"Waiting {SAFE_MODE_DEBOUNCE_SECONDS + 3} seconds to verify no auto-recovery..."
    )
    time.sleep(SAFE_MODE_DEBOUNCE_SECONDS + 3)

    # Verify still in safe mode
    mode = get_current_mode(fprime_test_api)
    assert mode == MODE_SAFE_MODE, (
        "Should still be in SAFE_MODE (no auto-recovery for GROUND_COMMAND reason)"
    )

    # Verify no AutoSafeModeExit event
    events = fprime_test_api.get_event_test_history()
    auto_exit_events = [
        e for e in events if "AutoSafeModeExit" in str(e.get_template().get_name())
    ]
    assert len(auto_exit_events) == 0, (
        "Should NOT have AutoSafeModeExit event for GROUND_COMMAND reason"
    )


# ==============================================================================
# Manual Tests - Require Voltage Control
# ==============================================================================


@pytest.mark.skip(reason="Requires voltage control - run manually with low battery")
def test_safe_05_auto_entry_low_voltage(fprime_test_api: IntegrationTestAPI, start_gds):
    """
    Test automatic entry into safe mode due to low voltage.

    MANUAL TEST - requires ability to control battery voltage:
    1. Ensure in NORMAL mode
    2. Lower battery voltage below SAFE_MODE_ENTRY_VOLTAGE (6.7V) for SAFE_MODE_DEBOUNCE_SECONDS
    3. Verify AutoSafeModeEntry event is emitted with reason=LOW_BATTERY
    4. Verify mode changes to SAFE_MODE
    5. Verify CurrentSafeModeReason = LOW_BATTERY

    Expected behavior:
    - Voltage must be below 6.7V threshold for 10 consecutive seconds (1Hz checks)
    - Invalid voltage readings also count as faults
    - AutoSafeModeEntry event emitted with reason=LOW_BATTERY and voltage value
    - Mode changes to SAFE_MODE
    - All 8 load switches turned OFF
    """
    # Ensure in NORMAL mode
    mode = get_current_mode(fprime_test_api)
    assert mode == MODE_NORMAL, "Should start in NORMAL mode"

    fprime_test_api.clear_histories()

    # --- MANUAL STEP: Lower voltage below 6.7V for 10+ seconds ---
    logger.info(
        f"MANUAL: Lower voltage below {SAFE_MODE_ENTRY_VOLTAGE}V for {SAFE_MODE_DEBOUNCE_SECONDS}+ seconds"
    )
    time.sleep(SAFE_MODE_DEBOUNCE_SECONDS + 5)

    # Verify AutoSafeModeEntry event
    fprime_test_api.assert_event(f"{component}.AutoSafeModeEntry", timeout=5)

    # Verify mode is SAFE_MODE
    mode = get_current_mode(fprime_test_api)
    assert mode == MODE_SAFE_MODE, "Should be in SAFE_MODE after low voltage"

    # Verify reason is LOW_BATTERY
    reason = get_safe_mode_reason(fprime_test_api)
    assert SAFE_MODE_REASON_LOW_BATTERY in str(reason).upper(), (
        f"Safe mode reason should be LOW_BATTERY, got {reason}"
    )


@pytest.mark.skip(
    reason="Requires voltage control - run manually with battery recovery"
)
def test_safe_06_auto_recovery_voltage(fprime_test_api: IntegrationTestAPI, start_gds):
    """
    Test automatic recovery from safe mode when voltage recovers.

    MANUAL TEST - requires ability to control battery voltage:
    1. Enter safe mode with LOW_BATTERY reason (use test_safe_05 first or simulate)
    2. Raise battery voltage above SAFE_MODE_RECOVERY_VOLTAGE (8.0V) for SAFE_MODE_DEBOUNCE_SECONDS
    3. Verify AutoSafeModeExit event is emitted with recovered voltage
    4. Verify mode changes to NORMAL
    5. Verify CurrentSafeModeReason = NONE

    Expected behavior:
    - Only works if reason is LOW_BATTERY (not SYSTEM_FAULT or GROUND_COMMAND)
    - Voltage must be above 8.0V threshold for 10 consecutive seconds
    - AutoSafeModeExit event emitted with voltage value
    - Mode changes to NORMAL
    - Face switches (0-5) turned ON
    """
    # This test assumes we're in safe mode with LOW_BATTERY reason
    # In real testing, run test_safe_05 first or manually set up the state

    # Verify in safe mode with LOW_BATTERY reason
    mode = get_current_mode(fprime_test_api)
    assert mode == MODE_SAFE_MODE, "Should be in SAFE_MODE"

    reason = get_safe_mode_reason(fprime_test_api)
    # Skip if not LOW_BATTERY reason
    if SAFE_MODE_REASON_LOW_BATTERY not in str(reason).upper():
        pytest.skip("Safe mode reason is not LOW_BATTERY - auto-recovery won't work")

    fprime_test_api.clear_histories()

    # --- MANUAL STEP: Raise voltage above 8.0V for 10+ seconds ---
    logger.info(
        f"MANUAL: Raise voltage above {SAFE_MODE_RECOVERY_VOLTAGE}V for {SAFE_MODE_DEBOUNCE_SECONDS}+ seconds"
    )
    time.sleep(SAFE_MODE_DEBOUNCE_SECONDS + 5)

    # Verify AutoSafeModeExit event
    fprime_test_api.assert_event(f"{component}.AutoSafeModeExit", timeout=5)

    # Verify mode is NORMAL
    mode = get_current_mode(fprime_test_api)
    assert mode == MODE_NORMAL, "Should be in NORMAL mode after recovery"

    # Verify reason is cleared to NONE
    reason = get_safe_mode_reason(fprime_test_api)
    assert SAFE_MODE_REASON_NONE in str(reason).upper(), (
        f"Safe mode reason should be NONE after recovery, got {reason}"
    )


@pytest.mark.skip(reason="Requires reboot - run manually with power cycle")
def test_safe_07_unintended_reboot_detection(
    fprime_test_api: IntegrationTestAPI, start_gds
):
    """
    Test unintended reboot detection and automatic safe mode entry.

    MANUAL TEST - requires power cycle without clean shutdown:
    1. Ensure in NORMAL mode
    2. Power cycle the board WITHOUT using COLD_RESET or WARM_RESET commands
    3. On boot, verify UnintendedRebootDetected event is emitted
    4. Verify mode is SAFE_MODE
    5. Verify CurrentSafeModeReason = SYSTEM_FAULT
    6. Verify auto-recovery does NOT work (must use EXIT_SAFE_MODE)

    Expected behavior:
    - cleanShutdown flag is 0 (not set by prepareForReboot)
    - On boot, ModeManager detects unintended reboot
    - Enters SAFE_MODE with reason = SYSTEM_FAULT
    - Requires manual EXIT_SAFE_MODE to recover (no auto-recovery)
    """
    # This test requires a power cycle, so we can only verify the aftermath

    # Check for UnintendedRebootDetected event (may have been emitted at boot)
    events = fprime_test_api.get_event_test_history()
    unintended_events = [
        e
        for e in events
        if "UnintendedRebootDetected" in str(e.get_template().get_name())
    ]

    if len(unintended_events) == 0:
        pytest.skip("No UnintendedRebootDetected event - board may have booted cleanly")

    # Verify in safe mode
    mode = get_current_mode(fprime_test_api)
    assert mode == MODE_SAFE_MODE, "Should be in SAFE_MODE after unintended reboot"

    # Verify reason is SYSTEM_FAULT
    reason = get_safe_mode_reason(fprime_test_api)
    assert SAFE_MODE_REASON_SYSTEM_FAULT in str(reason).upper(), (
        f"Safe mode reason should be SYSTEM_FAULT, got {reason}"
    )


@pytest.mark.skip(reason="Requires reboot - run manually to verify clean shutdown")
def test_safe_08_clean_reboot_no_safe_mode(
    fprime_test_api: IntegrationTestAPI, start_gds
):
    """
    Test that intentional reboot (via COLD_RESET/WARM_RESET) does NOT trigger safe mode.

    MANUAL TEST - requires reboot via command:
    1. Ensure in NORMAL mode
    2. Issue COLD_RESET or WARM_RESET command
    3. After reboot, verify NO UnintendedRebootDetected event
    4. Verify mode restored to previous state (NORMAL)
    5. Verify CurrentSafeModeReason = NONE

    Expected behavior:
    - prepareForReboot is called before sys_reboot()
    - cleanShutdown flag is set to 1
    - On boot, ModeManager sees clean shutdown, no safe mode entry
    - Mode restored from persistent state
    """
    # Ensure in NORMAL mode before reboot
    mode = get_current_mode(fprime_test_api)
    assert mode == MODE_NORMAL, "Should be in NORMAL mode before reboot"

    # Issue reboot command (this will disconnect GDS)
    logger.info("Issuing COLD_RESET command - GDS will disconnect")
    fprime_test_api.send_command("ReferenceDeployment.resetManager.COLD_RESET", [])

    # --- MANUAL STEP: Reconnect GDS after reboot ---
    logger.info("MANUAL: Reconnect GDS after board reboots")
    time.sleep(30)  # Wait for reboot

    # After reconnect, verify no unintended reboot detection
    fprime_test_api.clear_histories()
    time.sleep(5)  # Allow time for events to be received

    events = fprime_test_api.get_event_test_history()
    unintended_events = [
        e
        for e in events
        if "UnintendedRebootDetected" in str(e.get_template().get_name())
    ]
    assert len(unintended_events) == 0, (
        "Should NOT have UnintendedRebootDetected event after clean reboot"
    )

    # Verify in NORMAL mode
    mode = get_current_mode(fprime_test_api)
    assert mode == MODE_NORMAL, "Should be in NORMAL mode after clean reboot"

    # Verify reason is NONE
    reason = get_safe_mode_reason(fprime_test_api)
    assert SAFE_MODE_REASON_NONE in str(reason).upper(), (
        f"Safe mode reason should be NONE after clean reboot, got {reason}"
    )


# ==============================================================================
# Automated tests added for MS-L2-03 / MS-L2-05 / MS-L2-07
#
# Level: Board. Each observable below — a mode readback after a real reboot,
# a load-switch state read back over GDS, a detumble-mode channel on the
# downlink — exists only on hardware.
#
# Window constants (TP-4):
#   REBOOT_EVENT_S = 15
#       Time allowed for the FrameworkVersion event announcing that F Prime has
#       restarted. Same bound as reset_manager_test.py:27.
#   MODE_READBACK_S = 5
#       "within 5 s" from the MS-L2-05 / MM0007 pass criteria.
#   NO_EVENT_WINDOW_S = 5
#       Bounded negative window for "no UnintendedRebootDetected". The event is
#       raised inside loadState() during init, which completes before the first
#       1 Hz tick; 5 s covers boot plus several downlink framer cycles (the
#       radio job's framer runs at 2 s, conftest.py:_enable_radio).
#   DETUMBLE_TLM_S = 45
#       One TlmPacketizer period (30 s) plus 15 s margin, the "within 45 s" the
#       MS-L2-03 criterion names. detumbleManager.Mode is in packet group 3, so
#       tlmSend level 5 is commanded first and restored afterwards.
# ==============================================================================

REBOOT_EVENT_S = 15
MODE_READBACK_S = 5
NO_EVENT_WINDOW_S = 5
DETUMBLE_TLM_S = 45

DETUMBLE_PACKET_LEVEL = 5
DEFAULT_PACKET_LEVEL = 1

resetManager = "ReferenceDeployment.resetManager"
tlmSend = "CdhCore.tlmSend"
detumbleManager = "ReferenceDeployment.detumbleManager"

# Every load switch modeManager is wired to turn off on safe-mode entry
# (topology.fpp loadSwitchTurnOff connections). face4 is not fitted.
LOAD_SWITCHES = [
    "ReferenceDeployment.face0LoadSwitch",
    "ReferenceDeployment.face1LoadSwitch",
    "ReferenceDeployment.face2LoadSwitch",
    "ReferenceDeployment.face3LoadSwitch",
    "ReferenceDeployment.face5LoadSwitch",
    "ReferenceDeployment.payloadPowerLoadSwitch",
    "ReferenceDeployment.payloadBatteryLoadSwitch",
]

PAYLOAD_SWITCHES = [
    "ReferenceDeployment.payloadPowerLoadSwitch",
    "ReferenceDeployment.payloadBatteryLoadSwitch",
]

SWITCH_OFF = "OFF"


def _get_is_on(fprime_test_api: IntegrationTestAPI, switch: str) -> str:
    """Read one load switch's state via its GET_IS_ON command and IsOn event."""
    fprime_test_api.clear_histories()
    proves_send_and_assert_command(fprime_test_api, f"{switch}.GET_IS_ON")
    result: EventData = fprime_test_api.assert_event(
        f"{switch}.IsOn", timeout=MODE_READBACK_S
    )
    return str(result.args[0].val)


@pytest.mark.uart_only
@pytest.mark.verifies("MS-L2-07", "MM0007")
def test_safe_09_mode_persists_across_warm_reset(
    fprime_test_api: IntegrationTestAPI, start_gds
):
    """Safe mode and its entry reason survive a commanded warm reset.

    MS-L2-07 / MM0007: after FORCE_SAFE_MODE then WARM_RESET,
    GET_CURRENT_MODE reads SAFE_MODE with reason GROUND_COMMAND, and no
    UnintendedRebootDetected event is raised — a *commanded* reset is a clean
    shutdown and must not be misclassified as a crash.

    Marked ``uart_only`` (TP-5) because rebooting severs the RF link.
    """
    proves_send_and_assert_command(fprime_test_api, f"{component}.FORCE_SAFE_MODE")
    time.sleep(2)
    assert get_current_mode(fprime_test_api) == MODE_SAFE_MODE, (
        "Precondition failed: should be in SAFE_MODE before the reset"
    )

    start: TimeType = TimeType().set_datetime(
        datetime.now(), time_base=TimeType.TimeBase("TB_DONT_CARE")
    )
    fprime_test_api.clear_histories()
    fprime_test_api.send_command(f"{resetManager}.WARM_RESET")

    # F Prime announces a restart with FrameworkVersion (reset_manager_test.py:27).
    fprime_test_api.assert_event(
        "CdhCore.version.FrameworkVersion", start=start, timeout=REBOOT_EVENT_S
    )

    # MS-L2-07: the persisted state was restored.
    assert get_current_mode(fprime_test_api) == MODE_SAFE_MODE, (
        "Safe mode must survive a warm reset"
    )
    reason = get_safe_mode_reason(fprime_test_api)
    assert SAFE_MODE_REASON_GROUND_COMMAND in str(reason).upper(), (
        f"Entry reason must survive the reset as GROUND_COMMAND, got {reason}"
    )

    # MM0007: a commanded reset is clean, so no crash is reported.
    fprime_test_api.clear_histories()
    assert (
        fprime_test_api.await_event(
            f"{component}.UnintendedRebootDetected", timeout=NO_EVENT_WINDOW_S
        )
        is None
    ), "A commanded WARM_RESET must not be reported as an unintended reboot"

    # Teardown to NORMAL is handled by the module's setup_and_teardown fixture.


@pytest.mark.verifies("MS-L2-05", "MM0005")
def test_safe_10_safe_mode_turns_off_load_switches(
    fprime_test_api: IntegrationTestAPI, start_gds
):
    """Entering safe mode powers down every load switch.

    MS-L2-05 / MM0005 clause claimed here: the *Board* clause — within 5 s of
    FORCE_SAFE_MODE every load switch reads OFF via GET_IS_ON, and the payload
    switches remain OFF after EXIT_SAFE_MODE. (The Unit clause, that
    enterSafeMode signals all 8 connected ports exactly once, is covered by
    test_ModeManager_VoltageDebounce.cpp.)

    Design note: modeManager's loadSwitchTurnOn ports are not wired in the
    topology, so no switch is expected to come back on after EXIT_SAFE_MODE.
    This test therefore asserts only what the criterion states — the payload
    switches stay OFF — and does not assert that the face switches recover.
    """
    # Start from a known-on state so that "OFF" afterwards is a real change and
    # not the state the switch was already in.
    proves_send_and_assert_command(fprime_test_api, f"{LOAD_SWITCHES[0]}.TURN_ON")
    assert _get_is_on(fprime_test_api, LOAD_SWITCHES[0]) != SWITCH_OFF, (
        "Precondition failed: face0 load switch did not turn on"
    )

    proves_send_and_assert_command(fprime_test_api, f"{component}.FORCE_SAFE_MODE")
    time.sleep(2)

    still_on = [
        switch
        for switch in LOAD_SWITCHES
        if _get_is_on(fprime_test_api, switch) != SWITCH_OFF
    ]
    assert not still_on, (
        f"Every load switch must read OFF within {MODE_READBACK_S}s of entering "
        f"safe mode; still on: {still_on}"
    )

    proves_send_and_assert_command(
        fprime_test_api,
        f"{component}.EXIT_SAFE_MODE",
        events=[f"{component}.ExitingSafeMode"],
    )
    time.sleep(2)

    payload_on = [
        switch
        for switch in PAYLOAD_SWITCHES
        if _get_is_on(fprime_test_api, switch) != SWITCH_OFF
    ]
    assert not payload_on, (
        f"Payload switches must stay OFF after leaving safe mode; on: {payload_on}"
    )

    # Restore face0, which the thermal and light-sensor tests expect powered.
    proves_send_and_assert_command(fprime_test_api, f"{LOAD_SWITCHES[0]}.TURN_ON")


@pytest.mark.verifies("MS-L2-03", "MS-L2-06")
def test_safe_11_detumble_disabled_in_safe_mode(
    fprime_test_api: IntegrationTestAPI, start_gds
):
    """The one wired mode consumer reacts to the mode change.

    MS-L2-03: detumbleManager.Mode reads DISABLED within 45 s of
    FORCE_SAFE_MODE, and returns to its pre-test value within 45 s of
    EXIT_SAFE_MODE.
    MS-L2-06 clause claimed here: the *modeChanged broadcast* clause — the mode
    change is not merely logged, it actually reaches the only component wired
    to modeChanged (topology.fpp:368-369). The event clause of MS-L2-06 is
    covered by test_safe_02.

    The baseline is captured from the live channel rather than assumed, so the
    recovery assertion does not hard-code the OPERATING_MODE parameter value.
    """
    proves_send_and_assert_command(
        fprime_test_api, f"{tlmSend}.SET_LEVEL", [str(DETUMBLE_PACKET_LEVEL)]
    )
    try:
        baseline = fprime_test_api.await_telemetry(
            f"{detumbleManager}.Mode", start="NOW", timeout=DETUMBLE_TLM_S
        )
        assert baseline is not None, (
            f"Precondition failed: {detumbleManager}.Mode did not downlink "
            f"within {DETUMBLE_TLM_S}s at packet level {DETUMBLE_PACKET_LEVEL}"
        )
        baseline_mode = str(baseline.get_val())

        proves_send_and_assert_command(fprime_test_api, f"{component}.FORCE_SAFE_MODE")
        disabled = fprime_test_api.await_telemetry_count(
            1,
            f"{detumbleManager}.Mode",
            start="NOW",
            timeout=DETUMBLE_TLM_S,
        )
        assert disabled, (
            "detumbleManager.Mode did not downlink after entering safe mode"
        )
        assert str(disabled[-1].get_val()).upper() == "DISABLED", (
            f"Detumble must be disabled in safe mode; Mode reads "
            f"{disabled[-1].get_val()}"
        )

        proves_send_and_assert_command(
            fprime_test_api,
            f"{component}.EXIT_SAFE_MODE",
            events=[f"{component}.ExitingSafeMode"],
        )
        restored = fprime_test_api.await_telemetry_count(
            1,
            f"{detumbleManager}.Mode",
            start="NOW",
            timeout=DETUMBLE_TLM_S,
        )
        assert restored, "detumbleManager.Mode did not downlink after leaving safe mode"
        assert str(restored[-1].get_val()) == baseline_mode, (
            f"Detumble mode must return to its pre-test value {baseline_mode!r} "
            f"after leaving safe mode; reads {restored[-1].get_val()!r}"
        )
    finally:
        proves_send_and_assert_command(
            fprime_test_api, f"{tlmSend}.SET_LEVEL", [str(DEFAULT_PACKET_LEVEL)]
        )
