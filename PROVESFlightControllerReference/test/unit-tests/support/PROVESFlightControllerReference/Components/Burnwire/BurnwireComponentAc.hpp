// ======================================================================
// \title  BurnwireComponentAc.hpp (host-test stub)
// \brief  Stand-in for the fpp-generated Burnwire component base.
//
// Declares the base-class surface a Burnwire implementation can use, and
// records every outgoing effect into public members so host tests can assert
// on them — the same role the autocoded TesterBase plays in a full F Prime UT
// build.
//
// Interface mirrors Burnwire.fpp: commands START_BURNWIRE / STOP_BURNWIRE,
// input ports burnStart / burnStop (Fw.Signal) and schedIn (Svc.Sched),
// output port gpioSet: [2] Drv.GpioWrite (GpioDriverPorts.fpp:12-14,
// `port GpioWrite($state: Fw.Logic) -> GpioStatus`), events SetBurnwireState,
// SafetyTimerStatus, SafetyTimerState, BurnwireEndCount, parameter
// SAFETY_TIMER: U32 default 10.
//
// Unconnected ports: in real autocode, invoking an output port that is not
// connected fails an FW_ASSERT inside the generated *_out wrapper. Here each
// gpioSet port has a test-controlled `gpioSetConnected[i]` flag (both true by
// default, the deployed `burnwire` wiring). An invocation of a port whose flag
// is false is NOT recorded as a write; it is recorded in
// `unconnectedGpioSetInvocations` instead, so a test can fail cleanly on it
// rather than abort the test binary.
// ======================================================================

#ifndef UnitTestSupport_BurnwireComponentAc_HPP
#define UnitTestSupport_BurnwireComponentAc_HPP

#include <string>
#include <vector>

#include "../../../FpTypesStub.hpp"
#include "../../../Fw/Types/OnEnumAc.hpp"

namespace Components {

class BurnwireComponentBase {
  public:
    explicit BurnwireComponentBase(const char* const compName) : compName(compName) {}
    virtual ~BurnwireComponentBase() {}

    //! One recorded write on a connected gpioSet port.
    struct GpioWriteRecord {
        FwIndexType port;
        Fw::Logic::T state;
    };

    //! One recorded command response.
    struct CmdResponseRecord {
        FwOpcodeType opCode;
        U32 cmdSeq;
        Fw::CmdResponse::T response;
    };

    // ---- port counts (Burnwire.fpp "output port gpioSet: [2]") ----
    static constexpr FwIndexType getNum_gpioSet_OutputPorts() { return 2; }

    // ---- handlers implemented by the component (private overrides there);
    //      public here so a test drives them as the framework does ----
    virtual void burnStart_handler(FwIndexType portNum) = 0;
    virtual void burnStop_handler(FwIndexType portNum) = 0;
    virtual void schedIn_handler(FwIndexType portNum, U32 context) = 0;
    virtual void START_BURNWIRE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) = 0;
    virtual void STOP_BURNWIRE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) = 0;

    // ---- test-controlled wiring and parameter ----
    bool gpioSetConnected[2] = {true, true};
    Drv::GpioStatus gpioSetReturn = Drv::GpioStatus::OP_OK;
    U32 safetyTimer = 10;  // Burnwire.fpp "param SAFETY_TIMER: U32 default 10"
    Fw::ParamValid paramValidity = Fw::ParamValid::VALID;

    // ---- recorded outgoing effects, public for test inspection ----
    std::string compName;
    std::vector<GpioWriteRecord> gpioWrites;
    std::vector<FwIndexType> unconnectedGpioSetInvocations;
    std::vector<Fw::On::T> eventsSetBurnwireState;
    std::vector<Fw::On::T> eventsSafetyTimerStatus;
    std::vector<U32> eventsSafetyTimerState;
    std::vector<U32> eventsBurnwireEndCount;
    std::vector<CmdResponseRecord> cmdResponses;

  protected:
    // ---- base-class services the component implementation calls ----
    bool isConnected_gpioSet_OutputPort(FwIndexType portNum) const {
        if (portNum < 0 || portNum >= getNum_gpioSet_OutputPorts()) {
            return false;
        }
        return this->gpioSetConnected[portNum];
    }

    Drv::GpioStatus gpioSet_out(FwIndexType portNum, const Fw::Logic& state) {
        if (!this->isConnected_gpioSet_OutputPort(portNum)) {
            // Real autocode: FW_ASSERT on an unconnected port.
            this->unconnectedGpioSetInvocations.push_back(portNum);
            return Drv::GpioStatus::NOT_OPENED;
        }
        this->gpioWrites.push_back(GpioWriteRecord{portNum, state.e});
        return this->gpioSetReturn;
    }

    U32 paramGet_SAFETY_TIMER(Fw::ParamValid& valid) {
        valid = this->paramValidity;
        return this->safetyTimer;
    }

    void log_ACTIVITY_HI_SetBurnwireState(const Fw::On& burnwire_state) {
        this->eventsSetBurnwireState.push_back(burnwire_state.e);
    }

    void log_ACTIVITY_HI_SafetyTimerStatus(const Fw::On& burnwire_state) {
        this->eventsSafetyTimerStatus.push_back(burnwire_state.e);
    }

    void log_ACTIVITY_HI_SafetyTimerState(U32 burnwire_status) {
        this->eventsSafetyTimerState.push_back(burnwire_status);
    }

    void log_ACTIVITY_LO_BurnwireEndCount(U32 end_count) { this->eventsBurnwireEndCount.push_back(end_count); }

    void cmdResponse_out(FwOpcodeType opCode, U32 cmdSeq, Fw::CmdResponse response) {
        this->cmdResponses.push_back(CmdResponseRecord{opCode, cmdSeq, response.value()});
    }
};

}  // namespace Components

#endif
