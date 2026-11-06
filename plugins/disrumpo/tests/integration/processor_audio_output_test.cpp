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
#include <cstring>
#include <memory>
#include <vector>
#include "vst_param_changes.h"

#include "../test_support/disrumpo_rig.h"
#include "dsp/crossover_network.h"
#include "test_helpers/spectral_analysis.h"

#include <allocation_detector.h>

#include <algorithm>
#include <chrono>
#include <numbers>

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
    ProcessorFixture fixture;

    std::array<float, kBlockSize> inputL{};
    std::array<float, kBlockSize> inputR{};
    std::array<float, kBlockSize> outputL{};
    std::array<float, kBlockSize> outputR{};

    float lastOutputRmsL = 0.0f;
    float lastOutputRmsR = 0.0f;

    // Process multiple blocks to let filters settle
    for (Steinberg::int32 block = 0; block < kNumBlocks; ++block) {
        // Generate 1kHz sine on both channels
        generateSine(inputL.data(), kBlockSize, 1000.0f, kSampleRate, block * kBlockSize);
        generateSine(inputR.data(), kBlockSize, 1000.0f, kSampleRate, block * kBlockSize);

        // Clear output
        std::memset(outputL.data(), 0, sizeof(float) * kBlockSize);
        std::memset(outputR.data(), 0, sizeof(float) * kBlockSize);

        fixture.processBlock(inputL.data(), inputR.data(), outputL.data(), outputR.data());

        lastOutputRmsL = calculateRMS(outputL.data(), kBlockSize);
        lastOutputRmsR = calculateRMS(outputR.data(), kBlockSize);
    }

    INFO("Final block output L RMS: " << lastOutputRmsL);
    INFO("Final block output R RMS: " << lastOutputRmsR);

    // After settling, output MUST be non-zero
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

struct AliasReading {
    double aliasedDb = 0.0;      // power in the predicted fold-back bins (harmonics 2..200)
    double nonHarmonicDb = 0.0;  // power in every bin outside the harmonic main lobes
    double fundamentalDb = 0.0;
};

/// Power-averaged over the last kAliasFrames Hann-windowed 8192-sample frames.
AliasReading readAliasing(const std::vector<float>& x) {
    using namespace Krate::DSP::TestUtils;
    AliasingTestConfig cfg;
    cfg.testFrequencyHz = kAliasToneHz;
    cfg.sampleRate = static_cast<float>(kBlockSr);
    cfg.fftSize = kAliasFft;
    cfg.maxHarmonic = 200;
    auto aliased = getAliasedBins(cfg);
    std::sort(aliased.begin(), aliased.end());
    aliased.erase(std::unique(aliased.begin(), aliased.end()), aliased.end());
    const size_t fundBin = frequencyToBin(kAliasToneHz, cfg.sampleRate, kAliasFft);

    Krate::DSP::FFT fft;
    fft.prepare(kAliasFft);
    std::vector<float> window(kAliasFft);
    Krate::DSP::Window::generateHann(window.data(), kAliasFft);
    std::vector<float> frame(kAliasFft);
    std::vector<Krate::DSP::Complex> spectrum(fft.numBins());

    double aliasPower = 0.0;
    double nonHarmonicPower = 0.0;
    double fundamentalPower = 0.0;
    const size_t start = x.size() - kAliasFrames * kAliasFft;
    for (size_t f = 0; f < kAliasFrames; ++f) {
        for (size_t i = 0; i < kAliasFft; ++i) {
            frame[i] = x[start + f * kAliasFft + i] * window[i];
        }
        fft.forward(frame.data(), spectrum.data());
        const double a = detail::sumBinPower(spectrum.data(), aliased);
        aliasPower += a * a;
        const double m = spectrum[fundBin].magnitude();
        fundamentalPower += m * m;
        for (size_t b = 3; b < spectrum.size(); ++b) {
            const size_t r = b % fundBin;
            if (r <= 2 || r >= fundBin - 2) continue;  // harmonic main lobe (Hann: +-1 bin)
            const double mb = spectrum[b].magnitude();
            nonHarmonicPower += mb * mb;
        }
    }
    const auto db = [](double p) {
        return 10.0 * std::log10(p / static_cast<double>(kAliasFrames) + 1e-30);
    };
    return {.aliasedDb = db(aliasPower), .nonHarmonicDb = db(nonHarmonicPower), .fundamentalDb = db(fundamentalPower)};
}

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
    const auto r1 = readAliasing(out1x);
    const auto r4 = readAliasing(out4x);

    INFO("factor at Oversample Max 1x: " << factor1x << ", at 4x: " << factor4x);
    INFO("fundamental 1x " << r1.fundamentalDb << " dB, 4x " << r4.fundamentalDb << " dB");
    INFO("aliased bins 1x " << r1.aliasedDb << " dB, 4x " << r4.aliasedDb << " dB, reduction "
         << (r1.aliasedDb - r4.aliasedDb) << " dB");
    INFO("non-harmonic 1x " << r1.nonHarmonicDb << " dB, 4x " << r4.nonHarmonicDb << " dB, reduction "
         << (r1.nonHarmonicDb - r4.nonHarmonicDb) << " dB");
    REQUIRE(factor1x == 1);
    REQUIRE(factor4x == 4);
    REQUIRE(r1.aliasedDb - r4.aliasedDb >= 12.0);
    // D-009 (fix plan T1.06): the tone filter runs at the rate it is clocked, so the
    // 1 kHz fundamental reads the same at 1x and 4x (it read 56.97 vs 59.83 dB while
    // the adapters were prepared for 8x the host rate).
    REQUIRE(std::fabs(r1.fundamentalDb - r4.fundamentalDb) < 1.0);
}

TEST_CASE("Band bypass passes the crossover band unchanged", "[disrumpo][bypass][D-095]") {
    const auto in = sineAt(static_cast<size_t>(kBlockSr / 2.0), 220.0, -12.0);
    std::vector<float> outL;
    std::vector<float> outR;

    SECTION("4 bands, every band bypassed") {
        ts::Rig rig(kBlockSr);
        rig.apply(hardClipBands(1.0, 4, true));
        rig.reactivate();
        rig.render(in, in, outL, outR);
        const auto expected = crossoverSum(in, 4);
        const double diffL = maxAbsDiff(outL, expected);
        const double diffR = maxAbsDiff(outR, expected);
        INFO("max |out - crossover sum|: L " << diffL << ", R " << diffR);
        REQUIRE(diffL < 1e-5);
        REQUIRE(diffR < 1e-5);
    }

    SECTION("1 band bypassed passes the input through") {
        ts::Rig rig(kBlockSr);
        rig.apply(hardClipBands(0.0, 1, true));
        rig.reactivate();
        rig.render(in, in, outL, outR);
        const double diffL = maxAbsDiff(outL, in);
        const double diffR = maxAbsDiff(outR, in);
        INFO("max |out - in|: L " << diffL << ", R " << diffR);
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

    // While bypassed (one block after the toggle onward) the output is the clean crossover sum
    const auto clean = crossoverSum(in, 4);
    const size_t on = kOnBlock * kBlock;
    const size_t off = kOffBlock * kBlock;
    const double bypassedDiff = maxAbsDiff(outL, clean, on, off);
    INFO("max |out - crossover sum| while bypassed: " << bypassedDiff);
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

/// One band, node 0 SoftClip at drive 0.1 (linear region), mix 1, cursor on node 0.
/// Returns the RMS of the last 0.25 s of a 0.5 s sine at -30 dBFS.
double softClipToneRms(double toneNorm, double freqHz, double oversampleMaxNorm, int& factor) {
    ts::Rig rig(kBlockSr);
    rig.apply({{Disrumpo::kBandCountId, 0.0},
               {makeNodeParamId(0, 0, NodeParamType::kNodeType), typeNorm(DistortionType::SoftClip)},
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
        const double ref8k = softClipToneRms(kTone8k, kRefHz, oversampleMaxNorm, factor);
        const double at4k = softClipToneRms(kTone8k, 4000.0, oversampleMaxNorm, factor);
        const double ref4k = softClipToneRms(kTone4k, kRefHz, oversampleMaxNorm, factor);
        const double at1k = softClipToneRms(kTone4k, 1000.0, oversampleMaxNorm, factor);
        const double db8k = ts::toDb(at4k / ref8k);
        const double db4k = ts::toDb(at1k / ref4k);
        INFO("running factor " << factor << ": tone 8 kHz at 4 kHz " << db8k
             << " dB, tone 4 kHz at 1 kHz " << db4k << " dB (re 250 Hz)");
        REQUIRE(factor == expectedFactor);
        REQUIRE(std::fabs(db8k) <= 1.0);
        REQUIRE(std::fabs(db4k - (-0.26)) <= 0.5);
    };
    SECTION("Oversample Max 1x") { check(0.0, 1); }
    SECTION("Oversample Max 4x (SoftClip runs at 2x)") { check(2.0 / 3.0, 2); }
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
