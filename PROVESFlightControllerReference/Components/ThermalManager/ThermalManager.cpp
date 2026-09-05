// ======================================================================
// \title  ThermalManager.cpp
// \brief  cpp file for ThermalManager component implementation class
// ======================================================================

#include "PROVESFlightControllerReference/Components/ThermalManager/ThermalManager.hpp"

#include <Fw/Types/Assert.hpp>

namespace Components {

// ----------------------------------------------------------------------
// Component construction and destruction
// ----------------------------------------------------------------------

ThermalManager::ThermalManager(const char* const compName)
    : ThermalManagerComponentBase(compName), m_interval_s(Components::DEFAULT_INTERVAL_S) {}

ThermalManager::~ThermalManager() {}

// ----------------------------------------------------------------------
// Parameter update hook
// ----------------------------------------------------------------------

void ThermalManager::parameterUpdated(FwPrmIdType id) {
    switch (id) {
        case ThermalManager::PARAMID_COLLECTION_INTERVAL_S: {
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
            break;  // Other parameters are read on demand and need no cache
    }
}

// ----------------------------------------------------------------------
// Handler implementations for typed input ports
// ----------------------------------------------------------------------

void ThermalManager::run_handler(FwIndexType portNum, U32 context) {
    if (!this->m_interval.due(this->m_interval_s)) {
        return;
    }

    Fw::Success condition;

    // Face temp sensors
    for (FwIndexType i = 0; i < this->getNum_faceTempGet_OutputPorts(); i++) {
        const F64 temperature = this->faceTempGet_out(i, condition);
        if (condition == Fw::Success::SUCCESS) {  // Only evaluate thresholds if temperature reading was successful
            this->evaluateTemperatureThreshold(i, temperature, this->faceAboveTemperatureThrottleActive[i],
                                               this->faceBelowTemperatureThrottleActive[i],
                                               Components::ThermalManager_TempSensorType::FACE);
        }
    }

    // Battery cell temp sensors
    for (FwIndexType i = 0; i < this->getNum_battCellTempGet_OutputPorts(); i++) {
        const F64 temperature = this->battCellTempGet_out(i, condition);
        if (condition == Fw::Success::SUCCESS) {  // Only evaluate thresholds if temperature reading was successful
            this->evaluateTemperatureThreshold(i, temperature, this->battCellAboveTemperatureThrottleActive[i],
                                               this->battCellBelowTemperatureThrottleActive[i],
                                               Components::ThermalManager_TempSensorType::BATTERY);
        }
    }

    // Pico temp sensor
    this->picoTempGet_out(0, condition);

    // Report the interval this sweep ran at; the channel is "update on change",
    // so this costs a packet send only when the interval actually changes.
    this->tlmWrite_CollectionIntervalS(this->m_interval_s);
}

// ----------------------------------------------------------------------
// Private helper methods
// ----------------------------------------------------------------------

void ThermalManager::evaluateTemperatureThreshold(U32 idx,
                                                  F64 temperature,
                                                  bool& aboveTemperatureThrottleActive,
                                                  bool& belowTemperatureThrottleActive,
                                                  Components::ThermalManager_TempSensorType sensorType) {
    // Initialize parameter values
    Fw::ParamValid param_valid;
    F64 lowerThreshold = 0.0;
    F64 upperThreshold = 0.0;

    switch (sensorType) {
        case Components::ThermalManager_TempSensorType::FACE:
            lowerThreshold = this->paramGet_FACE_TEMP_LOWER_THRESHOLD(param_valid);
            upperThreshold = this->paramGet_FACE_TEMP_UPPER_THRESHOLD(param_valid);
            break;
        case Components::ThermalManager_TempSensorType::BATTERY:
            lowerThreshold = this->paramGet_BATT_CELL_TEMP_LOWER_THRESHOLD(param_valid);
            upperThreshold = this->paramGet_BATT_CELL_TEMP_UPPER_THRESHOLD(param_valid);
            break;
        default:
            FW_ASSERT(0, sensorType);  // Invalid temperature sensor type
    }

    // Check below temperature threshold
    if (!belowTemperatureThrottleActive && temperature < lowerThreshold) {
        belowTemperatureThrottleActive = true;
        aboveTemperatureThrottleActive = false;
        this->log_WARNING_LO_TemperatureBelowThreshold(sensorType, idx, temperature);
        return;
    }
    if (temperature > (lowerThreshold + ThermalManager::DEBOUNCE_ERROR)) {
        belowTemperatureThrottleActive = false;
    }

    // Check above temperature threshold
    if (!aboveTemperatureThrottleActive && temperature > upperThreshold) {
        aboveTemperatureThrottleActive = true;
        belowTemperatureThrottleActive = false;
        this->log_WARNING_LO_TemperatureAboveThreshold(sensorType, idx, temperature);
        return;
    }
    if (temperature < (upperThreshold - ThermalManager::DEBOUNCE_ERROR)) {
        aboveTemperatureThrottleActive = false;
    }
}

}  // namespace Components
