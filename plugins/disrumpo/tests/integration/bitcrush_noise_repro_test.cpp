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

#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

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
    double r0 = 0.0, r1 = 0.0;
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
        if (index < 0 || index >= static_cast<Steinberg::int32>(queues_.size())) return nullptr;
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
        std::array<float, kBlockSize> inL{}, inR{}, outL{}, outR{};
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
          "[integration][bitcrush][processor][diag]") {
    using namespace Disrumpo;

    ProcessorFixture fixture;
    Steinberg::int32 sampleOffset = 0;

    auto processBlocks = [&](int numBlocks,
                             Steinberg::Vst::IParameterChanges* params = nullptr,
                             float* captureL = nullptr) {
        std::array<float, kBlockSize> inL{}, inR{}, outL{}, outR{};
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

    // Capture with NO params (default SoftClip)
    std::array<float, kBlockSize> defaultL{};
    processBlocks(32, nullptr, defaultL.data());
    float rmsDefault = calculateRMS(defaultL.data(), kBlockSize);
    float acDefault = calculateAutocorrelation(defaultL.data(), kBlockSize);
    WARN("Default (SoftClip): RMS=" << rmsDefault << " autocorr=" << acDefault);

    // Now set Bitcrush with extreme settings: 1 bit (slot0=0)
    sampleOffset = 0;  // restart
    ProcessorFixture fixture2;
    Steinberg::int32 sampleOffset2 = 0;

    auto processBlocks2 = [&](int numBlocks,
                              Steinberg::Vst::IParameterChanges* params = nullptr,
                              float* captureL = nullptr) {
        std::array<float, kBlockSize> inL{}, inR{}, outL{}, outR{};
        for (int block = 0; block < numBlocks; ++block) {
            generateSine(inL.data(), kBlockSize, kFreq, kSampleRate, sampleOffset2);
            generateSine(inR.data(), kBlockSize, kFreq, kSampleRate, sampleOffset2);
            std::memset(outL.data(), 0, sizeof(float) * kBlockSize);
            std::memset(outR.data(), 0, sizeof(float) * kBlockSize);
            auto* p = (block == 0) ? params : nullptr;
            fixture2.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), p);
            if (block == numBlocks - 1 && captureL)
                std::memcpy(captureL, outL.data(), sizeof(float) * kBlockSize);
            sampleOffset2 += kBlockSize;
        }
    };

    {
        SimpleParameterChanges params;
        // Set Bitcrush AND set Slot0 (Bits) to 0 = minimum
        params.addChange(
            makeNodeParamId(0, 0, NodeParamType::kNodeType),
            11.0 / 25.0);  // Bitcrush
        params.addChange(
            makeNodeParamId(0, 0, NodeParamType::kNodeDrive),
            0.5);  // Drive = 5.0
        params.addChange(
            makeNodeParamId(0, 0, NodeParamType::kNodeMix),
            1.0);
        params.addChange(
            makeNodeParamId(0, 0, NodeParamType::kNodeShape0),
            0.0);  // Bits = 1 (minimum)
        processBlocks2(32, &params);
    }

    std::array<float, kBlockSize> bitcrushL{};
    processBlocks2(1, nullptr, bitcrushL.data());
    float rmsBitcrush = calculateRMS(bitcrushL.data(), kBlockSize);
    float acBitcrush = calculateAutocorrelation(bitcrushL.data(), kBlockSize);
    float zcrBitcrush = calculateZCR(bitcrushL.data(), kBlockSize);

    WARN("Bitcrush (1-bit, drive=5): RMS=" << rmsBitcrush
         << " autocorr=" << acBitcrush << " ZCR=" << zcrBitcrush);

    // Print first 20 output samples for inspection
    std::string samples = "First 20 samples: ";
    for (int i = 0; i < 20; ++i) {
        samples += std::to_string(bitcrushL[i]) + " ";
    }
    WARN(samples);

    // At 1-bit depth with drive=5, signal should be heavily quantized
    // Verify output is not silence and has periodic structure (not white noise)
    INFO("If autocorrelation is same as default, bitcrush is not taking effect");
    REQUIRE(rmsBitcrush > 0.001f);
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
        std::array<float, kBlockSize> inL{}, inR{}, outL{}, outR{};
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
