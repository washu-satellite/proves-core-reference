// ======================================================================
// \title  OnEnumAc.hpp (host-test stub)
// \brief  Stand-in for the fpp-generated Fw::On enum.
//
// Values mirror lib/fprime/Fw/Types/Types.fpp:32-35 (OFF = 0, ON = 1). Shape
// mirrors the generated enum class so flight code compiles identically here
// and against real autocode.
// ======================================================================

#ifndef UnitTestSupport_OnEnumAc_HPP
#define UnitTestSupport_OnEnumAc_HPP

#include "../../FpTypesStub.hpp"

namespace Fw {

//! Mirrors the generated FPP enum Fw.On.
class On {
  public:
    enum T { OFF = 0, ON = 1 };
    On() : e(OFF) {}
    On(T e1) : e(e1) {}                     // NOLINT(runtime/explicit) -- mirrors generated code
    operator T() const { return this->e; }  // NOLINT(runtime/explicit) -- enables switch/case
    bool operator==(T e1) const { return this->e == e1; }
    bool operator!=(T e1) const { return this->e != e1; }
    T e;
};

}  // namespace Fw

#endif
