// ======================================================================
// \title  config/FppConstantsAc.hpp (host-test stub)
// \brief  Stand-in for the fpp-generated project configuration constants.
//
// Mirrors the generated shape ComCfg::SpacecraftId (an anonymous enum since
// F Prime 4.3.0 / fpp 3.3.0; before the 2026-09 sync it was
// ComCfg::FppConstant_SpacecraftId::SpacecraftId) used by TcFrameCorrector.cpp,
// with the value declared in
// PROVESFlightControllerReference/project/config/ComCfg.fpp:12 (0x0044 == 68).
// Only the constants the flight code under test names are provided.
// ======================================================================

#ifndef UnitTestSupport_config_FppConstantsAc_HPP
#define UnitTestSupport_config_FppConstantsAc_HPP

#include "../FpTypesStub.hpp"

namespace ComCfg {

//! Spacecraft ID (10 bits) for the CCSDS Data Link layer (ComCfg.fpp:12).
enum { SpacecraftId = 68 };

}  // namespace ComCfg

#endif
