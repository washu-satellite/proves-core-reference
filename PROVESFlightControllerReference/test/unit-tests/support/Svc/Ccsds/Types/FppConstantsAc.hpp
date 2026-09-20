// ======================================================================
// \title  Svc/Ccsds/Types/FppConstantsAc.hpp (host-test stub)
// \brief  Stand-in for the fpp-generated CCSDS TC subfield constants.
//
// Mirrors the generated shape used by the flight code
// (lib/fprime/Svc/FrameAccumulator/FrameDetector/CcsdsTcFrameDetector.hpp:45-46
// reads Ccsds::TCSubfields::BypassFlagOffset) and the values declared in
// lib/fprime/Svc/Ccsds/Types/Types.fpp:55-67. Only the constants
// TcFrameCorrector.cpp actually names are provided.
// ======================================================================

#ifndef UnitTestSupport_Svc_Ccsds_Types_FppConstantsAc_HPP
#define UnitTestSupport_Svc_Ccsds_Types_FppConstantsAc_HPP

#include "../../../FpTypesStub.hpp"

namespace Svc {

namespace Ccsds {

namespace TCSubfields {

//! Bit offset of the bypass flag within flagsAndScId (Types.fpp:61).
enum FppConstant_BypassFlagOffset { BypassFlagOffset = 13 };

//! Mask of the 10-bit frame-length subfield of vcIdAndLength (Types.fpp:65).
enum FppConstant_FrameLengthMask { FrameLengthMask = 1023 };

}  // namespace TCSubfields

}  // namespace Ccsds

}  // namespace Svc

#endif
