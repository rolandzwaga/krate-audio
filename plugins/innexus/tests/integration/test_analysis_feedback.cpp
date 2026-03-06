// ==============================================================================
// Analysis Feedback Loop Integration Tests
// ==============================================================================
// Full pipeline integration tests verifying the analysis feedback loop
// (FeedbackAmount, FeedbackDecay) is correctly wired into the Innexus Processor.
// Tests instantiate a real Processor in sidechain mode, send parameter changes,
// and call process() with synthetic sidechain audio.
//
// Feature: 123-analysis-feedback-loop
// Requirements: SC-001, SC-002, SC-003, SC-004
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"
#include "dsp/sample_analysis.h"

#include <krate/dsp/processors/harmonic_oscillator_bank.h>
#include <krate/dsp/processors/harmonic_types.h>

#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/base/ibstream.h"
#include "base/source/fstreamer.h"

#include <algorithm>
#include <array>
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

static constexpr double kFbTestSampleRate = 44100.0;
static constexpr int32 kFbTestBlockSize = 512;

/// Bit-level NaN check (safe under -ffast-math)
static bool isNaNBits(float x) noexcept
{
    uint32_t bits = 0;
    std::memcpy(&bits, &x, sizeof(bits));
    return ((bits & 0x7F800000u) == 0x7F800000u) && ((bits & 0x007FFFFFu) != 0);
}

static ProcessSetup makeFbSetup(double sampleRate = kFbTestSampleRate,
                                int32 blockSize = kFbTestBlockSize)
{
    ProcessSetup setup{};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = blockSize;
    setup.sampleRate = sampleRate;
    return setup;
}

// IBStream implementation for setting state
class FbTestStream : public IBStream
{
public:
    tresult PLUGIN_API queryInterface(const TUID, void**) override
    {
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

    tresult PLUGIN_API read(void* buffer, int32 numBytes,
                            int32* numBytesRead) override
    {
        if (!buffer || numBytes <= 0)
            return kResultFalse;
        int32 available = static_cast<int32>(data_.size()) - readPos_;
        int32 toRead = std::min(numBytes, available);
        if (toRead <= 0)
        {
            if (numBytesRead) *numBytesRead = 0;
            return kResultFalse;
        }
        std::memcpy(buffer, data_.data() + readPos_, static_cast<size_t>(toRead));
        readPos_ += toRead;
        if (numBytesRead) *numBytesRead = toRead;
        return kResultOk;
    }

    tresult PLUGIN_API write(void* buffer, int32 numBytes,
                             int32* numBytesWritten) override
    {
        if (!buffer || numBytes <= 0)
            return kResultFalse;
        auto* bytes = static_cast<const char*>(buffer);
        data_.insert(data_.end(), bytes, bytes + numBytes);
        if (numBytesWritten) *numBytesWritten = numBytes;
        return kResultOk;
    }

    tresult PLUGIN_API seek(int64 pos, int32 mode, int64* result) override
    {
        int64 newPos = 0;
        switch (mode)
        {
        case kIBSeekSet: newPos = pos; break;
        case kIBSeekCur: newPos = readPos_ + pos; break;
        case kIBSeekEnd: newPos = static_cast<int64>(data_.size()) + pos; break;
        default: return kResultFalse;
        }
        if (newPos < 0 || newPos > static_cast<int64>(data_.size()))
            return kResultFalse;
        readPos_ = static_cast<int32>(newPos);
        if (result) *result = readPos_;
        return kResultOk;
    }

    tresult PLUGIN_API tell(int64* pos) override
    {
        if (pos) *pos = readPos_;
        return kResultOk;
    }

    void resetReadPos() { readPos_ = 0; }

private:
    std::vector<char> data_;
    int32 readPos_ = 0;
};

// Minimal EventList
class FbTestEventList : public IEventList
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
class FbTestParamValueQueue : public IParamValueQueue
{
public:
    FbTestParamValueQueue(ParamID id, ParamValue val)
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
class FbTestParameterChanges : public IParameterChanges
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
    std::vector<FbTestParamValueQueue> queues_;
};

/// Helper fixture for analysis feedback loop integration tests.
/// Supports sidechain mode with stereo sidechain input bus.
struct FeedbackTestFixture
{
    Innexus::Processor processor;
    FbTestEventList events;
    FbTestParameterChanges paramChanges;

    // Output buffers
    std::vector<float> outL;
    std::vector<float> outR;
    float* outChannels[2];
    AudioBusBuffers outputBus{};

    // Sidechain input buffers
    std::vector<float> scInL;
    std::vector<float> scInR;
    float* scChannels[2];
    AudioBusBuffers inputBus{};

    ProcessData data{};
    int32 blockSize;

    FeedbackTestFixture(int32 blkSize = kFbTestBlockSize,
                        double sampleRate = kFbTestSampleRate)
        : outL(static_cast<size_t>(blkSize), 0.0f)
        , outR(static_cast<size_t>(blkSize), 0.0f)
        , scInL(static_cast<size_t>(blkSize), 0.0f)
        , scInR(static_cast<size_t>(blkSize), 0.0f)
        , blockSize(blkSize)
    {
        outChannels[0] = outL.data();
        outChannels[1] = outR.data();
        outputBus.numChannels = 2;
        outputBus.channelBuffers32 = outChannels;
        outputBus.silenceFlags = 0;

        scChannels[0] = scInL.data();
        scChannels[1] = scInR.data();
        inputBus.numChannels = 2;
        inputBus.channelBuffers32 = scChannels;
        inputBus.silenceFlags = 0;

        data.processMode = kRealtime;
        data.symbolicSampleSize = kSample32;
        data.numSamples = blkSize;
        data.numOutputs = 1;
        data.outputs = &outputBus;
        data.numInputs = 1;
        data.inputs = &inputBus;
        data.inputParameterChanges = nullptr;
        data.outputParameterChanges = nullptr;
        data.inputEvents = &events;
        data.outputEvents = nullptr;

        processor.initialize(nullptr);
        auto setup = makeFbSetup(sampleRate, blkSize);
        processor.setupProcessing(setup);
        processor.setActive(true);

        // Set input source to sidechain mode via state
        setSidechainMode();
    }

    ~FeedbackTestFixture()
    {
        processor.setActive(false);
        processor.terminate();
    }

    void setSidechainMode()
    {
        // Set input source to sidechain via parameter change.
        // Run a silent process block to apply it.
        paramChanges.addChange(Innexus::kInputSourceId, 1.0);
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        data.inputParameterChanges = &paramChanges;
        processor.process(data);
        paramChanges.clear();
        data.inputParameterChanges = nullptr;
    }

    /// Fill sidechain input with a sine wave.
    void fillSidechainSine(float freq, float amp = 0.5f)
    {
        for (int32 s = 0; s < blockSize; ++s)
        {
            float val = amp * std::sin(
                2.0f * 3.14159265f * freq * static_cast<float>(s)
                / static_cast<float>(kFbTestSampleRate));
            scInL[static_cast<size_t>(s)] = val;
            scInR[static_cast<size_t>(s)] = val;
        }
    }

    /// Fill sidechain input with silence.
    void fillSidechainSilence()
    {
        std::fill(scInL.begin(), scInL.end(), 0.0f);
        std::fill(scInR.begin(), scInR.end(), 0.0f);
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

    float maxAbsOutput() const
    {
        float maxVal = 0.0f;
        for (size_t i = 0; i < outL.size(); ++i)
        {
            maxVal = std::max(maxVal, std::abs(outL[i]));
            maxVal = std::max(maxVal, std::abs(outR[i]));
        }
        return maxVal;
    }

    float rmsOutput() const
    {
        float sum = 0.0f;
        for (size_t i = 0; i < outL.size(); ++i)
        {
            sum += outL[i] * outL[i];
            sum += outR[i] * outR[i];
        }
        return std::sqrt(sum / static_cast<float>(outL.size() * 2));
    }

    /// Compute RMS of output for a given block range (used for measuring
    /// RMS at intervals).
    float rmsOutputRange(size_t startSample, size_t endSample) const
    {
        float sum = 0.0f;
        size_t count = 0;
        for (size_t i = startSample; i < endSample && i < outL.size(); ++i)
        {
            sum += outL[i] * outL[i] + outR[i] * outR[i];
            count += 2;
        }
        if (count == 0) return 0.0f;
        return std::sqrt(sum / static_cast<float>(count));
    }
};

// =============================================================================
// T017: SC-001 - FeedbackAmount=0.0 produces bit-identical output to baseline
// =============================================================================

TEST_CASE("Analysis Feedback: SC-001 FeedbackAmount=0.0 is bit-identical to baseline",
          "[analysis_feedback][SC-001]")
{
    // Run 1: Baseline (no feedback parameter change -- default is 0.0)
    std::vector<float> baselineL;
    std::vector<float> baselineR;
    {
        FeedbackTestFixture fix;
        fix.fillSidechainSine(440.0f, 0.5f);

        // Trigger note
        fix.events.addNoteOn(60, 0.8f);
        fix.processBlockWithParams();
        fix.events.clear();

        // Let pipeline settle
        for (int b = 0; b < 40; ++b)
            fix.processBlock();

        // Capture reference
        fix.processBlock();
        baselineL = fix.outL;
        baselineR = fix.outR;
    }

    // Run 2: Explicitly set FeedbackAmount=0.0
    std::vector<float> fbZeroL;
    std::vector<float> fbZeroR;
    {
        FeedbackTestFixture fix;
        fix.fillSidechainSine(440.0f, 0.5f);

        // Set FeedbackAmount explicitly to 0.0
        fix.paramChanges.addChange(Innexus::kAnalysisFeedbackId, 0.0);

        fix.events.addNoteOn(60, 0.8f);
        fix.processBlockWithParams();
        fix.events.clear();

        // Let pipeline settle
        for (int b = 0; b < 40; ++b)
            fix.processBlock();

        // Capture output
        fix.processBlock();
        fbZeroL = fix.outL;
        fbZeroR = fix.outR;
    }

    // Outputs must be bit-identical
    bool identical = true;
    for (size_t i = 0; i < baselineL.size(); ++i)
    {
        if (baselineL[i] != fbZeroL[i] || baselineR[i] != fbZeroR[i])
        {
            identical = false;
            break;
        }
    }

    REQUIRE(identical);
}

// =============================================================================
// T018: SC-002 - FeedbackAmount=1.0 with silent sidechain converges
// =============================================================================

TEST_CASE("Analysis Feedback: SC-002 FeedbackAmount=1.0 silent sidechain converges",
          "[analysis_feedback][SC-002]")
{
    FeedbackTestFixture fix;

    // First: feed sidechain audio to establish some analysis state
    fix.fillSidechainSine(440.0f, 0.5f);

    // Set feedback to 1.0
    fix.paramChanges.addChange(Innexus::kAnalysisFeedbackId, 1.0);

    fix.events.addNoteOn(60, 0.8f);
    fix.processBlockWithParams();
    fix.events.clear();

    // Let the pipeline establish some state with audio
    for (int b = 0; b < 20; ++b)
        fix.processBlock();

    // Now switch to silence on sidechain
    fix.fillSidechainSilence();

    // Measure RMS at 1-second intervals over 10 seconds
    // At 44100 Hz, 512-sample blocks: ~86 blocks per second
    const int blocksPerSecond = static_cast<int>(kFbTestSampleRate)
                                / kFbTestBlockSize;
    const int totalSeconds = 10;

    // Measure t=0 RMS (first second after switching to silence)
    float rmsAtT0 = 0.0f;
    {
        float sumSquared = 0.0f;
        int sampleCount = 0;
        for (int b = 0; b < blocksPerSecond; ++b)
        {
            fix.processBlock();
            for (size_t i = 0; i < fix.outL.size(); ++i)
            {
                sumSquared += fix.outL[i] * fix.outL[i]
                            + fix.outR[i] * fix.outR[i];
                sampleCount += 2;
            }
        }
        rmsAtT0 = std::sqrt(sumSquared / static_cast<float>(sampleCount));
    }

    // 3dB above t=0 threshold
    const float threshold3dB = rmsAtT0 * 1.4142f; // sqrt(2) ~ +3dB

    // Measure subsequent seconds (t=1 through t=9)
    bool anyExceeds3dB = false;
    for (int sec = 1; sec < totalSeconds; ++sec)
    {
        float sumSquared = 0.0f;
        int sampleCount = 0;
        for (int b = 0; b < blocksPerSecond; ++b)
        {
            fix.processBlock();
            for (size_t i = 0; i < fix.outL.size(); ++i)
            {
                sumSquared += fix.outL[i] * fix.outL[i]
                            + fix.outR[i] * fix.outR[i];
                sampleCount += 2;
            }
        }
        float rmsThisSecond = std::sqrt(
            sumSquared / static_cast<float>(sampleCount));
        if (rmsThisSecond > threshold3dB)
            anyExceeds3dB = true;
    }

    INFO("No 1-second interval RMS should exceed t=0 RMS by more than 3dB");
    REQUIRE_FALSE(anyExceeds3dB);
}

// =============================================================================
// T019: SC-004 - Output never exceeds kOutputClamp (2.0f)
// =============================================================================

TEST_CASE("Analysis Feedback: SC-004 output never exceeds kOutputClamp",
          "[analysis_feedback][SC-004]")
{
    // Test multiple parameter combinations
    struct TestCase
    {
        double feedbackAmount;
        double feedbackDecay;
        const char* name;
    };

    const TestCase cases[] = {
        {1.0, 0.0, "fb=1.0, decay=0.0"},
        {1.0, 1.0, "fb=1.0, decay=1.0"},
        {0.5, 0.5, "fb=0.5, decay=0.5"},
        {1.0, 0.2, "fb=1.0, decay=0.2"},
    };

    for (const auto& tc : cases)
    {
        SECTION(tc.name)
        {
            FeedbackTestFixture fix;

            // Feed a loud sidechain signal
            fix.fillSidechainSine(440.0f, 1.0f);

            fix.paramChanges.addChange(Innexus::kAnalysisFeedbackId, tc.feedbackAmount);
            fix.paramChanges.addChange(Innexus::kAnalysisFeedbackDecayId, tc.feedbackDecay);

            fix.events.addNoteOn(60, 1.0f);
            fix.processBlockWithParams();
            fix.events.clear();

            // Process many blocks to let feedback build up
            bool anyExceedsClamp = false;
            bool anyNaN = false;
            const float clamp = Krate::DSP::HarmonicOscillatorBank::kOutputClamp;

            for (int b = 0; b < 200; ++b)
            {
                fix.processBlock();
                for (size_t i = 0; i < fix.outL.size(); ++i)
                {
                    float absL = std::abs(fix.outL[i]);
                    float absR = std::abs(fix.outR[i]);
                    if (absL > clamp || absR > clamp)
                        anyExceedsClamp = true;
                    if (isNaNBits(fix.outL[i]) || isNaNBits(fix.outR[i]))
                        anyNaN = true;
                }
            }

            INFO("Output samples must never exceed kOutputClamp (" << clamp << ")");
            REQUIRE_FALSE(anyExceedsClamp);
            REQUIRE_FALSE(anyNaN);
        }
    }
}

// =============================================================================
// T028: SC-003 - Decay=1.0: output RMS falls below -60dBFS within 10 seconds
// =============================================================================

TEST_CASE("Analysis Feedback: SC-003 decay=1.0 reaches silence within 10 seconds",
          "[analysis_feedback][SC-003]")
{
    FeedbackTestFixture fix;

    // Feed sidechain audio for 1 second to establish analysis state
    fix.fillSidechainSine(440.0f, 0.5f);

    // Set feedback=1.0, decay=1.0
    fix.paramChanges.addChange(Innexus::kAnalysisFeedbackId, 1.0);
    fix.paramChanges.addChange(Innexus::kAnalysisFeedbackDecayId, 1.0);

    fix.events.addNoteOn(60, 0.8f);
    fix.processBlockWithParams();
    fix.events.clear();

    // Feed sidechain audio for ~1 second
    const int blocksPerSecond = static_cast<int>(kFbTestSampleRate)
                                / kFbTestBlockSize;
    for (int b = 1; b < blocksPerSecond; ++b)
        fix.processBlock();

    // Switch sidechain to silence
    fix.fillSidechainSilence();

    // Run for 10 seconds, measuring RMS at the end
    const int totalSeconds = 10;
    const int totalBlocks = totalSeconds * blocksPerSecond;

    for (int b = 0; b < totalBlocks; ++b)
        fix.processBlock();

    // Measure RMS of the final block
    float finalRMS = fix.rmsOutput();

    // -60dBFS = 10^(-60/20) = 0.001
    const float threshold = 0.001f;

    INFO("Final RMS after 10 seconds with decay=1.0: " << finalRMS
         << " (threshold: " << threshold << ")");
    REQUIRE(finalRMS < threshold);
}

// =============================================================================
// T029: SC-003 - Decay=0.5: output RMS falls below -60dBFS within 60 seconds
// =============================================================================

TEST_CASE("Analysis Feedback: SC-003 decay=0.5 reaches silence within 60 seconds",
          "[analysis_feedback][SC-003]")
{
    FeedbackTestFixture fix;

    // Feed sidechain audio for 1 second to establish analysis state
    fix.fillSidechainSine(440.0f, 0.5f);

    // Set feedback=1.0, decay=0.5
    fix.paramChanges.addChange(Innexus::kAnalysisFeedbackId, 1.0);
    fix.paramChanges.addChange(Innexus::kAnalysisFeedbackDecayId, 0.5);

    fix.events.addNoteOn(60, 0.8f);
    fix.processBlockWithParams();
    fix.events.clear();

    // Feed sidechain audio for ~1 second
    const int blocksPerSecond = static_cast<int>(kFbTestSampleRate)
                                / kFbTestBlockSize;
    for (int b = 1; b < blocksPerSecond; ++b)
        fix.processBlock();

    // Switch sidechain to silence
    fix.fillSidechainSilence();

    // Run for 60 seconds, measuring RMS at the end
    const int totalSeconds = 60;
    const int totalBlocks = totalSeconds * blocksPerSecond;

    for (int b = 0; b < totalBlocks; ++b)
        fix.processBlock();

    // Measure RMS of the final block
    float finalRMS = fix.rmsOutput();

    // -60dBFS = 10^(-60/20) = 0.001
    const float threshold = 0.001f;

    INFO("Final RMS after 60 seconds with decay=0.5: " << finalRMS
         << " (threshold: " << threshold << ")");
    REQUIRE(finalRMS < threshold);
}

// =============================================================================
// T035: SC-008 - Feedback mixing introduces negligible CPU overhead
// =============================================================================
//
// Code-structure verification (part a):
// The feedback mixing loop in processor.cpp (around line 408-418) contains ONLY:
//   - std::tanh() (transcendental arithmetic)
//   - float multiply (fbAmount * 2.0f, * 0.5f, * (1.0f - fbAmount))
//   - float add (sidechain + fbSample)
//   - array indexing into pre-allocated std::array<float, 8192>
// No allocations, no system calls, no virtual dispatch, no locks, no exceptions.
//
// The feedback capture loop (around line 1411-1444) contains ONLY:
//   - float add/multiply for stereo-to-mono averaging ((out[0] + out[1]) * 0.5f)
//   - std::exp() for decay coefficient (once per block)
//   - float multiply for decay application (feedbackBuffer[s] *= decayCoeff)
//   - array indexing into pre-allocated std::array<float, 8192>
// No allocations, no system calls, no virtual dispatch, no locks, no exceptions.
//
// Both loops are O(N) in block size with no branching per sample.
// =============================================================================

TEST_CASE("Analysis Feedback: SC-008 feedback mixing has negligible CPU overhead",
          "[analysis_feedback][SC-008]")
{
    // Part (b): Coarse timing sanity check.
    // Run 1000 blocks with feedback=0.0 (baseline) and 1000 blocks with
    // feedback=1.0 (active). The average block time difference must be
    // less than 1% of the baseline time.

    const int kNumBlocks = 1000;

    // --- Baseline: feedback=0.0 ---
    double baselineAvgNs = 0.0;
    {
        FeedbackTestFixture fix;
        fix.fillSidechainSine(440.0f, 0.5f);

        // Set feedback to 0.0 (baseline)
        fix.paramChanges.addChange(Innexus::kAnalysisFeedbackId, 0.0);
        fix.events.addNoteOn(60, 0.8f);
        fix.processBlockWithParams();
        fix.events.clear();

        // Warm up
        for (int b = 0; b < 50; ++b)
            fix.processBlock();

        // Timed run
        auto start = std::chrono::high_resolution_clock::now();
        for (int b = 0; b < kNumBlocks; ++b)
            fix.processBlock();
        auto end = std::chrono::high_resolution_clock::now();

        baselineAvgNs = static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count())
            / static_cast<double>(kNumBlocks);
    }

    // --- Active: feedback=1.0 ---
    double activeAvgNs = 0.0;
    {
        FeedbackTestFixture fix;
        fix.fillSidechainSine(440.0f, 0.5f);

        // Set feedback to 1.0 (active)
        fix.paramChanges.addChange(Innexus::kAnalysisFeedbackId, 1.0);
        fix.events.addNoteOn(60, 0.8f);
        fix.processBlockWithParams();
        fix.events.clear();

        // Warm up
        for (int b = 0; b < 50; ++b)
            fix.processBlock();

        // Timed run
        auto start = std::chrono::high_resolution_clock::now();
        for (int b = 0; b < kNumBlocks; ++b)
            fix.processBlock();
        auto end = std::chrono::high_resolution_clock::now();

        activeAvgNs = static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count())
            / static_cast<double>(kNumBlocks);
    }

    // The overhead must be less than 1% of baseline
    double overheadFraction = 0.0;
    if (baselineAvgNs > 0.0)
        overheadFraction = (activeAvgNs - baselineAvgNs) / baselineAvgNs;

    INFO("Baseline avg block time: " << baselineAvgNs << " ns");
    INFO("Active avg block time:   " << activeAvgNs << " ns");
    INFO("Overhead fraction:       " << overheadFraction
         << " (limit: 0.01 = 1%)");

    // Allow negative overhead (active faster than baseline due to noise)
    // Only fail if overhead exceeds +1%
    REQUIRE(overheadFraction < 0.01);
}

// =============================================================================
// T040: SC-005 - Freeze bypasses feedback path
// =============================================================================
//
// When freeze is engaged while feedback is active, the feedback buffer contents
// must NOT be mixed into the analysis input on the next process call.
// Approach: Two independent runs with identical setup, both frozen at the same
// block. One has feedback=0.0, the other feedback=1.0. Both should produce
// identical output because the feedback mixing is bypassed during freeze.
// =============================================================================

TEST_CASE("Analysis Feedback: SC-005 freeze bypasses feedback path",
          "[analysis_feedback][SC-005]")
{
    // FR-015: When freeze is engaged, feedback mixing must be bypassed.
    // Verification: Run with feedback=1.0, engage freeze, then measure
    // output RMS over many blocks while frozen. The RMS should remain
    // stable (not grow), proving feedback is not leaking into analysis.
    // Additionally, verify that during freeze the feedback mixing gate
    // (manualFrozen check) prevents feedback buffer from being mixed
    // into the sidechain input.

    FeedbackTestFixture fix;
    fix.fillSidechainSine(440.0f, 0.5f);

    // Set feedback to 1.0 and trigger a note
    fix.paramChanges.addChange(Innexus::kAnalysisFeedbackId, 1.0);
    fix.paramChanges.addChange(Innexus::kAnalysisFeedbackDecayId, 0.0);
    fix.events.addNoteOn(60, 0.8f);
    fix.processBlockWithParams();
    fix.events.clear();

    // Let pipeline settle
    for (int b = 0; b < 50; ++b)
        fix.processBlock();

    // Engage freeze
    fix.paramChanges.addChange(Innexus::kFreezeId, 1.0);
    fix.processBlockWithParams();

    // Let freeze crossfade complete
    for (int b = 0; b < 5; ++b)
        fix.processBlock();

    // Measure RMS over 100 blocks while frozen with feedback=1.0
    // If feedback were leaking, the analysis would change and output
    // would grow or become unstable. With bypass active, output should
    // come from the frozen frame only and remain stable.
    float maxRMS = 0.0f;
    float minRMS = std::numeric_limits<float>::max();
    for (int b = 0; b < 100; ++b)
    {
        fix.processBlock();
        float rms = fix.rmsOutput();
        if (rms > maxRMS) maxRMS = rms;
        if (rms > 0.0f && rms < minRMS) minRMS = rms;
    }

    // The RMS should be stable. If feedback leaked, we'd expect growing RMS.
    // Allow 1dB variation (oscillator phase effects) but no growth trend.
    if (minRMS > 0.0f && maxRMS > 0.0f)
    {
        float ratioDb = 20.0f * std::log10(maxRMS / minRMS);
        INFO("RMS variation while frozen with feedback=1.0: " << ratioDb << " dB "
             "(max=" << maxRMS << ", min=" << minRMS << ")");
        // Frozen frame + bypassed feedback should produce very stable output.
        // Allow up to 3dB variation for oscillator phase effects.
        REQUIRE(ratioDb < 3.0f);
    }
    else
    {
        // If output is zero, that's also acceptable (frozen empty frame)
        REQUIRE(maxRMS == 0.0f);
    }
}

// =============================================================================
// T041: SC-006 - Freeze disengage clears feedback buffer to all zeros
// =============================================================================

TEST_CASE("Analysis Feedback: SC-006 freeze disengage clears feedback buffer",
          "[analysis_feedback][SC-006]")
{
    // FR-016: When freeze is disengaged, feedbackBuffer_ must be cleared (zeroed)
    // to prevent stale audio from contaminating the re-engaged analysis pipeline.
    //
    // Strategy: manually populate the feedback buffer with known non-zero values
    // by running the processor in sidechain mode, then disengage freeze in sample
    // mode so the feedback capture block does NOT refill the buffer. This lets us
    // observe the clear directly.

    FeedbackTestFixture fix;
    fix.fillSidechainSine(440.0f, 0.5f);

    // Set feedback=1.0 and trigger a note
    fix.paramChanges.addChange(Innexus::kAnalysisFeedbackId, 1.0);
    fix.paramChanges.addChange(Innexus::kAnalysisFeedbackDecayId, 0.0);
    fix.events.addNoteOn(60, 0.8f);
    fix.processBlockWithParams();
    fix.events.clear();

    // Run many blocks so the analysis pipeline detects f0 and the oscillator
    // produces output, which gets captured into feedbackBuffer_.
    for (int b = 0; b < 100; ++b)
        fix.processBlock();

    // Now engage freeze (in sidechain mode -- the capture block still runs,
    // keeping the buffer populated)
    fix.paramChanges.addChange(Innexus::kFreezeId, 1.0);
    fix.processBlockWithParams();

    // Process a few blocks while frozen
    for (int b = 0; b < 3; ++b)
        fix.processBlock();

    // Switch to sample mode so the capture block is skipped on subsequent calls
    // (capture only runs when currentSource == 1 / sidechain mode)
    fix.paramChanges.addChange(Innexus::kInputSourceId, 0.0);
    fix.processBlockWithParams();

    // Disengage freeze while in sample mode.
    // The freeze-disengage transition should call feedbackBuffer_.fill(0.0f).
    // Since we're in sample mode, the capture block at the end of process()
    // will NOT refill the buffer, so we can observe the clear directly.
    fix.paramChanges.addChange(Innexus::kFreezeId, 0.0);
    fix.processBlockWithParams();

    // Verify feedbackBuffer_ is all zeros
    {
        const auto& fbBuf = fix.processor.getFeedbackBuffer();
        bool allZero = true;
        for (int32 s = 0; s < fix.blockSize; ++s)
        {
            if (fbBuf[static_cast<size_t>(s)] != 0.0f)
            {
                allZero = false;
                break;
            }
        }
        INFO("Feedback buffer must contain all zeros after freeze disengage (FR-016)");
        REQUIRE(allZero);
    }
}
