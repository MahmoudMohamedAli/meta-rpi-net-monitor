#include "link_fsm.hpp"
#include <gtest/gtest.h>

using namespace netmon;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static LinkFSM make_fsm(const std::string& iface = "eth0") {
    return LinkFSM(iface);
}

// ---------------------------------------------------------------------------
// Initial state
// ---------------------------------------------------------------------------
TEST(LinkFSM, InitialStateIsUnknown) {
    auto fsm = make_fsm();
    EXPECT_EQ(fsm.state(), LinkState::UNKNOWN);
}

// ---------------------------------------------------------------------------
// UNKNOWN transitions
// ---------------------------------------------------------------------------
TEST(LinkFSM, UnknownCarrierUp_GoesUp) {
    auto fsm = make_fsm();
    EXPECT_TRUE(fsm.process(LinkEvent::CARRIER_UP));
    EXPECT_EQ(fsm.state(), LinkState::UP);
}

TEST(LinkFSM, UnknownCarrierDown_GoesDown) {
    auto fsm = make_fsm();
    EXPECT_TRUE(fsm.process(LinkEvent::CARRIER_DOWN));
    EXPECT_EQ(fsm.state(), LinkState::DOWN);
}

// ---------------------------------------------------------------------------
// UP transitions
// ---------------------------------------------------------------------------
TEST(LinkFSM, UpCarrierDown_GoesDown) {
    auto fsm = make_fsm();
    fsm.process(LinkEvent::CARRIER_UP);
    EXPECT_TRUE(fsm.process(LinkEvent::CARRIER_DOWN));
    EXPECT_EQ(fsm.state(), LinkState::DOWN);
}

TEST(LinkFSM, UpErrorRateHigh_GoesDegraded) {
    auto fsm = make_fsm();
    fsm.process(LinkEvent::CARRIER_UP);
    EXPECT_TRUE(fsm.process(LinkEvent::ERROR_RATE_HIGH));
    EXPECT_EQ(fsm.state(), LinkState::DEGRADED);
}

TEST(LinkFSM, UpStableEvent_NoChange) {
    auto fsm = make_fsm();
    fsm.process(LinkEvent::CARRIER_UP);
    EXPECT_FALSE(fsm.process(LinkEvent::ERROR_RATE_OK));
    EXPECT_EQ(fsm.state(), LinkState::UP);
}

// ---------------------------------------------------------------------------
// DEGRADED transitions
// ---------------------------------------------------------------------------
TEST(LinkFSM, DegradedErrorOk_GoesUp) {
    auto fsm = make_fsm();
    fsm.process(LinkEvent::CARRIER_UP);
    fsm.process(LinkEvent::ERROR_RATE_HIGH);
    EXPECT_TRUE(fsm.process(LinkEvent::ERROR_RATE_OK));
    EXPECT_EQ(fsm.state(), LinkState::UP);
}

TEST(LinkFSM, DegradedCarrierDown_GoesDown) {
    auto fsm = make_fsm();
    fsm.process(LinkEvent::CARRIER_UP);
    fsm.process(LinkEvent::ERROR_RATE_HIGH);
    EXPECT_TRUE(fsm.process(LinkEvent::CARRIER_DOWN));
    EXPECT_EQ(fsm.state(), LinkState::DOWN);
}

// ---------------------------------------------------------------------------
// DOWN → RECOVERING → UP path
// ---------------------------------------------------------------------------
TEST(LinkFSM, DownCarrierUp_GoesRecovering) {
    auto fsm = make_fsm();
    fsm.process(LinkEvent::CARRIER_DOWN);
    EXPECT_TRUE(fsm.process(LinkEvent::CARRIER_UP));
    EXPECT_EQ(fsm.state(), LinkState::RECOVERING);
}

TEST(LinkFSM, RecoveringStableTimeout_GoesUp) {
    auto fsm = make_fsm();
    fsm.process(LinkEvent::CARRIER_DOWN);
    fsm.process(LinkEvent::CARRIER_UP);
    EXPECT_TRUE(fsm.process(LinkEvent::STABLE_TIMEOUT));
    EXPECT_EQ(fsm.state(), LinkState::UP);
}

TEST(LinkFSM, RecoveringCarrierDown_GoesDown) {
    auto fsm = make_fsm();
    fsm.process(LinkEvent::CARRIER_DOWN);
    fsm.process(LinkEvent::CARRIER_UP);
    EXPECT_TRUE(fsm.process(LinkEvent::CARRIER_DOWN));
    EXPECT_EQ(fsm.state(), LinkState::DOWN);
}

TEST(LinkFSM, RecoveringHighError_GoesDegraded) {
    auto fsm = make_fsm();
    fsm.process(LinkEvent::CARRIER_DOWN);
    fsm.process(LinkEvent::CARRIER_UP);
    EXPECT_TRUE(fsm.process(LinkEvent::ERROR_RATE_HIGH));
    EXPECT_EQ(fsm.state(), LinkState::DEGRADED);
}

// ---------------------------------------------------------------------------
// Callback fires on transition
// ---------------------------------------------------------------------------
TEST(LinkFSM, CallbackFiredOnTransition) {
    int call_count = 0;
    LinkState last_from = LinkState::UNKNOWN;
    LinkState last_to   = LinkState::UNKNOWN;

    LinkFSM fsm("eth0", [&](const std::string&,
                              LinkState from,
                              LinkState to,
                              LinkEvent) {
        ++call_count;
        last_from = from;
        last_to   = to;
    });

    fsm.process(LinkEvent::CARRIER_UP);
    EXPECT_EQ(call_count, 1);
    EXPECT_EQ(last_from, LinkState::UNKNOWN);
    EXPECT_EQ(last_to,   LinkState::UP);

    fsm.process(LinkEvent::CARRIER_UP);  // no transition — already UP
    EXPECT_EQ(call_count, 1);
}

// ---------------------------------------------------------------------------
// Full DOWN → RECOVERING → DOWN → RECOVERING → UP cycle
// ---------------------------------------------------------------------------
TEST(LinkFSM, FullRecoveryCycle) {
    auto fsm = make_fsm();
    fsm.process(LinkEvent::CARRIER_UP);
    fsm.process(LinkEvent::CARRIER_DOWN);
    fsm.process(LinkEvent::CARRIER_UP);          // RECOVERING
    fsm.process(LinkEvent::CARRIER_DOWN);         // DOWN again
    fsm.process(LinkEvent::CARRIER_UP);           // RECOVERING again
    fsm.process(LinkEvent::STABLE_TIMEOUT);       // UP
    EXPECT_EQ(fsm.state(), LinkState::UP);
}
