// ======================================================================
// \title  StartupManager.hpp
// \author starchmd
// \brief  hpp file for StartupManager component implementation class
// ======================================================================

#ifndef Components_StartupManager_HPP
#define Components_StartupManager_HPP

#include <atomic>

#include "PROVESFlightControllerReference/Components/StartupManager/StartupManagerComponentAc.hpp"

namespace Components {

class StartupManager final : public StartupManagerComponentBase {
  public:
    enum Status {
        SUCCESS,
        FAILURE,
    };
    // ----------------------------------------------------------------------
    // Component construction and destruction
    // ----------------------------------------------------------------------

    //! Construct StartupManager object
    StartupManager(const char* const compName  //!< The component name
    );

    //! Destroy StartupManager object
    ~StartupManager();

    //! \brief read and optionally increment the boot count
    //!
    //! Reads the boot count from the boot count file, which is a PersistedRecord (magic "SBC1", format version, CRC)
    //! carrying the count as a little-endian U64. A missing file is the first boot: count 0, no event. A file that is
    //! present but fails validation - truncated, wrong magic, bad length, bad CRC, unknown version, or unreadable -
    //! falls back to the same default and emits exactly one BootCountUpdateFailure (REQ-SM-008).
    //!
    //! When increment is true the count is raised by one (minimum 1) and written back through an atomic replace via
    //! "<path>.tmp"; a store failure emits one more BootCountUpdateFailure and leaves the previous record intact.
    //! When increment is false the file is read but not rewritten.
    //!
    //! \warning this function will modify the boot count file on disk when increment is true.
    //!
    //! \return The updated boot count
    FwSizeType get_boot_count(bool increment);

    //! \brief get and possibly initialize the quiescence start time
    //!
    //! Reads the quiescence start time from the quiescence start time file, a PersistedRecord (magic "SQS1", format
    //! version, CRC) carrying time base, context, seconds and microseconds as explicit little-endian fields. A valid
    //! record is returned untouched - there is a single quiescence start time for the whole mission.
    //!
    //! A missing file is the first boot: quiescence starts now and is written, with no event. A file that is present
    //! but fails validation - including a CRC-valid record whose useconds field is outside [0, 999999], which would
    //! panic Fw::Time::add in a boot loop - restarts quiescence from now, writes it, and emits exactly one
    //! QuiescenceFileInitFailure; a failing store emits a second (REQ-SM-008).
    //!
    //! \warning this function will modify the quiescence start time file on disk if it does not already hold a valid
    //!          record.
    //!
    //! \return The quiescence start time
    Fw::Time update_quiescence_start();

    // \brief get the system uptime
    //!
    Fw::Time get_uptime();

  private:
    // ----------------------------------------------------------------------
    // Handler implementations for typed input ports
    // ----------------------------------------------------------------------

    //! Handler implementation for completeSequence
    void completeSequence_handler(FwIndexType portNum,             //!< The port number
                                  FwOpcodeType opCode,             //!< Command Op Code
                                  U32 cmdSeq,                      //!< Command Sequence
                                  const Fw::CmdResponse& response  //!< The command response argument
                                  ) override;

    //! Handler implementation for sequenceStarted
    void sequenceStarted_handler(FwIndexType portNum,            //!< The port number
                                 const Fw::StringBase& fileName  //!< The file path for start-up sequence
                                 ) override;

    //! Handler implementation for run
    //!
    //! Check RTC time diff
    void run_handler(FwIndexType portNum,  //!< The port number
                     U32 context           //!< The call order
                     ) override;

  private:
    // ----------------------------------------------------------------------
    // Handler implementations for commands
    // ----------------------------------------------------------------------

    //! Handler implementation for command WAIT_FOR_QUIESCENCE
    //!
    //! Command to wait for system quiescence before proceeding with start-up
    void WAIT_FOR_QUIESCENCE_cmdHandler(FwOpcodeType opCode,  //!< The opcode
                                        U32 cmdSeq            //!< The command sequence number
                                        ) override;

    //! Handler implementation for command GET_BOOT_COUNT
    //!
    //! Command to output the current boot count
    void GET_BOOT_COUNT_cmdHandler(FwOpcodeType opCode,  //!< The opcode
                                   U32 cmdSeq            //!< The command sequence number
                                   ) override;

  private:
    Fw::Time m_quiescence_start;   //!< Time of the start of the quiescence wait
    FwOpcodeType m_stored_opcode;  //!< Stored opcode for delayed response
    FwSizeType m_boot_count;       //!< Current boot count
    U32 m_stored_sequence;         //!< Stored sequence number for delayed response
    std::atomic<bool> m_waiting;   //!< Indicates if waiting for quiescence
    Fw::String m_sequence_file;    //!< The filepath for the sequence last initiated
};

}  // namespace Components

#endif
