// ==============================================================================
// Integration Test: MIDI Event Dispatch
// ==============================================================================
// Verifies that MIDI events are dispatched correctly through the Processor:
// - Multiple noteOn events produce audio
// - NoteOff events trigger release
// - Velocity-0 noteOn treated as noteOff
// - Unsupported events are ignored
//
// Reference: specs/045-plugin-shell/spec.md FR-009, FR-010
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"

#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>
#include <cmath>
#include <vector>
#include "vst_param_changes.h"
#include "vst_event_list.h"

using Catch::Approx;

// =============================================================================
// Mock classes (same as processor_audio_test)
// =============================================================================

// IEventList mock consolidated into tests/test_helpers/vst_event_list.h
using TestEventList = Krate::Test::EventList;


// Parameter-change mocks consolidated into tests/test_helpers/vst_param_changes.h
using TestParameterChanges = Krate::Test::ParameterChanges;


// =============================================================================
// Helpers
// =============================================================================

static bool hasNonZeroSamples(const float* buffer, size_t numSamples) {
    for (size_t i = 0; i < numSamples; ++i) {
        if (buffer[i] != 0.0f) return true;
    }
    return false;
}

static float findPeak(const float* buffer, size_t numSamples) {
    float peak = 0.0f;
    for (size_t i = 0; i < numSamples; ++i) {
        float absVal = std::abs(buffer[i]);
        if (absVal > peak) peak = absVal;
    }
    return peak;
}

// =============================================================================
// Test Fixture Helper
// =============================================================================

struct ProcessorFixture {
    Ruinae::Processor processor;
    TestEventList events;
    TestParameterChanges params;
    std::vector<float> outL;
    std::vector<float> outR;
    float* channelBuffers[2];
    Steinberg::Vst::AudioBusBuffers outputBus{};
    Steinberg::Vst::ProcessData data{};
    static constexpr size_t kBlockSize = 512;

    ProcessorFixture() : outL(kBlockSize, 0.0f), outR(kBlockSize, 0.0f) {
        channelBuffers[0] = outL.data();
        channelBuffers[1] = outR.data();
        outputBus.numChannels = 2;
        outputBus.channelBuffers32 = channelBuffers;

        data.processMode = Steinberg::Vst::kRealtime;
        data.symbolicSampleSize = Steinberg::Vst::kSample32;
        data.numSamples = static_cast<Steinberg::int32>(kBlockSize);
        data.numInputs = 0;
        data.inputs = nullptr;
        data.numOutputs = 1;
        data.outputs = &outputBus;
        data.inputParameterChanges = &params;
        data.inputEvents = &events;
        data.processContext = nullptr;

        processor.initialize(nullptr);
        Steinberg::Vst::ProcessSetup setup{};
        setup.processMode = Steinberg::Vst::kRealtime;
        setup.symbolicSampleSize = Steinberg::Vst::kSample32;
        setup.sampleRate = 44100.0;
        setup.maxSamplesPerBlock = static_cast<Steinberg::int32>(kBlockSize);
        processor.setupProcessing(setup);
        processor.setActive(true);
    }

    ~ProcessorFixture() {
        processor.setActive(false);
        processor.terminate();
    }

    void processBlock() {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        processor.process(data);
    }

    void clearEvents() {
        events.clear();
    }
};

// =============================================================================
// Tests
// =============================================================================

TEST_CASE("Multiple noteOn events produce audio", "[midi][integration]") {
    ProcessorFixture f;

    // Send two notes simultaneously
    f.events.addNoteOn(60, 0.8f);
    f.events.addNoteOn(64, 0.8f);
    f.processBlock();
    f.clearEvents();

    // Process a few more blocks
    bool audioFound = false;
    for (int i = 0; i < 5; ++i) {
        f.processBlock();
        if (hasNonZeroSamples(f.outL.data(), f.kBlockSize)) {
            audioFound = true;
            break;
        }
    }

    REQUIRE(audioFound);
}

TEST_CASE("Velocity-0 noteOn is treated as noteOff", "[midi][integration]") {
    ProcessorFixture f;

    // Start a note
    f.events.addNoteOn(60, 0.8f);
    f.processBlock();
    f.clearEvents();

    // Let it play for a bit
    for (int i = 0; i < 5; ++i) {
        f.processBlock();
    }

    // Send velocity-0 noteOn (should act as noteOff)
    f.events.addNoteOn(60, 0.0f);
    f.processBlock();
    f.clearEvents();

    // Wait for release + effects tails
    float finalPeak = 1.0f;
    for (int i = 0; i < 200; ++i) {
        f.processBlock();
        finalPeak = std::max(findPeak(f.outL.data(), f.kBlockSize),
                             findPeak(f.outR.data(), f.kBlockSize));
        if (finalPeak < 1e-6f) break;
    }

    REQUIRE(finalPeak < 0.01f);
}

TEST_CASE("Unsupported event types are ignored", "[midi][integration]") {
    ProcessorFixture f;

    // Create an unsupported event type
    Steinberg::Vst::Event unsupportedEvent{};
    unsupportedEvent.type = Steinberg::Vst::Event::kDataEvent;
    unsupportedEvent.sampleOffset = 0;
    f.events.addEvent(unsupportedEvent);

    // Should not crash
    f.processBlock();

    // Output should be silence (no notes played)
    float peak = std::max(findPeak(f.outL.data(), f.kBlockSize),
                          findPeak(f.outR.data(), f.kBlockSize));
    REQUIRE(peak < 0.01f);
}
