// ==============================================================================
// Integration Test: Processor Audio Generation
// ==============================================================================
// Verifies the Processor lifecycle (initialize, setupProcessing, setActive,
// process) and that MIDI noteOn events produce audio output.
//
// Reference: specs/045-plugin-shell/spec.md FR-001, FR-002
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"

#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

using Catch::Approx;

// =============================================================================
// Minimal Mock IEventList for testing
// =============================================================================

class MockEventList : public Steinberg::Vst::IEventList {
public:
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override {
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }

    Steinberg::int32 PLUGIN_API getEventCount() override {
        return static_cast<Steinberg::int32>(events_.size());
    }

    Steinberg::tresult PLUGIN_API getEvent(Steinberg::int32 index,
                                            Steinberg::Vst::Event& e) override {
        if (index < 0 || index >= static_cast<Steinberg::int32>(events_.size()))
            return Steinberg::kResultFalse;
        e = events_[static_cast<size_t>(index)];
        return Steinberg::kResultTrue;
    }

    Steinberg::tresult PLUGIN_API addEvent(Steinberg::Vst::Event& e) override {
        events_.push_back(e);
        return Steinberg::kResultTrue;
    }

    void addNoteOn(int16_t pitch, float velocity, int32_t sampleOffset = 0) {
        Steinberg::Vst::Event e{};
        e.type = Steinberg::Vst::Event::kNoteOnEvent;
        e.sampleOffset = sampleOffset;
        e.noteOn.channel = 0;
        e.noteOn.pitch = pitch;
        e.noteOn.velocity = velocity;
        e.noteOn.noteId = -1;
        e.noteOn.length = 0;
        e.noteOn.tuning = 0.0f;
        events_.push_back(e);
    }

    void addNoteOff(int16_t pitch, int32_t sampleOffset = 0) {
        Steinberg::Vst::Event e{};
        e.type = Steinberg::Vst::Event::kNoteOffEvent;
        e.sampleOffset = sampleOffset;
        e.noteOff.channel = 0;
        e.noteOff.pitch = pitch;
        e.noteOff.velocity = 0.0f;
        e.noteOff.noteId = -1;
        e.noteOff.tuning = 0.0f;
        events_.push_back(e);
    }

    void clear() { events_.clear(); }

private:
    std::vector<Steinberg::Vst::Event> events_;
};

// =============================================================================
// Minimal Mock IParameterChanges (empty -- no parameter changes)
// =============================================================================

class MockParameterChanges : public Steinberg::Vst::IParameterChanges {
public:
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override {
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }

    Steinberg::int32 PLUGIN_API getParameterCount() override { return 0; }
    Steinberg::Vst::IParamValueQueue* PLUGIN_API getParameterData(
        Steinberg::int32) override { return nullptr; }
    Steinberg::Vst::IParamValueQueue* PLUGIN_API addParameterData(
        const Steinberg::Vst::ParamID&, Steinberg::int32&) override { return nullptr; }
};

// =============================================================================
// Test Helpers
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
// Tests
// =============================================================================

TEST_CASE("Processor lifecycle and audio generation", "[processor][integration]") {
    Ruinae::Processor processor;

    // Initialize
    auto initResult = processor.initialize(nullptr);
    REQUIRE(initResult == Steinberg::kResultTrue);

    // Setup processing
    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.sampleRate = 44100.0;
    setup.maxSamplesPerBlock = 512;

    auto setupResult = processor.setupProcessing(setup);
    REQUIRE(setupResult == Steinberg::kResultTrue);

    // Activate
    auto activeResult = processor.setActive(true);
    REQUIRE(activeResult == Steinberg::kResultTrue);

    constexpr size_t kBlockSize = 512;

    SECTION("Process without MIDI produces silence") {
        // Setup output buffers
        std::vector<float> outL(kBlockSize, 0.0f);
        std::vector<float> outR(kBlockSize, 0.0f);
        float* channelBuffers[2] = {outL.data(), outR.data()};

        Steinberg::Vst::AudioBusBuffers outputBus{};
        outputBus.numChannels = 2;
        outputBus.channelBuffers32 = channelBuffers;

        MockParameterChanges paramChanges;
        MockEventList eventList;

        Steinberg::Vst::ProcessData data{};
        data.processMode = Steinberg::Vst::kRealtime;
        data.symbolicSampleSize = Steinberg::Vst::kSample32;
        data.numSamples = static_cast<Steinberg::int32>(kBlockSize);
        data.numInputs = 0;
        data.inputs = nullptr;
        data.numOutputs = 1;
        data.outputs = &outputBus;
        data.inputParameterChanges = &paramChanges;
        data.inputEvents = &eventList;
        data.processContext = nullptr;

        auto result = processor.process(data);
        REQUIRE(result == Steinberg::kResultTrue);

        // Without any noteOn, output should be silence (or near-silence)
        float peakL = findPeak(outL.data(), kBlockSize);
        float peakR = findPeak(outR.data(), kBlockSize);
        // Allow small residual from effects chain tails
        REQUIRE(peakL < 0.01f);
        REQUIRE(peakR < 0.01f);
    }

    SECTION("NoteOn produces non-zero audio output") {
        MockEventList eventList;
        eventList.addNoteOn(60, 0.8f); // Middle C, velocity ~102

        MockParameterChanges paramChanges;

        // Process several blocks to allow attack to produce output
        std::vector<float> outL(kBlockSize, 0.0f);
        std::vector<float> outR(kBlockSize, 0.0f);
        float* channelBuffers[2] = {outL.data(), outR.data()};

        Steinberg::Vst::AudioBusBuffers outputBus{};
        outputBus.numChannels = 2;
        outputBus.channelBuffers32 = channelBuffers;

        Steinberg::Vst::ProcessData data{};
        data.processMode = Steinberg::Vst::kRealtime;
        data.symbolicSampleSize = Steinberg::Vst::kSample32;
        data.numSamples = static_cast<Steinberg::int32>(kBlockSize);
        data.numInputs = 0;
        data.inputs = nullptr;
        data.numOutputs = 1;
        data.outputs = &outputBus;
        data.inputParameterChanges = &paramChanges;
        data.inputEvents = &eventList;
        data.processContext = nullptr;

        // First block: send noteOn
        auto result = processor.process(data);
        REQUIRE(result == Steinberg::kResultTrue);

        // Clear events for subsequent blocks
        eventList.clear();
        data.inputEvents = &eventList;

        // Process a few more blocks to let the sound develop
        bool audioProduced = hasNonZeroSamples(outL.data(), kBlockSize) ||
                             hasNonZeroSamples(outR.data(), kBlockSize);

        for (int block = 0; block < 4 && !audioProduced; ++block) {
            std::fill(outL.begin(), outL.end(), 0.0f);
            std::fill(outR.begin(), outR.end(), 0.0f);
            processor.process(data);
            audioProduced = hasNonZeroSamples(outL.data(), kBlockSize) ||
                            hasNonZeroSamples(outR.data(), kBlockSize);
        }

        REQUIRE(audioProduced);
    }

    SECTION("NoteOff leads to eventual silence") {
        MockEventList eventList;
        MockParameterChanges paramChanges;

        std::vector<float> outL(kBlockSize, 0.0f);
        std::vector<float> outR(kBlockSize, 0.0f);
        float* channelBuffers[2] = {outL.data(), outR.data()};

        Steinberg::Vst::AudioBusBuffers outputBus{};
        outputBus.numChannels = 2;
        outputBus.channelBuffers32 = channelBuffers;

        Steinberg::Vst::ProcessData data{};
        data.processMode = Steinberg::Vst::kRealtime;
        data.symbolicSampleSize = Steinberg::Vst::kSample32;
        data.numSamples = static_cast<Steinberg::int32>(kBlockSize);
        data.numInputs = 0;
        data.inputs = nullptr;
        data.numOutputs = 1;
        data.outputs = &outputBus;
        data.inputParameterChanges = &paramChanges;
        data.inputEvents = &eventList;
        data.processContext = nullptr;

        // Send noteOn
        eventList.addNoteOn(60, 0.8f);
        processor.process(data);
        eventList.clear();

        // Process a few blocks
        for (int i = 0; i < 4; ++i) {
            std::fill(outL.begin(), outL.end(), 0.0f);
            std::fill(outR.begin(), outR.end(), 0.0f);
            processor.process(data);
        }

        // Send noteOff
        eventList.addNoteOff(60);
        data.inputEvents = &eventList;
        processor.process(data);
        eventList.clear();
        data.inputEvents = &eventList;

        // Process many blocks for release + effects tail to die out
        // (with reverb/delay effects, this could take many blocks)
        float finalPeak = 1.0f;
        for (int i = 0; i < 200; ++i) {
            std::fill(outL.begin(), outL.end(), 0.0f);
            std::fill(outR.begin(), outR.end(), 0.0f);
            processor.process(data);
            finalPeak = std::max(findPeak(outL.data(), kBlockSize),
                                 findPeak(outR.data(), kBlockSize));
            if (finalPeak < 1e-6f) break;
        }

        REQUIRE(finalPeak < 0.01f);
    }

    // Cleanup
    processor.setActive(false);
    processor.terminate();
}
