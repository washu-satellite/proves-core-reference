// ======================================================================
// \title  ModeManager.hpp
// \author Auto-generated
// \brief  hpp file for ModeManager component implementation class
// ======================================================================

#ifndef Components_ModeManager_HPP
#define Components_ModeManager_HPP

#include "Fw/Types/String.hpp"
#include "PROVESFlightControllerReference/Components/ModeManager/ModeManagerComponentAc.hpp"

namespace Components {

class ModeManager : public ModeManagerComponentBase {
  public:
    // ----------------------------------------------------------------------
    // Component construction and destruction
    // ----------------------------------------------------------------------

    //! Construct ModeManager object
    ModeManager(const char* const compName  //!< The component name
    );

    //! Destroy ModeManager object
    ~ModeManager();

    //! Initialize the component
    void init(FwSizeType queueDepth,        //!< Queue depth for async ports
              FwEnumStoreType instance = 0  //!< Instance ID
    );

  private:
    // ----------------------------------------------------------------------
    // Handler implementations for user-defined typed input ports
    // ----------------------------------------------------------------------

    //! Handler implementation for run
    //!
    //! Port receiving calls from the rate group (1Hz)
    void run_handler(FwIndexType portNum,  //!< The port number
                     U32 context           //!< The call order
                     ) override;

    //! Handler implementation for completeSequence
    //!
    //! Port receiving completion status from the safe mode sequence
    void completeSequence_handler(FwIndexType portNum,             //!< The port number
                                  FwOpcodeType opCode,             //!< The opcode (unused)
                                  U32 cmdSeq,                      //!< The command sequence number (unused)
                                  const Fw::CmdResponse& response  //!< The command response
                                  ) override;

    //! Handler implementation for forceSafeMode
    //!
    //! Port to force safe mode entry (callable by other components)
    //! @param reason The reason for entering safe mode (NONE defaults to EXTERNAL_REQUEST)
    void forceSafeMode_handler(FwIndexType portNum,                      //!< The port number
                               const Components::SafeModeReason& reason  //!< The safe mode reason
                               ) override;

    //! Handler implementation for getMode
    //!
    //! Port to query the current system mode
    Components::SystemMode getMode_handler(FwIndexType portNum  //!< The port number
                                           ) override;

    //! Handler implementation for prepareForReboot
    //!
    //! Port called before intentional reboot to set clean shutdown flag
    void prepareForReboot_handler(FwIndexType portNum  //!< The port number
                                  ) override;

    // ----------------------------------------------------------------------
    // Handler implementations for commands
    // ----------------------------------------------------------------------

    //! Handler implementation for command FORCE_SAFE_MODE
    void FORCE_SAFE_MODE_cmdHandler(FwOpcodeType opCode,  //!< The opcode
                                    U32 cmdSeq            //!< The command sequence number
                                    ) override;

    //! Handler implementation for command EXIT_SAFE_MODE
    void EXIT_SAFE_MODE_cmdHandler(FwOpcodeType opCode,  //!< The opcode
                                   U32 cmdSeq            //!< The command sequence number
                                   ) override;

    //! Handler implementation for command GET_CURRENT_MODE
    void GET_CURRENT_MODE_cmdHandler(FwOpcodeType opCode,  //!< The opcode
                                     U32 cmdSeq            //!< The command sequence number
                                     ) override;

    //! Handler implementation for command GET_SAFE_MODE_REASON
    void GET_SAFE_MODE_REASON_cmdHandler(FwOpcodeType opCode,  //!< The opcode
                                         U32 cmdSeq            //!< The command sequence number
                                         ) override;

  private:
    // ----------------------------------------------------------------------
    // Private helper methods
    // ----------------------------------------------------------------------

    //! Load persistent state from file
    void loadState();

    //! Save persistent state to file (clean-shutdown flag clear)
    void saveState();

    //! Pack the current mode, entry count, reason and clean flag into the
    //! STATE_PAYLOAD_SIZE-byte little-endian payload described above.
    //! \param clean clean-shutdown flag byte to store (0 or 1)
    //! \param out   destination buffer, at least STATE_PAYLOAD_SIZE bytes
    void encodeState(U8 clean, U8* out) const;

    //! Encode and atomically store the current state.
    //! \param clean clean-shutdown flag byte to store (0 or 1)
    //! \param op    operation string reported by StatePersistenceFailure on error
    //! \return true on success; on failure the event is emitted and the
    //!         previously stored record is left intact
    bool storeState(U8 clean, const char* op);

    //! Enter safe mode with specified reason
    void enterSafeMode(Components::SafeModeReason reason);

    //! Exit safe mode (manual command)
    void exitSafeMode();

    //! Exit safe mode automatically due to voltage recovery
    void exitSafeModeAutomatic(F32 voltage);

    //! Turn off non-critical components
    void turnOffNonCriticalComponents();

    //! Turn on components (restore normal operation)
    void turnOnComponents();

    // run the safe mode seauence
    void runSafeModeSequence();

    //! Get current voltage from INA219 system power manager
    //! Queries voltage via the voltageGet output port
    //! \param valid Output parameter indicating if the voltage reading is valid
    //! \return Current voltage (only valid if valid parameter is set to true)
    F32 getCurrentVoltage(bool& valid);

    //! Report one low-voltage sample to the FaultManager, if connected.
    //! \param voltage The sampled voltage (0 if the reading was invalid)
    //! \return true only if the FaultManager CLAIMED the recovery action, in
    //!         which case this component must not enter safe mode itself
    bool reportLowBattery(F32 voltage);

    // ----------------------------------------------------------------------
    // Private enums and types
    // ----------------------------------------------------------------------

    //! System mode enumeration
    enum class SystemMode : U8 { SAFE_MODE = 1, NORMAL = 2 };

    //! Size of the persisted state payload. The state is carried as an explicit
    //! little-endian byte layout inside a PersistedRecord (magic, version,
    //! length, CRC), not as a raw struct, so the on-disk format does not depend
    //! on compiler padding or target endianness (MM0011):
    //!   [0]    mode               U8  (1 = SAFE_MODE, 2 = NORMAL)
    //!   [1..4] safeModeEntryCount U32 little-endian
    //!   [5]    safeModeReason     U8  (SafeModeReason ordinal, 0..5)
    //!   [6]    cleanShutdown      U8  (1 = clean, 0 = unclean)
    static constexpr U16 STATE_PAYLOAD_SIZE = 7;

    // ----------------------------------------------------------------------
    // Private member variables
    // ----------------------------------------------------------------------

    SystemMode m_mode;                            //!< Current system mode
    U32 m_safeModeEntryCount;                     //!< Counter for safe mode entries
    U32 m_runCounter;                             //!< Counter for run handler calls (1Hz)
    Components::SafeModeReason m_safeModeReason;  //!< Current safe mode reason
    U32 m_safeModeVoltageCounter;                 //!< Counter for low voltage in NORMAL mode
    U32 m_recoveryVoltageCounter;                 //!< Counter for voltage recovery in SAFE_MODE

    // ----------------------------------------------------------------------
    // Constants
    // ----------------------------------------------------------------------

    static constexpr const char* STATE_FILE_PATH = "/mode_state.bin";  //!< State file path
    //! Staging file for the atomic replace: the record is written and flushed
    //! here in full, then renamed over STATE_FILE_PATH, so a failure at any
    //! step leaves the previous record intact.
    static constexpr const char* STATE_TEMP_PATH = "/mode_state.tmp";
};

}  // namespace Components

#endif
