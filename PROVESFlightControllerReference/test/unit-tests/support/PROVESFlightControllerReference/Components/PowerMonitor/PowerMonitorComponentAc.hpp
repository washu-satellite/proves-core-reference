// ======================================================================
// \title  PowerMonitorComponentAc.hpp (host-test stub)
// \brief  Stand-in for the fpp-generated PowerMonitor component base.
//
// Declares the exact base-class surface PowerMonitor.cpp/.hpp use, and records
// every outgoing effect into public members so host tests can assert on them —
// the same role the autocoded TesterBase plays in a full F Prime UT build.
//
// Parameter default mirrors PowerMonitor.fpp (COLLECTION_INTERVAL_S default 1
// id 0). Port signatures mirror Ina219Manager.fpp:2-4
//   port VoltageGet -> F64 / CurrentGet -> F64 / PowerGet -> F64
// The component reads the clock through getTime(); tests advance the public
// `now` member to control the energy-integration delta.
// ======================================================================

#ifndef UnitTestSupport_PowerMonitorComponentAc_HPP
#define UnitTestSupport_PowerMonitorComponentAc_HPP

#include <string>
#include <vector>

#include "../../../Fw/Time/Time.hpp"

namespace Components {

class PowerMonitorComponentBase {
  public:
    explicit PowerMonitorComponentBase(const char* const compName) : compName(compName) {}
    virtual ~PowerMonitorComponentBase() {}

    //! One recorded command response.
    struct CmdResponseRecord {
        FwOpcodeType opCode;
        U32 cmdSeq;
        Fw::CmdResponse::T response;
    };

    //! Parameter id of COLLECTION_INTERVAL_S (PowerMonitor.fpp "id 0").
    static constexpr FwPrmIdType PARAMID_COLLECTION_INTERVAL_S = 0;

    // ---- handlers implemented by the component (private overrides there) ----
    virtual void run_handler(FwIndexType portNum, U32 context) = 0;

    //! Public here so a test can drive the parameter-update path directly; the
    //! component overrides it privately, exactly as against real autocode.
    virtual void parameterUpdated(FwPrmIdType id) = 0;

    //! Called by the generated loadParameters() once /prmDb.dat has been read
    //! (F Prime 4.3.0); does nothing by default. Public here so a test can
    //! drive the boot path exactly as the framework does.
    virtual void parametersLoaded() {}

    virtual void RESET_TOTAL_POWER_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) = 0;
    virtual void RESET_TOTAL_GENERATION_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) = 0;
    virtual void GET_TOTAL_POWER_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) = 0;

    // ---- test-controlled sensor readings ----
    F64 sysVoltage = 0.0;
    F64 sysCurrent = 0.0;
    F64 sysPower = 0.0;
    F64 solVoltage = 0.0;
    F64 solCurrent = 0.0;
    F64 solPower = 0.0;

    // ---- test-controlled clock ----
    Fw::Time now;

    // ---- test-controlled parameter value ----
    U8 collectionIntervalS = 1;
    Fw::ParamValid paramValidity = Fw::ParamValid::VALID;

    // ---- recorded outgoing effects, public for test inspection ----
    std::string compName;
    U32 sysVoltageReads = 0;
    U32 sysCurrentReads = 0;
    U32 sysPowerReads = 0;
    U32 solVoltageReads = 0;
    U32 solCurrentReads = 0;
    U32 solPowerReads = 0;
    std::vector<F32> tlmTotalPowerConsumption;
    std::vector<F32> tlmTotalPowerGenerated;
    std::vector<U8> tlmCollectionIntervalS;
    std::vector<U8> eventsCollectionIntervalRejected;
    U32 eventsTotalPowerReset = 0;
    U32 eventsTotalGenerationReset = 0;
    std::vector<F32> eventsTotalPowerConsumptionReading;
    std::vector<CmdResponseRecord> cmdResponses;

  protected:
    // ---- base-class services the component implementation calls ----
    F64 sysVoltageGet_out(FwIndexType portNum) {
        (void)portNum;
        this->sysVoltageReads++;
        return this->sysVoltage;
    }

    F64 sysCurrentGet_out(FwIndexType portNum) {
        (void)portNum;
        this->sysCurrentReads++;
        return this->sysCurrent;
    }

    F64 sysPowerGet_out(FwIndexType portNum) {
        (void)portNum;
        this->sysPowerReads++;
        return this->sysPower;
    }

    F64 solVoltageGet_out(FwIndexType portNum) {
        (void)portNum;
        this->solVoltageReads++;
        return this->solVoltage;
    }

    F64 solCurrentGet_out(FwIndexType portNum) {
        (void)portNum;
        this->solCurrentReads++;
        return this->solCurrent;
    }

    F64 solPowerGet_out(FwIndexType portNum) {
        (void)portNum;
        this->solPowerReads++;
        return this->solPower;
    }

    Fw::Time getTime() { return this->now; }

    U8 paramGet_COLLECTION_INTERVAL_S(Fw::ParamValid& valid) {
        valid = this->paramValidity;
        return this->collectionIntervalS;
    }

    void tlmWrite_TotalPowerConsumption(F32 power_mWh) { this->tlmTotalPowerConsumption.push_back(power_mWh); }

    void tlmWrite_TotalPowerGenerated(F32 power_mWh) { this->tlmTotalPowerGenerated.push_back(power_mWh); }

    void tlmWrite_CollectionIntervalS(U8 interval_s) { this->tlmCollectionIntervalS.push_back(interval_s); }

    void log_ACTIVITY_LO_TotalPowerReset() { this->eventsTotalPowerReset++; }

    void log_ACTIVITY_LO_TotalGenerationReset() { this->eventsTotalGenerationReset++; }

    void log_ACTIVITY_LO_TotalPowerConsumptionReading(F32 power_mWh) {
        this->eventsTotalPowerConsumptionReading.push_back(power_mWh);
    }

    void log_WARNING_LO_CollectionIntervalRejected(U8 requested) {
        this->eventsCollectionIntervalRejected.push_back(requested);
    }

    void cmdResponse_out(FwOpcodeType opCode, U32 cmdSeq, Fw::CmdResponse response) {
        this->cmdResponses.push_back(CmdResponseRecord{opCode, cmdSeq, response.value()});
    }
};

}  // namespace Components

#endif
