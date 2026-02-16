// ==============================================================================
// Layer 0: Core Tests - Modulation Types
// ==============================================================================
// Tests for modulation type enumerations and value types.
//
// Reference: specs/008-modulation-system/spec.md (FR-001 to FR-003)
// ==============================================================================

#include <krate/dsp/core/modulation_types.h>

#include <catch2/catch_test_macros.hpp>

using namespace Krate::DSP;

// =============================================================================
// ModSource Enum Tests
// =============================================================================

TEST_CASE("ModSource enum has 14 values", "[core][modulation_types]") {
    REQUIRE(kModSourceCount == 14);
    REQUIRE(static_cast<uint8_t>(ModSource::None) == 0);
    REQUIRE(static_cast<uint8_t>(ModSource::LFO1) == 1);
    REQUIRE(static_cast<uint8_t>(ModSource::LFO2) == 2);
    REQUIRE(static_cast<uint8_t>(ModSource::EnvFollower) == 3);
    REQUIRE(static_cast<uint8_t>(ModSource::Random) == 4);
    REQUIRE(static_cast<uint8_t>(ModSource::Macro1) == 5);
    REQUIRE(static_cast<uint8_t>(ModSource::Macro2) == 6);
    REQUIRE(static_cast<uint8_t>(ModSource::Macro3) == 7);
    REQUIRE(static_cast<uint8_t>(ModSource::Macro4) == 8);
    REQUIRE(static_cast<uint8_t>(ModSource::Chaos) == 9);
    REQUIRE(static_cast<uint8_t>(ModSource::Rungler) == 10);
    REQUIRE(static_cast<uint8_t>(ModSource::SampleHold) == 11);
    REQUIRE(static_cast<uint8_t>(ModSource::PitchFollower) == 12);
    REQUIRE(static_cast<uint8_t>(ModSource::Transient) == 13);
}

// =============================================================================
// ModCurve Enum Tests
// =============================================================================

TEST_CASE("ModCurve enum has 4 values", "[core][modulation_types]") {
    REQUIRE(kModCurveCount == 4);
    REQUIRE(static_cast<uint8_t>(ModCurve::Linear) == 0);
    REQUIRE(static_cast<uint8_t>(ModCurve::Exponential) == 1);
    REQUIRE(static_cast<uint8_t>(ModCurve::SCurve) == 2);
    REQUIRE(static_cast<uint8_t>(ModCurve::Stepped) == 3);
}

// =============================================================================
// ModRouting Struct Tests
// =============================================================================

TEST_CASE("ModRouting default construction", "[core][modulation_types]") {
    ModRouting routing;

    REQUIRE(routing.source == ModSource::None);
    REQUIRE(routing.destParamId == 0);
    REQUIRE(routing.amount == 0.0f);
    REQUIRE(routing.curve == ModCurve::Linear);
    REQUIRE(routing.active == false);
}

TEST_CASE("ModRouting can be configured", "[core][modulation_types]") {
    ModRouting routing;
    routing.source = ModSource::LFO1;
    routing.destParamId = 42;
    routing.amount = 0.75f;
    routing.curve = ModCurve::Exponential;
    routing.active = true;

    REQUIRE(routing.source == ModSource::LFO1);
    REQUIRE(routing.destParamId == 42);
    REQUIRE(routing.amount == 0.75f);
    REQUIRE(routing.curve == ModCurve::Exponential);
    REQUIRE(routing.active == true);
}

TEST_CASE("ModRouting amount supports bipolar range", "[core][modulation_types]") {
    ModRouting routing;

    SECTION("positive amount") {
        routing.amount = 1.0f;
        REQUIRE(routing.amount == 1.0f);
    }

    SECTION("negative amount") {
        routing.amount = -1.0f;
        REQUIRE(routing.amount == -1.0f);
    }

    SECTION("zero amount") {
        routing.amount = 0.0f;
        REQUIRE(routing.amount == 0.0f);
    }
}

TEST_CASE("kMaxModRoutings is 32", "[core][modulation_types]") {
    REQUIRE(kMaxModRoutings == 32);
}

// =============================================================================
// MacroConfig Struct Tests
// =============================================================================

TEST_CASE("MacroConfig default construction", "[core][modulation_types]") {
    MacroConfig macro;

    REQUIRE(macro.value == 0.0f);
    REQUIRE(macro.minOutput == 0.0f);
    REQUIRE(macro.maxOutput == 1.0f);
    REQUIRE(macro.curve == ModCurve::Linear);
}

TEST_CASE("kMaxMacros is 4", "[core][modulation_types]") {
    REQUIRE(kMaxMacros == 4);
}

// =============================================================================
// EnvFollowerSourceType Enum Tests
// =============================================================================

TEST_CASE("EnvFollowerSourceType has 5 values", "[core][modulation_types]") {
    REQUIRE(static_cast<uint8_t>(EnvFollowerSourceType::InputL) == 0);
    REQUIRE(static_cast<uint8_t>(EnvFollowerSourceType::InputR) == 1);
    REQUIRE(static_cast<uint8_t>(EnvFollowerSourceType::InputSum) == 2);
    REQUIRE(static_cast<uint8_t>(EnvFollowerSourceType::Mid) == 3);
    REQUIRE(static_cast<uint8_t>(EnvFollowerSourceType::Side) == 4);
}

// =============================================================================
// SampleHoldInputType Enum Tests
// =============================================================================

TEST_CASE("SampleHoldInputType has 4 values", "[core][modulation_types]") {
    REQUIRE(static_cast<uint8_t>(SampleHoldInputType::Random) == 0);
    REQUIRE(static_cast<uint8_t>(SampleHoldInputType::LFO1) == 1);
    REQUIRE(static_cast<uint8_t>(SampleHoldInputType::LFO2) == 2);
    REQUIRE(static_cast<uint8_t>(SampleHoldInputType::External) == 3);
}
