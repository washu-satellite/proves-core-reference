// ======================================================================
// \title  TaskGate.hpp
// \brief  hpp file for TaskGate component implementation class
// ======================================================================

#ifndef Components_TaskGate_HPP
#define Components_TaskGate_HPP

#include "PROVESFlightControllerReference/Components/TaskGate/TaskGateComponentAc.hpp"

namespace Components {

class TaskGate final : public TaskGateComponentBase {
  public:
    // ----------------------------------------------------------------------
    // Component construction and destruction
    // ----------------------------------------------------------------------

    //! Construct TaskGate object
    TaskGate(const char* const compName  //!< The component name
    );

    //! Destroy TaskGate object
    ~TaskGate();

  private:
    // ----------------------------------------------------------------------
    // Handler implementations for typed input ports
    // ----------------------------------------------------------------------

    //! Handler implementation for schedIn
    //!
    //! Forwards the tick on the matching schedOut port while task portNum is
    //! enabled; otherwise the tick is dropped and counted. Gating one task
    //! never affects any other port.
    void schedIn_handler(FwIndexType portNum,  //!< The port number (== SchedTask ordinal)
                         U32 context           //!< The call order
                         ) override;

  private:
    // ----------------------------------------------------------------------
    // Handler implementations for commands
    // ----------------------------------------------------------------------

    //! Handler implementation for command ENABLE_TASK
    //!
    //! Latches the task enabled in RAM so the very next tick is forwarded.
    //! Idempotent: enabling an already-enabled task still responds OK and still
    //! emits TaskEnabled.
    void ENABLE_TASK_cmdHandler(FwOpcodeType opCode,        //!< The opcode
                                U32 cmdSeq,                 //!< The command sequence number
                                Components::SchedTask task  //!< The task to enable
                                ) override;

    //! Handler implementation for command DISABLE_TASK
    //!
    //! Latches the task disabled in RAM so the very next tick is dropped.
    //! Idempotent: disabling an already-disabled task still responds OK and
    //! still emits TaskDisabled.
    void DISABLE_TASK_cmdHandler(FwOpcodeType opCode,        //!< The opcode
                                 U32 cmdSeq,                 //!< The command sequence number
                                 Components::SchedTask task  //!< The task to disable
                                 ) override;

  private:
    // ----------------------------------------------------------------------
    // Private helper methods
    // ----------------------------------------------------------------------

    //! \brief Set the enabled state of one task, emit its event, republish the
    //!        mask and respond OK. Shared by both command handlers.
    void setTaskEnabled(FwOpcodeType opCode, U32 cmdSeq, Components::SchedTask task, bool enabled);

    //! \brief Write TasksEnabledMask from m_enabled (bit i = task i enabled).
    void writeMaskTlm();

  private:
    //! Number of gateable tasks, taken from the autocoded port count so the
    //! array bound, the port arrays and the SchedTask enum cannot drift apart.
    static constexpr FwIndexType kNumTasks = getNum_schedIn_InputPorts();

    //! Per-task enable flags, indexed by SchedTask ordinal (== port index). All
    //! true at boot: state is RAM-only, so a reset always restores full
    //! scheduling.
    bool m_enabled[kNumTasks];
    //! Cumulative count of scheduler ticks dropped across all tasks since boot
    U32 m_gatedRuns;
    //! Whether the boot-time mask and counter have been published once, so
    //! ground sees the default 0x1F without having to send a command first.
    bool m_published;
};

}  // namespace Components

#endif
