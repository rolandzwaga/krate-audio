// ==============================================================================
// Integration Test: TranceGate Parameter Flow from Host to Engine
// ==============================================================================
// Verifies that every TranceGate parameter changed at the VST host level
// propagates through the full pipeline:
//   Host param → processParameterChanges() → RuinaeTranceGateParams atomics
//   → applyParamsToEngine() → engine_.setTranceGateParams() → audible effect
//
// Each test plays a note, applies parameter changes, and measures the audio
// output difference to confirm the parameter actually affected the engine.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"

#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/vsttypes.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

using Catch::Approx;

// =============================================================================
// Mocks (same pattern as param_flow_test.cpp)
// =============================================================================

namespace {

class TGParamValueQueue : public Steinberg::Vst::IParamValueQueue {
public:
    TGParamValueQueue(Steinberg::Vst::ParamID id, double value)
        : paramId_(id), value_(value) {}

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override {
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }

    Steinberg::Vst::ParamID PLUGIN_API getParameterId() override { return paramId_; }
    Steinberg::int32 PLUGIN_API getPointCount() override { return 1; }

    Steinberg::tresult PLUGIN_API getPoint(
        Steinberg::int32 index,
        Steinberg::int32& sampleOffset,
        Steinberg::Vst::ParamValue& value) override {
        if (index != 0) return Steinberg::kResultFalse;
        sampleOffset = 0;
        value = value_;
        return Steinberg::kResultTrue;
    }

    Steinberg::tresult PLUGIN_API addPoint(
        Steinberg::int32, Steinberg::Vst::ParamValue,
        Steinberg::int32&) override {
        return Steinberg::kResultFalse;
    }

private:
    Steinberg::Vst::ParamID paramId_;
    double value_;
};

class TGParamChanges : public Steinberg::Vst::IParameterChanges {
public:
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override {
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }

    Steinberg::int32 PLUGIN_API getParameterCount() override {
        return static_cast<Steinberg::int32>(queues_.size());
    }

    Steinberg::Vst::IParamValueQueue* PLUGIN_API getParameterData(
        Steinberg::int32 index) override {
        if (index < 0 || index >= static_cast<Steinberg::int32>(queues_.size()))
            return nullptr;
        return &queues_[static_cast<size_t>(index)];
    }

    Steinberg::Vst::IParamValueQueue* PLUGIN_API addParameterData(
        const Steinberg::Vst::ParamID&, Steinberg::int32&) override {
        return nullptr;
    }

    void addChange(Steinberg::Vst::ParamID id, double value) {
        queues_.emplace_back(id, value);
    }

private:
    std::vector<TGParamValueQueue> queues_;
};

class TGEmptyEventList : public Steinberg::Vst::IEventList {
public:
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override {
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }
    Steinberg::int32 PLUGIN_API getEventCount() override { return 0; }
    Steinberg::tresult PLUGIN_API getEvent(Steinberg::int32,
                                            Steinberg::Vst::Event&) override {
        return Steinberg::kResultFalse;
    }
    Steinberg::tresult PLUGIN_API addEvent(Steinberg::Vst::Event&) override {
        return Steinberg::kResultTrue;
    }
};

class TGNoteOnEvents : public Steinberg::Vst::IEventList {
public:
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void**) override {
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }
    Steinberg::int32 PLUGIN_API getEventCount() override { return sent_ ? 0 : 1; }
    Steinberg::tresult PLUGIN_API getEvent(Steinberg::int32 index,
                                            Steinberg::Vst::Event& e) override {
        if (index != 0 || sent_) return Steinberg::kResultFalse;
        e = {};
        e.type = Steinberg::Vst::Event::kNoteOnEvent;
        e.sampleOffset = 0;
        e.noteOn.channel = 0;
        e.noteOn.pitch = 60;
        e.noteOn.velocity = 0.8f;
        e.noteOn.noteId = -1;
        sent_ = true;
        return Steinberg::kResultTrue;
    }
    Steinberg::tresult PLUGIN_API addEvent(Steinberg::Vst::Event&) override {
        return Steinberg::kResultTrue;
    }
    bool sent_ = false;
};

// =============================================================================
// Test Fixture
// =============================================================================

struct TranceGateFixture {
    Ruinae::Processor processor;
    TGEmptyEventList emptyEvents;
    std::vector<float> outL;
    std::vector<float> outR;
    float* channelBuffers[2];
    Steinberg::Vst::AudioBusBuffers outputBus{};
    Steinberg::Vst::ProcessData data{};
    static constexpr size_t kBlockSize = 256;

    TranceGateFixture() : outL(kBlockSize, 0.0f), outR(kBlockSize, 0.0f) {
        channelBuffers[0] = outL.data();
        channelBuffers[1] = outR.data();
        outputBus.numChannels = 2;
        outputBus.channelBuffers32 = channelBuffers;

        data.processMode = Steinberg::Vst::kRealtime;
        data.symbolicSampleSize = Steinberg::Vst::kSample32;
        data.numSamples = static_cast<Steinberg::int32>(kBlockSize);
        data.numInputs = 0;
        data.inputs = nullptr;
        data.numOutputs = 1;
        data.outputs = &outputBus;
        data.inputEvents = &emptyEvents;
        data.processContext = nullptr;

        processor.initialize(nullptr);
        Steinberg::Vst::ProcessSetup setup{};
        setup.processMode = Steinberg::Vst::kRealtime;
        setup.symbolicSampleSize = Steinberg::Vst::kSample32;
        setup.sampleRate = 44100.0;
        setup.maxSamplesPerBlock = static_cast<Steinberg::int32>(kBlockSize);
        processor.setupProcessing(setup);
        processor.setActive(true);

        // Effects chain stays disabled — trance gate operates per-voice before
        // effects, and delay/reverb feedback accumulates energy over time which
        // confounds sequential energy comparisons.
    }

    ~TranceGateFixture() {
        processor.setActive(false);
        processor.terminate();
    }

    void processWithParams(TGParamChanges& params) {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        data.inputParameterChanges = &params;
        processor.process(data);
    }

    void startNote() {
        TGNoteOnEvents noteEvents;
        TGParamChanges emptyParams;
        data.inputEvents = &noteEvents;
        data.inputParameterChanges = &emptyParams;
        processor.process(data);
        data.inputEvents = &emptyEvents;
    }

    // Process N blocks and return total energy (sum of squares)
    double processBlocksAndMeasureEnergy(int numBlocks) {
        TGParamChanges empty;
        double totalEnergy = 0.0;
        for (int i = 0; i < numBlocks; ++i) {
            std::fill(outL.begin(), outL.end(), 0.0f);
            std::fill(outR.begin(), outR.end(), 0.0f);
            data.inputParameterChanges = &empty;
            processor.process(data);
            for (size_t s = 0; s < kBlockSize; ++s) {
                totalEnergy += static_cast<double>(outL[s]) * outL[s];
            }
        }
        return totalEnergy;
    }

    // Process N blocks, applying params on first block only, return energy
    double applyParamsAndMeasureEnergy(TGParamChanges& params, int numBlocks) {
        TGParamChanges empty;
        double totalEnergy = 0.0;
        for (int i = 0; i < numBlocks; ++i) {
            std::fill(outL.begin(), outL.end(), 0.0f);
            std::fill(outR.begin(), outR.end(), 0.0f);
            data.inputParameterChanges = (i == 0) ? &params : &empty;
            processor.process(data);
            for (size_t s = 0; s < kBlockSize; ++s) {
                totalEnergy += static_cast<double>(outL[s]) * outL[s];
            }
        }
        return totalEnergy;
    }

    float measurePeakLevel(int numBlocks) {
        TGParamChanges empty;
        float peak = 0.0f;
        for (int i = 0; i < numBlocks; ++i) {
            std::fill(outL.begin(), outL.end(), 0.0f);
            std::fill(outR.begin(), outR.end(), 0.0f);
            data.inputParameterChanges = &empty;
            processor.process(data);
            for (size_t s = 0; s < kBlockSize; ++s) {
                float a = std::abs(outL[s]);
                if (a > peak) peak = a;
            }
        }
        return peak;
    }
};

} // anonymous namespace

// =============================================================================
// Tests
// =============================================================================

TEST_CASE("TranceGate enable/disable affects audio output",
          "[trance_gate][param_flow][integration]") {
    TranceGateFixture f;
    f.startNote();

    // Let sound stabilize (amp envelope attack + a few blocks margin)
    f.processBlocksAndMeasureEnergy(30);

    // Measure baseline energy with gate OFF (default).
    double energyDisabled = f.processBlocksAndMeasureEnergy(50);
    CHECK(energyDisabled > 0.0);

    // Enable trance gate with alternating step pattern for maximum gating effect
    TGParamChanges enableGate;
    enableGate.addChange(Ruinae::kTranceGateEnabledId, 1.0);
    enableGate.addChange(Ruinae::kTranceGateDepthId, 1.0);
    enableGate.addChange(Ruinae::kTranceGateTempoSyncId, 0.0); // free-run
    enableGate.addChange(Ruinae::kTranceGateRateId, 0.5);       // mid-range rate
    // Set alternating step levels: 0,1,0,1,...
    for (int i = 0; i < 32; ++i) {
        enableGate.addChange(
            static_cast<Steinberg::Vst::ParamID>(Ruinae::kTranceGateStepLevel0Id + i),
            (i % 2 == 0) ? 0.0 : 1.0);
    }
    double energyEnabled = f.applyParamsAndMeasureEnergy(enableGate, 50);

    // Gated output should have noticeably less energy than ungated
    INFO("Energy disabled: " << energyDisabled << ", enabled: " << energyEnabled);
    CHECK(energyEnabled < energyDisabled * 0.9);
}

TEST_CASE("TranceGate depth parameter propagates",
          "[trance_gate][param_flow][integration]") {
    TranceGateFixture f;
    f.startNote();
    f.processBlocksAndMeasureEnergy(30);

    // Enable gate with depth=0 (should produce no audible gating)
    TGParamChanges gateDepthZero;
    gateDepthZero.addChange(Ruinae::kTranceGateEnabledId, 1.0);
    gateDepthZero.addChange(Ruinae::kTranceGateDepthId, 0.0);
    gateDepthZero.addChange(Ruinae::kTranceGateTempoSyncId, 0.0);
    gateDepthZero.addChange(Ruinae::kTranceGateRateId, 0.5);
    for (int i = 0; i < 32; ++i) {
        gateDepthZero.addChange(
            static_cast<Steinberg::Vst::ParamID>(Ruinae::kTranceGateStepLevel0Id + i),
            (i % 2 == 0) ? 0.0 : 1.0);
    }
    double energyDepthZero = f.applyParamsAndMeasureEnergy(gateDepthZero, 50);

    // Now set depth to 1.0 (full gating)
    TGParamChanges depthFull;
    depthFull.addChange(Ruinae::kTranceGateDepthId, 1.0);
    double energyDepthFull = f.applyParamsAndMeasureEnergy(depthFull, 50);

    // Depth=0 should have more energy than depth=1 (full gating removes signal)
    INFO("Energy depth=0: " << energyDepthZero << ", depth=1: " << energyDepthFull);
    CHECK(energyDepthZero > energyDepthFull * 1.05);
}

TEST_CASE("TranceGate rate parameter propagates in free-run mode",
          "[trance_gate][param_flow][integration]") {
    TranceGateFixture f;
    f.startNote();
    f.processBlocksAndMeasureEnergy(30);

    // Enable gate in free-run mode with step 0 = 1.0, all others = 0.0.
    // At slow rate, the gate lingers on step 0 (full signal) for a long time.
    // At fast rate, step 0 passes quickly → much less energy overall.
    TGParamChanges slowRate;
    slowRate.addChange(Ruinae::kTranceGateEnabledId, 1.0);
    slowRate.addChange(Ruinae::kTranceGateDepthId, 1.0);
    slowRate.addChange(Ruinae::kTranceGateTempoSyncId, 0.0);
    slowRate.addChange(Ruinae::kTranceGateRateId, 0.0); // 0.0 → 0.1 Hz (very slow)
    // numSteps=16, step 0 = 1.0, all others = 0.0
    slowRate.addChange(Ruinae::kTranceGateNumStepsId, 14.0 / 30.0); // 16 steps
    slowRate.addChange(Ruinae::kTranceGateStepLevel0Id, 1.0);
    for (int i = 1; i < 32; ++i) {
        slowRate.addChange(
            static_cast<Steinberg::Vst::ParamID>(Ruinae::kTranceGateStepLevel0Id + i),
            0.0);
    }
    double energySlow = f.applyParamsAndMeasureEnergy(slowRate, 50);

    // Now switch to fast rate (100 Hz) — step 0 flashes past, rest is silence
    TGParamChanges fastRate;
    fastRate.addChange(Ruinae::kTranceGateRateId, 1.0); // 1.0 → 100 Hz
    double energyFast = f.applyParamsAndMeasureEnergy(fastRate, 50);

    // Slow rate keeps the high step much longer → more energy
    INFO("Energy slow rate: " << energySlow << ", fast rate: " << energyFast);
    CHECK(energySlow > energyFast * 1.05);
}

TEST_CASE("TranceGate step levels propagate",
          "[trance_gate][param_flow][integration]") {
    TranceGateFixture f;
    f.startNote();
    f.processBlocksAndMeasureEnergy(30);

    // Enable gate with all steps at 1.0 (passthrough — gate has no effect)
    TGParamChanges allOnes;
    allOnes.addChange(Ruinae::kTranceGateEnabledId, 1.0);
    allOnes.addChange(Ruinae::kTranceGateDepthId, 1.0);
    allOnes.addChange(Ruinae::kTranceGateTempoSyncId, 0.0);
    allOnes.addChange(Ruinae::kTranceGateRateId, 0.5);
    for (int i = 0; i < 32; ++i) {
        allOnes.addChange(
            static_cast<Steinberg::Vst::ParamID>(Ruinae::kTranceGateStepLevel0Id + i),
            1.0);
    }
    double energyAllOnes = f.applyParamsAndMeasureEnergy(allOnes, 50);

    // Set all steps to 0.0 (full silence)
    TGParamChanges allZeros;
    for (int i = 0; i < 32; ++i) {
        allZeros.addChange(
            static_cast<Steinberg::Vst::ParamID>(Ruinae::kTranceGateStepLevel0Id + i),
            0.0);
    }
    double energyAllZeros = f.applyParamsAndMeasureEnergy(allZeros, 50);

    // All-ones should have significant energy; all-zeros should have much less.
    // Note: not near-zero because delay/reverb effects tails persist independently.
    INFO("Energy all 1.0: " << energyAllOnes << ", all 0.0: " << energyAllZeros);
    CHECK(energyAllOnes > 0.001);
    CHECK(energyAllZeros < energyAllOnes * 0.6);
}

TEST_CASE("TranceGate numSteps parameter propagates",
          "[trance_gate][param_flow][integration]") {
    TranceGateFixture f;
    f.startNote();
    f.processBlocksAndMeasureEnergy(30);

    // Enable gate with 2 steps: step 0=0.0, step 1=1.0 → 50% duty cycle
    TGParamChanges twoSteps;
    twoSteps.addChange(Ruinae::kTranceGateEnabledId, 1.0);
    twoSteps.addChange(Ruinae::kTranceGateDepthId, 1.0);
    twoSteps.addChange(Ruinae::kTranceGateTempoSyncId, 0.0);
    twoSteps.addChange(Ruinae::kTranceGateRateId, 0.5);
    // numSteps normalized: (N - 2) / 30, so 2 steps = 0.0
    twoSteps.addChange(Ruinae::kTranceGateNumStepsId, 0.0);
    // Set step 0 = 0, step 1 = 1 (rest don't matter with numSteps=2)
    twoSteps.addChange(Ruinae::kTranceGateStepLevel0Id, 0.0);
    twoSteps.addChange(Ruinae::kTranceGateStepLevel1Id, 1.0);
    double energyTwoSteps = f.applyParamsAndMeasureEnergy(twoSteps, 50);

    // Now switch to 4 steps: 0,0,0,1 → 25% duty cycle (less energy expected)
    TGParamChanges fourSteps;
    // 4 steps: (4 - 2) / 30 = 0.0667
    fourSteps.addChange(Ruinae::kTranceGateNumStepsId, 2.0 / 30.0);
    // Must explicitly set all 4 steps (steps 0,1 carry over from previous phase)
    fourSteps.addChange(Ruinae::kTranceGateStepLevel0Id, 0.0);
    fourSteps.addChange(Ruinae::kTranceGateStepLevel1Id, 0.0);
    fourSteps.addChange(Ruinae::kTranceGateStepLevel2Id, 0.0);
    fourSteps.addChange(Ruinae::kTranceGateStepLevel3Id, 1.0);
    double energyFourSteps = f.applyParamsAndMeasureEnergy(fourSteps, 50);

    // Different step count should produce measurably different energy
    INFO("Energy 2 steps: " << energyTwoSteps << ", 4 steps: " << energyFourSteps);
    bool differs = std::abs(energyTwoSteps - energyFourSteps) >
                   0.05 * std::max(energyTwoSteps, energyFourSteps);
    CHECK(differs);
}

TEST_CASE("TranceGate attack/release parameters propagate",
          "[trance_gate][param_flow][integration]") {
    TranceGateFixture f;
    f.startNote();
    f.processBlocksAndMeasureEnergy(30);

    // Enable gate with very short attack and release
    TGParamChanges shortEnv;
    shortEnv.addChange(Ruinae::kTranceGateEnabledId, 1.0);
    shortEnv.addChange(Ruinae::kTranceGateDepthId, 1.0);
    shortEnv.addChange(Ruinae::kTranceGateTempoSyncId, 0.0);
    shortEnv.addChange(Ruinae::kTranceGateRateId, 0.3);
    // Attack: 0.0 → 1ms, Release: 0.0 → 1ms
    shortEnv.addChange(Ruinae::kTranceGateAttackId, 0.0);
    shortEnv.addChange(Ruinae::kTranceGateReleaseId, 0.0);
    for (int i = 0; i < 32; ++i) {
        shortEnv.addChange(
            static_cast<Steinberg::Vst::ParamID>(Ruinae::kTranceGateStepLevel0Id + i),
            (i % 2 == 0) ? 0.0 : 1.0);
    }
    double energyShortEnv = f.applyParamsAndMeasureEnergy(shortEnv, 50);

    // Now set very long release (50ms)
    TGParamChanges longRelease;
    // Release: 1.0 → 50ms
    longRelease.addChange(Ruinae::kTranceGateReleaseId, 1.0);
    double energyLongRelease = f.applyParamsAndMeasureEnergy(longRelease, 50);

    // Long release should retain more energy (slower decay between steps)
    INFO("Energy short env: " << energyShortEnv << ", long release: " << energyLongRelease);
    bool differs = std::abs(energyShortEnv - energyLongRelease) >
                   0.02 * std::max(energyShortEnv, energyLongRelease);
    CHECK(differs);
}

TEST_CASE("TranceGate tempo sync parameter propagates",
          "[trance_gate][param_flow][integration]") {
    TranceGateFixture f;

    // Provide process context with tempo information
    Steinberg::Vst::ProcessContext context{};
    context.state = Steinberg::Vst::ProcessContext::kTempoValid
                  | Steinberg::Vst::ProcessContext::kTimeSigValid
                  | Steinberg::Vst::ProcessContext::kPlaying;
    context.tempo = 120.0;
    context.timeSigNumerator = 4;
    context.timeSigDenominator = 4;
    context.projectTimeSamples = 0;
    context.sampleRate = 44100.0;
    f.data.processContext = &context;

    f.startNote();
    f.processBlocksAndMeasureEnergy(30);

    // Enable gate in tempo sync mode
    TGParamChanges syncOn;
    syncOn.addChange(Ruinae::kTranceGateEnabledId, 1.0);
    syncOn.addChange(Ruinae::kTranceGateDepthId, 1.0);
    syncOn.addChange(Ruinae::kTranceGateTempoSyncId, 1.0); // tempo sync ON
    for (int i = 0; i < 32; ++i) {
        syncOn.addChange(
            static_cast<Steinberg::Vst::ParamID>(Ruinae::kTranceGateStepLevel0Id + i),
            (i % 2 == 0) ? 0.0 : 1.0);
    }
    double energySync = f.applyParamsAndMeasureEnergy(syncOn, 50);

    // Switch to free-run with a very different rate
    TGParamChanges freeRun;
    freeRun.addChange(Ruinae::kTranceGateTempoSyncId, 0.0); // free-run
    freeRun.addChange(Ruinae::kTranceGateRateId, 1.0);       // 100 Hz
    double energyFreeRun = f.applyParamsAndMeasureEnergy(freeRun, 50);

    // The two modes should produce different energy profiles
    INFO("Energy sync: " << energySync << ", free-run: " << energyFreeRun);
    bool differs = std::abs(energySync - energyFreeRun) >
                   0.02 * std::max(energySync, energyFreeRun);
    CHECK(differs);
}

TEST_CASE("Multiple TranceGate params in same block",
          "[trance_gate][param_flow][integration]") {
    TranceGateFixture f;
    f.startNote();
    f.processBlocksAndMeasureEnergy(30);

    // Send all trance gate parameters simultaneously — should not crash
    TGParamChanges allParams;
    allParams.addChange(Ruinae::kTranceGateEnabledId, 1.0);
    allParams.addChange(Ruinae::kTranceGateNumStepsId, 0.5);    // ~17 steps
    allParams.addChange(Ruinae::kTranceGateRateId, 0.3);
    allParams.addChange(Ruinae::kTranceGateDepthId, 0.75);
    allParams.addChange(Ruinae::kTranceGateAttackId, 0.5);
    allParams.addChange(Ruinae::kTranceGateReleaseId, 0.5);
    allParams.addChange(Ruinae::kTranceGateTempoSyncId, 0.0);
    allParams.addChange(Ruinae::kTranceGateNoteValueId, 0.3);
    allParams.addChange(Ruinae::kTranceGateEuclideanEnabledId, 1.0);
    allParams.addChange(Ruinae::kTranceGateEuclideanHitsId, 0.25);
    allParams.addChange(Ruinae::kTranceGateEuclideanRotationId, 0.1);
    allParams.addChange(Ruinae::kTranceGatePhaseOffsetId, 0.5);
    for (int i = 0; i < 32; ++i) {
        allParams.addChange(
            static_cast<Steinberg::Vst::ParamID>(Ruinae::kTranceGateStepLevel0Id + i),
            static_cast<double>(i) / 31.0); // gradient pattern
    }

    // Process multiple blocks — should not crash and should produce audio
    f.processWithParams(allParams);
    double energy = f.processBlocksAndMeasureEnergy(5);
    INFO("Energy after all params: " << energy);
    CHECK(energy > 0.0);
}

// =============================================================================
// Regression: Trance gate step must NOT reset on noteOn (perVoice=false)
// =============================================================================
// When multiple notes are played in sequence, the trance gate should continue
// advancing through the pattern rather than restarting from step 0 each time.
// This verifies the fix for the bug where the step indicator only reached
// step ~8 with 32 steps at 1/8 note because each noteOn reset the gate.
// =============================================================================

#include "engine/ruinae_engine.h"

TEST_CASE("TranceGate syncs to host transport position",
          "[trance_gate][regression][integration]") {
    using namespace Krate::DSP;

    RuinaeEngine engine;
    engine.prepare(48000.0, 512);

    // Configure: 16 steps, 1/16th note, tempo sync
    TranceGateParams tgp;
    tgp.numSteps = 16;
    tgp.tempoSync = true;
    tgp.noteValue = NoteValue::Sixteenth;
    tgp.noteModifier = NoteModifier::None;
    tgp.depth = 1.0f;
    tgp.perVoice = false;
    engine.setTranceGateEnabled(true);
    engine.setTranceGateParams(tgp);
    engine.setTempo(120.0);
    for (int i = 0; i < 32; ++i) {
        engine.setTranceGateStep(i, 1.0f);
    }

    constexpr size_t kBlockSize = 512;
    std::vector<float> outL(kBlockSize, 0.0f);
    std::vector<float> outR(kBlockSize, 0.0f);

    // At 120 BPM, 1/16th note = 0.25 quarter notes per step
    // 16 steps = 4.0 quarter notes = 1 bar in 4/4

    // Simulate transport at bar 3, beat 2 = 10.0 quarter notes
    // Step should be: fmod(10.0, 4.0) = 2.0 / 0.25 = step 8
    BlockContext ctx;
    ctx.sampleRate = 48000.0;
    ctx.blockSize = kBlockSize;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.projectTimeMusic = 10.0;  // bar 3, beat 2
    ctx.projectTimeMusicValid = true;

    engine.noteOn(60, 100);
    engine.setBlockContext(ctx);
    engine.setTempo(120.0);
    engine.setTranceGateParams(tgp);
    std::fill(outL.begin(), outL.end(), 0.0f);
    std::fill(outR.begin(), outR.end(), 0.0f);
    engine.processBlock(outL.data(), outR.data(), kBlockSize);

    int step = engine.getTranceGateCurrentStep();
    INFO("At PPQ 10.0, step should be 8, got: " << step);
    CHECK(step == 8);

    // Reposition to start of song (PPQ 0.0) — step should jump to 0
    ctx.projectTimeMusic = 0.0;
    engine.setBlockContext(ctx);
    engine.setTranceGateParams(tgp);
    std::fill(outL.begin(), outL.end(), 0.0f);
    std::fill(outR.begin(), outR.end(), 0.0f);
    engine.processBlock(outL.data(), outR.data(), kBlockSize);

    step = engine.getTranceGateCurrentStep();
    INFO("At PPQ 0.0, step should be 0, got: " << step);
    CHECK(step == 0);

    // Jump to PPQ 3.75 — last step of bar 1
    // 3.75 / 0.25 = step 15
    ctx.projectTimeMusic = 3.75;
    engine.setBlockContext(ctx);
    engine.setTranceGateParams(tgp);
    std::fill(outL.begin(), outL.end(), 0.0f);
    std::fill(outR.begin(), outR.end(), 0.0f);
    engine.processBlock(outL.data(), outR.data(), kBlockSize);

    step = engine.getTranceGateCurrentStep();
    INFO("At PPQ 3.75, step should be 15, got: " << step);
    CHECK(step == 15);
}

TEST_CASE("TranceGate new voices sync to transport position (not step 0)",
          "[trance_gate][regression][integration]") {
    using namespace Krate::DSP;

    RuinaeEngine engine;
    engine.prepare(48000.0, 512);

    TranceGateParams tgp;
    tgp.numSteps = 32;
    tgp.tempoSync = true;
    tgp.noteValue = NoteValue::Eighth;
    tgp.noteModifier = NoteModifier::None;
    tgp.depth = 1.0f;
    tgp.perVoice = false;
    engine.setTranceGateEnabled(true);
    engine.setTranceGateParams(tgp);
    engine.setTempo(120.0);
    for (int i = 0; i < 32; ++i) {
        engine.setTranceGateStep(i, 1.0f);
    }

    constexpr size_t kBlockSize = 512;
    std::vector<float> outL(kBlockSize, 0.0f);
    std::vector<float> outR(kBlockSize, 0.0f);

    // At 120 BPM, 1/8 note = 0.5 quarter notes per step
    // 32 steps = 16.0 quarter notes (4 bars)
    // PPQ 5.0 → fmod(5.0, 16.0) = 5.0, step = floor(5.0/0.5) = 10

    BlockContext ctx;
    ctx.sampleRate = 48000.0;
    ctx.blockSize = kBlockSize;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    ctx.projectTimeMusic = 5.0;
    ctx.projectTimeMusicValid = true;

    // Play first note (voice 0)
    engine.noteOn(60, 100);
    engine.setBlockContext(ctx);
    engine.setTempo(120.0);
    engine.setTranceGateParams(tgp);
    std::fill(outL.begin(), outL.end(), 0.0f);
    std::fill(outR.begin(), outR.end(), 0.0f);
    engine.processBlock(outL.data(), outR.data(), kBlockSize);

    int step1 = engine.getTranceGateCurrentStep();
    INFO("Voice 0 step at PPQ 5.0: " << step1);
    CHECK(step1 == 10);

    // Advance slightly and play a second note (allocates voice 1)
    ctx.projectTimeMusic = 5.1;
    engine.noteOn(64, 100);
    engine.setBlockContext(ctx);
    engine.setTranceGateParams(tgp);
    std::fill(outL.begin(), outL.end(), 0.0f);
    std::fill(outR.begin(), outR.end(), 0.0f);
    engine.processBlock(outL.data(), outR.data(), kBlockSize);

    int step2 = engine.getTranceGateCurrentStep();
    INFO("After second noteOn at PPQ 5.1, step: " << step2);
    // Both voices should be around step 10 (not reset to 0)
    CHECK(step2 == 10);
}
