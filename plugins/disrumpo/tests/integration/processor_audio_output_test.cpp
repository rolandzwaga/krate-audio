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
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

using Catch::Matchers::WithinAbs;

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
class SimpleParamValueQueue : public Steinberg::Vst::IParamValueQueue {
public:
    SimpleParamValueQueue(Steinberg::Vst::ParamID id, double value)
    : id_(id), value_(value) {}

    Steinberg::Vst::ParamID PLUGIN_API getParameterId() override { return id_; }
    Steinberg::int32 PLUGIN_API getPointCount() override { return 1; }
    Steinberg::tresult PLUGIN_API getPoint(Steinberg::int32 index, Steinberg::int32& sampleOffset, Steinberg::Vst::ParamValue& value) override {
        if (index != 0) return Steinberg::kResultFalse;
        sampleOffset = 0;
        value = value_;
        return Steinberg::kResultTrue;
    }
    Steinberg::tresult PLUGIN_API addPoint(Steinberg::int32 /*sampleOffset*/, Steinberg::Vst::ParamValue /*value*/, Steinberg::int32& /*index*/) override {
        return Steinberg::kResultFalse;
    }
    DECLARE_FUNKNOWN_METHODS
private:
    Steinberg::Vst::ParamID id_;
    double value_;
};

IMPLEMENT_FUNKNOWN_METHODS(SimpleParamValueQueue, Steinberg::Vst::IParamValueQueue, Steinberg::Vst::IParamValueQueue::iid)

class SimpleParameterChanges : public Steinberg::Vst::IParameterChanges {
public:
    void addChange(Steinberg::Vst::ParamID id, double value) {
        queues_.push_back(std::make_unique<SimpleParamValueQueue>(id, value));
    }

    Steinberg::int32 PLUGIN_API getParameterCount() override {
        return static_cast<Steinberg::int32>(queues_.size());
    }
    Steinberg::Vst::IParamValueQueue* PLUGIN_API getParameterData(Steinberg::int32 index) override {
        if (index < 0 || index >= static_cast<Steinberg::int32>(queues_.size())) return nullptr;
        return queues_[index].get();
    }
    Steinberg::Vst::IParamValueQueue* PLUGIN_API addParameterData(const Steinberg::Vst::ParamID& /*id*/, Steinberg::int32& /*index*/) override {
        return nullptr;
    }
    DECLARE_FUNKNOWN_METHODS
private:
    std::vector<std::unique_ptr<SimpleParamValueQueue>> queues_;
};

IMPLEMENT_FUNKNOWN_METHODS(SimpleParameterChanges, Steinberg::Vst::IParameterChanges, Steinberg::Vst::IParameterChanges::iid)

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

        result = processor->setActive(true);
        REQUIRE(result == Steinberg::kResultTrue);
    }

    ~ProcessorFixture() {
        processor->setActive(false);
        processor->terminate();
    }

    /// @brief Process a single block of audio and return the output RMS
    /// @param inputL Left input buffer (kBlockSize samples)
    /// @param inputR Right input buffer (kBlockSize samples)
    /// @param outputL Left output buffer (kBlockSize samples, written by processor)
    /// @param outputR Right output buffer (kBlockSize samples, written by processor)
    /// @param paramChanges Optional parameter changes to inject
    void processBlock(float* inputL, float* inputR, float* outputL, float* outputR,
                      Steinberg::Vst::IParameterChanges* paramChanges = nullptr) {
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
