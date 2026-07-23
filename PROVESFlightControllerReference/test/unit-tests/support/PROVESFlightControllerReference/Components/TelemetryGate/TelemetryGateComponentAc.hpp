// ======================================================================
// \title  TelemetryGateComponentAc.hpp (host-test stub)
// \brief  Stand-in for the fpp-generated TelemetryGate component base.
//
// Declares the exact base-class surface TelemetryGate.cpp/.hpp use, and
// records every outgoing effect (runOut port calls, telemetry writes, events,
// command responses) into public members so host tests can assert on them —
// the same role the autocoded TesterBase plays in a full F Prime UT build.
// Handlers are public virtuals here so tests can invoke the component's
// private overrides through a base reference, as real autocode does.
// ======================================================================

#ifndef UnitTestSupport_TelemetryGateComponentAc_HPP
#define UnitTestSupport_TelemetryGateComponentAc_HPP

#include <string>
#include <vector>

#include "../../../FpTypesStub.hpp"

namespace Components {

//! Mirrors the generated shape of the FPP enum: values addressed as
//! TelemetryTxState::ENABLED / ::DISABLED, copyable, equality-comparable.
class TelemetryTxState {
  public:
    enum T { ENABLED = 0, DISABLED = 1 };
    TelemetryTxState() : m_value(ENABLED) {}
    TelemetryTxState(T value) : m_value(value) {}
    bool operator==(const TelemetryTxState& other) const { return this->m_value == other.m_value; }
    bool operator==(T value) const { return this->m_value == value; }
    bool operator!=(const TelemetryTxState& other) const { return !(*this == other); }
    T value() const { return this->m_value; }

  private:
    T m_value;
};

class TelemetryGateComponentBase {
  public:
    struct CmdResponseRecord {
        FwOpcodeType opCode;
        U32 cmdSeq;
        Fw::CmdResponse response;
    };

    explicit TelemetryGateComponentBase(const char* const compName) : compName(compName) {}
    virtual ~TelemetryGateComponentBase() {}

    // ---- handlers implemented by the component (private overrides there) ----
    virtual void runIn_handler(FwIndexType portNum, U32 context) = 0;
    virtual void SET_TRANSMIT_STATE_cmdHandler(FwOpcodeType opCode,
                                               U32 cmdSeq,
                                               Components::TelemetryTxState txState) = 0;

    // ---- recorded outgoing effects, public for test inspection ----
    std::string compName;
    std::vector<U32> runOutCalls;                          //!< contexts forwarded out runOut
    std::vector<TelemetryTxState> tlmTransmitState;        //!< tlmWrite_TransmitState history
    std::vector<U32> tlmGatedTicks;                        //!< tlmWrite_GatedTicks history
    std::vector<TelemetryTxState> eventsTransmitStateSet;  //!< TransmitStateSet event args
    U32 eventsStateFileWriteFailure = 0;
    U32 eventsStateFileCorrupt = 0;
    std::vector<CmdResponseRecord> cmdResponses;

  protected:
    // ---- base-class services the component implementation calls ----
    void runOut_out(FwIndexType portNum, U32 context) {
        (void)portNum;
        this->runOutCalls.push_back(context);
    }

    void tlmWrite_TransmitState(const TelemetryTxState& state) { this->tlmTransmitState.push_back(state); }

    void tlmWrite_GatedTicks(U32 ticks) { this->tlmGatedTicks.push_back(ticks); }

    void log_ACTIVITY_HI_TransmitStateSet(const TelemetryTxState& state) {
        this->eventsTransmitStateSet.push_back(state);
    }

    void log_WARNING_HI_StateFileWriteFailure() { this->eventsStateFileWriteFailure++; }

    void log_WARNING_HI_StateFileCorrupt() { this->eventsStateFileCorrupt++; }

    void cmdResponse_out(FwOpcodeType opCode, U32 cmdSeq, const Fw::CmdResponse& response) {
        this->cmdResponses.push_back(CmdResponseRecord{opCode, cmdSeq, response});
    }
};

}  // namespace Components

#endif
