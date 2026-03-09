// ==============================================================================
// Memory Slot ADSR Capture/Recall/Morph Tests (Spec 124: T035, T036)
// ==============================================================================
// Unit tests for:
// - Slot capture stores all 9 ADSR values (T035)
// - Slot recall restores all 9 ADSR values (T035)
// - Slot defaults have adsrAmount=0.0 (T035)
// - Morph interpolation: geometric mean for times, linear for others (T036)
// - Evolution engine ADSR interpolation (T036)
//
// Test-first: these tests MUST fail initially.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"
#include "dsp/sample_analysis.h"
#include "dsp/evolution_engine.h"

#include <krate/dsp/processors/harmonic_snapshot.h>
#include <krate/dsp/processors/harmonic_types.h>

#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using Catch::Approx;

// =============================================================================
// Test Infrastructure
// =============================================================================

static constexpr double kSlotTestSampleRate = 44100.0;
static constexpr int32 kSlotTestBlockSize = 128;

static ProcessSetup makeSlotSetup(double sampleRate = kSlotTestSampleRate,
                                  int32 blockSize = kSlotTestBlockSize)
{
    ProcessSetup setup{};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = blockSize;
    setup.sampleRate = sampleRate;
    return setup;
}

static Innexus::SampleAnalysis* makeSlotTestAnalysis(
    int numFrames = 20,
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
    analysis->filePath = "test_slot_adsr.wav";
    return analysis;
}

// Minimal EventList for MIDI events
class SlotTestEventList : public IEventList
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

// Parameter changes container
class SlotTestParamChanges : public IParameterChanges
{
public:
    class ParamQueue : public IParamValueQueue
    {
    public:
        ParamQueue(ParamID id, ParamValue val) : id_(id), value_(val) {}

        tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
        uint32 PLUGIN_API addRef() override { return 1; }
        uint32 PLUGIN_API release() override { return 1; }

        ParamID PLUGIN_API getParameterId() override { return id_; }
        int32 PLUGIN_API getPointCount() override { return 1; }
        tresult PLUGIN_API getPoint(int32 index, int32& sampleOffset, ParamValue& value) override
        {
            if (index != 0) return kResultFalse;
            sampleOffset = 0;
            value = value_;
            return kResultTrue;
        }
        tresult PLUGIN_API addPoint(int32, ParamValue, int32&) override { return kResultFalse; }

    private:
        ParamID id_;
        ParamValue value_;
    };

    tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

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

    void addParam(ParamID id, ParamValue value)
    {
        queues_.emplace_back(id, value);
    }

    void clear() { queues_.clear(); }

private:
    std::vector<ParamQueue> queues_;
};

/// Process one block through the processor.
static void processSlotBlock(
    Innexus::Processor& proc,
    int32 numSamples,
    IEventList* events = nullptr,
    IParameterChanges* paramChanges = nullptr)
{
    std::vector<float> outL(static_cast<size_t>(numSamples), 0.0f);
    std::vector<float> outR(static_cast<size_t>(numSamples), 0.0f);
    float* outBuffers[2] = {outL.data(), outR.data()};

    AudioBusBuffers outBus{};
    outBus.numChannels = 2;
    outBus.channelBuffers32 = outBuffers;

    ProcessData data{};
    data.numSamples = numSamples;
    data.numOutputs = 1;
    data.outputs = &outBus;
    data.inputEvents = events;
    data.inputParameterChanges = paramChanges;

    proc.process(data);
}

/// Initialize and activate a processor for testing.
static void setupSlotProcessor(Innexus::Processor& proc,
                                double sampleRate = kSlotTestSampleRate)
{
    proc.initialize(nullptr);
    auto setup = makeSlotSetup(sampleRate);
    proc.setupProcessing(setup);
    proc.setActive(true);
}

/// Helper: compute normalized value for log-mapped time parameters.
/// Plain range [1, 5000] ms, log mapping: norm = log(plain) / log(5000)
static double timeToNorm(float plainMs)
{
    return static_cast<double>(std::log(plainMs) / std::log(5000.0f));
}

/// Helper: compute normalized value for TimeScale parameter.
/// Plain range [0.25, 4.0], linear mapping
static double timeScaleToNorm(float plain)
{
    return static_cast<double>((plain - 0.25f) / (4.0f - 0.25f));
}

/// Helper: compute normalized value for curve parameters.
/// Plain range [-1.0, +1.0], linear mapping
static double curveToNorm(float plain)
{
    return static_cast<double>((plain - (-1.0f)) / (1.0f - (-1.0f)));
}

// =============================================================================
// T035: Unit tests for MemorySlot ADSR capture/recall
// =============================================================================

TEST_CASE("Memory Slot ADSR: default slot has adsrAmount=0.0",
          "[adsr][slot][defaults]")
{
    Krate::DSP::MemorySlot slot{};
    REQUIRE(slot.adsrAmount == Approx(0.0f));
    REQUIRE(slot.adsrAttackMs == Approx(10.0f));
    REQUIRE(slot.adsrDecayMs == Approx(100.0f));
    REQUIRE(slot.adsrSustainLevel == Approx(1.0f));
    REQUIRE(slot.adsrReleaseMs == Approx(100.0f));
    REQUIRE(slot.adsrTimeScale == Approx(1.0f));
    REQUIRE(slot.adsrAttackCurve == Approx(0.0f));
    REQUIRE(slot.adsrDecayCurve == Approx(0.0f));
    REQUIRE(slot.adsrReleaseCurve == Approx(0.0f));
}

TEST_CASE("Memory Slot ADSR: capture stores all 9 ADSR values into slot",
          "[adsr][slot][capture]")
{
    Innexus::Processor proc;
    setupSlotProcessor(proc);

    auto* analysis = makeSlotTestAnalysis();
    proc.testInjectAnalysis(analysis);

    // Set all 9 ADSR parameters to specific non-default values
    SlotTestParamChanges params;
    params.addParam(Innexus::kAdsrAttackId, timeToNorm(250.0f));
    params.addParam(Innexus::kAdsrDecayId, timeToNorm(300.0f));
    params.addParam(Innexus::kAdsrSustainId, 0.6);
    params.addParam(Innexus::kAdsrReleaseId, timeToNorm(500.0f));
    params.addParam(Innexus::kAdsrAmountId, 0.8);
    params.addParam(Innexus::kAdsrTimeScaleId, timeScaleToNorm(2.0f));
    params.addParam(Innexus::kAdsrAttackCurveId, curveToNorm(0.5f));
    params.addParam(Innexus::kAdsrDecayCurveId, curveToNorm(-0.3f));
    params.addParam(Innexus::kAdsrReleaseCurveId, curveToNorm(0.7f));

    // Select slot 0 and apply params
    params.addParam(Innexus::kMemorySlotId, 0.0); // slot 0

    // Play a note so frames are active
    SlotTestEventList events;
    events.addNoteOn(60, 1.0f);
    processSlotBlock(proc, kSlotTestBlockSize, &events, &params);

    // Process a few blocks to ensure params are applied
    for (int b = 0; b < 3; ++b)
        processSlotBlock(proc, kSlotTestBlockSize);

    // Verify params were applied
    REQUIRE(proc.getAdsrAttackMs() == Approx(250.0f).margin(5.0f));
    REQUIRE(proc.getAdsrAmount() == Approx(0.8f).margin(0.01f));

    // Trigger capture to slot 0
    SlotTestParamChanges captureParams;
    captureParams.addParam(Innexus::kMemoryCaptureId, 1.0);
    processSlotBlock(proc, kSlotTestBlockSize, nullptr, &captureParams);

    // Verify slot 0 now contains the 9 ADSR values
    const auto& slot = proc.getMemorySlot(0);
    REQUIRE(slot.occupied);
    REQUIRE(slot.adsrAttackMs == Approx(250.0f).margin(5.0f));
    REQUIRE(slot.adsrDecayMs == Approx(300.0f).margin(5.0f));
    REQUIRE(slot.adsrSustainLevel == Approx(0.6f).margin(0.01f));
    REQUIRE(slot.adsrReleaseMs == Approx(500.0f).margin(5.0f));
    REQUIRE(slot.adsrAmount == Approx(0.8f).margin(0.01f));
    REQUIRE(slot.adsrTimeScale == Approx(2.0f).margin(0.05f));
    REQUIRE(slot.adsrAttackCurve == Approx(0.5f).margin(0.05f));
    REQUIRE(slot.adsrDecayCurve == Approx(-0.3f).margin(0.05f));
    REQUIRE(slot.adsrReleaseCurve == Approx(0.7f).margin(0.05f));
}

TEST_CASE("Memory Slot ADSR: recall restores all 9 ADSR values",
          "[adsr][slot][recall]")
{
    Innexus::Processor proc;
    setupSlotProcessor(proc);

    auto* analysis = makeSlotTestAnalysis();
    proc.testInjectAnalysis(analysis);

    // Set specific ADSR values
    SlotTestParamChanges params;
    params.addParam(Innexus::kAdsrAttackId, timeToNorm(400.0f));
    params.addParam(Innexus::kAdsrDecayId, timeToNorm(200.0f));
    params.addParam(Innexus::kAdsrSustainId, 0.3);
    params.addParam(Innexus::kAdsrReleaseId, timeToNorm(800.0f));
    params.addParam(Innexus::kAdsrAmountId, 0.9);
    params.addParam(Innexus::kAdsrTimeScaleId, timeScaleToNorm(1.5f));
    params.addParam(Innexus::kAdsrAttackCurveId, curveToNorm(-0.8f));
    params.addParam(Innexus::kAdsrDecayCurveId, curveToNorm(0.6f));
    params.addParam(Innexus::kAdsrReleaseCurveId, curveToNorm(-0.5f));
    params.addParam(Innexus::kMemorySlotId, 0.0 / 7.0); // slot 0

    SlotTestEventList events;
    events.addNoteOn(60, 1.0f);
    processSlotBlock(proc, kSlotTestBlockSize, &events, &params);
    for (int b = 0; b < 3; ++b)
        processSlotBlock(proc, kSlotTestBlockSize);

    // Capture to slot 0
    SlotTestParamChanges captureParams;
    captureParams.addParam(Innexus::kMemoryCaptureId, 1.0);
    processSlotBlock(proc, kSlotTestBlockSize, nullptr, &captureParams);

    // Now change ADSR values to something different
    SlotTestParamChanges newParams;
    newParams.addParam(Innexus::kAdsrAttackId, timeToNorm(10.0f));
    newParams.addParam(Innexus::kAdsrDecayId, timeToNorm(100.0f));
    newParams.addParam(Innexus::kAdsrSustainId, 1.0);
    newParams.addParam(Innexus::kAdsrReleaseId, timeToNorm(100.0f));
    newParams.addParam(Innexus::kAdsrAmountId, 0.0);
    newParams.addParam(Innexus::kAdsrTimeScaleId, timeScaleToNorm(1.0f));
    newParams.addParam(Innexus::kAdsrAttackCurveId, curveToNorm(0.0f));
    newParams.addParam(Innexus::kAdsrDecayCurveId, curveToNorm(0.0f));
    newParams.addParam(Innexus::kAdsrReleaseCurveId, curveToNorm(0.0f));
    processSlotBlock(proc, kSlotTestBlockSize, nullptr, &newParams);

    // Verify ADSR changed
    REQUIRE(proc.getAdsrAmount() == Approx(0.0f).margin(0.01f));

    // Recall slot 0
    SlotTestParamChanges recallParams;
    recallParams.addParam(Innexus::kMemoryRecallId, 1.0);
    processSlotBlock(proc, kSlotTestBlockSize, nullptr, &recallParams);

    // Verify all 9 ADSR values restored from slot 0
    REQUIRE(proc.getAdsrAttackMs() == Approx(400.0f).margin(5.0f));
    REQUIRE(proc.getAdsrDecayMs() == Approx(200.0f).margin(5.0f));
    REQUIRE(proc.getAdsrSustainLevel() == Approx(0.3f).margin(0.01f));
    REQUIRE(proc.getAdsrReleaseMs() == Approx(800.0f).margin(5.0f));
    REQUIRE(proc.getAdsrAmount() == Approx(0.9f).margin(0.01f));
    REQUIRE(proc.getAdsrTimeScale() == Approx(1.5f).margin(0.05f));
    REQUIRE(proc.getAdsrAttackCurve() == Approx(-0.8f).margin(0.05f));
    REQUIRE(proc.getAdsrDecayCurve() == Approx(0.6f).margin(0.05f));
    REQUIRE(proc.getAdsrReleaseCurve() == Approx(-0.5f).margin(0.05f));
}

// =============================================================================
// T036: Unit tests for ADSR morph interpolation
// =============================================================================

TEST_CASE("Memory Slot ADSR: morph at t=0.5 uses geometric mean for times, linear for sustain",
          "[adsr][slot][morph]")
{
    // Set up two MemorySlots directly with known ADSR values
    // Slot A: Attack=10ms, Decay=100ms, Sustain=0.3, Release=200ms
    // Slot B: Attack=500ms, Decay=50ms, Sustain=0.9, Release=1000ms
    //
    // Expected at t=0.5:
    //   Attack  = sqrt(10 * 500) = sqrt(5000) ~ 70.71ms
    //   Decay   = sqrt(100 * 50) = sqrt(5000) ~ 70.71ms
    //   Sustain = (0.3 + 0.9) / 2 = 0.6
    //   Release = sqrt(200 * 1000) = sqrt(200000) ~ 447.21ms

    Innexus::Processor proc;
    setupSlotProcessor(proc);

    auto* analysis = makeSlotTestAnalysis();
    proc.testInjectAnalysis(analysis);

    // Play a note to have active frames
    SlotTestEventList events;
    events.addNoteOn(60, 1.0f);
    processSlotBlock(proc, kSlotTestBlockSize, &events);

    // --- Capture slot 0 with ADSR A ---
    {
        SlotTestParamChanges params;
        params.addParam(Innexus::kMemorySlotId, 0.0 / 7.0); // slot 0
        params.addParam(Innexus::kAdsrAttackId, timeToNorm(10.0f));
        params.addParam(Innexus::kAdsrDecayId, timeToNorm(100.0f));
        params.addParam(Innexus::kAdsrSustainId, 0.3);
        params.addParam(Innexus::kAdsrReleaseId, timeToNorm(200.0f));
        params.addParam(Innexus::kAdsrAmountId, 0.0);
        params.addParam(Innexus::kAdsrTimeScaleId, timeScaleToNorm(1.0f));
        params.addParam(Innexus::kAdsrAttackCurveId, curveToNorm(0.0f));
        params.addParam(Innexus::kAdsrDecayCurveId, curveToNorm(0.0f));
        params.addParam(Innexus::kAdsrReleaseCurveId, curveToNorm(0.0f));
        processSlotBlock(proc, kSlotTestBlockSize, nullptr, &params);
        for (int b = 0; b < 2; ++b)
            processSlotBlock(proc, kSlotTestBlockSize);

        SlotTestParamChanges captureParams;
        captureParams.addParam(Innexus::kMemoryCaptureId, 1.0);
        processSlotBlock(proc, kSlotTestBlockSize, nullptr, &captureParams);
    }

    // --- Capture slot 1 with ADSR B ---
    {
        SlotTestParamChanges params;
        params.addParam(Innexus::kMemorySlotId, 1.0 / 7.0); // slot 1
        params.addParam(Innexus::kAdsrAttackId, timeToNorm(500.0f));
        params.addParam(Innexus::kAdsrDecayId, timeToNorm(50.0f));
        params.addParam(Innexus::kAdsrSustainId, 0.9);
        params.addParam(Innexus::kAdsrReleaseId, timeToNorm(1000.0f));
        params.addParam(Innexus::kAdsrAmountId, 1.0);
        params.addParam(Innexus::kAdsrTimeScaleId, timeScaleToNorm(1.0f));
        params.addParam(Innexus::kAdsrAttackCurveId, curveToNorm(0.0f));
        params.addParam(Innexus::kAdsrDecayCurveId, curveToNorm(0.0f));
        params.addParam(Innexus::kAdsrReleaseCurveId, curveToNorm(0.0f));
        processSlotBlock(proc, kSlotTestBlockSize, nullptr, &params);
        for (int b = 0; b < 2; ++b)
            processSlotBlock(proc, kSlotTestBlockSize);

        SlotTestParamChanges captureParams;
        captureParams.addParam(Innexus::kMemoryCaptureId, 1.0);
        processSlotBlock(proc, kSlotTestBlockSize, nullptr, &captureParams);
    }

    // Verify both slots captured
    REQUIRE(proc.getMemorySlot(0).occupied);
    REQUIRE(proc.getMemorySlot(1).occupied);
    REQUIRE(proc.getMemorySlot(0).adsrAttackMs == Approx(10.0f).margin(2.0f));
    REQUIRE(proc.getMemorySlot(1).adsrAttackMs == Approx(500.0f).margin(10.0f));

    // --- Recall slot 0 and enable freeze (needed for morph to work) ---
    {
        SlotTestParamChanges recallParams;
        recallParams.addParam(Innexus::kMemorySlotId, 0.0 / 7.0);
        recallParams.addParam(Innexus::kMemoryRecallId, 1.0);
        processSlotBlock(proc, kSlotTestBlockSize, nullptr, &recallParams);
    }

    // --- Enable evolution engine with 2 waypoints at position 0.5 ---
    // The evolution engine interpolates between occupied slots.
    // Set phase to 0.5 manually by using specific speed/depth/mode.
    // For a direct test, we use the EvolutionEngine getInterpolatedFrame API.

    // Direct test of the ADSR interpolation values in the slots:
    const auto& slotA = proc.getMemorySlot(0);
    const auto& slotB = proc.getMemorySlot(1);

    // Compute expected geometric mean for times at t=0.5
    auto geomMean = [](float a, float b) {
        return std::exp(0.5f * std::log(a) + 0.5f * std::log(b));
    };

    float expectedAttack = geomMean(slotA.adsrAttackMs, slotB.adsrAttackMs);
    float expectedDecay = geomMean(slotA.adsrDecayMs, slotB.adsrDecayMs);
    float expectedRelease = geomMean(slotA.adsrReleaseMs, slotB.adsrReleaseMs);

    // Expected linear interpolation for sustain
    float expectedSustain = 0.5f * slotA.adsrSustainLevel + 0.5f * slotB.adsrSustainLevel;

    REQUIRE(expectedAttack == Approx(70.71f).margin(1.0f));
    REQUIRE(expectedDecay == Approx(70.71f).margin(1.0f));
    REQUIRE(expectedSustain == Approx(0.6f).margin(0.01f));
    REQUIRE(expectedRelease == Approx(447.21f).margin(1.0f));

    // Now test that the evolution engine actually produces these values.
    // Set up evolution with 2 slots, position at 0.5
    std::array<Krate::DSP::MemorySlot, 8> testSlots{};
    testSlots[0] = slotA;
    testSlots[1] = slotB;

    Innexus::EvolutionEngine evo;
    evo.prepare(44100.0);
    evo.updateWaypoints(testSlots);
    evo.setMode(Innexus::EvolutionMode::Cycle);
    evo.setSpeed(0.0f); // No movement, we set phase manually
    evo.setDepth(1.0f);

    // getInterpolatedFrame currently only returns harmonic/residual data.
    // After T040b implementation, it should also return ADSR data.
    // For now, we test that the EvolutionEngine can be set up correctly
    // and that the getInterpolatedFrame method works.
    Krate::DSP::HarmonicFrame evoFrame{};
    Krate::DSP::ResidualFrame evoResidual{};
    Krate::DSP::MemorySlot interpolatedAdsr{};

    // Test the new getInterpolatedADSR method
    // This method should return interpolated ADSR values from the two slots
    bool valid = evo.getInterpolatedFrame(testSlots, evoFrame, evoResidual,
                                          &interpolatedAdsr);

    REQUIRE(valid);
    // With phase=0.0, depth=1.0, and 2 waypoints, position maps to slot 0
    // Advance to get to 0.5 position - we need to use setManualOffset instead
    // since speed=0.0 means no movement
    evo.setManualOffset(0.5f);
    evo.setDepth(1.0f);

    valid = evo.getInterpolatedFrame(testSlots, evoFrame, evoResidual,
                                     &interpolatedAdsr);
    REQUIRE(valid);

    // At position 0.5 with 2 waypoints, localT should be 0.5
    REQUIRE(interpolatedAdsr.adsrAttackMs == Approx(expectedAttack).margin(5.0f));
    REQUIRE(interpolatedAdsr.adsrDecayMs == Approx(expectedDecay).margin(5.0f));
    REQUIRE(interpolatedAdsr.adsrSustainLevel == Approx(expectedSustain).margin(0.01f));
    REQUIRE(interpolatedAdsr.adsrReleaseMs == Approx(expectedRelease).margin(5.0f));
}

TEST_CASE("Memory Slot ADSR: morph at t=0.5 for Amount (linear interpolation)",
          "[adsr][slot][morph]")
{
    Krate::DSP::MemorySlot slotA{};
    Krate::DSP::MemorySlot slotB{};
    slotA.occupied = true;
    slotB.occupied = true;
    slotA.adsrAmount = 0.0f;
    slotB.adsrAmount = 1.0f;

    // Set some default harmonic data
    slotA.snapshot.f0Reference = 440.0f;
    slotA.snapshot.numPartials = 1;
    slotA.snapshot.normalizedAmps[0] = 1.0f;
    slotA.snapshot.relativeFreqs[0] = 1.0f;
    slotB.snapshot.f0Reference = 440.0f;
    slotB.snapshot.numPartials = 1;
    slotB.snapshot.normalizedAmps[0] = 1.0f;
    slotB.snapshot.relativeFreqs[0] = 1.0f;

    std::array<Krate::DSP::MemorySlot, 8> slots{};
    slots[0] = slotA;
    slots[1] = slotB;

    Innexus::EvolutionEngine evo;
    evo.prepare(44100.0);
    evo.updateWaypoints(slots);
    evo.setSpeed(0.0f);
    evo.setDepth(1.0f);
    evo.setManualOffset(0.5f);

    Krate::DSP::HarmonicFrame frame{};
    Krate::DSP::ResidualFrame residual{};
    Krate::DSP::MemorySlot interpolatedAdsr{};

    bool valid = evo.getInterpolatedFrame(slots, frame, residual, &interpolatedAdsr);
    REQUIRE(valid);

    // Linear interpolation: (0.0 + 1.0) / 2 = 0.5
    REQUIRE(interpolatedAdsr.adsrAmount == Approx(0.5f).margin(0.01f));
}

TEST_CASE("Memory Slot ADSR: morph at t=0.5 for Curve Amounts (linear interpolation)",
          "[adsr][slot][morph]")
{
    Krate::DSP::MemorySlot slotA{};
    Krate::DSP::MemorySlot slotB{};
    slotA.occupied = true;
    slotB.occupied = true;

    slotA.adsrAttackCurve = -1.0f;
    slotA.adsrDecayCurve = 0.0f;
    slotA.adsrReleaseCurve = 0.5f;

    slotB.adsrAttackCurve = 1.0f;
    slotB.adsrDecayCurve = 0.8f;
    slotB.adsrReleaseCurve = -0.5f;

    // Set minimal harmonic data
    slotA.snapshot.f0Reference = 440.0f;
    slotA.snapshot.numPartials = 1;
    slotA.snapshot.normalizedAmps[0] = 1.0f;
    slotA.snapshot.relativeFreqs[0] = 1.0f;
    slotB.snapshot.f0Reference = 440.0f;
    slotB.snapshot.numPartials = 1;
    slotB.snapshot.normalizedAmps[0] = 1.0f;
    slotB.snapshot.relativeFreqs[0] = 1.0f;

    std::array<Krate::DSP::MemorySlot, 8> slots{};
    slots[0] = slotA;
    slots[1] = slotB;

    Innexus::EvolutionEngine evo;
    evo.prepare(44100.0);
    evo.updateWaypoints(slots);
    evo.setSpeed(0.0f);
    evo.setDepth(1.0f);
    evo.setManualOffset(0.5f);

    Krate::DSP::HarmonicFrame frame{};
    Krate::DSP::ResidualFrame residual{};
    Krate::DSP::MemorySlot interpolatedAdsr{};

    bool valid = evo.getInterpolatedFrame(slots, frame, residual, &interpolatedAdsr);
    REQUIRE(valid);

    // Linear interpolation at t=0.5:
    REQUIRE(interpolatedAdsr.adsrAttackCurve == Approx(0.0f).margin(0.01f));   // (-1 + 1) / 2
    REQUIRE(interpolatedAdsr.adsrDecayCurve == Approx(0.4f).margin(0.01f));    // (0 + 0.8) / 2
    REQUIRE(interpolatedAdsr.adsrReleaseCurve == Approx(0.0f).margin(0.01f));  // (0.5 + -0.5) / 2
}

TEST_CASE("Memory Slot ADSR: evolution engine smooth ADSR interpolation across slots",
          "[adsr][slot][evolution]")
{
    // Create 3 occupied slots with distinct ADSR values
    std::array<Krate::DSP::MemorySlot, 8> slots{};
    for (int i = 0; i < 3; ++i)
    {
        slots[static_cast<size_t>(i)].occupied = true;
        slots[static_cast<size_t>(i)].snapshot.f0Reference = 440.0f;
        slots[static_cast<size_t>(i)].snapshot.numPartials = 1;
        slots[static_cast<size_t>(i)].snapshot.normalizedAmps[0] = 1.0f;
        slots[static_cast<size_t>(i)].snapshot.relativeFreqs[0] = 1.0f;
    }

    slots[0].adsrAttackMs = 10.0f;
    slots[0].adsrSustainLevel = 0.2f;
    slots[1].adsrAttackMs = 100.0f;
    slots[1].adsrSustainLevel = 0.5f;
    slots[2].adsrAttackMs = 1000.0f;
    slots[2].adsrSustainLevel = 0.8f;

    Innexus::EvolutionEngine evo;
    evo.prepare(44100.0);
    evo.updateWaypoints(slots);
    evo.setSpeed(0.0f);
    evo.setDepth(1.0f);

    // Test at position 0.0 (should be slot 0)
    evo.setManualOffset(0.0f);
    Krate::DSP::HarmonicFrame frame{};
    Krate::DSP::ResidualFrame residual{};
    Krate::DSP::MemorySlot interp{};
    bool valid = evo.getInterpolatedFrame(slots, frame, residual, &interp);
    REQUIRE(valid);
    REQUIRE(interp.adsrAttackMs == Approx(10.0f).margin(1.0f));
    REQUIRE(interp.adsrSustainLevel == Approx(0.2f).margin(0.01f));

    // Test at position 0.5 (should be midpoint between slot 0 and slot 1)
    evo.setManualOffset(0.25f);
    valid = evo.getInterpolatedFrame(slots, frame, residual, &interp);
    REQUIRE(valid);
    // Geometric mean of 10 and 100 at t=0.5
    float expectedAttack = std::exp(0.5f * std::log(10.0f) + 0.5f * std::log(100.0f));
    REQUIRE(interp.adsrAttackMs == Approx(expectedAttack).margin(2.0f));
    // Linear interpolation of sustain at t=0.5
    float expectedSustain = 0.5f * 0.2f + 0.5f * 0.5f;
    REQUIRE(interp.adsrSustainLevel == Approx(expectedSustain).margin(0.01f));

    // Test at position 1.0 (should be slot 2)
    evo.setManualOffset(1.0f);
    valid = evo.getInterpolatedFrame(slots, frame, residual, &interp);
    REQUIRE(valid);
    REQUIRE(interp.adsrAttackMs == Approx(1000.0f).margin(1.0f));
    REQUIRE(interp.adsrSustainLevel == Approx(0.8f).margin(0.01f));
}
