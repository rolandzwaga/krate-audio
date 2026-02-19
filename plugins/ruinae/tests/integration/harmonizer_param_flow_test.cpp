// ==============================================================================
// Integration Test: Harmonizer Parameter Flow from Host to Engine
// ==============================================================================
// Verifies that harmonizer parameters changed at the VST host level
// propagate through the full pipeline:
//   Host param → processParameterChanges() → RuinaeHarmonizerParams atomics
//   → applyParamsToEngine() → engine_.setHarmonizerXxx() → audible effect
//
// Each test plays a note, applies parameter changes, and measures the audio
// output difference to confirm the parameter actually affected the engine.
//
// Reference: specs/067-ruinae-harmonizer/spec.md
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
// Mocks (same pattern as trance_gate_param_flow_test.cpp)
// =============================================================================

namespace {

class HarmParamValueQueue : public Steinberg::Vst::IParamValueQueue {
public:
    HarmParamValueQueue(Steinberg::Vst::ParamID id, double value)
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

class HarmParamChanges : public Steinberg::Vst::IParameterChanges {
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
    std::vector<HarmParamValueQueue> queues_;
};

class HarmEmptyEventList : public Steinberg::Vst::IEventList {
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

class HarmNoteOnEvents : public Steinberg::Vst::IEventList {
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

struct HarmonizerFixture {
    Ruinae::Processor processor;
    HarmEmptyEventList emptyEvents;
    std::vector<float> outL;
    std::vector<float> outR;
    float* channelBuffers[2];
    Steinberg::Vst::AudioBusBuffers outputBus{};
    Steinberg::Vst::ProcessData data{};
    static constexpr size_t kBlockSize = 256;

    HarmonizerFixture() : outL(kBlockSize, 0.0f), outR(kBlockSize, 0.0f) {
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

        // Enable effects chain (defaults to disabled)
        HarmParamChanges enableParams;
        enableParams.addChange(Ruinae::kDelayEnabledId, 1.0);
        enableParams.addChange(Ruinae::kReverbEnabledId, 1.0);
        enableParams.addChange(Ruinae::kHarmonizerEnabledId, 1.0);
        // Set 1 voice with +7 semitone interval so harmonizer produces
        // audible pitched output distinct from the dry signal.
        // numVoices: norm 0.0 → index 0 → plain 1 (4-entry dropdown: 1-4)
        enableParams.addChange(Ruinae::kHarmonizerNumVoicesId, 0.0);
        // voice1 interval: +7 semitones → norm = (7 + 24) / 48 = 0.6458...
        enableParams.addChange(Ruinae::kHarmonizerVoice1IntervalId, 31.0 / 48.0);
        processWithParams(enableParams);
    }

    ~HarmonizerFixture() {
        processor.setActive(false);
        processor.terminate();
    }

    void processWithParams(HarmParamChanges& params) {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        data.inputParameterChanges = &params;
        processor.process(data);
    }

    void startNote() {
        HarmNoteOnEvents noteEvents;
        HarmParamChanges emptyParams;
        data.inputEvents = &noteEvents;
        data.inputParameterChanges = &emptyParams;
        processor.process(data);
        data.inputEvents = &emptyEvents;
    }

    // Process N blocks and return total energy (sum of squares, L+R)
    double processBlocksAndMeasureEnergy(int numBlocks) {
        HarmParamChanges empty;
        double totalEnergy = 0.0;
        for (int i = 0; i < numBlocks; ++i) {
            std::fill(outL.begin(), outL.end(), 0.0f);
            std::fill(outR.begin(), outR.end(), 0.0f);
            data.inputParameterChanges = &empty;
            processor.process(data);
            for (size_t s = 0; s < kBlockSize; ++s) {
                totalEnergy += static_cast<double>(outL[s]) * outL[s]
                             + static_cast<double>(outR[s]) * outR[s];
            }
        }
        return totalEnergy;
    }

    // Process N blocks, applying params on first block only, return energy (L+R)
    double applyParamsAndMeasureEnergy(HarmParamChanges& params, int numBlocks) {
        HarmParamChanges empty;
        double totalEnergy = 0.0;
        for (int i = 0; i < numBlocks; ++i) {
            std::fill(outL.begin(), outL.end(), 0.0f);
            std::fill(outR.begin(), outR.end(), 0.0f);
            data.inputParameterChanges = (i == 0) ? &params : &empty;
            processor.process(data);
            for (size_t s = 0; s < kBlockSize; ++s) {
                totalEnergy += static_cast<double>(outL[s]) * outL[s]
                             + static_cast<double>(outR[s]) * outR[s];
            }
        }
        return totalEnergy;
    }

    // Measure L and R energy separately (for pan tests)
    std::pair<double, double> processBlocksAndMeasureStereoEnergy(int numBlocks) {
        HarmParamChanges empty;
        double energyL = 0.0;
        double energyR = 0.0;
        for (int i = 0; i < numBlocks; ++i) {
            std::fill(outL.begin(), outL.end(), 0.0f);
            std::fill(outR.begin(), outR.end(), 0.0f);
            data.inputParameterChanges = &empty;
            processor.process(data);
            for (size_t s = 0; s < kBlockSize; ++s) {
                energyL += static_cast<double>(outL[s]) * outL[s];
                energyR += static_cast<double>(outR[s]) * outR[s];
            }
        }
        return {energyL, energyR};
    }

    // Apply params then measure L and R energy separately
    std::pair<double, double> applyParamsAndMeasureStereoEnergy(
        HarmParamChanges& params, int numBlocks) {
        HarmParamChanges empty;
        double energyL = 0.0;
        double energyR = 0.0;
        for (int i = 0; i < numBlocks; ++i) {
            std::fill(outL.begin(), outL.end(), 0.0f);
            std::fill(outR.begin(), outR.end(), 0.0f);
            data.inputParameterChanges = (i == 0) ? &params : &empty;
            processor.process(data);
            for (size_t s = 0; s < kBlockSize; ++s) {
                energyL += static_cast<double>(outL[s]) * outL[s];
                energyR += static_cast<double>(outR[s]) * outR[s];
            }
        }
        return {energyL, energyR};
    }
};

} // anonymous namespace

// =============================================================================
// Tests
// =============================================================================

TEST_CASE("Harmonizer enable/disable affects audio output",
          "[harmonizer][param_flow][integration]") {
    HarmonizerFixture f;
    f.startNote();

    // Suppress dry path so output is dominated by harmonizer wet voices.
    // This isolates the effect of enabling/disabling the harmonizer.
    HarmParamChanges wetOnly;
    wetOnly.addChange(Ruinae::kHarmonizerDryLevelId, 0.0);       // dry = -60 dB
    wetOnly.addChange(Ruinae::kHarmonizerWetLevelId, 1.0);       // wet = +6 dB
    wetOnly.addChange(Ruinae::kHarmonizerNumVoicesId, 1.0);      // 4 voices
    wetOnly.addChange(Ruinae::kHarmonizerVoice1LevelId, 1.0);    // +6 dB
    wetOnly.addChange(Ruinae::kHarmonizerVoice2IntervalId, 0.75); // +12 st
    wetOnly.addChange(Ruinae::kHarmonizerVoice2LevelId, 1.0);
    wetOnly.addChange(Ruinae::kHarmonizerVoice3IntervalId, 0.6);  // +5 st
    wetOnly.addChange(Ruinae::kHarmonizerVoice3LevelId, 1.0);
    wetOnly.addChange(Ruinae::kHarmonizerVoice4IntervalId, 0.4);  // -5 st
    wetOnly.addChange(Ruinae::kHarmonizerVoice4LevelId, 1.0);
    f.processWithParams(wetOnly);

    // Let sound stabilize past effects chain + PV latency
    f.processBlocksAndMeasureEnergy(50);

    // Measure energy with harmonizer ON (wet-only output)
    double energyEnabled = f.processBlocksAndMeasureEnergy(50);

    // Disable harmonizer — bypasses the entire harmonizer stage
    HarmParamChanges disableHarm;
    disableHarm.addChange(Ruinae::kHarmonizerEnabledId, 0.0);
    double energyDisabled = f.applyParamsAndMeasureEnergy(disableHarm, 50);

    // With harmonizer disabled, signal passes through unprocessed.
    // Since dry was set to -60 dB inside the harmonizer, disabling the harmonizer
    // restores the original signal (bypass). The outputs should differ significantly.
    INFO("Energy enabled (wet-only): " << energyEnabled
         << ", disabled (bypass): " << energyDisabled);
    bool differs = std::abs(energyEnabled - energyDisabled) >
                   0.05 * std::max(energyEnabled, energyDisabled);
    CHECK(differs);
}

TEST_CASE("Harmonizer numVoices propagates",
          "[harmonizer][param_flow][integration]") {
    HarmonizerFixture f;
    f.startNote();

    // Suppress dry path to isolate wet voice contribution
    HarmParamChanges setup;
    setup.addChange(Ruinae::kHarmonizerDryLevelId, 0.0);       // dry = -60 dB
    setup.addChange(Ruinae::kHarmonizerWetLevelId, 1.0);       // wet = +6 dB
    setup.addChange(Ruinae::kHarmonizerNumVoicesId, 0.0);      // start with 1 voice
    f.processWithParams(setup);

    f.processBlocksAndMeasureEnergy(50);

    // Measure with 1 voice (only voice 1 active at default interval)
    double energyOneVoice = f.processBlocksAndMeasureEnergy(50);

    // Set numVoices to 4 with distinct intervals: norm 1.0 → index 3 → 4 voices
    HarmParamChanges fourVoices;
    fourVoices.addChange(Ruinae::kHarmonizerNumVoicesId, 1.0);
    fourVoices.addChange(Ruinae::kHarmonizerVoice1LevelId, 1.0);
    fourVoices.addChange(Ruinae::kHarmonizerVoice2IntervalId, 0.75);  // +12 st
    fourVoices.addChange(Ruinae::kHarmonizerVoice2LevelId, 1.0);
    fourVoices.addChange(Ruinae::kHarmonizerVoice3IntervalId, 0.6);   // +5 st
    fourVoices.addChange(Ruinae::kHarmonizerVoice3LevelId, 1.0);
    fourVoices.addChange(Ruinae::kHarmonizerVoice4IntervalId, 0.4);   // -5 st
    fourVoices.addChange(Ruinae::kHarmonizerVoice4LevelId, 1.0);
    double energyFourVoices = f.applyParamsAndMeasureEnergy(fourVoices, 50);

    // 4 voices should produce more energy than 1 voice
    INFO("Energy 1 voice: " << energyOneVoice << ", 4 voices: " << energyFourVoices);
    CHECK(energyFourVoices > energyOneVoice * 1.1);
}

TEST_CASE("Harmonizer wet level propagates",
          "[harmonizer][param_flow][integration]") {
    HarmonizerFixture f;
    f.startNote();

    // Suppress dry path. Start with wet at -60 dB (effectively silent wet too).
    HarmParamChanges setup;
    setup.addChange(Ruinae::kHarmonizerDryLevelId, 0.0);       // dry = -60 dB
    setup.addChange(Ruinae::kHarmonizerWetLevelId, 0.0);       // wet = -60 dB
    setup.addChange(Ruinae::kHarmonizerNumVoicesId, 1.0);      // 4 voices
    setup.addChange(Ruinae::kHarmonizerVoice1LevelId, 1.0);
    setup.addChange(Ruinae::kHarmonizerVoice2IntervalId, 0.75);
    setup.addChange(Ruinae::kHarmonizerVoice2LevelId, 1.0);
    setup.addChange(Ruinae::kHarmonizerVoice3IntervalId, 0.6);
    setup.addChange(Ruinae::kHarmonizerVoice3LevelId, 1.0);
    setup.addChange(Ruinae::kHarmonizerVoice4IntervalId, 0.4);
    setup.addChange(Ruinae::kHarmonizerVoice4LevelId, 1.0);
    f.processWithParams(setup);

    f.processBlocksAndMeasureEnergy(50);

    // Measure with wet = -60 dB (near silent)
    double energyWetMin = f.processBlocksAndMeasureEnergy(50);

    // Crank wet to +6 dB
    HarmParamChanges wetMax;
    wetMax.addChange(Ruinae::kHarmonizerWetLevelId, 1.0);  // +6 dB
    double energyWetMax = f.applyParamsAndMeasureEnergy(wetMax, 50);

    // +6 dB wet should produce significantly more energy than -60 dB wet
    INFO("Energy wet=-60dB: " << energyWetMin << ", wet=+6dB: " << energyWetMax);
    CHECK(energyWetMax > energyWetMin * 1.1);
}

TEST_CASE("Harmonizer dry level propagates",
          "[harmonizer][param_flow][integration]") {
    HarmonizerFixture f;
    f.startNote();
    f.processBlocksAndMeasureEnergy(30);

    // Measure at default dry level (0 dB, norm ~0.909)
    double energyDefault = f.processBlocksAndMeasureEnergy(50);

    // Set dry level to -60 dB (norm 0.0) — effectively silences dry path
    HarmParamChanges dryMin;
    dryMin.addChange(Ruinae::kHarmonizerDryLevelId, 0.0);
    double energyDryMin = f.applyParamsAndMeasureEnergy(dryMin, 50);

    // Silencing the dry path should change total energy
    INFO("Energy default dry: " << energyDefault << ", dry=-60dB: " << energyDryMin);
    bool differs = std::abs(energyDefault - energyDryMin) >
                   0.05 * std::max(energyDefault, energyDryMin);
    CHECK(differs);
}

TEST_CASE("Harmonizer voice interval propagates",
          "[harmonizer][param_flow][integration]") {
    HarmonizerFixture f;
    f.startNote();

    // Suppress dry path to isolate wet voice output
    HarmParamChanges setup;
    setup.addChange(Ruinae::kHarmonizerDryLevelId, 0.0);       // dry = -60 dB
    setup.addChange(Ruinae::kHarmonizerWetLevelId, 1.0);       // wet = +6 dB
    setup.addChange(Ruinae::kHarmonizerNumVoicesId, 0.0);      // 1 voice
    setup.addChange(Ruinae::kHarmonizerVoice1LevelId, 1.0);    // +6 dB
    // Voice 1 at unison (0 semitones): norm = (0+24)/48 = 0.5
    setup.addChange(Ruinae::kHarmonizerVoice1IntervalId, 0.5);
    f.processWithParams(setup);

    f.processBlocksAndMeasureEnergy(50);

    // Measure with unison interval
    double energyUnison = f.processBlocksAndMeasureEnergy(50);

    // Change to +12 semitones (octave up): norm = (12+24)/48 = 0.75
    HarmParamChanges octaveUp;
    octaveUp.addChange(Ruinae::kHarmonizerVoice1IntervalId, 0.75);
    double energyOctave = f.applyParamsAndMeasureEnergy(octaveUp, 50);

    // Different pitch interval should produce a different energy profile.
    // Octave-up shortens the signal period → different energy distribution.
    INFO("Energy unison: " << energyUnison << ", octave: " << energyOctave);
    bool differs = std::abs(energyUnison - energyOctave) >
                   0.02 * std::max(energyUnison, energyOctave);
    CHECK(differs);
}

TEST_CASE("Harmonizer voice pan propagates",
          "[harmonizer][param_flow][integration]") {
    HarmonizerFixture f;
    f.startNote();

    // Suppress dry path (dry is center-panned, would mask voice pan changes)
    HarmParamChanges setup;
    setup.addChange(Ruinae::kHarmonizerDryLevelId, 0.0);       // dry = -60 dB
    setup.addChange(Ruinae::kHarmonizerWetLevelId, 1.0);       // wet = +6 dB
    setup.addChange(Ruinae::kHarmonizerNumVoicesId, 0.0);      // 1 voice
    setup.addChange(Ruinae::kHarmonizerVoice1LevelId, 1.0);    // +6 dB
    // Start with hard-left pan: norm 0.0 → plain -1.0
    setup.addChange(Ruinae::kHarmonizerVoice1PanId, 0.0);
    f.processWithParams(setup);

    f.processBlocksAndMeasureEnergy(50);

    // Measure with hard-left pan
    auto [leftL, leftR] = f.processBlocksAndMeasureStereoEnergy(50);

    // Switch to hard-right pan: norm 1.0 → plain +1.0
    HarmParamChanges panRight;
    panRight.addChange(Ruinae::kHarmonizerVoice1PanId, 1.0);
    auto [rightL, rightR] = f.applyParamsAndMeasureStereoEnergy(panRight, 50);

    // Hard-left should have higher L/R ratio than hard-right
    double leftRatio = (leftL > 0.0 && leftR > 0.0) ? leftL / leftR : 1.0;
    double rightRatio = (rightL > 0.0 && rightR > 0.0) ? rightL / rightR : 1.0;

    INFO("Left pan L/R ratio: " << leftRatio << ", Right pan L/R ratio: " << rightRatio);
    CHECK(leftRatio > rightRatio);
}

TEST_CASE("Multiple harmonizer params in same block",
          "[harmonizer][param_flow][integration]") {
    HarmonizerFixture f;
    f.startNote();
    f.processBlocksAndMeasureEnergy(30);

    // Measure baseline
    double energyBaseline = f.processBlocksAndMeasureEnergy(50);

    // Send many harmonizer parameters simultaneously — should not crash
    // and should produce a measurably different output
    HarmParamChanges allParams;
    allParams.addChange(Ruinae::kHarmonizerNumVoicesId, 1.0);         // 4 voices
    allParams.addChange(Ruinae::kHarmonizerHarmonyModeId, 1.0);       // Scalic
    allParams.addChange(Ruinae::kHarmonizerKeyId, 2.0 / 11.0);       // D
    allParams.addChange(Ruinae::kHarmonizerScaleId, 1.0 / 8.0);      // Minor
    allParams.addChange(Ruinae::kHarmonizerWetLevelId, 1.0);          // +6 dB
    allParams.addChange(Ruinae::kHarmonizerDryLevelId, 0.5);          // -27 dB
    allParams.addChange(Ruinae::kHarmonizerVoice1IntervalId, 0.75);   // +12 st
    allParams.addChange(Ruinae::kHarmonizerVoice1LevelId, 1.0);      // +6 dB
    allParams.addChange(Ruinae::kHarmonizerVoice1PanId, 0.25);       // left
    allParams.addChange(Ruinae::kHarmonizerVoice1DetuneId, 0.7);     // +20 cents
    allParams.addChange(Ruinae::kHarmonizerVoice2IntervalId, 0.25);   // -12 st
    allParams.addChange(Ruinae::kHarmonizerVoice2LevelId, 0.8);
    allParams.addChange(Ruinae::kHarmonizerVoice2PanId, 0.75);       // right
    allParams.addChange(Ruinae::kHarmonizerVoice3IntervalId, 0.6);    // +5 st
    allParams.addChange(Ruinae::kHarmonizerVoice3LevelId, 0.9);
    allParams.addChange(Ruinae::kHarmonizerVoice4IntervalId, 0.4);    // -5 st
    allParams.addChange(Ruinae::kHarmonizerVoice4LevelId, 0.85);

    double energyAllParams = f.applyParamsAndMeasureEnergy(allParams, 50);

    INFO("Energy baseline: " << energyBaseline << ", all params: " << energyAllParams);
    bool differs = std::abs(energyBaseline - energyAllParams) >
                   0.05 * std::max(energyBaseline, energyAllParams);
    CHECK(differs);
}

// =============================================================================
// Harmonizer Wet Level Diagnostic (Full Processor Pipeline)
// =============================================================================
// Measures wet output level relative to bypass through the complete processor.
// This reproduces the user's report of "very faint wet output."

TEST_CASE("Harmonizer wet output level - full pipeline diagnostic",
          "[harmonizer][param_flow][integration][diagnostic]") {
    Ruinae::Processor processor;
    processor.initialize(nullptr);
    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.sampleRate = 44100.0;
    setup.maxSamplesPerBlock = 256;
    processor.setupProcessing(setup);
    processor.setActive(true);

    constexpr size_t kBlock = 256;
    std::vector<float> outL(kBlock, 0.0f);
    std::vector<float> outR(kBlock, 0.0f);
    float* channelBuffers[2] = { outL.data(), outR.data() };

    Steinberg::Vst::AudioBusBuffers outputBus{};
    outputBus.numChannels = 2;
    outputBus.channelBuffers32 = channelBuffers;

    Steinberg::Vst::ProcessData data{};
    data.processMode = Steinberg::Vst::kRealtime;
    data.symbolicSampleSize = Steinberg::Vst::kSample32;
    data.numSamples = static_cast<Steinberg::int32>(kBlock);
    data.numInputs = 0;
    data.inputs = nullptr;
    data.numOutputs = 1;
    data.outputs = &outputBus;
    data.processContext = nullptr;

    HarmEmptyEventList emptyEvents;
    data.inputEvents = &emptyEvents;

    // Helper: process with param changes
    auto processWithParams = [&](HarmParamChanges& p) {
        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);
        data.inputParameterChanges = &p;
        processor.process(data);
    };

    // Helper: process N blocks, return energy
    auto measureEnergy = [&](int numBlocks) -> double {
        HarmParamChanges empty;
        double totalEnergy = 0.0;
        for (int i = 0; i < numBlocks; ++i) {
            std::fill(outL.begin(), outL.end(), 0.0f);
            std::fill(outR.begin(), outR.end(), 0.0f);
            data.inputParameterChanges = &empty;
            processor.process(data);
            for (size_t s = 0; s < kBlock; ++s) {
                totalEnergy += static_cast<double>(outL[s]) * outL[s]
                             + static_cast<double>(outR[s]) * outR[s];
            }
        }
        return totalEnergy;
    };

    // Step 1: Enable harmonizer with explicit voice configuration
    {
        HarmParamChanges enableAll;
        enableAll.addChange(Ruinae::kHarmonizerEnabledId, 1.0);
        enableAll.addChange(Ruinae::kHarmonizerNumVoicesId, 0.0);  // 1 voice
        enableAll.addChange(Ruinae::kHarmonizerPitchShiftModeId, 0.0); // Simple
        // Voice 1: +7 semitones, 0 dB level, center pan
        enableAll.addChange(Ruinae::kHarmonizerVoice1IntervalId, 31.0 / 48.0);
        enableAll.addChange(Ruinae::kHarmonizerVoice1LevelId, 60.0 / 66.0); // 0 dB
        enableAll.addChange(Ruinae::kHarmonizerVoice1PanId, 0.5);           // center
        // Wet-only: dry muted, wet at 0 dB
        enableAll.addChange(Ruinae::kHarmonizerDryLevelId, 0.0);            // -60 dB
        enableAll.addChange(Ruinae::kHarmonizerWetLevelId, 60.0 / 66.0);   // 0 dB
        processWithParams(enableAll);
    }

    // Step 2: Start note
    {
        HarmNoteOnEvents noteEvents;
        HarmParamChanges empty;
        data.inputEvents = &noteEvents;
        data.inputParameterChanges = &empty;
        processor.process(data);
        data.inputEvents = &emptyEvents;
    }

    // Step 3: First test - DRY ONLY (harmonizer enabled, dry=0dB, wet=-60dB, 0 voices)
    // This tests if signal passes through the harmonizer's dry path
    {
        HarmParamChanges dryOnly;
        dryOnly.addChange(Ruinae::kHarmonizerDryLevelId, 60.0 / 66.0);   // 0 dB
        dryOnly.addChange(Ruinae::kHarmonizerWetLevelId, 0.0);            // -60 dB
        dryOnly.addChange(Ruinae::kHarmonizerNumVoicesId, 0.0);           // 1 voice
        processWithParams(dryOnly);
    }
    measureEnergy(60);  // settle
    double energyDryOnly = measureEnergy(30);
    double rmsDryOnly = std::sqrt(energyDryOnly / (2.0 * 30.0 * kBlock));
    INFO("=== DRY-ONLY (harmonizer enabled, dry=0dB, wet=-60dB) ===");
    INFO("Energy: " << energyDryOnly << "  RMS: " << rmsDryOnly);

    // Step 4: Now switch to WET-ONLY and trace per-block energy
    {
        HarmParamChanges wetOnly;
        wetOnly.addChange(Ruinae::kHarmonizerDryLevelId, 0.0);            // -60 dB
        wetOnly.addChange(Ruinae::kHarmonizerWetLevelId, 60.0 / 66.0);   // 0 dB
        wetOnly.addChange(Ruinae::kHarmonizerNumVoicesId, 0.0);           // 1 voice
        wetOnly.addChange(Ruinae::kHarmonizerVoice1IntervalId, 31.0 / 48.0); // +7 st
        wetOnly.addChange(Ruinae::kHarmonizerVoice1LevelId, 60.0 / 66.0);    // 0 dB
        processWithParams(wetOnly);
    }

    // Per-block energy trace during settle
    {
        HarmParamChanges empty;
        for (int b = 0; b < 80; ++b) {
            std::fill(outL.begin(), outL.end(), 0.0f);
            std::fill(outR.begin(), outR.end(), 0.0f);
            data.inputParameterChanges = &empty;
            processor.process(data);
            double blockEnergy = 0.0;
            for (size_t s = 0; s < kBlock; ++s) {
                blockEnergy += static_cast<double>(outL[s]) * outL[s]
                             + static_cast<double>(outR[s]) * outR[s];
            }
            if (b < 10 || b % 10 == 0) {
                INFO("Block " << b << " energy: " << blockEnergy);
            }
        }
    }

    double energyWetOnly = measureEnergy(30);
    double rmsWetOnly = std::sqrt(energyWetOnly / (2.0 * 30.0 * kBlock));

    INFO("=== WET-ONLY (1 voice Simple +7st, wet=0dB) ===");
    INFO("Energy: " << energyWetOnly << "  RMS: " << rmsWetOnly);

    // Step 5: Disable harmonizer and measure BYPASS level
    {
        HarmParamChanges disableHarm;
        disableHarm.addChange(Ruinae::kHarmonizerEnabledId, 0.0);
        processWithParams(disableHarm);
    }
    measureEnergy(5);  // settle the crossfade
    double energyBypass = measureEnergy(30);
    double rmsBypass = std::sqrt(energyBypass / (2.0 * 30.0 * kBlock));

    INFO("=== BYPASS ===");
    INFO("Energy: " << energyBypass << "  RMS: " << rmsBypass);

    // Step 6: Re-enable with wet at +6 dB (user's "100%" scenario)
    {
        HarmParamChanges reEnable;
        reEnable.addChange(Ruinae::kHarmonizerEnabledId, 1.0);
        reEnable.addChange(Ruinae::kHarmonizerWetLevelId, 1.0);  // +6 dB
        processWithParams(reEnable);
    }
    measureEnergy(60);  // settle
    double energyWetMax = measureEnergy(30);
    double rmsWetMax = std::sqrt(energyWetMax / (2.0 * 30.0 * kBlock));

    INFO("=== WET at +6dB (user 100%) ===");
    INFO("Energy: " << energyWetMax << "  RMS: " << rmsWetMax);

    // Step 7: User scenario: dry at -27dB, wet at +6dB
    {
        HarmParamChanges userScenario;
        userScenario.addChange(Ruinae::kHarmonizerDryLevelId, 0.5);  // -27 dB
        userScenario.addChange(Ruinae::kHarmonizerWetLevelId, 1.0);  // +6 dB
        processWithParams(userScenario);
    }
    measureEnergy(20);  // settle
    double energyUser = measureEnergy(50);
    double rmsUser = std::sqrt(energyUser / (2.0 * 50.0 * kBlock));

    INFO("=== USER SCENARIO (dry=-27dB, wet=+6dB) ===");
    INFO("Energy: " << energyUser << "  RMS: " << rmsUser);

    // Report ratios
    if (rmsBypass > 0.0) {
        INFO("Wet-only / Bypass ratio: " << rmsWetOnly / rmsBypass);
        INFO("Wet+6dB / Bypass ratio: " << rmsWetMax / rmsBypass);
        INFO("User scenario / Bypass ratio: " << rmsUser / rmsBypass);
    }

    // Dry-only should be close to bypass (harmonizer in dry-passthrough mode)
    INFO("Dry-only / Bypass ratio: " << rmsDryOnly / rmsBypass);
    CHECK(energyDryOnly > energyBypass * 0.3);

    // The wet output should be at least 10% of bypass level
    CHECK(energyWetOnly > energyBypass * 0.1);
    CHECK(energyWetMax > energyBypass * 0.1);

    processor.setActive(false);
    processor.terminate();
}

// =============================================================================
// Per-PitchMode wet output comparison (Full Processor Pipeline)
// =============================================================================
// Measures wet output for each pitch mode to diagnose per-mode level differences.

TEST_CASE("Harmonizer wet level per pitch mode - full pipeline",
          "[harmonizer][param_flow][integration][diagnostic]") {
    const char* modeNames[] = {"Simple", "Granular", "PhaseVocoder", "PitchSync"};
    double modeNorms[] = {0.0, 1.0 / 3.0, 2.0 / 3.0, 1.0};
    double modeEnergies[4] = {};

    for (int mode = 0; mode < 4; ++mode) {
        Ruinae::Processor processor;
        processor.initialize(nullptr);
        Steinberg::Vst::ProcessSetup setup{};
        setup.processMode = Steinberg::Vst::kRealtime;
        setup.symbolicSampleSize = Steinberg::Vst::kSample32;
        setup.sampleRate = 44100.0;
        setup.maxSamplesPerBlock = 256;
        processor.setupProcessing(setup);
        processor.setActive(true);

        constexpr size_t kBlock = 256;
        std::vector<float> outL(kBlock, 0.0f);
        std::vector<float> outR(kBlock, 0.0f);
        float* channelBuffers[2] = { outL.data(), outR.data() };

        Steinberg::Vst::AudioBusBuffers outputBus{};
        outputBus.numChannels = 2;
        outputBus.channelBuffers32 = channelBuffers;

        Steinberg::Vst::ProcessData data{};
        data.processMode = Steinberg::Vst::kRealtime;
        data.symbolicSampleSize = Steinberg::Vst::kSample32;
        data.numSamples = static_cast<Steinberg::int32>(kBlock);
        data.numInputs = 0;
        data.inputs = nullptr;
        data.numOutputs = 1;
        data.outputs = &outputBus;
        data.processContext = nullptr;

        HarmEmptyEventList emptyEvents;
        data.inputEvents = &emptyEvents;

        // Enable harmonizer with this pitch mode, wet-only
        {
            HarmParamChanges params;
            params.addChange(Ruinae::kHarmonizerEnabledId, 1.0);
            params.addChange(Ruinae::kHarmonizerPitchShiftModeId, modeNorms[mode]);
            params.addChange(Ruinae::kHarmonizerNumVoicesId, 0.0);  // 1 voice
            params.addChange(Ruinae::kHarmonizerVoice1IntervalId, 31.0 / 48.0); // +7 st
            params.addChange(Ruinae::kHarmonizerVoice1LevelId, 60.0 / 66.0);    // 0 dB
            params.addChange(Ruinae::kHarmonizerVoice1PanId, 0.5);              // center
            params.addChange(Ruinae::kHarmonizerDryLevelId, 0.0);               // -60 dB
            params.addChange(Ruinae::kHarmonizerWetLevelId, 60.0 / 66.0);      // 0 dB
            std::fill(outL.begin(), outL.end(), 0.0f);
            std::fill(outR.begin(), outR.end(), 0.0f);
            data.inputParameterChanges = &params;
            processor.process(data);
        }

        // Start note
        {
            HarmNoteOnEvents noteEvents;
            HarmParamChanges empty;
            data.inputEvents = &noteEvents;
            data.inputParameterChanges = &empty;
            processor.process(data);
            data.inputEvents = &emptyEvents;
        }

        // Settle (200 blocks = ~1.16s at 44.1kHz/256 block)
        {
            HarmParamChanges empty;
            for (int b = 0; b < 200; ++b) {
                std::fill(outL.begin(), outL.end(), 0.0f);
                std::fill(outR.begin(), outR.end(), 0.0f);
                data.inputParameterChanges = &empty;
                processor.process(data);
            }
        }

        // Measure (100 blocks)
        double energy = 0.0;
        {
            HarmParamChanges empty;
            for (int b = 0; b < 100; ++b) {
                std::fill(outL.begin(), outL.end(), 0.0f);
                std::fill(outR.begin(), outR.end(), 0.0f);
                data.inputParameterChanges = &empty;
                processor.process(data);
                for (size_t s = 0; s < kBlock; ++s) {
                    energy += static_cast<double>(outL[s]) * outL[s]
                            + static_cast<double>(outR[s]) * outR[s];
                }
            }
        }

        double rms = std::sqrt(energy / (2.0 * 100.0 * kBlock));
        modeEnergies[mode] = energy;

        INFO(modeNames[mode] << ": Energy=" << energy << "  RMS=" << rms);
        // Each mode should produce audible output
        CHECK(energy > 1.0);

        processor.setActive(false);
        processor.terminate();
    }

    // Report comparison
    for (int mode = 0; mode < 4; ++mode) {
        double rms = std::sqrt(modeEnergies[mode] / (2.0 * 100.0 * 256));
        INFO(modeNames[mode] << ": RMS=" << rms
             << " ratio_vs_PitchSync="
             << (modeEnergies[3] > 0 ? modeEnergies[mode] / modeEnergies[3] : 0.0));
    }
    // All modes should be within 20dB of each other (10x energy)
    double maxE = *std::max_element(modeEnergies, modeEnergies + 4);
    double minE = *std::min_element(modeEnergies, modeEnergies + 4);
    CHECK(minE > maxE * 0.01); // within 20dB
}
