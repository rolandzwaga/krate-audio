// ==============================================================================
// Physical Model Output Level & Distortion Diagnostic Tests
// ==============================================================================
// End-to-end integration tests that measure the Innexus processor output when
// physical modelling components are active. These tests confirm that the output
// is being driven into the safety soft limiter (tanh), indicating distortion.
//
// The tests measure:
//   1. Peak amplitude and RMS across multiple blocks
//   2. How much time the output spends in the tanh saturation region (> 0.9)
//   3. Crest factor (peak / RMS) as a distortion indicator
//   4. Comparison of output levels with phys model OFF vs ON at various mixes
//   5. Individual component contributions (exciter types, resonator types,
//      body resonance, sympathetic resonance)
//
// These tests are DIAGNOSTIC — they capture the current (distorted) behavior
// so we can track improvements as we fix the gain staging.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"
#include "dsp/sample_analysis.h"

#include <krate/dsp/processors/harmonic_types.h>
#include <krate/dsp/processors/residual_types.h>

#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using Catch::Approx;

// =============================================================================
// Test Infrastructure
// =============================================================================

namespace {

static constexpr double kPMSampleRate = 44100.0;
static constexpr int32 kPMBlockSize = 512;
// Process ~0.5 seconds of audio for steady-state measurements
static constexpr int kPMSettleBlocks = 5;
static constexpr int kPMMeasureBlocks = 40;

class PMParamValueQueue : public IParamValueQueue
{
public:
    PMParamValueQueue(ParamID id, ParamValue val)
        : id_(id), value_(val) {}

    ParamID PLUGIN_API getParameterId() override { return id_; }
    int32 PLUGIN_API getPointCount() override { return 1; }
    tresult PLUGIN_API getPoint(int32, int32& sampleOffset, ParamValue& value) override
    {
        sampleOffset = 0;
        value = value_;
        return kResultTrue;
    }
    tresult PLUGIN_API addPoint(int32, ParamValue, int32&) override { return kResultFalse; }
    tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

private:
    ParamID id_;
    ParamValue value_;
};

class PMParameterChanges : public IParameterChanges
{
public:
    void addChange(ParamID id, ParamValue val)
    {
        queues_.emplace_back(id, val);
    }
    void clear() { queues_.clear(); }

    int32 PLUGIN_API getParameterCount() override
    {
        return static_cast<int32>(queues_.size());
    }
    IParamValueQueue* PLUGIN_API getParameterData(int32 index) override
    {
        if (index < 0 || index >= static_cast<int32>(queues_.size()))
            return nullptr;
        return &queues_[static_cast<size_t>(index)];
    }
    IParamValueQueue* PLUGIN_API addParameterData(const ParamID&, int32&) override
    {
        return nullptr;
    }
    tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

private:
    std::vector<PMParamValueQueue> queues_;
};

// Create analysis with residual frames for physical model excitation
Innexus::SampleAnalysis* makePMAnalysis(int numFrames = 50, float f0 = 220.0f,
                                         float amplitude = 0.5f)
{
    auto* analysis = new Innexus::SampleAnalysis();
    analysis->sampleRate = 44100.0f;
    analysis->hopTimeSec = 512.0f / 44100.0f;
    analysis->analysisFFTSize = 1024;
    analysis->analysisHopSize = 512;

    for (int f = 0; f < numFrames; ++f)
    {
        Krate::DSP::HarmonicFrame frame{};
        frame.f0 = f0;
        frame.f0Confidence = 0.9f;
        frame.numPartials = 8;
        frame.globalAmplitude = amplitude;

        for (int p = 0; p < 8; ++p)
        {
            auto& partial = frame.partials[static_cast<size_t>(p)];
            partial.harmonicIndex = p + 1;
            partial.frequency = f0 * static_cast<float>(p + 1);
            partial.amplitude = amplitude / static_cast<float>(p + 1);
            partial.relativeFrequency = static_cast<float>(p + 1);
            partial.inharmonicDeviation = 0.0f;
            partial.stability = 1.0f;
            partial.age = 10;
            partial.phase = 0.0f;
        }

        analysis->frames.push_back(frame);

        // Residual frames — moderate energy for exciter input
        Krate::DSP::ResidualFrame rFrame;
        rFrame.totalEnergy = 0.1f;
        rFrame.transientFlag = false;
        for (size_t b = 0; b < Krate::DSP::kResidualBands; ++b)
            rFrame.bandEnergies[b] = 0.05f;
        analysis->residualFrames.push_back(rFrame);
    }

    analysis->totalFrames = analysis->frames.size();
    analysis->filePath = "test_pm_output.wav";
    return analysis;
}

// Measurement results for a processing run
struct OutputMeasurement
{
    float peakAmplitude = 0.0f;
    float rmsLevel = 0.0f;
    float crestFactorDb = 0.0f;
    float saturationRatio = 0.0f; // fraction of samples > 0.9 (tanh saturation)
    float clippingRatio = 0.0f;   // fraction of samples > 0.99 (hard limiting)
    int totalSamples = 0;
    bool hasNaN = false;
    bool hasInf = false;

    // Pre-limiter estimate: if output = tanh(x), then x = atanh(output)
    // For samples near ±1.0, the pre-limiter signal was much larger
    float estimatedPreLimiterPeak = 0.0f;
};

struct PMFixture
{
    Innexus::Processor processor;
    std::vector<float> outL;
    std::vector<float> outR;
    float* channels[2];
    AudioBusBuffers outputBus{};
    PMParameterChanges params;

    PMFixture()
        : outL(static_cast<size_t>(kPMBlockSize), 0.0f)
        , outR(static_cast<size_t>(kPMBlockSize), 0.0f)
    {
        channels[0] = outL.data();
        channels[1] = outR.data();
        outputBus.numChannels = 2;
        outputBus.channelBuffers32 = channels;

        processor.initialize(nullptr);
        ProcessSetup setup{};
        setup.processMode = kRealtime;
        setup.symbolicSampleSize = kSample32;
        setup.maxSamplesPerBlock = kPMBlockSize;
        setup.sampleRate = kPMSampleRate;
        processor.setupProcessing(setup);
        processor.setActive(true);
    }

    ~PMFixture()
    {
        processor.setActive(false);
        processor.terminate();
    }

    void injectAnalysis()
    {
        processor.testInjectAnalysis(makePMAnalysis());
    }

    void noteOn(int pitch = 60, float velocity = 0.8f)
    {
        processor.onNoteOn(pitch, velocity);
    }

    void noteOff(int pitch = 60)
    {
        processor.onNoteOff(pitch, 0.0f);
    }

    void applyParams()
    {
        processBlock(&params);
        params.clear();
    }

    float processBlock(PMParameterChanges* p = nullptr)
    {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);

        ProcessData data{};
        data.numSamples = kPMBlockSize;
        data.numOutputs = 1;
        data.outputs = &outputBus;
        data.inputParameterChanges = p;
        processor.process(data);

        float maxAmp = 0.0f;
        for (size_t s = 0; s < outL.size(); ++s)
            maxAmp = std::max(maxAmp, std::max(std::abs(outL[s]), std::abs(outR[s])));
        return maxAmp;
    }

    // Process multiple blocks, settling first, then measuring
    OutputMeasurement measureOutput(int settleBlocks = kPMSettleBlocks,
                                     int measureBlocks = kPMMeasureBlocks)
    {
        // Settle — let transients and smoothers converge
        for (int b = 0; b < settleBlocks; ++b)
            processBlock();

        // Measure
        OutputMeasurement m{};
        double sumSquared = 0.0;
        int saturatedSamples = 0;
        int clippedSamples = 0;

        for (int b = 0; b < measureBlocks; ++b)
        {
            processBlock();

            for (size_t s = 0; s < outL.size(); ++s)
            {
                float absL = std::abs(outL[s]);
                float absR = std::abs(outR[s]);
                float maxSample = std::max(absL, absR);

                // NaN / Inf check (use bit manipulation for -ffast-math safety)
                uint32_t bitsL, bitsR;
                std::memcpy(&bitsL, &outL[s], sizeof(float));
                std::memcpy(&bitsR, &outR[s], sizeof(float));
                if ((bitsL & 0x7F800000u) == 0x7F800000u && (bitsL & 0x007FFFFFu) != 0)
                    m.hasNaN = true;
                if ((bitsR & 0x7F800000u) == 0x7F800000u && (bitsR & 0x007FFFFFu) != 0)
                    m.hasNaN = true;
                if ((bitsL & 0x7FFFFFFFu) == 0x7F800000u)
                    m.hasInf = true;
                if ((bitsR & 0x7FFFFFFFu) == 0x7F800000u)
                    m.hasInf = true;

                m.peakAmplitude = std::max(m.peakAmplitude, maxSample);
                sumSquared += static_cast<double>(outL[s]) * outL[s];
                sumSquared += static_cast<double>(outR[s]) * outR[s];

                if (maxSample > 0.9f)
                    saturatedSamples++;
                if (maxSample > 0.99f)
                    clippedSamples++;

                m.totalSamples++;
            }
        }

        m.rmsLevel = static_cast<float>(std::sqrt(sumSquared / (2.0 * m.totalSamples)));
        m.saturationRatio = static_cast<float>(saturatedSamples) / static_cast<float>(m.totalSamples);
        m.clippingRatio = static_cast<float>(clippedSamples) / static_cast<float>(m.totalSamples);

        if (m.rmsLevel > 1e-10f)
            m.crestFactorDb = 20.0f * std::log10(m.peakAmplitude / m.rmsLevel);

        // Estimate pre-limiter peak from tanh output
        // tanh(x) = y → x = atanh(y) = 0.5 * ln((1+y)/(1-y))
        // Clamp to avoid atanh(1.0) = infinity
        float clampedPeak = std::min(m.peakAmplitude, 0.9999f);
        m.estimatedPreLimiterPeak = 0.5f * std::log((1.0f + clampedPeak) / (1.0f - clampedPeak));

        return m;
    }
};

} // anonymous namespace


// =============================================================================
// TEST SECTION: Baseline (No Physical Model)
// =============================================================================

TEST_CASE("PM Output Levels: Baseline without physical model",
          "[innexus][integration][physical-model][output-levels]")
{
    PMFixture fx;
    fx.injectAnalysis();

    // Ensure physical model mix is 0 (default)
    fx.params.addChange(Innexus::kPhysModelMixId, 0.0);
    fx.params.addChange(Innexus::kHarmonicLevelId, 0.5);  // plain 1.0
    fx.params.addChange(Innexus::kResidualLevelId, 0.5);   // plain 1.0
    fx.params.addChange(Innexus::kMasterGainId, 0.5);      // unity
    fx.applyParams();

    fx.noteOn(60, 0.8f);

    auto m = fx.measureOutput();

    CAPTURE(m.peakAmplitude);
    CAPTURE(m.rmsLevel);
    CAPTURE(m.crestFactorDb);
    CAPTURE(m.saturationRatio);
    CAPTURE(m.clippingRatio);
    CAPTURE(m.estimatedPreLimiterPeak);
    CAPTURE(m.totalSamples);

    // Basic sanity
    REQUIRE_FALSE(m.hasNaN);
    REQUIRE_FALSE(m.hasInf);
    REQUIRE(m.peakAmplitude > 0.0f);      // Should produce sound
    REQUIRE(m.peakAmplitude <= 1.0f);      // Limiter caps at 1.0

    // Record baseline — these document current behavior
    INFO("Baseline (no phys model): peak=" << m.peakAmplitude
         << " rms=" << m.rmsLevel
         << " saturation=" << (m.saturationRatio * 100.0f) << "%"
         << " clipping=" << (m.clippingRatio * 100.0f) << "%");
}


// =============================================================================
// TEST SECTION: Physical Model at Full Mix — Residual Exciter + Modal Resonator
// =============================================================================

TEST_CASE("PM Output Levels: Full phys model mix with residual exciter + modal resonator",
          "[innexus][integration][physical-model][output-levels]")
{
    PMFixture fx;
    fx.injectAnalysis();

    // Physical model at full mix, residual exciter (type 0), modal resonator (type 0)
    fx.params.addChange(Innexus::kPhysModelMixId, 1.0);
    fx.params.addChange(Innexus::kExciterTypeId, 0.0);     // Residual
    fx.params.addChange(Innexus::kResonanceTypeId, 0.0);    // Modal
    fx.params.addChange(Innexus::kResonanceDecayId, 0.5);
    fx.params.addChange(Innexus::kResonanceBrightnessId, 0.5);
    fx.params.addChange(Innexus::kHarmonicLevelId, 0.5);
    fx.params.addChange(Innexus::kResidualLevelId, 0.5);
    fx.params.addChange(Innexus::kMasterGainId, 0.5);
    fx.params.addChange(Innexus::kBodyMixId, 0.0);          // No body resonance
    fx.params.addChange(Innexus::kSympatheticAmountId, 0.0); // No sympathetic
    fx.applyParams();

    fx.noteOn(60, 0.8f);

    auto m = fx.measureOutput();

    CAPTURE(m.peakAmplitude);
    CAPTURE(m.rmsLevel);
    CAPTURE(m.crestFactorDb);
    CAPTURE(m.saturationRatio);
    CAPTURE(m.clippingRatio);
    CAPTURE(m.estimatedPreLimiterPeak);

    REQUIRE_FALSE(m.hasNaN);
    REQUIRE_FALSE(m.hasInf);

    INFO("Residual+Modal: peak=" << m.peakAmplitude
         << " rms=" << m.rmsLevel
         << " saturation=" << (m.saturationRatio * 100.0f) << "%"
         << " est. pre-limiter peak=" << m.estimatedPreLimiterPeak);

    // DIAGNOSTIC: If saturation > 20%, signal is being significantly limited
    // This is the distortion the user reported
    if (m.saturationRatio > 0.2f)
    {
        WARN("HIGH SATURATION: " << (m.saturationRatio * 100.0f)
             << "% of samples above 0.9 — output is being heavily limited by tanh");
    }
}


// =============================================================================
// TEST SECTION: Impact Exciter + Modal Resonator
// =============================================================================

TEST_CASE("PM Output Levels: Impact exciter + modal resonator",
          "[innexus][integration][physical-model][output-levels]")
{
    PMFixture fx;
    fx.injectAnalysis();

    fx.params.addChange(Innexus::kPhysModelMixId, 1.0);
    fx.params.addChange(Innexus::kExciterTypeId, 0.5);     // Impact (normalized: 1/2)
    fx.params.addChange(Innexus::kResonanceTypeId, 0.0);    // Modal
    fx.params.addChange(Innexus::kImpactHardnessId, 0.5);
    fx.params.addChange(Innexus::kImpactMassId, 0.3);
    fx.params.addChange(Innexus::kImpactBrightnessId, 0.5); // neutral
    fx.params.addChange(Innexus::kImpactPositionId, 0.13);
    fx.params.addChange(Innexus::kResonanceDecayId, 0.5);
    fx.params.addChange(Innexus::kResonanceBrightnessId, 0.5);
    fx.params.addChange(Innexus::kHarmonicLevelId, 0.5);
    fx.params.addChange(Innexus::kResidualLevelId, 0.5);
    fx.params.addChange(Innexus::kMasterGainId, 0.5);
    fx.params.addChange(Innexus::kBodyMixId, 0.0);
    fx.params.addChange(Innexus::kSympatheticAmountId, 0.0);
    fx.applyParams();

    fx.noteOn(60, 0.8f);

    auto m = fx.measureOutput();

    CAPTURE(m.peakAmplitude);
    CAPTURE(m.rmsLevel);
    CAPTURE(m.saturationRatio);
    CAPTURE(m.estimatedPreLimiterPeak);

    REQUIRE_FALSE(m.hasNaN);
    REQUIRE_FALSE(m.hasInf);

    INFO("Impact+Modal: peak=" << m.peakAmplitude
         << " rms=" << m.rmsLevel
         << " saturation=" << (m.saturationRatio * 100.0f) << "%"
         << " est. pre-limiter peak=" << m.estimatedPreLimiterPeak);

    if (m.saturationRatio > 0.2f)
    {
        WARN("HIGH SATURATION (Impact+Modal): " << (m.saturationRatio * 100.0f) << "%");
    }
}


// =============================================================================
// TEST SECTION: Bow Exciter + Waveguide String
// =============================================================================

TEST_CASE("PM Output Levels: Bow exciter + waveguide string",
          "[innexus][integration][physical-model][output-levels]")
{
    PMFixture fx;
    fx.injectAnalysis();

    fx.params.addChange(Innexus::kPhysModelMixId, 1.0);
    fx.params.addChange(Innexus::kExciterTypeId, 1.0);     // Bow (normalized: 2/2)
    fx.params.addChange(Innexus::kResonanceTypeId, 1.0);    // Waveguide
    fx.params.addChange(Innexus::kBowPressureId, 0.3);
    fx.params.addChange(Innexus::kBowSpeedId, 0.5);
    fx.params.addChange(Innexus::kBowPositionId, 0.13);
    fx.params.addChange(Innexus::kWaveguideStiffnessId, 0.0);
    fx.params.addChange(Innexus::kWaveguidePickPositionId, 0.13);
    fx.params.addChange(Innexus::kResonanceDecayId, 0.5);
    fx.params.addChange(Innexus::kResonanceBrightnessId, 0.5);
    fx.params.addChange(Innexus::kHarmonicLevelId, 0.5);
    fx.params.addChange(Innexus::kResidualLevelId, 0.5);
    fx.params.addChange(Innexus::kMasterGainId, 0.5);
    fx.params.addChange(Innexus::kBodyMixId, 0.0);
    fx.params.addChange(Innexus::kSympatheticAmountId, 0.0);
    fx.applyParams();

    fx.noteOn(60, 0.8f);

    auto m = fx.measureOutput();

    CAPTURE(m.peakAmplitude);
    CAPTURE(m.rmsLevel);
    CAPTURE(m.saturationRatio);
    CAPTURE(m.estimatedPreLimiterPeak);

    REQUIRE_FALSE(m.hasNaN);
    REQUIRE_FALSE(m.hasInf);

    INFO("Bow+Waveguide: peak=" << m.peakAmplitude
         << " rms=" << m.rmsLevel
         << " saturation=" << (m.saturationRatio * 100.0f) << "%"
         << " est. pre-limiter peak=" << m.estimatedPreLimiterPeak);

    if (m.saturationRatio > 0.2f)
    {
        WARN("HIGH SATURATION (Bow+Waveguide): " << (m.saturationRatio * 100.0f) << "%");
    }
}


// =============================================================================
// TEST SECTION: Impact Exciter + Waveguide String
// =============================================================================

TEST_CASE("PM Output Levels: Impact exciter + waveguide string",
          "[innexus][integration][physical-model][output-levels]")
{
    PMFixture fx;
    fx.injectAnalysis();

    fx.params.addChange(Innexus::kPhysModelMixId, 1.0);
    fx.params.addChange(Innexus::kExciterTypeId, 0.5);     // Impact
    fx.params.addChange(Innexus::kResonanceTypeId, 1.0);    // Waveguide
    fx.params.addChange(Innexus::kImpactHardnessId, 0.5);
    fx.params.addChange(Innexus::kImpactMassId, 0.3);
    fx.params.addChange(Innexus::kImpactBrightnessId, 0.5);
    fx.params.addChange(Innexus::kImpactPositionId, 0.13);
    fx.params.addChange(Innexus::kWaveguideStiffnessId, 0.0);
    fx.params.addChange(Innexus::kWaveguidePickPositionId, 0.13);
    fx.params.addChange(Innexus::kResonanceDecayId, 0.5);
    fx.params.addChange(Innexus::kResonanceBrightnessId, 0.5);
    fx.params.addChange(Innexus::kHarmonicLevelId, 0.5);
    fx.params.addChange(Innexus::kResidualLevelId, 0.5);
    fx.params.addChange(Innexus::kMasterGainId, 0.5);
    fx.params.addChange(Innexus::kBodyMixId, 0.0);
    fx.params.addChange(Innexus::kSympatheticAmountId, 0.0);
    fx.applyParams();

    fx.noteOn(60, 0.8f);

    auto m = fx.measureOutput();

    CAPTURE(m.peakAmplitude);
    CAPTURE(m.rmsLevel);
    CAPTURE(m.saturationRatio);
    CAPTURE(m.estimatedPreLimiterPeak);

    REQUIRE_FALSE(m.hasNaN);
    REQUIRE_FALSE(m.hasInf);

    INFO("Impact+Waveguide: peak=" << m.peakAmplitude
         << " rms=" << m.rmsLevel
         << " saturation=" << (m.saturationRatio * 100.0f) << "%"
         << " est. pre-limiter peak=" << m.estimatedPreLimiterPeak);

    if (m.saturationRatio > 0.2f)
    {
        WARN("HIGH SATURATION (Impact+Waveguide): " << (m.saturationRatio * 100.0f) << "%");
    }
}


// =============================================================================
// TEST SECTION: Body Resonance adds to output level
// =============================================================================

TEST_CASE("PM Output Levels: Body resonance contribution",
          "[innexus][integration][physical-model][output-levels]")
{
    PMFixture fx;
    fx.injectAnalysis();

    // First: measure without body resonance
    fx.params.addChange(Innexus::kPhysModelMixId, 1.0);
    fx.params.addChange(Innexus::kExciterTypeId, 0.5);     // Impact
    fx.params.addChange(Innexus::kResonanceTypeId, 0.0);    // Modal
    fx.params.addChange(Innexus::kResonanceDecayId, 0.5);
    fx.params.addChange(Innexus::kResonanceBrightnessId, 0.5);
    fx.params.addChange(Innexus::kHarmonicLevelId, 0.5);
    fx.params.addChange(Innexus::kResidualLevelId, 0.5);
    fx.params.addChange(Innexus::kMasterGainId, 0.5);
    fx.params.addChange(Innexus::kBodyMixId, 0.0);          // Body OFF
    fx.params.addChange(Innexus::kSympatheticAmountId, 0.0);
    fx.applyParams();

    fx.noteOn(60, 0.8f);
    auto mNoBody = fx.measureOutput();

    // Reset processor for second run
    fx.processor.setActive(false);
    fx.processor.setActive(true);
    fx.injectAnalysis();

    // Now with body resonance at full
    fx.params.addChange(Innexus::kPhysModelMixId, 1.0);
    fx.params.addChange(Innexus::kExciterTypeId, 0.5);
    fx.params.addChange(Innexus::kResonanceTypeId, 0.0);
    fx.params.addChange(Innexus::kResonanceDecayId, 0.5);
    fx.params.addChange(Innexus::kResonanceBrightnessId, 0.5);
    fx.params.addChange(Innexus::kHarmonicLevelId, 0.5);
    fx.params.addChange(Innexus::kResidualLevelId, 0.5);
    fx.params.addChange(Innexus::kMasterGainId, 0.5);
    fx.params.addChange(Innexus::kBodyMixId, 1.0);          // Body FULL
    fx.params.addChange(Innexus::kBodySizeId, 0.5);
    fx.params.addChange(Innexus::kBodyMaterialId, 0.5);
    fx.params.addChange(Innexus::kSympatheticAmountId, 0.0);
    fx.applyParams();

    fx.noteOn(60, 0.8f);
    auto mBody = fx.measureOutput();

    CAPTURE(mNoBody.peakAmplitude);
    CAPTURE(mNoBody.rmsLevel);
    CAPTURE(mNoBody.saturationRatio);
    CAPTURE(mBody.peakAmplitude);
    CAPTURE(mBody.rmsLevel);
    CAPTURE(mBody.saturationRatio);

    REQUIRE_FALSE(mNoBody.hasNaN);
    REQUIRE_FALSE(mBody.hasNaN);

    float rmsBoostDb = 20.0f * std::log10(
        std::max(mBody.rmsLevel, 1e-10f) / std::max(mNoBody.rmsLevel, 1e-10f));

    INFO("Body resonance RMS boost: " << rmsBoostDb << " dB");
    INFO("Without body: peak=" << mNoBody.peakAmplitude << " rms=" << mNoBody.rmsLevel
         << " sat=" << (mNoBody.saturationRatio * 100.0f) << "%");
    INFO("With body: peak=" << mBody.peakAmplitude << " rms=" << mBody.rmsLevel
         << " sat=" << (mBody.saturationRatio * 100.0f) << "%");
}


// =============================================================================
// TEST SECTION: Sympathetic Resonance adds to output level
// =============================================================================

TEST_CASE("PM Output Levels: Sympathetic resonance contribution",
          "[innexus][integration][physical-model][output-levels]")
{
    PMFixture fx;
    fx.injectAnalysis();

    // Without sympathetic
    fx.params.addChange(Innexus::kPhysModelMixId, 1.0);
    fx.params.addChange(Innexus::kExciterTypeId, 0.5);
    fx.params.addChange(Innexus::kResonanceTypeId, 0.0);
    fx.params.addChange(Innexus::kResonanceDecayId, 0.5);
    fx.params.addChange(Innexus::kResonanceBrightnessId, 0.5);
    fx.params.addChange(Innexus::kHarmonicLevelId, 0.5);
    fx.params.addChange(Innexus::kResidualLevelId, 0.5);
    fx.params.addChange(Innexus::kMasterGainId, 0.5);
    fx.params.addChange(Innexus::kBodyMixId, 0.0);
    fx.params.addChange(Innexus::kSympatheticAmountId, 0.0); // Sympathetic OFF
    fx.applyParams();

    fx.noteOn(60, 0.8f);
    auto mNoSyp = fx.measureOutput();

    fx.processor.setActive(false);
    fx.processor.setActive(true);
    fx.injectAnalysis();

    // With sympathetic at full
    fx.params.addChange(Innexus::kPhysModelMixId, 1.0);
    fx.params.addChange(Innexus::kExciterTypeId, 0.5);
    fx.params.addChange(Innexus::kResonanceTypeId, 0.0);
    fx.params.addChange(Innexus::kResonanceDecayId, 0.5);
    fx.params.addChange(Innexus::kResonanceBrightnessId, 0.5);
    fx.params.addChange(Innexus::kHarmonicLevelId, 0.5);
    fx.params.addChange(Innexus::kResidualLevelId, 0.5);
    fx.params.addChange(Innexus::kMasterGainId, 0.5);
    fx.params.addChange(Innexus::kBodyMixId, 0.0);
    fx.params.addChange(Innexus::kSympatheticAmountId, 1.0); // Sympathetic FULL
    fx.params.addChange(Innexus::kSympatheticDecayId, 0.5);
    fx.applyParams();

    fx.noteOn(60, 0.8f);
    auto mSyp = fx.measureOutput();

    CAPTURE(mNoSyp.peakAmplitude);
    CAPTURE(mNoSyp.rmsLevel);
    CAPTURE(mNoSyp.saturationRatio);
    CAPTURE(mSyp.peakAmplitude);
    CAPTURE(mSyp.rmsLevel);
    CAPTURE(mSyp.saturationRatio);

    REQUIRE_FALSE(mNoSyp.hasNaN);
    REQUIRE_FALSE(mSyp.hasNaN);

    float rmsBoostDb = 20.0f * std::log10(
        std::max(mSyp.rmsLevel, 1e-10f) / std::max(mNoSyp.rmsLevel, 1e-10f));

    INFO("Sympathetic resonance RMS boost: " << rmsBoostDb << " dB");
    INFO("Without sympathetic: peak=" << mNoSyp.peakAmplitude << " rms=" << mNoSyp.rmsLevel
         << " sat=" << (mNoSyp.saturationRatio * 100.0f) << "%");
    INFO("With sympathetic: peak=" << mSyp.peakAmplitude << " rms=" << mSyp.rmsLevel
         << " sat=" << (mSyp.saturationRatio * 100.0f) << "%");
}


// =============================================================================
// TEST SECTION: Full Stack — All Physical Model Components Active
// =============================================================================

TEST_CASE("PM Output Levels: Full physical model stack (exciter + resonator + body + sympathetic)",
          "[innexus][integration][physical-model][output-levels]")
{
    PMFixture fx;
    fx.injectAnalysis();

    // Everything on at moderate settings
    fx.params.addChange(Innexus::kPhysModelMixId, 1.0);
    fx.params.addChange(Innexus::kExciterTypeId, 0.5);      // Impact
    fx.params.addChange(Innexus::kResonanceTypeId, 0.0);     // Modal
    fx.params.addChange(Innexus::kImpactHardnessId, 0.5);
    fx.params.addChange(Innexus::kImpactMassId, 0.3);
    fx.params.addChange(Innexus::kImpactBrightnessId, 0.5);
    fx.params.addChange(Innexus::kResonanceDecayId, 0.5);
    fx.params.addChange(Innexus::kResonanceBrightnessId, 0.5);
    fx.params.addChange(Innexus::kBodyMixId, 0.5);           // Body at 50%
    fx.params.addChange(Innexus::kBodySizeId, 0.5);
    fx.params.addChange(Innexus::kBodyMaterialId, 0.5);
    fx.params.addChange(Innexus::kSympatheticAmountId, 0.5);  // Sympathetic at 50%
    fx.params.addChange(Innexus::kSympatheticDecayId, 0.5);
    fx.params.addChange(Innexus::kHarmonicLevelId, 0.5);
    fx.params.addChange(Innexus::kResidualLevelId, 0.5);
    fx.params.addChange(Innexus::kMasterGainId, 0.5);
    fx.applyParams();

    fx.noteOn(60, 0.8f);

    auto m = fx.measureOutput();

    CAPTURE(m.peakAmplitude);
    CAPTURE(m.rmsLevel);
    CAPTURE(m.crestFactorDb);
    CAPTURE(m.saturationRatio);
    CAPTURE(m.clippingRatio);
    CAPTURE(m.estimatedPreLimiterPeak);

    REQUIRE_FALSE(m.hasNaN);
    REQUIRE_FALSE(m.hasInf);

    INFO("FULL STACK: peak=" << m.peakAmplitude
         << " rms=" << m.rmsLevel
         << " crest=" << m.crestFactorDb << "dB"
         << " saturation=" << (m.saturationRatio * 100.0f) << "%"
         << " clipping=" << (m.clippingRatio * 100.0f) << "%"
         << " est. pre-limiter peak=" << m.estimatedPreLimiterPeak);

    // DIAGNOSTIC CHECK: Confirm the distortion
    // A clean signal should have < 5% saturation; > 20% means heavy limiting
    if (m.saturationRatio > 0.2f)
    {
        WARN("CONFIRMED: Output is heavily saturated ("
             << (m.saturationRatio * 100.0f)
             << "%). The signal is being driven hard into the tanh limiter, "
             "causing the 'aggressive industrial' distortion character.");
    }

    // A well-gain-staged signal should have crest factor > 6 dB (some dynamics)
    // Very low crest factor means the signal is compressed/limited
    if (m.crestFactorDb < 3.0f)
    {
        WARN("LOW CREST FACTOR: " << m.crestFactorDb
             << " dB — signal dynamics are being crushed by the limiter");
    }
}


// =============================================================================
// TEST SECTION: Mix Sweep — Gradual Increase of Physical Model Mix
// =============================================================================

TEST_CASE("PM Output Levels: Mix sweep from 0.0 to 1.0",
          "[innexus][integration][physical-model][output-levels]")
{
    // Test at 5 mix points to see where distortion kicks in
    const float mixValues[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};

    for (float mix : mixValues)
    {
        SECTION("PhysModelMix = " + std::to_string(mix))
        {
            PMFixture fx;
            fx.injectAnalysis();

            fx.params.addChange(Innexus::kPhysModelMixId, static_cast<double>(mix));
            fx.params.addChange(Innexus::kExciterTypeId, 0.5);  // Impact
            fx.params.addChange(Innexus::kResonanceTypeId, 0.0); // Modal
            fx.params.addChange(Innexus::kResonanceDecayId, 0.5);
            fx.params.addChange(Innexus::kResonanceBrightnessId, 0.5);
            fx.params.addChange(Innexus::kHarmonicLevelId, 0.5);
            fx.params.addChange(Innexus::kResidualLevelId, 0.5);
            fx.params.addChange(Innexus::kMasterGainId, 0.5);
            fx.params.addChange(Innexus::kBodyMixId, 0.0);
            fx.params.addChange(Innexus::kSympatheticAmountId, 0.0);
            fx.applyParams();

            fx.noteOn(60, 0.8f);

            auto m = fx.measureOutput();

            CAPTURE(mix);
            CAPTURE(m.peakAmplitude);
            CAPTURE(m.rmsLevel);
            CAPTURE(m.saturationRatio);
            CAPTURE(m.estimatedPreLimiterPeak);

            REQUIRE_FALSE(m.hasNaN);
            REQUIRE_FALSE(m.hasInf);

            INFO("Mix=" << mix << ": peak=" << m.peakAmplitude
                 << " rms=" << m.rmsLevel
                 << " sat=" << (m.saturationRatio * 100.0f) << "%");
        }
    }
}


// =============================================================================
// TEST SECTION: Multiple Pitches — Distortion Across the Range
// =============================================================================

TEST_CASE("PM Output Levels: Distortion across pitch range",
          "[innexus][integration][physical-model][output-levels]")
{
    // Test low, mid, and high notes
    const int pitches[] = {36, 48, 60, 72, 84};
    const float f0s[] = {65.41f, 130.81f, 261.63f, 523.25f, 1046.5f};

    for (int i = 0; i < 5; ++i)
    {
        SECTION("MIDI note " + std::to_string(pitches[i]))
        {
            PMFixture fx;
            // Create analysis with matching f0
            auto* analysis = makePMAnalysis(50, f0s[i], 0.5f);
            fx.processor.testInjectAnalysis(analysis);

            fx.params.addChange(Innexus::kPhysModelMixId, 1.0);
            fx.params.addChange(Innexus::kExciterTypeId, 0.5);  // Impact
            fx.params.addChange(Innexus::kResonanceTypeId, 0.0); // Modal
            fx.params.addChange(Innexus::kResonanceDecayId, 0.5);
            fx.params.addChange(Innexus::kResonanceBrightnessId, 0.5);
            fx.params.addChange(Innexus::kHarmonicLevelId, 0.5);
            fx.params.addChange(Innexus::kResidualLevelId, 0.5);
            fx.params.addChange(Innexus::kMasterGainId, 0.5);
            fx.params.addChange(Innexus::kBodyMixId, 0.0);
            fx.params.addChange(Innexus::kSympatheticAmountId, 0.0);
            fx.applyParams();

            fx.noteOn(pitches[i], 0.8f);

            auto m = fx.measureOutput();

            CAPTURE(pitches[i]);
            CAPTURE(m.peakAmplitude);
            CAPTURE(m.rmsLevel);
            CAPTURE(m.saturationRatio);

            REQUIRE_FALSE(m.hasNaN);
            REQUIRE_FALSE(m.hasInf);

            INFO("Note " << pitches[i] << " (f0=" << f0s[i] << "Hz): peak="
                 << m.peakAmplitude << " rms=" << m.rmsLevel
                 << " sat=" << (m.saturationRatio * 100.0f) << "%");
        }
    }
}


// =============================================================================
// TEST SECTION: ADSR Envelope Reset on Note-On
// =============================================================================

TEST_CASE("ADSR envelope resets to zero on fresh note-on",
          "[innexus][integration][adsr][output-levels]")
{
    // Reproduce the bug: play a note with adsrAmount=0 (ADSR bypassed),
    // then turn up adsrAmount and play a new note. The envelope should
    // start its attack from 0, not from a stale value.
    PMFixture fx;
    fx.injectAnalysis();

    // Step 1: Play a note with ADSR bypassed (amount=0, the default)
    fx.params.addChange(Innexus::kAdsrAmountId, 0.0);
    fx.params.addChange(Innexus::kHarmonicLevelId, 0.5);
    fx.params.addChange(Innexus::kResidualLevelId, 0.5);
    fx.params.addChange(Innexus::kMasterGainId, 0.5);
    fx.applyParams();

    fx.noteOn(60, 0.8f);

    // Process several blocks so the voice state is established
    for (int i = 0; i < 20; ++i)
        fx.processBlock();

    fx.noteOff(60);

    // Let it decay
    for (int i = 0; i < 10; ++i)
        fx.processBlock();

    // Step 2: Now enable ADSR with a slow attack (500ms) and play a new note.
    // With the bug, the envelope starts from a stale output_ value (not 0),
    // so the first block's output would be at full level instead of near-zero.
    fx.params.addChange(Innexus::kAdsrAmountId, 1.0);     // Full ADSR effect
    fx.params.addChange(Innexus::kAdsrAttackId, 0.5);      // ~500ms attack
    fx.params.addChange(Innexus::kAdsrSustainId, 1.0);     // Full sustain
    fx.params.addChange(Innexus::kAdsrDecayId, 0.0);       // Minimal decay
    fx.params.addChange(Innexus::kAdsrReleaseId, 0.2);     // Short release
    fx.applyParams();

    fx.noteOn(64, 0.8f);

    // Measure the FIRST block immediately after note-on.
    // With slow attack (500ms) and the envelope starting from 0,
    // the first block (512 samples ≈ 11.6ms) should have very low output.
    // At 11.6ms into a 500ms attack, the envelope is at ~2.3% of peak.
    float firstBlockPeak = fx.processBlock();

    // Process more blocks to reach steady state
    for (int i = 0; i < 80; ++i)
        fx.processBlock();
    float steadyStatePeak = fx.processBlock();

    INFO("First block peak (should be near-zero with 500ms attack): " << firstBlockPeak);
    INFO("Steady state peak: " << steadyStatePeak);

    // The first block should be significantly quieter than steady state.
    // With the bug: firstBlockPeak ≈ steadyStatePeak (no attack shape)
    // With the fix: firstBlockPeak << steadyStatePeak (attack ramp from 0)
    REQUIRE(steadyStatePeak > 0.01f); // Must produce sound
    if (steadyStatePeak > 0.01f)
    {
        float ratio = firstBlockPeak / steadyStatePeak;
        INFO("First block / steady state ratio: " << ratio << " (should be < 0.3)");
        REQUIRE(ratio < 0.3f); // First block should be < 30% of steady state
    }
}
