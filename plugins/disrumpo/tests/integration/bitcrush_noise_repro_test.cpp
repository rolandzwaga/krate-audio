// Reproduction test: Bitcrush produces white noise after shape slot knob turn
//
// Full VST3 Processor test matching exact user scenario:
//   1. Fresh plugin → set Node 0 type to Bitcrush
//   2. Process audio → should be bitcrushed sine (periodic structure)
//   3. Turn a shape slot knob (e.g. Bits down)
//   4. Process audio → should STILL be bitcrushed, NOT white noise

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"
#include "dsp/distortion_types.h"
#include "dsp/morph_node.h"

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>
#include "vst_param_changes.h"

namespace {

constexpr double kSampleRate = 44100.0;
constexpr Steinberg::int32 kBlockSize = 512;
constexpr float kFreq = 440.0f;

void generateSine(float* buffer, Steinberg::int32 numSamples, float freq,
                  double sampleRate, Steinberg::int32 offset = 0) {
    constexpr double twoPi = 6.283185307179586;
    for (Steinberg::int32 i = 0; i < numSamples; ++i) {
        buffer[i] = static_cast<float>(
            std::sin(twoPi * freq * static_cast<double>(i + offset) / sampleRate));
    }
}

float calculateRMS(const float* buf, int n) {
    double sum = 0.0;
    for (int i = 0; i < n; ++i) sum += buf[i] * (double)buf[i];
    return static_cast<float>(std::sqrt(sum / n));
}

float calculateAutocorrelation(const float* buf, int n) {
    if (n < 2) return 0.0f;
    double mean = 0.0;
    for (int i = 0; i < n; ++i) mean += buf[i];
    mean /= n;
    double r0 = 0.0;
    double r1 = 0.0;
    for (int i = 0; i < n; ++i) {
        double x = buf[i] - mean;
        r0 += x * x;
        if (i < n - 1) r1 += x * (buf[i + 1] - mean);
    }
    if (r0 < 1e-10) return 0.0f;
    return static_cast<float>(r1 / r0);
}

float calculateZCR(const float* buf, int n) {
    int crossings = 0;
    for (int i = 1; i < n; ++i)
        if ((buf[i] >= 0) != (buf[i - 1] >= 0)) ++crossings;
    return static_cast<float>(crossings) / static_cast<float>(n - 1);
}

bool looksLikeWhiteNoise(const float* buf, int n) {
    float zcr = calculateZCR(buf, n);
    float autocorr = calculateAutocorrelation(buf, n);
    return (zcr > 0.40f && std::abs(autocorr) < 0.15f);
}

// ---- VST3 parameter change helpers ----
// Parameter-change mocks consolidated into tests/test_helpers/vst_param_changes.h
using SimpleParamValueQueue = Krate::Test::ParamValueQueue;
using SimpleParameterChanges = Krate::Test::ParameterChanges;




struct ProcessorFixture {
    std::unique_ptr<Disrumpo::Processor> processor;

    ProcessorFixture() {
        processor = std::make_unique<Disrumpo::Processor>();
        auto result = processor->initialize(nullptr);
        REQUIRE(result == Steinberg::kResultTrue);

        Steinberg::Vst::ProcessSetup setup{};
        setup.processMode = Steinberg::Vst::kRealtime;
        setup.symbolicSampleSize = Steinberg::Vst::kSample32;
        setup.sampleRate = kSampleRate;
        setup.maxSamplesPerBlock = kBlockSize;

        result = processor->setupProcessing(setup);
        REQUIRE(result == Steinberg::kResultTrue);

        result = processor->setActive(static_cast<Steinberg::TBool>(true));
        REQUIRE(result == Steinberg::kResultTrue);
    }

    ~ProcessorFixture() {
        processor->setActive(static_cast<Steinberg::TBool>(false));
        processor->terminate();
    }

    void processBlock(float* inputL, float* inputR, float* outputL, float* outputR,
                      Steinberg::Vst::IParameterChanges* paramChanges = nullptr) const {
        float* inChannels[2] = { inputL, inputR };
        float* outChannels[2] = { outputL, outputR };

        Steinberg::Vst::AudioBusBuffers inputBus{};
        inputBus.numChannels = 2;
        inputBus.channelBuffers32 = inChannels;

        Steinberg::Vst::AudioBusBuffers outputBus{};
        outputBus.numChannels = 2;
        outputBus.channelBuffers32 = outChannels;

        Steinberg::Vst::ProcessData data{};
        data.processMode = Steinberg::Vst::kRealtime;
        data.symbolicSampleSize = Steinberg::Vst::kSample32;
        data.numSamples = kBlockSize;
        data.numInputs = 1;
        data.numOutputs = 1;
        data.inputs = &inputBus;
        data.outputs = &outputBus;
        data.inputParameterChanges = paramChanges;
        data.outputParameterChanges = nullptr;
        data.processContext = nullptr;

        auto result = processor->process(data);
        REQUIRE(result == Steinberg::kResultTrue);
    }
};

} // namespace

TEST_CASE("Full Processor: Bitcrush knob turn does not produce white noise",
          "[integration][bitcrush][processor][bug]") {
    using namespace Disrumpo;

    ProcessorFixture fixture;
    Steinberg::int32 sampleOffset = 0;

    auto processBlocks = [&](int numBlocks,
                             Steinberg::Vst::IParameterChanges* params = nullptr,
                             float* captureL = nullptr) {
        std::array<float, kBlockSize> inL{};
        std::array<float, kBlockSize> inR{};
        std::array<float, kBlockSize> outL{};
        std::array<float, kBlockSize> outR{};
        for (int block = 0; block < numBlocks; ++block) {
            generateSine(inL.data(), kBlockSize, kFreq, kSampleRate, sampleOffset);
            generateSine(inR.data(), kBlockSize, kFreq, kSampleRate, sampleOffset);
            std::memset(outL.data(), 0, sizeof(float) * kBlockSize);
            std::memset(outR.data(), 0, sizeof(float) * kBlockSize);
            auto* p = (block == 0) ? params : nullptr;
            fixture.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), p);
            if (block == numBlocks - 1 && captureL)
                std::memcpy(captureL, outL.data(), sizeof(float) * kBlockSize);
            sampleOffset += kBlockSize;
        }
    };

    // ---- Step 1: Set Node 0 type to Bitcrush ----
    {
        SimpleParameterChanges params;
        // Bitcrush = index 11 → 11/25 = 0.44
        params.addChange(
            makeNodeParamId(0, 0, NodeParamType::kNodeType),
            11.0 / 25.0);
        // Drive = 1.0 → 0.1 normalized (RangeParameter [0,10])
        params.addChange(
            makeNodeParamId(0, 0, NodeParamType::kNodeDrive),
            0.1);
        // Mix = 100% → 1.0
        params.addChange(
            makeNodeParamId(0, 0, NodeParamType::kNodeMix),
            1.0);
        processBlocks(32, &params);
    }

    // ---- Step 2: Capture output BEFORE knob turn ----
    std::array<float, kBlockSize> beforeL{};
    processBlocks(1, nullptr, beforeL.data());

    float rms1 = calculateRMS(beforeL.data(), kBlockSize);
    float ac1 = calculateAutocorrelation(beforeL.data(), kBlockSize);
    float zcr1 = calculateZCR(beforeL.data(), kBlockSize);

    WARN("BEFORE knob turn: RMS=" << rms1 << " autocorr=" << ac1 << " ZCR=" << zcr1);
    REQUIRE(rms1 > 0.001f);

    // ---- Step 3: Turn the Bits knob (Shape Slot 0) down to ~4 bits ----
    // Slot0 = 0.2 → bitDepth = 1.0 + 0.2*15 = 4.0 bits (heavy bitcrush)
    {
        SimpleParameterChanges params;
        params.addChange(
            makeNodeParamId(0, 0, NodeParamType::kNodeShape0),
            0.2);  // Bits = 4.0
        processBlocks(8, &params);
    }

    // ---- Step 4: Capture output AFTER knob turn ----
    std::array<float, kBlockSize> afterL{};
    processBlocks(1, nullptr, afterL.data());

    float rms2 = calculateRMS(afterL.data(), kBlockSize);
    float ac2 = calculateAutocorrelation(afterL.data(), kBlockSize);
    float zcr2 = calculateZCR(afterL.data(), kBlockSize);

    WARN("AFTER knob turn: RMS=" << rms2 << " autocorr=" << ac2 << " ZCR=" << zcr2);

    // Output must not be silent
    REQUIRE(rms2 > 0.001f);

    // KEY CHECK: After turning bits knob, output must NOT be white noise
    INFO("Output after knob turn should be bitcrushed sine, not white noise");
    INFO("ZCR > 0.4 and autocorr < 0.15 = white noise");
    bool isNoise = looksLikeWhiteNoise(afterL.data(), kBlockSize);
    if (isNoise) {
        WARN("*** BUG REPRODUCED: Output is white noise after knob turn! ***");
    }
    REQUIRE_FALSE(isNoise);
    REQUIRE(std::abs(ac2) > 0.15f);
}

TEST_CASE("Full Processor: Bitcrush actually modifies the signal",
          "[integration][bitcrush][processor]") {
    using namespace Disrumpo;

    // One full-range band, so the processor output is the band's node output
    // (wet 100 %, output gain 0 dB) and its quantisation grid stays visible.
    // Both renders get the same input samples; only the node settings differ.
    constexpr int kRenderBlocks = 33;
    auto render = [](Steinberg::Vst::IParameterChanges* params) {
        ProcessorFixture fixture;
        std::array<float, kBlockSize> inL{};
        std::array<float, kBlockSize> inR{};
        std::array<float, kBlockSize> outL{};
        std::array<float, kBlockSize> outR{};
        Steinberg::int32 sampleOffset = 0;
        for (int block = 0; block < kRenderBlocks; ++block) {
            generateSine(inL.data(), kBlockSize, kFreq, kSampleRate, sampleOffset);
            generateSine(inR.data(), kBlockSize, kFreq, kSampleRate, sampleOffset);
            outL.fill(0.0f);
            outR.fill(0.0f);
            fixture.processBlock(inL.data(), inR.data(), outL.data(), outR.data(),
                                 (block == 0) ? params : nullptr);
            sampleOffset += kBlockSize;
        }
        return outL;  // the last block
    };

    SimpleParameterChanges defaultParams;
    defaultParams.addChange(kBandCountId, 0.0);  // 1 band, default SoftClip node
    const auto defaultL = render(&defaultParams);

    // Bitcrush at its lowest bit depth: slot 0 at 0 = 4 bits (the slot spans
    // 4-12 bits since T2.05, so the old "1 bit" setting no longer exists). The
    // crusher's grid is then 2^4 - 1 = 15 steps over [-1, 1], at most 17 values
    // once the +/-7.5 clamp is counted. Jitter (slot 3) is off: it moves the bit
    // depth per sample, off the 4-bit grid.
    SimpleParameterChanges crushParams;
    crushParams.addChange(kBandCountId, 0.0);
    crushParams.addChange(makeNodeParamId(0, 0, NodeParamType::kNodeType), 11.0 / 25.0);  // Bitcrush
    crushParams.addChange(makeNodeParamId(0, 0, NodeParamType::kNodeDrive), 0.5);  // Drive = 5.0
    crushParams.addChange(makeNodeParamId(0, 0, NodeParamType::kNodeMix), 1.0);
    crushParams.addChange(makeNodeParamId(0, 0, NodeParamType::kNodeShape0), 0.0);  // 4 bits
    crushParams.addChange(makeNodeParamId(0, 0, NodeParamType::kNodeShape3), 0.0);  // no jitter
    // Output Gain -6.03 dB (x 0.4994): the crushed full-scale sine is a +-1 staircase, and the
    // output safety clipper (user ruling 2026-11-10) reshapes anything above its -6 dBFS knee, so at
    // 0 dB the processor output would show the clipper's knee, not the crusher's grid. Below the
    // knee the clipper is an exact passthrough; the levels are the grid times one gain.
    constexpr double kOutputGainDb = -6.03;
    crushParams.addChange(kOutputGainId, (kOutputGainDb + 24.0) / 48.0);
    const auto bitcrushL = render(&crushParams);
    const auto outputGain = static_cast<float>(std::pow(10.0, kOutputGainDb / 20.0));

    std::array<float, kBlockSize> diff{};
    for (int i = 0; i < kBlockSize; ++i) diff[i] = bitcrushL[i] - defaultL[i];
    const float rmsDiff = calculateRMS(diff.data(), kBlockSize);

    std::vector<float> levels(bitcrushL.begin(), bitcrushL.end());
    std::sort(levels.begin(), levels.end());
    levels.erase(std::unique(levels.begin(), levels.end()), levels.end());

    INFO("RMS(default) = " << calculateRMS(defaultL.data(), kBlockSize)
         << ", RMS(bitcrush) = " << calculateRMS(bitcrushL.data(), kBlockSize)
         << ", RMS(bitcrush - default) = " << rmsDiff
         << ", distinct bitcrush output values = " << levels.size());
    REQUIRE(calculateRMS(bitcrushL.data(), kBlockSize) / outputGain > 0.5f);  // the node output: not silenced
    REQUIRE(rmsDiff > 0.05f);
    constexpr size_t kMaxLevelsAt4Bits = 17;  // 2^4 + 1
    REQUIRE(levels.size() <= kMaxLevelsAt4Bits);
    REQUIRE_FALSE(looksLikeWhiteNoise(bitcrushL.data(), kBlockSize));
}

TEST_CASE("Full Processor: Quantize knob turn does not produce white noise",
          "[integration][quantize][processor][bug]") {
    using namespace Disrumpo;

    ProcessorFixture fixture;
    Steinberg::int32 sampleOffset = 0;

    auto processBlocks = [&](int numBlocks,
                             Steinberg::Vst::IParameterChanges* params = nullptr,
                             float* captureL = nullptr) {
        std::array<float, kBlockSize> inL{};
        std::array<float, kBlockSize> inR{};
        std::array<float, kBlockSize> outL{};
        std::array<float, kBlockSize> outR{};
        for (int block = 0; block < numBlocks; ++block) {
            generateSine(inL.data(), kBlockSize, kFreq, kSampleRate, sampleOffset);
            generateSine(inR.data(), kBlockSize, kFreq, kSampleRate, sampleOffset);
            std::memset(outL.data(), 0, sizeof(float) * kBlockSize);
            std::memset(outR.data(), 0, sizeof(float) * kBlockSize);
            auto* p = (block == 0) ? params : nullptr;
            fixture.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), p);
            if (block == numBlocks - 1 && captureL)
                std::memcpy(captureL, outL.data(), sizeof(float) * kBlockSize);
            sampleOffset += kBlockSize;
        }
    };

    // ---- Step 1: Set Node 0 type to Quantize ----
    {
        SimpleParameterChanges params;
        // Quantize = index 13 → 13/25 = 0.52
        params.addChange(
            makeNodeParamId(0, 0, NodeParamType::kNodeType),
            13.0 / 25.0);
        params.addChange(
            makeNodeParamId(0, 0, NodeParamType::kNodeDrive),
            0.1);
        params.addChange(
            makeNodeParamId(0, 0, NodeParamType::kNodeMix),
            1.0);
        processBlocks(32, &params);
    }

    // Capture before
    std::array<float, kBlockSize> beforeL{};
    processBlocks(1, nullptr, beforeL.data());
    float rms1 = calculateRMS(beforeL.data(), kBlockSize);
    float ac1 = calculateAutocorrelation(beforeL.data(), kBlockSize);
    WARN("Quantize BEFORE knob: RMS=" << rms1 << " autocorr=" << ac1);

    // ---- Step 2: Turn Levels knob (Slot 0) down to few levels ----
    {
        SimpleParameterChanges params;
        params.addChange(
            makeNodeParamId(0, 0, NodeParamType::kNodeShape0),
            0.05);  // Very few levels
        processBlocks(8, &params);
    }

    // Capture after
    std::array<float, kBlockSize> afterL{};
    processBlocks(1, nullptr, afterL.data());
    float rms2 = calculateRMS(afterL.data(), kBlockSize);
    float ac2 = calculateAutocorrelation(afterL.data(), kBlockSize);
    float zcr2 = calculateZCR(afterL.data(), kBlockSize);

    WARN("Quantize AFTER knob: RMS=" << rms2 << " autocorr=" << ac2 << " ZCR=" << zcr2);

    REQUIRE(rms2 > 0.001f);
    bool isNoise = looksLikeWhiteNoise(afterL.data(), kBlockSize);
    if (isNoise) {
        WARN("*** BUG REPRODUCED: Quantize output is white noise after knob turn! ***");
    }
    REQUIRE_FALSE(isNoise);
    REQUIRE(std::abs(ac2) > 0.15f);
}
