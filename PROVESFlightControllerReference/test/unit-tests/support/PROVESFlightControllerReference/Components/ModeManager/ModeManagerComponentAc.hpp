// ======================================================================
// \title  ModeManagerComponentAc.hpp (host-test stub)
// \brief  Stand-in for the fpp-generated ModeManager component base.
//
// Declares the exact base-class surface ModeManager.cpp/.hpp use, and records
// every outgoing effect (mode-change notifications, load-switch signals,
// sequence starts, telemetry writes, events, command responses) into public
// members so host tests can assert on them — the same role the autocoded
// TesterBase plays in a full F Prime UT build. Handlers are public virtuals
// here so tests can invoke the component's private overrides through a base
// reference, as real autocode does.
//
// Shapes mirror ModeManager.fpp:
//   enum SystemMode { SAFE_MODE = 1, NORMAL = 2 }            (fpp:4-8)
//   enum SafeModeReason { NONE=0 .. LORA=5 }                 (fpp:10-17)
//   output port modeChanged[1], runSequence, loadSwitchTurnOn[8],
//   loadSwitchTurnOff[8], voltageGet (Drv.VoltageGet -> F64) (fpp:58-70)
//   param SafeModeEntryVoltage F32 6.7, SafeModeRecoveryVoltage F32 8.0,
//   SafeModeDebounceSeconds U32 10, SAFEMODE_SEQUENCE_FILE
//   "/seq/enter_safe.bin"                                    (fpp:194-202)
// ======================================================================

#ifndef UnitTestSupport_ModeManagerComponentAc_HPP
#define UnitTestSupport_ModeManagerComponentAc_HPP

#include <string>
#include <vector>

#include "../../../FpTypesStub.hpp"
#include "../FaultTypes/FaultTypesStub.hpp"
#include "Fw/Time/Time.hpp"
#include "Fw/Types/String.hpp"

namespace Components {

//! Mirrors the generated FPP enum ModeManager.SystemMode.
class SystemMode {
  public:
    enum T { SAFE_MODE = 1, NORMAL = 2 };
    SystemMode() : m_value(NORMAL) {}
    SystemMode(T value) : m_value(value) {}       // NOLINT(runtime/explicit) -- mirrors generated code
    operator T() const { return this->m_value; }  // NOLINT(runtime/explicit) -- enables switch/case
    bool operator==(const SystemMode& other) const { return this->m_value == other.m_value; }
    bool operator==(T value) const { return this->m_value == value; }
    T value() const { return this->m_value; }

  private:
    T m_value;
};

//! Mirrors the generated FPP enum ModeManager.SafeModeReason.
class SafeModeReason {
  public:
    enum T {
        NONE = 0,
        LOW_BATTERY = 1,
        SYSTEM_FAULT = 2,
        GROUND_COMMAND = 3,
        EXTERNAL_REQUEST = 4,
        LORA = 5,
        COMMAND_LOSS = 6
    };
    SafeModeReason() : m_value(NONE) {}
    SafeModeReason(T value) : m_value(value) {}   // NOLINT(runtime/explicit) -- mirrors generated code
    operator T() const { return this->m_value; }  // NOLINT(runtime/explicit) -- enables switch/case
    bool operator==(const SafeModeReason& other) const { return this->m_value == other.m_value; }
    bool operator==(T value) const { return this->m_value == value; }
    T value() const { return this->m_value; }

  private:
    T m_value;
};

class ModeManagerComponentBase {
  public:
    struct CmdResponseRecord {
        FwOpcodeType opCode;
        U32 cmdSeq;
        Fw::CmdResponse response;
    };

    struct StatePersistenceFailureRecord {
        std::string op;
        I32 status;
    };

    struct AutoSafeModeEntryRecord {
        SafeModeReason::T reason;
        F32 voltage;
    };

    //! One recorded fault report sent out faultOut.
    struct FaultReportRecord {
        FaultType::T faultType;
        FaultSource::T source;
        FaultSeverity::T severity;
        F32 value;
    };

    explicit ModeManagerComponentBase(const char* const compName) : compName(compName) {}
    virtual ~ModeManagerComponentBase() {}

    //! Real autocode registers ports and commands here; nothing to do on host.
    void init(FwSizeType queueDepth, FwEnumStoreType instance = 0) {
        (void)queueDepth;
        (void)instance;
    }

    // ---- port counts (ModeManager.fpp:58-70) ----
    static constexpr FwIndexType getNum_modeChanged_OutputPorts() { return 1; }
    static constexpr FwIndexType getNum_loadSwitchTurnOn_OutputPorts() { return 8; }
    static constexpr FwIndexType getNum_loadSwitchTurnOff_OutputPorts() { return 8; }
    static constexpr FwIndexType getNum_voltageGet_OutputPorts() { return 1; }

    // ---- handlers implemented by the component (private overrides there) ----
    virtual void run_handler(FwIndexType portNum, U32 context) = 0;
    virtual void completeSequence_handler(FwIndexType portNum,
                                          FwOpcodeType opCode,
                                          U32 cmdSeq,
                                          const Fw::CmdResponse& response) = 0;
    virtual void forceSafeMode_handler(FwIndexType portNum, const Components::SafeModeReason& reason) = 0;
    virtual Components::SystemMode getMode_handler(FwIndexType portNum) = 0;
    virtual void prepareForReboot_handler(FwIndexType portNum) = 0;
    virtual void packetRouted_handler(FwIndexType portNum) = 0;
    virtual void FORCE_SAFE_MODE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) = 0;
    virtual void EXIT_SAFE_MODE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) = 0;
    virtual void GET_CURRENT_MODE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) = 0;
    virtual void GET_SAFE_MODE_REASON_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) = 0;

    // ---- test-controlled port connectivity (all connected by default, which
    //      is what the topology wires for modeChanged and voltageGet) ----
    bool modeChangedConnected[1] = {true};
    bool loadSwitchTurnOnConnected[8] = {true, true, true, true, true, true, true, true};
    bool loadSwitchTurnOffConnected[8] = {true, true, true, true, true, true, true, true};
    bool voltageGetConnected = true;
    bool sequenceDoneNotifyConnected = false;

    // ---- fault reporting (Cycle D). The default of "not connected" is what
    //      every test written before the port existed sees, so those tests
    //      keep observing exactly the behaviour they always did. ----
    bool faultOutConnected = false;
    Components::FaultDisposition faultOutDisposition = Components::FaultDisposition::OBSERVED;
    std::vector<FaultReportRecord> faultOutCalls;

    // ---- test-controlled inputs ----
    F64 injectedVoltage = 7.5;

    // ---- test-controlled parameter values (defaults from ModeManager.fpp:194-202) ----
    F32 safeModeEntryVoltage = 6.7f;
    F32 safeModeRecoveryVoltage = 8.0f;
    U32 safeModeDebounceSeconds = 10;
    //! COMM_LOSS_TIME default {3*60*60*24, 0} (ModeManager.fpp:229).
    Fw::TimeIntervalValue commLossTime = Fw::TimeIntervalValue(3 * 60 * 60 * 24, 0);
    std::string safeModeSequenceFile = "/seq/enter_safe.bin";
    Fw::ParamValid paramValidity = Fw::ParamValid::VALID;

    // ---- recorded outgoing effects, public for test inspection ----
    std::string compName;
    std::vector<SystemMode::T> modeChangedCalls;
    std::vector<FwIndexType> loadSwitchTurnOnCalls;
    std::vector<FwIndexType> loadSwitchTurnOffCalls;
    std::vector<std::string> runSequenceCalls;
    U32 voltageGetCalls = 0;
    U32 stopWatchdogCalls = 0;
    std::vector<CmdResponseRecord> sequenceDoneNotifyCalls;

    std::vector<U8> tlmCurrentMode;
    std::vector<SafeModeReason::T> tlmCurrentSafeModeReason;
    std::vector<U32> tlmSafeModeEntryCount;

    std::vector<AutoSafeModeEntryRecord> eventsAutoSafeModeEntry;
    std::vector<F32> eventsAutoSafeModeExit;
    std::vector<std::string> eventsEnteringSafeMode;
    U32 eventsExitingSafeMode = 0;
    U32 eventsManualSafeModeEntry = 0;
    U32 eventsExternalFaultDetected = 0;
    U32 eventsSafeModeRequestIgnored = 0;
    U32 eventsUnintendedRebootDetected = 0;
    U32 eventsPreparingForReboot = 0;
    U32 eventsSafeModeSequenceCompleted = 0;
    std::vector<Fw::CmdResponse> eventsSafeModeSequenceFailed;
    std::vector<StatePersistenceFailureRecord> eventsStatePersistenceFailure;
    std::vector<SystemMode::T> eventsCurrentModeReading;
    std::vector<SafeModeReason::T> eventsCurrentSafeModeReasonReading;
    std::vector<U32> eventsCommandLossDetected;
    std::vector<CmdResponseRecord> cmdResponses;

  protected:
    // ---- output ports ----
    bool isConnected_modeChanged_OutputPort(FwIndexType portNum) const { return this->modeChangedConnected[portNum]; }
    void modeChanged_out(FwIndexType portNum, const SystemMode& mode) {
        (void)portNum;
        this->modeChangedCalls.push_back(mode.value());
    }

    bool isConnected_loadSwitchTurnOn_OutputPort(FwIndexType portNum) const {
        return this->loadSwitchTurnOnConnected[portNum];
    }
    void loadSwitchTurnOn_out(FwIndexType portNum) { this->loadSwitchTurnOnCalls.push_back(portNum); }

    bool isConnected_loadSwitchTurnOff_OutputPort(FwIndexType portNum) const {
        return this->loadSwitchTurnOffConnected[portNum];
    }
    void loadSwitchTurnOff_out(FwIndexType portNum) { this->loadSwitchTurnOffCalls.push_back(portNum); }

    bool isConnected_voltageGet_OutputPort(FwIndexType portNum) const {
        (void)portNum;
        return this->voltageGetConnected;
    }
    F64 voltageGet_out(FwIndexType portNum) {
        (void)portNum;
        this->voltageGetCalls++;
        return this->injectedVoltage;
    }

    bool isConnected_faultOut_OutputPort(FwIndexType portNum) const {
        (void)portNum;
        return this->faultOutConnected;
    }
    Components::FaultDisposition faultOut_out(FwIndexType portNum,
                                              const Components::FaultType& faultType,
                                              const Components::FaultSource& source,
                                              const Components::FaultSeverity& severity,
                                              F32 value) {
        (void)portNum;
        this->faultOutCalls.push_back(FaultReportRecord{faultType.e, source.e, severity.e, value});
        return this->faultOutDisposition;
    }

    void runSequence_out(FwIndexType portNum, const Fw::StringBase& filename, const Svc::SeqArgs& args) {
        (void)portNum;
        (void)args;
        this->runSequenceCalls.push_back(filename.toChar());
    }

    bool isConnected_sequenceDoneNotify_OutputPort(FwIndexType portNum) const {
        (void)portNum;
        return this->sequenceDoneNotifyConnected;
    }
    void sequenceDoneNotify_out(FwIndexType portNum, FwOpcodeType opCode, U32 cmdSeq, const Fw::CmdResponse& response) {
        (void)portNum;
        this->sequenceDoneNotifyCalls.push_back(CmdResponseRecord{opCode, cmdSeq, response});
    }

    void stopWatchdog_out(FwIndexType portNum) {
        (void)portNum;
        this->stopWatchdogCalls++;
    }

    // ---- parameters ----
    F32 paramGet_SafeModeEntryVoltage(Fw::ParamValid& valid) {
        valid = this->paramValidity;
        return this->safeModeEntryVoltage;
    }
    F32 paramGet_SafeModeRecoveryVoltage(Fw::ParamValid& valid) {
        valid = this->paramValidity;
        return this->safeModeRecoveryVoltage;
    }
    U32 paramGet_SafeModeDebounceSeconds(Fw::ParamValid& valid) {
        valid = this->paramValidity;
        return this->safeModeDebounceSeconds;
    }
    Fw::ParamString paramGet_SAFEMODE_SEQUENCE_FILE(Fw::ParamValid& valid) {
        valid = this->paramValidity;
        return Fw::ParamString(this->safeModeSequenceFile.c_str());
    }
    Fw::TimeIntervalValue paramGet_COMM_LOSS_TIME(Fw::ParamValid& valid) {
        valid = this->paramValidity;
        return this->commLossTime;
    }

    // ---- telemetry ----
    void tlmWrite_CurrentMode(U8 mode) { this->tlmCurrentMode.push_back(mode); }
    void tlmWrite_CurrentSafeModeReason(const SafeModeReason& reason) {
        this->tlmCurrentSafeModeReason.push_back(reason.value());
    }
    void tlmWrite_SafeModeEntryCount(U32 count) { this->tlmSafeModeEntryCount.push_back(count); }

    // ---- events ----
    void log_WARNING_HI_AutoSafeModeEntry(const SafeModeReason& reason, F32 voltage) {
        this->eventsAutoSafeModeEntry.push_back(AutoSafeModeEntryRecord{reason.value(), voltage});
    }
    void log_ACTIVITY_HI_AutoSafeModeExit(F32 voltage) { this->eventsAutoSafeModeExit.push_back(voltage); }
    void log_WARNING_HI_EnteringSafeMode(const Fw::LogStringArg& reason) {
        this->eventsEnteringSafeMode.push_back(reason.toChar());
    }
    void log_ACTIVITY_HI_ExitingSafeMode() { this->eventsExitingSafeMode++; }
    void log_ACTIVITY_HI_ManualSafeModeEntry() { this->eventsManualSafeModeEntry++; }
    void log_WARNING_HI_ExternalFaultDetected() { this->eventsExternalFaultDetected++; }
    void log_WARNING_LO_SafeModeRequestIgnored() { this->eventsSafeModeRequestIgnored++; }
    void log_WARNING_HI_UnintendedRebootDetected() { this->eventsUnintendedRebootDetected++; }
    void log_ACTIVITY_HI_PreparingForReboot() { this->eventsPreparingForReboot++; }
    void log_ACTIVITY_HI_SafeModeSequenceCompleted() { this->eventsSafeModeSequenceCompleted++; }
    void log_WARNING_HI_CommandLossDetected(U32 duration) { this->eventsCommandLossDetected.push_back(duration); }
    void log_WARNING_LO_SafeModeSequenceFailed(const Fw::CmdResponse& response) {
        this->eventsSafeModeSequenceFailed.push_back(response);
    }
    void log_WARNING_LO_StatePersistenceFailure(const Fw::LogStringArg& op, I32 status) {
        this->eventsStatePersistenceFailure.push_back(StatePersistenceFailureRecord{op.toChar(), status});
    }
    void log_ACTIVITY_LO_CurrentModeReading(const SystemMode& mode) {
        this->eventsCurrentModeReading.push_back(mode.value());
    }
    void log_ACTIVITY_LO_CurrentSafeModeReasonReading(const SafeModeReason& reason) {
        this->eventsCurrentSafeModeReasonReading.push_back(reason.value());
    }

    void cmdResponse_out(FwOpcodeType opCode, U32 cmdSeq, const Fw::CmdResponse& response) {
        this->cmdResponses.push_back(CmdResponseRecord{opCode, cmdSeq, response});
    }
};

}  // namespace Components

#endif
