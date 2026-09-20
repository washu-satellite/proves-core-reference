// ======================================================================
// \title  WatchdogComponentAc.hpp (host-test stub)
// \brief  Stand-in for the fpp-generated Watchdog component base.
//
// Declares the exact base-class surface Watchdog.cpp/.hpp use, and records
// every outgoing effect (GPIO writes, reboot notifications, telemetry, events,
// command responses, fault reports) into public members so host tests can
// assert on them — the same role the autocoded TesterBase plays in a full
// F Prime UT build. Handlers are public virtuals here so tests can invoke the
// component's private overrides through a base reference, as real autocode
// does.
//
// Shapes mirror Watchdog.fpp: telemetry WatchdogTransitions U32, events
// WatchdogStart/WatchdogStop, output ports prepareForReboot (Fw.Signal),
// gpioSet (Drv.GpioWrite -> Fw.Logic) and faultOut (Components.FaultReport).
// ======================================================================

#ifndef UnitTestSupport_WatchdogComponentAc_HPP
#define UnitTestSupport_WatchdogComponentAc_HPP

#include <string>
#include <vector>

#include "../../../FpTypesStub.hpp"
#include "../FaultTypes/FaultTypesStub.hpp"

namespace Components {

class WatchdogComponentBase {
  public:
    struct CmdResponseRecord {
        FwOpcodeType opCode;
        U32 cmdSeq;
        Fw::CmdResponse response;
    };

    //! One recorded fault report sent out faultOut.
    struct FaultReportRecord {
        FaultType::T faultType;
        FaultSource::T source;
        FaultSeverity::T severity;
        F32 value;
    };

    explicit WatchdogComponentBase(const char* const compName) : compName(compName) {}
    virtual ~WatchdogComponentBase() {}

    // ---- handlers implemented by the component (private overrides there) ----
    virtual void run_handler(FwIndexType portNum, U32 context) = 0;
    virtual void start_handler(FwIndexType portNum) = 0;
    virtual void stop_handler(FwIndexType portNum) = 0;
    virtual void START_WATCHDOG_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) = 0;
    virtual void STOP_WATCHDOG_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) = 0;

    // ---- fault reporting (Cycle D). Default "not connected" keeps the
    //      pre-existing behaviour visible to a test that does not opt in. ----
    bool faultOutConnected = false;
    Components::FaultDisposition faultOutDisposition = Components::FaultDisposition::OBSERVED;
    std::vector<FaultReportRecord> faultOutCalls;

    // ---- recorded outgoing effects, public for test inspection ----
    std::string compName;
    std::vector<Fw::Logic::T> gpioSetCalls;
    U32 prepareForRebootCalls = 0;
    std::vector<U32> tlmWatchdogTransitions;
    U32 eventsWatchdogStart = 0;
    U32 eventsWatchdogStop = 0;
    std::vector<CmdResponseRecord> cmdResponses;

  protected:
    // ---- output ports ----
    void gpioSet_out(FwIndexType portNum, const Fw::Logic& logic) {
        (void)portNum;
        this->gpioSetCalls.push_back(logic.e);
    }

    void prepareForReboot_out(FwIndexType portNum) {
        (void)portNum;
        this->prepareForRebootCalls++;
    }

    bool isConnected_faultOut_OutputPort(FwIndexType portNum) const {
        (void)portNum;
        return this->faultOutConnected;
    }

    Components::FaultDisposition faultOut_out(FwIndexType portNum,
                                              const Components::FaultType& faultType,
                                              const Components::FaultSource& source,
                                              const Components::FaultSeverity& severity,
                                              F32 value) {
        (void)portNum;
        this->faultOutCalls.push_back(FaultReportRecord{faultType.e, source.e, severity.e, value});
        return this->faultOutDisposition;
    }

    // ---- telemetry ----
    void tlmWrite_WatchdogTransitions(U32 transitions) { this->tlmWatchdogTransitions.push_back(transitions); }

    // ---- events ----
    void log_ACTIVITY_HI_WatchdogStart() { this->eventsWatchdogStart++; }
    void log_ACTIVITY_HI_WatchdogStop() { this->eventsWatchdogStop++; }

    void cmdResponse_out(FwOpcodeType opCode, U32 cmdSeq, const Fw::CmdResponse& response) {
        this->cmdResponses.push_back(CmdResponseRecord{opCode, cmdSeq, response});
    }
};

}  // namespace Components

#endif
