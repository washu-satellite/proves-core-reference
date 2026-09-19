// ======================================================================
// \title  FaultManager.cpp
// \brief  cpp file for FaultManager component implementation class
//
// Shadow mode is the shipped configuration: AUTHORITY_ENABLED defaults to
// false and AUTHORITY_MASK to 0, so FaultTable::claims() is false for every
// fault type. That single gate keeps faultIn answering OBSERVED and keeps
// run_handler from reaching forceSafeMode_out or stopWatchdog_out, which is
// what makes this component provably harmless to add to the topology.
// ======================================================================

#include "PROVESFlightControllerReference/Components/FaultManager/FaultManager.hpp"

namespace Components {

namespace {

//! Parameter fallbacks used whenever a parameter reads back INVALID or UNINIT.
//! They are the FPP defaults, i.e. shadow mode with today's timings.
constexpr bool DEFAULT_AUTHORITY_ENABLED = false;
constexpr U8 DEFAULT_AUTHORITY_MASK = 0;
constexpr U8 DEFAULT_DEBOUNCE_LOW_BATTERY = 10;
constexpr U8 DEFAULT_DEBOUNCE_THERMAL = 1;

//! The four thermal threshold fault types, in mask-bit order.
constexpr FaultLogic::Type THERMAL_TYPES[4] = {FaultLogic::FACE_TEMP_HIGH, FaultLogic::FACE_TEMP_LOW,
                                               FaultLogic::BATT_TEMP_HIGH, FaultLogic::BATT_TEMP_LOW};

//! A parameter value may be used only when the database actually holds one or
//! the FPP default applies; INVALID and UNINIT fall back to shadow mode.
bool paramUsable(const Fw::ParamValid& valid) {
    return (valid != Fw::ParamValid::INVALID) && (valid != Fw::ParamValid::UNINIT);
}

Components::FaultType toFppType(FaultLogic::Type type) {
    return Components::FaultType(static_cast<Components::FaultType::T>(type));
}

Components::FaultSource toFppSource(FaultLogic::Source source) {
    return Components::FaultSource(static_cast<Components::FaultSource::T>(source));
}

Components::FaultAction toFppAction(FaultLogic::Action action) {
    return Components::FaultAction(static_cast<Components::FaultAction::T>(action));
}

}  // namespace

// Compile-time alignment of the F Prime free mirror enums in FaultTable.hpp
// with the FPP enums in FaultTypes.fpp. There is no runtime mapping table, so
// these assertions are the only thing keeping the two definitions in step.
static_assert(static_cast<U8>(Components::FaultType::NONE) == static_cast<U8>(FaultLogic::NONE),
              "FaultType::NONE must match FaultLogic::NONE");
static_assert(static_cast<U8>(Components::FaultType::FACE_TEMP_HIGH) == static_cast<U8>(FaultLogic::FACE_TEMP_HIGH),
              "FaultType::FACE_TEMP_HIGH must match FaultLogic::FACE_TEMP_HIGH");
static_assert(static_cast<U8>(Components::FaultType::FACE_TEMP_LOW) == static_cast<U8>(FaultLogic::FACE_TEMP_LOW),
              "FaultType::FACE_TEMP_LOW must match FaultLogic::FACE_TEMP_LOW");
static_assert(static_cast<U8>(Components::FaultType::BATT_TEMP_HIGH) == static_cast<U8>(FaultLogic::BATT_TEMP_HIGH),
              "FaultType::BATT_TEMP_HIGH must match FaultLogic::BATT_TEMP_HIGH");
static_assert(static_cast<U8>(Components::FaultType::BATT_TEMP_LOW) == static_cast<U8>(FaultLogic::BATT_TEMP_LOW),
              "FaultType::BATT_TEMP_LOW must match FaultLogic::BATT_TEMP_LOW");
static_assert(static_cast<U8>(Components::FaultType::LOW_BATTERY) == static_cast<U8>(FaultLogic::LOW_BATTERY),
              "FaultType::LOW_BATTERY must match FaultLogic::LOW_BATTERY");
static_assert(static_cast<U8>(Components::FaultType::COMMAND_LOSS) == static_cast<U8>(FaultLogic::COMMAND_LOSS),
              "FaultType::COMMAND_LOSS must match FaultLogic::COMMAND_LOSS");
static_assert(static_cast<U8>(Components::FaultType::WATCHDOG_STOPPED) == static_cast<U8>(FaultLogic::WATCHDOG_STOPPED),
              "FaultType::WATCHDOG_STOPPED must match FaultLogic::WATCHDOG_STOPPED");
static_assert(static_cast<U8>(Components::FaultType::ADCS_UNSTABLE) == static_cast<U8>(FaultLogic::ADCS_UNSTABLE),
              "FaultType::ADCS_UNSTABLE must match FaultLogic::ADCS_UNSTABLE");

static_assert(static_cast<U8>(Components::FaultSource::THERMAL_MANAGER) == static_cast<U8>(FaultLogic::THERMAL_MANAGER),
              "FaultSource::THERMAL_MANAGER must match FaultLogic::THERMAL_MANAGER");
static_assert(static_cast<U8>(Components::FaultSource::MODE_MANAGER) == static_cast<U8>(FaultLogic::MODE_MANAGER),
              "FaultSource::MODE_MANAGER must match FaultLogic::MODE_MANAGER");
static_assert(static_cast<U8>(Components::FaultSource::AUTH_ROUTER) == static_cast<U8>(FaultLogic::AUTH_ROUTER),
              "FaultSource::AUTH_ROUTER must match FaultLogic::AUTH_ROUTER");
static_assert(static_cast<U8>(Components::FaultSource::WATCHDOG) == static_cast<U8>(FaultLogic::WATCHDOG),
              "FaultSource::WATCHDOG must match FaultLogic::WATCHDOG");
static_assert(static_cast<U8>(Components::FaultSource::DETUMBLE_MANAGER) ==
                  static_cast<U8>(FaultLogic::DETUMBLE_MANAGER),
              "FaultSource::DETUMBLE_MANAGER must match FaultLogic::DETUMBLE_MANAGER");

static_assert(static_cast<U8>(Components::FaultSeverity::WARNING) == static_cast<U8>(FaultLogic::WARNING),
              "FaultSeverity::WARNING must match FaultLogic::WARNING");
static_assert(static_cast<U8>(Components::FaultSeverity::CRITICAL) == static_cast<U8>(FaultLogic::CRITICAL),
              "FaultSeverity::CRITICAL must match FaultLogic::CRITICAL");

static_assert(static_cast<U8>(Components::FaultAction::NONE) == static_cast<U8>(FaultLogic::NO_ACTION),
              "FaultAction::NONE must match FaultLogic::NO_ACTION");
static_assert(static_cast<U8>(Components::FaultAction::SAFE_MODE) == static_cast<U8>(FaultLogic::SAFE_MODE),
              "FaultAction::SAFE_MODE must match FaultLogic::SAFE_MODE");
static_assert(static_cast<U8>(Components::FaultAction::SAFE_MODE_AND_REBOOT) ==
                  static_cast<U8>(FaultLogic::SAFE_MODE_AND_REBOOT),
              "FaultAction::SAFE_MODE_AND_REBOOT must match FaultLogic::SAFE_MODE_AND_REBOOT");

static_assert(static_cast<U8>(Components::FaultDisposition::OBSERVED) == static_cast<U8>(FaultLogic::OBSERVED),
              "FaultDisposition::OBSERVED must match FaultLogic::OBSERVED");
static_assert(static_cast<U8>(Components::FaultDisposition::CLAIMED) == static_cast<U8>(FaultLogic::CLAIMED),
              "FaultDisposition::CLAIMED must match FaultLogic::CLAIMED");

// ----------------------------------------------------------------------
// Component construction and destruction
// ----------------------------------------------------------------------

FaultManager ::FaultManager(const char* const compName)
    : FaultManagerComponentBase(compName),
      m_table(),
      m_tlmCache(),
      m_tick(0),
      m_shadowActionsSuppressed(0),
      m_actionsTaken(0),
      m_lastValue(0.0f),
      m_authorityMask(DEFAULT_AUTHORITY_MASK),
      m_debounceLowBattery(DEFAULT_DEBOUNCE_LOW_BATTERY),
      m_debounceThermal(DEFAULT_DEBOUNCE_THERMAL),
      m_lastType(FaultLogic::NONE),
      m_lastSource(FaultLogic::THERMAL_MANAGER),
      m_authorityEnabled(DEFAULT_AUTHORITY_ENABLED),
      m_tlmPrimed(false) {
    // Shadow mode holds even if parameterUpdated is never called.
}

FaultManager ::~FaultManager() {}

// ----------------------------------------------------------------------
// Handler implementations for user-defined typed input ports
// ----------------------------------------------------------------------

Components::FaultDisposition FaultManager ::faultIn_handler(FwIndexType portNum,
                                                            const Components::FaultType& faultType,
                                                            const Components::FaultSource& source,
                                                            const Components::FaultSeverity& faultSeverity,
                                                            F32 value) {
    // Guarded port: the generated base already holds the component mutex here,
    // so this handler must not lock, and must not call an output action port.
    (void)portNum;
    (void)faultSeverity;

    const FaultLogic::Type type = static_cast<FaultLogic::Type>(faultType.e);

    FaultLogic::Report report;
    report.type = type;
    report.source = static_cast<FaultLogic::Source>(source.e);
    report.value = value;
    static_cast<void>(this->m_table.report(report, this->m_tick));

    // The one gate that keeps every producer on its existing code path.
    if (FaultLogic::FaultTable::claims(type, this->m_authorityEnabled, this->m_authorityMask)) {
        return Components::FaultDisposition::CLAIMED;
    }
    return Components::FaultDisposition::OBSERVED;
}

void FaultManager ::run_handler(FwIndexType portNum, U32 context) {
    (void)portNum;
    (void)context;

    FaultLogic::Decision decisions[FaultLogic::NUM_FAULT_TYPES];
    TlmCache snapshot;

    this->lock();
    this->m_tick++;
    const U8 count = this->m_table.tick(this->m_tick, decisions, FaultLogic::NUM_FAULT_TYPES);
    const bool authorityEnabled = this->m_authorityEnabled;
    const U8 authorityMask = this->m_authorityMask;
    this->unLock();

    // Actions run with the lock released: forceSafeMode and stopWatchdog can
    // lead straight back into the guarded faultIn (watchdog.stop reports
    // WATCHDOG_STOPPED), which would deadlock on a held mutex.
    for (U8 i = 0; i < count; i++) {
        const FaultLogic::Decision& decision = decisions[i];
        this->m_lastType = decision.type;
        this->m_lastSource = decision.source;
        this->m_lastValue = decision.value;

        this->log_WARNING_HI_FaultConfirmed(toFppType(decision.type), toFppSource(decision.source), decision.value);

        if (FaultLogic::FaultTable::claims(decision.type, authorityEnabled, authorityMask)) {
            this->executeAction(decision);
            this->m_actionsTaken++;
            this->log_WARNING_HI_FaultActionTaken(toFppType(decision.type), toFppAction(decision.action));
        } else if (decision.action != FaultLogic::NO_ACTION) {
            this->m_shadowActionsSuppressed++;
            this->log_WARNING_LO_FaultActionSuppressed(toFppType(decision.type), toFppAction(decision.action));
        }
    }

    this->lock();
    this->snapshotTable(snapshot);
    this->unLock();

    snapshot.shadowSuppressed = this->m_shadowActionsSuppressed;
    snapshot.actionsTaken = this->m_actionsTaken;
    snapshot.lastValue = this->m_lastValue;
    snapshot.authorityState = authorityEnabled ? authorityMask : static_cast<U8>(0);
    snapshot.lastType = static_cast<U8>(this->m_lastType);
    snapshot.lastSource = static_cast<U8>(this->m_lastSource);

    this->writeChangedTelemetry(snapshot);
}

// ----------------------------------------------------------------------
// Handler implementations for commands
// ----------------------------------------------------------------------

void FaultManager ::CLEAR_FAULTS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->lock();
    this->m_table.clear();
    this->m_table.setDebounce(FaultLogic::LOW_BATTERY, this->m_debounceLowBattery);
    for (U8 i = 0; i < 4; i++) {
        this->m_table.setDebounce(THERMAL_TYPES[i], this->m_debounceThermal);
    }
    this->unLock();

    this->m_lastType = FaultLogic::NONE;
    this->m_lastSource = FaultLogic::THERMAL_MANAGER;
    this->m_lastValue = 0.0f;

    this->log_ACTIVITY_HI_FaultsCleared();
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void FaultManager ::GET_FAULT_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->lock();
    const U8 active = this->m_table.activeMask();
    const U32 detected = this->m_table.totalReports();
    const U32 confirmed = this->m_table.totalConfirmed();
    const U8 authority = this->m_authorityEnabled ? this->m_authorityMask : static_cast<U8>(0);
    this->unLock();

    this->log_ACTIVITY_LO_FaultStatusReport(active, detected, confirmed, authority);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

// ----------------------------------------------------------------------
// Parameter update hook
// ----------------------------------------------------------------------

void FaultManager ::parameterUpdated(FwPrmIdType id) {
    // Every parameter of this component is cached, and re-reading all four is
    // cheaper than branching, so the id is not needed.
    (void)id;
    this->refreshParameters();
}

// ----------------------------------------------------------------------
// Private helpers
// ----------------------------------------------------------------------

void FaultManager ::refreshParameters() {
    Fw::ParamValid valid;

    const bool enabledRaw = this->paramGet_AUTHORITY_ENABLED(valid);
    const bool enabled = paramUsable(valid) ? enabledRaw : DEFAULT_AUTHORITY_ENABLED;

    const U8 maskRaw = this->paramGet_AUTHORITY_MASK(valid);
    const U8 mask = paramUsable(valid) ? maskRaw : DEFAULT_AUTHORITY_MASK;

    const U8 lowBatteryRaw = this->paramGet_DEBOUNCE_LOW_BATTERY(valid);
    const U8 lowBattery = paramUsable(valid) ? lowBatteryRaw : DEFAULT_DEBOUNCE_LOW_BATTERY;

    const U8 thermalRaw = this->paramGet_DEBOUNCE_THERMAL(valid);
    const U8 thermal = paramUsable(valid) ? thermalRaw : DEFAULT_DEBOUNCE_THERMAL;

    this->lock();
    const bool authorityChanged = (enabled != this->m_authorityEnabled) || (mask != this->m_authorityMask);
    this->m_authorityEnabled = enabled;
    this->m_authorityMask = mask;
    this->m_debounceLowBattery = lowBattery;
    this->m_debounceThermal = thermal;
    this->m_table.setDebounce(FaultLogic::LOW_BATTERY, lowBattery);
    for (U8 i = 0; i < 4; i++) {
        this->m_table.setDebounce(THERMAL_TYPES[i], thermal);
    }
    this->unLock();

    if (authorityChanged) {
        this->log_WARNING_HI_FaultAuthorityChanged(enabled, mask);
    }
}

void FaultManager ::executeAction(const FaultLogic::Decision& decision) {
    switch (decision.action) {
        case FaultLogic::SAFE_MODE:
            if (this->isConnected_forceSafeMode_OutputPort(0)) {
                this->forceSafeMode_out(0, FaultManager::reasonFor(decision.type));
            }
            break;
        case FaultLogic::SAFE_MODE_AND_REBOOT:
            // Same two ports and reason as ModeManager::commandLossCheck uses
            // today; the order (stopWatchdog first) is the one the retired
            // AuthenticationRouter used and the FaultManager-4 criterion pins.
            if (this->isConnected_stopWatchdog_OutputPort(0)) {
                this->stopWatchdog_out(0);
            }
            if (this->isConnected_forceSafeMode_OutputPort(0)) {
                this->forceSafeMode_out(0, FaultManager::reasonFor(decision.type));
            }
            break;
        case FaultLogic::NO_ACTION:
        default:
            break;
    }
}

Components::SafeModeReason FaultManager ::reasonFor(FaultLogic::Type type) {
    if (type == FaultLogic::LOW_BATTERY) {
        return Components::SafeModeReason::LOW_BATTERY;
    }
    if (type == FaultLogic::COMMAND_LOSS) {
        // ModeManager::commandLossCheck persists COMMAND_LOSS (upstream 1af2a0c5);
        // a CLAIMED command loss must leave the same reason behind (Cycle F, F3).
        return Components::SafeModeReason::COMMAND_LOSS;
    }
    // Every other type that may gain an action later enters as EXTERNAL_REQUEST.
    return Components::SafeModeReason::EXTERNAL_REQUEST;
}

void FaultManager ::snapshotTable(TlmCache& snapshot) const {
    snapshot.faultsDetected = this->m_table.totalReports();
    snapshot.faultsConfirmed = this->m_table.totalConfirmed();
    snapshot.activeFaults = this->m_table.activeMask();

    U32 thermal = 0;
    for (U8 i = 0; i < 4; i++) {
        thermal += this->m_table.status(THERMAL_TYPES[i]).reports;
    }
    snapshot.countThermal = thermal;
    snapshot.countLowBattery = this->m_table.status(FaultLogic::LOW_BATTERY).reports;
    snapshot.countCommandLoss = this->m_table.status(FaultLogic::COMMAND_LOSS).reports;
    snapshot.countWatchdogStop = this->m_table.status(FaultLogic::WATCHDOG_STOPPED).reports;
}

void FaultManager ::writeChangedTelemetry(const TlmCache& snapshot) {
    const bool all = !this->m_tlmPrimed;

    if (all || (snapshot.faultsDetected != this->m_tlmCache.faultsDetected)) {
        this->tlmWrite_FaultsDetected(snapshot.faultsDetected);
    }
    if (all || (snapshot.faultsConfirmed != this->m_tlmCache.faultsConfirmed)) {
        this->tlmWrite_FaultsConfirmed(snapshot.faultsConfirmed);
    }
    if (all || (snapshot.activeFaults != this->m_tlmCache.activeFaults)) {
        this->tlmWrite_ActiveFaults(snapshot.activeFaults);
    }
    if (all || (snapshot.lastType != this->m_tlmCache.lastType)) {
        this->tlmWrite_LastFaultType(toFppType(static_cast<FaultLogic::Type>(snapshot.lastType)));
    }
    if (all || (snapshot.lastSource != this->m_tlmCache.lastSource)) {
        this->tlmWrite_LastFaultSource(toFppSource(static_cast<FaultLogic::Source>(snapshot.lastSource)));
    }
    if (all || (snapshot.lastValue != this->m_tlmCache.lastValue)) {
        this->tlmWrite_LastFaultValue(snapshot.lastValue);
    }
    if (all || (snapshot.shadowSuppressed != this->m_tlmCache.shadowSuppressed)) {
        this->tlmWrite_ShadowActionsSuppressed(snapshot.shadowSuppressed);
    }
    if (all || (snapshot.actionsTaken != this->m_tlmCache.actionsTaken)) {
        this->tlmWrite_ActionsTaken(snapshot.actionsTaken);
    }
    if (all || (snapshot.authorityState != this->m_tlmCache.authorityState)) {
        this->tlmWrite_AuthorityState(snapshot.authorityState);
    }
    if (all || (snapshot.countThermal != this->m_tlmCache.countThermal)) {
        this->tlmWrite_FaultCountThermal(snapshot.countThermal);
    }
    if (all || (snapshot.countLowBattery != this->m_tlmCache.countLowBattery)) {
        this->tlmWrite_FaultCountLowBattery(snapshot.countLowBattery);
    }
    if (all || (snapshot.countCommandLoss != this->m_tlmCache.countCommandLoss)) {
        this->tlmWrite_FaultCountCommandLoss(snapshot.countCommandLoss);
    }
    if (all || (snapshot.countWatchdogStop != this->m_tlmCache.countWatchdogStop)) {
        this->tlmWrite_FaultCountWatchdogStop(snapshot.countWatchdogStop);
    }

    this->m_tlmCache = snapshot;
    this->m_tlmPrimed = true;
}

}  // namespace Components
