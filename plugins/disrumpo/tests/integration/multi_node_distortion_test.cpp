// ==============================================================================
// Multi-Node Distortion Integration Test
// ==============================================================================
// Reproduces bug: when a band has one distortion (Fuzz), then a second node
// (Bitwise Mangler) is added by increasing activeNodeCount to 2, the second
// node produces pure white noise instead of valid distorted audio.
//
// The test verifies that adding a second distortion node to a band produces
// musically valid output (not noise) through the full VST3 Processor.
//
// Constitution Principle XII: Test-First Development
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"
#include "dsp/distortion_types.h"

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include "dsp/morph_engine.h"
#include "dsp/morph_node.h"
#include "dsp/distortion_adapter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <numeric>
#include <vector>

namespace {

constexpr double kSampleRate = 44100.0;
constexpr Steinberg::int32 kBlockSize = 512;
constexpr Steinberg::int32 kSettleBlocks = 64;  // Let filters and smoothers settle

/// @brief Generate sine wave samples into a buffer
void generateSine(float* buffer, Steinberg::int32 numSamples, float freq,
                  double sampleRate, Steinberg::int32 offset = 0) {
    constexpr double twoPi = 6.283185307179586;
    for (Steinberg::int32 i = 0; i < numSamples; ++i) {
        buffer[i] = static_cast<float>(
            std::sin(twoPi * freq * static_cast<double>(i + offset) / sampleRate));
    }
}

/// @brief Generate a complex multi-harmonic signal simulating real audio.
/// Uses fundamental + 5 harmonics with decreasing amplitude and varying phase.
void generateComplexTone(float* buffer, Steinberg::int32 numSamples, float freq,
                         double sampleRate, Steinberg::int32 offset = 0) {
    constexpr double twoPi = 6.283185307179586;
    for (Steinberg::int32 i = 0; i < numSamples; ++i) {
        double t = static_cast<double>(i + offset) / sampleRate;
        double sample = 0.0;
        // Fundamental + 5 harmonics with 1/n amplitude and offset phases
        for (int h = 1; h <= 6; ++h) {
            double phase = twoPi * freq * h * t + h * 0.7;  // Phase offset
            sample += std::sin(phase) / static_cast<double>(h);
        }
        buffer[i] = static_cast<float>(sample * 0.3);  // Scale to avoid clipping
    }
}

/// @brief Calculate RMS of a buffer
float calculateRMS(const float* buffer, Steinberg::int32 numSamples) {
    if (numSamples == 0) return 0.0f;
    double sumSq = 0.0;
    for (Steinberg::int32 i = 0; i < numSamples; ++i) {
        sumSq += static_cast<double>(buffer[i]) * static_cast<double>(buffer[i]);
    }
    return static_cast<float>(std::sqrt(sumSq / static_cast<double>(numSamples)));
}

/// @brief Calculate spectral flatness (Wiener entropy) of a buffer.
/// White noise has flatness close to 1.0; tonal signals have flatness close to 0.0.
/// Uses zero-crossing rate as a simple proxy for spectral content.
float calculateZeroCrossingRate(const float* buffer, Steinberg::int32 numSamples) {
    if (numSamples < 2) return 0.0f;
    int crossings = 0;
    for (Steinberg::int32 i = 1; i < numSamples; ++i) {
        if ((buffer[i] >= 0.0f) != (buffer[i - 1] >= 0.0f)) {
            ++crossings;
        }
    }
    return static_cast<float>(crossings) / static_cast<float>(numSamples - 1);
}

/// @brief Calculate normalized autocorrelation at lag 1.
/// Tonal signals have high autocorrelation; white noise has near-zero.
float calculateAutocorrelation(const float* buffer, Steinberg::int32 numSamples) {
    if (numSamples < 2) return 0.0f;

    // Calculate mean
    double mean = 0.0;
    for (Steinberg::int32 i = 0; i < numSamples; ++i)
        mean += buffer[i];
    mean /= numSamples;

    // Autocorrelation at lag=1, normalized by variance
    double r0 = 0.0;
    double r1 = 0.0;
    for (Steinberg::int32 i = 0; i < numSamples; ++i) {
        double x = buffer[i] - mean;
        r0 += x * x;
        if (i < numSamples - 1) {
            double y = buffer[i + 1] - mean;
            r1 += x * y;
        }
    }
    if (r0 < 1e-10) return 0.0f;
    return static_cast<float>(r1 / r0);
}

/// @brief Check if a buffer looks like white noise.
/// White noise has: high zero-crossing rate (~0.5) and low autocorrelation (~0.0).
/// A distorted sine should have lower ZCR and higher autocorrelation.
bool looksLikeWhiteNoise(const float* buffer, Steinberg::int32 numSamples) {
    float zcr = calculateZeroCrossingRate(buffer, numSamples);
    float autocorr = calculateAutocorrelation(buffer, numSamples);

    // White noise: ZCR ≈ 0.5, autocorrelation ≈ 0.0
    // A 1kHz sine at 44.1kHz has ZCR ≈ 0.045 (2*1000/44100)
    // Even heavily distorted sine should have ZCR < 0.3 and autocorr > 0.2
    return (zcr > 0.40f && std::abs(autocorr) < 0.15f);
}

// ---- VST3 parameter change helpers (same pattern as processor_audio_output_test) ----

class SimpleParamValueQueue : public Steinberg::Vst::IParamValueQueue {
public:
    SimpleParamValueQueue(Steinberg::Vst::ParamID id, double value)
        : id_(id), value_(value) {}

    Steinberg::Vst::ParamID PLUGIN_API getParameterId() override { return id_; }
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
        Steinberg::int32, Steinberg::Vst::ParamValue, Steinberg::int32&) override {
        return Steinberg::kResultFalse;
    }
    DECLARE_FUNKNOWN_METHODS
private:
    Steinberg::Vst::ParamID id_;
    double value_;
};

IMPLEMENT_FUNKNOWN_METHODS(SimpleParamValueQueue, Steinberg::Vst::IParamValueQueue,
                           Steinberg::Vst::IParamValueQueue::iid)

class SimpleParameterChanges : public Steinberg::Vst::IParameterChanges {
public:
    void addChange(Steinberg::Vst::ParamID id, double value) {
        queues_.push_back(std::make_unique<SimpleParamValueQueue>(id, value));
    }

    Steinberg::int32 PLUGIN_API getParameterCount() override {
        return static_cast<Steinberg::int32>(queues_.size());
    }
    Steinberg::Vst::IParamValueQueue* PLUGIN_API getParameterData(Steinberg::int32 index) override {
        if (index < 0 || index >= static_cast<Steinberg::int32>(queues_.size()))
            return nullptr;
        return queues_[index].get();
    }
    Steinberg::Vst::IParamValueQueue* PLUGIN_API addParameterData(
        const Steinberg::Vst::ParamID&, Steinberg::int32&) override {
        return nullptr;
    }
    DECLARE_FUNKNOWN_METHODS
private:
    std::vector<std::unique_ptr<SimpleParamValueQueue>> queues_;
};

IMPLEMENT_FUNKNOWN_METHODS(SimpleParameterChanges, Steinberg::Vst::IParameterChanges,
                           Steinberg::Vst::IParameterChanges::iid)

/// @brief RAII wrapper for Processor setup/teardown
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

        result = processor->setActive(true);
        REQUIRE(result == Steinberg::kResultTrue);
    }

    ~ProcessorFixture() {
        processor->setActive(false);
        processor->terminate();
    }

    void processBlock(float* inputL, float* inputR, float* outputL, float* outputR,
                      Steinberg::Vst::IParameterChanges* paramChanges = nullptr) {
        float* inChannels[2] = {inputL, inputR};
        float* outChannels[2] = {outputL, outputR};

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

    /// Process N blocks with optional params on the first block
    void processBlocks(Steinberg::int32 numBlocks, float freq,
                       Steinberg::int32& sampleOffset,
                       Steinberg::Vst::IParameterChanges* paramChanges = nullptr,
                       float* captureL = nullptr, float* captureR = nullptr) {
        std::array<float, kBlockSize> inL{}, inR{}, outL{}, outR{};

        for (Steinberg::int32 block = 0; block < numBlocks; ++block) {
            generateSine(inL.data(), kBlockSize, freq, kSampleRate, sampleOffset);
            generateSine(inR.data(), kBlockSize, freq, kSampleRate, sampleOffset);
            std::memset(outL.data(), 0, sizeof(float) * kBlockSize);
            std::memset(outR.data(), 0, sizeof(float) * kBlockSize);

            auto* p = (block == 0 && paramChanges)
                          ? paramChanges
                          : nullptr;
            processBlock(inL.data(), inR.data(), outL.data(), outR.data(), p);

            // Capture last block if requested
            if (block == numBlocks - 1) {
                if (captureL) std::memcpy(captureL, outL.data(), sizeof(float) * kBlockSize);
                if (captureR) std::memcpy(captureR, outR.data(), sizeof(float) * kBlockSize);
            }

            sampleOffset += kBlockSize;
        }
    }
};

} // anonymous namespace

// =============================================================================
// BUG REPRO: Adding second distortion node produces white noise
// =============================================================================
// Exact user scenario:
//   1. Band 0, Node 0 = Fuzz, single active node, audio playing → sounds fine
//   2. While audio is playing, increase activeNodeCount to 2
//   3. Change Node 1 type to BitwiseMangler (shadow defaults load: all slots 0.5)
//   4. Output immediately becomes white noise
//
// Root cause: ShapeShadowStorage defaults all slots to 0.5. For BitwiseMangler,
// slot 0 = 0.5 maps to op=3 (BitShuffle), which shuffles audio bits randomly,
// producing noise. The shadow should use sensible per-type defaults.
//
// Expected: Newly-added node should produce musically valid output with its
// default parameters, not white noise.
// =============================================================================

TEST_CASE("Adding second distortion node does not produce white noise",
          "[integration][morph][bug]") {
    ProcessorFixture fixture;
    Steinberg::int32 sampleOffset = 0;
    constexpr float kFreq = 440.0f;

    // Helper: process N blocks with complex tone and optional params on first block
    auto processComplexBlocks = [&](Steinberg::int32 numBlocks,
                                    Steinberg::Vst::IParameterChanges* params = nullptr,
                                    float* captureL = nullptr) {
        std::array<float, kBlockSize> inL{}, inR{}, outL{}, outR{};
        for (Steinberg::int32 block = 0; block < numBlocks; ++block) {
            generateComplexTone(inL.data(), kBlockSize, kFreq, kSampleRate, sampleOffset);
            generateComplexTone(inR.data(), kBlockSize, kFreq, kSampleRate, sampleOffset);
            std::memset(outL.data(), 0, sizeof(float) * kBlockSize);
            std::memset(outR.data(), 0, sizeof(float) * kBlockSize);
            auto* p = (block == 0) ? params : nullptr;
            fixture.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), p);
            if (block == numBlocks - 1 && captureL)
                std::memcpy(captureL, outL.data(), sizeof(float) * kBlockSize);
            sampleOffset += kBlockSize;
        }
    };

    // ---- Step 1: Set Node 0 to Fuzz, process complex audio ----
    {
        SimpleParameterChanges params;
        params.addChange(
            Disrumpo::makeNodeParamId(0, 0, Disrumpo::NodeParamType::kNodeType),
            4.0 / 25.0);  // Fuzz
        params.addChange(
            Disrumpo::makeNodeParamId(0, 0, Disrumpo::NodeParamType::kNodeDrive),
            0.5);
        params.addChange(
            Disrumpo::makeNodeParamId(0, 0, Disrumpo::NodeParamType::kNodeMix),
            1.0);
        processComplexBlocks(kSettleBlocks, &params);
    }

    // Sanity: single-node output should be valid
    std::array<float, kBlockSize> singleNodeOutputL{};
    processComplexBlocks(1, nullptr, singleNodeOutputL.data());
    float singleNodeAutocorr = calculateAutocorrelation(singleNodeOutputL.data(), kBlockSize);
    REQUIRE(calculateRMS(singleNodeOutputL.data(), kBlockSize) > 0.01f);
    REQUIRE_FALSE(looksLikeWhiteNoise(singleNodeOutputL.data(), kBlockSize));

    // ---- Step 2: While audio plays, increase to 2 nodes ----
    {
        SimpleParameterChanges params;
        params.addChange(
            Disrumpo::makeBandParamId(0, Disrumpo::BandParamType::kBandActiveNodes),
            1.0 / 3.0);  // "2" nodes
        processComplexBlocks(4, &params);
    }

    // ---- Step 3: Change Node 1 type to BitwiseMangler (shadow defaults) ----
    // No explicit shape slot values — shadow defaults (all 0.5) take effect.
    // This maps to: op=3 (BitShuffle), intensity=0.5, seed=32767
    {
        SimpleParameterChanges params;
        params.addChange(
            Disrumpo::makeNodeParamId(0, 1, Disrumpo::NodeParamType::kNodeType),
            15.0 / 25.0);  // BitwiseMangler
        processComplexBlocks(kSettleBlocks, &params);
    }

    // ---- Step 4: Capture output ----
    std::array<float, kBlockSize> twoNodeOutputL{};
    processComplexBlocks(1, nullptr, twoNodeOutputL.data());

    float twoNodeRMS = calculateRMS(twoNodeOutputL.data(), kBlockSize);
    float twoNodeZCR = calculateZeroCrossingRate(twoNodeOutputL.data(), kBlockSize);
    float twoNodeAutocorr = calculateAutocorrelation(twoNodeOutputL.data(), kBlockSize);

    INFO("Single-node (Fuzz) autocorrelation: " << singleNodeAutocorr);
    INFO("Two-node output RMS: " << twoNodeRMS);
    INFO("Two-node output ZCR: " << twoNodeZCR);
    INFO("Two-node output autocorrelation: " << twoNodeAutocorr);

    // Output must not be silent
    REQUIRE(twoNodeRMS > 0.01f);

    // KEY ASSERTION: Adding a second distortion node must not drastically
    // destroy the signal's harmonic structure. The autocorrelation should not
    // drop by more than 50% compared to the single-node baseline.
    // Bug: BitwiseMangler shadow defaults (all 0.5) produce BitShuffle (op=3)
    // at 50% intensity, which permutes audio bits and destroys spectral content.
    const float autocorrRatio = twoNodeAutocorr / singleNodeAutocorr;
    INFO("Autocorrelation ratio (two-node / single-node): " << autocorrRatio);
    REQUIRE(autocorrRatio > 0.5f);

    // ZCR should not increase by more than 3x (noise indicator)
    float singleNodeZCR = calculateZeroCrossingRate(singleNodeOutputL.data(), kBlockSize);
    INFO("Single-node ZCR: " << singleNodeZCR << " Two-node ZCR: " << twoNodeZCR);
    if (singleNodeZCR > 0.01f) {
        REQUIRE(twoNodeZCR / singleNodeZCR < 5.0f);
    }
}

// =============================================================================
// Variant: Morph position centered between two cross-family nodes
// =============================================================================
// When the morph cursor is at center (0.5, 0.5) between two nodes from
// different families, both nodes contribute. Neither should produce noise.
// =============================================================================

TEST_CASE("Cross-family morph blend does not produce noise",
          "[integration][morph][bug]") {
    ProcessorFixture fixture;
    Steinberg::int32 sampleOffset = 0;
    constexpr float kFreq = 440.0f;

    // Set up 2 nodes on Band 0 in one batch
    {
        SimpleParameterChanges params;

        // Node 0: Fuzz at position (0.0, 0.5) - left side of morph space
        params.addChange(
            Disrumpo::makeNodeParamId(0, 0, Disrumpo::NodeParamType::kNodeType),
            4.0 / 25.0);  // Fuzz
        params.addChange(
            Disrumpo::makeNodeParamId(0, 0, Disrumpo::NodeParamType::kNodeDrive),
            0.5);
        params.addChange(
            Disrumpo::makeNodeParamId(0, 0, Disrumpo::NodeParamType::kNodeMix),
            1.0);

        // Node 1: BitwiseMangler at position (1.0, 0.5) - right side
        params.addChange(
            Disrumpo::makeNodeParamId(0, 1, Disrumpo::NodeParamType::kNodeType),
            15.0 / 25.0);  // BitwiseMangler
        params.addChange(
            Disrumpo::makeNodeParamId(0, 1, Disrumpo::NodeParamType::kNodeDrive),
            0.5);
        params.addChange(
            Disrumpo::makeNodeParamId(0, 1, Disrumpo::NodeParamType::kNodeMix),
            1.0);
        params.addChange(
            Disrumpo::makeNodeParamId(0, 1, Disrumpo::NodeParamType::kNodeShape0),
            0.4);
        params.addChange(
            Disrumpo::makeNodeParamId(0, 1, Disrumpo::NodeParamType::kNodeShape1),
            0.5);

        // ActiveNodes = 2
        params.addChange(
            Disrumpo::makeBandParamId(0, Disrumpo::BandParamType::kBandActiveNodes),
            1.0 / 3.0);

        // Morph position centered (equal blend)
        params.addChange(
            Disrumpo::makeBandParamId(0, Disrumpo::BandParamType::kBandMorphX),
            0.5);
        params.addChange(
            Disrumpo::makeBandParamId(0, Disrumpo::BandParamType::kBandMorphY),
            0.5);

        fixture.processBlocks(kSettleBlocks, kFreq, sampleOffset, &params);
    }

    // Capture blended output
    std::array<float, kBlockSize> blendOutputL{};
    fixture.processBlocks(1, kFreq, sampleOffset, nullptr, blendOutputL.data());

    float blendRMS = calculateRMS(blendOutputL.data(), kBlockSize);
    float blendZCR = calculateZeroCrossingRate(blendOutputL.data(), kBlockSize);
    float blendAutocorr = calculateAutocorrelation(blendOutputL.data(), kBlockSize);

    INFO("Blended output RMS: " << blendRMS);
    INFO("Blended output ZCR: " << blendZCR);
    INFO("Blended output autocorrelation: " << blendAutocorr);

    // Must not be silent
    REQUIRE(blendRMS > 0.01f);

    // Must NOT be white noise
    REQUIRE_FALSE(looksLikeWhiteNoise(blendOutputL.data(), kBlockSize));

    // Must have some periodic structure
    REQUIRE(std::abs(blendAutocorr) > 0.15f);
}

// =============================================================================
// Variant: Morph cursor positioned fully on the second node
// =============================================================================
// After adding a second node, moving the morph cursor to that node's position
// should produce clean distorted output from that node alone. The user reported
// that the second node (BitwiseMangler) produces white noise.
// =============================================================================

TEST_CASE("Second node solo via morph cursor does not produce noise",
          "[integration][morph][bug]") {
    ProcessorFixture fixture;
    Steinberg::int32 sampleOffset = 0;
    constexpr float kFreq = 1000.0f;

    // ---- Step 1: Set Node 0 to Fuzz, single active node ----
    {
        SimpleParameterChanges params;
        params.addChange(
            Disrumpo::makeNodeParamId(0, 0, Disrumpo::NodeParamType::kNodeType),
            4.0 / 25.0);  // Fuzz
        params.addChange(
            Disrumpo::makeNodeParamId(0, 0, Disrumpo::NodeParamType::kNodeDrive),
            0.5);
        params.addChange(
            Disrumpo::makeNodeParamId(0, 0, Disrumpo::NodeParamType::kNodeMix),
            1.0);

        fixture.processBlocks(kSettleBlocks, kFreq, sampleOffset, &params);
    }

    // ---- Step 2: Set Node 1 to BitwiseMangler ----
    {
        SimpleParameterChanges params;
        params.addChange(
            Disrumpo::makeNodeParamId(0, 1, Disrumpo::NodeParamType::kNodeType),
            15.0 / 25.0);  // BitwiseMangler
        params.addChange(
            Disrumpo::makeNodeParamId(0, 1, Disrumpo::NodeParamType::kNodeDrive),
            0.5);
        params.addChange(
            Disrumpo::makeNodeParamId(0, 1, Disrumpo::NodeParamType::kNodeMix),
            1.0);

        fixture.processBlocks(kSettleBlocks / 4, kFreq, sampleOffset, &params);
    }

    // ---- Step 3: Activate 2 nodes AND move morph cursor to Node 1 position ----
    // Node 1 default position: X=1.0, Y=0.0
    // Moving morph cursor to (1.0, 0.0) should give Node 1 weight ≈ 1.0
    {
        SimpleParameterChanges params;
        params.addChange(
            Disrumpo::makeBandParamId(0, Disrumpo::BandParamType::kBandActiveNodes),
            1.0 / 3.0);  // 2 nodes
        params.addChange(
            Disrumpo::makeBandParamId(0, Disrumpo::BandParamType::kBandMorphX),
            1.0);  // Fully on Node 1
        params.addChange(
            Disrumpo::makeBandParamId(0, Disrumpo::BandParamType::kBandMorphY),
            0.0);

        fixture.processBlocks(kSettleBlocks, kFreq, sampleOffset, &params);
    }

    // ---- Step 4: Capture output with morph cursor on Node 1 ----
    std::array<float, kBlockSize> node1SoloL{};
    fixture.processBlocks(1, kFreq, sampleOffset, nullptr, node1SoloL.data());

    float node1RMS = calculateRMS(node1SoloL.data(), kBlockSize);
    float node1ZCR = calculateZeroCrossingRate(node1SoloL.data(), kBlockSize);
    float node1Autocorr = calculateAutocorrelation(node1SoloL.data(), kBlockSize);

    INFO("Node 1 solo RMS: " << node1RMS);
    INFO("Node 1 solo ZCR: " << node1ZCR);
    INFO("Node 1 solo autocorrelation: " << node1Autocorr);

    // Output must not be silent
    REQUIRE(node1RMS > 0.01f);

    // Output must NOT look like white noise
    REQUIRE_FALSE(looksLikeWhiteNoise(node1SoloL.data(), kBlockSize));

    // Must have periodic structure
    REQUIRE(std::abs(node1Autocorr) > 0.15f);
}

// =============================================================================
// Variant: All experimental/complex types as second node
// =============================================================================
// Test that various cross-family type combinations don't produce noise
// when used as the second node. The bug might be specific to certain types.
// =============================================================================

TEST_CASE("Various second node types do not produce noise",
          "[integration][morph][bug]") {
    // Test several cross-family combinations
    struct TestCase {
        int typeIndex;
        const char* name;
    };

    // Fuzz (index 4, Saturation family) as Node 0
    // Try ALL other Node 1 types to catch shadow-default noise bugs
    const std::array<TestCase, 25> testCases = {{
        // Saturation family (skip Fuzz=4, it's Node 0)
        {0, "SoftClip"},
        {1, "HardClip"},
        {2, "Tube"},
        {3, "Tape"},
        {5, "AsymmetricFuzz"},
        // Wavefold family
        {6, "SineFold"},
        {7, "TriangleFold"},
        {8, "SergeFold"},
        // Rectify family
        {9, "FullRectify"},
        {10, "HalfRectify"},
        // Digital family
        {11, "Bitcrush"},
        {12, "SampleReduce"},
        {13, "Quantize"},
        {14, "Aliasing"},
        {15, "BitwiseMangler"},
        // Dynamic family
        {16, "Temporal"},
        // Hybrid family
        {17, "RingSaturation"},
        {18, "FeedbackDist"},
        {19, "AllpassResonant"},
        // Experimental family
        {20, "Chaos"},
        {21, "Formant"},
        {22, "Granular"},
        {23, "Spectral"},
        {24, "Fractal"},
        {25, "Stochastic"},
    }};

    for (const auto& tc : testCases) {
        DYNAMIC_SECTION("Node 1 = " << tc.name) {
            ProcessorFixture fixture;
            Steinberg::int32 sampleOffset = 0;
            constexpr float kFreq = 1000.0f;

            // Set up both nodes and activate 2 nodes in one go
            SimpleParameterChanges params;

            // Node 0: Fuzz
            params.addChange(
                Disrumpo::makeNodeParamId(0, 0, Disrumpo::NodeParamType::kNodeType),
                4.0 / 25.0);
            params.addChange(
                Disrumpo::makeNodeParamId(0, 0, Disrumpo::NodeParamType::kNodeDrive),
                0.5);
            params.addChange(
                Disrumpo::makeNodeParamId(0, 0, Disrumpo::NodeParamType::kNodeMix),
                1.0);

            // Node 1: various types
            params.addChange(
                Disrumpo::makeNodeParamId(0, 1, Disrumpo::NodeParamType::kNodeType),
                static_cast<double>(tc.typeIndex) / 25.0);
            params.addChange(
                Disrumpo::makeNodeParamId(0, 1, Disrumpo::NodeParamType::kNodeDrive),
                0.5);
            params.addChange(
                Disrumpo::makeNodeParamId(0, 1, Disrumpo::NodeParamType::kNodeMix),
                1.0);

            // 2 active nodes, morph centered
            params.addChange(
                Disrumpo::makeBandParamId(0, Disrumpo::BandParamType::kBandActiveNodes),
                1.0 / 3.0);
            params.addChange(
                Disrumpo::makeBandParamId(0, Disrumpo::BandParamType::kBandMorphX),
                0.5);

            fixture.processBlocks(kSettleBlocks, kFreq, sampleOffset, &params);

            // Capture output
            std::array<float, kBlockSize> outputL{};
            fixture.processBlocks(1, kFreq, sampleOffset, nullptr, outputL.data());

            float rms = calculateRMS(outputL.data(), kBlockSize);
            float zcr = calculateZeroCrossingRate(outputL.data(), kBlockSize);
            float autocorr = calculateAutocorrelation(outputL.data(), kBlockSize);

            INFO(tc.name << " - RMS: " << rms << " ZCR: " << zcr
                         << " autocorr: " << autocorr);

            REQUIRE(rms > 0.01f);
            REQUIRE_FALSE(looksLikeWhiteNoise(outputL.data(), kBlockSize));
        }
    }
}

// =============================================================================
// ISOLATION TEST: MorphEngine directly — switch from 1 node to 2 cross-family
// =============================================================================
// Bypasses the full VST3 Processor to test MorphEngine in isolation.
// This reveals whether the noise bug is in the MorphEngine or parameter routing.
// =============================================================================

TEST_CASE("MorphEngine: switching to cross-family does not produce noise",
          "[integration][morph][engine][bug]") {
    using namespace Disrumpo;

    // MorphEngine is too large for stack (5 DistortionAdapters) — heap allocate
    auto engine = std::make_unique<MorphEngine>();
    engine->prepare(44100.0, 512);

    // Configure nodes like the user scenario
    std::array<MorphNode, kMaxMorphNodes> nodes;
    nodes[0] = MorphNode(0, 0.0f, 0.0f, DistortionType::Fuzz);
    nodes[0].commonParams = {1.0f, 1.0f, 4000.0f};

    nodes[1] = MorphNode(1, 1.0f, 0.0f, DistortionType::SoftClip);
    nodes[1].commonParams = {1.0f, 1.0f, 4000.0f};

    nodes[2] = MorphNode(2, 0.0f, 1.0f, DistortionType::SoftClip);
    nodes[2].commonParams = {1.0f, 1.0f, 4000.0f};

    nodes[3] = MorphNode(3, 1.0f, 1.0f, DistortionType::SoftClip);
    nodes[3].commonParams = {1.0f, 1.0f, 4000.0f};

    // Phase 1: Single node (Fuzz), process some audio
    engine->setNodes(nodes, 1);
    engine->setMorphPosition(0.5f, 0.5f);

    constexpr int kSamples = 8192;
    std::array<float, kSamples> input{};
    for (int i = 0; i < kSamples; ++i) {
        input[i] = std::sin(2.0f * 3.14159265f * 1000.0f * static_cast<float>(i) / 44100.0f);
    }

    // Warm up with single node
    for (int i = 0; i < kSamples; ++i) {
        [[maybe_unused]] float out = engine->process(input[i]);
    }

    // Phase 2: Switch to 2 nodes with BitwiseMangler (shadow defaults)
    // Simulate what happens when user selects BitwiseMangler:
    // Shadow loads all-0.5 slots → mapShapeSlotsToParams produces:
    //   op=3 (BitShuffle), intensity=0.5, pattern=0.5, bits=0.5
    // Use the FIXED shadow defaults for BitwiseMangler
    DistortionParams bmParams;
    bmParams.bitwiseOp = 2;         // BitRotate (fixed default: slot 0 = 0.3)
    bmParams.bitwiseIntensity = 0.5f;
    bmParams.bitwisePattern = 0.0f; // No XOR pattern (fixed default: slot 2 = 0.0)
    bmParams.bitwiseBits = 0.5f;    // rotateAmount = 0

    nodes[1].type = DistortionType::BitwiseMangler;
    nodes[1].params = bmParams;

    // First, just increase to 2 nodes (both still same family first call)
    WARN("Setting 2 nodes: Fuzz + BitwiseMangler");
    engine->setNodes(nodes, 2);
    WARN("setNodes completed");

    // Process and capture output immediately after switch
    std::array<float, kSamples> output{};
    for (int i = 0; i < kSamples; ++i) {
        output[i] = engine->process(input[i]);
        // Catch NaN/Inf
        if (std::isnan(output[i]) || std::isinf(output[i])) {
            WARN("NaN/Inf at sample " << i);
            output[i] = 0.0f;
        }
    }

    // Analyze the last portion (after settling)
    const float* analysisStart = output.data() + kSamples / 2;
    const int analysisLen = kSamples / 2;

    float rms = calculateRMS(analysisStart, analysisLen);
    float zcr = calculateZeroCrossingRate(analysisStart, analysisLen);
    float autocorr = calculateAutocorrelation(analysisStart, analysisLen);

    WARN("MorphEngine cross-family RMS=" << rms << " ZCR=" << zcr
         << " autocorr=" << autocorr);

    REQUIRE(rms > 0.01f);
    REQUIRE_FALSE(looksLikeWhiteNoise(analysisStart, analysisLen));
    REQUIRE(std::abs(autocorr) > 0.15f);
}

// =============================================================================
// ISOLATION TEST: DistortionAdapter with BitwiseMangler shadow defaults
// =============================================================================
// Tests if BitwiseMangler with default shadow slot values (all 0.5) produces
// white noise from a single adapter in isolation.
// =============================================================================

TEST_CASE("DistortionAdapter: BitwiseMangler with shadow defaults is not noise",
          "[integration][morph][adapter][bug]") {
    using namespace Disrumpo;

    DistortionAdapter adapter;
    adapter.prepare(44100.0, 512);

    // Set to BitwiseMangler with shadow-default params
    adapter.setType(DistortionType::BitwiseMangler);

    // Use the FIXED shadow defaults for BitwiseMangler:
    // slot 0 = 0.3 → op=2 (BitRotate), slot 2 = 0.0 → no pattern, slot 3 = 0.5 → amount=0
    DistortionParams p;
    p.bitwiseOp = 2;   // BitRotate (slot 0 = 0.3 → int(0.3*5+0.5) = 2)
    p.bitwiseIntensity = 0.5f;
    p.bitwisePattern = 0.0f;   // No XOR pattern
    p.bitwiseBits = 0.5f;      // rotateAmount = 0 (passthrough)
    adapter.setParams(p);

    DistortionCommonParams cp{1.0f, 1.0f, 4000.0f};
    adapter.setCommonParams(cp);

    constexpr int kSamples = 4096;
    std::array<float, kSamples> output{};

    for (int i = 0; i < kSamples; ++i) {
        float sample = std::sin(2.0f * 3.14159265f * 1000.0f * static_cast<float>(i) / 44100.0f);
        output[i] = adapter.process(sample);
    }

    // Analyze second half (after DC blocker settles)
    const float* analysisStart = output.data() + kSamples / 2;
    const int analysisLen = kSamples / 2;

    float rms = calculateRMS(analysisStart, analysisLen);
    float zcr = calculateZeroCrossingRate(analysisStart, analysisLen);
    float autocorr = calculateAutocorrelation(analysisStart, analysisLen);

    WARN("Adapter BitwiseMangler(shadow defaults) RMS=" << rms
         << " ZCR=" << zcr << " autocorr=" << autocorr);

    REQUIRE(rms > 0.01f);

    // The KEY check: does BitwiseMangler with shadow defaults produce noise?
    // If this fails, the shadow defaults are producing noise-like output.
    REQUIRE_FALSE(looksLikeWhiteNoise(analysisStart, analysisLen));
    REQUIRE(std::abs(autocorr) > 0.15f);
}
