// ======================================================================
// \title  ModeManager.cpp
// \author Auto-generated
// \brief  cpp file for ModeManager component implementation class
// ======================================================================

#include "PROVESFlightControllerReference/Components/ModeManager/ModeManager.hpp"

#include <algorithm>

#include "Fw/Time/Time.hpp"
#include "Fw/Types/Assert.hpp"
#include "PROVESFlightControllerReference/Components/PersistedRecord/PersistedRecordFile.hpp"

namespace Components {

namespace {
// Record-type magic: "MMS1" (Mode Manager State, PersistedRecord format
// version 1). A file left by an older image carries no magic at all, so it
// fails validation here rather than being misread as a valid state.
constexpr U8 STATE_MAGIC[4] = {'M', 'M', 'S', '1'};

// Highest defined SafeModeReason ordinal (ModeManager.fpp:10-17). A CRC-valid
// record carrying a larger value is a validation failure, not a state.
constexpr U8 MAX_SAFE_MODE_REASON = 6;

//! Read a little-endian U32 out of the persisted payload.
U32 decodeU32LE(const U8* in) {
    return static_cast<U32>(in[0]) | (static_cast<U32>(in[1]) << 8) | (static_cast<U32>(in[2]) << 16) |
           (static_cast<U32>(in[3]) << 24);
}
}  // namespace

// ----------------------------------------------------------------------
// Component construction and destruction
// ----------------------------------------------------------------------

ModeManager ::ModeManager(const char* const compName)
    : ModeManagerComponentBase(compName),
      m_mode(SystemMode::NORMAL),
      m_safeModeEntryCount(0),
      m_runCounter(0),
      m_safeModeReason(Components::SafeModeReason::NONE),
      m_safeModeVoltageCounter(0),
      m_recoveryVoltageCounter(0),
      m_commandLossCounter(0),
      m_commandLossDebounce(false) {
    // Compile-time verification that internal SystemMode enum matches FPP-generated enum
    static_assert(static_cast<U8>(SystemMode::SAFE_MODE) == static_cast<U8>(Components::SystemMode::SAFE_MODE),
                  "Internal SAFE_MODE value must match FPP enum");
    static_assert(static_cast<U8>(SystemMode::NORMAL) == static_cast<U8>(Components::SystemMode::NORMAL),
                  "Internal NORMAL value must match FPP enum");
}

ModeManager ::~ModeManager() {}

void ModeManager ::restorePersistentState() {
    this->loadState();
}

// ----------------------------------------------------------------------
// Handler implementations for user-defined typed input ports
// ----------------------------------------------------------------------

void ModeManager ::run_handler(FwIndexType portNum, U32 context) {
    // Increment run counter (1Hz tick counter)
    this->m_runCounter++;
    {
        Os::ScopeLock lock(m_commandLossMutex);
        this->m_commandLossCounter++;  // keep track of seconds since last packet
    }

    // Get current voltage (used by mode-specific voltage monitoring)
    bool valid = false;
    F32 voltage = this->getCurrentVoltage(valid);

    // Get configurable parameters
    Fw::ParamValid paramValid;
    F32 entryVoltage = this->paramGet_SafeModeEntryVoltage(paramValid);
    F32 recoveryVoltage = this->paramGet_SafeModeRecoveryVoltage(paramValid);
    U32 debounceSeconds = this->paramGet_SafeModeDebounceSeconds(paramValid);

    // Mode-specific voltage monitoring
    if (this->m_mode == SystemMode::NORMAL) {
        // Low-voltage protection for normal mode -> safe mode entry:
        // - Threshold: configurable via SafeModeEntryVoltage parameter (default 6.7V)
        // - Debounce: configurable via SafeModeDebounceSeconds parameter (default 10s)
        bool isFault = !valid || (voltage < entryVoltage);

        if (isFault) {
            // Observation only while the FaultManager is in shadow mode: a
            // CLAIMED disposition means it owns the entry for this condition.
            const bool claimed = this->reportLowBattery(valid ? voltage : 0.0f);

            if (!claimed) {
                this->m_safeModeVoltageCounter++;

                if (this->m_safeModeVoltageCounter >= debounceSeconds) {
                    // Trigger automatic entry into safe mode
                    this->runSafeModeSequence();
                    this->log_WARNING_HI_AutoSafeModeEntry(Components::SafeModeReason::LOW_BATTERY,
                                                           valid ? voltage : 0.0f);
                    this->enterSafeMode(Components::SafeModeReason::LOW_BATTERY);
                    this->m_safeModeVoltageCounter = 0;  // Reset counter
                }
            }
        } else {
            // Voltage OK and valid - reset counter
            this->m_safeModeVoltageCounter = 0;
        }

        // Reset recovery counter when in normal mode
        this->m_recoveryVoltageCounter = 0;

    } else if (this->m_mode == SystemMode::SAFE_MODE) {
        // Auto-recovery from safe mode (only if reason is LOW_BATTERY):
        // - Threshold: configurable via SafeModeRecoveryVoltage parameter (default 8.0V)
        // - Debounce: configurable via SafeModeDebounceSeconds parameter (default 10s)
        // - SYSTEM_FAULT or GROUND_COMMAND require manual EXIT_SAFE_MODE command
        if (this->m_safeModeReason == Components::SafeModeReason::LOW_BATTERY) {
            if (valid && voltage > recoveryVoltage) {
                this->m_recoveryVoltageCounter++;

                if (this->m_recoveryVoltageCounter >= debounceSeconds) {
                    // Trigger automatic exit from safe mode
                    this->exitSafeModeAutomatic(voltage);
                    this->m_recoveryVoltageCounter = 0;  // Reset counter
                }
            } else {
                // Voltage not recovered yet - reset counter
                this->m_recoveryVoltageCounter = 0;
            }
        }
        // Note: If reason is SYSTEM_FAULT, GROUND_COMMAND, or EXTERNAL_REQUEST, no auto-recovery

        // Reset safe mode entry counter when in safe mode
        this->m_safeModeVoltageCounter = 0;
    }

    // Check for command loss and trigger safe mode if timeout has expired
    commandLossCheck();

    // Update telemetry
    this->tlmWrite_CurrentMode(static_cast<U8>(this->m_mode));
    this->tlmWrite_CurrentSafeModeReason(this->m_safeModeReason);
    this->tlmWrite_SafeModeEntryCount(this->m_safeModeEntryCount);
}

void ModeManager ::forceSafeMode_handler(FwIndexType portNum, const Components::SafeModeReason& reason) {
    // Force entry into safe mode (called by other components)
    // Only allowed from NORMAL (sequential +1/-1 transitions)
    if (this->m_mode == SystemMode::NORMAL) {
        this->log_WARNING_HI_ExternalFaultDetected();

        // Use provided reason, defaulting to EXTERNAL_REQUEST if NONE is passed
        Components::SafeModeReason effectiveReason = reason;
        if (reason == Components::SafeModeReason::NONE) {
            effectiveReason = Components::SafeModeReason::EXTERNAL_REQUEST;
        }

        this->runSafeModeSequence();

        this->enterSafeMode(effectiveReason);
    } else if (this->m_mode == SystemMode::SAFE_MODE) {
        this->log_WARNING_LO_SafeModeRequestIgnored();
    }
    // Note: Request ignored if already in SAFE_MODE
}

void ModeManager ::runSafeModeSequence() {
    // run the safe mode sequence
    Fw::ParamValid is_valid;
    Fw::ParamString safe_mode_sequence = this->paramGet_SAFEMODE_SEQUENCE_FILE(is_valid);
    FW_ASSERT(is_valid == Fw::ParamValid::VALID || is_valid == Fw::ParamValid::DEFAULT);
    const Svc::SeqArgs no_args;
    this->runSequence_out(0, safe_mode_sequence, no_args);
}

void ModeManager ::completeSequence_handler(FwIndexType portNum,
                                            FwOpcodeType opCode,
                                            U32 cmdSeq,
                                            const Fw::CmdResponse& response) {
    (void)portNum;

    if (response == Fw::CmdResponse::OK) {
        // log that sequence completed successfully
        this->log_ACTIVITY_HI_SafeModeSequenceCompleted();
    } else {
        // log that sequence failed
        this->log_WARNING_LO_SafeModeSequenceFailed(response);
    }

    // Forward completion to other listeners (e.g. StartupManager active-sequence tracking)
    if (this->isConnected_sequenceDoneNotify_OutputPort(0)) {
        this->sequenceDoneNotify_out(0, opCode, cmdSeq, response);
    }
}

Components::SystemMode ModeManager ::getMode_handler(FwIndexType portNum) {
    // Return the current system mode
    // Convert internal C++ enum to FPP-generated enum type
    return static_cast<Components::SystemMode::T>(this->m_mode);
}

void ModeManager ::prepareForReboot_handler(FwIndexType portNum) {
    // Called before intentional reboot to set clean shutdown flag
    // This allows us to detect unintended reboots on next startup
    this->log_ACTIVITY_HI_PreparingForReboot();

    // Persist the state with the clean-shutdown flag set. On failure the
    // previous record survives, so the next boot reads a clear flag and
    // classifies this shutdown as unintended - conservative, and reported.
    (void)this->storeState(1, "shutdown-store");
}

void ModeManager ::packetRouted_handler(FwIndexType portNum) {
    Os::ScopeLock lock(m_commandLossMutex);
    this->m_commandLossCounter = 0;
    this->m_commandLossDebounce = false;
}

// ----------------------------------------------------------------------
// Handler implementations for commands
// ----------------------------------------------------------------------

void ModeManager ::FORCE_SAFE_MODE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    // Force entry into safe mode

    // Already in safe mode - idempotent success
    if (this->m_mode == SystemMode::SAFE_MODE) {
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
        return;
    }

    this->runSafeModeSequence();

    // Enter safe mode from NORMAL
    this->log_ACTIVITY_HI_ManualSafeModeEntry();
    this->enterSafeMode(Components::SafeModeReason::GROUND_COMMAND);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void ModeManager ::EXIT_SAFE_MODE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    // Manual command to exit safe mode
    this->exitSafeMode();
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void ModeManager ::GET_CURRENT_MODE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    Components::SystemMode fppMode = static_cast<Components::SystemMode::T>(this->m_mode);
    this->log_ACTIVITY_LO_CurrentModeReading(fppMode);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void ModeManager ::GET_SAFE_MODE_REASON_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->log_ACTIVITY_LO_CurrentSafeModeReasonReading(this->m_safeModeReason);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

// ----------------------------------------------------------------------
// Private helper methods
// ----------------------------------------------------------------------

void ModeManager ::loadState() {
    U8 payload[STATE_PAYLOAD_SIZE] = {0};
    U16 payloadLen = 0;

    const PersistedRecord::Status status =
        PersistedRecord::load(STATE_FILE_PATH, STATE_TEMP_PATH, STATE_MAGIC, payload, STATE_PAYLOAD_SIZE, payloadLen);

    // First-boot defaults. Every path below either keeps them or overwrites
    // them with a fully validated record.
    this->m_mode = SystemMode::NORMAL;
    this->m_safeModeEntryCount = 0;
    this->m_safeModeReason = Components::SafeModeReason::NONE;

    bool unintendedReboot = false;
    bool corrupt = false;

    if (status == PersistedRecord::Status::MISSING) {
        // No state file: first boot (or a fresh filesystem). MM0012 requires
        // NORMAL with no event. PersistedRecord separates absent from
        // unopenable with a stat, so this stays silent on Zephyr too, where
        // every open failure collapses to OTHER_ERROR.
        this->turnOnComponents();
    } else if (status == PersistedRecord::Status::OPEN_ERROR || status == PersistedRecord::Status::READ_ERROR ||
               status == PersistedRecord::Status::INVALID_ARGUMENT) {
        // The file is present but could not be read. That is a storage fault,
        // not a failed validation, so MM0012's SAFE rule does not apply and
        // today's behaviour (NORMAL defaults plus one warning) is kept.
        Fw::LogStringArg opStr(status == PersistedRecord::Status::OPEN_ERROR ? "load-open" : "load-read");
        this->log_WARNING_LO_StatePersistenceFailure(opStr, static_cast<I32>(status));
        this->turnOnComponents();
    } else if (status != PersistedRecord::Status::OK || payloadLen != STATE_PAYLOAD_SIZE) {
        // Truncated, wrong magic, bad length, bad CRC or an unrecognized
        // format version: the state cannot be trusted (MM0012).
        corrupt = true;
    } else {
        const U8 mode = payload[0];
        const U32 entryCount = decodeU32LE(&payload[1]);
        const U8 reason = payload[5];
        const U8 cleanShutdown = payload[6];

        // A CRC-valid record can still carry out-of-range fields (e.g. written
        // by a future image); MM0012 calls that a validation failure too.
        if (mode < static_cast<U8>(SystemMode::SAFE_MODE) || mode > static_cast<U8>(SystemMode::NORMAL) ||
            reason > MAX_SAFE_MODE_REASON || cleanShutdown > 1) {
            corrupt = true;
        } else {
            this->m_mode = static_cast<SystemMode>(mode);
            this->m_safeModeEntryCount = entryCount;
            this->m_safeModeReason = static_cast<Components::SafeModeReason::T>(reason);

            // Check for unintended reboot:
            // If cleanShutdown flag is NOT set (0) and we were in NORMAL mode,
            // this indicates an unintended reboot (crash, watchdog, power loss, etc.)
            if (cleanShutdown == 0 && this->m_mode == SystemMode::NORMAL) {
                unintendedReboot = true;
            }

            // Restore physical hardware state to match loaded mode
            if (this->m_mode == SystemMode::SAFE_MODE) {
                // Turn off non-critical components to match safe mode state
                this->turnOffNonCriticalComponents();

                // TODO: commented out because this crashes the board on boot
                // run radio safe to match default safe params
                // this->runSafeModeSequence();

                // Log that we're restoring safe mode (not entering it fresh)
                Fw::LogStringArg reasonStr("State restored from persistent storage");
                this->log_WARNING_HI_EnteringSafeMode(reasonStr);
            } else {
                // NORMAL mode - ensure components are turned on
                this->turnOnComponents();
            }
        }
    }

    // A state file that fails validation boots into safe mode with reason
    // SYSTEM_FAULT and exactly one StatePersistenceFailure (MM0012). The event
    // is emitted first so the ground sees the cause before the consequence;
    // enterSafeMode turns the load switches off, reports the entry and
    // rewrites the record in the current format.
    if (corrupt) {
        Fw::LogStringArg opStr("load-corrupt");
        this->log_WARNING_LO_StatePersistenceFailure(opStr, static_cast<I32>(status));
        this->enterSafeMode(Components::SafeModeReason::SYSTEM_FAULT);
    }

    // Handle unintended reboot detection AFTER basic state restoration
    // This ensures we enter safe mode due to system fault
    if (unintendedReboot) {
        // On unintended reboot, enter safe mode and run the safe mode sequence
        // (e.g., to reset radio parameters and enforce any transmit delay policy)
        this->log_WARNING_HI_UnintendedRebootDetected();
        this->enterSafeMode(Components::SafeModeReason::SYSTEM_FAULT);

        // TODO: commented out because this crashes the board on boot if ran
        // this->runSafeModeSequence();
    }

    // Clear clean shutdown flag for next boot detection
    // This ensures that if the system crashes before the next intentional reboot,
    // we'll detect it as an unintended reboot
    this->saveState();
}

void ModeManager ::encodeState(U8 clean, U8* out) const {
    const U32 entryCount = this->m_safeModeEntryCount;
    out[0] = static_cast<U8>(this->m_mode);
    out[1] = static_cast<U8>(entryCount & 0xFFU);
    out[2] = static_cast<U8>((entryCount >> 8) & 0xFFU);
    out[3] = static_cast<U8>((entryCount >> 16) & 0xFFU);
    out[4] = static_cast<U8>((entryCount >> 24) & 0xFFU);
    out[5] = static_cast<U8>(this->m_safeModeReason);
    out[6] = clean;
}

bool ModeManager ::storeState(U8 clean, const char* op) {
    U8 payload[STATE_PAYLOAD_SIZE];
    this->encodeState(clean, payload);

    // Atomic replace: written and flushed to the staging file, then renamed
    // over the target, so a failure at any step leaves the previous record
    // intact and still valid.
    const PersistedRecord::Status status =
        PersistedRecord::store(STATE_FILE_PATH, STATE_TEMP_PATH, STATE_MAGIC, payload, STATE_PAYLOAD_SIZE);
    if (status != PersistedRecord::Status::OK) {
        // Report but allow the component to continue: this runs during safe
        // mode entry, where crashing would be worse than a stale record.
        Fw::LogStringArg opStr(op);
        this->log_WARNING_LO_StatePersistenceFailure(opStr, static_cast<I32>(status));
        return false;
    }
    return true;
}

void ModeManager ::saveState() {
    // Clean-shutdown flag clear: only prepareForReboot stores a 1.
    (void)this->storeState(0, "save-store");
}

void ModeManager ::enterSafeMode(Components::SafeModeReason reason) {
    // Transition to safe mode
    this->m_mode = SystemMode::SAFE_MODE;
    this->m_safeModeEntryCount++;
    this->m_safeModeReason = reason;

    // Build reason string for event log
    Fw::LogStringArg reasonStr;
    switch (reason) {
        case Components::SafeModeReason::LOW_BATTERY:
            reasonStr = "Low battery voltage";
            break;
        case Components::SafeModeReason::SYSTEM_FAULT:
            reasonStr = "System fault (unintended reboot)";
            break;
        case Components::SafeModeReason::GROUND_COMMAND:
            reasonStr = "Ground command";
            break;
        case Components::SafeModeReason::EXTERNAL_REQUEST:
            reasonStr = "External component request";
            break;
        case Components::SafeModeReason::LORA:
            reasonStr = "LoRa communication fault";
            break;
        case Components::SafeModeReason::COMMAND_LOSS:
            reasonStr = "Loss of contact with ground";
            break;
        default:
            reasonStr = "Unknown";
            break;
    }

    this->log_WARNING_HI_EnteringSafeMode(reasonStr);

    // Turn off non-critical components
    this->turnOffNonCriticalComponents();

    // Update telemetry
    this->tlmWrite_CurrentMode(static_cast<U8>(this->m_mode));
    this->tlmWrite_SafeModeEntryCount(this->m_safeModeEntryCount);
    this->tlmWrite_CurrentSafeModeReason(this->m_safeModeReason);

    // Notify other components of mode change with new mode value
    Components::SystemMode fppMode = static_cast<Components::SystemMode::T>(this->m_mode);
    for (FwIndexType i = 0; i < this->getNum_modeChanged_OutputPorts(); i++) {
        if (!this->isConnected_modeChanged_OutputPort(i)) {
            continue;
        }
        this->modeChanged_out(i, fppMode);
    }

    // Save state
    this->saveState();
}

void ModeManager ::exitSafeMode() {
    // Transition back to normal mode (manual command)
    this->m_mode = SystemMode::NORMAL;
    this->m_safeModeReason = Components::SafeModeReason::NONE;  // Clear reason on exit

    this->log_ACTIVITY_HI_ExitingSafeMode();

    // Turn on components (restore normal operation)
    this->turnOnComponents();

    // Update telemetry
    this->tlmWrite_CurrentMode(static_cast<U8>(this->m_mode));
    this->tlmWrite_CurrentSafeModeReason(this->m_safeModeReason);

    // Notify other components of mode change with new mode value
    if (this->isConnected_modeChanged_OutputPort(0)) {
        Components::SystemMode fppMode = static_cast<Components::SystemMode::T>(this->m_mode);
        this->modeChanged_out(0, fppMode);
    }

    // Save state
    this->saveState();
}

void ModeManager ::exitSafeModeAutomatic(F32 voltage) {
    // Automatic exit from safe mode due to voltage recovery
    // Only called when safe mode reason is LOW_BATTERY and voltage > 8.0V
    this->m_mode = SystemMode::NORMAL;
    this->m_safeModeReason = Components::SafeModeReason::NONE;  // Clear reason on exit

    this->log_ACTIVITY_HI_AutoSafeModeExit(voltage);

    // Turn on components (restore normal operation)
    this->turnOnComponents();

    // Update telemetry
    this->tlmWrite_CurrentMode(static_cast<U8>(this->m_mode));
    this->tlmWrite_CurrentSafeModeReason(this->m_safeModeReason);

    // Notify other components of mode change with new mode value
    if (this->isConnected_modeChanged_OutputPort(0)) {
        Components::SystemMode fppMode = static_cast<Components::SystemMode::T>(this->m_mode);
        this->modeChanged_out(0, fppMode);
    }

    // Save state
    this->saveState();
}

void ModeManager ::turnOffNonCriticalComponents() {
    for (FwIndexType i = 0; i < this->getNum_loadSwitchTurnOff_OutputPorts(); i++) {
        if (!this->isConnected_loadSwitchTurnOff_OutputPort(i)) {
            continue;
        }
        this->loadSwitchTurnOff_out(i);
    }
}

void ModeManager ::turnOnComponents() {
    for (FwIndexType i = 0; i < this->getNum_loadSwitchTurnOn_OutputPorts(); i++) {
        if (!this->isConnected_loadSwitchTurnOn_OutputPort(i)) {
            continue;
        }
        this->loadSwitchTurnOn_out(i);
    }
}

F32 ModeManager ::getCurrentVoltage(bool& valid) {
    // Call the voltage get port to get current system voltage
    if (this->isConnected_voltageGet_OutputPort(0)) {
        F64 voltage = this->voltageGet_out(0);
        valid = true;
        return static_cast<F32>(voltage);  // Convert from F64 to F32
    }

    // Port is not connected - voltage reading is INVALID
    // Do NOT return a fake value that could mask a real brown-out condition
    valid = false;
    return 0.0f;
}

bool ModeManager ::reportLowBattery(F32 voltage) {
    if (!this->isConnected_faultOut_OutputPort(0)) {
        return false;
    }
    const Components::FaultDisposition disposition =
        this->faultOut_out(0, Components::FaultType::LOW_BATTERY, Components::FaultSource::MODE_MANAGER,
                           Components::FaultSeverity::CRITICAL, voltage);
    return disposition == Components::FaultDisposition::CLAIMED;
}

void ModeManager::commandLossCheck() {
    // Protect against concurrent access to command loss state
    Os::ScopeLock lock(this->m_commandLossMutex);

    // Get command loss period from parameter
    Fw::ParamValid paramValid;
    Fw::TimeIntervalValue commLossPeriod = this->paramGet_COMM_LOSS_TIME(paramValid);
    FW_ASSERT(paramValid == Fw::ParamValid::VALID || paramValid == Fw::ParamValid::DEFAULT);

    if (this->m_commandLossCounter >= commLossPeriod.get_seconds() && !this->m_commandLossDebounce) {
        // Debounce so we don't repeatedly re-trigger command loss behavior
        this->m_commandLossDebounce = true;

        // Telemeter the command loss duration
        U32 commandLossDuration = this->m_commandLossCounter;
        this->log_WARNING_HI_CommandLossDetected(commandLossDuration);

        // Trigger safe mode entry due to command loss
        this->runSafeModeSequence();
        this->enterSafeMode(Components::SafeModeReason::COMMAND_LOSS);

        // Stop the watchdog to trigger a hardware power cycle as a last resort for recovery
        this->stopWatchdog_out(0);
    }
}

}  // namespace Components
