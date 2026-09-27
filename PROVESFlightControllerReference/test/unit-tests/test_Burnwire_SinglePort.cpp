// ======================================================================
// \title  test_Burnwire_SinglePort.cpp
// \brief  Host unit tests for a Burnwire instance wired to one GPIO only
//         (burnwireDeploy2) and the unchanged two-GPIO behaviour (burnwire).
//
// Level: Unit. Runs on the host against the recorder stub in
// support/PROVESFlightControllerReference/Components/Burnwire/; no F Prime or
// Zephyr code is linked (test/unit-tests/README.md).
//
// Requirement verified: BW-008.
//   BW-008 pass criteria: "Host stub, SAFETY_TIMER 10: with only gpioSet[0]
//     connected, START_BURNWIRE emits SetBurnwireState(ON) and responds OK;
//     the first schedIn tick writes HIGH on port 0; STOP_BURNWIRE, and
//     separately 10 ticks without STOP (safety timer), write LOW on port 0 and
//     emit SetBurnwireState(OFF); gpioSet[1] is invoked zero times throughout.
//     With both ports connected the writes are exactly (0,HIGH),(1,HIGH) on the
//     first tick and (0,LOW),(1,LOW) on STOP_BURNWIRE or on safety-timer
//     expiry".
//
// Oracle (TP-3): the expected writes come from the criterion above (Cycle L
// 01-normative.md R3.1 and its harm-table pin); SAFETY_TIMER = 10 is the
// Burnwire.fpp default that R3.2 keeps. An invocation of an unconnected port —
// an FW_ASSERT in real autocode — is recorded by the stub in
// unconnectedGpioSetInvocations and must stay empty.
// ======================================================================

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "PROVESFlightControllerReference/Components/Burnwire/Burnwire.hpp"

namespace {

using Components::Burnwire;
using Components::BurnwireComponentBase;
using Write = BurnwireComponentBase::GpioWriteRecord;

// Burnwire.fpp "param SAFETY_TIMER: U32 default 10", unchanged by R3.2.
constexpr U32 SAFETY_TIMER_TICKS = 10;

constexpr FwOpcodeType OPCODE_START = 0x10;
constexpr FwOpcodeType OPCODE_STOP = 0x11;

bool sameWrites(const std::vector<Write>& actual, const std::vector<Write>& expected) {
    if (actual.size() != expected.size()) {
        return false;
    }
    for (size_t i = 0; i < actual.size(); ++i) {
        if (actual[i].port != expected[i].port || actual[i].state != expected[i].state) {
            return false;
        }
    }
    return true;
}

std::string describe(const std::vector<Write>& writes) {
    std::string out;
    for (const Write& w : writes) {
        out += "(" + std::to_string(w.port) + "," + (w.state == Fw::Logic::HIGH ? "HIGH" : "LOW") + ")";
    }
    return out.empty() ? "<none>" : out;
}

class BurnwireTest : public ::testing::Test {
  protected:
    BurnwireTest() : burn("burnwire") {}

    BurnwireComponentBase& base() { return static_cast<BurnwireComponentBase&>(this->burn); }

    void onlyPortZeroConnected() {
        this->burn.gpioSetConnected[0] = true;
        this->burn.gpioSetConnected[1] = false;
    }

    void start() { this->base().START_BURNWIRE_cmdHandler(OPCODE_START, 1); }
    void stop() { this->base().STOP_BURNWIRE_cmdHandler(OPCODE_STOP, 2); }

    void tick(U32 count) {
        for (U32 i = 0; i < count; ++i) {
            this->base().schedIn_handler(0, 0);
        }
    }

    Burnwire burn;
};

// ----------------------------------------------------------------------
// Only gpioSet[0] connected (the burnwireDeploy2 wiring)
// ----------------------------------------------------------------------

TEST_F(BurnwireTest, SinglePortStartThenStopDrivesPortZeroOnly) {
    RecordProperty("verifies", "BW-008");
    this->onlyPortZeroConnected();

    this->start();
    ASSERT_FALSE(this->burn.eventsSetBurnwireState.empty()) << "START_BURNWIRE must emit SetBurnwireState";
    EXPECT_EQ(this->burn.eventsSetBurnwireState.back(), Fw::On::ON);
    ASSERT_EQ(this->burn.cmdResponses.size(), 1u);
    EXPECT_EQ(this->burn.cmdResponses[0].response, Fw::CmdResponse::OK);

    this->tick(1);
    EXPECT_TRUE(sameWrites(this->burn.gpioWrites, {{0, Fw::Logic::HIGH}}))
        << "First tick after START must write HIGH on port 0 only; got " << describe(this->burn.gpioWrites);

    this->stop();
    EXPECT_TRUE(sameWrites(this->burn.gpioWrites, {{0, Fw::Logic::HIGH}, {0, Fw::Logic::LOW}}))
        << "STOP_BURNWIRE must write LOW on port 0; got " << describe(this->burn.gpioWrites);
    EXPECT_EQ(this->burn.eventsSetBurnwireState.back(), Fw::On::OFF);
    ASSERT_EQ(this->burn.cmdResponses.size(), 2u);
    EXPECT_EQ(this->burn.cmdResponses[1].response, Fw::CmdResponse::OK);

    EXPECT_TRUE(this->burn.unconnectedGpioSetInvocations.empty())
        << "gpioSet[1] is unconnected and must never be invoked (an FW_ASSERT on the target); invoked "
        << this->burn.unconnectedGpioSetInvocations.size() << " time(s)";
}

TEST_F(BurnwireTest, SinglePortSafetyTimerExpiryDrivesPortZeroOnly) {
    RecordProperty("verifies", "BW-008");
    this->onlyPortZeroConnected();

    this->start();
    this->tick(SAFETY_TIMER_TICKS);

    EXPECT_TRUE(sameWrites(this->burn.gpioWrites, {{0, Fw::Logic::HIGH}, {0, Fw::Logic::LOW}}))
        << "Safety-timer expiry must write LOW on port 0 after the HIGH; got " << describe(this->burn.gpioWrites);
    ASSERT_FALSE(this->burn.eventsSetBurnwireState.empty());
    EXPECT_EQ(this->burn.eventsSetBurnwireState.front(), Fw::On::ON);
    EXPECT_EQ(this->burn.eventsSetBurnwireState.back(), Fw::On::OFF);

    // Further ticks after expiry drive nothing.
    this->tick(3);
    EXPECT_EQ(this->burn.gpioWrites.size(), 2u);

    EXPECT_TRUE(this->burn.unconnectedGpioSetInvocations.empty())
        << "gpioSet[1] is unconnected and must never be invoked (an FW_ASSERT on the target); invoked "
        << this->burn.unconnectedGpioSetInvocations.size() << " time(s)";
}

TEST_F(BurnwireTest, SinglePortStopWhileIdleNeverInvokesPortOne) {
    RecordProperty("verifies", "BW-008");
    this->onlyPortZeroConnected();

    // The board-test restore fixture sends STOP_BURNWIRE with nothing burning.
    this->stop();
    this->tick(2);

    EXPECT_TRUE(this->burn.unconnectedGpioSetInvocations.empty())
        << "gpioSet[1] is unconnected and must never be invoked (an FW_ASSERT on the target); invoked "
        << this->burn.unconnectedGpioSetInvocations.size() << " time(s)";
    for (const Write& w : this->burn.gpioWrites) {
        EXPECT_EQ(w.port, 0) << "Only port 0 may be written";
        EXPECT_EQ(w.state, Fw::Logic::LOW) << "Nothing may be driven HIGH without START";
    }
}

// ----------------------------------------------------------------------
// Both ports connected (the deployed `burnwire` wiring): harm-table pin
// ----------------------------------------------------------------------

TEST_F(BurnwireTest, BothPortsStartThenStopWriteOrderUnchanged) {
    RecordProperty("verifies", "BW-008");

    this->start();
    EXPECT_TRUE(this->burn.gpioWrites.empty())
        << "START itself writes nothing; got " << describe(this->burn.gpioWrites);

    this->tick(1);
    EXPECT_TRUE(sameWrites(this->burn.gpioWrites, {{0, Fw::Logic::HIGH}, {1, Fw::Logic::HIGH}}))
        << "First tick must write HIGH on 0 then 1; got " << describe(this->burn.gpioWrites);

    // Ticks before expiry add nothing.
    this->tick(2);
    EXPECT_EQ(this->burn.gpioWrites.size(), 2u);

    this->stop();
    EXPECT_TRUE(sameWrites(this->burn.gpioWrites,
                           {{0, Fw::Logic::HIGH}, {1, Fw::Logic::HIGH}, {0, Fw::Logic::LOW}, {1, Fw::Logic::LOW}}))
        << "STOP must write LOW on 0 then 1; got " << describe(this->burn.gpioWrites);
    EXPECT_TRUE(this->burn.unconnectedGpioSetInvocations.empty());
}

TEST_F(BurnwireTest, BothPortsSafetyTimerWriteOrderUnchanged) {
    RecordProperty("verifies", "BW-008");

    this->start();
    this->tick(SAFETY_TIMER_TICKS);

    EXPECT_TRUE(sameWrites(this->burn.gpioWrites,
                           {{0, Fw::Logic::HIGH}, {1, Fw::Logic::HIGH}, {0, Fw::Logic::LOW}, {1, Fw::Logic::LOW}}))
        << "Safety-timer expiry must write LOW on 0 then 1 after the HIGHs; got " << describe(this->burn.gpioWrites);
    EXPECT_EQ(this->burn.eventsSetBurnwireState.back(), Fw::On::OFF);
    EXPECT_TRUE(this->burn.unconnectedGpioSetInvocations.empty());
}

}  // namespace
