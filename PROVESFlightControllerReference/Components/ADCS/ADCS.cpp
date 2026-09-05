// ======================================================================
// \title  ADCS.cpp
// \brief  cpp file for ADCS component implementation class
// ======================================================================

#include "PROVESFlightControllerReference/Components/ADCS/ADCS.hpp"

#include <Fw/Types/Assert.hpp>

namespace Components {

// ----------------------------------------------------------------------
// Component construction and destruction
// ----------------------------------------------------------------------

ADCS::ADCS(const char* const compName) : ADCSComponentBase(compName), m_interval_s(Components::DEFAULT_INTERVAL_S) {}

ADCS::~ADCS() {}

// ----------------------------------------------------------------------
// Parameter update hook
// ----------------------------------------------------------------------

void ADCS::parameterUpdated(FwPrmIdType id) {
    switch (id) {
        case ADCS::PARAMID_COLLECTION_INTERVAL_S: {
            Fw::ParamValid is_valid;
            const U8 requested = this->paramGet_COLLECTION_INTERVAL_S(is_valid);
            const bool valid = (is_valid != Fw::ParamValid::INVALID) && (is_valid != Fw::ParamValid::UNINIT);
            // Only a stored, in-range value replaces the cached interval; every
            // other outcome leaves the safe 1 s default in force.
            const U8 effective = RunInterval::effective(requested, valid);
            if (!valid || (effective != requested)) {
                this->log_WARNING_LO_CollectionIntervalRejected(requested);
            }
            this->m_interval_s = effective;
            this->tlmWrite_CollectionIntervalS(this->m_interval_s);
        } break;
        default:
            break;  // No other parameter is cached
    }
}

// ----------------------------------------------------------------------
// Handler implementations for typed input ports
// ----------------------------------------------------------------------

void ADCS::run_handler(FwIndexType portNum, U32 context) {
    if (!this->m_interval.due(this->m_interval_s)) {
        return;
    }

    Fw::Success condition;

    // Visible light
    for (FwIndexType i = 0; i < this->getNum_visibleLightGet_OutputPorts(); i++) {
        this->visibleLightGet_out(i, condition);
    }

    // Report the interval this sweep ran at; the channel is "update on change",
    // so this costs a packet send only when the interval actually changes.
    this->tlmWrite_CollectionIntervalS(this->m_interval_s);
}

}  // namespace Components
