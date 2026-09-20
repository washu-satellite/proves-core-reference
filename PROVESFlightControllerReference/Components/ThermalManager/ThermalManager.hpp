// ======================================================================
// \title  ThermalManager.hpp
// \brief  hpp file for ThermalManager component implementation class
// ======================================================================

#ifndef Components_ThermalManager_HPP
#define Components_ThermalManager_HPP

#include "PROVESFlightControllerReference/Components/RunInterval/RunInterval.hpp"
#include "PROVESFlightControllerReference/Components/ThermalManager/ThermalManagerComponentAc.hpp"

namespace Components {

class ThermalManager final : public ThermalManagerComponentBase {
  public:
    // ----------------------------------------------------------------------
    // Component construction and destruction
    // ----------------------------------------------------------------------

    //! Construct ThermalManager object
    ThermalManager(const char* const compName  //!< The component name
    );

    //! Destroy ThermalManager object
    ~ThermalManager();

  private:
    static constexpr F64 DEBOUNCE_ERROR = 3.0;  //!< Debounce error value for temperature threshold events

    bool faceAboveTemperatureThrottleActive[getNum_faceTempGet_OutputPorts()] = {false};
    bool faceBelowTemperatureThrottleActive[getNum_faceTempGet_OutputPorts()] = {false};
    bool battCellAboveTemperatureThrottleActive[getNum_battCellTempGet_OutputPorts()] = {false};
    bool battCellBelowTemperatureThrottleActive[getNum_battCellTempGet_OutputPorts()] = {false};

    //! Tick decimator driving the sensor sweep
    RunInterval m_interval;

    //! Collection interval currently in force, in seconds. Initialised to the
    //! 1 s default so a never-set parameter sweeps on every tick, as before.
    U8 m_interval_s;

    // ----------------------------------------------------------------------
    // Parameter update hook
    // ----------------------------------------------------------------------

    //! Recompute the effective collection interval after a parameter store
    void parameterUpdated(FwPrmIdType id  //!< The parameter ID
                          ) override;

    //! Apply the parameters loadParameters() has just read from PrmDb, so a
    //! saved interval is effective before the first tick (F Prime 4.3.0 hook)
    void parametersLoaded() override;

    // ----------------------------------------------------------------------
    // Handler implementations for typed input ports
    // ----------------------------------------------------------------------

    //! Handler implementation for run
    //!
    //! Scheduled port for periodic temperature reading
    void run_handler(FwIndexType portNum,  //!< The port number
                     U32 context           //!< The call order
                     ) override;

    // ----------------------------------------------------------------------
    // Private helper methods
    // ----------------------------------------------------------------------

    //! Helper function to log temperature threshold events
    void evaluateTemperatureThreshold(
        U32 idx,                    //!< The sensor index
        F64 temperature,            //!< The temperature reading
        bool& aboveThrottleActive,  //!< Whether the above threshold event throttle is currently active
        bool& belowThrottleActive,  //!< Whether the below threshold event throttle is currently active
        Components::ThermalManager_TempSensorType sensorType  //!< The type of the temperature sensor
    );

    //! Report one threshold crossing to the FaultManager, if connected. The
    //! disposition is ignored: this component's response to an out-of-range
    //! reading is the WARNING event, which is emitted either way.
    void reportFault(Components::FaultType type,  //!< The fault type to report
                     F64 temperature              //!< The reading that crossed the threshold
    );
};

}  // namespace Components

#endif
