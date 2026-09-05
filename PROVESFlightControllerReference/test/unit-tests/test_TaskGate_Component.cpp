// ======================================================================
// \title  test_TaskGate_Component.cpp
// \brief  Component-level host tests for TaskGate.
//
// Compiles the real TaskGate.cpp against the stubs in support/
// (TaskGateComponentAc.hpp records schedOut calls, telemetry, events and
// command responses) and drives the actual handlers. These claim TaskGate-*
// Unit requirements only: SC-L2-07 is a Board-level criterion whose evidence
// is the deferred integration test task_gate_test.py.
// ======================================================================

#include <gtest/gtest.h>

#include "PROVESFlightControllerReference/Components/TaskGate/TaskGate.hpp"

namespace {

using Components::SchedTask;
using Components::TaskGate;
using Components::TaskGateComponentBase;

constexpr FwOpcodeType OPCODE = 0x77;
constexpr U32 ALL_ENABLED_MASK = 0x1F;

class TaskGateComponentTest : public ::testing::Test {
  protected:
    //! Drive one scheduler tick into the port belonging to a task.
    static void tick(TaskGate& gate, SchedTask::T task, U32 context = 0) {
        static_cast<TaskGateComponentBase&>(gate).schedIn_handler(static_cast<FwIndexType>(task), context);
    }

    //! Drive one tick into every task's port, in ordinal order.
    static void tickAll(TaskGate& gate, U32 context = 0) {
        for (FwIndexType i = 0; i < TaskGateComponentBase::getNum_schedIn_InputPorts(); i++) {
            static_cast<TaskGateComponentBase&>(gate).schedIn_handler(i, context);
        }
    }

    static void enable(TaskGate& gate, SchedTask::T task, U32 cmdSeq = 0) {
        static_cast<TaskGateComponentBase&>(gate).ENABLE_TASK_cmdHandler(OPCODE, cmdSeq, task);
    }

    static void disable(TaskGate& gate, SchedTask::T task, U32 cmdSeq = 0) {
        static_cast<TaskGateComponentBase&>(gate).DISABLE_TASK_cmdHandler(OPCODE, cmdSeq, task);
    }

    //! Count the forwarded ticks recorded on one port.
    static size_t forwardedOn(const TaskGate& gate, SchedTask::T task) {
        size_t count = 0;
        for (const auto& call : gate.schedOutCalls) {
            if (call.portNum == static_cast<FwIndexType>(task)) {
                count++;
            }
        }
        return count;
    }
};

TEST_F(TaskGateComponentTest, DefaultForwardsEveryPortWithContext) {
    RecordProperty("verifies", "TaskGate-1");
    TaskGate gate("taskGate");

    tickAll(gate, 42);

    // Every task runs by default; the tick reaches the matching output port
    // with its call order untouched.
    ASSERT_EQ(gate.schedOutCalls.size(), 5u);
    for (FwIndexType i = 0; i < 5; i++) {
        EXPECT_EQ(gate.schedOutCalls[i].portNum, i);
        EXPECT_EQ(gate.schedOutCalls[i].context, 42u);
    }
    // Nothing gated, and the boot mask is published without any command.
    ASSERT_FALSE(gate.tlmTasksEnabledMask.empty());
    EXPECT_EQ(gate.tlmTasksEnabledMask.back(), ALL_ENABLED_MASK);
    ASSERT_FALSE(gate.tlmGatedRuns.empty());
    EXPECT_EQ(gate.tlmGatedRuns.back(), 0u);
    EXPECT_TRUE(gate.eventsTaskDisabled.empty());
}

TEST_F(TaskGateComponentTest, DisableGatesNextTickOnThatPortOnly) {
    RecordProperty("verifies", "TaskGate-2");
    TaskGate gate("taskGate");
    tickAll(gate);  // one cycle with everything running
    const size_t baseline = gate.schedOutCalls.size();
    ASSERT_EQ(baseline, 5u);

    disable(gate, SchedTask::IMU, 1);

    // The command is sync on a passive component, so it latches before the very
    // next tick: no extra cycle of IMU polling slips through.
    tickAll(gate);

    EXPECT_EQ(forwardedOn(gate, SchedTask::IMU), 1u);
    EXPECT_EQ(forwardedOn(gate, SchedTask::POWER_MONITOR), 2u);
    EXPECT_EQ(forwardedOn(gate, SchedTask::ADCS), 2u);
    EXPECT_EQ(forwardedOn(gate, SchedTask::THERMAL), 2u);
    EXPECT_EQ(forwardedOn(gate, SchedTask::FS_SPACE), 2u);

    ASSERT_EQ(gate.eventsTaskDisabled.size(), 1u);
    EXPECT_EQ(gate.eventsTaskDisabled[0], SchedTask::IMU);
    ASSERT_EQ(gate.cmdResponses.size(), 1u);
    EXPECT_TRUE(gate.cmdResponses[0].response == Fw::CmdResponse::OK);
    EXPECT_EQ(gate.tlmTasksEnabledMask.back(), ALL_ENABLED_MASK & ~(1u << SchedTask::IMU));
}

TEST_F(TaskGateComponentTest, EnableRestoresNextTick) {
    RecordProperty("verifies", "TaskGate-3");
    TaskGate gate("taskGate");
    disable(gate, SchedTask::THERMAL);
    tickAll(gate);
    ASSERT_EQ(forwardedOn(gate, SchedTask::THERMAL), 0u);

    enable(gate, SchedTask::THERMAL);
    tickAll(gate);

    EXPECT_EQ(forwardedOn(gate, SchedTask::THERMAL), 1u);
    ASSERT_EQ(gate.eventsTaskEnabled.size(), 1u);
    EXPECT_EQ(gate.eventsTaskEnabled[0], SchedTask::THERMAL);
    EXPECT_EQ(gate.tlmTasksEnabledMask.back(), ALL_ENABLED_MASK);
}

TEST_F(TaskGateComponentTest, GatedRunsAndMaskAreExact) {
    RecordProperty("verifies", "TaskGate-4");
    TaskGate gate("taskGate");

    disable(gate, SchedTask::ADCS);
    EXPECT_EQ(gate.tlmTasksEnabledMask.back(), 0x1Bu);  // 0b11011
    disable(gate, SchedTask::FS_SPACE);
    EXPECT_EQ(gate.tlmTasksEnabledMask.back(), 0x0Bu);  // 0b01011

    // Three cycles with two tasks gated: exactly six dropped ticks.
    tickAll(gate);
    tickAll(gate);
    tickAll(gate);

    EXPECT_EQ(gate.tlmGatedRuns.back(), 6u);
    EXPECT_EQ(gate.schedOutCalls.size(), 9u);

    enable(gate, SchedTask::ADCS);
    EXPECT_EQ(gate.tlmTasksEnabledMask.back(), 0x0Fu);  // 0b01111, FS_SPACE still gated
    tickAll(gate);
    // Only FS_SPACE is still gated, so the count advances by exactly one.
    EXPECT_EQ(gate.tlmGatedRuns.back(), 7u);
}

TEST_F(TaskGateComponentTest, CommandsAreIdempotentAndReturnOk) {
    RecordProperty("verifies", "TaskGate-5");
    TaskGate gate("taskGate");

    // Enabling an already-enabled task, and disabling twice, both stand.
    enable(gate, SchedTask::POWER_MONITOR, 1);
    EXPECT_EQ(gate.tlmTasksEnabledMask.back(), ALL_ENABLED_MASK);
    disable(gate, SchedTask::POWER_MONITOR, 2);
    disable(gate, SchedTask::POWER_MONITOR, 3);
    EXPECT_EQ(gate.tlmTasksEnabledMask.back(), ALL_ENABLED_MASK & ~(1u << SchedTask::POWER_MONITOR));

    tickAll(gate);
    EXPECT_EQ(forwardedOn(gate, SchedTask::POWER_MONITOR), 0u);
    EXPECT_EQ(gate.tlmGatedRuns.back(), 1u);

    ASSERT_EQ(gate.cmdResponses.size(), 3u);
    for (const auto& response : gate.cmdResponses) {
        EXPECT_TRUE(response.response == Fw::CmdResponse::OK);
    }
    EXPECT_EQ(gate.cmdResponses[0].cmdSeq, 1u);
    EXPECT_EQ(gate.cmdResponses[2].cmdSeq, 3u);
}

}  // namespace
