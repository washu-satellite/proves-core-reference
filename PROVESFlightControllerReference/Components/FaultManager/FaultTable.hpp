// ======================================================================
// \title  FaultTable.hpp
// \brief  Pure fault bookkeeping: debounce, confirmation, action policy.
//
// F Prime free by construction (<cstdint> only), like
// PersistedRecord/PersistedRecordCodec.cpp, so the whole decision logic of
// the FaultManager is testable on the host with no framework linked.
//
// The enums below mirror the FPP enums in Components/FaultTypes/FaultTypes.fpp;
// FaultManager.cpp carries static_asserts that pin the two together, so no
// runtime mapping table exists and neither definition can drift.
// ======================================================================

#ifndef Components_FaultTable_HPP
#define Components_FaultTable_HPP

#include <cstdint>

namespace Components {
namespace FaultLogic {

//! Mirrors Components.FaultType (FaultTypes.fpp).
enum Type : uint8_t {
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

//! Number of table slots: NONE plus the eight tracked fault types.
constexpr uint8_t NUM_FAULT_TYPES = 9;

//! Highest tracked fault type; also the number of mask bits in use.
constexpr uint8_t MAX_FAULT_TYPE = 8;

//! Mirrors Components.FaultSource (FaultTypes.fpp).
enum Source : uint8_t { THERMAL_MANAGER = 0, MODE_MANAGER = 1, AUTH_ROUTER = 2, WATCHDOG = 3, DETUMBLE_MANAGER = 4 };

//! Mirrors Components.FaultSeverity (FaultTypes.fpp).
enum Severity : uint8_t { WARNING = 0, CRITICAL = 1 };

//! Mirrors Components.FaultAction (FaultTypes.fpp). NO_ACTION is spelled out
//! because the unscoped Type enum already owns NONE in this namespace.
enum Action : uint8_t { NO_ACTION = 0, SAFE_MODE = 1, SAFE_MODE_AND_REBOOT = 2 };

//! Mirrors Components.FaultDisposition (FaultTypes.fpp).
enum Disposition : uint8_t { OBSERVED = 0, CLAIMED = 1 };

//! Per-type behaviour, fixed at compile time. "sampled" marks a fault whose
//! producer reports it once per 1 Hz sample while the condition holds (so a
//! tick with no report re-arms the debounce); an unsampled fault is reported
//! by a one-shot event and stays confirmed until clear().
struct Policy {
    uint8_t debounce;   //!< consecutive reports needed to confirm (>= 1)
    Action action;      //!< recovery action once confirmed
    bool sampled;       //!< true if a silent tick re-arms the debounce
    Severity severity;  //!< severity the policy assigns to the type
};

//! One incoming fault report.
struct Report {
    Type type;      //!< kind of fault
    Source source;  //!< reporting component
    float value;    //!< measured value behind the report
};

//! One confirmation emitted by tick().
struct Decision {
    Type type;      //!< kind of fault confirmed
    Action action;  //!< the policy action for that type
    Source source;  //!< source of the report that confirmed it
    float value;    //!< value of the report that confirmed it
};

//! Per-type bookkeeping.
struct Status {
    uint32_t reports;       //!< total reports ever seen for this type
    uint32_t lastTick;      //!< tick number of the most recent report
    float lastValue;        //!< value of the most recent report
    Source lastSource;      //!< source of the most recent report
    uint8_t consecutive;    //!< consecutive reporting ticks since the last re-arm
    bool confirmed;         //!< debounce satisfied and not yet cleared
    bool pending;           //!< confirmation edge not yet drained by tick()
    bool reportedThisTick;  //!< a report arrived since the last tick()
};

//! Default policy for a fault type. Values reproduce what the producers
//! already do today; see Components/FaultManager/docs/sdd.md.
Policy defaultPolicy(Type type);

//! Mask bit for a fault type: 1 << (type - 1); 0 for NONE and out of range.
uint8_t bitFor(Type type);

//! Fixed-size fault bookkeeping. No heap, no mutable global state, no I/O.
class FaultTable {
  public:
    FaultTable();

    //! Override the confirmation threshold for one type. 0 is coerced to 1 so
    //! a misconfigured parameter can never confirm a fault without a report.
    void setDebounce(Type type, uint8_t n);

    //! Record one report. Returns true exactly on the confirmation edge (the
    //! report that first satisfies the debounce), false otherwise.
    bool report(const Report& r, uint32_t tickNumber);

    //! Drain confirmation edges into out[0..max) and re-arm sampled types that
    //! did not report since the previous tick. Returns the number written.
    uint8_t tick(uint32_t tickNumber, Decision* out, uint8_t max);

    //! Forget every report, confirmation and counter.
    void clear();

    //! Bookkeeping for one type (NONE and out-of-range read back as slot 0).
    const Status& status(Type type) const;

    //! Bitmask of the currently confirmed types.
    uint8_t activeMask() const;

    //! Total reports across all types since construction or clear().
    uint32_t totalReports() const;

    //! Total confirmations across all types since construction or clear().
    uint32_t totalConfirmed() const;

    //! The shadow gate. True only when authority is enabled, the type's bit is
    //! set in the mask, and the type actually has an action to take. With the
    //! shipped defaults (enabled false, mask 0) this is false for every type,
    //! so no caller can reach an action port.
    static bool claims(Type type, bool authorityEnabled, uint8_t mask);

  private:
    Status m_status[NUM_FAULT_TYPES];
    uint8_t m_debounce[NUM_FAULT_TYPES];
    uint32_t m_totalReports;
    uint32_t m_totalConfirmed;
};

}  // namespace FaultLogic
}  // namespace Components

#endif
