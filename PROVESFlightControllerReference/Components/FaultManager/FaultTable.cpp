// ======================================================================
// \title  FaultTable.cpp
// \brief  Implementation of the pure fault bookkeeping module.
// ======================================================================

#include "PROVESFlightControllerReference/Components/FaultManager/FaultTable.hpp"

namespace Components {
namespace FaultLogic {

namespace {

//! Debounce for the sampled low-voltage path. Mirrors the ModeManager default
//! SafeModeDebounceSeconds = 10 (ModeManager.fpp "param SafeModeDebounceSeconds
//! U32 default 10") so a confirmation lands on the same 1 Hz tick as today's
//! AutoSafeModeEntry.
constexpr uint8_t LOW_BATTERY_DEBOUNCE = 10;

//! Event-driven faults confirm on the first report: the producer has already
//! applied its own hysteresis or latch before calling.
constexpr uint8_t EVENT_DEBOUNCE = 1;

}  // namespace

Policy defaultPolicy(Type type) {
    Policy policy;
    policy.debounce = EVENT_DEBOUNCE;
    policy.action = NO_ACTION;
    policy.sampled = false;
    policy.severity = WARNING;

    switch (type) {
        case FACE_TEMP_HIGH:
        case FACE_TEMP_LOW:
        case BATT_TEMP_HIGH:
        case BATT_TEMP_LOW:
            // ThermalManager already debounces with a 3 C hysteresis band and
            // takes no action beyond the WARNING event today.
            break;
        case LOW_BATTERY:
            policy.debounce = LOW_BATTERY_DEBOUNCE;
            policy.action = SAFE_MODE;
            policy.sampled = true;
            policy.severity = CRITICAL;
            break;
        case COMMAND_LOSS:
            // AuthenticationRouter::CallSafeMode stops the watchdog and then
            // forces safe mode; the manager reproduces that order exactly.
            policy.action = SAFE_MODE_AND_REBOOT;
            policy.severity = CRITICAL;
            break;
        case WATCHDOG_STOPPED:
            // The hardware reset is already under way; nothing left to do.
            policy.severity = CRITICAL;
            break;
        case ADCS_UNSTABLE:
            // FD-L2-07 hook: threshold is TBD by Mission Ops, so no action.
            break;
        case NONE:
        default:
            break;
    }
    return policy;
}

uint8_t bitFor(Type type) {
    if ((type == NONE) || (static_cast<uint8_t>(type) > MAX_FAULT_TYPE)) {
        return 0;
    }
    return static_cast<uint8_t>(1u << (static_cast<uint8_t>(type) - 1u));
}

bool FaultTable::claims(Type type, bool authorityEnabled, uint8_t mask) {
    const uint8_t bit = bitFor(type);
    if ((bit == 0) || !authorityEnabled) {
        return false;
    }
    if ((mask & bit) == 0) {
        return false;
    }
    return defaultPolicy(type).action != NO_ACTION;
}

FaultTable::FaultTable() : m_totalReports(0), m_totalConfirmed(0) {
    this->clear();
}

void FaultTable::clear() {
    for (uint8_t i = 0; i < NUM_FAULT_TYPES; i++) {
        this->m_status[i].reports = 0;
        this->m_status[i].lastTick = 0;
        this->m_status[i].lastValue = 0.0f;
        this->m_status[i].lastSource = THERMAL_MANAGER;
        this->m_status[i].consecutive = 0;
        this->m_status[i].confirmed = false;
        this->m_status[i].pending = false;
        this->m_status[i].reportedThisTick = false;
        this->m_debounce[i] = defaultPolicy(static_cast<Type>(i)).debounce;
    }
    this->m_totalReports = 0;
    this->m_totalConfirmed = 0;
}

void FaultTable::setDebounce(Type type, uint8_t n) {
    if ((type == NONE) || (static_cast<uint8_t>(type) >= NUM_FAULT_TYPES)) {
        return;
    }
    this->m_debounce[static_cast<uint8_t>(type)] = (n == 0) ? 1 : n;
}

bool FaultTable::report(const Report& r, uint32_t tickNumber) {
    if ((r.type == NONE) || (static_cast<uint8_t>(r.type) >= NUM_FAULT_TYPES)) {
        return false;
    }
    Status& s = this->m_status[static_cast<uint8_t>(r.type)];
    s.reports++;
    s.lastTick = tickNumber;
    s.lastValue = r.value;
    s.lastSource = r.source;
    s.reportedThisTick = true;
    this->m_totalReports++;

    // Saturate rather than wrap: a fault that stays reported for 255 ticks must
    // not fall back below its threshold and re-confirm.
    if (s.consecutive < 0xFF) {
        s.consecutive++;
    }

    if ((s.consecutive >= this->m_debounce[static_cast<uint8_t>(r.type)]) && !s.confirmed) {
        s.confirmed = true;
        s.pending = true;
        this->m_totalConfirmed++;
        return true;
    }
    return false;
}

uint8_t FaultTable::tick(uint32_t tickNumber, Decision* out, uint8_t max) {
    (void)tickNumber;
    uint8_t written = 0;
    for (uint8_t i = 1; i < NUM_FAULT_TYPES; i++) {
        Status& s = this->m_status[i];
        const Type type = static_cast<Type>(i);
        const Policy policy = defaultPolicy(type);

        if (s.pending) {
            s.pending = false;
            if ((out != nullptr) && (written < max)) {
                out[written].type = type;
                out[written].action = policy.action;
                out[written].source = s.lastSource;
                out[written].value = s.lastValue;
                written++;
            }
        }

        // A sampled fault whose producer stayed silent for a whole tick is no
        // longer present: re-arm the debounce, mirroring the counter reset in
        // ModeManager::run_handler's "voltage OK" branch.
        if (policy.sampled && !s.reportedThisTick) {
            s.consecutive = 0;
            s.confirmed = false;
        }
        s.reportedThisTick = false;
    }
    return written;
}

const Status& FaultTable::status(Type type) const {
    if (static_cast<uint8_t>(type) >= NUM_FAULT_TYPES) {
        return this->m_status[0];
    }
    return this->m_status[static_cast<uint8_t>(type)];
}

uint8_t FaultTable::activeMask() const {
    uint8_t mask = 0;
    for (uint8_t i = 1; i < NUM_FAULT_TYPES; i++) {
        if (this->m_status[i].confirmed) {
            mask = static_cast<uint8_t>(mask | bitFor(static_cast<Type>(i)));
        }
    }
    return mask;
}

uint32_t FaultTable::totalReports() const {
    return this->m_totalReports;
}

uint32_t FaultTable::totalConfirmed() const {
    return this->m_totalConfirmed;
}

}  // namespace FaultLogic
}  // namespace Components
