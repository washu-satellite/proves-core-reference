// ======================================================================
// \title  FaultTypesStub.hpp (host-test stub)
// \brief  Stand-ins for the enums fpp-to-cpp generates from FaultTypes.fpp.
//
// Each class mirrors the generated enum shape (a nested raw enum T, a public
// member e holding the value, a conversion operator and equality operators),
// so flight code compiles identically against this stub and against the real
// autocode. Values are copied from
// PROVESFlightControllerReference/Components/FaultTypes/FaultTypes.fpp.
//
// Components::SafeModeReason is NOT defined here: it belongs to ModeManager
// and already exists in that component's stub.
// ======================================================================

#ifndef UnitTestSupport_FaultTypesStub_HPP
#define UnitTestSupport_FaultTypesStub_HPP

#include "../../../FpTypesStub.hpp"

namespace Components {

//! Mirrors the generated FPP enum Components.FaultType.
class FaultType {
  public:
    enum T {
        NONE = 0,
        FACE_TEMP_HIGH = 1,
        FACE_TEMP_LOW = 2,
        BATT_TEMP_HIGH = 3,
        BATT_TEMP_LOW = 4,
        LOW_BATTERY = 5,
        COMMAND_LOSS = 6,
        WATCHDOG_STOPPED = 7,
        ADCS_UNSTABLE = 8
    };
    FaultType() : e(NONE) {}
    FaultType(T e1) : e(e1) {}              // NOLINT(runtime/explicit) -- mirrors generated code
    operator T() const { return this->e; }  // NOLINT(runtime/explicit) -- enables switch/case
    bool operator==(T e1) const { return this->e == e1; }
    bool operator!=(T e1) const { return this->e != e1; }
    T e;
};

//! Mirrors the generated FPP enum Components.FaultSource.
class FaultSource {
  public:
    enum T { THERMAL_MANAGER = 0, MODE_MANAGER = 1, AUTH_ROUTER = 2, WATCHDOG = 3, DETUMBLE_MANAGER = 4 };
    FaultSource() : e(THERMAL_MANAGER) {}
    FaultSource(T e1) : e(e1) {}            // NOLINT(runtime/explicit) -- mirrors generated code
    operator T() const { return this->e; }  // NOLINT(runtime/explicit) -- enables switch/case
    bool operator==(T e1) const { return this->e == e1; }
    bool operator!=(T e1) const { return this->e != e1; }
    T e;
};

//! Mirrors the generated FPP enum Components.FaultSeverity.
class FaultSeverity {
  public:
    enum T { WARNING = 0, CRITICAL = 1 };
    FaultSeverity() : e(WARNING) {}
    FaultSeverity(T e1) : e(e1) {}          // NOLINT(runtime/explicit) -- mirrors generated code
    operator T() const { return this->e; }  // NOLINT(runtime/explicit) -- enables switch/case
    bool operator==(T e1) const { return this->e == e1; }
    bool operator!=(T e1) const { return this->e != e1; }
    T e;
};

//! Mirrors the generated FPP enum Components.FaultAction.
class FaultAction {
  public:
    enum T { NONE = 0, SAFE_MODE = 1, SAFE_MODE_AND_REBOOT = 2 };
    FaultAction() : e(NONE) {}
    FaultAction(T e1) : e(e1) {}            // NOLINT(runtime/explicit) -- mirrors generated code
    operator T() const { return this->e; }  // NOLINT(runtime/explicit) -- enables switch/case
    bool operator==(T e1) const { return this->e == e1; }
    bool operator!=(T e1) const { return this->e != e1; }
    T e;
};

//! Mirrors the generated FPP enum Components.FaultDisposition.
class FaultDisposition {
  public:
    enum T { OBSERVED = 0, CLAIMED = 1 };
    FaultDisposition() : e(OBSERVED) {}
    FaultDisposition(T e1) : e(e1) {}       // NOLINT(runtime/explicit) -- mirrors generated code
    operator T() const { return this->e; }  // NOLINT(runtime/explicit) -- enables switch/case
    bool operator==(T e1) const { return this->e == e1; }
    bool operator!=(T e1) const { return this->e != e1; }
    T e;
};

}  // namespace Components

#endif
