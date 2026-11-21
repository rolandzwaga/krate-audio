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
#include <catch2/generators/catch_generators.hpp>

#include "processor/processor.h"
#include "parameters/env_params.h"
#include "plugin_ids.h"

#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include "vst_param_changes.h"
#include "vst_event_list.h"
#include "test_helpers/reverb_metrics.h"

using Catch::Approx;

// =============================================================================
// Minimal Mock IEventList for testing
// =============================================================================

// IEventList mock consolidated into tests/test_helpers/vst_event_list.h
using MockEventList = Krate::Test::EventList;


// =============================================================================
// Minimal Mock IParameterChanges (empty -- no parameter changes)
// =============================================================================

// Parameter-change mocks consolidated into tests/test_helpers/vst_param_changes.h
using MockParameterChanges = Krate::Test::ParameterChanges;
using MockParamValueQueue = Krate::Test::ParamValueQueue;
using MockParamChangesWithData = Krate::Test::ParameterChanges;


// =============================================================================
// Mock: Single Parameter Value Queue (for sending param changes)
// =============================================================================



// =============================================================================
// Mock: Parameter Changes Container (supports multiple param changes)
// =============================================================================



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
            params.addChange(Ruinae::kModulationTypeId, 1.0 / 3.0);   // Phaser (1/3 normalized)
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

// =============================================================================
// Mono mode: processor-level integration test
// =============================================================================
// Reproduces user-reported issue: switching VoiceMode to Mono via parameter
// change, then playing MIDI notes, produces no audio.
// Tests the full VST3 pipeline: parameter change → processParameterChanges →
// applyParamsToEngine → processEvents → engine_.processBlock.

TEST_CASE("Mono priority index 1 plays the lower note", "[integration][mono]") {
    Ruinae::Processor processor;
    REQUIRE(processor.initialize(nullptr) == Steinberg::kResultTrue);

    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.sampleRate = 44100.0;
    setup.maxSamplesPerBlock = 512;
    REQUIRE(processor.setupProcessing(setup) == Steinberg::kResultTrue);
    REQUIRE(processor.setActive(true) == Steinberg::kResultTrue);

    constexpr size_t kBlockSize = 512;
    std::vector<float> outL(kBlockSize, 0.0f);
    std::vector<float> outR(kBlockSize, 0.0f);

    auto processBlock = [&](Steinberg::Vst::IParameterChanges* params,
                            Steinberg::Vst::IEventList* events) {
        float* channelBuffers[2] = {outL.data(), outR.data()};
        Steinberg::Vst::AudioBusBuffers outputBus{};
        outputBus.numChannels = 2;
        outputBus.channelBuffers32 = channelBuffers;
        MockParameterChanges emptyParams;
        MockEventList emptyEvents;
        Steinberg::Vst::ProcessData data{};
        data.processMode = Steinberg::Vst::kRealtime;
        data.symbolicSampleSize = Steinberg::Vst::kSample32;
        data.numSamples = static_cast<Steinberg::int32>(kBlockSize);
        data.numOutputs = 1;
        data.outputs = &outputBus;
        data.inputParameterChanges = params ? params : &emptyParams;
        data.inputEvents = events ? events : &emptyEvents;
        return processor.process(data);
    };

    MockParamChangesWithData modeParams;
    modeParams.addChange(Ruinae::kVoiceModeId, 1.0);     // Mono
    modeParams.addChange(Ruinae::kMonoPriorityId, 0.5);  // index 1 = LowNote
    processBlock(&modeParams, nullptr);

    MockEventList events;
    events.addNoteOn(48, 0.8f);  // C3
    events.addNoteOn(60, 0.8f);  // C4
    processBlock(nullptr, &events);

    // Settle (effects chain latency), then measure over several blocks.
    for (int i = 0; i < 20; ++i) processBlock(nullptr, nullptr);

    std::vector<float> rendered;
    for (int i = 0; i < 16; ++i) {
        processBlock(nullptr, nullptr);
        rendered.insert(rendered.end(), outL.begin(), outL.end());
    }

    // Zero-crossing pitch estimate (rising crossings per second).
    size_t crossings = 0;
    for (size_t i = 1; i < rendered.size(); ++i) {
        if (rendered[i - 1] <= 0.0f && rendered[i] > 0.0f) ++crossings;
    }
    const double seconds = static_cast<double>(rendered.size()) / 44100.0;
    const double freq = static_cast<double>(crossings) / seconds;

    // C3 = 130.8 Hz, C4 = 261.6 Hz: 196 Hz separates them.
    INFO("estimated pitch " << freq << " Hz");
    REQUIRE(hasNonZeroSamples(rendered.data(), rendered.size()));
    CHECK(freq < 196.0);

    processor.setActive(false);
    processor.terminate();
}

// R-182: the Spectral Shift parameter reaches the audio through the processor.
TEST_CASE("Mixer spectral shift parameter reaches the audio", "[integration][mixer]") {
    // Holds C4 in SpectralMorph mode with Spectral Shift at `shiftNormalized` and
    // returns the left channel of 8 blocks after a 20-block settle.
    auto render = [](double shiftNormalized) {
        Ruinae::Processor processor;
        REQUIRE(processor.initialize(nullptr) == Steinberg::kResultTrue);
        Steinberg::Vst::ProcessSetup setup{};
        setup.processMode = Steinberg::Vst::kRealtime;
        setup.symbolicSampleSize = Steinberg::Vst::kSample32;
        setup.sampleRate = 44100.0;
        setup.maxSamplesPerBlock = 512;
        REQUIRE(processor.setupProcessing(setup) == Steinberg::kResultTrue);
        REQUIRE(processor.setActive(true) == Steinberg::kResultTrue);

        constexpr size_t kBlockSize = 512;
        std::vector<float> outL(kBlockSize, 0.0f);
        std::vector<float> outR(kBlockSize, 0.0f);
        auto processBlock = [&](Steinberg::Vst::IParameterChanges* params,
                                Steinberg::Vst::IEventList* events) {
            float* channelBuffers[2] = {outL.data(), outR.data()};
            Steinberg::Vst::AudioBusBuffers outputBus{};
            outputBus.numChannels = 2;
            outputBus.channelBuffers32 = channelBuffers;
            MockParameterChanges emptyParams;
            MockEventList emptyEvents;
            Steinberg::Vst::ProcessData data{};
            data.processMode = Steinberg::Vst::kRealtime;
            data.symbolicSampleSize = Steinberg::Vst::kSample32;
            data.numSamples = static_cast<Steinberg::int32>(kBlockSize);
            data.numOutputs = 1;
            data.outputs = &outputBus;
            data.inputParameterChanges = params ? params : &emptyParams;
            data.inputEvents = events ? events : &emptyEvents;
            return processor.process(data);
        };

        MockParamChangesWithData params;
        params.addChange(Ruinae::kMixerModeId, 1.0);  // Spectral Morph
        params.addChange(Ruinae::kMixerShiftId, shiftNormalized);
        MockEventList events;
        events.addNoteOn(60, 0.8f);
        processBlock(&params, &events);
        for (int i = 0; i < 20; ++i) processBlock(nullptr, nullptr);

        std::vector<float> rendered;
        for (int i = 0; i < 8; ++i) {
            processBlock(nullptr, nullptr);
            rendered.insert(rendered.end(), outL.begin(), outL.end());
        }
        processor.setActive(false);
        processor.terminate();
        return rendered;
    };

    using Krate::DSP::TestUtils::spectralCentroidHz;
    const auto down = render(0.25);  // -12 st
    const auto up = render(0.75);    // +12 st
    REQUIRE(hasNonZeroSamples(down.data(), down.size()));
    REQUIRE(hasNonZeroSamples(up.data(), up.size()));
    const double centroidDown = spectralCentroidHz(down, 44100.0);
    const double centroidUp = spectralCentroidHz(up, 44100.0);
    INFO("centroid at shift 0.25: " << centroidDown << " Hz, at 0.75: " << centroidUp << " Hz");
    CHECK(centroidUp > centroidDown * 1.5);
}

TEST_CASE("Processor mono mode produces audio via parameter change",
          "[processor][integration][mono-silence-bug]") {
    Ruinae::Processor processor;
    REQUIRE(processor.initialize(nullptr) == Steinberg::kResultTrue);

    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.sampleRate = 44100.0;
    setup.maxSamplesPerBlock = 512;
    REQUIRE(processor.setupProcessing(setup) == Steinberg::kResultTrue);
    REQUIRE(processor.setActive(true) == Steinberg::kResultTrue);

    constexpr size_t kBlockSize = 512;
    constexpr int kWarmUpBlocks = 20;  // Account for effects chain latency

    // Helper: run one process block with optional param changes and events
    auto processBlock = [&](Steinberg::Vst::IParameterChanges* params,
                            Steinberg::Vst::IEventList* events,
                            float* outL, float* outR) {
        float* channelBuffers[2] = {outL, outR};
        Steinberg::Vst::AudioBusBuffers outputBus{};
        outputBus.numChannels = 2;
        outputBus.channelBuffers32 = channelBuffers;

        MockParameterChanges emptyParams;
        MockEventList emptyEvents;

        Steinberg::Vst::ProcessData data{};
        data.processMode = Steinberg::Vst::kRealtime;
        data.symbolicSampleSize = Steinberg::Vst::kSample32;
        data.numSamples = static_cast<Steinberg::int32>(kBlockSize);
        data.numInputs = 0;
        data.inputs = nullptr;
        data.numOutputs = 1;
        data.outputs = &outputBus;
        data.inputParameterChanges = params ? params : &emptyParams;
        data.inputEvents = events ? events : &emptyEvents;
        data.processContext = nullptr;

        return processor.process(data);
    };

    std::vector<float> outL(kBlockSize, 0.0f);
    std::vector<float> outR(kBlockSize, 0.0f);

    SECTION("switch to mono via parameter then noteOn produces audio") {
        // Step 1: Send VoiceMode = 1.0 (Mono) parameter change
        MockParamChangesWithData monoParams;
        monoParams.addChange(Ruinae::kVoiceModeId, 1.0);  // 1.0 = Mono

        processBlock(&monoParams, nullptr, outL.data(), outR.data());

        // Step 2: Send noteOn in subsequent block
        MockEventList events;
        events.addNoteOn(60, 0.8f);

        processBlock(nullptr, &events, outL.data(), outR.data());

        // Step 3: Process warm-up blocks
        bool hasAudio = false;
        for (int i = 0; i < kWarmUpBlocks; ++i) {
            std::fill(outL.begin(), outL.end(), 0.0f);
            std::fill(outR.begin(), outR.end(), 0.0f);
            processBlock(nullptr, nullptr, outL.data(), outR.data());
            if (hasNonZeroSamples(outL.data(), kBlockSize) ||
                hasNonZeroSamples(outR.data(), kBlockSize)) {
                hasAudio = true;
            }
        }

        INFO("Mono mode via parameter change should produce audio");
        REQUIRE(hasAudio);
    }

    SECTION("mono param change and noteOn in same block produces audio") {
        // Simultaneous parameter change and MIDI event (same process call)
        MockParamChangesWithData monoParams;
        monoParams.addChange(Ruinae::kVoiceModeId, 1.0);

        MockEventList events;
        events.addNoteOn(60, 0.8f);

        processBlock(&monoParams, &events, outL.data(), outR.data());

        // Process warm-up blocks
        bool hasAudio = false;
        for (int i = 0; i < kWarmUpBlocks; ++i) {
            std::fill(outL.begin(), outL.end(), 0.0f);
            std::fill(outR.begin(), outR.end(), 0.0f);
            processBlock(nullptr, nullptr, outL.data(), outR.data());
            if (hasNonZeroSamples(outL.data(), kBlockSize) ||
                hasNonZeroSamples(outR.data(), kBlockSize)) {
                hasAudio = true;
            }
        }

        INFO("Mono param + noteOn in same block should produce audio");
        REQUIRE(hasAudio);
    }

    SECTION("poly noteOn then switch to mono continues producing audio") {
        // Play note in poly mode first
        MockEventList noteOnEvent;
        noteOnEvent.addNoteOn(60, 0.8f);
        processBlock(nullptr, &noteOnEvent, outL.data(), outR.data());

        // Warm up poly mode
        for (int i = 0; i < kWarmUpBlocks; ++i) {
            processBlock(nullptr, nullptr, outL.data(), outR.data());
        }

        // Switch to mono mode
        MockParamChangesWithData monoParams;
        monoParams.addChange(Ruinae::kVoiceModeId, 1.0);
        processBlock(&monoParams, nullptr, outL.data(), outR.data());

        // Check audio continues after switch
        bool hasAudio = false;
        for (int i = 0; i < kWarmUpBlocks; ++i) {
            std::fill(outL.begin(), outL.end(), 0.0f);
            std::fill(outR.begin(), outR.end(), 0.0f);
            processBlock(nullptr, nullptr, outL.data(), outR.data());
            if (hasNonZeroSamples(outL.data(), kBlockSize) ||
                hasNonZeroSamples(outR.data(), kBlockSize)) {
                hasAudio = true;
            }
        }

        INFO("Switching to mono with held note should continue producing audio");
        REQUIRE(hasAudio);
    }

    SECTION("poly play, release, switch mono, new note produces audio") {
        // Play and release in poly mode
        MockEventList noteOn;
        noteOn.addNoteOn(60, 0.8f);
        processBlock(nullptr, &noteOn, outL.data(), outR.data());

        for (int i = 0; i < kWarmUpBlocks; ++i) {
            processBlock(nullptr, nullptr, outL.data(), outR.data());
        }

        MockEventList noteOff;
        noteOff.addNoteOff(60);
        processBlock(nullptr, &noteOff, outL.data(), outR.data());

        // Let release tail complete
        for (int i = 0; i < 40; ++i) {
            processBlock(nullptr, nullptr, outL.data(), outR.data());
        }

        // Switch to mono
        MockParamChangesWithData monoParams;
        monoParams.addChange(Ruinae::kVoiceModeId, 1.0);
        processBlock(&monoParams, nullptr, outL.data(), outR.data());

        // Play new note in mono
        MockEventList newNote;
        newNote.addNoteOn(64, 0.8f);
        processBlock(nullptr, &newNote, outL.data(), outR.data());

        // Check for audio
        bool hasAudio = false;
        for (int i = 0; i < kWarmUpBlocks; ++i) {
            std::fill(outL.begin(), outL.end(), 0.0f);
            std::fill(outR.begin(), outR.end(), 0.0f);
            processBlock(nullptr, nullptr, outL.data(), outR.data());
            if (hasNonZeroSamples(outL.data(), kBlockSize) ||
                hasNonZeroSamples(outR.data(), kBlockSize)) {
                hasAudio = true;
            }
        }

        INFO("Poly→release→mono→new note should produce audio");
        REQUIRE(hasAudio);
    }

    processor.setActive(false);
    processor.terminate();
}

// =============================================================================
// R-054: delay and reverb survive an engine reset
// =============================================================================
// setActive(true) and every host preset load call engine_.reset(). The
// processor re-applies the delay/reverb enable flags every block, but the chain
// ignores a flag that did not change, so a reset that dropped the slots to Off
// left an enabled delay and reverb silent until the user toggled them.

namespace {

constexpr size_t kFxBlockSize = 512;
constexpr float kFxDelayMs = 200.0f;
constexpr int kFxPassBlocks = 48;
constexpr int kFxNoteOffBlock = 4;
// Dry note ends at block ~5 (note-off at 4, 5 ms release). The first echo
// arrives at 200 ms (8820 samples), i.e. block ~17.
constexpr int kFxReverbWindowBegin = 7;
constexpr int kFxReverbWindowEnd = 17;
constexpr int kFxEchoWindowBegin = 17;
constexpr int kFxEchoWindowEnd = 32;

struct FxTailLevels {
    float reverbRms = 0.0f; // after the dry note, before the echo arrives
    float echoRms = 0.0f;   // around the first delay echo
};

void processFxBlock(Ruinae::Processor& proc, Steinberg::Vst::IParameterChanges& params,
                    Steinberg::Vst::IEventList& events, std::vector<float>& outL,
                    std::vector<float>& outR) {
    std::fill(outL.begin(), outL.end(), 0.0f);
    std::fill(outR.begin(), outR.end(), 0.0f);
    float* channelBuffers[2] = {outL.data(), outR.data()};

    Steinberg::Vst::AudioBusBuffers outputBus{};
    outputBus.numChannels = 2;
    outputBus.channelBuffers32 = channelBuffers;

    Steinberg::Vst::ProcessData data{};
    data.processMode = Steinberg::Vst::kRealtime;
    data.symbolicSampleSize = Steinberg::Vst::kSample32;
    data.numSamples = static_cast<Steinberg::int32>(kFxBlockSize);
    data.numInputs = 0;
    data.inputs = nullptr;
    data.numOutputs = 1;
    data.outputs = &outputBus;
    data.inputParameterChanges = &params;
    data.inputEvents = &events;
    data.processContext = nullptr;
    proc.process(data);
}

float windowRms(const std::vector<float>& signal, int beginBlock, int endBlock) {
    const size_t begin = static_cast<size_t>(beginBlock) * kFxBlockSize;
    const size_t end = static_cast<size_t>(endBlock) * kFxBlockSize;
    double sum = 0.0;
    for (size_t i = begin; i < end; ++i) {
        sum += static_cast<double>(signal[i]) * static_cast<double>(signal[i]);
    }
    return static_cast<float>(std::sqrt(sum / static_cast<double>(end - begin)));
}

/// Plays one short note and measures the reverb tail and the first echo.
FxTailLevels playNoteAndMeasureTails(Ruinae::Processor& proc) {
    std::vector<float> outL(kFxBlockSize, 0.0f);
    std::vector<float> outR(kFxBlockSize, 0.0f);
    std::vector<float> captured;
    captured.reserve(static_cast<size_t>(kFxPassBlocks) * kFxBlockSize);

    MockParameterChanges noParams;
    for (int block = 0; block < kFxPassBlocks; ++block) {
        MockEventList events;
        if (block == 0) events.addNoteOn(60, 0.8f);
        if (block == kFxNoteOffBlock) events.addNoteOff(60);
        processFxBlock(proc, noParams, events, outL, outR);
        captured.insert(captured.end(), outL.begin(), outL.end());
    }

    FxTailLevels levels;
    levels.reverbRms = windowRms(captured, kFxReverbWindowBegin, kFxReverbWindowEnd);
    levels.echoRms = windowRms(captured, kFxEchoWindowBegin, kFxEchoWindowEnd);
    return levels;
}

std::vector<char> captureProcessorState(Ruinae::Processor& proc) {
    Steinberg::MemoryStream stream;
    proc.getState(&stream);
    Steinberg::int64 size = 0;
    stream.seek(0, Steinberg::IBStream::kIBSeekEnd, &size);
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    std::vector<char> bytes(static_cast<size_t>(size));
    Steinberg::int32 bytesRead = 0;
    stream.read(bytes.data(), static_cast<Steinberg::int32>(size), &bytesRead);
    return bytes;
}

void loadProcessorState(Ruinae::Processor& proc, const std::vector<char>& bytes) {
    Steinberg::MemoryStream stream;
    stream.write(const_cast<char*>(bytes.data()), // NOLINT(cppcoreguidelines-pro-type-const-cast): IBStream::write takes void*
        static_cast<Steinberg::int32>(bytes.size()), nullptr);
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    proc.setState(&stream);
}

} // namespace

TEST_CASE("Delay and reverb survive setActive cycle and a delay-on preset load",
          "[integration][fx][lifecycle]") {
    Ruinae::Processor processor;
    REQUIRE(processor.initialize(nullptr) == Steinberg::kResultTrue);

    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.sampleRate = 44100.0;
    setup.maxSamplesPerBlock = static_cast<Steinberg::int32>(kFxBlockSize);
    REQUIRE(processor.setupProcessing(setup) == Steinberg::kResultTrue);
    REQUIRE(processor.setActive(true) == Steinberg::kResultTrue);

    // Delay and reverb on, a short free-running echo and a short dry note.
    {
        MockParamChangesWithData params;
        params.addChange(Ruinae::kDelayEnabledId, 1.0);
        params.addChange(Ruinae::kReverbEnabledId, 1.0);
        params.addChange(Ruinae::kDelayTypeId, 0.0); // Digital
        params.addChange(Ruinae::kDelaySyncId, 0.0);
        params.addChange(Ruinae::kDelayTimeId, (kFxDelayMs - 1.0) / 4999.0);
        params.addChange(Ruinae::kDelayFeedbackId, 0.0);
        params.addChange(Ruinae::kDelayMixId, 0.5);
        params.addChange(Ruinae::kReverbMixId, 0.5);
        params.addChange(Ruinae::kAmpEnvAttackId, Ruinae::envTimeToNormalized(1.0f));
        params.addChange(Ruinae::kAmpEnvSustainId, 1.0);
        params.addChange(Ruinae::kAmpEnvReleaseId, Ruinae::envTimeToNormalized(5.0f));
        MockEventList noEvents;
        std::vector<float> outL(kFxBlockSize, 0.0f);
        std::vector<float> outR(kFxBlockSize, 0.0f);
        // Two blocks: apply the parameters, then let the slot fade-ins finish.
        processFxBlock(processor, params, noEvents, outL, outR);
        MockParameterChanges noParams;
        processFxBlock(processor, noParams, noEvents, outL, outR);
    }

    const FxTailLevels before = playNoteAndMeasureTails(processor);
    INFO("before reset: reverb RMS " << before.reverbRms << ", echo RMS " << before.echoRms);
    REQUIRE(before.reverbRms > 1e-3f);
    REQUIRE(before.echoRms > 1e-3f);

    SECTION("setActive(false) then setActive(true)") {
        REQUIRE(processor.setActive(false) == Steinberg::kResultTrue);
        REQUIRE(processor.setActive(true) == Steinberg::kResultTrue);
    }

    SECTION("setState of a delay-on state over a delay-on processor") {
        const auto bytes = captureProcessorState(processor);
        REQUIRE_FALSE(bytes.empty());
        loadProcessorState(processor, bytes);
    }

    const FxTailLevels after = playNoteAndMeasureTails(processor);
    INFO("after reset: reverb RMS " << after.reverbRms << ", echo RMS " << after.echoRms);
    CHECK(after.reverbRms > 0.5f * before.reverbRms);
    CHECK(after.echoRms > 0.5f * before.echoRms);

    processor.setActive(false);
    processor.terminate();
}

// =============================================================================
// R-016 / R-118: a SpectralFreeze patch survives an engine reset
// =============================================================================
// setActive(true) and every host preset load reset the engine. The
// SpectralFreeze oscillator dropped its frozen spectrum on reset and was only
// frozen again by a sample-rate change, so the patch went silent.

namespace {

constexpr int kFreezeNoteBlocks = 24;
constexpr int kFreezeReleaseBlocks = 8;

/// Holds one note for kFreezeNoteBlocks blocks, returns the RMS of its second
/// half, then releases it.
float playSpectralFreezeNote(Ruinae::Processor& proc) {
    std::vector<float> outL(kFxBlockSize, 0.0f);
    std::vector<float> outR(kFxBlockSize, 0.0f);
    std::vector<float> captured;
    captured.reserve(static_cast<size_t>(kFreezeNoteBlocks) * kFxBlockSize);

    MockParameterChanges noParams;
    for (int block = 0; block < kFreezeNoteBlocks; ++block) {
        MockEventList events;
        if (block == 0) events.addNoteOn(69, 1.0f);
        processFxBlock(proc, noParams, events, outL, outR);
        captured.insert(captured.end(), outL.begin(), outL.end());
    }
    for (int block = 0; block < kFreezeReleaseBlocks; ++block) {
        MockEventList events;
        if (block == 0) events.addNoteOff(69);
        processFxBlock(proc, noParams, events, outL, outR);
    }
    return windowRms(captured, kFreezeNoteBlocks / 2, kFreezeNoteBlocks);
}

} // namespace

TEST_CASE("SpectralFreeze patch survives setActive cycle and preset reload",
          "[integration][lifecycle]") {
    Ruinae::Processor processor;
    REQUIRE(processor.initialize(nullptr) == Steinberg::kResultTrue);

    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.sampleRate = 44100.0;
    setup.maxSamplesPerBlock = static_cast<Steinberg::int32>(kFxBlockSize);
    REQUIRE(processor.setupProcessing(setup) == Steinberg::kResultTrue);
    REQUIRE(processor.setActive(true) == Steinberg::kResultTrue);

    // OSC A = SpectralFreeze (type 8 of 10), mixer fully on OSC A.
    {
        MockParamChangesWithData params;
        params.addChange(Ruinae::kOscATypeId, 8.0 / 9.0);
        params.addChange(Ruinae::kMixerPositionId, 0.0);
        params.addChange(Ruinae::kAmpEnvAttackId, Ruinae::envTimeToNormalized(1.0f));
        params.addChange(Ruinae::kAmpEnvSustainId, 1.0);
        params.addChange(Ruinae::kAmpEnvReleaseId, Ruinae::envTimeToNormalized(5.0f));
        MockEventList noEvents;
        std::vector<float> outL(kFxBlockSize, 0.0f);
        std::vector<float> outR(kFxBlockSize, 0.0f);
        processFxBlock(processor, params, noEvents, outL, outR);
    }

    const float before = playSpectralFreezeNote(processor);
    INFO("SpectralFreeze note RMS before reset: " << before);
    REQUIRE(before > 1e-2f);

    SECTION("setActive(false), setupProcessing, setActive(true)") {
        REQUIRE(processor.setActive(false) == Steinberg::kResultTrue);
        REQUIRE(processor.setupProcessing(setup) == Steinberg::kResultTrue);
        REQUIRE(processor.setActive(true) == Steinberg::kResultTrue);
    }

    SECTION("setState of the same state twice") {
        const auto bytes = captureProcessorState(processor);
        REQUIRE_FALSE(bytes.empty());
        loadProcessorState(processor, bytes);
        loadProcessorState(processor, bytes);
    }

    const float after = playSpectralFreezeNote(processor);
    INFO("SpectralFreeze note RMS after reset: " << after);
    REQUIRE(after > 0.5f * before);

    processor.setActive(false);
    processor.terminate();
}

// =============================================================================
// R-116: the reported latency matches the measured dry path
// =============================================================================
// The effects chain used to delay the delay slot's wet path by 6144 samples
// while the processor reported 0, so every echo landed 139 ms late. The dry
// path must start exactly getLatencySamples() after the note, with any delay type.

namespace {

/// Plays one note at sample 0 of the first measured block and returns the
/// index of the first left output sample above -60 dBFS (or the window
/// length). The note's first sample is ~0.1 at mix 0.5; the threshold sits
/// above the Tape delay's wet noise floor (~2e-5), which is not a dry onset.
size_t dryOnsetSample(Ruinae::Processor& proc) {
    constexpr int kBlocks = 8;
    std::vector<float> outL(kFxBlockSize, 0.0f);
    std::vector<float> outR(kFxBlockSize, 0.0f);
    MockParameterChanges noParams;
    for (int block = 0; block < kBlocks; ++block) {
        MockEventList events;
        if (block == 0) events.addNoteOn(81, 0.9f);
        processFxBlock(proc, noParams, events, outL, outR);
        for (size_t i = 0; i < kFxBlockSize; ++i) {
            if (std::abs(outL[i]) > 1e-3f) {
                return static_cast<size_t>(block) * kFxBlockSize + i;
            }
        }
    }
    return static_cast<size_t>(kBlocks) * kFxBlockSize;
}

} // namespace

TEST_CASE("Processor_ReportedLatency_MatchesMeasuredDryPath", "[integration][fx][latency]") {
    const int delayType = GENERATE(-1, 0, 1, 2, 3, 4); // -1 = delay slot off
    CAPTURE(delayType);

    Ruinae::Processor processor;
    REQUIRE(processor.initialize(nullptr) == Steinberg::kResultTrue);
    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.sampleRate = 44100.0;
    setup.maxSamplesPerBlock = static_cast<Steinberg::int32>(kFxBlockSize);
    REQUIRE(processor.setupProcessing(setup) == Steinberg::kResultTrue);
    REQUIRE(processor.setActive(true) == Steinberg::kResultTrue);

    {
        MockParamChangesWithData params;
        params.addChange(Ruinae::kAmpEnvAttackId, 0.0);
        params.addChange(Ruinae::kAmpEnvSustainId, 1.0);
        params.addChange(Ruinae::kDelayEnabledId, delayType >= 0 ? 1.0 : 0.0);
        params.addChange(Ruinae::kDelayTypeId, delayType >= 0 ? delayType / 4.0 : 0.0);
        params.addChange(Ruinae::kDelaySyncId, 0.0);
        params.addChange(Ruinae::kDelayTimeId, 249.0 / 4999.0); // 250 ms
        params.addChange(Ruinae::kDelayFeedbackId, 0.0);
        params.addChange(Ruinae::kDelayMixId, 0.5);
        MockEventList noEvents;
        std::vector<float> outL(kFxBlockSize, 0.0f);
        std::vector<float> outR(kFxBlockSize, 0.0f);
        processFxBlock(processor, params, noEvents, outL, outR);
        MockParameterChanges noParams;
        // Let the type switch and the slot fade-in finish (pre-warm 250 ms + 30 ms crossfade).
        for (int block = 0; block < 40; ++block) {
            processFxBlock(processor, noParams, noEvents, outL, outR);
        }
    }

    const size_t onset = dryOnsetSample(processor);
    const auto reported = static_cast<size_t>(processor.getLatencySamples());
    INFO("dry onset at sample " << onset << ", reported latency " << reported);
    // The voice starts within the first block with a 0 ms attack.
    REQUIRE(onset < kFxBlockSize);
    REQUIRE(reported == 0);

    // Same note through a processor whose delay slot is off: the dry path is identical.
    Ruinae::Processor plain;
    REQUIRE(plain.initialize(nullptr) == Steinberg::kResultTrue);
    REQUIRE(plain.setupProcessing(setup) == Steinberg::kResultTrue);
    REQUIRE(plain.setActive(true) == Steinberg::kResultTrue);
    {
        MockParamChangesWithData params;
        params.addChange(Ruinae::kAmpEnvAttackId, 0.0);
        params.addChange(Ruinae::kAmpEnvSustainId, 1.0);
        MockEventList noEvents;
        std::vector<float> outL(kFxBlockSize, 0.0f);
        std::vector<float> outR(kFxBlockSize, 0.0f);
        processFxBlock(plain, params, noEvents, outL, outR);
        MockParameterChanges noParams;
        for (int block = 0; block < 40; ++block) {
            processFxBlock(plain, noParams, noEvents, outL, outR);
        }
    }
    const size_t plainOnset = dryOnsetSample(plain);
    INFO("dry onset with the delay slot off at sample " << plainOnset);
    REQUIRE(onset - plainOnset == reported);

    plain.setActive(false);
    plain.terminate();
    processor.setActive(false);
    processor.terminate();
}
