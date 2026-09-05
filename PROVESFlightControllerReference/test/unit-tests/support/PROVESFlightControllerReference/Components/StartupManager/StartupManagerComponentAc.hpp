// ======================================================================
// \title  StartupManagerComponentAc.hpp (host-test stub)
// \brief  Stand-in for the fpp-generated StartupManager component base.
//
// Declares the exact base-class surface StartupManager.cpp/.hpp use, and
// records every outgoing effect (sequence dispatches, command responses,
// telemetry writes, events) into public members so host tests can assert on
// them — the same role the autocoded TesterBase plays in a full F Prime UT
// build. Handlers are public virtuals here so tests can invoke the component's
// private overrides through a base reference, as real autocode does.
//
// Shapes mirror StartupManager.fpp:
//   events CurrentBootCount(I64), BootCountUpdateFailure,
//          QuiescenceFileInitFailure, StartupSequenceFinished,
//          StartupSequenceFailed(Fw.CmdResponse)                (fpp:29-46)
//   params ARMED bool true, QUIESCENCE_TIME {45*60, 0},
//          QUIESCENCE_START_FILE "/quiescence_start.bin",
//          STARTUP_SEQUENCE_FILE "/seq/startup.bin",
//          BOOT_COUNT_FILE "/boot_count.bin"                    (fpp:49-61)
//   telemetry BootCount: FwSizeType, QuiescenceEndTime: Fw.TimeValue (fpp:23-26)
// ======================================================================

#ifndef UnitTestSupport_StartupManagerComponentAc_HPP
#define UnitTestSupport_StartupManagerComponentAc_HPP

#include <string>
#include <vector>

#include "../../../FpTypesStub.hpp"
#include "Fw/Time/Time.hpp"
#include "Fw/Types/Assert.hpp"
#include "Fw/Types/String.hpp"

namespace Components {

class StartupManagerComponentBase {
  public:
    struct CmdResponseRecord {
        FwOpcodeType opCode;
        U32 cmdSeq;
        Fw::CmdResponse response;
    };

    explicit StartupManagerComponentBase(const char* const compName) : compName(compName) {}
    virtual ~StartupManagerComponentBase() {}

    // ---- handlers implemented by the component (private overrides there) ----
    virtual void run_handler(FwIndexType portNum, U32 context) = 0;
    virtual void completeSequence_handler(FwIndexType portNum,
                                          FwOpcodeType opCode,
                                          U32 cmdSeq,
                                          const Fw::CmdResponse& response) = 0;
    virtual void sequenceStarted_handler(FwIndexType portNum, const Fw::StringBase& fileName) = 0;
    virtual void WAIT_FOR_QUIESCENCE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) = 0;
    virtual void GET_BOOT_COUNT_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) = 0;

    // ---- test-controlled parameter values (defaults from StartupManager.fpp:49-61) ----
    bool armed = true;
    Fw::TimeIntervalValue quiescenceTime = Fw::TimeIntervalValue(45 * 60, 0);
    std::string quiescenceStartFile = "/quiescence_start.bin";
    std::string startupSequenceFile = "/seq/startup.bin";
    std::string bootCountFile = "/boot_count.bin";
    Fw::ParamValid paramValidity = Fw::ParamValid::DEFAULT;

    // ---- test-controlled time source ----
    Fw::Time injectedTime = Fw::Time(TimeBase::TB_WORKSTATION_TIME, 0, 1000, 0);

    // ---- recorded outgoing effects, public for test inspection ----
    std::string compName;
    std::vector<std::string> runSequenceCalls;
    std::vector<CmdResponseRecord> cmdResponses;

    std::vector<FwSizeType> tlmBootCount;
    std::vector<Fw::TimeValue> tlmQuiescenceEndTime;

    std::vector<I64> eventsCurrentBootCount;
    U32 eventsBootCountUpdateFailure = 0;
    U32 eventsQuiescenceFileInitFailure = 0;
    U32 eventsStartupSequenceFinished = 0;
    std::vector<Fw::CmdResponse> eventsStartupSequenceFailed;

  protected:
    // ---- time ----
    Fw::Time getTime() const { return this->injectedTime; }

    // ---- output ports ----
    void runSequence_out(FwIndexType portNum, const Fw::StringBase& filename) {
        (void)portNum;
        this->runSequenceCalls.push_back(filename.toChar());
    }

    // ---- parameters ----
    bool paramGet_ARMED(Fw::ParamValid& valid) {
        valid = this->paramValidity;
        return this->armed;
    }
    Fw::TimeIntervalValue paramGet_QUIESCENCE_TIME(Fw::ParamValid& valid) {
        valid = this->paramValidity;
        return this->quiescenceTime;
    }
    Fw::ParamString paramGet_QUIESCENCE_START_FILE(Fw::ParamValid& valid) {
        valid = this->paramValidity;
        return Fw::ParamString(this->quiescenceStartFile.c_str());
    }
    Fw::ParamString paramGet_STARTUP_SEQUENCE_FILE(Fw::ParamValid& valid) {
        valid = this->paramValidity;
        return Fw::ParamString(this->startupSequenceFile.c_str());
    }
    Fw::ParamString paramGet_BOOT_COUNT_FILE(Fw::ParamValid& valid) {
        valid = this->paramValidity;
        return Fw::ParamString(this->bootCountFile.c_str());
    }

    // ---- telemetry ----
    void tlmWrite_BootCount(FwSizeType count) { this->tlmBootCount.push_back(count); }
    void tlmWrite_QuiescenceEndTime(const Fw::TimeValue& time) { this->tlmQuiescenceEndTime.push_back(time); }

    // ---- events ----
    void log_ACTIVITY_LO_CurrentBootCount(I64 i) { this->eventsCurrentBootCount.push_back(i); }
    void log_WARNING_LO_BootCountUpdateFailure() { this->eventsBootCountUpdateFailure++; }
    void log_WARNING_LO_QuiescenceFileInitFailure() { this->eventsQuiescenceFileInitFailure++; }
    void log_ACTIVITY_LO_StartupSequenceFinished() { this->eventsStartupSequenceFinished++; }
    void log_WARNING_LO_StartupSequenceFailed(const Fw::CmdResponse& response) {
        this->eventsStartupSequenceFailed.push_back(response);
    }

    void cmdResponse_out(FwOpcodeType opCode, U32 cmdSeq, const Fw::CmdResponse& response) {
        this->cmdResponses.push_back(CmdResponseRecord{opCode, cmdSeq, response});
    }
};

}  // namespace Components

#endif
