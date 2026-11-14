// ==============================================================================
// Processor Audio Output Integration Test
// ==============================================================================
// Verifies that the full VST3 Processor produces non-zero audio output
// when given audio input. This is the end-to-end test for the audio signal
// path through the Processor class (crossover -> band processing -> summation).
//
// This fills the testing gap where unit/integration tests only tested
// individual DSP components (BandProcessor, CrossoverNetwork) but never
// the full VST3 Processor wrapper that hosts interact with.
//
// Constitution Principle XII: Test-First Development
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>
#include "vst_param_changes.h"

#include "../test_support/audit_metrics.h"
#include "../test_support/calibrated_clicks.h"
#include "../test_support/disrumpo_rig.h"
#include "dsp/crossover_network.h"
#include "test_helpers/spectral_analysis.h"

#include <allocation_detector.h>

#include <algorithm>
#include <chrono>
#include <functional>
#include <numbers>
#include <utility>

namespace {

constexpr double kSampleRate = 44100.0;
constexpr Steinberg::int32 kBlockSize = 512;
constexpr Steinberg::int32 kNumBlocks = 32;  // Process enough blocks for filters to settle

/// @brief Generate sine wave samples into a buffer
void generateSine(float* buffer, Steinberg::int32 numSamples, float freq, double sampleRate, Steinberg::int32 offset = 0) {
    constexpr double twoPi = 6.283185307179586;
    for (Steinberg::int32 i = 0; i < numSamples; ++i) {
        buffer[i] = static_cast<float>(std::sin(twoPi * freq * static_cast<double>(i + offset) / sampleRate));
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

/// @brief Check if buffer is completely silent (all zeros)
bool isSilent(const float* buffer, Steinberg::int32 numSamples) {
    for (Steinberg::int32 i = 0; i < numSamples; ++i) {
        if (buffer[i] != 0.0f) return false;
    }
    return true;
}

/// @brief Simple parameter changes implementation for injecting parameter values
// Parameter-change mocks consolidated into tests/test_helpers/vst_param_changes.h
using SimpleParamValueQueue = Krate::Test::ParamValueQueue;
using SimpleParameterChanges = Krate::Test::ParameterChanges;






/// @brief RAII wrapper for setting up and tearing down a Processor
struct ProcessorFixture {
    std::unique_ptr<Disrumpo::Processor> processor;

    explicit ProcessorFixture(double sampleRate = kSampleRate) {
        processor = std::make_unique<Disrumpo::Processor>();
        auto result = processor->initialize(nullptr);
        REQUIRE(result == Steinberg::kResultTrue);

        Steinberg::Vst::ProcessSetup setup{};
        setup.processMode = Steinberg::Vst::kRealtime;
        setup.symbolicSampleSize = Steinberg::Vst::kSample32;
        setup.sampleRate = sampleRate;
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

    /// @brief Process a single block of audio and return the output RMS
    /// @param inputL Left input buffer (kBlockSize samples)
    /// @param inputR Right input buffer (kBlockSize samples)
    /// @param outputL Left output buffer (kBlockSize samples, written by processor)
    /// @param outputR Right output buffer (kBlockSize samples, written by processor)
    /// @param paramChanges Optional parameter changes to inject
    void processBlock(float* inputL, float* inputR, float* outputL, float* outputR,
                      Steinberg::Vst::IParameterChanges* paramChanges = nullptr) const {
        // Set up audio bus buffers
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

} // anonymous namespace

// =============================================================================
// Test: Default processor produces non-zero output for non-zero input
// =============================================================================

TEST_CASE("Processor produces audio output with default parameters", "[integration][processor][audio]") {
    // D-129 (T5.08): at the common host rates and at 192 kHz, where the adapters run
    // up to 4x (768 kHz); every output sample finite.
    double sampleRate = kSampleRate;
    SECTION("44.1 kHz") { sampleRate = 44100.0; }
    SECTION("96 kHz") { sampleRate = 96000.0; }
    SECTION("192 kHz") { sampleRate = 192000.0; }
    ProcessorFixture fixture(sampleRate);

    std::array<float, kBlockSize> inputL{};
    std::array<float, kBlockSize> inputR{};
    std::array<float, kBlockSize> outputL{};
    std::array<float, kBlockSize> outputR{};

    float lastOutputRmsL = 0.0f;
    float lastOutputRmsR = 0.0f;
    int nonFinite = 0;

    // Process multiple blocks to let filters settle
    for (Steinberg::int32 block = 0; block < kNumBlocks; ++block) {
        // Generate 1kHz sine on both channels
        generateSine(inputL.data(), kBlockSize, 1000.0f, sampleRate, block * kBlockSize);
        generateSine(inputR.data(), kBlockSize, 1000.0f, sampleRate, block * kBlockSize);

        // Clear output
        std::memset(outputL.data(), 0, sizeof(float) * kBlockSize);
        std::memset(outputR.data(), 0, sizeof(float) * kBlockSize);

        fixture.processBlock(inputL.data(), inputR.data(), outputL.data(), outputR.data());

        for (Steinberg::int32 i = 0; i < kBlockSize; ++i)
            if (!Krate::DSP::detail::isFinite(outputL[static_cast<size_t>(i)]) ||
                !Krate::DSP::detail::isFinite(outputR[static_cast<size_t>(i)]))
                ++nonFinite;
        lastOutputRmsL = calculateRMS(outputL.data(), kBlockSize);
        lastOutputRmsR = calculateRMS(outputR.data(), kBlockSize);
    }

    std::printf("D-129 default patch at %.0f Hz: last block RMS L %.4f R %.4f, %d non-finite samples\n", sampleRate,
                static_cast<double>(lastOutputRmsL), static_cast<double>(lastOutputRmsR), nonFinite);
    INFO("Final block output L RMS: " << lastOutputRmsL);
    INFO("Final block output R RMS: " << lastOutputRmsR);

    // After settling, output MUST be non-zero
    CHECK(nonFinite == 0);
    REQUIRE(lastOutputRmsL > 0.01f);
    REQUIRE(lastOutputRmsR > 0.01f);
}

// =============================================================================
// Test: Processor output is not silent (checks for zeros)
// =============================================================================

TEST_CASE("Processor output is never completely silent with input", "[integration][processor][audio]") {
    ProcessorFixture fixture;

    std::array<float, kBlockSize> inputL{};
    std::array<float, kBlockSize> inputR{};
    std::array<float, kBlockSize> outputL{};
    std::array<float, kBlockSize> outputR{};

    int silentBlocksL = 0;
    int silentBlocksR = 0;

    // Process several blocks
    for (Steinberg::int32 block = 0; block < kNumBlocks; ++block) {
        generateSine(inputL.data(), kBlockSize, 440.0f, kSampleRate, block * kBlockSize);
        generateSine(inputR.data(), kBlockSize, 440.0f, kSampleRate, block * kBlockSize);

        std::memset(outputL.data(), 0, sizeof(float) * kBlockSize);
        std::memset(outputR.data(), 0, sizeof(float) * kBlockSize);

        fixture.processBlock(inputL.data(), inputR.data(), outputL.data(), outputR.data());

        if (isSilent(outputL.data(), kBlockSize)) ++silentBlocksL;
        if (isSilent(outputR.data(), kBlockSize)) ++silentBlocksR;
    }

    INFO("Silent blocks L: " << silentBlocksL << " / " << kNumBlocks);
    INFO("Silent blocks R: " << silentBlocksR << " / " << kNumBlocks);

    // Allow first few blocks to be silent (filter settling), but most blocks must have output
    REQUIRE(silentBlocksL < kNumBlocks / 2);
    REQUIRE(silentBlocksR < kNumBlocks / 2);
}

// =============================================================================
// Test: Processor with mute produces silence
// =============================================================================

TEST_CASE("Processor with all bands muted produces silence", "[integration][processor][audio]") {
    ProcessorFixture fixture;

    // Mute all 4 bands via parameter changes
    SimpleParameterChanges muteParams;
    for (int b = 0; b < Disrumpo::kMaxBands; ++b) {
        auto muteId = Disrumpo::makeBandParamId(static_cast<uint8_t>(b), Disrumpo::BandParamType::kBandMute);
        muteParams.addChange(muteId, 1.0);  // Muted
    }

    std::array<float, kBlockSize> inputL{};
    std::array<float, kBlockSize> inputR{};
    std::array<float, kBlockSize> outputL{};
    std::array<float, kBlockSize> outputR{};

    float lastOutputRmsL = 0.0f;
    float lastOutputRmsR = 0.0f;

    // Process with muted bands - inject parameter change on first block
    for (Steinberg::int32 block = 0; block < kNumBlocks; ++block) {
        generateSine(inputL.data(), kBlockSize, 1000.0f, kSampleRate, block * kBlockSize);
        generateSine(inputR.data(), kBlockSize, 1000.0f, kSampleRate, block * kBlockSize);

        std::memset(outputL.data(), 0, sizeof(float) * kBlockSize);
        std::memset(outputR.data(), 0, sizeof(float) * kBlockSize);

        // Only inject params on first block
        auto* params = (block == 0) ? static_cast<Steinberg::Vst::IParameterChanges*>(&muteParams) : nullptr;
        fixture.processBlock(inputL.data(), inputR.data(), outputL.data(), outputR.data(), params);

        lastOutputRmsL = calculateRMS(outputL.data(), kBlockSize);
        lastOutputRmsR = calculateRMS(outputR.data(), kBlockSize);
    }

    INFO("Muted output L RMS: " << lastOutputRmsL);
    INFO("Muted output R RMS: " << lastOutputRmsR);

    // After settling with mute, output should be near silence
    REQUIRE(lastOutputRmsL < 0.001f);
    REQUIRE(lastOutputRmsR < 0.001f);
}

// =============================================================================
// Test: TabView parameter does not affect audio output
// =============================================================================

TEST_CASE("TabView parameter change does not affect audio", "[integration][processor][audio]") {
    ProcessorFixture fixture;

    std::array<float, kBlockSize> inputL{};
    std::array<float, kBlockSize> inputR{};
    std::array<float, kBlockSize> outputL{};
    std::array<float, kBlockSize> outputR{};

    // First, get baseline output after settling
    float baselineRmsL = 0.0f;
    for (Steinberg::int32 block = 0; block < kNumBlocks; ++block) {
        generateSine(inputL.data(), kBlockSize, 1000.0f, kSampleRate, block * kBlockSize);
        generateSine(inputR.data(), kBlockSize, 1000.0f, kSampleRate, block * kBlockSize);
        std::memset(outputL.data(), 0, sizeof(float) * kBlockSize);
        std::memset(outputR.data(), 0, sizeof(float) * kBlockSize);
        fixture.processBlock(inputL.data(), inputR.data(), outputL.data(), outputR.data());
        baselineRmsL = calculateRMS(outputL.data(), kBlockSize);
    }

    // Now inject TabView parameter changes for all bands
    SimpleParameterChanges tabParams;
    for (int b = 0; b < Disrumpo::kMaxBands; ++b) {
        auto tabId = Disrumpo::makeBandParamId(static_cast<uint8_t>(b), Disrumpo::BandParamType::kBandTabView);
        tabParams.addChange(tabId, 1.0);  // Switch to "Shape" tab
    }

    // Process with TabView parameter and check output unchanged
    float afterTabRmsL = 0.0f;
    for (Steinberg::int32 block = 0; block < kNumBlocks; ++block) {
        generateSine(inputL.data(), kBlockSize, 1000.0f, kSampleRate, (kNumBlocks + block) * kBlockSize);
        generateSine(inputR.data(), kBlockSize, 1000.0f, kSampleRate, (kNumBlocks + block) * kBlockSize);
        std::memset(outputL.data(), 0, sizeof(float) * kBlockSize);
        std::memset(outputR.data(), 0, sizeof(float) * kBlockSize);
        auto* params = (block == 0) ? static_cast<Steinberg::Vst::IParameterChanges*>(&tabParams) : nullptr;
        fixture.processBlock(inputL.data(), inputR.data(), outputL.data(), outputR.data(), params);
        afterTabRmsL = calculateRMS(outputL.data(), kBlockSize);
    }

    INFO("Baseline RMS L: " << baselineRmsL);
    INFO("After TabView RMS L: " << afterTabRmsL);

    // TabView is UI-only: output should be virtually unchanged
    REQUIRE(baselineRmsL > 0.01f);
    REQUIRE(afterTabRmsL > 0.01f);
    REQUIRE(std::abs(baselineRmsL - afterTabRmsL) < 0.01f);
}

// =============================================================================
// Test: BitwiseMangler Pattern (NodeShape2) changes output through full processor
// =============================================================================

TEST_CASE("BitwiseMangler Pattern slider changes audio through full processor",
          "[integration][processor][bitwise]") {
    ProcessorFixture fixture;

    std::array<float, kBlockSize> inputL{};
    std::array<float, kBlockSize> inputR{};
    std::array<float, kBlockSize> outputL{};
    std::array<float, kBlockSize> outputR{};

    // Step 1: Set node 0 of band 0 to BitwiseMangler with XorPattern, Pattern=0
    {
        SimpleParameterChanges setupParams;
        setupParams.addChange(
            Disrumpo::makeNodeParamId(0, 0, Disrumpo::NodeParamType::kNodeType), 15.0 / 25.0);
        // Op = XorPattern (index 0 out of 4: normalized = 0.0)
        setupParams.addChange(
            Disrumpo::makeNodeParamId(0, 0, Disrumpo::NodeParamType::kNodeShape0), 0.0);
        // Intensity = 1.0 (full wet)
        setupParams.addChange(
            Disrumpo::makeNodeParamId(0, 0, Disrumpo::NodeParamType::kNodeShape1), 1.0);
        // Pattern = 0.0 (XOR with 0 = passthrough)
        setupParams.addChange(
            Disrumpo::makeNodeParamId(0, 0, Disrumpo::NodeParamType::kNodeShape2), 0.0);

        for (Steinberg::int32 block = 0; block < kNumBlocks; ++block) {
            generateSine(inputL.data(), kBlockSize, 1000.0f, kSampleRate, block * kBlockSize);
            generateSine(inputR.data(), kBlockSize, 1000.0f, kSampleRate, block * kBlockSize);
            std::memset(outputL.data(), 0, sizeof(float) * kBlockSize);
            std::memset(outputR.data(), 0, sizeof(float) * kBlockSize);
            auto* p = (block == 0) ? static_cast<Steinberg::Vst::IParameterChanges*>(&setupParams) : nullptr;
            fixture.processBlock(inputL.data(), inputR.data(), outputL.data(), outputR.data(), p);
        }
    }

    // Step 2: Capture output with Pattern=0.0 (passthrough)
    std::array<float, kBlockSize> capturedZero{};
    {
        generateSine(inputL.data(), kBlockSize, 1000.0f, kSampleRate, kNumBlocks * kBlockSize);
        generateSine(inputR.data(), kBlockSize, 1000.0f, kSampleRate, kNumBlocks * kBlockSize);
        std::memset(outputL.data(), 0, sizeof(float) * kBlockSize);
        std::memset(outputR.data(), 0, sizeof(float) * kBlockSize);
        fixture.processBlock(inputL.data(), inputR.data(), outputL.data(), outputR.data());
        capturedZero = outputL;
    }

    // Step 3: Change Pattern to 1.0 (full XOR mask) and settle
    {
        SimpleParameterChanges patternChange;
        patternChange.addChange(
            Disrumpo::makeNodeParamId(0, 0, Disrumpo::NodeParamType::kNodeShape2), 1.0);

        for (Steinberg::int32 block = 0; block < 8; ++block) {
            generateSine(inputL.data(), kBlockSize, 1000.0f, kSampleRate, (kNumBlocks + 1 + block) * kBlockSize);
            generateSine(inputR.data(), kBlockSize, 1000.0f, kSampleRate, (kNumBlocks + 1 + block) * kBlockSize);
            std::memset(outputL.data(), 0, sizeof(float) * kBlockSize);
            std::memset(outputR.data(), 0, sizeof(float) * kBlockSize);
            auto* p = (block == 0) ? static_cast<Steinberg::Vst::IParameterChanges*>(&patternChange) : nullptr;
            fixture.processBlock(inputL.data(), inputR.data(), outputL.data(), outputR.data(), p);
        }
    }

    // Step 4: Capture output with Pattern=1.0
    std::array<float, kBlockSize> capturedMax{};
    {
        generateSine(inputL.data(), kBlockSize, 1000.0f, kSampleRate, (kNumBlocks + 9) * kBlockSize);
        generateSine(inputR.data(), kBlockSize, 1000.0f, kSampleRate, (kNumBlocks + 9) * kBlockSize);
        std::memset(outputL.data(), 0, sizeof(float) * kBlockSize);
        std::memset(outputR.data(), 0, sizeof(float) * kBlockSize);
        fixture.processBlock(inputL.data(), inputR.data(), outputL.data(), outputR.data());
        capturedMax = outputL;
    }

    // Compare: outputs MUST differ when Pattern changes from 0 to max
    int differentSamples = 0;
    for (Steinberg::int32 i = 0; i < kBlockSize; ++i) {
        if (std::abs(capturedZero[i] - capturedMax[i]) > 0.01f) {
            ++differentSamples;
        }
    }

    INFO("Different samples: " << differentSamples << " / " << kBlockSize);
    REQUIRE(differentSamples > kBlockSize / 4);
}

// =============================================================================
// Block path: oversampling and band bypass run inside Processor::process
// =============================================================================
// Audit D-017 / D-027 / D-116 / D-095 / D-020 (fix plan T1.07): Processor::process
// called only the per-sample BandProcessor::process, so the oversamplers, the
// Oversample Max parameter and the band Bypass flag never reached the audio.
// =============================================================================

namespace {

namespace ts = Disrumpo::TestSupport;
using Disrumpo::BandParamType;
using Disrumpo::DistortionType;
using Disrumpo::NodeParamType;
using Disrumpo::makeBandParamId;
using Disrumpo::makeNodeParamId;

constexpr double kBlockSr = 48000.0;
constexpr float kAliasToneHz = 1031.25f;  // exactly bin 176 of an 8192-point FFT at 48 kHz
constexpr size_t kAliasFft = 8192;
constexpr size_t kAliasFrames = 4;        // the last 4 x 8192 = 32768 samples

std::vector<float> sineAt(size_t n, double freqHz, double dbfs) {
    const double amp = std::pow(10.0, dbfs / 20.0);
    std::vector<float> x(n);
    for (size_t i = 0; i < n; ++i) {
        x[i] = static_cast<float>(amp * std::sin(2.0 * std::numbers::pi * freqHz *
                                                 static_cast<double>(i) / kBlockSr));
    }
    return x;
}

double typeNorm(DistortionType t) { return static_cast<double>(static_cast<int>(t)) / 25.0; }

/// One band, node 0 HardClip drive 10, tone 8 kHz, mix 1, cursor on node 0.
std::vector<float> renderHardClipBand(double oversampleMaxNorm, int& factor) {
    ts::Rig rig(kBlockSr);
    rig.apply({{Disrumpo::kBandCountId, 0.0},
               {makeNodeParamId(0, 0, NodeParamType::kNodeType), typeNorm(DistortionType::HardClip)},
               {makeNodeParamId(0, 0, NodeParamType::kNodeDrive), 1.0},
               {makeNodeParamId(0, 0, NodeParamType::kNodeTone), 1.0},
               {makeNodeParamId(0, 0, NodeParamType::kNodeMix), 1.0},
               {makeBandParamId(0, BandParamType::kBandMorphX), 0.0},
               {makeBandParamId(0, BandParamType::kBandMorphY), 0.0},
               {Disrumpo::kOversampleMaxId, oversampleMaxNorm}});
    rig.reactivate();
    const auto in = sineAt(static_cast<size_t>(kBlockSr), kAliasToneHz, -6.0);
    std::vector<float> outL;
    std::vector<float> outR;
    rig.render(in, in, outL, outR);
    factor = rig.p->getBandProcessor(0).getOversampleFactor();
    return outL;
}

/// Every node of every band HardClip at drive 10, mix 1; band bypass as given.
ts::PList hardClipBands(double bandCountNorm, int numBands, bool bypass) {
    ts::PList l{{Disrumpo::kBandCountId, bandCountNorm}, {Disrumpo::kGlobalMixId, 1.0}};
    for (int b = 0; b < numBands; ++b) {
        const auto band = static_cast<uint8_t>(b);
        for (uint8_t n = 0; n < 4; ++n) {
            l.emplace_back(makeNodeParamId(band, n, NodeParamType::kNodeType), typeNorm(DistortionType::HardClip));
            l.emplace_back(makeNodeParamId(band, n, NodeParamType::kNodeDrive), 1.0);
            l.emplace_back(makeNodeParamId(band, n, NodeParamType::kNodeMix), 1.0);
        }
        l.emplace_back(makeBandParamId(band, BandParamType::kBandBypass), bypass ? 1.0 : 0.0);
    }
    return l;
}

/// The sum of a fresh CrossoverNetwork's bands: what an all-bypassed processor must output.
std::vector<float> crossoverSum(const std::vector<float>& in, int numBands) {
    Disrumpo::CrossoverNetwork xo;
    xo.prepare(kBlockSr, Disrumpo::kDefaultBands);
    xo.setBandCount(numBands);
    xo.reset();
    std::array<float, Disrumpo::kMaxBands> bands{};
    std::vector<float> out(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        xo.process(in[i], bands);
        float s = 0.0f;
        for (int b = 0; b < numBands; ++b) s += bands[static_cast<size_t>(b)];
        out[i] = s;
    }
    return out;
}

double maxAbsDiff(const std::vector<float>& a, const std::vector<float>& b, size_t from = 0,
                  size_t to = static_cast<size_t>(-1)) {
    double m = 0.0;
    to = std::min({to, a.size(), b.size()});
    for (size_t i = from; i < to; ++i) {
        m = std::max(m, std::fabs(static_cast<double>(a[i]) - static_cast<double>(b[i])));
    }
    return m;
}

/// `x` delayed by `d` samples (zeros first): the processor delays every band and the dry
/// path by the latency it reports (T2.10).
std::vector<float> delayedBy(const std::vector<float>& x, size_t d) {
    std::vector<float> y(x.size(), 0.0f);
    for (size_t i = d; i < x.size(); ++i) y[i] = x[i - d];
    return y;
}

double maxStep(const std::vector<float>& x, size_t from, size_t to) {
    double m = 0.0;
    for (size_t i = std::max<size_t>(from, 1); i < std::min(to, x.size()); ++i) {
        m = std::max(m, std::fabs(static_cast<double>(x[i]) - static_cast<double>(x[i - 1])));
    }
    return m;
}

} // anonymous namespace

TEST_CASE("Processor HardClip aliasing drops with Oversample Max 4x vs 1x",
          "[disrumpo][oversampling][D-027]") {
    int factor1x = 0;
    int factor4x = 0;
    const auto out1x = renderHardClipBand(0.0, factor1x);
    const auto out4x = renderHardClipBand(2.0 / 3.0, factor4x);
    const auto r1 = ts::readAliasing(out1x, kBlockSr, kAliasToneHz, kAliasFft, kAliasFrames);
    const auto r4 = ts::readAliasing(out4x, kBlockSr, kAliasToneHz, kAliasFft, kAliasFrames);

    INFO("factor at Oversample Max 1x: " << factor1x << ", at 4x: " << factor4x);
    INFO("fundamental 1x " << r1.fundamentalDb << " dB, 4x " << r4.fundamentalDb << " dB");
    INFO("aliased bins 1x " << r1.aliasedDb << " dB, 4x " << r4.aliasedDb << " dB, reduction "
         << (r1.aliasedDb - r4.aliasedDb) << " dB");
    INFO("non-harmonic 1x " << r1.nonHarmonicDb << " dB, 4x " << r4.nonHarmonicDb << " dB, reduction "
         << (r1.nonHarmonicDb - r4.nonHarmonicDb) << " dB");
    REQUIRE(factor1x == 1);
    REQUIRE(factor4x == 4);
    REQUIRE(r1.aliasedDb - r4.aliasedDb >= 12.0);
    // The audit probe "os-engaged" read the non-harmonic energy (fix plan T6.05; baseline
    // -43.3 dB at both settings, reduction 0.0 dB)
    REQUIRE(r1.nonHarmonicDb - r4.nonHarmonicDb >= 12.0);
    // D-009 (fix plan T1.06): the tone filter runs at the rate it is clocked, so the
    // 1 kHz fundamental reads the same at 1x and 4x (it read 56.97 vs 59.83 dB while
    // the adapters were prepared for 8x the host rate).
    REQUIRE(std::fabs(r1.fundamentalDb - r4.fundamentalDb) < 1.0);
}

TEST_CASE("Band bypass passes the crossover band unchanged", "[disrumpo][bypass][D-095]") {
    // Unchanged, delayed by the latency the processor reports: a bypassed band is delayed
    // like every other band and the dry path (T2.10)
    const auto in = sineAt(static_cast<size_t>(kBlockSr / 2.0), 220.0, -12.0);
    std::vector<float> outL;
    std::vector<float> outR;

    SECTION("4 bands, every band bypassed") {
        ts::Rig rig(kBlockSr);
        rig.apply(hardClipBands(1.0, 4, true));
        rig.reactivate();
        rig.render(in, in, outL, outR);
        const auto latency = static_cast<size_t>(rig.p->getLatencySamples());
        const auto expected = delayedBy(crossoverSum(in, 4), latency);
        const double diffL = maxAbsDiff(outL, expected);
        const double diffR = maxAbsDiff(outR, expected);
        INFO("latency " << latency << "; max |out - crossover sum|: L " << diffL << ", R " << diffR);
        REQUIRE(diffL < 1e-5);
        REQUIRE(diffR < 1e-5);
    }

    SECTION("1 band bypassed passes the input through") {
        ts::Rig rig(kBlockSr);
        rig.apply(hardClipBands(0.0, 1, true));
        rig.reactivate();
        rig.render(in, in, outL, outR);
        const auto latency = static_cast<size_t>(rig.p->getLatencySamples());
        const auto expected = delayedBy(in, latency);
        const double diffL = maxAbsDiff(outL, expected);
        const double diffR = maxAbsDiff(outR, expected);
        INFO("latency " << latency << "; max |out - in|: L " << diffL << ", R " << diffR);
        REQUIRE(diffL < 1e-6);
        REQUIRE(diffR < 1e-6);
    }
}

TEST_CASE("Band bypass toggled mid-stream stays finite with bounded steps", "[disrumpo][bypass][D-020]") {
    constexpr size_t kBlock = 512;
    constexpr size_t kOnBlock = 24;   // bypass on at 0.256 s
    constexpr size_t kOffBlock = 48;  // bypass off again at 0.512 s
    const auto in = sineAt(kBlock * 72, 220.0, -12.0);

    ts::Rig rig(kBlockSr);
    rig.apply(hardClipBands(1.0, 4, false));
    rig.reactivate();
    std::vector<float> outL;
    std::vector<float> outR;
    rig.render(in, in, outL, outR, static_cast<Steinberg::int32>(kBlock),
               [&](size_t blk, size_t, ts::PChanges& pc) {
                   if (blk != kOnBlock && blk != kOffBlock) return;
                   for (uint8_t b = 0; b < 4; ++b) {
                       pc.addChange(makeBandParamId(b, BandParamType::kBandBypass),
                                    blk == kOnBlock ? 1.0 : 0.0);
                   }
               });

    size_t nonFinite = 0;
    for (size_t i = 0; i < outL.size(); ++i) {
        if (!ts::fin(outL[i]) || !ts::fin(outR[i])) ++nonFinite;
    }
    REQUIRE(nonFinite == 0);

    // While bypassed (one block after the toggle onward) the output is the clean crossover
    // sum, delayed by the reported latency (T2.10)
    const auto latency = static_cast<size_t>(rig.p->getLatencySamples());
    const auto clean = delayedBy(crossoverSum(in, 4), latency);
    const size_t on = kOnBlock * kBlock;
    const size_t off = kOffBlock * kBlock;
    const double bypassedDiff = maxAbsDiff(outL, clean, on, off);
    INFO("latency " << latency << "; max |out - crossover sum| while bypassed: " << bypassedDiff);
    REQUIRE(bypassedDiff < 1e-5);

    // The hard switch steps between the distorted and the clean signal: no step may exceed
    // the two signals' combined peak (no blow-up), and the steps are reported.
    const double distortedPeak = ts::peakOf(outL.data() + 8 * kBlock, on - 8 * kBlock);
    const double cleanPeak = ts::peakOf(clean.data() + on, off - on);
    const double steadyStep = maxStep(outL, 8 * kBlock, on - 1);
    const double stepOn = maxStep(outL, on - 2, on + 2);
    const double stepOff = maxStep(outL, off - 2, off + 2);
    INFO("distorted peak " << distortedPeak << ", clean peak " << cleanPeak << ", steady max step "
         << steadyStep << ", step at bypass on " << stepOn << ", at bypass off " << stepOff);
    REQUIRE(stepOn <= distortedPeak + cleanPeak);
    REQUIRE(stepOff <= distortedPeak + cleanPeak);
}

TEST_CASE("Processor block path with oversampled bands allocates nothing",
          "[disrumpo][oversampling][realtime][D-027]") {
    constexpr Steinberg::int32 kBlock = 512;
    ts::Rig rig(kBlockSr, kBlock);
    rig.apply(hardClipBands(1.0, 4, false));  // 4 bands, 4x
    rig.reactivate();
    REQUIRE(rig.p->getBandProcessor(0).getOversampleFactor() == 4);

    const auto in = sineAt(static_cast<size_t>(kBlock), 220.0, -12.0);
    std::vector<float> inL(in);
    std::vector<float> inR(in);
    std::vector<float> outL(in.size());
    std::vector<float> outR(in.size());
    for (int b = 0; b < 8; ++b) {  // settle outside the scope
        rig.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), kBlock, nullptr);
    }
    ts::PChanges toTwoX;  // filled before the scope: Oversample Max 2x starts a factor switch
    toTwoX.addChange(Disrumpo::kOversampleMaxId, 1.0 / 3.0);

    size_t allocations = 0;
    {
        TestHelpers::AllocationScope scope;
        for (int b = 0; b < 16; ++b) {
            rig.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), kBlock,
                             b == 4 ? &toTwoX : nullptr);
        }
        allocations = TestHelpers::AllocationDetector::instance().getAllocationCount();
    }
    CHECK(rig.p->getBandProcessor(0).getActiveOversampleFactor() == 2);
    REQUIRE(allocations == 0);
}

namespace {

/// One band, node 0 Tube at drive 0.1 (linear region; a 2x type: SoftClip, which this read
/// before, is 4x since the drive re-stage ruling 2026-11-10), mix 1, cursor on node 0.
/// Returns the RMS of the last 0.25 s of a 0.5 s sine at -30 dBFS.
double tubeToneRms(double toneNorm, double freqHz, double oversampleMaxNorm, int& factor) {
    ts::Rig rig(kBlockSr);
    rig.apply({{Disrumpo::kBandCountId, 0.0},
               {makeNodeParamId(0, 0, NodeParamType::kNodeType), typeNorm(DistortionType::Tube)},
               {makeNodeParamId(0, 0, NodeParamType::kNodeDrive), 0.01},
               {makeNodeParamId(0, 0, NodeParamType::kNodeTone), toneNorm},
               {makeNodeParamId(0, 0, NodeParamType::kNodeMix), 1.0},
               {makeBandParamId(0, BandParamType::kBandMorphX), 0.0},
               {makeBandParamId(0, BandParamType::kBandMorphY), 0.0},
               {Disrumpo::kOversampleMaxId, oversampleMaxNorm}});
    rig.reactivate();
    const auto in = sineAt(static_cast<size_t>(kBlockSr / 2.0), freqHz, -30.0);
    std::vector<float> outL;
    std::vector<float> outR;
    rig.render(in, in, outL, outR);
    factor = rig.p->getBandProcessor(0).getOversampleFactor();
    const size_t half = outL.size() / 2;
    return ts::rmsOf(outL.data() + half, outL.size() - half);
}

} // anonymous namespace

// Audit D-009 (fix plan T1.06): the adapters' tone filter was prepared for 8x the host
// rate and clocked at the band's running rate, so Tone 8 kHz acted as about 1 kHz.
// Each tone level is read relative to a 250 Hz reference through the same chain, which
// cancels the drive scaling (0.1), the shaper's small-signal gain and the centre-pan law
// (D-021), and leaves the one-pole tone response. Expected (one-pole, a = exp(-2 pi fc/fs)):
// 8 kHz tone at 4 kHz: -0.87 dB at 1x, -0.94 dB at 2x; 4 kHz tone at 1 kHz: -0.26 dB at both.
TEST_CASE("Processor tone filter cutoff matches the Tone parameter", "[disrumpo][tone][D-009]") {
    constexpr double kRefHz = 250.0;
    constexpr double kTone8k = 1.0;
    constexpr double kTone4k = (4000.0 - 200.0) / 7800.0;
    const auto check = [&](double oversampleMaxNorm, int expectedFactor) {
        int factor = 0;
        const double ref8k = tubeToneRms(kTone8k, kRefHz, oversampleMaxNorm, factor);
        const double at4k = tubeToneRms(kTone8k, 4000.0, oversampleMaxNorm, factor);
        const double ref4k = tubeToneRms(kTone4k, kRefHz, oversampleMaxNorm, factor);
        const double at1k = tubeToneRms(kTone4k, 1000.0, oversampleMaxNorm, factor);
        const double db8k = ts::toDb(at4k / ref8k);
        const double db4k = ts::toDb(at1k / ref4k);
        INFO("running factor " << factor << ": tone 8 kHz at 4 kHz " << db8k
             << " dB, tone 4 kHz at 1 kHz " << db4k << " dB (re 250 Hz)");
        REQUIRE(factor == expectedFactor);
        REQUIRE(std::fabs(db8k) <= 1.0);
        REQUIRE(std::fabs(db4k - (-0.26)) <= 0.5);
    };
    SECTION("Oversample Max 1x") { check(0.0, 1); }
    SECTION("Oversample Max 4x (Tube runs at 2x)") { check(2.0 / 3.0, 2); }

    // The audit probe "tone-response" (fix plan T6.02): with Tone 4 kHz the processor's level
    // at 1 / 2 / 4 / 8 kHz (re 250 Hz) is within 1 dB of a DistortionAdapter prepared at the
    // host rate. Baseline: processor -2.98 / -7.00 / -12.46 / -18.93 dB against the adapter's
    // -0.25 / -0.94 / -2.91 / -6.59 dB (re 100 Hz).
    SECTION("Tone 4 kHz matches a DistortionAdapter prepared at the host rate") {
        const auto adapterRms = [](double freqHz) {
            auto adapter = std::make_unique<Disrumpo::DistortionAdapter>();
            adapter->prepare(kBlockSr, 512);
            adapter->setType(DistortionType::Tube);
            adapter->setCommonParams({.drive = 0.1f, .mix = 1.0f, .toneHz = 4000.0f});
            const auto in = sineAt(static_cast<size_t>(kBlockSr / 2.0), freqHz, -30.0);
            std::vector<float> out(in.size());
            for (size_t i = 0; i < in.size(); ++i) out[i] = adapter->process(in[i]);
            const size_t half = out.size() / 2;
            return ts::rmsOf(out.data() + half, out.size() - half);
        };
        for (const double osMax : {0.0, 2.0 / 3.0}) {
            int factor = 0;
            const double procRef = tubeToneRms(kTone4k, kRefHz, osMax, factor);
            const double adapterRef = adapterRms(kRefHz);
            for (const double f : {1000.0, 2000.0, 4000.0, 8000.0}) {
                const double procDb = ts::toDb(tubeToneRms(kTone4k, f, osMax, factor) / procRef);
                const double adapterDb = ts::toDb(adapterRms(f) / adapterRef);
                std::printf("T6.02 tone 4 kHz at %.0f Hz (running factor %d): processor %.2f dB, adapter %.2f dB "
                            "(re 250 Hz)\n", f, factor, procDb, adapterDb);
                INFO("running factor " << factor << ", " << f << " Hz: processor " << procDb << " dB, adapter "
                     << adapterDb << " dB (re 250 Hz)");
                CHECK(std::fabs(procDb - adapterDb) <= 1.0);
            }
        }
    }
}

namespace {

struct ChannelLevels {
    double leftDb = 0.0;   // output RMS re input RMS, last 0.25 s
    double rightDb = 0.0;
};

/// Neutral settings: every node of every band at drive 0 (adapter passthrough), band gain
/// 0 dB (default), global mix 1; `numBands` bands, every band at `panNorm`. 1 kHz at -12 dBFS
/// on both inputs, 0.5 s.
ChannelLevels neutralLevels(int numBands, double panNorm) {
    ts::PList l{{Disrumpo::kBandCountId, static_cast<double>(numBands - 1) / 3.0},
                {Disrumpo::kGlobalMixId, 1.0}};
    for (uint8_t b = 0; b < 4; ++b) {
        for (uint8_t n = 0; n < 4; ++n) l.emplace_back(makeNodeParamId(b, n, NodeParamType::kNodeDrive), 0.0);
        l.emplace_back(makeBandParamId(b, BandParamType::kBandPan), panNorm);
    }
    ts::Rig rig(kBlockSr);
    rig.apply(l);
    rig.reactivate();
    const auto in = sineAt(static_cast<size_t>(kBlockSr / 2.0), 1000.0, -12.0);
    std::vector<float> outL;
    std::vector<float> outR;
    rig.render(in, in, outL, outR);
    const size_t half = in.size() / 2;
    const double inRms = ts::rmsOf(in.data() + half, in.size() - half);
    return {.leftDb = ts::toDb(ts::rmsOf(outL.data() + half, outL.size() - half) / inRms),
            .rightDb = ts::toDb(ts::rmsOf(outR.data() + half, outR.size() - half) / inRms)};
}

} // anonymous namespace

// Audit D-021 (fix plan T1.08): band pan is a stereo BALANCE control on an already-stereo
// band signal: leftCoeff = min(1, 1 - pan), rightCoeff = min(1, 1 + pan). Centre leaves both
// channels untouched (unity), panning attenuates only the opposite side, hard left/right
// keep the near side at unity and silence the far side. The former mono equal-power law
// (cos/sin of pan*pi/4 + pi/4) put every band at -3.01 dB at centre.
TEST_CASE("Neutral settings are unity gain", "[disrumpo][unity][D-021]") {
    for (int numBands = 1; numBands <= 4; ++numBands) {
        const auto lv = neutralLevels(numBands, 0.5);
        INFO(numBands << " band(s), centre pan: L " << lv.leftDb << " dB, R " << lv.rightDb << " dB re input");
        CHECK(std::fabs(lv.leftDb) < 0.1);
        CHECK(std::fabs(lv.rightDb) < 0.1);
    }
}

TEST_CASE("Hard band pan keeps the near side at unity and silences the far side", "[disrumpo][unity][D-021]") {
    SECTION("hard left") {
        const auto lv = neutralLevels(4, 0.0);
        INFO("4 bands, hard left: L " << lv.leftDb << " dB, R " << lv.rightDb << " dB re input");
        REQUIRE(std::fabs(lv.leftDb) < 0.1);
        REQUIRE(lv.rightDb < -120.0);
    }
    SECTION("hard right") {
        const auto lv = neutralLevels(4, 1.0);
        INFO("4 bands, hard right: L " << lv.leftDb << " dB, R " << lv.rightDb << " dB re input");
        REQUIRE(lv.leftDb < -120.0);
        REQUIRE(std::fabs(lv.rightDb) < 0.1);
    }
}

namespace {

constexpr size_t kMixIrLength = 65536;

/// The default crossovers (logarithmic 20 Hz - 20 kHz for 4 bands; fewer bands use the lowest).
std::array<double, 3> defaultCrossoversHz() {
    const double logMin = std::log10(20.0);
    const double step = (std::log10(20000.0) - logMin) / 4.0;
    return {std::pow(10.0, logMin + step), std::pow(10.0, logMin + 2.0 * step),
            std::pow(10.0, logMin + 3.0 * step)};
}

/// Impulse response of the processor (left channel): `numBands` bands, every node at drive 0
/// (adapter passthrough), centre pan, Oversample Max 1x, global mix `mix`. `afterActivation`
/// is applied once the processor is running (crossover moves, band-count changes). 0.3 s
/// pre-roll, then an impulse of kMixImpulse; returns the kMixIrLength samples from the impulse
/// on, scaled back to a unit impulse. `oversampleMaxNorm` other than 0 sets a higher Oversample
/// Max (T2.10). The impulse stays below the output safety clipper's knee (-6 dBFS, user ruling
/// 2026-11-10), where it is an exact passthrough: a unit impulse reads the clipper (-0.31 dB at
/// 270 Hz through one neutral band), not the linear response these cases measure.
constexpr float kMixImpulse = 0.25f;  // -12 dBFS; a power of two, so the scaling back is exact
std::vector<float> mixImpulseResponse(double sr, int numBands, double mix, const ts::PList& afterActivation = {},
                                      double oversampleMaxNorm = 0.0) {
    ts::PList l{{Disrumpo::kBandCountId, static_cast<double>(numBands - 1) / 3.0},
                {Disrumpo::kGlobalMixId, mix},
                {Disrumpo::kOversampleMaxId, oversampleMaxNorm}};
    for (uint8_t b = 0; b < 4; ++b) {
        for (uint8_t n = 0; n < 4; ++n) l.emplace_back(makeNodeParamId(b, n, NodeParamType::kNodeDrive), 0.0);
        l.emplace_back(makeBandParamId(b, BandParamType::kBandPan), 0.5);
    }
    ts::Rig rig(sr);
    rig.apply(l);
    rig.reactivate();
    if (!afterActivation.empty()) rig.apply(afterActivation);
    const auto preRoll = static_cast<size_t>(sr * 0.3);
    std::vector<float> in(preRoll + kMixIrLength, 0.0f);
    in[preRoll] = kMixImpulse;
    std::vector<float> outL;
    std::vector<float> outR;
    rig.render(in, in, outL, outR);
    if (oversampleMaxNorm == 0.0) {
        REQUIRE(rig.p->getBandProcessor(0).getOversampleFactor() == 1);
    }
    std::vector<float> h(outL.begin() + static_cast<std::ptrdiff_t>(preRoll), outL.end());
    for (auto& v : h) v /= kMixImpulse;
    return h;
}

/// 480 log-spaced frequencies 30 Hz - 18 kHz plus 1 kHz, the default crossovers and `extraHz`.
std::vector<double> mixGrid(std::initializer_list<double> extraHz = {}) {
    std::vector<double> g;
    constexpr int kPoints = 480;
    g.reserve(kPoints + 4 + extraHz.size());
    for (int i = 0; i < kPoints; ++i) {
        g.push_back(30.0 * std::pow(18000.0 / 30.0, static_cast<double>(i) / (kPoints - 1)));
    }
    g.push_back(1000.0);
    for (const double f : defaultCrossoversHz()) g.push_back(f);
    for (const double f : extraHz) g.push_back(f);
    return g;
}

struct ResponseExtremes {
    double worstDb = 0.0;  // largest |level - reference|
    double worstHz = 0.0;
    double minDb = 0.0;    // deepest level re the reference
    double minHz = 0.0;
};

/// Level across `grid` relative to `refDb`.
ResponseExtremes responseExtremes(const std::vector<float>& h, double sr, const std::vector<double>& grid, double refDb) {
    ResponseExtremes r;
    r.minDb = 1e9;
    for (const double f : grid) {
        const double d = ts::responseDb(h, f, sr) - refDb;
        if (std::fabs(d) > std::fabs(r.worstDb)) { r.worstDb = d; r.worstHz = f; }
        if (d < r.minDb) { r.minDb = d; r.minHz = f; }
    }
    return r;
}

size_t peakIndex(const std::vector<float>& x) {
    size_t p = 0;
    for (size_t i = 1; i < x.size(); ++i) {
        if (std::fabs(x[i]) > std::fabs(x[p])) p = i;
    }
    return p;
}

double crossoverNorm(double hz) {
    return (std::log10(hz) - std::log10(20.0)) / (std::log10(20000.0) - std::log10(20.0));
}

} // anonymous namespace

// Audit D-018 (fix plan T1.09): the dry term of the global mix was the raw input, while the
// clean wet sum of an N-band LR4 cascade with D'Appolito compensation is the allpass chain
// AP(f0)...AP(fN-2) (-180 degrees at each crossover). Mixing the two notched every
// crossover frequency (audit probe: -16.6 dB at 114 Hz and 2 kHz at mix 50 %).
TEST_CASE("Global mix 50 % does not notch the crossover frequencies", "[disrumpo][mix][D-018]") {
    constexpr double kSr = 48000.0;
    const auto grid = mixGrid();
    for (const int numBands : {4, 2}) {
        const auto h = mixImpulseResponse(kSr, numBands, 0.5);
        const auto r = responseExtremes(h, kSr, grid, ts::responseDb(h, 1000.0, kSr));
        INFO(numBands << " bands, mix 50 %: deepest " << r.minDb << " dB at " << r.minHz
             << " Hz, worst |dev| " << r.worstDb << " dB at " << r.worstHz << " Hz (re 1 kHz, 30 Hz - 18 kHz)");
        CHECK(std::fabs(r.worstDb) <= 0.5);
    }
}

// With every band neutral, mix 50 % must reproduce the input within the crossover's own
// reconstruction error (SC-001: +/-0.1 dB), at every band count and sample rate, and after
// crossover moves and band-count changes made while running.
// Mix 100 % (the clean wet sum alone) is the audit probe "crossover:processor" / "unity:processor"
// (fix plan T6.04; baseline ripple 0.007 dB, level -3.010 dB at every band count).
TEST_CASE("Global mix 50 % and 100 % with neutral bands reproduce the input within the crossover error",
          "[disrumpo][mix][unity][D-018][D-021]") {
    const auto grid = mixGrid();
    for (const double sr : {44100.0, 48000.0, 96000.0, 192000.0}) {
        for (int numBands = 1; numBands <= 4; ++numBands) {
            for (const double mix : {0.5, 1.0}) {
                const auto h = mixImpulseResponse(sr, numBands, mix);
                const auto r = responseExtremes(h, sr, grid, 0.0);
                INFO(sr << " Hz, " << numBands << " band(s), mix " << mix * 100.0 << " %: worst |dev| " << r.worstDb
                     << " dB at " << r.worstHz << " Hz (re input, 30 Hz - 18 kHz)");
                CHECK(std::fabs(r.worstDb) <= 0.1);
            }
        }
    }
    {  // crossover 2 moved to 1.2 kHz and 3 bands, set while running
        constexpr double kSr = 48000.0;
        const auto h = mixImpulseResponse(kSr, 4, 0.5,
                                          {{Disrumpo::makeCrossoverParamId(1), crossoverNorm(1200.0)},
                                           {Disrumpo::kBandCountId, 2.0 / 3.0}});
        const auto r = responseExtremes(h, kSr, mixGrid({1200.0}), 0.0);
        INFO("48 kHz, 3 bands (112 Hz, 1.2 kHz), mix 50 %: worst |dev| " << r.worstDb << " dB at " << r.worstHz << " Hz");
        CHECK(std::fabs(r.worstDb) <= 0.1);
    }
}

// The dry path (mix 0) must carry the same phase and timing as the clean wet sum (mix 1):
// same impulse peak sample, residual of the difference at least 60 dB below the wet energy.
// 192 kHz is asserted since the crossover's allpasses run in double precision (follow-up 7;
// float32 Biquad allpasses read -59.55 dB there at the 112 Hz crossover).
TEST_CASE("Dry path is phase- and time-aligned with the clean wet path at 1x", "[disrumpo][mix][D-018]") {
    for (const double sr : {44100.0, 48000.0, 96000.0, 192000.0}) {
        for (const int numBands : {2, 4}) {
            const auto dry = mixImpulseResponse(sr, numBands, 0.0);
            const auto wet = mixImpulseResponse(sr, numBands, 1.0);
            double diffEnergy = 0.0;
            double wetEnergy = 0.0;
            for (size_t i = 0; i < wet.size(); ++i) {
                const double d = static_cast<double>(dry[i]) - static_cast<double>(wet[i]);
                diffEnergy += d * d;
                wetEnergy += static_cast<double>(wet[i]) * static_cast<double>(wet[i]);
            }
            const double residualDb = 10.0 * std::log10(diffEnergy / wetEnergy + 1e-30);
            INFO(sr << " Hz, " << numBands << " bands: impulse peak dry " << peakIndex(dry) << ", wet "
                 << peakIndex(wet) << "; dry - wet residual " << residualDb << " dB re wet; max |dry - wet| "
                 << maxAbsDiff(dry, wet));
            CHECK(peakIndex(dry) == peakIndex(wet));
            CHECK(residualDb <= -60.0);
        }
    }
}

// T2.10 (D-026, follow-up 5): at Oversample Max 4x the processor reports the bands' worst-case
// latency and delays the dry path and every band to it. Neutral bands (drive 0, clocked at 1x)
// carry the same delay as the dry path, so mix 50 % stays within SC-001. With the oversampler
// running (HardClip at 4x, node mix 0: only its IIR filters), the dry path is delayed by the
// oversampler's impulse-peak delay; the residual is the IIR's phase against a pure delay.
TEST_CASE("Global mix 50 % stays flat with Oversample Max 4x", "[disrumpo][mix][latency][D-026]") {
    const auto grid = mixGrid();
    for (const double sr : {44100.0, 48000.0, 96000.0}) {
        for (const int numBands : {1, 2, 4}) {
            const auto h = mixImpulseResponse(sr, numBands, 0.5, {}, 2.0 / 3.0);
            const auto r = responseExtremes(h, sr, grid, 0.0);
            INFO(sr << " Hz, " << numBands << " band(s), Oversample Max 4x, neutral bands, mix 50 %: worst |dev| "
                 << r.worstDb << " dB at " << r.worstHz << " Hz");
            CHECK(std::fabs(r.worstDb) <= 0.1);
        }
    }
    for (const int numBands : {1, 4}) {
        ts::PList oversampled;
        for (uint8_t b = 0; b < static_cast<uint8_t>(numBands); ++b) {
            oversampled.emplace_back(makeNodeParamId(b, 0, NodeParamType::kNodeType), typeNorm(DistortionType::HardClip));
            oversampled.emplace_back(makeNodeParamId(b, 0, NodeParamType::kNodeDrive), 1.0);
            oversampled.emplace_back(makeNodeParamId(b, 0, NodeParamType::kNodeMix), 0.0);
        }
        const auto h = mixImpulseResponse(kBlockSr, numBands, 0.5, oversampled, 2.0 / 3.0);
        const auto r = responseExtremes(h, kBlockSr, grid, ts::responseDb(h, 1000.0, kBlockSr));
        const auto dry = mixImpulseResponse(kBlockSr, numBands, 0.0, oversampled, 2.0 / 3.0);
        const auto wet = mixImpulseResponse(kBlockSr, numBands, 1.0, oversampled, 2.0 / 3.0);
        // Alignment: the lag (-16..16) at which the dry response best matches the wet one
        long bestLag = 0;
        double bestCorr = -1e300;
        for (long lag = -16; lag <= 16; ++lag) {
            double acc = 0.0;
            for (size_t i = 0; i < wet.size(); ++i) {
                const long j = static_cast<long>(i) - lag;
                if (j < 0 || std::cmp_greater_equal(j, dry.size())) continue;
                acc += static_cast<double>(wet[i]) * static_cast<double>(dry[static_cast<size_t>(j)]);
            }
            if (acc > bestCorr) {
                bestCorr = acc;
                bestLag = lag;
            }
        }
        const double ref = ts::responseDb(h, 1000.0, kBlockSr);
        const double gdDry = ts::groupDelaySamples(dry, 1000.0, kBlockSr);
        const double gdWet = ts::groupDelaySamples(wet, 1000.0, kBlockSr);
        std::printf("T2.10 mix 50 %% with %d HardClip band(s) at 4x (node mix 0): worst |dev| %.3f dB at %.0f Hz "
                    "(re 1 kHz, 30 Hz - 18 kHz), at 5 kHz %.3f dB, at 10 kHz %.3f dB; impulse peak dry %zu wet %zu, "
                    "dry/wet best-match lag %ld, group delay at 1 kHz dry %.3f wet %.3f\n",
                    numBands, r.worstDb, r.worstHz, ts::responseDb(h, 5000.0, kBlockSr) - ref,
                    ts::responseDb(h, 10000.0, kBlockSr) - ref, peakIndex(dry), peakIndex(wet), bestLag, gdDry, gdWet);
        // Follow-up 81, minimax ruling (2026-11-09): the 4x wet path arrives at the dry path's
        // group delay plus the 48 kHz 4x minimax offset, -0.506 sample, +/- 0.1 (ffd51f47: +0.008;
        // before it the integer best-match lag was 0 while the wet path was 0.52 samples early)
        CHECK(std::fabs(gdWet - gdDry - (-0.506)) <= 0.1);
    }
}

// Measurement probe (hidden): residual of CrossoverNetwork's float32 dry allpass chain, and of
// a double-precision RBJ allpass at the same frequencies, against the (double-precision LR4)
// clean band sum. Separates the dry chain's float32 rounding from any structural mismatch.
TEST_CASE("Probe: dry allpass precision vs band sum", "[.][t109-probe]") {
    for (const double sr : {44100.0, 48000.0, 96000.0, 192000.0}) {
        for (const int numBands : {2, 4}) {
            Disrumpo::CrossoverNetwork xo;
            xo.prepare(sr, Disrumpo::kDefaultBands);
            xo.setBandCount(numBands);
            xo.reset();
            struct ApD { double b0, b1, b2, a1, a2, z1 = 0.0, z2 = 0.0; };
            std::vector<ApD> aps;
            for (int i = 0; i < numBands - 1; ++i) {
                const double w = 2.0 * std::numbers::pi * static_cast<double>(xo.getCrossoverFrequency(i)) / sr;
                const double alpha = std::sin(w) / (2.0 * static_cast<double>(
                                                        Krate::DSP::MultibandCrossover<Disrumpo::kMaxBands>::kAllpassQ));
                const double a0 = 1.0 + alpha;
                aps.push_back({.b0 = (1.0 - alpha) / a0, .b1 = -2.0 * std::cos(w) / a0, .b2 = 1.0,
                               .a1 = -2.0 * std::cos(w) / a0, .a2 = (1.0 - alpha) / a0});
            }
            std::array<float, Disrumpo::kMaxBands> bands{};
            double eSum = 0.0;
            double eFloat = 0.0;
            double eDouble = 0.0;
            for (size_t n = 0; n < kMixIrLength; ++n) {
                const float x = n == 0 ? 1.0f : 0.0f;
                xo.process(x, bands);
                double sum = 0.0;
                for (int b = 0; b < numBands; ++b) sum += static_cast<double>(bands[static_cast<size_t>(b)]);
                const double dryF = static_cast<double>(xo.processDryCompensation(x));
                double y = static_cast<double>(x);
                for (auto& ap : aps) {
                    const double out = ap.b0 * y + ap.z1;
                    ap.z1 = ap.b1 * y - ap.a1 * out + ap.z2;
                    ap.z2 = ap.b2 * y - ap.a2 * out;
                    y = out;
                }
                eSum += sum * sum;
                eFloat += (dryF - sum) * (dryF - sum);
                eDouble += (y - sum) * (y - sum);
            }
            WARN("PROBE dry-ap: " << sr << " Hz, " << numBands << " bands: float32 Biquad chain residual "
                 << 10.0 * std::log10(eFloat / eSum + 1e-30) << " dB, double RBJ chain residual "
                 << 10.0 * std::log10(eDouble / eSum + 1e-30) << " dB (re band-sum energy)");
        }
    }
}

// Measurement probe (hidden): impulse delay through the band oversampler vs getLatencySamples().
TEST_CASE("Probe: oversampled band impulse delay vs reported latency", "[.][t107-probe]") {
    for (const double norm : {0.0, 1.0 / 3.0, 2.0 / 3.0}) {
        ts::Rig rig(kBlockSr);
        // HardClip drive 10 keeps the drive gate open; node mix 0 makes the band output the dry
        // (pre-shaper) signal, so only the oversampler's up/down filters shape the impulse.
        rig.apply({{Disrumpo::kBandCountId, 0.0},
                   {makeNodeParamId(0, 0, NodeParamType::kNodeType), typeNorm(DistortionType::HardClip)},
                   {makeNodeParamId(0, 0, NodeParamType::kNodeDrive), 1.0},
                   {makeNodeParamId(0, 0, NodeParamType::kNodeMix), 0.0},
                   {makeBandParamId(0, BandParamType::kBandMorphX), 0.0},
                   {makeBandParamId(0, BandParamType::kBandMorphY), 0.0},
                   {Disrumpo::kOversampleMaxId, norm}});
        rig.reactivate();
        constexpr size_t kAt = 4800;
        std::vector<float> in(9600, 0.0f);
        in[kAt] = 0.25f;
        std::vector<float> outL;
        std::vector<float> outR;
        rig.render(in, in, outL, outR);
        size_t peakIdx = 0;
        for (size_t i = 0; i < outL.size(); ++i) {
            if (std::fabs(outL[i]) > std::fabs(outL[peakIdx])) peakIdx = i;
        }
        WARN("PROBE latency: factor " << rig.p->getBandProcessor(0).getOversampleFactor()
             << " peak offset " << static_cast<long long>(peakIdx) - static_cast<long long>(kAt)
             << " samples, peak " << outL[peakIdx] << ", getLatencySamples " << rig.p->getLatencySamples());
    }
}

namespace {

/// One band, node 0 of `type` with the shape slots a fresh node of that type starts
/// with, drive `driveNorm`, every other parameter at its default (tone 4 kHz, mix 1);
/// 1031.25 Hz at -6 dBFS, 1 s. Returns the left channel.
std::vector<float> renderFreshNode(DistortionType type, double driveNorm, double inScale = 1.0) {
    ts::Rig rig(kBlockSr);
    rig.apply({{Disrumpo::kBandCountId, 0.0},
               {makeNodeParamId(0, 0, NodeParamType::kNodeType), typeNorm(type)},
               {makeNodeParamId(0, 0, NodeParamType::kNodeDrive), driveNorm},
               {makeBandParamId(0, BandParamType::kBandMorphX), 0.0},
               {makeBandParamId(0, BandParamType::kBandMorphY), 0.0}});
    rig.reactivate();
    auto in = sineAt(static_cast<size_t>(kBlockSr), kAliasToneHz, -6.0);
    for (auto& v : in) v = static_cast<float>(inScale * v);
    std::vector<float> outL;
    std::vector<float> outR;
    rig.render(in, in, outL, outR);
    return outL;
}

/// Peak (dBFS) of the last 0.5 s.
double tailPeakDb(const std::vector<float>& x) {
    const size_t half = x.size() / 2;
    return ts::toDb(ts::peakOf(x.data() + half, x.size() - half));
}

/// Magnitude-weighted spectral centroid (Hz) over the last kAliasFrames Hann frames.
double centroidHz(const std::vector<float>& x) {
    Krate::DSP::FFT fft;
    fft.prepare(kAliasFft);
    std::vector<float> window(kAliasFft);
    Krate::DSP::Window::generateHann(window.data(), kAliasFft);
    std::vector<float> frame(kAliasFft);
    std::vector<Krate::DSP::Complex> spectrum(fft.numBins());
    double weighted = 0.0;
    double total = 0.0;
    const size_t start = x.size() - kAliasFrames * kAliasFft;
    for (size_t f = 0; f < kAliasFrames; ++f) {
        for (size_t i = 0; i < kAliasFft; ++i) frame[i] = x[start + f * kAliasFft + i] * window[i];
        fft.forward(frame.data(), spectrum.data());
        for (size_t b = 1; b < spectrum.size(); ++b) {
            const double m = spectrum[b].magnitude();
            weighted += m * static_cast<double>(b) * kBlockSr / static_cast<double>(kAliasFft);
            total += m;
        }
    }
    return total > 0.0 ? weighted / total : 0.0;
}

} // anonymous namespace

// Audit D-145 / D-159 (fix plan T2.06): the three wavefolders at their fresh-node defaults
// read 20-45 dB below the other types and got quieter as Drive rose (audit probe: SineFold
// -23.2 / -26.6 / -31.0 dBFS, SergeFold -47.2 / -47.5 / -35.9 dBFS peak at drive 2.5 / 7.5 / 10).
//
// What is pinned: a fresh fold node is not near-silent. Peak >= -12 dBFS at every drive,
// within 6 dB of SoftClip at drive 2.5 and 7.5, and at most 6 dB lower at drive 10 than at
// drive 2.5. "Peak never falls as drive rises" is not a property of a folder behind a tone
// filter: more drive raises the fold index (sin(folds * drive * x)), moves the fold energy
// upward, and the node's one-pole Tone (4 kHz fresh) and the downsampler remove it. Measured
// with Tone 8 kHz at 1x, SineFold still reads -0.24 / -0.10 / -0.78 / -1.73 dBFS peak at
// drive 2.5 / 5 / 7.5 / 10. So drive 10 is held to a bounded drop, not to SoftClip's 0 dBFS.
//
// Measured, peak dBFS at drive 2.5 / 7.5 / 10 (SoftClip -0.01 / -0.00 / -0.00), 48 kHz:
// - after T1.06/T1.08, before T2.06: SineFold and SergeFold -1.59 / -4.44 / -9.75,
//   TriangleFold -26.31 / -35.87 / -36.35 (Smooth 0.5 = 3.16 kHz low-pass, folds 6.5,
//   triangle output capped at 1/folds).
// - after T2.06 (folds 2, Smooth 0, makeup x folds): SineFold and SergeFold
//   -0.68 / -1.76 / -5.07, TriangleFold -3.94 / -3.06 / -6.22.
TEST_CASE("Wavefold types at default settings are not near-silent and stay near SoftClip",
          "[disrumpo][wavefold][D-145][D-159]") {
    // Re-baselined for the drive re-stage (user ruling 2026-11-10): the -6 dBFS sine is scaled by
    // 1/8 (-24.06 dBFS), so the re-staged shapers see the drive this case was written for and
    // every output is the old one 18.06 dB lower: the -12 dBFS floor is -30.06 dBFS, the relative
    // bars stand. At -6 dBFS the shapers run 18 dB hotter: SoftClip sits at its ceiling (-18.06
    // dBFS) at every drive and the folders fold far further, their energy moving up to where the
    // node's Tone filter takes it (TriangleFold -26.6 / -32.4 / -35.2 dBFS at the first run).
    constexpr double kOldStaging = 0.125;
    constexpr double kFloorDb = -12.0 - 18.06;
    constexpr std::array<double, 3> kDrives{0.25, 0.75, 1.0};
    std::array<double, 3> softClip{};
    for (size_t d = 0; d < kDrives.size(); ++d) {
        softClip[d] = tailPeakDb(renderFreshNode(DistortionType::SoftClip, kDrives[d], kOldStaging));
    }
    std::printf("D-145 SoftClip fresh node peak: %.2f / %.2f / %.2f dBFS at drive 2.5 / 7.5 / 10\n", softClip[0],
                softClip[1], softClip[2]);
    for (const auto type : {DistortionType::SineFold, DistortionType::TriangleFold, DistortionType::SergeFold}) {
        std::array<double, 3> peak{};
        for (size_t d = 0; d < kDrives.size(); ++d) peak[d] = tailPeakDb(renderFreshNode(type, kDrives[d], kOldStaging));
        std::printf("D-145 type %d fresh node peak: %.2f / %.2f / %.2f dBFS at drive 2.5 / 7.5 / 10\n",
                    static_cast<int>(type), peak[0], peak[1], peak[2]);
        INFO("type " << static_cast<int>(type) << " peak " << peak[0] << " / " << peak[1] << " / " << peak[2]
             << " dBFS, SoftClip " << softClip[0] << " / " << softClip[1] << " / " << softClip[2] << " dBFS");
        for (size_t d = 0; d < kDrives.size(); ++d) CHECK(peak[d] >= kFloorDb);
        CHECK(std::fabs(peak[0] - softClip[0]) <= 6.0);  // drive 2.5
        CHECK(std::fabs(peak[1] - softClip[1]) <= 6.0);  // drive 7.5
        CHECK(peak[0] - peak[2] <= 6.0);                 // drive 2.5 -> 10
    }
}

// Audit D-157 (fix plan T2.06): both crossfades defaulted to a 50/50 blend of the same sine
// and triangle folds, so a fresh SineFold and a fresh TriangleFold node were identical.
TEST_CASE("SineFold and TriangleFold differ at defaults", "[disrumpo][wavefold][D-157]") {
    const auto sine = renderFreshNode(DistortionType::SineFold, 0.5);
    const auto tri = renderFreshNode(DistortionType::TriangleFold, 0.5);
    const size_t half = sine.size() / 2;
    const double rmsDiffDb = std::fabs(ts::toDb(ts::rmsOf(sine.data() + half, sine.size() - half)) -
                                       ts::toDb(ts::rmsOf(tri.data() + half, tri.size() - half)));
    const double sineCentroid = centroidHz(sine);
    const double triCentroid = centroidHz(tri);
    const double ratio = sineCentroid / triCentroid;
    std::printf("D-157 fresh SineFold vs TriangleFold at drive 5: RMS difference %.2f dB, centroid %.0f vs %.0f Hz "
                "(ratio %.3f)\n", rmsDiffDb, sineCentroid, triCentroid, ratio);
    REQUIRE((rmsDiffDb > 1.0 || ratio < 0.85 || ratio > 1.15));
}

// Found by the T6.02 parity probe: Temporal and Stochastic read the Drive inside the adapter's
// parameter routing, and the morph engine routes a node's type parameters before it stores
// the node's new Drive, so on a 2-node band one Drive change reached those two types only
// with the next node update. Node A carries the type, the cursor sits on it; node B is the
// same type (identical-type path) or SoftClip (parallel path).
TEST_CASE("One Drive change reaches a Temporal and a Stochastic node", "[disrumpo][drive]") {
    const auto in = sineAt(static_cast<size_t>(kBlockSr), 1031.25, -6.0);
    const auto levelDb = [&](DistortionType type, DistortionType partner, bool changeOnce) {
        const double startDrive = changeOnce ? 0.25 : 0.75;
        ts::Rig rig(kBlockSr);
        rig.apply({{Disrumpo::kBandCountId, 0.0},
                   {makeBandParamId(0, BandParamType::kBandActiveNodes), 1.0 / 3.0},
                   {makeBandParamId(0, BandParamType::kBandMorphX), 0.0},
                   {makeBandParamId(0, BandParamType::kBandMorphY), 0.0},
                   {makeNodeParamId(0, 0, NodeParamType::kNodeType), typeNorm(type)},
                   {makeNodeParamId(0, 1, NodeParamType::kNodeType), typeNorm(partner)},
                   {makeNodeParamId(0, 0, NodeParamType::kNodeMix), 1.0},
                   {makeNodeParamId(0, 1, NodeParamType::kNodeMix), 1.0},
                   {makeNodeParamId(0, 0, NodeParamType::kNodeDrive), startDrive},
                   {makeNodeParamId(0, 1, NodeParamType::kNodeDrive), startDrive}});
        rig.reactivate();
        std::vector<float> outL;
        std::vector<float> outR;
        // Drive 2.5 -> 7.5 at 0.5 s: one parameter change per node, nothing after it
        rig.render(in, in, outL, outR, 512, [&](size_t blk, size_t, ts::PChanges& pc) {
            if (!changeOnce || blk != 47) return;
            pc.addChange(makeNodeParamId(0, 0, NodeParamType::kNodeDrive), 0.75);
            pc.addChange(makeNodeParamId(0, 1, NodeParamType::kNodeDrive), 0.75);
        });
        const size_t from = outL.size() * 3 / 4;
        return ts::toDb(ts::rmsOf(outL.data() + from, outL.size() - from));
    };
    for (const auto type : {DistortionType::Temporal, DistortionType::Stochastic}) {
        for (const auto partner : {type, DistortionType::SoftClip}) {
            const double changed = levelDb(type, partner, true);
            const double direct = levelDb(type, partner, false);
            std::printf("Drive change on %s (node B %s): %.2f dBFS after one change 2.5 -> 7.5, %.2f dBFS at 7.5 "
                        "from the start\n", getTypeName(type), getTypeName(partner), changed, direct);
            INFO(getTypeName(type) << " with node B " << getTypeName(partner) << ": " << changed
                 << " dBFS after the change, " << direct << " dBFS at 7.5");
            CHECK(std::fabs(changed - direct) <= 0.1);
        }
    }
}

// =============================================================================
// Band-count changes during playback (audit D-023, D-140; fix plan T2.08)
// =============================================================================
// The crossover topology switched on the sample the parameter arrived: a different
// allpass chain and stale filter/band state stepped the output (probe
// automation:bandcount2-4-1 read jump ratio 3.78 / 3.90 with one click each).
// =============================================================================

namespace {

constexpr size_t kStepBlock = 512;

double bandCountNorm(int bands) { return static_cast<double>(bands - 1) / 3.0; }

/// The audit probe's step time: `tenths` x 0.1 s, down to its block boundary.
size_t stepAt(size_t tenths) { return (static_cast<size_t>(kBlockSr * 0.1 * static_cast<double>(tenths)) / kStepBlock) * kStepBlock; }

/// Continuity around each step (test_support/disrumpo_rig.h, the audit probe's windows).
std::vector<ts::ContResult> stepContinuity(const std::vector<float>& y, const std::vector<size_t>& steps) {
    return ts::stepContinuity(y, steps, kBlockSr);
}

/// Output gain around a band-count step from `from` to `to` bands on a steady DC input at
/// global mix 0 (the dry path), relative to the steady output just before the step. The
/// float allpasses' DC gain is off 1 by about 1e-4, so the gain is read against that level.
struct DipReading {
    double minGain = 1.0;
    double reducedMs = 0.0;  ///< first to last sample whose gain is off 1 by more than 1e-3
};

DipReading bandCountDip(int from, int to) {
    constexpr float kDc = 0.5f;
    const size_t at = 24 * kStepBlock;
    ts::Rig rig(kBlockSr);
    rig.apply({{Disrumpo::kGlobalMixId, 0.0}, {Disrumpo::kBandCountId, bandCountNorm(from)}});
    rig.reactivate();
    const std::vector<float> in(static_cast<size_t>(kBlockSr / 2.0), kDc);
    std::vector<float> outL;
    std::vector<float> outR;
    rig.render(in, in, outL, outR, static_cast<Steinberg::int32>(kStepBlock), [&](size_t, size_t start, ts::PChanges& pc) {
        if (start == at) pc.addChange(Disrumpo::kBandCountId, bandCountNorm(to));
    });
    const double steady = ts::meanOf(outL.data() + at - 480, 480);
    DipReading r;
    size_t first = 0;
    size_t last = 0;
    bool any = false;
    for (size_t i = at; i < outL.size(); ++i) {
        const double g = static_cast<double>(outL[i]) / steady;
        r.minGain = std::min(r.minGain, g);
        if (std::fabs(g - 1.0) > 1e-3) {
            if (!any) first = i;
            last = i;
            any = true;
        }
    }
    r.reducedMs = any ? static_cast<double>(last - first + 1) * 1000.0 / kBlockSr : 0.0;
    return r;
}

} // anonymous namespace

namespace {

/// The default patch at `startBands` bands (no event: held there for the whole render), 220 Hz at
/// -12 dBFS x `inScale`, 1.5 s, block kStepBlock; `onBlock` makes the band-count events.
std::vector<float> bandCountRender(int startBands, double inScale,
                                   const std::function<void(ts::Rig&, size_t, ts::PChanges&)>& onBlock) {
    auto in = sineAt(static_cast<size_t>(kBlockSr * 1.5), 220.0, -12.0);
    for (auto& v : in) v = static_cast<float>(inScale * v);
    ts::Rig rig(kBlockSr);
    rig.apply({{Disrumpo::kBandCountId, bandCountNorm(startBands)}});
    rig.reactivate();
    std::vector<float> outL;
    std::vector<float> outR;
    rig.render(in, in, outL, outR, static_cast<Steinberg::int32>(kStepBlock),
               [&](size_t, size_t start, ts::PChanges& pc) { if (onBlock) onBlock(rig, start, pc); });
    return outL;
}

/// The band-count clicks re-baselined for the drive re-stage (user ruling 2026-11-10): at
/// -12 dBFS the re-staged default nodes square the sine off and the pinned 5-sigma detector
/// reads the square's own edges through the 10 ms dip. Asserted for each event: no click at
/// 5 sigma with the input 18.06 dB lower (`quiet`, the shapers at the drive this case was written
/// for), and at -12 dBFS none at the threshold calibrated on `controls` (the band counts held,
/// and the held renders through a plain 5 ms fade out and in: the designed dip with no plugin
/// event in it, which the detector flags as it flags the event), where the positive-control step
/// is still caught on the event and on every control (calibrated_clicks.h).
void checkBandCountClicks(const char* label, const std::vector<float>& event, const std::vector<float>& quiet,
                          const std::vector<std::vector<float>>& controls, const std::vector<size_t>& steps) {
    const auto quietC = stepContinuity(quiet, steps);
    const auto win = static_cast<size_t>(kBlockSr * 0.05);
    for (size_t k = 0; k < steps.size(); ++k) {
        const auto cal = ts::calibratedClicks(event, controls, steps[k], win, steps[k] + win / 5, kBlockSr);
        std::printf("%s event %zu: input -18.06 dB clicks %zu; -12 dBFS: 5 sigma event %zu, held controls %zu, "
                    "calibrated %.1f sigma event %zu, positive control (step %.4f) %zu, on the held controls %zu\n",
                    label, k + 1, quietC[k].clicksEvent, cal.eventAt5, cal.controlsAt5, static_cast<double>(cal.sigma),
                    cal.eventAtSigma, cal.step, cal.positiveAtSigma, cal.positiveOnControls);
        INFO(label << " event " << k + 1);
        CHECK(quietC[k].clicksEvent == 0u);
        CHECK(cal.sigma > 0.0f);
        CHECK(cal.eventAtSigma == 0u);
        CHECK(cal.positiveAtSigma > 0u);
        CHECK(cal.positiveOnControls > 0u);
    }
}

} // anonymous namespace

TEST_CASE("Band count 2->4->1 during playback has jump ratio <= 1.3", "[disrumpo][bandcount][D-023][D-140]") {
    const std::vector<size_t> steps{stepAt(5), stepAt(10)};
    const auto events = [&](ts::Rig&, size_t start, ts::PChanges& pc) {
        if (start == steps[0]) pc.addChange(Disrumpo::kBandCountId, bandCountNorm(4));
        if (start == steps[1]) pc.addChange(Disrumpo::kBandCountId, bandCountNorm(1));
    };
    const auto outL = bandCountRender(2, 1.0, events);
    const auto c = stepContinuity(outL, steps);
    std::printf("D-140 band count 2->4 at %.3f s: jump ratio %.2f, clicks %zu (steady %zu); 4->1 at %.3f s: jump "
                "ratio %.2f, clicks %zu (steady %zu); non-finite %zu\n",
                static_cast<double>(steps[0]) / kBlockSr, c[0].ratio, c[0].clicksEvent, c[0].clicksBase,
                static_cast<double>(steps[1]) / kBlockSr, c[1].ratio, c[1].clicksEvent, c[1].clicksBase, c[0].nonFinite);
    REQUIRE(c[0].nonFinite == 0);
    for (const auto& s : c) CHECK(s.ratio <= 1.3);
    std::vector<std::vector<float>> controls;
    for (const int held : {2, 4, 1}) controls.push_back(bandCountRender(held, 1.0, nullptr));
    // The designed transitions (D-023): a plain 5 ms fade out of the old band count and in of the new one
    const auto half = static_cast<size_t>(kBlockSr * 0.005);
    controls.push_back(ts::plainDip(controls[0], controls[1], steps[0], half));
    controls.push_back(ts::plainDip(controls[1], controls[2], steps[1], half));
    checkBandCountClicks("D-140 band count 2->4->1", outL, bandCountRender(2, 0.125, events), controls, steps);
}

TEST_CASE("Band count change loaded by setState during playback is click-free",
          "[disrumpo][bandcount][D-023][D-003]") {
    // Two current-version streams that differ from a fresh instance in the band count only.
    const auto streamWith = [](int bands) {
        ts::Rig src(kBlockSr);
        src.apply({{Disrumpo::kBandCountId, bandCountNorm(bands)}});
        return ts::getStateBytes(*src.p);
    };
    const auto fourBands = streamWith(4);
    const auto oneBand = streamWith(1);

    const std::vector<size_t> steps{stepAt(5), stepAt(10)};
    const auto events = [&](ts::Rig& rig, size_t start, ts::PChanges&) {
        // The host's setState between two blocks: the next process() applies it
        if (start == steps[0]) REQUIRE(ts::setStateBytes(*rig.p, fourBands));
        if (start == steps[1]) REQUIRE(ts::setStateBytes(*rig.p, oneBand));
    };
    const auto outL = bandCountRender(2, 1.0, events);
    const auto c = stepContinuity(outL, steps);
    std::printf("D-023 setState band count 2->4: jump ratio %.2f, clicks %zu (steady %zu); 4->1: jump ratio %.2f, "
                "clicks %zu (steady %zu); non-finite %zu\n",
                c[0].ratio, c[0].clicksEvent, c[0].clicksBase, c[1].ratio, c[1].clicksEvent, c[1].clicksBase,
                c[0].nonFinite);
    REQUIRE(c[0].nonFinite == 0);
    for (const auto& s : c) CHECK(s.ratio <= 1.3);
    std::vector<std::vector<float>> controls;
    for (const int held : {2, 4, 1}) controls.push_back(bandCountRender(held, 1.0, nullptr));
    // The designed transitions (D-023): a plain 5 ms fade out of the old band count and in of the new one
    const auto half = static_cast<size_t>(kBlockSr * 0.005);
    controls.push_back(ts::plainDip(controls[0], controls[1], steps[0], half));
    controls.push_back(ts::plainDip(controls[1], controls[2], steps[1], half));
    checkBandCountClicks("D-023 setState band count 2->4->1", outL, bandCountRender(2, 0.125, events), controls, steps);
}

TEST_CASE("Band count change dips the output for 10 ms", "[disrumpo][bandcount][D-023]") {
    const auto down = bandCountDip(4, 2);
    const auto up = bandCountDip(2, 4);
    std::printf("D-023 band-count dip (DC, mix 0): 4->2 min gain %.6f over %.3f ms; 2->4 min gain %.6f over %.3f ms\n",
                down.minGain, down.reducedMs, up.minGain, up.reducedMs);
    // 5 ms down to 0 at the old topology, switch, 5 ms up at the new one
    REQUIRE(down.minGain < 0.01);
    REQUIRE(down.minGain >= 0.0);
    REQUIRE(down.reducedMs >= 9.9);
    REQUIRE(down.reducedMs <= 10.1);
}

TEST_CASE("Band count changes during playback allocate nothing", "[disrumpo][bandcount][realtime][D-023]") {
    std::vector<uint8_t> oneBand;
    {
        ts::Rig src(kBlockSr);
        src.apply({{Disrumpo::kBandCountId, bandCountNorm(1)}});
        oneBand = ts::getStateBytes(*src.p);
    }
    constexpr auto kBlock = static_cast<Steinberg::int32>(kStepBlock);
    ts::Rig rig(kBlockSr, kBlock);
    const auto in = sineAt(kStepBlock, 220.0, -12.0);
    std::vector<float> inL(in);
    std::vector<float> inR(in);
    std::vector<float> outL(in.size());
    std::vector<float> outR(in.size());
    for (int b = 0; b < 8; ++b) rig.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), kBlock, nullptr);
    std::array<ts::PChanges, 3> to{};  // filled before the scope
    to[0].addChange(Disrumpo::kBandCountId, bandCountNorm(2));
    to[1].addChange(Disrumpo::kBandCountId, bandCountNorm(4));
    to[2].addChange(Disrumpo::kBandCountId, bandCountNorm(1));

    size_t allocations = 0;
    {
        TestHelpers::AllocationScope scope;
        for (int b = 0; b < 16; ++b) {
            rig.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), kBlock,
                             (b % 4 == 0) ? &to[static_cast<size_t>(b / 4) % 3] : nullptr);
        }
        allocations += TestHelpers::AllocationDetector::instance().getAllocationCount();
    }
    REQUIRE(ts::setStateBytes(*rig.p, oneBand));  // host thread, outside the scope
    {
        TestHelpers::AllocationScope scope;
        for (int b = 0; b < 4; ++b) rig.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), kBlock, nullptr);
        allocations += TestHelpers::AllocationDetector::instance().getAllocationCount();
    }
    REQUIRE(allocations == 0);
}

// =============================================================================
// Node type changes during playback (audit D-032, D-080, D-141; fix plan T2.13)
// =============================================================================
// A node type change switched the adapter's processor on the next sample, with the new
// processor's state left from its last use (probe automation:type-switch, SoftClip
// drive 3 -> SineFold: jump ratio 1.37). A change that made the node types identical or
// different switched the morph engine between its interpolated processor and its
// parallel adapters, idle until then, on one sample. SoftClip -> SineFold also changes
// the band's oversampling factor (2x -> 4x), which dips the band (D-034); the 2x-only
// pairs below isolate the type switch.
// =============================================================================

namespace {

struct TypeSwitchCase {
    const char* name;
    int nodes;                   ///< active nodes of band 0 (morph X 0.5)
    DistortionType node0;
    DistortionType node1;
    uint8_t changedNode;
    DistortionType to;
};

struct TypeSwitchReading {
    ts::ContResult probe;        ///< the audit probe's windows (event from the step sample)
    double ratioWithStep = 0.0;  ///< the event window widened by one sample: the step itself
    std::vector<float> out;      ///< the render (left)
};

/// What a type-switch render does: switch at stepAt(5) (the event), or hold the node types
/// before / after the switch for the whole render (no-event controls).
enum class SwitchRender : std::uint8_t { Event, HoldFrom, HoldTo };

/// 1 band, nodes at drive 3 (normalized 0.3), 220 Hz at -12 dBFS (x `inScale`), block 512: one
/// node's type steps at stepAt(5).
TypeSwitchReading nodeTypeSwitch(const TypeSwitchCase& c, SwitchRender mode = SwitchRender::Event,
                                 double inScale = 1.0) {
    auto in = sineAt(static_cast<size_t>(kBlockSr), 220.0, -12.0);
    for (auto& v : in) v = static_cast<float>(inScale * v);
    const size_t at = stepAt(5);
    const bool startTo = mode == SwitchRender::HoldTo;
    const DistortionType node0 = startTo && c.changedNode == 0 ? c.to : c.node0;
    const DistortionType node1 = startTo && c.changedNode == 1 ? c.to : c.node1;
    ts::Rig rig(kBlockSr);
    rig.apply({{Disrumpo::kBandCountId, bandCountNorm(1)},
               {makeBandParamId(0, BandParamType::kBandActiveNodes), static_cast<double>(c.nodes - 1) / 3.0},
               {makeBandParamId(0, BandParamType::kBandMorphX), 0.5},
               {makeNodeParamId(0, 0, NodeParamType::kNodeDrive), 0.3},
               {makeNodeParamId(0, 1, NodeParamType::kNodeDrive), 0.3},
               {makeNodeParamId(0, 0, NodeParamType::kNodeType), typeNorm(node0)},
               {makeNodeParamId(0, 1, NodeParamType::kNodeType), typeNorm(node1)}});
    rig.reactivate();
    std::vector<float> outL;
    std::vector<float> outR;
    const auto typeId = makeNodeParamId(0, c.changedNode, NodeParamType::kNodeType);
    rig.render(in, in, outL, outR, static_cast<Steinberg::int32>(kStepBlock), [&](size_t, size_t start, ts::PChanges& pc) {
        if (start == at && mode == SwitchRender::Event) pc.addChange(typeId, typeNorm(c.to));
    });
    TypeSwitchReading r;
    r.probe = stepContinuity(outL, {at})[0];
    const size_t evEnd = at + static_cast<size_t>(kBlockSr * 0.05);
    const size_t preBeg = static_cast<size_t>(kBlockSr * 0.1);
    const size_t postBeg = at + static_cast<size_t>(kBlockSr * 0.2);
    r.ratioWithStep = ts::continuityAround(outL, at - 1, evEnd, preBeg, at, postBeg, outL.size(), kBlockSr).ratio;
    r.out = std::move(outL);
    return r;
}

} // anonymous namespace

TEST_CASE("Node type switch SoftClip->SineFold jump ratio <= 1.3", "[disrumpo][type-switch][D-141][D-032]") {
    using DT = DistortionType;
    const std::array<TypeSwitchCase, 5> cases{{
        {.name = "1 node SoftClip -> SineFold (audit probe)",
         .nodes = 1,
         .node0 = DT::SoftClip,
         .node1 = DT::SoftClip,
         .changedNode = 0,
         .to = DT::SineFold},
        {.name = "1 node SoftClip -> Tube",
         .nodes = 1,
         .node0 = DT::SoftClip,
         .node1 = DT::SoftClip,
         .changedNode = 0,
         .to = DT::Tube},
        {.name = "2 nodes SoftClip/SoftClip, node 1 -> Tube (interpolated -> parallel)",
         .nodes = 2,
         .node0 = DT::SoftClip,
         .node1 = DT::SoftClip,
         .changedNode = 1,
         .to = DT::Tube},
        {.name = "2 nodes SoftClip/Tube, node 1 -> SoftClip (parallel -> interpolated)",
         .nodes = 2,
         .node0 = DT::SoftClip,
         .node1 = DT::Tube,
         .changedNode = 1,
         .to = DT::SoftClip},
        {.name = "2 nodes SoftClip/Tube, node 1 -> Temporal (parallel)",
         .nodes = 2,
         .node0 = DT::SoftClip,
         .node1 = DT::Tube,
         .changedNode = 1,
         .to = DT::Temporal},
    }};
    // Re-baselined for the drive re-stage (user ruling 2026-11-10): at -12 dBFS and drive 3 the
    // re-staged nodes square the sine off and the pinned 5-sigma detector reads the square's own
    // edges. Asserted per case: the jump ratio as before; no click at 5 sigma with the input
    // 18.06 dB lower (the shapers then see the drive this case was written for); and at -12 dBFS
    // no click at the threshold calibrated on the node types held before and after the switch,
    // where the positive-control step, sized per waveform from the held renders' own detector
    // threshold (round 4: SineFold's spread edges hid a 1.5 x largest-edge step even on its held
    // render), is still caught on the switch render and on each held render (calibrated_clicks.h).
    const size_t at = stepAt(5);
    const auto win = static_cast<size_t>(kBlockSr * 0.05);
    for (const auto& c : cases) {
        const auto r = nodeTypeSwitch(c);
        const auto quiet = nodeTypeSwitch(c, SwitchRender::Event, 0.125);
        auto heldFrom = nodeTypeSwitch(c, SwitchRender::HoldFrom).out;
        auto heldTo = nodeTypeSwitch(c, SwitchRender::HoldTo).out;
        // The designed transition: D-141's 5 ms type fade, or the 10 ms factor crossfade when
        // the switch changes the band's oversampling factor (D-034), as a plain crossfade
        const auto fromFactor = getRecommendedOversample(c.changedNode == 0 ? c.node0 : c.node1);
        const size_t fade = static_cast<size_t>(kBlockSr * (getRecommendedOversample(c.to) != fromFactor ? 0.010 : 0.005));
        auto designed = ts::plainCrossfade(heldFrom, heldTo, at, fade);
        const std::vector<std::vector<float>> controls{std::move(heldFrom), std::move(heldTo), std::move(designed)};
        const auto cal = ts::calibratedClicks(r.out, controls, at, win, at + win / 5, kBlockSr);
        std::printf("D-141 %s: jump ratio %.2f (with the step sample %.2f), clicks %zu (steady %zu), non-finite %zu; "
                    "input -18.06 dB: clicks %zu; -12 dBFS calibrated: 5 sigma switch %zu, held controls %zu, %.1f "
                    "sigma switch %zu, positive control (step %.4f = 1.5 x max(largest edge %.4f, held threshold "
                    "%.4f)) %zu, on the held controls %zu\n",
                    c.name, r.probe.ratio, r.ratioWithStep, r.probe.clicksEvent, r.probe.clicksBase,
                    r.probe.nonFinite, quiet.probe.clicksEvent, cal.eventAt5, cal.controlsAt5,
                    static_cast<double>(cal.sigma), cal.eventAtSigma, cal.step, cal.largestStep, cal.heldThreshold,
                    cal.positiveAtSigma, cal.positiveOnControls);
        INFO(c.name);
        REQUIRE(r.probe.nonFinite == 0);
        CHECK(r.ratioWithStep <= 1.3);
        CHECK(quiet.probe.clicksEvent == 0);
        CHECK(cal.sigma > 0.0f);
        CHECK(cal.eventAtSigma == 0u);
        CHECK(cal.positiveAtSigma > 0u);
        CHECK(cal.positiveOnControls > 0u);
    }

    // Measured, not gated: Spectral raises the band's latency from 0 to 2047 samples, so
    // its output emerges after a gap (follow-up 42)
    const auto spectral = nodeTypeSwitch(
        {.name = "", .nodes = 1, .node0 = DT::SoftClip, .node1 = DT::SoftClip, .changedNode = 0, .to = DT::Spectral});
    std::printf("D-141 1 node SoftClip -> Spectral: jump ratio %.2f (with the step sample %.2f), clicks %zu "
                "(steady %zu)\n",
                spectral.probe.ratio, spectral.ratioWithStep, spectral.probe.clicksEvent, spectral.probe.clicksBase);
    CHECK(spectral.probe.nonFinite == 0);
}

// Probe (hidden): where the 5-sigma detections of the 1-node SoftClip -> Tube switch at -12 dBFS
// sit, and how the switch render differs from a plain 10 ms crossfade of the held renders there.
TEST_CASE("Probe: type-switch detections against a plain crossfade", "[.][type-switch-probe]") {
    using DT = DistortionType;
    for (const auto& c : {TypeSwitchCase{.name = "1 node SoftClip -> Tube", .nodes = 1, .node0 = DT::SoftClip,
                                         .node1 = DT::SoftClip, .changedNode = 0, .to = DT::Tube},
                          TypeSwitchCase{.name = "2 nodes SoftClip/Tube, node 1 -> SoftClip", .nodes = 2,
                                         .node0 = DT::SoftClip, .node1 = DT::Tube, .changedNode = 1,
                                         .to = DT::SoftClip}}) {
      for (const double scale : {1.0, 0.125}) {
        std::printf("TSP input x %.3f\n", scale);
        const size_t at = stepAt(5);
        const auto ev = nodeTypeSwitch(c, SwitchRender::Event, scale).out;
        const auto from = nodeTypeSwitch(c, SwitchRender::HoldFrom, scale).out;
        const auto to = nodeTypeSwitch(c, SwitchRender::HoldTo, scale).out;
        const auto plain = ts::plainCrossfade(from, to, at, static_cast<size_t>(kBlockSr * 0.010));
        Krate::DSP::TestUtils::ClickDetectorConfig cfg;
        cfg.sampleRate = static_cast<float>(kBlockSr);
        cfg.frameSize = 512;
        cfg.hopSize = 256;
        cfg.detectionThreshold = 5.0f;
        cfg.energyThresholdDb = -60.0f;
        Krate::DSP::TestUtils::ClickDetector det(cfg);
        det.prepare();
        const auto win = static_cast<size_t>(kBlockSr * 0.05);
        const auto hits = det.detect(ev.data() + at, win);
        std::printf("TSP %s: %zu detections\n", c.name, hits.size());
        for (const auto& h : hits) {
            const size_t i = at + h.sampleIndex;
            double maxDev = 0.0;
            for (size_t k = i - 8; k < i + 8; ++k) maxDev = std::max(maxDev, std::fabs(static_cast<double>(ev[k] - plain[k])));
            std::printf("TSP   at +%.2f ms: event step %.4f, plain step %.4f, from step %.4f, to step %.4f, "
                        "max |event - plain| within 8 samples %.4f, event %.4f plain %.4f\n",
                        1000.0 * static_cast<double>(h.sampleIndex) / kBlockSr, static_cast<double>(ev[i] - ev[i - 1]),
                        static_cast<double>(plain[i] - plain[i - 1]), static_cast<double>(from[i] - from[i - 1]),
                        static_cast<double>(to[i] - to[i - 1]), maxDev, static_cast<double>(ev[i]),
                        static_cast<double>(plain[i]));
        }
        double rmsDiff = 0.0;
        double rms = 0.0;
        for (size_t k = at; k < at + win; ++k) {
            rmsDiff += static_cast<double>(ev[k] - plain[k]) * (ev[k] - plain[k]);
            rms += static_cast<double>(plain[k]) * plain[k];
        }
        std::printf("TSP   event - plain over the 50 ms: %.1f dB re plain\n", 10.0 * std::log10(rmsDiff / rms + 1e-30));
        for (size_t ms = 0; ms < 50; ms += 2) {
            const size_t s0 = at + static_cast<size_t>(kBlockSr * 0.001 * static_cast<double>(ms));
            const size_t n = static_cast<size_t>(kBlockSr * 0.002);
            std::printf("TSP   %2zu ms: event %.2f dB, plain %.2f dB, from %.2f, to %.2f\n", ms,
                        ts::toDb(ts::rmsOf(ev.data() + s0, n)), ts::toDb(ts::rmsOf(plain.data() + s0, n)),
                        ts::toDb(ts::rmsOf(from.data() + s0, n)), ts::toDb(ts::rmsOf(to.data() + s0, n)));
        }
      }
    }
}

// A type switch must not dip the output (user ruling 2026-11-11, round 3 ruling 3). Before the
// fix a switch between types of different oversampling factors changed the reported latency and
// went through the processor's latency dip (5 ms out, 5 ms in: SoftClip -> Tube -17 dB at 4 ms),
// and a switch between the morph engine's parallel and interpolated paths faded from the frozen
// last output (-4 dB in the first 2 ms). Asserted: no 2 ms window of the switch render more than
// 2 dB below the same window of the plain crossfade of the renders held before and after it
// (probe [type-switch-cause-probe]: with the latency pinned the switch reads within 0.6 dB).
TEST_CASE("Node type switch does not dip the output", "[disrumpo][type-switch][D-141][D-034]") {
    using DT = DistortionType;
    const std::array<TypeSwitchCase, 5> cases{{
        {.name = "1 node SoftClip -> Tube (4x -> 2x)", .nodes = 1, .node0 = DT::SoftClip, .node1 = DT::SoftClip,
         .changedNode = 0, .to = DT::Tube},
        {.name = "1 node Tube -> HardClip (2x -> 4x)", .nodes = 1, .node0 = DT::Tube, .node1 = DT::Tube,
         .changedNode = 0, .to = DT::HardClip},
        {.name = "1 node SoftClip -> SineFold (4x -> 8x)", .nodes = 1, .node0 = DT::SoftClip,
         .node1 = DT::SoftClip, .changedNode = 0, .to = DT::SineFold},
        {.name = "2 nodes SoftClip/Tube, node 1 -> Temporal (4x -> 8x)", .nodes = 2, .node0 = DT::SoftClip,
         .node1 = DT::Tube, .changedNode = 1, .to = DT::Temporal},
        {.name = "2 nodes SoftClip/Tube, node 1 -> SoftClip (parallel -> interpolated)", .nodes = 2,
         .node0 = DT::SoftClip, .node1 = DT::Tube, .changedNode = 1, .to = DT::SoftClip},
    }};
    const size_t at = stepAt(5);
    for (const auto& c : cases) {
        const auto ev = nodeTypeSwitch(c).out;
        const auto from = nodeTypeSwitch(c, SwitchRender::HoldFrom).out;
        const auto to = nodeTypeSwitch(c, SwitchRender::HoldTo).out;
        const auto fromFactor = getRecommendedOversample(c.changedNode == 0 ? c.node0 : c.node1);
        const size_t fade = static_cast<size_t>(kBlockSr * (getRecommendedOversample(c.to) != fromFactor ? 0.010 : 0.005));
        const auto plain = ts::plainCrossfade(from, to, at, fade);
        double worst = 0.0;
        double worstMs = 0.0;
        for (size_t ms = 0; ms < 20; ms += 2) {
            const size_t s0 = at + static_cast<size_t>(kBlockSr * 0.001 * static_cast<double>(ms));
            const auto n = static_cast<size_t>(kBlockSr * 0.002);
            const double d = ts::toDb(ts::rmsOf(ev.data() + s0, n)) - ts::toDb(ts::rmsOf(plain.data() + s0, n));
            if (d < worst) {
                worst = d;
                worstMs = static_cast<double>(ms);
            }
        }
        std::printf("Type-switch dip %s: deepest 2 ms window %.2f dB re the plain crossfade at %.0f ms\n", c.name, worst,
                    worstMs);
        INFO(c.name << ": " << worst << " dB at " << worstMs << " ms");
        CHECK(worst >= -2.0);
    }
}

// Probe (hidden, round 3 ruling 3): what makes the type-switch dip. For each failing case: the
// reported latency held before / after; the event against the plain crossfade and against the
// processor's latency dip (plainDip: 5 ms out, 5 ms in, kBandCountFadeMs); and the same switch with
// the latency pinned by a second band that always reaches 8x (SineFold): no latency change there.
TEST_CASE("Probe: type-switch dip cause", "[.][type-switch-cause-probe]") {
    using DT = DistortionType;
    const size_t at = stepAt(5);
    const auto win = static_cast<size_t>(kBlockSr * 0.05);
    const auto renderCase = [&](const TypeSwitchCase& c, SwitchRender mode, bool pinned, int* latency) {
        auto in = sineAt(static_cast<size_t>(kBlockSr), 220.0, -12.0);
        const bool startTo = mode == SwitchRender::HoldTo;
        const DT node0 = startTo && c.changedNode == 0 ? c.to : c.node0;
        const DT node1 = startTo && c.changedNode == 1 ? c.to : c.node1;
        ts::Rig rig(kBlockSr);
        ts::PList setup{{Disrumpo::kBandCountId, bandCountNorm(pinned ? 2 : 1)},
                        {makeBandParamId(0, BandParamType::kBandActiveNodes), static_cast<double>(c.nodes - 1) / 3.0},
                        {makeBandParamId(0, BandParamType::kBandMorphX), 0.5},
                        {makeNodeParamId(0, 0, NodeParamType::kNodeDrive), 0.3},
                        {makeNodeParamId(0, 1, NodeParamType::kNodeDrive), 0.3},
                        {makeNodeParamId(0, 0, NodeParamType::kNodeType), typeNorm(node0)},
                        {makeNodeParamId(0, 1, NodeParamType::kNodeType), typeNorm(node1)}};
        if (pinned) {
            setup.emplace_back(makeBandParamId(1, BandParamType::kBandActiveNodes), 0.0);
            setup.emplace_back(makeNodeParamId(1, 0, NodeParamType::kNodeDrive), 0.3);
            setup.emplace_back(makeNodeParamId(1, 0, NodeParamType::kNodeType), typeNorm(DT::SineFold));
        }
        rig.apply(setup);
        rig.reactivate();
        std::vector<float> outL;
        std::vector<float> outR;
        const auto typeId = makeNodeParamId(0, c.changedNode, NodeParamType::kNodeType);
        int before = -1;
        rig.render(in, in, outL, outR, static_cast<Steinberg::int32>(kStepBlock), [&](size_t, size_t start, ts::PChanges& pc) {
            if (start == at) before = static_cast<int>(rig.p->getLatencySamples());
            if (start == at && mode == SwitchRender::Event) pc.addChange(typeId, typeNorm(c.to));
        });
        if (latency != nullptr) {
            latency[0] = before;
            latency[1] = static_cast<int>(rig.p->getLatencySamples());
        }
        return outL;
    };
    const auto residualDb = [&](const std::vector<float>& a, const std::vector<float>& b, size_t from, size_t n) {
        double d = 0.0;
        double r = 0.0;
        for (size_t k = from; k < from + n; ++k) {
            d += static_cast<double>(a[k] - b[k]) * (a[k] - b[k]);
            r += static_cast<double>(b[k]) * b[k];
        }
        return 10.0 * std::log10(d / (r + 1e-30) + 1e-30);
    };
    for (const auto& c :
         {TypeSwitchCase{.name = "1 node SoftClip -> Tube", .nodes = 1, .node0 = DT::SoftClip, .node1 = DT::SoftClip,
                         .changedNode = 0, .to = DT::Tube},
          TypeSwitchCase{.name = "1 node SoftClip -> SineFold", .nodes = 1, .node0 = DT::SoftClip,
                         .node1 = DT::SoftClip, .changedNode = 0, .to = DT::SineFold},
          TypeSwitchCase{.name = "1 node Tube -> HardClip", .nodes = 1, .node0 = DT::Tube, .node1 = DT::Tube,
                         .changedNode = 0, .to = DT::HardClip},
          TypeSwitchCase{.name = "2 nodes SoftClip/Tube, node 1 -> SoftClip", .nodes = 2, .node0 = DT::SoftClip,
                         .node1 = DT::Tube, .changedNode = 1, .to = DT::SoftClip},
          TypeSwitchCase{.name = "2 nodes SoftClip/Tube, node 1 -> Temporal", .nodes = 2, .node0 = DT::SoftClip,
                         .node1 = DT::Tube, .changedNode = 1, .to = DT::Temporal}}) {
        for (const bool pinned : {false, true}) {
            int latEv[2] = {0, 0};
            int latFrom[2] = {0, 0};
            int latTo[2] = {0, 0};
            const auto ev = renderCase(c, SwitchRender::Event, pinned, latEv);
            const auto from = renderCase(c, SwitchRender::HoldFrom, pinned, latFrom);
            const auto to = renderCase(c, SwitchRender::HoldTo, pinned, latTo);
            const auto fromFactor = getRecommendedOversample(c.changedNode == 0 ? c.node0 : c.node1);
            const size_t fade = static_cast<size_t>(kBlockSr * (getRecommendedOversample(c.to) != fromFactor ? 0.010 : 0.005));
            const auto plain = ts::plainCrossfade(from, to, at, fade);
            const auto dip = ts::plainDip(from, to, at, static_cast<size_t>(kBlockSr * 0.005));
            std::vector<float> frozen(ev);
            const double origin = ev[at - 1];
            const auto frozenLen = static_cast<size_t>(kBlockSr * 0.005);
            for (size_t k = at; k < at + frozenLen; ++k) {
                const double g = static_cast<double>(k - at) / static_cast<double>(frozenLen);
                frozen[k] = static_cast<float>(origin + g * (to[k] - origin));
            }
            for (size_t k = at + frozenLen; k < frozen.size(); ++k) frozen[k] = to[k];
            // The lane crossfade over `fade` with the outgoing lane's node adapter fading from its
            // frozen last output to the new type over 5 ms (D-141), the incoming lane at the new type
            std::vector<float> laneFrozen(ev);
            for (size_t k = at; k < frozen.size(); ++k) {
                const double gl = k >= at + fade ? 1.0 : static_cast<double>(k - at) / static_cast<double>(fade);
                laneFrozen[k] = static_cast<float>((1.0 - gl) * frozen[k] + gl * to[k]);
            }
            std::printf("TSC %s%s: residual over 12 ms re lane crossfade + frozen 5 ms type fade model %.1f dB\n", c.name,
                        pinned ? " [latency pinned by an 8x band]" : "",
                        residualDb(ev, laneFrozen, at, static_cast<size_t>(kBlockSr * 0.012)));
            const auto cal = ts::calibratedClicks(ev, {from, to, plain}, at, win, at + win / 5, kBlockSr);
            // The positive control's step, injected the same way into each render instead of the event
            for (const auto& [label, render] : {std::pair<const char*, const std::vector<float>*>{"event", &ev},
                                                {"held from", &from}, {"held to", &to}, {"plain", &plain}}) {
                const auto self = ts::calibratedClicks(*render, {from, to, plain}, at, win, at + win / 5, kBlockSr);
                std::printf("TSC   positive control on %s: step %.4f at sigma %.1f -> %zu\n", label, self.step,
                            static_cast<double>(self.sigma), self.positiveAtSigma);
            }
            std::printf("TSC %s%s: latency event %d -> %d, held from %d, held to %d; residual over 12 ms re model: "
                        "plain crossfade %.1f dB, latency dip %.1f dB, frozen-origin 5 ms fade %.1f dB; calibrated "
                        "sigma %.1f event %zu positive %zu\n",
                        c.name, pinned ? " [latency pinned by an 8x band]" : "", latEv[0], latEv[1], latFrom[1],
                        latTo[1], residualDb(ev, plain, at, static_cast<size_t>(kBlockSr * 0.012)),
                        residualDb(ev, dip, at, static_cast<size_t>(kBlockSr * 0.012)),
                        residualDb(ev, frozen, at, static_cast<size_t>(kBlockSr * 0.012)),
                        static_cast<double>(cal.sigma), cal.eventAtSigma, cal.positiveAtSigma);
            for (size_t ms = 0; ms < 14; ms += 2) {
                const size_t s0 = at + static_cast<size_t>(kBlockSr * 0.001 * static_cast<double>(ms));
                const auto n = static_cast<size_t>(kBlockSr * 0.002);
                std::printf("TSC   %2zu ms: event %.2f dB, plain %.2f, latency-dip model %.2f, frozen model %.2f\n", ms,
                            ts::toDb(ts::rmsOf(ev.data() + s0, n)), ts::toDb(ts::rmsOf(plain.data() + s0, n)),
                            ts::toDb(ts::rmsOf(dip.data() + s0, n)), ts::toDb(ts::rmsOf(frozen.data() + s0, n)));
            }
        }
    }
}

TEST_CASE("Node type switches during playback allocate nothing", "[disrumpo][type-switch][realtime][D-032]") {
    constexpr auto kBlock = static_cast<Steinberg::int32>(kStepBlock);
    ts::Rig rig(kBlockSr, kBlock);
    const auto in = sineAt(kStepBlock, 220.0, -12.0);
    std::vector<float> inL(in);
    std::vector<float> inR(in);
    std::vector<float> outL(in.size());
    std::vector<float> outR(in.size());
    for (int b = 0; b < 8; ++b) rig.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), kBlock, nullptr);
    // Every type in turn on node 0 of band 0, then back to SoftClip (filled before the scope)
    constexpr int kTypes = static_cast<int>(DistortionType::COUNT);
    std::vector<ts::PChanges> to(static_cast<size_t>(kTypes) + 1);
    for (int t = 0; t <= kTypes; ++t) {
        to[static_cast<size_t>(t)].addChange(makeNodeParamId(0, 0, NodeParamType::kNodeType),
                                             typeNorm(static_cast<DistortionType>(t % kTypes)));
    }
    // Audit probe "audio-thread allocations" (T6.09, D-002): node 0 to Spectral, then its
    // FFT-size slot (shape slot 1: 512 / 1024 / 2048 / 4096) through every size and back
    const ts::ParamID fftSlot = makeNodeParamId(0, 0, NodeParamType::kNodeShape1);
    to.emplace_back().addChange(makeNodeParamId(0, 0, NodeParamType::kNodeType), typeNorm(DistortionType::Spectral));
    for (const double slot : {1.0, 0.0, 1.0 / 3.0, 2.0 / 3.0, 1.0}) to.emplace_back().addChange(fftSlot, slot);

    size_t allocations = 0;
    {
        TestHelpers::AllocationScope scope;
        for (auto& changes : to) {
            rig.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), kBlock, &changes);
            rig.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), kBlock, nullptr);
        }
        allocations = TestHelpers::AllocationDetector::instance().getAllocationCount();
    }
    std::printf("D-032 / D-002 %d node type switches and 5 Spectral FFT-size changes in process(): %zu allocations\n",
                kTypes + 2, allocations);
    REQUIRE(allocations == 0);
}

// =============================================================================
// Global gain and mix (audit D-025; fix plan T2.09)
// =============================================================================
// Input gain, output gain and global mix were block constants (a 48 dB step landed
// on one sample), and output gain scaled only the wet sum (no effect at mix 0).
// =============================================================================

namespace {

constexpr double kGainNormPerDb = 1.0 / 48.0;  // -24..+24 dB over [0, 1]

double gainNorm(double db) { return (db + 24.0) * kGainNormPerDb; }

/// One band, every node at drive 0 (adapter passthrough), Oversample Max 1x: the wet
/// sum is the input times the input gain, so the output is plain gain and mix.
ts::PList neutralOneBand() {
    ts::PList l{{Disrumpo::kBandCountId, 0.0}, {Disrumpo::kOversampleMaxId, 0.0}};
    for (uint8_t n = 0; n < 4; ++n) l.emplace_back(makeNodeParamId(0, n, NodeParamType::kNodeDrive), 0.0);
    return l;
}

/// Time for a step of Output Gain from 0 dB to +6 dB to cover 99 % of its change
/// (DC at mix 0: the 1-band dry path is the input itself).
double outputGainSettleMs(double sr) {
    constexpr float kDc = 0.25f;
    constexpr Steinberg::int32 kBlock = 256;
    ts::Rig rig(sr, kBlock);
    auto l = neutralOneBand();
    l.emplace_back(Disrumpo::kGlobalMixId, 0.0);
    l.emplace_back(Disrumpo::kOutputGainId, gainNorm(0.0));
    rig.apply(l);
    rig.reactivate();
    const size_t at = size_t{8} * static_cast<size_t>(kBlock);
    const std::vector<float> in(static_cast<size_t>(sr / 5.0), kDc);
    std::vector<float> outL;
    std::vector<float> outR;
    rig.render(in, in, outL, outR, kBlock, [&](size_t, size_t start, ts::PChanges& pc) {
        if (start == at) pc.addChange(Disrumpo::kOutputGainId, gainNorm(6.0));
    });
    const double before = static_cast<double>(outL[at - 1]);
    const double after = before * std::pow(10.0, 6.0 / 20.0);
    for (size_t i = at; i < outL.size(); ++i) {
        if (static_cast<double>(outL[i]) >= before + 0.99 * (after - before)) {
            return static_cast<double>(i - at + 1) * 1000.0 / sr;
        }
    }
    return 1e9;
}

} // anonymous namespace

TEST_CASE("Output gain step of 48 dB in one block produces no sample jump above the sine slope bound",
          "[disrumpo][gain][D-025]") {
    constexpr double kFreq = 1000.0;
    constexpr double kInDb = -12.0;
    constexpr size_t kStepAt = size_t{10} * 512;  // the 1 kHz sine is at -0.87 of its peak there
    ts::Rig rig(kBlockSr);
    auto l = neutralOneBand();
    l.emplace_back(Disrumpo::kGlobalMixId, 1.0);
    l.emplace_back(Disrumpo::kOutputGainId, 0.0);  // -24 dB
    rig.apply(l);
    rig.reactivate();
    const auto in = sineAt(static_cast<size_t>(kBlockSr / 4.0), kFreq, kInDb);
    std::vector<float> outL;
    std::vector<float> outR;
    rig.render(in, in, outL, outR, 512, [&](size_t, size_t start, ts::PChanges& pc) {
        if (start == kStepAt) pc.addChange(Disrumpo::kOutputGainId, 1.0);  // +24 dB
    });
    // The steepest a sine at the final (+24 dB) level ever moves in one sample
    const double bound = std::pow(10.0, (kInDb + 24.0) / 20.0) * 2.0 * std::numbers::pi * kFreq / kBlockSr;
    const double jump = ts::maxJump(outL.data() + kStepAt - 1, outL.size() - kStepAt + 1);
    std::printf("D-025 output gain -24 -> +24 dB in one block: max step %.4f, sine slope bound %.4f (ratio %.2f)\n",
                jump, bound, jump / bound);
    REQUIRE(jump <= bound);
}

TEST_CASE("Output gain +6 dB raises output 6 dB at mix 0", "[disrumpo][gain][D-025]") {
    const auto in = sineAt(static_cast<size_t>(kBlockSr / 2.0), 1000.0, -12.0);
    const auto levelDb = [&](double outputGainDb) {
        ts::Rig rig(kBlockSr);
        rig.apply({{Disrumpo::kGlobalMixId, 0.0}, {Disrumpo::kOutputGainId, gainNorm(outputGainDb)}});
        rig.reactivate();
        std::vector<float> outL;
        std::vector<float> outR;
        rig.render(in, in, outL, outR);
        const size_t half = outL.size() / 2;
        return ts::toDb(ts::rmsOf(outL.data() + half, outL.size() - half));
    };
    const double at0 = levelDb(0.0);
    const double at6 = levelDb(6.0);
    std::printf("D-025 mix 0: output gain 0 dB -> %.3f dBFS, +6 dB -> %.3f dBFS (difference %.3f dB)\n", at0, at6,
                at6 - at0);
    REQUIRE(std::fabs((at6 - at0) - 6.0) < 0.05);
}

TEST_CASE("Global gain and mix smoothing settles in 15 ms and starts at its target",
          "[disrumpo][gain][D-025]") {
    SECTION("time to 99 % of an output gain step") {
        const double ms44 = outputGainSettleMs(44100.0);
        const double ms96 = outputGainSettleMs(96000.0);
        std::printf("D-025 output gain 0 -> +6 dB, time to 99 %%: %.3f ms at 44.1 kHz, %.3f ms at 96 kHz\n", ms44, ms96);
        REQUIRE(ms44 > 5.0);
        REQUIRE(ms44 <= 15.0);
        REQUIRE(ms96 > 5.0);
        REQUIRE(ms96 <= 15.0);
    }
    SECTION("first block after activation is at the steady level") {
        constexpr Steinberg::int32 kBlock = 480;  // 10 cycles of 1 kHz at 48 kHz
        ts::Rig rig(kBlockSr, kBlock);
        auto l = neutralOneBand();
        l.emplace_back(Disrumpo::kInputGainId, gainNorm(-6.0));
        l.emplace_back(Disrumpo::kOutputGainId, gainNorm(6.0));
        l.emplace_back(Disrumpo::kGlobalMixId, 0.3);
        rig.apply(l);
        rig.reactivate();
        const auto in = sineAt(static_cast<size_t>(kBlock) * 20, 1000.0, -12.0);
        std::vector<float> outL;
        std::vector<float> outR;
        rig.render(in, in, outL, outR, kBlock);
        const double firstDb = ts::toDb(ts::rmsOf(outL.data(), static_cast<size_t>(kBlock)));
        const double steadyDb = ts::toDb(ts::rmsOf(outL.data() + size_t{19} * static_cast<size_t>(kBlock), static_cast<size_t>(kBlock)));
        std::printf("D-025 first block after setActive %.3f dBFS, steady %.3f dBFS (difference %.3f dB)\n", firstDb,
                    steadyDb, firstDb - steadyDb);
        REQUIRE(std::fabs(firstDb - steadyDb) < 0.1);
    }
}

// Measurement probe (hidden): real-time ratio of Processor::process, 4 bands of HardClip.
TEST_CASE("Probe: Processor render cost with 4 HardClip bands", "[.][t107-cpu-probe]") {
    const auto in = sineAt(static_cast<size_t>(kBlockSr * 10.0), 220.0, -12.0);
    ts::Rig rig(kBlockSr);
    rig.apply(hardClipBands(1.0, 4, false));
    rig.reactivate();
    std::vector<float> outL;
    std::vector<float> outR;
    const auto t0 = std::chrono::steady_clock::now();
    rig.render(in, in, outL, outR);
    const auto t1 = std::chrono::steady_clock::now();
    const double secs = std::chrono::duration<double>(t1 - t0).count();
    WARN("PROBE cpu: " << secs << " s for 10 s of audio = " << (secs / 10.0 * 100.0) << " % of real time");
}
