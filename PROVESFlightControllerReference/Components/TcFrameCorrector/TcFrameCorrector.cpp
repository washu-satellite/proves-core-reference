// ======================================================================
// \title  TcFrameCorrector.cpp
// \brief  cpp file for TcFrameCorrector component implementation class
// ======================================================================

#include "PROVESFlightControllerReference/Components/TcFrameCorrector/TcFrameCorrector.hpp"

#include "PROVESFlightControllerReference/Components/TcFrameCorrector/TcFrameCorrectorCodec.hpp"
#include "Svc/Ccsds/Types/FppConstantsAc.hpp"
#include "config/FppConstantsAc.hpp"

namespace Components {

namespace {
//! Expected value of the first two frame bytes: bypass flag set, control flag
//! clear, spacecraft ID in the low ten bits. Same expression as the frame
//! detector's m_expectedFlagsAndScIdToken
//! (lib/fprime/Svc/FrameAccumulator/FrameDetector/CcsdsTcFrameDetector.hpp:45-46),
//! so a frame this component accepts is one the detector would accept.
constexpr U16 EXPECTED_TOKEN =
    static_cast<U16>((1u << Svc::Ccsds::TCSubfields::BypassFlagOffset) | static_cast<U32>(ComCfg::SpacecraftId));
}  // namespace

// ----------------------------------------------------------------------
// Component construction and destruction
// ----------------------------------------------------------------------

TcFrameCorrector ::TcFrameCorrector(const char* const compName)
    : TcFrameCorrectorComponentBase(compName), m_correctedFrames(0), m_uncorrectableFrames(0) {}

TcFrameCorrector ::~TcFrameCorrector() {}

// ----------------------------------------------------------------------
// Handler implementations for typed input ports
// ----------------------------------------------------------------------

void TcFrameCorrector ::dataIn_handler(FwIndexType portNum, Fw::Buffer& data, const ComCfg::FrameContext& context) {
    Fw::ParamValid valid = Fw::ParamValid::INVALID;
    const bool enabled = this->paramGet_CORRECTION_ENABLED(valid);

    // Disabled, or the parameter could not be read: forward the very same
    // buffer object with no copy and no byte touched. This is the flight
    // default, so the uplink path is byte-identical to the one without this
    // component until the parameter is deliberately set.
    if (!enabled || (valid != Fw::ParamValid::VALID && valid != Fw::ParamValid::DEFAULT)) {
        this->dataOut_out(0, data, context);
        return;
    }

    const U32 frameLength = static_cast<U32>(data.getSize());
    U16 bitIndex = 0;
    const TcFrameCorrection::Result result =
        TcFrameCorrection::correctSingleBit(data.getData(), frameLength, EXPECTED_TOKEN, bitIndex);

    if (result == TcFrameCorrection::Result::CORRECTED) {
        this->m_correctedFrames++;
        this->log_ACTIVITY_HI_FrameCorrected(bitIndex, static_cast<U16>(frameLength));
        this->tlmWrite_CorrectedFrames(this->m_correctedFrames);
    } else if (result == TcFrameCorrection::Result::UNCORRECTABLE) {
        this->m_uncorrectableFrames++;
        this->log_WARNING_LO_FrameUncorrectable(static_cast<U16>(frameLength));
        this->tlmWrite_UncorrectableFrames(this->m_uncorrectableFrames);
    }
    // VALID and PASS_THROUGH are the common case and are reported nowhere: the
    // frame is untouched and simply moves on.

    // Every buffer leaves on dataOut exactly once, whatever the outcome. An
    // uncorrectable frame is forwarded unchanged so the frame accumulator
    // rejects it byte-wise exactly as it does today.
    this->dataOut_out(0, data, context);
}

void TcFrameCorrector ::dataReturnIn_handler(FwIndexType portNum,
                                             Fw::Buffer& data,
                                             const ComCfg::FrameContext& context) {
    // Ownership handed back from downstream goes straight upstream to the LoRa
    // driver, which allocated it (mirrors Authenticate.cpp:395, TcDeframer.cpp:114).
    this->dataReturnOut_out(0, data, context);
}

}  // namespace Components
