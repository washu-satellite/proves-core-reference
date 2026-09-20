// ======================================================================
// \title  ADCSComponentAc.hpp (host-test stub)
// \brief  Stand-in for the fpp-generated ADCS component base.
//
// Declares the exact base-class surface ADCS.cpp/.hpp use, and records every
// outgoing effect into public members so host tests can assert on them — the
// same role the autocoded TesterBase plays in a full F Prime UT build.
//
// Port count and parameter default mirror ADCS.fpp:
//   numLightSensors = 6, COLLECTION_INTERVAL_S default 1 id 0.
// Port signature mirrors Veml6031Manager.fpp:2
//   port lightGet(ref condition: Fw.Success) -> F32
// ======================================================================

#ifndef UnitTestSupport_ADCSComponentAc_HPP
#define UnitTestSupport_ADCSComponentAc_HPP

#include <string>
#include <vector>

#include "../../../FpTypesStub.hpp"

namespace Components {

class ADCSComponentBase {
  public:
    explicit ADCSComponentBase(const char* const compName) : compName(compName) {}
    virtual ~ADCSComponentBase() {}

    //! Parameter id of COLLECTION_INTERVAL_S (ADCS.fpp "id 0").
    static constexpr FwPrmIdType PARAMID_COLLECTION_INTERVAL_S = 0;

    // ---- port counts (ADCS.fpp numLightSensors = 6) ----
    static constexpr FwIndexType getNum_visibleLightGet_OutputPorts() { return 6; }

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
    F32 visibleLight[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    Fw::Success visibleLightStatus[6] = {Fw::Success::SUCCESS, Fw::Success::SUCCESS, Fw::Success::SUCCESS,
                                         Fw::Success::SUCCESS, Fw::Success::SUCCESS, Fw::Success::SUCCESS};

    // ---- test-controlled parameter value ----
    U8 collectionIntervalS = 1;
    Fw::ParamValid paramValidity = Fw::ParamValid::VALID;

    // ---- recorded outgoing effects, public for test inspection ----
    std::string compName;
    U32 visibleLightReads = 0;
    std::vector<U8> tlmCollectionIntervalS;
    std::vector<U8> eventsCollectionIntervalRejected;

  protected:
    // ---- base-class services the component implementation calls ----
    F32 visibleLightGet_out(FwIndexType portNum, Fw::Success& condition) {
        this->visibleLightReads++;
        condition = this->visibleLightStatus[portNum];
        return this->visibleLight[portNum];
    }

    U8 paramGet_COLLECTION_INTERVAL_S(Fw::ParamValid& valid) {
        valid = this->paramValidity;
        return this->collectionIntervalS;
    }

    void tlmWrite_CollectionIntervalS(U8 interval_s) { this->tlmCollectionIntervalS.push_back(interval_s); }

    void log_WARNING_LO_CollectionIntervalRejected(U8 requested) {
        this->eventsCollectionIntervalRejected.push_back(requested);
    }
};

}  // namespace Components

#endif
