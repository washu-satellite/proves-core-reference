// ======================================================================
// \title  TaskGate.cpp
// \brief  cpp file for TaskGate component implementation class
// ======================================================================

#include "PROVESFlightControllerReference/Components/TaskGate/TaskGate.hpp"

#include <Fw/Types/Assert.hpp>

namespace Components {

// ----------------------------------------------------------------------
// Component construction and destruction
// ----------------------------------------------------------------------

TaskGate ::TaskGate(const char* const compName)
    : TaskGateComponentBase(compName), m_enabled(), m_gatedRuns(0), m_published(false) {
    // Every task starts enabled: the mask default is 0x1F and a reboot always
    // restores full scheduling.
    for (FwIndexType i = 0; i < kNumTasks; i++) {
        this->m_enabled[i] = true;
    }
}

TaskGate ::~TaskGate() {}

// ----------------------------------------------------------------------
// Handler implementations for typed input ports
// ----------------------------------------------------------------------

void TaskGate ::schedIn_handler(FwIndexType portNum, U32 context) {
    FW_ASSERT(portNum >= 0 && portNum < kNumTasks, static_cast<FwAssertArgType>(portNum));

    // Publish the boot state once, on the first tick, so ground can read the
    // default mask (0x1F) and a zero gated count without sending a command.
    if (!this->m_published) {
        this->m_published = true;
        this->writeMaskTlm();
        this->tlmWrite_GatedRuns(this->m_gatedRuns);
    }

    if (this->m_enabled[portNum]) {
        // Forward the tick unchanged on the matching output port.
        this->schedOut_out(portNum, context);
        return;
    }

    // Gated: the downstream component simply does not run this cycle.
    this->m_gatedRuns++;
    this->tlmWrite_GatedRuns(this->m_gatedRuns);
}

// ----------------------------------------------------------------------
// Handler implementations for commands
// ----------------------------------------------------------------------

void TaskGate ::ENABLE_TASK_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, const Components::SchedTask& task) {
    this->setTaskEnabled(opCode, cmdSeq, task, true);
}

void TaskGate ::DISABLE_TASK_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, const Components::SchedTask& task) {
    this->setTaskEnabled(opCode, cmdSeq, task, false);
}

// ----------------------------------------------------------------------
// Private helper methods
// ----------------------------------------------------------------------

void TaskGate ::setTaskEnabled(FwOpcodeType opCode, U32 cmdSeq, Components::SchedTask task, bool enabled) {
    const FwIndexType index = static_cast<FwIndexType>(task.e);
    // SchedTask has exactly the five gateable tasks, and the dispatcher rejects
    // any other value before it reaches here, so this only guards autocode drift.
    FW_ASSERT(index >= 0 && index < kNumTasks, static_cast<FwAssertArgType>(index));

    // Latch in RAM immediately. The command is sync and the component passive,
    // so the very next 1 Hz tick already honors the new state.
    this->m_enabled[index] = enabled;

    if (enabled) {
        this->log_ACTIVITY_HI_TaskEnabled(task);
    } else {
        this->log_ACTIVITY_HI_TaskDisabled(task);
    }

    this->writeMaskTlm();
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void TaskGate ::writeMaskTlm() {
    U32 mask = 0;
    for (FwIndexType i = 0; i < kNumTasks; i++) {
        if (this->m_enabled[i]) {
            mask |= (1u << static_cast<U32>(i));
        }
    }
    this->tlmWrite_TasksEnabledMask(mask);
}

}  // namespace Components
