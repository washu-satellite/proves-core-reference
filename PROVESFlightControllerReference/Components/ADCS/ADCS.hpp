// ======================================================================
// \title  ADCS.hpp
// \brief  hpp file for ADCS component implementation class
// ======================================================================

#ifndef Components_ADCS_HPP
#define Components_ADCS_HPP

#include "PROVESFlightControllerReference/Components/ADCS/ADCSComponentAc.hpp"
#include "PROVESFlightControllerReference/Components/RunInterval/RunInterval.hpp"

namespace Components {

class ADCS final : public ADCSComponentBase {
  public:
    // ----------------------------------------------------------------------
    // Component construction and destruction
    // ----------------------------------------------------------------------

    //! Construct ADCS object
    ADCS(const char* const compName  //!< The component name
    );

    //! Destroy ADCS object
    ~ADCS();

  private:
    //! Tick decimator driving the light-sensor sweep
    RunInterval m_interval;

    //! Collection interval currently in force, in seconds. Initialised to the
    //! 1 s default so a never-set parameter reads on every tick, as before.
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
};

}  // namespace Components

#endif
