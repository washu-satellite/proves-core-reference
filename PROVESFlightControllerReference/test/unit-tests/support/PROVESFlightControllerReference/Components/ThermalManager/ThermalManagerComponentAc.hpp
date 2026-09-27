// ======================================================================
// \title  ThermalManagerComponentAc.hpp (host-test stub)
// \brief  Stand-in for the fpp-generated ThermalManager component base.
//
// Declares the exact base-class surface ThermalManager.cpp/.hpp use, and
// records every threshold event into public members so host tests can assert
// on them — the same role the autocoded TesterBase plays in a full F Prime UT
// build. Sensor readings and parameter values are public members the test
// writes before invoking run_handler.
//
// Port counts and parameter defaults mirror ThermalManager.fpp:
//   numFaceTempSensors = 6 (raised from 5 by Cycle L, 01-normative.md R2.1:
//   faceTempGet[5] -> tmp112Face6Manager), numBattCellTempSensors = 4,
//   FACE_TEMP_LOWER/UPPER = -40.0 / 60.0, BATT_CELL_TEMP_LOWER/UPPER = 5.0 / 60.0
// Port signature mirrors Tmp112Manager.fpp:2
//   port temperatureGet(ref condition: Fw.Success) -> F64
// and PicoTempManager.fpp:2 for picoTemperatureGet.
// ======================================================================

#ifndef UnitTestSupport_ThermalManagerComponentAc_HPP
#define UnitTestSupport_ThermalManagerComponentAc_HPP

#include <string>
#include <vector>

#include "../../../FpTypesStub.hpp"
#include "../FaultTypes/FaultTypesStub.hpp"

namespace Components {

//! Mirrors the generated shape of the FPP enum ThermalManager.TempSensorType:
//! values addressed as ThermalManager_TempSensorType::FACE / ::BATTERY, and
//! implicitly convertible to the underlying enumerator so that the switch in
//! ThermalManager.cpp:65 compiles exactly as it does against real autocode.
class ThermalManager_TempSensorType {
  public:
    enum T { FACE = 0, BATTERY = 1 };
    ThermalManager_TempSensorType() : m_value(FACE) {}
    // NOLINTNEXTLINE(runtime/explicit) -- implicit by design, mirrors generated code
    ThermalManager_TempSensorType(T value) : m_value(value) {}
    operator T() const { return this->m_value; }  // NOLINT(runtime/explicit) -- enables switch/case
    bool operator==(const ThermalManager_TempSensorType& other) const { return this->m_value == other.m_value; }
    bool operator==(T value) const { return this->m_value == value; }
    T value() const { return this->m_value; }

  private:
    T m_value;
};

class ThermalManagerComponentBase {
  public:
    //! One recorded threshold event.
    struct TempEventRecord {
        ThermalManager_TempSensorType::T sensorType;
        U32 sensorId;
        F64 temperature;
    };

    //! One recorded fault report sent out faultOut.
    struct FaultReportRecord {
        FaultType::T faultType;
        FaultSource::T source;
        FaultSeverity::T severity;
        F32 value;
    };

    explicit ThermalManagerComponentBase(const char* const compName) : compName(compName) {}
    virtual ~ThermalManagerComponentBase() {}

    //! Parameter id of COLLECTION_INTERVAL_S (ThermalManager.fpp "id 4").
    static constexpr FwPrmIdType PARAMID_COLLECTION_INTERVAL_S = 4;

    // ---- port counts (constexpr: ThermalManager.hpp:29-32 uses them as array bounds) ----
    static constexpr FwIndexType getNum_faceTempGet_OutputPorts() { return 6; }
    static constexpr FwIndexType getNum_battCellTempGet_OutputPorts() { return 4; }
    static constexpr FwIndexType getNum_picoTempGet_OutputPorts() { return 1; }

    // ---- handlers implemented by the component (private overrides there) ----
    virtual void run_handler(FwIndexType portNum, U32 context) = 0;

    //! Public here so a test can drive the parameter-update path directly; the
    //! component overrides it privately, exactly as against real autocode.
    virtual void parameterUpdated(FwPrmIdType id) = 0;

    //! Called by the generated loadParameters() once /prmDb.dat has been read
    //! (F Prime 4.3.0); does nothing by default. Public here so a test can
    //! drive the boot path exactly as the framework does.
    virtual void parametersLoaded() {}

    // ---- test-controlled sensor readings ----
    F64 faceTemp[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    Fw::Success faceTempStatus[6] = {Fw::Success::SUCCESS, Fw::Success::SUCCESS, Fw::Success::SUCCESS,
                                     Fw::Success::SUCCESS, Fw::Success::SUCCESS, Fw::Success::SUCCESS};
    F64 battTemp[4] = {0.0, 0.0, 0.0, 0.0};
    Fw::Success battTempStatus[4] = {Fw::Success::SUCCESS, Fw::Success::SUCCESS, Fw::Success::SUCCESS,
                                     Fw::Success::SUCCESS};
    F64 picoTemp = 0.0;
    Fw::Success picoTempStatus = Fw::Success::SUCCESS;

    // ---- test-controlled parameter values (defaults from ThermalManager.fpp:7-16) ----
    F64 faceLowerThreshold = -40.0;
    F64 faceUpperThreshold = 60.0;
    F64 battLowerThreshold = 5.0;
    F64 battUpperThreshold = 60.0;
    U8 collectionIntervalS = 1;
    Fw::ParamValid paramValidity = Fw::ParamValid::VALID;

    // ---- recorded outgoing effects, public for test inspection ----
    std::string compName;
    std::vector<TempEventRecord> eventsAboveThreshold;
    std::vector<TempEventRecord> eventsBelowThreshold;
    U32 faceTempReads = 0;
    U32 faceTempReadsByPort[6] = {0, 0, 0, 0, 0, 0};  //!< per-port count of faceTempGet_out calls
    U32 battTempReads = 0;
    U32 picoTempReads = 0;
    std::vector<U8> tlmCollectionIntervalS;
    std::vector<U8> eventsCollectionIntervalRejected;

    // ---- fault reporting (Cycle D). The default of "not connected" is what
    //      every test written before the port existed sees, so those tests
    //      keep observing exactly the behaviour they always did. ----
    bool faultOutConnected = false;
    Components::FaultDisposition faultOutDisposition = Components::FaultDisposition::OBSERVED;
    std::vector<FaultReportRecord> faultOutCalls;

  protected:
    // ---- base-class services the component implementation calls ----
    F64 faceTempGet_out(FwIndexType portNum, Fw::Success& condition) {
        this->faceTempReads++;
        this->faceTempReadsByPort[portNum]++;
        condition = this->faceTempStatus[portNum];
        return this->faceTemp[portNum];
    }

    F64 battCellTempGet_out(FwIndexType portNum, Fw::Success& condition) {
        this->battTempReads++;
        condition = this->battTempStatus[portNum];
        return this->battTemp[portNum];
    }

    F64 picoTempGet_out(FwIndexType portNum, Fw::Success& condition) {
        (void)portNum;
        this->picoTempReads++;
        condition = this->picoTempStatus;
        return this->picoTemp;
    }

    F64 paramGet_FACE_TEMP_LOWER_THRESHOLD(Fw::ParamValid& valid) {
        valid = this->paramValidity;
        return this->faceLowerThreshold;
    }

    F64 paramGet_FACE_TEMP_UPPER_THRESHOLD(Fw::ParamValid& valid) {
        valid = this->paramValidity;
        return this->faceUpperThreshold;
    }

    F64 paramGet_BATT_CELL_TEMP_LOWER_THRESHOLD(Fw::ParamValid& valid) {
        valid = this->paramValidity;
        return this->battLowerThreshold;
    }

    F64 paramGet_BATT_CELL_TEMP_UPPER_THRESHOLD(Fw::ParamValid& valid) {
        valid = this->paramValidity;
        return this->battUpperThreshold;
    }

    U8 paramGet_COLLECTION_INTERVAL_S(Fw::ParamValid& valid) {
        valid = this->paramValidity;
        return this->collectionIntervalS;
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

    void tlmWrite_CollectionIntervalS(U8 interval_s) { this->tlmCollectionIntervalS.push_back(interval_s); }

    void log_WARNING_LO_CollectionIntervalRejected(U8 requested) {
        this->eventsCollectionIntervalRejected.push_back(requested);
    }

    void log_WARNING_LO_TemperatureAboveThreshold(const ThermalManager_TempSensorType& sensorType,
                                                  U32 sensorId,
                                                  F64 temperature) {
        this->eventsAboveThreshold.push_back(TempEventRecord{sensorType.value(), sensorId, temperature});
    }

    void log_WARNING_LO_TemperatureBelowThreshold(const ThermalManager_TempSensorType& sensorType,
                                                  U32 sensorId,
                                                  F64 temperature) {
        this->eventsBelowThreshold.push_back(TempEventRecord{sensorType.value(), sensorId, temperature});
    }
};

}  // namespace Components

#endif
