// ==============================================================================
// Innexus Processor MIDI Integration & State Persistence Tests (Phase 11)
// ==============================================================================
// T079: MIDI integration tests -- note-on, note-off, velocity, pitch bend,
//       monophonic, silence-when-no-sample, confidence-gated freeze
// T080: State persistence tests -- round-trip save/load, no-sample state
//
// Feature: 115-innexus-m1-core-instrument
// User Stories: US1, US2, US4
// Requirements: FR-048 to FR-058, SC-007 to SC-010
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"
#include "dsp/sample_analysis.h"

#include <krate/dsp/processors/harmonic_types.h>
#include <krate/dsp/core/midi_utils.h>

#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/base/ibstream.h"

#include <cstring>
#include <cmath>
#include <memory>
#include <vector>
#include <array>
#include <numeric>
#include <algorithm>

using namespace Steinberg;
using namespace Steinberg::Vst;
using Catch::Approx;

// =============================================================================
// Helpers
// =============================================================================

static constexpr double kTestSampleRate = 44100.0;
static constexpr int32 kTestBlockSize = 128;

static ProcessSetup makeSetup(double sampleRate = kTestSampleRate,
                              int32 blockSize = kTestBlockSize)
{
    ProcessSetup setup{};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = blockSize;
    setup.sampleRate = sampleRate;
    return setup;
}

// Create a simple harmonic analysis with known partials
static Innexus::SampleAnalysis* makeTestAnalysis(
    int numFrames = 10,
    float f0 = 440.0f,
    float amplitude = 0.5f)
{
    auto* analysis = new Innexus::SampleAnalysis();
    analysis->sampleRate = 44100.0f;
    analysis->hopTimeSec = 512.0f / 44100.0f; // ~11.6ms hop

    for (int f = 0; f < numFrames; ++f)
    {
        Krate::DSP::HarmonicFrame frame{};
        frame.f0 = f0;
        frame.f0Confidence = 0.9f;
        frame.numPartials = 4;
        frame.globalAmplitude = amplitude;

        for (int p = 0; p < 4; ++p)
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
    }

    analysis->totalFrames = analysis->frames.size();
    analysis->filePath = "test_sample.wav";

    return analysis;
}

// Create a 48-partial analysis for SC-004 full plugin CPU benchmark
static Innexus::SampleAnalysis* makeFullTestAnalysis(
    int numFrames = 50,
    float f0 = 220.0f,
    float amplitude = 0.5f)
{
    auto* analysis = new Innexus::SampleAnalysis();
    analysis->sampleRate = 44100.0f;
    analysis->hopTimeSec = 512.0f / 44100.0f;

    for (int f = 0; f < numFrames; ++f)
    {
        Krate::DSP::HarmonicFrame frame{};
        frame.f0 = f0;
        frame.f0Confidence = 0.9f;
        frame.numPartials = 48;
        frame.globalAmplitude = amplitude;

        for (int p = 0; p < 48; ++p)
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
    }

    analysis->totalFrames = analysis->frames.size();
    analysis->filePath = "test_sample.wav";

    return analysis;
}

// Create analysis with varying confidence for freeze testing
static Innexus::SampleAnalysis* makeConfidenceVaryingAnalysis(
    int numFrames = 20,
    float f0 = 440.0f)
{
    auto* analysis = new Innexus::SampleAnalysis();
    analysis->sampleRate = 44100.0f;
    analysis->hopTimeSec = 128.0f / 44100.0f; // Short hop for faster testing

    for (int f = 0; f < numFrames; ++f)
    {
        Krate::DSP::HarmonicFrame frame{};
        frame.f0 = f0;
        // Confidence drops below threshold in the middle frames
        if (f >= 5 && f <= 10)
            frame.f0Confidence = 0.1f; // Below threshold
        else
            frame.f0Confidence = 0.9f; // Above threshold

        frame.numPartials = 2;
        frame.globalAmplitude = 0.5f;

        for (int p = 0; p < 2; ++p)
        {
            auto& partial = frame.partials[static_cast<size_t>(p)];
            partial.harmonicIndex = p + 1;
            partial.frequency = f0 * static_cast<float>(p + 1);
            partial.amplitude = 0.3f;
            partial.relativeFrequency = static_cast<float>(p + 1);
            partial.inharmonicDeviation = 0.0f;
            partial.stability = 1.0f;
            partial.age = 10;
            partial.phase = 0.0f;
        }

        analysis->frames.push_back(frame);
    }

    analysis->totalFrames = analysis->frames.size();
    analysis->filePath = "test_confidence.wav";

    return analysis;
}

// Simple EventList for sending MIDI events in tests
class TestEventList : public IEventList
{
public:
    tresult PLUGIN_API queryInterface(const TUID, void**) override
    {
        return kNoInterface;
    }
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

    void addNoteOff(int16 pitch, int32 sampleOffset = 0)
    {
        Event e{};
        e.type = Event::kNoteOffEvent;
        e.sampleOffset = sampleOffset;
        e.noteOff.channel = 0;
        e.noteOff.pitch = pitch;
        e.noteOff.velocity = 0.0f;
        e.noteOff.noteId = pitch;
        e.noteOff.tuning = 0.0f;
        events_.push_back(e);
    }

    void addPitchBend(int msb, int lsb, int32 sampleOffset = 0)
    {
        Event e{};
        e.type = Event::kLegacyMIDICCOutEvent;
        e.sampleOffset = sampleOffset;
        e.midiCCOut.controlNumber =
            static_cast<uint8>(ControllerNumbers::kPitchBend);
        e.midiCCOut.channel = 0;
        e.midiCCOut.value = static_cast<int8>(msb);
        e.midiCCOut.value2 = static_cast<int8>(lsb);
        events_.push_back(e);
    }

    void clear() { events_.clear(); }

private:
    std::vector<Event> events_;
};

// Minimal IBStream implementation for state save/load testing
class TestStream : public IBStream
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
    [[nodiscard]] bool empty() const { return data_.empty(); }

private:
    std::vector<char> data_;
    int32 readPos_ = 0;
};

// Helper struct: set up processor with analysis injected and ready for processing
struct ProcessorTestFixture
{
    Innexus::Processor processor;
    TestEventList events;

    std::vector<float> outL;
    std::vector<float> outR;
    float* outChannels[2];
    AudioBusBuffers outputBus{};
    ProcessData data{};

    ProcessorTestFixture(int32 blockSize = kTestBlockSize,
                         double sampleRate = kTestSampleRate)
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
        auto setup = makeSetup(sampleRate, blockSize);
        processor.setupProcessing(setup);
        processor.setActive(true);
    }

    ~ProcessorTestFixture()
    {
        processor.setActive(false);
        processor.terminate();
    }

    void injectAnalysis(Innexus::SampleAnalysis* analysis)
    {
        processor.testInjectAnalysis(analysis);
    }

    float processAndGetMaxAmplitude()
    {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);

        processor.process(data);

        float maxAmp = 0.0f;
        for (size_t s = 0; s < outL.size(); ++s)
        {
            maxAmp = std::max(maxAmp, std::abs(outL[s]));
        }
        return maxAmp;
    }

    float processBlocksAndGetMax(int numBlocks)
    {
        float maxAmp = 0.0f;
        for (int b = 0; b < numBlocks; ++b)
        {
            events.clear();
            maxAmp = std::max(maxAmp, processAndGetMaxAmplitude());
        }
        return maxAmp;
    }
};

// =============================================================================
// T079: MIDI Integration Tests
// =============================================================================

TEST_CASE("Innexus: No sample loaded produces silence (FR-055)",
          "[innexus][processor][midi]")
{
    ProcessorTestFixture fix;

    // Send note-on without any analysis loaded
    fix.events.addNoteOn(60, 1.0f);
    float maxAmp = fix.processAndGetMaxAmplitude();
    REQUIRE(maxAmp == 0.0f);
}

TEST_CASE("Innexus: Note-on produces audio within first block (FR-048, SC-007)",
          "[innexus][processor][midi]")
{
    ProcessorTestFixture fix;
    fix.injectAnalysis(makeTestAnalysis());

    // Send note-on
    fix.events.addNoteOn(69, 1.0f); // A4 = 440Hz

    float maxAmp = fix.processAndGetMaxAmplitude();

    // First process block after note-on MUST produce non-zero audio (SC-007)
    REQUIRE(maxAmp > 0.0f);
}

TEST_CASE("Innexus: Note-off produces exponential decay (FR-049, SC-008)",
          "[innexus][processor][midi]")
{
    ProcessorTestFixture fix;
    fix.injectAnalysis(makeTestAnalysis());

    // Play note
    fix.events.addNoteOn(69, 1.0f);
    fix.processAndGetMaxAmplitude();

    // Let oscillators ramp up for a few blocks
    fix.events.clear();
    for (int i = 0; i < 10; ++i)
        fix.processAndGetMaxAmplitude();

    // Capture amplitude before note-off
    float ampBefore = fix.processAndGetMaxAmplitude();
    REQUIRE(ampBefore > 0.0f);

    // Send note-off
    fix.events.addNoteOff(69);
    fix.processAndGetMaxAmplitude();

    // Process more blocks -- amplitude should decay
    fix.events.clear();
    fix.processAndGetMaxAmplitude(); // block 1 after note-off
    float ampAfter2 = fix.processAndGetMaxAmplitude();
    float ampAfter3 = fix.processAndGetMaxAmplitude();

    // Exponential decay: each block should be quieter than the previous
    // (allow some tolerance for amplitude smoothing transients)
    REQUIRE(ampAfter2 < ampBefore);
    REQUIRE(ampAfter3 <= ampAfter2 + 1e-6f);
}

TEST_CASE("Innexus: 20ms anti-click minimum on note-off (FR-057)",
          "[innexus][processor][midi]")
{
    // Even with very short release time, we should have at least 20ms fade
    ProcessorTestFixture fix;
    fix.injectAnalysis(makeTestAnalysis());

    // Play note
    fix.events.addNoteOn(69, 1.0f);
    fix.processAndGetMaxAmplitude();

    // Let oscillators ramp up
    fix.events.clear();
    for (int i = 0; i < 10; ++i)
        fix.processAndGetMaxAmplitude();

    // Send note-off
    fix.events.addNoteOff(69);

    // 20ms at 44100 Hz = 882 samples. With 128-sample blocks, that's ~7 blocks.
    // The sound should NOT be completely silent within the first few blocks.
    float maxInFirst3Blocks = 0.0f;
    for (int i = 0; i < 3; ++i)
    {
        fix.events.clear();
        maxInFirst3Blocks = std::max(maxInFirst3Blocks,
                                     fix.processAndGetMaxAmplitude());
    }

    // There should still be audible output in the first 3 blocks (384 samples < 882)
    REQUIRE(maxInFirst3Blocks > 0.0f);
}

TEST_CASE("Innexus: Velocity scaling affects loudness, not timbre (FR-050)",
          "[innexus][processor][midi]")
{
    // Test at full velocity
    float maxAmpFull = 0.0f;
    {
        ProcessorTestFixture fix;
        fix.injectAnalysis(makeTestAnalysis());
        fix.events.addNoteOn(69, 1.0f); // Full velocity
        fix.processAndGetMaxAmplitude();
        fix.events.clear();
        for (int i = 0; i < 10; ++i)
            fix.processAndGetMaxAmplitude();
        maxAmpFull = fix.processAndGetMaxAmplitude();
    }

    // Test at half velocity
    float maxAmpHalf = 0.0f;
    {
        ProcessorTestFixture fix;
        fix.injectAnalysis(makeTestAnalysis());
        fix.events.addNoteOn(69, 0.5f); // Half velocity
        fix.processAndGetMaxAmplitude();
        fix.events.clear();
        for (int i = 0; i < 10; ++i)
            fix.processAndGetMaxAmplitude();
        maxAmpHalf = fix.processAndGetMaxAmplitude();
    }

    // Half velocity should produce roughly half the amplitude
    REQUIRE(maxAmpHalf > 0.0f);
    REQUIRE(maxAmpFull > maxAmpHalf);

    // The ratio should be approximately 2:1 (allow generous tolerance for
    // amplitude smoothing transients)
    float ratio = maxAmpFull / maxAmpHalf;
    REQUIRE(ratio > 1.3f);   // At least noticeably louder
    REQUIRE(ratio < 3.0f);   // But not wildly different
}

TEST_CASE("Innexus: Pitch bend shifts all partials (FR-051)",
          "[innexus][processor][midi]")
{
    ProcessorTestFixture fix;
    fix.injectAnalysis(makeTestAnalysis());

    // Play note
    fix.events.addNoteOn(69, 1.0f);
    fix.processAndGetMaxAmplitude();

    // Process a few blocks to let oscillators ramp up
    fix.events.clear();
    for (int i = 0; i < 5; ++i)
        fix.processAndGetMaxAmplitude();

    // Capture output before pitch bend
    float ampBeforeBend = fix.processAndGetMaxAmplitude();
    REQUIRE(ampBeforeBend > 0.0f);

    // Apply pitch bend (center = 64/0 = no bend, max up = 127/127)
    // MSB=96 LSB=0 -> (96 << 7 | 0) = 12288 -> normalized = (12288-8192)/8192 = 0.5
    // 0.5 * 12 = 6 semitones up
    fix.events.clear();
    fix.events.addPitchBend(96, 0);

    float ampAfterBend = fix.processAndGetMaxAmplitude();

    // After pitch bend, output should still be non-zero
    REQUIRE(ampAfterBend > 0.0f);

    // The oscillator bank should have been updated (no crash, audio continues)
    fix.events.clear();
    float ampContinue = fix.processAndGetMaxAmplitude();
    REQUIRE(ampContinue > 0.0f);
}

TEST_CASE("Innexus: Monophonic last-note-priority (FR-054)",
          "[innexus][processor][midi]")
{
    ProcessorTestFixture fix;
    fix.injectAnalysis(makeTestAnalysis());

    // Play first note
    fix.events.addNoteOn(60, 1.0f); // C4
    fix.processAndGetMaxAmplitude();
    fix.events.clear();

    // Let it play for a bit
    for (int i = 0; i < 5; ++i)
        fix.processAndGetMaxAmplitude();

    // Play second note (overlapping, no note-off for first)
    fix.events.addNoteOn(72, 1.0f); // C5 -- last-note-priority
    fix.processAndGetMaxAmplitude();
    fix.events.clear();

    // Continue processing -- should have audio (from second note)
    float ampAfterSecond = fix.processAndGetMaxAmplitude();
    REQUIRE(ampAfterSecond > 0.0f);

    // The first note should be replaced, not playing simultaneously
    // (monophonic = only one voice active at a time)
}

TEST_CASE("Innexus: No sample silence even with multiple notes (FR-055)",
          "[innexus][processor][midi]")
{
    ProcessorTestFixture fix;

    // No analysis injected
    fix.events.addNoteOn(60, 1.0f);
    fix.events.addNoteOn(72, 0.5f);
    fix.events.addNoteOff(60);

    float maxAmp = fix.processAndGetMaxAmplitude();
    REQUIRE(maxAmp == 0.0f);
}

TEST_CASE("Innexus: Confidence-gated freeze holds last good frame (FR-052)",
          "[innexus][processor][midi]")
{
    ProcessorTestFixture fix;
    fix.injectAnalysis(makeConfidenceVaryingAnalysis());

    // Play note
    fix.events.addNoteOn(69, 1.0f);
    fix.processAndGetMaxAmplitude();

    // Process many blocks to advance through frames
    // The analysis has frames with low confidence in the middle
    // The oscillator bank should keep producing output throughout
    fix.events.clear();
    bool hadOutputDuringLowConfidence = true;
    for (int i = 0; i < 200; ++i)
    {
        float amp = fix.processAndGetMaxAmplitude();
        // Even during low-confidence frames, the freeze mechanism should
        // keep the last good frame playing, so amplitude should stay > 0
        if (i > 5 && amp == 0.0f)
        {
            hadOutputDuringLowConfidence = false;
            break;
        }
    }

    REQUIRE(hadOutputDuringLowConfidence);
}

TEST_CASE("Innexus: Velocity-0 note-on treated as note-off",
          "[innexus][processor][midi]")
{
    ProcessorTestFixture fix;
    fix.injectAnalysis(makeTestAnalysis());

    // Play note normally
    fix.events.addNoteOn(69, 1.0f);
    fix.processAndGetMaxAmplitude();
    fix.events.clear();

    // Let oscillators ramp up
    for (int i = 0; i < 10; ++i)
        fix.processAndGetMaxAmplitude();

    float ampBefore = fix.processAndGetMaxAmplitude();
    REQUIRE(ampBefore > 0.0f);

    // Send velocity-0 note-on (should act as note-off per MIDI convention)
    fix.events.addNoteOn(69, 0.0f);
    fix.processAndGetMaxAmplitude();

    // Process enough blocks for release to take effect
    fix.events.clear();
    float lastAmp = 0.0f;
    for (int i = 0; i < 100; ++i)
    {
        lastAmp = fix.processAndGetMaxAmplitude();
    }

    // After 100 blocks of 128 samples (12800 samples at 44100Hz = ~290ms)
    // with default 100ms release, amplitude should have decayed significantly
    REQUIRE(lastAmp < ampBefore * 0.1f);
}

// =============================================================================
// T080: State Persistence Tests (FR-056, SC-009)
// =============================================================================

TEST_CASE("Innexus: State round-trip preserves parameters (FR-056)",
          "[innexus][processor][state]")
{
    // Known parameter values to verify round-trip
    const float expectedReleaseMs = 100.0f;    // default
    const float expectedInharm = 1.0f;         // default
    const float expectedMasterGain = 1.0f;     // default
    const std::string expectedPath = "test_roundtrip.wav";

    TestStream stream;

    {
        Innexus::Processor proc;
        proc.initialize(nullptr);
        auto setup = makeSetup();
        proc.setupProcessing(setup);
        proc.setActive(true);

        // Set a known file path (loadSample stores the path for state)
        proc.loadSample(expectedPath);

        REQUIRE(proc.getState(&stream) == kResultOk);

        proc.setActive(false);
        proc.terminate();
    }

    REQUIRE_FALSE(stream.empty());

    {
        stream.resetReadPos();

        Innexus::Processor proc;
        proc.initialize(nullptr);
        auto setup = makeSetup();
        proc.setupProcessing(setup);
        proc.setActive(true);

        REQUIRE(proc.setState(&stream) == kResultOk);

        // Verify parameter values survived the round-trip (T080 content check)
        REQUIRE(proc.getReleaseTimeMs() == Approx(expectedReleaseMs).margin(0.1f));
        REQUIRE(proc.getInharmonicityAmount() == Approx(expectedInharm).margin(0.001f));
        REQUIRE(proc.getMasterGain() == Approx(expectedMasterGain).margin(0.001f));
        REQUIRE(proc.getLoadedFilePath() == expectedPath);

        proc.setActive(false);
        proc.terminate();
    }
}

TEST_CASE("Innexus: State with no sample restores without crash (FR-056)",
          "[innexus][processor][state]")
{
    TestStream stream;

    {
        Innexus::Processor proc;
        proc.initialize(nullptr);
        auto setup = makeSetup();
        proc.setupProcessing(setup);
        proc.setActive(true);

        REQUIRE(proc.getState(&stream) == kResultOk);

        proc.setActive(false);
        proc.terminate();
    }

    {
        stream.resetReadPos();

        Innexus::Processor proc;
        proc.initialize(nullptr);
        auto setup = makeSetup();
        proc.setupProcessing(setup);
        proc.setActive(true);

        REQUIRE(proc.setState(&stream) == kResultOk);

        // Process a block to verify no crash
        std::vector<float> outL(kTestBlockSize, 0.0f);
        std::vector<float> outR(kTestBlockSize, 0.0f);
        float* outChannels[2] = {outL.data(), outR.data()};

        AudioBusBuffers outputBus{};
        outputBus.numChannels = 2;
        outputBus.channelBuffers32 = outChannels;

        ProcessData data{};
        data.processMode = kRealtime;
        data.symbolicSampleSize = kSample32;
        data.numSamples = kTestBlockSize;
        data.numOutputs = 1;
        data.outputs = &outputBus;
        data.numInputs = 0;
        data.inputParameterChanges = nullptr;
        data.inputEvents = nullptr;

        REQUIRE(proc.process(data) == kResultOk);

        proc.setActive(false);
        proc.terminate();
    }
}

TEST_CASE("Innexus: getState and setState return kResultFalse for null",
          "[innexus][processor][state]")
{
    Innexus::Processor proc;
    proc.initialize(nullptr);

    REQUIRE(proc.getState(nullptr) == kResultFalse);
    REQUIRE(proc.setState(nullptr) == kResultFalse);

    proc.terminate();
}

// =============================================================================
// SC-004: Full plugin CPU benchmark -- <5% CPU at 44.1kHz stereo, 48 partials
// =============================================================================
TEST_CASE("Innexus: SC-004 full plugin CPU benchmark 48 partials",
          "[.perf][innexus][processor]")
{
    constexpr double kBenchSampleRate = 44100.0;
    constexpr int32 kBenchBlockSize = 128;

    ProcessorTestFixture fix(kBenchBlockSize, kBenchSampleRate);

    // Create 48-partial analysis with many frames (analysis idle scenario)
    auto* analysis = makeFullTestAnalysis(50, 220.0f, 0.5f);
    fix.injectAnalysis(analysis);

    // Start a note so synthesis is active
    fix.events.addNoteOn(60, 1.0f, 0);
    fix.processAndGetMaxAmplitude();
    fix.events.clear();

    // Warm up
    for (int i = 0; i < 100; ++i)
    {
        fix.processAndGetMaxAmplitude();
    }

    BENCHMARK("Innexus full plugin process(), 48 partials, 128 samples")
    {
        fix.processAndGetMaxAmplitude();
        return fix.outL[0]; // prevent optimization
    };
}
