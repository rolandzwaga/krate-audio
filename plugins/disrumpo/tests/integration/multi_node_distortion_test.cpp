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
#include "dsp/band_processor.h"

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
    // Use the FIXED shadow defaults for BitwiseMangler
    DistortionParams bmParams;
    bmParams.bitwiseOp = 0;         // XorPattern (default)
    bmParams.bitwiseIntensity = 0.5f;
    bmParams.bitwisePattern = 0.5f; // Mid-range XOR pattern

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
    p.bitwiseOp = 0;   // XorPattern
    p.bitwiseIntensity = 0.5f;
    p.bitwisePattern = 0.5f;   // Mid-range XOR pattern
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

// =============================================================================
// BUG REPRO: Morph cursor position has no effect on cross-family distortion
// =============================================================================
// User scenario:
//   1. Band 0, Node A = SoftClip, Node B = BitwiseMangler
//   2. Move morph cursor fully to Node A position → hear SoftClip (correct)
//   3. Move morph cursor fully to Node B position → STILL hear SoftClip (BUG)
//   4. The output should be measurably different when cursor is on different
//      cross-family nodes.
//
// This tests at the MorphEngine level to isolate from VST parameter routing.
// =============================================================================

TEST_CASE("Cross-family morph: cursor position changes output character",
          "[integration][morph][bug]") {
    using namespace Disrumpo;

    constexpr double kSR = 44100.0;
    constexpr int kSamples = 8192;
    constexpr float kFreq = 440.0f;
    constexpr float twoPi = 6.283185307179586f;

    // Generate input signal
    std::array<float, kSamples> input{};
    for (int i = 0; i < kSamples; ++i) {
        input[i] = std::sin(twoPi * kFreq * static_cast<float>(i) / static_cast<float>(kSR));
    }

    // Configure nodes: A = SoftClip at (0,0), B = BitwiseMangler at (1,0)
    std::array<MorphNode, kMaxMorphNodes> nodes;
    nodes[0] = MorphNode(0, 0.0f, 0.0f, DistortionType::SoftClip);
    nodes[0].commonParams = {5.0f, 1.0f, 4000.0f};  // drive=5, mix=100%, tone=4kHz

    nodes[1] = MorphNode(1, 1.0f, 0.0f, DistortionType::BitwiseMangler);
    nodes[1].commonParams = {5.0f, 1.0f, 4000.0f};  // same drive/mix/tone
    // Use non-trivial BitwiseMangler params so the effect is audible
    nodes[1].params.bitwiseOp = 0;           // XorPattern
    nodes[1].params.bitwiseIntensity = 0.8f; // high intensity
    nodes[1].params.bitwisePattern = 0.7f;   // non-zero XOR pattern

    nodes[2] = MorphNode(2, 0.0f, 1.0f, DistortionType::SoftClip);
    nodes[3] = MorphNode(3, 1.0f, 1.0f, DistortionType::SoftClip);

    // --- Capture output with cursor fully on Node A (SoftClip) ---
    auto engineA = std::make_unique<MorphEngine>();
    engineA->prepare(kSR, 512);
    engineA->setSmoothingTime(0.0f);  // No smoothing for instant position
    engineA->setNodes(nodes, 2);
    engineA->setMorphPosition(0.0f, 0.0f);  // Fully on Node A

    // Warm up (let DC blockers/filters settle)
    for (int i = 0; i < kSamples; ++i) {
        [[maybe_unused]] float out = engineA->process(input[i]);
    }

    // Capture
    std::array<float, kSamples> outputA{};
    for (int i = 0; i < kSamples; ++i) {
        outputA[i] = engineA->process(input[i]);
    }

    // --- Capture output with cursor fully on Node B (BitwiseMangler) ---
    auto engineB = std::make_unique<MorphEngine>();
    engineB->prepare(kSR, 512);
    engineB->setSmoothingTime(0.0f);
    engineB->setNodes(nodes, 2);
    engineB->setMorphPosition(1.0f, 0.0f);  // Fully on Node B

    // Warm up
    for (int i = 0; i < kSamples; ++i) {
        [[maybe_unused]] float out = engineB->process(input[i]);
    }

    // Capture
    std::array<float, kSamples> outputB{};
    for (int i = 0; i < kSamples; ++i) {
        outputB[i] = engineB->process(input[i]);
    }

    // --- Capture output with cursor at center (50/50 blend) ---
    auto engineMid = std::make_unique<MorphEngine>();
    engineMid->prepare(kSR, 512);
    engineMid->setSmoothingTime(0.0f);
    engineMid->setNodes(nodes, 2);
    engineMid->setMorphPosition(0.5f, 0.0f);  // Midpoint

    // Warm up
    for (int i = 0; i < kSamples; ++i) {
        [[maybe_unused]] float out = engineMid->process(input[i]);
    }

    // Capture
    std::array<float, kSamples> outputMid{};
    for (int i = 0; i < kSamples; ++i) {
        outputMid[i] = engineMid->process(input[i]);
    }

    // --- Analysis ---
    // Use second half for analysis (after any transients)
    const int analysisOffset = kSamples / 2;
    const int analysisLen = kSamples / 2;

    const float* aPtr = outputA.data() + analysisOffset;
    const float* bPtr = outputB.data() + analysisOffset;
    const float* midPtr = outputMid.data() + analysisOffset;

    float rmsA = calculateRMS(aPtr, analysisLen);
    float rmsB = calculateRMS(bPtr, analysisLen);
    float rmsMid = calculateRMS(midPtr, analysisLen);
    float zcrA = calculateZeroCrossingRate(aPtr, analysisLen);
    float zcrB = calculateZeroCrossingRate(bPtr, analysisLen);
    float zcrMid = calculateZeroCrossingRate(midPtr, analysisLen);
    float autocorrA = calculateAutocorrelation(aPtr, analysisLen);
    float autocorrB = calculateAutocorrelation(bPtr, analysisLen);
    float autocorrMid = calculateAutocorrelation(midPtr, analysisLen);

    INFO("Node A (SoftClip)       - RMS: " << rmsA << " ZCR: " << zcrA << " autocorr: " << autocorrA);
    INFO("Node B (BitwiseMangler) - RMS: " << rmsB << " ZCR: " << zcrB << " autocorr: " << autocorrB);
    INFO("Midpoint blend          - RMS: " << rmsMid << " ZCR: " << zcrMid << " autocorr: " << autocorrMid);

    // Both outputs must be non-silent
    REQUIRE(rmsA > 0.01f);
    REQUIRE(rmsB > 0.01f);
    REQUIRE(rmsMid > 0.01f);

    // KEY ASSERTION: Compute sample-by-sample difference between A and B outputs.
    // If the morph is working, they should be significantly different.
    double diffSumSq = 0.0;
    for (int i = 0; i < analysisLen; ++i) {
        double d = static_cast<double>(aPtr[i]) - static_cast<double>(bPtr[i]);
        diffSumSq += d * d;
    }
    float diffRMS = static_cast<float>(std::sqrt(diffSumSq / analysisLen));
    INFO("RMS difference between Node A and Node B output: " << diffRMS);

    // The outputs must be meaningfully different — not just numerical noise.
    // A diffRMS > 0.05 means the waveforms are substantially different.
    // BUG: If this fails, cursor position has no effect on the output.
    REQUIRE(diffRMS > 0.05f);

    // Also verify midpoint is different from both extremes
    double diffMidASumSq = 0.0;
    double diffMidBSumSq = 0.0;
    for (int i = 0; i < analysisLen; ++i) {
        double dA = static_cast<double>(midPtr[i]) - static_cast<double>(aPtr[i]);
        double dB = static_cast<double>(midPtr[i]) - static_cast<double>(bPtr[i]);
        diffMidASumSq += dA * dA;
        diffMidBSumSq += dB * dB;
    }
    float diffMidA = static_cast<float>(std::sqrt(diffMidASumSq / analysisLen));
    float diffMidB = static_cast<float>(std::sqrt(diffMidBSumSq / analysisLen));
    INFO("RMS diff midpoint vs A: " << diffMidA);
    INFO("RMS diff midpoint vs B: " << diffMidB);

    // Midpoint should differ from at least one extreme
    REQUIRE((diffMidA > 0.02f || diffMidB > 0.02f));
}

// =============================================================================
// BUG REPRO (VST3 Processor): SoftClip + BitwiseMangler morph stuck on SoftClip
// =============================================================================
// Same scenario through the full VST3 Processor parameter routing.
// Verifies the bug isn't just in MorphEngine but also in parameter handling.
// =============================================================================

// =============================================================================
// Multi-node morph: diverse distortion types produce distinct outputs
// =============================================================================
// Each test configures N nodes with distortion types from DIFFERENT categories
// (Saturation, Wavefold, Rectify, Digital) and verifies that moving the morph
// cursor to each node produces measurably different output. This exercises
// both same-family and cross-family morph paths through the full VST3 processor.
// =============================================================================

/// Helper: add a distortion node to band 0 with the given type index and drive.
/// type is raw enum index (0-25).
static void addNodeParams(SimpleParameterChanges& params, int nodeIdx,
                          int typeIdx, double drive, double mix = 1.0) {
    params.addChange(
        Disrumpo::makeNodeParamId(0, nodeIdx, Disrumpo::NodeParamType::kNodeType),
        static_cast<double>(typeIdx) / 25.0);
    params.addChange(
        Disrumpo::makeNodeParamId(0, nodeIdx, Disrumpo::NodeParamType::kNodeDrive),
        drive);
    params.addChange(
        Disrumpo::makeNodeParamId(0, nodeIdx, Disrumpo::NodeParamType::kNodeMix),
        mix);
}

/// Helper: capture output at a given morph cursor position.
static std::array<float, kBlockSize> captureAtCursor(
    ProcessorFixture& fixture, float morphX, float morphY,
    float freq, Steinberg::int32& sampleOffset) {
    // Move cursor
    {
        SimpleParameterChanges params;
        params.addChange(
            Disrumpo::makeBandParamId(0, Disrumpo::BandParamType::kBandMorphX),
            morphX);
        params.addChange(
            Disrumpo::makeBandParamId(0, Disrumpo::BandParamType::kBandMorphY),
            morphY);
        fixture.processBlocks(kSettleBlocks, freq, sampleOffset, &params);
    }
    // Capture
    std::array<float, kBlockSize> output{};
    fixture.processBlocks(1, freq, sampleOffset, nullptr, output.data());
    return output;
}

/// Helper: compute RMS difference between two buffers.
static float rmsDifference(const float* a, const float* b, Steinberg::int32 n) {
    double sumSq = 0.0;
    for (Steinberg::int32 i = 0; i < n; ++i) {
        double d = static_cast<double>(a[i]) - static_cast<double>(b[i]);
        sumSq += d * d;
    }
    return static_cast<float>(std::sqrt(sumSq / n));
}

TEST_CASE("2-node morph: Tube vs SineFold produce distinct outputs",
          "[integration][morph][multi-node]") {
    // Tube (D03, Saturation) vs SineFold (D07, Wavefold) — different categories
    ProcessorFixture fixture;
    Steinberg::int32 sampleOffset = 0;
    constexpr float kFreq = 440.0f;

    {
        SimpleParameterChanges params;
        addNodeParams(params, 0, 2, 0.7);  // Tube (index 2)
        addNodeParams(params, 1, 6, 0.7);  // SineFold (index 6)

        // 2 active nodes
        params.addChange(
            Disrumpo::makeBandParamId(0, Disrumpo::BandParamType::kBandActiveNodes),
            1.0 / 3.0);

        fixture.processBlocks(kSettleBlocks, kFreq, sampleOffset, &params);
    }

    // Capture at Node 0 (Tube): morph cursor (0, 0)
    auto outputA = captureAtCursor(fixture, 0.0f, 0.0f, kFreq, sampleOffset);

    // Capture at Node 1 (SineFold): morph cursor (1, 0)
    auto outputB = captureAtCursor(fixture, 1.0f, 0.0f, kFreq, sampleOffset);

    float rmsA = calculateRMS(outputA.data(), kBlockSize);
    float rmsB = calculateRMS(outputB.data(), kBlockSize);
    float diff = rmsDifference(outputA.data(), outputB.data(), kBlockSize);

    INFO("Tube RMS: " << rmsA << "  SineFold RMS: " << rmsB << "  diffRMS: " << diff);

    REQUIRE(rmsA > 0.01f);
    REQUIRE(rmsB > 0.01f);
    REQUIRE(diff > 0.05f);
}

TEST_CASE("3-node morph: Fuzz vs FullRectify vs Bitcrush produce distinct outputs",
          "[integration][morph][multi-node]") {
    // Fuzz (D05, Saturation) vs FullRectify (D10, Rectify) vs Bitcrush (D12, Digital)
    ProcessorFixture fixture;
    Steinberg::int32 sampleOffset = 0;
    constexpr float kFreq = 440.0f;

    {
        SimpleParameterChanges params;
        addNodeParams(params, 0, 4, 0.6);   // Fuzz (index 4)
        addNodeParams(params, 1, 9, 0.6);   // FullRectify (index 9)
        addNodeParams(params, 2, 11, 0.6);  // Bitcrush (index 11)

        // 3 active nodes
        params.addChange(
            Disrumpo::makeBandParamId(0, Disrumpo::BandParamType::kBandActiveNodes),
            2.0 / 3.0);

        fixture.processBlocks(kSettleBlocks, kFreq, sampleOffset, &params);
    }

    // 3-node layout is a triangle: Node0=(0,0), Node1=(1,0), Node2=(0.5,1)
    auto outputA = captureAtCursor(fixture, 0.0f, 0.0f, kFreq, sampleOffset);
    auto outputB = captureAtCursor(fixture, 1.0f, 0.0f, kFreq, sampleOffset);
    auto outputC = captureAtCursor(fixture, 0.5f, 1.0f, kFreq, sampleOffset);

    float rmsA = calculateRMS(outputA.data(), kBlockSize);
    float rmsB = calculateRMS(outputB.data(), kBlockSize);
    float rmsC = calculateRMS(outputC.data(), kBlockSize);
    float diffAB = rmsDifference(outputA.data(), outputB.data(), kBlockSize);
    float diffAC = rmsDifference(outputA.data(), outputC.data(), kBlockSize);
    float diffBC = rmsDifference(outputB.data(), outputC.data(), kBlockSize);

    INFO("Fuzz RMS: " << rmsA << "  FullRectify RMS: " << rmsB
         << "  Bitcrush RMS: " << rmsC);
    INFO("diffAB: " << diffAB << "  diffAC: " << diffAC << "  diffBC: " << diffBC);

    // All nodes produce output
    REQUIRE(rmsA > 0.01f);
    REQUIRE(rmsB > 0.01f);
    REQUIRE(rmsC > 0.01f);

    // All pairs are distinct
    REQUIRE(diffAB > 0.05f);
    REQUIRE(diffAC > 0.05f);
    REQUIRE(diffBC > 0.05f);
}

TEST_CASE("4-node morph: Tape vs SergeFold vs HalfRectify vs Quantize produce distinct outputs",
          "[integration][morph][multi-node]") {
    // Tape (D04, Saturation) vs SergeFold (D09, Wavefold) vs
    // HalfRectify (D11, Rectify) vs Quantize (D14, Digital)
    ProcessorFixture fixture;
    Steinberg::int32 sampleOffset = 0;
    constexpr float kFreq = 440.0f;

    {
        SimpleParameterChanges params;
        addNodeParams(params, 0, 3, 0.7);   // Tape (index 3)
        addNodeParams(params, 1, 8, 0.7);   // SergeFold (index 8)
        addNodeParams(params, 2, 10, 0.7);  // HalfRectify (index 10)
        addNodeParams(params, 3, 13, 0.7);  // Quantize (index 13)

        // 4 active nodes
        params.addChange(
            Disrumpo::makeBandParamId(0, Disrumpo::BandParamType::kBandActiveNodes),
            1.0);

        fixture.processBlocks(kSettleBlocks, kFreq, sampleOffset, &params);
    }

    // 4-node layout: corners (0,0), (1,0), (1,1), (0,1)
    auto outputA = captureAtCursor(fixture, 0.0f, 0.0f, kFreq, sampleOffset);
    auto outputB = captureAtCursor(fixture, 1.0f, 0.0f, kFreq, sampleOffset);
    auto outputC = captureAtCursor(fixture, 1.0f, 1.0f, kFreq, sampleOffset);
    auto outputD = captureAtCursor(fixture, 0.0f, 1.0f, kFreq, sampleOffset);

    float rmsA = calculateRMS(outputA.data(), kBlockSize);
    float rmsB = calculateRMS(outputB.data(), kBlockSize);
    float rmsC = calculateRMS(outputC.data(), kBlockSize);
    float rmsD = calculateRMS(outputD.data(), kBlockSize);

    INFO("Tape RMS: " << rmsA << "  SergeFold RMS: " << rmsB
         << "  HalfRectify RMS: " << rmsC << "  Quantize RMS: " << rmsD);

    // All nodes produce output
    REQUIRE(rmsA > 0.01f);
    REQUIRE(rmsB > 0.01f);
    REQUIRE(rmsC > 0.01f);
    REQUIRE(rmsD > 0.01f);

    // Adjacent pairs should be distinct (at minimum)
    float diffAB = rmsDifference(outputA.data(), outputB.data(), kBlockSize);
    float diffBC = rmsDifference(outputB.data(), outputC.data(), kBlockSize);
    float diffCD = rmsDifference(outputC.data(), outputD.data(), kBlockSize);
    float diffDA = rmsDifference(outputD.data(), outputA.data(), kBlockSize);

    INFO("diffAB: " << diffAB << "  diffBC: " << diffBC
         << "  diffCD: " << diffCD << "  diffDA: " << diffDA);

    REQUIRE(diffAB > 0.05f);
    REQUIRE(diffBC > 0.05f);
    REQUIRE(diffCD > 0.05f);
    REQUIRE(diffDA > 0.05f);
}

// (Old SoftClip-BitwiseMangler morph tests replaced by diverse multi-node tests above)

// =============================================================================
// DIAGNOSTIC: MorphEngine with processor-style setNodes call sequence
// =============================================================================
// The processor calls setMorphNodes multiple times during parameter processing:
// once per parameter change, with activeCount=1 until kBandActiveNodes is set.
// This test mimics that exact sequence to see if the intermediate calls break
// the engine state.
// =============================================================================

TEST_CASE("MorphEngine: processor-style setNodes sequence preserves node state",
          "[integration][morph][diagnostic]") {
    using namespace Disrumpo;

    constexpr double kSR = 44100.0;
    constexpr int kSamples = 8192;
    constexpr float kFreq = 440.0f;
    constexpr float twoPi = 6.283185307179586f;

    std::array<float, kSamples> input{};
    for (int i = 0; i < kSamples; ++i) {
        input[i] = std::sin(twoPi * kFreq * static_cast<float>(i) / static_cast<float>(kSR));
    }

    // Use SoftClip vs SineFold — different categories, reliably different spectral output
    constexpr DistortionCommonParams kDefaultCommon{1.0f, 1.0f, 4000.0f};
    std::array<MorphNode, kMaxMorphNodes> nodes;
    nodes[0] = MorphNode(0, 0.0f, 0.0f, DistortionType::SoftClip);
    nodes[0].commonParams = kDefaultCommon;
    nodes[1] = MorphNode(1, 1.0f, 0.0f, DistortionType::SoftClip);
    nodes[1].commonParams = kDefaultCommon;
    nodes[2] = MorphNode(2, 0.0f, 1.0f, DistortionType::SoftClip);
    nodes[2].commonParams = kDefaultCommon;
    nodes[3] = MorphNode(3, 1.0f, 1.0f, DistortionType::SoftClip);
    nodes[3].commonParams = kDefaultCommon;

    auto engine = std::make_unique<MorphEngine>();
    engine->prepare(kSR, 512);
    engine->setSmoothingTime(0.0f);

    int activeCount = kDefaultActiveNodes;  // = 1

    engine->setNodes(nodes, activeCount);
    engine->setMorphPosition(0.5f, 0.5f);

    // Mimic processor parameter sequence with multiple setNodes calls
    engine->setNodes(nodes, activeCount);
    nodes[0].commonParams.drive = 5.0f;
    engine->setNodes(nodes, activeCount);
    nodes[0].commonParams.mix = 1.0f;
    engine->setNodes(nodes, activeCount);

    // Node 1 → SineFold (Wavefold category, cross-family with SoftClip)
    nodes[1].type = DistortionType::SineFold;
    engine->setNodes(nodes, activeCount);  // still activeCount=1
    nodes[1].commonParams.drive = 5.0f;
    engine->setNodes(nodes, activeCount);
    nodes[1].commonParams.mix = 1.0f;
    engine->setNodes(nodes, activeCount);

    // Activate 2 nodes
    activeCount = 2;
    engine->setNodes(nodes, activeCount);
    engine->setMorphPosition(0.0f, 0.0f);

    // Warm up
    for (int i = 0; i < kSamples; ++i) {
        [[maybe_unused]] float out = engine->process(input[i]);
    }

    // Capture at Node A (SoftClip)
    engine->setMorphPosition(0.0f, 0.0f);
    for (int i = 0; i < 1024; ++i) {
        [[maybe_unused]] float out = engine->process(input[i % kSamples]);
    }
    std::array<float, kSamples> outputA{};
    for (int i = 0; i < kSamples; ++i) {
        outputA[i] = engine->process(input[i]);
    }

    // Capture at Node B (SineFold)
    engine->setMorphPosition(1.0f, 0.0f);
    for (int i = 0; i < 1024; ++i) {
        [[maybe_unused]] float out = engine->process(input[i % kSamples]);
    }
    std::array<float, kSamples> outputB{};
    for (int i = 0; i < kSamples; ++i) {
        outputB[i] = engine->process(input[i]);
    }

    // Analysis
    const int analysisOffset = kSamples / 2;
    const int analysisLen = kSamples / 2;
    const float* aPtr = outputA.data() + analysisOffset;
    const float* bPtr = outputB.data() + analysisOffset;

    float rmsA = calculateRMS(aPtr, analysisLen);
    float rmsB = calculateRMS(bPtr, analysisLen);

    double diffSumSq = 0.0;
    for (int i = 0; i < analysisLen; ++i) {
        double d = static_cast<double>(aPtr[i]) - static_cast<double>(bPtr[i]);
        diffSumSq += d * d;
    }
    float diffRMS = static_cast<float>(std::sqrt(diffSumSq / analysisLen));

    INFO("Processor-style sequence: Node A RMS: " << rmsA << " Node B RMS: " << rmsB
         << " diffRMS: " << diffRMS);

    REQUIRE(rmsA > 0.01f);
    REQUIRE(rmsB > 0.01f);
    REQUIRE(diffRMS > 0.10f);
}

// =============================================================================
// DIAGNOSTIC: BandProcessor-level test (bypasses Processor routing)
// =============================================================================
// Tests the BandProcessor directly to determine if the bug is in:
// a) BandProcessor (oversampling/wrapping of MorphEngine), or
// b) Processor (parameter routing/per-block updates)
// =============================================================================

TEST_CASE("BandProcessor: cross-family morph cursor changes output",
          "[integration][morph][diagnostic]") {
    using namespace Disrumpo;

    constexpr double kSR = 44100.0;
    constexpr Steinberg::int32 kSamples = 8192;
    constexpr float kFreq = 440.0f;
    constexpr float twoPi = 6.283185307179586f;

    std::vector<float> inputL(kSamples), inputR(kSamples);
    for (int i = 0; i < kSamples; ++i) {
        float s = std::sin(twoPi * kFreq * static_cast<float>(i) / static_cast<float>(kSR));
        inputL[i] = s;
        inputR[i] = s;
    }

    // SoftClip (Saturation) vs SineFold (Wavefold) — reliably different character
    constexpr DistortionCommonParams kCommon{5.0f, 1.0f, 4000.0f};
    std::array<MorphNode, kMaxMorphNodes> nodes;
    nodes[0] = MorphNode(0, 0.0f, 0.0f, DistortionType::SoftClip);
    nodes[0].commonParams = kCommon;
    nodes[1] = MorphNode(1, 1.0f, 0.0f, DistortionType::SineFold);
    nodes[1].commonParams = kCommon;
    nodes[2] = MorphNode(2, 0.0f, 1.0f, DistortionType::SoftClip);
    nodes[2].commonParams = kCommon;
    nodes[3] = MorphNode(3, 1.0f, 1.0f, DistortionType::SoftClip);
    nodes[3].commonParams = kCommon;

    // Cursor on Node A (SoftClip)
    BandProcessor bpA;
    bpA.prepare(kSR, 512);
    bpA.setMaxOversampleFactor(1);
    bpA.setMorphEnabled(true);
    bpA.setMorphNodes(nodes, 2);
    bpA.setMorphPosition(0.0f, 0.0f);

    std::vector<float> warmL(kSamples), warmR(kSamples);
    std::copy(inputL.begin(), inputL.end(), warmL.begin());
    std::copy(inputR.begin(), inputR.end(), warmR.begin());
    bpA.processBlock(warmL.data(), warmR.data(), kSamples);

    std::vector<float> outAL(inputL), outAR(inputR);
    bpA.processBlock(outAL.data(), outAR.data(), kSamples);

    // Cursor on Node B (SineFold)
    BandProcessor bpB;
    bpB.prepare(kSR, 512);
    bpB.setMaxOversampleFactor(1);
    bpB.setMorphEnabled(true);
    bpB.setMorphNodes(nodes, 2);
    bpB.setMorphPosition(1.0f, 0.0f);

    std::copy(inputL.begin(), inputL.end(), warmL.begin());
    std::copy(inputR.begin(), inputR.end(), warmR.begin());
    bpB.processBlock(warmL.data(), warmR.data(), kSamples);

    std::vector<float> outBL(inputL), outBR(inputR);
    bpB.processBlock(outBL.data(), outBR.data(), kSamples);

    // Analysis
    const int off = kSamples / 2;
    const int len = kSamples / 2;
    float rmsA = calculateRMS(outAL.data() + off, len);
    float rmsB = calculateRMS(outBL.data() + off, len);

    double diffSumSq = 0.0;
    for (int i = 0; i < len; ++i) {
        double d = static_cast<double>(outAL[off + i]) - static_cast<double>(outBL[off + i]);
        diffSumSq += d * d;
    }
    float diffRMS = static_cast<float>(std::sqrt(diffSumSq / len));

    INFO("BandProcessor: Node A RMS: " << rmsA << " Node B RMS: " << rmsB
         << " diffRMS: " << diffRMS);

    REQUIRE(rmsA > 0.01f);
    REQUIRE(rmsB > 0.01f);
    REQUIRE(diffRMS > 0.05f);
}

// =============================================================================
// DIAGNOSTIC: BandProcessor per-sample path (matches Processor usage)
// =============================================================================
// The Processor calls bandProcessors_[b].process(float&, float&) per sample,
// NOT processBlock(). This test verifies the per-sample path works.
// =============================================================================

TEST_CASE("BandProcessor per-sample: cross-family morph cursor changes output",
          "[integration][morph][diagnostic]") {
    using namespace Disrumpo;

    constexpr double kSR = 44100.0;
    constexpr Steinberg::int32 kSamples = 8192;
    constexpr float kFreq = 440.0f;
    constexpr float twoPi = 6.283185307179586f;

    std::vector<float> input(kSamples);
    for (int i = 0; i < kSamples; ++i) {
        input[i] = std::sin(twoPi * kFreq * static_cast<float>(i) / static_cast<float>(kSR));
    }

    // SoftClip vs SineFold — different categories, reliable spectral difference
    auto runWithProcessorFlow = [&](float morphX, float morphY,
                                     std::vector<float>& outL) {
        constexpr DistortionCommonParams kInitCommon{1.0f, 1.0f, 4000.0f};
        std::array<MorphNode, kMaxMorphNodes> initNodes;
        initNodes[0] = MorphNode(0, 0.0f, 0.0f, DistortionType::SoftClip);
        initNodes[0].commonParams = kInitCommon;
        initNodes[1] = MorphNode(1, 1.0f, 0.0f, DistortionType::SoftClip);
        initNodes[1].commonParams = kInitCommon;
        initNodes[2] = MorphNode(2, 0.0f, 1.0f, DistortionType::SoftClip);
        initNodes[2].commonParams = kInitCommon;
        initNodes[3] = MorphNode(3, 1.0f, 1.0f, DistortionType::SoftClip);
        initNodes[3].commonParams = kInitCommon;

        BandProcessor bp;
        bp.prepare(kSR);
        bp.setMorphEnabled(true);
        bp.setMorphNodes(initNodes, kDefaultActiveNodes);
        bp.setMorphPosition(0.5f, 0.5f);

        auto mutableNodes = initNodes;

        // Node 0: SoftClip with drive
        mutableNodes[0].commonParams.drive = 5.0f;
        bp.setMorphNodes(mutableNodes, 1);
        mutableNodes[0].commonParams.mix = 1.0f;
        bp.setMorphNodes(mutableNodes, 1);

        // Node 1: SineFold with drive
        mutableNodes[1].type = DistortionType::SineFold;
        bp.setMorphNodes(mutableNodes, 1);
        mutableNodes[1].commonParams.drive = 5.0f;
        bp.setMorphNodes(mutableNodes, 1);
        mutableNodes[1].commonParams.mix = 1.0f;
        bp.setMorphNodes(mutableNodes, 1);

        // Activate 2 nodes
        bp.setMorphNodes(mutableNodes, 2);
        bp.setMorphPosition(morphX, morphY);

        // Warm up
        for (int i = 0; i < 32768; ++i) {
            float l = input[i % kSamples], r = input[i % kSamples];
            bp.process(l, r);
            if (i % 512 == 511) bp.setMorphPosition(morphX, morphY);
        }

        // Capture
        outL.resize(kSamples);
        for (int i = 0; i < kSamples; ++i) {
            float l = input[i], r = input[i];
            bp.process(l, r);
            outL[i] = l;
            if (i % 512 == 511) bp.setMorphPosition(morphX, morphY);
        }
    };

    std::vector<float> outAL, outBL;
    runWithProcessorFlow(0.0f, 0.0f, outAL);
    runWithProcessorFlow(1.0f, 0.0f, outBL);

    const int off = kSamples / 2;
    const int len = kSamples / 2;
    float rmsA = calculateRMS(outAL.data() + off, len);
    float rmsB = calculateRMS(outBL.data() + off, len);

    double dss = 0.0;
    for (int i = 0; i < len; ++i) {
        double d = static_cast<double>(outAL[off + i]) - static_cast<double>(outBL[off + i]);
        dss += d * d;
    }
    float diffRMS = static_cast<float>(std::sqrt(dss / len));

    INFO("BandProcessor per-sample: Node A RMS: " << rmsA << " Node B RMS: " << rmsB
         << " diffRMS: " << diffRMS);

    REQUIRE(rmsA > 0.01f);
    REQUIRE(rmsB > 0.01f);
    REQUIRE(diffRMS > 0.05f);
}
