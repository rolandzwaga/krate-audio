// ==============================================================================
// Unit Test: Processor Bus Configuration
// ==============================================================================
// Verifies that Ruinae Processor configures bus layout correctly:
// - One optional sidechain (aux) audio input bus: mono or stereo
// - One stereo audio output bus
// - setBusArrangements rejects invalid configurations
//
// Reference: specs/045-plugin-shell/spec.md FR-003; T2.26 (R-192)
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"

#include "pluginterfaces/vst/vstspeaker.h"

TEST_CASE("Processor bus configuration", "[processor][bus]") {
    Ruinae::Processor processor;
    auto initResult = processor.initialize(nullptr);
    REQUIRE(initResult == Steinberg::kResultTrue);

    SECTION("Accepts zero inputs + stereo output") {
        Steinberg::Vst::SpeakerArrangement stereo = Steinberg::Vst::SpeakerArr::kStereo;
        auto result = processor.setBusArrangements(
            nullptr, 0,  // zero inputs
            &stereo, 1   // one stereo output
        );
        REQUIRE(result == Steinberg::kResultTrue);
    }

    SECTION("Rejects mono output") {
        Steinberg::Vst::SpeakerArrangement mono = Steinberg::Vst::SpeakerArr::kMono;
        auto result = processor.setBusArrangements(
            nullptr, 0,
            &mono, 1
        );
        REQUIRE(result == Steinberg::kResultFalse);
    }

    SECTION("Accepts stereo sidechain input + stereo output") {
        Steinberg::Vst::SpeakerArrangement stereo = Steinberg::Vst::SpeakerArr::kStereo;
        auto result = processor.setBusArrangements(
            &stereo, 1,  // one stereo input (sidechain)
            &stereo, 1   // one stereo output
        );
        REQUIRE(result == Steinberg::kResultTrue);
    }

    SECTION("Accepts mono sidechain + stereo output") {
        Steinberg::Vst::SpeakerArrangement mono = Steinberg::Vst::SpeakerArr::kMono;
        Steinberg::Vst::SpeakerArrangement stereo = Steinberg::Vst::SpeakerArr::kStereo;
        auto result = processor.setBusArrangements(&mono, 1, &stereo, 1);
        REQUIRE(result == Steinberg::kResultTrue);

        // The accepted arrangement is what the bus now reports
        Steinberg::Vst::SpeakerArrangement arr = 0;
        REQUIRE(processor.getBusArrangement(Steinberg::Vst::kInput, 0, arr) == Steinberg::kResultTrue);
        CHECK(arr == Steinberg::Vst::SpeakerArr::kMono);
    }

    SECTION("Rejects mono output with a sidechain") {
        Steinberg::Vst::SpeakerArrangement mono = Steinberg::Vst::SpeakerArr::kMono;
        Steinberg::Vst::SpeakerArrangement stereo = Steinberg::Vst::SpeakerArr::kStereo;
        auto result = processor.setBusArrangements(&stereo, 1, &mono, 1);
        REQUIRE(result == Steinberg::kResultFalse);
    }

    SECTION("Rejects two outputs") {
        Steinberg::Vst::SpeakerArrangement stereo[2] = {
            Steinberg::Vst::SpeakerArr::kStereo,
            Steinberg::Vst::SpeakerArr::kStereo
        };
        auto result = processor.setBusArrangements(
            nullptr, 0,
            stereo, 2
        );
        REQUIRE(result == Steinberg::kResultFalse);
    }

    SECTION("Rejects zero outputs") {
        auto result = processor.setBusArrangements(
            nullptr, 0,
            nullptr, 0
        );
        REQUIRE(result == Steinberg::kResultFalse);
    }

    processor.terminate();
}
