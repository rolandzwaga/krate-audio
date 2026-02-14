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
// Mock: Single Parameter Value Queue (for sending param changes)
// =============================================================================

class MockParamValueQueue : public Steinberg::Vst::IParamValueQueue {
public:
    MockParamValueQueue(Steinberg::Vst::ParamID id, double value)
        : paramId_(id), value_(value) {}

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override {
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }

    Steinberg::Vst::ParamID PLUGIN_API getParameterId() override { return paramId_; }
    Steinberg::int32 PLUGIN_API getPointCount() override { return 1; }

    Steinberg::tresult PLUGIN_API getPoint(
        Steinberg::int32 index, Steinberg::int32& sampleOffset,
        Steinberg::Vst::ParamValue& value) override {
        if (index != 0) return Steinberg::kResultFalse;
        sampleOffset = 0;
        value = value_;
        return Steinberg::kResultTrue;
    }

    Steinberg::tresult PLUGIN_API addPoint(
        Steinberg::int32, Steinberg::Vst::ParamValue,
        Steinberg::int32&) override {
        return Steinberg::kResultFalse;
    }

private:
    Steinberg::Vst::ParamID paramId_;
    double value_;
};

// =============================================================================
// Mock: Parameter Changes Container (supports multiple param changes)
// =============================================================================

class MockParamChangesWithData : public Steinberg::Vst::IParameterChanges {
public:
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override {
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }

    Steinberg::int32 PLUGIN_API getParameterCount() override {
        return static_cast<Steinberg::int32>(queues_.size());
    }

    Steinberg::Vst::IParamValueQueue* PLUGIN_API getParameterData(
        Steinberg::int32 index) override {
        if (index < 0 || index >= static_cast<Steinberg::int32>(queues_.size()))
            return nullptr;
        return &queues_[static_cast<size_t>(index)];
    }

    Steinberg::Vst::IParamValueQueue* PLUGIN_API addParameterData(
        const Steinberg::Vst::ParamID&, Steinberg::int32&) override {
        return nullptr;
    }

    void addChange(Steinberg::Vst::ParamID id, double value) {
        queues_.emplace_back(id, value);
    }

private:
    std::vector<MockParamValueQueue> queues_;
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

// =============================================================================
// End-to-End: Phaser effect modifies audio through full Processor pipeline
// =============================================================================

TEST_CASE("Processor phaser end-to-end: phaser ON vs OFF produces different output",
          "[processor][integration][phaser]") {
    // This test verifies the FULL pipeline: Host param change → Processor →
    // Engine → EffectsChain → Phaser DSP. Two identical Processor instances
    // play the same note; one has phaser enabled, the other doesn't. The
    // outputs must differ if the phaser is actually in the signal path.

    constexpr size_t kBlockSize = 512;
    constexpr int kNumBlocks = 20;  // ~232ms of audio at 44.1kHz

    // Helper lambda: set up a processor, play a note, collect output
    auto runProcessor = [&](bool enablePhaser) -> std::vector<float> {
        Ruinae::Processor proc;
        proc.initialize(nullptr);

        Steinberg::Vst::ProcessSetup setup{};
        setup.processMode = Steinberg::Vst::kRealtime;
        setup.symbolicSampleSize = Steinberg::Vst::kSample32;
        setup.sampleRate = 44100.0;
        setup.maxSamplesPerBlock = static_cast<Steinberg::int32>(kBlockSize);
        proc.setupProcessing(setup);
        proc.setActive(true);

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
        data.processContext = nullptr;

        // Block 0: Send phaser params + noteOn
        MockParamChangesWithData params;
        if (enablePhaser) {
            params.addChange(Ruinae::kPhaserEnabledId, 1.0);   // Enable phaser
            params.addChange(Ruinae::kPhaserMixId, 1.0);        // 100% wet
            params.addChange(Ruinae::kPhaserDepthId, 1.0);      // Full depth
            params.addChange(Ruinae::kPhaserRateId, 0.5);       // ~10 Hz
            params.addChange(Ruinae::kPhaserFeedbackId, 0.75);  // +50% feedback
            params.addChange(Ruinae::kPhaserStagesId, 0.6);     // ~8 stages
            params.addChange(Ruinae::kPhaserCenterFreqId, 0.5); // ~5000 Hz center
        }
        data.inputParameterChanges = &params;

        MockEventList events;
        events.addNoteOn(48, 0.9f);  // C3, high velocity
        data.inputEvents = &events;

        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        proc.process(data);

        // Clear events/params for subsequent blocks
        MockParameterChanges emptyParams;
        MockEventList emptyEvents;
        data.inputParameterChanges = &emptyParams;
        data.inputEvents = &emptyEvents;

        // Collect output from remaining blocks
        std::vector<float> allOutput;
        allOutput.insert(allOutput.end(), outL.begin(), outL.end());

        for (int block = 1; block < kNumBlocks; ++block) {
            std::fill(outL.begin(), outL.end(), 0.0f);
            std::fill(outR.begin(), outR.end(), 0.0f);
            proc.process(data);
            allOutput.insert(allOutput.end(), outL.begin(), outL.end());
        }

        proc.setActive(false);
        proc.terminate();
        return allOutput;
    };

    auto outputWithPhaser = runProcessor(true);
    auto outputWithout = runProcessor(false);

    REQUIRE(outputWithPhaser.size() == outputWithout.size());

    // Compare: find max sample difference
    float maxDiff = 0.0f;
    float maxAbs = 0.0f;
    for (size_t i = 0; i < outputWithPhaser.size(); ++i) {
        float diff = std::abs(outputWithPhaser[i] - outputWithout[i]);
        maxDiff = std::max(maxDiff, diff);
        maxAbs = std::max(maxAbs, std::max(
            std::abs(outputWithPhaser[i]), std::abs(outputWithout[i])));
    }

    INFO("Max sample difference (phaser ON vs OFF): " << maxDiff);
    INFO("Max absolute output level: " << maxAbs);
    INFO("Output has audio: " << (maxAbs > 0.01f ? "YES" : "NO"));

    // The phaser at 100% wet, full depth, 8 stages, +50% feedback should
    // produce a VERY audible difference on any harmonically rich synth signal.
    REQUIRE(maxAbs > 0.01f);     // Verify we actually have audio
    REQUIRE(maxDiff > 0.05f);    // Phaser must clearly modify the signal
}
