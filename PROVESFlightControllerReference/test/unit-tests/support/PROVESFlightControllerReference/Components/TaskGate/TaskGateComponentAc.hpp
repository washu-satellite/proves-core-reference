// ======================================================================
// \title  TaskGateComponentAc.hpp (host-test stub)
// \brief  Stand-in for the fpp-generated TaskGate component base.
//
// Declares the exact base-class surface TaskGate.cpp/.hpp use, and records
// every outgoing effect (schedOut port calls, telemetry writes, events,
// command responses) into public members so host tests can assert on them —
// the same role the autocoded TesterBase plays in a full F Prime UT build.
// Handlers are public virtuals here so tests can invoke the component's
// private overrides through a base reference, as real autocode does.
//
// Port count mirrors TaskGate.fpp (NUM_TASKS = 5) and is static constexpr
// because TaskGate.hpp uses it as an array bound, exactly as ThermalManager
// uses getNum_faceTempGet_OutputPorts().
// ======================================================================

#ifndef UnitTestSupport_TaskGateComponentAc_HPP
#define UnitTestSupport_TaskGateComponentAc_HPP

#include <string>
#include <vector>

#include "../../../FpTypesStub.hpp"

namespace Components {

//! Mirrors the generated shape of the FPP enum Components.SchedTask: values
//! addressed as SchedTask::IMU etc., a public `e` member (the generated enums
//! expose one, see TelemetryTxStateEnumAc.hpp:161) and an implicit conversion
//! to the underlying enumerator.
class SchedTask {
  public:
    enum T { IMU = 0, POWER_MONITOR = 1, ADCS = 2, THERMAL = 3, FS_SPACE = 4 };
    SchedTask() : e(IMU) {}
    SchedTask(T value) : e(value) {}        // NOLINT(runtime/explicit) -- implicit by design, mirrors generated code
    operator T() const { return this->e; }  // NOLINT(runtime/explicit) -- enables switch/case
    bool operator==(const SchedTask& other) const { return this->e == other.e; }
    bool operator==(T value) const { return this->e == value; }
    T value() const { return this->e; }

    T e;
};

class TaskGateComponentBase {
  public:
    struct CmdResponseRecord {
        FwOpcodeType opCode;
        U32 cmdSeq;
        Fw::CmdResponse response;
    };

    //! One recorded schedOut call.
    struct SchedOutRecord {
        FwIndexType portNum;
        U32 context;
    };

    explicit TaskGateComponentBase(const char* const compName) : compName(compName) {}
    virtual ~TaskGateComponentBase() {}

    // ---- port counts (constexpr: TaskGate.hpp uses this as an array bound) ----
    static constexpr FwIndexType getNum_schedIn_InputPorts() { return 5; }
    static constexpr FwIndexType getNum_schedOut_OutputPorts() { return 5; }

    // ---- handlers implemented by the component (private overrides there) ----
    virtual void schedIn_handler(FwIndexType portNum, U32 context) = 0;
    virtual void ENABLE_TASK_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, Components::SchedTask task) = 0;
    virtual void DISABLE_TASK_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, Components::SchedTask task) = 0;

    // ---- recorded outgoing effects, public for test inspection ----
    std::string compName;
    std::vector<SchedOutRecord> schedOutCalls;
    std::vector<U32> tlmTasksEnabledMask;
    std::vector<U32> tlmGatedRuns;
    std::vector<SchedTask::T> eventsTaskEnabled;
    std::vector<SchedTask::T> eventsTaskDisabled;
    std::vector<CmdResponseRecord> cmdResponses;

  protected:
    // ---- base-class services the component implementation calls ----
    void schedOut_out(FwIndexType portNum, U32 context) {
        this->schedOutCalls.push_back(SchedOutRecord{portNum, context});
    }

    void tlmWrite_TasksEnabledMask(U32 mask) { this->tlmTasksEnabledMask.push_back(mask); }

    void tlmWrite_GatedRuns(U32 runs) { this->tlmGatedRuns.push_back(runs); }

    void log_ACTIVITY_HI_TaskEnabled(const SchedTask& task) { this->eventsTaskEnabled.push_back(task.value()); }

    void log_ACTIVITY_HI_TaskDisabled(const SchedTask& task) { this->eventsTaskDisabled.push_back(task.value()); }

    void cmdResponse_out(FwOpcodeType opCode, U32 cmdSeq, const Fw::CmdResponse& response) {
        this->cmdResponses.push_back(CmdResponseRecord{opCode, cmdSeq, response});
    }
};

}  // namespace Components

#endif
