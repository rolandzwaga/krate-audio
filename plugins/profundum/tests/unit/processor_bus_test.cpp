// ==============================================================================
// Profundum_ProcessorBusSetup (Phase 10 T008, SC-012; FR-020, FR-021, FR-022)
// ==============================================================================
// Instrument bus shape: one event input, no audio input, one stereo output.
// setBusArrangements accepts only (no inputs, exactly one stereo output).
// The engine is heap-created in initialize() and destroyed in terminate().
// ==============================================================================

#include "profundum_test_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <memory>

using namespace Steinberg;
using namespace Steinberg::Vst;

TEST_CASE("Profundum_ProcessorBusSetup", "[profundum][processor]") {
    auto proc = std::make_unique<::Profundum::Processor>();
    REQUIRE(proc->initialize(nullptr) == kResultOk);

    SECTION("BusCounts") {
        CHECK(proc->getBusCount(kAudio, kInput) == 0);
        CHECK(proc->getBusCount(kEvent, kInput) == 1);
        CHECK(proc->getBusCount(kAudio, kOutput) == 1);

        BusInfo info{};
        REQUIRE(proc->getBusInfo(kAudio, kOutput, 0, info) == kResultOk);
        CHECK(info.channelCount == 2);

        CHECK(proc->engineForTest() != nullptr);
        REQUIRE(proc->terminate() == kResultOk);
        CHECK(proc->engineForTest() == nullptr);
    }

    SECTION("Arrangements") {
        SpeakerArrangement stereo = SpeakerArr::kStereo;
        SpeakerArrangement mono = SpeakerArr::kMono;
        std::array<SpeakerArrangement, 2> twoStereo{SpeakerArr::kStereo, SpeakerArr::kStereo};

        CHECK(proc->setBusArrangements(nullptr, 0, &stereo, 1) == kResultTrue);
        CHECK(proc->setBusArrangements(&stereo, 1, &stereo, 1) == kResultFalse);
        CHECK(proc->setBusArrangements(nullptr, 0, &mono, 1) == kResultFalse);
        CHECK(proc->setBusArrangements(nullptr, 0, twoStereo.data(), 2) == kResultFalse);
    }

    proc->terminate();  // idempotent: a second terminate() after BusCounts is harmless
}
