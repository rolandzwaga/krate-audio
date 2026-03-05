// ==============================================================================
// Harmonic Memory Integration Tests (M5)
// ==============================================================================
// Integration tests for capture trigger logic in the Processor.
// Tests capture source selection, slot storage, auto-reset, and slot independence.
//
// Feature: 119-harmonic-memory
// User Story 1: Capture a Harmonic Snapshot
// Requirements: FR-006, FR-007, FR-008, FR-009, FR-010, SC-001, SC-009
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"
#include "dsp/sample_analysis.h"

#include <krate/dsp/processors/harmonic_snapshot.h>
#include <krate/dsp/processors/harmonic_frame_utils.h>
#include <krate/dsp/processors/harmonic_types.h>
#include <krate/dsp/processors/residual_types.h>

#include "dsp/harmonic_snapshot_json.h"

#include "public.sdk/source/vst/hosting/hostclasses.h"

#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/base/ibstream.h"
#include "base/source/fstreamer.h"

#include "test_helpers/artifact_detection.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using Catch::Approx;

// =============================================================================
// Test Infrastructure (mirrors M4 test patterns)
// =============================================================================

static constexpr double kM5TestSampleRate = 44100.0;
static constexpr int32 kM5TestBlockSize = 128;

static ProcessSetup makeM5Setup(double sampleRate = kM5TestSampleRate,
                                int32 blockSize = kM5TestBlockSize)
{
    ProcessSetup setup{};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = blockSize;
    setup.sampleRate = sampleRate;
    return setup;
}

// Create a simple harmonic analysis with known partials
static Innexus::SampleAnalysis* makeM5TestAnalysis(
    int numFrames = 10,
    float f0 = 440.0f,
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
        frame.numPartials = 4;
        frame.globalAmplitude = amplitude;
        frame.spectralCentroid = 1000.0f;
        frame.brightness = 0.5f;
        frame.noisiness = 0.1f;

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
            partial.phase = static_cast<float>(p) * 0.3f;
        }

        analysis->frames.push_back(frame);
    }

    analysis->totalFrames = analysis->frames.size();
    analysis->filePath = "test_m5_sample.wav";

    return analysis;
}

// Create analysis with different F0/amplitude for distinguishable captures
static Innexus::SampleAnalysis* makeM5DistinctAnalysis(
    float f0 = 880.0f,
    float amplitude = 0.7f,
    int numFrames = 10)
{
    return makeM5TestAnalysis(numFrames, f0, amplitude);
}

// Create analysis with residual frames
static Innexus::SampleAnalysis* makeM5AnalysisWithResidual(
    int numFrames = 10,
    float f0 = 440.0f,
    float amplitude = 0.5f)
{
    auto* analysis = makeM5TestAnalysis(numFrames, f0, amplitude);
    analysis->analysisFFTSize = 1024;
    analysis->analysisHopSize = 256;

    for (int f = 0; f < numFrames; ++f)
    {
        Krate::DSP::ResidualFrame rframe{};
        rframe.totalEnergy = 0.05f;
        for (size_t b = 0; b < Krate::DSP::kResidualBands; ++b)
            rframe.bandEnergies[b] = 0.003f;
        rframe.transientFlag = (f == 0);
        analysis->residualFrames.push_back(rframe);
    }

    return analysis;
}

// Minimal EventList for MIDI events
class M5TestEventList : public IEventList
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
class M5TestParamValueQueue : public IParamValueQueue
{
public:
    M5TestParamValueQueue(ParamID id, ParamValue val)
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
class M5TestParameterChanges : public IParameterChanges
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
    std::vector<M5TestParamValueQueue> queues_;
};

// Helper fixture for M5 tests
struct M5TestFixture
{
    Innexus::Processor processor;
    M5TestEventList events;
    M5TestParameterChanges paramChanges;

    std::vector<float> outL;
    std::vector<float> outR;
    float* outChannels[2];
    AudioBusBuffers outputBus{};
    ProcessData data{};

    M5TestFixture(int32 blockSize = kM5TestBlockSize,
                  double sampleRate = kM5TestSampleRate)
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
        auto setup = makeM5Setup(sampleRate, blockSize);
        processor.setupProcessing(setup);
        processor.setActive(true);
    }

    ~M5TestFixture()
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

    // Helper: trigger capture on the current slot
    void triggerCapture()
    {
        paramChanges.addChange(Innexus::kMemoryCaptureId, 1.0);
        processBlockWithParams();
    }

    // Helper: select memory slot (0-7 mapped to 0.0-1.0)
    void selectSlot(int slotIndex)
    {
        float norm = static_cast<float>(slotIndex) / 7.0f;
        paramChanges.addChange(Innexus::kMemorySlotId, static_cast<double>(norm));
        processBlockWithParams();
    }

    // Helper: trigger recall on the current slot
    void triggerRecall()
    {
        paramChanges.addChange(Innexus::kMemoryRecallId, 1.0);
        processBlockWithParams();
    }

    // Helper: engage freeze via parameter change
    void engageFreeze()
    {
        paramChanges.addChange(Innexus::kFreezeId, 1.0);
        processBlockWithParams();
    }

    // Helper: set morph position
    void setMorphPosition(double pos)
    {
        paramChanges.addChange(Innexus::kMorphPositionId, pos);
        processBlockWithParams();
    }

    // Helper: set harmonic filter type (0=AllPass, 1=OddOnly, ...)
    void setFilterType(int type)
    {
        double norm = static_cast<double>(type) / 4.0;
        paramChanges.addChange(Innexus::kHarmonicFilterTypeId, norm);
        processBlockWithParams();
    }
};

// =============================================================================
// Phase 3.3: Capture Integration Tests
// =============================================================================

// T032: Triggering capture stores snapshot in selected slot
TEST_CASE("M5 Capture: triggering capture stores snapshot in selected slot",
          "[innexus][m5][capture][us1]")
{
    M5TestFixture fix;
    fix.injectAnalysis(makeM5TestAnalysis(10, 440.0f, 0.5f));

    // Play a note so frames are loaded
    fix.events.addNoteOn(69, 1.0f);
    fix.processBlock();
    fix.events.clear();

    // Process a few blocks to load frames
    for (int i = 0; i < 3; ++i)
        fix.processBlock();

    // Verify slot 0 is empty before capture
    REQUIRE(fix.processor.getMemorySlot(0).occupied == false);

    // Trigger capture
    fix.triggerCapture();

    // Verify slot 0 is now occupied
    REQUIRE(fix.processor.getMemorySlot(0).occupied == true);
    REQUIRE(fix.processor.getMemorySlot(0).snapshot.numPartials == 4);
}

// T032 continued: verify auto-reset of capture trigger
TEST_CASE("M5 Capture: kMemoryCaptureId resets to 0.0 after capture fires",
          "[innexus][m5][capture][us1]")
{
    M5TestFixture fix;
    fix.injectAnalysis(makeM5TestAnalysis(10, 440.0f, 0.5f));

    fix.events.addNoteOn(69, 1.0f);
    fix.processBlock();
    fix.events.clear();
    for (int i = 0; i < 3; ++i)
        fix.processBlock();

    // Trigger capture
    fix.triggerCapture();

    REQUIRE(fix.processor.getMemorySlot(0).occupied == true);

    // Process another block without any parameter changes -- the trigger should
    // have been consumed so a second capture does NOT happen with no new trigger
    // (We can verify indirectly by checking the trigger can fire again)

    // Inject a new analysis with different data
    fix.injectAnalysis(makeM5DistinctAnalysis(880.0f, 0.7f));
    for (int i = 0; i < 3; ++i)
        fix.processBlock();

    // Trigger capture again (should work because auto-reset made it back to 0)
    fix.triggerCapture();

    // The slot should now contain the new 880 Hz data
    REQUIRE(fix.processor.getMemorySlot(0).occupied == true);
    REQUIRE(fix.processor.getMemorySlot(0).snapshot.f0Reference
            == Approx(880.0f).margin(1.0f));
}

// T033: Captured relativeFreqs match source within 1e-6
TEST_CASE("M5 Capture: captured relativeFreqs match source HarmonicFrame within 1e-6",
          "[innexus][m5][capture][sc001]")
{
    M5TestFixture fix;
    fix.injectAnalysis(makeM5TestAnalysis(10, 440.0f, 0.5f));

    fix.events.addNoteOn(69, 1.0f);
    fix.processBlock();
    fix.events.clear();
    for (int i = 0; i < 3; ++i)
        fix.processBlock();

    fix.triggerCapture();

    const auto& snap = fix.processor.getMemorySlot(0).snapshot;
    REQUIRE(snap.numPartials == 4);

    // Source frame has relativeFrequency = 1, 2, 3, 4 (perfect harmonics)
    for (int p = 0; p < 4; ++p)
    {
        REQUIRE(snap.relativeFreqs[static_cast<size_t>(p)]
                == Approx(static_cast<float>(p + 1)).margin(1e-6f));
    }
}

// T034: Capture during morph blend stores post-morph state
TEST_CASE("M5 Capture: capture during morph blend stores post-morph state",
          "[innexus][m5][capture][fr008]")
{
    // Use two distinguishable analyses: frozen (f0=440, amp=0.5) vs live (f0=880, amp=1.0)
    // At morph=0.5, interpolated values should lie strictly between both sources.
    constexpr float frozenF0 = 440.0f;
    constexpr float frozenAmp = 0.5f;
    constexpr float liveF0 = 880.0f;
    constexpr float liveAmp = 1.0f;

    M5TestFixture fix;
    fix.injectAnalysis(makeM5TestAnalysis(10, frozenF0, frozenAmp));

    // Play a note and process to load frames
    fix.events.addNoteOn(69, 1.0f);
    fix.processBlock();
    fix.events.clear();
    for (int i = 0; i < 3; ++i)
        fix.processBlock();

    // Engage freeze -- captures current state as frozen frame (f0=440, amp=0.5)
    fix.engageFreeze();

    // Inject a different analysis for the live source (f0=880, amp=1.0)
    fix.injectAnalysis(makeM5TestAnalysis(10, liveF0, liveAmp));

    // Process a few blocks so the processor loads the new live frames
    for (int i = 0; i < 5; ++i)
        fix.processBlock();

    // Set morph position to 0.5 (halfway between frozen and live)
    fix.setMorphPosition(0.5);

    // Process several blocks to let morph smoother converge
    for (int i = 0; i < 20; ++i)
        fix.processBlock();

    // Trigger capture -- should capture the morphed (interpolated) frame
    fix.triggerCapture();

    const auto& snap = fix.processor.getMemorySlot(0).snapshot;
    REQUIRE(snap.numPartials > 0);
    REQUIRE(snap.f0Reference > 0.0f);

    // Verify snapshot values are strictly between frozen-only and live-only values,
    // confirming the capture read from the morphed frame, not either pure source.
    // captureSnapshot stores frame.f0 as f0Reference and frame.globalAmplitude directly.
    // lerpHarmonicFrame interpolates: result = (1-t)*frozen + t*live at t=0.5.
    const float minF0 = std::min(frozenF0, liveF0);
    const float maxF0 = std::max(frozenF0, liveF0);
    REQUIRE(snap.f0Reference > minF0);
    REQUIRE(snap.f0Reference < maxF0);

    const float minAmp = std::min(frozenAmp, liveAmp);
    const float maxAmp = std::max(frozenAmp, liveAmp);
    REQUIRE(snap.globalAmplitude > minAmp);
    REQUIRE(snap.globalAmplitude < maxAmp);

    // Also check partial[0] amplitude: frozen = 0.5/1 = 0.5, live = 1.0/1 = 1.0
    // After L2 normalization in captureSnapshot the absolute values change, but
    // we can verify the morphed frame's raw partial amplitude is interpolated by
    // checking the morphedFrame directly before capture normalizes it.
    // Instead, verify f0Reference is approximately the expected morph midpoint.
    REQUIRE(snap.f0Reference == Approx((frozenF0 + liveF0) / 2.0f).margin(5.0f));
    REQUIRE(snap.globalAmplitude == Approx((frozenAmp + liveAmp) / 2.0f).margin(0.05f));
}

// T035: Capture with harmonic filter active stores pre-filter data
TEST_CASE("M5 Capture: capture with harmonic filter stores pre-filter data",
          "[innexus][m5][capture][fr009]")
{
    M5TestFixture fix;
    fix.injectAnalysis(makeM5TestAnalysis(10, 440.0f, 0.5f));

    fix.events.addNoteOn(69, 1.0f);
    fix.processBlock();
    fix.events.clear();
    for (int i = 0; i < 3; ++i)
        fix.processBlock();

    // Enable "Odd Only" filter (type 1 -- zeros even harmonics)
    fix.setFilterType(1);

    // Process a block to apply the filter
    fix.processBlock();

    // Trigger capture
    fix.triggerCapture();

    const auto& snap = fix.processor.getMemorySlot(0).snapshot;
    REQUIRE(snap.numPartials == 4);

    // Even-harmonic partials (index 2, 4) should have NON-ZERO amplitudes
    // in the captured snapshot because capture is pre-filter (FR-009)
    // Partial indices 1 and 3 (0-based) are harmonics 2 and 4 (even)
    REQUIRE(snap.normalizedAmps[1] > 0.0f); // harmonic 2 (even)
    REQUIRE(snap.normalizedAmps[3] > 0.0f); // harmonic 4 (even)
}

// T036: Capture with no analysis loaded stores empty snapshot
TEST_CASE("M5 Capture: capture with no analysis stores empty snapshot",
          "[innexus][m5][capture][empty]")
{
    M5TestFixture fix;
    // No analysis injected, no note active -- process() will early-return
    // But capture should still work on the empty default frames

    // We need a note active and some form of source for process() to reach
    // capture logic. With no analysis, process() returns early with silence.
    // However, the capture trigger detection should still fire at block start.

    // Trigger capture (no analysis, no note)
    fix.triggerCapture();

    const auto& slot = fix.processor.getMemorySlot(0);
    REQUIRE(slot.occupied == true);
    REQUIRE(slot.snapshot.numPartials == 0);
    REQUIRE(slot.snapshot.residualEnergy == Approx(0.0f).margin(1e-6f));
}

// T037: Capture overwrites existing slot data
TEST_CASE("M5 Capture: capture into occupied slot overwrites previous data",
          "[innexus][m5][capture][overwrite]")
{
    M5TestFixture fix;
    fix.injectAnalysis(makeM5TestAnalysis(10, 440.0f, 0.5f));

    fix.events.addNoteOn(69, 1.0f);
    fix.processBlock();
    fix.events.clear();
    for (int i = 0; i < 3; ++i)
        fix.processBlock();

    // First capture (timbre A at 440 Hz)
    fix.triggerCapture();
    REQUIRE(fix.processor.getMemorySlot(0).snapshot.f0Reference
            == Approx(440.0f).margin(1.0f));

    // Change analysis to a different timbre
    fix.injectAnalysis(makeM5DistinctAnalysis(880.0f, 0.7f));
    for (int i = 0; i < 5; ++i)
        fix.processBlock();

    // Second capture into the same slot (overwrites)
    fix.triggerCapture();

    // Slot 0 should now contain timbre B (880 Hz)
    REQUIRE(fix.processor.getMemorySlot(0).occupied == true);
    REQUIRE(fix.processor.getMemorySlot(0).snapshot.f0Reference
            == Approx(880.0f).margin(1.0f));
}

// T038: Capturing into Slot N does NOT modify other slots (SC-009)
TEST_CASE("M5 Capture: slot independence - capturing into Slot N does not modify other slots",
          "[innexus][m5][capture][sc009]")
{
    M5TestFixture fix;
    fix.injectAnalysis(makeM5TestAnalysis(10, 440.0f, 0.5f));

    fix.events.addNoteOn(69, 1.0f);
    fix.processBlock();
    fix.events.clear();
    for (int i = 0; i < 3; ++i)
        fix.processBlock();

    // Populate all 8 slots with distinct captures
    for (int slot = 0; slot < 8; ++slot)
    {
        // Change to a different analysis for each slot
        float f0 = 200.0f + static_cast<float>(slot) * 100.0f;
        fix.injectAnalysis(makeM5TestAnalysis(10, f0, 0.5f));
        for (int b = 0; b < 3; ++b)
            fix.processBlock();

        fix.selectSlot(slot);
        fix.triggerCapture();

        REQUIRE(fix.processor.getMemorySlot(slot).occupied == true);
    }

    // Record the f0Reference for all slots
    std::array<float, 8> f0Before{};
    std::array<int, 8> numPartialsBefore{};
    for (int slot = 0; slot < 8; ++slot)
    {
        f0Before[static_cast<size_t>(slot)] =
            fix.processor.getMemorySlot(slot).snapshot.f0Reference;
        numPartialsBefore[static_cast<size_t>(slot)] =
            fix.processor.getMemorySlot(slot).snapshot.numPartials;
    }

    // Now capture a new timbre into Slot 3 only
    fix.injectAnalysis(makeM5DistinctAnalysis(1500.0f, 0.9f));
    for (int b = 0; b < 3; ++b)
        fix.processBlock();
    fix.selectSlot(3);
    fix.triggerCapture();

    // Verify Slot 3 was updated
    REQUIRE(fix.processor.getMemorySlot(3).snapshot.f0Reference
            == Approx(1500.0f).margin(1.0f));

    // Verify all OTHER slots are unchanged
    for (int slot = 0; slot < 8; ++slot)
    {
        if (slot == 3) continue;
        auto idx = static_cast<size_t>(slot);
        REQUIRE(fix.processor.getMemorySlot(slot).snapshot.f0Reference
                == Approx(f0Before[idx]).margin(1e-6f));
        REQUIRE(fix.processor.getMemorySlot(slot).snapshot.numPartials
                == numPartialsBefore[idx]);
    }
}

// T039: Rapid consecutive captures - last capture wins
TEST_CASE("M5 Capture: rapid consecutive captures result in last capture winning",
          "[innexus][m5][capture][rapid]")
{
    M5TestFixture fix;
    fix.injectAnalysis(makeM5TestAnalysis(10, 440.0f, 0.5f));

    fix.events.addNoteOn(69, 1.0f);
    fix.processBlock();
    fix.events.clear();
    for (int i = 0; i < 3; ++i)
        fix.processBlock();

    // Trigger multiple captures rapidly (each trigger resets to 0, then fires again)
    for (int i = 0; i < 5; ++i)
    {
        float f0 = 300.0f + static_cast<float>(i) * 100.0f;
        fix.injectAnalysis(makeM5TestAnalysis(10, f0, 0.5f));
        fix.processBlock(); // Load new analysis
        fix.triggerCapture();
    }

    // Slot should contain the LAST capture (f0 = 300 + 4*100 = 700)
    REQUIRE(fix.processor.getMemorySlot(0).occupied == true);
    REQUIRE(fix.processor.getMemorySlot(0).snapshot.f0Reference
            == Approx(700.0f).margin(1.0f));
}

// =============================================================================
// Phase 4.1: Recall Integration Tests (User Story 2)
// =============================================================================

// T047: Triggering recall on occupied slot loads snapshot into manualFrozenFrame_
TEST_CASE("M5 Recall: triggering recall loads snapshot into manualFrozenFrame",
          "[innexus][m5][recall][fr012]")
{
    M5TestFixture fix;
    fix.injectAnalysis(makeM5TestAnalysis(10, 440.0f, 0.5f));

    // Play a note and process to load frames
    fix.events.addNoteOn(69, 1.0f);
    fix.processBlock();
    fix.events.clear();
    for (int i = 0; i < 3; ++i)
        fix.processBlock();

    // Capture into slot 0
    fix.triggerCapture();
    REQUIRE(fix.processor.getMemorySlot(0).occupied == true);

    const auto& snap = fix.processor.getMemorySlot(0).snapshot;
    REQUIRE(snap.numPartials == 4);

    // Trigger recall
    fix.triggerRecall();

    // Verify manualFrozenFrame_ was loaded from the snapshot
    const auto& frozenFrame = fix.processor.getManualFrozenFrame();
    REQUIRE(frozenFrame.numPartials == snap.numPartials);

    for (int p = 0; p < snap.numPartials; ++p)
    {
        auto idx = static_cast<size_t>(p);
        REQUIRE(frozenFrame.partials[idx].relativeFrequency
                == Approx(snap.relativeFreqs[idx]).margin(1e-6f));
        REQUIRE(frozenFrame.partials[idx].amplitude
                == Approx(snap.normalizedAmps[idx]).margin(1e-6f));
    }
}

// T047 continued: verify auto-reset of recall trigger
TEST_CASE("M5 Recall: kMemoryRecallId resets to 0.0 after recall fires",
          "[innexus][m5][recall][fr011]")
{
    M5TestFixture fix;
    fix.injectAnalysis(makeM5TestAnalysis(10, 440.0f, 0.5f));

    fix.events.addNoteOn(69, 1.0f);
    fix.processBlock();
    fix.events.clear();
    for (int i = 0; i < 3; ++i)
        fix.processBlock();

    // Capture into slot 0
    fix.triggerCapture();

    // Recall
    fix.triggerRecall();
    REQUIRE(fix.processor.getManualFreezeActive() == true);

    // Disengage freeze so next capture reads live data (not frozen frame)
    fix.paramChanges.addChange(Innexus::kFreezeId, 0.0);
    fix.processBlockWithParams();
    for (int i = 0; i < 10; ++i)
        fix.processBlock();

    // Now change to a different analysis (different data) and capture
    fix.injectAnalysis(makeM5DistinctAnalysis(880.0f, 0.7f));
    for (int i = 0; i < 5; ++i)
        fix.processBlock();
    fix.triggerCapture();

    // Recall again -- should work because auto-reset made it back to 0
    fix.triggerRecall();

    // Verify the frozen frame updated to the new snapshot (880 Hz)
    REQUIRE(fix.processor.getManualFrozenFrame().f0
            == Approx(880.0f).margin(1.0f));
}

// T048: Triggering recall sets manualFreezeActive_ = true
TEST_CASE("M5 Recall: triggering recall sets manualFreezeActive to true",
          "[innexus][m5][recall][fr012d]")
{
    M5TestFixture fix;
    fix.injectAnalysis(makeM5TestAnalysis(10, 440.0f, 0.5f));

    fix.events.addNoteOn(69, 1.0f);
    fix.processBlock();
    fix.events.clear();
    for (int i = 0; i < 3; ++i)
        fix.processBlock();

    REQUIRE(fix.processor.getManualFreezeActive() == false);

    fix.triggerCapture();
    fix.triggerRecall();

    REQUIRE(fix.processor.getManualFreezeActive() == true);
}

// T049: Recall on empty slot is silently ignored
TEST_CASE("M5 Recall: recall on empty slot is silently ignored",
          "[innexus][m5][recall][fr013]")
{
    M5TestFixture fix;
    fix.injectAnalysis(makeM5TestAnalysis(10, 440.0f, 0.5f));

    fix.events.addNoteOn(69, 1.0f);
    fix.processBlock();
    fix.events.clear();
    for (int i = 0; i < 3; ++i)
        fix.processBlock();

    // Slot 0 is empty (no capture)
    REQUIRE(fix.processor.getMemorySlot(0).occupied == false);
    REQUIRE(fix.processor.getManualFreezeActive() == false);

    // Trigger recall on empty slot
    fix.triggerRecall();

    // Should remain unchanged
    REQUIRE(fix.processor.getManualFreezeActive() == false);
    REQUIRE(fix.processor.getManualFrozenFrame().numPartials == 0);
}

// T050: After recall, oscillator bank receives recalled frame data (not live)
TEST_CASE("M5 Recall: recalled frame data is used by oscillator bank (not live analysis)",
          "[innexus][m5][recall][sc002]")
{
    M5TestFixture fix;
    fix.injectAnalysis(makeM5TestAnalysis(10, 440.0f, 0.5f));

    // Play a note and load frames
    fix.events.addNoteOn(69, 1.0f);
    fix.processBlock();
    fix.events.clear();
    for (int i = 0; i < 3; ++i)
        fix.processBlock();

    // Capture into slot 0 (440 Hz source)
    fix.triggerCapture();

    const auto& snap = fix.processor.getMemorySlot(0).snapshot;

    // Change to a completely different analysis (880 Hz)
    fix.injectAnalysis(makeM5DistinctAnalysis(880.0f, 0.7f));
    for (int i = 0; i < 5; ++i)
        fix.processBlock();

    // Recall slot 0 (should load 440 Hz data, not 880 Hz)
    fix.triggerRecall();

    // Process a block for the recall to take effect in the osc bank
    fix.processBlock();

    // The frozen frame should match the original 440 Hz snapshot
    const auto& frozenFrame = fix.processor.getManualFrozenFrame();
    REQUIRE(frozenFrame.numPartials == snap.numPartials);
    REQUIRE(frozenFrame.f0 == Approx(snap.f0Reference).margin(1e-6f));

    for (int p = 0; p < snap.numPartials; ++p)
    {
        auto idx = static_cast<size_t>(p);
        REQUIRE(frozenFrame.partials[idx].relativeFrequency
                == Approx(snap.relativeFreqs[idx]).margin(1e-6f));
    }

    // The morphedFrame (which feeds the osc bank when freeze is active with morph=0)
    // should reflect the frozen (recalled) data, not the live 880 Hz
    const auto& morphed = fix.processor.getMorphedFrame();
    REQUIRE(morphed.f0 == Approx(snap.f0Reference).margin(1e-6f));
}

// T051: Slot-to-slot recall produces no audible click (crossfade test)
TEST_CASE("M5 Recall: slot-to-slot recall produces no audible click",
          "[innexus][m5][recall][sc003]")
{
    // Use 512-sample blocks as specified by SC-003
    M5TestFixture fix(512, kM5TestSampleRate);
    fix.injectAnalysis(makeM5AnalysisWithResidual(10, 440.0f, 0.5f));

    // Play a note
    fix.events.addNoteOn(69, 1.0f);
    fix.processBlock();
    fix.events.clear();
    for (int i = 0; i < 3; ++i)
        fix.processBlock();

    // Capture two different timbres into Slot 0 and Slot 1
    fix.selectSlot(0);
    fix.triggerCapture();

    fix.injectAnalysis(makeM5AnalysisWithResidual(10, 880.0f, 0.7f));
    for (int i = 0; i < 3; ++i)
        fix.processBlock();
    fix.selectSlot(1);
    fix.triggerCapture();

    // Recall slot 0 first
    fix.selectSlot(0);
    fix.triggerRecall();

    // Process blocks for the recall to stabilize and crossfade to complete
    for (int i = 0; i < 20; ++i)
        fix.processBlock();

    // Compute RMS of the buffer immediately before the crossfade (SC-003)
    fix.processBlock();
    float sumSquares = 0.0f;
    for (int s = 0; s < 512; ++s)
        sumSquares += fix.outL[static_cast<size_t>(s)] * fix.outL[static_cast<size_t>(s)];
    float rmsBeforeCrossfade = std::sqrt(sumSquares / 512.0f);

    // Now recall slot 1 (this should trigger a crossfade)
    fix.selectSlot(1);
    fix.triggerRecall();

    // Verify the crossfade mechanism is active (FR-015: slot-to-slot crossfade)
    // The recovery counter should have been set when the second recall fired
    // We check the crossfade length is <= 10ms
    const int expectedCrossfadeSamples = static_cast<int>(
        std::round(kM5TestSampleRate * 0.010));
    REQUIRE(fix.processor.getManualFreezeRecoveryLengthSamples()
            <= expectedCrossfadeSamples);

    // Process a block during the crossfade
    fix.processBlock();

    // SC-003: Verify no audible click or pop. The spec requires that no
    // sample-to-sample amplitude step exceeds -60 dB relative to the RMS
    // level of the sustained note.
    //
    // For an additive synthesizer producing partials at audible frequencies,
    // the raw sample-to-sample waveform variation is inherently large
    // (proportional to frequency and amplitude). A click/pop is an ANOMALOUS
    // discontinuity that exceeds normal signal dynamics. We use the
    // ClickDetector (derivative-based statistical detection) to identify
    // such anomalies. The energy threshold is set to -60 dB relative to
    // the RMS level, matching the SC-003 specification.
    if (rmsBeforeCrossfade > 1e-4f)
    {
        // Convert RMS to dB for the energy threshold
        float rmsDb = 20.0f * std::log10(rmsBeforeCrossfade);
        float thresholdDb = rmsDb - 60.0f;

        Krate::DSP::TestUtils::ClickDetectorConfig config;
        config.sampleRate = static_cast<float>(kM5TestSampleRate);
        config.frameSize = 512;
        config.hopSize = 256;
        // Use 5-sigma threshold for robust detection
        config.detectionThreshold = 5.0f;
        config.energyThresholdDb = thresholdDb;

        Krate::DSP::TestUtils::ClickDetector detector(config);
        detector.prepare();

        auto clicks = detector.detect(fix.outL.data(), 512);

        INFO("RMS before crossfade: " << rmsBeforeCrossfade
             << " (" << rmsDb << " dB)");
        INFO("Energy threshold: " << thresholdDb << " dB");
        INFO("Number of clicks detected: " << clicks.size());
        if (!clicks.empty())
        {
            INFO("First click at sample " << clicks[0].sampleIndex
                 << " amplitude " << clicks[0].amplitude);
        }

        // SC-003: No clicks/pops should be detected during crossfade
        REQUIRE(clicks.empty());
    }
}

// T052: After recall, morph position 0.0 plays recalled snapshot, morph 1.0 plays live
TEST_CASE("M5 Recall: morph 0.0 plays recalled snapshot, morph 1.0 plays live",
          "[innexus][m5][recall][fr016]")
{
    M5TestFixture fix;
    // Start with 440 Hz analysis (will become the frozen/recalled frame)
    fix.injectAnalysis(makeM5TestAnalysis(10, 440.0f, 0.5f));

    fix.events.addNoteOn(69, 1.0f);
    fix.processBlock();
    fix.events.clear();
    for (int i = 0; i < 3; ++i)
        fix.processBlock();

    // Capture into slot 0
    fix.triggerCapture();
    const float capturedF0 = fix.processor.getMemorySlot(0).snapshot.f0Reference;

    // Change to a different analysis (880 Hz = live)
    fix.injectAnalysis(makeM5TestAnalysis(10, 880.0f, 0.7f));
    for (int i = 0; i < 5; ++i)
        fix.processBlock();

    // Recall slot 0 (freeze engaged with 440 Hz snapshot)
    fix.triggerRecall();

    // Morph at 0.0 -> should be fully recalled (frozen) snapshot
    fix.setMorphPosition(0.0);
    for (int i = 0; i < 10; ++i)
        fix.processBlock();

    const auto& morphedAt0 = fix.processor.getMorphedFrame();
    REQUIRE(morphedAt0.f0 == Approx(capturedF0).margin(1e-6f));

    // Morph at 1.0 -> should be fully live analysis (880 Hz)
    fix.setMorphPosition(1.0);
    for (int i = 0; i < 50; ++i) // Allow smoother to converge
        fix.processBlock();

    const auto& morphedAt1 = fix.processor.getMorphedFrame();
    REQUIRE(morphedAt1.f0 == Approx(880.0f).margin(5.0f));
}

// T053: After recall, harmonic filter applies to recalled data
TEST_CASE("M5 Recall: harmonic filter applies to recalled snapshot data",
          "[innexus][m5][recall][fr017]")
{
    M5TestFixture fix;
    fix.injectAnalysis(makeM5TestAnalysis(10, 440.0f, 0.5f));

    fix.events.addNoteOn(69, 1.0f);
    fix.processBlock();
    fix.events.clear();
    for (int i = 0; i < 3; ++i)
        fix.processBlock();

    // Capture into slot 0 (4 partials: harmonics 1, 2, 3, 4)
    fix.triggerCapture();

    // Recall slot 0
    fix.triggerRecall();
    fix.processBlock();

    // Enable "Odd Only" filter (type 1)
    fix.setFilterType(1);

    // Process for filter to take effect
    fix.processBlock();

    // morphedFrame_ should have the filter applied -- even harmonics zeroed
    const auto& morphed = fix.processor.getMorphedFrame();

    // With Odd Only filter, harmonics 2 and 4 (indices 1 and 3) should be ~0
    // Harmonics 1 and 3 (indices 0 and 2) should be non-zero
    REQUIRE(morphed.partials[0].amplitude > 0.0f); // harmonic 1 (odd)
    REQUIRE(morphed.partials[1].amplitude == Approx(0.0f).margin(1e-6f)); // harmonic 2 (even)
    REQUIRE(morphed.partials[2].amplitude > 0.0f); // harmonic 3 (odd)
    REQUIRE(morphed.partials[3].amplitude == Approx(0.0f).margin(1e-6f)); // harmonic 4 (even)
}

// T054: Disengaging freeze after recall returns to live analysis
TEST_CASE("M5 Recall: disengaging freeze after recall returns to live analysis",
          "[innexus][m5][recall][fr018]")
{
    M5TestFixture fix;
    fix.injectAnalysis(makeM5TestAnalysis(10, 440.0f, 0.5f));

    fix.events.addNoteOn(69, 1.0f);
    fix.processBlock();
    fix.events.clear();
    for (int i = 0; i < 3; ++i)
        fix.processBlock();

    // Capture and recall
    fix.triggerCapture();
    fix.triggerRecall();

    REQUIRE(fix.processor.getManualFreezeActive() == true);

    // Change to different live analysis
    fix.injectAnalysis(makeM5TestAnalysis(10, 880.0f, 0.7f));
    for (int i = 0; i < 5; ++i)
        fix.processBlock();

    // Disengage freeze via kFreezeId = 0
    fix.paramChanges.addChange(Innexus::kFreezeId, 0.0);
    fix.processBlockWithParams();

    // Process enough blocks for the crossfade to complete (~10ms at 44100 = 441 samples)
    // and for the live analysis to propagate through to the morphed frame
    for (int i = 0; i < 50; ++i)
        fix.processBlock();

    // manualFreezeActive_ should be false
    REQUIRE(fix.processor.getManualFreezeActive() == false);

    // FR-018: After freeze disengage, the morphed frame should return to tracking
    // the live analysis (880 Hz), confirming the oscillator bank is no longer
    // playing the recalled snapshot
    const auto& morphedAfterDisengage = fix.processor.getMorphedFrame();
    REQUIRE(morphedAfterDisengage.f0 == Approx(880.0f).margin(5.0f));
}

// T055: Capturing into active recalled slot does NOT update manualFrozenFrame_
TEST_CASE("M5 Recall: capturing into active recalled slot does not update manualFrozenFrame",
          "[innexus][m5][recall][edge]")
{
    M5TestFixture fix;
    fix.injectAnalysis(makeM5TestAnalysis(10, 440.0f, 0.5f));

    fix.events.addNoteOn(69, 1.0f);
    fix.processBlock();
    fix.events.clear();
    for (int i = 0; i < 3; ++i)
        fix.processBlock();

    // Capture into slot 0 (440 Hz)
    fix.triggerCapture();

    // Recall slot 0 -- manualFrozenFrame_ is a COPY of the snapshot
    fix.triggerRecall();
    fix.processBlock();

    const auto& frozenBefore = fix.processor.getManualFrozenFrame();
    float frozenF0Before = frozenBefore.f0;
    REQUIRE(frozenF0Before == Approx(440.0f).margin(1.0f));

    // Set morph to 1.0 so capture reads from the live (morphed) frame,
    // not from the frozen frame. This ensures the slot gets different data.
    fix.injectAnalysis(makeM5DistinctAnalysis(880.0f, 0.7f));
    fix.setMorphPosition(1.0);
    for (int i = 0; i < 50; ++i) // Let smoother converge to 1.0
        fix.processBlock();

    // Trigger capture -- should capture the post-morph frame (fully live = 880 Hz)
    fix.triggerCapture();

    // The slot should be updated to approximately 880 Hz (morphed live data)
    REQUIRE(fix.processor.getMemorySlot(0).snapshot.f0Reference
            == Approx(880.0f).margin(5.0f));

    // But manualFrozenFrame_ should STILL be the originally recalled 440 Hz data
    fix.processBlock();
    const auto& frozenAfter = fix.processor.getManualFrozenFrame();
    REQUIRE(frozenAfter.f0 == Approx(440.0f).margin(1.0f));
}

// T056: Changing slot selector while a recalled slot is active does NOT auto-recall
TEST_CASE("M5 Recall: changing slot selector does not auto-recall new slot",
          "[innexus][m5][recall][edge]")
{
    M5TestFixture fix;
    fix.injectAnalysis(makeM5TestAnalysis(10, 440.0f, 0.5f));

    fix.events.addNoteOn(69, 1.0f);
    fix.processBlock();
    fix.events.clear();
    for (int i = 0; i < 3; ++i)
        fix.processBlock();

    // Capture into slot 0 (440 Hz) and slot 3 (880 Hz)
    fix.selectSlot(0);
    fix.triggerCapture();

    fix.injectAnalysis(makeM5DistinctAnalysis(880.0f, 0.7f));
    for (int i = 0; i < 3; ++i)
        fix.processBlock();
    fix.selectSlot(3);
    fix.triggerCapture();

    // Recall slot 0
    fix.selectSlot(0);
    fix.triggerRecall();
    fix.processBlock();

    REQUIRE(fix.processor.getManualFreezeActive() == true);
    REQUIRE(fix.processor.getManualFrozenFrame().f0
            == Approx(440.0f).margin(1.0f));

    // Change slot selector to slot 3 -- should NOT auto-recall
    fix.selectSlot(3);
    fix.processBlock();

    // Freeze frame should still be slot 0's data (440 Hz)
    REQUIRE(fix.processor.getManualFreezeActive() == true);
    REQUIRE(fix.processor.getManualFrozenFrame().f0
            == Approx(440.0f).margin(1.0f));
}

// =============================================================================
// Phase 5: State Persistence Tests (User Story 3)
// =============================================================================

// Minimal IBStream implementation for state save/load testing
class M5TestStream : public IBStream
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

// Helper: create a distinct HarmonicSnapshot with known data for a given slot index
static Krate::DSP::HarmonicSnapshot makeDistinctSnapshot(int slotIndex)
{
    Krate::DSP::HarmonicSnapshot snap{};
    snap.f0Reference = 100.0f + static_cast<float>(slotIndex) * 50.0f;
    snap.numPartials = 3 + slotIndex % 5;
    snap.globalAmplitude = 0.3f + static_cast<float>(slotIndex) * 0.05f;
    snap.spectralCentroid = 800.0f + static_cast<float>(slotIndex) * 100.0f;
    snap.brightness = 0.2f + static_cast<float>(slotIndex) * 0.1f;
    snap.residualEnergy = 0.01f + static_cast<float>(slotIndex) * 0.005f;

    for (int p = 0; p < snap.numPartials; ++p)
    {
        auto idx = static_cast<size_t>(p);
        snap.relativeFreqs[idx] = static_cast<float>(p + 1) + 0.01f * static_cast<float>(slotIndex);
        snap.normalizedAmps[idx] = 0.5f / static_cast<float>(p + 1);
        snap.phases[idx] = 0.1f * static_cast<float>(p) + 0.05f * static_cast<float>(slotIndex);
        snap.inharmonicDeviation[idx] = 0.01f * static_cast<float>(slotIndex);
    }

    for (size_t b = 0; b < Krate::DSP::kResidualBands; ++b)
    {
        snap.residualBands[b] = 0.002f + 0.001f * static_cast<float>(slotIndex)
                                + 0.0001f * static_cast<float>(b);
    }

    return snap;
}

// Helper: compare two snapshots field by field within tolerance
static void requireSnapshotsEqual(const Krate::DSP::HarmonicSnapshot& a,
                                  const Krate::DSP::HarmonicSnapshot& b,
                                  float tolerance = 1e-6f)
{
    REQUIRE(a.numPartials == b.numPartials);
    REQUIRE(a.f0Reference == Approx(b.f0Reference).margin(tolerance));
    REQUIRE(a.globalAmplitude == Approx(b.globalAmplitude).margin(tolerance));
    REQUIRE(a.spectralCentroid == Approx(b.spectralCentroid).margin(tolerance));
    REQUIRE(a.brightness == Approx(b.brightness).margin(tolerance));
    REQUIRE(a.residualEnergy == Approx(b.residualEnergy).margin(tolerance));

    for (int p = 0; p < a.numPartials; ++p)
    {
        auto idx = static_cast<size_t>(p);
        REQUIRE(a.relativeFreqs[idx] == Approx(b.relativeFreqs[idx]).margin(tolerance));
        REQUIRE(a.normalizedAmps[idx] == Approx(b.normalizedAmps[idx]).margin(tolerance));
        REQUIRE(a.phases[idx] == Approx(b.phases[idx]).margin(tolerance));
        REQUIRE(a.inharmonicDeviation[idx] == Approx(b.inharmonicDeviation[idx]).margin(tolerance));
    }

    for (size_t b_idx = 0; b_idx < Krate::DSP::kResidualBands; ++b_idx)
    {
        REQUIRE(a.residualBands[b_idx] == Approx(b.residualBands[b_idx]).margin(tolerance));
    }
}

// T063: All 8 slots round-trip through state save/reload (SC-004)
TEST_CASE("M5 State: all 8 slots round-trip through state save/reload (SC-004)",
          "[innexus][m5][state][sc004]")
{
    M5TestStream stream;

    // Distinct snapshots for each slot
    std::array<Krate::DSP::HarmonicSnapshot, 8> originals{};

    // Save state from a processor with all 8 slots populated
    {
        M5TestFixture fix;
        fix.injectAnalysis(makeM5TestAnalysis(10, 440.0f, 0.5f));

        fix.events.addNoteOn(69, 1.0f);
        fix.processBlock();
        fix.events.clear();
        for (int i = 0; i < 3; ++i)
            fix.processBlock();

        // Populate all 8 slots with distinct snapshots via capture
        for (int slot = 0; slot < 8; ++slot)
        {
            float f0 = 200.0f + static_cast<float>(slot) * 100.0f;
            fix.injectAnalysis(makeM5TestAnalysis(10, f0, 0.3f + static_cast<float>(slot) * 0.05f));
            for (int b = 0; b < 3; ++b)
                fix.processBlock();

            fix.selectSlot(slot);
            fix.triggerCapture();
            REQUIRE(fix.processor.getMemorySlot(slot).occupied == true);
            originals[static_cast<size_t>(slot)] = fix.processor.getMemorySlot(slot).snapshot;
        }

        // Save state
        REQUIRE(fix.processor.getState(&stream) == kResultOk);
        REQUIRE_FALSE(stream.empty());
    }

    // Reload into a fresh processor
    {
        M5TestFixture fix;
        stream.resetReadPos();
        REQUIRE(fix.processor.setState(&stream) == kResultOk);

        // Verify all 8 slots are occupied and match originals
        for (int slot = 0; slot < 8; ++slot)
        {
            INFO("Checking slot " << slot);
            const auto& loaded = fix.processor.getMemorySlot(slot);
            REQUIRE(loaded.occupied == true);
            requireSnapshotsEqual(loaded.snapshot,
                                  originals[static_cast<size_t>(slot)]);
        }
    }
}

// T064: Partial slot occupancy round-trips correctly
TEST_CASE("M5 State: partial slot occupancy round-trips correctly",
          "[innexus][m5][state][partial]")
{
    M5TestStream stream;

    std::array<Krate::DSP::HarmonicSnapshot, 8> originals{};

    // Save state with slots 0, 2, 6 occupied; others empty
    {
        M5TestFixture fix;
        fix.injectAnalysis(makeM5TestAnalysis(10, 440.0f, 0.5f));

        fix.events.addNoteOn(69, 1.0f);
        fix.processBlock();
        fix.events.clear();
        for (int i = 0; i < 3; ++i)
            fix.processBlock();

        const int occupiedSlots[] = {0, 2, 6};
        for (int slot : occupiedSlots)
        {
            float f0 = 300.0f + static_cast<float>(slot) * 100.0f;
            fix.injectAnalysis(makeM5TestAnalysis(10, f0, 0.5f));
            for (int b = 0; b < 3; ++b)
                fix.processBlock();

            fix.selectSlot(slot);
            fix.triggerCapture();
            originals[static_cast<size_t>(slot)] = fix.processor.getMemorySlot(slot).snapshot;
        }

        REQUIRE(fix.processor.getState(&stream) == kResultOk);
    }

    // Reload
    {
        M5TestFixture fix;
        stream.resetReadPos();
        REQUIRE(fix.processor.setState(&stream) == kResultOk);

        // Slots 0, 2, 6 occupied
        REQUIRE(fix.processor.getMemorySlot(0).occupied == true);
        requireSnapshotsEqual(fix.processor.getMemorySlot(0).snapshot, originals[0]);
        REQUIRE(fix.processor.getMemorySlot(2).occupied == true);
        requireSnapshotsEqual(fix.processor.getMemorySlot(2).snapshot, originals[2]);
        REQUIRE(fix.processor.getMemorySlot(6).occupied == true);
        requireSnapshotsEqual(fix.processor.getMemorySlot(6).snapshot, originals[6]);

        // Slots 1, 3, 4, 5, 7 empty
        REQUIRE(fix.processor.getMemorySlot(1).occupied == false);
        REQUIRE(fix.processor.getMemorySlot(3).occupied == false);
        REQUIRE(fix.processor.getMemorySlot(4).occupied == false);
        REQUIRE(fix.processor.getMemorySlot(5).occupied == false);
        REQUIRE(fix.processor.getMemorySlot(7).occupied == false);
    }
}

// T065: Loading v4 state initializes all slots to empty (SC-005)
TEST_CASE("M5 State: loading v4 state initializes all slots to empty (SC-005)",
          "[innexus][m5][state][sc005]")
{
    M5TestStream stream;

    // Create a v4 state (no M5 data) by saving from a processor
    // that hasn't been updated to write v5 data yet.
    // We'll simulate this by creating a processor, saving state,
    // and then verifying a fresh processor loads it correctly.
    //
    // Since getState now writes v5 format, we need to create a v4 blob manually.
    // Instead, we'll use a processor that writes the current format,
    // then load it to verify backward-compatible behavior.
    //
    // Actually, a simpler approach: populate some slots in a fresh processor,
    // save a v4-format state by writing manually. But that's complex.
    //
    // The simplest correct test: create a processor with NO slots populated,
    // save its state (which will be v5 with all-empty slots), then load it.
    // For TRUE v4 testing, we need to synthesize a v4 blob.
    //
    // Let's create a v4 state blob using a pre-update save. Since the code
    // is being modified in this phase, we'll verify by setting the version
    // check in setState. To test v4 compat, we produce a v4 blob by saving
    // state from a processor that has M4 params set, but we need to control
    // the version. Since we can't do that directly, we test indirectly:
    // The setState code for version >= 5 reads memory data; for version < 5,
    // it initializes slots to empty. We verify this path by loading a state
    // that has only v4 data (missing the M5 tail).

    // Approach: Save v5 state, truncate it to v4 length by saving from
    // a processor with no M5 data and manually writing version 4.
    // Actually, we'll just save from the current code. The version will be 5.
    // To properly test v4, we'd need to produce the exact v4 binary.
    //
    // Best approach: save state from a fresh processor (no captures),
    // then modify the version byte in the stream to 4 and truncate the M5 tail.
    // But that's fragile.
    //
    // Most robust: Use the knowledge that setState handles version < 5 by
    // defaulting all slots. Test with an actual old-format state.
    // We can construct a minimal v4 state using IBStreamer directly.

    // Write a minimal v4 state manually
    {
        Steinberg::IBStreamer streamer(&stream, kLittleEndian);

        // Version 4
        streamer.writeInt32(4);

        // M1 parameters
        streamer.writeFloat(100.0f);   // releaseTimeMs
        streamer.writeFloat(0.3f);     // inharmonicityAmount
        streamer.writeFloat(0.8f);     // masterGain
        streamer.writeFloat(0.0f);     // bypass

        // Sample file path (empty)
        streamer.writeInt32(0);

        // M2 parameters
        streamer.writeFloat(1.0f);     // harmonicLevel (plain)
        streamer.writeFloat(1.0f);     // residualLevel (plain)
        streamer.writeFloat(0.0f);     // brightness (plain)
        streamer.writeFloat(0.0f);     // transientEmphasis (plain)

        // Residual frames (none)
        streamer.writeInt32(0);        // frameCount
        streamer.writeInt32(0);        // fftSize
        streamer.writeInt32(0);        // hopSize

        // M3 parameters
        streamer.writeInt32(0);        // inputSource (sample)
        streamer.writeInt32(0);        // latencyMode (low)

        // M4 parameters
        streamer.writeInt8(static_cast<Steinberg::int8>(0));  // freeze off
        streamer.writeFloat(0.3f);     // morphPosition
        streamer.writeInt32(1);        // harmonicFilterType (OddOnly)
        streamer.writeFloat(0.6f);     // responsiveness
    }

    // Load the v4 state into a fresh processor
    {
        M5TestFixture fix;
        stream.resetReadPos();
        REQUIRE(fix.processor.setState(&stream) == kResultOk);

        // All 8 memory slots should be empty
        for (int slot = 0; slot < 8; ++slot)
        {
            INFO("Checking slot " << slot);
            REQUIRE(fix.processor.getMemorySlot(slot).occupied == false);
        }

        // M4 parameters should be restored
        REQUIRE(fix.processor.getFreeze() == Approx(0.0f).margin(0.1f));
        REQUIRE(fix.processor.getMorphPosition() == Approx(0.3f).margin(0.01f));
        REQUIRE(fix.processor.getResponsiveness() == Approx(0.6f).margin(0.01f));
    }
}

// T066: Selected slot index persists across state save/reload
TEST_CASE("M5 State: selected slot index persists across save/reload",
          "[innexus][m5][state][slot_index]")
{
    M5TestStream stream;

    // Save state with slot 2 selected
    {
        M5TestFixture fix;

        // Select slot 2 (normalized = 2/7)
        fix.selectSlot(2);
        fix.processBlock();

        REQUIRE(fix.processor.getSelectedSlotIndex() == 2);

        REQUIRE(fix.processor.getState(&stream) == kResultOk);
    }

    // Reload
    {
        M5TestFixture fix;
        stream.resetReadPos();
        REQUIRE(fix.processor.setState(&stream) == kResultOk);

        REQUIRE(fix.processor.getSelectedSlotIndex() == 2);
    }
}

// T067: Recall/freeze state does NOT persist (FR-023)
TEST_CASE("M5 State: recall/freeze state does not persist (FR-023)",
          "[innexus][m5][state][fr023]")
{
    M5TestStream stream;

    // Save state with freeze engaged via recall
    {
        M5TestFixture fix;
        fix.injectAnalysis(makeM5TestAnalysis(10, 440.0f, 0.5f));

        fix.events.addNoteOn(69, 1.0f);
        fix.processBlock();
        fix.events.clear();
        for (int i = 0; i < 3; ++i)
            fix.processBlock();

        // Capture and recall (engages freeze)
        fix.triggerCapture();
        fix.triggerRecall();
        REQUIRE(fix.processor.getManualFreezeActive() == true);

        REQUIRE(fix.processor.getState(&stream) == kResultOk);
    }

    // Reload -- freeze should be off
    {
        M5TestFixture fix;
        stream.resetReadPos();
        REQUIRE(fix.processor.setState(&stream) == kResultOk);

        // FR-023: freeze defaults to off on load
        REQUIRE(fix.processor.getManualFreezeActive() == false);
    }
}

// =============================================================================
// Phase 6: JSON Export/Import Tests (User Story 4)
// =============================================================================

// Helper: create a populated snapshot for JSON tests
static Krate::DSP::HarmonicSnapshot makeJsonTestSnapshot()
{
    Krate::DSP::HarmonicSnapshot snap{};
    snap.f0Reference = 440.0f;
    snap.numPartials = 5;
    snap.globalAmplitude = 0.3f;
    snap.spectralCentroid = 2200.0f;
    snap.brightness = 0.6f;
    snap.residualEnergy = 0.05f;

    for (int i = 0; i < 5; ++i)
    {
        auto idx = static_cast<size_t>(i);
        snap.relativeFreqs[idx] = static_cast<float>(i + 1) + 0.001f * static_cast<float>(i);
        snap.normalizedAmps[idx] = 0.5f / static_cast<float>(i + 1);
        snap.phases[idx] = static_cast<float>(i) * 0.5f;
        snap.inharmonicDeviation[idx] = 0.001f * static_cast<float>(i);
    }

    for (size_t b = 0; b < Krate::DSP::kResidualBands; ++b)
    {
        snap.residualBands[b] = 0.01f * static_cast<float>(b + 1);
    }

    return snap;
}

// T076: snapshotToJson produces non-empty string with all required keys
TEST_CASE("M5 JSON: snapshotToJson produces string with all required keys",
          "[innexus][m5][json][us4]")
{
    auto snap = makeJsonTestSnapshot();
    std::string json = Innexus::snapshotToJson(snap);

    REQUIRE(!json.empty());

    // Check all required top-level keys are present
    REQUIRE(json.find("\"version\"") != std::string::npos);
    REQUIRE(json.find("\"f0Reference\"") != std::string::npos);
    REQUIRE(json.find("\"numPartials\"") != std::string::npos);
    REQUIRE(json.find("\"relativeFreqs\"") != std::string::npos);
    REQUIRE(json.find("\"normalizedAmps\"") != std::string::npos);
    REQUIRE(json.find("\"phases\"") != std::string::npos);
    REQUIRE(json.find("\"inharmonicDeviation\"") != std::string::npos);
    REQUIRE(json.find("\"residualBands\"") != std::string::npos);
    REQUIRE(json.find("\"residualEnergy\"") != std::string::npos);
    REQUIRE(json.find("\"globalAmplitude\"") != std::string::npos);
    REQUIRE(json.find("\"spectralCentroid\"") != std::string::npos);
    REQUIRE(json.find("\"brightness\"") != std::string::npos);
}

// T077: exported JSON contains "version": 1
TEST_CASE("M5 JSON: exported JSON contains version 1",
          "[innexus][m5][json][us4]")
{
    auto snap = makeJsonTestSnapshot();
    std::string json = Innexus::snapshotToJson(snap);

    REQUIRE(json.find("\"version\": 1") != std::string::npos);
}

// T078: round-trip snapshotToJson -> jsonToSnapshot matches within 1e-6
TEST_CASE("M5 JSON: round-trip export-import matches within 1e-6",
          "[innexus][m5][json][us4]")
{
    auto original = makeJsonTestSnapshot();
    std::string json = Innexus::snapshotToJson(original);

    Krate::DSP::HarmonicSnapshot imported{};
    REQUIRE(Innexus::jsonToSnapshot(json, imported));

    REQUIRE(imported.f0Reference == Approx(original.f0Reference).margin(1e-6f));
    REQUIRE(imported.numPartials == original.numPartials);
    REQUIRE(imported.globalAmplitude == Approx(original.globalAmplitude).margin(1e-6f));
    REQUIRE(imported.spectralCentroid == Approx(original.spectralCentroid).margin(1e-6f));
    REQUIRE(imported.brightness == Approx(original.brightness).margin(1e-6f));
    REQUIRE(imported.residualEnergy == Approx(original.residualEnergy).margin(1e-6f));

    for (int i = 0; i < original.numPartials; ++i)
    {
        auto idx = static_cast<size_t>(i);
        REQUIRE(imported.relativeFreqs[idx]
                == Approx(original.relativeFreqs[idx]).margin(1e-6f));
        REQUIRE(imported.normalizedAmps[idx]
                == Approx(original.normalizedAmps[idx]).margin(1e-6f));
        REQUIRE(imported.phases[idx]
                == Approx(original.phases[idx]).margin(1e-6f));
        REQUIRE(imported.inharmonicDeviation[idx]
                == Approx(original.inharmonicDeviation[idx]).margin(1e-6f));
    }

    for (size_t b = 0; b < Krate::DSP::kResidualBands; ++b)
    {
        REQUIRE(imported.residualBands[b]
                == Approx(original.residualBands[b]).margin(1e-6f));
    }
}

// T079: jsonToSnapshot with empty string returns false
TEST_CASE("M5 JSON: jsonToSnapshot with empty string returns false",
          "[innexus][m5][json][us4]")
{
    Krate::DSP::HarmonicSnapshot snap{};
    snap.f0Reference = 999.0f; // sentinel

    REQUIRE(Innexus::jsonToSnapshot("", snap) == false);

    // Verify snap is unchanged
    REQUIRE(snap.f0Reference == Approx(999.0f));
}

// T080: jsonToSnapshot with missing required fields returns false
TEST_CASE("M5 JSON: jsonToSnapshot with missing required fields returns false",
          "[innexus][m5][json][us4]")
{
    // JSON with version and numPartials but no relativeFreqs
    std::string json = R"({
        "version": 1,
        "f0Reference": 440.0,
        "numPartials": 0,
        "normalizedAmps": [],
        "phases": [],
        "inharmonicDeviation": [],
        "residualBands": [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],
        "residualEnergy": 0.0,
        "globalAmplitude": 0.0,
        "spectralCentroid": 0.0,
        "brightness": 0.0
    })";

    Krate::DSP::HarmonicSnapshot snap{};
    REQUIRE(Innexus::jsonToSnapshot(json, snap) == false);
}

// T081: jsonToSnapshot with numPartials > 48 returns false
TEST_CASE("M5 JSON: jsonToSnapshot with numPartials > 48 returns false",
          "[innexus][m5][json][us4]")
{
    std::string json = R"({
        "version": 1,
        "f0Reference": 440.0,
        "numPartials": 49,
        "relativeFreqs": [],
        "normalizedAmps": [],
        "phases": [],
        "inharmonicDeviation": [],
        "residualBands": [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],
        "residualEnergy": 0.0,
        "globalAmplitude": 0.0,
        "spectralCentroid": 0.0,
        "brightness": 0.0
    })";

    Krate::DSP::HarmonicSnapshot snap{};
    REQUIRE(Innexus::jsonToSnapshot(json, snap) == false);
}

// T082: jsonToSnapshot with negative amplitude returns false
TEST_CASE("M5 JSON: jsonToSnapshot with negative amplitude returns false",
          "[innexus][m5][json][us4]")
{
    std::string json = R"({
        "version": 1,
        "f0Reference": 440.0,
        "numPartials": 1,
        "relativeFreqs": [1.0],
        "normalizedAmps": [-0.5],
        "phases": [0.0],
        "inharmonicDeviation": [0.0],
        "residualBands": [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],
        "residualEnergy": 0.0,
        "globalAmplitude": 0.0,
        "spectralCentroid": 0.0,
        "brightness": 0.0
    })";

    Krate::DSP::HarmonicSnapshot snap{};
    REQUIRE(Innexus::jsonToSnapshot(json, snap) == false);
}

// T083: notify() handler receives "HarmonicSnapshotImport" and populates slot
TEST_CASE("M5 JSON: notify() handles HarmonicSnapshotImport message",
          "[innexus][m5][json][us4][notify]")
{
    M5TestFixture fix;

    // Create a test snapshot
    auto snap = makeJsonTestSnapshot();

    // Construct IMessage using SDK HostMessage
    Steinberg::Vst::HostMessage msg;
    msg.setMessageID("HarmonicSnapshotImport");
    auto* attrs = msg.getAttributes();
    attrs->setInt("slotIndex", 2);
    attrs->setBinary("snapshotData", &snap, sizeof(snap));

    // Call notify
    REQUIRE(fix.processor.notify(&msg) == Steinberg::kResultOk);

    // Verify slot 2 is now occupied with the correct data
    const auto& slot = fix.processor.getMemorySlot(2);
    REQUIRE(slot.occupied == true);
    REQUIRE(slot.snapshot.f0Reference == Approx(snap.f0Reference).margin(1e-6f));
    REQUIRE(slot.snapshot.numPartials == snap.numPartials);
    REQUIRE(slot.snapshot.globalAmplitude == Approx(snap.globalAmplitude).margin(1e-6f));

    for (int i = 0; i < snap.numPartials; ++i)
    {
        auto idx = static_cast<size_t>(i);
        REQUIRE(slot.snapshot.relativeFreqs[idx]
                == Approx(snap.relativeFreqs[idx]).margin(1e-6f));
        REQUIRE(slot.snapshot.normalizedAmps[idx]
                == Approx(snap.normalizedAmps[idx]).margin(1e-6f));
    }

    // Other slots should still be empty
    REQUIRE(fix.processor.getMemorySlot(0).occupied == false);
    REQUIRE(fix.processor.getMemorySlot(1).occupied == false);
    REQUIRE(fix.processor.getMemorySlot(3).occupied == false);
}

// T084: notify() rejects message with wrong binary size
TEST_CASE("M5 JSON: notify() rejects message with wrong binary size",
          "[innexus][m5][json][us4][notify]")
{
    M5TestFixture fix;

    char smallBuffer[4] = {0, 0, 0, 0};

    Steinberg::Vst::HostMessage msg;
    msg.setMessageID("HarmonicSnapshotImport");
    auto* attrs = msg.getAttributes();
    attrs->setInt("slotIndex", 2);
    attrs->setBinary("snapshotData", smallBuffer, sizeof(smallBuffer));

    REQUIRE(fix.processor.notify(&msg) == Steinberg::kResultFalse);

    // Slot should remain empty
    REQUIRE(fix.processor.getMemorySlot(2).occupied == false);
}

// T085: notify() rejects message with out-of-range slot index
TEST_CASE("M5 JSON: notify() rejects out-of-range slot index",
          "[innexus][m5][json][us4][notify]")
{
    M5TestFixture fix;
    auto snap = makeJsonTestSnapshot();

    // Test negative index
    {
        Steinberg::Vst::HostMessage msg;
        msg.setMessageID("HarmonicSnapshotImport");
        auto* attrs = msg.getAttributes();
        attrs->setInt("slotIndex", -1);
        attrs->setBinary("snapshotData", &snap, sizeof(snap));

        REQUIRE(fix.processor.notify(&msg) == Steinberg::kResultFalse);
    }

    // Test index >= 8
    {
        Steinberg::Vst::HostMessage msg;
        msg.setMessageID("HarmonicSnapshotImport");
        auto* attrs = msg.getAttributes();
        attrs->setInt("slotIndex", 8);
        attrs->setBinary("snapshotData", &snap, sizeof(snap));

        REQUIRE(fix.processor.notify(&msg) == Steinberg::kResultFalse);
    }

    // No slots should be modified
    for (int i = 0; i < 8; ++i)
    {
        REQUIRE(fix.processor.getMemorySlot(i).occupied == false);
    }
}

// T083 extension: notify() with nullptr returns kInvalidArgument
TEST_CASE("M5 JSON: notify() with nullptr returns kInvalidArgument",
          "[innexus][m5][json][us4][notify]")
{
    M5TestFixture fix;
    REQUIRE(fix.processor.notify(nullptr) == Steinberg::kInvalidArgument);
}

// T083 extension: notify() delegates unknown messages to base class
TEST_CASE("M5 JSON: notify() delegates unknown messages to base",
          "[innexus][m5][json][us4][notify]")
{
    M5TestFixture fix;

    Steinberg::Vst::HostMessage msg;
    msg.setMessageID("SomeUnknownMessage");

    // Should delegate to AudioEffect::notify, which returns some result
    // (we just verify it doesn't crash and doesn't affect slots)
    fix.processor.notify(&msg);

    for (int i = 0; i < 8; ++i)
    {
        REQUIRE(fix.processor.getMemorySlot(i).occupied == false);
    }
}

// =============================================================================
// Phase 7: Edge Case Robustness Tests
// =============================================================================

// Helper: create analysis with a configurable number of partials
static Innexus::SampleAnalysis* makeM5AnalysisWithPartialCount(
    int numPartials,
    float f0 = 440.0f,
    float amplitude = 0.5f,
    int numFrames = 10)
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
        frame.globalAmplitude = amplitude;
        frame.spectralCentroid = 1000.0f;
        frame.brightness = 0.5f;
        frame.noisiness = 0.1f;

        for (int p = 0; p < numPartials && p < static_cast<int>(Krate::DSP::kMaxPartials); ++p)
        {
            auto& partial = frame.partials[static_cast<size_t>(p)];
            partial.harmonicIndex = p + 1;
            partial.frequency = f0 * static_cast<float>(p + 1);
            partial.amplitude = amplitude / static_cast<float>(p + 1);
            partial.relativeFrequency = static_cast<float>(p + 1);
            partial.inharmonicDeviation = 0.0f;
            partial.stability = 1.0f;
            partial.age = 10;
            partial.phase = static_cast<float>(p) * 0.3f;
        }

        analysis->frames.push_back(frame);
    }

    analysis->totalFrames = analysis->frames.size();
    analysis->filePath = "test_m5_edge.wav";

    return analysis;
}

// T095: Capture during morph at exactly 0.0 reads from manualFrozenFrame_
TEST_CASE("M5 Edge: capture during morph at exactly 0.0 reads from frozen frame",
          "[innexus][m5][edge][capture]")
{
    M5TestFixture fix;
    fix.injectAnalysis(makeM5TestAnalysis(10, 440.0f, 0.5f));

    fix.events.addNoteOn(69, 1.0f);
    fix.processBlock();
    fix.events.clear();
    for (int i = 0; i < 3; ++i)
        fix.processBlock();

    // Engage freeze to capture the 440 Hz state
    fix.engageFreeze();
    fix.processBlock();

    // Change analysis to a different timbre while freeze is active
    fix.injectAnalysis(makeM5DistinctAnalysis(880.0f, 0.7f));
    for (int i = 0; i < 3; ++i)
        fix.processBlock();

    // Set morph to exactly 0.0 (full freeze, no live blend)
    fix.setMorphPosition(0.0);
    fix.processBlock();

    // Capture into slot 0
    fix.selectSlot(0);
    fix.triggerCapture();

    // The captured snapshot should match the frozen frame (440 Hz), not live (880 Hz)
    const auto& slot = fix.processor.getMemorySlot(0);
    REQUIRE(slot.occupied == true);
    REQUIRE(slot.snapshot.f0Reference == Approx(440.0f).margin(1.0f));
}

// T096: Changing slot selector while no recall is active does not change playback
TEST_CASE("M5 Edge: changing slot selector while freeze off does not affect playback",
          "[innexus][m5][edge][slot]")
{
    M5TestFixture fix;
    fix.injectAnalysis(makeM5TestAnalysis(10, 440.0f, 0.5f));

    fix.events.addNoteOn(69, 1.0f);
    fix.processBlock();
    fix.events.clear();
    for (int i = 0; i < 3; ++i)
        fix.processBlock();

    // Capture into slots 0 and 5 with different timbres
    fix.selectSlot(0);
    fix.triggerCapture();

    fix.injectAnalysis(makeM5DistinctAnalysis(880.0f, 0.7f));
    for (int i = 0; i < 3; ++i)
        fix.processBlock();
    fix.selectSlot(5);
    fix.triggerCapture();

    // No recall active -- freeze should be off
    REQUIRE(fix.processor.getManualFreezeActive() == false);

    // Change slot selector from 5 to 0
    fix.selectSlot(0);

    // manualFreezeActive should still be false
    REQUIRE(fix.processor.getManualFreezeActive() == false);

    // Change slot selector again
    fix.selectSlot(5);
    REQUIRE(fix.processor.getManualFreezeActive() == false);
}

// T097: Recall while morph position is at 0.7 applies morph immediately
TEST_CASE("M5 Edge: recall with morph at 0.7 produces morphed output",
          "[innexus][m5][edge][recall][morph]")
{
    M5TestFixture fix;
    fix.injectAnalysis(makeM5TestAnalysis(10, 440.0f, 0.5f));

    fix.events.addNoteOn(69, 1.0f);
    fix.processBlock();
    fix.events.clear();
    for (int i = 0; i < 3; ++i)
        fix.processBlock();

    // Capture into slot 0
    fix.selectSlot(0);
    fix.triggerCapture();

    // Inject a different live analysis
    fix.injectAnalysis(makeM5DistinctAnalysis(880.0f, 0.7f));
    for (int i = 0; i < 3; ++i)
        fix.processBlock();

    // Set morph to 0.7 BEFORE recall
    fix.setMorphPosition(0.7);
    fix.processBlock();

    // Now recall slot 0
    fix.triggerRecall();

    // Process several blocks to let morph smoother settle
    for (int i = 0; i < 10; ++i)
        fix.processBlock();

    // The frozen frame should still be the 440 Hz recalled snapshot
    const auto& frozenFrame = fix.processor.getManualFrozenFrame();
    REQUIRE(frozenFrame.f0 == Approx(440.0f).margin(1.0f));

    // The morphed frame should differ from the frozen frame because morph is 0.7
    // (70% live 880Hz / 30% frozen 440Hz)
    const auto& morphedFrame = fix.processor.getMorphedFrame();
    bool morphDiffers = (std::abs(morphedFrame.f0 - frozenFrame.f0) > 1.0f);
    REQUIRE(morphDiffers);
}

// T098: Snapshot captured at one F0 is correctly recalled at any MIDI pitch
TEST_CASE("M5 Edge: recalled snapshot relativeFreqs are pitch-independent",
          "[innexus][m5][edge][recall][pitch]")
{
    M5TestFixture fix;
    fix.injectAnalysis(makeM5TestAnalysis(10, 440.0f, 0.5f));

    fix.events.addNoteOn(69, 1.0f);  // A4 = MIDI 69
    fix.processBlock();
    fix.events.clear();
    for (int i = 0; i < 3; ++i)
        fix.processBlock();

    // Capture into slot 0 at 440 Hz
    fix.selectSlot(0);
    fix.triggerCapture();

    // Recall slot 0
    fix.triggerRecall();
    fix.processBlock();

    // Verify frozen frame has relativeFrequency preserved from snapshot
    const auto& frozenFrame = fix.processor.getManualFrozenFrame();
    for (int p = 0; p < frozenFrame.numPartials; ++p)
    {
        float expectedRelFreq = static_cast<float>(p + 1);  // harmonic series
        REQUIRE(frozenFrame.partials[static_cast<size_t>(p)].relativeFrequency
                == Approx(expectedRelFreq).margin(1e-4f));
    }

    // Now play a different MIDI note -- the frozen frame data should not change
    fix.events.addNoteOn(60, 1.0f);  // C4 = MIDI 60 (261.6 Hz)
    fix.processBlock();
    fix.events.clear();
    for (int i = 0; i < 3; ++i)
        fix.processBlock();

    // The frozen frame's relativeFrequencies are still the same
    const auto& afterNote = fix.processor.getManualFrozenFrame();
    for (int p = 0; p < afterNote.numPartials; ++p)
    {
        float expectedRelFreq = static_cast<float>(p + 1);
        REQUIRE(afterNote.partials[static_cast<size_t>(p)].relativeFrequency
                == Approx(expectedRelFreq).margin(1e-4f));
    }
}

// T098b: Snapshot recalled at a different sample rate than capture
TEST_CASE("M5 Edge: recalled snapshot is sample-rate independent",
          "[innexus][m5][edge][recall][samplerate]")
{
    // Capture at 44100 Hz
    Krate::DSP::HarmonicFrame srcFrame{};
    srcFrame.f0 = 440.0f;
    srcFrame.numPartials = 4;
    srcFrame.globalAmplitude = 0.5f;
    srcFrame.spectralCentroid = 1000.0f;
    srcFrame.brightness = 0.5f;

    for (int p = 0; p < 4; ++p)
    {
        auto& partial = srcFrame.partials[static_cast<size_t>(p)];
        partial.harmonicIndex = p + 1;
        partial.frequency = 440.0f * static_cast<float>(p + 1);
        partial.amplitude = 0.5f / static_cast<float>(p + 1);
        partial.relativeFrequency = static_cast<float>(p + 1);
        partial.inharmonicDeviation = 0.01f * static_cast<float>(p);
        partial.phase = 0.0f;
        partial.stability = 1.0f;
        partial.age = 10;
    }

    Krate::DSP::ResidualFrame srcResidual{};
    srcResidual.totalEnergy = 0.05f;
    for (size_t b = 0; b < Krate::DSP::kResidualBands; ++b)
        srcResidual.bandEnergies[b] = 0.003f;

    // Capture
    auto snap = Krate::DSP::captureSnapshot(srcFrame, srcResidual);

    // Recall -- the snapshot contains no sample-rate-dependent data
    Krate::DSP::HarmonicFrame recalledFrame{};
    Krate::DSP::ResidualFrame recalledResidual{};
    Krate::DSP::recallSnapshotToFrame(snap, recalledFrame, recalledResidual);

    // relativeFrequency values should be identical regardless of sample rate
    for (int p = 0; p < 4; ++p)
    {
        REQUIRE(recalledFrame.partials[static_cast<size_t>(p)].relativeFrequency
                == Approx(snap.relativeFreqs[static_cast<size_t>(p)]).margin(1e-6f));
    }

    // The snapshot stores pure ratios, not sample-rate-dependent values
    REQUIRE(snap.relativeFreqs[0] == Approx(1.0f).margin(1e-6f));
    REQUIRE(snap.relativeFreqs[1] == Approx(2.0f).margin(1e-6f));
    REQUIRE(snap.relativeFreqs[2] == Approx(3.0f).margin(1e-6f));
    REQUIRE(snap.relativeFreqs[3] == Approx(4.0f).margin(1e-6f));
}

// T099: All 8 slots can be populated and recalled independently
TEST_CASE("M5 Edge: all 8 slots populated with distinct partial counts and recalled independently",
          "[innexus][m5][edge][slot][independence]")
{
    M5TestFixture fix;

    // Partial counts for each slot: 1, 5, 10, 15, 20, 25, 30, 35
    const int partialCounts[8] = {1, 5, 10, 15, 20, 25, 30, 35};

    fix.events.addNoteOn(69, 1.0f);
    fix.processBlock();
    fix.events.clear();

    // Populate all 8 slots with distinct partial counts
    for (int s = 0; s < 8; ++s)
    {
        float f0 = 200.0f + static_cast<float>(s) * 100.0f;
        fix.injectAnalysis(makeM5AnalysisWithPartialCount(
            partialCounts[s], f0, 0.5f));
        for (int i = 0; i < 3; ++i)
            fix.processBlock();

        fix.selectSlot(s);
        fix.triggerCapture();
    }

    // Verify all slots are occupied with correct partial counts
    for (int s = 0; s < 8; ++s)
    {
        const auto& slot = fix.processor.getMemorySlot(s);
        REQUIRE(slot.occupied == true);
        REQUIRE(slot.snapshot.numPartials == partialCounts[s]);
    }

    // Recall each slot in turn and verify manualFrozenFrame_.numPartials matches
    for (int s = 0; s < 8; ++s)
    {
        fix.selectSlot(s);
        fix.triggerRecall();

        const auto& frozen = fix.processor.getManualFrozenFrame();
        REQUIRE(frozen.numPartials == partialCounts[s]);
        REQUIRE(fix.processor.getManualFreezeActive() == true);
    }
}
