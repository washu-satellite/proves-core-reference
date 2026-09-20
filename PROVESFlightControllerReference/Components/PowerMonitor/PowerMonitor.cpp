// ======================================================================
// \title  PowerMonitor.cpp
// \brief  cpp file for PowerMonitor component implementation class
// ======================================================================

#include "PROVESFlightControllerReference/Components/PowerMonitor/PowerMonitor.hpp"

#include <Fw/Time/Time.hpp>

namespace Components {

// ----------------------------------------------------------------------
// Component construction and destruction
// ----------------------------------------------------------------------

PowerMonitor ::PowerMonitor(const char* const compName)
    : PowerMonitorComponentBase(compName),
      m_totalPower_mWh(0.0f),
      m_totalGeneration_mWh(0.0f),
      m_lastUpdateTime_s(0.0),
      m_interval_s(Components::DEFAULT_INTERVAL_S) {}

PowerMonitor ::~PowerMonitor() {}

// ----------------------------------------------------------------------
// Parameter update hook
// ----------------------------------------------------------------------

void PowerMonitor ::parameterUpdated(FwPrmIdType id) {
    switch (id) {
        case PowerMonitor::PARAMID_COLLECTION_INTERVAL_S: {
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

void PowerMonitor ::parametersLoaded() {
    // The generated loadParameters() fills the parameter database from
    // /prmDb.dat but never calls parameterUpdated(); apply the saved value so
    // a reboot does not fall back to the compiled default (A10).
    this->parameterUpdated(PowerMonitor::PARAMID_COLLECTION_INTERVAL_S);
}

// ----------------------------------------------------------------------
// Handler implementations for typed input ports
// ----------------------------------------------------------------------

void PowerMonitor ::run_handler(FwIndexType portNum, U32 context) {
    if (!this->m_interval.due(this->m_interval_s)) {
        return;
    }

    // System Power Monitor Requests
    this->sysVoltageGet_out(0);
    this->sysCurrentGet_out(0);
    F64 sysPowerW = this->sysPowerGet_out(0);

    // Solar Panel Power Monitor Requests
    this->solVoltageGet_out(0);
    this->solCurrentGet_out(0);
    F64 solPowerW = this->solPowerGet_out(0);

    // Update total power consumption with combined system and solar power
    F64 totalPowerW = sysPowerW + solPowerW;
    this->updatePower(totalPowerW);

    // Update total solar power generation
    this->updateGeneration(solPowerW);

    // Report the interval this sample ran at; the channel is "update on change",
    // so this costs a packet send only when the interval actually changes.
    this->tlmWrite_CollectionIntervalS(this->m_interval_s);
}

// ----------------------------------------------------------------------
// Handler implementations for commands
// ----------------------------------------------------------------------

void PowerMonitor ::RESET_TOTAL_POWER_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->m_totalPower_mWh = 0.0f;
    this->m_lastUpdateTime_s = this->getCurrentTimeSeconds();
    this->log_ACTIVITY_LO_TotalPowerReset();
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void PowerMonitor ::RESET_TOTAL_GENERATION_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->m_totalGeneration_mWh = 0.0f;
    this->log_ACTIVITY_LO_TotalGenerationReset();
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void PowerMonitor ::GET_TOTAL_POWER_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->log_ACTIVITY_LO_TotalPowerConsumptionReading(this->m_totalPower_mWh);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

// ----------------------------------------------------------------------
// Helper method implementations
// ----------------------------------------------------------------------

F64 PowerMonitor ::maxAccumulationDt(U8 interval_s) {
    const F64 twoIntervals = 2.0 * static_cast<F64>(interval_s);
    return (twoIntervals > 10.0) ? twoIntervals : 10.0;
}

F64 PowerMonitor ::getCurrentTimeSeconds() {
    Fw::Time t = this->getTime();
    return static_cast<F64>(t.getSeconds()) + (static_cast<F64>(t.getUSeconds()) / 1.0e6);
}

void PowerMonitor ::updatePower(F64 powerW) {
    // Guard against invalid power values
    if (powerW < 0.0 || powerW > 1000.0) {  // Sanity check: power should be 0-1000W
        return;
    }

    F64 now_s = this->getCurrentTimeSeconds();

    // Initialize time on first call
    if (this->m_lastUpdateTime_s == 0.0) {
        this->m_lastUpdateTime_s = now_s;
        // Emit initial telemetry value
        this->tlmWrite_TotalPowerConsumption(this->m_totalPower_mWh);
        return;
    }

    F64 dt_s = now_s - this->m_lastUpdateTime_s;

    // Only accumulate if time has passed and the delta is reasonable (a gap longer
    // than twice the collection interval, and at least 10 s, reads as a time jump)
    if (dt_s > 0.0 && dt_s < PowerMonitor::maxAccumulationDt(this->m_interval_s)) {
        // Convert to mWh: Power (W) * time (hours) * 1000
        F32 energyAdded_mWh = static_cast<F32>(powerW * (dt_s / 3600.0) * 1000.0);
        this->m_totalPower_mWh += energyAdded_mWh;
    }

    this->m_lastUpdateTime_s = now_s;

    // Emit telemetry update
    this->tlmWrite_TotalPowerConsumption(this->m_totalPower_mWh);
}

void PowerMonitor ::updateGeneration(F64 powerW) {
    // Guard against invalid power values
    if (powerW < 0.0 || powerW > 1000.0) {  // Sanity check: power should be 0-1000W
        return;
    }

    F64 now_s = this->getCurrentTimeSeconds();

    // Initialize time on first call
    if (this->m_lastUpdateTime_s == 0.0) {
        // Emit initial telemetry value
        this->tlmWrite_TotalPowerGenerated(this->m_totalGeneration_mWh);
        return;
    }

    F64 dt_s = now_s - this->m_lastUpdateTime_s;

    // Only accumulate if time has passed and the delta is reasonable (a gap longer
    // than twice the collection interval, and at least 10 s, reads as a time jump)
    if (dt_s > 0.0 && dt_s < PowerMonitor::maxAccumulationDt(this->m_interval_s)) {
        // Convert to mWh: Power (W) * time (hours) * 1000
        F32 energyAdded_mWh = static_cast<F32>(powerW * (dt_s / 3600.0) * 1000.0);
        this->m_totalGeneration_mWh += energyAdded_mWh;
    }

    // Emit telemetry update
    this->tlmWrite_TotalPowerGenerated(this->m_totalGeneration_mWh);
}

}  // namespace Components
