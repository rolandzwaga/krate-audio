// ==============================================================================
// Vorago - processor bus setup tests (T011, FR-020 / FR-021 / FR-022 -> SC-011)
// ==============================================================================
// Vorago is an instrument: 1 event input, 1 stereo audio output, and NO audio
// input bus. Each setBusArrangements rejection is asserted separately because
// each guards a different failure:
//   (a) numIns  != 0 -- no audio input bus exists to arrange;
//   (b) numOuts != 1 -- exactly one output bus exists;
//   (c) non-stereo   -- the render path reads channelBuffers32[0] and [1].
// FR-022: the engine and cavern are heap-owned, created by initialize() and
// destroyed (pointer nulled) by terminate().
// ==============================================================================

// Includes the shared fixture so this TU keeps compile-checking it.
#include "vorago_test_fixture.h"

#include "processor/processor.h"

#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/vstspeaker.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <memory>

using namespace Steinberg;
using namespace Steinberg::Vst;

TEST_CASE("Vorago_ProcessorBusSetup", "[vorago][processor]") {
    auto proc = std::make_unique<::Vorago::Processor>();  // FR-064: heap
    REQUIRE(proc->initialize(nullptr) == kResultOk);

    SECTION("bus counts are exactly instrument-shaped") {
        REQUIRE(proc->getBusCount(kEvent, kInput) == 1);
        REQUIRE(proc->getBusCount(kAudio, kInput) == 0);
        REQUIRE(proc->getBusCount(kAudio, kOutput) == 1);

        SpeakerArrangement arr = SpeakerArr::kEmpty;
        REQUIRE(proc->getBusArrangement(kOutput, 0, arr) == kResultTrue);
        REQUIRE(arr == SpeakerArr::kStereo);
    }

    SECTION("one stereo output bus with no inputs is accepted") {
        SpeakerArrangement stereo = SpeakerArr::kStereo;
        REQUIRE(proc->setBusArrangements(nullptr, 0, &stereo, 1) == kResultTrue);
    }

    SECTION("an audio input arrangement is rejected") {
        SpeakerArrangement in = SpeakerArr::kStereo;
        SpeakerArrangement out = SpeakerArr::kStereo;
        REQUIRE(proc->setBusArrangements(&in, 1, &out, 1) == kResultFalse);
    }

    SECTION("a mono output arrangement is rejected") {
        SpeakerArrangement mono = SpeakerArr::kMono;
        REQUIRE(proc->setBusArrangements(nullptr, 0, &mono, 1) == kResultFalse);
    }

    SECTION("two output buses is rejected (only one exists)") {
        std::array<SpeakerArrangement, 2> outs{SpeakerArr::kStereo, SpeakerArr::kStereo};
        REQUIRE(proc->setBusArrangements(nullptr, 0, outs.data(),
                                         static_cast<int32>(outs.size())) == kResultFalse);
    }

    SECTION("engine and cavern exist after initialize and are nulled by terminate") {
        REQUIRE(proc->engineForTest() != nullptr);
        REQUIRE(proc->cavernForTest() != nullptr);

        REQUIRE(proc->terminate() == kResultOk);
        REQUIRE(proc->engineForTest() == nullptr);
        REQUIRE(proc->cavernForTest() == nullptr);
        return;  // already terminated
    }

    REQUIRE(proc->terminate() == kResultOk);
}
