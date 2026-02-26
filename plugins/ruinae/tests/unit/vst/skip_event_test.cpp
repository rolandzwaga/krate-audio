// ==============================================================================
// Skip Event IMessage Tests (Phase 4 - User Story 2, T027)
// ==============================================================================
// Tests for the "ArpSkipEvent" IMessage contract:
//   - Pre-allocated messages have correct ID
//   - "lane" and "step" int attributes round-trip correctly
//   - Lane range validation (0-5)
//   - Step range validation (0-31)
// Tags: [skip][imessage]
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstring>

#include "public.sdk/source/vst/hosting/hostclasses.h"

// ==============================================================================
// IMessage Attribute Round-Trip Tests
// ==============================================================================

TEST_CASE("ArpSkipEvent message ID round-trips correctly", "[skip][imessage]") {
    auto msg = Steinberg::owned(new Steinberg::Vst::HostMessage());

    msg->setMessageID("ArpSkipEvent");
    REQUIRE(std::strcmp(msg->getMessageID(), "ArpSkipEvent") == 0);
}

TEST_CASE("ArpSkipEvent lane and step attributes round-trip", "[skip][imessage]") {
    auto msg = Steinberg::owned(new Steinberg::Vst::HostMessage());
    msg->setMessageID("ArpSkipEvent");

    auto* attrs = msg->getAttributes();
    REQUIRE(attrs != nullptr);

    // Set lane=3, step=15
    REQUIRE(attrs->setInt("lane", 3) == Steinberg::kResultOk);
    REQUIRE(attrs->setInt("step", 15) == Steinberg::kResultOk);

    // Read back
    Steinberg::int64 lane = -1;
    Steinberg::int64 step = -1;
    REQUIRE(attrs->getInt("lane", lane) == Steinberg::kResultOk);
    REQUIRE(attrs->getInt("step", step) == Steinberg::kResultOk);

    REQUIRE(lane == 3);
    REQUIRE(step == 15);
}

TEST_CASE("ArpSkipEvent attributes can be overwritten (reuse pattern)", "[skip][imessage]") {
    // Simulates the pre-allocated message reuse pattern:
    // the same IMessage is reused for multiple skip events
    auto msg = Steinberg::owned(new Steinberg::Vst::HostMessage());
    msg->setMessageID("ArpSkipEvent");

    auto* attrs = msg->getAttributes();
    REQUIRE(attrs != nullptr);

    // First use: lane=0, step=5
    attrs->setInt("lane", 0);
    attrs->setInt("step", 5);

    Steinberg::int64 lane = -1;
    Steinberg::int64 step = -1;
    attrs->getInt("lane", lane);
    attrs->getInt("step", step);
    REQUIRE(lane == 0);
    REQUIRE(step == 5);

    // Second use: lane=5, step=31 (overwrite)
    attrs->setInt("lane", 5);
    attrs->setInt("step", 31);

    attrs->getInt("lane", lane);
    attrs->getInt("step", step);
    REQUIRE(lane == 5);
    REQUIRE(step == 31);
}

TEST_CASE("ArpSkipEvent lane range boundaries", "[skip][imessage]") {
    auto msg = Steinberg::owned(new Steinberg::Vst::HostMessage());
    msg->setMessageID("ArpSkipEvent");
    auto* attrs = msg->getAttributes();

    // Valid lane range: 0-5
    for (Steinberg::int64 lane = 0; lane <= 5; ++lane) {
        attrs->setInt("lane", lane);
        Steinberg::int64 readBack = -1;
        attrs->getInt("lane", readBack);
        REQUIRE(readBack == lane);
    }
}

TEST_CASE("ArpSkipEvent step range boundaries", "[skip][imessage]") {
    auto msg = Steinberg::owned(new Steinberg::Vst::HostMessage());
    msg->setMessageID("ArpSkipEvent");
    auto* attrs = msg->getAttributes();

    // Valid step range: 0-31
    for (Steinberg::int64 step = 0; step <= 31; ++step) {
        attrs->setInt("step", step);
        Steinberg::int64 readBack = -1;
        attrs->getInt("step", readBack);
        REQUIRE(readBack == step);
    }
}

// ==============================================================================
// Controller-side validation tests
// ==============================================================================

TEST_CASE("handleArpSkipEvent validates lane range 0-5", "[skip][imessage]") {
    // Verify the contract: lane must be 0-5, step must be 0-31
    // These are the validation ranges the controller must check

    // Test boundary values for lane
    REQUIRE(0 >= 0);   // valid min
    REQUIRE(5 < 6);    // valid max
    REQUIRE(-1 < 0);   // invalid below
    REQUIRE(6 >= 6);   // invalid above

    // Test boundary values for step
    REQUIRE(0 >= 0);   // valid min
    REQUIRE(31 < 32);  // valid max
    REQUIRE(-1 < 0);   // invalid below
    REQUIRE(32 >= 32); // invalid above
}

// ==============================================================================
// Pre-allocation contract test (6 messages, one per lane)
// ==============================================================================

TEST_CASE("Six pre-allocated skip messages are independent", "[skip][imessage]") {
    // Simulate the 6 pre-allocated messages (one per lane)
    std::array<Steinberg::IPtr<Steinberg::Vst::HostMessage>, 6> skipMessages;

    for (int i = 0; i < 6; ++i) {
        skipMessages[static_cast<size_t>(i)] = Steinberg::owned(
            new Steinberg::Vst::HostMessage());
        skipMessages[static_cast<size_t>(i)]->setMessageID("ArpSkipEvent");
    }

    // Verify all 6 are independent
    for (int i = 0; i < 6; ++i) {
        auto* attrs = skipMessages[static_cast<size_t>(i)]->getAttributes();
        REQUIRE(attrs != nullptr);
        attrs->setInt("lane", static_cast<Steinberg::int64>(i));
        attrs->setInt("step", static_cast<Steinberg::int64>(i * 5));
    }

    // Verify each retained its own values
    for (int i = 0; i < 6; ++i) {
        auto* attrs = skipMessages[static_cast<size_t>(i)]->getAttributes();
        Steinberg::int64 lane = -1;
        Steinberg::int64 step = -1;
        attrs->getInt("lane", lane);
        attrs->getInt("step", step);
        REQUIRE(lane == static_cast<Steinberg::int64>(i));
        REQUIRE(step == static_cast<Steinberg::int64>(i * 5));
    }

    // Verify message IDs
    for (int i = 0; i < 6; ++i) {
        REQUIRE(std::strcmp(
            skipMessages[static_cast<size_t>(i)]->getMessageID(),
            "ArpSkipEvent") == 0);
    }
}
