// ==============================================================================
// Global Controls and Band Count Tests
// ==============================================================================
// Constitution Principle VIII: Testing Discipline
//
// Band Count parameter (D-123, fix plan T5.03): the StringListParameter the
// controller registers, ["1", "2", "3", "4"], list index i at normalized i / 3,
// and the band count the processor decodes from that normalized value.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "../test_support/disrumpo_rig.h"

#include "controller/controller.h"
#include "plugin_ids.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace Disrumpo;

// ==============================================================================
// Test: Band Count list entry -> band count, controller and processor
// ==============================================================================
TEST_CASE("Band Count list entry i is i + 1 bands in the controller and the processor", "[global][bandcount][D-123]") {
    Controller c;
    REQUIRE(c.initialize(nullptr) == Steinberg::kResultOk);
    const auto bandCountId = makeGlobalParamId(GlobalParamType::kGlobalBandCount);
    Steinberg::Vst::ParameterInfo info{};
    REQUIRE(c.getParameterInfoByTag(bandCountId, info) == Steinberg::kResultTrue);
    REQUIRE(info.stepCount == kMaxBands - 1);

    int wrong = 0;
    for (int index = 0; index <= info.stepCount; ++index) {
        const double norm = static_cast<double>(index) / static_cast<double>(info.stepCount);

        // Controller: the entry's host text is the band count
        Steinberg::Vst::String128 text{};
        REQUIRE(c.getParamStringByValue(bandCountId, norm, text) == Steinberg::kResultOk);
        std::string shown;
        for (int k = 0; k < 128 && text[k] != 0; ++k) shown.push_back(static_cast<char>(text[k]));

        // Processor: the band count it runs (saved right after the band count field)
        TestSupport::Rig rig(48000.0);
        rig.apply({{bandCountId, norm}});
        const auto state = TestSupport::getStateBytes(*rig.p);
        REQUIRE(state.size() >= size_t{20});
        int32_t bands = 0;
        std::memcpy(&bands, state.data() + 16, sizeof(bands));  // version, 3 globals, band count

        std::printf("D-123 Band Count entry %d (norm %.4f): host text \"%s\", processor %d bands\n", index, norm,
                    shown.c_str(), bands);
        if (shown != std::to_string(index + 1) || bands != index + 1) ++wrong;
    }
    CHECK(wrong == 0);
    c.terminate();
}

// ==============================================================================
// Test: Band Count Parameter ID
// ==============================================================================
TEST_CASE("Band count parameter ID is correctly encoded", "[global][bandcount]") {
    SECTION("Band count parameter ID is in global range (0x0F00)") {
        auto paramId = makeGlobalParamId(GlobalParamType::kGlobalBandCount);
        REQUIRE(paramId == 0x0F03);
        REQUIRE(paramId == 3843);
    }

    SECTION("Band count parameter is identified as global") {
        auto paramId = makeGlobalParamId(GlobalParamType::kGlobalBandCount);
        REQUIRE(isGlobalParamId(paramId) == true);
        REQUIRE(isBandParamId(paramId) == false);
        REQUIRE(isNodeParamId(paramId) == false);
        REQUIRE(isSweepParamId(paramId) == false);
    }
}

// ==============================================================================
// Test: Global Control Parameter IDs
// ==============================================================================
TEST_CASE("Global control parameter IDs are correctly encoded", "[global][encoding]") {
    SECTION("Input Gain ID") {
        auto paramId = makeGlobalParamId(GlobalParamType::kGlobalInputGain);
        REQUIRE(paramId == 0x0F00);
        REQUIRE(paramId == 3840);
    }

    SECTION("Output Gain ID") {
        auto paramId = makeGlobalParamId(GlobalParamType::kGlobalOutputGain);
        REQUIRE(paramId == 0x0F01);
        REQUIRE(paramId == 3841);
    }

    SECTION("Mix ID") {
        auto paramId = makeGlobalParamId(GlobalParamType::kGlobalMix);
        REQUIRE(paramId == 0x0F02);
        REQUIRE(paramId == 3842);
    }

    SECTION("Band Count ID") {
        auto paramId = makeGlobalParamId(GlobalParamType::kGlobalBandCount);
        REQUIRE(paramId == 0x0F03);
        REQUIRE(paramId == 3843);
    }

    SECTION("Oversample Max ID") {
        auto paramId = makeGlobalParamId(GlobalParamType::kGlobalOversample);
        REQUIRE(paramId == 0x0F04);
        REQUIRE(paramId == 3844);
    }
}
