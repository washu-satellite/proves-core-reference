// ======================================================================
// \title  FaultManager.hpp
// \brief  hpp file for FaultManager component implementation class
// ======================================================================

#ifndef Components_FaultManager_HPP
#define Components_FaultManager_HPP

#include "PROVESFlightControllerReference/Components/FaultManager/FaultManagerComponentAc.hpp"
#include "PROVESFlightControllerReference/Components/FaultManager/FaultTable.hpp"

namespace Components {

class FaultManager : public FaultManagerComponentBase {
  public:
    // ----------------------------------------------------------------------
    // Component construction and destruction
    // ----------------------------------------------------------------------

    //! Construct FaultManager object
    explicit FaultManager(const char* const compName  //!< The component name
    );

    //! Destroy FaultManager object
    ~FaultManager();

  private:
    //! Last-written value of every telemetry channel, so run_handler can write
    //! only what actually changed.
    struct TlmCache {
        U32 faultsDetected;
        U32 faultsConfirmed;
        U32 shadowSuppressed;
        U32 actionsTaken;
        U32 countThermal;
        U32 countLowBattery;
        U32 countCommandLoss;
        U32 countWatchdogStop;
        F32 lastValue;
        U8 activeFaults;
        U8 authorityState;
        U8 lastType;
        U8 lastSource;
    };

    // ----------------------------------------------------------------------
    // Handler implementations for user-defined typed input ports
    // ----------------------------------------------------------------------

    //! Handler implementation for faultIn
    //!
    //! Guarded: runs under the component mutex and touches only the fault
    //! table. Returns CLAIMED only when this component holds authority for the
    //! reported type, which never happens with the shipped parameter defaults.
    Components::FaultDisposition faultIn_handler(FwIndexType portNum,  //!< The port number
                                                 const Components::FaultType& faultType,
                                                 const Components::FaultSource& source,
                                                 const Components::FaultSeverity& faultSeverity,
                                                 F32 value) override;

    //! Handler implementation for run
    //!
    //! Drains confirmations, then acts on them outside the lock
    void run_handler(FwIndexType portNum,  //!< The port number
                     U32 context           //!< The call order
                     ) override;

    // ----------------------------------------------------------------------
    // Handler implementations for commands
    // ----------------------------------------------------------------------

    //! Handler implementation for command CLEAR_FAULTS
    void CLEAR_FAULTS_cmdHandler(FwOpcodeType opCode,  //!< The opcode
                                 U32 cmdSeq            //!< The command sequence number
                                 ) override;

    //! Handler implementation for command GET_FAULT_STATUS
    void GET_FAULT_STATUS_cmdHandler(FwOpcodeType opCode,  //!< The opcode
                                     U32 cmdSeq            //!< The command sequence number
                                     ) override;

    // ----------------------------------------------------------------------
    // Parameter update hook
    // ----------------------------------------------------------------------

    //! Cache the authority gate and the debounce thresholds. Any parameter
    //! that is INVALID or UNINIT falls back to the shadow-mode default.
    void parameterUpdated(FwPrmIdType id) override;

    // ----------------------------------------------------------------------
    // Private helpers
    // ----------------------------------------------------------------------

    //! Re-read every parameter and apply it. Callers must not hold the lock.
    void refreshParameters();

    //! Perform the recovery action for one confirmed fault. Only ever reached
    //! through FaultTable::claims(), and never with the lock held.
    void executeAction(const FaultLogic::Decision& decision);

    //! Safe mode reason associated with a fault type
    static Components::SafeModeReason reasonFor(FaultLogic::Type type);

    //! Fill the table-derived fields of a snapshot. Caller holds the lock.
    void snapshotTable(TlmCache& snapshot) const;

    //! Write the channels whose value differs from the previous tick
    void writeChangedTelemetry(const TlmCache& snapshot);

    // ----------------------------------------------------------------------
    // Member variables (declared in initialisation order; -Wreorder)
    // ----------------------------------------------------------------------

    FaultLogic::FaultTable m_table;   //!< Debounce and confirmation bookkeeping
    TlmCache m_tlmCache;              //!< Last telemetry values written
    U32 m_tick;                       //!< 1 Hz tick counter
    U32 m_shadowActionsSuppressed;    //!< Actions declined for lack of authority
    U32 m_actionsTaken;               //!< Actions performed by this component
    F32 m_lastValue;                  //!< Value of the last confirmed fault
    U8 m_authorityMask;               //!< Cached AUTHORITY_MASK
    U8 m_debounceLowBattery;          //!< Cached DEBOUNCE_LOW_BATTERY
    U8 m_debounceThermal;             //!< Cached DEBOUNCE_THERMAL
    FaultLogic::Type m_lastType;      //!< Type of the last confirmed fault
    FaultLogic::Source m_lastSource;  //!< Source of the last confirmed fault
    bool m_authorityEnabled;          //!< Cached AUTHORITY_ENABLED
    bool m_tlmPrimed;                 //!< False until the first telemetry write
};

}  // namespace Components

#endif
