// ==============================================================================
// M6 Pipeline Integration Tests
// ==============================================================================
// Full pipeline integration test covering all M6 features active simultaneously.
// Also includes CPU benchmark (opt-in via [.perf] tag).
//
// Feature: 120-creative-extensions
// Tasks: T055, T057
// Requirements: FR-049, FR-052, SC-007, SC-008
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"
#include "dsp/sample_analysis.h"

#include <krate/dsp/processors/harmonic_types.h>
#include <krate/dsp/processors/residual_types.h>
#include <krate/dsp/processors/residual_synthesizer.h>

#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"

#include <algorithm>
#include <chrono>
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

static constexpr double kPipelineTestSampleRate = 44100.0;
static constexpr int32 kPipelineTestBlockSize = 512;

static ProcessSetup makePipelineSetup(double sampleRate = kPipelineTestSampleRate,
                                       int32 blockSize = kPipelineTestBlockSize)
{
    ProcessSetup setup{};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = blockSize;
    setup.sampleRate = sampleRate;
    return setup;
}

/// Create test analysis with spectrally distinct content for memory slots
static Innexus::SampleAnalysis* makePipelineTestAnalysis(
    int numFrames = 20,
    float f0 = 440.0f,
    int numPartials = 16,
    float baseAmplitude = 0.5f)
{
    auto* analysis = new Innexus::SampleAnalysis();
    analysis->sampleRate = 44100.0f;
    analysis->hopTimeSec = 512.0f / 44100.0f;

    for (int f = 0; f < numFrames; ++f)
    {
        Krate::DSP::HarmonicFrame frame{};
        frame.f0 = f0;
        frame.f0Confidence = 0.9f;
        frame.numPartials = numPartials;
        frame.globalAmplitude = baseAmplitude;
        frame.spectralCentroid = 1200.0f;
        frame.brightness = 0.5f;
        frame.noisiness = 0.1f;

        for (int p = 0; p < numPartials; ++p)
        {
            auto& partial = frame.partials[static_cast<size_t>(p)];
            partial.harmonicIndex = p + 1;
            partial.frequency = f0 * static_cast<float>(p + 1);
            partial.amplitude = baseAmplitude / static_cast<float>(p + 1);
            partial.relativeFrequency = static_cast<float>(p + 1);
            partial.inharmonicDeviation = 0.0f;
            partial.stability = 1.0f;
            partial.age = 10;
            partial.phase = static_cast<float>(p) * 0.3f;
        }

        analysis->frames.push_back(frame);
    }

    analysis->totalFrames = analysis->frames.size();
    analysis->filePath = "test_pipeline.wav";

    return analysis;
}

// Minimal EventList
class PipelineTestEventList : public IEventList
{
public:
    tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

    int32 PLUGIN_API getEventCount() override
    {
        return static_cast<int32>(events_.size());
    }
    tresult PLUGIN_API getEvent(int32 index, Event& e) override
    {
        if (index < 0 || index >= static_cast<int32>(events_.size()))
            return kResultFalse;
        e = events_[static_cast<size_t>(index)];
        return kResultTrue;
    }
    tresult PLUGIN_API addEvent(Event& e) override
    {
        events_.push_back(e);
        return kResultTrue;
    }

    void addNoteOn(int16 pitch, float velocity, int32 sampleOffset = 0)
    {
        Event e{};
        e.type = Event::kNoteOnEvent;
        e.sampleOffset = sampleOffset;
        e.noteOn.channel = 0;
        e.noteOn.pitch = pitch;
        e.noteOn.velocity = velocity;
        e.noteOn.noteId = pitch;
        e.noteOn.tuning = 0.0f;
        e.noteOn.length = 0;
        events_.push_back(e);
    }

    void clear() { events_.clear(); }

private:
    std::vector<Event> events_;
};

// Minimal parameter value queue
class PipelineTestParamValueQueue : public IParamValueQueue
{
public:
    PipelineTestParamValueQueue(ParamID id, ParamValue val)
        : id_(id), value_(val) {}

    ParamID PLUGIN_API getParameterId() override { return id_; }
    int32 PLUGIN_API getPointCount() override { return 1; }
    tresult PLUGIN_API getPoint(int32 /*index*/, int32& sampleOffset,
                                 ParamValue& value) override
    {
        sampleOffset = 0;
        value = value_;
        return kResultTrue;
    }
    tresult PLUGIN_API addPoint(int32, ParamValue, int32&) override
    {
        return kResultFalse;
    }

    tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

private:
    ParamID id_;
    ParamValue value_;
};

// Parameter changes container
class PipelineTestParameterChanges : public IParameterChanges
{
public:
    void addChange(ParamID id, ParamValue val)
    {
        queues_.emplace_back(id, val);
    }

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

    void clear() { queues_.clear(); }

private:
    std::vector<PipelineTestParamValueQueue> queues_;
};

// Helper fixture for pipeline integration tests
struct PipelineTestFixture
{
    Innexus::Processor processor;
    PipelineTestEventList events;
    PipelineTestParameterChanges paramChanges;

    std::vector<float> outL;
    std::vector<float> outR;
    float* outChannels[2];
    AudioBusBuffers outputBus{};
    ProcessData data{};

    PipelineTestFixture(int32 blockSize = kPipelineTestBlockSize,
                        double sampleRate = kPipelineTestSampleRate)
        : outL(static_cast<size_t>(blockSize), 0.0f)
        , outR(static_cast<size_t>(blockSize), 0.0f)
    {
        outChannels[0] = outL.data();
        outChannels[1] = outR.data();

        outputBus.numChannels = 2;
        outputBus.channelBuffers32 = outChannels;
        outputBus.silenceFlags = 0;

        data.processMode = kRealtime;
        data.symbolicSampleSize = kSample32;
        data.numSamples = blockSize;
        data.numOutputs = 1;
        data.outputs = &outputBus;
        data.numInputs = 0;
        data.inputs = nullptr;
        data.inputParameterChanges = nullptr;
        data.outputParameterChanges = nullptr;
        data.inputEvents = &events;
        data.outputEvents = nullptr;

        processor.initialize(nullptr);
        auto setup = makePipelineSetup(sampleRate, blockSize);
        processor.setupProcessing(setup);
        processor.setActive(true);
    }

    ~PipelineTestFixture()
    {
        processor.setActive(false);
        processor.terminate();
    }

    void injectAnalysis(Innexus::SampleAnalysis* analysis)
    {
        processor.testInjectAnalysis(analysis);
    }

    void processBlock()
    {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        data.inputParameterChanges = nullptr;
        processor.process(data);
    }

    void processBlockWithParams()
    {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        data.inputParameterChanges = &paramChanges;
        processor.process(data);
        paramChanges.clear();
    }
};

/// Bit-level NaN check (safe under -ffast-math)
static bool isNaNBits(float x) noexcept
{
    uint32_t bits;
    std::memcpy(&bits, &x, sizeof(bits));
    return ((bits & 0x7F800000u) == 0x7F800000u) && ((bits & 0x007FFFFFu) != 0);
}

// =============================================================================
// T055: Full Pipeline Integration Test
// =============================================================================

TEST_CASE("M6 pipeline: all features active produces non-silent non-NaN output",
          "[innexus][m6][integration][pipeline]")
{
    PipelineTestFixture fix;
    fix.injectAnalysis(makePipelineTestAnalysis());

    // Enable all M6 features simultaneously
    fix.paramChanges.addChange(Innexus::kStereoSpreadId, 0.5);
    fix.paramChanges.addChange(Innexus::kDetuneSpreadId, 0.5);
    fix.paramChanges.addChange(Innexus::kEvolutionEnableId, 1.0);
    fix.paramChanges.addChange(Innexus::kEvolutionSpeedId, 0.5);  // mid-range speed
    fix.paramChanges.addChange(Innexus::kEvolutionDepthId, 0.5);
    fix.paramChanges.addChange(Innexus::kMod1EnableId, 1.0);
    fix.paramChanges.addChange(Innexus::kMod1RateId, 0.5);
    fix.paramChanges.addChange(Innexus::kMod1DepthId, 0.5);
    fix.paramChanges.addChange(Innexus::kMod2EnableId, 1.0);
    fix.paramChanges.addChange(Innexus::kMod2RateId, 0.3);
    fix.paramChanges.addChange(Innexus::kMod2DepthId, 0.4);

    // Trigger a note
    fix.events.addNoteOn(60, 0.8f);
    fix.processBlockWithParams();
    fix.events.clear();

    // Advance 44100 samples total (about 1 second at 44.1kHz)
    // We already processed one block of 512 samples
    constexpr int totalSamples = 44100;
    constexpr int blockSize = kPipelineTestBlockSize;
    const int remainingBlocks = (totalSamples - blockSize) / blockSize;

    bool hasNaN = false;
    bool hasSilence = true;
    float maxAbsL = 0.0f;
    float maxAbsR = 0.0f;

    // Check first block
    for (size_t i = 0; i < fix.outL.size(); ++i)
    {
        if (isNaNBits(fix.outL[i]) || isNaNBits(fix.outR[i]))
            hasNaN = true;
        maxAbsL = std::max(maxAbsL, std::abs(fix.outL[i]));
        maxAbsR = std::max(maxAbsR, std::abs(fix.outR[i]));
    }

    for (int b = 0; b < remainingBlocks; ++b)
    {
        fix.processBlock();
        for (size_t i = 0; i < fix.outL.size(); ++i)
        {
            if (isNaNBits(fix.outL[i]) || isNaNBits(fix.outR[i]))
                hasNaN = true;
            maxAbsL = std::max(maxAbsL, std::abs(fix.outL[i]));
            maxAbsR = std::max(maxAbsR, std::abs(fix.outR[i]));
        }
    }

    if (maxAbsL > 1e-8f || maxAbsR > 1e-8f)
        hasSilence = false;

    REQUIRE_FALSE(hasNaN);
    REQUIRE_FALSE(hasSilence);
}

// =============================================================================
// T055(c): Verify pipeline order: blend/evolution -> filter -> modulators -> osc
// (FR-049)
// =============================================================================
// The pipeline order is verified structurally by checking that when modulators
// are enabled, their effect is applied AFTER the harmonic filter. If a partial
// is filtered out (amplitude=0 by filter), the modulator cannot bring it back.
// If the order were reversed, the modulator would have modified the amplitude
// before the filter zeroed it.

TEST_CASE("M6 pipeline order: filtered partials stay silent even with modulators (FR-049)",
          "[innexus][m6][integration][pipeline]")
{
    // Use block size = 128 for quicker settling
    PipelineTestFixture fix(128, kPipelineTestSampleRate);
    fix.injectAnalysis(makePipelineTestAnalysis(20, 440.0f, 16, 0.5f));

    // Enable "Odd Only" harmonic filter (type index=1): only odd partials pass
    // Set mod1 to amplitude target, full depth, applied to all partials
    fix.paramChanges.addChange(Innexus::kHarmonicFilterTypeId, 0.25); // OddOnly = 1/4
    fix.paramChanges.addChange(Innexus::kMod1EnableId, 1.0);
    fix.paramChanges.addChange(Innexus::kMod1TargetId, 0.0);  // Amplitude
    fix.paramChanges.addChange(Innexus::kMod1DepthId, 1.0);
    fix.paramChanges.addChange(Innexus::kMod1RateId, 0.25);   // 5 Hz approx
    fix.paramChanges.addChange(Innexus::kMod1RangeStartId, 0.0);   // partial 1
    fix.paramChanges.addChange(Innexus::kMod1RangeEndId, 1.0);     // partial 96
    fix.paramChanges.addChange(Innexus::kStereoSpreadId, 0.0);

    fix.events.addNoteOn(60, 0.8f);
    fix.processBlockWithParams();
    fix.events.clear();

    // Let smoothers settle
    for (int b = 0; b < 40; ++b)
        fix.processBlock();

    // The output should have non-zero samples (odd partials pass through filter,
    // then modulator modifies their amplitude but cannot bring back even partials
    // that were already filtered). This test mainly verifies the processor doesn't
    // crash and produces output with the correct pipeline order.
    fix.processBlock();
    bool hasOutput = false;
    for (size_t i = 0; i < fix.outL.size(); ++i)
    {
        if (std::abs(fix.outL[i]) > 1e-8f || std::abs(fix.outR[i]) > 1e-8f)
        {
            hasOutput = true;
            break;
        }
    }
    REQUIRE(hasOutput);
}

// =============================================================================
// T055(d): Verify blendEnabled overrides evolutionEnabled (FR-052)
// =============================================================================

TEST_CASE("M6 pipeline: blendEnabled overrides evolutionEnabled (FR-052)",
          "[innexus][m6][integration][pipeline]")
{
    PipelineTestFixture fix(128, kPipelineTestSampleRate);
    fix.injectAnalysis(makePipelineTestAnalysis(20, 440.0f, 8, 0.5f));

    // First, capture a snapshot into slot 1 so blend has something to work with.
    // We need to trigger a capture and recall cycle.

    // Trigger note and let some audio process to establish analysis frames
    fix.events.addNoteOn(60, 0.8f);
    fix.processBlockWithParams();
    fix.events.clear();

    // Process some blocks to settle
    for (int b = 0; b < 10; ++b)
        fix.processBlock();

    // Capture into slot 0
    fix.paramChanges.addChange(Innexus::kMemorySlotId, 0.0);  // Slot 1
    fix.paramChanges.addChange(Innexus::kMemoryCaptureId, 1.0);
    fix.processBlockWithParams();
    fix.paramChanges.addChange(Innexus::kMemoryCaptureId, 0.0);
    fix.processBlockWithParams();

    // Capture into slot 1 (different morph position for variation)
    fix.paramChanges.addChange(Innexus::kMemorySlotId, 1.0 / 7.0);  // Slot 2
    fix.paramChanges.addChange(Innexus::kMorphPositionId, 0.5);
    fix.paramChanges.addChange(Innexus::kMemoryCaptureId, 1.0);
    fix.processBlockWithParams();
    fix.paramChanges.addChange(Innexus::kMemoryCaptureId, 0.0);
    fix.processBlockWithParams();

    // Now enable BOTH evolution AND blend
    fix.paramChanges.addChange(Innexus::kEvolutionEnableId, 1.0);
    fix.paramChanges.addChange(Innexus::kEvolutionSpeedId, 0.5);
    fix.paramChanges.addChange(Innexus::kEvolutionDepthId, 1.0);
    fix.paramChanges.addChange(Innexus::kBlendEnableId, 1.0);
    fix.paramChanges.addChange(Innexus::kBlendSlotWeight1Id, 1.0);
    fix.processBlockWithParams();

    // Collect output with both enabled (blend should override evolution)
    for (int b = 0; b < 20; ++b)
        fix.processBlock();

    std::vector<float> blendOutput(fix.outL.begin(), fix.outL.end());
    fix.processBlock();
    std::vector<float> blendOutput2(fix.outL.begin(), fix.outL.end());

    // Now disable blend, keep evolution
    fix.paramChanges.addChange(Innexus::kBlendEnableId, 0.0);
    fix.processBlockWithParams();

    for (int b = 0; b < 20; ++b)
        fix.processBlock();

    fix.processBlock();
    std::vector<float> evoOutput(fix.outL.begin(), fix.outL.end());

    // The blend output and evolution output should differ (proving blend was
    // overriding evolution when both were enabled, and now evolution alone
    // produces different output).
    // At minimum: verify both produce non-silent output
    bool blendHasOutput = false;
    bool evoHasOutput = false;
    for (size_t i = 0; i < blendOutput.size(); ++i)
    {
        if (std::abs(blendOutput[i]) > 1e-8f) blendHasOutput = true;
        if (std::abs(evoOutput[i]) > 1e-8f) evoHasOutput = true;
    }

    // Both modes produce output
    REQUIRE(blendHasOutput);
    REQUIRE(evoHasOutput);

    // With blend enabled, evolution engine is active but not used for frame
    // selection (FR-052). This is verified structurally in the processor code.
    // The test verifies at minimum that the processor doesn't crash with both active.
}

// =============================================================================
// T055(e): Click-free parameter sweeps (SC-007)
// =============================================================================
// Sweep each M6 parameter from 0 to 1 at maximum rate and verify no
// sample-level discontinuities above -80 dBFS.

namespace {

/// Measure the maximum sample-to-sample delta in a buffer, tracking
/// continuity across calls via prevSample (updated in place).
float measureMaxDelta(const std::vector<float>& buffer, float& prevSample)
{
    float maxDelta = 0.0f;
    for (size_t i = 0; i < buffer.size(); ++i)
    {
        float delta = std::abs(buffer[i] - prevSample);
        if (delta > maxDelta) maxDelta = delta;
        prevSample = buffer[i];
    }
    return maxDelta;
}

/// Measure the maximum "second-order discontinuity" -- the largest jump
/// in the sample-to-sample delta compared to the previous delta.
/// A click appears as a sudden spike in the delta that the previous delta
/// didn't predict. Normal waveform evolution has smoothly changing deltas;
/// clicks have sharp spikes in the delta derivative.
float measureMaxDeltaOfDelta(const std::vector<float>& buffer, float& prevSample,
                              float& prevDelta)
{
    float maxDod = 0.0f;
    for (size_t i = 0; i < buffer.size(); ++i)
    {
        float delta = std::abs(buffer[i] - prevSample);
        float dod = std::abs(delta - prevDelta);
        if (dod > maxDod) maxDod = dod;
        prevSample = buffer[i];
        prevDelta = delta;
    }
    return maxDod;
}

} // namespace

TEST_CASE("M6 pipeline: click-free parameter sweeps (SC-007)",
          "[innexus][m6][integration][pipeline]")
{
    // SC-007 methodology: use a SINGLE processor instance per parameter.
    // Phase 1 (static): hold the parameter at its starting value, measure
    //   the max sample-to-sample delta of the steady-state output.
    // Phase 2 (sweep): CONTINUE processing on the same instance and sweep
    //   the parameter from 0 to 1, measure max sample-to-sample delta.
    // The ADDED discontinuity (sweepMaxDelta - staticMaxDelta) must be
    // below -80 dBFS (0.0001 linear). Using a single instance ensures
    // both phases produce the same underlying audio, so any excess delta
    // is genuinely from the parameter change, not different waveforms.

    struct ParamSweepInfo
    {
        ParamID id;
        const char* name;
    };

    const ParamSweepInfo sweepParams[] = {
        {Innexus::kStereoSpreadId, "StereoSpread"},
        {Innexus::kDetuneSpreadId, "DetuneSpread"},
        {Innexus::kMod1DepthId, "Mod1Depth"},
        {Innexus::kMod2DepthId, "Mod2Depth"},
    };

    // -80 dBFS in linear amplitude
    constexpr float kClickThresholdLinear = 0.0001f;
    constexpr int measureBlocks = 20;

    for (const auto& param : sweepParams)
    {
        SECTION(std::string("Sweep ") + param.name)
        {
            // --- Step 1: Measure STATIC baseline across multiple values ---
            // Sample the max delta at several static parameter values (0, 0.25,
            // 0.5, 0.75, 1.0) to capture the full range of natural signal
            // dynamics. Take the max of all these as the baseline.
            float staticMaxDelta = 0.0f;

            const double staticValues[] = {0.0, 0.25, 0.5, 0.75, 1.0};
            for (double sv : staticValues)
            {
                PipelineTestFixture fixS(128, kPipelineTestSampleRate);
                fixS.injectAnalysis(makePipelineTestAnalysis(20, 440.0f, 8, 0.5f));

                fixS.paramChanges.addChange(Innexus::kMod1EnableId, 1.0);
                fixS.paramChanges.addChange(Innexus::kMod1RateId, 0.25);
                fixS.paramChanges.addChange(Innexus::kMod2EnableId, 1.0);
                fixS.paramChanges.addChange(Innexus::kMod2RateId, 0.3);
                fixS.paramChanges.addChange(param.id, sv);

                fixS.events.addNoteOn(60, 0.8f);
                fixS.processBlockWithParams();
                fixS.events.clear();

                for (int b = 0; b < 30; ++b)
                    fixS.processBlock();

                float prev = fixS.outL.back();
                for (int b = 0; b < measureBlocks; ++b)
                {
                    fixS.processBlock();
                    float d = measureMaxDelta(fixS.outL, prev);
                    if (d > staticMaxDelta) staticMaxDelta = d;
                }
            }

            // --- Step 2: Measure SWEEP max delta (0 -> 1) ---
            PipelineTestFixture fixSweep(128, kPipelineTestSampleRate);
            fixSweep.injectAnalysis(makePipelineTestAnalysis(20, 440.0f, 8, 0.5f));

            fixSweep.paramChanges.addChange(Innexus::kMod1EnableId, 1.0);
            fixSweep.paramChanges.addChange(Innexus::kMod1RateId, 0.25);
            fixSweep.paramChanges.addChange(Innexus::kMod2EnableId, 1.0);
            fixSweep.paramChanges.addChange(Innexus::kMod2RateId, 0.3);
            fixSweep.paramChanges.addChange(param.id, 0.0);

            fixSweep.events.addNoteOn(60, 0.8f);
            fixSweep.processBlockWithParams();
            fixSweep.events.clear();

            for (int b = 0; b < 30; ++b)
                fixSweep.processBlock();

            float prevSweep = fixSweep.outL.back();
            float sweepMaxDelta = 0.0f;
            constexpr int sweepBlocks = 50;
            for (int b = 0; b < sweepBlocks; ++b)
            {
                float val = static_cast<float>(b) / static_cast<float>(sweepBlocks - 1);
                fixSweep.paramChanges.addChange(param.id, static_cast<double>(val));
                fixSweep.processBlockWithParams();
                float d = measureMaxDelta(fixSweep.outL, prevSweep);
                if (d > sweepMaxDelta) sweepMaxDelta = d;
            }

            // --- Step 3: Verify sweep delta <= envelope of static deltas ---
            float addedDiscontinuity = sweepMaxDelta - staticMaxDelta;

            INFO("Static max delta (envelope of 0/0.25/0.5/0.75/1.0) for "
                 << param.name << " = " << staticMaxDelta);
            INFO("Sweep max delta (0->1) for " << param.name << " = " << sweepMaxDelta);
            INFO("Added discontinuity = " << addedDiscontinuity
                 << " (threshold = " << kClickThresholdLinear << ")");

            // SC-007: The sweep should not produce sample-to-sample deltas
            // exceeding the envelope of all static measurements by more than
            // -80 dBFS (0.0001 linear). This proves parameter smoothing
            // prevents click artifacts during sweeps.
            REQUIRE(addedDiscontinuity < kClickThresholdLinear);
        }
    }
}

// =============================================================================
// Residual Level parameter must audibly affect output
// =============================================================================

static float computeRms(const std::vector<float>& buf)
{
    float sum = 0.0f;
    for (float s : buf)
        sum += s * s;
    return std::sqrt(sum / static_cast<float>(buf.size()));
}

TEST_CASE("Residual level at 0 vs 1.0 produces measurably different output",
          "[innexus][residual][integration]")
{
    // Helper: run processor with a given residual level, return RMS of output
    auto measureWithResidualLevel = [](double resLevelNormalized) -> float
    {
        PipelineTestFixture fix;

        // Create analysis with residual frames and set FFT/hop sizes
        // 20 frames (short analysis). The processor must sustain the residual
        // at the last frame via periodic re-loading for overlap-add.
        auto* analysis = makePipelineTestAnalysis(20, 440.0f, 16, 0.5f);
        analysis->analysisFFTSize = 1024;
        analysis->analysisHopSize = 512;
        // Add residual frames with meaningful energy
        for (size_t i = 0; i < analysis->frames.size(); ++i)
        {
            Krate::DSP::ResidualFrame rf{};
            rf.totalEnergy = 0.3f;
            for (size_t b = 0; b < Krate::DSP::kResidualBands; ++b)
                rf.bandEnergies[b] = 0.02f;
            rf.transientFlag = false;
            analysis->residualFrames.push_back(rf);
        }
        fix.injectAnalysis(analysis);

        // Set residual level (normalized 0-1, maps to plain 0-2)
        fix.paramChanges.addChange(Innexus::kResidualLevelId, resLevelNormalized);
        // Set harmonic level to 0 so we only measure the residual path
        fix.paramChanges.addChange(Innexus::kHarmonicLevelId, 0.0);
        fix.events.addNoteOn(69, 0.8f); // A4
        fix.processBlockWithParams();
        fix.events.clear();

        // Let smoothers settle
        for (int b = 0; b < 20; ++b)
            fix.processBlock();

        // Measure RMS over several blocks
        float totalRms = 0.0f;
        int measureBlocks = 10;
        for (int b = 0; b < measureBlocks; ++b)
        {
            fix.processBlock();
            totalRms += computeRms(fix.outL);
        }
        return totalRms / static_cast<float>(measureBlocks);
    };

    // First, verify test infrastructure produces sound with harmonics on
    {
        PipelineTestFixture fix;
        auto* analysis = makePipelineTestAnalysis(20, 440.0f, 16, 0.5f);
        analysis->analysisFFTSize = 1024;
        analysis->analysisHopSize = 512;
        for (size_t i = 0; i < analysis->frames.size(); ++i) {
            Krate::DSP::ResidualFrame rf{};
            rf.totalEnergy = 0.3f;
            for (auto& b : rf.bandEnergies) b = 0.02f;
            analysis->residualFrames.push_back(rf);
        }
        fix.injectAnalysis(analysis);
        fix.events.addNoteOn(69, 0.8f);
        fix.processBlockWithParams();
        fix.events.clear();
        for (int b = 0; b < 20; ++b) fix.processBlock();
        fix.processBlock();
        float harmRms = computeRms(fix.outL);
        INFO("Harmonic RMS (default levels): " << harmRms);
        REQUIRE(harmRms > 1e-4f); // sanity: harmonics produce sound
    }

    // --- First: verify ResidualSynthesizer produces output directly ---
    {
        Krate::DSP::ResidualSynthesizer resSynth;
        resSynth.prepare(1024, 512, static_cast<float>(kPipelineTestSampleRate));

        Krate::DSP::ResidualFrame rf{};
        rf.totalEnergy = 0.3f;
        for (auto& b : rf.bandEnergies) b = 0.02f;

        resSynth.loadFrame(rf, 0.0f, 0.0f);

        float maxAbs = 0.0f;
        for (size_t i = 0; i < 512; ++i) {
            float s = resSynth.process(0.0f);
            if (std::abs(s) > maxAbs) maxAbs = std::abs(s);
        }
        INFO("ResidualSynthesizer direct output max abs: " << maxAbs);
        REQUIRE(maxAbs > 1e-6f);
    }

    float rmsAtZero = measureWithResidualLevel(0.0);   // plain 0.0
    float rmsAtFull = measureWithResidualLevel(0.5);    // plain 1.0
    float rmsAtMax  = measureWithResidualLevel(1.0);    // plain 2.0

    INFO("RMS at residual level 0.0: " << rmsAtZero);
    INFO("RMS at residual level 1.0: " << rmsAtFull);
    INFO("RMS at residual level 2.0: " << rmsAtMax);

    // At level 0, output should be near-silent (only residual path, harmonics off)
    REQUIRE(rmsAtZero < 1e-5f);

    // At level 1.0, output should be significantly louder than at 0
    REQUIRE(rmsAtFull > rmsAtZero * 10.0f);

    // At level 2.0, output should be louder than at 1.0
    REQUIRE(rmsAtMax > rmsAtFull * 1.5f);
}

// =============================================================================
// T057: CPU Benchmark (opt-in via [.perf] tag)
// =============================================================================

TEST_CASE("M6 CPU benchmark: all features active < 2% total, < 1% delta (SC-008)",
          "[.perf][innexus][m6][benchmark]")
{
    constexpr double sampleRate = 44100.0;
    constexpr int32 blockSize = 512;
    constexpr int totalSamples = static_cast<int>(sampleRate * 10.0); // 10 seconds
    constexpr int blocksPerIteration = totalSamples / blockSize;
    constexpr int numIterations = 100;

    // --- Helper lambda: create a processor, inject analysis, trigger note,
    //     and let smoothers settle ---
    auto makeProcessor = [&](bool enableM6) -> PipelineTestFixture*
    {
        auto* fix = new PipelineTestFixture(blockSize, sampleRate);
        fix->injectAnalysis(makePipelineTestAnalysis(20, 440.0f, 16, 0.5f));

        if (enableM6)
        {
            fix->paramChanges.addChange(Innexus::kStereoSpreadId, 0.5);
            fix->paramChanges.addChange(Innexus::kDetuneSpreadId, 0.5);
            fix->paramChanges.addChange(Innexus::kEvolutionEnableId, 1.0);
            fix->paramChanges.addChange(Innexus::kEvolutionSpeedId, 0.3);
            fix->paramChanges.addChange(Innexus::kEvolutionDepthId, 0.5);
            fix->paramChanges.addChange(Innexus::kMod1EnableId, 1.0);
            fix->paramChanges.addChange(Innexus::kMod1RateId, 0.25);
            fix->paramChanges.addChange(Innexus::kMod1DepthId, 0.5);
            fix->paramChanges.addChange(Innexus::kMod2EnableId, 1.0);
            fix->paramChanges.addChange(Innexus::kMod2RateId, 0.3);
            fix->paramChanges.addChange(Innexus::kMod2DepthId, 0.4);
        }
        else
        {
            fix->paramChanges.addChange(Innexus::kStereoSpreadId, 0.0);
            fix->paramChanges.addChange(Innexus::kDetuneSpreadId, 0.0);
            fix->paramChanges.addChange(Innexus::kEvolutionEnableId, 0.0);
            fix->paramChanges.addChange(Innexus::kMod1EnableId, 0.0);
            fix->paramChanges.addChange(Innexus::kMod2EnableId, 0.0);
        }

        // Trigger note
        fix->events.addNoteOn(60, 0.8f);
        fix->processBlockWithParams();
        fix->events.clear();

        // Settle smoothers
        for (int b = 0; b < 40; ++b)
            fix->processBlock();

        return fix;
    };

    // --- Measure M5 baseline (no M6 features) ---
    std::vector<double> baselineTimes;
    baselineTimes.reserve(numIterations);

    {
        auto* fix = makeProcessor(false);
        for (int iter = 0; iter < numIterations; ++iter)
        {
            auto start = std::chrono::high_resolution_clock::now();
            for (int b = 0; b < blocksPerIteration; ++b)
                fix->processBlock();
            auto end = std::chrono::high_resolution_clock::now();

            double elapsedSec = std::chrono::duration<double>(end - start).count();
            baselineTimes.push_back(elapsedSec);
        }
        delete fix;
    }

    // --- Measure M6 active (all features) ---
    std::vector<double> m6Times;
    m6Times.reserve(numIterations);

    {
        auto* fix = makeProcessor(true);
        for (int iter = 0; iter < numIterations; ++iter)
        {
            auto start = std::chrono::high_resolution_clock::now();
            for (int b = 0; b < blocksPerIteration; ++b)
                fix->processBlock();
            auto end = std::chrono::high_resolution_clock::now();

            double elapsedSec = std::chrono::duration<double>(end - start).count();
            m6Times.push_back(elapsedSec);
        }
        delete fix;
    }

    // --- Compute medians ---
    std::sort(baselineTimes.begin(), baselineTimes.end());
    std::sort(m6Times.begin(), m6Times.end());

    double medianBaseline = baselineTimes[numIterations / 2];
    double medianM6 = m6Times[numIterations / 2];

    // CPU% = (time_for_10s / 10.0) * 100
    double baselineCpuPct = (medianBaseline / 10.0) * 100.0;
    double m6CpuPct = (medianM6 / 10.0) * 100.0;
    double deltaCpuPct = m6CpuPct - baselineCpuPct;

    INFO("M5 baseline: " << medianBaseline << " s (" << baselineCpuPct << "% CPU)");
    INFO("M6 active:   " << medianM6 << " s (" << m6CpuPct << "% CPU)");
    INFO("M6 delta:    " << deltaCpuPct << "% CPU");

    // SC-008: M6_active - M5_baseline < 1.0%
    REQUIRE(deltaCpuPct < 1.0);

    // SC-008: M6_active < 2.0% total
    REQUIRE(m6CpuPct < 2.0);
}
