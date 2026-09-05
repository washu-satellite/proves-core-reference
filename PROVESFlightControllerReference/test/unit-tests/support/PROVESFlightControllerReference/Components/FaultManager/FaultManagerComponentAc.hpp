// ======================================================================
// \title  FaultManagerComponentAc.hpp (host-test stub)
// \brief  Stand-in for the fpp-generated FaultManager component base.
//
// Declares the exact base-class surface FaultManager.cpp/.hpp use, and
// records every outgoing effect (action port calls, telemetry writes, events,
// command responses) into public members so host tests can assert on them —
// the same role the autocoded TesterBase plays in a full F Prime UT build.
//
// Two properties of the real base matter to the tests and are reproduced:
//   * lock()/unLock() bracket the guarded region. Here they only count, but
//     any action port call made while the count is non-zero sets
//     actionWhileLocked, which is how the deadlock rule ("never call an
//     output action port while holding the lock") is asserted on the host.
//   * Every isConnected_* flag defaults to true, matching the topology, so a
//     test has to opt out to exercise the unconnected path.
//
// Shapes mirror FaultManager.fpp: 4 faultIn slots (Components.FaultInPorts),
// params AUTHORITY_ENABLED bool false / AUTHORITY_MASK U8 0 /
// DEBOUNCE_LOW_BATTERY U8 10 / DEBOUNCE_THERMAL U8 1.
// ======================================================================

#ifndef UnitTestSupport_FaultManagerComponentAc_HPP
#define UnitTestSupport_FaultManagerComponentAc_HPP

#include <string>
#include <vector>

#include "../../../FpTypesStub.hpp"
#include "../FaultTypes/FaultTypesStub.hpp"
#include "../ModeManager/ModeManagerComponentAc.hpp"

namespace Components {

class FaultManagerComponentBase {
  public:
    struct CmdResponseRecord {
        FwOpcodeType opCode;
        U32 cmdSeq;
        Fw::CmdResponse response;
    };

    struct FaultConfirmedRecord {
        FaultType::T faultType;
        FaultSource::T source;
        F32 value;
    };

    struct FaultActionRecord {
        FaultType::T faultType;
        FaultAction::T action;
    };

    struct FaultStatusReportRecord {
        U8 active;
        U32 detected;
        U32 confirmed;
        U8 authority;
    };

    struct FaultAuthorityChangedRecord {
        bool enabled;
        U8 mask;
    };

    explicit FaultManagerComponentBase(const char* const compName) : compName(compName) {}
    virtual ~FaultManagerComponentBase() {}

    //! Parameter ids, in FaultManager.fpp declaration order.
    static constexpr FwPrmIdType PARAMID_AUTHORITY_ENABLED = 0;
    static constexpr FwPrmIdType PARAMID_AUTHORITY_MASK = 1;
    static constexpr FwPrmIdType PARAMID_DEBOUNCE_LOW_BATTERY = 2;
    static constexpr FwPrmIdType PARAMID_DEBOUNCE_THERMAL = 3;

    //! Port counts (Components.FaultInPorts = 4).
    static constexpr FwIndexType getNum_faultIn_InputPorts() { return 4; }
    static constexpr FwIndexType getNum_forceSafeMode_OutputPorts() { return 1; }
    static constexpr FwIndexType getNum_stopWatchdog_OutputPorts() { return 1; }

    // ---- handlers implemented by the component (private overrides there) ----
    virtual Components::FaultDisposition faultIn_handler(FwIndexType portNum,
                                                         const Components::FaultType& faultType,
                                                         const Components::FaultSource& source,
                                                         const Components::FaultSeverity& faultSeverity,
                                                         F32 value) = 0;
    virtual void run_handler(FwIndexType portNum, U32 context) = 0;
    virtual void CLEAR_FAULTS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) = 0;
    virtual void GET_FAULT_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) = 0;

    //! Public here so a test can drive the parameter-update path directly; the
    //! component overrides it privately, exactly as against real autocode.
    virtual void parameterUpdated(FwPrmIdType id) = 0;

    //! Invoke faultIn the way the generated guarded port would: take the lock,
    //! call the handler, release the lock.
    Components::FaultDisposition faultIn_guarded(FwIndexType portNum,
                                                 const Components::FaultType& faultType,
                                                 const Components::FaultSource& source,
                                                 const Components::FaultSeverity& faultSeverity,
                                                 F32 value) {
        this->lock();
        const Components::FaultDisposition disposition =
            this->faultIn_handler(portNum, faultType, source, faultSeverity, value);
        this->unLock();
        return disposition;
    }

    // ---- test-controlled port connectivity (topology wires both) ----
    bool forceSafeModeConnected = true;
    bool stopWatchdogConnected = true;

    // ---- test-controlled parameter values (FaultManager.fpp defaults) ----
    bool authorityEnabled = false;
    U8 authorityMask = 0;
    U8 debounceLowBattery = 10;
    U8 debounceThermal = 1;
    Fw::ParamValid paramValidity = Fw::ParamValid::VALID;

    // ---- recorded outgoing effects, public for test inspection ----
    std::string compName;
    std::vector<SafeModeReason::T> forceSafeModeCalls;
    U32 stopWatchdogCalls = 0;
    //! Every action port call in order: "stopWatchdog" or "forceSafeMode".
    std::vector<std::string> actionOrder;
    //! Set if any action port was called while the guarded lock was held.
    bool actionWhileLocked = false;
    //! Set if unLock() was called without a matching lock().
    bool lockUnderflow = false;
    I32 lockDepth = 0;
    U32 lockCalls = 0;

    std::vector<U32> tlmFaultsDetected;
    std::vector<U32> tlmFaultsConfirmed;
    std::vector<U8> tlmActiveFaults;
    std::vector<FaultType::T> tlmLastFaultType;
    std::vector<FaultSource::T> tlmLastFaultSource;
    std::vector<F32> tlmLastFaultValue;
    std::vector<U32> tlmShadowActionsSuppressed;
    std::vector<U32> tlmActionsTaken;
    std::vector<U8> tlmAuthorityState;
    std::vector<U32> tlmFaultCountThermal;
    std::vector<U32> tlmFaultCountLowBattery;
    std::vector<U32> tlmFaultCountCommandLoss;
    std::vector<U32> tlmFaultCountWatchdogStop;

    std::vector<FaultConfirmedRecord> eventsFaultConfirmed;
    std::vector<FaultActionRecord> eventsFaultActionSuppressed;
    std::vector<FaultActionRecord> eventsFaultActionTaken;
    U32 eventsFaultsCleared = 0;
    std::vector<FaultStatusReportRecord> eventsFaultStatusReport;
    std::vector<FaultAuthorityChangedRecord> eventsFaultAuthorityChanged;
    std::vector<CmdResponseRecord> cmdResponses;

  protected:
    // ---- guarded-port mutex stand-in ----
    virtual void lock() {
        this->lockDepth++;
        this->lockCalls++;
    }
    virtual void unLock() {
        if (this->lockDepth == 0) {
            this->lockUnderflow = true;
        } else {
            this->lockDepth--;
        }
    }

    // ---- output ports ----
    bool isConnected_forceSafeMode_OutputPort(FwIndexType portNum) const {
        (void)portNum;
        return this->forceSafeModeConnected;
    }
    void forceSafeMode_out(FwIndexType portNum, const SafeModeReason& reason) {
        (void)portNum;
        if (this->lockDepth > 0) {
            this->actionWhileLocked = true;
        }
        this->forceSafeModeCalls.push_back(reason.value());
        this->actionOrder.push_back("forceSafeMode");
    }

    bool isConnected_stopWatchdog_OutputPort(FwIndexType portNum) const {
        (void)portNum;
        return this->stopWatchdogConnected;
    }
    void stopWatchdog_out(FwIndexType portNum) {
        (void)portNum;
        if (this->lockDepth > 0) {
            this->actionWhileLocked = true;
        }
        this->stopWatchdogCalls++;
        this->actionOrder.push_back("stopWatchdog");
    }

    // ---- parameters ----
    bool paramGet_AUTHORITY_ENABLED(Fw::ParamValid& valid) {
        valid = this->paramValidity;
        return this->authorityEnabled;
    }
    U8 paramGet_AUTHORITY_MASK(Fw::ParamValid& valid) {
        valid = this->paramValidity;
        return this->authorityMask;
    }
    U8 paramGet_DEBOUNCE_LOW_BATTERY(Fw::ParamValid& valid) {
        valid = this->paramValidity;
        return this->debounceLowBattery;
    }
    U8 paramGet_DEBOUNCE_THERMAL(Fw::ParamValid& valid) {
        valid = this->paramValidity;
        return this->debounceThermal;
    }

    // ---- telemetry ----
    void tlmWrite_FaultsDetected(U32 v) { this->tlmFaultsDetected.push_back(v); }
    void tlmWrite_FaultsConfirmed(U32 v) { this->tlmFaultsConfirmed.push_back(v); }
    void tlmWrite_ActiveFaults(U8 v) { this->tlmActiveFaults.push_back(v); }
    void tlmWrite_LastFaultType(const FaultType& v) { this->tlmLastFaultType.push_back(v.e); }
    void tlmWrite_LastFaultSource(const FaultSource& v) { this->tlmLastFaultSource.push_back(v.e); }
    void tlmWrite_LastFaultValue(F32 v) { this->tlmLastFaultValue.push_back(v); }
    void tlmWrite_ShadowActionsSuppressed(U32 v) { this->tlmShadowActionsSuppressed.push_back(v); }
    void tlmWrite_ActionsTaken(U32 v) { this->tlmActionsTaken.push_back(v); }
    void tlmWrite_AuthorityState(U8 v) { this->tlmAuthorityState.push_back(v); }
    void tlmWrite_FaultCountThermal(U32 v) { this->tlmFaultCountThermal.push_back(v); }
    void tlmWrite_FaultCountLowBattery(U32 v) { this->tlmFaultCountLowBattery.push_back(v); }
    void tlmWrite_FaultCountCommandLoss(U32 v) { this->tlmFaultCountCommandLoss.push_back(v); }
    void tlmWrite_FaultCountWatchdogStop(U32 v) { this->tlmFaultCountWatchdogStop.push_back(v); }

    // ---- events ----
    void log_WARNING_HI_FaultConfirmed(const FaultType& faultType, const FaultSource& source, F32 value) {
        this->eventsFaultConfirmed.push_back(FaultConfirmedRecord{faultType.e, source.e, value});
    }
    void log_WARNING_LO_FaultActionSuppressed(const FaultType& faultType, const FaultAction& action) {
        this->eventsFaultActionSuppressed.push_back(FaultActionRecord{faultType.e, action.e});
    }
    void log_WARNING_HI_FaultActionTaken(const FaultType& faultType, const FaultAction& action) {
        this->eventsFaultActionTaken.push_back(FaultActionRecord{faultType.e, action.e});
    }
    void log_ACTIVITY_HI_FaultsCleared() { this->eventsFaultsCleared++; }
    void log_ACTIVITY_LO_FaultStatusReport(U8 active, U32 detected, U32 confirmed, U8 authority) {
        this->eventsFaultStatusReport.push_back(FaultStatusReportRecord{active, detected, confirmed, authority});
    }
    void log_WARNING_HI_FaultAuthorityChanged(bool enabled, U8 mask) {
        this->eventsFaultAuthorityChanged.push_back(FaultAuthorityChangedRecord{enabled, mask});
    }

    void cmdResponse_out(FwOpcodeType opCode, U32 cmdSeq, const Fw::CmdResponse& response) {
        this->cmdResponses.push_back(CmdResponseRecord{opCode, cmdSeq, response});
    }
};

}  // namespace Components

#endif
