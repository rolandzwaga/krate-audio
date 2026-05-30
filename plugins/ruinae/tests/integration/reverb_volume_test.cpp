// ==============================================================================
// Integration Test: Reverb Volume Drop Bug
// ==============================================================================
// When reverb is enabled, the output volume should remain comparable to when
// reverb is off. A significant volume drop indicates a gain staging bug in the
// reverb processing path.
//
// Bug report: Enabling reverb causes significant volume drop.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"

#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <vector>
#include "vst_param_changes.h"
#include "vst_event_list.h"

using Catch::Approx;

// =============================================================================
// Mock classes (same pattern as processor_audio_test.cpp)
// =============================================================================

namespace {

// IEventList mock consolidated into tests/test_helpers/vst_event_list.h
using MockEventList = Krate::Test::EventList;


// Parameter-change mocks consolidated into tests/test_helpers/vst_param_changes.h
using MockParameterChanges = Krate::Test::ParameterChanges;
using MockParamValueQueue = Krate::Test::ParamValueQueue;
using MockParamChangesWithData = Krate::Test::ParameterChanges;






// Helper: compute RMS of a buffer
float computeRMS(const float* buffer, size_t numSamples) {
    double sum = 0.0;
    for (size_t i = 0; i < numSamples; ++i) {
        sum += static_cast<double>(buffer[i]) * static_cast<double>(buffer[i]);
    }
    return static_cast<float>(std::sqrt(sum / static_cast<double>(numSamples)));
}

float findPeak(const float* buffer, size_t numSamples) {
    float peak = 0.0f;
    for (size_t i = 0; i < numSamples; ++i) {
        float absVal = std::abs(buffer[i]);
        if (absVal > peak) peak = absVal;
    }
    return peak;
}

} // anonymous namespace

// =============================================================================
// Test: Reverb enabled should NOT cause significant volume drop
// =============================================================================

TEST_CASE("Reverb enabled does not cause significant volume drop",
          "[processor][integration][reverb][volume-drop]") {
    // Strategy: Two identical Processor instances play the same note.
    // One has reverb OFF (default), the other has reverb ON.
    // Measure RMS levels over several blocks.
    // The reverb-ON output should be within -3dB of the reverb-OFF output.
    // A larger drop indicates a gain staging bug.

    constexpr size_t kBlockSize = 512;
    constexpr int kNumBlocks = 40;  // ~464ms at 44.1kHz - let sound develop

    auto runProcessor = [&](bool enableReverb) -> std::pair<float, float> {
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

        // Block 0: send reverb params + noteOn
        MockParamChangesWithData params;
        if (enableReverb) {
            params.addChange(Ruinae::kReverbEnabledId, 1.0);  // Enable reverb
            // Use default reverb settings (size=0.5, damping=0.5, mix=0.5, etc.)
        }
        data.inputParameterChanges = &params;

        MockEventList events;
        events.addNoteOn(48, 0.9f);  // C3, high velocity
        data.inputEvents = &events;

        proc.process(data);

        // Clear events/params for subsequent blocks
        MockParameterChanges emptyParams;
        MockEventList emptyEvents;
        data.inputParameterChanges = &emptyParams;
        data.inputEvents = &emptyEvents;

        // Collect output over all blocks, compute running RMS and peak
        std::vector<float> allSamples;
        allSamples.insert(allSamples.end(), outL.begin(), outL.end());

        for (int block = 1; block < kNumBlocks; ++block) {
            std::fill(outL.begin(), outL.end(), 0.0f);
            std::fill(outR.begin(), outR.end(), 0.0f);
            proc.process(data);
            allSamples.insert(allSamples.end(), outL.begin(), outL.end());
        }

        float rms = computeRMS(allSamples.data(), allSamples.size());
        float peak = findPeak(allSamples.data(), allSamples.size());

        proc.setActive(false);
        proc.terminate();

        return {rms, peak};
    };

    auto [rmsOff, peakOff] = runProcessor(false);
    auto [rmsOn, peakOn] = runProcessor(true);

    INFO("Reverb OFF - RMS: " << rmsOff << ", Peak: " << peakOff);
    INFO("Reverb ON  - RMS: " << rmsOn << ", Peak: " << peakOn);
    INFO("RMS ratio (ON/OFF): " << (rmsOff > 0 ? rmsOn / rmsOff : 0.0f));
    INFO("Peak ratio (ON/OFF): " << (peakOff > 0 ? peakOn / peakOff : 0.0f));

    // Sanity: both should produce audio
    REQUIRE(rmsOff > 0.001f);
    REQUIRE(rmsOn > 0.001f);

    // The reverb at 50% mix should NOT drop more than 3dB (~0.707x) in RMS.
    // A wet/dry mix of 0.5 means: output = 0.5*dry + 0.5*wet
    // Even if wet is slightly lower (reverb smears energy), the total should
    // stay close to the dry level. A drop below 0.7x indicates a gain bug.
    float rmsRatio = rmsOn / rmsOff;
    REQUIRE(rmsRatio > 0.7f);  // No more than ~3dB drop

    // Peak should also not drop dramatically
    float peakRatio = peakOn / peakOff;
    REQUIRE(peakRatio > 0.6f);
}
