// ======================================================================
// \title  PowerMonitor.hpp
// \brief  hpp file for PowerMonitor component implementation class
// ======================================================================

#ifndef Components_PowerMonitor_HPP
#define Components_PowerMonitor_HPP

#include "PROVESFlightControllerReference/Components/PowerMonitor/PowerMonitorComponentAc.hpp"
#include "PROVESFlightControllerReference/Components/RunInterval/RunInterval.hpp"

namespace Components {

class PowerMonitor final : public PowerMonitorComponentBase {
  public:
    // ----------------------------------------------------------------------
    // Component construction and destruction
    // ----------------------------------------------------------------------

    //! Construct PowerMonitor object
    PowerMonitor(const char* const compName  //!< The component name
    );

    //! Destroy PowerMonitor object
    ~PowerMonitor();

  private:
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
    void run_handler(FwIndexType portNum,  //!< The port number
                     U32 context           //!< The call order
                     ) override;

    // ----------------------------------------------------------------------
    // Handler implementations for commands
    // ----------------------------------------------------------------------

    //! Handler implementation for RESET_TOTAL_POWER
    void RESET_TOTAL_POWER_cmdHandler(FwOpcodeType opCode,  //!< The opcode
                                      U32 cmdSeq            //!< The command sequence number
                                      ) override;

    //! Handler implementation for RESET_TOTAL_GENERATION
    void RESET_TOTAL_GENERATION_cmdHandler(FwOpcodeType opCode,  //!< The opcode
                                           U32 cmdSeq            //!< The command sequence number
                                           ) override;

    //! Handler implementation for GET_TOTAL_POWER
    void GET_TOTAL_POWER_cmdHandler(FwOpcodeType opCode,  //!< The opcode
                                    U32 cmdSeq            //!< The command sequence number
                                    ) override;

    // ----------------------------------------------------------------------
    // Helper methods
    // ----------------------------------------------------------------------

    //! Get current time in seconds
    F64 getCurrentTimeSeconds();

    //! Update power consumption with new power reading
    void updatePower(F64 powerW);

    //! Update solar power generation with new power reading
    void updateGeneration(F64 powerW);

    //! Largest inter-sample gap that still counts as energy, in seconds.
    //!
    //! The accumulators ignore a gap they consider a time jump. At the 1 s
    //! default this is 10.0 s, identical to the literal it replaces; a longer
    //! collection interval widens it to twice that interval so a legitimately
    //! decimated sample is not discarded.
    static F64 maxAccumulationDt(U8 interval_s);

    //! Read the charge-status pin once, if connected, and report Charging;
    //! ChargeStateChanged fires on the first read and on every change only
    void updateChargeStatus();

    // ----------------------------------------------------------------------
    // Member variables
    // ----------------------------------------------------------------------

    //! Accumulated power consumption in mWh
    F32 m_totalPower_mWh;

    //! Accumulated solar power generation in mWh
    F32 m_totalGeneration_mWh;

    //! Last update time in seconds
    F64 m_lastUpdateTime_s;

    //! Collection interval currently in force, in seconds. Initialised to the
    //! 1 s default so a never-set parameter samples on every tick, as before.
    //! Declared after m_lastUpdateTime_s to match the constructor's init order.
    U8 m_interval_s;

    //! Tick decimator driving the power sampling
    RunInterval m_interval;

    //! True once the charge-status pin has been read at least once
    bool m_chargeKnown;

    //! Charge state from the most recent read (valid when m_chargeKnown)
    Fw::On m_charging;
};

}  // namespace Components

#endif
